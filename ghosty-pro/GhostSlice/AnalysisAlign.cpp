#include "AnalysisAlign.h"
#include "PitchEdit.h"
#include "Chords.h"
#include <map>
#include <set>

namespace gslice::align
{
namespace
{
constexpr double kMinSegPF = 32.0;   // as Model.cpp: the shortest warp segment
constexpr int64 kMinSegSF = 32;
double kRateOf (const Project& p) { return p.sampleRate / juce::jmax (1.0, p.source.sampleRate); }   // normalised rate = ds/dt * this
}

const char* modeName (Mode m) { return m == Mode::Groove ? "GROOVE" : m == Mode::Manual ? "MANUAL" : "TIGHT"; }
const char* statusName (Status s)
{
    switch (s)
    {
        case Status::Move: return "move"; case Status::Follow: return "follows its word"; case Status::OnGrid: return "on the grid";
        case Status::Weak: return "weak"; case Status::Breath: return "breath"; case Status::Far: return "far from the grid";
        case Status::Fixed: return "at a user marker / anchor"; case Status::RateLimit: return "would stretch beyond 0.5x - 2x";
        case Status::Manual: return "MANUAL"; case Status::Skipped: return "reversed region";
    }
    return "";
}

juce::String Proposals::summary() const
{
    juce::String s; s << moving << " move";
    if (skipped > 0) s << ", " << skipped << " untouched";
    if (refused > 0) s << ", " << refused << " refused (amber)";
    return s;
}

double alignStep (const Project& p)
{
    const double g = p.gridStep();
    return g > 0 ? g : p.secondsPerBeat() * p.sampleRate / 4.0;
}

Project withoutAuto (const Project& in)
{
    Project p = in;
    for (auto& r : p.regions)
    {
        std::vector<WarpMarker> keep;
        for (size_t i = 0; i < r.warp.size(); ++i)
        {
            const bool inner = i > 0 && i + 1 < r.warp.size();
            if (inner && r.warp[i].isAuto() && ! r.warp[i].anchor) continue;
            keep.push_back (r.warp[i]);
        }
        r.warp = std::move (keep);
    }
    return p;
}

int countAuto (const Project& p) { int n = 0; for (auto& r : p.regions) for (auto& w : r.warp) if (w.isAuto()) ++n; return n; }

static double absOf (const Region& r, double localT) { return r.dstStart + (r.reverse ? r.length() - localT : localT); }

Groove extractGroove (const Project& in, const std::vector<analysis::Event>& events, double minConf)
{
    const Project p = withoutAuto (in);
    Groove g;
    const double step = alignStep (p), bar = p.secondsPerBeat() * p.sampleRate * 4.0;
    g.slots = juce::jmax (1, (int) std::lround (bar / step));
    std::vector<double> sum ((size_t) g.slots, 0.0), wsum ((size_t) g.slots, 0.0);
    g.count.assign ((size_t) g.slots, 0);
    for (auto& e : events)
    {
        if (e.strength < minConf || (e.flags & analysis::kBreath) != 0) continue;
        for (auto& r : p.regions)
        {
            if (r.reverse || r.muted || e.pos <= r.srcIn() || e.pos >= r.srcOut()) continue;
            const double t = absOf (r, r.map().outAt ((double) e.pos));
            const double k = std::round ((t - p.beatOffset) / step);
            const int slot = (int) ((((int64) k) % g.slots + g.slots) % g.slots);
            const double dev = (t - (p.beatOffset + k * step)) / step;
            sum[(size_t) slot] += dev * e.strength; wsum[(size_t) slot] += e.strength; g.count[(size_t) slot]++;
            break;
        }
    }
    g.dev.assign ((size_t) g.slots, 0.0);
    double odd = 0, even = 0; int no = 0, ne = 0;
    for (int i = 0; i < g.slots; ++i)
    {
        if (g.count[(size_t) i] >= 2 && wsum[(size_t) i] > 0) g.dev[(size_t) i] = sum[(size_t) i] / wsum[(size_t) i];
        if (g.count[(size_t) i] >= 2) { if (i % 2) { odd += g.dev[(size_t) i]; ++no; } else { even += g.dev[(size_t) i]; ++ne; } }
    }
    g.swing = (no > 0 ? odd / no : 0.0) - (ne > 0 ? even / ne : 0.0);
    return g;
}

static double targetOf (const Project& p, double from, double step, const Settings& s, const Groove& g)
{
    const double k = std::round ((from - p.beatOffset) / step);
    double t = p.beatOffset + k * step;
    if (s.mode == Mode::Groove && ! g.dev.empty())
    {
        const int slot = (int) ((((int64) k) % g.slots + g.slots) % g.slots);
        t += juce::jlimit (0.0, 1.0, s.grooveFeel) * g.dev[(size_t) slot] * step;
    }
    return t;
}

Proposals propose (const Project& in, const std::vector<analysis::Event>& events, const Settings& s)
{
    Proposals pr; pr.settings = s;
    const Project p = withoutAuto (in);
    const double step = alignStep (p), kRate = kRateOf (p);
    if (s.mode == Mode::Groove) pr.groove = extractGroove (p, events);
    std::vector<analysis::Event> ev = events;
    std::sort (ev.begin(), ev.end(), [] (const analysis::Event& a, const analysis::Event& b) { return a.pos < b.pos; });
    for (auto& r : p.regions)
    {
        if (r.muted) continue;
        const auto m = r.map();
        const size_t first = pr.items.size();
        for (auto& e : ev)
        {
            if (e.pos <= r.srcIn() || e.pos >= r.srcOut()) continue;
            Proposal it; it.regionId = r.id; it.src = e.pos; it.strength = e.strength; it.group = e.group;
            const double lt = m.outAt ((double) e.pos);
            it.from = it.to = absOf (r, lt);
            if (r.reverse) it.status = Status::Skipped;
            else if (s.mode == Mode::Manual) it.status = Status::Manual;
            else
            {
                bool nearFixed = false;   // region edges, USER markers, anchors: fixed points of the map
                for (auto& w : r.warp) if (std::abs ((double) (w.s - e.pos)) < 4.0 * kMinSegSF || std::abs (w.t - lt) < 4.0 * kMinSegPF) nearFixed = true;
                if (nearFixed) it.status = Status::Fixed;
                else if (e.strength < s.minConfidence) it.status = Status::Weak;
                else if ((e.flags & analysis::kBreath) != 0 && ! s.includeBreaths) it.status = Status::Breath;
                else it.status = Status::Move;   // (tentative: the group / grid / rate rules below decide)
            }
            pr.items.push_back (it);
        }
        if (r.reverse || s.mode == Mode::Manual) continue;
        // groups: the lead (first eligible event) gets the target, the rest of the group moves rigidly with it
        const size_t last = pr.items.size();
        auto keyOf = [&] (size_t i) { return pr.items[i].group >= 0 ? pr.items[i].group : -(int) (i + 2); };
        std::map<int, std::vector<size_t>> groups;
        for (size_t i = first; i < last; ++i) groups[keyOf (i)].push_back (i);
        std::map<int, double> gd;   // the group's offset (PF)
        for (auto& [key, mem] : groups)
        {
            bool blocked = false; for (auto i : mem) if (pr.items[i].status == Status::Fixed) blocked = true;
            size_t lead = last; for (auto i : mem) if (pr.items[i].status == Status::Move) { lead = i; break; }
            if (lead == last) continue;
            auto& L = pr.items[lead];
            if (blocked) { for (auto i : mem) if (pr.items[i].status == Status::Move) pr.items[i].status = Status::Fixed; continue; }
            const double dist = targetOf (p, L.from, step, s, pr.groove) - L.from;
            Status st = Status::Move; double d = 0;
            if (std::abs (dist) > s.farFraction * step) st = Status::Far;
            else { d = juce::jlimit (0.0, 1.0, s.strength) * dist; if (std::abs (d) < 0.5) { st = Status::OnGrid; d = 0; } }
            for (auto i : mem)
            {
                auto& it = pr.items[i];
                if (st == Status::Move) it.status = i == lead ? Status::Move : Status::Follow;
                else if (i == lead || it.status == Status::Move) it.status = st;
            }
            if (st == Status::Move) gd[key] = d;
        }
        // rate limits: shrink the offending corrections by halves (8 passes), then refuse them (amber)
        for (int pass = 0; pass < 12; ++pass)
        {
            struct Pt { double t; int64 s; int key; bool movable; };
            std::vector<Pt> pts;
            for (auto& w : r.warp) pts.push_back ({ w.t, w.s, 0, false });
            for (size_t i = first; i < last; ++i)
                if (pr.items[i].moves() && gd.count (keyOf (i))) pts.push_back ({ pr.items[i].from - r.dstStart + gd[keyOf (i)], pr.items[i].src, keyOf (i), true });
            std::sort (pts.begin(), pts.end(), [] (const Pt& a, const Pt& b) { return a.s < b.s; });
            std::set<int> bad;
            for (size_t k = 0; k + 1 < pts.size(); ++k)
            {
                const auto& a = pts[k]; const auto& b = pts[k + 1];
                if (! a.movable && ! b.movable) continue;
                const double dt = b.t - a.t, rate = dt > 0 ? (double) (b.s - a.s) / dt * kRate : 1e30;
                if (dt < kMinSegPF || rate < s.rateMin - 1e-9 || rate > s.rateMax + 1e-9) { if (a.movable) bad.insert (a.key); if (b.movable) bad.insert (b.key); }
            }
            if (bad.empty()) break;
            for (int key : bad)
            {
                if (pass < 8) { gd[key] *= 0.5; continue; }
                gd.erase (key);
                for (auto i : groups[key]) if (pr.items[i].moves()) pr.items[i].status = Status::RateLimit;
            }
        }
        for (size_t i = first; i < last; ++i)
        {
            auto& it = pr.items[i];
            if (it.moves() && gd.count (keyOf (i))) it.to = it.from + gd[keyOf (i)];
            else if (it.moves()) it.status = Status::RateLimit;
        }
    }
    for (auto& it : pr.items) { if (it.moves()) ++pr.moving; else if (it.amber()) ++pr.refused; else ++pr.skipped; }
    return pr;
}

std::optional<Project> apply (const Project& in, const Proposals& pr, juce::String* why)
{
    if (pr.settings.mode == Mode::Manual) { if (why) *why = "MANUAL: no automatic moves (drag the markers yourself)"; return std::nullopt; }
    Project q = withoutAuto (in);
    const bool removed = ! (q == in);
    int added = 0;
    const double kRate = kRateOf (q);
    for (auto& r : q.regions)
    {
        if (r.reverse) continue;
        std::vector<WarpMarker> nm;
        for (auto& it : pr.items)
            if (it.regionId == r.id && it.moves())
                nm.push_back ({ 0, it.to - r.dstStart, it.src, false, MarkerKind::Auto });
        if (nm.empty()) continue;
        for (auto& w : nm) w.id = q.nextId++;
        r.warp.insert (r.warp.end(), nm.begin(), nm.end());
        std::sort (r.warp.begin(), r.warp.end(), [] (const WarpMarker& a, const WarpMarker& b) { return a.s < b.s; });
        if (! r.valid()) { if (why) *why = "the aligned markers would cross (try a lower STRENGTH)"; return std::nullopt; }
        for (size_t i = 0; i + 1 < r.warp.size(); ++i)
        {
            const double rt = (double) (r.warp[i + 1].s - r.warp[i].s) / (r.warp[i + 1].t - r.warp[i].t) * kRate;
            if (rt < limits::kHardMin - 1e-9 || rt > limits::kHardMax + 1e-9) { if (why) *why = "a segment would stretch beyond 0.25x - 4x"; return std::nullopt; }
        }
        added += (int) nm.size();
    }
    if (added == 0 && ! removed) { if (why) *why = "nothing to align (" + pr.summary() + ")"; return std::nullopt; }
    if (! ops::anchorsKept (in, q)) { if (why) *why = "ANCHOR LOCK"; return std::nullopt; }
    return q;
}
} // namespace gslice::align

namespace gslice::keyops
{
std::optional<Project> matchProjectKey (const Project& p, const analysis::Key& detected, juce::String* why, int* shiftOut)
{
    bool srcMinor = false, dstMinor = false;
    int src = analysis::parseKey (p.key, srcMinor);
    if (src < 0)
    {
        if (! detected.valid()) { if (why) *why = "No key detected (and none set): set the SOURCE key"; return std::nullopt; }
        if (detected.confidence < kMinConfidence)
        { if (why) *why = "The detected key (" + detected.readout() + ") is not certain enough: set the SOURCE key by hand"; return std::nullopt; }
        src = detected.tonic; srcMinor = detected.minor;
    }
    const int dst = analysis::parseKey (p.projectKey, dstMinor);
    if (dst < 0) { if (why) *why = "Set the PROJECT key first"; return std::nullopt; }
    const int shift = analysis::keyShift (src, srcMinor, dst, dstMinor);
    if (shiftOut) *shiftOut = shift;
    Project q = p; q.pitchSemis = shift; q.pitchCents = 0;
    if (q == p) { if (why) *why = "Already in the project key (" + p.projectKey + ")"; return std::nullopt; }
    return q;
}

const char* modeName (Mode m)
{
    static const char* n[kModes] { "CHROMATIC", "SCALE SNAP", "MELODY PRESERVE", "MANUAL", "USE CHORDS" };
    return n[juce::jlimit (0, kModes - 1, (int) m)];
}

bool chordShift (const analysis::Result* a, int dst, bool dstMinor, int& shift, double& fit, juce::String* why)
{
    const auto* ct = a != nullptr ? a->chords.get() : nullptr;
    double total = 0;
    if (ct != nullptr) for (auto& c : ct->chords) if (c.isChord()) total += (double) (c.s1 - c.s0) * c.confidence;
    if (ct == nullptr || total <= 0 || ct->chordShare() < 0.05) { if (why) *why = "No chords detected (USE CHORDS needs harmony: chords, pads, keys)"; return false; }
    double best = -1; int bs = 0;
    for (int s : { 0, 1, -1, 2, -2, 3, -3, 4, -4, 5, -5, -6 })   // (smallest shift first: a tie keeps it)
    {
        double sc = 0;
        for (auto& c : ct->chords)
            if (c.isChord()) sc += (double) (c.s1 - c.s0) * c.confidence * analysis::diatonicFit ((c.root + s + 12) % 12, c.quality, dst, dstMinor);
        if (sc > best + 1e-9 * total) { best = sc; bs = s; }
    }
    shift = bs; fit = best / total;
    return true;
}

bool sourceKey (const Project& p, const analysis::Key& detected, int& tonic, bool& minor, juce::String* why)
{
    tonic = analysis::parseKey (p.key, minor);
    if (tonic >= 0) return true;
    if (! detected.valid()) { if (why) *why = "No key detected (and none set): set the SOURCE key"; return false; }
    if (detected.confidence < kMinConfidence) { if (why) *why = "The detected key (" + detected.readout() + ") is not certain enough: set the SOURCE key by hand"; return false; }
    tonic = detected.tonic; minor = detected.minor; return true;
}

std::optional<Project> matchKey (const Project& p, const analysis::Result* a, juce::String* why, int* shiftOut, int* snappedOut)
{
    const analysis::Key det = a != nullptr ? a->key : analysis::Key();
    const auto mode = (Mode) juce::jlimit (0, kModes - 1, p.keyMode);
    if (mode == Mode::Chromatic) return matchProjectKey (p, det, why, shiftOut);
    if (mode == Mode::Manual) { if (why) *why = "KEY MODE MANUAL: drag the note blobs yourself (or pick another KEY MODE)"; return std::nullopt; }
    bool dstMinor = false;
    const int dst = analysis::parseKey (p.projectKey, dstMinor);
    if (dst < 0) { if (why) *why = "Set the PROJECT key first"; return std::nullopt; }
    if (mode == Mode::UseChords)
    {   // the chord roots' relation to the project key decides (not the notes key)
        int shift = 0; double fit = 0;
        if (! chordShift (a, dst, dstMinor, shift, fit, why)) return std::nullopt;
        if (shiftOut) *shiftOut = shift;
        Project q = p; q.pitchSemis = shift; q.pitchCents = 0;
        if (q == p) { if (why) *why = "The chords already sit in " + p.projectKey + " (" + juce::String (juce::roundToInt (fit * 100)) + " % on its chords)"; return std::nullopt; }
        return q;
    }
    if (mode == Mode::ScaleSnap)
    {
        if (a == nullptr || a->notes.empty()) { if (why) *why = "No notes detected to snap"; return std::nullopt; }
        Project q = p; q.pitchSemis = 0; q.pitchCents = 0;   // the snap replaces a global shift (the notes land on the scale as heard)
        pitchedit::Scale sc; sc.chromatic = false; sc.tonic = dst; sc.minor = dstMinor;
        int n = 0;
        auto s = ops::snapNotes (q, pitchedit::notesOf (q, a), sc, 0.5f, &n);
        if (snappedOut) *snappedOut = n;
        if (s.has_value()) q = *s;
        if (q == p) { if (why) *why = "Every note is already on " + p.projectKey; return std::nullopt; }
        return q;
    }
    int src = 0; bool srcMinor = false;
    if (! sourceKey (p, det, src, srcMinor, why)) return std::nullopt;
    int shift = ((dst - src) % 12 + 12) % 12; if (shift > 5) shift -= 12;   // tonic to tonic: the melody keeps its intervals (and its mode)
    if (shiftOut) *shiftOut = shift;
    Project q = p; q.pitchSemis = shift; q.pitchCents = 0;
    if (q == p) { if (why) *why = "Already on the project tonic (" + p.projectKey + ")"; return std::nullopt; }
    return q;
}
}
