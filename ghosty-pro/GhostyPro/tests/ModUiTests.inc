// MOD (round 3): the modulation UI, headless (like the `ui` mode): tiles placed only in free space, drag-to-knob (with a spring-loaded
// tab), ring geometry + hit testing + ring drag, the knob menu, the ROUTES list (sort, move), screenshots in every look.
// Included by TrapTests.cpp after TrapTestsNew.inc (needs contrastAudit / contrastOf).
#define GPRO_MOD_UI_TESTS 1
#include "ProModUI.h"
namespace modtest
{
static juce::File modShotDir ("/root/work/clang-mod/uishots");
static void shot (juce::Component& c, const juce::String& name)
{
    modShotDir.createDirectory();
    auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
    auto f = modShotDir.getChildFile (name + ".png"); f.deleteFile();
    if (auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream())) juce::PNGImageFormat().writeImageToStream (img, *os);
}
static juce::Point<int> screenCentre (juce::Component& c) { return c.localPointToGlobal (c.getLocalBounds().getCentre()); }
static void modUiTests (const juce::StringArray& args)
{
    if (args.size() > 0) modShotDir = juce::File (args[0]);
    section ("MOD UI: tiles in free space, drag a tile onto a knob (spring-loaded tab), rings + hit testing, knob menu, ROUTES list, screenshots");
    auto p = makeProc (progOf ("Trap Starter Stack"));
    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
    auto* pe = dynamic_cast<ProEditor*> (ed.get());
    pe->setSize (ProEditor::W, ProEditor::H); pe->setTheme (0); pump (60);
    auto* ui = pe->modUi();
    if (ui == nullptr) { CHECK (false, "the editor has a modulation UI"); return; }
    auto* content = pe->contentComponent();
    pe->showTab (1); pump (60);

    // ---- strictly additive: the new tiles / button sit on the MOD page without covering anything that was there
    {
        auto& page = ui->modPage;
        const auto tl = ui->tiles(); std::vector<juce::Component*> mine (tl.begin(), tl.end()); mine.push_back (ui->routesButton()); mine.push_back (ui->routes());
        int overlaps = 0, inside = 0, headerTiles = 0;
        for (auto* t : ui->tiles())
        {
            if (t->getParentComponent() != &page) continue;
            ++headerTiles;
            if (page.getLocalBounds().contains (t->getBounds())) ++inside;
            for (auto* c : page.getChildren())
                if (std::find (mine.begin(), mine.end(), c) == mine.end() && c->isVisible() && c->getBounds().intersects (t->getBounds())) { ++overlaps; printf ("      tile %s overlaps %s\n", t->getBounds().toString().toRawUTF8(), c->getBounds().toString().toRawUTF8()); }
        }
        for (auto* c : page.getChildren())
            if (std::find (mine.begin(), mine.end(), c) == mine.end() && c->isVisible() && c->getBounds().intersects (ui->routesButton()->getBounds())) ++overlaps;
        CHECK (headerTiles == 9 && inside == 9 && overlaps == 0 && (int) ui->tiles().size() == 9 + gpro::mod::numSrc - 1,
               "MOD page: %d header tiles (LFO 1 / 2, ENV 2 / 3 / 1, 4 macros) inside the page, %d overlaps with existing controls; %d tiles in all", headerTiles, overlaps, (int) ui->tiles().size());
    }
    // ---- drag the LFO 1 tile from the MOD page onto CUTOFF on the SYNTH page: hover over the SYNTH tab first
    auto* lfoTile = ui->tiles()[0];
    juce::Slider* cut = nullptr;
    {
        ui->beginDrag (gpro::mod::lfo1, -1, screenCentre (*lfoTile));
        // over the SYNTH tab (content x 16 .. 126, y 76 .. 108) for 0.5 s
        ui->dragMoveTo (content->localPointToGlobal (juce::Point<int> (60, 92)));
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        while (juce::Time::getMillisecondCounterHiRes() - t0 < 600) pump (20);
        const bool sprung = ui->hooks.currentTab() == 0;
        cut = ui->sliderFor ("cutoff");
        if (cut == nullptr) { CHECK (false, "found the CUTOFF knob"); return; }
        ui->dragMoveTo (screenCentre (*cut));
        const bool aimed = ui->drag().target.getComponent() == cut;
        shot (*pe, "mod-drag-black");
        ui->dragEnd (screenCentre (*cut));
        const auto l = p->modr.list();
        CHECK (sprung && aimed && l.size() == 1 && l[0].src == gpro::mod::lfo1 && l[0].dst == "cutoff",
               "drag LFO 1 from the MOD page: holding it over the SYNTH tab opened SYNTH (%d), CUTOFF was the target (%d), dropped: %d route(s) %s > %s",
               (int) sprung, (int) aimed, (int) l.size(), l.empty() ? "-" : gpro::mod::srcNames()[l[0].src].toRawUTF8(), l.empty() ? "-" : l[0].dst.toRawUTF8());
    }
    // a second source on the same knob (the menu's Mod Source), the amounts
    p->modr.add (gpro::mod::env2, "cutoff", -0.4f); p->modr.setAmount (0, 0.5f);
    pump (80);
    // ---- rings: geometry and hit testing
    {
        gpro::ModUi::KnobGeom kg; ui->knobGeom (*cut, kg);
        auto at = [&] (float r, float angle) { return juce::Point<float> (kg.c.x + std::sin (angle) * r, kg.c.y - std::cos (angle) * r); };
        const float mid = 0.5f * (kg.a0 + kg.a1);
        const auto r0 = ui->ringAt (at (gpro::ModUi::ringRadius (kg.rad, 0), mid)), r1 = ui->ringAt (at (gpro::ModUi::ringRadius (kg.rad, 1), kg.a0 + 0.3f));
        const auto inKnob = ui->ringAt (kg.c), far = ui->ringAt (kg.c.translated (kg.rad + 30, 0));
        auto* ovl = ui->overlay();
        const bool ovHit = ovl->hitTest ((int) at (gpro::ModUi::ringRadius (kg.rad, 0), mid).x, (int) at (gpro::ModUi::ringRadius (kg.rad, 0), mid).y);
        const bool ovMiss = ! ovl->hitTest ((int) kg.c.x, (int) kg.c.y);
        // the knob itself still turns where there is no ring, and a covered knob has no rings (the MOD page over the SYNTH page)
        auto* hit = content->getComponentAt (kg.c.toInt());
        const bool knobGetsCentre = hit == cut || cut->isParentOf (hit);
        CHECK (r0.conn == 0 && r1.conn == 1 && r0.slider == cut && inKnob.conn < 0 && far.conn < 0 && ovHit && ovMiss && knobGetsCentre,
               "rings on CUTOFF (knob radius %.1f): ring 1 at r %.1f -> route %d, ring 2 at r %.1f -> route %d, the knob's centre -> %d (the knob gets the click: %d), 30 px out -> %d; overlay hit test on the ring %d / off it %d",
               kg.rad, gpro::ModUi::ringRadius (kg.rad, 0), r0.conn, gpro::ModUi::ringRadius (kg.rad, 1), r1.conn, inKnob.conn, (int) knobGetsCentre, far.conn, (int) ovHit, (int) ovMiss);
        // drag ring 1 upwards: the amount grows; Shift = fine
        auto src = juce::Desktop::getInstance().getMainMouseSource();
        const auto p0 = at (gpro::ModUi::ringRadius (kg.rad, 0), mid); const auto now = juce::Time::getCurrentTime();
        const float before = p->modr.list()[0].amount;
        juce::MouseEvent down (src, p0, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0, ovl, ovl, now, p0, now, 1, false);
        juce::MouseEvent drag (src, p0.translated (0, -40), juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0, ovl, ovl, now, p0, now, 1, true);
        juce::MouseEvent up (src, p0.translated (0, -40), juce::ModifierKeys(), 1.0f, 0, 0, 0, 0, ovl, ovl, now, p0, now, 1, true);
        ovl->mouseDown (down); ovl->mouseDrag (drag); ovl->mouseUp (up);
        const float after = p->modr.list()[0].amount;
        CHECK (after > before + 0.15f && after <= 1.0f && std::abs (cut->getValue() - p->apvts.getRawParameterValue ("cutoff")->load()) < 1e-6, "dragging ring 1 up 40 px: amount %+.2f -> %+.2f; the knob's own value untouched", before, after);
        p->modr.setAmount (0, 0.5f);
    }
    pump (80);
    for (int t : { 0, 1, 2 })
    {
        static const char* tn[] { "black", "purple", "light" };
        pe->setTheme (t); pump (80);
        shot (*pe, juce::String ("mod-rings-synth-") + tn[t]);
        {   // the knob with two rings, 3x
            gpro::ModUi::KnobGeom kg; ui->knobGeom (*cut, kg);
            auto img = content->createComponentSnapshot (juce::Rectangle<int> (130, 90).withCentre (kg.c.toInt()).translated (0, 10), true, 3.0f);
            auto f = modShotDir.getChildFile (juce::String ("mod-two-rings-zoom-") + tn[t] + ".png"); f.deleteFile();
            if (auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream())) juce::PNGImageFormat().writeImageToStream (img, *os);
        }
    }
    pe->setTheme (0); pump (40);
    // ---- the knob menu
    {
        auto m = ui->buildKnobMenu ("cutoff");
        juce::StringArray items;
        std::function<void (const juce::PopupMenu&, int)> walk = [&] (const juce::PopupMenu& pm, int depth)
        {
            for (juce::PopupMenu::MenuItemIterator it (pm); it.next();)
            {
                const auto& item = it.getItem();
                if (depth == 0) items.add (item.text);
                if (item.subMenu != nullptr && depth == 0) walk (*item.subMenu, depth + 1);
            }
        };
        walk (m, 0);
        int missing = 0; juce::StringArray miss;
        for (auto* need : { "Type a Value...", "Mod Source", "Aux Source", "Edit Custom Curve", "Bypass Modulator", "Remove Modulator", "Remove All Modulators", "Reset Control", "MIDI Learn" })
            if (! items.contains (need)) { ++missing; miss.add (need); }
        CHECK (missing == 0, "knob menu (two routes on CUTOFF): %s%s", items.joinIntoString (" | ").toRawUTF8(), missing ? (" -- MISSING " + miss.joinIntoString (", ")).toRawUTF8() : "");
        // right-click goes to the menu for knobs, to the type-in box for anything else
        juce::Slider plain; plain.setSliderStyle (juce::Slider::LinearHorizontal);
        ui->menuParent = pe;          // (no desktop windows under Xvfb: the menu opens inside the editor)
        const bool knobMenu = ui->knobRightClick (*cut);
        juce::PopupMenu::dismissAllActiveMenus(); pump (40);
        CHECK (knobMenu && ! ui->knobRightClick (plain), "right-click: a knob opens the menu (%d), a slider outside the pages keeps the type-in box (%d)", (int) knobMenu, (int) ! ui->knobRightClick (plain));
        // run the items (ids as the menu builds them)
        auto idOf = [&] (const juce::String& text, const juce::String& sub = {}, const juce::String& subsub = {}) -> int
        {
            const auto menu = ui->buildKnobMenu ("cutoff");
            for (juce::PopupMenu::MenuItemIterator it (menu); it.next();)
            {
                const auto& item = it.getItem();
                if (item.text == text && item.itemID != 0) { if (sub.isEmpty()) return item.itemID; }
                if (item.text == text && item.subMenu != nullptr)
                    for (juce::PopupMenu::MenuItemIterator i2 (*item.subMenu); i2.next();)
                    {
                        if (i2.getItem().text == sub && i2.getItem().itemID != 0 && subsub.isEmpty()) return i2.getItem().itemID;
                        if (i2.getItem().text == sub && i2.getItem().subMenu != nullptr)
                            for (juce::PopupMenu::MenuItemIterator i3 (*i2.getItem().subMenu); i3.next();) if (i3.getItem().text == subsub) return i3.getItem().itemID;
                    }
            }
            return 0;
        };
        ui->runKnobMenu ("cutoff", idOf ("Aux Source", "LFO 1", "VELOCITY"), cut);
        const bool aux = p->modr.list()[0].aux == gpro::mod::vel;
        ui->runKnobMenu ("cutoff", idOf ("Bypass Modulator", "ENV 2"), cut);
        const bool byp = p->modr.list()[1].bypass;
        ui->menuParent = pe;
        const auto kidsBefore = pe->getChildren();
        ui->runKnobMenu ("cutoff", idOf ("Edit Custom Curve", "ENV 2"), cut);
        const bool custom = p->modr.list()[1].curve == gpro::mod::cCustom;
        juce::Component* curveEd = nullptr; for (auto* c : pe->getChildren()) if (! kidsBefore.contains (c)) curveEd = c;
        if (curveEd != nullptr) { shot (*curveEd, "mod-curve-editor-black"); delete curveEd; }
        ui->runKnobMenu ("cutoff", idOf ("MIDI Learn"), cut);
        const bool learning = p->modr.learning() == "cutoff";
        p->modr.forget ("cutoff");
        CHECK (aux && byp && custom && curveEd != nullptr && learning, "menu items: Aux Source > LFO 1 > VELOCITY (%d), Bypass > ENV 2 (%d), Edit Custom Curve > ENV 2 opens the editor (%d / %d), MIDI Learn waits for a CC (%d)",
               (int) aux, (int) byp, (int) custom, (int) (curveEd != nullptr), (int) learning);
        // the menu itself, inside the editor, in every look
        for (int t : { 0, 1, 2 })
        {
            static const char* tn[] { "black", "purple", "light" };
            pe->setTheme (t); pump (60);
            ui->showKnobMenu (*cut);            // (snapshot at once: without a foreground window the menu closes at its first timer tick)
            shot (*pe, juce::String ("mod-menu-") + tn[t]);
            juce::PopupMenu::dismissAllActiveMenus(); pump (80);
        }
        ui->menuParent = nullptr;
        pe->setTheme (0); pump (40);
        p->modr.setBypass (1, false);
        // Reset Control / Remove Modulator / Remove All
        p->setParam ("cutoff", 0.3f);
        ui->runKnobMenu ("cutoff", idOf ("Reset Control"), cut);
        const bool reset = std::abs (p->apvts.getRawParameterValue ("cutoff")->load() - 1.0f) < 1e-6f;
        const int n0 = p->modr.size();
        ui->runKnobMenu ("cutoff", idOf ("Remove Modulator", "ENV 2"), cut);
        const int n1 = p->modr.size();
        p->modr.add (gpro::mod::env2, "cutoff", -0.4f); p->modr.add (gpro::mod::lfo2, "res", 0.2f);
        ui->runKnobMenu ("cutoff", idOf ("Remove All Modulators"), cut);
        const int n2 = p->modr.size();
        CHECK (reset && n0 == 2 && n1 == 1 && n2 == 1, "Reset Control -> CUTOFF default (%d); Remove Modulator 2 -> 1 route(s) (%d); Remove All on CUTOFF leaves only RESONANCE's (%d)", (int) reset, n1, n2);
    }
    // ---- the ROUTES page: rows, sort, move a route to another knob
    {
        p->modr.clear();
        p->modr.add (gpro::mod::lfo1, "cutoff", 0.5f); p->modr.add (gpro::mod::env2, "cutoff", -0.4f); p->modr.add (gpro::mod::mac1, "verb", 0.8f);
        p->modr.add (gpro::mod::vel, "o1_pos", 0.2f); p->modr.add (gpro::mod::lfo3, "res", -0.6f);
        pe->showTab (1); pump (40);
        ui->openRoutes (true); pump (120);
        int rows = 0; std::vector<juce::Slider*> amts;
        std::function<void (juce::Component&)> walk = [&] (juce::Component& c) { for (auto* k : c.getChildren()) { if (auto* s = dynamic_cast<juce::Slider*> (k); s != nullptr && ! s->isRotary()) amts.push_back (s); walk (*k); } };
        walk (*ui->routes());
        rows = (int) amts.size();
        // an amount bar moves the route
        if (! amts.empty()) amts[0]->setValue (0.25, juce::sendNotificationSync);
        const bool barMoves = std::abs (p->modr.list()[0].amount - 0.25f) < 1e-6f;
        for (int t : { 0, 1, 2 })
        {
            static const char* tn[] { "black", "purple", "light" };
            pe->setTheme (t); pump (100);
            shot (*pe, juce::String ("mod-routes-") + tn[t]);
            int checked = 0; auto issues = contrastAudit (*pe, *ui->routes(), checked); int bad = 0;
            for (auto& i : issues) if (i.enabled) { ++bad; printf ("      unreadable: %s (%.2f:1)\n", i.what.toRawUTF8(), i.ratio); }
            CHECK (bad == 0, "ROUTES page %s: %d texts checked, %d unreadable", tn[t], checked, bad);
        }
        pe->setTheme (0); pump (40);
        CHECK (rows == 5 + 0 && barMoves, "ROUTES list: %d rows for 5 routes, an amount bar sets the route (%d)", rows, (int) barMoves);
        // move: drag route 3 (MACRO 1 > REVERB) onto the DRIVE knob (FX card on the SYNTH page) through the SYNTH tab
        ui->beginDrag (gpro::mod::mac1, 2, content->localPointToGlobal (juce::Point<int> (900, 400)));
        ui->dragMoveTo (content->localPointToGlobal (juce::Point<int> (60, 92)));
        const auto t0 = juce::Time::getMillisecondCounterHiRes(); while (juce::Time::getMillisecondCounterHiRes() - t0 < 600) pump (20);
        auto* drv = ui->sliderFor ("drive");
        if (drv != nullptr) { ui->dragMoveTo (screenCentre (*drv)); ui->dragEnd (screenCentre (*drv)); }
        const auto l = p->modr.list();
        CHECK (drv != nullptr && l.size() == 5 && l[2].dst == "drive" && std::abs (l[2].amount - 0.8f) < 1e-6f, "move modulation: MACRO 1 > REVERB dragged onto DRIVE -> %s (amount kept %+.2f)", l.size() > 2 ? l[2].dst.toRawUTF8() : "-", l.size() > 2 ? l[2].amount : 0.0f);
        pe->showTab (1); pump (60);
        shot (*pe, "mod-routes-after-move-black");
        ui->openRoutes (false); pump (60);
        for (int t : { 0, 2 })
        {
            pe->setTheme (t); pump (80);
            shot (*pe, juce::String ("mod-tab-") + (t == 0 ? "black" : "light"));
        }
        pe->setTheme (0);
    }
    // ---- hover a tile: its targets light up (painted by the overlay); seeThroughPixels of the MOD page stays 0
    {
        p->modr.clear(); p->modr.add (gpro::mod::lfo1, "cutoff", 0.4f); p->modr.add (gpro::mod::lfo1, "res", -0.3f);
        pe->showTab (0); pump (40);
        ui->setHoverSource (gpro::mod::lfo1); pump (60);
        shot (*pe, "mod-hover-lfo1-black");
        ui->setHoverSource (0);
        pe->showTab (1); pump (40);
        const int see = pe->seeThroughPixels();
        CHECK (see == 0, "MOD page with the tiles: %d see-through pixels inside its cards", see);
    }
}
} // namespace modtest
