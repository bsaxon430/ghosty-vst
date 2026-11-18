// Ghost Mosaic tests (GPL-2.0-or-later, like the rest of this fork).
//
//   GhostMosaicTests                 run every test, print PASS/FAIL lines, exit code = failures
//   GhostMosaicTests shots <dir>     screenshots of the editor
//   GhostMosaicTests demos <dir>     render the demo WAVs
// Env: MOSAIC_DEMO_SESSION=<path to upstream demo.samplebrain> adds the session null test.
#include "Processor.h"
#include "Editor.h"
#include "DemoKits.h"
#include "Synapses.h"
#include "renderer.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include <cstdio>
#include <cstdlib>

namespace alloccount { void start(); long stop(); }

using namespace mosaic;
using namespace spiralcore;

static int gFails = 0, gPasses = 0;
static void check (bool ok, const juce::String& name, const juce::String& detail = {})
{
    std::printf ("%s %s%s\n", ok ? "PASS" : "FAIL", name.toRawUTF8(), detail.isNotEmpty() ? (" -- " + detail).toRawUTF8() : "");
    std::fflush (stdout);
    (ok ? gPasses : gFails)++;
}

static void pump (int ms = 5) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }
static void waitIdle (Processor& p, int timeoutMs = 900000)
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    do { pump (5); } while ((p.engine().busy()) && juce::Time::getMillisecondCounter() < end);
    pump (20);
}

// ---------------------------------------------------------------- sounds for tests (all synthesised)
static juce::StringArray registerKit (const juce::String& which, double sr, const juce::String& tag)
{
    juce::StringArray keys;
    for (auto& s : demo::kit (which, sr))
    {
        const auto key = "mem:test:" + tag + ":" + s.name;
        compat::putMemorySound (key, s.audio, sr);
        keys.add (key);
    }
    return keys;
}
static juce::String registerTarget (const juce::String& which, double sr)
{
    const auto key = "mem:test:target:" + which;
    if (which == "beat") compat::putMemorySound (key, demo::beatLoop (sr), sr);
    else if (which == "vowels") compat::putMemorySound (key, demo::vowelPhrase (sr), sr);
    else compat::putMemorySound (key, demo::pianoLine (sr), sr);
    return key;
}

struct BuildSpec { u32 bs = 1000; float ov = 0.75f; window::type w = window::DODGY; };

static std::unique_ptr<brain> upstreamLibrary (const juce::StringArray& keys, BuildSpec s, bool parallelSynapses)
{
    auto b = std::make_unique<brain>();
    for (auto& k : keys) b->load_sound (k.toStdString(), brain::MIX);
    const float ov = (float) s.bs * s.ov;
    b->init (s.bs, (u32) ov, s.w);
    search_params p (1, 0, 0, 100, 0);
    if (parallelSynapses) { std::atomic<float> prog { 0 }; buildSynapses (*b, p, prog); }
    else b->build_synapses_fixed (p);
    return b;
}
static std::unique_ptr<brain> upstreamTarget (const juce::String& key, brain::stereo_mode m, BuildSpec s)
{
    auto b = std::make_unique<brain>();
    b->load_sound (key.toStdString(), m);
    const float ov = (float) s.bs * s.ov;
    b->init (s.bs, (u32) ov, s.w);
    return b;
}

struct Knobs
{
    float ratio = 0.5f, n_ratio = 0, novelty = 0, boredom = 0.9f, sticky = 0, autotune = 0, n_mix = 0, target_mix = 0, volume = 1;
    u32 fft0 = 0, fft1 = 100, stretch = 1, synapses = 20; int algo = 0; double slide = 1000;
};
template <class R> static void applyKnobs (R& r, const Knobs& k)
{
    auto* p = r.get_params();
    p->m_ratio = k.ratio; p->m_n_ratio = k.n_ratio; p->m_fft1_start = k.fft0; p->m_fft1_end = k.fft1;
    p->m_usage_importance = k.novelty; p->m_stickyness = k.sticky; p->m_num_synapses = k.synapses;
    r.set_autotune (k.autotune); r.set_n_mix (k.n_mix); r.set_target_mix (k.target_mix); r.set_volume (k.volume);
    r.set_search_algo ((renderer::search_algo) k.algo); r.set_stretch (k.stretch); r.set_slide_error (k.slide);
    r.set_playing (true);
}

static double maxDiff (const std::vector<float>& a, const std::vector<float>& b, size_t offB = 0)
{
    double m = 0;
    const size_t n = juce::jmin (a.size(), b.size() > offB ? b.size() - offB : 0);
    for (size_t i = 0; i < n; ++i) m = juce::jmax (m, (double) std::abs (a[i] - b[i + offB]));
    return n == 0 ? 1e9 : m;
}
static double peakOf (const std::vector<float>& a) { double m = 0; for (float v : a) m = juce::jmax (m, (double) std::abs (v)); return m; }
static juce::String nullDetail (double diff, double peak)
{
    const double db = diff <= 0 ? -999 : 20 * std::log10 (diff / juce::jmax (1e-12, peak));
    return "max diff " + juce::String (diff, 12) + " (" + (diff <= 0 ? juce::String ("bit-exact") : juce::String (db, 1) + " dB re peak") + "), peak " + juce::String (peak, 4);
}

// ---------------------------------------------------------------- 1. renderer port vs upstream renderer
static void testRendererNull()
{
    const double sr = 44100;
    struct Case { const char* name; const char* kit; const char* tgt; BuildSpec lib, tg; Knobs k; bool stereo; int buf; };
    std::vector<Case> cases;
    { Case c { "basic, defaults", "drums", "vowels", {}, {}, {}, false, 2048 }; cases.push_back (c); }
    { Case c { "basic, mfcc + novelty + boredom + sticky", "tones", "beat", { 1500, 0.75f, window::HANN }, { 1200, 0.5f, window::BLACKMAN }, {}, false, 512 };
      c.k.ratio = 1; c.k.novelty = 0.4f; c.k.boredom = 0.5f; c.k.sticky = 0.3f; cases.push_back (c); }
    { Case c { "reversed", "noise", "piano", { 800, 0.5f, window::BARTLETT }, { 800, 0.5f, window::BARTLETT }, {}, false, 1024 }; c.k.algo = 1; c.k.ratio = 0; cases.push_back (c); }
    { Case c { "synaptic, stretch 3, fft band 10-60", "drums", "piano", { 1000, 0.75f, window::DODGY }, { 900, 0.6f, window::HAMMING }, {}, false, 2048 };
      c.k.algo = 2; c.k.stretch = 3; c.k.fft0 = 10; c.k.fft1 = 60; c.k.synapses = 40; cases.push_back (c); }
    { Case c { "slide, slide error 300", "tones", "vowels", { 2048, 0.75f, window::HANN }, { 2048, 0.75f, window::HANN }, {}, false, 256 };
      c.k.algo = 3; c.k.slide = 300; c.k.synapses = 60; cases.push_back (c); }
    { Case c { "autotune + flatten + original mix + pitch only", "tones", "vowels", { 1000, 0.75f, window::GAUSSIAN }, { 1000, 0.75f, window::FLAT_TOP }, {}, false, 2048 };
      c.k.autotune = 0.4f; c.k.n_mix = 0.5f; c.k.target_mix = 0.3f; c.k.n_ratio = 0.6f; c.k.volume = 3.0f; cases.push_back (c); }
    { Case c { "stereo, synaptic", "drums", "beat", { 700, 0.25f, window::RECTANGLE }, { 700, 0.25f, window::RECTANGLE }, {}, true, 1024 };
      c.k.algo = 2; c.k.novelty = 0.2f; cases.push_back (c); }

    for (auto& c : cases)
    {
        auto keys = registerKit (c.kit, sr, c.kit);
        auto tkey = registerTarget (c.tgt, sr);
        auto srcA = upstreamLibrary (keys, c.lib, false), srcB = upstreamLibrary (keys, c.lib, true);
        // the parallel network build must equal build_synapses_fixed
        bool same = srcA->get_num_blocks() == srcB->get_num_blocks();
        for (u32 i = 0; same && i < srcA->get_num_blocks(); ++i)
            same = srcA->get_block (i).get_synapse_const() == srcB->get_block (i).get_synapse_const();
        check (same, juce::String ("network build identical to build_synapses_fixed: ") + c.name, juce::String ((int) srcA->get_num_blocks()) + " pieces");

        auto tlA = upstreamTarget (tkey, brain::LEFT, c.tg), trA = upstreamTarget (tkey, brain::RIGHT, c.tg);
        auto tlB = upstreamTarget (tkey, brain::LEFT, c.tg), trB = upstreamTarget (tkey, brain::RIGHT, c.tg);
        const int buffers = (int) (sr * 14 / c.buf);
        std::vector<float> outA, outB, outAR, outBR;
        {
            renderer L (*srcA, *tlA), R (*srcA, *trA);
            applyKnobs (L, c.k); applyKnobs (R, c.k);
            srcA->set_usage_falloff (c.k.boredom);
            std::srand (4242);
            std::vector<float> l ((size_t) c.buf), r ((size_t) c.buf);
            for (int b = 0; b < buffers; ++b)
            {
                std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f);
                L.process ((u32) c.buf, l.data());
                if (c.stereo) R.process ((u32) c.buf, r.data()); else r = l;
                outA.insert (outA.end(), l.begin(), l.end()); outAR.insert (outAR.end(), r.begin(), r.end());
            }
        }
        {
            RtRenderer L, R;
            L.init (srcB.get(), tlB.get()); R.init (srcB.get(), trB.get());
            applyKnobs (L, c.k); applyKnobs (R, c.k);
            srcB->set_usage_falloff (c.k.boredom);
            std::srand (4242);
            std::vector<float> l ((size_t) c.buf), r ((size_t) c.buf);
            for (int b = 0; b < buffers; ++b)
            {
                std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f);
                L.process ((u32) c.buf, l.data());
                if (c.stereo) R.process ((u32) c.buf, r.data()); else r = l;
                outB.insert (outB.end(), l.begin(), l.end()); outBR.insert (outBR.end(), r.begin(), r.end());
            }
        }
        const double d = juce::jmax (maxDiff (outA, outB), maxDiff (outAR, outBR));
        check (d == 0.0 && peakOf (outA) > 1e-4, juce::String ("null test, realtime renderer vs upstream renderer: ") + c.name, nullDetail (d, peakOf (outA)));
    }
}

// ---------------------------------------------------------------- 2. whole plug-in engine vs upstream (fresh build)
static void setP (Processor& p, const char* id, float v) { p.setParam (id, v, false); }

static std::vector<float> runEngine (Processor& p, int buffers, int bufN, std::vector<float>* right = nullptr)
{
    juce::AudioBuffer<float> io (2, bufN);
    juce::MidiBuffer midi;
    std::vector<float> out;
    for (int b = 0; b < buffers; ++b)
    {
        io.clear();
        p.engine().process (io, midi, false, false);
        out.insert (out.end(), io.getReadPointer (0), io.getReadPointer (0) + bufN);
        if (right) right->insert (right->end(), io.getReadPointer (1), io.getReadPointer (1) + bufN);
    }
    return out;
}

static void testEngineNull()
{
    const double sr = 44100;
    struct Case { const char* name; const char* kit; const char* tgt; int algo; float novelty; float sticky; u32 bs; float ov; int win; bool stereo; };
    const Case cases[] = {
        { "drums -> vowels, basic", "drums", "vowels", 0, 0.1f, 0, 1500, 0.75f, 6, false },
        { "tones -> beat, synaptic, stereo", "tones", "beat", 2, 0.2f, 0.2f, 1000, 0.75f, 0, true },
        { "noise -> piano, slide", "noise", "piano", 3, 0, 0, 2048, 0.5f, 2, false },
    };
    for (auto& c : cases)
    {
        Processor p;
        p.engine().prepare (sr, 2048);
        setP (p, "auto_build", 0);   // (SAFETY LIMITER and SOFT START/STOP left at their defaults: off, the output is upstream's)
        setP (p, "algo", (float) c.algo); setP (p, "novelty", c.novelty); setP (p, "stickyness", c.sticky);
        setP (p, "src_block", (float) c.bs); setP (p, "tgt_block", (float) c.bs); setP (p, "src_overlap", c.ov); setP (p, "tgt_overlap", c.ov);
        setP (p, "src_window", (float) c.win); setP (p, "tgt_window", (float) c.win); setP (p, "stereo", c.stereo ? 1.0f : 0.0f);
        setP (p, "volume", 0.3f);
        for (auto& s : demo::kit (c.kit, sr)) p.engine().addMemorySound (s.name, s.audio, sr);
        p.loadDemoTarget (c.tgt);
        p.engine().generateLibrary(); p.engine().generateTarget();
        waitIdle (p);
        auto sounds = p.engine().sounds();
        juce::StringArray keys; for (auto& s : sounds) keys.add (s.key);
        // upstream, built the way process_thread does
        BuildSpec spec { c.bs, (float) (int) std::lround (c.ov * 100) / 100.0f, (window::type) c.win };
        auto src = upstreamLibrary (keys, spec, false);
        juce::String tkey;
        { // the engine's target key is private: rebuild the same target audio under a test key
            tkey = registerTarget (c.tgt, sr);
        }
        auto tl = upstreamTarget (tkey, brain::LEFT, spec), tr = upstreamTarget (tkey, brain::RIGHT, spec);
        Knobs k; k.algo = c.algo; k.novelty = (float) (int) std::lround (c.novelty * 100) / 100.0f; k.sticky = (float) (int) std::lround (c.sticky * 100) / 100.0f;
        k.volume = 0.3f * 10; k.boredom = 0.9f;
        const int bufN = 2048, buffers = (int) (sr * 12 / bufN);
        std::vector<float> ref, refR;
        {
            renderer L (*src, *tl), R (*src, *tr);
            applyKnobs (L, k); applyKnobs (R, k);
            src->set_usage_falloff (k.boredom);
            std::srand (99);
            L.reset(); R.reset();            // /generate_brain resets both renderers
            std::vector<float> l (bufN), r (bufN);
            for (int b = 0; b < buffers + 1; ++b)
            {
                std::fill (l.begin(), l.end(), 0.0f); std::fill (r.begin(), r.end(), 0.0f);
                L.process (bufN, l.data());
                if (c.stereo) R.process (bufN, r.data()); else r = l;
                ref.insert (ref.end(), l.begin(), l.end()); refR.insert (refR.end(), r.begin(), r.end());
            }
        }
        std::srand (99);
        p.engine().play();
        std::vector<float> outR;
        auto out = runEngine (p, buffers, bufN, &outR);
        // (build 13: with the extras off the limiter is not in the path at all: no delay, the upstream output itself)
        const double d = juce::jmax (maxDiff (out, ref), maxDiff (outR, refR));
        check (d == 0.0 && peakOf (out) > 1e-4, juce::String ("null test, plug-in engine vs upstream engine: ") + c.name, nullDetail (d, peakOf (ref)));
    }
}

// ---------------------------------------------------------------- 3. the upstream demo session
static void testDemoSession()
{
    const auto path = juce::SystemStats::getEnvironmentVariable ("MOSAIC_DEMO_SESSION", {});
    if (path.isEmpty() || ! juce::File (path).existsAsFile()) { std::printf ("SKIP demo session (MOSAIC_DEMO_SESSION not set)\n"); return; }
    const double sr = 44100;
    const int bufN = 2048, buffers = (int) (sr * 20 / bufN);
    for (int pass = 0; pass < 2; ++pass)
    {
        Processor p;
        p.engine().prepare (sr, bufN);
        setP (p, "limiter", 0); setP (p, "declick", 0); setP (p, "auto_build", 0);
        p.engine().loadSessionFile (juce::File (path));
        waitIdle (p);
        // upstream: process_thread::load_session into fresh objects
        brain src, tl, tr;
        renderer L (src, tl), R (src, tr);
        u32 sbs, tbs; float sov, tov; window::type sw, tw;
        {
            std::ifstream ifs (path.toStdString().c_str(), std::ios::binary);
            u32 version = 0;
            ifs || version; ifs || L; ifs || R;
            ifs || sbs || sov; ifs || tbs || tov; ifs || sw || tw;
            ifs || src; ifs || tl; ifs || tr;
        }
        if (pass == 1)
        {
            // second pass: also switch to the network search and some novelty (a "different settings" case)
            setP (p, "algo", 2); setP (p, "novelty", 0.25f);
            L.set_search_algo (renderer::SYNAPTIC); R.set_search_algo (renderer::SYNAPTIC);
            L.get_params()->m_usage_importance = 0.25f; R.get_params()->m_usage_importance = 0.25f;
        }
        L.set_playing (true); R.set_playing (true);
        std::vector<float> ref;
        std::srand (7);
        {
            std::vector<float> l (bufN);
            for (int b = 0; b < buffers + 1; ++b) { std::fill (l.begin(), l.end(), 0.0f); L.process (bufN, l.data()); ref.insert (ref.end(), l.begin(), l.end()); }
        }
        std::srand (7);
        p.engine().play();
        auto out = runEngine (p, buffers, bufN);
        const double d = maxDiff (out, ref);
        check (d == 0.0 && peakOf (ref) > 1e-5, juce::String ("null test, upstream demo session (") + (pass == 0 ? "as saved" : "network search + novelty") + ")",
               nullDetail (d, peakOf (ref)) + ", " + juce::String ((int) src.get_num_blocks()) + " library pieces, " + juce::String ((int) tl.get_num_blocks()) + " target pieces");
    }
}

// ---------------------------------------------------------------- 4. sessions round-trip
static void testSessions()
{
    const double sr = 44100;
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("mosaic-tests");
    dir.createDirectory();
    Processor a;
    a.engine().prepare (sr, 512);
    setP (a, "auto_build", 0);
    setP (a, "novelty", 0.33f); setP (a, "algo", 2); setP (a, "tgt_block", 1200);
    a.loadKit ("tones"); a.loadDemoTarget ("beat");
    a.engine().generateLibrary(); a.engine().generateTarget();
    waitIdle (a);
    // .samplebrain: save, load into another, save again: same bytes
    auto f1 = dir.getChildFile ("a.samplebrain"), f2 = dir.getChildFile ("b.samplebrain");
    a.engine().saveSessionFile (f1); waitIdle (a);
    Processor b; b.engine().prepare (sr, 512); setP (b, "auto_build", 0);
    b.engine().loadSessionFile (f1); waitIdle (b);
    b.engine().saveSessionFile (f2); waitIdle (b);
    // Upstream's sample stream (spiralcore/sample.cpp) writes sample::m_sample_type, which nothing
    // ever sets: 4 bytes of whatever was in memory, after every "sample" tag. Samplebrain's own
    // load + save changes them too, so they are blanked before comparing; every other byte must match.
    auto loadMasked = [] (const juce::File& f, juce::MemoryBlock& m)
    {
        f.loadFileAsData (m);
        static const char tag[] = { 6, 0, 0, 0, 0, 0, 0, 0, 's', 'a', 'm', 'p', 'l', 'e', 1, 0, 0, 0 };
        auto* d = static_cast<char*> (m.getData());
        int masked = 0;
        for (size_t i = 0; i + sizeof (tag) + 4 <= m.getSize(); ++i)
            if (std::memcmp (d + i, tag, sizeof (tag)) == 0) { std::memset (d + i + sizeof (tag), 0, 4); i += sizeof (tag) + 3; ++masked; }
        return masked;
    };
    juce::MemoryBlock m1, m2; loadMasked (f1, m1); loadMasked (f2, m2);
    auto firstDiff = [] (const juce::MemoryBlock& x, const juce::MemoryBlock& y)
    {
        const size_t n = juce::jmin (x.getSize(), y.getSize());
        for (size_t i = 0; i < n; ++i) if (x[i] != y[i]) return juce::String (" first difference at byte ") + juce::String ((juce::int64) i) + " of " + juce::String ((juce::int64) x.getSize()) + " / " + juce::String ((juce::int64) y.getSize());
        return x.getSize() == y.getSize() ? juce::String() : juce::String (" sizes ") + juce::String ((juce::int64) x.getSize()) + " / " + juce::String ((juce::int64) y.getSize());
    };
    check (m1.getSize() > 1000 && m1 == m2, "session file (.samplebrain) round-trips byte for byte", juce::String ((int) m1.getSize()) + " bytes" + firstDiff (m1, m2));
    check (std::abs (b.getParam ("novelty") - 0.33f) < 1e-6 && (int) b.getParam ("algo") == 2 && (int) b.getParam ("tgt_block") == 1200, "session file restores the settings",
           "novelty " + juce::String (b.getParam ("novelty")) + ", algo " + juce::String (b.getParam ("algo")) + ", target piece " + juce::String (b.getParam ("tgt_block")));
    // .brain
    auto l1 = dir.getChildFile ("a.brain"), l2 = dir.getChildFile ("b.brain");
    a.engine().saveLibraryFile (l1); waitIdle (a);
    b.engine().loadLibraryFile (l1); waitIdle (b);
    b.engine().saveLibraryFile (l2); waitIdle (b);
    juce::MemoryBlock b1, b2; loadMasked (l1, b1); loadMasked (l2, b2);
    // and the network search's current piece (brain::m_current_block_index, 24 bytes from the end):
    // loading a library resets the players, which picks a random starting piece (brain::jiggle), as upstream
    for (auto* m : { &b1, &b2 }) if (m->getSize() > 24) std::memset (static_cast<char*> (m->getData()) + m->getSize() - 24, 0, 4);
    check (b1.getSize() > 1000 && b1 == b2, "library file (.brain) round-trips byte for byte", juce::String ((int) b1.getSize()) + " bytes" + firstDiff (b1, b2));
    // plug-in state: settings + sounds + target, then identical offline renders
    juce::MemoryBlock st; a.getStateInformation (st);
    Processor c; c.engine().prepare (sr, 512);
    c.setStateInformation (st.getData(), (int) st.getSize());
    waitIdle (c);
    check (c.engine().sounds().size() == a.engine().sounds().size() && c.engine().targetName() == a.engine().targetName()
           && std::abs (c.getParam ("novelty") - 0.33f) < 1e-6, "plug-in state restores sounds, target and settings", juce::String ((int) st.getSize()) + " bytes of state");
    auto w1 = dir.getChildFile ("a.wav"), w2 = dir.getChildFile ("c.wav");
    bool d1 = false, d2 = false;
    std::srand (5); a.engine().renderToFile (w1, 10, [&] (bool ok) { d1 = ok; }); waitIdle (a);
    std::srand (5); c.engine().renderToFile (w2, 10, [&] (bool ok) { d2 = ok; }); waitIdle (c);
    pump (50);
    juce::MemoryBlock r1, r2; w1.loadFileAsData (r1); w2.loadFileAsData (r2);
    check (d1 && d2 && r1.getSize() > 10000 && r1 == r2, "restored state renders the identical WAV", juce::String ((int) r1.getSize()) + " bytes");
}

// ---------------------------------------------------------------- 5. clicks at the joins
static double hfPeak (const std::vector<float>& x, double sr)
{
    // 4th-order high-pass at 9 kHz (two biquads); the library + target here have nothing above ~2 kHz,
    // so what comes out up there is joins that don't fade to zero
    juce::dsp::IIR::Coefficients<float>::Ptr c = juce::dsp::IIR::Coefficients<float>::makeHighPass (sr, 9000.0f);
    juce::dsp::IIR::Filter<float> f1, f2; f1.coefficients = c; f2.coefficients = c;
    double m = 0;
    for (size_t i = 0; i < x.size(); ++i) { const float y = f2.processSample (f1.processSample (x[i])); if (i > 2000) m = juce::jmax (m, (double) std::abs (y)); }
    return m;
}
static void testClicks()
{
    const double sr = 44100;
    // smooth sines (no content above 2 kHz)
    juce::StringArray keys;
    const double freqs[] = { 110, 165, 220, 330, 440, 660 };
    for (int i = 0; i < 6; ++i)
    {
        juce::AudioBuffer<float> b (1, (int) sr * 2);
        for (int n = 0; n < b.getNumSamples(); ++n) b.setSample (0, n, 0.6f * (float) std::sin (juce::MathConstants<double>::twoPi * freqs[i] * n / sr) * (0.6f + 0.4f * (float) std::sin (n / sr * 3.0)));
        const auto key = "mem:test:sine" + juce::String (i);
        compat::putMemorySound (key, b, sr); keys.add (key);
    }
    juce::AudioBuffer<float> t (1, (int) sr * 6);
    for (int n = 0; n < t.getNumSamples(); ++n) { const double ts = n / sr; t.setSample (0, n, 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * (180 + 200 * std::sin (ts * 0.9)) * ts)); }
    compat::putMemorySound ("mem:test:glide", t, sr);
    struct W { window::type w; const char* name; bool shouldBeClean; };
    const W ws[] = { { window::DODGY, "soft ramp (dodgy)", true }, { window::HANN, "hann", true }, { window::BLACKMAN, "blackman", true }, { window::BARTLETT, "triangle", true },
                     { window::RECTANGLE, "hard edge (rectangle) - expected to click, proves the detector", false } };
    double cleanWorst = 0, rectHF = 0;
    for (auto& w : ws)
    {
        BuildSpec s { 1000, 0.75f, w.w };
        auto src = upstreamLibrary (keys, s, true);
        auto tl = upstreamTarget ("mem:test:glide", brain::LEFT, s);
        RtRenderer r; r.init (src.get(), tl.get());
        Knobs k; k.volume = 1.0f; k.novelty = 0.3f; applyKnobs (r, k);
        std::vector<float> out, buf (512);
        for (int b = 0; b < (int) (sr * 5 / 512); ++b) { std::fill (buf.begin(), buf.end(), 0.0f); r.process (512, buf.data()); out.insert (out.end(), buf.begin(), buf.end()); }
        const double hf = hfPeak (out, sr), pk = peakOf (out);
        const double rel = hf / juce::jmax (1e-9, pk);
        if (w.shouldBeClean) { cleanWorst = juce::jmax (cleanWorst, rel); check (rel < 0.003, juce::String ("no clicks at piece joins: ") + w.name, "HF peak " + juce::String (20 * std::log10 (juce::jmax (1e-12, rel)), 1) + " dB re peak"); }
        else rectHF = rel;
    }
    check (rectHF > cleanWorst * 10, "click detector sees the hard-edge window's clicks", "rectangle HF " + juce::String (20 * std::log10 (juce::jmax (1e-12, rectHF)), 1) + " dB");
}

// ---------------------------------------------------------------- 6. true peak
static double truePeakDb (const juce::AudioBuffer<float>& b, int start = 0)
{
    // 8x windowed-sinc reconstruction (64 taps per phase), independent of the limiter's own 4x estimate
    double m = 0;
    for (int c = 0; c < b.getNumChannels(); ++c)
    {
        const float* x = b.getReadPointer (c);
        const int n = b.getNumSamples();
        for (int i = juce::jmax (start, 32); i < n - 33; ++i)
            for (int ph = 0; ph < 8; ++ph)
            {
                const double f = ph / 8.0;
                double acc = 0;
                for (int j = -31; j <= 32; ++j)
                {
                    const double tt = f - j;
                    const double s = std::abs (tt) < 1e-12 ? 1.0 : std::sin (M_PI * tt) / (M_PI * tt);
                    const double w = 0.5 + 0.5 * std::cos (M_PI * tt / 33.0);
                    acc += x[i + j] * s * w;
                }
                m = juce::jmax (m, std::abs (acc));
            }
    }
    return 20 * std::log10 (juce::jmax (1e-12, m));
}
static void testTruePeak()
{
    const double sr = 44100;
    Processor p;
    p.engine().prepare (sr, 512);
    setP (p, "auto_build", 0); setP (p, "declick", 0); setP (p, "limiter", 1);   // (an extra: off by default)
    setP (p, "volume", 0.99f); setP (p, "target_mix", 0.4f);    // as loud as the original dial goes (gain 9.9)
    p.loadKit ("drums"); p.loadDemoTarget ("beat");
    p.engine().generateLibrary(); p.engine().generateTarget(); waitIdle (p);
    p.engine().play();
    std::vector<float> r;
    auto l = runEngine (p, (int) (sr * 8 / 512), 512, &r);
    juce::AudioBuffer<float> b (2, (int) l.size());
    for (size_t i = 0; i < l.size(); ++i) { b.setSample (0, (int) i, l[i]); b.setSample (1, (int) i, r[i]); }
    const double tp = truePeakDb (b);
    double raw = 0; for (float v : l) raw = juce::jmax (raw, (double) std::abs (v));
    check (tp <= -1.0, "SAFETY LIMITER on: true peak <= -1 dBTP with the volume dial at maximum", "true peak " + juce::String (tp, 2) + " dBTP, limiter's deepest gain " + juce::String (juce::Decibels::gainToDecibels (p.engine().limiterGain()), 1) + " dB");
    // and the limiter is exactly transparent below the ceiling
    TruePeakLimiter lim; lim.prepare (sr, 2);
    juce::AudioBuffer<float> q (2, 20000);
    for (int i = 0; i < q.getNumSamples(); ++i) { const float v = 0.4f * (float) std::sin (i * 0.05); q.setSample (0, i, v); q.setSample (1, i, -v); }
    auto orig = q;
    float* ch[2] = { q.getWritePointer (0), q.getWritePointer (1) };
    lim.process (ch, q.getNumSamples());
    double dd = 0;
    for (int i = TruePeakLimiter::kLatency; i < q.getNumSamples(); ++i) dd = juce::jmax (dd, (double) std::abs (q.getSample (0, i) - orig.getSample (0, i - TruePeakLimiter::kLatency)));
    check (dd == 0.0, "limiter is bit-transparent below the ceiling (only delays)", "max diff " + juce::String (dd, 9));
}

// ---------------------------------------------------------------- 7. no allocation on the audio thread
static void testNoAlloc()
{
    const double sr = 48000;
    Processor p;
    p.prepareToPlay (sr, 256);
    setP (p, "auto_build", 0);
    p.loadKit ("drums");
    if (! p.isEffect()) p.loadDemoTarget ("vowels");
    p.engine().generateLibrary(); p.engine().generateTarget(); waitIdle (p);
    juce::AudioBuffer<float> io (2, 256);
    juce::MidiBuffer midi;
    p.engine().play();
    // warm up (first buffers pick up the swaps)
    for (int i = 0; i < 20; ++i) { io.clear(); p.processBlock (io, midi); }
    midi.ensureSize (64);
    long worst = 0;
    const char* algos[] = { "closest", "reversed", "network", "network + wait" };
    for (int a = 0; a < 4; ++a)
    {
        setP (p, "algo", (float) a); setP (p, "stereo", a % 2 ? 1.0f : 0.0f); setP (p, "autotune", a == 3 ? 0.4f : 0.0f);
        for (int i = 0; i < 4; ++i) { io.clear(); p.processBlock (io, midi); }   // let it apply
        alloccount::start();
        for (int i = 0; i < (int) (sr * 4 / 256); ++i)
        {
            for (int c = 0; c < 2; ++c) for (int s = 0; s < 256; ++s) io.setSample (c, s, 0.3f * (float) std::sin ((i * 256 + s) * 0.01));
            if (! p.isEffect() && i % 50 == 0) { midi.clear(); midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0); }
            p.processBlock (io, midi);
            midi.clear();
        }
        const long n = alloccount::stop();
        worst = juce::jmax (worst, n);
        check (n == 0, juce::String ("no allocation on the audio thread (") + algos[a] + ", " + (a % 2 ? "stereo" : "mono") + ")", juce::String ((int) n) + " allocations in 4 s");
    }
    // rebuilding the library while playing: still none on the audio thread
    p.engine().generateLibrary();
    alloccount::start();
    for (int i = 0; i < 400; ++i) { io.clear(); p.processBlock (io, midi); juce::Thread::sleep (1); }
    const long n = alloccount::stop();
    waitIdle (p);
    for (int i = 0; i < 20; ++i) { io.clear(); p.processBlock (io, midi); }   // pick up the swap (it allocates nothing either)
    check (n == 0, "no allocation on the audio thread while the library rebuilds", juce::String ((int) n));
}

// ---------------------------------------------------------------- 8. big libraries: no stalls
static void testLargeLibrary()
{
    const double sr = 44100;
    Processor p;
    p.prepareToPlay (sr, 512);
    setP (p, "auto_build", 0); setP (p, "algo", 2);
    // a minute of sound: 12 five-second sounds made from the kits (about 10,000 pieces)
    juce::Random rnd (3);
    for (int i = 0; i < 12; ++i)
    {
        auto kit = demo::kit (demo::kitNames()[i % 4], sr);
        juce::AudioBuffer<float> b (1, (int) (sr * 5));
        b.clear();
        for (int k = 0; k < 14; ++k)
        {
            auto& s = kit[(size_t) rnd.nextInt ((int) kit.size())].audio;
            const int at = rnd.nextInt (b.getNumSamples() - 1000);
            b.addFrom (0, at, s, 0, 0, juce::jmin (s.getNumSamples(), b.getNumSamples() - at), 0.5f);
        }
        p.engine().addMemorySound ("big " + juce::String (i), b, sr);
    }
    p.loadDemoTarget ("vowels");
    p.engine().generateTarget(); waitIdle (p);
    // play an earlier small library while the big one builds, and time every audio buffer
    juce::AudioBuffer<float> io (2, 512);
    juce::MidiBuffer midi;
    p.engine().play();
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    p.engine().generateLibrary();
    double worst = 0; int buffers = 0;
    while (p.engine().busy() && juce::Time::getMillisecondCounterHiRes() - t0 < 900000)
    {
        const auto a = juce::Time::getMillisecondCounterHiRes();
        io.clear(); p.processBlock (io, midi);
        worst = juce::jmax (worst, juce::Time::getMillisecondCounterHiRes() - a);
        ++buffers;
        pump (1);
    }
    const double buildSec = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
    auto pic = p.engine().libraryPicture();
    const int nb = pic ? pic->numBlocks : 0;
    check (worst < 11.6 * 0.5, "audio keeps running while a big library builds (" + juce::String (nb) + " pieces)",
           "slowest buffer " + juce::String (worst, 2) + " ms of 11.6 ms, build " + juce::String (buildSec, 1) + " s, " + juce::String (buffers) + " buffers meanwhile");
    // playing the big library: the network search (constant time) per buffer
    for (int i = 0; i < 10; ++i) { io.clear(); p.processBlock (io, midi); }
    double sum = 0, mx = 0;
    for (int i = 0; i < 400; ++i)
    {
        const auto a = juce::Time::getMillisecondCounterHiRes();
        io.clear(); p.processBlock (io, midi);
        const double d = juce::Time::getMillisecondCounterHiRes() - a;
        sum += d; mx = juce::jmax (mx, d);
    }
    check (mx < 11.6 * 0.5, "big library plays in Network style within budget", "average " + juce::String (sum / 400, 3) + " ms, slowest " + juce::String (mx, 3) + " ms per 512-sample buffer");
    setP (p, "algo", 0);
    for (int i = 0; i < 4; ++i) { io.clear(); p.processBlock (io, midi); }
    double sumB = 0;
    for (int i = 0; i < 100; ++i) { const auto a = juce::Time::getMillisecondCounterHiRes(); io.clear(); p.processBlock (io, midi); sumB += juce::Time::getMillisecondCounterHiRes() - a; }
    std::printf ("INFO Closest (basic) search over %d pieces: %.3f ms per 512-sample buffer (upstream's note: 'not usable with large brains')\n", nb, sumB / 100);
}

// ---------------------------------------------------------------- 9. effect: live input, latency
static void testEffect()
{
    const double sr = 48000;
    const int bufN = 480;
    {
        Processor p;
        setP (p, "auto_build", 0);
        setP (p, "target_mix", 1.0f); setP (p, "declick", 0); setP (p, "volume", 0.5f); setP (p, "tgt_overlap", 0.0f); setP (p, "tgt_window", 7);
        p.prepareToPlay (sr, bufN);
        p.loadKit ("tones");
        p.engine().generateLibrary(); waitIdle (p);
        p.prepareToPlay (sr, bufN); waitIdle (p);
        const int lat = p.getLatencySamples();
        juce::AudioBuffer<float> io (2, bufN);
        juce::MidiBuffer midi;
        std::vector<float> out;
        const int impulseAt = 48000 + 333;
        for (int b = 0; b < 400; ++b)
        {
            io.clear();
            for (int i = 0; i < bufN; ++i) if (b * bufN + i == impulseAt) { io.setSample (0, i, 0.8f); io.setSample (1, i, 0.8f); }
            p.processBlock (io, midi);
            out.insert (out.end(), io.getReadPointer (0), io.getReadPointer (0) + bufN);
            if (auto* lt = p.engine().liveTarget()) lt->waitIdle (200);
        }
        int at = -1; float mx = 0;
        for (size_t i = 0; i < out.size(); ++i) if (std::abs (out[i]) > mx) { mx = std::abs (out[i]); at = (int) i; }
        check (at - impulseAt == lat, "effect reports its true latency (target piece through ORIGINAL MIX)", "reported " + juce::String (lat) + ", measured " + juce::String (at - impulseAt));
        // the DRY path lines up too
        setP (p, "target_mix", 0.0f); setP (p, "dry", 1.0f); setP (p, "volume", 0.0f);
        out.clear();
        for (int b = 0; b < 400; ++b)
        {
            io.clear();
            for (int i = 0; i < bufN; ++i) if (b * bufN + i == impulseAt) { io.setSample (0, i, 0.8f); io.setSample (1, i, 0.8f); }
            p.processBlock (io, midi);
            out.insert (out.end(), io.getReadPointer (0), io.getReadPointer (0) + bufN);
            if (auto* lt = p.engine().liveTarget()) lt->waitIdle (200);   // (the test runs faster than real time)
        }
        at = -1; mx = 0;
        for (size_t i = 0; i < out.size(); ++i) if (std::abs (out[i]) > mx) { mx = std::abs (out[i]); at = (int) i; }
        check (at - impulseAt == lat, "DRY is delayed by the same latency", "measured " + juce::String (at - impulseAt));
        check (p.engine().lateBlocks() == 0, "live analysis never late", juce::String (p.engine().lateBlocks()) + " late");
    }
    {
        // live: a sung line in, rebuilt from drums; compare with the same line as a file target
        Processor p;
        setP (p, "auto_build", 0); setP (p, "declick", 0);
        p.prepareToPlay (sr, bufN);
        p.loadKit ("drums");
        p.engine().generateLibrary(); waitIdle (p);
        auto voice = demo::vowelPhrase (sr, 6);
        juce::AudioBuffer<float> io (2, bufN);
        juce::MidiBuffer midi;
        std::vector<float> out;
        for (int b = 0; b * bufN < voice.getNumSamples() - bufN; ++b)
        {
            for (int c = 0; c < 2; ++c) io.copyFrom (c, 0, voice, c, b * bufN, bufN);
            p.processBlock (io, midi);
            out.insert (out.end(), io.getReadPointer (0), io.getReadPointer (0) + bufN);
            if (auto* lt = p.engine().liveTarget()) lt->waitIdle (200);
        }
        check (peakOf (out) > 0.01 && p.engine().lateBlocks() == 0, "live input is rebuilt from the library", "peak " + juce::String (peakOf (out), 3));
    }
    {
        // as in a DAW: buffers at real time (no waiting for the analysis), 6 s, 128-sample buffers
        Processor p;
        setP (p, "auto_build", 0);
        p.prepareToPlay (sr, 128);
        p.loadKit ("tones");
        p.engine().generateLibrary(); waitIdle (p);
        auto voice = demo::vowelPhrase (sr, 6);
        juce::AudioBuffer<float> io (2, 128);
        juce::MidiBuffer midi;
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        for (int b = 0; (b + 1) * 128 < voice.getNumSamples(); ++b)
        {
            for (int c = 0; c < 2; ++c) io.copyFrom (c, 0, voice, 0, b * 128, 128);
            p.processBlock (io, midi);
            const double due = t0 + (b + 1) * 128 * 1000.0 / sr;
            while (juce::Time::getMillisecondCounterHiRes() < due) juce::Thread::sleep (0);
        }
        check (p.engine().lateBlocks() == 0, "live analysis keeps up at real time (128-sample buffers, 6 s)", juce::String (p.engine().lateBlocks()) + " late");
    }
}

// ---------------------------------------------------------------- 10. MIDI + host sync (instrument)
static void testMidi()
{
    const double sr = 44100;
    Processor p;
    p.prepareToPlay (sr, 512);
    setP (p, "auto_build", 0); setP (p, "play_mode", 1);
    p.loadKit ("tones"); p.loadDemoTarget ("beat");
    p.engine().generateLibrary(); p.engine().generateTarget(); waitIdle (p);
    juce::AudioBuffer<float> io (2, 512);
    juce::MidiBuffer midi;
    auto block = [&] { io.clear(); p.processBlock (io, midi); midi.clear(); return io.getMagnitude (0, 512); };
    float silent = 0; for (int i = 0; i < 20; ++i) silent = juce::jmax (silent, block());
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 120), 0);
    float on = 0; for (int i = 0; i < 80; ++i) on = juce::jmax (on, block());
    midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
    for (int i = 0; i < 10; ++i) block();
    float after = 0; for (int i = 0; i < 20; ++i) after = juce::jmax (after, block());
    check (silent == 0 && on > 0.01f && after == 0, "MIDI keys: silent, a held note plays, release stops",
           "before " + juce::String (silent) + ", held " + juce::String (on, 3) + ", after " + juce::String (after));
}

// ---------------------------------------------------------------- screenshots / demos are in MosaicShots.inc
#include "TerrainTests.inc"

// ---------------------------------------------------------------- build 13: the open-source engine stays pure
// (Brandon: "with anything open source we keep it pure as is"). Nothing of ours is on the output by default:
// SAFETY LIMITER and SOFT START/STOP are extras that start off; a session saved with them on keeps them.
static void testPure()
{
    const double sr = 44100;
    {
        Processor fresh; fresh.prepareToPlay (sr, 512);
        const int ownLat = fresh.isEffect() ? fresh.engine().latencySamples() : 0;   // (the effect's live analysis)
        check (fresh.getParam ("limiter") == 0.0f && fresh.getParam ("declick") == 0.0f && (! fresh.isEffect() || fresh.getParam ("dry") == 0.0f)
                   && fresh.getLatencySamples() == ownLat && (fresh.isEffect() || ownLat == 0),
               "pure by default: SAFETY LIMITER off, SOFT START/STOP off" + juce::String (fresh.isEffect() ? ", DRY 0" : "") + ", no look-ahead delay",
               "latency " + juce::String (fresh.getLatencySamples()) + " samples");
        // an older session saved with the extras on keeps them; a new instance starts pure
        Processor a; setP (a, "limiter", 1); setP (a, "declick", 1);
        juce::MemoryBlock st; a.getStateInformation (st);
        Processor b; b.setStateInformation (st.getData(), (int) st.getSize()); waitIdle (b); b.prepareToPlay (sr, 512);
        check (b.getParam ("limiter") == 1.0f && b.getParam ("declick") == 1.0f && b.engine().latencySamples() == ownLat + TruePeakLimiter::kLatency,
               "a session saved with SAFETY LIMITER and SOFT START/STOP on keeps them on (and reports the limiter's delay)",
               "latency " + juce::String (b.engine().latencySamples()));
    }
#if ! GHOSTMOSAIC_FX
    {
        // presets leave the extras as you set them
        Processor p; p.prepareToPlay (sr, 512); setP (p, "auto_build", 0);
        setP (p, "declick", 1); setP (p, "limiter", 1);
        p.applyPreset (1); waitIdle (p); pump (20);
        const bool kept = p.getParam ("declick") == 1.0f && p.getParam ("limiter") == 1.0f;
        Processor q; q.prepareToPlay (sr, 512); setP (q, "auto_build", 0);
        q.applyPreset (2); waitIdle (q); pump (20);
        check (kept && q.getParam ("declick") == 0.0f && q.getParam ("limiter") == 0.0f, "presets leave SAFETY LIMITER and SOFT START/STOP as you set them");
    }
    {
        // switching the limiter on and off while it plays: no clicks, no allocation, and off again = untouched
        Processor p; p.prepareToPlay (sr, 512);
        tland::buildSineLibrary (p, sr);
        juce::AudioBuffer<float> io (2, 512); juce::MidiBuffer midi;
        p.engine().play();
        for (int i = 0; i < 20; ++i) { io.clear(); p.processBlock (io, midi); }
        std::vector<float> l;
        long n = 0;
        l.reserve ((size_t) sr * 5);
        for (int i = 0; i < (int) (sr * 4 / 512); ++i)
        {
            if (i % 40 == 10) setP (p, "limiter", (i / 40) % 2 == 0 ? 1.0f : 0.0f);   // (the switch itself is outside the count)
            io.clear();
            alloccount::start(); p.processBlock (io, midi); n += alloccount::stop();
            l.insert (l.end(), io.getReadPointer (0), io.getReadPointer (0) + 512);
        }
        setP (p, "limiter", 0);
        for (int i = 0; i < 10; ++i) { io.clear(); p.processBlock (io, midi); }
        const double hf = hfPeak (l, sr) / juce::jmax (1e-9, peakOf (l));
        check (hf < 0.003 && n == 0 && peakOf (l) > 1e-3, "switching SAFETY LIMITER on and off while playing: no clicks, no allocation",
               "HF peak " + juce::String (20 * std::log10 (juce::jmax (1e-12, hf)), 1) + " dB re peak, " + juce::String ((int) n) + " allocations");
    }
#endif
}
#include "HostScenarios.h"
#include "MosaicShots.inc"
extern "C" const char* ghostmosaic_host_test (const char* cmd, const char* arg);

// ---------------------------------------------------------------- does it make sound like in a host (in-process twin of HostTest)
static void testReady()
{
#if ! GHOSTMOSAIC_FX
    ::setenv ("GHOSTMOSAIC_HOST_TEST", "1", 1);
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ghostmosaic-ready");
    dir.createDirectory();
    for (int pass = 0; pass < 2; ++pass)
    {
        Processor::preloadNewInstances = true;
        auto p = std::make_unique<Processor>();
        Processor::preloadNewInstances = false;
        auto* ed = p->createEditorIfNeeded();
        ed->setSize (1280, 800);
        hostscen::Env env { *p, [] (const char* c, const juce::String& a) { return juce::String (ghostmosaic_host_test (c, a.toRawUTF8())); },
                            [] (bool ok, const juce::String& n, const juce::String& d) { check (ok, n, d); }, [] (int ms) { pump (ms); }, "in-process" };
        hostscen::run (env, pass == 0 ? 44100.0 : 48000.0, pass == 1, dir);
        p->editorBeingDeleted (ed); delete ed;
        p.reset();
        pump (50);
    }
#endif
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    Processor::preloadNewInstances = false;   // (the tests start empty; testReady switches it on)
    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add (argv[i]);
    if (args[0] == "shots") return runShots (juce::File (args[1]));
    if (args[0] == "demos") return runDemos (juce::File (args[1]));
    const auto only = args[0];
    auto want = [&] (const char* n) { return only.isEmpty() || only == n; };
#if GHOSTMOSAIC_FX
    if (want ("effect")) testEffect();
    if (want ("alloc")) testNoAlloc();
    if (want ("pure")) testPure();
    if (want ("ghost")) testGhostShape();
    if (want ("ui")) { testFreshStart(); testUi(); }
#else
    if (want ("ready")) testReady();
    if (want ("pure")) testPure();
    if (want ("renderer")) testRendererNull();
    if (want ("engine")) testEngineNull();
    if (want ("session")) testDemoSession();
    if (want ("sessions")) testSessions();
    if (want ("clicks")) testClicks();
    if (want ("truepeak")) testTruePeak();
    if (want ("alloc")) testNoAlloc();
    if (want ("midi")) testMidi();
    if (want ("large")) testLargeLibrary();
    if (want ("terrain")) testTerrain();
    if (want ("terrain")) testTerrainBrightness();
    if (want ("terrain")) testTerrainDrag();
    if (want ("terrain")) testTerrainOwnSound();
    if (want ("terrain") || want ("coverage")) testTerrainCoverage();
    if (want ("ghost")) testGhostShape();
    if (want ("ui")) { testFreshStart(); testUi(); }
#endif
    std::printf ("\n%d passed, %d failed\n", gPasses, gFails);
    return gFails;
}
