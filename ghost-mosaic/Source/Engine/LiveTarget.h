// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
//
// LiveTarget: the effect's live audio input as the target. (Upstream's block_stream did this
// with busy-waiting worker threads and was switched off in the Qt app: "use mic input" sent
// /mic, which the audio thread ignored.) Here:
//  - the audio thread only copies input into a ring (no locks, no allocation);
//  - one analysis thread cuts the ring into target pieces exactly like brain::chop_and_add
//    (same length, same window, same `block` analysis) and publishes them into fixed slots;
//  - the renderer reads the slots through a block_source view (left / right).
// The renderer plays piece k at output time k*hop, so input reaches the output exactly
// `latencySamples()` later (piece size + the largest host buffer + a safety margin).
#pragma once
#include "block.h"
#include "block_source.h"
#include "window.h"
#include <juce_core/juce_core.h>
#include <atomic>
#include <vector>

namespace mosaic
{
using ::u32;

class LiveTarget
{
public:
    LiveTarget (u32 blockSize, u32 overlap, spiralcore::window::type w, int maxHostBlock);
    ~LiveTarget();

    // audio thread
    void push (const float* left, const float* right, int n);   // right may be == left
    int64_t written() const { return m_written.load (std::memory_order_acquire); }
    int64_t latestReady() const { return m_latest.load (std::memory_order_acquire); }  // -1 = none yet
    void setBase (int64_t b) { m_base = b; }                    // absolute piece of renderer piece 0

    u32 hop() const { return m_hop; }
    int latencySamples() const { return m_latency; }
    const spiralcore::block_source& left() const { return m_left; }
    const spiralcore::block_source& right() const { return m_right; }

    int64_t lateCount() const { return m_late.load(); }
    void noteLate() { m_late.fetch_add (1); }

    // tests: wait until everything pushed so far has been analysed
    void waitIdle (int timeoutMs) const;

private:
    struct View : spiralcore::block_source
    {
        const LiveTarget* owner = nullptr; int ch = 0;
        const spiralcore::block& get_block (u32 index) const override;
        u32 get_num_blocks() const override { return UINT_MAX; }
        void set (u32 bs, u32 ov) { m_block_size = bs; m_overlap = ov; }
    };
    struct Slot { std::atomic<int64_t> seq { -1 }; spiralcore::block b; };

    void run();
    const spiralcore::block& blockAt (int ch, int64_t absolute) const;

    u32 m_bs, m_ov, m_hop;
    spiralcore::window m_window;
    int m_latency = 0;
    std::vector<float> m_ring[2];
    int64_t m_ringMask = 0;
    std::atomic<int64_t> m_written { 0 }, m_latest { -1 }, m_late { 0 };
    int64_t m_base = 0;
    std::vector<std::unique_ptr<Slot>> m_slots[2];
    int64_t m_numSlots = 0;
    spiralcore::block m_dummy;
    std::atomic<bool> m_exit { false };
    std::unique_ptr<std::thread> m_thread;
    View m_left, m_right;
};
}
