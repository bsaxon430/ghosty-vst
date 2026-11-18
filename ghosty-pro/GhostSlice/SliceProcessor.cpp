#include "SliceProcessor.h"
#include "SliceEditor.h"

using namespace gslice;

// ---------------------------------------------------------------- the on-screen keys -> the audio thread (lock-free)
struct SliceProcessor::KeyFifo : juce::MidiKeyboardState::Listener
{
    struct Ev { int note; float vel; bool on; };
    std::array<Ev, 256> buf {}; juce::AbstractFifo fifo { 256 };
    void handleNoteOn (juce::MidiKeyboardState*, int, int note, float vel) override { push ({ note, vel, true }); }
    void handleNoteOff (juce::MidiKeyboardState*, int, int note, float) override { push ({ note, 0.0f, false }); }
    void push (Ev e) { int s1, n1, s2, n2; fifo.prepareToWrite (1, s1, n1, s2, n2); if (n1 > 0) buf[(size_t) s1] = e; else if (n2 > 0) buf[(size_t) s2] = e; fifo.finishedWrite (n1 + n2); }
    template <typename F> void drain (F&& f) { int s1, n1, s2, n2; fifo.prepareToRead (fifo.getNumReady(), s1, n1, s2, n2); for (int i = 0; i < n1; ++i) f (buf[(size_t) (s1 + i)]); for (int i = 0; i < n2; ++i) f (buf[(size_t) (s2 + i)]); fifo.finishedRead (n1 + n2); }
};
namespace
{
uint32_t freshSeed() { auto& r = juce::Random::getSystemRandom(); uint32_t s = 0; while (s == 0) s = (uint32_t) r.nextInt() ^ (uint32_t) (juce::Time::getHighResolutionTicks() & 0xffffffff); return s; }
}

static bool standaloneNow() { return juce::JUCEApplicationBase::isStandaloneApp(); }

SliceProcessor::SliceProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input (RECORD)", juce::AudioChannelSet::stereo(), standaloneNow())
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    keyFifo = std::make_unique<KeyFifo>();
    keyboard.addListener (keyFifo.get());
    fx.space.store (0.4f); fx.tone.store (0.0f);
    startTimerHz (10);
}

SliceProcessor::~SliceProcessor()
{
    alive->store (false); sourceGen->store (false);
    stopTimer();
    mic.close();
    pool.removeAllJobs (true, 20000);
    keyboard.removeListener (keyFifo.get());
}

bool SliceProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    if (l.getMainOutputChannelSet() != juce::AudioChannelSet::stereo()) return false;
    const auto in = l.getMainInputChannelSet();
    return in.isDisabled() || in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void SliceProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    maxBlock = juce::jmax (16, samplesPerBlock);
    hostSr.store (sampleRate);
    engine.prepare (sampleRate, maxBlock);
    fx.prepare (sampleRate, maxBlock);
    ceiling.ceilingDb = -1.2f; ceiling.prepare (sampleRate);
    setLatencySamples (ceiling.latency());
    demo.prepare (sampleRate);
    kbMidi.ensureSize (16384);
}

void SliceProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    const int n = buffer.getNumSamples();
    if (n == 0) return;
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) if (auto bpm = pos->getBpm()) hostBpm.store (*bpm);
    // RECORD: the track input (written into a buffer made before recording started)
    const int inCh = juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels());
    float pk = 0;
    for (int c = 0; c < inCh; ++c) pk = std::max (pk, buffer.getMagnitude (c, 0, n));
    if (inCh > 0) level.store (std::max (pk, level.load() * 0.9f));
    if (recOn.load (std::memory_order_acquire) && inCh > 0 && recTrack.getNumSamples() > 0)
    {
        const int at = recTrackPos.load(), m = juce::jmin (n, recTrack.getNumSamples() - at);
        if (m > 0)
        {
            for (int c = 0; c < 2; ++c) recTrack.copyFrom (c, at, buffer, juce::jmin (c, inCh - 1), 0, m);
            recTrackPos.store (at + m);
            trackPeak.store (std::max (trackPeak.load(), pk));
        }
    }
    for (int c = 0; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
    if (buffer.getNumChannels() < 2) return;
    float* L = buffer.getWritePointer (0); float* R = buffer.getWritePointer (1);
    // MIDI: the host's + the on-screen keys
    kbMidi.clear();
    kbMidi.addEvents (midi, 0, n, 0);
    keyFifo->drain ([this] (const KeyFifo::Ev& e) { kbMidi.addEvent (e.on ? juce::MidiMessage::noteOn (1, e.note, e.vel) : juce::MidiMessage::noteOff (1, e.note), 0); });
    for (const auto m : kbMidi)   // (the VELOCITY meter)
    {
        const auto msg = m.getMessage();
        if (msg.isNoteOn()) { lastVel.store (msg.getFloatVelocity()); noteOns.fetch_add (1); }
    }
    engine.process (L, R, n, kbMidi);
    fx.process (L, R, n);
    {   // the UNDER THE HOOD meters: the take's level after the FX (peak, decaying on the message thread)
        float pl = 0, pr = 0; for (int i = 0; i < n; ++i) { pl = std::max (pl, std::abs (L[i])); pr = std::max (pr, std::abs (R[i])); }
        outL.store (std::max (outL.load (std::memory_order_relaxed), pl), std::memory_order_relaxed); outR.store (std::max (outR.load (std::memory_order_relaxed), pr), std::memory_order_relaxed);
    }
    if (const auto* ab = audPlay.load (std::memory_order_acquire))
    {   // the STYLE browser's audition (dry, after the FX)
        int pos = audPos.load(); const int m = juce::jmin (n, ab->getNumSamples() - pos);
        for (int i = 0; i < m; ++i) { L[i] += ab->getSample (0, pos + i); R[i] += ab->getSample (1, pos + i); }
        pos += juce::jmax (0, m); audPos.store (pos);
        if (pos >= ab->getNumSamples()) audPlay.store (nullptr, std::memory_order_release);
    }
    if (const auto* s = srcPlay.load (std::memory_order_acquire))
    {   // the sound itself (the play button under the waveform): dry, after the FX, linear interpolation to the host rate
        const double step = s->sampleRate / juce::jmax (1.0, hostSr.load()), end = srcEnd.load();
        double pos = srcPos.load();
        const int chs = s->audio.getNumChannels(); const int64 frames = (int64) s->audio.getNumSamples();
        if (chs > 0)
        {
            const float* a = s->audio.getReadPointer (0); const float* b = s->audio.getReadPointer (juce::jmin (1, chs - 1));
            const bool exact = std::abs (step - 1.0) < 1e-9 && std::abs (pos - std::round (pos)) < 1e-9;
            for (int i = 0; i < n && pos < end; ++i, pos += step)
            {
                const int64 k = (int64) pos;
                if (k + 1 >= frames) { pos = end; break; }
                if (exact) { L[i] += 0.8f * a[k]; R[i] += 0.8f * b[k]; continue; }   // (the host runs at the sound's rate: the samples themselves)
                // another rate: the windowed-sinc read (the engine's CLEAR read), never a straight line between samples
                L[i] += 0.8f * gen::dsp::readSinc (s->audio, 0, pos); R[i] += 0.8f * gen::dsp::readSinc (s->audio, juce::jmin (1, chs - 1), pos);
            }
        }
        srcPos.store (pos);
        if (pos >= end) srcPlay.store (nullptr, std::memory_order_release);
    }
    ceiling.process (L, R, n);
    demo.process (buffer, license.isUnlocked());
}

double SliceProcessor::tempo() const { return tempoFromDaw() ? hostBpm.load() : ownBpm; }

// ---------------------------------------------------------------- the sound
void SliceProcessor::installSource (std::shared_ptr<const SourceAudio> s, bool keepTakes, bool restoring)
{
    sourceGen->store (false); sourceGen = std::make_shared<std::atomic<bool>> (true);   // (old jobs drop their results)
    playSource (false);
    source = s;
    { std::lock_guard<std::mutex> l (prepMutex); prepared = nullptr; }
    if (! keepTakes)
    {
        slots.clear(); current = -1; renderedOrder.clear(); takeCounter = 0; engine.publish (nullptr); undoSteps.clear(); redoSteps.clear();
        if (! restoring) { trim0 = 0.0; trim1 = 1.0; if (! keyLock) keyTonic = -1; }   // (the SCALE lock keeps the key)
    }
    else { for (auto& sl : slots) sl.take = nullptr; renderedOrder.clear(); engine.publish (nullptr); }
    queuePrepare();
    if (keepTakes && current >= 0) queueRender (current, true, false);
    changed();
}

bool SliceProcessor::loadAudio (const juce::File& f, juce::String& error)
{
    auto s = source::load (f, error);
    if (s == nullptr) return false;
    installSource (s, false);
    lastMessage = "Loaded " + f.getFileName();
    return true;
}

bool SliceProcessor::loadSourceForTests (std::shared_ptr<const SourceAudio> s) { if (s == nullptr) return false; installSource (s, false); return true; }

void SliceProcessor::loadAudioAsync (const juce::File& f)
{
    loading.store (true); changed();
    auto flag = alive;
    juce::Thread::launch ([this, f, flag]
    {
        juce::String err; auto s = source::load (f, err);
        juce::MessageManager::callAsync ([this, f, flag, s, err]
        {
            if (! flag->load()) return;
            loading.store (false);
            if (s != nullptr)
            {
                const bool same = waitingFor.isNotEmpty() && s->fingerprint == waitingFor;   // the session's missing sound, dropped again: its takes come back
                waitingFor.clear();
                installSource (s, same, same);
                toast (same ? "Found it: your takes are back." : "Loaded " + f.getFileName() + ". Press GENERATE.");
            }
            else toast (err);
            changed();
        });
    });
}

gen::Key SliceProcessor::detectedKey() const
{
    if (auto P = getPrepared()) return P->detected;
    gen::Key k; k.tonic = 0; k.minor = true; return k;
}

void SliceProcessor::queuePrepare()
{
    if (source == nullptr) return;
    jobs.fetch_add (1); prog.store (0.0f);
    { std::lock_guard<std::mutex> l (prepMutex); busyWhat = "LISTENING TO YOUR SOUND"; }
    auto flag = alive; auto gen = sourceGen; auto src = source; const double tr0 = trim0, tr1 = trim1;
    pool.addJob (std::function<juce::ThreadPoolJob::JobStatus()> ([this, flag, gen, src, tr0, tr1]
    {
        gen::Progress p; p.report = [this] (float f) { prog.store (f); }; p.cancelled = [flag, gen] { return ! flag->load() || ! gen->load(); };
        const double t0 = tr0, t1 = tr1;
        auto P = prepareFor (src, t0, t1, p);
        if (flag->load() && gen->load()) { std::lock_guard<std::mutex> l (prepMutex); prepared = P; prepT0 = t0; prepT1 = t1; }
        juce::MessageManager::callAsync ([this, flag, gen, P]
        {
            if (! flag->load()) return;
            jobs.fetch_sub (1);
            if (! gen->load()) return;
            if (P == nullptr) toast ("That sound is too short or silent. Try another one.");
            changed();
        });
        return juce::ThreadPoolJob::jobHasFinished;
    }));
}

// the analysis + material of the part [t0, t1] (0..1) of the sound (the whole sound: as it is)
std::shared_ptr<const gen::Prepared> SliceProcessor::prepareFor (std::shared_ptr<const SourceAudio> src, double t0, double t1, const gen::Progress& p)
{
    if (src == nullptr) return nullptr;
    if (t0 <= 1e-6 && t1 >= 1.0 - 1e-6) return gen::prepare (src, nullptr, p);
    const int64 a = juce::jlimit ((int64) 0, src->frames, (int64) std::llround (t0 * (double) src->frames));
    const int64 b = juce::jlimit (a + 1, src->frames, (int64) std::llround (t1 * (double) src->frames));
    juce::AudioBuffer<float> part (juce::jmax (1, src->channels), (int) (b - a));
    for (int c = 0; c < part.getNumChannels(); ++c) part.copyFrom (c, 0, src->audio, juce::jmin (c, src->channels - 1), (int) a, (int) (b - a));
    return gen::prepare (source::fromBuffer (std::move (part), src->sampleRate, src->file.getFileName()), nullptr, p);
}

std::shared_ptr<const gen::Prepared> SliceProcessor::preparedFor (double t0, double t1) const
{
    std::lock_guard<std::mutex> l (prepMutex);
    return std::abs (prepT0 - t0) < 1e-9 && std::abs (prepT1 - t1) < 1e-9 ? prepared : nullptr;
}

void SliceProcessor::setTrim (double a, double b)
{
    a = juce::jlimit (0.0, 1.0, a); b = juce::jlimit (0.0, 1.0, b);
    if (b < a) std::swap (a, b);
    if (source != nullptr)
    {   // at least half a second (or the whole sound when it is shorter)
        const double minF = juce::jmin (1.0, 0.5 / juce::jmax (1e-3, source->seconds()));
        if (b - a < minF) { if (a + minF <= 1.0) b = a + minF; else { b = 1.0; a = 1.0 - minF; } }
    }
    if (std::abs (a - trim0) < 1e-9 && std::abs (b - trim1) < 1e-9) return;
    trim0 = a; trim1 = b;
    playSource (false);
    queuePrepare();   // (listens to that part: its key, its material)
    changed();
}

void SliceProcessor::playSource (bool on)
{
    if (on) stopAudition();
    if (! on || source == nullptr)
    {
        srcPlay.store (nullptr, std::memory_order_release);
        if (srcPlayHold != nullptr) { srcPlayOld = std::move (srcPlayHold); srcPlayOldAt = juce::Time::getMillisecondCounterHiRes(); }
        return;
    }
    engine.playPreview (false);
    srcPlay.store (nullptr, std::memory_order_release);
    if (srcPlayHold != nullptr && srcPlayHold != source) { srcPlayOld = std::move (srcPlayHold); srcPlayOldAt = juce::Time::getMillisecondCounterHiRes(); }
    srcPlayHold = source;
    srcPos.store (trim0 * (double) source->frames); srcEnd.store (trim1 * (double) source->frames);
    srcPlay.store (source.get(), std::memory_order_release);
}

double SliceProcessor::sourcePosition() const
{
    const auto* s = srcPlay.load();
    return s != nullptr && s->sampleRate > 0 ? srcPos.load() / s->sampleRate : -1.0;
}

// ---------------------------------------------------------------- takes
gen::Recipe SliceProcessor::recipeNow() const
{
    gen::Recipe r;
    r.type = type; r.musical = musical; r.keyTonic = keyTonic; r.keyMinor = keyMinor; r.character = character; r.bpm = tempo(); r.clear = clear;
    r.engine = 8; r.style = style; r.bars = lengthBars;
    applyKnobs (r, gen::resolveType (r));
    playSettingsInto (r.edit, play);   // (build 8: MASTER / ARP / CHORDS carry on to new takes; notes and swaps are the take's own)
    return r;
}

// ---------------------------------------------------------------- build 6: the third knob, the melody style, the UNDER THE HOOD knobs
void SliceProcessor::applyKnobs (gen::Recipe& r, gen::Type t) const
{
    const int ti = juce::jlimit (0, 4, (int) t);
    r.knob = knobs[(size_t) ti]; r.deep = deeps[(size_t) ti];
}

gen::Type SliceProcessor::knobType() const
{
    if (type != gen::Type::Auto) return type;
    if (current >= 0 && current < (int) slots.size()) return slotType (current);
    return gen::Type::Instrument;
}

double SliceProcessor::knobValue() const
{
    const auto t = knobType(); gen::Recipe r; r.knob = knobs[(size_t) juce::jlimit (0, 4, (int) t)];
    return r.knobOr (t);
}

double SliceProcessor::deepValue (int i) const
{
    const auto t = knobType(); gen::Recipe r; r.clear = clear; r.deep = deeps[(size_t) juce::jlimit (0, 4, (int) t)];
    return r.deepOr (t, i);
}

bool SliceProcessor::rerenderCurrentFor (gen::Type t)
{   // the take on screen, when it is of this type and made by build 6: it re-renders with the knobs as they are now (same idea)
    if (current < 0 || current >= (int) slots.size() || source == nullptr) return false;
    auto& sl = slots[(size_t) current];
    if (sl.recipe.engine < 6 || slotType (current) != t) return false;
    auto r = sl.recipe; applyKnobs (r, t); r.style = style; r.bars = lengthBars;
    if (r.knob == sl.recipe.knob && r.deep == sl.recipe.deep && r.style == sl.recipe.style && r.bars == sl.recipe.bars) return false;
    sl.recipe.knob = r.knob; sl.recipe.deep = r.deep; sl.recipe.style = r.style; sl.recipe.bars = r.bars;
    queueRender (current, true, true);
    changed();
    return true;
}

void SliceProcessor::setKnob (double v, bool rerender)
{
    const auto t = knobType();
    knobs[(size_t) juce::jlimit (0, 4, (int) t)] = v < 0 ? -1.0 : juce::jlimit (0.0, 1.0, v);
    if (rerender) rerenderCurrentFor (t);
}

void SliceProcessor::setDeep (int i, double v, bool rerender)
{
    const auto t = knobType();
    deeps[(size_t) juce::jlimit (0, 4, (int) t)][(size_t) juce::jlimit (0, gen::Recipe::kDeep - 1, i)] = v < 0 ? -1.0 : juce::jlimit (0.0, 1.0, v);
    if (rerender) rerenderCurrentFor (t);
}

void SliceProcessor::setLength (int bars, bool rerender)
{
    lengthBars = bars <= 1 ? 1 : bars <= 2 ? 2 : bars <= 4 ? 4 : bars <= 8 ? 8 : 16;
    if (rerender) rerenderCurrentFor (knobType());
}

std::vector<gen::Style> SliceProcessor::styleOrder()
{   // AUTO, then every style group by group (the brain's own ACID duplicate skipped: our ACID is the same generator)
    std::vector<gen::Style> order { gen::Style::Auto };
    for (int i = 1; i < gen::styleCount(); ++i) { const auto s = (gen::Style) i; if (i >= gen::kStyles && juce::String (gen::styleName (s)) == "ACID") continue; order.push_back (s); }
    std::stable_sort (order.begin() + 1, order.end(), [] (gen::Style a, gen::Style b) { return gen::styleGroupIndex (a) < gen::styleGroupIndex (b); });
    return order;
}

void SliceProcessor::stopAudition()
{
    audPlay.store (nullptr, std::memory_order_release);
    if (audHold != nullptr) { audOld = std::move (audHold); audOldAt = juce::Time::getMillisecondCounterHiRes(); }
}

void SliceProcessor::auditionStyle (gen::Style st)
{
    stopAudition(); engine.playPreview (false); playSource (false);
    const auto key = keyTonic >= 0 ? gen::Key { keyTonic, keyMinor } : detectedKey();
    const auto t = st == gen::Style::Auto ? gen::resolveStyle (st, (int) gen::resolveType (recipeNow()), 1234u) : st;
    const auto song = gen::compose (key, 64, t, 2, 1234u, 77u, 0, 0.5, 0.5, {}, tempo());
    const double sr = juce::jmax (8000.0, hostSr.load()), spb = 60.0 / juce::jmax (40.0, tempo());
    const int n = (int) ((8.0 * spb + 1.2) * sr);
    auto buf = std::make_shared<juce::AudioBuffer<float>> (2, n); buf->clear();
    auto tone = [&] (int note, double t0, double len, float vel, float level, double decayS)
    {   // a soft bell: three sine partials, a quick attack, an exponential decay, released at the note's end
        const double hz = gen::dsp::midiToHz (note); const int a = (int) (t0 * sr), m = juce::jmin (n - a, (int) ((len + 0.25) * sr));
        for (int i = 0; i < m; ++i)
        {
            const double tt = i / sr, env = juce::jmin (1.0, tt / 0.006) * std::exp (-tt / decayS) * (tt > len ? std::exp (-(tt - len) / 0.06) : 1.0);
            const double v = env * vel * level * (std::sin (juce::MathConstants<double>::twoPi * hz * tt) + 0.25 * std::sin (juce::MathConstants<double>::twoPi * 2.0 * hz * tt) + 0.08 * std::sin (juce::MathConstants<double>::twoPi * 3.0 * hz * tt));
            buf->addSample (0, a + i, (float) v); buf->addSample (1, a + i, (float) v);
        }
    };
    for (size_t i = 0; i < song.chords.size(); ++i)   // the chords under it, quiet
        for (int nn : song.chords[i].notes) { const double b0 = i * song.beatsPerChord; if (b0 < 8.0) tone (nn, b0 * spb, juce::jmin (song.beatsPerChord, 8.0 - b0) * spb, 0.6f, 0.05f, 1.6); }
    for (auto& e : song.melody) if (e.beat < 8.0) tone (e.note, e.beat * spb, juce::jmax (0.05, e.length) * spb, e.velocity, 0.16f, 0.9);
    gen::dsp::fadeEdges (*buf, 16, (int) (0.05 * sr));
    audHold = buf; audPos.store (0); audPlay.store (audHold.get(), std::memory_order_release);
}

void SliceProcessor::stepStyle (int d)
{
    const auto order = styleOrder();
    int at = 0; for (size_t i = 0; i < order.size(); ++i) if (order[i] == style) at = (int) i;
    setStyle (order[(size_t) ((at + d + (int) order.size()) % (int) order.size())], true);
}

void SliceProcessor::setStyle (gen::Style s, bool rerender)
{
    style = s;
    if (rerender) rerenderCurrentFor (knobType());
}

gen::Type SliceProcessor::slotType (int i) const
{
    if (i < 0 || i >= (int) slots.size()) return gen::Type::Instrument;
    return slots[(size_t) i].take != nullptr ? slots[(size_t) i].take->type : gen::resolveType (slots[(size_t) i].recipe);
}

juce::String SliceProcessor::slotKey (int i) const
{
    if (i < 0 || i >= (int) slots.size()) return {};
    if (auto& t = slots[(size_t) i].take) return t->keyName();
    const auto& r = slots[(size_t) i].recipe;
    const auto k = r.keyTonic >= 0 ? gen::Key { r.keyTonic, r.keyMinor } : detectedKey();
    return analysis::pitchClassName (k.tonic) + (k.minor ? " MIN" : " MAJ");
}

void SliceProcessor::generate()
{
    if (source == nullptr) { toast ("Drop a vocal or any sound first (or press RECORD)."); return; }
    auto r = recipeNow();
    r.seed = freshSeed(); r.detail = freshSeed();
    if (type == gen::Type::Auto) { r.rotation = rotation++; applyKnobs (r, gen::resolveType (r)); }
    Slot s; s.recipe = r; s.index = ++takeCounter; s.trim0 = trim0; s.trim1 = trim1;
    s.recipe.name = "Take " + juce::String (s.index) + " - " + gen::typeName (gen::resolveType (r));
    slots.push_back (s);
    current = (int) slots.size() - 1;
    engine.playPreview (false);
    queueRender (current, true, true);
    changed();
}

void SliceProcessor::vary()
{
    if (current < 0 || current >= (int) slots.size()) { generate(); return; }
    auto r = slots[(size_t) current].recipe;
    // the take's type and structure stay; the details are new, the controls are the ones set now
    const auto resolved = gen::resolveType (r);
    r.type = resolved; r.detail = freshSeed(); r.musical = musical; r.character = character; r.keyTonic = keyTonic; r.keyMinor = keyMinor; r.bpm = tempo(); r.favourite = false; r.clear = clear;
    // build 6: the same idea (seed, type, key, tempo, style, the user's boxes) developed: a new motif answer and B phrase, another
    // voicing / a substitute chord, other chops in another order, a new grain and freeze path (Melody.h, GenerateV6.inl). The knobs
    // are the ones set now for its type. A take from before build 6 varies with the build 6 engine.
    r.engine = 8; applyKnobs (r, resolved); if (style != gen::Style::Auto) r.style = style; r.bars = lengthBars;
    // build 8: your notes stay (locked) and so do your chord swaps; what you removed from the old take does not carry over (VARY's own
    // notes are new)
    r.edit.removed.clear(); playSettingsInto (r.edit, play);
    const int index = slots[(size_t) current].index;
    int v = 0; for (auto& s : slots) if (s.index == index) v = std::max (v, s.recipe.variation);
    r.variation = v + 1;
    Slot s; s.recipe = r; s.index = index; s.trim0 = slots[(size_t) current].trim0; s.trim1 = slots[(size_t) current].trim1;
    s.recipe.name = gen::takeName (index, resolved, r.keyTonic >= 0 ? gen::Key { r.keyTonic, r.keyMinor } : detectedKey(), r.variation);
    slots.insert (slots.begin() + current + 1, s);
    for (auto& i : renderedOrder) if (i > current) ++i;
    ++current;
    engine.playPreview (false);
    queueRender (current, true, true);
    changed();
}

void SliceProcessor::queueRender (int i, bool makeCurrent, bool playWhenDone)
{
    if (i < 0 || i >= (int) slots.size() || source == nullptr) return;
    const auto r = slots[(size_t) i].recipe; const int index = slots[(size_t) i].index;
    const double t0 = slots[(size_t) i].trim0, t1 = slots[(size_t) i].trim1; auto src = source;
    auto old = slots[(size_t) i].take;   // (build 8: an edit on the roll reuses its zones; generate() checks it is the same sound)
    jobs.fetch_add (1); prog.store (0.0f);
    { std::lock_guard<std::mutex> l (prepMutex); busyWhat = "MAKING TAKE " + juce::String (index) + (r.variation > 0 ? juce::String::charToString ((juce::juce_wchar) ('a' + r.variation)) : juce::String()); }
    auto flag = alive; auto gen = sourceGen;
    pool.addJob (std::function<juce::ThreadPoolJob::JobStatus()> ([this, flag, gen, r, index, makeCurrent, playWhenDone, t0, t1, src, old]
    {
        std::shared_ptr<const gen::Take> t;
        auto P = preparedFor (t0, t1);
        if (P == nullptr && src != nullptr && flag->load() && gen->load())
        {   // a take made from another TRIM part than the one listened to last: listen to its part first
            gen::Progress pp; pp.cancelled = [flag, gen] { return ! flag->load() || ! gen->load(); };
            P = prepareFor (src, t0, t1, pp);
            if (P != nullptr && flag->load() && gen->load()) { std::lock_guard<std::mutex> l (prepMutex); prepared = P; prepT0 = t0; prepT1 = t1; }
        }
        if (P != nullptr && flag->load() && gen->load())
        {
            gen::Progress p; p.report = [this] (float f) { prog.store (0.05f + 0.95f * f); }; p.cancelled = [flag, gen] { return ! flag->load() || ! gen->load(); };
            lastRenderOnMessageThread.store (juce::MessageManager::getInstance()->isThisTheMessageThread());
            auto rr = r;   // (the parts in the prepared sound's seconds: it starts at the TRIM start)
            if (src != nullptr && t0 > 0.0) for (auto& q : rr.parts) { q.first = juce::jmax (0.0, q.first - t0 * src->seconds()); q.second = juce::jmax (q.first, q.second - t0 * src->seconds()); }
            const double tr0 = juce::Time::getMillisecondCounterHiRes();
            auto tk = gen::generate (*P, rr, p, old);
            lastRenderMsV.store (juce::Time::getMillisecondCounterHiRes() - tr0); renderCount.fetch_add (1);
            if (tk != nullptr) tk->recipe.parts = r.parts;
            if (tk != nullptr)
            {   // the take's name, now that its type and key are known; the parts it used in the whole sound's seconds
                tk->recipe.name = gen::takeName (index, tk->type, gen::Key { tk->keyTonic, tk->keyMinor }, r.variation);
                if (src != nullptr && t0 > 0.0) for (auto& u : tk->used) { u.first += t0 * src->seconds(); u.second += t0 * src->seconds(); }
                t = tk;
            }
        }
        juce::MessageManager::callAsync ([this, flag, gen, t, r, index, makeCurrent, playWhenDone]
        {
            if (! flag->load()) return;
            jobs.fetch_sub (1);
            if (! gen->load()) return;
            int at = -1;
            at = slotFor (index, r.variation, r.seed, r.detail);
            if (at < 0) return;   // (deleted meanwhile)
            if (slots[(size_t) at].recipe.parts != r.parts) return;   // (edited again meanwhile: the newer render follows)
            { const auto& now = slots[(size_t) at].recipe; if (now.knob != r.knob || now.deep != r.deep || now.style != r.style || now.bars != r.bars || now.edit != r.edit || now.engine != r.engine) return; }   // (a knob moved meanwhile)
            if (t == nullptr) { toast ("That take did not work out. Press GENERATE again."); return; }
            const bool fav = slots[(size_t) at].recipe.favourite;
            slots[(size_t) at].take = t; slots[(size_t) at].recipe.name = t->recipe.name; slots[(size_t) at].recipe.favourite = fav;
            renderedOrder.erase (std::remove (renderedOrder.begin(), renderedOrder.end(), at), renderedOrder.end()); renderedOrder.push_back (at);
            if (makeCurrent && current == at) { publishCurrent(); if (playWhenDone) engine.playPreview (true); }
            trimMemory();
            changed();
        });
        return juce::ThreadPoolJob::jobHasFinished;
    }));
}

int SliceProcessor::slotFor (int index, int variation, uint32_t seed, uint32_t detail) const
{
    for (size_t k = 0; k < slots.size(); ++k)
        if (slots[k].index == index && slots[k].recipe.variation == variation && slots[k].recipe.seed == seed && slots[k].recipe.detail == detail) return (int) k;
    return -1;
}

// ---------------------------------------------------------------- the parts (the boxes)
SliceProcessor::Parts SliceProcessor::takeParts() const
{
    if (current < 0 || current >= (int) slots.size()) return {};
    const auto& sl = slots[(size_t) current];
    if (! sl.recipe.parts.empty()) return sl.recipe.parts;
    if (sl.take == nullptr) return {};
    Parts u = sl.take->used; std::sort (u.begin(), u.end());
    Parts m;   // (overlapping parts of the generator's joined: boxes never overlap)
    for (auto& q : u) { if (! m.empty() && q.first < m.back().second) m.back().second = std::max (m.back().second, q.second); else m.push_back (q); }
    const int cap = maxParts();
    while ((int) m.size() > cap)
    {   // (more than the type takes: join the two closest)
        size_t bi = 0; double bg = 1e9; for (size_t i = 0; i + 1 < m.size(); ++i) if (m[i + 1].first - m[i].second < bg) { bg = m[i + 1].first - m[i].second; bi = i; }
        m[bi].second = m[bi + 1].second; m.erase (m.begin() + (long) bi + 1);
    }
    return m;
}

bool SliceProcessor::partsEdited() const { return current >= 0 && current < (int) slots.size() && ! slots[(size_t) current].recipe.parts.empty(); }
int SliceProcessor::maxParts() const { return gen::Recipe::maxParts (slotType (current)); }

juce::Range<double> SliceProcessor::partLimits() const
{
    if (source == nullptr) return {};
    const double secs = source->seconds();
    if (current < 0 || current >= (int) slots.size()) return { trim0 * secs, trim1 * secs };
    return { slots[(size_t) current].trim0 * secs, slots[(size_t) current].trim1 * secs };
}

bool SliceProcessor::editParts (Parts parts, const juce::String& what)
{
    if (current < 0 || current >= (int) slots.size() || source == nullptr) return false;
    const auto lim = partLimits();
    std::sort (parts.begin(), parts.end());
    Parts ok;
    for (auto q : parts)
    {
        q.first = juce::jlimit (lim.getStart(), lim.getEnd(), q.first); q.second = juce::jlimit (q.first, lim.getEnd(), q.second);
        if (! ok.empty() && q.first < ok.back().second) q.first = ok.back().second;   // (never overlapping)
        if (q.second - q.first >= 0.02) ok.push_back (q);
    }
    if (ok.empty() || (int) ok.size() > maxParts()) return false;
    auto& sl = slots[(size_t) current];
    if (ok == takeParts() && ! sl.recipe.parts.empty()) return false;
    undoSteps.push_back (stepOf (sl, false)); redoSteps.clear();
    if (undoSteps.size() > 100) undoSteps.erase (undoSteps.begin());
    sl.recipe.parts = ok;
    queueRender (current, true, false);
    if (what.isNotEmpty()) toast (what);
    changed();
    return true;
}

bool SliceProcessor::undoEdit()
{
    while (! undoSteps.empty())
    {
        const auto u = undoSteps.back(); undoSteps.pop_back();
        const int at = slotFor (u.index, u.variation, u.seed, u.detail);
        if (at < 0) continue;   // (that take was deleted)
        redoSteps.push_back (stepOf (slots[(size_t) at], u.notes));
        slots[(size_t) at].recipe.parts = u.parts; slots[(size_t) at].recipe.edit = u.edit; if (u.notes) slots[(size_t) at].recipe.engine = u.engine;
        if (at != current) { current = at; engine.playPreview (false); }
        playSettingsInto (play, u.edit);
        queueRender (at, true, false);
        toast (u.notes ? "Undone." : u.parts.empty() ? "Undone: the take's own parts are back." : "Undone.");
        changed();
        return true;
    }
    return false;
}

bool SliceProcessor::redoEdit()
{
    while (! redoSteps.empty())
    {
        const auto u = redoSteps.back(); redoSteps.pop_back();
        const int at = slotFor (u.index, u.variation, u.seed, u.detail);
        if (at < 0) continue;
        undoSteps.push_back (stepOf (slots[(size_t) at], u.notes));
        slots[(size_t) at].recipe.parts = u.parts; slots[(size_t) at].recipe.edit = u.edit; if (u.notes) slots[(size_t) at].recipe.engine = u.engine;
        if (at != current) { current = at; engine.playPreview (false); }
        playSettingsInto (play, u.edit);
        queueRender (at, true, false);
        toast ("Redone.");
        changed();
        return true;
    }
    return false;
}

// ---------------------------------------------------------------- build 8: PLAY IT
void SliceProcessor::playSettingsInto (gen::NoteEdit& d, const gen::NoteEdit& s)
{
    d.master = s.master; d.arp = s.arp; d.arpRate = s.arpRate; d.arpOct = s.arpOct; d.chords = s.chords; d.grid = s.grid; d.bed = s.bed;
}
gen::NoteEdit SliceProcessor::takeEdit() const
{
    if (current >= 0 && current < (int) slots.size()) return slots[(size_t) current].recipe.edit;
    gen::NoteEdit e; playSettingsInto (e, play); return e;
}
bool SliceProcessor::editable() const
{
    if (current < 0 || current >= (int) slots.size()) return false;
    const auto t = slots[(size_t) current].take;
    return t != nullptr;   // (build 9: older takes too, upgraded on their first edit)
}
bool SliceProcessor::setEdit (gen::NoteEdit e, const juce::String& what)
{
    if (! editable() || source == nullptr) return false;
    auto& sl = slots[(size_t) current];
    // (your notes stay in the key whatever the caller did: the editor snaps them, this makes sure)
    // (times / amounts on a 1/1024 grid: exact in binary and in the session's JSON, so a reopened session is bit for bit the same)
    auto q = [] (double x) { return std::round (x * 1024.0) / 1024.0; };
    if (auto t = sl.take) { const gen::Key k { t->keyTonic, t->keyMinor }; for (auto& n : e.locked) { n.note = gen::edit::snapPitch (n.note, k); n.mine = true; n.beat = q (n.beat); n.length = juce::jmax (1.0 / 1024.0, q (n.length)); n.velocity = (float) q (n.velocity); n.linkAmt = (float) q (n.linkAmt); } }
    for (auto& r : e.rests) { const double a = std::floor (r.first * 1024.0) / 1024.0, b = std::ceil ((r.first + r.second) * 1024.0) / 1024.0; r = { a, juce::jmax (1.0 / 1024.0, b - a) }; }
    int engineTo = sl.recipe.engine;
    Parts upParts;   // (build 10: an upgraded take's parts, set after the undo step is taken)
    if (sl.recipe.engine < 6 && sl.take != nullptr)
    {   // build 9: an older take, upgraded on its first edit. Its notes (read back from its MIDI) become yours, so the melody you see stays
        // where it is; then the edit you made applies. Build 10: the parts of the sound it used become its parts (the same syllables,
        // the same chops, in the same order), and CHOPS keep their phrase too (the old take's notes carry their chop numbers now).
        const gen::Key k { sl.take->keyTonic, sl.take->keyMinor };
        gen::NoteEdit up = e; up.locked.clear(); up.removed.clear(); up.rests.clear();
        const bool m = sl.take->type == gen::Type::Instrument || sl.take->type == gen::Type::Chops;
        for (auto n : gen::notesOf (*sl.take))
        {
            bool gone = false; for (auto& r : e.removed) if (std::abs (r.first - n.beat) < 1e-4 && (m || r.second == n.note)) gone = true;
            if (gone) continue;
            n.note = gen::edit::snapPitch (n.note, k); n.mine = true; n.beat = q (n.beat); n.length = juce::jmax (1.0 / 1024.0, q (n.length)); n.velocity = (float) q (n.velocity);
            up.locked.push_back (n);
        }
        for (auto& n : e.locked) up.locked.push_back (n);
        up.onlyMine = true;
        {   // its parts: the take's own (sorted, never overlapping, as many as the type uses)
            Parts ps = sl.take->used; std::sort (ps.begin(), ps.end()); Parts ok;
            for (auto p2 : ps) { if (! ok.empty() && p2.first < ok.back().second) p2.first = ok.back().second; if (p2.second - p2.first >= 0.02) ok.push_back (p2); }
            if ((int) ok.size() > gen::Recipe::maxParts (sl.take->type)) ok.resize ((size_t) gen::Recipe::maxParts (sl.take->type));
            if (! ok.empty() && sl.recipe.parts.empty()) upParts = ok;
        }
        e = up; engineTo = 8;
    }
    if (e == sl.recipe.edit && engineTo == sl.recipe.engine) return false;
    undoSteps.push_back (stepOf (sl, true)); redoSteps.clear();
    if (undoSteps.size() > 100) undoSteps.erase (undoSteps.begin());
    sl.recipe.engine = engineTo;
    if (! upParts.empty()) sl.recipe.parts = upParts;
    sl.recipe.edit = std::move (e);
    playSettingsInto (play, sl.recipe.edit);
    queueRender (current, true, previewPlaying());
    if (what.isNotEmpty()) toast (what);
    changed();
    return true;
}
bool SliceProcessor::setPlay (const std::function<void (gen::NoteEdit&)>& fn, const juce::String& what)
{
    fn (play);
    if (! editable()) { if (what.isNotEmpty()) toast (what + (slots.empty() ? ". Press GENERATE." : "")); changed(); return false; }
    auto e = slots[(size_t) current].recipe.edit; fn (e);
    return setEdit (e, what);
}

void SliceProcessor::playRegion (double a, double b)
{
    if (source == nullptr) return;
    playSource (false);
    engine.playPreview (false);
    const double sr = source->sampleRate;
    srcPlayHold = source;
    srcPos.store (juce::jlimit (0.0, (double) source->frames, a * sr)); srcEnd.store (juce::jlimit (0.0, (double) source->frames, b * sr));
    srcPlay.store (source.get(), std::memory_order_release);
}

void SliceProcessor::publishCurrent()
{
    auto t = currentTake();
    engine.publish (t);
    if (t != nullptr) { fx.ensemble.store (t->ensemble); fx.clear.store (t->clear()); fx.dream.store (t->dream); }
    engine.collectGarbage();
}

void SliceProcessor::trimMemory()
{   // the audio of at most 4 takes, and about 160 MB, stays in memory (the current one always); the rest come back (exactly) when picked
    auto total = [this] { size_t b = 0; for (int i : renderedOrder) if (i >= 0 && i < (int) slots.size() && slots[(size_t) i].take != nullptr) b += slots[(size_t) i].take->bytes(); return b; };
    while (renderedOrder.size() > 4 || (renderedOrder.size() > 1 && total() > (size_t) 160 * 1024 * 1024))
    {
        auto it = std::find_if (renderedOrder.begin(), renderedOrder.end(), [this] (int i) { return i != current; });
        if (it == renderedOrder.end()) break;
        if (*it >= 0 && *it < (int) slots.size()) slots[(size_t) *it].take = nullptr;
        renderedOrder.erase (it);
    }
}

void SliceProcessor::selectTake (int i)
{
    if (i < 0 || i >= (int) slots.size()) return;
    engine.playPreview (false);
    current = i;
    playSettingsInto (play, slots[(size_t) i].recipe.edit);   // (build 8: PLAY IT shows this take's settings)
    if (slots[(size_t) i].take != nullptr)
    {
        renderedOrder.erase (std::remove (renderedOrder.begin(), renderedOrder.end(), i), renderedOrder.end()); renderedOrder.push_back (i);
        publishCurrent(); engine.playPreview (true);
    }
    else { engine.publish (nullptr); queueRender (i, true, true); }
    changed();
}

void SliceProcessor::deleteTake (int i)
{
    if (i < 0 || i >= (int) slots.size()) return;
    engine.playPreview (false);
    slots.erase (slots.begin() + i);
    renderedOrder.erase (std::remove (renderedOrder.begin(), renderedOrder.end(), i), renderedOrder.end());
    for (auto& k : renderedOrder) if (k > i) --k;
    if (current >= (int) slots.size()) current = (int) slots.size() - 1;
    else if (current > i) --current;
    if (slots.empty()) { current = -1; engine.publish (nullptr); }
    else if (slots[(size_t) current].take != nullptr) publishCurrent();
    else { engine.publish (nullptr); queueRender (current, true, false); }
    changed();
}

void SliceProcessor::toggleFavourite (int i)
{
    if (i < 0 || i >= (int) slots.size()) return;
    slots[(size_t) i].recipe.favourite = ! slots[(size_t) i].recipe.favourite;
    changed();
}

bool SliceProcessor::waitIdle (int timeoutMs)
{
   #if JUCE_MODAL_LOOPS_PERMITTED   // (tests only: the plugin never waits)
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    while (juce::Time::getMillisecondCounterHiRes() - t0 < timeoutMs)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        if (jobs.load() == 0) { juce::MessageManager::getInstance()->runDispatchLoopUntil (5); return true; }
    }
    return false;
   #else
    juce::ignoreUnused (timeoutMs);
    return jobs.load() == 0;
   #endif
}

void SliceProcessor::timerCallback()
{
    engine.collectGarbage();
    level.store (level.load() * 0.85f);
    if (srcPlay.load() == nullptr && srcPlayHold != nullptr) { srcPlayOld = std::move (srcPlayHold); srcPlayOldAt = juce::Time::getMillisecondCounterHiRes(); }
    if (srcPlayOld != nullptr && juce::Time::getMillisecondCounterHiRes() - srcPlayOldAt > 500.0) srcPlayOld = nullptr;   // (the audio thread let go of it)
    if (audPlay.load() == nullptr && audHold != nullptr) { audOld = std::move (audHold); audOldAt = juce::Time::getMillisecondCounterHiRes(); }
    if (audOld != nullptr && juce::Time::getMillisecondCounterHiRes() - audOldAt > 500.0) audOld = nullptr;
}

// ---------------------------------------------------------------- RECORD
double SliceProcessor::recordedSeconds() const
{
    const double a = recTrackRate > 0 ? recTrackPos.load() / recTrackRate : 0.0, b = recMicRate > 0 ? recMicPos.load() / recMicRate : 0.0;
    return std::max (a, b);
}

juce::String SliceProcessor::recordSourceText() const
{
    if (input == Input::Mic) return "MAC MIC";
    if (input == Input::Track) return "TRACK INPUT";
    return isHosted() ? "TRACK INPUT, ELSE THE MAC MIC" : "THE INPUT";
}

bool SliceProcessor::startRecording()
{
    if (recOn.load()) return true;
    engine.playPreview (false);
    const double sr = hostSr.load();
    recTrackRate = sr; recTrack.setSize (2, (int) (kMaxRecordS * sr)); recTrack.clear(); recTrackPos.store (0); trackPeak.store (0);
    recMic.setSize (0, 0); recMicPos.store (0); micPeak.store (0);
    const bool wantMic = input == Input::Mic || (input == Input::Auto && isHosted());
    if (wantMic)
    {   // the buffer and the callback exist before the device starts (its thread may call the sink at once); a mic faster than
        // 48 kHz simply records a shorter maximum
        mic.close();
        recMic.setSize (2, (int) (kMaxRecordS * 48000.0)); recMic.clear();
        auto* self = this;
        mic.sink = [self] (const float* const* in, int nCh, int n)
        {
            if (! self->recOn.load (std::memory_order_acquire)) return;
            const int at = self->recMicPos.load(), m = juce::jmin (n, self->recMic.getNumSamples() - at);
            if (m <= 0) return;
            float pk = 0;
            for (int c = 0; c < 2; ++c)
            {
                const float* x = in[juce::jmin (c, nCh - 1)]; if (x == nullptr) continue;
                self->recMic.copyFrom (c, at, x, m);
                pk = std::max (pk, juce::FloatVectorOperations::findMaximum (x, m));
            }
            self->recMicPos.store (at + m);
            self->micPeak.store (std::max (self->micPeak.load(), pk));
            self->level.store (std::max (self->level.load(), pk));
        };
        if (mic.open()) recMicRate = mic.rate.load() > 0 ? mic.rate.load() : 48000.0;
        else
        {
            mic.sink = nullptr; recMic.setSize (0, 0);
            if (input == Input::Mic) { toast ("The Mac's microphone did not open (" + mic.error + "). Allow it in System Settings > Privacy > Microphone."); return false; }
        }
    }
    recOn.store (true, std::memory_order_release);
    toast ("Recording from " + recordSourceText() + "... press STOP when done (60 s max).");
    changed();
    return true;
}

void SliceProcessor::stopRecording()
{
    if (! recOn.exchange (false)) return;
    juce::Thread::sleep (juce::jlimit (5, 60, (int) (1000.0 * maxBlock / hostSr.load()) * 2));   // (the last block lands)
    mic.close(); mic.sink = nullptr;   // (close first: it waits for the device's callback to finish)
    // which take of the input: the track's when something came in on it, else the mic's
    const bool useTrack = recTrackPos.load() > 0 && (input == Input::Track || trackPeak.load() > 0.003f || recMicPos.load() == 0);
    const auto& b = useTrack ? recTrack : recMic; const int n = useTrack ? recTrackPos.load() : recMicPos.load();
    const double sr = useTrack ? recTrackRate : recMicRate;
    // trim the silence at both ends (keep 60 ms)
    int a = 0, e = n; const float th = 0.004f;
    while (a < e && std::abs (b.getSample (0, a)) < th && std::abs (b.getSample (1, a)) < th) ++a;
    while (e > a && std::abs (b.getSample (0, e - 1)) < th && std::abs (b.getSample (1, e - 1)) < th) --e;
    a = juce::jmax (0, a - (int) (0.06 * sr)); e = juce::jmin (n, e + (int) (0.06 * sr));
    if (e - a < (int) (0.3 * sr)) { toast ("Nothing came in. Check the input (" + recordSourceText() + ") and try again."); changed(); return; }
    juce::AudioBuffer<float> take (2, e - a);
    for (int c = 0; c < 2; ++c) take.copyFrom (c, 0, b, c, a, e - a);
    gen::dsp::fadeEdges (take, (int) (0.01 * sr), (int) (0.03 * sr));
    auto dir = recordingsFolder(); dir.createDirectory();
    auto f = dir.getNonexistentChildFile ("Recording " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S"), ".wav", false);
    {
        auto os = std::make_unique<juce::FileOutputStream> (f);
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> w (os->openedOk() ? wav.createWriterFor (os.get(), sr, 2, 24, {}, 0) : nullptr);   // NOT-EXPORT: the user's own input recording, kept so the session can find its sound again (nothing the plugin made)
        if (w == nullptr) { toast ("Couldn't save the recording in " + dir.getFullPathName()); changed(); return; }
        os.release(); w->writeFromAudioSampleBuffer (take, 0, take.getNumSamples());
    }
    recTrack.setSize (0, 0); recMic.setSize (0, 0);
    loadAudioAsync (f);
}

// ---------------------------------------------------------------- exports
juce::File SliceProcessor::takesFolder() { return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Ghosty").getChildFile ("Ghost Slice").getChildFile ("Takes"); }
juce::File SliceProcessor::recordingsFolder() { return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Ghosty").getChildFile ("Ghost Slice").getChildFile ("Recordings"); }
juce::File SliceProcessor::dragFolder() { return juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Ghost Slice drag"); }

juce::String SliceProcessor::takeFileName() const
{
    auto t = currentTake();
    return juce::File::createLegalFileName ("Ghost Slice - " + (t != nullptr ? t->recipe.name : juce::String ("take")));
}

juce::File SliceProcessor::writeTakeWav (const juce::File& dir, juce::String& error)
{
    if (! ghosty::exportUnlocked (license)) { error = "Export is part of the full version"; return {}; }
    auto t = currentTake();
    if (t == nullptr) { error = "No take yet: press GENERATE"; return {}; }
    dir.createDirectory();
    const auto audio = gen::renderForExport (*t, fx.tone.load(), fx.space.load(), fx.width.load(), fx.warmth.load());
    auto f = dir.getChildFile (takeFileName() + ".wav");
    return exporter::writeWav (audio, t->rate, f, 24, error) ? f : juce::File();
}

juce::File SliceProcessor::writeTakeMidi (const juce::File& dir, juce::String& error)
{
    if (! ghosty::exportUnlocked (license)) { error = "Export is part of the full version"; return {}; }
    auto t = currentTake();
    if (t == nullptr) { error = "No take yet: press GENERATE"; return {}; }
    dir.createDirectory();
    auto f = dir.getChildFile (takeFileName() + ".mid");
    return exporter::writeMidi (t->midi, f, error) ? f : juce::File();
}

juce::File SliceProcessor::saveTake (juce::String& error)
{
    auto f = writeTakeWav (takesFolder(), error);
    if (f == juce::File()) return {};
    juce::String e2; writeTakeMidi (takesFolder(), e2);
    return f;
}

// ---------------------------------------------------------------- state: the sound's path + the takes' recipes + the controls
void SliceProcessor::getStateInformation (juce::MemoryBlock& mb)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("ghostslice", 2);
    if (source != nullptr)
    {
        auto* s = new juce::DynamicObject();
        s->setProperty ("path", source->file.getFullPathName()); s->setProperty ("sha256", source->fingerprint); s->setProperty ("fileName", source->file.getFileName());
        o->setProperty ("source", juce::var (s));
    }
    juce::Array<juce::var> ts;
    for (auto& sl : slots)
    {
        auto v = sl.recipe.toVar(); v.getDynamicObject()->setProperty ("index", sl.index);
        if (sl.trim0 > 0.0 || sl.trim1 < 1.0) { v.getDynamicObject()->setProperty ("trim0", sl.trim0); v.getDynamicObject()->setProperty ("trim1", sl.trim1); }
        ts.add (v);
    }
    o->setProperty ("takes", ts); o->setProperty ("current", current); o->setProperty ("takeCounter", takeCounter); o->setProperty ("rotation", rotation);
    o->setProperty ("type", gen::typeName (type)); o->setProperty ("musical", musical); o->setProperty ("keyTonic", keyTonic); o->setProperty ("keyMinor", keyMinor);
    o->setProperty ("character", gen::characterName (character)); o->setProperty ("tone", (double) fx.tone.load()); o->setProperty ("space", (double) fx.space.load());
    o->setProperty ("bpm", ownBpm); o->setProperty ("theme", uiTheme.load()); o->setProperty ("input", (int) input);
    o->setProperty ("trim0", trim0); o->setProperty ("trim1", trim1); o->setProperty ("keyLock", keyLock); o->setProperty ("clear", clear);
    {   // build 6: the third knob and the UNDER THE HOOD knobs per type, the melody style
        juce::Array<juce::var> kn, dp; for (int t = 0; t < 5; ++t) { kn.add (knobs[(size_t) t]); juce::Array<juce::var> d; for (double x : deeps[(size_t) t]) d.add (x); dp.add (d); }
        o->setProperty ("knobs", kn); o->setProperty ("deeps", dp); o->setProperty ("style", gen::styleName (style)); o->setProperty ("length", lengthBars);
    }
    if (play != gen::NoteEdit()) o->setProperty ("play", play.toVar());   // (build 8: PLAY IT's settings for the next takes)
    const auto s = juce::JSON::toString (juce::var (o), true);
    mb.replaceAll (s.toRawUTF8(), s.getNumBytesAsUTF8());
}

void SliceProcessor::setStateInformation (const void* data, int size)
{
    if (! license.isUnlocked()) { pendingState.replaceAll (data, (size_t) size); return; }   // demo: a reopened session starts empty (UNLOCK brings it back)
    const auto v = juce::JSON::parse (juce::String::fromUTF8 ((const char*) data, size));
    if (! v.isObject() || ! v.hasProperty ("ghostslice")) return;
    uiTheme.store (juce::jlimit (0, 3, (int) v["theme"]));
    juce::String path, sha, name;
    if ((int) v["ghostslice"] >= 2)
    {
        type = gen::typeFromName (v["type"].toString()); musical = v.hasProperty ("musical") ? juce::jlimit (0.0, 1.0, (double) v["musical"]) : 0.7;
        keyTonic = v.hasProperty ("keyTonic") ? juce::jlimit (-1, 11, (int) v["keyTonic"]) : -1; keyMinor = (bool) v["keyMinor"];
        character = gen::characterFromName (v["character"].toString());
        setTone ((float) (double) v["tone"]); setSpace (v.hasProperty ("space") ? (float) (double) v["space"] : 0.4f);
        ownBpm = v.hasProperty ("bpm") ? juce::jlimit (40.0, 240.0, (double) v["bpm"]) : 120.0; input = (Input) juce::jlimit (0, 2, (int) v["input"]);
        slots.clear();
        if (auto* arr = v["takes"].getArray())
            for (auto& x : *arr)
            {
                Slot s; s.recipe = gen::Recipe::fromVar (x); s.index = juce::jmax (1, (int) x["index"]);
                s.trim0 = x.hasProperty ("trim0") ? juce::jlimit (0.0, 1.0, (double) x["trim0"]) : 0.0; s.trim1 = x.hasProperty ("trim1") ? juce::jlimit (s.trim0, 1.0, (double) x["trim1"]) : 1.0;
                slots.push_back (s);
            }
        trim0 = v.hasProperty ("trim0") ? juce::jlimit (0.0, 1.0, (double) v["trim0"]) : 0.0; trim1 = v.hasProperty ("trim1") ? juce::jlimit (trim0, 1.0, (double) v["trim1"]) : 1.0;
        keyLock = (bool) v["keyLock"];
        clear = v.hasProperty ("clear") && (bool) v["clear"];   // (a session from before CLEAR: off, so it sounds as it did)
        knobs.fill (-1.0); for (auto& d : deeps) d.fill (-1.0); style = gen::styleFromName (v["style"].toString());
        lengthBars = v.hasProperty ("length") ? juce::jlimit (1, 16, (int) v["length"]) : 4;
        play = v.hasProperty ("play") ? gen::NoteEdit::fromVar (v["play"]) : gen::NoteEdit(); play.locked.clear(); play.removed.clear(); play.chordAlt.clear();
        if (auto* a = v["knobs"].getArray()) for (int t = 0; t < juce::jmin (5, a->size()); ++t) knobs[(size_t) t] = juce::jlimit (-1.0, 1.0, (double) (*a)[t]);
        if (auto* a = v["deeps"].getArray())
            for (int t = 0; t < juce::jmin (5, a->size()); ++t)
                if (auto* d = (*a)[t].getArray()) for (int i = 0; i < juce::jmin (gen::Recipe::kDeep, d->size()); ++i) deeps[(size_t) t][(size_t) i] = juce::jlimit (-1.0, 1.0, (double) (*d)[i]);
        current = slots.empty() ? -1 : juce::jlimit (0, (int) slots.size() - 1, (int) v["current"]);
        takeCounter = juce::jmax ((int) v["takeCounter"], (int) slots.size()); rotation = (int) v["rotation"];
        path = v["source"]["path"].toString(); sha = v["source"]["sha256"].toString(); name = v["source"]["fileName"].toString();
    }
    else
    {   // an old (editor) session: its sound comes back on the new screen, nothing else
        slots.clear(); current = -1; clear = false;
        const auto s = v["project"]["source"];
        path = s["path"].toString(); sha = s["sha256"].toString(); name = s["fileName"].toString();
    }
    engine.publish (nullptr); changed();
    if (path.isEmpty() && sha.isEmpty()) return;
    juce::File f = juce::File::isAbsolutePath (path) ? juce::File (path) : juce::File();
    loading.store (true);
    auto flag = alive; const bool keep = ! slots.empty();
    juce::Thread::launch ([this, flag, f, sha, name, keep]
    {
        juce::File use = f;
        if (! use.existsAsFile() && sha.isNotEmpty())
            use = source::findByFingerprint (sha, name, { f.getParentDirectory(), recordingsFolder(), juce::File::getSpecialLocation (juce::File::userMusicDirectory) });
        juce::String err; auto s = use.existsAsFile() ? source::load (use, err) : nullptr;
        juce::MessageManager::callAsync ([this, flag, s, name, keep, sha]
        {
            if (! flag->load()) return;
            loading.store (false);
            if (s == nullptr) { waitingFor = sha; toast ("The sound (" + name + ") was not found. Drop it again: your takes come back with it."); changed(); return; }
            installSource (s, keep && (sha.isEmpty() || s->fingerprint == sha), true);
        });
    });
}

void SliceProcessor::unlockedNow()
{
    if (pendingState.getSize() == 0) return;
    const auto mb = pendingState; pendingState.reset();
    setStateInformation (mb.getData(), (int) mb.getSize());
}

juce::AudioProcessorEditor* SliceProcessor::createEditor() { return new SliceEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SliceProcessor(); }
