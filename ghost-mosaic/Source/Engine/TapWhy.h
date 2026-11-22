// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
//
// THE REPLAY: why the engine picked a piece. It does the engine's own search again, away from the audio
// thread, from the numbers the tap copied out (Tap.h): the same arithmetic in the same order as
// samplebrain/brain/src/brain.cpp (search, rev_search, search_synapses, stickify) and block.cpp (compare),
// so the scores are the engine's to the bit, and the piece it arrives at is the piece the engine played.
// It keeps its own copy of the engine's "used" memory in step, pick by pick.
//
// No GUI, no engine, no JUCE component: plain data in, plain data out (tests/TapTests.inc drives it directly).
// mosaic::tap::Model (TapModel.h) runs it on a low-priority thread and hands the answers to the views.
#pragma once
#include "Tap.h"

namespace mosaic::tap
{
// One candidate's score, in the engine's own stages. LOWER = CLOSER = BETTER, except SEARCH STYLE
// "Most different", which picks the HIGHEST. Scores have no fixed scale (they depend on the sounds and on
// TONE / TIMBRE): compare them with each other or use Model::scoreScale(), never against constants.
struct Score
{
    int piece = -1;                    // library piece (-1 = none)
    int rank = 0;                      // 0 = the search's winner, 1 = the first runner-up ...
    double total = 0;                  // what the engine compared
    // the stages, in the engine's order
    double tone = 0, timbre = 0;       // plain measurements: the band distance and the timbre distance (0 when TONE / TIMBRE leaves one out)
    double flatTone = 0, flatTimbre = 0;   // the same from the flattened measurements
    double plain = 0, flat = 0;        // each side after TONE / TIMBRE
    double mixed = 0;                  // the two sides after SHAPE / PITCH ONLY
    double terrain = 1;                // TERRAIN's brightness factor for this piece (1 = none; < 1 = preferred)
    double shaped = 0;                 // mixed x terrain
    double used = 0;                   // the piece's "used" memory when it was scored (0 when not a finite number)
    double noveltyPenalty = 0;         // total - shaped: what NOVELTY did to this score (can be negative)
    // three shares that add up to `total` (to rounding): for a stacked bar
    double toneShare = 0, timbreShare = 0, usedShare = 0;
    bool finite = true;                // false: the engine could not score it (a silent piece has no flattened measurements); total = 0
};

// What one pick was searched with (the renderer's own values at that moment)
struct WhySettings
{
    int style = 0;                     // 0 Closest, 1 Most different, 2 Network, 3 Network + wait
    float ratio = 0, nRatio = 0;       // TONE / TIMBRE, SHAPE / PITCH ONLY
    uint32_t fftStart = 0, fftEnd = 100;
    float novelty = 0, boredom = 0.9f, stickyness = 0;
    uint32_t synapses = 20;            // CONNECTIONS as the engine held it before this search
    double slideLimit = 0;             // WAIT UNTIL CLOSE
    bool weightsOn = false, factorOn = false;
    float tilt = 0;                    // TERRAIN's brightness preference the factors were made for
};

// One side of a comparison: where a target piece's four rows are, and which of them hold a not-a-number
struct WhyTarget
{
    const float* bands = nullptr; const float* timbre = nullptr; const float* flatBands = nullptr; const float* flatTimbre = nullptr;
    int bandCount = 0; uint8_t flags = 0;            // rowFlag bits
    bool ok() const noexcept { return bands != nullptr && timbre != nullptr; }
};

inline constexpr int kRunnerUps = 8, kMaxAllowed = 64;

// The explanation of one pick
struct Why
{
    enum State : uint8_t { waiting = 0, ready, skipped, unavailable };
    State state = waiting;             // ready: everything below is filled in
    bool approximate = false;          // true: the replay could not be sure it saw exactly what the engine saw (see `note`)
    bool agrees = false;               // the replay arrived at the piece the engine played
    const char* note = "Waiting to be explained.";   // plain words, never null
    int candidates = 0;                // pieces the search scored
    int unscored = 0;                  // pieces it looked at but could not score (not-a-number: silent pieces)
    Score winner;                      // the search's winner (piece -1: nothing could be scored)
    Score played;                      // the piece that plays: the winner, unless SMOOTH played the following piece or a Network search found nothing
    int runnerUps = 0;
    Score runnerUp[kRunnerUps];        // the next best, best first
    // Network styles: the previous piece's connections the search was allowed to look at, nearest first
    int allowedTotal = 0;              // how many it was allowed (CONNECTIONS, or fewer if the piece has fewer)
    int allowedListed = 0;             // how many are listed here (at most kMaxAllowed)
    int allowed[kMaxAllowed];          // library pieces
    bool allowedOn[kMaxAllowed];       // false: its sound is switched off, so it was not scored
    // SMOOTH (stickyness): the engine plays the piece after the previous one when  following x (1 - SMOOTH) < winner x SMOOTH
    bool stickyTested = false;         // SMOOTH was above 0 and the previous piece had a following piece
    int stickyPiece = -1;              // that following piece
    float stickyFollowing = 0;         // its score
    float stickyWinner = 0;            // the winner's score
    float stickyLeft = 0, stickyRight = 0;   // the two sides of the comparison above
    double stickyLimit = 0;            // the following piece is played when its score is under this (winner x SMOOTH / (1 - SMOOTH))
    bool stickyPlayed = false;         // it was played instead of the winner
    // WAIT UNTIL CLOSE (style 3): the target moves on only when the winner's score is under the limit
    bool slideTested = false;
    bool slideErrorKnown = false;      // false: nothing could be scored (the engine holds "infinitely far")
    double slideError = 0, slideLimit = 0;
    bool slideHeld = false;            // the target waited
};

// Keeps the engine's "used" memory in step and explains picks. One instance, one thread (not thread-safe).
class Replay
{
public:
    // ---- the world. `usedKnown`: this library was seen arriving, so its "used" memory starts as the snapshot's.
    void setLibrary (std::shared_ptr<const BrainSnapshot> lib, bool usedKnown);
    const BrainSnapshot* library() const noexcept { return m_lib.get(); }
    // which sounds the search may use, in the engine's order: each entry = the sound's first piece, top bit = switched on
    void setSounds (const uint32_t* firstAndOn, int count, bool complete);
    void setWeights (const float* hundred);                  // TERRAIN's weight per band (used when a pick says weightsOn)

    // ---- the engine's "used" memory
    void setUsed (uint32_t piece, float value);              // an exact value from the engine (a rested piece)
    void usageSlice (uint32_t first, const float* values, int count);   // a slice of the engine's own counters
    void usedLost();                                          // cells were lost: the copy can no longer be trusted
    bool usedValid() const noexcept { return m_usedValid; }   // the copy is the engine's, to the bit
    const std::vector<float>& used() const noexcept { return m_used; }
    int usedChecked() const noexcept { return (int) juce::jmin ((int64_t) m_cleanRun, (int64_t) m_used.size()); }   // pieces confirmed since the last correction

    // ---- one pick, in the order the engine made them. `scan` false keeps the "used" memory going but explains nothing.
    void pick (const WhySettings& s, const WhyTarget& target, const Match& m, const Used& u, bool scan, Why& out);

    // ---- the engine's distance between a target piece and library piece `i` under `s` (what block::compare returns)
    double distance (const WhySettings& s, const WhyTarget& target, int i) const;
    void describe (const WhySettings& s, const WhyTarget& target, int i, Score& out) const;

private:
    struct Dist;
    void prepare (Dist& d, const WhySettings& s, const WhyTarget& t) const;
    void fade (float boredom);
    void stamp (uint32_t piece);
    void checkAgainst (uint32_t piece, float engineValue);
    bool soundActive (uint32_t piece) const;
    const float* factorsFor (float tilt) const;

    std::shared_ptr<const BrainSnapshot> m_lib;
    std::vector<float> m_used;
    bool m_usedValid = false;
    int64_t m_cleanRun = 0;                                  // "used" values confirmed in a row against the engine's
    struct Range { uint32_t first, end; bool on; };          // [first, end): the search stops one short of the sound's last piece
    std::vector<Range> m_ranges; bool m_rangesComplete = false, m_rangesKnown = false;
    float m_weights[kBands]; bool m_weightsKnown = false;
    mutable std::vector<float> m_factor; mutable float m_factorTilt = 0; mutable bool m_factorMade = false;
};

// A stable 0..1 scale for scores, for drawing (scores have no fixed scale). It follows the scores of recent
// picks slowly, on a log scale: 0 = as close as the best winners lately, 1 = as far as the worst runner-ups.
class ScoreScale
{
public:
    float unit (double score) const noexcept;                // 0 (closest) .. 1 (farthest), clamped
    double low() const noexcept { return m_lo; }             // the score drawn at 0
    double high() const noexcept { return m_hi; }            // the score drawn at 1
    bool ready() const noexcept { return m_seen > 0; }       // false until one pick has been explained (unit() then returns 0.5)
    void feed (const Why& w, double dtSeconds);              // (the model calls this)
    void reset() noexcept { m_seen = 0; m_lo = 1; m_hi = 10; }
    // the same mapping for your own range
    static float logUnit (double score, double low, double high) noexcept;

private:
    double m_lo = 1, m_hi = 10; int m_seen = 0;
};
}
