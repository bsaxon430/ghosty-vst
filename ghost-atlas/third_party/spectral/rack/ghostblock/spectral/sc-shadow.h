/*
 * Poltergeist 2 (GhostBlock) - SPECTRAL FX: how the unmodified Spectral Corruptor modules get their parameters
 * on the audio thread without a lock or an allocation.
 *
 * Upstream's modules read their parameters from a juce::ValueTree (state.getProperty ("amount", 0.0f)). Every such
 * read makes a juce::Identifier from a string, which takes the global string-pool lock and can free memory: fine in
 * upstream's own plugin, not allowed on our audio thread. The module sources must stay untouched, so instead of
 * changing them we compile them against RtTree: inside namespace scrr::dsp the name juce::ValueTree resolves to the
 * small fixed-size property bag below (same calls, same juce::var values and conversions, no lock, no allocation for
 * numbers). Everything else named juce:: there is the real JUCE. No upstream file is edited and no macro is used.
 *
 * Include this header instead of upstream's module headers, and only from our spectral .cpp files.
 */
#pragma once
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cstring>

namespace ghostblock::p2::spectral::detail
{
class RtTree
{
  public:
    static constexpr int kMax = 56;
    RtTree() = default;
    explicit RtTree(const char *) {} // juce::ValueTree ("module")

    // ---- the part of juce::ValueTree the upstream modules call
    juce::var getProperty(const char *name, const juce::var &fallback) const
    {
        if (auto *v = find(name)) return *v;
        return fallback;
    }
    juce::var getProperty(const juce::String &name, const juce::var &fallback) const { return getProperty(name.toRawUTF8(), fallback); }
    juce::var getProperty(const juce::Identifier &name, const juce::var &fallback) const
    {
        return getProperty(name.getCharPointer().getAddress(), fallback);
    }
    const juce::var &operator[](const char *name) const
    {
        if (auto *v = find(name)) return *v;
        return none;
    }
    RtTree &setProperty(const char *name, const juce::var &v, juce::UndoManager *)
    {
        if (auto *e = slot(name)) *e = v;
        return *this;
    }
    RtTree &setProperty(const juce::String &name, const juce::var &v, juce::UndoManager *u) { return setProperty(name.toRawUTF8(), v, u); }

    // ---- ours: the audio thread overwrites a number in place (a juce::var holding a number owns no memory)
    int indexOf(const char *name) const
    {
        for (int i = 0; i < n; ++i)
            if (std::strcmp(e[(size_t)i].key, name) == 0) return i;
        return -1;
    }
    void setDouble(int i, double v) noexcept { e[(size_t)i].value = v; }
    void setInt(int i, int v) noexcept { e[(size_t)i].value = v; }
    void setBool(int i, bool v) noexcept { e[(size_t)i].value = v; }

  private:
    struct Entry
    {
        char key[32]{};
        juce::var value;
    };
    const juce::var *find(const char *name) const
    {
        const int i = indexOf(name);
        return i >= 0 ? &e[(size_t)i].value : nullptr;
    }
    juce::var *slot(const char *name)
    {
        int i = indexOf(name);
        if (i < 0)
        {
            if (n >= kMax) return nullptr;
            i = n++;
            std::strncpy(e[(size_t)i].key, name, sizeof(e[(size_t)i].key) - 1);
        }
        return &e[(size_t)i].value;
    }
    std::array<Entry, kMax> e;
    int n{0};
    juce::var none;
};
} // namespace ghostblock::p2::spectral::detail

// inside scrr::dsp, "juce::ValueTree" is RtTree; every other juce:: name is the real one
namespace scrr
{
namespace dsp
{
namespace juce
{
using namespace ::juce;
using ValueTree = ::ghostblock::p2::spectral::detail::RtTree;
} // namespace juce
} // namespace dsp
} // namespace scrr

#include "modules/ModuleInstance.h"
#include "dsp/FFTEngine.h"
#include "dsp/PartialProcessor.h"
#include "dsp/FrequencySplit.h"
#include "dsp/DryWetMixer.h"
