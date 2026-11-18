#pragma once
// Ghosty Pro TRAP update: CHARACTER (saturation -> distortion, 2x oversampled, with clean-up EQ and gain compensation),
// TAPE / SLOWED, the STACK layer MIDI router and the SAMPLE layer (one-shot across the keys, pitched with Signalsmith Stretch,
// mono or stereo, formants kept). Included by ProProcessor.h after gpro::Env / gpro::Svf.
#include "signalsmith-stretch/signalsmith-stretch.h"
#include <juce_dsp/juce_dsp.h>

namespace gpro
{
constexpr int NUM_LAYERS = 3;            // layer 1 = this sound; layers 2 and 3 = another factory sound or the sample
constexpr int MAX_SMP_VOICES = 16;
constexpr double MAX_SAMPLE_SECS = 30.0;
inline const juce::StringArray& layerSrcNames() { static juce::StringArray s { "Off", "Sound", "Sample" }; return s; }
inline const juce::StringArray& playModeNames() { static juce::StringArray s { "All", "Vel Low", "Vel High", "Key Low", "Key High" }; return s; }
enum LayerSrc { lsOff = 0, lsSound, lsSample };
enum PlayMode { pmAll = 0, pmVelLow, pmVelHigh, pmKeyLow, pmKeyHigh };

// ---------------------------------------------------------------- CHARACTER
// One knob: soft saturation that turns into clipping (run at 2x so the clipper's aliases stay down), a bell cut in the low mids
// (the mud distortion adds), a 5 Hz DC blocker (subs pass untouched) and output gain compensation measured from the curve
// itself, so the level stays put while the knob moves. Exact bypass at zero.
struct Character
{
    double sr = 44100; float amt = 0, comp = 1, dcA = 0, bellK = 1, bellG = 0; float dcS[2] {}; Svf bell; int maxBlock = 512;
    juce::dsp::Oversampling<float> os { 2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, false };
    void prepare (double s, int block) { sr = s; maxBlock = juce::jmax (16, block); os.initProcessing ((size_t) maxBlock); amt = 0; reset(); }
    void reset() { bell.reset(); dcS[0] = dcS[1] = 0; os.reset(); }
    static float shape (float x, float a) noexcept
    {
        const float d = 1.0f + 30.0f * std::pow (a, 1.6f), h = a * a, u = d * x;
        const float soft = std::tanh (u), hard = juce::jlimit (-1.0f, 1.0f, u * 0.8f);
        return soft + (hard - soft) * h;
    }
    void update (float a)
    {
        amt = a;
        double in = 0, out = 0;
        for (int i = 0; i < 64; ++i) { const float x = 0.35f * std::sin (juce::MathConstants<float>::twoPi * (float) i / 64.0f), y = shape (x, a); in += x * x; out += y * y; }
        comp = (float) std::sqrt (in / std::max (1.0e-9, out));
        bellK = 1.0f / 1.2f; bell.set ((float) std::tan (juce::MathConstants<double>::pi * 280.0 / sr), bellK);
        bellG = juce::Decibels::decibelsToGain (-7.0f * a) - 1.0f;
        dcA = 1.0f - (float) std::exp (-juce::MathConstants<double>::twoPi * 5.0 / sr);
    }
    void process (juce::AudioBuffer<float>& b, float a)
    {
        a = juce::jlimit (0.0f, 1.0f, a);
        if (a <= 1.0e-4f) { if (amt > 0.0f) { amt = 0; reset(); } return; }      // exact bypass at zero
        if (a != amt) update (a);
        const int n = b.getNumSamples(), chans = juce::jmin (2, b.getNumChannels());
        if (chans <= 0) return;
        for (int pos = 0; pos < n; pos += maxBlock)                                // never more than the oversampler was prepared for
        {
            const int len = juce::jmin (maxBlock, n - pos);
            juce::dsp::AudioBlock<float> blk (b.getArrayOfWritePointers(), (size_t) chans, (size_t) pos, (size_t) len);
            auto up = os.processSamplesUp (blk);
            for (size_t c = 0; c < up.getNumChannels(); ++c)
            {
                auto* d = up.getChannelPointer (c);
                for (size_t i = 0; i < up.getNumSamples(); ++i) d[i] = shape (d[i], a) * comp;
            }
            os.processSamplesDown (blk);
            for (int c = 0; c < chans; ++c)
            {
                auto* d = b.getWritePointer (c, pos);
                for (int i = 0; i < len; ++i)
                {
                    float y = d[i];
                    dcS[c] += (y - dcS[c]) * dcA; y -= dcS[c];
                    float bp, hp; bell.tick (y, c, bp, hp); y += bellG * bp * bellK;
                    d[i] = y;
                }
            }
        }
    }
};

// ---------------------------------------------------------------- TAPE / SLOWED
// Wow + flutter (a short modulated delay), tape saturation and a low-pass, all scaled by one AMOUNT. The pitch-down of a
// "slowed" edit is a separate SLOW knob (semitones) and is applied at the oscillators (ProProcessor::BlockCtl::extraSemis).
struct Tape
{
    double sr = 44100; std::vector<float> dl[2]; int w = 0, mask = 0; double ph1 = 0, ph2 = 0, ph3 = 0; float lp[2] {}, mix = 0, amtSm = -1.0f;
    static float semisFor (bool on, float slowSemis) noexcept { return on ? juce::jlimit (-12.0f, 0.0f, slowSemis) : 0.0f; }
    void prepare (double s)
    {
        sr = s; const int n = juce::nextPowerOfTwo ((int) (s * 0.03) + 16);
        for (auto& d : dl) d.assign ((size_t) n, 0.0f);
        mask = n - 1; w = 0; mix = 0; lp[0] = lp[1] = 0; amtSm = -1.0f;
    }
    void clear() noexcept { for (auto& d : dl) std::fill (d.begin(), d.end(), 0.0f); lp[0] = lp[1] = 0; }   // BYPASS: no old audio left in the line
    void process (juce::AudioBuffer<float>& b, bool on, float amt)
    {
        const float target = on ? 1.0f : 0.0f;
        if (mix <= 0.0f && target <= 0.0f) return;           // exact bypass when off
        amt = juce::jlimit (0.0f, 1.0f, amt);
        if (amtSm < 0.0f) amtSm = amt;
        const int n = b.getNumSamples();
        const float mixA = 1.0f - (float) std::exp (-1.0 / (0.02 * sr));
        const float base = (float) (0.004 * sr);
        float wowD = amt * (float) (0.0011 * sr), flD = amt * (float) (0.00012 * sr);
        const double i1 = juce::MathConstants<double>::twoPi * 0.55 / sr, i2 = juce::MathConstants<double>::twoPi * 1.7 / sr, i3 = juce::MathConstants<double>::twoPi * 6.1 / sr;
        float sat = 1.0f + amt * 2.2f, satG = (1.0f + amt * 0.4f) / sat;
        const float fc = 18000.0f * std::pow (3200.0f / 18000.0f, amt);
        float lpA = 1.0f - (float) std::exp (-juce::MathConstants<double>::twoPi * juce::jmin ((double) fc, sr * 0.45) / sr);
        // AMOUNT moving (2026-10-24): it glides (30 ms) - wow / flutter depth and saturation per sample (a stepped wow depth made the
        // delay read jump: a click), the low-pass every 16 samples. At rest: exactly the per-block values above.
        const bool gliding = amtSm != amt;
        const float gA = 1.0f - (float) std::exp (-1.0 / (0.03 * sr));
        auto* L = b.getWritePointer (0); auto* R = b.getNumChannels() > 1 ? b.getWritePointer (1) : nullptr;
        for (int i = 0; i < n; ++i)
        {
            if (gliding && amtSm != amt)
            {
                amtSm += (amt - amtSm) * gA; if (std::abs (amt - amtSm) < 1.0e-5f) amtSm = amt;
                const float a = amtSm;
                wowD = a * (float) (0.0011 * sr); flD = a * (float) (0.00012 * sr);
                sat = 1.0f + a * 2.2f; satG = (1.0f + a * 0.4f) / sat;
                if ((i & 15) == 0 || amtSm == amt)
                {
                    const float f = 18000.0f * std::pow (3200.0f / 18000.0f, a);
                    lpA = 1.0f - (float) std::exp (-juce::MathConstants<double>::twoPi * juce::jmin ((double) f, sr * 0.45) / sr);
                }
            }
            const float mod = wowD * (0.6f * (float) std::sin (ph1) + 0.4f * (float) std::sin (ph2)) + flD * (float) std::sin (ph3);
            ph1 += i1; ph2 += i2; ph3 += i3;
            const float dpos = base + wowD + flD + mod;
            const double rp = (double) w - dpos; const int ri = (int) std::floor (rp); const float t = (float) (rp - ri);
            mix += (target - mix) * mixA;
            for (int ch = 0; ch < 2; ++ch)
            {
                float* d = ch == 0 ? L : R; if (d == nullptr) break;
                auto& buf = dl[ch];
                const float x = d[i]; buf[(size_t) (w & mask)] = x;
                float y = buf[(size_t) (ri & mask)] * (1.0f - t) + buf[(size_t) ((ri + 1) & mask)] * t;
                y = std::tanh (y * sat) * satG;
                lp[ch] += (y - lp[ch]) * lpA; y = lp[ch];
                d[i] = x + (y - x) * mix;
            }
            w = (w + 1) & mask;
        }
        if (ph1 > 1.0e6) { ph1 = std::fmod (ph1, juce::MathConstants<double>::twoPi); ph2 = std::fmod (ph2, juce::MathConstants<double>::twoPi); ph3 = std::fmod (ph3, juce::MathConstants<double>::twoPi); }
    }
};

// ---------------------------------------------------------------- STACK: per-layer MIDI (play mode split + start delay)
struct LayerMidi
{
    struct Pend { int when; int size; uint8_t d[3]; };
    std::vector<Pend> pend; juce::MidiBuffer out;
    void prepare() { pend.clear(); pend.reserve (2048); out.ensureSize (32768 + 64); }   // the whole performed buffer fits without growing
    bool pending() const noexcept { return ! pend.empty(); }
    void run (const juce::MidiBuffer& in, int n, int dlySamples, int play, int vsplit, int ksplit)
    {
        out.clear();
        for (const auto meta : in)
        {
            if (meta.numBytes > 3) continue;
            const auto m = meta.getMessage();
            if (m.isNoteOn())
            {
                const int v = m.getVelocity(), k = m.getNoteNumber();
                if ((play == pmVelLow && v >= vsplit) || (play == pmVelHigh && v < vsplit) || (play == pmKeyLow && k >= ksplit) || (play == pmKeyHigh && k < ksplit)) continue;
            }
            if (dlySamples <= 0 || (! m.isNoteOnOrOff() && ! m.isPitchWheel() && ! m.isController() && ! m.isAftertouch() && ! m.isChannelPressure()))
            { out.addEvent (meta.data, meta.numBytes, meta.samplePosition); continue; }
            if (pend.size() < pend.capacity()) { Pend p { meta.samplePosition + dlySamples, meta.numBytes, {} }; std::memcpy (p.d, meta.data, (size_t) meta.numBytes); pend.push_back (p); }
            else if (! m.isNoteOn()) out.addEvent (meta.data, meta.numBytes, meta.samplePosition);   // queue full (never in practice): note-offs still get through, so nothing sticks
        }
        size_t w = 0;
        for (auto& p : pend) { if (p.when < n) out.addEvent (p.d, p.size, p.when); else { p.when -= n; pend[w++] = p; } }
        pend.resize (w);
    }
};

// ---------------------------------------------------------------- SAMPLE layer
struct SampleData
{
    std::vector<float> fwd[2], rev[2];     // peak-normalised, 1 or 2 channels; rev = the same backwards
    int chans = 1; double rate = 48000; int root = 60; float rootCents = 0; juce::String name, path;
    juce::MemoryBlock encoded;             // FLAC of the sample for the plug-in state (built once on the message thread)
    int length() const noexcept { return (int) fwd[0].size(); }
    const std::vector<float>& ch (int c, bool reverse) const noexcept { const int k = c < chans ? c : 0; return reverse ? rev[k] : fwd[k]; }
};

// Single-builder (message thread) -> audio thread hand-over of an immutable object, no locks / frees on the audio side.
template <class T>
class Handoff
{
public:
    Handoff() = default;
    ~Handoff() { delete pending.exchange (nullptr); delete trash.exchange (nullptr); delete active; }
    void post (std::unique_ptr<T> s) { delete pending.exchange (s.release()); collect(); }
    void collect() { delete trash.exchange (nullptr); }
    const T* acquire() noexcept
    {
        if (pending.load (std::memory_order_acquire) != nullptr && trash.load (std::memory_order_acquire) == nullptr)
            if (auto* p = pending.exchange (nullptr, std::memory_order_acq_rel)) { trash.store (active, std::memory_order_release); active = p; }
        return active;
    }
private:
    std::atomic<T*> pending { nullptr }, trash { nullptr };
    T* active = nullptr;
    JUCE_DECLARE_NON_COPYABLE (Handoff)
};
struct SampleSlot { std::shared_ptr<const SampleData> data; };

// YIN pitch of a mono buffer (the first clear frames after the onset); 0 if nothing periodic was found.
// Frames of (near) silence never vote (a trimmed one-shot padded with zeros used to read as 2.3 kHz), the analysis runs at
// <= 48 kHz whatever the file rate (so an 808 exported at 96 k still reaches down to 35 Hz).
inline double yinPitch (const std::vector<float>& src, double srcRate)
{
    std::vector<float> dec; const std::vector<float>* xp = &src; double rate = srcRate;
    if (srcRate > 50000.0)
    {
        const int k = (int) std::ceil (srcRate / 48000.0);
        dec.resize (src.size() / (size_t) k);
        for (size_t i = 0; i < dec.size(); ++i) { double a = 0; for (int j = 0; j < k; ++j) a += src[i * (size_t) k + (size_t) j]; dec[i] = (float) (a / k); }
        xp = &dec; rate = srcRate / k;
    }
    const auto& x = *xp;
    const int W = 2048, maxTau = juce::jmin (W - 1, (int) (rate / 35.0)), minTau = juce::jmax (2, (int) (rate / 2200.0));
    const int n = (int) x.size();
    if (n < W * 2 || maxTau <= minTau) return 0;
    float peak = 0; for (auto v : x) peak = std::max (peak, std::abs (v));
    if (peak < 1.0e-6f) return 0;
    int onset = 0; while (onset < n && std::abs (x[(size_t) onset]) < peak * 0.1f) ++onset;
    std::vector<double> found, d ((size_t) maxTau + 1), cm ((size_t) maxTau + 1, 1.0);
    const double floorE = (double) (0.01f * peak) * (0.01f * peak) * W;       // a frame below 1 % of the peak (RMS) is silence
    int silentRun = 0;
    for (int k = 0; k < 10; ++k)
    {
        const int s = onset + (int) (k * rate * 0.06);
        if (s + W + maxTau >= n) break;
        double e = 0; for (int i = 0; i < W; ++i) e += (double) x[(size_t) (s + i)] * x[(size_t) (s + i)];
        if (e < floorE) { if (++silentRun >= 3) break; continue; }
        silentRun = 0;
        for (int tau = 1; tau <= maxTau; ++tau)
        {
            double acc = 0;
            for (int i = 0; i < W; ++i) { const double df = x[(size_t) (s + i)] - x[(size_t) (s + i + tau)]; acc += df * df; }
            d[(size_t) tau] = acc;
        }
        double run = 0; int best = -1;
        for (int tau = 1; tau <= maxTau; ++tau) { run += d[(size_t) tau]; cm[(size_t) tau] = run > 1.0e-9 * W ? d[(size_t) tau] * tau / run : 1.0; }
        for (int tau = minTau; tau < maxTau; ++tau)
            if (cm[(size_t) tau] < 0.15) { while (tau + 1 < maxTau && cm[(size_t) tau + 1] < cm[(size_t) tau]) ++tau; best = tau; break; }
        if (best < 0) continue;
        double t = best;
        if (best > 1 && best < maxTau) { const double a = cm[(size_t) best - 1], b = cm[(size_t) best], c = cm[(size_t) best + 1], den = a - 2 * b + c; if (std::abs (den) > 1.0e-12) t += 0.5 * (a - c) / den; }
        found.push_back (rate / t);
    }
    if (found.empty()) return 0;
    std::sort (found.begin(), found.end());
    return found[found.size() / 2];
}

struct SampleParams
{
    int root = 60; float fine = 0; bool formant = true, loop = false; float start = 0, end = 1, revVel = 0;   // revVel > 0: notes at / above it play backwards
    float a = 0.002f, d = 1.0f, s = 1.0f, r = 0.3f;
    float semis[NUM_LAYERS] {}, lvl[NUM_LAYERS] {}, pan[NUM_LAYERS] {}; bool rev[NUM_LAYERS] {};
};

// 16 voices, each with a mono and a stereo stretcher (a mono one-shot costs half). A new note is primed (the stretcher's
// pre-roll) inside render(), at most a few per block, so a big chord never blows a small host buffer; a voice that has to
// wait starts one block later. Everything is sized in prepare(); render() works in chunks of that size.
class SampleEngine
{
public:
    void prepare (double s, int block)
    {
        sr = s; blockMax = juce::jmax (64, block);
        const int fft = juce::jmax (256, (int) (sr * 0.04)), hop = juce::jmax (64, fft / 3);   // 40 ms window, 3 frames in flight: a note primes in ~0.25 ms
        for (auto& v : voices)
        {
            v.stM.configure (1, fft, hop, true); v.stS.configure (2, fft, hop, true);
            const int preLen = v.stS.inputLatency() + v.stS.outputLatency() + 8;
            for (int c = 0; c < 2; ++c) { v.in[c].assign ((size_t) (blockMax + 8), 0.0f); v.pre[c].assign ((size_t) preLen, 0.0f); }
            float* ip[2] = { v.pre[0].data(), v.pre[1].data() };
            v.stM.outputSeek (ip, preLen - 8); v.stS.outputSeek (ip, preLen - 8);     // pre-warm: scratch buffers sized here, never on the audio thread
            v.stM.reset(); v.stS.reset();
            v.env.sr = s; v.active = false; v.primed = false; v.declickL = v.declickR = v.lastL = v.lastR = 0;
        }
        for (auto& sc : scratch) sc.assign ((size_t) blockMax, 0.0f);
    }
    // fade = let go over 5 ms (the sample data stays valid); otherwise a hard cut with a 10 ms declick (the data is going away)
    void allOff (int layer = -1, bool fade = true)
    {
        for (auto& v : voices)
            if (v.active && (layer < 0 || v.layer == layer))
            {
                if (fade && v.primed) { v.held = false; v.env.r = 0.005f; v.env.curve = 0; v.env.off(); if (! v.env.active()) v.active = false; }
                else { v.declickL += v.lastL; v.declickR += v.lastR; v.active = false; }
            }
    }
    void clear() { for (auto& v : voices) { v.active = false; v.held = false; v.declickL = v.declickR = v.lastL = v.lastR = 0; } }   // gone, no tails
    int active (int layer = -1) const { int c = 0; for (auto& v : voices) if (v.active && (layer < 0 || v.layer == layer)) ++c; return c; }
    bool busy() const { for (auto& v : voices) if (v.active || std::abs (v.declickL) + std::abs (v.declickR) > 1.0e-6f) return true; return false; }

    void noteOn (int layer, int note, float vel, const SampleParams& p, const SampleData& d, juce::uint32 stamp)
    {
        if (d.length() < 8) return;
        Voice* v = nullptr;
        for (auto& c : voices) if (! c.active) { v = &c; break; }
        if (v == nullptr) for (auto& c : voices) if (! c.held && (v == nullptr || c.env.out < v->env.out)) v = &c;   // steal: the quietest released voice first
        if (v == nullptr) for (auto& c : voices) if (v == nullptr || c.stamp < v->stamp) v = &c;                      // ... then the oldest
        if (v->active) { v->declickL += v->lastL; v->declickR += v->lastR; }                                          // a 10 ms fade carries the stolen voice out
        v->active = true; v->held = true; v->primed = false; v->note = note; v->layer = layer; v->vel = vel; v->stamp = stamp; v->drained = 0; v->lastSemis = 1.0e9f; v->lastL = v->lastR = 0;
        v->env.reset(); v->env.set (p.a, p.d, p.s, p.r, 0.5f);          // on() once primed
        v->rev = p.rev[layer] || (p.revVel > 0.0f && vel >= p.revVel);
        v->stereo = d.chans > 1;
        v->s0 = juce::jlimit (0, d.length() - 1, (int) ((v->rev ? 1.0f - p.end : p.start) * (float) d.length()));
        v->e0 = juce::jlimit (v->s0 + 1, d.length(), (int) ((v->rev ? 1.0f - p.start : p.end) * (float) d.length()));
        v->pos = v->s0; v->step = d.rate / sr; v->loop = p.loop;
    }
    void noteOff (int layer, int note)
    {
        for (auto& v : voices) if (v.active && v.held && v.layer == layer && v.note == note) { v.held = false; if (v.primed) v.env.off(); else v.active = false; }
    }
    // d == nullptr: the sample is gone, only the declick tails play out
    void render (juce::AudioBuffer<float>& out, int n, const SampleParams& p, const SampleData* d)
    {
        auto* L = out.getWritePointer (0); auto* R = out.getNumChannels() > 1 ? out.getWritePointer (1) : nullptr;
        const float dc = (float) std::exp (-1.0 / (0.010 * sr));
        for (auto& v : voices)
            if (std::abs (v.declickL) + std::abs (v.declickR) > 1.0e-6f)
            {
                for (int i = 0; i < n; ++i) { L[i] += v.declickL; if (R) R[i] += v.declickR; v.declickL *= dc; v.declickR *= dc; }
                if (std::abs (v.declickL) + std::abs (v.declickR) < 1.0e-6f) v.declickL = v.declickR = 0;
            }
        if (d == nullptr) { for (auto& v : voices) if (v.active) { v.declickL += v.lastL; v.declickR += v.lastR; v.active = false; } return; }
        const int primeBudget = juce::jmax (2, blockMax / 128);
        int primes = 0;
        for (int pos = 0; pos < n; pos += blockMax)
        {
            const int len = juce::jmin (blockMax, n - pos);
            for (auto& v : voices)
            {
                if (! v.active) continue;
                if (! v.primed) { if (primes >= primeBudget) continue; prime (v, p, *d); ++primes; }
                setPitch (v, p, *d);
                auto& st = v.stereo ? v.stS : v.stM; const int ch = v.stereo ? 2 : 1;
                fetch (v, *d, v.in, len);
                float* ip[2] = { v.in[0].data(), v.in[1].data() }; float* op[2] = { scratch[0].data(), scratch[1].data() };
                st.process (ip, len, op, len);
                const float velGain = 1.0f - 0.6f * (1.0f - v.vel);
                const float lvl = p.lvl[v.layer] * 0.4f * velGain, gl = lvl * juce::jmin (1.0f, 1.0f - p.pan[v.layer]), gr = lvl * juce::jmin (1.0f, 1.0f + p.pan[v.layer]);
                const float* sl = scratch[0].data(); const float* sr2 = ch > 1 ? scratch[1].data() : sl;
                for (int i = 0; i < len; ++i)
                {
                    const float e = v.env.next();
                    float yl = sl[i] * e, yr = sr2[i] * e;
                    if (! std::isfinite (yl) || ! std::isfinite (yr)) yl = yr = 0;
                    v.lastL = yl * gl; v.lastR = yr * gr;
                    L[pos + i] += v.lastL; if (R) R[pos + i] += v.lastR;
                }
                const int lat = st.inputLatency() + st.outputLatency();
                if (! v.env.active() || (! v.loop && v.drained > lat + 256)) v.active = false;
            }
        }
    }

private:
    struct Voice
    {
        signalsmith::stretch::SignalsmithStretch<float> stM { 0x7a91 }, stS { 0x7a92 };
        bool active = false, held = false, rev = false, loop = false, primed = false, stereo = false; int note = 60, layer = 1, s0 = 0, e0 = 0, drained = 0;
        float vel = 1, lastSemis = 1.0e9f, lastL = 0, lastR = 0, declickL = 0, declickR = 0; double pos = 0, step = 1; juce::uint32 stamp = 0; Env env;
        std::vector<float> in[2], pre[2];
    };
    std::array<Voice, MAX_SMP_VOICES> voices; std::vector<float> scratch[2]; double sr = 48000; int blockMax = 512;

    void prime (Voice& v, const SampleParams& p, const SampleData& d)
    {
        auto& st = v.stereo ? v.stS : v.stM;
        v.lastSemis = 1.0e9f; setPitch (v, p, d);
        st.setFormantFactor (1.0f, p.formant);
        st.setFormantBase (p.formant ? (float) (juce::MidiMessage::getMidiNoteInHertz (p.root) / sr) : 0.0f);
        const int preLen = (int) v.pre[0].size() - 8;
        fetch (v, d, v.pre, preLen);
        float* ip[2] = { v.pre[0].data(), v.pre[1].data() };
        st.outputSeek (ip, preLen);
        v.primed = true; v.env.on();
    }
    void setPitch (Voice& v, const SampleParams& p, const SampleData& d)
    {
        const float semis = (float) (v.note - p.root) - d.rootCents * 0.01f + p.fine * 0.01f + p.semis[v.layer];
        if (std::abs (semis - v.lastSemis) > 1.0e-4f) { v.lastSemis = semis; (v.stereo ? v.stS : v.stM).setTransposeSemitones (semis, (float) (8000.0 / sr)); }
    }
    // reads 'count' input samples at the host rate from the region (loop or run dry), 4-point interpolation, 1 or 2 channels
    void fetch (Voice& v, const SampleData& d, std::vector<float>* dst, int count)
    {
        const int ch = v.stereo ? 2 : 1, len = d.length();
        const std::vector<float>* src[2] = { &d.ch (0, v.rev), &d.ch (1, v.rev) };
        for (int i = 0; i < count; ++i)
        {
            if (v.pos >= v.e0) { if (v.loop && v.e0 > v.s0) v.pos = v.s0 + std::fmod (v.pos - v.s0, (double) (v.e0 - v.s0)); else { for (int c = 0; c < ch; ++c) dst[c][(size_t) i] = 0; ++v.drained; continue; } }
            const int k = (int) v.pos; const float t = (float) (v.pos - k);
            for (int c = 0; c < ch; ++c)
            {
                const auto& buf = *src[c];
                auto at = [&] (int j) { j = juce::jlimit (0, len - 1, j); return buf[(size_t) j]; };
                const float y0 = at (k - 1), y1 = at (k), y2 = at (k + 1), y3 = at (k + 2);
                const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
                dst[c][(size_t) i] = ((c3 * t + c2) * t + c1) * t + y1;
            }
            v.pos += v.step;
        }
    }
};
} // namespace gpro
