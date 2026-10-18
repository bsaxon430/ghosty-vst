#pragma once
#include "SpectrumProcessor.h"
#include "GhostUI.h"

// The field: 20 Hz at the bottom, 20 kHz at the top (log scale), pan left/right. Live spectrogram behind.
class SpectrumField : public juce::Component, private juce::Timer
{
public:
    enum Mode { PlayMode, ToneMode, DrumMode };
    explicit SpectrumField (SpectrumProcessor& p);
    Mode mode = PlayMode;
    int selected = -1;
    int drumToAdd = 0;
    std::function<void()> onSelectionChanged;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    float f2y (float f) const;
    float y2f (float y) const;
    float pan2x (float p) const;
    float x2pan (float x) const;

private:
    SpectrumProcessor& proc;
    juce::dsp::FFT fft { 12 };
    std::array<float, 8192> fftData {};
    juce::Image spec;
    std::array<std::array<float, 32>, SpectrumProcessor::NUM_TIMBRES> amps {};
    int lastHits[SpectrumProcessor::MAX_EL] {};
    float flash[SpectrumProcessor::MAX_EL] {};
    float glitch[SpectrumProcessor::MAX_EL] {};
    enum Drag { None, DragEl, DragLP, DragHP, DragLive } drag = None;
    juce::Point<float> grab, liveAt;
    ghosty::GhostLook liveLook;
    ghosty::NeonRain rain;
    struct Ripple { float x, y, r, a; };
    std::vector<Ripple> ripples;
    const float LEFT = 52.0f, RIGHT = 8.0f, TOP = 8.0f, BOT = 8.0f;
    int hitTest (juce::Point<float>) const;
    void updateSpectrogram();
    void timerCallback() override;
};

class SpectrumEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SpectrumEditor (SpectrumProcessor&);
    ~SpectrumEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    SpectrumProcessor& proc;
    ghosty::GhostLnF lnf;
    SpectrumField field;
    juce::ComboBox sceneBox, keyBox, scaleBox, refBox, playSoundBox, elSoundBox, drumPick;
    juce::TextButton playBtn { "PLAY" }, modePlay { "PLAY NOTES" }, modeTone { "+ TONE" }, modeDrum { "+ DRUM" }, snapBtn { "SNAP" },
                     droneBtn { "DRONE" }, delBtn { "REMOVE" };
    juce::OwnedArray<juce::TextButton> stepBtns;
    juce::Slider vol { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Label selLabel;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> keyAtt, scaleAtt, refAtt, playSoundAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> snapAtt;
    juce::OwnedArray<ghosty::GhostKnob> knobs;
    int lastScene = -1;
    float wordGlitch = 0;
    void setMode (SpectrumField::Mode m);
    void refreshSelection();
    void refreshScenes();
    void timerCallback() override;
};
