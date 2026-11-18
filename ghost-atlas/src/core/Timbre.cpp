// SPDX-License-Identifier: AGPL-3.0-or-later
// GHOST ATLAS - timbre with Samplebrain's own classes (see Timbre.h). Its analysis is used here; its mosaic (the brain
// and the renderer) in Reimagine.cpp.
#include "Timbre.h"

#include "block.h"
#include "fft.h"
#include "mfcc.h"
#include "window.h"
#include "search_params.h"
#include <complex>
#include <fstream>
#include <mutex>

namespace atlas
{
std::mutex& samplebrainLock() { static std::mutex m; return m; }

namespace
{
constexpr int kMfccFilters = 12;   // block.cpp's MFCC_FILTERS
}

juce::File Timbre::blockFile (const juce::String& instrumentId)
{
    const auto name = juce::String::toHexString (instrumentId.hashCode64()) + ".blocks";
    return atlasRoot().getChildFile ("Cache").getChildFile ("timbre").getChildFile (name);
}

bool Timbre::build (const std::vector<float>& mono, double fs, const juce::File& file, TimbreSummary& out)
{
    using namespace spiralcore;
    out = TimbreSummary();
    const int n = (int) mono.size();
    if (n < kBlockSize + 64) return false;
    // six places along the held note: the very start, then through the body
    const double at[kBlocks] = { 0.02, 0.25, 0.6, 1.2, 1.8, 2.4 };
    std::lock_guard<std::mutex> guard (samplebrainLock());

    window w;
    w.init ((u32) kBlockSize);
    w.set_current_type (window::HANN);
    const u32 rate = (u32) std::lround (fs);

    file.getParentDirectory().createDirectory();
    std::ofstream of (file.getFullPathName().toRawUTF8(), std::ios::binary);
    if (! of.good()) return false;
    u32 count = 0;
    std::vector<int> starts;
    for (int k = 0; k < kBlocks; ++k)
    {
        int s = (int) (at[k] * fs);
        if (s + kBlockSize > n) s = n - kBlockSize;
        if (s < 0) break;
        starts.push_back (s);
    }
    count = (u32) starts.size();
    of || count;

    // the same two calls block::process makes, to keep the averaged numbers for the database
    FFT fft ((u32) kBlockSize, rate, 100);
    Aquila::Mfcc mfccProc ((std::size_t) kBlockSize, (unsigned int) rate);
    std::array<double, 12> mSum {}, mSq {};
    std::array<double, 16> fSum {};

    for (u32 k = 0; k < count; ++k)
    {
        spiralcore::sample pcm (mono.data() + starts[k], (unsigned int) kBlockSize);
        block b ((u64) k, std::string ("atlas"), pcm, rate, w, true);   // Samplebrain's own analysis of this block
        of || b;                                                         // and its own way of storing it

        spiralcore::sample win (mono.data() + starts[k], (unsigned int) kBlockSize);
        w.run (win);
        fft.impulse2freq (win.get_buffer());
        fft.calculate_bins();
        std::vector<std::complex<double>> spec;
        spec.reserve ((size_t) kBlockSize);
        for (int i = 0; i < kBlockSize; ++i) spec.emplace_back (fft.m_spectrum[i][0], fft.m_spectrum[i][1]);
        const std::vector<double> m = mfccProc.calculate (spec, (std::size_t) kMfccFilters);
        for (int i = 0; i < kMfccFilters && i < (int) m.size(); ++i) { mSum[(size_t) i] += m[(size_t) i]; mSq[(size_t) i] += m[(size_t) i] * m[(size_t) i]; }
        for (int i = 0; i < 96; ++i) fSum[(size_t) (i / 6)] += fft.m_bin[i];
    }
    of.close();
    if (count == 0) return false;
    for (int i = 0; i < 12; ++i)
    {
        const double mean = mSum[(size_t) i] / count;
        out.mfcc[(size_t) i] = (float) mean;
        out.mfccDev[(size_t) i] = (float) std::sqrt (juce::jmax (0.0, mSq[(size_t) i] / count - mean * mean));
    }
    for (int i = 0; i < 16; ++i) out.fft[(size_t) i] = (float) (fSum[(size_t) i] / (count * 6.0));
    out.blocks = (int) count;
    return true;
}

// =====================================================================================================================
struct Timbre::Index::Impl
{
    std::map<juce::String, std::vector<spiralcore::block>> sets;
};
Timbre::Index::Index() : impl (new Impl()) {}
Timbre::Index::~Index() = default;

bool Timbre::Index::add (const juce::String& id)
{
    using namespace spiralcore;
    if (impl->sets.count (id) > 0) return true;
    const auto file = blockFile (id);
    if (! file.existsAsFile()) return false;
    std::lock_guard<std::mutex> guard (samplebrainLock());
    std::ifstream is (file.getFullPathName().toRawUTF8(), std::ios::binary);
    if (! is.good()) return false;
    u32 count = 0;
    is || count;
    if (count == 0 || count > 64) return false;
    std::vector<block> blocks ((size_t) count);
    for (u32 k = 0; k < count; ++k)
    {
        is || blocks[k];
        if (! is.good()) return false;
        // (a block's count of how often it was used is not in the file and is left as it was found in memory: here it
        // is set to nothing, since anything else, however little it is weighed, can turn a distance into no number at all)
        blocks[k].get_usage() = 0;
    }
    impl->sets[id] = std::move (blocks);
    return true;
}
bool Timbre::Index::has (const juce::String& id) const { return impl->sets.count (id) > 0; }

double Timbre::Index::distance (const juce::String& a, const juce::String& b, float mfccRatio, float normalised) const
{
    using namespace spiralcore;
    const auto ia = impl->sets.find (a), ib = impl->sets.find (b);
    if (ia == impl->sets.end() || ib == impl->sets.end()) return -1.0;
    const search_params params (mfccRatio, normalised, 0, 100, 0.0f);
    const size_t n = juce::jmin (ia->second.size(), ib->second.size());
    if (n == 0) return -1.0;
    double sum = 0;
    for (size_t k = 0; k < n; ++k) sum += ia->second[k].compare (ib->second[k], params);
    return sum / (double) n;
}
} // namespace atlas
