// GLASS (round 3): the theme-aware XY ghost pad (GhostyPro/ProGlass.cpp) - contrast of every ghost / label / trail on each backdrop,
// BLACK pixel-identical to the original pad, and close-up shots of the pad in the three looks.
//   ProTests glass [dir]     these checks + the shots (also run at the end of `ProTests theme`)
#include "ProGlass.h"

namespace {

static juce::Image snap (juce::Component& c) { return c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f); }
static bool writePng (const juce::Image& img, const juce::File& f)
{
    f.getParentDirectory().createDirectory(); f.deleteFile();
    if (auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream())) return juce::PNGImageFormat().writeImageToStream (img, *os);
    return false;
}
static int differingPixels (const juce::Image& a, const juce::Image& b)
{
    if (a.getBounds() != b.getBounds()) return -1;
    int n = 0; for (int y = 0; y < a.getHeight(); ++y) for (int x = 0; x < a.getWidth(); ++x) n += a.getPixelAt (x, y) != b.getPixelAt (x, y);
    return n;
}
template <typename T> static T* findChild (juce::Component& root)
{
    if (auto* t = dynamic_cast<T*> (&root)) return t;
    for (auto* c : root.getChildren()) if (auto* t = findChild<T> (*c)) return t;
    return nullptr;
}
// the colour a translucent colour shows on an opaque background
static juce::Colour over (juce::Colour bg, juce::Colour c) { return bg.overlaidWith (c); }

int glassPadTests (const juce::File& dir)
{
    namespace th = ghosty::themes;
    section ("GLASS (round 3): the XY ghost pad follows the theme - BLACK = the original black pad, PURPLE = deep purple, LIGHT = pale glass");
    static const char* themeName[] { "black", "purple", "light" };
    auto p = makeProc (progOf ("Trap Starter Stack"));
    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
    auto* pe = dynamic_cast<ProEditor*> (ed.get());
    pe->setSize (ProEditor::W, ProEditor::H); pe->setTheme (0); pe->showTab (0); pump (100);
    auto* pad = findChild<ghosty::XYGhostPad> (*ed);
    CHECK (pad != nullptr && (bool) pad->backdrop, "the SYNTH page's XY pad opts in to the themed backdrop");
    if (pad == nullptr) return 0;

    // 1) BLACK: no backdrop -> the original paint path; the pad paints the same pixels with the opt-in set as with it removed
    {
        CHECK (progl::padBackdrop (th::Theme::Dark) == nullptr, "BLACK has no backdrop (the original black pad)");
        auto keep = pad->backdrop;
        // (no message loop between the two snapshots: the pad's timer cannot run, both paints see the same state)
        auto a = snap (*pad); pad->backdrop = nullptr; auto b = snap (*pad); pad->backdrop = keep;
        const int d = differingPixels (a, b);
        const auto corner = a.getPixelAt (20, a.getHeight() / 2);
        CHECK (d == 0 && ghosty::themes::relLum (corner) < 0.02, "BLACK pad pixel-identical to the original pad (%d of %d pixels differ), backdrop #%s",
               d, a.getWidth() * a.getHeight(), corner.toDisplayString (false).toRawUTF8());
    }
    // 2) PURPLE / LIGHT: every ghost (all 35 looks), label, readout and trail readable on the backdrop (both ends of its gradient)
    const auto colours = pad->ghostColours();
    for (int t : { 1, 2 })
    {
        const auto* bd = progl::padBackdrop (th::fromInt (t));
        CHECK (bd != nullptr, "%s has a backdrop", themeName[t]);
        if (bd == nullptr) continue;
        const juce::Colour ends[] { bd->top, bd->bottom };
        double lab = 99, trail = 99, ghost = 99, grid = 99; juce::String worst;
        for (auto c : colours)
            for (auto e : ends)
            {
                const double l = contrastOf (bd->label (c), e); if (l < lab) { lab = l; worst = bd->label (c).toDisplayString (false); }
                trail = std::min (trail, contrastOf (bd->ink (c), e));
            }
        for (int look = 0; look < 35; ++look)
        {   // the edge a ghost shows: its outline (LIGHT), else its body; a dark-bodied ghost shows its slime rim
            const auto L = ghosty::lookFor (look);
            const bool darkBody = L.body.getBrightness() < 0.3f;
            for (auto e : ends)
            {
                double r = contrastOf (L.body, e);
                if (! bd->ghostOutline.isTransparent()) r = std::max (r, contrastOf (over (e, bd->ghostOutline), e));
                if (darkBody) r = std::max (r, contrastOf (L.slime, e));
                ghost = std::min (ghost, r);
            }
        }
        for (auto e : ends) grid = std::min (grid, contrastOf (over (e, bd->grid.withAlpha (bd->gridAlpha)), e));
        const double bgMid = contrastOf (bd->top, bd->bottom);
        CHECK (lab >= 4.5 && ghost >= 3.0 && trail >= 3.0 && bd->top.isOpaque() && bd->bottom.isOpaque(),
               "%-6s pad #%s -> #%s: ghost labels / readout %.2f:1 (lowest #%s), ghosts (35 looks) %.2f:1, trails + glow colours %.2f:1, grid %.2f:1 (soft), gradient %.2f:1",
               themeName[t], bd->top.toDisplayString (false).toRawUTF8(), bd->bottom.toDisplayString (false).toRawUTF8(), lab, worst.toRawUTF8(), ghost, trail, grid, bgMid);
    }
    {   // the round-3 ghost characters (ProGhostArt.h) on the PURPLE / LIGHT backdrops: rim and the name on the body >= 3:1
        CHECK ((bool) pad->ghostPainter && (bool) pad->backdrop, "the pad uses both opt-ins: the round-3 ghost art (ghostPainter) and the themed backdrop");
        using proghost::Kind;
        for (int t : { 1, 2 })
        {
            const auto* bd = progl::padBackdrop (th::fromInt (t));
            double rim = 99, name = 99; juce::String rw, nw;
            for (auto k : ghosttest::kinds)
                for (auto bg : { bd->top, bd->bottom })
                {
                    const double r = th::contrast (proghost::rimColour (k, proghost::isLightBackdrop (bg)), bg);
                    if (r < rim) { rim = r; rw = juce::String (ghosttest::kindName (k)) + " on #" + bg.toDisplayString (false); }
                    for (float R : { 20.0f, 32.0f })
                        for (float amt : { 0.05f, 0.95f })
                        {
                            const int sd = (int) std::ceil (R * 4.2f);
                            auto pose = ghosttest::poseFor ({ "x", false, amt }, bg); pose.title = false;
                            auto img = ghosttest::tile (k, R, pose, sd);
                            const auto tb = proghost::titleBounds (k, (float) sd * 0.5f, (float) sd * 0.5f, R).reduced (1.0f);
                            for (int gy = 0; gy < 3; ++gy)
                                for (int gx = 0; gx < 7; ++gx)
                                {
                                    const auto px = img.getPixelAt ((int) (tb.getX() + tb.getWidth() * ((float) gx + 0.5f) / 7.0f), (int) (tb.getY() + tb.getHeight() * ((float) gy + 0.5f) / 3.0f));
                                    const double c = th::contrast (proghost::titleColour (k), px);
                                    if (c < name) { name = c; nw = juce::String (ghosttest::kindName (k)) + " R " + juce::String (R, 0); }
                                }
                        }
                }
            CHECK (rim >= 3.0 && name >= 3.0, "%-6s pad, round-3 ghosts: rim %.2f:1 (lowest %s), name on the body %.2f:1 (lowest %s)", themeName[t], rim, rw.toRawUTF8(), name, nw.toRawUTF8());
        }
    }
    {   // the look of the backdrops: PURPLE in the theme's purple family (blue, red > green, dark), LIGHT pale (>= 85 % luminance-ish), grid lighter violet / grey-violet
        const auto* pu = progl::padBackdrop (th::Theme::Purple); const auto* li = progl::padBackdrop (th::Theme::Light);
        const bool purpleFamily = pu->top.getBlue() > pu->top.getGreen() && pu->top.getRed() > pu->top.getGreen() && th::relLum (pu->top) < 0.1 && pu->grid.getBrightness() > pu->top.getBrightness();
        const bool pale = th::relLum (li->bottom) > 0.65 && li->top.getRed() < 0xf8;
        CHECK (purpleFamily && pale, "PURPLE pad is deep purple with a lighter violet grid (#%s on #%s); LIGHT pad is pale glass (#%s, no pure white)",
               pu->grid.toDisplayString (false).toRawUTF8(), pu->top.toDisplayString (false).toRawUTF8(), li->top.toDisplayString (false).toRawUTF8());
    }
    // 3) shots: the whole SYNTH page and the pad close-up in each look, with trails (the ghosts moved) - and the rendered pixels
    for (int t = 0; t < 3; ++t)
    {
        pe->setTheme (t); pump (60);
        for (int k = 0; k <= 16; ++k)
        {   // move TONE and MORPH so their trails show
            p->setParam ("cutoff", 400.0f + 300.0f * (float) k); p->setParam ("o1_pos", (float) k / 20.0f); pump (30);
        }
        auto img = snap (*pad);
        if (t == 0) { const auto pb = pe->getLocalArea (pad, pad->getLocalBounds()); printf ("      pad bounds in the window: %d %d %d %d\n", pb.getX(), pb.getY(), pb.getWidth(), pb.getHeight()); }
        writePng (img, dir.getChildFile (juce::String ("pad-") + themeName[t] + ".png"));
        writePng (snap (*pe), dir.getChildFile (juce::String ("synth-") + themeName[t] + ".png"));
        // the backdrop pixel at the left edge of the grid area (no ghost there at these settings) - the theme's colour, not black
        const auto px = img.getPixelAt (8, img.getHeight() / 2);
        const double lum = th::relLum (px);
        CHECK (t == 0 ? lum < 0.01 : t == 1 ? (lum > 0.01 && lum < 0.08 && px.getBlue() > px.getGreen()) : lum > 0.6,
               "%-6s pad close-up written (%s): backdrop pixel #%s", themeName[t], dir.getChildFile (juce::String ("pad-") + themeName[t] + ".png").getFullPathName().toRawUTF8(), px.toDisplayString (false).toRawUTF8());
    }
    pe->setTheme (0);
    return 0;
}

// GLASS (round 3): the "liquid glass" LIGHT materials (GhostyPro/ProGlass.h): text contrast on every glass surface, BLACK / PURPLE untouched,
// the image cache, and the LIGHT shots (SYNTH, a MORE pop-down, the FX tab)
int glassLightTests (const juce::File& dir)
{
    namespace th = ghosty::themes;
    section ("GLASS (round 3): LIGHT = liquid glass (Ghosty Pro only) - text >= 4.5:1 on every glass surface, BLACK / PURPLE pixel-identical, cached materials");
    const auto& T = progl::tones();
    const auto L = pt::Palette::make (th::Theme::Light);
    const auto& S = pt::glassScreenPal();
    CHECK (th::glassLight() && th::get (th::Theme::Light).window == juce::Colour (0xffeceef2), "Ghosty Pro opts in: LIGHT window #%s", th::get (th::Theme::Light).window.toDisplayString (false).toRawUTF8());
    {   // every glass surface as painted (gradient ends, with the inner shadow / sheen / tint over them) vs every text colour that sits on it
        struct Surf { const char* name; juce::Colour c; };
        const auto acc = L.slime;
        const Surf cardS[] { { "card top", T.bodyTop }, { "card bottom + inner shadow", over (T.bodyBottom, T.innerShade.withAlpha (0.07f)) },
                             { "card top + accent tint", over (T.bodyTop, acc.withAlpha (0.08f)) }, { "card top + specular", over (T.bodyTop, T.highlight.withAlpha (0.42f)) },
                             { "pill top", T.knobTop }, { "pill bottom", T.knobBottom },
                             { "window top", th::get (th::Theme::Light).windowTop }, { "window bottom", th::get (th::Theme::Light).windowBottom } };
        double body = 99, sec = 99, accs = 99; juce::String worst;
        for (auto& sf : cardS)
        {
            body = std::min (body, contrastOf (L.white, sf.c)); sec = std::min (sec, contrastOf (L.dim, sf.c));
            for (auto a : { L.slime, L.ecto, L.pink, L.ice, L.gold, L.red, L.violet })
                if (contrastOf (a, sf.c) < accs) { accs = contrastOf (a, sf.c); worst = a.toDisplayString (false) + " on " + sf.name; }
        }
        for (auto w : { L.deep, over (L.deep, T.innerShade.withAlpha (0.06f)) }) { body = std::min (body, contrastOf (L.white, w)); sec = std::min (sec, contrastOf (L.dim, w)); }   // wells: ink / dim text only
        CHECK (body >= 7.0 && sec >= 4.5 && accs >= 4.5, "glass panels / cards / pills / wells: body text %.2f:1, secondary %.2f:1, accents as text (card titles, values) %.2f:1 (lowest %s)", body, sec, accs, worst.toRawUTF8());
        const juce::Colour scr[] { T.screenTop, over (T.screenTop, T.innerShade.withAlpha (0.07f)), over (T.screenTop, T.highlight.withAlpha (0.22f)), T.screenBottom, S.deep };
        double sb = 99, ss = 99, sa = 99; juce::String sw;
        for (auto c : scr)
        {
            sb = std::min (sb, contrastOf (S.white, c)); ss = std::min (ss, contrastOf (S.dim, c));
            for (auto a : { S.slime, S.ecto, S.pink, S.ice, S.gold, S.red, S.violet }) if (contrastOf (a, c) < sa) { sa = contrastOf (a, c); sw = a.toDisplayString (false) + " on #" + c.toDisplayString (false); }
        }
        CHECK (sb >= 7.0 && ss >= 4.5 && sa >= 4.5, "light frosted screens (wave, filter, env, scopes, FLOW): text %.2f:1, secondary %.2f:1, curves / labels (deepened glow colours) %.2f:1 (lowest %s)", sb, ss, sa, sw.toRawUTF8());
        // a lit glass pill: the label (onAccent) on the accent with its gloss, and on the darker bottom
        double lit = 99;
        for (auto a : { L.slime, L.pink, L.ice }) lit = std::min ({ lit, contrastOf (L.onAccent, over (a, T.highlight.withAlpha (0.10f))), contrastOf (L.onAccent, a.darker (0.22f)) });
        CHECK (lit >= 4.5, "lit glass pills (accent fill + gloss): label %.2f:1", lit);
        const double screenVsPanel = contrastOf (T.bodyTop, T.screenBottom);
        CHECK (screenVsPanel > 1.1 && th::relLum (T.screenBottom) < th::relLum (T.bodyBottom), "screens are a slightly darker inset than the panels (%.2f:1)", screenVsPanel);
    }
    {   // BLACK / PURPLE: the look-and-feel paints the same pixels with the glass opt-in on and off
        ProLnF lnf;
        juce::Slider sl (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox); sl.setRange (0, 1); sl.setValue (0.6); sl.setSize (60, 60);
        juce::TextButton tb ("ON"); tb.setSize (70, 24); juce::TextButton tbOn ("ON"); tbOn.setSize (70, 24); tbOn.setToggleState (true, juce::dontSendNotification);
        juce::ComboBox cb; cb.setSize (120, 24);
        auto paintAll = [&]
        {
            juce::Image img (juce::Image::ARGB, 400, 80, true); juce::Graphics g (img);
            lnf.drawRotarySlider (g, 0, 0, 60, 60, 0.6f, -2.4f, 2.4f, sl);
            g.setOrigin (70, 0); lnf.drawButtonBackground (g, tb, {}, false, false);
            g.setOrigin (80, 0); lnf.drawButtonBackground (g, tbOn, {}, false, false);
            g.setOrigin (80, 0); lnf.drawComboBox (g, 120, 24, false, 0, 0, 0, 0, cb);
            return img;
        };
        int diff = 0;
        for (auto t : { th::Theme::Dark, th::Theme::Purple })
        {
            pt::setTheme (t); lnf.applyTheme();
            th::optInGlassLight (true); auto a = paintAll();
            th::optInGlassLight (false); auto b = paintAll();
            th::optInGlassLight (true);
            diff += differingPixels (a, b);
        }
        pt::setTheme (th::Theme::Dark);
        CHECK (diff == 0, "BLACK / PURPLE knobs, buttons, combo boxes: %d pixels differ with the glass opt-in on vs off", diff);
    }
    {   // the cache: one image per size; a new size (resize) = a new entry; never grows past its cap
        auto& c = progl::cache(); c.images.clear();
        juce::Image img (juce::Image::ARGB, 300, 200, true); juce::Graphics g (img);
        progl::card (g, { 20, 20, 200, 120 }, 12, juce::Colours::red); progl::card (g, { 30, 30, 200, 120 }, 12, juce::Colours::blue);
        const size_t one = c.images.size();
        progl::card (g, { 20, 20, 210, 120 }, 12, juce::Colours::red);
        const size_t two = c.images.size();
        for (int w = 0; w < 300; ++w) progl::card (g, { 0, 0, 20.0f + (float) w, 20 }, 4, juce::Colours::transparentBlack, false);
        CHECK (one == 1 && two == 2 && c.images.size() <= 129, "material cache: same size reused (%d), resized = new entry (%d), capped (%d after 300 sizes)", (int) one, (int) two, (int) c.images.size());
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int k = 0; k < 200; ++k) progl::card (g, { 20, 20, 200, 120 }, 12, juce::Colours::red);
        const double ms = (juce::Time::getMillisecondCounterHiRes() - t0) / 200.0;
        CHECK (ms < 1.0, "a cached glass card paints in %.3f ms (no real-time blur)", ms);
    }
    // shots: LIGHT SYNTH page, a MORE pop-down, the FX tab
    auto p = makeProc (progOf ("Trap Starter Stack"));
    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
    auto* pe = dynamic_cast<ProEditor*> (ed.get());
    pe->setSize (ProEditor::W, ProEditor::H); pe->setTheme (2); pe->showTab (0); pump (300);
    writePng (snap (*pe), dir.getChildFile ("light-synth.png"));
    {
        juce::TextButton* more = nullptr;
        std::function<bool (juce::Component*)> shown = [&] (juce::Component* c) { for (; c != nullptr; c = c->getParentComponent()) if (! c->isVisible()) return false; return true; };
        std::function<void (juce::Component&)> findShown = [&] (juce::Component& c) { for (auto* k : c.getChildren()) { if (auto* b = dynamic_cast<juce::TextButton*> (k); b != nullptr && b->getButtonText() == "MORE" && shown (b) && more == nullptr) more = b; findShown (*k); } };
        ed->setVisible (true); pump (50); findShown (*ed);
        const auto f = dir.getChildFile ("light-popdown.png"); f.deleteFile();
        if (more != nullptr) { setenv ("GHOSTY_MORESHOT", f.getFullPathName().toRawUTF8(), 1); more->triggerClick(); pump (100); unsetenv ("GHOSTY_MORESHOT"); }
        CHECK (f.existsAsFile(), "LIGHT MORE pop-down shot: %s", f.getFullPathName().toRawUTF8());
    }
    pe->showTab (2); pump (400);
    writePng (snap (*pe), dir.getChildFile ("light-fx.png"));
    if (auto* panel = findChild<gpfx::Panel> (*ed))
    {   // the round-3 FX units' pages in the glass LIGHT
        int n = 0;
        for (int u : { (int) gpfx::uDist, (int) gpfx::uComp, (int) gpfx::uBands, (int) gpfx::uEq3 })
        {
            panel->getProperties().set ("select", u); panel->resized(); pump (300);
            const auto f = dir.getChildFile ("light-fx-" + juce::String (gpfx::unitInfo (u).shortName).toLowerCase().removeCharacters (" /") + ".png");
            n += writePng (snap (*pe), f) ? 1 : 0;
        }
        CHECK (n == 4, "LIGHT shots of the round-3 FX unit pages (DIST, COMP, BANDS, TONE EQ): %d written", n);
    }
    CHECK (dir.getChildFile ("light-fx.png").existsAsFile(), "LIGHT FX tab shot: %s", dir.getChildFile ("light-fx.png").getFullPathName().toRawUTF8());
    pe->setTheme (0);
    return 0;
}
} // namespace
