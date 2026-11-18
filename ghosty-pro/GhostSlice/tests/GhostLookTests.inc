// GhostSliceTests: the GHOST look (liquid glass on the LIGHT base, Slice-only). Group: ghostui
//   the look button cycles BLACK -> PURPLE -> LIGHT -> GHOST -> BLACK and the look persists in the session (old sessions unchanged);
//   BLACK / PURPLE / LIGHT render the same before and after visiting GHOST; GHOST text contrast measured on the rendered pixels
//   (the worst backdrop point under each text); tooltips; layout at both sizes; the aurora cache; GHOST screenshots at 1280x780 + 1600x1000.

// the worst contrast of `text` against every pixel of `area` in img
static double worstContrastIn (const juce::Image& img, juce::Rectangle<int> area, juce::Colour text)
{
    area = area.getIntersection (img.getBounds());
    double worst = 99.0;
    const juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
    for (int y = area.getY(); y < area.getBottom(); ++y)
        for (int x = area.getX(); x < area.getRight(); ++x)
            worst = std::min (worst, ghosty::themes::contrast (text, bd.getPixelColour (x, y).withAlpha (1.0f)));
    return worst;
}
static bool imagesEqual (const juce::Image& a, const juce::Image& b)
{
    if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight()) return false;
    const juce::Image::BitmapData A (a, juce::Image::BitmapData::readOnly), B (b, juce::Image::BitmapData::readOnly);
    for (int y = 0; y < a.getHeight(); ++y)
        for (int x = 0; x < a.getWidth(); ++x)
            if (A.getPixelColour (x, y) != B.getPixelColour (x, y)) return false;
    return true;
}
// a button's text box over exactly what is under it on screen (the button hidden, then its own glass drawn without the text)
static double glassButtonContrast (SliceEditor& ed, juce::Button& b, juce::Colour text)
{
    const auto R = ed.getLocalArea (b.getParentComponent(), b.getBounds());
    b.setVisible (false);
    auto img = ed.createComponentSnapshot (R, true, 1.0f);
    b.setVisible (true);
    {
        juce::Graphics g (img);
        if (auto* tb = dynamic_cast<juce::TextButton*> (&b)) ed.lnf.drawButtonBackground (g, *tb, {}, false, false);
        else ghosty::glass::pill (g, img.getBounds().toFloat(), b.getToggleState(), false, false, b.isEnabled());
    }
    const auto body = ghosty::glass::pillBody (img.getBounds().toFloat());
    const auto f = ed.lnf.getTextButtonFont (*static_cast<juce::TextButton*> (&b), b.getHeight());
    const float tw = juce::jmin (body.getWidth() - 8.0f, ghosty::textWidth (f, b.getButtonText()));
    const auto tr = juce::Rectangle<float> (tw, f.getAscent()).withCentre (body.getCentre()).toNearestInt();
    return worstContrastIn (img, tr, text);
}

static void testGhostUi (const juce::File& shots)
{
    section ("[24] GHOST look: 4-look cycle, persistence, old looks unchanged, glass contrast on pixels, tooltips, layout, screenshots");
    using namespace ghosty::themes;
    const auto G = gslice::ui::palette (Theme::Light, true);
    {   // tokens (as the old looks are checked): text >= 4.5:1, marks >= 3:1
        double worstText = 99, worstMark = 99;
        for (auto bg : { G.window, G.panel, G.header, G.card, G.cell, G.waveBg }) for (auto fg : { G.ink, G.dim }) worstText = std::min (worstText, contrast (fg, bg));
        worstText = std::min (worstText, contrast (G.onAccent, ghosty::glass::violetDeep()));
        for (auto fg : { G.wave, G.marker, G.pitch, G.warn, G.playhead, G.slime, G.draw, G.f0 }) worstMark = std::min (worstMark, contrast (fg, G.waveBg));
        check (G.glass && G.light && G.theme == Theme::Light && worstText >= 4.5 && worstMark >= 3.0,
               "GHOST tokens: text >= 4.5:1 (worst " + juce::String (worstText, 2) + "), marks >= 3:1 (worst " + juce::String (worstMark, 2) + ")");
        const auto L = gslice::ui::palette (Theme::Light);
        check (! L.glass && L.window == gslice::ui::palette (Theme::Light, false).window && L.ink != G.ink, "LIGHT stays LIGHT (GHOST is a separate palette instance, the shared themes untouched)");
        check (! ghosty::themes::glassLight(), "the shared opt-in glass LIGHT (Ghosty Pro's) stays off in Ghost Slice");
    }

    // ---- the cycle + persistence
    Rig rig (drums (48000, 4.0));
    auto q = ops::splitAt (rig->project(), rig.r0().id, 96000); rig->apply (*q, "cut");
    std::unique_ptr<SliceEditor> ed (static_cast<SliceEditor*> (rig->createEditor()));
    ed->setVisible (true); ed->setSize (1280, 780); ed->resized(); pump (20);
    ed->setTheme (Theme::Dark);
    juce::StringArray seq; juce::String names;
    for (int i = 0; i < 4; ++i) { names << ed->themeBtn.getButtonText() << " "; ed->themeBtn.onClick(); pump (2); seq.add (juce::String (ed->look()) + "/" + juce::String (rig->uiTheme.load())); }
    pump (300);   // (the clicked button's down flash ends)
    check (seq.joinIntoString (" ") == "1/1 2/2 3/3 0/0" && names.trim() == "PURPLE LIGHT GHOST BLACK",
           "the look button cycles BLACK -> PURPLE -> LIGHT -> GHOST -> BLACK (" + seq.joinIntoString (" ") + "; it shows the next look: " + names.trim() + ")");
    check (ed->themeBtn.getTooltip().contains ("GHOST") && ed->themeBtn.getTooltip().contains ("LIGHT"), "the look button's tooltip lists the four looks");
    bool helpLists = false; for (auto& l : HelpOverlay::lines()) helpLists = helpLists || (l.contains ("GHOST") && l.contains ("PURPLE"));
    check (helpLists, "the ? list names the four looks");
    ed->setLook (SliceEditor::kLookGhost); pump (5);
    check (ed->P.glass && ed->lnf.P.glass && ed->playPanel.P.glass && ed->theme() == Theme::Light && ed->syncBtn.glass, "GHOST reaches the editor, the look-and-feel, the views and the SYNC button");
    {
        juce::MemoryBlock mb; rig->getStateInformation (mb);
        SliceProcessor t; t.setPlayConfigDetails (0, 2, 48000, 512); t.prepareToPlay (48000, 512);
        t.setStateInformation (mb.getData(), (int) mb.getSize());
        std::unique_ptr<SliceEditor> e2 (static_cast<SliceEditor*> (t.createEditor())); e2->setVisible (true); pump (10);
        check (t.uiTheme.load() == 3 && e2->look() == SliceEditor::kLookGhost && e2->P.glass, "GHOST is saved with the session and reopens as GHOST");
        e2.reset(); pump (5);
        for (int old : { 0, 1, 2 })
        {   // a session saved before GHOST existed (theme 0 / 1 / 2) opens in the same look as before
            t.uiTheme = old; juce::MemoryBlock m2; t.getStateInformation (m2);
            SliceProcessor u; u.setPlayConfigDetails (0, 2, 48000, 512); u.prepareToPlay (48000, 512); u.setStateInformation (m2.getData(), (int) m2.getSize());
            std::unique_ptr<SliceEditor> e3 (static_cast<SliceEditor*> (u.createEditor())); e3->setVisible (true); pump (5);
            check (e3->look() == old && ! e3->P.glass && e3->theme() == fromInt (old), "an old session with look " + displayName (fromInt (old)) + " opens unchanged");
        }
        t.uiTheme = 9; std::unique_ptr<SliceEditor> e4 (static_cast<SliceEditor*> (t.createEditor())); e4->setVisible (true); pump (5);
        check (e4->look() == SliceEditor::kLookBlack && ! e4->P.glass, "an unknown stored look falls back to BLACK (as before)");
    }

    // ---- the old looks render the same before and after visiting GHOST (no state leaks into BLACK / PURPLE / LIGHT)
    {
        auto& v = ed->view; v.zoomToFit();
        std::vector<juce::Image> before, after;
        for (int ws : { 0, 2 })
            for (auto t : { Theme::Dark, Theme::Purple, Theme::Light }) { ed->setWorkspace ((gslice::ui::Workspace) ws); ed->setTheme (t); pump (5); before.push_back (ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f)); }
        ed->setLook (SliceEditor::kLookGhost); ed->setWorkspace (gslice::ui::Workspace::Play); pump (5); ed->setWorkspace (gslice::ui::Workspace::Edit); pump (5);
        for (int ws : { 0, 2 })
            for (auto t : { Theme::Dark, Theme::Purple, Theme::Light }) { ed->setWorkspace ((gslice::ui::Workspace) ws); ed->setTheme (t); pump (5); after.push_back (ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f)); }
        int same = 0; for (size_t i = 0; i < before.size(); ++i) { const bool eq = imagesEqual (before[i], after[i]); same += eq ? 1 : 0; if (! eq) { std::printf ("    differs: shot %d\n", (int) i); if (std::getenv ("GSLICE_GHOST_DEBUG")) { for (auto* im : { &before[i], &after[i] }) { const auto f = shots.getChildFile ("dbg-" + juce::String ((int) i) + (im == &before[i] ? "a" : "b") + ".png"); f.deleteFile(); juce::FileOutputStream os (f); juce::PNGImageFormat png; png.writeImageToStream (*im, os); } } } }
        check (same == (int) before.size(), "BLACK / PURPLE / LIGHT (EDIT + PITCH) are pixel-identical before and after GHOST (" + juce::String (same) + "/" + juce::String ((int) before.size()) + ")");
        ed->setWorkspace (gslice::ui::Workspace::Edit);
    }

    // ---- the aurora is rendered once per size
    {
        ed->setLook (SliceEditor::kLookGhost); pump (5);
        (void) ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        const int b0 = ed->backdrop.builds, p0 = ed->panels.builds;
        for (int i = 0; i < 3; ++i) (void) ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        const int b1 = ed->backdrop.builds, p1 = ed->panels.builds;
        ed->setSize (1400, 860); ed->resized(); pump (5); (void) ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        check (b1 == b0 && p1 == p0 && ed->backdrop.builds == b0 + 1, "GHOST paints from cached images: the aurora + panels are not rebuilt on repaint (aurora builds " + juce::String (b0) + " -> "
               + juce::String (b1) + "), a resize rebuilds the aurora once (" + juce::String (ed->backdrop.builds) + ")");
        ed->setSize (1280, 780); ed->resized(); pump (5);
    }

    // ---- GHOST contrast, measured on the rendered pixels (the worst backdrop point under each text)
    auto arig = analysedRig (stereoOf (voice (48000, fourNotes())), "MELODIC");
    std::unique_ptr<SliceEditor> pe (static_cast<SliceEditor*> ((*arig)->createEditor()));
    pe->setVisible (true);
    for (auto size : { juce::Point<int> (1280, 780), juce::Point<int> (1600, 1000) })
    {
        pe->setSize (size.x, size.y); pe->resized(); pe->setLook (SliceEditor::kLookGhost); pump (10);
        const juce::String sz = juce::String (size.x) + "x" + juce::String (size.y) + " ";
        // the surfaces without any text: header texts, readouts, the info strip
        juce::Image surf (juce::Image::ARGB, pe->getWidth(), pe->getHeight(), true);
        { juce::Graphics g (surf); pe->paintGlassSurfaces (g); }
        const auto P = pe->P;
        double header = 99, info = 99;
        header = std::min (header, worstContrastIn (surf, pe->logoArea.withTrimmedLeft (38), P.ink));
        header = std::min (header, worstContrastIn (surf, pe->logoArea.withTrimmedLeft (38), P.wave));
        for (auto* l : { &pe->bpmLbl, &pe->keyLbl, &pe->gridLbl })
            header = std::min (header, std::min (worstContrastIn (surf, l->getBounds().withHeight (SliceEditor::kReadoutH).withX (l->getX() - 32).withWidth (l->getWidth() + 30).reduced (0, 3), P.dim),
                                                 worstContrastIn (surf, l->getBounds().reduced (0, 3), P.ink)));
        const auto ia = pe->infoArea.reduced (SliceEditor::kGlassGutter + 10, 5);
        for (auto c : { P.ink, P.dim, P.marker, P.warn }) { info = std::min (info, worstContrastIn (surf, ia, c)); if (std::getenv ("GSLICE_GHOST_DEBUG")) std::printf ("    info %08x %.2f\n", (unsigned) c.getARGB(), worstContrastIn (surf, ia, c)); }
        if (std::getenv ("GSLICE_GHOST_DEBUG")) std::printf ("    logo ink %.2f wave %.2f\n", worstContrastIn (surf, pe->logoArea.withTrimmedLeft (38), P.ink), worstContrastIn (surf, pe->logoArea.withTrimmedLeft (38), P.wave));
        check (header >= 4.5, sz + "GHOST header: wordmark, readouts, captions >= 4.5:1 at the worst pixel (" + juce::String (header, 2) + ")");
        check (info >= 4.5, sz + "GHOST info strip: ink / dim / toast / amber >= 4.5:1 at the worst pixel (" + juce::String (info, 2) + ")");
        // tabs + every visible button: the text over its own glass (active = white on violet glass)
        double tabs = 99, buttons = 99, onBtn = 99; int nb = 0;
        for (int w = 0; w < 4; ++w)
        {
            pe->setWorkspace ((gslice::ui::Workspace) w); pump (5);
            for (auto& t : pe->tabs) tabs = std::min (tabs, glassButtonContrast (*pe, t, t.getToggleState() ? P.onAccent : P.ink));
            std::vector<juce::Component*> ctl; collectControls (*pe, ctl);
            for (auto* c : ctl)
                if (auto* b = dynamic_cast<juce::TextButton*> (c); b != nullptr && b->isEnabled() && b->getButtonText().isNotEmpty() && b->getWidth() > 12)
                {
                    const double x = glassButtonContrast (*pe, *b, b->getToggleState() ? P.onAccent : P.ink); ++nb;
                    (b->getToggleState() ? onBtn : buttons) = std::min (b->getToggleState() ? onBtn : buttons, x);
                    if (x < 4.5) std::printf ("    low: %s %.2f\n", b->getButtonText().toRawUTF8(), x);
                }
        }
        check (tabs >= 4.5, sz + "GHOST tabs (active white on violet glass, the rest indigo): >= 4.5:1 (" + juce::String (tabs, 2) + ")");
        check (buttons >= 4.5 && onBtn >= 4.5, sz + "GHOST buttons (" + juce::String (nb) + " in the four workspaces): off >= 4.5:1 (" + juce::String (buttons, 2) + "), on >= 4.5:1 (" + juce::String (onBtn, 2) + ")");
        // the value boxes (recessed glass) over what is under them
        double boxes = 99; int nv = 0;
        pe->setWorkspace (gslice::ui::Workspace::Pitch); pump (5);
        std::vector<juce::Component*> ctl; collectControls (*pe, ctl);
        for (auto* c : ctl)
            if (auto* s = dynamic_cast<juce::Slider*> (c))
                for (auto* ch : s->getChildren())
                    if (auto* lab = dynamic_cast<juce::Label*> (ch); lab != nullptr && lab->isVisible())
                    {
                        const auto R = pe->getLocalArea (s, lab->getBounds());
                        lab->setVisible (false); auto img = pe->createComponentSnapshot (R, true, 1.0f); lab->setVisible (true);
                        { juce::Graphics g (img); ghosty::glass::recessed (g, img.getBounds().toFloat().reduced (1.0f, 2.0f)); }
                        boxes = std::min (boxes, worstContrastIn (img, img.getBounds().reduced (12, 6), P.ink)); ++nv;
                    }
        check (nv > 4 && boxes >= 4.5, sz + "GHOST value boxes (" + juce::String (nv) + "): >= 4.5:1 (" + juce::String (boxes, 2) + ")");
    }
    {   // notes + help + tooltip + popup over the darkest and the lightest point of the aurora
        juce::Image aur (juce::Image::RGB, 1280, 780, false); { juce::Graphics g (aur); ghosty::glass::paintAurora (g, 1280, 780); }
        juce::Colour dark = juce::Colours::white, light = juce::Colours::black;
        { const juce::Image::BitmapData bd (aur, juce::Image::BitmapData::readOnly);
          for (int y = 0; y < 780; y += 2) for (int x = 0; x < 1280; x += 2) { const auto c = bd.getPixelColour (x, y); if (relLum (c) < relLum (dark)) dark = c; if (relLum (c) > relLum (light)) light = c; } }
        double notes = 99, help = 99, helpHead = 99;
        for (auto bg : { dark, light })
            for (int sel = 0; sel < 2; ++sel)
                for (float conf : { 0.0f, 1.0f })
                {   // the view's glass panel, the PITCH frost, then the capsule; the label is drawn in 0xff1b1740
                    juce::Image img (juce::Image::ARGB, 160, 60, true); juce::Graphics g (img);
                    g.fillAll (bg); ghosty::glass::body (g, img.getBounds().toFloat(), 16.0f, juce::Colours::white, 0.30f);
                    g.setColour (juce::Colours::white.withAlpha (0.45f)); g.fillAll();
                    ghosty::glass::capsule (g, { 20, 20, 120, 18 }, sel == 1, conf);
                    notes = std::min (notes, worstContrastIn (img, { 30, 23, 40, 12 }, juce::Colour (0xff1b1740)));
                }
        for (auto bg : { dark, light })
        {
            juce::Image img (juce::Image::ARGB, 400, 300, true); juce::Graphics g (img);
            g.fillAll (bg); g.setColour (juce::Colour (0xfff4f1fc).withAlpha (0.94f)); g.fillAll();
            ghosty::glass::panel (g, img.getBounds().reduced (12, 8).toFloat(), 22.0f, juce::Colours::white, 0.40f, false, 8.0f);
            const juce::Rectangle<int> rows[] { { 40, 30, 300, 20 }, { 40, 150, 300, 20 }, { 40, 260, 300, 20 } };
            for (auto& r : rows) { help = std::min ({ help, worstContrastIn (img, r, G.ink), worstContrastIn (img, r, G.dim) }); helpHead = std::min (helpHead, worstContrastIn (img, r, G.slime)); }
        }
        check (notes >= 4.5, "GHOST note capsules (violet / teal, any confidence) over the darkest and lightest aurora: the label >= 4.5:1 (" + juce::String (notes, 2) + ")");
        check (help >= 4.5 && helpHead >= 3.0, "GHOST ? overlay: keys / text >= 4.5:1 (" + juce::String (help, 2) + "), the green section headers >= 3:1 (" + juce::String (helpHead, 2) + ")");
        juce::Image tip (juce::Image::ARGB, 300, 40, true); { juce::Graphics g (tip); ed->lnf.drawTooltip (g, "", 300, 40); }
        juce::Image menu (juce::Image::ARGB, 200, 200, true); { juce::Graphics g (menu); ed->lnf.drawPopupMenuBackground (g, 200, 200); }
        const double t = std::min (worstContrastIn (tip, { 10, 8, 280, 24 }, G.ink), worstContrastIn (menu, { 10, 10, 180, 180 }, G.ink));
        const double hl = contrast (ed->lnf.findColour (juce::PopupMenu::highlightedTextColourId), ed->lnf.findColour (juce::PopupMenu::highlightedBackgroundColourId));
        check (t >= 4.5 && hl >= 4.5, "GHOST tooltips / menus: ink >= 4.5:1 (" + juce::String (t, 2) + "), the highlighted item >= 4.5:1 (" + juce::String (hl, 2) + ")");
    }

    // ---- layout + tooltips in GHOST, both sizes, every workspace
    static const char* wsName[4] { "EDIT", "FLEX", "PITCH", "PLAY" };
    for (auto size : { juce::Point<int> (1160, 700), juce::Point<int> (1600, 1000) })
    {
        pe->setSize (size.x, size.y); pe->resized(); pump (5);
        for (int ws = 0; ws < 4; ++ws)
        {
            pe->setWorkspace ((gslice::ui::Workspace) ws); pump (5);
            int out = 0, overlap = 0, tight = 0, noTip = 0;
            std::vector<juce::Component*> vis;
            for (auto* c : pe->getChildren()) if (c->isVisible() && c != &pe->help && dynamic_cast<ghosty::exportgate::Hint*> (c) == nullptr
                                                  && dynamic_cast<juce::TooltipWindow*> (c) == nullptr && dynamic_cast<juce::ResizableCornerComponent*> (c) == nullptr && (ws != 3 || c != &pe->playPanel)) vis.push_back (c);
            for (auto* c : vis) if (! pe->getLocalBounds().contains (c->getBounds())) { ++out; std::printf ("    outside: %s\n", c->getComponentID().toRawUTF8()); }
            for (size_t i = 0; i < vis.size(); ++i) for (size_t j = i + 1; j < vis.size(); ++j) if (vis[i]->getBounds().intersects (vis[j]->getBounds())) { ++overlap; std::printf ("    overlap: %s / %s\n", vis[i]->getComponentID().toRawUTF8(), vis[j]->getComponentID().toRawUTF8()); }
            for (auto* c : vis) if (auto* b = dynamic_cast<juce::TextButton*> (c)) { const auto f = pe->lnf.getTextButtonFont (*b, b->getHeight()); if (ghosty::textWidth (f, b->getButtonText()) > ghosty::glass::pillBody (b->getLocalBounds().toFloat()).getWidth() - 6) { ++tight; std::printf ("    text too wide: %s\n", b->getButtonText().toRawUTF8()); } }
            std::vector<juce::Component*> ctl; collectControls (*pe, ctl);
            for (auto* c : ctl) if (auto* tc = dynamic_cast<juce::SettableTooltipClient*> (c); tc != nullptr && tc->getTooltip().isEmpty()) { ++noTip; std::printf ("    no tooltip: %s\n", c->getComponentID().toRawUTF8()); }
            check (out == 0 && overlap == 0 && tight == 0 && noTip == 0, "GHOST " + juce::String (size.x) + "x" + juce::String (size.y) + " " + wsName[ws] + ": " + juce::String ((int) vis.size())
                   + " panels / controls inside, no overlaps, labels fit the pills, " + juce::String ((int) ctl.size()) + " controls all with tooltips");
        }
    }

    // ---- GHOST screenshots: EDIT, FLEX, PITCH (notes + f0), PLAY (pads), HOLD, REPEAT, the ? overlay, the empty screen; 1280x780 + 1600x1000
    int written = 0;
    auto shot = [&] (juce::Component& c, const juce::String& name) { snap (c, shots.getChildFile ("slice-ghost-" + name + ".png")); ++written; };
    Rig hrig (vowel (48000));
    std::unique_ptr<SliceEditor> he (static_cast<SliceEditor*> (hrig->createEditor())); he->setVisible (true);
    Rig rrig (drums (48000, 4.0));
    std::unique_ptr<SliceEditor> re (static_cast<SliceEditor*> (rrig->createEditor())); re->setVisible (true);
    {
        auto qq = ops::splitRange (rig->project(), 48000, 96000); qq = ops::splitAt (*qq, qq->regions.back().id, 150000); rig->apply (*qq, "cuts");
        int64 mk = 0; auto qm = ops::addMarker (rig->project(), rig->project().regions[0].id, 30000, &mk); if (qm) rig->apply (*qm, "marker");
        rig->apply (*ops::turnIntoInstrument (rig->project(), (int) rig->currentSlices().size()), "map"); rig->buildBankNow();
    }
    for (auto size : { juce::Point<int> (1280, 780), juce::Point<int> (1600, 1000) })
    {
        const juce::String sz = juce::String (size.x);
        ed->setSize (size.x, size.y); ed->resized(); ed->setLook (SliceEditor::kLookGhost); ed->view.zoomToFit(); pump (10);
        ed->setWorkspace (gslice::ui::Workspace::Edit); ed->view.sel.regionId = rig->project().regions[0].id; ed->view.sel.a = 20000; ed->view.sel.b = 40000; rig->setCursor (70000); ed->refresh(); pump (10);
        shot (*ed, "edit-" + sz);
        ed->view.sel.a = ed->view.sel.b = 0;
        ed->setWorkspace (gslice::ui::Workspace::Flex); if (! rig->project().regions[0].warp.empty()) { ed->view.sel.markerId = rig->project().regions[0].warp[std::min<size_t> (1, rig->project().regions[0].warp.size() - 1)].id; ed->view.sel.markerRegion = rig->project().regions[0].id; } ed->refresh(); pump (10);
        shot (*ed, "flex-" + sz);
        ed->setWorkspace (gslice::ui::Workspace::Play); ed->playPanel.selectPad (1); pump (10);
        shot (*ed, "play-" + sz);
        ed->setWorkspace (gslice::ui::Workspace::Edit); pump (5);
        // PITCH: the analysed melody, notes selected + the f0 curve
        pe->setSize (size.x, size.y); pe->resized(); pe->setLook (SliceEditor::kLookGhost); pe->setWorkspace (gslice::ui::Workspace::Pitch); pe->view.zoomToFit(); pump (10);
        pe->view.showF0 = true;
        const auto notes = pe->view.pitchNotes();
        std::vector<gslice::pitchedit::NoteRef> pick; for (size_t i = 0; i < notes.size(); i += 2) pick.push_back (notes[i]);
        pe->view.selectNotes (pick);
        pe->refresh(); pe->view.repaint(); pump (10);
        shot (*pe, "pitch-" + sz);
        pe->showHelp (true); pump (5); shot (*pe, "help-" + sz); pe->showHelp (false);
        // HOLD + REPEAT
        he->setSize (size.x, size.y); he->resized(); he->setLook (SliceEditor::kLookGhost); he->setWorkspace (gslice::ui::Workspace::Edit); he->view.zoomToFit(); pump (10);
        if (hrig->project().regions.front().holds.empty()) { he->view.sel.a = 2400; he->view.sel.b = 34560; he->holdBtn.triggerClick(); pump (20); }
        he->view.zoomToFit(); pump (10); shot (*he, "hold-" + sz);
        re->setSize (size.x, size.y); re->resized(); re->setLook (SliceEditor::kLookGhost); re->setWorkspace (gslice::ui::Workspace::Edit); re->view.zoomToFit(); pump (10);
        if (rrig->project().repeats.empty()) { re->view.sel.a = 48000; re->view.sel.b = 72000; re->repeatBtn.triggerClick(); pump (20); }
        re->view.viewStart = 30000; re->view.hfPerPx = 60; re->view.repaint(); pump (10); shot (*re, "repeat-" + sz);
        {
            SliceProcessor empty; empty.setPlayConfigDetails (0, 2, 48000, 512); empty.prepareToPlay (48000, 512); empty.uiTheme = SliceEditor::kLookGhost;
            std::unique_ptr<juce::AudioProcessorEditor> ee (empty.createEditor()); ee->setVisible (true); ee->setSize (size.x, size.y); pump (20);
            shot (*ee, "empty-" + sz);
        }
    }
    ed->setSize (1280, 780); ed->resized(); pump (5);
    { auto img = ed->createComponentSnapshot (ed->getLocalBounds().withHeight (SliceEditor::kHeader + SliceEditor::kInfo), true, 2.0f);
      const auto f = shots.getChildFile ("slice-ghost-header-hidpi.png"); f.deleteFile(); juce::FileOutputStream os (f); juce::PNGImageFormat png; png.writeImageToStream (img, os); ++written; }
    check (written == 17 && shots.getChildFile ("slice-ghost-pitch-1600.png").existsAsFile(), "GHOST screenshots written (" + juce::String (written) + ") to " + shots.getFullPathName());
    he.reset(); re.reset(); pe.reset(); ed.reset(); pump (20);
}
