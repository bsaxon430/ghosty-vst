// MickensTests: the GHOST look (stored theme 3: liquid glass on the LIGHT base, Mickens-local; the shared Theme enum untouched). Group: ghostui
//   the look button cycles BLACK -> PURPLE -> LIGHT -> GHOST -> BLACK; GHOST persists in the session (older builds read 3 as LIGHT); BLACK /
//   PURPLE / LIGHT render the same before and after visiting GHOST; GHOST text contrast measured on the rendered pixels (the worst glass pixel
//   under each text, the text composited with its alpha); tooltips kept; the cached layers; full-repaint time GHOST vs LIGHT; screenshots.
//   MickensTests ghostui [shots dir]

static double gWorstIn(const juce::Image& img, juce::Rectangle<int> area, juce::Colour text) {
    area = area.getIntersection(img.getBounds()); double worst = 99.0;
    const juce::Image::BitmapData bd(img, juce::Image::BitmapData::readOnly);
    for (int y = area.getY(); y < area.getBottom(); ++y) for (int x = area.getX(); x < area.getRight(); ++x) {
        const auto bg = bd.getPixelColour(x, y).withAlpha(1.0f); worst = std::min(worst, ghosty::themes::contrast(bg.overlaidWith(text), bg)); }
    return worst;
}
static bool gSame(const juce::Image& a, const juce::Image& b) {
    if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight()) return false;
    const juce::Image::BitmapData A(a, juce::Image::BitmapData::readOnly), B(b, juce::Image::BitmapData::readOnly);
    for (int y = 0; y < a.getHeight(); ++y) for (int x = 0; x < a.getWidth(); ++x) if (A.getPixelColour(x, y) != B.getPixelColour(x, y)) return false;
    return true;
}
static void gCollect(juce::Component& c, std::vector<juce::Component*>& out) { for (auto* k : c.getChildren()) if (k->isVisible()) { out.push_back(k); gCollect(*k, out); } }
static juce::TextButton* lookButton(juce::Component& ed) {
    std::vector<juce::Component*> all; gCollect(ed, all);
    for (auto* c : all) if (auto* b = dynamic_cast<juce::TextButton*>(c)) if (b->getTooltip().startsWith("Switch the look")) return b;
    return nullptr;
}

static void testGhostLook(const juce::File& shots) {
    std::cout << "\n[ghostui] GHOST look\n";
    using namespace ghosty::themes;
    {   const auto G = MickensEditor::contrastOf(MickensEditor::kLookGhost), L = MickensEditor::contrastOf(2);
        check(G.body >= 7.0 && G.secondary >= 4.5 && G.accent >= 4.5 && !glassLight(), "GHOST tokens: body " + juce::String(G.body, 2) + ":1 (>= 7), secondary " + juce::String(G.secondary, 2)
              + ":1, accents incl. the whole rainbow " + juce::String(G.accent, 2) + ":1 (>= 4.5) on window / panel / cell; LIGHT unchanged (body " + juce::String(L.body, 2) + ")"); }
    MickensEditor::freezeAnimation = true;
    MickensProcessor p; waitReady(p); p.theme = 1;
    std::unique_ptr<juce::AudioProcessorEditor> edp(p.createEditor()); auto* ed = dynamic_cast<MickensEditor*>(edp.get()); ed->setVisible(true); pump(300);
    auto* tb = lookButton(*ed);
    if (tb == nullptr) { check(false, "the look button is found"); return; }
    {   juce::String seq, names;
        for (int i = 0; i < 4; ++i) { names << tb->getButtonText() << " "; tb->onClick(); pump(120); seq << p.theme << " "; }
        check(seq.trim() == "0 2 3 1" && names.trim() == "PURPLE LIGHT GHOST BLACK" && tb->getTooltip().contains("GHOST") && tb->getTooltip().contains("LIGHT"),
              "the look button cycles BLACK -> PURPLE -> LIGHT -> GHOST -> BLACK (stored " + seq.trim() + "), names the next look (" + names.trim() + "), the tooltip lists all four"); }
    {   // persistence: GHOST round-trips as theme 3 ("ghost"); old sessions unchanged; unknown values as before; an older build reads 3 as LIGHT
        auto save = [](MickensProcessor& a) { juce::MemoryBlock mb; a.getStateInformation(mb); return mb; };
        p.theme = 3; auto mb = save(p); MickensProcessor q; waitReady(q); q.restoreState(mb.getData(), (int) mb.getSize()); const bool rt = q.theme == 3;
        auto xml = Peek::getXmlFromBinary(mb.getData(), (int) mb.getSize()); auto st = juce::ValueTree::fromXml(*xml); auto mm = st.getChildWithName("MODEL");
        const bool named = mm.getProperty("theme_name").toString() == "ghost" && !(bool) mm.getProperty("dark");
        bool old = true; for (int t : { 0, 1, 2 }) { mm.setProperty("theme", t, nullptr); juce::MemoryBlock o; Peek::copyXmlToBinary(*st.createXml(), o); q.restoreState(o.getData(), (int) o.getSize()); old = old && q.theme == t; }
        mm.setProperty("theme", 7, nullptr); { juce::MemoryBlock o; Peek::copyXmlToBinary(*st.createXml(), o); q.restoreState(o.getData(), (int) o.getSize()); }
        const bool unknown = q.theme == 2;                       // as before: clamped
        const bool olderBuild = juce::jlimit(0, 2, 3) == 2;      // what a build without GHOST does with 3: LIGHT, GHOST's base
        check(rt && named && old && unknown && olderBuild, "GHOST is saved with the session (theme 3, \"ghost\") and reopens as GHOST; old sessions (0 / 1 / 2) unchanged; unknown values as before");
        p.theme = 1; pump(150); }
    {   // the old looks render the same before and after visiting GHOST (main page + BREAK SEARCH)
        std::vector<juce::Image> before, after;
        auto grab = [&](std::vector<juce::Image>& out) { for (int t : { 0, 1, 2 }) { p.theme = t; pump(200); out.push_back(ed->createComponentSnapshot(ed->getLocalBounds()));
                                                           ed->openSearch(0, 0); pump(150); out.push_back(ed->createComponentSnapshot(ed->getLocalBounds())); ed->closeSearch(); pump(80); } };
        grab(before); p.theme = 3; pump(200); (void) ed->createComponentSnapshot(ed->getLocalBounds()); ed->openSearch(1, 1); pump(150); ed->closeSearch(); pump(80); grab(after);
        int same = 0; for (size_t i = 0; i < before.size(); ++i) same += gSame(before[i], after[i]) ? 1 : 0;
        check(same == (int) before.size(), "PURPLE / BLACK / LIGHT (main + search) are pixel-identical before and after GHOST (" + juce::String(same) + "/" + juce::String((int) before.size()) + ")"); }
    p.theme = 3; pump(250);
    {   // the cached layers + opaque window
        (void) ed->createComponentSnapshot(ed->getLocalBounds()); const int b0 = MickensEditor::ghostBackdropBuilds(), p0 = MickensEditor::ghostPanelBuilds();
        for (int i = 0; i < 3; ++i) (void) ed->createComponentSnapshot(ed->getLocalBounds());
        const int b1 = MickensEditor::ghostBackdropBuilds(), p1 = MickensEditor::ghostPanelBuilds();
        ed->setSize(1200, 845); (void) ed->createComponentSnapshot(ed->getLocalBounds()); const int bSmall = MickensEditor::ghostBackdropBuilds();   // (smaller: the 1x image is scaled down)
        ed->setSize(1800, 1267); (void) ed->createComponentSnapshot(ed->getLocalBounds()); const int b2 = MickensEditor::ghostBackdropBuilds() - (bSmall - b1);
        ed->setSize(1500, 1056); (void) ed->createComponentSnapshot(ed->getLocalBounds());
        juce::Image img(juce::Image::ARGB, ed->getWidth(), ed->getHeight(), true); { juce::Graphics g(img); ed->paintEntireComponent(g, true); }
        int holesN = 0; for (int y = 0; y < img.getHeight(); y += 2) for (int x = 0; x < img.getWidth(); x += 2) holesN += img.getPixelAt(x, y).getAlpha() < 255;
        std::cout << "      aurora builds " << b0 << " -> " << b1 << " (3 repaints) -> " << b2 << " (resize); glass panels " << p0 << " -> " << p1 << "; see-through px " << holesN << "\n";
        check(b1 == b0 && p1 == p0 && b2 == b0 + 1 && holesN == 0, "GHOST paints from cached images (no rebuild on repaint, one aurora rebuild on resize) and the window stays opaque"); }
    {   // full-repaint time GHOST vs LIGHT
        auto cost = [&](int t) { p.theme = t; pump(200); for (int i = 0; i < 3; ++i) (void) ed->createComponentSnapshot(ed->getLocalBounds()); double best = 1e9;
                                 for (int r = 0; r < 3; ++r) { const double t0 = juce::Time::getMillisecondCounterHiRes(); for (int i = 0; i < 6; ++i) (void) ed->createComponentSnapshot(ed->getLocalBounds());
                                                               best = std::min(best, (juce::Time::getMillisecondCounterHiRes() - t0) / 6.0); } return best; };
        const double l = cost(2), g = cost(3), l2 = cost(2), g2 = cost(3); const double ratio = std::min(g, g2) / std::min(l, l2);
        std::cout << "      full repaint LIGHT " << std::min(l, l2) << " ms, GHOST " << std::min(g, g2) << " ms (" << ratio << "x)\n";
        check(ratio <= 1.5, "GHOST full repaint within 1.5x of LIGHT (" + juce::String(ratio, 2) + "x)"); }
    p.theme = 3; pump(200);
    {   // texts painted on the glass (section titles, captions, bay labels), over the surfaces without any text
        juce::Image surf(juce::Image::ARGB, ed->getWidth(), ed->getHeight(), true); { juce::Graphics g(surf); ed->paintGhostSurfaces(g); }
        double worst = 99; juce::Rectangle<int> wr; for (auto& [r, c] : ed->ghostTexts()) { const double x = gWorstIn(surf, r, c); if (x < worst) { worst = x; wr = r; } }
        std::cout << "      painted texts on glass: worst " << worst << ":1 at " << wr.toString() << "\n";
        check(worst >= 4.5, "GHOST section titles / captions / bay labels >= 4.5:1 at the worst glass pixel (" + juce::String(worst, 2) + ")"); }
    {   // every visible button: its text over its own glass (lit = white on the tinted glass); every label / value box over what is under it
        double offB = 99, onB = 99, labels = 99; int nb = 0, nl = 0; juce::String wb, wl;
        auto scan = [&](const juce::String& view) {
            std::vector<juce::Component*> all; juce::Component* root = ed;
            if (view != "main") { std::vector<juce::Component*> every; gCollect(*ed, every); for (auto* c : every) if (c->getComponentID() == "searchPanel") root = c; }   // (the overlay covers the page)
            gCollect(*root, all);
            for (auto* c : all) {
                if (auto* b = dynamic_cast<juce::TextButton*>(c); b != nullptr && b->isEnabled() && b->getButtonText().isNotEmpty() && b->getWidth() > 14 && b->getHeight() > 12 && b->getAlpha() > 0.99f) {
                    const auto R = ed->getLocalArea(b->getParentComponent(), b->getBounds()); b->setVisible(false); auto img = ed->createComponentSnapshot(R); b->setVisible(true);
                    { juce::Graphics g(img); b->getLookAndFeel().drawButtonBackground(g, *b, juce::Colour(), false, false); }
                    const bool lit = b->getToggleState(); const auto f = b->getLookAndFeel().getTextButtonFont(*b, b->getHeight());
                    const float tw = juce::jmin((float) R.getWidth() - 10.0f, juce::GlyphArrangement::getStringWidth(f, b->getButtonText()));
                    const auto tr = juce::Rectangle<float>(tw, f.getAscent() * 0.7f).withCentre(img.getBounds().toFloat().getCentre()).toNearestInt();
                    const double x = gWorstIn(img, tr, b->findColour(lit ? juce::TextButton::textColourOnId : juce::TextButton::textColourOffId)); ++nb;
                    double& w = lit ? onB : offB; if (x < w) { w = x; if (x < 4.5) wb = view + ": " + b->getButtonText(); }
                } else if (auto* l = dynamic_cast<juce::Label*>(c); l != nullptr && l->getText().isNotEmpty() && l->getWidth() > 10 && l->getHeight() > 8 && l->isEnabled() && l->getAlpha() > 0.99f) {
                    bool dimmed = false; for (auto* q = l->getParentComponent(); q != nullptr; q = q->getParentComponent()) dimmed = dimmed || q->getAlpha() < 0.99f || !q->isEnabled();
                    if (dimmed) continue;   // (greyed out on purpose: a disabled / faded control)
                    const auto R = ed->getLocalArea(l->getParentComponent(), l->getBounds()); l->setVisible(false); auto img = ed->createComponentSnapshot(R); l->setVisible(true);
                    const auto f = l->getFont(); const float tw = juce::jmin((float) R.getWidth() - 4.0f, juce::GlyphArrangement::getStringWidth(f, l->getText().upToFirstOccurrenceOf("\n", false, false)));
                    const auto tr = juce::Rectangle<float>(tw, f.getAscent() * 0.6f).withCentre(img.getBounds().toFloat().getCentre()).toNearestInt();
                    const double x = gWorstIn(img, tr, l->findColour(juce::Label::textColourId)); ++nl; if (x < labels) { labels = x; wl = view + ": " + l->getText(); }
                }
            } };
        scan("main"); for (int md = 0; md < 3; ++md) { ed->openSearch(md == 0 ? 0 : 1, md); pump(150); scan("search " + juce::String(md)); } ed->closeSearch(); pump(80);
        std::cout << "      buttons " << nb << ": off " << offB << ":1, lit " << onB << ":1 " << wb << "; labels " << nl << ": " << labels << ":1 (" << wl << ")\n";
        check(nb > 100 && offB >= 4.5 && onB >= 4.5, "GHOST buttons (" + juce::String(nb) + ", main + search): off >= 4.5:1 (" + juce::String(offB, 2) + "), lit >= 4.5:1 (" + juce::String(onB, 2) + ") " + wb);
        check(nl > 5 && labels >= 4.5, "GHOST labels / value boxes (" + juce::String(nl) + "): >= 4.5:1 (" + juce::String(labels, 2) + " " + wl + ")"); }
    {   // tooltip + menu on the glass card, the highlighted menu item
        auto& lf = ed->getLookAndFeel();
        juce::Image tip(juce::Image::ARGB, 300, 40, true); { juce::Graphics g(tip); lf.drawTooltip(g, "", 300, 40); }
        juce::Image menu(juce::Image::ARGB, 200, 200, true); { juce::Graphics g(menu); lf.drawPopupMenuBackground(g, 200, 200); }
        const double t = std::min(gWorstIn(tip, { 10, 8, 280, 24 }, lf.findColour(juce::TooltipWindow::textColourId)), gWorstIn(menu, { 10, 10, 180, 180 }, lf.findColour(juce::PopupMenu::textColourId)));
        const double hl = contrast(lf.findColour(juce::PopupMenu::highlightedTextColourId), lf.findColour(juce::PopupMenu::highlightedBackgroundColourId));
        check(t >= 4.5 && hl >= 4.5, "GHOST tooltips / menus >= 4.5:1 (" + juce::String(t, 2) + "), highlighted item " + juce::String(hl, 2)); }
    {   // tooltips: the same set in GHOST as in BLACK
        auto tips = [&](int t) { p.theme = t; pump(200); std::map<juce::String, int> m; std::vector<juce::Component*> all; gCollect(*ed, all);
                                 for (auto* c : all) if (auto* tc = dynamic_cast<juce::SettableTooltipClient*>(c)) if (tc->getTooltip().isNotEmpty()) m[tc->getTooltip()]++; return m; };
        const auto a = tips(1), b = tips(3); int n = 0; for (auto& kv : b) n += kv.second;
        check(a == b && n > 80, "GHOST keeps every control's tooltip (" + juce::String(n) + ", the same as BLACK)"); }
    if (shots != juce::File()) {
        shots.createDirectory(); p.theme = 3; pump(250);
        { Head h; h.bpm = 174; p.setRateAndBufferSizeDetails(48000, 512); p.prepareToPlay(48000, 512); p.setPlayHead(&h); juce::AudioBuffer<float> buf(2, 512); juce::MidiBuffer m; for (int k = 0; k < 200; ++k) { p.processBlock(buf, m); h.ppq += 512 / (48000 * 60.0 / 174); } p.setPlayHead(nullptr); }
        pump(300); int n = 0;
        auto save = [&](const juce::String& name) { auto img = ed->createComponentSnapshot(ed->getLocalBounds()); auto f = shots.getChildFile("mick-ghost-" + name + ".png"); f.deleteFile(); juce::PNGImageFormat png; if (auto os = f.createOutputStream()) { png.writeImageToStream(img, *os); ++n; } };
        save("main"); static const char* sn[] = { "search-feel", "match-similar", "match-complement" };
        for (int md = 0; md < 3; ++md) { ed->openSearch(md == 0 ? 0 : 1, md); pump(200); save(sn[md]); } ed->closeSearch(); pump(80);
        check(n == 4, "GHOST screenshots written (" + juce::String(n) + ") to " + shots.getFullPathName()); }
    p.theme = 0; pump(100); edp.reset(); MickensEditor::freezeAnimation = false;
}
