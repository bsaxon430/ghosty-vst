/*
 * SEANCE (GhostBlock) - STEPS: the pattern kinds and Seance's own built-in patterns (lines for one key, arps over the
 * held notes). No ArpEZator, no JUCE: the sound-writing tool uses this too.
 */
#pragma once
#include <algorithm>
#include <string>
#include <vector>
#include "ghostblock/seance/steps.h"

namespace ghostblock::seance
{
namespace steppat
{
// kinds (StepPattern::kind): what ANOTHER makes another of. ArpEZator moods: 0 SUNRISE 1 DUSK 2 MIDNIGHT 3 TRAP 4 HIP HOP
enum Kind : int { EMPTY_KEY = 0, EMPTY_HELD = 1, IDEA_MIDNIGHT = 10, IDEA_DUSK = 11, FIG_MIDNIGHT = 12, FIG_DUSK = 13, IDEA_SUNRISE = 14, FIG_SUNRISE = 15, IDEA_TRAP = 16, IDEA_HIPHOP = 17,
                  LINE = 100, HELD_ARP = 200 };
inline bool isIdea(int kind) { return kind == IDEA_MIDNIGHT || kind == IDEA_DUSK || kind == IDEA_SUNRISE || kind == IDEA_TRAP || kind == IDEA_HIPHOP; }
inline bool isFigure(int kind) { return kind == FIG_MIDNIGHT || kind == FIG_DUSK || kind == FIG_SUNRISE; }
inline bool rerollable(int kind) { return isIdea(kind) || isFigure(kind); }
inline int moodOfKind(int kind) { return kind == IDEA_SUNRISE || kind == FIG_SUNRISE ? 0 : kind == IDEA_DUSK || kind == FIG_DUSK ? 1 : kind == IDEA_TRAP ? 3 : kind == IDEA_HIPHOP ? 4 : 2; }
inline int ideaKindOf(int mood) { return mood == 0 ? IDEA_SUNRISE : mood == 1 ? IDEA_DUSK : mood == 3 ? IDEA_TRAP : mood == 4 ? IDEA_HIPHOP : IDEA_MIDNIGHT; }
inline int figKindOf(int mood) { return mood == 0 ? FIG_SUNRISE : mood == 1 ? FIG_DUSK : FIG_MIDNIGHT; }
inline int ladderOff(int idx, int oct) { return (idx & 63) + 64 * oct; } // how a ladder note is stored (oct may be negative)

// ---------------------------------------------------------------- Seance's own patterns
struct Builtin { const char *name; int bars; const char *steps; }; // one token per sixteenth, see parse()
// LINES (one key). Tokens: a number = semitones from the key, "." rest, "-" the note before rings on, ">" after a number = accent
inline const std::vector<Builtin> &lines()
{
    static const std::vector<Builtin> v = {
        {"Octave pump", 1, "0 12 0 12 0 12 0 12 0 12 0 12 0 12 0 12"},
        {"Offbeat bass", 1, ". . 0> - . . 0> - . . 0> - . . 0> -"},
        {"Gallop", 1, "0> . 0 0 0> . 0 0 0> . 0 0 0> . 0 0"},
        {"Tresillo", 1, "0> - - 0> - - 0> - 0> - - 0> - - 0> -"},
        {"Trance gate", 1, "0> . 0 0 . 0 0 . 0> . 0 0 . 0 0 0"},
        {"Acid crawl", 2, "0> 0 12 0 3> 0 10 12 0> 0 7 0 3 12> 0 10 0> - 12 0 3 3> 15 12 0 0> 7 - 10 12> 3 0"},
        {"Minor climb", 2, "0> - 3 - 7 - 10 - 12> - 15 - 19 - 15 - 12> - 10 - 7 - 3 - 0> - 3 - 7 - 10 -"},
        {"Dark fall", 2, "12> - - 10 - - 8 - 7> - - 5 - - 3 - 2> - - 0 - - -2 - 0> - - - . . . ."},
        {"Fifth stabs", 1, "0> . . 7 . . 0> . . 7 . . 12> . 7 ."},
        {"Dotted echo", 2, "0> - - 0 - - 0 - - 0 - - 0 - - 0 - - 0 - - 0 - - 0 - - 0 - - 0 -"},
        {"Rolling threes", 2, "0> 7 12 0 7 12 0 7 12 0 7 12 0 7 12 0 0> 7 12 0 7 12 0 7 12 0 7 12 0 7 12 0"},
        {"Two-bar riff", 2, "0> . 0 3 . 5 . 7> - . 5 . 3 . 0 . 0> . 0 3 . 5 . 10> - . 7 . 5 3 . ."},
        {"Night bells", 2, "12> - - - 19 - - - 15> - - - 22 - 19 - 12> - - - 24 - - - 22> - 19 - 15 - - -"},
        {"Stutter fall", 1, "12> 12 . 10 10 . 7> 7 . 5 5 . 3> 3 0 0"},
    };
    return v;
}
// ARPS (the notes you hold). Tokens: a number = which held note (0 = lowest; past the top it carries on an octave up), rest / tie / accent as above
inline const std::vector<Builtin> &heldArps()
{
    static const std::vector<Builtin> v = {
        {"Up", 1, "0> 1 2 3 0> 1 2 3 0> 1 2 3 0> 1 2 3"},
        {"Down", 1, "3> 2 1 0 3> 2 1 0 3> 2 1 0 3> 2 1 0"},
        {"Up and down", 1, "0> 1 2 3 4 3 2 1 0> 1 2 3 4 3 2 1"},
        {"Up two octaves", 1, "0> 1 2 3 4 5 6 7 0> 1 2 3 4 5 6 7"},
        {"Thumb pedal", 1, "0> 1 0 2 0 3 0 2 0> 1 0 2 0 3 0 4"},
        {"Top pedal", 1, "3> 0 3 1 3 2 3 1 3> 0 3 1 3 2 3 4"},
        {"Converge", 1, "0> 5 1 4 2 3 2 4 0> 5 1 4 2 3 2 4"},
        {"Skip a step", 1, "0> 2 1 3 2 4 3 5 4> 2 3 1 2 0 1 3"},
        {"Three against four", 2, "0> 1 2 0 1 2 0 1 2 0 1 2 0 1 2 0 0> 1 2 0 1 2 0 1 2 0 1 2 0 1 2 3"},
        {"Five-step drift", 2, "0> 1 2 3 4 0 1 2 3 4 0 1 2 3 4 0 0> 1 2 3 4 0 1 2 3 4 0 1 2 3 4 5"},
        {"Held eighths", 1, "0> - 1 - 2 - 3 - 0> - 1 - 2 - 3 -"},
        {"Broken chord, slow", 2, "0> - - - 1 - - - 2 - - - 3 - - - 4> - - - 3 - - - 2 - - - 1 - - -"},
        {"Syncopated", 1, "0> . 1 . . 2 . 3 0> . 1 . . 2 . 4"},
        {"Random walk", 2, "0> 2 1 3 2 2 4 3 1> 0 2 3 5 4 3 2 0> 1 3 2 4 4 3 5 2> 1 0 2 1 3 2 0"},
    };
    return v;
}
inline bool fromBuiltin(StepPattern &out, const Builtin &b, bool ladder, int index)
{
    out = StepPattern();
    out.loopTicks = b.bars * StepSeq::BAR;
    out.ladder = ladder ? 1 : 0;
    out.kind = (ladder ? (int)HELD_ARP : (int)LINE) + index;
    int k = 0, last = -1;
    const char *p = b.steps;
    while (*p && k < b.bars * 16)
    {
        while (*p == ' ') ++p;
        if (!*p) break;
        if (*p == '.') { last = -1; ++p; ++k; continue; }
        if (*p == '-' && !(p[1] >= '0' && p[1] <= '9'))
        {
            if (last >= 0) out.notes[last].d += 24;
            ++p; ++k;
            continue;
        }
        int sign = 1, v = 0;
        if (*p == '-') { sign = -1; ++p; }
        while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
        bool acc = false;
        if (*p == '>') { acc = true; ++p; }
        last = out.n;
        out.add(k * 24, 20, ladder ? ladderOff(std::max(0, v), 0) : sign * v, acc ? 118 : 88, 1);
        ++k;
    }
    out.setName(std::string(b.name) + (ladder ? "  (over the notes you hold)" : "  (one key)"));
    return out.n > 0;
}
} // namespace steppat
} // namespace ghostblock::seance
