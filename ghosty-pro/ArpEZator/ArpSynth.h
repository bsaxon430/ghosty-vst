#pragma once
// ArpEZator preview sound: lead voices (glide chains, 7 presets incl. NEON PLUCK and BRIGHT PIANO, +PLUCK / +OCTAVE
// layers), chord voices (soft pad or wide supersaw STABS), bass, a little kick, tempo-synced cutoff LFO, dotted-1/8
// ping-pong delay, reverb and a quarter-note PUMP. Everything is preallocated in prepare(): no allocation, no locks
// on the audio thread.
#include <juce_audio_basics/juce_audio_basics.h>
#include "ArpSampler.h"
#include <array>
#include <cmath>

namespace arpsyn
{
constexpr int kLead = 24, kChord = 8, kBass = 4, kMaxPitches = 6;
enum Preset { Neon = 0, Piano, Pluck, Supersaw, Keys, Bell, Acid, Euphoric, DuskPluck, HypnoSaw, TrapFlute, Bass808, DustyKeys, SubBass, SoulOrgan, WarmPadKeys, GuitarPluck, SadPad, kPresets };   // the moods' own sounds appended (saved indices keep their sound; the processor moves YOUR SAMPLE up in older states)
constexpr int kPresetsBeforeUrban = 10;   // the preset count before TRAP FLUTE / 808 BASS / DUSTY KEYS (older states)
inline const char* presetName (int p)
{
    static const char* n[] { "NEON PLUCK", "BRIGHT PIANO", "PLUCK", "SUPERSAW", "SOFT KEYS", "BELL", "ACID", "SUNRISE STACK", "DUSK PLUCK", "MIDNIGHT SAW", "TRAP FLUTE", "808 BASS", "DUSTY KEYS", "ROUND SUB", "SOUL ORGAN", "WARM PAD KEYS", "CLEAN GUITAR PLUCK", "SAD PAD" };
    return n[juce::jlimit (0, kPresets - 1, p)];
}
inline bool& testNoNoise() { static bool off = false; return off; }   // tests only: the new sounds without their noise parts (breath, vinyl bed) for the aliasing check of the tone
inline float mtof (float p) { return 440.0f * std::pow (2.0f, (p - 69.0f) / 12.0f); }
inline float polyBlep (float t, float dt)
{
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}
inline float sawB (float ph, float dt) { return 2.0f * ph - 1.0f - polyBlep (ph, dt); }
inline float sqrB (float ph, float dt) { float v = ph < 0.5f ? 1.0f : -1.0f; v += polyBlep (ph, dt); float p2 = ph + 0.5f; if (p2 >= 1.0f) p2 -= 1.0f; v -= polyBlep (p2, dt); return v; }

struct SVF   // TPT state-variable low-pass
{
    float ic1 = 0, ic2 = 0;
    void reset() { ic1 = ic2 = 0; }
    float lp (float x, float g, float k)
    {
        const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
        return v2;
    }
};
inline float svfG (float fc, float sr) { return std::tan (juce::MathConstants<float>::pi * juce::jlimit (20.0f, sr * 0.45f, fc) / sr); }

struct Env   // attack (linear) -> exponential decay to sustain -> exponential release (like setTargetAtTime)
{
    float level = 0, a = 0.003f, d = 0.3f, s = 0.2f, r = 0.15f; int stage = 0;   // 0 idle 1 attack 2 decay 3 release
    void on (float peak) { stage = 1; target = peak; }
    void off() { if (stage != 0) stage = 3; }
    float next (float sr)
    {
        switch (stage)
        {
            case 1: level += target / std::max (1.0f, a * sr); if (level >= target) { level = target; stage = 2; } break;
            case 2: level += (target * s - level) * (1.0f - std::exp (-3.0f / std::max (1.0f, d * sr))); break;
            case 3: level += (0.0f - level) * (1.0f - std::exp (-3.0f / std::max (1.0f, r * sr))); if (level < 1.0e-5f) { level = 0; stage = 0; } break;
            case 4: level += rampInc * 44100.0f / sr; if (level >= rampTo) { level = rampTo; stage = 2; } break;   // a legato lift, glided
            default: break;
        }
        return level;
    }
    // a legato note lifts the level (never lowers it). 2026-11-13: the lift glides over 4 ms; it jumped in one sample = a click on every tied note
    void bump (float peak)
    {
        target = peak; if (stage == 0) { stage = 1; return; }
        const float want = std::max (level, peak * (s + (1.0f - s) * 0.45f));
        if (want > level + 1.0e-4f) { rampTo = want; rampInc = (want - level) / (0.004f * 44100.0f); stage = 4; } else stage = 2;
    }
    float target = 1, rampTo = 0, rampInc = 0;
};

struct PresetDef { int nOsc; float det[8]; float semi[8]; float gain[8]; int wave[8]; /*0 saw 1 sqr 2 tri 3 sine*/ float cut, envAmt, q, fdec, att, dec, sus, rel, gain0; bool fm; float fmRatio, fmIdx; bool additive; };
inline const PresetDef& def (int p)
{
    static const PresetDef D[kPresets] {
        // NEON PLUCK: 7 detuned saws, bright, fast filter envelope, a touch of resonance
        { 7, { -21, -13, -6, 0, 6, 13, 21 }, { 0, 0, 0, 0, 0, 0, 0 }, { .22f, .22f, .22f, .26f, .22f, .22f, .22f }, { 0, 0, 0, 0, 0, 0, 0 }, 1000, 6000, 0.9f, 0.16f, 0.002f, 0.32f, 0.24f, 0.2f, 1.28f, false, 0, 0, false },
        // BRIGHT PIANO (additive partials, see LeadVoice)
        { 0, {}, {}, {}, {}, 9000, 0, 0.5f, 0.3f, 0.002f, 1.6f, 0.0f, 0.35f, 0.55f, false, 0, 0, true },
        { 3, { -9, 9, 0 }, { 0, 0, -12 }, { .5f, .5f, .2f }, { 0, 0, 1 }, 420, 5200, 1.5f, 0.17f, 0.002f, 0.24f, 0.14f, 0.14f, 1.19f, false, 0, 0, false },
        { 6, { -26, -12, 0, 12, 26, 0 }, { 0, 0, 0, 0, 0, -12 }, { .3f, .3f, .3f, .3f, .3f, .16f }, { 0, 0, 0, 0, 0, 0 }, 1500, 4200, 1.13f, 0.32f, 0.006f, 0.45f, 0.6f, 0.22f, 0.8f, false, 0, 0, false },
        { 3, { 0, 3, -3 }, { 0, 12, 0 }, { .75f, .2f, .35f }, { 2, 3, 3 }, 2000, 2800, 1.08f, 0.45f, 0.003f, 0.9f, 0.2f, 0.3f, 0.84f, false, 0, 0, false },
        { 2, { 0, 2 }, { 0, 12 }, { .8f, .1f }, { 3, 3 }, 9000, 0, 1.06f, 0.3f, 0.002f, 1.4f, 0.0f, 0.5f, 0.69f, true, 3.5f, 2.6f, false },
        { 1, { 0 }, { 0 }, { 1 }, { 0 }, 240, 3000, 3.98f, 0.2f, 0.003f, 0.35f, 0.55f, 0.07f, 0.5f, false, 0, 0, false },
        // SUNRISE STACK: a wide 7-saw unison, open and euphoric (+ a soft piano layer for the attack, see Engine::leadStart)
        { 7, { -24, -15, -7, 0, 7, 15, 24 }, { 0, 0, 0, 0, 0, 0, 0 }, { .2f, .2f, .2f, .24f, .2f, .2f, .2f }, { 0, 0, 0, 0, 0, 0, 0 }, 2200, 4200, 0.8f, 0.4f, 0.005f, 0.7f, 0.5f, 0.3f, 1.25f, false, 0, 0, false },
        // DUSK PLUCK: two close saws + a soft sine octave, round, short and warm
        { 3, { -7, 7, 0 }, { 0, 0, 12 }, { .45f, .45f, .16f }, { 0, 0, 3 }, 380, 4400, 1.15f, 0.2f, 0.002f, 0.36f, 0.1f, 0.22f, 1.3f, false, 0, 0, false },
        // MIDNIGHT SAW: three saws, dark and resonant; the tempo-synced filter LFO does the hypnotic part
        { 3, { -9, 0, 9 }, { 0, 0, 0 }, { .36f, .4f, .36f }, { 0, 0, 0 }, 760, 1500, 1.9f, 0.45f, 0.003f, 0.5f, 0.6f, 0.18f, 0.88f, false, 0, 0, false },
        // TRAP FLUTE (own path, see LeadVoice::renderFlute): sine + triangle body, band-passed breath, soft attack, delayed vibrato
        { 2, { 0, 0 }, { 0, 0 }, { .8f, .2f }, { 3, 3 }, 2200, 900, 0.75f, 0.35f, 0.045f, 1.2f, 0.85f, 0.2f, 0.27f, false, 0, 0, false },
        // 808 BASS (own path, see LeadVoice::render808): sine + a little triangle, a click, a fast pitch drop, long decay, soft drive, mono
        { 1, { 0 }, { 0 }, { 1 }, { 3 }, 900, 0, 0.7f, 0.2f, 0.0015f, 1.56f, 0.0f, 0.078f, 0.4f, false, 0, 0, false },
        // DUSTY KEYS (own path, see LeadVoice::renderKeys): two detuned sine / triangle layers + a short pluck, low-passed ~3.5 kHz, tape wobble, a vinyl bed
        { 2, { -6, 6 }, { 0, 0 }, { .6f, .6f }, { 3, 2 }, 3500, 1200, 0.75f, 0.12f, 0.003f, 2.4f, 0.0f, 0.3f, 0.36f, false, 0, 0, false },
        // ROUND SUB (the 808 path without the click / pitch drop / drive): a clean sine sub that holds while the note is held, glides
        { 1, { 0 }, { 0 }, { 1 }, { 3 }, 600, 0, 0.7f, 0.2f, 0.004f, 0.3f, 0.85f, 0.18f, 0.36f, false, 0, 0, false },
        // SOUL ORGAN (own path, see LeadVoice::renderOrgan): drawbar partials 16' 8' 5 1/3' 4' 2 2/3' 2', a soft key click, a slow rotary wobble
        { 6, {}, {}, {}, {}, 5000, 0, 0.7f, 0.1f, 0.005f, 0.3f, 1.0f, 0.08f, 0.2f, false, 0, 0, false },
        // WARM PAD KEYS (renderKeys, muted): a soft electric-piano body, slow attack, long release, dark low-pass
        { 2, { -8, 8 }, { 0, 0 }, { .6f, .6f }, { 3, 3 }, 1500, 300, 0.7f, 0.3f, 0.03f, 3.0f, 0.55f, 1.2f, 0.42f, false, 0, 0, false },
        // CLEAN GUITAR PLUCK (own path, see LeadVoice::renderGuitar): a Karplus-Strong plucked string (the string decays by itself: the
        // envelope only holds and releases), body / pickup resonances, chorus taps from the string's own delay line
        { 1, { 0 }, { 0 }, { 1 }, { 0 }, 6500, 0, 0.7f, 0.26f, 0.001f, 3.0f, 1.0f, 0.36f, 0.5f, false, 0, 0, false },
        // SAD PAD (own path, see LeadVoice::renderPad): two saws +-9 c + a triangle an octave down, a slow 450 ms swell, long release
        { 3, { -9, 9, 0 }, { 0, 0, -12 }, { .5f, .5f, .4f }, { 0, 0, 2 }, 1800, 0, 0.75f, 0.3f, 0.45f, 1.5f, 0.8f, 2.9f, 0.17f, false, 0, 0, false } };
    return D[juce::jlimit (0, kPresets - 1, p)];
}

struct LeadVoice
{
    bool active = false; int chain = -1, lane = 0, preset = 0; float shift = 0, gainMul = 1, bright = 1;   // bright: the arp EVOLVE curve's filter multiplier
    float pitch = 60, gFrom = 60, gTo = 60; int gPos = 0, gLen = 0;
    int endIn = 0;        // samples until the chain ends (release)
    float vel = 0.8f, envAmt = 0; std::array<float, 16> ph {}; float fmPh = 0, fmEnv = 0; float noise = 0; uint32_t rnd = 1;
    Env amp, fenv; SVF fl, fr; juce::uint64 age = 0;
    float pianoT = 0;
    float vt = 0, vibPh = 0, slideFrom = 0; SVF nzA, nzB; float clickPh = 0;   // TRAP FLUTE / 808 BASS / DUSTY KEYS state (time, vibrato, breath band, click)
    // CLEAN GUITAR PLUCK: the string's delay line (a slice of the engine's pool, assigned in Engine::leadStart; nullptr = the
    // filtered-saw fallback), loop loss / damping, the body filters, the strum delay; SAD PAD / GUITAR: the sends into the shared FX
    struct Sends { float* rev = nullptr; float* dly = nullptr; float* scratch = nullptr; bool used = false; };
    struct Biq { float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0; float run (float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; } };
    float* ks = nullptr; int ksMask = 0; int ksW = 0; float ksRho = 0.996f, ksS = 0.17f, ksPrev = 0, ksTone = 0, ksPick = 0, ksHp = 0; Sends* sends = nullptr;
    std::array<Biq, 3> body {}; int delayIn = 0; bool pluckNow = false; float chA = 0, chB = 0;

    void start (int pre, float p, float v, int lengthSamples, int ln, int ch, juce::uint64 a, float sr)
    {
        preset = pre; const PresetDef& D = def (pre);
        active = true; chain = ch; lane = ln; age = a; pitch = gFrom = gTo = p; gPos = gLen = 0; endIn = std::max (1, lengthSamples); vel = v;
        amp.a = D.att; amp.d = D.dec / 3.0f; amp.s = D.sus; amp.r = D.rel / 3.0f; amp.stage = 0; amp.level = 0; amp.on (D.gain0 * std::pow (v, 1.3f));
        fenv.a = 0.001f; fenv.d = D.fdec / 3.0f; fenv.s = 0.0f; fenv.r = 0.05f; fenv.stage = 0; fenv.level = 0; fenv.on (1.0f);
        envAmt = (pre == Acid && v > 0.8f ? 1.7f : 1.0f) * D.envAmt * (0.45f + 0.55f * v);
        for (size_t i = 0; i < ph.size(); ++i) ph[i] = (float) ((rnd = rnd * 1664525u + 1013904223u) >> 8) / 16777216.0f;
        fmPh = 0; fmEnv = 1; fl.reset(); fr.reset(); pianoT = 0; (void) sr;
        vt = 0; vibPh = 0; slideFrom = 0; nzA.reset(); nzB.reset(); clickPh = 0;
        delayIn = 0; pluckNow = pre == GuitarPluck; ksPrev = 0; ksTone = 0; ksPick = 0; ksHp = 0;
        if (pre == GuitarPluck)
        {   // body: a neck / body bump at 110 + 220 Hz and the clean pickup's 2.5 kHz presence (peaking filters, RBJ)
            const float f[3] { 110.0f, 220.0f, 2500.0f }, q[3] { 2.0f, 2.0f, 1.0f }, db[3] { 2.5f, 1.5f, 2.0f };
            for (int k = 0; k < 3; ++k)
            {
                const float A = std::pow (10.0f, db[k] / 40.0f), w = juce::MathConstants<float>::twoPi * juce::jmin (f[k], sr * 0.45f) / sr, al = std::sin (w) / (2.0f * q[k]), a0 = 1.0f + al / A;
                body[(size_t) k] = { (1.0f + al * A) / a0, -2.0f * std::cos (w) / a0, (1.0f - al * A) / a0, -2.0f * std::cos (w) / a0, (1.0f - al / A) / a0, 0, 0 };
            }
        }
    }
    void legato (float p, float v)
    {
        const PresetDef& D = def (preset);
        if (preset == TrapFlute && (gLen == 0 || gPos >= gLen) && std::abs (p - pitch) > 0.01f) { gFrom = pitch; gTo = p; gPos = 0; gLen = 1; slideFrom = 1; }   // a legato step slides (length set in render)
        else if (gLen == 0 || gPos >= gLen) pitch = gTo = p;
        vel = v;
        if (preset == Bass808 || preset == SubBass) return;   // a slide: no new attack, no new pitch drop
        if (preset == GuitarPluck) { pluckNow = true; return; }   // a legato note: the string is picked again (a running slide keeps going)
        amp.bump (D.gain0 * std::pow (v, 1.3f)); fenv.level = std::max (fenv.level, 0.5f * v); fenv.stage = 2;
    }
    void glide (float to, int lenSamples) { gFrom = pitch; gTo = to; gPos = 0; gLen = std::max (1, lenSamples); slideFrom = preset == Bass808 || preset == SubBass ? 2.0f : 0.0f; }   // (808: 30..120 ms, ROUND SUB 30..250 ms, see render808)
    void release() { amp.off(); fenv.off(); }

    void render (float* L, float* R, int n, float sr, float cutMul, float lfoOct)
    {
        const PresetDef& D = def (preset);
        if (preset == TrapFlute) { renderFlute (L, R, n, sr, cutMul); return; }
        if (preset == Bass808 || preset == SubBass) { render808 (L, R, n, sr, cutMul); return; }
        if (preset == DustyKeys || preset == WarmPadKeys) { renderKeys (L, R, n, sr, cutMul); return; }
        if (preset == SoulOrgan) { renderOrgan (L, R, n, sr, cutMul); return; }
        if (preset == GuitarPluck) { renderGuitar (L, R, n, sr, cutMul); return; }
        if (preset == SadPad) { renderPad (L, R, n, sr, cutMul); return; }
        for (int i = 0; i < n; ++i)
        {
            if (endIn > 0 && --endIn == 0) release();
            if (gLen > 0 && gPos < gLen) { ++gPos; pitch = gFrom + (gTo - gFrom) * ((float) gPos / (float) gLen); }
            const float a = amp.next (sr), fe = fenv.next (sr);
            if (amp.stage == 0) { active = false; return; }
            const float f0 = mtof (pitch + shift);
            float sl = 0, sr2 = 0;
            if (D.additive)
            {   // bright piano: two slightly detuned strings of 8 stretched partials, higher partials die first
                pianoT += 1.0f / sr;
                const float keyDecay = juce::jlimit (0.35f, 2.6f, 2.4f - (pitch - 60.0f) * 0.035f);
                for (int s = 0; s < 2; ++s)
                    for (int k = 1; k <= 8; ++k)
                    {
                        const size_t idx = (size_t) (s * 8 + k - 1);
                        const float fk = f0 * (float) k * std::sqrt (1.0f + 0.00035f * (float) (k * k)) * (s ? 1.0012f : 0.9991f);
                        if (fk > sr * 0.45f) continue;
                        ph[idx] += fk / sr; if (ph[idx] >= 1.0f) ph[idx] -= 1.0f;
                        const float amk = std::pow ((float) k, -1.15f) * std::exp (-pianoT * (1.0f + 0.55f * (float) k) / keyDecay) * (k == 1 ? 1.0f : (0.6f + 0.4f * vel));
                        const float v = std::sin (juce::MathConstants<float>::twoPi * ph[idx]) * amk * 0.5f;
                        if (s) sr2 += v; else sl += v;
                    }
                if (pianoT < 0.006f) { rnd = rnd * 1664525u + 1013904223u; const float nz = ((float) (rnd >> 8) / 8388608.0f - 1.0f) * (1.0f - pianoT / 0.006f) * 0.25f * vel; sl += nz; sr2 += nz; }
            }
            else
            {
                float mod = 0;
                if (D.fm) { fmPh += f0 * D.fmRatio / sr; if (fmPh >= 1) fmPh -= 1; fmEnv += (0.05f - fmEnv) * (1.0f - std::exp (-1.0f / (0.22f * sr))); mod = std::sin (juce::MathConstants<float>::twoPi * fmPh) * D.fmIdx * fmEnv * vel; }
                for (int o = 0; o < D.nOsc; ++o)
                {
                    const float f = f0 * std::pow (2.0f, (D.semi[o] * 100.0f + D.det[o]) / 1200.0f), dt = f / sr;
                    float& p = ph[(size_t) o]; p += dt * (1.0f + mod * 0.0f); if (p >= 1.0f) p -= 1.0f;
                    float v;
                    switch (D.wave[o])
                    {
                        case 0: v = sawB (p, dt); break;
                        case 1: v = sqrB (p, dt); break;
                        case 2: v = 4.0f * std::abs (p - 0.5f) - 1.0f; break;
                        default: v = std::sin (juce::MathConstants<float>::twoPi * p + mod); break;
                    }
                    v *= D.gain[o];
                    const float pan = D.nOsc > 1 ? ((float) o / (float) (D.nOsc - 1) - 0.5f) * (preset == Neon || preset == Supersaw || preset == Euphoric ? 1.2f : 0.5f) : 0.0f;
                    sl += v * (0.5f - pan * 0.5f) * 2.0f * 0.5f; sr2 += v * (0.5f + pan * 0.5f) * 2.0f * 0.5f;
                }
            }
            float fc = (D.cut + envAmt * fe) * cutMul * bright * std::pow (2.0f, lfoOct);
            fc = juce::jlimit (40.0f, 18000.0f, fc);
            const float g = svfG (fc, sr), k = 1.0f / D.q;
            const float yl = fl.lp (sl, g, k), yr = fr.lp (sr2, g, k);
            L[i] += yl * a * gainMul; R[i] += yr * a * gainMul;
        }
    }
    // TRAP FLUTE: the breathy low pan-flute lead. A hollow tone (fundamental + a little of the 3rd and 5th harmonics), a second voice
    // drifting a few cents against it, a short chiff of noise at the attack and a touch of breath under the note, a slow vibrato that
    // fades in after ~0.2 s, 90 ms slides between legato notes, a dark low-pass. Every control moves per sample.
    void renderFlute (float* L, float* R, int n, float sr, float cutMul)
    {
        const PresetDef& D = def (preset);
        if (slideFrom > 0.5f) { gLen = std::max (1, (int) (0.09f * sr)); slideFrom = 0; }   // legato: a 90 ms slide
        const float twoPi = juce::MathConstants<float>::twoPi;
        for (int i = 0; i < n; ++i)
        {
            if (endIn > 0 && --endIn == 0) release();
            if (gLen > 0 && gPos < gLen) { ++gPos; const float u = (float) gPos / (float) gLen; pitch = gFrom + (gTo - gFrom) * (u * u * (3.0f - 2.0f * u)); }
            const float a = amp.next (sr);
            if (amp.stage == 0) { active = false; return; }
            vt += 1.0f / sr;
            const float vibD = juce::jlimit (0.0f, 1.0f, (vt - 0.2f) / 0.5f), vib = 0.18f * vibD * vibD * std::sin (twoPi * vibPh);   // up to +-18 cents, 4.6 Hz
            vibPh += 4.6f / sr; if (vibPh >= 1.0f) vibPh -= 1.0f;
            const float drift = 0.035f * std::sin (twoPi * 0.31f * vt + (float) (rnd & 7));   // the second voice drifts +-3.5 cents
            const float f0 = mtof (pitch + shift + vib), dt = f0 / sr, dt2 = mtof (pitch + shift + vib + drift) / sr;
            float& p0 = ph[0]; p0 += dt; if (p0 >= 1.0f) p0 -= 1.0f;
            float& p1 = ph[1]; p1 += dt2; if (p1 >= 1.0f) p1 -= 1.0f;
            const float body = (std::sin (twoPi * p0) + 0.14f * std::sin (3.0f * twoPi * p0) + 0.045f * std::sin (5.0f * twoPi * p0)) * 0.72f
                             + std::sin (twoPi * p1) * 0.28f;
            rnd = rnd * 1664525u + 1013904223u; const float wn = (float) (rnd >> 8) / 8388608.0f - 1.0f;
            const float hiB = nzA.lp (wn, svfG (juce::jmin (sr * 0.4f, f0 * 4.0f + 1500.0f), sr), 1.2f), loB = nzB.lp (wn, svfG (f0 * 1.1f, sr), 1.2f);
            const float chiff = std::exp (-vt / 0.025f);
            const float breath = testNoNoise() ? 0.0f : (hiB - loB) * (0.06f + 0.45f * chiff) * (0.7f + 0.3f * vel);
            const float x = body + breath;
            const float fc = juce::jlimit (300.0f, 12000.0f, D.cut * cutMul * bright + 1500.0f * chiff);
            const float g = svfG (fc, sr), k = 1.0f / D.q;
            const float yl = fl.lp (x, g, k), yr = fr.lp (body * 0.98f + breath * 1.02f, g, k);
            L[i] += yl * a * gainMul; R[i] += yr * a * gainMul;
        }
    }
    // DUSTY KEYS / WARM PAD KEYS: two layers detuned against each other (a sine and a soft triangle made of its first odd partials),
    // a short pluck at the attack (DUSTY only), a low-pass (3.5 kHz / 1.5 kHz), a gentle tape wobble, a vinyl bed at about -40 dB
    // that lives and dies with the note (DUSTY only).
    void renderKeys (float* L, float* R, int n, float sr, float cutMul)
    {
        const PresetDef& D = def (preset); const bool dusty = preset == DustyKeys;
        const float twoPi = juce::MathConstants<float>::twoPi;
        for (int i = 0; i < n; ++i)
        {
            if (endIn > 0 && --endIn == 0) release();
            if (gLen > 0 && gPos < gLen) { ++gPos; pitch = gFrom + (gTo - gFrom) * ((float) gPos / (float) gLen); }
            const float a = amp.next (sr);
            if (amp.stage == 0) { active = false; return; }
            vt += 1.0f / sr;
            const float wob = 0.07f * std::sin (twoPi * 0.55f * vt) + 0.025f * std::sin (twoPi * 1.9f * vt + 1.3f);
            const float fa = mtof (pitch + shift + wob + D.det[0] * 0.01f) / sr, fb = mtof (pitch + shift + wob + D.det[1] * 0.01f) / sr;
            float& pa = ph[0]; pa += fa; if (pa >= 1.0f) pa -= 1.0f;
            float& pb = ph[1]; pb += fb; if (pb >= 1.0f) pb -= 1.0f;
            const float tri = std::sin (twoPi * pb) - std::sin (3.0f * twoPi * pb) / 9.0f + std::sin (5.0f * twoPi * pb) / 25.0f;
            float x = std::sin (twoPi * pa) * D.gain[0] + (dusty ? tri : std::sin (twoPi * pb)) * D.gain[1];
            if (dusty) { const float pk = std::exp (-vt / 0.012f); x += std::sin (2.0f * twoPi * pa + 1.5f * pk * std::sin (twoPi * pa)) * 0.35f * pk * vel; }   // the pluck
            else x += std::sin (2.0f * twoPi * pa) * 0.08f * std::exp (-vt / 0.15f);   // a soft tine
            // (2026-11-13: the vinyl bed is gone: it read as crackle on the chords)
            const float fc = juce::jlimit (200.0f, 9000.0f, (D.cut + D.envAmt * std::exp (-vt / 0.08f) * vel) * cutMul * bright);
            const float g = svfG (fc, sr), k = 1.0f / D.q;
            const float yl = fl.lp (x, g, k), yr = fr.lp (x, g, k);
            L[i] += yl * a * gainMul; R[i] += yr * a * gainMul;
        }
    }
    // SOUL ORGAN: additive drawbars (16' 8' 5 1/3' 4' 2 2/3' 2'), a 3 ms key click, a slow rotary: tremolo + a few cents of vibrato +
    // the sound moving between left and right
    void renderOrgan (float* L, float* R, int n, float sr, float cutMul)
    {
        const PresetDef& D = def (preset);
        static const float ratio[6] { 0.5f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f }, bar[6] { 0.45f, 1.0f, 0.55f, 0.6f, 0.28f, 0.32f };
        const float twoPi = juce::MathConstants<float>::twoPi;
        for (int i = 0; i < n; ++i)
        {
            if (endIn > 0 && --endIn == 0) release();
            if (gLen > 0 && gPos < gLen) { ++gPos; pitch = gFrom + (gTo - gFrom) * ((float) gPos / (float) gLen); }
            const float a = amp.next (sr);
            if (amp.stage == 0) { active = false; return; }
            vt += 1.0f / sr;
            vibPh += 0.8f / sr; if (vibPh >= 1.0f) vibPh -= 1.0f;   // the slow rotor
            const float rot = std::sin (twoPi * vibPh);
            const float f0 = mtof (pitch + shift + 0.04f * rot);
            float x = 0;
            for (int k = 0; k < 6; ++k)
            {
                const float f = f0 * ratio[k]; if (f > sr * 0.45f) continue;
                float& p = ph[(size_t) k]; p += f / sr; if (p >= 1.0f) p -= 1.0f;
                x += std::sin (twoPi * p) * bar[k];
            }
            x *= 0.33f;
            if (vt < 0.003f) { rnd = rnd * 1664525u + 1013904223u; x += ((float) (rnd >> 8) / 8388608.0f - 1.0f) * 0.18f * (1.0f - vt / 0.003f) * vel; }   // key click
            const float g = svfG (juce::jlimit (300.0f, 12000.0f, D.cut * cutMul * bright), sr), kq = 1.0f / D.q;
            const float trem = 1.0f - 0.12f * (0.5f + 0.5f * rot);
            const float yl = fl.lp (x, g, kq) * trem * (1.0f + 0.25f * rot), yr = fr.lp (x, g, kq) * trem * (1.0f - 0.25f * rot);
            L[i] += yl * a * gainMul; R[i] += yr * a * gainMul;
        }
    }
    // CLEAN GUITAR PLUCK: an extended Karplus-Strong string. On the pluck the delay line is filled with one period of white noise,
    // low-passed by the pick brightness (1.2 .. 6.5 kHz with velocity) and combed by the pick position (13 % of the string); the loop
    // reads it back through a 4-point Lagrange fractional delay (in tune at every pitch, slides move it smoothly), a one-zero damping
    // blend (high partials die first) and a loss that sets the decay (T60 3 s on the low strings .. 1.5 s on the high ones, the
    // feedback capped at 0.996). Body bumps + the pickup's presence, a soft tone low-pass, a 3 ms pick click, two chorus taps from
    // the same delay line (0.4 Hz, L / R in opposite phase, 30 %: mono-safe), and its own delay + reverb sends. Strums: Engine.
    void renderGuitar (float* L, float* R, int n, float sr, float cutMul)
    {
        const PresetDef& D = def (preset);
        if (ks == nullptr || sends == nullptr) { renderSawPluck (L, R, n, sr, cutMul); return; }
        const float twoPi = juce::MathConstants<float>::twoPi;
        const int cap = ksMask + 1, maxD = cap - (int) (0.016f * sr) - 8;
        const float toneA = 1.0f - std::exp (-twoPi * juce::jlimit (800.0f, sr * 0.45f, 6500.0f * cutMul * bright) / sr);
        auto loopDelay = [&] (float& f0) { f0 = mtof (pitch + shift); return juce::jlimit (2.0f, (float) maxD, sr / f0 - ksS); };   // this delay + the damping's ~S samples = one period
        float f0 = 0, Dl = loopDelay (f0);
        // the chorus taps: the LFO per block, the tap positions ramped per sample from where the last block left them
        const float lfo1 = std::sin (twoPi * 0.4f * (vt + (float) n / sr)), cA1 = (0.0075f + 0.0018f * lfo1) * sr, cB1 = (0.0075f - 0.0018f * lfo1) * sr;
        if (vt <= 0.0f) { const float l0 = std::sin (0.0f); chA = (0.0075f + 0.0018f * l0) * sr; chB = chA; }
        const float dA = (cA1 - chA) / (float) std::max (1, n), dB = (cB1 - chB) / (float) std::max (1, n);
        for (int i = 0; i < n; ++i)
        {
            if (delayIn > 0) { --delayIn; continue; }   // a strum: this string starts a little later
            if (endIn > 0 && --endIn == 0) release();
            if (gLen > 0 && gPos < gLen) { ++gPos; const float u = (float) gPos / (float) gLen; pitch = gFrom + (gTo - gFrom) * (u * u * (3.0f - 2.0f * u)); Dl = loopDelay (f0); }
            const float a = amp.next (sr);
            if (amp.stage == 0) { active = false; return; }
            if (pluckNow) { pluck (Dl, f0, sr); pluckNow = false; }
            vt += 1.0f / sr;
            const float y0 = readKs (Dl);
            float y = ksRho * ((1.0f - ksS) * y0 + ksS * ksPrev); ksPrev = y0;
            ks[ksW] = y; ksW = (ksW + 1) & ksMask;
            float x = y;
            for (auto& b : body) x = b.run (x);
            ksTone += toneA * (x - ksTone); x = ksTone;
            if (vt < 0.004f && ! testNoNoise())
            {   // the pick: a short high-passed click (~ -20 dB)
                rnd = rnd * 1664525u + 1013904223u; const float wn = (float) (rnd >> 8) / 8388608.0f - 1.0f;
                ksHp += 0.35f * (wn - ksHp); x += (wn - ksHp) * 0.09f * vel * (1.0f - vt / 0.004f);
            }
            chA += dA; chB += dB; const float cA = readKs (chA), cB = readKs (chB);
            const float g = a * gainMul * 0.82f;
            L[i] += (x + 0.3f * cA) * g; R[i] += (x + 0.3f * cB) * g;
            if (sends != nullptr) { sends->rev[i] += x * g * 0.5f; sends->dly[i] += x * g * 0.35f; sends->used = true; }
        }
        (void) D;
    }
    float readKs (float d) const
    {   // the delay line d samples back (d >= 2), 4-point Lagrange
        const int di = (int) d; const float t = d - (float) di + 1.0f; const int m = di - 1;
        const float x0 = ks[(ksW - m) & ksMask], x1 = ks[(ksW - m - 1) & ksMask], x2 = ks[(ksW - m - 2) & ksMask], x3 = ks[(ksW - m - 3) & ksMask];
        const float h0 = -(t - 1.0f) * (t - 2.0f) * (t - 3.0f) / 6.0f, h1 = t * (t - 2.0f) * (t - 3.0f) * 0.5f, h2 = -t * (t - 1.0f) * (t - 3.0f) * 0.5f, h3 = t * (t - 1.0f) * (t - 2.0f) / 6.0f;
        return h0 * x0 + h1 * x1 + h2 * x2 + h3 * x3;
    }
    void pluck (float Dl, float f0, float sr)
    {   // (re)fill one period of the string: noise, brightness low-pass, pick-position comb, no DC, a fixed RMS; a legato re-pluck adds on top
        const bool fresh = vt <= 0.0f;
        if (fresh) for (int k = 0; k <= ksMask; ++k) ks[k] = 0.0f;
        const int len = std::min (ksMask - 4, (int) Dl + 4), comb = std::max (1, (int) std::lround (0.13f * (float) len));
        const float fc = 1200.0f + 5300.0f * std::pow (juce::jlimit (0.0f, 1.0f, vel), 1.6f), la = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * juce::jmin (fc, sr * 0.45f) / sr);
        // the string's own decay: T60 3 s (E2) .. 1.5 s (E5), per pass; damping 0.35 (a clean electric)
        const float t60 = juce::jlimit (1.2f, 3.2f, 3.0f - (pitch - 40.0f) * (1.5f / 36.0f));
        ksRho = juce::jlimit (0.9f, 0.996f, std::pow (0.001f, 1.0f / (t60 * f0)));
        ksS = 0.5f * 0.35f;
        float lp = 0;
        const int base = ksW - len;   // the newest `len` samples of the line
        float* e = sends->scratch;    // (the engine's scratch line, as long as a delay line)
        const int L = len;
        for (int k = 0; k < L; ++k) { rnd = rnd * 1664525u + 1013904223u; const float wn = (float) (rnd >> 8) / 8388608.0f - 1.0f; lp += la * (wn - lp); e[k] = lp; }
        for (int k = L - 1; k >= comb; --k) e[k] -= e[k - comb];
        double mean = 0; for (int k = 0; k < L; ++k) mean += e[k]; mean /= std::max (1, L);
        double ss = 0; for (int k = 0; k < L; ++k) { e[k] -= (float) mean; ss += (double) e[k] * e[k]; }
        const float norm = (fresh ? 0.42f : 0.25f) / (float) std::sqrt (ss / std::max (1, L) + 1e-12);
        for (int k = 0; k < L; ++k) ks[(base + k) & ksMask] += e[k] * norm;
    }
    // the filtered-saw pluck (no delay line): saw + 20 % square, two voices +-6 c, 24 dB low-pass 4.5 kHz -> 600 Hz over 260 ms
    void renderSawPluck (float* L, float* R, int n, float sr, float cutMul)
    {
        const float twoPi = juce::MathConstants<float>::twoPi; (void) twoPi;
        for (int i = 0; i < n; ++i)
        {
            if (delayIn > 0) { --delayIn; continue; }
            if (endIn > 0 && --endIn == 0) release();
            if (gLen > 0 && gPos < gLen) { ++gPos; pitch = gFrom + (gTo - gFrom) * ((float) gPos / (float) gLen); }
            const float a = amp.next (sr);
            if (amp.stage == 0) { active = false; return; }
            vt += 1.0f / sr;
            float x = 0;
            for (int o = 0; o < 2; ++o)
            {
                const float dt = mtof (pitch + shift + (o ? 0.06f : -0.06f)) / sr; float& p = ph[(size_t) o]; p += dt; if (p >= 1.0f) p -= 1.0f;
                x += (sawB (p, dt) * 0.8f + sqrB (p, dt) * 0.2f) * 0.5f;
            }
            const float env = std::exp (-vt / 0.7f) * (1.0f - std::exp (-vt / 0.001f));
            const float fc = juce::jlimit (200.0f, sr * 0.42f, (600.0f + 3900.0f * std::exp (-vt / 0.26f) * (0.6f + 0.4f * vel)) * cutMul * bright);
            const float g = svfG (fc, sr);
            const float y = fr.lp (fl.lp (x, g, 1.0f / 0.75f), g, 1.0f / 0.75f) * env * a * gainMul * 0.8f;
            L[i] += y; R[i] += y;
            if (sends != nullptr) { sends->rev[i] += y * 0.5f; sends->dly[i] += y * 0.35f; sends->used = true; }
        }
    }
    // SAD PAD: two saws +-9 cents (spread left / right, ~120 % wide) + a triangle an octave down (centred, mono below ~250 Hz), a breath
    // of air (band-passed noise, -30 dB), a 12 dB low-pass at 1.8 kHz breathing +-300 Hz at 0.12 Hz, a 0.1 % tape wow, a touch of
    // saturation; the 450 ms swell / 2.2 s release come from its envelope; a big dark hall through its own reverb send
    void renderPad (float* L, float* R, int n, float sr, float cutMul)
    {
        const PresetDef& D = def (preset);
        const float twoPi = juce::MathConstants<float>::twoPi;
        // the slow parts per block (wow 0.2 Hz, filter LFO 0.12 Hz); the pitch per sample only while it glides
        const float wow = 0.017f * std::sin (twoPi * 0.2f * vt);
        float dts[3]; auto setDt = [&] { for (int o = 0; o < 3; ++o) dts[o] = mtof (pitch + shift + wow + D.semi[o] + D.det[o] * 0.01f) / sr; };
        setDt();
        const float fc = juce::jlimit (300.0f, 9000.0f, (D.cut + 300.0f * std::sin (twoPi * 0.12f * vt + (float) (rnd & 15) * 0.3f)) * cutMul * bright);
        const float g = svfG (fc, sr), k = 1.0f / D.q, g6 = svfG (6000.0f, sr);
        for (int i = 0; i < n; ++i)
        {
            if (delayIn > 0) { --delayIn; continue; }
            if (endIn > 0 && --endIn == 0) release();
            if (gLen > 0 && gPos < gLen) { ++gPos; pitch = gFrom + (gTo - gFrom) * ((float) gPos / (float) gLen); setDt(); }
            const float a = amp.next (sr);
            if (amp.stage == 0) { active = false; return; }
            vt += 1.0f / sr;
            float xa = 0, xb = 0, xt = 0;
            for (int o = 0; o < 3; ++o)
            {
                const float dt = dts[o]; float& p = ph[(size_t) o]; p += dt; if (p >= 1.0f) p -= 1.0f;
                const float v = (o < 2 ? sawB (p, dt) : 4.0f * std::abs (p - 0.5f) - 1.0f) * D.gain[o];
                if (o == 0) xa = v; else if (o == 1) xb = v; else xt = v;
            }
            float air = 0;
            if (! testNoNoise()) { rnd = rnd * 1664525u + 1013904223u; const float wn = (float) (rnd >> 8) / 8388608.0f - 1.0f; air = (nzA.lp (wn, g6 * 1.15f, 1.4f) - nzB.lp (wn, g6 * 0.87f, 1.4f)) * 0.06f; }
            float yl = fl.lp (xa * 0.8f + xb * 0.2f + xt + air, g, k), yr = fr.lp (xa * 0.2f + xb * 0.8f + xt + air, g, k);
            yl = yl * (1.0f - 0.12f * yl * yl); yr = yr * (1.0f - 0.12f * yr * yr);   // a touch of tape saturation (the pad stays well under 1)
            const float gg = a * gainMul;
            L[i] += yl * gg; R[i] += yr * gg;
            if (sends != nullptr) { sends->rev[i] += (yl + yr) * gg * 0.6f; sends->used = true; }
        }
    }
    // 808 BASS: a sine (+ a sine an octave up at -15 dB) with a fast pitch drop (+14 st, 35 ms), a 4 ms click, a 1.2 s decay and a
    // WARM drive that only adds harmonics (the fundamental below ~80 Hz stays clean); mono (Engine::leadStart cuts the lane's previous
    // one), glides 30..120 ms, a slid note never re-triggers the envelopes. ROUND SUB: the same voice, clean, held, no click / drop.
    void render808 (float* L, float* R, int n, float sr, float cutMul)
    {
        const PresetDef& D = def (preset); const bool sub = preset == SubBass;
        if (slideFrom > 1.5f) { gLen = juce::jlimit ((int) (0.03f * sr), (int) ((sub ? 0.25f : 0.12f) * sr), gLen); slideFrom = 0; }
        const float twoPi = juce::MathConstants<float>::twoPi;
        const float drive = sub ? 0.0f : 0.22f + 0.18f * vel;   // WARM: 10..25 % of the harmonics' share
        for (int i = 0; i < n; ++i)
        {
            if (endIn > 0 && --endIn == 0) release();
            if (gLen > 0 && gPos < gLen) { ++gPos; const float u = (float) gPos / (float) gLen; pitch = gFrom + (gTo - gFrom) * (1.0f - (1.0f - u) * (1.0f - u)); }
            const float a = amp.next (sr);
            if (amp.stage == 0) { active = false; return; }
            vt += 1.0f / sr;
            const float drop = sub ? 0.0f : 14.0f * std::exp (-vt / 0.0117f);   // +14 st, down to ~0.7 st after 35 ms
            const float f0 = mtof (pitch + shift + drop), dt = f0 / sr;
            float& p0 = ph[0]; p0 += dt; if (p0 >= 1.0f) p0 -= 1.0f;
            float& p1 = ph[1]; p1 += dt * 2.0f; if (p1 >= 1.0f) p1 -= 1.0f;
            const float s0 = std::sin (twoPi * p0);
            float x = s0 * 0.86f + std::sin (twoPi * p1) * 0.15f;
            if (drive > 0.0f) x += drive * (std::tanh (2.4f * s0) / std::tanh (2.4f) - s0);   // only the added 3rd / 5th harmonics
            if (! sub && vt < 0.004f) { rnd = rnd * 1664525u + 1013904223u; const float wn = (float) (rnd >> 8) / 8388608.0f - 1.0f; clickPh += wn - clickPh * 0.6f; x += (wn - clickPh * 0.4f) * 0.22f * (1.0f - vt / 0.004f) * vel; }   // the knock
            const float fc = juce::jlimit (200.0f, 9000.0f, D.cut * cutMul * bright * 1.6f + f0 * 3.0f);
            const float g = svfG (fc, sr), k = 1.0f / D.q;
            const float y = fl.lp (x, g, k) * a * gainMul;
            L[i] += y; R[i] += y;
        }
    }
};

struct ChordVoice
{
    bool active = false, stab = false; int n = 0; std::array<float, kMaxPitches> pitch {}; std::array<float, kMaxPitches * 3> ph {};
    int endIn = 0; float vel = 0.8f; Env amp, fenv; SVF fl, fr; juce::uint64 age = 0;
    void start (const int* ps, int count, float v, int lenSamples, bool stabs, juce::uint64 a)
    {
        active = true; stab = stabs; n = juce::jmin (count, kMaxPitches); for (int i = 0; i < n; ++i) pitch[(size_t) i] = (float) ps[i];
        endIn = std::max (1, lenSamples); vel = v; age = a;
        if (stab) { amp.a = 0.003f; amp.d = 0.12f; amp.s = 0.35f; amp.r = 0.05f; } else { amp.a = 0.12f; amp.d = 0.4f / 3; amp.s = 0.75f; amp.r = 0.18f / 3; }
        amp.stage = 0; amp.level = 0; amp.on (v); fenv.a = 0.001f; fenv.d = 0.06f; fenv.s = 0; fenv.r = 0.05f; fenv.stage = 0; fenv.level = 0; fenv.on (1.0f);
        fl.reset(); fr.reset();
    }
    void render (float* L, float* R, int nS, float sr, float cutMul)
    {
        for (int i = 0; i < nS; ++i)
        {
            if (endIn > 0 && --endIn == 0) { amp.off(); fenv.off(); }
            const float a = amp.next (sr), fe = fenv.next (sr);
            if (amp.stage == 0) { active = false; return; }
            float sl = 0, srr = 0;
            const int per = stab ? 3 : 2;
            for (int k = 0; k < n; ++k)
                for (int u = 0; u < per; ++u)
                {
                    const float cents = stab ? (u == 0 ? -17.0f : u == 1 ? 0.0f : 17.0f) : (u == 0 ? -11.0f : 11.0f);
                    const float f = mtof (pitch[(size_t) k]) * std::pow (2.0f, cents / 1200.0f), dt = f / sr;
                    float& p = ph[(size_t) (k * 3 + u)]; p += dt; if (p >= 1.0f) p -= 1.0f;
                    const float v = sawB (p, dt) * (stab ? 0.15f : 0.10f);
                    const float pan = stab ? (u == 0 ? -0.8f : u == 1 ? 0.0f : 0.8f) : (u == 0 ? -0.35f : 0.35f);
                    sl += v * (1.0f - pan) * 0.5f * 2.0f * 0.7f; srr += v * (1.0f + pan) * 0.5f * 2.0f * 0.7f;
                }
            const float fc = juce::jlimit (60.0f, 16000.0f, (stab ? 1800.0f + 5200.0f * fe : 1100.0f) * cutMul);
            const float g = svfG (fc, sr), kq = 1.0f / 0.6f;
            L[i] += fl.lp (sl, g, kq) * a; R[i] += fr.lp (srr, g, kq) * a;
        }
    }
};

struct BassVoice
{
    bool active = false; float pitch = 36, ph1 = 0, ph2 = 0; int endIn = 0; Env amp; SVF f;
    void start (int p, float v, int len) { active = true; pitch = (float) p; endIn = std::max (1, len); amp.a = 0.006f; amp.d = 0.08f; amp.s = 0.64f; amp.r = 0.025f; amp.stage = 0; amp.level = 0; amp.on (0.5f * v); f.reset(); }
    void render (float* L, float* R, int n, float sr)
    {
        for (int i = 0; i < n; ++i)
        {
            if (endIn > 0 && --endIn == 0) amp.off();
            const float a = amp.next (sr); if (amp.stage == 0) { active = false; return; }
            const float fr = mtof (pitch), dt = fr / sr;
            ph1 += dt; if (ph1 >= 1) ph1 -= 1; ph2 += dt; if (ph2 >= 1) ph2 -= 1;
            const float x = std::sin (juce::MathConstants<float>::twoPi * ph1) * 0.8f + sawB (ph2, dt) * 0.28f;
            const float y = f.lp (x, svfG (420.0f, sr), 1.0f) * a;
            L[i] += y; R[i] += y;
        }
    }
};

struct Kick
{
    float t = 1e9f, ph = 0;
    void hit() { t = 0; ph = 0; }
    void render (float* L, float* R, int n, float sr, float level)
    {
        for (int i = 0; i < n; ++i)
        {
            if (t > 0.42f) return;
            const float f = 46.0f + (150.0f - 46.0f) * std::exp (-t / 0.03f);
            ph += f / sr; if (ph >= 1) ph -= 1;
            const float env = std::exp (-t / 0.11f) * std::min (1.0f, t / 0.002f);
            const float y = std::sin (juce::MathConstants<float>::twoPi * ph) * env * 0.9f * level;
            L[i] += y; R[i] += y; t += 1.0f / sr;
        }
    }
};

struct Params   // read once per block on the audio thread
{
    int preset = Neon; bool layerPluck = false, layerOct = false, stabs = false; float cutoff = 0.6f, lfoDepth = 0.3f; int lfoRate = 3;   // 1/4 1/2 1 bar 2 bars 4 bars
    float delay = 0.35f, reverb = 0.3f, pump = 0.25f, volume = 0.8f; bool kick = false;
    float laneGain[5] { 1, 1, 1, 1, 1 };   // per-lane volume (linear): chords, arp, melody, bass, kick (smoothed per block)
    int sampleMask = 0; bool auditionSample = false;   // YOUR SAMPLE: the lanes it plays (bits: chords arp melody bass) + the note audition
    int lanePreset[4] { -1, -1, -1, -1 };             // a lane's own factory sound (-1 = LINKED: the shared sound / the classic chord pad + bass)
    // LANE FX (chords, arp, melody, bass): DRIVE 0..1, TONE -1 (darker) .. 0 (off) .. +1 (thinner), ECHO + SPACE = extra sends into the
    // shared delay / reverb. All 0 = the lanes exactly as before (the code path is skipped).
    float fxDrive[4] {}, fxTone[4] {}, fxEcho[4] {}, fxSpace[4] {};
    bool laneFxOn() const { for (int k = 0; k < 4; ++k) if (fxDrive[k] != 0.0f || fxTone[k] != 0.0f || fxEcho[k] != 0.0f || fxSpace[k] != 0.0f) return true; return false; }
};

class Engine
{
public:
    void prepare (double sampleRate, int maxBlock)
    {
        sr = (float) sampleRate; pumpK = 1.0f - std::exp (-1.0f / (0.001f * sr)); pumpG = 1.0f;
        const int dl = (int) (sampleRate * 2.2) + 8;
        dL.assign ((size_t) dl, 0.0f); dR.assign ((size_t) dl, 0.0f); dPos = 0;
        busL.assign ((size_t) maxBlock, 0.0f); busR.assign ((size_t) maxBlock, 0.0f);
        chL.assign ((size_t) maxBlock, 0.0f); chR.assign ((size_t) maxBlock, 0.0f);
        bsL.assign ((size_t) maxBlock, 0.0f); bsR.assign ((size_t) maxBlock, 0.0f);
        verb.setSampleRate (sampleRate); juce::Reverb::Parameters rp; rp.roomSize = 0.78f; rp.damping = 0.45f; rp.wetLevel = 1.0f; rp.dryLevel = 0.0f; rp.width = 1.0f; verb.setParameters (rp);
        vL.assign ((size_t) maxBlock, 0.0f); vR.assign ((size_t) maxBlock, 0.0f);
        eL.assign ((size_t) maxBlock, 0.0f); eR.assign ((size_t) maxBlock, 0.0f); sL.assign ((size_t) maxBlock, 0.0f); sR.assign ((size_t) maxBlock, 0.0f);
        for (auto& z : toneZ) z = 0.0f;
        mL.assign ((size_t) maxBlock, 0.0f); mR.assign ((size_t) maxBlock, 0.0f);
        {   // CLEAN GUITAR PLUCK: one delay line per voice (down to ~36 Hz + the chorus taps), power of two; SAD PAD / GUITAR sends per lane
            int cap = 64; while (cap < (int) (sampleRate / 36.0 + sampleRate * 0.02) + 16) cap <<= 1;
            ksCap = cap; ksPool.assign ((size_t) cap * (size_t) (kLead + 4 * 24), 0.0f); ksScratch.assign ((size_t) cap, 0.0f);
            for (int k = 0; k < 5; ++k)
            {
                xsRev[(size_t) k].assign ((size_t) maxBlock, 0.0f); xsDly[(size_t) k].assign ((size_t) maxBlock, 0.0f);
                sends[(size_t) k].rev = xsRev[(size_t) k].data(); sends[(size_t) k].dly = xsDly[(size_t) k].data(); sends[(size_t) k].scratch = ksScratch.data(); sends[(size_t) k].used = false;
            }
            xsLp = 0; strumAt.fill (~(juce::uint64) 0); strumN.fill (0); clock = 0;
        }
        gInit = false;
        smp.prepare (sampleRate, maxBlock);
        allOff (true);
    }
    // YOUR SAMPLE (audio thread, once per block): the current sample (nullptr / empty = none) and its settings
    void setSample (const arpsmp::SampleData* d, const arpsmp::Params& p) { if (d != sdata) { smp.dataChanged(); sdata = d; } sparams = p; }
    int sampleVoices() const { return smp.activeCount(); }
    void allOff (bool hard)
    {
        for (auto& v : lead) { if (hard) v.active = false; else v.release(); }
        for (auto& v : chords) { if (hard) v.active = false; else { v.amp.off(); v.fenv.off(); } }
        for (auto& v : bass) { if (hard) v.active = false; else v.amp.off(); }
        if (hard) { std::fill (dL.begin(), dL.end(), 0.0f); std::fill (dR.begin(), dR.end(), 0.0f); verb.reset(); }
        for (auto& b : laneBank) for (auto& v : b) { if (hard) v.active = false; else v.release(); }
        smp.allOff (hard);
    }
    // ---- events (audio thread)
    // energy (0..1, arp EVOLVE GROW / WAVE / JOURNEY; < 0 = none): darker + drier + no octave sparkle when low, open + wide when high
    void leadStart (int lane, int chain, float pitch, float vel, int lenSamples, const Params& P, float energy = -1.0f, float fat = 0.0f, int idx = -1)
    {
        const bool e = energy >= 0.0f;
        const float br = e ? std::pow (2.0f, (energy - 0.85f) * 3.2f) : 1.0f, envK = e ? 0.4f + 0.7f * energy : 1.0f;
        if (lane == 1) arpEnergy = e ? energy : -1.0f;
        if (useSample (lane, P)) { smp.noteOn (lane, chain, pitch, vel, lenSamples, sparams, *sdata, br, fat, idx); return; }   // YOUR SAMPLE plays this lane
        const int own = lane >= 0 && lane < 4 ? P.lanePreset[lane] : -1;   // a sound of the lane's own (its own voice bank) or the shared preview sound
        const int pre = own >= 0 ? own : P.preset;
        if (pre == Bass808 || pre == SubBass)
        {   // mono: the lane's previous 808 stops fast (8 ms) when the next one starts (a glide is a legato, not a new start)
            if (own >= 0) { for (auto& v : laneBank[(size_t) lane]) if (v.active && (v.preset == Bass808 || v.preset == SubBass)) { v.amp.r = 0.008f / 3.0f; v.release(); } }
            else for (auto& v : lead) if (v.active && v.lane == lane && (v.preset == Bass808 || v.preset == SubBass)) { v.amp.r = 0.008f / 3.0f; v.release(); }
        }
        auto go = [&] (int preset, float shift, float gain, int chainId)
        {
            LeadVoice& v = own >= 0 ? freeIn (laneBank[(size_t) lane]) : freeLead();
            v.start (preset, pitch, vel, lenSamples, lane, chainId, ++ageC, sr); v.shift = shift; v.gainMul = gain; v.bright = br; v.envAmt *= envK;
            v.sends = nullptr; v.ks = nullptr;
            if ((preset == GuitarPluck || preset == SadPad) && ksCap > 0)
            {
                const int li = lane >= 0 && lane < 4 ? lane : 4;
                v.sends = &sends[(size_t) li];
                if (preset == GuitarPluck)
                {   // its own delay line; notes that start together on a lane strum (low -> high as they arrive, ~18 ms apart)
                    const size_t idx = own >= 0 ? (size_t) kLead + (size_t) lane * 24 + (size_t) (&v - laneBank[(size_t) lane].data()) : (size_t) (&v - lead.data());
                    v.ks = ksPool.data() + idx * (size_t) ksCap; v.ksMask = ksCap - 1; v.ksW = 0;
                    if (strumAt[(size_t) li] == clock) ++strumN[(size_t) li]; else { strumAt[(size_t) li] = clock; strumN[(size_t) li] = 0; }
                    v.delayIn = std::min (5, strumN[(size_t) li]) * (int) (0.018f * sr);
                }
            }
        };
        go (pre, 0, 1.0f, chain);
        if (pre == Euphoric) go (Piano, 0, 0.42f, chain);   // SUNRISE STACK: the piano's attack inside the saws
        if (P.layerPluck && pre != Pluck) go (Pluck, 0, 0.55f, chain);
        if (P.layerOct) go (pre == Piano ? Piano : Pluck, 12, 0.45f * (e ? 0.15f + 0.85f * energy : 1.0f), chain);
    }
    void leadRelease (int chain) { for (auto& v : lead) if (v.active && v.chain == chain) v.release(); for (auto& b : laneBank) for (auto& v : b) if (v.active && v.chain == chain) v.release(); smp.release (chain); }
    void leadLegato (int chain, float pitch, float vel, float fat = -1.0f) { for (auto& v : lead) if (v.active && v.chain == chain) v.legato (pitch, vel); for (auto& b : laneBank) for (auto& v : b) if (v.active && v.chain == chain) v.legato (pitch, vel); smp.legato (chain, pitch, vel, fat); }
    void leadGlide (int chain, float to, int lenSamples) { for (auto& v : lead) if (v.active && v.chain == chain) v.glide (to, lenSamples); for (auto& b : laneBank) for (auto& v : b) if (v.active && v.chain == chain) v.glide (to, lenSamples); smp.glide (chain, to, lenSamples); }
    bool hasChain (int chain) const { for (auto& v : lead) if (v.active && v.chain == chain) return true; for (auto& b : laneBank) for (auto& v : b) if (v.active && v.chain == chain) return true; return smp.hasChain (chain); }
    void chord (const int* ps, int n, float vel, int lenSamples, bool stabs, const Params* P = nullptr, int idx = -1)
    {
        if (P != nullptr && useSample (0, *P)) { for (int k = 0; k < n; ++k) smp.noteOn (0, -2, (float) ps[k], vel * 0.85f, lenSamples, sparams, *sdata, 1.0f, 0.0f, idx); return; }
        if (P != nullptr && P->lanePreset[0] >= 0) { for (int k = 0; k < n; ++k) leadStart (0, -2, (float) ps[k], vel * 0.8f, lenSamples, *P); return; }   // the chords' own sound
        ChordVoice* best = &chords[0];
        for (auto& v : chords) { if (! v.active) { best = &v; break; } if (v.age < best->age) best = &v; }
        best->start (ps, n, vel, lenSamples, stabs, ++ageC);
    }
    void bassNote (int p, float vel, int lenSamples, const Params* P = nullptr, int idx = -1)
    {
        if (P != nullptr && useSample (3, *P)) { smp.noteOn (3, -3, (float) p, vel, lenSamples, sparams, *sdata, 1.0f, 0.0f, idx); return; }
        if (P != nullptr && P->lanePreset[3] >= 0) { leadStart (3, -3, (float) p, vel, lenSamples, *P); return; }   // the bass's own sound
        BassVoice* best = &bass[0]; for (auto& v : bass) if (! v.active) { best = &v; break; }
        best->start (p, vel, lenSamples);
    }
    void kickHit() { kick.hit(); }

    // ---- render: beatPhase / lfoPhase come from the transport (0..1), playing = transport running
    void render (float* outL, float* outR, int n, const Params& P, double bpm, double tickStart, double ticksPerSample, bool playing)
    {   // in chunks of the prepared block size (hosts sometimes send bigger blocks than they announced)
        const int maxBlk = juce::jmax (1, (int) busL.size());
        for (int done = 0; done < n;)
        {
            const int k = juce::jmin (n - done, maxBlk);
            renderChunk (outL + done, outR + done, k, P, bpm, tickStart + done * ticksPerSample, ticksPerSample, playing);
            done += k;
        }
    }
    int activeVoices() const { int c = 0; for (auto& v : lead) c += v.active; for (auto& v : chords) c += v.active; for (auto& v : bass) c += v.active; return c; }

private:
    void renderChunk (float* outL, float* outR, int n, const Params& P, double bpm, double tickStart, double ticksPerSample, bool playing)
    {
        std::fill (busL.begin(), busL.begin() + n, 0.0f); std::fill (busR.begin(), busR.begin() + n, 0.0f);
        std::fill (chL.begin(), chL.begin() + n, 0.0f); std::fill (chR.begin(), chR.begin() + n, 0.0f);
        std::fill (bsL.begin(), bsL.begin() + n, 0.0f); std::fill (bsR.begin(), bsR.begin() + n, 0.0f);
        std::fill (mL.begin(), mL.begin() + n, 0.0f); std::fill (mR.begin(), mR.begin() + n, 0.0f);
        for (size_t k = 0; k < sends.size(); ++k) if (sendsDirty[k]) { std::fill (xsRev[k].begin(), xsRev[k].end(), 0.0f); std::fill (xsDly[k].begin(), xsDly[k].end(), 0.0f); sendsDirty[k] = false; }
        for (auto& s : sends) s.used = false;
        static const double lfoBeats[] { 1, 2, 4, 8, 16 };
        const double cyc = lfoBeats[juce::jlimit (0, 4, P.lfoRate)] * 96.0;
        const double ph = std::fmod (std::max (0.0, tickStart), cyc) / cyc;
        const float lfoOct = P.lfoDepth * 2.2f * (float) std::sin (juce::MathConstants<double>::twoPi * ph);
        const float cutMul = std::pow (2.0f, (P.cutoff - 0.6f) * 6.0f);
        std::fill (vL.begin(), vL.begin() + n, 0.0f); std::fill (vR.begin(), vR.begin() + n, 0.0f);   // (audition bus until the reverb send)
        for (auto& v : lead) if (v.active) { float* l = v.lane == 2 ? mL.data() : v.lane == 1 ? busL.data() : vL.data(); float* r = v.lane == 2 ? mR.data() : v.lane == 1 ? busR.data() : vR.data(); v.render (l, r, n, sr, cutMul, playing ? lfoOct : 0.0f); }
        {   // the lanes' own sounds (own banks, own buses)
            float* Ls[4] = { chL.data(), busL.data(), mL.data(), bsL.data() }; float* Rs[4] = { chR.data(), busR.data(), mR.data(), bsR.data() };
            for (int b = 0; b < 4; ++b) for (auto& v : laneBank[(size_t) b]) if (v.active) v.render (Ls[b], Rs[b], n, sr, cutMul, playing ? lfoOct : 0.0f);
        }
        for (auto& v : chords) if (v.active) v.render (chL.data(), chR.data(), n, sr, std::pow (2.0f, (P.cutoff - 0.6f) * 2.5f));
        for (auto& v : bass) if (v.active) v.render (bsL.data(), bsR.data(), n, sr);
        {   // YOUR SAMPLE: into the lanes' own buses (lane volume, delay, reverb, pump and the ENERGY space apply as for the synth)
            float* Ls[5] = { chL.data(), busL.data(), mL.data(), bsL.data(), vL.data() }; float* Rs[5] = { chR.data(), busR.data(), mR.data(), bsR.data(), vR.data() };
            smp.render (Ls, Rs, n, sparams, sdata);
        }
        bool xsend = false; float xg[5] { 1, 1, 1, 1, 1 };   // CLEAN GUITAR PLUCK / SAD PAD sends (scaled by the lane volume below)
        for (size_t k = 0; k < sends.size(); ++k) if (sends[k].used) { sendsDirty[k] = true; xsend = true; }
        clock += (juce::uint64) n;
        const bool lfx = P.laneFxOn();
        bool lsend = false;
        if (lfx)
        {   // LANE FX: drive + tone on each lane's bus (before its volume), the echo / space sends collected after the volume
            float* Ls[4] = { chL.data(), busL.data(), mL.data(), bsL.data() }; float* Rs[4] = { chR.data(), busR.data(), mR.data(), bsR.data() };
            for (int b = 0; b < 4; ++b)
            {
                const float d = juce::jlimit (0.0f, 1.0f, P.fxDrive[b]), t = juce::jlimit (-1.0f, 1.0f, P.fxTone[b]);
                lsend |= P.fxEcho[b] > 0.0f || P.fxSpace[b] > 0.0f;
                if (d > 0.0f)
                {
                    const float k = 1.0f + d * 11.0f, mk = 1.0f / std::sqrt (k);
                    for (int i = 0; i < n; ++i) { Ls[b][i] = std::tanh (Ls[b][i] * k) * mk; Rs[b][i] = std::tanh (Rs[b][i] * k) * mk; }
                }
                if (t != 0.0f)
                {   // one pole: below 0 a low-pass down to 400 Hz, above 0 a high-pass up to 2 kHz
                    const float fc = t < 0.0f ? 20000.0f * std::pow (0.02f, -t) : 20.0f * std::pow (100.0f, t);
                    const float a = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * juce::jmin (fc, sr * 0.45f) / sr);
                    float& zl = toneZ[(size_t) b * 2]; float& zr = toneZ[(size_t) b * 2 + 1];
                    for (int i = 0; i < n; ++i)
                    {
                        zl += a * (Ls[b][i] - zl); zr += a * (Rs[b][i] - zr);
                        if (t < 0.0f) { Ls[b][i] = zl; Rs[b][i] = zr; } else { Ls[b][i] -= zl; Rs[b][i] -= zr; }
                    }
                }
            }
            if (lsend) { std::fill (eL.begin(), eL.begin() + n, 0.0f); std::fill (eR.begin(), eR.begin() + n, 0.0f); std::fill (sL.begin(), sL.begin() + n, 0.0f); std::fill (sR.begin(), sR.begin() + n, 0.0f); }
        }
        {   // per-lane volume, ramped over the block (no zipper). Lead voices of other lanes (audition) stay at unity.
            float g0[5], g1[5];
            for (int k = 0; k < 5; ++k) { g1[k] = juce::jlimit (0.0f, 2.0f, P.laneGain[k]); g0[k] = gInit ? gCur[k] : g1[k]; gCur[k] = g1[k]; }
            gInit = true;
            for (int k = 0; k < 4; ++k) xg[k] = g1[k];
            const float inv = 1.0f / (float) juce::jmax (1, n);
            for (int i = 0; i < n; ++i)
            {
                const float u = (float) (i + 1) * inv; auto g = [&] (int k) { return g0[k] + (g1[k] - g0[k]) * u; };
                if (lsend)
                {   // (the lanes' echo / space sends, after their volume)
                    const float cl = chL[(size_t) i] * g (0), cr = chR[(size_t) i] * g (0), al = busL[(size_t) i] * g (1), ar = busR[(size_t) i] * g (1);
                    const float ml = mL[(size_t) i] * g (2), mr = mR[(size_t) i] * g (2), bl = bsL[(size_t) i] * g (3), br = bsR[(size_t) i] * g (3);
                    eL[(size_t) i] = cl * P.fxEcho[0] + al * P.fxEcho[1] + ml * P.fxEcho[2] + bl * P.fxEcho[3]; eR[(size_t) i] = cr * P.fxEcho[0] + ar * P.fxEcho[1] + mr * P.fxEcho[2] + br * P.fxEcho[3];
                    sL[(size_t) i] = cl * P.fxSpace[0] + al * P.fxSpace[1] + ml * P.fxSpace[2] + bl * P.fxSpace[3]; sR[(size_t) i] = cr * P.fxSpace[0] + ar * P.fxSpace[1] + mr * P.fxSpace[2] + br * P.fxSpace[3];
                }
                chL[(size_t) i] *= g (0); chR[(size_t) i] *= g (0);
                bsL[(size_t) i] *= g (3); bsR[(size_t) i] *= g (3);
                busL[(size_t) i] = busL[(size_t) i] * g (1) + mL[(size_t) i] * g (2) + vL[(size_t) i]; busR[(size_t) i] = busR[(size_t) i] * g (1) + mR[(size_t) i] * g (2) + vR[(size_t) i];
            }
        }
        // delay (lead): dotted 1/8 ping-pong with a darkening feedback loop
        const int dlen = (int) dL.size();
        const int dt = juce::jlimit (1, dlen - 2, (int) std::lround (60.0 / juce::jlimit (40.0, 300.0, bpm) * 0.75 * sr));
        const float send = P.delay * 0.55f * (arpEnergy >= 0.0f ? 0.35f + 0.65f * arpEnergy : 1.0f), fb = 0.25f + P.delay * 0.3f;   // the arp EVOLVE curve opens the delay too
        const float lpA = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 3200.0f / sr);
        for (int i = 0; i < n; ++i)
        {
            int rp = dPos - dt; if (rp < 0) rp += dlen;
            const float yl = dL[(size_t) rp], yr = dR[(size_t) rp];
            fbL += (yr - fbL) * lpA; fbR += (yl - fbR) * lpA;
            dL[(size_t) dPos] = (busL[(size_t) i] + busR[(size_t) i]) * 0.5f * send + fbL * fb;
            if (xsend) for (size_t k = 0; k < 5; ++k) if (sendsDirty[k]) dL[(size_t) dPos] += xsDly[k][(size_t) i] * xg[k] * send;   // the guitar's own echo
            if (lsend) dL[(size_t) dPos] += (eL[(size_t) i] + eR[(size_t) i]) * 0.5f * 0.6f;   // LANE FX ECHO
            dR[(size_t) dPos] = fbR * fb;
            if (++dPos >= dlen) dPos = 0;
            busL[(size_t) i] += yl * 0.85f; busR[(size_t) i] += yr * 0.85f;
        }
        // reverb send (lead + chords)
        const float rs = P.reverb * 0.55f;
        for (int i = 0; i < n; ++i) { vL[(size_t) i] = (busL[(size_t) i] + chL[(size_t) i] * 0.7f) * rs; vR[(size_t) i] = (busR[(size_t) i] + chR[(size_t) i] * 0.7f) * rs; }
        if (lsend) for (int i = 0; i < n; ++i) { vL[(size_t) i] += sL[(size_t) i] * 0.6f; vR[(size_t) i] += sR[(size_t) i] * 0.6f; }   // LANE FX SPACE
        if (xsend)
        {   // the guitar / pad's own hall, darkened (one pole ~2.8 kHz)
            const float a = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * 2800.0f / sr);
            for (int i = 0; i < n; ++i)
            {
                float x = 0; for (size_t k = 0; k < 5; ++k) if (sendsDirty[k]) x += xsRev[k][(size_t) i] * xg[k];
                xsLp += a * (x - xsLp); vL[(size_t) i] += xsLp * rs; vR[(size_t) i] += xsLp * rs;
            }
        }
        verb.processStereo (vL.data(), vR.data(), n);
        const float vol = P.volume * P.volume * 1.4f;
        for (int i = 0; i < n; ++i)
        {
            float gT = 1.0f;
            if (playing && P.pump > 0.001f)
            {   // quarter-note duck: down at each beat, back up with ~0.17 beat time constant
                // (2026-11-13: the dip glides in over 6 ms instead of dropping in one sample: that drop was a click on every beat)
                const double beatPos = std::fmod (std::max (0.0, tickStart + ticksPerSample * i), 96.0) / 96.0;
                const double beatSecs = 60.0 / bpm, secs = beatPos * beatSecs, tc = 0.17 * beatSecs;
                const double tail = std::exp (-std::max (0.0, secs - 0.012) / tc), prevTail = std::exp (-std::max (0.0, beatSecs - 0.012) / tc);
                const double att = juce::jlimit (0.0, 1.0, secs / 0.006), sm = att * att * (3.0 - 2.0 * att);
                gT = 1.0f - 0.72f * P.pump * (float) (sm * tail + (1.0 - sm) * prevTail);
            }
            pumpG += (gT - pumpG) * pumpK;   // + a 1 ms smoother: turning PUMP, starting or stopping never steps the level
            const float g = pumpG;
            float l = (busL[(size_t) i] + chL[(size_t) i] + bsL[(size_t) i] + vL[(size_t) i]) * g;
            float r = (busR[(size_t) i] + chR[(size_t) i] + bsR[(size_t) i] + vR[(size_t) i]) * g;
            outL[i] = l * vol; outR[i] = r * vol;
        }
        if (P.kick) kick.render (outL, outR, n, sr, vol * gCur[4]);
        for (int i = 0; i < n; ++i) { outL[i] = std::tanh (outL[i] * 0.9f) / 0.9f; outR[i] = std::tanh (outR[i] * 0.9f) / 0.9f; }
    }
    template <size_t N> LeadVoice& freeIn (std::array<LeadVoice, N>& bank)
    {   // a lane's own bank: it only ever steals from itself
        LeadVoice* best = &bank[0];
        for (auto& v : bank) { if (! v.active) return v; if (v.age < best->age) best = &v; }
        return *best;
    }
    LeadVoice& freeLead()
    {
        LeadVoice* best = &lead[0];
        for (auto& v : lead) { if (! v.active) return v; if (v.age < best->age) best = &v; }
        return *best;
    }
    float sr = 44100;
    std::array<LeadVoice, kLead> lead; std::array<ChordVoice, kChord> chords; std::array<BassVoice, kBass> bass; Kick kick;
    std::vector<float> dL, dR, busL, busR, chL, chR, bsL, bsR, vL, vR, mL, mR; std::vector<float> eL, eR, sL, sR; std::array<float, 8> toneZ {}; /* LANE FX sends + tone filter states */ int dPos = 0; float fbL = 0, fbR = 0; float gCur[5] { 1, 1, 1, 1, 1 }; bool gInit = false;
    juce::Reverb verb; juce::uint64 ageC = 0;
    arpsmp::Sampler smp; const arpsmp::SampleData* sdata = nullptr; arpsmp::Params sparams;
    std::array<std::array<LeadVoice, 24>, 4> laneBank;   // a lane's own sound: its own voices (chords, arp, melody, bass)
    bool useSample (int lane, const Params& P) const { return sdata != nullptr && sdata->length() >= 16 && (lane == 9 ? P.auditionSample : ((P.sampleMask >> lane) & 1) != 0); }
    float pumpG = 1.0f, pumpK = 0.02f;   // PUMP's gain, smoothed (pumpK set in prepare: ~1 ms)
    float arpEnergy = -1.0f;   // the last arp note's EVOLVE energy (delay send), -1 = none
    std::vector<float> ksPool, ksScratch; int ksCap = 0; std::array<std::vector<float>, 5> xsRev, xsDly; std::array<LeadVoice::Sends, 5> sends {}; std::array<bool, 5> sendsDirty {}; float xsLp = 0;
    juce::uint64 clock = 0; std::array<juce::uint64, 5> strumAt {}; std::array<int, 5> strumN {};
};
} // namespace arpsyn
