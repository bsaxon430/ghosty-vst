#pragma once
// Ghosty Beats 2 engine. Real-time safe: no allocation, no locks, no string lookups in process().
// Timing is in beats (host ppq when the DAW plays, an internal clock otherwise); 16 steps = 1 bar.
#include "BeatModel.h"
#include <juce_dsp/juce_dsp.h>

namespace gb {

struct EngineParams {
    float swing = 0, human = 0, glitch = 0, filter = 0.5f, res = 0.2f, echo = 0, verb = 0, crush = 0, drive = 0, glue = 0, volume = 0.8f;
    bool songMode = false; int pattern = 0; float bpm = 140;
    int stutter = 0; bool tape = false;             // performance (held buttons)
    bool padRepeat = false; int repeatDiv = 2; bool recording = false;
};

struct PadEvent { int track = 0; float vel = 1; };
struct RecEvent { int pattern = 0, track = 0, step = 0; uint8_t vel = 100; };

class BeatEngine {
public:
    static constexpr int kVoices = 64;

    void prepare(double rate, int maxBlock) {
        sr = rate;
        trackBuf.setSize(kTracks * 2, juce::jmax(64, maxBlock));
        for (auto& v : voices) v = Voice();
        juce::dsp::ProcessSpec spec { rate, (juce::uint32) juce::jmax(64, maxBlock), 2 };
        reverb.prepare(spec);
        echoL.assign((size_t) (rate * 2.5), 0.0f); echoR = echoL; ePos = 0;
        ring.assign((size_t) (rate * 4.0), 0.0f); ring2 = ring; rPos = 0;
        stutBuf.assign((size_t) (rate * 2.5), 0.0f); stutBuf2 = stutBuf;
        for (auto& f : trackFilt) f = SVF();
        clock = 0; lastStepUi = -1;
    }
    void setSnapshot(const Snapshot* s) { snap.store(s); }

    // ui feedback
    std::atomic<int> uiStep { -1 }, uiPattern { 0 }, uiBar { 0 };
    std::atomic<uint32_t> trig[kTracks] {};
    std::atomic<uint32_t> heldPads { 0 };
    bool internalPlay = false;

    // pad events in (UI -> audio), recorded steps out (audio -> UI)
    juce::AbstractFifo padFifo { 256 }; PadEvent padBuf[256];
    juce::AbstractFifo recFifo { 512 }; RecEvent recBuf[512];
    void pushPad(int track, float vel) { int a, b, c, d; padFifo.prepareToWrite(1, a, b, c, d); if (b > 0) { padBuf[a] = { track, vel }; padFifo.finishedWrite(1); } }

    void process(juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const EngineParams& P, double hostBpm, double hostPpq, bool hostPlaying) {
        const int n = out.getNumSamples();
        out.clear();
        const Snapshot* S = snap.load();
        const double bpm = hostPlaying ? hostBpm : P.bpm;
        const double spb = sr * 60.0 / juce::jmax(20.0, bpm);
        const bool running = hostPlaying || internalPlay;
        double b0 = hostPlaying ? hostPpq : clock;
        if (hostPlaying) clock = hostPpq;

        nHits = 0;
        // live MIDI (notes 36..51 = tracks 1..16, the MPC layout)
        for (const auto md : midi) { const auto m = md.getMessage(); if (m.isNoteOn()) { const int t = m.getNoteNumber() - 36; if (juce::isPositiveAndBelow(t, kTracks)) addHit(t, m.getFloatVelocity(), md.samplePosition); } }
        // pads from the UI (MPC page): play now, and record into the pattern while running
        { int a, b, c, d; const int k = padFifo.getNumReady(); padFifo.prepareToRead(k, a, b, c, d);
          auto take = [&](int idx) { const auto& e = padBuf[idx]; addHit(e.track, e.vel, 0);
              if (P.recording && running && S) {
                  const long step = (long) std::llround(b0 * 4.0); int pat = P.pattern, ps = 0;
                  if (resolve(S, P, step, pat, ps)) { int w1, w2, w3, w4; recFifo.prepareToWrite(1, w1, w2, w3, w4); if (w2 > 0) { recBuf[w1] = { pat, e.track, ps, (uint8_t) juce::jlimit(1, 127, (int) (e.vel * 127)) }; recFifo.finishedWrite(1); } }
              } };
          for (int i = 0; i < b; ++i) take(a + i); for (int i = 0; i < d; ++i) take(c + i); padFifo.finishedRead(b + d); }

        const double b1 = b0 + n / spb;
        if (running && S) {
            schedule(S, P, b0, b1, spb);
            // note repeat on held pads
            if (P.padRepeat && heldPads.load()) {
                static const double D[] = { 0.5, 0.25, 0.125, 1.0 / 6.0 };   // 1/8, 1/16, 1/32, 1/16T
                const double q = D[juce::jlimit(0, 3, P.repeatDiv)];
                for (long k = (long) std::ceil(b0 / q); k * q < b1; ++k) {
                    const int off = (int) ((k * q - b0) * spb); const uint32_t mask = heldPads.load();
                    for (int t = 0; t < kTracks; ++t) if (mask & (1u << t)) addHit(t, 0.85f, off);
                }
            }
            const long st = (long) std::floor(b0 * 4.0); int pat = P.pattern, ps = 0;
            if (resolve(S, P, st, pat, ps)) { uiStep.store(ps); uiPattern.store(pat); } else uiStep.store(-1);
            uiBar.store((int) (st / 16));
            if (!hostPlaying) clock = b1;
        } else uiStep.store(-1);

        // sort the few hits by time, then render sample by sample
        std::sort(hits, hits + nHits, [](const Hit& a, const Hit& b) { return a.offset < b.offset; });
        trackBuf.clear(0, n);
        int h = 0;
        for (int i = 0; i < n; ++i) {
            while (h < nHits && hits[h].offset <= i) { startVoice(S, hits[h]); ++h; }
            for (auto& v : voices) {
                if (!v.data) continue;
                float x;
                if (v.reverse) { const double p = v.len - 1 - v.pos; if (p < 0) { v.data = nullptr; continue; } x = interp(v.data, v.len, p); }
                else { if (v.pos >= v.len - 1) { v.data = nullptr; continue; } x = interp(v.data, v.len, v.pos); }
                v.pos += v.rate;
                v.env *= v.envK;
                float g = v.env * v.gain;
                if (v.fade >= 0) { g *= v.fade / (float) kFade; if (--v.fade < 0) { v.data = nullptr; continue; } }
                trackBuf.addSample(v.track * 2, i, x * g * v.panL);
                trackBuf.addSample(v.track * 2 + 1, i, x * g * v.panR);
            }
        }
        // per-track tone (one knob: low-pass <- 0.5 -> high-pass) + drive, then mix
        float* L = out.getWritePointer(0); float* R = out.getWritePointer(1);
        for (int t = 0; t < kTracks; ++t) {
            if (!S) break;
            const auto& tr = S->tracks[(size_t) t];
            const bool audible = !tr.mute && (!S->anySolo || tr.solo);
            float* tl = trackBuf.getWritePointer(t * 2); float* trr = trackBuf.getWritePointer(t * 2 + 1);
            const float tone = tr.tone; auto& F = trackFilt[t];
            const bool filt = std::abs(tone - 0.5f) > 0.02f;
            if (filt) F.setup(tone < 0.5f ? 150.0f * std::pow(130.0f, tone * 2.0f) : 30.0f * std::pow(130.0f, (tone - 0.5f) * 2.0f), 0.25f, (float) sr);
            const float dg = 1.0f + tr.drive * tr.drive * 20.0f, dc = 1.0f / (1.0f + tr.drive * 1.6f);
            for (int i = 0; i < n; ++i) {
                float a = tl[i], b = trr[i];
                if (filt) { a = F.tick(0, a, tone < 0.5f ? 0 : 2); b = F.tick(1, b, tone < 0.5f ? 0 : 2); }
                if (tr.drive > 0.01f) { a = std::tanh(a * dg) * dc; b = std::tanh(b * dg) * dc; }
                if (audible) { L[i] += a; R[i] += b; }
            }
        }
        master(out, P, spb);
    }

private:
    struct Voice { const float* data = nullptr; int len = 0; double pos = 0, rate = 1; float gain = 1, env = 1, envK = 1, panL = 0.7f, panR = 0.7f; int track = 0, fade = -1; bool reverse = false; uint32_t age = 0; };
    struct Hit { int track = 0; float vel = 1; int offset = 0; };
    struct SVF {
        float s1[2] {}, s2[2] {}, a1 = 0, a2 = 0, a3 = 0, k = 1;
        void setup(float f, float q, float rate) { const float g = std::tan(juce::MathConstants<float>::pi * juce::jlimit(20.0f, rate * 0.45f, f) / rate); k = 2.0f - 1.9f * q; a1 = 1 / (1 + g * (g + k)); a2 = g * a1; a3 = g * a2; }
        float tick(int c, float x, int type) { const float v3 = x - s2[c], v1 = a1 * s1[c] + a2 * v3, v2 = s2[c] + a2 * s1[c] + a3 * v3; s1[c] = 2 * v1 - s1[c]; s2[c] = 2 * v2 - s2[c]; return type == 0 ? v2 : (type == 1 ? v1 : x - k * v1 - v2); }
    };
    static constexpr int kFade = 220;   // ~5 ms fade when a voice is cut (steal / choke)

    double sr = 44100, clock = 0;
    std::atomic<const Snapshot*> snap { nullptr };
    Voice voices[kVoices]; uint32_t ageCounter = 0;
    Hit hits[1024]; int nHits = 0;
    juce::AudioBuffer<float> trackBuf;
    SVF trackFilt[kTracks], djFilt;
    float djCut = 0.5f, glueEnv = 0, crushHold[2] {}; int crushCount = 0;
    juce::dsp::Reverb reverb;
    std::vector<float> echoL, echoR; int ePos = 0; float eLp[2] {};
    std::vector<float> ring, ring2; int rPos = 0;
    std::vector<float> stutBuf, stutBuf2; int stutLen = 0, stutPos = 0, stutRec = 0, stutWas = 0;
    double tapePos = 0; float tapeRate = 1; bool tapeWas = false;
    int lastStepUi = -1;

    static float interp(const float* d, int len, double p) { const int i = (int) p; const float t = (float) (p - i); return d[i] + (d[juce::jmin(i + 1, len - 1)] - d[i]) * t; }
    static float hash01(long a, long b) { uint32_t x = (uint32_t) (a * 73856093L) ^ (uint32_t) (b * 19349663L) ^ 0x9e3779b9u; x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return (x & 0xffffff) / 16777216.0f; }

    void addHit(int track, float vel, int offset) { if (nHits < 1024) hits[nHits++] = { track, vel, juce::jmax(0, offset) }; }

    // which pattern and pattern-step play at global 16th `s`
    static bool resolve(const Snapshot* S, const EngineParams& P, long s, int& pat, int& ps) {
        if (s < 0) return false;
        if (P.songMode && S->songCount > 0) {
            const long bar = s / 16;
            for (int i = 0; i < S->songCount; ++i) {
                const auto& b = S->song[(size_t) i];
                if (bar >= b.startBar && bar < b.startBar + b.bars) { pat = juce::jlimit(0, kPatterns - 1, b.pattern); ps = (int) ((s - (long) b.startBar * 16) % S->patterns[(size_t) pat].length); return true; }
            }
            return false;
        }
        pat = juce::jlimit(0, kPatterns - 1, P.pattern); ps = (int) (s % S->patterns[(size_t) pat].length); return true;
    }

    void schedule(const Snapshot* S, const EngineParams& P, double b0, double b1, double spb) {
        const double step = 0.25;
        for (long s = (long) std::floor(b0 * 4.0) - 2; s <= (long) std::floor(b1 * 4.0); ++s) {
            int pat = 0, ps = 0; if (!resolve(S, P, s, pat, ps)) continue;
            const auto& p = S->patterns[(size_t) pat];
            const double swing = (s & 1) ? P.swing * 0.5 * step : 0.0;
            for (int t = 0; t < kTracks; ++t) {
                const Step& st = p.steps[(size_t) t][(size_t) ps];
                if (!st.vel) continue;
                if (st.prob < 100 && hash01(s, t * 7 + 1) * 100.0f >= st.prob) continue;
                int roll = juce::jlimit(1, 8, (int) st.roll);
                float vel = st.vel / 127.0f;
                if (P.glitch > 0.01f) { const float g = hash01(s, t * 7 + 3); if (g < P.glitch * 0.12f) continue; if (g > 1 - P.glitch * 0.18f) roll = juce::jmax(roll, 2 + (int) (hash01(s, t * 7 + 4) * 3)); }
                double base = s * step + swing + st.nudge / 100.0 * step;
                if (P.human > 0.01f) { base += (hash01(s, t * 7 + 5) - 0.3) * P.human * 0.06; vel *= 1.0f - hash01(s, t * 7 + 6) * P.human * 0.35f; }
                for (int r = 0; r < roll; ++r) {
                    const double tb = base + r * step / roll;
                    if (tb < b0 || tb >= b1) continue;
                    addHit(t, vel * (roll > 1 ? 0.7f + 0.3f * r / (roll - 1) : 1.0f), (int) ((tb - b0) * spb));
                }
            }
        }
    }

    void startVoice(const Snapshot* S, const Hit& h) {
        if (!S) return;
        const auto* snd = S->sound[(size_t) h.track]; if (!snd || snd->data.empty()) return;
        const auto& tr = S->tracks[(size_t) h.track];
        trig[h.track].fetch_add(1);
        if (tr.choke > 0) for (auto& v : voices) if (v.data && v.track != h.track && S->tracks[(size_t) v.track].choke == tr.choke && v.fade < 0) v.fade = kFade;
        for (auto& v : voices) if (v.data && v.track == h.track && v.fade < 0 && v.age + 2 < ageCounter) { /* same track: let tails ring unless retriggered fast */ }
        Voice* pick = nullptr; uint32_t oldest = UINT32_MAX;
        for (auto& v : voices) { if (!v.data) { pick = &v; break; } if (v.age < oldest) { oldest = v.age; pick = &v; } }
        if (pick->data) { // steal with a fade: re-use the slot after a quick duck
            pick->fade = juce::jmin(pick->fade < 0 ? kFade : pick->fade, 32);
        }
        Voice& v = *pick;
        v.data = snd->data.data(); v.len = (int) snd->data.size(); v.pos = 0; v.age = ++ageCounter;
        v.rate = std::exp2(tr.pitch / 12.0) * snd->rate / sr; v.reverse = tr.reverse; v.track = h.track; v.fade = -1;
        v.gain = tr.vol * (0.25f + 0.75f * h.vel); v.env = 1;
        // decay < 1 shortens the sound with an exponential fade over its length
        const double lenSec = v.len / snd->rate / (v.rate * sr / snd->rate);
        v.envK = tr.decay >= 0.99f ? 1.0f : (float) std::exp(-6.9 / juce::jmax(64.0, tr.decay * lenSec * sr));
        const float pan = juce::jlimit(-1.0f, 1.0f, tr.pan);
        v.panL = std::cos((pan + 1) * juce::MathConstants<float>::pi * 0.25f); v.panR = std::sin((pan + 1) * juce::MathConstants<float>::pi * 0.25f);
    }

    void master(juce::AudioBuffer<float>& out, const EngineParams& P, double spb) {
        const int n = out.getNumSamples();
        float* L = out.getWritePointer(0); float* R = out.getWritePointer(1);
        // GLUE: bus compressor (gentle, punchy)
        if (P.glue > 0.01f) {
            const float att = std::exp(-1.0f / (0.008f * (float) sr)), rel = std::exp(-1.0f / (0.12f * (float) sr));
            const float thr = juce::Decibels::decibelsToGain(-6.0f - P.glue * 18.0f), makeup = 1.0f + P.glue * 1.2f;
            for (int i = 0; i < n; ++i) {
                const float lvl = juce::jmax(std::abs(L[i]), std::abs(R[i]));
                glueEnv = lvl > glueEnv ? att * glueEnv + (1 - att) * lvl : rel * glueEnv + (1 - rel) * lvl;
                const float g = glueEnv > thr ? std::pow(thr / glueEnv, 0.75f) : 1.0f;
                L[i] *= g * makeup; R[i] *= g * makeup;
            }
        }
        // DJ FILTER: low-pass <- 0.5 -> high-pass, smoothed
        for (int i = 0; i < n; ++i) {
            djCut += (P.filter - djCut) * 0.002f;
            if ((i & 15) == 0 && std::abs(djCut - 0.5f) > 0.01f)
                djFilt.setup(djCut < 0.5f ? 80.0f * std::pow(250.0f, djCut * 2.0f) : 20.0f * std::pow(250.0f, (djCut - 0.5f) * 2.0f), P.res, (float) sr);
            if (std::abs(djCut - 0.5f) > 0.01f) { const int ty = djCut < 0.5f ? 0 : 2; L[i] = djFilt.tick(0, L[i], ty); R[i] = djFilt.tick(1, R[i], ty); }
        }
        // DRIVE + CRUSH
        if (P.drive > 0.01f) { const float g = 1 + P.drive * P.drive * 14, c = 1 / (1 + P.drive * 1.2f); for (int i = 0; i < n; ++i) { L[i] = std::tanh(L[i] * g) * c; R[i] = std::tanh(R[i] * g) * c; } }
        if (P.crush > 0.01f) {
            const float q = std::exp2(12.0f - P.crush * 9.0f); const int hold = 1 + (int) (P.crush * P.crush * 12);
            for (int i = 0; i < n; ++i) { if (crushCount++ % hold == 0) { crushHold[0] = std::round(L[i] * q) / q; crushHold[1] = std::round(R[i] * q) / q; } L[i] = crushHold[0]; R[i] = crushHold[1]; }
        }
        // ECHO: dotted 1/8 ping-pong, always fed so nothing old replays when you turn it up
        {
            const int N = (int) echoL.size(); const int dly = juce::jlimit(1, N - 1, (int) (spb * 0.75)); const float mix = P.echo * 0.5f, fb = 0.3f + P.echo * 0.25f;
            for (int i = 0; i < n; ++i) {
                const int rp = (ePos - dly + N) % N; const float yl = echoL[(size_t) rp], yr = echoR[(size_t) rp];
                eLp[0] += (yr - eLp[0]) * 0.4f; eLp[1] += (yl - eLp[1]) * 0.4f;
                echoL[(size_t) ePos] = L[i] * (P.echo > 0.01f ? 1.0f : 0.0f) + eLp[0] * fb; echoR[(size_t) ePos] = eLp[1] * fb;
                L[i] += yl * mix; R[i] += yr * mix; ePos = (ePos + 1) % N;
            }
        }
        // REVERB
        if (P.verb > 0.01f) {
            juce::dsp::Reverb::Parameters rp; rp.roomSize = 0.5f + P.verb * 0.45f; rp.damping = 0.5f; rp.wetLevel = P.verb * 0.45f; rp.dryLevel = 1.0f; rp.width = 1.0f;
            reverb.setParameters(rp); juce::dsp::AudioBlock<float> b(out); reverb.process(juce::dsp::ProcessContextReplacing<float>(b));
        }
        // STUTTER (hold): grab the next slice, then repeat it
        if (P.stutter > 0) {
            static const double D[] = { 0, 1.0, 0.5, 0.25, 0.125 };
            if (stutWas != P.stutter) { stutLen = juce::jlimit(64, (int) stutBuf.size(), (int) (spb * D[juce::jlimit(1, 4, P.stutter)])); stutRec = 0; stutPos = 0; stutWas = P.stutter; }
            for (int i = 0; i < n; ++i) {
                if (stutRec < stutLen) { stutBuf[(size_t) stutRec] = L[i]; stutBuf2[(size_t) stutRec] = R[i]; ++stutRec; }
                else { const float w = juce::jmin(1.0f, juce::jmin((float) stutPos, (float) (stutLen - stutPos)) / 64.0f); L[i] = stutBuf[(size_t) stutPos] * w; R[i] = stutBuf2[(size_t) stutPos] * w; stutPos = (stutPos + 1) % stutLen; }
            }
        } else stutWas = 0;
        // TAPE STOP (hold): play the recent output back slower and slower
        const int RN = (int) ring.size();
        for (int i = 0; i < n; ++i) { ring[(size_t) rPos] = L[i]; ring2[(size_t) rPos] = R[i]; rPos = (rPos + 1) % RN; }
        if (P.tape) {
            if (!tapeWas) { tapePos = rPos - n; tapeRate = 1; tapeWas = true; }
            for (int i = 0; i < n; ++i) {
                tapeRate = juce::jmax(0.0f, tapeRate - (float) (1.0 / (0.9 * sr)));
                double p = std::fmod(tapePos + RN, (double) RN); const int k = (int) p; const float t = (float) (p - k);
                L[i] = (ring[(size_t) k] + (ring[(size_t) ((k + 1) % RN)] - ring[(size_t) k]) * t) * tapeRate;
                R[i] = (ring2[(size_t) k] + (ring2[(size_t) ((k + 1) % RN)] - ring2[(size_t) k]) * t) * tapeRate;
                tapePos += tapeRate;
            }
        } else tapeWas = false;
        // volume + soft limiter
        for (int i = 0; i < n; ++i) { L[i] = std::tanh(L[i] * P.volume * 1.1f) * 0.95f; R[i] = std::tanh(R[i] * P.volume * 1.1f) * 0.95f; }
    }
};

} // namespace gb
