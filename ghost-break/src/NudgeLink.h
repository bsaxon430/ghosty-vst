// SPDX-License-Identifier: AGPL-3.0-or-later
// NUDGE LINK: Ghost Break tells any Phantom FX in the same project what each of its bays' NUDGE is, step by step, so Phantom FX's
// HAUNT EDIT (FOLLOW GHOST BREAK) can move other audio by the same amount. The same file is in both plugins (Ghost Break:
// ghost-break/src/NudgeLink.h, Phantom FX: src/rhythm/NudgeLink.h); keep them identical.
//
// Ghost Break writes, at every step it plays, for every bay: the step's host position (quarter notes) and how far NUDGE moves the
// break there (quarter notes, 0 .. the break's length). It also keeps a heartbeat and each bay's longest possible move. The two
// plugins are separate binaries, so they meet in a small named shared-memory block (POSIX shm; macOS and Linux). No locks: each
// entry is written under a sequence tag, and a reader takes an entry only when its tag reads the same before and after.
#pragma once
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#if defined(__APPLE__) || defined(__linux__)
 #include <fcntl.h>
 #include <sys/stat.h>
 #include <sys/mman.h>
 #include <unistd.h>
 #define GHOST_NUDGE_LINK 1
#else
 #define GHOST_NUDGE_LINK 0
#endif

namespace ghostlink
{
constexpr int kWriters = 4, kBays = 4, kRing = 256;
constexpr uint32_t kMagic = 0x47424e31;   // "GBN1"
constexpr int64_t kFreshMs = 1500;        // a writer that has not beaten for this long is gone

struct Entry { std::atomic<int64_t> tag { 0 }; std::atomic<double> ppq { 0 }, shiftQN { 0 }; };   // tag = step + 1 (0: empty, -1: being written)
struct Bay { std::atomic<double> maxShiftQN { 0 }; Entry ring[kRing]; };
struct Writer { std::atomic<uint64_t> owner { 0 }; std::atomic<int64_t> beatMs { 0 }; std::atomic<int32_t> playing { 0 }; std::atomic<double> mul { 1 }; Bay bay[kBays]; };
struct Shared { std::atomic<uint32_t> magic { 0 }; Writer w[kWriters]; };

inline int64_t nowMs() noexcept { return std::chrono::duration_cast<std::chrono::milliseconds> (std::chrono::steady_clock::now().time_since_epoch()).count(); }

// the process's mapping of the block (made on first use, kept for the life of the process); nullptr where there is none
inline Shared* shared() noexcept
{
#if GHOST_NUDGE_LINK
    static Shared* s = [] () -> Shared*
    {
        static_assert (std::atomic<int64_t>::is_always_lock_free && std::atomic<double>::is_always_lock_free, "the link needs lock-free atomics");
        const int fd = shm_open ("/ghostblock.nudge1", O_CREAT | O_RDWR, 0600);
        if (fd < 0) return nullptr;
        struct stat st {}; if (fstat (fd, &st) != 0) { close (fd); return nullptr; }
        if ((size_t) st.st_size < sizeof (Shared) && ftruncate (fd, (off_t) sizeof (Shared)) != 0) { close (fd); return nullptr; }
        void* p = mmap (nullptr, sizeof (Shared), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0); close (fd);
        if (p == MAP_FAILED) return nullptr;
        auto* sh = static_cast<Shared*> (p); uint32_t z = 0; sh->magic.compare_exchange_strong (z, kMagic);   // fresh memory is all zeros: a valid empty block
        return sh->magic.load() == kMagic ? sh : nullptr;
    }();
    return s;
#else
    return nullptr;
#endif
}

// ---------------------------------------------------------------- Ghost Break's side
class Sender
{
public:
    Sender() noexcept { token = (uint64_t) (uintptr_t) this ^ ((uint64_t) nowMs() << 20) ^ 0x9e3779b97f4a7c15ull; if (token == 0) token = 1; }
    ~Sender() { if (w != nullptr) { w->beatMs.store (0); w->owner.store (0); } }
    // every block (audio thread): claims a writer slot if it has none, beats, says whether the host is playing and the TEMPO multiplier
    void beat (bool hostPlaying, double mul) noexcept
    {
        if (w == nullptr && ! claim()) return;
        w->playing.store (hostPlaying ? 1 : 0, std::memory_order_relaxed); w->mul.store (mul, std::memory_order_relaxed);
        w->beatMs.store (nowMs(), std::memory_order_release);
    }
    void setMaxShift (int bay, double qn) noexcept { if (w != nullptr && bay >= 0 && bay < kBays) w->bay[bay].maxShiftQN.store (qn, std::memory_order_relaxed); }
    // the step `step` (Ghost Break's 16th count on its own grid) starts at host quarter note ppq; NUDGE moves bay `bay` by shiftQN there
    void step (int bay, long long step, double ppq, double shiftQN) noexcept
    {
        if (w == nullptr || bay < 0 || bay >= kBays) return;
        Entry& e = w->bay[bay].ring[(size_t) (((step % kRing) + kRing) % kRing)];
        e.tag.store (-1, std::memory_order_relaxed); std::atomic_thread_fence (std::memory_order_release);
        e.ppq.store (ppq, std::memory_order_relaxed); e.shiftQN.store (shiftQN, std::memory_order_relaxed);
        e.tag.store ((int64_t) step + 1, std::memory_order_release);
    }
    bool linked() const noexcept { return w != nullptr; }
private:
    bool claim() noexcept
    {
        Shared* s = shared(); if (s == nullptr) return false;
        const int64_t t = nowMs();
        for (auto& x : s->w)
        {
            uint64_t o = x.owner.load();
            // a free slot, or one whose owner has stopped beating (a crashed or closed host)
            if (o == 0 || t - x.beatMs.load() > kFreshMs * 4)
                if (x.owner.compare_exchange_strong (o, token)) { for (auto& b : x.bay) for (auto& e : b.ring) e.tag.store (0); w = &x; return true; }
        }
        return false;
    }
    Writer* w = nullptr; uint64_t token = 1;
};

// ---------------------------------------------------------------- Phantom FX's side
class Receiver
{
public:
    // the Ghost Break being followed: the first one that is beating (nullptr: none)
    const Writer* source() const noexcept
    {
        Shared* s = shared(); if (s == nullptr) return nullptr;
        const int64_t t = nowMs();
        for (auto& x : s->w) if (x.owner.load (std::memory_order_relaxed) != 0 && t - x.beatMs.load (std::memory_order_acquire) <= kFreshMs) return &x;
        return nullptr;
    }
    // the longest move bay `bay` can make, in quarter notes (0: no Ghost Break)
    double maxShift (int bay) const noexcept { const Writer* w = source(); return w != nullptr && bay >= 0 && bay < kBays ? w->bay[bay].maxShiftQN.load (std::memory_order_relaxed) : 0.0; }
    // NUDGE's move for bay `bay` at host quarter note ppq: the entry of the step that started last at or before ppq. false: none known
    bool shiftAt (int bay, double ppq, double& shiftQN) const noexcept { return shiftAt (source(), bay, ppq, shiftQN); }
    // the same from a source already found (once a block: the audio thread asks per sample)
    static bool shiftAt (const Writer* w, int bay, double ppq, double& shiftQN) noexcept
    {
        if (w == nullptr || bay < 0 || bay >= kBays) return false;
        const double mul = w->mul.load (std::memory_order_relaxed); if (! (mul > 0)) return false;
        const long long s0 = (long long) std::floor (ppq * mul * 4.0 + 1.0e-9);
        for (long long s = s0; s >= s0 - 2; --s)   // swing can start a step up to an 8th late: look back two
        {
            const Entry& e = w->bay[bay].ring[(size_t) (((s % kRing) + kRing) % kRing)];
            const int64_t t1 = e.tag.load (std::memory_order_acquire); if (t1 != s + 1) continue;
            const double p = e.ppq.load (std::memory_order_relaxed), q = e.shiftQN.load (std::memory_order_relaxed);
            std::atomic_thread_fence (std::memory_order_acquire);
            if (e.tag.load (std::memory_order_relaxed) != t1) continue;
            if (p <= ppq + 1.0e-9) { shiftQN = q; return true; }
        }
        return false;
    }
};
} // namespace ghostlink
