// Ghosty Spectrum GHOST VOICE tests. Every voice is synthesised here (glottal pulses through formant resonators at known F1..F4);
// no recordings of anyone. Measures: F0 RMSE (cents) vs the input, pitch-contour correlation, voiced / unvoiced agreement, timing
// drift, formant positions of the blend landing between A's and B's by the blend percentage, and identity consistency (the blend
// does not flicker between A and B from syllable to syllable).
//   GhostVoiceTests              -> engine + host + page tests (exit code = failures)
//   GhostVoiceTests shots DIR    -> PNG screenshots of the page (the three house looks)
//   GhostVoiceTests demos DIR    -> WAVs: A only, B only, 75/25, 50/50, 25/75 (+ a designed-preset blend and a 3-way blend)
#include "VoiceFusion.h"
#include "VoiceFusionClear.h"
#include "VoiceFusionHost.h"
#include "GhostVoicePage.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_dsp/juce_dsp.h>
#include <iostream>

using namespace voice;
static int fails = 0, passes = 0;
static void check(bool ok, const juce::String& what, const juce::String& detail = {}) {
    std::cout << (ok ? "  PASS  " : "  FAIL  ") << what << (detail.isNotEmpty() ? "  (" + detail + ")" : juce::String()) << std::endl;
    (ok ? passes : fails)++;
}
static constexpr double kPi = juce::MathConstants<double>::pi;

// ---------------------------------------------------------------------------------------------------------------- signals
struct Reso { double a1 = 0, a2 = 0, g = 1, y1 = 0, y2 = 0;
    void set(double f, double bw, double fs) { const double r = std::exp(-kPi * bw / fs), th = 2 * kPi * f / fs; a1 = -2 * r * std::cos(th); a2 = r * r; g = (1 - r) * std::sqrt(1 - 2 * r * std::cos(2 * th) + r * r); }
    double tick(double x) { const double y = g * x - a1 * y1 - a2 * y2; y2 = y1; y1 = y; return y; } };
struct VoiceSpec { std::function<double(double)> f0; std::function<std::array<double, 4>(double)> F; double seconds = 1.5, breath = 0.01; std::array<double, 4> bw { 80, 100, 140, 180 }; };
static std::vector<float> synthVoice(const VoiceSpec& v, int fs, unsigned seed = 1) {
    const int n = (int) (v.seconds * fs); std::vector<float> x((size_t) n); juce::Random rnd((juce::int64) seed);
    std::array<Reso, 4> R; double phase = 0, prev = 0; float peak = 0;
    for (int i = 0; i < n; ++i) {
        const double t = i / (double) fs; const double f = v.f0(t); double src = 0;
        if (f > 0) { phase += f / fs; if (phase >= 1) phase -= 1; const double op = 0.4, cl = 0.16; src = phase < op ? 0.5 * (1 - std::cos(kPi * phase / op)) : phase < op + cl ? std::cos(0.5 * kPi * (phase - op) / cl) : 0.0; src -= 0.3; }
        src += v.breath * (rnd.nextFloat() * 2 - 1) + (f > 0 ? 0.0 : 0.25 * (rnd.nextFloat() * 2 - 1));
        if ((i & 31) == 0) { const auto F = v.F(t); for (int k = 0; k < 4; ++k) R[(size_t) k].set(F[(size_t) k], v.bw[(size_t) k], fs); }
        double y = src; for (auto& r : R) y = r.tick(y);
        const double out = y - 0.97 * prev; prev = y; const double env = std::min(1.0, std::min(t, v.seconds - t) * 30.0);
        x[(size_t) i] = (float) (out * env); peak = std::max(peak, std::abs(x[(size_t) i]));
    }
    for (auto& s : x) s *= 0.7f / std::max(1e-6f, peak);
    return x;
}
static std::array<double, 4> vowelF(int i, int set = VowelsMale, double scale = 1.0) { const auto& V = vowelTable(set)[(size_t) i]; return { V.F[0] * scale, V.F[1] * scale, V.F[2] * scale, V.F[3] * scale }; }
// the performance: a sung phrase, 6 notes with vibrato, vowels changing per note, short unvoiced gaps (consonants) between notes
struct Phrase { std::vector<double> notes; std::vector<int> vowels; double noteLen = 0.5, gap = 0.06; double vib = 0.3, vibHz = 5.3; };
static Phrase defaultPhrase() { return { { 57, 60, 62, 64, 62, 59, 57 }, { 0, 1, 2, 3, 4, 0, 2 } }; }
static double phraseSeconds(const Phrase& ph) { return ph.notes.size() * (ph.noteLen + ph.gap) + 0.1; }
static VoiceSpec phraseSpec(const Phrase& ph, int set, double scale, double f0Scale, std::array<double, 4> bw = { 80, 100, 140, 180 }) {
    VoiceSpec v; v.seconds = phraseSeconds(ph); v.bw = bw;
    v.f0 = [ph, f0Scale](double t) { const double per = ph.noteLen + ph.gap; const int i = std::min((int) ph.notes.size() - 1, (int) (t / per)); const double u = t - i * per; if (u > ph.noteLen || t >= ph.notes.size() * per) return 0.0;
                                     return midiHz(ph.notes[(size_t) i] + ph.vib * std::sin(2 * kPi * ph.vibHz * t) * std::min(1.0, u / 0.15)) * f0Scale; };
    v.F = [ph, set, scale](double t) { const double per = ph.noteLen + ph.gap; const int i = std::min((int) ph.notes.size() - 1, (int) (t / per)); return vowelF(ph.vowels[(size_t) i], set, scale); };
    return v;
}
static std::vector<float> voiceA(int fs) { return synthVoice(phraseSpec(defaultPhrase(), VowelsMale, 1.0, 1.0), fs, 1); }
// voice B: a different singer (female table, smaller tract, higher, narrower bandwidths) singing OTHER notes and vowels in another order
static std::vector<float> voiceB(int fs) { Phrase ph = { { 62, 64, 65, 60, 62, 67, 65, 62 }, { 2, 0, 4, 1, 3, 0, 2, 1 }, 0.42, 0.05, 0.25, 6.0 }; return synthVoice(phraseSpec(ph, VowelsFemale, 1.0, 1.0, { 60, 80, 110, 150 }), fs, 7); }
static std::vector<float> voiceC(int fs) { Phrase ph = { { 45, 47, 48, 50, 47 }, { 3, 4, 0, 1, 2 }, 0.6, 0.08, 0.2, 4.6 }; return synthVoice(phraseSpec(ph, VowelsMale, 0.86, 1.0, { 100, 120, 160, 200 }), fs, 11); }

// ---------------------------------------------------------------------------------------------------------------- measures
static Analysis analysed(const std::vector<float>& x, int fs) { Analysis a; AnalysisOptions o; analyse(x.data(), (int) x.size(), fs, o, a); return a; }
static AudioRef refOf(const std::vector<float>& x, int fs) { AudioRef r; r.ch.push_back(x.data()); r.n = (int) x.size(); r.fs = fs; return r; }
static std::vector<float> mixdown(const Rendered& r) { if (!r.stereo()) return r.L; std::vector<float> m(r.L.size()); for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (r.L[i] + r.R[i]); return m; }
// an independent pitch tracker for the measurements (not WORLD): YIN's cumulative mean normalised difference per 10 ms, 60..1000 Hz,
// the first dip under 0.15 (de Cheveigne & Kawahara 2002, written from the paper), parabolic refinement
static std::vector<double> ownF0(const std::vector<float>& x, int fs, double hop = 0.01) {
    std::vector<double> out; const int H = (int) (hop * fs), W = (int) (0.03 * fs), lagMin = fs / 1000, lagMax = fs / 60;
    std::vector<double> d((size_t) lagMax + 1), cm((size_t) lagMax + 1);
    for (int c = 0; c + W + lagMax < (int) x.size(); c += H) {
        double e0 = 0; for (int i = 0; i < W; ++i) e0 += (double) x[(size_t) (c + i)] * x[(size_t) (c + i)];
        if (e0 / W < 1e-7) { out.push_back(0); continue; }
        double run = 0; cm[0] = 1;
        for (int l = 1; l <= lagMax; ++l) { double s = 0; for (int i = 0; i < W; ++i) { const double df = (double) x[(size_t) (c + i)] - x[(size_t) (c + i + l)]; s += df * df; } d[(size_t) l] = s; run += s; cm[(size_t) l] = s * l / std::max(1e-18, run); }
        int bl = 0; for (int l = lagMin; l < lagMax; ++l) if (cm[(size_t) l] < 0.15) { while (l + 1 < lagMax && cm[(size_t) l + 1] < cm[(size_t) l]) ++l; bl = l; break; }
        if (bl == 0) { out.push_back(0); continue; }
        if (2 * bl + 1 < lagMax) { int l2 = 2 * bl; for (int d = -2; d <= 2; ++d) if (cm[(size_t) (2 * bl + d)] < cm[(size_t) l2]) l2 = 2 * bl + d; if (cm[(size_t) l2] < cm[(size_t) bl] - 0.03) bl = l2; }   // a clearly deeper dip an octave down = the real period
        const double y0 = cm[(size_t) bl - 1], y1 = cm[(size_t) bl], y2 = cm[(size_t) bl + 1], den = y0 - 2 * y1 + y2; const double off = std::abs(den) > 1e-12 ? juce::jlimit(-0.5, 0.5, 0.5 * (y0 - y2) / den) : 0.0;
        out.push_back(fs / (bl + off));
    }
    return out;
}
struct PitchStats { double rmseCents = 0, corr = 0, vuvAgree = 0; int voicedBoth = 0; };
static PitchStats comparePitch(const std::vector<double>& ref, const std::vector<double>& got, double shiftSemis = 0) {
    PitchStats s; const size_t n = std::min(ref.size(), got.size()); double se = 0; int agree = 0; std::vector<double> a, b;
    auto nearEdge = [&](size_t i) { for (int d = -3; d <= 3; ++d) { const long j = (long) i + d; if (j < 0 || j >= (long) n) return true; if ((ref[(size_t) j] > 0) != (ref[i] > 0) || (got[(size_t) j] > 0) != (got[i] > 0)) return true; } return false; };
    for (size_t i = 0; i < n; ++i) { const bool va = ref[i] > 0, vb = got[i] > 0; agree += va == vb; if (va && vb && !nearEdge(i)) { const double c = 1200.0 * std::log2(got[i] / ref[i]) - 100.0 * shiftSemis; se += c * c; a.push_back(std::log2(ref[i])); b.push_back(std::log2(got[i])); } }
    s.voicedBoth = (int) a.size(); s.rmseCents = a.empty() ? 1e9 : std::sqrt(se / a.size()); s.vuvAgree = n ? agree / (double) n : 0;   // RMSE over the notes (30 ms either side of a voicing change left out: glides in / out of a note)
    if (a.size() > 2) { double ma = 0, mb = 0; for (size_t i = 0; i < a.size(); ++i) { ma += a[i]; mb += b[i]; } ma /= a.size(); mb /= a.size(); double sab = 0, saa = 0, sbb = 0; for (size_t i = 0; i < a.size(); ++i) { sab += (a[i] - ma) * (b[i] - mb); saa += (a[i] - ma) * (a[i] - ma); sbb += (b[i] - mb) * (b[i] - mb); } s.corr = sab / std::sqrt(std::max(1e-18, saa * sbb)); }
    return s;
}
// timing drift: the lag (ms) that best aligns the RMS envelopes of two signals (10 ms hop, +-100 ms search)
static double timingDriftMs(const std::vector<float>& a, const std::vector<float>& b, int fs) {
    const int H = fs / 100; auto env = [&](const std::vector<float>& x) { std::vector<double> e; for (size_t i = 0; i + (size_t) H <= x.size(); i += (size_t) H) { double s = 0; for (int k = 0; k < H; ++k) s += (double) x[i + (size_t) k] * x[i + (size_t) k]; e.push_back(std::sqrt(s / H)); } return e; };
    const auto ea = env(a), eb = env(b); const int n = (int) std::min(ea.size(), eb.size()); double best = -1; int bl = 0;
    for (int l = -10; l <= 10; ++l) { double s = 0, sa = 0, sb = 0; for (int i = 0; i < n; ++i) { const int j = i + l; if (j < 0 || j >= n) continue; s += ea[(size_t) i] * eb[(size_t) j]; sa += ea[(size_t) i] * ea[(size_t) i]; sb += eb[(size_t) j] * eb[(size_t) j]; } const double c = s / std::sqrt(std::max(1e-18, sa * sb)); if (c > best) { best = c; bl = l; } }
    return bl * 10.0;
}
// formants of a signal at a time (median over +-60 ms of WORLD's envelope formants)
static std::pair<double, double> formantsAt(const Analysis& a, double t) {
    std::vector<float> f1, f2; std::vector<double> row((size_t) a.bins());
    for (int i = a.frameAt(t - 0.06); i <= a.frameAt(t + 0.06); ++i) { if (!a.voiced(i)) continue; a.specRow(i, row.data()); const auto F = fusion::formantsLpc(row.data(), a.bins(), a.fs, a.f0[(size_t) i]); if (F[0].hz > 0 && F[1].hz > 0) { f1.push_back(F[0].hz); f2.push_back(F[1].hz); } }
    if (f1.empty()) return { 0, 0 }; std::sort(f1.begin(), f1.end()); std::sort(f2.begin(), f2.end()); return { f1[f1.size() / 2], f2[f2.size() / 2] };
}
static void writeWav(const juce::File& f, const Rendered& x) { f.getParentDirectory().createDirectory(); fusion::FusionHost::writeWav(f, x); std::cout << "  wrote " << f.getFullPathName() << std::endl; }
static void writeWav(const juce::File& f, const std::vector<float>& x, int fs) { Rendered r; r.L = x; r.fs = fs; writeWav(f, r); }

// ---------------------------------------------------------------------------------------------------------------- engine tests
static void testGridAndMorph() {
    std::cout << "-- grid + morph" << std::endl;
    // grid round trip of a smooth envelope
    const int fs = 48000, bins = 1025; std::vector<double> spec((size_t) bins), back((size_t) bins); const double binHz = fs / (2.0 * (bins - 1));
    for (int k = 0; k < bins; ++k) { const double f = k * binHz; spec[(size_t) k] = 1.0 / (1.0 + std::pow((f - 700) / 150, 2)) + 0.3 / (1.0 + std::pow((f - 1900) / 200, 2)) + 1e-4; }
    std::array<double, fusion::kGrid> g {}; const double lvl = fusion::toGrid(spec.data(), bins, fs, g.data()); fusion::fromGrid(g.data(), bins, fs, back.data());
    double maxErr = 0; for (int k = 2; k < bins; ++k) { const double f = k * binHz; if (f < 80 || f > 15000) continue; maxErr = std::max(maxErr, std::abs((back[(size_t) k] + lvl - std::log(spec[(size_t) k])) / std::log(10.0) * 10.0)); }
    check(maxErr < 1.0, "grid <-> bins round trip within 1 dB (80 Hz .. 15 kHz)", juce::String(maxErr, 2) + " dB");
    // morph of two single-formant envelopes at 50 %: ONE peak between them (not two)
    auto bump = [&](double f0, std::array<double, fusion::kGrid>& o) { for (int i = 0; i < fusion::kGrid; ++i) { const double f = fusion::gridHz(i); o[(size_t) i] = std::log(1.0 / (1.0 + std::pow((f - f0) / (0.1 * f0), 2)) + 1e-5); } };
    std::array<double, fusion::kGrid> L[2]; bump(500, L[0]); bump(1000, L[1]); Formants F[2] {}; F[0][0] = { 500, 60, 0 }; F[1][0] = { 1000, 90, 0 };
    const double w[2] = { 0.5, 0.5 }; std::array<double, fusion::kGrid> o {}; Formants oF {}; fusion::morphGrid(L, F, w, 2, o.data(), &oF);
    int peaks = 0; double peakHz = 0, pv = -1e9; for (int i = 1; i < fusion::kGrid - 1; ++i) if (o[(size_t) i] > o[(size_t) i - 1] && o[(size_t) i] >= o[(size_t) i + 1] && o[(size_t) i] > o[0] + 1.0) { ++peaks; if (o[(size_t) i] > pv) { pv = o[(size_t) i]; peakHz = fusion::gridHz(i); } }
    check(peaks == 1 && std::abs(peakHz - std::sqrt(500.0 * 1000.0)) / std::sqrt(500.0 * 1000.0) < 0.06 && std::abs(oF[0].hz - 707.1f) < 10, "50 % morph of 500 Hz + 1 kHz peaks = ONE peak at ~707 Hz (formant-aligned, no double peak)", juce::String(peaks) + " peaks, " + juce::String(peakHz, 0) + " Hz, F " + juce::String(oF[0].hz, 0));
    // naive average would have two peaks: show the difference
    int naive = 0; for (int i = 1; i < fusion::kGrid - 1; ++i) { const double v = 0.5 * (L[0][(size_t) i] + L[1][(size_t) i]), a = 0.5 * (L[0][(size_t) i - 1] + L[1][(size_t) i - 1]), b = 0.5 * (L[0][(size_t) i + 1] + L[1][(size_t) i + 1]); if (v > a && v >= b && v > 0.5 * (L[0][0] + L[1][0]) + 1.0) ++naive; }
    check(naive == 2, "(the naive average keeps both peaks: that is what the warp avoids)", juce::String(naive) + " peaks");
    // 25 / 75: the peak lands at the log-interpolated position
    const double w2[2] = { 0.25, 0.75 }; fusion::morphGrid(L, F, w2, 2, o.data(), &oF);
    check(std::abs(oF[0].hz - 500 * std::pow(2.0, 0.75)) < 12, "25 / 75 morph: formant at 500 * 2^0.75 = 841 Hz", juce::String(oF[0].hz, 0));
}

static void testIdentity(int fs) {
    std::cout << "-- identity @ " << fs << std::endl;
    const auto xb = voiceB(fs); const auto ab = analysed(xb, fs); const auto id = fusion::extractIdentity(ab);
    check(!id.empty() && (int) id.frames.size() >= 4 && (int) id.frames.size() <= fusion::kMaxFrames, "identity of a sung phrase: several vowel frames", juce::String((int) id.frames.size()) + " frames");
    check(std::abs(1200.0 * std::log2(id.medianF0 / midiHz(64))) < 300, "identity remembers the voice's pitch", juce::String(id.medianF0, 0) + " Hz");
    // its frames cover the vowels sung: for each vowel of B, some frame has F1 / F2 within 12 % of it
    int covered = 0; for (int v = 0; v < 5; ++v) { const auto Fv = vowelF(v, VowelsFemale); bool hit = false; for (auto& fr : id.frames) if (fr.F[0].hz > 0 && std::abs(std::log(fr.F[0].hz / Fv[0])) < 0.15 && std::abs(std::log(fr.F[1].hz / Fv[1])) < 0.15) hit = true; covered += hit; }
    check(covered >= 4, "identity frames cover the vowels B sang (F1 / F2 within ~15 %)", juce::String(covered) + " / 5");
    // state round trip
    const auto back = fusion::Identity::fromTree(id.toTree()); check(back == id && back.frames.size() == id.frames.size(), "identity saves and loads exactly");
    // the view for an A vowel picks B's version of that vowel
    const auto fa = vowelF(2, VowelsMale); Formants Fa {}; Fa[0] = { (float) fa[0], 60, 0 }; Fa[1] = { (float) fa[1], 90, -10 }; Fa[2] = { (float) fa[2], 120, -16 };
    const auto v = fusion::viewFor(id, Fa, nullptr); const auto Fi = vowelF(2, VowelsFemale);
    check(v.valid && std::abs(std::log(v.F[0].hz / Fi[0])) < 0.2 && std::abs(std::log(v.F[1].hz / Fi[1])) < 0.2, "the view of A's 'I' is B's 'I' (B's own F1 / F2)", juce::String(v.F[0].hz, 0) + " / " + juce::String(v.F[1].hz, 0) + " Hz vs " + juce::String(Fi[0], 0) + " / " + juce::String(Fi[1], 0));
    // designed identities exist, differ from each other, and name no one
    const auto& fac = fusion::factoryIdentities(); check(fac.size() >= 8, "factory identities", juce::String((int) fac.size()));
    bool distinct = true; for (size_t i = 0; i < fac.size(); ++i) for (size_t j = i + 1; j < fac.size(); ++j) if (fac[i] == fac[j]) distinct = false; check(distinct, "every factory identity is different");
    bool names = true; for (auto& d : fusion::factoryDesigns()) { const juce::String n = d.name; if (n.containsIgnoreCase("aphex") || n.containsIgnoreCase("twin") || n.containsIgnoreCase("bjork") || n.containsIgnoreCase("thom")) names = false; } check(names, "no real artist named in the presets");
}

static std::vector<double> worldF0(const std::vector<float>& x, int fs) { std::vector<double> f; AnalysisOptions o; trackF0(x.data(), (int) x.size(), fs, o, f); std::vector<double> out; for (size_t i = 0; i < f.size(); i += 2) out.push_back(f[i]); return out; }   // 10 ms hop
struct BlendMeasure { PitchStats pitch, pitchYin; double drift = 0; double posF1 = 0, posF2 = 0, consistency = 0, relPos = 0, relConsistency = 0; int vowelsMeasured = 0; };
// where the blend's formants land between A's (0) and B's (1) version of the vowel sung there, against the KNOWN synthetic formants
// (A = the adult-male table, B = the adult-female table); consistency = std dev of that position over the notes
static BlendMeasure measureBlend(const std::vector<float>& xa, const Analysis& aa, const fusion::Identity& idB, const std::vector<float>& y, int fs, float wB, const std::vector<double>& refF0, double shift = 0,
                                 const Analysis* a0 = nullptr, const Analysis* a100 = nullptr) {
    BlendMeasure m; std::vector<double> rel; const auto ay = analysed(y, fs); m.pitch = comparePitch(worldF0(xa, fs), worldF0(y, fs), shift); m.pitchYin = comparePitch(refF0, ownF0(y, fs), shift); m.drift = timingDriftMs(xa, y, fs);
    const auto ph = defaultPhrase(); std::vector<double> pos; int n1 = 0, n2 = 0;
    for (size_t i = 0; i < ph.notes.size(); ++i) {
        const double t = i * (ph.noteLen + ph.gap) + ph.noteLen * 0.55; const auto fy = formantsAt(ay, t); if (fy.first <= 0) continue;
        const auto FA = vowelF(ph.vowels[i], VowelsMale), FB = vowelF(ph.vowels[i], VowelsFemale);
        const double d1 = std::log(FB[0] / FA[0]), d2 = std::log(FB[1] / FA[1]);
        if (std::abs(d1) > 0.12) { const double p1 = std::log(fy.first / FA[0]) / d1; m.posF1 += p1; ++n1; pos.push_back(p1); }
        if (std::abs(d2) > 0.12) { const double p2 = std::log(fy.second / FA[1]) / d2; m.posF2 += p2; ++n2; pos.push_back(p2); }
        ++m.vowelsMeasured;
        if (a0 && a100) {   // against the engine's own 0 % and 100 % renders measured the same way: the estimator's per-vowel bias cancels
            const auto f0_ = formantsAt(*a0, t), f1_ = formantsAt(*a100, t); if (f0_.first <= 0 || f1_.first <= 0) continue;
            const double e1 = std::log(f1_.first / f0_.first), e2 = std::log(f1_.second / f0_.second);
            // only clear moves count (> 3.5 semitones); a jump of more than an octave is the estimator switching peaks, not a formant moving
            if (std::abs(e1) > 0.2 && std::abs(e1) < 0.7) rel.push_back(std::log(fy.first / f0_.first) / e1); if (std::abs(e2) > 0.2 && std::abs(e2) < 0.7) rel.push_back(std::log(fy.second / f0_.second) / e2);
        }
    }
    if (n1) m.posF1 /= n1; if (n2) m.posF2 /= n2;
    if (rel.size() > 1) {   // robust: the median position and the share of (note, formant) measurements within 0.25 of the blend asked for
        std::vector<double> srt = rel; std::sort(srt.begin(), srt.end()); m.relPos = srt[srt.size() / 2]; int in = 0; for (double p : rel) in += std::abs(p - wB) <= 0.25; m.relConsistency = in / (double) rel.size(); }
    if (pos.size() > 1) { double mean = 0; for (double p : pos) mean += p; mean /= pos.size(); double var = 0; for (double p : pos) var += (p - mean) * (p - mean); m.consistency = std::sqrt(var / pos.size()); }
    (void) wB; (void) idB; return m;
}

static void testBlend(int fs) {
    std::cout << "-- blend @ " << fs << std::endl;
    const auto xa = voiceA(fs), xb = voiceB(fs); const auto aa = analysed(xa, fs), ab = analysed(xb, fs); const auto idB = fusion::extractIdentity(ab);
    const auto refF0 = ownF0(xa, fs);
    Analysis a0, a100; { fusion::Params p; Rendered y; p.wB = 0; fusion::render(aa, refOf(xa, fs), &idB, nullptr, p, y); a0 = analysed(y.L, fs); p.wB = 1; fusion::render(aa, refOf(xa, fs), &idB, nullptr, p, y); a100 = analysed(y.L, fs); }
    for (float wB : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }) {
        fusion::Params p; p.wB = wB; Rendered y; fusion::Info info; const bool ok = fusion::render(aa, refOf(xa, fs), &idB, nullptr, p, y, {}, &info);
        check(ok && y.L.size() == xa.size() - (xa.size() % 1) && !y.stereo(), "render " + juce::String((int) (wB * 100)) + " % B: mono, the take's length", juce::String((int) y.L.size()) + " vs " + juce::String((int) xa.size()));
        const auto m = measureBlend(xa, aa, idB, y.L, fs, wB, refF0, 0, &a0, &a100);
        check(m.pitch.rmseCents < 12 && m.pitch.corr > 0.995, "  pitch exactly as sung: F0 RMSE < 12 cents, contour correlation > 0.995 (Harvest on both)", juce::String(m.pitch.rmseCents, 1) + " c, r = " + juce::String(m.pitch.corr, 4));
        check(m.pitchYin.rmseCents < 25 && m.pitchYin.corr > 0.99, "  second opinion (own YIN tracker): RMSE < 25 cents", juce::String(m.pitchYin.rmseCents, 1) + " c, r = " + juce::String(m.pitchYin.corr, 4));
        check(m.pitch.vuvAgree > 0.9, "  voiced / unvoiced agreement > 90 %", juce::String(m.pitch.vuvAgree * 100, 1) + " %");
        check(std::abs(m.drift) <= 10, "  timing drift <= 10 ms", juce::String(m.drift, 0) + " ms");
        if (wB > 0 && wB < 1) {
            check(m.vowelsMeasured >= 4 && std::abs(m.posF1 - wB) < 0.22 && std::abs(m.posF2 - wB) < 0.22, "  formants land between A and B at the blend %", "F1 at " + juce::String(m.posF1, 2) + ", F2 at " + juce::String(m.posF2, 2) + " (want " + juce::String(wB, 2) + ") over " + juce::String(m.vowelsMeasured) + " vowels");
            check(std::abs(m.relPos - wB) < 0.15 && m.relConsistency >= 0.75, "  identity consistent across the notes / vowels: median position vs the engine's own A / B renders within 0.15, >= 75 % of the formants within 0.25", "median " + juce::String(m.relPos, 2) + ", " + juce::String((int) std::round(m.relConsistency * 100)) + " % within 0.25 (truth-table std " + juce::String(m.consistency, 2) + ")");
        } else if (wB == 0) check(m.vowelsMeasured >= 4 && std::abs(m.posF1) < 0.25 && std::abs(m.posF2) < 0.25, "  0 % B = A's own formants (within the estimator's bias)", "F1 at " + juce::String(m.posF1, 2) + ", F2 at " + juce::String(m.posF2, 2));
        else check(m.vowelsMeasured >= 4 && std::abs(m.posF1 - 1) < 0.3 && std::abs(m.posF2 - 1) < 0.3, "  100 % B = B's formants (within the estimator's bias)", "F1 at " + juce::String(m.posF1, 2) + ", F2 at " + juce::String(m.posF2, 2));
        // clicks: no sample-to-sample jump beyond what the source has (x 1.5)
        float jumpA = 0, jumpY = 0; for (size_t i = 1; i < xa.size(); ++i) jumpA = std::max(jumpA, std::abs(xa[i] - xa[i - 1])); for (size_t i = 1; i < y.L.size(); ++i) jumpY = std::max(jumpY, std::abs(y.L[i] - y.L[i - 1]));
        check(jumpY < jumpA * 1.5f + 0.05f, "  no clicks (largest sample step vs the source)", juce::String(jumpY, 3) + " vs " + juce::String(jumpA, 3));
        // the blend weight actually used: 1 on vowels, 0 on the consonant gaps (KEEP MY CONSONANTS)
        if (wB > 0) { int on = 0, off = 0; for (int j = 0; j < info.frames(); ++j) { if (info.f0[(size_t) j] > 0) on += info.mixUsed[(size_t) j] > 0.9f * wB; else off += info.mixUsed[(size_t) j] < 0.5f * wB; }
            check(on > info.frames() / 2 && off > 0, "  blend applied on vowels, A kept on consonants", juce::String(on) + " vowel frames, " + juce::String(off) + " consonant frames"); }
    }
}

static void testDesignAndPitch(int fs) {
    std::cout << "-- design + pitch + stack @ " << fs << std::endl;
    const auto xa = voiceA(fs); const auto aa = analysed(xa, fs); const auto& glass = fusion::factoryIdentities()[0]; const auto refF0 = ownF0(xa, fs);
    { fusion::Params p; p.wB = 0.5f; p.octave = -1; p.semis = 2; Rendered y; fusion::render(aa, refOf(xa, fs), &glass, nullptr, p, y);
      const auto s = comparePitch(refF0, ownF0(y.L, fs), -10); check(s.rmseCents < 15 && s.corr > 0.99, "transpose -1 octave +2 st: pitch moved exactly, contour kept", juce::String(s.rmseCents, 1) + " c");
      const auto ay = analysed(y.L, fs); const auto fa = formantsAt(aa, 0.3), fy = formantsAt(ay, 0.3); const auto v = fusion::viewFor(glass, aa.formants[(size_t) aa.frameAt(0.3)], nullptr);
      const double want = std::exp(0.5 * std::log(fa.first) + 0.5 * std::log(v.F[0].hz));
      check(fy.first > 0 && std::abs(std::log(fy.first / want)) < 0.2, "formants independent of the transpose (F1 stays at the blend)", juce::String(fy.first, 0) + " Hz, want ~" + juce::String(want, 0)); }
    { fusion::Params p; p.wB = 0.5f; p.formant = 4; Rendered y1, y2; fusion::render(aa, refOf(xa, fs), &glass, nullptr, p, y1); p.formant = -4; fusion::render(aa, refOf(xa, fs), &glass, nullptr, p, y2);
      const auto f1 = formantsAt(analysed(y1.L, fs), 0.3), f2 = formantsAt(analysed(y2.L, fs), 0.3);
      const auto f0_ = formantsAt(aa, 0.3); check(f1.first > f2.first * 1.3, "FORMANT +4 / -4 st moves the formants up / down (A: " + juce::String(f0_.first, 0) + ")", juce::String(f1.first, 0) + " vs " + juce::String(f2.first, 0) + " Hz"); }
    { fusion::Params p; p.wB = 0.5f; p.tune = true; p.tuneMode = 1; p.root = 9; p.scale = 2; Rendered y; fusion::render(aa, refOf(xa, fs), &glass, nullptr, p, y);
      const auto f0 = ownF0(y.L, fs); int onKey = 0, voiced = 0; for (double f : f0) if (f > 0) { ++voiced; const double m = hzMidi(f); if (std::abs(m - snapMidi((float) m, 9, 2)) < 0.2) ++onKey; }
      check(voiced > 20 && onKey > voiced * 0.8, "HARD tuning to A minor: notes flat on the key", juce::String(onKey) + " / " + juce::String(voiced)); }
    { fusion::Params p; p.wB = 0.5f; p.voices = 4; p.octDown = 0.5f; Rendered y; fusion::render(aa, refOf(xa, fs), &glass, nullptr, p, y);
      double c = 0, l = 0, r = 0; for (size_t i = 0; i < y.L.size(); ++i) { c += (double) y.L[i] * y.R[i]; l += (double) y.L[i] * y.L[i]; r += (double) y.R[i] * y.R[i]; }
      check(y.stereo() && c / std::sqrt(l * r) < 0.97, "STACK 4 + octave down: a stereo ghost choir", "L/R correlation " + juce::String(c / std::sqrt(l * r), 3)); }
    { fusion::Params p; p.wB = 0.5f; p.breath = 0.8f; p.grit = 0.6f; p.brightness = 0.7f; p.warmth = -0.5f; p.resonance = 0.6f; p.weight = 0.5f; p.genderAge = 0.6f; Rendered y; const bool ok = fusion::render(aa, refOf(xa, fs), &glass, nullptr, p, y);
      bool fin = true; for (float v : y.L) fin = fin && std::isfinite(v); check(ok && fin && !y.L.empty(), "every design knob at once: finite audio"); }
    { fusion::Params p; p.wB = 0.4f; p.wC = 0.4f; const auto xc = voiceC(fs); const auto idC = fusion::extractIdentity(analysed(xc, fs)); Rendered y; const bool ok = fusion::render(aa, refOf(xa, fs), &glass, &idC, p, y);
      const auto s = comparePitch(refF0, ownF0(y.L, fs)); check(ok && s.rmseCents < 12, "3-way blend (20 / 40 / 40): pitch kept", juce::String(s.rmseCents, 1) + " c"); }
    { fusion::Params p; p.wB = 0.5f; p.trimStart = 0.5; p.trimEnd = 1.5; Rendered y; fusion::render(aa, refOf(xa, fs), &glass, nullptr, p, y);
      check(std::abs((double) y.L.size() / fs - 1.0) < 0.02, "START / END: only that second is rendered", juce::String((double) y.L.size() / fs, 3) + " s"); }
    { std::atomic<int> pct { 0 }; std::atomic<bool> stop { true }; fusion::Params p; p.wB = 0.5f; Rendered y; const bool ok = fusion::render(aa, refOf(xa, fs), &glass, nullptr, p, y, { &pct, &stop }); check(!ok && y.L.empty(), "cancel stops the render"); }
    { const auto idA = fusion::extractIdentity(aa); fusion::Params p; p.wB = 0.5f; const auto bl = fusion::blendIdentity(idA, &glass, nullptr, p);
      int between = 0, n = 0; for (size_t i = 0; i < bl.frames.size(); ++i) { const auto& fa = idA.frames[i].F[0]; const auto v = fusion::viewFor(glass, idA.frames[i].F, nullptr); const auto& fo = bl.frames[i].F[0]; if (fa.hz <= 0 || v.F[0].hz <= 0 || fo.hz <= 0) continue; ++n; const double lo = std::min(fa.hz, v.F[0].hz) * 0.97, hi = std::max(fa.hz, v.F[0].hz) * 1.03; between += fo.hz >= lo && fo.hz <= hi; }
      check(n > 0 && between >= n * 3 / 4, "SAVE VOICE: the blended identity's F1 sits between A's and B's (>= 75 % of the vowels; the rest = a peak paired differently)", juce::String(between) + " / " + juce::String(n)); }
}


// ---------------------------------------------------------------------------------------------------------------- CLEAR engine
static void testClear(int fs) {
    std::cout << "-- NATURAL engine @ " << fs << std::endl;
    const auto xa = voiceA(fs), xb = voiceB(fs); const auto aa = analysed(xa, fs), ab = analysed(xb, fs); const auto idB = fusion::extractIdentity(ab); const auto refF0 = ownF0(xa, fs);
    auto run = [&](float wB, fusion::Params p = {}) { p.engine = 1; p.wB = wB; Rendered y; fusion::Info info; fusion::render(aa, refOf(xa, fs), &idB, nullptr, p, y, {}, &info); return std::make_pair(y, info); };
    Analysis a0, a100; { a0 = analysed(run(0).first.L, fs); a100 = analysed(run(1).first.L, fs); }
    std::vector<double> lufs;
    for (float wB : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }) {
        const auto r = run(wB); const auto& y = r.first; lufs.push_back(fusion::clear::integratedLufs(y));
        bool fin = true; for (float v : y.L) fin = fin && std::isfinite(v);
        check(fin && y.L.size() == xa.size() && !y.stereo(), "NATURAL " + juce::String((int) (wB * 100)) + " % B: finite, mono, the take's length");
        const auto m = measureBlend(xa, aa, idB, y.L, fs, wB, refF0, 0, &a0, &a100);
        check(m.pitch.rmseCents < 8 && m.pitch.corr > 0.998, "  pitch exactly as sung (your own audio, filtered): F0 RMSE < 8 cents", juce::String(m.pitch.rmseCents, 1) + " c, r = " + juce::String(m.pitch.corr, 4));
        check(std::abs(m.drift) <= 3, "  timing drift <= 3 ms", juce::String(m.drift, 1) + " ms");
        if (wB > 0 && wB < 1) check(m.relPos > wB - 0.2 && m.relPos < wB + 0.2, "  formants between its own 0 % and 100 % at the blend %", "median " + juce::String(m.relPos, 2) + " (want " + juce::String(wB, 2) + ")");
        float jumpA = 0, jumpY = 0, pk = 0; for (size_t i = 1; i < xa.size(); ++i) jumpA = std::max(jumpA, std::abs(xa[i] - xa[i - 1])); for (size_t i = 1; i < y.L.size(); ++i) { jumpY = std::max(jumpY, std::abs(y.L[i] - y.L[i - 1])); pk = std::max(pk, std::abs(y.L[i])); }
        float pkA = 0; for (float v : xa) pkA = std::max(pkA, std::abs(v));
        check(jumpY / std::max(1e-6f, pk) < 1.5f * jumpA / pkA + 0.05f && pk <= 0.95f, "  no clicks, peak under -0.4 dBFS", juce::String(jumpY, 3) + " step, peak " + juce::String(pk, 3));
        if (wB == 0) {   // 0 % = your own recording back (no vocoder in the way): sample correlation with A
            double c = 0, l = 0, q = 0; for (size_t i = 0; i < xa.size(); ++i) { c += (double) xa[i] * y.L[i]; l += (double) xa[i] * xa[i]; q += (double) y.L[i] * y.L[i]; }
            check(c / std::sqrt(l * q) > 0.995, "  0 % = your own take back, sample for sample (correlation > 0.995)", juce::String(c / std::sqrt(l * q), 4));
        }
    }
    double lo = 1e9, hi = -1e9; for (double v : lufs) { lo = std::min(lo, v); hi = std::max(hi, v); }
    check(hi - lo < 0.5 && std::abs(lo - fusion::clear::kTargetLufs) < 0.3, "every blend at the same loudness (-18 LUFS, within 0.5 dB)", juce::String(lo, 1) + " .. " + juce::String(hi, 1) + " LUFS");
    { fusion::Params p; p.octave = -1; const auto r = run(0.5f, p); const auto st = comparePitch(refF0, ownF0(r.first.L, fs), -12); check(st.rmseCents < 15, "NATURAL with a pitch move (octave down, PSOLA): pitch moved exactly", juce::String(st.rmseCents, 1) + " c"); }
    { fusion::Params p; p.voices = 3; p.octDown = 0.4f; const auto r = run(0.5f, p); bool fin = true; for (float v : r.first.L) fin = fin && std::isfinite(v); check(r.first.stereo() && fin, "NATURAL + STACK 3 + octave layer: stereo, finite"); }
    { fusion::Params p; p.engine = 1; const auto t = p.toTree(); check(fusion::Params::fromTree(t).engine == 1, "the engine choice is saved with the session");
      fusion::Params old; const auto t0 = old.toTree(); check(!t0.hasProperty("engine") && fusion::Params::fromTree(t0).engine == 0, "older sessions (no engine saved) load as CLASSIC, written byte-identical"); }
    { fusion::FusionHost h; h.autoRender = false; check(h.params().engine == 1, "a fresh page starts on NATURAL"); }
    {   // the whole host state: NATURAL saved + reloaded stays NATURAL; a state saved before NATURAL existed (no engine key) loads as CLASSIC
        fusion::FusionHost h; h.autoRender = false; auto p = h.params(); p.engine = 1; p.wB = 0.4f; h.setParams(p); const auto st = h.toTree();
        fusion::FusionHost h2; h2.autoRender = false; h2.fromTree(st); check(h2.params().engine == 1 && std::abs(h2.params().wB - 0.4f) < 1e-6f, "a NATURAL session saves and reloads as NATURAL");
        auto old = st.createCopy(); auto gp = old.getChildWithName("GVPARAMS"); if (gp.isValid()) gp.removeProperty("engine", nullptr);
        fusion::FusionHost h3; h3.autoRender = false; h3.fromTree(old); check(gp.isValid() && h3.params().engine == 0, "an older session (saved before NATURAL) reloads as CLASSIC");
        p.engine = 0; h.setParams(p); check(!h.toTree().getChildWithName("GVPARAMS").hasProperty("engine"), "a CLASSIC session is written exactly as before (no engine key)");
    }
    {   // NATURAL with every design knob, BREATH and GRIT: finite, and it adds breath noise where asked
        fusion::Params p; p.breath = 0.8f; p.grit = 0.6f; p.brightness = 0.7f; p.warmth = -0.5f; p.resonance = 0.6f; p.weight = 0.5f; p.genderAge = 0.6f; const auto r = run(0.5f, p);
        bool fin = true; for (float v : r.first.L) fin = fin && std::isfinite(v); check(fin && !r.first.L.empty(), "NATURAL with every design knob, BREATH and GRIT: finite audio"); }
    {   // 3-way, trim, cancel
        const auto xc = voiceC(fs); const auto idC = fusion::extractIdentity(analysed(xc, fs)); fusion::Params p; p.engine = 1; p.wB = 0.4f; p.wC = 0.4f; Rendered y; const bool ok = fusion::render(aa, refOf(xa, fs), &idB, &idC, p, y);
        const auto s3 = comparePitch(refF0, ownF0(y.L, fs)); check(ok && s3.rmseCents < 8, "NATURAL 3-way blend: pitch kept", juce::String(s3.rmseCents, 1) + " c");
        p.trimStart = 0.5; p.trimEnd = 1.5; fusion::render(aa, refOf(xa, fs), &idB, nullptr, p, y); check(std::abs((double) y.L.size() / fs - 1.0) < 0.02, "NATURAL START / END: only that second", juce::String((double) y.L.size() / fs, 3) + " s");
        std::atomic<int> pct { 0 }; std::atomic<bool> stop { true }; fusion::Params q; q.engine = 1; q.wB = 0.5f; Rendered z; check(!fusion::render(aa, refOf(xa, fs), &idB, nullptr, q, z, { &pct, &stop }) && z.L.empty(), "NATURAL: cancel stops the render"); }
    { std::array<double, 4> x { 1, 0, 0, 0 }, y { 0, 2, 0, 0 }, o {}; double ang = 0; fusion::clear::slerp(x.data(), y.data(), 4, 0.5, o.data(), &ang);
      const double len = std::sqrt(o[0] * o[0] + o[1] * o[1]); check(std::abs(len - 1.5) < 1e-9 && std::abs(o[0] - o[1] / 1.0) < 1e-9 && std::abs(ang - kPi / 2) < 1e-9, "SLERP: halfway keeps the length (1.5, not the average's 1.12) and splits the angle", "length " + juce::String(len, 3)); }
}

// ---------------------------------------------------------------------------------------------------------------- host
static void testHost() {
    std::cout << "-- host" << std::endl;
    const int fs = 48000; const auto xa = voiceA(fs), xb = voiceB(fs);
    fusion::FusionHost h; h.autoRender = false; juce::String err;
    check(h.setTake(fusion::SlotA, xa, fs, "me", false, &err) && h.analysis(0) && h.identity(0), "take A loads + analyses (sync)");
    check(h.setTake(fusion::SlotB, xb, fs, "other", false, &err) && h.identity(1) && h.identity(1)->frames.size() > 3, "take B loads + its identity is extracted");
    auto p = h.params(); p.wB = 0.5f; h.setParams(p); check(h.renderNow(err) && h.result() && h.result()->audio.L.size() > 1000, "render now", err);
    check(!h.resultStale(), "result up to date after the render");
    p.wB = 0.75f; h.setParams(p); check(h.resultStale(), "a change makes it stale");
    check(h.undo() && std::abs(h.params().wB - 0.5f) < 1e-6f, "undo brings the blend back"); check(h.redo() && std::abs(h.params().wB - 0.75f) < 1e-6f, "redo");
    // a designed voice in C, then the worker render
    h.setIdentity(fusion::SlotC, fusion::factoryIdentities()[1], "Midnight"); p = h.params(); p.wB = 0.3f; p.wC = 0.3f; h.setParams(p); h.autoRender = true; h.requestRender(); h.waitIdle(60000);
    check(h.result() && h.result()->params.wC > 0.29f && h.result()->audio.L.size() > 1000, "worker render with B (recorded) + C (designed)");
    // state round trip (takes as FLAC, the designed identity as a tree)
    const auto xml = h.toTree().toXmlString(); fusion::FusionHost g2; g2.autoRender = false; g2.fromTree(juce::ValueTree::fromXml(xml)); g2.waitIdle(60000);
    check(g2.slot(0) && g2.slot(0)->take && std::abs(g2.slot(0)->seconds() - xa.size() / (double) fs) < 0.01 && g2.slot(2) && g2.slot(2)->presetName == "Midnight" && g2.identity(2) && *g2.identity(2) == fusion::factoryIdentities()[1] && std::abs(g2.params().wC - 0.3f) < 1e-5f, "state saves + loads (takes, the designed voice, the settings)");
    check(g2.renderNow(err), "the restored page renders", err);
    // recorder: 4-beat count-in at 120 bpm, then 1.5 s of audio through processRecord in 512-sample blocks
    { fusion::FusionHost r; r.autoRender = false; r.prepare(fs, 512); r.startRecording(fusion::SlotA, 4, 120.0); check(r.isCountingIn() && r.countInLeft() == 4, "count-in starts at 4");
      std::vector<float> in((size_t) fs * 4); for (size_t i = 0; i < in.size(); ++i) in[i] = 0.3f * (float) std::sin(2 * kPi * 200 * i / fs);
      juce::AudioBuffer<float> out(2, 512); double clickEnergy = 0; int blocksToRec = -1, blk = 0;
      for (size_t pos = 0; pos + 512 <= in.size() && blk < (int) (3.5 * fs / 512); pos += 512, ++blk) { out.clear(); r.processRecord(in.data() + pos, nullptr, 512, out); if (blocksToRec < 0 && r.isRecording()) blocksToRec = blk; for (int i = 0; i < 512; ++i) clickEnergy += (double) out.getSample(0, i) * out.getSample(0, i); }
      const double startSec = blocksToRec * 512.0 / fs;
      check(blocksToRec > 0 && std::abs(startSec - 2.0) < 0.02, "recording starts after 4 beats at 120 bpm (2.0 s)", juce::String(startSec, 3) + " s");
      check(clickEnergy > 1.0, "count-in clicks are audible", juce::String(clickEnergy, 2));
      r.stopRecording(); r.waitIdle(60000); auto s = r.slot(0); check(s && s->take && std::abs(s->seconds() - (3.5 - 2.0)) < 0.05 && s->analysis(), "the take lands in YOUR VOICE and is analysed", s ? juce::String(s->seconds(), 2) + " s" : "none");
      bool pure = true; for (size_t i = 100; i < 2000 && s && s->take; ++i) pure = pure && std::abs(s->take->mono[i] - 0.3f * (float) std::sin(2 * kPi * 200 * (i + (size_t) (2.0 * fs)) / fs)) < 1e-3f; check(pure, "the recorded audio is the input, sample-exact from the downbeat"); }
    // presets: SAVE VOICE, then load into B of a fresh host
    { juce::File f; check(h.saveVoice("Test ghost", err, &f) && f.existsAsFile(), "SAVE VOICE writes a .ghostvoice file", err);
      fusion::FusionHost g3; g3.autoRender = false; check(g3.loadVoice(f, fusion::SlotB, true, err) && g3.identity(1) && g3.identity(1)->frames.size() == h.identity(0)->frames.size() && std::abs(g3.params().wC - 0.3f) < 1e-5f, "the saved voice loads into B with its settings", err);
      check(fusion::FusionHost::presetFiles().contains("Test ghost"), "it is listed under MY VOICES"); f.deleteFile(); }
    // preview player: result plays, A/B swaps at the same spot, no clicks at the switch
    { h.prepare(fs, 256); h.play(fusion::PlayResult, 0.5); juce::AudioBuffer<float> b(2, 256); float pk = 0, jump = 0, prev = 0; for (int k = 0; k < 40; ++k) { b.clear(); h.process(b); for (int i = 0; i < 256; ++i) { const float v = b.getSample(0, i); pk = std::max(pk, std::abs(v)); jump = std::max(jump, std::abs(v - prev)); prev = v; } if (k == 20) h.toggleAB(); }
      check(pk > 0.01f && h.playing.load() == fusion::PlayA && jump < 0.25f, "preview plays, A/B switches to the dry voice without a click", "peak " + juce::String(pk, 3) + " jump " + juce::String(jump, 3)); h.stop(); }
    // cancel while rendering a long take
    { std::vector<float> longA; for (int k = 0; k < 20; ++k) longA.insert(longA.end(), xa.begin(), xa.end()); fusion::FusionHost c; c.autoRender = false; c.setTake(0, longA, fs, "long", true); c.waitIdle(120000); c.setIdentity(1, fusion::factoryIdentities()[0], "Glass Angel"); c.requestRender(); juce::Thread::sleep(300); c.cancel(); c.waitIdle(30000); check(c.result() == nullptr || c.result()->params.wB < 0, "cancel stops a long render (no result published)"); }
}

// ---------------------------------------------------------------------------------------------------------------- page
static void prepPage(fusion::FusionHost& h) {
    const int fs = 44100; h.autoRender = false; h.setTake(fusion::SlotA, voiceA(fs), fs, "my take", false); h.setTake(fusion::SlotB, voiceB(fs), fs, "singer B", false); h.setIdentity(fusion::SlotC, fusion::factoryIdentities()[3], "Dream Machine");
    auto p = h.params(); p.wB = 0.45f; p.wC = 0.15f; p.trimStart = 0.3; p.voices = 3; p.tune = true; p.root = 9; p.scale = 2; h.setParams(p); juce::String err; h.renderNow(err);
}
static void testPage(const juce::File& shots) {
    std::cout << "-- page" << std::endl;
    const ghosty::themes::Theme looks[3] = { ghosty::themes::Theme::Dark, ghosty::themes::Theme::Purple, ghosty::themes::Theme::Light }; const char* names[3] = { "black", "purple", "light" };
    for (int l = 0; l < 3; ++l) {
        fusion::FusionHost h; prepPage(h); GhostVoicePage page(h); page.setLook(looks[l]); page.setSize(1200, 750); page.refreshNow();
        auto img = page.createComponentSnapshot(page.getLocalBounds(), true, 1.0f); bool nonBlank = false; const auto c0 = img.getPixelAt(5, 5);
        for (int y = 60; y < 700 && !nonBlank; y += 7) for (int x = 300; x < 1100; x += 7) if (img.getPixelAt(x, y) != c0) { nonBlank = true; break; }
        check(img.isValid() && nonBlank && page.look() == looks[l], juce::String("page paints: ") + names[l]);
        if (shots != juce::File()) { shots.createDirectory(); auto f = shots.getChildFile(juce::String("ghostvoice_") + names[l] + ".png"); f.deleteFile(); juce::FileOutputStream os(f); juce::PNGImageFormat png; png.writeImageToStream(img, os); }
    }
    { fusion::FusionHost h; h.autoRender = false; GhostVoicePage page(h); page.setSize(1200, 750); auto img = page.createComponentSnapshot(page.getLocalBounds(), true, 1.0f); check(img.isValid(), "empty page paints");
      if (shots != juce::File()) { auto f = shots.getChildFile("ghostvoice_empty.png"); f.deleteFile(); juce::FileOutputStream os(f); juce::PNGImageFormat png; png.writeImageToStream(img, os); } }
    { fusion::FusionHost h; prepPage(h); GhostVoicePage page(h); page.setSize(1200, 750); page.refreshNow();
      page.setKnob("FORMANT", 3.0); check(std::abs(h.params().formant - 3.0f) < 1e-5f && page.knobText("FORMANT") == "+3.0 st", "a knob turns the setting", page.knobText("FORMANT"));
      page.setKnob("GENDER / AGE", -0.5); check(std::abs(h.params().genderAge + 0.5f) < 1e-5f && page.knobText("GENDER / AGE").startsWith("bigger"), "GENDER / AGE reads in words", page.knobText("GENDER / AGE"));
      check(h.canUndo() && h.undo() && std::abs(h.params().genderAge) < 1e-6f, "undo after a knob");
      // every tooltip present, no banned words anywhere in the UI
      int noTip = 0; bool banned = false; std::function<void(juce::Component&)> walk = [&](juce::Component& c) { if (auto* t = dynamic_cast<juce::SettableTooltipClient*>(&c)) { if (t->getTooltip().isEmpty() && (dynamic_cast<juce::Button*>(&c) || dynamic_cast<juce::Slider*>(&c) || dynamic_cast<juce::ComboBox*>(&c))) ++noTip; if (t->getTooltip().containsIgnoreCase("brain") || t->getTooltip().contains("AI ")) banned = true; }
          if (auto* b = dynamic_cast<juce::TextButton*>(&c)) if (b->getButtonText().containsIgnoreCase("brain")) banned = true; for (auto* ch : c.getChildren()) walk(*ch); }; walk(page);
      check(noTip == 0 && !banned, "every control has a tooltip; plain words only", juce::String(noTip) + " without");
      {   // the sound switch: NATURAL / CLASSIC buttons drive the engine (and nothing user-visible says "CLEAR sound")
          juce::TextButton* nat = nullptr; juce::TextButton* cls = nullptr; bool clearWord = false;
          std::function<void(juce::Component&)> find = [&](juce::Component& c) { if (auto* b = dynamic_cast<juce::TextButton*>(&c)) { if (b->getTooltip().startsWith("NATURAL sound")) nat = b; if (b->getTooltip().startsWith("CLASSIC sound")) cls = b; if (b->getTooltip().contains("CLEAR sound")) clearWord = true; }
              for (auto* ch : c.getChildren()) find(*ch); }; find(page);
          bool ok = nat && cls && nat->getButtonText() == "NATURAL" && cls->getButtonText() == "CLASSIC" && !clearWord;
          if (ok) { cls->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(30); const int e0 = h.params().engine; nat->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(30); ok = e0 == 0 && h.params().engine == 1; }
          check(ok, "NATURAL / CLASSIC buttons switch the sound (labels in plain words)"); }
      {   // the tuning style is SMOOTH / HARD (tuneMode 0 / 1, saved as before); exactly one button on the page says NATURAL (the sound)
          int naturals = 0; juce::TextButton* smooth = nullptr; juce::TextButton* hard = nullptr;
          std::function<void(juce::Component&)> find = [&](juce::Component& c) { if (auto* b = dynamic_cast<juce::TextButton*>(&c)) { if (b->getButtonText() == "NATURAL") ++naturals; if (b->getButtonText() == "SMOOTH") smooth = b; if (b->getButtonText() == "HARD") hard = b; }
              for (auto* ch : c.getChildren()) find(*ch); }; find(page);
          bool ok = naturals == 1 && smooth && hard && smooth->getTooltip().startsWith("SMOOTH:");
          if (ok) { hard->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(30); const int m1 = h.params().tuneMode; smooth->triggerClick(); juce::MessageManager::getInstance()->runDispatchLoopUntil(30); ok = m1 == 1 && h.params().tuneMode == 0; }
          fusion::Params q; q.tuneMode = 0; check(ok && fusion::Params::fromTree(q.toTree()).tuneMode == 0 && (int) q.toTree().getProperty("tuneMode") == 0, "PITCH: SMOOTH / HARD switch the tuning style (saved as tuneMode 0 / 1, as before); one NATURAL button on the page"); }
      // the export gate: unwired = refused; wired = a file
      const auto refused = page.exportForTest(0); check(refused.isEmpty() && page.lastMessage().contains("full version"), "DRAG WAV OUT with no gate wired: refused");
      page.allowExport = [](juce::Component*, const char*) { return true; }; const auto files = page.exportForTest(0); check(files.size() == 1 && juce::File(files[0]).existsAsFile(), "DRAG WAV OUT with the gate open: a WAV"); for (auto& f : files) juce::File(f).deleteFile();
      page.pickFactoryVoice(1, 0); check(h.slot(1) && h.slot(1)->presetName == "Glass Angel", "PICK A VOICE puts a designed voice in B");
      check(page.saveVoiceForTest("Page test voice"), "SAVE VOICE from the page"); fusion::FusionHost::voicesDir().getChildFile("Page test voice.ghostvoice").deleteFile(); }
}

// ---------------------------------------------------------------------------------------------------------------- demos
static void demos(const juce::File& dir) {
    const int fs = 48000; const auto xa = voiceA(fs), xb = voiceB(fs), xc = voiceC(fs); const auto aa = analysed(xa, fs); const auto idB = fusion::extractIdentity(analysed(xb, fs)), idC = fusion::extractIdentity(analysed(xc, fs));
    writeWav(dir.getChildFile("01_A_only_my_take.wav"), xa, fs); writeWav(dir.getChildFile("02_B_only_the_other_singer.wav"), xb, fs);
    { fusion::Params p; p.wB = 1.0f; Rendered y; fusion::render(aa, refOf(xa, fs), &idB, nullptr, p, y); writeWav(dir.getChildFile("03_B_sings_my_take_100pct.wav"), y); }
    for (int w : { 75, 50, 25 }) { fusion::Params p; p.wB = (100 - w) / 100.0f; Rendered y; fusion::render(aa, refOf(xa, fs), &idB, nullptr, p, y); writeWav(dir.getChildFile("04_blend_" + juce::String(w) + "me_" + juce::String(100 - w) + "B.wav"), y); }
    { fusion::Params p; p.wB = 0.0f; Rendered y; fusion::render(aa, refOf(xa, fs), &idB, nullptr, p, y); writeWav(dir.getChildFile("05_resynth_check_100me.wav"), y); }
    const auto& F = fusion::factoryIdentities(); const auto& D = fusion::factoryDesigns();
    for (int i : { 0, 1, 2, 6 }) { fusion::Params p; p.wB = 0.6f; Rendered y; fusion::render(aa, refOf(xa, fs), &F[(size_t) i], nullptr, p, y); writeWav(dir.getChildFile("06_preset_" + juce::String(D[(size_t) i].name).replaceCharacter(' ', '_') + "_60pct.wav"), y); }
    { fusion::Params p; p.wB = 0.35f; p.wC = 0.35f; Rendered y; fusion::render(aa, refOf(xa, fs), &idB, &idC, p, y); writeWav(dir.getChildFile("07_three_way_30me_35B_35C.wav"), y); }
    { fusion::Params p; p.wB = 0.5f; p.voices = 4; p.octDown = 0.4f; Rendered y; fusion::render(aa, refOf(xa, fs), &F[8], nullptr, p, y); writeWav(dir.getChildFile("08_ghost_choir_stack4_octdown.wav"), y); }
    { fusion::Params p; p.wB = 0.5f; p.tune = true; p.tuneMode = 1; p.root = 9; p.scale = 2; p.octave = -1; Rendered y; fusion::render(aa, refOf(xa, fs), &idB, nullptr, p, y); writeWav(dir.getChildFile("09_hard_tune_Aminor_octave_down.wav"), y); }
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui; const juce::String cmd = argc > 1 ? argv[1] : "";
    if (cmd == "shots" && argc > 2) { testPage(juce::File(argv[2])); std::cout << passes << " passed, " << fails << " failed" << std::endl; return fails; }
    if (cmd == "demos" && argc > 2) { demos(juce::File(argv[2])); return 0; }
    if (cmd == "natural" || cmd == "clear") { testClear(48000); testClear(44100); std::cout << passes << " passed, " << fails << " failed" << std::endl; return fails; }
    std::cout << "GHOST VOICE tests" << std::endl;
    testGridAndMorph(); testIdentity(48000); testBlend(48000); testDesignAndPitch(48000); testClear(48000);
    if (cmd != "quick") { testBlend(44100); testClear(44100); testHost(); }
    testPage({});
    std::cout << passes << " passed, " << fails << " failed" << std::endl; return fails;
}
