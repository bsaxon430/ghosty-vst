#pragma once
// Ghosty Pro's own effects, for what Vital's effects have no version of. They sit after Vital's whole output and are
// OFF by default (Vital's sound passes untouched while they are off).
#include "JuceHeader.h"
#include <atomic>
#include <memory>

namespace ghosty {

class Extras {
  public:
    Extras();
    ~Extras();
    void prepare(double sample_rate, int max_block);
    // audio thread; does nothing (not even a copy) while every extra is off
    void process(AudioSampleBuffer& buffer, double ppq, double bpm, bool playing) noexcept;
    bool anyOn() const noexcept;

    ValueTree toTree() const;
    void fromTree(const ValueTree& tree);

    // message thread (the FX page)
    void setGateOn(bool on);
    bool gateOn() const;
    void setHauntOn(bool on);
    bool hauntOn() const;
    void setGatePattern(int index);            // 0 .. 15
    int gatePattern() const;
    static StringArray gatePatternNames();
    void setGateLength(int index);             // 0 = 2 beats, 1 = 1 bar, 2 = 2 bars
    int gateLength() const;
    void holdHauntPad(int pad);                // 0 .. 15 while held, -1 = let go
    static StringArray hauntPadNames();

    struct Impl;

  private:
    std::unique_ptr<Impl> impl_;
};

} // namespace ghosty
