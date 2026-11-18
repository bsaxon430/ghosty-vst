#pragma once
// YOUR SAMPLE: a user sample as ArpEZator's preview instrument, played by the lanes the user picks.
//  - pitched across the keyboard from a ROOT (auto-detected with YIN + an energy floor, adapted from Ghosty Pro's TRAP sample layer) + FINE
//  - high quality pitching: a 16-tap windowed-sinc interpolator on an anti-aliased mip chain (each level = the one above low-passed and
//    decimated by 2), so pitching up never aliases; optional KEEP FORMANTS through Signalsmith Stretch (as Ghosty Pro's sample layer)
//  - START / END trim, LOOP with crossfaded seamless loop points (auto-found), REVERSE, ADSR, a low-pass + TONE tilt, VOLUME, velocity -> level
//  - 16 stereo voices, soft stealing with a 10 ms fade; nothing allocates or locks on the audio thread (prepare() sizes everything,
//    the sample itself arrives through a Handoff and the old one is freed on the message thread)
#include <juce_audio_basics/juce_audio_basics.h>
#include "signalsmith-stretch/signalsmith-stretch.h"
#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <vector>

namespace arpsmp
{
constexpr int kVoices = 16, kLevels = 6, kTaps = 16, kPhases = 512;

// ---------------------------------------------------------------- the sample (immutable once built; message thread builds it)
struct SampleData
{
    std::vector<float> lv[kLevels][2];   // lv[0] = the sample (peak-normalised, quantised to 24 bit), lv[k] = low-passed + decimated by 2^k
    int chans = 1; double rate = 48000; int root = 60; float rootCents = 0; double detectedHz = 0;
    float loopS = 0.5f, loopE = 1.0f;    // the auto-found loop (fractions of the length)
    juce::String name, path; juce::MemoryBlock encoded;   // FLAC (24-bit) for the project, made once at import
    int length() const noexcept { return (int) lv[0][0].size(); }
    const std::vector<float>& at (int level, int c) const noexcept { return lv[level][c < chans ? c : 0]; }
};

// single builder (message thread) -> audio thread hand-over of an immutable object: no locks, no frees on the audio side
template <class T>
class Handoff
{
public:
    Handoff() = default;
    ~Handoff() { delete pending.exchange (nullptr); delete trash.exchange (nullptr); delete active; }
    void post (std::unique_ptr<T> s) { delete pending.exchange (s.release()); collect(); }
    void postNull() { post (std::unique_ptr<T> (new T())); clearNext = true; }
    void collect() { delete trash.exchange (nullptr); }
    // audio thread: the newest object (a T with length 0 = "no sample")
    const T* acquire() noexcept
    {
        if (pending.load (std::memory_order_acquire) != nullptr && trash.load (std::memory_order_acquire) == nullptr)
            if (auto* p = pending.exchange (nullptr, std::memory_order_acq_rel)) { trash.store (active, std::memory_order_release); active = p; ++generation; }
        return active;
    }
    int gen() const noexcept { return generation; }
    const T* peek() const noexcept { return active; }   // message thread, only for display when nothing is pending
private:
    std::atomic<T*> pending { nullptr }, trash { nullptr };
    T* active = nullptr; int generation = 0; bool clearNext = false;
    JUCE_DECLARE_NON_COPYABLE (Handoff)
};

// ---------------------------------------------------------------- the sound settings (a seqlock: the UI writes, the audio thread copies)
struct Params
{
    int root = 60; float fine = 0;                        // cents
    int mode = 0;                                         // PLAY MODE: 0 ONE-SHOT (START..END whatever the note length), 1 LOOP (sustains in the loop),
                                                          //            2 NOTE LENGTH (the note's length decides how much plays), 3 SLICES (one slice per note)
    bool reverse = false, formant = false;
    float start = 0, end = 1, loopStart = 0.5f, loopEnd = 1.0f, xfade = 0.03f;   // fractions of the length; xfade in seconds
    float a = 0.003f, d = 0.5f, s = 1.0f, r = 0.25f;      // ADSR (seconds, sustain level)
    float cutoff = 1.0f, tone = 0.0f, volDb = 0.0f;       // low-pass 0..1 (20 Hz .. 20 kHz), tone -1..1, volume dB
    // NOTE LENGTH: TRIM (play at the key's pitch, cut at the note's end) or STRETCH (START..END time-stretched to fill the note exactly);
    // longer notes than the region: hold in the loop (fitLoop) or stop; fitFade = the fade at the note's end (seconds; also used by SLICES)
    bool fitStretch = false, fitLoop = false; float fitFade = 0.02f;
    // SLICES: slice starts (fractions, ascending; a slice ends at the next one, the last at END); sliceOrder 0 = IN ORDER (the k-th note
    // of the lane plays slice k), 1 = BY PITCH (semitones from ROOT pick the slice); slicePitched = slices follow the key (else their own pitch)
    int sliceOrder = 0; bool slicePitched = false; int nSlices = 0; std::array<float, 64> slices {};
};
class ParamBox
{
public:
    void write (const Params& p) { seq.fetch_add (1, std::memory_order_acq_rel); box = p; seq.fetch_add (1, std::memory_order_acq_rel); }
    Params read() const noexcept
    {
        for (int tries = 0; tries < 64; ++tries)
        {
            const unsigned a = seq.load (std::memory_order_acquire); if (a & 1u) continue;
            Params p = box; std::atomic_thread_fence (std::memory_order_acquire);
            if (seq.load (std::memory_order_acquire) == a) return p;
        }
        return last;   // (the UI is mid-write for 64 tries: last block's settings)
    }
    void remember (const Params& p) noexcept { last = p; }
private:
    std::atomic<unsigned> seq { 0 }; Params box; Params last;
};

// ---------------------------------------------------------------- the windowed-sinc table (shared, built once)
inline const std::array<float, (kPhases + 1) * kTaps>& sincTable()
{
    static const auto t = [] {
        std::array<float, (kPhases + 1) * kTaps> a {};
        const double pi = 3.14159265358979323846, cut = 0.92;   // a hair under Nyquist (the mip chain does the anti-aliasing)
        for (int ph = 0; ph <= kPhases; ++ph)
        {
            const double frac = (double) ph / kPhases; double sum = 0;
            for (int j = 0; j < kTaps; ++j)
            {
                const double x = (double) (j - (kTaps / 2 - 1)) - frac;   // taps at -7 .. 8 around the read point
                const double s = std::abs (x) < 1.0e-9 ? cut : std::sin (pi * cut * x) / (pi * x);
                const double w = (x + kTaps / 2.0) / kTaps;               // Blackman-Harris over the 16 taps
                const double bh = w <= 0 || w >= 1 ? 0.0 : 0.35875 - 0.48829 * std::cos (2 * pi * w) + 0.14128 * std::cos (4 * pi * w) - 0.01168 * std::cos (6 * pi * w);
                a[(size_t) (ph * kTaps + j)] = (float) (s * bh); sum += s * bh;
            }
            for (int j = 0; j < kTaps; ++j) a[(size_t) (ph * kTaps + j)] = (float) (a[(size_t) (ph * kTaps + j)] / sum);   // unity gain at DC
        }
        return a; }();
    return t;
}

// ---------------------------------------------------------------- building a sample (message thread)
// x[c]: the decoded audio (any rate, 1 or 2 channels). Peak-normalises, quantises to 24 bit (so the FLAC in the project is bit-exact),
// builds the mip chain, detects the root and finds a loop.
double yinPitch (const std::vector<float>& src, double srcRate);
void autoLoop (const std::vector<float>& mono, int s0, int e0, double rate, int& ls, int& le);
inline std::unique_ptr<SampleData> build (std::vector<float> x[2], int chans, double rate, bool normalise = true)
{
    auto d = std::make_unique<SampleData>();
    d->chans = juce::jlimit (1, 2, chans); d->rate = rate > 0 ? rate : 48000.0;
    float peak = 0; for (int c = 0; c < d->chans; ++c) for (float v : x[c]) peak = std::max (peak, std::abs (v));
    const float g = peak > 1.0e-6f ? 0.98f / peak : 1.0f;
    for (int c = 0; c < d->chans; ++c)
    {
        auto& L0 = d->lv[0][c]; L0 = std::move (x[c]);
        if (normalise) for (auto& v : L0) v = (float) (std::round ((double) v * g * 8388607.0) / 8388607.0);   // peak-normalised on the 24-bit grid
    }
    // the mip chain: a low-pass under a quarter of the rate, then every second sample
    constexpr int HT = 127, HC = HT / 2;   // a 127-tap Blackman low-pass at 0.22 fs: down > 70 dB before the new Nyquist
    std::vector<float> h (HT); { double sum = 0; for (int i = 0; i < HT; ++i) { const double x2 = i - HC, s = std::abs (x2) < 1e-9 ? 0.44 : std::sin (3.14159265358979 * 0.44 * x2) / (3.14159265358979 * x2), w = 0.42 - 0.5 * std::cos (2 * 3.14159265358979 * i / (HT - 1)) + 0.08 * std::cos (4 * 3.14159265358979 * i / (HT - 1)); h[(size_t) i] = (float) (s * w); sum += s * w; } for (auto& v : h) v = (float) (v / sum); }
    for (int k = 1; k < kLevels; ++k)
        for (int c = 0; c < d->chans; ++c)
        {
            const auto& up = d->lv[k - 1][c]; auto& dn = d->lv[k][c]; const int n = (int) up.size();
            dn.assign ((size_t) ((n + 1) / 2), 0.0f);
            for (int i = 0; i < (int) dn.size(); ++i) { double acc = 0; const int ctr = 2 * i; for (int j = 0; j < HT; ++j) { const int q = ctr + j - HC; if (q >= 0 && q < n) acc += (double) h[(size_t) j] * up[(size_t) q]; } dn[(size_t) i] = (float) acc; }
        }
    std::vector<float> mono (d->lv[0][0].size(), 0.0f);
    for (int c = 0; c < d->chans; ++c) for (size_t i = 0; i < mono.size(); ++i) mono[i] += d->lv[0][c][i] / (float) d->chans;
    const double hz = yinPitch (mono, d->rate);
    d->detectedHz = hz;
    if (hz > 20)
    {
        const double m = 69 + 12 * std::log2 (hz / 440.0);
        d->root = juce::jlimit (0, 127, (int) std::lround (m)); d->rootCents = (float) ((m - d->root) * 100.0);
    }
    int ls = 0, le = d->length(); autoLoop (mono, 0, d->length(), d->rate, ls, le);
    d->loopS = d->length() > 0 ? (float) ls / (float) d->length() : 0.5f; d->loopE = d->length() > 0 ? (float) le / (float) d->length() : 1.0f;
    return d;
}

// YIN pitch of a mono buffer (the first clear frames after the onset); 0 if nothing periodic was found. Adapted from Ghosty Pro's
// TRAP sample layer: silent frames never vote (a trimmed one-shot padded with zeros), analysis at <= 48 kHz whatever the file rate.
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
    const double floorE = (double) (0.01f * peak) * (0.01f * peak) * W;
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

// a loop that joins without a bump: the end in the last quarter of the region, the start somewhere after a third, both at rising zero
// crossings, the pair whose surroundings correlate best
inline void autoLoop (const std::vector<float>& x, int s0, int e0, double rate, int& ls, int& le)
{
    const int n = (int) x.size(); s0 = juce::jlimit (0, n, s0); e0 = juce::jlimit (s0, n, e0);
    ls = s0 + (e0 - s0) / 2; le = e0;
    const int W = 384, len = e0 - s0;
    if (len < (int) (rate * 0.15) || len < 4 * W) return;
    auto zc = [&] (int i) { return i > 0 && i < n && x[(size_t) i - 1] <= 0.0f && x[(size_t) i] > 0.0f; };
    std::vector<int> ends, starts;
    for (int i = e0 - W - 1; i > s0 + len * 3 / 4 && (int) ends.size() < 24; --i) if (zc (i)) ends.push_back (i);
    const int sLo = s0 + len / 3, sHi = std::max (sLo + 1, e0 - (int) (rate * 0.08) - W);
    const int stride = std::max (1, (sHi - sLo) / 400);
    for (int i = sLo; i < sHi && (int) starts.size() < 400; i += stride) { int j = i; while (j < sHi && ! zc (j)) ++j; if (j < sHi) starts.push_back (j); i = j; }
    if (ends.empty() || starts.empty()) return;
    double best = -2;
    for (int e : ends)
        for (int s : starts)
        {
            if (e - s < (int) (rate * 0.05)) continue;
            double num = 0, ea = 0, eb = 0;
            for (int k = -W / 2; k < W / 2; ++k) { const int a = e + k, b = s + k; if (a < 0 || b < 0 || a >= n || b >= n) continue; num += (double) x[(size_t) a] * x[(size_t) b]; ea += (double) x[(size_t) a] * x[(size_t) a]; eb += (double) x[(size_t) b] * x[(size_t) b]; }
            const double c = num / std::sqrt (ea * eb + 1e-18);
            if (c > best) { best = c; ls = s; le = e; }
        }
}

// ---------------------------------------------------------------- the voices
class Sampler
{
public:
    void prepare (double s, int block)
    {
        sr = s; blockMax = juce::jmax (64, block); (void) sincTable();
        const int fft = juce::jmax (256, (int) (sr * 0.04)), hop = juce::jmax (64, fft / 3);
        for (auto& v : voices)
        {
            v.st.configure (2, fft, hop, true);
            const int preLen = v.st.inputLatency() + v.st.outputLatency() + 8;
            for (int c = 0; c < 2; ++c) { v.in[c].assign ((size_t) (blockMax * 4 + 8), 0.0f);   // (x4: NOTE LENGTH STRETCH reads up to 4x faster)
                                           v.pre[c].assign ((size_t) preLen, 0.0f); v.out[c].assign ((size_t) (blockMax + 8), 0.0f); }
            float* ip[2] = { v.pre[0].data(), v.pre[1].data() };
            v.st.outputSeek (ip, preLen - 8); v.st.reset();   // pre-warm: scratch sized here, never on the audio thread
            v.active = false; v.declickL = v.declickR = v.lastL = v.lastR = 0;
        }
    }
    // the sample changed (or is gone): every voice ends with a 10 ms fade of its last value (the data may be freed)
    void dataChanged() { for (auto& v : voices) if (v.active) { v.declickL += v.lastL; v.declickR += v.lastR; v.active = false; } }
    void allOff (bool hard)
    {
        for (auto& v : voices) if (v.active) { if (hard) { v.declickL += v.lastL; v.declickR += v.lastR; v.active = false; } else v.release(); }
    }
    int activeCount() const { int c = 0; for (auto& v : voices) c += v.active; return c; }

    // lane: 0 chords 1 arp 2 melody 3 bass 9 audition; chain groups legato / glide; lenSamples = when the note lets go; bright = the ENERGY filter factor
    // fat (0..1): the FATTEN connector into this note: 2-4 detuned copies of the voice, spread wide
    // idx: the note's index in its lane (SLICES IN ORDER; -1 = count the notes as they come)
    void noteOn (int lane, int chain, float pitch, float vel, int lenSamples, const Params& p, const SampleData& d, float bright, float fat = 0.0f, int idx = -1)
    {
        if (d.length() < 16) return;
        if ((p.mode == 0 || p.mode == 3) && lane >= 1 && lane <= 3) chokeLane (lane);   // ONE-SHOT / SLICES on a lead lane: the next note takes over (a 10 ms fade)
        Voice* v = nullptr;
        for (auto& c : voices) if (! c.active) { v = &c; break; }
        // steal inside the lane first (lanes never take each other's voices unless the lane has none): the quietest releasing, then the oldest
        if (v == nullptr) for (auto& c : voices) if (c.lane == lane && c.stage >= 3 && (v == nullptr || c.env < v->env)) v = &c;
        if (v == nullptr) for (auto& c : voices) if (c.lane == lane && (v == nullptr || c.age < v->age)) v = &c;
        if (v == nullptr) for (auto& c : voices) if (c.stage >= 3 && (v == nullptr || c.env < v->env)) v = &c;
        if (v == nullptr) for (auto& c : voices) if (v == nullptr || c.age < v->age) v = &c;
        if (v->active) { v->declickL += v->lastL; v->declickR += v->lastR; }                                         // a 10 ms fade carries the stolen voice out
        const int L = d.length();
        v->active = true; v->lane = lane; v->chain = chain; v->vel = vel; v->age = ++ageC; v->endIn = p.mode == 0 && lane != 9 ? 0 : std::max (1, lenSamples);   // ONE-SHOT: rings to END
        setFat (*v, fat);
        v->pitch = v->gFrom = v->gTo = pitch; v->gLen = v->gPos = 0; v->bright = bright; v->fixedPitch = false; v->tf = 1.0; v->relK = -1.0f;
        v->s0 = juce::jlimit (0, L - 2, (int) (p.start * L)); v->e0 = juce::jlimit (v->s0 + 1, L, (int) (p.end * L));
        v->ls = juce::jlimit (v->s0, v->e0 - 1, (int) (p.loopStart * L)); v->le = juce::jlimit (v->ls + 1, v->e0, (int) (p.loopEnd * L));
        v->loop = p.mode == 1 && v->le - v->ls > 64; v->rev = p.reverse; v->ended = false;
        bool stretch = false;
        if (lane != 9 && (p.mode == 2 || p.mode == 3))
        {   // the note's length decides: a fade at its end (not the ADSR release)
            v->relK = std::exp (-4.6f / std::max (1.0f, juce::jlimit (0.001f, 2.0f, p.fitFade) * (float) sr));
            if (p.mode == 2)
            {
                v->loop = p.fitLoop && v->le - v->ls > 64;
                if (p.fitStretch && lenSamples > 16)
                {   // STRETCH: START..END fills the note exactly (time 0.25x .. 4x), the pitch still from the key
                    stretch = true; v->loop = false;
                    v->tf = juce::jlimit (0.25, 4.0, ((double) (v->e0 - v->s0) / d.rate) / ((double) lenSamples / sr));
                }
            }
            else if (p.nSlices > 0)
            {   // SLICES: which slice, its bounds, its pitch
                const int n = juce::jlimit (1, (int) p.slices.size(), p.nSlices);
                int k = 0;
                if (p.sliceOrder == 1) k = ((int) std::lround (pitch) - p.root) % n;
                else k = (idx >= 0 ? idx : sliceCount[(size_t) juce::jlimit (0, 9, lane)]++) % n;
                if (k < 0) k += n;
                v->s0 = juce::jlimit (0, L - 2, (int) (p.slices[(size_t) k] * L));
                v->e0 = juce::jlimit (v->s0 + 1, L, (int) ((k + 1 < n ? p.slices[(size_t) k + 1] : p.end) * L));
                v->loop = false;
                if (! p.slicePitched) { v->fixedPitch = true; v->pitch = v->gFrom = v->gTo = (float) p.root + d.rootCents * 0.01f * (p.root == d.root ? 1.0f : 0.0f) - p.fine * 0.01f; }
            }
        }
        for (auto& q : v->lpos) q = v->s0;
        v->xf = std::min ((double) p.xfade * d.rate, (v->le - v->ls) * 0.45);
        v->tfAcc = 0; v->pos = v->s0; v->formant = p.formant || stretch; v->primed = ! v->formant; v->drained = 0; v->lastSemis = 1.0e9f;
        v->env = 0; v->stage = 1; v->fl[0] = v->fl[1] = v->fb[0] = v->fb[1] = v->tl[0] = v->tl[1] = 0;
        v->lastL = v->lastR = 0;
    }
    void release (int chain) { for (auto& v : voices) if (v.active && v.chain == chain) v.release(); }
    void legato (int chain, float pitch, float vel, float fat = -1.0f) { for (auto& v : voices) if (v.active && v.chain == chain) { if (! v.fixedPitch && (v.gLen == 0 || v.gPos >= v.gLen)) v.pitch = v.gTo = pitch; v.vel = vel; if (fat >= 0) setFat (v, fat); } }
    void resetSliceCount() { sliceCount.fill (0); }
    void chokeLane (int lane) { for (auto& v : voices) if (v.active && v.lane == lane) { v.declickL += v.lastL; v.declickR += v.lastR; v.active = false; } }
    void glide (int chain, float to, int lenSamples) { for (auto& v : voices) if (v.active && v.chain == chain && ! v.fixedPitch) { v.gFrom = v.pitch; v.gTo = to; v.gPos = 0; v.gLen = std::max (1, lenSamples); } }
    bool hasChain (int chain) const { for (auto& v : voices) if (v.active && v.chain == chain) return true; return false; }

    // renders every voice into its lane's bus (outL / outR indexed by bus: 0 chords 1 arp 2 melody 3 bass 4 audition)
    void render (float* const* outL, float* const* outR, int n, const Params& p, const SampleData* d)
    {
        const float dc = (float) std::exp (-1.0 / (0.010 * sr));
        for (auto& v : voices)
            if (std::abs (v.declickL) + std::abs (v.declickR) > 1.0e-7f)
            {
                float* L = outL[busOf (v.lane)]; float* R = outR[busOf (v.lane)];
                for (int i = 0; i < n; ++i) { L[i] += v.declickL; R[i] += v.declickR; v.declickL *= dc; v.declickR *= dc; }
                if (std::abs (v.declickL) + std::abs (v.declickR) < 1.0e-7f) v.declickL = v.declickR = 0;
            }
        if (d == nullptr || d->length() < 16) { dataChanged(); return; }
        const float vol = juce::Decibels::decibelsToGain (p.volDb, -96.0f);
        const float fc0 = 20.0f * std::pow (1000.0f, juce::jlimit (0.0f, 1.0f, p.cutoff));
        const float tone = juce::jlimit (-0.9f, 1.0f, p.tone), toneA = 1.0f - std::exp (-6.2831853f * 1500.0f / (float) sr);
        const float aStep = 1.0f / std::max (1.0f, p.a * (float) sr), dK = std::exp (-4.6f / std::max (1.0f, p.d * (float) sr)), rK = std::exp (-4.6f / std::max (1.0f, p.r * (float) sr));
        int primes = 0;
        for (int pos0 = 0; pos0 < n; pos0 += blockMax)
        {
            const int len = std::min (blockMax, n - pos0);
            for (auto& v : voices)
            {
                if (! v.active) continue;
                if (v.formant && ! v.primed) { if (primes >= 2) continue; prime (v, p, *d); ++primes; }
                float* L = outL[busOf (v.lane)] + pos0; float* R = outR[busOf (v.lane)] + pos0;
                // the dry samples: pitched directly, or at the sample's own speed into the stretcher (KEEP FORMANTS)
                if (v.formant)
                {   // (STRETCH: more or less input per output block = the time change)
                    const int inLen = v.tf == 1.0 ? len : juce::jlimit (1, (int) v.in[0].size() - 8, (int) std::lround (len * v.tf + v.tfAcc));
                    if (v.tf != 1.0) v.tfAcc += len * v.tf - inLen;
                    setStretchPitch (v, p, *d); fetch (v, p, *d, v.in, inLen, true); float* ip[2] = { v.in[0].data(), v.in[1].data() }; float* op[2] = { v.out[0].data(), v.out[1].data() }; v.st.process (ip, inLen, op, len);
                }
                else fetch (v, p, *d, v.out, len, false);
                const float fc = juce::jlimit (30.0f, (float) sr * 0.45f, fc0 * v.bright);
                const bool filt = fc < (float) sr * 0.44f;
                const float g = std::tan (3.14159265f * fc / (float) sr), k = 1.4f, a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
                const float velG = 0.2f + 0.8f * std::pow (juce::jlimit (0.0f, 1.0f, v.vel), 1.3f);
                const float gain = vol * velG * 0.55f;
                for (int i = 0; i < len; ++i)
                {
                    if (v.endIn > 0 && --v.endIn == 0) v.release();
                    // ADSR
                    if (v.stage == 1) { v.env += aStep; if (v.env >= 1.0f) { v.env = 1.0f; v.stage = 2; } }
                    else if (v.stage == 2) v.env = p.s + (v.env - p.s) * dK;
                    else if (v.stage == 3) { v.env *= v.relK >= 0.0f ? v.relK : rK; if (v.env < 1.0e-4f) { v.active = false; } }
                    float y[2] = { v.out[0][(size_t) i], v.out[1][(size_t) i] };
                    for (int c = 0; c < 2; ++c)
                    {
                        if (filt) { const float v3 = y[c] - v.fb[c], v1 = a1 * v.fl[c] + a2 * v3, v2 = v.fb[c] + a2 * v.fl[c] + a3 * v3; v.fl[c] = 2 * v1 - v.fl[c]; v.fb[c] = 2 * v2 - v.fb[c]; y[c] = v2; }
                        if (tone != 0.0f) { v.tl[c] += (y[c] - v.tl[c]) * toneA; y[c] += tone * (y[c] - v.tl[c]); }
                    }
                    float yl = y[0] * v.env * gain, yr = y[1] * v.env * gain;
                    if (! std::isfinite (yl) || ! std::isfinite (yr)) { yl = yr = 0; v.active = false; }
                    v.lastL = yl; v.lastR = yr; L[i] += yl; R[i] += yr;
                    if (! v.active) break;
                }
                if (v.active && v.formant && v.drained > v.st.inputLatency() + v.st.outputLatency() + 256) v.active = false;
                else if (v.active && ! v.formant && v.ended) { v.declickL += v.lastL; v.declickR += v.lastR; v.active = false; }
            }
        }
    }
    double sampleRate() const { return sr; }

private:
    struct Voice
    {
        signalsmith::stretch::SignalsmithStretch<float> st { 0x5a3e };
        std::vector<float> in[2], pre[2], out[2];
        bool active = false, loop = false, rev = false, formant = false, primed = true, ended = false;
        int lane = 1, chain = -1, endIn = 0, s0 = 0, e0 = 1, ls = 0, le = 1, gLen = 0, gPos = 0, drained = 0, stage = 0;
        double pos = 0, xf = 0; float pitch = 60, gFrom = 60, gTo = 60, vel = 1, env = 0, bright = 1, lastSemis = 1.0e9f;
        bool fixedPitch = false; double tf = 1.0, tfAcc = 0; float relK = -1.0f;   // SLICES at their own pitch; STRETCH time factor; NOTE LENGTH / SLICES end fade
        float fl[2] {}, fb[2] {}, tl[2] {}, lastL = 0, lastR = 0, declickL = 0, declickR = 0; juce::uint64 age = 0;
        int layers = 0; double lpos[3] {}; double lmul[3] { 1, 1, 1 };   // FATTEN copies (position, speed factor)
        void release() { if (stage != 3) stage = 3; }
    };
    std::array<Voice, kVoices> voices; double sr = 48000; int blockMax = 512; juce::uint64 ageC = 0; std::array<int, 10> sliceCount {};
    static int busOf (int lane) { return lane == 9 ? 4 : juce::jlimit (0, 3, lane); }
    static void setFat (Voice& v, float fat)
    {   // fat 0 = one voice; > 0: 2..3 extra copies detuned by up to +-22 cents (the connector's fat %)
        fat = juce::jlimit (0.0f, 1.0f, fat);
        const int want = fat <= 0.001f ? 0 : 1 + (int) std::lround (fat * 2.0f);
        const float cents = 6.0f + 16.0f * fat;
        static const float sgn[3] { 1.0f, -1.0f, 0.5f };
        for (int k = 0; k < 3; ++k) { v.lmul[k] = std::pow (2.0, sgn[k] * cents / 1200.0); if (k >= v.layers) v.lpos[k] = v.pos; }
        v.layers = want;
    }

    float semisOf (const Voice& v, const Params& p, const SampleData& d) const { return v.pitch - (float) p.root - d.rootCents * 0.01f * (p.root == d.root ? 1.0f : 0.0f) + p.fine * 0.01f; }
    // one interpolated value of channel c at a position in level-0 samples (mirrored for REVERSE)
    float read (const SampleData& d, int level, int c, double pos, bool rev) const noexcept
    {
        const int L0 = d.length(); if (rev) pos = (double) (L0 - 1) - pos;
        const auto& buf = d.at (level, c); const int n = (int) buf.size();
        const double p = pos / (double) (1 << level); const int i = (int) std::floor (p); const double fr = p - i;
        const auto& T = sincTable(); const int ph = (int) (fr * kPhases); const float t = (float) (fr * kPhases - ph);
        const float* k0 = T.data() + ph * kTaps; const float* k1 = k0 + kTaps;
        float acc = 0;
        const int b = i - (kTaps / 2 - 1);
        if (b >= 0 && b + kTaps <= n) { const float* x = buf.data() + b; for (int j = 0; j < kTaps; ++j) acc += x[j] * (k0[j] + t * (k1[j] - k0[j])); }
        else for (int j = 0; j < kTaps; ++j) { const int q = b + j; if (q >= 0 && q < n) acc += buf[(size_t) q] * (k0[j] + t * (k1[j] - k0[j])); }
        return acc;
    }
    // 'count' output samples of the region (loop with a crossfade, or run dry); raw = at the sample's own speed (into the stretcher)
    void fetch (Voice& v, const Params& p, const SampleData& d, std::vector<float>* dst, int count, bool raw)
    {
        const int chans = 2;
        for (int i = 0; i < count; ++i)
        {
            if (v.gLen > 0 && v.gPos < v.gLen) { ++v.gPos; v.pitch = v.gFrom + (v.gTo - v.gFrom) * ((float) v.gPos / (float) v.gLen); }
            const double ratio = raw ? 1.0 : std::pow (2.0, semisOf (v, p, d) / 12.0);
            const double step = ratio * d.rate / sr;
            int level = 0; double st = step; while (st > 1.0 && level + 1 < kLevels) { st *= 0.5; ++level; }   // pitching up: a band-limited level
            if (v.pos >= v.e0 && ! v.loop) { for (int c = 0; c < chans; ++c) dst[c][(size_t) i] = 0; ++v.drained; v.ended = true; continue; }
            auto one = [&] (int c, double ps) {
                float y = read (d, level, c, ps, v.rev);
                if (v.loop && v.xf > 1 && ps >= v.le - v.xf)
                {   // the seam: fade into the loop start (equal gain; a good loop point correlates)
                    const float w = (float) ((ps - (v.le - v.xf)) / v.xf);
                    y = y * (1.0f - w) + read (d, level, c, ps - (v.le - v.ls), v.rev) * w;
                }
                return y; };
            for (int c = 0; c < chans; ++c) dst[c][(size_t) i] = one (c, v.pos);
            if (v.layers > 0 && ! raw)
            {   // FATTEN: detuned copies, alternately left / right
                const float norm = 1.0f / std::sqrt (1.0f + 0.7f * (float) v.layers);
                for (int c = 0; c < chans; ++c) dst[c][(size_t) i] *= norm;
                for (int k = 0; k < v.layers; ++k)
                {
                    if (v.lpos[k] >= v.e0 && ! v.loop) continue;
                    const float pl = (k & 1) ? 0.25f : 1.0f, pr = (k & 1) ? 1.0f : 0.25f;
                    dst[0][(size_t) i] += one (0, v.lpos[k]) * norm * 0.85f * pl; dst[1][(size_t) i] += one (1, v.lpos[k]) * norm * 0.85f * pr;
                    v.lpos[k] += step * v.lmul[k];
                    if (v.loop && v.lpos[k] >= v.le) v.lpos[k] -= (double) (v.le - v.ls);
                }
            }
            v.pos += step;
            if (v.loop && v.pos >= v.le) v.pos -= (double) (v.le - v.ls);
        }
    }
    void setStretchPitch (Voice& v, const Params& p, const SampleData& d)
    {
        const float semis = semisOf (v, p, d);
        if (std::abs (semis - v.lastSemis) > 1.0e-4f) { v.lastSemis = semis; v.st.setTransposeSemitones (semis, (float) (8000.0 / sr)); }
    }
    void prime (Voice& v, const Params& p, const SampleData& d)
    {
        v.lastSemis = 1.0e9f; setStretchPitch (v, p, d);
        v.st.setFormantFactor (1.0f, p.formant);   // (NOTE LENGTH STRETCH without KEEP FORMANTS: plain transposition)
        v.st.setFormantBase ((float) (juce::MidiMessage::getMidiNoteInHertz (p.root) / sr));
        const int preLen = (int) v.pre[0].size() - 8;
        fetch (v, p, d, v.pre, preLen, true);
        float* ip[2] = { v.pre[0].data(), v.pre[1].data() };
        v.st.outputSeek (ip, preLen);
        v.primed = true;
    }
};
} // namespace arpsmp
