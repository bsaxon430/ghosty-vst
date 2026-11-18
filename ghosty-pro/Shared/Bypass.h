#pragma once
// BYPASS, shared by every GhostBlock plugin so they all look and behave the same.
//
//   instruments (Mickens, Poltergeist, Beats, Spectrum, ArpEZator, Ghosty Pro): BYPASS mutes the whole plugin.
//        ~6 ms fade out, then silence and no CPU; sounding notes are stopped (and note-offs sent where the plugin has MIDI out);
//        incoming MIDI is ignored while bypassed; fades back in when released.
//   effects (Ghost Box): BYPASS passes the unprocessed input through, time-aligned with the processed path (the plugin's latency),
//        crossfaded over ~10 ms, so the track keeps playing exactly as it did without the effect.
//
// The button sits top-right in every plugin's header. The state is a normal automatable host parameter named "Bypass" (id "bypass"), and
// the plugin returns it from getBypassParameter(), so the host's own bypass button drives the same thing. It is NEVER part of a preset,
// a randomise, or an INIT/reset of the sound, and it defaults to off (a session saved before this existed loads with bypass off).
//
// WIRING (processor)
//   1. Add the parameter LAST in the parameter list (so existing automation keeps its index):
//        bypassParam = new juce::AudioParameterBool(juce::ParameterID { "bypass", 1 }, "Bypass", false);   // or the APVTS equivalent
//   2. juce::AudioProcessorParameter* getBypassParameter() const override { return bypassParam; }
//   3. member:  ghosty::BypassFader bypass;     prepareToPlay():  bypass.prepare(sampleRate, 0.006, bypassParam->get());
//   4. instruments, at the top of processBlock():
//        bypass.set(bypassParam->get());
//        if (bypass.takeEngageEdge()) { /* stop all voices / pending notes; send note-offs on any MIDI output */ }
//        if (bypass.silent()) { buffer.clear(); midi.clear(); return; }          // fully bypassed: nothing runs, MIDI input ignored
//        ... render as usual ...
//        bypass.applyMute(buffer);                                                // the fade on engage / release
//      effects: render the processed block into 'buffer' and keep a copy of the input delayed by the plugin's latency in 'dry', then
//        bypass.set(...); bypass.applyDryWet(buffer, dry);
//   5. Editor: add the button top-right:  ghosty::BypassButton bypassButton { *processor.bypassParam };  (about 84 x 26 px), call
//        bypassButton.setDark(isDark) when the theme changes.
//   6. No allocation, no locks: everything here is safe on the audio thread.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "GhostThemes.h"
#include <utility>

namespace ghosty {

class BypassFader {
public:
    // rampSeconds: length of the fade (about 6 ms for muting, use 0.010 for the effect's dry/wet crossfade); startBypassed snaps with no fade
    void prepare(double sampleRate, double rampSeconds = 0.006, bool startBypassed = false) {
        gain.reset(sampleRate, rampSeconds);
        target = startBypassed; engagePending = false;
        gain.setCurrentAndTargetValue(startBypassed ? 0.0f : 1.0f);
    }
    // call once per block with the parameter's current value
    void set(bool bypassed) {
        if (bypassed == target) return;
        target = bypassed;
        if (bypassed) engagePending = true;
        gain.setTargetValue(bypassed ? 0.0f : 1.0f);
    }
    // true exactly once after BYPASS was switched on: stop voices / send note-offs now
    bool takeEngageEdge() { return std::exchange(engagePending, false); }
    bool isBypassed() const { return target; }
    // fully faded out: the instrument can skip rendering completely
    bool silent() const { return target && gain.getCurrentValue() <= 1.0e-5f; }

    // instruments: multiply the rendered block by the fade (1 = playing, 0 = bypassed); bit-exact no-op while playing and settled
    void applyMute(juce::AudioBuffer<float>& b) {
        if (! target && ! gain.isSmoothing()) return;
        const int n = b.getNumSamples(), nch = b.getNumChannels();
        for (int i = 0; i < n; ++i) {
            const float g = gain.getNextValue();
            for (int c = 0; c < nch; ++c) b.getWritePointer(c)[i] *= g;
        }
    }
    // effects: b holds the processed block, dry the unprocessed input delayed by the plugin's latency; result = crossfade between them
    void applyDryWet(juce::AudioBuffer<float>& b, const juce::AudioBuffer<float>& dry) {
        if (! target && ! gain.isSmoothing()) return;
        const int n = b.getNumSamples(), nch = juce::jmin(b.getNumChannels(), dry.getNumChannels());
        for (int i = 0; i < n; ++i) {
            const float g = gain.getNextValue();                       // 1 = processed, 0 = dry
            for (int c = 0; c < nch; ++c) { float* w = b.getWritePointer(c); w[i] = w[i] * g + dry.getReadPointer(c)[i] * (1.0f - g); }
        }
    }

private:
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> gain { 1.0f };
    bool target = false, engagePending = false;
};

// the button: toggles the "Bypass" parameter (one undoable host gesture per click), follows host automation and the host's own bypass button
class BypassButton : public juce::Button {
public:
    explicit BypassButton(juce::RangedAudioParameter& p)
        : juce::Button("BYPASS"), attach(p, [this](float v) { setToggleState(v > 0.5f, juce::dontSendNotification); repaint(); }) {
        setClickingTogglesState(true);
        setTooltip("BYPASS: mute the whole plugin (instruments) / pass the sound through unprocessed (Ghost Box). Click again to turn it back on.");
        attach.sendInitialUpdate();
    }
    void setDark(bool d) { dark = d; themed = false; custom = false; repaint(); }                         // old API: dark / light look
    void setTheme(themes::Theme t) { theme = t; dark = t != themes::Theme::Light; themed = true; custom = false; repaint(); }
    // a plugin's own look (Ghosty Beats NEON): idle fill, ink, and the "on" accent + its text; setTheme() / setDark() end it
    void setColours(juce::Colour fill, juce::Colour ink, juce::Colour accent, juce::Colour accentInk) { cFill = fill; cInk = ink; cAcc = accent; cAccInk = accentInk; custom = true; repaint(); }   // BLACK / PURPLE / LIGHT tokens
    void clicked() override { attach.setValueAsCompleteGesture(getToggleState() ? 1.0f : 0.0f); }
    void paintButton(juce::Graphics& g, bool over, bool down) override {
        const auto r = getLocalBounds().toFloat().reduced(1.0f);
        const bool on = getToggleState();
        const auto accent = custom ? cAcc : juce::Colour(0xffffb020);                    // amber = the plugin is bypassed
        auto idleFill = dark ? juce::Colour(0x33ffffff) : juce::Colour(0x22000000);
        auto idleText = dark ? juce::Colour(0xffe9e4f5) : juce::Colour(0xff2c2540);
        if (themed) { const auto& p = themes::get(theme); idleFill = p.cell; idleText = p.ink; }   // opaque theme cell + ink (>= 4.5:1)
        if (custom) { idleFill = cFill; idleText = cInk; }
        g.setColour(on ? accent : idleFill.brighter(over ? 0.25f : 0.0f));
        g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
        g.setColour(on ? accent.brighter(0.3f) : idleText.withAlpha(0.55f));
        g.drawRoundedRectangle(r, r.getHeight() * 0.5f, down ? 2.0f : 1.2f);
        g.setColour(on ? (custom ? cAccInk : juce::Colour(0xff201400)) : idleText);
        g.setFont(juce::Font(juce::FontOptions(juce::jmin(14.0f, r.getHeight() * 0.55f), juce::Font::bold)));
        g.drawText(on ? "BYPASSED" : "BYPASS", getLocalBounds(), juce::Justification::centred);
    }
private:
    juce::ParameterAttachment attach;
    bool dark = true, themed = false;
    themes::Theme theme = themes::Theme::Dark;
    bool custom = false; juce::Colour cFill, cInk, cAcc, cAccInk;
};

} // namespace ghosty
