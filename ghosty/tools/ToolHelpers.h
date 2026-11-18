#pragma once
// Shared by the tool and the tests: play notes through Ghosty Pro's processor (as a host would), write WAVs, measure.
#include "JuceHeader.h"
#include "GhostyProcessor.h"
#include <vector>

namespace ghosty::tool {

struct Note { double start = 0, length = 1; int note = 60; int velocity = 100; };

// a host's transport: playing from beat 0 at 'bpm' (play() moves it along)
struct HostClock : public AudioPlayHead {
  double bpm = 120.0, rate = 48000.0; int64 samples = 0; bool playing = true;
  bool getCurrentPosition(CurrentPositionInfo& info) override {
    info.resetToDefault();
    info.bpm = bpm; info.isPlaying = playing; info.timeInSamples = samples;
    info.timeInSeconds = samples / rate; info.ppqPosition = info.timeInSeconds * bpm / 60.0;
    info.timeSigNumerator = 4; info.timeSigDenominator = 4;
    return true;
  }
};

// plays notes through processBlock (what a DAW does) at 'rate', 'block' samples at a time
inline AudioBuffer<float> play(GhostyProcessor& p, const std::vector<Note>& notes, double seconds, double rate = 44100.0,
                               int block = 512, bool vitalOnly = false, HostClock* clock = nullptr,
                               std::function<void(int)> beforeBlock = nullptr) {
  p.setPlayConfigDetails(0, 2, rate, block);
  p.prepareToPlay(rate, block);
  if (clock) { clock->rate = rate; clock->samples = 0; p.setPlayHead(clock); }
  const int total = (int) (seconds * rate);
  AudioBuffer<float> out(2, total);
  out.clear();
  AudioBuffer<float> buf(2, block);
  for (int pos = 0; pos < total; pos += block) {
    const int n = jmin(block, total - pos);
    buf.setSize(2, n, false, false, true);
    buf.clear();
    MidiBuffer midi;
    for (auto& nt : notes) {
      const int on = (int) (nt.start * rate), off = (int) ((nt.start + nt.length) * rate);
      if (on >= pos && on < pos + n) midi.addEvent(MidiMessage::noteOn(1, nt.note, (uint8) nt.velocity), on - pos);
      if (off >= pos && off < pos + n) midi.addEvent(MidiMessage::noteOff(1, nt.note), off - pos);
    }
    if (beforeBlock) beforeBlock(pos);
    if (vitalOnly) p.processBlockVitalOnly(buf, midi);
    else p.processBlock(buf, midi);
    if (clock) clock->samples += n;
    for (int c = 0; c < 2; ++c) out.copyFrom(c, pos, buf, c, 0, n);
  }
  if (clock) p.setPlayHead(nullptr);
  return out;
}

inline bool writeWav(const File& f, const AudioBuffer<float>& b, double rate, int bits = 24) {
  f.deleteFile();
  std::unique_ptr<FileOutputStream> s(f.createOutputStream());
  if (!s) return false;
  WavAudioFormat wav;
  std::unique_ptr<AudioFormatWriter> w(wav.createWriterFor(s.get(), rate, (unsigned) b.getNumChannels(), bits, {}, 0));
  if (!w) return false;
  s.release();
  return w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
}

inline bool readWav(const File& f, AudioBuffer<float>& b, double& rate) {
  WavAudioFormat wav;
  std::unique_ptr<AudioFormatReader> r(wav.createReaderFor(new FileInputStream(f), true));
  if (!r) return false;
  b.setSize((int) r->numChannels, (int) r->lengthInSamples);
  r->read(&b, 0, (int) r->lengthInSamples, 0, true, true);
  rate = r->sampleRate;
  return true;
}

// true peak: 4x oversampled with a windowed-sinc interpolator (as BS.1770's annex: inter-sample peaks), in dBTP
inline float truePeakDb(const AudioBuffer<float>& b) {
  constexpr int kOver = 4, kTaps = 16;
  float peak = 0.0f;
  for (int c = 0; c < b.getNumChannels(); ++c) {
    const float* x = b.getReadPointer(c);
    const int n = b.getNumSamples();
    for (int i = 0; i < n; ++i) {
      peak = jmax(peak, std::abs(x[i]));
      for (int k = 1; k < kOver; ++k) {
        const double t = (double) k / kOver;
        double acc = 0.0;
        for (int j = -kTaps + 1; j <= kTaps; ++j) {
          const int idx = i + j;
          if (idx < 0 || idx >= n) continue;
          const double d = t - j;
          const double sinc = std::sin(MathConstants<double>::pi * d) / (MathConstants<double>::pi * d);
          const double win = 0.5 + 0.5 * std::cos(MathConstants<double>::pi * d / kTaps);
          acc += x[idx] * sinc * win;
        }
        peak = jmax(peak, (float) std::abs(acc));
      }
    }
  }
  return Decibels::gainToDecibels(peak, -200.0f);
}

inline float rmsDb(const AudioBuffer<float>& b) {
  double s = 0; int n = 0;
  for (int c = 0; c < b.getNumChannels(); ++c) for (int i = 0; i < b.getNumSamples(); ++i) { s += b.getSample(c, i) * (double) b.getSample(c, i); ++n; }
  return Decibels::gainToDecibels((float) std::sqrt(s / jmax(1, n)), -200.0f);
}

inline bool finite(const AudioBuffer<float>& b) {
  for (int c = 0; c < b.getNumChannels(); ++c) for (int i = 0; i < b.getNumSamples(); ++i) if (!std::isfinite(b.getSample(c, i))) return false;
  return true;
}

// clicks: a sample-to-sample jump far above what the signal's own motion explains (second difference vs its local level)
inline int countClicks(const AudioBuffer<float>& b, float ratio = 40.0f, int* where = nullptr) {
  int clicks = 0;
  const int win = 256;
  for (int c = 0; c < b.getNumChannels(); ++c) {
    const float* x = b.getReadPointer(c);
    const int n = b.getNumSamples();
    std::vector<float> d2((size_t) n, 0.0f);
    for (int i = 2; i < n; ++i) d2[(size_t) i] = std::abs(x[i] - 2 * x[i - 1] + x[i - 2]);
    double acc = 0;
    for (int i = 2; i < n; ++i) {
      acc += d2[(size_t) i];
      if (i - win >= 2) acc -= d2[(size_t) (i - win)];
      const float local = (float) (acc / win);
      if (i > win && d2[(size_t) i] > 0.02f && d2[(size_t) i] > ratio * (local + 1.0e-4f)) { if (where && clicks == 0) *where = i; ++clicks; i += win; }
    }
  }
  return clicks;
}

} // namespace ghosty::tool
