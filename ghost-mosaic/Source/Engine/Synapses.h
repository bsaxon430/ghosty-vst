// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
//
// Growing the network (brain::build_synapses_fixed) on every core. Upstream compares every
// piece with every other on one thread; each piece's list is independent of the others, so
// we hand pieces out to several threads and run upstream's exact steps for each: the same
// compare(), the same std::sort with the same comparator, the closest 1000 kept.
// tests/MosaicTests.cpp checks the lists are identical to build_synapses_fixed's.
#pragma once
#include "brain.h"
#include <atomic>
#include <thread>
#include <algorithm>

namespace mosaic
{
inline void buildSynapses (spiralcore::brain& b, spiralcore::search_params& params, std::atomic<float>& progress, int threads = 0)
{
    using namespace spiralcore;
    const u32 n = b.get_num_blocks();
    if (n < 2) { if (n == 1) {} return; }   // upstream: nothing to connect
    u32 num = 1000;                         // NUM_FIXED_SYNAPSES
    if (num >= n) num = n - 1;
    if (threads <= 0) threads = (int) juce::jlimit (1u, 8u, std::thread::hardware_concurrency() > 1 ? std::thread::hardware_concurrency() - 1 : 1u);
    std::atomic<u32> next { 0 }, done { 0 };
    auto work = [&]
    {
        std::vector<std::pair<u32, double>> collect;
        collect.reserve (n);
        for (;;)
        {
            const u32 outer = next.fetch_add (1);
            if (outer >= n) break;
            const block& i = b.get_block (outer);
            collect.clear();
            for (u32 index = 0; index < n; ++index)
                if (index != outer)
                    collect.push_back (std::pair<u32, double> (index, i.compare (b.get_block (index), params)));
            std::sort (collect.begin(), collect.end(),
                       [] (const std::pair<u32, double>& a, const std::pair<u32, double>& c) -> bool { return a.second < c.second; });
            auto& syn = const_cast<block&> (i).get_synapse();
            for (u32 k = 0; k < num; ++k) syn.push_back (collect[k].first);
            progress.store ((float) done.fetch_add (1) / (float) n);
        }
    };
    std::vector<std::thread> pool;
    for (int t = 1; t < threads; ++t) pool.emplace_back (work);
    work();
    for (auto& t : pool) t.join();
}
}
