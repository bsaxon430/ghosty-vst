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
    juce::ComboBox presetBox, waveABox, waveBBox;
    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, editA { "DRAW A" }, editB { "DRAW B" }, diceBtn { "DICE" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attA, attB;
    juce::OwnedArray<ghosty::GhostKnob> knobs;
    struct BgGhost { ghosty::GhostLook look; float a, r, sp, sz, al; };
    std::vector<BgGhost> bg;
    float phase = 0, wordGlitch = 0;
    void timerCallback() override;
    void refreshPresetBox();
};
