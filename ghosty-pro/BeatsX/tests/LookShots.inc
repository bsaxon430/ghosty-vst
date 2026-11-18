// BeatsTests lookshots <dir> [ghost]: deterministic editor shots (no message loop between shots, so the header ghosts never move) of every page in
// BLACK / PURPLE / LIGHT (+ GHOST with "ghost"), compact + LARGE 1600 + FOCUS. Uses only long-standing test hooks, so the same file builds against
// an older checkout: shots from two builds are compared pixel by pixel (the old looks must stay identical).
static void writePng(const juce::Image& img, const juce::File& f) { f.deleteFile(); juce::FileOutputStream o(f); if (o.openedOk()) { juce::PNGImageFormat png; png.writeImageToStream(img, o); } }
static void lookShots(const juce::File& dir, bool withGhost, bool onlyNeon = false, bool two = false) {   // two: NEON + LIGHT (the two looks left, 2026-11-13)
    dir.createDirectory();
    BeatsXProcessor p; p.applyKit(10, true); p.focusMode = false;
    std::unique_ptr<juce::AudioProcessorEditor> e(p.createEditor()); auto* be = dynamic_cast<BeatsXEditor*>(e.get()); e->setSize(1180, 790);
    const char* pages[] { "grid", "sounds", "process", "tracker", "lab", "pads", "song", "scripts", "mix", "melody" };
    const char* names[] { "black", "purple", "light", "ghost", "neon" };
    int n = 0;
    std::vector<int> looks; if (two) looks = { 4, 2 }; else for (int look = onlyNeon ? 4 : 0; look < (onlyNeon ? 5 : withGhost ? 4 : 3); ++look) looks.push_back(look);
    for (int look : looks) {
        be->setThemeForTest(look);
        for (int pg = 0; pg < 10; ++pg) { be->showPageForTest(pg); writePng(e->createComponentSnapshot(e->getLocalBounds(), true, 1.0f), dir.getChildFile(juce::String("look-") + names[look] + "-" + pages[pg] + ".png")); ++n; }
        p.focusMode = true; be->showPageForTest(0); writePng(e->createComponentSnapshot(e->getLocalBounds(), true, 1.0f), dir.getChildFile(juce::String("look-") + names[look] + "-focus.png")); p.focusMode = false; be->showPageForTest(0); ++n;
        e->setSize(1600, 1000); for (int pg : { 0, 4, 8 }) { be->showPageForTest(pg); writePng(e->createComponentSnapshot(e->getLocalBounds(), true, 1.0f), dir.getChildFile(juce::String("look-") + names[look] + "-large-" + pages[pg] + ".png")); ++n; }
        e->setSize(1180, 790); be->showPageForTest(0);
    }
    be->setThemeForTest(0);
    std::printf("lookshots: %d shots in %s\n", n, dir.getFullPathName().toRawUTF8());
}
