#pragma once
// GHOST HUM - the files that leave the plugin: a standard MIDI file (type 1, 960 ticks per beat, the take's tempo, melody on channel 1,
// drums on channel 10 with the General MIDI notes) and a WAV (24-bit, 48 kHz stereo) of the built-in sounds. Both honour the current
// QUANTIZE / SWING (ON GRID) or the as-sung timing (ORIGINAL). Every caller asks the export gate first (Shared/ExportGate.h).
#include "HumSounds.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace hum::files {

inline juce::MidiFile midiOf(const Take& T, const Grid& G, bool original, const juce::String& name) {
    juce::MidiFile mf; const int ppq = 960; mf.setTicksPerQuarterNote(ppq);
    const double bpm = T.bpm > 0 ? T.bpm : 120.0, beat = 60.0 / bpm; const auto evs = sound::sequence(T, G, original);
    double first = 0; for (auto& e : evs) first = juce::jmin(first, e.t - T.origin);   // nothing before tick 0
    juce::MidiMessageSequence meta; meta.addEvent(juce::MidiMessage::tempoMetaEvent((int) std::lround(60000000.0 / bpm)), 0); meta.addEvent(juce::MidiMessage::timeSignatureMetaEvent(4, 4), 0);
    meta.addEvent(juce::MidiMessage::textMetaEvent(3, name), 0); meta.addEvent(juce::MidiMessage::endOfTrack(), ppq); mf.addTrack(meta);
    juce::MidiMessageSequence seq; const int ch = T.mode == Melody ? 1 : 10; seq.addEvent(juce::MidiMessage::textMetaEvent(3, T.mode == Melody ? "Ghost Hum melody" : "Ghost Hum beat"), 0);
    auto tick = [&](double t) { return juce::jmax(0.0, (t - T.origin - first) / beat * ppq); };
    bool bends = false; for (auto& e : evs) if ((e.type == 0 || e.type == 3) && std::abs(e.cents) > 0.5f) bends = true;
    auto wheel = [](float cents) { return juce::jlimit(0, 16383, (int) std::lround(8192 + juce::jlimit(-200.0f, 200.0f, cents) / 200.0 * 8192)); };
    for (auto& e : evs) {
        const double tk = tick(e.t); const int vel = juce::jlimit(1, 127, (int) std::lround(e.vel * 127));
        // (pitch wheel: +-2 semitones, the General MIDI default. Build 4: every note sets it when the take bends at all, so a steady note after a
        // bent one is not left bent; the bend line follows as wheel moves)
        if (e.type == 0) { if (bends) seq.addEvent(juce::MidiMessage::pitchWheel(ch, wheel(e.cents)), tk);
            seq.addEvent(juce::MidiMessage::noteOn(ch, e.num, (juce::uint8) vel), tk); }
        else if (e.type == 1) { seq.addEvent(juce::MidiMessage::noteOff(ch, e.num), tk); }
        else if (e.type == 3) { seq.addEvent(juce::MidiMessage::pitchWheel(ch, wheel(e.cents)), tk); }
        else { seq.addEvent(juce::MidiMessage::noteOn(ch, gmNote(e.num), (juce::uint8) vel), tk); seq.addEvent(juce::MidiMessage::noteOff(ch, gmNote(e.num)), tk + ppq / 8); }
    }
    seq.updateMatchedPairs(); seq.addEvent(juce::MidiMessage::endOfTrack(), sound::sequenceEnd(evs) > 0 ? tick(sound::sequenceEnd(evs)) + ppq : ppq); mf.addTrack(seq);
    return mf;
}
inline bool writeMidi(const juce::File& f, const Take& T, const Grid& G, bool original, const juce::String& name) {
    auto mf = midiOf(T, G, original, name); juce::MemoryOutputStream ms; if (!mf.writeTo(ms)) return false;
    return f.replaceWithData(ms.getData(), ms.getDataSize());
}
inline bool writeWav(const juce::File& f, const Take& T, const Grid& G, bool original, int voice) {
    const double sr = 48000.0; auto buf = sound::render(T, G, original, voice, sr);
    juce::WavAudioFormat wav; f.deleteFile(); std::unique_ptr<juce::FileOutputStream> os(f.createOutputStream()); if (!os) return false;
    std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(os.get(), sr, 2, 24, {}, 0)); if (!w) return false; os.release();
    return w->writeFromAudioSampleBuffer(buf, 0, buf.getNumSamples());
}
inline bool writeTakeWav(const juce::File& f, const std::vector<float>& x, double sr) {   // the recording itself (REDO keeps nothing; this is for tests / a later "save my voice")
    juce::WavAudioFormat wav; f.deleteFile(); std::unique_ptr<juce::FileOutputStream> os(f.createOutputStream()); if (!os) return false;
    std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(os.get(), sr, 1, 16, {}, 0)); if (!w) return false; os.release();
    juce::AudioBuffer<float> b(1, (int) x.size()); b.copyFrom(0, 0, x.data(), (int) x.size()); return w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
}
inline juce::String slug(juce::String s) { s = s.retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_#"); return s.trim().isEmpty() ? juce::String("Ghost Hum") : s.trim(); }
inline juce::File exportDir(const char* sub) { auto d = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Ghosty").getChildFile("Ghost Hum").getChildFile(sub); d.createDirectory(); return d; }
inline juce::String takeName(const Take& T) {
    juce::String n = T.mode == Melody ? "Ghost Hum melody" : "Ghost Hum beat"; if (T.mode == Melody && T.key >= 0) n << " " << keyText(T); n << " " << juce::String(juce::roundToInt(T.bpm)) << "bpm"; return slug(n);
}

} // namespace hum::files
