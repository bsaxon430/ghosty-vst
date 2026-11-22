// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
//
// The GHOST MOSAIC wordmark: a JUCE port of Bencho's "Heat map" / "Heat word" (MIT,
// bencho.dev/licence; see NOTICE.md), in our colours. Flat grey at rest; around the pointer
// - and, while the engine plays, around the part of the target being rebuilt - the word turns
// into a heat map of its own shape.
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace mosaic
{
namespace heat
{
// the palette, rim (0) to core (1), as a 256-entry RGBA table (the "Ghost" palette: an acid
// green rim, a white-lilac band, an electric purple body, a deep violet core)
const juce::uint8* lut();
// the grey at rest: alpha per field brightness
const juce::uint8* greyAlpha();
inline constexpr juce::uint8 kGrey[3] = { 150, 150, 154 };
inline constexpr int kFloor = 34;   // Bencho: below the floor it is the card
}

class HeatWord : public juce::Component, private juce::Timer
{
public:
    explicit HeatWord (juce::String word = "GHOST MOSAIC");
    // the engine's part: where in the target it is (0..1) and how loud (0..1); 0 loudness = none.
    // where < 0: music is playing but there is no playhead to follow (the host's song, the track coming in):
    // the glow roams along the whole word by itself, swelling with the music
    void setAudioHeat (float where, float level);
    // the letters' ink is fitted to the component's width minus this much on each side (component px)
    void setInset (float px) { if (px != m_inset) { m_inset = px; if (getWidth() > 0) resized(); } }
    // where the letters' ink actually sits, in this component's coordinates (for lining things up under it)
    juce::Rectangle<float> inkBounds() const { return m_ink; }
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseEnter (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    float power() const { return m_power; }
    float grown() const { return m_grow; }          // 0 = not drawn yet .. 1 = fully drawn (it grows in once, when it first appears)
    // tests and the studio harness: no timer and no wall clock; advance (dt) is one step of what the timer does
    void setManualClock (bool on);
    void advance (float dtSeconds);

private:
    void timerCallback() override;
    void build();
    void paintField();
    void run();
    bool step (float dt);                           // one frame; false = everything has come to rest

    juce::String m_word;
    juce::Image m_shown;
    std::vector<juce::uint8> m_base;       // the blurred word: the field
    int m_w = 0, m_h = 0;
    float m_px = 0.5f, m_py = 0.5f, m_tx = 0.5f, m_ty = 0.5f, m_power = 0, m_want = 0, m_grow = 0;
    float m_audioX = 0.5f, m_audioLevel = 0, m_audioPower = 0, m_ax = 0.5f;
    bool m_roam = false, m_built = false, m_manual = false, m_moving = false;
    float m_roamPhase = 0, m_pulse = 0, m_ay = 0.5f;
    float m_inset = 0;
    juce::Rectangle<float> m_ink;
    double m_prev = 0;
};
}
