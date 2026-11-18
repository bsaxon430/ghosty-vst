#pragma once
// Ghosty Spectrum 2: paint a loop on the frequency spectrum.
//   Layers (max 8): a synth sound (a Poltergeist patch), a drum (Ghosty Beats library) or your own sample.
//   Notes: placed on the canvas; x = time in the loop, y = frequency (pitch).
// Audio thread reads an immutable SpecSnapshot; per-layer Poltergeist engines + a drum voice pool.
#include "Summon.h"        // Poltergeist: patches, Summon, engine Snap
#include "Engine.h"
#include "DrumKit.h"       // Ghosty Beats 2 drum library
#include "SpecScale.h"     // scales + snap-to-key
#include "SpecPad.h"       // the PAD painter's voices + cinematic pad effects
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
// a painted pad dot: tone = x (0 pure .. 1 bright/driven), pitch = y, voice = the brush it was painted with
struct PadDot { float tone = 0.3f, pitch = 60, gain = 0.8f; int voice = 0; uint32_t id = 0; int slot = -1; };
constexpr int kPatterns = 8;
struct Pattern { std::vector<Note> notes; std::vector<PadDot> pads; int bars = 2; bool used() const { return !notes.empty() || !pads.empty(); } };
struct Model {
    std::vector<Layer> layers;                 // the sounds (shared by every pattern)
    std::vector<Note> notes; std::vector<PadDot> pads; int bars = 2;   // the pattern being edited (lives here while current)
    int root = 0, scale = 1; bool snap = true;
    bool quantize = true; int gridDiv = 16;    // note placement grid: 4 = 1/4, 8 = 1/8, 16 = 1/16
    int mode = 0, brush = 0;                   // canvas mode: 0 PAD painter, 1 LOOP; brush = pad voice type
    int current = 0; std::array<Pattern, kPatterns> patterns;   // A..H (the current one is stored in notes/pads/bars)
    void storeCurrent() { auto& P = patterns[(size_t) current]; P.notes = notes; P.pads = pads; P.bars = bars; }
};

inline float midiToHz(float m) { return 440.0f * std::exp2((m - 69.0f) / 12.0f); }
inline float hzToMidi(float hz) { return 69.0f + 12.0f * std::log2(juce::jmax(1.0f, hz) / 440.0f); }
// scaleSteps / scaleNames / snapPitch live in SpecScale.h (shared with the SPECTRAL LAB)

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
        layerBuf.setSize(kLayers * 2, maxBlock); sendBuf.setSize(2, maxBlock); padTmp.setSize(2, maxBlock);
        for (auto& m : midi) m.ensureSize(4096);
        juce::dsp::ProcessSpec spec { rate, (juce::uint32) maxBlock, 2 };
        reverb.prepare(spec); juce::dsp::Reverb::Parameters rp; rp.roomSize = 0.82f; rp.damping = 0.4f; rp.wetLevel = 1.0f; rp.dryLevel = 0.0f; rp.width = 1.0f; reverb.setParameters(rp);
        for (auto& v : drums) v = DV();
        pad.prepare(rate, maxBlock);
        clock = 0;
    }
    void setSnapshot(const SpecSnapshot* s) { snap.store(s); }
    gs::Engine& engine(int i) { return engines[(size_t) i]; }
    bool playing = false;                         // internal transport (when the DAW is stopped)
    std::atomic<bool> padStopped { false };       // STOP: the pad fades out even with HOLD on (any new touch / key / DAW play starts it again)
    std::atomic<float> uiBeat { -1 };             // loop position for the playhead
    // analyser feed: the mono output, read by the UI for the live spectrum
    static constexpr int kRing = 8192; float ring[kRing] {}; std::atomic<int> ringPos { 0 };
    // ---- PAD painter
    PadSlot padSlots[kMaxDots];                   // written by the UI, read here
    PadParams padParams;                          // set by the processor right before process() (audio thread)
    float padTranspose = 0; bool padMidiGate = false; float padMidiStrike = 0;
    int padVoices() const { return pad.activeVoices(); }
    // ---- input (sidechain) feed for DETECT KEY: mono, read by the message thread
    float inRing[kRing] {}; std::atomic<int> inPos { 0 };
    void pushInput(const float* l, const float* r, int n) {
        int p = inPos.load(std::memory_order_relaxed);
        for (int i = 0; i < n; ++i) { inRing[p] = r ? (l[i] + r[i]) * 0.5f : l[i]; p = (p + 1) % kRing; }
        inPos.store(p, std::memory_order_release);
    }
    double rate() const { return sr; }
    // audition (bank clicks): a note on a layer index or a drum sound, from the message thread
    std::atomic<int> auditionLayer { -1 }; std::atomic<int> auditionNote { 60 }; std::atomic<const gb::Sound*> auditionDrum { nullptr };

    // a host may (against the contract) send a block larger than prepared: work through it in prepared-size pieces
    // gate (PLAY: ON NOTES): per-sample gain for what plays by itself from the transport - the PAINT LOOP layers always, the pad only
    // while HOLD is on (gatePad); nullptr = no gate (exactly the classic path)
    void process(juce::AudioBuffer<float>& out, double hostBpm, double hostPpq, bool hostPlaying, double fallbackBpm, const float* gate = nullptr, bool gatePad = false) {
        const int total = out.getNumSamples();
        if (total <= maxBlock || out.getNumChannels() < 2) { processChunk(out, hostBpm, hostPpq, hostPlaying, fallbackBpm, gate, gatePad); return; }
        const double bpm = hostPlaying ? hostBpm : fallbackBpm, beatsPerSample = juce::jmax(20.0, bpm) / 60.0 / sr;
        for (int start = 0; start < total; start += maxBlock) {
            const int len = juce::jmin(maxBlock, total - start);
            juce::AudioBuffer<float> view(out.getArrayOfWritePointers(), 2, start, len);
            processChunk(view, hostBpm, hostPpq + start * beatsPerSample, hostPlaying, fallbackBpm, gate != nullptr ? gate + start : nullptr, gatePad);
        }
    }
    // BYPASS engaged (audio thread): every layer gets all-notes-off, drum hits and the pad stop
    void panic() { panicPending = true; padStopped.store(true); }
    void processChunk(juce::AudioBuffer<float>& out, double hostBpm, double hostPpq, bool hostPlaying, double fallbackBpm, const float* gate = nullptr, bool gatePad = false) {
        const int n = out.getNumSamples(); out.clear();
        const SpecSnapshot* S = snap.load(); if (!S) return;
        const double bpm = hostPlaying ? hostBpm : fallbackBpm, spb = sr * 60.0 / juce::jmax(20.0, bpm);
        const bool run = hostPlaying || playing;
        const double b0 = hostPlaying ? hostPpq : clock, b1 = b0 + n / spb, L = S->bars * 4.0;
        for (auto& m : midi) m.clear();
        if (panicPending) { panicPending = false; for (auto& m : midi) m.addEvent(juce::MidiMessage::allNotesOff(1), 0); for (auto& v : drums) v = DV(); for (auto& o : pendingOff) o = -1; }
        int restrikeAt = -1;
        if (run) {
            const double wrap = std::ceil(b0 / L - 1e-9) * L;
            if (!wasRunning) restrikeAt = 0; else if (wrap < b1) restrikeAt = juce::jlimit(0, n - 1, (int) ((wrap - b0) * spb));
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
        if (gate != nullptr) for (int i = 0; i < n; ++i) { const float g = gate[i]; L0[i] *= g; R0[i] *= g; sendBuf.getWritePointer(0)[i] *= g; sendBuf.getWritePointer(1)[i] *= g; }   // the PAINT LOOP follows the gate
        renderDrums(out, -1, n);   // auditioned bank drums go straight out
        { auto pp = padParams; pp.bpm = bpm; const bool stop = padStopped.load(std::memory_order_relaxed); if (stop) pp.freeze = false;   // a frozen tail would hang forever
          const bool gp = gate != nullptr && gatePad;   // HOLD: the pad plays by itself, so it follows the gate too
          if (gp) { padTmp.copyFrom(0, 0, L0, n); padTmp.copyFrom(1, 0, R0, n); }
          pad.process(L0, R0, n, padSlots, pp, padTranspose, (pp.hold || padMidiGate) && ! stop, restrikeAt, padMidiStrike); padMidiStrike = 0;
          if (gp) { const float* a = padTmp.getReadPointer(0); const float* b = padTmp.getReadPointer(1); for (int i = 0; i < n; ++i) { L0[i] = a[i] + (L0[i] - a[i]) * gate[i]; R0[i] = b[i] + (R0[i] - b[i]) * gate[i]; } } }
        juce::dsp::AudioBlock<float> sb(sendBuf.getArrayOfWritePointers(), 2, (size_t) n); reverb.process(juce::dsp::ProcessContextReplacing<float>(sb));
        for (int i = 0; i < n; ++i) {
            L0[i] = std::tanh((L0[i] + sendBuf.getSample(0, i) * 0.6f) * 0.9f); R0[i] = std::tanh((R0[i] + sendBuf.getSample(1, i) * 0.6f) * 0.9f);
        }
        { int rp = ringPos.load(std::memory_order_relaxed); for (int i = 0; i < n; ++i) { ring[rp] = (L0[i] + R0[i]) * 0.5f; rp = (rp + 1) % kRing; } ringPos.store(rp, std::memory_order_release); }
    }

private:
    struct DV { const gb::Sound* s = nullptr; double pos = 0, rate = 1; float gain = 1; int layer = 0, delay = 0; };
    struct TP { float s1[2] {}, s2[2] {}, a1 = 0, a2 = 0, a3 = 0;
        void setup(float f, float rate) { const float g = std::tan(juce::MathConstants<float>::pi * juce::jlimit(20.0f, rate * 0.45f, f) / rate); a1 = 1 / (1 + g * (g + 1.4f)); a2 = g * a1; a3 = g * a2; }
        float tick(int c, float x, bool lowpass) { const float v3 = x - s2[c], v1 = a1 * s1[c] + a2 * v3, v2 = s2[c] + a2 * s1[c] + a3 * v3; s1[c] = 2 * v1 - s1[c]; s2[c] = 2 * v2 - s2[c]; return lowpass ? v2 : x - 1.4f * v1 - v2; } };
    double sr = 44100, clock = 0; int maxBlock = 512; bool wasRunning = false, panicPending = false;
    std::atomic<const SpecSnapshot*> snap { nullptr };
    std::array<gs::Engine, kLayers> engines;
    PadSynth pad;
    std::array<juce::MidiBuffer, kLayers> midi;
    juce::AudioBuffer<float> layerBuf, sendBuf, padTmp;
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
