#pragma once
// MIDI drag-out file of the sequencer pattern (header-only so the tests can use it without the editor)
#include "GhostSequencer.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace ghost
{
// f = the TEMPO switch factor (0.5, 1, 2). The pattern is in the plugin's own beats at the effective tempo; the file is written for the DAW's
// timeline: every time / f and the tempo event * f, so it plays back in FL exactly as heard. f = 1 is the file as before, byte for byte.
inline juce::MidiFile patternMidiFile(ghost::Sequencer &sq, double bpm, double f, const juce::String &name)
{
    juce::MidiMessageSequence seqm;
    const int tpq = 960;
    seqm.addEvent(juce::MidiMessage::tempoMetaEvent((int)std::lround(60000000.0 / std::max(20.0, bpm))), 0);
    seqm.addEvent(juce::MidiMessage::timeSignatureMetaEvent(4, 4), 0);
    seqm.addEvent(juce::MidiMessage::textMetaEvent(3, name), 0);
    // One clean track for FL Studio: notes on whole ticks, a slide's overlap kept only between
    // different pitches (FL draws that as a slide), never past the next hit of the same pitch or
    // the end of the 4 bars, and the end-of-track marker after the last note-off.
    auto evs = sq.events(4);
    std::sort(evs.begin(), evs.end(), [](auto &a, auto &b) { return a.start < b.start; });
    const double endT = 16.0 * tpq;
    for (size_t i = 0; i < evs.size(); ++i)
    {
        auto &e = evs[i];
        double t0 = std::round(std::max(0.0, e.start) * tpq);
        double t1 = std::round((std::max(0.0, e.start) + e.len) * tpq);
        for (size_t j = i + 1; j < evs.size(); ++j)
            if (evs[j].note == e.note)
            {
                t1 = std::min(t1, std::round(std::max(0.0, evs[j].start) * tpq) - 1);
                break;
            }
        t1 = std::min(t1, endT - 1);
        if (t0 >= endT || t1 <= t0)
            continue;
        seqm.addEvent(juce::MidiMessage::noteOn(1, e.note, (juce::uint8)e.vel), t0);
        seqm.addEvent(juce::MidiMessage::noteOff(1, e.note), t1);
    }
    seqm.addEvent(juce::MidiMessage::endOfTrack(), endT);
    seqm.updateMatchedPairs();
    juce::MidiFile mf;
    mf.setTicksPerQuarterNote(tpq);
    mf.addTrack(seqm);
    if (std::abs(f - 1.0) > 1e-9 && f > 0.0)
    {
        juce::MidiMessageSequence sc;
        for (auto *e : seqm)
        {
            auto m = e->message;
            const double ts = m.getTimeStamp() / f;
            if (m.isTempoMetaEvent())
                m = juce::MidiMessage::tempoMetaEvent((int)std::llround(m.getTempoSecondsPerQuarterNote() * 1.0e6 * f));
            m.setTimeStamp(ts);
            sc.addEvent(m);
        }
        sc.updateMatchedPairs();
        juce::MidiFile o;
        o.setTicksPerQuarterNote(tpq);
        o.addTrack(sc);
        mf = std::move(o);
    }
    return mf;
}

} // namespace ghost
