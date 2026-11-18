// BeatsTests: the GHOST look (liquid glass on the LIGHT base, Beats-only 4th look). Group: ghostui
//   the look button cycles BLACK -> PURPLE -> LIGHT -> GHOST -> BLACK; GHOST is remembered on this computer (theme_name "ghost"), old values load
//   unchanged; BLACK / PURPLE / LIGHT render pixel-identically before and after visiting GHOST; GHOST keeps the LIGHT layout; text contrast on the
//   rendered pixels (the worst glass pixel under each text); tooltips; the cached aurora / panels / sprites; full-repaint time GHOST vs LIGHT;
//   the mascot + wordmark on glass; GHOST screenshots of every page.
namespace ghostui {
static double worstIn(const juce::Image& img, juce::Rectangle<int> area, juce::Colour text) {
    area = area.getIntersection(img.getBounds()); double worst = 99.0;
    const juce::Image::BitmapData bd(img, juce::Image::BitmapData::readOnly);
    for (int y = area.getY(); y < area.getBottom(); ++y) for (int x = area.getX(); x < area.getRight(); ++x) worst = std::min(worst, ghosty::themes::contrast(text, bd.getPixelColour(x, y).withAlpha(1.0f)));
    return worst;
}
static bool same(const juce::Image& a, const juce::Image& b) {
    if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight()) return false;
    const juce::Image::BitmapData A(a, juce::Image::BitmapData::readOnly), B(b, juce::Image::BitmapData::readOnly);
    for (int y = 0; y < a.getHeight(); ++y) for (int x = 0; x < a.getWidth(); ++x) if (A.getPixelColour(x, y) != B.getPixelColour(x, y)) return false;
    return true;
}
static bool visibleIn(juce::Component* c, juce::Component* root) { for (; c && c != root; c = c->getParentComponent()) if (!c->isVisible()) return false; return c == root; }
static void collect(juce::Component& c, std::vector<juce::Component*>& out) { for (auto* ch : c.getChildren()) { out.push_back(ch); collect(*ch, out); } }
static float textW(const juce::Font& f, const juce::String& s) { return ghosty::textWidth(f, s); }
// the text box of `txt` drawn with `f` and `just` inside `r`
static juce::Rectangle<int> textBox(juce::Rectangle<int> r, const juce::Font& f, const juce::String& txt, juce::Justification just) {
    const int w = juce::jmin(r.getWidth(), (int) std::ceil(textW(f, txt))), h = juce::jmin(r.getHeight(), (int) std::ceil(f.getAscent()));
    int x = r.getX(); if (just.testFlags(juce::Justification::horizontallyCentred)) x = r.getCentreX() - w / 2; else if (just.testFlags(juce::Justification::right)) x = r.getRight() - w;
    int y = r.getCentreY() - h / 2; if (just.testFlags(juce::Justification::top)) y = r.getY(); else if (just.testFlags(juce::Justification::bottom)) y = r.getBottom() - h;
    return { x, y, w, h };
}
static void png(const juce::Image& img, const juce::File& f) { f.deleteFile(); juce::FileOutputStream o(f); if (o.openedOk()) { juce::PNGImageFormat p; p.writeImageToStream(img, o); } }
} // namespace ghostui

static void testGhostUi(const juce::File& shots) {
    using namespace ghostui; using ghosty::themes::contrast;
    section("GHOST look (Beats): 4-look cycle, persistence, old looks unchanged, layout, glass contrast on pixels, tooltips, caches, repaint time, screenshots");
    shots.createDirectory();
    {   // tokens: text >= 4.5:1 on every flat glass surface; ON reads on every accent; accents read as text on the surfaces
        bx::applyLook(bx::kLookGhost);
        double text = 99, onAcc = 99, acc = 99;
        for (auto bg : { bx::BG, bx::CARD, bx::CARD2, bx::POP }) for (auto fg : { bx::INK, bx::GREY }) text = std::min(text, contrast(fg, bg));
        for (auto a : { bx::PURPLE, bx::GREEN, bx::PINK, bx::AMBER, bx::BLUE, bx::CYAN, bx::RED, bx::LIME }) { onAcc = std::min(onAcc, contrast(bx::ON, a)); for (auto bg : { bx::BG, bx::CARD, bx::CARD2 }) acc = std::min(acc, contrast(a, bg)); }
        const bool flags = bx::GLASS && bx::LOOK == 3 && bx::THEME == ghosty::themes::Theme::Light && !bx::isDark;
        bx::applyLook(0);
        CHECK(flags && text >= 4.5 && onAcc >= 4.5 && acc >= 4.5 && !bx::GLASS && bx::LOOK == 0, "GHOST tokens: text %.2f:1, ON on accents %.2f:1, accents on glass %.2f:1 (>= 4.5); BLACK again clears GLASS", text, onAcc, acc);
        CHECK(!ghosty::themes::glassLight(), "the shared opt-in glass LIGHT (Ghosty Pro's) stays off in Beats: the shared themes untouched");
    }
    BeatsXProcessor p; p.applyKit(10, true); p.focusMode = false;
    const auto savedName = p.settings->getValue("theme_name"); const bool hadName = p.settings->containsKey("theme_name"); const auto savedDark = p.settings->getValue("dark"); const bool hadDark = p.settings->containsKey("dark");
    std::unique_ptr<juce::AudioProcessorEditor> e(p.createEditor()); auto* be = dynamic_cast<BeatsXEditor*>(e.get()); e->setSize(1180, 790); be->setThemeForTest(0);
    auto* root = be->contentForTest();
    // ---- the cycle
    {
        auto* tb = be->themeButtonForTest(); juce::StringArray seq; juce::String names;
        for (int i = 0; i < 5; ++i) { names << tb->getButtonText() << " "; tb->onClick(); seq.add(juce::String(be->lookForTest()) + "/" + juce::String(p.look())); }
        CHECK(seq.joinIntoString(" ") == "1/1 2/2 3/3 4/4 0/0" && names.trim() == "PURPLE LIGHT GHOST NEON BLACK", "the look button cycles BLACK -> PURPLE -> LIGHT -> GHOST -> NEON -> BLACK (%s; it names the next look: %s)", seq.joinIntoString(" ").toRawUTF8(), names.trim().toRawUTF8());
        CHECK(tb->getTooltip().contains("GHOST") && tb->getTooltip().contains("LIGHT"), "the look button's tooltip lists the four looks");
    }
    // ---- persistence (this computer's settings, as the theme always was): GHOST = "ghost"; old values load as before; unknown = BLACK
    {
        be->setThemeForTest(3); p.settings->saveIfNeeded();   // (written when the processor closes; the test reads it back now)
        const bool saved = p.settings->getValue("theme_name") == "ghost" && p.look() == 3 && p.theme == ghosty::themes::Theme::Light;
        BeatsXProcessor q; std::unique_ptr<juce::AudioProcessorEditor> qe(q.createEditor()); auto* qb = dynamic_cast<BeatsXEditor*>(qe.get());
        const bool reopened = q.look() == 3 && qb->lookForTest() == 3 && bx::GLASS;
        qe.reset();
        CHECK(saved && reopened, "GHOST is remembered (theme_name \"ghost\") and a new window opens in GHOST");
        struct Old { const char* name; int want; } olds[] { { "dark", 0 }, { "purple", 1 }, { "light", 2 }, { "bogus", 0 }, { "7", 0 } };
        juce::String got; bool ok = true;
        for (auto& o : olds) {
            p.settings->setValue("theme_name", o.name); p.settings->saveIfNeeded(); BeatsXProcessor r; std::unique_ptr<juce::AudioProcessorEditor> re(r.createEditor()); auto* rb = dynamic_cast<BeatsXEditor*>(re.get());
            got << o.name << "->" << rb->lookForTest() << " "; ok = ok && r.look() == o.want && rb->lookForTest() == o.want && !bx::GLASS && r.theme == ghosty::themes::fromInt(o.want);
        }
        p.settings->removeValue("theme_name"); p.settings->setValue("dark", false); p.settings->saveIfNeeded(); { BeatsXProcessor r; got << "legacy dark=false->" << r.look(); ok = ok && r.look() == 2; }
        bx::applyLook(be->lookForTest());   // (the other windows changed the shared palette: this window's children still wear GHOST)
        CHECK(ok, "saved looks from before GHOST open unchanged, unknown -> BLACK (%s)", got.trim().toRawUTF8());
        be->setThemeForTest(0);
    }
    // ---- NEON (5th look, 2026-11-11): tokens readable, remembered as "neon", reopens in NEON, BLACK clears it
    {
        bx::applyLook(bx::kLookNeon);
        double text = 99, acc = 99, onAcc = 99;
        for (auto bg : { bx::BG, bx::CARD, bx::CARD2, bx::POP }) for (auto fg : { bx::INK, bx::GREY }) text = std::min(text, contrast(fg, bg));
        for (auto a : { bx::PURPLE, bx::GREEN, bx::PINK, bx::AMBER, bx::BLUE, bx::CYAN, bx::RED, bx::LIME }) { onAcc = std::min(onAcc, contrast(bx::ON, a)); for (auto bg : { bx::BG, bx::CARD, bx::CARD2 }) acc = std::min(acc, contrast(a, bg)); }
        const bool flags = bx::NEON && !bx::GLASS && bx::LOOK == 4 && bx::isDark && bx::THEME == ghosty::themes::Theme::Dark;
        bx::applyLook(0); const bool cleared = !bx::NEON && bx::LOOK == 0;
        CHECK(flags && cleared && text >= 4.5 && acc >= 4.5 && onAcc >= 4.5, "NEON tokens: text %.2f:1, accents %.2f:1, ON on accents %.2f:1 (>= 4.5); BLACK clears NEON", text, acc, onAcc);
        be->setThemeForTest(4); p.settings->saveIfNeeded();
        const bool saved = p.settings->getValue("theme_name") == "neon" && p.look() == 4 && p.theme == ghosty::themes::Theme::Dark;
        BeatsXProcessor q; std::unique_ptr<juce::AudioProcessorEditor> qe(q.createEditor()); auto* qb = dynamic_cast<BeatsXEditor*>(qe.get());
        const bool reopened = q.look() == 4 && qb->lookForTest() == 4 && bx::NEON; qe.reset();
        bx::applyLook(be->lookForTest());
        CHECK(saved && reopened, "NEON is remembered (theme_name \"neon\") and a new window opens in NEON");
        be->setThemeForTest(0);
    }
    // ---- BLACK / PURPLE / LIGHT render the same before and after GHOST (every page; no message loop between shots: the header ghosts stand still)
    std::vector<juce::Image> before, after; std::vector<juce::Image> ghostShots;
    std::vector<std::vector<juce::Rectangle<int>>> layLight, layGhost;
    auto layout = [&] { std::vector<juce::Component*> v; collect(*root, v); std::vector<juce::Rectangle<int>> r; for (auto* c : v) r.push_back(c->isVisible() ? c->getBounds() : juce::Rectangle<int>(-1, -1, 0, 0)); return r; };
    for (int look = 0; look < 3; ++look) { be->setThemeForTest(look); for (int pg = 0; pg < 10; ++pg) { be->showPageForTest(pg); before.push_back(e->createComponentSnapshot(e->getLocalBounds(), true, 1.0f)); if (look == 2) layLight.push_back(layout()); } }
    be->setThemeForTest(3); for (int pg = 0; pg < 10; ++pg) { be->showPageForTest(pg); ghostShots.push_back(e->createComponentSnapshot(e->getLocalBounds(), true, 1.0f)); layGhost.push_back(layout()); }
    be->setThemeForTest(4); for (int pg = 0; pg < 10; ++pg) { be->showPageForTest(pg); (void) e->createComponentSnapshot(e->getLocalBounds(), true, 1.0f); }   // (NEON visited too: the old looks must not change after it either)
    for (int look = 0; look < 3; ++look) { be->setThemeForTest(look); for (int pg = 0; pg < 10; ++pg) { be->showPageForTest(pg); after.push_back(e->createComponentSnapshot(e->getLocalBounds(), true, 1.0f)); } }
    { int eq = 0; for (size_t i = 0; i < before.size(); ++i) { if (same(before[i], after[i])) ++eq; else std::printf("    differs: look %d page %d\n", (int) i / 10, (int) i % 10); }
      CHECK(eq == (int) before.size(), "BLACK / PURPLE / LIGHT x 10 pages are pixel-identical before and after GHOST (%d/%d)", eq, (int) before.size());
      int lay = 0; for (size_t i = 0; i < layLight.size(); ++i) lay += layLight[i] == layGhost[i] ? 1 : 0;
      int diffG = 0; for (size_t i = 0; i < ghostShots.size(); ++i) diffG += same(ghostShots[i], before[20 + i]) ? 0 : 1;
      CHECK(lay == 10 && diffG == 10, "GHOST keeps the LIGHT layout on all 10 pages (%d/10 identical bounds) and paints its own look (%d/10 pages differ from LIGHT)", lay, diffG); }
    // ---- tooltips: GHOST shows the same tooltip on every control as LIGHT
    {
        auto tips = [&] { juce::StringArray t; std::vector<juce::Component*> v; collect(*root, v); for (auto* c : v) if (auto* tc = dynamic_cast<juce::SettableTooltipClient*>(c)) t.add(tc->getTooltip()); return t; };
        be->setThemeForTest(2); const auto tl = tips(); be->setThemeForTest(3); const auto tg = tips();
        int n = 0; for (auto& s : tg) n += s.isNotEmpty() ? 1 : 0;
        CHECK(tl == tg && n >= 10, "GHOST: every control keeps its tooltip (%d tooltips, identical to LIGHT)", n);
        juce::Image tip(juce::Image::ARGB, 320, 44, true); { juce::Graphics g(tip); e->getLookAndFeel().drawTooltip(g, "", 320, 44); }
        juce::Image menu(juce::Image::ARGB, 200, 200, true); { juce::Graphics g(menu); e->getLookAndFeel().drawPopupMenuBackground(g, 200, 200); }
        const double t = std::min(worstIn(tip, { 10, 8, 300, 28 }, bx::INK), worstIn(menu, { 10, 10, 180, 180 }, bx::INK));
        const double hl = contrast(e->getLookAndFeel().findColour(juce::PopupMenu::highlightedTextColourId), e->getLookAndFeel().findColour(juce::PopupMenu::highlightedBackgroundColourId));
        CHECK(t >= 4.5 && hl >= 4.5, "GHOST tooltips / menus: glass card, ink %.2f:1, highlighted item %.2f:1", t, hl);
    }
    // ---- contrast on the rendered pixels, every page: each enabled text control's text against the worst glass pixel under it (the control's own
    //      glass drawn, its text left out); the house scan (text vs its dominant background) for everything else too
    {
        auto& lnf = e->getLookAndFeel(); double worstBtn = 99, worstOn = 99, worstLbl = 99, worstCombo = 99, worstBar = 99; int nb = 0, nl = 0, nc = 0, nbar = 0, low = 0;
        for (int pg = 0; pg < 10; ++pg) {
            be->showPageForTest(pg);
            std::vector<juce::Component*> v; collect(*root, v);
            for (auto* c : v) {
                if (!visibleIn(c, root) || !c->isEnabled() || c->getWidth() < 8) continue;
                const auto R = e->getLocalArea(c->getParentComponent(), c->getBounds());
                auto under = [&] { c->setVisible(false); auto img = e->createComponentSnapshot(R, true, 1.0f); c->setVisible(true); return img; };
                if (auto* b = dynamic_cast<juce::TextButton*>(c); b && b->getButtonText().isNotEmpty()) {
                    auto img = under(); { juce::Graphics g(img); lnf.drawButtonBackground(g, *b, {}, false, false); }
                    const bool filled = b->getToggleState() || (bool) b->getProperties()["accent"]; const auto col = filled ? bx::ON : b->findColour(juce::TextButton::textColourOffId);
                    const auto f = lnf.getTextButtonFont(*b, b->getHeight());
                    const auto body = ghosty::glass::pillBody(img.getBounds().toFloat()).toNearestInt().reduced(4, 0);
                    const double k = worstIn(img, textBox(body, f, b->getButtonText(), juce::Justification::centred).reduced(0, 1), col); ++nb;
                    (filled ? worstOn : worstBtn) = std::min(filled ? worstOn : worstBtn, k); if (k < 4.5 && ++low <= 30) std::printf("    low %.2f button \"%s\" page %d\n", k, b->getButtonText().toRawUTF8(), pg);
                } else if (auto* cb = dynamic_cast<juce::ComboBox*>(c); cb && cb->getText().isNotEmpty()) {
                    auto img = under(); { juce::Graphics g(img); lnf.drawComboBox(g, cb->getWidth(), cb->getHeight(), false, 0, 0, 0, 0, *cb); }
                    const auto f = lnf.getComboBoxFont(*cb); const double k = worstIn(img, textBox({ 4, 1, cb->getWidth() - 18, cb->getHeight() - 2 }, f, cb->getText(), juce::Justification::centredLeft).reduced(0, 1), cb->findColour(juce::ComboBox::textColourId)); ++nc;
                    worstCombo = std::min(worstCombo, k); if (k < 4.5 && ++low <= 30) std::printf("    low %.2f combo \"%s\" page %d\n", k, cb->getText().toRawUTF8(), pg);
                } else if (auto* l = dynamic_cast<juce::Label*>(c); l && l->getText().trim().isNotEmpty() && !l->isBeingEdited() && dynamic_cast<juce::ComboBox*>(l->getParentComponent()) == nullptr) {
                    const bool onBar = dynamic_cast<juce::Slider*>(l->getParentComponent()) != nullptr;
                    auto img = under(); const auto br = l->getBorderSize().subtractedFrom(l->getLocalBounds());
                    const auto tb = l->getText().containsChar('\n') || textW(l->getFont(), l->getText()) > (float) br.getWidth() ? br : textBox(br, l->getFont(), l->getText(), l->getJustificationType());
                    const double k = worstIn(img, tb, l->findColour(juce::Label::textColourId));
                    if (onBar) { ++nbar; worstBar = std::min(worstBar, k); } else { ++nl; worstLbl = std::min(worstLbl, k); }
                    if (k < 4.5 && ++low <= 30) std::printf("    low %.2f label \"%s\" page %d colour %08x pink %08x\n", k, l->getText().substring(0, 30).toRawUTF8(), pg, (unsigned) l->findColour(juce::Label::textColourId).getARGB(), (unsigned) bx::PINK.getARGB());
                    if (k < 4.5 && low == 1 && std::getenv("BEATS_GHOST_DEBUG")) png(img, shots.getChildFile("dbg-label.png"));
                }
            }
        }
        CHECK(worstBtn >= 4.5 && worstOn >= 4.5, "GHOST buttons (%d over 10 pages): text over its own glass pill >= 4.5:1 at the worst pixel (off %.2f, on %.2f)", nb, worstBtn, worstOn);
        CHECK(worstCombo >= 4.5 && worstLbl >= 4.5 && worstBar >= 4.5, "GHOST combos (%d) %.2f, labels (%d) %.2f, value bars (%d) %.2f: >= 4.5:1 at the worst pixel", nc, worstCombo, nl, worstLbl, nbar, worstBar);
        // painted captions on the glass surfaces (no text in the image): MASTER / PERFORM / MACROS, the step numbers, the version
        juce::Image surf(juce::Image::ARGB, root->getWidth(), root->getHeight(), true); be->showPageForTest(0); { juce::Graphics g(surf); be->paintGlassSurfacesForTest(g); }
        auto* grid = be->partForTest("grid"); const int lh = root->getHeight();
        if (std::getenv("BEATS_GHOST_DEBUG")) { std::printf("    cap %.2f %.2f %.2f %.2f\n", worstIn(surf, { 18, lh - 94, 60, 11 }, bx::GREY), worstIn(surf, { 474, lh - 94, 60, 11 }, bx::GREY), worstIn(surf, { 768, lh - 94, 140, 11 }, bx::GREY), worstIn(surf, { 180, 40, 36, 12 }, bx::GREY)); png(surf, shots.getChildFile("dbg-surf.png")); }
        const double cap = std::min({ worstIn(surf, { 18, lh - 94, 60, 11 }, bx::GREY), worstIn(surf, { 474, lh - 94, 60, 11 }, bx::GREY), worstIn(surf, { 768, lh - 94, 140, 11 }, bx::GREY), worstIn(surf, textBox({ 150, 40, 66, 12 }, bx::font(9.0f, false), juce::String("v") + BeatsXProcessor::kVersion, juce::Justification::centredRight), bx::GREY) });
        const double steps = std::min(worstIn(surf, { grid->getX(), grid->getY() - 13, grid->getWidth(), 12 }, bx::GREY), worstIn(surf, { grid->getX(), grid->getY() - 13, grid->getWidth(), 12 }, bx::AMBER));
        // the track rows: a glass row over the card, names in INK / muted in DIM-less GREY
        auto* tl = be->partForTest("tl0"); juce::Image rows(juce::Image::ARGB, tl->getWidth(), tl->getHeight(), true);
        { juce::Graphics g(rows); g.drawImageAt(surf.getClippedImage(tl->getBounds()), 0, 0); const float rh = tl->getHeight() / 16.0f; for (int t = 0; t < 16; ++t) bx::glassui::sprites().draw(g, { 0, t * rh + 1.5f, (float) tl->getWidth(), rh - 3 }, 5.0f, bx::glassui::Row); }
        const double rowsK = worstIn(rows, { 8, 4, tl->getWidth() - 124, tl->getHeight() - 8 }, bx::INK);
        CHECK(cap >= 4.5 && steps >= 4.5 && rowsK >= 4.5, "GHOST painted text on glass: captions %.2f, step numbers %.2f, track names %.2f (>= 4.5:1 at the worst pixel)", cap, steps, rowsK);
        // the house scan (as the other looks are checked): text vs the dominant background under it, every page
        double worst = 99; int checked = 0, failed = 0;
        for (int pg = 0; pg < 10; ++pg) {
            be->showPageForTest(pg); const auto img = e->createComponentSnapshot(e->getLocalBounds(), true, 1.0f);
            std::vector<juce::Component*> v; collect(*root, v);
            for (auto* c : v) {
                if (!visibleIn(c, root) || !c->isEnabled()) continue; juce::String text;
                if (auto* l = dynamic_cast<juce::Label*>(c)) { if (l->isBeingEdited() || dynamic_cast<juce::ComboBox*>(l->getParentComponent()) || dynamic_cast<juce::Slider*>(l->getParentComponent())) continue; text = l->getText(); }
                else if (auto* b = dynamic_cast<juce::Button*>(c)) text = b->getButtonText(); else if (auto* cb = dynamic_cast<juce::ComboBox*>(c)) text = cb->getText();
                if (text.trim().isEmpty()) continue;
                auto area = e->getLocalArea(c->getParentComponent(), c->getBounds());
                if (dynamic_cast<juce::TextButton*>(c) != nullptr) area = ghosty::glass::pillBody(area.toFloat()).toNearestInt().reduced(4, 3);   // (the pill itself: the drifting header ghosts may pass behind its rim)
                juce::Colour bg; const double k = pxContrast(img, area, bg); ++checked; worst = std::min(worst, k);
                if (k < 4.5 && ++failed <= 20) std::printf("    LOW %.2f \"%s\" page %d\n", k, text.substring(0, 30).toRawUTF8(), pg);
            }
        }
        CHECK(failed == 0 && checked > 200, "GHOST house scan: %d text controls over 10 pages, worst %.2f:1, %d under 4.5:1", checked, worst, failed);
        // the mascot + the wordmark read on their glass plates
        juce::Image full(juce::Image::ARGB, root->getWidth(), root->getHeight(), true); { juce::Graphics g(full); root->paint(g); }
        auto legible = [&](juce::Rectangle<int> r) { int n = 0, hi = 0; const juce::Image::BitmapData A(full, juce::Image::BitmapData::readOnly), B(surf, juce::Image::BitmapData::readOnly);
            for (int y = r.getY(); y < r.getBottom(); ++y) for (int x = r.getX(); x < r.getRight(); ++x) { ++n; if (contrast(A.getPixelColour(x, y).withAlpha(1.0f), B.getPixelColour(x, y).withAlpha(1.0f)) >= 3.0) ++hi; }
            return n ? (double) hi / n : 0.0; };
        const double mas = legible(be->mascotRectForTest().toNearestInt()), word = legible({ 14, 6, 176, 40 });
        CHECK(mas > 0.08 && word > 0.08, "GHOST: the GHOST KIT mascot (%.0f%% of its box >= 3:1 against its glass disc) and the wordmark (%.0f%%) stand out on glass", 100 * mas, 100 * word);
    }
    // ---- caches: a repaint rebuilds nothing; a resize rebuilds the aurora once
    {
        be->setThemeForTest(3); be->showPageForTest(0); (void) e->createComponentSnapshot(e->getLocalBounds(), true, 1.0f);
        const int b0 = be->glassBuildsForTest(0), p0 = be->glassBuildsForTest(1), s0 = be->glassBuildsForTest(2);
        for (int i = 0; i < 3; ++i) (void) e->createComponentSnapshot(e->getLocalBounds(), true, 1.0f);
        const int b1 = be->glassBuildsForTest(0), p1 = be->glassBuildsForTest(1), s1 = be->glassBuildsForTest(2);
        e->setSize(1600, 1000); (void) e->createComponentSnapshot(e->getLocalBounds(), true, 1.0f); const int b2 = be->glassBuildsForTest(0);
        e->setSize(1180, 790);
        CHECK(b1 == b0 && p1 == p0 && s1 == s0 && b2 == b0 + 1, "GHOST paints from cached images: aurora %d -> %d, panels %d -> %d, glass sprites %d -> %d on repaint; a resize rebuilds the aurora once (%d)", b0, b1, p0, p1, s0, s1, b2);
    }
    // ---- full-repaint time GHOST vs LIGHT (warm caches): the grid page (16 x 16 glass cells), PADS, MIX and all pages together
    {
        auto timePages = [&](int look, std::initializer_list<int> pages) { be->setThemeForTest(look); double tot = 0;
            for (int pg : pages) { be->showPageForTest(pg); (void) e->createComponentSnapshot(e->getLocalBounds(), true, 1.0f); (void) e->createComponentSnapshot(e->getLocalBounds(), true, 1.0f);
                const double t0 = juce::Time::getMillisecondCounterHiRes(); for (int i = 0; i < 12; ++i) (void) e->createComponentSnapshot(e->getLocalBounds(), true, 1.0f); tot += (juce::Time::getMillisecondCounterHiRes() - t0) / 12.0; }
            return tot; };
        auto& P = p.model.patterns[(size_t) p.currentPattern()]; const auto keep = P; for (int t = 0; t < 16; ++t) for (int s = 0; s < 16; s += 1 + t % 3) if (P.steps[(size_t) t][(size_t) s].vel == 0) P.steps[(size_t) t][(size_t) s].vel = (uint8_t) (60 + (s * 7 + t * 3) % 67);
        p.publish();
        double gl = 0, gg = 0, all = 0, allG = 0;
        for (int rep = 0; rep < 2; ++rep) { gl = timePages(2, { 0 }); gg = timePages(3, { 0 }); }   // (twice: the second pass is the measured one)
        all = timePages(2, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 }); allG = timePages(3, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 });
        std::printf("  full repaint (1180 x 790): grid LIGHT %.2f ms, GHOST %.2f ms (x%.2f); all 10 pages LIGHT %.1f ms, GHOST %.1f ms (x%.2f)\n", gl, gg, gg / gl, all, allG, allG / all);
        P = keep; p.publish();
        CHECK(gg <= gl * 1.5 && allG <= all * 1.5, "GHOST full repaint within 1.5x LIGHT: grid x%.2f, all pages x%.2f", gg / gl, allG / all);
    }
    // ---- the RE-CREATE panel in GHOST, with its DRUMS HEARD summary + chips: every text over its own glass >= 4.5:1 at the worst pixel,
    //      a tooltip on every chip (BLACK / PURPLE / LIGHT: testThemePixels scans the same panel)
    {
        const auto K = a2mb::kitOf(kitIndexOf("Trap")); juce::Random rnd(5); auto C = a2mb::build(K, a2mb::patterns()[0], rnd);
        auto f = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("ghosty ghost-look loop 140bpm.wav"); a2mb::wav(C.L.x, C.L.sr, f);
        be->setThemeForTest(3); be->showPageForTest(0); be->startRecreateForTest(f);
        for (int i = 0; i < 600 && be->recreateBusyForTest(); ++i) { juce::Thread::sleep(10); be->recreateTickForTest(); }
        auto* panel = dynamic_cast<bx::RecreatePanel*>(be->recreatePanelForTest()); auto& lnf = e->getLookAndFeel(); double worst = 99; int n = 0, chips = 0, tipped = 0;
        if (panel) {
            std::vector<juce::Component*> v; collect(*panel, v);
            for (auto* c : v) {
                if (!visibleIn(c, root) || !c->isEnabled() || c->getWidth() < 8) continue;
                const auto R = e->getLocalArea(c->getParentComponent(), c->getBounds());
                auto under = [&] { c->setVisible(false); auto img = e->createComponentSnapshot(R, true, 1.0f); c->setVisible(true); return img; };
                if (auto* b = dynamic_cast<juce::TextButton*>(c); b && b->getButtonText().isNotEmpty()) {
                    auto img = under(); { juce::Graphics g(img); lnf.drawButtonBackground(g, *b, {}, false, false); }
                    const bool filled = b->getToggleState() || (bool) b->getProperties()["accent"]; const auto col = filled ? bx::ON : b->findColour(juce::TextButton::textColourOffId);
                    const auto body = ghosty::glass::pillBody(img.getBounds().toFloat()).toNearestInt().reduced(4, 0);
                    const double k = worstIn(img, textBox(body, lnf.getTextButtonFont(*b, b->getHeight()), b->getButtonText(), juce::Justification::centred).reduced(0, 1), col); ++n;
                    worst = std::min(worst, k); if (k < 4.5) std::printf("    low %.2f re-create button \"%s\"\n", k, b->getButtonText().toRawUTF8());
                } else if (auto* l = dynamic_cast<juce::Label*>(c); l && l->getText().trim().isNotEmpty() && dynamic_cast<juce::ComboBox*>(l->getParentComponent()) == nullptr && dynamic_cast<juce::Slider*>(l->getParentComponent()) == nullptr) {
                    auto img = under(); const auto br = l->getBorderSize().subtractedFrom(l->getLocalBounds());
                    const auto tb = l->getText().containsChar('\n') || textW(l->getFont(), l->getText()) > (float) br.getWidth() ? br : textBox(br, l->getFont(), l->getText(), l->getJustificationType());
                    const double k = worstIn(img, tb, l->findColour(juce::Label::textColourId)); ++n;
                    worst = std::min(worst, k); if (k < 4.5) std::printf("    low %.2f re-create label \"%s\"\n", k, l->getText().substring(0, 40).toRawUTF8());
                }
            }
            for (auto& c : panel->chips) if (c.isVisible()) { ++chips; if (c.getTooltip().isNotEmpty()) ++tipped; }
        }
        png(e->createComponentSnapshot(e->getLocalBounds(), true, 1.0f), shots.getChildFile("beats-ghost-recreate.png"));
        be->closeRecreateForTest(); f.deleteFile();
        CHECK(panel && chips >= 3 && tipped == chips && worst >= 4.5, "GHOST re-create panel: %d texts (DRUMS HEARD + %d chips, all with tooltips: %d) >= 4.5:1 at the worst pixel (%.2f)", n, chips, tipped, worst);
    }
    // ---- screenshots: every page + FOCUS + LARGE + the step editor, GHOST, default size
    {
        be->setThemeForTest(3); const char* pages[] { "sequencer", "sounds", "process", "tracker", "rhythmlab", "pads", "song", "scripts", "mix", "melody" }; int n = 0;
        for (int pg = 0; pg < 10; ++pg) { be->showPageForTest(pg); png(e->createComponentSnapshot(e->getLocalBounds(), true, 1.0f), shots.getChildFile(juce::String("beats-ghost-") + pages[pg] + ".png")); ++n; }
        p.focusMode = true; be->showPageForTest(0); png(e->createComponentSnapshot(e->getLocalBounds(), true, 1.0f), shots.getChildFile("beats-ghost-focus.png")); p.focusMode = false; be->showPageForTest(0); ++n;
        e->setSize(1600, 1000); png(e->createComponentSnapshot(e->getLocalBounds(), true, 1.0f), shots.getChildFile("beats-ghost-large-1600.png")); e->setSize(1180, 790); ++n;
        bx::Ed ed(p); bx::StepEditor se(ed, 0, 0, nullptr); se.setLookAndFeel(&e->getLookAndFeel()); se.setSize(se.getWidth(), se.getHeight()); se.resized();
        png(se.createComponentSnapshot(se.getLocalBounds(), true, 1.0f), shots.getChildFile("beats-ghost-step-editor.png")); se.setLookAndFeel(nullptr); ++n;
        CHECK(n == 13 && shots.getChildFile("beats-ghost-sequencer.png").existsAsFile(), "GHOST screenshots written (%d) to %s", n, shots.getFullPathName().toRawUTF8());
    }
    be->setThemeForTest(0); e.reset();
    if (hadName) p.settings->setValue("theme_name", savedName); else p.settings->removeValue("theme_name");
    if (hadDark) p.settings->setValue("dark", savedDark); else p.settings->removeValue("dark");
    p.settings->saveIfNeeded();
}
