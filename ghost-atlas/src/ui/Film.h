// SPDX-License-Identifier: AGPL-3.0-or-later
// GHOST ATLAS - the worlds' film. Each world's painting was made from one frame of the owner's recording; the frames that
// follow it in the recording are kept beside the library (<home>/Film/<world>.gafilm, made by tools/make_film.py), and
// played slowly forwards and back again, so the world moves and always comes home to its painting.
//
// A film never touches the message thread except to hand over a finished picture: its frames are decoded, blended and
// brought to the window's size on a thread of their own.
#pragma once
#include "Theme.h"

namespace atlas::ui
{
// one world's run of frames
class Film
{
public:
    static juce::File folder();                          // <home>/Film, or $GHOST_ATLAS_FILM
    static bool exists (const juce::String& worldKey);

    bool open (const juce::String& worldKey);            // reads the file whole (a few MB); false: not there, or not a film
    const juce::String& world() const { return key; }
    int frames() const { return (int) where.size(); }
    int width() const { return w; }
    int height() const { return h; }
    float pace() const { return secondsOnScreen; }       // how long each frame is given on screen (slower for busier runs)

    // a frame as plain bytes: red, green, blue for each pixel, row after row. Decoded when first asked for; the last few are kept.
    struct Plain { int w = 0, h = 0; std::vector<juce::uint8> rgb; };
    std::shared_ptr<const Plain> frame (int index);

private:
    juce::String key;
    juce::MemoryBlock data;
    std::vector<std::pair<size_t, size_t>> where;        // each frame's place and length in data
    int w = 0, h = 0;
    float secondsOnScreen = 0.2f;
    std::vector<std::pair<int, std::shared_ptr<const Plain>>> kept;
};

// plays a film into pictures of the window's own size
class FilmPlayer final : private juce::Thread, private juce::AsyncUpdater
{
public:
    FilmPlayer();
    ~FilmPlayer() override;

    // What is wanted (message thread; cheap to say again): which world, the size of the stage in pixels, how the painting
    // is framed in it (the picture's left, top, width and height in those pixels), and whether the film should be running.
    struct Ask
    {
        juce::String world;
        int pixelW = 0, pixelH = 0;
        float left = 0, top = 0, width = 0, height = 0;
        bool running = false;
        bool operator== (const Ask& o) const { return world == o.world && pixelW == o.pixelW && pixelH == o.pixelH && left == o.left && top == o.top && width == o.width && height == o.height && running == o.running; }
    };
    void ask (const Ask&);

    // the newest picture, with where the run stood when it was made (message thread). False: none for this world yet.
    struct Shown { juce::Image image; float position = 0.0f; int frames = 0; };
    bool latest (const juce::String& world, Shown&);

    std::function<void()> onFrame;                       // a new picture is ready (message thread)
    float lastMakeMs() const { return makeMs.load(); }   // how long the last picture took to make
    // How long the window just took to draw (message thread). The film gives the screen room: it never asks for more than
    // two pictures in every five such lengths of time, and never spends more than half of its own thread's time making
    // them, so a slow machine or a very large window gets a slower film and not a slow plug-in.
    void drawTook (float ms) { const float was = drawMs.load(); drawMs.store (ms > was ? ms : was + (ms - was) * 0.15f); }
    float pictureEveryMs() const { return juce::jlimit (66.0f, 250.0f, juce::jmax (drawMs.load() * 2.5f, makeMs.load() * 2.0f)); }
    int made() const { return count.load(); }

private:
    void run() override;
    void handleAsyncUpdate() override { if (onFrame) onFrame(); }
    void render (const Film::Plain& a, const Film::Plain* b, int blend256, const Ask& how, juce::Image& into);

    juce::CriticalSection lock;
    Ask want;
    Shown front;
    juce::String frontWorld;
    std::array<juce::Image, 3> pool;
    std::vector<juce::uint8> mixed;
    std::atomic<float> makeMs { 0.0f }, drawMs { 0.0f };
    std::atomic<int> count { 0 };
};
} // namespace atlas::ui
