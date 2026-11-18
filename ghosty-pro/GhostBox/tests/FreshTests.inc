// Ghost Box (2026-11-09): the size the window opens at, and a fresh start every time (Brandon's 2026-11-06 15:39 / 15:44 rules).
//   size   opens at about 1200 x 760 (never wider than 1240, never taller than 780), resizable, reopens at that size (not the last one),
//          and at the open size no two controls overlap and none sticks out of its parent, on every tab, extra page and effect page
//   fresh  two instances in one process: changing the first leaves the second at factory defaults; destroy + recreate = defaults
//   GhostBoxTests fresh [shot dir]

static bool freshIsControl (juce::Component* c)
{
    return dynamic_cast<juce::Button*> (c) != nullptr || dynamic_cast<juce::Slider*> (c) != nullptr || dynamic_cast<juce::ComboBox*> (c) != nullptr
        || dynamic_cast<juce::TextEditor*> (c) != nullptr || dynamic_cast<juce::Label*> (c) != nullptr || dynamic_cast<ghosty::tempomul::Switch*> (c) != nullptr;
}
// every visible control: no sibling control overlaps it, and it is inside its parent (1 px slack). Returns the problems found.
static int freshOverlaps (juce::Component& root, juce::StringArray& why)
{
    int bad = 0;
    std::function<void (juce::Component&)> walk = [&] (juce::Component& parent)
    {
        std::vector<juce::Component*> cs;
        for (auto* c : parent.getChildren())
        {
            if (! c->isVisible() || c->getBounds().isEmpty()) continue;
            if (freshIsControl (c))
            {
                cs.push_back (c);
                if (! parent.getLocalBounds().expanded (1).contains (c->getBounds()))
                { ++bad; if (why.size() < 12) why.add ("outside " + parent.getName() + ": " + c->getName() + " " + c->getBounds().toString() + " in " + parent.getLocalBounds().toString()); }
            }
            walk (*c);
        }
        for (size_t i = 0; i < cs.size(); ++i)
            for (size_t j = i + 1; j < cs.size(); ++j)
                if (cs[i]->getBounds().intersects (cs[j]->getBounds()))
                {
                    ++bad;
                    if (why.size() < 12)
                    {
                        auto nm = [] (juce::Component* c) { if (auto* b = dynamic_cast<juce::Button*> (c)) if (b->getButtonText().isNotEmpty()) return b->getButtonText(); return c->getName().isNotEmpty() ? c->getName() : juce::String (typeid (*c).name()); };
                        why.add (nm (cs[i]) + " " + cs[i]->getBounds().toString() + " / " + nm (cs[j]) + " " + cs[j]->getBounds().toString());
                    }
                }
    };
    walk (root);
    return bad;
}
static juce::String boxStateOf (BoxProcessor& p) { juce::MemoryBlock mb; p.getStateInformation (mb); return mb.toBase64Encoding(); }

static void testFreshAndSize (const juce::File& shotDir)
{
    std::printf ("\n[41] SIZE: opens at about 1200 x 760 (at most 1240 x 780), resizable, nothing overlapping on any page\n");
    {
        BoxProcessor p;
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        pumpLoop (60);
        const int w = ed->getWidth(), h = ed->getHeight();
        std::printf ("    opens at %d x %d\n", w, h);
        check (w <= 1240 && h <= 780 && w >= 1100 && h >= 720, "opens at about 1200 x 760, never bigger than 1240 x 780");
        check (ed->isResizable(), "resizable");
        Panel* panel = nullptr; for (auto* c : ed->getChildren()) if (auto* pp = dynamic_cast<Panel*> (c)) panel = pp;
        check (panel != nullptr, "the rack panel is there");
        int pages = 0, badTotal = 0;
        auto look = [&] (const juce::String& what)
        {
            pumpLoop (30);
            juce::StringArray why; const int bad = freshOverlaps (*ed, why);
            ++pages; badTotal += bad;
            if (bad > 0) { std::printf ("    %s: %d problems\n", what.toRawUTF8(), bad); for (auto& s : why) std::printf ("      %s\n", s.toRawUTF8()); }
            if (shotDir != juce::File() && (what.startsWith ("tab") || what == "unit 0"))
            {
                shotDir.createDirectory();
                const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
                juce::FileOutputStream os (shotDir.getChildFile ("ghostbox-" + what.replaceCharacter (' ', '-') + ".png"));
                if (os.openedOk()) { os.setPosition (0); os.truncate(); juce::PNGImageFormat().writeImageToStream (img, os); }
            }
        };
        if (panel != nullptr)
        {
            const int nModes = 6 + (int) p.rack.impl->extraPages.size();
            for (int m = 0; m < nModes; ++m) { panel->getProperties().set ("mode", m); panel->resized(); look ("tab " + juce::String (m)); }
            panel->getProperties().set ("mode", 0); panel->resized();
            for (int u = 0; u < gpfx::kNumUnits; ++u) { panel->getProperties().set ("select", u); panel->resized(); pumpLoop (5); panel->resized(); look ("unit " + juce::String (u)); }
            panel->getProperties().set ("select", 0); panel->resized();
        }
        std::printf ("    %d pages checked at %d x %d: %d overlapping / cut-off controls\n", pages, w, h, badTotal);
        check (pages >= 30 && badTotal == 0, "no control overlaps another or sticks out of its box at the open size");
        for (auto sz : { juce::Point<int> (1240, 780), juce::Point<int> (1600, 1000) })
        {   // bigger sizes: informational
            ed->setSize (sz.x, sz.y); pumpLoop (30); juce::StringArray why; const int bad = freshOverlaps (*ed, why);
            std::printf ("    (info) first page at %d x %d: %d problems%s\n", sz.x, sz.y, bad, bad > 0 ? (" e.g. " + why[0]).toRawUTF8() : "");
        }
        ed->setSize (1700, 1100); pumpLoop (10); ed.reset();
        std::unique_ptr<juce::AudioProcessorEditor> e2 (p.createEditor()); pumpLoop (10);
        check (e2->getWidth() == w && e2->getHeight() == h, "reopens at the fitting size, not the last size (" + juce::String (e2->getWidth()) + " x " + juce::String (e2->getHeight()) + ")");
    }

    std::printf ("\n[42] FRESH START: a new Ghost Box is always brand new (nothing carries over from another instance or project)\n");
    juce::String factory; { BoxProcessor f; factory = boxStateOf (f); }
    { BoxProcessor f2; check (boxStateOf (f2) == factory, "two fresh instances save exactly the same state"); }
    {
        BoxProcessor a, b;
        const auto bStart = boxStateOf (b);
        // change everything the first one can remember
        a.rack.applyScene (a.apvts, "galaxyecho");
        if (auto* q = a.apvts.getParameter ("box_mix")) q->setValueNotifyingHost (0.3f);
        if (auto* q = a.apvts.getParameter ("box_in")) q->setValueNotifyingHost (0.8f);
        a.tempoMul.store (ghosty::tempomul::Double); a.uiTheme.store (2);
        a.sceneStore (0); a.sceneRename (0, "MINE"); a.captureBarsIdx = 3; a.dnaSeed = 99; a.dnaStrength = 0.9f; a.abToggle(); a.abCopyToOther();
        {   // and its window: another look, another size, another page
            std::unique_ptr<juce::AudioProcessorEditor> ea (a.createEditor()); pumpLoop (20); ea->setSize (1500, 1000);
            for (auto* c : ea->getChildren()) if (auto* pp = dynamic_cast<Panel*> (c)) { pp->getProperties().set ("mode", 4); pp->resized(); }
            if (auto* be = dynamic_cast<BoxEditor*> (ea.get())) be->setLook (BoxEditor::kLookGhost);
            pumpLoop (20);
        }
        check (boxStateOf (a) != factory, "the first instance has a new sound, scene, tempo and settings");
        check (bStart == factory && boxStateOf (b) == factory, "the second, open at the same time, is still at factory defaults");
        check (b.tempoMul.load() == ghosty::tempomul::Normal && b.uiTheme.load() == 0 && b.captureBarsIdx == 1 && b.dnaSeed == 1 && ! b.sceneBank.slots[0].filled() && b.abCurrent() == 0 && ! b.abHas (1),
               "the second has 1x, the BLACK look, 4 bars, DNA seed 1, empty scene slots, no A/B");
        {   // its window opens at the fitting size on the first page in the default look
            std::unique_ptr<juce::AudioProcessorEditor> eb (b.createEditor()); pumpLoop (20);
            auto* be = dynamic_cast<BoxEditor*> (eb.get());
            check (eb->getWidth() == BoxEditor::kOpenW && eb->getHeight() == BoxEditor::kOpenH && be != nullptr && be->look() == 0, "the second's window: the open size and the BLACK look");
        }
        BoxProcessor c;
        check (boxStateOf (c) == factory, "a third, made after the changes, is at factory defaults");
    }
    {   // destroy + recreate
        auto a = std::make_unique<BoxProcessor>(); a->rack.applyScene (a->apvts, "tapedrop"); a->tempoMul.store (ghosty::tempomul::Half); a->sceneStore (2);
        a.reset();
        BoxProcessor again; check (boxStateOf (again) == factory, "destroy + recreate = factory defaults");
    }
    {   // a project's own saved state still comes back (the host hands it over)
        BoxProcessor a; a.rack.applyScene (a.apvts, "galaxyecho"); a.tempoMul.store (ghosty::tempomul::Double); a.sceneStore (1);
        juce::MemoryBlock mb; a.getStateInformation (mb);
        BoxProcessor b; b.setStateInformation (mb.getData(), (int) mb.getSize());
        check (boxStateOf (b) == boxStateOf (a), "a saved project restores exactly (scenes, tempo, sound)");
    }
}
