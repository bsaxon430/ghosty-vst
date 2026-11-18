#pragma once
// GHOST HUM - the built-in sounds and the player: a small piano (and SOFT KEYS, BELL, PLUCK) for melodies, five synthesised drums
// (kick, snare, hi-hat, open hat, clap: our own, no samples) for beats, the metronome click, and the sequence of a take at the
// current grid. The plugin's playback and the WAV render use the same code, so what you hear is what you drag out.
#include "HumModel.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace hum::sound {

enum Voice : int { Piano = 0, SoftKeys, Bell, Pluck, kVoices };
inline const char* voiceName(int v) { static const char* n[] { "PIANO", "SOFT KEYS", "BELL", "PLUCK" }; return v >= 0 && v < kVoices ? n[v] : "?"; }

struct Rng { uint32_t s; explicit Rng(uint32_t seed) : s(seed * 2654435761u + 12345u) {} float u() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) (s & 0xffffff) / 8388608.0f - 1.0f; } };
struct OnePole { double z = 0, a = 1; void set(double hz, double sr) { a = 1.0 - std::exp(-juce::MathConstants<double>::twoPi * juce::jlimit(10.0, sr * 0.45, hz) / sr); } float lp(float x) { z += a * (x - z); return (float) z; } };
struct SVF { double s1 = 0, s2 = 0, a1 = 0, a2 = 0, a3 = 0, k = 1;
    void set(double f, double q, double sr) { const double g = std::tan(juce::MathConstants<double>::pi * juce::jlimit(10.0, sr * 0.45, f) / sr); k = 1.0 / juce::jmax(0.05, q); a1 = 1 / (1 + g * (g + k)); a2 = g * a1; a3 = g * a2; }
    double tick(double x, int type) { const double v3 = x - s2, v1 = a1 * s1 + a2 * v3, v2 = s2 + a2 * s1 + a3 * v3; s1 = 2 * v1 - s1; s2 = 2 * v2 - s2; return type == 0 ? v2 : (type == 1 ? v1 : x - k * v1 - v2); } };
inline void filt(std::vector<float>& b, double sr, int type, double f, double q = 0.707) { SVF s; s.set(f, q, sr); for (auto& x : b) x = (float) s.tick(x, type); }
inline double env(double t, double att, double hold, double dec) { const double a = att > 0 ? juce::jmin(1.0, t / att) : 1.0; return a * (t < hold ? 1.0 : std::exp(-(t - hold) / juce::jmax(1e-4, dec))); }

// ---------------------------------------------------------------- drums (rendered once per sample rate)
inline std::vector<float> makeDrum(int d, double sr) {
    const double TAU = juce::MathConstants<double>::twoPi; Rng r((uint32_t) (d * 7 + 3));
    const double len = d == Kick ? 0.5 : d == Snare ? 0.3 : d == HatClosed ? 0.12 : d == HatOpen ? 0.5 : 0.35; const int n = (int) (len * sr); std::vector<float> b((size_t) n, 0.0f);
    auto metal = [&](double t) { static const double S[] { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 }; double v = 0; for (double s : S) v += std::sin(TAU * s * 1.7 * t) >= 0 ? 1.0 : -1.0; return v / 6.0; };
    switch (d) {
        case Kick: { double ph = 0; OnePole lp; lp.set(2500, sr);
            for (int i = 0; i < n; ++i) { const double t = i / sr, f = 52 + 120 * std::exp(-t / 0.028); ph += TAU * f / sr;
                const double body = std::tanh(1.8 * std::sin(ph)) * env(t, 0.0005, 0.02, 0.17), click = lp.lp(r.u()) * std::exp(-t / 0.004) * 1.2;
                b[(size_t) i] = (float) (body + click * 0.5); } break; }
        case Snare: { std::vector<float> nz((size_t) n); for (auto& x : nz) x = r.u(); filt(nz, sr, 2, 900); std::vector<float> bp = nz; filt(bp, sr, 1, 3800, 0.7);
            double p1 = 0, p2 = 0; for (int i = 0; i < n; ++i) { const double t = i / sr, bend = 1 + 0.3 * std::exp(-t / 0.012); p1 += TAU * 185 * bend / sr; p2 += TAU * 185 * 1.6 * bend / sr;
                const double body = (std::sin(p1) + 0.5 * std::sin(p2)) * env(t, 0.0003, 0.004, 0.06) * 0.55, ns = (nz[(size_t) i] * 0.45 + bp[(size_t) i] * 1.1) * env(t, 0.0005, 0.002, 0.11);
                b[(size_t) i] = (float) std::tanh(1.3 * (body + ns)); } break; }
        case HatClosed: case HatOpen: { const double dec = d == HatClosed ? 0.045 : 0.28;
            for (int i = 0; i < n; ++i) { const double t = i / sr; b[(size_t) i] = (float) ((metal(t) * 0.6 + r.u() * 0.4) * env(t, 0.0003, 0, dec)); }
            filt(b, sr, 2, 7000); filt(b, sr, 2, 5600, 0.6); break; }
        default: { std::vector<float> nz((size_t) n); for (auto& x : nz) x = r.u(); std::vector<float> bp = nz; filt(bp, sr, 1, 1200, 0.9); filt(nz, sr, 2, 400); for (int i = 0; i < n; ++i) nz[(size_t) i] = bp[(size_t) i] * 1.6f + nz[(size_t) i] * 0.15f;
            const double on[4] { 0.0, 0.010, 0.021, 0.031 };
            for (int i = 0; i < n; ++i) { const double t = i / sr; double e = 0; for (double o : on) if (t >= o) e = juce::jmax(e, std::exp(-(t - o) / 0.006)); if (t > on[3]) e = juce::jmax(e, std::exp(-(t - on[3]) / 0.11) * 0.6);
                b[(size_t) i] = (float) std::tanh(1.2 * nz[(size_t) i] * e); } break; }
    }
    float pk = 1e-6f; for (auto v : b) pk = juce::jmax(pk, std::abs(v)); const float g = (d == Kick ? 0.95f : d == Snare ? 0.8f : d == Clap ? 0.75f : 0.5f) / pk; for (auto& v : b) v *= g;
    const int fade = juce::jmin(n / 4, (int) (sr * 0.01)); for (int i = 0; i < fade; ++i) b[(size_t) (n - 1 - i)] *= (float) i / (float) fade;
    return b;
}
inline std::vector<float> makeClick(bool accent, double sr) {
    const int n = (int) (sr * 0.03); std::vector<float> b((size_t) n); const double f = accent ? 2000 : 1400;
    for (int i = 0; i < n; ++i) { const double t = i / sr; b[(size_t) i] = (float) (std::sin(juce::MathConstants<double>::twoPi * f * t) * env(t, 0.0005, 0.002, 0.008) * (accent ? 0.6 : 0.4)); }
    return b;
}

// ---------------------------------------------------------------- the synth
struct Synth {
    double sr = 48000; int voice = Piano; float gain = 0.8f;
    std::array<std::vector<float>, kDrums> drums; std::vector<float> clickA, clickB;
    struct NoteV { bool on = false, held = false; int midi = 0; double ph[4] {}, f = 440; float vel = 1, cents = 0; double t = 0, rel = -1; SVF lp; OnePole click; double rnd = 0; };
    struct Shot { const std::vector<float>* buf = nullptr; int pos = 0; float g = 1; };
    std::array<NoteV, 16> voices; std::array<Shot, 24> shots; Rng rng { 9 };

    void prepare(double rate) { sr = rate; for (int d = 0; d < kDrums; ++d) drums[(size_t) d] = makeDrum(d, sr); clickA = makeClick(true, sr); clickB = makeClick(false, sr); allOff(true); }
    void allOff(bool hard = false) { for (auto& v : voices) { if (hard) v.on = false; else if (v.on) { v.held = false; v.rel = 0; } } if (hard) for (auto& s : shots) s.buf = nullptr; }
    void noteOn(int midi, float vel, float cents = 0) {
        int k = -1; for (int i = 0; i < (int) voices.size(); ++i) if (!voices[(size_t) i].on) { k = i; break; }
        if (k < 0) { double old = -1; for (int i = 0; i < (int) voices.size(); ++i) if (voices[(size_t) i].t > old) { old = voices[(size_t) i].t; k = i; } }
        auto& v = voices[(size_t) k]; v = NoteV(); v.on = true; v.held = true; v.midi = midi; v.vel = juce::jlimit(0.05f, 1.0f, vel); v.cents = cents; v.f = 440.0 * std::pow(2.0, (midi - 69 + cents / 100.0) / 12.0); v.t = 0; v.rel = -1;
        v.lp.set(juce::jmin(sr * 0.4, v.f * (voice == Pluck ? 9.0 : 6.0) + 800.0 * vel), 0.6, sr); v.click.set(3000, sr); v.rnd = (double) rng.u();
    }
    void noteOff(int midi) { for (auto& v : voices) if (v.on && v.held && v.midi == midi) { v.held = false; v.rel = 0; } }
    void drum(int d, float vel) { if (d < 0 || d >= kDrums) return; for (auto& s : shots) if (!s.buf) { s.buf = &drums[(size_t) d]; s.pos = 0; s.g = std::pow(juce::jlimit(0.05f, 1.0f, vel), 1.4f); return; }
        shots[0].buf = &drums[(size_t) d]; shots[0].pos = 0; shots[0].g = vel; }
    void click(bool accent) { for (auto& s : shots) if (!s.buf) { s.buf = accent ? &clickA : &clickB; s.pos = 0; s.g = 1; return; } }
    bool busy() const { for (auto& v : voices) if (v.on) return true; for (auto& s : shots) if (s.buf) return true; return false; }

    float tickVoice(NoteV& v) {
        const double TAU = juce::MathConstants<double>::twoPi, t = v.t, dt = 1.0 / sr; double out = 0; const double fr = v.f / sr;
        const double relT = v.rel >= 0 ? v.rel : -1;
        switch (voice) {
            case Piano: {   // three slightly stretched partials + a soft hammer, decays shorter up the keyboard, release 70 ms
                const double tone = juce::jlimit(0.2, 1.0, 1.4 - v.f / 1400.0); const double dec = 0.9 * std::pow(0.55, (v.midi - 60) / 12.0);
                v.ph[0] += TAU * fr; v.ph[1] += TAU * fr * 2.003; v.ph[2] += TAU * fr * 3.012; v.ph[3] += TAU * fr * 4.03;
                const double a = env(t, 0.002, 0, dec), a2 = env(t, 0.002, 0, dec * 0.45), a3 = env(t, 0.002, 0, dec * 0.25);
                out = std::sin(v.ph[0]) * a + 0.5 * tone * std::sin(v.ph[1]) * a2 + 0.25 * tone * std::sin(v.ph[2]) * a3 + 0.12 * tone * std::sin(v.ph[3]) * a3 * v.vel;
                out += v.click.lp(rng.u()) * std::exp(-t / 0.003) * 0.35 * v.vel;
                out = v.lp.tick(out, 0) * (0.35 + 0.65 * v.vel);
                if (relT >= 0) out *= std::exp(-relT / 0.07);
                if ((t > 0.01 && a < 0.002) || (relT > 0.4)) v.on = false; break; }
            case SoftKeys: { v.ph[0] += TAU * fr; v.ph[1] += TAU * fr * 1.0015; const double tri = 2.0 / juce::MathConstants<double>::pi * std::asin(std::sin(v.ph[1]));
                out = (std::sin(v.ph[0]) * 0.7 + tri * 0.3) * env(t, 0.02, 1e9, 1) * (0.3 + 0.7 * v.vel) * 0.8; out = v.lp.tick(out, 0);
                if (relT >= 0) out *= std::exp(-relT / 0.25); if (relT > 1.2) v.on = false; break; }
            case Bell: { const double idx = 2.2 * std::exp(-t / 0.6) * (0.5 + 0.5 * v.vel); v.ph[1] += TAU * fr * 3.5; v.ph[0] += TAU * fr; const double mod = idx * std::sin(v.ph[1]);
                out = std::sin(v.ph[0] + mod) * env(t, 0.001, 0, 1.2) * (0.3 + 0.7 * v.vel) * 0.7; if (relT >= 0) out *= std::exp(-relT / 0.3); if (relT > 1.5 || t > 4) v.on = false; break; }
            default: { v.ph[0] += fr; if (v.ph[0] >= 1) v.ph[0] -= 1; const double saw = 2 * v.ph[0] - 1, dec = 0.5;
                v.lp.set(juce::jlimit(200.0, sr * 0.4, v.f * 1.5 + 6000.0 * v.vel * std::exp(-t / 0.12)), 0.8, sr);
                out = v.lp.tick(saw, 0) * env(t, 0.001, 0, dec) * (0.3 + 0.7 * v.vel) * 0.8; if (relT >= 0) out *= std::exp(-relT / 0.08); if (t > 2.5 || relT > 0.5) v.on = false; break; }
        }
        v.t += dt; if (v.rel >= 0) v.rel += dt;
        return (float) out;
    }
    void render(float* L, float* R, int n) {
        for (int i = 0; i < n; ++i) {
            float s = 0; for (auto& v : voices) if (v.on) s += tickVoice(v);
            float d = 0; for (auto& sh : shots) if (sh.buf) { d += (*sh.buf)[(size_t) sh.pos] * sh.g; if (++sh.pos >= (int) sh.buf->size()) sh.buf = nullptr; }
            const float o = (s * 0.5f + d) * gain; L[i] += o; if (R) R[i] += o;
        }
    }
};

// ---------------------------------------------------------------- the sequence of a take
// original = ORIGINAL timing (exactly as performed; the grid is ignored); otherwise ON GRID (QUANTIZE / SWING applied). Both play the
// working notes (the PITCH setting and your edits); the as-heard notes (origNotes) are the layer PITCH changes start from.
struct Ev { double t = 0; int type = 0; int num = 0; float vel = 1, cents = 0; };   // type 0 note on, 1 note off, 2 drum
inline std::vector<Ev> sequence(const Take& T, const Grid& G, bool original) {
    std::vector<Ev> evs; const bool grid = !original;
    if (T.mode == Melody) { const auto& ns = T.notes;
        for (auto& n : ns) { const double t0 = grid ? G.place(n.t, T) : n.t; double t1 = grid ? G.place(n.t + n.len, T) : n.t + n.len; if (t1 - t0 < 0.03) t1 = t0 + juce::jmax(0.03, n.len);
            evs.push_back({ t0, 0, n.midi, n.vel, n.cents }); evs.push_back({ t1, 1, n.midi, 0, 0 }); } }
    else { const auto& hs = T.hits; for (auto& h : hs) evs.push_back({ grid ? G.place(h.t, T) : h.t, 2, h.drum, h.vel, 0 }); }
    std::stable_sort(evs.begin(), evs.end(), [](const Ev& a, const Ev& b) { return a.t < b.t || (a.t == b.t && a.type > b.type); });   // offs before ons at the same time
    return evs;
}
inline double sequenceEnd(const std::vector<Ev>& evs) { double e = 0; for (auto& v : evs) e = juce::jmax(e, v.t); return e; }

// offline render (the WAV export): stereo, 48 kHz unless asked, a tail so the last sound rings out
inline juce::AudioBuffer<float> render(const Take& T, const Grid& G, bool original, int voice, double sr = 48000.0) {
    Synth S; S.prepare(sr); S.voice = voice; const auto evs = sequence(T, G, original);
    const double tail = T.mode == Melody ? 1.2 : 0.6; double end = sequenceEnd(evs) + tail; if (T.mode == Melody && T.bpm > 0 && G.quant != QOff) { const double bar = 240.0 / T.bpm; end = juce::jmax(end, T.origin + std::ceil((end - T.origin) / bar) * bar); }
    const int total = (int) std::ceil(end * sr); juce::AudioBuffer<float> out(2, juce::jmax(1, total)); out.clear();
    size_t k = 0; const int block = 256;
    for (int pos = 0; pos < total; pos += block) { const int n = juce::jmin(block, total - pos);
        for (int i = 0; i < n; ++i) { const double t = (pos + i) / sr; while (k < evs.size() && evs[k].t <= t) { const auto& e = evs[k++]; if (e.type == 0) S.noteOn(e.num, e.vel, e.cents); else if (e.type == 1) S.noteOff(e.num); else S.drum(e.num, e.vel); }
            float l = 0, r = 0; S.render(&l, &r, 1); out.setSample(0, pos + i, l); out.setSample(1, pos + i, r); } }
    return out;
}

} // namespace hum::sound
