// Ghost Voice - AGPL-3.0-only (see LICENSE, NOTICE.md).
// GhostVoiceTests              the checks (exit code = failures)
// GhostVoiceTests shots DIR    + the window's pages as PNGs (BLACK / PURPLE / LIGHT, and the IDENTITY LAB on every ENGINE)
// GhostVoiceTests dsp OUTDIR SOURCE --a A1 [..] --b B1 [..]
//                              NATURAL + CLASSIC through the plug-in's own render path, in Spectrum's GhostVoiceABC layout:
//                              CI null-tests these files against Spectrum's program, sample for sample
// GhostVoiceTests e2e ...      the real engine (see endToEnd)
// The engine is replaced by tests/fake_engine.py (same protocol, no model), so the whole plug-in flow runs anywhere:
// the rights check, CREATE NEW VOICE C, the measurement on screen, SAVE / the library, the song with MOTION, state.
#include "Processor.h"
#include "Editor.h"
#include "LiquidToggle.h"
#include "BubbleFx.h"
#include "DspEngine.h"
#include "SeedVcOptions.h"
#include "UnderTheHood.h"
#include "../Source/Fusion/VoiceFusion.h"
#include "ChopPage.h"
#include "src/PluginProcessor.h"   // INTERSECT (the CHOP engine)
#include <iostream>
#include <sys/resource.h>

using namespace gv;
static int fails = 0, checks = 0;
static void check (bool ok, const juce::String& what) { ++checks; if (! ok) { ++fails; std::cout << "FAIL  " << what << std::endl; } else std::cout << "ok    " << what << std::endl; }
static bool near (double a, double b, double tol) { return std::abs (a - b) <= tol; }

static juce::File tmp;
static juce::File makeWav (const juce::String& name, double hz, double seconds, int fs = 44100)
{
    std::vector<float> x ((size_t) (seconds * fs));
    for (size_t i = 0; i < x.size(); ++i) x[i] = 0.4f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * (double) i / fs);
    auto f = tmp.getChildFile (name); writeWav (f, x, fs, 16); return f;
}
static bool waitFor (Processor& p, int ms = 20000)
{
    const auto t0 = juce::Time::getMillisecondCounter();
    while (juce::Time::getMillisecondCounter() - t0 < (juce::uint32) ms)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        if (! p.busy()) return true;
    }
    return false;
}

// a synthetic singer (no recording of anyone): glottal pulses at a sung melody with vibrato, through three formant resonators
// gliding between vowels, with short noise consonants. size scales the formants (vocal-tract size), f0 sets the range.
static juce::File makeSinger (const juce::String& name, double f0, double size, double seconds, unsigned seed, int fs = 44100)
{
    const double vow[5][3] { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 270, 2290, 3010 }, { 570, 840, 2410 }, { 300, 870, 2240 } };
    const double notes[] { 0, 2, 4, 5, 7, 5, 4, 2 };
    std::vector<float> x ((size_t) (seconds * fs));
    double ph = 0, y1[3] {}, y2[3] {};
    juce::Random rng ((juce::int64) seed);
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = (double) i / fs, seg = t / 0.45;
        const int k = (int) seg; const double fr = seg - k;
        const double hz = f0 * std::pow (2.0, (notes[k % 8] + 0.25 * std::sin (2 * juce::MathConstants<double>::pi * 5.5 * t)) / 12.0);
        ph += hz / fs; double src = 0;
        if (ph >= 1.0) { ph -= 1.0; src = 1.0; }
        const bool cons = fr < 0.08;                                   // a short "s" between the notes
        if (cons) src = 0.08 * (rng.nextDouble() * 2 - 1);
        const double* va = vow[(size_t) (k + seed) % 5]; const double* vb = vow[(size_t) (k + seed + 1) % 5];
        const double w = juce::jlimit (0.0, 1.0, (fr - 0.6) / 0.4);
        double v = src;
        for (int f = 0; f < 3; ++f)
        {
            const double F = size * (va[f] + (vb[f] - va[f]) * w), bw = 60 + 40 * f;
            const double r = std::exp (-juce::MathConstants<double>::pi * bw / fs), a1 = -2 * r * std::cos (2 * juce::MathConstants<double>::pi * F / fs), a2 = r * r;
            const double y = v - a1 * y1[f] - a2 * y2[f]; y2[f] = y1[f]; y1[f] = y; v = y * (1 - r);
        }
        const double env = std::min (1.0, std::min (t, seconds - t) * 20.0);
        x[i] = (float) (v * env);
    }
    float pk = 0; for (float v : x) pk = juce::jmax (pk, std::abs (v)); for (auto& v : x) v *= 0.6f / juce::jmax (1e-6f, pk);
    auto f = tmp.getChildFile (name); writeWav (f, x, fs, 24); return f;
}
static double rmsOf (const std::vector<float>& x) { double e = 0; for (float v : x) e += (double) v * v; return std::sqrt (e / juce::jmax<size_t> (1, x.size())); }

static void dspTests()
{
    // ---- the engines are Spectrum's own, byte for byte (when the GhostBlock repo is next to us)
    {
        const auto shared = juce::File (GHOSTVOICE_SOURCE_DIR).getSiblingFile ("ghosty-pro").getChildFile ("Shared");
        if (shared.isDirectory())
        {
            bool same = true; juce::String which;
            for (auto n : { "VoiceEngine.h", "VoiceEngine.cpp", "VoiceFusion.h", "VoiceFusion.cpp", "VoiceFusionClear.h", "VoiceFusionClear.cpp", "VoiceFusionNatural.cpp" })
            {
                juce::MemoryBlock a, b;
                juce::File (GHOSTVOICE_SOURCE_DIR).getChildFile ("Source/Fusion").getChildFile (n).loadFileAsData (a); shared.getChildFile (n).loadFileAsData (b);
                if (a != b || a.getSize() == 0) { same = false; which << n << " "; }
            }
            check (same, "NATURAL / CLASSIC are ghosty-pro/Shared's engines byte for byte " + which);
        }
        else std::cout << "skip  (ghosty-pro/Shared not next to us: byte-for-byte check skipped)" << std::endl;
    }
    // ---- BEST OF ALL's weights and its crossfade
    {
        check (seedWeight (0.0f) == 1.0f && seedWeight (1.0f) == 1.0f && seedWeight (0.08f) == 1.0f && seedWeight (0.5f) == 0.0f && seedWeight (0.25f) == 0.0f && seedWeight (0.75f) == 0.0f,
               "BEST OF ALL: SEED-VC at the anchors, NATURAL from 25 % to 75 %");
        bool smooth = true; float last = 1.0f;
        for (int i = 0; i <= 500; ++i) { const float w = seedWeight ((float) i / 1000.0f); smooth = smooth && w <= last + 1e-6f && std::abs (w - last) < 0.02f; last = w; }
        check (smooth, "BEST OF ALL: the weight falls smoothly (no jumps)");
        const int fs = 1000; std::vector<float> sd (3000, 0.5f), nt (3000, 0.25f);
        float g = 0;
        auto y = bestOfAll (sd, nt, fs, fs, [] (double t) { return (float) (t / 3.0 * 0.5); }, &g);   // 0 -> 0.5 B over 3 s
        check (near (g, 2.0, 1e-4) && y[0] == 0.5f && near (y[2999], 0.5, 1e-4) && near (y[1500], 0.5, 1e-4), "BEST OF ALL: one gain brings NATURAL to SEED-VC's level");
        std::vector<float> nt2 (3000); for (int i = 0; i < 3000; ++i) nt2[(size_t) i] = (i % 2) ? 0.5f : -0.5f;
        auto y2 = bestOfAll (sd, nt2, fs, fs, [] (double t) { return (float) (t / 3.0 * 0.5); });
        check (y2[10] == 0.5f && std::abs (y2[2990]) > 0.4f && y2[2990] != y2[2991], "BEST OF ALL: pure SEED-VC near A, pure NATURAL in the middle");
    }
    // ---- NATURAL and CLASSIC render: the same path as Spectrum's GhostVoiceABC
    const auto perf = makeSinger ("perf.wav", 196, 1.0, 3.6, 3), A1 = makeSinger ("dA1.wav", 130, 1.12, 3.6, 1), A2 = makeSinger ("dA2.wav", 140, 1.12, 2.7, 2), B1 = makeSinger ("dB1.wav", 260, 0.86, 3.6, 4);
    for (Engine e : { Engine::natural, Engine::classic })
    {
        const auto en = engineName (e);
        DspJob j; j.a = { A1.getFullPathName(), A2.getFullPathName() }; j.b = { B1.getFullPathName() }; j.performance = perf.getFullPathName();
        j.mixes = { 0.0f, 0.5f, 1.0f }; j.engine = e; j.outDir = tmp.getChildFile ("dsp_" + en);
        std::map<float, juce::File> out; std::map<float, double> ms; juce::String err;
        check (! renderDsp (j, out, ms, err) && err.contains ("rights"), en + ": nothing renders without the rights tick");
        j.rights = true; out.clear();
        const bool ok = renderDsp (j, out, ms, err);
        check (ok && out.size() == 3, en + " renders A / 50 % / B (" + err + ")");
        if (! ok) continue;
        std::vector<float> y0, y5, y1; int fs = 0; juce::String e2;
        readMono (out[0.0f], y0, fs, e2); readMono (out[0.5f], y5, fs, e2); readMono (out[1.0f], y1, fs, e2);
        bool fin = true; for (auto* y : { &y0, &y5, &y1 }) for (float v : *y) fin = fin && std::isfinite (v);
        check (fs == 44100 && near ((double) y5.size() / fs, 3.6, 0.05) && fin && rmsOf (y5) > 0.01, en + ": the performance's length, finite, not silent");
        double d = 0; for (size_t i = 0; i < std::min (y0.size(), y1.size()); ++i) d += std::abs (y0[i] - y1[i]);
        check (d / (double) y0.size() > 1e-3, en + ": voice A and voice B come out different");
        // the same render through the engine's own API, called the way Spectrum's GhostVoiceABC calls it: must null
        {
            std::vector<float> xs, xa, xb, t; int sfs = 0, r = 0; juce::String er;
            loadForFusion (perf, xs, sfs, 0, er);
            for (auto& f : j.a) { loadForFusion (juce::File (f), t, r, sfs, er); if (! xa.empty()) xa.insert (xa.end(), (size_t) (0.3 * sfs), 0.0f); xa.insert (xa.end(), t.begin(), t.end()); }
            loadForFusion (B1, xb, r, sfs, er);
            voice::AnalysisOptions o; voice::Analysis as, aa, ab;
            voice::analyse (xs.data(), (int) xs.size(), sfs, o, as); voice::analyse (xa.data(), (int) xa.size(), sfs, o, aa); voice::analyse (xb.data(), (int) xb.size(), sfs, o, ab);
            const auto idA = fusion::extractIdentity (aa), idB = fusion::extractIdentity (ab);
            fusion::Params p; p.engine = e == Engine::classic ? 0 : 1; p.wB = 0.5f; p.wC = 0.5f;
            voice::AudioRef ref; ref.ch.push_back (xs.data()); ref.n = (int) xs.size(); ref.fs = sfs;
            voice::Rendered y; fusion::render (as, ref, &idA, &idB, p, y);
            // through a 24-bit WAV, as both programs write it
            auto f = tmp.getChildFile ("abc_" + en + ".wav"); writeWav (f, y.L, y.fs, 24);
            std::vector<float> z; int zfs = 0; readMono (f, z, zfs, er);
            double maxd = z.size() == y5.size() ? 0.0 : 1.0; for (size_t i = 0; i < std::min (z.size(), y5.size()); ++i) maxd = std::max (maxd, (double) std::abs (z[i] - y5[i]));
            check (maxd == 0.0, en + " nulls against Spectrum's way of calling it (max difference " + juce::String (maxd, 9) + ")");
        }
        // the same job twice: identical (the analysis cache changes nothing)
        {
            std::map<float, juce::File> o2; DspJob j2 = j; j2.mixes = { 0.5f }; j2.outDir = tmp.getChildFile ("dsp2_" + en);
            renderDsp (j2, o2, ms, err);
            std::vector<float> z; readMono (o2[0.5f], z, fs, e2);
            check (z == y5, en + ": the same job renders the same, sample for sample");
        }
        // its own controls change the sound (TONE)
        {
            std::map<float, juce::File> o3; DspJob j3 = j; j3.mixes = { 0.5f }; j3.settings.tone = 0.8f; j3.outDir = tmp.getChildFile ("dsp3_" + en);
            renderDsp (j3, o3, ms, err);
            std::vector<float> z; readMono (o3[0.5f], z, fs, e2);
            double dd = 0; for (size_t i = 0; i < std::min (z.size(), y5.size()); ++i) dd += std::abs (z[i] - y5[i]);
            check (dd / (double) y5.size() > 1e-4, en + ": TONE moves the engine's own brightness");
        }
        std::cout << "      " << en << " 50 %: " << juce::String (ms[0.5f], 0) << " ms for 3.6 s" << std::endl;
    }
    // ---- TERRAIN on NATURAL / CLASSIC only moves their own controls, within range
    {
        ghost::terrain::Terrain flat;
        check (applyLand (DspSettings(), mapVoiceLand (flat, 0, 1, Engine::natural)) == DspSettings(), "flat land changes nothing on NATURAL");
        bool inRange = true, moved = false;
        for (int i = 0; i < ghost::terrain::shapePresetCount(); ++i)
        {
            const auto l = mapVoiceLand (ghost::terrain::shapePreset (ghost::terrain::shapePresetName (i), 3), 0, 1, Engine::natural);
            const auto d = applyLand (DspSettings(), l);
            inRange = inRange && d.resonance >= -1 && d.resonance <= 1 && d.warmth >= -1 && d.warmth <= 1 && d.texture >= 0 && d.texture <= 1;
            moved = moved || ! (d == DspSettings());
            for (int k = 0; k < l.meterCount; ++k) inRange = inRange && juce::String (l.meters[(size_t) k].name) != "FOLLOW" && juce::String (l.meters[(size_t) k].name) != "QUALITY";
        }
        check (inRange && moved, "every TERRAIN shape moves NATURAL's own controls (in range, with NATURAL's names)");
    }
}

static juce::File singer (const juce::String& n) { return tmp.getChildFile (n); }


// ================================================================================================ build 4: UNDER THE HOOD
// build 3's jobToJson, copied verbatim from ghost-voice/Source/Engine/VoiceEngine.cpp at b000452 (the reference for "untouched = unchanged")
static juce::String b3JobToJson (const Job& j)
{
    auto* o = new juce::DynamicObject();
    juce::var keep (o);
    auto arr = [] (const juce::StringArray& s) { juce::Array<juce::var> v; for (auto& x : s) v.add (x); return juce::var (v); };
    o->setProperty ("source", j.performance);
    o->setProperty ("a", arr (j.a));
    o->setProperty ("b", arr (j.b));
    juce::Array<juce::var> mx; for (float m : j.mixes) mx.add ((double) m);
    o->setProperty ("mixes", mx);
    o->setProperty ("rights", j.rights);
    o->setProperty ("out_dir", j.outDir.getFullPathName());
    o->setProperty ("steps", j.settings.steps);
    o->setProperty ("cfg", (double) j.settings.follow);
    o->setProperty ("semitones", j.settings.semitones);
    o->setProperty ("auto_f0", j.settings.autoPitch);
    o->setProperty ("prompt", j.settings.detail ? "nearest" : "none");
    o->setProperty ("seed", j.settings.seed);
    o->setProperty ("device", j.settings.cpuOnly ? "cpu" : "auto");
    o->setProperty ("novelty", (double) j.settings.novelty);
    o->setProperty ("variation", j.settings.variation);
    if (! j.measure.empty())
    {
        juce::Array<juce::var> ms;
        for (auto& [mx, f] : j.measure) { juce::Array<juce::var> pair; pair.add ((double) mx); pair.add (f); ms.add (juce::var (pair)); }
        o->setProperty ("measure", ms);
    }
    if (j.fakeDelay > 0) o->setProperty ("fake_delay", j.fakeDelay);
    return juce::JSON::toString (keep, false);
}

static Job hoodJob (const EngineSettings& s)
{
    Job j; j.a = { "/v/a1.wav", "/v/a2.wav" }; j.b = { "/v/b.wav" }; j.performance = "/p/song.wav"; j.mixes = { 0.0f, 0.25f, 0.5f, 1.0f };
    j.settings = s; j.rights = true; j.outDir = juce::File ("/o/render");
    return j;
}

static void hoodTests()
{
    // 1. nothing touched: the job file is build 3's, byte for byte
    {
        EngineSettings d;
        check (jobToJson (hoodJob (d)) == b3JobToJson (hoodJob (d)), "UNDER THE HOOD untouched: the job file is byte for byte build 3's");
        EngineSettings s = d; s.steps = 100; s.follow = 0.35f; s.semitones = -5; s.autoPitch = true; s.detail = true; s.cpuOnly = true; s.seed = 99; s.novelty = 0.4f; s.variation = 7;
        check (jobToJson (hoodJob (s)) == b3JobToJson (hoodJob (s)), "build 3's own controls changed (QUALITY, FOLLOW, PITCH, MATCH RANGE ...): still build 3's job file, byte for byte");
        auto j = hoodJob (d); j.measure = { { 0.5f, "/m/n.wav" } }; j.fakeDelay = 1.5;
        check (jobToJson (j) == b3JobToJson (j), "with a MEASURE list and the test delay: byte for byte build 3's");
        EngineSettings back = s; back.lengthAdjust = 1.4f; back.f0Condition = false; back.fp16 = 1; back.checkpoint = "/c/x.pth"; back.config = "/c/x.yml";
        hood::setGhostVoiceStart (back); back.detail = false; back.cpuOnly = false; back.seed = 1234; back.novelty = 0; back.variation = 1;
        check (jobToJson (hoodJob (back)) == b3JobToJson (hoodJob (d)), "GHOST VOICE START puts every Seed-VC option back: the job is build 3's again");
    }
    // 2. every option reaches the job under its own key, only when changed
    {
        EngineSettings s;
        s.lengthAdjust = 1.3f; s.f0Condition = false; s.fp16 = 0; s.checkpoint = "/c/m.pth"; s.config = "/c/m.yml";
        const auto v = juce::JSON::parse (jobToJson (hoodJob (s)));
        check (near (v["length_adjust"], 1.3, 1e-9) && ! (bool) v["f0_condition"] && ! (bool) v["fp16"] && v["checkpoint"].toString() == "/c/m.pth" && v["config"].toString() == "/c/m.yml",
               "length_adjust, f0_condition, fp16, checkpoint and config reach the job with their values");
        check (jobToJson (hoodJob (s)).contains ("\"length_adjust\": 1.3,") || jobToJson (hoodJob (s)).contains ("\"length_adjust\": 1.3"), "length_adjust is written clean (1.3, not 1.2999999)");
        EngineSettings t; t.fp16 = 1;
        check ((bool) juce::JSON::parse (jobToJson (hoodJob (t)))["fp16"] && ! juce::JSON::parse (jobToJson (hoodJob (EngineSettings())))["fp16"].isBool(), "fp16 True is written; automatic is not written at all");
        const auto base = juce::JSON::parse (jobToJson (hoodJob (EngineSettings())));
        check (! base.hasProperty ("length_adjust") && ! base.hasProperty ("f0_condition") && ! base.hasProperty ("fp16") && ! base.hasProperty ("checkpoint") && ! base.hasProperty ("config"), "an untouched job carries none of the new keys");
    }
    // 3. saved state: untouched sessions save exactly what build 3 saved; the new options round-trip
    {
        Processor p;
        const auto before = p.toTree().toXmlString();
        check (! before.contains ("lengthAdjust") && ! before.contains ("ckpt") && ! before.contains ("fp16") && ! before.contains ("f0="), "an untouched session saves none of the new properties (build 3's state, unchanged)");
        p.settings.lengthAdjust = 0.8f; p.settings.f0Condition = false; p.settings.fp16 = 1; p.settings.checkpoint = "/c/m.pth"; p.settings.config = "/c/m.yml";
        Processor q; q.fromTree (p.toTree());
        check (q.settings == p.settings && q.settings.lengthAdjust == 0.8f && ! q.settings.f0Condition && q.settings.fp16 == 1 && q.settings.checkpoint == "/c/m.pth", "the options save and load with the session");
    }
    // 4. the shown command line is what the helper builds (the helper's own `show`, no torch needed)
    {
        const auto dev = juce::File (GHOSTVOICE_DEV_HELPER_DIR).getChildFile ("ghostvoice_engine.py");
        bool compared = false, allSame = true;
        EngineSettings s2; s2.steps = 55; s2.follow = 0.5f; s2.semitones = 3; s2.autoPitch = true; s2.lengthAdjust = 1.25f; s2.f0Condition = false; s2.fp16 = 1; s2.checkpoint = "/c/m.pth"; s2.config = "/c/m.yml"; s2.detail = true;
        for (const auto& st : { EngineSettings(), s2 })
            for (float mix : { 0.25f, 0.5f, 1.0f })
            {
                auto j = hoodJob (st); j.mixes = { mix };
                auto jf = tmp.getChildFile ("hood_job.json"); jf.replaceWithText (jobToJson (j));
                juce::ChildProcess cp;
                if (! cp.start (juce::StringArray { "python3", dev.getFullPathName(), "show", jf.getFullPathName(), "mps" })) continue;
                const auto out = cp.readAllProcessOutput(); cp.waitForProcessToFinish (20000);
                const auto line = out.fromFirstOccurrenceOf ("GV_COMMAND ", false, false).upToFirstOccurrenceOf ("\n", false, false);
                const auto v = juce::JSON::parse (line);
                if (! v.isObject()) continue;
                compared = true;
                const auto theirs = v["cli"].toString(), ours = hood::commandLine (j, mix, true);
                if (theirs != ours) { allSame = false; std::cout << "      helper: " << theirs << std::endl << "      page:   " << ours << std::endl; }
            }
        check (compared && allSame, "the command line on the page is exactly the helper's (ghostvoice_engine.py show) for 6 jobs");
    }
    // 5. the page and the window's own controls are the same values (one source of truth)
    {
        Processor p;
        p.addTakes (slotA, { makeWav ("hA.wav", 220, 1).getFullPathName() }); p.addTakes (slotB, { makeWav ("hB.wav", 330, 1).getFullPathName() });
        p.setBalance (0.5f);
        std::unique_ptr<Editor> ed (dynamic_cast<Editor*> (p.createEditor()));
        ed->showPage (Editor::hood);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
        auto* h = ed->hoodPage();
        check (h != nullptr && ed->page() == Editor::hood, "UNDER THE HOOD is a tab and opens");
        check (h->shownRows() == (int) hood::options().size() && hood::options().size() == 14, "the page lists all 14 Seed-VC options");
        check (h->jobText() == b3JobToJson ([&] { Job j; j.a = p.takes[slotA]; j.b = p.takes[slotB]; j.performance = p.takes[slotA][0]; j.settings = p.effectiveSettings(); j.rights = p.rights;
                                                    j.mixes = { std::round (juce::jlimit (0.0f, 1.0f, p.blendB() + p.land().lean) * 1000.0f) / 1000.0f };
                                                    j.outDir = workDir().getChildFile ("renders").getChildFile ("voiceC_<date>"); return j; }()),
               "with nothing touched the page's job is build 3's job");
        check (h->touch (hood::Id::steps, 77) && p.settings.steps == 77, "the page's diffusion-steps IS QUALITY (writes the same value)");
        check (h->touch (hood::Id::cfg, 0.5) && p.settings.follow == 0.5f, "the page's inference-cfg-rate IS FOLLOW");
        check (h->touch (hood::Id::semis, -7) && p.settings.semitones == -7, "the page's semi-tone-shift IS PITCH");
        check (h->touch (hood::Id::autoF0, 1) && p.settings.autoPitch, "the page's auto-f0-adjust IS MATCH RANGE");
        check (h->touch (hood::Id::length, 1.3) && p.settings.lengthAdjust == 1.3f && h->touch (hood::Id::f0, 0) && ! p.settings.f0Condition && h->touch (hood::Id::fp16, 2) && p.settings.fp16 == 1,
               "length-adjust, f0-condition and fp16 write their values");
        const auto cmd = h->commandText(), job = h->jobText();
        check (cmd.contains ("--diffusion-steps 77") && cmd.contains ("--inference-cfg-rate 0.5") && cmd.contains ("--semi-tone-shift -7") && cmd.contains ("--auto-f0-adjust True")
               && cmd.contains ("--length-adjust 1.3") && cmd.contains ("--f0-condition False") && cmd.contains ("--fp16 True"), "the command box shows the changed options in Seed-VC's own flags");
        check (job.contains ("\"steps\": 77") && job.contains ("\"length_adjust\": 1.3") && job.contains ("\"f0_condition\": false") && job.contains ("\"fp16\": true"), "the job box shows the same values");
        p.settings.steps = 40; ed->refresh();
        check (h->commandText().contains ("--diffusion-steps 40"), "QUALITY changes show on the page (one source of truth)");
    }
    // 6. the table is Seed-VC's: names in inference.py's order, every flag unique, jobKeys real
    {
        const auto& o = hood::options();
        juce::StringArray flags; for (auto& x : o) flags.add (x.flag);
        const juce::StringArray want { "--source", "--target", "--output", "--diffusion-steps", "--length-adjust", "--inference-cfg-rate", "--f0-condition", "--auto-f0-adjust",
                                       "--semi-tone-shift", "--checkpoint", "--config", "--fp16", "--share", "--gpu" };
        check (flags == want, "the options are Seed-VC's own 12 inference.py options in its order, then app_svc.py's --share and --gpu");
    }
}

static void engineFlowTests()
{
    Processor p; p.prepareToPlay (48000, 512);
    const auto py = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory().getChildFile ("fake_engine.py");
    juce::File (GHOSTVOICE_SOURCE_DIR).getChildFile ("tests/fake_engine.py").copyFileTo (py);
    auto fake = [py] (const char* cmd) { return [py, cmd] (const juce::File& job) { return juce::StringArray { "python3", py.getFullPathName(), cmd, job.getFullPathName() }; }; };
    p.runner.renderCommand = fake ("render"); p.runner.analyzeCommand = fake ("analyze"); p.runner.measureCommand = fake ("measure");
    bool ready = false; p.engineReady = [&ready] { return ready; };
    p.addTakes (slotA, { singer ("dA1.wav").getFullPathName(), singer ("dA2.wav").getFullPathName() });
    p.addTakes (slotB, { singer ("dB1.wav").getFullPathName() });
    p.rights = true;
    check (p.engine == Engine::best, "a new Ghost Voice starts on BEST OF ALL");
    // NATURAL without the voice engine: works, says it isn't measured
    p.engine = Engine::natural; p.setBalance (0.4f);
    check (p.whyNotPreview().isEmpty(), "NATURAL needs no download");
    check (! p.noveltyWorks() && p.noveltyWhy().contains ("NEURAL"), "NOVELTY is greyed out on NATURAL, with the reason");
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    check (p.createVoiceC() && waitFor (p, 60000), "CREATE NEW VOICE C on NATURAL");
    const double took = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
    check (p.preview.existsAsFile() && p.previewEngine == Engine::natural && p.previewIsCurrent() && ! p.previewId.valid, "NATURAL's voice C: made, current, not measured without the engine");
    std::cout << "      NATURAL preview in " << juce::String (took, 2) << " s" << std::endl;
    p.dsp.tone = 0.3f; check (! p.previewIsCurrent(), "TONE makes NATURAL's preview out of date");
    p.settings.steps = 50; p.dsp.tone = 0; check (p.previewIsCurrent(), "SEED-VC's settings don't matter to a NATURAL preview");
    p.settings.steps = 30;
    // with the voice engine: the NATURAL take is measured by the same CAMPPlus
    ready = true;
    check (p.createVoiceC() && waitFor (p, 60000) && p.previewId.valid && near (p.previewId.cosA, 0.8 - 0.5 * 0.4, 1e-3), "NATURAL's take is measured (CAMPPlus) when the voice engine is there");
    juce::String why;
    check (p.saveVoiceC ("Natural Voice", why) && p.library.back().engine == Engine::natural, "SAVE VOICE C keeps the engine");
    // CLASSIC
    p.engine = Engine::classic;
    check (! p.previewIsCurrent() && p.createVoiceC() && waitFor (p, 60000) && p.previewEngine == Engine::classic && p.previewIsCurrent(), "CREATE NEW VOICE C on CLASSIC");
    // SEED-VC with the instant NATURAL preview while it sings
    p.engine = Engine::seedvc; p.instantPreview = true; p.fakeEngineDelay = 4.0;
    check (p.noveltyWorks(), "NOVELTY works on SEED-VC");
    check (p.createVoiceC(), "CREATE on SEED-VC starts");
    bool sawQuick = false;
    for (int k = 0; k < 600 && p.busy(); ++k) { juce::MessageManager::getInstance()->runDispatchLoopUntil (20); sawQuick = sawQuick || (p.previewQuick && p.preview.existsAsFile() && p.preview.getFileName().startsWith ("natural")); }
    check (sawQuick, "while SEED-VC sings, NATURAL's instant preview is there");
    check (! p.busy() && ! p.previewQuick && p.preview.getFileName().startsWith ("mix_") && p.previewId.valid && p.previewIsCurrent(), "then SEED-VC's own voice C replaces it");
    p.fakeEngineDelay = 0;
    // BEST OF ALL
    p.engine = Engine::best;
    ready = false; p.setBalance (0.5f);
    check (p.whyNotPreview().isEmpty() && p.createVoiceC() && waitFor (p, 60000) && p.preview.getFileName().startsWith ("natural"), "BEST OF ALL at 50 %: NATURAL alone (no download needed)");
    p.setBalance (0.05f);
    check (p.whyNotPreview().contains ("install"), "BEST OF ALL near A needs SEED-VC (says so)");
    ready = true; p.instantPreview = false;
    { const bool started = p.createVoiceC(); const bool done = started && waitFor (p, 60000);
      check (done && p.preview.getFileName().startsWith ("mix_"), "BEST OF ALL at 5 %: SEED-VC alone (" + juce::String ((int) started) + " " + p.preview.getFileName() + " " + p.lastError + ")"); }
    p.setBalance (0.17f);
    check (p.createVoiceC() && waitFor (p, 60000) && p.preview.getFileName().startsWith ("best_of_all") && p.previewIsCurrent() && p.previewId.valid, "BEST OF ALL at 17 %: both, crossfaded, measured");
    // songs
    p.addTakes (slotPerf, { singer ("perf.wav").getFullPathName() });
    p.board.orbit.mode = ghosty::spirit::Orbit::Circle; p.board.orbit.depth = 0.45f; p.board.orbit.rate = 0.5f; p.setBalance (0.5f);
    {
        const auto j = p.makeSongJob();
        const auto sm = p.seedMixesFor (j.mixes);
        check (j.mixes.size() >= 4 && ! sm.empty() && std::all_of (sm.begin(), sm.end(), [] (float m) { return m <= 0.25f || m >= 0.75f; }), "BEST OF ALL + MOTION: SEED-VC sings only the steps near A and B");
    }
    for (Engine e : { Engine::natural, Engine::classic, Engine::best })
    {
        p.engine = e;
        check (p.renderSong() && waitFor (p, 120000) && p.result.existsAsFile() && p.result.getFileName().contains (engineName (e)), "the song with MOTION on " + engineName (e));
        std::vector<float> y; int fs = 0; juce::String err; readMono (p.result, y, fs, err);
        check (y.size() > 0 && rmsOf (y) > 0.005, engineName (e) + "'s song is real audio (" + p.resultInfo + ")");
    }
    // state + library
    p.engine = Engine::classic; p.dsp.timbre = 1.5f; p.dsp.keepConsonants = false; p.instantPreview = false;
    {
        Processor q2; q2.fromTree (p.toTree());
        check (q2.engine == Engine::classic && q2.dsp == p.dsp && ! q2.instantPreview && q2.previewEngine == p.previewEngine, "the engine and its controls save and load");
    }
    for (int i = 0; i < (int) p.library.size(); ++i) if (p.library[(size_t) i].engine == Engine::natural) { p.useVoice (i); break; }
    check (p.engine == Engine::natural && p.previewIsCurrent(), "USE a saved NATURAL voice: back on NATURAL, its preview current");
    p.library.clear();
}

// peak memory of this process so far (MB)
static double peakMB()
{
    rusage u {}; getrusage (RUSAGE_SELF, &u);
   #if JUCE_MAC
    return (double) u.ru_maxrss / (1024.0 * 1024.0);   // bytes on macOS
   #else
    return (double) u.ru_maxrss / 1024.0;              // KB on Linux
   #endif
}

// build 3 (Brandon's Mac is Intel; Ghosty Pro 2 ran out of memory opening its AU in FL): making the plug-in and its window must
// stay light. Runs FIRST, so the peak-memory readings are this step's own.
static void constructionTests()
{
    // a session with two 5-minute takes (the window draws their waveforms)
    std::vector<float> x ((size_t) (300 * 44100));
    for (size_t i = 0; i < x.size(); ++i) x[i] = 0.3f * (float) std::sin (0.031 * (double) i) * (float) ((i / 22050) % 2);
    const auto longA = tmp.getChildFile ("long_A.wav"), longB = tmp.getChildFile ("long_B.wav");
    writeWav (longA, x, 44100, 16); writeWav (longB, x, 44100, 16);
    x.clear(); x.shrink_to_fit();
    juce::MemoryBlock state;
    { Processor p; p.addTakes (slotA, { longA.getFullPathName() }); p.addTakes (slotB, { longB.getFullPathName() }); p.addTakes (slotPerf, { longA.getFullPathName() }); p.getStateInformation (state); }
    const double m0 = peakMB();
    auto t0 = juce::Time::getMillisecondCounterHiRes();
    std::unique_ptr<Processor> p (new Processor());
    const double tProc = juce::Time::getMillisecondCounterHiRes() - t0;
    p->setStateInformation (state.getData(), (int) state.getSize());
    const double m1 = peakMB();
    t0 = juce::Time::getMillisecondCounterHiRes();
    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
    juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
    const double tEd = juce::Time::getMillisecondCounterHiRes() - t0;
    const double m2 = peakMB();
    std::cout << "      construct: plug-in " << juce::String (tProc, 1) << " ms (+" << juce::String (m1 - m0, 1) << " MB peak), window with two 5-min takes "
              << juce::String (tEd, 0) << " ms (+" << juce::String (m2 - m1, 1) << " MB peak)" << std::endl;
    check (tProc < 300 && m1 - m0 < 40, "making the plug-in is light (no audio, no engine, no threads started)");
    check (m2 - m1 < 120, "opening the window with long takes stays light (the waveforms are streamed)");
    ed = nullptr; p = nullptr;
}

static void unitTests()
{
    // ---- protocol lines
    { auto l = parseLine ("GV_PROGRESS 0.25 singing 50 % B"); check (l.kind == Line::progress && near (l.value, 0.25, 1e-6) && l.text == "singing 50 % B", "progress line"); }
    { auto l = parseLine ("GV_RESULT 0.7500 /tmp/a b/mix_075.wav"); check (l.kind == Line::result && near (l.value, 0.75, 1e-6) && l.text == "/tmp/a b/mix_075.wav", "result line (spaces in the path)"); }
    { auto l = parseLine ("GV_STEP 3/7 Seed-VC"); check (l.kind == Line::step && near (l.value, 2.0 / 7.0, 1e-4), "install step line"); }
    { auto l = parseLine ("GV_IDENTITY 0.5000 {\"cos_a\": 0.4, \"cos_b\": 0.5, \"cos_perf\": 0.2, \"cos_a_b\": 0.3}"); auto i = Identity::fromJson (l.text);
      check (l.kind == Line::identity && i.valid && near (i.distA(), 0.6, 1e-5) && near (i.distB(), 0.5, 1e-5), "identity line"); }
    check (parseLine ("GV_FAILED models").kind == Line::failed && parseLine ("GV_INSTALLED").kind == Line::installed && parseLine ("hello").kind == Line::other, "other lines");
    // ---- the UNIQUE rule
    { Identity i; i.valid = true; i.cosAB = 0.4f; i.cosA = 0.6f; i.cosB = 0.65f; i.cosPerf = 0.2f; check (i.unique(), "unique: far from both, not the performer"); }
    { Identity i; i.valid = true; i.cosAB = 0.4f; i.cosA = 0.9f; i.cosB = 0.5f; i.cosPerf = 0.2f; check (! i.unique(), "not unique: still close to A"); }
    { Identity i; i.valid = true; i.cosAB = 0.4f; i.cosA = 0.6f; i.cosB = 0.6f; i.cosPerf = 0.8f; check (! i.unique(), "not unique: it is the performer coming through"); }
    { Identity i; check (! i.unique(), "not unique when not measured"); }
    { Identity i; i.valid = true; i.cosAB = 0.8f; i.cosA = 0.8f; i.cosB = 0.77f; i.cosPerf = 0.67f; check (! i.unique(), "not unique: a plain 50 % blend of close singers (the e2e numbers)"); }
    // ---- MOTION ladder
    check (ladderFor (0.5f, 0.5f) == std::vector<float> { 0.5f }, "constant blend: one render");
    check (ladderFor (0.3f, 0.6f) == std::vector<float> ({ 0.25f, 0.5f, 0.75f }), "0.3..0.6 -> 25 / 50 / 75");
    check (ladderFor (0.0f, 1.0f).size() == 5 && ladderFor (0.6f, 0.2f).front() == 0.0f, "full range, reversed range");
    {
        const int fs = 1000; std::vector<std::vector<float>> r { std::vector<float> (2000, 0.0f), std::vector<float> (2000, 1.0f) };
        auto y = renderMotion ({ 0.0f, 1.0f }, r, fs, [] (double s) { return (float) (s / 2.0); });
        check (y.size() == 2000 && near (y[0], 0.0, 1e-3) && near (y[1000], 0.5, 0.01) && near (y[1999], 1.0, 0.01), "MOTION crossfade follows the curve");
        bool mono = true; for (size_t i = 1; i < y.size(); ++i) mono = mono && y[i] >= y[i - 1] - 1e-6f;
        check (mono, "MOTION crossfade is smooth (no steps back)");
        auto one = renderMotion ({ 0.5f }, { std::vector<float> (100, 0.3f) }, fs, [] (double) { return 0.5f; });
        check (one.size() == 100 && one[50] == 0.3f, "one render passes through unchanged (no processing of ours)");
    }
    // ---- jobs
    {
        Job j; j.a = { makeWav ("a.wav", 220, 2).getFullPathName() }; j.b = { makeWav ("b.wav", 330, 2).getFullPathName() };
        j.performance = makeWav ("p.wav", 262, 2).getFullPathName(); j.mixes = { 0.5f };
        check (whyNot (j).contains ("rights"), "no render without the rights tick");
        j.rights = true; check (whyNot (j).isEmpty(), "a complete job can run");
        j.settings.novelty = 0.4f; j.settings.variation = 7; j.settings.detail = true;
        const auto v = juce::JSON::parse (jobToJson (j));
        check ((bool) v["rights"] && near ((double) v["novelty"], 0.4, 1e-6) && (int) v["variation"] == 7 && v["prompt"].toString() == "nearest" && v["a"].size() == 1, "job file carries every setting");
        j.performance = tmp.getChildFile ("missing.wav").getFullPathName(); check (whyNot (j).contains ("performance"), "missing file is caught");
    }
    // ---- TERRAIN only moves Seed-VC's own controls, within their ranges
    {
        ghost::terrain::Terrain flat; const auto l0 = mapVoiceLand (flat);
        check (! l0.any && applyLand (EngineSettings(), l0) == EngineSettings(), "flat land changes nothing");
        bool inRange = true, moved = false;
        for (int i = 0; i < ghost::terrain::shapePresetCount(); ++i)
        {
            const auto t = ghost::terrain::shapePreset (ghost::terrain::shapePresetName (i), 3);
            const auto s = applyLand (EngineSettings(), mapVoiceLand (t));
            inRange = inRange && s.steps >= 5 && s.steps <= 100 && s.follow >= 0 && s.follow <= 1 && std::abs (s.semitones) <= 24;
            moved = moved || ! (s == EngineSettings());
        }
        check (inRange && moved, "every TERRAIN shape keeps Seed-VC's controls in range (and moves them)");
    }
    // ---- bubbles: bloom, live, pop; none when off or when the system reduces motion
    {
        BubbleFx fx; fx.setSize (400, 300); fx.reduceMotion = false;
        fx.spawn ({ 100, 100 }, 40); check (fx.alive() == 1, "a press blooms a bubble");
        for (int i = 0; i < 8; ++i) fx.spawn ({ 100, 100 }, 40);
        check (fx.alive() <= 6, "at most 6 bubbles at once");
        for (int i = 0; i < 60; ++i) fx.advance (1.0f / 60.0f);
        check (fx.alive() == 0, "bubbles pop by themselves (within a second)");
        fx.enabled = false; fx.spawn ({ 1, 1 }, 30); check (fx.alive() == 0, "BUBBLES off: nothing");
        fx.enabled = true; fx.reduceMotion = true; fx.spawn ({ 1, 1 }, 30); check (fx.alive() == 0, "Reduce motion: nothing");
    }
    // ---- the liquid toggle
    {
        LiquidToggle t ("X"); t.setSize (160, 30); bool got = false, val = false; t.onChange = [&] (bool v) { got = true; val = v; };
        const auto c = juce::Point<float> (10, 15);
        juce::MouseEvent down (juce::Desktop::getInstance().getMainMouseSource(), c, {}, juce::MouseInputSource::defaultPressure, 0, 0, 0, 0, &t, &t, juce::Time::getCurrentTime(), c, juce::Time::getCurrentTime(), 1, false);
        t.mouseDown (down); t.mouseUp (down);
        check (got && val && t.getToggleState(), "liquid toggle: a click turns it on");
        t.setToggleState (false, false); t.snap();
        check (! t.getToggleState() && t.thumbX() < 5, "liquid toggle: set off without notifying");
    }
}

static void flowTests (const juce::File& shots)
{
    juce::ignoreUnused (shots);
    Processor p;
    p.prepareToPlay (48000, 512);
    p.engine = Engine::seedvc; p.instantPreview = false;   // b1's flow: SEED-VC alone
    const auto py = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory().getChildFile ("fake_engine.py");
    juce::File (GHOSTVOICE_SOURCE_DIR).getChildFile ("tests/fake_engine.py").copyFileTo (py);
    auto fake = [py] (const char* cmd) { return [py, cmd] (const juce::File& job) { return juce::StringArray { "python3", py.getFullPathName(), cmd, job.getFullPathName() }; }; };
    p.runner.renderCommand = fake ("render"); p.runner.analyzeCommand = fake ("analyze"); p.engineReady = [] { return true; };
    p.addTakes (slotA, { makeWav ("A1.wav", 220, 3).getFullPathName(), makeWav ("A2.wav", 230, 3).getFullPathName() });
    p.addTakes (slotB, { makeWav ("B1.wav", 330, 3).getFullPathName() });
    check (p.takes[slotA].size() == 2 && p.takes[slotB].size() == 1, "takes added (A keeps several)");
    check (! p.createVoiceC(), "CREATE refused without the rights tick");
    p.rights = true;
    p.setBalance (0.4f); check (near (p.blendB(), 0.4, 0.01), "BALANCE puts the puck where it says");
    p.settings.novelty = 0.5f;
    check (p.createVoiceC() && waitFor (p), "CREATE NEW VOICE C runs (fake engine)");
    check (p.preview.existsAsFile() && p.previewId.valid && near (p.previewBalance, 0.4, 0.002), "voice C's preview + its measurement arrive");
    check (near (p.previewId.cosA, 0.9 - 0.24 - 0.15, 1e-3), "the measurement is shown exactly as measured");
    const auto seen = juce::JSON::parse (p.preview.getParentDirectory().getChildFile ("seen.json"));
    check (seen["performance"].isVoid() && juce::File (seen["source"].toString()).getFileName() == "A1.wav" && near ((double) seen["novelty"], 0.5, 1e-6), "no performance: the preview sings voice A's first take");
    check (p.previewIsCurrent(), "the preview matches the settings");
    p.settings.novelty = 0.6f; check (! p.previewIsCurrent(), "changing NOVELTY makes the preview out of date");
    p.settings.novelty = 0.5f;
    check (p.analyzeVoices() && waitFor (p) && near ((double) p.analysisAB["cos_a_b"], 0.42, 1e-6), "ANALYZE VOICES");
    // library
    juce::String why;
    const size_t before = p.library.size();
    check (p.saveVoiceC ("Test Voice", why) && p.library.size() == before + 1, "SAVE VOICE C -> the library");
    const auto& sv = p.library.back();
    check (sv.a.size() == 2 && juce::File (sv.a[0]).isAChildOf (sv.dir) && sv.identity.valid && near (sv.balance, 0.4, 0.002), "a saved voice keeps its takes, balance and measurement");
    p.setBalance (0.9f); p.settings.novelty = 0;
    p.useVoice ((int) p.library.size() - 1);
    check (near (p.blendB(), 0.4, 0.01) && near (p.settings.novelty, 0.5, 1e-6) && p.previewIsCurrent(), "USE a saved voice restores it");
    // the song with MOTION
    check (p.whyNotSong().contains ("performance") || ! engineInstalled(), "the song needs a performance");
    p.addTakes (slotPerf, { makeWav ("song.wav", 262, 2).getFullPathName() });
    p.board.orbit.mode = ghosty::spirit::Orbit::Circle; p.board.orbit.depth = 0.3f; p.board.orbit.rate = 0.5f;
    const auto job = p.makeSongJob();
    check (job.mixes.size() >= 3, "MOTION asks for a ladder of blends (" + juce::String ((int) job.mixes.size()) + ")");
    check (p.renderSong() && waitFor (p), "the song renders (fake engine)");
    std::vector<float> y; int fs = 0; juce::String err;
    check (p.result.existsAsFile() && readMono (p.result, y, fs, err) && fs == 44100 && y.size() == 88200, "the take is written (44.1 kHz, full length)");
    {   // the level of the take follows the blend curve (the fake renders' level = 0.1 + 0.8 * mix)
        bool follows = true;
        for (double t : { 0.25, 0.75, 1.25, 1.75 })
        {
            float pk = 0; for (int i = (int) (t * fs) - 120; i < (int) (t * fs) + 120; ++i) pk = juce::jmax (pk, std::abs (y[(size_t) i]));
            follows = follows && near (pk, (0.1 + 0.8 * p.blendAt (t)) * 0.99997, 0.03);
        }
        check (follows, "MOTION: the take moves between the blends with the song's tempo");
    }
    // state round trip
    {
        Processor q2; q2.fromTree (p.toTree());
        check (q2.takes[slotA] == p.takes[slotA] && q2.rights && q2.settings == p.settings && near (q2.blendB(), p.blendB(), 1e-4) && q2.board.orbit.mode == p.board.orbit.mode
               && q2.previewId.valid && q2.preview == p.preview && q2.result == p.result, "the session saves and loads");
    }
    // recorder
    {
        p.startRecording (slotPerf);
        juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer mb;
        for (int k = 0; k < 120; ++k) { for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) b.setSample (c, i, 0.25f); p.processBlock (b, mb); }
        p.stopRecording();
        std::vector<float> r; int rfs = 0;
        check (readMono (juce::File (p.takes[slotPerf][0]), r, rfs, err) && r.size() == 120 * 512 && near (r[1000], 0.25, 1e-3), "REC records the input into the slot");
    }
    // player: the input is replaced by the take while it plays, then passes again
    {
        p.play (p.result);
        juce::AudioBuffer<float> b (2, 256); juce::MidiBuffer mb; b.clear(); for (int i = 0; i < 256; ++i) b.setSample (0, i, 0.123f);
        p.processBlock (b, mb);
        check (p.isPlaying() && b.getSample (0, 10) != 0.123f, "PLAY plays the take");
        p.stopPlaying(); for (int i = 0; i < 256; ++i) b.setSample (0, i, 0.123f); p.processBlock (b, mb);
        check (b.getMagnitude (0, 0, 256) < 1.0e-6f, "stopped: silence (an instrument: the input is never heard)");
    }
    p.library.clear();
    juce::ignoreUnused (shots);
}

// ------------------------------------------------------------------------------------------------ window size + fresh start
// Brandon 2026-11-06 15:39 / 15:44: the window OPENS at the size that fits his 1440x900 MacBook Pro in FL Studio (about 1200 x 760,
// never wider than 1240 or taller than 780), resizable, with nothing overlapping at any size; and a new instance is ALWAYS a fresh start.
static juce::String overlapsIn (juce::Component& ed)
{
    juce::String bad;
    std::vector<juce::Component*> cs;
    for (auto* c : ed.getChildren())
        if (c->isVisible() && ! c->getBounds().isEmpty() && dynamic_cast<BubbleFx*> (c) == nullptr) cs.push_back (c);
    for (auto* c : cs)
        if (! ed.getLocalBounds().contains (c->getBounds())) bad << " [" << c->getName() << c->getBounds().toString() << " outside]";
    // an intended overlay (marked "overlay" and passing every click through, itself and its children) may sit over another part;
    // it still has to stay inside the window
    auto overlay = [] (juce::Component* c) { bool self = true, kids = true; c->getInterceptsMouseClicks (self, kids); return ! self && ! kids && (bool) c->getProperties()["overlay"]; };
    for (size_t i = 0; i < cs.size(); ++i)
        for (size_t j = i + 1; j < cs.size(); ++j)
            if (! overlay (cs[i]) && ! overlay (cs[j]) && cs[i]->getBounds().intersects (cs[j]->getBounds()))
            {
                auto nm = [] (juce::Component* c) { auto* b = dynamic_cast<juce::Button*> (c); return (b != nullptr ? b->getButtonText() : juce::String (typeid (*c).name())) + " " + c->getBounds().toString(); };
                bad << " [" << nm (cs[i]) << " x " << nm (cs[j]) << "]";
            }
    return bad;
}

// a session as text, without the Spirit Board's per-ghost random ids (each new ghost gets a fresh UUID; that is not carried state)
static juce::String stateText (const Processor& p)
{
    auto x = p.toTree().toXmlString();
    juce::String out; int i = 0;
    while (true)
    {
        const int k = x.indexOf (i, " id=\""); if (k < 0) { out << x.substring (i); break; }
        const int e = x.indexOf (k + 5, "\""); out << x.substring (i, k) << " id=\"*\""; i = e + 1;
    }
    return out;
}

static void windowAndFreshStartTests()
{
    {   // SIZE
        Processor p;
        std::unique_ptr<Editor> ed (dynamic_cast<Editor*> (p.createEditor()));
        check (ed->getWidth() == 1200 && ed->getHeight() == 760, "the window opens at 1200 x 760 (fits a 1440 x 900 MacBook Pro inside FL Studio): " + juce::String (ed->getWidth()) + " x " + juce::String (ed->getHeight()));
        check (ed->getWidth() <= 1240 && ed->getHeight() <= 780, "never wider than 1240 or taller than 780 when it opens");
        auto* c = ed->getConstrainer();
        check (ed->isResizable() && c != nullptr && c->getMinimumWidth() == 1200 && c->getMinimumHeight() == 760 && c->getMaximumWidth() >= 1900, "resizable (bigger), never smaller than the fitting size");
        // nothing overlaps: every page, every engine, at the opening size, a middle size and the biggest
        p.engineReady = [] { return true; };
        juce::String all; int layouts = 0;
        for (auto sz : { juce::Point<int> (1200, 760), juce::Point<int> (1440, 860), juce::Point<int> (2000, 1400) })
        {
            ed->setSize (sz.x, sz.y);
            for (Engine en : { Engine::seedvc, Engine::natural, Engine::classic, Engine::best })
            {
                p.engine = en;
                for (int pg = 0; pg < Editor::kPages; ++pg)
                {
                    ed->showPage (pg); ++layouts;
                    const auto o = overlapsIn (*ed);
                    if (o.isNotEmpty()) all << "\n      " << sz.x << "x" << sz.y << " " << engineName (en) << " page " << pg << ":" << o;
                }
            }
        }
        check (all.isEmpty(), "nothing overlaps or sticks out on any page or engine at 1200x760, 1440x860 and 2000x1400 (" + juce::String (layouts) + " layouts)" + all);
        // build 10: no text cut off: every tab's two lines fit whole, every button's word fits its button, at every size and page
        juce::String cut;
        for (auto sz : { juce::Point<int> (1200, 760), juce::Point<int> (1440, 860), juce::Point<int> (2000, 1400) })
        {
            ed->setSize (sz.x, sz.y);
            for (int pg = 0; pg < Editor::kPages; ++pg)
            {
                ed->showPage (pg);
                if (pg == 0) cut << ed->tabsThatDontFit();
                std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
                {
                    if (! c.isVisible()) return;
                    if (auto* b = dynamic_cast<juce::TextButton*> (&c))
                    {
                        auto& lf = b->getLookAndFeel();
                        const float tw = ghosty::textWidth (lf.getTextButtonFont (*b, b->getHeight()), b->getButtonText());
                        if (tw * 0.8f > (float) b->getWidth() - 12.0f) cut << " [" << b->getButtonText() << " " << b->getWidth() << " px at " << sz.x << "x" << sz.y << "]";
                    }
                    for (auto* k : c.getChildren()) walk (*k);
                };
                walk (*ed);
            }
        }
        check (cut.isEmpty(), "no text is cut off: every tab and every button's words fit whole" + cut);
        // ONE about / credits item, and it names the open-source parts (the window itself does not)
        // (fix-blur round, Brandon 2026-11-22: "remove the credits, hide them somewhere else") it is no longer in the left column on
        // any page; it is one small entry at the foot of Settings
        ed->setSize (1200, 760);
        auto countAbouts = [&]
        {
            int n = 0;
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
            {
                for (auto* k : c.getChildren())
                {
                    if (! k->isVisible()) continue;
                    if (auto* b = dynamic_cast<juce::Button*> (k)) if (b->getButtonText().containsIgnoreCase ("credits")) ++n;
                    walk (*k);
                }
            };
            walk (*ed);
            return n;
        };
        juce::String inColumn;
        for (int pg = 0; pg < Editor::kPages; ++pg)
            if (pg != Editor::prefs) { ed->showPage (pg); if (countAbouts() != 0) inColumn << " page " << pg; }
        check (inColumn.isEmpty(), "no ABOUT / CREDITS item in the main window's left column (only Settings has it)" + inColumn);
        ed->showPage (Editor::prefs);
        int abouts = 0; juce::Button* about = nullptr;
        for (auto* k : ed->getChildren()) if (auto* b = dynamic_cast<juce::Button*> (k)) if (b->getButtonText().containsIgnoreCase ("credits") && b->isVisible()) { ++abouts; about = b; }
        check (abouts == 1 && countAbouts() == 1, "exactly one ABOUT / CREDITS item in the window (in Settings)");
        check (about != nullptr && about->getX() >= 206 && about->getBottom() <= ed->getHeight() && about->getBottom() >= ed->getHeight() - 80,
               "the ABOUT / CREDITS item sits at the foot of the Settings page, not in the left column");
        if (about != nullptr) { about->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil (20); }
        check (ed->creditsShown(), "the Settings entry opens the ABOUT / CREDITS page");
        ed->showCredits (false);
        const auto cr = Editor::creditsText();
        check (cr.contains ("Seed-VC") && cr.contains ("GPL-3.0") && cr.contains ("https://github.com/Plachtaa/seed-vc") && cr.contains ("WORLD") && cr.contains ("JUCE") && cr.contains ("AGPL")
               && cr.contains ("INTERSECT") && cr.contains ("https://github.com/tucktuckg00se/INTERSECT") && cr.contains ("Flayr Chop") && cr.contains ("https://github.com/FlayrLabs/flayr-chop"),
               "ABOUT / CREDITS names each project, its licence and its source link");
        ed->showCredits (true); check (ed->creditsShown(), "ABOUT / CREDITS opens"); ed->showCredits (false); check (! ed->creditsShown(), "and closes");
    }
    {   // NAMES: no open-source project names on the window, any page, any engine (only ABOUT / CREDITS has them, and UNDER THE HOOD,
        // which is explicitly the ORIGINAL CONTROLS page: its sub-tab titles and the engines' own option names are the projects')
        const char* names[] { "Seed-VC", "SEED-VC", "seed-vc", "CAMPPlus", "Whisper", "BigVGAN", "RMVPE", "Plachtaa", "VocalSet", "Bencho", "Crypta",
                              "INTERSECT", "Intersect", "Flayr", "Signalsmith", "Bungee", "VelociLoops", "WhisperX", "whisperx", "OpenVPI" };
        int scanned = 0, seen = 0;
        auto scan = [&] (juce::Component& root) -> juce::String
        {
            juce::String hits;
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
            {
                juce::StringArray texts;
                if (&c != &root && ! c.isVisible()) return;   // (the window itself is not on a screen here)
                if (dynamic_cast<HoodPage*> (&c) != nullptr) return;   // (the one exemption: UNDER THE HOOD's own page; everything around it is checked)
                if (auto* b = dynamic_cast<juce::Button*> (&c)) texts.add (b->getButtonText());
                if (auto* t = dynamic_cast<juce::SettableTooltipClient*> (&c)) texts.add (t->getTooltip());
                if (auto* cb = dynamic_cast<juce::ComboBox*> (&c)) for (int i = 0; i < cb->getNumItems(); ++i) texts.add (cb->getItemText (i));
                if (auto* te = dynamic_cast<juce::TextEditor*> (&c)) texts.add (te->getText());
                seen += texts.size();
                for (auto& t : texts) for (auto* n : names) if (t.contains (n)) hits << " [" << n << " in \"" << t.substring (0, 60) << "\"]";
                for (auto* k : c.getChildren()) walk (*k);
            };
            walk (root);
            ++scanned;
            return hits;
        };
        Processor p; p.engineReady = [] { return true; };
        std::unique_ptr<Editor> ed (dynamic_cast<Editor*> (p.createEditor()));
        juce::String hits;
        for (Engine en : { Engine::seedvc, Engine::natural, Engine::best })
        {
            p.engine = en;
            for (int pg = 0; pg < Editor::kPages; ++pg) { ed->showPage (pg); hits << scan (*ed); hits << [&] { juce::String h; for (auto* n : names) if (ed->statusText().contains (n)) h << " [status: " << n << "]"; return h; }(); }
            for (auto* n : names) if (engineName (en).contains (n) || engineBlurb (en).contains (n) || p.noveltyWhy().contains (n) || p.whyNotPreview().contains (n)) hits << " [engine words: " << n << "]";
        }
        // the painted text: render each page into an image is not readable, so the strings painted come from these (checked above) and
        // from the helper's messages, which pass through uiWords:
        check (uiWords ("measuring with CAMPPlus: Seed-VC's Whisper / BigVGAN / RMVPE").containsIgnoreCase ("seed") == false
               && ! uiWords ("SEED-VC: loading Seed-VC").contains ("SEED"), "helper / installer messages are shown in plain words");
        {   // the exemption is UNDER THE HOOD's page only: it is not empty, and nothing else in the window is a HoodPage
            ed->showPage (Editor::hood);
            int hoods = 0; std::function<void (juce::Component&)> count = [&] (juce::Component& c) { if (dynamic_cast<HoodPage*> (&c) != nullptr) ++hoods; for (auto* k : c.getChildren()) count (*k); };
            count (*ed);
            check (hoods == 1 && ed->hoodPage() != nullptr && ed->hoodPage()->tabTitle (HoodPage::intersectSound).contains ("INTERSECT"),
                   "the names exemption covers exactly one component, UNDER THE HOOD's page (its sub-tabs carry the projects' names)");
        }
        check (hits.isEmpty() && seen > 300, "no open-source project names on any page outside UNDER THE HOOD, button, tooltip, menu or status line (only there and in ABOUT / CREDITS; "
               + juce::String (seen) + " texts on " + juce::String (scanned) + " page views checked)" + hits);
    }
    {   // FRESH START: two instances in one process; changing one leaves the other at factory defaults; destroy + recreate = defaults
        juce::String factory;
        { Processor ref; factory = stateText (ref); }
        auto a = std::make_unique<Processor>(), b = std::make_unique<Processor>();
        a->engine = Engine::classic; a->setBalance (0.9f); a->settings.novelty = 0.7f; a->settings.steps = 77; a->dsp.tone = 0.4f;
        a->theme = ghosty::themes::Theme::Light; a->voiceName = "Changed"; a->rights = true; a->terrainAmount = 0.2f; a->instantPreview = false;
        a->addTakes (slotA, { makeWav ("fresh_A.wav", 220, 1).getFullPathName() }); a->board.orbit.mode = ghosty::spirit::Orbit::Circle;
        std::unique_ptr<Editor> ea (dynamic_cast<Editor*> (a->createEditor())); ea->showPage (Editor::tools); ea->setSize (1700, 1000);
        check (stateText (*a) != factory, "(the first instance really changed)");
        auto firstDiff = [&] (const juce::String& x) { int i = 0; while (i < x.length() && i < factory.length() && x[i] == factory[i]) ++i; return x.substring (juce::jmax (0, i - 60), i + 60) + "  vs  " + factory.substring (juce::jmax (0, i - 60), i + 60); };
        check (stateText (*b) == factory, "a second instance in the same process is still at factory defaults" + (stateText (*b) == factory ? juce::String() : "  " + firstDiff (stateText (*b))));
        std::unique_ptr<Editor> eb (dynamic_cast<Editor*> (b->createEditor()));
        check (eb->page() == Editor::chop && eb->getWidth() == 1200 && eb->getHeight() == 760, "its window opens on the first page (CHOP) at the fitting size (nothing carried over)");
        auto c = std::make_unique<Processor>();
        check (stateText (*c) == factory && c->takes[slotA].isEmpty(), "a third instance made while the first is changed: factory defaults");
        juce::MemoryBlock st; a->getStateInformation (st);
        eb = nullptr; ea = nullptr; a = nullptr; b = nullptr; c = nullptr;
        auto d = std::make_unique<Processor>();
        check (stateText (*d) == factory, "destroy everything and make a new one: factory defaults");
        d->setStateInformation (st.getData(), (int) st.getSize());
        check (d->engine == Engine::classic && d->voiceName == "Changed" && d->takes[slotA].size() == 1, "it restores only what the host hands back (the project's own state)");
        auto e = std::make_unique<Processor>();
        check (stateText (*e) == factory, "and the next new one is still factory defaults");
    }
}

static void shotsAndEditor (const juce::File& dir)
{
    Processor p; p.prepareToPlay (48000, 512);
    auto fake = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getParentDirectory().getChildFile ("fake_engine.py");
    p.runner.renderCommand = [fake] (const juce::File& job) { return juce::StringArray { "python3", fake.getFullPathName(), "render", job.getFullPathName() }; };
    p.runner.measureCommand = [fake] (const juce::File& job) { return juce::StringArray { "python3", fake.getFullPathName(), "measure", job.getFullPathName() }; };
    p.engineReady = [] { return true; };
    p.engine = Engine::seedvc; p.instantPreview = false;
    p.addTakes (slotA, { makeWav ("A1.wav", 220, 3).getFullPathName() }); p.addTakes (slotB, { makeWav ("B1.wav", 330, 3).getFullPathName() });
    p.rights = true; p.setBalance (0.5f); p.settings.novelty = 0.65f;
    p.createVoiceC(); waitFor (p);
    p.addTakes (slotPerf, { makeWav ("song.wav", 262, 2).getFullPathName() });
    p.renderSong(); waitFor (p);
    std::unique_ptr<Editor> ed (dynamic_cast<Editor*> (p.createEditor()));
    check (ed != nullptr && ed->getWidth() == 1200 && ed->getHeight() == 760, "the window opens (1200 x 760)");
    bool allPages = true;
    for (int t = 0; t < 3; ++t)
    {
        p.theme = ghosty::themes::fromInt (t == 0 ? 1 : t == 1 ? 0 : 2); ed->applyTheme();
        for (int pg = 0; pg < Editor::kPages; ++pg)
        {
            ed->showPage (pg);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            allPages = allPages && img.isValid();
            if (dir != juce::File())
            {
                const juce::String names[] { "identity-lab", "voice-library", "sing-engine", "tools-terrain", "settings", "under-the-hood", "chop-empty" };
                const juce::String th = t == 0 ? "purple" : t == 1 ? "black" : "light";
                juce::FileOutputStream os (dir.getChildFile ("ghostvoice-" + names[pg] + "-" + th + ".png"));
                if (os.openedOk()) { os.setPosition (0); os.truncate(); juce::PNGImageFormat().writeImageToStream (img, os); }
            }
        }
    }
    check (allPages, "every page paints in BLACK / PURPLE / LIGHT");
    if (dir != juce::File())
    {   // the one ABOUT / CREDITS item, open
        p.theme = ghosty::themes::Theme::Dark; ed->applyTheme();
        check (ed->pal().window.getPerceivedBrightness() < 0.2f && ed->pal().ink.getPerceivedBrightness() > 0.85f, "DARK: a night ground with light words (build 12)");
        p.theme = ghosty::themes::Theme::Light; ed->applyTheme();
        check (ed->pal().window.getPerceivedBrightness() > 0.8f && ed->pal().ink.getPerceivedBrightness() < 0.2f, "LIGHT: the painting's pale ground with dark words");
        {
            Processor fresh; check (fresh.theme == ghosty::themes::Theme::Dark, "a new Ghost Voice opens DARK");
            fresh.theme = ghosty::themes::Theme::Dark; juce::MemoryBlock mb; fresh.getStateInformation (mb);
            Processor back; back.setStateInformation (mb.getData(), (int) mb.getSize());
            check (back.theme == ghosty::themes::Theme::Dark, "DARK is saved with the project");
            fresh.theme = ghosty::themes::Theme::Light; juce::MemoryBlock ml; fresh.getStateInformation (ml);
            Processor backL; backL.setStateInformation (ml.getData(), (int) ml.getSize());
            check (backL.theme == ghosty::themes::Theme::Light, "LIGHT is saved with the project too");
        }
        p.theme = ghosty::themes::Theme::Dark; ed->applyTheme(); ed->showPage (Editor::prefs); ed->showCredits (true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::FileOutputStream os (dir.getChildFile ("ghostvoice-about-credits-black.png")); if (os.openedOk()) { os.setPosition (0); os.truncate(); juce::PNGImageFormat().writeImageToStream (img, os); }
        ed->showCredits (false);
    }
    if (dir != juce::File())
    {   // a press with its bubble, mid-bloom
        p.theme = ghosty::themes::Theme::Dark; ed->applyTheme(); ed->showPage (Editor::lab);
        auto* fx = dynamic_cast<BubbleFx*> (ed->getChildComponent (ed->getNumChildComponents() - 1));
        if (fx != nullptr) { fx->reduceMotion = false; fx->spawn ({ 680.0f, 696.0f }, 62.0f); fx->spawn ({ 1235.0f, 528.0f }, 30.0f); }
        if (fx != nullptr) for (int i = 0; i < 11; ++i) fx->advance (1.0f / 60.0f);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        juce::FileOutputStream os (dir.getChildFile ("ghostvoice-press-bubbles-black.png")); if (os.openedOk()) { os.setPosition (0); os.truncate(); juce::PNGImageFormat().writeImageToStream (img, os); }
        check (fx != nullptr && fx->alive() > 0, "the press bubbles sit on top of the window");
        // the IDENTITY LAB on every engine (NATURAL / CLASSIC / BEST OF ALL rendered for real on the synthetic singers)
        Processor q; q.prepareToPlay (48000, 512);
        q.runner.renderCommand = p.runner.renderCommand; q.runner.measureCommand = p.runner.measureCommand; q.engineReady = [] { return true; };
        q.addTakes (slotA, { singer ("dA1.wav").getFullPathName(), singer ("dA2.wav").getFullPathName() }); q.addTakes (slotB, { singer ("dB1.wav").getFullPathName() });
        q.addTakes (slotPerf, { singer ("perf.wav").getFullPathName() }); q.rights = true; q.theme = ghosty::themes::Theme::Dark;
        std::unique_ptr<Editor> e2 (dynamic_cast<Editor*> (q.createEditor()));
        bool shotsOk = true;
        for (Engine en : { Engine::natural, Engine::classic, Engine::best, Engine::seedvc })
        {
            q.engine = en; q.setBalance (en == Engine::best ? 0.2f : 0.5f); q.settings.novelty = en == Engine::seedvc ? 0.5f : 0.0f;
            shotsOk = shotsOk && q.createVoiceC() && waitFor (q, 60000) && q.preview.existsAsFile();
            e2->showPage (Editor::lab); juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
            auto img = e2->createComponentSnapshot (e2->getLocalBounds(), true, 1.0f);
            juce::FileOutputStream os (dir.getChildFile ("ghostvoice-identity-lab-engine-" + engineName (en).toLowerCase().replace (" ", "-") + "-black.png"));
            if (os.openedOk()) { os.setPosition (0); os.truncate(); juce::PNGImageFormat().writeImageToStream (img, os); }
        }
        q.engine = Engine::best; q.board.orbit.mode = ghosty::spirit::Orbit::Figure8; q.board.orbit.depth = 0.45f; q.setBalance (0.5f);
        shotsOk = shotsOk && q.renderSong() && waitFor (q, 120000);
        e2->showPage (Editor::sing); juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
        {
            auto img = e2->createComponentSnapshot (e2->getLocalBounds(), true, 1.0f);
            juce::FileOutputStream os (dir.getChildFile ("ghostvoice-sing-engine-best-of-all-black.png"));
            if (os.openedOk()) { os.setPosition (0); os.truncate(); juce::PNGImageFormat().writeImageToStream (img, os); }
        }
        q.engine = Engine::natural; e2->showPage (Editor::tools); juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
        {
            auto img = e2->createComponentSnapshot (e2->getLocalBounds(), true, 1.0f);
            juce::FileOutputStream os (dir.getChildFile ("ghostvoice-tools-terrain-natural-black.png"));
            if (os.openedOk()) { os.setPosition (0); os.truncate(); juce::PNGImageFormat().writeImageToStream (img, os); }
        }
        check (shotsOk, "screenshots of every engine (real NATURAL / CLASSIC renders)");
        e2 = nullptr; q.library.clear();
    }
    check (ed->statusText().isNotEmpty(), "the status pill says something real");
    ed->setSize (1200, 760); ed->showPage (Editor::lab); ed->setSize (1900, 1200); ed->showPage (Editor::sing);
    check (true, "the window resizes without trouble");
    p.library.clear();
}

// GhostVoiceTests e2e OUTDIR A1 A2 B1 B2 SONG: the plug-in's own code with the REAL installed engine (helper + Seed-VC):
//   SEED-VC: voice C at 50 % B (novelty 0, 0.6, 1), the anchors and 25 / 75 %;
//   NATURAL and CLASSIC: 0 / 25 / 50 / 75 / 100 % (each measured with the same CAMPPlus); BEST OF ALL at 15 % (crossfade zone);
//   SAVE; then the song with MOTION on SEED-VC and on BEST OF ALL. Writes the WAVs + e2e.json (every distance, every time).
static int endToEnd (const juce::File& out, const juce::StringArray& f, bool quick = false)
{   // quick (Intel runner, build 3): NATURAL, CLASSIC and SEED-VC at 50 % only, + NATURAL's song
    out.createDirectory();
    Processor p; p.prepareToPlay (48000, 512);
    if (! engineInstalled()) { std::cout << "FAIL  the engine is not installed at " << engineDir().getFullPathName() << std::endl; return 1; }
    p.addTakes (slotA, { f[0], f[1] }); p.addTakes (slotB, { f[2], f[3] }); p.addTakes (slotPerf, { f[4] });
    p.rights = true; p.instantPreview = false; p.setBalance (0.5f);
    auto* o = new juce::DynamicObject(); juce::var keep (o);
    auto runOne = [&] (const juce::String& name, Engine e, float balance, float novelty) -> bool
    {
        p.engine = e; p.setBalance (balance); p.settings.novelty = novelty;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        if (! p.createVoiceC() || ! waitFor (p, 3600 * 1000) || ! p.previewIsCurrent()) { std::cout << "FAIL  " << name << ": " << p.lastError << " " << p.runner.message() << "\n" << p.runner.log() << std::endl; return false; }
        const double total = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
        p.preview.copyFileTo (out.getChildFile (name + ".wav"));
        auto* r = new juce::DynamicObject();
        r->setProperty ("engine", engineName (e)); r->setProperty ("balance", balance); r->setProperty ("novelty", novelty);
        r->setProperty ("render_seconds", p.previewSeconds); r->setProperty ("seconds_with_measuring", total);
        r->setProperty ("measured", p.previewId.valid);
        r->setProperty ("cos_a", p.previewId.cosA); r->setProperty ("cos_b", p.previewId.cosB); r->setProperty ("cos_perf", p.previewId.cosPerf); r->setProperty ("cos_a_b", p.previewId.cosAB);
        r->setProperty ("dist_a", p.previewId.distA()); r->setProperty ("dist_b", p.previewId.distB()); r->setProperty ("unique", p.previewId.unique());
        o->setProperty (name, juce::var (r));
        std::cout << "ok    " << name << ": " << engineName (e) << " " << juce::roundToInt (balance * 100) << " % B  dist A " << p.previewId.distA() << "  dist B " << p.previewId.distB()
                  << "  unique " << (int) p.previewId.unique() << "  (render " << p.previewSeconds << " s)" << std::endl;
        return true;
    };
    if (quick)
    {
        o->setProperty ("machine", juce::SystemStats::getCpuModel() + " (" + juce::String (juce::SystemStats::getNumCpus()) + " cores)");
        if (! runOne ("natural_050", Engine::natural, 0.5f, 0) || ! runOne ("classic_050", Engine::classic, 0.5f, 0) || ! runOne ("seedvc_050", Engine::seedvc, 0.5f, 0)) return 1;
        p.engine = Engine::natural;
        p.board.orbit.mode = ghosty::spirit::Orbit::Circle; p.board.orbit.depth = 0.5f; p.board.orbit.rate = 1.0f / 8.0f;
        if (! p.renderSong() || ! waitFor (p, 600 * 1000) || ! p.result.existsAsFile()) { std::cout << "FAIL  natural song: " << p.lastError << std::endl; return 1; }
        p.result.copyFileTo (out.getChildFile ("song_motion_natural.wav"));
        o->setProperty ("song_motion_natural_info", p.resultInfo);
        std::cout << "ok    song_motion: " << p.resultInfo << std::endl;
        out.getChildFile ("e2e.json").replaceWithText (juce::JSON::toString (keep));
        return 0;
    }
    for (auto [n, b] : { std::pair<const char*, float> { "000", 0.0f }, { "025", 0.25f }, { "050", 0.5f }, { "075", 0.75f }, { "100", 1.0f } })
    {
        if (! runOne (juce::String ("natural_") + n, Engine::natural, b, 0)) return 1;
        if (! runOne (juce::String ("classic_") + n, Engine::classic, b, 0)) return 1;
    }
    if (! runOne ("voiceC_50_novelty0", Engine::seedvc, 0.5f, 0.0f)) return 1;
    out.getChildFile ("voiceC_50_novelty0.wav").copyFileTo (out.getChildFile ("seedvc_050.wav"));
    if (! runOne ("voiceC_50_novelty60", Engine::seedvc, 0.5f, 0.6f)) return 1;
    if (! runOne ("voiceC_50_novelty100", Engine::seedvc, 0.5f, 1.0f)) return 1;
    for (auto [n, b] : { std::pair<const char*, float> { "000", 0.0f }, { "025", 0.25f }, { "075", 0.75f }, { "100", 1.0f } })
        if (! runOne (juce::String ("seedvc_") + n, Engine::seedvc, b, 0)) return 1;
    if (! runOne ("best_of_all_015", Engine::best, 0.15f, 0.0f)) return 1;
    juce::String why;
    if (! p.saveVoiceC ("CI Voice", why)) { std::cout << "FAIL  save: " << why << std::endl; return 1; }
    std::cout << "ok    saved to the library" << std::endl;
    p.settings.novelty = 0.0f; p.setBalance (0.5f);
    p.board.orbit.mode = ghosty::spirit::Orbit::Circle; p.board.orbit.depth = 0.5f; p.board.orbit.rate = 1.0f / 8.0f;   // a trip every 2 bars at 120 bpm, A to B
    for (auto [n, e] : { std::pair<const char*, Engine> { "song_motion_natural", Engine::natural }, { "song_motion_best_of_all", Engine::best }, { "song_motion", Engine::seedvc } })
    {
        p.engine = e;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        if (! p.renderSong() || ! waitFor (p, 3600 * 1000) || ! p.result.existsAsFile()) { std::cout << "FAIL  " << n << ": " << p.lastError << " " << p.runner.message() << "\n" << p.runner.log() << std::endl; return 1; }
        p.result.copyFileTo (out.getChildFile (juce::String (n) + ".wav"));
        o->setProperty (juce::String (n) + "_seconds", (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0);
        o->setProperty (juce::String (n) + "_info", p.resultInfo);
        std::cout << "ok    " << n << ": " << p.resultInfo << std::endl;
    }
    out.getChildFile ("e2e.json").replaceWithText (juce::JSON::toString (keep));
    p.library.clear();
    return 0;
}

// ================================================================================================ CHOP (build 8: the instrument)
// a beat with no recording of anyone: a kick on every beat (decaying 55 Hz), a hat on the 8ths, a held tone, at `bpm`
static juce::File makeBeat (const juce::String& name, double bpm, int bars, int fs = 44100)
{
    const double beat = 60.0 / bpm; const int n = (int) (bars * 4 * beat * fs);
    std::vector<float> x ((size_t) n, 0.0f); juce::Random rng (7);
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / fs, tb = std::fmod (t, beat), t8 = std::fmod (t, beat * 0.5);
        double v = 0.18 * std::sin (2 * juce::MathConstants<double>::pi * 220.0 * t);
        v += 0.9 * std::exp (-tb * 28.0) * std::sin (2 * juce::MathConstants<double>::pi * 55.0 * tb * (1.0 + 2.0 * std::exp (-tb * 40.0)));
        v += 0.25 * std::exp (-t8 * 90.0) * (rng.nextDouble() * 2 - 1);
        x[(size_t) i] = (float) juce::jlimit (-1.0, 1.0, v * 0.7);
    }
    auto f = tmp.getChildFile (name); writeWav (f, x, fs, 24); return f;
}
// plays blocks through the processor (INTERSECT finishes loads on the audio thread) and runs the message loop
static double pump (Processor& p, int ms, int noteOn = -1, int noteOff = -1)
{
    juce::AudioBuffer<float> b (2, 512); double e = 0; int blocks = 0;
    const auto t0 = juce::Time::getMillisecondCounter();
    while (juce::Time::getMillisecondCounter() - t0 < (juce::uint32) ms || blocks < 60)   // (at least 60 blocks: a slow Mac still sends the note-off)
    {
        juce::MidiBuffer m;
        if (blocks == 0 && noteOn >= 0) m.addEvent (juce::MidiMessage::noteOn (1, noteOn, 0.9f), 0);
        if (blocks == 40 && noteOff >= 0) m.addEvent (juce::MidiMessage::noteOff (1, noteOff), 0);
        b.clear(); p.processBlock (b, m); ++blocks;
        for (int c = 0; c < 2; ++c) e += b.getRMSLevel (c, 0, 512);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (4);
    }
    return e / juce::jmax (1, blocks);
}
static bool pumpUntil (Processor& p, std::function<bool()> ok, int ms = 15000)
{
    const auto t0 = juce::Time::getMillisecondCounter();
    while (juce::Time::getMillisecondCounter() - t0 < (juce::uint32) ms) { pump (p, 40); if (ok()) return true; }
    return ok();
}

static void chopTests (const juce::File& shots)
{
    Processor p;
    p.prepareToPlay (44100, 512);
    auto& c = *p.chopper;
    check (p.acceptsMidi() && ! p.producesMidi(), "an instrument: it takes MIDI");
    {
        juce::AudioProcessor::BusesLayout l; l.inputBuses.add (juce::AudioChannelSet::disabled()); l.outputBuses.add (juce::AudioChannelSet::stereo());
        check (p.checkBusesLayoutSupported (l), "it runs with no input (FL Studio's channel rack)");
    }
    {   // silence until there is something to play: an instrument never passes its input through
        juce::AudioBuffer<float> b (2, 512); for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < 512; ++i) b.setSample (ch, i, 0.5f);
        juce::MidiBuffer m; p.processBlock (b, m);
        check (b.getMagnitude (0, 512) < 1.0e-6f, "the input is never heard (an instrument)");
    }
    const auto beatFile = makeBeat ("beat120.wav", 120.0, 4);
    juce::String why;
    check (c.loadFile (beatFile, why) && c.hasClip() && c.frames() == (juce::int64) (8.0 * 44100), "a WAV loads into the clip (" + why + ")");
    check (pumpUntil (p, [&] { return c.engineLoaded() && c.slices().size() == 1; }), "the chopper engine has it: one slice, the whole clip");
    {
        const auto sl = c.slices();
        check (sl.size() == 1 && sl[0].note == 0 && sl[0].highNote == 127 && sl[0].r.a == 0 && std::abs (sl[0].r.b - c.frames()) < 4,
               "WHOLE: the clip covers every key (C3 plays it as it is)");
    }
    check (pump (p, 300, 60, 60) > 0.01, "a key plays it (WHOLE, C3)");
    check (pump (p, 300, 48, 48) > 0.01, "another key plays it pitched (WHOLE, C2)");
    check (pumpUntil (p, [&] { return c.detected().ready; }), "the clip is analysed (tempo, key, hits)");
    const auto det = c.detected();
    check (near (det.bpm, 120.0, 1.5), "the tempo is found: " + juce::String (det.bpm, 2) + " BPM (made at 120)");
    check (det.onsets.size() >= 12, "the hits are found (" + juce::String ((int) det.onsets.size()) + ")");
    // AUTO CHOP, every mode
    for (int m = 0; m < (int) chop::AutoMode::phrases; ++m)   // (PHRASES / WORDS: below, they need Python)
    {
        c.autoMode = (chop::AutoMode) m; c.chopCount = 8;
        const bool ok = c.autoChop();
        const bool in = pumpUntil (p, [&] { return (int) c.slices().size() == (int) c.chops.size(); }, 5000);
        const auto sl = c.slices();
        bool keysOk = ! sl.empty();
        for (size_t i = 0; i < sl.size(); ++i) keysOk = keysOk && sl[i].note == chop::ChopEngine::rootNote + (int) i && sl[i].highNote == sl[i].note;
        check (ok && in && c.keys == chop::KeysMode::chops && c.chops.size() >= 4 && c.chops.size() <= 8 && keysOk,
               "AUTO CHOP " + chop::autoModeName ((chop::AutoMode) m) + ": " + juce::String ((int) c.chops.size()) + " chops, one per key from C1");
    }
    {   // RANDOM: on the beat (snapped to a hit), 1 or 2 beats long, never the same beat twice; a new set each press
        c.autoMode = chop::AutoMode::random; c.chopCount = 6; c.autoChop(); const auto first = c.chops; c.autoChop();
        const double beat = 60.0 / det.bpm * 44100;
        bool onBeat = true, lengths = true; std::set<juce::int64> starts;
        for (auto& r : c.chops)
        {
            const double k = (r.a - det.firstBeat * 44100) / beat; onBeat = onBeat && std::abs (k - std::round (k)) * beat < 0.035 * 44100;
            const double L = (double) r.length() / beat; lengths = lengths && (std::abs (L - 1) < 0.1 || std::abs (L - 2) < 0.1 || r.b == c.frames());
            starts.insert (r.a);
        }
        check (onBeat && lengths && starts.size() == c.chops.size(), "RANDOM chops sit on the beat, 1 or 2 beats long, all different");
        check (first != c.chops, "pressing AUTO CHOP again makes a new set");
    }
    pumpUntil (p, [&] { return (int) c.slices().size() == (int) c.chops.size(); }, 5000);
    check (pump (p, 200, chop::ChopEngine::rootNote, chop::ChopEngine::rootNote) > 0.005, "C1 plays the first chop");
    check (pump (p, 200, chop::ChopEngine::rootNote + 40) < 1.0e-5, "a key with no chop is silent");
    // SLICE / DELETE tools
    {
        c.setKeys (chop::KeysMode::whole); c.clearChops();
        c.splitAt (c.frames() / 2);
        check (c.chops.size() == 2 && c.chops[0].b == c.frames() / 2 && c.keys == chop::KeysMode::chops, "SLICE cuts the clip in two");
        c.splitAt (c.frames() / 4); check (c.chops.size() == 3, "SLICE again: three chops");
        check (c.removeChopAt (c.frames() / 8) && c.chops.size() == 2, "DELETE removes the chop under the click");
        pumpUntil (p, [&] { return c.slices().size() == 2; }, 5000);
        check (c.slices().size() == 2, "the engine follows the chops");
    }
    // FL-style edits + undo
    {
        const auto L = c.frames(); const chop::Region beat1 { 0, 22050 };
        check (c.copy (beat1) && c.hasClipboard(), "COPY");
        check (c.paste (L) && c.frames() == L + 22050, "PASTE at the end makes it longer");
        check (c.undo() && c.frames() == L, "UNDO");
        check (c.redo() && c.frames() == L + 22050, "REDO");
        check (c.undo() && c.frames() == L, "(undo again)");
        check (c.cut ({ 22050, 44100 }) && c.frames() == L - 22050, "CUT");
        check (c.paste (0) && c.frames() == L, "PASTE at the start");
        check (c.erase ({ 0, 11025 }) && c.frames() == L - 11025, "DELETE the selected audio");
        check (c.duplicate ({ 0, 11025 }) && c.frames() == L, "DUPLICATE");
        check (c.trim ({ 0, 44100 }) && c.frames() == 44100, "TRIM keeps only the selection");
        for (int i = 0; i < 6; ++i) c.undo();
        check (c.frames() == L, "six undos bring the original back");
        check (! c.erase ({ 0, c.frames() }), "deleting everything is refused (nothing would be left)");
        pumpUntil (p, [&] { return c.engineLoaded(); }, 8000);
        check (c.engineLoaded(), "the engine has the edited clip");
    }
    // the clip's transport
    {
        c.play (0, {}, false); const double e = pump (p, 200); c.stop();
        check (e > 0.01, "PLAY plays the clip");
        c.play (0, { 0, 4410 }, true); pump (p, 400);
        check (c.isPlaying() && c.playhead() < 4410, "LOOP keeps it inside the loop"); c.stop();
    }
    // state: a new instance restores the clip, the chops and the keys
    {
        c.autoMode = chop::AutoMode::beats; c.chopCount = 8; c.autoChop(); pumpUntil (p, [&] { return (int) c.slices().size() == (int) c.chops.size(); }, 5000);
        juce::MemoryBlock st; p.getStateInformation (st);
        Processor q; q.prepareToPlay (44100, 512); q.setStateInformation (st.getData(), (int) st.getSize());
        auto& d = *q.chopper;
        check (d.hasClip() && d.frames() == c.frames() && d.chops.size() == c.chops.size() && d.keys == chop::KeysMode::chops, "a saved project brings back the clip and the chops");
        check (pumpUntil (q, [&] { return d.engineLoaded() && d.slices().size() == c.slices().size(); }), "and the engine's slices");
        check (pump (q, 200, chop::ChopEngine::rootNote, chop::ChopEngine::rootNote) > 0.005, "and it plays");
    }

    // PHRASES (OpenVPI's slicer) and WORDS (WhisperX), each a separate Python process (build 9)
    {
        const auto py = juce::SystemStats::getEnvironmentVariable ("GHOSTVOICE_TEST_PHRASES_PYTHON", {});
        if (py.isNotEmpty())
        {
            setenv ("GHOSTVOICE_CHOP_PYTHON", py.toRawUTF8(), 1);
            std::vector<float> x; for (int k = 0; k < 3; ++k) { for (int i = 0; i < 30000; ++i) x.push_back (0.5f * (float) std::sin (0.05 * i)); x.insert (x.end(), 22050, 0.0f); }
            auto f = tmp.getChildFile ("phrases.wav"); writeWav (f, x, 44100, 24);
            juce::String why; c.loadFile (f, why); pumpUntil (p, [&] { return c.engineLoaded(); });
            c.autoMode = chop::AutoMode::phrases;
            check (c.toolReady() && c.autoChop(), "PHRASES starts");
            check (pumpUntil (p, [&] { return ! c.toolBusy() && c.chops.size() == 3; }, 30000), "PHRASES finds the 3 phrases (" + c.message + ")");
            check (c.chops.size() == 3 && std::abs (c.chops[0].a) < 2000 && std::abs (c.chops[1].a - 52050) < 10000, "at the right places (" + juce::String (c.chops[1].a) + ")");
            unsetenv ("GHOSTVOICE_CHOP_PYTHON");
        }
        else std::cout << "skip  PHRASES (set GHOSTVOICE_TEST_PHRASES_PYTHON to a python with numpy)" << std::endl;
        const auto wpy = juce::SystemStats::getEnvironmentVariable ("GHOSTVOICE_TEST_WORDS_PYTHON", {}), speech = juce::SystemStats::getEnvironmentVariable ("GHOSTVOICE_TEST_SPEECH", {});
        if (wpy.isNotEmpty() && speech.isNotEmpty())
        {
            setenv ("GHOSTVOICE_CHOP_PYTHON", wpy.toRawUTF8(), 1);
            juce::String why; c.loadFile (juce::File (speech), why); pumpUntil (p, [&] { return c.engineLoaded(); });
            c.autoMode = chop::AutoMode::words;
            check (c.autoChop(), "WORDS starts");
            check (pumpUntil (p, [&] { return ! c.toolBusy() && ! c.chops.empty(); }, 300000), "WORDS finds the words (" + c.message + ")");
            juce::StringArray w; for (size_t i = 0; i < c.chops.size(); ++i) w.add (c.labelFor (i));
            check (c.chops.size() >= 5 && w.joinIntoString (" ").containsIgnoreCase ("hello"), "each word on its own key, named: " + w.joinIntoString (" "));
            pumpUntil (p, [&] { return (int) c.slices().size() == (int) c.chops.size(); }, 8000);
            check ((int) c.slices().size() == (int) c.chops.size(), "the engine has a chop per word");
            unsetenv ("GHOSTVOICE_CHOP_PYTHON");
        }
        else std::cout << "skip  WORDS (set GHOSTVOICE_TEST_WORDS_PYTHON and GHOSTVOICE_TEST_SPEECH)" << std::endl;
    }
    // SOUND: one chop's own settings go to the engine and read back
    {
        c.autoMode = chop::AutoMode::even; c.chopCount = 4; c.autoChop(); pumpUntil (p, [&] { return (int) c.slices().size() == 4; });
        using IP = IntersectProcessor;
        c.setSliceParam (2, IP::FieldPitch, 7.0f); c.setSliceParam (2, IP::FieldReverse, 1.0f); c.setSliceParam (2, IP::FieldVolume, -6.0f);
        pump (p, 100);
        check (near (c.sliceParam (2, IP::FieldPitch), 7.0, 1e-4) && c.sliceParam (2, IP::FieldReverse) > 0.5f && near (c.sliceParam (2, IP::FieldVolume), -6.0, 1e-4),
               "SOUND: a chop's pitch, reverse and volume are set on that chop");
        check (near (c.sliceParam (1, IP::FieldPitch), 0.0, 1e-4) && c.sliceParam (1, IP::FieldReverse) < 0.5f, "and only on that chop");
        check (pump (p, 200, chop::ChopEngine::rootNote + 2, chop::ChopEngine::rootNote + 2) > 0.003, "the changed chop still plays");
    }
    // CONVERT VOICE with the built-in NATURAL engine: the selection is sung by voice C and put back in place
    {
        p.engine = Engine::natural; p.rights = true;
        p.addTakes (slotA, { makeSinger ("chopA.wav", 140, 1.0, 3.0, 1).getFullPathName() });
        p.addTakes (slotB, { makeSinger ("chopB.wav", 220, 0.8, 3.0, 2).getFullPathName() });
        const auto sing = makeSinger ("chopSing.wav", 180, 0.9, 2.0, 3);
        check (c.loadFile (sing, why), "a vocal loads");
        Processor* pp = &p;
        std::unique_ptr<Editor> ed (dynamic_cast<Editor*> (p.createEditor()));
        auto* page = ed->chopPage();
        check (page != nullptr && ed->page() == Editor::chop && page->isVisible(), "the window opens on CHOP");
        const auto L = c.frames();
        page->selection = { L / 4, L / 2 };
        page->convertVoice();
        check (page->converting(), "CONVERT VOICE starts on the selection");
        const bool done = pumpUntil (*pp, [&] { return ! page->converting(); }, 60000);
        check (done && std::abs (c.frames() - L) < 44100 * 0.02 && c.canUndo(), "the converted part is back in its place (" + page->status() + ")");
        check (c.undo() && c.frames() == L, "Cmd+Z puts the original back");
        // the keys
        page->setTool (ChopPage::Tool::select);
        check (page->keyPressed (juce::KeyPress ('c')) && page->tool() == ChopPage::Tool::slice, "C picks SLICE (Block Step's letters)");
        check (page->keyPressed (juce::KeyPress ('e')) && page->tool() == ChopPage::Tool::select, "E picks SELECT");
        check (page->keyPressed (juce::KeyPress ('a', juce::ModifierKeys::commandModifier, 0)) && page->selection.length() == c.frames(), "Cmd+A selects everything");
        check (page->keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)) && c.isPlaying(), "SPACE plays");
        check (page->keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)) && ! c.isPlaying(), "SPACE stops");
        check (page->keyPressed (juce::KeyPress ('u', juce::ModifierKeys::commandModifier, 0)), "Cmd+U = AUTO CHOP");
        pumpUntil (p, [&] { return ! c.chops.empty(); }, 8000);
        check (! c.chops.empty(), "(AUTO CHOP made chops)");
        page->selection = { 0, c.frames() / 3 };
        check (page->keyPressed (juce::KeyPress ('l', juce::ModifierKeys::commandModifier, 0)) && page->loopOn && page->loop == page->selection, "Cmd+L loops the selection");
        if (shots != juce::File())
            for (auto th : { ghosty::themes::Theme::Dark, ghosty::themes::Theme::Purple, ghosty::themes::Theme::Light })
            {
                p.theme = th; ed->applyTheme(); ed->showPage (Editor::chop); pump (p, 100);
                auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
                juce::FileOutputStream os (shots.getChildFile (juce::String ("ghostvoice-chop-") + (th == ghosty::themes::Theme::Dark ? "black" : th == ghosty::themes::Theme::Purple ? "purple" : "light") + ".png"));
                if (os.openedOk()) { os.setPosition (0); os.truncate(); juce::PNGImageFormat().writeImageToStream (img, os); }
            }
        p.theme = ghosty::themes::Theme::Dark;
        ed = nullptr;
    }
}

// UNDER THE HOOD = the ORIGINAL CONTROLS page of every open-source engine (Brandon 2026-11-22)
static void hoodOriginalsTests()
{
    using IP = IntersectProcessor;
    Processor p; p.prepareToPlay (44100, 512);
    auto& c = *p.chopper;
    juce::String why;
    check (c.loadFile (makeBeat ("hoodBeat.wav", 120.0, 4), why) && pumpUntil (p, [&] { return c.engineLoaded() && c.detected().ready; }), "HOOD: a clip is in");
    c.autoMode = chop::AutoMode::even; c.chopCount = 4; c.autoChop(); pumpUntil (p, [&] { return (int) c.slices().size() == 4; });
    std::unique_ptr<Editor> ed (dynamic_cast<Editor*> (p.createEditor()));
    ed->showPage (Editor::hood);
    auto* h = ed->hoodPage();
    // 1. a sub-tab per engine
    {
        const juce::StringArray want { "SEED-VC", "INTERSECT  SOUND", "INTERSECT  AUTO CHOP", "OPENVPI  PHRASES", "FLAYR CHOP" };
        bool ok = HoodPage::kTabs == 5;
        for (int i = 0; i < HoodPage::kTabs; ++i)
        {
            h->showTab (i);
            ok = ok && h->tab() == i && h->tabTitle (i) == want[i] && (i == 0 ? h->panel (i) == nullptr : (h->panel (i) != nullptr && h->panel (i)->isVisible()));
            for (int k = 1; k < HoodPage::kTabs; ++k) if (k != i) ok = ok && ! h->panel (k)->isVisible();
        }
        check (ok, "UNDER THE HOOD has a sub-tab per engine: SEED-VC, INTERSECT SOUND, INTERSECT AUTO CHOP, OPENVPI PHRASES, FLAYR CHOP");
        h->showTab (HoodPage::seedvc);
        check (h->shownRows() == 14 && h->subLine().contains ("neural engine"), "the SEED-VC sub-tab is the same 14-option page");
    }
    // 2. every INTERSECT field of its SignalChainBar has its control, under INTERSECT's own name
    h->showTab (HoodPage::intersectSound);
    auto* snd = h->panel (HoodPage::intersectSound);
    {
        const std::pair<int, const char*> want[] {
            { IP::FieldBpm, "BPM" }, { IP::FieldPitch, "PITCH" }, { IP::FieldCentsDetune, "TUNE" }, { IP::FieldStretchEnabled, "STRETCH" }, { IP::FieldAlgorithm, "ALGO" },
            { IP::FieldRepitchMode, "MODE" }, { IP::FieldTonality, "TONAL" }, { IP::FieldFormant, "FMNT" }, { IP::FieldFormantComp, "FMNT C" }, { IP::FieldGrainMode, "GRAIN" },
            { IP::FieldFilterType, "TYPE" }, { IP::FieldFilterSlope, "SLOPE" }, { IP::FieldFilterCutoff, "CUT" }, { IP::FieldFilterReso, "RESO" }, { IP::FieldFilterDrive, "DRIVE" },
            { IP::FieldFilterAsym, "ASYM" }, { IP::FieldFilterKeyTrack, "KEY" }, { IP::FieldFilterEnvAmount, "AMT" }, { IP::FieldFilterEnvAttack, "ATK" }, { IP::FieldFilterEnvDecay, "DEC" },
            { IP::FieldFilterEnvSustain, "SUS" }, { IP::FieldFilterEnvRelease, "REL" }, { IP::FieldAttack, "ATK" }, { IP::FieldDecay, "DEC" }, { IP::FieldSustain, "SUS" },
            { IP::FieldRelease, "REL" }, { IP::FieldReleaseTail, "TAIL" }, { IP::FieldVolume, "GAIN" }, { IP::FieldReverse, "REV" }, { IP::FieldLoop, "LOOP" },
            { IP::FieldCrossfade, "FADE" }, { IP::FieldMuteGroup, "MUTE" }, { IP::FieldOneShot, "1SHOT" }, { IP::FieldOutputBus, "OUT" } };
        juce::String missing;
        for (auto& [f, n] : want) { auto* cell = snd->find (f); if (cell == nullptr || cell->name != n || cell->plain.isEmpty()) missing << " " << n; }
        for (int id : { (int) IP::FieldFilterEnabled, (int) hoodui::idSetBpm, (int) hoodui::idVoices, (int) hoodui::idRoot, (int) hoodui::idSlices, (int) hoodui::idNote })
            if (! snd->hasControl (id)) missing << " #" << id;
        check (missing.isEmpty(), "INTERSECT SOUND has all 34 of its SignalChainBar controls under its own names, each with a plain line, plus FILTER ON/OFF, SET BPM, "
                                  "and the context row's NOTE, SLICES, ROOT and VOICES" + missing);
        auto* algo = snd->find (IP::FieldAlgorithm); auto* loop = snd->find (IP::FieldLoop); auto* type = snd->find (IP::FieldFilterType);
        check (algo != nullptr && algo->choices == juce::StringArray { "Repitch", "Signalsmith", "Bungee" } && loop->choices == juce::StringArray { "Off", "Loop", "PP" }
               && type->choices == juce::StringArray { "LP", "HP", "BP", "NT" }, "its choices are INTERSECT's own option names (ALGO Repitch / Signalsmith / Bungee ...)");
    }
    // 3. the same values as CHOP's SOUND panel
    {
        snd->setScope (true);
        ed->chopPage()->selectChop (2); snd->sync();
        auto soundSlider = [&] (const juce::String& name) -> juce::Slider*
        {
            juce::Slider* found = nullptr;
            std::function<void (juce::Component&)> walk = [&] (juce::Component& k) { if (auto* sl = dynamic_cast<juce::Slider*> (&k)) if (sl->getTooltip().startsWith (name + ":")) found = sl; for (auto* x : k.getChildren()) walk (*x); };
            walk (*ed->chopPage()); return found;
        };
        check (snd->touch (IP::FieldPitch, 5.0f) && snd->touch (IP::FieldAttack, 250.0f) && snd->touch (IP::FieldFilterEnabled, 1.0f), "HOOD: PITCH, ATK and the filter are set on chop 3");
        pump (p, 120); snd->sync();
        check (snd->touch (IP::FieldFilterReso, 40.0f), "and RESO (the filter is on now)");
        pump (p, 120);
        ed->chopPage()->selectChop (2);
        auto *pitch = soundSlider ("PITCH"), *atk = soundSlider ("ATK"), *reso = soundSlider ("RESO");
        check (near (c.sliceParam (2, IP::FieldPitch), 5.0, 1e-4) && near (c.sliceParam (2, IP::FieldAttack), 0.25, 1e-4) && near (c.sliceParam (2, IP::FieldFilterReso), 40.0, 1e-3),
               "a control on UNDER THE HOOD writes INTERSECT's own value (PITCH +5 st, ATK 250 ms, RESO 40 %)");
        check (pitch != nullptr && atk != nullptr && reso != nullptr && near (pitch->getValue(), 5.0, 1e-4) && near (atk->getValue(), 250.0, 0.5) && near (reso->getValue(), 40.0, 0.05),
               "CHOP's SOUND panel shows the same values (PITCH +5, ATK 250 ms, RESO 40 %)");
        c.setSliceParam (2, IP::FieldVolume, -6.0f); pump (p, 120); snd->sync();
        check (near (snd->find (IP::FieldVolume)->value, -6.0, 1e-4) && snd->find (IP::FieldVolume)->own, "and the other way: GAIN set from CHOP shows on UNDER THE HOOD (as the chop's own)");
        check (! snd->find (IP::FieldFormantComp)->on && ! snd->find (IP::FieldGrainMode)->on && snd->find (IP::FieldRepitchMode)->on && ! snd->find (IP::FieldOutputBus)->on
               && ! snd->find (IP::FieldBpm)->on && ! snd->find (IP::FieldCrossfade)->on,
               "greyed when they do nothing: FMNT C / GRAIN on Repitch, BPM with STRETCH off, FADE with LOOP off, OUT (one output)");
        // SAMPLE: the clip's own value reaches every chop that doesn't set its own
        snd->setScope (false); snd->sync();
        check (snd->touch (IP::FieldVolume, -3.0f), "HOOD SAMPLE tab: GAIN -3 dB for the clip");
        pump (p, 120); snd->sync();
        check (near (c.sampleParam (IP::FieldVolume), -3.0, 1e-4) && near (c.sliceParam (1, IP::FieldVolume), -3.0, 1e-4) && near (c.sliceParam (2, IP::FieldVolume), -6.0, 1e-4),
               "SAMPLE sets the clip's own level: chop 2 follows it, chop 3 keeps its own");
        snd->setScope (true);
    }
    // 4. INTERSECT's AUTO CHOP: SPLIT EQUAL (DIV) and SPLIT TRANSIENTS make chops, with undo
    {
        h->showTab (HoodPage::intersectAutoChop);
        auto* ac = h->panel (HoodPage::intersectAutoChop);
        check (ac->hasControl (hoodui::idSens) && ac->hasControl (hoodui::idMin) && ac->hasControl (hoodui::idDiv) && ac->find (hoodui::idSens)->value == 50.0f
               && ac->find (hoodui::idMin)->value == 100.0f && ac->find (hoodui::idDiv)->value == 16.0f, "INTERSECT AUTO CHOP: SENS 50, MIN 100 ms, DIV 16 (its own defaults)");
        ed->chopPage()->selectChop (1); ac->sync();
        check (ac->touch (hoodui::idDiv, 3.0f) && c.split.div == 3, "DIV writes the setting");
        const auto before = c.chops;
        check (ac->pressButton ("SPLIT EQUAL") && c.chops.size() == before.size() + 2 && c.chops[1].a == before[1].a && c.chops[3].b == before[1].b, "SPLIT EQUAL: chop 2 becomes 3 equal chops");
        check (pumpUntil (p, [&] { return (int) c.slices().size() == (int) c.chops.size(); }), "and they are on the keys");
        check (c.undo() && c.chops == before, "Cmd+Z takes the split back");
        pumpUntil (p, [&] { return (int) c.slices().size() == (int) c.chops.size(); });
        c.setKeys (chop::KeysMode::whole); c.clearChops(); pumpUntil (p, [&] { return c.slices().size() == 1; });
        ed->chopPage()->selectChop (0); ac->sync();
        const auto pts = c.transientPoints (0, 50.0f, 100.0f);
        check (pts.size() >= 8, "SPLIT TRANSIENTS finds the beat's hits with INTERSECT's own finder (" + juce::String ((int) pts.size()) + ")");
        check (ac->pressButton ("SPLIT TRANSIENTS") && c.chops.size() == pts.size() + 1 && c.keys == chop::KeysMode::chops, "SPLIT TRANSIENTS chops the whole clip at them");
        check (pumpUntil (p, [&] { return (int) c.slices().size() == (int) c.chops.size(); }), "and they play from C1");
    }
    // 5. OpenVPI's options: untouched, the helper's command line is exactly what it always was
    {
        const chop::Tools t;
        const juce::File wav ("/x/clip.wav"), out ("/x/out.json");
        const juce::StringArray old { "/usr/bin/env", "HF_HOME=" + chop::Tools::dir().getChildFile ("hf").getFullPathName(), "TORCH_HOME=" + chop::Tools::dir().getChildFile ("torch").getFullPathName(),
                                      "PYTHONNOUSERSITE=1", "py", chop::Tools::script().getFullPathName(), "phrases", wav.getFullPathName(), out.getFullPathName() };
        const chop::ChopEngine::PhraseSettings d;
        check (d.isDefault() && d.db == -40.0f && d.minLength == 500 && d.minInterval == 150 && d.hop == 10 && d.maxSilKept == 200 && d.args().isEmpty()
               && t.commandFor (chop::Tools::Kind::phrases, "py", wav, out, c.phrase.args()) == old, "untouched OpenVPI options: the exact same helper command line as before");
        h->showTab (HoodPage::phrases);
        auto* ph = h->panel (HoodPage::phrases);
        c.autoMode = chop::AutoMode::hits; ph->sync();
        check (ph->find (hoodui::idDb) != nullptr && ! ph->find (hoodui::idDb)->on, "OPENVPI PHRASES: greyed while AUTO CHOP isn't on PHRASES");
        check (ph->pressButton ("USE PHRASES") && c.autoMode == chop::AutoMode::phrases && ph->find (hoodui::idDb)->on, "USE PHRASES switches AUTO CHOP to PHRASES");
        check (ph->touch (hoodui::idDb, -35.0f) && ph->touch (hoodui::idMinLength, 400.0f), "db_thresh and min_length change");
        const auto cmd = t.commandFor (chop::Tools::Kind::phrases, "py", wav, out, c.phrase.args());
        check (cmd.joinIntoString (" ").endsWith ("--db_thresh -35.0 --min_length 400 --min_interval 150 --hop_size 10 --max_sil_kept 200"), "and go to the helper under slicer2.py's own names: " + cmd.joinIntoString (" ").fromLastOccurrenceOf ("out.json", false, false));
        const auto py = juce::SystemStats::getEnvironmentVariable ("GHOSTVOICE_TEST_PHRASES_PYTHON", {});
        if (py.isNotEmpty())
        {   // the helper reads them: defaults given out loud = nothing given; changed ones change the result
            std::vector<float> x; for (int k = 0; k < 3; ++k) { for (int i = 0; i < 30000; ++i) x.push_back (0.5f * (float) std::sin (0.05 * i)); x.insert (x.end(), 9000, 0.0f); }
            auto f = tmp.getChildFile ("hoodPhrases.wav"); writeWav (f, x, 44100, 24);
            auto run = [&] (juce::StringArray extra) -> juce::String
            {
                auto o = tmp.getChildFile ("hoodPhrases.json"); o.deleteFile();
                juce::StringArray a { py, chop::Tools::script().getFullPathName(), "phrases", f.getFullPathName(), o.getFullPathName() }; a.addArray (extra);
                juce::ChildProcess cp; if (! cp.start (a)) return "no start"; cp.readAllProcessOutput(); cp.waitForProcessToFinish (30000);
                return o.loadFileAsString();
            };
            const auto plain = run ({}), spelled = run ({ "--db_thresh", "-40.0", "--min_length", "500", "--min_interval", "150", "--hop_size", "10", "--max_sil_kept", "200" }),
                       longer = run ({ "--db_thresh", "-40.0", "--min_length", "500", "--min_interval", "300", "--hop_size", "10", "--max_sil_kept", "200" });
            check (plain.contains ("\"ok\": true") && plain == spelled, "the helper with no options = with Ghost Voice's values spelled out (the same phrases)");
            check (longer.contains ("\"ok\": true") && longer != plain, "and min_interval 300 ms (longer than the 204 ms gaps) changes what it finds");
        }
        else std::cout << "skip  the helper's own reading of the options (set GHOSTVOICE_TEST_PHRASES_PYTHON)" << std::endl;
        check (ph->pressButton ("GHOST VOICE START") && c.phrase.isDefault(), "GHOST VOICE START puts them back");
    }
    // 6. FLAYR CHOP: its modes, the ones Ghost Voice has are live
    {
        h->showTab (HoodPage::flayr);
        auto* fl = h->panel (HoodPage::flayr);
        check (fl->touch (hoodui::idFlayrMode, 1.0f) && c.autoMode == chop::AutoMode::beats && fl->touch (hoodui::idFlayrPads, 8.0f) && c.chopCount == 8, "FLAYR CHOP: Every beat, 8 pads = EVERY BEAT, 8 chops");
        check (! fl->touch (hoodui::idFlayrMode, 6.0f) && c.autoMode == chop::AutoMode::beats, "its Random (Flayr Chop's player tool) is greyed");
        check (fl->pressButton ("CHOP") && c.chops.size() >= 4 && c.chops.size() <= 8, "CHOP chops the clip (" + juce::String ((int) c.chops.size()) + ")");
    }
    // 7. state: the new settings persist; an old project (without them) loads with today's values
    {
        c.phrase.db = -30.0f; c.phrase.minInterval = 200; c.split.sens = 70.0f; c.split.div = 8;
        auto t = c.toTree();
        chop::ChopEngine d; d.fromTree (t);
        check (d.phrase.db == -30.0f && d.phrase.minInterval == 200 && d.split.sens == 70.0f && d.split.div == 8, "the OpenVPI and auto-chop settings are saved with the project");
        auto old = t.createCopy();
        for (auto k : { "phraseDb", "phraseMinLength", "phraseMinInterval", "phraseHop", "phraseMaxSilKept", "splitSens", "splitMinMs", "splitDiv" }) old.removeProperty (k, nullptr);
        chop::ChopEngine o; o.phrase.db = -12.0f; o.split.div = 99; o.fromTree (old);
        const chop::ChopEngine::SplitSettings sd;
        check (o.phrase.isDefault() && o.split.sens == sd.sens && o.split.minMs == sd.minMs && o.split.div == sd.div && o.autoMode == c.autoMode && o.chopCount == c.chopCount
               && o.chops == c.chops && o.keys == c.keys, "an old project (no new keys) loads with identical values: today's PHRASES and auto-chop settings, its chops and keys");
        juce::StringArray oldKeys; for (int i = 0; i < old.getNumProperties(); ++i) oldKeys.add (old.getPropertyName (i).toString());
        auto again = o.toTree(); bool same = true;
        for (auto& k : oldKeys) same = same && (k == "engine" || again.getProperty (k) == old.getProperty (k));
        check (same, "and every key it had keeps its meaning (saved again, each old key has the same value)");
    }
    // 8. the window: every sub-tab fills the page, nothing overlaps, DARK and LIGHT
    {
        juce::String bad;
        for (auto th : { ghosty::themes::Theme::Dark, ghosty::themes::Theme::Light })
        {
            p.theme = th; ed->applyTheme();
            for (int i = 0; i < HoodPage::kTabs; ++i)
            {
                h->showTab (i); ed->resized();
                if (! ed->getLocalBounds().contains (h->getBounds())) bad << " page" << i;
                if (auto* pn = h->panel (i))
                {
                    if (pn->getBounds() != h->getLocalBounds().withTrimmedTop (42)) bad << " fill" << i;
                    const auto cs = pn->cells();
                    for (int a = 0; a < cs.size(); ++a)
                        for (int b = a + 1; b < cs.size(); ++b)
                            if (cs[a]->isVisible() && cs[b]->isVisible() && cs[a]->getParentComponent() == cs[b]->getParentComponent() && cs[a]->getBounds().intersects (cs[b]->getBounds()))
                                bad << " overlap" << i << ":" << cs[a]->name << "/" << cs[b]->name;
                }
                auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
                if (! img.isValid()) bad << " paint" << i;
            }
        }
        p.theme = ghosty::themes::Theme::Dark; ed->applyTheme();
        check (bad.isEmpty(), "every UNDER THE HOOD sub-tab fills the page with no overlapping controls, DARK and LIGHT" + bad);
    }
    ed = nullptr;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    tmp = juce::File::createTempFile ("ghostvoice-tests"); tmp.createDirectory();
    setenv ("GHOSTVOICE_HOME", tmp.getChildFile ("home").getFullPathName().toRawUTF8(), 1);   // never the user's own library
    if (argc >= 8 && (juce::String (argv[1]) == "e2e" || juce::String (argv[1]) == "e2e-quick"))
    {
        juce::StringArray f; for (int i = 3; i < 8; ++i) f.add (juce::File::getCurrentWorkingDirectory().getChildFile (argv[i]).getFullPathName());
        const int r = endToEnd (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]), f, juce::String (argv[1]) == "e2e-quick");
        tmp.deleteRecursively(); return r;
    }
    if (argc >= 4 && juce::String (argv[1]) == "dsp")
    {   // GhostVoiceTests dsp OUTDIR SOURCE --a A1 .. --b B1 ..
        auto cwd = juce::File::getCurrentWorkingDirectory();
        juce::StringArray as, bs; int mode = 0;
        for (int i = 4; i < argc; ++i) { const juce::String a (argv[i]); if (a == "--a") mode = 1; else if (a == "--b") mode = 2; else if (mode == 1) as.add (cwd.getChildFile (a).getFullPathName()); else if (mode == 2) bs.add (cwd.getChildFile (a).getFullPathName()); }
        const int r = renderAbcLayout (cwd.getChildFile (argv[2]), cwd.getChildFile (argv[3]), as, bs);
        tmp.deleteRecursively(); return r;
    }
    if (argc >= 3 && juce::String (argv[1]) == "hand")
    {   // GhostVoiceTests hand OUT.png: the hand-drawn buttons at several sizes (a look check)
        juce::Image img (juce::Image::ARGB, 600, 260, true); juce::Graphics g (img); g.fillAll (gv::splash::palette().window);
        int y = 10;
        for (int h : { 24, 30, 34, 48, 60 }) { gv::splash::handButton (g, gv::splash::palette(), { 10.0f, (float) y, 150.0f, (float) h }, true, true, false, 900 + h);
                                               gv::splash::handButton (g, gv::splash::palette(), { 200.0f, (float) y, 150.0f, (float) h }, false, true, false, 900 + h);
                                               juce::Path p; p.addRoundedRectangle (400.0f, (float) y, 150.0f, (float) h, 14.0f); g.setColour (juce::Colours::red); g.fillPath (gv::splash::wobble (p, 3, 1.0f)); y += h + 6; }
        juce::FileOutputStream os (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2])); os.setPosition (0); os.truncate(); juce::PNGImageFormat().writeImageToStream (img, os);
        return 0;
    }
    juce::File shots;
    if (argc >= 3 && juce::String (argv[1]) == "shots") { shots = juce::File (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2])); shots.createDirectory(); }
    constructionTests();
    windowAndFreshStartTests();
    unitTests();
    dspTests();
    hoodTests();
    engineFlowTests();
    flowTests (shots);
    shotsAndEditor (shots);
    chopTests (shots);
    hoodOriginalsTests();
    tmp.deleteRecursively();
    std::cout << (checks - fails) << " / " << checks << " checks passed" << std::endl;
    return fails;
}
