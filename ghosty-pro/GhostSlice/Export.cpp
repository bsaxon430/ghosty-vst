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
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, (unsigned int) audio.getNumChannels(), bits, {}, 0));   // GATE: via-caller (slice.drag.wav / slice.take.save in SliceEditor; the processor re-checks the licence)
    if (w == nullptr) { error = "Couldn't create the WAV writer"; return false; }
    os.release();   // the writer owns the stream now
    if (! w->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples())) { error = "Writing the WAV failed"; return false; }
    return true;
}

bool writeMidi (const juce::MidiFile& m, const juce::File& f, juce::String& error)
{
    f.deleteFile();
    juce::FileOutputStream os (f);
    if (! os.openedOk()) { error = "Couldn't write " + f.getFullPathName(); return false; }
    if (! m.writeTo (os)) { error = "Writing the MIDI file failed"; return false; }   // GATE: via-caller (slice.drag.midi in SliceEditor)
    return true;
}
} // namespace gslice::exporter
