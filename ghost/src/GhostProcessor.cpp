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
static const char* kGraphBundle = "Ghost Graph.vst3";
static constexpr int kMaxAlign = 65536;

// a short diary of what GHOST opened (to find out why a part is missing): Library/Application Support/GhostBlock/GHOST/ghost.log
static void diary (const juce::String& line)
{
    const auto f = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("GhostBlock/GHOST/ghost.log");
    f.getParentDirectory().createDirectory();
    if (f.getSize() > 512 * 1024) f.deleteFile();
    f.appendText (juce::Time::getCurrentTime().toString (true, true, true, true) + "  " + line + "\n");
}   // the most delay GHOST lines up (FX 16384 + a synth's own)

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
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { n + "_level", 1 }, N + " Level", NormalisableRange<float> (-60.0f, 6.0f, 0.1f, 2.5f), -8.0f, AudioParameterFloatAttributes().withLabel ("dB")));
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { n + "_tone", 1 }, N + " Tone", NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));
        l.add (std::make_unique<AudioParameterBool> (ParameterID { n + "_mute", 1 }, N + " Mute", false));
    }
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "beats_level", 1 }, "Beats Level", NormalisableRange<float> (-60.0f, 6.0f, 0.1f, 2.5f), -4.0f, AudioParameterFloatAttributes().withLabel ("dB")));
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
    aliveFlag->store (false);
    stopTimer();
    for (auto& L : lanes) { const juce::SpinLock::ScopedLockType g (L.audioLock); L.plugin.reset(); }
    { const juce::SpinLock::ScopedLockType g (arpLock); arp.reset(); }
    { const juce::SpinLock::ScopedLockType g (beatsLock); beats.reset(); }
    { const juce::SpinLock::ScopedLockType g (graphLock); graph.reset(); }
    delete graphLive.exchange (nullptr);
    retired.clear();
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
}

// The hosted plug-ins are prepared on the message thread only, and never while GHOST holds a lock: a VST3's prepare waits
// for the message thread, so preparing it from FL's audio-setup thread while the message thread waited for a lane froze
// the Mac. Until they're ready again the audio thread leaves them alone.
void GhostProcessor::prepareHosted()
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
    hostedReady = false;
    std::vector<juce::AudioPluginInstance*> all { arp.get(), beats.get(), graph.get() };
    for (auto& L : lanes) all.push_back (L.plugin.get());
    for (auto* p : all)
        if (p != nullptr && (p->getSampleRate() != sr || p->getBlockSize() != maxBlock))
        {
            p->releaseResources();
            p->setRateAndBufferSizeDetails (sr, maxBlock);
            p->prepareToPlay (sr, maxBlock);
        }
    hostedReady = true;
    needRealign = true;
}

void GhostProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    maxBlock = juce::jmax (32, samplesPerBlock);
    scratch.setSize (32, maxBlock, false, true, false);
    arpScratch.setSize (32, maxBlock, false, true, false);
    arpMidiIn.ensureSize (8192); arpMidiOut.ensureSize (8192);
    hostedReady = false;   // (the hosted plug-ins rest until they're prepared for this rate / size, on the message thread)
    for (auto& L : lanes)
    {
        const juce::SpinLock::ScopedLockType g (L.audioLock);
        prepareLane (L);
    }
    graphDrums.ensureSize (4096);
    {
        const juce::SpinLock::ScopedLockType g (beatsLock);
        beatsAlign.prepare (kMaxAlign);
    }
    for (auto* g : { &gBeats, &gMaster }) g->reset (sr, 0.03);
    for (auto& g : gLane) g.reset (sr, 0.03);
    safety.prepare ({ sr, (juce::uint32) maxBlock, 2 }); safety.setThreshold (-0.3f); safety.setRelease (80.0f); safety.reset();
    prepared = true;
    needRealign = true;
    if (juce::MessageManager::getInstance()->isThisTheMessageThread()) prepareHosted();
    else needPrepareHosted = true;   // (the timer does it)
}

void GhostProcessor::releaseResources()
{
    // (the hosted plug-ins stay prepared: releasing them here, maybe on FL's audio-setup thread, is what can freeze)
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
    if (! prepared || n <= 0 || ! hostedReady.load()) { midi.clear(); return; }

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

        // 1. ArpEZator writes this block's notes (its own sound is off: GHOST's lanes play them) - or Ghost Graph does
        for (auto& L : lanes) L.midi.clear();
        graphDrums.clear();
        const int src = source.load();
        if (src != lastSource)
        {   // switching songs: what the other one was holding stops
            for (auto& L : lanes) for (int ch = 1; ch <= 1; ++ch) L.midi.addEvent (juce::MidiMessage::allNotesOff (ch), 0);
            graphFlush (0);
            graphPrevEnd = -1.0;
            lastSource = src;
        }
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
                    if (src != FromArpez) break;
                    auto msg = m.getMessage();
                    const int ch = msg.getChannel();
                    for (int l = 0; l < kLanes; ++l)
                        if (ch == laneMidiChannel (l)) { msg.setChannel (1); if (msg.isNoteOn()) ++lanes[(size_t) l].notesIn; lanes[(size_t) l].midi.addEvent (msg, m.samplePosition); }
                }
            }
        }
        // 1b. Ghost Graph runs (its window shows the playhead); its notes go to the lanes and to Ghosty Beats' drums
        {
            const juce::SpinLock::ScopedTryLockType g (graphLock);
            if (g.isLocked() && graph && (src == FromGraph || graphVisible.load()))   // (Ghost Graph only runs when it's used)
            {
                auto view = viewOf (arpScratch, *graph, len);
                arpMidiIn.clear();
                graph->setPlayHead (&hostedHead);
                graph->processBlock (view, arpMidiIn);   // (its own sound is not heard)
            }
        }
        if (src == FromGraph) graphSchedule (info, at, len, bpm);
        else if (graphWasPlaying) { graphFlush (0); graphWasPlaying = false; graphPrevEnd = -1.0; }

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
                if (src == FromGraph)
                {   // GRAPH: Ghost Graph's drum hits play Ghosty Beats' sounds (its own pattern rests)
                    beatsHead.info = info; beatsHead.info->setIsPlaying (false);
                    beats->setPlayHead (&beatsHead);
                    beats->processBlock (view, graphDrums);
                }
                else
                {
                    empty.clear();
                    beats->setPlayHead (&hostedHead);
                    beats->processBlock (view, empty);
                }
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

// GHOST opens its parts one at a time, one per tick of the message thread, so the window and FL keep answering in between
// (opening a plug-in can take a second or more; all of them in one go froze the Mac). ArpEZator first, then the four
// lanes, then Ghosty Beats. Ghost Graph opens only when the GRAPH page is opened (or the song was saved on GRAPH).
bool GhostProcessor::openNextPart()
{
    const double rate = prepared ? sr : 48000.0;
    const int blk = prepared ? maxBlock : 512;
    auto unblob = [] (juce::AudioPluginInstance* p, const juce::String& b64)
    {
        if (p == nullptr || b64.isEmpty()) return;
        juce::MemoryBlock mb; if (mb.fromBase64Encoding (b64) && mb.getSize() > 0) p->setStateInformation (mb.getData(), (int) mb.getSize());
    };
    const auto& t = restoreTree;
    const int stage = openStage++;
    if (stage == 0)
    {
        opening = "ARPEZATOR";
        if (! arp && arpError.isEmpty())
        {
            juce::String e;
            auto p = Hosted::open (kArpBundle, rate, blk, e);
            if (p) { p->setPlayHead (&hostedHead); const juce::SpinLock::ScopedLockType g (arpLock); arp = std::move (p); }
            else arpError = e;
        }
        if (t.isValid())
        {
            unblob (arp.get(), t["arp"].toString());
            source = (int) t.getProperty ("source", (int) FromArpez);
            restoredSong = true; ideaKnown = false;   // (the song's own ArpEZator mutes are kept: they are read, not changed)
        }
        setArpSoundOff();
        ++uiVersion;
        return true;
    }
    if (stage >= 1 && stage <= kLanes)
    {
        const int l = stage - 1;
        auto& L = lanes[(size_t) l];
        opening = juce::String (laneName (l));
        if (! L.hasEngine) { const juce::SpinLock::ScopedLockType g (L.audioLock); prepareLane (L); }
        juce::ValueTree c;
        if (t.isValid()) for (auto ch : t) if (ch.hasType ("LANE") && (int) ch["index"] == l) c = ch;
        Engine e;
        if (c.isValid() && engineFromKey (c["engine"].toString(), e))
        {
            juce::String err;
            if (! openLanePlugin (l, e, err)) { L.error = err; L.soundName = c["name"].toString(); }
            else
            {
                unblob (L.plugin.get(), c["synth"].toString());
                L.soundId = c["sound"].toString(); L.soundName = c["name"].toString(); L.soundCategory = c["category"].toString(); L.error = {};
            }
            const auto fx = c["fx"].toString().toStdString();
            if (fx.empty() || ! L.fx.fromState (fx)) L.fx.resetToDefaults();
            const juce::SpinLock::ScopedLockType g (L.audioLock);
            L.fx.activate (sr);
        }
        else if (L.soundId.isEmpty() && ! t.isValid())
        {   // a new GHOST: a sound on every lane (MIDNIGHT-friendly picks)
            static const char* first[kLanes] { "gp2:factory:Pad/Warm Blanket", "gp2:factory:Pluck/Crystal Pluck", "gp2:factory:Lead/Ghost Whistle", "gp2:factory:Bass/Velvet 808" };
            juce::String e2; if (! loadSound (l, first[l], e2)) L.error = e2;
        }
        ++uiVersion;
        return true;
    }
    if (stage == kLanes + 1)
    {
        opening = "GHOSTY BEATS";
        if (! beats && beatsError.isEmpty())
        {
            juce::String e;
            auto p = Hosted::open (kBeatsBundle, rate, blk, e);
            if (p) { p->setPlayHead (&hostedHead); const juce::SpinLock::ScopedLockType g (beatsLock); beats = std::move (p); }
            else beatsError = e;
        }
        if (t.isValid()) unblob (beats.get(), t["beats"].toString());
        ++uiVersion;
        return true;
    }
    if (stage == kLanes + 2)
    {   // Ghost Graph: now only if the song was on GRAPH (else when its page is first opened)
        if (t.isValid() && (graph != nullptr || (int) t.getProperty ("source", 0) == FromGraph))
        {
            opening = "GHOST GRAPH";
            openGraph();
            unblob (graph.get(), t["graph"].toString());
            pendingGraphState = {};
        }
        else if (t.isValid()) pendingGraphState = t["graph"].toString();   // (given to Ghost Graph when it opens)
        return true;
    }
    // done
    restoreTree = {};
    opening = {};
    realign();
    refreshGraphSong (true);
    pollArp();
    ++uiVersion;
    diary ("opened: ArpEZator " + (arp ? juce::String ("ok") : arpError) + ", Ghosty Beats " + (beats ? juce::String ("ok") : beatsError)
           + ", Ghost Graph " + (graph ? juce::String ("ok") : graphError.isNotEmpty() ? graphError : juce::String ("(when its page opens)")));
    for (int l = 0; l < kLanes; ++l)
        diary (juce::String ("  ") + laneName (l) + ": " + lanes[(size_t) l].soundName + (lanes[(size_t) l].error.isNotEmpty() ? " ERROR " + lanes[(size_t) l].error : juce::String()));
    return false;
}

void GhostProcessor::openParts() { while (openNextPart()) {} partsDone = true; }

void GhostProcessor::openGraph()
{
    if (graph || graphError.isNotEmpty()) return;
    juce::String e;
    auto p = Hosted::open (kGraphBundle, prepared ? sr : 48000.0, prepared ? maxBlock : 512, e);
    if (p) { p->setPlayHead (&hostedHead); const juce::SpinLock::ScopedLockType g (graphLock); graph = std::move (p); }
    else graphError = e;
    if (graph && pendingGraphState.isNotEmpty())
    {
        juce::MemoryBlock mb; if (mb.fromBase64Encoding (pendingGraphState) && mb.getSize() > 0) graph->setStateInformation (mb.getData(), (int) mb.getSize());
        pendingGraphState = {};
    }
    refreshGraphSong (true);
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
    if (! Catalog::hostState (s, state, error)) { L.error = error; ++uiVersion; diary (juce::String (laneName (lane)) + ": " + s.id + " " + error); return false; }
    if (! openLanePlugin (lane, s.engine, error)) { L.error = error; ++uiVersion; diary (juce::String (laneName (lane)) + ": " + s.id + " " + error); return false; }
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
    if (needPrepareHosted.exchange (false)) prepareHosted();
    if (! partsDone) { partsDone = ! openNextPart(); return; }
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
    ++tick;
    if (tick % 20 == 0) setArpSoundOff();   // (if ArpEZator's own switches were changed in its window, GHOST puts them back)
    if (tick % 10 == 5 && graph != nullptr && (source.load() == FromGraph || graphVisible.load())) refreshGraphSong (false);
    if (tick % 40 == 10 && source.load() == FromArpez) pollArp();
    const auto now = juce::Time::getMillisecondCounter();
    retired.erase (std::remove_if (retired.begin(), retired.end(), [now] (const auto& r) { return now - r.first > 2000; }), retired.end());
}

// ---------------------------------------------------------------- the song (saved in the project)
void GhostProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::ValueTree t ("GHOST");
    t.setProperty ("version", GHOST_VERSION_STRING, nullptr);
    t.setProperty ("selected", selectedLane.load(), nullptr);
    t.appendChild (apvts.copyState(), nullptr);
    if (restoreTree.isValid() && ! partsDone)
    {   // the parts are still opening: keep what was loaded, unchanged
        juce::MemoryOutputStream mo (dest, false); restoreTree.writeToStream (mo); return;
    }
    auto blob = [] (juce::AudioPluginInstance* p) { juce::MemoryBlock mb; if (p) p->getStateInformation (mb); return mb.toBase64Encoding(); };
    t.setProperty ("arp", blob (arp.get()), nullptr);
    t.setProperty ("beats", blob (beats.get()), nullptr);
    t.setProperty ("graph", graph != nullptr ? blob (graph.get()) : pendingGraphState, nullptr);
    t.setProperty ("source", source.load(), nullptr);
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
    auto apply = [this, t2 = t.createCopy()]
    {
        restoreTree = t2;
        openStage = 0;        // (the parts take the song one at a time, like when they open)
        partsDone = false;
    };
    if (juce::MessageManager::getInstance()->isThisTheMessageThread()) apply();
    else juce::MessageManager::callAsync ([apply, alive = aliveFlag] { if (alive->load()) apply(); });
}

// ---------------------------------------------------------------- GRAPH
void GhostProcessor::setSource (int s)
{
    s = juce::jlimit (0, 1, s);
    if (s == FromGraph) openGraph();
    if (source.exchange (s) != s) ++uiVersion;
}

void GhostProcessor::retireSong (GraphSong* s)
{
    if (s != nullptr) retired.push_back ({ juce::Time::getMillisecondCounter(), std::unique_ptr<GraphSong> (s) });
}

void GhostProcessor::refreshGraphSong (bool force)
{
    if (! graph) return;
    const auto now = juce::Time::getMillisecondCounter();
    if (beats && (force || (source.load() == FromGraph && now - lastKitRead > 3000)))
    {   // Ghosty Beats' kit can change any time (its SOUNDS page): which track is the kick, the snare, the hat...
        juce::MemoryBlock bs; beats->getStateInformation (bs);
        juce::String kit;
        const auto m = drumMapFromBeatsState (bs, &kit);
        lastKitRead = now;
        if (m != drumMap) { drumMap = m; beatsKit = kit; force = true; }
    }
    juce::MemoryBlock st; graph->getStateInformation (st);
    if (! force && st == lastGraphState) return;
    lastGraphState = st;
    auto song = graphSongFromState (st, drumMap);
    graphShown.beats = song->beats; graphShown.laneParts = song->laneParts; graphShown.drums = song->drums; graphShown.error = song->error;
    retireSong (graphLive.exchange (song.release(), std::memory_order_acq_rel));
    ++uiVersion;
}

void GhostProcessor::graphFlush (int at)
{
    for (int l = 0; l < kLanes; ++l)
        for (int n = 0; n < 128; ++n)
            if (graphActive[(size_t) l][(size_t) n]) { lanes[(size_t) l].midi.addEvent (juce::MidiMessage::noteOff (1, n), at); graphActive[(size_t) l][(size_t) n] = false; }
}

void GhostProcessor::graphSchedule (const juce::AudioPlayHead::PositionInfo& info, int /*at*/, int len, double bpm)
{
    auto* song = graphLive.load (std::memory_order_acquire);
    const auto ppq = info.getPpqPosition();
    if (! info.getIsPlaying() || ! ppq.hasValue() || song == nullptr || song->beats <= 0.0)
    {
        if (graphWasPlaying) graphFlush (0);
        graphWasPlaying = false; graphPrevEnd = -1.0;
        return;
    }
    const double p0 = *ppq, p1 = p0 + (double) len / sr * bpm / 60.0;
    if (graphPrevEnd >= 0.0 && std::abs (p0 - graphPrevEnd) > 0.002) graphFlush (0);   // a jump (loop, a click on the timeline)
    graphWasPlaying = true; graphPrevEnd = p1;
    const double L = song->beats, span = juce::jmax (1.0e-9, p1 - p0);
    const auto& ev = song->events;
    for (double k = std::floor (p0 / L); k <= std::floor (p1 / L); k += 1.0)
    {
        const double base = k * L, lo = juce::jmax (0.0, p0 - base), hi = juce::jmin (L, p1 - base);
        if (hi <= lo) continue;
        auto it = std::lower_bound (ev.begin(), ev.end(), lo, [] (const GraphEvent& e, double b) { return e.beat < b; });
        for (; it != ev.end() && it->beat < hi; ++it)
        {
            const int off = juce::jlimit (0, len - 1, (int) ((base + it->beat - p0) / span * len));
            if (it->target == 4)
            {
                if (it->on) { graphDrums.addEvent (juce::MidiMessage::noteOn (10, it->note, (juce::uint8) juce::jlimit (1, 127, it->velocity)), off); ++graphDrumHits; }
                continue;
            }
            auto& L2 = lanes[(size_t) it->target];
            auto& act = graphActive[(size_t) it->target][(size_t) juce::jlimit (0, 127, it->note)];
            if (it->on)
            {
                if (act) L2.midi.addEvent (juce::MidiMessage::noteOff (1, it->note), off);
                L2.midi.addEvent (juce::MidiMessage::noteOn (1, it->note, (juce::uint8) juce::jlimit (1, 127, it->velocity)), off);
                act = true; ++L2.notesIn;
            }
            else if (act) { L2.midi.addEvent (juce::MidiMessage::noteOff (1, it->note), off); act = false; }
        }
    }
}

// ---------------------------------------------------------------- ArpEZator's lanes
bool GhostProcessor::editArpIdea (const std::function<void (juce::var&)>& edit)
{
    if (! arp) return false;
    juce::MemoryBlock st; arp->getStateInformation (st);
    const auto raw = componentStateOf (st);
    auto xml = juce::AudioProcessor::getXmlFromBinary (raw.getData(), (int) raw.getSize());
    if (xml == nullptr) return false;
    auto idea = juce::JSON::parse (xml->getStringAttribute ("idea"));
    if (! idea.isObject()) return false;
    edit (idea);
    xml->setAttribute ("idea", juce::JSON::toString (idea, true));
    juce::MemoryBlock back; juce::AudioProcessor::copyXmlToBinary (*xml, back);
    const auto host = Catalog::wrapForVst3Host (back);
    arp->setStateInformation (host.getData(), (int) host.getSize());
    setArpSoundOff();
    return true;
}

void GhostProcessor::unmuteArpLane (int lane)
{
    editArpIdea ([lane] (juce::var& idea)
    {
        if (auto* la = idea["lanes"].getArray())
            if (lane >= 0 && lane < la->size())
                if (auto* o = la->getReference (lane).getDynamicObject()) o->setProperty ("mute", false);
    });
    pollArp();
}

void GhostProcessor::pollArp()
{
    if (! arp) return;
    juce::MemoryBlock st; arp->getStateInformation (st);
    const auto raw = componentStateOf (st);
    auto xml = juce::AudioProcessor::getXmlFromBinary (raw.getData(), (int) raw.getSize());
    if (xml == nullptr) return;
    const auto idea = juce::JSON::parse (xml->getStringAttribute ("idea"));
    if (! idea.isObject()) return;
    const juce::String key = idea["mode"].toString() + "|" + idea["words"].toString() + "|" + idea["seed"].toString() + "|" + idea["prompt"].toString() + "|" + idea["seedUsed"].toString();
    bool changed = false;
    if (auto* la = idea["lanes"].getArray())
        for (int l = 0; l < kLanes && l < la->size(); ++l)
        {
            const bool m = (bool) la->getReference (l).getProperty ("mute", false);
            const int n = la->getReference (l)["notes"].isArray() ? la->getReference (l)["notes"].getArray()->size() : 0;
            changed = changed || m != arpMuted[(size_t) l] || n != arpNoteCount[(size_t) l];
            arpMuted[(size_t) l] = m; arpNoteCount[(size_t) l] = n;
        }
    const bool newIdea = ! ideaKnown ? ! restoredSong : key != lastIdeaKey;
    ideaKnown = true; lastIdeaKey = key;
    if (newIdea)
    {   // a new idea: GHOST plays all four lanes (a mood can mute one in ArpEZator, e.g. MIDNIGHT's ARP; GHOST has its own M for that)
        bool any = false;
        for (int l = 0; l < kLanes; ++l) any = any || (arpMuted[(size_t) l] && arpNoteCount[(size_t) l] > 0);
        if (any)
        {
            editArpIdea ([this] (juce::var& idea2)
            {
                if (auto* la = idea2["lanes"].getArray())
                    for (int l = 0; l < kLanes && l < la->size(); ++l)
                        if (arpMuted[(size_t) l] && arpNoteCount[(size_t) l] > 0)
                            if (auto* o = la->getReference (l).getDynamicObject()) o->setProperty ("mute", false);
            });
            for (int l = 0; l < kLanes; ++l) if (arpNoteCount[(size_t) l] > 0) arpMuted[(size_t) l] = false;
            diary ("ArpEZator: a new idea - every lane plays");
            changed = true;
        }
    }
    if (changed) ++uiVersion;
}

juce::AudioProcessorEditor* GhostProcessor::createEditor() { return new GhostEditor (*this); }
} // namespace ghost

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ghost::GhostProcessor(); }
