// Ghost Mosaic tests: the TERRAIN page (build 9). Included by MosaicTests.cpp.
//   flat land = the engine untouched; a weighting peak at a band moves the picks towards pieces with
//   energy there; a time ridge raises NOVELTY there; undo; sessions; no allocation on the audio thread.
#include "TerrainShaping.h"
#include "Brightness.h"
#include "TerrainSerial.h"
#include <cstring>

namespace tland
{
using namespace ghost::terrain;

// a ridge at one frequency that lasts the whole target (a band that always counts)
static Terrain bandRidge (float hz, float height = 1.0f)
{
    Terrain t;
    t.add ({ hzToX (hz, terrainRange()), 0.5f, height, 0.05f, kMaxDepth, 0.0f });
    return t;
}
// a ridge right across all frequencies at one moment of the target
static Terrain timeRidge (float z, float height = 1.0f)
{
    Terrain t;
    t.add ({ 0.5f, z, height, kMaxWidth, 0.05f, 0.0f });
    return t;
}

// the library for the weighting test: five sounds, each a different blend of a low hum (70 + 140 Hz)
// and a high bell cluster (5..9 kHz); sound k is k/4 low. Levels wobble so every piece is a little different.
static constexpr int kBlend = 5;
static float lowShareOf (int sound) { return 1.0f - (float) sound / (float) (kBlend - 1); }
static juce::AudioBuffer<float> blendSound (double sr, float lowShare, double seconds, juce::uint32 seed, bool wobble)
{
    juce::Random rnd ((juce::int64) seed);
    const int n = (int) (sr * seconds);
    juce::AudioBuffer<float> b (1, n);
    // hiss: white noise through a 5 kHz high-pass and an 11 kHz low-pass (two one-pole stages each)
    float h1 = 0, h2 = 0, l1 = 0, l2 = 0, px = 0, py = 0;
    const float aH = std::exp (-2.0f * juce::MathConstants<float>::pi * 5000.0f / (float) sr);
    const float aL = std::exp (-2.0f * juce::MathConstants<float>::pi * 11000.0f / (float) sr);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        const float env = wobble ? (float) (0.55 + 0.45 * std::sin (2 * juce::MathConstants<double>::pi * 0.7 * t + seed)) : 1.0f;
        const float wl = wobble ? (float) (0.5 + 0.5 * std::sin (2 * juce::MathConstants<double>::pi * 0.31 * t)) : 0.5f;   // the target leans low, then high
        const float ls = wobble ? juce::jlimit (0.0f, 1.0f, wl) : lowShare;
        const float hum = (float) (0.6 * std::sin (2 * juce::MathConstants<double>::pi * 70 * t) + 0.3 * std::sin (2 * juce::MathConstants<double>::pi * 140 * t));
        const float x = rnd.nextFloat() * 2 - 1;
        // high-pass (x - lowpassed x), twice, then low-pass twice
        h1 = aH * h1 + (1 - aH) * x; float hp = x - h1;
        h2 = aH * h2 + (1 - aH) * hp; hp = hp - h2;
        l1 = aL * l1 + (1 - aL) * hp; l2 = aL * l2 + (1 - aL) * l1;
        juce::ignoreUnused (px, py);
        // the highs: a bright bell-like cluster (5.2 + 7.3 + 9.1 kHz) - steady tones, so its band powers match from piece to
        // piece (noise's don't: the engine's power distance can't tell one burst of hiss from another)
        const float bell = (float) (0.3 * std::sin (2 * juce::MathConstants<double>::pi * 5200 * t) + 0.25 * std::sin (2 * juce::MathConstants<double>::pi * 7300 * t)
                                    + 0.2 * std::sin (2 * juce::MathConstants<double>::pi * 9100 * t));
        juce::ignoreUnused (l2);
        b.setSample (0, i, env * (ls * 0.5f * hum + (1 - ls) * 0.6f * bell));
    }
    return b;
}

struct Picks { double lowShare = 0; int n = 0; std::vector<int> src, tgt; };
static Picks play (Processor& p, int buffers, int bufN = 1024)
{
    Picks r;
    auto pic = p.engine().libraryPicture();
    juce::AudioBuffer<float> io (2, bufN);
    juce::MidiBuffer midi;
    std::vector<Engine::Event> ev (4096);
    p.engine().restart();
    p.engine().play();
    for (int b = 0; b < buffers; ++b)
    {
        io.clear();
        p.engine().process (io, midi, false, false);
        const int k = p.engine().popEvents (ev.data(), (int) ev.size());
        for (int i = 0; i < k; ++i)
        {
            const int s = (int) ev[(size_t) i].src;
            if (ev[(size_t) i].channel != 0 || pic == nullptr || s >= (int) pic->dots.size()) continue;
            r.lowShare += lowShareOf (pic->dots[(size_t) s].sound);
            r.src.push_back (s); r.tgt.push_back ((int) ev[(size_t) i].tgt);
            ++r.n;
        }
    }
    p.engine().pause();
    for (int b = 0; b < 8; ++b) { io.clear(); p.engine().process (io, midi, false, false); }
    if (r.n > 0) r.lowShare /= r.n;
    if (std::getenv ("MOSAIC_DEBUG") != nullptr && pic != nullptr)
    {
        int c[16] = {}; for (int s : r.src) c[juce::jlimit (0, 15, pic->dots[(size_t) s].sound)]++;
        std::printf ("  picks per sound: %d %d %d %d %d (%s)\n", c[0], c[1], c[2], c[3], c[4], pic->soundNames.size() > 0 ? pic->soundNames[0].toRawUTF8() : "");
    }
    return r;
}
static void settle (Processor& p) { pump (20); juce::AudioBuffer<float> io (2, 1024); juce::MidiBuffer m; for (int i = 0; i < 40; ++i) { io.clear(); p.engine().process (io, m, false, false); } }

static void buildBlendLibrary (Processor& p, double sr)
{
    setP (p, "auto_build", 0); setP (p, "limiter", 0); setP (p, "declick", 0);
    setP (p, "src_block", 2048); setP (p, "tgt_block", 2048); setP (p, "src_window", 6); setP (p, "tgt_window", 6); setP (p, "src_overlap", 0.75f); setP (p, "tgt_overlap", 0.75f);
    setP (p, "novelty", 0); setP (p, "stickyness", 0); setP (p, "algo", 0);
    for (int k = 0; k < kBlend; ++k)
        p.engine().addMemorySound ("blend" + juce::String (k), blendSound (sr, lowShareOf (k), 3.0, 100 + (juce::uint32) k, false), sr);
    p.engine().setTargetMemory ("lean", blendSound (sr, 0.5f, 8.0, 7, true), sr);
    p.engine().generateLibrary(); p.engine().generateTarget();
    waitIdle (p);
}
}

static void testTerrain()
{
    using namespace tland;
    const double sr = 44100;
    // ---- the mapping
    {
        const auto flat = makeShaping (flatTerrain(), sr);
        Terrain gone = defaultTerrain(); gone.clear();
        check (! flat.active() && ! makeShaping (gone, sr).active(), "TERRAIN: flat land (and land cleared) changes nothing (weighting off, knobs as set)");
        const auto lo = makeShaping (bandRidge (100), sr), hi = makeShaping (bandRidge (8000), sr), tr = makeShaping (timeRidge (0.5f), sr);
        const int b100 = bandOfHz (100, sr), b8k = bandOfHz (8000, sr);
        double mean = 0; for (auto w : lo.weights) mean += w; mean /= Shaping::kBands;
        check (lo.weightsOn && lo.weights[b100] > 4 * lo.weights[b8k] && hi.weightsOn && hi.weights[b8k] > 4 * hi.weights[b100] && std::abs (mean - 1.0) < 1e-4,
               "TERRAIN: a ridge at a frequency makes the engine band holding it count more (weights average 1)",
               "100 Hz ridge: band " + juce::String (b100) + " x" + juce::String (lo.weights[b100], 2) + "; 8 kHz ridge: band " + juce::String (b8k) + " x" + juce::String (hi.weights[b8k], 2));
        float loWild = 0; for (auto w : lo.wild) loWild = juce::jmax (loWild, std::abs (w));
        float trW = 0; for (auto w : tr.weights) trW = juce::jmax (trW, std::abs (w - 1.0f));
        float loW = 0; for (auto w : lo.weights) loW = juce::jmax (loW, std::abs (w - 1.0f));
        check (tr.timeOn && tr.wildAt (0.5f) > 0.8f && std::abs (tr.wildAt (0.05f)) < 0.05f && (! lo.timeOn || loWild < 0.2f) && trW < 0.4f * loW,
               "TERRAIN: a ridge across the land at a moment makes the matching wild there only; a frequency ridge leaves time alone",
               "wild at 50% " + juce::String (tr.wildAt (0.5f), 2) + ", at 5% " + juce::String (tr.wildAt (0.05f), 2) + "; freq ridge's wildest " + juce::String (loWild, 3)
               + "; time ridge's biggest weight change " + juce::String (trW, 2));
        auto valley = timeRidge (0.3f, -0.8f);
        const auto vs = makeShaping (valley, sr);
        check (vs.timeOn && vs.wildAt (0.3f) < -0.5f && Shaping::novelty (0.4f, vs.wildAt (0.3f)) < 0.2f, "TERRAIN: a valley lowers NOVELTY there (sticks close to the target)",
               "novelty 0.40 -> " + juce::String (Shaping::novelty (0.4f, vs.wildAt (0.3f)), 2));
    }

    // ---- the whole-landscape moves, sound picking, presets, determinism
    {
        auto byName = [] (const char* n) { for (auto& sp : shapePresets()) if (juce::String (sp.name) == n) return sp.make(); return flatTerrain(); };
        const auto forest = makeShaping (byName ("SPIRE FOREST"), sr), table = makeShaping (byName ("PLATEAU"), sr);
        const auto twins = makeShaping (byName ("TWIN PEAKS"), sr), islands = makeShaping (byName ("ARCHIPELAGO"), sr);
        check (forest.stutter > 0.3f && table.smooth > 0.3f && twins.blend > 0.2f && table.stutter < 0.01f && islands.stutter < 0.3f,
               "TERRAIN: spires stutter, plateaus run smooth, merging mountains blend",
               "stutter " + juce::String (forest.stutter, 2) + ", smooth " + juce::String (table.smooth, 2) + ", blend " + juce::String (twins.blend, 2));
        // the new component's columns: a mountain taller than 1 has a flat top, and runs smooth like a plateau; 16 mountains fit
        {
            Terrain col; col.add ({ 0.5f, 0.5f, 1.2f, 0.06f, 2.0f, 0.0f });
            const auto lf = features (col);
            Terrain many; for (int i = 0; i < Terrain::kMaxPeaks; ++i) many.add ({ 0.04f + 0.06f * (float) i, 0.5f, 0.4f, 0.02f, 1.0f, 0.0f });
            check (lf.plateaus == 1 && makeShaping (col, sr).smooth > 0.1f && many.count == 16 && features (many).spires == 16,
                   "TERRAIN: a flat-topped column (height 1.2) runs smooth like a plateau; 16 mountains all count",
                   "column smooth " + juce::String (makeShaping (col, sr).smooth, 2) + ", spires of 16: " + juce::String (features (many).spires));
        }
        // which sounds are in play: one mountain over the lows keeps the dark sounds
        const std::vector<float> sx { 0.10f, 0.25f, 0.45f, 0.70f, 0.90f, 0.95f };
        const auto one = makeShaping (bandRidge (60), sr, sx), many = makeShaping (byName ("ARCHIPELAGO"), sr, sx);
        check (one.maskOn && one.allows (0) && ! one.allows (5) && one.inPlay < 6 && ! many.maskOn,
               "TERRAIN: one mountain keeps the sounds whose brightness sits under it in play; four or more mountains play them all",
               juce::String (one.inPlay) + " of 6 in play with one mountain over the lows");
        // every preset is different, and the same land always makes the same settings
        bool distinct = true, same = true;
        std::vector<Shaping> all;
        for (auto& sp : shapePresets()) all.push_back (makeShaping (sp.make(), sr, sx));
        for (size_t i = 0; i < all.size(); ++i)
        {
            const auto again = makeShaping (shapePresets()[i].make(), sr, sx);
            same = same && std::memcmp (&again, &all[i], sizeof (Shaping)) == 0;
            for (size_t j = i + 1; j < all.size(); ++j) distinct = distinct && std::memcmp (&all[i], &all[j], sizeof (Shaping)) != 0;
        }
        check (same && distinct && all.size() >= 6, "TERRAIN: " + juce::String ((int) all.size()) + " SHAPES presets, all different, and the mapping is repeatable");
    }

    // ---- flat land through the plug-in: still the upstream engine, sample for sample
    {
        Processor p;
        p.engine().prepare (sr, 2048);
        setP (p, "limiter", 0); setP (p, "declick", 0); setP (p, "auto_build", 0);
        setP (p, "algo", 0); setP (p, "novelty", 0.1f); setP (p, "src_block", 1500); setP (p, "tgt_block", 1500); setP (p, "src_window", 6); setP (p, "tgt_window", 6); setP (p, "src_overlap", 0.75f); setP (p, "tgt_overlap", 0.75f);
        setP (p, "volume", 0.3f);
        // the land was shaped, played a little, then RESET and the plug-in started again from the top
        p.terrain.set (bandRidge (300)); pump (5);
        for (auto& s : demo::kit ("drums", sr)) p.engine().addMemorySound (s.name, s.audio, sr);
        p.loadDemoTarget ("vowels");
        p.engine().generateLibrary(); p.engine().generateTarget();
        waitIdle (p);
        p.engine().play();
        { juce::AudioBuffer<float> io (2, 2048); juce::MidiBuffer m; for (int i = 0; i < 30; ++i) { io.clear(); p.engine().process (io, m, false, false); } }
        p.engine().pause();
        p.terrain.set (flatTerrain()); pump (5);
        { juce::AudioBuffer<float> io (2, 2048); juce::MidiBuffer m; for (int i = 0; i < 40; ++i) { io.clear(); p.engine().process (io, m, false, false); } }
        const bool off = ! p.engine().shapeNow().weights && ! p.engine().shapeNow().time;
        if (std::getenv ("MOSAIC_DEBUG") != nullptr)
        {
            auto& rl = p.engine().leftRenderer(); auto* sp = rl.get_params();
            std::printf ("  after flat: smooth %.4f rep %u mix %.4f ratio %.4f nratio %.4f fft %u..%u syn %u slide %.1f auto %.4f nmix %.4f algo %d nov %.4f\n", sp->m_stickyness, rl.get_stretch(), rl.get_target_mix(),
                         sp->m_ratio, sp->m_n_ratio, sp->m_fft1_start, sp->m_fft1_end, sp->m_num_synapses, rl.get_slide_error(), rl.get_autotune(), rl.get_n_mix(), (int) rl.get_search_algo(), sp->m_usage_importance);
        }
        auto sounds = p.engine().sounds();
        juce::StringArray keys; for (auto& s : sounds) keys.add (s.key);
        BuildSpec spec { 1500, 0.75f, (window::type) 6 };
        auto src = upstreamLibrary (keys, spec, false);
        const auto tkey = registerTarget ("vowels", sr);
        auto tl = upstreamTarget (tkey, brain::LEFT, spec);
        Knobs k; k.algo = 0; k.novelty = 0.1f; k.volume = 3.0f; k.boredom = 0.9f;
        const int bufN = 2048, buffers = (int) (sr * 10 / bufN);
        std::vector<float> ref;
        {
            renderer L (*src, *tl);
            applyKnobs (L, k);
            src->set_usage_falloff (k.boredom);
            // (the engine's renderer has played before: it already knows the target's hop, so it doesn't spend its first
            // buffer re-starting as a brand-new one does; one buffer here brings upstream's to the same point)
            { std::vector<float> l (bufN); L.process (bufN, l.data()); }
            std::srand (99);
            L.reset(); src->jiggle();        // (the engine's RESTART resets both renderers: two jiggles)
            std::vector<float> l (bufN);
            for (int b = 0; b < buffers + 1; ++b) { std::fill (l.begin(), l.end(), 0.0f); L.process (bufN, l.data()); ref.insert (ref.end(), l.begin(), l.end()); }
        }
        // a fresh start: rebuild (as GENERATE does) so the pieces' use counts start from zero like upstream's
        p.engine().generateLibrary(); waitIdle (p);
        { juce::AudioBuffer<float> io (2, 2048); juce::MidiBuffer m; for (int i = 0; i < 4; ++i) { io.clear(); p.engine().process (io, m, false, false); } }
        std::srand (99);
        p.engine().restart();
        p.engine().play();
        auto out = runEngine (p, buffers, bufN);
        const double d = maxDiff (out, ref);   // (restart + play reach the renderers in the first buffer)
        check (off && d == 0.0 && peakOf (out) > 1e-4, "TERRAIN null test: land shaped, played, then flattened = upstream engine, sample for sample", nullDetail (d, peakOf (ref)));
    }

    // ---- the weighting itself, in the engine's own search: a library of a low hum alone and a high bell alone, a target
    // with both (their balance drifting). Flat: some of each. Lows raised: the hum wins more; highs raised: the bell.
    {
        compat::putMemorySound ("mem:test:land:hum", blendSound (sr, 1.0f, 3.0, 201, false), sr);
        compat::putMemorySound ("mem:test:land:hiss", blendSound (sr, 0.0f, 3.0, 202, false), sr);
        compat::putMemorySound ("mem:test:land:both", blendSound (sr, 0.5f, 8.0, 203, true), sr);
        BuildSpec spec { 2048, 0.5f, window::HANN };
        auto lib = upstreamLibrary ({ "mem:test:land:hum", "mem:test:land:hiss" }, spec, false);
        auto tgt = upstreamTarget ("mem:test:land:both", brain::LEFT, spec);
        const u32 humEnd = lib->get_samples().front().m_end;
        auto humShare = [&] (const Shaping& sh)
        {
            search_params p (0, 0, 0, 100, 0);
            p.m_band_weights = sh.weightsOn ? sh.weights : nullptr;
            int hum = 0, n = 0;
            for (u32 i = 0; i < tgt->get_num_blocks(); ++i) { hum += lib->search (tgt->get_block (i), p) <= humEnd ? 1 : 0; ++n; }
            return (double) hum / juce::jmax (1, n);
        };
        const double f0 = humShare (makeShaping (flatTerrain(), sr)), fl = humShare (makeShaping (bandRidge (100), sr)), fh = humShare (makeShaping (bandRidge (8000), sr));
        check (fl > f0 + 0.05 && fh < f0 - 0.05, "TERRAIN: in the engine's search, raising the lows picks the low pieces, raising the highs the high ones",
               "hum pieces picked: flat " + juce::String (f0, 2) + ", lows raised " + juce::String (fl, 2) + ", highs raised " + juce::String (fh, 2));
    }

    // ---- a weighting peak at a band moves the picks towards pieces with energy there (the plug-in, everything on)
    {
        Processor p;
        p.engine().prepare (sr, 1024);
        buildBlendLibrary (p, sr);
        const int bufs = (int) (sr * 7 / 1024);
        auto run = [&] (const Terrain& t, float ratio) { setP (p, "ratio", ratio); p.terrain.set (t); settle (p); return play (p, bufs); };
        for (float ratio : { 0.0f, 0.5f })
        {
            const auto flat = run (flatTerrain(), ratio), low = run (bandRidge (100), ratio), high = run (bandRidge (8000), ratio);
            const auto det = "low share of the chosen pieces: flat " + juce::String (flat.lowShare, 3) + ", lows raised " + juce::String (low.lowShare, 3)
                             + ", highs raised " + juce::String (high.lowShare, 3) + " (" + juce::String (flat.n) + " picks each)";
            // (raising the lows makes the hum's LEVEL count: the picks change to the pieces whose lows match, not simply the
            // darkest ones; raising the highs moves them to the brighter pieces)
            int changed = 0; for (size_t i = 0; i < std::min (flat.src.size(), low.src.size()); ++i) changed += flat.src[i] != low.src[i] ? 1 : 0;
            check (flat.n > 50 && changed > flat.n / 5 && high.lowShare < flat.lowShare - 0.1,
                   "TERRAIN: raising the lows changes the picks, raising the highs moves them to brighter pieces (TONE / TIMBRE " + juce::String (ratio, 1) + ")",
                   det + "; " + juce::String (changed) + " picks changed by the lows");
        }
        setP (p, "ratio", 0.5f);
        // ---- a time ridge raises NOVELTY at that point of the target (and the picks spread out there)
        setP (p, "novelty", 0.0f); setP (p, "boredom", 0.9f);
        p.terrain.set (timeRidge (0.5f)); settle (p);
        juce::AudioBuffer<float> io (2, 1024); juce::MidiBuffer midi;
        p.engine().restart(); p.engine().play();
        float novMid = 0, novEdge = 1; int nMid = 0;
        std::vector<Engine::Event> ev (4096);
        std::vector<int> mid, early;
        const auto tp = p.engine().targetPicture();
        const int nb = tp != nullptr ? tp->numBlocks : 1;
        for (int b = 0; b < bufs; ++b)
        {
            io.clear(); p.engine().process (io, midi, false, false);
            const auto now = p.engine().shapeNow();
            if (now.pos > 0.45f && now.pos < 0.55f) { novMid += now.novelty; ++nMid; }
            if (now.pos < 0.15f) novEdge = juce::jmin (novEdge, now.novelty);
            const int k = p.engine().popEvents (ev.data(), (int) ev.size());
            for (int i = 0; i < k; ++i)
            {
                if (ev[(size_t) i].channel != 0) continue;
                const float pos = (float) ev[(size_t) i].tgt / (float) juce::jmax (1, nb - 1);
                if (pos > 0.4f && pos < 0.6f) mid.push_back ((int) ev[(size_t) i].src);
                if (pos < 0.2f) early.push_back ((int) ev[(size_t) i].src);
            }
        }
        p.engine().pause();
        auto distinct = [] (std::vector<int> v) { std::sort (v.begin(), v.end()); return (double) (std::unique (v.begin(), v.end()) - v.begin()) / (double) std::max<size_t> (1, v.size()); };
        if (nMid > 0) novMid /= (float) nMid;
        check (nMid > 0 && novMid > 0.6f && novEdge < 0.02f && distinct (mid) > distinct (early),
               "TERRAIN: a time ridge raises NOVELTY there (and the picks spread out there), the rest stays as set",
               "novelty " + juce::String (novEdge, 2) + " at the start, " + juce::String (novMid, 2) + " in the middle; different pieces per pick " + juce::String (distinct (early), 2)
               + " early vs " + juce::String (distinct (mid), 2) + " in the middle");
    }

    // ---- undo / redo reach the engine; sessions keep the land; old sessions load flat
    {
        Processor a;
        a.terrain.set (flatTerrain());
        a.terrain.set (bandRidge (500));
        const bool on = a.engine().shaping().weightsOn;
        a.terrain.undo();
        const bool offAfterUndo = ! a.engine().shaping().active();
        a.terrain.redo();
        check (on && offAfterUndo && a.engine().shaping().weightsOn && a.terrain.canUndo(), "TERRAIN: an edit is one undo step; UNDO / REDO reach the matching");

        Terrain land = bandRidge (200); land.add ({ 0.6f, 0.3f, -0.5f, 0.1f, 0.1f, 0.3f }); land.grit = 0.2f;
        a.terrain.set (land);
        juce::MemoryBlock st; a.getStateInformation (st);
        Processor b;
        b.setStateInformation (st.getData(), (int) st.getSize());
        waitIdle (b);
        const auto sa = a.engine().shaping(), sb = b.engine().shaping();
        bool same = sa.weightsOn == sb.weightsOn && sa.timeOn == sb.timeOn;
        for (int i = 0; i < Shaping::kBands; ++i) same = same && sa.weights[i] == sb.weights[i];
        for (int i = 0; i < Shaping::kSteps; ++i) same = same && sa.wild[i] == sb.wild[i];
        check (b.terrain.terrain() == land && same, "TERRAIN: the land is saved with the plug-in's state and comes back exactly");

        // a state from before build 9 has no landscape: it loads flat, even over a shaped one
        Processor old;
        old.engine().setTerrain (flatTerrain(), {});     // (getState then writes no "terrain": build 8's format)
        juce::MemoryBlock ost; old.getStateInformation (ost);
        Processor c;
        c.terrain.set (land);
        c.setStateInformation (ost.getData(), (int) ost.getSize());
        waitIdle (c);
        check (c.terrain.terrain() == flatTerrain() && ! c.engine().shaping().active(), "TERRAIN: an older session (no landscape) loads flat",
               juce::String (c.terrain.terrain().size()) + " mountains, shaping " + (c.engine().shaping().active() ? "on" : "off") + ", state " + juce::String ((int) c.engine().terrainState().length()) + " chars");

        // beside a .samplebrain session file
        Processor s1; setP (s1, "auto_build", 0);
        s1.loadKit ("drums"); s1.loadDemoTarget ("beat"); s1.engine().generateLibrary(); s1.engine().generateTarget(); waitIdle (s1);
        s1.terrain.set (land);
        auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("mosaic-terrain-test.samplebrain");
        s1.engine().saveSessionFile (f); waitIdle (s1);
        Processor s2; s2.engine().loadSessionFile (f); waitIdle (s2); pump (50);
        check (f.withFileExtension ("terrain.json").existsAsFile() && s2.terrain.terrain() == land, "TERRAIN: saving a session file keeps the land beside it; opening it brings it back");
        f.deleteFile(); f.withFileExtension ("terrain.json").deleteFile();
    }

    // ---- live moves reach the engine while playing, go back exactly when flat or off, and fast drags don't click
    {
        Processor p;
        p.prepareToPlay (sr, 512);
        setP (p, "auto_build", 0); setP (p, "limiter", 0); setP (p, "src_window", 6); setP (p, "tgt_window", 6); setP (p, "src_overlap", 0.75f); setP (p, "tgt_overlap", 0.75f); setP (p, "src_block", 1000); setP (p, "tgt_block", 1000);
        setP (p, "stretch", 1); setP (p, "stickyness", 0.2f); setP (p, "target_mix", 0.0f);
        p.loadKit ("drums"); p.loadKit ("tones"); p.loadDemoTarget ("vowels");
        p.engine().generateLibrary(); p.engine().generateTarget(); waitIdle (p);
        juce::AudioBuffer<float> io (2, 512); juce::MidiBuffer midi;
        p.engine().play();
        auto run = [&] (int n, std::vector<float>* outL = nullptr, std::vector<float>* outR = nullptr)
        {
            for (int i = 0; i < n; ++i)
            {
                io.clear(); p.processBlock (io, midi);
                if (outL) outL->insert (outL->end(), io.getReadPointer (0), io.getReadPointer (0) + 512);
                if (outR) outR->insert (outR->end(), io.getReadPointer (1), io.getReadPointer (1) + 512);
            }
        };
        run (20);
        auto& rl = p.engine().leftRenderer();
        for (auto& sp : shapePresets()) if (juce::String (sp.name) == "SPIRE FOREST") p.terrain.set (sp.make());
        pump (5); run (60);
        const u32 rep = rl.get_stretch();
        const float tm = rl.get_target_mix();
        std::vector<float> l, r; run (200, &l, &r);
        double side = 0, mid = 0; for (size_t i = 0; i < l.size(); ++i) { side += std::abs (l[i] - r[i]); mid += std::abs (l[i] + r[i]); }
        p.setTerrainOn (false); pump (5); run (60);
        const bool offOk = rl.get_stretch() == 1 && std::abs (rl.get_params()->m_stickyness - 0.2f) < 1e-6f && ! p.engine().shapeNow().weights;
        p.setTerrainOn (true); pump (5); run (60);
        const bool backOn = rl.get_stretch() > 1;
        p.terrain.set (flatTerrain()); pump (5); run (60);
        const bool flatOk = rl.get_stretch() == 1 && std::abs (rl.get_params()->m_stickyness - 0.2f) < 1e-6f && rl.get_target_mix() == 0.0f;
        check (rep > 1 && side == 0.0 && mid > 0.0 && offOk && backOn && flatOk, "TERRAIN: the landscape moves REPEAT / SMOOTH live, the sound stays Samplebrain's mono (no widening); OFF or flat puts the knobs back exactly",
               "REPEAT " + juce::String ((int) rep) + ", ORIGINAL MIX " + juce::String (tm, 2) + ", left - right " + juce::String (side, 6));
    }
    {
        // fast drags: a new landscape every buffer for 6 s; the output has no clicks (the piece-join detector:
        // smooth sines in, so anything above 9 kHz is a join that doesn't fade)
        Processor p;
        p.prepareToPlay (sr, 512);
        setP (p, "auto_build", 0); setP (p, "limiter", 0); setP (p, "declick", 0); setP (p, "src_window", 6); setP (p, "tgt_window", 6); setP (p, "src_overlap", 0.75f); setP (p, "tgt_overlap", 0.75f); setP (p, "src_block", 1000); setP (p, "tgt_block", 1000);
        setP (p, "novelty", 0.3f); setP (p, "volume", 0.3f);
        const double freqs[] = { 110, 165, 220, 330, 440, 660 };
        for (int i = 0; i < 6; ++i)
        {
            juce::AudioBuffer<float> b (1, (int) sr * 2);
            for (int n = 0; n < b.getNumSamples(); ++n) b.setSample (0, n, 0.6f * (float) std::sin (juce::MathConstants<double>::twoPi * freqs[i] * n / sr) * (0.6f + 0.4f * (float) std::sin (n / sr * 3.0)));
            p.engine().addMemorySound ("sine" + juce::String (i), b, sr);
        }
        juce::AudioBuffer<float> tg (1, (int) sr * 6);
        for (int n = 0; n < tg.getNumSamples(); ++n) { const double ts = n / sr; tg.setSample (0, n, 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * (180 + 200 * std::sin (ts * 0.9)) * ts)); }
        p.engine().setTargetMemory ("glide", tg, sr);
        p.engine().generateLibrary(); p.engine().generateTarget(); waitIdle (p);
        juce::AudioBuffer<float> io (2, 512); juce::MidiBuffer midi;
        p.engine().play();
        std::vector<float> stillR, dragR;
        auto run = [&] (int n, std::vector<float>* outL = nullptr, std::vector<float>* outR = nullptr)
        {
            for (int i = 0; i < n; ++i)
            {
                io.clear(); p.processBlock (io, midi);
                if (outL) outL->insert (outL->end(), io.getReadPointer (0), io.getReadPointer (0) + 512);
                if (outR) outR->insert (outR->end(), io.getReadPointer (1), io.getReadPointer (1) + 512);
            }
        };
        // (5 s each from the top of the 6 s target: the target's own loop restart cuts every piece, upstream as well)
        run (20);
        std::vector<float> still, drag;
        p.engine().restart();
        run (2); still.clear(); stillR.clear();
        run ((int) (sr * 5 / 512), &still, &stillR);
        p.engine().restart();
        run (2);
        juce::Random rnd (5);
        for (int i = 0; i < (int) (sr * 5 / 512); ++i)
        {
            Terrain t;
            const float ph = (float) i / 40.0f;
            t.add ({ 0.5f + 0.4f * std::sin (ph), 0.5f + 0.4f * std::cos (ph * 0.7f), 0.9f * std::abs (std::sin (ph * 1.3f)), 0.012f + 0.2f * std::abs (std::sin (ph * 0.4f)), 0.1f, 0.3f });
            t.add ({ 0.3f, 0.3f, 0.6f, 0.15f, 0.5f, 0.0f });
            if (i % 3 == 0) t.add ({ rnd.nextFloat(), rnd.nextFloat(), 0.7f, 0.02f, 0.06f, 0.0f });
            p.terrain.setWithoutUndo (t);
            run (1, &drag, &dragR);
        }
        const double hfStill = juce::jmax (hfPeak (still, sr) / juce::jmax (1e-9, peakOf (still)), hfPeak (stillR, sr) / juce::jmax (1e-9, peakOf (stillR)));
        if (std::getenv ("MOSAIC_DEBUG") != nullptr)
            std::printf ("  drag HF: L %.1f dB, R %.1f dB\n", 20 * std::log10 (hfPeak (drag, sr) / peakOf (drag) + 1e-12), 20 * std::log10 (hfPeak (dragR, sr) / peakOf (dragR) + 1e-12));
        const double hfDrag = juce::jmax (hfPeak (drag, sr) / juce::jmax (1e-9, peakOf (drag)), hfPeak (dragR, sr) / juce::jmax (1e-9, peakOf (dragR)));
        check (hfDrag < 0.003 && peakOf (drag) > 1e-3, "TERRAIN: no clicks while the land is dragged fast (a new landscape every buffer for 5 s)",
               "HF peak " + juce::String (20 * std::log10 (juce::jmax (1e-12, hfDrag)), 1) + " dB re peak (still land " + juce::String (20 * std::log10 (juce::jmax (1e-12, hfStill)), 1) + " dB)");
    }

    // ---- no allocation on the audio thread while the land is being shaped
    {
        Processor p;
        p.prepareToPlay (48000, 256);
        setP (p, "auto_build", 0);
        p.loadKit ("drums"); p.loadDemoTarget ("vowels");
        p.engine().generateLibrary(); p.engine().generateTarget(); waitIdle (p);
        juce::AudioBuffer<float> io (2, 256); juce::MidiBuffer midi;
        p.engine().play();
        for (int i = 0; i < 20; ++i) { io.clear(); p.processBlock (io, midi); }
        long n = 0;
        alloccount::start();
        for (int i = 0; i < 48000 * 3 / 256; ++i)
        {
            if (i % 8 == 0)
            {
                n += alloccount::stop();     // (the message thread's work isn't counted: here the test is both threads)
                Terrain t = timeRidge (0.2f + 0.6f * (float) ((i / 8) % 10) / 10.0f);
                t.add ({ (float) ((i / 8) % 7) / 7.0f, 0.5f, 0.8f, 0.06f, kMaxDepth, 0.2f });
                p.terrain.setWithoutUndo (t);
                alloccount::start();
            }
            io.clear(); p.processBlock (io, midi);
        }
        n += alloccount::stop();
        check (n == 0 && p.engine().shapeNow().time, "TERRAIN: no allocation on the audio thread while the land changes", juce::String ((int) n) + " allocations in 3 s");
    }
}

// the ghost in the particle view: the first shape, with holes for its eyes and mouth
static void testGhostShape()
{
    Swarm s;
    s.setSize (480, 400);
    const auto pts = s.homePoints();
    auto near = [&] (float x, float y, float r) { int c = 0; for (auto& q : pts) c += q.getDistanceFrom ({ x, y }) < r ? 1 : 0; return c; };
    // (Swarm.cpp's ghost: the head's centre at (240, 158); the eyes at (189, 156) and (291, 156); the mouth at (240, 235))
    const int eyeL = near (189, 156, 12), eyeR = near (291, 156, 12), mouth = near (240, 235, 9), cheek = near (240, 120, 12);
    check (s.shapeIndex() == Swarm::kGhostShape && (int) pts.size() == s.numDots() && eyeL == 0 && eyeR == 0 && mouth == 0 && cheek > 3,
           "the live view forms the ghost on first open, with its eyes and mouth left as holes",
           juce::String (s.numDots()) + " dots; dots in the eyes " + juce::String (eyeL) + "/" + juce::String (eyeR) + ", the mouth " + juce::String (mouth) + ", the forehead " + juce::String (cheek));
    // the hem is scalloped: dots reach lower under each lobe's middle than at the notches between them
    auto lowest = [&] (float x) { float y = 0; for (auto& q : pts) if (std::abs (q.x - x) < 5) y = juce::jmax (y, q.y); return y; };
    const float lobe = lowest (112 + 32), notch = lowest (112 + 64);
    check (lobe > notch + 8, "the ghost's hem is wavy (scallops)", "lowest dot under a lobe " + juce::String (lobe, 0) + ", at a notch " + juce::String (notch, 0));
    // the cycle comes back round to the ghost
    int back = 0;
    for (int i = 0; i < 4; ++i) { s.shockwave(); if (s.shapeIndex() == Swarm::kGhostShape) back = i + 1; }
    check (back == 4, "the shape cycle comes back round to the ghost (circle, square, flower, ghost)");
}

// ---------------------------------------------------------------- build 10: darker / brighter pieces, and the drag
namespace tland
{
static double meanCentroid (Processor& p, const Picks& r)
{
    auto pic = p.engine().libraryPicture();
    if (pic == nullptr || r.src.empty()) return 0;
    double s = 0; for (int i : r.src) s += pic->centroidHz[(size_t) i];
    return s / (double) r.src.size();
}
}

static void testTerrainBrightness()
{
    using namespace tland;
    const double sr = 44100;
    {
        const auto lo = makeShaping (bandRidge (100), sr), hi = makeShaping (bandRidge (8000), sr), fl = makeShaping (flatTerrain(), sr);
        check (lo.tilt < -0.3f && hi.tilt > 0.3f && fl.tilt == 0.0f, "TERRAIN: raised lows ask for darker pieces, raised highs for brighter ones, flat for neither",
               "tilt: lows " + juce::String (lo.tilt, 2) + ", highs " + juce::String (hi.tilt, 2));
    }
    Processor p;
    p.engine().prepare (sr, 1024);
    buildBlendLibrary (p, sr);
    auto pic = p.engine().libraryPicture();
    check (pic != nullptr && (int) pic->centroidHz.size() == pic->numBlocks && pic->numBlocks > 0, "TERRAIN: every library piece has its brightness (spectral centroid)");
    const int bufs = (int) (sr * 7 / 1024);
    for (float ratio : { 0.0f, 0.5f, 1.0f })
    {
        setP (p, "ratio", ratio);
        p.terrain.set (flatTerrain()); settle (p); const auto f = play (p, bufs);
        p.terrain.set (bandRidge (100)); settle (p); const auto l = play (p, bufs);
        p.terrain.set (bandRidge (8000)); settle (p); const auto h = play (p, bufs);
        const double cf = meanCentroid (p, f), cl = meanCentroid (p, l), ch = meanCentroid (p, h);
        check (cl < cf * 0.8 && ch > cf * 1.1, "TERRAIN: raising the lows picks clearly darker pieces, the highs brighter ones (TONE / TIMBRE " + juce::String (ratio, 1) + ")",
               "mean spectral centroid of the chosen pieces: flat " + juce::String (cf, 0) + " Hz, lows raised " + juce::String (cl, 0) + " Hz, highs raised " + juce::String (ch, 0) + " Hz");
    }
}

static void testTerrainDrag()
{
    using namespace tland;
    const double sr = 44100;
    // the drag, straight into the engine: a fast pull up and to the right is heard at once and is gone after
    {
        Processor p;
        p.prepareToPlay (sr, 512);
        setP (p, "auto_build", 0); setP (p, "novelty", 0.1f); setP (p, "stickyness", 0.2f); setP (p, "stretch", 1);
        p.loadKit ("drums"); p.loadKit ("tones"); p.loadDemoTarget ("vowels");
        p.engine().generateLibrary(); p.engine().generateTarget(); waitIdle (p);
        juce::AudioBuffer<float> io (2, 512); juce::MidiBuffer midi;
        p.engine().play();
        auto run = [&] (int n) { for (int i = 0; i < n; ++i) { io.clear(); p.processBlock (io, midi); } };
        run (40);
        auto& rl = p.engine().leftRenderer();
        const bool quiet = p.engine().dragNow() == 0.0f && std::abs (rl.shapedNovelty() - 0.1f) < 1e-6f && rl.get_stretch() == 1;
        p.engine().setDrag (1.0f, 0.8f, 0.7f);
        run (3);    // ~35 ms
        const float novFast = p.engine().shapeNow().novelty, tiltFast = p.engine().shapeNow().tilt; const u32 repFast = rl.get_stretch();
        run (20);
        double side = 0, mid = 0;
        for (int i = 0; i < 40; ++i) { io.clear(); p.processBlock (io, midi); for (int k = 0; k < 512; ++k) { side += std::abs (io.getSample (0, k) - io.getSample (1, k)); mid += std::abs (io.getSample (0, k) + io.getSample (1, k)); } }
        p.engine().setDrag (0, 0, 0);
        run ((int) (sr * 3 / 512));
        const bool gone = p.engine().dragNow() == 0.0f && std::abs (rl.shapedNovelty() - 0.1f) < 1e-6f && rl.get_stretch() == 1
                          && std::abs (rl.get_params()->m_stickyness - 0.2f) < 1e-6f && ! p.engine().shapeNow().time;
        check (quiet && novFast > 0.5f && repFast > 1 && tiltFast > 0.3f && side == 0.0 && mid > 0.0 && gone && p.engine().shapeNow().tilt == 0.0f,
               "TERRAIN drag: a fast pull up and right is heard within ~35 ms (wilder, stutter, brighter pieces), stays mono, and is completely gone within 3 s of letting go",
               "NOVELTY 0.10 -> " + juce::String (novFast, 2) + ", REPEAT 1 -> " + juce::String ((int) repFast) + ", brightness " + juce::String (tiltFast, 2) + ", left - right " + juce::String (side, 6));
        // pulling DOWN pulls it back towards the target
        p.engine().setDrag (0.3f, 0.0f, -1.0f); run (10);
        const float novDown = p.engine().shapeNow().novelty;
        p.engine().setDrag (0, 0, 0); run ((int) (sr * 3 / 512));
        check (novDown < 0.1f, "TERRAIN drag: pulling down calms the matching (NOVELTY below the knob)", "NOVELTY " + juce::String (novDown, 3));
        // no allocation on the audio thread while flicking
        alloccount::start();
        juce::Random rnd (3);
        for (int i = 0; i < (int) (sr * 3 / 512); ++i)
        {
            if (i % 10 == 0) p.engine().setDrag (rnd.nextFloat(), rnd.nextFloat() * 2 - 1, rnd.nextFloat() * 2 - 1);
            io.clear(); p.processBlock (io, midi);
        }
        const long n = alloccount::stop();
        p.engine().setDrag (0, 0, 0);
        check (n == 0, "TERRAIN drag: no allocation on the audio thread while flicking", juce::String ((int) n));
    }
    // flicks don't click (the piece-join detector, smooth sines in)
    {
        Processor p;
        p.prepareToPlay (sr, 512);
        setP (p, "auto_build", 0); setP (p, "limiter", 0); setP (p, "declick", 0); setP (p, "src_window", 6); setP (p, "tgt_window", 6); setP (p, "src_overlap", 0.75f); setP (p, "tgt_overlap", 0.75f); setP (p, "src_block", 1000); setP (p, "tgt_block", 1000);
        setP (p, "novelty", 0.3f); setP (p, "volume", 0.3f);
        const double freqs[] = { 110, 165, 220, 330, 440, 660 };
        for (int i = 0; i < 6; ++i)
        {
            juce::AudioBuffer<float> b (1, (int) sr * 2);
            for (int k = 0; k < b.getNumSamples(); ++k) b.setSample (0, k, 0.6f * (float) std::sin (juce::MathConstants<double>::twoPi * freqs[i] * k / sr) * (0.6f + 0.4f * (float) std::sin (k / sr * 3.0)));
            p.engine().addMemorySound ("sine" + juce::String (i), b, sr);
        }
        juce::AudioBuffer<float> tg (1, (int) sr * 6);
        for (int k = 0; k < tg.getNumSamples(); ++k) { const double ts = k / sr; tg.setSample (0, k, 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * (180 + 200 * std::sin (ts * 0.9)) * ts)); }
        p.engine().setTargetMemory ("glide", tg, sr);
        p.engine().generateLibrary(); p.engine().generateTarget(); waitIdle (p);
        p.terrain.set (bandRidge (200));
        juce::AudioBuffer<float> io (2, 512); juce::MidiBuffer midi;
        p.engine().play();
        for (int i = 0; i < 20; ++i) { io.clear(); p.processBlock (io, midi); }
        p.engine().restart();
        for (int i = 0; i < 2; ++i) { io.clear(); p.processBlock (io, midi); }
        std::vector<float> l, r;
        juce::Random rnd (8);
        for (int i = 0; i < (int) (sr * 5 / 512); ++i)
        {
            if (i % 12 == 0) p.engine().setDrag (rnd.nextFloat(), rnd.nextFloat() * 2 - 1, rnd.nextFloat() * 2 - 1);
            if (i % 12 == 6) p.engine().setDrag (0, 0, 0);
            io.clear(); p.processBlock (io, midi);
            l.insert (l.end(), io.getReadPointer (0), io.getReadPointer (0) + 512);
            r.insert (r.end(), io.getReadPointer (1), io.getReadPointer (1) + 512);
        }
        p.engine().setDrag (0, 0, 0);
        const double hf = juce::jmax (hfPeak (l, sr) / juce::jmax (1e-9, peakOf (l)), hfPeak (r, sr) / juce::jmax (1e-9, peakOf (r)));
        check (hf < 0.003 && peakOf (l) > 1e-3, "TERRAIN drag: no clicks while flicking (a new flick every 0.14 s for 5 s)", "HF peak " + juce::String (20 * std::log10 (juce::jmax (1e-12, hf)), 1) + " dB re peak");
    }
    // the page: pulling a dot fast feeds the engine; letting go stops it
    {
        Processor p; p.prepareToPlay (sr, 735);
        Editor ed (p);
        ed.setPage (1);
        p.terrain.set (bandRidge (300, 0.2f));   // (low, so pulling its dot up has room)
        auto& v = ed.terrainPage().view();
        v.settle();
        juce::AudioBuffer<float> io (2, 735); juce::MidiBuffer midi;
        for (int i = 0; i < 10; ++i) { ed.frame(); pump (16); }
        const auto dot = v.handleDot (0);
        v.interaction.mouseDown (dot, {});
        float most = 0;
        for (int i = 1; i <= 12; ++i)
        {
            v.interaction.mouseDrag (dot.translated (3.0f * (float) i, -9.0f * (float) i), {});
            ed.frame(); io.clear(); p.processBlock (io, midi); pump (16);
            most = juce::jmax (most, ed.terrainPage().dragFeelNow().burst);
        }
        const float during = p.engine().dragNow();
        v.interaction.mouseUp (dot.translated (36.0f, -108.0f), {});
        for (int i = 0; i < 200; ++i) { ed.frame(); io.clear(); p.processBlock (io, midi); pump (5); }
        check (most > 0.1f && during > 0.05f && p.engine().dragNow() == 0.0f && ed.terrainPage().dragFeelNow().burst == 0.0f,
               "TERRAIN page: pulling a dot fast is felt by the engine while you drag; nothing once you let go",
               "burst " + juce::String (most, 2) + ", engine " + juce::String (during, 2));
    }
}

// ---------------------------------------------------------------- build 12: TERRAIN uses only Samplebrain's own sound
// (Brandon: "we are using theirs and moving it around through terrain"). TERRAIN may only move the engine's own
// settings; it adds no processing of ours. With STEREO off the right channel is the left search, as upstream.
namespace tland
{
// smooth sines in and a gliding sine target: anything above 9 kHz in the output is a join that doesn't fade (a click)
static void buildSineLibrary (Processor& p, double sr)
{
    setP (p, "auto_build", 0); setP (p, "limiter", 0); setP (p, "declick", 0); setP (p, "src_window", 6); setP (p, "tgt_window", 6); setP (p, "src_overlap", 0.75f); setP (p, "tgt_overlap", 0.75f); setP (p, "src_block", 1000); setP (p, "tgt_block", 1000);
    setP (p, "novelty", 0.3f); setP (p, "volume", 0.3f);
    const double freqs[] = { 110, 165, 220, 330, 440, 660 };
    for (int i = 0; i < 6; ++i)
    {
        juce::AudioBuffer<float> b (1, (int) sr * 2);
        for (int k = 0; k < b.getNumSamples(); ++k) b.setSample (0, k, 0.6f * (float) std::sin (juce::MathConstants<double>::twoPi * freqs[i] * k / sr) * (0.6f + 0.4f * (float) std::sin (k / sr * 3.0)));
        p.engine().addMemorySound ("sine" + juce::String (i), b, sr);
    }
    juce::AudioBuffer<float> tg (1, (int) sr * 6);
    for (int k = 0; k < tg.getNumSamples(); ++k) { const double ts = k / sr; tg.setSample (0, k, 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * (180 + 200 * std::sin (ts * 0.9)) * ts)); }
    p.engine().setTargetMemory ("glide", tg, sr);
    p.engine().generateLibrary(); p.engine().generateTarget(); waitIdle (p);
}
static double rmsOf (const std::vector<float>& a) { double s = 0; for (float v : a) s += (double) v * v; return std::sqrt (s / (double) std::max<size_t> (1, a.size())); }
}

// ---- build 15 coverage: every Samplebrain control that can change live is mapped to the landscape, and each one really moves
static void testTerrainCoverage()
{
    using namespace tland;
    const double sr = 44100;
    // 1. the table: eligible = a search or mix control that needs no rebuild and no restart (STEREO restarts the search, VOLUME is level)
    int eligible = 0, mapped = 0, notMoved = 0; juce::String missing, listed;
    for (auto& pi : paramTable())
    {
        const bool live = (pi.group == Group::Search || pi.group == Group::Mix) && ! pi.rebuildsLibrary && ! pi.rebuildsTarget && juce::String (pi.id) != "stereo";
        if (live) { ++eligible; if (Engine::liveIndexOf (pi.id) >= 0) ++mapped; else missing << pi.id << " "; }
        else if (pi.group != Group::Ours) { ++notMoved; listed << pi.id << " "; }
    }
    int roundTrip = 0; for (int i = 0; i < Engine::kLiveCount; ++i) if (Engine::liveIndexOf (Engine::liveParamId (i)) == i) ++roundTrip;
    check (eligible > 0 && mapped == eligible && roundTrip == Engine::kLiveCount,
           "TERRAIN coverage: 100% of the controls that can change live are mapped to the landscape",
           juce::String (mapped) + " of " + juce::String (eligible) + " live controls mapped (missing: " + missing + "), " + juce::String (notMoved) + " listed as not moved (" + listed.trim() + ")");

    // 2. each mapped control really moves on some land (MOVEMENT up), and every one stays inside its own range
    Processor p; p.prepareToPlay (sr, 512);
    setP (p, "auto_build", 0); setP (p, "limiter", 0);
    p.loadKit ("drums"); p.loadKit ("tones"); p.loadDemoTarget ("vowels");
    setP (p, "ratio", 0.5f); setP (p, "n_ratio", 0.3f); setP (p, "novelty", 0.3f); setP (p, "boredom", 0.5f); setP (p, "stickyness", 0.3f);
    setP (p, "stretch", 2); setP (p, "synapses", 50); setP (p, "slide_error", 2000); setP (p, "fft_start", 10); setP (p, "fft_end", 90);
    setP (p, "autotune", 0.3f); setP (p, "n_mix", 0.3f); setP (p, "target_mix", 0.3f); setP (p, "move_amount", 1.0f); setP (p, "move_rate", 0);
    p.engine().generateLibrary(); p.engine().generateTarget(); waitIdle (p);
    juce::AudioBuffer<float> io (2, 512); juce::MidiBuffer midi;
    p.engine().play();
    bool moved[Engine::kLiveCount] = {}; int outOfRange = 0; bool soundsMoved = false;
    std::vector<Terrain> lands; for (auto& sp : shapePresets()) lands.push_back (sp.make());
    for (juce::uint32 k = 0; k < 10; ++k) lands.push_back (randomTerrain (2000 + k, -1, k % 2 == 1));
    for (auto& l : lands)
    {
        p.terrain.set (l); pump (5);
        for (int i = 0; i < 160; ++i)
        {
            io.clear(); p.processBlock (io, midi);
            for (int c = 0; c < Engine::kLiveCount; ++c)
            {
                const auto v = p.engine().liveValue (c);
                if (v.moving) moved[c] = true;
                const auto& pi = paramInfo (Engine::liveParamId (c));
                if (v.now < pi.min - 1e-3f || v.now > pi.max + 1e-3f) ++outOfRange;
            }
            if (p.engine().soundsMasked()) for (int k = 0; k < (int) p.engine().sounds().size(); ++k) if (p.engine().soundBenched (k)) soundsMoved = true;
        }
    }
    int moves = 0; juce::String still; for (int c = 0; c < Engine::kLiveCount; ++c) { if (moved[c]) ++moves; else still << Engine::liveParamId (c) << " "; }
    check (moves == Engine::kLiveCount && outOfRange == 0 && soundsMoved,
           "TERRAIN coverage: every mapped control visibly moves on some land (and the sounds switch in and out), always inside its own range",
           juce::String (moves) + " of " + juce::String (Engine::kLiveCount) + " controls moved (still: " + still + "), sounds " + (soundsMoved ? "moved" : "not moved") + ", out of range " + juce::String (outOfRange));

    // 3. MOVEMENT 0 or flat land: nothing is moved
    setP (p, "move_amount", 0.0f); p.terrain.set (flatTerrain()); pump (5);
    for (int i = 0; i < 120; ++i) { io.clear(); p.processBlock (io, midi); }
    int stray = 0; for (int c = 0; c < Engine::kLiveCount; ++c) if (p.engine().liveValue (c).moving) ++stray;
    setP (p, "move_amount", 1.0f);
    for (int i = 0; i < 120; ++i) { io.clear(); p.processBlock (io, midi); }
    for (int c = 0; c < Engine::kLiveCount; ++c) if (p.engine().liveValue (c).moving) ++stray;
    check (stray == 0, "TERRAIN coverage: flat land moves nothing, even with MOVEMENT at maximum (and MOVEMENT 0 adds no wandering)", juce::String (stray) + " controls moved");
}

static void testTerrainOwnSound()
{
    using namespace tland;
    const double sr = 44100;
    std::vector<std::pair<juce::String, Terrain>> lands;
    for (auto& sp : shapePresets()) lands.push_back ({ sp.name, sp.make() });
    for (juce::uint32 k = 0; k < 12; ++k) lands.push_back ({ "random " + juce::String ((int) k), randomTerrain (1000 + k, -1, k % 2 == 1) });

    // ---- every shape, random lands (wild ones too) and flicks, live with STEREO off: the right channel IS the left,
    // and every setting TERRAIN moves stays inside the original control's own range
    {
        Processor p;
        p.prepareToPlay (sr, 512);
        setP (p, "auto_build", 0); setP (p, "limiter", 0); setP (p, "stereo", 0); setP (p, "novelty", 0.1f); setP (p, "stickyness", 0.2f);
        p.loadKit ("drums"); p.loadKit ("tones"); p.loadDemoTarget ("vowels");
        p.engine().generateLibrary(); p.engine().generateTarget(); waitIdle (p);
        juce::AudioBuffer<float> io (2, 512); juce::MidiBuffer midi;
        p.engine().play();
        auto& rl = p.engine().leftRenderer();
        double lr = 0, level = 0; int outOfRange = 0; juce::String worst;
        juce::Random rnd (5);
        for (auto& l : lands)
        {
            p.terrain.set (l.second); pump (5);
            for (int i = 0; i < 60; ++i)
            {
                if (i % 9 == 0) p.engine().setDrag (rnd.nextFloat(), rnd.nextFloat() * 2 - 1, rnd.nextFloat() * 2 - 1);
                if (i % 9 == 4) p.engine().setDrag (0, 0, 0);
                io.clear(); p.processBlock (io, midi);
                for (int k = 0; k < 512; ++k) { lr = juce::jmax (lr, (double) std::abs (io.getSample (0, k) - io.getSample (1, k))); level += std::abs (io.getSample (0, k)); }
                const auto* sp = rl.get_params();
                const auto now = p.engine().shapeNow();
                const bool ok = sp->m_stickyness >= 0.0f && sp->m_stickyness <= 1.0f && rl.get_stretch() >= 1 && rl.get_stretch() <= 50
                                && rl.get_target_mix() >= 0.0f && rl.get_target_mix() <= 1.0f && rl.shapedNovelty() >= 0.0f && rl.shapedNovelty() <= 1.0f
                                && now.boredom >= 0.0f && now.boredom <= 1.0f;
                if (! ok) { ++outOfRange; worst = l.first; }
            }
        }
        p.engine().setDrag (0, 0, 0);
        check (lr == 0.0 && level > 0.0, "TERRAIN own sound: every shape, 12 random lands and drag flicks with STEREO off: right = left, bit for bit (Samplebrain's mono, no widener)",
               juce::String ((int) lands.size()) + " lands, biggest left - right " + juce::String (lr, 9));
        check (outOfRange == 0, "TERRAIN own sound: every setting TERRAIN moves (SMOOTH, REPEAT, ORIGINAL MIX, NOVELTY, BOREDOM) stays inside the original control's range",
               outOfRange == 0 ? juce::String() : juce::String (outOfRange) + " buffers out of range, last in " + worst);
    }

    // ---- the rendered WAV (DRAG WAV / record) the same: a shaped land, STEREO off, both channels identical
    {
        Processor p; p.prepareToPlay (sr, 512);
        setP (p, "auto_build", 0); setP (p, "stereo", 0);
        p.loadKit ("drums"); p.loadKit ("tones"); p.loadDemoTarget ("vowels");
        p.engine().generateLibrary(); p.engine().generateTarget(); waitIdle (p);
        for (auto& sp : shapePresets()) if (juce::String (sp.name) == "ARCHIPELAGO") p.terrain.set (sp.make());
        pump (5);
        auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("mosaic-own-sound.wav"); f.deleteFile();
        bool ok = false, done = false;
        p.engine().renderToFile (f, 6, [&] (bool r) { ok = r; done = true; });
        for (int i = 0; i < 6000 && ! done; ++i) pump (10);
        double d = -1, pk = 0;
        if (auto rd = std::unique_ptr<juce::AudioFormatReader> (juce::WavAudioFormat().createReaderFor (f.createInputStream().release(), true)))
        {
            juce::AudioBuffer<float> a (2, (int) rd->lengthInSamples); rd->read (&a, 0, a.getNumSamples(), 0, true, true);
            d = 0; for (int k = 0; k < a.getNumSamples(); ++k) { d = juce::jmax (d, (double) std::abs (a.getSample (0, k) - a.getSample (1, k))); pk = juce::jmax (pk, (double) std::abs (a.getSample (0, k))); }
        }
        f.deleteFile();
        check (ok && d == 0.0 && pk > 1e-4, "TERRAIN own sound: a rendered WAV of a scattered land (ARCHIPELAGO) with STEREO off has identical channels", "left - right " + juce::String (d, 9));
    }

    // ---- never worse: every shape played live has no clicks and no level jump against flat land (the plain engine)
    {
        Processor p; p.prepareToPlay (sr, 512);
        buildSineLibrary (p, sr);
        juce::AudioBuffer<float> io (2, 512); juce::MidiBuffer midi;
        auto playLand = [&] (const Terrain& t)
        {
            p.terrain.set (t); pump (5);
            p.engine().restart(); p.engine().play();
            std::vector<float> l;
            for (int i = 0; i < (int) (sr * 4 / 512); ++i) { io.clear(); p.processBlock (io, midi); l.insert (l.end(), io.getReadPointer (0), io.getReadPointer (0) + 512); }
            p.engine().pause();
            for (int i = 0; i < 4; ++i) { io.clear(); p.processBlock (io, midi); }
            return std::vector<float> (l.begin() + (long) (sr * 0.5), l.end());   // (past the first half second)
        };
        const auto flat = playLand (flatTerrain());
        const double flatRms = rmsOf (flat), flatHf = hfPeak (flat, sr) / juce::jmax (1e-9, peakOf (flat));
        double worstHf = flatHf, lowDb = 0, highDb = 0, worstPk = 0; juce::String clicky, lowName, highName;
        for (auto& l : lands)
        {
            const auto o = playLand (l.second);
            const double hf = hfPeak (o, sr) / juce::jmax (1e-9, peakOf (o)), db = 20 * std::log10 (juce::jmax (1e-9, rmsOf (o)) / juce::jmax (1e-9, flatRms));
            if (hf > worstHf) { worstHf = hf; clicky = l.first; }
            if (db < lowDb) { lowDb = db; lowName = l.first; }
            if (db > highDb) { highDb = db; highName = l.first; }
            worstPk = juce::jmax (worstPk, peakOf (o) / juce::jmax (1e-9, peakOf (flat)));
        }
        if (std::getenv ("MOSAIC_DEBUG") != nullptr) std::printf ("  flat hf %.5f, worst hf %.5f (%s), level %.1f dB (%s) .. +%.1f dB (%s), peak x%.2f\n",
                                                                  flatHf, worstHf, clicky.toRawUTF8(), lowDb, lowName.toRawUTF8(), highDb, highName.toRawUTF8(), worstPk);
        check (worstHf < 0.003 && lowDb > -6.0 && highDb < 3.0 && worstPk < 1.6,
               "TERRAIN never worse: every shape and 12 random lands play without clicks, and no louder (no more than +3 dB) or much quieter (-6 dB) than flat land",
               "worst HF " + juce::String (20 * std::log10 (juce::jmax (1e-12, worstHf)), 1) + " dB re peak (flat " + juce::String (20 * std::log10 (juce::jmax (1e-12, flatHf)), 1)
               + " dB), level " + juce::String (lowDb, 1) + " dB (" + lowName + ") .. +" + juce::String (highDb, 1) + " dB (" + highName + "), peak x" + juce::String (worstPk, 2));
    }
}
