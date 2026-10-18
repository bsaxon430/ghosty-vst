#pragma once
#include "BeatsProcessor.h"
#include "GhostUI.h"

// Vertical step grid: each column is a drum with its own ghost, time runs downwards.
class StepGrid : public juce::Component, private juce::Timer
{
public:
    explicit StepGrid (BeatsProcessor& p);
    std::function<void (int)> onSelect;
    int selected = 0;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
private:
    BeatsProcessor& proc;
    ghosty::GhostLook looks[BeatsProcessor::TRACKS];
    float hit[BeatsProcessor::TRACKS] {};
    int lastHits[BeatsProcessor::TRACKS] {};
    int paintValue = 1;
    struct Ring { int c, r; float a; };
    std::vector<Ring> rings;
    static constexpr float HEAD = 70.0f;
    bool cellAt (juce::Point<float> p, int& c, int& r, bool& head) const;
    void timerCallback() override;
};

class BeatsEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BeatsEditor (BeatsProcessor&);
    ~BeatsEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    BeatsProcessor& proc;
    ghosty::GhostLnF lnf;
    StepGrid grid;
    ghosty::XYGhostPad pad;
    juce::ComboBox patternBox, soundBox, speedBox;
    juce::TextButton prevBtn { "<" }, nextBtn { ">" }, playBtn { "PLAY" }, revBtn { "REVERSE" }, diceBtn { "DICE" }, clearBtn { "CLEAR" },
                     trackRev { "REVERSE" }, trackMute { "MUTE" }, trackPreview { "HIT" };
    juce::Slider pitch { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight }, vol { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label trackLabel, pitchLabel, volLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> soundAtt, speedAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pitchAtt, volAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> revAtt, trackRevAtt, trackMuteAtt;
    juce::OwnedArray<ghosty::HoldButton> holds;
    juce::OwnedArray<ghosty::GhostKnob> knobs;
    float wordGlitch = 0;
    void selectTrack (int t);
    void timerCallback() override;
    void refreshPatternBox();
};
