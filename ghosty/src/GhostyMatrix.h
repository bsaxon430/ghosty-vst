#pragma once
// Ghosty Pro 2's header: the dot-matrix GHOSTY row (Brandon's design). Six tiles of 7x7 glowing dots. When the window opens the row plays
// the opening of his reference clip once (patterns, then every tile morphs into its letter) and stays on GHOSTY. Moving the cursor over a
// tile replays the clip for that tile only (each tile is independent). UI only: no audio thread, no locks, no timer of its own: the editor's
// 30 Hz timer calls tick() while the window is showing, and tick() says whether anything is still moving (nothing repaints when idle).
#include "JuceHeader.h"

namespace ghosty {

class MatrixRow : public Component, public SettableTooltipClient {
  public:
    static constexpr int kTiles = 6, kDots = 7;
    MatrixRow();

    // advance to `nowMs` (a millisecond clock); true while any tile is still animating (repaint needed)
    bool tick(double nowMs);
    bool idle() const;
    bool tileAnimating(int tile) const { return tile >= 0 && tile < kTiles && state_[(size_t) tile].start >= 0.0; }
    // the cursor is over `tile`: replay that tile (re-entering restarts it)
    void hoverTile(int tile, double nowMs);
    int tileAt(Point<float> p) const;
    // brightness 0..1 of a dot right now (tests)
    float dotLevel(int tile, int row, int col) const;
    static bool glyphBit(int tile, int row, int col);

    void paint(Graphics&) override;
    void resized() override;
    void mouseMove(const MouseEvent&) override;
    void mouseDown(const MouseEvent&) override;
    void mouseExit(const MouseEvent&) override;

  private:
    struct Tile { double start = -1.0; double speed = 1.0; };   // start < 0: idle on its letter
    void rebuildSprites(float scale);
    Rectangle<float> tileRect(int t) const;
    double now_ = 0.0;
    bool intro_ = true;           // the opening play starts on the first tick
    int hover_ = -1;
    std::array<Tile, kTiles> state_;
    std::vector<Image> sprites_;  // the dot at 33 brightness levels
    Colour spriteGreen_, spritePurple_;
    float spriteScale_ = 0.0f;
    int spriteTile_ = 0;
    bool spriteSketch_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MatrixRow)
};


// where the pieces of the header's logo area go (one place, used by the editor and its tests): the ghost box, the row of six tiles, the tag
struct HeaderLogoLayout { Rectangle<int> ghost, row, tag; };
HeaderLogoLayout layoutHeaderLogo(Rectangle<int> header, int logoWidth);
// the ghost (floating, glow pulsing, fitted inside its box) and the small PRO 2 / VST tag under the row
void paintHeaderLogo(Graphics& g, const HeaderLogoLayout& l, float seconds);

}
