#pragma once
#include "TypeIn.h"
#include "PluginProcessor.h"
#include "WaveRegion.h"
#include <juce_audio_utils/juce_audio_utils.h>

#include "GhostThemes.h"

namespace gsui {
namespace th = ghosty::themes;
// GhostBlock palette: the house themes (Shared/GhostThemes.h: BLACK / PURPLE / LIGHT). The named colours below are the CURRENT
// theme, read at paint time; setTheme() swaps them. Every colour comes from the theme's tokens (the accents keep Poltergeist's
// hues only where they carry meaning: the ghost colours and the slice blue, made readable on the panel). Every surface is opaque.
struct Palette {
    th::Theme theme = th::Theme::Dark;
    bool dark = true;                    // = not the LIGHT theme (dark text-on-light vs light-on-dark decisions)
    juce::Colour bg, card, card2, ink, grey, dim, purple, violet, green, red, blue, amber, pink, cyan, keyWhite, keyBlack, onAccent, line, ghost[7];
    static Palette make(th::Theme t) {
        const auto& T = th::get(t); Palette p; p.theme = t; p.dark = !T.isLight;
        p.bg = T.window; p.card = T.panel; p.card2 = T.cell; p.ink = T.ink; p.grey = T.dim; p.dim = T.line; p.line = T.line;
        p.purple = T.violet; p.violet = T.isLight ? T.violet.brighter(0.15f) : T.violet.darker(0.25f);
        p.green = T.accent3; p.red = T.bad; p.amber = T.warn; p.pink = T.accent1; p.cyan = T.accent2;
        for (auto* a : { &p.purple, &p.green, &p.red, &p.amber, &p.pink, &p.cyan }) *a = th::readableOn(*a, T.panel, 3.2);   // (accents read on the panels: >= 3:1)
        p.blue = th::readableOn(juce::Colour(0xff60a5fa), T.panel, 3.2);                     // (slice colours: a blue distinct from cyan)
        p.keyWhite = T.keyWhite; p.keyBlack = T.keyBlack; p.onAccent = T.onAccent;
        const juce::uint32 g[7] = { 0, 0xff5fb24a, 0xff9b74d2, 0xff5bd7f5, 0xfffbbf24, 0xfff472b6, 0xffa8b3c4 };   // ghost 1 = the body text colour
        p.ghost[0] = T.ink; for (int i = 1; i < 7; ++i) p.ghost[i] = th::readableOn(juce::Colour(g[i]), T.panel, 3.2);
        // the theme switch re-maps colours set on components by value: keep every role's colour unique (1 step apart at most)
        juce::Colour* all[] = { &p.bg, &p.card, &p.card2, &p.ink, &p.grey, &p.dim, &p.purple, &p.violet, &p.green, &p.red, &p.blue, &p.amber, &p.pink, &p.cyan, &p.keyWhite,
                                &p.ghost[0], &p.ghost[1], &p.ghost[2], &p.ghost[3], &p.ghost[4], &p.ghost[5], &p.ghost[6] };
        for (size_t i = 0; i < std::size(all); ++i)
            for (bool clash = true; clash; ) { clash = false; for (size_t j = 0; j < i; ++j) if ((all[j]->getARGB() & 0xffffff) == (all[i]->getARGB() & 0xffffff)) { clash = true; break; }
                                               if (clash) { const auto b = all[i]->getBlue(); *all[i] = juce::Colour(all[i]->getRed(), all[i]->getGreen(), (juce::uint8) (b < 255 ? b + 1 : b - 1), all[i]->getAlpha()); } }
        return p;
    }
    static Palette make(bool darkLook) { return make(darkLook ? th::Theme::Dark : th::Theme::Light); }   // (older call sites)
};
inline juce::Colour BG { 0xff05040a }, CARD { 0xff0b0a13 }, CARD2 { 0xff110f1c }, INK { 0xfff5f3ff }, GREY { 0xff9ca3af },
                    DIM { 0xff6b7280 }, PURPLE { 0xffa855f7 }, VIOLET { 0xff8b5cf6 }, GREEN { 0xff00ff88 }, RED { 0xfff87171 }, BLUE { 0xff60a5fa },
                    AMBER { 0xfffbbf24 }, PINK { 0xfff472b6 }, CYAN { 0xff22d3ee }, KEYWHITE { 0xffe9e5f5 }, KEYBLACK { 0xff110a1b }, ON_ACCENT { 0xff07040d }, LINE { 0xff3a2a55 };
inline juce::Colour GHOST_COL[7] = { juce::Colour(0xfff5f3ff), juce::Colour(0xff5fb24a), juce::Colour(0xff9b74d2), juce::Colour(0xff5bd7f5),
                                    juce::Colour(0xfffbbf24), juce::Colour(0xfff472b6), juce::Colour(0xffa8b3c4) };   // ghosts 1-6 + the noise ghost
inline Palette& currentPalette();
inline bool isDark() { return currentPalette().dark; }
inline th::Theme currentTheme() { return currentPalette().theme; }
// a line / grid / hairline colour: the theme's overlay token (white on the dark looks, ink on LIGHT)
inline juce::Colour FG(float a) { const auto& T = th::get(currentTheme()); return T.overlay.withAlpha(T.isLight ? juce::jmin(1.0f, a * 1.8f) : a); }
// switch the theme (message thread); the editor then re-applies component colours and repaints
inline void applyPalette(const Palette& p) {
    BG = p.bg; CARD = p.card; CARD2 = p.card2; INK = p.ink; GREY = p.grey; DIM = p.dim; PURPLE = p.purple; VIOLET = p.violet; GREEN = p.green;
    RED = p.red; BLUE = p.blue; AMBER = p.amber; PINK = p.pink; CYAN = p.cyan; KEYWHITE = p.keyWhite; KEYBLACK = p.keyBlack; ON_ACCENT = p.onAccent; LINE = p.line;
    for (int i = 0; i < 7; ++i) GHOST_COL[i] = p.ghost[i];
}
inline Palette& currentPalette() { static Palette p = [] { auto q = Palette::make(th::Theme::Dark); applyPalette(q); return q; }(); return p; }
// an accent used as TEXT: the same hue, made to read >= 4.5:1 on the surface under it
inline juce::Colour textOn(juce::Colour c, juce::Colour bg) { return th::readableOn(c, bg, 5.2); }   // (5.2: anti-aliased glyphs measure a little lower)
inline void setTheme(th::Theme t) { currentPalette() = Palette::make(t); applyPalette(currentPalette()); }

struct LookAndFeel : juce::LookAndFeel_V4 {
    explicit LookAndFeel(bool alwaysDark = false);
    bool forceDark;                      // (optional) a dark island that ignores the theme
    Palette pal() const { return forceDark ? Palette::make(th::Theme::Dark) : currentPalette(); }
    void applyColours();                 // the colour ids from the palette (after a theme switch)
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawPopupMenuBackground(juce::Graphics&, int w, int h) override;
    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
    void drawComboBox(juce::Graphics&, int w, int h, bool down, int, int, int, int, juce::ComboBox&) override;
    void drawLinearSlider(juce::Graphics&, int x, int y, int w, int h, float pos, float minP, float maxP, juce::Slider::SliderStyle, juce::Slider&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override { return juce::FontOptions(13.0f, juce::Font::bold); }
    juce::Font getTextButtonFont(juce::TextButton&, int h) override { return juce::FontOptions(juce::jmin(13.5f, h * 0.48f), juce::Font::bold); }
};

// A knob with its name under it, attached to one parameter.
struct Knob : juce::Component {
    Knob(const juce::String& name, bool big = false);
    void attach(juce::AudioProcessorValueTreeState& s, const juce::String& id);
    void resized() override;
    void paintOverChildren(juce::Graphics&) override;
    std::unique_ptr<juce::Slider> slider;
    juce::Label label;
    juce::StringArray ticks; std::vector<double> tickValues;   // optional scale marks on the dial (e.g. the vowels a-e-i-o-u)
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    bool big;
};

class GhostField;
} // namespace gsui

class PoltergeistEditor : public juce::AudioProcessorEditor, public juce::FileDragAndDropTarget, private juce::Timer {
public:
    explicit PoltergeistEditor(PoltergeistProcessor&);
    ~PoltergeistEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void selectGhost(int op);
    void doSummon(const juce::String& words);
    void refreshPresetName();
    void loadSample(const juce::File& f);   // ADD SAMPLE from a file (also a file dropped on the plugin)
    void sampleAdded();                     // after proc.addSample*: show the SAMPLE ghost's panel + say what happened
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int, int) override;
    void testShow(int page, int a, int b);   // test harness: show a page (the old sample pages 1 / 5 / 6 show GHOSTS)
    void testPresetMenu(const juce::String& query);   // test harness: open the preset list (search results)
    void testBypassDark(bool dark);
    void testTheme(ghosty::themes::Theme t);          // test harness: switch the theme
    juce::Button* testThemeButton();
    void testEnvMode(int ghost, bool filter);
    ghosty::SnapshotBar* testSnapBar();
    int testSnapshotMenuItems(const juce::String& query);   // the SNAPSHOTS bank in the preset menu: how many snapshot entries
    void testPresetMenuResult(int r);
    bool testKey(const juce::KeyPress& k);
    juce::String testPresetName();
    void testPresetStep(int dir);
    juce::Slider* testKnobSlider(const juce::String& paramId);   // test harness: a knob's slider by parameter id
    std::vector<juce::Component*> testPanels();       // test harness: the panels that must be opaque in the light theme
    std::vector<juce::String> testTabs();             // test harness: the page tabs, in order
    // ADD SAMPLE (test harness): the ghost field, a ghost's centre (7 = SAMPLE), a link's dot, the slot's waveform / ROOT / buttons / parts
    juce::Component* testField();
    juce::Point<float> testGhostPos(int g);
    juce::Point<float> testLinkHandle(int from, int to);
    ghosty::WaveRegion* testSampleWave();
    juce::ComboBox* testSampleRoot();
    juce::Button* testSampleButton(bool clear);
    std::vector<juce::Component*> testSampleComps();
    void testRefresh();

private:
    void timerCallback() override;
    PoltergeistProcessor& proc;
    gsui::LookAndFeel lnf;

    struct Content;
    std::unique_ptr<Content> content;
    ghosty::TypeIn typeIn { *this };   // right-click any knob to type a value
    juce::TooltipWindow tooltipWindow { this, 700 };   // the ONE tooltip window of this editor (theme colours from the look-and-feel)
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PoltergeistEditor)
};
