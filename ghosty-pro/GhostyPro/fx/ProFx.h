#pragma once
// Ghosty Pro "Ghost FX rack": the INTERFACE CONTRACT between the synth (GhostyPro/*) and the FX rack (GhostyPro/fx/*).
// The FX engineer owns GhostyPro/fx/ and implements everything declared here (may add private members / extra files in
// fx/ freely). The synth engineer only calls what's declared here. Do not change these signatures without telling the
// other side.
//
// Units in the rack (original designs; never copy names, graphics or code of other products):
//   GHOST GATE  - pattern-based volume gate + time-repeat / half-speed / reverse slices (variable-speed tape-style), 16 patterns
//   HAUNT EDIT  - performance stutter: hold a pad to stutter/gate/glitch with tempo-synced sizes
//   ECTO ECHO   - character delay: TAPE / ANALOG / BBD / DIGITAL modes, wow-flutter, saturation, ping-pong, sync
//   SPIRIT EQ   - visual parametric EQ / filters with a live spectrum (pro-EQ-style polish)
//   CRYSTAL     - granular reverse-echo / pitch-shifted splices, glassy and ambient
//   WRECKER     - multi-mode destruction: tube, tape, fuzz, wavefold, bitcrush, decimate, with tone + mix
//   plus CHORUS, GHOST SQUEEZE (3-band upward/downward comp, parameter key "ott"), SHIMMER REVERB, and a final GHOST LIMITER.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

namespace gpfx {

#if GPFX_BOX
struct Transport { double sampleRate = 48000, bpm = 120, ppq = 0; bool playing = false; int noteOns = 0; int noteOnAt = -1; double beatsPerBar = 4.0; };   // beatsPerBar: the host's time signature (GHOST MOTION timeline); noteOns / noteOnAt: Ghosty Pro, this block's note-ons and the first one's sample (HYPER RETRIG)
#else
struct Transport { double sampleRate = 48000, bpm = 120, ppq = 0; bool playing = false; int noteOns = 0; int noteOnAt = -1; int lastNote = -1; };   // lastNote (FX RACK round 3): this block's newest note-on, -1 none
#endif

class Rack {
public:
    Rack();
    ~Rack();
    // Adds every FX parameter to the plugin's layout. All IDs start with "fx_". Called once from the processor's layout().
    static void addParameters(juce::AudioProcessorValueTreeState::ParameterLayout& layout);
    // Ghosty Pro only (no-op in Ghost Box): parameters of units added later (FATTEN). Call at the VERY END of the processor's layout().
    static void addLateParameters(juce::AudioProcessorValueTreeState::ParameterLayout& layout);
    // Caches raw parameter pointers (call once after the APVTS exists).
    void attach(juce::AudioProcessorValueTreeState& apvts);
    void prepare(double sampleRate, int maxBlockSize);
    void reset();
    // Processes the synth's stereo output in place (audio thread: no allocation, no locks).
    void process(juce::AudioBuffer<float>& buffer, const Transport& t);
    int latencySamples() const;
    // CLEAN SOUND (Ghosty Pro): auto-gain on the drive units (WRECKER / TUBE / SOUL CRUSH / FATTEN) - never louder than their input.
    // Off by default (Ghost Box never turns it on). Audio thread, once per block.
    void setCleanMatch (bool on) noexcept;
#if GPFX_BOX
    // Ghost Box only: the external sidechain for this block (nullptr = not connected) and the host's offline (bounce) state
    void setSidechain (const float* left, const float* right, int numSamples);
    void setOffline (bool isOffline);
#endif
    // Non-parameter state (gate patterns, user EQ curve extras...) saved inside the plugin state.
    juce::ValueTree saveState() const;
    void loadState(const juce::ValueTree& tree);
    // Call after the HOST restored the plugin state (setStateInformation): the next block starts every on / mix / duck smoother at
    // its target, so a reopened project's first beat sounds like the later ones. Not for preset / undo loads while audio runs
    // (those are ducked). The rack also snaps by itself when the transport starts (stopped -> playing) or relocates after an idle gap.
    void snapOnNextBlock();
    // Applies a named FX scene for presets: "" or "dry" = everything off. The synth's presets call this.
    // Must at least support: dry, wide, tape, dub, shimmer, gate8, gate16, halftime, stutter, lofi, ott, space, crystal, wreck.
    void applyScene(juce::AudioProcessorValueTreeState& apvts, const juce::String& scene);
    struct Impl; std::unique_ptr<Impl> impl;
};

// The FX page. The synth's editor shows it in its "FX" tab, sized to the tab area (design for about 1100 x 560).
// It must look like the Ghosty family: dark (0xff0B0C10), ghost palette from Shared/GhostArt.h, trippy ghost touches.
class Panel : public juce::Component {
public:
    Panel(juce::AudioProcessorValueTreeState& apvts, Rack& rack);
    ~Panel() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    // DARK / LIGHT (fx/FxTheme.h): call fxtheme::set (isLight) first, then this: re-applies the look-and-feel and every stored colour
    void themeChanged();
#if GPFX_BOX
    void paintGhostSurfaces (juce::Graphics&);   // GHOST (Ghost Box): the static glass panels (scene bar, card, macro bar), without the texts
    juce::String ghostStaticKey() const;         // their layout (the window caches them with its aurora as one image)
    std::vector<std::pair<juce::Rectangle<int>, juce::Colour>> ghostTexts();   // where the panel paints its own texts (+ colour)
#endif
    struct Impl; std::unique_ptr<Impl> impl;
};

} // namespace gpfx
