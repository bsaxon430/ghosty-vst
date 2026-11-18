// ZIPPER test (2026-10-24, "very crackly when dragging the HEAT XY ghost"): a held note while each XY ghost's two parameters
// are dragged at the speed of a fast mouse drag (full range in 300 ms, a new value at every 64-sample block), against the same
// path played quasi-statically (36 points along it, each held until settled = the sweep at ~1/1000 of the speed).
//   HF:   the output high-passed at 6 kHz (4th-order Butterworth): the fast sweep may add at most 1 dB of energy up there
//   JUMP: the largest sample-to-sample step of the fast sweep may exceed the static renders' own largest step by at most 0.05
// Each ghost is swept on X alone, on Y alone and on both (X legs 300 ms, Y legs 450 ms). A second pass sends the values only every
// 768 samples (16 ms: what a real mouse does) and must pass the same bars. The held note is a saw through the low-pass at CUTOFF
// 0.25 (nearly a sine: any zipper stands out) and at 0.6 (bright), and the factory unison stack "Hyper Saw Anthem" as it loads.
// A third bar: the loudest 10 ms of HF in the fast sweep at most 3 dB above the loudest 10 ms of the static renders (a click is short). Included by TrapTests.cpp after CleanTests.inc.
namespace zippertest
{
static constexpr int ZBS = 64;

struct HfMeter
{   // 4th-order Butterworth high-pass at 6 kHz (two biquads, Q 0.5412 / 1.3066), double precision, one per channel
    double b[2][3] {}, a[2][2] {}, z[2][2][2] {};
    HfMeter()
    {
        const double q[2] { 0.54119610, 1.30656296 };
        for (int s = 0; s < 2; ++s)
        {
            const double w = 2 * juce::MathConstants<double>::pi * 6000.0 / SR, c = std::cos (w), al = std::sin (w) / (2 * q[s]), a0 = 1 + al;
            b[s][0] = (1 + c) / 2 / a0; b[s][1] = -(1 + c) / a0; b[s][2] = b[s][0]; a[s][0] = -2 * c / a0; a[s][1] = (1 - al) / a0;
        }
    }
    double tick (double x, int ch) noexcept
    {
        for (int s = 0; s < 2; ++s)
        {
            auto& zz = z[ch][s];
            const double y = b[s][0] * x + zz[0];
            zz[0] = b[s][1] * x - a[s][0] * y + zz[1]; zz[1] = b[s][2] * x - a[s][1] * y;
            x = y;
        }
        return x;
    }
};

struct Meas
{   // hf / cnt: mean HF power; win: the loudest 10 ms window's HF power (a crackle is short: the mean can hide it)
    double hf = 0; long cnt = 0; float jump = 0; double win = 0, winAcc = 0; int winN = 0;
    void add (double h) noexcept { hf += h; ++cnt; winAcc += h; if (++winN == 960) { win = std::max (win, winAcc / 960.0); winAcc = 0; winN = 0; } }
};
// processes one block of the held note and measures it (when 'm' is given); prev = the last sample of each channel
static void block (ProProcessor& p, juce::AudioBuffer<float>& blk, juce::MidiBuffer& mb, int& idx, HfMeter& hm, float prev[2], Meas* m)
{
    blk.setSize (2, ZBS, false, false, true);
    seedSystemRandom (0x2177 + idx++);
    p.processBlock (blk, mb); mb.clear();
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* d = blk.getReadPointer (ch);
        for (int i = 0; i < ZBS; ++i)
        {
            const double h = hm.tick (d[i], ch);
            if (m != nullptr) { m->add (h * h); m->jump = std::max (m->jump, std::abs (d[i] - prev[ch])); }
            prev[ch] = d[i];
        }
    }
}

static double tri (double t, double leg) { const double u = std::fmod (t / leg, 2.0); return u <= 1.0 ? u : 2.0 - u; }

static int presetIndex (const char* name) { const auto& b = gpro::factoryBank(); for (int i = 0; i < (int) b.size(); ++i) if (b[(size_t) i].name == name) return i; return -1; }
struct Ghost { const char* name; const char* x; const char* y; };

static float zipCut = 0.25f; static int zipProgram = -1;   // the held note's CUTOFF (TONE sweeps it itself): 0.25 = nearly a sine, where any zipper shows
static std::unique_ptr<ProProcessor> zipProc (const Ghost& g)
{
    auto p = makeProc (zipProgram);
    p->prepareToPlay (SR, ZBS);
    if (zipProgram < 0)
    {
        p->setParam ("verb", 0.0f); p->setParam ("echo", 0.0f);
        p->setParam ("verb_type", 0.0f);   // SOUND ENGINE V2: the fixture keeps the reverb it was built on (program 0 used to load the classic one).
                                            // The GHOST VERB's slow line modulation makes its HF non-stationary, which a held-vs-swept compare can't
                                            // tell from zipper (it fails even with the mix held); its amount glide is checked alone (gvzip, below)
        if (juce::String (g.name) != "TONE") p->setParam ("cutoff", zipCut);
        if (juce::String (g.name) == "WARP") { p->setParam ("o1_w1", (float) gpro::wBendUp); p->setParam ("o1_w2", (float) gpro::wMirror); }   // round 3: the warp amounts dragged
    }
    settle (*p);
    return p;
}

// mode 0 = X alone (Y at 0.5), 1 = Y alone (X at 0.5), 2 = both; every = blocks between new values (1 = each block)
static bool sweepCase (const Ghost& g, int mode, int every, bool verbose)
{
    constexpr double SWEEP = 1.8, PRE = 0.5;
    auto xAt = [mode] (double t) { return mode == 1 ? 0.5 : tri (t, 0.30); };
    auto yAt = [mode] (double t) { return mode == 0 ? 0.5 : tri (t, 0.45); };
    const bool space = juce::String (g.name) == "SPACE";

    // ---- fast
    Meas fast;
    {
        auto p = zipProc (g);
        auto* px = p->apvts.getParameter (g.x); auto* py = p->apvts.getParameter (g.y);
        px->setValueNotifyingHost ((float) xAt (0)); py->setValueNotifyingHost ((float) yAt (0));
        juce::AudioBuffer<float> blk (2, ZBS); juce::MidiBuffer mb; HfMeter hm; float prev[2] {}; int idx = 0;
        mb.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 0);
        const int pre = (int) (PRE * SR / ZBS), nb = (int) (SWEEP * SR / ZBS);
        for (int k = 0; k < pre; ++k) block (*p, blk, mb, idx, hm, prev, nullptr);
        for (int k = 0; k < nb; ++k)
        {
            if (k % every == 0) { const double t = k * (double) ZBS / SR; px->setValueNotifyingHost ((float) xAt (t)); py->setValueNotifyingHost ((float) yAt (t)); }
            block (*p, blk, mb, idx, hm, prev, &fast);
        }
    }
    // ---- quasi-static: 36 points along the same path, each settled, then measured
    Meas ref;
    {
        auto p = zipProc (g);
        auto* px = p->apvts.getParameter (g.x); auto* py = p->apvts.getParameter (g.y);
        juce::AudioBuffer<float> blk (2, ZBS); juce::MidiBuffer mb; HfMeter hm; float prev[2] {}; int idx = 0;
        mb.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 0);
        px->setValueNotifyingHost ((float) xAt (0)); py->setValueNotifyingHost ((float) yAt (0));
        for (int k = 0; k < (int) (PRE * SR / ZBS); ++k) block (*p, blk, mb, idx, hm, prev, nullptr);
        constexpr int K = 36;
        const int settleB = (int) ((space ? 1.5 : 0.3) * SR / ZBS), measB = (int) (0.05 * SR / ZBS);
        for (int j = 0; j < K; ++j)
        {
            const double t = (j + 0.5) * SWEEP / K;
            px->setValueNotifyingHost ((float) xAt (t)); py->setValueNotifyingHost ((float) yAt (t));
            for (int k = 0; k < settleB; ++k) block (*p, blk, mb, idx, hm, prev, nullptr);
            Meas m; m.jump = 0;
            for (int k = 0; k < measB; ++k) block (*p, blk, mb, idx, hm, prev, &m);
            ref.hf += m.hf; ref.cnt += m.cnt; ref.jump = std::max (ref.jump, m.jump); ref.win = std::max (ref.win, m.win);
        }
    }
    const double pf = fast.hf / std::max (1L, fast.cnt), pr = ref.hf / std::max (1L, ref.cnt);
    const double addDb = 10.0 * std::log10 ((pf + 1e-20) / (pr + 1e-20));
    const double hfDbFs = 10.0 * std::log10 (pf + 1e-20), refDbFs = 10.0 * std::log10 (pr + 1e-20);
    const float extraJump = fast.jump - ref.jump;
    const double winDb = 10.0 * std::log10 ((fast.win + 1e-20) / (ref.win + 1e-20));
    const bool ok = addDb <= 1.0 && extraJump <= 0.05f && winDb <= 3.0;
    static const char* modes[] { "X  ", "Y  ", "X+Y" };
    if (verbose || ! ok)
        printf ("    %s %-5s %s (%s / %s) %s%s: HF > 6 kHz fast %6.1f dBFS vs static %6.1f dBFS = %+5.1f dB, loudest 10 ms %+5.1f dB; largest step %.4f vs %.4f (%+.4f)\n",
                ok ? "  " : "!!", g.name, modes[mode], g.x, g.y, zipProgram >= 0 ? "preset" : zipCut > 0.5f ? "bright" : "dull  ", every > 1 ? " mouse-rate" : "", hfDbFs, refDbFs, addDb, winDb, fast.jump, ref.jump, extraJump);
    fflush (stdout);
    return ok;
}


// MOD (round 3): dragging a modulation AMOUNT (the ring on a knob / the matrix bar) at mouse speed, against the same amounts held.
// The source is held still (MACRO 1 at 100 %, or ENV 2 at its sustain of 100 %), so everything the amount drag adds is zipper.
struct ModCase { const char* name; int src; const char* dst; };
static bool modSweepCase (const ModCase& mc, int every, bool verbose)
{
    constexpr double SWEEP = 1.8, PRE = 0.5;
    auto amtAt = [] (double t) { return (float) (tri (t, 0.30) - 0.5); };          // -50 % .. +50 % and back, 300 ms per leg
    auto setup = [&mc, amtAt]
    {
        auto p = makeProc (-1);
        p->prepareToPlay (SR, ZBS);
        p->setParam ("verb", 0.0f); p->setParam ("echo", 0.0f); p->setParam ("cutoff", juce::String (mc.dst) == "cutoff" ? 0.5f : zipCut);
        p->setParam ("mac1", 1.0f); p->setParam ("e2_a", 0.001f); p->setParam ("e2_s", 1.0f);
        if (juce::String (mc.dst) == "verb") p->setParam ("verb", 0.3f);
        p->setParam ("verb_type", 0.0f);   // (the fixture's classic reverb: see zipProc)
        settle (*p);
        p->modr.add (mc.src, mc.dst, amtAt (0));
        return p;
    };
    Meas fast;
    {
        auto p = setup();
        juce::AudioBuffer<float> blk (2, ZBS); juce::MidiBuffer mb; HfMeter hm; float prev[2] {}; int idx = 0;
        mb.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 0);
        for (int k = 0; k < (int) (PRE * SR / ZBS); ++k) block (*p, blk, mb, idx, hm, prev, nullptr);
        const int nb = (int) (SWEEP * SR / ZBS);
        for (int k = 0; k < nb; ++k)
        {
            if (k % every == 0) p->modr.setAmount (0, amtAt (k * (double) ZBS / SR));
            block (*p, blk, mb, idx, hm, prev, &fast);
        }
    }
    Meas ref;
    {
        auto p = setup();
        juce::AudioBuffer<float> blk (2, ZBS); juce::MidiBuffer mb; HfMeter hm; float prev[2] {}; int idx = 0;
        mb.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 0);
        for (int k = 0; k < (int) (PRE * SR / ZBS); ++k) block (*p, blk, mb, idx, hm, prev, nullptr);
        constexpr int K = 36;
        const bool space = juce::String (mc.dst) == "verb";
        const int settleB = (int) ((space ? 1.5 : 0.3) * SR / ZBS), measB = (int) (0.05 * SR / ZBS);
        for (int j = 0; j < K; ++j)
        {
            p->modr.setAmount (0, amtAt ((j + 0.5) * SWEEP / K));
            for (int k = 0; k < settleB; ++k) block (*p, blk, mb, idx, hm, prev, nullptr);
            Meas m; m.jump = 0;
            for (int k = 0; k < measB; ++k) block (*p, blk, mb, idx, hm, prev, &m);
            ref.hf += m.hf; ref.cnt += m.cnt; ref.jump = std::max (ref.jump, m.jump); ref.win = std::max (ref.win, m.win);
        }
    }
    const double pf = fast.hf / std::max (1L, fast.cnt), pr = ref.hf / std::max (1L, ref.cnt);
    const double addDb = 10.0 * std::log10 ((pf + 1e-20) / (pr + 1e-20)), winDb = 10.0 * std::log10 ((fast.win + 1e-20) / (ref.win + 1e-20));
    const float extraJump = fast.jump - ref.jump;
    const bool ok = addDb <= 1.0 && extraJump <= 0.05f && winDb <= 3.0;
    if (verbose || ! ok)
        printf ("    %s MODAMT %-8s -> %-8s%s: HF > 6 kHz %+5.1f dB vs held amounts, loudest 10 ms %+5.1f dB; largest step %.4f vs %.4f (%+.4f)\n",
                ok ? "  " : "!!", gpro::mod::srcShortNames()[mc.src].toRawUTF8(), mc.dst, every > 1 ? " mouse-rate" : "           ", addDb, winDb, fast.jump, ref.jump, extraJump);
    fflush (stdout);
    return ok;
}

static int zipperTests (const juce::StringArray& only)
{
    section ("ZIPPER: dragging an XY ghost fast adds no crackle (HF > 6 kHz +1 dB at most, no step > 0.05 beyond the waveform's own)");
    const Ghost ghosts[] { { "HEAT", "drive", "sub_lvl" }, { "TONE", "cutoff", "res" }, { "MORPH", "o1_pos", "o1_det" }, { "SPACE", "echo", "verb" }, { "WARP", "o1_w1a", "o1_w2a" } };
    int bad = 0, n = 0;
    for (auto& g : ghosts)
    {
        if (! only.isEmpty() && ! only.contains (g.name)) continue;
        for (int src = 0; src < 3; ++src)
            for (int every : { 1, 12 })
                for (int mode = 0; mode < 3; ++mode)
                {
                    zipCut = src == 1 ? 0.6f : 0.25f; zipProgram = src == 2 ? presetIndex ("Hyper Saw Anthem") : -1;   // (a unison stack with echo + reverb)
                    ++n; if (! sweepCase (g, mode, every, true)) ++bad;
                }
    }
    CHECK (bad == 0, "XY ghost drags without zipper / crackle (%d of %d sweeps failed)", bad, n);
    // MOD (round 3): a modulation amount dragged at mouse speed (voice targets: CUTOFF, OSC 1 POS, FILTER DRIVE; global: REVERB, DRIVE)
    if (only.isEmpty() || only.contains ("MODAMT"))
    {
        const ModCase mcs[] { { "cut", gpro::mod::mac1, "cutoff" }, { "pos", gpro::mod::env2, "o1_pos" }, { "fdrv", gpro::mod::mac1, "f_drive" }, { "verb", gpro::mod::mac1, "verb" }, { "drv", gpro::mod::env2, "drive" } };
        int mbad = 0, mn = 0;
        for (auto& mc : mcs) for (int every : { 1, 12 }) { ++mn; if (! modSweepCase (mc, every, true)) ++mbad; }
        CHECK (mbad == 0, "modulation AMOUNT drags without zipper / crackle (%d of %d sweeps failed)", mbad, mn);
    }
    printf ("\n%d failures\n", failures);
    return failures;
}
} // namespace zippertest
