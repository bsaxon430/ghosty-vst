// GhostSliceTests: the Ghost Slice foundation (phases 1-6). Run under xvfb-run -a.
//   GhostSliceTests              every test group (UI shots into GSLICE_SHOTS_DIR)
//   GhostSliceTests <group>      one group: load peaks working json determinism ops warp stretch live undo projects export play repeat hold ui playui repeatui holdui
//                                analysis (SliceAnalysisTests.inc): onsets tempo align pitch key gpitch
//                                note editing (PitchEditTests.inc): notes notesui; chop (ChopTests.inc): chop chopui; keymodes (KeyTests.inc); polish polishui (PolishTests.inc)
//                                the GHOST look (GhostLookTests.inc): ghostui
//                                the finishing pass (FinishTests.inc): tempo2 bounce5 resample elastic syllables regionpitch cliphint repeatwave
//                                the release harness (HarnessTests.inc): golden proptest srmatrix rtsafety blocktime
//                                the chord track (ChordTests.inc): chords chordsui
//   GhostSliceTests demogate     the demo export gate paths + the drop-out
//   GhostSliceTests shots [DIR]  only the UI group (screenshots)
//   GhostSliceTests formantrenders [DIR]  phase 15 A/B renders (before / WIDE + correction / VOCAL) as WAV
//   GhostSliceTests renderhashes / formantlab  render hashes (old-vs-new identity) / the formant table
//   GhostSliceTests golden --update  rewrite tests/golden/golden.json (after an engine-version bump); pluginsmoke [ARTEFACTS] the built VST3 / CLAP
//   GhostSliceTests chordfile F   the chord track + key of an audio file (real-material check)
//   GhostSliceTests polyproto     phase 17 research: multi-f0 prototype on synthetic chords (numbers in POLYPHONIC.md)
//   GhostSliceTests bench [DIR]  the pitch-engine benchmark (SIGNALSMITH / WIDE / VOCAL / WORLD): DIR/bench.csv (BenchTests.inc)
#include "../SliceEditor.h"
#include "tests/DemoGateHarness.h"
#include <random>
#include <set>
#include <cstdio>
#include <atomic>
#include <new>

using namespace gslice;

// ---------------------------------------------------------------- allocation counter (the audio thread must never allocate)
static thread_local bool tlAudio = false;
static std::atomic<long> gAudioAllocs { 0 };
void* operator new (std::size_t n) { if (tlAudio) gAudioAllocs.fetch_add (1); if (void* p = std::malloc (n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[] (std::size_t n) { if (tlAudio) gAudioAllocs.fetch_add (1); if (void* p = std::malloc (n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

static int gPass = 0, gFail = 0;
static void check (bool ok, const juce::String& what)
{
    (ok ? gPass : gFail)++;
    std::printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
    std::fflush (stdout);
}
static void section (const char* s) { std::printf ("\n== %s\n", s); }
static void pump (int ms) { ghosty::demogate::pump (ms); }

static juce::File tmpDir()
{
    static juce::File d = [] { auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("gslice-tests-" + juce::String (juce::Time::currentTimeMillis())); f.createDirectory(); return f; }();
    return d;
}

// ---------------------------------------------------------------- test audio
static juce::AudioBuffer<float> sine (double sr, double secs, double hz, float amp, int ch = 2)
{
    juce::AudioBuffer<float> b (ch, (int) (sr * secs));
    for (int c = 0; c < ch; ++c) for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (c, i, amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / sr + c * 0.3));
    return b;
}
static juce::AudioBuffer<float> drums (double sr, double secs, double bpm = 120.0)
{   // decaying noise + tone hits on 8th notes (strong onsets)
    juce::AudioBuffer<float> b (2, (int) (sr * secs)); b.clear();
    std::mt19937 rng (7); std::uniform_real_distribution<float> U (-1.0f, 1.0f);
    const int step = (int) (sr * 30.0 / bpm);
    for (int hit = 0; hit * step < b.getNumSamples(); ++hit)
        for (int i = 0; i < (int) (sr * 0.12) && hit * step + i < b.getNumSamples(); ++i)
        {
            const float env = std::exp (-(float) i / (float) (sr * 0.025));
            const float v = 0.45f * env * (0.6f * U (rng) + 0.4f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 90.0 * i / sr));
            for (int c = 0; c < 2; ++c) b.setSample (c, hit * step + i, v);
        }
    return b;
}
static juce::AudioBuffer<float> pad (double sr, double secs)
{
    juce::AudioBuffer<float> b (2, (int) (sr * secs)); b.clear();
    for (int c = 0; c < 2; ++c) for (int i = 0; i < b.getNumSamples(); ++i)
    {
        double v = 0; for (int k = 1; k <= 5; ++k) v += std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * k * i / sr + k + c) / k;
        b.setSample (c, i, (float) (0.18 * v));
    }
    return b;
}
static float maxStep (const juce::AudioBuffer<float>& b, int from = 1, int to = -1)
{
    float m = 0; if (to < 0) to = b.getNumSamples();
    for (int c = 0; c < b.getNumChannels(); ++c) { const float* x = b.getReadPointer (c); for (int i = juce::jmax (1, from); i < to; ++i) m = std::max (m, std::abs (x[i] - x[i - 1])); }
    return m;
}
static int maxStepAt (const juce::AudioBuffer<float>& b)
{
    float m = 0; int at = 0;
    for (int c = 0; c < b.getNumChannels(); ++c) { const float* x = b.getReadPointer (c); for (int i = 1; i < b.getNumSamples(); ++i) if (std::abs (x[i] - x[i - 1]) > m) { m = std::abs (x[i] - x[i - 1]); at = i; } }
    return at;
}
static bool bitEqual (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples()) return false;
    for (int c = 0; c < a.getNumChannels(); ++c) if (std::memcmp (a.getReadPointer (c), b.getReadPointer (c), sizeof (float) * (size_t) a.getNumSamples()) != 0) return false;
    return true;
}
static double rmsDiffDb (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int from, int to)
{
    double d = 0, s = 0;
    for (int c = 0; c < 2; ++c) for (int i = from; i < to; ++i) { const double x = a.getSample (c, i), y = b.getSample (c, i); d += (x - y) * (x - y); s += x * x; }
    return 10.0 * std::log10 ((d + 1e-30) / (s + 1e-30));
}
static int zeroCrossings (const juce::AudioBuffer<float>& b, int from, int to) { int n = 0; for (int i = from + 1; i < to; ++i) if ((b.getSample (0, i - 1) < 0) != (b.getSample (0, i) < 0)) ++n; return n; }

static bool writeAudio (const juce::AudioBuffer<float>& b, double sr, const juce::File& f, juce::AudioFormat& fmt, int bits)
{
    f.deleteFile();
    auto os = std::make_unique<juce::FileOutputStream> (f); if (! os->openedOk()) return false;
    std::unique_ptr<juce::AudioFormatWriter> w (fmt.createWriterFor (os.get(), sr, (unsigned) b.getNumChannels(), bits, {}, 0));
    if (w == nullptr) return false;
    os.release();
    return w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

// a processor at 48 kHz with memory audio loaded (LIVE quality unless a test turns STUDIO on)
struct Rig
{
    std::unique_ptr<SliceProcessor> p = std::make_unique<SliceProcessor>();
    explicit Rig (juce::AudioBuffer<float> audio, double sr = 48000.0)
    {
        p->setPlayConfigDetails (0, 2, sr, 512); p->prepareToPlay (sr, 512); pump (5);
        p->loadSourceForTests (source::fromBuffer (std::move (audio), sr, "test.wav"));
        p->setStudioMode (false);
    }
    SliceProcessor* operator->() { return p.get(); }
    juce::AudioBuffer<float> render() { return render::plan (p->currentPlan(), *p->getWorking(), nullptr, false); }
    const Region& r0() { return p->project().regions.front(); }
};

// ================================================================ [1] load + fingerprint
static void testLoad()
{
    section ("[1] load / decode / fingerprint (WAV, AIFF, FLAC, MP3)");
    const auto src = sine (48000, 1.0, 440.0, 0.5);
    juce::WavAudioFormat wav; juce::AiffAudioFormat aiff; juce::FlacAudioFormat flac;
    const auto fw = tmpDir().getChildFile ("t.wav"), fa = tmpDir().getChildFile ("t.aiff"), ff = tmpDir().getChildFile ("t.flac");
    check (writeAudio (src, 48000, fw, wav, 24) && writeAudio (src, 48000, fa, aiff, 24) && writeAudio (src, 48000, ff, flac, 24), "test files written");
    juce::String err;
    auto a = source::load (fw, err), b = source::load (fw, err);
    check (a != nullptr && b != nullptr && a->fingerprint == b->fingerprint && a->fingerprint == juce::SHA256 (fw).toHexString(), "WAV: fingerprint = SHA-256 of the file, the same on every load");
    check (a != nullptr && a->frames == 48000 && a->channels == 2 && a->sampleRate == 48000, "WAV: rate / length / channels (" + (a ? a->describe() : err) + ")");
    float worst = 0; if (a) for (int i = 0; i < 48000; ++i) worst = std::max (worst, std::abs (a->audio.getSample (0, i) - src.getSample (0, i)));
    check (worst < 2.0e-6f, "WAV decodes to float within 24-bit quantisation (" + juce::String (worst, 8) + ")");
    for (auto* f : { &fa, &ff })
    {
        auto s = source::load (*f, err); float wv = 0;
        if (s) for (int i = 0; i < juce::jmin<int> ((int) s->frames, 48000); ++i) wv = std::max (wv, std::abs (s->audio.getSample (1, i) - src.getSample (1, i)));
        check (s != nullptr && s->frames == 48000 && wv < 2.0e-6f, f->getFileExtension().toUpperCase() + " decodes the same audio");
    }
    const juce::File mp3 ("/root/work/04_vocal_ecto_echo.mp3");
    if (mp3.existsAsFile()) { auto s = source::load (mp3, err); check (s != nullptr && s->frames > 44100 && s->fingerprint.length() == 64, "MP3 decodes (" + (s ? s->describe() : err) + ")"); }
    juce::MemoryBlock mb; fw.loadFileAsData (mb); static_cast<char*> (mb.getData())[(int) mb.getSize() - 3] ^= 1;
    const auto f2 = tmpDir().getChildFile ("t2.wav"); f2.replaceWithData (mb.getData(), mb.getSize());
    check (a != nullptr && source::fingerprintFile (f2) != a->fingerprint, "a one-byte change gives a new fingerprint");
    check (source::load (tmpDir().getChildFile ("missing.wav"), err) == nullptr && err.isNotEmpty(), "a missing file is refused with a message");
    const auto cache = tmpDir().getChildFile ("cache");
    auto c1 = source::load (fw, err, cache), c2 = source::load (fw, err, cache);
    check (c1 && c2 && a && c1->peaks.digest() == c2->peaks.digest() && c1->peaks.digest() == a->peaks.digest(), "peak cache: the cached pyramid equals the built one");
}

// ================================================================ [2] peak pyramid + tiles
static bool sameImage (const juce::Image& a, const juce::Image& c)
{
    if (a.getWidth() != c.getWidth() || a.getHeight() != c.getHeight()) return false;
    juce::Image::BitmapData A (a, juce::Image::BitmapData::readOnly), C (c, juce::Image::BitmapData::readOnly);
    for (int y = 0; y < a.getHeight(); ++y) if (std::memcmp (A.getLinePointer (y), C.getLinePointer (y), (size_t) (A.pixelStride * a.getWidth())) != 0) return false;
    return true;
}
static void testPeaks()
{
    section ("[2] min/max pyramid = brute force; tiles + tile cache determinism");
    std::mt19937 rng (3); std::uniform_real_distribution<float> U (-1, 1);
    juce::AudioBuffer<float> b (2, 200003); for (int c = 0; c < 2; ++c) for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (c, i, U (rng) * (0.2f + 0.8f * (float) ((i / 5000) % 3) / 2.0f));
    PeakPyramid pk; pk.build (b);
    PeakPyramid pk2; pk2.build (b);
    check (pk.digest() == pk2.digest(), "the pyramid is deterministic (" + juce::String (pk.numLevels()) + " levels)");
    int bad = 0; std::uniform_int_distribution<int> P (0, b.getNumSamples() - 1);
    for (int k = 0; k < 400; ++k)
    {
        int x = P (rng), y = P (rng); if (x > y) std::swap (x, y); if (y == x) ++y;
        if (k % 4 == 0) y = juce::jmin (b.getNumSamples(), x + 1 + k);
        const int ch = k & 1;
        const auto q = pk.query (b, ch, x, y);
        float mn = 1e9f, mx = -1e9f; double ms = 0; for (int i = x; i < y; ++i) { const float v = b.getSample (ch, i); mn = std::min (mn, v); mx = std::max (mx, v); ms += (double) v * v; }
        ms /= (y - x);
        if (q.mn != mn || q.mx != mx || std::abs (q.ms - ms) > 1e-4 * ms + 1e-7) ++bad;
    }
    check (bad == 0, "400 random spans: min / max exact, mean square within 1e-4 (" + juce::String (bad) + " bad)");
    Rig rig (pad (48000, 4.0));
    auto P0 = ui::palette (ui::Theme::Dark);
    const auto& r = rig.r0();
    auto t1 = ui::renderWaveTile (*rig->getSource(), rig->project(), r, 48000, 0, 40.0, 120, P0, true);
    auto t2 = ui::renderWaveTile (*rig->getSource(), rig->project(), r, 48000, 0, 40.0, 120, P0, true);
    check (sameImage (t1, t2), "a tile renders the same pixels twice");
    auto close = ui::renderWaveTile (*rig->getSource(), rig->project(), r, 48000, 100, 0.25, 120, P0, true);
    int lit = 0; { juce::Image::BitmapData D (close, juce::Image::BitmapData::readOnly); for (int x = 0; x < close.getWidth(); ++x) for (int y = 0; y < close.getHeight(); ++y) if (D.getPixelColour (x, y).getAlpha() > 60) { ++lit; break; } }
    check (lit > 200, "sample-level zoom draws the samples as a line (" + juce::String (lit) + " lit columns)");
    ui::WaveTileCache cache; cache.capacity = 4;
    ui::WaveTileCache::Key k; k.region = 1; k.h = 100;
    int made = 0; auto mk = [&] { ++made; return juce::Image (juce::Image::ARGB, 4, 4, true); };
    for (int i = 0; i < 3; ++i) { k.tile = i; cache.get (k, mk); }
    for (int i = 0; i < 3; ++i) { k.tile = i; cache.get (k, mk); }
    for (int i = 3; i < 8; ++i) { k.tile = i; cache.get (k, mk); }
    check (made == 8 && cache.hits == 3 && cache.size() == 4, "tile LRU: repeats are hits, the capacity holds (" + juce::String (made) + " made, " + juce::String (cache.hits) + " hits)");
}

// ================================================================ [3] working copy
static void testWorking()
{
    section ("[3] the working copy at the host rate");
    auto s = source::fromBuffer (sine (44100, 2.0, 1000.0, 0.5), 44100, "s.wav");
    auto w = source::makeWorking (s, 48000);
    check (w->frames() == (int64) std::llround (88200 * 48000.0 / 44100.0), "length = frames x 48000 / 44100 (" + juce::String (w->frames()) + ")");
    juce::AudioBuffer<float> wb (1, (int) w->frames()); for (int i = 0; i < wb.getNumSamples(); ++i) wb.setSample (0, i, w->at (0, i));
    const int zc = zeroCrossings (wb, 4800, 4800 + 48000);
    check (std::abs (zc - 2000) <= 2, "a 1 kHz sine stays 1 kHz (" + juce::String (zc) + " crossings per second)");
    auto cross = [] (std::function<float (int)> f, int from) { for (int i = from; i < from + 400; ++i) if (f (i - 1) < 0 && f (i) >= 0) return (double) (i - 1) + f (i - 1) / (f (i - 1) - f (i)); return -1.0; };
    const double a = cross ([&] (int i) { return s->audio.getSample (0, i); }, 22050) / 44100.0;
    const double b = cross ([&] (int i) { return w->at (0, i); }, (int) std::llround (a * 48000) - 20) / 48000.0;
    check (std::abs (a - b) * 48000 < 1.0, "time-aligned within 1 sample (" + juce::String ((a - b) * 48000.0, 3) + ")");
    auto same = source::makeWorking (s, 44100);
    check (same->ratio == 1.0 && same->frames() == s->frames, "same rate: the original is read directly (no copy)");
}

// ================================================================ [4] JSON round trip
static Project randomEdit (const Project& p, std::mt19937& rng, juce::String& label)
{
    std::uniform_int_distribution<int> op (0, 14);
    std::uniform_real_distribution<double> U (0, 1);
    if (p.regions.empty()) return p;
    const auto& r = p.regions[(size_t) std::uniform_int_distribution<int> (0, (int) p.regions.size() - 1) (rng)];
    std::optional<Project> q;
    switch (op (rng))
    {
        case 0: q = ops::splitAt (p, r.id, r.dstStart + r.length() * (0.1 + 0.8 * U (rng))); label = "split"; break;
        case 1: q = ops::moveRegion (p, r.id, U (rng) * p.length()); label = "move"; break;
        case 2: q = ops::setGain (p, r.id, -24 + 30 * U (rng)); label = "gain"; break;
        case 3: q = ops::setFades (p, r.id, 200 * U (rng), 200 * U (rng)); label = "fades"; break;
        case 4: q = ops::setReverse (p, r.id, ! r.reverse); label = "rev"; break;
        case 5: if (p.regions.size() < 12) q = ops::duplicate (p, r.id); label = "dup"; break;
        case 6: if (p.regions.size() > 1) q = ops::deleteRegion (p, r.id); label = "del"; break;
        case 7: q = ops::stretchTo (p, r.id, r.length() * (0.6 + 0.8 * U (rng))); label = "stretch"; break;
        case 8: q = ops::addMarker (p, r.id, r.length() * (0.1 + 0.8 * U (rng))); label = "marker"; break;
        case 9: if (r.warp.size() > 2) q = ops::dragMarker (p, r.id, r.warp[1].id, r.warp[1].t * (0.8 + 0.4 * U (rng)), U (rng) < 0.5 ? FlexMode::Move : FlexMode::Stretch); label = "drag"; break;
        case 10: q = ops::slip (p, r.id, (int64) ((U (rng) - 0.5) * 4000)); label = "slip"; break;
        case 11: if (r.warp.size() > 2) q = ops::setAnchor (p, r.id, r.warp[1].id, true); label = "anchor"; break;
        case 12: if (p.regions.size() > 1) q = ops::rearrange (p, 0, (int) p.regions.size() - 1); label = "rearrange"; break;
        case 13: { Project n = p; n.pitchSemis = std::round (U (rng) * 10 - 5); n.bpm = 80 + U (rng) * 80; n.gridDiv = 16; q = n; label = "global"; break; }
        case 14: q = ops::trim (p, r.id, U (rng) < 0.5, r.srcIn() + (int64) ((U (rng) - 0.5) * 2000)); label = "trim"; break;
        default: break;
    }
    return q.value_or (p);
}

static void testJson()
{
    section ("[4] project JSON round trip (1000 random projects)");
    SourceRef ref; ref.fingerprint = juce::String::repeatedString ("ab", 32); ref.sampleRate = 48000; ref.frames = 480000; ref.channels = 2; ref.fileName = "x.wav"; ref.path = "/tmp/x.wav";
    std::mt19937 rng (11);
    int bad = 0, invalid = 0;
    for (int k = 0; k < 1000; ++k)
    {
        Project p = makeProject (ref); juce::String l;
        const int n = k % 25;
        for (int i = 0; i < n; ++i) p = randomEdit (p, rng, l);
        if (k % 7 == 0 && ! p.regions.empty()) p.regions[0].dstStart += 0.123456789012345;   // awkward doubles survive exactly
        for (auto& r : p.regions) invalid += r.valid() ? 0 : 1;
        Project q; juce::String err;
        if (! json::fromString (json::toString (p), q, err) || ! (q == p)) { if (bad < 3) std::printf ("    mismatch case %d: %s\n", k, err.toRawUTF8()); ++bad; }
    }
    check (bad == 0, "model -> JSON -> model is identical (" + juce::String (bad) + " of 1000 differ)");
    check (invalid == 0, "every random edit keeps every region valid (monotonic warp, in bounds)");
    Project p = makeProject (ref); auto v = json::toVar (p); v.getDynamicObject()->setProperty ("futureThing", 42);
    Project q; juce::String err;
    const bool ok = json::fromVar (v, q, err);
    check (ok && json::toVar (q)["futureThing"].toString() == "42", "unknown fields are kept on save (forward compatibility)");
    v.getDynamicObject()->setProperty ("formatVersion", 99);
    check (! json::fromVar (v, q, err) && err.contains ("newer"), "a newer format is refused with a message");
    check (! json::fromString ("{\"format\":\"something\"}", q, err), "not a project: refused");
}

// ================================================================ [5] render list determinism
static void testDeterminism()
{
    section ("[5] render list + render determinism (bit-exact)");
    Rig rig (pad (48000, 3.0));
    Project p = rig->project();
    p = *ops::stretchTo (p, rig.r0().id, rig.r0().length() * 1.25);
    int64 mid = 0; p = *ops::addMarker (p, p.regions[0].id, 30000, &mid);
    p = *ops::splitAt (p, p.regions[0].id, 90000);
    p.pitchSemis = 3;
    rig->apply (p, "setup");
    CompileOptions o; o.hostRate = 48000;
    const auto a = compile (rig->project(), rig->getSource().get(), o), b = compile (rig->project(), rig->getSource().get(), o);
    bool same = a.fingerprint == b.fingerprint && a.pieces.size() == b.pieces.size();
    for (size_t i = 0; same && i < a.pieces.size(); ++i) same = a.pieces[i].fingerprint == b.pieces[i].fingerprint && a.pieces[i].studioKey == b.pieces[i].studioKey;
    check (same, "the same project compiles to identical fingerprints (" + juce::String ((int) a.pieces.size()) + " pieces, plan " + shortFp (a.fingerprint) + ")");
    const auto r1 = render::plan (a, *rig->getWorking(), nullptr, true), r2 = render::plan (b, *rig->getWorking(), nullptr, true);
    check (bitEqual (r1, r2) && r1.getNumSamples() == a.length, "two STUDIO renders are bit-exact (" + juce::String (r1.getNumSamples()) + " frames)");
    StudioCache cache; const auto r3 = render::plan (a, *rig->getWorking(), &cache, true), r4 = render::plan (a, *rig->getWorking(), &cache, true);
    check (bitEqual (r1, r3) && bitEqual (r1, r4), "renders through the cache equal fresh renders");
    Project m = rig->project(); m.regions.back().dstStart += 4800;
    const auto c = compile (m, rig->getSource().get(), o);
    bool keyKept = false; for (auto& pc : c.pieces) if (pc.regionId == m.regions.back().id) for (auto& pa : a.pieces) if (pa.regionId == pc.regionId && pa.studioKey == pc.studioKey) keyKept = true;
    check (c.fingerprint != a.fingerprint && keyKept, "moving a region: a new plan fingerprint, the same STUDIO cache key (no re-render)");
    Project g = rig->project(); g.regions.back().gainDb = -3;
    const auto d = compile (g, rig->getSource().get(), o);
    check (d.fingerprint != a.fingerprint && d.pieces.back().studioKey == a.pieces.back().studioKey && d.pieces.back().liveKey == a.pieces.back().liveKey, "gain: a new fingerprint, the same stream keys (the voice keeps playing)");
    Project h = rig->project(); h.pitchSemis = 4;
    const auto e = compile (h, rig->getSource().get(), o);
    check (e.pieces.back().studioKey != a.pieces.back().studioKey && e.pieces.back().liveKey == a.pieces.back().liveKey, "pitch: a new STUDIO key, the LIVE voice continues");
}

// ================================================================ [6] region ops (1x, sample-exact)
static void testOps()
{
    section ("[6] region ops render exactly (cut / delete / duplicate / reverse / gain / fades / move / rearrange / top wins / trim / anchor)");
    const int sr = 48000;
    Rig rig (sine (sr, 2.0, 220.0, 0.5));
    const auto src = rig->getSource();
    const auto base = rig.render();
    bool nulls = base.getNumSamples() == src->frames;
    for (int c = 0; c < 2 && nulls; ++c) nulls = std::memcmp (base.getReadPointer (c), src->audio.getReadPointer (c), sizeof (float) * (size_t) src->frames) == 0;
    check (nulls, "the fresh project (1x) renders the source bit for bit");
    auto p1 = ops::splitAt (rig->project(), rig.r0().id, 31234.0);
    check (p1.has_value() && p1->regions.size() == 2, "CUT at the cursor makes two regions");
    if (p1) rig->apply (*p1, "split");
    check (bitEqual (rig.render(), base), "a cut at 1x plays exactly like before (no fade, no gap)");
    auto p2 = ops::deleteRange (rig->project(), 60000, 72000, false);
    if (p2) rig->apply (*p2, "delete range");
    auto r2 = rig.render();
    float inGap = 0; for (int i = 60000; i < 72000; ++i) inGap = std::max (inGap, std::abs (r2.getSample (0, i)));
    const float natural = maxStep (base);
    const float fadeBound = natural + 0.5f * juce::MathConstants<float>::pi / (2.0f * 48.0f);   // the signal's slope + a 1 ms fade of a 0.5 peak
    check (p2.has_value() && inGap == 0.0f && maxStep (r2) <= fadeBound, "DELETE a range: silence inside, 1 ms fades at the edges (max step " + juce::String (maxStep (r2), 5) + ", bound " + juce::String (fadeBound, 5) + "; a hard cut would step up to 0.5)");
    float refErr = 0; const int edge = 48;
    for (int i = 0; i < 2000; ++i) { const int n = 72000 + i; const double g = i < edge ? 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * i / edge) : 1.0; refErr = std::max (refErr, std::abs (r2.getSample (0, n) - (float) (src->audio.getSample (0, n) * g))); }
    check (refErr < 1.0e-6f, "after the cut: source x a 1 ms raised-cosine fade-in, sample-exact vs a hand-built reference (" + juce::String (refErr, 9) + ")");
    Rig d (sine (sr, 1.0, 330.0, 0.4));
    int64 nid = 0; auto pd = ops::duplicate (d->project(), d.r0().id, &nid); d->apply (*pd, "dup");
    auto rd = d.render();
    float dupErr = 0; for (int i = 100; i < 47000; ++i) dupErr = std::max (dupErr, std::abs (rd.getSample (1, 48000 + i) - rd.getSample (1, i)));
    check (rd.getNumSamples() == 96000 && dupErr < 1e-7f, "DUPLICATE: the copy right after equals the original");
    Rig rv (drums (sr, 1.0)); auto prv = ops::setReverse (rv->project(), rv.r0().id, true); rv->apply (*prv, "rev");
    auto rr = rv.render(); float revErr = 0;
    for (int i = 100; i < 47900; ++i) revErr = std::max (revErr, std::abs (rr.getSample (0, i) - rv->getSource()->audio.getSample (0, 47999 - i)));
    check (revErr < 1e-7f, "REVERSE plays the region backwards, sample-exact (" + juce::String (revErr, 9) + ")");
    Rig gn (sine (sr, 1.0, 100.0, 0.5)); auto pg = ops::setGain (gn->project(), gn.r0().id, -6.0206); pg = ops::setFades (*pg, gn.r0().id, 100, 50); gn->apply (*pg, "gain");
    auto rgn = gn.render(); float gErr = 0;
    for (int i = 6000; i < 40000; ++i) gErr = std::max (gErr, std::abs (rgn.getSample (0, i) - 0.5f * gn->getSource()->audio.getSample (0, i)));
    bool fadeOk = std::abs (rgn.getSample (0, 0)) < 1e-6f; double prevEnv = -1;
    for (int i = 0; i < 4800; i += 120) { const double s = std::abs ((double) gn->getSource()->audio.getSample (0, i)); if (s > 0.05) { const double e = std::abs (rgn.getSample (0, i)) / s; if (e + 1e-6 < prevEnv) fadeOk = false; prevEnv = e; } }
    check (gErr < 1e-4f, "GAIN -6.02 dB halves the level (" + juce::String (gErr, 7) + ")");
    check (fadeOk && std::abs (rgn.getSample (0, 47999)) < 1e-3f, "FADE IN starts at 0 and rises; FADE OUT ends at 0");
    Rig mv (sine (sr, 1.0, 200.0, 0.5)); auto pm = ops::moveRegion (mv->project(), mv.r0().id, 24000); mv->apply (*pm, "move");
    auto rmv = mv.render(); float mvErr = 0; for (int i = 100; i < 47000; ++i) mvErr = std::max (mvErr, std::abs (rmv.getSample (0, 24000 + i) - mv->getSource()->audio.getSample (0, i)));
    float before = 0; for (int i = 0; i < 24000; ++i) before = std::max (before, std::abs (rmv.getSample (0, i)));
    check (rmv.getNumSamples() == 72000 && mvErr < 1e-7f && before == 0, "MOVE: silence before, the audio shifted by 0.5 s");
    Rig ra (drums (sr, 2.0));
    auto pa = ops::splitAt (ra->project(), ra.r0().id, 48000); ra->apply (*pa, "split");
    auto pb = ops::rearrange (ra->project(), 0, 1); ra->apply (*pb, "rearrange");
    auto rra = ra.render(); const auto& S = ra->getSource()->audio; float raErr = 0;
    for (int i = 200; i < 47800; ++i) { raErr = std::max (raErr, std::abs (rra.getSample (0, i) - S.getSample (0, 48000 + i))); raErr = std::max (raErr, std::abs (rra.getSample (0, 48000 + i) - S.getSample (0, i))); }
    check (ra->project().regions[1].dstStart == 0 && raErr < 1e-7f, "REARRANGE swaps the blocks: the render equals the swapped source");
    check (maxStep (rra) <= maxStep (drums (sr, 2.0)) * 1.05f, "no clicks at the swapped joins (max sample step)");
    Rig tw (sine (sr, 2.0, 150.0, 0.4));
    auto t1 = ops::splitAt (tw->project(), tw.r0().id, 48000); tw->apply (*t1, "split");
    auto t2 = ops::moveRegion (tw->project(), tw->project().regions[1].id, 24000); tw->apply (*t2, "overlap");
    const auto& plan = tw->currentPlan(); bool noOverlap = true; for (size_t i = 1; i < plan.pieces.size(); ++i) if (plan.pieces[i].dst0 < plan.pieces[i - 1].dst1) noOverlap = false;
    auto rtw = tw.render(); float twErr = 0; for (int i = 200; i < 20000; ++i) twErr = std::max (twErr, std::abs (rtw.getSample (0, 24000 + i) - tw->getSource()->audio.getSample (0, 48000 + i)));
    check (noOverlap && twErr < 1e-7f, "overlap: the top region wins, the plan never overlaps");
    Rig tr (sine (sr, 1.0, 200.0, 0.4)); auto pt = ops::trim (tr->project(), tr.r0().id, false, 30000); if (pt) tr->apply (*pt, "trim");
    check (pt.has_value() && tr->project().regions[0].srcOut() == 30000 && std::abs (tr->project().regions[0].length() - 30000) < 1e-9, "TRIM the end keeps 1x");
    Rig an (sine (sr, 1.0, 200.0, 0.4)); int64 mk = 0; auto pan = ops::addMarker (an->project(), an.r0().id, 20000, &mk); pan = ops::setAnchor (*pan, an.r0().id, mk, true); an->apply (*pan, "anchor");
    juce::String why;
    check (! ops::moveRegion (an->project(), an.r0().id, 5000, &why).has_value() && why.contains ("ANCHOR"), "ANCHOR LOCK: the region can't move (" + why + ")");
    // reversed split keeps the audio
    Rig rs (drums (sr, 1.0)); auto prs = ops::setReverse (rs->project(), rs.r0().id, true); rs->apply (*prs, "rev"); const auto revBase = rs.render();
    auto prs2 = ops::splitAt (rs->project(), rs.r0().id, 20000); if (prs2) rs->apply (*prs2, "split rev");
    check (prs2.has_value() && bitEqual (rs.render(), revBase), "a cut in a reversed region is transparent too");
}

// ================================================================ [7] warp maths
static void testWarp()
{
    section ("[7] warp map maths: monotonic, inverse, MOVE / STRETCH / SLIP, limits, anchors, transient lock");
    WarpMap m; m.t = { 0, 1000, 3000, 4000 }; m.s = { 0, 800, 3200, 4000 };
    bool inv = true; for (double u = -50; u < 4100; u += 7.3) if (std::abs (m.outAt (m.srcAt (u)) - u) > 1e-9) inv = false;
    check (m.valid() && inv, "s(t) and t(s) are inverse (extrapolation included)");
    check (std::abs (m.rateAt (500) - 0.8) < 1e-12 && std::abs (m.rateAt (2000) - 1.2) < 1e-12, "segment rates");
    WarpMap bad = m; bad.s[2] = 700; check (! bad.valid(), "a non-monotonic map is invalid");
    SourceRef ref; ref.fingerprint = "f"; ref.sampleRate = 48000; ref.frames = 96000; ref.channels = 1;
    Project p = makeProject (ref); const auto id = p.regions[0].id;
    int64 a = 0, b = 0; p = *ops::addMarker (p, id, 24000, &a); p = *ops::addMarker (p, id, 48000, &b);
    const double len = p.regions[0].length();
    auto mv = ops::dragMarker (p, id, a, 30000, FlexMode::Move);
    check (mv && mv->regions[0].length() == len && mv->regions[0].warp[1].t == 30000 && mv->regions[0].warp[2].t == 48000 && mv->regions[0].warp[1].s == 24000, "MOVE: the marker moves, its source stays, the length stays");
    auto st = ops::dragMarker (p, id, a, 30000, FlexMode::Stretch);
    check (st && std::abs (st->regions[0].length() - (len + 6000)) < 1e-9 && st->regions[0].warp[2].t == 54000, "STRETCH: the length changes by the drag, later markers shift");
    check (! ops::slip (p, id, 1000).has_value(), "SLIP at the source end is refused (the region covers the whole file)");
    auto shorter = ops::trim (p, id, false, 80000); auto sl2 = shorter ? ops::slip (*shorter, id, 1000) : std::nullopt;
    check (sl2 && sl2->regions[0].warp[1].s == 25000 && sl2->regions[0].warp[1].t == 24000 && sl2->regions[0].length() == shorter->regions[0].length(), "SLIP: every source position shifts, output times stay");
    juce::String why; auto lim = ops::dragMarker (p, id, a, 1000, FlexMode::Move, &why);
    check (! lim.has_value() && why.contains ("limit"), "a drag past 4x is refused (" + why + ")");
    auto anc = *ops::setAnchor (p, id, b, true);
    auto st2 = ops::dragMarker (anc, id, a, 30000, FlexMode::Stretch);
    check (st2 && st2->regions[0].warp[2].t == 48000 && st2->regions[0].length() == len && ops::anchorsKept (anc, *st2), "STRETCH stops at an anchor: the anchored marker never moves");
    check (! ops::dragMarker (anc, id, b, 50000, FlexMode::Move).has_value(), "an anchored marker can't be dragged");
    auto rp = *ops::setReverse (p, id, true);
    auto rmv = ops::dragMarker (rp, id, b, 50000, FlexMode::Move);
    check (rmv && std::abs ((len - rmv->regions[0].warp[2].t) - 50000) < 1e-9, "a reversed region's marker follows the mouse in output time");
    WarpMap w; w.t = { 0, 60000 }; w.s = { 0, 48000 };   // 0.8x
    int locked = 0; auto tl = transientLock (w, { 12000, 30000 }, 96, 960, &locked);
    bool unit = true; for (size_t i = 0; i + 1 < tl.t.size(); ++i) { const double mid = (tl.s[i] + tl.s[i + 1]) * 0.5; for (double e : { 12000.0, 30000.0 }) if (mid > e - 96 && mid < e + 960 && std::abs (tl.rateOf (i) - 1.0) > 1e-9) unit = false; }
    check (locked == 2 && tl.valid() && unit && tl.t.back() == 60000 && tl.s.back() == 48000, "transient lock: 1x around each onset, the region's ends unchanged (" + juce::String ((int) tl.t.size()) + " points)");
    check (ops::bpmFromSpan (2.0) == 120.0 && std::abs (ops::bpmFromSpan (0.5) - 120.0) < 1e-9, "BPM from two markers: 2 s = 4 beats at 120, 0.5 s = 1 beat at 120");
    Project gp = p; gp.bpm = 120; gp.gridDiv = 16;   // 1/16 at 120 = 6000 frames
    check (std::abs (gp.snap (6100) - 6000) < 1e-9 && std::abs (gp.snap (6100, 0.5) - 6050) < 1e-9 && gp.snap (6100, 0.0) == 6100, "grid snap with strength 100 / 50 / 0 %");
    gp.gridTriplet = true; check (std::abs (gp.gridStep() - 4000) < 1e-9, "1/16 triplet = 4000 frames at 120 BPM");
}

// ================================================================ [8] stretch
static void testStretch()
{
    section ("[8] stretch: exact length, 1x nulls, pitch, joins, marker alignment, transient lock");
    for (double ratio : { 0.5, 0.75, 1.25, 2.0 })
    {
        Rig rig (pad (48000, 1.5));
        auto q = ops::stretchTo (rig->project(), rig.r0().id, std::round (rig.r0().length() * ratio)); rig->apply (*q, "stretch");
        const auto out = rig.render();
        const int64 want = (int64) std::llround (72000 * ratio);
        float pk = 0; for (int i = 0; i < out.getNumSamples(); ++i) pk = std::max (pk, std::abs (out.getSample (0, i)));
        check (out.getNumSamples() == want && rig->currentPlan().pieces[0].length() == want && pk > 0.1f && maxStep (out) < maxStep (rig->getSource()->audio) * 1.5f,
               "STUDIO " + juce::String (ratio, 2) + "x: exactly " + juce::String (want) + " frames, sound (peak " + juce::String (pk, 2) + "), no clicks");
    }
    Rig one (pad (48000, 1.0)); const auto o1 = one.render();
    check (bitEqual (o1, one->getSource()->audio) && ! one->currentPlan().pieces[0].stretch, "1.0x nulls to the source (bit for bit; the stretcher is not used)");
    Rig pr (sine (48000, 1.5, 300.0, 0.4)); Project pp = pr->project(); pp.pitchSemis = 12; pr->apply (pp, "pitch");
    const auto po = pr.render(); const int zc = zeroCrossings (po, 12000, 60000);
    check (po.getNumSamples() == 72000 && std::abs (zc - 1200) < 30, "PITCH +12: 600 Hz out of 300 Hz (" + juce::String (zc / 2) + " Hz), same length");
    Rig fm (sine (48000, 1.0, 300.0, 0.4)); Project fp = fm->project(); fp.formantSemis = 3; fm->apply (fp, "formant");
    check (fm->currentPlan().pieces[0].stretch && fm.render().getNumSamples() == 48000, "FORMANT alone runs the stretcher (Signalsmith formant shift), same length");
    Rig jn (pad (48000, 2.0));
    auto jq = ops::splitAt (jn->project(), jn.r0().id, 48000); jq = ops::stretchTo (*jq, jq->regions[0].id, 60000);
    jq = ops::moveRegion (*jq, jq->regions[1].id, 60000); jn->apply (*jq, "join");
    const auto jo = jn.render(); float joinStep = 0; for (int i = 59800; i < 60200; ++i) for (int c = 0; c < 2; ++c) joinStep = std::max (joinStep, std::abs (jo.getSample (c, i) - jo.getSample (c, i - 1)));
    check (joinStep <= maxStep (jn->getSource()->audio) * 1.2f, "no click at the join of a stretched and a plain piece (step " + juce::String (joinStep, 5) + ")");
    juce::AudioBuffer<float> imp (2, 96000); imp.clear(); for (int c = 0; c < 2; ++c) imp.setSample (c, 36000, 0.8f);
    Rig al (imp); int64 mk = 0;
    auto aq = ops::addMarker (al->project(), al.r0().id, 36000, &mk);
    aq = ops::dragMarker (*aq, al.r0().id, mk, 42000, FlexMode::Move); aq->transientLock = false; al->apply (*aq, "align");
    const auto ao = al.render(); int at = 0; float best = 0; for (int i = 0; i < ao.getNumSamples(); ++i) if (std::abs (ao.getSample (0, i)) > best) { best = std::abs (ao.getSample (0, i)); at = i; }
    check (std::abs (at - 42000) <= 96, "a marker pins source 0.75 s to output 0.875 s: the click lands within 2 ms (" + juce::String (at - 42000) + " samples)");
    Rig dr (drums (48000, 2.0)); auto dq = ops::stretchTo (dr->project(), dr.r0().id, 128000); dr->apply (*dq, "slow");
    const int lockedSpans = dr->currentPlan().pieces[0].lockedOnsets;
    const auto locked = dr.render(); Project nl = dr->project(); nl.transientLock = false; dr->apply (nl, "unlocked"); const auto plain = dr.render();
    // attack time: 10 % -> 90 % rise of a 0.5 ms-hop RMS envelope at each hit (median), the study's transient-smear metric
    auto rise = [] (const juce::AudioBuffer<float>& b, double hitStep)
    {
        const int hop = 24, n = b.getNumSamples() / hop; std::vector<float> env ((size_t) n, 0.0f);
        for (int f = 0; f < n; ++f) { double e = 0; for (int i = 0; i < 48 && f * hop + i < b.getNumSamples(); ++i) e += std::pow (b.getSample (0, f * hop + i), 2); env[(size_t) f] = (float) std::sqrt (e / 48); }
        std::vector<double> rs;
        for (int h = 1; h * hitStep < b.getNumSamples() - 4800; ++h)
        {
            const int c = (int) (h * hitStep / hop), w = 2400 / hop; int pk = c - w;
            for (int f = juce::jmax (0, c - w); f < juce::jmin (n, c + w); ++f) if (env[(size_t) f] > env[(size_t) pk]) pk = f;
            int lo = pk; while (lo > 0 && env[(size_t) lo] > 0.1f * env[(size_t) pk]) --lo;
            int hi = lo; while (hi < pk && env[(size_t) hi] < 0.9f * env[(size_t) pk]) ++hi;
            rs.push_back ((hi - lo) * hop / 48.0);
        }
        std::sort (rs.begin(), rs.end()); return rs.empty() ? 0.0 : rs[rs.size() / 2];
    };
    const double rSrc = rise (dr->getSource()->audio, 12000.0), rLock = rise (locked, 16000.0), rPlain = rise (plain, 16000.0);
    check (lockedSpans >= 6 && rLock <= rPlain + 0.25 && rLock - rSrc < 2.0,
           "transient lock at 0.75x on drums (" + juce::String (lockedSpans) + " attacks locked): attack time " + juce::String (rLock, 2) + " ms vs "
           + juce::String (rPlain, 2) + " ms without, source " + juce::String (rSrc, 2) + " ms (gate: growth < 2 ms)");
}

// ================================================================ [9] LIVE playback + LIVE -> STUDIO switch
static juce::AudioBuffer<float> playFor (SliceProcessor& p, int64 from, int frames, int block, std::function<void (int64)> atBlock = {})
{
    p.setCursor (from); p.play (true);
    juce::MidiBuffer m; juce::AudioBuffer<float> b (2, block);
    const int lat = p.ceilingLatency();
    juce::AudioBuffer<float> all (2, frames + lat + block); all.clear();
    for (int done = 0; done < frames + lat; done += block)
    {
        if (atBlock) atBlock (done);
        tlAudio = true; p.processBlock (b, m); tlAudio = false;
        for (int c = 0; c < 2; ++c) all.copyFrom (c, done, b, c, 0, block);
    }
    p.play (false);
    juce::AudioBuffer<float> out (2, frames);
    for (int c = 0; c < 2; ++c) out.copyFrom (c, 0, all, c, lat, frames);
    return out;
}

static void testLive()
{
    section ("[9] LIVE playback, LIVE -> STUDIO switch, edits while playing, real-time safety");
    Rig a (pad (48000, 1.0));
    { juce::AudioBuffer<float> w (2, 512); juce::MidiBuffer m; a->processBlock (w, m); }   // warm up (first-block lazies)
    gAudioAllocs = 0;
    const auto out = playFor (*a.p, 0, 40000, 256);
    float err = 0; for (int i = 0; i < 40000; ++i) err = std::max (err, std::abs (out.getSample (0, i) - a->getSource()->audio.getSample (0, i)));
    check (err < 1e-6f, "LIVE 1x playback through the plugin output = the source (" + juce::String (err, 8) + ")");
    Rig s (pad (48000, 3.0));
    auto q = ops::stretchTo (s->project(), s.r0().id, 144000 * 1.1); s->apply (*q, "stretch");
    const auto live = playFor (*s.p, 0, 120000, 256);
    const auto studio = render::plan (s->currentPlan(), *s->getWorking(), nullptr, false);
    const double diff = rmsDiffDb (studio, live, 12000, 110000);
    float pk = 0; for (int i = 0; i < 120000; ++i) pk = std::max (pk, std::abs (live.getSample (0, i)));
    check (pk > 0.1f && maxStep (live) < maxStep (studio) * 1.5f, "LIVE stretch plays (peak " + juce::String (pk, 2) + "), no clicks (max step " + juce::String (maxStep (live), 4) + " at " + juce::String (maxStepAt (live)) + ")");
    std::printf ("    LIVE vs STUDIO difference on the pad at 1.1x: %.1f dB (two presets, different phases)\n", diff);
    check (gAudioAllocs.load() == 0, "no allocation on the audio thread while playing LIVE (" + juce::String (gAudioAllocs.load()) + ")");
    Rig w (pad (48000, 3.0));
    auto wq = ops::stretchTo (w->project(), w.r0().id, 144000 * 1.1); w->apply (*wq, "stretch");
    w->setStudioMode (true); w->getPlayer().switches = 0;
    bool published = false;
    const auto sw = playFor (*w.p, 0, 120000, 256, [&] (int64 done)
    {
        if (! published && done >= 30000) { w->waitStudio (20000); published = true; gAudioAllocs = 0; }   // the render lands mid-piece
    });
    const int switches = w->getPlayer().switches.load();
    check (switches >= 1, "LIVE -> STUDIO switched while playing (" + juce::String (switches) + ")");
    check (maxStep (sw) <= maxStep (studio) * 1.5f, "the switch is click-free (max step " + juce::String (maxStep (sw), 4) + " at " + juce::String (maxStepAt (sw)) + " vs the render's " + juce::String (maxStep (studio), 4) + ")");
    float post = 0; for (int i = 90000; i < 110000; ++i) post = std::max (post, std::abs (sw.getSample (0, i) - studio.getSample (0, i)));
    check (post < 1e-5f, "after the crossfade the output IS the STUDIO render (" + juce::String (post, 7) + ")");
    check (gAudioAllocs.load() == 0, "no allocation on the audio thread across the switch (" + juce::String (gAudioAllocs.load()) + ")");
    Rig d (drums (48000, 3.0));
    auto dq = ops::stretchTo (d->project(), d.r0().id, 144000 * 1.2); d->apply (*dq, "stretch");
    d->setStudioMode (true); d->waitStudio (20000);
    const auto sbp = d->currentPlan().pieces.empty() ? nullptr : d->studioCache().get (d->currentPlan().pieces[0].studioKey);
    int hot = 0; if (sbp) for (auto h : sbp->hot) hot += h;
    check (sbp != nullptr && hot > 0, "STUDIO renders mark their transients for the safe switch (" + juce::String (hot) + " ms flagged)");
    Rig e (sine (48000, 2.0, 200.0, 0.4)); bool did = false;
    const auto eo = playFor (*e.p, 0, 60000, 256, [&] (int64 done) { if (! did && done >= 20000) { did = true; auto g = ops::setGain (e->project(), e.r0().id, -12); e->apply (*g, "gain"); } });
    check (maxStep (eo) <= maxStep (e->getSource()->audio) * 1.05f, "a gain edit while playing is smoothed per sample (no step)");
    Rig pe (sine (48000, 2.0, 200.0, 0.4)); bool pdid = false;
    const auto peo = playFor (*pe.p, 0, 60000, 256, [&] (int64 done) { if (! pdid && done >= 20000) { pdid = true; Project n = pe->project(); n.pitchSemis = 2; pe->apply (n, "pitch"); } });
    check (maxStep (peo) <= maxStep (pe->getSource()->audio) * 2.0f,   // (two different signals summed during the equal-power fade)
           "a PITCH change while playing crossfades DIRECT -> LIVE (no click; step " + juce::String (maxStep (peo), 4) + " at " + juce::String (maxStepAt (peo)) + ")");
    Rig l (sine (48000, 2.0, 210.0, 0.4)); l->setLoop (10000, 30000, true);
    const auto lo = playFor (*l.p, 10000, 60000, 256);
    check (maxStep (lo) <= maxStep (l->getSource()->audio) * 1.2f, "a loop wraps without a click (" + juce::String (maxStep (lo), 4) + ")");
    Rig ab (sine (48000, 1.0, 200.0, 0.4)); auto abq = ops::setReverse (ab->project(), ab.r0().id, true); ab->apply (*abq, "rev"); ab->setCompareOriginal (true);
    const auto abo = playFor (*ab.p, 0, 20000, 256);
    float abErr = 0; for (int i = 100; i < 20000; ++i) abErr = std::max (abErr, std::abs (abo.getSample (0, i) - ab->getSource()->audio.getSample (0, i)));
    check (abErr < 1e-6f, "A/B: B plays the original source (the edit is untouched)");
    // a suspended host (no audio callbacks): old play tables don't pile up; playing again is safe
    Rig g (sine (48000, 2.0, 200.0, 0.4)); playFor (*g.p, 0, 4800, 256);
    { g->setCursor (24000); g->play (true); juce::AudioBuffer<float> b (2, 256); juce::MidiBuffer m; g->processBlock (b, m); }   // a voice holds the current table
    for (int i = 0; i < 20; ++i) g->apply (*ops::setGain (g->project(), g.r0().id, -0.5 * (i + 1)), "gain");
    const auto kept = g->getPlayer().tablesKept();
    pump (1100); g->apply (*ops::setGain (g->project(), g.r0().id, -11.0), "gain"); g->apply (*ops::setGain (g->project(), g.r0().id, -12.0), "gain");
    const auto after = g->getPlayer().tablesKept();
    const auto resumed = playFor (*g.p, 0, 9600, 256);
    check (kept > 10 && after <= 2 && maxStep (resumed) < 0.05f, "suspended audio: " + juce::String ((int) kept) + " tables pile up, after 1 s " + juce::String ((int) after) + " are kept; playing again is clean");
}

// ================================================================ [10] undo / redo
static void testUndo()
{
    section ("[10] undo / redo (snapshots)");
    SourceRef ref; ref.fingerprint = "u"; ref.sampleRate = 48000; ref.frames = 480000; ref.channels = 2;
    History h; Project p0 = makeProject (ref); h.reset (p0);
    std::mt19937 rng (5); std::vector<Project> states { p0 };
    for (int i = 0; i < 1000; ++i) { juce::String l; auto n = randomEdit (h.current(), rng, l); if (! (n == h.current())) { h.push (n, l); states.push_back (n); } }
    const size_t steps = std::min<size_t> (states.size() - 1, 200);
    bool ok = true; for (size_t k = 0; k < steps; ++k) { h.undo(); if (! (h.current() == states[states.size() - 2 - k])) ok = false; }
    check (ok && steps == 200 && ! h.canUndo(), "undo walks back exactly (" + juce::String ((int) steps) + " steps kept of " + juce::String ((int) states.size() - 1) + " edits)");
    for (size_t k = 0; k < steps; ++k) h.redo();
    check (h.current() == states.back() && ! h.canRedo(), "redo comes back to the last state");
    History g; g.reset (p0); g.beginGesture(); Project x = p0;
    for (int i = 1; i <= 10; ++i) { x = *ops::moveRegion (x, x.regions[0].id, i * 1000.0); g.push (x, "drag"); }
    g.endGesture();
    const bool one = g.undoCount() == 1; g.undo();
    check (one && g.current() == p0, "one drag = one undo step");
    Rig r (sine (48000, 1.0, 100, 0.3)); const auto f0 = r->currentPlan().fingerprint;
    r->apply (*ops::setReverse (r->project(), r.r0().id, true), "rev");
    const auto f1 = r->currentPlan().fingerprint; r->undo(); const auto f2 = r->currentPlan().fingerprint; r->redo();
    check (f1 != f0 && f2 == f0 && r->currentPlan().fingerprint == f1, "processor undo / redo recompile the same plans");
}

// ================================================================ [11] project files, relink, host state
static void testProjects()
{
    section ("[11] project save / load, relink by fingerprint, DAW state, sample-rate change");
    const auto dir = tmpDir().getChildFile ("proj"); dir.createDirectory();
    const auto audio = dir.getChildFile ("media/loop.wav"); audio.getParentDirectory().createDirectory();
    juce::WavAudioFormat wav; writeAudio (drums (48000, 2.0), 48000, audio, wav, 24);
    SliceProcessor p; p.setPlayConfigDetails (0, 2, 48000, 512); p.prepareToPlay (48000, 512);
    juce::String err; check (p.loadAudio (audio, err), "load a WAV into the processor");
    p.apply (*ops::splitAt (p.project(), p.project().regions[0].id, 30000), "cut");
    p.apply (*ops::setGain (p.project(), p.project().regions[1].id, -4), "gain");
    const auto loc = dir.getChildFile ("Edit.ghostslice");
    check (p.saveProject (loc, err) && loc.getChildFile ("project.json").existsAsFile(), "saved as a .ghostslice folder (project.json)");
    check (projectio::cacheDirOf (loc).getChildFile ("analysis").getChildFile (p.getSource()->fingerprint).getChildFile ("peaks-v1.bin").existsAsFile(), "the peak cache sits next to it (delete-safe)");
    const auto json = loc.getChildFile ("project.json").loadFileAsString();
    check (json.contains (p.getSource()->fingerprint) && json.contains ("media/loop.wav"), "the audio is referenced by SHA-256 + relative path (never copied)");
    SliceProcessor q; q.setPlayConfigDetails (0, 2, 48000, 512); q.prepareToPlay (48000, 512);
    Project want = p.project(); want.source.path = audio.getFullPathName(); want.source.relPath = "media/loop.wav"; want.source.fileName = "loop.wav";
    check (q.openProject (loc, err) && q.currentPlan().fingerprint == p.currentPlan().fingerprint && q.project().regions == p.project().regions, "open: the same model, the same plan");
    const auto moved = dir.getChildFile ("elsewhere/renamed.wav"); moved.getParentDirectory().createDirectory(); audio.moveFileTo (moved);
    SliceProcessor r; r.setPlayConfigDetails (0, 2, 48000, 512); r.prepareToPlay (48000, 512);
    check (r.openProject (loc, err) && r.audioFile() == moved, "moved + renamed audio is found by its fingerprint (" + moved.getFileName() + ")");
    const auto far = tmpDir().getChildFile ("far"); far.createDirectory(); const auto farFile = far.getChildFile ("x.wav"); moved.moveFileTo (farFile);
    SliceProcessor s; s.setPlayConfigDetails (0, 2, 48000, 512); s.prepareToPlay (48000, 512);
    const bool missing = ! s.openProject (loc, err) && err == "relink" && s.needsRelink() && s.project().regions == p.project().regions;
    check (missing, "missing audio: the model still loads, RELINK is asked for");
    const auto other = far.getChildFile ("other.wav"); writeAudio (sine (48000, 1.0, 300, 0.3), 48000, other, wav, 16);
    check (! s.relinkAudio (other, err) && err.contains ("different"), "relinking a different recording is refused");
    check (s.relinkAudio (farFile, err) && ! s.needsRelink() && s.currentPlan().fingerprint == p.currentPlan().fingerprint, "relinking the original restores the edit");
    const auto single = dir.getChildFile ("Edit.gslice.json");
    check (s.saveProject (single, err) && single.existsAsFile() && projectio::cacheDirOf (single).isDirectory(), "single JSON + sidecar cache folder");
    juce::MemoryBlock mb; s.uiTheme = 2; s.getStateInformation (mb);
    SliceProcessor t; t.setPlayConfigDetails (0, 2, 48000, 512); t.prepareToPlay (48000, 512);
    t.setStateInformation (mb.getData(), (int) mb.getSize());
    for (int i = 0; i < 300 && t.isLoading(); ++i) pump (10);
    check (t.project().regions == s.project().regions && t.getSource() != nullptr && t.uiTheme.load() == 2 && t.currentPlan().fingerprint == s.currentPlan().fingerprint, "the DAW session restores the model, the audio and the look");
    ghosty::License::testForce() = 0;
    SliceProcessor u; u.setPlayConfigDetails (0, 2, 48000, 512); u.prepareToPlay (48000, 512); u.setStateInformation (mb.getData(), (int) mb.getSize());
    const bool demoEmpty = u.project().regions.empty();
    ghosty::License::testForce() = 1; u.unlockedNow(); for (int i = 0; i < 300 && u.isLoading(); ++i) pump (10);
    ghosty::License::testForce() = -1;
    check (demoEmpty && u.project().regions == s.project().regions, "demo: a reopened session starts empty; UNLOCK brings it back");
    SliceProcessor v; v.setPlayConfigDetails (0, 2, 48000, 512); v.prepareToPlay (48000, 512); v.loadAudio (farFile, err);
    const auto len48 = v.currentPlan().length;
    v.prepareToPlay (96000, 512); pump (80);
    check (v.currentPlan().hostRate == 96000 && v.currentPlan().length == len48 * 2 && v.getWorking()->rate == 96000, "a 96 kHz host: the working copy and the plan follow (" + juce::String (v.currentPlan().length) + " frames)");
}

// ================================================================ [12] export
static void testExport()
{
    section ("[12] export WAV (STUDIO render) + MIDI of the slices");
    Rig r (drums (48000, 2.0));
    auto q = ops::splitRange (r->project(), 24000, 48000); q = ops::stretchTo (*q, q->regions[1].id, 30000); r->apply (*q, "edit");
    juce::String err; const auto f = tmpDir().getChildFile ("out.wav"), m = tmpDir().getChildFile ("out.mid");
    check (r->exportWav (f, err) && f.existsAsFile(), "WAV written");
    std::unique_ptr<juce::AudioFormatReader> rd (source::formats().createReaderFor (f));
    const auto ref = r->renderArrangement();
    bool same = rd != nullptr && rd->lengthInSamples == ref.getNumSamples() && rd->sampleRate == 48000;
    float e = 1;
    if (same) { juce::AudioBuffer<float> b (2, (int) rd->lengthInSamples); rd->read (&b, 0, b.getNumSamples(), 0, true, true); e = 0; for (int i = 0; i < b.getNumSamples(); ++i) e = std::max (e, std::abs (b.getSample (0, i) - ref.getSample (0, i))); same = e < 1.0e-6f; }
    check (same, "the WAV is the STUDIO render (24-bit, error " + juce::String (e, 8) + ")");
    check (r->exportMidi (m, err) && m.existsAsFile(), "MIDI written");
    juce::MidiFile mf; juce::FileInputStream in (m); mf.readFrom (in);
    int notes = 0; double second = -1;
    if (mf.getNumTracks() > 0) for (auto* ev : *mf.getTrack (0)) if (ev->message.isNoteOn()) { if (notes == 1) second = ev->message.getTimeStamp(); ++notes; }
    check (notes == (int) r->currentPlan().pieces.size() && std::abs (second - 960.0 * 2 * 0.5) < 1.5, "one note per slice at its position (" + juce::String (notes) + " notes, the 2nd at tick " + juce::String (second) + ")");
    ghosty::License::testForce() = 0;
    check (! r->exportWav (tmpDir().getChildFile ("locked.wav"), err) && ! tmpDir().getChildFile ("locked.wav").exists(), "locked: the processor writes nothing (back-stop)");
    ghosty::License::testForce() = -1;
}

// ================================================================ [13] UI
static juce::MouseEvent mouseAt (juce::Component& c, juce::Point<float> p, juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier, int clicks = 1, juce::Point<float> downAt = { -1, -1 })
{
    if (downAt.x < 0) downAt = p;
    return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p, mods, 1.0f, 0, 0, 0, 0, &c, &c, juce::Time::getCurrentTime(), downAt, juce::Time::getCurrentTime(), clicks, false);
}
static void snap (juce::Component& c, const juce::File& f, float scale = 1.0f)
{
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, scale);
    f.getParentDirectory().createDirectory(); f.deleteFile();
    juce::FileOutputStream os (f); juce::PNGImageFormat png; png.writeImageToStream (img, os);
}

static void alignShots (const juce::File& shots);   // (SliceAnalysisTests.inc)
static void pitchShots (const juce::File& shots);
static void testUi (const juce::File& shots)
{
    section ("[13] UI: placement + contrast in BLACK / PURPLE / LIGHT, gestures, screenshots");
    using namespace ghosty::themes;
    for (auto t : { Theme::Dark, Theme::Purple, Theme::Light })
    {
        const auto P = gslice::ui::palette (t);
        double worstText = 99, worstMark = 99;
        for (auto bg : { P.window, P.panel, P.header, P.card }) for (auto fg : { P.ink, P.dim }) worstText = std::min (worstText, contrast (fg, bg));
        worstText = std::min (worstText, contrast (P.ink, P.cell)); worstText = std::min (worstText, contrast (P.onAccent, P.wave));
        for (auto fg : { P.wave, P.marker, P.pitch, P.warn, P.playhead }) worstMark = std::min (worstMark, contrast (fg, P.waveBg));
        worstMark = std::min (worstMark, contrast (P.warn, P.region));
        check (worstText >= 4.5 && worstMark >= 3.0, displayName (t) + ": text >= 4.5:1 (worst " + juce::String (worstText, 2) + "), waveform / markers / pitch / amber >= 3:1 (worst " + juce::String (worstMark, 2) + ")");
        check (contrast (readableOn (P.dim, P.cell, 3.0), P.cell) >= 3.0, displayName (t) + ": greyed buttons stay readable (>= 3:1)");
    }
    Rig rig (drums (48000, 4.0));
    auto q = ops::splitAt (rig->project(), rig.r0().id, 96000); q = ops::stretchTo (*q, q->regions[1].id, q->regions[1].length() * 0.45);
    int64 mk = 0; q = ops::addMarker (*q, q->regions[0].id, 36000, &mk); q = ops::dragMarker (*q, q->regions[0].id, mk, 40000, FlexMode::Move); q = ops::setAnchor (*q, q->regions[0].id, mk, true);
    q = ops::setFades (*q, q->regions[0].id, 60, 120);
    rig->apply (*q, "ui");
    std::unique_ptr<SliceEditor> ed (static_cast<SliceEditor*> (rig->createEditor()));
    ed->setVisible (true);
    for (auto size : { juce::Point<int> (1160, 700), juce::Point<int> (1600, 1000) })
    {
        ed->setSize (size.x, size.y); ed->resized(); pump (20);
        for (int ws = 0; ws < 3; ++ws)
        {
            ed->setWorkspace ((gslice::ui::Workspace) ws);
            int out = 0, overlap = 0, tight = 0;
            std::vector<juce::Component*> vis;
            for (auto* c : ed->getChildren()) if (c->isVisible() && c != &ed->view && c != &ed->lane && dynamic_cast<ghosty::exportgate::Hint*> (c) == nullptr && dynamic_cast<juce::TooltipWindow*> (c) == nullptr && dynamic_cast<juce::ResizableCornerComponent*> (c) == nullptr) vis.push_back (c);
            for (auto* c : vis) if (! ed->getLocalBounds().contains (c->getBounds())) { ++out; std::printf ("    outside: %s\n", c->getComponentID().toRawUTF8()); }
            for (size_t i = 0; i < vis.size(); ++i) for (size_t j = i + 1; j < vis.size(); ++j) if (vis[i]->getBounds().intersects (vis[j]->getBounds())) { ++overlap; std::printf ("    overlap: %s / %s\n", vis[i]->getComponentID().toRawUTF8(), vis[j]->getComponentID().toRawUTF8()); }
            for (auto* c : vis) if (auto* b = dynamic_cast<juce::TextButton*> (c)) { const auto f = ed->lnf.getTextButtonFont (*b, b->getHeight()); if (ghosty::textWidth (f, b->getButtonText()) > (float) b->getWidth() - 6) { ++tight; std::printf ("    text too wide: %s\n", b->getButtonText().toRawUTF8()); } }
            check (out == 0 && overlap == 0 && tight == 0, juce::String (size.x) + "x" + juce::String (size.y)  + (ws == 0 ? " EDIT" : ws == 1 ? " FLEX" : " PITCH") + ": " + juce::String ((int) vis.size()) + " controls inside, no overlaps, every label fits");
        }
    }
    ed->setSize (1280, 780); ed->resized(); ed->setWorkspace (gslice::ui::Workspace::Edit); pump (20);
    auto& v = ed->view; v.zoomToFit(); pump (10);
    {
        const auto r1 = rig->project().regions[1]; const auto R = v.regionRect (r1);
        const auto p0 = juce::Point<float> (R.getX() + 20, R.getY() + 6), p1 = p0.translated (60, 0);
        v.mouseDown (mouseAt (v, p0)); v.mouseDrag (mouseAt (v, p1, juce::ModifierKeys::leftButtonModifier, 1, p0)); v.mouseUp (mouseAt (v, p1, {}, 1, p0));
        const auto* m1 = rig->project().find (r1.id);
        const double step = rig->project().gridStep();
        check (m1 != nullptr && m1->dstStart > r1.dstStart && std::abs (std::fmod (m1->dstStart, step)) < 1.0 && rig->getHistory().undoLabel() == "Move", "drag the region's top bar = MOVE, snapped to the 1/16 grid (to " + juce::String (m1 ? m1->dstStart : 0) + ")");
        rig->undo();
        const auto r0 = rig->project().regions[0]; v.sel.regionId = r0.id;
        const auto fh = v.fadeHandle (r0, true).getCentre(), fh2 = fh.translated (40, 0);
        v.mouseDown (mouseAt (v, fh)); v.mouseDrag (mouseAt (v, fh2, juce::ModifierKeys::leftButtonModifier, 1, fh)); v.mouseUp (mouseAt (v, fh2, {}, 1, fh));
        check (rig->project().regions[0].fadeInMs > 60.0, "drag the fade square = FADE IN (" + juce::String (rig->project().regions[0].fadeInMs, 1) + " ms)");
        const auto wa = v.waveArea().toFloat();
        const auto rp = juce::Point<float> (wa.getX() + 300, wa.getCentreY());
        v.mouseDown (mouseAt (v, rp, juce::ModifierKeys::rightButtonModifier)); v.mouseUp (mouseAt (v, rp, juce::ModifierKeys::rightButtonModifier));
        check (std::abs ((double) rig->cursor() - v.hfAt (rp.x)) < v.hfPerPx + 1, "right-click = the cursor here (the house waveform gesture)");
        const auto ra = juce::Point<float> (wa.getX() + 200, wa.getCentreY()), rb = ra.translated (150, 0);
        v.mouseDown (mouseAt (v, ra, juce::ModifierKeys::rightButtonModifier)); v.mouseDrag (mouseAt (v, rb, juce::ModifierKeys::rightButtonModifier, 1, ra)); v.mouseUp (mouseAt (v, rb, juce::ModifierKeys::rightButtonModifier, 1, ra));
        check (rig->looping() && rig->loopRange().second > rig->loopRange().first, "right-drag = a LOOP over the span");
        rig->setLoop (0, 0, false);
        const auto ga = juce::Point<float> (v.regionRect (rig->project().regions[0]).getX() + 60, wa.getCentreY() + 30), gb = ga.translated (120, 0);
        v.mouseDown (mouseAt (v, ga)); v.mouseDrag (mouseAt (v, gb, juce::ModifierKeys::leftButtonModifier, 1, ga)); v.mouseUp (mouseAt (v, gb, {}, 1, ga));
        check (v.sel.hasRange(), "left-drag on the waveform selects a range");
        const auto nRegions = rig->project().regions.size();
        check (v.cutAtCursorOrSelection() && rig->project().regions.size() == nRegions + 2, "CUT the selection = split at both edges");
        rig->undo(); v.sel.a = v.sel.b = 0;
        ed->setWorkspace (gslice::ui::Workspace::Flex);
        const auto nBefore = rig->project().regions[0].warp.size();
        const auto dp = juce::Point<float> (v.xOf (rig->pfToHf (12000)), wa.getCentreY());
        v.mouseDoubleClick (mouseAt (v, dp, {}, 2));
        check (rig->project().regions[0].warp.size() == nBefore + 1, "FLEX: double-click adds a warp marker");
        const auto w1 = rig->project().regions[0].warp[1];
        const auto mp = juce::Point<float> (v.xOf (v.markerAbsHf (rig->project().regions[0], w1)), wa.getY() + 40), mp2 = mp.translated (25, 0);
        const double t0 = w1.t; const auto id1 = w1.id;
        v.mouseDown (mouseAt (v, mp, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier))); v.mouseDrag (mouseAt (v, mp2, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::altModifier), 1, mp)); v.mouseUp (mouseAt (v, mp2, {}, 1, mp));
        double t1 = t0; for (auto& w : rig->project().regions[0].warp) if (w.id == id1) t1 = w.t;
        check (t1 > t0 && v.sel.markerId == id1 && rig->getHistory().undoLabel() == "Marker", "FLEX: drag a marker (" + juce::String (t0, 0) + " -> " + juce::String (t1, 0) + "), one undo step");
        check (v.toggleAnchor() && v.bpmFromMarkers(), "FLEX: ANCHOR the marker, TAP BPM from two markers (" + juce::String (rig->project().bpm, 2) + ")");
        ed->setWorkspace (gslice::ui::Workspace::Edit);
        auto& lane = ed->lane; const auto bl = lane.blocks();
        if (bl.size() >= 2)
        {
            Project noAnchor = rig->project(); for (auto& r : noAnchor.regions) for (auto& w : r.warp) w.anchor = false; rig->apply (noAnchor, "unanchor");
            const auto a = bl[0].second.getCentre(), b = juce::Point<float> (bl[1].second.getRight() - 2, a.y);
            const auto idFirst = bl[0].first;
            lane.mouseDown (mouseAt (lane, a)); lane.mouseDrag (mouseAt (lane, b, juce::ModifierKeys::leftButtonModifier, 1, a)); lane.mouseUp (mouseAt (lane, b, {}, 1, a));
            const auto bl2 = lane.blocks();
            check (bl2.size() == bl.size() && bl2.back().first == idFirst, "lane strip: drag block 1 past block 2 = REARRANGE");
            rig->undo(); rig->undo();
        }
        // keys
        const auto n0 = rig->project().regions.size(); rig->setCursor ((int64) rig->pfToHf (20000));
        ed->keyPressed (juce::KeyPress ('s')); const bool cutKey = rig->project().regions.size() == n0 + 1;
        ed->keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)); const bool undoKey = rig->project().regions.size() == n0;
        check (cutKey && undoKey, "keys: S = cut at the cursor, Cmd/Ctrl+Z = undo");
    }
    for (auto t : { Theme::Dark, Theme::Purple, Theme::Light })
    {
        ed->setTheme (t);
        const auto nm = displayName (t).toLowerCase();
        ed->setWorkspace (gslice::ui::Workspace::Edit); v.sel.regionId = rig->project().regions[0].id; v.sel.markerId = 0; v.sel.a = 30000; v.sel.b = 60000; rig->setCursor (90000); ed->refresh(); pump (20);
        snap (*ed, shots.getChildFile ("slice-" + nm + "-edit.png"));
        ed->setWorkspace (gslice::ui::Workspace::Flex); v.sel.a = v.sel.b = 0; v.sel.markerId = rig->project().regions[0].warp[1].id; v.sel.markerRegion = rig->project().regions[0].id; ed->refresh(); pump (20);
        snap (*ed, shots.getChildFile ("slice-" + nm + "-flex.png"));
        ed->setWorkspace (gslice::ui::Workspace::Pitch); pump (10);
        snap (*ed, shots.getChildFile ("slice-" + nm + "-pitch.png"));
    }
    ed->setTheme (Theme::Dark); ed->setWorkspace (gslice::ui::Workspace::Edit);
    alignShots (shots);
    pitchShots (shots);
    v.hfPerPx = 0.35; v.viewStart = 150; v.repaint(); pump (10);   // inside the first hit: the samples as a line
    snap (*ed, shots.getChildFile ("slice-black-zoom-samples.png"));
    v.zoomToFit(); pump (10);
    snap (*ed, shots.getChildFile ("slice-black-edit-hidpi.png"), 2.0f);
    {
        SliceProcessor empty; empty.setPlayConfigDetails (0, 2, 48000, 512); empty.prepareToPlay (48000, 512);
        std::unique_ptr<juce::AudioProcessorEditor> ee (empty.createEditor()); ee->setVisible (true); pump (20);
        snap (*ee, shots.getChildFile ("slice-black-empty.png"));
    }
    check (shots.getChildFile ("slice-light-flex.png").existsAsFile() && shots.getChildFile ("slice-black-edit-hidpi.png").existsAsFile(), "screenshots written to " + shots.getFullPathName());
    ed.reset(); pump (20);
}

#include "PlayTests.inc"
#include "RepeatTests.inc"
#include "HoldTests.inc"

// ================================================================ demogate
static constexpr int kSliceInventory = 4;
static int demogateTest()
{
    ghosty::demogate::Sandbox box;
    Rig rig (drums (48000, 2.0));
    std::unique_ptr<SliceEditor> ed (static_cast<SliceEditor*> (rig->createEditor())); ed->setVisible (true); pump (50);
    std::vector<ghosty::demogate::Path> paths {
        { "slice.export.wav", ghosty::demogate::File, [&] { ed->exportWavBtn.triggerClick(); pump (30); } },
        { "slice.export.midi", ghosty::demogate::File, [&] { ed->exportMidiBtn.triggerClick(); pump (30); } },
        { "slice.export.playmidi", ghosty::demogate::File, [&] { ed->setWorkspace (gslice::ui::Workspace::Play); ed->playPanel.exportMappingClicked(); pump (30); } },
        { "slice.drag.padwav", ghosty::demogate::Drag, [&] { ed->setWorkspace (gslice::ui::Workspace::Play); ed->playPanel.dragPadOut (0); pump (30); } },
    };
    rig->apply (*ops::turnIntoInstrument (rig->project(), (int) rig->currentSlices().size()), "map"); rig->buildBankNow();
    int fails = ghosty::demogate::run ("Ghost Slice", paths, kSliceInventory, box);
    ed.reset(); pump (30);
    fails += ghosty::demogate::checkDropout ("Ghost Slice", [] {
        auto p = std::make_unique<SliceProcessor>(); p->setPlayConfigDetails (0, 2, 48000, 480); p->prepareToPlay (48000, 480);
        p->loadSourceForTests (source::fromBuffer (pad (48000, 30.0), 48000, "pad.wav")); p->play (true);
        return std::unique_ptr<juce::AudioProcessor> (std::move (p)); }, {});
    return fails;
}

#include "SliceAnalysisTests.inc"
#include "PitchEditTests.inc"
#include "FormantTests.inc"
#include "ChopTests.inc"
#include "KeyTests.inc"
#include "PolishTests.inc"
#include "GhostLookTests.inc"
#include "FinishTests.inc"
#include "BenchTests.inc"
#include "HarnessTests.inc"
#include "ChordTests.inc"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    auto home = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("gslice_home"); home.deleteRecursively(); home.createDirectory();
    setenv ("HOME", home.getFullPathName().toRawUTF8(), 1);
    const juce::String mode = argc > 1 ? argv[1] : "all";
    if (mode == "demogate") { const int f = demogateTest(); std::printf ("\nGhostSliceTests demogate: %s\n", f == 0 ? "PASS" : "FAIL"); return f == 0 ? 0 : 1; }
    const juce::File shots (argc > 2 ? juce::String (argv[2]) : juce::String (GSLICE_SHOTS_DIR));
    if (mode == "renders") { writeRenders (juce::File (argc > 2 ? juce::String (argv[2]) : juce::String ("/tmp/gslice-renders"))); return gFail == 0 ? 0 : 1; }
    if (mode == "renderhashes") { renderHashes(); return 0; }
    if (mode == "golden" && argc > 2 && juce::String (argv[2]) == "--update") { updateGolden(); return 0; }
    if (mode == "pluginsmoke") { testPluginSmoke (juce::File (argc > 2 ? juce::String (argv[2]) : juce::String (GSLICE_ARTEFACTS_DIR))); std::printf ("\nGhostSliceTests pluginsmoke: %d passed, %d failed\n", gPass, gFail); return gFail == 0 ? 0 : 1; }
    if (mode == "polyproto") { polyPrototype(); return 0; }
    if (mode == "chordfile" && argc > 2)
    {   // the chord track + key of an audio file (a listening / labelling aid for real material)
        juce::String err; auto src = source::load (juce::File (juce::String (argv[2])), err);
        if (src == nullptr) { std::printf ("cannot load: %s\n", err.toRawUTF8()); return 1; }
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        auto r = analysis::analyse (*src, analysis::Material::Melodic, {});
        std::printf ("%s: key %s (notes only %s), %.1f s analysed in %.0f ms\n", argv[2], r->key.readout().toRawUTF8(), r->noteKey.readout().toRawUTF8(),
                     src->audio.getNumSamples() / src->sampleRate, juce::Time::getMillisecondCounterHiRes() - t0);
        for (auto& c : r->chords->chords) std::printf ("  %7.2f - %7.2f  %-6s %3d %%\n", c.s0 / src->sampleRate, c.s1 / src->sampleRate, c.name().toRawUTF8(), (int) std::lround (c.confidence * 100));
        return 0;
    }
    if (mode == "bench") { runBench (juce::File (argc > 2 ? juce::String (argv[2]) : juce::String ("/root/work/gslice-bench"))); return 0; }
    if (mode == "formantlab") { formantLab(); realVocalCheck (false); return 0; }
    if (mode == "formantrenders") { writeFormantRenders (juce::File (argc > 2 ? juce::String (argv[2]) : juce::String ("/root/work/wt-slice-formant-renders"))); return 0; }
    if (mode == "shots") { testUi (shots); testPlayUi (shots); testRepeatUi (shots); testHoldUi (shots); testNotesUi (shots); testChopUi (shots); testPolishUi (shots); testChordUi (shots); return gFail == 0 ? 0 : 1; }
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    auto want = [&] (const char* g) { return mode == "all" || mode == g; };
    if (want ("load")) testLoad();
    if (want ("peaks")) testPeaks();
    if (want ("working")) testWorking();
    if (want ("json")) testJson();
    if (want ("determinism")) testDeterminism();
    if (want ("ops")) testOps();
    if (want ("warp")) testWarp();
    if (want ("stretch")) testStretch();
    if (want ("live")) testLive();
    if (want ("undo")) testUndo();
    if (want ("projects")) testProjects();
    if (want ("export")) testExport();
    if (want ("onsets")) testOnsets();
    if (want ("tempo")) testTempo();
    if (want ("align")) testAlign();
    if (want ("pitch")) testPitch();
    if (want ("key")) testKey();
    if (want ("gpitch")) testGlobalPitch();
    if (want ("notes")) testNotes();
    if (want ("formant")) { testFormant(); realVocalCheck (true); }
    if (want ("chop")) testChop();
    if (want ("keymodes")) testKeyModes (shots);
    if (want ("polish")) testPolish();
    if (want ("play")) testPlay();
    if (want ("repeat")) testRepeat();
    if (want ("hold")) testHold();
    if (want ("tempo2")) testTempoOctave();
    if (want ("bounce5")) testBounce5();
    if (want ("resample")) testResample();
    if (want ("elastic")) testElasticJoin();
    if (want ("syllables")) testSyllables();
    if (want ("regionpitch")) testRegionPitch();
    if (want ("cliphint")) testClipHint();
    if (want ("golden")) testGolden();
    if (want ("proptest")) testPropertyJson();
    if (want ("srmatrix")) testSrMatrix();
    if (want ("rtsafety")) testRtSafety();
    if (want ("blocktime")) testBlockTime();
    if (want ("chords")) { testChords(); testChordAnalysis(); }
    if (want ("ui")) testUi (shots);
    if (want ("ui") || mode == "playui") testPlayUi (shots);
    if (want ("ui") || mode == "repeatui") testRepeatUi (shots);
    if (want ("ui") || mode == "holdui") testHoldUi (shots);
    if (want ("ui") || mode == "notesui") testNotesUi (shots);
    if (want ("ui") || mode == "formantui") testFormantUi();
    if (want ("ui") || mode == "chopui") testChopUi (shots);
    if (want ("ui") || mode == "polishui") testPolishUi (shots);
    if (want ("ui") || mode == "ghostui") testGhostUi (shots);
    if (want ("ui") || mode == "repeatwave") testRepeatWave (shots);
    if (want ("ui") || mode == "chordsui") testChordUi (shots);
    std::printf ("\nGhostSliceTests: %d passed, %d failed (%.1f s)\n", gPass, gFail, (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0);
    tmpDir().deleteRecursively();
    return gFail == 0 ? 0 : 1;
}
