// SPDX-License-Identifier: AGPL-3.0-or-later
// GHOST ATLAS - the Play page: the world itself, with the presets down the left and, on the water at the bottom, the
// layers as lanterns, the four macros, the master level and a keyboard.
#include "Pages.h"

namespace atlas::ui
{
namespace
{
// ============================================================================================ the layers as lanterns
class LanternRow final : public juce::Component
{
public:
    struct Item { bool used = false, muted = false; juce::String name; float level = 0.0f, gainDb = 0.0f; };
    std::array<Item, kLayers> items;
    int selected = 0;
    std::function<void (int)> onOpen, onAdd, onMute;
    std::function<void (int, float)> onLevel;
    std::function<void (int, bool)> onGesture;

    // the slots the row shows (see layerSlotsShown): each is its layer's lantern, or a place to add one
    int shown() const
    {
        int last = -1;
        for (int i = 0; i < kLayers; ++i) if (items[(size_t) i].used) last = i;
        return layerSlotsShown (last);
    }
    juce::Rectangle<float> cell (int i) const { const float w = (float) getWidth() / (float) shown(); return { w * (float) i, 0.0f, w, (float) getHeight() }; }
    void paint (juce::Graphics& g) override
    {
        const auto& l = look();
        const int n = shown();
        for (int i = 0; i < n; ++i)
        {
            const auto& it = items[(size_t) i];
            const auto c = cell (i);
            const juce::Point<float> at (c.getCentreX(), c.getY() + 30.0f);
            const bool hot = i == hover;
            {
                // (a lantern is drawn for a slot 65 wide; in a narrower one, past eight slots, it is drawn smaller)
                juce::Graphics::ScopedSaveState state (g);
                const float k = juce::jmin (1.0f, c.getWidth() / 65.0f);
                if (k < 0.999f) g.addTransform (juce::AffineTransform::scale (k, k, at.x, at.y));
                if (! it.used)
                {
                    juce::Path ring; ring.addEllipse (at.x - 15.0f, at.y - 15.0f, 30.0f, 30.0f);
                    const float dashes[2] = { 3.0f, 4.0f };
                    juce::Path dashed; juce::PathStrokeType (1.2f).createDashedStroke (dashed, ring, dashes, 2);
                    g.setColour (l.text (hot ? 0.75f : 0.36f)); g.fillPath (dashed);
                    g.drawLine (at.x - 5.0f, at.y, at.x + 5.0f, at.y, 1.3f); g.drawLine (at.x, at.y - 5.0f, at.x, at.y + 5.0f, 1.3f);
                    continue;
                }
                const float lit = it.muted ? 0.0f : juce::jlimit (0.0f, 1.0f, it.level);
                // a ring goes out over the water each time the layer starts to sound
                const float ring = rings[(size_t) i];
                if (ring > 0.0f && ring < 1.0f && ! it.muted)
                {
                    const float rr = 15.0f + 26.0f * std::sqrt (ring);
                    g.setColour (l.lantern.withAlpha (0.55f * (1.0f - ring) * (1.0f - ring)));
                    g.drawEllipse (at.x - rr, at.y - rr, rr * 2.0f, rr * 2.0f, 1.3f);
                }
                // its level, as an arc around it
                const float a0 = -2.42f, a1 = 2.42f, lev = juce::jlimit (0.0f, 1.0f, (it.gainDb + 60.0f) / 72.0f);
                juce::Path track; track.addCentredArc (at.x, at.y, 21.0f, 21.0f, 0.0f, a0, a1, true);
                g.setColour (l.text (hot ? 0.3f : 0.18f)); g.strokePath (track, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                juce::Path arc; arc.addCentredArc (at.x, at.y, 21.0f, 21.0f, 0.0f, a0, a0 + (a1 - a0) * lev, true);
                g.setColour ((it.muted ? l.text (0.4f) : l.accent).withAlpha (0.95f)); g.strokePath (arc, juce::PathStrokeType (2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                if (! it.muted)
                {   // where the arc ends, a bead of light
                    const float an = a0 + (a1 - a0) * lev;
                    const juce::Point<float> tip (at.x + std::sin (an) * 21.0f, at.y - std::cos (an) * 21.0f);
                    glow (g, tip, 7.0f, l.accent, 0.5f + 0.4f * lit);
                    g.setColour (juce::Colours::white.withAlpha (0.85f)); g.fillEllipse (tip.x - 1.7f, tip.y - 1.7f, 3.4f, 3.4f);
                }
                // the flame: a glass with the light inside it, hotter in the middle the louder the layer is
                if (! it.muted) glow (g, at, 22.0f + 38.0f * lit, l.lantern, 0.32f + 0.58f * lit);
                const auto glass = juce::Rectangle<float> (at.x - 12.5f, at.y - 12.5f, 25.0f, 25.0f);
                if (it.muted) { g.setColour (l.deep.darker (0.5f).withAlpha (0.82f)); g.fillEllipse (glass); }
                else
                {
                    const auto core = l.lantern.interpolatedWith (juce::Colours::white, 0.35f + 0.6f * lit);
                    juce::ColourGradient flame (core, at.x - 2.5f, at.y - 3.5f, l.lantern.darker (0.12f).withAlpha (0.9f), at.x + 12.5f, at.y + 12.5f, true);
                    flame.addColour (0.55, l.lantern.interpolatedWith (juce::Colours::white, 0.18f * lit));
                    g.setGradientFill (flame); g.fillEllipse (glass);
                }
                g.setColour (juce::Colours::white.withAlpha (it.muted ? 0.16f : 0.5f)); g.drawEllipse (glass.reduced (0.5f), 1.0f);
                g.setColour (it.muted ? l.text (0.55f) : l.ink().withAlpha (0.9f));
                g.setFont (sansBold (13.5f));
                g.drawText (juce::String (i + 1), glass, juce::Justification::centred, false);
            }
            // its name under it (smaller when the slots are narrow)
            const bool narrow = c.getWidth() < 56.0f;
            const auto nameBox = juce::Rectangle<int> ((int) c.getX() + 1, (int) c.getY() + (narrow ? 53 : 57), (int) c.getWidth() - 2, 32);
            g.setFont (sans (narrow ? 11.5f : 13.0f));
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.drawFittedText (it.name, nameBox.translated (0, 1), juce::Justification::centredTop, 2, 0.8f);
            g.setColour (i == selected ? l.lantern : l.text (it.muted ? 0.5f : 0.92f));
            g.drawFittedText (it.name, nameBox, juce::Justification::centredTop, 2, 0.8f);
        }
    }
    // called as the screen ticks with each layer's level: a layer that starts to sound sends a ring out. True when the picture changed.
    bool step (const std::array<float, kLayers>& levels)
    {
        bool moved = false;
        for (int i = 0; i < kLayers; ++i)
        {
            auto& it = items[(size_t) i]; auto& ring = rings[(size_t) i];
            if (it.used && ! it.muted && levels[(size_t) i] > it.level + 0.16f && (ring <= 0.0f || ring > 0.35f)) ring = 0.001f;
            if (ring > 0.0f) { ring += 0.055f; if (ring >= 1.0f) ring = 0.0f; moved = true; }
            if (std::abs (levels[(size_t) i] - it.level) > 0.01f) { it.level = levels[(size_t) i]; moved = true; }
        }
        if (moved) repaint();
        return moved;
    }
    int indexAt (juce::Point<float> p) const { const int n = shown(); return juce::jlimit (0, n - 1, (int) (p.x / juce::jmax (1.0f, (float) getWidth() / (float) n))); }
    void mouseMove (const juce::MouseEvent& e) override { const int h = indexAt (e.position); if (h != hover) { hover = h; repaint(); } }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void mouseDown (const juce::MouseEvent& e) override { down = indexAt (e.position); dragged = false; startDb = items[(size_t) down].gainDb; }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (down < 0 || ! items[(size_t) down].used) return;
        if (! dragged && std::abs (e.getDistanceFromDragStartY()) < 4) return;
        if (! dragged) { dragged = true; if (onGesture) onGesture (down, true); }
        if (onLevel) onLevel (down, juce::jlimit (-60.0f, 12.0f, startDb - (float) e.getDistanceFromDragStartY() * (e.mods.isShiftDown() ? 0.06f : 0.3f)));
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (down < 0) return;
        const int i = down; down = -1;
        if (dragged) { if (onGesture) onGesture (i, false); return; }
        if (e.getNumberOfClicks() >= 2 && items[(size_t) i].used) { if (onMute) onMute (i); return; }
        if (items[(size_t) i].used) { if (onOpen) onOpen (i); } else if (onAdd) onAdd (i);
    }
private:
    int hover = -1, down = -1; bool dragged = false; float startDb = 0.0f;
    std::array<float, kLayers> rings {};
};

// ============================================================================================ the sound, seen on the water
// Light that stands on the water where the sound is. Left to right is low to high, laid out along the keyboard beneath it:
// a note's own light stands over its key and its overtones stand to the right of it. Every note played also drops a ring.
// With no sound there is nothing here at all, and the picture is left alone.
class SoundWater final : public juce::Component
{
public:
    int first = 24, last = 96;
    float keysLeft = 24.0f, keysWidth = 960.0f;        // where the keyboard is, inside this strip
    SoundWater() { setInterceptsMouseClicks (false, false); for (int i = 0; i < kSize; ++i) window[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (kSize - 1)); }

    void drop (int note, float strength)
    {
        if (drops.size() >= 20) drops.erase (drops.begin());
        drops.push_back ({ keysLeft + Keys::centreOf (note, first, last, keysWidth), 0.0f, juce::jlimit (0.3f, 1.0f, strength) });
    }
    // thirty times a second, with the engine that is sounding. True while anything here is alight.
    bool step (const Engine& e)
    {
        const int keys = last - first + 1;
        bool heard = false;
        if (juce::jmax (e.outputLevel (0), e.outputLevel (1)) > 2.0e-4f)
        {
            std::fill (work.begin(), work.end(), 0.0f);
            e.readScope (work.data(), kSize);
            for (int i = 0; i < kSize; ++i) work[(size_t) i] *= window[(size_t) i];
            fft.performFrequencyOnlyForwardTransform (work.data(), true);
            const double sr = juce::jmax (8000.0, e.sampleRateNow());
            float loudest = -200.0f;
            for (int k = 0; k < keys; ++k)
            {
                const int note = first + k;
                const double bin = 440.0 * std::pow (2.0, (note - 69) / 12.0) * (double) kSize / sr;
                const int b = juce::jlimit (1, kSize / 2 - 2, (int) bin);
                const float frac = (float) juce::jlimit (0.0, 1.0, bin - (double) b);
                const float mag = (work[(size_t) b] * (1.0f - frac) + work[(size_t) b + 1] * frac) * 4.0f / (float) kSize;
                now[(size_t) k] = gainToDb (juce::jmax (1.0e-7f, mag)) + juce::jmax (0.0f, (float) (note - 48)) * 0.18f;       // (the upper notes are fainter: lifted a little)
                loudest = juce::jmax (loudest, now[(size_t) k]);
            }
            // Measured against the loudest note sounding (which falls back slowly), so a quiet sound still stands up off the
            // water, only less tall than a loud one.
            top = juce::jmax (loudest, top - 0.5f);
            const float stand = std::pow (juce::jlimit (0.0f, 1.0f, (top + 74.0f) / 44.0f), 0.7f);
            for (int k = 0; k < keys; ++k) now[(size_t) k] = stand * std::pow (juce::jlimit (0.0f, 1.0f, (now[(size_t) k] - (top - 40.0f)) / 40.0f), 1.3f);
            // each note's light spreads a little to either side (a peak with shoulders, not a spike)
            {
                std::array<float, 128> lone = now;
                for (int k = 0; k < keys; ++k)
                {
                    float v = lone[(size_t) k];
                    if (k > 0) v = juce::jmax (v, 0.62f * lone[(size_t) k - 1]);
                    if (k + 1 < keys) v = juce::jmax (v, 0.62f * lone[(size_t) k + 1]);
                    if (k > 1) v = juce::jmax (v, 0.28f * lone[(size_t) k - 2]);
                    if (k + 2 < keys) v = juce::jmax (v, 0.28f * lone[(size_t) k + 2]);
                    now[(size_t) k] = v;
                }
            }
            heard = true;
        }
        bool alight = false;
        for (int k = 0; k < keys; ++k)
        {
            auto& v = shown[(size_t) k];
            const float to = heard ? now[(size_t) k] : 0.0f;
            v = to > v ? v + (to - v) * 0.5f : juce::jmax (to, v * 0.87f - 0.002f);
            if (v < 0.0f) v = 0.0f;
            alight = alight || v > 0.004f;
        }
        for (auto& d : drops) d.age += 1.0f / 30.0f;
        drops.erase (std::remove_if (drops.begin(), drops.end(), [] (const Drop& d) { return d.age > kDropLife; }), drops.end());
        const bool live = alight || ! drops.empty();
        if (live || wasLive) repaint();
        wasLive = live;
        return live;
    }

    void paint (juce::Graphics& g) override
    {
        const auto& l = look();
        const int keys = last - first + 1;
        const float base = (float) (kUp * kCell);                    // the water line: light stands above it and is mirrored below
        float most = 0.0f; for (int k = 0; k < keys; ++k) most = juce::jmax (most, shown[(size_t) k]);
        if (most > 0.004f)
        {
            // The light is worked out on a coarse grid and drawn enlarged, which is what makes it soft: every column of it
            // fades out towards its own top, so nothing here has an edge.
            if (mist.isNull()) mist = juce::Image (juce::Image::ARGB, kCols, kUp + kDown, true);
            const auto body = l.accent.interpolatedWith (juce::Colours::white, 0.38f), foot = l.lantern.interpolatedWith (juce::Colours::white, 0.6f);
            juce::Image::BitmapData px (mist, juce::Image::BitmapData::writeOnly);
            int k = 0;
            for (int c = 0; c < kCols; ++c)
            {
                // how much sound there is under this column (between the two keys it lies between)
                const float x = ((float) c + 0.5f) * (float) kCell - keysLeft;
                while (k + 1 < keys && Keys::centreOf (first + k + 1, first, last, keysWidth) < x) ++k;
                const float xa = Keys::centreOf (first + k, first, last, keysWidth), xb = Keys::centreOf (first + juce::jmin (keys - 1, k + 1), first, last, keysWidth);
                float v;
                if (x <= xa) v = shown[(size_t) k] * juce::jlimit (0.0f, 1.0f, 1.0f + (x - xa) / 18.0f);
                else if (k + 1 >= keys) v = shown[(size_t) k] * juce::jlimit (0.0f, 1.0f, 1.0f - (x - xa) / 18.0f);
                else { const float f = (x - xa) / juce::jmax (0.01f, xb - xa); v = shown[(size_t) k] * (1.0f - f) + shown[(size_t) k + 1] * f; }
                auto alphaAt = [v] (float y, float& warmth) {        // y: 0 at the water, 1 at the top of the strip
                    if (v < 0.004f) { warmth = 0.0f; return 0.0f; }
                    const float u = juce::jlimit (0.0f, 1.0f, y / juce::jmax (0.03f, v)), fall = 1.0f - u * u * (3.0f - 2.0f * u);
                    const float sheet = fall * std::sqrt (fall) * (0.2f + 0.42f * v), low = std::exp (-y * 10.0f) * (0.2f + 0.45f * v) * juce::jmin (1.0f, v * 5.0f);
                    warmth = juce::jlimit (0.0f, 1.0f, low / (sheet + low + 1.0e-4f) + 0.12f);
                    return juce::jmin (0.86f, sheet + low);
                };
                for (int r = 0; r < kUp + kDown; ++r)
                {
                    float warmth = 0.0f, a;
                    if (r < kUp) a = alphaAt (((float) (kUp - r) - 0.5f) / (float) kUp, warmth);
                    else { const float d = ((float) (r - kUp) + 0.5f) / (float) kDown; a = alphaAt (d * 0.8f, warmth) * 0.42f * (1.0f - d); }
                    const auto col = body.interpolatedWith (foot, warmth);
                    const auto a8 = (juce::uint8) juce::jlimit (0, 255, (int) std::lround (a * 255.0f));
                    ((juce::PixelARGB*) px.getPixelPointer (c, r))->setARGB (a8, (juce::uint8) (col.getRed() * a8 / 255), (juce::uint8) (col.getGreen() * a8 / 255), (juce::uint8) (col.getBlue() * a8 / 255));
                }
            }
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (mist, juce::Rectangle<float> (0.0f, 0.0f, (float) (kCols * kCell), (float) ((kUp + kDown) * kCell)));
        }
        // the rings: each one widens over the water and is gone
        for (const auto& d : drops)
        {
            const float room = juce::jmin (d.x - 2.0f, (float) getWidth() - d.x - 2.0f);
            const auto colour = l.paper.interpolatedWith (l.lantern, 0.45f);
            for (int ring = 0; ring < 2; ++ring)
            {
                const float t = (d.age - 0.22f * (float) ring) / (kDropLife - 0.22f * (float) ring);
                if (t <= 0.0f || t >= 1.0f) continue;
                const float rx = juce::jmin (room, 5.0f + 78.0f * (1.0f - (1.0f - t) * (1.0f - t))), ry = rx * 0.23f;
                const float a = d.strength * (1.0f - t) * (1.0f - t) * (ring == 0 ? 1.0f : 0.6f);
                g.setColour (colour.withAlpha (0.16f * a)); g.drawEllipse (d.x - rx, base - 2.0f - ry, rx * 2.0f, ry * 2.0f, 3.6f);
                g.setColour (colour.withAlpha (0.62f * a)); g.drawEllipse (d.x - rx, base - 2.0f - ry, rx * 2.0f, ry * 2.0f, 1.2f);
            }
            if (d.age < 0.25f) glow (g, { d.x, base - 2.0f }, 18.0f, l.lantern, 0.65f * d.strength * (1.0f - d.age / 0.25f));
        }
    }

private:
    static constexpr int kOrder = 12, kSize = 1 << kOrder;
    static constexpr int kCell = 4, kCols = 250, kUp = 24, kDown = 7;      // the grid the light is worked out on: 4 units a cell
    static constexpr float kDropLife = 2.2f;
    juce::Image mist;
    struct Drop { float x, age, strength; };
    juce::dsp::FFT fft { kOrder };
    std::array<float, (size_t) kSize * 2> work {};
    std::array<float, (size_t) kSize> window {};
    std::array<float, 128> now {}, shown {};
    std::vector<Drop> drops;
    bool wasLive = false;
    float top = -200.0f;
};

// ============================================================================================ the eight worlds, as small pictures
class WorldGrid final : public juce::Component, public juce::SettableTooltipClient
{
public:
    int selected = -1;
    std::function<void (int)> onPick;
    juce::Rectangle<float> cell (int i) const
    {
        const float gap = 6.0f, w = ((float) getWidth() - gap * 3.0f) / 4.0f, h = ((float) getHeight() - gap) / 2.0f;
        return { (w + gap) * (float) (i % 4), (h + gap) * (float) (i / 4), w, h };
    }
    void paint (juce::Graphics& g) override
    {
        const auto& l = look();
        for (int i = 0; i < kWorlds; ++i)
        {
            const auto c = cell (i);
            juce::Path shape; shape.addRoundedRectangle (c, 6.0f);
            {
                juce::Graphics::ScopedSaveState ss (g);
                g.reduceClipRegion (shape);
                g.setOpacity (selected < 0 || selected == i || hover == i ? 1.0f : 0.5f);
                g.drawImage (worldThumb (i), c, juce::RectanglePlacement::fillDestination);
            }
            if (i == selected) { g.setColour (l.lantern); g.strokePath (shape, juce::PathStrokeType (2.0f)); }
            else if (i == hover) { g.setColour (l.text (0.6f)); g.strokePath (shape, juce::PathStrokeType (1.2f)); }
        }
    }
    int indexAt (juce::Point<float> p) const { for (int i = 0; i < kWorlds; ++i) if (cell (i).contains (p)) return i; return -1; }
    void mouseMove (const juce::MouseEvent& e) override
    {
        const int h = indexAt (e.position);
        if (h != hover) { hover = h; setTooltip (h >= 0 ? juce::String (world (h).name) : juce::String()); repaint(); }
    }
    void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
    void mouseUp (const juce::MouseEvent& e) override { const int i = indexAt (e.position); if (i >= 0 && onPick) onPick (i); }
private:
    int hover = -1;
};

// ============================================================================================ the page
class PlayPage final : public Page
{
public:
    explicit PlayPage (Shell& s) : Page (s)
    {
        search.setHint ("Search sounds");
        search.onChange = [this] (const juce::String& t) { query = t.trim(); rebuild(); };
        addAndMakeVisible (search);

        worlds.onPick = [this] (int w) {
            worldFilter = worldFilter == w ? -1 : w; categoryFilter.clear(); worlds.selected = worldFilter;
            if (worldFilter >= 0 && shell.showWorld) shell.showWorld (worldFilter);
            rebuild();
        };
        addAndMakeVisible (worlds);

        static const char* modeNames[4] = { "All", "Favourites", "Recent", "Mine" };
        for (int i = 0; i < 4; ++i)
        {
            auto& b = modes[(size_t) i];
            b.text = modeNames[i]; b.style = TextBtn::quiet; b.size = 14.0f;
            b.onClick = [this, i] { mode = i; rebuild(); };
            addAndMakeVisible (b);
        }
        category.menuParent = shell.canvas;
        category.get = [this] { return (float) categoryIndex(); };
        category.set = [this] (float v) {
            const int i = (int) v;
            if (i <= 0) categoryFilter.clear();
            else { categoryFilter = categories()[(size_t) (i - 1)].name; worldFilter = categories()[(size_t) (i - 1)].world; worlds.selected = worldFilter; if (shell.showWorld) shell.showWorld (worldFilter); }
            rebuild();
        };
        category.choices.add ("Every kind of sound");
        for (const auto& c : categories()) category.choices.add (c.name);
        addAndMakeVisible (category);

        list.setRowHeight (31);
        list.count = [this] { return (int) shown.size(); };
        list.paintRow = [this] (int row, juce::Graphics& g, juce::Rectangle<float> r, bool, bool) { paintPreset (row, g, r); };
        list.clicked = [this] (int row, const juce::MouseEvent& e) {
            if (row < 0 || row >= (int) shown.size()) return;
            const auto& p = shown[(size_t) row];
            if (e.x < 30) { { const juce::ScopedLock sl (shell.proc.bankLock()); shell.proc.bank().setFavourite (p.file, ! p.favourite); } rebuild(); return; }
            shell.proc.loadPresetFile (p.file);
        };
        list.chosen = [this] (int row) { if (! quiet && row >= 0 && row < (int) shown.size() && shown[(size_t) row].file != shell.proc.presetFile()) shell.proc.loadPresetFile (shown[(size_t) row].file); };
        addAndMakeVisible (list);

        lanterns.onOpen = [this] (int i) { shell.layer = i; if (shell.goTo) shell.goTo (pageLayers); };
        lanterns.onAdd = [this] (int i) { if (shell.pickInstrument) shell.pickInstrument (i); };
        lanterns.onMute = [this] (int i) { auto& e = shell.proc.engine(); e.setLayerParam (i, lpMute, e.getLayerParam (i, lpMute) > 0.5f ? 0.0f : 1.0f); refresh(); };
        lanterns.onLevel = [this] (int i, float db) { shell.proc.setHost (Processor::hpLevel1 + i, db); lanterns.items[(size_t) i].gainDb = db; lanterns.repaint(); };
        lanterns.onGesture = [this] (int i, bool start) { if (start) shell.proc.beginGesture (Processor::hpLevel1 + i); else shell.proc.endGesture (Processor::hpLevel1 + i); };
        addAndMakeVisible (water);            // (behind the lanterns and the knobs)
        addAndMakeVisible (lanterns);

        for (int i = 0; i < kMacros; ++i)
        {
            auto& k = macros[(size_t) i];
            k.lo = 0.0f; k.hi = 1.0f; k.def = 0.0f; k.valueSize = 14.0f; k.labelSize = 14.5f;
            k.text = [] (float v) { return juce::String ((int) std::lround (v * 100.0f)); };
            k.get = [this, i] { return shell.proc.getHost (Processor::hpMacro1 + i); };
            k.set = [this, i] (float v) { shell.proc.setHost (Processor::hpMacro1 + i, v); };
            k.begin = [this, i] { shell.proc.beginGesture (Processor::hpMacro1 + i); };
            k.end = [this, i] { shell.proc.endGesture (Processor::hpMacro1 + i); };
            k.usable = [this, i] { return macroAimed[(size_t) i]; };
            addAndMakeVisible (k);
        }
        master.label = "Master"; master.lo = -60.0f; master.hi = 12.0f; master.def = 0.0f; master.valueSize = 13.0f; master.labelSize = 14.5f;
        master.text = [] (float v) { return v <= -59.5f ? juce::String ("off") : juce::String (v, 1); };
        master.get = [this] { return shell.proc.getHost (Processor::hpMaster); };
        master.set = [this] (float v) { shell.proc.setHost (Processor::hpMaster, v); };
        master.begin = [this] { shell.proc.beginGesture (Processor::hpMaster); };
        master.end = [this] { shell.proc.endGesture (Processor::hpMaster); };
        addAndMakeVisible (master);
        addAndMakeVisible (meter);

        keys.onNote = [this] (int n, float v, bool on) { shell.proc.engine().noteFromScreen (n, v, on); };
        keys.held = [this] (int n) { return shell.proc.noteHeld (n); };
        keys.playable = [this] (int n) { return n >= 0 && n < 128 && playable[(size_t) n]; };
        addAndMakeVisible (keys);
        refresh();
    }

    std::vector<Stage::Pane> panes() const override
    {
        return { { { 0.0f, 0.0f, (float) kBaseW, 104.0f }, 0.0f, Stage::scrimTop },
                 { { 316.0f, 404.0f, (float) kBaseW - 316.0f, (float) kBaseH - 404.0f }, 0.0f, Stage::scrimBottom },
                 { { 16.0f, 68.0f, 300.0f, 636.0f }, 14.0f, Stage::glass } };
    }
    juce::Rectangle<float> open() const override { return { 332.0f, 62.0f, 972.0f, 452.0f }; }
    bool showsTitle() const override { return true; }
    bool secondView() const override { return false; }

    void resized() override
    {
        search.setBounds (28, 80, 276, 34);
        worlds.setBounds (28, 124, 276, 84);
        int x = 26;
        static const int widths[4] = { 38, 86, 62, 48 };
        for (int i = 0; i < 4; ++i) { modes[(size_t) i].setBounds (x, 216, widths[i], 28); x += widths[i] + 8; }
        category.setBounds (28, 250, 276, 30);
        list.setBounds (20, 288, 292, 408);
        lanterns.setBounds (344, 520, 520, 92);
        for (int i = 0; i < kMacros; ++i) macros[(size_t) i].setBounds (884 + i * 78, 522, 72, 86);
        master.setBounds (1204, 522, 72, 86);
        meter.setBounds (1284, 532, 14, 54);
        keys.setBounds (344, 634 - (int) keys.room, 960, 62 + (int) keys.room);
        water.setBounds (320, 404, 1000, 124);       // (its water line lies just above the lanterns)
        water.first = keys.first; water.last = keys.last; water.keysLeft = 24.0f; water.keysWidth = 960.0f;
    }

    void refresh() override
    {
        const auto p = shell.proc.capture();
        auto& lib = shell.proc.library();
        playable.fill (false);
        for (int i = 0; i < kLayers; ++i)
        {
            const auto& ls = p.layers[(size_t) i];
            auto& it = lanterns.items[(size_t) i];
            it.used = ls.used(); it.muted = ls.p[lpMute] > 0.5f; it.gainDb = ls.p[lpGain];
            it.name = ls.label.isNotEmpty() ? ls.label : shortName (ls.instrument, shell.proc);
            if (! it.used) continue;
            Instrument inst;
            const bool known = lib.findLoose (ls.instrument, inst);
            const int coarse = (int) ls.p[lpCoarse];
            for (int k = juce::jmax (0, (int) ls.p[lpKeyLo]); k <= juce::jmin (127, (int) ls.p[lpKeyHi]); ++k)
                if (! known || inst.plays (k + coarse)) playable[(size_t) k] = true;
        }
        lanterns.selected = shell.layer;
        for (int i = 0; i < kMacros; ++i)
        {
            macros[(size_t) i].label = p.macros[(size_t) i].name.isNotEmpty() ? worded (p.macros[(size_t) i].name) : "Macro " + juce::String (i + 1);
            macroAimed[(size_t) i] = ! p.macros[(size_t) i].targets.empty();
            macros[(size_t) i].setTooltip (macroAimed[(size_t) i] ? juce::String() : "This macro is not aimed at anything in this sound");
        }
        empty = lib.size() == 0;
        rebuild();
        repaint();
    }

    void tick() override
    {
        auto& e = shell.proc.engine();
        std::array<float, kLayers> levels {};
        for (int i = 0; i < kLayers; ++i) levels[(size_t) i] = juce::jlimit (0.0f, 1.0f, std::sqrt (e.layerLevel (i)) * 1.5f);
        lanterns.step (levels);
        meter.setLevels (e.outputLevel (0), e.outputLevel (1));
        // a key that has just gone down drops a ring on the water above it
        for (int n = keys.first; n <= keys.last; ++n)
        {
            const bool down = shell.proc.noteHeld (n);
            if (down && ! wasDown[(size_t) n]) water.drop (n, 0.8f);
            wasDown[(size_t) n] = down;
        }
        keys.step();
        water.step (e);
        for (auto& k : macros) if (std::abs (k.value() - k.getProperties().getWithDefault ("v", -1.0f).operator float()) > 0.0005f) { k.getProperties().set ("v", k.value()); k.repaint(); }
        if (std::abs (master.value() - masterShown) > 0.005f) { masterShown = master.value(); master.repaint(); }
    }

    void paint (juce::Graphics& g) override
    {
        const auto& l = look();
        if (shown.empty())
        {
            g.setColour (l.text (0.7f)); g.setFont (sans (15.0f));
            const juce::String msg = mode == 1 ? "No favourites yet. Click the heart beside a sound to keep it here."
                                   : mode == 2 ? "Sounds you load will be listed here."
                                   : mode == 3 ? "Your own sounds will be listed here. Change a sound, then choose Save."
                                               : "No sound matches. Try fewer words, or another world.";
            g.drawFittedText (msg, 36, 300, 260, 80, juce::Justification::topLeft, 4);
        }
        if (empty)
        {
            const auto box = juce::Rectangle<float> (348.0f, 430.0f, 600.0f, 64.0f);
            g.setColour (l.deep.darker (0.7f).withAlpha (0.78f)); g.fillRoundedRectangle (box, 10.0f);
            g.setColour (l.lantern); g.setFont (sansBold (15.5f));
            g.drawText ("No sample libraries are installed yet, so these sounds are silent.", box.reduced (16.0f, 8.0f).removeFromTop (22.0f), juce::Justification::centredLeft, true);
            g.setColour (l.text (0.85f)); g.setFont (sans (14.5f));
            g.drawText ("Open Library and add a folder of samples, or run the starter library download.", box.reduced (16.0f, 8.0f).removeFromBottom (22.0f), juce::Justification::centredLeft, true);
        }
    }

private:
    int categoryIndex() const { for (size_t i = 0; i < categories().size(); ++i) if (categoryFilter == categories()[i].name) return (int) i + 1; return 0; }

    void rebuild()
    {
        std::vector<PresetInfo> all;
        juce::StringArray recents;
        {
            const juce::ScopedLock sl (shell.proc.bankLock());
            all = shell.proc.bank().list(); recents = shell.proc.bank().recent();
        }
        shown.clear();
        const auto words = juce::StringArray::fromTokens (query.toLowerCase(), " ", "");
        for (const auto& p : all)
        {
            if (mode == 1 && ! p.favourite) continue;
            if (mode == 2 && ! recents.contains (p.file)) continue;
            if (mode == 3 && p.factory) continue;
            if (categoryFilter.isNotEmpty() && p.category != categoryFilter) continue;
            if (worldFilter >= 0 && worldIndex (p.world) != worldFilter) continue;
            bool match = true;
            if (! words.isEmpty())
            {
                const auto hay = (p.name + " " + p.category + " " + p.tags.joinIntoString (" ") + " " + p.author + " " + p.instruments.joinIntoString (" ")).toLowerCase();
                for (const auto& w : words) if (w.isNotEmpty() && ! hay.contains (w)) { match = false; break; }
            }
            if (match) shown.push_back (p);
        }
        if (mode == 2) std::stable_sort (shown.begin(), shown.end(), [&recents] (const PresetInfo& a, const PresetInfo& b) { return recents.indexOf (a.file) < recents.indexOf (b.file); });
        for (int i = 0; i < 4; ++i) { modes[(size_t) i].selected = i == mode; modes[(size_t) i].repaint(); }
        category.repaint(); worlds.repaint();
        quiet = true;
        list.updateContent();
        int row = -1;
        for (size_t i = 0; i < shown.size(); ++i) if (shown[i].file == shell.proc.presetFile()) row = (int) i;
        if (row >= 0) list.selectRow (row, true, true); else list.deselectAllRows();
        quiet = false;
        list.repaint(); repaint();
    }

    void paintPreset (int row, juce::Graphics& g, juce::Rectangle<float> r)
    {
        if (row < 0 || row >= (int) shown.size()) return;
        const auto& l = look();
        const auto& p = shown[(size_t) row];
        const bool current = p.file == shell.proc.presetFile();
        if (current) { g.setColour (l.lantern.withAlpha (0.15f)); g.fillRoundedRectangle (r.reduced (2.0f, 1.0f), 7.0f); }
        heart (g, { r.getX() + 9.0f, r.getCentreY() - 6.5f, 13.0f, 13.0f }, p.favourite, p.favourite ? l.lantern : l.text (0.34f));
        g.setColour (current ? l.lantern : l.text (0.93f));
        const auto kind = juce::String (p.category);
        const float kindW = juce::jmin (104.0f, juce::GlyphArrangement::getStringWidth (sans (12.5f), kind) + 4.0f);
        drawFitted (g, p.name, r.withTrimmedLeft (32.0f).withTrimmedRight (kindW + 14.0f), juce::Justification::centredLeft, current ? sansBold (15.5f) : sans (15.5f), 0.74f);
        g.setColour (juce::Colour (world (worldIndex (p.world)).accent).withAlpha (0.78f));
        drawFitted (g, kind, r.withTrimmedRight (10.0f).removeFromRight (kindW), juce::Justification::centredRight, sans (12.5f), 0.8f);
    }

    TextBox search;
    WorldGrid worlds;
    std::array<TextBtn, 4> modes;
    Choice category;
    RowList list;
    LanternRow lanterns;
    SoundWater water;
    std::array<bool, 128> wasDown {};
    std::array<Knob, kMacros> macros;
    std::array<bool, kMacros> macroAimed {};
    Knob master;
    Meter meter;
    Keys keys;
    std::vector<PresetInfo> shown;
    std::array<bool, 128> playable {};
    juce::String query, categoryFilter;
    int mode = 0, worldFilter = -1;
    bool quiet = false, empty = false;
    float masterShown = -999.0f;
};
} // namespace

std::unique_ptr<Page> makePlayPage (Shell& s) { return std::make_unique<PlayPage> (s); }
} // namespace atlas::ui
