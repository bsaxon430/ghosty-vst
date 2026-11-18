// Ghosty Beats: the drum-library expansion (docs/research/DRUM-SOUNDS-STUDY.md section 5): era() / bridgedT() building blocks,
// the classic-character packs and kits, TRAP CLASSIC. Measurements per the study's `character` metrics:
//   centroid: spectral centroid of the first 100 ms (Hann, 4096 FFT, 48 kHz)      t20 / t40: peak -> the 10 ms RMS 20 / 40 dB down
//   f0@60ms: the fundamental at 60 ms (analytic-signal phase)                       floor: RMS of (era output - era bypassed) vs the reference RMS
//   images: energy above the emulated Nyquist vs the total                          bursts: envelope peaks in the first 40 ms (claps)
// Included from BeatsTests.cpp (after its helpers).

namespace ch {
using Buf = std::vector<float>;
constexpr double SR = 48000.0;
inline Buf copyOf(const gb::Sound& s) { return s.data; }
// spectral centroid of [a, a + 100 ms), Hann, zero-padded 4096 FFT
inline double centroid(const Buf& x, double fromS = 0, double lenS = 0.1) {
    const int order = 12, N = 1 << order; juce::dsp::FFT fft(order); std::vector<float> b((size_t) 2 * N, 0.0f);
    const int a = (int) (fromS * SR), n = juce::jmin(N, (int) (lenS * SR));
    for (int k = 0; k < n && a + k < (int) x.size(); ++k) b[(size_t) k] = x[(size_t) (a + k)] * (0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * k / n));
    fft.performFrequencyOnlyForwardTransform(b.data()); double sw = 0, s = 0;
    for (int k = 1; k < N / 2; ++k) { sw += b[(size_t) k] * (k * SR / N); s += b[(size_t) k]; }
    return s > 0 ? sw / s : 0;
}
// 10 ms RMS envelope (1 ms hop), in dB
inline std::vector<double> rmsEnv(const Buf& x) { std::vector<double> e; const int w = 480, hop = 48; for (int a = 0; a + w <= (int) x.size(); a += hop) { double s = 0; for (int i = a; i < a + w; ++i) s += (double) x[(size_t) i] * x[(size_t) i]; e.push_back(10 * std::log10(s / w + 1e-30)); } return e; }
// time from the envelope peak until it first falls `db` below the peak (seconds; the sound's length if it never does)
inline double tDrop(const Buf& x, double db) { const auto e = rmsEnv(x); if (e.empty()) return 0; size_t pk = 0; for (size_t i = 1; i < e.size(); ++i) if (e[i] > e[pk]) pk = i;
    for (size_t i = pk; i < e.size(); ++i) if (e[i] < e[pk] - db) return (double) (i - pk) * 0.001; return (double) x.size() / SR; }
// the analytic signal's instantaneous frequency, averaged over [t0, t1) (a band-limited single partial: the resonator alone)
inline double instFreq(const Buf& x, double t0, double t1) {
    int order = 10; while ((1 << order) < (int) x.size() * 2 && order < 20) ++order; const int N = 1 << order;
    juce::dsp::FFT fft(order); std::vector<juce::dsp::Complex<float>> in((size_t) N), out((size_t) N);
    for (int i = 0; i < (int) x.size(); ++i) in[(size_t) i] = { x[(size_t) i], 0.0f };
    fft.perform(in.data(), out.data(), false);
    for (int k = 1; k < N / 2; ++k) out[(size_t) k] *= 2.0f; for (int k = N / 2 + 1; k < N; ++k) out[(size_t) k] = {}; out[0] *= 1.0f;
    fft.perform(out.data(), in.data(), true);
    const int a = juce::jmax(1, (int) (t0 * SR)), b = juce::jmin((int) x.size() - 1, (int) (t1 * SR)); double acc = 0; int c = 0;
    for (int i = a; i < b; ++i) { double d = std::arg(in[(size_t) i]) - std::arg(in[(size_t) i - 1]); while (d < -juce::MathConstants<double>::pi) d += juce::MathConstants<double>::twoPi; while (d > juce::MathConstants<double>::pi) d -= juce::MathConstants<double>::twoPi; acc += d; ++c; }
    return c ? acc / c * SR / juce::MathConstants<double>::twoPi : 0;
}
// energy fraction (dB) of [lo, hi) Hz in the whole buffer
inline double bandDb(const Buf& x, double lo, double hi) {
    int order = 10; while ((1 << order) < (int) x.size() && order < 20) ++order; const int N = 1 << order;
    juce::dsp::FFT fft(order); std::vector<float> b((size_t) 2 * N, 0.0f); std::copy(x.begin(), x.end(), b.begin()); fft.performFrequencyOnlyForwardTransform(b.data());
    double in = 0, all = 0; for (int k = 1; k < N / 2; ++k) { const double e = (double) b[(size_t) k] * b[(size_t) k], f = k * SR / N; all += e; if (f >= lo && f < hi) in += e; }
    return 10 * std::log10(in / (all + 1e-30) + 1e-30);
}
// the two strongest spectral peaks in [lo, hi) Hz (16k FFT, the first 0.34 s), at least `gap` Hz apart
inline std::pair<double, double> twoPeaks(const Buf& x, double lo, double hi, double gap = 60) {
    const int order = 14, N = 1 << order; juce::dsp::FFT fft(order); std::vector<float> b((size_t) 2 * N, 0.0f);
    for (int k = 0; k < N && k < (int) x.size(); ++k) b[(size_t) k] = x[(size_t) k] * (0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * k / N));
    fft.performFrequencyOnlyForwardTransform(b.data()); int p1 = -1, p2 = -1;
    for (int k = (int) (lo * N / SR); k < (int) (hi * N / SR); ++k) if (p1 < 0 || b[(size_t) k] > b[(size_t) p1]) p1 = k;
    for (int k = (int) (lo * N / SR); k < (int) (hi * N / SR); ++k) if (std::abs(k - p1) * SR / N > gap && (p2 < 0 || b[(size_t) k] > b[(size_t) p2])) p2 = k;
    double a = p1 * SR / N, c = p2 * SR / N; if (a > c) std::swap(a, c); return { a, c };
}
// RMS of (y - ref) vs the RMS of ref over [a, a + n) samples (whole buffer if n <= 0), dB
inline double floorDb(const Buf& y, const Buf& ref, int a = 0, int n = 0) { const int m = (int) juce::jmin(y.size(), ref.size()); const int e = n > 0 ? juce::jmin(m, a + n) : m; double r = 0, s = 0;
    for (int i = a; i < e; ++i) { const double d = (double) y[(size_t) i] - ref[(size_t) i]; r += d * d; s += (double) ref[(size_t) i] * ref[(size_t) i]; } return 10 * std::log10(r / (s + 1e-30) + 1e-30); }
// clap bursts: peaks of the 1.5 ms RMS envelope in the first 40 ms, >= 6 ms apart, above half the loudest, each rising >= 4 dB out of
// the dip before it; spacing = the mean distance between them
inline int bursts(const Buf& x, double* spacingMs = nullptr) {
    const int n = juce::jmin((int) x.size(), (int) (0.045 * SR)), h = 36; std::vector<double> e((size_t) n, 0.0);
    for (int i = 0; i < n; ++i) { double s = 0; int c = 0; for (int k = juce::jmax(0, i - h); k <= juce::jmin(n - 1, i + h); ++k) { s += (double) x[(size_t) k] * x[(size_t) k]; ++c; } e[(size_t) i] = std::sqrt(s / c); }
    double mx = 0; for (auto v : e) mx = std::max(mx, v);
    std::vector<int> pk; double mn = 0;   // (silence before the first burst)
    for (int i = 1; i + 1 < n && i < (int) (0.04 * SR); ++i) { mn = std::min(mn, e[(size_t) i]);
        if (e[(size_t) i] >= e[(size_t) i - 1] && e[(size_t) i] > e[(size_t) i + 1] && e[(size_t) i] > 0.5 * mx && e[(size_t) i] > mn * 1.585 && (pk.empty() || i - pk.back() > (int) (0.006 * SR))) { pk.push_back(i); mn = e[(size_t) i]; } }
    if (spacingMs) { *spacingMs = pk.size() > 1 ? (pk.back() - pk.front()) / (double) (pk.size() - 1) / SR * 1000.0 : 0; }
    return (int) pk.size();
}
inline uint64_t hashBuf(const Buf& b) { uint64_t h = 1469598103934665603ULL; for (auto v : b) { uint32_t u; std::memcpy(&u, &v, 4); h = (h ^ u) * 1099511628211ULL; } return h; }
} // namespace ch

// ---------------------------------------------------------------- 1. era() + bridgedT(): the building blocks
static void testEraBridged() {
    section("DRUM LIBRARY 1: era() (rate / bits / mu-law / re-pitch / quantise-before-envelope / clip / output filter) + bridgedT() resonator");
    using namespace gb::synth; const double sr = ch::SR;
    auto sine = [&](double hz, double amp, double secs) { Buf b((size_t) (secs * sr)); for (size_t i = 0; i < b.size(); ++i) b[i] = (float) (amp * std::sin(TAU * hz * i / sr)); return b; };
    auto run = [&](Buf b, const EraP& p, const Buf* env = nullptr) { era(b, sr, p, env); return b; };
    // ---- the quantiser: floors (residual vs signal RMS) against the ideal 6.02 N + 1.76 dB
    const Buf s1k = sine(1000, 0.999, 0.5);
    EraP q12; q12.bits = 12; EraP q8; q8.bits = 8; EraP m8 = q8; m8.law = 1;
    const double f12 = ch::floorDb(run(s1k, q12), s1k), f8 = ch::floorDb(run(s1k, q8), s1k);
    const Buf s30 = sine(1000, juce::Decibels::decibelsToGain(-30.0), 0.5);
    const double snrMu = -ch::floorDb(run(s30, m8), s30), snrLin = -ch::floorDb(run(s30, q8), s30);
    std::printf("  quantiser floor (1 kHz full-scale sine): 12-bit %.1f dB (ideal -74.0), 8-bit %.1f dB (ideal -49.9) | -30 dBFS sine at 8 bits: mu-law SNR %.1f dB, linear SNR %.1f dB\n", f12, f8, snrMu, snrLin);
    CHECK(std::abs(f12 + 74) <= 3 && std::abs(f8 + 50) <= 3, "quantiser floors 12-bit %.1f, 8-bit %.1f", f12, f8);
    CHECK(snrMu >= 30 && snrLin < 22, "mu-law keeps the SNR on a quiet signal (%.1f vs linear %.1f dB)", snrMu, snrLin);
    // ---- the rate stage: images without a reconstruction filter (SP-style), none with the MPC-style filters
    Buf nz((size_t) (0.5 * sr)); { Rng r(77); for (auto& v : nz) v = r.u() * 0.5f; }
    Buf band = nz; brickwall(band, sr, 12000);
    EraP sp; sp.rate = 26040; sp.bits = 12;
    const double img = ch::bandDb(run(band, sp), 13020, 24000), imgSrc = ch::bandDb(band, 13020, 24000);
    EraP sp2 = sp; sp2.postLP = 12000; sp2.postType = 1; const double imgF = ch::bandDb(run(band, sp2), 13020, 24000);
    EraP mpc; mpc.rate = 40000; mpc.bits = 12; mpc.preLP = 17600; mpc.preType = 1; mpc.postLP = 17600; mpc.postType = 1;
    const double hi = ch::bandDb(run(nz, mpc), 18500, 24000), hiSrc = ch::bandDb(nz, 18500, 24000);
    std::printf("  images: 26.04 kHz, no output filter %.1f dB above 13.02 kHz (source %.1f, with a brickwall %.1f) | 40 kHz + 18 kHz filters on white noise: %.1f dB above 18.5 kHz (source %.1f)\n", img, imgSrc, imgF, hi, hiSrc);
    CHECK(img >= -30 && imgSrc < -60 && imgF < -50, "26.04 kHz without a reconstruction filter keeps its images (%.1f dB)", img);
    CHECK(hi <= -45, "40 kHz / 18 kHz bandwidth: %.1f dB above 18.5 kHz", hi);
    // ---- re-pitching: drop-sample keeps the clock (images stay at 26.04 kHz), clock mode moves it; both shift the pitch
    {   EraP d; d.rate = 26040; d.semis = -5; EraP c = d; c.pitchMode = 1; const Buf s = sine(1000, 0.9, 0.4);
        const Buf yd = run(s, d), yc = run(s, c); const double fd = ch::instFreq(yd, 0.05, 0.3), fc = ch::instFreq(yc, 0.05, 0.3);
        const double want = 1000 * std::pow(2.0, -5 / 12.0);
        std::printf("  re-pitch -5 semitones: drop-sample %.1f Hz, clock %.1f Hz (want %.1f); length %.3f / %.3f s (source 0.400)\n", fd, fc, want, yd.size() / sr, yc.size() / sr);
        CHECK(std::abs(fd / want - 1) < 0.01 && std::abs(fc / want - 1) < 0.01 && std::abs(yd.size() / sr - 0.4 * std::pow(2.0, 5 / 12.0)) < 0.002, "re-pitch (drop %.1f, clock %.1f Hz)", fd, fc); }
    // ---- quantise BEFORE the envelope: the tail is clean (decay after the DAC)
    {   Buf sus((size_t) (0.6 * sr)), envB(sus.size()); Rng r(5); for (size_t i = 0; i < sus.size(); ++i) { const double t = i / sr; sus[i] = (float) (0.6 * std::sin(TAU * 200 * t) + 0.3 * r.u()); envB[i] = (float) std::exp(-t / 0.05); }
        EraP e; e.rate = 25000; e.bits = 8; EraP e0 = e; e0.bits = 0;
        Buf pre = sus; for (size_t i = 0; i < pre.size(); ++i) pre[i] *= envB[i];
        const Buf ref = run(pre, e0), qbe = run(sus, e, &envB), etq = run(pre, e);
        const int t40 = (int) (ch::tDrop(ref, 40) * sr) + (int) (0.01 * sr);
        auto tailRes = [&](const Buf& y) { double s = 0; int c = 0; for (int i = t40; i < (int) ref.size(); ++i) { const double d = y[(size_t) i] - ref[(size_t) i]; s += d * d; ++c; } return 10 * std::log10(s / juce::jmax(1, c) + 1e-30); };
        const double a = tailRes(qbe), b = tailRes(etq);
        std::printf("  quantise-before-envelope (25 kHz, 8-bit): tail residual %.1f dB vs envelope-then-quantise %.1f dB (%.1f dB cleaner)\n", a, b, b - a);
        CHECK(b - a >= 20, "the tail after the DAC is %.1f dB cleaner", b - a); }
    // ---- converter clip, the swept output filter, determinism
    {   EraP c; c.clip = 1.6; const Buf y = run(s1k, c); float pk = 0; for (auto v : y) pk = std::max(pk, std::abs(v));
        Buf imp((size_t) (0.3 * sr)); { Rng r(9); for (auto& v : imp) v = r.u(); } EraP sw; sw.postLP = 9000; sw.postEnd = 2500; sw.sweepT = 0.08; const Buf ys = run(imp, sw);
        const double c0 = ch::centroid(ys, 0.0, 0.05), c1 = ch::centroid(ys, 0.15, 0.05);
        EraP det; det.rate = 26040; det.bits = 12; det.semis = 5; det.clip = 1.3; det.postLP = 6000; const auto h1 = ch::hashBuf(run(nz, det)), h2 = ch::hashBuf(run(nz, det));
        std::printf("  clip 1.6: peak %.3f (hard at full scale) | swept output filter 9 -> 2.5 kHz: centroid %.0f -> %.0f Hz | hash %016llx (twice the same: %d)\n", pk, c0, c1, (unsigned long long) h1, (int) (h1 == h2));
        CHECK(pk <= 1.0f && pk > 0.99f, "converter clip");
        CHECK(c1 < c0 * 0.6, "swept output filter (%.0f -> %.0f Hz)", c0, c1);
        CHECK(h1 == h2 && h1 == 0x4c0dbf35f36fd96cULL, "era() deterministic (hash %016llx)", (unsigned long long) h1); }

    // ---- bridgedT(): the resonator kick
    auto kickOf = [&](BridgedP p, double secs) { Buf b((size_t) (secs * sr), 0.0f); Rng r(1); bridgedT(b, sr, p, r); return b; };
    BridgedP k; k.f0 = 50; k.dec = 0.8; k.click = 0; k.tone = 0;   // the resonator alone (no leaked pulse) for the pitch measurements
    const Buf kb = kickOf(k, 1.2);
    const double f60 = ch::instFreq(kb, 0.0505, 0.0695), fAtt = ch::instFreq(kb, 0.0015, 0.0058), f20 = ch::instFreq(kb, 0.0105, 0.0295), f150 = ch::instFreq(kb, 0.1405, 0.1595);   // (one period around each point: the analytic phase ripples once per cycle)
    const double sighPct = (1 - f150 / f20) * 100;
    BridgedP flat = k; flat.attackMul = 1; const double fAttFlat = ch::instFreq(kickOf(flat, 0.3), 0.0015, 0.0058);
    std::printf("  bridgedT kick (f0 50, the ANALOG BOOM tuning): f0@60ms %.1f Hz, attack window (0-6 ms) %.1f Hz (%.2f x; no shift: %.1f Hz), sigh %.1f Hz -> %.1f Hz (%.1f %% down 20 -> 150 ms)\n", f60, fAtt, fAtt / f60, fAttFlat, f20, f150, sighPct);
    CHECK(f60 >= 49 && f60 <= 56, "f0@60ms %.1f Hz", f60);
    CHECK(fAtt >= 2 * f60, "attack shift: %.1f Hz >= 2 x f0", fAtt);
    CHECK(sighPct >= 2 && sighPct <= 5, "pitch sigh %.1f %%", sighPct);
    {   const double knobs[] { 0.12, 0.3, 0.6, 1.0, 1.6, 6.0 }; double prev = 0; bool mono = true, close = true; juce::String row;
        for (double d : knobs) { BridgedP p; p.f0 = 52; p.dec = d; const Buf b = kickOf(p, 4.6); const double t40 = ch::tDrop(b, 40);
            if (t40 <= prev) mono = false; if (d <= 1.6 && std::abs(t40 / d - 1) > 0.25) close = false; prev = t40; row << juce::String(d, 2) << " -> " << juce::String(t40, 2) << " s, "; }
        BridgedP p; p.dec = 6.0; const Buf b6 = kickOf(p, 4.6); const double t6 = ch::tDrop(b6, 40);
        double mean = 0, pk = 0; for (auto v : b6) { mean += v; pk = std::max(pk, (double) std::abs(v)); } mean /= (double) b6.size();
        std::printf("  decay knob -> t40: %s DC %.2e of the peak\n", row.toRawUTF8(), std::abs(mean) / pk);
        CHECK(mono && close, "t40 tracks the decay knob (monotonic, within 25 %% up to 1.6 s)");
        CHECK(t6 <= 4.0 + 0.05, "decay clamp: knob 6 s -> t40 %.2f s (<= 4)", t6);
        CHECK(std::abs(mean) / pk < 1e-3, "no DC"); }
}

// ---------------------------------------------------------------- the classic-character packs + kits: shared checks
static std::shared_ptr<gb::DrumLibrary> gClassicLib;
static const gb::DrumLibrary& classicLib() { if (!gClassicLib) gClassicLib = gb::loadBeatsLibrary(48000.0, false); return *gClassicLib; }
static ch::Buf sndData(const char* name) { if (auto s = classicLib().byName(name)) return s->data; std::printf("  missing sound %s\n", name); return ch::Buf(4800, 0.0f); }
// every sound of the classic packs whose name starts with one of `prefixes` (empty = all): in order, appended last, right folder,
// peak-normalised, levelled to its folder median (inside the +-6 dB trim range), 0.02-6 s
static void classicPackChecks(const juce::StringArray& names) {
    const auto& lib = classicLib(); const int n = (int) lib.sounds().size(), nc = gb::classicPackNames().size();
    std::map<juce::String, std::vector<float>> older; for (auto& sp : lib.sounds()) if (sp->category != "Break" && !gb::isPackSound(sp->name)) older[gb::folderOf(sp->category)].push_back(gb::loudest200ms(*sp));
    for (auto& kv : older) std::sort(kv.second.begin(), kv.second.end());
    int found = 0, order = 0, catOk = 0, peakOk = 0, trimOk = 0, lenOk = 0;
    for (auto& nm : names) {
        const int at = lib.indexOf(nm), want = n - nc + gb::classicPackNames().indexOf(nm); const auto* d = gb::classic::find(nm); if (at < 0 || !d) { std::printf("  missing %s\n", nm.toRawUTF8()); continue; } ++found;
        const auto& s = *lib.sounds()[(size_t) at]; if (at == want) ++order; if (s.category == d->cat) ++catOk;
        float pk = 0; for (auto v : s.data) pk = std::max(pk, std::abs(v)); if (pk > 0.85f && pk < 0.9f) ++peakOk;
        const auto& v = older[gb::folderOf(s.category)]; const float med = v[v.size() / 2], raw = gb::loudest200ms(s), lv = raw + juce::Decibels::gainToDecibels(s.trim);
        if (std::abs(lv - med) < 0.6f && std::abs(med - raw) < 8.9f) ++trimOk; else std::printf("  %s: level %.1f dB vs folder median %.1f (raw %.1f)\n", s.name.toRawUTF8(), lv, med, raw);
        const double len = s.data.size() / s.rate; if (len >= 0.02 && len <= 6.0) ++lenOk;
    }
    const int k = names.size();
    CHECK(found == k && order == k && catOk == k && lenOk == k, "pack: %d / %d found, %d in order at the end of the library, %d in the right folder, %d lengths ok", found, k, order, catOk, lenOk);
    CHECK(peakOk == k && trimOk == k, "pack levels: %d peak-normalised, %d levelled to their folder median", peakOk, trimOk);
}
static juce::StringArray packWith(const char* prefix) { juce::StringArray r; for (auto& n : gb::classicPackNames()) if (n.startsWith(prefix)) r.add(n); return r; }
// a kit: appended at `index`, its sounds all exist, patterns A and B (8 bars each) near -14 LUFS, true peak under the ceiling, the limiter idle
static void classicKitChecks(const char* kit, int index) {
    const int k = kitIndexOf(kit); int missing = 0; for (auto* nm : gb::allKits()[(size_t) k].sounds) if (!classicLib().byName(nm)) ++missing;
    BeatsXProcessor q; q.applyKit(k, true); setParam(q, "songmode", 0);
    const float ceiling = juce::Decibels::decibelsToGain(q.apvts.getRawParameterValue("ceiling")->load());
    double l[2] {}; float tp[2] {}, gr[2] {}; double busy[2] {};
    for (int pat = 0; pat < 2; ++pat) { setParam(q, "pattern", (float) (pat + 1)); const auto on = renderBars(q, 8), off = renderBars(q, 8, 2); l[pat] = lufs(on); tp[pat] = truePeak4x(on);
        int b = 0, w = 0; for (int a = 0; a + 480 <= off.getNumSamples(); a += 480) { float m = 0; for (int c = 0; c < 2; ++c) m = std::max(m, off.getMagnitude(c, a, 480)); const float d = juce::Decibels::gainToDecibels(m / ceiling); gr[pat] = std::max(gr[pat], d); if (d > 1.0f) ++b; ++w; }
        busy[pat] = 100.0 * b / juce::jmax(1, w); }
    std::printf("  kit %-17s index %d, %d BPM: A %.1f LUFS / true peak %.2f dBFS / limiter max %.1f dB (%.1f %% busy) | B %.1f LUFS / %.2f dBFS / %.1f dB (%.1f %%)\n", kit, k, gb::allKits()[(size_t) k].bpm,
                l[0], juce::Decibels::gainToDecibels(tp[0]), gr[0], busy[0], l[1], juce::Decibels::gainToDecibels(tp[1]), gr[1], busy[1]);
    CHECK(juce::String(gb::allKits()[(size_t) k].name) == kit && k == index && missing == 0, "kit %s appended at %d (%d), %d sounds missing", kit, index, k, missing);
    CHECK(std::abs(l[0] + 14) <= 1.5 && std::abs(l[1] + 14) <= 1.5, "kit %s near -14 LUFS (A %.1f, B %.1f)", kit, l[0], l[1]);
    CHECK(tp[0] <= ceiling && tp[1] <= ceiling && gr[0] < 1.5f && gr[1] < 1.5f && busy[0] < 1.0 && busy[1] < 1.0, "kit %s clean: true peak under the ceiling, the limiter idle", kit);
}
// a 2-bar demo of a kit (bar 1 = pattern A, bar 2 = pattern B), 24-bit WAV
static void kitDemo(const char* kit, const juce::File& dir) {
    BeatsXProcessor q; q.applyKit(kitIndexOf(kit), true); setParam(q, "songmode", 0);
    setParam(q, "pattern", 1); const auto a = renderBars(q, 1); setParam(q, "pattern", 2); const auto b = renderBars(q, 1);
    juce::AudioBuffer<float> o(2, a.getNumSamples() + b.getNumSamples()); for (int c = 0; c < 2; ++c) { o.copyFrom(c, 0, a, c, 0, a.getNumSamples()); o.copyFrom(c, a.getNumSamples(), b, c, 0, b.getNumSamples()); }
    const auto f = dir.getChildFile(juce::String(kit).replaceCharacter(' ', '_') + "_DEMO.wav"); writeWav24(o, f);
    std::printf("  %s: 2 bars (A, B) at %d BPM, %.1f s, %.1f LUFS, true peak %.2f dBFS\n", f.getFullPathName().toRawUTF8(), gb::allKits()[(size_t) kitIndexOf(kit)].bpm, o.getNumSamples() / 48000.0, lufs(o), juce::Decibels::gainToDecibels(truePeak4x(o)));
}
static ch::Buf lp4(ch::Buf x, double f) { gb::synth::filt(x, 48000, 0, f, 0.54); gb::synth::filt(x, 48000, 0, f, 1.31); return x; }

// ---------------------------------------------------------------- 2. ANALOG BOOM + ANALOG PUNCH
static void testAnalogKits() {
    section("DRUM LIBRARY 2: ANALOG BOOM (8-series character) + ANALOG PUNCH (9-series character)");
    classicPackChecks(packWith("ANALOG "));
    // ---- ANALOG BOOM
    {   const auto kick = sndData("ANALOG BOOM KICK"), boom = sndData("ANALOG BOOM LONG");
        const double f60 = ch::instFreq(kick, 0.0505, 0.0695), att = ch::instFreq(lp4(kick, 1500), 0.0015, 0.0058), kt40 = ch::tDrop(kick, 40), bt40 = ch::tDrop(boom, 40), bf = ch::instFreq(boom, 0.4905, 0.5095 + 0.0213);
        std::printf("  ANALOG BOOM KICK: f0@60ms %.1f Hz, attack window %.1f Hz (%.2f x), t40 %.2f s, centroid %.0f Hz | LONG: %.1f Hz at 0.5 s, t40 %.2f s\n", f60, att, att / f60, kt40, ch::centroid(kick), bf, bt40);
        CHECK(f60 >= 49 && f60 <= 56 && att >= 2 * f60 && kt40 > 0.5 && kt40 < 1.1 && bt40 > 2.5 && bt40 <= 4.0, "ANALOG BOOM kick / long: f0 %.1f, attack %.1f Hz, t40 %.2f / %.2f s", f60, att, kt40, bt40);
        const auto sn = sndData("ANALOG BOOM SNARE"); const double sc = ch::centroid(sn), st40 = ch::tDrop(sn, 40);
        double sp = 0; const int nb = ch::bursts(sndData("ANALOG BOOM CLAP"), &sp);
        std::printf("  ANALOG BOOM SNARE: centroid %.0f Hz, t40 %.2f s | CLAP: %d bursts, %.1f ms apart, t40 %.2f s\n", sc, st40, nb, sp, ch::tDrop(sndData("ANALOG BOOM CLAP"), 40));
        CHECK(sc >= 2500 && sc <= 5000, "snare centroid %.0f Hz", sc);
        CHECK(nb == 4 && sp >= 8 && sp <= 12, "clap: %d bursts %.1f ms apart", nb, sp);
        const auto chh = sndData("ANALOG BOOM CLOSED HAT"), ohh = sndData("ANALOG BOOM OPEN HAT"), cym = sndData("ANALOG BOOM CYMBAL");
        const double cc = ch::centroid(chh), ct = ch::tDrop(chh, 40), ot = ch::tDrop(ohh, 40), lowC = ch::bandDb(chh, 0, 2000), lowO = ch::bandDb(ohh, 0, 2000);
        std::printf("  ANALOG BOOM hats (authentic six-oscillator set, no x1.7): closed centroid %.0f Hz, t40 %.0f ms, < 2 kHz %.1f dB | open t40 %.2f s, < 2 kHz %.1f dB | cymbal centroid %.0f Hz, t40 %.2f s\n",
                    cc, ct * 1000, lowC, ot, lowO, ch::centroid(cym), ch::tDrop(cym, 40));
        CHECK(cc >= 7000 && cc <= 10000 && ct >= 0.03 && ct <= 0.07 && ot >= 0.25 && ot <= 0.7 && lowC <= -20 && lowO <= -20, "8-series hats: centroid, decay, low end");
        const auto pk = ch::twoPeaks(sndData("ANALOG BOOM COWBELL"), 400, 1000);
        std::printf("  ANALOG BOOM COWBELL: peaks %.1f + %.1f Hz (540 + 800)\n", pk.first, pk.second);
        CHECK(std::abs(pk.first / 540 - 1) < 0.03 && std::abs(pk.second / 800 - 1) < 0.03, "cowbell oscillators 540 + 800 Hz");
        const double tl = ch::instFreq(sndData("ANALOG BOOM TOM LO"), 0.1, 0.2), tm = ch::instFreq(sndData("ANALOG BOOM TOM MID"), 0.1, 0.2), th = ch::instFreq(sndData("ANALOG BOOM TOM HI"), 0.1, 0.2),
                     tStart = ch::instFreq(lp4(sndData("ANALOG BOOM TOM MID"), 600), 0.004, 0.012),
                     cl = ch::instFreq(sndData("ANALOG BOOM CONGA LO"), 0.06, 0.12), cm = ch::instFreq(sndData("ANALOG BOOM CONGA MID"), 0.06, 0.12), chi = ch::instFreq(sndData("ANALOG BOOM CONGA HI"), 0.06, 0.12),
                     clv = ch::instFreq(sndData("ANALOG BOOM CLAVES"), 0.005, 0.03);
        std::printf("  ANALOG BOOM toms %.0f / %.0f / %.0f Hz (mid starts at %.0f Hz: the pitch drop), congas %.0f / %.0f / %.0f Hz, claves %.0f Hz, rimshot centroid %.0f Hz, maracas centroid %.0f Hz\n", tl, tm, th, tStart, cl, cm, chi, clv,
                    ch::centroid(sndData("ANALOG BOOM RIMSHOT")), ch::centroid(sndData("ANALOG BOOM MARACAS")));
        CHECK(std::abs(tl / 90 - 1) < 0.06 && std::abs(tm / 130 - 1) < 0.06 && std::abs(th / 180 - 1) < 0.06 && tStart > tm * 1.1, "toms tuned 90 / 130 / 180 with a pitch drop");
        CHECK(std::abs(cl / 220 - 1) < 0.05 && std::abs(cm / 330 - 1) < 0.05 && std::abs(chi / 470 - 1) < 0.05 && std::abs(clv / 2500 - 1) < 0.05, "congas 220 / 330 / 470, claves 2.5 kHz");
    }
    // ---- ANALOG PUNCH
    {   const auto kick = sndData("ANALOG PUNCH KICK"); const auto kl = lp4(kick, 450);
        const double f5 = ch::instFreq(kl, 0.003, 0.007), f60 = ch::instFreq(kl, 0.052, 0.068), kt40 = ch::tDrop(kick, 40);
        std::printf("  ANALOG PUNCH KICK: f(5 ms) %.0f Hz, f(60 ms) %.1f Hz, sweep %.2f x, t40 %.2f s, centroid %.0f Hz\n", f5, f60, f5 / f60, kt40, ch::centroid(kick));
        CHECK(f5 / f60 >= 3 && f5 / f60 <= 5, "kick sweep %.2f x", f5 / f60);
        const auto pk = ch::twoPeaks(sndData("ANALOG PUNCH SNARE"), 120, 450);
        std::printf("  ANALOG PUNCH SNARE: partials %.1f + %.1f Hz (180 + 330), centroid %.0f Hz\n", pk.first, pk.second, ch::centroid(sndData("ANALOG PUNCH SNARE")));
        CHECK(std::abs(pk.first / 180 - 1) < 0.05 && std::abs(pk.second / 330 - 1) < 0.05, "snare partials 180 + 330 Hz");
        auto fl = [&](const char* nm) { const auto* d = gb::classic::find(nm); if (!d) return 0.0; const auto a = gb::classic::renderRaw(*d, 48000), b = gb::classic::renderRaw(*d, 48000, true);
            float p = 1e-9f; for (auto v : b) p = std::max(p, std::abs(v)); double r = 0; const int m = (int) (0.005 * 48000); for (int i = 0; i < m; ++i) { const double e = a[(size_t) i] - b[(size_t) i]; r += e * e; }
            return 20 * std::log10(std::sqrt(r / m) / p + 1e-12); };
        const double fc = fl("ANALOG PUNCH CLOSED HAT"), fo = fl("ANALOG PUNCH OPEN HAT"), fr = fl("ANALOG PUNCH RIDE");
        const auto oh = sndData("ANALOG PUNCH OPEN HAT"); const double tailRes = [&] { const auto* d = gb::classic::find("ANALOG PUNCH OPEN HAT"); const auto a = gb::classic::renderRaw(*d, 48000), b = gb::classic::renderRaw(*d, 48000, true);
            double r = 0; const int a0 = (int) (0.4 * 48000), a1 = (int) (0.5 * 48000); for (int i = a0; i < a1; ++i) { const double e = a[(size_t) i] - b[(size_t) i]; r += e * e; } return 20 * std::log10(std::sqrt(r / (a1 - a0)) + 1e-12); }();
        std::printf("  ANALOG PUNCH metal (6-bit, 32 kHz, envelope after the DAC): floor vs peak at the strike: closed %.1f, open %.1f, ride %.1f dB (ideal 6-bit -40.9) | open hat tail (0.4-0.5 s) residual %.1f dBFS | closed t40 %.0f ms, open t40 %.2f s, centroid %.0f Hz\n",
                    fc, fo, fr, tailRes, ch::tDrop(sndData("ANALOG PUNCH CLOSED HAT"), 40) * 1000, ch::tDrop(oh, 40), ch::centroid(sndData("ANALOG PUNCH CLOSED HAT")));
        CHECK(std::abs(fc + 38) <= 4 && std::abs(fo + 38) <= 4 && std::abs(fr + 38) <= 4, "6-bit metal floors %.1f / %.1f / %.1f dB", fc, fo, fr);
    }
    classicKitChecks("ANALOG BOOM", 33); classicKitChecks("ANALOG PUNCH", 34);
}

// floor of a pack sound's converter vs its peak, over [a, a + n) seconds (the recipe rendered twice: with and without the quantiser)
static double packFloorDb(const char* nm, double a, double n) {
    const auto* d = gb::classic::find(nm); if (!d) return 0; const auto x = gb::classic::renderRaw(*d, 48000), y = gb::classic::renderRaw(*d, 48000, true);
    float p = 1e-9f; for (auto v : y) p = std::max(p, std::abs(v)); double r = 0; const int i0 = (int) (a * 48000), i1 = juce::jmin((int) y.size(), (int) ((a + n) * 48000));
    for (int i = i0; i < i1; ++i) { const double e = (double) x[(size_t) i] - y[(size_t) i]; r += e * e; } return 20 * std::log10(std::sqrt(r / juce::jmax(1, i1 - i0)) / p + 1e-12);
}
// ---------------------------------------------------------------- 3. DIGITAL EIGHTIES + CRUNCH TWELVE + SWING TWELVE
static void testSampledEraKits() {
    section("DRUM LIBRARY 3: DIGITAL EIGHTIES (8-bit mu-law ~28-35 kHz) + CRUNCH TWELVE (12-bit 26.04 kHz) + SWING TWELVE (12-bit 40 kHz)");
    classicPackChecks(packWith("DIGITAL EIGHTIES ")); classicPackChecks(packWith("CRUNCH TWELVE ")); classicPackChecks(packWith("SWING TWELVE "));
    using namespace gb::synth;
    // ---- mu-law: the floor follows the level (8-bit companding keeps ~constant SNR), linear does not
    {   auto sine = [&](double db) { Buf b(24000); for (size_t i = 0; i < b.size(); ++i) b[i] = (float) (juce::Decibels::decibelsToGain(db) * 0.99 * std::sin(TAU * 440 * i / 48000.0)); return b; };
        auto snr = [&](double db, int law) { Buf x = sine(db), y = x; EraP p; p.bits = 8; p.law = law; era(y, 48000, p); return -ch::floorDb(y, x); };
        const double m0 = snr(0, 1), m30 = snr(-30, 1), l0 = snr(0, 0), l30 = snr(-30, 0);
        std::printf("  8-bit SNR at 0 / -30 dBFS: mu-law %.1f / %.1f dB, linear %.1f / %.1f dB\n", m0, m30, l0, l30);
        CHECK(std::abs(m0 - m30) <= 6 && l0 - l30 > 25, "mu-law floor tracks the level (%.1f vs %.1f dB)", m0, m30); }
    // ---- DIGITAL EIGHTIES
    {   const auto kick = sndData("DIGITAL EIGHTIES KICK"), sn = sndData("DIGITAL EIGHTIES SNARE");
        const double kt = ch::tDrop(kick, 40), st = ch::tDrop(sn, 40), sc = ch::centroid(sn), fk = packFloorDb("DIGITAL EIGHTIES KICK", 0.0, 0.03), fs = packFloorDb("DIGITAL EIGHTIES SNARE", 0.0, 0.03);
        double sp = 0; const int nb = ch::bursts(sndData("DIGITAL EIGHTIES CLAP"), &sp);
        const double tl = ch::centroid(sndData("DIGITAL EIGHTIES TOM LO")), tm = ch::centroid(sndData("DIGITAL EIGHTIES TOM MID")), th = ch::centroid(sndData("DIGITAL EIGHTIES TOM HI"));
        const double ftl = ch::instFreq(lp4(sndData("DIGITAL EIGHTIES TOM LO"), 400), 0.08, 0.16), ftm = ch::instFreq(lp4(sndData("DIGITAL EIGHTIES TOM MID"), 400), 0.08, 0.16), fth = ch::instFreq(lp4(sndData("DIGITAL EIGHTIES TOM HI"), 500), 0.08, 0.16);
        const double hStrike = packFloorDb("DIGITAL EIGHTIES OPEN HAT", 0.0, 0.01), hTail = packFloorDb("DIGITAL EIGHTIES OPEN HAT", 0.45, 0.1);
        std::printf("  DIGITAL EIGHTIES: kick t40 %.0f ms (tight), snare t40 %.0f ms, centroid %.0f Hz | 8-bit mu-law floor vs peak: kick %.1f, snare %.1f dB | clap %d bursts %.1f ms apart\n", kt * 1000, st * 1000, sc, fk, fs, nb, sp);
        std::printf("  DIGITAL EIGHTIES toms tuned by the clock: %.0f / %.0f / %.0f Hz, centroid %.0f / %.0f / %.0f Hz (the low one darker) | open hat (8-bit, decay after the DAC): floor %.1f dB at the strike, %.1f dB in the tail\n", ftl, ftm, fth, tl, tm, th, hStrike, hTail);
        CHECK(kt <= 0.22 && st <= 0.35 && sc >= 2000 && sc <= 6000, "DIGITAL EIGHTIES kick / snare (t40 %.2f / %.2f s, centroid %.0f)", kt, st, sc);
        CHECK(fk <= -25 && fk >= -55 && fs <= -25 && fs >= -55, "8-bit converter floors %.1f / %.1f dB", fk, fs);
        CHECK(nb == 5, "a group clap: %d bursts", nb);
        CHECK(tl < tm && tm < th && std::abs(ftm / 130 - 1) < 0.06 && std::abs(ftl / (130 * std::pow(2.0, -5 / 12.0)) - 1) < 0.06 && std::abs(fth / (130 * std::pow(2.0, 4 / 12.0)) - 1) < 0.06, "clock-tuned toms (pitch + brightness move together)");
        CHECK(hTail <= hStrike - 20, "decay after the DAC: the tail is %.1f dB cleaner", hStrike - hTail);
    }
    // ---- CRUNCH TWELVE
    {   const auto sn = sndData("CRUNCH TWELVE SNARE"), kk = sndData("CRUNCH TWELVE KICK"), ko = sndData("CRUNCH TWELVE KICK OPEN");
        const double img = ch::bandDb(sn, 13020, 24000), imgK = ch::bandDb(ko, 13020, 24000), fl = packFloorDb("CRUNCH TWELVE SNARE", 0.0, 0.03);
        const double c0 = ch::centroid(kk, 0.0, 0.05), c1 = ch::centroid(kk, 0.15, 0.05), o0 = ch::centroid(ko, 0.0, 0.05), o1 = ch::centroid(ko, 0.15, 0.05);
        const double kf = ch::instFreq(lp4(kk, 300), 0.08, 0.12);
        std::printf("  CRUNCH TWELVE: open-channel snare images %.1f dB above 13.02 kHz (open kick %.1f) | 12-bit floor vs peak %.1f dB | swept-channel kick centroid %.0f -> %.0f Hz (%.0f %% fall; open channel %.0f -> %.0f) | kick %.1f Hz after the -5 st drop-sample\n",
                    img, imgK, fl, c0, c1, 100 * (1 - c1 / c0), o0, o1, kf);
        CHECK(img >= -30, "images %.1f dB", img);
        CHECK(std::abs(fl + 74) <= 4, "12-bit floor %.1f dB", fl);
        CHECK(c1 <= c0 * 0.6, "swept channel: the centroid falls %.0f %%", 100 * (1 - c1 / c0));
        CHECK(std::abs(kf / 52 - 1) < 0.08, "the 45-rpm trick lands the kick back on its pitch (%.1f Hz)", kf);
    }
    // ---- SWING TWELVE
    {   int clean = 0; double worst = -200; const auto names = packWith("SWING TWELVE ");
        for (auto& nm : names) { const double hi = ch::bandDb(sndData(nm.toRawUTF8()), 18500, 24000); worst = std::max(worst, hi); if (hi <= -45) ++clean; }
        const double fl = packFloorDb("SWING TWELVE SNARE", 0.0, 0.03), fk = packFloorDb("SWING TWELVE KICK", 0.0, 0.03);
        std::printf("  SWING TWELVE: energy above 18.5 kHz <= -45 dB in %d / %d sounds (worst %.1f dB) | 12-bit floor vs peak: snare %.1f, kick %.1f dB\n", clean, names.size(), worst, fl, fk);
        CHECK(clean == names.size(), "~18 kHz bandwidth (worst %.1f dB above 18.5 kHz)", worst);
        CHECK(std::abs(fl + 74) <= 4 && std::abs(fk + 74) <= 4, "12-bit floors %.1f / %.1f dB", fl, fk);
    }
    // ---- swing presets: 16th swing S % puts the off 16th at 2 S / 100 sixteenths (66.7 % = the triplet); the kits' hat swing is 58 % / 61 %
    {   const auto clk = makeSound("click", 480, [](int i) { return i < 48 ? 0.9f : 0.0f; }); const auto* snd = clk.get(); juce::String row; bool ok = true;
        for (double S : { 50.0, 54.0, 58.0, 62.0, 66.6667, 71.0 }) { if (!snd) break;
            gb::Snapshot Sn = oneTrack(snd); auto& pt = Sn.patterns[0]; pt.length = 16; pt.res = 1; pt.steps[0][0].vel = 110; pt.steps[0][1].vel = 110; Sn.tracks[0].swing = (float) (4 * S / 100 - 2);
            const auto o = onsets(render(Sn, cleanParams(120), 0.5)); const double want = 2 * S / 100 * 6000;   // 120 BPM: a 16th = 6000 samples
            const int got = o.size() >= 2 ? o[1] - o[0] : -1; if (std::abs(got - want) > 1.0) ok = false; row << juce::String(S, 1) << "% -> " << got << " (" << juce::String(want, 0) << "), "; }
        std::printf("  swing presets (second 16th, samples at 120 BPM): %s kit hat swing CRUNCH TWELVE %.0f %%, SWING TWELVE %.0f %%\n", row.toRawUTF8(), (gb::allKits()[(size_t) kitIndexOf("CRUNCH TWELVE")].hatSwing + 2) * 25, (gb::allKits()[(size_t) kitIndexOf("SWING TWELVE")].hatSwing + 2) * 25);
        CHECK(ok, "swing presets 50-71 %% (66.7 %% = 2/3 of the 8th, +-1 sample)");
    }
    classicKitChecks("DIGITAL EIGHTIES", 35); classicKitChecks("CRUNCH TWELVE", 36); classicKitChecks("SWING TWELVE", 37);
}

// ---------------------------------------------------------------- 4. TRAP GOLD + TEXAS BOUNCE + BOOM BAP CLASSIC + POP SHINE
static void testModernKits() {
    section("DRUM LIBRARY 4: TRAP GOLD + TEXAS BOUNCE + BOOM BAP CLASSIC + POP SHINE (modern processed kits)");
    for (auto* pre : { "TRAP GOLD ", "TEXAS BOUNCE ", "BOOM BAP CLASSIC ", "POP SHINE " }) classicPackChecks(packWith(pre));
    {   const auto k = sndData("TRAP GOLD KICK"), cl = sndData("TRAP GOLD CLAP"), hh = sndData("TRAP GOLD CLOSED HAT"), oh = sndData("TRAP GOLD OPEN HAT");
        const double kt = ch::tDrop(k, 40), kc = ch::centroid(k), cc = ch::centroid(cl), ht = ch::tDrop(hh, 40), ot = ch::tDrop(oh, 40);
        juce::String row; bool tuned = true; double minT = 99;
        for (auto nf : std::vector<std::pair<const char*, double>> { { "TRAP GOLD 808 D", 36.71 }, { "TRAP GOLD 808 F", 43.65 }, { "TRAP GOLD 808 G", 49.0 }, { "TRAP GOLD 808 A", 55.0 } }) {
            const auto b = sndData(nf.first); const double f = ch::instFreq(lp4(b, 120), 0.4, 0.8), t = ch::tDrop(b, 40); if (std::abs(f / nf.second - 1) > 0.03) tuned = false; minT = std::min(minT, t);
            row << juce::String(nf.first).fromLastOccurrenceOf(" ", false, false) << " " << juce::String(f, 1) << " Hz / t40 " << juce::String(t, 2) << " s, "; }
        const double f5 = ch::instFreq(lp4(sndData("TRAP GOLD 808 F"), 400), 0.002, 0.008);
        std::printf("  TRAP GOLD: kick t40 %.0f ms, centroid %.0f Hz (click-led) | clap centroid %.0f Hz | closed hat t40 %.0f ms (a 1/32 at 150 BPM = 50 ms), open %.2f s | 808s %s 808 F starts at %.0f Hz (pitch envelope) | bell centroid %.0f Hz\n",
                    kt * 1000, kc, cc, ht * 1000, ot, row.toRawUTF8(), f5, ch::centroid(sndData("TRAP GOLD BELL")));
        CHECK(kt >= 0.07 && kt <= 0.15 && kc >= 1500, "TRAP GOLD kick t40 %.0f ms, centroid %.0f", kt * 1000, kc);
        CHECK(cc >= 1500 && cc <= 3000 && ht <= 0.04, "TRAP GOLD clap centroid %.0f, hat t40 %.0f ms", cc, ht * 1000);
        CHECK(tuned && minT >= 0.6 && f5 > 43.65 * 1.5, "TRAP GOLD 808 family tuned, long, with the pitch envelope");
    }
    {   const auto k = sndData("TEXAS BOUNCE KICK"), cl = sndData("TEXAS BOUNCE CLAP"), b8 = sndData("TEXAS BOUNCE 808");
        const double kt = ch::tDrop(k, 40), ct = ch::tDrop(cl, 40), bt = ch::tDrop(b8, 40), bf = ch::instFreq(lp4(b8, 120), 0.1, 0.25);
        std::printf("  TEXAS BOUNCE: kick t40 %.0f ms | clap t40 %.2f s (roomy) | 808 %.1f Hz, t40 %.2f s (short) | snap centroid %.0f Hz | cowbell peaks %.0f + %.0f Hz\n", kt * 1000, ct, bf, bt, ch::centroid(sndData("TEXAS BOUNCE SNAP")),
                    ch::twoPeaks(sndData("TEXAS BOUNCE COWBELL"), 500, 1200).first, ch::twoPeaks(sndData("TEXAS BOUNCE COWBELL"), 500, 1200).second);
        CHECK(kt >= 0.09 && kt <= 0.18 && ct >= 0.15 && ct <= 0.3 && bt <= 0.45 && std::abs(bf / 49 - 1) < 0.04, "TEXAS BOUNCE kick %.0f ms, clap %.2f s, 808 %.2f s", kt * 1000, ct, bt);
        CHECK(std::abs(gb::allKits()[(size_t) kitIndexOf("TEXAS BOUNCE")].bpm - 115) <= 10, "TEXAS BOUNCE up-tempo, straight");
    }
    {   const auto k = sndData("BOOM BAP CLASSIC KICK"), sn = sndData("BOOM BAP CLASSIC SNARE");
        const double kf = ch::instFreq(lp4(k, 200), 0.1, 0.2), fl = packFloorDb("BOOM BAP CLASSIC SNARE", 0.0, 0.03), img = ch::bandDb(sn, 13020, 24000), sw = (gb::allKits()[(size_t) kitIndexOf("BOOM BAP CLASSIC")].hatSwing + 2) * 25;
        std::printf("  BOOM BAP CLASSIC: kick %.1f Hz (55 Hz source, 35 cents flat = %.1f) | snare 12-bit floor %.1f dB, images %.1f dB, t40 %.2f s | hat swing %.0f %% | %d BPM\n", kf, 55 * std::pow(2.0, -0.35 / 12), fl, img, ch::tDrop(sn, 40), sw,
                    gb::allKits()[(size_t) kitIndexOf("BOOM BAP CLASSIC")].bpm);
        CHECK(kf < 55 * 1.0 && kf > 55 * std::pow(2.0, -1.0 / 12), "BOOM BAP CLASSIC a hair flat (%.1f Hz)", kf);
        CHECK(std::abs(fl + 74) <= 4 && std::abs(sw - 58) < 0.5 && gb::allKits()[(size_t) kitIndexOf("BOOM BAP CLASSIC")].bpm == 90, "BOOM BAP CLASSIC: 12-bit (%.1f dB), swing 58 %%, 90 BPM", fl);
    }
    {   const auto sn = sndData("POP SHINE SNARE"), gs = sndData("POP SHINE SNARE GATED"); const double st = ch::tDrop(sn, 40);
        double pre = 0, post = 0; for (size_t i = 0; i < gs.size(); ++i) { const double e = (double) gs[i] * gs[i]; if (i < (size_t) (0.17 * 48000)) pre = std::max(pre, e); else if (i > (size_t) (0.205 * 48000)) post = std::max(post, e); }
        const double gate = 10 * std::log10(post / (pre + 1e-30) + 1e-30);
        std::printf("  POP SHINE: snare t40 %.2f s (plate), centroid %.0f Hz | gated snare: after the gate %.1f dB below its peak | kick t40 %.0f ms, clap centroid %.0f Hz\n", st, ch::centroid(sn), -gate, ch::tDrop(sndData("POP SHINE KICK"), 40) * 1000, ch::centroid(sndData("POP SHINE CLAP")));
        CHECK(st >= 0.25 && st <= 0.5, "POP SHINE snare plate t40 %.2f s", st);
        CHECK(gate <= -40, "the gated Eighties snare shuts (%.1f dB)", gate);
    }
    classicKitChecks("TRAP GOLD", 38); classicKitChecks("TEXAS BOUNCE", 39); classicKitChecks("BOOM BAP CLASSIC", 40); classicKitChecks("POP SHINE", 41);
}

// ---------------------------------------------------------------- 5. TRAP CLASSIC style + kit
static void testTrapClassic() {
    section("TRAP CLASSIC style + kit: rolling hats, the 808 on the kicks, snare + clap on 3, tempo, rotation, levels, state");
    namespace T = gb::trapc;
    {   juce::StringArray tc { "BOOM 808", "TRAP KICK", "TRAP CLASSIC SNARE", "TRAP CLASSIC CLAP", "TRAP HAT CLOSED", "TRAP HAT OPEN", "TRAP CLASSIC CRASH", "BRASS HIT", "TIMP TOM" };
        classicPackChecks(tc);
        int clash = 0; for (auto& nm : gb::classicPackNames()) for (auto& sp : classicLib().sounds()) if (sp->name != nm && sp->name.equalsIgnoreCase(nm)) { ++clash; std::printf("  name clash: %s / %s\n", nm.toRawUTF8(), sp->name.toRawUTF8()); }
        CHECK(clash == 0, "classic pack names unique (case-insensitive) in the library");
        int same = 0; uint64_t h = 1469598103934665603ULL;
        for (auto& d : gb::classic::defs()) { const auto s1 = gb::classicSound(d, 48000.0); const auto lib = classicLib().byName(d.name); if (lib && lib->data == s1->data) ++same; for (auto v : s1->data) { uint32_t u; std::memcpy(&u, &v, 4); h = (h ^ u) * 1099511628211ULL; } }
        std::printf("  classic packs: %d sounds, the parallel library render == a serial render for %d, pack hash %016llx\n", (int) gb::classic::defs().size(), same, (unsigned long long) h);
        CHECK(same == (int) gb::classic::defs().size() && (int) gb::classic::defs().size() == gb::classicPackNames().size() && h == 0x9726f9f165283f7bULL, "classic packs: parallel render bit-identical to serial (%d / %d), pack hash pinned (later packs append, these stay)", same, (int) gb::classic::defs().size());
        const auto b8 = sndData("BOOM 808"), hc = sndData("TRAP HAT CLOSED"), br = sndData("BRASS HIT"), tp = sndData("TIMP TOM");
        const double f8 = ch::instFreq(lp4(b8, 120), 0.5, 1.0), t8 = ch::tDrop(b8, 40), ht = ch::tDrop(hc, 40), hcc = ch::centroid(hc), ft = ch::instFreq(lp4(tp, 140), 0.1, 0.3);
        const auto bp = ch::twoPeaks(br, 150, 450, 25);
        std::printf("  BOOM 808 %.1f Hz (G1 49.0), t40 %.2f s | TRAP HAT CLOSED t40 %.0f ms, centroid %.0f Hz | BRASS HIT peaks %.0f + %.0f Hz, centroid %.0f Hz | TIMP TOM %.1f Hz (G2 98), t40 %.2f s | clap t40 %.2f s, snare t40 %.2f s (reverb)\n",
                    f8, t8, ht * 1000, hcc, bp.first, bp.second, ch::centroid(br), ft, ch::tDrop(tp, 40), ch::tDrop(sndData("TRAP CLASSIC CLAP"), 40), ch::tDrop(sndData("TRAP CLASSIC SNARE"), 40));
        CHECK(std::abs(f8 / 49 - 1) < 0.03 && t8 >= 1.5 && ht <= 0.04 && hcc >= 7000 && std::abs(ft / 98 - 1) < 0.05, "TRAP CLASSIC sounds: a long G1 808, short bright hats, a G2 timp");
    }
    const int tk = kitIndexOf("TRAP CLASSIC");
    CHECK(juce::String(gb::allKits()[(size_t) tk].name) == "TRAP CLASSIC" && tk == (int) gb::allKits().size() - 1 && tk == 42, "the TRAP CLASSIC kit is appended last (index %d)", tk);
    BeatsXProcessor p; p.applyKit(tk, true); const auto roles = p.roles(); std::array<bool, gb::kTracks> none {};
    const auto kinds = T::kindsOf(p.model, [&](const juce::String& nm) { return p.categoryOf(nm); });
    auto trackOf = [&](const std::array<uint8_t, gb::kTracks>& ks, int kind) { for (int t = 0; t < gb::kTracks; ++t) if (ks[(size_t) t] == kind) return t; return -1; };
    const int tK = trackOf(kinds, T::KKick), tB = trackOf(kinds, T::KBass), tS = trackOf(kinds, T::KSnare), tC = trackOf(kinds, T::KClap), tH = trackOf(kinds, T::KHat), tO = trackOf(kinds, T::KOpen),
              tX = trackOf(kinds, T::KCrash), tBr = trackOf(kinds, T::KBrass), tTi = trackOf(kinds, T::KTimp);
    CHECK(tB == 0 && tK == 1 && tS == 2 && tC == 3 && tH == 4 && tO == 5 && tBr == 6 && tTi == 7 && tX == 8, "TRAP CLASSIC kit: every slot its part (808 %d kick %d snare %d clap %d hat %d open %d brass %d timp %d crash %d)", tB, tK, tS, tC, tH, tO, tBr, tTi, tX);
    const int N = 300; int rollsOk = 0, rampOk = 0, varied = 0, openOk = 0, bassOk = 0, slides = 0, snareOk = 0, dropOk = 0, crashOk = 0, lenOk = 0, rangeOk = 0, rot = 0, tempoOk = 0, timpOk = 0;
    std::set<int> vocab, fams, tempos; int rMin = 99, rMax = 0;
    for (int seed = 1; seed <= N; ++seed) {
        gb::Pattern pt; pt.clear(); gb::rhythm::generate(pt, roles, none, T::kStyle, seed, 0, &kinds);
        if (pt.length == 64 && pt.res == 1 && pt.fillEvery == 0) ++lenOk;
        std::set<int> v; const int nr = T::hatRolls(pt, tH, &v); vocab.insert(v.begin(), v.end()); rMin = std::min(rMin, nr); rMax = std::max(rMax, nr);
        if (nr >= 4 && nr <= 8) ++rollsOk; if (v.size() >= 2) ++varied;
        bool ramps = true; for (int s = 0; s < 64; ++s) { const auto& st = pt.steps[(size_t) tH][(size_t) s]; if (st.vel && st.roll > 1 && st.velRamp == 0) ramps = false; } if (ramps) ++rampOk;
        { bool ok = true; int n = 0; for (int s = 0; s < 64; ++s) if (pt.steps[(size_t) tO][(size_t) s].vel) { ++n; if (s % 4 != 2) ok = false; } if (ok && n >= 1) ++openOk; }
        if (T::bassOnKicks(pt, tK, tB)) ++bassOk;
        { bool any = false, attOk = true; for (int s = 0; s < 64; ++s) { const auto& st = pt.steps[(size_t) tB][(size_t) s]; if (!st.vel) continue; if (st.slide()) any = true; if (st.slide() == st.mono() || st.hold < 1) attOk = false; } if (any) ++slides; if (!attOk) bassOk -= 1; }
        { bool ok = true; for (int b = 0; b < 4; ++b) { if (pt.steps[(size_t) tS][(size_t) (b * 16 + 8)].vel < 100 || pt.steps[(size_t) tC][(size_t) (b * 16 + 8)].vel < 90) ok = false; }
          for (int s = 0; s < 60; ++s) if (s % 16 != 8 && pt.steps[(size_t) tS][(size_t) s].vel >= 90) ok = false; if (ok) ++snareOk; }
        { int n = 0; for (int s = 60; s < 64; ++s) { const auto& st = pt.steps[(size_t) tS][(size_t) s]; if (st.vel) n += st.roll; } if (n >= 4) ++dropOk; }
        if (pt.steps[(size_t) tX][0].vel && pt.steps[(size_t) tBr][0].vel) ++crashOk;
        { int n = 0; for (int s = 60; s < 64; ++s) n += pt.steps[(size_t) tTi][(size_t) s].vel > 0; if (n == 4) ++timpOk; }
        bool inRange = true; for (int t = 0; t < gb::kTracks; ++t) for (int s = 0; s < gb::kMaxSteps; ++s) { const auto& st = pt.steps[(size_t) t][(size_t) s]; if (!st.vel) continue; if (s >= 64 || st.vel > 127 || st.roll < 1 || st.roll > 16) inRange = false; } if (inRange) ++rangeOk;
        fams.insert(T::family(seed)); if (seed > 1 && T::family(seed) != T::family(seed - 1) && T::rollOrder(seed) != T::rollOrder(seed - 1)) ++rot;
        const int bpm = T::tempoFor(seed); tempos.insert(bpm); if (bpm >= 140 && bpm <= 160) ++tempoOk;
    }
    juce::String vs; for (int r : vocab) vs << T::rollName(r) << ", ";
    std::printf("  %d generations: 4 bars %d | hat rolls %d..%d per 4 bars (4-8 in %d), >= 2 roll kinds %d, every roll ramped %d, vocabulary seen: %s | open hats on the off-beat 8ths %d\n", N, lenOk, rMin, rMax, rollsOk, varied, rampOk, vs.toRawUTF8(), openOk);
    std::printf("  808 on the kick steps (and only there, MONO / SLIDE, held) %d, with slides %d | snare + clap on 3 of every bar %d, snare roll into the drop %d, timp under it %d, crash + brass on the phrase start %d | steps in range %d\n", bassOk, slides, snareOk, dropOk, timpOk, crashOk, rangeOk);
    std::printf("  rotation: %d families, family + roll order change on every new generation %d / %d | tempo %d..%d BPM (%d values)\n", (int) fams.size(), rot, N - 1, *tempos.begin(), *tempos.rbegin(), (int) tempos.size());
    CHECK(lenOk == N && rollsOk == N && varied == N && rampOk == N && vocab.size() == (size_t) T::kRolls, "rolling hats: %d / %d with 4-8 rolls, %d roll kinds seen", rollsOk, N, (int) vocab.size());
    CHECK(openOk >= N * 9 / 10, "open-hat off-beats %d / %d", openOk, N);
    CHECK(bassOk == N && slides >= N / 4, "808 on the kick positions %d, slides in %d", bassOk, slides);
    CHECK(snareOk == N && dropOk == N && timpOk == N && crashOk == N && rangeOk == N, "snare on 3 %d, drop roll %d, timp %d, crash %d", snareOk, dropOk, timpOk, crashOk);
    CHECK(fams.size() >= 8 && fams.size() == T::families().size() && rot == N - 1, "%d pattern families, rotation", (int) fams.size());
    CHECK(tempoOk == N && tempos.size() == 21, "tempo map 140-160");
    {   // another kit (no brass / timp): snare on 3 and the 808 on the kicks still; through the processor: the tempo lane + the 808 duck
        BeatsXProcessor q; q.applyKit(kitIndexOf("Trap"), true); setParam(q, "songmode", 0); const auto qk = T::kindsOf(q.model, [&](const juce::String& nm) { return q.categoryOf(nm); });
        const int k2 = trackOf(qk, T::KKick), b2 = trackOf(qk, T::KBass), s2 = trackOf(qk, T::KSnare); int ok = 0;
        for (int seed = 1; seed <= 50; ++seed) { gb::Pattern pt; pt.clear(); gb::rhythm::generate(pt, q.roles(), none, T::kStyle, seed, 0, &qk); bool g = k2 >= 0 && b2 >= 0 && s2 >= 0 && T::bassOnKicks(pt, k2, b2);
            for (int b = 0; b < 4 && g; ++b) g = pt.steps[(size_t) s2][(size_t) (b * 16 + 8)].vel >= 100; ok += g; }
        q.model.toolSeed = 21; q.generatePattern(T::kStyle); const float bpm = q.apvts.getRawParameterValue("bpm")->load();
        std::printf("  Trap kit: snare on 3 + 808 on the kicks in %d / 50 | GENERATE TRAP CLASSIC (seed %d): %.0f BPM (wants %d), 808 duck %.2f\n", ok, q.model.toolSeed, bpm, T::tempoFor(q.model.toolSeed), q.model.tracks[(size_t) b2].duck);
        CHECK(ok == 50 && std::lround(bpm) == T::tempoFor(q.model.toolSeed) && std::abs(q.model.tracks[(size_t) b2].duck - 0.4f) < 1e-6f, "TRAP CLASSIC on another kit + tempo lane + duck");
    }
    {   BeatsXProcessor a; a.applyKit(tk, true); setParam(a, "songmode", 0); a.model.toolSeed = 40; a.generatePattern(T::kStyle); a.publish();
        const auto st = state(a); BeatsXProcessor b; b.setStateInformation(st.getData(), (int) st.getSize());
        bool same = b.model.kit == "TRAP CLASSIC"; for (int i = 0; i < 2; ++i) same = same && std::memcmp(a.model.patterns[(size_t) i].steps.data(), b.model.patterns[(size_t) i].steps.data(), sizeof(gb::Pattern::steps)) == 0 && a.model.patterns[(size_t) i].length == b.model.patterns[(size_t) i].length;
        const auto ha = fnvBuf(renderBars(a, 4)), hb = fnvBuf(renderBars(b, 4));
        CHECK(same && ha == hb, "TRAP CLASSIC state round trip (render %016llx vs %016llx)", (unsigned long long) ha, (unsigned long long) hb);
    }
    {   BeatsXProcessor q; q.applyKit(tk, true); setParam(q, "songmode", 0);
        const float ceiling = juce::Decibels::decibelsToGain(q.apvts.getRawParameterValue("ceiling")->load());
        float worstTp = 0, worstGr = 0; double worstBusy = 0, lmin = 0, lmax = -100, sMin = 0, sMax = -100;
        for (int g = 0; g < 12; ++g) {
            if (g == 1) setParam(q, "pattern", 2); if (g == 2) setParam(q, "pattern", 1);
            if (g >= 2) { q.model.toolSeed = 300 + g * 11; q.generatePattern(T::kStyle); }
            const auto on = renderBars(q, 8), off = renderBars(q, 8, 2); const float tpk = truePeak4x(on); const double l = lufs(on);
            worstTp = std::max(worstTp, tpk); lmin = std::min(lmin, l); lmax = std::max(lmax, l); if (g < 2) { sMin = std::min(sMin, l); sMax = std::max(sMax, l); }
            int busy = 0, wins = 0; for (int a = 0; a + 480 <= off.getNumSamples(); a += 480) { float m = 0; for (int c = 0; c < 2; ++c) m = std::max(m, off.getMagnitude(c, a, 480));
                const float d = juce::Decibels::gainToDecibels(m / ceiling); worstGr = std::max(worstGr, d); if (d > 1.0f) ++busy; ++wins; }
            worstBusy = std::max(worstBusy, 100.0 * busy / juce::jmax(1, wins));
        }
        std::printf("  TRAP CLASSIC kit, 12 beats (A, B + 10 generations) x 8 bars: %.1f..%.1f LUFS (starters %.1f..%.1f), true peak %.2f dBFS (ceiling %.2f), limiter work max %.1f dB, > 1 dB in %.1f %% of 10 ms windows\n",
                    lmin, lmax, sMin, sMax, juce::Decibels::gainToDecibels(worstTp), juce::Decibels::gainToDecibels(ceiling), worstGr, worstBusy);
        CHECK(worstTp <= ceiling && worstGr < 1.5f && worstBusy < 1.0, "TRAP CLASSIC clean (true peak %.2f dBFS, limiter max %.1f dB)", juce::Decibels::gainToDecibels(worstTp), worstGr);
        CHECK(sMin >= -15.5 && sMax <= -12.5 && lmin >= -16.5 && lmax <= -11.5, "TRAP CLASSIC near -14 LUFS (starters within 1.5 LU, generations within 2.5 LU: %.1f..%.1f)", lmin, lmax);
    }
}
static void trapClassicRenders(const juce::File& dir) {
    dir.createDirectory(); BeatsXProcessor p; p.applyKit(kitIndexOf("TRAP CLASSIC"), true); setParam(p, "songmode", 0);
    for (int i = 1; i <= 2; ++i) {
        p.model.toolSeed = i == 1 ? 9 : 14; p.generatePattern(gb::trapc::kStyle); const int sd = p.model.toolSeed;
        const auto b = renderBars(p, 8); const auto f = dir.getChildFile("TRAP_CLASSIC_" + juce::String(i) + ".wav"); writeWav24(b, f);
        std::set<int> v; gb::trapc::hatRolls(p.model.patterns[(size_t) p.currentPattern()], 4, &v); juce::String vs; for (int r : v) vs << gb::trapc::rollName(r) << " ";
        std::printf("  %s: seed %d, %.0f BPM, %s, hat rolls: %s| %.1f s, %.1f LUFS, peak %.2f dBFS, true peak %.2f dBFS\n", f.getFullPathName().toRawUTF8(), sd, p.apvts.getRawParameterValue("bpm")->load(),
                    gb::trapc::families()[(size_t) gb::trapc::family(sd)].name, vs.toRawUTF8(), b.getNumSamples() / 48000.0, lufs(b), juce::Decibels::gainToDecibels(peakOf(b)), juce::Decibels::gainToDecibels(truePeak4x(b)));
    }
}
