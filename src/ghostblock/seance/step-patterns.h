/*
 * SEANCE (GhostBlock) - what the STEPS page can put into the sequencer. Main thread only, JUCE-free.
 *
 *  - MIDNIGHT / DUSK ideas: written by ArpEZator's own composing engine (libs/arpezator-engine, unchanged): its
 *    chords, arp, melody and bass for a seed, exactly as ArpEZator's GENERATE makes them, stored as offsets from the
 *    key's tonic so one held key plays the whole idea in that key.
 *  - MIDNIGHT / DUSK figures: the same idea's arp FIGURE (its 3-8 step ladder cell with accents, rests, ties and
 *    octave lifts, re-synced as ArpEZator does) laid over the notes you hold in place of the idea's own chords.
 *  - Lines (one key) and arps (held notes): Seance's own patterns, defined in this file.
 */
#pragma once
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#include "ghostblock/seance/steps.h"
#include "ghostblock/seance/step-builtins.h"
#include "Idea.h" // ArpEZator's engine

namespace ghostblock::seance
{
namespace steppat
{
inline arp::Idea makeIdea(int mood, uint32_t seed)
{
    arp::setNewIdeaMelVer(arp::kMelVerCurrent); // TRAP: ArpEZator's current melody writer, as its own plugin sets at start
    arp::Idea I;
    I.mode = mood;
    I.seed = seed;
    arp::applyModeDefaults(I);
    arp::newIdea(I);
    return I;
}

// One key plays the whole idea. Returns the lanes the mode leaves on (bit per lane) through laneMaskOut.
// followChords: the idea is re-written over the notes you hold. Every note is filed as "which tone of the chord that
// was under it in ArpEZator's own progression (root first), how many octaves from that chord's root-position stack near
// the tonic, and how many semitones off that tone" - and played as the same tone of YOUR chord (your lowest note stands
// for the root), so the rhythm, contour and non-chord colour notes carry over to whatever you play.
inline bool arpezIdea(StepPattern &out, int mood, uint32_t seed, int *laneMaskOut = nullptr, bool followChords = false)
{
    const arp::Idea I = makeIdea(mood, seed);
    const int loop = std::clamp(I.total, arp::BAR, arp::BAR * 16);
    out = StepPattern();
    out.loopTicks = loop;
    out.ladder = followChords ? 1 : 0;
    out.kind = ideaKindOf(mood);
    out.seed = seed;
    const int tonic = arp::tonicAtOrAbove(I.H.key, 55); // the held key stands for this note
    auto put = [&](const std::vector<arp::MidiNote> &v, int lane) {
        for (auto &n : v)
        {
            if (n.t < 0 || n.t >= loop) continue;
            const int vel = (int)std::lround(std::clamp(n.v, 0.05, 1.0) * 127.0);
            if (!followChords)
            {
                out.add(n.t, std::min(n.d, loop), std::clamp(n.p - tonic, -120, 120), vel, lane);
                continue;
            }
            const arp::HChord &c = I.H.at(n.t);
            std::vector<int> iv; // the chord's tones as semitones above its root, ascending
            for (int pc : c.pcs) { const int d = ((pc - c.root) % 12 + 12) % 12; if (std::find(iv.begin(), iv.end(), d) == iv.end()) iv.push_back(d); }
            if (std::find(iv.begin(), iv.end(), 0) == iv.end()) iv.push_back(0);
            std::sort(iv.begin(), iv.end());
            int rootAt = tonic + (((c.root - tonic) % 12) + 12) % 12; // the chord's root nearest the tonic
            if (rootAt > tonic + 6) rootAt -= 12;
            int bestJ = 0, bestO = 0, bestD = 999;
            for (int o = -5; o <= 5; ++o)
                for (int j = 0; j < (int)iv.size() && j < 60; ++j)
                {
                    const int d = n.p - (rootAt + iv[(size_t)j] + 12 * o);
                    if (std::abs(d) < std::abs(bestD) || (std::abs(d) == std::abs(bestD) && d > bestD)) { bestD = d; bestJ = j; bestO = o; }
                }
            out.add(n.t, std::min(n.d, loop), ladderOff(bestJ, bestO), vel, StepNote::packLane(lane, bestD));
        }
    };
    put(arp::chordLaneNotes(I), 0);
    for (int l : {(int)arp::LaneArp, (int)arp::LaneMelody, (int)arp::LaneBass})
    { // as ArpEZator's own MIDI export: the idea's GROOVE applied on the way out (TRAP and HIP HOP swing)
        const int spb = l == arp::LaneArp ? I.arpSpb : l == arp::LaneMelody ? I.melSpb : 8;
        put(arp::laneMidiNotes(arp::applyGroove(I.lanes[l].notes, I.groove, spb, I.seedUsed + (uint32_t)l), loop), l);
    }
    out.sortNotes();
    int mask = 0;
    for (int l = 0; l < 4; ++l)
        if (!I.lanes[l].mute) mask |= 1 << l;
    (void)mask; // (ArpEZator's own default leaves some parts off per mood; here the arp and the melody start on, chords and bass a click away)
    if (laneMaskOut) *laneMaskOut = 0x6;
    out.setName(followChords ? arp::modeName(mood) + " idea over your chords  #" + std::to_string(seed)
                             : arp::modeName(mood) + " idea in " + arp::keyName(I.H.key, arp::chordScale(I)) + "  #" + std::to_string(seed));
    return out.n > 0;
}

// The idea's arp figure over the notes you hold (four bars).
inline bool arpezFigure(StepPattern &out, int mood, uint32_t seed)
{
    const arp::Idea I = makeIdea(mood, seed);
    const arp::Figure &f = I.fig;
    const int len = std::clamp(f.len, 1, 8), st = arp::BAR / 16, bars = 4, total = bars * 16;
    int resyncBars = f.recipe ? std::max(1, f.resync) : (I.resync > 0 ? I.resync : 2);
    const double gate = I.arpGate > 0 ? I.arpGate : 0.45;
    out = StepPattern();
    out.loopTicks = bars * arp::BAR;
    out.ladder = 1;
    out.kind = figKindOf(mood);
    out.seed = seed;
    int lowest = 99;
    bool hasPedal = false;
    for (int i = 0; i < len; ++i)
    {
        hasPedal = hasPedal || f.s[(size_t)i].pedal;
        if (!f.s[(size_t)i].pedal && !f.s[(size_t)i].rest && !f.s[(size_t)i].hold) lowest = std::min(lowest, f.s[(size_t)i].idx);
    }
    if (lowest == 99) lowest = 1;
    const int base = hasPedal ? 1 : 0; // with a pedal in the figure the pedal takes your lowest note and the ladder starts above it
    int sync = 0, last = -1;
    for (int k = 0; k < total; ++k)
    {
        if (k > 0 && k % (resyncBars * 16) == 0) sync = k;
        const int phase = (k - sync) % len;
        const arp::Step &s = f.s[(size_t)phase];
        if (s.rest) { last = -1; continue; }
        if (s.hold)
        { // "~": the note before rings on
            if (last >= 0) out.notes[last].d += st;
            continue;
        }
        // the pedal is the lowest note you hold; ladder notes count up from it
        const int idx = s.pedal ? 0 : std::clamp(s.idx - lowest + base, 0, 47);
        const bool phraseEnd = k + 1 >= total || (k + 1) % (resyncBars * 16) == 0;
        const int d = (s.tie || phraseEnd) ? st : std::max(6, (int)std::lround(st * gate));
        const float v = phase == 0 ? 0.9f : s.acc ? 0.84f : 0.64f;
        last = out.n;
        if (!out.add(k * st, d, ladderOff(idx, s.oct), (int)std::lround(v * 127.f), 1)) break;
    }
    out.setName(arp::modeName(mood) + " figure over your chord (" + std::to_string(len) + " steps)  #" + std::to_string(seed));
    return out.n > 0;
}

} // namespace steppat
} // namespace ghostblock::seance
