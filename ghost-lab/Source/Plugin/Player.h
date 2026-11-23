#pragma once
// A/B preview playback. The audio thread only reads raw pointers published here; it never locks, allocates or frees.
// The message thread owns the sounds (shared_ptr) and frees a replaced one only once the audio thread has let go of it.
#include "../Core/AudioTools.h"
#include <atomic>

namespace glab
{
class Player
{
public:
    // Preview: the TERRAIN page's live listen (looped). Touch: the processed sound going round while a hand is on THE
    // LIVING SOUND (the processor plays it through the body's live chain)
    enum Slot { Original = 0, Processed = 1, Preview = 2, Touch = 3, Room = 4, kSlots = 5 };   // Room: the SPACE room's live listen (looped)

    // ---- message thread
    void set (int slot, AudioPtr a);
    void collect();                          // frees replaced sounds nobody plays any more (call from a timer)
    void play (int slot, double fromFraction = -1.0);   // -1: keep the position when switching A/B, else from there
    void stop();
    void setLoop (int slot, bool on) { loopSlot.store (on ? slot : -1); }   // that slot plays round and round
    int playingSlot() const { return playingNow.load(); }   // -1 = stopped
    float playhead() const { return headFrac.load(); }

    // ---- audio thread
    void prepare (double hostRate) { rate = hostRate; }
    void noteOn (int note, float velocity);
    void noteOff (int note);
    void setMidiSlot (int slot) { midiSlot.store (slot); }
    void setHoldLoops (bool on) { holdLoops.store (on); }   // a held key plays the sound round and round
    bool keyHeld() const { return heldNow.load(); }        // a key is down right now (any thread)
    void render (juce::AudioBuffer<float>& out, int start, int num, float gain);   // adds into out

    std::atomic<int> debugAudioThreadAllocs { 0 };

private:
    std::atomic<const Audio*> current[kSlots] { nullptr, nullptr, nullptr, nullptr, nullptr };
    std::atomic<const Audio*> inUse[kSlots] { nullptr, nullptr, nullptr, nullptr, nullptr };
    AudioPtr owners[kSlots];
    std::vector<AudioPtr> retired;

    // commands from the message thread
    std::atomic<int> cmdSlot { -2 };          // -2 nothing, -1 stop, 0.. play that slot
    std::atomic<double> cmdFrom { -1.0 };
    std::atomic<int> midiSlot { Processed };
    std::atomic<int> loopSlot { -1 };
    std::atomic<bool> holdLoops { true }, heldNow { false };

    // audio-thread state
    double rate = 44100.0, pos = 0.0, speed = 1.0;
    int slotNow = -1, heldNote = -1, pendingSlot = -1;
    double pendingFrom = 0.0;
    float gainNow = 0.0f, gainTarget = 0.0f;
    std::atomic<int> playingNow { -1 };
    std::atomic<float> headFrac { 0.0f };
    const Audio* acquire (int slot);
};
}
