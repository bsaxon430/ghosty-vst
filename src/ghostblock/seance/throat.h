/*
 * SEANCE (GhostBlock) - a spirit's THROAT: one of Surge XT's filters on that operator's own output, before anything
 * hears it (the links it feeds and the middle of the table). The filter code is the Surge Synth Team's sst-filters
 * library (libs/sst/sst-filters, GPL-3), used as it is through its filters++ interface; nothing here is a rewrite.
 */
#pragma once
#include <algorithm>
#include <memory>
#include <string>
#include <vector>
#include <cmath>
#include "sst/filters++.h"

namespace ghostblock::seance
{
namespace sfpp = sst::filtersplusplus;
struct ThroatKind
{
    sfpp::FilterModel model{sfpp::FilterModel::None};
    sfpp::ModelConfig cfg{};
    std::string group, name; // "Vintage Ladder", "LP 24dB ..."
};
// [0] is Off. The order is the library's own (sorted); saved sounds store the index, so the library stays pinned.
// The filters that need a delay line (the combs) come AFTER all the others, so every index saved before they were
// added still means the same filter. Each Throat owns one delay line (about 8 KB) for them.
inline const std::vector<ThroatKind> &throatCatalog()
{
    static const std::vector<ThroatKind> cat = [] {
        std::vector<ThroatKind> v;
        v.push_back({sfpp::FilterModel::None, {}, "", "Off"});
        for (auto m : sfpp::Filter::availableModels())
        {
            if (m == sfpp::FilterModel::None) continue;
            for (auto &c : sfpp::Filter::availableModelConfigurations(m, true))
            {
                if (sfpp::Filter::requiredDelayLinesSizes(m, c) != 0) continue;
                v.push_back({m, c, sfpp::toString(m), c.toString()});
            }
        }
        for (auto m : sfpp::Filter::availableModels())
        {
            if (m == sfpp::FilterModel::None) continue;
            for (auto &c : sfpp::Filter::availableModelConfigurations(m, true))
                if (sfpp::Filter::requiredDelayLinesSizes(m, c) != 0) v.push_back({m, c, sfpp::toString(m), c.toString()});
        }
        return v;
    }();
    return cat;
}

// the longest delay line any filter in the catalog asks for (floats)
inline size_t throatLineSize()
{
    static const size_t n = [] {
        size_t m = 0;
        for (auto &k : throatCatalog())
            if (k.model != sfpp::FilterModel::None) m = std::max(m, sfpp::Filter::requiredDelayLinesSizes(k.model, k.cfg));
        return m;
    }();
    return n;
}
struct Throat
{
    sfpp::Filter filter;
    int kind{-1};
    bool ok{false};
    float extra{0.f}; // a comb with a mix setting plays half filtered, half straight (the classic comb); every other filter: its default
    std::unique_ptr<float[]> line; // this throat's delay line (made with the voice, never on the audio thread)
    Throat() : line(throatLineSize() ? new float[throatLineSize() + 32]() : nullptr) {}
    // the library's filter runs four lanes at once and this throat feeds only the first: the other three share one
    // scratch line (they are fed silence and never listened to)
    static float *spareLine()
    {
        static std::unique_ptr<float[]> s(new float[throatLineSize() + 32]());
        return s.get();
    }
    void forget() { kind = -1; ok = false; } // at a new note: configure (and so clear) at the next block
    void configure(int k, double sampleRate, int blockSize)
    {
        const auto &cat = throatCatalog();
        kind = k;
        ok = false;
        if (k <= 0 || k >= (int)cat.size()) return;
        filter = sfpp::Filter();
        filter.setSampleRateAndBlockSize((float)sampleRate, blockSize);
        filter.setFilterModel(cat[(size_t)k].model);
        filter.setModelConfiguration(cat[(size_t)k].cfg);
        const size_t need = sfpp::Filter::requiredDelayLinesSizes(cat[(size_t)k].model, cat[(size_t)k].cfg);
        extra = (need && sfpp::Filter::coefficientsExtraCount(cat[(size_t)k].model, cat[(size_t)k].cfg) > 0) ? 0.5f : 0.f;
        if (need)
        { // a comb: an empty delay line for this note
            if (!line || need > throatLineSize()) return;
            std::fill(line.get(), line.get() + need, 0.f);
            filter.provideDelayLine(0, line.get());
            for (int v = 1; v < 4; ++v) filter.provideDelayLine(v, spareLine());
        }
        ok = filter.prepareInstance();
        if (ok) filter.reset();
    }
    // cutoff in semitones from 440 Hz (Surge's own scale), resonance 0 .. 1
    void process(float *buf, int n, float cutoff, float resonance)
    {
        filter.makeCoefficients(0, cutoff, resonance, extra);
        filter.prepareBlock();
        for (int i = 0; i < n; ++i)
        {
            auto y = filter.processMonoSample(buf[i]);
            buf[i] = std::isfinite(y) ? y : 0.f;
        }
        filter.concludeBlock();
    }
};
} // namespace ghostblock::seance
