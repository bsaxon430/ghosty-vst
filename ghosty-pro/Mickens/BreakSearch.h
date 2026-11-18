#pragma once
// Mr. Mickens 2: BREAK SEARCH BY FEEL + BREAK MATCH.
// Every break gets feel descriptors from its DNA (tempo, ghost-note density, snare accent strength, swing, kick
// sparseness, high-frequency busyness, brightness, syncopation) and text tags. SEARCH ranks breaks by how close they
// are to the feel you dial in (sliders left on ANY don't count) and filters by words. MATCH ranks breaks against the
// current bay's break: SIMILAR (closest feel + groove) or COMPLEMENT (fills the other's gaps: different density and
// spectrum, few colliding hits, and a swing that sits with it).
#include "BreakDNA.h"
#include <numeric>

namespace mk {

struct BreakEntry { int index = -1; juce::String name, family; std::shared_ptr<const BreakDNA> dna; juce::StringArray tags; double bpm = 0; };

enum FeelAxis { kAxTempo, kAxGhost, kAxSnareAcc, kAxSwing, kAxKickSparse, kAxHfBusy, kAxBright, kAxSynco, kNumAxes };
inline const char* axisName(int a) { static const char* n[] = { "TEMPO", "GHOSTS", "SNARE ACCENT", "SWING", "KICK SPARSE", "HAT BUSY", "BRIGHT", "SYNCOPATION" }; return n[juce::jlimit(0, kNumAxes - 1, a)]; }

// every descriptor on a 0..1 scale
inline std::array<float, kNumAxes> feelVector(const BreakDNA& d) {
    return { juce::jlimit(0.0f, 1.0f, (d.bpm - 80.0f) / 100.0f), juce::jlimit(0.0f, 1.0f, d.ghostDensity * 4.0f), d.snareAccent,
             juce::jlimit(0.0f, 1.0f, 0.5f + d.swing * 4.0f), d.kickSparse, d.hfBusy, d.bright, d.synco };
}
// brightMedian: BRIGHT / DARK is judged against the list (the old-record processing makes every built-in "dark" against 0.5)
inline juce::StringArray feelTags(const BreakDNA& d, float brightMedian = 0.5f) {
    juce::StringArray t;
    t.add(d.bpm < 100 ? "slow" : d.bpm > 150 ? "fast" : "mid");
    if (d.bpm >= 160) t.add("jungle");
    if (d.ghostDensity > 0.1f) t.add("ghosty");
    if (d.kickSparse > 0.55f) t.add("sparse"); else if (d.kickSparse < 0.25f) t.add("kicky");
    if (d.hfBusy > 0.75f) t.add("busy");
    t.add(d.bright > brightMedian ? "bright" : "dark");
    t.add(std::abs(d.swing) > 0.04f ? "swung" : "straight");
    if (d.synco > 0.45f) t.add("syncopated");
    if (d.density > 0.85f) t.add("rolling");
    if (d.snareAccent > 0.65f) t.add("punchy");
    return t;
}

// onsets folded onto 32 steps, for groove comparisons
inline std::array<uint8_t, 32> fold32(const BreakDNA& d) {
    std::array<uint8_t, 32> f {}; if (d.steps <= 0) return f;
    for (int i = 0; i < d.steps; ++i) f[(size_t) ((i * 32 / d.steps) % 32)] |= d.cls[(size_t) i];
    return f;
}

struct SearchIndex {
    std::vector<BreakEntry> items;
    std::array<float, kNumAxes> target {};   // -1 = any
    SearchIndex() { target.fill(-1.0f); }

    // ranked positions into items: the closest feel first; every word must appear in the name, family or tags
    std::vector<int> search(const std::array<float, kNumAxes>& want, const juce::String& text) const {
        auto words = juce::StringArray::fromTokens(text.toLowerCase(), " ,", ""); words.removeEmptyStrings();
        std::vector<std::pair<float, int>> r;
        for (int i = 0; i < (int) items.size(); ++i) {
            const auto& e = items[(size_t) i]; if (!e.dna) continue;
            const juce::String hay = (e.name + " " + e.family + " " + e.tags.joinIntoString(" ")).toLowerCase();
            bool ok = true; for (auto& w : words) if (!hay.contains(w)) { ok = false; break; }
            if (!ok) continue;
            const auto v = feelVector(*e.dna); float dist = 0;
            for (int a = 0; a < kNumAxes; ++a) if (want[(size_t) a] >= 0) dist += std::abs(v[(size_t) a] - want[(size_t) a]);
            r.push_back({ dist, i });
        }
        std::stable_sort(r.begin(), r.end(), [](auto& a, auto& b) { return a.first < b.first; });
        std::vector<int> out; for (auto& x : r) out.push_back(x.second); return out;
    }

    static float similarity(const BreakDNA& a, const BreakDNA& b) {   // 1 = the same
        const auto va = feelVector(a), vb = feelVector(b); float d = 0; for (int i = 0; i < kNumAxes; ++i) d += std::abs(va[(size_t) i] - vb[(size_t) i]);
        float sp = 0; for (int i = 0; i < 4; ++i) sp += std::abs(a.bandShape[i] - b.bandShape[i]);
        const auto fa = fold32(a), fb = fold32(b); int inter = 0, uni = 0;
        for (int i = 0; i < 32; ++i) { const bool x = fa[(size_t) i] & (kK | kS), y = fb[(size_t) i] & (kK | kS); inter += x && y; uni += x || y; }
        const float groove = uni ? inter / (float) uni : 1.0f;
        return 1.0f / (1.0f + d + sp + (1 - groove) * 2.0f);
    }
    static float complement(const BreakDNA& a, const BreakDNA& b) {   // high = they fit together
        const float dens = std::abs(a.density - b.density);
        float dot = 0, na = 0, nb = 0; for (int i = 0; i < 4; ++i) { dot += a.bandShape[i] * b.bandShape[i]; na += a.bandShape[i] * a.bandShape[i]; nb += b.bandShape[i] * b.bandShape[i]; }
        const float spec = 1.0f - dot / std::max(1e-9f, std::sqrt(na * nb));
        const auto fa = fold32(a), fb = fold32(b); int both = 0, ca = 0, cb = 0;
        for (int i = 0; i < 32; ++i) { const bool x = fa[(size_t) i] & (kK | kS | kG), y = fb[(size_t) i] & (kK | kS | kG); both += x && y; ca += x; cb += y; }
        const float overlap = std::min(ca, cb) > 0 ? both / (float) std::min(ca, cb) : 1.0f;
        const float swingOk = std::exp(-std::abs(a.swing - b.swing) * 20.0f);
        return swingOk * (0.35f * juce::jmin(1.0f, dens * 2.0f) + 0.25f * juce::jmin(1.0f, spec * 3.0f) + 0.4f * (1.0f - overlap));
    }
    // mode 0 SIMILAR, 1 COMPLEMENT: positions into items with their scores, best first
    std::vector<std::pair<int, float>> match(const BreakDNA& ref, int mode) const {
        std::vector<std::pair<int, float>> r;
        for (int i = 0; i < (int) items.size(); ++i) if (items[(size_t) i].dna) r.push_back({ i, mode == 0 ? similarity(ref, *items[(size_t) i].dna) : complement(ref, *items[(size_t) i].dna) });
        std::stable_sort(r.begin(), r.end(), [](auto& a, auto& b) { return a.second > b.second; });
        return r;
    }
};

} // namespace mk
