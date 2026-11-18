#include "Repeat.h"
#include "SliceVoice.h"

namespace gslice::repeat
{
using namespace ghosty::slicevoice;

double period (const RepeatOp& r, double bpm) { return retrig::periodSeconds (bpm, r.rateDiv, r.rateMod); }

std::vector<Trigger> triggers (const RepeatOp& r, double bpm, double sampleRate)
{
    std::vector<Trigger> out;
    const double P = period (r, bpm), D = r.length / juce::jmax (1.0, sampleRate);
    if (! (P > 0) || ! (D > 0)) return out;
    std::vector<double> t;
    if (r.ratchetOn) retrig::curveTimes (P, r.ratchet.data(), (int) r.ratchet.size(), D, t);
    else if (std::abs (r.ramp - 1.0) > 1e-9) retrig::rampTimes (P, juce::jlimit (0.25, 4.0, r.ramp), D, t);
    else retrig::constantTimes (P, D, t);
    if (t.empty()) return out;
    // swing: odd triggers late by (s - 0.5) * 2 * (the local period)
    std::vector<double> sw (t.size());
    for (size_t n = 0; n < t.size(); ++n)
    {
        const double local = n + 1 < t.size() ? t[n + 1] - t[n] : (n > 0 ? t[n] - t[n - 1] : P);
        sw[n] = t[n] + retrig::swingOffset ((int) n, r.swing, local);
    }
    const int N = (int) t.size();
    for (int n = 0; n < N; ++n)
    {
        Trigger g; g.n = n; g.t = sw[(size_t) n];
        const double next = n + 1 < N ? sw[(size_t) n + 1] : D;
        g.len = std::min (juce::jlimit (0.1, 1.0, r.gate) * (next - g.t), D - g.t);
        if (! (g.len > 0)) continue;
        g.pitch = juce::jlimit (-24.0, 24.0, r.pitchRamp ? (N > 1 ? r.pitchSemis * (double) n / (double) (N - 1) : 0.0) : r.pitchSemis * n);
        g.reverse = r.reverse == 2 || (r.reverse == 1 && (n & 1) == 1);
        g.gain = std::pow (juce::jlimit (0.0, 1.0, r.decay), (double) n);
        g.fadeIn = juce::jmax (0.0, r.fadeInMs) * 0.001; g.fadeOut = juce::jmax (0.0, r.fadeOutMs) * 0.001;
        retrig::fitFades (g.len, g.fadeIn, g.fadeOut);
        out.push_back (g);
    }
    return out;
}

// a piece's map restricted to region-local [u, u + len) and re-based to 0 (for a reversed repetition)
static WarpMap subMap (const WarpMap& m, double u, double len)
{
    WarpMap s; s.t.push_back (0); s.s.push_back (m.srcAt (u));
    for (size_t i = 0; i < m.t.size(); ++i) if (m.t[i] > u + 1e-9 && m.t[i] < u + len - 1e-9) { s.t.push_back (m.t[i] - u); s.s.push_back (m.s[i]); }
    s.t.push_back (len); s.s.push_back (m.srcAt (u + len));
    return s;
}

void expand (std::vector<Piece>& pieces, const Project& p, double hostRate, double bpm)
{
    if (p.repeats.empty()) return;
    const double kT = hostRate / p.sampleRate;
    for (const auto& op : p.repeats)
    {
        const int64 S = (int64) std::llround (op.start * kT), E = (int64) std::llround (op.end() * kT);
        if (E - S < 16) continue;
        const auto base = pieces;   // the arrangement under the span (what the repetitions read)
        auto pieceAt = [&base] (int64 d) -> const Piece* { for (auto& pc : base) if (d >= pc.dst0 && d < pc.dst1) return &pc; return nullptr; };
        // cut the span out
        std::vector<Piece> kept;
        for (auto& pc : pieces)
        {
            if (pc.dst1 <= S || pc.dst0 >= E) { kept.push_back (pc); continue; }
            if (pc.dst0 < S) { Piece l = pc; l.dst1 = S; kept.push_back (l); }
            if (pc.dst1 > E) { Piece r = pc; r.u0 += E - pc.dst0; r.dst0 = E; kept.push_back (r); }
        }
        // the repetitions
        const auto tr = triggers (op, bpm, p.sampleRate);
        std::vector<Piece> reps;
        for (size_t k = 0; k < tr.size(); ++k)
        {
            const auto& g = tr[k];
            const int64 d0 = S + (int64) std::llround (g.t * hostRate);
            int64 len = (int64) std::llround (g.len * hostRate);
            if (k + 1 < tr.size()) len = std::min (len, S + (int64) std::llround (tr[k + 1].t * hostRate) - d0);
            len = std::min (len, E - d0);
            const int64 q = op.retrigger ? S : d0;   // where the repetition reads the arrangement
            const Piece* bp = pieceAt (q);
            if (bp == nullptr || len < 8) continue;
            len = std::min (len, bp->dst1 - q);
            if (len < 8) continue;
            Piece pc = *bp;
            pc.part = 10000 + (int) k; pc.repeatIndex = g.n; pc.repeatId = op.id;
            pc.dst0 = d0; pc.dst1 = d0 + len;
            const int64 uSrc = q - bp->dst0 + bp->u0;
            pc.u0 = uSrc;
            const bool rev = g.reverse && bp->hold == nullptr;
            if (rev)
            {   // a reversed repetition: its own map over [uSrc, uSrc + len), in the other direction
                const WarpMap sm = subMap (bp->map, (double) uSrc, (double) len);
                const size_t n = sm.t.size(); WarpMap mm;
                if (! bp->reverse)
                {   // forward source -> mirrored coordinates (like a reversed region)
                    pc.mirror = sm.s.front() + sm.s.back();
                    for (size_t i = 0; i < n; ++i) { mm.t.push_back ((double) len - sm.t[n - 1 - i]); mm.s.push_back (pc.mirror - sm.s[n - 1 - i]); }
                    pc.reverse = true;
                }
                else
                {   // a reversed region's piece played backwards = forwards
                    for (size_t i = 0; i < n; ++i) { mm.t.push_back ((double) len - sm.t[n - 1 - i]); mm.s.push_back (bp->mirror - 1.0 - sm.s[n - 1 - i]); }
                    pc.reverse = false; pc.mirror = 0;
                }
                mm.t.front() = 0; mm.t.back() = (double) len;
                if (! mm.valid()) continue;
                pc.map = mm; pc.u0 = 0; pc.regionLen = len; pc.fadeIn = pc.fadeOut = 0; pc.splices.clear();
            }
            else
            {
                // transient-lock splices outside this repetition don't matter (shape() / the splice only look inside)
            }
            pc.pitchOffset = bp->pitchOffset + g.pitch;   // (on top of the region's own PITCH)
            pc.stretch = pc.stretch || std::abs (g.pitch) > 1e-9 || (rev && ! pc.map.unity (1e-6));
            if (std::abs (g.pitch) > 1e-9) pc.splices.clear();
            pc.gain = (float) (bp->gain * g.gain);
            pc.fixedEdges = true;
            pc.edgeIn = juce::jmin<int64> (len / 2, (int64) std::llround (g.fadeIn * hostRate));
            pc.edgeOut = juce::jmin<int64> (len / 2, (int64) std::llround (g.fadeOut * hostRate));
            reps.push_back (std::move (pc));
        }
        pieces = kept;
        for (auto& r : reps) pieces.push_back (std::move (r));
        std::sort (pieces.begin(), pieces.end(), [] (const Piece& a, const Piece& b) { return a.dst0 < b.dst0; });
    }
}
} // namespace gslice::repeat

namespace gslice::ops
{
R addRepeat (const Project& in, double a, double b, int64* newId, juce::String* why)
{
    if (b < a) std::swap (a, b);
    if (b - a < in.sampleRate * 0.01) { if (why) *why = "select a range (at least 10 ms) to REPEAT"; return std::nullopt; }
    for (auto& r : in.repeats) if (a < r.end() && b > r.start) { if (why) *why = "this overlaps another REPEAT"; return std::nullopt; }
    Project p = in; RepeatOp r; r.id = p.nextId++; r.start = std::round (a); r.length = std::round (b - a);
    p.repeats.push_back (r);
    std::sort (p.repeats.begin(), p.repeats.end(), [] (const RepeatOp& x, const RepeatOp& y) { return x.start < y.start; });
    if (newId) *newId = r.id;
    return p;
}
R setRepeat (const Project& in, const RepeatOp& nr)
{
    Project p = in;
    for (auto& r : p.repeats)
        if (r.id == nr.id)
        {
            RepeatOp x = nr;
            x.length = std::max (in.sampleRate * 0.01, std::round (x.length)); x.start = std::max (0.0, std::round (x.start));
            x.rateDiv = juce::jlimit (4, 128, x.rateDiv); x.rateMod = juce::jlimit (0, 2, x.rateMod); x.reverse = juce::jlimit (0, 2, x.reverse);
            x.swing = juce::jlimit (0.5, 0.75, x.swing); x.ramp = juce::jlimit (0.25, 4.0, x.ramp); x.decay = juce::jlimit (0.0, 1.0, x.decay);
            x.gate = juce::jlimit (0.1, 1.0, x.gate); x.pitchSemis = juce::jlimit (-24.0, 24.0, x.pitchSemis);
            x.fadeInMs = juce::jlimit (0.0, 50.0, x.fadeInMs); x.fadeOutMs = juce::jlimit (0.0, 50.0, x.fadeOutMs);
            for (auto& c : x.ratchet) c = juce::jlimit (-2.0f, 2.0f, c);
            for (auto& o : p.repeats) if (o.id != x.id && x.start < o.end() && x.end() > o.start) return std::nullopt;   // no overlaps
            if (x == r) return std::nullopt;
            r = x;
            std::sort (p.repeats.begin(), p.repeats.end(), [] (const RepeatOp& a, const RepeatOp& b) { return a.start < b.start; });
            return p;
        }
    return std::nullopt;
}
R removeRepeat (const Project& in, int64 id)
{
    Project p = in;
    for (size_t i = 0; i < p.repeats.size(); ++i) if (p.repeats[i].id == id) { p.repeats.erase (p.repeats.begin() + (std::ptrdiff_t) i); return p; }
    return std::nullopt;
}
}
