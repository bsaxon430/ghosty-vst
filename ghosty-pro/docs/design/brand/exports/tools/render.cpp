// Renders the GHOST KIT + Ghosty logo PNGs (icons, splash / hero, og card, contact sheet) from the vector art headers.
#include <juce_gui_basics/juce_gui_basics.h>
#include "GhostKitArt.h"
#include "GhostyLogo.h"
#include "GhostyProLogo.h"

static juce::File out ("/root/work/logos");
static void save (const juce::Image& img, const juce::String& name)
{
    auto f = out.getChildFile (name); f.deleteFile();
    juce::PNGImageFormat png; if (auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream())) png.writeImageToStream (img, *os);
    std::printf ("  %s\n", f.getFullPathName().toRawUTF8());
}
template <typename F> static juce::Image draw (int w, int h, F&& fn, bool clear = true)
{
    juce::Image img (juce::Image::ARGB, w, h, clear, juce::SoftwareImageType());
    juce::Graphics g (img); fn (g); return img;
}
namespace th = ghosty::themes;

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::String mode = argc > 1 ? argv[1] : "all";
    const float phase = argc > 2 ? juce::String (argv[2]).getFloatValue() : 1.3f;
    out.createDirectory();
    const int sizes[] { 16, 32, 64, 128, 256, 512, 1024 };
    if (mode == "all" || mode == "kit")
    {
        for (int s : sizes) save (draw (s, s, [&] (juce::Graphics& g) { ghostkit::drawIcon (g, { 0.0f, 0.0f, (float) s, (float) s }); }), "ghost-kit-icon-" + juce::String (s) + ".png");
        save (draw (1600, 900, [&] (juce::Graphics& g) { ghostkit::drawSplash (g, { 0.0f, 0.0f, 1600.0f, 900.0f }, th::Theme::Dark, phase, true); }), "ghost-kit-splash.png");
        save (draw (1600, 900, [&] (juce::Graphics& g) { ghostkit::drawSplash (g, { 0.0f, 0.0f, 1600.0f, 900.0f }, th::Theme::Purple, phase, true); }), "ghost-kit-splash-purple.png");
        save (draw (1600, 900, [&] (juce::Graphics& g) { ghostkit::drawSplash (g, { 0.0f, 0.0f, 1600.0f, 900.0f }, th::Theme::Light, phase, true); }), "ghost-kit-splash-light.png");
    }
    if (mode == "all" || mode == "ghosty")
    {
        for (int s : sizes) save (draw (s, s, [&] (juce::Graphics& g) { ghosty::logo::drawIcon (g, { 0.0f, 0.0f, (float) s, (float) s }); }), "ghosty-icon-" + juce::String (s) + ".png");
        save (draw (1600, 900, [&] (juce::Graphics& g) { ghosty::logo::drawHero (g, { 0.0f, 0.0f, 1600.0f, 900.0f }, phase); }), "ghosty-hero.png");
        save (draw (1200, 630, [&] (juce::Graphics& g) { ghosty::logo::drawSocial (g, { 0.0f, 0.0f, 1200.0f, 630.0f }); }), "ghosty-og.png");
    }
    if (mode == "all" || mode == "pro")
    {
        for (int s : sizes) save (draw (s, s, [&] (juce::Graphics& g) { ghosty::prologo::drawIcon (g, { 0.0f, 0.0f, (float) s, (float) s }); }), "ghosty-pro-icon-" + juce::String (s) + ".png");
        save (draw (1600, 900, [&] (juce::Graphics& g) { ghosty::prologo::drawHero (g, { 0.0f, 0.0f, 1600.0f, 900.0f }, phase); }), "ghosty-pro-hero.png");
        const juce::Colour bgs[] { juce::Colour (0xff0B0C10), juce::Colour (0xff2A1650), juce::Colour (0xffF4F1FA) };
        save (draw (1500, 900, [&] (juce::Graphics& g)
        {
            for (int i = 0; i < 3; ++i)
            {
                const float x0 = 500.0f * (float) i; g.setColour (bgs[i]); g.fillRect (x0, 0.0f, 500.0f, 900.0f);
                ghosty::prologo::drawMascot (g, { x0 + 150, 20, 200, 200 }, bgs[i], 1.0f);
                ghosty::prologo::drawMascot (g, { x0 + 20, 240, 120, 120 }, bgs[i], 1.0f);
                ghosty::prologo::drawMascot (g, { x0 + 160, 250, 48, 48 }, bgs[i], 1.0f);
                ghosty::prologo::drawMascot (g, { x0 + 230, 250, 32, 32 }, bgs[i], 1.0f);
                ghosty::prologo::drawMascot (g, { x0 + 20, 380, 200, 450 }, bgs[i], 1.0f);
                ghosty::prologo::drawWordmark (g, { x0 + 20, 700, 460, 150 }, bgs[i]);
            }
        }), "ghosty-pro-contact.png");
    }
    if (mode == "all" || mode == "sheet")
    {
        const th::Theme themes[] { th::Theme::Dark, th::Theme::Purple, th::Theme::Light };
        const char* names[] { "BLACK", "PURPLE", "LIGHT" };
        const int PW = 780, PH = 1560;
        save (draw (PW * 3, PH, [&] (juce::Graphics& g)
        {
            for (int i = 0; i < 3; ++i)
            {
                const auto& P = th::get (themes[i]);
                const auto bg = P.window; const float x0 = (float) (i * PW);
                g.setColour (bg); g.fillRect (x0, 0.0f, (float) PW, (float) PH);
                g.setColour (P.ink); g.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
                g.drawText (juce::String (names[i]) + "   kit outline " + juce::String (th::contrast (ghostkit::outlineColour (themes[i]), bg), 1) + ":1   ghosty outline "
                            + juce::String (ghosty::logo::contrast (ghosty::logo::outlineColour (bg), bg), 1) + ":1", juce::Rectangle<float> (x0 + 20, 10, PW - 40, 30), juce::Justification::centredLeft);
                for (int row = 0; row < 2; ++row)
                {
                    const float y0 = 60.0f + (float) row * 560.0f; float x = x0 + 20.0f;
                    for (int s : { 32, 120, 512 })
                    {
                        const auto r = juce::Rectangle<float> (x, y0 + (512.0f - (float) s) * 0.5f, (float) s, (float) s);
                        if (row == 0) ghostkit::drawMascot (g, r, themes[i], phase); else ghosty::logo::drawMascot (g, r, bg, phase);
                        g.setColour (P.dim); g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::plain)));
                        g.drawText (juce::String (s) + " px", juce::Rectangle<float> (x, y0 + 520.0f, (float) juce::jmax (s, 60), 18.0f), juce::Justification::centredLeft);
                        x += (float) s + 30.0f;
                    }
                }
                ghostkit::drawWordmark (g, juce::Rectangle<float> (x0 + 30, 1190, PW - 60, 110), themes[i]);
                ghostkit::drawWordmark (g, juce::Rectangle<float> (x0 + 30, 1320, 300, 40), themes[i]);
                ghosty::logo::drawWordmark (g, juce::Rectangle<float> (x0 + 360, 1310, 220, 60), bg);
                ghosty::logo::drawWordmark (g, juce::Rectangle<float> (x0 + 30, 1390, PW - 60, 150), bg);
            }
        }), "CONTACT.png");
    }
    if (mode == "anim")   // frames of the idle loop (for checking the animation)
        for (int k = 0; k < 8; ++k)
            save (draw (520, 260, [&] (juce::Graphics& g) { g.fillAll (th::get (th::Theme::Dark).window);
                ghostkit::drawMascot (g, { 0, 0, 260, 260 }, th::Theme::Dark, (float) k * 0.5f); ghosty::logo::drawMascot (g, { 260, 0, 260, 260 }, juce::Colour (0xff07040d), (float) k * 0.5f); }),
                  "anim/frame-" + juce::String (k) + ".png");
    return 0;
}
