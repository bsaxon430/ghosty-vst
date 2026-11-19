// Ghost Graph -- GENERATE, version 2: writes new songs the way the reference videos are written.
//
// It doesn't throw random numbers at the screen. Every song follows one of the five videos' blueprints -- how its
// scenes build, which voices play, how the equations are shaped -- and fills it with NEW music, made with the same
// tricks the videos use:
//   * evenly spread rhythms (Euclidean): "mod(5n + o, 16) < 5" puts 5 hits as evenly as possible across 16 steps;
//   * riffs and chords stored as digit-numbers, read one digit (or two) per step or per bar;
//   * scales as one formula: floor((12d + a)/7) walks a 7-note mode, floor((12d + b)/5) a 5-note one;
//   * chords voiced so each voice moves to the NEAREST note of the next chord (smooth voice leading),
//     with the little scoop up into each chord the first video does;
//   * glides into the next chord in the last beat of a bar, fills at the end of the 4-bar phrase;
//   * riffs that answer themselves: bar 2 changes the ending of bar 1, bar 3 repeats bar 1, bar 4 turns around.
// Same seed -> same song.
#pragma once
#include <random>
#include <string>
#include <vector>
#include <map>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <algorithm>
#include "GraphExpr.h"

namespace gg {

class Generator
{
public:
    static constexpr int kStyles = 5;
    static const char* styleName (int s)
    {
        static const char* n[] = { "Drift (like Track 1)", "Riff house (like Track 2)", "Breaks (like Track 3)",
                                   "Cosine song (like Track 4)", "Acid (like Track 5)" };
        return n[std::clamp (s, 0, kStyles - 1)];
    }

    explicit Generator (uint32_t seed, int style = -1) : rng (seed)
    {
        styleIdx = style >= 0 && style < kStyles ? style : pick (0, kStyles - 1);
        song();
    }

    std::vector<std::string> track()
    {
        switch (styleIdx)
        {
            case 0:  return drift();
            case 1:  return riffHouse();
            case 2:  return breaks();
            case 3:  return cosineSong();
            default: return acid();
        }
    }
    // a new take on one scene of this song (same key, chords and tempo; new rhythm and notes)
    std::string sceneAgain (int index)
    {
        rng.seed (rng() ^ 0x5bd1e995u);
        rerollParts();
        auto t = track();
        return t[(size_t) std::clamp (index, 0, (int) t.size() - 1)];
    }

    double bpm() const   { return tempo; }
    float wave() const   { return (float) waveIdx; }
    float tune() const   { return (float) tuneSt; }
    float tone() const   { return toneHz; }
    int style() const    { return styleIdx; }

private:
    std::mt19937 rng;
    int styleIdx = 0, waveIdx = 3, tuneSt = 0;
    double tempo = 120;
    float toneHz = 20000.f;

    // the song: key and chords are kept when a scene is generated again
    int modeA = 2;                    // floor((12d + modeA)/7): 2 aeolian, 3 dorian, 1 phrygian, 4 mixolydian, 5 major
    std::vector<int> progDeg;         // 4 chord roots, as scale steps
    std::vector<int> progSemi;        // the same roots in semitones (0 = the key)

    int pick (int lo, int hi) { return std::uniform_int_distribution<int> (lo, hi) (rng); }
    bool chance (int pct) { return pick (1, 100) <= pct; }
    template <typename T> const T& oneOf (const std::vector<T>& v) { return v[(size_t) pick (0, (int) v.size() - 1)]; }

    static std::string num (double v)
    {
        char buf[32]; std::snprintf (buf, sizeof buf, "%.3g", v);
        return buf;
    }
    static std::string I (long long v) { return std::to_string (v); }

    // fill @NAME@ placeholders
    static std::string fill (std::string s, const std::map<std::string, std::string>& m)
    {
        for (auto& kv : m)
        {
            const std::string key = "@" + kv.first + "@";
            for (size_t p = s.find (key); p != std::string::npos; p = s.find (key, p + kv.second.size()))
                s.replace (p, key.size(), kv.second);
        }
        return s;
    }

    int scaleSemi (int d) const   // the song's 7-note scale, in semitones (any octave)
    {
        return (int) std::floor ((12.0 * d + modeA) / 7.0);
    }
    int sc (int d) const { return scaleSemi (d) - scaleSemi (0); }   // semitones of scale step d above the key
    bool major() const { return modeA >= 4; }

    void song()
    {
        static const std::vector<int> modes { 2, 2, 2, 3, 3, 1, 4, 5 };   // mostly minor, like the videos
        modeA = oneOf (modes);
        // progressions the videos (and a lot of good music) use, as scale steps from the key
        static const std::vector<std::vector<int>> progs {
            { 0, -2, -4, -3 }, { 0, 5, 3, 4 }, { 0, -3, -2, -4 }, { 0, 3, 5, 4 }, { 0, -2, 3, 4 },
            { 0, 4, 5, 3 }, { 0, -2, -3, -4 }, { 0, 2, -2, 4 }, { 5, 3, 0, 4 } };
        progDeg = oneOf (progs);
        progSemi.clear();
        for (int d : progDeg) progSemi.push_back (scaleSemi (d) - scaleSemi (0));

        switch (styleIdx)
        {
            case 0:  tempo = 70 + 2 * pick (0, 5);  waveIdx = 4; tuneSt = oneOf (std::vector<int> { -7, -5, -9 }); toneHz = 20000.f; break;
            case 1:  tempo = 116 + 2 * pick (0, 4); waveIdx = 5; tuneSt = oneOf (std::vector<int> { 0, -2, 2, 3 }); toneHz = 20000.f; break;
            case 2:  tempo = 130 + 2 * pick (0, 6); waveIdx = 5; tuneSt = oneOf (std::vector<int> { 0, -2, 1 });    toneHz = 20000.f; break;
            case 3:  tempo = 124 + 2 * pick (0, 5); waveIdx = 3; tuneSt = oneOf (std::vector<int> { 0, -3, 2 });    toneHz = 20000.f; break;
            default: tempo = 126 + 2 * pick (0, 4); waveIdx = 3; tuneSt = oneOf (std::vector<int> { 0, -2, 3 });    toneHz = 2200.f;  break;
        }
        rerollParts();
    }

    // ------------------------------------------------------------------------------------------------ rhythm tools
    // an evenly spread rhythm of k hits in n steps, rotated by r: the mask has bit i set where step i plays
    static long long euclid (int k, int n, int r)
    {
        long long m = 0;
        for (int i = 0; i < n; ++i)
            if (((k * (i + r)) % n + n) % n < k) m |= (1LL << i);
        return m;
    }
    long long groove16 (int kMin, int kMax, bool onOne)
    {
        for (int tries = 0; tries < 20; ++tries)
        {
            const long long m = euclid (pick (kMin, kMax), 16, pick (0, 15));
            if (! onOne || (m & 1)) return m;
        }
        return euclid (kMin, 16, 0) | 1;
    }

    // ------------------------------------------------------------------------------------------------ melody tools
    // a 16-step riff as digits: 0 = rest, 1..4 = scale steps 0..3, 6..9 = the same an octave up (the second video's code)
    std::string riffDigits (long long rhythm, int startDeg, bool allowOctave)
    {
        std::string s;
        int d = startDeg;
        for (int i = 0; i < 16; ++i)
        {
            if (! (rhythm >> i & 1)) { s += '0'; continue; }
            if (i > 0)
            {
                const int step = oneOf (std::vector<int> { -1, -1, 0, 1, 1, 2, -2, 0 });   // mostly steps, a few leaps
                d = std::clamp (d + step, 0, 3);
            }
            const bool up = allowOctave && i > 0 && chance (18);
            s += (char) ('0' + d + 1 + (up ? 5 : 0));
        }
        if (s[0] == '0') s[0] = '1';   // the number must start with 1..8 (and every bar starts on the root)
        return s;
    }
    // bar 2 / bar 4: keep the start, write a new ending (call and answer)
    std::string answer (const std::string& a, int from, long long rhythm, bool allowOctave)
    {
        std::string b = a;
        const std::string tail = riffDigits (rhythm, pick (0, 3), allowOctave);
        for (int i = from; i < 16; ++i) b[(size_t) i] = tail[(size_t) i];
        return b;
    }

    // six voices, each moving to the nearest note of the next chord (smooth voice leading), as two-digit notes
    std::vector<std::string> voicings()
    {
        static const int lo[6] = { -14, -9, -5, -1, 2, 5 }, hi[6] = { -6, -2, 1, 4, 7, 10 };
        std::vector<std::vector<int>> notes (6, std::vector<int> (4, 0));
        int prev[6];
        for (int v = 0; v < 6; ++v) prev[v] = (lo[v] + hi[v]) / 2;
        for (int c = 0; c < 4; ++c)
        {
            // the chord: root, 3rd, 5th, 7th of the scale from this bar's root
            std::vector<int> pcs;
            for (int t = 0; t < 4; ++t) pcs.push_back (((scaleSemi (progDeg[(size_t) c] + 2 * t) - scaleSemi (0)) % 12 + 12) % 12);
            for (int v = 0; v < 6; ++v)
            {
                int best = prev[v], bd = 999;
                for (int n = lo[v]; n <= hi[v]; ++n)
                {
                    const int pc = ((n % 12) + 12) % 12;
                    if (std::find (pcs.begin(), pcs.end(), pc) == pcs.end()) continue;
                    if (v == 0 && pc != pcs[0]) continue;              // the lowest voice holds the root
                    int d = std::abs (n - prev[v]) * 2 + (pc == pcs[3] ? 2 : 0) + (c == 0 ? std::abs (n - (lo[v] + hi[v]) / 2) : 0);
                    for (int u = 0; u < v; ++u) if (notes[(size_t) u][(size_t) c] == n) d += 10;   // no doubled notes
                    if (d < bd) { bd = d; best = n; }
                }
                notes[(size_t) v][(size_t) c] = best;
                prev[v] = best;
            }
        }
        std::vector<std::string> out;
        for (int v = 0; v < 6; ++v)
        {
            std::string s;
            for (int c = 0; c < 4; ++c) { char b[8]; std::snprintf (b, sizeof b, "%02d", notes[(size_t) v][(size_t) c] + 30); s += b; }
            out.push_back (s);
        }
        return out;
    }

    // per-scene parts (re-rolled by "generate this scene again")
    struct Parts
    {
        long long hatMask = 0, bassMask = 0, acidMask = 0, hats32 = 0;
        int kA10 = 15, kD = 14, hiInt = 19, motorRot = 0, arpMul = 3, drumK = 5, drumO = 10, bellK = 7;
        std::string NA, NB, ND, NA2, NB2, ND2, WA, WB, WD, H1, H2;
        std::vector<int> coef;
    } P;

    void rerollParts()
    {
        P.hatMask = groove16 (9, 13, false);
        P.bassMask = groove16 (6, 10, true);
        P.acidMask = groove16 (9, 12, true);
        long long h = 0;   // hats over two bars (32 steps): mostly off-beats, a few extra
        for (int i = 0; i < 32; ++i) if (i % 4 == 2 || (i % 2 == 1 && chance (35)) || (i % 4 == 0 && chance (15))) h |= (1LL << i);
        P.hats32 = h;
        P.kA10 = pick (12, 18); P.kD = 2 * pick (6, 9); P.hiInt = oneOf (std::vector<int> { 14, 15, 17, 19, 22 });
        P.motorRot = pick (0, 2); P.arpMul = oneOf (std::vector<int> { 3, 5, 7 });
        P.drumK = oneOf (std::vector<int> { 3, 5, 7 }); P.drumO = pick (0, 15); P.bellK = oneOf (std::vector<int> { 5, 7, 9 });

        // riffs (the second video's NA / NB / ND), then a second set for the later scenes
        const long long r1 = groove16 (6, 9, true), r2 = groove16 (6, 10, true);
        P.NA = riffDigits (r1, 0, true);
        P.NB = answer (P.NA, pick (8, 12), r1, true);
        P.ND = answer (P.NA, pick (10, 13), r2, true);
        P.NA2 = riffDigits (r2, 0, true);
        P.NB2 = answer (P.NA2, pick (8, 12), r2, true);
        P.ND2 = answer (P.NA2, 12, r1, true);
        // arps: 0 rest, 1 = root, 2 = octave, 3 = 7, 4 = 10, 5 = 5, 6 = 3 (semitones)
        auto arp = [this] ()
        {
            std::string s;
            for (int i = 0; i < 16; ++i)
            {
                if (i % 2 == 1 && ! chance (25)) { s += '0'; continue; }
                s += (char) ('0' + (major() ? oneOf (std::vector<int> { 1, 2, 2, 3, 3, 5 }) : oneOf (std::vector<int> { 1, 2, 2, 3, 3, 4, 5, 6 })));
            }
            s[0] = '2';
            return s;
        };
        P.WA = arp(); P.WB = arp(); P.WD = arp();
        // the acid line's notes (the red video's H(v)): 8 steps of 0..4
        auto acidLine = [this] ()
        {
            std::string s;
            for (int i = 0; i < 8; ++i) s += (char) ('0' + (chance (25) ? 0 : pick (1, 4)));
            if (s[0] == '0') s[0] = '2';
            return s;
        };
        P.H1 = acidLine(); P.H2 = acidLine();
        // the cosine melody's weights (the blue video: 12, 12, 7, 5, 3)
        static const std::vector<std::vector<int>> cs { { 12, 12, 7, 5, 3 }, { 12, 7, 7, 5, 3 }, { 24, 12, 7, 5, 3 }, { 12, 12, 5, 7, 3 }, { 12, 7, 5, 5, 3 }, { 12, 12, 7, 3, 5 } };
        P.coef = oneOf (cs);
        for (size_t i = 2; i < P.coef.size(); ++i) if (chance (25)) P.coef[i] = -P.coef[i];
    }

    static std::string R4 (const char* fn, const std::vector<int>& v)   // F(j) = {mod(j, 4) = 0: a, = 1: b, = 2: c, d}
    {
        return std::string (fn) + "(j) = {mod(j, 4) = 0: " + I (v[0]) + ", mod(j, 4) = 1: " + I (v[1]) + ", mod(j, 4) = 2: " + I (v[2]) + ", " + I (v[3]) + "}\n";
    }

    // ================================================================================================ 0: DRIFT (Track 1)
    std::vector<std::string> drift()
    {
        const auto vs = voicings();
        const int third = sc (2);
        std::map<std::string, std::string> m {
            { "KA", num (P.kA10 / 10.0) }, { "KD", I (P.kD) }, { "HI", I (sc (7 + oneOf (std::vector<int> { 1, 2, 3, 4, 6 }))) }, { "A2", I (oneOf (std::vector<int> { 2, 3, 3, 5 })) },
            { "I3", I (oneOf (std::vector<int> { sc (4), sc (4), third, sc (6) })) },
            { "N0", vs[0] }, { "N1", vs[1] }, { "N2", vs[2] }, { "N3", vs[3] }, { "N4", vs[4] }, { "N5", vs[5] },
            { "H2", I (oneOf (std::vector<int> { sc (4), sc (4), third, 12 })) },
        };
        const int c0 = 7 + pick (0, 2);
        m["C0"] = I (sc (c0)); m["C1"] = I (sc (c0 + 1));   // two neighbouring scale notes rubbing, like the video
        static const std::vector<std::vector<int>> motors { { 0, 3, 6 }, { 0, 2, 5 }, { 0, 3, 5 } };
        const auto& mo = motors[(size_t) P.motorRot];
        m["M0"] = I (mo[0]); m["M1"] = I (mo[1]); m["M2"] = I (mo[2]);
        static const std::vector<std::vector<int>> lens { { -7, -3, 1, 5 }, { -7, -3, 2, 4 }, { -7, 0, 4, 9 }, { -14, -7, 2, 6 } };   // scale steps
        const auto& L = oneOf (lens);
        m["L0"] = I (sc (L[0])); m["L1"] = I (sc (L[1])); m["L2"] = I (sc (L[2])); m["L3"] = I (sc (L[3]));
        m["P5"] = I (sc (oneOf (std::vector<int> { -3, -4, -5, 2 })));

        std::vector<std::string> s;
        s.push_back (fill (
            "y = [kick, -1, -1 + @A2@w^2/12, @I3@/12 - w/12, high]\n"
            "kick = {h < 0.3: @KA@e^(-@KD@h) - 2.1}\n"
            "high = {w > 0.5: @HI@/12 - (w - 0.5)/6}\n"
            "h = frac(x/2)\n"
            "w = frac(x/8)\n", m));
        s.push_back (fill (
            "y = [kick, V(@N0@), V(@N1@), V(@N2@), V(@N3@), V(@N4@), V(@N5@)]\n"
            "kick = {q < 0.3: 1.2e^(-14q) - 2, {q > 0.875 and frac(8q) < 0.6: 0.9e^(-5frac(8q)) - 2}}\n"
            "V(N) = {q < 0.93: (mod(floor(N/100^(3 - c)), 100) - 30 - 2clamp(1 - q/0.08, 0, 1))/12}\n"
            "q = frac(x/4)\n"
            "c = floor(x/4) + 2\n", m));
        s.push_back (fill (
            "y = [kick, motor, H(0), H(@H2@)]\n"
            "kick = {frac(x/4) < 0.5: 1.5e^(-9frac(x/4)) - 2}\n"
            "motor = {frac(2x) < 0.55 + 0.3a: -1 + a/12}\n"
            "a = {m = @M0@ or m = @M1@ or m = @M2@}\n"
            "H(i) = (i + R(c) + (R(c + 1) - R(c))clamp((q - 0.85)/0.15, 0, 1))/12 - 1\n"
            + R4 ("R", progSemi) +
            "m = mod(floor(2x) + 16, 8)\n"
            "q = frac(x/4)\n"
            "c = floor(x/4) + 2\n", m));
        s.push_back (fill (
            "y = [kick, H(0), H(7), H(12), C(@C0@), C(@C1@)]\n"
            "kick = {w < 0.12: 1.4e^(-30w) - 2}\n"
            "H(i) = {w < 0.55: i/12 - 1 - (w/0.55)^2}\n"
            "C(i) = {w > 0.5 and w < 0.97: (i + 2(w - 0.5))/12}\n"
            "w = frac(x/8)\n", m));
        s.push_back (fill (
            "y = [kick, tap, -1, @P5@/12, tune, tune - 1]\n"
            "kick = {m = 0 or m = 6: 1.2e^(-9f) - 2}\n"
            "tap = {(m = 3 or m = 4 or m = 7) and f < 0.5: 0.5e^(-10f) - 1.1}\n"
            "tune = (R(c) + (R(c + 1) - R(c))clamp((q - 0.8)/0.2, 0, 1) - 0.5clamp(1 - q/0.03, 0, 1)(c = 0))/12 + 1\n"
            + R4 ("R", progSemi) +
            "m = mod(floor(2x) + 16, 8)\n"
            "f = frac(2x)\n"
            "q = frac(x/4)\n"
            "c = floor(x/4) + 2\n", m));
        s.push_back (fill (
            "y = [V(@L0@), V(@L1@), V(@L2@), V(@L3@), sub]\n"
            "V(i) = i s/12\n"
            "sub = {s > 0.5: -2}\n"
            "s = (sin(pi frac(x/8)))^2\n", m));
        return s;
    }

    // ================================================================================================ 1: RIFF HOUSE (Track 2)
    std::vector<std::string> riffHouse()
    {
        const int sa = std::clamp (modeA - 1, 0, 6);   // the riff's scale: floor((12i + sa)/7)
        std::map<std::string, std::string> m { { "SA", I (sa) }, { "NA", P.NA }, { "NB", P.NB }, { "ND", P.ND },
            { "NA2", P.NA2 }, { "NB2", P.NB2 }, { "ND2", P.ND2 }, { "WA", P.WA }, { "WB", P.WB }, { "WD", P.WD } };
        const std::string common =
            "g = v > 0 and (f < 0.7 or (k = 14 and mod(c, 2) = 1))\n"
            "d = mod(v, 5) - 1\n"
            "o = v > 4\n"
            "S(i) = floor((12i + @SA@)/7)\n"
            "v = mod(floor(N/10^(15 - k)), 10)\n"
            "N = {c = 0: NA, c = 1: NB, c = 2: NC, ND}\n";
        const std::string clock = "k = mod(n, 16)\nc = floor(n/16)\nn = floor(4x) + 32\nf = frac(4x)\nb = frac(x)\n";
        const std::string drums =
            "kick = {b < 1/3: 2.5e^(-20b) - 1.75}\n"
            "riff = {g: (S(d) + 12o)/12 - 2 + 0.2e^(-9f)}\n"
            "clap = {mod(k, 8) = 4 and f < 0.75: 1.3 - f}\n";
        const std::string arpW =
            "arp = {w > 0 and f < 0.7: {w = 3: 7, w = 4: 10, w = 5: 5, w = 6: 3, 12w - 12}/12}\n"
            "w = mod(floor({c = 0: WA, c = 1: WB, c = 2: WC, WD}/10^(15 - k)), 10)\n"
            "WC = WA\n";
        const std::string notes1 = "NA = @NA@\nNB = @NB@\nNC = NA\nND = @ND@\n";
        const std::string notes2 = "NA = @NA2@\nNB = @NB2@\nNC = NA\nND = @ND2@\n";
        std::vector<std::string> s;
        s.push_back (fill ("y = outline(kick, riff, clap)\n" + drums + common + notes1 + clock, m));
        s.push_back (fill ("y = outline(kick, riff, clap, lead, fifth)\n"
            "fifth = {g and (c < 3 or k < 9): (S(d + 4) + 12o)/12 + a2 + 0.1e^(-9f)}\n"
            "lead = {c = 3 and k > 9: {f < 0.7: {mod(k - 10, 3) = 0: -1, mod(k - 10, 3) = 1: -5/12, 0} + 0.1e^(-9f)}, {g and (c < 3 or k != 9): (S(d + 2) + 12o)/12 + a + 0.1e^(-9f)}}\n"
            "a = -1\na2 = a\n" + drums + common + notes1 + clock, m));
        s.push_back (fill ("y = outline(kick, riff, clap, hats, riser, lead, fifth, pad)\n"
            "hats = {c > 1 and f < {mod(k, 4) = 2: 0.5, 0.25}: 1.7 - 0.25(mod(k, 4) != 2) - f}\n"
            "riser = 3clamp((x + 4)/12, 0, 1) - 1\n"
            "pad = S({c < 2: 0, c = 2: 1, 4})/12\n"
            "lead = {g: (S(d + 2) + 12o)/12 + a + 0.1e^(-9f)}\n"
            "fifth = {g: (S(d + 4) + 12o)/12 + a2 + 0.1e^(-9f)}\n"
            "a = -1\na2 = a\n" + drums + common + notes1 + clock, m));
        s.push_back (fill ("y = outline(kick, riff, clap, hats, arp, lead, fifth, pad)\n"
            "pad = S({c < 2: 7, c = 2: 8, 4})/12\n"
            "WA = @WA@\nWB = @WB@\nWD = @WD@\n"
            "a = 0\na2 = -1\n"
            "hats = {f < {mod(k, 4) = 2: 0.5, 0.25}: 1.7 - 0.25(mod(k, 4) != 2) - f}\n"
            "lead = {g: (S(d + 2) + 12o)/12 + a + 0.1e^(-9f)}\n"
            "fifth = {g: (S(d + 4) + 12o)/12 + a2 + 0.1e^(-9f)}\n" + drums + arpW + common + notes2 + clock, m));
        s.push_back (fill ("y = outline(kick, riff, clap, ohat, arp, fall, lead)\n"
            "fall = 1 - 3clamp((x + 4)/12, 0, 1)\n"
            "WA = @WA@\nWB = @WB@\nWD = @WD@\n"
            "lead = {g and c < 2: (S(d + 2) + 12o)/12 + a + 0.1e^(-9f)}\n"
            "ohat = {mod(k, 4) = 2 and f < 0.5: 1.7 - f}\n"
            "a = -1\n" + drums + arpW + common + notes1 + clock, m));
        return s;
    }

    // ================================================================================================ 2: BREAKS (Track 3)
    std::vector<std::string> breaks()
    {
        const int pentB = oneOf (std::vector<int> { 3, 3, 0 });          // floor((12d + 3)/5) minor pentatonic, + 0 major
        std::map<std::string, std::string> m { { "PB", I (pentB) }, { "DK", I (P.drumK) }, { "DO", I (P.drumO) },
            { "BK", I (P.bellK) }, { "AM", I (P.arpMul) }, { "RU", I (oneOf (std::vector<int> { 3, 2, 5 })) }, { "MA", I (std::clamp (modeA - 1, 0, 6)) },
            { "B2", I (oneOf (std::vector<int> { 2, 3, 4 })) }, { "SUB2", I (oneOf (std::vector<int> { 2, 3, 5 })) },
            { "TK", I (oneOf (std::vector<int> { 5, 3, 7 })) }, { "TO", I (pick (0, 15)) } };
        std::vector<std::string> s;
        s.push_back (fill ("y = outline(boom, snare, hats)\n"
            "boom = (@RU@mod(floor(x/4), 2) - 2(x >= 0) - 6(x >= 13/2) - 24)/12 + 2.5e^(-18h)\n"
            "h = min(mod(x, 4), mod(x - 5/2, 4))\n"
            "snare = {mod(x - 2, 4) < 3/16: 1.3 - 4mod(x - 2, 4)}\n"
            "hats = {frac(R x) < 0.5: 1.2 + 0.4frac(x) - frac(R x)/2}\n"
            "R = {mod(j, 8) = 7: 8, mod(j, 4) = 3: 6, 2 + 2mod(j, 2)}\n"
            "j = floor(x)\n", m));
        s.push_back (fill ("y = outline(kick, drum, clap, hats)\n"
            "kick = {b < 1/3: 2.5e^(-20b) - 1.75}\n"
            "drum = {mod(@DK@n + @DO@, 16) < @DK@ and f < 0.95: (floor((12d + @PB@)/5) - 21)/12 + 0.3e^(-4f)}\n"
            "d = mod(floor((@DK@n + @DO@)/16), 4) + 2mod(floor(x/4), 2)\n"
            "clap = {mod(n, 8) = 4 and f < 0.75: 1.3 - f}\n"
            "hats = {f < 0.35: 1.1 + 0.4(mod(n, 2) = 1) - f}\n"
            "n = floor(4x)\nf = frac(4x)\nb = frac(x)\n", m));
        s.push_back (fill ("y = outline(boom, snare, hats, bell)\n"
            "boom = (-24 - @B2@(mod(x, 8) >= 4))/12 + 2.5e^(-18h)\n"
            "h = min(mod(x, 2), mod(x - 3/4, 4))\n"
            "snare = {mod(x - 1, 2) < 3/16: 1.3 - 4mod(x - 1, 2)}\n"
            "hats = {f < 0.4: 1.2 + 0.3(mod(n, 2) = 0) - f}\n"
            "bell = {mod(@BK@n, 16) < @BK@ and f < 0.8: (floor((12d + @MA@)/7) - 2)/12}\n"
            "d = 7 - mod(floor(@BK@n/16), 8)\n"
            "n = floor(4x)\nf = frac(4x)\n", m));
        s.push_back (fill ("y = outline(kick, acid, clap, hats)\n"
            "kick = {b < 1/3: 2.5e^(-20b) - 1.75}\n"
            "acid = {f < 0.7 or mod(@AM@n, 8) < 3: (floor((12d + @MA@)/7) + 12(mod(5n, 16) < 5) - 24)/12}\n"
            "d = mod(@AM@n, 8)\n"
            "clap = {mod(n, 8) = 4 and f < 0.75: 1.3 - f}\n"
            "hats = {mod(n, 4) = 2 and f < 0.5: 1.7 - f}\n"
            "n = floor(4x)\nf = frac(4x)\nb = frac(x)\n", m));
        s.push_back (fill ("y = outline(kick, sub, clap, hats, stab)\n"
            "kick = {mod(5n + 50, 16) < 5 and f < 0.9: 2.5e^(-5f) - 1.75}\n"
            "sub = (-24 - @SUB2@(mod(x, 8) >= 4))/12\n"
            "clap = {mod(n, 8) = 4 and f < 0.75: 1.3 - f}\n"
            "hats = {f < 0.3: 1.2 + 0.3(mod(n, 2) = 0) - f}\n"
            "stab = {mod(3n, 8) < 3 and f < 0.6: (7mod(m, 2)(p = 1) + floor((12mod(m, 4) + 13)/7)(p = 2))/12}\n"
            "p = mod(floor(3n/8), 3)\nm = floor(n/8)\n"
            "n = floor(4x)\nf = frac(4x)\n", m));
        s.push_back (fill ("y = outline(kick, tom, rim, hats, drone)\n"
            "kick = {mod(mod(n, 16), 6) = 0 and f < 0.9: 2.5e^(-5f) - 1.75}\n"
            "tom = {mod(@TK@n + @TO@, 16) < @TK@ and f < 0.8: (5mod(n, 2) - 14)/12 + 0.5e^(-8f)}\n"
            "rim = {mod(n, 8) = 4 and f < 0.3: 1.1 - f}\n"
            "hats = {f < 0.3: 1 + 0.4(mod(n, 4) = 2) - f}\n"
            "drone = ((B > 0) + 6(B = 2) - 12)/12\n"
            "B = floor(x/4) + 2\n"
            "n = floor(4x)\nf = frac(4x)\n", m));
        return s;
    }

    // ================================================================================================ 3: COSINE SONG (Track 4)
    std::vector<std::string> cosineSong()
    {
        const auto& c = P.coef;
        auto term = [] (int w, const char* arg, bool first) { return std::string (first ? "" : (w < 0 ? " - " : " + ")) + I (first ? w : std::abs (w)) + "B(" + arg + ")"; };
        const std::string K = term (c[0], "x/8", true) + term (c[1], "x/4", false) + term (c[2], "x/2", false) + term (c[3], "x", false) + term (c[4], "2x", false) + " + 15";
        // the chord roots as 4 digits (scale steps 0..6 from the key): the blue video's 534 / 5624
        std::string Rd;
        for (int d : progDeg) Rd += (char) ('0' + ((d % 7) + 7) % 7);
        while (Rd.size() > 1 && Rd[0] == '0') Rd.erase (0, 1);
        std::map<std::string, std::string> m { { "K", K }, { "RD", Rd }, { "HA", I (modeA) },
            { "BM1", I (P.bassMask) }, { "BM2", I (groove16 (7, 10, true)) }, { "HM", I (P.hats32) }, { "HM2", I (P.hats32 ^ (1LL << 30)) },
            { "V1", I (oneOf (std::vector<int> { 7, 7, 5, 12 })) } };
        const std::string clock = "B(u) = clamp(144cos(pi u), -1, 1)\nn = floor(4x)\nk = mod(n, 16)\nf = frac(4x)\nj = floor(x)\nb = frac(x)\n";
        const std::string hats = "hats = {f < 0.25 - 0.07(mod(k, 4) != 2) and mod(floor(@HM@/2^mod(n, 32)), 2) = 1: 2.4 - 2.5f - 0.25(mod(k, 4) != 2)}\n";
        const std::string clap = "clap = {mod(j, 2) = 1 and b < 1/10: 2.3 - 6b}\n";
        const std::string r = "r = 5(1 - B(x/2))/2\n";
        const std::string tw = "arp = h(T)/12 - 1\nharm = h(T - 2)/12 - 1\nT = W + mod(R + 3 + B(2x) - 2B(x), 7)\nW = 7(2 + B(x/4) + B(x/8))/2\n"
                               "h(d) = floor((12d + @HA@)/7) + (q = 3)(mod(d, 7) = 6)\nq = floor((x + 8)/4)\n";
        std::vector<std::string> s;
        s.push_back (fill ("y = (@K@ - 12)/24\nB(u) = clamp(18cos(pi u), -1, 1)\n", m));
        s.push_back (fill ("y = outline(kick, bass, arp)\n"
            "kick = {b < 1/3: -2 + 2.5e^(-20b)}\n"
            "bass = {mod(floor(@BM1@/2^k), 2) = 1 and f < 0.8: (r - 24 + v)/12}\n"
            "v = @V1@(p = 1) + (12 - 2mod(j, 2))(p = 3)\np = mod(n, 4)\n"
            "arp = K/24\nK = @K@\n" + r + clock, m));
        s.push_back (fill ("y = outline(kick, bass, clap, hats, arp, harm)\n"
            "kick = {b < 1/3: -2 + 2.5e^(-20b)}\n"
            "bass = {mod(floor(@BM2@/2^k), 2) = 1 and f < 0.8: (r - 24 + v)/12}\n"
            "v = @V1@(p = 1) + (12 - 2mod(j, 2))(p = 3)\np = mod(n, 4)\n" + clap + hats +
            "arp = K/24\nharm = (K/2 - 3 - 2(c = 0) - (c = 7))/12\nc = mod(K/2 - r, 12)\nK = @K@\n" + r + clock, m));
        s.push_back (fill ("y = outline(kick, bass, clap, hats, arp, harm)\n"
            "kick = {b < 1/3: -2 + 2.5e^(-20b)}\n"
            "bass = {mod(floor(@BM1@/2^k), 2) = 1 and f < 0.8: h(R + v)/12 - 2}\n"
            "v = 4(p = 1) + (7 - mod(j, 2))(p = 3)\np = mod(n, 4)\n" + clap + hats + tw +
            "R = mod(floor(@RD@/10^(3 - q)), 10)\n" + clock, m));
        s.push_back (fill ("y = outline(kick, bass, clap, hats, arp, harm)\n"
            "kick = {b < 1/2: h(R)/12 - 2 + 3e^(-30b)}\n"
            "bass = {mod(floor(@BM2@/2^k), 2) = 1 and f < 0.8: h(R + v)/12 - 2}\n"
            "v = 4(p = 1) + (7 - mod(j, 2))(p = 3)\np = mod(n, 4)\n" + clap +
            "hats = {f < 0.25 - 0.07(mod(k, 4) != 2) and mod(floor(@HM2@/2^mod(n, 32)), 2) = 1: 2.4 - 2.5f - 0.25(mod(k, 4) != 2)}\n" + tw +
            "R = mod(floor(@RD@/10^(3 - q)), 10)\n" + clock, m));
        s.push_back (fill ("y = outline(kick, bass, clap, hats, arp)\n"
            "kick = {x < 4 and b < 1/3: -2 + 2.5e^(-20b)}\n"
            "bass = {x < 4 and mod(floor(@BM1@/2^k), 2) = 1 and f < 0.8: (r - 24 + v)/12}\n"
            "v = @V1@(p = 1) + (12 - 2mod(j, 2))(p = 3)\np = mod(n, 4)\n"
            "clap = {x < 4 and mod(j, 2) = 1 and b < 1/10: 2.3 - 6b}\n"
            "hats = {x < 4 and f < 0.25 - 0.07(mod(k, 4) != 2) and mod(floor(@HM@/2^mod(n, 32)), 2) = 1: 2.4 - 2.5f - 0.25(mod(k, 4) != 2)}\n"
            "arp = {x < 4: K/24, x < 6 or frac(8x) < 0.6 - 0.1floor(x - 4): K/24 + 1 - 1.2max(0, x - 7)^2}\n"
            "K = @K@\n" + r + clock, m));
        return s;
    }

    // ================================================================================================ 4: ACID (Track 5)
    std::vector<std::string> acid()
    {
        // G's chord roots as 2-digit numbers (+12): the red video's 12120810
        std::string R;
        for (int i = 0; i < 4; ++i) { int v = progSemi[(size_t) i]; while (v > 5) v -= 12; while (v < -9) v += 12; char b[8]; std::snprintf (b, sizeof b, "%02d", v + 12); R += b; }
        while (R.size() > 1 && R[0] == '0') R.erase (0, 1);
        // chord colours per bar (the red video's q = D(3322), p = D(5545))
        std::string Q, Pp;
        for (int i = 0; i < 4; ++i)
        {
            const int d = progDeg[(size_t) i];
            Q  += (char) ('0' + std::clamp (scaleSemi (d + 2) - scaleSemi (d), 0, 9));
            Pp += (char) ('0' + std::clamp (scaleSemi (d + 4) - scaleSemi (d), 0, 9));
        }
        const std::string acidCore = "(h(n(s)) + 12O + lift + z(h(n(s + 1)) - h(n(s)) - 12O + tgt)clamp(2f - 1, 0, 1))/12 + 0.3(mod(s, 3) = 0)e^(-14f)";
        std::map<std::string, std::string> m { { "R", R }, { "Q", Q }, { "P", Pp }, { "HM", I (P.hatMask) }, { "HM2", I (groove16 (10, 13, false)) },
            { "AM", I (P.acidMask) }, { "H1", I (std::stoll (P.H1)) }, { "H2", I (std::stoll (P.H2)) }, { "CORE", acidCore },
            { "PB", I (oneOf (std::vector<int> { 3, 3, 0 })) } };
        const std::string clock = "D(N) = mod(floor(N/10^(3 - b)), 10)\na = q(j = 1) + p(j = 2) + 7(j = 3)\nj = mod(floor(3s/8), 4)\nk = mod(s, 16)\ns = floor(4x)\nf = frac(4x)\nb = floor((x + 8)/4)\n";
        const std::string kick = "kick = {frac(x) < 1/4: -2 + 3e^(-20frac(x))}\n";
        const std::string G = "G = {frac(3s/8) < 3/8: (R + a)/12 - 1 + 0.08e^(-12f), {f < 0.35: R/12 - 1}}\n";
        const std::string parts = "z = k = 3 or k = 13\nh(d) = floor((12d + @PB@)/5)\nn(v) = (u(v) > 0)(mod(u(v) - 1 + w(v), 4) + 1)\n"
                                  "u(v) = mod(floor(H(v)/10^(7 - mod(v, 8))), 10)\nH(v) = {mod(floor(v/8), 2) = 0: @H1@, @H2@}\n";
        const std::string notes = "R = mod(floor(@R@/100^(3 - b)), 100) - 12\nq = D(@Q@)\np = D(@P@)\n";
        auto hats = [] (const char* mask) { return std::string ("hats = {f < 0.25 - 0.07(mod(k, 4) != 2) and (mod(floor(") + mask + "/2^k), 2) = 1 or (b = 3 and k >= 12)): 2.25 - 2f - 0.3(mod(k, 4) != 2)}\n"; };
        std::vector<std::string> s;
        s.push_back (fill ("y = [kick, G, G + 7/12]\n" + kick + G + notes + clock, m));
        s.push_back (fill ("y = [kick, G, G + 7/12, hats]\n" + kick + G + hats ("@HM@") + notes + clock, m));
        s.push_back (fill ("y = [kick, G, G + 7/12, hats, acid]\n" + kick + G + hats ("@HM@")
            + "acid = {mod(floor(@AM@/2^k), 2) = 1 and (f < 0.8 or z = 1): @CORE@}\n" + parts
            + "w(v) = 0\nO = k = 6\nlift = 2(k = 10)(mod(b, 2) = 1) + 12(k = 0)(b = 1)\ntgt = 4(k = 13)(b >= 2)\n" + notes + clock, m));
        s.push_back (fill ("y = outline(kick, hats, acid, bass)\n" + kick + hats ("@HM2@")
            + "acid = {b = 3 and k >= 12 and f < 0.8: (10 + 2(k - 12) + (k >= 14))/12 + 0.3(mod(s, 3) = 0)e^(-14f), "
              "mod(floor(@AM@/2^k), 2) = 1 and (f < 0.8 or z = 1): @CORE@}\n" + parts
            + "w(v) = 3mod(floor(v/8), 4)\nO = k = 6 or k = 14\nlift = 0\ntgt = 0\n"
              "bass = {b = 3 and k >= 12: {f < 0.8: (2(k - 12) + (k >= 14) - 2)/12 - 1 + 0.25(mod(s, 3) = 0)e^(-9f)}, "
              "(R + 12(r5 = 2) - 2(r5 = 4)(1 - e^(-2f)))/12 - 1 + 0.25(mod(s, 3) = 0)e^(-9f)}\nr5 = mod(s + 2, 5)\n" + notes + clock, m));
        s.push_back (fill ("y = [kick, G, G + 7/12, hats, acid, bass]\n" + kick
            + "G = {b >= 2 and frac(3s/8) < 3/8: (R + a)/12 - 1 + 0.08e^(-12f), {b >= 2 and f < 0.35: R/12 - 1}}\n"
              "hats = {b < 3 and f < 0.25 - 0.07(mod(k, 4) != 2) and mod(floor(@HM@/2^k), 2) = 1: 2.25 - 2f - 0.3(mod(k, 4) != 2)}\n"
              "acid = {b = 3 and k >= 12 and f < 0.8: (7 - 2(k - 12) + (k = 15))/12 + 0.3(mod(s, 3) = 0)e^(-14f), "
              "mod(floor(@AM@/2^k), 2) = 1 and (b < 3 or k <= 10) and (f < 0.8 or z = 1): @CORE@}\n" + parts
            + "w(v) = 0\nO = k = 6\nlift = 2(k = 10)(mod(b, 2) = 1) + 4(k = 14)(b = 2)\ntgt = 4(k = 13)(b = 2) + (k = 3)(b = 2)\n"
              "bass = {b = 3 and k >= 12: {f < 0.8: (7 - 2(k - 12) + (k = 15))/12 - 1 + 0.25(mod(s, 3) = 0)e^(-9f)}, "
              "(R + 12(R < -3) + 12(r5 = 2) - 2(r5 = 4)(1 - e^(-2f)))/12 - 1 + 0.25(mod(s, 3) = 0)e^(-9f)}\nr5 = mod(s + 2, 5)\n" + notes + clock, m));
        return s;
    }
};

} // namespace gg
