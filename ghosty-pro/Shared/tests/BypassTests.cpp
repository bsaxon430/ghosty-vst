// Tests Shared/Bypass.h: the fade (no clicks, exact when off), the engage edge, silence, dry/wet crossfade, and the button <-> parameter link.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "Bypass.h"
#include <cmath>
#include <cstdio>

static int fails = 0;
#define CHECK(c) do { if (c) std::printf("  ok    %s\n", #c); else { std::printf("  FAIL  %s\n", #c); ++fails; } } while (0)

static juce::AudioBuffer<float> ones(int n, float v = 1.0f, int ch = 2) { juce::AudioBuffer<float> b(ch, n); for (int c = 0; c < ch; ++c) for (int i = 0; i < n; ++i) b.setSample(c, i, v); return b; }

int main() {
    juce::ScopedJuceInitialiser_GUI init;
    const double sr = 48000.0; const int ramp = (int) std::ceil(0.006 * sr);

    std::printf("playing and settled: bit-exact no-op\n");
    { ghosty::BypassFader f; f.prepare(sr, 0.006); f.set(false);
      auto b = ones(512, 0.7f); f.applyMute(b);
      bool exact = true; for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) exact &= b.getSample(c, i) == 0.7f;
      CHECK(exact); CHECK(! f.silent()); CHECK(! f.isBypassed()); CHECK(! f.takeEngageEdge()); }

    std::printf("engage: one edge, smooth fade to silence within ~6 ms, no click\n");
    { ghosty::BypassFader f; f.prepare(sr, 0.006); f.set(true);
      CHECK(f.takeEngageEdge()); CHECK(! f.takeEngageEdge());               // exactly once
      f.set(true); CHECK(! f.takeEngageEdge());                             // staying bypassed is not a new edge
      auto b = ones(2 * ramp); f.applyMute(b);
      float maxStep = 0, prev = 1; bool mono = true;
      for (int i = 0; i < b.getNumSamples(); ++i) { const float v = b.getSample(0, i); maxStep = juce::jmax(maxStep, std::abs(prev - v)); mono &= v <= prev + 1e-6f; prev = v; }
      std::printf("        biggest step %.5f (a hard cut would be 1.0), ramp %d samples\n", maxStep, ramp);
      CHECK(maxStep < 2.5f / (float) ramp); CHECK(mono);
      CHECK(b.getSample(0, 2 * ramp - 1) == 0.0f && b.getSample(1, 2 * ramp - 1) == 0.0f);
      CHECK(f.silent()); }

    std::printf("release: fades back in without a click\n");
    { ghosty::BypassFader f; f.prepare(sr, 0.006, true); CHECK(f.silent());   // starting bypassed snaps, no burst
      f.set(false); CHECK(! f.silent());
      auto b = ones(2 * ramp); f.applyMute(b);
      float maxStep = 0, prev = 0; for (int i = 0; i < b.getNumSamples(); ++i) { const float v = b.getSample(0, i); maxStep = juce::jmax(maxStep, std::abs(v - prev)); prev = v; }
      CHECK(maxStep < 2.5f / (float) ramp); CHECK(b.getSample(0, 2 * ramp - 1) == 1.0f); CHECK(! f.takeEngageEdge()); }

    std::printf("effect: crossfade processed <-> dry\n");
    { ghosty::BypassFader f; f.prepare(sr, 0.010);
      auto dry = ones(4096, 0.5f), wet = ones(4096, 1.0f);
      f.applyDryWet(wet, dry); CHECK(wet.getSample(0, 100) == 1.0f);          // not bypassed: processed untouched
      f.set(true); wet = ones(4096, 1.0f); f.applyDryWet(wet, dry);
      CHECK(wet.getSample(0, 4095) == 0.5f && wet.getSample(1, 4095) == 0.5f);  // fully bypassed: exactly the dry signal
      const float mid = wet.getSample(0, (int) (0.005 * sr)); CHECK(mid > 0.5f && mid < 1.0f);   // in between while fading
      f.set(false); wet = ones(4096, 1.0f); f.applyDryWet(wet, dry); CHECK(wet.getSample(0, 4095) == 1.0f); }

    std::printf("button follows the parameter and writes it back\n");
    { juce::AudioParameterBool p(juce::ParameterID { "bypass", 1 }, "Bypass", false);
      ghosty::BypassButton btn(p); btn.setBounds(0, 0, 84, 26);
      CHECK(! btn.getToggleState());
      btn.triggerClick();
      std::printf("        after click: toggle=%d param=%d (pump the message loop and look again)\n", (int) btn.getToggleState(), (int) p.get());
      juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
      std::printf("        after pump:  toggle=%d param=%d\n", (int) btn.getToggleState(), (int) p.get());
      CHECK(p.get());                                                          // click -> parameter on
      p.setValueNotifyingHost(0.0f);
      juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
      CHECK(! btn.getToggleState());                                           // host switched it off -> button follows
      p.setValueNotifyingHost(1.0f);
      juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
      CHECK(btn.getToggleState());
      juce::Image img(juce::Image::ARGB, 84, 26, true); juce::Graphics g(img); btn.paintEntireComponent(g, true); btn.setDark(false); CHECK(true); }

    std::printf("%s (%d failures)\n", fails ? "FAILED" : "ALL PASS", fails);
    return fails ? 1 : 0;
}
