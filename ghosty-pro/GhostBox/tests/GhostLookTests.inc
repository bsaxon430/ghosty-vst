// GhostBoxTests: the GHOST look (liquid glass on the LIGHT base, Ghost Box only). Group: ghostui (mode "ghostui [shots dir]", also in the full run)
//   the look button cycles BLACK -> PURPLE -> LIGHT -> GHOST -> BLACK, the look persists in the session ("ghost"; old sessions unchanged);
//   BLACK / PURPLE / LIGHT render the same before and after visiting GHOST; GHOST text contrast measured on the rendered pixels (the worst
//   backdrop point under each text, the text composited with its alpha); tooltips kept; the cached layers; full-repaint time GHOST vs LIGHT;
//   GHOST screenshots of every tab (+ unit pages) at the default size.

// the worst contrast of `text` (composited with its alpha) against every pixel of `area` in img
static double gWorstIn (const juce::Image& img, juce::Rectangle<int> area, juce::Colour text)
{
    area = area.getIntersection (img.getBounds());
    double worst = 99.0;
    const juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x)
        {
            const auto bg = bd.getPixelColour (x, y).withAlpha (1.0f);
            worst = std::min (worst, ghosty::themes::contrast (bg.overlaidWith (text), bg));
        }
    return worst;
}
static bool gImagesEqual (const juce::Image& a, const juce::Image& b)
{
    if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight()) return false;
    const juce::Image::BitmapData A (a, juce::Image::BitmapData::readOnly), B (b, juce::Image::BitmapData::readOnly);
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (A.getPixelColour (x, y) != B.getPixelColour (x, y)) return false;
    return true;
}
static void gCollect (juce::Component& c, std::vector<juce::Component*>& out)
{
    for (auto* k : c.getChildren())
    {
        if (! k->isVisible()) continue;
        out.push_back (k);
        gCollect (*k, out);
    }
}
// a text button's label box over exactly what is under it on screen (the button hidden, then its own glass drawn without the text)
static double gButtonContrast (juce::Component& top, juce::TextButton& b, juce::Colour text)
{
    const auto R = top.getLocalArea (b.getParentComponent(), b.getBounds());
    b.setVisible (false);
    auto img = top.createComponentSnapshot (R, true, 1.0f);
    b.setVisible (true);
    fxtheme::setLook (BoxEditor::kLookGhost);
    { juce::Graphics g (img); b.getLookAndFeel().drawButtonBackground (g, b, b.findColour (juce::TextButton::buttonColourId), false, false); }
    const auto f = ghosty::font (juce::jmin (12.0f, (float) b.getHeight() * 0.5f));
    const auto body = ghosty::glass::pillBody (img.getBounds().toFloat());
    const float tw = juce::jmin (body.getWidth() - 8.0f, ghosty::textWidth (f, b.getButtonText()));
    const auto tr = juce::Rectangle<float> (tw, f.getAscent() * 0.8f).withCentre (body.getCentre()).toNearestInt();
    return gWorstIn (img, tr, text);
}

static void testGhostUi (const juce::File& shots)
{
    std::printf ("\n[24] GHOST look: 4-look cycle, persistence, old looks unchanged, glass contrast on pixels, tooltips, caches, repaint cost, screenshots\n");
    using namespace ghosty::themes;
    {   // tokens: the GHOST palette (Ghost Box only) text >= 4.5:1 on every surface, the mapped accents >= 4.5:1 on the cells; the shared themes untouched
        const auto& G = fxtheme::ghostPalette();
        double text = 99, acc = 99;
        for (auto bg : { G.window, G.panel, G.card, G.cell, G.waveBg }) for (auto fg : { G.ink, G.dim }) text = std::min (text, contrast (fg, bg));
        text = std::min (text, contrast (juce::Colours::white, ghosty::glass::violetDeep()));
        fxtheme::setLook (BoxEditor::kLookGhost);
        for (auto c : { fxtheme::col::slime, fxtheme::col::ecto, fxtheme::col::pink, fxtheme::col::ice, fxtheme::col::gold }) acc = std::min (acc, contrast (c.get(), G.cell));
        const bool ghostOn = fxtheme::ghost() && fxtheme::theme() == Theme::Light;
        fxtheme::set (Theme::Light);
        const bool lightIsLight = ! fxtheme::ghost() && &fxtheme::pal() == &get (Theme::Light) && get (Theme::Light).window != G.window && ! glassLight();
        fxtheme::set (Theme::Dark);
        std::printf ("    tokens: text %.2f:1, accents on cells %.2f:1\n", text, acc);
        check (ghostOn && lightIsLight && text >= 4.5 && acc >= 4.5, "GHOST palette: text >= 4.5:1 (" + juce::String (text, 2) + "), accents >= 4.5:1 (" + juce::String (acc, 2) + "); LIGHT and the shared themes unchanged");
    }

    // ---- the cycle + persistence
    Proc p (true); p.scene ("radiate");
    std::unique_ptr<juce::AudioProcessorEditor> edp (p->createEditor());
    auto* ed = dynamic_cast<BoxEditor*> (edp.get());
    ed->setSize (1180, 780 + BoxEditor::kSceneBar);
    Panel* panel = nullptr; for (auto* c : ed->getChildren()) if (auto* pp = dynamic_cast<Panel*> (c)) panel = pp;
    ed->setTheme (Theme::Dark);
    {
        juce::StringArray seq; juce::String names;
        for (int i = 0; i < 4; ++i) { names << ed->lookButton().getButtonText() << " "; ed->lookButton().onClick(); seq.add (juce::String (ed->look()) + "/" + juce::String (p->uiTheme.load())); }
        std::printf ("    cycle %s, labels %s\n", seq.joinIntoString (" ").toRawUTF8(), names.trim().toRawUTF8());
        check (seq.joinIntoString (" ") == "1/1 2/2 3/3 0/0" && names.trim() == "PURPLE LIGHT GHOST BLACK",
               "the look button cycles BLACK -> PURPLE -> LIGHT -> GHOST -> BLACK and names the next look (" + seq.joinIntoString (" ") + ")");
        ed->setLook (BoxEditor::kLookGhost);
        check (ed->lookButton().getTooltip().contains ("GHOST") && ed->lookButton().getTooltip().contains ("LIGHT") && ed->isGhost() && ed->theme() == Theme::Light,
               "the look button's tooltip lists the four looks; GHOST reads as LIGHT for every light() branch");
    }
    {
        auto roundTrip = [] (int t) { Proc a (true); a->uiTheme = t; juce::MemoryBlock mb; a->getStateInformation (mb); Proc q (true); q->uiTheme = 1; q->setStateInformation (mb.getData(), (int) mb.getSize()); return q->uiTheme.load(); };
        auto named = [] (const juce::String& v) { Proc a (true); auto t = a->stateTree(); t.setProperty ("theme_name", v, nullptr);
                                                  juce::MemoryBlock mb; if (auto x = t.createXml()) a->copyXmlToBinary (*x, mb); Proc q (true); q->uiTheme = 1; q->setStateInformation (mb.getData(), (int) mb.getSize()); return q->uiTheme.load(); };
        Proc a (true); a->uiTheme = BoxEditor::kLookGhost; const auto saved = a->stateTree().getProperty ("theme_name").toString();
        const bool rt = roundTrip (3) == 3 && roundTrip (0) == 0 && roundTrip (1) == 1 && roundTrip (2) == 2;
        const bool old = named ("dark") == 0 && named ("purple") == 1 && named ("light") == 2 && named ("zzz") == 0 && named ("ghost") == 3;
        const bool oldBuildReadsGhost = fromString ("ghost") == Theme::Dark;   // what a build without GHOST does with "ghost": unknown -> BLACK (as today)
        std::unique_ptr<juce::AudioProcessorEditor> e2 (a->createEditor());
        const bool reopens = dynamic_cast<BoxEditor*> (e2.get())->isGhost();
        e2.reset();
        std::printf ("    saved as \"%s\", round trip %d, old names %d, old build reads it as BLACK %d, reopens as GHOST %d\n", saved.toRawUTF8(), (int) rt, (int) old, (int) oldBuildReadsGhost, (int) reopens);
        check (saved == "ghost" && rt && old && oldBuildReadsGhost && reopens, "GHOST is saved with the session (theme_name \"ghost\") and reopens as GHOST; old sessions open unchanged; unknown -> BLACK");
    }

    // ---- the old looks render the same before and after visiting GHOST (rack page, BOARD, MORPH)
    {
        std::vector<juce::Image> before, after;
        auto grab = [&] (std::vector<juce::Image>& out)
        {
            for (int m : { 0, 5, 1 })
                for (auto t : { Theme::Dark, Theme::Purple, Theme::Light })
                {
                    ed->setTheme (t); panel->getProperties().set ("mode", m); panel->resized();
                    out.push_back (ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f));
                }
            panel->getProperties().set ("mode", 0); panel->resized();
        };
        grab (before);
        ed->setLook (BoxEditor::kLookGhost); for (int m : { 0, 5, 1, 2, 3, 4, 0 }) { panel->getProperties().set ("mode", m); panel->resized(); (void) ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f); }
        grab (after);
        int same = 0; for (size_t i = 0; i < before.size(); ++i) { const bool eq = gImagesEqual (before[i], after[i]); same += eq ? 1 : 0; if (! eq) std::printf ("    differs: shot %d\n", (int) i); }
        check (same == (int) before.size(), "BLACK / PURPLE / LIGHT (RACK, BOARD, MORPH) are pixel-identical before and after GHOST (" + juce::String (same) + "/" + juce::String ((int) before.size()) + ")");
    }

    // ---- the cached layers: the aurora + glass panels are built once per size, not on repaint; a resize rebuilds the aurora once
    {
        ed->setLook (BoxEditor::kLookGhost);
        (void) ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        const int b0 = ed->backdrop.builds, h0 = ed->headerGlass.builds, s0 = ed->ghostStaticBuilds;
        for (int i = 0; i < 3; ++i) (void) ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        const int b1 = ed->backdrop.builds, h1 = ed->headerGlass.builds, s1 = ed->ghostStaticBuilds;
        ed->setSize (1300, 820); (void) ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        const int b2 = ed->backdrop.builds;
        ed->setSize (1180, 780 + BoxEditor::kSceneBar); (void) ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        std::printf ("    aurora builds %d -> %d (3 repaints) -> %d (resize); header glass %d -> %d\n", b0, b1, b2, h0, h1);
        check (b1 == b0 && h1 == h0 && s1 == s0 && b2 == b0 + 1, "GHOST paints from cached images (aurora + static glass layer): no rebuild on repaint, one aurora rebuild on resize");
    }

    // ---- full-repaint time GHOST vs LIGHT (warm caches): within 1.5x on the rack page, BOARD and PERFORM
    {
        auto cost = [&] (int look, int m)
        {
            ed->setLook (look); panel->getProperties().set ("mode", m); panel->resized();
            for (int i = 0; i < 3; ++i) (void) ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            double best = 1e9;
            for (int r = 0; r < 3; ++r)
            {
                const double t0 = juce::Time::getMillisecondCounterHiRes();
                for (int i = 0; i < 8; ++i) (void) ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
                best = std::min (best, (juce::Time::getMillisecondCounterHiRes() - t0) / 8.0);
            }
            return best;
        };
        double worst = 0; juce::String txt;
        for (int m : { 0, 5, 4 })
        {
            const double l = cost (2, m), gh = cost (BoxEditor::kLookGhost, m);
            worst = std::max (worst, gh / l); txt << juce::String (m) << ": LIGHT " << juce::String (l, 1) << " ms GHOST " << juce::String (gh, 1) << " ms  ";
        }
        panel->getProperties().set ("mode", 0); panel->resized();
        std::printf ("    full repaint %s(worst %.2fx)\n", txt.toRawUTF8(), worst);
        check (worst <= 1.5, "GHOST full repaint within 1.5x of LIGHT (worst " + juce::String (worst, 2) + "x)");
    }

    // ---- GHOST contrast, measured on the rendered pixels
    ed->setLook (BoxEditor::kLookGhost);
    {   // header + panel texts over the glass surfaces (painted without the texts)
        juce::Image surf (juce::Image::ARGB, ed->getWidth(), ed->getHeight(), true);
        {
            juce::Graphics g (surf); ed->paintGhostSurfaces (g);
            juce::Graphics::ScopedSaveState ss (g); g.setOrigin (panel->getPosition()); panel->paintGhostSurfaces (g);
        }
        double worst = 99; juce::String worstName;
        for (auto& t : ed->ghostHeaderTexts()) { const double c = gWorstIn (surf, t.r, t.c); if (c < worst) { worst = c; worstName = t.what; } }
        for (int m = 0; m < 6; ++m)
        {
            panel->getProperties().set ("mode", m); panel->resized();
            int k = 0; for (auto& [r, c] : panel->ghostTexts()) { const double x = gWorstIn (surf, r + panel->getPosition(), c); if (x < worst) { worst = x; worstName = "panel text " + juce::String (k) + " (mode " + juce::String (m) + ")"; } ++k; }
        }
        panel->getProperties().set ("mode", 0); panel->resized();
        // the page card (inside its rim): ink / dim / the mapped accents used for captions
        juce::Component* strip = nullptr; for (auto* c : panel->getChildren()) if (c->getWidth() > panel->getWidth() - 40 && c->getHeight() > 100 && c->getHeight() < 220) strip = c;
        const int cardTop = panel->getY() + (strip != nullptr ? strip->getBottom() : 220) + 24;
        const auto card = juce::Rectangle<int> (30, cardTop, ed->getWidth() - 60, panel->getBottom() - 90 - cardTop);
        double inCard = 99;
        for (auto c : { fxtheme::col::white.get(), fxtheme::col::dim.get(), fxtheme::col::slime.get(), fxtheme::col::ecto.get(), fxtheme::col::pink.get(), fxtheme::col::ice.get(), fxtheme::col::gold.get() })
        {   const double x = gWorstIn (surf, card, c); inCard = std::min (inCard, x); if (std::getenv ("GHOST_DEBUG")) std::printf ("      card %08x %.2f\n", (unsigned) c.getARGB(), x); }
        std::printf ("    header + panel texts worst %.2f:1 (%s); every text token over the page card's worst pixel %.2f:1\n", worst, worstName.toRawUTF8(), inCard);
        check (worst >= 4.5 && inCard >= 4.5, "GHOST header / panel texts >= 4.5:1 at the worst pixel (" + juce::String (worst, 2) + " " + worstName + "), page card " + juce::String (inCard, 2));
        // the unit strip's legend line sits straight on the aurora
        const auto legend = strip != nullptr ? ed->getLocalArea (panel, strip->getBounds().withHeight (13)) : juce::Rectangle<int>();
        const double leg = gWorstIn (surf, legend, ghosty::glass::ink());
        std::printf ("    strip legend on the aurora %.2f:1\n", leg);
        check (strip != nullptr && leg >= 4.5, "GHOST strip legend / hint line on the aurora >= 4.5:1 (" + juce::String (leg, 2) + ")");
    }
    {   // the effect tiles: names over the glass tile on the darkest and the lightest aurora point (active ink >= 4.5:1, greyed >= 3:1)
        const auto& aur = ed->backdrop.get (ed->getWidth(), ed->auroraHeight(), 1.0f);
        juce::Colour dark = juce::Colours::white, light = juce::Colours::black;
        { const juce::Image::BitmapData bd (aur, juce::Image::BitmapData::readOnly);
          for (int y = 0; y < aur.getHeight(); y += 2) for (int x = 0; x < aur.getWidth(); x += 2) { const auto c = bd.getPixelColour (x, y); if (relLum (c) < relLum (dark)) dark = c; if (relLum (c) > relLum (light)) light = c; } }
        double on = 99, off = 99;
        for (auto bg : { dark, light })
            for (float body : { 0.36f, 0.50f })
            {
                juce::Image img (juce::Image::ARGB, 120, 90, true); juce::Graphics g (img); g.fillAll (bg);
                ghosty::glass::PanelCache pc; pc.draw (g, { 20, 10, 80, 70 }, 9.0f, body, 4.0f);
                const juce::Rectangle<int> name (34, 58, 52, 10);
                on = std::min (on, gWorstIn (img, name, fxtheme::col::white.get()));
                const auto offInk = fxtheme::col::dim.get(), veil = fxtheme::T (juce::Colour (0xff0b0c10)).withAlpha (0.25f);
                const juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
                for (int y = name.getY(); y < name.getBottom(); ++y) for (int x = name.getX(); x < name.getRight(); ++x)
                {   // a greyed tile: the name, then the veil over name and glass
                    const auto px = bd.getPixelColour (x, y).withAlpha (1.0f);
                    off = std::min (off, contrast (px.overlaidWith (offInk).overlaidWith (veil), px.overlaidWith (veil)));
                }
            }
        std::printf ("    tiles: active names %.2f:1, greyed names %.2f:1\n", on, off);
        check (on >= 4.5 && off >= 3.0, "GHOST tiles over the darkest / lightest aurora: active names >= 4.5:1 (" + juce::String (on, 2) + "), greyed >= 3:1 (" + juce::String (off, 2) + ")");
    }
    {   // every visible text button (header, rack, every tab, every unit page): its text over its own glass; value boxes + combo texts over what is under them
        double offB = 99, onB = 99, labels = 99; int nb = 0, nl = 0; juce::String worstB, worstL;
        auto scan = [&] (const juce::String& view)
        {
            std::vector<juce::Component*> all; gCollect (*ed, all);
            for (auto* c : all)
            {
                                if (auto* b = dynamic_cast<juce::TextButton*> (c); b != nullptr && b->isEnabled() && b->getButtonText().isNotEmpty() && b->getWidth() > 14 && b->getHeight() > 12
                                                                  && dynamic_cast<ghosty::UnlockButton*> (c) == nullptr)
                {
                    const bool lit = b->getToggleState();
                    const double x = gButtonContrast (*ed, *b, lit ? juce::Colours::white : ghosty::glass::ink()); ++nb;
                    double& w = lit ? onB : offB;
                    if (x < w) { w = x; if (x < 4.5) worstB = view + ": " + b->getButtonText(); }
                }
                else if (auto* l = dynamic_cast<juce::Label*> (c); view != "BROWSE" && l != nullptr && l->getText().isNotEmpty() && l->getWidth() > 10 && l->getHeight() > 8 && l->isEnabled())
                {
                    const auto R = ed->getLocalArea (l->getParentComponent(), l->getBounds());
                    l->setVisible (false); auto img = ed->createComponentSnapshot (R, true, 1.0f); l->setVisible (true);
                    const auto f = l->getFont(); const float tw = juce::jmin ((float) R.getWidth() - 4.0f, ghosty::textWidth (f, l->getText()));
                    const auto tr = juce::Rectangle<float> (tw, f.getAscent() * 0.7f).withCentre (img.getBounds().toFloat().getCentre()).toNearestInt();
                    const double x = gWorstIn (img, tr, l->findColour (juce::Label::textColourId)); ++nl;
                    if (x < labels) { labels = x; worstL = view + ": " + l->getText(); }
                }
            }
        };
        static const char* modeName[6] { "RACK", "MORPH", "MOTION", "BRAIN", "PERFORM", "BOARD" };
        for (int m = 1; m < 6; ++m) { panel->getProperties().set ("mode", m); panel->resized(); scan (modeName[m]); }
        panel->getProperties().set ("mode", 0); panel->resized();
        for (int u = 0; u < kNumUnits; ++u) { panel->getProperties().set ("select", u); panel->resized(); scan (unitInfo (u).shortName); }
        panel->getProperties().set ("browse", 1); panel->resized(); scan ("BROWSE"); panel->getProperties().set ("browse", 0); panel->resized();
        std::printf ("    buttons %d: off %.2f:1, lit %.2f:1 %s; labels %d: %.2f:1 (%s)\n", nb, offB, onB, worstB.toRawUTF8(), nl, labels, worstL.toRawUTF8());
        check (nb > 100 && offB >= 4.5 && onB >= 4.5, "GHOST buttons (" + juce::String (nb) + " over every tab + unit page): off >= 4.5:1 (" + juce::String (offB, 2) + "), lit >= 4.5:1 (" + juce::String (onB, 2) + ") " + worstB);
        check (nl > 50 && labels >= 4.5, "GHOST value boxes / combo texts (" + juce::String (nl) + "): >= 4.5:1 (" + juce::String (labels, 2) + " " + worstL + ")");
    }
    {   // tooltips + menus on the glass card; the highlighted menu item
        juce::Image tip (juce::Image::ARGB, 300, 40, true); { juce::Graphics g (tip); ed->getLookAndFeel().drawTooltip (g, "", 300, 40); }
        juce::Image menu (juce::Image::ARGB, 200, 200, true); { juce::Graphics g (menu); ed->getLookAndFeel().drawPopupMenuBackground (g, 200, 200); }
        const double t = std::min (gWorstIn (tip, { 10, 8, 280, 24 }, ghosty::glass::ink()), gWorstIn (menu, { 10, 10, 180, 180 }, ghosty::glass::ink()));
        auto& lf = ed->getLookAndFeel();
        const double hl = contrast (lf.findColour (juce::PopupMenu::highlightedTextColourId), lf.findColour (juce::PopupMenu::highlightedBackgroundColourId));
        check (t >= 4.5 && hl >= 4.5, "GHOST tooltips / menus: ink >= 4.5:1 (" + juce::String (t, 2) + "), highlighted item >= 4.5:1 (" + juce::String (hl, 2) + ")");
    }

    // ---- tooltips: every control keeps its tooltip in GHOST (the same set as BLACK; only the look button's text changes)
    {
        auto tips = [&] (int look)
        {
            ed->setLook (look); std::map<juce::String, int> m; int none = 0;
            auto add = [&] { std::vector<juce::Component*> all; gCollect (*ed, all);
                             for (auto* c : all) if (auto* tc = dynamic_cast<juce::SettableTooltipClient*> (c); tc != nullptr && c != &ed->lookButton()) { if (tc->getTooltip().isEmpty()) ++none; else m[tc->getTooltip()]++; } };
            for (int mo = 0; mo < 6; ++mo) { panel->getProperties().set ("mode", mo); panel->resized(); add(); }
            panel->getProperties().set ("mode", 0); panel->resized();
            for (int u = 0; u < kNumUnits; u += 3) { panel->getProperties().set ("select", u); panel->resized(); add(); }
            return std::make_pair (m, none);
        };
        const auto blk = tips (0), gh = tips (BoxEditor::kLookGhost);
        int n = 0; for (auto& kv : gh.first) n += kv.second;
        std::printf ("    tooltips: %d in GHOST, same as BLACK %d, controls without one %d (BLACK %d)\n", n, (int) (blk.first == gh.first), gh.second, blk.second);
        check (gh.first == blk.first && gh.second == blk.second && n > 100 && ! ed->lookButton().getTooltip().isEmpty(), "GHOST keeps every control's tooltip (" + juce::String (n) + ")");
    }

    // ---- GHOST screenshots: every tab + the browser + unit pages, at the default size
    if (shots != juce::File())
    {
        shots.createDirectory();
        ed->setSize (1180, 780 + BoxEditor::kSceneBar); ed->setLook (BoxEditor::kLookGhost);
        p.impl().boardAdd (0.25f, 0.3f); p.scene ("galaxyecho"); p.impl().boardAdd (0.75f, 0.35f); p.scene ("fatcrush"); p.impl().boardAdd (0.5f, 0.8f); p.scene ("radiate");
        p.impl().boardEnable (true); p.set ("box_board_x", 0.42f); p.set ("box_board_y", 0.5f); run (p, noise (0.2));
        int written = 0;
        auto shot = [&] (const juce::String& n) { snapTo (*ed, shots.getChildFile ("box-ghost-" + n + ".png")); ++written; };
        static const char* modes[] { "rack", "morph", "motion", "brain", "perform", "board" };
        panel->getProperties().set ("select", (int) uGate); panel->resized(); shot ("rack");
        for (int m = 1; m < 6; ++m) { panel->getProperties().set ("mode", m); panel->resized(); shot (modes[m]); }
        panel->getProperties().set ("mode", 0); panel->resized();
        panel->getProperties().set ("browse", 1); panel->resized(); shot ("browser"); panel->getProperties().set ("browse", 0); panel->resized();
        for (int u : { (int) uEq, (int) uSpace, (int) uSpectra, (int) uEcho, (int) uFatten, (int) uHalls, (int) uSplit, (int) uBassLab })
        { panel->getProperties().set ("select", u); panel->resized(); shot (juce::String ("unit-") + unitInfo (u).key); }
        p.set ("bypass", 1); shot ("bypassed"); p.set ("bypass", 0);
        check (written == 16, "GHOST screenshots written (" + juce::String (written) + ") to " + shots.getFullPathName());
    }
    ed->setLook (0);
    edp.reset();
    fxtheme::set (Theme::Dark);
}
