// CLEAN SOUND tests (2026-10-23): 'soundaudit' (numbers on stress patches) and 'clean' (the pass / fail checks).
// Included by TrapTests.cpp after TrapTestsNew.inc (uses its helpers: makeProc, render, settle, progOf, CHECK ...).

namespace cleantest
{
// 4x true peak (BS.1770-style: windowed-sinc interpolation, 48 taps per phase)
static double truePeakDb (const juce::AudioBuffer<float>& b, int start = 0, int end = -1)
{
    if (end < 0) end = b.getNumSamples();
    constexpr int OS = 4, HALF = 24;
    static float k[OS][2 * HALF];
    static bool made = false;
    if (! made)
    {
        for (int ph = 0; ph < OS; ++ph)
            for (int j = 0; j < 2 * HALF; ++j)
            {
                const double t = (double) (j - HALF + 1) - (double) ph / OS;      // distance from the interpolated point
                const double x = juce::MathConstants<double>::pi * t;
                const double sinc = std::abs (t) < 1e-9 ? 1.0 : std::sin (x) / x;
                const double w = 0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * t / (HALF + 0.5));
                k[ph][j] = (float) (sinc * w);
            }
        made = true;
    }
    float pk = 0;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
    {
        const float* d = b.getReadPointer (ch);
        for (int i = start; i < end; ++i)
        {
            pk = std::max (pk, std::abs (d[i]));
            for (int ph = 1; ph < OS; ++ph)
            {
                double acc = 0;
                for (int j = 0; j < 2 * HALF; ++j) { const int idx = i + j - HALF + 1; if (idx >= 0 && idx < b.getNumSamples()) acc += (double) d[idx] * k[ph][j]; }
                pk = std::max (pk, (float) std::abs (acc));
            }
        }
    }
    return juce::Decibels::gainToDecibels (pk, -200.0f);
}
static double peakDb (const juce::AudioBuffer<float>& b) { return juce::Decibels::gainToDecibels (peakOf (b), -200.0f); }

// energy (dB re full scale) in [lo, hi] Hz of the mono sum, and the share of the whole spectrum (dB)
struct Bands { double mudAbs = -200, mudShare = -200, lowAbs = -200, total = -200; };
static Bands bands (const juce::AudioBuffer<float>& x, int start = 0, int end = -1)
{
    if (end < 0) end = x.getNumSamples();
    constexpr int ORDER = 13, N = 1 << ORDER;
    juce::dsp::FFT fft (ORDER);
    std::vector<double> acc ((size_t) N / 2, 0.0); std::vector<float> buf ((size_t) N * 2);
    int frames = 0;
    for (int s = start; s + N <= end; s += N / 2, ++frames)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < N; ++i) buf[(size_t) i] = 0.5f * (x.getSample (0, s + i) + x.getSample (1, s + i)) * (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / N));
        fft.performFrequencyOnlyForwardTransform (buf.data());
        for (int k = 0; k < N / 2; ++k) acc[(size_t) k] += (double) buf[(size_t) k] * buf[(size_t) k];
    }
    Bands r; if (frames == 0) return r;
    double mud = 0, low = 0, tot = 0;
    for (int k = 1; k < N / 2; ++k)
    {
        const double f = k * SR / N, e = acc[(size_t) k] / frames;
        tot += e; if (f >= 150 && f <= 400) mud += e; if (f < 120) low += e;
    }
    const double norm = (double) N * N * 0.375 / 4.0;      // Hann power, re a full-scale sine ~0 dB
    r.mudAbs = 10 * std::log10 (std::max (1e-30, mud / norm)); r.lowAbs = 10 * std::log10 (std::max (1e-30, low / norm));
    r.total = 10 * std::log10 (std::max (1e-30, tot / norm)); r.mudShare = r.mudAbs - r.total;
    return r;
}
// correlation of L and R below 120 Hz (4th-order low pass)
static double lowCorr (const juce::AudioBuffer<float>& b, double fc = 120.0, int start = 0, int end = -1)
{
    if (end < 0) end = b.getNumSamples();
    auto c = juce::dsp::IIR::Coefficients<float>::makeLowPass (SR, (float) fc, 0.7071f);
    juce::dsp::IIR::Filter<float> f[2][2];
    for (auto& a : f) for (auto& q : a) q.coefficients = c;
    double lr = 0, ll = 0, rr = 0;
    for (int i = start; i < end; ++i)
    {
        const float l = f[0][1].processSample (f[0][0].processSample (b.getSample (0, i)));
        const float r = f[1][1].processSample (f[1][0].processSample (b.getSample (1, i)));
        lr += (double) l * r; ll += (double) l * l; rr += (double) r * r;
    }
    return ll > 1e-12 && rr > 1e-12 ? lr / std::sqrt (ll * rr) : 1.0;
}
// stereo centroid: side / mid balance as a signed pan (-1 left .. +1 right) by energy
static double centroid (const juce::AudioBuffer<float>& b)
{
    double el = 0, er = 0;
    for (int i = 0; i < b.getNumSamples(); ++i) { el += (double) b.getSample (0, i) * b.getSample (0, i); er += (double) b.getSample (1, i) * b.getSample (1, i); }
    return (er - el) / std::max (1e-20, er + el);
}
static double sideRatioDb (const juce::AudioBuffer<float>& b)
{
    double m = 0, s = 0;
    for (int i = 0; i < b.getNumSamples(); ++i) { const double l = b.getSample (0, i), r = b.getSample (1, i); m += (l + r) * (l + r); s += (l - r) * (l - r); }
    return 10 * std::log10 (std::max (1e-20, s) / std::max (1e-20, m));
}

static std::vector<Ev> chordPhrase()
{
    std::vector<Ev> e;
    int i = 0;
    for (int root : { 36, 48, 60, 72 })
    {
        for (int iv : { 0, 4, 7, 11 }) { e.push_back ({ i * 1.0, root + iv, 0.9f, true }); e.push_back ({ i * 1.0 + 0.9, root + iv, 0, false }); }
        ++i;
    }
    for (int root : { 36, 48, 60, 72 }) for (int iv : { 0, 4, 7, 11 }) { e.push_back ({ 4.0, root + iv, 0.9f, true }); e.push_back ({ 5.0, root + iv, 0, false }); }   // all 16 at once
    return e;
}
static std::vector<Ev> singleNote (int note, double len) { return { { 0.0, note, 0.85f, true }, { len, note, 0.0f, false } }; }

static void set (ProProcessor& p, std::initializer_list<std::pair<const char*, float>> kv) { for (auto& [k, v] : kv) p.setParam (k, v); }

// the stress patches (on top of a loaded program, or INIT when program < 0)
enum Stress { sFactoryStack, sLayers3Uni8, sLayersFx100, sCrunchMax, sMorphStacks, sBrassStack, numStress };
static const char* stressName (int s)
{
    static const char* n[] { "Trap Starter Stack (factory)", "3 layers + unison 8 x2", "3 layers + uni 8 + FX 100%", "3 layers + uni 8 + CHAR/FATTEN max", "MORPH: two stacks", "BRASS ON stack + tape + uni 8" };
    return n[s];
}
static std::unique_ptr<ProProcessor> stressProc (int s, int clean /* -1 = leave as loaded */)
{
    std::unique_ptr<ProProcessor> p;
    if (s == sFactoryStack) p = makeProc (progOf ("Trap Starter Stack"));
    else if (s == sBrassStack)
    {   // the TRAP page with BRASS ON (scoop, growl, horn body, fall), a brass stack, unison 8, CHARACTER, TAPE and reverb
        p = makeProc (progOf ("Fanfare Stack"));
        set (*p, { { "br_scoop", 0.8f }, { "br_growl", 0.6f }, { "br_form", 0.9f }, { "br_fall", 0.3f }, { "o1_uni", 8 }, { "o1_det", 0.3f }, { "ch_amt", 0.6f },
                   { "tp_on", 1 }, { "tp_amt", 0.6f }, { "verb", 0.5f }, { "ly2_lvl", 1.0f }, { "ly3_lvl", 0.8f } });
        settle (*p);
    }
    else if (s == sMorphStacks)
    {
        p = makeProc (0);
        p->morphAddGhost (0.3f, 0.5f, ProProcessor::factorySound (progOf ("Trap Starter Stack")), "Trap Starter Stack");
        p->morphAddGhost (0.7f, 0.5f, ProProcessor::factorySound (progOf ("Festival Saw Stack")), "Festival Saw Stack");
        p->morph.enabled.store (true); p->morphChanged (false);
        p->setParam ("morph_x", 0.45f); p->setParam ("morph_y", 0.5f);
        juce::AudioBuffer<float> b (2, BS); for (int k = 0; k < 3; ++k) { juce::MidiBuffer m; b.clear(); p->processBlock (b, m); }
        p->morphTick(); settle (*p);
    }
    else
    {
        p = makeProc (-1); p->initPatch(); settle (*p);
        set (*p, { { "o1_uni", 8 }, { "o1_det", 0.35f }, { "o1_spr", 0.8f }, { "o2_on", 1 }, { "o2_uni", 8 }, { "o2_det", 0.3f }, { "o2_spr", 0.8f }, { "o2_lvl", 0.8f }, { "sub_lvl", 0.4f },
                   { "f_dlaw", 0 }, { "uni_law", 0 }, { "nz_type", 0 }, { "verb_type", 0 }, { "quality", 0 } });   // (the round-1 stress patches on the round-1 laws; the round-2 laws have their own tests: drive / unison / noise / verb)
        p->setLayerSound (1, "Hyper Saw Anthem", ProProcessor::engineClassic); p->setLayerSound (2, "Velvet Supersaw", ProProcessor::engineClassic);   // (their layers too: SOUND ENGINE CLASSIC)
        set (*p, { { "ly2_lvl", 1.0f }, { "ly3_lvl", 1.0f } });
        if (s == sLayersFx100) set (*p, { { "drive", 1 }, { "crush", 1 }, { "chorus", 1 }, { "echo", 1 }, { "verb", 1 }, { "haunt", 1 } });
        if (s == sCrunchMax) set (*p, { { "ch_amt", 1 }, { "fx_fatten_on", 1 }, { "fx_fatten_fat", 1 }, { "fx_fatten_crunch", 1 }, { "fx_fatten_drive", 24 }, { "f_drive", 1 } });
        settle (*p);
    }
    if (clean >= 0) p->setParam ("cl_on", (float) clean);
    return p;
}

struct Metrics { double tp, pk, rms, crest, lufsI, mudShare, mudAbs, lowAbs, corr; };
static Metrics measure (const juce::AudioBuffer<float>& b)
{
    Metrics m {};
    m.tp = truePeakDb (b); m.pk = peakDb (b); m.rms = rmsDb (b); m.crest = m.pk - m.rms; m.lufsI = lufs (b);
    const auto bd = bands (b); m.mudShare = bd.mudShare; m.mudAbs = bd.mudAbs; m.lowAbs = bd.lowAbs; m.corr = lowCorr (b);
    return m;
}
static void printMetrics (const char* label, const Metrics& m)
{
    printf ("  %-38s TP %+6.2f dBTP  peak %+6.2f  RMS %6.1f  crest %5.1f  LUFS %6.1f  mud(150-400) %6.1f dB (share %+5.1f)  low<120 %6.1f  lowCorr %5.2f\n",
            label, m.tp, m.pk, m.rms, m.crest, m.lufsI, m.mudAbs, m.mudShare, m.lowAbs, m.corr);
}

int soundAudit()
{
    printf ("\n=== SOUND AUDIT: stress patches (4-note chords C2..C5, then all 16 at once) ===\n");
    for (int s = 0; s < numStress; ++s)
    {
        for (int cl = 0; cl < 2; ++cl)
        {
            auto p = stressProc (s, cl);
#if GPRO_TAPS
            p->resetTaps();
            for (int k = 1; k < gpro::NUM_LAYERS; ++k) if (auto* c = p->layerChild (k)) c->resetTaps();
#endif
            seedSystemRandom (99);
            auto out = render (*p, chordPhrase(), 5.6);
            char lab[128]; snprintf (lab, sizeof lab, "%s CLEAN %s", stressName (s), cl ? "ON " : "OFF");
            printMetrics (lab, measure (out));
#if GPRO_TAPS
            static const char* tn[] { "layer1", "+layers", "char", "fxchain", "rack", "tape", "gain", "out" };
            printf ("      stage peaks (dBFS):");
            for (int t = 0; t < ProProcessor::numTaps; ++t) printf ("  %s %+5.1f", tn[t], juce::Decibels::gainToDecibels (p->tapPk[t], -120.0f));
            printf ("\n      stage RMS   (dBFS):");
            for (int t = 0; t < ProProcessor::numTaps; ++t) printf ("  %s %+5.1f", tn[t], p->tapRmsDb (t));
            printf ("\n");
            for (int k = 1; k < gpro::NUM_LAYERS; ++k)
                if (auto* c = p->layerChild (k); c != nullptr && c->tapPk[ProProcessor::tapGain] > 0)
                    printf ("      layer %d child: pre-clip %+5.1f dBFS, out %+5.1f dBFS\n", k + 1, juce::Decibels::gainToDecibels (c->tapPk[ProProcessor::tapGain], -120.0f), juce::Decibels::gainToDecibels (c->tapPk[ProProcessor::tapOut], -120.0f));
#endif
            if (! p->apvts.getParameter ("cl_on")) break;   // the baseline build has no CLEAN switch
        }
    }
    return 0;
}
int fxDebug()
{
    for (auto* id : { "drive", "crush", "chorus", "echo", "verb", "haunt", "phaser" })
        for (int cl = 0; cl < 2; ++cl)
        {
            auto p = stressProc (sLayers3Uni8, cl);
            p->setParam (id, 1.0f); settle (*p);
            seedSystemRandom (99);
            auto out = render (*p, chordPhrase(), 5.6);
            char lab[64]; snprintf (lab, sizeof lab, "%s CLEAN %d", id, cl);
            printMetrics (lab, measure (out));
        }
    return 0;
}
// which CLEAN part changes a preset's loudness / sound (each part switched off alone; LUFS vs CLEAN off and vs the full CLEAN)
int attribution (const juce::StringArray& names)
{
    static const char* part[] { "voice law", "osc law", "drive match", "CHAR match", "synth FX", "rack match", "tape match", "sum pockets", "layer pockets", "unison" };
    for (auto& nm : names)
    {
        auto run = [&] (int on, int mask) { auto p = makeProc (progOf (nm)); p->setParam ("cl_on", (float) on); p->setParam ("out_trim", gpro::presetTrimDb (nm)); p->cleanOff = mask; settle (*p); return seededRender (*p); };
        auto off = run (0, 0), full = run (1, 0);
        printf ("%-24s CLEAN off %6.1f LUFS (TP %+5.2f), CLEAN on %6.1f LUFS (TP %+5.2f)  |", nm.toRawUTF8(), lufs (off), truePeakDb (off), lufs (full), truePeakDb (full));
        for (int b = 0; b < 10; ++b) { auto x = run (1, 1 << b); printf (" %s %+.1f", part[b], lufs (x) - lufs (full)); }
        printf ("\n"); fflush (stdout);
    }
    return 0;
}
// every continuous knob at its minimum and maximum on three presets: true peak, NaN / denormals, loudness jumps
int soundSweep (const juce::StringArray& only)
{
    section ("SOUND SWEEP: every continuous knob at min / max on INIT, Trap Starter Stack and Festival Saw Stack");
    const std::vector<Ev> phrase { { 0.0, 48, 0.85f, true }, { 0.0, 60, 0.85f, true }, { 0.0, 64, 0.85f, true }, { 0.0, 67, 0.85f, true }, { 1.0, 48, 0, false }, { 1.0, 60, 0, false }, { 1.0, 64, 0, false }, { 1.0, 67, 0, false },
                                   { 1.1, 36, 0.9f, true }, { 1.5, 36, 0, false } };
    auto isLevel = [] (const juce::String& id)
    {
        for (auto* k : { "lvl", "gain", "out", "trim", "mix", "amt", "vel", "verb", "echo", "haunt", "chorus", "crush", "drive", "sustain", "res", "fenv", "cutoff", "mac", "depth", "wet", "dry", "level", "thresh", "ceiling", "fat", "f_s", "e2_", "e3_", "env", "makeup", "boost", "smp_", "attack", "decay", "release", "_dly", "glide", "lfo", "_a", "_d", "_r" })
            if (id.contains (k)) return true;
        return false;
    };
    int outliers = 0, checked = 0; juce::String worstTpId; double worstTp = -100, worstJump = 0; juce::String worstJumpId;
    for (int pr = 0; pr < 3; ++pr)
    {
        auto fresh = [pr] { std::unique_ptr<ProProcessor> p; if (pr == 0) { p = makeProc (-1); p->initPatch(); settle (*p); } else p = makeProc (progOf (pr == 1 ? "Trap Starter Stack" : "Festival Saw Stack")); return p; };
        const char* pname = pr == 0 ? "INIT" : pr == 1 ? "Trap Starter Stack" : "Festival Saw Stack";
        auto base = fresh(); seedSystemRandom (21); const double L0 = lufs (render (*base, phrase, 2.0));
        juce::StringArray ids;
        for (auto* prm : base->getParameters())
            if (auto* fp = dynamic_cast<juce::AudioParameterFloat*> (prm))
            {
                const auto id = fp->getParameterID();
                if (id == "morph_x" || id == "morph_y" || id == "bypass") continue;
                if (only.size() > 0 && ! only.contains (id)) continue;
                ids.add (id);
            }
        auto p = fresh();
        auto snap = p->captureSound();
        for (auto& id : ids)
            for (int end = 0; end < 2; ++end)
            {
                p->restoreSound (snap, "sweep"); settle (*p);
                auto* fp = dynamic_cast<juce::AudioParameterFloat*> (p->apvts.getParameter (id));
                p->setParam (id, end ? fp->range.end : fp->range.start); settle (*p);
                seedSystemRandom (21);
                auto out = render (*p, phrase, 2.0);
                ++checked;
                int nans = 0; peakOf (out, &nans);
                int denorm = 0; for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < out.getNumSamples(); ++i) { const float v = std::abs (out.getSample (ch, i)); if (v > 0.0f && v < std::numeric_limits<float>::min()) ++denorm; }   // (true denormals)
                const double tp = truePeakDb (out), L = lufs (out), jump = L - L0;
                if (tp > worstTp) { worstTp = tp; worstTpId = juce::String (pname) + " " + id + (end ? " max" : " min"); }
                const bool lvl = isLevel (id);
                if (! lvl && std::abs (jump) > std::abs (worstJump) && L > -70) { worstJump = jump; worstJumpId = juce::String (pname) + " " + id + (end ? " max" : " min"); }
                if (tp > -0.3 || nans > 0 || denorm > 0 || (! lvl && std::abs (jump) > 6.0 && L > -70 && L0 > -70))
                {
                    ++outliers;
                    printf ("  OUTLIER %-20s %-18s %s: true peak %+.2f dBTP, LUFS %.1f (%+.1f dB vs the preset), NaN %d, denormals %d\n", pname, id.toRawUTF8(), end ? "max" : "min", tp, L, jump, nans, denorm);
                    fflush (stdout);
                }
            }
    }
    CHECK (worstTp <= -0.3, "%d knob ends rendered: worst true peak %+.2f dBTP (%s)", checked, worstTp, worstTpId.toRawUTF8());
    printf ("  largest loudness move of a non-level knob: %+.1f dB (%s); %d outliers listed above\n", worstJump, worstJumpId.toRawUTF8(), outliers);
    printf ("\n%d failures\n", failures);
    return failures;
}
// the CLEAN-ON factory sounds get a new TRIM so they play as loud as before (LUFS of the old build's render): presets only set knobs
int cleanTrims (const juce::File& refDir, const juce::String& outInc)
{
    const auto& bank = gpro::factoryBank();
    juce::StringArray lines;
    for (int i = 0; i < (int) bank.size(); ++i)
    {
        const auto& f = bank[(size_t) i];
        if (! ProProcessor::factoryCleanOn (f.name)) continue;
        auto ref = readRaw (refDir.getChildFile (safeName (f.name) + ".f32"));
        if (ref.getNumSamples() == 0) { printf ("no reference for %s\n", f.name.toRawUTF8()); continue; }
        auto p = makeProc (i);
        const float trimNow = ProProcessor::trimDbOf (p->trimRaw->load());
        auto now = seededRender (*p);
        const double lo = lufs (ref), ln = lufs (now);
        const float newTrim = (float) juce::jlimit (-24.0, 18.0, std::round ((trimNow + (lo - ln)) * 100.0) / 100.0);
        p->setParam ("out_trim", newTrim); settle (*p);
        auto again = seededRender (*p);
        printf ("%-28s old %6.1f LUFS (peak %+5.1f dB)  CLEAN %6.1f LUFS at trim %+6.2f -> trim %+6.2f: %6.1f LUFS, true peak %+5.2f dBTP\n", f.name.toRawUTF8(), lo,
                juce::Decibels::gainToDecibels (peakOf (ref)), ln, trimNow, newTrim, lufs (again), truePeakDb (again));
        lines.add ("{ \"" + f.name + "\", " + juce::String (newTrim, 2) + "f },");
    }
    if (outInc.isNotEmpty())
        juce::File (outInc).replaceWithText ("// generated by ProTests cliplist + cleantrims: the factory sounds whose output hit the old soft clip (peak > -1 dBFS) on the test phrase load\n"
                                             "// with CLEAN ON, with the TRIM that keeps them as loud as before\n" + lines.joinIntoString ("\n") + "\n");
    return 0;
}
// the factory sounds whose output hit the OLD output soft clip (pre-clip peak above -1 dBFS) on the compare phrase -> CLEAN ON
int clipList (const juce::String& outInc)
{
#if GPRO_TAPS
    const auto& bank = gpro::factoryBank();
    juce::StringArray lines; int n = 0;
    int cleanOnV2 = 0;
    for (int i = 0; i < (int) bank.size(); ++i)
    {
        if (std::getenv ("GP_SHOWN_ONLY") && gpro::isLegacy (bank[(size_t) i].name)) continue;   // SOUND ENGINE V2: the library BROWSE shows
        auto p = makeProc (i);
        const bool loadsClean = p->clOn->load() > 0.5f;
        p->setParam ("cl_on", 0.0f);
        p->resetTaps();
        seededRender (*p);
        const float pk = p->tapPk[ProProcessor::tapGain];
        if (pk > 0.891f && loadsClean) ++cleanOnV2;
        if (pk > 0.891f) { ++n; lines.add ("{ \"" + bank[(size_t) i].name + "\", " + juce::String (gpro::presetTrimDb (bank[(size_t) i].name), 2) + "f },"); printf ("CLIPPED %-30s pre-clip peak %+5.2f dBFS\n", bank[(size_t) i].name.toRawUTF8(), juce::Decibels::gainToDecibels (pk)); }
    }
    int checked = 0; for (auto& f : bank) checked += (std::getenv ("GP_SHOWN_ONLY") && gpro::isLegacy (f.name)) ? 0 : 1;
    printf ("%d of %d factory sounds clipped (with CLEAN off); %d of them load with CLEAN on (the hidden ceiling + CLEAN hold them), %d without\n", n, checked, cleanOnV2, n - cleanOnV2);
    if (outInc.isNotEmpty())
        juce::File (outInc).replaceWithText ("// generated by ProTests cliplist: factory sounds whose output hit the old soft clip (peak > -1 dBFS) on the test phrase -> CLEAN ON\n"
                                             + lines.joinIntoString ("\n") + (lines.isEmpty() ? "" : "\n"));
#endif
    return 0;
}

// ---------------------------------------------------------------- the CLEAN checks
static int pickPlainPreset()
{   // a factory sound with no trim, no FX scene, no CHARACTER and no stack: the same sound as three layers sums plainly
    const auto& bank = gpro::factoryBank();
    for (int pass = 0; pass < 2; ++pass)
        for (int i = 0; i < (int) bank.size(); ++i)
        {
            const auto& f = bank[(size_t) i];
            if (std::abs (gpro::presetTrimDb (f.name)) > 1.0e-6f || f.scene.isNotEmpty() || f.spec.contains ("ly2") || f.spec.contains ("ch_amt") || f.spec.contains ("mc=")) continue;
            if (pass == 0 && ! (f.cat.containsIgnoreCase ("pad") && f.spec.contains ("o1_uni"))) continue;
            return i;
        }
    return 0;
}
static double mid (const juce::AudioBuffer<float>& b, double s0, double s1) { return rmsDb (b, (int) (s0 * SR), (int) (s1 * SR)); }

// a render loop that also drives the MORPH timer on a simulated clock (every tickMs of audio)
static juce::AudioBuffer<float> renderTicks (ProProcessor& p, const std::vector<Ev>& evs, double seconds, double tickMs)
{
    const int total = (int) (seconds * SR);
    juce::AudioBuffer<float> out (2, total); out.clear(); juce::AudioBuffer<float> blk (2, BS);
    struct Head : juce::AudioPlayHead
    {
        double pos = 0;
        juce::Optional<PositionInfo> getPosition() const override { PositionInfo i; i.setBpm (120); i.setIsPlaying (true); i.setPpqPosition (pos); i.setTimeSignature (TimeSignature { 4, 4 }); return i; }
    } head;
    p.setPlayHead (&head);
    double nextTick = 0;
    for (int pos = 0; pos < total; pos += BS)
    {
        const int n = juce::jmin (BS, total - pos);
        const double ms = pos / SR * 1000.0;
        if (ms >= nextTick) { p.morphTick (1.0e12 + ms); p.flushPendingWaves(); nextTick += tickMs; }
        juce::MidiBuffer mb;
        for (auto& e : evs) { const int s = (int) (e.t * SR); if (s >= pos && s < pos + n) mb.addEvent (e.on ? juce::MidiMessage::noteOn (1, e.note, e.vel) : juce::MidiMessage::noteOff (1, e.note), s - pos); }
        head.pos = pos / SR * 2.0;
        blk.setSize (2, n, false, false, true);
        p.processBlock (blk, mb);
        for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, blk, ch, 0, n);
    }
    p.setPlayHead (nullptr);
    return out;
}
// clicks: 1 ms windows whose 2nd-difference energy jumps far above the local level (x30 the median of the windows around it)
static int clickCount (const juce::AudioBuffer<float>& b, double from)
{
    const int W = 48, n = b.getNumSamples();
    std::vector<double> e;
    for (int s = (int) (from * SR); s + W <= n; s += W)
    {
        double acc = 0;
        for (int ch = 0; ch < 2; ++ch) { auto* d = b.getReadPointer (ch); for (int i = s; i < s + W; ++i) if (i >= 2) { const double v = d[i] - 2.0 * d[i - 1] + d[i - 2]; acc += v * v; } }
        e.push_back (acc);
    }
    int clicks = 0;
    for (size_t i = 0; i < e.size(); ++i)
    {
        std::vector<double> loc; for (size_t j = (i > 40 ? i - 40 : 0); j < std::min (e.size(), i + 40); ++j) if (j != i) loc.push_back (e[j]);
        if (loc.empty()) continue;
        std::nth_element (loc.begin(), loc.begin() + (long) loc.size() / 2, loc.end());
        if (e[i] > 30.0 * loc[loc.size() / 2] + 1e-9) ++clicks;
    }
    return clicks;
}

int cleanTests (const juce::File& shotDir)
{
    section ("CLEAN SOUND: ceiling, gain law, pockets, drives, limiter, ORBIT, UI");
    // ---- 1. the ceiling on every stress patch (CLEAN on and off): true peak <= -0.3 dBTP
    {
        double worst = -100; juce::String which;
        for (int st = 0; st < numStress; ++st)
            for (int cl = 0; cl < 2; ++cl)
            {
                auto p = stressProc (st, cl); seedSystemRandom (99);
                auto out = render (*p, chordPhrase(), 5.6);
                const double tp = truePeakDb (out);
                if (tp > worst) { worst = tp; which = juce::String (stressName (st)) + (cl ? " (CLEAN on)" : " (CLEAN off)"); }
            }
        CHECK (worst <= -0.3, "true peak never above -0.3 dBTP on the stress patches (CLEAN on and off): worst %+.2f dBTP (%s)", worst, which.toRawUTF8());
        auto p = makeProc (0);
        CHECK (p->getLatencySamples() == p->ceilingLatency() + p->rack.latencySamples() + p->engineLatency() && p->ceilingLatency() > 0 && p->ceilingLatency() <= (int) (0.003 * SR),
               "latency reported to the host: %d samples (ceiling %d = %.2f ms, rack %d, engine oversampling %d)", p->getLatencySamples(), p->ceilingLatency(), p->ceilingLatency() * 1000.0 / SR, p->rack.latencySamples(), p->engineLatency());
    }
    // ---- 2. loudness steady: 1..3 layers of the same sound, unison 1..8 (CLEAN on, the same notes)
    {
        const int prog = pickPlainPreset();
        const auto name = gpro::factoryBank()[(size_t) prog].name;
        double lv[3] {};
        for (int nl = 1; nl <= 3; ++nl)
        {
            auto p = makeProc (-1); p->setSoundEngine (ProProcessor::engineClassic); p->setCurrentProgram (prog);   // (CLEAN's layer law on a fixed sound: the round-1 engine, as built)
            p->setParam ("cl_on", 1.0f); p->setParam ("out_trim", 0.0f);
            if (nl >= 2) { p->setLayerSound (1, name); p->setParam ("ly2_lvl", 1.0f); p->layerChild (1)->setParam ("out_trim", 0.0f); }
            if (nl >= 3) { p->setLayerSound (2, name); p->setParam ("ly3_lvl", 1.0f); p->layerChild (2)->setParam ("out_trim", 0.0f); }   // (a layer's own trim stacks on the parent's: equal sounds = both at 0)
            settle (*p); seedSystemRandom (5);
            auto out = render (*p, { { 0.0, 60, 0.85f, true }, { 0.0, 64, 0.85f, true }, { 2.5, 60, 0, false }, { 2.5, 64, 0, false } }, 2.6);
            lv[nl - 1] = mid (out, 0.6, 2.4);
        }
        const double spread = std::max ({ lv[0], lv[1], lv[2] }) - std::min ({ lv[0], lv[1], lv[2] });
        CHECK (spread <= 2.0 && std::abs (lv[1] - lv[0]) <= 1.0 && std::abs (lv[2] - lv[0]) <= 1.0, "1 / 2 / 3 layers of '%s': %.1f / %.1f / %.1f dB RMS (within +-1 dB of one layer)", name.toRawUTF8(), lv[0], lv[1], lv[2]);
        double uv[8] {}; double lo = 100, hi = -100;
        for (int u = 1; u <= 8; ++u)
        {
            auto p = makeProc (-1); p->initPatch(); settle (*p);
            p->setParam ("o1_uni", (float) u); p->setParam ("o1_det", 0.3f); p->setParam ("o1_spr", 0.7f); settle (*p); seedSystemRandom (7);
            auto out = render (*p, { { 0.0, 60, 0.85f, true }, { 1.6, 60, 0, false } }, 1.7);
            uv[u - 1] = mid (out, 0.3, 1.5); lo = std::min (lo, uv[u - 1]); hi = std::max (hi, uv[u - 1]);
        }
        CHECK (hi - lo <= 2.0 && hi - uv[0] <= 1.0 && uv[0] - lo <= 1.0, "unison 1..8: %.1f %.1f %.1f %.1f %.1f %.1f %.1f %.1f dB RMS (within +-1 dB of unison 1)", uv[0], uv[1], uv[2], uv[3], uv[4], uv[5], uv[6], uv[7]);
    }
    // ---- 3 + 4. lows mono, less mud at equal loudness
    {
        int monoOk = 0, mudOk = 0; juce::String rows;
        for (int st = 0; st < numStress; ++st)
        {
            Metrics m[2];
            for (int cl = 0; cl < 2; ++cl) { auto p = stressProc (st, cl); seedSystemRandom (99); m[cl] = measure (render (*p, chordPhrase(), 5.6)); }
            monoOk += m[1].corr >= 0.95; mudOk += m[1].mudShare < m[0].mudShare;
            rows << juce::String::formatted ("\n      %-36s low corr %.2f -> %.2f, mud share %+.1f -> %+.1f dB", stressName (st), m[0].corr, m[1].corr, m[0].mudShare, m[1].mudShare);
        }
        CHECK (monoOk == numStress, "lows (< 120 Hz) mono with CLEAN on: %d of %d stress patches at correlation >= 0.95%s", monoOk, numStress, rows.toRawUTF8());
        CHECK (mudOk == numStress, "less mud with CLEAN on (150-400 Hz share of the whole, level-independent): %d of %d", mudOk, numStress);
    }
    // ---- 5. each layer has its own place: measured as each layer goes into the mix (all three sounding, the main owns the bass)
    {
        auto setup = [] (float width)
        {
            auto p = stressProc (sLayers3Uni8, 1);
            set (*p, { { "o1_spr", 0.0f }, { "o2_spr", 0.0f }, { "sub_lvl", 1.0f }, { "ly2_oct", 1 }, { "ly3_oct", 1 }, { "ly2_lvl", 0.8f }, { "ly3_lvl", 0.8f }, { "cl_width", width } });
            settle (*p); return p;
        };
        auto p = setup (0.5f); p->resetTaps(); seedSystemRandom (3);
        render (*p, { { 0.0, 48, 0.85f, true }, { 1.9, 48, 0, false } }, 2.0);
        const double c0 = p->layerCentroid (0), c1 = p->layerCentroid (1), c2 = p->layerCentroid (2);
        CHECK (p->bassOwnerLive.load() == 0 && std::abs (c0) < 0.1 && c1 < -0.1 && c2 > 0.1 && c2 - c1 > 0.3, "pockets: layer 1 owns the bass, centred (%+.2f); layer 2 leans left (%+.2f), layer 3 right (%+.2f)", c0, c1, c2);
        auto q = setup (0.0f); q->resetTaps(); seedSystemRandom (3);
        render (*q, { { 0.0, 48, 0.85f, true }, { 1.9, 48, 0, false } }, 2.0);
        CHECK (std::abs (q->layerCentroid (1)) < 0.1 && std::abs (q->layerCentroid (2)) < 0.1, "POCKET WIDTH 0: all centred (%+.2f / %+.2f)", q->layerCentroid (1), q->layerCentroid (2));
        auto r = setup (0.5f); r->setParam ("ly2_pan", 0.5f); settle (*r); r->resetTaps(); seedSystemRandom (3);
        render (*r, { { 0.0, 48, 0.85f, true }, { 1.9, 48, 0, false } }, 2.0);
        CHECK (r->layerCentroid (1) > c1 + 0.2, "the layer's own PAN moves it from its pocket (layer 2 at PAN +50 %%: %+.2f, was %+.2f)", r->layerCentroid (1), c1);
        // the bass moves to the layer that carries it: layer 2 two octaves down
        auto o = setup (0.5f); set (*o, { { "sub_lvl", 0.0f }, { "ly2_oct", -2 }, { "ly2_lvl", 1.0f } }); settle (*o); seedSystemRandom (3);
        render (*o, { { 0.0, 60, 0.85f, true }, { 1.9, 60, 0, false } }, 2.0);
        CHECK (o->bassOwnerLive.load() == 1 && o->pocket[0].hpAmt > 0.5f && o->pocket[1].hpAmt < 0.5f, "layer 2 two octaves down owns the bass (owner %d): it is uncut, layer 1 gets the LOW CUT", o->bassOwnerLive.load() + 1);
    }
    // ---- 6. side budget: both wide layers sounding -> the quieter narrows
    {
        auto p = stressProc (sLayers3Uni8, 1); p->setParam ("ly2_lvl", 0.35f); p->setParam ("ly3_lvl", 1.0f); settle (*p); seedSystemRandom (3);
        render (*p, { { 0.0, 64, 0.85f, true }, { 1.4, 64, 0, false } }, 1.2);
        CHECK (p->pocket[1].budget < 0.9f && p->pocket[2].budget >= 1.0f, "side budget: layer 2 (quieter) narrowed to %.2f, layer 3 free (%.2f)", p->pocket[1].budget, p->pocket[2].budget);
        p->layerMute[2] = true; render (*p, { { 0.0, 64, 0.85f, true }, { 1.4, 64, 0, false } }, 1.2);
        CHECK (p->pocket[1].budget >= 1.0f, "layer 3 muted: layer 2 gets its full width back (%.2f)", p->pocket[1].budget);
    }
    // ---- 7. MONO CHECK: the mono sum loses at most 3 dB; the button really sums to mono
    {
        auto p = stressProc (sLayersFx100, 1); seedSystemRandom (11);
        auto out = render (*p, chordPhrase(), 5.6);
        double pm = 0, ps = 0; for (int i = 0; i < out.getNumSamples(); ++i) { const double l = out.getSample (0, i), r = out.getSample (1, i); pm += 0.25 * (l + r) * (l + r); ps += 0.5 * (l * l + r * r); }
        const double loss = 10 * std::log10 (pm / ps);
        p->monoCheck = true; auto mono = render (*p, singleNote (60, 0.5), 0.6); p->monoCheck = false;
        float d = 0; for (int i = 0; i < mono.getNumSamples(); ++i) d = std::max (d, std::abs (mono.getSample (0, i) - mono.getSample (1, i)));
        CHECK (loss >= -3.0 && d == 0.0f, "MONO CHECK: mono sum %.2f dB vs stereo (FX 100 %%, widest patch), button output L == R (max diff %.2g)", loss, d);
    }
    // ---- 8. every drive at 100 %%: under the ceiling and within +-1.5 dB of its 0 %% loudness (CLEAN on)
    {
        struct D { const char* label; std::vector<std::pair<const char*, float>> zero, full; };
        const std::vector<D> drives {
            { "filter DRIVE",   { { "f_drive", 0 }, { "f_dlaw", 0 } }, { { "f_drive", 1 }, { "f_dlaw", 0 } } },   // (the CLASSIC law; the INSIDE law grows louder on purpose: ProTests drive)
            { "FX DRIVE",       { { "drive", 0 } }, { { "drive", 1 } } },
            { "FX CRUSH",       { { "crush", 0 } }, { { "crush", 1 } } },
            { "CHARACTER",      { { "ch_amt", 0 } }, { { "ch_amt", 1 } } },
            { "TAPE",           { { "tp_on", 1 }, { "tp_amt", 0 } }, { { "tp_on", 1 }, { "tp_amt", 1 } } },
            { "WRECKER",        { { "fx_wreck_on", 1 }, { "fx_wreck_drive", 0 } }, { { "fx_wreck_on", 1 }, { "fx_wreck_drive", 1 } } },
            { "TUBE GLOW",      { { "fx_tube_on", 1 }, { "fx_tube_drive", 0 } }, { { "fx_tube_on", 1 }, { "fx_tube_drive", 1 } } },
            { "SOUL CRUSH",     { { "fx_crush_on", 1 }, { "fx_crush_crush", 0 } }, { { "fx_crush_on", 1 }, { "fx_crush_crush", 1 } } },
            { "FATTEN",         { { "fx_fatten_on", 1 }, { "fx_fatten_fat", 0 }, { "fx_fatten_crunch", 0 } }, { { "fx_fatten_on", 1 }, { "fx_fatten_fat", 1 }, { "fx_fatten_crunch", 1 }, { "fx_fatten_drive", 24 } } } };
        juce::String rows; int ok = 0;
        for (auto& d : drives)
        {
            double L[2] {}, T[2] {};
            for (int side = 0; side < 2; ++side)
            {
                auto p = makeProc (-1); p->initPatch(); settle (*p);
                p->setParam ("o1_uni", 4); p->setParam ("o1_det", 0.25f);
                for (auto& [k, v] : (side ? d.full : d.zero)) p->setParam (k, v);
                settle (*p); seedSystemRandom (13);
                auto out = render (*p, { { 0.0, 48, 0.9f, true }, { 0.0, 55, 0.9f, true }, { 0.0, 64, 0.9f, true }, { 2.0, 48, 0, false }, { 2.0, 55, 0, false }, { 2.0, 64, 0, false } }, 2.2);
                L[side] = lufs (out, (int) (0.3 * SR), (int) (2.0 * SR)); T[side] = truePeakDb (out);
            }
            const bool good = std::abs (L[1] - L[0]) <= 1.5 && T[1] <= -0.3 && T[0] <= -0.3;
            ok += good;
            rows << juce::String::formatted ("\n      %s %-13s 0 %%: %6.1f LUFS  100 %%: %6.1f LUFS (%+.1f dB), true peak %+.2f dBTP", good ? "ok  " : "FAIL", d.label, L[0], L[1], L[1] - L[0], T[1]);
        }
        CHECK (ok == (int) drives.size(), "drives at 100 %% (CLEAN on): %d of %d within +-1.5 dB of 0 %% and under the ceiling%s", ok, (int) drives.size(), rows.toRawUTF8());
    }
    // ---- 9. the ceiling on a 10 s pad pushed 12 dB into it: no pumping (the loudness wobble is the pad's own), latency fixed
    {
        double wob[2] {}, grMean = 0, grDev = 0, tpOut = 0;
        for (int hot = 0; hot < 2; ++hot)
        {
            auto p = stressProc (sLayers3Uni8, 1); p->setParam ("gain", hot ? 1.0f : 0.5f); p->setParam ("attack", 0.4f); settle (*p);
            std::vector<Ev> pad; for (int nt : { 48, 55, 60, 64, 67, 71 }) { pad.push_back ({ 0.0, nt, 0.8f, true }); pad.push_back ({ 10.0, nt, 0, false }); }
            const int total = (int) (10.0 * SR); juce::AudioBuffer<float> blk (2, BS); std::vector<double> gr; juce::AudioBuffer<float> out (2, total);
            for (int pos = 0; pos < total; pos += BS)
            {
                juce::MidiBuffer mb; for (auto& e : pad) { const int s = (int) (e.t * SR); if (s >= pos && s < pos + BS) mb.addEvent (e.on ? juce::MidiMessage::noteOn (1, e.note, e.vel) : juce::MidiMessage::noteOff (1, e.note), s - pos); }
                const int n = juce::jmin (BS, total - pos); blk.setSize (2, n, false, false, true); p->processBlock (blk, mb);
                for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, blk, ch, 0, n);
                if (pos > 2.0 * SR) gr.push_back (p->ceiling.lastBlockGrDb);
            }
            std::vector<double> st; for (double t = 2.0; t + 0.4 <= 9.9; t += 0.1) st.push_back (mid (out, t, t + 0.4));
            double m = 0; for (double v : st) m += v; m /= (double) st.size(); double v2 = 0; for (double v : st) v2 += (v - m) * (v - m); wob[hot] = std::sqrt (v2 / (double) st.size());
            if (hot)
            {
                for (double g : gr) grMean += g; grMean /= std::max<size_t> (1, gr.size());
                for (double g : gr) grDev += (g - grMean) * (g - grMean); grDev = std::sqrt (grDev / std::max<size_t> (1, gr.size()));
                tpOut = truePeakDb (out);
            }
        }
        CHECK (wob[1] - wob[0] <= 0.5 && tpOut <= -0.3, "10 s pad 12 dB into the ceiling (%.1f dB gain reduction, %.2f dB deviation): short-term loudness wobble %.2f dB vs %.2f dB without (no pumping), true peak %+.2f dBTP",
               grMean, grDev, wob[1], wob[0], tpOut);
    }
    // ---- 10. MORPH: two stacks as ghosts (CLEAN handles the summed depth); ORBIT figure-8 at max rate: no reload thrash, no clicks
    {
        int reloads[2] {}, clicks[2] {}; double tp[2] {};
        for (int legacy = 1; legacy >= 0; --legacy)
        {
            auto p = stressProc (sMorphStacks, 1);
            p->morphLegacyReload = legacy == 1;
            p->morphBoard.orbit.mode = gspirit::Orbit::Figure8; p->morphBoard.orbit.rate = 4.0f; p->morphBoard.orbit.sync = true; p->morphBoard.orbit.depth = 0.5f;
            p->setParam ("morph_x", 0.5f); p->morphChanged (false);
            const int r0 = p->morphReloads.load();
            std::vector<Ev> evs; for (int nt : { 48, 60, 64, 67 }) { evs.push_back ({ 0.0, nt, 0.8f, true }); evs.push_back ({ 5.8, nt, 0, false }); }
            seedSystemRandom (17);
            auto out = renderTicks (*p, evs, 6.0, 50.0);
            reloads[legacy] = p->morphReloads.load() - r0; clicks[legacy] = clickCount (out, 0.5); tp[legacy] = truePeakDb (out);
        }
        CHECK (reloads[0] <= 6 && reloads[0] < reloads[1] && clicks[0] <= clicks[1], "ORBIT figure-8 at max rate (8 Hz), 6 s: %d discrete reloads (%.1f/s; before: %d = %.1f/s), clicks %d (before %d)",
               reloads[0], reloads[0] / 6.0, reloads[1], reloads[1] / 6.0, clicks[0], clicks[1]);
        CHECK (tp[0] <= -0.3, "two stack presets on the board, CLEAN on: true peak %+.2f dBTP", tp[0]);
        // the puck parked on the other ghost: the leader follows after the flips stop
        auto p = stressProc (sMorphStacks, 1);
        p->setParam ("morph_x", 0.75f); juce::AudioBuffer<float> b (2, BS); for (int k = 0; k < 4; ++k) { juce::MidiBuffer m; p->processBlock (b, m); }
        p->morphTick (2.0e12); p->morphTick (2.0e12 + 300.0);
        CHECK (p->morph.lastLeader == 1, "puck parked on ghost B: B leads (leader %d)", p->morph.lastLeader);
    }
    // ---- 11. old saved states: CLEAN off + their trim; a new sound: CLEAN on; factory: only the ones that clipped
    {
        auto p = makeProc (-1); p->initPatch();
        const bool initOn = p->cleanNow();
        int listOn = 0, listOff = 0; const auto& bank = gpro::factoryBank();
        for (auto& f : bank) (ProProcessor::factoryCleanOn (f.name) ? listOn : listOff)++;
        auto q = makeProc (progOf ("Flat Sub Pure"));
        juce::MemoryBlock mb; q->getStateInformation (mb);
        auto xml = Peek::getXmlFromBinary (mb.getData(), (int) mb.getSize()); auto tree = juce::ValueTree::fromXml (*xml);
        tree.removeChild (tree.getChildWithProperty ("id", "cl_on"), nullptr); tree.removeChild (tree.getChildWithProperty ("id", "out_trim"), nullptr);
        juce::MemoryBlock mb2; Peek::copyXmlToBinary (*tree.createXml(), mb2);
        auto r = makeProc (0); r->setParam ("cl_on", 1.0f); r->setStateInformation (mb2.getData(), (int) mb2.getSize());
        CHECK (initOn && ! r->cleanNow() && std::abs (ProProcessor::trimDbOf (r->trimRaw->load()) - gpro::presetTrimDb ("Flat Sub Pure")) < 0.005f,
               "INIT = CLEAN on (%d); an older state = CLEAN off (%d) with its hidden trim on the TRIM knob (%.2f dB, preset %.2f); factory: %d CLEAN on (they clipped), %d off",
               (int) initOn, (int) ! r->cleanNow(), ProProcessor::trimDbOf (r->trimRaw->load()), gpro::presetTrimDb ("Flat Sub Pure"), listOn, listOff);
        auto sound = q->captureSound();
        CHECK (sound.getChildWithProperty ("id", "out_trim").isValid() && sound.getChildWithProperty ("id", "cl_on").isValid(), "a sound carries its TRIM and CLEAN as knobs");
    }
    // ---- 12. UI: the strip and the card in the three looks, nothing else moved
    {
        shotDir.createDirectory();
        auto p = stressProc (sLayers3Uni8, 1); seedSystemRandom (1); render (*p, chordPhrase(), 1.0);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor()); auto* pe = dynamic_cast<ProEditor*> (ed.get());
        pe->setSize (ProEditor::W, ProEditor::H); pe->showTab (5);
        static const char* themeName[] { "black", "purple", "light" };
        int overlaps = 0;
        for (int t = 0; t < 3; ++t)
        {
            pe->setTheme (t); pe->showCleanCard (false); pump (150);
            for (int card = 0; card < 2; ++card)
            {
                pe->showCleanCard (card == 1); pump (200);
                auto img = pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f);
                auto f = shotDir.getChildFile (juce::String ("clean-") + themeName[t] + (card ? "-card" : "-strip") + ".png"); f.deleteFile();
                if (auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream())) juce::PNGImageFormat().writeImageToStream (img, *os);
            }
        }
        // the strip sits in free header space: it overlaps no other visible header control
        juce::Component* strip = nullptr; auto* content = pe->contentComponent();
        for (auto* c : content->getChildren()) if (dynamic_cast<juce::SettableTooltipClient*> (c) != nullptr && c->getWidth() == 112) strip = c;
        if (strip != nullptr) for (auto* c : content->getChildren()) if (c != strip && c->isVisible() && c->getY() < 76 && c->getBounds().intersects (strip->getBounds())) ++overlaps;
        CHECK (strip != nullptr && overlaps == 0, "the CLEAN strip sits in free header space (%d overlaps); screenshots in %s", overlaps, shotDir.getFullPathName().toRawUTF8());
    }
    printf ("\n%d failures\n", failures);
    return failures;
}
} // namespace cleantest
