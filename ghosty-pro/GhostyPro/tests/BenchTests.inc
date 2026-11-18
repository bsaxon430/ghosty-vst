// ProTests bench: CPU as % of real time (one core) at 48 kHz / 128-sample blocks, measured on the processor alone (no wrapper,
// no editor). Only processBlock is timed; patch loading and wavetable building happen before the clock starts.
//   ProTests bench                    -> every scenario + the heaviest factory presets, markdown table to stdout and to bench.md
//   ProTests bench --check            -> the same, then fail (exit 1) if INIT 1 voice > 2.5 % or 16 x 8 x 2 Ultra > 100 %
//                                        (each the quietest = lowest of its 3 runs; limits sit ~1.5x / 1.15x above the quiet-machine numbers)
//   ProTests bench --out <file>       -> where the markdown goes (default: bench.md in the working directory)
//   ProTests bench --quick            -> skip the factory preset sweep
//   ProTests bench --only <scenario> [--secs N]   -> loop one scenario for N seconds of audio (for perf record); scenarios:
//                                        init1, init8, big, big-draft, big-high, big-ultra, verbhyper, preset:<name>
// Per-stage cost: the processor has no per-stage timers (GPRO_TAPS only measures levels), so this mode reports totals; the
// hot spots per stage come from `perf record` on `bench --only ...` (see the notes written under the table).
#include <chrono>

namespace bench
{
static constexpr int kBlock = 128;
static constexpr double kMaxInit1 = 2.5, kMaxUltra = 100.0;  // bench --check limits (% of real time, quietest run; Ultra 75 -> 100 with the sound audit's full-band fine mips: 65 -> 87 %)

struct Result { juce::String name, note; double pct = 0, median = 0; int voices = 0, uniVoices = 0; int quality = -1; bool finite = true; };

static int qualityOf (ProProcessor& p) { auto* q = p.apvts.getRawParameterValue ("quality"); return q != nullptr ? (int) q->load() : -1; }
static const char* qualityName (int q) { return q == 0 ? "Draft" : q == 1 ? "High" : q == 2 ? "Ultra" : "?"; }

static std::unique_ptr<ProProcessor> fresh (int program)
{
    seedSystemRandom (777);
    auto p = std::make_unique<ProProcessor>();
    p->setNonRealtime (false);
    p->prepareToPlay (SR, kBlock);
    if (program >= 0) p->setCurrentProgram (program); else p->initPatch();
    settle (*p);
    return p;
}

// Hold `notes` from the first block; warm up `warm` seconds untimed, then time `secs` seconds of 128-sample blocks.
static Result run (ProProcessor& p, const std::vector<int>& notes, double secs, double warm = 0.25)
{
    struct Head : juce::AudioPlayHead
    {
        double pos = 0;
        juce::Optional<PositionInfo> getPosition() const override { PositionInfo i; i.setBpm (120.0); i.setIsPlaying (true); i.setPpqPosition (pos); i.setTimeSignature (TimeSignature { 4, 4 }); return i; }
    } head;
    p.setPlayHead (&head);
    juce::AudioBuffer<float> blk (2, kBlock);
    Result r; r.quality = qualityOf (p);
    const int warmBlocks = (int) std::ceil (warm * SR / kBlock), timedBlocks = (int) std::ceil (secs * SR / kBlock);
    double ns = 0; long long blockNo = 0;
    for (int b = 0; b < warmBlocks + timedBlocks; ++b)
    {
        juce::MidiBuffer mb;
        if (b == 0) for (int n : notes) mb.addEvent (juce::MidiMessage::noteOn (1, n, 0.85f), 0);
        blk.clear();
        head.pos = (double) blockNo++ * kBlock / SR * 2.0;
        const auto t0 = std::chrono::steady_clock::now();
        p.processBlock (blk, mb);
        const auto t1 = std::chrono::steady_clock::now();
        if (b >= warmBlocks)
        {
            ns += (double) std::chrono::duration_cast<std::chrono::nanoseconds> (t1 - t0).count();
            for (int ch = 0; ch < 2; ++ch) { auto* d = blk.getReadPointer (ch); for (int i = 0; i < kBlock; ++i) if (! std::isfinite (d[i])) r.finite = false; }
        }
        if (b == warmBlocks + timedBlocks / 2) { r.voices = p.voicesLive.load(); r.uniVoices = p.uniVoicesLive.load(); }
    }
    p.setPlayHead (nullptr);
    r.pct = ns * 1e-9 / ((double) timedBlocks * kBlock / SR) * 100.0;
    return r;
}

// best (lowest) of `reps` runs, each on a freshly made processor; the median is kept too. The lowest run is the least disturbed
// by other processes on a shared machine, so it is what the table and --check use.
template <typename Make>
static Result measure (const juce::String& name, Make make, const std::vector<int>& notes, double secs, int reps, const juce::String& note = {})
{
    std::vector<Result> rs;
    for (int k = 0; k < reps; ++k) { auto p = make(); rs.push_back (run (*p, notes, secs)); }
    std::sort (rs.begin(), rs.end(), [] (const Result& a, const Result& b) { return a.pct < b.pct; });
    Result r = rs.front(); r.median = rs[rs.size() / 2].pct; r.name = name; r.note = note;
    for (auto& x : rs) r.finite = r.finite && x.finite;
    return r;
}

static const std::vector<int> kOne { 60 };
static const std::vector<int> kChord4 { 48, 55, 60, 64 };
static const std::vector<int> kChord6 { 48, 52, 55, 59, 62, 65 };
static std::vector<int> sixteen() { std::vector<int> v; for (int i = 0; i < 16; ++i) v.push_back (36 + i * 3); return v; }   // C2 .. A5, minor thirds

static std::unique_ptr<ProProcessor> bigPatch (int quality)
{
    auto p = fresh (-1);
    cleantest::set (*p, { { "o1_uni", 8.0f }, { "o2_on", 1.0f }, { "o2_uni", 8.0f }, { "poly", 16.0f }, { "mono", 0.0f } });
    if (quality >= 0) p->setParam ("quality", (float) quality);
    settle (*p);
    return p;
}
static std::unique_ptr<ProProcessor> verbHyperPatch()
{
    auto p = fresh (-1);
    cleantest::set (*p, { { "verb_type", 1.0f }, { "verb", 0.6f }, { "fx_hyper_on", 1.0f }, { "fx_hyper_voices", 7.0f }, { "fx_hyper_mode", 0.0f }, { "fx_hyper_mix", 1.0f } });
    settle (*p);
    return p;
}

static juce::String cpuModel()
{
    juce::String m;
   #if JUCE_LINUX
    juce::StringArray lines; lines.addLines (juce::File ("/proc/cpuinfo").loadFileAsString());
    for (auto& l : lines) if (l.startsWith ("model name")) { m = l.fromFirstOccurrenceOf (":", false, false).trim(); break; }
   #endif
    if (m.isEmpty()) m = juce::SystemStats::getCpuModel();
    return m + " (" + juce::String (juce::SystemStats::getNumCpus()) + " logical cores, " + juce::String (juce::SystemStats::getCpuSpeedInMegahertz()) + " MHz reported)";
}

static const char* buildKind()
{
   #if JUCE_DEBUG
    return "Debug";
   #else
    return "Release";
   #endif
}

static juce::String row (const Result& r)
{
    return "| " + r.name + " | " + juce::String (r.pct, 2) + " % | " + juce::String (r.median, 2) + " % | " + qualityName (r.quality) + " | " + juce::String (r.voices) + " / " + juce::String (r.uniVoices)
         + " | " + (r.finite ? "ok" : "NaN!") + " | " + r.note + " |";
}

static int run (const juce::StringArray& args)
{
    const bool check = args.contains ("--check"), quick = args.contains ("--quick");
    auto argAfter = [&] (const char* flag) { const int i = args.indexOf (flag); return i >= 0 && i + 1 < args.size() ? args[i + 1] : juce::String(); };
    const juce::File out = argAfter ("--out").isNotEmpty() ? juce::File::getCurrentWorkingDirectory().getChildFile (argAfter ("--out"))
                                                           : juce::File::getCurrentWorkingDirectory().getChildFile ("bench.md");
    const double secs = 5.0; const int reps = 3;

    // ---- one scenario in a loop (perf record)
    if (const auto only = argAfter ("--only"); only.isNotEmpty())
    {
        const double s = argAfter ("--secs").isNotEmpty() ? argAfter ("--secs").getDoubleValue() : 20.0;
        std::unique_ptr<ProProcessor> p; std::vector<int> notes = kOne;
        if (only == "init1") p = fresh (-1);
        else if (only == "init8") { p = fresh (-1); p->setParam ("o1_uni", 8.0f); settle (*p); }
        else if (only == "big") { p = bigPatch (-1); notes = sixteen(); }
        else if (only == "big-draft") { p = bigPatch (0); notes = sixteen(); }
        else if (only == "big-high") { p = bigPatch (1); notes = sixteen(); }
        else if (only == "big-ultra") { p = bigPatch (2); notes = sixteen(); }
        else if (only == "verbhyper") { p = verbHyperPatch(); notes = kChord4; }
        else if (only.startsWith ("preset:")) { const int prog = progOf (only.fromFirstOccurrenceOf (":", false, false)); if (prog < 0) { printf ("no preset %s\n", only.toRawUTF8()); return 2; } p = fresh (prog); notes = kChord6; }
        else { printf ("unknown scenario %s\n", only.toRawUTF8()); return 2; }
        const auto r = run (*p, notes, s);
        printf ("%s: %.2f %% of real time (%s, %d voices / %d unison voices)\n", only.toRawUTF8(), r.pct, qualityName (r.quality), r.voices, r.uniVoices);
        return 0;
    }

    std::vector<Result> rows;
    auto add = [&] (Result r) { printf ("  %-44s %7.2f %% (median %6.2f %%)  %-5s voices %d / %d%s\n", r.name.toRawUTF8(), r.pct, r.median, qualityName (r.quality), r.voices, r.uniVoices, r.finite ? "" : "  NaN!"); fflush (stdout); rows.push_back (r); return r; };

    printf ("bench: %s, 48 kHz, %d-sample blocks, best / median of %d x %.0f s per scenario\n", cpuModel().toRawUTF8(), kBlock, reps, secs);
    const auto init1 = add (measure ("INIT, 1 voice (C4)", [] { return fresh (-1); }, kOne, secs, reps, "INIT patch as loaded"));
    add (measure ("INIT, 8 unison (C4)", [] { auto p = fresh (-1); p->setParam ("o1_uni", 8.0f); settle (*p); return p; }, kOne, secs, reps, "o1_uni 8"));
    add (measure ("16 notes x 8 unison x 2 osc (INIT quality)", [] { return bigPatch (-1); }, sixteen(), secs, reps, "o1+o2 on, uni 8 each = 256 osc voices"));
    Result ultra;
    for (int q = 0; q < 3; ++q)
    {
        auto r = add (measure (juce::String ("16 x 8 x 2, Quality ") + qualityName (q), [q] { return bigPatch (q); }, sixteen(), secs, reps, juce::String (q == 0 ? "1x" : q == 1 ? "2x" : "4x") + " engine oversampling"));
        if (q == 2) ultra = r;
    }
    add (measure ("4-note chord, GHOST VERB + HYPER 7", [] { return verbHyperPatch(); }, kChord4, secs, reps, "INIT, verb_type Ghost, verb 60 %, HYPER 7 voices mix 100 %"));

    // ---- factory presets: screen all with a 6-note chord (1 x 1.5 s), re-measure the 10 heaviest (best of 3 x 5 s), report the top 5
    std::vector<Result> presetRows;
    if (! quick)
    {
        const auto& bank = gpro::factoryBank();
        std::vector<std::pair<double, int>> screen;
        printf ("  screening %d factory presets ...\n", (int) bank.size()); fflush (stdout);
        for (int i = 0; i < (int) bank.size(); ++i) { auto p = fresh (i); screen.push_back ({ run (*p, kChord6, 1.5).pct, i }); }
        std::sort (screen.begin(), screen.end(), [] (auto& a, auto& b) { return a.first > b.first; });
        std::vector<Result> top;
        for (int k = 0; k < juce::jmin (10, (int) screen.size()); ++k)
        {
            const int i = screen[(size_t) k].second;
            top.push_back (measure (bank[(size_t) i].name + " (" + bank[(size_t) i].cat + ")", [i] { return fresh (i); }, kChord6, secs, reps, "6-note chord"));
        }
        std::sort (top.begin(), top.end(), [] (const Result& a, const Result& b) { return a.pct > b.pct; });
        for (int k = 0; k < juce::jmin (5, (int) top.size()); ++k) presetRows.push_back (add (top[(size_t) k]));
        double sum = 0; for (auto& s : screen) sum += s.first;
        printf ("  all %d presets (screen, 6-note chord): mean %.2f %%, lightest %.2f %%, heaviest %.2f %%\n", (int) screen.size(), sum / std::max<size_t> (1, screen.size()), screen.back().first, screen.front().first);
    }

    juce::String md;
    md << "# Ghosty Pro CPU bench\n\n"
       << "Machine: " << cpuModel() << "  \n"
       << "48 kHz, " << kBlock << "-sample blocks, processor only (ProTests, no plugin wrapper / editor), one core; best and median of " << reps << " x " << (int) secs << " s per row. "
       << "Build: " << buildKind() << ", " << juce::SystemStats::getJUCEVersion() << ".  \n"
       << "CPU % = processBlock time / audio time. Voices = synth voices / unison voices playing (mid-render).\n\n"
       << "| Scenario | CPU, best (% of real time) | median | Quality | Voices | Output | Notes |\n|---|---:|---:|---|---|---|---|\n";
    for (auto& r : rows) md << row (r) << "\n";
    md << "\nThresholds (`bench --check`, quietest of 3 runs): INIT 1 voice <= " << kMaxInit1 << " % (now " << juce::String (init1.pct, 2) << " %), 16 x 8 x 2 Ultra <= " << kMaxUltra << " % (now " << juce::String (ultra.pct, 2) << " %).\n";
    md << "Load average while measuring: " << juce::File ("/proc/loadavg").loadFileAsString().upToFirstOccurrenceOf (" ", false, false).trim()
       << " (1 min; the machine may be shared - rerun on a quiet machine before trusting a small difference).\n";
    out.replaceWithText (md);
    printf ("\n%s\nwrote %s\n", md.toRawUTF8(), out.getFullPathName().toRawUTF8());

    int bad = 0;
    for (auto& r : rows) if (! r.finite) { printf ("FAIL  %s: non-finite output\n", r.name.toRawUTF8()); ++bad; }
    if (check)
    {
        CHECK (init1.pct <= kMaxInit1, "bench --check: INIT 1 voice %.2f %% <= %.1f %%", init1.pct, kMaxInit1);
        CHECK (ultra.pct <= kMaxUltra, "bench --check: 16 x 8 x 2 Ultra %.2f %% <= %.0f %%", ultra.pct, kMaxUltra);
        if (init1.pct > kMaxInit1) ++bad;
        if (ultra.pct > kMaxUltra) ++bad;
    }
    return bad > 0 ? 1 : 0;
}
} // namespace bench
