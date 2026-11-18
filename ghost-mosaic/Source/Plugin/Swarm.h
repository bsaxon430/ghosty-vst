// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
//
// THE LIVE VIEW. Two looks of the same library:
//  - DOTS: a JUCE port of Bencho's "Particles" (MIT, bencho.dev/licence; see NOTICE.md).
//    Each dot is a piece of the library, tinted by the sound it was cut from; it flashes when
//    the engine plays it. MUTATE (or a press on the view) fires the shockwave and the dots
//    gather again into the next shape.
//  - HEAT: which pieces are being used, as a heat map drawn the way Bencho's "Heat map"
//    (MIT) draws one: a soft grey field, then every pixel looked up in a palette (ours).
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "Engine.h"

namespace mosaic
{
class Swarm : public juce::Component, public juce::SettableTooltipClient
{
public:
    Swarm();

    void setLibrary (std::shared_ptr<const LibraryPicture> pic);
    void played (int block, int channel);       // a library piece was just played
    void shockwave();                           // MUTATE / GENERATE: from the middle, then the next shape
    void setHeatView (bool heat) { m_heat = heat; repaint(); }
    bool heatView() const { return m_heat; }
    void tick();                                // the editor's frame clock (60 Hz)
    int numDots() const { return (int) m_dots.size(); }
    int shapeIndex() const { return m_shape; }
    bool settled() const { return ! m_busy; }
    static constexpr int kGhostShape = 3;
    std::vector<juce::Point<float>> homePoints() const;   // tests: where every dot is going (480 x 400 space)

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;

    std::function<void()> onPress;              // (the editor shows a hint the first time)

private:
    struct Dot { float hx, hy, x, y, vx, vy; float flash = 0; float heat = 0; int sound = 0; juce::Colour tint; };
    void become (int shape, bool fresh);
    void burst (juce::Point<float> p, float strength);
    juce::Point<float> toLocal (juce::Point<float> screen) const;
    void paintDots (juce::Graphics&);
    void paintHeat (juce::Graphics&);
    void paintEmpty (juce::Graphics&);

    std::vector<Dot> m_dots;
    std::shared_ptr<const LibraryPicture> m_pic;
    int m_blocksPerDot = 1;
    int m_shape = 3;                            // the ghost (Swarm.cpp: kGhost) on first open
    std::optional<juce::Point<float>> m_hand;
    bool m_busy = false, m_heat = false;
    juce::AffineTransform m_toView;
    float m_scale = 1, m_spacing = 10;
    double m_lastTick = 0;
    struct Lit { int dot; float life; };
    std::vector<Lit> m_links;                  // the last played pieces, drawn with their network neighbours
    // heat field
    std::vector<float> m_field;
    juce::Image m_heatImg;
};
}
