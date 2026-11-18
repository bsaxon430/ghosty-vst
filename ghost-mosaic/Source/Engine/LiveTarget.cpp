// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
#include "LiveTarget.h"
#include "Compat.h"
#include <thread>

using namespace spiralcore;

namespace mosaic
{
static int64_t nextPow2 (int64_t v) { int64_t p = 1; while (p < v) p <<= 1; return p; }

LiveTarget::LiveTarget (u32 blockSize, u32 overlap, window::type w, int maxHostBlock)
{
    m_bs = juce::jmax<u32> (10, blockSize);
    m_ov = overlap >= m_bs ? 0 : overlap;            // as brain::chop_and_add
    m_hop = m_bs - m_ov;
    m_window.init (m_bs);
    m_window.set_current_type (w);
    m_latency = (int) m_bs + juce::jmax (64, maxHostBlock) + 1024;

    const int64_t ring = nextPow2 (juce::jmax<int64_t> ((int64_t) m_bs * 4 + m_latency * 2, (int64_t) (1 << 16)));
    m_ringMask = ring - 1;
    for (auto& r : m_ring) r.assign ((size_t) ring, 0.0f);

    // slots: enough to cover a few hundred pieces, but never more than ~8M samples of pieces
    m_numSlots = juce::jlimit<int64_t> (64, 1024, (int64_t) (8000000 / juce::jmax<u32> (1, m_bs)));
    compat::initFftThreads();
    spiralcore::sample silence (m_bs - 1);
    silence.zero();
    m_dummy = block (0, "silence", silence, 44100, m_window);
    for (auto& s : m_slots)
    {
        s.resize ((size_t) m_numSlots);
        for (auto& p : s) { p = std::make_unique<Slot>(); p->b = m_dummy; }
    }
    m_left.owner = this; m_left.ch = 0; m_left.set (m_bs, m_ov);
    m_right.owner = this; m_right.ch = 1; m_right.set (m_bs, m_ov);
    m_thread = std::make_unique<std::thread> ([this] { run(); });
}

LiveTarget::~LiveTarget()
{
    m_exit = true;
    if (m_thread && m_thread->joinable()) m_thread->join();
}

void LiveTarget::push (const float* l, const float* r, int n)
{
    int64_t w = m_written.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i)
    {
        const auto idx = (size_t) ((w + i) & m_ringMask);
        m_ring[0][idx] = l[i];
        m_ring[1][idx] = r[i];
    }
    m_written.store (w + n, std::memory_order_release);
}

void LiveTarget::run()
{
    compat::initFftThreads();
    int64_t next = 0;
    spiralcore::sample region[2];
    while (! m_exit.load())
    {
        const int64_t w = m_written.load (std::memory_order_acquire);
        bool did = false;
        while (! m_exit.load() && next * (int64_t) m_hop + (int64_t) m_bs <= w)
        {
            const int64_t start = next * (int64_t) m_hop;
            // a piece runs to start+bs-1 (exclusive), as sample::get_region(pos, pos+bs-1) in chop_and_add
            if (w - start > m_ringMask + 1 - (int64_t) m_bs) { next = (w - (int64_t) m_bs) / m_hop + 1; continue; }  // fell behind: skip ahead
            for (int ch = 0; ch < 2; ++ch)
            {
                region[ch].allocate (m_bs - 1);
                for (u32 i = 0; i < m_bs - 1; ++i) region[ch][i] = m_ring[ch][(size_t) ((start + i) & m_ringMask)];
                auto& slot = *m_slots[ch][(size_t) (next % m_numSlots)];
                slot.seq.store (-1, std::memory_order_release);
                slot.b = block (0, "input", region[ch], 44100, m_window);
                slot.seq.store (next, std::memory_order_release);
            }
            m_latest.store (next, std::memory_order_release);
            ++next;
            did = true;
        }
        if (! did) std::this_thread::sleep_for (std::chrono::microseconds (700));
    }
}

void LiveTarget::waitIdle (int timeoutMs) const
{
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    for (;;)
    {
        const int64_t w = written();
        const int64_t need = w < (int64_t) m_bs ? -1 : (w - (int64_t) m_bs) / (int64_t) m_hop;
        if (latestReady() >= need || juce::Time::getMillisecondCounter() > end) return;
        std::this_thread::sleep_for (std::chrono::microseconds (200));
    }
}

const block& LiveTarget::blockAt (int ch, int64_t absolute) const
{
    if (absolute < 0) return m_dummy;
    auto& slot = *m_slots[ch][(size_t) (absolute % m_numSlots)];
    if (slot.seq.load (std::memory_order_acquire) != absolute) return m_dummy;
    return slot.b;
}

const block& LiveTarget::View::get_block (u32 index) const
{
    return owner->blockAt (ch, owner->m_base + (int64_t) index);
}
}
