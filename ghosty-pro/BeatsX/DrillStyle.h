#pragma once
// Ghosty Beats: the DRILL style (GENERATE DRILL and the DRILL kit's starter beats). Original archetypes only (see
// docs/research/TRAP-HIPHOP-STYLE.md, addendum 2 section A): a 2-bar drum cycle at 138-146 BPM read in half time.
//   SNARE: beat 3 in bar 1; in bar 2 the backbeat is DISPLACED to beat 4 or a 16th early, with a counter-snare on the
//          "and of 4" (or the "a of 4"). This 2-bar asymmetry is the defining rule.
//   KICK:  sparse (DD rows, 1-4 per bar, off-16ths) or 808-as-kick (the kick only marks the loop and one 808 hit).
//   HATS:  3-3-2 / 3-2-3 / 2-3-3 cells with 2-3 holes per 2 bars and at most 2 rolls per 2 bars; swing on the hats only (0-8 %).
//   808:   one of 12 sliding patterns (DB1-DB12): 2-6 slides per 2 bars, at most 3 per bar, MONO notes with a HOLD length.
//   GRIT:  0-100 (the beat's DRILL GRIT; 0 = exactly the DRILL of installer #27). Higher = the tough, dirty NYC lane: the 808
//          tuned F#1-G#1 and kept low (upper notes an octave down), 4-7 slides per 2 bars with octave dives and half-step falls,
//          longer tails (notes ride over the rests), the DIRTY 808 / snare (the processor swaps the DRILL pack sounds at 50+),
//          the snare + clap layer 10-20 ms late, sparser kicks (808-as-kick more often), hats softer with more holes.
// Included from GenreKits.h (after BeatModel.h): include BeatModel.h, never this header directly.
#include "BeatModel.h"

namespace gb::drill {

constexpr int kStyle = 2;   // GENERATE style index: 0 INTRICATE, 1 MACHINE GUN, 2 DRILL
constexpr int kLowest = -14, kHighest = 12;   // 808 register: semitones on top of the track pitch (the DRILL 808s sit on G1)
inline uint32_t mix(uint32_t x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; }
inline int tempoFor(int seed) { return 138 + (int) (mix((uint32_t) seed * 2654435761u + 77u) % 9u); }   // 138..146 (felt 69-73, half time)

// what one generation rotates (consecutive seeds always change all three)
inline int bassFamily(int seed) { return (int) (((int64_t) seed * 5 % 12 + 12) % 12); }    // DB1..DB12
inline int snareSpot(int seed) { return ((seed % 3) + 3) % 3; }                             // 0 beat 4 + "and of 4", 1 a 16th early + "and of 4", 2 beat 4 + "a of 4"
inline int hatCell(int seed) { const int s = juce::jmax(0, seed); return (s + s / 3) % 3; } // 3-3-2, 3-2-3, 2-3-3

// ---------------- the 808 archetypes (2 bars, 16th grid; "T:" = that bar on the 1/16-triplet grid, 12 steps)
// X attack   - hold   . rest   / \ hold, and the next X SLIDES into its note (direction comes from the pitches)
// pitches: semitones from the root, one per attack (99 ends the list): 1 = 0, b2 = 1, b3 = 3, 4 = 5, 5 = 7, b6 = 8, b7 = 10, 8 = 12, L = -12
struct Bass { const char* name; const char* bar1; const char* bar2; std::array<int, 8> p1, p2; };
inline const std::array<Bass, 12>& bassPatterns() {
    static const std::array<Bass, 12> b { {
        { "DB1 five-up stair",       "X--X--/X---...X-", "X--...X/X---\\X-.", { 0, 0, 7, 10, 99 },     { 0, 8, 7, 0, 99 } },
        { "DB2 octave dive",         "X-----.X-/X-....", "X-..X---------\\X", { 0, 0, 12, 99 },        { 3, 0, -12, 99 } },
        { "DB3 half-step cry",       "X--...X\\X---....", "X--...X\\X--...X.", { 0, 8, 7, 99 },         { 0, 8, 7, -11, 99 } },
        { "DB4 push off the kick",   "...X-/X-...X-\\X-", "...X--/X---X--..", { 0, 7, 3, 0, 99 },      { 0, 7, 5, 99 } },
        { "DB5 minor-3rd ping",      "X-/X--..X-/X--..", "X---X/X-..X-....", { 0, 3, 0, 3, 99 },      { 0, 7, 12, 10, 99 } },
        { "DB6 triplet bar",         "X-----.X----....", "T:X/X-X/X-\\X-.",   { 0, 10, 99 },           { 0, 7, 7, 12, 10, 99 } },
        { "DB7 long ride, late turn","X-------------X/", "X--------.X-\\X..", { 0, 0, 99 },            { 7, 8, 7, 99 } },
        { "DB8 answer the snare",    "X-.......X/X--..", "X-.........X/X\\X", { 0, 3, 5, 99 },         { 0, 0, 7, 3, 99 } },
        { "DB9 phrygian walk",       "X-/X-\\X-----X---", "X-/X--X-----....", { 0, 1, 0, 10, 99 },     { 0, 1, 0, 99 } },
        { "DB10 stab and swoop",     "X..X..X....X/X--", "X..X..X.....X-\\X", { 0, 0, 0, 7, 10, 99 },  { 0, 0, 0, 12, 0, 99 } },
        { "DB11 fifth seesaw",       "X-/X-\\X-..X/X--.", "X-/X-\\X-....X---", { 0, 7, 0, 10, 12, 99 }, { 0, 7, 0, 8, 99 } },
        { "DB12 no-kick ride",       "X-----X----X-/X-", "X----\\X-X--.....", { 0, 0, 3, 7, 99 },      { 8, 7, 0, 99 } },
    } };
    return b;
}
// ---------------- kick rows from the drum archetypes DD1-DD7 (32 steps; DD5 = the 808-as-kick row, DD6 is the fill bar)
struct KickRow { const char* bar1; const char* bar2; };
inline const std::array<KickRow, 6>& kickRows() {
    static const std::array<KickRow, 6> k { {
        { "X.........X.....", "...X..........X." },   // DD1 cold two-step
        { "X.....X........X", "X.........X....." },   // DD2 early snare, late clap
        { "X.............X.", "...X..X........." },   // DD3 and-of-4 counter
        { "X......X........", "X.........X....." },   // DD4 triplet hat bar
        { "X..X......X..X..", "X.....X.......X." },   // DD7 bounce
        { "X...............", "................" },   // DD5 bare (808-as-kick)
    } };
    return k;
}

// one 808 note as the generator lays it down
struct Note { double pos = 0; int pitch = 0; bool slide = false; double end = 32; };

// parse an archetype into notes (positions in 16th steps 0..32, triplet bars on thirds of a beat), in time order.
// A note ends at the first rest after it or at the next attack (the loop end = 32).
inline std::vector<Note> parseBass(const Bass& b, int root) {
    std::vector<Note> out; std::vector<double> rests; bool slideNext = false;
    auto deg = [&](int d) { if (d < 0 && root + d < kLowest) d = d == -12 ? -5 : d + 12; return juce::jlimit(kLowest, kHighest, root + d); };
    for (int bar = 0; bar < 2; ++bar) {
        const char* s = bar == 0 ? b.bar1 : b.bar2; const bool trip = s[0] == 'T' && s[1] == ':'; if (trip) s += 2;
        const auto& P = bar == 0 ? b.p1 : b.p2; int k = 0;
        const int n = trip ? 12 : 16; const double w = trip ? 16.0 / 12.0 : 1.0;
        for (int i = 0; i < n && s[i]; ++i) {
            const char c = s[i]; const double pos = bar * 16 + i * w;
            if (c == 'X') {
                const int d = k < 8 && P[(size_t) k] != 99 ? P[(size_t) k] : 0; ++k;
                Note nt; nt.pos = pos; nt.pitch = deg(d); nt.slide = slideNext && !out.empty(); out.push_back(nt);
            } else if (c == '.') rests.push_back(pos);
            slideNext = c == '/' || c == '\\';
        }
    }
    for (size_t i = 0; i < out.size(); ++i) {
        double e = i + 1 < out.size() ? out[i + 1].pos : 32.0;
        for (double r : rests) if (r > out[i].pos + 1e-9 && r < e) { e = r; break; }
        out[i].end = e;
    }
    return out;
}
inline int barOfPos(double pos) { return pos < 16.0 - 1e-9 ? 0 : 1; }
// 2-6 slides per 2 bars, at most 3 per bar; added slides use the drill intervals (up a 5th, octave up / down, half-step fall)
inline void fixSlides(std::vector<Note>& v, juce::Random& R, int lo = 2, int hi = 6, int perBar = 3) {
    auto count = [&](int bar) { int c = 0; for (auto& n : v) if (n.slide && (bar < 0 || barOfPos(n.pos) == bar)) ++c; return c; };
    for (int bar = 0; bar < 2; ++bar) for (int i = (int) v.size() - 1; i >= 0 && count(bar) > perBar; --i) if (v[(size_t) i].slide && barOfPos(v[(size_t) i].pos) == bar) v[(size_t) i].slide = false;
    for (int i = (int) v.size() - 1; i >= 0 && count(-1) > hi; --i) if (v[(size_t) i].slide) v[(size_t) i].slide = false;
    for (int guard = 0; count(-1) < lo && guard < 16; ++guard) {
        std::vector<int> cand; for (int i = 1; i < (int) v.size(); ++i) if (!v[(size_t) i].slide && count(barOfPos(v[(size_t) i].pos)) < perBar) cand.push_back(i);
        if (cand.empty()) break;
        const int i = cand[(size_t) R.nextInt((int) cand.size())]; auto& n = v[(size_t) i]; const auto& pr = v[(size_t) i - 1];
        n.slide = true;
        if (n.pitch == pr.pitch) { static const int I[] { 7, 12, -12, -1 }; for (int k = 0; k < 4; ++k) { const int c = pr.pitch + I[(R.nextInt(4) + k) % 4]; if (c >= kLowest && c <= kHighest) { n.pitch = c; break; } } }
    }
}
// GRIT on the 808 line (its own dice G; never called at GRIT 0): the register pulled down, more slides (notes split into a
// slide-on note: octave dives, half-step falls, the odd 5th), longer tails. lo..hi slides per 2 bars, perBar at most per bar.
inline void gritBass(std::vector<Note>& v, juce::Random& G, float g, int root) {
    if (v.empty()) return;
    const int lo = 2 + (int) std::lround(2.0f * g), hi = 6 + (g >= 0.5f ? 1 : 0), perBar = 3 + (g >= 0.5f ? 1 : 0);
    const int target = lo + G.nextInt(juce::jmax(1, hi - lo + 1));
    // 1. low and heavy: notes a 5th or more above the root drop an octave (the octave itself stays: it is where the dives start)
    for (size_t i = 1; i < v.size(); ++i) { auto& n = v[i]; const int up = n.pitch - root; if (up >= 5 && up != 12 && n.pitch - 12 >= kLowest && G.nextFloat() < 0.75f * g) n.pitch -= 12; }
    auto count = [&](int bar) { int c = 0; for (auto& n : v) if (n.slide && (bar < 0 || barOfPos(n.pos) == bar)) ++c; return c; };
    auto interval = [&](int from) {   // a dive (an octave down, or from the octave back to the root), a half-step fall, else up a 5th
        const float u = G.nextFloat();
        if (u < 0.4f) { if (from - root >= 12) return root; if (from - 12 >= kLowest) return from - 12; }
        if (u < 0.8f && from - 1 >= kLowest) return from - 1;
        return juce::jmin(kHighest, from + 7);
    };
    // 2. more slides: split a long note (a slide-on note 2-3 steps in, on an off-16th), else turn a MONO attack into a slide
    for (int guard = 0; count(-1) < target && guard < 24; ++guard) {
        // (a slide never retriggers the 808's envelope, so splitting keeps every attack: the line stays as loud as before)
        // (a slide never retriggers the 808's envelope: splitting keeps every attack, so the line stays as loud; turning an attack into
        // a slide would thin it out, so that is never done here)
        std::vector<int> cand; for (int pass = 0; pass < 2 && cand.empty(); ++pass) for (int i = 0; i < (int) v.size(); ++i) { const auto& n = v[(size_t) i];
            if (n.end - n.pos >= (pass == 0 ? 3.0 : 2.0) - 1e-9 && count(barOfPos(n.pos + (pass == 0 ? 2 : 1))) < perBar && std::abs(n.pos - std::round(n.pos)) < 1e-9) cand.push_back(i); }
        if (cand.empty()) break;
        {
            const int i = cand[(size_t) G.nextInt((int) cand.size())]; Note nt = v[(size_t) i]; const double len = v[(size_t) i].end - v[(size_t) i].pos;
            nt.pos = v[(size_t) i].pos + (len < 3.0 - 1e-9 ? 1 : (len >= 4.0 - 1e-9 && G.nextBool() ? 3 : 2)); if (barOfPos(nt.pos) != barOfPos(v[(size_t) i].pos) && count(barOfPos(nt.pos)) >= perBar) continue;
            nt.slide = true; nt.pitch = interval(v[(size_t) i].pitch); nt.end = v[(size_t) i].end; v[(size_t) i].end = nt.pos;
            v.insert(v.begin() + i + 1, nt);
        }
    }
    for (int bar = 0; bar < 2; ++bar) for (int i = (int) v.size() - 1; i >= 0 && count(bar) > perBar; --i) if (v[(size_t) i].slide && barOfPos(v[(size_t) i].pos) == bar) v[(size_t) i].slide = false;
    for (int i = (int) v.size() - 1; i >= 0 && count(-1) > hi; --i) if (v[(size_t) i].slide) v[(size_t) i].slide = false;
    // 3. longer tails: a note rides over the rest after it (up to the next attack)
    for (size_t i = 0; i < v.size(); ++i) { const double next = i + 1 < v.size() ? v[i + 1].pos : 32.0; if (v[i].end < next - 1e-9 && G.nextFloat() < 0.7f * g) v[i].end = juce::jmin(next, v[i].end + 2 + G.nextInt(4)); }
}
inline int countSlides(const Pattern& p, int t, int bar) {   // (tests too) SLIDE steps on track t in bar 0 / 1 (-1 = both)
    int c = 0; for (int s = 0; s < 32; ++s) { const auto& st = p.steps[(size_t) t][(size_t) s]; if (st.vel && st.slide() && (bar < 0 || s / 16 == bar)) ++c; } return c;
}

// ---------------- one DRILL generation on a 2-bar 1/16 pattern (locked tracks are left alone; the caller restores locked steps)
inline void generate(Pattern& p, const std::array<uint8_t, kTracks>& roles, const std::array<bool, kTracks>& lockedTrack, int seed, int grit = 0) {
    juce::Random R((juce::int64) seed * 69069 + 12345);
    const bool gr = grit > 0; const float g = juce::jlimit(0, 100, grit) / 100.0f;
    juce::Random G((juce::int64) seed * 7919 + 4242);   // GRIT's own dice (GRIT 0 never rolls them, so R runs exactly as in installer #27)
    const int L = 32; p.length = L; p.res = 1; p.evolveStyle = 0; p.evolve = 0.0f; p.fillEvery = R.nextFloat() < 0.6f ? 4 : 8; p.seed = seed;
    const int fam = bassFamily(seed), spot = snareSpot(seed), cell = hatCell(seed);
    const int swing = R.nextInt(9);   // hats only, 0..8 % of a step on the off-16ths
    bool bassKick = fam == 11 || R.nextFloat() < 0.2f;   // 808-as-kick (always with the no-kick ride)
    static const int roots[] { -6, -5, -2, -1, 0 };   // C#, D, F, F#, G (on the G1 808)
    int root = roots[R.nextInt(5)];
    if (gr) { if (G.nextFloat() < 0.5f * g) bassKick = true;                       // GRIT: the 808 is the kick more often
              if (G.nextFloat() < 0.3f + 0.7f * g) root = G.nextInt(3) - 1; }      // ... tuned F#1-G#1
    auto notes = parseBass(bassPatterns()[(size_t) fam], root); fixSlides(notes, R);
    if (gr) gritBass(notes, G, g, root);
    std::vector<int> bassSteps; for (auto& n : notes) bassSteps.push_back((int) std::lround(n.pos));
    // the snare grammar
    const int s1 = 8, s2 = spot == 1 ? 27 : 28, counter = spot == 2 ? 31 : 30;
    // GRIT: the snare (and the clap layered on it) sits 10-20 ms late; nudge is in % of a 16th at this style's tempo
    const int late = gr ? juce::jlimit(0, 50, (int) std::lround((10.0 + 10.0 * g) / (15000.0 / tempoFor(seed)) * 100.0)) : 0;
    // gain staging inside the beat (the peaks stay under the bus ceiling, so the limiter rests and the true peak stays clean):
    // an 808 attack under a main snare or the downbeat kick is a little softer, the hat roll and the downbeat perc sit back
    bool kickDone = false, snareDone = false, clapDone = false, hatDone = false, openDone = false, bassDone = false, percDone = false;
    for (int t = 0; t < kTracks; ++t) {
        if (lockedTrack[(size_t) t]) continue;
        auto& row = p.steps[(size_t) t]; for (auto& st : row) st = Step(); p.trackLen[(size_t) t] = 0;
        const int role = roles[(size_t) t];
        auto hit = [&](int s, int v) -> Step& { auto& st = row[(size_t) (((s % L) + L) % L)]; st = Step(); st.vel = (uint8_t) juce::jlimit(1, 127, v); return st; };
        if (role == Role808 && !bassDone) {
            bassDone = true;
            for (size_t i = 0; i < notes.size(); ++i) {
                const auto& n = notes[i]; const int s = (int) std::lround(n.pos);
                auto& st = hit(s, (s == 0 ? 104 : (s == s1 || s == s2 ? 96 : 110)) + (gr && !n.slide && s != s1 && s != s2 ? (int) std::lround(6 * g) : 0));   // (GRIT: the attacks dig in a little harder)
                st.pitch = (int8_t) n.pitch; st.nudge = (int8_t) juce::jlimit(-50, 50, (int) std::lround((n.pos - s) * 100));
                st.flags = (uint8_t) (n.slide ? 16 : 32);   // SLIDE into this note, or MONO (cut the ringing one)
                if (n.slide) { const auto& pr = notes[i - 1]; const int iv = std::abs(n.pitch - pr.pitch); const double gap = n.pos - pr.pos;
                               st.glideT = (uint8_t) (iv >= 12 && n.pos >= 24 ? 30 : (gap <= 2.5 ? 9 : 14)); }   // dive 300 ms at a phrase end, quick 90 ms, else 140 ms
                // HOLD: to the first rest / the next note; a note that slides on is held over the slide target by one step (overlap)
                double end = n.end; if (i + 1 < notes.size() && notes[i + 1].slide) end = notes[i + 1].pos + 1.0;
                st.hold = (uint8_t) juce::jlimit(1, kMaxSteps, (int) std::ceil(end - n.pos - 1e-6));
            }
        } else if (role == RoleKick && !kickDone) {
            kickDone = true;
            if (bassKick) { hit(0, 106); const int pick = bassSteps.size() > 1 ? bassSteps[(size_t) (1 + R.nextInt((int) bassSteps.size() - 1))] : 16; if (pick != s1 && pick != s2) hit(pick, 100); }
            else {
                const auto& kr = kickRows()[(size_t) ((juce::jmax(0, seed) / 2 + fam) % 5)];
                for (int s = 0; s < L; ++s) if ((s < 16 ? kr.bar1[s] : kr.bar2[s - 16]) == 'X') {
                    const bool on808 = std::find(bassSteps.begin(), bassSteps.end(), s) != bassSteps.end();
                    if (on808 && s != 0 && s != 16) continue;   // the 808 is the kick there
                    if (s == s1 || s == s2) continue;
                    if (gr && s != 0 && s != 16 && G.nextFloat() < 0.3f * g) continue;   // GRIT: sparser
                    hit(s, s == 0 || s == 16 ? 106 : 100);
                }
            }
        } else if (role == RoleSnare && !snareDone) {
            snareDone = true;
            hit(s1, 112 + R.nextInt(8)).nudge = (int8_t) late; hit(s2, 112 + R.nextInt(8)).nudge = (int8_t) late;   // bar 1: beat 3; bar 2: displaced
            hit(counter, 44 + R.nextInt(26)).nudge = (int8_t) (late / 2);                                          // the counter-snare
            if (R.nextFloat() < 0.5f) hit(R.nextBool() ? 14 : 15, 40 + R.nextInt(25));   // a pickup ghost late in bar 1
            if (R.nextFloat() < 0.35f) hit(s2 == 28 ? 26 : 25, 40 + R.nextInt(20));      // a pickup before the displaced snare
        } else if (role == RoleSnare && snareDone && !clapDone) {
            clapDone = true;   // the clap layers the two main snares only (GRIT: a hair behind the snare, so the two smear into one wide, dirty hit)
            for (int s : { s1, s2 }) hit(s, 84).nudge = (int8_t) (gr ? juce::jmin(50, late + 3) : 0);
        } else if (role == RoleHat && !hatDone) {
            hatDone = true;
            static const char* cells[] { "x..x..x.", "x..x.x..", "x.x..x.." };
            std::vector<int> hs; for (int h = 0; h < 4; ++h) for (int i = 0; i < 8; ++i) if (cells[cell][i] == 'x') hs.push_back(h * 8 + i);
            const int holes = 2 + R.nextInt(2) + (gr ? (int) std::lround(2.0f * g) : 0);   // 2-3 holes per 2 bars (never the downbeats; GRIT: up to 2 more)
            for (int k = 0; k < holes; ++k) { std::vector<int> c; for (int s : hs) if (s % 16 != 0) c.push_back(s); if (c.empty()) break; const int d = c[(size_t) R.nextInt((int) c.size())]; hs.erase(std::find(hs.begin(), hs.end(), d)); }
            for (int s : hs) { const bool start = (s % 8) == 0, underSnare = s == s1 || s == s2, downbeat = s % 16 == 0;   // (a hat under a main snare or the downbeat kick
                               // stays softer: it is masked there anyway, and the peak stays clean)
                               auto& st = hit(s, underSnare ? 58 : (downbeat ? 82 : (start ? 100 : (s % 16 == 15 ? 52 : 72 + R.nextInt(18))))); if (s & 1) st.nudge = (int8_t) swing;
                               if (gr) st.vel = (uint8_t) juce::jmax(1, (int) std::lround(st.vel * (1.0f - 0.12f * g))); }   // GRIT: a touch quieter
            // rolls: one before the bar-1 snare, and (often) one at the end of bar 2; never more than two per 2 bars
            { auto& st = hit(s1 - 1, 84); st.roll = 2; st.rate = 8; st.velRamp = 30; if (swing) st.nudge = (int8_t) swing; }
            if (R.nextFloat() < 0.75f && !(gr && G.nextFloat() < 0.4f * g)) { const int s = 30; if (R.nextBool()) { auto& st = hit(s, 78); st.roll = 4; st.rate = 8; st.velRamp = 40; } else { auto& st = hit(s, 76); st.roll = 3; st.rate = 6; st.velRamp = 25; } row[31] = Step(); }
        } else if (role == RoleOpenHat && !openDone) {
            openDone = true; if (R.nextFloat() < 0.6f) { const int s = R.nextBool() ? 31 : 15; if (!(gr && G.nextFloat() < 0.5f * g)) hit(s, 80); }
        } else if (role == RolePerc && !percDone) {
            percDone = true;   // metallic shots in the holes, and now and then the "money shot" on the loop's beat 1
            static const int spots[] { 6, 12, 20, 23, 29 }; int n = 1 + R.nextInt(2);
            for (int k = 0; k < n; ++k) { const int s = spots[R.nextInt(5)]; if (s != s1 && s != s2) hit(s, 70 + R.nextInt(30)).pitch = (int8_t) (R.nextInt(3) - 1); }
            if (R.nextFloat() < 0.3f) hit(0, 56);
        }
    }
}

// the DRILL kit's mix: the 808 carries the low end at the level of the drums; the fill tom sits back. (The kit holds only four
// snare / perc tracks, so the every-4/8-bar fills stay a fill and never stack into one loud hit.)
inline const std::array<float, kTracks>& kitVols() {
    static const std::array<float, kTracks> v { 1.0f, 0.5f, 0.55f, 0.27f, 0.62f, 0.55f, 0.45f, 0.32f, 0.45f, 1.0f, 1.0f, 0.45f, 0.45f, 0.45f, 0.45f, 0.45f };
    return v;
}
// the DRILL kit's starter beats: A and B are two different generations (pattern family, snare spot and hat cell all change), at the
// new-beat GRIT (70: the tough, dirty lane out of the box) with the DIRTY 808 / snare
constexpr int kStarterGrit = 70;
inline void starter(Model& m, const std::function<juce::String(const juce::String&)>& categoryOf) {
    std::array<uint8_t, kTracks> roles {}; std::array<bool, kTracks> none {};
    for (int t = 0; t < kTracks; ++t) { const auto& nm = m.tracks[(size_t) t].sound; roles[(size_t) t] = categoryOf ? roleOf(categoryOf(nm), nm) : (nm.contains("808") ? (uint8_t) Role808 : roleOf({}, nm)); }
    generate(m.patterns[0], roles, none, 7, kStarterGrit); generate(m.patterns[1], roles, none, 8, kStarterGrit);
    for (auto& tr : m.tracks) { if (tr.sound == "DRILL 808 SLIDE" || tr.sound == "DRILL 808 LONG") tr.sound = "DRILL 808 DIRTY"; else if (tr.sound == "DRILL SNARE") tr.sound = "DRILL SNARE DIRTY"; }   // (as GENERATE DRILL does at GRIT 50+)
}

} // namespace gb::drill
