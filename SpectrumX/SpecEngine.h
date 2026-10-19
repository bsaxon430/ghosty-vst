#pragma once
// Ghosty Spectrum 2: paint a loop on the frequency spectrum.
//   Layers (max 8): a synth sound (a Poltergeist patch), a drum (Ghosty Beats library) or your own sample.
//   Notes: placed on the canvas; x = time in the loop, y = frequency (pitch).
// Audio thread reads an immutable SpecSnapshot; per-layer Poltergeist engines + a drum voice pool.
#include "Summon.h"        // Poltergeist: patches, Summon, engine Snap
#include "Engine.h"
#include "DrumKit.h"       // Ghosty Beats 2 drum library
#include <juce_dsp/juce_dsp.h>

namespace sp {

constexpr int kLayers = 8, kMaxNotes = 1024;

// ---------------- the sound bank
struct BankItem { juce::String name, group; int kind = 0; juce::String words; uint32_t seed = 1; };   // kind: 0 synth, 1 drum, 2 sample
inline juce::String groupFor(const juce::String& cat) {
    if (cat == "808" || cat == "sub" || cat == "bass" || cat == "growl" || cat == "wobble" || cat == "reese" || cat == "wavetable") return "Bass";
    if (cat == "keys") return "Keys"; if (cat == "pad" || cat == "granular") return "Pads"; if (cat == "lead") return "Leads";
    if (cat == "pluck" || cat == "arp" || cat == "chop") return "Plucks"; if (cat == "bell") return "Bells"; if (cat == "choir") return "Vox";
    if (cat == "brass") return "Brass"; return "FX";
}
inline const std::vector<BankItem>& synthBank() {
    static const std::vector<BankItem> b = [] {
        std::vector<BankItem> v;
        for (auto& p : gs::factoryPresets()) { juce::String cat; gs::Summoner::summon(p.words, p.seed, &cat); if (cat == "chop" || cat == "granular" || cat == "wavetable") continue; v.push_back({ p.name, groupFor(cat), 0, p.words, p.seed }); }
        const std::pair<const char*, const char*> extra[] = { { "Soft Sub", "sub" }, { "Deep Bass", "bass dark" }, { "Glass Keys", "keys bright" }, { "Warm Pad", "pad warm wide" },
            { "Air Pad", "pad airy" }, { "Saw Lead", "lead dirty" }, { "Soft Pluck", "pluck soft" }, { "Kalimba", "pluck bright" }, { "Music Box", "bell bright" },
            { "Ghost Choir", "choir haunted" }, { "Big Brass", "brass wide" }, { "Arp Spark", "arp bright" }, { "Laser Rise", "riser bright" }, { "Bubble Pluck", "bubbly pluck" } };
        uint32_t seed = 100;
        for (auto& [n, w] : extra) { juce::String cat; gs::Summoner::summon(w, seed, &cat); v.push_back({ n, groupFor(cat), 0, w, seed++ }); }
        return v;
    }();
    return b;
}
inline juce::StringArray synthGroups() { return { "Bass", "Keys", "Pads", "Leads", "Plucks", "Bells", "Vox", "Brass", "FX" }; }

// ---------------- model (message thread)
struct Layer {
    int kind = 0; juce::String name, words; uint32_t seed = 1;
    std::shared_ptr<gs::SampleData> sample;   // kind 2
    float vol = 0.8f, pan = 0, tone = 0.5f, space = 0.2f; bool mute = false;
    float centroidMidi = 60;                  // drums: where they sit on the spectrum
};
struct Note { int layer = 0; double start = 0, len = 0.5; float pitch = 60, vel = 0.85f; bool glide = false; };
struct Model {
    std::vector<Layer> layers; std::vector<Note> notes;
    int bars = 2, root = 0, scale = 1; bool snap = true;
};

inline float midiToHz(float m) { return 440.0f * std::exp2((m - 69.0f) / 12.0f); }
inline float hzToMidi(float hz) { return 69.0f + 12.0f * std::log2(juce::jmax(1.0f, hz) / 440.0f); }
inline const std::vector<int>& scaleSteps(int s) {
    static const std::vector<std::vector<int>> S { { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 8, 11 }, { 0, 2, 3, 5, 7, 9, 10 }, { 0, 3, 5, 7, 10 }, { 0, 2, 4, 7, 9 }, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 } };
    return S[(size_t) juce::jlimit(0, 6, s)];
}
inline juce::StringArray scaleNames() { return { "Major", "Minor", "Harmonic minor", "Dorian", "Pentatonic minor", "Pentatonic major", "Chromatic" }; }
inline float snapPitch(float m, int root, int scale) {
    const int n = (int) std::round(m); const auto& sc = scaleSteps(scale);
    int best = n, bd = 99;
    for (int k = n - 6; k <= n + 6; ++k) { const int rel = ((k - root) % 12 + 12) % 12; if (std::find(sc.begin(), sc.end(), rel) != sc.end() && std::abs(k - n) < bd) { bd = std::abs(k - n); best = k; } }
    return (float) best;
}

// a drum's spot on the spectrum: its average zero-crossing frequency over the first 60 ms
inline float centroidOf(const gb::Sound& s) {
    const int n = juce::jmin((int) s.data.size(), (int) (s.rate * 0.06)); int z = 0;
    for (int i = 1; i < n; ++i) if ((s.data[(size_t) i - 1] < 0) != (s.data[(size_t) i] < 0)) ++z;
    return hzToMidi(juce::jmax(30.0f, z / 2.0f / (n / (float) s.rate)));
}

// ---------------- what the audio thread reads
struct SpecSnapshot {
    struct L { int kind = 0; gs::Snap snap {}; const gb::Sound* drum = nullptr; float vol = 0.8f, pan = 0, tone = 0.5f, space = 0.2f; bool mute = false, solo = false; float centroid = 60; };
    std::array<L, kLayers> layers {}; int layerCount = 0;
    std::array<Note, kMaxNotes> notes {}; int noteCount = 0;
    int bars = 2; bool anySolo = false;
};

class SpecEngine {
public:
    void prepare(double rate, int block) {
        sr = rate; maxBlock = juce::jmax(64, block);
        for (auto& e : engines) e.prepare(rate, maxBlock);
        layerBuf.setSize(kLayers * 2, maxBlock); sendBuf.setSize(2, maxBlock);
        for (auto& m : midi) m.ensureSize(4096);
        juce::dsp::ProcessSpec spec { rate, (juce::uint32) maxBlock, 2 };
        reverb.prepare(spec); juce::dsp::Reverb::Parameters rp; rp.roomSize = 0.82f; rp.damping = 0.4f; rp.wetLevel = 1.0f; rp.dryLevel = 0.0f; rp.width = 1.0f; reverb.setParameters(rp);
        for (auto& v : drums) v = DV();
        clock = 0;
    }
    void setSnapshot(const SpecSnapshot* s) { snap.store(s); }
    gs::Engine& engine(int i) { return engines[(size_t) i]; }
    bool playing = false;                         // internal transport (when the DAW is stopped)
    std::atomic<float> uiBeat { -1 };             // loop position for the playhead
    // analyser feed: the mono output, read by the UI for the live spectrum
    static constexpr int kRing = 8192; float ring[kRing] {}; std::atomic<int> ringPos { 0 };
    // audition (bank clicks): a note on a layer index or a drum sound, from the message thread
    std::atomic<int> auditionLayer { -1 }; std::atomic<int> auditionNote { 60 }; std::atomic<const gb::Sound*> auditionDrum { nullptr };

    void process(juce::AudioBuffer<float>& out, double hostBpm, double hostPpq, bool hostPlaying, double fallbackBpm) {
        const int n = out.getNumSamples(); out.clear();
        const SpecSnapshot* S = snap.load(); if (!S) return;
        const double bpm = hostPlaying ? hostBpm : fallbackBpm, spb = sr * 60.0 / juce::jmax(20.0, bpm);
        const bool run = hostPlaying || playing;
        const double b0 = hostPlaying ? hostPpq : clock, b1 = b0 + n / spb, L = S->bars * 4.0;
        for (auto& m : midi) m.clear();
        if (run) {
            for (int i = 0; i < S->noteCount; ++i) {
                const auto& nt = S->notes[(size_t) i]; if (nt.layer >= S->layerCount) continue;
                const auto& ly = S->layers[(size_t) nt.layer];
                for (double k = std::floor(b0 / L) - 1; k * L < b1; k += 1) {
                    const double on = k * L + nt.start, off = on + nt.len;
                    if (on >= b0 && on < b1) {
                        const int at = (int) ((on - b0) * spb);
                        if (ly.kind == 1) startDrum(ly.drum, nt.layer, nt.pitch - ly.centroid, nt.vel, at);
                        else midi[(size_t) nt.layer].addEvent(juce::MidiMessage::noteOn(1, juce::jlimit(0, 127, (int) std::round(nt.pitch)), nt.vel), at);
                    }
                    if (ly.kind != 1 && off >= b0 && off < b1) midi[(size_t) nt.layer].addEvent(juce::MidiMessage::noteOff(1, juce::jlimit(0, 127, (int) std::round(nt.pitch))), juce::jmax(0, (int) ((off - b0) * spb) - 1));
                }
            }
            if (!hostPlaying) clock = b1;
            uiBeat.store((float) std::fmod(b0, L));
        } else { uiBeat.store(-1); if (wasRunning) for (auto& m : midi) m.addEvent(juce::MidiMessage::allNotesOff(1), 0); }
        wasRunning = run;
        // audition requests
        if (const int al = auditionLayer.exchange(-1); al >= 0 && al < S->layerCount) {
            const auto& ly = S->layers[(size_t) al];
            if (ly.kind == 1) startDrum(ly.drum, al, 0, 0.9f, 0);
            else { const int nn = auditionNote.load(); midi[(size_t) al].addEvent(juce::MidiMessage::noteOn(1, nn, 0.85f), 0); midi[(size_t) al].addEvent(juce::MidiMessage::noteOff(1, nn), juce::jmin(n - 1, n / 2)); pendingOff[al] = nn; pendingOffSamples[al] = (int) (sr * 0.45); }
        }
        if (auto* d = auditionDrum.exchange(nullptr)) startDrum(d, -1, 0, 0.9f, 0);
        for (int l = 0; l < kLayers; ++l) if (pendingOff[l] >= 0) { pendingOffSamples[l] -= n; if (pendingOffSamples[l] <= 0) { midi[(size_t) l].addEvent(juce::MidiMessage::noteOff(1, pendingOff[l]), 0); pendingOff[l] = -1; } }

        // render each layer, then tone / pan / volume / reverb send
        layerBuf.clear(0, n); sendBuf.clear(0, n);
        float* L0 = out.getWritePointer(0); float* R0 = out.getWritePointer(1);
        for (int l = 0; l < S->layerCount; ++l) {
            const auto& ly = S->layers[(size_t) l];
            juce::AudioBuffer<float> view(layerBuf.getArrayOfWritePointers() + l * 2, 2, n);
            if (ly.kind != 1) { engines[(size_t) l].setBpm(bpm); engines[(size_t) l].setPpq(b0, run); engines[(size_t) l].process(view, midi[(size_t) l], ly.snap); }
            renderDrums(view, l, n);
            const bool audible = !ly.mute && (!S->anySolo || ly.solo);
            if (!audible) continue;
            auto& F = tone[(size_t) l]; const bool filt = std::abs(ly.tone - 0.5f) > 0.02f;
            if (filt) F.setup(ly.tone < 0.5f ? 120.0f * std::pow(160.0f, ly.tone * 2) : 25.0f * std::pow(160.0f, (ly.tone - 0.5f) * 2), (float) sr);
            const float pl = std::cos((ly.pan + 1) * juce::MathConstants<float>::pi * 0.25f) * 1.414f, pr = std::sin((ly.pan + 1) * juce::MathConstants<float>::pi * 0.25f) * 1.414f;
            float* a = view.getWritePointer(0); float* b = view.getWritePointer(1);
            for (int i = 0; i < n; ++i) {
                float x = a[i], y = b[i];
                if (filt) { x = F.tick(0, x, ly.tone < 0.5f); y = F.tick(1, y, ly.tone < 0.5f); }
                x *= ly.vol * pl; y *= ly.vol * pr;
                L0[i] += x; R0[i] += y; sendBuf.addSample(0, i, x * ly.space); sendBuf.addSample(1, i, y * ly.space);
            }
        }
        renderDrums(out, -1, n);   // auditioned bank drums go straight out
        juce::dsp::AudioBlock<float> sb(sendBuf.getArrayOfWritePointers(), 2, (size_t) n); reverb.process(juce::dsp::ProcessContextReplacing<float>(sb));
        for (int i = 0; i < n; ++i) {
            L0[i] = std::tanh((L0[i] + sendBuf.getSample(0, i) * 0.6f) * 0.9f); R0[i] = std::tanh((R0[i] + sendBuf.getSample(1, i) * 0.6f) * 0.9f);
            const int rp = ringPos.load(); ring[rp] = (L0[i] + R0[i]) * 0.5f; ringPos.store((rp + 1) % kRing);
        }
    }

private:
    struct DV { const gb::Sound* s = nullptr; double pos = 0, rate = 1; float gain = 1; int layer = 0, delay = 0; };
    struct TP { float s1[2] {}, s2[2] {}, a1 = 0, a2 = 0, a3 = 0;
        void setup(float f, float rate) { const float g = std::tan(juce::MathConstants<float>::pi * juce::jlimit(20.0f, rate * 0.45f, f) / rate); a1 = 1 / (1 + g * (g + 1.4f)); a2 = g * a1; a3 = g * a2; }
        float tick(int c, float x, bool lowpass) { const float v3 = x - s2[c], v1 = a1 * s1[c] + a2 * v3, v2 = s2[c] + a2 * s1[c] + a3 * v3; s1[c] = 2 * v1 - s1[c]; s2[c] = 2 * v2 - s2[c]; return lowpass ? v2 : x - 1.4f * v1 - v2; } };
    double sr = 44100, clock = 0; int maxBlock = 512; bool wasRunning = false;
    std::atomic<const SpecSnapshot*> snap { nullptr };
    std::array<gs::Engine, kLayers> engines;
    std::array<juce::MidiBuffer, kLayers> midi;
    juce::AudioBuffer<float> layerBuf, sendBuf;
    std::array<TP, kLayers> tone;
    juce::dsp::Reverb reverb;
    DV drums[48];
    int pendingOff[kLayers] { -1, -1, -1, -1, -1, -1, -1, -1 }, pendingOffSamples[kLayers] {};

    void startDrum(const gb::Sound* s, int layer, float semis, float vel, int at) {
        if (!s || s->data.empty()) return;
        DV* pick = &drums[0]; for (auto& d : drums) { if (!d.s) { pick = &d; break; } if (d.pos > pick->pos) pick = &d; }
        *pick = { s, 0, std::exp2(juce::jlimit(-24.0f, 24.0f, semis) / 12.0) * s->rate / sr, 0.3f + 0.7f * vel, layer, at };
    }
    void renderDrums(juce::AudioBuffer<float>& buf, int layer, int n) {
        float* a = buf.getWritePointer(0); float* b = buf.getWritePointer(1);
        for (auto& d : drums) {
            if (!d.s || d.layer != layer) continue;
            const int len = (int) d.s->data.size();
            for (int i = d.delay; i < n; ++i) {
                if (d.pos >= len - 1) { d.s = nullptr; break; }
                const int k = (int) d.pos; const float t = (float) (d.pos - k);
                const float x = (d.s->data[(size_t) k] + (d.s->data[(size_t) k + 1] - d.s->data[(size_t) k]) * t) * d.gain * 0.7f;
                a[i] += x; b[i] += x; d.pos += d.rate;
            }
            d.delay = 0;
        }
    }
};

} // namespace sp
