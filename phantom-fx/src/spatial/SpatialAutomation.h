// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX SPATIAL "memory" (SPATIAL.md 4b/4c): every tile can record its movement against the host's playhead (bars / beats) and
// play it back whenever the song reaches that part.
//   READ  : tiles follow their recorded paths (where a path exists; elsewhere they sit where their parameters put them).
//   TOUCH : armed tiles record only while you hold them; let go and they read again. Re-recording replaces only what you touched.
//   WRITE : armed tiles record the whole time the song plays.
//   OFF   : paths are kept but ignored.
// The audio thread records into a lock-free FIFO; the message thread merges it into the paths (60 Hz). The audio thread reads the paths
// under a try-lock (a missed lock keeps the last position for one block). Paths are saved in the project; a new instance has none.
#pragma once
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include "../effects/Catalog.h"
#include <array>
#include <atomic>
#include <vector>
#include <algorithm>

namespace phantom
{
struct SpPoint { double ppq = 0; float az = 0, el = 0, dist = 0; bool segStart = false; };
enum SpatialAutoMode { spOff = 0, spRead = 1, spTouch = 2, spWrite = 3 };
inline const char* spatialModeName (int m) { static const char* n[] { "OFF", "READ", "TOUCH", "WRITE" }; return n[juce::jlimit (0, 3, m)]; }
inline double spatialQuantBeats (int q) { static const double b[] { 0.0, 0.25, 0.5, 1.0 }; return b[juce::jlimit (0, 3, q)]; }   // OFF, 1/16, 1/8, 1/4
inline const char* spatialQuantName (int q) { static const char* n[] { "OFF", "1/16", "1/8", "1/4" }; return n[juce::jlimit (0, 3, q)]; }
inline int spatialLoopBars (int l) { static const int b[] { 0, 1, 2, 4 }; return b[juce::jlimit (0, 3, l)]; }
inline const char* spatialLoopName (int l) { static const char* n[] { "OFF", "1 BAR", "2 BARS", "4 BARS" }; return n[juce::jlimit (0, 3, l)]; }

class SpatialAutomation
{
public:
    SpatialAutomation() { for (auto& a : armed) a = false; for (auto& t : touching) t = false; for (auto& q : quant) q = 0; for (auto& l : loop) l = 0; for (auto& p : passLast) p = -1.0; }

    std::atomic<int> mode { spRead };
    std::array<std::atomic<bool>, kNumUnits> armed, touching;
    std::array<std::atomic<int>, kNumUnits> quant, loop;
    std::atomic<int> version { 0 };      // bumps whenever a path changes (the UI redraws its trails)
    juce::SpinLock lock;                 // guards paths

    // ---- audio thread
    void push (int u, double ppq, float az, float el, float dist) noexcept { Rec r { u, ppq, az, el, dist, false }; write (r); }
    void pushEnd (int u) noexcept { Rec r { u, -1.0, 0, 0, 0, true }; write (r); }
    // where unit u's path puts it at song position ppq (call with the lock held). false = no path there.
    bool lookup (int u, double ppq, double beatsPerBar, SpPoint& out) const noexcept
    {
        const auto& v = paths[(size_t) u]; if (v.empty()) return false;
        double q = ppq;
        if (const int lb = spatialLoopBars (loop[(size_t) u].load (std::memory_order_relaxed)); lb > 0)
        {
            const double bar = juce::jmax (0.25, beatsPerBar), start = std::floor (v.front().ppq / bar + 1.0e-9) * bar, len = lb * bar;
            if (q >= start) q = start + std::fmod (q - start, len);
        }
        if (const double g = spatialQuantBeats (quant[(size_t) u].load (std::memory_order_relaxed)); g > 0.0) q = std::floor (q / g + 1.0e-9) * g;
        auto it = std::upper_bound (v.begin(), v.end(), q, [] (double x, const SpPoint& p) { return x < p.ppq; });
        if (it == v.begin()) return false;
        const auto& a = *(it - 1);
        if (it == v.end() || it->segStart) { if (q - a.ppq <= kHold) { out = a; return true; } return false; }
        const auto& b = *it; const double span = b.ppq - a.ppq;
        const float f = span > 0.0 ? (float) ((q - a.ppq) / span) : 1.0f;
        float daz = b.az - a.az; if (daz > 180.0f) daz -= 360.0f; else if (daz < -180.0f) daz += 360.0f;   // the short way round
        float az = a.az + daz * f; if (az > 180.0f) az -= 360.0f; else if (az < -180.0f) az += 360.0f;
        out = { q, az, a.el + (b.el - a.el) * f, a.dist + (b.dist - a.dist) * f, false };
        return true;
    }

    // ---- message thread
    void drain()
    {
        int n = fifo.getNumReady(); if (n == 0) return;
        const juce::SpinLock::ScopedLockType sl (lock);
        auto rd = fifo.read (n);
        auto apply = [this] (const Rec& r)
        {
            auto& v = paths[(size_t) r.u]; auto& last = passLast[(size_t) r.u];
            if (r.end) { last = -1.0; return; }
            const bool fresh = last < 0.0 || r.ppq < last || r.ppq - last > kNewPassGap;
            const double lo = fresh ? r.ppq - 1.0e-9 : last;
            // replace what was there between the previous point of this pass and this one (touch replaces only what it covers)
            auto b = std::upper_bound (v.begin(), v.end(), lo, [] (double x, const SpPoint& p) { return x < p.ppq; });
            auto e = std::upper_bound (b, v.end(), r.ppq, [] (double x, const SpPoint& p) { return x < p.ppq; });
            auto at = v.erase (b, e);
            v.insert (at, SpPoint { r.ppq, r.az, r.el, r.dist, fresh });
            last = r.ppq;
        };
        for (int i = 0; i < rd.blockSize1; ++i) apply (buf[(size_t) (rd.startIndex1 + i)]);
        for (int i = 0; i < rd.blockSize2; ++i) apply (buf[(size_t) (rd.startIndex2 + i)]);
        ++version;
    }
    void clear (int u) { const juce::SpinLock::ScopedLockType sl (lock); paths[(size_t) u].clear(); passLast[(size_t) u] = -1.0; ++version; }
    void clearAll() { const juce::SpinLock::ScopedLockType sl (lock); for (auto& p : paths) p.clear(); for (auto& p : passLast) p = -1.0; ++version; }
    // clear [fromPpq, toPpq) of one unit (u >= 0) or of all of them (u < 0)
    void clearSection (int u, double fromPpq, double toPpq)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        for (int k = 0; k < kNumUnits; ++k)
        {
            if (u >= 0 && k != u) continue;
            auto& v = paths[(size_t) k];
            auto b = std::lower_bound (v.begin(), v.end(), fromPpq, [] (const SpPoint& p, double x) { return p.ppq < x; });
            auto e = std::lower_bound (b, v.end(), toPpq, [] (const SpPoint& p, double x) { return p.ppq < x; });
            auto at = v.erase (b, e); if (at != v.end()) at->segStart = true;
        }
        ++version;
    }
    std::vector<SpPoint> copyPath (int u) const { const juce::SpinLock::ScopedLockType sl (lock); return paths[(size_t) u]; }
    bool hasPath (int u) const { const juce::SpinLock::ScopedLockType sl (lock); return ! paths[(size_t) u].empty(); }
    void setPath (int u, std::vector<SpPoint> v) { const juce::SpinLock::ScopedLockType sl (lock); paths[(size_t) u] = std::move (v); ++version; }

    // ---- state: one child tree, deterministic bytes (the round-trip check compares saved states byte for byte)
    juce::ValueTree toTree() const
    {
        juce::ValueTree t ("SPATIAL");
        t.setProperty ("mode", mode.load(), nullptr);
        const juce::SpinLock::ScopedLockType sl (lock);
        for (int u = 0; u < kNumUnits; ++u)
        {
            const auto& v = paths[(size_t) u];
            const int a = armed[(size_t) u] ? 1 : 0, q = quant[(size_t) u], l = loop[(size_t) u];
            if (v.empty() && a == 0 && q == 0 && l == 0) continue;
            juce::ValueTree c ("TILE"); c.setProperty ("unit", juce::String (unitDef (u).key), nullptr);
            c.setProperty ("arm", a, nullptr); c.setProperty ("quant", q, nullptr); c.setProperty ("loop", l, nullptr);
            if (! v.empty())
            {
                juce::MemoryOutputStream os;
                for (auto& p : v) { os.writeDouble (p.ppq); os.writeFloat (p.az); os.writeFloat (p.el); os.writeFloat (p.dist); os.writeByte (p.segStart ? 1 : 0); }
                c.setProperty ("path", os.getMemoryBlock().toBase64Encoding(), nullptr);
            }
            t.appendChild (c, nullptr);
        }
        return t;
    }
    void fromTree (const juce::ValueTree& t)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        for (auto& p : paths) p.clear();
        for (int u = 0; u < kNumUnits; ++u) { armed[(size_t) u] = false; quant[(size_t) u] = 0; loop[(size_t) u] = 0; passLast[(size_t) u] = -1.0; touching[(size_t) u] = false; }
        mode = t.isValid() ? juce::jlimit (0, 3, (int) t.getProperty ("mode", (int) spRead)) : (int) spRead;
        for (const auto& c : t)
        {
            int u = -1; const auto key = c["unit"].toString(); for (int k = 0; k < kNumUnits; ++k) if (key == unitDef (k).key) u = k;
            if (u < 0) continue;
            armed[(size_t) u] = (int) c["arm"] != 0; quant[(size_t) u] = juce::jlimit (0, 3, (int) c["quant"]); loop[(size_t) u] = juce::jlimit (0, 3, (int) c["loop"]);
            juce::MemoryBlock mb; if (! mb.fromBase64Encoding (c["path"].toString())) continue;
            juce::MemoryInputStream is (mb, false); auto& v = paths[(size_t) u];
            while (is.getNumBytesRemaining() >= 21) { SpPoint p; p.ppq = is.readDouble(); p.az = is.readFloat(); p.el = is.readFloat(); p.dist = is.readFloat(); p.segStart = is.readByte() != 0; v.push_back (p); }
            std::stable_sort (v.begin(), v.end(), [] (const SpPoint& a, const SpPoint& b) { return a.ppq < b.ppq; });
        }
        ++version;
    }

private:
    struct Rec { int u; double ppq; float az, el, dist; bool end; };
    static constexpr int kCap = 4096;
    static constexpr double kNewPassGap = 1.0;    // beats: a jump bigger than this (a stop / a locate) starts a new recorded segment
    static constexpr double kHold = 0.0625;       // beats: a segment's last point holds this long (one block or so) before the tile is free again
    void write (const Rec& r) noexcept { auto w = fifo.write (1); if (w.blockSize1 > 0) buf[(size_t) w.startIndex1] = r; else if (w.blockSize2 > 0) buf[(size_t) w.startIndex2] = r; }
    juce::AbstractFifo fifo { kCap }; std::array<Rec, kCap> buf {};
    std::array<std::vector<SpPoint>, kNumUnits> paths;
    std::array<double, kNumUnits> passLast {};
};
} // namespace phantom
