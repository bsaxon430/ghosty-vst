#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>

// MIDI "performer" that sits in front of the synth: scale lock, scale-aware chords and a tempo-synced arpeggiator.
// Real-time safe: fixed-size arrays only, no allocation.
namespace ghosty
{
inline const juce::StringArray& keyNames() { static juce::StringArray k { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; return k; }
inline const juce::StringArray& scaleNames()
{
    static juce::StringArray s { "Off (all notes)", "Major", "Minor", "Penta", "Minor Penta", "Blues", "Dorian", "Mixolydian", "Lydian", "Phrygian", "Harmonic Minor", "Japanese", "Arabic" };
    return s;
}
inline const juce::StringArray& chordNames() { static juce::StringArray c { "Single notes", "Triads", "7th chords", "Power chords" }; return c; }
inline const juce::StringArray& arpRateNames() { static juce::StringArray r { "1/4", "1/8", "1/16", "1/32", "1/8 T", "1/16 T" }; return r; }
inline const juce::StringArray& arpModeNames() { static juce::StringArray m { "Up", "Down", "Up + Down", "Random", "As played" }; return m; }

// Scale intervals; index matches scaleNames() (0 = off / chromatic).
inline int scaleMask (int scale)
{
    static const int sets[13][12] = {
        { 0,1,2,3,4,5,6,7,8,9,10,11 }, { 0,2,4,5,7,9,11,-1 }, { 0,2,3,5,7,8,10,-1 }, { 0,2,4,7,9,-1 }, { 0,3,5,7,10,-1 }, { 0,3,5,6,7,10,-1 },
        { 0,2,3,5,7,9,10,-1 }, { 0,2,4,5,7,9,10,-1 }, { 0,2,4,6,7,9,11,-1 }, { 0,1,3,5,7,8,10,-1 }, { 0,2,3,5,7,8,11,-1 }, { 0,1,5,7,8,-1 }, { 0,1,4,5,7,8,11,-1 } };
    int mask = 0;
    const int s = juce::jlimit (0, 12, scale);
    for (int i = 0; i < 12; ++i) { const int v = sets[s][i]; if (v < 0) break; mask |= 1 << v; if (s == 0 && i == 11) break; }
    return mask;
}
inline bool inScale (int note, int key, int mask) { return (mask >> (((note - key) % 12 + 12) % 12)) & 1; }
inline int snapToScale (int note, int key, int mask)
{
    if (inScale (note, key, mask)) return note;
    for (int d = 1; d < 7; ++d) { if (note - d >= 0 && inScale (note - d, key, mask)) return note - d; if (note + d < 128 && inScale (note + d, key, mask)) return note + d; }
    return note;
}
// n-th scale step above/below a note that is already in the scale.
inline int scaleStep (int note, int steps, int key, int mask)
{
    int n = note, left = steps;
    while (left > 0 && n < 127) { ++n; if (inScale (n, key, mask)) --left; }
    return n;
}

class Performer
{
public:
    struct Settings
    {
        int key = 9, scale = 0, chord = 0;
        bool arp = false; int arpRate = 2, arpMode = 0, arpOct = 2; float arpGate = 0.7f;
        double bpm = 120.0; bool hostPlaying = false; double ppq = 0.0; bool havePpq = false;
    };

    void prepare (double sampleRate) { sr = sampleRate; reset(); }
    // Bytes the output MidiBuffer was pre-allocated with (MidiBuffer::ensureSize). Events that would not fit are
    // dropped instead of letting the buffer grow (and allocate) on the audio thread. 0 = no limit.
    void setOutputCapacity (int bytes) { capacity = bytes; }
    void reset()
    {
        for (auto& p : produced) p.fill (-1);
        refCount.fill (0); poolRef.fill (0); poolCount = 0; orderCount = 0; arpNote = -1; offIn = -1; arpWasOn = false; seqPos = 0; dirUp = true;
    }

    void process (const juce::MidiBuffer& in, juce::MidiBuffer& out, int numSamples, const Settings& s)
    {
        out.clear();
        const int mask = scaleMask (s.scale);
        const bool arpOn = s.arp;

        if (arpWasOn && ! arpOn) { stopArpNote (out, 0); poolCount = orderCount = 0; poolRef.fill (0); }
        if (! arpWasOn && arpOn)
        {
            // move currently held chords into the arp pool
            for (int n = 0; n < 128; ++n) if (refCount[(size_t) n] > 0) { add (out, juce::MidiMessage::noteOff (1, n), 0); refCount[(size_t) n] = 0; }
            for (int n = 0; n < 128; ++n) for (auto o : produced[(size_t) n]) if (o >= 0) addToPool (o);
        }
        arpWasOn = arpOn;

        for (const auto meta : in)
        {
            const auto m = meta.getMessage(); const int pos = meta.samplePosition;
            if (m.isNoteOn())
            {
                const int src = m.getNoteNumber();
                lastVelocity = m.getVelocity();
                releaseSource (src, out, pos, arpOn);
                const int root = s.scale > 0 ? snapToScale (src, s.key, mask) : src;
                int notes[4]; const int count = chordFor (root, s, mask, notes);
                for (int k = 0; k < 4; ++k) produced[(size_t) src][(size_t) k] = (int8_t) (k < count ? notes[k] : -1);
                for (int k = 0; k < count; ++k)
                {
                    if (arpOn) addToPool (notes[k]);
                    else if (refCount[(size_t) notes[k]]++ == 0) add (out, juce::MidiMessage::noteOn (1, notes[k], (juce::uint8) lastVelocity), pos);   // always channel 1: note-offs are sent on 1, and JUCE matches voices by channel (notes from other channels rang forever)
                }
                if (arpOn && poolCount > 0 && arpNote < 0) { arpPhase = std::floor (arpPhase) + 0.999999; }   // start right away
            }
            else if (m.isNoteOff()) releaseSource (m.getNoteNumber(), out, pos, arpOn);
            else if (m.isAllNotesOff() || m.isAllSoundOff()) { for (int n = 0; n < 128; ++n) if (refCount[(size_t) n]) add (out, juce::MidiMessage::noteOff (1, n), pos); reset(); add (out, m, pos); }
            else if (m.getRawDataSize() <= 3) add (out, m, pos);     // CCs, pitch bend, aftertouch; SysEx is not passed on
        }

        if (! arpOn) return;

        static const double rateQN[6] { 1.0, 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0 };
        const double stepQN = rateQN[juce::jlimit (0, 5, s.arpRate)];
        const double inc = (s.bpm / 60.0) / sr / stepQN;     // steps per sample
        if (s.hostPlaying && s.havePpq)
        {
            const double hostPhase = s.ppq / stepQN;
            if (std::abs (hostPhase - arpPhase) > 0.5 && arpNote >= 0) arpPhase = hostPhase;
            else if (arpNote < 0 && poolCount == 0) arpPhase = hostPhase;
        }
        const int stepSamples = juce::jmax (1, (int) (stepQN * 60.0 / s.bpm * sr));
        for (int i = 0; i < numSamples; ++i)
        {
            if (offIn >= 0 && --offIn < 0) stopArpNote (out, i);
            const double before = arpPhase; arpPhase += inc;
            if (std::floor (arpPhase) != std::floor (before) && poolCount > 0)
            {
                stopArpNote (out, i);
                arpNote = nextArpNote (s);
                add (out, juce::MidiMessage::noteOn (1, arpNote, (juce::uint8) juce::jmax (1, lastVelocity)), i);
                offIn = juce::jmax (1, (int) (stepSamples * juce::jlimit (0.05f, 0.98f, s.arpGate)));
                ++arpCounter;
            }
        }
        if (poolCount == 0) stopArpNote (out, juce::jmax (0, numSamples - 1));
    }

    std::atomic<int> arpCounter { 0 };   // UI can watch this to flash on every arp step

private:
    double sr = 44100.0, arpPhase = 0.0;
    std::array<std::array<int8_t, 4>, 128> produced {};
    std::array<uint8_t, 128> refCount {};
    // pool = sorted unique notes, order = the same notes in the order they arrived; poolRef counts how many
    // sources hold each note so a note shared by two chords is only in the pool once.
    std::array<int, 128> pool {}; int poolCount = 0;
    std::array<int, 128> order {}; int orderCount = 0;
    std::array<uint8_t, 128> poolRef {};
    int capacity = 0;
    int arpNote = -1, offIn = -1, seqPos = 0, lastVelocity = 100; bool arpWasOn = false, dirUp = true;
    juce::Random rng;

    int chordFor (int root, const Settings& s, int mask, int* notes) const
    {
        notes[0] = root;
        if (s.chord == 0) return 1;
        if (s.chord == 3) { notes[1] = juce::jmin (127, root + 7); notes[2] = juce::jmin (127, root + 12); return 3; }
        if (s.scale == 0)
        {
            notes[1] = juce::jmin (127, root + 4); notes[2] = juce::jmin (127, root + 7);
            if (s.chord == 2) { notes[3] = juce::jmin (127, root + 10); return 4; }
            return 3;
        }
        notes[1] = scaleStep (root, 2, s.key, mask); notes[2] = scaleStep (root, 4, s.key, mask);
        if (s.chord == 2) { notes[3] = scaleStep (root, 6, s.key, mask); return 4; }
        return 3;
    }

    void releaseSource (int src, juce::MidiBuffer& out, int pos, bool arpOn)
    {
        auto& p = produced[(size_t) src];
        for (auto& o : p)
        {
            if (o < 0) continue;
            if (arpOn) removeFromPool (o);
            else if (refCount[(size_t) o] > 0 && --refCount[(size_t) o] == 0) add (out, juce::MidiMessage::noteOff (1, o), pos);
            o = -1;
        }
    }

    // real-time safe add: never grows the buffer past what was pre-allocated
    bool add (juce::MidiBuffer& out, const juce::MidiMessage& m, int pos)
    {
        constexpr int header = (int) (sizeof (int32_t) + sizeof (uint16_t));
        if (capacity > 0 && out.data.size() + header + m.getRawDataSize() > capacity) return false;
        return out.addEvent (m, pos);
    }

    void addToPool (int n)
    {
        if (n < 0 || n > 127) return;
        if (poolRef[(size_t) n]++ > 0) return;          // already in the pool
        if (poolCount >= 128) return;
        int i = poolCount; while (i > 0 && pool[(size_t) i - 1] > n) { pool[(size_t) i] = pool[(size_t) i - 1]; --i; }
        pool[(size_t) i] = n; ++poolCount;
        if (orderCount < 128) order[(size_t) orderCount++] = n;
    }
    void removeFromPool (int n)
    {
        if (n < 0 || n > 127 || poolRef[(size_t) n] == 0) return;
        if (--poolRef[(size_t) n] > 0) return;          // another source still holds it
        for (int i = 0; i < poolCount; ++i) if (pool[(size_t) i] == n) { for (int j = i; j < poolCount - 1; ++j) pool[(size_t) j] = pool[(size_t) j + 1]; --poolCount; break; }
        for (int i = 0; i < orderCount; ++i) if (order[(size_t) i] == n) { for (int j = i; j < orderCount - 1; ++j) order[(size_t) j] = order[(size_t) j + 1]; --orderCount; break; }
    }
    void stopArpNote (juce::MidiBuffer& out, int pos)
    {
        if (arpNote >= 0) add (out, juce::MidiMessage::noteOff (1, arpNote), pos);
        arpNote = -1; offIn = -1;
    }
    int nextArpNote (const Settings& s)
    {
        // "As played" walks the arrival order, the others the sorted pool. Both hold the same unique notes,
        // but index and octave are always taken from the list actually used so they can never disagree.
        const bool asPlayed = s.arpMode == 4 && orderCount > 0;
        const int count = asPlayed ? orderCount : poolCount;
        const int oct = juce::jlimit (1, 3, s.arpOct), len = juce::jmax (1, count * oct);
        auto noteAt = [&] (int idx)
        {
            const int base = asPlayed ? order[(size_t) (idx % count)] : pool[(size_t) (idx % count)];
            return juce::jmin (127, base + 12 * (idx / count));
        };
        int idx = 0;
        switch (s.arpMode)
        {
            case 1: idx = len - 1 - (seqPos % len); ++seqPos; break;
            case 2:
            {
                if (len == 1) { idx = 0; break; }
                const int cycle = 2 * len - 2, p = seqPos % cycle; idx = p < len ? p : cycle - p; ++seqPos; break;
            }
            case 3: idx = rng.nextInt (len); break;
            case 4: idx = seqPos % len; ++seqPos; break;
            default: idx = seqPos % len; ++seqPos; break;
        }
        return noteAt (idx);
    }
};
} // namespace ghosty
