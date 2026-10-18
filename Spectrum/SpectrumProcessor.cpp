#include "SpectrumProcessor.h"
#include "SpectrumEditor.h"

using namespace ghosty;

namespace
{
const char* sceneNames[] { "Planet A 440", "Deep Space", "Trap Atmosphere", "Overtone Choir", "Glitch Rain", "Just 440", "Empty Sky" };
constexpr int numScenes = 7;
int stepsOf (const char* s) { int m = 0; for (int i = 0; i < 16 && s[i]; ++i) if (s[i] == 'x') m |= 1 << i; return m; }
// default patterns per drum index
const char* defSteps[16] { "x...x...x...x...", "x.....x.x.......", "x.....x...x.....", "....x.......x...", "....x.......x...", "..x...x...x...x.", "......x.......x.",
                           "..........x..x..", "...x......x.....", "x..x..x...x.....", "xxxxxxxxxxxxxxxx", ".......x.......x", "x.x...x.x...x...", "x..x....x.x.....", "x...............", "............x..." };
}

float SpectrumProcessor::drumCenter (int d)
{
    static const float c[16] { 55, 62, 55, 190, 1200, 9000, 8000, 120, 1700, 560, 7000, 800, 880, 600, 6000, 4000 };
    return c[juce::jlimit (0, 15, d)];
}
juce::Point<float> SpectrumProcessor::drumBand (int d)
{
    static const float lo[16] { 35, 40, 38, 150, 600, 6000, 5000, 80, 1000, 480, 4000, 80, 820, 200, 3000, 1000 };
    static const float hi[16] { 160, 220, 200, 9000, 7000, 17000, 17000, 420, 4500, 2600, 15000, 3500, 2000, 6000, 17000, 15000 };
    const int i = juce::jlimit (0, 15, d);
    return { lo[i], hi[i] };
}

juce::AudioProcessorValueTreeState::ParameterLayout SpectrumProcessor::layout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto hz = [] (const String& id, const String& nm, float def)
    {
        NormalisableRange<float> r (20.0f, 20000.0f); r.setSkewForCentre (630.0f);
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, nm, r, def,
                   AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v >= 1000 ? String (v / 1000.0f, 1) + " kHz" : String (roundToInt (v)) + " Hz"; }));
    };
    auto pct = [] (const String& id, const String& nm, float def)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, nm, NormalisableRange<float> (0.0f, 1.0f), def,
                   AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100)) + "%"; }));
    };
    l.add (hz ("lp", "Low-pass", 20000.0f));
    l.add (hz ("hp", "High-pass", 20.0f));
    l.add (pct ("atmos", "Atmosphere", 0.4f));
    l.add (pct ("drive", "Drive", 0.1f));
    l.add (pct ("gain", "Volume", 0.8f));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "ref", 1 }, "Tuning", StringArray { "A4 = 440 Hz", "A4 = 432 Hz", "A4 = 442 Hz" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "key", 1 }, "Key", keyNames(), 9));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "scale", 1 }, "Scale", scaleNames(), 2));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "snap", 1 }, "Snap to scale", true));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "playTimbre", 1 }, "Play sound", shapeNames(), 6));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "bpm", 1 }, "Tempo (standalone)", NormalisableRange<float> (50.0f, 200.0f, 1.0f), 120.0f));
    return l;
}

SpectrumProcessor::SpectrumProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "GHOSTYSPECTRUM", layout())
{
    for (int i = 0; i < NUM_TIMBRES; ++i) mips[(size_t) i].build (makeShape (i), 44100.0);
    mipRate = 44100.0;
    buildScene (0);
}

bool SpectrumProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void SpectrumProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    if (std::abs (mipRate - sampleRate) > 1.0) { for (int i = 0; i < NUM_TIMBRES; ++i) mips[(size_t) i].build (makeShape (i), sampleRate); mipRate = sampleRate; }
    bank.build (sampleRate);
    fx.prepare (sampleRate, samplesPerBlock);
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 2 };
    lpF.prepare (spec); hpF.prepare (spec);
    lpF.setType (juce::dsp::StateVariableTPTFilterType::lowpass); hpF.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    for (auto& d : drums) d.active = false;
    for (auto& v : live) v.active = false;
}

float SpectrumProcessor::ref() const
{
    static const float refs[3] { 440.0f, 432.0f, 442.0f };
    return refs[juce::jlimit (0, 2, (int) apvts.getRawParameterValue ("ref")->load())];
}

float SpectrumProcessor::snap (float f) const
{
    const float m = midiOf (f);
    int n = juce::roundToInt (m);
    if (apvts.getRawParameterValue ("snap")->load() > 0.5f)
    {
        const int key = (int) apvts.getRawParameterValue ("key")->load(), scale = (int) apvts.getRawParameterValue ("scale")->load();
        n = snapToScale (n, key, scaleMask (scale));
    }
    return fOf ((float) n);
}

int SpectrumProcessor::addTone (float freq, float pan, int timbre, bool drone, int steps, float vol)
{
    for (int i = 0; i < MAX_EL; ++i)
    {
        auto& e = els[(size_t) i];
        if (e.active.load()) continue;
        e.kind = 0; e.freq = freq; e.pan = pan; e.timbre = timbre; e.drone = drone; e.steps = steps; e.vol = vol; e.level = 0;
        e.look = rng.nextInt (35);
        e.active.store (true);
        return i;
    }
    return -1;
}

int SpectrumProcessor::addDrum (int drum, float freq, float pan, int steps, float vol)
{
    for (int i = 0; i < MAX_EL; ++i)
    {
        auto& e = els[(size_t) i];
        if (e.active.load()) continue;
        e.kind = 1; e.freq = freq > 0 ? freq : drumCenter (drum); e.pan = pan; e.timbre = drum; e.drone = false;
        e.steps = steps >= 0 ? steps : stepsOf (defSteps[juce::jlimit (0, 15, drum)]); e.vol = vol; e.level = 0;
        e.look = rng.nextInt (35);
        e.active.store (true);
        return i;
    }
    return -1;
}

void SpectrumProcessor::buildScene (int index)
{
    clearElements();
    auto set = [this] (const char* id, float v) { if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v)); };
    auto A = [this] (int m) { return fOf ((float) m); };
    switch (index)
    {
        case 0:
            set ("key", 9); set ("scale", 2); set ("bpm", 120);
            addTone (A (33), -0.1f, 0, true, 0, 1.2f); addTone (A (45), 0.1f, 2, false, stepsOf ("x..x..x...x..x.."), 0.9f);
            addTone (A (64), -0.45f, 6, true, 0, 0.7f); addTone (440.0f * ref() / 440.0f, 0.0f, 9, true, 0, 0.8f); addTone (A (72), 0.5f, 5, false, stepsOf ("..x...x...x.x..."), 0.7f);
            addDrum (0, 0, 0.0f); addDrum (4, 0, 0.15f); addDrum (5, 0, 0.35f); addDrum (10, 0, -0.4f, stepsOf (".x.x.x.x.x.x.x.x"), 0.6f);
            break;
        case 1:
            set ("key", 2); set ("scale", 2); set ("bpm", 72);
            addTone (A (26), 0.0f, 0, true, 0, 1.3f); addTone (A (50), -0.4f, 5, true, 0, 0.7f); addTone (A (57), 0.4f, 9, true, 0, 0.7f);
            addTone (A (65), -0.2f, 6, false, stepsOf ("x.......x...x..."), 0.8f); addTone (A (77), 0.6f, 0, false, stepsOf ("......x.......x."), 0.6f);
            addDrum (2, A (26), 0.0f, stepsOf ("x.........x.....")); addDrum (8, 0, 0.5f, stepsOf ("....x.......x..x"), 0.7f);
            break;
        case 2:
            set ("key", 1); set ("scale", 4); set ("bpm", 140);
            addTone (A (61), -0.3f, 9, true, 0, 0.6f); addTone (A (68), 0.3f, 6, true, 0, 0.5f); addTone (A (73), 0.6f, 8, false, stepsOf ("x..x..x.x..x..x."), 0.6f);
            addDrum (2, A (37), 0.0f, stepsOf ("x.....x...x...x.")); addDrum (3, 0, 0.1f, stepsOf ("........x.......")); addDrum (5, 0, 0.3f, stepsOf ("x.x.x.xxx.x.x.xx"), 0.8f); addDrum (6, 0, -0.4f, stepsOf ("..............x."));
            break;
        case 3:
        {
            set ("key", 9); set ("scale", 3);
            const int ms[7] { 33, 45, 52, 57, 61, 64, 69 }; const int tb[7] { 0, 5, 9, 6, 1, 10, 0 };
            for (int i = 0; i < 7; ++i) addTone (A (ms[i]), (float) i / 6.0f * 1.6f - 0.8f, tb[i], true, 0, 0.55f);
            break;
        }
        case 4:
            set ("key", 4); set ("scale", 11); set ("bpm", 150);
            addTone (A (40), 0.0f, 7, false, stepsOf ("x..x....x.x....."), 1.0f); addTone (A (64), -0.5f, 8, false, stepsOf ("x.xx.x..x.xx.x.x"), 0.6f); addTone (A (71), 0.5f, 3, false, stepsOf ("..x...x...x..xx."), 0.5f);
            addDrum (1, 0, 0.0f, stepsOf ("x..x....x.x.....")); addDrum (11, 0, 0.4f); addDrum (4, 0, -0.2f, stepsOf ("....x.......x..x")); addDrum (10, 0, 0.6f, -1, 0.7f);
            break;
        case 5:
            set ("key", 9); set ("scale", 0);
            addTone (ref(), 0.0f, 0, true, 0, 1.0f);
            break;
        default: break;
    }
    sceneVersion++;
}

int SpectrumProcessor::getNumPrograms() { return numScenes; }
const juce::String SpectrumProcessor::getProgramName (int i) { return juce::isPositiveAndBelow (i, numScenes) ? sceneNames[i] : ""; }
void SpectrumProcessor::setCurrentProgram (int i) { if (juce::isPositiveAndBelow (i, numScenes)) { currentScene = i; buildScene (i); } }

void SpectrumProcessor::triggerStep (int s, int offset)
{
    playStep.store (s);
    for (auto& e : els)
    {
        if (! e.active.load() || ! ((e.steps.load() >> s) & 1)) continue;
        if (e.kind.load() == 1)
        {
            const int d = e.timbre.load();
            DrumVoice* slot = nullptr;
            for (auto& v : drums) if (! v.active) { slot = &v; break; }
            if (! slot) slot = &drums[(size_t) rng.nextInt ((int) drums.size())];
            const auto& snd = bank.get (d);
            const float pan = e.pan.load();
            slot->data = &snd.fwd; slot->pos = 0; slot->rate = e.freq.load() / drumCenter (d);
            slot->gain = e.vol.load() * snd.gain * 0.9f; slot->pl = std::sqrt (0.5f * (1 - pan)); slot->pr = std::sqrt (0.5f * (1 + pan));
            slot->delay = offset; slot->active = true;
            e.hits.fetch_add (1);
        }
        else if (! e.drone.load())
        {
            e.env = 1.0f; e.hits.fetch_add (1);
        }
    }
}

void SpectrumProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    auto* L = buffer.getWritePointer (0);
    auto* R = buffer.getWritePointer (1);

    bool hp = false; double hostBpm = 0, hostPpq = 0; bool havePpq = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            hp = pos->getIsPlaying();
            if (auto b = pos->getBpm()) hostBpm = *b;
            if (auto q = pos->getPpqPosition()) { hostPpq = *q; havePpq = true; }
        }
    hostPlaying.store (hp);
    const double bpm = hostBpm > 0 ? hostBpm : (double) apvts.getRawParameterValue ("bpm")->load();
    const double inc16 = bpm / 60.0 * 4.0 / sr;
    const double stepSec = 60.0 / bpm / 4.0;
    const bool running = hp || internalPlay.load();

    // ---- MIDI notes play the current "play sound" at their exact frequency
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn())
        {
            int slot = -1;
            for (int i = 1; i < (int) live.size(); ++i) if (! live[(size_t) i].active) { slot = i; break; }
            if (slot < 0) slot = 1 + rng.nextInt ((int) live.size() - 1);
            auto& v = live[(size_t) slot];
            v.active = true; v.held = true; v.note = m.getNoteNumber(); v.freq = v.freqNow = fOf ((float) v.note); v.env = 0; v.stage = 0; v.pan = 0; v.phase = 0;
        }
        else if (m.isNoteOff())
            for (int i = 1; i < (int) live.size(); ++i) if (live[(size_t) i].active && live[(size_t) i].note == m.getNoteNumber()) live[(size_t) i].held = false;
    }
    // on-screen PLAY voice
    {
        auto& v = live[0];
        const int trig = liveTrig.load();
        if (trig != liveTrigSeen) { liveTrigSeen = trig; v.active = true; v.env = 0; v.stage = 0; v.phase = 0; v.freqNow = liveFreq.load(); }
        v.held = liveGate.load(); v.freq = liveFreq.load(); v.pan = livePan.load();
    }

    // ---- sequencer
    if (running)
    {
        if (hp && havePpq) { const double host16 = hostPpq * 4.0; if (! wasPlaying || std::abs (host16 - pos16) > 0.75) { pos16 = host16; nextStep = (long long) std::ceil (pos16 - 1e-9); } }
        else if (! wasPlaying) { pos16 = 0; nextStep = 0; }
        for (int i = 0; i < n; ++i)
        {
            pos16 += inc16;
            while (pos16 >= (double) nextStep) { triggerStep ((int) (((nextStep % 16) + 16) % 16), i); ++nextStep; }
        }
    }
    else playStep.store (-1);
    wasPlaying = running;

    // ---- tones
    const float pulseDecay = (float) std::exp (-1.0 / (juce::jmax (0.06, stepSec * 1.2) * sr));
    for (auto& e : els)
    {
        if (! e.active.load()) { e.gainNow = 0; e.level.store (0); continue; }
        if (e.kind.load() != 0) { e.level.store (e.level.load() * 0.85f); continue; }
        const int tb = juce::jlimit (0, NUM_TIMBRES - 1, e.timbre.load());
        const auto& mip = mips[(size_t) tb];
        const float target = e.freq.load(), pan = e.pan.load(), pl = std::sqrt (0.5f * (1 - pan)), pr = std::sqrt (0.5f * (1 + pan));
        const float g0 = 0.16f * e.vol.load() * juce::jmin (1.4f, std::pow (220.0f / juce::jmax (20.0f, target), 0.18f));
        const bool drone = e.drone.load();
        float peak = 0;
        for (int i = 0; i < n; ++i)
        {
            e.freqNow += (target - e.freqNow) * 0.002f;
            float amp;
            if (drone) { e.gainNow += (g0 - e.gainNow) * 0.0006f; amp = e.gainNow; }
            else { e.env *= pulseDecay; amp = g0 * 1.6f * e.env; e.gainNow = amp; }
            const int m = MipSet::mipFor (e.freqNow);
            const float s = mip.read (m, e.phase) * amp;
            e.phase += e.freqNow / sr; if (e.phase >= 1.0) e.phase -= 1.0;
            L[i] += s * pl; R[i] += s * pr;
            peak = juce::jmax (peak, amp);
        }
        e.level.store (juce::jlimit (0.0f, 1.0f, peak / juce::jmax (0.0001f, g0)));
    }

    // ---- live voices (PLAY strip + MIDI)
    const int ptb = juce::jlimit (0, NUM_TIMBRES - 1, (int) apvts.getRawParameterValue ("playTimbre")->load());
    const auto& pmip = mips[(size_t) ptb];
    const float atk = 1.0f / (0.012f * (float) sr), rel = (float) std::exp (-1.0 / (0.25 * sr)), dec = (float) std::exp (-1.0 / (0.4 * sr));
    for (auto& v : live)
    {
        if (! v.active) continue;
        const float pan = v.pan, pl = std::sqrt (0.5f * (1 - pan)), pr = std::sqrt (0.5f * (1 + pan));
        const float g0 = 0.2f * juce::jmin (1.4f, std::pow (220.0f / juce::jmax (20.0f, v.freq), 0.18f));
        for (int i = 0; i < n; ++i)
        {
            v.freqNow += (v.freq - v.freqNow) * 0.004f;
            if (! v.held) v.env *= rel;
            else if (v.stage == 0) { v.env += atk; if (v.env >= 1) { v.env = 1; v.stage = 1; } }
            else v.env = 0.55f + (v.env - 0.55f) * dec;
            const float s = pmip.read (MipSet::mipFor (v.freqNow), v.phase) * v.env * g0;
            v.phase += v.freqNow / sr; if (v.phase >= 1.0) v.phase -= 1.0;
            L[i] += s * pl; R[i] += s * pr;
        }
        if (! v.held && v.env < 0.0005f) v.active = false;
    }

    // ---- drums
    for (auto& d : drums)
    {
        if (! d.active) continue;
        const auto& data = *d.data; const double len = (double) data.size() - 1.0;
        for (int i = 0; i < n; ++i)
        {
            if (d.delay > 0) { --d.delay; continue; }
            if (d.pos >= len) { d.active = false; break; }
            const int idx = (int) d.pos; const float fr = (float) (d.pos - idx);
            const float s = (data[(size_t) idx] + (data[(size_t) idx + 1] - data[(size_t) idx]) * fr) * d.gain;
            L[i] += s * d.pl; R[i] += s * d.pr;
            d.pos += d.rate;
        }
    }

    // ---- filters + atmosphere
    juce::dsp::AudioBlock<float> block (buffer);
    juce::dsp::ProcessContextReplacing<float> ctx (block);
    const float lp = apvts.getRawParameterValue ("lp")->load(), hpHz = apvts.getRawParameterValue ("hp")->load();
    if (lp < 19900.0f) { lpF.setCutoffFrequency (lp); lpF.setResonance (0.8f); lpF.process (ctx); }
    if (hpHz > 21.0f) { hpF.setCutoffFrequency (hpHz); hpF.setResonance (0.8f); hpF.process (ctx); }
    buffer.applyGain (apvts.getRawParameterValue ("gain")->load() * 1.4f);
    FxParams p;
    const float atm = apvts.getRawParameterValue ("atmos")->load();
    p.verb = atm; p.echo = atm * 0.45f; p.drive = apvts.getRawParameterValue ("drive")->load(); p.bpm = bpm;
    fx.process (buffer, p);
    int wp = ringPos.load();
    for (int i = 0; i < n; ++i)
    {
        L[i] = std::tanh (L[i]); R[i] = std::tanh (R[i]);
        ring[(size_t) wp] = 0.5f * (L[i] + R[i]); wp = (wp + 1) & (RING - 1);
    }
    ringPos.store (wp);
    const float rms = buffer.getRMSLevel (0, 0, n);
    level.store (level.load() * 0.75f + juce::jmin (1.0f, rms * 3.5f) * 0.25f);
}

void SpectrumProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    juce::ValueTree list ("ELEMENTS");
    for (auto& e : els)
    {
        if (! e.active.load()) continue;
        juce::ValueTree t ("EL");
        t.setProperty ("kind", e.kind.load(), nullptr); t.setProperty ("freq", e.freq.load(), nullptr); t.setProperty ("pan", e.pan.load(), nullptr);
        t.setProperty ("vol", e.vol.load(), nullptr); t.setProperty ("timbre", e.timbre.load(), nullptr); t.setProperty ("drone", e.drone.load(), nullptr);
        t.setProperty ("steps", e.steps.load(), nullptr);
        list.appendChild (t, nullptr);
    }
    state.removeChild (state.getChildWithName ("ELEMENTS"), nullptr);
    state.appendChild (list, nullptr);
    state.setProperty ("scene", currentScene, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void SpectrumProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        if (! state.isValid()) return;
        auto list = state.getChildWithName ("ELEMENTS");
        state.removeChild (list, nullptr);
        apvts.replaceState (state);
        currentScene = (int) state.getProperty ("scene", 0);
        clearElements();
        for (auto t : list)
        {
            if ((int) t.getProperty ("kind") == 1) addDrum ((int) t.getProperty ("timbre"), (float) t.getProperty ("freq"), (float) t.getProperty ("pan"), (int) t.getProperty ("steps"), (float) t.getProperty ("vol"));
            else addTone ((float) t.getProperty ("freq"), (float) t.getProperty ("pan"), (int) t.getProperty ("timbre"), (bool) t.getProperty ("drone"), (int) t.getProperty ("steps"), (float) t.getProperty ("vol"));
        }
        sceneVersion++;
    }
}

juce::AudioProcessorEditor* SpectrumProcessor::createEditor() { return new SpectrumEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SpectrumProcessor(); }
