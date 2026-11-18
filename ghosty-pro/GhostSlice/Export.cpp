#include "Export.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace gslice::exporter
{
bool writeWav (const juce::AudioBuffer<float>& audio, double sr, const juce::File& f, int bits, juce::String& error)
{
    f.deleteFile();
    auto os = std::make_unique<juce::FileOutputStream> (f);
    if (! os->openedOk()) { error = "Couldn't write " + f.getFullPathName(); return false; }
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, (unsigned int) audio.getNumChannels(), bits, {}, 0));   // GATE: via-caller (slice.export.wav in SliceEditor; the processor re-checks the licence)
    if (w == nullptr) { error = "Couldn't create the WAV writer"; return false; }
    os.release();   // the writer owns the stream now
    if (! w->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples())) { error = "Writing the WAV failed"; return false; }
    return true;
}

juce::MidiFile slicesMidi (const Plan& plan, double bpm)
{
    juce::MidiFile mf; mf.setTicksPerQuarterNote (960);
    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / juce::jlimit (20.0, 999.0, bpm))), 0);
    seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0);
    const double ticksPerFrame = 960.0 * bpm / 60.0 / plan.hostRate;
    int note = 36;
    for (auto& pc : plan.pieces)
    {
        const double t0 = (double) pc.dst0 * ticksPerFrame, t1 = (double) pc.dst1 * ticksPerFrame;
        seq.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), std::round (t0));
        seq.addEvent (juce::MidiMessage::noteOff (1, note), std::max (std::round (t0) + 1, std::round (t1) - 1));
        note = note < 127 ? note + 1 : 36;
    }
    seq.updateMatchedPairs();
    seq.addEvent (juce::MidiMessage::endOfTrack(), seq.getEndTime() + 1);
    mf.addTrack (seq);
    return mf;
}

bool writeMidi (const juce::MidiFile& m, const juce::File& f, juce::String& error)
{
    f.deleteFile();
    juce::FileOutputStream os (f);
    if (! os.openedOk()) { error = "Couldn't write " + f.getFullPathName(); return false; }
    if (! m.writeTo (os)) { error = "Writing the MIDI file failed"; return false; }   // GATE: via-caller (slice.export.midi in SliceEditor)
    return true;
}
} // namespace gslice::exporter
