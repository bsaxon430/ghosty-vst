// Ghost Mosaic studio: the tap tests (group `tap`, both test binaries). Included by MosaicTests.cpp.
// Owned by the tap engineer.
//
//   (a) the output is the same, bit for bit, with the tap off, read flat out by another thread, never read
//       (its queue full), and read by the model with its explaining thread running;
//   (b) nothing is allocated on the audio thread with the tap on;
//   (c) a full queue never blocks: records are dropped whole and counted, the hole is marked;
//   (d) the upstream null test still holds with the tap on (the `renderer` and `engine` groups hold it with the tap off);
//   (e) the stream is complete: the replay arrives at the piece the engine played for every pick, its distance
//       is block::compare's to the bit, and its copy of the "used" memory is the engine's to the bit.
#include "TapModel.h"
#include <thread>

namespace taptest
{
// TAP.md example begin
struct NewestPickView
{
    explicit NewestPickView (mosaic::tap::Model& m) : model (m) {}
    juce::String text;                                       // (a real view draws; this one writes)
    void frame()                                             // after model.tick(), on the message thread
    {
        using namespace mosaic::tap;
        text.clear();
        if (! model.active()) { text = model.counters().note; return; }
        if (model.numPicks() == 0) { text = model.transport().whyNot; return; }
        const Pick& p = model.pick (0);                      // 0 = the newest pick
        const LibraryView& lib = model.library();
        text << "piece " << p.libraryPiece << " of \"" << lib.soundName (lib.soundOf (p.libraryPiece)) << "\" at "
             << juce::String (lib.secondsInSound (p.libraryPiece), 2) << " s, for target piece " << p.targetPiece
             << " (" << juce::roundToInt (p.ageSeconds * 1000.0) << " ms ago)\n";
        const Why& w = p.why;
        if (w.state != Why::ready) { text << w.note; return; }   // waiting, skipped or unavailable: it says which
        const ScoreScale& scale = model.scoreScale();        // scores have no fixed scale: draw them through this
        text << "scored " << w.candidates << " pieces; winner " << w.winner.piece << " at " << juce::String (w.winner.total, 4)
             << " (bar " << juce::String (scale.unit (w.winner.total), 2) << "): tone " << juce::String (w.winner.toneShare, 4)
             << " + timbre " << juce::String (w.winner.timbreShare, 4) << " + used " << juce::String (w.winner.usedShare, 4)
             << ", TERRAIN x" << juce::String (w.winner.terrain, 2) << "\n";
        for (int i = 0; i < w.runnerUps; ++i)
            text << "  then piece " << w.runnerUp[i].piece << " at " << juce::String (w.runnerUp[i].total, 4)
                 << " (bar " << juce::String (scale.unit (w.runnerUp[i].total), 2) << ")\n";
        if (w.stickyTested)
            text << (w.stickyPlayed ? "SMOOTH played the following piece " : "SMOOTH kept the winner; the following piece ")
                 << w.stickyPiece << " scored " << juce::String (w.stickyFollowing, 4) << ", limit " << juce::String (w.stickyLimit, 4) << "\n";
        if (w.slideTested)
            text << (w.slideHeld ? "the target waited: " : "the target moved on: ") << juce::String (w.slideError, 1)
                 << " against " << juce::String (w.slideLimit, 1) << "\n";
        if (w.approximate) text << w.note << "\n";            // never hide it: say when a number is not the engine's own
        if (model.counters().cellsLost > 0) text << "(the screen fell behind: " << (juce::int64) model.counters().cellsLost << " cells lost)\n";
    }
    mosaic::tap::Model& model;
};
// TAP.md example end

using namespace mosaic;

static void info (const juce::String& s) { std::printf ("INFO %s\n", s.toRawUTF8()); std::fflush (stdout); }

struct Setup
{
    const char* name = ""; const char* kit = "drums"; const char* tgt = "vowels";
    int algo = 0; float novelty = 0, sticky = 0, ratio = 0.5f, nRatio = 0, autotune = 0, nMix = 0, targetMix = 0;
    int stretch = 1, synapses = 20, fft0 = 0, fft1 = 100; float slide = 1000; bool stereo = false;
    int terrain = 0;                           // 1: a band ridge, a ridge across time and a mountain to one side
    float dry = 0;                             // (the effect)
    float tinySeconds = 0;                     // > 0: the library is one made-up sound this long (a handful of pieces: the searches cost almost nothing)
    int block = 1000; float overlap = 0.75f; int window = 6;
};

// one plug-in, built and ready, fed buffer by buffer on this thread (the tests' audio thread)
struct Rig
{
    Rig (const Setup& setup, double sampleRate, int bufferSize, bool buildNow = true)
        : s (setup), sr (sampleRate), bufN (bufferSize), io (2, bufferSize)
    {
        setP (p, "auto_build", 0); setP (p, "declick", 0); setP (p, "limiter", 0);
        setP (p, "algo", (float) s.algo); setP (p, "novelty", s.novelty); setP (p, "stickyness", s.sticky);
        setP (p, "ratio", s.ratio); setP (p, "n_ratio", s.nRatio); setP (p, "autotune", s.autotune); setP (p, "n_mix", s.nMix);
        setP (p, "target_mix", s.targetMix); setP (p, "stretch", (float) s.stretch); setP (p, "synapses", (float) s.synapses);
        setP (p, "fft_start", (float) s.fft0); setP (p, "fft_end", (float) s.fft1); setP (p, "slide_error", s.slide);
        setP (p, "stereo", s.stereo ? 1.0f : 0.0f); setP (p, "volume", 0.3f);
        setP (p, "src_block", (float) s.block); setP (p, "tgt_block", (float) s.block); setP (p, "src_overlap", s.overlap); setP (p, "tgt_overlap", s.overlap);
        setP (p, "src_window", (float) s.window); setP (p, "tgt_window", (float) s.window);
       #if GHOSTMOSAIC_FX
        setP (p, "dry", s.dry);
        p.prepareToPlay (sr, bufN);
        input = demo::vowelPhrase (sr, 6);
       #else
        p.engine().prepare (sr, bufN);
       #endif
        if (buildNow) build();
    }
    void build()
    {
        if (s.tinySeconds > 0)
        {
            juce::AudioBuffer<float> tiny (1, (int) (sr * s.tinySeconds));
            juce::Random rnd (2024);
            for (int i = 0; i < tiny.getNumSamples(); ++i) tiny.setSample (0, i, 0.4f * (float) std::sin (i * 0.05 * (1.0 + i * 0.0002)) + 0.1f * (rnd.nextFloat() - 0.5f));
            p.engine().addMemorySound ("tiny", tiny, sr);
        }
       #if GHOSTMOSAIC_FX
        if (s.tinySeconds <= 0) p.loadKit (s.kit);
        p.engine().generateLibrary(); waitIdle (p);
       #else
        if (s.tinySeconds <= 0) for (auto& snd : demo::kit (s.kit, sr)) p.engine().addMemorySound (snd.name, snd.audio, sr);
        p.loadDemoTarget (s.tgt);
        p.engine().generateLibrary(); p.engine().generateTarget(); waitIdle (p);
       #endif
        if (s.terrain == 1)
        {
            ghost::terrain::Terrain land = tland::bandRidge (700);
            land.add ({ 0.5f, 0.55f, 0.9f, ghost::terrain::kMaxWidth, 0.08f, 0.0f });   // a ridge across time: NOVELTY rises there
            land.add ({ 0.8f, 0.3f, 0.8f, 0.12f, 0.2f, 0.0f });                          // a mountain towards the bright side
            p.terrain.set (land);
        }
    }
    void start (unsigned seed = 4711) { std::srand (seed); p.engine().play(); }
    template <class Each> void run (int buffers, Each&& each)
    {
        for (int b = 0; b < buffers; ++b)
        {
           #if GHOSTMOSAIC_FX
            const int n = input.getNumSamples();
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < bufN; ++i) io.setSample (c, i, input.getSample (juce::jmin (c, input.getNumChannels() - 1), (pos + i) % n));
            pos += bufN;
            const auto t0 = timing ? juce::Time::getHighResolutionTicks() : 0;
            p.processBlock (io, midi);
            if (timing) timedTicks += juce::Time::getHighResolutionTicks() - t0;
            if (auto* lt = p.engine().liveTarget()) lt->waitIdle (2000);         // (the test runs faster than real time)
           #else
            io.clear();
            const auto t0 = timing ? juce::Time::getHighResolutionTicks() : 0;
            p.engine().process (io, midi, false, false);
            if (timing) timedTicks += juce::Time::getHighResolutionTicks() - t0;
           #endif
            l.insert (l.end(), io.getReadPointer (0), io.getReadPointer (0) + bufN);
            r.insert (r.end(), io.getReadPointer (1), io.getReadPointer (1) + bufN);
            each();
        }
    }
    void run (int buffers) { run (buffers, [] {}); }
    int buffersFor (double seconds) const { return (int) (seconds * sr / bufN); }

    Setup s; double sr; int bufN;
    Processor p;
    juce::AudioBuffer<float> io, input; juce::MidiBuffer midi;
    std::vector<float> l, r; int pos = 0;
    bool timing = false; juce::int64 timedTicks = 0;                             // (measureCost: the process call alone)
};

static bool sameAudio (const Rig& a, const Rig& b) { return a.l.size() == b.l.size() && a.l == b.l && a.r == b.r; }
[[maybe_unused]] static bool bitsEqual (double a, double b) { return std::memcmp (&a, &b, sizeof (double)) == 0 || (std::isnan (a) && std::isnan (b)); }
static bool bitsEqualF (float a, float b) { return std::memcmp (&a, &b, sizeof (float)) == 0; }

// ---------------------------------------------------------------- (c) the queue on its own
static void testQueue()
{
    using namespace mosaic::tap;
    {
        // nobody reading: it fills, then every push is refused at once and counted
        Queue q (1024);
        Match m {}; m.kind = kMatch; Used u {}; u.kind = kUsed;
        Cell rec[2]; std::memcpy (rec, &m, 32); std::memcpy (rec + 1, &u, 32);
        int accepted = 0;
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        const int tries = 5000000;
        for (int i = 0; i < tries; ++i) accepted += q.push (rec, 2, 64) ? 1 : 0;
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        const uint64_t lost = q.lostCells();
        check (accepted > 0 && accepted * 2 <= 1024 - 64 && lost == (uint64_t) (tries - accepted) * 2 && ms < 2000.0,
               "tap queue, nobody reading: it fills, then drops whole records at once and counts every cell",
               juce::String (accepted) + " records in, " + juce::String ((juce::int64) lost) + " cells counted lost, "
               + juce::String (ms * 1.0e6 / tries, 1) + " ns per refused push");
        // the reader comes back: the hole is marked, with the exact count, before the next record
        std::vector<Cell> out (2048);
        const int had = q.read (out.data(), 2048);
        const bool pushed = q.push (rec, 2, 64);
        const int got = q.read (out.data(), 2048);
        const Gap g = [&] { Gap x; std::memcpy (&x, out.data(), 32); return x; }();
        check (had == accepted * 2 && pushed && got == 3 && out[0].kind == kGap && g.lostCells == (uint32_t) lost && out[1].kind == kMatch && out[2].kind == kUsed,
               "tap queue: the hole is marked with the exact number of lost cells before the next record");
    }
    {
        // a reader in another thread, sometimes slow: every cell is either read or counted lost, and no pick is ever torn
        Queue q (4096);
        std::atomic<bool> stop { false };
        std::atomic<uint64_t> readCells { 0 }, torn { 0 }, gapSum { 0 }, order { 0 };
        std::thread reader ([&]
        {
            std::vector<Cell> out (512);
            uint32_t last = 0, wantSrc = 0; bool have = false, wantUsed = false; int spin = 0;
            for (;;)
            {
                const int n = q.read (out.data(), 512);
                if (n == 0) { if (stop.load()) break; std::this_thread::yield(); continue; }
                readCells.fetch_add ((uint64_t) n);
                for (int i = 0; i < n; ++i)
                {
                    const Cell& c = out[(size_t) i];
                    if (wantUsed)
                    {
                        Used u; std::memcpy (&u, &c, 32);
                        if (u.kind != kUsed || u.synapses != wantSrc) torn.fetch_add (1);
                        wantUsed = false;
                    }
                    else if (c.kind == kGap) { Gap g; std::memcpy (&g, &c, 32); gapSum.fetch_add (g.lostCells); }
                    else if (c.kind == kMatch)
                    {
                        Match m; std::memcpy (&m, &c, 32);
                        if (have && m.src <= last) order.fetch_add (1);
                        last = m.src; have = true; wantUsed = true; wantSrc = m.src;
                    }
                    else torn.fetch_add (1);                                     // (its second cell without the first)
                }
                if ((++spin & 63) == 0) std::this_thread::sleep_for (std::chrono::microseconds (300));   // (a slow frame)
            }
            if (wantUsed) torn.fetch_add (1);
        });
        const int tries = 3000000;
        uint64_t in = 0;
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < tries; ++i)
        {
            Match m {}; m.kind = kMatch; m.src = (uint32_t) i + 1;
            Used u {}; u.kind = kUsed; u.synapses = (uint32_t) i + 1;
            Cell rec[2]; std::memcpy (rec, &m, 32); std::memcpy (rec + 1, &u, 32);
            if (q.push (rec, 2, 64)) in += 2;
        }
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        {
            // one last record, so the last hole is marked too
            Match m {}; m.kind = kMatch; m.src = (uint32_t) tries + 1;
            Used u {}; u.kind = kUsed; u.synapses = (uint32_t) tries + 1;
            Cell rec[2]; std::memcpy (rec, &m, 32); std::memcpy (rec + 1, &u, 32);
            while (! q.push (rec, 2, 0)) std::this_thread::yield();
            in += 2;
        }
        stop = true; reader.join();
        const uint64_t lost = q.lostCells();
        check (in + lost >= (uint64_t) tries * 2 + 2 && torn.load() == 0 && order.load() == 0 && gapSum.load() == lost && readCells.load() >= in && lost > 0,
               "tap queue, a reader in another thread: every cell is read or counted lost, picks arrive whole and in order, every hole is marked",
               juce::String ((juce::int64) in) + " cells read, " + juce::String ((juce::int64) lost) + " counted lost (" + juce::String ((juce::int64) gapSum.load()) + " marked), "
               + juce::String ((juce::int64) torn.load()) + " torn, " + juce::String (ms * 1.0e6 / tries, 1) + " ns per pick pushed");
    }
}

// ---------------------------------------------------------------- (a) the same output whatever the tap does
enum Mode { tapOff, tapFlatOut, tapNeverRead, tapModel };

struct ModeRun
{
    ModeRun (const Setup& s, Mode mode, double seconds, double sr = 44100, int bufN = 512) : rig (s, sr, bufN)
    {
        int owner = 0;
        std::atomic<bool> stop { false };
        std::thread reader;
        std::unique_ptr<tap::Model> model;
        auto& t = rig.p.engine().tap();
        rig.start();
        if (mode == tapFlatOut)
        {
            t.attach (&owner); t.wantUsage (true); t.wantLive (true);
            reader = std::thread ([&] { std::vector<tap::Cell> c (1024); while (! stop.load()) cellsRead += (uint64_t) t.read (c.data(), 1024); });
        }
        if (mode == tapNeverRead) { t.attach (&owner); t.wantUsage (true); t.wantLive (true); }
        if (mode == tapModel) { model = std::make_unique<tap::Model> (rig.p.engine()); model->setActive (true); }
        const double dt = bufN / sr;
        rig.run (rig.buffersFor (seconds), [&] { if (model != nullptr) model->tick (dt); });
        stop = true;
        if (reader.joinable()) reader.join();
        lost = t.lostCells();
        late = rig.p.engine().lateBlocks();
        if (model != nullptr) { picks = model->counters().picksSeen; cellsRead = model->counters().cellsRead; model.reset(); }
        if (mode == tapFlatOut || mode == tapNeverRead) t.detach (&owner);
    }
    Rig rig;
    uint64_t lost = 0, cellsRead = 0, picks = 0; juce::int64 late = 0;
};

static void testSameOutput()
{
    std::vector<Setup> cases;
   #if GHOSTMOSAIC_FX
    { Setup s; s.name = "effect, live input, closest"; s.kit = "drums"; cases.push_back (s); }
    { Setup s; s.name = "effect, live input, network, stereo, NOVELTY, SMOOTH, AUTOTUNE, DRY"; s.kit = "tones"; s.algo = 2; s.stereo = true; s.novelty = 0.3f; s.sticky = 0.3f; s.autotune = 0.3f; s.dry = 0.4f; cases.push_back (s); }
   #else
    { Setup s; s.name = "closest, defaults"; cases.push_back (s); }
    { Setup s; s.name = "network, stereo, NOVELTY, SMOOTH"; s.kit = "tones"; s.tgt = "beat"; s.algo = 2; s.stereo = true; s.novelty = 0.2f; s.sticky = 0.2f; cases.push_back (s); }
    { Setup s; s.name = "network + wait, REPEAT 2, AUTOTUNE, FLATTEN, ORIGINAL MIX"; s.kit = "tones"; s.algo = 3; s.slide = 300; s.stretch = 2; s.autotune = 0.4f; s.nMix = 0.5f; s.targetMix = 0.3f; s.synapses = 60; cases.push_back (s); }
    { Setup s; s.name = "closest, TERRAIN shaping everything, NOVELTY"; s.kit = "tones"; s.tgt = "beat"; s.novelty = 0.3f; s.terrain = 1; cases.push_back (s); }
   #endif
    bool firstCase = true;
    for (auto& c : cases)
    {
        // (the first case runs with 32-sample buffers, long enough for the never-read queue to fill and drop)
        const int bufN = firstCase ? 32 : 512;
        const double secs = firstCase ? 8.0 : 5.0;
        ModeRun off (c, tapOff, secs, 44100, bufN), flat (c, tapFlatOut, secs, 44100, bufN), never (c, tapNeverRead, secs, 44100, bufN), model (c, tapModel, secs, 44100, bufN);
        const bool heard = peakOf (off.rig.l) > 1.0e-4;
        const bool same = sameAudio (off.rig, flat.rig) && sameAudio (off.rig, never.rig) && sameAudio (off.rig, model.rig);
        const bool lateSame = off.late == flat.late && off.late == never.late && off.late == model.late;
        const bool filled = ! firstCase || never.lost > 0;
        firstCase = false;
        check (heard && same && off.lost == 0 && filled && model.picks > 0 && model.lost == 0,
               juce::String ("tap off / read flat out / never read / read by the model: identical output (") + c.name + ")",
               juce::String ((int) off.rig.l.size()) + " samples per channel, peak " + juce::String (peakOf (off.rig.l), 4)
               + "; flat out read " + juce::String ((juce::int64) flat.cellsRead) + " cells (" + juce::String ((juce::int64) flat.lost) + " lost)"
               + "; never read lost " + juce::String ((juce::int64) never.lost) + " cells; the model saw " + juce::String ((juce::int64) model.picks) + " picks"
               + (lateSame ? juce::String() : "; LATE LIVE PIECES DIFFER BETWEEN RUNS (" + juce::String (off.late) + " / " + juce::String (flat.late) + " / " + juce::String (never.late) + " / " + juce::String (model.late) + ")"));
    }
}

// ---------------------------------------------------------------- (d) upstream's own renderer, with the tap on
#if ! GHOSTMOSAIC_FX
static void testUpstreamNullWithTap()
{
    const double sr = 44100;
    const int bufN = 2048, buffers = (int) (sr * 8 / bufN);
    Setup s; s.name = "tones -> beat, network, stereo"; s.kit = "tones"; s.tgt = "beat"; s.algo = 2; s.novelty = 0.2f; s.sticky = 0.2f; s.stereo = true; s.window = 0;
    Rig rig (s, sr, bufN);
    juce::StringArray keys; for (auto& snd : rig.p.engine().sounds()) keys.add (snd.key);
    BuildSpec spec { (u32) s.block, s.overlap, (window::type) s.window };
    auto src = upstreamLibrary (keys, spec, false);
    const auto tkey = registerTarget (s.tgt, sr);
    auto tl = upstreamTarget (tkey, brain::LEFT, spec), tr = upstreamTarget (tkey, brain::RIGHT, spec);
    Knobs k; k.algo = s.algo; k.novelty = s.novelty; k.sticky = s.sticky; k.volume = 0.3f * 10; k.boredom = 0.9f;
    std::vector<float> ref, refR;
    {
        renderer L (*src, *tl), R (*src, *tr);
        applyKnobs (L, k); applyKnobs (R, k);
        src->set_usage_falloff (k.boredom);
        std::srand (99);
        L.reset(); R.reset();
        std::vector<float> l ((size_t) bufN), r ((size_t) bufN);
        for (int b = 0; b < buffers; ++b)
        {
            std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f);
            L.process ((u32) bufN, l.data()); R.process ((u32) bufN, r.data());
            ref.insert (ref.end(), l.begin(), l.end()); refR.insert (refR.end(), r.begin(), r.end());
        }
    }
    tap::Model model (rig.p.engine());
    model.setActive (true);
    rig.start (99);
    rig.run (buffers, [&] { model.tick (bufN / sr); });
    const double d = juce::jmax (maxDiff (rig.l, ref), maxDiff (rig.r, refR));
    check (juce::exactlyEqual (d, 0.0) && peakOf (ref) > 1.0e-4 && model.counters().picksSeen > 0,
           juce::String ("null test with the tap on and the model reading, plug-in engine vs upstream engine: ") + s.name,
           nullDetail (d, peakOf (ref)) + ", " + juce::String ((juce::int64) model.counters().picksSeen) + " picks seen");
}
#endif

// ---------------------------------------------------------------- (b) no allocation on the audio thread
static void testNoAllocWithTap()
{
    Setup s; s.name = "alloc"; s.algo = 2; s.stereo = true; s.novelty = 0.2f; s.sticky = 0.3f; s.autotune = 0.3f;
    const double sr = 48000; const int bufN = 256;
    const char* names[] = { "read flat out by another thread", "never read (the queue full)", "read by the model" };
    for (int mode = 0; mode < 3; ++mode)
    {
        Rig rig (s, sr, bufN);
        auto& t = rig.p.engine().tap();
        int owner = 0;
        std::atomic<bool> stop { false };
        std::thread reader;
        std::unique_ptr<tap::Model> model;
        rig.start();
        rig.run (20);                                                            // (the first buffers pick up the library)
        if (mode == 0) { t.attach (&owner); t.wantUsage (true); t.wantLive (true); reader = std::thread ([&] { std::vector<tap::Cell> c (1024); while (! stop.load()) t.read (c.data(), 1024); }); }
        if (mode == 1)
        {
            t.attach (&owner); t.wantUsage (true); t.wantLive (true);
            for (int guard = 0; t.lostCells() == 0 && guard < 400; ++guard) { rig.l.clear(); rig.r.clear(); rig.run (50); }   // (until the queue is full)
        }
        if (mode == 2) { model = std::make_unique<tap::Model> (rig.p.engine()); model->setActive (true); }
        rig.l.clear(); rig.r.clear();
        long n = 0;
        int algo = 0;
        for (int b = 0; b < rig.buffersFor (4.0); ++b)
        {
            if (b % 150 == 0) { setP (rig.p, "algo", (float) (algo++ % 4)); }    // (every search style; the switch itself is outside the count)
           #if GHOSTMOSAIC_FX
            for (int c = 0; c < 2; ++c) for (int i = 0; i < bufN; ++i) rig.io.setSample (c, i, rig.input.getSample (0, (rig.pos + i) % rig.input.getNumSamples()));
            rig.pos += bufN;
            alloccount::start(); rig.p.processBlock (rig.io, rig.midi); n += alloccount::stop();
            if (auto* lt = rig.p.engine().liveTarget()) lt->waitIdle (2000);
           #else
            rig.io.clear();
            alloccount::start(); rig.p.engine().process (rig.io, rig.midi, false, false); n += alloccount::stop();
           #endif
            if (model != nullptr) model->tick (bufN / sr);
        }
        stop = true;
        if (reader.joinable()) reader.join();
        const uint64_t lost = t.lostCells();
        const uint64_t picks = model != nullptr ? model->counters().picksSeen : 0;
        model.reset();
        if (mode < 2) t.detach (&owner);
        check (n == 0 && (mode != 1 || lost > 0) && (mode != 2 || picks > 0), juce::String ("no allocation on the audio thread with the tap on: ") + names[mode],
               juce::String ((int) n) + " allocations in 4 s" + (mode == 1 ? ", " + juce::String ((juce::int64) lost) + " cells dropped" : juce::String()));
    }
}

// ---------------------------------------------------------------- (e) the stream is complete
struct Tally
{
    uint64_t last = 0;
    int picks = 0, ready = 0, exact = 0, agree = 0, skipped = 0, unavailable = 0, waiting = 0, followed = 0, held = 0, liveRows = 0, runnerUpsOk = 0, flagged = 0, wrongButUnflagged = 0;
    int styles[4] = { 0, 0, 0, 0 };
    juce::String firstBad;
    void take (tap::Model& m)
    {
        m.waitUntilExplained (60000);
        m.tick (0);
        for (int i = m.numPicks() - 1; i >= 0; --i)
        {
            const tap::Pick& p = m.pick (i);
            if (p.serial <= last) continue;
            last = p.serial; ++picks;
            if (p.style >= 0 && p.style < 4) ++styles[p.style];
            const tap::Why& w = p.why;
            if (w.state == tap::Why::ready) ++ready; else if (w.state == tap::Why::skipped) ++skipped; else if (w.state == tap::Why::unavailable) ++unavailable; else ++waiting;
            if (w.state == tap::Why::ready && w.agrees) ++agree;
            const bool ok = w.state == tap::Why::ready && w.agrees && ! w.approximate && w.played.piece == p.libraryPiece;
            if (w.state == tap::Why::ready && ! w.approximate && ! (w.agrees && w.played.piece == p.libraryPiece)) ++wrongButUnflagged;
            if (w.state == tap::Why::ready && w.approximate) ++flagged;
            if (ok) ++exact;
            else if (firstBad.isEmpty()) firstBad = "pick " + juce::String ((juce::int64) p.serial) + " (style " + juce::String (p.style) + "): " + w.note;
            if (p.playedFollowing == tap::Pick::yes) ++followed;
            if (! p.targetMovedOn) ++held;
            if (p.liveTarget && m.targetPieceOf (p).valid && m.targetPieceOf (p).live) ++liveRows;
            bool sorted = true;
            for (int k = 0; k + 1 < w.runnerUps; ++k) sorted = sorted && (p.style == 1 ? w.runnerUp[k].total >= w.runnerUp[k + 1].total : w.runnerUp[k].total <= w.runnerUp[k + 1].total);
            if (sorted) ++runnerUpsOk;
        }
    }
    juce::String text() const
    {
        return juce::String (picks) + " picks: " + juce::String (exact) + " exact, " + juce::String (agree) + " arrive at the engine's piece, " + juce::String (skipped) + " skipped, "
             + juce::String (unavailable) + " unavailable, " + juce::String (waiting) + " waiting; SMOOTH played the following piece " + juce::String (followed) + " times, the target was held "
             + juce::String (held) + " times" + (firstBad.isNotEmpty() ? "; first not exact: " + firstBad : juce::String());
    }
};

// the engine's own "used" counters against the model's copy
static int usedMismatches (Rig& rig, tap::Model& model)
{
    brain* src = rig.p.engine().leftRenderer().getSource();
    if (src == nullptr) return -1;
    int bad = 0;
    for (u32 i = 0; i < src->get_num_blocks(); ++i)
    {
        const float e = const_cast<block&> (src->get_block (i)).get_usage();
        if (! bitsEqualF (std::isfinite (e) ? e : 0.0f, model.used ((int) i))) ++bad;
    }
    return bad;
}

static void testComplete()
{
    std::vector<Setup> cases;
   #if GHOSTMOSAIC_FX
    { Setup s; s.name = "effect, live input, closest, NOVELTY"; s.novelty = 0.3f; cases.push_back (s); }
    { Setup s; s.name = "effect, live input, network, stereo, SMOOTH"; s.kit = "tones"; s.algo = 2; s.stereo = true; s.sticky = 0.4f; s.novelty = 0.2f; cases.push_back (s); }
   #else
    { Setup s; s.name = "closest, defaults"; cases.push_back (s); }
    { Setup s; s.name = "closest, NOVELTY 0.4, BOREDOM, SMOOTH 0.5, timbre only"; s.kit = "tones"; s.tgt = "beat"; s.novelty = 0.4f; s.sticky = 0.5f; s.ratio = 1.0f; cases.push_back (s); }
    { Setup s; s.name = "most different, bands 10-60, bands only"; s.kit = "noise"; s.tgt = "piano"; s.algo = 1; s.ratio = 0.0f; s.fft0 = 10; s.fft1 = 60; s.novelty = 0.2f; cases.push_back (s); }
    { Setup s; s.name = "network, stereo, NOVELTY, SMOOTH, SHAPE / PITCH ONLY 0.6"; s.kit = "tones"; s.tgt = "beat"; s.algo = 2; s.stereo = true; s.novelty = 0.2f; s.sticky = 0.4f; s.nRatio = 0.6f; s.synapses = 40; cases.push_back (s); }
    { Setup s; s.name = "network + wait, REPEAT 3, limit 300"; s.kit = "tones"; s.algo = 3; s.slide = 300; s.stretch = 3; s.synapses = 60; s.novelty = 0.1f; cases.push_back (s); }
    { Setup s; s.name = "closest, TERRAIN shaping everything, NOVELTY, stereo"; s.kit = "tones"; s.tgt = "beat"; s.novelty = 0.3f; s.sticky = 0.2f; s.terrain = 1; s.stereo = true; cases.push_back (s); }
   #endif
    for (auto& c : cases)
    {
        const double sr = 44100; const int bufN = 512;
        Rig rig (c, sr, bufN, false);
        tap::Model model (rig.p.engine());
        model.explainEveryPick (true);
        model.setActive (true);
        rig.run (2, [&] { model.tick (bufN / sr); });                            // the page is open before there is a library
        rig.build();                                                             // ... and sees it arrive
        rig.start();
        Tally t;
        int n = 0;
        rig.run (rig.buffersFor (6.0), [&] { model.tick (bufN / sr); if (++n % 60 == 0) t.take (model); });
        t.take (model);
        const auto& cnt = model.counters();
        const int usedBad = usedMismatches (rig, model);
        const bool live = model.target().live();
        // (the fallback build of TapPeek.h cannot explain a live target, nor the first pick after a restart: there it must say so)
        const bool asPromised = tap::fullRead() ? t.exact == t.picks && model.usedValid() && usedBad == 0 && (! live || t.liveRows == t.picks)
                                                : t.exact + t.flagged + t.unavailable == t.picks && (live || t.exact * 10 >= t.picks * 7);
        check (t.picks > 200 && asPromised && t.wrongButUnflagged == 0 && cnt.cellsLost == 0 && cnt.picksSeen == (uint64_t) t.picks && t.runnerUpsOk == t.picks,
               juce::String (tap::fullRead() ? "" : "[fallback build] ") + "every pick explained exactly, and the \"used\" memory is the engine's to the bit (" + c.name + ")",
               t.text() + "; " + juce::String (usedBad) + " \"used\" values differ" + (live ? "; " + juce::String (t.liveRows) + " live-input pieces captured" : juce::String()));
    }
    {
        // the page opened late, with NOVELTY on: the "used" memory is read back from the engine, then everything is exact again
        Setup c; c.name = "opened late"; c.kit = "tones"; c.novelty = 0.4f; c.sticky = 0.3f; c.stereo = true;
        const double sr = 44100; const int bufN = 512;
        Rig rig (c, sr, bufN);
        rig.start();
        rig.run (rig.buffersFor (3.0));
        tap::Model model (rig.p.engine());
        model.explainEveryPick (true);
        model.setActive (true);
        int buffersUntilValid = -1, n = 0;
        Tally before;
        while (buffersUntilValid < 0 && n < rig.buffersFor (20.0))
            rig.run (4, [&] { model.tick (bufN / sr); ++n; if (n % 4 == 0) { before.take (model); if (model.usedValid()) buffersUntilValid = n; } });
        Tally after; after.last = before.last;
        n = 0;
        rig.run (rig.buffersFor (3.0), [&] { model.tick (bufN / sr); if (++n % 60 == 0) after.take (model); });
        after.take (model);
        const int usedBad = usedMismatches (rig, model);
        const bool lateOk = tap::fullRead() ? before.flagged > 0 && before.exact + before.flagged == before.picks && after.exact == after.picks && usedBad == 0 && model.usedValid()
                                            : after.exact + after.flagged + after.unavailable == after.picks && after.wrongButUnflagged == 0;
        check (buffersUntilValid > 0 && before.wrongButUnflagged == 0 && after.picks > 200 && lateOk,
               juce::String (tap::fullRead() ? "" : "[fallback build] ") + "opened late with NOVELTY on: picks are flagged approximate until the \"used\" memory has been read back from the engine, then every pick is exact",
               "\"used\" confirmed after " + juce::String (buffersUntilValid) + " buffers (" + juce::String (model.library().pieces()) + " pieces); until then " + juce::String (before.picks) + " picks: "
               + juce::String (before.flagged) + " flagged approximate (" + juce::String (before.agree - before.exact) + " of them right all the same), " + juce::String (before.exact) + " exact, "
               + juce::String (before.wrongButUnflagged) + " wrong without a flag; after: " + after.text());
    }
    {
        // a new library built while it plays: the model follows, and stays exact
        Setup c; c.name = "rebuilt"; c.kit = "drums"; c.novelty = 0.3f;
        const double sr = 44100; const int bufN = 512;
        Rig rig (c, sr, bufN);
        tap::Model model (rig.p.engine());
        model.explainEveryPick (true);
        model.setActive (true);
        rig.start();
        rig.run (rig.buffersFor (2.0), [&] { model.tick (bufN / sr); });
        const uint32_t gen0 = model.library().generation();
        setP (rig.p, "src_block", 1400);
        rig.p.engine().generateLibrary();
        int guard = 0;
        while (rig.p.engine().busy() && guard++ < 20000) { rig.run (1, [&] { model.tick (bufN / sr); }); juce::Thread::sleep (1); pump (1); }
        rig.run (4, [&] { model.tick (bufN / sr); });
        Tally skip; skip.take (model);
        Tally t; t.last = skip.last;
        int n = 0;
        rig.run (rig.buffersFor (3.0), [&] { model.tick (bufN / sr); if (++n % 60 == 0) t.take (model); });
        t.take (model);
        const int usedBad = usedMismatches (rig, model);
        const bool rebuiltOk = tap::fullRead() ? t.exact == t.picks && usedBad == 0 : t.exact + t.flagged + t.unavailable == t.picks && t.wrongButUnflagged == 0;
        check (model.library().generation() != gen0 && model.library().generation() != 0 && t.picks > 100 && rebuiltOk,
               juce::String (tap::fullRead() ? "" : "[fallback build] ") + "a library rebuilt while playing: the model follows it, and every pick after it is exact",
               "generation " + juce::String ((int) gen0) + " -> " + juce::String ((int) model.library().generation()) + ", " + juce::String (model.library().pieces()) + " pieces; " + t.text());
    }
}

// ---------------------------------------------------------------- (e) the distance itself, against block::compare
#if ! GHOSTMOSAIC_FX
static void testDistance()
{
    Setup c; c.name = "distance"; c.kit = "tones"; c.tgt = "beat"; c.novelty = 0.3f;
    Rig rig (c, 44100, 512);
    rig.start();
    rig.run (rig.buffersFor (2.0));                                              // (so the "used" counters are not all zero)
    brain* src = rig.p.engine().leftRenderer().getSource();
    const block_source* target = rig.p.engine().leftRenderer().getTargetSource();
    auto lib = rig.p.engine().brainSnapshot();
    auto tgt = rig.p.engine().targetSnapshot();
    if (src == nullptr || target == nullptr || lib == nullptr || tgt == nullptr) { check (false, "the replay's distance is block::compare's", "no world"); return; }
    const int N = lib->numBlocks, T = tgt->numBlocks;
    tap::Replay replay;
    replay.setLibrary (lib, true);
    std::vector<float> used ((size_t) N);
    for (int i = 0; i < N; ++i) used[(size_t) i] = const_cast<block&> (src->get_block ((u32) i)).get_usage();
    replay.usageSlice (0, used.data(), N);
    float weights[100];
    juce::Random rnd (77);
    for (auto& w : weights) w = 0.2f + 3.8f * rnd.nextFloat();
    replay.setWeights (weights);
    struct V { float ratio, nRatio, novelty; u32 f0, f1; bool weights; float tilt; };
    const V vs[] = { { 0.5f, 0, 0, 0, 100, false, 0 }, { 0, 0, 0, 0, 100, false, 0 }, { 1, 0, 0, 0, 100, false, 0 }, { 0.5f, 0.6f, 0, 0, 100, false, 0 }, { 0.5f, 1, 0, 0, 100, false, 0 },
                     { 0.3f, 0.4f, 0.35f, 10, 60, false, 0 }, { 0.5f, 0, 0.2f, 0, 100, true, 0 }, { 0, 0.5f, 0.2f, 5, 90, true, 0.6f }, { 0.7f, 0.3f, 0.9f, 0, 100, false, -0.8f },
                     { 0.5f, 0, 0, 60, 10, false, 0 } };
    long compared = 0, different = 0, nans = 0;
    std::vector<float> bias ((size_t) N);
    for (auto& v : vs)
    {
        search_params sp (v.ratio, v.nRatio, (int) v.f0, (int) v.f1, v.novelty);
        tap::WhySettings ws;
        ws.ratio = v.ratio; ws.nRatio = v.nRatio; ws.fftStart = v.f0; ws.fftEnd = v.f1; ws.novelty = v.novelty;
        const bool tilted = ! juce::exactlyEqual (v.tilt, 0.0f);
        ws.weightsOn = v.weights; ws.factorOn = tilted; ws.tilt = v.tilt;
        if (v.weights) sp.m_band_weights = weights;
        if (tilted)
        {
            for (int i = 0; i < N; ++i) bias[(size_t) i] = Shaping::tiltFactor (v.tilt, lib->bright[(size_t) i]);
            sp.m_block_bias = bias.data(); sp.m_block_bias_count = (u32) N;
        }
        const juce::ScopedNoDenormals asTheAudioThread;
        for (int t = 0; t < T; t += 7)
        {
            tap::WhyTarget wt;
            wt.bands = tgt->bandsOf (0, t); wt.timbre = tgt->timbreOf (0, t); wt.flatBands = tgt->flatBandsOf (0, t); wt.flatTimbre = tgt->flatTimbreOf (0, t);
            wt.bandCount = tgt->bandCount; wt.flags = tgt->rows[0].flags[(size_t) t];
            const block& tb = target->get_block ((u32) t);
            for (int i = 0; i < N; i += 3)
            {
                const double a = tb.compare (src->get_block ((u32) i), sp), b = replay.distance (ws, wt, i);
                ++compared;
                if (std::isnan (a)) ++nans;
                if (! bitsEqual (a, b)) ++different;
                if (i % 30 == 0)
                {
                    tap::Score sc;
                    replay.describe (ws, wt, i, sc);
                    if (sc.finite && ! bitsEqual (sc.total, a)) ++different;
                    if (sc.finite && std::abs (sc.toneShare + sc.timbreShare + sc.usedShare - sc.total) > 1.0e-9 * juce::jmax (1.0, std::abs (sc.total))) ++different;
                }
            }
        }
    }
    check (compared > 100000 && different == 0, "the replay's distance is block::compare's, bit for bit (10 settings: bands, timbre, flattened, NOVELTY, TERRAIN weights and factor)",
           juce::String ((int) compared) + " comparisons, " + juce::String ((int) different) + " differ, " + juce::String ((int) nans) + " not-a-number on both sides (silent pieces)");

    // the snapshot's rows are the engine's own numbers (finite ones untouched)
    long rows = 0, rowBad = 0;
    for (int i = 0; i < N; i += 5)
    {
        search_params only (0, 0, 0, 100, 0);
        // (a piece against itself is 0; against its neighbour it is what the snapshot's rows give)
        const int j = juce::jmin (N - 1, i + 1);
        double acc = 0;
        for (int k = 0; k < lib->bandCount; ++k) { const double d = (double) (lib->bandsOf (i)[k] - lib->bandsOf (j)[k]); acc += d * d; }
        const double mine = (acc / (float) lib->bandCount) * 200;
        const double theirs = src->get_block ((u32) i).compare (src->get_block ((u32) j), only);
        ++rows;
        const bool flatBad = (lib->rows.flags[(size_t) i] & 12) != 0 || (lib->rows.flags[(size_t) j] & 12) != 0;
        if (! flatBad && ! bitsEqual (mine, theirs)) ++rowBad;
    }
    check (rows > 0 && rowBad == 0, "the library snapshot holds the engine's own measurements", juce::String ((int) rows) + " pieces checked against block::compare");
}
#endif

// ---------------------------------------------------------------- the model as views see it
static void testModelFace()
{
    Setup c; c.name = "face"; c.kit = "tones"; c.novelty = 0.2f; c.sticky = 0.3f; c.autotune = 0.3f; c.targetMix = 0.25f;
   #if ! GHOSTMOSAIC_FX
    c.tgt = "beat";
   #endif
    const double sr = 44100; const int bufN = 512;
    Rig rig (c, sr, bufN);
    tap::Model model (rig.p.engine());
    NewestPickView view (model);
    view.frame();
    const juce::String offText = view.text;
    model.explainEveryPick (true);
    model.setActive (true);
    view.frame();
    const juce::String idleText = view.text;
    rig.start();
    double worstTick = 0, sumTick = 0; int ticks = 0, maxVoices = 0, minVoicesLate = 1000;
    bool agesOk = true, voicesOk = true;
    rig.run (rig.buffersFor (4.0), [&]
    {
        model.tick (bufN / sr);
        ++ticks; sumTick += model.counters().lastTickMs; worstTick = juce::jmax (worstTick, model.counters().lastTickMs);
        maxVoices = juce::jmax (maxVoices, model.numVoices());
        if (ticks > 100) minVoicesLate = juce::jmin (minVoicesLate, model.numVoices());
        for (int i = 0; i + 1 < juce::jmin (64, model.numPicks()); ++i)
            if (model.pick (i).channel == model.pick (i + 1).channel && model.pick (i).ageSeconds > model.pick (i + 1).ageSeconds + 1.0e-9) agesOk = false;
        for (int i = 0; i < model.numVoices(); ++i)
        {
            const tap::Voice& v = model.voice (i);
            const tap::Pick* p = model.findPick (v.pickSerial);
            if (p == nullptr || p->libraryPiece != v.libraryPiece || ! p->sounding || v.length != model.library().pieceLength() || v.position < 0 || v.position > (float) v.length || v.pitchRatio <= 0) voicesOk = false;
        }
    });
    model.waitUntilExplained (60000); model.tick (0);
    view.frame();
    const auto& lib = model.library(); const auto& tgt = model.target();
    brain* src = rig.p.engine().leftRenderer().getSource();
    const auto& st = model.settings();
    const auto& tr = model.transport();
    const auto& me = model.meters();
    const auto& ph = model.playhead (0);

    bool worldOk = lib.ok() && src != nullptr && lib.pieces() == (int) src->get_num_blocks() && lib.bandCount() == 100 && lib.sounds() > 0
                   && lib.pieceLength() == (int) src->get_block (0).get_pcm().get_length() && lib.hop() == (int) (src->get_block_size() - src->get_overlap())
                   && lib.bands (0).size() == 100 && lib.timbre (0).size() == 12 && lib.flatBands (0).size() == 100 && lib.flatTimbre (0).size() == 12
                   && lib.bands (-1).empty() && lib.bands (lib.pieces()).empty() && lib.soundOf (lib.pieces()) == -1 && lib.soundName (99999).isEmpty()
                   && (tap::fullRead() ? (int) lib.fade().size() == lib.pieceLength() && lib.fadeType() == c.window : lib.fade().empty() && lib.fadeType() == -1)
                   && lib.connections (0) > 0 && ! lib.nearest (0).empty();
    int covered = 0;
    for (int k = 0; k < lib.sounds(); ++k) { covered += lib.soundPieces (k); worldOk = worldOk && lib.soundOn (k) && ! lib.soundRemoved (k) && lib.soundName (k).isNotEmpty(); }
    worldOk = worldOk && covered == lib.pieces();
    for (int i = 0; i < lib.pieces(); i += 37)
    {
        const block& b = src->get_block ((u32) i);
        float lo, hi; lib.envelope (i, 5, lo, hi);
        worldOk = worldOk && juce::exactlyEqual (lib.pitchAsEngineHolds (i), b.get_freq()) && lib.soundOf (i) >= 0 && lo <= hi && lo >= -1.0f && hi <= 1.0f && lib.peak (i) >= lib.loudness (i)
                  && lib.secondsInSound (i) >= 0 && lib.brightness (i) >= 0 && lib.brightness (i) <= 1;
    }
   #if GHOSTMOSAIC_FX
    const bool targetOk = tgt.live() && ! tgt.ok() && tgt.hop() > 0 && juce::String (tgt.note()).isNotEmpty()
                          && (tap::fullRead() ? model.targetPieceOf (model.pick (0)).valid && model.targetPieceOf (model.pick (0)).live && model.targetPieceOf (model.pick (0)).bands.size() == 100
                                                    && model.targetPieceOf (model.pick (0)).timbre.size() == 12
                                              : ! model.targetPieceOf (model.pick (0)).valid && juce::String (model.targetPieceOf (model.pick (0)).note).contains ("not available"))
                          && ! model.targetPieceOf (model.pick (-1)).valid;
   #else
    const block_source* target = rig.p.engine().leftRenderer().getTargetSource();
    const bool targetOk = tgt.ok() && ! tgt.live() && target != nullptr && tgt.pieces() == (int) target->get_num_blocks() && tgt.bands (0, 0).size() == 100 && tgt.bands (1, tgt.pieces() - 1).size() == 100
                          && tgt.bands (2, 0).empty() && tgt.hop() == (int) (target->get_block_size() - target->get_overlap()) && tgt.name().isNotEmpty()
                          && std::abs (tgt.secondsOf (10) - 10.0 * tgt.hop() / sr) < 1.0e-9 && tap::bandCentroidHz (tgt.bands (0, 3), tgt.rate()) > 0 && tap::bandCentroidHz (tgt.bands (0, 3), tgt.rate()) < (float) (sr / 2) && ! (tap::bandCentroidHz (tgt.bands (0, tgt.pieces()), tgt.rate()) > 0)
                          && model.targetPieceOf (model.pick (0)).valid && ! model.targetPieceOf (model.pick (0)).live
                          && model.targetPieceOf (model.pick (0)).bands.data() == tgt.bands (0, model.pick (0).targetPiece).data() && model.targetPieceOf (model.pick (0)).flatTimbre.size() == 12
                          && ! model.targetPieceOf (model.pick (-1)).valid;
   #endif
    const bool settingsOk = st.style == 0 && std::abs (st.novelty - 0.2f) < 1.0e-6f && std::abs (st.smooth - 0.3f) < 1.0e-6f && std::abs (st.autotune - 0.3f) < 1.0e-6f
                            && std::abs (st.originalMix - 0.25f) < 1.0e-6f && st.lowestBand == 0 && st.highestBand == 100 && st.repeat == 1 && ! st.stereo && ! st.terrainWeights
                            && std::abs (st.volume - 3.0f) < 1.0e-5f && std::abs (st.pieceGain - 0.75f * 0.2f * 3.0f) < 1.0e-5f && model.terrainWeights().empty()
                            && std::abs (model.pick (0).settings.novelty - 0.2f) < 1.0e-6f;
    const bool transportOk = tr.playing && tr.wanted && juce::String (tr.whyNot).isEmpty() && juce::String (idleText).isNotEmpty();
    const bool metersOk = me.outputPeak > 0 && me.shapeValid && me.outMin[0] < 0 && me.outMax[0] > 0 && me.outputPeakHold > 0 && me.engineVoices[0] > 0 && me.limiterGain > 0.99f && ! me.limiterOn
                          && ph.valid && ph.renderSample > 0 && model.counters().audioSeconds > 3.5 && model.counters().audioSeconds < 4.5 && model.counters().cellsLost == 0;
    const bool picksOk = model.numPicks() == juce::jmin ((int) model.counters().picksSeen, (int) tap::Model::kMaxPicks) && model.pick (-1).serial == 0 && model.pick (model.numPicks()).serial == 0
                         && model.pick (0).serial == model.counters().picksSeen && model.findPick (model.pick (5).serial) == &model.pick (5) && model.newestPick (0) == &model.pick (0)
                         && model.newestPick (1) == nullptr && model.voice (-1).pickSerial == 0
                         && (model.pick (0).why.state != tap::Why::ready     // (the fallback build cannot explain a live target: then there is no scale either)
                             || (model.scoreScale().ready() && juce::exactlyEqual (model.scoreScale().unit (model.scoreScale().low() * 0.5), 0.0f)
                                 && juce::exactlyEqual (model.scoreScale().unit (model.scoreScale().high() * 2.0), 1.0f)))
                         && (model.pick (0).why.state == tap::Why::ready || ! tap::fullRead());
    check (worldOk, "model: the library as views read it (pieces, sounds, measurements, outline, fade, network)", juce::String (lib.pieces()) + " pieces, " + juce::String (lib.sounds()) + " sounds");
    check (targetOk, "model: the target as views read it", tgt.live() ? juce::String (tgt.note()) : juce::String (tgt.pieces()) + " pieces of \"" + tgt.name() + "\"");
    check (settingsOk && transportOk && metersOk, "model: settings in force, transport, meters, clocks",
           juce::String (settingsOk ? "" : "SETTINGS ") + (transportOk ? "" : "TRANSPORT ") + (metersOk ? "" : "METERS ") + "peak " + juce::String (me.outputPeak, 3) + ", shape " + juce::String (me.outMin[0], 3) + " .. " + juce::String (me.outMax[0], 3)
           + ", audio " + juce::String (model.counters().audioSeconds, 2) + " s");
    check (picksOk && agesOk && voicesOk && maxVoices >= 3 && minVoicesLate >= 2, "model: the pick ring (newest first, ages in order) and the voices (each from its pick until it ends)",
           juce::String (model.numPicks()) + " picks in the ring, " + juce::String (minVoicesLate) + " to " + juce::String (maxVoices) + " voices at once");
    const bool explained = model.pick (0).why.state == tap::Why::ready;
    check (offText.isNotEmpty() && view.text.contains ("piece ") && (explained ? view.text.contains ("scored ") && view.text.contains ("winner ") : view.text.contains (model.pick (0).why.note)), "the example view in studio/TAP.md compiles, runs and explains the newest pick",
           view.text.upToFirstOccurrenceOf ("\n", false, false));
    info ("tick(): " + juce::String (sumTick / juce::jmax (1, ticks), 4) + " ms on average, " + juce::String (worstTick, 3) + " ms at worst, over " + juce::String (ticks) + " ticks (one per 512-sample buffer, "
          + juce::String (lib.pieces()) + " library pieces); explaining one pick takes " + juce::String (model.counters().explainMs, 3) + " ms on the model's own thread");
    check (sumTick / juce::jmax (1, ticks) < 0.5, "model: tick() stays well under 1 ms", juce::String (sumTick / juce::jmax (1, ticks), 4) + " ms on average, " + juce::String (worstTick, 3) + " ms at worst");

    // a second reader is refused, and takes over when the first lets go; switching off costs the engine nothing
    tap::Model second (rig.p.engine());
    second.setActive (true);
    const bool refused = ! second.active() && juce::String (second.counters().note).contains ("Another");
    model.setActive (false);
    second.tick (0.016);
    rig.run (20, [&] { second.tick (bufN / sr); });
    const bool tookOver = second.active() && second.numPicks() > 0 && ! model.active();
    second.setActive (false);
    rig.run (20);
    tap::Cell left[64];
    const int leftOver = rig.p.engine().tap().read (left, 64);
    check (refused && tookOver && leftOver == 0, "model: one reader at a time; a second takes over when the first lets go; switched off, the engine writes nothing",
           juce::String (leftOver) + " cells written after switching off");

    // the example in studio/TAP.md is the one compiled here
    juce::File self;
    for (auto& base : { juce::File::getCurrentWorkingDirectory(), juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory().getParentDirectory().getParentDirectory() })
    {
        const juce::File f = juce::File::isAbsolutePath (__FILE__) ? juce::File (__FILE__) : base.getChildFile (__FILE__);
        if (f.existsAsFile()) { self = f; break; }
    }
    const juce::File guide = self.getParentDirectory().getSiblingFile ("studio").getChildFile ("TAP.md");
    if (self.existsAsFile() && guide.existsAsFile())
    {
        auto tidy = [] (const juce::String& s) { juce::StringArray lines; lines.addLines (s); for (auto& l : lines) l = l.trimEnd(); while (lines.size() > 0 && lines[lines.size() - 1].isEmpty()) lines.remove (lines.size() - 1); return lines.joinIntoString ("\n"); };
        const juce::String code = tidy (self.loadFileAsString().fromFirstOccurrenceOf ("// TAP.md example begin\n", false, false).upToFirstOccurrenceOf ("// TAP.md example end", false, false));
        const juce::String shown = tidy (guide.loadFileAsString().fromFirstOccurrenceOf ("<!-- example begin -->", false, false).fromFirstOccurrenceOf ("```cpp\n", false, false).upToFirstOccurrenceOf ("```", false, false));
        check (code.isNotEmpty() && code == shown, "studio/TAP.md shows exactly the example compiled here", juce::String (juce::StringArray::fromLines (code).size()) + " lines");
    }
    else std::printf ("SKIP studio/TAP.md example comparison (source files not found from here)\n");
}

// ---------------------------------------------------------------- what it costs the audio thread
// Only the process call is timed, on this thread alone: the queue is emptied between the timed stretches, never during
// them. Every stretch is run several times with the same seed (so the searches are the same work), tap off and tap on
// taking turns, and the fastest time of each stretch counts; the answer is the middle one of the per-stretch differences.
struct CostRun { std::vector<double> ns; uint64_t picks = 0, cells = 0; };
static void costOnce (CostRun& out, const Setup& c, int bufN, bool playing, bool tapOn, bool wantExtras, int stretches, int buffersPerStretch)
{
    if (out.ns.empty()) out.ns.assign ((size_t) stretches, 1.0e30);
    Rig rig (c, 44100, bufN, playing);
    auto& t = rig.p.engine().tap();
    int owner = 0;
    std::vector<tap::Cell> sink (8192);
    if (playing) rig.start();
    rig.l.reserve (1 << 16); rig.r.reserve (1 << 16);
    rig.run (60);
    if (tapOn) { t.attach (&owner); t.wantUsage (wantExtras); t.wantLive (wantExtras); }
    uint64_t picks = 0, cells = 0;
    rig.timing = true;
    for (int sidx = 0; sidx < stretches; ++sidx)
    {
        rig.l.clear(); rig.r.clear(); rig.timedTicks = 0;
        rig.run (buffersPerStretch);
        const double ns = juce::Time::highResolutionTicksToSeconds (rig.timedTicks) * 1.0e9 / buffersPerStretch;
        out.ns[(size_t) sidx] = juce::jmin (out.ns[(size_t) sidx], ns);
        for (int n; (n = t.read (sink.data(), 8192)) > 0;) { cells += (uint64_t) n; for (int i = 0; i < n; ++i) if (sink[(size_t) i].kind == tap::kMatch) ++picks; }
    }
    if (tapOn) t.detach (&owner);
    out.picks = picks; out.cells = cells;
}
static double middle (std::vector<double> v) { if (v.empty()) return 0; std::sort (v.begin(), v.end()); return v[v.size() / 2]; }
static double middleDifference (const CostRun& a, const CostRun& b) { std::vector<double> d; for (size_t i = 0; i < a.ns.size() && i < b.ns.size(); ++i) d.push_back (b.ns[i] - a.ns[i]); return middle (d); }

static void measureCost()
{
    const int stretches = 40, per = 50, reps = 7;
    const double buffers = (double) stretches * per;
    double idleExtra = 0;
    {
        // nothing to play: the per-buffer part of the tap is all there is to see
        Setup c;
        CostRun off, on;
        for (int r = 0; r < reps; ++r) { costOnce (off, c, 128, false, false, false, stretches, per); costOnce (on, c, 128, false, true, false, stretches, per); }
        idleExtra = middleDifference (off, on);
        info ("audio-thread cost, engine idle, 128-sample buffers: " + juce::String (middle (off.ns), 0) + " ns per buffer with the tap off, +" + juce::String (idleExtra, 0)
              + " ns with it on (the state check, the meter frame, the output's shape: " + juce::String ((double) on.cells / buffers, 2) + " cells per buffer)");
    }
    {
        // playing a library of a handful of pieces, so the searches are small and the tap's own work shows
        Setup c; c.stereo = true; c.novelty = 0.2f; c.tinySeconds = 0.07f;
        CostRun off, on, all;
        for (int r = 0; r < reps; ++r)
        {
            costOnce (off, c, 128, true, false, false, stretches, per); costOnce (on, c, 128, true, true, false, stretches, per); costOnce (all, c, 128, true, true, true, stretches, per);
        }
        const double ppb = (double) on.picks / buffers, extra = middleDifference (off, on), extraAll = middleDifference (off, all);
        info ("audio-thread cost, playing a tiny library (stereo, 128-sample buffers, " + juce::String (ppb, 2) + " picks and " + juce::String ((double) on.cells / buffers, 1) + " cells per buffer): "
              + juce::String (middle (off.ns), 0) + " ns per buffer with the tap off, +" + juce::String (extra, 0) + " ns with it on");
        info ("  the same with the \"used\" read-back" + juce::String (GHOSTMOSAIC_FX ? " and the live-input pieces" : "") + " switched on (" + juce::String ((double) all.cells / buffers, 1) + " cells per buffer): +"
              + juce::String (extraAll, 0) + " ns per buffer");
    }
}
}

static void testTap()
{
    taptest::testQueue();
    taptest::testSameOutput();
   #if ! GHOSTMOSAIC_FX
    taptest::testUpstreamNullWithTap();
   #endif
    taptest::testNoAllocWithTap();
    taptest::testComplete();
   #if ! GHOSTMOSAIC_FX
    taptest::testDistance();
   #endif
    taptest::testModelFace();
    taptest::measureCost();
}
