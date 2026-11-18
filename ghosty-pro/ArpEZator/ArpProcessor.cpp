#include "ArpProcessor.h"
#include "HostNotify.h"
#include "ArpChop.h"
#include "ArpEditor.h"
#include "RecipeJson.h"
#include "UrbanMoods.h"
#if ARPEZATOR_HAS_DATA
 #include "BinaryData.h"
#endif
#include <cmath>
#include <mutex>
#include <cstring>
#include <map>
#include <numeric>
#include <set>

using namespace arp;

namespace
{
// a one-job-at-a-time background worker (LISTEN analysis, file decoding)
class Worker : public juce::Thread
{
public:
    Worker() : juce::Thread ("ArpEZator worker") {}
    ~Worker() override { stopThread (8000); }
    bool post (std::function<void()> fn)
    {
        if (isThreadRunning()) return false;
        job = std::move (fn); startThread(); return true;
    }
    void run() override { if (job) job(); job = nullptr; }
private:
    std::function<void()> job;
};
// the HOOK LAB's worker: the latest job wins (a new GENERATE replaces a waiting one; the running one is cancelled by its flag)
class LabWorker : public juce::Thread
{
public:
    LabWorker() : juce::Thread ("ArpEZator hook lab") {}
    ~LabWorker() override { signalThreadShouldExit(); ev.signal(); stopThread (8000); }
    void submit (std::function<void()> fn)
    {
        { const juce::ScopedLock sl (lock); next = std::move (fn); }
        if (! isThreadRunning()) startThread();
        ev.signal();
    }
    void run() override
    {
        while (! threadShouldExit())
        {
            std::function<void()> j;
            { const juce::ScopedLock sl (lock); j = std::move (next); next = nullptr; }
            if (j) j(); else ev.wait (250);
        }
    }
private:
    juce::CriticalSection lock; std::function<void()> next; juce::WaitableEvent ev;
};

} // namespace
void arpLoadEmbeddedRecipe()
{
#if ARPEZATOR_HAS_DATA
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
        if (juce::String (BinaryData::originalFilenames[i]) == "recipe.json")
        {
            int size = 0;
            if (auto* d = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size))
            {
                arp::Recipe R;
                if (arprecipe::fromVar (juce::JSON::parse (juce::String::fromUTF8 (d, size)), R)) arp::setRecipe (R);
            }
        }
#endif
}
namespace
{
juce::String slug (const juce::String& s) { return s.retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 #-").trim(); }
} // namespace

ArpProcessor::ArpProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                                       .withInput ("Sidechain (LISTEN)", juce::AudioChannelSet::stereo(), false)),
      apvts (*this, nullptr, "ARPEZATOR", layout())
{
    static std::once_flag once; std::call_once (once, [] { arpLoadEmbeddedRecipe(); });
    setNewIdeaMelVer (kMelVerCurrent);   // new ideas get the current TRAP melody writer (loaded sessions keep the one they were saved with)
    worker = std::make_unique<Worker>();
    apvts.state.setProperty ("nPresets", arpsyn::kPresets, nullptr);   // older states (without it) get YOUR SAMPLE moved up on load
    pSound = apvts.getRawParameterValue ("sound"); pVol = apvts.getRawParameterValue ("volume"); pPreset = apvts.getRawParameterValue ("preset");
    pLayerPluck = apvts.getRawParameterValue ("layerPluck"); pLayerOct = apvts.getRawParameterValue ("layerOct"); pStabs = apvts.getRawParameterValue ("stabs");
    pCut = apvts.getRawParameterValue ("cutoff"); pLfoD = apvts.getRawParameterValue ("lfoDepth"); pLfoR = apvts.getRawParameterValue ("lfoRate");
    pDelay = apvts.getRawParameterValue ("delay"); pVerb = apvts.getRawParameterValue ("reverb"); pPump = apvts.getRawParameterValue ("pump");
    pKick = apvts.getRawParameterValue ("kick"); pMidiOut = apvts.getRawParameterValue ("midiOut"); pFollow = apvts.getRawParameterValue ("follow");
    for (int k = 0; k < kLaneVols; ++k) pLaneVol[(size_t) k] = apvts.getRawParameterValue (laneVolId (k));
    pVelScale = apvts.getRawParameterValue ("velScale"); pCh1 = apvts.getRawParameterValue ("midiCh1");
    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter ("bypass"));
    pPlayMode = apvts.getRawParameterValue ("playMode"); pTranspose = apvts.getRawParameterValue ("transpose");
    { static const char* ids[] { "preset_chords", "preset_arp", "preset_melody", "preset_bass" }; for (int k = 0; k < 4; ++k) pLanePreset[(size_t) k] = apvts.getRawParameterValue (ids[k]); }
    for (auto& ch : active) ch.fill (-1.0);
    // the first idea: MIDNIGHT
    I.mode = Midnight; I.seed = 128; applyModeDefaults (I); newIdea (I);
    applyMoodSound (Midnight);   // a new instance starts with MIDNIGHT's sound (a saved project / session restores its own)
    genHist.push_back ({ juce::String (I.words), I.seed }); genIdx = 0;
    labWorker = std::make_unique<LabWorker>(); ideaWorker = std::make_unique<LabWorker>();   // (the harvester's weights load on first lab use: no disk IO here)
    rebuildPattern();
    startTimerHz (20);
}

ArpProcessor::~ArpProcessor()
{
    stopTimer();
    alive->store (false);
    if (labJobCancel) labJobCancel->store (true);
    labWorker.reset(); ideaWorker.reset();
    if (tasteDirty) flushTaste();
    worker.reset();
    delete pending.exchange (nullptr);
    delete current; current = nullptr;
    int s1, n1, s2, n2; retireFifo.prepareToRead (retireFifo.getNumReady(), s1, n1, s2, n2);
    for (int i = 0; i < n1; ++i) delete retired[(size_t) (s1 + i)];
    for (int i = 0; i < n2; ++i) delete retired[(size_t) (s2 + i)];
    retireFifo.finishedRead (n1 + n2);
}

juce::AudioProcessorValueTreeState::ParameterLayout ArpProcessor::layout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pct = [] (float v, int) { return String (juce::roundToInt (v * 100.0f)) + "%"; };
    auto pctIn = [] (const String& t) { return jlimit (0.0f, 1.0f, t.retainCharacters ("0123456789.-").getFloatValue() / 100.0f); };   // "35%" -> 0.35 (round-trips the text)
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "sound", 1 }, "Sound", true));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "volume", 1 }, "Volume", NormalisableRange<float> (0, 1), 0.8f, AudioParameterFloatAttributes().withStringFromValueFunction (pct).withValueFromStringFunction (pctIn)));
    StringArray pres; for (int i = 0; i < arpsyn::kPresets; ++i) pres.add (arpsyn::presetName (i));
    pres.add ("YOUR SAMPLE");   // (appended: saved indices keep their sound)
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "preset", 1 }, "Sound Preset", pres, 0));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "layerPluck", 1 }, "+ Pluck Layer", false));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "layerOct", 1 }, "+ Octave Layer", false));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "stabs", 1 }, "Supersaw Chord Stabs", false));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "cutoff", 1 }, "Cutoff", NormalisableRange<float> (0, 1), 0.6f, AudioParameterFloatAttributes().withStringFromValueFunction (pct).withValueFromStringFunction (pctIn)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "lfoDepth", 1 }, "LFO Depth", NormalisableRange<float> (0, 1), 0.3f, AudioParameterFloatAttributes().withStringFromValueFunction (pct).withValueFromStringFunction (pctIn)));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "lfoRate", 1 }, "LFO Rate", StringArray { "1/4", "1/2", "1 bar", "2 bars", "4 bars" }, 3));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "delay", 1 }, "Delay", NormalisableRange<float> (0, 1), 0.35f, AudioParameterFloatAttributes().withStringFromValueFunction (pct).withValueFromStringFunction (pctIn)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "reverb", 1 }, "Reverb", NormalisableRange<float> (0, 1), 0.3f, AudioParameterFloatAttributes().withStringFromValueFunction (pct).withValueFromStringFunction (pctIn)));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "pump", 1 }, "Pump", NormalisableRange<float> (0, 1), 0.25f, AudioParameterFloatAttributes().withStringFromValueFunction (pct).withValueFromStringFunction (pctIn)));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "kick", 1 }, "Kick", false));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "midiOut", 1 }, "MIDI Out", true));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "follow", 1 }, "Follow My Keys", false));
    // per-lane volume (-60 dB = off .. +6 dB), host-automatable; optionally scales the MIDI out velocity too
    auto db = [] (float v, int) { return v <= -59.9f ? String ("-inf") : String (v, 1) + " dB"; };
    for (int k = 0; k < kLaneVols; ++k)
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { laneVolId (k), 1 }, String (laneVolName (k)) + " Volume", NormalisableRange<float> (-60.0f, 6.0f, 0.0f, 2.2f), 0.0f,
                                                      AudioParameterFloatAttributes().withStringFromValueFunction (db).withLabel ("dB")));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "velScale", 1 }, "Volume Scales MIDI Velocity", true));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "midiCh1", 1 }, "MIDI Out All On Channel 1", true));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "glideBend", 1 }, "Glides As Pitch Bend", true));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "bendRange", 1 }, "Pitch Bend Range", StringArray { "+-2", "+-12", "+-24" }, 1));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "playMode", 1 }, "Play", StringArray { "Always", "On Notes" }, 0));   // PLAY: ALWAYS | ON NOTES (after the old ones)
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "bypass", 1 }, "Bypass", false));   // after the old ones: existing automation keeps its index
    l.add (std::make_unique<AudioParameterInt> (ParameterID { "transpose", 1 }, "Transpose", -24, 24, 0, AudioParameterIntAttributes().withLabel ("st")));   // TRANSPOSE (live offset)
    {   // a sound per lane (LAST): LINKED = as before (the shared sound for ARP / MELODY, the classic pad + bass), a factory sound, or YOUR SAMPLE
        StringArray lp { "LINKED" }; for (int i = 0; i < arpsyn::kPresets; ++i) lp.add (arpsyn::presetName (i)); lp.add ("YOUR SAMPLE");
        static const char* ids[] { "preset_chords", "preset_arp", "preset_melody", "preset_bass" };
        static const char* nms[] { "Chords Sound", "Arp Sound", "Melody Sound", "Bass Sound" };
        for (int k = 0; k < 4; ++k) l.add (std::make_unique<AudioParameterChoice> (ParameterID { ids[k], 1 }, nms[k], lp, 0));
    }
    return l;
}

bool ArpProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    if (layouts.inputBuses.size() > 0)
    {
        const auto in = layouts.getChannelSet (true, 0);
        if (! in.isDisabled() && in != juce::AudioChannelSet::stereo() && in != juce::AudioChannelSet::mono()) return false;
    }
    return true;
}

void ArpProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate > 0 ? sampleRate : 44100.0;
    synth.prepare (sr, juce::jmax (16, samplesPerBlock));
    sync.prepare (sr);
    bypass.prepare (sr, 0.006, bypassParam != nullptr && bypassParam->get());
    noteGate.prepare (sr, 0.006, pPlayMode->load() > 0.5f && heldN == 0);
    demo.prepare (sr);
    capSr = sr;
    const int capLen = (int) (sr * 13.0) + 1;
    if ((int) capBuf.size() != capLen) { capBuf.assign ((size_t) capLen, 0.0f); capFifo.setTotalSize (capLen); }
    capFifo.reset(); capWritten = 0;
    wasPlaying = false; expectTick = -1;
}

// ---------------------------------------------------------------- the audio thread
void ArpProcessor::flushNotes (juce::MidiBuffer& out, int sample)
{
    for (int ch = 0; ch < 16; ++ch)
        for (int p = 0; p < 128; ++p)
            if (active[(size_t) ch][(size_t) p] >= 0) { out.addEvent (juce::MidiMessage::noteOff (ch + 1, p), sample); active[(size_t) ch][(size_t) p] = -1; }
}

int ArpProcessor::transformPitch (int p, double tick, const ArpPattern& P) const
{
    if (heldN <= 0) return p;
    int hl[16]; int m = 0; for (int k = 0; k < 128 && m < 16; ++k) if (held[(size_t) k]) hl[m++] = k;
    if (m == 0) return p;
    const int loop = juce::jmax (1, P.chordLoop);
    const int tt = (int) std::fmod (std::max (0.0, tick + P.offset), (double) loop);
    const ArpPattern::Span* sp = P.spans.empty() ? nullptr : &P.spans.back();
    for (auto& s : P.spans) if (tt >= s.t && tt < s.tEnd) { sp = &s; break; }
    const int pc = p % 12;
    auto nearestWithPc = [p] (int targetPc) { int best = p, bd = 99; for (int q = p - 6; q <= p + 6; ++q) if (((q % 12) + 12) % 12 == targetPc && std::abs (q - p) < bd) { bd = std::abs (q - p); best = q; } return best; };
    if (sp != nullptr && (sp->mask >> pc) & 1)
    {   // the chord-tone order (from the root up) maps onto the held notes in the same order
        int order = 0; for (int iv = 0; iv < 12; ++iv) { const int q = (sp->root + iv) % 12; if (q == pc) break; if ((sp->mask >> q) & 1) ++order; }
        return juce::jlimit (0, 127, nearestWithPc (hl[order % m] % 12));
    }
    const int root = sp != nullptr ? sp->root : 0;
    int shift = ((hl[0] % 12) - root + 12) % 12; if (shift > 6) shift -= 12;
    return juce::jlimit (0, 127, p + shift);
}

void ArpProcessor::bypassedBlock (juce::AudioBuffer<float>& buffer, int n, bool own)
{   // fully bypassed / gated: no sound, no MIDI; the transport keeps counting so the return lands in time
    buffer.clear();
    bool hostPlay = false; double ppq = 0, bpm = current != nullptr ? current->bpm : 120.0;
    if (! own && curT.fromDaw)
    {
        if (curT.bpm > 1) { bpm = curT.bpm; hostBpm = curT.bpm; }
        hostPlay = curT.playing; ppq = curT.ppq;
    }
    hostRunning = hostPlay;
    if (own) bpm = curT.bpm;
    const double tf = tempoFactor(); bpm *= tf; ppq *= tf;   // TEMPO ½x / 2x
    const bool playing = (own ? curT.playing : (hostPlay || internalPlay.load())) && current != nullptr && current->len > 0;
    if (! playing) { wasPlaying = false; playTick = -1.0; freeTick = 0; expectTick = -1; return; }
    const double tps = juce::jlimit (20.0, 800.0, bpm) / 60.0 * PPQ / sr;
    double req = startTickReq.exchange (-1.0); if (restartFree.exchange (false)) req = 0.0;
    if (! hostPlay && ! own && req >= 0.0) freeTick = req;
    const double tick1 = (own ? curT.ppq * tf * PPQ : hostPlay ? ppq * PPQ : freeTick) + n * tps;
    expectTick = tick1; wasPlaying = true;
    playTick = current->offset + std::fmod (std::max (0.0, tick1), (double) current->len);
    if (! hostPlay && ! own) freeTick = tick1;
}
void ArpProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    midi.clear();
    flushNotes (midi, 0);
    synth.allOff (true);
    buffer.clear();
    wasPlaying = false; expectTick = -1; playTick = -1.0;
}

void ArpProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (auto* p = pending.exchange (nullptr))
    {
        if (current != nullptr)
        {
            int s1, n1, s2, n2; retireFifo.prepareToWrite (1, s1, n1, s2, n2);
            if (n1 > 0) { retired[(size_t) s1] = current; retireFifo.finishedWrite (1); }
            else if (n2 > 0) { retired[(size_t) s2] = current; retireFifo.finishedWrite (1); }
            // (a full retire queue would leak one pattern; it holds 64 and the timer empties it 20x a second)
        }
        current = p;
    }
    // SYNC: one resolve per block (DAW or the plugin's own clock)
    const auto hostPos = getPlayHead() != nullptr ? getPlayHead()->getPosition() : juce::Optional<juce::AudioPlayHead::PositionInfo> {};
    hostPresent = hostPos.hasValue();
    curT = sync.resolve (hostPos, n);
    const bool own = syncOwn(); const bool modeFlip = own != lastOwn; lastOwn = own;
    // BYPASS: the engage edge stops every note (preview + MIDI out); fully bypassed = silence, no MIDI, incoming MIDI ignored, the clock runs on
    bypass.set (bypassParam != nullptr && bypassParam->get());
    const bool byp = bypass.isBypassed();
    // MIDI in: held keys (FOLLOW MY KEYS); the output carries only what ArpEZator plays
    for (const auto meta : midi)
    {
        if (byp) break;
        const auto m = meta.getMessage();
        if (m.isNoteOn()) { if (! held[(size_t) m.getNoteNumber()]) ++heldN; held[(size_t) m.getNoteNumber()] = true; }
        else if (m.isNoteOff()) { if (held[(size_t) m.getNoteNumber()]) --heldN; held[(size_t) m.getNoteNumber()] = false; }
        else if (m.isAllNotesOff() || m.isAllSoundOff()) { held.fill (false); heldN = 0; }
    }
    heldN = juce::jmax (0, heldN); heldCount = heldN;
    midi.clear();
    // PLAY: ON NOTES: only while a MIDI note is held (FL's channel mute stops the notes, so it stops the plugin); ALWAYS = as before
    noteGate.set (pPlayMode->load() > 0.5f && heldN == 0);
    const bool hush = byp || noteGate.isBypassed();   // fading out: nothing new starts
    if (bypass.takeEngageEdge()) { flushNotes (midi, 0); synth.allOff (false); }
    if (noteGate.takeEngageEdge()) { flushNotes (midi, 0); synth.allOff (false); }
    if (modeFlip) { flushNotes (midi, 0); synth.allOff (false); expectTick = -1; }   // SYNC switched: every sounding note ends
    if (bypass.silent() || noteGate.silent()) { bypassedBlock (buffer, n, own); return; }

    // LISTEN: copy the sidechain into the FIFO (no allocation here)
    if (capturing.load() && getBusCount (true) > 0)
    {
        auto in = getBusBuffer (buffer, true, 0);
        if (in.getNumChannels() > 0)
        {
            int s1, n1, s2, n2; capFifo.prepareToWrite (n, s1, n1, s2, n2);
            auto put = [&] (int start, int count, int offset)
            {
                for (int i = 0; i < count; ++i)
                {
                    float v = 0; for (int c = 0; c < in.getNumChannels(); ++c) v += in.getSample (c, offset + i);
                    capBuf[(size_t) (start + i)] = v / (float) in.getNumChannels();
                }
            };
            put (s1, n1, 0); put (s2, n2, n1);
            capFifo.finishedWrite (n1 + n2); capWritten += n1 + n2;
        }
    }
    for (int c = 0; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);

    arpsyn::Params SP;
    {   // the sounds: the shared one (or YOUR SAMPLE for the linked ARP / MELODY) + each lane's own
        const int mainP = (int) pPreset->load(); const bool mainSample = mainP >= arpsyn::kPresets;
        SP.preset = mainSample ? (int) arpsyn::Neon : mainP; SP.sampleMask = 0;
        for (int l = 0; l < 4; ++l)
        {
            const int v = (int) pLanePreset[(size_t) l]->load();
            SP.lanePreset[l] = v >= 1 && v <= arpsyn::kPresets ? v - 1 : -1;
            if (v == arpsyn::kPresets + 1 || (v == 0 && mainSample && (l == LaneArp || l == LaneMelody))) SP.sampleMask |= 1 << l;
        }
        SP.auditionSample = ((SP.sampleMask >> juce::jlimit (0, 3, selLane)) & 1) != 0 || forceSampleAudition.load();
        const auto* sd = sampleHand.acquire();
        const auto smpPar = smpBox.read(); smpBox.remember (smpPar);
        synth.setSample (sd != nullptr && sd->length() >= 16 ? sd : nullptr, smpPar);
    }
    SP.layerPluck = pLayerPluck->load() > 0.5f; SP.layerOct = pLayerOct->load() > 0.5f; SP.stabs = pStabs->load() > 0.5f;
    SP.cutoff = pCut->load(); SP.lfoDepth = pLfoD->load(); SP.lfoRate = (int) pLfoR->load(); SP.delay = pDelay->load(); SP.reverb = pVerb->load();
    SP.pump = pPump->load(); SP.volume = pVol->load(); SP.kick = pKick->load() > 0.5f;
    for (int l = 0; l < 4; ++l) { SP.fxDrive[l] = laneFx[(size_t) (l * 4)].load(); SP.fxTone[l] = laneFx[(size_t) (l * 4 + 1)].load(); SP.fxEcho[l] = laneFx[(size_t) (l * 4 + 2)].load(); SP.fxSpace[l] = laneFx[(size_t) (l * 4 + 3)].load(); }
    {   // HOOK LAB PIANO TEST: the plainest possible playback (the toggle flushes the voices; the sends fade over ~50 ms instead of cutting)
        const bool pt = current != nullptr && current->pianoTest;
        const float step = (float) juce::jlimit (0.02, 1.0, n / (0.05 * sr));
        pianoFade = pt ? juce::jmin (1.0f, pianoFade + step) : juce::jmax (0.0f, pianoFade - step);
        if (pt) { SP.preset = arpsyn::Piano; SP.layerPluck = SP.layerOct = SP.stabs = false; SP.lfoDepth = 0; SP.kick = false; SP.sampleMask = 0; SP.auditionSample = false; for (auto& lp : SP.lanePreset) lp = -1; }
        const float k = 1.0f - pianoFade; SP.delay *= k; SP.reverb *= k; SP.pump *= k;
    }
    static const int volOfLane[] { 0, 1, 2, 3 };   // lane -> lane volume (chords, arp, melody, bass; 4 = kick)
    float laneG[kLaneVols]; for (int k = 0; k < kLaneVols; ++k) { laneG[k] = dbToGain (pLaneVol[(size_t) k]->load()); SP.laneGain[k] = laneG[k]; }
    const bool velScale = pVelScale->load() > 0.5f, allCh1 = pCh1->load() > 0.5f;
    {   // audition (piano keys / clicked notes): straight to the preview synth, transport or not
        int s1, n1, s2, n2; audFifo.prepareToRead (audFifo.getNumReady(), s1, n1, s2, n2);
        auto doAud = [&] (const AudEv& a) { const int chain = -1000 - a.pitch; if (a.on) { synth.leadRelease (chain); synth.leadStart (9, chain, (float) a.pitch, 0.85f, (int) (sr * 6), SP); } else synth.leadRelease (chain); };
        for (int i = 0; i < n1; ++i) doAud (audBuf[(size_t) (s1 + i)]);
        for (int i = 0; i < n2; ++i) doAud (audBuf[(size_t) (s2 + i)]);
        audFifo.finishedRead (n1 + n2);
    }
    const bool soundOn = pSound->load() > 0.5f, midiOut = pMidiOut->load() > 0.5f, follow = pFollow->load() > 0.5f;
    const int trNow = (int) std::lround (pTranspose->load()), trMask = transposeMask.load();
    followKeys = follow;

    // transport: the host when it plays, else PLAY (free run at the idea's tempo)
    bool hostPlay = false; double ppq = 0, bpm = current != nullptr ? current->bpm : 120.0;
    if (! own && hostPos.hasValue())
    {   // SYNC: DAW (exactly the old host reading)
        if (auto b = hostPos->getBpm()) { if (*b > 1) { bpm = *b; hostBpm = *b; } }
        hostPlay = hostPos->getIsPlaying();
        if (auto q = hostPos->getPpqPosition()) ppq = *q; else hostPlay = false;
    }
    hostRunning = hostPlay;
    if (own) bpm = curT.bpm;                                   // SYNC: OWN: the plugin's own tempo
    else if (! hostPlay && current != nullptr) bpm = current->bpm;
    const double tf = tempoFactor(); bpm *= tf; ppq *= tf;     // TEMPO ½x / 2x: the multiple of the tempo, still on the DAW's bars (ppq 0 = bar 1 for both)
    bpm = juce::jlimit (10.0, 800.0, bpm);
    const bool playing = (own ? curT.playing : (hostPlay || internalPlay.load())) && current != nullptr && current->len > 0;
    const double tps = bpm / 60.0 * PPQ / sr;
    {   // HOOK LAB auditions start at their bar 1; SPACE starts at the PLAY cursor (the host's own transport is never touched)
        double req = startTickReq.exchange (-1.0);
        if (restartFree.exchange (false)) req = 0.0;
        if (! hostPlay && ! own && req >= 0.0) { freeTick = req; expectTick = -1; flushNotes (midi, 0); synth.allOff (false); }
    }
    const double tick0 = own ? curT.ppq * tf * PPQ : hostPlay ? ppq * PPQ : freeTick;
    if (needFlush.exchange (false)) { flushNotes (midi, 0); synth.allOff (false); }

    if (! playing)
    {
        if (wasPlaying) { flushNotes (midi, 0); synth.allOff (false); }
        wasPlaying = false; playTick = -1.0; freeTick = 0; expectTick = -1;
        if (soundOn) synth.render (buffer.getWritePointer (0), buffer.getWritePointer (juce::jmin (1, buffer.getNumChannels() - 1)), n, SP, bpm, 0, tps, false);
        if (buffer.getNumChannels() > 1 && ! soundOn) buffer.clear();
        demo.process (buffer, license.isUnlocked());
        noteGate.applyMute (buffer); bypass.applyMute (buffer);
        return;
    }
    if (wasPlaying && std::abs (tick0 - expectTick) > juce::jmax (1.0, tps * 4.0))
    {   // the host jumped / looped
        flushNotes (midi, 0); synth.allOff (false);
        if (hostPlay && tick0 < expectTick - PPQ)
        {   // a DAW loop: how long is it? (a short one replays only the idea's start: the editor says so once)
            const int span = (int) std::lround (expectTick - tick0);
            if (std::abs (span - hostLoopTicks.load()) < PPQ / 2) hostLoopCount.fetch_add (1); else { hostLoopTicks = span; hostLoopCount = 1; }
        }
    }
    wasPlaying = true;
    const double tick1 = tick0 + n * tps;
    expectTick = tick1;
    auto offsetOf = [&] (double t) { return juce::jlimit (0, n - 1, (int) std::floor ((t - tick0) / tps + 1.0e-7)); };

    // the MIDI out of this block, collected first so a pitch never overlaps itself on a channel (note-off before note-on)
    struct OEv { int s; uint8_t ord, type, ch, p, v; int val; };   // ord: 0 off of an earlier note, 1 controller / bend, 2 on, 3 off of a note that began on this sample; type 0 off 1 on 2 bend 3 cc
    OEv oev[2048]; int no = 0;
    // note-offs that fall inside this block
    for (int ch = 0; ch < 16; ++ch)
        for (int p = 0; p < 128; ++p)
        {
            double& a = active[(size_t) ch][(size_t) p];
            if (a >= 0 && a < tick1) { if (no < 2048) oev[no++] = { offsetOf (a), 0, 0, (uint8_t) ch, (uint8_t) p, 0, 0 }; a = -1; }
        }

    const ArpPattern& P = *current;
    const double L = P.len;
    struct SEv { int off; const ArpPattern::Ev* e; double base; };
    SEv sev[1024]; int ns = 0;
    const bool silentFollow = follow && heldN == 0;
    const long kStart = (long) std::floor (tick0 / L), kEnd = (long) std::floor ((tick1 - 1.0e-9) / L);
    for (long k = kStart; k <= kEnd; ++k)
    {
        const double base = (double) k * L, lo = tick0 - base, hi = tick1 - base;
        // MIDI out
        auto it = std::lower_bound (P.midi.begin(), P.midi.end(), lo, [] (const ArpPattern::MEv& e, double t) { return e.tick < t; });
        for (; it != P.midi.end() && it->tick < hi; ++it)
        {
            if (! midiOut || silentFollow || hush) continue;
            const double at = base + it->tick; const int off = offsetOf (at);
            if (it->kind != 0)
            {   // pitch bend / controller (glides, FATTEN swells); follow mode leaves them out
                if (follow || no >= 2048) continue;
                const int cch = allCh1 ? 0 : it->ch - 1;
                oev[no++] = { off, 1, (uint8_t) (it->kind == 1 ? 2 : 3), (uint8_t) cch, it->pitch, 0, it->val };
                continue;
            }
            int pitch = it->pitch; if (follow && it->lane != LaneChords) pitch = transformPitch (pitch, it->tick, P);
            else if (trNow != 0 && ((trMask >> it->lane) & 1)) pitch = foldPitch (pitch + trNow, it->lane);
            const int ch = allCh1 ? 0 : it->ch - 1;
            int vel = it->vel;
            if (velScale) { const float g = laneG[volOfLane[juce::jlimit (0, 3, (int) it->lane)]]; if (g < 0.002f) continue; vel = juce::jlimit (1, 127, (int) std::lround (vel * juce::jmin (1.0f, g))); }
            double& a = active[(size_t) ch][(size_t) pitch];
            bool dup = false;
            for (int q = 0; q < no; ++q)
                if (oev[q].ch == ch && oev[q].p == pitch && oev[q].type <= 1)
                {
                    if (oev[q].type == 1 && oev[q].s == off) dup = true;                                  // the same pitch twice on one sample: keep one
                    else if (oev[q].type == 0 && oev[q].s > off) { oev[q].s = off; oev[q].ord = 0; }     // an earlier note still ringing: end it here
                }
            if (dup || no + 3 > 2048) continue;
            if (a >= 0) oev[no++] = { off, 0, 0, (uint8_t) ch, (uint8_t) pitch, 0, 0 };
            oev[no++] = { off, 2, 1, (uint8_t) ch, (uint8_t) pitch, (uint8_t) vel, 0 };
            a = at + it->dur;
            if (a < tick1) { const int s2 = juce::jmax (off, offsetOf (a)); oev[no++] = { s2, (uint8_t) (s2 == off ? 3 : 0), 0, (uint8_t) ch, (uint8_t) pitch, 0, 0 }; a = -1; }
        }
        // synth events
        auto jt = std::lower_bound (P.ev.begin(), P.ev.end(), lo, [] (const ArpPattern::Ev& e, double t) { return e.tick < t; });
        for (; jt != P.ev.end() && jt->tick < hi && ns < 1024; ++jt) sev[ns++] = { offsetOf (base + jt->tick), &*jt, base };
    }
    {   // hand the MIDI to the host in time order (insertion sort: a few events per block)
        for (int i = 1; i < no; ++i) { const OEv e = oev[i]; int j = i - 1; while (j >= 0 && (oev[j].s > e.s || (oev[j].s == e.s && oev[j].ord > e.ord))) { oev[j + 1] = oev[j]; --j; } oev[j + 1] = e; }
        const bool licensed = license.isUnlocked();
        for (int i = 0; i < no; ++i)
        {
            const auto& e = oev[i]; const int c = e.ch + 1;
            if (e.type == 1 && demo.midiMuted (e.s, licensed)) continue;   // demo: the MIDI out has the same 2 s holes as the audio
            midi.addEvent (e.type == 1 ? juce::MidiMessage::noteOn (c, e.p, (juce::uint8) e.v) : e.type == 0 ? juce::MidiMessage::noteOff (c, e.p)
                         : e.type == 2 ? juce::MidiMessage::pitchWheel (c, juce::jlimit (0, 16383, e.val)) : juce::MidiMessage::controllerEvent (c, e.p, juce::jlimit (0, 127, e.val)), e.s);
        }
    }
    // the little kick on every beat
    int kicks[64]; int nk = 0;
    if (SP.kick && ! hush) for (double b = std::ceil (tick0 / PPQ) * PPQ; b < tick1 && nk < 64; b += PPQ) kicks[nk++] = offsetOf (b);

    float* outL = buffer.getWritePointer (0);
    float* outR = buffer.getWritePointer (juce::jmin (1, buffer.getNumChannels() - 1));
    int cursor = 0, ki = 0;
    auto renderTo = [&] (int to)
    {
        if (to > cursor) { if (soundOn) synth.render (outL + cursor, outR + cursor, to - cursor, SP, bpm, tick0 + cursor * tps, tps, true); cursor = to; }
    };
    for (int i = 0; i <= ns; ++i)
    {
        const int off = i < ns ? sev[i].off : n;
        while (ki < nk && kicks[ki] <= off) { renderTo (kicks[ki]); synth.kickHit(); ++ki; }
        renderTo (off);
        if (i == ns || ! soundOn || silentFollow || hush) continue;
        const ArpPattern::Ev& e = *sev[i].e;
        int pitch = e.pitch; if (follow && e.type != ArpPattern::Chord) pitch = transformPitch (pitch, e.tick, P);
        else if (trNow != 0 && ((trMask >> e.lane) & 1)) pitch = foldPitch (pitch + trNow, e.lane);
        const int lenS = juce::jmax (1, (int) std::lround (e.dur / tps));
        switch (e.type)
        {
            case ArpPattern::LeadStart: synth.leadStart (e.lane, e.chain, (float) pitch, e.vel, lenS, SP, e.energy, e.fat, e.idx); break;
            case ArpPattern::LeadLegato: if (synth.hasChain (e.chain)) synth.leadLegato (e.chain, (float) pitch, e.vel, e.fat); else synth.leadStart (e.lane, e.chain, (float) pitch, e.vel, lenS, SP, e.energy, e.fat, e.idx); break;
            case ArpPattern::LeadGlide: synth.leadGlide (e.chain, (float) pitch, lenS); break;
            case ArpPattern::Chord:
            {
                int ps[arpsyn::kMaxPitches]; int cnt = 0;
                if (follow) { for (int k = 0; k < 128 && cnt < arpsyn::kMaxPitches; ++k) if (held[(size_t) k]) ps[cnt++] = k; }
                else for (int k = 0; k < e.n && cnt < arpsyn::kMaxPitches; ++k) ps[cnt++] = trNow != 0 && (trMask & 1) ? foldPitch (e.ps[(size_t) k] + trNow, LaneChords) : e.ps[(size_t) k];
                if (cnt > 0) synth.chord (ps, cnt, e.vel, lenS, SP.stabs, &SP, e.idx);
                break;
            }
            case ArpPattern::Bass: synth.bassNote (pitch, e.vel, lenS, &SP, e.idx); break;
            default: break;
        }
    }
    renderTo (n);
    if (! soundOn) buffer.clear();
    demo.process (buffer, license.isUnlocked());
    noteGate.applyMute (buffer); bypass.applyMute (buffer);
    playTick = P.offset + std::fmod (std::max (0.0, tick1), L);   // (LOOP SECTION: the idea position)
    if (! hostPlay && ! own) freeTick = tick1;
}

// ---------------------------------------------------------------- message thread: patterns
void ArpProcessor::publish (ArpPattern* p) { delete pending.exchange (p); }

// ---------------------------------------------------------------- SOUNDS PER LANE + YOUR SAMPLE
int ArpProcessor::laneChoice (int lane) const { return lane < 0 || lane > 3 ? 0 : (int) pLanePreset[(size_t) lane]->load(); }
void ArpProcessor::setLaneChoice (int lane, int choice)
{
    static const char* ids[] { "preset_chords", "preset_arp", "preset_melody", "preset_bass" };
    if (lane < 0 || linkAll)
    {   // the shared sound (a factory preset or YOUR SAMPLE) and every lane linked to it, as before
        if (choice >= 1) if (auto* q = apvts.getParameter ("preset")) q->setValueNotifyingHost (q->convertTo0to1 ((float) (choice - 1)));
        for (auto* id : ids) if (auto* q = apvts.getParameter (id)) q->setValueNotifyingHost (0.0f);
    }
    else if (auto* q = apvts.getParameter (ids[lane])) q->setValueNotifyingHost (q->convertTo0to1 ((float) juce::jlimit (0, arpsyn::kPresets + 1, choice)));
    if (lane == LaneBass || lane < 0) rebuildPattern();   // an 808 / ROUND SUB bass plays as mono chains (slides)
    ++version;
}
juce::StringArray ArpProcessor::recentSamples() const
{
    juce::StringArray a; a.addLines (presetDir().getChildFile ("recent_samples.txt").loadFileAsString()); a.removeEmptyStrings(); return a;
}
void ArpProcessor::addRecent (const juce::String& path)
{
    if (path.isEmpty()) return;
    auto a = recentSamples(); a.removeString (path); a.insert (0, path); while (a.size() > 8) a.remove (a.size() - 1);
    presetDir().getChildFile ("recent_samples.txt").replaceWithText (a.joinIntoString ("\n"));
}
bool ArpProcessor::loadSample (const juce::File& f, juce::String& msg)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
    if (rd == nullptr || rd->lengthInSamples <= 0) { msg = "Couldn't read that file (WAV, AIFF, FLAC, OGG or MP3)"; return false; }
    const juce::int64 cap = (juce::int64) (rd->sampleRate * 60.0);
    const bool capped = rd->lengthInSamples > cap;
    const int n = (int) juce::jmin (rd->lengthInSamples, cap), ch = (int) juce::jlimit (1u, 2u, rd->numChannels);
    juce::AudioBuffer<float> b ((int) juce::jmax (1u, rd->numChannels), n); rd->read (&b, 0, n, 0, true, true);
    std::vector<float> x[2];
    for (int c = 0; c < ch; ++c) x[c].assign (b.getReadPointer (c), b.getReadPointer (c) + n);
    if (rd->numChannels > 2) for (int c = 2; c < (int) rd->numChannels; ++c) for (int i = 0; i < n; ++i) x[c % 2][(size_t) i] += b.getSample (c, i);
    const bool ok = loadSampleAudio (x, ch, rd->sampleRate, f.getFileNameWithoutExtension(), f.getFullPathName(), msg);
    if (ok && capped) msg = "Long file: the first 60 s are used. " + msg;
    if (ok) addRecent (f.getFullPathName());
    return ok;
}
namespace
{
// FLAC (24 bit) of a float buffer; WAV 24 when FLAC can't take the rate
juce::MemoryBlock encodeSample (const std::vector<float> x[2], int chans, double rate)
{
    juce::MemoryBlock mb;
    {
        auto* os = new juce::MemoryOutputStream (mb, false);
        juce::FlacAudioFormat flac; juce::WavAudioFormat wav;
        const bool flacOk = flac.getPossibleSampleRates().contains ((int) std::lround (rate));
        std::unique_ptr<juce::AudioFormatWriter> w (flacOk ? flac.createWriterFor (os, rate, (unsigned) chans, 24, {}, 0) : wav.createWriterFor (os, rate, (unsigned) chans, 24, {}, 0));   // NOT-EXPORT: the user's own loaded sample, kept in memory for the project state
        if (w == nullptr) { delete os; return {}; }
        const float* ptr[2] = { x[0].data(), chans > 1 ? x[1].data() : x[0].data() };
        w->writeFromFloatArrays (ptr, chans, (int) x[0].size());
    }
    return mb;
}
bool decodeSample (const juce::MemoryBlock& mb, std::vector<float> x[2], int& chans, double& rate)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (std::make_unique<juce::MemoryInputStream> (mb, false)));
    if (rd == nullptr || rd->lengthInSamples <= 0) return false;
    chans = (int) juce::jlimit (1u, 2u, rd->numChannels); rate = rd->sampleRate; const int n = (int) rd->lengthInSamples;
    juce::AudioBuffer<float> b ((int) juce::jmax (1u, rd->numChannels), n); rd->read (&b, 0, n, 0, true, true);
    for (int c = 0; c < chans; ++c) x[c].assign (b.getReadPointer (c), b.getReadPointer (c) + n);
    return true;
}
}
bool ArpProcessor::loadSampleAudio (std::vector<float> x[2], int chans, double rate, const juce::String& name, const juce::String& path, juce::String& msg)
{
    chans = juce::jlimit (1, 2, chans);
    if (x[0].size() < 64) { msg = "That sample is too short"; return false; }
    float peak = 0; for (int c = 0; c < chans; ++c) for (float v : x[c]) peak = std::max (peak, std::abs (v));
    if (peak < 1.0e-5f) { msg = "That sample is silent"; return false; }
    for (int c = 0; c < chans; ++c) for (auto& v : x[c]) v *= 0.98f / peak;   // peak-normalised once, here
    // FLAC once; the in-memory sample is the decoded FLAC, so the project round trip is bit-exact
    auto flac = encodeSample (x, chans, rate);
    std::vector<float> y[2]; int ch2 = chans; double r2 = rate;
    if (flac.getSize() == 0 || ! decodeSample (flac, y, ch2, r2)) { msg = "Couldn't store that sample"; return false; }
    sampleFlac = flac;
    const bool ok = restoreSampleFlac (sampleFlac, name, path);
    if (ok)
    {
        msg = "YOUR SAMPLE: " + name + (sampleInfo.detectedHz > 0 ? ", root " + juce::MidiMessage::getMidiNoteName (sampleInfo.root, true, true, 4) + juce::String (sampleInfo.rootCents >= 0 ? " +" : " ") + juce::String (juce::roundToInt (sampleInfo.rootCents)) + " ct" : juce::String (", no clear pitch: root C4"));
        // the lanes that use the preview sound now play it (LINK ALL: the shared sound; otherwise the selected lane)
        if (laneChoice (0) != arpsyn::kPresets + 1 && laneChoice (1) != arpsyn::kPresets + 1 && laneChoice (2) != arpsyn::kPresets + 1 && laneChoice (3) != arpsyn::kPresets + 1 && (int) pPreset->load() != arpsyn::kPresets)
            setLaneChoice (linkAll ? -1 : juce::jlimit (0, 3, selLane), arpsyn::kPresets + 1);
    }
    return ok;
}
bool ArpProcessor::restoreSampleFlac (const juce::MemoryBlock& flac, const juce::String& name, const juce::String& path)
{
    std::vector<float> y[2]; int ch = 1; double r = 48000;
    if (! decodeSample (flac, y, ch, r)) return false;
    // build from exactly the decoded values (no second normalising: bit-exact to what the project stores)
    auto d = std::make_unique<arpsmp::SampleData>();
    d = arpsmp::build (y, ch, r, false);
    d->name = name; d->path = path; d->encoded = flac;
    SampleInfo si; si.len = d->length(); si.chans = d->chans; si.rate = d->rate; si.root = d->root; si.rootCents = d->rootCents; si.detectedHz = d->detectedHz;
    si.loopS = d->loopS; si.loopE = d->loopE; si.name = name; si.path = path;
    {   // the waveform the panel draws (1200 columns, peak of the channels)
        const int cols = 1200; si.peaks.assign ((size_t) cols, 0.0f);
        for (int i = 0; i < si.len; ++i) { const int k = (int) ((juce::int64) i * cols / si.len); float a = 0; for (int c = 0; c < d->chans; ++c) a = std::max (a, std::abs (d->lv[0][c][(size_t) i])); si.peaks[(size_t) k] = std::max (si.peaks[(size_t) k], a); }
    }
    sampleInfo = si; sampleFlac = flac;
    auto p = smpP; p.root = d->root; p.fine = 0; p.loopStart = d->loopS; p.loopEnd = d->loopE; p.start = 0; p.end = 1;
    sampleHand.post (std::move (d));
    setSampleParams (p);
    return true;
}
void ArpProcessor::clearSample()
{
    sampleHand.post (std::make_unique<arpsmp::SampleData>()); sampleInfo = {}; sampleFlac.reset();
    static const char* ids[] { "preset_chords", "preset_arp", "preset_melody", "preset_bass" };
    for (auto* id : ids) if (auto* q = apvts.getParameter (id)) if ((int) std::lround (q->convertFrom0to1 (q->getValue())) == arpsyn::kPresets + 1) q->setValueNotifyingHost (0.0f);
    if ((int) pPreset->load() == arpsyn::kPresets) if (auto* q = apvts.getParameter ("preset")) q->setValueNotifyingHost (q->convertTo0to1 ((float) arpsyn::Neon));
    ++version;
}
void ArpProcessor::auditionSampleRoot (bool on)
{
    if (on) forceSampleAudition = true;
    const int t = transposeNow(), l = juce::jlimit (0, 3, selLane); const int sh = t != 0 && ((transposeMask.load() >> l) & 1) ? t : 0;
    audition (smpP.root - sh, on);   // (audition adds the live transpose: the root you hear is the root)
    if (! on) forceSampleAudition = false;
}
void ArpProcessor::setTranspose (int s)
{
    if (auto* q = apvts.getParameter ("transpose")) q->setValueNotifyingHost (q->convertTo0to1 ((float) juce::jlimit (-24, 24, s)));
    ++version;
}
Idea ArpProcessor::exportIdea() const
{
    const juce::ScopedLock sl (ideaLock);
    Idea X = I; bakeTranspose (X, transposeNow(), transposeMask.load());
    return X;
}
int ArpProcessor::foldedCount() const
{
    const int t = transposeNow(), m = transposeMask.load(); if (t == 0) return 0;
    const juce::ScopedLock sl (ideaLock);
    int n = 0; for (int l : { LaneArp, LaneMelody, LaneBass }) if ((m >> l) & 1) for (auto& x : I.lanes[l].notes) if (foldsAt (x.p + t, l)) ++n;
    return n;
}
int ArpProcessor::transposeToKey (int root)
{
    const int s = keyShiftTo (I.H.key, root);
    setTranspose (s);
    return s;
}
void ArpProcessor::bakeTransposeNow()
{
    const int t = transposeNow(), m = transposeMask.load(); if (t == 0) return;
    bakeUndoTranspose = t;   // the BAKE undo step brings the offset back too
    pushUndo ("BAKE TRANSPOSE");
    bakeUndoTranspose = 0;
    {   // the Hook Lab tiles move with it (no refit needed)
        Idea X; { const juce::ScopedLock sl (ideaLock); X = I; } bakeTranspose (X, t, m);
        const auto pr = harmonyPrint (X.H), old = harmonyPrint (I.H);
        if (m & (1 << LaneMelody)) for (auto& h : lab.tiles) if (h.harm == old) { for (auto& n : h.notes) n.p = foldPitch (n.p + t, LaneMelody); h.harm = pr; }
    }
    change ([t, m] (Idea& X) { bakeTranspose (X, t, m); for (int l : { LaneArp, LaneMelody, LaneBass }) if ((m >> l) & 1) X.lanes[l].edited = X.lanes[l].edited || ! X.lanes[l].notes.empty(); }, false);
    setTranspose (0);
}
void ArpProcessor::setSection (int s, int e)
{
    { const juce::ScopedLock sl (ideaLock);
      auto& x = sections[(size_t) juce::jlimit (0, kMoods - 1, I.mode)];
      const int L = juce::jmax (BAR, I.total);
      s = juce::jlimit (0, L - PPQ / 4, juce::jmin (s, e)); e = juce::jlimit (s + PPQ / 4, L, juce::jmax (s, e));
      x.start = s; x.end = e; }
    rebuildPattern(); ++version;
}
void ArpProcessor::setSectionOn (bool on) { { const juce::ScopedLock sl (ideaLock); sections[(size_t) juce::jlimit (0, kMoods - 1, I.mode)].on = on; } rebuildPattern(); ++version; }
void ArpProcessor::clearSection() { { const juce::ScopedLock sl (ideaLock); sections[(size_t) juce::jlimit (0, kMoods - 1, I.mode)] = {}; } rebuildPattern(); ++version; }
bool ArpProcessor::sectionActive() const { const auto s = section(); return s.on && s.valid() && s.end <= juce::jmax (BAR, I.total) && ! melOverrideOn; }
void ArpProcessor::setPlayCursor (int t)
{
    playCursor = t < 0 || t >= juce::jmax (BAR, I.total) ? -1 : t;
    ++version;
}
void ArpProcessor::setSyncOwn (bool own)
{
    if (own && ! ownTempoSet) sync.setOwnBpm (I.g.bpm);   // OWN starts at the idea's tempo until you set one
    sync.setMode (own ? ghosty::SyncTransport::Mode::Own : ghosty::SyncTransport::Mode::Daw);
    if (! own) sync.stopOwn();
    internalPlay = false; labStartedPlay = false;
    ++version;
}
void ArpProcessor::setOwnBpm (double b) { sync.setOwnBpm (b); ownTempoSet = true; ++version; }
int ArpProcessor::playStartTick() const
{   // in pattern ticks: the cursor (inside the section when one loops), else the start
    const int c = playCursor.load();
    if (sectionActive()) { const auto s = section(); return c >= s.start && c < s.end ? c - s.start : 0; }
    return c >= 0 && c < juce::jmax (BAR, I.total) ? c : 0;
}
bool ArpProcessor::spacePlay()
{
    if (syncOwn())
    {   // SYNC: OWN: SPACE plays / stops only this plugin, from the cursor
        sync.toggleOwn ((double) playStartTick() / PPQ);
        return true;
    }
    if (hostPresent.load()) return false;   // SYNC: DAW with a DAW: SPACE is the DAW's
    if (hostRunning.load()) return false;   // the host owns the position
    labStartedPlay = false;
    if (internalPlay.load()) { internalPlay = false; return true; }
    startTickReq = (double) playStartTick();   // from the cursor (every time), else the beginning (LOOP SECTION: its start)
    internalPlay = true;
    return true;
}
void ArpProcessor::rebuildPattern()
{
    const juce::ScopedLock sl (ideaLock);
    labSyncHarmony();
    if (playCursor.load() >= juce::jmax (BAR, I.total)) playCursor = -1;   // the idea got shorter: the cursor can't point past the end
    auto* P = new ArpPattern();
    const int L0 = juce::jmax (BAR, I.total);
    const bool over = melOverrideOn && ! melOverride.empty();
    const int len = over && overrideLen > L0 ? overrideLen : L0;   // an audition longer than the idea: the lanes repeat under it
    P->len = len; P->bpm = juce::jlimit (40, 240, I.g.bpm); P->chordLoop = juce::jmax (BAR, I.H.loopTicks);
    for (auto& c : I.H.chords) { ArpPattern::Span s; s.t = c.t; s.tEnd = c.tEnd; s.root = c.root; for (int pc : c.pcs) s.mask = (uint16_t) (s.mask | (1u << pc)); P->spans.push_back (s); }
    chainBase = (chainBase + 1000003) % 1000000000;
    static const int laneCh[] { 3, 1, 2, 4 };
    const auto MX = midiExport();
    auto addCtl = [&] (const std::vector<Note>& notes, int lane, int loopLen) {   // glide bends (MELODY / BASS) + FATTEN CC11 swells for the live MIDI out
        for (auto& c : laneMidiCtl (notes, loopLen, MX.glideBend && lane != LaneArp, MX.bendRange))
        { ArpPattern::MEv e; e.tick = c.t; e.dur = 0; e.ch = (uint8_t) laneCh[lane]; e.kind = (uint8_t) (c.kind == 0 ? 1 : 2); e.pitch = (uint8_t) juce::jlimit (0, 127, c.num); e.val = c.val; e.lane = (uint8_t) lane; P->midi.push_back (e); } };
    const bool pianoT = over && lab.piano;   // PIANO TEST: does the hook still work as plain piano?
    P->pianoTest = pianoT;
    const bool energy = arpEvolving (I);
    auto addLead = [&] (std::vector<Note> notes, int lane, int loopLen) {
        const int spb = lane == LaneArp ? I.arpSpb : I.melSpb;
        notes = applyGroove (notes, I.groove, spb, I.seedUsed + (uint32_t) lane);
        if (lane == LaneMelody && pianoT) for (auto& x : notes) { x.v = 0.72f; x.l = Link {}; }
        sortNotes (notes);
        for (size_t i = 0; i < notes.size();)
        {
            size_t j = i; while (j + 1 < notes.size() && notes[j].l.type == 1 && notes[j + 1].t > notes[j].t) ++j;
            const int chain = chainBase + lane * 200000 + (int) i;
            const double end = notes[j].t + notes[j].d;
            ArpPattern::Ev s; s.tick = notes[i].t; s.type = ArpPattern::LeadStart; s.lane = (uint8_t) lane; s.pitch = (uint8_t) notes[i].p; s.vel = playVel (notes, i); s.dur = end - notes[i].t; s.chain = chain; s.idx = (int) i;
            if (energy && lane == LaneArp) s.energy = (float) arpEnergyAt (I, notes[i].t);
            if (i > 0 && notes[i - 1].l.type == 2) s.fat = notes[i - 1].l.f;
            P->ev.push_back (s);
            for (size_t k = i + 1; k <= j; ++k)
            {
                const double w = notes[k - 1].l.w;
                double gs = notes[k - 1].t + w * (notes[k].t - notes[k - 1].t);
                gs = juce::jlimit ((double) notes[k - 1].t + 1.0, (double) notes[k].t - 1.0, gs);
                ArpPattern::Ev g; g.tick = gs; g.type = ArpPattern::LeadGlide; g.lane = (uint8_t) lane; g.pitch = (uint8_t) notes[k].p; g.dur = notes[k].t - gs; g.chain = chain; P->ev.push_back (g);
                ArpPattern::Ev lg; lg.tick = notes[k].t; lg.type = ArpPattern::LeadLegato; lg.lane = (uint8_t) lane; lg.pitch = (uint8_t) notes[k].p; lg.vel = playVel (notes, k); lg.dur = end - notes[k].t; lg.chain = chain;
                lg.fat = notes[k - 1].l.type == 2 ? notes[k - 1].l.f : 0.0f; P->ev.push_back (lg);
            }
            i = j + 1;
        }
        for (auto& m : laneMidiNotes (notes, loopLen))
        { ArpPattern::MEv e; e.tick = m.t; e.dur = m.d; e.ch = (uint8_t) laneCh[lane]; e.pitch = (uint8_t) juce::jlimit (0, 127, m.p); e.vel = (uint8_t) juce::jlimit (1, 127, (int) std::lround (m.v * 127)); e.lane = (uint8_t) lane; P->midi.push_back (e); }
        addCtl (notes, lane, loopLen);
        if (energy && lane == LaneArp)   // GROW / WAVE / JOURNEY move the user's own synth too: CC74 brightness + CC11 expression on the ARP channel
            for (auto& c : arpEnergyCtl (I))
            { ArpPattern::MEv e; e.tick = c.t; e.dur = 0; e.ch = (uint8_t) laneCh[LaneArp]; e.kind = 2; e.pitch = (uint8_t) c.num; e.val = c.val; e.lane = LaneArp; P->midi.push_back (e); }
    };
    if (! I.lanes[LaneArp].mute && ! pianoT) addLead (I.lanes[LaneArp].notes, LaneArp, L0);
    if (! over && ! I.lanes[LaneMelody].mute) addLead (I.lanes[LaneMelody].notes, LaneMelody, L0);
    if (! I.lanes[LaneChords].mute)
    {
        const auto cn = chordLaneNotes (I); int chordIdx = 0;
        for (size_t i = 0; i < cn.size();)
        {
            size_t j = i; ArpPattern::Ev c; c.tick = cn[i].t; c.type = ArpPattern::Chord; c.lane = LaneChords; c.dur = cn[i].d; c.vel = (float) cn[i].v; c.idx = chordIdx++;
            while (j < cn.size() && cn[j].t == cn[i].t) { if (c.n < arpsyn::kMaxPitches) c.ps[c.n++] = (uint8_t) cn[j].p; ++j; }
            P->ev.push_back (c);
            i = j;
        }
        for (auto& m : cn) { ArpPattern::MEv e; e.tick = m.t; e.dur = m.d; e.ch = 3; e.pitch = (uint8_t) m.p; e.vel = (uint8_t) juce::jlimit (1, 127, (int) std::lround (m.v * 127)); e.lane = LaneChords; P->midi.push_back (e); }
    }
    const int bassSound = laneChoice (LaneBass) - 1;
    if (! I.lanes[LaneBass].mute && ! pianoT && (bassSound == arpsyn::Bass808 || bassSound == arpsyn::SubBass)) addLead (I.lanes[LaneBass].notes, LaneBass, L0);   // 808 / ROUND SUB: mono chains, slides
    else if (! I.lanes[LaneBass].mute && ! pianoT)
    {
        int bassIdx = 0;
        for (auto& n : I.lanes[LaneBass].notes)
        {
            ArpPattern::Ev b; b.tick = n.t; b.type = ArpPattern::Bass; b.lane = LaneBass; b.pitch = (uint8_t) n.p; b.vel = n.v; b.dur = n.d; b.idx = bassIdx++; P->ev.push_back (b);
        }
        for (auto& m : laneMidiNotes (I.lanes[LaneBass].notes, I.total))
        { ArpPattern::MEv e; e.tick = m.t; e.dur = m.d; e.ch = 4; e.pitch = (uint8_t) juce::jlimit (0, 127, m.p); e.vel = (uint8_t) juce::jlimit (1, 127, (int) std::lround (m.v * 127)); e.lane = LaneBass; P->midi.push_back (e); }
        addCtl (I.lanes[LaneBass].notes, LaneBass, L0);
    }
    if (len > L0)
    {   // the lanes repeat under a longer audition (own chains per pass so legato never joins two passes)
        const auto ev0 = P->ev; const auto mi0 = P->midi;
        for (int r = 1; r * L0 < len; ++r)
        {
            for (auto e : ev0) { e.tick += r * L0; if (e.chain >= 0) e.chain += r * 700000; P->ev.push_back (e); }
            for (auto e : mi0) { e.tick += r * L0; P->midi.push_back (e); }
        }
    }
    if (over) addLead (melOverride, LaneMelody, len);
    {   // LOOP SECTION: the pattern becomes [start, end) of the idea, from 0 (notes held into it start at 0, notes crossing the end stop there)
        auto& secRef = sections[(size_t) juce::jlimit (0, kMoods - 1, I.mode)];
        if (secRef.valid() && secRef.end > L0) secRef = {};   // the idea got shorter than the brace
        if (! over && sectionActive())
        {
            const int s = secRef.start, e = secRef.end, slen = e - s;
            std::vector<ArpPattern::Ev> ev; std::vector<ArpPattern::MEv> mi;
            for (auto x : P->ev)
            {
                const double end = x.tick + x.dur;
                if (x.tick >= s && x.tick < e) { x.tick -= s; x.dur = juce::jmin (x.dur, (double) e - (x.tick + s)); ev.push_back (x); }
                else if (x.tick < s && end > s + PPQ / 4 && (x.type == ArpPattern::LeadStart || x.type == ArpPattern::Chord || x.type == ArpPattern::Bass))
                { x.tick = 0; x.dur = juce::jmin (end, (double) e) - s; ev.push_back (x); }   // still sounding at the start: it plays from 0
            }
            std::map<int, ArpPattern::MEv> lastCc;   // the controllers' state at the section start (the ENERGY CC74 / CC11)
            for (auto x : P->midi)
            {
                if (x.kind != 0) { if (x.tick < s) { if (x.kind == 2) lastCc[x.ch * 128 + x.pitch] = x; } else if (x.tick < e) { x.tick -= s; mi.push_back (x); } continue; }
                const double end = x.tick + x.dur;
                if (x.tick >= s && x.tick < e) { x.tick -= s; x.dur = juce::jmin (x.dur, (double) e - (x.tick + s)); mi.push_back (x); }
                else if (x.tick < s && end > s + PPQ / 4) { x.tick = 0; x.dur = juce::jmin (end, (double) e) - s; mi.push_back (x); }
            }
            for (auto& kv : lastCc) { auto c = kv.second; c.tick = 0; mi.push_back (c); }
            P->ev = std::move (ev); P->midi = std::move (mi); P->len = slen; P->offset = s;
        }
    }
    std::stable_sort (P->ev.begin(), P->ev.end(), [] (const ArpPattern::Ev& a, const ArpPattern::Ev& b) { return a.tick < b.tick; });
    std::stable_sort (P->midi.begin(), P->midi.end(), [] (const ArpPattern::MEv& a, const ArpPattern::MEv& b) { return a.tick < b.tick; });
    publish (P);
}

void ArpProcessor::timerCallback()
{
    int s1, n1, s2, n2; retireFifo.prepareToRead (retireFifo.getNumReady(), s1, n1, s2, n2);
    for (int i = 0; i < n1; ++i) delete retired[(size_t) (s1 + i)];
    for (int i = 0; i < n2; ++i) delete retired[(size_t) (s2 + i)];
    retireFifo.finishedRead (n1 + n2);
    // LISTEN: enough audio captured -> analyse it in the background
    if (capturing.load() && capWritten.load() >= (int) (capSr * 12.0)) stopListening();
    // the harvester's weights: one write a second at most
    if (tasteDirty && juce::Time::getMillisecondCounterHiRes() - tasteDirtyAt > 1000.0) flushTaste();
    // HOOK LAB AUTO-PLAY: after one pass of the tile, the next one
    if (lab.autoPlay && melOverrideOn && lab.playing >= 0 && lab.playing < (int) lab.tiles.size())
    {
        const double pt = playTick.load(); const int ht = juce::jmax (BAR, lab.tiles[(size_t) lab.playing].ticks());
        if (pt >= 0)
        {
            if (labLastTick >= 0 && (pt < labLastTick || (int) (pt / ht) != (int) (labLastTick / ht))) ++labPasses;
            labLastTick = pt;
            if (labPasses >= 1 && ! lab.tiles.empty()) labAudition ((lab.playing + 1) % (int) lab.tiles.size());
        }
    }
}

// ---------------------------------------------------------------- idea changes
juce::String ArpProcessor::soundString() const
{
    if (auto xml = const_cast<juce::AudioProcessorValueTreeState&> (apvts).copyState().createXml()) return xml->toString (juce::XmlElement::TextFormat().singleLine());
    return {};
}
// states saved before TRAP FLUTE / 808 BASS / DUSTY KEYS / ROUND SUB were appended: YOUR SAMPLE (the last choice) moves up, every
// factory sound keeps its index
static juce::ValueTree upgradePresetChoices (juce::ValueTree params)
{
    const int n0 = params.hasProperty ("nPresets") ? (int) params.getProperty ("nPresets") : arpsyn::kPresetsBeforeUrban;
    if (n0 < arpsyn::kPresets)
    {
        const int add = arpsyn::kPresets - n0;
        for (auto c : params)
        {
            if (! c.hasType ("PARAM")) continue;
            const auto id = c.getProperty ("id").toString(); const double v = c.getProperty ("value");
            if (id == "preset" && v >= n0 - 0.5) c.setProperty ("value", v + add, nullptr);
            else if (id.startsWith ("preset_") && v >= n0 + 0.5) c.setProperty ("value", v + add, nullptr);
        }
    }
    params.setProperty ("nPresets", arpsyn::kPresets, nullptr);
    return params;
}
void ArpProcessor::restoreSound (const juce::String& xmlText)
{   // undo / presets / the modes' stored sounds / offline renders: never the BYPASS (it stays as the user has it)
    const bool byp = bypassParam != nullptr && bypassParam->get();
    auto* pm = apvts.getParameter ("playMode"); const float pmv = pm != nullptr ? pm->getValue() : 0.0f;
    auto* tp = apvts.getParameter ("transpose"); const float tpv = tp != nullptr ? tp->getValue() : 0.5f;
    if (auto xml = juce::parseXML (xmlText)) if (xml->hasTagName (apvts.state.getType())) apvts.replaceState (upgradePresetChoices (juce::ValueTree::fromXml (*xml)));
    if (bypassParam != nullptr && bypassParam->get() != byp) bypassParam->setValueNotifyingHost (byp ? 1.0f : 0.0f);
    if (pm != nullptr && std::abs (pm->getValue() - pmv) > 1.0e-6f) pm->setValueNotifyingHost (pmv);   // PLAY: per project, never from undo / presets / moods
    if (tp != nullptr && std::abs (tp->getValue() - tpv) > 1.0e-6f) tp->setValueNotifyingHost (tpv);   // TRANSPOSE too
}
juce::String ArpProcessor::sessionString() const
{
    auto* o = new juce::DynamicObject();
    { const juce::ScopedLock sl (ideaLock); o->setProperty ("idea", ideaToVar (I)); o->setProperty ("lab", labToVar (lab)); }
    juce::Array<juce::var> st; for (auto& m : modeStash) st.add (m); o->setProperty ("stash", st);
    o->setProperty ("sound", soundString());
    return juce::JSON::toString (juce::var (o), true);
}
void ArpProcessor::restoreSession (const juce::String& s)
{
    const auto v = juce::JSON::parse (s); if (! v.isObject()) return;
    {
        const juce::ScopedLock sl (ideaLock);
        ideaFromVar (v["idea"], I);
        if (v.hasProperty ("lab")) { labFromVar (v["lab"], lab); ++labJob; }   // the lab comes back with UNDO (a running job is dropped)
        melOverrideOn = false; overrideLen = 0;
        if (auto* a = v["stash"].getArray()) for (int k = 0; k < kMoods && k < a->size(); ++k) modeStash[(size_t) k] = (*a)[k].toString();
    }
    if (v["sound"].toString().isNotEmpty()) restoreSound (v["sound"].toString());
    rebuildPattern(); needFlush = true; ++version;
}
void ArpProcessor::pushUndo (const juce::String& label)
{
    undoStack.push_back ({ sessionString(), label, bakeUndoTranspose });
    if ((int) undoStack.size() > kUndoSteps) undoStack.erase (undoStack.begin());
    redoStack.clear();
}
void ArpProcessor::pushUndoPoint (const juce::String& label) { pushUndo (label); ++version; }

void ArpProcessor::change (const std::function<void (Idea&)>& fn, bool undoPoint, const juce::String& label)
{
    if (undoPoint) pushUndo (label);
    {
        const juce::ScopedLock sl (ideaLock);
        const int rw0 = I.chordRewrites;
        fn (I);
        if (I.mutant.active && I.mutant.harm != harmonyPrint (I.H)) mutantApply (I);   // MUTANT ARP: the chords changed under it -> the lane again (CHORD FOLLOW)
        if (I.chordRewrites != rw0) { chordRewriteKind = I.chordRewriteKind; ++chordRewriteSeq; }   // EDITED CHORDS were rebuilt: the editor says so
        melOverrideOn = false; lab.playing = -1;   // any change ends a HOOK LAB audition
    }
    rebuildPattern();
    ++version;
}

void ArpProcessor::audition (int pitch, bool on)
{
    { const int t = transposeNow(), l = juce::jlimit (0, kLanes - 1, selLane); if (t != 0 && ((transposeMask.load() >> l) & 1)) pitch = foldPitch (pitch + t, l); }   // you hear what will play
    int s1, n1, s2, n2; audFifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 > 0) audBuf[(size_t) s1] = { juce::jlimit (0, 127, pitch), on }; else if (n2 > 0) audBuf[(size_t) s2] = { juce::jlimit (0, 127, pitch), on };
    audFifo.finishedWrite (n1 + n2);
}

void ArpProcessor::stashBeforeReplace (int newMode)
{   // an idea of another mode replaces this one: keep this mode's idea for when the user comes back
    const juce::ScopedLock sl (ideaLock);
    newMode = juce::jlimit (0, kMoods - 1, newMode);
    if (newMode == I.mode) return;
    auto* o = new juce::DynamicObject(); o->setProperty ("idea", ideaToVar (I)); o->setProperty ("sound", soundString()); o->setProperty ("lab", labToVar (lab));
    modeStash[(size_t) I.mode] = juce::JSON::toString (juce::var (o), true);
    modeStash[(size_t) newMode] = {};
}

void ArpProcessor::switchMode (int mode)
{
    mode = juce::jlimit (0, kMoods - 1, mode);
    if (mode == I.mode) return;
    pushUndo ("mode " + juce::String (modeName (mode)));
    juce::String keep;
    {
        const juce::ScopedLock sl (ideaLock);
        auto* o = new juce::DynamicObject(); o->setProperty ("idea", ideaToVar (I)); o->setProperty ("sound", soundString()); o->setProperty ("lab", labToVar (lab));
        modeStash[(size_t) I.mode] = juce::JSON::toString (juce::var (o), true);
        keep = modeStash[(size_t) mode]; modeStash[(size_t) mode] = {};
    }
    if (keep.isEmpty()) { if (asyncUi) generateAsync (mode, true); else generate (mode, true, false); return; }   // first visit: a fresh GENERATE
    const auto v = juce::JSON::parse (keep);
    { const juce::ScopedLock sl (ideaLock); ideaFromVar (v["idea"], I); if (v.hasProperty ("lab")) { labFromVar (v["lab"], lab); ++labJob; } melOverrideOn = false; overrideLen = 0; }
    restoreSound (v["sound"].toString());
    rebuildPattern(); needFlush = true; ++version;
}

namespace
{
struct OfflineHead : juce::AudioPlayHead
{
    double bpm = 124, ppq = 0; bool playing = true; juce::int64 samples = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p; p.setBpm (bpm); p.setIsPlaying (playing); p.setPpqPosition (ppq); p.setTimeInSamples (samples); p.setTimeSignature (TimeSignature { 4, 4 });
        return p;
    }
};
} // namespace

ArpProcessor::RenderJob ArpProcessor::renderJob (double rate, double bpm) const
{
    RenderJob j; j.rate = rate; j.bpm = bpm;
    j.idea = exportIdea();   // as it sounds (TRANSPOSE baked in; the render's own processor runs at 0)
    if (sampleFlac.getSize() > 0) j.sampleFlac = sampleFlac;   // DRAG AUDIO renders with YOUR SAMPLE (an empty MemoryBlock copy trips UBSan inside JUCE)
    j.sampleParams = smpP;
    { const auto sc = section(); if (drawMode() == 1 && sc.valid()) { j.secStart = sc.start; j.secEnd = sc.end; } }
    j.sound = soundString();
    for (size_t k = 0; k < j.laneFx.size(); ++k) j.laneFx[k] = laneFx[k].load();
    return j;
}
juce::AudioBuffer<float> ArpProcessor::renderOffline (const RenderJob& job, int laneMask, std::atomic<float>* progress)
{
    auto r = std::make_unique<ArpProcessor>();
    r->restoreSound (job.sound);
    if (job.sampleFlac.getSize() > 0 && r->restoreSampleFlac (job.sampleFlac, {}, {})) r->setSampleParams (job.sampleParams);
    for (size_t k = 0; k < job.laneFx.size(); ++k) r->laneFx[k] = job.laneFx[k];
    const bool sec = job.secStart >= 0 && job.secEnd > job.secStart;
    r->change ([&] (Idea& T) { T = job.idea; if (laneMask >= 0) for (int l = 0; l < kLanes; ++l) T.lanes[l].mute = T.lanes[l].mute || (laneMask & (1 << l)) == 0; }, false);
    if (laneMask >= 0) if (auto* q = r->apvts.getParameter ("kick")) q->setValueNotifyingHost ((laneMask & 16) != 0 && r->pKick->load() > 0.5f ? 1.0f : 0.0f);
    if (auto* q = r->apvts.getParameter ("sound")) q->setValueNotifyingHost (1.0f);
    r->internalPlay = false;
    OfflineHead head; head.bpm = job.bpm; r->setPlayHead (&head);
    const double rate = job.rate; const int block = 512; r->setRateAndBufferSizeDetails (rate, block); r->prepareToPlay (rate, block);
    if (sec) { r->sections[(size_t) juce::jlimit (0, kMoods - 1, job.idea.mode)] = { job.secStart, job.secEnd, true }; r->rebuildPattern(); }   // DRAG SECTION: the brace, from 0
    const double loopSamples = (double) (sec ? job.secEnd - job.secStart : juce::jmax (BAR, job.idea.total)) / PPQ * 60.0 / job.bpm * rate;
    const int total = (int) std::ceil (loopSamples + job.tail * rate);
    juce::AudioBuffer<float> out (2, total); out.clear();
    juce::AudioBuffer<float> b (2, block); juce::MidiBuffer m;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = juce::jmin (block, total - pos);
        head.playing = pos < loopSamples; head.ppq = pos / rate * job.bpm / 60.0; head.samples = pos;
        b.setSize (2, n, false, false, true); b.clear(); m.clear();
        r->processBlock (b, m);
        for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, b, c, 0, n);
        if (progress != nullptr) progress->store ((float) pos / (float) total);
    }
    r->setPlayHead (nullptr);
    return out;
}
bool ArpProcessor::writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double rate)
{
    f.getParentDirectory().createDirectory();
    std::unique_ptr<juce::FileOutputStream> os (f.createOutputStream());
    if (os == nullptr) return false;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), rate, (unsigned) b.getNumChannels(), 24, {}, 0));   // GATE: via-caller (the audio renders + the sample's WAV grip ask first)
    if (w == nullptr) return false;
    os.release();
    return w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

juce::String ArpProcessor::undo()
{
    if (undoStack.empty()) return {};
    const auto s = undoStack.back(); undoStack.pop_back();
    redoStack.push_back ({ sessionString(), s.label, s.bakeT != 0 ? -1 : 0 });   // (redo of a BAKE: the offset goes back to 0)
    restoreSession (s.state);
    if (s.bakeT != 0) setTranspose (s.bakeT);
    return s.label;
}
juce::String ArpProcessor::redo()
{
    if (redoStack.empty()) return {};
    const auto s = redoStack.back(); redoStack.pop_back();
    undoStack.push_back ({ sessionString(), s.label, s.bakeT != 0 ? transposeNow() : 0 });
    restoreSession (s.state);
    if (s.bakeT != 0) setTranspose (0);
    return s.label;
}

static void setParam (juce::AudioProcessorValueTreeState& s, const char* id, float plain)
{
    if (auto* p = s.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (plain));
}

void ArpProcessor::generate (int mode, bool newSeed, bool undoPoint)
{
    const bool modeChanged = mode != I.mode;
    if (undoPoint) pushUndo ("GENERATE");
    const auto recent = recentIdeas[(size_t) juce::jlimit (0, kMoods - 1, mode)];
    const bool further = newSeed && usesPushFurther (mode);
    change ([&] (Idea& X) {
        if (modeChanged) { X.mode = juce::jlimit (0, kMoods - 1, mode); applyModeDefaults (X); }
        if (newSeed) X.seed = (uint32_t) juce::Random::getSystemRandom().nextInt ({ 1, 999999 });
        if (further) newIdeaFurther (X, recent); else newIdea (X);   // PUSH FURTHER: away from the last ideas (X.seed = the seed that made it)
        if (X.mutant.active)
        {   // MUTANT ARP stays on: a new cell over the new chords (locked steps kept), seeded by the idea
            if (newSeed) X.mutant.P.seed = X.seed;
            X.mutant.cell = arp::mutantGenerate (X.H, X.mutant.P, &X.mutant.cell); X.mutant.generation = 0;
            if (X.mutant.P.twoLine == TwoInterlock) X.mutant.cellB = mutantSecondCell (X.mutant.cell, X.H, X.mutant.P);
            mutantApply (X);
        }
    }, false);
    if (newSeed) { const juce::ScopedLock sl (ideaLock); rememberIdea (I); }
    if (modeChanged) applyMoodSound (mode);   // the preview sound follows the mode
    else if (urbanMood (mode)) applyFlavourLanes (false);   // (a new flavour: its lane sounds, where the user left the mood's)
    if (newSeed || modeChanged)
    {
        genHist.resize ((size_t) (genIdx + 1)); genHist.push_back ({ juce::String (I.words), I.seed });
        if (genHist.size() > 60) genHist.erase (genHist.begin());
        genIdx = (int) genHist.size() - 1;
    }
}

// GENERATE without freezing the window: the new idea is composed on a worker (on a copy), applied on the message thread
// (one undo step); a newer request replaces a pending one, and a result is dropped when another one was asked for meanwhile
void ArpProcessor::runIdeaJob (const juce::String& label, std::function<void (Idea&)> work, std::function<void()> after)
{
    const int job = ++ideaJob; ideaBusy = true; ++version;
    Idea X; { const juce::ScopedLock sl (ideaLock); X = I; }
    auto flag = alive;
    static_cast<LabWorker*> (ideaWorker.get())->submit ([this, flag, X, work, after, label, job] () mutable {
        work (X);
        auto res = std::make_shared<Idea> (std::move (X));
        juce::MessageManager::callAsync ([this, flag, res, after, label, job] {
            if (! flag->load() || job != ideaJob) return;   // a newer GENERATE replaced it
            ideaBusy = false;
            if (label.isNotEmpty()) pushUndo (label);
            change ([res] (Idea& Y) { Y = *res; }, false);
            if (after) after();
        });
    });
}
void ArpProcessor::generateAsync (int mode, bool newSeed)
{
    mode = juce::jlimit (0, kMoods - 1, mode);
    const bool modeChanged = mode != I.mode;
    const uint32_t seed = newSeed ? (uint32_t) juce::Random::getSystemRandom().nextInt ({ 1, 999999 }) : I.seed;
    const auto recent = recentIdeas[(size_t) mode];
    const bool further = newSeed && usesPushFurther (mode);
    runIdeaJob ("GENERATE", [mode, modeChanged, newSeed, seed, recent, further] (Idea& X) {
        if (modeChanged) { X.mode = mode; applyModeDefaults (X); }
        if (newSeed) X.seed = seed;
        if (further) newIdeaFurther (X, recent); else newIdea (X);
    }, [this, mode, modeChanged, newSeed] {
        if (newSeed) { const juce::ScopedLock sl (ideaLock); rememberIdea (I); }
        if (modeChanged) applyMoodSound (mode); else if (urbanMood (mode)) applyFlavourLanes (false);
        if (newSeed || modeChanged)
        {
            genHist.resize ((size_t) (genIdx + 1)); genHist.push_back ({ juce::String (I.words), I.seed });
            if (genHist.size() > 60) genHist.erase (genHist.begin());
            genIdx = (int) genHist.size() - 1;
        }
    });
}
void ArpProcessor::stepHistoryAsync (int dir)
{
    const int ni = genIdx + dir;
    if (ni < 0) return;
    if (ni >= (int) genHist.size()) { generateAsync (I.mode, true); return; }
    genIdx = ni;
    const auto h = genHist[(size_t) ni];
    runIdeaJob ("seed", [h] (Idea& X) { X.words = h.first.toStdString(); X.seed = h.second; newIdea (X); }, {});
}
void ArpProcessor::applyMoodSound (int mode)
{   // each mood's own preview sound: SUNRISE a euphoric saw stack (+ piano attack), DUSK a warm pluck, MIDNIGHT a hypnotic saw under a slow filter LFO,
    // TRAP a breathy flute over an 808 that slides, HIP HOP dusty keys over a round sub
    mode = juce::jlimit (0, kMoods - 1, mode);
    const auto& M = recipe().mode[mode];
    static const int pre[] { arpsyn::Euphoric, arpsyn::DuskPluck, arpsyn::HypnoSaw, arpsyn::TrapFlute, arpsyn::DustyKeys };
    setParam (apvts, "preset", (float) pre[mode]); setParam (apvts, "stabs", M.stabs ? 1.0f : 0.0f);
    for (auto* id : { "preset_chords", "preset_arp", "preset_melody", "preset_bass" }) setParam (apvts, id, 0.0f);   // the mood sets every lane (linked to its sound)
    setParam (apvts, "layerPluck", 0.0f); setParam (apvts, "layerOct", 0.0f);
    if (urbanMood (mode))
    {
        if (linkAll) { linkAll = false; linkOffByMood = true; }
        applyFlavourLanes (true);   // the lanes' own sounds (TRAP: bells on the arp, the 808; HIP HOP: organ / pad keys on the chords, sub / 808)
        setParam (apvts, "lfoDepth", 0.0f); setParam (apvts, "lfoRate", 3.0f); applyDarknessSound();
        setParam (apvts, "pump", mode == Trap ? 0.12f : 0.1f);
        setParam (apvts, "delay", mode == Trap ? 0.3f : 0.2f); setParam (apvts, "reverb", mode == Trap ? 0.42f : 0.26f);   // the flute: a little air
        rebuildPattern();   // (the bass lane plays as mono 808 / sub chains)
        return;
    }
    if (linkOffByMood) { linkAll = true; linkOffByMood = false; }
    setParam (apvts, "lfoDepth", mode == Midnight ? 0.42f : mode == Dusk ? 0.1f : 0.06f); setParam (apvts, "lfoRate", mode == Midnight ? 3.0f : 2.0f);
    setParam (apvts, "cutoff", mode == Midnight ? 0.55f : 0.6f);
    setParam (apvts, "pump", mode == Sunrise ? 0.35f : 0.22f);
    setParam (apvts, "delay", mode == Dusk ? 0.38f : 0.32f); setParam (apvts, "reverb", mode == Sunrise ? 0.34f : 0.28f);
}
void ArpProcessor::applyDarknessSound()
{   // DARKNESS darkens the preview sound too: the cutoff (TRAP 0.6 / HIP HOP 0.5 at the default)
    if (! urbanMood (I.mode)) return;
    const float d = (float) darknessOf (I, I.mode) / 100.0f;
    setParam (apvts, "cutoff", I.mode == Trap ? 0.74f - 0.2f * d : 0.58f - 0.2f * d);
}
void ArpProcessor::applyFlavourLanes (bool force)
{
    const int md = I.mode; if (! urbanMood (md)) return;
    const int fl = I.flavourUsed;
    int chords = 0, arpL = 0, bass = 0, mel = 0;
    if (md == Trap && isEmo (I)) { chords = 1 + arpsyn::SadPad; arpL = 1 + arpsyn::GuitarPluck; bass = 1 + arpsyn::Bass808; mel = emoLead (I) == 1 ? 1 + arpsyn::GuitarPluck : 0; }   // EMO: the guitar bed, the sad pad, a soft lead or the guitar
    else if (md == Trap) { arpL = 1 + arpsyn::Bell; bass = 1 + arpsyn::Bass808; }
    else
    {   // BOOM BAP / WEST GLIDE: the organ; MEMPHIS / UNDERWATER / JAZZ SOUL: warm pad keys. MEMPHIS + UNDERWATER (the modern ones): the 808
        chords = 1 + (fl == 0 || fl == 2 ? arpsyn::SoulOrgan : arpsyn::WarmPadKeys);
        bass = 1 + (fl == 1 || fl == 3 ? arpsyn::Bass808 : arpsyn::SubBass);
    }
    auto isDefault = [] (int v) { return v == 0 || v == 1 + arpsyn::Bell || v == 1 + arpsyn::SoulOrgan || v == 1 + arpsyn::WarmPadKeys || v == 1 + arpsyn::SubBass || v == 1 + arpsyn::Bass808
                                         || v == 1 + arpsyn::GuitarPluck || v == 1 + arpsyn::SadPad; };
    const std::pair<const char*, int> want[] { { "preset_chords", chords }, { "preset_arp", arpL }, { "preset_bass", bass }, { "preset_melody", md == Trap ? mel : -1 } };
    for (auto& w : want)
        if (w.second >= 0)
        if (auto* q = apvts.getParameter (w.first))
            if (force || isDefault ((int) std::lround (q->convertFrom0to1 (q->getValue())))) q->setValueNotifyingHost (q->convertTo0to1 ((float) w.second));
    rebuildPattern();
}
void ArpProcessor::recallSeed (uint32_t seed) { change ([&] (Idea& X) { X.seed = seed; newIdea (X); }); }

void ArpProcessor::stepHistory (int dir)
{
    const int ni = genIdx + dir;
    if (ni < 0) return;
    if (ni >= (int) genHist.size()) { generate (I.mode, true); return; }
    genIdx = ni;
    const auto h = genHist[(size_t) ni];
    change ([&] (Idea& X) { X.words = h.first.toStdString(); X.seed = h.second; newIdea (X); });
}

void ArpProcessor::laneTool (int lane, const juce::String& tool, const std::vector<bool>* sel)
{
    const uint32_t seed = (uint32_t) juce::Random::getSystemRandom().nextInt ({ 1, 1 << 30 });
    change ([&] (Idea& X) {
        if (lane == LaneChords)
        {
            const bool simpleLoop = X.simple && ! X.simpleProg.empty();
            if (tool == "newChords") { X.g.prog = newProg (X.g, seed); X.g.progSeed = seed; pickSimpleProg (X, seed); }
            else if ((tool == "varyLittle" || tool == "varyMore") && simpleLoop) pickSimpleProg (X, seed, true);   // SIMPLE: another classic loop
            else if (tool == "varyLittle" || tool == "varyMore")
            {
                const int k = tool == "varyMore" ? 2 : 1; auto& pr = chordProg (X);
                for (int i = 0; i < k && ! pr.empty(); ++i) { auto& s = pr[(size_t) ((seed >> (i * 5)) % pr.size())]; s.alt = (s.alt + 1 + (int) ((seed >> 11) % 3)) % (int) altNames().size(); }
            }
            else if (X.simple && tool == "simpler") X.chordRhythm = juce::jmax (0, X.chordRhythm - 1);   // SIMPLE: rhythm only (the chords stay plain triads)
            else if (X.simple && tool == "busier") X.chordRhythm = juce::jmin (2, X.chordRhythm + 1);
            else if (tool == "simpler") X.g.colour = juce::jmax (0.0, X.g.colour - 0.25);
            else if (tool == "busier") X.g.colour = juce::jmin (1.0, X.g.colour + 0.25);
            rebuildChords (X); refitLanesToChords (X);
            return;
        }
        auto& L = X.lanes[lane];
        const bool hasSel = sel != nullptr && std::find (sel->begin(), sel->end(), true) != sel->end();
        EditCtx c = editCtx (X, lane); c.sel = hasSel ? sel : nullptr;
        if (tool == "new")
        {   // NEW: the lane's next take (a deterministic sequence: laneVar 1, 2, 3 ...; saved with the idea). The seed changes BEFORE composing,
            // and a take that comes out the same as the one shown is skipped, so every press gives a different take.
            const auto before = L.notes;
            if (lane == LaneMelody) X.melVer = newIdeaMelVer();   // (a NEW melody take is a new generation: the current TRAP writer)
            auto same = [&before] (const std::vector<Note>& v) { if (v.size() != before.size()) return false;
                for (size_t i = 0; i < v.size(); ++i) if (v[i].t != before[i].t || v[i].p != before[i].p || v[i].d != before[i].d) return false; return true; };
            const int v0 = X.laneVar[(size_t) lane]; bool moved = false;
            for (int k = 0; k < 12 && ! moved; ++k) { X.laneVar[(size_t) lane] = X.laneVar[(size_t) lane] % 9999 + 1; composeLane (X, lane, true); moved = ! same (X.lanes[lane].notes); }
            if (! moved) { X.laneVar[(size_t) lane] = v0 % 9999 + 1; composeLane (X, lane, true); }   // nothing can change (e.g. a TRAP arp with no melody rests to answer in): one step on
            return;
        }
        const bool urbanArp = urbanMood (X.laneMood[(size_t) LaneArp] >= 0 ? X.laneMood[(size_t) LaneArp] : X.mode);   // TRAP / HIP HOP arps are notes, not figures
        if (lane == LaneArp && ! L.edited && ! hasSel && ! urbanArp)
        {   // the arp is still its FIGURE: tweak the figure, so the change stays hypnotic and consistent everywhere
            const ArpParams P = arpParams (X); Figure f = X.fig;
            if (tool == "varyLittle") f = figureVary (f, P, seed, 1, X.H, X.total, X.pedalPc);
            else if (tool == "varyMore") f = figureVary (f, P, seed, 2, X.H, X.total, X.pedalPc);
            else if (tool == "newNotes") f = figureNewNotes (f, P, seed, X.H, X.total, X.pedalPc);
            else if (tool == "newRhythm") f = figureNewRhythm (f, P, seed, X.H, X.total, X.pedalPc);
            else if (tool == "simpler") f = figureSimpler (f);
            else if (tool == "busier") f = figureBusier (f, P);
            else if (tool == "octUp" || tool == "octDown") { X.reg = juce::jlimit (-2, 2, X.reg + (tool == "octUp" ? 1 : -1)); }
            X.fig = f;
            L.notes = mergeLocked (realizeArp (X.fig, X.H, arpParams (X), X.total, X.pedalPc, &X.figEvents), L.notes, c);
            return;
        }
        std::vector<Note> out = L.notes;
        if (tool == "varyLittle") out = toolVary (L.notes, c, seed, 1);
        else if (tool == "varyMore") out = toolVary (L.notes, c, seed, 2);
        else if (tool == "newNotes") out = toolNewNotes (L.notes, c, seed);
        else if (tool == "newRhythm") out = toolNewRhythm (L.notes, c, seed);
        else if (tool == "simpler") out = toolSimpler (L.notes, c);
        else if (tool == "busier") out = toolBusier (L.notes, c, seed);
        else if (tool == "octUp") out = toolOctave (L.notes, c, 1);
        else if (tool == "octDown") out = toolOctave (L.notes, c, -1);
        sortNotes (out); L.notes = out; L.edited = true;
    }, true, tool);
}

void ArpProcessor::harvest (const std::vector<bool>& sel, int barFrom, int barTo)
{
    change ([&] (Idea& X) {
        const auto& src = X.lanes[LaneArp].notes;
        int t0 = barFrom * BAR, t1 = (barTo + 1) * BAR;
        const bool hasSel = std::find (sel.begin(), sel.end(), true) != sel.end();
        if (hasSel)
        {
            t0 = 1 << 30; t1 = 0;
            for (size_t i = 0; i < src.size() && i < sel.size(); ++i) if (sel[i]) { t0 = juce::jmin (t0, src[i].t); t1 = juce::jmax (t1, src[i].t + 1); }
        }
        auto& M = X.lanes[LaneMelody];
        std::vector<Note> out;
        for (auto& n : M.notes) if (n.t < t0 || n.t >= t1 || n.lock) out.push_back (n);
        for (size_t i = 0; i < src.size(); ++i)
            if (hasSel ? (i < sel.size() && sel[i]) : (src[i].t >= t0 && src[i].t < t1)) { Note n = src[i]; n.lock = false; out.push_back (n); }
        sortNotes (out); M.notes = out; M.edited = true; M.mute = false;
    }, true, "HARVEST");
}

// ---------------------------------------------------------------- HOOK LAB
void ArpProcessor::ensureTaste()
{
    if (tasteLoaded) return;
    tasteLoaded = true;
    if (! persistTaste) return;
    const auto f = presetDir().getChildFile ("hooklab_taste.txt");
    if (f.existsAsFile()) taste = weightsFromString (f.loadFileAsString().toStdString());
}
void ArpProcessor::learnTaste (const HFeat& f, bool kept)
{
    ensureTaste();
    learnKeep (taste, f, kept);
    tastePending.push_back ({ f, kept });
    saveTaste();
}
void ArpProcessor::saveTaste() { tasteDirty = true; tasteDirtyAt = juce::Time::getMillisecondCounterHiRes(); }
void ArpProcessor::flushTaste()
{
    tasteDirty = false;
    if (! persistTaste) { tastePending.clear(); return; }
    // read-modify-write: another instance may have learned meanwhile; its keeps + ours
    const auto f = presetDir().getChildFile ("hooklab_taste.txt");
    HarvestWeights W = taste;
    if (f.existsAsFile()) { W = weightsFromString (f.loadFileAsString().toStdString()); for (auto& op : tastePending) learnKeep (W, op.first, op.second); }
    tastePending.clear();
    f.replaceWithText (juce::String (weightsToString (W)));
    taste = W;
}
void ArpProcessor::labSyncHarmony()
{
    const juce::ScopedLock sl (ideaLock);
    const auto pr = harmonyPrint (I.H);
    bool changed = false; std::vector<Hook> keep;
    for (auto& h : lab.tiles)
    {
        if (h.harm == pr) { keep.push_back (h); continue; }
        changed = true;
        if (h.star) { Hook k = h; refitHook (I, k, lab.reg); keep.push_back (k); }   // a keeper follows the chords; the rest belonged to the old ones
    }
    if (! changed) return;
    lab.tiles = keep; lab.chordsChanged = true; lab.harvest.clear();
    lab.selected = lab.tiles.empty() ? -1 : juce::jlimit (0, (int) lab.tiles.size() - 1, lab.selected);
    lab.playing = -1; melOverrideOn = false; overrideLen = 0;
    lab.history.clear(); lab.histIdx = -1;
}
void ArpProcessor::labPushHistory()
{
    if (lab.histIdx >= 0 && lab.histIdx < (int) lab.history.size()) lab.history[(size_t) lab.histIdx] = lab.tiles;   // stars made since
    lab.history.resize ((size_t) (lab.histIdx + 1));
}
void ArpProcessor::labStepHistory (int dir)
{
    const int ni = lab.histIdx + dir;
    if (ni < 0 || ni >= (int) lab.history.size()) return;
    lab.history[(size_t) lab.histIdx] = lab.tiles;
    lab.histIdx = ni; lab.tiles = lab.history[(size_t) ni]; lab.selected = lab.tiles.empty() ? -1 : 0;
    labAudition (-1);
    ++version;
}
HookParams ArpProcessor::labParams (const Idea& X) const
{
    HookParams hp = hookParams (X, lab.bars, lab.seed);
    hp.style = lab.style; hp.candy = lab.candy; hp.tension = lab.tension; hp.reg = lab.reg;
    hp.meter = lab.meter; hp.euclidHits = lab.euclidHits; hp.euclidRot = lab.euclidRot; hp.feel = lab.feel;
    return hp;
}
std::vector<Hook> ArpProcessor::labRun (const Idea& X, int kind, std::vector<Hook>& keep, const HarvestWeights& W, const std::atomic<bool>* cancel, HookParams hp)
{   // the work itself (any thread): everything it reads is a copy the caller owns
    hp.taste = &W; hp.cancel = cancel; hp.progress = &labProgress;
    if (kind == 2 && ! keep.empty())
    {
        hp.bars = keep[0].bars;
        return findMagic (X, keep[0], hp, juce::jmax (0, 16 - (int) keep.size()));
    }
    hp.count = juce::jmax (0, 16 - (int) keep.size());
    return generateHooks (X, hp);
}
void ArpProcessor::labDeliver (std::vector<Hook> keep, std::vector<Hook> fresh, int kind, int job, const std::string& print)
{
    if (job != labJob) return;   // a newer job (or a cancel) replaced this one
    lab.busy = false; lab.busyKind = 0;
    if (print != harmonyPrint (I.H)) { ++version; return; }   // the chords changed while it worked
    labPushHistory();
    keep.insert (keep.end(), fresh.begin(), fresh.end());
    lab.tiles = keep; lab.selected = lab.tiles.empty() ? -1 : 0; lab.chordsChanged = false;
    lab.history.push_back (lab.tiles); lab.histIdx = (int) lab.history.size() - 1;
    if (lab.history.size() > 12) { lab.history.erase (lab.history.begin()); --lab.histIdx; }
    (void) kind;
    labAudition (-1);
    ++version;
}
void ArpProcessor::labStart (int kind, bool reseed)
{
    ensureTaste();
    if (reseed) lab.seed = lab.seed * 1103515245u + 12345u + (uint32_t) version.load();
    std::vector<Hook> keep;
    if (kind == 2)
    {
        if (lab.selected < 0 || lab.selected >= (int) lab.tiles.size()) return;
        keep.push_back (lab.tiles[(size_t) lab.selected]);
        for (size_t i = 0; i < lab.tiles.size(); ++i) if (lab.tiles[i].star && (int) i != lab.selected) keep.push_back (lab.tiles[i]);
    }
    else for (auto& h : lab.tiles) if (h.star) keep.push_back (h);
    if (labJobCancel) labJobCancel->store (true);
    auto cancel = std::make_shared<std::atomic<bool>> (false); labJobCancel = cancel;
    const int job = ++labJob;
    Idea X; { const juce::ScopedLock sl (ideaLock); X = I; }
    const auto print = harmonyPrint (X.H);
    lab.busy = true; lab.busyKind = kind; labProgress = 0; ++version;
    auto flag = alive; const HarvestWeights W = taste; const HookParams hp = labParams (X);
    static_cast<LabWorker*> (labWorker.get())->submit ([this, flag, X, keep, W, kind, job, print, cancel, hp] () mutable {
        auto fresh = labRun (X, kind, keep, W, cancel.get(), hp);
        if (cancel->load()) return;
        juce::MessageManager::callAsync ([this, flag, keep, fresh, kind, job, print] () mutable {
            if (! flag->load()) return;
            labDeliver (std::move (keep), std::move (fresh), kind, job, print); });
    });
}
void ArpProcessor::labGenerateAsync (int bars) { lab.bars = bars >= 8 ? 8 : 4; labStart (1, true); }
void ArpProcessor::labMagicAsync() { labStart (2, false); }
void ArpProcessor::labCancel()
{
    if (labJobCancel) labJobCancel->store (true);
    ++labJob; lab.busy = false; lab.busyKind = 0; ++version;
}
void ArpProcessor::labGenerate (int bars)
{
    ensureTaste();
    lab.bars = bars >= 8 ? 8 : 4; lab.seed = lab.seed * 1103515245u + 12345u + (uint32_t) version.load();
    std::vector<Hook> keep; for (auto& h : lab.tiles) if (h.star) keep.push_back (h);
    const auto fresh = labRun (I, 1, keep, taste, nullptr, labParams (I));
    labDeliver (keep, fresh, 1, ++labJob, harmonyPrint (I.H));
}
void ArpProcessor::labMagic()
{
    if (lab.selected < 0 || lab.selected >= (int) lab.tiles.size()) return;
    ensureTaste();
    std::vector<Hook> keep { lab.tiles[(size_t) lab.selected] };
    for (size_t i = 0; i < lab.tiles.size(); ++i) if (lab.tiles[i].star && (int) i != lab.selected) keep.push_back (lab.tiles[i]);
    lab.seed ^= (uint32_t) version.load() * 2654435761u;
    const auto fresh = labRun (I, 2, keep, taste, nullptr, labParams (I));
    labDeliver (keep, fresh, 2, ++labJob, harmonyPrint (I.H));
}
void ArpProcessor::labSetParams (int style, int candy, double tension, int reg)
{
    const bool same = style == lab.style && candy == lab.candy && std::abs (tension - lab.tension) < 1e-9 && reg == lab.reg;
    lab.style = juce::jlimit (0, kStyles - 1, style); lab.candy = juce::jlimit (0, kCandies - 1, candy); lab.tension = juce::jlimit (-1.0, 1.0, tension); lab.reg = juce::jlimit (0, kRegisters - 1, reg);
    if (! same && ! lab.tiles.empty()) labStart (1, false);   // the same seed: compare settings, not luck
    ++version;
}
void ArpProcessor::labSetRhythm (int meter, int hits, int rot, int feel)
{
    meter = juce::jlimit (0, kMeters - 1, meter); const int steps = meterSteps (meter);
    hits = juce::jlimit (0, steps, hits); rot = hits > 0 ? ((rot % steps) + steps) % steps : 0; feel = juce::jlimit (0, kFeels - 1, feel);
    const bool same = meter == lab.meter && hits == lab.euclidHits && rot == lab.euclidRot && feel == lab.feel;
    lab.meter = meter; lab.euclidHits = hits; lab.euclidRot = rot; lab.feel = feel;
    if (! same && ! lab.tiles.empty()) labStart (1, false);
    ++version;
}
void ArpProcessor::labPianoTest (bool on)
{
    { const juce::ScopedLock sl (ideaLock); lab.piano = on; }
    needFlush = true;   // voices already sounding don't switch patch mid-note
    rebuildPattern();
    ++version;
}
void ArpProcessor::labAudition (int tile)
{
    bool start = false;
    {
        const juce::ScopedLock sl (ideaLock);
        labSyncHarmony();
        if (tile >= 0 && tile < (int) lab.tiles.size())
        {
            const auto& h = lab.tiles[(size_t) tile];
            const int ht = h.ticks(), L0 = juce::jmax (BAR, I.total);
            overrideLen = (int) juce::jmin ((juce::int64) 64 * BAR, (juce::int64) L0 / std::gcd (L0, ht) * ht);   // the whole hook, over every lane, then it loops
            melOverride = loopHook (h.notes, ht, overrideLen, I.H); melOverrideOn = true; lab.playing = tile; lab.selected = tile;
            labPasses = 0; labLastTick = -1; start = true;
        }
        else { melOverrideOn = false; overrideLen = 0; lab.playing = -1; }
    }
    rebuildPattern();
    if (start && syncOwn()) { if (! sync.isOwnPlaying()) labStartedPlay = true; sync.startOwn (0.0); }   // OWN: from its bar 1 on the plugin's clock
    else if (start && ! hostRunning.load())
    {   // hear it from its bar 1
        if (! internalPlay.load()) { internalPlay = true; labStartedPlay = true; }
        restartFree = true;
    }
    else if (! start && labStartedPlay) { if (syncOwn()) sync.stopOwn(); else internalPlay = false; labStartedPlay = false; }
    ++version;
}
void ArpProcessor::labStar (int tile)
{
    if (tile < 0 || tile >= (int) lab.tiles.size()) return;
    auto& h = lab.tiles[(size_t) tile]; h.star = ! h.star;
    if (h.star)
    {
        const auto ms = strongestMotif (h.notes, I.H, h.ticks());
        if (ms.valid()) { std::vector<Note> fr; for (auto& x : h.notes) if (x.t >= ms.t0 && x.t < ms.t1) fr.push_back (x); learnTaste (fragmentFeatures (fr, h.notes, I.H, ms.t0, ms.t1), true); }
    }
    ++version;
}
// the idea got longer (USE THIS of an 8-bar hook, MAKE IT A SONG, MUTANT EVOLVE): the other lanes follow; the user's edited lanes are looped + refit, never recomposed
juce::String extendLanes (Idea& X, int oldTotal);
juce::String extendLanes (Idea& X, int oldTotal)
{
    if (X.total <= oldTotal) return {};
    juce::StringArray kept, fresh;
    for (int l : { LaneArp, LaneBass })
    {
        auto& L = X.lanes[l];
        static const char* nm[] { "CHORDS", "ARP", "MELODY", "BASS" };
        if (L.edited) { std::vector<Note> base; for (auto& n : L.notes) if (n.t < oldTotal) base.push_back (n); L.notes = loopHook (base, oldTotal, X.total, X.H); kept.add (nm[l]); }
        else { composeLane (X, l, true); fresh.add (nm[l]); }
    }
    juce::String s;
    if (! fresh.isEmpty()) s << fresh.joinIntoString (" + ") << " extended";
    if (! kept.isEmpty()) s << (s.isEmpty() ? "" : ", ") << kept.joinIntoString (" + ") << " (your edits) looped";
    return s + " to " + juce::String (X.total / BAR) + " bars";
}
void ArpProcessor::labUse (int tile)
{
    if (tile < 0 || tile >= (int) lab.tiles.size()) return;
    const Hook h = lab.tiles[(size_t) tile];
    {   // a USE is the strongest keep there is
        const auto ms = strongestMotif (h.notes, I.H, h.ticks());
        if (ms.valid()) { std::vector<Note> fr; for (auto& x : h.notes) if (x.t >= ms.t0 && x.t < ms.t1) fr.push_back (x); learnTaste (fragmentFeatures (fr, h.notes, I.H, ms.t0, ms.t1), true); }
    }
    change ([&] (Idea& X) {
        const int old = X.total;
        if (X.total < h.ticks()) { X.phraseBars = juce::jmax (X.phraseBars, h.bars); rebuildChords (X); extendLanes (X, old); }   // an 8-bar hook over a 4-bar idea: the idea grows
        auto& M = X.lanes[LaneMelody];
        M.notes = loopHook (h.notes, h.ticks(), X.total, X.H); M.edited = true; M.mute = false;
        X.stagger = 0;
    }, true, "USE HOOK");
}
juce::String ArpProcessor::labSong (int tile, int bars)
{
    if (tile < 0 || tile >= (int) lab.tiles.size()) return {};
    const Hook h = lab.tiles[(size_t) tile];
    juce::String also;
    change ([&] (Idea& X) {
        const int old = X.total;
        X.songBars = bars >= 32 ? 32 : 16;   // the take gets longer; EVOLVE stays as the user had it
        rebuildChords (X);
        also = extendLanes (X, old);
        HookParams hp = hookParams (X, h.bars, h.seed); hp.reg = lab.reg;
        auto song = makeSong (X, h, X.songBars, hp);
        auto& M = X.lanes[LaneMelody];
        M.notes = loopHook (song, X.songBars * BAR, X.total, X.H); M.edited = true; M.mute = false; X.stagger = 0;
    }, true, "MAKE IT A SONG");
    return also;
}
int ArpProcessor::labCombine (int r, int o, int e)
{
    const int n = (int) lab.tiles.size();
    if (r < 0 || o < 0 || e < 0 || r >= n || o >= n || e >= n) return -1;
    Hook c = combineHooks (I, lab.tiles[(size_t) r], lab.tiles[(size_t) o], lab.tiles[(size_t) e]);
    c.label = "COMBO " + std::to_string (r + 1) + "/" + std::to_string (o + 1) + "/" + std::to_string (e + 1);
    int at = -1;   // replaces the lowest-scoring unstarred tile that isn't one of the three (or appends under 16)
    if (n < 16) { lab.tiles.push_back (c); at = n; }
    else { double ws = 1e9; for (int i = 0; i < n; ++i) if (! lab.tiles[(size_t) i].star && i != r && i != o && i != e && lab.tiles[(size_t) i].score < ws) { ws = lab.tiles[(size_t) i].score; at = i; } if (at >= 0) lab.tiles[(size_t) at] = c; else return -2; }
    labAudition (at);
    return at;
}
juce::File ArpProcessor::writeHookMidi (int tile)
{
    if (! exportUnlocked()) return {};   // back-stop: the demo writes nothing (the caller asked the gate)
    if (tile < 0 || tile >= (int) lab.tiles.size()) return {};
    const auto& h = lab.tiles[(size_t) tile];
    auto hn = h.notes; { const int tr = transposeNow(); if (tr != 0 && (transposeMask.load() & (1 << LaneMelody))) for (auto& n : hn) n.p = foldPitch (n.p + tr, LaneMelody); }
    MidiTrack t; t.name = "Hook " + std::to_string (tile + 1); t.ch = 0; t.notes = laneMidiNotes (hn, h.ticks());
    const auto bytes = toHostTempo (writeMidi (effectiveBpm(), { t }));   // TEMPO: written at the DAW's tempo, times scaled (lands as heard)
    const auto name = slug ("ArpEZator hook " + juce::String (tile + 1) + " " + juce::String (keyName (I.g.key, I.g.scale)) + " " + ghosty::tempomul::bpmText (effectiveBpm()) + "bpm" + ghosty::tempomul::fileTag (tempoMul.load()));
    auto dir = presetDir().getChildFile ("MIDI"); dir.createDirectory();
    auto f = dir.getChildFile (name + ".mid").getNonexistentSibling (true);
    f.replaceWithData (bytes.data(), bytes.size());   // GATE: arp.hook.midi-drag
    return f;
}
void ArpProcessor::respond (double sim)
{
    lab.similarity = juce::jlimit (0.0, 1.0, sim);
    change ([&] (Idea& X) {
        auto& M = X.lanes[LaneMelody];
        const int P = melodyPhraseTicks (X), half = juce::jmax (PPQ, (P / 2 / BAR) * BAR > 0 ? (P / 2 / BAR) * BAR : P / 2);
        const int end = juce::jmin (P, 2 * half);   // an odd phrase (3, 5, 7 bars): the bars after the answer stay as they were
        std::vector<Note> phrase; for (auto& n : M.notes) if (n.t < P) phrase.push_back (n);
        auto resp = arp::generateResponse (phrase, X.H, 0, half, half, lab.similarity, X.g.arpSeed ^ (uint32_t) version.load());
        std::vector<Note> out; for (auto& n : phrase) if (n.t < half || n.t >= end || n.lock) out.push_back (n);
        std::set<int> lockedT; for (auto& n : out) if (n.lock) lockedT.insert (n.t);
        for (auto& n : resp) if (n.t < end && ! lockedT.count (n.t)) out.push_back (n);   // never on top of a locked note
        sortNotes (out);
        std::vector<Note> tidyOut; for (auto& n : out) { if (! tidyOut.empty() && tidyOut.back().t == n.t) { if (n.lock) tidyOut.back() = n; continue; } tidyOut.push_back (n); }
        for (size_t i = 0; i + 1 < tidyOut.size(); ++i) if (tidyOut[i].l.type != 1) tidyOut[i].d = juce::jmax (6, juce::jmin (tidyOut[i].d, tidyOut[i + 1].t - tidyOut[i].t));
        M.notes = loopHook (tidyOut, P, X.total, X.H); M.edited = true; M.mute = false;
    }, true, "RESPONSE");
}
void ArpProcessor::lockMotif (const std::vector<bool>& sel)
{
    change ([&] (Idea& X) {
        const int P = melodyPhraseTicks (X);
        if (! X.motif.empty())
        {   // unlock only what LOCK MOTIF placed (the user's own LOCK SEL locks stay)
            std::set<std::pair<int, int>> m; for (auto& n : X.motif) m.insert ({ n.t, n.p });
            for (auto& n : X.lanes[LaneMelody].notes)
            {
                const int rel = (n.t % P) - X.motifT0;
                if (rel >= 0 && rel < X.motifLen && m.count ({ rel, n.p })) n.lock = false;
            }
            X.motif.clear(); X.motifLen = 0; return;
        }
        const auto& ns = X.lanes[LaneMelody].notes;
        int t0 = 1 << 30, t1 = -1;
        for (size_t i = 0; i < ns.size() && i < sel.size(); ++i) if (sel[i]) { t0 = juce::jmin (t0, ns[i].t); t1 = juce::jmax (t1, ns[i].t + ns[i].d); }
        if (t1 < 0) { std::vector<Note> ph; for (auto& n : ns) if (n.t < P) ph.push_back (n); const auto ms = strongestMotif (ph, X.H, P); if (! ms.valid()) return; t0 = ms.t0; t1 = ms.t1; }
        t0 = (t0 / PPQ) * PPQ; t1 = juce::jmin (t0 + P, ((t1 + PPQ - 1) / PPQ) * PPQ);
        X.motif.clear(); for (auto n : ns) if (n.t >= t0 && n.t < t1) { n.t -= t0; n.lock = false; X.motif.push_back (n); }
        X.motifT0 = t0 % P; X.motifLen = t1 - t0;
        X.lanes[LaneMelody].notes = placeMotif (ns, X.motif, X.motifT0, X.motifLen, P, X.total);
    }, true, "LOCK MOTIF");
}
void ArpProcessor::labHarvestRank() { ensureTaste(); lab.harvest = harvestFragments (I, taste, 6); ++version; }
void ArpProcessor::labHarvestKeep (int k)
{
    if (k < 0 || k >= (int) lab.harvest.size()) return;
    const Fragment F = lab.harvest[(size_t) k];
    learnTaste (F.f, true);
    for (int j = 0; j < k; ++j) learnTaste (lab.harvest[(size_t) j].f, false);   // ranked above it and passed over
    change ([&] (Idea& X) {
        const int P = melodyPhraseTicks (X);
        X.motif.clear(); for (auto n : F.notes) { n.t -= F.t0; n.lock = false; X.motif.push_back (n); }
        X.motifT0 = F.t0 % P; X.motifLen = F.t1 - F.t0;
        if (F.lane == LaneArp) { const int med = medianPitch (X.lanes[LaneMelody].notes); const int fm = medianPitch (X.motif); const int sh = 12 * (int) std::lround ((med - fm) / 12.0); for (auto& n : X.motif) n.p += sh; }
        auto& M = X.lanes[LaneMelody];
        M.notes = placeMotif (M.notes, X.motif, X.motifT0, X.motifLen, P, X.total); M.edited = true; M.mute = false;
    }, true, "HARVEST MOTIF");
    labHarvestRank();
}

static juce::StringArray starterFiles()
{
    juce::StringArray s;
#if ARPEZATOR_HAS_DATA
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        const juce::String f (BinaryData::originalFilenames[i]);
        if (f.endsWithIgnoreCase (".json") && f != "recipe.json" && f != "index.json") s.add (f);
    }
    s.sort (true);
#endif
    return s;
}

juce::StringArray ArpProcessor::starterNames() const
{   // "midnight-01-m09-Csm-125bpm.json" -> "MIDNIGHT 01  C#m  125 bpm"
    juce::StringArray out;
    for (auto& f : starterFiles())
    {
        auto parts = juce::StringArray::fromTokens (f.upToLastOccurrenceOf (".", false, false), "-", "");
        if (parts.size() >= 5) out.add (parts[0].toUpperCase() + " " + parts[1] + "   " + parts[3].replace ("s", "#") + "   " + parts[4].replace ("bpm", " bpm"));
        else out.add (f);
    }
    return out;
}

void ArpProcessor::loadStarter (int index)
{
#if ARPEZATOR_HAS_DATA
    const auto files = starterFiles();
    if (index < 0 || index >= files.size()) return;
    int size = 0; const char* data = nullptr;
    for (int i = 0; i < BinaryData::namedResourceListSize; ++i) if (juce::String (BinaryData::originalFilenames[i]) == files[index]) data = BinaryData::getNamedResource (BinaryData::namedResourceList[i], size);
    if (data == nullptr) return;
    const auto j = juce::JSON::parse (juce::String::fromUTF8 (data, size));
    if (! j.isObject()) return;
    static const std::map<juce::String, int> PCN { { "C", 0 }, { "C#", 1 }, { "Db", 1 }, { "D", 2 }, { "D#", 3 }, { "Eb", 3 }, { "E", 4 }, { "F", 5 }, { "F#", 6 }, { "Gb", 6 }, { "G", 7 }, { "G#", 8 }, { "Ab", 8 }, { "A", 9 }, { "A#", 10 }, { "Bb", 10 }, { "B", 11 } };
    const juce::String modeS = j["mode"].toString();
    const int mode = modeS == "SUNRISE" ? Sunrise : modeS == "DUSK" ? Dusk : Midnight;
    const int key = PCN.count (j["key"].toString()) ? PCN.at (j["key"].toString()) : 9;
    std::string scale = j["scale"].toString().toStdString(); if (scale.empty()) scale = "minor";
    const int bpm = juce::jlimit (60, 200, (int) j["bpm"]);
    Idea X; X.mode = mode; X.seed = (uint32_t) j["id"].toString().hashCode(); applyModeDefaults (X); X.simple = I.simple;
    X.g = generateForMode (mode, "", X.seed, X.prompt, X.seedUsed);
    X.g.key = key; X.g.scale = scale; X.g.bpm = bpm; X.chordBars = 1;
    // exact chords from the voicings (as fixed chords: root from the numeral, colour from the notes)
    std::vector<Slot> prog;
    if (auto* tl = j["realized"]["chord_timeline"].getArray())
        for (auto& c : *tl)
        {
            const std::string sym = c["chord"].toString().toStdString();
            static const int MAJ[7] { 0, 2, 4, 5, 7, 9, 11 };
            int acc = 0; size_t i = 0; if (i < sym.size() && (sym[i] == 'b' || sym[i] == '#')) { acc = sym[i] == 'b' ? -1 : 1; ++i; }
            int deg = 0; static const char* NUM[] { "VII", "VI", "IV", "V", "III", "II", "I", "vii", "vi", "iv", "v", "iii", "ii", "i" }; static const int DEG[] { 6, 5, 3, 4, 2, 1, 0, 6, 5, 3, 4, 2, 1, 0 };
            for (int k = 0; k < 14; ++k) if (sym.compare (i, std::strlen (NUM[k]), NUM[k]) == 0) { deg = DEG[k]; break; }
            const int root = mod12 (MAJ[deg] + acc);
            FixedChord f; f.root = root; std::vector<int> rel;
            if (auto* vo = c["voicing"].getArray()) for (auto& p : *vo) { const int r = mod12 ((int) p - key - root); if (std::find (rel.begin(), rel.end(), r) == rel.end()) rel.push_back (r); }
            if (std::find (rel.begin(), rel.end(), 0) == rel.end()) rel.push_back (0);
            std::sort (rel.begin(), rel.end());
            const bool third = std::find (rel.begin(), rel.end(), 3) != rel.end() || std::find (rel.begin(), rel.end(), 4) != rel.end();
            for (int r : rel) f.iv.push_back (r == 2 && third ? 14 : r == 5 && third ? 17 : r);
            std::sort (f.iv.begin(), f.iv.end());
            const auto& sc = scaleOf (scale); for (int x : f.iv) if (std::find (sc.begin(), sc.end(), mod12 (root + x)) == sc.end()) f.chrom = true;
            Slot s; s.tok = sym; s.fixed = f; s.len = juce::jmax (1, (int) std::lround (((double) c["end16"] - (double) c["start16"]) / 4.0));
            prog.push_back (s);
        }
    if (prog.empty()) return;
    X.g.prog = prog;
    const juce::String cr = j["chord_rhythm"].toString();
    X.chordRhythm = cr.startsWith ("x---") ? 0 : cr == "x..x..x...x..x.." ? 2 : 1;
    rebuildChords (X);
    auto notesOf = [] (const juce::var& a) { std::vector<Note> ns; if (auto* arr = a.getArray()) for (auto& n : *arr) { Note x; x.t = (int) n["t"]; x.d = juce::jmax (6, (int) n["d"]); x.p = (int) n["pitch"]; x.v = (float) (double) n["vel"]; x.acc = x.t % PPQ == 0; ns.push_back (x); } sortNotes (ns); return ns; };
    for (auto& L : X.lanes) { L.notes.clear(); L.edited = false; }
    if (j["arp"].isObject())
    {   // the arp as a real FIGURE (the pattern editor + tweaks work on it)
        const auto A = j["arp"]; std::vector<std::string> toks; if (auto* pa = A["pattern"].getArray()) for (auto& t : *pa) toks.push_back (t.toString().toStdString());
        X.fig = figureFromTokens (toks); X.fig.pedalDeg = A["pedal"].toString().toStdString(); X.fig.resync = juce::jlimit (1, 4, (int) A["resync"]); X.fig.floorOff = (int) A["floor_off"];
        X.arpGate = A.hasProperty ("gate") ? (double) A["gate"] : 0.5; X.arpSpb = 16;
        X.figEvents.clear();
        if (auto* ev = A["evolve"].getArray())
            for (auto& e : *ev)
            {
                FigEvent fe; fe.fromBar = (int) e["from_bar"]; fe.pedalDeg = e["pedal"].toString().toStdString();
                if (auto* pa = e["pattern"].getArray()) { std::vector<std::string> t2; for (auto& t : *pa) t2.push_back (t.toString().toStdString()); fe.hasFig = true; fe.fig = figureFromTokens (t2); fe.fig.floorOff = X.fig.floorOff; fe.fig.resync = X.fig.resync; }
                X.figEvents.push_back (fe);
            }
        rerealizeArp (X); X.lanes[LaneArp].mute = false;
        composeLane (X, LaneMelody, false);
    }
    else
    {
        X.lanes[LaneMelody].notes = notesOf (j["realized"]["lead"]); X.lanes[LaneMelody].edited = true; X.lanes[LaneMelody].mute = false;
        composeLane (X, LaneArp, false);
    }
    X.lanes[LaneBass].notes = notesOf (j["realized"]["bass"]); X.lanes[LaneBass].edited = true;
    if (X.simple) refitLanesToChords (X);   // SIMPLE: the starter's chords were simplified, the lanes follow
    pushUndo ("starter"); stashBeforeReplace (X.mode);
    change ([&X] (Idea& target) { target = X; }, false);
#else
    juce::ignoreUnused (index);
#endif
}

arp::MidiExport ArpProcessor::midiExport() const
{
    MidiExport X;
    X.glideBend = apvts.getRawParameterValue ("glideBend")->load() > 0.5f;
    X.bendRange = std::vector<int> { 2, 12, 24 }[(size_t) juce::jlimit (0, 2, (int) apvts.getRawParameterValue ("bendRange")->load())];
    if (apvts.getRawParameterValue ("velScale")->load() > 0.5f) for (int l = 0; l < kLanes; ++l) X.laneGain[l] = dbToGain (apvts.getRawParameterValue (laneVolId (l))->load());
    return X;
}

std::vector<uint8_t> ArpProcessor::toHostTempo (const std::vector<uint8_t>& smf) const
{
    const double f = tempoFactor(); if (f == 1.0 || smf.empty()) return smf;
    juce::MidiFile mf; juce::MemoryInputStream in (smf.data(), smf.size(), false); if (! mf.readFrom (in)) return smf;
    ghosty::tempomul::scaleMidiToHost (mf, f);
    juce::MemoryOutputStream out; mf.writeTo (out); const auto* d = static_cast<const uint8_t*> (out.getData()); return { d, d + out.getDataSize() };
}
juce::File ArpProcessor::writeMidiFile (int laneMask, bool oneTrack)
{
    if (! exportUnlocked()) return {};   // back-stop: the demo writes nothing (the caller asked the gate)
    std::vector<uint8_t> bytes; juce::String name;
    {
        const juce::ScopedLock sl (ideaLock);
        const auto sc = section(); const bool sec = drawMode() == 1 && sc.valid();
        bytes = toHostTempo (ideaMidi (exportIdea(), laneMask, oneTrack, effectiveBpm(), midiExport(), sec ? sc.start : -1, sec ? sc.end : -1));   // TEMPO: at the DAW's tempo, times scaled   // as it sounds (TRANSPOSE included); DRAG: SECTION = just the brace
        static const char* nm[] { "chords", "arp", "melody", "bass" };
        juce::String what; int cnt = 0; for (int l = 0; l < kLanes; ++l) if (laneMask & (1 << l)) { what << (cnt++ ? "+" : "") << nm[l]; }
        if (cnt == kLanes) what = "all";
        name = slug ("ArpEZator " + juce::String (keyName (I.g.key, I.g.scale)) + " " + ghosty::tempomul::bpmText (effectiveBpm()) + "bpm" + ghosty::tempomul::fileTag (tempoMul.load()) + " " + what);
    }
    auto dir = presetDir().getChildFile ("MIDI"); dir.createDirectory();
    auto f = dir.getChildFile (name + ".mid").getNonexistentSibling (true);   // never overwrites
    f.replaceWithData (bytes.data(), bytes.size());   // GATE: via-caller (every lane / stems / section / click MIDI path asks first; back-stop above)
    return f;
}

juce::File ArpProcessor::presetDir()
{
    auto d = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Ghosty").getChildFile ("ArpEZator");
    d.createDirectory();
    return d;
}

bool ArpProcessor::savePreset (const juce::String& name)
{
    juce::var v;
    { const juce::ScopedLock sl (ideaLock); v = ideaToVar (I); }
    if (auto* o = v.getDynamicObject())
    {
        juce::MemoryOutputStream ms; if (auto xml = apvts.copyState().createXml()) xml->writeTo (ms);
        o->setProperty ("sound", ms.toString());
    }
    auto f = presetDir().getChildFile (slug (name.isEmpty() ? juce::String ("My idea") : name) + ".arpz").getNonexistentSibling (true);
    return f.replaceWithText (juce::JSON::toString (v));
}

bool ArpProcessor::loadPreset (const juce::File& f)
{
    const auto v = juce::JSON::parse (f.loadFileAsString());
    if (! v.isObject()) return false;
    pushUndo ("load idea"); stashBeforeReplace (juce::jlimit (0, kMoods - 1, v.hasProperty ("mode") ? (int) v["mode"] : I.mode));
    change ([&] (Idea& X) { ideaFromVar (v, X); rebuildChords (X); }, false);
    if (v["sound"].toString().isNotEmpty()) restoreSound (v["sound"].toString());   // (never the BYPASS)
    return true;
}

void ArpProcessor::tapTempo()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (! taps.empty() && now - taps.back() > 2000) taps.clear();
    taps.push_back (now); if (taps.size() > 8) taps.erase (taps.begin());
    if (taps.size() >= 2)
    {
        double bpm = 60000.0 / ((taps.back() - taps.front()) / (double) (taps.size() - 1));
        while (bpm < 60) bpm *= 2;
        while (bpm > 200) bpm /= 2;
        const int b = juce::roundToInt (bpm);
        if (syncOwn()) setOwnBpm (b); else change ([b] (Idea& X) { X.g.bpm = b; }, false);
    }
}

// ---------------------------------------------------------------- LISTEN
float ArpProcessor::listenProgress() const { return capturing.load() ? juce::jlimit (0.0f, 1.0f, (float) (capWritten.load() / (capSr * 12.0))) : 0.0f; }

void ArpProcessor::startListening()
{
    if (listenState == 2) return;
    capFifo.reset(); capWritten = 0; capturing = true; listenState = 1;
    listenMessage = getBusCount (true) > 0 && getBus (true, 0)->isEnabled() ? "Listening to the sidechain... (about 12 s)" : "Listening... route audio into the sidechain input (about 12 s)";
    ++version;
}

void ArpProcessor::stopListening()
{
    if (! capturing.load()) return;
    capturing = false;
    const int ready = capFifo.getNumReady();
    std::vector<float> mono ((size_t) ready);
    int s1, n1, s2, n2; capFifo.prepareToRead (ready, s1, n1, s2, n2);
    std::copy (capBuf.begin() + s1, capBuf.begin() + s1 + n1, mono.begin());
    std::copy (capBuf.begin() + s2, capBuf.begin() + s2 + n2, mono.begin() + n1);
    capFifo.finishedRead (n1 + n2);
    if (ready < (int) (capSr * 4)) { listenState = 4; listenMessage = "Only " + juce::String (ready / capSr, 1) + " s heard. Let it listen for at least 4 s."; ++version; return; }
    analyseAsync (std::move (mono), capSr);
}

void ArpProcessor::analyseAsync (std::vector<float> mono, double rate)
{
    listenState = 2; listenMessage = "Working out tempo, key and chords..."; ++version;
    auto* w = static_cast<Worker*> (worker.get());
    auto flag = alive;
    const bool ok = w->post ([this, flag, mono = std::move (mono), rate] {
        auto r = listen::analyze (mono, rate);
        juce::MessageManager::callAsync ([this, flag, r] {
            if (! flag->load()) return;
            lastHeard = r;
            if (! r.ok) { listenState = 4; listenMessage = r.why == "too quiet" ? "That was too quiet. Turn the music up, then LISTEN again." : "Couldn't analyse that (" + juce::String (r.why) + ")."; ++version; return; }
            listenState = 3;
            listenMessage = "Heard " + juce::String (r.bpmInt) + " BPM, " + juce::String (noteName (r.key, r.key, r.scale)) + " " + juce::String (r.scale) + ". New idea written from scratch.";
            applyHeard ((uint32_t) juce::Random::getSystemRandom().nextInt ({ 1, 999999 }), false);
        });
    });
    if (! ok) { listenState = 4; listenMessage = "Still busy, try again in a moment."; ++version; }
}

void ArpProcessor::listenToFile (const juce::File& f)
{
    listenState = 2; listenMessage = "Reading " + f.getFileName() + "..."; ++version;
    auto* w = static_cast<Worker*> (worker.get());
    auto flag = alive;
    const bool ok = w->post ([this, flag, f] {
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
        if (rd == nullptr) { juce::MessageManager::callAsync ([this, flag] { if (! flag->load()) return; listenState = 4; listenMessage = "Couldn't decode that file. Try a wav, aiff, flac, ogg or mp3."; ++version; }); return; }
        const juce::int64 maxLen = (juce::int64) (rd->sampleRate * 75);
        const juce::int64 len = juce::jmin (rd->lengthInSamples, maxLen), start = rd->lengthInSamples > maxLen ? (rd->lengthInSamples - maxLen) / 2 : 0;
        juce::AudioBuffer<float> b ((int) juce::jmax (1u, rd->numChannels), (int) len);
        rd->read (&b, 0, (int) len, start, true, true);
        std::vector<float> mono ((size_t) len, 0.0f);
        for (int c = 0; c < b.getNumChannels(); ++c) for (int i = 0; i < (int) len; ++i) mono[(size_t) i] += b.getSample (c, i) / (float) b.getNumChannels();
        auto r = listen::analyze (mono, rd->sampleRate);
        juce::MessageManager::callAsync ([this, flag, r] {
            if (! flag->load()) return;
            lastHeard = r;
            if (! r.ok) { listenState = 4; listenMessage = "Couldn't analyse that (" + juce::String (r.why) + ")."; ++version; return; }
            listenState = 3;
            listenMessage = "Heard " + juce::String (r.bpmInt) + " BPM, " + juce::String (noteName (r.key, r.key, r.scale)) + " " + juce::String (r.scale) + ". New idea written from scratch.";
            applyHeard ((uint32_t) juce::Random::getSystemRandom().nextInt ({ 1, 999999 }), false);
        });
    });
    if (! ok) { listenState = 4; listenMessage = "Still busy, try again in a moment."; ++version; }
}

void ArpProcessor::applyHeard (uint32_t seed, bool keepChords)
{
    const listen::Result h = lastHeard;
    if (! h.ok) return;
    change ([&] (Idea& X) {
        if (keepChords) { X.g.bpm = h.bpmInt; X.g.key = h.key; X.g.scale = h.scale; rebuildChords (X); refitLanesToChords (X); return; }
        // a prompt in the style of what was heard, then the HEARD chords as slots (numerals when they have one)
        const juce::String g = h.bpmInt < 96 ? (h.centroid < 1600 ? "lofi" : "cinematic") : h.bpmInt < 121 ? "house" : h.bpmInt < 133 ? "progressive" : "trance";
        const juce::String mood = h.centroid < 1100 ? "dark" : h.scale == "minor" ? "emotional" : "happy";
        const juce::String p = mood + " " + g + " in " + juce::String (noteName (h.key, h.key, h.scale)).toLowerCase() + " " + juce::String (h.scale) + " " + juce::String (h.bpmInt) + " bpm " + (h.rate == "1/16T" ? juce::String ("triplet") : juce::String (h.rate));
        X.g = arp::generate (p.toStdString(), seed); X.prompt = p.toStdString(); X.seedUsed = seed; X.simpleProg.clear();   // SIMPLE: the heard chords, simplified
        X.g.bpm = h.bpmInt; X.g.key = h.key; X.g.scale = h.scale; X.groove = h.groove; X.arpRange = juce::jlimit (1, 3, h.oct);
        X.arpSpb = h.rate == "1/8" ? 8 : h.rate == "1/16T" ? 24 : 16;
        Rng R ((seed ^ 0x11573u));
        std::vector<Slot> prog;
        for (auto& c : h.chords)
        {
            const int relr = mod12 (c.root - h.key); const std::vector<int> tri (c.iv.begin(), c.iv.begin() + 3);
            const auto tok = degToken (relr, tri, h.scale == "minor" ? "minor" : "major");
            std::string q; if (c.type == "7" || c.type == "m7" || c.type == "maj7") q = "sev"; else if (c.type == "add9") q = "add9"; else if (c.type == "sus2" || c.type == "sus4") q = "sus";
            bool done = false;
            if (tok)
            {
                Slot s; s.tok = *tok; s.r = R(); s.r2 = R(); s.alt = 0; s.len = 4;
                const auto a = altChord (s, h.scale, 0);
                if (mod12 (a.root) == relr && (triadQuality (a.iv) == triadQuality (tri) || q == "sus")) { s.q = q; prog.push_back (s); done = true; }
            }
            if (! done)
            {
                Slot s; FixedChord fx; fx.root = relr; for (int x : c.iv) fx.iv.push_back (c.type == "add9" && x == 2 ? 14 : x);
                const auto& sc = scaleOf (h.scale); for (int x : fx.iv) if (std::find (sc.begin(), sc.end(), mod12 (relr + x)) == sc.end()) fx.chrom = true;
                s.fixed = fx; s.len = 4; s.r = 0.5; s.r2 = 0.9; prog.push_back (s);
            }
        }
        if (! prog.empty()) X.g.prog = prog;
        rebuildChords (X);
        for (int l = LaneArp; l < kLanes; ++l) composeLane (X, l, false);
    }, true, "LISTEN");
}

// ---------------------------------------------------------------- state
static juce::var notesToVar (const std::vector<Note>& ns)
{
    juce::Array<juce::var> a;
    for (auto& n : ns)
    {
        juce::Array<juce::var> e; e.add (n.t); e.add (n.d); e.add (n.p); e.add (n.v); e.add (n.l.type); e.add (n.l.w); e.add (n.l.f); e.add ((int) n.lock); e.add ((int) n.acc);
        a.add (e);
    }
    return a;
}
static std::vector<Note> notesFromVar (const juce::var& v)
{
    std::vector<Note> o;
    if (auto* a = v.getArray())
        for (auto& e : *a)
            if (auto* x = e.getArray(); x != nullptr && x->size() >= 9)
            {
                Note n; n.t = (*x)[0]; n.d = juce::jmax (1, (int) (*x)[1]); n.p = juce::jlimit (0, 127, (int) (*x)[2]); n.v = (float) (double) (*x)[3];
                n.l.type = juce::jlimit (0, 2, (int) (*x)[4]); n.l.w = (float) (double) (*x)[5]; n.l.f = (float) (double) (*x)[6]; n.lock = (int) (*x)[7] != 0; n.acc = (int) (*x)[8] != 0;
                o.push_back (n);
            }
    sortNotes (o);
    return o;
}
juce::var ArpProcessor::labToVar (const HookLabState& L)
{
    auto* o = new juce::DynamicObject();
    juce::Array<juce::var> tiles;
    for (auto& h : L.tiles)
    {
        auto* t = new juce::DynamicObject();
        t->setProperty ("notes", notesToVar (h.notes)); t->setProperty ("bars", h.bars); t->setProperty ("seed", (juce::int64) h.seed);
        t->setProperty ("score", h.score); t->setProperty ("piano", h.piano); t->setProperty ("star", h.star); t->setProperty ("label", juce::String (h.label));
        t->setProperty ("harm", juce::String (h.harm)); t->setProperty ("kept", h.kept); t->setProperty ("fresh", h.fresh); t->setProperty ("meter", h.meter); t->setProperty ("feel", h.feel);
        tiles.add (juce::var (t));
    }
    o->setProperty ("tiles", tiles); o->setProperty ("selected", L.selected); o->setProperty ("bars", L.bars); o->setProperty ("seed", (juce::int64) L.seed);
    o->setProperty ("similarity", L.similarity); o->setProperty ("piano", L.piano); o->setProperty ("style", L.style); o->setProperty ("candy", L.candy);
    o->setProperty ("tension", L.tension); o->setProperty ("reg", L.reg); o->setProperty ("meter", L.meter); o->setProperty ("euclidHits", L.euclidHits); o->setProperty ("euclidRot", L.euclidRot); o->setProperty ("feel", L.feel); o->setProperty ("autoPlay", L.autoPlay); o->setProperty ("chordsChanged", L.chordsChanged);
    return juce::var (o);
}
void ArpProcessor::labFromVar (const juce::var& v, HookLabState& L)
{
    if (! v.isObject()) return;
    L.tiles.clear();
    if (auto* a = v["tiles"].getArray())
        for (auto& t : *a)
        {
            Hook h; h.notes = notesFromVar (t["notes"]); h.bars = (int) t["bars"] >= 8 ? 8 : 4; h.seed = (uint32_t) (juce::int64) t["seed"];
            h.score = (double) t["score"]; h.piano = (double) t["piano"]; h.star = (bool) t["star"]; h.label = t["label"].toString().toStdString();
            h.harm = t["harm"].toString().toStdString(); h.kept = (bool) t["kept"]; h.fresh = t.hasProperty ("fresh") ? (double) t["fresh"] : -1.0; h.meter = juce::jlimit (0, kMeters - 1, (int) t.getProperty ("meter", 0)); h.feel = juce::jlimit (0, kFeels - 1, (int) t.getProperty ("feel", 0));
            if (! h.notes.empty() && L.tiles.size() < 16) L.tiles.push_back (h);
        }
    L.selected = juce::jlimit (-1, (int) L.tiles.size() - 1, (int) v.getProperty ("selected", -1)); L.playing = -1;
    L.bars = (int) v.getProperty ("bars", 4) >= 8 ? 8 : 4; L.seed = (uint32_t) (juce::int64) v.getProperty ("seed", 1);
    L.similarity = juce::jlimit (0.0, 1.0, (double) v.getProperty ("similarity", 0.6)); L.piano = (bool) v.getProperty ("piano", false);
    L.style = juce::jlimit (0, kStyles - 1, (int) v.getProperty ("style", 0)); L.candy = juce::jlimit (0, kCandies - 1, (int) v.getProperty ("candy", 0));
    L.tension = juce::jlimit (-1.0, 1.0, (double) v.getProperty ("tension", -1.0)); L.reg = juce::jlimit (0, kRegisters - 1, (int) v.getProperty ("reg", (int) RegMid));
    L.meter = juce::jlimit (0, kMeters - 1, (int) v.getProperty ("meter", 0)); L.euclidHits = juce::jlimit (0, 20, (int) v.getProperty ("euclidHits", 0));
    L.euclidRot = juce::jmax (0, (int) v.getProperty ("euclidRot", 0)); L.feel = juce::jlimit (0, kFeels - 1, (int) v.getProperty ("feel", 0));
    L.autoPlay = (bool) v.getProperty ("autoPlay", false); L.chordsChanged = (bool) v.getProperty ("chordsChanged", false);
    L.harvest.clear(); L.history.clear(); L.histIdx = -1; L.busy = false; L.busyKind = 0;
}

static juce::var progToVar (const std::vector<Slot>& pr)
{
    juce::Array<juce::var> prog;
    for (auto& s : pr)
    {
        auto* so = new juce::DynamicObject();
        so->setProperty ("tok", juce::String (s.tok)); so->setProperty ("r", s.r); so->setProperty ("r2", s.r2); so->setProperty ("alt", s.alt); so->setProperty ("len", s.len);
        so->setProperty ("q", juce::String (s.q)); so->setProperty ("sl", juce::String (s.sl));
        if (s.fixed)
        {
            juce::Array<juce::var> iv; for (int x : s.fixed->iv) iv.add (x);
            so->setProperty ("fRoot", s.fixed->root); so->setProperty ("fIv", iv); so->setProperty ("fBass", s.fixed->bass ? *s.fixed->bass : -1); so->setProperty ("fChrom", s.fixed->chrom);
        }
        prog.add (juce::var (so));
    }
    return prog;
}
static std::vector<Slot> progFromVar (const juce::var& v)
{
    auto gi = [] (const juce::var& o, const char* k, int d) { return o.hasProperty (k) ? (int) o[k] : d; };
    auto gd = [] (const juce::var& o, const char* k, double d) { return o.hasProperty (k) ? (double) o[k] : d; };
    std::vector<Slot> out;
    if (auto* a = v.getArray())
        for (auto& so : *a)
        {
            Slot s; s.tok = so["tok"].toString().toStdString(); s.r = gd (so, "r", 0.5); s.r2 = gd (so, "r2", 0.9); s.alt = gi (so, "alt", 0); s.len = juce::jlimit (1, 32, gi (so, "len", 4));
            s.q = so["q"].toString().toStdString(); s.sl = so["sl"].toString().toStdString();
            if (so.hasProperty ("fRoot"))
            {
                FixedChord f; f.root = gi (so, "fRoot", 0); if (auto* iv = so["fIv"].getArray()) for (auto& x : *iv) f.iv.push_back ((int) x);
                const int b = gi (so, "fBass", -1); if (b >= 0) f.bass = b; f.chrom = so.hasProperty ("fChrom") && (bool) so["fChrom"];
                if (f.iv.size() >= 2) s.fixed = f;
            }
            out.push_back (s);
        }
    return out;
}

juce::var ArpProcessor::ideaToVar (const Idea& I)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("v", 1);
    o->setProperty ("mode", I.mode); o->setProperty ("words", juce::String (I.words)); o->setProperty ("seed", (juce::int64) I.seed);
    o->setProperty ("prompt", juce::String (I.prompt)); o->setProperty ("seedUsed", (juce::int64) I.seedUsed);
    const Gen& g = I.g;
    auto* go = new juce::DynamicObject();
    go->setProperty ("genre", juce::String (g.genre)); juce::StringArray moods; for (auto& m : g.moods) moods.add (m); go->setProperty ("moods", moods.joinIntoString (","));
    go->setProperty ("key", g.key); go->setProperty ("scale", juce::String (g.scale)); go->setProperty ("bars", g.bars); go->setProperty ("colour", g.colour);
    go->setProperty ("style", juce::String (g.style)); go->setProperty ("preset", juce::String (g.preset)); go->setProperty ("rate", juce::String (g.rate));
    go->setProperty ("bpm", g.bpm); go->setProperty ("master", g.master); go->setProperty ("oct", g.oct); go->setProperty ("pedal", g.pedal);
    go->setProperty ("arpSeed", (juce::int64) g.arpSeed); go->setProperty ("progSeed", (juce::int64) g.progSeed);
    go->setProperty ("prog", progToVar (g.prog));
    o->setProperty ("gen", juce::var (go));
    o->setProperty ("simple", I.simple); o->setProperty ("simpleProg", progToVar (I.simpleProg));
    o->setProperty ("chordBars", I.chordBars); o->setProperty ("voicing", I.voicing); o->setProperty ("chordRhythm", I.chordRhythm);
    juce::Array<juce::var> fig;
    for (int i = 0; i < I.fig.len; ++i) { const auto& s = I.fig.s[(size_t) i]; juce::Array<juce::var> e; e.add (s.idx); e.add ((int) s.pedal); e.add ((int) s.rest); e.add ((int) s.acc); e.add ((int) s.tie); e.add (s.oct); e.add ((int) s.hold); fig.add (e); }
    o->setProperty ("fig", fig); o->setProperty ("figRule", I.fig.rule); o->setProperty ("pedalPc", I.pedalPc);
    o->setProperty ("figRecipe", I.fig.recipe); o->setProperty ("figFloor", I.fig.floorOff); o->setProperty ("figResync", I.fig.resync); o->setProperty ("figPedal", juce::String (I.fig.pedalDeg));
    {
        auto figVar = [] (const Figure& f) { juce::Array<juce::var> a; for (int i = 0; i < f.len; ++i) { const auto& s = f.s[(size_t) i]; juce::Array<juce::var> e; e.add (s.idx); e.add ((int) s.pedal); e.add ((int) s.rest); e.add ((int) s.acc); e.add ((int) s.tie); e.add (s.oct); e.add ((int) s.hold); a.add (e); } return juce::var (a); };
        juce::Array<juce::var> evs;
        for (auto& e : I.figEvents) { auto* eo = new juce::DynamicObject(); eo->setProperty ("bar", e.fromBar); eo->setProperty ("pedal", juce::String (e.pedalDeg)); if (e.hasFig) eo->setProperty ("fig", figVar (e.fig)); evs.add (juce::var (eo)); }
        o->setProperty ("figEvents", evs);
    }
    o->setProperty ("arpSpb", I.arpSpb); o->setProperty ("arpRange", I.arpRange); o->setProperty ("cycle", I.cycle); o->setProperty ("resync", I.resync); o->setProperty ("holdPedal", I.holdPedal); o->setProperty ("arpGate", I.arpGate);
    o->setProperty ("melSpb", I.melSpb); o->setProperty ("melRange", I.melRange); o->setProperty ("form", I.form); o->setProperty ("phraseBars", I.phraseBars); o->setProperty ("stagger", I.stagger); o->setProperty ("stutter", I.stutter);
    o->setProperty ("bassRate", I.bassRate); o->setProperty ("master", I.master); o->setProperty ("groove", I.groove); o->setProperty ("evolve", I.evolve); o->setProperty ("evolveBars", I.evolveBars); o->setProperty ("reg", I.reg);
    o->setProperty ("style", I.style); o->setProperty ("candy", I.candy); o->setProperty ("tension", I.tension); o->setProperty ("evolveMode", I.evolveMode); if (I.songBars > 0) o->setProperty ("songBars", I.songBars);
    if (I.keyShift != 0) o->setProperty ("keyShift", I.keyShift);
    if (I.chordOct != 0) o->setProperty ("chordOct", I.chordOct);   // (only when used: older files stay byte-identical)
    if (! I.energy.empty()) { juce::Array<juce::var> e; for (float v : I.energy) e.add ((double) v); o->setProperty ("energy", e); }
    if (I.arpMeter > 0) o->setProperty ("arpMeter", I.arpMeter);
    if (I.arpEuclid > 0) { o->setProperty ("arpEuclid", I.arpEuclid); o->setProperty ("arpEuclidRot", I.arpEuclidRot); }
    o->setProperty ("arpDir", I.arpDir); o->setProperty ("arpContour", I.arpContour); o->setProperty ("arpAccent", I.arpAccent);
    o->setProperty ("motif", notesToVar (I.motif)); o->setProperty ("motifT0", I.motifT0); o->setProperty ("motifLen", I.motifLen);
    if (urbanMood (I.mode))
    {   // TRAP / HIP HOP: the flavour + what the engines picked (only written for these moods: older files stay byte-identical)
        o->setProperty ("flavour", I.flavour); o->setProperty ("flavourUsed", I.flavourUsed); o->setProperty ("colour", juce::String (I.colour)); o->setProperty ("doubleTime", I.doubleTime);
        o->setProperty ("darkness", I.darkness);
        o->setProperty ("urbanProg", I.urbanProg); o->setProperty ("urbanBass", I.urbanBassArch); o->setProperty ("urbanMel", I.urbanMelArch);
    }
    if (I.melVer > 0) o->setProperty ("melVer", I.melVer);   // (only written when it is the new writer: older files stay byte-identical)
    {   // LANE MOOD (only written when used: old files stay byte-identical)
        bool used = false; for (int l = 0; l < 4; ++l) used |= I.laneMood[(size_t) l] >= 0 || I.laneVar[(size_t) l] != 0;
        if (used) { juce::StringArray a; for (int l = 0; l < 4; ++l) a.add (juce::String (I.laneMood[(size_t) l]) + ":" + juce::String (I.laneVar[(size_t) l])); o->setProperty ("laneMood", a.joinIntoString (" ")); }
    }
    if (I.mutant.active || I.mutant.cell.valid())
    {   // MUTANT ARP (only written when used: older files stay byte-identical)
        const auto& Mu = I.mutant; const auto& P = Mu.P;
        auto* mo = new juce::DynamicObject();
        mo->setProperty ("active", Mu.active); mo->setProperty ("generation", Mu.generation); mo->setProperty ("harm", juce::String (Mu.harm));
        auto* po = new juce::DynamicObject();
        po->setProperty ("mode", P.mode); po->setProperty ("seed", (juce::int64) P.seed); po->setProperty ("div", P.div); po->setProperty ("cellSteps", P.cellSteps);
        po->setProperty ("density", P.density); po->setProperty ("octaveJump", P.octaveJump); po->setProperty ("intervalChaos", P.intervalChaos); po->setProperty ("retrigger", P.retrigger); po->setProperty ("stepSkip", P.stepSkip);
        po->setProperty ("velRandom", P.velRandom); po->setProperty ("gate", P.gate); po->setProperty ("swing", P.swing); po->setProperty ("evolve", P.evolve); po->setProperty ("glide", P.glide);
        po->setProperty ("direction", P.direction); po->setProperty ("rotate", P.rotate); po->setProperty ("scaleLock", P.scaleLock); po->setProperty ("chordFollow", P.chordFollow);
        po->setProperty ("evolveBars", P.evolveBars); po->setProperty ("variationBars", P.variationBars); po->setProperty ("reg", P.reg);
        po->setProperty ("twoLine", P.twoLine); po->setProperty ("shadowSteps", P.shadowSteps); po->setProperty ("lineBReg", P.lineBReg);
        mo->setProperty ("params", juce::var (po));
        auto cellVar = [] (const MutantCell& c) { juce::Array<juce::var> a; for (auto& st : c.s) { juce::Array<juce::var> e; e.add (st.idx); e.add (st.oct); e.add (st.col); e.add (st.role); e.add ((int) st.rest); e.add ((int) st.hold); e.add ((int) st.acc); e.add ((int) st.lock); e.add ((int) st.glide); e.add (st.ratchet); e.add (st.prob); e.add (st.gate); e.add (st.vel); e.add ((int) st.grace); e.add ((int) st.trill); e.add (st.sus); a.add (e); } return juce::var (a); };
        mo->setProperty ("cell", cellVar (Mu.cell)); if (Mu.cellB.valid()) mo->setProperty ("cellB", cellVar (Mu.cellB));
        if (Mu.hasClassic) mo->setProperty ("classic", notesToVar (Mu.classic));
        o->setProperty ("mutant", juce::var (mo));
    }
    juce::Array<juce::var> lanes;
    for (auto& L : I.lanes)
    {
        auto* lo = new juce::DynamicObject();
        lo->setProperty ("notes", notesToVar (L.notes)); lo->setProperty ("edited", L.edited); lo->setProperty ("mute", L.mute);
        juce::String locks; for (bool b : L.barLocks) locks << (b ? "1" : "0"); lo->setProperty ("locks", locks);
        lanes.add (juce::var (lo));
    }
    o->setProperty ("lanes", lanes);
    return juce::var (o);
}

void ArpProcessor::ideaFromVar (const juce::var& v, Idea& I)
{
    if (! v.isObject()) return;
    auto gi = [&] (const juce::var& o, const char* k, int d) { return o.hasProperty (k) ? (int) o[k] : d; };
    auto gd = [&] (const juce::var& o, const char* k, double d) { return o.hasProperty (k) ? (double) o[k] : d; };
    auto gb = [&] (const juce::var& o, const char* k, bool d) { return o.hasProperty (k) ? (bool) o[k] : d; };
    I.mode = juce::jlimit (0, kMoods - 1, gi (v, "mode", Midnight)); I.words = v["words"].toString().toStdString(); I.seed = (uint32_t) (juce::int64) v["seed"];
    I.prompt = v["prompt"].toString().toStdString(); I.seedUsed = (uint32_t) (juce::int64) v["seedUsed"];
    const auto go = v["gen"];
    if (go.isObject())
    {
        Gen& g = I.g;
        g.genre = go["genre"].toString().toStdString(); g.moods.clear();
        for (auto& m : juce::StringArray::fromTokens (go["moods"].toString(), ",", "")) if (m.isNotEmpty()) g.moods.push_back (m.toStdString());
        g.key = juce::jlimit (0, 11, gi (go, "key", 9)); g.scale = go["scale"].toString().toStdString(); if (g.scale.empty()) g.scale = "minor";
        g.bars = gi (go, "bars", 4); g.colour = gd (go, "colour", 0.4); g.style = go["style"].toString().toStdString(); g.preset = go["preset"].toString().toStdString();
        g.rate = go["rate"].toString().toStdString(); g.bpm = juce::jlimit (40, 240, gi (go, "bpm", 124)); g.master = gd (go, "master", 0.5); g.oct = gi (go, "oct", 2); g.pedal = gb (go, "pedal", false);
        g.arpSeed = (uint32_t) (juce::int64) go["arpSeed"]; g.progSeed = (uint32_t) (juce::int64) go["progSeed"];
        g.prog = progFromVar (go["prog"]);
        if (g.prog.empty()) { Slot s; s.tok = "i"; g.prog.push_back (s); }
    }
    I.flavour = juce::jlimit (-1, flavourCount (I.mode) - 1, gi (v, "flavour", -1)); I.flavourUsed = juce::jlimit (0, flavourCount (I.mode) - 1, gi (v, "flavourUsed", 0)); I.colour = v["colour"].toString().toStdString(); I.doubleTime = gb (v, "doubleTime", false);
    I.darkness = juce::jlimit (-1, 100, gi (v, "darkness", -1));
    I.urbanProg = gi (v, "urbanProg", -1); I.urbanBassArch = gi (v, "urbanBass", -1); I.urbanMelArch = gi (v, "urbanMel", -1);
    I.melVer = juce::jlimit (0, kMelVerCurrent, gi (v, "melVer", 0));   // absent = a session from before 3.2.1: the shipped writer, exactly
    I.simple = gb (v, "simple", I.simple);   // ideas saved before the switch keep the current setting
    I.simpleProg = progFromVar (v["simpleProg"]);
    I.chordBars = juce::jlimit (1, 2, gi (v, "chordBars", 1)); I.voicing = juce::jlimit (0, 3, gi (v, "voicing", 0)); I.chordRhythm = juce::jlimit (0, 2, gi (v, "chordRhythm", 0));
    if (auto* fa = v["fig"].getArray())
    {
        I.fig.len = juce::jlimit (1, 8, fa->size());
        for (int i = 0; i < I.fig.len; ++i)
            if (auto* e = (*fa)[i].getArray(); e != nullptr && e->size() >= 6)
            { auto& s = I.fig.s[(size_t) i]; s.idx = juce::jlimit (1, 24, (int) (*e)[0]); s.pedal = (int) (*e)[1] != 0; s.rest = (int) (*e)[2] != 0; s.acc = (int) (*e)[3] != 0; s.tie = (int) (*e)[4] != 0; s.oct = juce::jlimit (-2, 2, (int) (*e)[5]); }
    }
    I.fig.rule = gi (v, "figRule", 0); I.pedalPc = gi (v, "pedalPc", 0);
    I.fig.recipe = gb (v, "figRecipe", false); I.fig.floorOff = juce::jlimit (-12, 12, gi (v, "figFloor", 0)); I.fig.resync = juce::jlimit (1, 4, gi (v, "figResync", 1)); I.fig.pedalDeg = v["figPedal"].toString().toStdString();
    if (auto* fa = v["fig"].getArray()) for (int i = 0; i < I.fig.len && i < fa->size(); ++i) if (auto* e = (*fa)[i].getArray(); e != nullptr && e->size() >= 7) I.fig.s[(size_t) i].hold = (int) (*e)[6] != 0;
    I.figEvents.clear();
    if (auto* ea = v["figEvents"].getArray())
        for (auto& eo : *ea)
        {
            FigEvent e; e.fromBar = juce::jlimit (0, 64, gi (eo, "bar", 0)); e.pedalDeg = eo["pedal"].toString().toStdString();
            if (auto* fa = eo["fig"].getArray())
            {
                e.hasFig = true; e.fig = I.fig; e.fig.len = juce::jlimit (1, 8, fa->size());
                for (int i = 0; i < e.fig.len; ++i) if (auto* x = (*fa)[i].getArray(); x != nullptr && x->size() >= 7)
                { auto& s = e.fig.s[(size_t) i]; s.idx = juce::jlimit (1, 24, (int) (*x)[0]); s.pedal = (int) (*x)[1] != 0; s.rest = (int) (*x)[2] != 0; s.acc = (int) (*x)[3] != 0; s.tie = (int) (*x)[4] != 0; s.oct = juce::jlimit (-2, 2, (int) (*x)[5]); s.hold = (int) (*x)[6] != 0; }
            }
            I.figEvents.push_back (e);
        }
    I.arpSpb = gi (v, "arpSpb", 16); if (I.arpSpb != 8 && I.arpSpb != 16 && I.arpSpb != 24 && I.arpSpb != 32) I.arpSpb = 16;
    I.arpRange = juce::jlimit (1, 3, gi (v, "arpRange", 2)); I.cycle = juce::jlimit (0, 8, gi (v, "cycle", 0)); I.resync = juce::jlimit (-1, 8, gi (v, "resync", 0));
    I.holdPedal = gb (v, "holdPedal", true); I.arpGate = juce::jlimit (0.1, 1.0, gd (v, "arpGate", 0.45));
    I.melSpb = gi (v, "melSpb", 16); if (I.melSpb != 8 && I.melSpb != 16) I.melSpb = 16;
    I.melRange = juce::jlimit (1, 3, gi (v, "melRange", 2)); I.form = juce::jlimit (0, 3, gi (v, "form", 0)); I.phraseBars = juce::jlimit (0, 16, gi (v, "phraseBars", 0));
    I.stagger = juce::jlimit (0, BAR, gi (v, "stagger", 0)); I.stutter = juce::jlimit (0.0, 1.0, gd (v, "stutter", 0)); I.bassRate = juce::jlimit (0, 2, gi (v, "bassRate", 0));
    I.master = juce::jlimit (0.0, 1.0, gd (v, "master", 0.5)); I.groove = juce::jlimit (0.0, 1.0, gd (v, "groove", 0)); I.evolve = gb (v, "evolve", false);
    I.evolveBars = gi (v, "evolveBars", 16) >= 32 ? 32 : 16; I.reg = juce::jlimit (-2, 2, gi (v, "reg", 0));
    I.style = juce::jlimit (0, kStyles - 1, gi (v, "style", 0)); I.candy = juce::jlimit (0, kCandies - 1, gi (v, "candy", 0));
    I.tension = v.hasProperty ("tension") ? juce::jlimit (-1.0, 1.0, (double) v["tension"]) : -1.0; I.evolveMode = juce::jlimit (0, kEvolveModes - 1, gi (v, "evolveMode", 0)); I.songBars = juce::jlimit (0, 64, gi (v, "songBars", 0)); I.keyShift = juce::jlimit (-48, 48, gi (v, "keyShift", 0));
    I.chordOct = juce::jlimit (-2, 2, gi (v, "chordOct", 0));
    I.energy.clear(); if (auto* ea = v["energy"].getArray()) for (auto& e : *ea) if (I.energy.size() < 64) I.energy.push_back ((float) juce::jlimit (0.0, 1.0, (double) e));
    I.arpMeter = juce::jlimit (0, kMeters - 1, gi (v, "arpMeter", 0)); I.arpEuclid = juce::jlimit (0, 32, gi (v, "arpEuclid", 0)); I.arpEuclidRot = juce::jmax (0, gi (v, "arpEuclidRot", 0));
    I.arpDir = juce::jlimit (0, kArpDirs - 1, gi (v, "arpDir", 0)); I.arpContour = juce::jlimit (0, kArpContours - 1, gi (v, "arpContour", 0)); I.arpAccent = juce::jlimit (0, kArpAccents - 1, gi (v, "arpAccent", 0));
    I.motif = notesFromVar (v["motif"]); I.motifT0 = juce::jmax (0, gi (v, "motifT0", 0)); I.motifLen = juce::jmax (0, gi (v, "motifLen", 0));
    {   // LANE MOOD (absent = every lane follows the idea)
        auto a = juce::StringArray::fromTokens (v["laneMood"].toString(), " ", ""); a.removeEmptyStrings();
        for (int l = 0; l < 4; ++l) { I.laneMood[(size_t) l] = l < a.size() ? juce::jlimit (-1, kMoods - 1, a[l].upToFirstOccurrenceOf (":", false, false).getIntValue()) : -1;
                                      I.laneVar[(size_t) l] = l < a.size() ? juce::jlimit (0, 9999, a[l].fromFirstOccurrenceOf (":", false, false).getIntValue()) : 0; }
    }
    I.mutant = MutantState {};
    if (const auto mo = v["mutant"]; mo.isObject())
    {   // MUTANT ARP
        auto& Mu = I.mutant; auto& P = Mu.P;
        Mu.active = gb (mo, "active", false); Mu.generation = gi (mo, "generation", 0); Mu.harm = mo["harm"].toString().toStdString();
        const auto po = mo["params"];
        if (po.isObject())
        {
            P.mode = juce::jlimit (0, kMutantModes - 1, gi (po, "mode", 0)); P.seed = (uint32_t) (juce::int64) po["seed"]; P.div = gi (po, "div", 16); if (P.div != 4 && P.div != 8 && P.div != 16 && P.div != 32 && P.div != 64) P.div = 16;
            P.cellSteps = juce::jlimit (0, 64, gi (po, "cellSteps", 0));
            P.density = juce::jlimit (0.0, 1.0, gd (po, "density", 0.5)); P.octaveJump = juce::jlimit (0.0, 1.0, gd (po, "octaveJump", 0.5)); P.intervalChaos = juce::jlimit (0.0, 1.0, gd (po, "intervalChaos", 0.3));
            P.retrigger = juce::jlimit (0.0, 1.0, gd (po, "retrigger", 0.3)); P.stepSkip = juce::jlimit (0.0, 1.0, gd (po, "stepSkip", 0.0)); P.velRandom = juce::jlimit (0.0, 1.0, gd (po, "velRandom", 0.3));
            P.gate = juce::jlimit (0.0, 1.0, gd (po, "gate", 0.5)); P.swing = juce::jlimit (0.0, 1.0, gd (po, "swing", 0.0)); P.evolve = juce::jlimit (0.0, 1.0, gd (po, "evolve", 0.4)); P.glide = juce::jlimit (0.0, 1.0, gd (po, "glide", 0.0));
            P.direction = juce::jlimit (0, 2, gi (po, "direction", 0)); P.rotate = juce::jlimit (0, 63, gi (po, "rotate", 0)); P.scaleLock = gb (po, "scaleLock", true); P.chordFollow = gb (po, "chordFollow", true);
            P.evolveBars = gi (po, "evolveBars", 0); if (P.evolveBars != 0 && P.evolveBars != 4 && P.evolveBars != 8 && P.evolveBars != 16) P.evolveBars = 0;
            P.variationBars = juce::jlimit (0, 8, gi (po, "variationBars", 0)); P.reg = juce::jlimit (-2, 2, gi (po, "reg", 0));
            P.twoLine = juce::jlimit (0, kTwoLines - 1, gi (po, "twoLine", 0)); P.shadowSteps = juce::jlimit (1, 16, gi (po, "shadowSteps", 3)); P.lineBReg = juce::jlimit (-2, 2, gi (po, "lineBReg", -1));
        }
        auto cellFrom = [&] (const juce::var& a) { MutantCell c; c.len = 0; if (auto* arr = a.getArray()) for (auto& e : *arr) if (auto* x = e.getArray(); x != nullptr && x->size() >= 13) { MutantStep st; st.idx = juce::jlimit (0, 7, (int) (*x)[0]); st.oct = juce::jlimit (0, 5, (int) (*x)[1]); st.col = juce::jlimit (-2, 2, (int) (*x)[2]); st.role = juce::jlimit (0, 2, (int) (*x)[3]); st.rest = (int) (*x)[4] != 0; st.hold = (int) (*x)[5] != 0; st.acc = (int) (*x)[6] != 0; st.lock = (int) (*x)[7] != 0; st.glide = (int) (*x)[8] != 0; st.ratchet = juce::jlimit (1, 4, (int) (*x)[9]); st.prob = juce::jlimit (0.0, 1.0, (double) (*x)[10]); st.gate = juce::jlimit (0.05, 2.0, (double) (*x)[11]); st.vel = juce::jlimit (0.05, 1.0, (double) (*x)[12]); if (x->size() >= 15) { st.grace = (int) (*x)[13] != 0; st.trill = (int) (*x)[14] != 0; } if (x->size() >= 16) st.sus = juce::jlimit (1, 64, (int) (*x)[15]); c.s.push_back (st); } c.len = (int) c.s.size(); return c; };
        Mu.cell = cellFrom (mo["cell"]); Mu.cellB = cellFrom (mo["cellB"]);
        Mu.hasClassic = mo.hasProperty ("classic"); Mu.classic = Mu.hasClassic ? notesFromVar (mo["classic"]) : std::vector<Note> {};
        if (! Mu.cell.valid() || Mu.cell.len < 1) Mu.active = false;
    }
    I.chordSig = 0; I.lanes[LaneChords].notes.clear(); I.lanes[LaneChords].edited = false;   // (a loaded idea's chords are its own: no rewrite against the one before)
    rebuildChords (I);
    if (auto* la = v["lanes"].getArray())
        for (int l = 0; l < kLanes && l < la->size(); ++l)
        {
            const auto& lo = (*la)[l]; auto& L = I.lanes[l];
            L.notes = notesFromVar (lo["notes"]); L.edited = gb (lo, "edited", false); L.mute = gb (lo, "mute", false);
            const auto locks = lo["locks"].toString();
            for (int b = 0; b < (int) L.barLocks.size(); ++b) L.barLocks[(size_t) b] = b < locks.length() && locks[b] == '1';
        }
    if (chordsEdited (I)) applyChordOct (I);   // EDITED CHORDS: the octave as far as the edited notes allow
}

void ArpProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::ValueTree root ("ARPEZATOR_STATE");
    root.setProperty ("version", 1, nullptr);
    { const juce::ScopedLock sl (ideaLock); root.setProperty ("idea", juce::JSON::toString (ideaToVar (I), true), nullptr); }
    root.setProperty ("theme_name", ghostLook() || isFace (theme) ? theme : ghosty::themes::toString (themeId()), nullptr);   // (GHOST: older versions read the unknown name as BLACK)
    root.setProperty ("theme", themeId() == ghosty::themes::Theme::Light ? "light" : "dark", nullptr);   // (older versions read this one)
    root.setProperty ("hintsSeen", hintsSeen, nullptr); root.setProperty ("selLane", selLane, nullptr); root.setProperty ("oneTrack", oneTrackDrag, nullptr);
    root.setProperty ("play", internalPlay.load(), nullptr);
    if (transposeMask.load() != 15) root.setProperty ("trMask", transposeMask.load(), nullptr);
    if (syncOwn()) root.setProperty ("syncMode", 1, nullptr);
    if (tempoMul.load() != ghosty::tempomul::Normal) root.setProperty (ghosty::tempomul::stateKey, tempoMul.load(), nullptr);   // TEMPO (per project)
    for (int k = 0; k < kMoods; ++k) if (sections[(size_t) k].valid()) root.setProperty ("section" + juce::String (k), juce::String (sections[(size_t) k].start) + "," + juce::String (sections[(size_t) k].end) + "," + (sections[(size_t) k].on ? "1" : "0"), nullptr);
    if (drawSection.load() == 1) root.setProperty ("dragSection", 1, nullptr);
    if (! linkAll) root.setProperty ("linkAll", false, nullptr);
    if (pushFurther) root.setProperty ("pushFurther", true, nullptr);
    if (sampleFlac.getSize() > 0)
    {   // YOUR SAMPLE: the FLAC made at import + its settings + the source path as a hint
        juce::ValueTree s ("SAMPLE");
        s.setProperty ("flac", sampleFlac.toBase64Encoding(), nullptr); s.setProperty ("name", sampleInfo.name, nullptr); s.setProperty ("path", sampleInfo.path, nullptr);
        const auto& q = smpP;
        s.setProperty ("root", q.root, nullptr); s.setProperty ("fine", q.fine, nullptr); s.setProperty ("mode", q.mode, nullptr); s.setProperty ("reverse", q.reverse, nullptr); s.setProperty ("formant", q.formant, nullptr);
        s.setProperty ("start", q.start, nullptr); s.setProperty ("end", q.end, nullptr); s.setProperty ("loopStart", q.loopStart, nullptr); s.setProperty ("loopEnd", q.loopEnd, nullptr); s.setProperty ("xfade", q.xfade, nullptr);
        s.setProperty ("a", q.a, nullptr); s.setProperty ("d", q.d, nullptr); s.setProperty ("s", q.s, nullptr); s.setProperty ("r", q.r, nullptr);
        s.setProperty ("cutoff", q.cutoff, nullptr); s.setProperty ("tone", q.tone, nullptr); s.setProperty ("vol", q.volDb, nullptr);
        s.setProperty ("fitStretch", q.fitStretch, nullptr); s.setProperty ("fitLoop", q.fitLoop, nullptr); s.setProperty ("fitFade", q.fitFade, nullptr);
        s.setProperty ("sliceOrder", q.sliceOrder, nullptr); s.setProperty ("slicePitched", q.slicePitched, nullptr);
        { juce::StringArray sl; for (int k = 0; k < q.nSlices; ++k) sl.add (juce::String (q.slices[(size_t) k], 6)); s.setProperty ("slices", sl.joinIntoString (" "), nullptr); }
        root.appendChild (s, nullptr);
    }
    if (ownTempoSet) root.setProperty ("ownBpm", sync.getOwnBpm(), nullptr);
    {   // LANE FX: only written when something is on (old projects stay as they were)
        bool any = false; juce::StringArray fx; for (auto& v : laneFx) { any |= v.load() != 0.0f; fx.add (juce::String (v.load(), 4)); }
        if (any) root.setProperty ("laneFx", fx.joinIntoString (" "), nullptr);
    }
    { const juce::ScopedLock sl (ideaLock); root.setProperty ("lab", juce::JSON::toString (labToVar (lab), true), nullptr); }
    if (tasteLoaded) root.setProperty ("taste", juce::String (weightsToString (taste)), nullptr);   // a copy (the file is shared by every instance)
    { const juce::ScopedLock sl (ideaLock); for (int k = 0; k < kMoods; ++k) if (k < 3 || modeStash[(size_t) k].isNotEmpty()) root.setProperty ("mode" + juce::String (k), modeStash[(size_t) k], nullptr); }   // (TRAP / HIP HOP only when used)
    root.appendChild (apvts.copyState(), nullptr);
    if (auto xml = root.createXml()) copyXmlToBinary (*xml, dest);
}

void ArpProcessor::setLaneMood (int lane, int mood)
{
    if (lane != LaneArp && lane != LaneMelody) return;
    change ([lane, mood] (Idea& X) { X.laneMood[(size_t) lane] = juce::jlimit (-1, kMoods - 1, mood); composeLane (X, lane, true); }, true, "lane mood");
}
void ArpProcessor::newLaneVariation (int lane)
{
    if (lane != LaneArp && lane != LaneMelody) return;
    change ([lane] (Idea& X) { if (lane == LaneMelody) X.melVer = newIdeaMelVer(); X.laneVar[(size_t) lane] = X.laneVar[(size_t) lane] % 9999 + 1; composeLane (X, lane, true); }, true, "lane variation");   // (a NEW melody take is a new generation: the current writer)
}
bool ArpProcessor::decodeSampleBuffer (juce::AudioBuffer<float>& out, double& rate) const
{
    if (sampleFlac.getSize() == 0) return false;
    juce::FlacAudioFormat flac;
    std::unique_ptr<juce::AudioFormatReader> rd (flac.createReaderFor (new juce::MemoryInputStream (sampleFlac, false), true));
    if (rd == nullptr || rd->lengthInSamples < 2) return false;
    const int ch = (int) juce::jlimit (1u, 2u, rd->numChannels);
    out.setSize (ch, (int) rd->lengthInSamples); rd->read (&out, 0, out.getNumSamples(), 0, true, ch > 1); rate = rd->sampleRate;
    return true;
}
int ArpProcessor::sliceSample (float sensitivity)
{
    juce::AudioBuffer<float> b; double rate = 48000; if (! decodeSampleBuffer (b, rate)) return 0;
    auto q = smpP; const int n = b.getNumSamples();
    const int s0 = juce::jlimit (0, n - 2, (int) (q.start * n)), s1 = juce::jlimit (s0 + 2, n, (int) (q.end * n));
    std::vector<float> mono ((size_t) (s1 - s0), 0.0f);
    for (int c = 0; c < b.getNumChannels(); ++c) for (int i = s0; i < s1; ++i) mono[(size_t) (i - s0)] += b.getSample (c, i) / (float) b.getNumChannels();
    if (q.reverse) std::reverse (mono.begin(), mono.end());   // (REVERSE: slices of what you hear)
    const auto st = arpsmp::chop (mono, rate, juce::jlimit (0.0f, 1.0f, sensitivity), 4, (int) q.slices.size());
    q.nSlices = 0;
    for (int p : st) if (q.nSlices < (int) q.slices.size()) { const int at = q.reverse ? (int) mono.size() - 1 - p : p; q.slices[(size_t) q.nSlices++] = (float) (s0 + at) / (float) n; }
    if (q.reverse) std::sort (q.slices.begin(), q.slices.begin() + q.nSlices);
    q.mode = 3; setSampleParams (q);
    return q.nSlices;
}
bool ArpProcessor::slicePattern (int lane)
{
    const auto q = smpP; if (q.nSlices < 1 || ! hasSample() || lane < 0 || lane >= kLanes || lane == LaneChords) return false;
    const double tps = effectiveBpm() / 60.0 * PPQ, lenSec = sampleInfo.len / sampleInfo.rate;
    const int grid = PPQ / 4;   // a 16th
    std::vector<int> st;
    for (int k = 0; k < q.nSlices; ++k)
    {
        const double sec = (q.slices[(size_t) k] - q.start) * lenSec; const int t = (int) std::lround (sec * tps / grid) * grid;
        if (st.empty() || t > st.back()) st.push_back (juce::jmax (0, t));
    }
    const int region = juce::jmax (BAR / 4, (int) std::lround ((q.end - q.start) * lenSec * tps / grid) * grid);
    change ([&] (Idea& X) {
        auto& L = X.lanes[lane].notes; const int pitch = L.empty() ? 60 : L.front().p; L.clear();
        for (int rep = 0; rep * region < X.total; ++rep)
            for (size_t k = 0; k < st.size(); ++k)
            {
                const int t = rep * region + st[k]; if (t >= X.total || st[k] >= region) break;
                const int nx = k + 1 < st.size() && st[k + 1] < region ? rep * region + st[k + 1] : (rep + 1) * region;
                Note nt; nt.t = t; nt.d = juce::jmax (grid / 2, juce::jmin (nx, X.total) - t); nt.p = pitch; nt.v = k == 0 ? 0.9f : 0.75f; L.push_back (nt);
            }
    }, true, "slice pattern");
    return true;
}

juce::String ArpProcessor::themeFromState (const juce::ValueTree& root)
{
    // 2026-11-13: the old looks (BLACK / PURPLE / LIGHT / GHOST) are gone from the menu: a project saved with one opens on AUTO (a face per mood)
    if (root.hasProperty ("theme_name")) { const auto n = root.getProperty ("theme_name").toString().trim().toLowerCase(); if (isFace (n)) return n; }
    return "auto";
}

void ArpProcessor::setStateInformation (const void* data, int size)
{
    ghosty::StateLoadNotify notifyHost (*this);   // CLAP: tell the host the values changed (Shared/HostNotify.h)
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr) return;
    auto root = juce::ValueTree::fromXml (*xml);
    if (! root.hasType ("ARPEZATOR_STATE")) return;
    theme = themeFromState (root); hintsSeen = (bool) root.getProperty ("hintsSeen", false);
    transposeMask = juce::jlimit (0, 15, (int) root.getProperty ("trMask", 15));
    {   // LANE FX (absent = all off)
        auto fx = juce::StringArray::fromTokens (root.getProperty ("laneFx").toString(), " ", ""); fx.removeEmptyStrings();
        for (size_t k = 0; k < laneFx.size(); ++k) laneFx[k] = k < (size_t) fx.size() ? juce::jlimit (-1.0f, 1.0f, fx[(int) k].getFloatValue()) : 0.0f;
    }
    ownTempoSet = root.hasProperty ("ownBpm"); sync.setOwnBpm (ownTempoSet ? (double) root.getProperty ("ownBpm") : 120.0);
    sync.setMode ((int) root.getProperty ("syncMode", 0) == 1 ? ghosty::SyncTransport::Mode::Own : ghosty::SyncTransport::Mode::Daw);
    tempoMul.store (ghosty::tempomul::clampCode ((int) root.getProperty (ghosty::tempomul::stateKey, (int) ghosty::tempomul::Normal)));   // TEMPO (old projects: 1x)
    linkAll = (bool) root.getProperty ("linkAll", true);
    pushFurther = (bool) root.getProperty ("pushFurther", false);
    for (int k = 0; k < kMoods; ++k)
    {   // LOOP SECTION braces (one per mode)
        juce::StringArray a; a.addTokens (root.getProperty ("section" + juce::String (k), "").toString(), ",", "");
        sections[(size_t) k] = a.size() == 3 ? Section { a[0].getIntValue(), a[1].getIntValue(), a[2] == "1" } : Section {};
    }
    drawSection = (int) root.getProperty ("dragSection", 0);
    {
        const auto s = root.getChildWithName ("SAMPLE");
        juce::MemoryBlock mb;
        if (s.isValid() && mb.fromBase64Encoding (s.getProperty ("flac").toString()) && restoreSampleFlac (mb, s.getProperty ("name").toString(), s.getProperty ("path").toString()))
        {
            arpsmp::Params q = smpP;
            q.root = (int) s.getProperty ("root", q.root); q.fine = (float) (double) s.getProperty ("fine", 0.0); q.mode = (int) s.getProperty ("mode", 0); q.reverse = (bool) s.getProperty ("reverse", false); q.formant = (bool) s.getProperty ("formant", false);
            q.start = (float) (double) s.getProperty ("start", 0.0); q.end = (float) (double) s.getProperty ("end", 1.0); q.loopStart = (float) (double) s.getProperty ("loopStart", q.loopStart); q.loopEnd = (float) (double) s.getProperty ("loopEnd", q.loopEnd);
            q.xfade = (float) (double) s.getProperty ("xfade", 0.03); q.a = (float) (double) s.getProperty ("a", 0.003); q.d = (float) (double) s.getProperty ("d", 0.5); q.s = (float) (double) s.getProperty ("s", 1.0); q.r = (float) (double) s.getProperty ("r", 0.25);
            q.cutoff = (float) (double) s.getProperty ("cutoff", 1.0); q.tone = (float) (double) s.getProperty ("tone", 0.0); q.volDb = (float) (double) s.getProperty ("vol", 0.0);
            q.fitStretch = (bool) s.getProperty ("fitStretch", false); q.fitLoop = (bool) s.getProperty ("fitLoop", false); q.fitFade = (float) (double) s.getProperty ("fitFade", 0.02);
            q.sliceOrder = (int) s.getProperty ("sliceOrder", 0); q.slicePitched = (bool) s.getProperty ("slicePitched", false);
            { auto sl = juce::StringArray::fromTokens (s.getProperty ("slices").toString(), " ", ""); sl.removeEmptyStrings(); q.nSlices = 0;
              for (auto& t : sl) if (q.nSlices < (int) q.slices.size()) q.slices[(size_t) q.nSlices++] = juce::jlimit (0.0f, 1.0f, t.getFloatValue()); }
            setSampleParams (q);
        }
        else if (sampleInfo.len > 0) clearSample();
    }
    selLane = juce::jlimit (0, kLanes - 1, (int) root.getProperty ("selLane", LaneChords)); oneTrackDrag = (bool) root.getProperty ("oneTrack", false);
    if (! license.isUnlocked()) { ++version; return; }   // demo mode: a reopened project starts at the default idea (unlocking brings the saved one back)
    const auto ideaJson = juce::JSON::parse (root.getProperty ("idea").toString());
    {
        const juce::ScopedLock sl (ideaLock);
        if (ideaJson.isObject()) ideaFromVar (ideaJson, I);
        for (int k = 0; k < kMoods; ++k) modeStash[(size_t) k] = root.getProperty ("mode" + juce::String (k), "").toString();
        modeStash[(size_t) I.mode] = {};
        if (root.hasProperty ("lab")) { labFromVar (juce::JSON::parse (root.getProperty ("lab").toString()), lab); ++labJob; }
        melOverrideOn = false; overrideLen = 0;
    }
    if (root.hasProperty ("taste"))
    {   // the shared file wins; the project's copy fills in when the file is gone (another machine)
        ensureTaste();
        if (taste.keeps == 0) taste = weightsFromString (root.getProperty ("taste").toString().toStdString());
    }
    const auto params = root.getChildWithName (apvts.state.getType());
    if (params.isValid()) apvts.replaceState (upgradePresetChoices (params.createCopy()));
    rebuildPattern(); needFlush = true; ++version;
}

juce::AudioProcessorEditor* ArpProcessor::createEditor() { return new ArpEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ArpProcessor(); }
