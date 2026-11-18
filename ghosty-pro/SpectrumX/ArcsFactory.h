#pragma once
// ARCS page: factory pages (a starting point in one click) and line patches (sound presets for a line).
// All original, generated from code; no samples.
#include "ArcsModel.h"

namespace arcs::factory {

// ---------------------------------------------------------------- line patches
struct Patch { const char* name; const char* group; void (*apply)(Arc&); };
inline std::array<float, kWaveLen> mixWaves(int a, int b, float t) { auto x = waveShape(a), y = waveShape(b); for (int i = 0; i < kWaveLen; ++i) x[(size_t) i] = x[(size_t) i] * (1 - t) + y[(size_t) i] * t; return x; }
inline void resetSound(Arc& a) { a.source = SrcWave; a.unison = 1; a.detune = 0.3f; a.spread = 0.6f; a.sub = 0; a.drive = 0; a.filter = FiltOff; a.res = 0.2f; a.track = false; a.cutoff.clear(); }
inline const std::vector<Patch>& patches() {
    static const std::vector<Patch> P {
        { "Clean sine", "Basic", [](Arc& a) { resetSound(a); a.setWave(waveShape(ShapeSine)); a.env = { { 0, 0 }, { 0.04f, 1 }, { 0.9f, 0.85f }, { 1, 0 } }; } },
        { "Soft lead", "Lead", [](Arc& a) { resetSound(a); a.setWave(waveShape(ShapeSoft)); a.env = { { 0, 0 }, { 0.05f, 1 }, { 0.7f, 0.75f }, { 1, 0 } }; } },
        { "Saw lead", "Lead", [](Arc& a) { resetSound(a); a.setWave(waveShape(ShapeSaw)); a.filter = FiltLP; a.track = true; a.cutoff = { { 0, 0.85f } }; a.res = 0.25f; } },
        { "Super saw", "Lead", [](Arc& a) { resetSound(a); a.setWave(waveShape(ShapeSaw)); a.unison = 7; a.detune = 0.45f; a.spread = 0.9f; a.env = { { 0, 0 }, { 0.08f, 1 }, { 0.85f, 0.85f }, { 1, 0 } }; } },
        { "Square bass", "Bass", [](Arc& a) { resetSound(a); a.setWave(waveShape(ShapeSquare)); a.sub = 0.5f; a.filter = FiltLP; a.cutoff = { { 0, 0.62f } }; a.res = 0.3f; } },
        { "Sub bass", "Bass", [](Arc& a) { resetSound(a); a.setWave(waveShape(ShapeSine)); a.sub = 0.6f; a.drive = 0.15f; } },
        { "Reese", "Bass", [](Arc& a) { resetSound(a); a.setWave(waveShape(ShapeSaw)); a.unison = 3; a.detune = 0.35f; a.spread = 0.3f; a.sub = 0.4f; a.filter = FiltLP; a.cutoff = { { 0, 0.55f }, { 0.5f, 0.68f }, { 1, 0.5f } }; a.res = 0.2f; } },
        { "Driven saw", "Bass", [](Arc& a) { resetSound(a); a.setWave(waveShape(ShapeSaw)); a.drive = 0.6f; a.sub = 0.3f; a.filter = FiltLP; a.cutoff = { { 0, 0.72f } }; } },
        { "Acid", "Bass", [](Arc& a) { resetSound(a); a.setWave(waveShape(ShapeSaw)); a.filter = FiltLP; a.track = true; a.res = 0.82f; a.drive = 0.35f; a.cutoff = { { 0, 0.85f }, { 0.25f, 0.45f }, { 0.5f, 0.75f }, { 0.75f, 0.4f }, { 1, 0.6f } }; } },
        { "Pluck", "Keys", [](Arc& a) { resetSound(a); a.setWave(waveShape(ShapePulse)); a.env = { { 0, 1 }, { 0.15f, 0.45f }, { 0.5f, 0.15f }, { 1, 0 } }; a.filter = FiltLP; a.track = true; a.cutoff = { { 0, 0.9f }, { 0.3f, 0.55f }, { 1, 0.45f } }; } },
        { "Pad swell", "Pad", [](Arc& a) { resetSound(a); a.setWave(mixWaves(ShapeTriangle, ShapeSine, 0.5f)); a.unison = 3; a.detune = 0.2f; a.spread = 0.8f; a.env = { { 0, 0 }, { 0.4f, 1 }, { 0.8f, 0.8f }, { 1, 0 } }; } },
        { "Hollow", "Pad", [](Arc& a) { resetSound(a); a.setWave(waveShape(ShapeSquare)); a.filter = FiltBP; a.track = true; a.cutoff = { { 0, 0.62f } }; a.res = 0.55f; a.unison = 2; a.detune = 0.15f; } },
        { "Noise riser", "FX", [](Arc& a) { resetSound(a); a.source = SrcNoise; a.filter = FiltBP; a.track = true; a.res = 0.7f; a.cutoff = { { 0, 0.5f } }; a.env = { { 0, 0 }, { 0.85f, 1 }, { 1, 0 } }; } },
        { "Noise wash", "FX", [](Arc& a) { resetSound(a); a.source = SrcNoise; a.filter = FiltLP; a.cutoff = { { 0, 0.4f }, { 1, 0.9f } }; a.res = 0.3f; a.env = { { 0, 0 }, { 0.5f, 1 }, { 1, 0 } }; } },
        { "Noise hat", "FX", [](Arc& a) { resetSound(a); a.source = SrcNoise; a.filter = FiltHP; a.cutoff = { { 0, 0.88f } }; a.env = { { 0, 1 }, { 0.2f, 0.2f }, { 1, 0 } }; } },
        { "Fat pulse", "Lead", [](Arc& a) { resetSound(a); a.setWave(waveShape(ShapePulse)); a.unison = 5; a.detune = 0.3f; a.spread = 0.7f; a.drive = 0.25f; } },
    };
    return P;
}
inline void applyPatch(Arc& a, const juce::String& name) { for (auto& p : patches()) if (name == p.name) { p.apply(a); return; } }

// ---------------------------------------------------------------- factory pages
struct PageDef { const char* name; const char* group; Page (*make)(); };
namespace detail {
inline Arc line(std::vector<Pt> pts, const char* patch, float gain = 0.6f, int colour = 0) { Arc a; a.pts = std::move(pts); applyPatch(a, patch); a.gain = gain; a.colour = colour; return a; }
inline Page beats(double b, double bpm = 120) { Page p; p.tempoSync = true; p.lengthBeats = b; p.ownBpm = bpm; p.master = 0.5f; return p; }
inline Page secs(double s) { Page p; p.tempoSync = false; p.lengthSeconds = s; p.master = 0.45f; return p; }
inline int add(Page& p, Arc a) { auto* n = p.add(std::move(a)); return n ? n->id : -1; }
inline void fm(Page& p, int carrier, int mod, float depth) { if (auto* c = p.find(carrier)) { c->fmSource = mod; c->fmDepth = depth; } }
inline Arc modLine(std::vector<Pt> pts, float gain = 0.9f, int shape = ShapeSine) { Arc m; m.pts = std::move(pts); m.setWave(waveShape(shape)); m.modOnly = true; m.gain = gain; m.colour = 2; return m; }
inline void melody(Page& p, const std::vector<std::pair<float, float>>& notes, float pageBeats, const char* patch, float gain, int colour, float glide = 0.2f) {
    for (size_t i = 0; i < notes.size(); ++i) { const float b0 = notes[i].first, b1 = i + 1 < notes.size() ? notes[i + 1].first : b0 + 1; if (notes[i].second <= 0) continue;
        std::vector<Pt> pts { { b0 / pageBeats, notes[i].second }, { (b1 - glide - 0.02f) / pageBeats, notes[i].second } };
        if (glide > 0 && i + 1 < notes.size() && notes[i + 1].second > 0) pts.push_back({ (b1 - 0.02f) / pageBeats, notes[i + 1].second });
        add(p, line(pts, patch, gain, colour)); }
}
} // namespace detail

inline const std::vector<PageDef>& pages() {
    using namespace detail;
    static const std::vector<PageDef> D {
        { "Sub dive", "Bass", [] { Page p = beats(8); add(p, line({ { 0.0f, 55 }, { 0.12f, 52 }, { 0.6f, 31 }, { 0.98f, 26 } }, "Sub bass", 0.8f, 5)); add(p, line({ { 0.0f, 67 }, { 0.5f, 43 }, { 0.98f, 38 } }, "Driven saw", 0.35f, 0)); p.name = "Sub dive"; return p; } },
        { "Reese drone", "Bass", [] { Page p = beats(16); add(p, line({ { 0.0f, 33 }, { 0.5f, 33.3f }, { 0.75f, 31 }, { 1.0f, 33 } }, "Reese", 0.7f, 5)); p.name = "Reese drone"; return p; } },
        { "FM growl", "Bass", [] { Page p = beats(8); const int c = add(p, line({ { 0.0f, 36 }, { 0.25f, 36 }, { 0.3f, 43 }, { 0.6f, 43 }, { 0.65f, 34 }, { 1.0f, 34 } }, "Sub bass", 0.75f, 5));
            const int m = add(p, modLine({ { 0.0f, 36 }, { 0.5f, 55 }, { 1.0f, 41 } }, 0.9f, ShapeSaw)); fm(p, c, m, 0.55f); p.name = "FM growl"; return p; } },
        { "Wobble pulse", "Bass", [] { Page p = beats(8); Arc w = line({ { 0.0f, 38 }, { 0.124f, 38 } }, "Acid", 0.6f, 5); w.repeat = true; w.rate = 1.0f; w.cutoff = { { 0, 0.3f }, { 0.5f, 0.9f }, { 1, 0.3f } }; add(p, w);
            add(p, line({ { 0.0f, 26 }, { 1.0f, 26 } }, "Sub bass", 0.6f, 1)); p.name = "Wobble pulse"; return p; } },
        { "Dive bombs", "Bass", [] { Page p = beats(16); for (int k = 0; k < 4; ++k) add(p, line({ { k * 0.25f, 72.0f - k * 3 }, { k * 0.25f + 0.2f, 30.0f - k } }, "Driven saw", 0.5f, k)); p.name = "Dive bombs"; return p; } },
        { "Neuro stabs", "Bass", [] { Page p = beats(4); const float t[] = { 0, 0.75f, 1.5f, 2.0f, 3.0f, 3.5f };
            for (int k = 0; k < 6; ++k) { const int c = add(p, line({ { t[k] / 4, 40.0f + (k % 3) * 2 }, { (t[k] + 0.4f) / 4, 39.0f + (k % 3) * 2 } }, "Square bass", 0.6f, 5));
                const int m = add(p, modLine({ { t[k] / 4, 52.0f + k }, { (t[k] + 0.4f) / 4, 64.0f - k } }, 0.8f)); fm(p, c, m, 0.5f + 0.05f * k); } p.name = "Neuro stabs"; return p; } },
        { "Noise riser", "Riser", [] { Page p = beats(32); add(p, line(makeCurve({ 0.0f, 40 }, { 0.98f, 100 }, 0), "Noise riser", 0.6f, 3)); add(p, line(makeCurve({ 0.25f, 48 }, { 0.98f, 84 }, 0), "Super saw", 0.25f, 0)); p.name = "Noise riser"; return p; } },
        { "Octave riser", "Riser", [] { Page p = beats(16); for (int k = 0; k < 3; ++k) add(p, line(makeCurve({ 0.0f, 45.0f + 12 * k }, { 0.98f, 69.0f + 12 * k }, 1), "Super saw", 0.25f, k)); p.name = "Octave riser"; return p; } },
        { "Tension S-curve", "Riser", [] { Page p = beats(16); p.speedOn = true; p.speed = { { 0, -0.5f }, { 1, 0.6f } }; for (int k = 0; k < 5; ++k) add(p, line(makeCurve({ 0.02f, 50.0f + k * 5 }, { 0.98f, 64.0f + k * 7 }, 1), "Pad swell", 0.25f, k)); p.name = "Tension S-curve"; return p; } },
        { "Glass drone", "Texture", [] { Page p = secs(20); const float n[] = { 50, 57, 62, 69, 74 }; for (int k = 0; k < 5; ++k) { const int c = add(p, line({ { 0.0f, n[k] }, { 1.0f, n[k] + 0.08f } }, "Clean sine", 0.25f, 2));
            const int m = add(p, modLine({ { 0.0f, n[k] + 28.0f }, { 1.0f, n[k] + 28.0f } }, 0.6f)); fm(p, c, m, 0.25f); } p.name = "Glass drone"; return p; } },
        { "Cluster swell", "Texture", [] { Page p = secs(16); for (int k = 0; k < 12; ++k) add(p, line({ { 0.0f, 60.0f + k * 0.5f }, { 0.5f, 62.0f + k }, { 1.0f, 55.0f + k * 1.5f } }, "Pad swell", 0.15f, k % 8)); p.name = "Cluster swell"; return p; } },
        { "Glissando cloud", "Texture", [] { Page p = secs(24); juce::Random r(11); for (int k = 0; k < 32; ++k) add(p, line({ { r.nextFloat() * 0.1f, 40 + r.nextFloat() * 50 }, { 0.45f + r.nextFloat() * 0.1f, 62.0f + (k % 5) * 5 }, { 0.9f + r.nextFloat() * 0.09f, 40 + r.nextFloat() * 50 } }, k % 3 ? "Soft lead" : "Saw lead", 0.12f, k % 8)); p.name = "Glissando cloud"; return p; } },
        { "Metal FM rain", "Texture", [] { Page p = secs(16); juce::Random r(5); for (int k = 0; k < 12; ++k) { const float x = r.nextFloat() * 0.85f; const float y = 72 + r.nextFloat() * 20;
            const int c = add(p, line({ { x, y }, { x + 0.1f, y - 14 } }, "Pluck", 0.3f, 2)); const int m = add(p, modLine({ { x, y + 7 }, { x + 0.1f, y + 19 } }, 0.8f)); fm(p, c, m, 0.6f); } p.name = "Metal FM rain"; return p; } },
        { "Drawn melody", "Melodic", [] { Page p = beats(16, 96); p.root = 9; p.scale = 1; melody(p, { { 0, 69 }, { 1.5f, 72 }, { 2, 71 }, { 3, 69 }, { 4, 65 }, { 6, 67 }, { 7, 69 }, { 8, 72 }, { 10, 76 }, { 11, 74 }, { 12, 71 }, { 14, 69 }, { 15.8f, 0 } }, 16, "Soft lead", 0.45f, 0);
            melody(p, { { 0, 45 }, { 4, 41 }, { 8, 48 }, { 12, 43 }, { 15.9f, 0 } }, 16, "Square bass", 0.45f, 5, 0.1f); p.name = "Drawn melody"; return p; } },
        { "Polyrhythm 3:4", "Melodic", [] { Page p = beats(12); Arc a = line({ { 0.0f, 69 }, { 1.0f / 36, 72 }, { 2.0f / 36, 76 }, { 3.0f / 36 - 0.002f, 76 } }, "Pluck", 0.4f, 1); a.repeat = true; a.rate = 1.0f; add(p, a);
            Arc b = line({ { 0.0f, 57 }, { 2.0f / 36, 64 }, { 3.0f / 36 - 0.002f, 64 } }, "Pluck", 0.4f, 0); b.repeat = true; b.rate = 0.75f; add(p, b); p.name = "Polyrhythm 3:4"; return p; } },
        { "Arp 5/4", "Melodic", [] { Page p = beats(10); Arc a = line({ { 0.0f, 62 }, { 0.02f, 66 }, { 0.04f, 69 }, { 0.06f, 73 }, { 0.08f, 74 }, { 0.0995f, 74 } }, "Pluck", 0.4f, 1); a.repeat = true; add(p, a);
            add(p, line({ { 0.0f, 38 }, { 0.98f, 38 } }, "Sub bass", 0.5f, 5)); p.grid = 10; p.name = "Arp 5/4"; return p; } },
        { "Pluck sequence", "Melodic", [] { Page p = beats(8); const float n[] = { 60, 63, 67, 70, 72, 70, 67, 63, 58, 62, 65, 70, 74, 70, 65, 62 };
            for (int k = 0; k < 16; ++k) { add(p, line({ { k / 16.0f, n[k] }, { k / 16.0f + 0.05f, n[k] } }, "Pluck", 0.4f, k % 2)); } p.name = "Pluck sequence"; return p; } },
        { "Bell canon", "Melodic", [] { Page p = beats(16); const float n[] = { 81, 84, 88, 86, 81, 79 };
            for (int v = 0; v < 2; ++v) for (int k = 0; k < 6; ++k) { const float x = (k * 2 + v * 3) / 16.0f; if (x > 0.9f) continue; const int c = add(p, line({ { x, n[k] - 12.0f * v }, { x + 0.11f, n[k] - 12.0f * v } }, "Clean sine", 0.3f, 2));
                const int m = add(p, modLine({ { x, n[k] - 12.0f * v + 21.69f }, { x + 0.11f, n[k] - 12.0f * v + 21.69f } }, 0.9f)); fm(p, c, m, 0.45f); if (auto* a = p.find(c)) a->env = { { 0, 1 }, { 0.12f, 0.45f }, { 0.5f, 0.15f }, { 1, 0 } }; }
            p.name = "Bell canon"; return p; } },
        { "Hypnotic house arp", "Melodic", [] { Page p = beats(16, 124); p.root = 7; p.scale = 1; Arc a = line({ { 0.0f, 67 }, { 0.015f, 67 }, { 0.016f, 70 }, { 0.031f, 70 }, { 0.032f, 74 }, { 0.047f, 74 }, { 0.048f, 79 }, { 0.0620f, 79 } }, "Acid", 0.35f, 1);
            a.repeat = true; a.drive = 0.2f; a.cutoff = { { 0, 0.55f }, { 1, 0.75f } }; add(p, a); add(p, line({ { 0.0f, 43 }, { 0.99f, 43 } }, "Sub bass", 0.45f, 5)); p.name = "Hypnotic house arp"; return p; } },
        { "Gesture scribble", "IDM", [] { Page p = beats(8); juce::Random r(3); std::vector<Pt> pts; float y = 70; for (int k = 0; k <= 80; ++k) { y = juce::jlimit(40.0f, 100.0f, y + (r.nextFloat() - 0.5f) * 9); pts.push_back({ k / 80.0f * 0.98f, y }); }
            add(p, line(pts, "Fat pulse", 0.35f, 4)); p.speedOn = true; p.speed = { { 0, 0.8f }, { 0.3f, -1.0f }, { 0.6f, 1.2f }, { 1, -0.4f } }; p.name = "Gesture scribble"; return p; } },
        { "Ratio clock", "IDM", [] { Page p = beats(16); const float rates[] = { 1, 1.25f, 4.0f / 3, 1.5f, 2.0f / 3 };
            for (int k = 0; k < 5; ++k) { Arc a = line({ { 0.0f, 60.0f + k * 5 }, { 0.03f, 60.0f + k * 5 } }, "Pluck", 0.3f, k); a.repeat = true; a.rate = rates[k]; add(p, a); } p.name = "Ratio clock"; return p; } },
        { "Zigzag grains", "IDM", [] { Page p = beats(8); for (int k = 0; k < 24; ++k) { const float x = k / 24.0f; add(p, line({ { x, 70.0f + (k % 2 ? 12 : -12) }, { x + 0.02f, 70.0f + (k % 2 ? -12 : 12) } }, k % 3 ? "Pluck" : "Noise hat", 0.3f, k % 8)); } p.name = "Zigzag grains"; return p; } },
    };
    return D;
}

} // namespace arcs::factory
