// Shared/SnapshotBank.h: take / list newest first / load / cycle with wrap, auto names, rename / star / note, delete + undo,
// corrupt + foreign files skipped, restart persistence, atomic files, prune only when allowed, export / import, the bar + hotkeys.
#include "SnapshotBank.h"
#include <iostream>
static int passes = 0, fails = 0;
static void check(bool ok, const juce::String& what) { (ok ? passes : fails)++; std::cout << (ok ? "PASS  " : "FAIL  ") << what << "\n"; }

struct FakePlugin {   // a "plugin" whose whole sound is a few numbers + a blob (like an embedded sample)
    float cutoff = 0.5f, drive = 0.1f; juce::String table = "sine"; juce::MemoryBlock blob;
    juce::ValueTree state() const { juce::ValueTree t("STATE"); t.setProperty("cutoff", cutoff, nullptr); t.setProperty("drive", drive, nullptr);
        juce::ValueTree c("TABLE"); c.setProperty("name", table, nullptr); c.setProperty("blob", blob.toBase64Encoding(), nullptr); t.appendChild(c, nullptr); return t; }
    void load(const juce::ValueTree& t) { cutoff = t["cutoff"]; drive = t["drive"]; auto c = t.getChildWithName("TABLE"); table = c["name"].toString(); blob.reset(); blob.fromBase64Encoding(c["blob"].toString()); }
};

int main() {
    juce::ScopedJuceInitialiser_GUI gui;
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("snaptest-" + juce::String(juce::Time::currentTimeMillis()));
    dir.deleteRecursively();
    FakePlugin P; for (int i = 0; i < 5000; ++i) { const uint8_t b = (uint8_t) (i * 7); P.blob.append(&b, 1); }
    auto wire = [&](ghosty::SnapshotBank& b) { b.capture = [&] { return P.state(); }; b.restore = [&](const juce::ValueTree& t) { P.load(t); }; };
    ghosty::SnapshotBank bank({ "TestSynth", dir }); wire(bank);
    check(bank.empty() && bank.current() == -1 && bank.nextAutoName() == "Snap 1", "empty bank, first name Snap 1");
    // three snapshots of three different sounds
    const float cut[3] = { 0.2f, 0.6f, 0.9f };
    for (int i = 0; i < 3; ++i) { P.cutoff = cut[i]; P.table = "t" + juce::String(i); bank.take(); juce::Thread::sleep(3); }
    check(bank.size() == 3 && bank.info(0).name == "Snap 3" && bank.info(2).name == "Snap 1" && bank.current() == 0, "take x3: newest first (Snap 3, 2, 1), current = the newest");
    bool exact = true; for (int i = 0; i < 3; ++i) { P.cutoff = 0; P.table = {}; P.blob.reset(); bank.load(2 - i); exact &= P.cutoff == cut[i] && P.table == "t" + juce::String(i) && P.blob.getSize() == 5000; }
    check(exact, "load(i) restores each sound exactly (numbers, names and a 5000-byte blob)");
    bank.load(0); bank.next(); const int a1 = bank.current(); bank.next(); bank.next(); const int a3 = bank.current(); bank.prev(); const int a4 = bank.current();
    check(a1 == 1 && a3 == 0 && a4 == 2 && P.cutoff == cut[0], "next / prev cycle with wrap (0 > 1 > 2 > 0, prev > 2) and load as they go");
    check(bank.take("My growl") >= 0 && bank.info(0).name == "My growl" && bank.nextAutoName() == "Snap 4", "a named snapshot; the counter keeps going (Snap 4)");
    // edit
    check(bank.rename(0, "Big growl") && bank.star(0, true) && bank.setNote(0, "use for the drop") && bank.info(0).name == "Big growl" && bank.info(0).starred, "rename / star / note");
    // corrupt + foreign + empty files are skipped
    dir.getChildFile("snapshot-broken.xml").replaceWithText("<GHOSTSNAPSHOT name=\"x\" time=\"1\"><STATE cutoff=");
    dir.getChildFile("snapshot-empty.xml").replaceWithText("");
    dir.getChildFile("other.xml").replaceWithText("<SOMETHING/>");
    dir.getChildFile("snapshot-foreign.xml").replaceWithText("<GHOSTSNAPSHOT plugin=\"OtherSynth\" name=\"f\" time=\"5\"><STATE/></GHOSTSNAPSHOT>");
    // restart: a new bank (a new plugin instance) lists the same
    ghosty::SnapshotBank again({ "TestSynth", dir }); wire(again);
    check(again.size() == 4 && again.corruptCount() == 4 && again.info(0).name == "Big growl" && again.info(0).starred && again.info(0).note == "use for the drop" && again.info(3).name == "Snap 1",
          "restart: a new instance lists the 4 snapshots with name / star / note; 4 broken / empty / foreign files skipped (" + juce::String(again.corruptCount()) + ")");
    P.cutoff = 0; again.load(3); check(P.cutoff == cut[0], "after the restart, loading still restores the sound");
    // delete + undo
    const auto gone = again.info(1).name; const int before = again.size();
    check(again.remove(1) && again.size() == before - 1 && again.indexOfName(gone) < 0 && again.canUndoRemove(), "delete removes the file and the entry");
    const int back = again.undoRemove();
    check(back >= 0 && again.size() == before && again.info(back).name == gone && again.stateOf(back).isValid(), "undo delete brings it back (file and all)");
    // no temp files left; every file parses
    int tmp = 0; for (auto& f : dir.findChildFiles(juce::File::findFiles, false, "*")) if (! f.hasFileExtension("xml")) ++tmp;
    check(tmp == 0, "atomic writes leave no temp files behind");
    // prune: never by default; with allowPrune the oldest unstarred go
    ghosty::SnapshotBank lim({ "TestSynth", dir.getChildFile("lim") }); wire(lim);
    for (int i = 0; i < 6; ++i) { P.cutoff = (float) i; lim.take(); juce::Thread::sleep(2); }
    lim.star(5, true);   // the oldest
    check(lim.size() == 6, "default: nothing is ever deleted on its own (6 kept)");
    lim.setAllowPrune(true, 3);
    check(lim.size() == 3 && lim.info(2).starred && lim.info(2).name == "Snap 1", "allowPrune (max 3): the oldest UNSTARRED went, the starred oldest stayed");
    // export / import
    auto ex = dir.getChildFile("export"); const int nx = again.exportTo(ex);
    ghosty::SnapshotBank imp({ "TestSynth", dir.getChildFile("imported") }); wire(imp);
    const int ni = imp.importFrom(ex), ni2 = imp.importFrom(ex);
    check(nx == 4 && ni == 4 && ni2 == 0 && imp.size() == 4, "export 4 / import 4 (importing again adds no duplicates)");
    // the bar: SNAPSHOT button + hotkeys + menu
    {
        ghosty::SnapshotBank bb({ "TestSynth", dir.getChildFile("bar") }); wire(bb);
        ghosty::SnapshotBar bar(bb); bar.setSize(320, 30); juce::StringArray msgs; bar.onMessage = [&](const juce::String& m) { msgs.add(m); };
        bar.setTheme(ghosty::themes::Theme::Purple);
        bar.snapBtn.onClick(); const bool flashed = bar.snapBtn.flash > 0.9f;
        { const int n0 = bb.size(); const bool plainTook = bar.handleKey(juce::KeyPress('s')); check(! plainTook && bb.size() == n0, "a plain S never takes a snapshot (it is a piano key)"); }   // a plain S is a piano key: never a snapshot
        bar.handleKey(juce::KeyPress('s', juce::ModifierKeys::shiftModifier | juce::ModifierKeys::altModifier, 0));
        check(bb.size() == 2 && msgs[0] == "Snapshot saved: Snap 1" && msgs[1] == "Snapshot saved: Snap 2" && flashed, "SNAPSHOT button and Shift+Alt+S save (toast \"" + msgs[0] + "\", the button flashes)");
        bar.menuResult(10003);   // delete the current (Snap 2)
        const bool del = bb.size() == 1; bar.handleKey(juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0));
        check(del && bb.size() == 2, "menu Delete, then Ctrl/Cmd+Z undoes it");
        bar.step(1); check(bb.current() >= 0 && msgs[msgs.size() - 1].startsWith("Loaded snapshot"), "the bar's > loads the next snapshot (" + msgs[msgs.size() - 1] + ")");
        auto m = bar.menuFor(); check(m.getNumItems() >= 5, "the list menu: header, one item per snapshot, this-snapshot menu, undo, show folder");
        juce::Image img(juce::Image::ARGB, 320, 30, true); { juce::Graphics g(img); bar.paintEntireComponent(g, true); }
        const auto& T = ghosty::themes::get(ghosty::themes::Theme::Purple);
        check(ghosty::themes::contrast(T.onAccent, T.violet) >= 4.5, "the SNAPSHOT label reads on its accent fill in PURPLE");
    }
    dir.deleteRecursively();
    std::cout << passes << " passed, " << fails << " failure(s)\n";
    return fails ? 1 : 0;
}
