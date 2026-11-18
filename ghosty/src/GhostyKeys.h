#pragma once
// KEYS: Ghosty Pro's playing extras in front of Vital's engine: SCALE LOCK, CHORDS, ARPEGGIATOR (tempo-synced to the
// host) and HOLD. They only change the MIDI notes Vital receives; every sound is still Vital's. All OFF by default:
// while they are off (and nothing they made is still sounding) the MIDI goes to Vital untouched.
//
// Source: Ghosty Pro's own Performer (ghosty-pro/GhostyPro/Performer.h on the main branch, our code), ported to
// JUCE 6 and extended with HOLD, dotted rates and a seeded random mode. Before porting we looked for a maintained,
// GPL-compatible arp / chord / scale library to build on: LibreArp (GPL-3) and Arpligner are whole pattern-arp
// plugins with their own editors, not libraries, and neither does chords or scale lock, so our own tested code is used.
//
// Real-time safe: fixed-size arrays only, no allocation on the audio thread.
#include "JuceHeader.h"
#include <array>
#include <atomic>

namespace ghosty {

inline const StringArray& keyNames() { static StringArray k { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; return k; }
inline const StringArray& scaleNames() {
  static StringArray s { "OFF", "MAJOR", "MINOR", "PENTA", "MINOR PENTA", "BLUES", "DORIAN", "MIXOLYDIAN", "LYDIAN", "PHRYGIAN",
                         "HARMONIC MINOR", "JAPANESE", "ARABIC" };
  return s;
}
inline const StringArray& chordNames() { static StringArray c { "OFF", "TRIADS", "7THS", "POWER" }; return c; }
inline const StringArray& arpRateNames() { static StringArray r { "1/4", "1/8", "1/16", "1/32", "1/8 T", "1/16 T", "1/8 D", "1/16 D" }; return r; }
inline const StringArray& arpModeNames() { static StringArray m { "UP", "DOWN", "UP + DOWN", "RANDOM", "AS PLAYED" }; return m; }
inline double arpRateQuarterNotes(int rate) {
  static const double q[8] { 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0, 0.75, 0.375 };
  return q[jlimit(0, 7, rate)];
}

// scale intervals; index matches scaleNames() (0 = off / every note)
inline int scaleMask(int scale) {
  static const int sets[13][12] = {
    { 0,1,2,3,4,5,6,7,8,9,10,11 }, { 0,2,4,5,7,9,11,-1 }, { 0,2,3,5,7,8,10,-1 }, { 0,2,4,7,9,-1 }, { 0,3,5,7,10,-1 }, { 0,3,5,6,7,10,-1 },
    { 0,2,3,5,7,9,10,-1 }, { 0,2,4,5,7,9,10,-1 }, { 0,2,4,6,7,9,11,-1 }, { 0,1,3,5,7,8,10,-1 }, { 0,2,3,5,7,8,11,-1 }, { 0,1,5,7,8,-1 }, { 0,1,4,5,7,8,11,-1 } };
  int mask = 0;
  const int s = jlimit(0, 12, scale);
  for (int i = 0; i < 12; ++i) { const int v = sets[s][i]; if (v < 0) break; mask |= 1 << v; }
  return mask;
}
inline bool inScale(int note, int key, int mask) { return (mask >> (((note - key) % 12 + 12) % 12)) & 1; }
inline int snapToScale(int note, int key, int mask) {
  if (inScale(note, key, mask)) return note;
  for (int d = 1; d < 7; ++d) {
    if (note - d >= 0 && inScale(note - d, key, mask)) return note - d;
    if (note + d < 128 && inScale(note + d, key, mask)) return note + d;
  }
  return note;
}
// the n-th scale step above a note that is already in the scale
inline int scaleStep(int note, int steps, int key, int mask) {
  int n = note, left = steps;
  while (left > 0 && n < 127) { ++n; if (inScale(n, key, mask)) --left; }
  return n;
}

class Keys {
  public:
    static constexpr int kChannel = 16;
    // the controls (message thread writes, audio thread reads)
    std::atomic<int> key { 0 }, scale { 0 }, chord { 0 }, arpRate { 2 }, arpMode { 0 }, arpOctaves { 1 };
    std::atomic<bool> arp { false }, hold { false };
    std::atomic<float> arpGate { 0.7f };
    std::atomic<int> arpCounter { 0 };   // +1 on every arp step (the UI flashes)

    bool anyOn() const { return arp.load() || hold.load() || chord.load() > 0 || scale.load() > 0; }
    // still has notes of its own sounding / held: keep routing MIDI through it until they are released
    bool busy() const { return arpNote_ >= 0 || poolCount_ > 0 || producedCount_ > 0 || latchedCount_ > 0; }

    void prepare(double sampleRate, int capacityBytes) { sr_ = sampleRate; capacity_ = capacityBytes; reset(); }
    void reset() {
      for (auto& p : produced_) p.fill(-1);
      refCount_.fill(0); poolRef_.fill(0); down_.fill(false); latched_.fill(false);
      poolCount_ = orderCount_ = producedCount_ = latchedCount_ = 0;
      arpNote_ = offIn_ = -1; arpWasOn_ = holdWasOn_ = false; seqPos_ = 0; arpPhase_ = 0.0;
      rng_.setSeed(0x6057);
    }

    void process(const MidiBuffer& in, MidiBuffer& out, int numSamples, double bpm, bool hostPlaying, double ppq) {
      out.clear();
      const int sc = scale.load(), k = key.load(), mask = scaleMask(sc);
      const bool arpOn = arp.load(), holdOn = hold.load();
      const double stepQN = arpRateQuarterNotes(arpRate.load());
      bpm = bpm > 0.0 ? bpm : 120.0;
      const double inc = (bpm / 60.0) / sr_ / stepQN;   // steps per sample

      if (arpWasOn_ && !arpOn) {
        // arp off: the held notes sound as plain notes again
        stopArpNote(out, 0);
        poolCount_ = orderCount_ = 0; poolRef_.fill(0);
        for (int s = 0; s < 128; ++s)
          for (auto o : produced_[(size_t) s])
            if (o >= 0 && refCount_[(size_t) o]++ == 0) add(out, MidiMessage::noteOn(1, o, (uint8) lastVelocity_), 0);
      }
      if (!arpWasOn_ && arpOn) {
        // arp on: what is held moves into the arp
        for (int n = 0; n < 128; ++n) if (refCount_[(size_t) n] > 0) { add(out, MidiMessage::noteOff(1, n), 0); refCount_[(size_t) n] = 0; }
        for (int s = 0; s < 128; ++s) for (auto o : produced_[(size_t) s]) if (o >= 0) addToPool(o);
        arpPhase_ = std::floor(arpPhase_) + 1.0;   // first step right now
      }
      arpWasOn_ = arpOn;
      if (holdWasOn_ && !holdOn)   // HOLD off lets every held note go
        for (int s = 0; s < 128; ++s) if (latched_[(size_t) s]) { latched_[(size_t) s] = false; --latchedCount_; releaseSource(s, out, 0, arpOn); }
      holdWasOn_ = holdOn;

      for (const auto meta : in) {
        const auto m = meta.getMessage();
        const int pos = meta.samplePosition;
        if (m.isNoteOn()) {
          const int src = m.getNoteNumber();
          lastVelocity_ = m.getVelocity();
          if (holdOn && !anyDown()) {
            // a new chord after letting go replaces the held one
            for (int s = 0; s < 128; ++s) if (latched_[(size_t) s]) { latched_[(size_t) s] = false; --latchedCount_; releaseSource(s, out, pos, arpOn); }
          }
          if (latched_[(size_t) src]) { latched_[(size_t) src] = false; --latchedCount_; }
          releaseSource(src, out, pos, arpOn);
          down_[(size_t) src] = true;
          const int root = sc > 0 ? snapToScale(src, k, mask) : src;
          int notes[4];
          const int count = chordFor(root, sc, k, mask, notes);
          for (int i = 0; i < 4; ++i) produced_[(size_t) src][(size_t) i] = (int8_t) (i < count ? notes[i] : -1);
          ++producedCount_;
          for (int i = 0; i < count; ++i) {
            if (arpOn) addToPool(notes[i]);
            else if (refCount_[(size_t) notes[i]]++ == 0) add(out, MidiMessage::noteOn(1, notes[i], (uint8) lastVelocity_), pos);
          }
          if (arpOn && poolCount_ > 0 && arpNote_ < 0 && !hostPlaying)
            arpPhase_ = std::floor(arpPhase_) + 1.0 - inc * pos;   // song stopped: the first step plays right on this note
        }
        else if (m.isNoteOff()) {
          const int src = m.getNoteNumber();
          down_[(size_t) src] = false;
          if (holdOn && produced_[(size_t) src][0] >= 0) {
            if (!latched_[(size_t) src]) { latched_[(size_t) src] = true; ++latchedCount_; }
          }
          else releaseSource(src, out, pos, arpOn);
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff()) {
          stopArpNote(out, pos);
          for (int n = 0; n < 128; ++n) if (refCount_[(size_t) n]) add(out, MidiMessage::noteOff(1, n), pos);
          reset();
          add(out, m, pos, true);
          add(out, m, pos);
        }
        else if (m.getRawDataSize() <= 3) add(out, m, pos);   // CCs, pitch bend, aftertouch
      }

      if (!arpOn) return;

      if (hostPlaying)
        arpPhase_ = ppq / stepQN;   // locked to the song: every step lands exactly on the host's grid
      const int stepSamples = jmax(1, (int) (stepQN * 60.0 / bpm * sr_));
      const float gate = jlimit(0.05f, 0.98f, arpGate.load());
      // sample i is at phase start + i * inc; a step plays on the first sample at or past each whole step
      constexpr double eps = 1.0e-7;
      const double start = arpPhase_;
      for (int i = 0; i < numSamples; ++i) {
        if (offIn_ > 0 && --offIn_ == 0) stopArpNote(out, i);
        const double now = start + i * inc, before = now - inc;
        if (std::floor(now + eps) != std::floor(before + eps) && poolCount_ > 0) {
          stopArpNote(out, i);
          arpNote_ = nextArpNote();
          add(out, MidiMessage::noteOn(1, arpNote_, (uint8) jmax(1, lastVelocity_)), i);
          offIn_ = jmax(1, (int) (stepSamples * gate));
          arpCounter.fetch_add(1);
        }
      }
      arpPhase_ = start + numSamples * inc;
      if (poolCount_ == 0) stopArpNote(out, jmax(0, numSamples - 1));
    }

    ValueTree toTree() const {
      ValueTree t("GHOSTY_KEYS");
      t.setProperty("key", key.load(), nullptr); t.setProperty("scale", scale.load(), nullptr);
      t.setProperty("chord", chord.load(), nullptr); t.setProperty("arp", arp.load(), nullptr);
      t.setProperty("rate", arpRate.load(), nullptr); t.setProperty("mode", arpMode.load(), nullptr);
      t.setProperty("octaves", arpOctaves.load(), nullptr); t.setProperty("gate", arpGate.load(), nullptr);
      t.setProperty("hold", hold.load(), nullptr);
      return t;
    }
    void fromTree(const ValueTree& t) {
      Keys d;   // missing keys (older projects) = defaults = off
      key = jlimit(0, 11, (int) t.getProperty("key", d.key.load()));
      scale = jlimit(0, 12, (int) t.getProperty("scale", d.scale.load()));
      chord = jlimit(0, 3, (int) t.getProperty("chord", d.chord.load()));
      arp = (bool) t.getProperty("arp", false);
      arpRate = jlimit(0, 7, (int) t.getProperty("rate", d.arpRate.load()));
      arpMode = jlimit(0, 4, (int) t.getProperty("mode", d.arpMode.load()));
      arpOctaves = jlimit(1, 4, (int) t.getProperty("octaves", d.arpOctaves.load()));
      arpGate = jlimit(0.05f, 0.98f, (float) t.getProperty("gate", d.arpGate.load()));
      hold = (bool) t.getProperty("hold", false);
    }

  private:
    double sr_ = 44100.0, arpPhase_ = 0.0;
    std::array<std::array<int8_t, 4>, 128> produced_ {};
    std::array<uint8_t, 128> refCount_ {}, poolRef_ {};
    std::array<bool, 128> down_ {}, latched_ {};
    // pool = sorted unique notes, order = the same notes in the order they arrived
    std::array<int, 128> pool_ {}, order_ {};
    int poolCount_ = 0, orderCount_ = 0, producedCount_ = 0, latchedCount_ = 0, capacity_ = 0;
    int arpNote_ = -1, offIn_ = -1, seqPos_ = 0, lastVelocity_ = 100;
    bool arpWasOn_ = false, holdWasOn_ = false;
    Random rng_ { 0x6057 };

    bool anyDown() const { for (bool d : down_) if (d) return true; return false; }

    int chordFor(int root, int sc, int k, int mask, int* notes) const {
      notes[0] = root;
      const int c = chord.load();
      if (c == 0) return 1;
      if (c == 3) { notes[1] = jmin(127, root + 7); notes[2] = jmin(127, root + 12); return 3; }
      if (sc == 0) {
        notes[1] = jmin(127, root + 4); notes[2] = jmin(127, root + 7);
        if (c == 2) { notes[3] = jmin(127, root + 10); return 4; }
        return 3;
      }
      notes[1] = scaleStep(root, 2, k, mask); notes[2] = scaleStep(root, 4, k, mask);
      if (c == 2) { notes[3] = scaleStep(root, 6, k, mask); return 4; }
      return 3;
    }

    void releaseSource(int src, MidiBuffer& out, int pos, bool arpOn) {
      auto& p = produced_[(size_t) src];
      if (p[0] < 0) return;
      for (auto& o : p) {
        if (o < 0) continue;
        if (arpOn) removeFromPool(o);
        else if (refCount_[(size_t) o] > 0 && --refCount_[(size_t) o] == 0) add(out, MidiMessage::noteOff(1, o), pos);
        o = -1;
      }
      --producedCount_;
    }

    // Everything KEYS sends goes out on its own MIDI channel (16), notes and the controls that shape them (pitch bend,
    // sustain, mod wheel), so Vital bends / sustains them as usual. Own channel: Vital's on-screen keyboard state tracks
    // notes per channel, and KEYS' notes must never switch off a key the player is still holding (it would hang the arp).
    // Never grows the buffer past what was pre-allocated.
    bool add(MidiBuffer& out, const MidiMessage& m, int pos, bool keepChannel = false) {
      constexpr int header = (int) (sizeof(int32_t) + sizeof(uint16_t));
      if (capacity_ > 0 && out.data.size() + header + m.getRawDataSize() > capacity_) return false;
      if (keepChannel || m.getChannel() <= 0) { out.addEvent(m, pos); return true; }
      MidiMessage c(m);
      c.setChannel(kChannel);
      out.addEvent(c, pos);
      return true;
    }

    void addToPool(int n) {
      if (n < 0 || n > 127) return;
      if (poolRef_[(size_t) n]++ > 0) return;
      int i = poolCount_;
      while (i > 0 && pool_[(size_t) i - 1] > n) { pool_[(size_t) i] = pool_[(size_t) i - 1]; --i; }
      pool_[(size_t) i] = n; ++poolCount_;
      if (orderCount_ < 128) order_[(size_t) orderCount_++] = n;
    }
    void removeFromPool(int n) {
      if (n < 0 || n > 127 || poolRef_[(size_t) n] == 0) return;
      if (--poolRef_[(size_t) n] > 0) return;
      for (int i = 0; i < poolCount_; ++i) if (pool_[(size_t) i] == n) { for (int j = i; j < poolCount_ - 1; ++j) pool_[(size_t) j] = pool_[(size_t) j + 1]; --poolCount_; break; }
      for (int i = 0; i < orderCount_; ++i) if (order_[(size_t) i] == n) { for (int j = i; j < orderCount_ - 1; ++j) order_[(size_t) j] = order_[(size_t) j + 1]; --orderCount_; break; }
    }
    void stopArpNote(MidiBuffer& out, int pos) {
      if (arpNote_ >= 0) add(out, MidiMessage::noteOff(1, arpNote_), pos);
      arpNote_ = -1; offIn_ = -1;
    }
    int nextArpNote() {
      const int mode = arpMode.load();
      const bool asPlayed = mode == 4 && orderCount_ > 0;
      const int count = asPlayed ? orderCount_ : poolCount_;
      const int oct = jlimit(1, 4, arpOctaves.load()), len = jmax(1, count * oct);
      auto noteAt = [&](int idx) {
        const int base = asPlayed ? order_[(size_t) (idx % count)] : pool_[(size_t) (idx % count)];
        return jmin(127, base + 12 * (idx / count));
      };
      int idx = 0;
      switch (mode) {
        case 1: idx = len - 1 - (seqPos_ % len); ++seqPos_; break;
        case 2: {
          if (len == 1) { idx = 0; break; }
          const int cycle = 2 * len - 2, p = seqPos_ % cycle;
          idx = p < len ? p : cycle - p; ++seqPos_; break;
        }
        case 3: idx = rng_.nextInt(len); break;
        default: idx = seqPos_ % len; ++seqPos_; break;
      }
      return noteAt(idx);
    }
};

}  // namespace ghosty
