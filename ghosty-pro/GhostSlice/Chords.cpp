// GHOST SLICE chord track: see Chords.h (the method and the papers it follows).
#include "Chords.h"
#include "AnalysisInternal.h"

namespace gslice::analysis
{
using namespace detail;
static constexpr double kPi = 3.14159265358979323846;
static constexpr int kQualities = 5;                       // maj min 7 dim sus (RootOnly is a label, not a template)
static constexpr int kStates = 12 * kQualities + 1;         // + N.C. (the last state)

std::vector<int> chordTones (ChordQuality q)
{
    switch (q)
    {
        case ChordQuality::Maj: return { 0, 4, 7 };
        case ChordQuality::Min: return { 0, 3, 7 };
        case ChordQuality::Dom7: return { 0, 4, 7, 10 };
        case ChordQuality::Dim: return { 0, 3, 6 };
        case ChordQuality::Sus: return { 0, 5, 7 };
        case ChordQuality::RootOnly: return { 0 };
    }
    return { 0 };
}

juce::String Chord::name (int transpose) const
{
    if (root < 0) return "N.C.";
    const juce::String r = pitchClassName (((root + transpose) % 12 + 12) % 12);
    switch (quality)
    {
        case ChordQuality::Maj: return r;
        case ChordQuality::Min: return r + "m";
        case ChordQuality::Dom7: return r + "7";
        case ChordQuality::Dim: return r + "dim";
        case ChordQuality::Sus: return r + "sus";
        case ChordQuality::RootOnly: return r + "?";
    }
    return r;
}

const Chord* ChordTrack::at (int64 s) const
{
    auto it = std::upper_bound (chords.begin(), chords.end(), s, [] (int64 v, const Chord& c) { return v < c.s1; });
    return it != chords.end() && it->s0 <= s ? &*it : nullptr;
}

double ChordTrack::chordShare() const
{
    if (frames <= 0) return 0.0;
    int64 t = 0; for (auto& c : chords) if (c.isChord()) t += c.s1 - c.s0;
    return (double) t / (double) frames;
}

juce::String ChordTrack::digest() const
{
    Hasher h; h.add ("chords").add (kChordVersion).add (frames).add ((int) chords.size());
    for (auto& c : chords) h.add (c.s0).add (c.s1).add (c.root).add ((int) c.quality).add (c.confidence);
    return h.hex();
}

// ---------------------------------------------------------------- per-frame pitch-class profiles from multi-f0
namespace
{
struct Frames
{
    int N = 0, H = 0, T = 0;
    std::vector<std::array<double, 12>> chroma;   // L2-normalised (zero = nothing pitched)
    std::vector<uint8_t> forcedNC;                // silent / one pitch class only
};

Frames profiles (const float* x, int64 n, double sr, const ChordSettings& cs, const std::atomic<bool>* cancel)
{
    Frames F;
    F.N = nextPow2 ((int) std::ceil (0.17 * sr)); F.H = F.N / 4;
    const int N = F.N, H = F.H, K = N / 2;
    F.T = n > 0 ? (int) ((n + H - 1) / H) : 0;
    F.chroma.assign ((size_t) F.T, {}); F.forcedNC.assign ((size_t) F.T, 1);
    if (F.T == 0) return F;
    FFT fft (N); const auto w = hann (N);
    std::vector<double> re ((size_t) N), im ((size_t) N), mag ((size_t) K), wm ((size_t) K), cum ((size_t) K + 1);
    std::vector<double> rms ((size_t) F.T, 0.0);
    // frame levels first (the silence gate is relative to the loudest frame)
    for (int f = 0; f < F.T; ++f)
    {
        const int64 c = (int64) f * H, a = std::max<int64> (0, c - N / 2), b = std::min<int64> (n, c + N / 2);
        double e = 0; for (int64 i = a; i < b; ++i) e += (double) x[i] * x[i];
        rms[(size_t) f] = b > a ? std::sqrt (e / (double) N) : 0.0;
    }
    double peak = 0; for (double v : rms) peak = std::max (peak, v);
    const double gate = std::max (std::pow (10.0, cs.silenceDb / 20.0), peak * std::pow (10.0, -45.0 / 20.0));
    const double fmax = std::min (5000.0, 0.45 * sr);
    for (int f = 0; f < F.T; ++f)
    {
        if ((f & 15) == 0 && cancel != nullptr && cancel->load()) return {};
        if (rms[(size_t) f] < gate || peak <= 0) continue;
        const int64 c = (int64) f * H;
        for (int i = 0; i < N; ++i) { const int64 p = c - N / 2 + i; re[(size_t) i] = (p >= 0 && p < n ? (double) x[p] : 0.0) * w[(size_t) i]; im[(size_t) i] = 0.0; }
        fft.transform (re.data(), im.data(), false);
        for (int k = 0; k < K; ++k) mag[(size_t) k] = std::sqrt (re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k]);
        // spectral whitening: divide by the (compressed) rms envelope of a +-1/6-octave band around each bin (Klapuri 2006, 2.1)
        cum[0] = 0; for (int k = 0; k < K; ++k) cum[(size_t) k + 1] = cum[(size_t) k] + mag[(size_t) k] * mag[(size_t) k];
        wm[0] = 0;
        for (int k = 1; k < K; ++k)
        {
            const int a = std::max (1, (int) (k / 1.12)), b = std::min (K - 1, (int) (k * 1.12) + 1);
            const double env = std::sqrt ((cum[(size_t) b + 1] - cum[(size_t) a]) / (b - a + 1)) + 1e-9;
            wm[(size_t) k] = mag[(size_t) k] * std::pow (env, -0.67);
        }
        // the plain pitch-class profile of the whitened spectrum (Fujishima 1999: the peak per semitone band, folded): it keeps the
        // chord tones that sit on the partials of a lower note (the cancellation below removes those with the partials)
        std::array<double, 12> raw {};
        for (int m = cs.minMidi; m <= 96; ++m)
        {
            const double fc = 440.0 * std::exp2 ((m - 69) / 12.0); if (fc >= fmax) break;
            const int a = std::max (1, (int) std::floor (fc * 0.9715 * N / sr)), b = std::min (K - 1, (int) std::ceil (fc * 1.0293 * N / sr));
            double mx = 0; for (int k = a; k <= b; ++k) mx = std::max (mx, wm[(size_t) k]);
            const double reg = m <= cs.melodyFrom ? 1.0 : std::max (0.3, 1.0 - 0.1 * (m - cs.melodyFrom));
            raw[(size_t) (m % 12)] += reg * mx;
        }
        // iterative estimation + cancellation
        std::array<double, 12> ch {}; double first = 0, lowRef = 0; int found = 0;
        for (int it = 0; it < cs.maxNotes; ++it)
        {
            double best = -1, sum = 0; int bm = -1, cand = 0;
            for (int m = cs.minMidi; m <= cs.maxMidi; ++m)
            {
                const double f0 = 440.0 * std::exp2 ((m - 69) / 12.0); double s = 0;
                for (int h = 1; h <= 20 && h * f0 < fmax; ++h)
                {
                    const double kc = h * f0 * N / sr; const int a = (int) std::floor (kc * 0.985), b = (int) std::ceil (kc * 1.015);
                    double mx = 0; for (int k = std::max (1, a); k <= b && k < K; ++k) mx = std::max (mx, wm[(size_t) k]);
                    s += (f0 + 52.0) / (h * f0 + 320.0) * mx;   // Klapuri's partial weighting (alpha = 52 Hz, beta = 320 Hz)
                }
                sum += s; ++cand;
                if (s > best) { best = s; bm = m; }
            }
            if (bm < 0 || best <= 0) break;
            if (it == 0)
            {   // pitchedness: a harmonic sound stands far above the mean salience of all candidates, noise does not
                first = best;
                if (best < cs.minPeakiness * sum / cand) break;
            }
            // the stop rule is relative to the strongest note of the chord register: a loud melody note on top must not end the
            // search before the chord tones under it are found (until one is found, half the first note's salience)
            if (best < cs.stopRatio * (lowRef > 0 ? lowRef : 0.5 * first)) break;
            if (lowRef <= 0 && bm <= cs.melodyFrom) lowRef = best;
            // register weight: chords live in the low / middle register, a melody on top weighs less (it passes through)
            const double reg = bm <= cs.melodyFrom ? 1.0 : std::max (0.3, 1.0 - 0.1 * (bm - cs.melodyFrom));
            ch[(size_t) (bm % 12)] += reg * best / first; ++found;
            const double f0 = 440.0 * std::exp2 ((bm - 69) / 12.0);
            for (int h = 1; h <= 20 && h * f0 < fmax; ++h)
            {
                const double kc = h * f0 * N / sr; const int a = (int) std::floor (kc * 0.985), b = (int) std::ceil (kc * 1.015);
                for (int k = std::max (1, a); k <= b && k < K; ++k) wm[(size_t) k] *= 0.15;
            }
        }
        double nn = 0; int classes = 0; double mx = 0;
        for (double v : ch) { nn += v * v; mx = std::max (mx, v); }
        for (double v : ch) classes += v > 0.25 * mx ? 1 : 0;
        if (nn <= 0 || found == 0) continue;
        const bool onePitch = classes < 2 && found <= 2;   // one note (a root doubled in octaves, its third + fifth cancelled, is found 3+ times)
        nn = std::sqrt (nn); for (auto& v : ch) v /= nn;
        {   // the blend: the multi-f0 profile + a share of the plain one (each normalised), then classes from the blend
            double rn = 0; for (double v : raw) rn += v * v; rn = std::sqrt (rn);
            if (rn > 0) for (int k = 0; k < 12; ++k) ch[(size_t) k] += cs.rawShare * raw[(size_t) k] / rn;
            double bn = 0; mx = 0; for (double v : ch) { bn += v * v; mx = std::max (mx, v); }
            bn = std::sqrt (bn); for (auto& v : ch) v /= bn;
            mx /= bn; classes = 0; for (double v : ch) classes += v > 0.25 * mx ? 1 : 0;
        }
        F.chroma[(size_t) f] = ch;
        F.forcedNC[(size_t) f] = onePitch || classes < 2 ? 1 : 0;
    }
    return F;
}

struct Tpl { std::array<double, 12> v {}; int root = 0; ChordQuality q = ChordQuality::Maj; };
std::vector<Tpl> templates()
{
    std::vector<Tpl> t;
    for (int q = 0; q < kQualities; ++q)
        for (int r = 0; r < 12; ++r)
        {
            Tpl p; p.root = r; p.q = (ChordQuality) q;
            const auto tones = chordTones (p.q);
            for (size_t i = 0; i < tones.size(); ++i) p.v[(size_t) ((r + tones[i]) % 12)] = i == 3 ? 0.85 : 1.0;   // (the 7th a little lighter)
            double nn = 0; for (double v : p.v) nn += v * v; nn = std::sqrt (nn); for (auto& v : p.v) v /= nn;
            t.push_back (p);
        }
    return t;
}
inline double dot (const std::array<double, 12>& a, const std::array<double, 12>& b) { double s = 0; for (int i = 0; i < 12; ++i) s += a[(size_t) i] * b[(size_t) i]; return s; }
} // namespace

ChordTrack detectChords (const float* x, int64 n, double sr, const ChordSettings& cs, const std::atomic<bool>* cancel)
{
    ChordTrack out; out.frames = n;
    if (n <= 0 || sr <= 0) return out;
    const auto F = profiles (x, n, sr, cs, cancel);
    if (cancel != nullptr && cancel->load()) return {};
    if (F.T == 0) { out.chords.push_back ({ 0, n, -1, ChordQuality::RootOnly, 1.0f }); return out; }
    const auto T = templates();
    const int S = kStates, NC = S - 1, Tn = F.T;
    // emission scores (cosine units) and Viterbi with a constant change penalty: best predecessor = stay, or the best state - penalty
    std::vector<double> score ((size_t) Tn * S);
    for (int f = 0; f < Tn; ++f)
    {
        double* e = &score[(size_t) f * S];
        if (F.forcedNC[(size_t) f]) { for (int s = 0; s < NC; ++s) e[s] = 0.0; e[NC] = 1.0; continue; }
        for (int s = 0; s < NC; ++s) e[s] = dot (F.chroma[(size_t) f], T[(size_t) s].v);
        e[NC] = cs.noChord;
    }
    std::vector<double> acc ((size_t) S), nxt ((size_t) S);
    std::vector<int> back ((size_t) Tn * S);
    for (int s = 0; s < S; ++s) acc[(size_t) s] = score[(size_t) s];
    for (int f = 1; f < Tn; ++f)
    {
        int bs = 0; for (int s = 1; s < S; ++s) if (acc[(size_t) s] > acc[(size_t) bs]) bs = s;
        const double* e = &score[(size_t) f * S];
        for (int s = 0; s < S; ++s)
        {
            const double stay = acc[(size_t) s], jump = acc[(size_t) bs] - cs.changePenalty;
            const bool st = stay >= jump || bs == s;
            nxt[(size_t) s] = (st ? stay : jump) + e[s];
            back[(size_t) f * S + s] = st ? s : bs;
        }
        std::swap (acc, nxt);
    }
    std::vector<int> path ((size_t) Tn);
    { int bs = 0; for (int s = 1; s < S; ++s) if (acc[(size_t) s] > acc[(size_t) bs]) bs = s; path[(size_t) Tn - 1] = bs; }
    for (int f = Tn - 1; f > 0; --f) path[(size_t) f - 1] = back[(size_t) f * S + path[(size_t) f]];

    // the onset strength (5 ms hop) for the boundary snap
    const int oN = nextPow2 ((int) std::ceil (0.043 * sr)), oH = std::max (1, oN / 8);
    const auto odf = odfFrames (x, n, sr, oN, oH, cancel);
    if (cancel != nullptr && cancel->load()) return {};
    auto snap = [&] (int64 b) -> int64
    {   // the strongest flux rise within one hop of b, if it stands out; then sample accurate
        if (odf.flux.empty()) return b;
        const int64 a0 = std::max<int64> (0, b - F.H), a1 = std::min<int64> (n, b + F.H);
        int bf = -1; double bv = 0;
        std::vector<double> loc;
        for (int64 p = a0; p < a1; p += oH)
        {
            const int k = (int) (p / oH); if (k < 0 || k >= (int) odf.flux.size()) continue;
            const double v = odf.flux[(size_t) k] + 0.5 * odf.hfc[(size_t) k];
            loc.push_back (v);
            if (v > bv) { bv = v; bf = k; }
        }
        if (bf < 0 || loc.size() < 4 || bv < 2.0 * median (loc) + 1e-12) return b;
        const int64 c = (int64) bf * oH;
        return refineOnset (x, n, sr, std::max<int64> (0, c - oN / 2 - (int64) (0.01 * sr)), std::min<int64> (n, c + oN / 2));
    };

    // segments (frame f covers [f*H - H/2, f*H + H/2)), each change snapped
    std::vector<std::pair<int64, int>> segs;   // (start, state)
    for (int f = 0; f < Tn; ++f)
        if (f == 0 || path[(size_t) f] != path[(size_t) f - 1])
            segs.push_back ({ f == 0 ? 0 : path[(size_t) f] == NC ? (int64) f * F.H - F.H / 2 : snap ((int64) f * F.H - F.H / 2), path[(size_t) f] });   // (an end is no onset)
    for (size_t i = 1; i < segs.size(); ++i) segs[i].first = juce::jlimit (segs[i - 1].first + 1, n - 1, segs[i].first);
    for (size_t i = 0; i < segs.size(); ++i)
    {
        Chord c; c.s0 = segs[i].first; c.s1 = i + 1 < segs.size() ? segs[i + 1].first : n;
        if (c.s1 <= c.s0) continue;
        const int st = segs[i].second;
        // the segment's mean profile: confidence (vs the best other root) and whether the quality is clear
        std::array<double, 12> m {}; int cnt = 0;
        const int f0 = (int) juce::jlimit<int64> (0, Tn - 1, (c.s0 + F.H / 2) / F.H), f1 = (int) juce::jlimit<int64> (f0 + 1, Tn, (c.s1 + F.H / 2) / F.H);
        for (int f = f0; f < f1; ++f) if (! F.forcedNC[(size_t) f]) { for (int k = 0; k < 12; ++k) m[(size_t) k] += F.chroma[(size_t) f][(size_t) k]; ++cnt; }
        if (st == NC || cnt == 0) { c.root = -1; c.quality = ChordQuality::RootOnly; c.confidence = 1.0f; }
        else
        {
            double nn = 0; for (double v : m) nn += v * v; nn = std::sqrt (nn); for (auto& v : m) v /= std::max (1e-12, nn);
            const auto& tp = T[(size_t) st];
            double own = dot (m, tp.v), sameRoot = 0, otherRoot = 0;
            for (int s = 0; s < NC; ++s)
            {
                if (s == st) continue;
                const double v = dot (m, T[(size_t) s].v);
                if (T[(size_t) s].root == tp.root) sameRoot = std::max (sameRoot, v); else otherRoot = std::max (otherRoot, v);
            }
            c.root = tp.root;
            c.quality = own - sameRoot >= cs.qualityMargin ? tp.q : ChordQuality::RootOnly;
            c.confidence = (float) juce::jlimit (0.0, 1.0, 0.5 + 4.0 * (own - otherRoot));
        }
        if (! out.chords.empty() && out.chords.back().root == c.root && out.chords.back().quality == c.quality)
        { auto& b = out.chords.back(); b.confidence = std::max (b.confidence, c.confidence); b.s1 = c.s1; continue; }   // (two states, one label)
        out.chords.push_back (c);
    }
    if (out.chords.empty()) out.chords.push_back ({ 0, n, -1, ChordQuality::RootOnly, 1.0f });
    out.chords.front().s0 = 0; out.chords.back().s1 = n;
    return out;
}

// ---------------------------------------------------------------- key with chords
double diatonicFit (int root, ChordQuality q, int tonic, bool minor)
{
    if (root < 0 || tonic < 0) return 0.0;
    const int d = ((root - tonic) % 12 + 12) % 12;
    static const int majScale[7] { 0, 2, 4, 5, 7, 9, 11 }, minScale[7] { 0, 2, 3, 5, 7, 8, 10 };
    // the diatonic triads (minor: natural minor + the harmonic V / V7)
    static const ChordQuality majQ[7] { ChordQuality::Maj, ChordQuality::Min, ChordQuality::Min, ChordQuality::Maj, ChordQuality::Maj, ChordQuality::Min, ChordQuality::Dim };
    static const ChordQuality minQ[7] { ChordQuality::Min, ChordQuality::Dim, ChordQuality::Maj, ChordQuality::Min, ChordQuality::Min, ChordQuality::Maj, ChordQuality::Maj };
    const int* sc = minor ? minScale : majScale;
    for (int i = 0; i < 7; ++i)
    {
        if (sc[i] != d) continue;
        const ChordQuality want = minor ? minQ[i] : majQ[i];
        if (q == want || q == ChordQuality::RootOnly) return 1.0;
        if (q == ChordQuality::Sus) return 0.9;
        if (q == ChordQuality::Dom7) return (! minor && i == 4) || (minor && i == 4) ? 1.0 : 0.6;   // V7 (minor: the harmonic V7)
        if (minor && i == 4 && q == ChordQuality::Maj) return 1.0;                                  // harmonic minor V
        return 0.6;
    }
    return 0.0;
}

Key estimateKeyWithChords (const std::vector<Note>& notes, const ChordTrack& track)
{
    double chordTime = 0;
    for (auto& c : track.chords) if (c.isChord()) chordTime += (double) (c.s1 - c.s0) * c.confidence;
    if (chordTime <= 0 || track.chordShare() < 0.05) return estimateKey (notes);
    std::array<double, 12> wn {}, wc {};
    double sn = 0, sc = 0;
    for (auto& n : notes)
    {
        if (n.confidence < 0.3f) continue;
        const int pc = (((int) std::lround (n.pitch)) % 12 + 12) % 12;
        const double v = (double) (n.s1 - n.s0) * (double) n.loudness; wn[(size_t) pc] += v; sn += v;
    }
    for (auto& c : track.chords)
    {
        if (! c.isChord()) continue;
        const double d = (double) (c.s1 - c.s0) * c.confidence;
        const auto tones = chordTones (c.quality == ChordQuality::RootOnly ? ChordQuality::Maj : c.quality);
        for (size_t i = 0; i < tones.size(); ++i)
        {
            if (c.quality == ChordQuality::RootOnly && i == 1) continue;   // (root + fifth only when the third is unclear)
            const double v = d * (i == 0 ? 1.5 : 1.0); wc[(size_t) ((c.root + tones[i]) % 12)] += v; sc += v;
        }
    }
    // monophonic notes and chords together: the more of the music is chords, the less the (monophonic) note tracker is trusted
    const double alpha = sn > 0 ? juce::jlimit (0.6, 0.9, 0.5 + track.chordShare()) : 1.0;
    std::array<double, 12> w {};
    for (int i = 0; i < 12; ++i) w[(size_t) i] = (sn > 0 ? (1.0 - alpha) * wn[(size_t) i] / sn : 0.0) + alpha * wc[(size_t) i] / sc;
    Key k = estimateKey (w);
    if (! k.valid()) return estimateKey (notes);
    // chord cues (functional harmony, after Temperley, "The Cognition of Basic Musical Structures", 2001, ch. 7): how much of the
    // music is diatonic to each key, how much sits on its tonic chord, its authentic cadences (a major / 7th chord a fifth above
    // moving to the tonic chord) and whether the music starts / ends on the tonic chord
    std::vector<const Chord*> seq; for (auto& c : track.chords) if (c.isChord()) seq.push_back (&c);
    const double r0 = k.r[(size_t) (k.minor ? 12 + k.tonic : k.tonic)];
    for (int m = 0; m < 2; ++m)
        for (int t = 0; t < 12; ++t)
        {
            auto isTonic = [&] (const Chord& c) { return c.root == t && (c.quality == ChordQuality::RootOnly || c.quality == (m == 1 ? ChordQuality::Min : ChordQuality::Maj)); };
            double dia = 0, ton = 0, cad = 0;
            for (size_t i = 0; i < seq.size(); ++i)
            {
                const auto& c = *seq[i];
                const double d = (double) (c.s1 - c.s0) * c.confidence;
                dia += d * diatonicFit (c.root, c.quality, t, m == 1);
                ton += isTonic (c) ? d : 0.0;
                if (i > 0 && isTonic (c) && seq[i - 1]->root == (t + 7) % 12
                    && (seq[i - 1]->quality == ChordQuality::Maj || seq[i - 1]->quality == ChordQuality::Dom7 || seq[i - 1]->quality == ChordQuality::RootOnly))
                    cad += 1.0;
            }
            const double edges = (isTonic (*seq.front()) ? 1.0 : 0.0) + (isTonic (*seq.back()) ? 1.0 : 0.0);
            k.r[(size_t) (m * 12 + t)] += alpha * (0.35 * dia / chordTime + 0.5 * ton / chordTime + 0.12 * std::min (2.0, cad) + 0.15 * edges);
        }
    juce::ignoreUnused (r0);
    int best = 0; for (int i = 1; i < 24; ++i) if (k.r[(size_t) i] > k.r[(size_t) best]) best = i;
    k.minor = best >= 12; k.tonic = best % 12;
    const int rel = k.minor ? (k.tonic + 3) % 12 : 12 + (k.tonic + 9) % 12;
    double r2 = -1; for (int i = 0; i < 24; ++i) if (i != best && i != rel) r2 = std::max (r2, k.r[(size_t) i]);
    const double r1 = k.r[(size_t) best], rr = k.r[(size_t) rel];
    // the margin over the best other key and over the relative (the chords are what tells the relative keys apart)
    k.confidence = juce::jlimit (0.0, 1.0, 0.5 * (r1 - r2) / 0.25 + 0.5 * (r1 - rr) / 0.25);
    return k;
}
} // namespace gslice::analysis
