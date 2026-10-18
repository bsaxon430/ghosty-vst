#pragma once
#include "SynthProcessor.h"
#include "GhostUI.h"

// The drawable wave screen: frame A or B in front, the morph position glowing in between.
class WaveScreen : public juce::Component, private juce::Timer
{
public:
    explicit WaveScreen (SynthProcessor& p) : proc (p) { startTimerHz (30); }
    bool editB = false;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { last = -1; paintAt (e.position); }
    void mouseDrag (const juce::MouseEvent& e) override { paintAt (e.position); }
    void mouseUp (const juce::MouseEvent&) override;
    void randomise();
private:
    SynthProcessor& proc;
    int last = -1; bool dirty = false; int sinceRebuild = 0; float glitch = 0;
    juce::Rectangle<float> front;
    void paintAt (juce::Point<float>);
    void timerCallback() override;
};

// Slide across the scale: every column is a note that fits the chosen key and scale.
class FlowStrip : public juce::Component, private juce::Timer
{
public:
    explicit FlowStrip (SynthProcessor& p) : proc (p) { startTimerHz (40); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent& e) override { down = true; play (e.position); }
    void mouseDrag (const juce::MouseEvent& e) override { play (e.position); }
    void mouseUp (const juce::MouseEvent&) override { down = false; release(); }
private:
    SynthProcessor& proc;
    int current = -1; bool down = false; juce::Point<float> finger;
    float flash[128] {};
    struct Ripple { float x, y, r, a; juce::Colour c; };
    std::vector<Ripple> ripples;
    int notes[64] {}; int count = 0;
    void buildNotes();
    void play (juce::Point<float>);
    void release() { if (current >= 0) proc.keyboardState.noteOff (1, current, 0.0f); current = -1; }
    juce::Colour colourFor (int note) const;
    void timerCallback() override;
};

class SynthEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SynthEditor (SynthProcessor&);
    ~SynthEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    SynthProcessor& proc;
    ghosty::GhostLnF lnf;
    WaveScreen wave;
    ghosty::XYGhostPad pad;
    FlowStrip flow;
    juce::ComboBox presetBox, waveABox, waveBBox, keyBox, scaleBox, chordBox, rateBox, modeBox, octBox;
    juce::TextButton arpBtn { "ARP" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyAtt, scaleAtt, chordAtt, rateAtt, modeAtt, octAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> arpAtt;
    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, editA { "DRAW A" }, editB { "DRAW B" }, diceBtn { "DICE" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attA, attB;
    juce::OwnedArray<ghosty::GhostKnob> knobs;
    struct BgGhost { ghosty::GhostLook look; float a, r, sp, sz, al; };
    std::vector<BgGhost> bg;
    float phase = 0, wordGlitch = 0;
    void timerCallback() override;
    void refreshPresetBox();
};
