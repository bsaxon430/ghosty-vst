// RE-CREATE AS MIDI (DRUMS) benchmark: loops built from our own factory kits at known steps, transcribed, scored per class.
// Included from BeatsTests.cpp (after testRecreate's helpers). Uses only the public a2m API (analyse, roleName, mapRoles, writeDrums),
// so the same code measured the old classifier (docs/research/RECREATE-DRUMS.md, "before") and the new one.
//   corpus     every factory kit x 5 patterns (TRAP 140 with rolls, BOOM BAP 90 swung + ghosts, HOUSE 124, BREAK 100 with ghost snares
//              + tom fill + crash, DRILL 142 with a sliding 808); each kit's first sound of each class, circular (seamless) loops,
//              hit level = class level x (vel / 127)^1.5 (the engine's velocity law), monophonic per class (a re-hit cuts the last one;
//              the open hat is choked by the closed hat)
//   variants   clean | reverb (room, 30 % wet) | noise (hiss -38 dB + crackle) | lo-fi (11 kHz hold, 10 bits, 7 kHz low-pass, drive) |
//              room + drive (a random room: size 0.15..0.7, damping 0.1..0.6, 12..35 % wet, then tanh drive 1..2.5: a mixed, glued loop)
//   scoring    per class: one-to-one matching within +-30 ms, precision / recall / F1; micro-averaged overall; velocity correlation (Pearson,
//              matched pairs, per class, averaged); track accuracy (the hit written on the kit track that holds that sound, at its 1/32 step)
namespace a2mb {
enum Cls { cKick = 0, cSnare, cClap, cHat, cOpen, cTom, cPerc, cCym, cBass, kCls };
static const char* clsName[] { "kick", "snare", "clap", "hat", "open hat", "tom", "perc", "cymbal", "808" };
static int clsOfRoleName(const juce::String& n) {
    static const char* rn[] { "KICK", "SNARE", "CLAP", "HAT", "OPEN HAT", "TOM", "PERC", "CYMBAL", "808" };
    for (int c = 0; c < kCls; ++c) if (n == rn[c]) return c; return -1;
}
static int clsOfSound(const gb::Sound& s) {
    const auto& c = s.category; const auto n = s.name.toLowerCase();
    if (n.contains("shaker") || n.contains("reverse") || n.contains("riser") || n.contains("sweep")) return -1;
    if (c == "Kick") return cKick; if (c == "808") return cBass; if (c == "Snare") return cSnare; if (c == "Clap") return cClap;
    if (c == "Hat" || c == "Pedal Hat") return cHat; if (c == "Open Hat") return cOpen; if (c == "Tom") return cTom; if (c == "Perc") return cPerc;
    if (c == "Cymbal") return cCym; return -1;
}
struct Ev { int cls; double sec; float vel; int track; bool tripRoll = false; };
static const gb::a2m::DrumModel* gModel = nullptr;   // (recreatetrain: the freshly fitted classifier instead of the built-in one)
static gb::a2m::Options drumsOpts() { gb::a2m::Options o; o.mode = std::getenv("A2M_MODE") ? std::atoi(std::getenv("A2M_MODE")) : 0; o.model = gModel; return o; }
struct Kit { int index = 0; juce::String name; std::array<gb::SoundPtr, kCls> s {}; std::array<int, kCls> track {}; };
static std::shared_ptr<gb::DrumLibrary> lib() { static auto L = gb::loadBeatsLibrary(48000.0, false); return L; }
static Kit kitOf(int k) {
    Kit K; K.index = k; const auto& d = gb::allKits()[(size_t) k]; K.name = d.name; K.track.fill(-1);
    for (int t = 0; t < gb::kTracks; ++t) { auto s = lib()->byName(d.sounds[(size_t) t]); if (!s) continue; const int c = clsOfSound(*s); if (c >= 0 && !K.s[(size_t) c]) { K.s[(size_t) c] = s; K.track[(size_t) c] = t; } }
    return K;
}
// a pattern: per class a step string over all bars (16 per bar); x 105, X 127, o 55, g 35, 2 = two 32nds, 3 = three hits in the 16th
struct Pat { const char* name; double bpm; int bars; double swing; double humanMs; std::array<const char*, kCls> lines; bool slide808 = false; };
static const std::vector<Pat>& patterns() {
    static const std::vector<Pat> p {
        { "trap 140", 140, 2, 50, 3, { "x.........x.....x......x..x.....", "", "........x...............x.......", "x.x.x.x.x.x.x2x.x.x.x3x.x.x.x.2.", "..............................x.", "", "", "", "x.........x.....x......x........" } },
        { "boom bap 90 swung", 90, 2, 58, 12, { "X......x.x......X.x....x..x.....", "....x.......x.......x..o....x..o", "", "x.o.x.o.x.o.x.o.x.o.x.o.x.o.x...", "..............................x.", "", "", "", "" } },
        { "house 124", 124, 2, 50, 3, { "x...x...x...x...x...x...x...x...", "", "....x.......x.......x.......x...", "xo.oxo.oxo.oxo.oxo.oxo.oxo.oxo.o", "..x...x...x...x...x...x...x...x.", "", "...x......x........x......x.....", "", "" } },
        { "break 100 ghosts", 100, 2, 50, 10, { "X.x.......x..x..X.x.......x.....", "....x..o.g..x..o....X..o.g..o...", "", "x.x.x.o.x.x.x.o.x.x.x.o.x.x.....", "", "............................xxxx", "", "x...............................", "" } },
        { "drill 142 slide", 142, 2, 50, 4, { "x.........x.......x.......x.....", "", "........x..........x....x.......", "x..x..x.x..x..x.x..x..x.x.3x..x.", "", "", "..........x..............x......", "", "x.....x...x.......x...x.....x..." }, true },
    };
    return p;
}
static float levelOf(int c) { static const float l[] { 1.0f, 0.8f, 0.75f, 0.35f, 0.3f, 0.6f, 0.4f, 0.3f, 0.7f }; return l[c]; }
struct Corpus { gb::a2m::Loop L; std::vector<Ev> truth; double bpm = 120; int bars = 1; };
// plays sound s at `at` (seconds) into x (circular), gain g, cut after `maxLen` seconds (5 ms fade), pitch ratio r0 -> r1 over the first 80 ms
static void place(std::vector<float>& x, double sr, const gb::Sound& s, double at, float g, double maxLen, double r0 = 1, double r1 = 1) {
    const int n = (int) x.size(), len = (int) juce::jmin((double) s.data.size() / juce::jmax(0.25, juce::jmin(r0, r1)), maxLen * sr), fade = (int) (0.005 * sr); const int a = (int) std::lround(at * sr);
    double pos = 0;
    for (int i = 0; i < len; ++i) {
        const double u = i / sr, r = u < 0.08 ? r0 + (r1 - r0) * (u / 0.08) : r1; const int k = (int) pos; if (k + 1 >= (int) s.data.size()) break; const double fr = pos - k;
        float v = (float) (s.data[(size_t) k] * (1 - fr) + s.data[(size_t) k + 1] * fr) * g; if (len - i < fade) v *= (float) (len - i) / (float) fade;
        x[(size_t) (((a + i) % n + n) % n)] += v; pos += r;
    }
}
static Corpus build(const Kit& K, const Pat& P, juce::Random& rnd) {
    Corpus C; C.bpm = P.bpm; C.bars = P.bars; const double sr = 48000, beat = 60.0 / P.bpm, step = beat / 4, len = P.bars * 4 * beat; C.L.sr = sr; C.L.name = "corpus loop"; C.L.x.assign((size_t) std::lround(len * sr), 0.0f);
    struct Hit { int cls; double at; float vel; bool trip; double r0 = 1, r1 = 1; };
    std::vector<Hit> hits; const int steps = P.bars * 16;
    static const double semis[] { 0, 3, -2, 5, 0, 7, -4, 2 };
    int bassN = 0;
    for (int c = 0; c < kCls; ++c) {
        const char* ln = P.lines[(size_t) c]; if (!ln || !*ln || !K.s[(size_t) c]) continue;
        for (int s = 0; s < steps && ln[s]; ++s) {
            const char ch = ln[s]; if (ch == '.') continue;
            double b = s / 4.0; if (s % 4 == 2) b += (P.swing - 50.0) / 50.0 * 0.5;
            const double t0 = b * beat + (P.humanMs > 0 && c != cKick ? (rnd.nextDouble() * 2 - 1) * P.humanMs * 0.001 : 0.0);
            auto add = [&](double t, float v, bool trip) { Hit h { c, juce::jmax(0.0, t), v, trip };
                if (c == cBass) { const double st = semis[bassN % 8]; h.r0 = h.r1 = std::pow(2.0, st / 12.0); if (P.slide808 && (bassN % 3) == 2) h.r0 = std::pow(2.0, semis[(bassN + 7) % 8] / 12.0); ++bassN; }
                hits.push_back(h); };
            if (ch == '2') { add(t0, 95, false); add(t0 + step / 2, 80, false); }
            else if (ch == '3') { add(t0, 95, true); add(t0 + step / 3, 80, true); add(t0 + 2 * step / 3, 70, true); }
            else add(t0, ch == 'X' ? 127.f : ch == 'o' ? 55.f : ch == 'g' ? 35.f : 105.f, false);
        }
    }
    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) { return a.at < b.at; });
    for (size_t i = 0; i < hits.size(); ++i) {
        const auto& h = hits[i]; double next = len + 1.5;   // monophonic per class (+ the closed hat chokes the open hat), circular
        for (size_t j = 1; j <= hits.size(); ++j) { const auto& o = hits[(i + j) % hits.size()]; const double dt = o.at - h.at + (i + j >= hits.size() ? len : 0.0);
            if (dt <= 0) continue; if (o.cls == h.cls || (h.cls == cOpen && o.cls == cHat)) { next = juce::jmin(next, dt); break; } }
        const float g = levelOf(h.cls) * std::pow(h.vel / 127.0f, 1.5f);
        place(C.L.x, sr, *K.s[(size_t) h.cls], h.at, g, juce::jmin(next, 2.0), h.r0, h.r1);
        C.truth.push_back({ h.cls, h.at, h.vel / 127.0f, K.track[(size_t) h.cls], h.trip });
    }
    float pk = 1e-6f; for (auto v : C.L.x) pk = juce::jmax(pk, std::abs(v)); for (auto& v : C.L.x) v *= 0.9f / pk;
    return C;
}
// processing variants
static void process(Corpus& C, int variant, juce::Random& rnd) {
    auto& x = C.L.x; const double sr = C.L.sr;
    if (variant == 1) { juce::Reverb rv; juce::Reverb::Parameters p; p.roomSize = 0.6f; p.damping = 0.4f; p.wetLevel = 0.3f; p.dryLevel = 1.0f; p.width = 0; rv.setParameters(p); rv.setSampleRate(sr);
        std::vector<float> pre(x); for (int pass = 0; pass < 2; ++pass) { auto y = pre; rv.processMono(y.data(), (int) y.size()); if (pass == 1) x = y; } }   // (twice: the tail wraps like a loop's)
    if (variant == 2) { float pk = 0; for (auto v : x) pk = juce::jmax(pk, std::abs(v)); float lp = 0;
        for (auto& v : x) { lp += 0.15f * ((rnd.nextFloat() * 2 - 1) - lp); v += pk * 0.0126f * (rnd.nextFloat() * 2 - 1) * 0.5f + pk * 0.02f * lp; if (rnd.nextInt(6000) == 0) v += pk * 0.15f * (rnd.nextFloat() * 2 - 1); } }
    if (variant == 3) { const int hold = (int) std::lround(sr / 11025.0); float held = 0; auto L1 = gb::a2m::Biquad::make(false, 7000, sr), L2 = L1;
        for (size_t i = 0; i < x.size(); ++i) { if (i % (size_t) hold == 0) held = std::round(x[i] * 512.0f) / 512.0f; x[i] = std::tanh(2.0f * L2.run(L1.run(held))) / std::tanh(2.0f); } }
    if (variant == 4) { juce::Reverb rv; juce::Reverb::Parameters p; p.roomSize = 0.15f + 0.55f * rnd.nextFloat(); p.damping = 0.1f + 0.5f * rnd.nextFloat(); p.wetLevel = 0.12f + 0.23f * rnd.nextFloat(); p.dryLevel = 1.0f; p.width = 0;
        rv.setParameters(p); rv.setSampleRate(sr); const float drive = 1.0f + 1.5f * rnd.nextFloat();
        std::vector<float> pre(x); for (int pass = 0; pass < 2; ++pass) { auto y = pre; rv.processMono(y.data(), (int) y.size()); if (pass == 1) x = y; }
        float pk = 1e-6f; for (auto v : x) pk = juce::jmax(pk, std::abs(v)); for (auto& v : x) v = std::tanh(drive * v / pk) / std::tanh(drive); }
    float pk = 1e-6f; for (auto v : x) pk = juce::jmax(pk, std::abs(v)); for (auto& v : x) v *= 0.9f / pk;
}
constexpr int kVariants = 5;
static const char* variantName[] { "clean", "reverb", "noise", "lo-fi", "room + drive" };

struct Score {
    std::array<int, kCls> tp {}, fp {}, fn {};
    std::array<std::array<int, kCls + 1>, kCls + 1> conf {};   // [truth or NONE][detected or MISS]
    std::array<std::vector<std::pair<double, double>>, kCls> velPairs;
    int trackOk = 0, trackN = 0, tempoOk = 0, loops = 0, autoPitched = 0;
    void add(const Score& o) { for (int c = 0; c < kCls; ++c) { tp[(size_t) c] += o.tp[(size_t) c]; fp[(size_t) c] += o.fp[(size_t) c]; fn[(size_t) c] += o.fn[(size_t) c]; velPairs[(size_t) c].insert(velPairs[(size_t) c].end(), o.velPairs[(size_t) c].begin(), o.velPairs[(size_t) c].end()); }
        for (int a = 0; a <= kCls; ++a) for (int b = 0; b <= kCls; ++b) conf[(size_t) a][(size_t) b] += o.conf[(size_t) a][(size_t) b]; trackOk += o.trackOk; autoPitched += o.autoPitched; trackN += o.trackN; tempoOk += o.tempoOk; loops += o.loops; }
    double f1(int c) const { const double p = tp[(size_t) c] + fp[(size_t) c] > 0 ? (double) tp[(size_t) c] / (tp[(size_t) c] + fp[(size_t) c]) : 0, r = tp[(size_t) c] + fn[(size_t) c] > 0 ? (double) tp[(size_t) c] / (tp[(size_t) c] + fn[(size_t) c]) : 0; return p + r > 0 ? 2 * p * r / (p + r) : 0; }
    double prec(int c) const { return tp[(size_t) c] + fp[(size_t) c] > 0 ? (double) tp[(size_t) c] / (tp[(size_t) c] + fp[(size_t) c]) : 0; }
    double rec(int c) const { return tp[(size_t) c] + fn[(size_t) c] > 0 ? (double) tp[(size_t) c] / (tp[(size_t) c] + fn[(size_t) c]) : 0; }
    double overall() const { int T = 0, F = 0, N = 0; for (int c = 0; c < kCls; ++c) { T += tp[(size_t) c]; F += fp[(size_t) c]; N += fn[(size_t) c]; } return T > 0 ? 2.0 * T / (2.0 * T + F + N) : 0; }
    double velCorr(int c) const { const auto& v = velPairs[(size_t) c]; if (v.size() < 4) return -2; double ma = 0, mb = 0; for (auto& p : v) { ma += p.first; mb += p.second; } ma /= v.size(); mb /= v.size();
        double ab = 0, aa = 0, bb = 0; for (auto& p : v) { ab += (p.first - ma) * (p.second - mb); aa += (p.first - ma) * (p.first - ma); bb += (p.second - mb) * (p.second - mb); } return aa > 1e-9 && bb > 1e-12 ? ab / std::sqrt(aa * bb) : -2; }
    double velCorrMean() const { double s = 0; int k = 0; for (int c = 0; c < kCls; ++c) { const double v = velCorr(c); if (v > -1.5) { s += v; ++k; } } return k ? s / k : 0; }
    int truthN(int c) const { return tp[(size_t) c] + fn[(size_t) c]; }
};
// one loop: analyse + score (+ the pattern written onto the kit's tracks, for track accuracy)
static Score scoreLoop(const Corpus& C, const gb::a2m::Result& R, const std::array<int, gb::a2m::kNumRoles>& map, const std::array<gb::Track, gb::kTracks>& tracks, gb::Pattern* outP = nullptr) {
    Score S; S.loops = 1; S.tempoOk = std::abs(R.bpm - C.bpm) / C.bpm < 0.01 ? 1 : 0;
    struct D { int cls; double sec; float vel; bool used = false; };
    std::vector<D> det; for (auto& h : R.hits) { const int c = clsOfRoleName(gb::a2m::roleName(h.role)); if (c >= 0) det.push_back({ c, h.sec, h.vel }); }
    std::vector<int> truthMatch(C.truth.size(), -1);
    for (int c = 0; c < kCls; ++c) {   // greedy one-to-one by the smallest time difference
        struct Pr { double d; int t, k; }; std::vector<Pr> prs;
        for (int t = 0; t < (int) C.truth.size(); ++t) if (C.truth[(size_t) t].cls == c) for (int k = 0; k < (int) det.size(); ++k) if (det[(size_t) k].cls == c) { const double d = std::abs(det[(size_t) k].sec - C.truth[(size_t) t].sec); if (d <= 0.030) prs.push_back({ d, t, k }); }
        std::sort(prs.begin(), prs.end(), [](const Pr& a, const Pr& b) { return a.d < b.d; });
        for (auto& p : prs) if (truthMatch[(size_t) p.t] < 0 && !det[(size_t) p.k].used) { truthMatch[(size_t) p.t] = p.k; det[(size_t) p.k].used = true; }
    }
    for (int t = 0; t < (int) C.truth.size(); ++t) { const auto& e = C.truth[(size_t) t];
        if (truthMatch[(size_t) t] >= 0) { ++S.tp[(size_t) e.cls]; S.conf[(size_t) e.cls][(size_t) e.cls]++; S.velPairs[(size_t) e.cls].push_back({ e.vel, det[(size_t) truthMatch[(size_t) t]].vel }); continue; }
        ++S.fn[(size_t) e.cls]; int best = -1; double bd = 0.030;
        for (int k = 0; k < (int) det.size(); ++k) if (!det[(size_t) k].used) { const double d = std::abs(det[(size_t) k].sec - e.sec); if (d <= bd) { bd = d; best = k; } }
        if (best >= 0) { S.conf[(size_t) e.cls][(size_t) det[(size_t) best].cls]++; det[(size_t) best].used = true; ++S.fp[(size_t) det[(size_t) best].cls]; } else S.conf[(size_t) e.cls][kCls]++;
    }
    for (auto& d : det) if (!d.used) { ++S.fp[(size_t) d.cls]; S.conf[kCls][(size_t) d.cls]++; }
    // track accuracy: the kit track that holds the sound, at the hit's own 1/32 step
    gb::Pattern P; gb::a2m::WriteOpts w; w.res = 2; gb::a2m::writeDrums(P, tracks, R, map, w);
    const double beat = 60.0 / R.bpm;
    for (auto& e : C.truth) { if (e.tripRoll || e.track < 0) continue; ++S.trackN; long s = std::lround((e.sec - R.origin) / beat / 0.125); if (s >= P.length) s -= P.length; if (s >= 0 && s < P.length && P.steps[(size_t) e.track][(size_t) s].vel) ++S.trackOk; }
    if (outP) *outP = P;
    return S;
}
static void printScore(const char* title, const Score& S, bool confusion) {
    std::printf("  %s: %d loops (AUTO mode called %d of them PITCHED; scored in DRUMS mode), tempo right %d / %d, overall F1 %.3f, velocity r %.2f, track accuracy %.1f %%\n", title, S.loops, S.autoPitched, S.tempoOk, S.loops, S.overall(), S.velCorrMean(), S.trackN ? 100.0 * S.trackOk / S.trackN : 0.0);
    std::printf("    %-9s %6s %6s %6s %6s %6s\n", "class", "truth", "P", "R", "F1", "vel r");
    for (int c = 0; c < kCls; ++c) if (S.truthN(c) || S.fp[(size_t) c]) std::printf("    %-9s %6d %6.3f %6.3f %6.3f %6.2f\n", clsName[c], S.truthN(c), S.prec(c), S.rec(c), S.f1(c), S.velCorr(c));
    if (!confusion) return;
    std::printf("    confusion (rows truth, cols detected):\n    %-9s", ""); for (int c = 0; c <= kCls; ++c) std::printf(" %6.6s", c < kCls ? clsName[c] : "MISS"); std::printf("\n");
    for (int a = 0; a <= kCls; ++a) { int any = 0; for (int b = 0; b <= kCls; ++b) any += S.conf[(size_t) a][(size_t) b]; if (!any) continue;
        std::printf("    %-9s", a < kCls ? clsName[a] : "(none)"); for (int b = 0; b <= kCls; ++b) std::printf(" %6d", S.conf[(size_t) a][(size_t) b]); std::printf("\n"); }
}
// the written pattern played back with the kit's own sounds (same mixer as the corpus), for listening
static std::vector<float> playBack(const gb::Pattern& P, const Kit& K, const gb::a2m::Result& R, double sr, int nSamples) {
    std::vector<float> x((size_t) nSamples, 0.0f); const double sb = gb::stepBeats(P.res) * 60.0 / R.bpm;
    for (int t = 0; t < gb::kTracks; ++t) { int c = -1; for (int k = 0; k < kCls; ++k) if (K.track[(size_t) k] == t) c = k; if (c < 0) continue;
        std::vector<std::pair<double, float>> on; for (int s = 0; s < P.length; ++s) { const auto& st = P.steps[(size_t) t][(size_t) s]; if (st.vel) on.push_back({ R.origin + (s + st.nudge / 100.0) * sb, st.vel / 127.0f }); }
        for (size_t i = 0; i < on.size(); ++i) { const double nx = i + 1 < on.size() ? on[i + 1].first - on[i].first : 2.0; place(x, sr, *K.s[(size_t) c], on[i].first, levelOf(c) * std::pow(on[i].second, 1.5f), juce::jmin(nx, 2.0)); } }
    float pk = 1e-6f; for (auto v : x) pk = juce::jmax(pk, std::abs(v)); for (auto& v : x) v *= 0.9f / pk; return x;
}
static void wav(const std::vector<float>& x, double sr, const juce::File& f) {
    juce::AudioBuffer<float> b(1, (int) x.size()); b.copyFrom(0, 0, x.data(), (int) x.size()); f.deleteFile(); juce::WavAudioFormat w;
    std::unique_ptr<juce::AudioFormatWriter> wr(w.createWriterFor(new juce::FileOutputStream(f), sr, 1, 24, {}, 0)); if (wr) wr->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
}
// the hard cases: { kick + hat on every 8th }, { snare + clap layered }, { hats on 16ths over a long 808 }
static Corpus hardCase(int which, const Kit& K) {
    Pat P { "hard", 100, 2, 50, 0, {} };
    static const char* kh[] { "x...x...x...x...x...x...x...x...", "", "", "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.", "", "", "", "", "" };
    static const char* sc[] { "x.......x.......x.......x.......", "....x.......x.......x.......x...", "....x.......x.......x.......x...", "", "", "", "", "", "" };
    static const char* hb[] { "", "", "", "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", "", "", "", "", "x...............x..............." };
    const char* const* L = which == 0 ? kh : which == 1 ? sc : hb; for (int c = 0; c < kCls; ++c) P.lines[(size_t) c] = L[c];
    juce::Random r(11); return build(K, P, r);
}
// the library's 8 factory breaks (DrumSynth.h BREAKS: our own drums through a different mixer, swing, ghost notes at 0.32, 2 ms jitter,
// saturation + room): not in the training data. Their patterns are copied here from DrumSynth.h (kick, snare, ghost snare, hat, open hat)
struct BreakTruth { const char* name; double bpm, swing; const char *kick, *snare, *ghost, *hat, *open; };
static const BreakTruth kBreaks[] {
    { "Break Ghost Funk", 104, 0.12, "x.........x.....x.x.......x.....", "....x.......x.......x.......x...", ".......o.o....o........o.o..o..o", "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.", "..............x...............x." },
    { "Break Rolling", 172, 0.0, "x.........x.......x.......x.....", "....x.......x.......x..x....x...", "..o....o.o...o.o..o....o..o..o.o", "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.", "......................x........." },
    { "Break Dusty Swing", 90, 0.3, "x.......x.x.....x.....x...x.....", "....x.......x.......x.......x...", "...o......o..o.....o..o....o..o.", "x.xxx.x.x.xxx.x.x.xxx.x.x.xxx.x.", "..............x...............x." },
    { "Break Tight Machine", 128, 0.0, "x.....x...x.....x.....x...x..x..", "....x.......x.......x.......x...", "", "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx", "..x...x...x...x...x...x...x...x." },
    { "Break Shuffle", 120, 0.4, "x.....x...x.....x.....x.x.......", "....x.......x.......x.......x...", ".o.....o.o....o..o.....o.o..o..o", "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.", "" },
    { "Break Halftime", 140, 0.0, "x.....x...x.......x.......x.....", "........x...............x.......", "..o....o.....o.o...o.......o..o.", "x.x.x.x.x.x.x.x.x.x.x.x.x.x.xxxx", "..............x................." },
    { "Break Glitch Hop", 110, 0.15, "x..x......x..x..x.....x...x.....", "....x.......x.......x.......x.x.", ".......o.......o...o...o......o.", "x.x.x.x.x.x.x.x.x.xxx.x.x.x.xxxx", "......x...............x........." },
    { "Break Breakbeat", 135, 0.05, "x.x.......x.......x.x.....x.....", "....x.......x.......x.......x...", ".......o...o.o.........o..o..o.o", "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.", "..........x...............x....." },
};
static Corpus factoryBreak(int i) {
    const auto& B = kBreaks[i]; Corpus C; C.bpm = B.bpm; C.bars = 2; auto s = lib()->byName(B.name); if (!s) return C;
    C.L.sr = s->rate; C.L.name = "factory break"; C.L.x.assign(s->data.begin(), s->data.begin() + (long) std::min<size_t>(s->data.size(), (size_t) std::lround(8 * 60.0 / B.bpm * s->rate)));
    const double stepT = 60.0 / B.bpm / 4.0;
    auto add = [&](const char* pat, int cls, bool ghost) { if (!pat) return; for (int k = 0; k < 32 && pat[k]; ++k) if (pat[k] != '.') C.truth.push_back({ cls, k * stepT + ((k & 1) ? B.swing * stepT * 0.5 : 0.0), ghost ? 0.32f : 0.8f, -1 }); };
    add(B.kick, cKick, false); add(B.snare, cSnare, false); add(B.ghost, cSnare, true); add(B.hat, cHat, false); add(B.open, cOpen, false);
    std::sort(C.truth.begin(), C.truth.end(), [](const Ev& a, const Ev& b) { return a.sec < b.sec; });
    return C;
}
static Score runBreaks(bool verbose = false) {
    Score T; BeatsXProcessor p; p.applyKit(0, true); const auto map = gb::a2m::mapRoles(p.model.tracks, [&](const juce::String& n) { return p.categoryOf(n); });
    for (int i = 0; i < 8; ++i) { auto C = factoryBreak(i); if (C.truth.empty()) continue; const auto R = gb::a2m::analyse(C.L, drumsOpts()); const auto S = scoreLoop(C, R, map, p.model.tracks); T.add(S);
        if (verbose) std::printf("    %-20s %.1f BPM (truth %.0f, from %s), F1 %.3f\n", kBreaks[i].name, R.bpm, C.bpm, R.bpmFrom.toRawUTF8(), S.overall()); }
    return T;
}
// run the whole corpus; returns per-variant scores; renders a few loops when `renders` is a directory
static std::array<Score, kVariants> runCorpus(int kitStride, int kitOffset, int variants, const juce::File& renders, Score* perPattern = nullptr, std::array<Score, 3>* hard = nullptr, double* msOut = nullptr) {
    std::array<Score, kVariants> out {}; BeatsXProcessor p; double ms = 0; int runs = 0;
    for (int k = kitOffset; k < (int) gb::allKits().size(); k += kitStride) {
        const auto K = kitOf(k); p.applyKit(k, true); const auto map = gb::a2m::mapRoles(p.model.tracks, [&](const juce::String& n) { return p.categoryOf(n); });
        for (int pi = 0; pi < (int) patterns().size(); ++pi) for (int v = 0; v < variants; ++v) {
            juce::Random rnd(1000 + k * 7 + pi * 13 + v); auto C = build(K, patterns()[(size_t) pi], rnd); if (C.truth.empty()) continue; process(C, v, rnd);
            const auto t0 = juce::Time::getMillisecondCounterHiRes(); const auto R = gb::a2m::analyse(C.L, drumsOpts()); ms += juce::Time::getMillisecondCounterHiRes() - t0; ++runs;
            gb::Pattern P; auto S = scoreLoop(C, R, map, p.model.tracks, &P); if (v == 0) { auto oa = drumsOpts(); oa.mode = -1; S.autoPitched = gb::a2m::analyse(C.L, oa).pitched ? 1 : 0; if (S.autoPitched && std::getenv("A2M_AUTO")) std::printf("  AUTO -> PITCHED: kit %d (%s) pattern %d\n", k, K.name.toRawUTF8(), pi); } out[(size_t) v].add(S); if (v == 0 && perPattern) perPattern[pi].add(S);
            if (renders.isDirectory() && v == 0 && (k % 8) == 0) {
                const auto base = juce::String(K.name).replaceCharacters(" /&", "___") + "_" + juce::String(patterns()[(size_t) pi].name).replaceCharacters(" ", "_");
                wav(C.L.x, C.L.sr, renders.getChildFile(base + "_A_original.wav")); wav(playBack(P, K, R, C.L.sr, (int) C.L.x.size()), C.L.sr, renders.getChildFile(base + "_B_recreated.wav"));
            }
        }
        if (hard) for (int h = 0; h < 3; ++h) { auto C = hardCase(h, K); if (C.truth.empty()) continue; const auto R = gb::a2m::analyse(C.L, drumsOpts()); (*hard)[(size_t) h].add(scoreLoop(C, R, map, p.model.tracks)); }
    }
    if (msOut) *msOut = runs ? ms / runs : 0;
    return out;
}
// (diagnostic) one loop: truth vs detected, in time order. pattern -1..-3 = the hard cases
static void debugOne(int k, int pi, int v) {
    const auto K = kitOf(k); BeatsXProcessor p; p.applyKit(k, true); const auto map = gb::a2m::mapRoles(p.model.tracks, [&](const juce::String& n) { return p.categoryOf(n); });
    juce::Random rnd(1000 + k * 7 + pi * 13 + v); auto C = pi <= -10 ? factoryBreak(-pi - 10) : pi < 0 ? hardCase(-pi - 1, K) : build(K, patterns()[(size_t) pi], rnd); if (pi > -10) process(C, v, rnd);
    std::printf("kit %s:", K.name.toRawUTF8()); for (int c = 0; c < kCls; ++c) if (K.s[(size_t) c]) std::printf(" %s=%s", clsName[c], K.s[(size_t) c]->name.toRawUTF8()); std::printf("\n");
    const auto R = gb::a2m::analyse(C.L, drumsOpts()); const auto S = scoreLoop(C, R, map, p.model.tracks);
    std::printf("bpm %.2f (%s) truth %.1f, origin %.3f, F1 %.3f\n", R.bpm, R.bpmFrom.toRawUTF8(), C.bpm, R.origin, S.overall());
    struct L { double t; juce::String s; }; std::vector<L> lines;
    for (auto& e : C.truth) lines.push_back({ e.sec, "  T " + juce::String(e.sec, 3) + " " + clsName[e.cls] + " v" + juce::String(e.vel, 2) });
    for (auto& h : R.hits) lines.push_back({ h.sec + 1e-6, "        D " + juce::String(h.sec, 3) + " " + gb::a2m::roleName(h.role) + " v" + juce::String(h.vel, 2) });
    std::sort(lines.begin(), lines.end(), [](const L& a, const L& b) { return a.t < b.t; }); for (auto& l : lines) std::printf("%s\n", l.s.toRawUTF8());
}
// (diagnostic) every event of the corpus with its features and the truth classes within 25 ms -> CSV (for choosing the rules)
static void dump(const juce::File& out, int variants) {
    juce::String csv = "var,kit,pat,t,dt,truth,trvel,gap,bursts,subHz,midHz,hiDrop";
    for (int b = 0; b < gb::a2m::kBands; ++b) csv << ",d" << b << ",n" << b << ",r" << b << ",t" << b << ",p" << b; csv << "\n";
    int missed = 0, total = 0; std::array<int, kCls> missedBy {};
    for (int k = 0; k < (int) gb::allKits().size(); ++k) { const auto K = kitOf(k);
        for (int pi = -3; pi < (int) patterns().size(); ++pi) for (int v = 0; v < (pi < 0 ? 1 : variants); ++v) {
            juce::Random rnd(1000 + k * 7 + pi * 13 + v); auto C = pi < 0 ? hardCase(-pi - 1, K) : build(K, patterns()[(size_t) pi], rnd); if (C.truth.empty()) continue; process(C, v, rnd);
            const auto R = gb::a2m::analyse(C.L, drumsOpts());
            std::array<float, gb::a2m::kBands> mx {}; for (auto& e : R.events) for (int b = 0; b < gb::a2m::kBands; ++b) mx[(size_t) b] = juce::jmax(mx[(size_t) b], e.delta[(size_t) b]);
            for (auto& tr : C.truth) { ++total; bool f = false; for (auto& e : R.events) if (std::abs(e.sec - tr.sec) <= 0.03) f = true; if (!f) { ++missed; ++missedBy[(size_t) tr.cls]; if (std::getenv("A2M_MISSED")) std::printf("  missed kit %d pat %d var %d: %s at %.3f\n", k, pi, v, clsName[tr.cls], tr.sec); } }
            for (auto& e : R.events) { int lab = 0; double dt = 1; float tv = 0; for (auto& tr : C.truth) { const double d = e.sec - tr.sec; if (std::abs(d) <= 0.025) { lab |= 1 << tr.cls; tv = juce::jmax(tv, tr.vel); } if (std::abs(d) < std::abs(dt)) dt = d; }
                csv << v << "," << k << "," << pi << "," << juce::String(e.sec, 4) << "," << juce::String(dt * 1000, 2) << "," << lab << "," << juce::String(tv, 3) << "," << juce::String(e.gap, 4) << "," << e.bursts << "," << juce::String(e.subHz, 1) << "," << juce::String(e.midHz, 1) << "," << juce::String(e.hiDrop, 2);
                for (int b = 0; b < gb::a2m::kBands; ++b) csv << "," << juce::String(10 * std::log10(e.delta[(size_t) b] + 1e-20), 2) << "," << juce::String(e.delta[(size_t) b] / juce::jmax(1e-20f, mx[(size_t) b]), 4) << "," << juce::String(e.rise[(size_t) b], 2) << "," << juce::String(e.t12[(size_t) b], 4) << "," << juce::String(10 * std::log10(e.post[(size_t) b] + 1e-20), 2);
                csv << "\n"; }
        } }
    out.replaceWithText(csv); std::printf("truth hits with no event within 30 ms: %d / %d:", missed, total); for (int c = 0; c < kCls; ++c) std::printf(" %s %d", clsName[c], missedBy[(size_t) c]); std::printf("\n");
}
static void bench(const juce::File& renders, int kitStride, int kitOffset) {
    section("RE-CREATE DRUMS benchmark (factory kits x 5 patterns x 5 variants + the 8 factory breaks)");
    if (renders != juce::File()) renders.createDirectory();
    std::array<Score, 5> perPat {}; std::array<Score, 3> hard {}; double ms = 0;
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    const auto res = runCorpus(kitStride, kitOffset, kVariants, renders, perPat.data(), &hard, &ms);
    for (int v = 0; v < kVariants; ++v) printScore(variantName[v], res[(size_t) v], v == 0);
    for (int pi = 0; pi < 5; ++pi) std::printf("  clean, %-18s overall F1 %.3f  kick %.2f snare %.2f clap %.2f hat %.2f open %.2f tom %.2f perc %.2f cym %.2f 808 %.2f | track %.1f %% | tempo %d/%d\n", patterns()[(size_t) pi].name, perPat[(size_t) pi].overall(),
        perPat[(size_t) pi].f1(0), perPat[(size_t) pi].f1(1), perPat[(size_t) pi].f1(2), perPat[(size_t) pi].f1(3), perPat[(size_t) pi].f1(4), perPat[(size_t) pi].f1(5), perPat[(size_t) pi].f1(6), perPat[(size_t) pi].f1(7), perPat[(size_t) pi].f1(8),
        perPat[(size_t) pi].trackN ? 100.0 * perPat[(size_t) pi].trackOk / perPat[(size_t) pi].trackN : 0.0, perPat[(size_t) pi].tempoOk, perPat[(size_t) pi].loops);
    static const char* hn[] { "kick + hat together", "snare + clap layered", "hats over a long 808" };
    for (int h = 0; h < 3; ++h) printScore(hn[h], hard[(size_t) h], false);
    if (kitOffset == 0) printScore("factory breaks (not trained on: another mixer, saturation + room)", runBreaks(true), true);
    std::printf("  %.0f ms per loop analysis on average; total %.1f s\n", ms, (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0);
}
// ---- fitting the classifier (logistic, per drum, L2; Adam on the full batch) on the corpus; prints the C++ block for AudioToMidi.h
template <size_t N> static void fitLogistic(const std::vector<std::array<float, N>>& X, const std::vector<uint32_t>& Y, int r, std::array<float, N + 1>& w, float lambda, int iters = 500) {
    const size_t n = X.size(); double pos = 0; for (auto y : Y) pos += (y >> r) & 1u; const double wp = juce::jlimit(1.0, 8.0, std::sqrt((n - pos + 1) / (pos + 1)));
    std::array<double, N + 1> m {}, v {}; w.fill(0); const double lr = 0.05, b1 = 0.9, b2 = 0.999;
    for (int it = 1; it <= iters; ++it) {
        std::array<double, N + 1> g {}; double W = 0;
        for (size_t i = 0; i < n; ++i) { const bool y = (Y[i] >> r) & 1u; const double wt = y ? wp : 1.0; const double p = gb::a2m::sigmoid(gb::a2m::dot<N>(w, X[i])); const double e = (p - (y ? 1.0 : 0.0)) * wt; W += wt;
            for (size_t d = 0; d < N; ++d) g[d] += e * X[i][d]; g[N] += e; }
        for (size_t d = 0; d <= N; ++d) { double gd = g[d] / W + (d < N ? lambda * w[d] : 0.0); m[d] = b1 * m[d] + (1 - b1) * gd; v[d] = b2 * v[d] + (1 - b2) * gd * gd;
            const double mh = m[d] / (1 - std::pow(b1, it)), vh = v[d] / (1 - std::pow(b2, it)); w[d] -= (float) (lr * mh / (std::sqrt(vh) + 1e-8)); }
    }
}
// stage 1: one hidden layer (tanh), sigmoid outputs, weighted cross-entropy (positives up-weighted per drum), L2, Adam on mini-batches
static void fitMlp(const std::vector<std::array<float, gb::a2m::kFeat>>& X, const std::vector<uint32_t>& Y, gb::a2m::DrumModel& M, int epochs = 80) {
    using namespace gb::a2m; constexpr int F = kFeat, H = kHidden, K = kNumRoles; const size_t n = X.size();
    std::array<double, K> wp {}; for (int r = 0; r < K; ++r) { double pos = 0; for (auto y : Y) pos += (y >> r) & 1u; wp[(size_t) r] = juce::jlimit(1.0, 8.0, std::sqrt((n - pos + 1) / (pos + 1))); }
    juce::Random R(12345); for (auto& row : M.h1) { for (auto& v : row) v = (R.nextFloat() * 2 - 1) * std::sqrt(6.0f / (F + H)); row[F] = 0; }
    for (auto& row : M.o1) { for (auto& v : row) v = (R.nextFloat() * 2 - 1) * std::sqrt(6.0f / (H + K)); row[H] = 0; }
    const size_t P = (size_t) H * (F + 1) + (size_t) K * (H + 1); std::vector<double> m(P, 0.0), v(P, 0.0), g(P, 0.0);
    auto param = [&](size_t i) -> float& { if (i < (size_t) H * (F + 1)) return M.h1[i / (F + 1)][i % (F + 1)]; i -= (size_t) H * (F + 1); return M.o1[i / (H + 1)][i % (H + 1)]; };
    std::vector<size_t> idx(n); for (size_t i = 0; i < n; ++i) idx[i] = i; const size_t batch = 256; const double lr = 0.003, lambda = std::getenv("A2M_L2") ? std::atof(std::getenv("A2M_L2")) : 3e-3; int step = 0;
    if (std::getenv("A2M_EPOCHS")) epochs = std::atoi(std::getenv("A2M_EPOCHS"));
    for (int ep = 0; ep < epochs; ++ep) {
        for (size_t i = n - 1; i > 0; --i) std::swap(idx[i], idx[(size_t) R.nextInt((int) i + 1)]);
        for (size_t b0 = 0; b0 < n; b0 += batch) {
            std::fill(g.begin(), g.end(), 0.0); double W = 0;
            for (size_t bi = b0; bi < juce::jmin(n, b0 + batch); ++bi) { const auto& x = X[idx[bi]]; const uint32_t y = Y[idx[bi]];
                std::array<float, H> h {}; for (int j = 0; j < H; ++j) h[(size_t) j] = std::tanh(dot<F>(M.h1[(size_t) j], x));
                std::array<double, H> dh {};
                for (int r = 0; r < K; ++r) { const bool yy = (y >> r) & 1u; const double wt = yy ? wp[(size_t) r] : 1.0; const double p = sigmoid(dot<H>(M.o1[(size_t) r], h)); const double d = (p - (yy ? 1.0 : 0.0)) * wt;
                    const size_t base = (size_t) H * (F + 1) + (size_t) r * (H + 1); for (int j = 0; j < H; ++j) { g[base + (size_t) j] += d * h[(size_t) j]; dh[(size_t) j] += d * M.o1[(size_t) r][(size_t) j]; } g[base + H] += d; }
                for (int j = 0; j < H; ++j) { const double dz = dh[(size_t) j] * (1.0 - h[(size_t) j] * h[(size_t) j]); const size_t base = (size_t) j * (F + 1); for (int f = 0; f < F; ++f) g[base + (size_t) f] += dz * x[(size_t) f]; g[base + F] += dz; }
                W += 1.0; }
            ++step;
            for (size_t i = 0; i < P; ++i) { float& w = param(i); const double gd = g[i] / W + lambda * w; m[i] = 0.9 * m[i] + 0.1 * gd; v[i] = 0.999 * v[i] + 0.001 * gd * gd;
                const double mh = m[i] / (1 - std::pow(0.9, step)), vh = v[i] / (1 - std::pow(0.999, step)); w -= (float) (lr * mh / (std::sqrt(vh) + 1e-8)); }
        }
    }
}
struct LoopData { std::vector<gb::a2m::DrumEvent> ev; gb::a2m::Result R; std::vector<uint32_t> y; };
static std::vector<LoopData> collect(int stride, int offset, int variants) {
    std::vector<LoopData> out;
    for (int k = offset; k < (int) gb::allKits().size(); k += stride) { const auto K = kitOf(k);
        for (int pi = -3; pi < (int) patterns().size(); ++pi) for (int v = 0; v < (pi < 0 ? 1 : variants); ++v) {
            juce::Random rnd(1000 + k * 7 + pi * 13 + v); auto C = pi < 0 ? hardCase(-pi - 1, K) : build(K, patterns()[(size_t) pi], rnd); if (C.truth.empty()) continue; process(C, v, rnd);
            LoopData d; d.R = gb::a2m::analyse(C.L, drumsOpts()); d.ev = d.R.events;
            for (auto& e : d.ev) { uint32_t m = 0; for (auto& tr : C.truth) if (std::abs(e.sec - tr.sec) <= 0.025) m |= 1u << tr.cls; d.y.push_back(m); }
            out.push_back(std::move(d));
        } }
    return out;
}
static gb::a2m::DrumModel train(const std::vector<LoopData>& data) {
    using namespace gb::a2m; DrumModel M; M.thr.fill(0.5f);
    std::vector<std::array<float, kFeat>> X; std::vector<uint32_t> Y;
    for (auto& d : data) { DrumStages S; runStages(d.ev, d.R, M, S, false); X.insert(X.end(), S.X.begin(), S.X.end()); Y.insert(Y.end(), d.y.begin(), d.y.end()); }
    std::printf("  stage 1: %d events\n", (int) X.size());
    fitMlp(X, Y, M);
    std::vector<std::array<float, kCtx>> C; std::vector<std::array<float, kNumRoles>> P1;
    for (auto& d : data) { DrumStages S; runStages(d.ev, d.R, M, S, false); C.insert(C.end(), S.C.begin(), S.C.end()); }
    for (int r = 0; r < kNumRoles; ++r) fitLogistic<kCtx>(C, Y, r, M.w2[(size_t) r], 1e-3f);
    // thresholds: the best event-level F1 per drum on this data
    for (int r = 0; r < kNumRoles; ++r) { double bestF = -1; float bestT = 0.5f;
        for (float t = 0.15f; t <= 0.851f; t += 0.025f) { int tp = 0, fp = 0, fn = 0; for (size_t i = 0; i < C.size(); ++i) { const bool y = (Y[i] >> r) & 1u, p = sigmoid(dot<kCtx>(M.w2[(size_t) r], C[i])) >= t; tp += y && p; fp += !y && p; fn += y && !p; }
            const double f = tp ? 2.0 * tp / (2.0 * tp + fp + fn) : 0; if (f > bestF) { bestF = f; bestT = t; } }
        M.thr[(size_t) r] = bestT; std::printf("  %-8s threshold %.3f, event F1 on the training data %.3f\n", roleName(r), bestT, bestF); }
    return M;
}
static juce::String modelCode(const gb::a2m::DrumModel& M) {
    juce::String s; s << "    static const DrumModel M = [] { DrumModel m;\n";
    auto arr = [&](const char* nm, auto& W) { s << "        m." << nm << " = {{\n"; for (auto& row : W) { s << "            {{ "; for (size_t i = 0; i < row.size(); ++i) s << juce::String(row[i], 4) << "f" << (i + 1 < row.size() ? ", " : ""); s << " }},\n"; } s << "        }};\n"; };
    arr("h1", M.h1); arr("o1", M.o1); arr("w2", M.w2); s << "        m.thr = {{ "; for (size_t i = 0; i < M.thr.size(); ++i) s << juce::String(M.thr[i], 3) << "f" << (i + 1 < M.thr.size() ? ", " : ""); s << " }};\n        return m; }();\n";
    return s;
}
// recreatetrain [split] [out]: split = fit on the even kits, score the odd ones (held out) + all; otherwise fit on every kit and write the block
static void trainMode(bool split, const juce::File& out) {
    static gb::a2m::DrumModel M;
    gModel = nullptr; const auto data = collect(split ? 2 : 1, 0, kVariants); M = train(data); gModel = &M;
    if (out != juce::File()) { out.replaceWithText(modelCode(M)); std::printf("  model written to %s\n", out.getFullPathName().toRawUTF8()); }
    if (split) { std::printf("  HELD-OUT kits (odd indices):\n"); bench({}, 2, 1); std::printf("  TRAINING kits (even indices):\n"); bench({}, 2, 0); }
    gModel = nullptr;
}
} // namespace a2mb
