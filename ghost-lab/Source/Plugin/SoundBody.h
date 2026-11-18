#pragma once
// THE LIVING SOUND: a glowing organism of liquid light drawn from the sound's spectrum (how loud each moment is makes
// it thick or thin there, the balance of its frequency bands lights its ribbons, its roughness makes it sparkle),
// purple where the sound starts through magenta, blue and cyan to acid green where it ends. You touch it to process
// the sound, and it follows your hand and springs back like jelly:
//   drag sideways      PULL    stretches the part of the sound you dragged across
//   drag up / down     PINCH   squeezes (down) or widens (up) the spectrum
//   circle on it       TWIST   bends its character (clockwise / anticlockwise): its ribbons wind up as you turn
//   shake it           MUTATE  controlled random mutations (the harder you shake, the wilder)
//   double-click / hold FREEZE holds that moment
//   drop SPECIMEN 002 on it   BREED (merge two specimens)
// While your hand is on it, onFeel says how it is being pulled / pinched / twisted / shaken so the sound can change as
// you move (the editor plays it through the live chain); letting go runs the real process (onGesture).
#include "Theme.h"
#include "../Core/Actions.h"

namespace glab
{
class SoundBody : public juce::Component, public juce::DragAndDropTarget, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    SoundBody();
    void setProfile (const Profile& p);        // the processed sound
    void setEmpty (bool e) { if (empty != e) { empty = e; frameValid = softValid = false; updateCursor(); } repaint(); }
    void setBusy (bool b) { busy = b; }
    void setPlayhead (float frac) { playhead = frac; }
    void setNarrowStyle (bool n);              // the phone-width page: a rounded window, not the long tube
    // the wide page: where the whole specimen tube is, in our coordinates (our copy of its liquid lines up with the page's)
    void setTube (juce::Rectangle<float> box) { if (box != tubeBox) { tubeBox = box; staticDirty = true; repaint(); } }
    void setAnimating (bool a) { if (a) startTimerHz (60); else stopTimer(); }
    void settle (double atTime = 2.0);         // jump to the target shape (screenshots, tests)
    // put it in a pose (screenshots, tests): how far pulled / pinched / twisted / shaken, bent up-down and dragged
    // sideways at `where` along it
    void setDeformation (float pull, float pinch, float twist, float shake = 0.0f, float bend = 0.0f, float drag = 0.0f, float where = 0.5f);

    std::function<void (const Step&)> onGesture;
    // while a hand is on it and has moved: how it is being pulled / pinched / twisted / shaken right now and where it was
    // grabbed (0 = the sound's start .. 1 = its end). When the hand lets go: active = false, and `commit` says whether a
    // gesture was made (the real process is about to run) or not (nothing more will happen).
    struct Feel { float pull = 0, pinch = 0, twist = 0, shake = 0, bend = 0, where = 0.5f; bool active = false, commit = false; };
    std::function<void (const Feel&)> onFeel;
    // the gesture being made, in words ("PULL 3.2x"; empty = none recognised yet) and what letting go will do (empty =
    // no hand on it)
    std::function<void (const juce::String& gesture, const juce::String& hint)> onLive;
    std::function<void()> onBreed, onClick, onClickEmpty;
    std::function<void (const juce::StringArray&)> onFiles;

    // gesture recognition (public for the tests): points in local coordinates, times in ms
    struct Stroke { std::vector<juce::Point<float>> pts; std::vector<double> ms; };
    bool classify (const Stroke& s, Step& out) const;

    // a hand on it (the mouse calls these; so do the tests)
    void press (juce::Point<float> at);
    void dragTo (juce::Point<float> at);
    void release (juce::Point<float> at, int clicks = 1);
    bool touching() const { return pressing; }
    Op liveOp() const { return liveGesture; }                  // the gesture onLive last named (Op::Count = none yet)

    void paint (juce::Graphics&) override;
    void resized() override { staticDirty = true; }
    void mouseDown (const juce::MouseEvent& e) override { press (e.position); }
    void mouseDrag (const juce::MouseEvent& e) override { dragTo (e.position); }
    void mouseUp (const juce::MouseEvent& e) override { release (e.position, e.getNumberOfClicks()); }
    void mouseMove (const juce::MouseEvent& e) override { hoverAt = e.position; hovering = true; }
    void mouseExit (const juce::MouseEvent&) override { hovering = false; }
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMagnify (const juce::MouseEvent&, float scale) override;

    bool isInterestedInDragSource (const SourceDetails& d) override { return d.description == "specimen-002"; }
    void itemDragEnter (const SourceDetails&) override { mergeHover = true; }
    void itemDragExit (const SourceDetails&) override { mergeHover = false; }
    void itemDropped (const SourceDetails&) override { mergeHover = false; mergeFlash = 1.0f; if (onBreed) onBreed(); }
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { fileHover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { fileHover = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override { fileHover = false; if (onFiles) onFiles (files); }

    // what the body looks like right now (tests): deformation amounts
    float livePull() const { return defPull; }
    float liveTwist() const { return defTwist; }
    float livePinch() const { return defPinch; }
    float liveBend() const { return defBend; }
    float liveDrag() const { return defDrag; }
    juce::Rectangle<float> bodyRect() const;   // where the sound lies in the glass: its start at the left edge, its end at the right

    // tests: how long one frame of the body takes to draw (ms), averaged over n frames at the current size; `scale` 2 = a
    // Retina screen (the frame is drawn at the screen's own pixels, up to 2x)
    double benchmarkFrameMs (int n, bool touching, float scale = 1.0f);
    void advance (int frames) { for (int i = 0; i < frames; ++i) timerCallback(); }   // tests: let time pass without a message loop

private:
    void timerCallback() override;
    bool wantsFrame();                         // once a tick: should this one be drawn?
    void freezeAt (float x);
    void sendFeel (bool active, bool commit);
    void updateCursor();
    float xToU (float x) const;

    // ---- the sound it shows (eased toward the newest profile)
    std::vector<float> dispE, dispB, dispF, targetE, targetB, targetF;
    bool empty = true, busy = false, narrow = false, mergeHover = false, fileHover = false;
    float playhead = -1.0f, playShown = -1.0f, mergeFlash = 0.0f;
    double t = 0.0;

    // ---- the hand
    struct Shape { float len = 0, chord = 0, dx = 0, dy = 0, turn = 0, round = 0; int reversals = 0; double dur = 0; };
    static Shape measure (const Stroke& s);
    bool decide (const Shape& m, const Stroke& s, Step& out) const;
    Stroke stroke;
    bool pressing = false, held = false, moved = false, hovering = false;
    double pressMs = 0;
    juce::Point<float> hoverAt, handAt;
    int reversalsSeen = 0;
    Op liveGesture = Op::Count;
    // the body follows the hand at once (the part you grabbed goes where the pointer goes, before any gesture is
    // recognised) and springs back like jelly when let go: the shown amounts (def*) chase the hand's (t*) - directly
    // while it is held, through a damped spring (velocities v*) once it is free
    float defPull = 0, defPullC = 0.5f, defPinch = 0, defTwist = 0, defShake = 0, defBend = 0, defDrag = 0;
    float tPull = 0, tPinch = 0, tTwist = 0, tShake = 0, tBend = 0, tDrag = 0;
    float vPull = 0, vPinch = 0, vTwist = 0, vShake = 0, vBend = 0, vDrag = 0;
    float grabU = 0.5f, shakeNow = 0, touchGlow = 0, hoverGlow = 0;
    float freezeX = -1, freezeGlow = 0;
    float magnify = 1.0f; double magnifyMs = 0;

    // ---- drawing (see SoundBody.cpp): a frame is built from the liquid behind (kept), a soft light field on a coarse
    // grid, and fine bright lines, sparks and bubbles drawn at the screen's own pixels
    static constexpr int kRibbons = 5, kPerRibbon = 4, kWisps = 3, kLines = kRibbons * kPerRibbon + kWisps + 1;
    static constexpr int kSparks = 56, kBubbles = 11;
    static constexpr float kCell = 4.0f;       // the light field: one sample every 4 px
    struct Col
    {
        bool on = false;
        float uu = 0, u = 0;                   // where along the glass; which moment of the sound is there (pulled / dragged)
        float cy = 0, top = 0, bot = 0;        // the spine, and how far the body reaches above / below it (px)
        float e = 0, glow = 0, phi = 0;        // how loud; how bright; how far round its ribbons have wound
        float r = 0, g = 0, b = 0;             // its colour there
        float flux = 0;
    };
    void prepare (float renderScale) const;
    void buildColumns (const std::vector<float>& e, const std::vector<float>& bands, const std::vector<float>& flux, float alpha) const;
    void buildLight() const;
    void composeSoft() const;
    void composeSharp() const;
    void drawLine (juce::uint8* pix, int stride, const float* y, const float* c, float width) const;
    void drawSparks (juce::uint8* pix, int stride) const;
    void drawBubbles (juce::uint8* pix, int stride) const;
    void addDot (juce::uint8* pix, int stride, float x, float y, float rad, float b, float g, float r) const;
    void renderFrame (const std::vector<float>& e, const std::vector<float>& bands, const std::vector<float>& flux, float alpha, bool freshSoft) const;
    void drawEmptyHint (juce::Graphics&, juce::Rectangle<float> r) const;

    juce::Rectangle<float> tubeBox;
    mutable juce::Image frame;                 // the finished picture (opaque)
    mutable std::vector<juce::uint8> backPix;  // the liquid behind, in the frame's own pixel layout
    mutable std::vector<juce::uint8> softPix;  // the liquid plus the soft light (the sharp parts go over a copy of it)
    mutable int ow = 0, oh = 0, ps = 3, rowBytes = 0, iw = 0, ih = 0, softNo = 0;
    int stamp = 0; mutable int shownStamp = -1;   // ticks of the timer; the one the finished frame shows
    mutable float scaleNow = 0.0f;
    mutable bool staticDirty = true, frameValid = false, softValid = false, narrowNow = false;
    mutable juce::Rectangle<float> tubeNow;
    mutable std::vector<Col> cols;
    mutable std::vector<float> light, lineY, lineC, veilLo, veilHi, veilI;
    mutable std::vector<juce::uint8> coarse, wide;
    mutable std::vector<int> xi, xw, yi, yw;
    mutable float lineW[kLines] {};
};
}
