// GhostSynth (see GhostSynth.h). Built on Open303 (MIT) and the Plaits engines (MIT), both vendored in third_party/.
#define TEST 1   // (the vendored eurorack headers: desktop build, no ARM assembly)
#include "GhostSynth.h"
#if defined (__clang__)
 #pragma clang diagnostic push
 #pragma clang diagnostic ignored "-Weverything"
#elif defined (__GNUC__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wall"
 #pragma GCC diagnostic ignored "-Wextra"
 #pragma GCC diagnostic ignored "-Wshadow"
#elif defined (_MSC_VER)
 #pragma warning (push, 0)
#endif
#include "rosic_Open303.h"
#include "rosic_TeeBeeFilter.h"
#include "plaits/dsp/engine/virtual_analog_engine.h"
#include "plaits/dsp/engine/fm_engine.h"
#include "plaits/dsp/engine/swarm_engine.h"
#include "plaits/dsp/oscillator/oscillator.h"
#include "stmlib/utils/random.h"
#include "stmlib/utils/buffer_allocator.h"
#if defined (__clang__)
 #pragma clang diagnostic pop
#elif defined (__GNUC__)
 #pragma GCC diagnostic pop
#elif defined (_MSC_VER)
 #pragma warning (pop)
#endif
#undef PI
#undef INLINE
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <new>
#include <vector>

namespace gsyn {

Knobs defaultKnobs(int s) {
    Knobs k;
    switch (s) {
        case S_ACID:     k = { 0.30f, 0.80f, 0.60f, 0.35f, 0.70f, 0.30f }; break;
        case S_ACIDSQ:   k = { 0.35f, 0.70f, 0.55f, 0.30f, 0.60f, 0.25f }; break;
        case S_SAW101:   k = { 0.40f, 0.35f, 0.50f, 0.30f, 0.50f, 0.15f }; break;
        case S_FMBASS:   k = { 0.45f, 0.30f, 0.55f, 0.30f, 0.50f, 0.10f }; break;
        case S_FMBELL:   k = { 0.55f, 0.20f, 0.45f, 0.60f, 0.40f, 0.00f }; break;
        case S_SUPERSAW: k = { 0.72f, 0.15f, 0.20f, 0.55f, 0.40f, 0.05f }; break;
        case S_PLUCK:    k = { 0.48f, 0.30f, 0.65f, 0.35f, 0.50f, 0.05f }; break;
        case S_STAB:     k = { 0.58f, 0.25f, 0.55f, 0.35f, 0.50f, 0.10f }; break;
        default: break;
    }
    return k;
}

namespace {
constexpr double kTwoPi = 6.283185307179586;
inline double lin2exp(double x, double lo, double hi) { return lo * std::pow(hi / lo, std::clamp(x, 0.0, 1.0)); }
inline float drive(float x, float d) { if (d <= 0.001f) return x; const float g = 1.0f + d * 11.0f; return std::tanh(x * g) / std::tanh(g) * (1.0f + d * 0.6f); }

// 2x decimator: 31-tap windowed-sinc half band (the Plaits engines run at twice the host rate so their saws alias less)
struct Decim2 {
    static constexpr int N = 31; float h[N] {}; float x[N] {}; int w = 0;
    Decim2() { double s = 0; for (int i = 0; i < N; ++i) { const double k = i - (N - 1) / 2.0, sinc = k == 0 ? 0.5 : std::sin(kTwoPi * 0.25 * k) / (0.5 * kTwoPi * k),
                   win = 0.42 - 0.5 * std::cos(kTwoPi * i / (N - 1)) + 0.08 * std::cos(2 * kTwoPi * i / (N - 1)); h[i] = (float) (sinc * win); s += h[i]; }
               for (auto& v : h) v = (float) (v / s); }
    void clear() { std::fill(std::begin(x), std::end(x), 0.0f); w = 0; }
    float push2(float a, float b) {   // two engine-rate samples in, one host-rate sample out
        x[w] = a; w = (w + 1) % N; x[w] = b; w = (w + 1) % N;
        float y = 0; for (int i = 0; i < N; ++i) y += h[i] * x[(w + i) % N]; return y;
    }
};

// one Plaits engine in its own (zeroed) storage, its working memory allocated up front
template <typename... T> constexpr std::size_t maxOf() { std::size_t m = 0; ((m = std::max(m, sizeof(T))), ...); return m; }
struct Core {
    enum Kind { VA, FM, SWARM };
    alignas(16) unsigned char storage[maxOf<plaits::VirtualAnalogEngine, plaits::FMEngine, plaits::SwarmEngine>()];
    plaits::Engine* e = nullptr; int kind = -1; void (*kill)(plaits::Engine*) noexcept = nullptr;
    std::vector<unsigned char> work = std::vector<unsigned char>(48 * 1024, 0);
    float aux[64] {}; uint32_t rng = 0x21;
    Core() = default; Core(const Core&) = delete; Core& operator=(const Core&) = delete;
    ~Core() { destroy(); }
    void destroy() { if (e && kill) kill(e); e = nullptr; kill = nullptr; }
    template <typename T> void build() { e = new (storage) T(); kill = [](plaits::Engine* p) noexcept { static_cast<T*>(p)->T::~T(); }; }
    void select(int k, float rate) {
        if (k == kind && e) { plaits::SetSampleRate(rate); stmlib::Random::Seed(rng); e->Reset(); rng = stmlib::Random::state(); return; }
        destroy(); std::memset(storage, 0, sizeof(storage)); kind = k;
        if (k == VA) build<plaits::VirtualAnalogEngine>(); else if (k == FM) build<plaits::FMEngine>(); else build<plaits::SwarmEngine>();
        std::fill(work.begin(), work.end(), (unsigned char) 0); stmlib::BufferAllocator a(work.data(), work.size());
        plaits::SetSampleRate(rate); e->Init(&a); e->Reset();
    }
    void render(float rate, float note, float h, float t, float m, bool trig, bool unpatched, float* out, int n) {
        plaits::SetSampleRate(rate); stmlib::Random::Seed(rng);
        plaits::EngineParameters p; p.trigger = trig ? plaits::TRIGGER_RISING_EDGE : plaits::TRIGGER_LOW; if (unpatched) p.trigger = plaits::TRIGGER_UNPATCHED;
        p.note = note; p.harmonics = std::clamp(h, 0.f, 1.f); p.timbre = std::clamp(t, 0.f, 1.f); p.morph = std::clamp(m, 0.f, 1.f); p.accent = 0.8f;
        bool env = false; e->Render(p, out, aux, (size_t) n, &env); rng = stmlib::Random::state();
        for (int i = 0; i < n; ++i) if (! std::isfinite(out[i])) out[i] = 0.0f;
    }
};

// exponential ADSR (times in seconds)
struct Adsr {
    double a = 0.002, d = 0.3, s = 0.0, r = 0.1, sr = 44100, v = 0; int st = 0;   // 0 idle 1 attack 2 decay/sustain 3 release
    void on() { st = 1; } void off() { if (st) st = 3; }
    double next() {
        if (st == 1) { v += 1.0 / std::max(1.0, a * sr); if (v >= 1.0) { v = 1.0; st = 2; } }
        else if (st == 2) v = s + (v - s) * std::exp(-1.0 / std::max(1.0, d * sr / 4.6));
        else if (st == 3) { v *= std::exp(-1.0 / std::max(1.0, r * sr / 4.6)); if (v < 1e-5) { v = 0; st = 0; } }
        return v;
    }
};

// a Plaits-based voice (101 BASS, FM, SUPERSAW, PLUCK, STAB)
struct Voice {
    bool on = false; int sound = -1, lane = -1, midi = 60; uint32_t id = 0; int64_t t0 = 0, off = INT64_MAX;
    double hz = 261.6, target = 261.6, glide = 0; float vel = 0.8f, pan = 0, gain = 1.0f; bool accent = false; Knobs k;
    // saw: band-limited saws (Plaits' oscillator) for 101 / SUPERSAW / PLUCK / STAB
    Core c[2]; plaits::Oscillator saw[2][7]; uint32_t rs = 1; rosic::TeeBeeFilter f[2]; Decim2 dec[2]; Adsr amp, fenv; double subPh = 0; float sub = 0; bool trig = false;
    float buf[2][48] {}; int bufPos = 12, bufN = 12;
    int hold = 0; float gs = -1;   // amp waits out the decimator delay (the first real sample); gs = the gain, gliding   // host-rate samples of the current block (12 per 24 engine samples)
    void prepare(double sr) { for (auto& x : f) x.setSampleRate(2.0 * sr); amp.sr = fenv.sr = sr; }
};

struct Mono303 { rosic::Open303 o; uint32_t id = 0; int64_t off = INT64_MAX; bool used = false; float pan = 0; Knobs k; int sound = 0; float lvl = 1, lvlT = 1; };

struct Ev { int64_t t; bool on; int lane, sound, midi; float vel; bool acc, slide; float pan; Knobs k; uint32_t id; float gain = 1.0f; };
} // namespace

struct Bus::Impl {
    double sr = 44100; int maxBlk = 512;
    std::vector<Voice> voices = std::vector<Voice>(24);
    static constexpr int k303 = 4;   // 303 voices: lanes share them (lane & 3); built only when a pattern uses the 303 (their wavetables take a while)
    std::array<std::atomic<Mono303*>, k303> m303 {}; std::vector<std::unique_ptr<Mono303>> own303;
    std::array<int, Bus::kLanes> laneVoice {};   // mono Plaits sound: the voice index this lane is using (-1 none)
    std::array<uint32_t, Bus::kLanes> laneId {};  // the newest note on the lane (its gate is the one that counts)
    std::array<Ev, 1024> q {}; int nq = 0; uint32_t ids = 0;
    // bus FX
    float space = 0.0f, echo = 0.0f; double bpm = 124; int64_t idle = INT64_MAX / 2;
    std::vector<float> dl, dr; int dpos = 0; float dlp[2] {};
    struct Verb { std::array<std::vector<float>, 8> comb; std::array<int, 8> cp {}; std::array<float, 8> cf {}; std::array<std::vector<float>, 4> ap; std::array<int, 4> apos {}; } vb[2];
    void prepareVerb(double rate) {
        static const int CT[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 }, AT[4] = { 556, 441, 341, 225 };   // Freeverb's tunings (public domain)
        for (int s = 0; s < 2; ++s) { for (int i = 0; i < 8; ++i) { vb[s].comb[(size_t) i].assign((size_t) ((CT[i] + s * 23) * rate / 44100.0) + 1, 0.0f); vb[s].cp[(size_t) i] = 0; vb[s].cf[(size_t) i] = 0; }
                                      for (int i = 0; i < 4; ++i) { vb[s].ap[(size_t) i].assign((size_t) ((AT[i] + s * 23) * rate / 44100.0) + 1, 0.0f); vb[s].apos[(size_t) i] = 0; } }
    }
    float verb(int s, float x) {
        auto& v = vb[s]; float y = 0; const float fb = 0.84f, damp = 0.25f;
        for (int i = 0; i < 8; ++i) { auto& b = v.comb[(size_t) i]; int& p = v.cp[(size_t) i]; const float o = b[(size_t) p]; v.cf[(size_t) i] = o * (1 - damp) + v.cf[(size_t) i] * damp; b[(size_t) p] = x + v.cf[(size_t) i] * fb; p = (p + 1) % (int) b.size(); y += o; }
        for (int i = 0; i < 4; ++i) { auto& b = v.ap[(size_t) i]; int& p = v.apos[(size_t) i]; const float o = b[(size_t) p]; b[(size_t) p] = y + o * 0.5f; y = o - y; p = (p + 1) % (int) b.size(); }
        return y;
    }

    void push(const Ev& e) {
        if (nq >= (int) q.size()) return;
        int i = nq++; while (i > 0 && q[(size_t) i - 1].t > e.t) { q[(size_t) i] = q[(size_t) i - 1]; --i; } q[(size_t) i] = e;
    }
    void set303(Mono303& m, const Knobs& k, int sound) {
        auto& o = m.o;
        o.setWaveform(sound == S_ACIDSQ ? 1.0 : 0.0);
        o.setCutoff(lin2exp(k.cut, 314.0, 2394.0)); o.setResonance(k.res * 100.0); o.setEnvMod(k.env * 100.0);
        o.setDecay(lin2exp(k.dec, 200.0, 2000.0)); o.setAccent(k.acc * 100.0); o.setVolume(-6.0);
    }
    void start(const Ev& e) {
        if (e.sound == S_ACID || e.sound == S_ACIDSQ) {
            Mono303* mp = m303[(size_t) (e.lane & 3)].load(std::memory_order_acquire); if (! mp) return; auto& m = *mp;
            const bool held = m.used && (m.o.isHeld() || ! m.o.ampEnv.endIsReached()), slide = e.slide && m.o.isHeld() && m.used;
            set303(m, e.k, e.sound); m.o.gateOn(e.midi, e.acc, slide); m.id = e.id; m.used = true; m.pan = e.pan; m.sound = e.sound;
            m.lvlT = 0.36f * (0.6f + 0.4f * e.vel) * e.gain; if (! held) m.lvl = m.lvlT; m.k = e.k;   // a note into a sounding 303: its level glides (no step)
            return;
        }
        if (isMono(e.sound)) {   // 101 / FM BASS: one voice per lane; a slide glides without restarting the envelopes
            const int vi = laneVoice[(size_t) e.lane];
            if (vi >= 0 && voices[(size_t) vi].on && voices[(size_t) vi].sound == e.sound && e.slide && voices[(size_t) vi].amp.st != 3) {
                auto& v = voices[(size_t) vi]; v.target = 440.0 * std::exp2((e.midi - 69) / 12.0); v.glide = 1.0 - std::exp(-1.0 / (0.06 * sr / 4.6)); v.id = e.id; v.midi = e.midi; v.accent = e.acc; v.gain = e.gain; return;
            }
            if (vi >= 0 && voices[(size_t) vi].on) voices[(size_t) vi].amp.r = 0.004;   // the old note gets out of the way
        }
        int vi = -1; double quiet = 1e9; int oi = 0;
        for (int i = 0; i < (int) voices.size(); ++i) { const auto& w = voices[(size_t) i]; if (! w.on) { vi = i; break; } const double lv = w.amp.v * (w.amp.st == 3 ? 0.5 : 1.0); if (lv < quiet) { quiet = lv; oi = i; } }
        if (vi < 0) vi = oi;
        auto& v = voices[(size_t) vi];
        const float er = (float) (2.0 * sr);
        if (e.sound == S_FMBASS || e.sound == S_FMBELL) { v.c[0].rng = 0x9E3779B9u * (e.id + 1); v.c[0].select(Core::FM, er); }
        else {   // saws: every one starts at its own random phase (a supersaw that starts in phase sounds like a zap)
            v.rs = 0x9E3779B9u * (e.id + 1) | 1u; const double hz0 = 440.0 * std::exp2((e.midi - 69) / 12.0); float scratch[48];
            const int nsaw = e.sound == S_SUPERSAW ? 7 : e.sound == S_STAB ? 3 : e.sound == S_PLUCK ? 2 : 1, nch = e.sound == S_SUPERSAW ? 2 : 1;
            for (int ch = 0; ch < nch; ++ch) for (int k = 0; k < nsaw; ++k) { auto& o = v.saw[ch][k]; o.Init();
                if (nsaw > 1) { v.rs ^= v.rs << 13; v.rs ^= v.rs >> 17; v.rs ^= v.rs << 5; int left = (int) (v.rs % (uint32_t) std::max(1.0, er / hz0));
                    const float fq = (float) (hz0 / er); while (left > 0) { const int m = std::min(48, left); o.Render<plaits::OSCILLATOR_SHAPE_SAW>(fq, 0.5f, scratch, (size_t) m); left -= m; } } }
        }
        for (auto& x : v.dec) x.clear();
        for (auto& x : v.f) { x.reset(); x.setMode(e.sound == S_SAW101 ? rosic::TeeBeeFilter::TB_303 : rosic::TeeBeeFilter::LP_24); }
        v.on = true; v.sound = e.sound; v.lane = e.lane; v.midi = e.midi; v.id = e.id; v.t0 = e.t; v.off = INT64_MAX; v.vel = e.vel; v.gain = e.gain; v.pan = e.pan; v.accent = e.acc; v.k = e.k;
        v.hz = v.target = 440.0 * std::exp2((e.midi - 69) / 12.0); v.glide = 0; v.subPh = 0; v.trig = true; v.bufPos = v.bufN; v.hold = (Decim2::N - 1) / 4 + 1; v.gs = -1;
        const double dk = e.k.dec;
        auto& A = v.amp; auto& F = v.fenv; A.v = 0; F.v = 0;
        switch (e.sound) {
            case S_SAW101:   A.a = 0.003; A.d = lin2exp(dk, 0.15, 1.5); A.s = 0.7; A.r = 0.04; F.a = 0.001; F.d = lin2exp(dk, 0.08, 1.2); F.s = 0; F.r = 0.05; break;
            case S_FMBASS:   A.a = 0.003; A.d = lin2exp(dk, 0.10, 1.2); A.s = 0.35; A.r = 0.06; F.a = 0.001; F.d = lin2exp(dk, 0.05, 0.8); F.s = 0; F.r = 0.05; break;
            case S_FMBELL:   A.a = 0.003; A.d = lin2exp(dk, 0.4, 4.0); A.s = 0.0; A.r = 0.8; F.a = 0.001; F.d = lin2exp(dk, 0.2, 2.0); F.s = 0; F.r = 0.3; break;
            case S_SUPERSAW: A.a = 0.008; A.d = 0.3; A.s = 0.85; A.r = lin2exp(dk, 0.12, 1.4); F.a = 0.004; F.d = lin2exp(dk, 0.2, 2.0); F.s = 0.3; F.r = 0.4; break;
            case S_PLUCK:    A.a = 0.003; A.d = lin2exp(dk, 0.15, 0.9); A.s = 0.0; A.r = 0.15; F.a = 0.001; F.d = lin2exp(dk, 0.04, 0.45); F.s = 0; F.r = 0.1; break;
            case S_STAB:     A.a = 0.003; A.d = lin2exp(dk, 0.12, 0.6); A.s = 0.0; A.r = 0.12; F.a = 0.001; F.d = lin2exp(dk, 0.05, 0.4); F.s = 0; F.r = 0.1; break;
            default: break;
        }
        if (e.acc) { A.d *= 0.8; F.d *= 0.7; }
        A.on(); F.on();
        if (isMono(e.sound)) laneVoice[(size_t) e.lane] = vi;
    }
    void stop(const Ev& e) {
        if (e.sound == S_ACID || e.sound == S_ACIDSQ) { Mono303* mp = m303[(size_t) (e.lane & 3)].load(std::memory_order_acquire); if (mp && mp->id == e.id) mp->o.gateOff(); return; }
        for (auto& v : voices) if (v.on && v.id == e.id) { v.amp.off(); v.fenv.off(); }
    }
    // one block of a voice: 24 engine-rate samples -> 12 host-rate samples
    void block(Voice& v) {
        const float er = (float) (2.0 * sr); float eo[2][24];
        if (v.glide > 0) for (int i = 0; i < 12; ++i) v.hz += (v.target - v.hz) * v.glide; else v.hz = v.target;
        const float note = (float) (69.0 + 12.0 * std::log2(v.hz / 440.0));
        const double fe = v.fenv.v; const auto& k = v.k; const float acc = v.accent ? k.acc : 0.0f;
        const int s = v.sound;
        if (s == S_FMBASS || s == S_FMBELL) {   // FM: CUTOFF = brightness (index), ENV = how much the index falls, RESO = feedback
            const float ratio = s == S_FMBASS ? 0.238f : 0.72f;   // Plaits' ratio table: 0.238 = 1:1 (a clean bass in tune), 0.72 = about 1:3.5 (a bell)
            const float idx = std::clamp((float) (k.cut * 0.75 + fe * k.env * 0.5 + acc * 0.15), 0.0f, 1.0f);
            v.c[0].render(er, note, ratio, idx, 0.5f + 0.5f * k.res, v.trig, false, eo[0], 24);
        } else {   // saws: SUPERSAW = 7 per side (the JP-8000 spread, the sides detuned the other way), STAB = 3, PLUCK = 2, 101 = 1
            static const float SS[7] = { -0.11002313f, -0.06288439f, -0.01952356f, 0.0f, 0.01991221f, 0.06216538f, 0.10745242f };
            static const float SG[7] = { 0.55f, 0.65f, 0.8f, 1.0f, 0.8f, 0.65f, 0.55f };
            const int nsaw = s == S_SUPERSAW ? 7 : s == S_STAB ? 3 : s == S_PLUCK ? 2 : 1, nch = s == S_SUPERSAW ? 2 : 1;
            float tmp[24];
            for (int ch = 0; ch < nch; ++ch) { std::fill(eo[ch], eo[ch] + 24, 0.0f); float gsum = 0;
                for (int k = 0; k < nsaw; ++k) {
                    const float semis = s == S_SUPERSAW ? SS[ch ? 6 - k : k] * 3.2f : s == S_STAB ? (k == 0 ? 0.0f : k == 1 ? 0.11f : -0.12f) : s == S_PLUCK ? (k ? 0.06f : -0.06f) : 0.0f;
                    const float g = s == S_SUPERSAW ? SG[k] : 1.0f; gsum += g;
                    const float fq = (float) (v.hz * std::exp2(semis / 12.0) / er);
                    v.saw[ch][k].Render<plaits::OSCILLATOR_SHAPE_SAW>(fq, 0.5f, tmp, 24);
                    for (int i = 0; i < 24; ++i) eo[ch][i] += tmp[i] * g; }
                const float norm = 1.0f / std::sqrt(gsum * (float) nsaw) * 1.4f; for (int i = 0; i < 24; ++i) eo[ch][i] *= norm; }
        }
        v.trig = false;
        const bool stereo = s == S_SUPERSAW;
        // the ladder: CUTOFF 40 Hz .. 16 kHz, ENV adds up to 7 octaves, ACCENT opens it further
        if (s != S_FMBASS && s != S_FMBELL) {
            const double base = lin2exp(k.cut, 40.0, 16000.0), oct = fe * (k.env * 7.0 + acc * 2.0), kt = std::clamp(0.6 + 0.4 * v.hz / 220.0, 0.6, 2.0);
            const double fc = std::min(base * std::exp2(oct) * kt, std::min(sr * 0.9, s == S_SAW101 ? 1.0e9 : 12000.0));
            for (int ch = 0; ch < (stereo ? 2 : 1); ++ch) { v.f[ch].setCutoff(fc); v.f[ch].setResonance(k.res * (s == S_SAW101 ? 95.0 : 80.0));
                for (int i = 0; i < 24; ++i) {
                    float x = eo[ch][i];
                    if (s == S_SAW101 && ch == 0) { const double dp = v.hz * 0.5 / er; v.subPh += dp; if (v.subPh >= 1) v.subPh -= 1;   // the sub: a band-limited square (polyBLEP)
                        auto blep = [](double t, double d) { if (t < d) { t /= d; return t + t - t * t - 1.0; } if (t > 1.0 - d) { t = (t - 1.0) / d; return t * t + t + t + 1.0; } return 0.0; };
                        double t2 = v.subPh + 0.5; if (t2 >= 1) t2 -= 1; const double sq = (v.subPh < 0.5 ? 1.0 : -1.0) + blep(v.subPh, dp) - blep(t2, dp); x += (float) (0.22 * sq); }
                    eo[ch][i] = (float) v.f[ch].getSample(x);
                } }
        }
        for (int ch = 0; ch < 2; ++ch) { const float* src = eo[stereo ? ch : 0]; Decim2& d = v.dec[stereo ? ch : 0];
            if (! stereo && ch == 1) { for (int i = 0; i < 12; ++i) v.buf[1][i] = v.buf[0][i]; break; }
            for (int i = 0; i < 12; ++i) v.buf[ch][i] = d.push2(src[2 * i], src[2 * i + 1]); }
        v.bufPos = 0; v.bufN = 12;
    }
    void renderVoice(Voice& v, float* L, float* R, int from, int to) {
        static const float gain[kSounds] = { 1, 1, 0.55f, 0.6f, 0.55f, 0.75f, 1.3f, 0.75f };
        const float g0 = gain[v.sound] * (0.45f + 0.55f * v.vel) * (v.accent ? 1.25f : 1.0f) * v.gain;
        const float pl = v.pan > 0 ? 1.0f - v.pan : 1.0f, pr = v.pan < 0 ? 1.0f + v.pan : 1.0f, kg = (float) (1.0 - std::exp(-1.0 / (0.005 * sr)));
        if (v.gs < 0) v.gs = g0;
        for (int i = from; i < to; ++i) {
            if (v.bufPos >= v.bufN) block(v);
            float a; if (v.hold > 0) { --v.hold; a = 0.0f; } else a = (float) v.amp.next(); v.fenv.next(); v.gs += (g0 - v.gs) * kg;
            const float x0 = drive(v.buf[0][v.bufPos], v.k.drv), x1 = drive(v.buf[1][v.bufPos], v.k.drv); ++v.bufPos;
            L[i] += x0 * a * v.gs * pl; R[i] += x1 * a * v.gs * pr;
            if (v.amp.st == 0) { v.on = false; if (v.lane >= 0 && v.lane < Bus::kLanes && laneVoice[(size_t) v.lane] >= 0 && &voices[(size_t) laneVoice[(size_t) v.lane]] == &v) laneVoice[(size_t) v.lane] = -1; return; }
        }
    }
    void renderAll(float* L, float* R, int from, int to) {
        for (auto& v : voices) if (v.on) renderVoice(v, L, R, from, to);
        for (auto& ap : m303) { Mono303* mp = ap.load(std::memory_order_acquire); if (! (mp && mp->used)) continue; auto& m = *mp; if (! m.o.isHeld() && m.o.ampEnv.endIsReached()) { m.used = false; continue; } const float pl = m.pan > 0 ? 1.0f - m.pan : 1.0f, pr = m.pan < 0 ? 1.0f + m.pan : 1.0f;
            const float kg = (float) (1.0 - std::exp(-1.0 / (0.005 * sr)));
            for (int i = from; i < to; ++i) { m.lvl += (m.lvlT - m.lvl) * kg; const float x = drive((float) m.o.getSample() * m.lvl, m.k.drv) * (1.0f - 0.35f * m.k.drv); L[i] += x * pl; R[i] += x * pr; } }
    }
};

Bus::Bus() : d(std::make_unique<Impl>()) { d->laneVoice.fill(-1); }
Bus::~Bus() = default;

void Bus::prepare(double sampleRate, int maxBlock) {
    d->sr = sampleRate > 0 ? sampleRate : 44100.0; d->maxBlk = std::max(1, maxBlock);
    for (auto& v : d->voices) { v.on = false; v.prepare(d->sr); }   // (a Voice is never copied: its engines live in its own storage)
    for (auto& m : d->own303) { m->o.setSampleRate(d->sr); m->used = false; m->o.gateOff(); }
    d->dl.assign((size_t) (d->sr * 2.0) + 4, 0.0f); d->dr = d->dl; d->dpos = 0; d->dlp[0] = d->dlp[1] = 0;
    d->prepareVerb(d->sr); d->nq = 0; d->laneVoice.fill(-1);
}
void Bus::reset() {
    for (auto& v : d->voices) v.on = false;
    for (auto& m : d->own303) { m->o.gateOff(); m->used = false; }
    d->nq = 0; d->laneVoice.fill(-1);
    std::fill(d->dl.begin(), d->dl.end(), 0.0f); std::fill(d->dr.begin(), d->dr.end(), 0.0f);
}
void Bus::note(int64_t t, int64_t off, int lane, int sound, int midi, float vel, bool accent, bool slide, float pan, const Knobs& k, float gain) {
    if (lane < 0 || lane >= kLanes || sound < 0 || sound >= kSounds) return;
    const uint32_t id = ++d->ids; midi = std::clamp(midi, 12, 108);
    gain = std::clamp(gain, 0.0f, 1.5f);
    d->push({ t, true, lane, sound, midi, vel, accent, slide, pan, k, id, gain });
    d->push({ std::max(off, t + 1), false, lane, sound, midi, vel, accent, slide, pan, k, id, gain });
}
void Bus::setFx(float space, float echo, double bpm) { d->space = std::clamp(space, 0.f, 1.f); d->echo = std::clamp(echo, 0.f, 1.f); if (bpm > 20) d->bpm = bpm; }
void Bus::render(float* L, float* R, int n, int64_t now) {
    auto& D = *d; int at = 0;
    // nothing playing and the SPACE / ECHO tails long gone: nothing to do (no CPU when the synth page is silent)
    if (D.nq == 0 && activeVoices() == 0) { if (D.idle >= (int64_t) (D.sr * 4.0)) return; D.idle += n; } else D.idle = 0;
    // wet buffers: the voices render into a scratch pair, then the bus FX
    float sl[2048], srr[2048];
    while (at < n) {
        const int m = std::min(n - at, 2048);
        std::fill(sl, sl + m, 0.0f); std::fill(srr, srr + m, 0.0f);
        int i = 0;
        while (i < m) {
            int64_t t = now + at + i; int next = m;
            while (D.nq > 0 && D.q[0].t <= t) { const Ev e = D.q[0]; for (int k = 1; k < D.nq; ++k) D.q[(size_t) k - 1] = D.q[(size_t) k]; --D.nq; if (e.on) D.start(e); else D.stop(e); }
            if (D.nq > 0) next = (int) std::clamp<int64_t>(D.q[0].t - (now + at), i + 1, m);
            D.renderAll(sl, srr, i, next); i = next;
        }
        // ECHO: dotted 1/8 ping-pong, darkened; SPACE: a stereo room
        const int dt = std::clamp((int) (60.0 / D.bpm * 0.75 * D.sr), 1, (int) D.dl.size() - 1); const int N = (int) D.dl.size();
        for (int k = 0; k < m; ++k) {
            float a = sl[k], b = srr[k];
            if (D.echo > 0.001f) { const int rp = (D.dpos - dt + N) % N; const float el = D.dl[(size_t) rp], er = D.dr[(size_t) rp];
                D.dlp[0] += 0.35f * (er - D.dlp[0]); D.dlp[1] += 0.35f * (el - D.dlp[1]);
                D.dl[(size_t) D.dpos] = (a + b) * 0.5f * D.echo + D.dlp[0] * 0.55f; D.dr[(size_t) D.dpos] = D.dlp[1] * 0.55f;
                a += el * D.echo; b += er * D.echo; }
            D.dpos = (D.dpos + 1) % N;
            if (D.space > 0.001f) { const float in = (a + b) * 0.015f; const float wl = D.verb(0, in), wr = D.verb(1, in); a += wl * D.space * 1.6f; b += wr * D.space * 1.6f; }
            L[at + k] += a; R[at + k] += b;
        }
        at += m;
    }
}
int Bus::activeVoices() const { int c = 0; for (auto& v : d->voices) c += v.on; for (auto& m : d->own303) c += (m->used && m->o.isHeld()); return c; }
void Bus::need303() {
    if (! d->own303.empty()) return;
    for (int k = 0; k < Impl::k303; ++k) { auto m = std::make_unique<Mono303>(); m->o.setSampleRate(d->sr); m->o.setSlideTime(60.0); m->o.setAmpSustain(-60.0); m->o.gateOff();
        d->m303[(size_t) k].store(m.get(), std::memory_order_release); d->own303.push_back(std::move(m)); }
}
bool Bus::silent() const { return activeVoices() == 0; }

} // namespace gsyn
