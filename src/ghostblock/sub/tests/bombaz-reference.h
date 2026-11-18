// Poltergeist 2 (GhostBlock) - SUB checks: upstream Bombaz itself, behind a plain interface.
// bombaz-reference.cpp compiles upstream's Parameters.cpp and Synth.cpp (with Voice.h, Oscillator.h, DCBlockFilter.h,
// DSP.h) exactly as they are, against real JUCE, and plays them the way upstream's plugin does: parameters through a
// juce::AudioProcessorValueTreeState, notes as MIDI bytes.
#pragma once
#include <cstdint>
#include <memory>

namespace p2sub_ref
{
class BombazRef
{
  public:
    BombazRef();
    ~BombazRef();
    void prepare(float sampleRate); // Parameters::prepareToPlay + reset, Synth::prepareToPlay + reset
    // the plugin's parameters, in their own units; octave: 0 = "-2", 1 = "-1", 2 = "0"
    void set(float widthPercent, float drivePercent, int octaveIndex, float tuneCents, float glideMs);
    void midi(uint8_t d0, uint8_t d1, uint8_t d2); // Synth::midiMessage
    void update();                                // Parameters::update (the plugin calls it once per block)
    void render(float *out, int n);               // Synth::render
    // what Parameters::update made of them (read by the check to feed our voice the same numbers)
    float width() const;           // widthParam->get()
    float drive() const;           // driveParam->get()
    float glideMs() const;         // glideTimeParam->get()
    int transpose() const;         // semitones
    float pitchModulation() const; // pitch bend x tuning

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace p2sub_ref
