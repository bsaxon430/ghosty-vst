/*
 * Ghost Bass b11 - the SUB layer engine that sits next to Surge.
 * Copyright 2026 the Ghost Bass authors. GPL-3 (Ghost Bass is a Surge XT fork).
 *
 *   SubEngine  = Bombaz   https://github.com/hollance/bombaz     (GPL-3.0, Matthijs Hollemans)
 *
 * It runs the upstream DSP classes UNCHANGED: the upstream source files are compiled as they are from the pinned
 * submodule in libs/ghostbass-layers (BombazUnity.cpp only wraps them in a namespace so their
 * class names, like "Oscillator", cannot meet Surge's). Our code is the host: it feeds MIDI, calls the same
 * render sequence as the upstream plug-in's processBlock, and exposes the engines' own parameters to the UI.
 * No header of the upstream projects is included here, so this file is safe to include anywhere.
 */
#ifndef GHOSTBASS_SRC_GHOST_LAYERS_LAYERENGINES_H
#define GHOSTBASS_SRC_GHOST_LAYERS_LAYERENGINES_H

#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <cstdint>

namespace ghost::layers
{
// the engines' own parameters live in a private AudioProcessorValueTreeState (not shown to the host as separate params)
class EngineBase
{
  public:
    virtual ~EngineBase() = default;
    virtual juce::AudioProcessorValueTreeState &apvts() = 0;
    // original control names / ids and their group, in upstream order
    juce::RangedAudioParameter *param(const juce::String &id) { return apvts().getParameter(id); }
    juce::String stateToString();
    bool stateFromString(const juce::String &xml);
    void setToDefaults();
    bool isDefault();
};

// SUB = Bombaz. Ids: width (X of the pad), drive (Y of the pad), octave (-2 / -1 / 0), tuning, glideTime
class SubEngine : public EngineBase
{
  public:
    SubEngine();
    ~SubEngine() override;
    void prepare(double sampleRate, int maxBlock); // = upstream prepareToPlay
    void reset();
    void midi(uint8_t d0, uint8_t d1, uint8_t d2); // = upstream midiMessage
    void render(float *mono, int n);               // = upstream update() + Synth::render
    juce::AudioProcessorValueTreeState &apvts() override;
    struct Impl;

  private:
    std::unique_ptr<Impl> impl;
};

} // namespace ghost::layers

#endif
