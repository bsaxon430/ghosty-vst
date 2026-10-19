#pragma once
#include "BeatEngine.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

class BeatsXProcessor : public juce::AudioProcessor {
public:
    BeatsXProcessor();
    ~BeatsXProcessor() override;
    const juce::String getName() const override { return "Ghosty Beats"; }
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& l) const override { return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo(); }
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }
    int getNumPrograms() override { return (int) gb::kits().size(); }
    int getCurrentProgram() override { return currentKit; }
    void setCurrentProgram(int i) override { if (juce::isPositiveAndBelow(i, getNumPrograms())) { applyKit(i, true); } }
    const juce::String getProgramName(int i) override { return juce::isPositiveAndBelow(i, getNumPrograms()) ? gb::kits()[(size_t) i].name : ""; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // ---- message-thread API for the editor
    gb::Model model;
    std::shared_ptr<gb::DrumLibrary> lib;
    juce::AudioProcessorValueTreeState apvts;
    gb::BeatEngine engine;
    int currentKit = 0;
    void publish();                                   // model -> new snapshot for the audio thread
    void applyKit(int kitIndex, bool patternsToo);
    bool loadSampleToTrack(int track, const juce::File& f, juce::String& err);
    void setTrackSound(int track, const juce::String& name);
    void drainRecorded();                             // pads recorded while playing -> model
    juce::File userDir() const;
    // whole beats (kit, your samples, all 16 patterns, the song, the mixer) in Documents/Ghosty/Beats
    juce::File beatsDir() const { auto d = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Ghosty").getChildFile("Beats"); d.createDirectory(); return d; }
    bool saveBeat(const juce::String& name) { juce::MemoryBlock mb; getStateInformation(mb); return beatsDir().getChildFile(juce::File::createLegalFileName(name) + ".ghostbeat").replaceWithData(mb.getData(), mb.getSize()); }
    bool loadBeat(const juce::File& f) { juce::MemoryBlock mb; if (!f.loadFileAsData(mb) || mb.getSize() == 0) return false; setStateInformation(mb.getData(), (int) mb.getSize()); return true; }
    bool savePattern(int pattern, const juce::String& name);
    bool loadPattern(const juce::File& f, int pattern);
    juce::File bounce(bool song, int bars);           // render to WAV (for drag-out)
    juce::StringArray bounceStems(bool song, int bars); // one WAV per track that plays (kick, snare, hats...)
    juce::File exportMidi(bool song);                  // MIDI notes 36..51 (for drag-out)
    int songBars() const;
    std::atomic<int> stutter { 0 }; std::atomic<bool> tape { false };
    std::atomic<bool> recording { false };
    void auditionTrack(int t, float vel = 0.9f) { engine.pushPad(t, vel); }

private:
    gb::EngineParams readParams(double bpm) const;
    juce::File renderTo(const gb::Snapshot* s, bool song, int bars, const juce::File& f);
    std::vector<std::unique_ptr<gb::Snapshot>> snaps;   // older snapshots stay alive (audio may still read the last one)
    std::vector<gb::SoundPtr> keepSounds;
    std::atomic<float>* p[20] {};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BeatsXProcessor)
};
