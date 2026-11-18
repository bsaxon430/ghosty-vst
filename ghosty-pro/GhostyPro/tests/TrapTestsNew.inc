// Ghosty Pro TRAP feature tests (included by TrapTests.cpp unless TRAP_REF_ONLY): stack, sample, character, tape, brass,
// state, cpu, shots <dir>, demo <dir>, all.  Every CHECK prints ok/FAIL and the numbers it looked at.
#include <juce_dsp/juce_dsp.h>
#include <thread>
#ifndef THEME_REF
#include "ProTheme.h"
#include "FxCore.h"
#endif

namespace
{
struct Peek : juce::AudioProcessor { using juce::AudioProcessor::getXmlFromBinary; using juce::AudioProcessor::copyXmlToBinary; };   // the protected statics
int progOf (const juce::String& name)
{
    const auto& b = gpro::factoryBank();
    for (int i = 0; i < (int) b.size(); ++i) if (b[(size_t) i].name == name) return i;
    printf ("FAIL  no factory sound called %s\n", name.toRawUTF8()); ++failures; return 0;
}
std::vector<Ev> note (int n, double t0, double len, float vel = 0.9f) { return { { t0, n, vel, true }, { t0 + len, n, 0, false } }; }
std::vector<Ev> operator+ (std::vector<Ev> a, const std::vector<Ev>& b) { a.insert (a.end(), b.begin(), b.end()); return a; }

// normalised autocorrelation pitch of the mono sum in [t0, t1); 0 if nothing periodic
double pitchOf (const juce::AudioBuffer<float>& b, double t0, double t1, double fmin = 40, double fmax = 2000)
{
    const int s = (int) (t0 * SR), e = juce::jmin (b.getNumSamples(), (int) (t1 * SR)), n = e - s;
    if (n < 64) return 0;
    std::vector<double> x ((size_t) n);
    for (int i = 0; i < n; ++i) x[(size_t) i] = b.getSample (0, s + i) + (b.getNumChannels() > 1 ? b.getSample (1, s + i) : 0.0f);
    const int minLag = juce::jmax (2, (int) (SR / fmax)), maxLag = juce::jmin (n / 2, (int) (SR / fmin));
    if (maxLag <= minLag + 2) return 0;
    const int m = n - maxLag - 1;      // lags run to maxLag + 1
    double e0 = 0; for (int i = 0; i < m; ++i) e0 += x[(size_t) i] * x[(size_t) i];
    if (e0 < 1.0e-10) return 0;
    std::vector<double> ac ((size_t) maxLag + 2, 0.0);
    for (int lag = minLag - 1; lag <= maxLag + 1; ++lag)
    {
        double sum = 0, e1 = 0;
        for (int i = 0; i < m; ++i) { sum += x[(size_t) i] * x[(size_t) (i + lag)]; e1 += x[(size_t) (i + lag)] * x[(size_t) (i + lag)]; }
        if (lag >= 0 && lag < (int) ac.size()) ac[(size_t) lag] = sum / std::sqrt (e0 * e1 + 1.0e-12);
    }
    double mx = 0; for (int lag = minLag; lag <= maxLag; ++lag) mx = std::max (mx, ac[(size_t) lag]);
    if (mx < 0.3) return 0;
    int bl = -1;
    for (int lag = minLag; lag <= maxLag; ++lag)
        if (ac[(size_t) lag] >= 0.9 * mx && ac[(size_t) lag] >= ac[(size_t) lag - 1] && ac[(size_t) lag] >= ac[(size_t) lag + 1]) { bl = lag; break; }
    if (bl < 0) return 0;
    double d = bl;
    { const double a = ac[(size_t) bl - 1], c = ac[(size_t) bl + 1], bb = ac[(size_t) bl], den = a - 2 * bb + c; if (std::abs (den) > 1.0e-12) d += 0.5 * (a - c) / den; }
    return SR / d;
}
double semisBetween (double fa, double fb) { return (fa > 0 && fb > 0) ? 12.0 * std::log2 (fb / fa) : 999.0; }

// Goertzel power (dB) of the mono sum at one frequency in [t0, t1)
double toneDb (const juce::AudioBuffer<float>& b, double t0, double t1, double f)
{
    const int s = (int) (t0 * SR), e = juce::jmin (b.getNumSamples(), (int) (t1 * SR)), n = e - s;
    if (n < 16) return -200;
    const double w = 2.0 * std::cos (2.0 * juce::MathConstants<double>::pi * f / SR);
    double s0 = 0, s1 = 0, s2 = 0;
    for (int i = 0; i < n; ++i)
    {
        const double x = (b.getSample (0, s + i) + (b.getNumChannels() > 1 ? b.getSample (1, s + i) : 0.0f)) * (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / n));
        s0 = x + w * s1 - s2; s2 = s1; s1 = s0;
    }
    const double p = (s1 * s1 + s2 * s2 - w * s1 * s2) / ((double) n * n);
    return 10.0 * std::log10 (std::max (1.0e-20, p));
}
// spectral centroid (Hz) of the mono sum in [t0, t1)
double centroid (const juce::AudioBuffer<float>& b, double t0, double t1)
{
    constexpr int ORDER = 13, N = 1 << ORDER;
    juce::dsp::FFT fft (ORDER);
    std::vector<float> buf ((size_t) N * 2, 0.0f);
    const int s = (int) (t0 * SR), e = juce::jmin (b.getNumSamples(), (int) (t1 * SR)), n = juce::jmin (N, e - s);
    for (int i = 0; i < n; ++i) buf[(size_t) i] = (b.getSample (0, s + i) + b.getSample (1, s + i)) * (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / n));
    fft.performFrequencyOnlyForwardTransform (buf.data());
    double num = 0, den = 0;
    for (int k = 1; k < N / 2; ++k) { const double m = buf[(size_t) k]; num += m * m * k * SR / N; den += m * m; }
    return den > 0 ? num / den : 0;
}
int nansIn (const juce::AudioBuffer<float>& b) { int n = 0; peakOf (b, &n); return n; }

void section (const char* t) { printf ("\n== %s\n", t); fflush (stdout); }
juce::File scratchDir() { auto d = juce::File ("/root/work/trap/tmp"); d.createDirectory(); return d; }

// a 1 s "vowel" test sample at 220 Hz: harmonics under two formant bumps (700 / 1200 Hz), decaying to -20 dB
juce::File makeTestSample()
{
    const int n = (int) SR;
    juce::AudioBuffer<float> b (2, n);
    for (int i = 0; i < n; ++i)
    {
        const double t = i / SR; double v = 0;
        for (int h = 1; h <= 20; ++h)
        {
            const double f = 220.0 * h;
            const double form = std::exp (-std::pow ((f - 700.0) / 180.0, 2.0)) + 0.7 * std::exp (-std::pow ((f - 1200.0) / 220.0, 2.0)) + 0.08;
            v += form / h * std::sin (2.0 * juce::MathConstants<double>::pi * f * t);
        }
        v *= std::exp (-t * 2.3) * (t < 0.005 ? t / 0.005 : 1.0);
        b.setSample (0, i, (float) (v * 0.5)); b.setSample (1, i, (float) (v * 0.5));
    }
    auto f = scratchDir().getChildFile ("vowel_a3.wav");
    writeWav (b, f);
    return f;
}

// ------------------------------------------------------------------ STACK
void testStack()
{
    section ("STACK: layers 2/3 play another factory sound, with octave, delay, velocity / key split, level, reverse; nothing sticks");
    const int sub = progOf ("Flat Sub Pure");
    auto p = makeProc (sub);
    p->setParam ("verb", 0.0f); p->setParam ("echo", 0.0f);
    auto base = render (*p, note (48, 0.05, 1.0), 1.6);
    const double f1 = juce::MidiMessage::getMidiNoteInHertz (48), f2 = f1 * 2;
    const double baseLow = toneDb (base, 0.3, 0.9, f1), baseHigh = toneDb (base, 0.3, 0.9, f2);
    CHECK (nansIn (base) == 0 && baseLow > -40, "main alone: %.1f dB at %.0f Hz, %.1f dB at %.0f Hz", baseLow, f1, baseHigh, f2);

    CHECK (p->setLayerSound (1, "Flat Sub Pure") && p->layerSound (1) == "Flat Sub Pure", "setLayerSound picks a factory sound by name");
    CHECK (! p->setLayerSound (1, "No Such Sound"), "unknown sound name is refused");
    CHECK ((int) p->ly[1].src->load() == gpro::lsSound, "picking a sound switches layer 2 to Sound");
    p->setParam ("ly2_oct", 1.0f); settle (*p);
    auto two = render (*p, note (48, 0.05, 1.0), 1.6);
    const double twoHigh = toneDb (two, 0.3, 0.9, f2);
    CHECK (nansIn (two) == 0 && twoHigh > baseHigh + 10, "layer 2 an octave up adds %.1f dB at %.0f Hz (was %.1f)", twoHigh, f2, baseHigh);

    p->setParam ("ly2_lvl", 0.0f);
    auto muted = render (*p, note (48, 0.05, 1.0), 1.6);
    CHECK (maxDiff (muted, base) < 1.0e-3f, "layer level 0 = main alone (max diff %.2e)", maxDiff (muted, base));
    p->setParam ("ly2_lvl", 0.8f);

    p->setParam ("ly2_dly", 0.4f);
    auto delayed = render (*p, note (48, 0.05, 1.0), 1.8);
    const double early = toneDb (delayed, 0.1, 0.4, f2), late = toneDb (delayed, 0.6, 1.0, f2);
    CHECK (early < late - 15, "start delay 0.4 s: octave layer %.1f dB before, %.1f dB after", early, late);
    p->setParam ("ly2_dly", 0.0f);

    p->setParam ("ly2_play", (float) gpro::pmVelHigh); p->setParam ("stk_vsplit", 80.0f);
    auto soft = render (*p, note (48, 0.05, 1.0, 0.3f), 1.6), hard = render (*p, note (48, 0.05, 1.0, 0.95f), 1.6);
    CHECK (toneDb (soft, 0.3, 0.9, f2) < toneDb (hard, 0.3, 0.9, f2) - 15, "VEL HIGH split: soft note %.1f dB, hard note %.1f dB at the layer's pitch",
           toneDb (soft, 0.3, 0.9, f2), toneDb (hard, 0.3, 0.9, f2));
    p->setParam ("ly2_play", (float) gpro::pmKeyLow); p->setParam ("stk_ksplit", 60.0f);
    auto low = render (*p, note (48, 0.05, 1.0), 1.6), high = render (*p, note (72, 0.05, 1.0), 1.6);
    const double fHi = juce::MidiMessage::getMidiNoteInHertz (72) * 2;
    CHECK (toneDb (low, 0.3, 0.9, f2) > -40 && toneDb (high, 0.3, 0.9, fHi) < toneDb (low, 0.3, 0.9, f2) - 15, "KEY LOW split: note 48 layer %.1f dB, note 72 layer %.1f dB",
           toneDb (low, 0.3, 0.9, f2), toneDb (high, 0.3, 0.9, fHi));
    p->setParam ("ly2_play", (float) gpro::pmAll);

    // reverse on a sound layer = swell in, cut on release
    p->setParam ("ly2_rev", 1.0f);
    auto rev = render (*p, note (48, 0.05, 1.2), 1.8);
    const double rEarly = toneDb (rev, 0.15, 0.35, f2), rLate = toneDb (rev, 0.9, 1.2, f2), rAfter = rmsDb (rev, (int) (1.4 * SR), (int) (1.8 * SR));
    CHECK (rLate > rEarly + 6 && rAfter < -50, "REVERSE sound layer swells: %.1f dB early -> %.1f dB late, %.1f dB RMS after the cut", rEarly, rLate, rAfter);
    p->setParam ("ly2_rev", 0.0f);

    // the layer pitch follows the detune knob
    p->setParam ("ly2_det", 50.0f);
    auto det = render (*p, note (48, 0.05, 1.0), 1.6);
    p->setParam ("ly2_det", 0.0f);
    CHECK (toneDb (det, 0.3, 0.9, f2 * std::exp2 (50.0 / 1200.0)) > toneDb (det, 0.3, 0.9, f2) + 6, "layer detune +50 ct moves the layer's partial (%.1f vs %.1f dB)",
           toneDb (det, 0.3, 0.9, f2 * std::exp2 (50.0 / 1200.0)), toneDb (det, 0.3, 0.9, f2));

    // three layers, sample-less trap stack preset: clean, bounded, silent afterwards, no voices left
    auto st = makeProc (progOf ("Trap Starter Stack"));
    auto buf = render (*st, testPhrase(), 7.0);
    int nans = 0; const float pk = peakOf (buf, &nans);
    const double tail = rmsDb (buf, (int) (6.5 * SR), (int) (7.0 * SR)), body = rmsDb (buf, 0, (int) (3.5 * SR));
    int left = 0; for (int k = 0; k < gpro::NUM_LAYERS; ++k) left += st->layerVoices[k].load();
    CHECK (nans == 0 && pk <= 1.0f && body > -30 && tail < -60 && left == 0, "Trap Starter Stack (3 layers): %d NaN, peak %.3f, body %.1f dB, tail %.1f dB, voices left %d", nans, pk, body, tail, left);

    // switching a layer off while notes are held leaves nothing hanging when it comes back
    {
        auto q = makeProc (sub); q->setParam ("verb", 0.0f); q->setParam ("echo", 0.0f);
        q->setLayerSound (1, "Flat Sub Pure"); q->setParam ("ly2_oct", 1.0f); settle (*q);
        std::vector<Ev> evs { { 0.05, 48, 0.9f, true } };            // note on, never off while the layer is on
        auto a = render (*q, evs, 0.5);
        q->setParam ("ly2_src", (float) gpro::lsOff);
        auto b = render (*q, { { 0.0, 48, 0, false } }, 0.5);        // note off arrives while the layer is off
        q->setParam ("ly2_src", (float) gpro::lsSound);
        auto c = render (*q, {}, 1.0);
        CHECK (toneDb (a, 0.2, 0.5, f2) > -40 && rmsDb (c, (int) (0.5 * SR), (int) (1.0 * SR)) < -60 && q->layerVoices[1].load() == 0,
               "layer off -> note off -> layer on: %.1f dB RMS afterwards, %d layer voices", rmsDb (c, (int) (0.5 * SR), (int) (1.0 * SR)), q->layerVoices[1].load());
    }
}

// ------------------------------------------------------------------ SAMPLE layer
void testSample()
{
    section ("SAMPLE layer: import, root detection, pitch across the keys, formant keep, reverse, loop, release, clear");
    auto wav = makeTestSample();
    auto p = makeProc (progOf ("Flat Sub Pure"));
    p->setParam ("verb", 0.0f); p->setParam ("echo", 0.0f); p->setParam ("ly1_lvl", 0.0f);     // main muted: only the sample layer sounds
    juce::String msg;
    CHECK (p->importTrapSample (wav, msg), "import: %s", msg.toRawUTF8());
    auto d = p->trapSample();
    CHECK (d != nullptr && d->length() == (int) SR && d->root == 57 && std::abs (d->rootCents) < 25, "root detected %d (%+.0f ct), %d samples", d ? d->root : -1, d ? d->rootCents : 0.0f, d ? d->length() : 0);
    CHECK ((int) p->ly[1].src->load() == gpro::lsSample, "the first free layer became the SAMPLE layer");
    settle (*p);

    auto at57 = render (*p, note (57, 0.02, 0.6), 1.2), at69 = render (*p, note (69, 0.02, 0.6), 1.2), at45 = render (*p, note (45, 0.02, 0.6), 1.2);
    const double f57 = pitchOf (at57, 0.15, 0.45), f69 = pitchOf (at69, 0.15, 0.45), f45 = pitchOf (at45, 0.15, 0.45);
    CHECK (nansIn (at57) + nansIn (at69) + nansIn (at45) == 0, "no NaN");
    CHECK (std::abs (semisBetween (220.0, f57)) < 0.35, "root key plays %.1f Hz (220 expected)", f57);
    CHECK (std::abs (semisBetween (440.0, f69)) < 0.35, "an octave up plays %.1f Hz (440 expected)", f69);
    CHECK (std::abs (semisBetween (110.0, f45)) < 0.35, "an octave down plays %.1f Hz (110 expected)", f45);
    CHECK (rmsDb (at57, (int) (0.15 * SR), (int) (0.45 * SR)) > -30, "sample layer level %.1f dB RMS", rmsDb (at57, (int) (0.15 * SR), (int) (0.45 * SR)));

    // formant keep: an octave up keeps the spectral envelope where it was; without it everything shifts up
    const double cRoot = centroid (at57, 0.1, 0.27), cKeep = centroid (at69, 0.1, 0.27);
    p->setParam ("smp_formant", 0.0f);
    auto plain69 = render (*p, note (69, 0.02, 0.6), 1.2);
    const double cPlain = centroid (plain69, 0.1, 0.27);
    p->setParam ("smp_formant", 1.0f);
    CHECK (cKeep < cPlain * 0.8 && std::abs (std::log2 (cKeep / cRoot)) < 0.5, "centroid: root %.0f Hz, +12 st with formant keep %.0f Hz, without %.0f Hz", cRoot, cKeep, cPlain);

    // velocity
    auto softv = render (*p, note (57, 0.02, 0.6, 0.3f), 1.2);
    CHECK (rmsDb (softv, (int) (0.15 * SR), (int) (0.45 * SR)) < rmsDb (at57, (int) (0.15 * SR), (int) (0.45 * SR)) - 3, "velocity 0.3 is %.1f dB quieter",
           rmsDb (at57, (int) (0.15 * SR), (int) (0.45 * SR)) - rmsDb (softv, (int) (0.15 * SR), (int) (0.45 * SR)));

    // reverse: the decaying sample now rises
    p->setParam ("ly2_rev", 1.0f);
    auto rev = render (*p, note (57, 0.02, 1.1), 1.6);
    const double early = rmsDb (rev, (int) (0.15 * SR), (int) (0.35 * SR)), late = rmsDb (rev, (int) (0.75 * SR), (int) (0.95 * SR));
    const double fwdEarly = rmsDb (at57, (int) (0.15 * SR), (int) (0.35 * SR)), fwdLate = rmsDb (at57, (int) (0.4 * SR), (int) (0.6 * SR));
    CHECK (late > early + 6 && fwdLate < fwdEarly, "REVERSE: %.1f dB early -> %.1f dB late (forwards: %.1f -> %.1f)", early, late, fwdEarly, fwdLate);
    p->setParam ("ly2_rev", 0.0f);

    // loop a short region: the note keeps sounding; without the loop the one-shot runs out
    p->setParam ("smp_loop", 1.0f); p->setParam ("smp_start", 0.2f); p->setParam ("smp_end", 0.4f);
    auto looped = render (*p, note (57, 0.02, 2.4), 3.0);
    p->setParam ("smp_loop", 0.0f); p->setParam ("smp_start", 0.0f); p->setParam ("smp_end", 1.0f);
    auto oneShot = render (*p, note (57, 0.02, 2.4), 3.0);
    const double lLate = rmsDb (looped, (int) (2.0 * SR), (int) (2.4 * SR)), oLate = rmsDb (oneShot, (int) (2.0 * SR), (int) (2.4 * SR));
    CHECK (lLate > -40 && oLate < -70, "LOOP 0.2..0.4 holds at %.1f dB after 2 s; the one-shot is at %.1f dB", lLate, oLate);
    CHECK (std::abs (semisBetween (220.0, pitchOf (looped, 1.5, 2.0))) < 0.5, "looped region still plays %.1f Hz", pitchOf (looped, 1.5, 2.0));

    // release and silence afterwards, no voices left
    p->setParam ("smp_r", 0.2f);
    auto rel = render (*p, note (57, 0.02, 0.3), 1.5);
    CHECK (rmsDb (rel, (int) (0.9 * SR), (int) (1.5 * SR)) < -70 && p->layerVoices[1].load() == 0, "release 0.2 s: %.1f dB RMS 0.6 s after note-off, %d sample voices left",
           rmsDb (rel, (int) (0.9 * SR), (int) (1.5 * SR)), p->layerVoices[1].load());

    // polyphony: 6 notes at once, nothing explodes; the 7th steals
    std::vector<Ev> chord;
    for (int i = 0; i < 7; ++i) chord = chord + note (50 + i * 3, 0.02 + i * 0.01, 0.6);
    auto poly = render (*p, chord, 1.3);
    CHECK (nansIn (poly) == 0 && peakOf (poly) <= 1.0f && p->layerVoices[1].load() == 0, "7 notes: %d NaN, peak %.3f, %d left", nansIn (poly), peakOf (poly), p->layerVoices[1].load());

    // clear
    p->clearTrapSample(); pump (50);
    auto gone = render (*p, note (57, 0.02, 0.6), 1.0);
    CHECK (p->trapSample() == nullptr && rmsDb (gone, 0, (int) SR) < -80, "CLEAR: no sample, %.1f dB RMS", rmsDb (gone, 0, (int) SR));
    CHECK (! p->importTrapSample (juce::File ("/nonexistent/none.wav"), msg), "a missing file is refused: %s", msg.toRawUTF8());
}

// ------------------------------------------------------------------ CHARACTER
void testCharacter()
{
    section ("CHARACTER: exact bypass at 0, level held while the knob moves, harmonics added, no NaN at full");
    gpro::Character ch; ch.prepare (SR, 8192);
    auto sine = [] (float amp, double f) { juce::AudioBuffer<float> b (2, 8192); for (int i = 0; i < 8192; ++i) { const float v = amp * (float) std::sin (2.0 * juce::MathConstants<double>::pi * f * i / SR); b.setSample (0, i, v); b.setSample (1, i, v); } return b; };
    auto in = sine (0.35f, 440.0), x = in;
    ch.process (x, 0.0f);
    CHECK (maxDiff (x, in) == 0.0f, "amount 0: bit-exact bypass");
    for (float a : { 0.25f, 0.5f, 0.75f, 1.0f })
    {
        auto y = sine (0.35f, 440.0);
        ch.reset(); ch.process (y, a);
        const double gIn = rmsDb (in, 2048, 8192), gOut = rmsDb (y, 2048, 8192);
        const double fund = toneDb (y, 2048.0 / SR, 8192.0 / SR, 440.0), h3 = toneDb (y, 2048.0 / SR, 8192.0 / SR, 1320.0), h3in = toneDb (in, 2048.0 / SR, 8192.0 / SR, 1320.0);
        CHECK (nansIn (y) == 0 && std::abs (gOut - gIn) < 2.5 && h3 > h3in + 20, "amount %.2f: level %+.1f dB (comp), 3rd harmonic %.1f dB below the fundamental (clean: %.1f), peak %.2f",
               a, gOut - gIn, fund - h3, fund - h3in, peakOf (y));
    }
    {
        auto hot = sine (2.0f, 55.0);        // way too hot, low: stays bounded, DC-free
        ch.reset(); ch.process (hot, 1.0f);
        double dc = 0; for (int i = 4096; i < 8192; ++i) dc += hot.getSample (0, i); dc /= 4096;
        CHECK (nansIn (hot) == 0 && peakOf (hot) < 1.6f && std::abs (dc) < 0.02, "+6 dB 55 Hz in at full: peak %.2f, DC %.3f", peakOf (hot), dc);
    }
    // in the synth: a sub preset keeps its loudness while CHARACTER goes up
    auto p = makeProc (progOf ("Flat Sub Pure"));
    p->setParam ("ch_amt", 0.0f);
    auto clean = render (*p, note (41, 0.05, 1.5), 2.0);
    p->setParam ("ch_amt", 0.7f);
    auto dirty = render (*p, note (41, 0.05, 1.5), 2.0);
    const double lc = lufs (clean, 0, (int) (1.5 * SR)), ld = lufs (dirty, 0, (int) (1.5 * SR));
    CHECK (nansIn (dirty) == 0 && std::abs (ld - lc) < 3.0, "sub with CHARACTER 0.7: %.1f LUFS vs %.1f clean", ld, lc);
    // moving the knob mid-note: no click
    {
        auto q = makeProc (progOf ("Flat Sub Pure"));
        std::vector<Ev> on { { 0.02, 41, 0.9f, true } };
        auto a = render (*q, on, 0.5);
        float worst = 0;
        for (int step = 1; step <= 10; ++step)
        {
            q->setParam ("ch_amt", step * 0.1f);
            auto b = render (*q, {}, 0.05);
            for (int i = 1; i < b.getNumSamples(); ++i) worst = std::max (worst, std::abs (b.getSample (0, i) - b.getSample (0, i - 1)));
        }
        CHECK (worst < 0.08f, "knob sweep while a sub is held: largest sample step %.3f", worst);
    }
}

// ------------------------------------------------------------------ TAPE / SLOWED
void testTape()
{
    section ("TAPE / SLOWED: exact bypass when off, soft fade in, roll-off, wow, pitch-down on the whole stack");
    gpro::Tape tp; tp.prepare (SR);
    auto tone = [] (double f, int n = 24000) { juce::AudioBuffer<float> b (2, n); for (int i = 0; i < n; ++i) { const float v = 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * f * i / SR); b.setSample (0, i, v); b.setSample (1, i, v); } return b; };
    auto in = tone (1000.0), x = in;
    tp.process (x, false, 1.0f);
    CHECK (maxDiff (x, in) == 0.0f, "off: bit-exact bypass");
    auto lo = tone (500.0), hi = tone (10000.0);
    tp.prepare (SR); tp.process (lo, true, 1.0f); tp.prepare (SR); tp.process (hi, true, 1.0f);
    const double gLo = rmsDb (lo, 12000, 24000) - rmsDb (in, 12000, 24000), gHi = rmsDb (hi, 12000, 24000) - rmsDb (in, 12000, 24000);
    CHECK (nansIn (lo) + nansIn (hi) == 0 && gLo > -4 && gLo < 2 && gHi < gLo - 8, "full amount: 500 Hz %+.1f dB, 10 kHz %+.1f dB (roll-off)", gLo, gHi);
    {
        auto y = tone (1000.0, 48000);
        tp.prepare (SR); tp.process (y, true, 1.0f);
        const double f0 = pitchOf (y, 0.3, 0.5, 500, 1500), f1 = pitchOf (y, 0.6, 0.8, 500, 1500);
        CHECK (std::abs (f0 - 1000) < 30 && std::abs (f1 - 1000) < 30 && std::abs (f0 - f1) > 0.01, "wow / flutter: %.2f Hz then %.2f Hz around 1 kHz", f0, f1);
    }
    {
        // switching on / off while audio runs: soft, no big steps
        auto y = tone (1000.0, 48000); tp.prepare (SR);
        juce::AudioBuffer<float> blk (2, 512); float worst = 0;
        for (int pos = 0; pos + 512 <= 48000; pos += 512)
        {
            for (int c = 0; c < 2; ++c) blk.copyFrom (c, 0, y, c, pos, 512);
            tp.process (blk, (pos / 512) % 20 < 10, 1.0f);
            for (int i = 1; i < 512; ++i) worst = std::max (worst, std::abs (blk.getSample (0, i) - blk.getSample (0, i - 1)));
        }
        CHECK (worst < 0.12f, "toggling every 107 ms: largest sample step %.3f (the 1 kHz tone itself steps %.3f)", worst, 0.3f * 2.0f * (float) std::sin (juce::MathConstants<double>::pi * 1000.0 / SR));
    }
    // in the synth: pitch-down of main and layer
    auto p = makeProc (progOf ("Flat Sub Pure"));
    p->setParam ("verb", 0.0f); p->setParam ("echo", 0.0f);
    p->setLayerSound (1, "Flat Sub Pure"); p->setParam ("ly2_oct", 1.0f); settle (*p);
    auto dry = render (*p, note (57, 0.05, 1.5), 2.0);
    p->setParam ("tp_on", 1.0f); p->setParam ("tp_amt", 1.0f);
    auto slow = render (*p, note (57, 0.05, 1.5), 2.0);
    const double fDry = pitchOf (dry, 0.8, 1.4, 100, 300), fSlow = pitchOf (slow, 0.8, 1.4, 100, 300);
    CHECK (nansIn (slow) == 0 && std::abs (semisBetween (fDry, fSlow) + 3.0) < 0.3, "full SLOWED: main %.1f -> %.1f Hz (%.2f st)", fDry, fSlow, semisBetween (fDry, fSlow));
    const double layerDry = toneDb (dry, 0.8, 1.4, 440.0), layerSlow = toneDb (slow, 0.8, 1.4, 440.0 * std::exp2 (-0.25));
    CHECK (layerSlow > layerDry - 6, "the layer follows: %.1f dB at 440 dry, %.1f dB at %.0f Hz slowed", layerDry, layerSlow, 440.0 * std::exp2 (-0.25));
    p->setParam ("tp_amt", 0.3f);
    auto warm = render (*p, note (57, 0.05, 1.5), 2.0);
    p->setParam ("tp_slow", -7.0f);
    auto slow7 = render (*p, note (57, 0.05, 1.5), 2.0);
    CHECK (std::abs (semisBetween (fDry, pitchOf (warm, 0.8, 1.4, 100, 300)) + 3.0) < 0.2 && std::abs (lufs (warm) - lufs (dry)) < 3 && std::abs (semisBetween (fDry, pitchOf (slow7, 0.8, 1.4, 100, 300)) + 7.0) < 0.2,
           "amount 0.3: still %.2f st (SLOW -3), %.1f LUFS (dry %.1f); SLOW -7: %.2f st", semisBetween (fDry, pitchOf (warm, 0.8, 1.4, 100, 300)), lufs (warm), lufs (dry), semisBetween (fDry, pitchOf (slow7, 0.8, 1.4, 100, 300)));
    // TAPE belongs to the sound: the next preset starts without it
    p->setCurrentProgram (progOf ("Flat Sub Pure")); pump (20);
    CHECK (p->tpOn->load() < 0.5f, "loading a sound resets TAPE");
}

// ------------------------------------------------------------------ BRASS engine
// level wobble (dB std-dev) of RMS windows one fundamental period long (so the waveform itself doesn't count), 2 ms hop
double wobble (const juce::AudioBuffer<float>& b, double t0, double t1, double f0 = 130.81)
{
    const int win = (int) std::round (SR / f0); std::vector<double> e;
    for (int s = (int) (t0 * SR); s + win <= (int) (t1 * SR); s += (int) (0.002 * SR)) e.push_back (rmsDb (b, s, s + win));
    double m = 0; for (auto v : e) m += v; m /= (double) juce::jmax<size_t> (1, e.size());
    double v2 = 0; for (auto v : e) v2 += (v - m) * (v - m);
    return std::sqrt (v2 / (double) juce::jmax<size_t> (1, e.size()));
}
void testBrass()
{
    section ("SYNTH BRASS: scoop into the note, growl, body peak, fall after release, Growl as a mod target");
    auto fresh = [] { auto p = makeProc (progOf ("Flat Sub Pure")); p->initPatch(); p->setParam ("o1_tbl", 3.0f); p->setParam ("o1_uni", 1.0f); p->setParam ("attack", 0.002f); p->setParam ("release", 0.4f);
                      p->setParam ("sustain", 1.0f); p->setParam ("f_type", 4.0f); p->setParam ("cutoff", 0.62f); p->setParam ("verb", 0.0f); p->setParam ("echo", 0.0f); settle (*p); return p; };
    {
        auto p = fresh(); p->setParam ("br_scoop", 1.0f); p->setParam ("br_scoopt", 0.12f);
        auto b = render (*p, note (60, 0.0, 1.0), 1.2);
        const double fStart = pitchOf (b, 0.012, 0.045, 150, 400), fHeld = pitchOf (b, 0.6, 0.9, 150, 400);
        CHECK (nansIn (b) == 0 && semisBetween (fHeld, fStart) < -1.5 && std::abs (semisBetween (261.6, fHeld)) < 0.15, "SCOOP: %.1f Hz at 30 ms -> %.1f Hz held (%.2f st)", fStart, fHeld, semisBetween (fHeld, fStart));
    }
    {
        auto p = fresh(); p->setParam ("cutoff", 0.5f);
        auto plain = render (*p, note (48, 0.0, 1.0), 1.0);
        p->setParam ("br_growl", 1.0f);
        auto growl = render (*p, note (48, 0.0, 1.0), 1.0);
        CHECK (nansIn (growl) == 0 && wobble (growl, 0.3, 0.9) > wobble (plain, 0.3, 0.9) + 1.0, "GROWL: period-synchronous level wobble %.2f dB (plain %.2f dB)", wobble (growl, 0.3, 0.9), wobble (plain, 0.3, 0.9));
    }
    {
        auto p = fresh(); p->setParam ("cutoff", 0.9f);
        auto plain = render (*p, note (48, 0.0, 1.0), 1.0);
        p->setParam ("br_form", 1.0f); p->setParam ("br_formf", 0.5f);     // 400 * 2^1.65 = 1255 Hz
        auto body = render (*p, note (48, 0.0, 1.0), 1.0);
        const double f0 = juce::MidiMessage::getMidiNoteInHertz (48);
        auto band = [&] (const juce::AudioBuffer<float>& b, int h0, int h1) { double s = 0; for (int h = h0; h <= h1; ++h) s += std::pow (10.0, toneDb (b, 0.3, 0.9, f0 * h) / 10.0); return 10.0 * std::log10 (s); };
        const double gainBody = band (body, 8, 11) - band (plain, 8, 11), gainHigh = band (body, 20, 25) - band (plain, 20, 25);
        CHECK (gainBody > gainHigh + 4 && gainBody > 3, "FORMANT body at 1.25 kHz: harmonics 8-11 %+.1f dB, harmonics 20-25 %+.1f dB", gainBody, gainHigh);
    }
    {
        auto p = fresh(); p->setParam ("br_fall", 1.0f); p->setParam ("release", 0.6f);
        auto b = render (*p, note (60, 0.0, 0.6), 1.4);
        const double fHeld = pitchOf (b, 0.3, 0.55, 150, 400), fFall = pitchOf (b, 0.8, 0.9, 80, 400);
        CHECK (semisBetween (fHeld, fFall) < -3.0 && rmsDb (b, (int) (0.8 * SR), (int) (0.9 * SR)) > -60, "FALL: %.1f Hz held -> %.1f Hz 0.25 s into the release (%.1f st), level there %.1f dB",
               fHeld, fFall, semisBetween (fHeld, fFall), rmsDb (b, (int) (0.8 * SR), (int) (0.9 * SR)));
        CHECK (rmsDb (b, (int) (1.3 * SR), (int) (1.4 * SR)) < -70, "silent after the release (%.1f dB)", rmsDb (b, (int) (1.3 * SR), (int) (1.4 * SR)));
    }
    {
        // Growl as a mod destination: LFO1 square at 2 Hz -> growl on / off every 250 ms
        auto p = fresh(); p->setParam ("cutoff", 0.5f);
        p->setParam ("m1_src", (float) gpro::srcLfo1); p->setParam ("m1_dst", (float) gpro::dGrowl); p->setParam ("m1_amt", 1.0f);
        p->setParam ("l1_rate", 2.0f); p->setParam ("l1_shape", 4.0f); p->setParam ("l1_trig", 1.0f);
        auto b = render (*p, note (48, 0.0, 1.0), 1.0);
        const double wOn = wobble (b, 0.05, 0.22), wOff = wobble (b, 0.3, 0.47);
        CHECK (wOn > wOff + 1.0, "LFO -> Growl: wobble %.2f dB in the on half, %.2f dB in the off half", wOn, wOff);
    }
    // every brass preset renders clean
    for (auto* nm : { "Trap Horn Section", "Doit Fall Brass", "Growl Horn Lead", "Nasty Brass Distort", "Fanfare Stack", "Growl Brass Wobble" })
    {
        auto p = makeProc (progOf (nm));
        auto b = render (*p, testPhrase(), 6.0);
        int nans = 0; const float pk = peakOf (b, &nans);
        const double tail = rmsDb (b, (int) (5.6 * SR), (int) (6.0 * SR));
        const bool hasScene = gpro::factoryBank()[(size_t) progOf (nm)].scene.isNotEmpty();     // a dub delay / reverb scene is still ringing at 6 s
        CHECK (nans == 0 && pk <= 1.0f && lufs (b) > -24 && tail < (hasScene ? -30 : -50), "%-22s %d NaN, peak %.2f, %.1f LUFS, tail %.1f dB%s", nm, nans, pk, lufs (b), tail, hasScene ? " (FX scene)" : "");
    }
}

// ------------------------------------------------------------------ STATE
void testState()
{
    section ("STATE: trap parameters, layer sounds and the sample survive save / load; a plain sound saves no trap keys");
    {
        auto p = makeProc (0);
        juce::MemoryBlock mb; p->getStateInformation (mb);
        const auto xml = Peek::getXmlFromBinary (mb.getData(), (int) mb.getSize());
        const auto text = xml ? xml->toString() : juce::String();
        bool anyTrap = false;
        for (auto* k : { "br_", "ch_amt", "tp_", "ly1_", "ly2_", "ly3_", "stk_", "smp_", "TRAPSAMPLE" }) anyTrap |= text.contains (k);
        CHECK (! anyTrap && text.isNotEmpty(), "init-state XML (%d bytes) has no trap keys", (int) text.length());
    }
    auto p = makeProc (progOf ("Trap Starter Stack"));
    auto wav = makeTestSample(); juce::String msg;
    p->importTrapSample (wav, msg);
    p->setParam ("ly3_src", (float) gpro::lsSample); p->setParam ("ly3_rev", 1.0f); p->setParam ("ly3_oct", -1.0f); p->setParam ("ly3_det", -33.0f);
    p->setParam ("tp_on", 1.0f); p->setParam ("tp_amt", 0.42f); p->setParam ("ch_amt", 0.61f); p->setParam ("br_fall", 0.37f); p->setParam ("smp_loop", 1.0f); p->setParam ("smp_start", 0.11f);
    p->setParam ("v_same1", 1.0f); p->setParam ("v_steal", (float) gpro::stHighest);      // VOICING (round 3 item 6): part of the sound too
    settle (*p);
    juce::MemoryBlock mb; p->getStateInformation (mb);
    auto q = makeProc (0);
    q->setStateInformation (mb.getData(), (int) mb.getSize()); settle (*q);
    int same = 0, total = 0; juce::String bad;
    for (auto* prm : p->getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            ++total;
            auto* other = q->apvts.getParameter (rp->getParameterID());
            if (other != nullptr && std::abs (other->getValue() - rp->getValue()) < 1.0e-5f) ++same; else bad << rp->getParameterID() << " ";
        }
    CHECK (same == total, "%d / %d parameters equal after reload %s", same, total, bad.toRawUTF8());
    CHECK (q->sameNoteP->load() > 0.5f && (int) q->stealP->load() == gpro::stHighest, "VOICING reloads: 1 PER NOTE on, STEAL Highest");
    CHECK (q->layerSound (1) == p->layerSound (1) && q->layerSound (1) == "Boom Sub Long", "layer 2 sound restored: %s", q->layerSound (1).toRawUTF8());
    auto a = p->trapSample(), b = q->trapSample();
    float sd = 0; if (a && b && a->length() == b->length()) for (int i = 0; i < a->length(); ++i) sd = std::max (sd, std::abs (a->fwd[0][(size_t) i] - b->fwd[0][(size_t) i]));
    CHECK (a && b && a->length() == b->length() && a->root == b->root && std::abs (a->rootCents - b->rootCents) < 0.01f && a->name == b->name && sd < 1.0f / 30000.0f,
           "sample restored: %d samples, root %d, name %s, max diff %.2e (16-bit)", b ? b->length() : 0, b ? b->root : -1, b ? b->name.toRawUTF8() : "-", sd);
    auto ra = render (*p, testPhrase(), 4.0), rb = render (*q, testPhrase(), 4.0);
    CHECK (nansIn (ra) + nansIn (rb) == 0 && std::abs (lufs (ra) - lufs (rb)) < 1.5, "both play alike: %.1f vs %.1f LUFS", lufs (ra), lufs (rb));
    // user preset save / load keeps the layer sounds and the sample
    {
        const auto dir = ProProcessor::userPresetDir(); dir.createDirectory();
        CHECK (p->saveUserPreset ("_trap_test_sound"), "saveUserPreset");
        auto r = makeProc (0);
        auto f = dir.getChildFile ("_trap_test_sound.ghostypro");
        CHECK (r->loadUserPreset (f) && r->layerSound (1) == "Boom Sub Long" && r->trapSample() != nullptr && std::abs (r->chAmt->load() - 0.61f) < 1.0e-4f, "user sound reloads layers + sample + CHARACTER %.2f", r->chAmt->load());
        f.deleteFile();
    }
    // a state saved with a layer sound that no longer exists: loads, layer simply off
    {
        auto xml = Peek::getXmlFromBinary (mb.getData(), (int) mb.getSize());
        auto tree = juce::ValueTree::fromXml (*xml);
        tree.setProperty ("ly2_sound", "Gone Forever", nullptr); tree.setProperty ("ly2_sound_id", "Nowhere/99", nullptr);
        juce::MemoryBlock mb2; Peek::copyXmlToBinary (*tree.createXml(), mb2);
        auto r = makeProc (0); r->setStateInformation (mb2.getData(), (int) mb2.getSize()); settle (*r);
        auto out = render (*r, testPhrase(), 2.0);
        CHECK (r->layerSound (1).isEmpty() && nansIn (out) == 0, "unknown layer sound (and id) in a state: ignored, still plays (%d NaN)", nansIn (out));
    }
}

// ------------------------------------------------------------------ FINAL GATE punch list regressions
juce::File makeStereoSample()
{
    const int n = (int) SR; juce::AudioBuffer<float> b (2, n);
    for (int i = 0; i < n; ++i) { const double t = i / SR, e = std::exp (-t * 2.0); b.setSample (0, i, (float) (0.5 * e * std::sin (2 * juce::MathConstants<double>::pi * 220 * t))); b.setSample (1, i, (float) (0.5 * e * std::sin (2 * juce::MathConstants<double>::pi * 330 * t))); }
    auto f = scratchDir().getChildFile ("stereo_a3.wav"); writeWav (b, f); return f;
}
void testGate()
{
    section ("GATE: big host blocks, YIN on trimmed one-shots, DELAY-bypass stuck note, ghost tail, stereo + 16 voices, layer FX, mod targets, REV VEL, solo / mute, hidden trim, FLAC state, sound ids");
    {   // 2 blocker: a block 4x bigger than prepared, with the sample layer playing
        auto p = makeProc (progOf ("Flat Sub Pure")); juce::String msg; p->importTrapSample (makeTestSample(), msg); p->setParam ("ly2_src", (float) gpro::lsSample); settle (*p);
        auto q = makeProc (progOf ("Flat Sub Pure")); q->importTrapSample (makeTestSample(), msg); q->setParam ("ly2_src", (float) gpro::lsSample); settle (*q);
        juce::AudioBuffer<float> big (2, 2048), outA (2, 2048 * 10), outB (2, 2048 * 10), small (2, 512);
        for (int k = 0; k < 10; ++k)
        {
            juce::MidiBuffer m; if (k == 0) m.addEvent (juce::MidiMessage::noteOn (1, 57, 0.9f), 100);
            big.clear(); p->processBlock (big, m); for (int c = 0; c < 2; ++c) outA.copyFrom (c, k * 2048, big, c, 0, 2048);
            for (int j = 0; j < 4; ++j)
            {
                juce::MidiBuffer m2; if (k == 0 && j == 0) m2.addEvent (juce::MidiMessage::noteOn (1, 57, 0.9f), 100);
                small.clear(); q->processBlock (small, m2); for (int c = 0; c < 2; ++c) outB.copyFrom (c, k * 2048 + j * 512, small, c, 0, 512);
            }
        }
        CHECK (nansIn (outA) == 0 && rmsDb (outA, 4800, 20000) > -40 && maxDiff (outA, outB) < 1.0e-5f, "2048-sample blocks on a 512 prepare: %d NaN, %.1f dB RMS, max diff vs 512 blocks %.2e", nansIn (outA), rmsDb (outA, 4800, 20000), maxDiff (outA, outB));
    }
    {   // 3 blocker: YIN on zero-padded one-shots and at 96 kHz
        auto hit = [] (double hz, double secs, double rate, double total = 1.5) { std::vector<float> x ((size_t) (total * rate), 0.0f); for (int i = 0; i < (int) (secs * rate); ++i) x[(size_t) i] = (float) (std::sin (2 * juce::MathConstants<double>::pi * hz * i / rate) * std::exp (-i / rate / 0.2)); return x; };
        const double a = gpro::yinPitch (hit (110, 0.3, SR), SR), b = gpro::yinPitch (hit (55, 0.25, SR), SR), c = gpro::yinPitch (hit (55, 1.0, 96000), 96000), z = gpro::yinPitch (std::vector<float> ((size_t) SR, 0.0f), SR);
        CHECK (std::abs (a - 110) < 1.5 && std::abs (b - 55) < 1.0 && std::abs (c - 55) < 1.0 && z == 0, "YIN: 0.3 s 110 Hz + zeros -> %.1f Hz, 0.25 s 55 Hz + zeros -> %.1f Hz, 55 Hz at 96 k -> %.1f Hz, silence -> %.0f", a, b, c, z);
    }
    {   // 4 blocker: main DELAY automated to 0 while a note is pending
        auto p = makeProc (progOf ("Flat Sub Pure")); p->setParam ("verb", 0.0f); p->setParam ("echo", 0.0f); p->setParam ("ly1_dly", 0.5f);
        auto a = render (*p, { { 0.0, 48, 0.9f, true } }, 0.2);
        p->setParam ("ly1_dly", 0.0f);
        auto b = render (*p, { { 0.2, 48, 0, false } }, 1.0);
        p->setParam ("ly1_dly", 0.3f);
        auto c = render (*p, {}, 3.0);
        CHECK (rmsDb (c, (int) (2.0 * SR), (int) (3.0 * SR)) < -80 && p->voicesLive.load() == 0, "DELAY 0.5 -> 0 -> 0.3 around one key press: %.1f dB RMS afterwards, %d voices", rmsDb (c, (int) (2.0 * SR), (int) (3.0 * SR)), p->voicesLive.load());
    }
    {   // 5 major: a layer switched off and on again must not resume a frozen tail
        auto p = makeProc (progOf ("Fanfare Stack"));
        std::vector<Ev> evs; for (int n : { 55, 59, 62 }) { evs.push_back ({ 0.0, n, 0.9f, true }); evs.push_back ({ 1.0, n, 0, false }); }
        auto a = render (*p, evs, 1.05);
        p->setCurrentProgram (progOf ("Trap Horn Section")); settle (*p);
        auto b = render (*p, {}, 1.0);
        p->setCurrentProgram (progOf ("Fanfare Stack")); settle (*p);
        auto c = render (*p, {}, 1.0);
        // where does whatever is left come from? the same switch with the layers muted / with the main Ghost FX dry
        auto p2 = makeProc (progOf ("Fanfare Stack")); render (*p2, evs, 1.05); p2->setCurrentProgram (progOf ("Trap Horn Section")); settle (*p2); render (*p2, {}, 1.0);
        p2->setCurrentProgram (progOf ("Fanfare Stack")); settle (*p2); p2->layerMute[1] = p2->layerMute[2] = true; auto c2 = render (*p2, {}, 1.0);
        auto p3 = makeProc (progOf ("Fanfare Stack")); render (*p3, evs, 1.05); p3->setCurrentProgram (progOf ("Trap Horn Section")); settle (*p3); render (*p3, {}, 1.0);
        p3->setCurrentProgram (progOf ("Fanfare Stack")); settle (*p3); for (auto* id : { "verb", "echo", "chorus", "phaser", "haunt" }) p3->setParam (id, 0.0f); auto c3 = render (*p3, {}, 1.0);
        printf ("      before the switch back %.1f dB; after: %.1f dB, layers muted %.1f dB, main FX dry %.1f dB\n", rmsDb (b, (int) (0.9 * SR), (int) SR), rmsDb (c, 0, (int) (0.25 * SR)), rmsDb (c2, 0, (int) (0.25 * SR)), rmsDb (c3, 0, (int) (0.25 * SR)));
        CHECK (rmsDb (c, 0, (int) (0.25 * SR)) < rmsDb (b, (int) (0.9 * SR), (int) SR) + 6 && rmsDb (c, 0, (int) (0.25 * SR)) < -50, "ghost tail: %.1f dB RMS right after the stack comes back, %.1f dB just before (was +24 dB / -27 dB)", rmsDb (c, 0, (int) (0.25 * SR)), rmsDb (b, (int) (0.9 * SR), (int) SR));
    }
    {   // 9 major: stereo sample, 16 voices
        auto p = makeProc (progOf ("Flat Sub Pure")); p->setParam ("verb", 0.0f); p->setParam ("echo", 0.0f); p->setParam ("ly1_lvl", 0.0f);
        juce::String msg; p->importTrapSample (makeStereoSample(), msg); settle (*p);
        auto d = p->trapSample();
        auto b = render (*p, note (d ? d->root : 57, 0.02, 0.5), 0.8);      // the 220 + 330 Hz mix reads as 110 Hz (A2): play the root
        juce::AudioBuffer<float> L (2, b.getNumSamples()), R (2, b.getNumSamples());
        for (int c = 0; c < 2; ++c) { L.copyFrom (c, 0, b, 0, 0, b.getNumSamples()); R.copyFrom (c, 0, b, 1, 0, b.getNumSamples()); }
        CHECK (d && d->chans == 2 && toneDb (L, 0.1, 0.4, 220) > toneDb (L, 0.1, 0.4, 330) + 15 && toneDb (R, 0.1, 0.4, 330) > toneDb (R, 0.1, 0.4, 220) + 15,
               "stereo sample stays stereo: left 220 Hz %.1f / 330 Hz %.1f dB, right 220 Hz %.1f / 330 Hz %.1f dB (%s)", toneDb (L, 0.1, 0.4, 220), toneDb (L, 0.1, 0.4, 330), toneDb (R, 0.1, 0.4, 220), toneDb (R, 0.1, 0.4, 330), msg.toRawUTF8());
        std::vector<Ev> many; for (int i = 0; i < 14; ++i) many.push_back ({ 0.01 * i, 40 + i * 2, 0.9f, true });
        auto held = render (*p, many, 0.6);
        CHECK (p->layerVoices[1].load() == 14 && nansIn (held) == 0, "14 sample notes held: %d voices live (16 available)", p->layerVoices[1].load());
        render (*p, { { 0.0, 1, 0, false } }, 0.1);
    }
    {   // 11 major: LAYER FX off by default (dry layer), on keeps the layer's own verb
        auto p = makeProc (progOf ("Flat Sub Pure")); p->setParam ("verb", 0.0f); p->setParam ("echo", 0.0f); p->setParam ("ly1_lvl", 0.0f);
        p->setLayerSound (1, "Dark Choir"); settle (*p);
        auto dry = render (*p, note (60, 0.02, 0.5), 2.5);
        p->setParam ("ly2_fx", 1.0f);
        auto wet = render (*p, note (60, 0.02, 0.5), 2.5);
        const double tDry = rmsDb (dry, (int) (2.3 * SR), (int) (2.5 * SR)), tWet = rmsDb (wet, (int) (2.3 * SR), (int) (2.5 * SR));
        CHECK (tWet > tDry + 6, "LAYER FX: tail 1.8 s after note-off %.1f dB dry (default) vs %.1f dB with the layer's own verb", tDry, tWet);
    }
    {   // 12 major: macro -> CHARACTER, LFO -> layer 2 level
        auto p = makeProc (progOf ("Flat Sub Pure")); p->setParam ("verb", 0.0f); p->setParam ("echo", 0.0f);
        p->setParam ("m1_src", (float) gpro::srcMac1); p->setParam ("m1_dst", (float) gpro::dCharacter); p->setParam ("m1_amt", 1.0f);
        p->setParam ("mac1", 0.0f);
        auto clean = render (*p, note (41, 0.02, 1.0), 1.2);
        p->setParam ("mac1", 1.0f);
        auto dirty = render (*p, note (41, 0.02, 1.0), 1.2);
        const double f0 = juce::MidiMessage::getMidiNoteInHertz (41);
        CHECK (toneDb (dirty, 0.3, 0.9, 3 * f0) > toneDb (clean, 0.3, 0.9, 3 * f0) + 20, "Macro 1 -> Character: 3rd harmonic %.1f dB (clean %.1f dB)", toneDb (dirty, 0.3, 0.9, 3 * f0), toneDb (clean, 0.3, 0.9, 3 * f0));
        p->setParam ("mac1", 0.0f); p->setLayerSound (1, "Flat Sub Pure"); p->setParam ("ly2_oct", 1.0f); settle (*p);
        p->setParam ("m1_src", (float) gpro::srcLfo1); p->setParam ("m1_dst", (float) gpro::dLy2Lvl); p->setParam ("m1_amt", -1.0f);
        p->setParam ("l1_rate", 1.0f); p->setParam ("l1_shape", 4.0f); p->setParam ("l1_trig", 1.0f);
        auto b = render (*p, note (48, 0.0, 1.0), 1.0);
        const double hi = toneDb (b, 0.05, 0.45, 2 * f0 * std::exp2 (7.0 / 12.0)), lo = toneDb (b, 0.55, 0.95, 2 * f0 * std::exp2 (7.0 / 12.0));
        CHECK (hi < lo - 15, "LFO 1 (square 1 Hz) -> Layer 2 Level: octave layer %.1f dB in the first half, %.1f dB in the second", hi, lo);
    }
    {   // 27 minor: REV VEL - hard notes play the sample backwards
        auto p = makeProc (progOf ("Flat Sub Pure")); p->setParam ("verb", 0.0f); p->setParam ("echo", 0.0f); p->setParam ("ly1_lvl", 0.0f);
        juce::String msg; p->importTrapSample (makeTestSample(), msg); p->setParam ("smp_revvel", 0.7f); settle (*p);
        auto soft = render (*p, note (57, 0.02, 1.1, 0.5f), 1.4), hard = render (*p, note (57, 0.02, 1.1, 0.95f), 1.4);
        const double sE = rmsDb (soft, (int) (0.15 * SR), (int) (0.35 * SR)), sL = rmsDb (soft, (int) (0.75 * SR), (int) (0.95 * SR));
        const double hE = rmsDb (hard, (int) (0.15 * SR), (int) (0.35 * SR)), hL = rmsDb (hard, (int) (0.75 * SR), (int) (0.95 * SR));
        CHECK (sL < sE && hL > hE + 6, "REV VEL 0.7: vel 0.5 decays (%.1f -> %.1f dB), vel 0.95 rises (%.1f -> %.1f dB)", sE, sL, hE, hL);
    }
    {   // 25 minor: solo / mute
        auto p = makeProc (progOf ("Flat Sub Pure")); p->setParam ("verb", 0.0f); p->setParam ("echo", 0.0f); p->setLayerSound (1, "Flat Sub Pure"); p->setParam ("ly2_oct", 1.0f); settle (*p);
        const double f1 = juce::MidiMessage::getMidiNoteInHertz (48);
        p->layerMute[0] = true;  auto m = render (*p, note (48, 0.02, 0.8), 1.0); p->layerMute[0] = false;
        p->layerSolo[1] = true;  auto so = render (*p, note (48, 0.02, 0.8), 1.0); p->layerSolo[1] = false;
        auto both = render (*p, note (48, 0.02, 0.8), 1.0);
        CHECK (toneDb (m, 0.2, 0.7, f1) < toneDb (both, 0.2, 0.7, f1) - 40 && toneDb (so, 0.2, 0.7, f1) < toneDb (both, 0.2, 0.7, f1) - 40 && toneDb (so, 0.2, 0.7, 2 * f1) > toneDb (both, 0.2, 0.7, 2 * f1) - 1,
               "mute main: %.1f dB at the main's pitch; solo layer 2: main %.1f dB, layer %.1f dB (both %.1f / %.1f)", toneDb (m, 0.2, 0.7, f1), toneDb (so, 0.2, 0.7, f1), toneDb (so, 0.2, 0.7, 2 * f1), toneDb (both, 0.2, 0.7, f1), toneDb (both, 0.2, 0.7, 2 * f1));
    }
    {   // 21 minor: the trim is a hidden gain, VOLUME sits at 0 dB; an older state (trim baked into VOLUME, no trimDb) plays the same
        auto p = makeProc (progOf ("Flat Sub Pure"));
        const float trim = gpro::presetTrimDb ("Flat Sub Pure");
        CHECK (std::abs (p->gain->load() - 0.5f) < 1.0e-6f && std::abs (juce::Decibels::gainToDecibels (p->trimGain.load()) - trim) < 0.01f, "VOLUME %.2f (0 dB), hidden trim %.2f dB", p->gain->load(), juce::Decibels::gainToDecibels (p->trimGain.load()));
        juce::MemoryBlock mb; p->getStateInformation (mb);
        auto xml = Peek::getXmlFromBinary (mb.getData(), (int) mb.getSize()); auto tree = juce::ValueTree::fromXml (*xml);
        tree.removeProperty ("trimDb", nullptr);
        tree.removeChild (tree.getChildWithProperty ("id", "out_trim"), nullptr); tree.removeChild (tree.getChildWithProperty ("id", "cl_on"), nullptr);   // (a truly older state: no TRIM knob, no CLEAN switch)
        auto g = tree.getChildWithProperty ("id", "gain"); g.setProperty ("value", std::sqrt (juce::Decibels::decibelsToGain (trim) / 4.0f), nullptr);
        juce::MemoryBlock mb2; Peek::copyXmlToBinary (*tree.createXml(), mb2);
        auto q = makeProc (0); q->setStateInformation (mb2.getData(), (int) mb2.getSize()); settle (*q);
        auto ra = render (*p, note (41, 0.02, 1.0), 1.5), rb = render (*q, note (41, 0.02, 1.0), 1.5);
        CHECK (std::abs (q->trimGain.load() - 1.0f) < 1.0e-6f && maxDiff (ra, rb) < 2.0e-4f, "older state with the trim in VOLUME: hidden trim %.2f, max diff vs the fresh preset %.2e", q->trimGain.load(), maxDiff (ra, rb));
    }
    {   // 10 major: FLAC in the state (smaller than raw PCM), stereo survives; 28 minor: stable sound id
        auto p = makeProc (progOf ("Flat Sub Pure")); juce::String msg; p->importTrapSample (makeStereoSample(), msg); p->setLayerSound (1, "Boom Sub Long"); settle (*p);
        juce::MemoryBlock mb; p->getStateInformation (mb);
        const int pcm = (int) (p->trapSample()->length() * 2 * 2 * 4 / 3);
        auto q = makeProc (0); q->setStateInformation (mb.getData(), (int) mb.getSize()); settle (*q);
        auto d = q->trapSample();
        CHECK ((int) mb.getSize() < pcm && d && d->chans == 2 && d->length() == (int) SR && d->path.isNotEmpty(), "state %d kB (raw PCM base64 would be %d kB), reloaded: %d ch, %d samples, path kept", (int) mb.getSize() / 1024, pcm / 1024, d ? d->chans : 0, d ? d->length() : 0);
        auto xml = Peek::getXmlFromBinary (mb.getData(), (int) mb.getSize()); auto tree = juce::ValueTree::fromXml (*xml);
        CHECK (tree.getProperty ("ly2_sound_id").toString() == ProProcessor::soundIdOf ("Boom Sub Long") && ProProcessor::nameOfSoundId (tree.getProperty ("ly2_sound_id").toString()) == "Boom Sub Long", "sound id %s", tree.getProperty ("ly2_sound_id").toString().toRawUTF8());
        tree.setProperty ("ly2_sound", "Renamed Away", nullptr);
        juce::MemoryBlock mb2; Peek::copyXmlToBinary (*tree.createXml(), mb2);
        auto r = makeProc (0); r->setStateInformation (mb2.getData(), (int) mb2.getSize()); settle (*r);
        CHECK (r->layerSound (1) == "Boom Sub Long", "a renamed layer sound is found by its id: %s", r->layerSound (1).toRawUTF8());
    }
    {   // 19 minor: import with both layers busy takes layer 3 and says so
        auto p = makeProc (progOf ("Fanfare Stack")); juce::String msg; p->importTrapSample (makeTestSample(), msg);
        CHECK ((int) p->ly[2].src->load() == gpro::lsSample && msg.contains ("layer 3"), "both layers were sounds: %s", msg.toRawUTF8());
    }
    {   // 7 major: CHARACTER keeps the sub, aliasing down
        auto sineLvl = [] (double hz, float amt) { gpro::Character c; c.prepare (SR, 512); juce::AudioBuffer<float> b (2, (int) SR * 2); for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (ch, i, 0.4f * (float) std::sin (2 * juce::MathConstants<double>::pi * hz * i / SR)); const double in = rmsDb (b, (int) SR, 2 * (int) SR); for (int pos = 0; pos < b.getNumSamples(); pos += 512) { juce::AudioBuffer<float> blk (b.getArrayOfWritePointers(), 2, pos, juce::jmin (512, b.getNumSamples() - pos)); c.process (blk, amt); } return rmsDb (b, (int) SR, 2 * (int) SR) - in; };
        const double s33 = sineLvl (33.0, 0.3), s33b = sineLvl (33.0, 0.6), s1k = sineLvl (1000.0, 0.3);
        CHECK (s33 > s1k - 0.5 && s33b > sineLvl (1000.0, 0.6) - 0.5, "sub: 33 Hz %+.2f dB vs 1 kHz %+.2f dB at 0.3; %+.2f dB at 0.6 (was -2.8)", s33, s1k, s33b);
        constexpr int N = 1 << 16; gpro::Character c; c.prepare (SR, 512);
        juce::AudioBuffer<float> b (2, N + 4096); const double f0 = 1234.5;
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (ch, i, 0.5f * (float) std::sin (2 * juce::MathConstants<double>::pi * f0 * i / SR));
        for (int pos = 0; pos < b.getNumSamples(); pos += 512) { juce::AudioBuffer<float> blk (b.getArrayOfWritePointers(), 2, pos, juce::jmin (512, b.getNumSamples() - pos)); c.process (blk, 1.0f); }
        juce::dsp::FFT fft (16); std::vector<float> buf ((size_t) N * 2, 0.0f);
        for (int i = 0; i < N; ++i) buf[(size_t) i] = b.getSample (0, 4096 + i) * (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / N));
        fft.performFrequencyOnlyForwardTransform (buf.data());
        const double binHz = SR / N; double fund = 0, nonharm = 0, worst = 0;
        for (int k = 2; k < N / 2; ++k)
        {
            const double f = k * binHz, ratio = f / f0, near = std::abs (ratio - std::round (ratio)) * f0, e = (double) buf[(size_t) k] * buf[(size_t) k];
            if (std::abs (f - f0) < 3 * binHz) fund += e; else if (near >= 4 * binHz) { nonharm += e; worst = std::max (worst, e); }
        }
        CHECK (10 * std::log10 (nonharm / fund) < -40 && 10 * std::log10 (worst / fund) < -45, "aliasing at full CHARACTER (1234.5 Hz, 2x oversampled): non-harmonic %.1f dB, worst line %.1f dB re the fundamental (was -26 / -34)", 10 * std::log10 (nonharm / fund), 10 * std::log10 (worst / fund));
    }
    {   // 6 major: note-on cost on the audio thread (priming is budgeted inside render)
        gpro::SampleEngine eng; eng.prepare (SR, 64);
        auto d = std::make_shared<gpro::SampleData>(); d->rate = SR; d->root = 33; d->chans = 1; d->fwd[0].assign ((size_t) SR, 0.0f);
        for (int i = 0; i < (int) SR; ++i) d->fwd[0][(size_t) i] = (float) (std::sin (2 * juce::MathConstants<double>::pi * 55.0 * i / SR) * std::exp (-i / SR / 0.4));
        d->rev[0].assign (d->fwd[0].rbegin(), d->fwd[0].rend());
        gpro::SampleParams sp; sp.root = 33; for (auto& l : sp.lvl) l = 1.0f;
        juce::AudioBuffer<float> blk (2, 64);
        const auto t0 = juce::Time::getHighResolutionTicks();
        for (int v = 0; v < 6; ++v) eng.noteOn (1, 40 + v, 1.0f, sp, *d, (juce::uint32) v + 1);
        const double onMs = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0) * 1000;
        double worst = 0, steady = 0;
        for (int k = 0; k < 40; ++k)
        {
            blk.clear(); const auto t1 = juce::Time::getHighResolutionTicks(); eng.render (blk, 64, sp, d.get());
            const double ms = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t1) * 1000;
            if (k < 8) worst = std::max (worst, ms); else steady = std::max (steady, ms);
        }
        CHECK (onMs < 0.1 && worst < 1.2 && steady < 0.6, "6 note-ons %.3f ms, worst 64-sample render while they prime %.2f ms, steady %.2f ms (budget 1.33 ms at 48 k)", onMs, worst, steady);
    }
}

// ------------------------------------------------------------------ BYPASS (Shared/Bypass.h wiring)
void testBypass()
{
    section ("BYPASS: last parameter, host bypass, 6 ms fade to exact silence, nothing stuck or bursting back, never part of a sound");
    auto fresh = [] { auto p = makeProc (progOf ("Flat Sub Pure")); p->setParam ("verb", 0.0f); p->setParam ("echo", 0.0f); return p; };
    auto p = fresh();
    CHECK (p->getBypassParameter() != nullptr && p->getBypassParameter() == p->apvts.getParameter ("bypass") && p->getBypassParameter()->isAutomatable() && p->getBypassParameter()->getParameterIndex() == 507,
           "getBypassParameter() = the automatable 'bypass' parameter, index %d of %d (after every older one; only the MORPH puck follows)", p->getBypassParameter()->getParameterIndex(), p->getParameters().size());
    {   // off = bit-identical: a processor that was bypassed and released before the phrase renders exactly like one never touched
        auto q = fresh(); q->setParam ("bypass", 1.0f); render (*q, {}, 0.2); q->setParam ("bypass", 0.0f); render (*q, {}, 0.2);
        auto a = render (*p, note (48, 0.05, 1.0), 1.5), b = render (*q, note (48, 0.05, 1.0), 1.5);
        CHECK (maxDiff (a, b) == 0.0f && rmsDb (a, (int) (0.3 * SR), (int) (0.9 * SR)) > -30, "off: max diff %.3g vs a never-bypassed processor", maxDiff (a, b));
    }
    {   // engage while a chord is held: 6 ms fade, then exact zeros and no CPU; release: nothing comes back on its own, no stuck notes
        auto q = fresh();
        std::vector<Ev> on { { 0.0, 48, 0.9f, true }, { 0.0, 55, 0.9f, true }, { 0.0, 60, 0.9f, true } };
        auto a = render (*q, on, 0.5);
        float stepTone = 0; for (int i = 1; i < a.getNumSamples(); ++i) stepTone = std::max (stepTone, std::abs (a.getSample (0, i) - a.getSample (0, i - 1)));
        q->setParam ("bypass", 1.0f);
        auto b = render (*q, {}, 0.5);               // (render has a 100 ms silent pre-roll: the fade sits inside it; measure a block-exact run instead)
        juce::AudioBuffer<float> blk (2, 512), fade (2, 4800); fade.clear();
        auto r = fresh(); render (*r, on, 0.5); r->setParam ("bypass", 1.0f);
        for (int pos = 0; pos < 4800; pos += 512) { juce::MidiBuffer m; blk.clear(); r->processBlock (blk, m); for (int c = 0; c < 2; ++c) fade.copyFrom (c, pos, blk, c, 0, juce::jmin (512, 4800 - pos)); }
        float stepFade = 0; for (int i = 1; i < 480; ++i) stepFade = std::max (stepFade, std::abs (fade.getSample (0, i) - fade.getSample (0, i - 1)));
        float after = 0; for (int i = (int) (0.02 * SR); i < 4800; ++i) after = std::max (after, std::abs (fade.getSample (0, i)));
        CHECK (after == 0.0f && stepFade < stepTone * 1.5f + 1.0e-3f, "engaged on a held chord: exact 0 from 20 ms on (peak %.3g), fade step %.4f vs the tone's own %.4f, %d voices", after, stepFade, stepTone, r->voicesLive.load());
        CHECK (rmsDb (b, (int) (0.1 * SR), (int) (0.5 * SR)) < -120 && r->voicesLive.load() == 0, "bypassed: %.1f dB RMS, %d voices", rmsDb (b, (int) (0.1 * SR), (int) (0.5 * SR)), r->voicesLive.load());
        auto c = render (*r, { { 0.1, 50, 0.9f, true } }, 0.5);       // a note arriving while bypassed is ignored
        r->setParam ("bypass", 0.0f);
        auto d = render (*r, {}, 1.0);
        CHECK (rmsDb (d, 0, (int) SR) < -90 && r->voicesLive.load() == 0, "released with the keys still 'down': %.1f dB RMS, %d voices (nothing stuck, the ignored note stays ignored)", rmsDb (d, 0, (int) SR), r->voicesLive.load());
        auto e = render (*r, note (48, 0.02, 0.5), 0.8);
        CHECK (rmsDb (e, (int) (0.1 * SR), (int) (0.4 * SR)) > -30, "plays again after release: %.1f dB RMS", rmsDb (e, (int) (0.1 * SR), (int) (0.4 * SR)));
    }
    {   // the FX rack / Ghost FX tail must not burst back out when BYPASS is released (space scene = long reverb)
        auto q = makeProc (progOf ("Dark Choir"));
        auto a = render (*q, note (60, 0.0, 0.6) + note (67, 0.0, 0.6), 0.9);
        q->setParam ("bypass", 1.0f); render (*q, {}, 0.3);
        q->setParam ("bypass", 0.0f);
        juce::AudioBuffer<float> blk (2, 512), back (2, 9600); back.clear();
        for (int pos = 0; pos < 9600; pos += 512) { juce::MidiBuffer m; blk.clear(); q->processBlock (blk, m); for (int c = 0; c < 2; ++c) back.copyFrom (c, pos, blk, c, 0, juce::jmin (512, 9600 - pos)); }
        // where does whatever comes back live? the same run with the rack scene dry / the main Ghost FX dry / both
        auto variant = [&] (bool rackDry, bool fxDry)
        {
            auto v = makeProc (progOf ("Dark Choir"));
            if (rackDry) v->rack.applyScene (v->apvts, "dry");
            if (fxDry) for (auto* id : { "verb", "echo", "chorus", "phaser", "haunt" }) v->setParam (id, 0.0f);
            settle (*v);
            render (*v, note (60, 0.0, 0.6) + note (67, 0.0, 0.6), 0.9);
            v->setParam ("bypass", 1.0f); render (*v, {}, 0.3); v->setParam ("bypass", 0.0f);
            juce::AudioBuffer<float> bb (2, 9600); bb.clear(); juce::AudioBuffer<float> bk (2, 512);
            for (int pos = 0; pos < 9600; pos += 512) { juce::MidiBuffer m; bk.clear(); v->processBlock (bk, m); for (int c = 0; c < 2; ++c) bb.copyFrom (c, pos, bk, c, 0, juce::jmin (512, 9600 - pos)); }
            return rmsDb (bb, 0, 9600);
        };
        printf ("      after release: %.1f dB; rack dry %.1f dB; main FX dry %.1f dB; both dry %.1f dB\n", rmsDb (back, 0, 9600), variant (true, false), variant (false, true), variant (true, true));
        CHECK (peakOf (back) == 0.0f, "reverb scene: peak %.3g (%.1f dB RMS) in the 200 ms after release, exact silence (tail before bypass was %.1f dB)", peakOf (back), rmsDb (back, 0, 9600), rmsDb (a, (int) (0.8 * SR), (int) (0.9 * SR)));
    }
    // block-exact runs (no pre-roll): what really comes out right after an engage / a release
    auto run = [] (ProProcessor& pr, double secs, const std::vector<Ev>& evs = {})
    {
        const int total = (int) (secs * SR); juce::AudioBuffer<float> out (2, total), bk (2, BS); out.clear();
        for (int pos = 0; pos < total; pos += BS)
        {
            const int len = juce::jmin (BS, total - pos); juce::MidiBuffer m;
            for (auto& e : evs) { const int sp = (int) (e.t * SR); if (sp >= pos && sp < pos + len) m.addEvent (e.on ? juce::MidiMessage::noteOn (1, e.note, e.vel) : juce::MidiMessage::noteOff (1, e.note), sp - pos); }
            bk.setSize (2, len, false, false, true); bk.clear(); pr.processBlock (bk, m);
            for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, bk, c, 0, len);
        }
        return out;
    };
    auto maxStep = [] (const juce::AudioBuffer<float>& x, int s0, int s1) { float m = 0; for (int c = 0; c < 2; ++c) for (int i = juce::jmax (1, s0); i < s1; ++i) m = std::max (m, std::abs (x.getSample (c, i) - x.getSample (c, i - 1))); return m; };
    auto silentFrom = [] (const juce::AudioBuffer<float>& x) { int last = -1; for (int c = 0; c < 2; ++c) for (int i = 0; i < x.getNumSamples(); ++i) if (std::abs (x.getSample (c, i)) > 1.0e-6f) last = std::max (last, i); return (last + 1) / SR * 1000.0; };
    {   // STACK layers 2/3 (their own voices + FX) + a layer DELAY note still queued, engaged mid-phrase through the host's bypass
        auto q = makeProc (progOf ("Trap Starter Stack"));
        q->setParam ("ly2_dly", 0.3f); settle (*q);
        auto pre = run (*q, 0.5, note (40, 0.0, 2.0) + note (72, 0.0, 2.0) + note (43, 0.45, 2.0));   // note 43's layer-2 copy lands at 0.75 s
        const float stepMusic = maxStep (pre, 0, pre.getNumSamples());
        const int v0 = q->layerVoices[0].load(), v1 = q->layerVoices[1].load(), v2 = q->layerVoices[2].load();
        q->getBypassParameter()->setValueNotifyingHost (1.0f);          // the host's own bypass button
        auto fade = run (*q, 0.5);
        const double dbAfter20 = rmsDb (fade, (int) (0.02 * SR), fade.getNumSamples());
        CHECK (dbAfter20 < -120 && silentFrom (fade) <= 20.0 && maxStep (fade, 0, (int) (0.02 * SR)) < stepMusic * 1.5f + 1.0e-3f,
               "STACK engaged mid-phrase (layers %d/%d/%d voices before): silent (>1e-6) after %.1f ms, %.1f dB from 20 ms on; fade step %.4f vs the music's own %.4f",
               v0, v1, v2, silentFrom (fade), dbAfter20, maxStep (fade, 0, (int) (0.02 * SR)), stepMusic);
        q->getBypassParameter()->setValueNotifyingHost (0.0f);
        auto back = run (*q, 1.0);
        CHECK (peakOf (back) == 0.0f && q->layerVoices[0].load() + q->layerVoices[1].load() + q->layerVoices[2].load() == 0,
               "released after the queued DELAY note's time: peak %.3g over 1 s, voices %d/%d/%d (no stuck notes, no queued note landing, no layer FX tail)", peakOf (back), q->layerVoices[0].load(), q->layerVoices[1].load(), q->layerVoices[2].load());
        auto again = run (*q, 0.6, note (40, 0.0, 0.5) + note (72, 0.0, 0.5));
        CHECK (rmsDb (again, (int) (0.1 * SR), (int) (0.5 * SR)) > -30 && q->layerVoices[1].load() + q->layerVoices[2].load() > 0, "plays again with its layers: %.1f dB RMS, layer voices %d/%d", rmsDb (again, (int) (0.1 * SR), (int) (0.5 * SR)), q->layerVoices[1].load(), q->layerVoices[2].load());
    }
    {   // SAMPLE layer with a long release + loop, held while engaged
        auto q = makeProc (progOf ("Flat Sub Pure")); q->setParam ("verb", 0.0f); q->setParam ("echo", 0.0f);
        juce::String msg; q->importTrapSample (makeTestSample(), msg); q->setParam ("smp_loop", 1.0f); q->setParam ("smp_r", 8.0f); settle (*q);
        auto pre = run (*q, 0.5, note (57, 0.0, 0.3));                  // released at 0.3 s: the sample rings on (8 s release)
        const float stepMusic = maxStep (pre, 0, pre.getNumSamples());
        q->setParam ("bypass", 1.0f);
        auto fade = run (*q, 0.3);
        q->setParam ("bypass", 0.0f);
        auto back = run (*q, 0.5);
        CHECK (rmsDb (pre, (int) (0.35 * SR), (int) (0.5 * SR)) > -40 && silentFrom (fade) <= 20.0 && maxStep (fade, 0, 960) < stepMusic * 1.5f + 1.0e-3f && peakOf (back) == 0.0f && q->layerVoices[1].load() == 0,
               "SAMPLE layer ringing at %.1f dB: silent after %.1f ms, fade step %.4f vs %.4f, after release peak %.3g, sample voices %d",
               rmsDb (pre, (int) (0.35 * SR), (int) (0.5 * SR)), silentFrom (fade), maxStep (fade, 0, 960), stepMusic, peakOf (back), q->layerVoices[1].load());
        auto again = run (*q, 0.4, note (57, 0.0, 0.3));
        CHECK (rmsDb (again, (int) (0.05 * SR), (int) (0.3 * SR)) > -40, "the sample plays again: %.1f dB RMS", rmsDb (again, (int) (0.05 * SR), (int) (0.3 * SR)));
    }
    {   // release while the keys are still held and the host keeps sending: no burst, a new note-on starts cleanly with the fade-in
        auto q = fresh();
        run (*q, 0.3, note (48, 0.0, 5.0));
        q->setParam ("bypass", 1.0f); run (*q, 0.1); q->setParam ("bypass", 0.0f);
        auto x = run (*q, 0.3, note (55, 0.05, 0.2));
        const float stepPlay = maxStep (x, (int) (0.1 * SR), (int) (0.25 * SR));
        CHECK (peakOf (x) > 0.05f && maxStep (x, 0, (int) (0.1 * SR)) < stepPlay * 1.5f + 1.0e-3f && peakOf (juce::AudioBuffer<float> (x.getArrayOfWritePointers(), 2, 0, (int) (0.05 * SR))) == 0.0f,
               "un-bypassed mid-phrase: silence until the next note-on (old held note stays gone), onset step %.4f vs the note's own %.4f", maxStep (x, 0, (int) (0.1 * SR)), stepPlay);
    }
    {   // CPU: fully bypassed nothing renders
        auto q = makeProc (progOf ("Trap Starter Stack"));
        auto timeIt = [&] (int blocks, bool hold)
        {
            juce::AudioBuffer<float> bk (2, BS); juce::MidiBuffer on; if (hold) for (int nn : { 40, 52, 64, 72 }) on.addEvent (juce::MidiMessage::noteOn (1, nn, 0.9f), 0);
            const auto t0 = juce::Time::getHighResolutionTicks();
            for (int k = 0; k < blocks; ++k) { juce::MidiBuffer m; if (k == 0) m = on; bk.clear(); q->processBlock (bk, m); }
            return juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0) / (blocks * BS / SR) * 100.0;
        };
        const double playing = timeIt (400, true);
        q->setParam ("bypass", 1.0f); timeIt (10, false);
        const double bypassed = timeIt (400, true);
        CHECK (bypassed < 0.5 && bypassed < playing * 0.05, "CPU: %.2f %% of real time playing a 4-note STACK chord, %.3f %% bypassed (notes sent while bypassed are ignored)", playing, bypassed);
    }
    {   // state: round trip keeps it; a state from before BYPASS existed loads with it off; sounds / programs / INIT / user sounds never touch it
        auto q = fresh(); q->setParam ("bypass", 1.0f);
        juce::MemoryBlock mb; q->getStateInformation (mb);
        auto r = fresh(); r->setStateInformation (mb.getData(), (int) mb.getSize()); settle (*r);
        auto xml = Peek::getXmlFromBinary (mb.getData(), (int) mb.getSize()); auto tree = juce::ValueTree::fromXml (*xml);
        auto c = tree.getChildWithProperty ("id", "bypass"); tree.removeChild (c, nullptr);
        juce::MemoryBlock mb2; Peek::copyXmlToBinary (*tree.createXml(), mb2);
        auto s = fresh(); s->setStateInformation (mb2.getData(), (int) mb2.getSize()); settle (*s);
        CHECK (r->bypassParam->get() && ! s->bypassParam->get(), "round trip keeps BYPASS on; a pre-BYPASS state loads with it off");
        int browsed = 0, flips = 0;
        for (int k = 0; k < q->getNumPrograms(); k += 3) { q->setCurrentProgram (k); ++browsed; flips += q->bypassParam->get() ? 0 : 1; }
        q->initPatch(); q->resetMasterLimiter(); q->rack.applyScene (q->apvts, "space"); q->resetSoundParams();
        CHECK (q->bypassParam->get() && flips == 0, "still on after browsing %d of %d factory sounds (%d changed it) + INIT + a scene + sound reset", browsed, q->getNumPrograms(), flips);
        {   // a real state saved by the build before BYPASS existed
            const juce::File old ("/root/work/trap/ref/dump/808_Boom.state");
            if (old.existsAsFile())
            {
                juce::MemoryBlock ob; old.loadFileAsData (ob);
                auto o = fresh(); o->setParam ("bypass", 1.0f); o->setStateInformation (ob.getData(), (int) ob.getSize()); settle (*o);
                CHECK (! o->bypassParam->get(), "a session saved by the pre-BYPASS build loads with BYPASS off (even over an instance that was bypassed)");
            }
        }
        {
            const juce::File f = ProProcessor::userPresetDir().getChildFile ("_bypass_strip.ghostypro");
            q->saveUserPreset ("_bypass_strip");
            CHECK (! f.loadFileAsString().contains ("\"bypass\""), "a user sound saved while bypassed doesn't contain the BYPASS parameter");
            f.deleteFile();
        }
        const auto dir = ProProcessor::userPresetDir(); dir.createDirectory();
        q->saveUserPreset ("_bypass_test_sound");                     // saved while bypassed
        auto t = fresh(); t->loadUserPreset (dir.getChildFile ("_bypass_test_sound.ghostypro"));
        auto u = fresh(); u->setParam ("bypass", 1.0f); u->setParam ("bypass", 1.0f); auto plain = makeProc (0); plain->saveUserPreset ("_bypass_test_plain"); u->loadUserPreset (dir.getChildFile ("_bypass_test_plain.ghostypro"));
        CHECK (! t->bypassParam->get() && u->bypassParam->get(), "user sounds: loading one saved while bypassed leaves it off; loading a plain one while bypassed leaves it on");
        dir.getChildFile ("_bypass_test_sound.ghostypro").deleteFile(); dir.getChildFile ("_bypass_test_plain.ghostypro").deleteFile();
    }
}

// ------------------------------------------------------------------ CPU
void testCpu()
{
    section ("CPU: 3-layer stacks and the sample layer in real time, odd block sizes");
    auto measure = [] (ProProcessor& p, const char* what, int notes)
    {
        std::vector<Ev> evs; for (int i = 0; i < notes; ++i) evs = evs + note (48 + (i * 5) % 24, 0.05 + i * 0.03, 3.0);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        auto b = render (p, evs, 4.0);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0, pct = ms / 4000.0 * 100.0;
        CHECK (nansIn (b) == 0 && pct < 60.0, "%-34s %d notes: %.1f%% of one core (%.0f ms for 4 s), peak %.2f", what, notes, pct, ms, peakOf (b));
        return pct;
    };
    { auto p = makeProc (progOf ("Trap Horn Section")); measure (*p, "Trap Horn Section (unison 5 + 2)", 6); }
    { auto p = makeProc (progOf ("Fanfare Stack")); measure (*p, "Fanfare Stack (3 sound layers)", 6); }
    { auto p = makeProc (progOf ("Trap Starter Stack")); measure (*p, "Trap Starter Stack (3 layers)", 8); }
    {
        auto p = makeProc (progOf ("Trap Horn Section"));
        auto wav = makeTestSample(); juce::String msg; p->importTrapSample (wav, msg);
        p->setParam ("ly3_src", (float) gpro::lsSample); p->setParam ("tp_on", 1.0f); settle (*p);
        measure (*p, "brass + sample layer + TAPE", 6);
        for (int k = 0; k < gpro::NUM_LAYERS; ++k) printf ("      layer %d live cpu %.1f%%\n", k + 1, p->cpuLive[k].load());
    }
    // odd block sizes (1 .. 700 samples) through a stack with a sample: no NaN, no voices left, equals the 512 render loudness
    {
        auto p = makeProc (progOf ("Trap Starter Stack"));
        auto wav = makeTestSample(); juce::String msg; p->importTrapSample (wav, msg);
        p->setParam ("ly3_src", (float) gpro::lsSample); settle (*p);
        p->prepareToPlay (SR, 1024);
        const int total = (int) (4.0 * SR);
        juce::AudioBuffer<float> out (2, total), blk (2, 1024); out.clear();
        juce::Random rnd (5); int pos = 0; const auto evs = testPhrase();
        while (pos < total)
        {
            const int n = juce::jmin (total - pos, 1 + rnd.nextInt (700));
            juce::MidiBuffer mb;
            for (auto& e : evs) { const int s = (int) (e.t * SR); if (s >= pos && s < pos + n) mb.addEvent (e.on ? juce::MidiMessage::noteOn (1, e.note, e.vel) : juce::MidiMessage::noteOff (1, e.note), s - pos); }
            blk.setSize (2, n, false, false, true); blk.clear();
            p->processBlock (blk, mb);
            for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, blk, c, 0, n);
            pos += n;
        }
        auto ref = makeProc (progOf ("Trap Starter Stack")); ref->importTrapSample (wav, msg); ref->setParam ("ly3_src", (float) gpro::lsSample); settle (*ref);
        auto r512 = render (*ref, evs, 4.0);
        int left = 0; for (int k = 0; k < gpro::NUM_LAYERS; ++k) left += p->layerVoices[k].load();
        CHECK (nansIn (out) == 0 && std::abs (lufs (out) - lufs (r512)) < 1.0, "random block sizes: %d NaN, %.1f LUFS vs %.1f at 512, voices left %d", nansIn (out), lufs (out), lufs (r512), left);
    }
}

// ------------------------------------------------------------------ screenshots (needs DISPLAY)
int shots (const juce::File& dir)
{
    dir.createDirectory();
    auto snap = [&] (ProProcessor& p, int tab, const char* name)
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto* pe = dynamic_cast<ProEditor*> (ed.get());
        if (pe == nullptr) { printf ("FAIL  no editor\n"); ++failures; return; }
        pe->setSize (ProEditor::W, ProEditor::H);
        pe->showTab (tab);
        pump (400);
        auto img = pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png; juce::File f = dir.getChildFile (name); f.deleteFile();
        if (auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream())) png.writeImageToStream (img, *os);
        // a blank page would be a bug: count the distinct colours
        std::set<juce::uint32> cols; for (int y = 0; y < img.getHeight(); y += 7) for (int x = 0; x < img.getWidth(); x += 7) cols.insert (img.getPixelAt (x, y).getARGB());
        CHECK (img.getWidth() == ProEditor::W && cols.size() > 40, "%s  %dx%d, %d colours", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight(), (int) cols.size());
    };
    { auto p = makeProc (progOf ("Trap Horn Section")); snap (*p, 5, "trap_page_brass.png"); }
    {
        auto p = makeProc (progOf ("Trap Starter Stack"));
        auto wav = makeTestSample(); juce::String msg; p->importTrapSample (wav, msg);
        p->setParam ("ly3_src", (float) gpro::lsSample); p->setParam ("ly3_rev", 1.0f); p->setParam ("tp_on", 1.0f); p->setParam ("tp_amt", 0.6f); p->setParam ("ch_amt", 0.45f); settle (*p);
        render (*p, { { 0.0, 48, 0.9f, true }, { 0.0, 60, 0.9f, true }, { 0.0, 67, 0.9f, true } }, 0.4);   // voices live for the readouts
        snap (*p, 5, "trap_page_stack_sample_tape.png");
        snap (*p, 3, "trap_browse.png");
        snap (*p, 1, "trap_mod.png"); snap (*p, 2, "trap_fx.png"); snap (*p, 4, "trap_bend.png");
    }
    {   // BYPASS button: top right, 84 x 26, inside the window, overlapping no other control; off and on
        auto p = makeProc (progOf ("Trap Starter Stack"));
        snap (*p, 0, "bypass-off.png");
        snap (*p, 5, "bypass-off-trap.png");
        p->setParam ("bypass", 1.0f); pump (50);
        snap (*p, 0, "bypass-on.png");
        snap (*p, 5, "bypass-on-trap.png");
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor()); ed->setSize (ProEditor::W, ProEditor::H); pump (50);
        ghosty::BypassButton* bb = nullptr;
        std::function<void (juce::Component&)> find = [&] (juce::Component& c) { for (auto* k : c.getChildren()) { if (auto* b = dynamic_cast<ghosty::BypassButton*> (k)) bb = b; else find (*k); } };
        find (*ed);
        juce::StringArray hits;
        if (bb != nullptr)
            for (auto* c : bb->getParentComponent()->getChildren())
                if (c != bb && c->isVisible() && c->getBounds().intersects (bb->getBounds())) hits.add (c->getName().isEmpty() ? juce::String (typeid (*c).name()) : c->getName());
        CHECK (bb != nullptr && bb->getWidth() == 84 && bb->getHeight() == 26 && bb->getRight() <= bb->getParentWidth() - 8 && bb->getBottom() <= 76 && hits.isEmpty() && bb->getToggleState(),
               "BYPASS button at %d,%d %dx%d (window %d wide, tab row at y=76), overlaps: %s; shows the bypassed state: %d",
               bb ? bb->getX() : -1, bb ? bb->getY() : -1, bb ? bb->getWidth() : 0, bb ? bb->getHeight() : 0, ProEditor::W, hits.isEmpty() ? "none" : hits.joinIntoString (", ").toRawUTF8(), bb ? (int) bb->getToggleState() : -1);
        p->setParam ("bypass", 0.0f); pump (50);
        CHECK (bb != nullptr && ! bb->getToggleState(), "the button follows the parameter (host automation) back to off");
    }
    return 0;
}

// ------------------------------------------------------------------ DARK / LIGHT theme
#ifndef THEME_REF
double contrastOf (juce::Colour a, juce::Colour b)
{
    auto lum = [] (juce::Colour c)
    {
        auto ch = [] (juce::uint8 v) { const double x = v / 255.0; return x <= 0.03928 ? x / 12.92 : std::pow ((x + 0.055) / 1.055, 2.4); };
        return 0.2126 * ch (c.getRed()) + 0.7152 * ch (c.getGreen()) + 0.0722 * ch (c.getBlue());
    };
    const double la = lum (a), lb = lum (b);
    return (std::max (la, lb) + 0.05) / (std::min (la, lb) + 0.05);
}

// ------------------------------------------------------------------ FATTEN in the FX rack (added 2026-10-23): additive only
int testFattenPro (const juce::File& dir, const juce::File& oldStates)
{
    section ("FATTEN (Ghosty Pro FX rack): off by default, parameters after every older one, old states load unchanged, state round trip, page in 3 looks");
    {   // parameter order: every fx_fatten_* parameter comes after the last parameter of the build before (morph_y)
        auto p = makeProc (0);
        int lastOld = -1, firstFat = 1 << 30, nFat = 0;
        const auto& ps = p->getParameters();
        for (int i = 0; i < ps.size(); ++i)
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (ps[i]))
            {
                if (r->getParameterID() == "morph_y") lastOld = i;
                if (r->getParameterID().startsWith ("fx_fatten_")) { firstFat = std::min (firstFat, i); ++nFat; }
            }
        const float on = p->apvts.getRawParameterValue ("fx_fatten_on")->load();
        CHECK (lastOld >= 0 && firstFat > lastOld && nFat == 10 && on < 0.5f, "%d FATTEN parameters from index %d, the old last parameter (morph_y) at %d; FATTEN on = %.0f by default", nFat, firstFat, lastOld, on);
    }
    if (oldStates.isDirectory())
    {   // states saved by the build before: they load, FATTEN stays off, and saving again gives the same bytes
        int n = 0, same = 0, fatOff = 0;
        for (auto& f : oldStates.findChildFiles (juce::File::findFiles, false, "*.state"))
        {
            juce::MemoryBlock mb; f.loadFileAsData (mb);
            auto p = makeProc (0); p->setStateInformation (mb.getData(), (int) mb.getSize()); pump (20);
            juce::MemoryBlock again; p->getStateInformation (again);
            ++n; same += again == mb; fatOff += p->apvts.getRawParameterValue ("fx_fatten_on")->load() < 0.5f;
        }
        CHECK (n > 300 && fatOff == n, "%d old states: FATTEN off in %d, saved again byte-identical %d (the others: float rounding of trimDb / LFO rate, as in the build before)", n, fatOff, same);
    }
    {   // round trip of FATTEN's settings
        auto a = makeProc (0);
        a->setParam ("fx_fatten_on", 1.0f); a->setParam ("fx_fatten_mode", 7.0f); a->setParam ("fx_fatten_fat", 0.8f); a->setParam ("fx_fatten_drive", 6.0f); a->setParam ("fx_fatten_punch", 1.0f); settle (*a);
        juce::MemoryBlock mb; a->getStateInformation (mb);
        auto b = makeProc (0); b->setStateInformation (mb.getData(), (int) mb.getSize()); pump (20);
        auto g = [&] (const char* id) { return b->apvts.getRawParameterValue (id)->load(); };
        CHECK (g ("fx_fatten_on") > 0.5f && std::abs (g ("fx_fatten_mode") - 7.0f) < 0.01f && std::abs (g ("fx_fatten_fat") - 0.8f) < 0.01f && std::abs (g ("fx_fatten_drive") - 6.0f) < 0.05f && g ("fx_fatten_punch") > 0.5f,
               "FATTEN settings survive save / load (on, SAUSAGE, FAT 80 %%, INPUT 6 dB, PUNCH)");
        // and it does something: FATTEN on (SAUSAGE) vs off on the same note
        auto c = makeProc (0); settle (*c);
        const auto offB = render (*c, { { 0.0, 48, 0.9f, true } }, 1.0);
        auto d = makeProc (0); d->setParam ("fx_fatten_on", 1.0f); d->setParam ("fx_fatten_mode", 7.0f); d->setParam ("fx_fatten_fat", 0.9f); settle (*d);
        const auto onB = render (*d, { { 0.0, 48, 0.9f, true } }, 1.0);
        bool fin = true; double diff = 0; for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < onB.getNumSamples(); ++i) { fin &= std::isfinite (onB.getSample (ch, i)); diff = std::max (diff, (double) std::abs (onB.getSample (ch, i) - offB.getSample (ch, i))); }
        CHECK (fin && diff > 1.0e-3, "FATTEN on changes the sound (max |diff| %.3f), no NaN", diff);
    }
    {   // the FX tab shows FATTEN's page, in all three looks
        dir.createDirectory();
        static const char* themeName[] { "black", "purple", "light" };
        auto p = makeProc (progOf ("Trap Starter Stack"));
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        auto* pe = dynamic_cast<ProEditor*> (ed.get());
        pe->setSize (ProEditor::W, ProEditor::H); pe->showTab (2); pump (200);
        gpfx::Panel* panel = nullptr;
        std::function<void (juce::Component&)> find = [&] (juce::Component& c) { for (auto* k : c.getChildren()) { if (auto* q = dynamic_cast<gpfx::Panel*> (k)) panel = q; else find (*k); } };
        find (*ed);
        CHECK (panel != nullptr, "FX panel found");
        if (panel == nullptr) return 0;
        for (int t = 0; t < 3; ++t)
        {
            pe->setTheme (t); pump (100);
            panel->getProperties().set ("select", (int) gpfx::uFatten); panel->resized(); pump (300);
            auto img = pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f);
            juce::File f = dir.getChildFile (juce::String ("pro-") + themeName[t] + "-fx-fatten.png"); f.deleteFile();
            if (auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream())) juce::PNGImageFormat().writeImageToStream (img, *os);
            std::set<juce::uint32> cols; for (int y = 0; y < img.getHeight(); y += 7) for (int x = 0; x < img.getWidth(); x += 7) cols.insert (img.getPixelAt (x, y).getARGB());
            CHECK (cols.size() > 40, "%s: %d colours", f.getFullPathName().toRawUTF8(), (int) cols.size());
        }
        pe->setTheme (0);
    }
    return 0;
}

int glassPadTests (const juce::File& dir);     // GLASS (round 3): GlassTests.inc
int glassLightTests (const juce::File& dir);
int themeShots (const juce::File& dir)
{
    section ("THEMES: BLACK / PURPLE / LIGHT (Shared/GhostThemes.h) on every page, contrast, nothing see-through, saved per project, never in a sound");
    dir.createDirectory();
    static const char* themeName[] { "black", "purple", "light" };
    for (int t = 0; t < 3; ++t)
    {   // contrast of the palette as Ghosty Pro uses it: text on every surface, accents as text too
        const auto p = pt::Palette::make (ghosty::themes::fromInt (t));
        double body = 99, sec = 99, acc = 99; juce::String worst;
        for (auto bgc : { p.bg, p.card, p.card2, p.deep })
        {
            body = std::min (body, contrastOf (p.white, bgc));
            sec = std::min (sec, contrastOf (p.dim, bgc));
            for (auto a : { p.slime, p.ecto, p.pink, p.ice, p.gold, p.red, p.blue, p.violet })
                if (contrastOf (a, bgc) < acc) { acc = contrastOf (a, bgc); worst = a.toDisplayString (false) + " on " + bgc.toDisplayString (false); }
        }
        bool opaque = true; for (auto c : { p.bg, p.card, p.card2, p.deep, p.knobFace, p.btnTop, p.btnBot, p.track }) opaque &= c.isOpaque();
        // GLASS (round 3): Ghosty Pro's LIGHT is the owner's "liquid glass" off-white / silver (#ECEEF2 .. #F6F7FA): no pure white, brightest channel <= 0xFA
        int maxCh = 0; if (t == 2) for (auto c : p.all()) maxCh = std::max ({ maxCh, (int) c.getRed(), (int) c.getGreen(), (int) c.getBlue() });
        CHECK (body >= 7.0 && sec >= 4.5 && acc >= 4.5 && opaque && (t != 2 || maxCh <= (ghosty::themes::glassLight() ? 0xFA : 0xED)), "%s palette: body text %.2f:1, secondary %.2f:1, accents as text %.2f:1 (lowest %s), surfaces opaque%s",
               themeName[t], body, sec, acc, worst.toRawUTF8(), t == 2 ? (", brightest channel 0x" + juce::String::toHexString (maxCh)).toRawUTF8() : "");
    }
    static const char* tabName[] { "synth", "mod", "fx", "browse", "bend", "trap" };
    auto p = makeProc (progOf ("Trap Starter Stack"));
    auto wav = makeTestSample(); juce::String msg; p->importTrapSample (wav, msg);
    p->setParam ("ly3_src", (float) gpro::lsSample); p->setParam ("tp_on", 1.0f); p->setParam ("ch_amt", 0.45f); settle (*p);
    render (*p, { { 0.0, 48, 0.9f, true }, { 0.0, 60, 0.9f, true }, { 0.0, 67, 0.9f, true } }, 0.4);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
    auto* pe = dynamic_cast<ProEditor*> (ed.get());
    pe->setSize (ProEditor::W, ProEditor::H);
    for (int t : { 0, 1, 2, 0 })
    {
        pe->setTheme (t); pump (100);
        for (int tab = 0; tab < 6; ++tab)
        {
            pe->showTab (tab); pump (250);
            auto img = pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f);
            juce::File f = dir.getChildFile (juce::String ("pro-") + themeName[t] + "-" + tabName[tab] + ".png"); f.deleteFile();
            if (auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream())) juce::PNGImageFormat().writeImageToStream (img, *os);
            int transparent = 0; for (int y = 0; y < img.getHeight(); y += 4) for (int x = 0; x < img.getWidth(); x += 4) transparent += img.getPixelAt (x, y).getAlpha() < 255;
            const int seeThrough = pe->seeThroughPixels();
            CHECK (transparent == 0 && (seeThrough == 0 || (tab == 2 && seeThrough == -2)), "%-6s %-6s %s: window alpha holes %d, see-through pixels inside its cards %d",
                   themeName[t], tabName[tab], f.getFileName().toRawUTF8(), transparent, seeThrough);
        }
    }
    {   // a new window is BLACK; the theme button sits left of BYPASS, names the NEXT look and cycles BLACK -> PURPLE -> LIGHT -> BLACK
        auto q = makeProc (0);
        std::unique_ptr<juce::AudioProcessorEditor> e2 (q->createEditor()); e2->setSize (ProEditor::W, ProEditor::H); pump (50);
        auto* pe2 = dynamic_cast<ProEditor*> (e2.get());
        juce::Button* themeBtn = nullptr; ghosty::BypassButton* bb = nullptr;
        std::function<void (juce::Component&)> find = [&] (juce::Component& c) { for (auto* k : c.getChildren()) { if (k->getName() == "THEME") themeBtn = dynamic_cast<juce::Button*> (k); if (auto* b = dynamic_cast<ghosty::BypassButton*> (k)) bb = b; find (*k); } };
        find (*e2);
        juce::StringArray hits;
        if (themeBtn != nullptr) for (auto* c : themeBtn->getParentComponent()->getChildren()) if (c != themeBtn && c->isVisible() && c->getName() != "MOD OVERLAY" && c->getBounds().intersects (themeBtn->getBounds())) hits.add (c->getName());   // (MOD round 3: the ring overlay is see-through and click-through except on rings)
        CHECK (pe2->themeIndex() == 0 && themeBtn != nullptr && bb != nullptr && themeBtn->getRight() < bb->getX() && hits.isEmpty(), "new window BLACK; theme button left of BYPASS, overlaps: %s", hits.isEmpty() ? "none" : hits.joinIntoString (", ").toRawUTF8());
        juce::String seq;
        static const char* lookName[] { "black", "purple", "light", "ghost" };     // GHOST LOOK: the 4th look follows LIGHT
        for (int k = 0; k < 4; ++k) { themeBtn->triggerClick(); pump (30); seq << lookName[pe2->themeIndex()] << "(" << q->uiTheme.load() << ") "; }
        CHECK (seq.trim() == "purple(1) light(2) ghost(3) black(0)", "clicks cycle the look and mark the project: %s", seq.toRawUTF8());
    }
    {   // saved per project ('theme_name'); the older key still reads (uiTheme="light" -> LIGHT, none -> BLACK); sounds never carry it
        auto q = makeProc (0);
        auto stateWith = [&] (int t) { q->uiTheme.store (t); juce::MemoryBlock mb; q->getStateInformation (mb); return mb; };
        int ok = 0;
        for (int t = 0; t < 3; ++t) { auto mb = stateWith (t); auto r = makeProc (0); r->uiTheme.store (t == 0 ? 2 : 0); r->setStateInformation (mb.getData(), (int) mb.getSize()); ok += r->uiTheme.load() == t; }
        auto mb = stateWith (0);
        auto xml = Peek::getXmlFromBinary (mb.getData(), (int) mb.getSize());
        xml->removeAttribute ("theme_name"); xml->setAttribute ("uiTheme", "light");
        juce::MemoryBlock legacyLight; Peek::copyXmlToBinary (*xml, legacyLight);
        xml->removeAttribute ("uiTheme");
        juce::MemoryBlock legacyNone; Peek::copyXmlToBinary (*xml, legacyNone);
        auto r1 = makeProc (0); r1->setStateInformation (legacyLight.getData(), (int) legacyLight.getSize());
        auto r2 = makeProc (0); r2->uiTheme.store (1); r2->setStateInformation (legacyNone.getData(), (int) legacyNone.getSize());
        CHECK (ok == 3 && r1->uiTheme.load() == 2 && r2->uiTheme.load() == 0, "round trip keeps BLACK / PURPLE / LIGHT (%d of 3); an older LIGHT project loads LIGHT, an older DARK one BLACK", ok);
        q->uiTheme.store (1); q->saveUserPreset ("_theme_test");
        const auto f = ProProcessor::userPresetDir().getChildFile ("_theme_test.ghostypro");
        auto u = makeProc (0); u->uiTheme.store (2); u->loadUserPreset (f); for (int k : { 0, 7, 99 }) u->setCurrentProgram (k); u->initPatch();
        CHECK (! f.loadFileAsString().contains ("theme_name") && u->uiTheme.load() == 2, "sounds never carry the look; sounds / programs / INIT never change it");
        f.deleteFile();
    }
    glassPadTests (dir.getChildFile ("glass"));     // GLASS (round 3): the themed XY pad
    glassLightTests (dir.getChildFile ("glass"));   // GLASS (round 3): the liquid-glass LIGHT
    return 0;
}
#endif

// ------------------------------------------------------------------ oscillator POS: the sound must follow the morph
// harmonic band energies (k * f0 +- f0/3, k = 1..24) of a held note, as dB re the total
std::vector<double> harmonicProfile (const juce::AudioBuffer<float>& b, double f0, double t0, double t1)
{
    constexpr int ORDER = 15, N = 1 << ORDER; juce::dsp::FFT fft (ORDER); std::vector<float> buf ((size_t) N * 2, 0.0f);
    const int st = (int) (t0 * SR), len = juce::jmin (N, (int) ((t1 - t0) * SR));
    for (int i = 0; i < len; ++i) buf[(size_t) i] = (b.getSample (0, st + i) + b.getSample (1, st + i)) * (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / len));
    fft.performFrequencyOnlyForwardTransform (buf.data());
    std::vector<double> e (25, 0.0); double tot = 1e-20;
    for (int k = 1; k <= 24; ++k)
    {
        const int a = (int) ((k - 0.33) * f0 * N / SR), z = (int) ((k + 0.33) * f0 * N / SR);
        for (int i = a; i <= z && i < N / 2; ++i) e[(size_t) k] += (double) buf[(size_t) i] * buf[(size_t) i];
        tot += e[(size_t) k];
    }
    for (auto& v : e) v = 10.0 * std::log10 (v / tot + 1e-12);
    return e;
}
double oddEvenDb (const std::vector<double>& e)   // odd-harmonic energy over even-harmonic energy (k = 2..16), dB
{
    double o = 0, ev = 0; for (int k = 2; k <= 16; ++k) (k % 2 ? o : ev) += std::pow (10.0, e[(size_t) k] / 10.0);
    return 10.0 * std::log10 ((o + 1e-12) / (ev + 1e-12));
}
double profileDistance (const std::vector<double>& a, const std::vector<double>& b)
{
    double d = 0; int n = 0; for (int k = 1; k <= 16; ++k) { d += std::abs (std::max (a[(size_t) k], -60.0) - std::max (b[(size_t) k], -60.0)); ++n; } return d / n;
}
int testPos (bool verbose)
{
    section ("OSC POS: the sound follows the wavetable position on both oscillators, every table, plain and with the owner's unison settings");
    int bad = 0;
    for (int osc = 0; osc < 2; ++osc)
        for (int uniCase = 0; uniCase < 2; ++uniCase)
            for (int tbl = 0; tbl < gpro::tSample; ++tbl)
            {
                const juce::String px = osc == 0 ? "o1_" : "o2_";
                std::vector<std::vector<double>> prof; std::vector<double> oe;
                for (float pos : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
                {
                    auto p = makeProc (-1);
                    p->setParam ("o1_on", osc == 0 ? 1.0f : 0.0f); p->setParam ("o2_on", osc == 1 ? 1.0f : 0.0f);
                    p->setParam (px + "lvl", 0.8f); p->setParam (px + "tbl", (float) tbl); p->setParam (px + "pos", pos);
                    p->setParam ("cutoff", 1.0f); p->setParam ("verb", 0.0f); p->setParam ("echo", 0.0f); p->setParam ("chorus", 0.0f);
                    if (uniCase == 1) { p->setParam (px + "uni", osc == 0 ? 8.0f : 6.0f); p->setParam (px + "bln", osc == 0 ? 0.8f : 0.75f); p->setParam (px + "det", osc == 0 ? 0.42f : 0.3f); p->setParam (px + "spr", osc == 0 ? 0.9f : 0.6f); }
                    else p->setParam (px + "uni", 1.0f);
                    settle (*p);
                    auto b = render (*p, note (48, 0.0, 1.4), 1.4);
                    const double f0 = 440.0 * std::pow (2.0, (48 - 69) / 12.0);
                    prof.push_back (harmonicProfile (b, f0, 0.4, 1.3)); oe.push_back (oddEvenDb (prof.back()));
                }
                const double d01 = profileDistance (prof[0], prof[4]);
                double steps = 0; for (int i = 0; i < 4; ++i) steps += profileDistance (prof[(size_t) i], prof[(size_t) i + 1]);
                const bool moves = d01 > 1.0 || steps > 2.0;
                const bool flat = tbl == gpro::tGhost;   // Ghost A/B with its default shapes A = B? (checked below separately)
                if (! moves && ! flat) ++bad;
                if (verbose || (! moves && ! flat))
                    printf ("      %s OSC %d %-14s %s: POS 0 -> 1 spectrum change %.1f dB (sum of steps %.1f), odd/even %+.1f %+.1f %+.1f %+.1f %+.1f dB\n", moves ? "  " : "!!", osc + 1,
                            gpro::tableNames()[tbl].toRawUTF8(), uniCase ? "unison" : "single", d01, steps, oe[0], oe[1], oe[2], oe[3], oe[4]);
                fflush (stdout);
            }
    CHECK (bad == 0, "every table moves the sound with POS on both oscillators (%d tables x 2 oscs x 2 unison settings did not)", bad);
    return 0;
}

// ------------------------------------------------------------------ every knob: turning it changes the rendered audio
// Each sound parameter is rendered at its minimum and at its maximum, in a context where it can act (a mod route for an LFO / envelope,
// OSC 2 on for OSC 2 knobs, a held chord for arp / voice settings, a layer for layer knobs ...). Inert = the two renders differ by less
// than -60 dB re the signal. Rack (fx_) parameters belong to the FX page tests.
int testKnobSweep (const juce::StringArray& only = {})
{
    section ("KNOB SWEEP: every Ghosty Pro sound parameter changes the audio between its min and max (in a context where it acts)");
    auto wav = makeTestSample();
    auto setup = [&] (ProProcessor& p, const juce::String& id)
    {
        auto route = [&] (int src, int dst, float amt) { p.setParam ("m1_src", (float) src); p.setParam ("m1_dst", (float) dst); p.setParam ("m1_amt", amt); };
        p.setParam ("verb", 0.0f); p.setParam ("echo", 0.0f);
        if (id.startsWith ("o2_") && id != "o2_on") p.setParam ("o2_on", 1.0f);
        for (auto o : { "o1_", "o2_" }) if (id == juce::String (o) + "det" || id == juce::String (o) + "spr" || id == juce::String (o) + "bln") p.setParam (juce::String (o) + "uni", 4.0f);
        if (id == "waveA" || id == "waveB") { p.setParam ("o1_tbl", (float) gpro::tGhost); p.setParam ("o1_pos", id == "waveA" ? 0.0f : 1.0f); }
        if (id == "sub_oct" || id == "sub_shp") p.setParam ("sub_lvl", 0.6f);
        if (id == "nz_col") p.setParam ("nz_lvl", 0.6f);
        if (id == "xamt") { p.setParam ("o2_on", 1.0f); p.setParam ("xmod", 1.0f); }
        if (id == "xmod") { p.setParam ("o2_on", 1.0f); p.setParam ("xamt", 0.5f); }
        if (id == "res" || id == "f_drive" || id == "f_key" || id == "vel_f" || id == "f_type") p.setParam ("cutoff", 0.45f);
        if (id == "fenv") p.setParam ("cutoff", 0.3f);
        if (id == "f_a" || id == "f_d" || id == "f_s" || id == "f_r") { p.setParam ("cutoff", 0.3f); p.setParam ("fenv", 0.7f); if (id != "f_s") p.setParam ("f_s", 0.5f); p.setParam ("release", 1.0f); }
        if (id == "decay") p.setParam ("sustain", 0.3f);
        if (id == "e2_r" || id == "e3_r") p.setParam (id.substring (0, 3) + "s", 0.6f);
        if (id == "smp_d") p.setParam ("smp_s", 0.3f);
        if (id == "legato") { p.setParam ("attack", 0.15f); p.setParam ("decay", 0.2f); p.setParam ("sustain", 0.3f); }   // a retrigger re-attacks from the sustain level
        if (id == "key") p.setParam ("scale", 1.0f);
        if (id == "curve") { p.setParam ("decay", 0.5f); p.setParam ("sustain", 0.3f); p.setParam ("release", 0.4f); }
        if (id.startsWith ("e2_") || id.startsWith ("e3_")) { route (id.startsWith ("e2_") ? gpro::srcEnv2 : gpro::srcEnv3, gpro::dCutoff, 0.8f); p.setParam ("cutoff", 0.3f); }
        if (id.startsWith ("l1_") || id.startsWith ("l2_"))
        {
            route (id.startsWith ("l1_") ? gpro::srcLfo1 : gpro::srcLfo2, gpro::dCutoff, 0.8f); p.setParam ("cutoff", 0.4f);
            p.setParam (id.substring (0, 3) + "sync", id.endsWith ("div") ? 1.0f : 0.0f);
            if (id.endsWith ("fade")) p.setParam (id.substring (0, 3) + "rate", 4.0f);
        }
        if (id.startsWith ("m") && id.length() == 6 && id[1] >= '1' && id[1] <= '8')
        {
            const juce::String pfx = id.substring (0, 3);
            p.setParam (pfx + "src", (float) gpro::srcLfo1); p.setParam (pfx + "dst", (float) gpro::dCutoff); p.setParam (pfx + "amt", 0.8f); p.setParam ("cutoff", 0.4f); p.setParam ("l1_rate", 3.0f);
        }
        if (id.startsWith ("mac") && id.length() == 4) { route (gpro::srcMac1 + id.getTrailingIntValue() - 1, gpro::dCutoff, 0.8f); p.setParam ("cutoff", 0.3f); }
        if (id == "glide") p.setParam ("mono", 1.0f);
        if (id == "legato") { p.setParam ("mono", 1.0f); p.setParam ("glide", 0.2f); }
        if (id == "key" || id == "scale") p.setParam ("chord", 1.0f);
        if (id.startsWith ("arp") && id != "arp") p.setParam ("arp", 1.0f);
        if (id.startsWith ("bl_") && id != "bl_on") { p.setParam ("bl_on", 1.0f); p.bendShape.set (bendlane::factory()[(size_t) bendlane::factoryNames().indexOf ("Slide down")].shape); }
        if (id == "br_scoopt") p.setParam ("br_scoop", 0.6f);
        if (id == "br_formf") p.setParam ("br_form", 0.6f);
        if (id == "tp_amt" || id == "tp_slow") p.setParam ("tp_on", 1.0f);
        if (id.startsWith ("ly2_") || id.startsWith ("ly3_") || id.startsWith ("stk_"))
        {
            const int k = id.startsWith ("ly3_") ? 2 : 1;
            p.setLayerSound (k, "Boom Sub Long");
            if (id.endsWith ("_fx")) { p.setParam ("verb", 0.5f); p.setParam ("drive", 0.6f); }
            if (id == "stk_vsplit") p.setParam ("ly2_play", 1.0f);
            if (id == "stk_ksplit") p.setParam ("ly2_play", 3.0f);
        }
        if (id.startsWith ("smp_"))
        {
            juce::String m; p.importTrapSample (wav, m); p.setParam ("ly1_lvl", 0.0f);
            if (id == "smp_end" || id == "smp_start") p.setParam ("smp_loop", 0.0f);
        }
        if (id == "smp_revvel") p.setParam ("ly2_rev", 0.0f);
    };
    auto evsFor = [] (const juce::String& id)
    {
        std::vector<Ev> e;
        if (id == "glide" || id == "legato" || id == "mono" || id == "poly")
        { e = note (48, 0.0, 0.5) + note (55, 0.25, 0.5) + note (60, 0.5, 0.5) + note (67, 0.75, 0.4); return e; }
        if (id == "vel_amp" || id == "vel_f" || id == "smp_revvel" || id == "stk_vsplit") return note (48, 0.0, 0.8, 0.45f) + note (55, 0.0, 0.8, 0.95f);
        if (id == "stk_ksplit") return note (40, 0.0, 0.8) + note (72, 0.0, 0.8);
        if (id.endsWith ("_play")) return note (40, 0.0, 0.8, 0.45f) + note (72, 0.0, 0.8, 0.95f);   // both sides of the key and velocity splits
        if (id == "bl_mode") return note (48, 0.31, 0.9);                                              // off the beat: RETRIGGER restarts the lane there
        return note (48, 0.0, 0.8) + note (52, 0.0, 0.8) + note (55, 0.0, 0.8);
    };
    juce::StringArray inert; int tested = 0;
    auto ids = makeProc (-1);
    for (auto* prm0 : ids->getParameters())
    {
        auto* prm = dynamic_cast<juce::RangedAudioParameter*> (prm0);
        if (prm == nullptr) continue;
        const auto id = prm->getParameterID();
        if (id.startsWith ("fx_") || id == "bypass" || id.startsWith ("morph_")) continue;     // (the MORPH puck: the morph tests)
        if (! only.isEmpty() && ! only.contains (id)) continue;
        auto renderAt = [&] (float norm)
        {
            auto p = makeProc (-1);
            setup (*p, id);
            p->apvts.getParameter (id)->setValueNotifyingHost (norm);
            settle (*p);
            if (id == "bend")
            {   // BEND RANGE: the pitch wheel pushed fully up while a note holds
                juce::AudioBuffer<float> out (2, (int) (1.0 * SR)), blk (2, BS); out.clear();
                for (int pos = 0, k = 0; pos + BS <= out.getNumSamples(); pos += BS, ++k)
                {
                    juce::MidiBuffer m; if (k == 0) { m.addEvent (juce::MidiMessage::pitchWheel (1, 16383), 0); m.addEvent (juce::MidiMessage::noteOn (1, 48, 0.9f), 1); }
                    blk.clear(); p->processBlock (blk, m); for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, blk, ch, 0, BS);
                }
                return out;
            }
            auto evs = evsFor (id);
            auto b = render (*p, evs, 1.4);
            return b;
        };
        // choices / switches: the first option against one in the middle (the last one can be an equivalent of the first, e.g. a
        // modulation target that needs a sample, a layer source "Sample" with none loaded); BEND RANGE needs a pitch wheel (none here)

        const bool discrete = dynamic_cast<juce::AudioParameterChoice*> (prm) != nullptr || dynamic_cast<juce::AudioParameterInt*> (prm) != nullptr;
        float hi = discrete && ! id.endsWith ("oct") && ! id.endsWith ("uni") && prm->getNumSteps() > 2 ? 0.5f : (id == "smp_revvel" ? 0.6f : 1.0f);
        if (id.length() == 6 && id.startsWith ("m") && id.endsWith ("_src")) hi = prm->convertTo0to1 ((float) gpro::srcLfo1);     // a source with a moving input
        if (id.endsWith ("_play")) hi = prm->convertTo0to1 (2.0f);                                                               // KEY HIGH: only the upper note
        auto a = renderAt (0.0f), b = renderAt (hi);
        double d = 0, e = 1e-20; for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < a.getNumSamples(); ++i) { const double x = a.getSample (ch, i) - b.getSample (ch, i); d += x * x; e += (double) a.getSample (ch, i) * a.getSample (ch, i) + (double) b.getSample (ch, i) * b.getSample (ch, i); }
        const double db = 10.0 * std::log10 (d / e + 1e-30);
        ++tested;
        if (db < -60.0) { inert.add (id + " (" + juce::String (db, 0) + " dB)"); printf ("      INERT %-14s %s  min vs max differ by %.0f dB\n", id.toRawUTF8(), prm->getName (40).toRawUTF8(), db); fflush (stdout); }
    }
    printf ("      tested %d parameters, inert: %s\n", tested, inert.isEmpty() ? "none" : inert.joinIntoString (", ").toRawUTF8());
    CHECK (inert.isEmpty(), "%d sound parameters swept min -> max: %d inert", tested, inert.size());
    return 0;
}

// ------------------------------------------------------------------ UI audits: text contrast on the pixels, clicks reach their control
bool shownIn (juce::Component* c, juce::Component* root)
{
    for (; c != nullptr && c != root; c = c->getParentComponent()) if (! c->isVisible()) return false;
    return c == root;
}
juce::String textOf (juce::Component* c)
{
    if (auto* l = dynamic_cast<juce::Label*> (c)) return l->isBeingEdited() ? juce::String() : l->getText();
    if (auto* b = dynamic_cast<juce::Button*> (c)) return b->getButtonText();
    if (auto* cb = dynamic_cast<juce::ComboBox*> (c)) return cb->getText();
    return {};
}
// every visible Label / Button / ComboBox with text, inside 'root', checked on the editor's own pixels: background = the most common
// colour of the control's area, ink = the pixel that differs most from it; contrast must be >= 4.5 (disabled = greyed controls: >= 3)
struct ContrastIssue { juce::String what; double ratio; bool enabled; };
std::vector<ContrastIssue> contrastAudit (ProEditor& pe, juce::Component& root, int& checked)
{
    std::vector<ContrastIssue> out;
    auto img = pe.createComponentSnapshot (pe.getLocalBounds(), true, 1.0f);
    std::vector<juce::Component*> all;
    std::function<void (juce::Component&)> walk = [&] (juce::Component& c) { for (auto* k : c.getChildren()) { all.push_back (k); walk (*k); } };
    walk (root);
    for (auto* c : all)
    {
        if (! shownIn (c, &pe)) continue;
        const bool isLabel = dynamic_cast<juce::Label*> (c) != nullptr, isBtn = dynamic_cast<juce::Button*> (c) != nullptr, isCombo = dynamic_cast<juce::ComboBox*> (c) != nullptr;
        if (! isLabel && ! isBtn && ! isCombo) continue;
        if (dynamic_cast<juce::ComboBox*> (c->getParentComponent()) != nullptr) continue;      // the combo's own label: judged as the combo
        const auto txt = textOf (c).trim();
        auto r = pe.getLocalArea (c, c->getLocalBounds()).getIntersection (pe.getLocalBounds());
        if (r.getWidth() < 6 || r.getHeight() < 6) continue;
        bool enabled = c->isEnabled(); for (auto* q = c; q != nullptr && q != &pe; q = q->getParentComponent()) if (q->getAlpha() < 0.99f) enabled = false;
        if (isCombo && txt.isEmpty() && dynamic_cast<juce::ComboBox*> (c)->getNumItems() > 0 && dynamic_cast<juce::ComboBox*> (c)->getTextWhenNothingSelected().isEmpty())
        { out.push_back ({ juce::String ("combo with no text at ") + r.toString(), 0.0, enabled }); ++checked; continue; }
        if (txt.isEmpty()) continue;
        std::map<juce::uint32, int> hist;
        for (int y = r.getY(); y < r.getBottom(); ++y) for (int x = r.getX(); x < r.getRight(); ++x) { const auto px = img.getPixelAt (x, y); ++hist[(px.getARGB() & 0x00f0f0f0u)]; }
        juce::uint32 modeKey = 0; int best = -1; for (auto& [k, n] : hist) if (n > best) { best = n; modeKey = k; }
        juce::Colour bg; { double rr = 0, gg = 0, bb = 0; int n = 0; for (int y = r.getY(); y < r.getBottom(); ++y) for (int x = r.getX(); x < r.getRight(); ++x) { const auto px = img.getPixelAt (x, y); if ((px.getARGB() & 0x00f0f0f0u) == modeKey) { rr += px.getRed(); gg += px.getGreen(); bb += px.getBlue(); ++n; } }
                           bg = juce::Colour ((juce::uint8) (rr / n), (juce::uint8) (gg / n), (juce::uint8) (bb / n)); }
        double ratio = 1.0;
        for (int y = r.getY(); y < r.getBottom(); ++y) for (int x = r.getX(); x < r.getRight(); ++x) ratio = std::max (ratio, contrastOf (img.getPixelAt (x, y), bg));
        ++checked;
        if (ratio < (enabled ? 4.5 : 3.0)) out.push_back ({ juce::String (typeid (*c).name()).fromLastOccurrenceOf ("N", false, false) + " \"" + txt + "\" at " + r.toString(), ratio, enabled });
    }
    return out;
}
// a click at the centre of every visible enabled control must reach that control (or something inside it)
juce::StringArray clickAudit (ProEditor& pe, juce::Component& root, int& checked)
{
    juce::StringArray out;
    std::vector<juce::Component*> all;
    std::function<void (juce::Component&)> walk = [&] (juce::Component& c) { for (auto* k : c.getChildren()) { all.push_back (k); walk (*k); } };
    walk (root);
    for (auto* c : all)
    {
        if (! shownIn (c, &pe) || ! c->isEnabled() || c->getWidth() < 4 || c->getHeight() < 4) continue;
        const bool interactive = dynamic_cast<juce::Button*> (c) || dynamic_cast<juce::ComboBox*> (c) || dynamic_cast<juce::Slider*> (c) || dynamic_cast<juce::TextEditor*> (c);
        if (! interactive) continue;
        if (dynamic_cast<juce::Slider*> (c->getParentComponent()) || dynamic_cast<juce::ComboBox*> (c->getParentComponent())) continue;
        auto* content = pe.contentComponent();
        const auto centre = content->getLocalPoint (c, c->getLocalBounds().getCentre());
        auto* hit = content->getComponentAt (centre);
        ++checked;
        if (hit != c && ! c->isParentOf (hit))
            out.add (juce::String (typeid (*c).name()).fromLastOccurrenceOf ("N", false, false) + " \"" + textOf (c) + "\" at " + centre.toString() + " -> click lands on "
                     + (hit ? juce::String (typeid (*hit).name()) + " \"" + textOf (hit) + "\"" : juce::String ("nothing")));
    }
    return out;
}
// the FX page: select every rack unit by clicking its tile (the strip is the rack panel's first child)
void forEachFxUnit (ProEditor& pe, const std::function<void (int)>& fn)
{
    juce::Component* fxp = nullptr;
    std::function<void (juce::Component&)> find = [&] (juce::Component& c) { for (auto* k : c.getChildren()) { if (dynamic_cast<gpfx::Panel*> (k)) fxp = k; else find (*k); } };
    find (pe);
    if (fxp == nullptr || fxp->getNumChildComponents() == 0) { fn (-1); return; }
    auto* strip = fxp->getChildComponent (0);
    const int rows = strip->getWidth() < 1300 ? 2 : 1, perRow = (gpfx::kNumUnits + rows - 1) / rows;
    auto src = juce::Desktop::getInstance().getMainMouseSource();
    for (int k = 0; k < gpfx::kNumUnits; ++k)
    {
        const float w = (float) strip->getWidth() / (float) perRow, rh = ((float) strip->getHeight() - 14.0f) / (float) rows;
        const juce::Point<float> pos ((float) (k % perRow) * w + w * 0.4f, 14.0f + (float) (k / perRow) * rh + rh * 0.6f);
        const auto now = juce::Time::getCurrentTime();
        juce::MouseEvent down (src, pos, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0, strip, strip, now, pos, now, 1, false);
        juce::MouseEvent up (src, pos, juce::ModifierKeys(), 1.0f, 0, 0, 0, 0, strip, strip, now, pos, now, 1, false);
        strip->mouseDown (down); strip->mouseUp (up); pump (60);
        fn (k);
    }
}

#ifndef THEME_REF
int testUiAudit (const juce::File& dir)
{
    section ("UI audit: every text readable (pixel contrast) and every control clickable, every page, every FX unit, both themes, after theme flips");
    dir.createDirectory();
    auto p = makeProc (progOf ("Fat Hoover Saw"));
    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
    auto* pe = dynamic_cast<ProEditor*> (ed.get());
    pe->setSize (ProEditor::W, ProEditor::H); pump (100);
    static const char* tabName[] { "synth", "mod", "fx", "browse", "bend", "trap" };
    int totalBad = 0, fxBad = 0, totalClickBad = 0, checked = 0, clicks = 0;
    for (int pass = 0; pass < 4; ++pass)          // BLACK fresh, PURPLE, LIGHT, BLACK again after the cycle
    {
        static const int themes[] { 0, 1, 2, 0 };
        pe->setTheme (themes[pass]); pump (100);
        const juce::String th = pass == 0 ? "black" : pass == 1 ? "purple" : pass == 2 ? "light" : "black-after-cycle";
        for (int tab = 0; tab < 6; ++tab)
        {
            pe->showTab (tab); pump (150);
            auto report = [&] (const juce::String& where)
            {
                auto issues = contrastAudit (*pe, *pe->contentComponent(), checked);
                auto clicksBad = clickAudit (*pe, *pe->contentComponent(), clicks);
                int bad = 0; for (auto& i : issues) if (i.enabled || i.ratio < 3.0) ++bad;
                (where.startsWith ("fx-") ? fxBad : totalBad) += bad; totalClickBad += clicksBad.size();
                if (bad > 0 || ! clicksBad.isEmpty())
                {
                    printf ("      %s %s: %d unreadable, %d unclickable\n", th.toRawUTF8(), where.toRawUTF8(), bad, clicksBad.size());
                    int shown = 0;
                    for (auto& i : issues) if ((i.enabled || i.ratio < 3.0) && shown++ < 12) printf ("        text %.2f:1 %s %s\n", i.ratio, i.enabled ? "" : "(greyed)", i.what.toRawUTF8());
                    for (int k = 0; k < juce::jmin (12, clicksBad.size()); ++k) printf ("        click %s\n", clicksBad[k].toRawUTF8());
                    auto img = pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f);
                    juce::File f = dir.getChildFile ("audit-" + th + "-" + where + ".png"); f.deleteFile();
                    if (auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream())) juce::PNGImageFormat().writeImageToStream (img, *os);
                    fflush (stdout);
                }
            };
            if (tab == 2) forEachFxUnit (*pe, [&] (int u) { report (juce::String ("fx-unit") + juce::String (u)); });
            else report (tabName[tab]);
        }
    }
    {   // two Ghosty Pro windows, one LIGHT one DARK: the DARK window's FX rack repainting ALONE (its parents not repainted) after the
        // LIGHT window painted must still be DARK
        pe->setLightTheme (false); pe->showTab (2); pump (100);
        auto q = makeProc (progOf ("Fat Hoover Saw"));
        std::unique_ptr<juce::AudioProcessorEditor> ed2 (q->createEditor());
        auto* pe2 = dynamic_cast<ProEditor*> (ed2.get()); pe2->setSize (ProEditor::W, ProEditor::H); pe2->setLightTheme (true); pe2->showTab (2); pump (100);
        juce::Component* fxp = nullptr;
        std::function<void (juce::Component&)> find = [&] (juce::Component& c) { for (auto* k : c.getChildren()) { if (dynamic_cast<gpfx::Panel*> (k)) fxp = k; else find (*k); } };
        find (*pe);
        auto lightShot = pe2->createComponentSnapshot (pe2->getLocalBounds(), true, 1.0f);     // the LIGHT window paints last
        auto alone = fxp->createComponentSnapshot (fxp->getLocalBounds(), true, 1.0f);           // then only the DARK window's rack repaints
        double sum = 0; int n = 0; for (int y = 0; y < alone.getHeight(); y += 5) for (int x = 0; x < alone.getWidth(); x += 5) { sum += alone.getPixelAt (x, y).getBrightness(); ++n; }
        CHECK (sum / n < 0.4, "a DARK window's FX rack repainting alone after a LIGHT window painted stays DARK (mean brightness %.2f)", sum / n);
        juce::File f = dir.getChildFile ("fx-alone-after-light-window.png"); f.deleteFile();
        if (auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream())) juce::PNGImageFormat().writeImageToStream (alone, *os);
    }
    CHECK (totalBad == 0, "text contrast, Ghosty Pro pages: %d controls checked in all (BLACK, PURPLE, LIGHT, BLACK again), %d unreadable (< 4.5:1, greyed < 3:1)", checked, totalBad);
    CHECK (fxBad == 0, "text contrast, the Ghost FX rack (shared fx/ UI, every unit open): %d unreadable", fxBad);
    CHECK (totalClickBad == 0, "clicks: %d controls clicked at their centre, %d did not reach their control", clicks, totalClickBad);
    return 0;
}
#endif

// ------------------------------------------------------------------ drawing the wave (SYNTH page display, DRAW A / B, A/B shapes)
#ifndef THEME_REF
int testDraw (const juce::File& dir)
{
    section ("DRAW: drag on the wavetable display draws the wave (switching to GHOST A/B), live + click-free, undo, saved, A/B shapes morph");
    dir.createDirectory();
    auto p = makeProc (-1); p->setParam ("o1_tbl", (float) gpro::tPulse);
    auto clean = [] (ProProcessor& q)
    {   // a plain oscillator for the measurements: no unison, filter open, no drive / FX
        for (auto [id, v] : std::initializer_list<std::pair<const char*, float>> { { "verb", 0 }, { "echo", 0 }, { "chorus", 0 }, { "phaser", 0 }, { "drive", 0 }, { "crush", 0 }, { "haunt", 0 },
                                                                                  { "o2_on", 0 }, { "o1_uni", 1 }, { "cutoff", 1 }, { "res", 0 }, { "f_drive", 0 }, { "fenv", 0 }, { "ch_amt", 0 }, { "sub_lvl", 0 }, { "nz_lvl", 0 }, { "o1_pos", 0 } })
            q.setParam (id, v);
        q.rack.applyScene (q.apvts, "dry"); settle (q);
    };
    clean (*p);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
    auto* pe = dynamic_cast<ProEditor*> (ed.get()); pe->setSize (ProEditor::W, ProEditor::H); pe->showTab (0); pump (100);
    juce::Component* screen = nullptr; juce::ComboBox* comboA = nullptr; juce::TextButton* drawB = nullptr; juce::TextButton* drawA = nullptr;
    std::function<void (juce::Component&)> find = [&] (juce::Component& c)
    {
        for (auto* k : c.getChildren())
        {
            if (auto* t = dynamic_cast<juce::TooltipClient*> (k); t != nullptr && dynamic_cast<juce::Slider*> (k) == nullptr && t->getTooltip().containsIgnoreCase ("draw your own wave")) screen = k;
            if (auto* cb = dynamic_cast<juce::ComboBox*> (k); cb != nullptr && cb->getTooltip().startsWith ("GHOST A/B: the shape of frame A")) comboA = cb;
            if (auto* b = dynamic_cast<juce::TextButton*> (k); b != nullptr && b->getButtonText() == "DRAW B") drawB = b;
            if (auto* b = dynamic_cast<juce::TextButton*> (k); b != nullptr && b->getButtonText() == "DRAW A") drawA = b;
            find (*k);
        }
    };
    find (*pe);
    CHECK (screen != nullptr && comboA != nullptr && drawB != nullptr, "found the display, the A shape box and DRAW B");
    if (screen == nullptr || comboA == nullptr || drawB == nullptr) return 0;
    CHECK (comboA->isEnabled() && drawB->isEnabled(), "A/B shape boxes and DRAW A/B are live on a non-GHOST table (they used to be disabled)");
    const int tblBefore = (int) p->apvts.getRawParameterValue ("o1_tbl")->load();
    auto before = render (*p, note (48, 0.0, 0.8), 0.8);
    auto src = juce::Desktop::getInstance().getMainMouseSource();
    auto ev = [&] (juce::Point<float> pos, juce::ModifierKeys mods, int clicks = 1)
    { const auto now = juce::Time::getCurrentTime(); return juce::MouseEvent (src, pos, mods, 1.0f, 0, 0, 0, 0, screen, screen, now, pos, now, clicks, false); };
    const float W = (float) screen->getWidth(), H = (float) screen->getHeight();
    const juce::ModifierKeys lb (juce::ModifierKeys::leftButtonModifier);
    // 1) a click / stroke on the display of a NON-ghost table must never draw or switch anything (the owner kept starting a drawing by accident)
    {
        const auto frameA0 = p->frameA; const int tbl0 = (int) p->apvts.getRawParameterValue ("o1_tbl")->load();
        screen->mouseDown (ev ({ 14.0f, H * 0.25f }, lb));
        for (float x = 14.0f; x < W - 12.0f; x += 23.0f) screen->mouseDrag (ev ({ x, H * 0.75f }, lb));
        screen->mouseUp (ev ({ W - 14.0f, H * 0.75f }, juce::ModifierKeys()));
        pump (60);
        CHECK ((int) p->apvts.getRawParameterValue ("o1_tbl")->load() == tbl0 && p->frameA == frameA0, "a click-drag on the display of a normal table does NOT draw or switch to GHOST A/B (table %d)", tbl0);
    }
    // 2) pressing DRAW A is the deliberate way in: it switches to GHOST A/B, then a drag draws
    if (drawA != nullptr) drawA->triggerClick();
    pump (60);
    // a square-ish stroke: high for the first half, low for the second, drawn fast (big gaps between mouse samples)
    screen->mouseDown (ev ({ 14.0f, H * 0.25f }, lb));
    for (float x = 14.0f; x < W - 12.0f; x += 23.0f) screen->mouseDrag (ev ({ x, x < W * 0.5f ? H * 0.25f : H * 0.75f }, lb));
    screen->mouseUp (ev ({ W - 14.0f, H * 0.75f }, juce::ModifierKeys()));
    pump (100);
    const int tblAfter = (int) p->apvts.getRawParameterValue ("o1_tbl")->load();
    auto fA = p->frameA;
    double mean = 0, peak = 0; int nans = 0, high = 0, low = 0;
    for (int i = 0; i < ghosty::FRAME; ++i) { const float v = fA[(size_t) i]; if (! std::isfinite (v)) ++nans; mean += v; peak = std::max (peak, (double) std::abs (v)); if (i < ghosty::FRAME / 2 - 20 && v > 0.5f) ++high; if (i > ghosty::FRAME / 2 + 20 && v < -0.5f) ++low; }
    mean /= ghosty::FRAME;
    CHECK (tblBefore != gpro::tGhost && tblAfter == gpro::tGhost, "DRAW A on another table switched OSC 1 to GHOST A/B, then drawing works (%s -> %s)", gpro::tableNames()[tblBefore].toRawUTF8(), gpro::tableNames()[tblAfter].toRawUTF8());
    CHECK (nans == 0 && std::abs (mean) < 1.0e-3 && std::abs (peak - 1.0) < 1.0e-3 && high > 80 && low > 80, "the drawn frame: square-ish (%d high / %d low points), DC %.1e, peak %.3f, %d NaN", high, low, mean, peak, nans);
    auto after = render (*p, note (48, 0.0, 0.8), 0.8);
    double d = 0, e = 1e-20; for (int i = 0; i < after.getNumSamples(); ++i) { const double x = after.getSample (0, i) - before.getSample (0, i); d += x * x; e += (double) before.getSample (0, i) * before.getSample (0, i); }
    const double f0 = 440.0 * std::pow (2.0, (48 - 69) / 12.0);
    const double pd = profileDistance (harmonicProfile (before, f0, 0.2, 0.7), harmonicProfile (after, f0, 0.2, 0.7));
    CHECK (10 * std::log10 (d / e) > -20 && pd > 3.0, "the sound follows the drawing: changed by %.1f dB re the old sound, harmonic profile moved %.1f dB", 10 * std::log10 (d / e), pd);
    {   // SHIFT = straight line from the stroke's start
        screen->mouseDown (ev ({ W * 0.2f, H * 0.5f }, lb));
        screen->mouseDrag (ev ({ W * 0.45f, H * 0.2f }, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier)));
        int mono = 0, n = 0; const auto& f = p->frameA;
        const int i0 = (int) (ghosty::FRAME * 0.22f), iEnd = (int) (ghosty::FRAME * 0.42f);
        for (int i = i0 + 1; i < iEnd; ++i) { ++n; mono += f[(size_t) i] >= f[(size_t) i - 1] - 1e-6f; }
        screen->mouseUp (ev ({ W * 0.45f, H * 0.2f }, juce::ModifierKeys())); pump (50);
        CHECK (mono == n, "SHIFT draws a straight rising line (%d of %d steps rise)", mono, n);
    }
    {   // undo / redo (cmd + Z)
        const auto drawn = p->frameA;
        screen->keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)); pump (50);
        const bool undone = p->frameA != drawn && p->frameA == fA;
        screen->keyPressed (juce::KeyPress ('z', juce::ModifierKeys (juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier), 0)); pump (50);
        CHECK (undone && p->frameA == drawn, "cmd+Z undoes the last stroke, cmd+shift+Z redoes it");
    }
    {   // click-free live update: redraw the frame while a note holds; the biggest sample step around the swap vs the note's own
        auto q = makeProc (progOf ("Fat Hoover Saw")); q->setParam ("o1_tbl", (float) gpro::tGhost); q->setParam ("o2_on", 0.0f); q->setParam ("verb", 0.0f); q->setParam ("echo", 0.0f); settle (*q);
        juce::AudioBuffer<float> out (2, (int) (0.6 * SR)), blk (2, BS); out.clear();
        double stepNormal = 0, stepSwap = 0;
        for (int pos = 0, k = 0; pos + BS <= out.getNumSamples(); pos += BS, ++k)
        {
            juce::MidiBuffer m; if (k == 0) m.addEvent (juce::MidiMessage::noteOn (1, 48, 0.9f), 0);
            if (k == 20) { for (auto& v : q->frameA) v = -v; q->rebuildWaves(); }     // the drawing changes (inverted = the worst case)
            blk.clear(); q->processBlock (blk, m);
            for (int ch = 0; ch < 2; ++ch) out.copyFrom (ch, pos, blk, ch, 0, BS);
        }
        for (int i = 1; i < out.getNumSamples(); ++i)
        {
            const double st = std::abs (out.getSample (0, i) - out.getSample (0, i - 1));
            if (i >= 21 * BS - 4 && i < 21 * BS + 400) stepSwap = std::max (stepSwap, st); else if (i > 4 * BS) stepNormal = std::max (stepNormal, st);
        }
        CHECK (stepSwap <= stepNormal * 1.3 + 1e-4, "a redrawn wave fades in: biggest step at the swap %.4f vs the note's own %.4f", stepSwap, stepNormal);
    }
    {   // saved with the sound: state round trip keeps the drawn frame and GHOST A/B
        juce::MemoryBlock mb; p->getStateInformation (mb);
        auto r = makeProc (0); r->setStateInformation (mb.getData(), (int) mb.getSize()); settle (*r);
        double md = 0; for (int i = 0; i < ghosty::FRAME; ++i) md = std::max (md, (double) std::abs (r->frameA[(size_t) i] - p->frameA[(size_t) i]));
        CHECK ((int) r->apvts.getRawParameterValue ("o1_tbl")->load() == gpro::tGhost && md < 1e-6, "state round trip: GHOST A/B and the drawn frame (max difference %.1e)", md);
    }
    {   // picking A/B shapes on another table: the free Ghosty's morph, Saw -> Square across POS
        auto q = makeProc (-1); q->setParam ("o1_tbl", (float) gpro::tPulse); clean (*q);
        std::unique_ptr<juce::AudioProcessorEditor> ed2 (q->createEditor()); auto* pe2 = dynamic_cast<ProEditor*> (ed2.get()); pe2->setSize (ProEditor::W, ProEditor::H); pe2->showTab (0); pump (50);
        juce::ComboBox* ca = nullptr; std::function<void (juce::Component&)> f2 = [&] (juce::Component& c) { for (auto* k : c.getChildren()) { if (auto* cb = dynamic_cast<juce::ComboBox*> (k); cb != nullptr && cb->getTooltip().startsWith ("GHOST A/B: the shape of frame A")) ca = cb; f2 (*k); } };
        f2 (*pe2);
        const auto pos = ca->getLocalBounds().getCentre().toFloat(); const auto now = juce::Time::getCurrentTime();
        ca->getParentComponent()->mouseDown (juce::MouseEvent (src, pos, lb, 1.0f, 0, 0, 0, 0, ca, ca, now, pos, now, 1, false));   // the user's click arms it
        q->setParam ("waveA", (float) ghosty::shapeNames().indexOf ("Saw")); q->setParam ("waveB", (float) ghosty::shapeNames().indexOf ("Square")); pump (50);
        const bool ghost = (int) q->apvts.getRawParameterValue ("o1_tbl")->load() == gpro::tGhost;
        auto at = [&] (float ps) { q->setParam ("o1_pos", ps); settle (*q); return harmonicProfile (render (*q, note (48, 0.0, 0.8), 0.8), f0, 0.2, 0.7); };
        const auto p0 = at (0.0f), p5 = at (0.5f), p1 = at (1.0f);
        const double d5 = profileDistance (p0, p5), d1 = profileDistance (p0, p1);
        CHECK (ghost && oddEvenDb (p1) > oddEvenDb (p0) + 15.0 && d5 > 1.0 && d1 > d5, "A = Saw, B = Square: OSC 1 switches to GHOST A/B and POS morphs it (away from the saw: %.1f dB at 50 %%, %.1f dB at 100 %%; odd/even %+.1f -> %+.1f dB)",
               d5, d1, oddEvenDb (p0), oddEvenDb (p1));
    }
    auto img = pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f);
    juce::File f = dir.getChildFile ("draw-dark.png"); f.deleteFile(); if (auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream())) juce::PNGImageFormat().writeImageToStream (img, *os);
    pe->setLightTheme (true); pump (100);
    img = pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f);
    f = dir.getChildFile ("draw-light.png"); f.deleteFile(); if (auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream())) juce::PNGImageFormat().writeImageToStream (img, *os);
    return 0;
}
#endif

// ------------------------------------------------------------------ envelope tracer dots (MOD page ENV 2 / ENV 3)
#ifndef THEME_REF
int testTrace (const juce::File& dir)
{
    section ("ENVELOPE DOTS: the published positions match the engine's envelope, one dot per voice, no tearing, shown on the MOD page");
    auto p = makeProc (-1);
    p->setParam ("e2_a", 0.10f); p->setParam ("e2_d", 0.20f); p->setParam ("e2_s", 0.5f); p->setParam ("e2_r", 0.40f); p->setParam ("curve", 0.0f);   // straight segments
    p->setParam ("release", 0.5f); settle (*p);
    p->traceOn.store (true);
    juce::AudioBuffer<float> blk (2, BS); int blocks = 0;
    auto run = [&] (double secs, bool noteOn, bool noteOff)
    {
        const int nb = (int) std::round (secs * SR / BS);
        for (int k = 0; k < nb; ++k)
        {
            juce::MidiBuffer m; if (k == 0 && noteOn) m.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0); if (k == 0 && noteOff) m.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
            blk.clear(); p->processBlock (blk, m); ++blocks;
        }
    };
    auto look = [&] (int& st, float& lin, float& out) { ProProcessor::TraceSnap sn; const bool ok = p->readTrace (sn); st = sn.v[0].stage[2]; lin = sn.v[0].lin[2]; out = sn.v[0].out[2]; return ok && sn.count == 1; };
    int st; float lin, out; double worst = 0; juce::String rows;
    // attack: after 48 ms of 100 ms -> lin 0.48 (the block grid: 9 blocks of 512 at 48 k = 96 ms -> lin 0.96 -> use exact block times)
    const double tA = 5 * BS / SR; run (tA, true, false);
    bool ok = look (st, lin, out); worst = std::max (worst, std::abs (lin - tA / 0.10)); rows << "attack " << juce::String (lin, 3) << "/" << juce::String (tA / 0.10, 3);
    ok &= st == 1;
    const double tD = 20 * BS / SR; run (tD - tA, false, false);            // 213 ms: 113 ms into the 200 ms decay -> lin 1 - 0.565
    const bool ok2 = look (st, lin, out); const double wantD = 1.0 - (tD - 0.10) / 0.20;
    worst = std::max (worst, std::abs (lin - wantD)); rows << ", decay " << juce::String (lin, 3) << "/" << juce::String (wantD, 3) << " level " << juce::String (out, 3) << "/" << juce::String (0.5 + 0.5 * wantD, 3);
    worst = std::max (worst, std::abs (out - (0.5 + 0.5 * wantD)));
    run (0.4, false, false); const bool ok3 = look (st, lin, out); worst = std::max (worst, std::abs (out - 0.5)); const int stS = st; rows << ", sustain " << juce::String (out, 3);
    const double tR = 15 * BS / SR; run (tR, false, true); const bool ok4 = look (st, lin, out);
    const double wantR = 1.0 - tR / 0.40; worst = std::max (worst, std::abs (lin - wantR)); rows << ", release " << juce::String (lin, 3) << "/" << juce::String (wantR, 3) << " level " << juce::String (out, 3);
    worst = std::max (worst, std::abs (out - 0.5 * wantR));
    CHECK (ok && ok2 && ok3 && ok4 && stS == 3 && st == 4 && worst <= 0.01, "ENV 2 A 100 ms / D 200 ms / S 50 %% / R 400 ms: %s (worst error %.4f, limit 0.01)", rows.toRawUTF8(), worst);
    {   // ENV 1 = the AMP envelope (MOD page): the same check on the amp envelope; LFO 1 phase dots (retriggered at the note)
        auto q = makeProc (-1);
        q->setParam ("attack", 0.10f); q->setParam ("decay", 0.20f); q->setParam ("sustain", 0.5f); q->setParam ("release", 0.40f); q->setParam ("curve", 0.0f);
        q->setParam ("l1_rate", 2.0f); q->setParam ("l1_sync", 0.0f); q->setParam ("l1_trig", 1.0f); settle (*q);
        q->traceOn.store (true);
        juce::AudioBuffer<float> b (2, BS);
        auto go = [&] (int nb, bool on, bool off) { for (int k = 0; k < nb; ++k) { juce::MidiBuffer m; if (k == 0 && on) m.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0); if (k == 0 && off) m.addEvent (juce::MidiMessage::noteOff (1, 60), 0); b.clear(); q->processBlock (b, m); } };
        double worstA = 0, worstL = 0; ProProcessor::TraceSnap sn;
        go (5, true, false); q->readTrace (sn);
        worstA = std::max (worstA, std::abs (sn.v[0].lin[0] - 5 * BS / SR / 0.10)); const bool att = sn.v[0].stage[0] == 1;
        double ph = 2.0 * 5 * BS / SR; worstL = std::max (worstL, std::abs (sn.v[0].lfo[0] - (ph - std::floor (ph))));
        go (15, false, false); q->readTrace (sn);
        worstA = std::max (worstA, std::abs (sn.v[0].lin[0] - (1.0 - (20 * BS / SR - 0.10) / 0.20))); const bool dec = sn.v[0].stage[0] == 2;
        ph = 2.0 * 20 * BS / SR; worstL = std::max (worstL, std::abs (sn.v[0].lfo[0] - (ph - std::floor (ph))));
        go (40, false, false); q->readTrace (sn); worstA = std::max (worstA, std::abs (sn.v[0].out[0] - 0.5)); const bool sus = sn.v[0].stage[0] == 3;
        go (15, false, true); q->readTrace (sn); worstA = std::max (worstA, std::abs (sn.v[0].lin[0] - (1.0 - 15 * BS / SR / 0.40))); const bool rel = sn.v[0].stage[0] == 4;
        CHECK (att && dec && sus && rel && worstA <= 0.01, "ENV 1 / AMP dots: attack, decay, sustain, release in turn, worst error %.4f (limit 0.01)", worstA);
        CHECK (worstL <= 0.01, "LFO 1 phase dot (2 Hz, retriggered by the note): worst error %.4f of a cycle (limit 0.01)", worstL);
    }
    {   // several voices: one dot each (newest first), capped at 8
        auto q = makeProc (-1); q->traceOn.store (true); settle (*q);
        juce::MidiBuffer m; for (int n = 0; n < 10; ++n) m.addEvent (juce::MidiMessage::noteOn (1, 48 + n, 0.8f), n * 4);
        blk.clear(); q->processBlock (blk, m); blk.clear(); juce::MidiBuffer none; q->processBlock (blk, none);
        ProProcessor::TraceSnap sn; const bool got = q->readTrace (sn);
        bool newestFirst = true; for (int i = 1; i < sn.count; ++i) newestFirst &= sn.v[i - 1].age >= sn.v[i].age;
        CHECK (got && sn.count == ProProcessor::kTraceVoices && newestFirst, "10 notes: %d dots (cap %d), newest first", sn.count, ProProcessor::kTraceVoices);
        q->traceOn.store (false);
        const auto seq = sn.seq; blk.clear(); q->processBlock (blk, none); ProProcessor::TraceSnap sn2; q->readTrace (sn2);
        CHECK (sn2.seq == seq, "nothing published while the MOD page is not showing");
    }
    {   // tearing: an audio thread publishing while this thread reads 20000 times
        auto q = makeProc (-1); q->traceOn.store (true); settle (*q);
        std::atomic<bool> go { true };
        std::thread audio ([&]
        {
            juce::AudioBuffer<float> b (2, 64); int k = 0;
            while (go.load()) { juce::MidiBuffer m; if (k % 50 == 0) m.addEvent (juce::MidiMessage::noteOn (1, 48 + k % 24, 0.8f), 0); if (k % 50 == 25) m.addEvent (juce::MidiMessage::allNotesOff (1), 0); b.clear(); q->processBlock (b, m); ++k; std::this_thread::sleep_for (std::chrono::microseconds (300)); }   // an audio period, roughly
        });
        int reads = 0, good = 0, bad = 0;
        for (int i = 0; i < 20000; ++i)
        {
            if (i % 50 == 0) std::this_thread::sleep_for (std::chrono::microseconds (200));
            ProProcessor::TraceSnap sn; ++reads;
            if (q->readTrace (sn)) { ++good; for (int v = 0; v < sn.count; ++v) for (int e = 0; e < ProProcessor::kTraceEnvs; ++e) if (! (sn.v[v].stage[e] >= 0 && sn.v[v].stage[e] <= 4 && std::isfinite (sn.v[v].lin[e]) && sn.v[v].lin[e] >= -0.01f && sn.v[v].lin[e] <= 1.01f)) ++bad; }
        }
        go.store (false); audio.join();
        CHECK (good > reads / 4 && bad == 0, "20000 reads while the audio thread publishes: %d consistent snapshots, %d with impossible values (torn)", good, bad);
    }
    {   // screenshots: MOD page with a chord held (the dots on ENV 2 / ENV 3), BLACK / PURPLE / LIGHT
        dir.createDirectory();
        auto q = makeProc (progOf ("Trap Starter Stack"));
        q->setParam ("e2_a", 0.3f); q->setParam ("e2_d", 1.5f); q->setParam ("e2_s", 0.4f); q->setParam ("e3_a", 0.05f); q->setParam ("e3_d", 0.6f); q->setParam ("e3_s", 0.6f); settle (*q);
        std::unique_ptr<juce::AudioProcessorEditor> ed (q->createEditor()); auto* pe = dynamic_cast<ProEditor*> (ed.get()); pe->setSize (ProEditor::W, ProEditor::H); pe->showTab (1); pump (100);
        juce::MidiBuffer m; for (int n : { 48, 55, 60, 64 }) m.addEvent (juce::MidiMessage::noteOn (1, n, 0.8f), 0);
        juce::AudioBuffer<float> b (2, BS); b.clear(); q->processBlock (b, m);
        for (int k = 0; k < 40; ++k) { if (k == 20) { juce::MidiBuffer m2; m2.addEvent (juce::MidiMessage::noteOn (1, 67, 0.8f), 0); b.clear(); q->processBlock (b, m2); } juce::MidiBuffer none; b.clear(); q->processBlock (b, none); }
        static const char* tn[] { "black", "purple", "light" };
        for (int t = 0; t < 3; ++t)
        {
            pe->setTheme (t); pump (120);
            auto img = pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f);
            juce::File f = dir.getChildFile (juce::String ("pro-env-dots-") + tn[t] + ".png"); f.deleteFile();
            if (auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream())) juce::PNGImageFormat().writeImageToStream (img, *os);
            printf ("      %s\n", f.getFullPathName().toRawUTF8());
        }
    }
    return 0;
}
#endif

// ------------------------------------------------------------------ SNAPSHOTS (Shared/SnapshotBank.h)
#ifndef THEME_REF
int testSnapshots()
{
    section ("SNAPSHOTS: take 3, cycle, exact restore, persistence in a new instance, corrupt file ignored, no dropout while playing");
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("gp-snap-" + juce::String (juce::Time::currentTimeMillis()));
    auto p = makeProc (progOf ("Trap Starter Stack")); p->snapshots->setFolder (dir);
    const float cuts[3] { 0.25f, 0.55f, 0.85f };
    std::vector<juce::AudioBuffer<float>> refs;
    for (int i = 0; i < 3; ++i)
    {
        p->setParam ("cutoff", cuts[i]); p->setParam ("mac4", 0.3f * (float) i);
        for (auto& v : p->frameA) v = std::sin ((float) (i + 1) * 6.2831853f * (float) (&v - &p->frameA[0]) / ghosty::FRAME); p->rebuildWaves(); settle (*p);
        seedSystemRandom (99); refs.push_back (render (*p, testPhrase(), 2.0));
        p->snapshots->take(); juce::Thread::sleep (3);
    }
    CHECK (p->snapshots->size() == 3 && p->snapshots->info (0).name == "Snap 3", "3 snapshots, newest first (%s)", p->snapshots->size() > 0 ? p->snapshots->info (0).name.toRawUTF8() : "-");
    p->setCurrentProgram (0); settle (*p);
    CHECK (p->snapshots->current() == -1, "loading a factory sound clears the current snapshot");
    int sameState = 0; double worst = 0;
    for (int i = 0; i < 3; ++i)
    {
        p->snapshots->load (2 - i); settle (*p);
        auto now = p->captureSound(), want = p->snapshots->stateOf (2 - i);
        for (auto* t : { &now, &want }) { t->removeProperty ("patchName", nullptr); t->removeProperty ("patchUser", nullptr); }
        sameState += now.isEquivalentTo (want) ? 1 : 0;
        // the sound itself, from a fresh instance (no tails from earlier renders): exactly the reference rendered when it was taken
        auto f = makeProc (0); f->snapshots->setFolder (dir); f->snapshots->load (2 - i); settle (*f);
        seedSystemRandom (99); auto b = render (*f, testPhrase(), 2.0);
        auto g = makeProc (0); g->restoreSound (want, "x"); settle (*g); seedSystemRandom (99); auto c = render (*g, testPhrase(), 2.0);
        worst = std::max (worst, (double) maxDiff (b, c));
    }
    const float firstRef = [&] { auto f = makeProc (0); f->snapshots->setFolder (dir); f->snapshots->load (2); settle (*f); seedSystemRandom (99); return maxDiff (render (*f, testPhrase(), 2.0), refs[0]); }();
    CHECK (sameState == 3 && worst == 0.0 && firstRef == 0.0f, "each snapshot restores the whole sound: state identical (%d of 3, incl. drawn wave + macros), render exact (%.3g; vs the take-time render %.3g)", sameState, worst, firstRef);
    p->snapshots->load (0); p->snapshots->next(); const int a1 = p->snapshots->current(); p->snapshots->next(); p->snapshots->next(); const int a3 = p->snapshots->current();
    CHECK (a1 == 1 && a3 == 0, "next cycles with wrap (0 > 1 > 2 > 0)");
    {   // BYPASS and the look are kept across a snapshot load
        p->setParam ("bypass", 1.0f); p->uiTheme.store (2); p->snapshots->load (1);
        CHECK (p->bypassParam->get() && p->uiTheme.load() == 2, "a snapshot load keeps BYPASS and the look");
        p->setParam ("bypass", 0.0f);
    }
    // persistence: a new instance lists them; a corrupt file is ignored
    dir.getChildFile ("snapshot-broken.xml").replaceWithText ("<GHOSTSNAPSHOT name=\"x\" time=\"1\"><PARAMS");
    auto q = makeProc (0); q->snapshots->setFolder (dir);
    CHECK (q->snapshots->size() == 3 && q->snapshots->corruptCount() == 1, "a new instance lists the 3 snapshots, the corrupt file is skipped (%d)", q->snapshots->corruptCount());
    q->snapshots->load (2); settle (*q); seedSystemRandom (99); auto qb = render (*q, testPhrase(), 2.0);
    CHECK (maxDiff (qb, refs[0]) == 0.0f, "...and loading one there plays the same sound (max difference %.3g)", maxDiff (qb, refs[0]));
    {   // no dropout: snapshots loaded between blocks while a chord plays
        auto r = makeProc (progOf ("Trap Starter Stack")); r->snapshots->setFolder (dir); settle (*r);
        juce::AudioBuffer<float> blk (2, BS); int silent = 0, nans = 0, blocks = 0;
        for (int k = 0; k < 200; ++k)
        {
            juce::MidiBuffer m; if (k == 0) for (int n : { 48, 55, 60 }) m.addEvent (juce::MidiMessage::noteOn (1, n, 0.8f), 0);
            if (k > 20 && k % 30 == 0) r->snapshots->load ((k / 30) % 3);
            blk.clear(); r->processBlock (blk, m); ++blocks;
            int nn = 0; peakOf (blk, &nn); nans += nn;
            if (k > 10 && blk.getRMSLevel (0, 0, BS) < 1.0e-5f) ++silent;
        }
        CHECK (silent == 0 && nans == 0, "6 snapshot loads while a chord plays: %d silent blocks, %d NaN", silent, nans);
    }
    {   // the S key in the editor, the SNAPSHOTS bank at the top of the browser
        auto e = makeProc (0); e->snapshots->setFolder (dir);
        std::unique_ptr<juce::AudioProcessorEditor> ed (e->createEditor()); ed->setSize (ProEditor::W, ProEditor::H); pump (50);
        const int before = e->snapshots->size();
        ed->keyPressed (juce::KeyPress ('s'));   // a plain S is a piano key in hosts: never a snapshot
        CHECK (e->snapshots->size() == before, "a plain S does NOT take a snapshot (%d -> %d)", before, e->snapshots->size());
        ed->keyPressed (juce::KeyPress ('s', juce::ModifierKeys::shiftModifier | juce::ModifierKeys::altModifier, 0));
        CHECK (e->snapshots->size() == before + 1, "Shift+Alt+S takes a snapshot (%d -> %d)", before, e->snapshots->size());
    }
    dir.deleteRecursively();
    return 0;
}
#endif

// ------------------------------------------------------------------ MORPH BOARD (ProMorph.h + Shared/SpiritBoard.h)
#ifndef THEME_REF
int testMorph (const juce::File& dir)
{
    section ("MORPH BOARD: a ghost alone = its sound exactly, 50 % = the midpoint, switches follow the leader with hysteresis, state, automation, no NaN");
    const int pA = progOf ("Trap Starter Stack"), pB = progOf ("Dark Choir");
    auto sA = ProProcessor::factorySound (pA), sB = ProProcessor::factorySound (pB);
    // the fixture starts where it always did: program 0 as the round-1 engine played it. (SOUND ENGINE V2's program 0 runs the GHOST VERB and
    // HYPER, whose glides / tails a morph carries on from - as it should - while a plain load starts them fresh: not "exactly that sound")
    auto startProc = [] { auto q = makeProc (-1); q->setSoundEngine (ProProcessor::engineClassic); q->setCurrentProgram (0); q->setSoundEngine (ProProcessor::engineV2); settle (*q); return q; };
    auto p = startProc();
    p->morphAddGhost (0.2f, 0.5f, sA, "Trap Starter Stack"); p->morphAddGhost (0.8f, 0.5f, sB, "Dark Choir");
    p->morph.enabled.store (true); p->morphChanged (false);
    auto block = [&] (ProProcessor& q, int nb = 2) { juce::AudioBuffer<float> b (2, BS); for (int k = 0; k < nb; ++k) { juce::MidiBuffer m; b.clear(); q.processBlock (b, m); } };
    auto puck = [&] (ProProcessor& q, float x, float y) { q.setParam ("morph_x", x); q.setParam ("morph_y", y); block (q); q.morphTick(); settle (q); block (q, 4); };
    puck (*p, 0.2f, 0.5f);
    seedSystemRandom (99); auto a = render (*p, testPhrase(), 2.0);
    // the ghost's sound loaded plainly, with the same block history (free-running LFOs etc. advance per block)
    auto ref = startProc(); block (*ref); ref->restoreSound (sA, "A"); settle (*ref); block (*ref, 4); seedSystemRandom (99); auto r = render (*ref, testPhrase(), 2.0);
    CHECK (maxDiff (a, r) == 0.0f, "the puck on ghost A (a factory preset): exactly that sound (max difference %.3g)", maxDiff (a, r));
    // 50 / 50: every continuous parameter at the midpoint of the two sounds (in its own normalised domain)
    puck (*p, 0.5f, 0.5f);
    int bad = 0, checked = 0;
    for (auto* id : { "cutoff", "res", "attack", "release", "o1_det", "verb", "gain", "o1_pos", "fenv" })
    {
        auto* prm = dynamic_cast<juce::RangedAudioParameter*> (p->apvts.getParameter (id));
        auto valueIn = [&] (const juce::ValueTree& t) { auto c = t.getChildWithProperty ("id", id); return c.isValid() ? (float) c.getProperty ("value") : prm->convertFrom0to1 (prm->getDefaultValue()); };
        const float want = prm->convertFrom0to1 (0.5f * (prm->convertTo0to1 (valueIn (sA)) + prm->convertTo0to1 (valueIn (sB))));
        const float got = p->macros.effective (id);
        ++checked; if (std::abs (got - want) > 1e-4f * (prm->getNormalisableRange().end - prm->getNormalisableRange().start) + 1e-6f) { ++bad; printf ("      %s: %.5f want %.5f\n", id, got, want); }
    }
    CHECK (bad == 0, "puck half-way: %d continuous parameters at the midpoint of A and B (%d off)", checked, bad);
    // switches follow the highest weight, with hysteresis
    puck (*p, 0.45f, 0.5f); const int l1 = p->morph.lastLeader;
    puck (*p, 0.51f, 0.5f); const int l2 = p->morph.lastLeader;
    puck (*p, 0.70f, 0.5f); const int l3 = p->morph.lastLeader;
    CHECK (l1 == 0 && l2 == 0 && l3 == 1, "the leader (switches, tables, FX): A at 45 %%, still A just past the middle (hysteresis), B at 70 %% (%d %d %d)", l1, l2, l3);
    // the puck as host automation reaches the engine
    p->apvts.getParameter ("morph_x")->setValueNotifyingHost (0.2f); block (*p, 3);
    const float c1 = p->macros.effective ("cutoff");
    p->apvts.getParameter ("morph_x")->setValueNotifyingHost (0.8f); block (*p, 40);
    const float c2 = p->macros.effective ("cutoff");
    auto cutIn = [&] (const juce::ValueTree& t) { auto c = t.getChildWithProperty ("id", "cutoff"); return c.isValid() ? (float) c.getProperty ("value") : 1.0f; };
    CHECK (std::abs (c1 - cutIn (sA)) < 1e-5f && std::abs (c2 - cutIn (sB)) < 1e-5f, "automating Morph X moves the engine: cutoff %.3f (A %.3f) -> %.3f (B %.3f)", c1, cutIn (sA), c2, cutIn (sB));
    // saved in the session; never in a sound
    {
        juce::MemoryBlock mb; p->getStateInformation (mb);
        auto q = makeProc (0); q->setStateInformation (mb.getData(), (int) mb.getSize()); settle (*q); block (*q, 3);
        CHECK (q->morphBoard.ghosts.size() == 2 && q->morph.enabled.load() && std::abs (q->macros.effective ("cutoff") - p->macros.effective ("cutoff")) < 1e-5f,
               "state round trip: 2 ghosts, the board on, the same blend (cutoff %.4f vs %.4f)", q->macros.effective ("cutoff"), p->macros.effective ("cutoff"));
        CHECK (! p->captureSound().getChildWithName ("MORPHBOARD").isValid() && ! p->captureSound().getChildWithProperty ("id", "morph_x").isValid(), "a sound / snapshot never carries the board or the puck");
    }
    {   // the FX rack blends too: an FX parameter that differs between A and B sits at its midpoint half-way
        juce::String fxId; float fa = 0, fb = 0;
        for (auto* prm0 : p->getParameters())
            if (auto* fp = dynamic_cast<juce::AudioParameterFloat*> (prm0); fp != nullptr && fp->getParameterID().startsWith ("fx_") && fxId.isEmpty())
            {
                auto in = [&] (const juce::ValueTree& t) { auto c = t.getChildWithProperty ("id", fp->getParameterID()); return c.isValid() ? (float) c.getProperty ("value") : fp->convertFrom0to1 (static_cast<juce::RangedAudioParameter*> (fp)->getDefaultValue()); };
                if (std::abs (in (sA) - in (sB)) > 0.05f * (fp->range.end - fp->range.start)) { fxId = fp->getParameterID(); fa = in (sA); fb = in (sB); }
            }
        puck (*p, 0.5f, 0.5f); block (*p, 30);
        auto* fp = dynamic_cast<juce::AudioParameterFloat*> (p->apvts.getParameter (fxId));
        const float got = fxId.isNotEmpty() ? p->apvts.getRawParameterValue (fxId)->load() : -1.0f;
        const float want = fp != nullptr ? fp->convertFrom0to1 (0.5f * (fp->convertTo0to1 (fa) + fp->convertTo0to1 (fb))) : 0.0f;
        CHECK (fxId.isNotEmpty() && std::abs (got - want) < 1e-4f * (fp->range.end - fp->range.start) + 1e-6f, "the FX rack blends: %s at %.4f half-way between A %.4f and B %.4f (want %.4f)", fxId.toRawUTF8(), got, fa, fb, want);
        // the knobs follow the board while MORPH is on, and MORPH OFF keeps the blend as the knobs' values
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor()); auto* pe = dynamic_cast<ProEditor*> (ed.get()); pe->setSize (ProEditor::W, ProEditor::H); pe->showTab (0); pump (200);
        juce::Slider* cut = nullptr; std::function<void (juce::Component&)> find = [&] (juce::Component& c) { for (auto* k : c.getChildren()) { if (auto* sl = dynamic_cast<juce::Slider*> (k); sl != nullptr && sl->getProperties()["paramId"].toString() == "cutoff") cut = sl; find (*k); } };
        find (*pe);
        const float blendCut = p->macros.effective ("cutoff");
        CHECK (cut != nullptr && std::abs (cut->getValue() - blendCut) < 1e-4, "MORPH ON: the CUTOFF knob shows the board's live value (%.4f, the board %.4f, the stored value %.4f)", cut ? cut->getValue() : -1.0, blendCut, p->apvts.getRawParameterValue ("cutoff")->load());
        p->morphCommit(); p->morph.enabled.store (false); block (*p, 3); pump (100);
        CHECK (std::abs (p->apvts.getRawParameterValue ("cutoff")->load() - blendCut) < 1e-4f && std::abs (p->macros.effective ("cutoff") - blendCut) < 1e-4f && cut != nullptr && std::abs (cut->getValue() - blendCut) < 1e-4,
               "MORPH OFF: the blend stays (cutoff %.4f stored, %.4f playing), the knob agrees", p->apvts.getRawParameterValue ("cutoff")->load(), p->macros.effective ("cutoff"));
        p->morph.enabled.store (true);
    }
    {   // an orbit and random puck jumps: never NaN, never silent
        auto q = makeProc (0); q->morphAddGhost (0.1f, 0.1f, sA, "A"); q->morphAddGhost (0.9f, 0.2f, sB, "B"); q->morphAddGhost (0.5f, 0.9f, ProProcessor::factorySound (progOf ("Hyper Saw Anthem")), "C");
        q->morphBoard.orbit.mode = ghosty::spirit::Orbit::Figure8; q->morphBoard.orbit.depth = 0.4f; q->morphBoard.orbit.rate = 2.0f; q->morphBoard.orbit.sync = false;
        q->morph.enabled.store (true); q->morphChanged (false); settle (*q);
        juce::AudioBuffer<float> b (2, BS); int nans = 0; float pk = 0;
        for (int k = 0; k < 400; ++k)
        {
            juce::MidiBuffer m; if (k % 50 == 0) m.addEvent (juce::MidiMessage::noteOn (1, 48 + k % 12, 0.8f), 0); if (k % 50 == 40) m.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            if (k % 37 == 0) { q->setParam ("morph_x", juce::Random::getSystemRandom().nextFloat()); q->setParam ("morph_y", juce::Random::getSystemRandom().nextFloat()); }
            if (k % 25 == 0) q->morphTick();
            b.clear(); q->processBlock (b, m); int nn = 0; pk = std::max (pk, peakOf (b, &nn)); nans += nn;
        }
        CHECK (nans == 0 && pk > 0.01f && pk < 1.0f, "3 ghosts, a figure-8 orbit and random puck jumps for 400 blocks: %d NaN, peak %.2f", nans, pk);
    }
    {   // off = the sound as it is (no override)
        auto q = makeProc (pA); q->morphAddGhost (0.2f, 0.5f, sB, "B"); q->morph.enabled.store (false); q->morphChanged (false); settle (*q);
        seedSystemRandom (99); auto o = render (*q, testPhrase(), 2.0);
        auto q0 = makeProc (pA); seedSystemRandom (99); auto r0 = render (*q0, testPhrase(), 2.0);
        CHECK (maxDiff (o, r0) == 0.0f, "MORPH off: the board changes nothing (max difference %.3g)", maxDiff (o, r0));
    }
    {   // PLAY settings (arp, scale, chords, mono ...) are how you play, not part of a ghost: the leader changing must never flip them
        auto q = makeProc (pA);
        auto setP = [&] (const char* id, float plain) { auto* pr = q->apvts.getParameter (id); pr->setValueNotifyingHost (pr->convertTo0to1 (plain)); };
        setP ("arp", 1.0f); setP ("scale", 3.0f); setP ("chord", 2.0f); setP ("arpRate", 3.0f); setP ("arpMode", 1.0f); setP ("arpOct", 2.0f); setP ("mono", 0.0f); setP ("legato", 1.0f);
        q->morphAddGhost (0.1f, 0.5f, sA, "A"); q->morphAddGhost (0.9f, 0.5f, sB, "B");   // both factory sounds: arp off, scale 0, chord 0
        q->morph.enabled.store (true); q->morphChanged (false);
        const char* ids[] { "arp", "scale", "chord", "arpRate", "arpMode", "arpOct", "mono", "legato" };
        float before[8], after[8]; for (int i = 0; i < 8; ++i) before[i] = q->apvts.getRawParameterValue (ids[i])->load();
        q->apvts.getParameter ("morph_x")->setValueNotifyingHost (0.1f); block (*q, 3); pump (50);
        q->apvts.getParameter ("morph_x")->setValueNotifyingHost (0.9f); block (*q, 3); pump (50);   // the leader moves from A to B
        q->apvts.getParameter ("morph_x")->setValueNotifyingHost (0.1f); block (*q, 3); pump (50);   // and back
        bool same = true; for (int i = 0; i < 8; ++i) { after[i] = q->apvts.getRawParameterValue (ids[i])->load(); same = same && after[i] == before[i]; }
        CHECK (same, "the leader changing A -> B -> A keeps your arp / scale / chords / arp rate, mode, octaves, mono and legato (arp %.0f scale %.0f chord %.0f)", after[0], after[1], after[2]);
    }
    {   // edits made while MORPH is OFF belong to the ghost you were on: turning MORPH back on must keep them
        auto q = makeProc (pA);
        q->morphAddGhost (0.1f, 0.5f, sA, "A"); q->morphAddGhost (0.9f, 0.5f, sB, "B");
        q->morph.enabled.store (true); q->morphChanged (false);
        q->apvts.getParameter ("morph_x")->setValueNotifyingHost (0.1f); block (*q, 3); pump (50);        // on ghost A
        q->morphCommit(); q->morph.enabled.store (false); q->morph.lastLeader = -1; q->morphChanged (false); pump (50);   // MORPH off
        auto* pr = q->apvts.getParameter ("cutoff"); const float oldCut = pr->convertFrom0to1 (pr->getValue());
        pr->setValueNotifyingHost (pr->convertTo0to1 (oldCut < 0.5f ? 0.9f : 0.1f)); const float newCut = pr->convertFrom0to1 (pr->getValue());   // you tweak the sound
        q->morphWriteBackEdits(); q->morph.enabled.store (true); q->morph.lastLeader = -1; q->morphChanged (false);   // MORPH on again
        q->apvts.getParameter ("morph_x")->setValueNotifyingHost (0.9f); block (*q, 3); pump (50);        // go to B
        q->apvts.getParameter ("morph_x")->setValueNotifyingHost (0.1f); block (*q, 3); pump (50);        // and back to A
        const float nowCut = q->macros.effective ("cutoff");
        CHECK (std::abs (nowCut - newCut) < 0.02f && std::abs (nowCut - oldCut) > 0.1f, "your edits made while MORPH was off are kept when it goes on again (cutoff %.2f -> %.2f, after the round trip %.2f)", oldCut, newCut, nowCut);
    }
    // screenshots of the MORPH tab, all three looks
    {
        dir.createDirectory();
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor()); auto* pe = dynamic_cast<ProEditor*> (ed.get()); pe->setSize (ProEditor::W, ProEditor::H);
        pe->showTab (6); pump (150);
        static const char* tn[] { "black", "purple", "light" };
        for (int t = 0; t < 3; ++t)
        {
            pe->setTheme (t); pump (120);
            auto img = pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f);
            juce::File f = dir.getChildFile (juce::String ("pro-morph-") + tn[t] + ".png"); f.deleteFile();
            if (auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream())) juce::PNGImageFormat().writeImageToStream (img, *os);
            int checkedC = 0; auto issues = contrastAudit (*pe, *pe->contentComponent(), checkedC); int badC = 0; for (auto& i : issues) if (i.enabled) ++badC;
            CHECK (badC == 0, "MORPH tab %s: %d texts checked, %d unreadable (%s)", tn[t], checkedC, badC, f.getFileName().toRawUTF8());
        }
    }
    return 0;
}
#endif

// ------------------------------------------------------------------ SYNTH page: click an OSC panel to show it; OSC 1 pink / OSC 2 blue
#ifndef THEME_REF
int testOscSelect (const juce::File& dir)
{
    section ("OSC PANELS: a click in the OSC 1 / OSC 2 panel shows it above, knob drags still turn the knob, OSC 1 pink / OSC 2 blue in all looks");
    auto p = makeProc (0);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor()); auto* pe = dynamic_cast<ProEditor*> (ed.get()); pe->setSize (ProEditor::W, ProEditor::H); pe->showTab (0); pump (100);
    juce::Slider* pos2 = nullptr; juce::TextButton* v1 = nullptr; juce::TextButton* v2 = nullptr;
    std::function<void (juce::Component&)> find = [&] (juce::Component& c)
    {
        for (auto* k : c.getChildren())
        {
            if (auto* sl = dynamic_cast<juce::Slider*> (k); sl != nullptr && sl->getProperties()["paramId"].toString() == "o2_pos") pos2 = sl;
            if (auto* b = dynamic_cast<juce::TextButton*> (k)) { if (b->getButtonText() == "OSC 1") v1 = b; if (b->getButtonText() == "OSC 2") v2 = b; }
            find (*k);
        }
    };
    find (*pe);
    if (pos2 == nullptr || v1 == nullptr || v2 == nullptr) { CHECK (false, "found the OSC 2 POS knob and the view buttons"); return 0; }
    auto* knob2 = pos2->getParentComponent(); auto* page = knob2->getParentComponent();
    auto src = juce::Desktop::getInstance().getMainMouseSource();
    auto clickAt = [&] (juce::Component* target, juce::Point<float> pos)
    {
        const auto now = juce::Time::getCurrentTime();
        juce::MouseEvent down (src, pos, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0, target, target, now, pos, now, 1, false);
        juce::MouseEvent up (src, pos, juce::ModifierKeys(), 1.0f, 0, 0, 0, 0, target, target, now, pos, now, 1, false);
        // deliver like JUCE: to the component under the mouse, then its listeners (the page listens to the OSC knobs)
        target->mouseDown (down); if (target != page) page->mouseDown (down.getEventRelativeTo (target)); target->mouseUp (up);
    };
    const bool start = v1->getToggleState() && ! v2->getToggleState();
    // empty space in the OSC 2 panel (its title row, right of the title): find a point no child covers
    juce::Point<float> empty; bool foundEmpty = false;
    for (int y = 372; y < 372 + 24 && ! foundEmpty; y += 2) for (int x = 120; x < 400 && ! foundEmpty; x += 4) if (page->getComponentAt (x, y) == page) { empty = { (float) x, (float) y }; foundEmpty = true; }
    clickAt (page, empty);
    const bool panelClick = v2->getToggleState() && ! v1->getToggleState();
    // a knob caption in the OSC 1 panel (the caption lets the click through to the knob, which the page listens to)
    juce::Slider* pos1 = nullptr; std::function<void (juce::Component&)> f1 = [&] (juce::Component& c) { for (auto* k : c.getChildren()) { if (auto* sl = dynamic_cast<juce::Slider*> (k); sl != nullptr && sl->getProperties()["paramId"].toString() == "o1_pos") pos1 = sl; f1 (*k); } };
    f1 (*pe);
    clickAt (pos1->getParentComponent(), { 20.0f, 5.0f });
    const bool captionClick = v1->getToggleState() && ! v2->getToggleState();
    // a drag on OSC 2's POS knob turns it and shows OSC 2
    const double before = pos2->getValue();
    {
        const auto c = pos2->getLocalBounds().getCentre().toFloat(); const auto now = juce::Time::getCurrentTime();
        juce::MouseEvent down (src, c, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0, pos2, pos2, now, c, now, 1, false);
        juce::MouseEvent drag (src, c.translated (0, -40), juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0, pos2, pos2, now, c, now, 1, true);
        juce::MouseEvent up (src, c.translated (0, -40), juce::ModifierKeys(), 1.0f, 0, 0, 0, 0, pos2, pos2, now, c, now, 1, true);
        pos2->mouseDown (down); pos2->mouseDrag (drag); pos2->mouseUp (up);
    }
    const bool dragTurns = pos2->getValue() > before + 0.01 && v2->getToggleState();
    CHECK (start && foundEmpty && panelClick && captionClick && dragTurns, "click in the OSC 2 panel -> OSC 2 shown (%d), a click on an OSC 1 caption -> OSC 1 (%d), dragging OSC 2 POS turns it (%.2f -> %.2f) and shows OSC 2 (%d)",
           (int) panelClick, (int) captionClick, before, pos2->getValue(), (int) dragTurns);
    dir.createDirectory();
    static const char* tn[] { "black", "purple", "light" };
    for (int t = 0; t < 3; ++t)
    {
        const auto P = pt::Palette::make (ghosty::themes::fromInt (t));
        double lo = 99; for (auto bgc : { P.bg, P.card, P.card2 }) lo = std::min ({ lo, contrastOf (P.pink, bgc), contrastOf (P.ice, bgc) });
        const auto D = pt::Palette::make (ghosty::themes::Theme::Dark);                      // the display is a dark screen in every look
        const double screen = std::min (contrastOf (D.pink, D.deep), contrastOf (D.ice, D.deep));
        pe->setTheme (t); pump (100);
        auto img = pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f);
        juce::File f = dir.getChildFile (juce::String ("pro-oscsel-") + tn[t] + ".png"); f.deleteFile();
        if (auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream())) juce::PNGImageFormat().writeImageToStream (img, *os);
        CHECK (lo >= 3.0 && screen >= 3.0, "%s: OSC 1 pink / OSC 2 blue on the panels >= %.2f:1, on the display >= %.2f:1 (%s)", tn[t], lo, screen, f.getFileName().toRawUTF8());
    }
    return 0;
}
#endif

// ------------------------------------------------------------------ UI conventions: tooltips, sliders, version
#ifndef THEME_REF
int testUi()
{
    section ("UI: one tooltip window, slider convention (bars jump to the click, double-click = default, type-in, hover value), version");
    auto p = makeProc (progOf ("Trap Starter Stack"));
    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
    auto* pe = dynamic_cast<ProEditor*> (ed.get());
    pe->setSize (ProEditor::W, ProEditor::H); pump (50);
    int tipWindows = 0; std::vector<juce::Slider*> sliders;
    std::function<void (juce::Component&)> walk = [&] (juce::Component& c) { for (auto* k : c.getChildren()) { if (dynamic_cast<juce::TooltipWindow*> (k)) ++tipWindows; if (auto* sl = dynamic_cast<juce::Slider*> (k)) sliders.push_back (sl); walk (*k); } };
    walk (*pe);
    auto shown = [&] (juce::Component* c) { for (; c != nullptr && c != pe; c = c->getParentComponent()) if (! c->isVisible()) return false; return c == pe; };
    CHECK (tipWindows == 1, "tooltip windows in the editor: %d (one, themed by the editor's look-and-feel)", tipWindows);
    for (int t : { 0, 1, 2 })
    {
        pe->setTheme (t);
        auto& lf = pe->getLookAndFeel();
        const auto bg = lf.findColour (juce::TooltipWindow::backgroundColourId), tx = lf.findColour (juce::TooltipWindow::textColourId);
        CHECK (bg.isOpaque() && contrastOf (tx, bg) >= 7.0, "%s tooltip colours: text %.1f:1 on an opaque background", t == 0 ? "BLACK " : t == 1 ? "PURPLE" : "LIGHT ", contrastOf (tx, bg));
    }
    pe->setTheme (0);
    // the knobs / sliders of the Ghosty Pro pages (the FX rack's own knobs follow the rack's convention)
    int withHover = 0, withFine = 0, withDefault = 0, rotary = 0, bars = 0;
    for (auto* sl : sliders)
    {
        bool inRack = false; for (auto* c = sl->getParentComponent(); c != nullptr; c = c->getParentComponent()) inRack |= dynamic_cast<gpfx::Panel*> (c) != nullptr;
        if (inRack) continue;
        withHover += (bool) sl->getProperties()["hoverValue"] ? 1 : 0;
        withFine += sl->getVelocityModeIsSwappable() ? 1 : 0;
        withDefault += sl->isDoubleClickReturnEnabled() ? 1 : 0;
        (sl->isRotary() ? rotary : bars) += 1;
    }
    CHECK (withHover == rotary + bars && withFine == rotary + bars && withDefault >= (rotary + bars) * 9 / 10,
           "%d knobs + %d bars: hover value %d, Shift = fine %d, double-click default %d", rotary, bars, withHover, withFine, withDefault);
    // a bar (the mod matrix AMOUNT): a click at 25 / 50 / 75 %% of its track sets that value; double-click returns to the default
    pe->showTab (1); pump (100);
    juce::Slider* bar = nullptr;
    for (auto* sl : sliders) if (! sl->isRotary() && shown (sl) && bar == nullptr) { bool inRack = false; for (auto* c = sl->getParentComponent(); c != nullptr; c = c->getParentComponent()) inRack |= dynamic_cast<gpfx::Panel*> (c) != nullptr; if (! inRack) bar = sl; }
    if (bar != nullptr)
    {
        auto src = juce::Desktop::getInstance().getMainMouseSource();
        auto click = [&] (float x, int clicks)
        {
            const juce::Point<float> pos (x, bar->getHeight() * 0.5f); const auto now = juce::Time::getCurrentTime();
            juce::MouseEvent down (src, pos, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0, bar, bar, now, pos, now, clicks, false);
            juce::MouseEvent up (src, pos, juce::ModifierKeys(), 1.0f, 0, 0, 0, 0, bar, bar, now, pos, now, clicks, false);
            if (clicks == 2) bar->mouseDoubleClick (down); else { bar->mouseDown (down); bar->mouseUp (up); }
        };
        const auto layout = bar->getLookAndFeel().getSliderLayout (*bar);
        const float x0 = (float) layout.sliderBounds.getX(), w = (float) layout.sliderBounds.getWidth();
        double worst = 0;
        for (double frac : { 0.25, 0.5, 0.75 })
        {
            click (x0 + (float) frac * w, 1);
            const double want = bar->proportionOfLengthToValue (frac), got = bar->getValue();
            worst = std::max (worst, std::abs (got - want) / (bar->getMaximum() - bar->getMinimum()));
            printf ("      click at %2.0f %%: value %.3f (want %.3f)\n", frac * 100, got, want);
        }
        CHECK (worst < 0.03, "bar: a click sets the value at the click position (worst error %.1f %% of the range)", worst * 100);
        click (x0 + 0.8f * w, 2);
        CHECK (std::abs (bar->getValue() - bar->getDoubleClickReturnValue()) < 1e-6, "double-click returns the bar to its default (%.3f)", bar->getValue());
        // type-in (what right-click opens): the text in real units goes through the parameter's own parser
        const double typed = bar->getValueFromText (bar->getTextFromValue (bar->proportionOfLengthToValue (0.3)));
        CHECK (std::abs (typed - bar->proportionOfLengthToValue (0.3)) < 0.02 * (bar->getMaximum() - bar->getMinimum()), "type-in: \"%s\" -> %.3f", bar->getTextFromValue (bar->proportionOfLengthToValue (0.3)).toRawUTF8(), typed);
    }
    else CHECK (false, "no bar slider found on the MOD page");
    // automation: a host change moves the knob, and a knob move reaches the host parameter
    {
        auto* prm = p->apvts.getParameter ("cutoff"); prm->setValueNotifyingHost (0.3f); pump (50);
        juce::Slider* cut = nullptr; pe->showTab (0); pump (50);
        for (auto* sl : sliders) if (sl->getTooltip() == "Cutoff") cut = sl;
        CHECK (cut != nullptr && std::abs (cut->getValue() - prm->convertFrom0to1 (0.3f)) < 1e-4, "host automation moves the knob (cutoff at 30 %%: %.3f)", cut ? cut->getValue() : -1.0);
        if (cut != nullptr) { cut->setValue (prm->convertFrom0to1 (0.7f), juce::sendNotificationSync); CHECK (std::abs (prm->getValue() - 0.7f) < 1e-4, "a knob move reaches the host parameter (%.3f)", prm->getValue()); }
    }
    {   // every unit the knobs show reads back: text -> value -> the same text
        int bad = 0; juce::StringArray tried;
        for (auto [id, txt] : std::initializer_list<std::pair<const char*, const char*>> { { "cutoff", "1.6 kHz" }, { "cutoff", "440 Hz" }, { "attack", "250 ms" }, { "release", "1.20 s" },
                                                                                          { "gain", "-6.0 dB" }, { "res", "40%" }, { "m1_amt", "-40%" }, { "o1_fine", "-12 ct" } })
        {
            auto* prm = p->apvts.getParameter (id);
            const auto back = prm->getText (prm->getValueForText (txt), 32);
            tried.add (juce::String (id) + " \"" + txt + "\" -> \"" + back + "\"");
            if (back.removeCharacters (" ") != juce::String (txt).removeCharacters (" ")) ++bad;
        }
        CHECK (bad == 0, "typed values in real units: %s", tried.joinIntoString (", ").toRawUTF8());
    }
    CHECK (ProEditor::versionString().isNotEmpty(), "version string shown under the PRO badge: v%s", ProEditor::versionString().toRawUTF8());
    return 0;
}
#endif

// ------------------------------------------------------------------ demos (WAV -> MP3 via ffmpeg)
struct Demo { const char* preset; int kind; };      // kind 0 chords, 1 bass line, 2 melody, 3 stabs, 4 pad
std::vector<Ev> phrase (int kind, double bpm, int bars)
{
    const double beat = 60.0 / bpm; std::vector<Ev> e;
    const int cm[] { 48, 55, 60, 63 }, ab[] { 44, 51, 56, 60 }, fm[] { 41, 48, 53, 56 }, g[] { 43, 50, 55, 59 };
    const int* prog[] { cm, ab, fm, g };
    for (int bar = 0; bar < bars; ++bar)
    {
        const double t0 = bar * 4 * beat; const int* ch = prog[bar % 4];
        switch (kind)
        {
            case 0: for (int i = 0; i < 4; ++i) e = e + note (ch[i], t0, 3.6 * beat, 0.85f); break;
            case 3: for (double s : { 0.0, 1.5, 2.0, 3.0, 3.5 }) for (int i = 0; i < 4; ++i) e = e + note (ch[i], t0 + s * beat, 0.4 * beat, s == 0.0 ? 1.0f : 0.8f); break;
            case 4: for (int i = 0; i < 4; ++i) e = e + note (ch[i], t0 - 0.02 * beat, 4.0 * beat, 0.7f); break;
            case 1:
            {
                const int root = ch[0] - 12;
                for (double s : { 0.0, 0.75, 1.5, 2.5, 3.0 }) e = e + note (s == 2.5 ? root + 3 : root, t0 + s * beat, s == 1.5 ? 1.0 * beat : 0.6 * beat, 1.0f);
                break;
            }
            case 2:
            {
                const int mel[] { ch[2] + 12, ch[3] + 12, ch[2] + 12, ch[1] + 12, ch[3] + 12, ch[2] + 12 };
                const double at[] { 0.0, 0.5, 1.0, 2.0, 2.5, 3.0 }, len[] { 0.45, 0.45, 0.9, 0.45, 0.45, 0.9 };
                for (int i = 0; i < 6; ++i) e = e + note (mel[i], t0 + at[i] * beat, len[i] * beat, 0.8f);
                break;
            }
        }
    }
    return e;
}

int demo (const juce::File& dir)
{
    dir.createDirectory();
    const Demo demos[] { { "Trap Horn Section", 3 }, { "Doit Fall Brass", 2 }, { "Growl Horn Lead", 2 }, { "Boom Sub Long", 1 }, { "Distorted Sub Long", 1 },
                         { "Breathy Flute", 2 }, { "Dark Trap Piano", 0 }, { "Music Box Phantom", 2 }, { "Tape Brass Slowed", 0 }, { "Dark Choir", 4 }, { "Trap Starter Stack", 0 }, { "Riser Reverse Stack", 4 } };
    const double bpm = 140; const int bars = 4;
    for (auto& d : demos)
    {
        auto p = makeProc (progOf (d.preset));
        const double len = bars * 4 * 60.0 / bpm + 4.0;
        auto buf = render (*p, phrase (d.kind, bpm, bars), len, bpm);
        int nans = 0; const float pk = peakOf (buf, &nans);
        const double L = lufs (buf, 0, (int) ((len - 4.0) * SR)), tail = rmsDb (buf, (int) ((len - 0.4) * SR), (int) (len * SR));
        const bool hasScene = gpro::factoryBank()[(size_t) progOf (d.preset)].scene.isNotEmpty();   // a reverb / dub scene is still ringing 4 s after the last note
        int left = 0; for (int k = 0; k < gpro::NUM_LAYERS; ++k) left += p->layerVoices[k].load();
        auto wav = dir.getChildFile (safeName (d.preset) + ".wav"); writeWav (buf, wav);
        auto mp3 = dir.getChildFile (safeName (d.preset) + ".mp3"); mp3.deleteFile();
        juce::ChildProcess ff; bool made = false;
        if (ff.start (juce::StringArray { "ffmpeg", "-loglevel", "error", "-y", "-i", wav.getFullPathName(), "-codec:a", "libmp3lame", "-q:a", "2", mp3.getFullPathName() }))
        { ff.waitForProcessToFinish (60000); made = mp3.existsAsFile() && mp3.getSize() > 10000; }
        if (made) wav.deleteFile();
        CHECK (nans == 0 && pk <= 1.0f && L > -26 && L < -6 && tail < (hasScene ? -30 : -50) && left == 0 && made, "%-22s %d NaN, peak %.2f, %.1f LUFS, tail %.1f dB%s, voices left %d -> %s (%d kB)",
               d.preset, nans, pk, L, tail, hasScene ? " (FX scene)" : "", left, mp3.getFileName().toRawUTF8(), (int) (mp3.getSize() / 1024));
        fflush (stdout);
    }
    return 0;
}
} // namespace
#if ! defined (THEME_REF) && ! defined (LEADS_REF)
#include "LeadsTests.inc"
#endif

// diagnostic: what differs between a factory sound loaded fresh and the same sound after a save / load round trip
static void paramDiff (const juce::String& name)
{
    auto p = makeProc (progOf (name));
    juce::MemoryBlock mb; p->getStateInformation (mb);
    auto q = makeProc (0); q->setStateInformation (mb.getData(), (int) mb.getSize()); settle (*q);
    int diffs = 0;
    for (auto* prm : p->getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
        {
            auto* o = q->apvts.getParameter (rp->getParameterID());
            if (o == nullptr || std::abs (o->getValue() - rp->getValue()) > 1.0e-6f) { ++diffs; printf ("  %-12s fresh %.5f  reloaded %.5f\n", rp->getParameterID().toRawUTF8(), rp->convertFrom0to1 (rp->getValue()), o ? o->convertFrom0to1 (o->getValue()) : -1.0f); }
        }
    const auto a = p->rack.saveState().toXmlString(), b = q->rack.saveState().toXmlString();
    printf ("  %d parameter differences; rack state %s; scene '%s' vs '%s'\n", diffs, a == b ? "same" : "DIFFERENT", p->currentScene().toRawUTF8(), q->currentScene().toRawUTF8());
    if (a != b) printf ("%s\n----\n%s\n", a.toRawUTF8(), b.toRawUTF8());
    auto ra = seededRender (*p), rb = seededRender (*q);
    printf ("  render max diff %.3g, LUFS %.1f vs %.1f\n", maxDiff (ra, rb), lufs (ra), lufs (rb));
}

// diagnostics for the regression harness: does seeding the system Random take, and do two fresh processors render alike?
static void seedCheck (const juce::String& name)
{
    seedSystemRandom (5); const int a1 = juce::Random::getSystemRandom().nextInt(), a2 = juce::Random::getSystemRandom().nextInt();
    seedSystemRandom (5); const int b1 = juce::Random::getSystemRandom().nextInt(), b2 = juce::Random::getSystemRandom().nextInt();
    printf ("  system Random after seeding: %d %d / %d %d -> %s\n", a1, a2, b1, b2, (a1 == b1 && a2 == b2) ? "repeatable" : "NOT repeatable");
    auto p = makeProc (progOf (name)), q = makeProc (progOf (name));
    auto ra = seededRender (*p), rb = seededRender (*q);
    int w = 0; float m = 0; for (int i = 0; i < ra.getNumSamples(); ++i) { const float d = std::abs (ra.getSample (0, i) - rb.getSample (0, i)); if (d > m) { m = d; w = i; } }
    int first = -1; for (int i = 0; i < ra.getNumSamples(); ++i) if (std::abs (ra.getSample (0, i) - rb.getSample (0, i)) > 1.0e-7f) { first = i; break; }
    printf ("  %s: two fresh processors, max diff %.4g at %d, first diff at %d\n", name.toRawUTF8(), m, w, first);
    auto rc = seededRender (*p);
    printf ("  the same processor rendered again (tails / LFO phases carry over): max diff %.4g\n", maxDiff (ra, rc));
}

static int newModes (const juce::String& mode, const juce::StringArray& args)
{
    if (mode == "seedcheck") { seedCheck (args.size() > 1 ? args[1] : juce::String ("Fat Hoover Saw")); return 0; }
    if (mode == "paramdiff") { paramDiff (args.size() > 1 ? args[1] : juce::String ("Sync Screamer")); return 0; }
    if (mode == "stack") { testStack(); return 0; }
    if (mode == "sample") { testSample(); return 0; }
    if (mode == "character") { testCharacter(); return 0; }
    if (mode == "tape") { testTape(); return 0; }
    if (mode == "brass") { testBrass(); return 0; }
    if (mode == "state") { testState(); r2::v2StateTests(); return 0; }
    if (mode == "cpu") { testCpu(); return 0; }
    if (mode == "gate") { testGate(); return 0; }
    if (mode == "bypass") { testBypass(); return 0; }
    if (mode == "sweep") { juce::StringArray only (args); only.remove (0); testKnobSweep (only); printf ("\n%d failures\n", failures); return 0; }
    if (mode == "pos") { testPos (args.size() > 1 && args[1] == "v"); printf ("\n%d failures\n", failures); return 0; }
#if ! defined (THEME_REF) && ! defined (LEADS_REF)
    if (mode == "leads") return leadsModes (args);
#endif
    if (mode == "all") { testStack(); testSample(); testCharacter(); testTape(); testBrass(); testState(); testGate(); testBypass(); testCpu();
#if ! defined (THEME_REF) && ! defined (LEADS_REF)
                            leadCheck(); leadState(); leadThreads();
#endif
                            printf ("\n%d failures\n", failures); return 0; }
#ifndef THEME_REF
    if (mode == "draw") { testDraw (juce::File (args.size() > 1 ? args[1] : juce::String ("/root/work/trap/shots"))); printf ("\n%d failures\n", failures); return 0; }
    if (mode == "trace") { testTrace (juce::File (args.size() > 1 ? args[1] : juce::String ("/root/work/trap/shots"))); printf ("\n%d failures\n", failures); return 0; }
    if (mode == "snap") { testSnapshots(); printf ("\n%d failures\n", failures); return 0; }
    if (mode == "morph") { testMorph (juce::File (args.size() > 1 ? args[1] : juce::String ("/root/work/trap/shots"))); printf ("\n%d failures\n", failures); return 0; }
    if (mode == "oscsel") { testOscSelect (juce::File (args.size() > 1 ? args[1] : juce::String ("/root/work/trap/shots"))); printf ("\n%d failures\n", failures); return 0; }
    if (mode == "ui") { testUi(); r2::v2UiTests(); printf ("\n%d failures\n", failures); return 0; }
    if (mode == "audit") { testUiAudit (juce::File (args.size() > 1 ? args[1] : juce::String ("/root/work/trap/audit"))); printf ("\n%d failures\n", failures); return 0; }
    if (mode == "fatten") { testFattenPro (juce::File (args.size() > 1 ? args[1] : juce::String ("/root/work/fatten/proshots")), juce::File (args.size() > 2 ? args[2] : juce::String())); printf ("\n%d failures\n", failures); return 0; }
    if (mode == "glass") { glassPadTests (juce::File (args.size() > 1 ? args[1] : juce::String ("/root/work/clang-glass/uishots"))); glassLightTests (juce::File (args.size() > 1 ? args[1] : juce::String ("/root/work/clang-glass/uishots"))); printf ("\n%d failures\n", failures); return 0; }   // GLASS (round 3)
    if (mode == "theme") { themeShots (juce::File (args.size() > 1 ? args[1] : juce::String ("/root/work/trap/shots"))); printf ("\n%d failures\n", failures); return 0; }
#endif
    if (mode == "shots") return shots (juce::File (args.size() > 1 ? args[1] : juce::String ("/root/work/trap/shots")));
    if (mode == "demo") return demo (juce::File (args.size() > 1 ? args[1] : juce::String ("/root/work/trap/demos")));
    return -1;
}
