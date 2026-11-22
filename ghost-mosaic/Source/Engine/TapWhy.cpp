// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
// The replay (TapWhy.h). The arithmetic here follows samplebrain/brain/src/block.cpp and brain.cpp line for
// line: the same types, the same order, the same helper shapes, so a compiler makes the same numbers.
#include "TapWhy.h"
#include "Shaping.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <cfloat>
#include <cmath>
#include <limits>

namespace mosaic::tap
{
namespace
{
// (block.cpp's own helpers, kept in its shape)
double blend (double a, double b, double t) { return a * (1 - t) + b * t; }
double square (double a) { return a * a; }
const double kUsageFactor = 1000;                // brain.cpp: usage_factor
const int kMfccFilters = 12, kFftBias = 200;     // block.cpp: MFCC_FILTERS, FFT_BIAS

inline bool sameBits (float a, float b) noexcept { return std::memcmp (&a, &b, sizeof (float)) == 0; }
inline bool sameBits (double a, double b) noexcept { return std::memcmp (&a, &b, sizeof (double)) == 0; }
inline bool isZero (float v) noexcept { return ! (v < 0.0f) && ! (v > 0.0f); }   // (also false for a not-a-number, as == 0 is)
inline bool isOne (float v) noexcept { return ! (v < 1.0f) && ! (v > 1.0f); }

// the best few candidates of a search, best first (ties keep the engine's order: the earlier one stays ahead)
struct Top
{
    explicit Top (bool highestWins) : rev (highestWins) {}
    void offer (double d, int piece) noexcept
    {
        if (n == kMax && ! ahead (d, score[n - 1])) return;
        int p = n < kMax ? n : kMax - 1;
        while (p > 0 && ahead (d, score[p - 1])) { score[p] = score[p - 1]; index[p] = index[p - 1]; --p; }
        score[p] = d; index[p] = piece;
        if (n < kMax) ++n;
    }
    bool ahead (double a, double b) const noexcept { return rev ? a > b : a < b; }
    static constexpr int kMax = kRunnerUps + 2;
    double score[kMax]; int index[kMax]; int n = 0; bool rev;
};
}

// ------------------------------------------------------------------------------------ the distance
// block::compare for one target piece against library pieces, set up once per search
struct Replay::Dist
{
    const BrainSnapshot* B = nullptr;
    const float* tb = nullptr; const float* tt = nullptr; const float* tfb = nullptr; const float* tft = nullptr;
    uint8_t tflags = 0;
    uint32_t start = 0, end = 0;
    float fl = 1;                                // (float) fft_a.get_length()
    float ratio = 0, nRatio = 0, novelty = 0;
    const float* w = nullptr;                    // TERRAIN's band weights (nullptr = none)
    const float* factor = nullptr;               // TERRAIN's factor per piece (nullptr = none)
    const float* used = nullptr;
    bool usesBands = false, usesTimbre = false;
    bool plainNeeded = true, flatNeeded = true;  // a side multiplied by exactly 0 is left out (it changes no bit)
    bool clipped = false;                        // the library holds fewer bands than the search asked for
    bool flatMissing = false;                    // flattened rows are needed but were not kept

    // block::_compare (tone / timbre: the two parts before TONE / TIMBRE mixes them)
    inline double side (const float* fa, const float* ma, const float* fb, const float* mb, double* tone, double* timbre) const noexcept
    {
        double mfcc_acc = 0;
        double fft_acc = 0;
        if (isZero (ratio))
        {
            if (w != nullptr) { for (uint32_t i = start; i < end; ++i) fft_acc += w[i] * square (fa[i] - fb[i]); }
            else { for (uint32_t i = start; i < end; ++i) fft_acc += square (fa[i] - fb[i]); }
            const double r = (fft_acc / fl) * kFftBias;
            if (tone != nullptr) { *tone = r; *timbre = 0; }
            return r;
        }
        if (isOne (ratio))
        {
            for (int i = 0; i < kMfccFilters; ++i) mfcc_acc += square (ma[i] - mb[i]);
            const double r = mfcc_acc / (float) kMfccFilters;
            if (tone != nullptr) { *tone = 0; *timbre = r; }
            return r;
        }
        if (w != nullptr) { for (uint32_t i = start; i < end; ++i) fft_acc += w[i] * square (fa[i] - fb[i]); }
        else { for (uint32_t i = start; i < end; ++i) fft_acc += square (fa[i] - fb[i]); }
        for (int i = 0; i < kMfccFilters; ++i) mfcc_acc += square (ma[i] - mb[i]);
        if (tone != nullptr) { *tone = fft_acc / fl; *timbre = mfcc_acc / (float) kMfccFilters; }
        return blend (fft_acc / fl, mfcc_acc / (float) kMfccFilters, ratio);
    }
    bool bad (int i) const noexcept
    {
        const uint8_t f = (uint8_t) (B->rows.flags[(size_t) i] | tflags);
        return (usesBands && (f & (rowFlag::badBands | rowFlag::badFlatBands)) != 0)
            || (usesTimbre && (f & (rowFlag::badTimbre | rowFlag::badFlatTimbre)) != 0);
    }
    // block::compare
    inline double total (int i) const noexcept
    {
        if (bad (i)) return std::numeric_limits<double>::quiet_NaN();   // (the engine's own sum would be not-a-number: never chosen)
        const double a = plainNeeded ? side (tb, tt, B->bandsOf (i), B->timbreOf (i), nullptr, nullptr) : 0.0;
        const double b = flatNeeded ? side (tfb, tft, B->flatBandsOf (i), B->flatTimbreOf (i), nullptr, nullptr) : 0.0;
        if (factor != nullptr) return blend (blend (a, b, nRatio) * factor[i], used[i], novelty);
        return blend (blend (a, b, nRatio), used[i], novelty);
    }
    void describe (int i, Score& s) const noexcept
    {
        s = Score();
        s.piece = i;
        s.finite = ! bad (i);
        const bool haveFlat = tfb != nullptr && tft != nullptr && B->hasFlattened;
        s.plain = side (tb, tt, B->bandsOf (i), B->timbreOf (i), &s.tone, &s.timbre);
        s.flat = haveFlat ? side (tfb, tft, B->flatBandsOf (i), B->flatTimbreOf (i), &s.flatTone, &s.flatTimbre) : 0.0;
        s.mixed = blend (s.plain, s.flat, nRatio);
        s.terrain = factor != nullptr ? (double) factor[i] : 1.0;
        s.shaped = factor != nullptr ? s.mixed * factor[i] : s.mixed;
        const float u = used[i];
        s.used = std::isfinite (u) ? (double) u : 0.0;
        s.total = blend (s.shaped, u, novelty);
        if (! s.finite || ! std::isfinite (s.total)) { s.finite = false; s.total = 0; }
        s.noveltyPenalty = s.total - s.shaped;
        const double r = ratio, nr = nRatio, keep = (1.0 - (double) novelty) * s.terrain;
        const double toneIn = isZero (ratio) ? 1.0 : (isOne (ratio) ? 0.0 : 1.0 - r), timbreIn = isOne (ratio) ? 1.0 : (isZero (ratio) ? 0.0 : r);
        s.toneShare = keep * ((1.0 - nr) * toneIn * s.tone + nr * toneIn * s.flatTone);
        s.timbreShare = keep * ((1.0 - nr) * timbreIn * s.timbre + nr * timbreIn * s.flatTimbre);
        s.usedShare = s.used * (double) novelty;
    }
};

void Replay::prepare (Dist& d, const WhySettings& s, const WhyTarget& t) const
{
    const BrainSnapshot& B = *m_lib;
    d.B = &B;
    d.tb = t.bands; d.tt = t.timbre; d.tfb = t.flatBands; d.tft = t.flatTimbre; d.tflags = t.flags;
    d.ratio = s.ratio; d.nRatio = s.nRatio; d.novelty = s.novelty;
    d.start = s.fftStart;
    d.end = (uint32_t) std::fmin (s.fftEnd, (uint32_t) t.bandCount);          // block.cpp: fmin (m_fft1_end, m_fft.get_length())
    d.fl = (float) (uint32_t) t.bandCount;
    if (d.end > (uint32_t) B.bandCount) { d.end = (uint32_t) B.bandCount; d.clipped = true; }
    d.usesBands = ! isOne (s.ratio) && d.start < d.end;
    d.usesTimbre = ! isZero (s.ratio);
    d.plainNeeded = ! isOne (s.nRatio);
    d.flatNeeded = ! isZero (s.nRatio);
    d.flatMissing = d.flatNeeded && (t.flatBands == nullptr || t.flatTimbre == nullptr || ! B.hasFlattened);
    d.w = s.weightsOn ? m_weights : nullptr;
    d.factor = s.factorOn ? factorsFor (s.tilt) : nullptr;
    d.used = m_used.data();
}

const float* Replay::factorsFor (float tilt) const
{
    const BrainSnapshot& B = *m_lib;
    if (B.bright.size() != (size_t) B.numBlocks) return nullptr;
    if (! m_factorMade || ! sameBits (tilt, m_factorTilt) || m_factor.size() != B.bright.size())
    {
        m_factor.resize (B.bright.size());
        for (size_t i = 0; i < m_factor.size(); ++i) m_factor[i] = Shaping::tiltFactor (tilt, B.bright[i]);   // (Engine::updateBias)
        m_factorTilt = tilt; m_factorMade = true;
    }
    return m_factor.data();
}

double Replay::distance (const WhySettings& s, const WhyTarget& target, int i) const
{
    const juce::ScopedNoDenormals asTheAudioThread;
    if (m_lib == nullptr || i < 0 || i >= m_lib->numBlocks || ! m_lib->hasMeasurements || ! target.ok()) return std::numeric_limits<double>::quiet_NaN();
    Dist d;
    prepare (d, s, target);
    if (d.flatMissing) return std::numeric_limits<double>::quiet_NaN();
    return d.total (i);
}

void Replay::describe (const WhySettings& s, const WhyTarget& target, int i, Score& out) const
{
    const juce::ScopedNoDenormals asTheAudioThread;
    out = Score();
    if (m_lib == nullptr || i < 0 || i >= m_lib->numBlocks || ! m_lib->hasMeasurements || ! target.ok()) { out.finite = false; return; }
    Dist d;
    prepare (d, s, target);
    d.describe (i, out);
}

// ------------------------------------------------------------------------------------ the world
void Replay::setLibrary (std::shared_ptr<const BrainSnapshot> lib, bool usedKnown)
{
    m_lib = std::move (lib);
    m_used.clear();
    if (m_lib != nullptr) m_used = m_lib->usage0;
    m_usedValid = usedKnown && m_lib != nullptr && m_used.size() == (size_t) m_lib->numBlocks;
    m_cleanRun = m_usedValid ? (int64_t) m_used.size() : 0;
    m_ranges.clear(); m_rangesKnown = false; m_rangesComplete = false;
    m_factorMade = false;
}

void Replay::setSounds (const uint32_t* firstAndOn, int count, bool complete)
{
    m_ranges.clear();
    const uint32_t n = m_lib != nullptr ? (uint32_t) m_lib->numBlocks : 0;
    for (int k = 0; k < count; ++k)
    {
        Range r;
        r.first = firstAndOn[2 * k] & 0x7fffffffu; r.on = (firstAndOn[2 * k] >> 31) != 0; r.end = firstAndOn[2 * k + 1];
        if (r.end > n) r.end = r.first < n && r.end != 0xffffffffu ? n : r.first;      // (never past the library)
        if (r.end < r.first) r.end = r.first;
        m_ranges.push_back (r);
    }
    m_rangesKnown = true; m_rangesComplete = complete;
}

void Replay::setWeights (const float* hundred) { std::memcpy (m_weights, hundred, sizeof (m_weights)); m_weightsKnown = true; }

bool Replay::soundActive (uint32_t piece) const
{
    for (auto& r : m_ranges) if (piece >= r.first && piece < r.end && r.on) return true;   // brain::is_block_active
    return false;
}

// ------------------------------------------------------------------------------------ the "used" memory
void Replay::fade (float boredom) { for (auto& v : m_used) v *= boredom; }                       // brain::deplete_usage
void Replay::stamp (uint32_t piece) { if (piece < m_used.size()) m_used[piece] = (float) ((double) m_used[piece] + kUsageFactor); }   // get_usage() += usage_factor

void Replay::checkAgainst (uint32_t piece, float engineValue)
{
    if (piece >= m_used.size()) return;
    if (sameBits (m_used[piece], engineValue)) return;
    m_used[piece] = engineValue; m_cleanRun = 0; m_usedValid = false;
}

void Replay::setUsed (uint32_t piece, float value) { if (piece < m_used.size()) m_used[piece] = value; }

void Replay::usageSlice (uint32_t first, const float* values, int count)
{
    bool clean = true;
    for (int k = 0; k < count; ++k)
    {
        const size_t i = (size_t) first + (size_t) k;
        if (i >= m_used.size()) break;
        if (! sameBits (m_used[i], values[k])) { m_used[i] = values[k]; clean = false; }
    }
    if (! clean) { m_cleanRun = 0; m_usedValid = false; return; }
    m_cleanRun += count;
    if (m_cleanRun >= (int64_t) m_used.size() && ! m_used.empty()) m_usedValid = true;   // a whole round with nothing to correct
}

void Replay::usedLost() { m_cleanRun = 0; m_usedValid = false; }

// ------------------------------------------------------------------------------------ one pick
void Replay::pick (const WhySettings& s, const WhyTarget& target, const Match& m, const Used& u, bool scan, Why& out)
{
    const juce::ScopedNoDenormals asTheAudioThread;              // (the engine searches with denormals off: the same here, or bits differ)
    out = Why();
    const BrainSnapshot* B = m_lib.get();
    const uint32_t N = B != nullptr ? (uint32_t) B->numBlocks : 0;
    const uint32_t src = m.src, prev = m.prevSrc;
    if (B == nullptr || N == 0 || m_used.size() != (size_t) N || src >= N)
    {
        out.state = Why::unavailable; out.note = "This pick's library is no longer here.";
        return;
    }
    const int style = s.style;
    const bool prevKnown = (m.flags & matchFlag::prevUnknown) == 0 && prev < N;
    const bool nextExists = prevKnown && (m.flags & matchFlag::nextExists) != 0 && prev + 1 < N;
    const bool usedWasValid = m_usedValid;

    if (style == 3)
    {
        out.slideTested = true; out.slideLimit = s.slideLimit;
        out.slideErrorKnown = (m.flags & matchFlag::nothingFound) == 0;
        out.slideError = out.slideErrorKnown ? u.error : 0.0;
        out.slideHeld = ! (u.error < s.slideLimit);
    }

    const char* cannot = nullptr;
    Dist d;
    if (! scan) cannot = "Not explained: the explanations had fallen behind the music.";
    else if (! B->hasMeasurements) cannot = "The library is too large for its measurements to be kept for explanations.";
    else if (! target.ok()) cannot = (m.flags & matchFlag::liveTarget) != 0 ? "Live target: this piece's measurements were not captured." : "This pick's target is no longer here.";
    else if (! m_rangesKnown) cannot = "Waiting to hear which sounds are switched on.";
    else if (s.weightsOn && ! m_weightsKnown) cannot = "Waiting for TERRAIN's band weights.";
    else if (style >= 2 && ! prevKnown) cannot = "The piece the library was at is not known for this pick.";
    else
    {
        prepare (d, s, target);
        if (d.flatMissing) cannot = "The flattened measurements were not kept (the library is very large), and SHAPE / PITCH ONLY needs them.";
    }

    if (cannot != nullptr)
    {
        // keep the "used" memory going from what the engine's own counters say
        fade (s.boredom);
        bool sure = true;
        if (style == 1) stamp (src);
        else if (style == 0)
        {
            if (! (s.stickyness > 0 && nextExists && src == prev + 1)) stamp (src);
            else
            {
                // SMOOTH may have played the following piece. Was that piece the search's winner as well? Its counter says.
                const float faded = u.nextBefore * s.boredom;
                const float stamped = (float) ((double) faded + kUsageFactor);
                if (sameBits (u.srcAfter, stamped)) stamp (src);
                else sure = false;                               // (the winner was another piece, and only a search finds which)
            }
        }
        else if (prevKnown) stamp (prev);
        else sure = false;
        if (! sure) usedLost();
        checkAgainst (src, u.srcAfter);
        if (nextExists) checkAgainst (prev + 1, u.nextAfter);
        out.state = scan ? Why::unavailable : Why::skipped; out.note = cannot;
        return;
    }

    // ---- the search itself (brain::search / rev_search / search_synapses)
    Top top (style == 1);
    uint32_t w = 0;
    bool found = false, cut = false;
    double closest = 0;
    if (style == 0)
    {
        closest = FLT_MAX;
        for (auto& r : m_ranges)
            if (r.on)
                for (uint32_t i = r.first; i < r.end; ++i)
                {
                    const double diff = d.total ((int) i);
                    if (std::isnan (diff)) { ++out.unscored; continue; }
                    ++out.candidates;
                    if (diff < closest) { closest = diff; w = i; found = true; }
                    top.offer (diff, (int) i);
                }
    }
    else if (style == 1)
    {
        closest = 0;                                             // (here: the furthest so far)
        for (auto& r : m_ranges)
            if (r.on)
                for (uint32_t i = r.first; i < r.end; ++i)
                {
                    const double diff = d.total ((int) i);
                    if (std::isnan (diff)) { ++out.unscored; continue; }
                    ++out.candidates;
                    if (diff > closest) { closest = diff; w = i; found = true; }
                    top.offer (diff, (int) i);
                }
    }
    else
    {
        closest = DBL_MAX;
        const uint32_t size = B->synCount[(size_t) prev];
        uint32_t num = s.synapses;
        if (size < num) num = size - 1;                          // (as the engine: unsigned)
        const uint32_t lim = size < num ? size : num;
        out.allowedTotal = (int) lim;
        for (uint32_t k = 0; k < lim; ++k)
        {
            if (k >= (uint32_t) B->synK) { cut = true; break; }
            const uint32_t c = B->synOf ((int) prev)[k];
            const bool on = c < N && soundActive (c);
            if (out.allowedListed < kMaxAllowed) { out.allowed[out.allowedListed] = (int) c; out.allowedOn[out.allowedListed] = on; ++out.allowedListed; }
            if (! on) continue;
            const double diff = d.total ((int) c);
            if (std::isnan (diff)) { ++out.unscored; continue; }
            ++out.candidates;
            if (diff < closest) { closest = diff; w = c; }
            top.offer (diff, (int) c);
        }
        found = w != 0;                                          // (the engine reads piece 0 as "nothing found")
    }

    // the scores as they stood when the search compared them (the "used" memory moves on below)
    {
        int at = 0;
        if (found && top.n > 0 && top.index[0] == (int) w) { d.describe ((int) w, out.winner); at = 1; }
        else if (found) d.describe ((int) w, out.winner);
        out.winner.rank = 0;
        if (! found) out.winner = Score();
        for (int k = at; k < top.n && out.runnerUps < kRunnerUps; ++k)
        {
            if (found && top.index[k] == (int) w) continue;
            d.describe (top.index[k], out.runnerUp[out.runnerUps]);
            out.runnerUp[out.runnerUps].rank = out.runnerUps + 1;
            ++out.runnerUps;
        }
    }

    // ---- deplete_usage, the stamp, stickify
    fade (s.boredom);
    stamp (style >= 2 ? prev : w);                               // (Network styles mark the piece being left)
    uint32_t result = w;
    bool stickyUnknown = false;
    if (style == 1) result = w;
    else if (style >= 2 && ! found) result = prev;
    else if (s.stickyness > 0)
    {
        if (! prevKnown) stickyUnknown = true;
        else if (nextExists)
        {
            const uint32_t next = prev + 1;
            const float dist = (float) closest;
            const float distToNext = (float) d.total ((int) next);
            out.stickyTested = true; out.stickyPiece = (int) next;
            out.stickyFollowing = distToNext; out.stickyWinner = dist;
            out.stickyLeft = distToNext * (1 - s.stickyness); out.stickyRight = dist * s.stickyness;
            out.stickyLimit = s.stickyness < 1.0f ? (double) dist * (double) s.stickyness / (1.0 - (double) s.stickyness) : std::numeric_limits<double>::infinity();
            if (out.stickyLeft < out.stickyRight) { result = next; out.stickyPlayed = true; }
        }
    }

    out.agrees = result == src;
    if (result == w && found) out.played = out.winner;
    else d.describe ((int) (out.agrees ? result : src), out.played);
    out.played.rank = out.played.piece == out.winner.piece ? 0 : -1;
    if (! out.agrees) out.played.piece = (int) src;

    // ---- what the engine's own counters say (two exact values per pick)
    const bool errorAgrees = style < 2 || sameBits (closest, u.error);
    checkAgainst (src, u.srcAfter);
    if (nextExists) checkAgainst (prev + 1, u.nextAfter);

    out.state = Why::ready;
    if (! out.agrees) { out.approximate = true; out.note = "Approximate: working it out again gave a different piece than the engine played."; }
    else if (! errorAgrees) { out.approximate = true; out.note = "Approximate: the winner's score differs from the engine's own."; }
    else if (! usedWasValid && ! isZero (s.novelty)) { out.approximate = true; out.note = "Approximate: the engine's \"used\" memory is still being read, and NOVELTY is on."; }
    else if (! m_usedValid && usedWasValid) { out.approximate = true; out.note = "Approximate: the \"used\" memory did not match the engine's after this pick."; }
    else if (cut) { out.approximate = true; out.note = "Approximate: more CONNECTIONS were searched than the screen keeps for a library this large."; }
    else if (d.clipped) { out.approximate = true; out.note = "Approximate: the library and the target hold different numbers of bands."; }
    else if (! m_rangesComplete) { out.approximate = true; out.note = "Approximate: the library has more sounds (over 512) than the screen follows."; }
    else if (stickyUnknown) { out.approximate = true; out.note = "Approximate: the piece the library was at is not known, so SMOOTH could not be checked."; }
    else out.note = "Exact: the engine's own arithmetic, on the engine's own numbers.";
}

// ------------------------------------------------------------------------------------ the scale
float ScoreScale::logUnit (double score, double low, double high) noexcept
{
    if (! (low > 0.0) || ! (high > low)) return 0.5f;
    if (! (score > low)) return 0.0f;
    if (! (score < high)) return 1.0f;
    return (float) ((std::log (score) - std::log (low)) / (std::log (high) - std::log (low)));
}

float ScoreScale::unit (double score) const noexcept { return m_seen > 0 ? logUnit (score, m_lo, m_hi) : 0.5f; }

void ScoreScale::feed (const Why& w, double)
{
    if (w.state != Why::ready || w.winner.piece < 0 || ! w.winner.finite) return;
    double lo = w.winner.total, hi = w.winner.total;
    for (int k = 0; k < w.runnerUps; ++k)
        if (w.runnerUp[k].finite) { lo = std::min (lo, w.runnerUp[k].total); hi = std::max (hi, w.runnerUp[k].total); }
    if (! (hi > 0.0)) return;                                    // (all exactly 0: nothing to scale)
    lo = std::max (lo, hi * 1.0e-6);
    hi = std::max (hi, lo * 4.0);
    if (m_seen == 0) { m_lo = lo; m_hi = hi; m_seen = 1; return; }
    // on a log scale: widen at once, narrow slowly (so the picture does not pump)
    const double a = std::log (m_lo), b = std::log (m_hi), x = std::log (lo), y = std::log (hi);
    m_lo = std::exp (x < a ? a + 0.5 * (x - a) : a + 0.02 * (x - a));
    m_hi = std::exp (y > b ? b + 0.5 * (y - b) : b + 0.02 * (y - b));
    if (m_hi < m_lo * 4.0) m_hi = m_lo * 4.0;
    if (m_seen < 1000000) ++m_seen;
}
}
