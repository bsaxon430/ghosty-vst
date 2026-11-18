// GHOST VOICE NATURAL engine: the render (see VoiceFusionClear.h for the design). Offline, any thread.
#include "VoiceFusionClear.h"
#include <cmath>
#include <algorithm>
#include <memory>

namespace fusion {
namespace {
const double kPi = juce::MathConstants<double>::pi;
constexpr double kDbToLn = 2.302585092994046 / 10.0;
inline double clampLn(double l) { return std::isfinite(l) ? std::max(-46.0, l) : -46.0; }
// zero-phase one-pole smoothing of frames x kGrid rows over time (tau in frames)
void smoothRows(std::vector<float>& v, int F, double tauFrames) {
    if (F < 2) return; const float al = (float) (1.0 - std::exp(-1.0 / std::max(0.5, tauFrames)));
    for (int k = 0; k < kGrid; ++k) {
        float s = v[(size_t) k]; for (int j = 0; j < F; ++j) { float& x = v[(size_t) j * kGrid + k]; s += al * (x - s); x = s; }
        s = v[(size_t) (F - 1) * kGrid + k]; for (int j = F - 1; j >= 0; --j) { float& x = v[(size_t) j * kGrid + k]; s += al * (x - s); x = s; }
    }
}
} // namespace

bool renderNatural(const voice::Analysis& a, const voice::AudioRef& aAudio, const Identity* b0, const Identity* c0, const Params& p,
                   voice::Rendered& out, voice::Progress prog, Info* info) {
    const Identity* b = (b0 && !b0->empty()) ? b0 : nullptr; const Identity* c = (c0 && !c0->empty()) ? c0 : nullptr;
    const double wB = b ? p.wB : 0.0, wC = c ? p.wC : 0.0;
    const int fs = a.fs, bins = a.bins(), Fa = a.frames(); const double fp = a.framePeriod;
    const auto idA = extractIdentity(a, p.voicing);
    if (!aAudio.valid() || aAudio.fs != fs || idA.empty()) {   // nothing of yours to reshape (no audio / no vowels): CLASSIC renders it
        Params q = p; q.engine = 0; return render(a, aAudio, b0, c0, q, out, prog, info);
    }
    // ---- the frame range (START / END), the pitch track, the consonant ramps: exactly as CLASSIC
    int i0 = 0, i1 = Fa;
    if (p.trimStart > 0 || (p.trimEnd > 0 && p.trimEnd < a.seconds())) {
        const double tA = juce::jlimit(0.0, a.seconds(), std::max(0.0, p.trimStart)), tB = p.trimEnd > 0 ? juce::jlimit(tA, a.seconds(), p.trimEnd) : a.seconds();
        i0 = juce::jlimit(0, std::max(0, Fa - 2), (int) std::floor(tA * 1000.0 / fp)); i1 = juce::jlimit(i0 + 2, Fa, (int) std::ceil(tB * 1000.0 / fp) + 1);
    }
    const int F = i1 - i0; const double t0 = i0 * fp / 1000.0;
    const auto ef = voice::gateF0(a.f0Raw, a.periodicity, p.voicing);
    std::vector<double> f0((size_t) F), fA((size_t) F);
    { std::vector<double> midi((size_t) F); for (int j = 0; j < F; ++j) { fA[(size_t) j] = ef[(size_t) (i0 + j)]; midi[(size_t) j] = fA[(size_t) j] > 0 ? voice::hzMidi(fA[(size_t) j]) : std::numeric_limits<double>::quiet_NaN(); }
      if (p.tune) { const bool hard = p.tuneMode == 1; midi = voice::tunePitch(midi, fp, p.root, p.scale, hard ? 0.0f : p.retuneMs, hard ? 1.0f : (p.keepVibrato ? 0.0f : 0.6f), hard ? 0.0f : p.humanize); }
      const double semis = p.octave * 12.0 + p.semis + designPitchSemis(p);
      for (int j = 0; j < F; ++j) f0[(size_t) j] = std::isfinite(midi[(size_t) j]) ? juce::jlimit(20.0, fs / 4.0, voice::midiHz(midi[(size_t) j] + semis)) : 0.0; }
    std::vector<float> mixScale((size_t) F, 1.0f);
    if (p.keepConsonants) { std::vector<float> raw((size_t) F); for (int j = 0; j < F; ++j) raw[(size_t) j] = fA[(size_t) j] > 0 ? 1.0f : 0.0f;
        for (int j = 0; j < F; ++j) { float s = 0, w = 0; for (int d = -3; d <= 3; ++d) { const int k = juce::jlimit(0, F - 1, j + d); const float ww = 4.0f - std::abs((float) d); s += ww * raw[(size_t) k]; w += ww; } mixScale[(size_t) j] = s / w; } }
    // ---- the voices' sizes and their vowel prototypes per frame (yours, and B's / C's for the same vowel in their own size)
    const double tB = b ? clear::tractShift(idA, *b) : 0.0, tC = c ? clear::tractShift(idA, *c) : 0.0;
    const auto meanA = clear::meanEnvelope(idA); std::array<double, kGrid> meanB {}, meanC {}; if (b) meanB = clear::meanEnvelope(*b); if (c) meanC = clear::meanEnvelope(*c);
    auto meanAp = [](const Identity& id) { std::array<float, kApBands> m {}; double ws = 0; for (auto& fr : id.frames) { for (int k = 0; k < kApBands; ++k) m[(size_t) k] += fr.weight * fr.ap[(size_t) k]; ws += fr.weight; } if (ws > 0) for (auto& v : m) v = (float) (v / ws); return m; };
    std::array<float, kApBands> apMeanB {}, apMeanC {}; if (b) apMeanB = meanAp(*b); if (c) apMeanC = meanAp(*c);
    std::vector<float> gAs((size_t) F * kGrid), vA((size_t) F * kGrid), vB, vC; std::vector<double> levels((size_t) F); std::vector<voice::Formants> fmA((size_t) F);
    std::vector<std::array<float, kApBands>> apA((size_t) F), apB((size_t) F), apC((size_t) F);
    std::vector<voice::Formants> fvB((size_t) F), fvC((size_t) F);   // B's / C's prototype formants (their own size)
    if (b) vB.assign((size_t) F * kGrid, 0.0f); if (c) vC.assign((size_t) F * kGrid, 0.0f);
    std::vector<double> row((size_t) bins), P((size_t) bins);
    float rmsMax = 0; for (int j = 0; j < F; ++j) rmsMax = std::max(rmsMax, a.rms[(size_t) (i0 + j)]);
    for (int j = 0; j < F; ++j) {
        if ((j & 63) == 0) { if (prog.stop()) return false; prog.set((int) (25.0 * j / F)); }
        const int i = i0 + j; a.specRow(i, row.data()); std::array<double, kGrid> gA {};
        levels[(size_t) j] = toGrid(row.data(), bins, fs, gA.data()); for (int k = 0; k < kGrid; ++k) gAs[(size_t) j * kGrid + k] = (float) gA[(size_t) k];
        const bool voiced = fA[(size_t) j] > 0;
        voice::Formants Fa_ = voiced ? formantsLpc(row.data(), bins, fs, fA[(size_t) j]) : a.formants[(size_t) i]; if (!(Fa_[0].hz > 0)) Fa_ = a.formants[(size_t) i]; fmA[(size_t) j] = Fa_;
        a.apRow(i, P.data()); voice::calibrateAperiodicity(P.data(), bins, fs, a.periodicity[(size_t) i]); apA[(size_t) j] = detail::apBandsOf(P.data(), bins, fs);
        const bool haveF = Fa_[0].hz > 0 && Fa_[1].hz > 0;
        { const View v = (voiced && haveF) ? viewFor(idA, Fa_, gA.data()) : View(); for (int k = 0; k < kGrid; ++k) vA[(size_t) j * kGrid + k] = (float) (v.valid ? v.logEnv[(size_t) k] : meanA[(size_t) k]); }
        auto other = [&](const Identity& id, double t, const std::array<double, kGrid>& mean, const std::array<float, kApBands>& apMean, std::vector<float>& dst, std::array<float, kApBands>& apDst, voice::Formants& fDst) {
            View v; if (voiced && haveF) { voice::Formants Fs = Fa_; for (auto& f : Fs) if (f.hz > 0) f.hz = (float) (f.hz * std::exp(t)); std::array<double, kGrid> gs {}; clear::shiftGrid(gA.data(), t, gs.data()); v = viewFor(id, Fs, gs.data()); }
            for (int k = 0; k < kGrid; ++k) dst[(size_t) j * kGrid + k] = (float) (v.valid ? v.logEnv[(size_t) k] : mean[(size_t) k]); apDst = v.valid ? v.ap : apMean; fDst = v.valid ? v.F : voice::Formants {}; };
        if (b) other(*b, tB, meanB, apMeanB, vB, apB[(size_t) j], fvB[(size_t) j]); if (c) other(*c, tC, meanC, apMeanC, vC, apC[(size_t) j], fvC[(size_t) j]);
    }
    // smoothed over ~25 ms both ways: a vowel prototype glides, it never jumps (the same smoothing for yours and theirs, so 0 % = you exactly)
    const double tau = 25.0 / fp; smoothRows(vA, F, tau); if (b) smoothRows(vB, F, tau); if (c) smoothRows(vC, F, tau);
    auto smoothF = [&](std::vector<voice::Formants>& fv) {   // the prototype formants glide the same way (log Hz; gaps keep the last value)
        const double al = 1.0 - std::exp(-1.0 / std::max(0.5, tau));
        for (int q = 0; q < voice::kFormants; ++q) { double sm = 0; bool on = false;
            for (int j = 0; j < F; ++j) { auto& f = fv[(size_t) j][(size_t) q]; if (f.hz > 0) { const double l = std::log(f.hz); sm = on ? sm + al * (l - sm) : l; on = true; f.hz = (float) std::exp(sm); } }
            on = false; for (int j = F - 1; j >= 0; --j) { auto& f = fv[(size_t) j][(size_t) q]; if (f.hz > 0) { const double l = std::log(f.hz); sm = on ? sm + al * (l - sm) : l; on = true; f.hz = (float) std::exp(sm); } } } };
    if (b) smoothF(fvB); if (c) smoothF(fvC);
    // ---- the target envelope per frame
    std::vector<float> envs((size_t) F * kGrid); std::vector<float> wId((size_t) F, 0.0f), noise((size_t) F * kApBands, 0.0f); std::vector<double> uOf((size_t) F, 0.0);
    std::array<double, kGrid> dsg {}; detail::designCurveOf(p, dsg.data());
    bool anyNoise = false;
    for (int j = 0; j < F; ++j) {
        const double m = mixScale[(size_t) j], wb = wB * m, wc = wC * m, u = wb * tB + wc * tC; uOf[(size_t) j] = u; wId[(size_t) j] = (float) (wb + wc);
        const float* ga = gAs.data() + (size_t) j * kGrid; const float* va = vA.data() + (size_t) j * kGrid;
        // two (or three) envelopes, each at the blended tract size: yours, and B's vowel prototype carrying your detail around your own
        // prototype (consonants, transitions); then warped so their formants meet at the blend (one set of peaks, never two singers
        // at once) and blended; the depth of the colour is kept as in SLERP (the length of the blended shape is interpolated)
        std::array<std::array<double, kGrid>, 3> L {}; std::array<voice::Formants, 3> Fs {}; double w[3] = { 1.0 - wb - wc, 0, 0 }; int n = 1;
        std::array<double, kGrid> gaa {}, res {}, rs {}, o {}; for (int k = 0; k < kGrid; ++k) { gaa[(size_t) k] = ga[k]; res[(size_t) k] = (double) ga[k] - va[k]; }
        clear::shiftGrid(gaa.data(), u, L[0].data()); clear::shiftGrid(res.data(), u, rs.data());
        Fs[0] = fmA[(size_t) j]; for (auto& f : Fs[0]) if (f.hz > 0) f.hz = (float) (f.hz * std::exp(u));
        auto add = [&](const std::vector<float>& v, const voice::Formants& fv, double t, double wt) {
            std::array<double, kGrid> pr {}, ps {}; for (int k = 0; k < kGrid; ++k) pr[(size_t) k] = v[(size_t) j * kGrid + k];
            clear::shiftGrid(pr.data(), u - t, ps.data()); for (int k = 0; k < kGrid; ++k) L[(size_t) n][(size_t) k] = ps[(size_t) k] + rs[(size_t) k];
            Fs[(size_t) n] = fv; for (auto& f : Fs[(size_t) n]) if (f.hz > 0) f.hz = (float) (f.hz * std::exp(u - t)); w[n] = wt; ++n; };
        if (b && wb > 1e-6) add(vB, fvB[(size_t) j], tB, wb); if (c && wc > 1e-6) add(vC, fvC[(size_t) j], tC, wc);
        if (n == 1) o = L[0];
        else {
            morphGrid(L.data(), Fs.data(), w, n, o.data());
            auto devLen = [&](const std::array<double, kGrid>& g) { const double mu = clear::bandMean(g.data()); double s2 = 0; for (int k = clear::bandLo(); k <= clear::bandHi(); ++k) s2 += (g[(size_t) k] - mu) * (g[(size_t) k] - mu); return std::sqrt(s2); };
            double want = 0; for (int q = 0; q < n; ++q) want += w[q] * devLen(L[(size_t) q]); const double have = devLen(o);
            if (have > 1e-6) { const double sc = juce::jlimit(1.0, 1.5, want / have), mu = clear::bandMean(o.data()); for (auto& x : o) x = mu + (x - mu) * sc; }
        }
        { double lv = 0; for (int k = clear::bandLo(); k <= clear::bandHi(); ++k) lv += o[(size_t) k] - ga[k]; lv /= (clear::bandHi() - clear::bandLo() + 1); for (auto& x : o) x -= lv; }   // same mean as yours
        if (p.keepConsonants && wb + wc > 1e-6 && p.clarity > 0) for (int k = 0; k < kGrid; ++k) { const double f = gridHz(k); if (f <= 5000) continue; const double r = std::min(1.0, (f - 5000.0) / 3000.0) * p.clarity; o[(size_t) k] += (ga[k] - o[(size_t) k]) * r; }
        if (std::abs(p.resonance) > 1e-3) {   // RESONANCE as CLASSIC: peaks vs valleys around a 25-point trend
            std::array<double, kGrid> pre {}, trend {}; double run = 0; for (int k = 0; k < kGrid; ++k) { run += o[(size_t) k]; pre[(size_t) k] = run; }
            for (int k = 0; k < kGrid; ++k) { const int lo = std::max(0, k - 12), hi = std::min(kGrid - 1, k + 12); trend[(size_t) k] = (pre[(size_t) hi] - (lo > 0 ? pre[(size_t) lo - 1] : 0.0)) / (hi - lo + 1); }
            const double gain = p.resonance > 0 ? 1.0 + 0.6 * p.resonance : 1.0 + 0.8 * p.resonance; for (int k = 0; k < kGrid; ++k) o[(size_t) k] = trend[(size_t) k] + (o[(size_t) k] - trend[(size_t) k]) * gain;
        }
        for (int k = 0; k < kGrid; ++k) envs[(size_t) j * kGrid + k] = (float) clampLn(o[(size_t) k] + dsg[(size_t) k]);
        // breath: B's / C's breathiness above yours, BREATH, and GRIT's rasp (1.5 .. 5 kHz)
        for (int q = 0; q < kApBands; ++q) {
            const double aq = apA[(size_t) j][(size_t) q]; double n = 0;
            if (b) n += wb * std::max(0.0, (double) apB[(size_t) j][(size_t) q] - aq); if (c) n += wc * std::max(0.0, (double) apC[(size_t) j][(size_t) q] - aq);
            if (p.breath > 0) n += p.breath * (0.25 + 0.75 * q / (kApBands - 1.0)) * (1.0 - aq);
            if (p.grit > 0 && bandHz(q) > 1500 && bandHz(q) < 5000) n += 0.25 * p.grit * (1.0 - aq);
            noise[(size_t) j * kApBands + q] = (float) juce::jlimit(0.0, 1.0, n); anyNoise = anyNoise || n > 1e-4;
        }
    }
    // ---- the filter: target / your own envelope, frame energy kept, boosts capped by what your recording has
    clear::Shaping sh; sh.frames = F; sh.fp = fp; sh.gain.assign((size_t) F * kGrid, 0.0f); sh.deficit.assign((size_t) F * kGrid, 0.0f); if (anyNoise) sh.noise = noise;
    std::vector<double> S((size_t) bins), logS((size_t) bins), gridRow((size_t) kGrid), gc((size_t) bins), dd((size_t) bins);
    const double warp = designWarp(p), binHz = fs / (2.0 * (bins - 1));
    const int k80 = std::max(1, (int) (80.0 / binHz)), kTop = std::min(bins - 1, (int) (std::min(16000.0, 0.48 * fs) / binHz));
    const double lnCut = std::log(std::pow(10.0, -40.0 / 20.0)), lnVoiced = std::log(std::pow(10.0, 24.0 / 20.0)), lnCons = std::log(std::pow(10.0, 12.0 / 20.0));
    bool anyDeficit = false; if (info) info->formants.resize((size_t) F);
    const int yLen = (int) ((F - 1) * fp / 1000.0 * fs) + 1;
    const bool mayFill = wB + wC > 1e-6 || std::abs(p.brightness) > 1e-6 || std::abs(p.warmth) > 1e-6 || std::abs(p.weight) > 1e-6 || std::abs(p.resonance) > 1e-6 || std::abs(warp - 1.0) > 1e-6;
    std::unique_ptr<voice::StreamSynth> syn; if (mayFill) syn = std::make_unique<voice::StreamSynth>(fs, fp, a.fftSize, (size_t) yLen);
    for (int j = 0; j < F; ++j) {
        if ((j & 63) == 0) { if (prog.stop()) return false; prog.set(25 + (int) (25.0 * j / F)); }
        const int i = i0 + j; for (int k = 0; k < kGrid; ++k) gridRow[(size_t) k] = envs[(size_t) j * kGrid + k];
        fromGrid(gridRow.data(), bins, fs, logS.data());
        for (int k = 0; k < bins; ++k) S[(size_t) k] = std::exp(clampLn(logS[(size_t) k] + levels[(size_t) j]));
        if (std::abs(warp - 1.0) > 1e-6) voice::warpEnvelope(S.data(), bins, warp, S.data());
        a.specRow(i, row.data());
        double eA = 0, eT = 0, pkA = 0; for (int k = k80; k <= kTop; ++k) { eA += row[(size_t) k]; eT += S[(size_t) k]; pkA = std::max(pkA, row[(size_t) k]); }
        const double norm = (eA > 0 && eT > 0) ? 0.5 * std::log(eA / eT) : 0.0;
        const bool voiced = fA[(size_t) j] > 0, silent = a.rms[(size_t) i] < rmsMax * 1e-3f;
        const double cap = silent ? 0.0 : voiced ? lnVoiced : lnCons;
        bool def = false;
        for (int k = 0; k < bins; ++k) {
            const double want = 0.5 * (std::log(std::max(1e-30, S[(size_t) k])) - std::log(std::max(1e-30, row[(size_t) k]))) + norm;
            const double room = row[(size_t) k] < pkA * 1e-7 ? 0.0 : cap;
            const double g = juce::jlimit(lnCut, room, want); gc[(size_t) k] = g;
            const double hz = k * binHz; double d = 0;
            if (voiced && !silent && hz > 1500 && want > g + 0.05) d = (1.0 - std::exp(2.0 * (g - want))) * juce::jlimit(0.0, 1.0, (hz - 1500.0) / 1000.0);
            dd[(size_t) k] = std::min(0.98, d); def = def || d > 1e-3;
        }
        for (int q = 0; q < kGrid; ++q) { const double pos = juce::jlimit(0.0, (double) bins - 1, gridHz(q) / binHz); const int k = (int) pos, k1 = std::min(bins - 1, k + 1); const double fr = pos - k;
            sh.gain[(size_t) j * kGrid + q] = (float) (gc[(size_t) k] + (gc[(size_t) k1] - gc[(size_t) k]) * fr); sh.deficit[(size_t) j * kGrid + q] = (float) (dd[(size_t) k] + (dd[(size_t) k1] - dd[(size_t) k]) * fr); }
        anyDeficit = anyDeficit || def;
        if (info) { voice::Formants o = fmA[(size_t) j]; for (auto& f : o) if (f.hz > 0) f.hz = (float) (f.hz * std::exp(uOf[(size_t) j]) * warp); info->formants[(size_t) j] = o; }
        // the fill's harmonics: your pitch, the target envelope, your breath (only heard where the deficit lets it through)
        if (syn) { a.apRow(i, P.data()); voice::calibrateAperiodicity(P.data(), bins, fs, a.periodicity[(size_t) i]);
        syn->push(voiced ? fA[(size_t) j] : 0.0, S.data(), P.data()); }
    }
    { auto sm = [&](std::vector<float>& v) { std::vector<float> o(v.size()); for (int j = 0; j < F; ++j) { const int jm = std::max(0, j - 1), jp = std::min(F - 1, j + 1);
          for (int k = 0; k < kGrid; ++k) o[(size_t) j * kGrid + k] = 0.25f * v[(size_t) jm * kGrid + k] + 0.5f * v[(size_t) j * kGrid + k] + 0.25f * v[(size_t) jp * kGrid + k]; } v.swap(o); };
      sm(sh.gain); sm(sh.deficit); }
    std::vector<float> fill; if (anyDeficit && syn) fill = syn->finish(); else sh.deficit.clear();
    bool cancelled = false;
    const auto y0 = clear::shape(aAudio, (int) std::lround(t0 * fs), yLen, fill, sh, prog, 50, 70, cancelled);
    if (cancelled) return false;
    // ---- the voices: PSOLA copies of the reshaped take (pitch moves, STACK, octave layers, GRIT); no vocoder
    struct VoiceDef { double cents = 0, drift = 0, driftHz = 0, driftPh = 0, delay = 0, gL = 1, gR = 1; };
    const bool stereo = p.stereoOut();
    auto pan = [](double pn, double level, VoiceDef& v) { const double ang = (juce::jlimit(-1.0, 1.0, pn) + 1.0) * kPi / 4.0; v.gL = std::cos(ang) * level; v.gR = std::sin(ang) * level; };
    std::vector<VoiceDef> voices(1); if (stereo) pan(0, 1.0, voices[0]);
    if (p.voices >= 2) { const int extra = p.voices - 1, ranks = (extra + 1) / 2;
        for (int k = 0; k < extra; ++k) { VoiceDef v; const double side = k % 2 == 0 ? -1.0 : 1.0, rank = k / 2 + 1; v.cents = side * p.detuneCents * rank / ranks; v.drift = 0.25 * p.detuneCents; v.driftHz = 0.17 + 0.07 * k; v.driftPh = 1.7 * k;
            v.delay = p.spreadMs * (0.3 + 0.7 * (k + 1) / extra) / 1000.0; pan(side * p.width * rank / ranks, 0.75, v); voices.push_back(v); } }
    if (p.octDown > 0.001f) { VoiceDef v; v.cents = -1200; v.delay = 0.004; pan(0, p.octDown, v); voices.push_back(v); }
    if (p.octUp > 0.001f) { VoiceDef v; v.cents = 1200; v.delay = 0.006; pan(0, 0.8 * p.octUp, v); voices.push_back(v); }
    voice::Rendered seg; seg.fs = fs; seg.L.assign((size_t) yLen, 0.0f); if (stereo) seg.R.assign((size_t) yLen, 0.0f);
    bool pitchMoved = false; for (int j = 0; j < F && !pitchMoved; ++j) pitchMoved = fA[(size_t) j] > 0 && f0[(size_t) j] > 0 && std::abs(f0[(size_t) j] / fA[(size_t) j] - 1.0) > 1e-4;
    for (size_t vi = 0; vi < voices.size(); ++vi) {
        if (prog.stop()) return false; prog.set(70 + (int) (25.0 * vi / voices.size()));
        const auto& V = voices[vi]; std::vector<float> y;
        if (vi == 0 && !pitchMoved && p.grit < 1e-6) y = y0;
        else {
            auto ratio = [&](int j) { const double fa = fA[(size_t) j], fo = f0[(size_t) j]; if (!(fa > 0) || !(fo > 0)) return 1.0;
                const double cents = V.cents + (V.drift > 0 ? V.drift * std::sin(2 * kPi * V.driftHz * j * fp / 1000.0 + V.driftPh) : 0.0); return fo / fa * std::exp2(cents / 1200.0); };
            y = clear::psola(y0, fs, fA, fp, ratio, p.grit, 0x60057u + (unsigned) vi * 977u);
        }
        const int d = (int) std::lround(V.delay * fs);
        for (int n = 0; n + d < yLen; ++n) { if (stereo) { seg.L[(size_t) (n + d)] += (float) (V.gL * y[(size_t) n]); seg.R[(size_t) (n + d)] += (float) (V.gR * y[(size_t) n]); } else seg.L[(size_t) (n + d)] += y[(size_t) n]; }
    }
    out = std::move(seg); clear::finaliseLoudness(out, p.normalise, p.gainDb);
    if (info) { info->f0 = f0; info->mixUsed = wId; info->framePeriod = fp; info->fs = fs; info->t0 = t0; }
    prog.set(100); return true;
}
} // namespace fusion
