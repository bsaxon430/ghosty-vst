// SPDX-License-Identifier: AGPL-3.0-or-later
// GHOST ATLAS - the stage: the painted world behind everything. It fills the window with one of the eighteen pictures,
// frames it round one point of the picture, lays panes of glass (a soft, darkened copy of the same picture) where the
// controls sit, and adds what moves: the world's own weather and mist, or its film.
//
// The pictures are large, so each one is decoded, framed and softened on a background thread and then faded in; the
// screen never waits for a picture.
#pragma once
#include "Theme.h"
#include "Film.h"

namespace atlas::ui
{
class Stage final : public juce::Component, private juce::Timer
{
public:
    enum PaneKind { glass, scrimTop, scrimBottom };
    struct Pane { juce::Rectangle<float> r; float radius = 14.0f; PaneKind kind = glass; };

    Stage();
    ~Stage() override;

    // which world, which of its two views, where the glass lies, and the part of the picture left open (the picture is
    // framed so that its own point of interest lies inside it, and the weather is drawn there). All in the base layout's coordinates.
    void setScene (int world, bool secondView, std::vector<Pane> panes, juce::Rectangle<float> open, bool showTitle);
    void setVeil (float amount);                 // 0..1: an overlay is open
    // how much the world moves: 0 still (nothing moves), 1 alive (the weather, and mist over the water),
    // 2 film (the world's own clip from the recording plays behind everything, when its frames are installed)
    enum { motionStill = 0, motionAlive = 1, motionFilm = 2 };
    void setMotion (int mode);
    int motion() const { return motionMode; }
    bool filmAvailable() const { return anyFilm; }       // some world's frames are installed
    bool filmShowing() const { return filmShow > 0.5f; }
    void drawTook (float ms) { film.drawTook (ms); }       // the whole window was just drawn in this long
    // The stage lies under everything, so it is the first thing drawn when the window is: it says so here (whether most
    // of the window is being drawn, and how many pixels one unit of the layout is), and whoever listens can time it.
    std::function<void (bool mostOfIt, float pixelsPerUnit)> onDrawBegins;
    const FilmPlayer& player() const { return film; }
    void setOffscreen (bool on) { offscreen = on; }      // drawn into a picture, not a window (screenshots and checks): it still moves
    void scaleChanged();                         // the window was resized: the pictures are prepared again at the new size
    int world() const { return wantWorld; }
    bool settled() const { return current != nullptr && ! fading && ! waiting; }
    void waitUntilSettled (int timeoutMs = 8000);   // for the screenshots

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct View
    {
        int world = -1; bool second = false;
        juce::Image art, soft;                   // the framed picture at the window's own pixel size, and its glass copy
        float s = 1.0f, ox = 0.0f, oy = 0.0f, scale = 1.0f;   // how the picture was framed: its size against the window, its corner, and pixels per unit
        float topLight = 0.0f, titleLight = 0.0f; // how bright the picture is under the top bar and under the title (0..1)
    };
    struct Mote { float x = 0, y = 0, vx = 0, vy = 0, size = 1, phase = 0, age = 0, life = 1, spin = 0; };
    class Prepare;

    void timerCallback() override { advance (false); }
    void advance (bool evenIfHidden);
    void request();
    void arrived (std::shared_ptr<View> v, int serial);
    void drawView (juce::Graphics&, const View&, float alpha, bool withLife);
    void drawWeather (juce::Graphics&, float alpha);
    bool filmWanted (bool visible) const;
    void drawHaze (juce::Graphics&, float alpha);
    void spawn (Mote&, bool anywhere);
    void spawnHaze (Mote&, bool anywhere);
    void stepWeather (float dt);
    float pixelScale() const;

    std::shared_ptr<View> current, incoming;
    std::vector<Pane> panes;
    juce::Rectangle<float> open;
    int wantWorld = 7; bool wantSecond = false, title = true, moving = true, fading = false, asked = false, waiting = false;
    float fade = 0.0f, veil = 0.0f, veilNow = 0.0f, preparedScale = 0.0f;
    float clock = 0.0f;                          // seconds of movement so far (the mist keeps time by it)
    int serial = 0, weatherKind = -1, motionMode = motionAlive;
    std::vector<Mote> motes, haze;
    FilmPlayer film;
    float filmShow = 0.0f;                       // 0 the painting .. 1 the film (eased, so neither ever jumps)
    bool anyFilm = false, offscreen = false;
    mutable std::array<int, kWorlds + 1> filmThere {};    // per world: 0 not looked for yet, 1 its frames are installed, 2 they are not
    int filmW = 0, filmH = 0;                      // the size the film's pictures were last asked for
    juce::Random rng { 0x41746c61 };
    juce::uint32 lastTick = 0, resizeAt = 0;
    juce::ThreadPool pool { 1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Stage)
};
} // namespace atlas::ui
