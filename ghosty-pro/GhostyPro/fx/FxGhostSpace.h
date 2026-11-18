#pragma once
// GHOST SPACE + the GHOSTPRINT targets (Ghost Box only). Original code (no third-party sources).
//
// GHOST SPACE: a source moves through a 2-D field around a listener at the centre. Its distance sets the level, the high-frequency
// damping ("air"), the reverb pre-delay and the dry / reverb balance; its angle sets the pan, an inter-aural time and level difference
// (the far ear is a little later, quieter and darker) and the pattern of 6 early reflections (image sources of a box room: left / right /
// front / back wall, floor, ceiling). The reverb is the rack's own 8-line FDN (FxSpaceDsp.h Fdn8, with its freeze); GHOST mode uses the
// rack's PitchShifter on a delayed copy; the movement reuses the SPIRIT BOARD's ORBIT maths (Shared/SpiritBoard.h: orbitOffset, OrbitPath).
//   PATH  STILL       the dot stays where you put it
//         ORBIT       circles the listener (radius = DEPTH), one turn per RATE, in time with the song
//         FALL        falls away into the distance and a bigger room, then comes back (one fall per RATE)
//         GHOST       orbits like ORBIT; a delayed, pitch-shifted copy (TRAIL, GHOST PITCH) follows its path behind it
//         BLACK HOLE  spirals into the centre, is swallowed by a frozen, darkening reverb, then released (one cycle per RATE)
//         DRAWN       a drawn looping path (1 / 2 / 4 / 8 bars, host-synced), the same model as the board's DRAWN ORBIT
// Every derived value is smoothed (gains, filters, delays) so a jump of the dot, a path change or a transport jump never clicks.
// Switched off and settled, the stage returns without touching the buffer (bit-identical to the rack without it).
//
// GHOSTPRINT targets: three clean settings the GHOSTPRINT patterns (and anyone) can move with GHOST MOTION timeline lanes:
// VOLUME (linear gain), WIDTH (mid / side) and TONE (a 12 dB / oct low-pass). At their defaults the stage does nothing (bit-identical).
#include "FxCore.h"
#include "FxSpaceDsp.h"
#include "SpiritBoard.h"

#if GPFX_BOX
namespace gpfx {

enum SpacePathMode { spStill, spOrbit, spFall, spGhost, spHole, spDrawn };
inline const juce::StringArray& spacePathNames() { static juce::StringArray s { "STILL", "ORBIT", "FALL", "GHOST", "BLACK HOLE", "DRAWN" }; return s; }
inline const juce::StringArray& spaceRateNames() { static juce::StringArray s { "1/2 BAR", "1 BAR", "2 BARS", "4 BARS", "8 BARS" }; return s; }
inline double spaceRateBars (int i) noexcept { static const double b[] { 0.5, 1.0, 2.0, 4.0, 8.0 }; return b[juce::jlimit (0, 4, i)]; }
inline const juce::StringArray& spaceTrailNames() { static juce::StringArray s { "1/16", "1/8", "1/8 D", "1/4", "1/2" }; return s; }
inline double spaceTrailBeats (int i) noexcept { static const double b[] { 0.25, 0.5, 0.75, 1.0, 2.0 }; return b[juce::jlimit (0, 4, i)]; }

// ---------------------------------------------------------------- the movement (pure: the same song position always gives the same place)
// Field coordinates: x 0..1 left -> right, y 0..1 top -> bottom; the listener sits at (0.5, 0.5) facing up (y < 0.5 = in front).
struct SpaceMove
{
    int path = spStill; float x = 0.5f, y = 0.3f, depth = 0.6f; double periodBeats = 4.0; double trailBeats = 0.75;
    const float* drawn = nullptr; int drawnN = 0; double drawnBeats = 4.0;   // DRAWN: a baked table of one loop (x, y pairs)
};
struct SpacePos { float x = 0.5f, y = 0.3f; float swallow = 0.0f; bool freeze = false; float fall = 0.0f; float gx = 0.5f, gy = 0.5f; };
inline double spacePhase (double beats, double period) noexcept { return positiveMod (beats / std::max (1.0e-6, period), 1.0); }
inline float spaceSmoothstep (float a, float b, float x) noexcept { const float t = clampf ((x - a) / (b - a), 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }
inline juce::Point<float> spaceOrbitAt (const SpaceMove& m, double beats) noexcept
{   // ORBIT = the board's circle orbit around the listener (orbitOffset, synced: rate = turns per beat), starting at the dot's own angle
    ghosty::spirit::OrbitParams op; op.mode = ghosty::spirit::Orbit::Circle; op.sync = true; op.rate = (float) (1.0 / std::max (1.0e-6, m.periodBeats));
    op.depth = clampf (m.depth, 0.0f, 1.0f) * 0.5f;
    const float a0 = std::atan2 (m.y - 0.5f, m.x - 0.5f);   // the dot's angle (screen maths: y down)
    const auto o = ghosty::spirit::orbitOffset (op, beats);
    const float c = std::cos (a0), s = std::sin (a0);
    return { 0.5f + o.x * c - o.y * s, 0.5f + o.x * s + o.y * c };
}
inline SpacePos spacePositionAt (const SpaceMove& m, double beats) noexcept
{
    SpacePos p; p.x = clampf (m.x, 0.0f, 1.0f); p.y = clampf (m.y, 0.0f, 1.0f); p.gx = p.x; p.gy = p.y;
    switch (m.path)
    {
        case spOrbit: { const auto q = spaceOrbitAt (m, beats); p.x = q.x; p.y = q.y; break; }
        case spGhost:
        {
            const auto q = spaceOrbitAt (m, beats), g = spaceOrbitAt (m, beats - m.trailBeats);   // the copy is where the source was TRAIL ago
            p.x = q.x; p.y = q.y; p.gx = g.x; p.gy = g.y; break;
        }
        case spFall:
        {   // out along the dot's own direction to the edge of the field, then back (raised cosine)
            const float f = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) spacePhase (beats, m.periodBeats));
            float dx = p.x - 0.5f, dy = p.y - 0.5f; const float len = std::sqrt (dx * dx + dy * dy);
            if (len < 1.0e-4f) { dx = 0.0f; dy = -1.0f; } else { dx /= len; dy /= len; }
            const float tx = 0.5f + dx * 0.5f, ty = 0.5f + dy * 0.5f, k = f * clampf (m.depth, 0.0f, 1.0f);
            p.x += (tx - p.x) * k; p.y += (ty - p.y) * k; p.fall = k; break;
        }
        case spHole:
        {   // 0 .. 0.55 spiral in (two turns), swallowed from 0.25; 0.5 .. 0.85 frozen; 0.85 .. 1 released, the dot back at its place
            const float ph = (float) spacePhase (beats, m.periodBeats);
            const float r0 = std::sqrt ((p.x - 0.5f) * (p.x - 0.5f) + (p.y - 0.5f) * (p.y - 0.5f));
            const float a0 = std::atan2 (p.y - 0.5f, p.x - 0.5f);
            if (ph < 0.55f)
            {
                const float u = ph / 0.55f, r = r0 * (1.0f - u * u * (3.0f - 2.0f * u) * clampf (0.4f + 0.6f * m.depth, 0.0f, 1.0f));
                const float a = a0 + juce::MathConstants<float>::twoPi * 2.0f * u;
                p.x = 0.5f + r * std::cos (a); p.y = 0.5f + r * std::sin (a);
            }
            else if (ph < 0.85f) { const float r = r0 * (1.0f - clampf (0.4f + 0.6f * m.depth, 0.0f, 1.0f)); p.x = 0.5f + r * std::cos (a0); p.y = 0.5f + r * std::sin (a0); }
            p.swallow = ph < 0.85f ? spaceSmoothstep (0.25f, 0.55f, ph) : 1.0f - spaceSmoothstep (0.85f, 1.0f, ph);
            p.freeze = ph >= 0.5f && ph < 0.85f;
            break;
        }
        case spDrawn:
            if (m.drawn != nullptr && m.drawnN > 1)
            {
                const double t = spacePhase (beats, m.drawnBeats) * m.drawnN; const int i = (int) t; const float f = (float) (t - i);
                const int a = juce::jlimit (0, m.drawnN - 1, i), b = (a + 1) % m.drawnN;
                p.x = m.drawn[2 * a] + f * (m.drawn[2 * b] - m.drawn[2 * a]); p.y = m.drawn[2 * a + 1] + f * (m.drawn[2 * b + 1] - m.drawn[2 * a + 1]);
            }
            break;
        default: break;
    }
    p.x = clampf (p.x, 0.0f, 1.0f); p.y = clampf (p.y, 0.0f, 1.0f); p.gx = clampf (p.gx, 0.0f, 1.0f); p.gy = clampf (p.gy, 0.0f, 1.0f);
    return p;
}
// distance 0 (at the listener) .. 1 (the edge of the field's circle and beyond), angle in radians (0 = front, + = right)
inline float spaceDistance (float x, float y) noexcept { return clampf (2.0f * std::sqrt ((x - 0.5f) * (x - 0.5f) + (y - 0.5f) * (y - 0.5f)), 0.0f, 1.0f); }
inline float spaceAngle (float x, float y) noexcept { return std::atan2 (x - 0.5f, 0.5f - y); }
// the distance laws (also what the page and the tests read): level, air (low-pass Hz) and the reverb return, all monotone
inline float spaceDirectGain (float r) noexcept { return 1.0f / (1.0f + 3.0f * clampf (r, 0.0f, 1.0f)); }                         // 0 .. -12 dB
inline float spaceAirHz (float r, float air) noexcept { return 20000.0f * std::exp2 (-5.0f * clampf (air, 0.0f, 1.0f) * clampf (r, 0.0f, 1.0f)); }
inline float spaceVerbGain (float r, float verb) noexcept { return 2.0f * clampf (verb, 0.0f, 1.0f) * (0.22f + 0.13f * clampf (r, 0.0f, 1.0f)); }
inline float spacePreDelayMs (float r, float room) noexcept { return 4.0f + 36.0f * (1.0f - clampf (r, 0.0f, 1.0f)) * (0.35f + 0.65f * clampf (room, 0.0f, 1.0f)); }

// DRAWN path hand-over: the message thread bakes a loop into a free table; the audio thread reads the newest (three tables, no locks)
struct SpacePathTables
{
    static constexpr int kN = ghosty::spirit::kPathTable;
    std::array<std::array<float, 2 * kN>, 3> t {};
    std::array<double, 3> beats { 4.0, 4.0, 4.0 }; std::array<bool, 3> has {};
    std::atomic<int> latest { 0 }, inUse { 0 };
    void publish (const ghosty::spirit::OrbitPath& path)   // message thread
    {
        const int a = latest.load(), b = inUse.load();
        int w = 0; while (w == a || w == b) ++w;
        has[(size_t) w] = ! path.empty(); beats[(size_t) w] = path.loopBeats();
        if (has[(size_t) w]) for (int k = 0; k < kN; ++k) { const auto p = path.at ((double) k / kN); t[(size_t) w][(size_t) (2 * k)] = p.x; t[(size_t) w][(size_t) (2 * k + 1)] = p.y; }
        latest.store (w, std::memory_order_release);
    }
    int acquire() noexcept { const int k = latest.load (std::memory_order_acquire); inUse.store (k, std::memory_order_release); return k; }   // audio thread
};

class SpaceStage
{
public:
    P on, x, y, path, rate, depth, room, air, verb, ghostSt, trail;
    SpacePathTables tables; ghosty::spirit::OrbitPath drawnPath;   // (drawnPath: message thread; saved in the rack state)
    // display (relaxed, drawing only)
    std::atomic<float> liveX { 0.5f }, liveY { 0.3f }, liveGX { -1.0f }, liveGY { -1.0f }, liveSwallow { 0.0f }, liveLevel { 0.0f };
    std::atomic<bool> running { false };

    void attach (juce::AudioProcessorValueTreeState& s)
    {
        on.bind (s, "fx_gspace_on", 0); x.bind (s, "fx_gspace_x", 0.5f); y.bind (s, "fx_gspace_y", 0.3f); path.bind (s, "fx_gspace_path", 0);
        rate.bind (s, "fx_gspace_rate", 1); depth.bind (s, "fx_gspace_depth", 0.6f); room.bind (s, "fx_gspace_room", 0.5f); air.bind (s, "fx_gspace_air", 0.5f);
        verb.bind (s, "fx_gspace_verb", 0.5f); ghostSt.bind (s, "fx_gspace_ghost", -12.0f); trail.bind (s, "fx_gspace_trail", 2);
    }
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        ringL.prepare ((int) (1.2 * sr) + 64); ringR.prepare ((int) (1.2 * sr) + 64); ringM.prepare ((int) (1.2 * sr) + 64);
        fdn.prepare (sr, 3.0f); pitch.prepare (sr, 50.0f);
        aCtl = 1.0f - (float) std::exp (-(double) kCtl / (0.02 * sr));      // gains, filters, inter-aural delays: 20 ms
        aSlow = 1.0f - (float) std::exp (-(double) kCtl / (0.06 * sr));     // propagation / reflection / pre-delays: 60 ms (a soft doppler, never a jump)
        onLen = std::max (1, (int) (0.02 * sr));
        reset();
    }
    void reset()
    {
        ringL.clear(); ringR.clear(); ringM.clear(); fdn.clear(); pitch.clear();
        for (auto* f : { &lpL, &lpR, &erLpL, &erLpR, &revLpL, &revLpR, &ghLp, &holeL, &holeR }) f->s = 0.0f;
        inRamp.start ((int) (0.01 * sr)); ctlPos = 0; inited = false; onAmt = on.b() ? 1.0f : 0.0f; idle = ! on.b(); frozeWas = false;
    }
    void snap() { onAmt = on.b() ? 1.0f : 0.0f; inited = false; }
    bool active() const noexcept { return ! idle; }

    // audio thread: n <= kSub samples at the song position ctx.ppq (already latency-compensated by the rack)
    void process (float* L, float* R, int n, const BlockCtx& ctx) noexcept
    {
        const bool want = on.b();
        if (! want && idle) return;                       // off and settled: the buffer is untouched
        if (want && idle) { reset(); idle = false; onAmt = 0.0f; }
        const int tk = tables.acquire();
        for (int i = 0; i < n; ++i)
        {
            if (ctlPos == 0) control (ctx.ppqAt (i), ctx, tk);
            if (++ctlPos >= kCtl) ctlPos = 0;
            step();
            const float inL = L[i], inR = R[i], inM = 0.5f * (inL + inR);
            const float rp = inRamp.next();   // (after a reset the delay lines fade their input in: no edge reaches a later tap)
            ringL.push (inL * rp); ringR.push (inR * rp); ringM.push (inM * rp);
            // direct sound: propagation + inter-aural delay per ear (read from both channels so the source keeps its own width)
            const float dl = cur.prop + cur.itdL, dr = cur.prop + cur.itdR;
            const float lL = ringL.read (dl), rL = ringR.read (dl), lR = ringL.read (dr), rR = ringR.read (dr);
            const float w = cur.width, earL = 0.5f * ((1.0f + w) * lL + (1.0f - w) * rL), earR = 0.5f * ((1.0f + w) * rR + (1.0f - w) * lR);
            float dL = lpL.lp (earL, cur.aL) * cur.gL, dR = lpR.lp (earR, cur.aR) * cur.gR;
            // early reflections: 6 image sources, each its own delay / level / pan
            float eL = 0.0f, eR = 0.0f;
            for (int k = 0; k < kTaps; ++k) { const float v = ringM.readLinear (cur.tap[(size_t) k]); eL += v * cur.tapL[(size_t) k]; eR += v * cur.tapR[(size_t) k]; }
            eL = erLpL.lp (eL, cur.aEr); eR = erLpR.lp (eR, cur.aEr);
            // GHOST: the delayed, pitch-shifted copy where the source was a TRAIL ago
            // (the pitch shifter always runs while the stage is on, so the copy never starts from an empty or stale buffer)
            const float gs = ghLp.lp (pitch.process (ringM.readLinear (cur.trail)), cur.aGh);
            const float gL = gs * cur.ghL, gR = gs * cur.ghR;
            // reverb: pre-delayed, darkened send of the source (+ a little of the ghost) into the rack's FDN
            const float pl = revLpL.lp (ringL.readLinear (cur.pre), cur.aRev), pr = revLpR.lp (ringR.readLinear (cur.pre), cur.aRev);
            float wl = 0.0f, wr = 0.0f;
            fdn.process ((pl + 0.5f * gL) * cur.send, (pr + 0.5f * gR) * cur.send, wl, wr);
            wl = holeL.lp (wl, cur.aHole) * cur.ret; wr = holeR.lp (wr, cur.aHole) * cur.ret;
            const float oL = dL + eL + gL + wl, oR = dR + eR + gR + wr;
            // on / off: 20 ms crossfade with the input
            if (onAmt != (want ? 1.0f : 0.0f)) onAmt = clampf (onAmt + (want ? 1.0f : -1.0f) / (float) onLen, 0.0f, 1.0f);
            L[i] = inL + onAmt * (oL - inL); R[i] = inR + onAmt * (oR - inR);
            lvl = std::max (lvl * 0.9999f, std::abs (oL) + std::abs (oR));
        }
        liveLevel.store (lvl, std::memory_order_relaxed);
        if (! want && onAmt <= 0.0f) { idle = true; running.store (false, std::memory_order_relaxed); }   // (the state is cleared when it comes back on)
        else running.store (true, std::memory_order_relaxed);
    }

    static constexpr int kCtl = 16, kTaps = 6;

private:
    struct Lp1 { float s = 0.0f; float lp (float x, float a) noexcept { s += a * (x - s); return s; } };
    struct Ctl
    {
        float gL = 1, gR = 1, aL = 1, aR = 1, itdL = 0, itdR = 0, prop = 0, width = 1, aEr = 1, aRev = 1, aHole = 1, aGh = 1, send = 0, ret = 0, pre = 100, trail = 1000;
        float ghG = 0, ghL = 0, ghR = 0;
        std::array<float, kTaps> tap {}, tapL {}, tapR {};
    };
    static constexpr int idx (size_t off) noexcept { return (int) (off / sizeof (float)); }
    static constexpr int kFields = (int) (sizeof (Ctl) / sizeof (float));
    double sr = 48000; float aCtl = 0.1f, aSlow = 0.01f; int onLen = 960;
    Ring ringL, ringR, ringM; InRamp inRamp; Fdn8 fdn; PitchShifter pitch;
    Lp1 lpL, lpR, erLpL, erLpR, revLpL, revLpR, ghLp, holeL, holeR;
    Ctl tgt, sm, cur, inc; int ctlPos = 0; bool inited = false, idle = true, ghostOn = false, frozeWas = false; float onAmt = 0.0f, lvl = 0.0f;
    int fdnTick = 0; float roomNow = -1.0f, decayNow = -1.0f, dampNow = -1.0f; bool freezeNow = false;

    static float* fieldsOf (Ctl& c) noexcept { return &c.gL; }   // (Ctl is all floats, laid out in order)
    float lpCoef (float hz) const noexcept { return 1.0f - std::exp (-juce::MathConstants<float>::twoPi * clampf (hz, 20.0f, 0.45f * (float) sr) / (float) sr); }
    void step() noexcept { float* c = fieldsOf (cur); const float* d = fieldsOf (inc); for (int k = 0; k < kFields; ++k) c[k] += d[k]; }

    // every kCtl samples: the place, then every target, smoothed (one-pole) and spread linearly over the next kCtl samples
    void control (double ppq, const BlockCtx& ctx, int tk) noexcept
    {
        SpaceMove mv; mv.path = juce::jlimit (0, 5, path.i()); mv.x = x(); mv.y = y(); mv.depth = clampf (depth(), 0.0f, 1.0f);
        const double bpb = ctx.beatsPerBar > 0.0 ? ctx.beatsPerBar : 4.0;
        mv.periodBeats = spaceRateBars (rate.i()) * bpb; mv.trailBeats = spaceTrailBeats (trail.i());
        if (tables.has[(size_t) tk]) { mv.drawn = tables.t[(size_t) tk].data(); mv.drawnN = SpacePathTables::kN; mv.drawnBeats = tables.beats[(size_t) tk]; }
        const auto p = spacePositionAt (mv, ppq);
        liveX.store (p.x, std::memory_order_relaxed); liveY.store (p.y, std::memory_order_relaxed); liveSwallow.store (p.swallow, std::memory_order_relaxed);
        ghostOn = mv.path == spGhost;
        liveGX.store (ghostOn ? p.gx : -1.0f, std::memory_order_relaxed); liveGY.store (ghostOn ? p.gy : -1.0f, std::memory_order_relaxed);
        const float r = spaceDistance (p.x, p.y), th = spaceAngle (p.x, p.y), sn = std::sin (th), cs = std::cos (th), as = std::abs (sn);
        const float airK = clampf (air(), 0.0f, 1.0f), roomK = clampf (room() + (1.0f - room()) * p.fall, 0.0f, 1.0f), verbK = clampf (verb(), 0.0f, 1.0f);
        const float keep = 1.0f - p.swallow;
        // direct: distance level, inter-aural level / time difference, air + head shadow (far ear) + a darker back half
        const float g = spaceDirectGain (r) * keep, far = 1.0f - 0.6f * as, rear = cs < 0.0f ? -cs : 0.0f;
        const float fc = spaceAirHz (r, airK) * (1.0f - 0.45f * rear), fcFar = std::min (fc, 20000.0f * std::exp2 (-2.0f * as));
        const float itd = (float) (0.00066 * sr) * as;
        tgt.gL = g * (sn > 0 ? far : 1.0f) * (1.0f - 0.12f * rear); tgt.gR = g * (sn < 0 ? far : 1.0f) * (1.0f - 0.12f * rear);
        tgt.aL = lpCoef (sn > 0 ? fcFar : fc); tgt.aR = lpCoef (sn < 0 ? fcFar : fc);
        tgt.itdL = sn > 0 ? itd : 0.0f; tgt.itdR = sn < 0 ? itd : 0.0f;
        tgt.prop = (float) (0.010 * sr) * r;                       // up to 10 ms: a soft doppler on FALL, constant on an ORBIT
        tgt.width = 1.0f - 0.55f * as;                             // a source at the side narrows (it is one place now)
        // early reflections: the image sources of a box room (half size W metres) around the listener; delays relative to the direct
        const float W = 3.0f + 12.0f * roomK, H = 2.5f + 3.0f * roomK;
        const float sx = (p.x - 0.5f) * 2.0f * W, sy = (0.5f - p.y) * 2.0f * W, dDir = std::max (0.3f, std::sqrt (sx * sx + sy * sy));
        const float img[kTaps][2] { { -2.0f * W - sx, sy }, { 2.0f * W - sx, sy }, { sx, 2.0f * W - sy }, { sx, -2.0f * W - sy }, { sx, sy }, { sx, sy } };
        const float zz[kTaps] { 0.0f, 0.0f, 0.0f, 0.0f, 3.0f, 2.0f * H - 3.0f };   // floor (ears at 1.5 m) and ceiling
        const float erK = 2.0f * verbK * keep;
        for (int k = 0; k < kTaps; ++k)
        {
            const float ix = img[k][0], iy = img[k][1], d = std::sqrt (ix * ix + iy * iy + zz[k] * zz[k]);
            const float ps = juce::jlimit (-1.0f, 1.0f, ix / std::max (0.3f, std::sqrt (ix * ix + iy * iy)));
            const float gk = erK * 0.22f * 6.0f / (6.0f + d) * (k >= 4 ? 0.7f : 1.0f);
            tgt.tap[(size_t) k] = (float) (sr * ((d - dDir) / 343.0)) + tgt.prop + 8.0f;
            const float pa = (ps + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
            tgt.tapL[(size_t) k] = gk * std::cos (pa); tgt.tapR[(size_t) k] = gk * std::sin (pa);
        }
        tgt.aEr = lpCoef (7000.0f * std::exp2 (-2.5f * airK * r));
        // reverb: the send rises a little with distance and on a FALL; the return is normalised by the decay so a long room is not louder
        const float decay = 0.6f + 4.4f * roomK * roomK + 3.0f * p.fall;
        const float meanLen = 0.05f * (0.25f + roomK * (3.0f - 0.25f));
        const float gl = std::pow (10.0f, -3.0f * meanLen / decay), norm = std::sqrt (std::max (0.02f, 1.0f - gl * gl));
        tgt.send = (1.0f + 1.5f * p.swallow) * (1.0f + 0.6f * p.fall);
        tgt.ret = spaceVerbGain (r, verbK) * norm * 1.6f * (p.freeze ? 0.8f : 1.0f);
        tgt.pre = (float) (sr * 0.001 * spacePreDelayMs (r, roomK)) + tgt.prop;
        tgt.aRev = lpCoef (spaceAirHz (r, airK) * 0.8f);
        tgt.aHole = lpCoef (20000.0f * std::exp2 (-5.5f * p.swallow * (p.freeze ? 1.0f : 0.6f)));   // the swallowed sound darkens
        // GHOST copy: its own place (level, pan, air), TRAIL behind, GHOST PITCH
        if (ghostOn)
        {
            const float gr = spaceDistance (p.gx, p.gy), gth = spaceAngle (p.gx, p.gy);
            const float pa = (std::sin (gth) + 1.0f) * 0.25f * juce::MathConstants<float>::pi, gg = 0.7f * spaceDirectGain (gr);
            tgt.ghG = gg; tgt.ghL = gg * std::cos (pa) * 1.41421356f; tgt.ghR = gg * std::sin (pa) * 1.41421356f;
            tgt.ghL = std::min (tgt.ghL, gg); tgt.ghR = std::min (tgt.ghR, gg);
            tgt.aGh = lpCoef (spaceAirHz (gr, airK) * 0.6f);
            pitch.setSemis (clampf (ghostSt(), -24.0f, 24.0f));
        }
        else { tgt.ghG = tgt.ghL = tgt.ghR = 0.0f; }
        // TRAIL: while the copy is silent its delay jumps straight to the new length (a glide through a long delay would scrub)
        tgt.trail = (float) std::min (1.1 * sr, mv.trailBeats * ctx.samplesPerBeat);
        const bool trailJump = cur.ghG < 1.0e-4f;
        // the FDN's own settings (size / decay / damping / freeze) every 4 control steps: it smooths size and freeze itself
        if ((fdnTick++ & 3) == 0 || ! inited)
        {
            const float damp = 3000.0f + 9000.0f * (1.0f - airK * 0.7f);
            if (std::abs (roomK - roomNow) > 1.0e-4f || std::abs (decay - decayNow) > 1.0e-3f || std::abs (damp - dampNow) > 1.0f || p.freeze != freezeNow || ! inited)
            { fdn.set (roomK, decay, damp, 0.25f, p.freeze); roomNow = roomK; decayNow = decay; dampNow = damp; freezeNow = p.freeze; }
        }
        // smooth (start at the targets the first time), then the per-sample increments for the next kCtl samples
        float* t = fieldsOf (tgt); float* s = fieldsOf (sm); float* c = fieldsOf (cur); float* d = fieldsOf (inc);
        if (! inited) { sm = tgt; cur = tgt; inited = true; }
        if (trailJump) sm.trail = cur.trail = tgt.trail;
        for (int k = 0; k < kFields; ++k)
        {
            const bool slow = k == idx (offsetof (Ctl, prop)) || k == idx (offsetof (Ctl, pre)) || k == idx (offsetof (Ctl, trail))
                           || (k >= idx (offsetof (Ctl, tap)) && k < idx (offsetof (Ctl, tap)) + kTaps);   // prop, pre, trail, the tap delays
            s[k] += (slow ? aSlow : aCtl) * (t[k] - s[k]);
            d[k] = (s[k] - c[k]) / (float) kCtl;
        }
    }
};

// ---------------------------------------------------------------- GHOSTPRINT targets: VOLUME / WIDTH / TONE
class PrintStage
{
public:
    P vol, width, tone;
    void attach (juce::AudioProcessorValueTreeState& s) { vol.bind (s, "fx_print_vol", 1.0f); width.bind (s, "fx_print_width", 1.0f); tone.bind (s, "fx_print_tone", 1.0f); }
    void prepare (double sampleRate) { sr = sampleRate; a = 1.0f - (float) std::exp (-1.0 / (0.002 * sr)); reset(); }
    void reset() { gV = clampf (vol(), 0.0f, 1.0f); gW = clampf (width(), 0.0f, 2.0f); gT = clampf (tone(), 0.0f, 1.0f); s1 = {}; s2 = {}; }
    void snap() { reset(); }
    static float toneHz (float t) noexcept { return 250.0f * std::exp2 (6.3f * clampf (t, 0.0f, 1.0f)); }   // 250 Hz .. 20 kHz
    void process (float* L, float* R, int n) noexcept
    {
        const float tv = clampf (vol(), 0.0f, 1.0f), tw = clampf (width(), 0.0f, 2.0f), tt = clampf (tone(), 0.0f, 1.0f);
        if (tv == 1.0f && tw == 1.0f && tt == 1.0f && gV == 1.0f && gW == 1.0f && gT == 1.0f) return;   // at the defaults: untouched
        for (int i = 0; i < n; ++i)
        {
            gV += a * (tv - gV); gW += a * (tw - gW); gT += a * (tt - gT);
            if (std::abs (gV - tv) < 1.0e-6f) gV = tv;
            if (std::abs (gW - tw) < 1.0e-6f) gW = tw;
            if (std::abs (gT - tt) < 1.0e-6f) gT = tt;
            float l = L[i], r = R[i];
            if (gT < 1.0f)
            {   // TONE: two TPT one-poles (12 dB / oct), faded in over the top 5 % so the default is exactly the input
                if ((cnt++ & 15) == 0 || std::abs (gT - lastT) > 0.002f) { const float w = std::tan (juce::MathConstants<float>::pi * std::min (toneHz (gT), 0.45f * (float) sr) / (float) sr); k = w / (1.0f + w); lastT = gT; }
                const float b = clampf ((1.0f - gT) * 20.0f, 0.0f, 1.0f);
                const float yl = s2[0].tick (s1[0].tick (l, k), k), yr = s2[1].tick (s1[1].tick (r, k), k);
                l += b * (yl - l); r += b * (yr - r);
            }
            else { s1[0].z = s1[1].z = s2[0].z = s2[1].z = 0.0f; }
            if (gW != 1.0f) { const float m = 0.5f * (l + r), sd = 0.5f * (l - r) * gW; l = m + sd; r = m - sd; }
            L[i] = l * gV; R[i] = r * gV;
        }
    }
private:
    struct Tpt { float z = 0.0f; float tick (float x, float k) noexcept { const float v = (x - z) * k, y = v + z; z = y + v; return y; } };
    double sr = 48000; float a = 0.01f, gV = 1, gW = 1, gT = 1, k = 1, lastT = -1; int cnt = 0;
    std::array<Tpt, 2> s1 {}, s2 {};
};

inline void addSpaceParameters (juce::AudioProcessorValueTreeState::ParameterLayout& l)
{   // (appended at the very end of the Ghost Box parameter list: no existing automation index moves; defaults = off, today's sound)
    addBool (l, "fx_gspace_on", "FX Ghost Space On", false);
    addFloat (l, "fx_gspace_x", "FX Ghost Space X", juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f, "", 2);
    addFloat (l, "fx_gspace_y", "FX Ghost Space Y", juce::NormalisableRange<float> (0.0f, 1.0f), 0.3f, "", 2);
    addChoice (l, "fx_gspace_path", "FX Ghost Space Path", spacePathNames(), 0);
    addChoice (l, "fx_gspace_rate", "FX Ghost Space Rate", spaceRateNames(), 1);
    addPct (l, "fx_gspace_depth", "FX Ghost Space Depth", 0.6f);
    addPct (l, "fx_gspace_room", "FX Ghost Space Room", 0.5f);
    addPct (l, "fx_gspace_air", "FX Ghost Space Air", 0.5f);
    addPct (l, "fx_gspace_verb", "FX Ghost Space Reverb", 0.5f);
    addFloat (l, "fx_gspace_ghost", "FX Ghost Space Ghost Pitch", juce::NormalisableRange<float> (-24.0f, 24.0f, 1.0f), -12.0f, "st", 0);
    addChoice (l, "fx_gspace_trail", "FX Ghost Space Ghost Trail", spaceTrailNames(), 2);
}
inline void addPrintParameters (juce::AudioProcessorValueTreeState::ParameterLayout& l)
{   // GHOSTPRINT targets (appended after GHOST SPACE; defaults = exactly the input)
    addPct (l, "fx_print_vol", "FX Ghostprint Volume", 1.0f);
    addFloat (l, "fx_print_width", "FX Ghostprint Width", juce::NormalisableRange<float> (0.0f, 2.0f), 1.0f, "", 2);
    addPct (l, "fx_print_tone", "FX Ghostprint Tone", 1.0f);
}

} // namespace gpfx
#endif
