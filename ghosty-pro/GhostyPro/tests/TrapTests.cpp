// Ghosty Pro TRAP test harness (off by default; see GHOSTY_BUILD_PRO_TESTS).
//   TrapTests dump <dir>            -> seeded renders + saved states of every factory preset (run on the OLD build: the reference)
//   TrapTests compare <dir> [dir2]  -> the same renders on this build vs the reference: sample-exact, or the same sound within the old build's own run-to-run spread (dir2 = a second old dump)
//   TrapTests v2audit [--csv f] [--fit inc] [--shard k/N] [names]  -> SOUND ENGINE V2 vs CLASSIC for every factory sound (summary + verdict)
//   TrapTests v2render <dir> | classicexact <ref dump> | gvzip | speclist | paramlist  -> SOUND ENGINE V2 (V2Tests.inc); GP_ENGINE=classic = the
//             internal CLASSIC engine for a whole run (compare against an old dump); GP_SHOWN_ONLY=1 = r2audit / cliplist on the BROWSE library
//   TrapTests levels <trims.inc>    -> loudness of every preset, writes trims for the ones without one
//   TrapTests stack | sample | character | tape | brass | state | cpu   -> feature tests
//   TrapTests shots <dir>           -> PNG screenshots of the TRAP page (needs a display / Xvfb)
//   TrapTests demo <dir>            -> 5 demo WAVs
//   TrapTests bench [--check] [--out f] [--quick] [--only s]  -> CPU % of real time at 48 kHz / 128-sample blocks (BenchTests.inc)
// Built with TRAP_REF_ONLY=1 against the pre-TRAP sources it only has dump / levels / list.
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <set>
#include "ProProcessor.h"
#include "ProBank.h"
#include "ProEditor.h"
#include "ProLayout2.h"

static constexpr double SR = 48000.0;
static constexpr int BS = 512;
struct Ev { double t; int note; float vel; bool on; };
static int failures = 0;
// JUCE 8 ignores Random::setSeed on the system Random (jassert + return); assigning a fresh Random over it sets the seed anyway.
// Test harness only: unison phases / noise then repeat run to run, in this build and in the pre-TRAP reference build.
static void seedSystemRandom (juce::int64 seed) { juce::Random::getSystemRandom() = juce::Random (seed); }
#define CHECK(cond, ...) do { const bool ok_ = (cond); printf ("%s  ", ok_ ? "  ok " : "FAIL "); printf (__VA_ARGS__); printf ("\n"); if (! ok_) ++failures; } while (0)

static std::vector<Ev> testPhrase()
{
    std::vector<Ev> e;
    for (int n : { 60, 64, 67, 71 }) { e.push_back ({ 0.0, n, 0.8f, true }); e.push_back ({ 1.6, n, 0, false }); }
    const int bass[] { 36, 36, 43, 36, 39, 41, 43, 46 };
    for (int i = 0; i < 8; ++i) { const double t = 1.8 + i * 0.2; e.push_back ({ t, bass[i], 0.85f, true }); e.push_back ({ t + 0.15, bass[i], 0, false }); }
    return e;
}

static juce::AudioBuffer<float> render (ProProcessor& p, const std::vector<Ev>& evs, double seconds, double bpm = 120.0)
{
    const int total = (int) (seconds * SR);
    juce::AudioBuffer<float> out (2, total); out.clear();
    juce::AudioBuffer<float> blk (2, BS);
    struct Head : juce::AudioPlayHead
    {
        double bpm = 120, pos = 0;
        juce::Optional<PositionInfo> getPosition() const override { PositionInfo i; i.setBpm (bpm); i.setIsPlaying (true); i.setPpqPosition (pos); i.setTimeSignature (TimeSignature { 4, 4 }); return i; }
    } head;
    head.bpm = bpm;
    p.setPlayHead (&head);
    // 100 ms of silence first (the rack's on / mix ramps and the gain ramp settle), and the system random is re-seeded before
    // every block: unison phases / noise then come out the same from any build whose draw order is the same
    const int preBlocks = (int) std::ceil (0.1 * SR / BS);
    int blockIdx = 0;
    for (int k = 0; k < preBlocks; ++k)
    {
        juce::MidiBuffer mb; blk.setSize (2, BS, false, false, true);
        head.pos = -(double) (preBlocks - k) * BS / SR * bpm / 60.0;
        seedSystemRandom (0x5eed + blockIdx++);
        p.processBlock (blk, mb);
    }
    for (int pos = 0; pos < total; pos += BS)
    {
        const int n = juce::jmin (BS, total - pos);
        juce::MidiBuffer mb;
        for (auto& e : evs)
        {
            const int s = (int) (e.t * SR);
            if (s >= pos && s < pos + n) mb.addEvent (e.on ? juce::MidiMessage::noteOn (1, e.note, e.vel) : juce::MidiMessage::noteOff (1, e.note), s - pos);
        }
        head.pos = pos / SR * bpm / 60.0;
        blk.setSize (2, n, false, false, true);
        seedSystemRandom (0x5eed + blockIdx++);
        p.processBlock (blk, mb);
        for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, blk, ch, 0, n);
    }
    p.setPlayHead (nullptr);
    return out;
}

static void pump (int ms = 5) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }
// the wavetables are built on the message thread by a 20 Hz timer: build them now so a render never starts with silent blocks
static void settle (ProProcessor& p) { p.flushPendingWaves(); pump (120); p.flushPendingWaves(); pump (5); }

static std::unique_ptr<ProProcessor> makeProc (int program)
{
    seedSystemRandom (777);
    auto p = std::make_unique<ProProcessor>();
    p->prepareToPlay (SR, BS);
#ifndef TRAP_REF_ONLY
    if (const char* e = std::getenv ("GP_ENGINE")) p->setSoundEngine (juce::String (e) == "classic" ? ProProcessor::engineClassic : ProProcessor::engineV2);   // SOUND ENGINE for the whole run (compare in CLASSIC mode)
#endif
    if (program >= 0) p->setCurrentProgram (program);
    settle (*p);
    return p;
}

static float peakOf (const juce::AudioBuffer<float>& b, int* nans = nullptr)
{
    float pk = 0; int bad = 0;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i) { const float v = b.getSample (ch, i); if (! std::isfinite (v)) ++bad; else pk = std::max (pk, std::abs (v)); }
    if (nans) *nans = bad;
    return pk;
}
static double rmsDb (const juce::AudioBuffer<float>& b, int s = 0, int e = -1)
{
    if (e < 0) e = b.getNumSamples();
    double sum = 0; for (int ch = 0; ch < 2; ++ch) for (int i = s; i < e; ++i) sum += (double) b.getSample (ch, i) * b.getSample (ch, i);
    return 10 * std::log10 (std::max (1e-14, sum / (2.0 * std::max (1, e - s))));
}
static float maxDiff (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    float m = 0; const int n = juce::jmin (a.getNumSamples(), b.getNumSamples());
    for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < n; ++i) m = std::max (m, std::abs (a.getSample (ch, i) - b.getSample (ch, i)));
    return m;
}
// simplified BS.1770 integrated loudness (K-weighting at 48 kHz, 400 ms blocks, absolute + relative gate)
static double lufs (const juce::AudioBuffer<float>& b, int start = 0, int end = -1)
{
    if (end < 0) end = b.getNumSamples();
    const double sb0 = 1.53512485958697, sb1 = -2.69169618940638, sb2 = 1.19839281085285, sa1 = -1.69065929318241, sa2 = 0.73248077421585;
    const double hb1 = -2.0, hb2 = 1.0, ha1 = -1.99004745483398, ha2 = 0.99007225036621;
    std::vector<double> ms; std::vector<std::vector<double>> filt (2);
    for (int ch = 0; ch < 2; ++ch)
    {
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0, z1 = 0, z2 = 0, w1 = 0, w2 = 0;
        auto& f = filt[(size_t) ch]; f.resize ((size_t) (end - start));
        for (int i = start; i < end; ++i)
        {
            const double x = b.getSample (ch, i);
            const double y = sb0 * x + sb1 * x1 + sb2 * x2 - sa1 * y1 - sa2 * y2; x2 = x1; x1 = x; y2 = y1; y1 = y;
            const double z = y + hb1 * z1 + hb2 * z2 - ha1 * w1 - ha2 * w2; z2 = z1; z1 = y; w2 = w1; w1 = z;
            f[(size_t) (i - start)] = z;
        }
    }
    const int blk = (int) (0.4 * SR), hop = (int) (0.1 * SR);
    for (int s = 0; s + blk <= end - start; s += hop)
    {
        double sum = 0;
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < blk; ++i) sum += filt[(size_t) ch][(size_t) (s + i)] * filt[(size_t) ch][(size_t) (s + i)];
        ms.push_back (sum / blk);
    }
    auto L = [] (double m) { return -0.691 + 10.0 * std::log10 (std::max (1e-12, m)); };
    std::vector<double> g1; for (auto m : ms) if (L (m) > -70) g1.push_back (m);
    if (g1.empty()) return -100;
    double mean = 0; for (auto m : g1) mean += m; mean /= (double) g1.size();
    const double rel = L (mean) - 10;
    double m2 = 0; int c = 0; for (auto m : g1) if (L (m) > rel) { m2 += m; ++c; }
    return c ? L (m2 / c) : -100;
}

static void writeWav (const juce::AudioBuffer<float>& b, const juce::File& f)
{
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> os (f.createOutputStream());
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), SR, 2, 24, {}, 0));
    if (w) { os.release(); w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples()); }
}
static void writeRaw (const juce::AudioBuffer<float>& b, const juce::File& f)
{
    f.deleteFile(); juce::FileOutputStream os (f);
    for (int ch = 0; ch < 2; ++ch) os.write (b.getReadPointer (ch), (size_t) b.getNumSamples() * sizeof (float));
}
static juce::AudioBuffer<float> readRaw (const juce::File& f)
{
    juce::MemoryBlock mb; f.loadFileAsData (mb);
    const int n = (int) (mb.getSize() / sizeof (float) / 2);
    juce::AudioBuffer<float> b (2, juce::jmax (0, n));
    for (int ch = 0; ch < 2; ++ch) std::memcpy (b.getWritePointer (ch), (const char*) mb.getData() + (size_t) ch * (size_t) n * sizeof (float), (size_t) n * sizeof (float));
    return b;
}
static juce::String safeName (const juce::String& s) { return juce::File::createLegalFileName (s).replaceCharacter (' ', '_'); }

// Phase-insensitive "same sound" check for renders that can't be sample-identical (unison phases / noise come from the
// system random): 400 ms RMS envelope within envTol dB where either side is above -60 dBFS, and the long-term 1/3-octave
// spectrum (50 Hz .. 16 kHz, bands within 50 dB of the loudest) within specTol dB.
// levelOff = the overall level difference (b re a) that was taken out first: a re-trimmed preset is the same sound, louder.
static bool sameSound (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, double& envErr, double& specErr, double envTol = 2.0, double specTol = 2.5, double* levelOff = nullptr)
{
    envErr = specErr = 0;
    const int n = juce::jmin (a.getNumSamples(), b.getNumSamples()), win = (int) (0.4 * SR);
    const double off = rmsDb (b, 0, n) - rmsDb (a, 0, n);
    if (levelOff) *levelOff = off;
    for (int s = 0; s + win <= n; s += win / 2)
    {
        const double da = rmsDb (a, s, s + win), db = rmsDb (b, s, s + win) - off;
        if (da > -60 || db > -60) envErr = std::max (envErr, std::abs (da - db));
    }
    auto spectrum = [n] (const juce::AudioBuffer<float>& x)
    {
        constexpr int ORDER = 12, N = 1 << ORDER;
        juce::dsp::FFT fft (ORDER);
        std::vector<double> acc ((size_t) N / 2, 0.0); std::vector<float> buf ((size_t) N * 2);
        for (int s = 0; s + N <= n; s += N / 2)
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            for (int i = 0; i < N; ++i) buf[(size_t) i] = (x.getSample (0, s + i) + x.getSample (1, s + i)) * (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / N));
            fft.performFrequencyOnlyForwardTransform (buf.data());
            for (int k = 0; k < N / 2; ++k) acc[(size_t) k] += (double) buf[(size_t) k] * buf[(size_t) k];
        }
        std::vector<double> bands;
        for (double f = 50.0; f < 16000.0; f *= std::pow (2.0, 1.0 / 3.0))
        {
            const int k0 = (int) (f / std::pow (2.0, 1.0 / 6.0) * N / SR), k1 = juce::jmax (k0 + 1, (int) (f * std::pow (2.0, 1.0 / 6.0) * N / SR));
            double e = 0; for (int k = k0; k < k1 && k < N / 2; ++k) e += acc[(size_t) k];
            bands.push_back (10.0 * std::log10 (std::max (1.0e-20, e)));
        }
        return bands;
    };
    const auto sa = spectrum (a), sb = spectrum (b);
    double top = -1000; for (size_t i = 0; i < sa.size(); ++i) top = std::max (top, std::max (sa[i], sb[i]));
    for (size_t i = 0; i < sa.size(); ++i) if (std::max (sa[i], sb[i] - off) > top - 50) specErr = std::max (specErr, std::abs (sa[i] - (sb[i] - off)));
    return envErr <= envTol && specErr <= specTol;
}

// ------------------------------------------------------------------ dump / compare (old build vs this build)
static constexpr double REF_SECS = 3.5;
static juce::AudioBuffer<float> seededRender (ProProcessor& p)
{
    seedSystemRandom (99);
    return render (p, testPhrase(), REF_SECS);
}

static int dump (const juce::File& dir)
{
    dir.createDirectory();
    const auto& bank = gpro::factoryBank();
    juce::StringArray names;
    for (int i = 0; i < (int) bank.size(); ++i)
    {
        auto p = makeProc (i);
        writeRaw (seededRender (*p), dir.getChildFile (safeName (bank[(size_t) i].name) + ".f32"));
        juce::MemoryBlock mb; p->getStateInformation (mb);
        dir.getChildFile (safeName (bank[(size_t) i].name) + ".state").replaceWithData (mb.getData(), mb.getSize());
        names.add (bank[(size_t) i].name);
    }
    dir.getChildFile ("names.txt").replaceWithText (names.joinIntoString ("\n"));
    printf ("dumped %d presets to %s\n", names.size(), dir.getFullPathName().toRawUTF8());
    return 0;
}

// compare <ref> [ref2]: ref2 = a second dump from the SAME old build; unison phases / noise come from the system random, so
// two runs of one build already differ, and that per-preset spread (x1.5 + 0.5 dB) is the bar a preset must stay within.
static int compare (const juce::File& dir, const juce::File& dir2)
{
    const auto& bank = gpro::factoryBank();
    juce::StringArray refNames; dir.getChildFile ("names.txt").readLines (refNames);
    int same = 0, close = 0, sound = 0, bad = 0, missing = 0, stateBad = 0, stateSound = 0;
    for (auto& nm : refNames)
    {
        if (nm.isEmpty()) continue;
        // sounds renamed since the reference build (program numbers unchanged)
        static const std::pair<const char*, const char*> renamed[] { { "Acid Lead 303", "Acid Lead Squelch" }, { "Rhodes Ghost", "Tine Ghost" }, { "Wurli Bark", "Reed Bark" }, { "Clavinet Funk", "Funk Key Bite" },
            { "Reese Garage", "Detune Garage Bass" }, { "Reese Classic", "Detuned Saw Bass" }, { "Neuro Reese", "Neuro Detune Bass" }, { "Acid Square 303", "Acid Square Squelch" }, { "DX Electric Bass", "FM Electric Bass" },
            { "Moog Fat Bass", "Ladder Fat Bass" }, { "Drum and Bass Sub Reese", "Drum and Bass Sub Detune" }, { "Solina Ensemble", "String Machine Ensemble" }, { "Wurly", "Reed Keys" }, { "Clav Funk", "Funk Comb Keys" },
            { "Reese Bass", "Detuned Bass" }, { "Acid 303", "Acid Box" }, { "Moog-ish", "Ladder-ish" } };
        juce::String newName = nm; for (auto& r : renamed) if (nm == r.first) newName = r.second;
        int idx = -1; for (int i = 0; i < (int) bank.size(); ++i) if (bank[(size_t) i].name == newName) idx = i;
        if (idx < 0) { printf ("MISSING in this build: %s\n", nm.toRawUTF8()); ++missing; continue; }
        auto ref = readRaw (dir.getChildFile (safeName (nm) + ".f32"));
        auto p = makeProc (idx);
        auto now = seededRender (*p);
#ifndef TRAP_REF_ONLY
        // this build adds the CLEAN ceiling's fixed latency (reported to the host, which compensates): line the renders up
        const int lat = p->ceilingLatency();
        auto align = [lat] (juce::AudioBuffer<float>& b, bool isNew)
        {
            const int len = b.getNumSamples() - lat;
            juce::AudioBuffer<float> o (2, len);
            for (int ch = 0; ch < 2; ++ch) o.copyFrom (ch, 0, b, ch, isNew ? lat : 0, len);
            b = o;
        };
        align (ref, false); align (now, true);
#endif
        const float pk = std::max (peakOf (ref), 1.0e-4f), d = maxDiff (ref, now);
        const double db = 20.0 * std::log10 (std::max (1.0e-12f, d) / pk);
        // the state the old build saved must load into this build and play the same
        juce::MemoryBlock mb; dir.getChildFile (safeName (nm) + ".state").loadFileAsData (mb);
        auto q = makeProc (0); q->setStateInformation (mb.getData(), (int) mb.getSize()); pump();
        auto viaState = seededRender (*q);
#ifndef TRAP_REF_ONLY
        align (viaState, true);
#endif
        const float ds = maxDiff (ref, viaState);
        const double dbs = 20.0 * std::log10 (std::max (1.0e-12f, ds) / pk);
        double envErr = 0, specErr = 0, envTol = 2.0, specTol = 2.5, selfEnv = 0, selfSpec = 0, off = 0;
        if (dir2.isDirectory() && dir2.getChildFile (safeName (nm) + ".f32").existsAsFile())
        {
            auto ref2 = readRaw (dir2.getChildFile (safeName (nm) + ".f32"));
            sameSound (ref, ref2, selfEnv, selfSpec);
            envTol = std::max (envTol, selfEnv * 1.5 + 0.5); specTol = std::max (specTol, selfSpec * 1.5 + 0.5);
        }
        if (d == 0.0f) ++same; else if (db <= -90.0) ++close;
        else if (sameSound (ref, now, envErr, specErr, envTol, specTol, &off)) { ++sound; printf ("%s %-28s %.1f dB sample diff, same sound: envelope %.2f dB, spectrum %.2f dB, level %+.1f dB (old build vs itself %.2f / %.2f)\n", std::abs (off) > 0.3 ? "trim " : "phase", nm.toRawUTF8(), db, envErr, specErr, off, selfEnv, selfSpec); }
        else { ++bad; printf ("DIFF  %-28s %.1f dB (max %.3g, peak %.3f), envelope %.2f dB, spectrum %.2f dB, level %+.1f dB (old build vs itself %.2f / %.2f)\n", nm.toRawUTF8(), db, d, pk, envErr, specErr, off, selfEnv, selfSpec); }
        if (dbs > -90.0)
        {
            if (sameSound (ref, viaState, envErr, specErr, envTol, specTol)) ++stateSound;
            else { ++stateBad; printf ("STATE %-28s old saved state plays %.1f dB different: envelope %.2f dB, spectrum %.2f dB (old build vs itself %.2f / %.2f)\n", nm.toRawUTF8(), dbs, envErr, specErr, selfEnv, selfSpec); }
        }
    }
    printf ("compare: %d identical, %d within -90 dB, %d same sound (other unison phases), %d DIFFERENT, %d missing; old saved states: %d same sound, %d DIFFERENT\n", same, close, sound, bad, missing, stateSound, stateBad);
    return bad + missing + stateBad;
}

// ------------------------------------------------------------------ levels (new presets get a trim)
static int levels (const juce::String& trimsOut, double target)
{
    const auto& bank = gpro::factoryBank();
    std::set<juce::String> names; int dup = 0;
    for (auto& f : bank) { if (names.count (f.name)) { printf ("DUPLICATE NAME: %s\n", f.name.toRawUTF8()); ++dup; } names.insert (f.name); }
    juce::StringArray trimLines; int flagged = 0, measured = 0;
    for (int i = 0; i < (int) bank.size(); ++i)
    {
        const bool hasTrim = std::abs (gpro::presetTrimDb (bank[(size_t) i].name)) > 1.0e-6f;
        if (hasTrim && trimsOut.isNotEmpty()) continue;    // only the untrimmed (new) ones when writing trims
        auto p = makeProc (i);
        auto buf = render (*p, testPhrase(), 5.0);
        int nans = 0; const float pk = peakOf (buf, &nans);
        const double L = lufs (buf, 0, (int) (4.0 * SR));
        const double tail = rmsDb (buf, (int) (4.5 * SR), (int) (5.0 * SR)), body = rmsDb (buf, 0, (int) (3.5 * SR));
        const double pkDb = juce::Decibels::gainToDecibels (pk, -120.0f);
        const bool limited = bank[(size_t) i].scene == "cleanlimit" || bank[(size_t) i].spec.contains ("fx_lim_on=1");   // the GHOST LIMITER holds the peaks
        const double newTrim = juce::jlimit (-18.0, 14.0, gpro::presetTrimDb (bank[(size_t) i].name) + (limited ? target - L : std::min (target - L, -0.5 - pkDb)));   // the output soft clip starts at -1 dB
        juce::String flags;
        if (nans) flags << " NAN"; if (L < -60) flags << " SILENT"; if (tail > body + 1) flags << " RUNAWAY?"; if (pk > 0.95f) flags << " LIMIT";
        if (hasTrim && std::abs (L - target) > 3.0) flags << " OFF-TARGET";
        if (flags.isNotEmpty()) ++flagged;
        ++measured;
        printf ("%3d %-18s %-28s LUFS %6.1f  peak %6.1f dB  tail %6.1f  trim-> %6.2f%s\n", i, bank[(size_t) i].cat.toRawUTF8(), bank[(size_t) i].name.toRawUTF8(), L, pkDb, tail - body, newTrim, flags.toRawUTF8());
        trimLines.add ("{ \"" + bank[(size_t) i].name + "\", " + juce::String (newTrim, 2) + "f },");
        fflush (stdout);
    }
    printf ("\n%d measured, %d flagged, %d duplicate names\n", measured, flagged, dup);
    if (trimsOut.isNotEmpty()) juce::File (trimsOut).replaceWithText (trimLines.joinIntoString ("\n") + "\n");
    return flagged + dup;
}

#ifndef TRAP_REF_ONLY
namespace r2 { void v2StateTests(); void v2UiTests(); int gvZip(); }   // SOUND ENGINE (V2Tests.inc)
#include "TrapTestsNew.inc"
#include "CleanTests.inc"
#include "ZipperTests.inc"
namespace engtest { void oscEngines (int quality, bool strict); int engineTests (const juce::StringArray&); int engineShots (const juce::File&); }   // ENGINES (round 3)
#include "SoundR2Tests.inc"
#include "V2Tests.inc"            // SOUND ENGINE V2
#include "BrowseTests.inc"
#include "GhostArtTests.inc"
#include "KeysTests.inc"   // KEYS (round 3)
#include "FxRackR3Tests.inc"   // FX RACK (round 3)
#include "BenchTests.inc"
#include "ModUiTests.inc"        // MOD (round 3)
#include "ModTests.inc"
#include "GlassTests.inc"      // GLASS (round 3)
#include "EngineTests.inc"
#include "LayoutTests.inc"
#include "DemoGateTests.inc"   // the demo export gate (`demogate`)
#include "PadGhostTests.inc"    // the pad ghosts look where they move + drag glitch (`padghosts`)
#include "GhostLookTests.inc"   // GHOST LOOK: the 4th look
#include "SoundAuditTests.inc"  // SOUND-ENGINE AUDIT (`soundaudit`, `auditrender`, `auditmatch`)
#include "NoPresetsTests.inc"   // FACTORY PRESETS OFF (`nopresets` / `nopresetsref`)
#endif

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::MessageManager::getInstance()->setCurrentThreadAsMessageThread();
    juce::StringArray args; for (int i = 1; i < argc; ++i) args.add (argv[i]);
    const auto mode = args[0];
    if (mode == "dump") return dump (juce::File (args[1]));
    if (mode == "compare") return compare (juce::File (args[1]), juce::File (args.size() > 2 ? args[2] : juce::String()));
    if (mode == "levels") return levels (args.size() > 1 ? args[1] : juce::String(), args.size() > 2 ? args[2].getDoubleValue() : -16.0);
    if (mode == "list") { int i = 0; for (auto& f : gpro::factoryBank()) printf ("%3d %s | %s | %s\n", i++, f.cat.toRawUTF8(), f.name.toRawUTF8(), f.scene.toRawUTF8()); return 0; }
#ifndef TRAP_REF_ONLY
    if (mode == "demogate") return demogatetest::run();
    if (mode == "savegate") return demogatetest::savegate();
    if (mode == "layout") { layouttest::studioTests (juce::File (args.size() > 1 ? args[1] : juce::String ("/root/work/clang-layout/uishots"))); printf ("\n%d failures\n", failures); return failures; }
    if (mode == "ghostref") return ghostlooktest::ghostRef (juce::File (args.size() > 1 ? args[1] : juce::String ("/root/work/clang-pro-ghost/ghost-ref.hash")), juce::File (args.size() > 2 ? args[2] : juce::String()));   // GHOST LOOK
    if (mode == "ghostshots") { ghostlooktest::ghostShots (juce::File (args.size() > 1 ? args[1] : juce::String ("/root/work/clang-pro-ghost/uishots"))); return 0; }
    if (mode == "ghostlook") { ghostlooktest::ghostLookTests (juce::File (args.size() > 1 ? args[1] : juce::String ("/root/work/clang-pro-ghost/uishots")), juce::File (args.size() > 2 ? args[2] : juce::String ("/root/work/clang-pro-ghost/ghost-ref.hash"))); printf ("\n%d failures\n", failures); return failures; }
    if (mode == "classicref") return layouttest::classicRef (juce::File (args.size() > 1 ? args[1] : juce::String()));
    if (mode == "classic") { layouttest::classicIdentity (juce::File (args.size() > 1 ? args[1] : juce::String ("/root/work/clang-layout/uishots"))); printf ("\n%d failures\n", failures); return failures; }   // (CLASSIC is a fallback now: not in 'ui')
    if (mode == "zipper") { juce::StringArray a (args); a.remove (0); const int z = zippertest::zipperTests (a); if (a.isEmpty()) { section ("ZIPPER: the GHOST VERB's amount (SOUND ENGINE V2)"); const int before = failures; r2::gvZip(); printf ("\n%d failures\n", failures - before); return z + failures - before; } return z; }
    if (mode == "cleanaudit") return cleantest::soundAudit();      // (the CLEAN stress-patch audit; was `soundaudit`)
    if (mode == "soundaudit") { juce::StringArray a (args); a.remove (0); return saudit::run (a); }   // SOUND-ENGINE AUDIT (SoundAuditTests.inc)
    if (mode == "auditprobe") { juce::StringArray a (args); a.remove (0); return saudit::probe (a); }
#if __has_include ("ProV2Norm.inc")
    if (mode == "auditnorm") return saudit::auditNorm (juce::File (args[1]), juce::File (args[2]));
    if (mode == "auditarp") return saudit::auditArp (juce::File (args[1]));   // ArpEZator's preview synth vs Pro (SoundAuditTests.inc)
#endif
    if (mode == "auditbank") { const auto sh = args.size() > 2 ? args[2].fromFirstOccurrenceOf ("=", false, false) : juce::String ("0/1"); return saudit::auditBank (juce::File (args[1]), sh.upToFirstOccurrenceOf ("/", false, false).getIntValue(), juce::jmax (1, sh.fromFirstOccurrenceOf ("/", false, false).getIntValue())); }
    if (mode == "auditbanksum") { const int r = saudit::auditBankSum (juce::File (args[1]), juce::File (args[2])); return r; }
    if (mode == "audittables") return saudit::auditTables (juce::File (args[1]));
    if (mode == "auditrender") return saudit::auditRender (juce::File (args[1]));
    if (mode == "auditmatch") return saudit::auditMatch (juce::File (args[1]), juce::File (args[2]), juce::File (args[3]), juce::File (args.size() > 4 ? args[4] : juce::String()), juce::File (args.size() > 5 ? args[5] : juce::String()));
    if (mode == "fxdebug") return cleantest::fxDebug();
    if (mode == "soundsweep") { juce::StringArray a (args); a.remove (0); return cleantest::soundSweep (a); }
    if (mode == "attrib") { juce::StringArray a (args); a.remove (0); return cleantest::attribution (a); }
    if (mode == "cleantrims") return cleantest::cleanTrims (juce::File (args[1]), args.size() > 2 ? args[2] : juce::String());
    if (mode == "cliplist") return cleantest::clipList (args.size() > 1 ? args[1] : juce::String());
    if (mode == "keys") { { const juce::File d (args.size() > 1 ? args[1] : juce::String ("/root/work/clang-keys/uishots")); keystest::labelTests (d); keystest::holdUi (d); keystest::cursorTests (d); } printf ("\n%d failures\n", failures); return failures; }   // KEYS (round 3)
    if (mode == "hold") { keystest::holdTests(); printf ("\n%d failures\n", failures); return failures; }   // KEYS (round 3)
    if (mode == "modexact")
    {   // MOD (round 3): every factory sound against a dump of the build before, sample for sample (no latency alignment: both builds have the same)
        const juce::File dir (args[1]); juce::StringArray names; dir.getChildFile ("names.txt").readLines (names);
        int same = 0, diff = 0, missing = 0;
        for (auto& nm : names)
        {
            if (nm.isEmpty()) continue;
            const int idx = progOf (nm); if (idx < 0) { ++missing; continue; }
            auto p = makeProc (idx); auto b = seededRender (*p); auto ref = readRaw (dir.getChildFile (safeName (nm) + ".f32"));
            const float d = ref.getNumSamples() == b.getNumSamples() ? maxDiff (b, ref) : 1.0f;
            if (d == 0.0f) ++same; else { ++diff; printf ("DIFFERENT %-30s max diff %g\n", nm.toRawUTF8(), d); }
        }
        printf ("\n%d bit-identical, %d different, %d missing\n", same, diff, missing);
        return diff;
    }
    if (mode == "mod") { juce::StringArray a (args); a.remove (0); return modtest::modTests (a); }    // MOD (round 3)
    if (mode == "drive") return r2::driveTests();
    if (mode == "unison") return r2::unisonTests();
    if (mode == "noise") return r2::noiseTests();
    if (mode == "verb") return r2::verbTests();
    if (mode == "quality") return r2::qualityTests();
    if (mode == "hyper") return r2::hyperTests();
    if (mode == "fxrack") return fxr3::fxRackTests (juce::File (args.size() > 1 ? args[1] : juce::String ("/root/work/wt-pro-fx-renders")), juce::File (args.size() > 2 ? args[2] : juce::String ("/root/work/clang-fx/uishots")));   // FX RACK (round 3)
    if (mode == "specdbg") return r2::specDebug();
    if (mode == "wtable") return r2::wtableTests();
    if (mode == "wtui") return r2::wtuiTests (juce::File (args.size() > 1 ? args[1] : juce::String ("/root/work/clang-prosound/uishots")));
    if (mode == "moreshot") return r2::moreShot (juce::File (args.size() > 1 ? args[1] : juce::String ("/root/work/clang-prosound/uishots")), args.size() > 2 ? args[2].getIntValue() : 0);
    if (mode == "osc") { juce::StringArray a (args); a.remove (0); return r2::oscTests (a); }
    if (mode == "engosc") { engtest::oscEngines (args.size() > 1 ? args[1].getIntValue() : 0, args.size() > 2 && args[2] == "check"); printf ("\n%d failures\n", failures); return failures; }
    if (mode == "engines") { juce::StringArray a (args); a.remove (0); return engtest::engineTests (a); }                   // ENGINES (round 3)
    if (mode == "engstress") { juce::StringArray a (args); a.remove (0); return engtest::engineStress (a); }               // installer #23 Mac crash
    if (mode == "engshots") return engtest::engineShots (juce::File (args.size() > 1 ? args[1] : juce::String ("/root/work/wt-pro-engines-renders/shots")));
    if (mode == "r2audit") { juce::StringArray only (args); only.removeRange (0, 2); return r2::presetAudit (juce::File (args.size() > 1 ? args[1] : juce::String()), only); }
    if (mode == "paramlist") { auto p = makeProc (-1); for (auto* prm : p->getParameters()) if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm)) { printf ("%s | %s | def %s", rp->getParameterID().toRawUTF8(), rp->getName (60).toRawUTF8(), rp->getText (rp->getDefaultValue(), 30).toRawUTF8()); if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (rp)) printf (" | %s", c->choices.joinIntoString (",").toRawUTF8()); printf ("\n"); } return 0; }
    if (mode == "speclist") { int i = 0; for (auto& f : gpro::factoryBank()) printf ("%d|%s|%s|%s|%s|%.2f|%d\n", i++, f.cat.toRawUTF8(), f.name.toRawUTF8(), f.scene.toRawUTF8(), gpro::expandSpec (f.spec).toRawUTF8(), ProProcessor::factoryTrimDb (f.name), ProProcessor::factoryCleanOn (f.name) ? 1 : 0); return 0; }
    if (mode == "v2audit") { juce::StringArray a (args); a.remove (0); return r2::v2Audit (a); }      // SOUND ENGINE V2: every factory sound V2 vs CLASSIC
    if (mode == "gvzip") return r2::gvZip();
    if (mode == "classicexact") return r2::classicExact (juce::File (args.size() > 1 ? args[1] : juce::String ("/root/work/clang-v2-base/ref1")));
    if (mode == "v2render") return r2::v2Render (juce::File (args.size() > 1 ? args[1] : juce::String ("/root/work/wt-pro-v2-renders")));
    if (mode == "r2render") return r2::renderOwner (juce::File (args.size() > 1 ? args[1] : juce::String ("/root/work/wt-pro-sound-renders")));
    if (mode == "ghosts") { ghosttest::ghostTests (juce::File (args.size() > 1 ? args[1] : juce::String ("/root/work/clang-ghosts/uishots"))); printf ("\n%d failures\n", failures); return failures; }
    if (mode == "padghosts") { padghosttest::padGhostTests (juce::File (args.size() > 1 ? args[1] : juce::String ("/root/work/clang-pad-ghosts/uishots"))); printf ("\n%d failures\n", failures); return failures; }
    if (mode == "ghostbench") return ghosttest::ghostBench (args.size() > 1 ? args[1].getIntValue() : 4000, args.size() > 2 ? args[2].getIntValue() : 0);
    if (mode == "bench") return bench::run (args);   // CPU % of real time at 48 kHz / 128 (see BenchTests.inc); bench --check = thresholds
    if (mode == "clean") return cleantest::cleanTests (juce::File (args.size() > 1 ? args[1] : juce::String ("/root/work/wt-clean-pro-build/shots")));
    if (mode == "nopresetsref") return noprtest::makeRef (juce::File (args.size() > 1 ? args[1] : juce::String ("/root/work/clang-pro-nopresets-ref")));
#ifdef GHOSTY_FACTORY_DATA
    if (mode == "nopresets") return noprtest::run (juce::File (args.size() > 1 ? args[1] : juce::String ("/root/work/clang-pro-nopresets-ref")),
                                                   juce::File (args.size() > 2 ? args[2] : juce::String ("/root/work/clang-pro-nopresets/uishots")));
#endif
    if (mode == "browse") return browsetest::browseTests (juce::File (args.size() > 1 ? args[1] : juce::String ("/root/work/clang-browser/uishots")));
    if (mode == "steal") return browsetest::stealTests();
    const int r = newModes (mode, args);
    if (r >= 0) return r + failures;
#endif
    printf ("modes: dump <dir>, compare <dir>, levels [trims.inc] [target], list, stack, sample, character, tape, brass, state, cpu, shots <dir>, demo <dir>\n");
    return 2;
}
