#include "GhostProcessor.h"
#include "GhostEditor.h"
#include "HostedPlugin.h"
#include <random>

namespace ghost
{
const char* laneName (int lane) { static const char* n[] { "CHORDS", "ARP", "MELODY", "BASS" }; return n[juce::jlimit (0, 3, lane)]; }
int laneMidiChannel (int lane) { static const int c[] { 3, 1, 2, 4 }; return c[juce::jlimit (0, 3, lane)]; }

static const char* kArpBundle = "ArpEZator.vst3";
static const char* kBeatsBundle = "Ghosty Beats.vst3";
static constexpr int kMaxAlign = 65536;   // the most delay GHOST lines up (FX 16384 + a synth's own)

void AlignDelay::process (float* L, float* R, int n) noexcept
{
    const int dd = d.load (std::memory_order_relaxed);
    if (dd <= 0) return;
    const int size = buf.getNumSamples();
    if (dd >= size) return;
    auto* bl = buf.getWritePointer (0); auto* br = buf.getWritePointer (1);
    for (int i = 0; i < n; ++i)
    {
        bl[w] = L[i]; br[w] = R[i];
        int r = w - dd; if (r < 0) r += size;
        L[i] = bl[r]; R[i] = br[r];
        if (++w >= size) w = 0;
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout GhostProcessor::layout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "master", 1 }, "Master", NormalisableRange<float> (-60.0f, 6.0f, 0.1f), 0.0f, AudioParameterFloatAttributes().withLabel ("dB")));
    for (int i = 0; i < kLanes; ++i)
    {
        const String n = String (laneName (i)).toLowerCase(), N = String (laneName (i)).substring (0, 1) + String (laneName (i)).substring (1).toLowerCase();
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { n + "_level", 1 }, N + " Level", NormalisableRange<float> (-60.0f, 6.0f, 0.1f, 2.5f), -6.0f, AudioParameterFloatAttributes().withLabel ("dB")));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { n + "_tone", 1 }, N + " Tone", NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));
        l.add (std::make_unique<AudioParameterBool> (ParameterID { n + "_mute", 1 }, N + " Mute", false));
    }
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "beats_level", 1 }, "Beats Level", NormalisableRange<float> (-60.0f, 6.0f, 0.1f, 2.5f), -3.0f, AudioParameterFloatAttributes().withLabel ("dB")));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "beats_mute", 1 }, "Beats Mute", false));
    return l;
}

GhostProcessor::GhostProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", layout())
{
    pMaster = apvts.getRawParameterValue ("master");
    for (int i = 0; i < kLanes; ++i)
    {
        const juce::String n = juce::String (laneName (i)).toLowerCase();
        pLevel[(size_t) i] = apvts.getRawParameterValue (n + "_level");
        pTone[(size_t) i] = apvts.getRawParameterValue (n + "_tone");
        pMute[(size_t) i] = apvts.getRawParameterValue (n + "_mute");
    }
    pBeatsLevel = apvts.getRawParameterValue ("beats_level");
    pBeatsMute = apvts.getRawParameterValue ("beats_mute");
    // the parts open on the first tick of the message thread (a host that only looks at GHOST - a plug-in scan - never
    // opens them)
    startTimerHz (20);
}

GhostProcessor::~GhostProcessor()
{
    stopTimer();
    for (auto& L : lanes) { const juce::SpinLock::ScopedLockType g (L.audioLock); L.plugin.reset(); }
    { const juce::SpinLock::ScopedLockType g (arpLock); arp.reset(); }
    { const juce::SpinLock::ScopedLockType g (beatsLock); beats.reset(); }
}

bool GhostProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void GhostProcessor::prepareLane (Lane& L)
{
    L.fx.activate (sr);
    L.align.prepare (kMaxAlign);
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) maxBlock, 2 };
    L.lp.prepare (spec); L.hp.prepare (spec);
    L.lp.setType (juce::dsp::StateVariableTPTFilterType::lowpass); L.hp.setType (juce::dsp::StateVariableTPTFilterType::highpass);
    L.lp.setResonance (0.6f); L.hp.setResonance (0.6f);
    L.lastTone = -2.0f;
    L.midi.ensureSize (4096);
    if (L.plugin) { L.plugin->releaseResources(); L.plugin->setRateAndBufferSizeDetails (sr, maxBlock); L.plugin->prepareToPlay (sr, maxBlock); }
}

void GhostProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    maxBlock = juce::jmax (32, samplesPerBlock);
    scratch.setSize (32, maxBlock, false, true, false);
    arpScratch.setSize (32, maxBlock, false, true, false);
    arpMidiIn.ensureSize (8192); arpMidiOut.ensureSize (8192);
    for (auto& L : lanes)
    {
        const juce::SpinLock::ScopedLockType g (L.audioLock);
        prepareLane (L);
    }
    {
        const juce::SpinLock::ScopedLockType g (arpLock);
        if (arp) { arp->releaseResources(); arp->setRateAndBufferSizeDetails (sr, maxBlock); arp->prepareToPlay (sr, maxBlock); }
    }
    {
        const juce::SpinLock::ScopedLockType g (beatsLock);
        beatsAlign.prepare (kMaxAlign);
        if (beats) { beats->releaseResources(); beats->setRateAndBufferSizeDetails (sr, maxBlock); beats->prepareToPlay (sr, maxBlock); }
    }
    for (auto* g : { &gBeats, &gMaster }) g->reset (sr, 0.03);
    for (auto& g : gLane) g.reset (sr, 0.03);
    safety.prepare ({ sr, (juce::uint32) maxBlock, 2 }); safety.setThreshold (-0.3f); safety.setRelease (80.0f); safety.reset();
    prepared = true;
    needRealign = true;
}

void GhostProcessor::releaseResources()
{
    for (auto& L : lanes) { const juce::SpinLock::ScopedLockType g (L.audioLock); if (L.plugin) L.plugin->releaseResources(); }
    { const juce::SpinLock::ScopedLockType g (arpLock); if (arp) arp->releaseResources(); }
    { const juce::SpinLock::ScopedLockType g (beatsLock); if (beats) beats->releaseResources(); }
}

// ---------------------------------------------------------------- the audio
namespace
{
juce::AudioBuffer<float> viewOf (juce::AudioBuffer<float>& scratch, juce::AudioPluginInstance& p, int n)
{
    const int ch = juce::jlimit (2, scratch.getNumChannels(), juce::jmax (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels()));
    for (int c = 0; c < ch; ++c) juce::FloatVectorOperations::clear (scratch.getWritePointer (c), n);
    return juce::AudioBuffer<float> (scratch.getArrayOfWritePointers(), ch, n);
}
float peakOf (const float* x, int n) { float m = 0; for (int i = 0; i < n; ++i) m = juce::jmax (m, std::abs (x[i])); return m; }
void meterInto (std::atomic<float>& m, float peak) { const float old = m.load (std::memory_order_relaxed); m.store (juce::jmax (peak, old * 0.85f), std::memory_order_relaxed); }
}

void GhostProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (! prepared || n <= 0) { midi.clear(); return; }

    // ---- the clock the parts follow: the DAW's when it plays, else GHOST's own PLAY
    juce::Optional<juce::AudioPlayHead::PositionInfo> daw;
    if (auto* ph = getPlayHead()) daw = ph->getPosition();
    const bool dawPlay = daw.hasValue() && daw->getIsPlaying();
    double bpm = 120.0;
    if (daw.hasValue()) if (auto b = daw->getBpm()) if (*b > 1.0) bpm = *b;
    shownBpm = bpm;
    dawPlaying = dawPlay;
    const bool own = ! dawPlay && ownPlay.load();
    if (own && ! wasOwn) ownPpq = 0.0;
    wasOwn = own;

    auto* outL = buffer.getWritePointer (0);
    auto* outR = buffer.getWritePointer (juce::jmin (1, buffer.getNumChannels() - 1));

    for (int at = 0; at < n; at += maxBlock)
    {
        const int len = juce::jmin (maxBlock, n - at);
        juce::AudioPlayHead::PositionInfo info;
        if (daw.hasValue()) info = *daw;
        info.setBpm (bpm);
        if (own)
        {
            info.setIsPlaying (true); info.setIsRecording (false); info.setIsLooping (false);
            info.setPpqPosition (ownPpq);
            info.setPpqPositionOfLastBarStart (std::floor (ownPpq / 4.0) * 4.0);
            info.setTimeInSeconds (ownPpq * 60.0 / bpm);
            info.setTimeInSamples ((juce::int64) std::llround (ownPpq * 60.0 / bpm * sr));
            info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
            ownPpq += (double) len / sr * bpm / 60.0;
        }
        else if (dawPlay && at > 0)
        {
            if (auto q = daw->getPpqPosition()) info.setPpqPosition (*q + (double) at / sr * bpm / 60.0);
            if (auto t = daw->getTimeInSamples()) info.setTimeInSamples (*t + at);
        }
        else if (! dawPlay) info.setIsPlaying (false);
        hostedHead.info = info;
        if (auto q = info.getPpqPosition()) shownPpq = *q;

        // 1. ArpEZator writes this block's notes (its own sound is off: GHOST's lanes play them)
        for (auto& L : lanes) L.midi.clear();
        {
            const juce::SpinLock::ScopedTryLockType g (arpLock);
            if (g.isLocked() && arp)
            {
                auto view = viewOf (arpScratch, *arp, len);
                arpMidiIn.clear();
                arp->setPlayHead (&hostedHead);
                arp->processBlock (view, arpMidiIn);   // (a hosted plug-in hands its MIDI out back in the same buffer)
                for (const auto m : arpMidiIn)
                {
                    auto msg = m.getMessage();
                    const int ch = msg.getChannel();
                    for (int l = 0; l < kLanes; ++l)
                        if (ch == laneMidiChannel (l)) { msg.setChannel (1); if (msg.isNoteOn()) ++lanes[(size_t) l].notesIn; lanes[(size_t) l].midi.addEvent (msg, m.samplePosition); }
                }
            }
        }
        // 2. the DAW's keyboard plays the selected lane
        {
            const int sel = juce::jlimit (0, kLanes - 1, selectedLane.load());
            for (const auto m : midi)
            {
                if (m.samplePosition < at || m.samplePosition >= at + len) continue;
                auto msg = m.getMessage();
                if (msg.getChannel() > 0) msg.setChannel (1);
                lanes[(size_t) sel].midi.addEvent (msg, m.samplePosition - at);
            }
        }
        // 3. the lanes
        for (int l = 0; l < kLanes; ++l)
        {
            auto& L = lanes[(size_t) l];
            const juce::SpinLock::ScopedTryLockType g (L.audioLock);
            if (! g.isLocked() || ! L.plugin) continue;
            if (! L.midi.isEmpty()) L.activity = true;
            auto view = viewOf (scratch, *L.plugin, len);
            L.plugin->setPlayHead (&hostedHead);
            L.plugin->processBlock (view, L.midi);
            auto* l0 = view.getWritePointer (0);
            auto* r0 = view.getWritePointer (view.getNumChannels() > 1 && L.plugin->getTotalNumOutputChannels() > 1 ? 1 : 0);
            if (r0 == l0) { r0 = view.getWritePointer (1); juce::FloatVectorOperations::copy (r0, l0, len); }
            // TONE: left = darker (low-pass down to 300 Hz), right = thinner (high-pass up to 1.2 kHz), middle = as it is
            const float tone = pTone[(size_t) l]->load();
            if (std::abs (tone) > 0.01f)
            {
                if (std::abs (tone - L.lastTone) > 1.0e-4f)
                {
                    L.lp.setCutoffFrequency ((float) juce::jmin (sr * 0.45, 300.0 * std::pow (20000.0 / 300.0, 1.0 + juce::jmin (0.0f, tone))));
                    L.hp.setCutoffFrequency ((float) (20.0 * std::pow (1200.0 / 20.0, juce::jmax (0.0f, tone))));
                    L.lastTone = tone;
                }
                float* ch2[] { l0, r0 };
                juce::dsp::AudioBlock<float> blk (ch2, 2, (size_t) len);
                juce::dsp::ProcessContextReplacing<float> ctx (blk);
                if (tone < 0) L.lp.process (ctx); else L.hp.process (ctx);
            }
            else if (L.lastTone != 0.0f) { L.lp.reset(); L.hp.reset(); L.lastTone = 0.0f; }
            // SPECTRAL FX (Poltergeist 2's rack)
            L.fx.process (l0, r0, (uint32_t) len, bpm);
            // level / mute
            const float target = pMute[(size_t) l]->load() > 0.5f ? 0.0f : juce::Decibels::decibelsToGain (pLevel[(size_t) l]->load(), -59.9f);
            gLane[l].setTargetValue (target);
            for (int i = 0; i < len; ++i) { const float gg = gLane[l].getNextValue(); l0[i] *= gg; r0[i] *= gg; }
            L.align.process (l0, r0, len);
            meterInto (L.meter, juce::jmax (peakOf (l0, len), peakOf (r0, len)));
            juce::FloatVectorOperations::add (outL + at, l0, len);
            juce::FloatVectorOperations::add (outR + at, r0, len);
        }
        // 4. the drums
        {
            const juce::SpinLock::ScopedTryLockType g (beatsLock);
            if (g.isLocked() && beats)
            {
                auto view = viewOf (scratch, *beats, len);
                empty.clear();
                beats->setPlayHead (&hostedHead);
                beats->processBlock (view, empty);
                auto* l0 = view.getWritePointer (0); auto* r0 = view.getWritePointer (1);
                if (beats->getTotalNumOutputChannels() < 2) juce::FloatVectorOperations::copy (r0, l0, len);
                const float target = pBeatsMute->load() > 0.5f ? 0.0f : juce::Decibels::decibelsToGain (pBeatsLevel->load(), -59.9f);
                gBeats.setTargetValue (target);
                for (int i = 0; i < len; ++i) { const float gg = gBeats.getNextValue(); l0[i] *= gg; r0[i] *= gg; }
                beatsAlign.process (l0, r0, len);
                meterInto (beatsMeter, juce::jmax (peakOf (l0, len), peakOf (r0, len)));
                juce::FloatVectorOperations::add (outL + at, l0, len);
                juce::FloatVectorOperations::add (outR + at, r0, len);
            }
        }
    }
    // master
    gMaster.setTargetValue (juce::Decibels::decibelsToGain (pMaster->load(), -59.9f));
    for (int i = 0; i < n; ++i) { const float gg = gMaster.getNextValue(); outL[i] *= gg; outR[i] *= gg; }
    if (buffer.getNumChannels() >= 2)
    {
        juce::dsp::AudioBlock<float> blk (buffer.getArrayOfWritePointers(), 2, (size_t) n);
        safety.process (juce::dsp::ProcessContextReplacing<float> (blk));
    }
    meterInto (masterMeterL, peakOf (outL, n));
    meterInto (masterMeterR, peakOf (outR, n));
    midi.clear();
}

// ---------------------------------------------------------------- the parts (message thread)
void GhostProcessor::setArpSoundOff()
{   // ArpEZator in GHOST: its own preview sound off, every lane on its own MIDI channel, glides as notes, no click kick
    if (! arp) return;
    for (auto* p : arp->getParameters())
    {
        const auto name = p->getName (64);
        auto set = [p] (float v) { if (std::abs (p->getValue() - v) > 1.0e-4f) p->setValueNotifyingHost (v); };
        if (name == "Sound") set (0.0f);
        else if (name == "MIDI Out") set (1.0f);
        else if (name == "MIDI Out All On Channel 1") set (0.0f);
        else if (name == "Glides As Pitch Bend") set (0.0f);
        else if (name == "Kick") set (0.0f);
        else if (name == "Follow My Keys") set (0.0f);
    }
}

void GhostProcessor::openParts()
{
    const double rate = prepared ? sr : 48000.0;
    const int blk = prepared ? maxBlock : 512;
    if (! arp && arpError.isEmpty())
    {
        juce::String e;
        auto p = Hosted::open (kArpBundle, rate, blk, e);
        if (p) { p->setPlayHead (&hostedHead); const juce::SpinLock::ScopedLockType g (arpLock); arp = std::move (p); }
        else arpError = e;
        setArpSoundOff();
    }
    if (! beats && beatsError.isEmpty())
    {
        juce::String e;
        auto p = Hosted::open (kBeatsBundle, rate, blk, e);
        if (p) { p->setPlayHead (&hostedHead); const juce::SpinLock::ScopedLockType g (beatsLock); beats = std::move (p); }
        else beatsError = e;
    }
    for (auto& L : lanes)
        if (! L.hasEngine) { const juce::SpinLock::ScopedLockType g (L.audioLock); prepareLane (L); }
    if (restoreTree.isValid()) applyRestore();
    else
    {   // a new GHOST: a sound on every lane (MIDNIGHT-friendly picks)
        static const char* first[kLanes] { "gp2:factory:Pad/Warm Blanket", "gp2:factory:Pluck/Crystal Pluck", "gp2:factory:Lead/Ghost Whistle", "gp2:factory:Bass/Velvet 808" };
        for (int l = 0; l < kLanes; ++l)
            if (lanes[(size_t) l].soundId.isEmpty()) { juce::String e; if (! loadSound (l, first[l], e)) lanes[(size_t) l].error = e; }
    }
    realign();
    ++uiVersion;
}

bool GhostProcessor::openLanePlugin (int lane, Engine e, juce::String& error)
{
    auto& L = lanes[(size_t) lane];
    if (L.hasEngine && L.engine == e && L.plugin) return true;
    const double rate = prepared ? sr : 48000.0;
    const int blk = prepared ? maxBlock : 512;
    auto p = Hosted::open (engineBundle (e), rate, blk, error);
    if (! p) return false;
    p->setPlayHead (&hostedHead);
    if (L.plugin && onLanePluginClosing) onLanePluginClosing (lane);
    std::unique_ptr<juce::AudioPluginInstance> old;
    {
        const juce::SpinLock::ScopedLockType g (L.audioLock);
        old = std::move (L.plugin);
        L.plugin = std::move (p);
        L.engine = e; L.hasEngine = true;
    }
    old.reset();   // (outside the lock: a synth can take a moment to close)
    return true;
}

bool GhostProcessor::loadSound (int lane, const juce::String& soundId, juce::String& error)
{
    Sound s;
    if (! Catalog::get().find (soundId, s)) { error = "can't find that sound"; return false; }
    return loadSound (lane, s, error);
}

bool GhostProcessor::loadSound (int lane, const Sound& s, juce::String& error)
{
    lane = juce::jlimit (0, kLanes - 1, lane);
    auto& L = lanes[(size_t) lane];
    juce::MemoryBlock state;
    if (! Catalog::hostState (s, state, error)) { L.error = error; ++uiVersion; return false; }
    if (! openLanePlugin (lane, s.engine, error)) { L.error = error; ++uiVersion; return false; }
    L.plugin->setStateInformation (state.getData(), (int) state.getSize());
    L.soundId = s.id; L.soundName = s.name; L.soundCategory = s.category; L.edited = false; L.error = {};
    realign();
    ++uiVersion;
    return true;
}

std::vector<Role> GhostProcessor::rolesFor (int lane)
{
    switch (lane)
    {
        case LaneChords: return { Role::Pad, Role::Keys };
        case LaneArp:    return { Role::Pluck, Role::Bell, Role::Keys };
        case LaneMelody: return { Role::Lead, Role::Bell, Role::Pluck, Role::Keys };
        default:         return { Role::Bass };
    }
}

std::vector<Sound> GhostProcessor::suitable (int lane) const
{
    const auto roles = rolesFor (lane);
    std::vector<Sound> v;
    for (auto& s : Catalog::get().snapshot())
        if (std::find (roles.begin(), roles.end(), s.role) != roles.end()) v.push_back (s);
    return v;
}

juce::String GhostProcessor::nextGood (int lane)
{
    auto v = suitable (lane);
    if (v.empty()) return "no sounds found";
    // a mix of all three synths: pick the synth first (only the installed ones), then a sound of it
    std::array<std::vector<int>, kEngines> by;
    for (int i = 0; i < (int) v.size(); ++i)
        if (Hosted::findBundle (engineBundle (v[(size_t) i].engine)) != juce::File() && v[(size_t) i].id != lanes[(size_t) lane].soundId)
            by[(size_t) v[(size_t) i].engine].push_back (i);
    std::vector<int> engines; for (int e = 0; e < kEngines; ++e) if (! by[(size_t) e].empty()) engines.push_back (e);
    if (engines.empty()) return "none of the three synths is installed";
    static std::mt19937 rng ((unsigned) juce::Time::currentTimeMillis());
    const int e = engines[(size_t) (rng() % engines.size())];
    const auto& list = by[(size_t) e];
    const auto& s = v[(size_t) list[(size_t) (rng() % list.size())]];
    juce::String err;
    return loadSound (lane, s, err) ? s.name : err;
}

juce::String GhostProcessor::stepSound (int lane, int dir)
{
    auto all = Catalog::get().snapshot();
    if (all.empty()) return "no sounds found";
    auto& L = lanes[(size_t) lane];
    // steps through the lane's kind of sound across all three synths (in the list's order: GP2, P2, ATLAS, then yours)
    auto v = suitable (lane);
    if (v.empty()) v = all;
    int cur = -1;
    for (int i = 0; i < (int) v.size(); ++i) if (v[(size_t) i].id == L.soundId) { cur = i; break; }
    for (int tries = 0; tries < (int) v.size(); ++tries)
    {
        cur = cur < 0 ? (dir > 0 ? 0 : (int) v.size() - 1) : (cur + dir + (int) v.size()) % (int) v.size();
        const auto& s = v[(size_t) cur];
        if (Hosted::findBundle (engineBundle (s.engine)) == juce::File()) continue;
        juce::String err;
        return loadSound (lane, s, err) ? s.name : err;
    }
    return "no other sound";
}

void GhostProcessor::fxChanged (int lane)
{
    auto& L = lanes[(size_t) lane];
    L.fx.serviceMainThread();
    if (L.fx.takeRestartRequest() || L.fx.latencyWanted() != L.fx.latency())
    {
        const juce::SpinLock::ScopedLockType g (L.audioLock);
        L.fx.activate (sr);
    }
    realign();
    ++uiVersion;
}

void GhostProcessor::realign()
{
    int lat[kLanes] {}; int most = 0;
    for (int l = 0; l < kLanes; ++l)
    {
        auto& L = lanes[(size_t) l];
        L.pluginLatency = L.plugin ? L.plugin->getLatencySamples() : 0;
        L.fxLatency = (int) L.fx.latency();
        lat[l] = L.pluginLatency + L.fxLatency;
        most = juce::jmax (most, lat[l]);
    }
    beatsLatency = beats ? beats->getLatencySamples() : 0;
    most = juce::jlimit (0, kMaxAlign - 1, juce::jmax (most, beatsLatency));
    for (int l = 0; l < kLanes; ++l) lanes[(size_t) l].align.d = juce::jlimit (0, kMaxAlign - 1, most - lat[l]);
    beatsAlign.d = juce::jlimit (0, kMaxAlign - 1, most - beatsLatency);
    if (most != alignTo) { alignTo = most; setLatencySamples (most); }
}

void GhostProcessor::togglePlay() { ownPlay = ! ownPlay.load(); }

void GhostProcessor::timerCallback()
{
    if (! partsOpened) { partsOpened = true; openParts(); return; }
    // the FX racks: build what an edit asked for, restart one whose delay changed
    for (int l = 0; l < kLanes; ++l)
    {
        auto& L = lanes[(size_t) l];
        if (L.fx.takeMainThreadRequest()) L.fx.serviceMainThread();
        if (L.fx.takeRestartRequest()) { const juce::SpinLock::ScopedLockType g (L.audioLock); L.fx.activate (sr); realign(); ++uiVersion; }
    }
    // a part's own delay can change (a synth's FX): line up again
    bool changed = false;
    for (auto& L : lanes) if (L.plugin && L.plugin->getLatencySamples() != L.pluginLatency) changed = true;
    if (beats && beats->getLatencySamples() != beatsLatency) changed = true;
    if (changed || needRealign.exchange (false)) realign();
    if (++tick % 20 == 0) setArpSoundOff();   // (if ArpEZator's own switches were changed in its window, GHOST puts them back)
}

// ---------------------------------------------------------------- the song (saved in the project)
void GhostProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::ValueTree t ("GHOST");
    t.setProperty ("version", GHOST_VERSION_STRING, nullptr);
    t.setProperty ("selected", selectedLane.load(), nullptr);
    t.appendChild (apvts.copyState(), nullptr);
    if (restoreTree.isValid() && ! partsOpened)
    {   // the parts never opened (saved before the first tick): keep what was loaded, unchanged
        juce::MemoryOutputStream mo (dest, false); restoreTree.writeToStream (mo); return;
    }
    auto blob = [] (juce::AudioPluginInstance* p) { juce::MemoryBlock mb; if (p) p->getStateInformation (mb); return mb.toBase64Encoding(); };
    t.setProperty ("arp", blob (arp.get()), nullptr);
    t.setProperty ("beats", blob (beats.get()), nullptr);
    for (int l = 0; l < kLanes; ++l)
    {
        auto& L = lanes[(size_t) l];
        juce::ValueTree c ("LANE");
        c.setProperty ("index", l, nullptr);
        if (L.hasEngine) c.setProperty ("engine", engineKey (L.engine), nullptr);
        c.setProperty ("sound", L.soundId, nullptr);
        c.setProperty ("name", L.soundName, nullptr);
        c.setProperty ("category", L.soundCategory, nullptr);
        c.setProperty ("synth", blob (L.plugin.get()), nullptr);
        c.setProperty ("fx", juce::String (L.fx.toState()), nullptr);
        t.appendChild (c, nullptr);
    }
    juce::MemoryOutputStream mo (dest, false);
    t.writeToStream (mo);
}

void GhostProcessor::setStateInformation (const void* data, int size)
{
    auto t = juce::ValueTree::readFromData (data, (size_t) size);
    if (! t.hasType ("GHOST")) return;
    if (auto p = t.getChildWithName (apvts.state.getType()); p.isValid()) apvts.replaceState (p.createCopy());
    selectedLane = (int) t.getProperty ("selected", 0);
    restoreTree = t.createCopy();
    if (partsOpened)
    {
        if (juce::MessageManager::getInstance()->isThisTheMessageThread()) applyRestore();
        else juce::MessageManager::callAsync ([this] { applyRestore(); });
    }
}

void GhostProcessor::applyRestore()
{
    auto t = restoreTree; restoreTree = {};
    if (! t.isValid()) return;
    auto unblob = [] (juce::AudioPluginInstance* p, const juce::String& b64)
    {
        if (p == nullptr || b64.isEmpty()) return;
        juce::MemoryBlock mb; if (mb.fromBase64Encoding (b64) && mb.getSize() > 0) p->setStateInformation (mb.getData(), (int) mb.getSize());
    };
    unblob (arp.get(), t["arp"].toString());
    setArpSoundOff();
    unblob (beats.get(), t["beats"].toString());
    for (auto c : t)
    {
        if (! c.hasType ("LANE")) continue;
        const int l = (int) c["index"];
        if (l < 0 || l >= kLanes) continue;
        auto& L = lanes[(size_t) l];
        Engine e;
        if (! engineFromKey (c["engine"].toString(), e)) continue;
        juce::String err;
        if (! openLanePlugin (l, e, err)) { L.error = err; L.soundName = c["name"].toString(); continue; }
        unblob (L.plugin.get(), c["synth"].toString());
        L.soundId = c["sound"].toString(); L.soundName = c["name"].toString(); L.soundCategory = c["category"].toString(); L.error = {};
        const auto fx = c["fx"].toString().toStdString();
        if (fx.empty() || ! L.fx.fromState (fx)) L.fx.resetToDefaults();
        {
            const juce::SpinLock::ScopedLockType g (L.audioLock);
            L.fx.activate (sr);
        }
    }
    realign();
    ++uiVersion;
}

juce::AudioProcessorEditor* GhostProcessor::createEditor() { return new GhostEditor (*this); }
} // namespace ghost

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ghost::GhostProcessor(); }
