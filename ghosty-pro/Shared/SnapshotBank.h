#pragma once
// SNAPSHOT: one click saves the sound you are tweaking as a preset in a bank called SNAPSHOTS; cycle through them later.
// Header-only, shared by every GhostBlock plugin. Message thread only (file I/O happens on the click, like a preset save).
//
//   ghosty::SnapshotBank bank ({ "Poltergeist" });                 // folder: <user app data>/GhostBlock/Poltergeist/Snapshots/
//   bank.capture = [&] { return myState(); };                       // EVERYTHING that defines the sound (params, samples, tables ...)
//   bank.restore = [&] (const juce::ValueTree& s) { myPresetLoad (s); };   // the plugin's normal preset-load path
//   bank.take();            // "Snap 3" saved, newest first in list()
//   bank.next(); bank.prev(); bank.load (i);
//
// Files: one per snapshot, snapshot-YYYYMMDD-HHMMSS-mmm.xml:
//   <GHOSTSNAPSHOT version="1" plugin=".." name="Snap 3" time="<ms since 1970>" star="0" note=".."> <the captured tree/> </GHOSTSNAPSHOT>
// Written atomically (temp file + rename). Unreadable / foreign files are skipped (corruptCount()). Nothing is ever deleted unless
// the user deletes it, or Options::allowPrune is on (then the oldest UNSTARRED beyond maxSnapshots go). A delete can be undone.
//
// UI: SnapshotBar = [SNAPSHOT] (+ [v] list) and, unless compact, [<] [name] [>]. Themed with Shared/GhostThemes.h (setTheme()).
//   The SNAPSHOT button flashes and reads "SAVED" for a moment; onMessage gets "Snapshot saved: Snap 3" for the host's toast.
//   handleKey(): Shift+Alt+S = take (when no text box has focus; plain S is a piano key in hosts), Ctrl/Cmd+Z = undo the last snapshot delete (only within 60 s of it).
#include <juce_gui_basics/juce_gui_basics.h>
#include "GhostThemes.h"
#include <vector>
#include <functional>

namespace ghosty {

struct SnapshotInfo {
    juce::File file;
    juce::String name, note;
    juce::int64 timeMs = 0;          // when it was taken (ms since 1970)
    bool starred = false;
};

class SnapshotBank {
public:
    struct Options {
        juce::String plugin;                 // folder + file tag, e.g. "Poltergeist"
        juce::File folder {};                // empty = defaultFolder (plugin)
        int maxSnapshots = 500;              // only used when allowPrune
        bool allowPrune = false;             // default: never delete anything on its own
        bool timeNames = false;              // auto names: false "Snap 3" (counter), true "Snap 14:32:07"
        juce::String company = "GhostBlock";
    };
    std::function<juce::ValueTree()> capture;                   // the plugin's whole sound
    std::function<void (const juce::ValueTree&)> restore;       // load it (the normal preset-load path)
    std::function<void()> onChanged;                            // the list / current changed
    std::function<void (const SnapshotInfo&)> onRestored;       // after restore() of a snapshot (e.g. show its name as the preset name)

    explicit SnapshotBank (Options o) : opt (std::move (o)) { rescan(); }

    static juce::File defaultFolder (const juce::String& plugin, const juce::String& company = "GhostBlock") {
        return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile (company).getChildFile (plugin).getChildFile ("Snapshots");
    }
    juce::File folder() const { return opt.folder != juce::File() ? opt.folder : defaultFolder (opt.plugin, opt.company); }
    void setFolder (const juce::File& f) { opt.folder = f; current_ = -1; rescan(); }
    const Options& options() const { return opt; }
    void setAllowPrune (bool on, int maxN = 500) { opt.allowPrune = on; opt.maxSnapshots = juce::jmax (1, maxN); prune(); }

    // ---- the list (newest first)
    void rescan() {
        const auto keepFile = juce::isPositiveAndBelow (current_, size()) ? items[(size_t) current_].file : juce::File();
        items.clear(); corrupt = 0;
        for (auto& f : folder().findChildFiles (juce::File::findFiles, false, "*.xml")) {
            SnapshotInfo in; if (readInfo (f, in)) items.push_back (in); else ++corrupt;
        }
        sortItems();
        current_ = indexOfFile (keepFile);
    }
    int size() const { return (int) items.size(); }
    bool empty() const { return items.empty(); }
    const std::vector<SnapshotInfo>& list() const { return items; }
    const SnapshotInfo& info (int i) const { return items[(size_t) i]; }
    int current() const { return current_; }                       // the snapshot last taken / loaded, -1 none
    void clearCurrent() { if (current_ != -1) { current_ = -1; changed(); } }   // (the host loaded something else)
    int corruptCount() const { return corrupt; }
    int indexOfFile (const juce::File& f) const { for (int i = 0; i < size(); ++i) if (items[(size_t) i].file == f) return i; return -1; }
    int indexOfName (const juce::String& n) const { for (int i = 0; i < size(); ++i) if (items[(size_t) i].name == n) return i; return -1; }

    juce::String nextAutoName() const {
        if (opt.timeNames) return "Snap " + juce::Time::getCurrentTime().formatted ("%H:%M:%S");
        int hi = 0;
        for (auto& s : items) if (s.name.startsWith ("Snap ") && s.name.substring (5).trim().containsOnly ("0123456789")) hi = juce::jmax (hi, s.name.substring (5).trim().getIntValue());
        return "Snap " + juce::String (hi + 1);
    }

    // ---- take / load / cycle
    // saves capture() as a new snapshot; returns its index (0 = newest) or -1 (no capture / write failed)
    int take (const juce::String& name = {}) {
        if (! capture) return -1;
        auto state = capture(); if (! state.isValid()) return -1;
        SnapshotInfo in; in.name = name.trim().isNotEmpty() ? name.trim() : nextAutoName(); in.timeMs = juce::Time::currentTimeMillis();
        const auto dir = folder(); if (! dir.createDirectory()) return -1;
        auto f = dir.getChildFile ("snapshot-" + juce::Time (in.timeMs).formatted ("%Y%m%d-%H%M%S") + "-" + juce::String (in.timeMs % 1000).paddedLeft ('0', 3) + ".xml");
        if (f.exists()) f = dir.getNonexistentChildFile (f.getFileNameWithoutExtension(), ".xml", false);
        in.file = f;
        juce::ValueTree root ("GHOSTSNAPSHOT"); writeMeta (root, in); root.appendChild (state.createCopy(), nullptr);
        if (! writeAtomic (f, root)) return -1;
        items.push_back (in); sortItems(); current_ = indexOfFile (f);
        prune(); current_ = indexOfFile (f);
        changed();
        return current_;
    }
    juce::ValueTree stateOf (int i) const {   // the captured tree of snapshot i (read from its file), invalid if unreadable
        if (! juce::isPositiveAndBelow (i, size())) return {};
        auto root = readRoot (items[(size_t) i].file);
        return root.isValid() && root.getNumChildren() > 0 ? root.getChild (0).createCopy() : juce::ValueTree();
    }
    bool load (int i) {
        auto st = stateOf (i); if (! st.isValid() || ! restore) return false;
        restore (st); current_ = i; if (onRestored) onRestored (items[(size_t) i]); changed(); return true;
    }
    bool next() { return empty() ? false : load (current_ < 0 ? 0 : (current_ + 1) % size()); }            // towards older, wraps
    bool prev() { return empty() ? false : load (current_ < 0 ? 0 : (current_ - 1 + size()) % size()); }   // towards newer, wraps

    // ---- edit
    bool rename (int i, const juce::String& n) { return edit (i, [&] (SnapshotInfo& in) { in.name = n.trim().isNotEmpty() ? n.trim() : in.name; }); }
    bool star (int i, bool on) { return edit (i, [&] (SnapshotInfo& in) { in.starred = on; }); }
    bool setNote (int i, const juce::String& n) { return edit (i, [&] (SnapshotInfo& in) { in.note = n; }); }
    bool remove (int i) {
        if (! juce::isPositiveAndBelow (i, size())) return false;
        const auto in = items[(size_t) i]; const auto text = in.file.loadFileAsString();
        if (! in.file.deleteFile()) return false;
        trash.push_back ({ in, text, juce::Time::getMillisecondCounter() }); if (trash.size() > 32) trash.erase (trash.begin());
        items.erase (items.begin() + i);
        if (current_ == i) current_ = -1; else if (current_ > i) --current_;
        changed(); return true;
    }
    bool canUndoRemove() const { return ! trash.empty(); }
    juce::uint32 msSinceLastRemove() const { return trash.empty() ? 0xffffffffu : juce::Time::getMillisecondCounter() - trash.back().when; }
    int undoRemove() {   // brings the last deleted snapshot back; returns its index or -1
        if (trash.empty()) return -1;
        auto t = trash.back(); trash.pop_back();
        auto f = t.info.file; if (f.exists()) f = f.getParentDirectory().getNonexistentChildFile (f.getFileNameWithoutExtension(), ".xml", false);
        folder().createDirectory();
        juce::TemporaryFile tmp (f);
        if (! tmp.getFile().replaceWithText (t.text) || ! tmp.overwriteTargetFileWithTemporary()) return -1;
        t.info.file = f; items.push_back (t.info); sortItems(); changed();
        return indexOfFile (f);
    }

    // ---- move between machines
    int exportTo (const juce::File& dir) const {
        if (! dir.createDirectory()) return 0; int n = 0;
        for (auto& s : items) n += s.file.copyFileTo (dir.getChildFile (s.file.getFileName())) ? 1 : 0;
        return n;
    }
    int importFrom (const juce::File& dir) {
        int n = 0; folder().createDirectory();
        for (auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.xml")) {
            SnapshotInfo in; if (! readInfo (f, in)) continue;
            bool dup = false; for (auto& s : items) if (s.timeMs == in.timeMs && s.name == in.name) dup = true;
            if (dup) continue;
            auto dst = folder().getChildFile (f.getFileName()); if (dst.exists()) dst = folder().getNonexistentChildFile (f.getFileNameWithoutExtension(), ".xml", false);
            if (f.copyFileTo (dst)) { in.file = dst; items.push_back (in); ++n; }
        }
        if (n) { sortItems(); prune(); changed(); }
        return n;
    }

    static bool isTypingIntoText() {   // a text box has the keyboard: don't steal S
        auto* c = juce::Component::getCurrentlyFocusedComponent();
        return c != nullptr && (dynamic_cast<juce::TextEditor*> (c) != nullptr || c->findParentComponentOfClass<juce::TextEditor>() != nullptr);
    }

private:
    struct Trashed { SnapshotInfo info; juce::String text; juce::uint32 when; };
    Options opt; std::vector<SnapshotInfo> items; std::vector<Trashed> trash; int current_ = -1, corrupt = 0;

    void changed() { if (onChanged) onChanged(); }
    void sortItems() { std::stable_sort (items.begin(), items.end(), [] (const SnapshotInfo& a, const SnapshotInfo& b) { return a.timeMs != b.timeMs ? a.timeMs > b.timeMs : a.file.getFileName() > b.file.getFileName(); }); }
    static void writeMeta (juce::ValueTree& root, const SnapshotInfo& in) {
        root.setProperty ("version", 1, nullptr); root.setProperty ("name", in.name, nullptr); root.setProperty ("time", juce::String (in.timeMs), nullptr);
        root.setProperty ("star", in.starred ? 1 : 0, nullptr); root.setProperty ("note", in.note, nullptr);
    }
    bool writeAtomic (const juce::File& f, juce::ValueTree& root) const {
        root.setProperty ("plugin", opt.plugin, nullptr);
        auto xml = root.createXml(); if (! xml) return false;
        juce::TemporaryFile tmp (f);
        return xml->writeTo (tmp.getFile()) && tmp.overwriteTargetFileWithTemporary();
    }
    static juce::ValueTree readRoot (const juce::File& f) {
        if (f.getSize() <= 0) return {};
        auto xml = juce::XmlDocument::parse (f); if (! xml || ! xml->hasTagName ("GHOSTSNAPSHOT")) return {};
        return juce::ValueTree::fromXml (*xml);
    }
    bool readInfo (const juce::File& f, SnapshotInfo& in) const {
        // the whole file must parse (truncated / damaged / foreign files are skipped)
        auto xml = juce::XmlDocument::parse (f); if (! xml || ! xml->hasTagName ("GHOSTSNAPSHOT") || xml->getNumChildElements() < 1) return false;
        if (xml->getStringAttribute ("plugin").isNotEmpty() && opt.plugin.isNotEmpty() && xml->getStringAttribute ("plugin") != opt.plugin) return false;
        in.file = f; in.name = xml->getStringAttribute ("name", f.getFileNameWithoutExtension()); in.note = xml->getStringAttribute ("note");
        in.timeMs = xml->getStringAttribute ("time").getLargeIntValue(); if (in.timeMs <= 0) in.timeMs = f.getLastModificationTime().toMilliseconds();
        in.starred = xml->getIntAttribute ("star") != 0;
        return true;
    }
    bool edit (int i, const std::function<void (SnapshotInfo&)>& fn) {
        if (! juce::isPositiveAndBelow (i, size())) return false;
        auto root = readRoot (items[(size_t) i].file); if (! root.isValid()) return false;
        auto in = items[(size_t) i]; fn (in); writeMeta (root, in);
        if (! writeAtomic (in.file, root)) return false;
        items[(size_t) i] = in; changed(); return true;
    }
    void prune() {
        if (! opt.allowPrune) return;
        while (size() > opt.maxSnapshots) {
            int victim = -1; for (int i = size(); --i >= 0;) if (! items[(size_t) i].starred && i != current_) { victim = i; break; }
            if (victim < 0) break;
            items[(size_t) victim].file.deleteFile(); items.erase (items.begin() + victim); if (current_ > victim) --current_;
        }
    }
};

// ---------------------------------------------------------------------------------------------------------
// [SNAPSHOT] [v]   (+ [<] [name] [>] unless compact). Buttons inherit the host's LookAndFeel; the SNAPSHOT button is painted
// with the theme's accent and flashes when a snapshot is saved.
class SnapshotBar : public juce::Component, private juce::Timer {
public:
    std::function<void (const juce::String&)> onMessage;    // toasts: "Snapshot saved: Snap 3", "Loaded snapshot: Snap 2" ...
    std::function<void()> onLoaded;                         // after a snapshot was loaded from the bar (refresh the host UI)

    explicit SnapshotBar (SnapshotBank& b, bool compactLayout = false) : bank (b), compact (compactLayout) {
        snapBtn.onClick = [this] { takeNow(); };
        snapBtn.setTooltip ("SNAPSHOT (Shift+Alt+S): save the sound right now into the SNAPSHOTS bank. Keep tweaking, hit it again");
        listBtn.setButtonText (juce::String::fromUTF8 ("\xe2\x96\xbe")); listBtn.setTooltip ("Your snapshots: load, star, rename, delete, show the folder");
        listBtn.onClick = [this] { showList(); };
        prevBtn.onClick = [this] { step (-1); }; nextBtn.onClick = [this] { step (1); };
        prevBtn.setTooltip ("Previous snapshot (newer)"); nextBtn.setTooltip ("Next snapshot (older)");
        nameBtn.onClick = [this] { showList(); }; nameBtn.setTooltip ("Your snapshots");
        addAndMakeVisible (snapBtn); addAndMakeVisible (listBtn);
        for (auto* c : { &prevBtn, &nameBtn, &nextBtn }) { addChildComponent (*c); c->setVisible (! compact); }
        refresh();
    }
    ~SnapshotBar() override { stopTimer(); }

    void setTheme (ghosty::themes::Theme t) { theme = t; snapBtn.theme = t; repaint(); snapBtn.repaint(); }
    void setCompact (bool c) { compact = c; for (auto* b : { &prevBtn, &nameBtn, &nextBtn }) b->setVisible (! c); resized(); }

    int takeNow() {
        const int i = bank.take();
        if (i < 0) { message ("Couldn't save the snapshot (is the folder writable?)"); return -1; }
        snapBtn.flash = 1.0f; startTimerHz (60); refresh();
        message ("Snapshot saved: " + bank.info (i).name);
        return i;
    }
    bool step (int dir) {
        if (bank.empty()) { message ("No snapshots yet: press SNAPSHOT (or Shift+Alt+S) while you tweak"); return false; }
        const bool ok = dir > 0 ? bank.next() : bank.prev();
        if (ok) loaded();
        return ok;
    }
    bool loadIndex (int i) { if (! bank.load (i)) return false; loaded(); return true; }
    // keys while the plugin window has focus: S = SNAPSHOT (not while typing), Ctrl/Cmd+Z = undo the last snapshot delete (within 60 s)
    bool handleKey (const juce::KeyPress& k) {
        const auto m = k.getModifiers();
        // plain letters are NEVER used: hosts play the computer keyboard as piano keys (S = a note). Take = Shift+Alt+S (Option+Shift+S on a Mac)
        if (m.isShiftDown() && m.isAltDown() && ! m.isCommandDown() && ! m.isCtrlDown() && juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) k.getKeyCode()) == 'S' && ! SnapshotBank::isTypingIntoText()) { takeNow(); return true; }
        if (m.isCommandDown() && ! m.isShiftDown() && juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) k.getKeyCode()) == 'Z' && bank.canUndoRemove() && bank.msSinceLastRemove() < 60000) { undoDelete(); return true; }
        return false;
    }
    void undoDelete() { const int i = bank.undoRemove(); if (i >= 0) { refresh(); message ("Snapshot back: " + bank.info (i).name); } }
    void refresh() {
        const int c = bank.current();
        nameBtn.setButtonText (c >= 0 ? (bank.info (c).starred ? juce::String::fromUTF8 ("\xe2\x98\x85 ") : juce::String()) + bank.info (c).name : (bank.empty() ? "no snapshots" : "SNAPSHOTS (" + juce::String (bank.size()) + ")"));
        prevBtn.setEnabled (! bank.empty()); nextBtn.setEnabled (! bank.empty());
    }
    // the list menu (also used by tests through menuFor()): items 1..n load; 10001 star / unstar, 10002 rename, 10003 delete, 10004 undo delete, 10005 show folder
    juce::PopupMenu menuFor() const {
        juce::PopupMenu m; m.addSectionHeader ("SNAPSHOTS (" + juce::String (bank.size()) + ")");
        if (bank.empty()) m.addItem (-1, "None yet: press SNAPSHOT (or Shift+Alt+S) while you tweak", false);
        for (int i = 0; i < bank.size(); ++i) {
            const auto& s = bank.info (i);
            m.addItem (i + 1, (s.starred ? juce::String::fromUTF8 ("\xe2\x98\x85 ") : juce::String()) + s.name + "    " + juce::Time (s.timeMs).formatted ("%d %b %H:%M"), true, i == bank.current());
        }
        m.addSeparator();
        const int c = bank.current();
        juce::PopupMenu cur; const bool has = c >= 0;
        cur.addItem (10001, has && bank.info (c).starred ? "Unstar" : "Star", has); cur.addItem (10002, "Rename...", has); cur.addItem (10003, "Delete", has);
        m.addSubMenu (has ? "This snapshot: " + bank.info (c).name : "This snapshot", cur, has);
        m.addItem (10004, "Undo delete", bank.canUndoRemove());
        m.addItem (10005, "Show the folder");
        return m;
    }
    void menuResult (int r) {
        const int c = bank.current();
        if (r >= 1 && r <= bank.size()) { loadIndex (r - 1); return; }
        if (r == 10001 && c >= 0) { bank.star (c, ! bank.info (c).starred); refresh(); }
        if (r == 10002 && c >= 0) askRename (c);
        if (r == 10003 && c >= 0) { const auto n = bank.info (c).name; if (bank.remove (c)) { refresh(); message ("Deleted " + n + " (Ctrl/Cmd+Z or Undo delete brings it back)"); } }
        if (r == 10004) undoDelete();
        if (r == 10005) { bank.folder().createDirectory(); bank.folder().revealToUser(); }
    }

    void resized() override {
        auto r = getLocalBounds();
        if (compact) { listBtn.setBounds (r.removeFromRight (juce::jmin (24, r.getWidth() / 4))); r.removeFromRight (2); snapBtn.setBounds (r); return; }
        const int sw = juce::jmin (92, r.getWidth() / 3);
        snapBtn.setBounds (r.removeFromLeft (sw)); r.removeFromLeft (4);
        prevBtn.setBounds (r.removeFromLeft (24)); r.removeFromLeft (2);
        nextBtn.setBounds (r.removeFromRight (24)); r.removeFromRight (2);
        listBtn.setBounds (r.removeFromRight (22)); r.removeFromRight (2);
        nameBtn.setBounds (r);
    }

    // the SNAPSHOT button itself (accent fill, flash on save)
    struct SnapButton : juce::Button {
        SnapButton() : juce::Button ("SNAPSHOT") {}
        ghosty::themes::Theme theme = ghosty::themes::Theme::Dark; float flash = 0;
        void paintButton (juce::Graphics& g, bool over, bool down) override {
            const auto& T = ghosty::themes::get (theme); auto r = getLocalBounds().toFloat().reduced (1);
            const auto fill = T.violet.interpolatedWith (T.accent3, flash * 0.85f);
            g.setColour (fill.brighter (over ? 0.08f : 0.0f).darker (down ? 0.15f : 0.0f)); g.fillRoundedRectangle (r, r.getHeight() * 0.35f);
            g.setColour (T.isLight ? T.violet.darker (0.3f) : T.violet.brighter (0.3f)); g.drawRoundedRectangle (r, r.getHeight() * 0.35f, 1.2f);
            if (flash > 0) { g.setColour (T.accent3.withAlpha (flash * 0.9f)); g.drawRoundedRectangle (r.expanded (0.0f), r.getHeight() * 0.35f, 1.0f + 2.5f * flash); }
            const auto txtCol = ghosty::themes::contrast (T.onAccent, fill) >= ghosty::themes::contrast (T.ink, fill) ? T.onAccent : T.ink;
            g.setColour (txtCol); g.setFont (juce::FontOptions (juce::jmin (13.5f, getHeight() * 0.46f), juce::Font::bold));
            g.drawFittedText (flash > 0.25f ? "SAVED" : "SNAPSHOT", getLocalBounds().reduced (4, 2), juce::Justification::centred, 1, 0.75f);
        }
    };
    SnapButton snapBtn;
    juce::TextButton listBtn, prevBtn { "<" }, nextBtn { ">" }, nameBtn;

private:
    SnapshotBank& bank; bool compact; ghosty::themes::Theme theme = ghosty::themes::Theme::Dark;
    void message (const juce::String& m) { if (onMessage) onMessage (m); }
    void loaded() { refresh(); if (onLoaded) onLoaded(); const int c = bank.current(); if (c >= 0) message ("Loaded snapshot: " + bank.info (c).name + "  (" + juce::String (c + 1) + " / " + juce::String (bank.size()) + ")"); }
    void showList() {
        menuFor().showMenuAsync (juce::PopupMenu::Options().withTargetComponent (compact ? (juce::Component*) &listBtn : (juce::Component*) &nameBtn),
                                 [sp = juce::Component::SafePointer<SnapshotBar> (this)] (int r) { if (sp && r != 0) sp->menuResult (r); });
    }
    void askRename (int i) {
        auto* w = new juce::AlertWindow ("Rename snapshot", "New name:", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("n", bank.info (i).name); w->addButton ("RENAME", 1, juce::KeyPress (juce::KeyPress::returnKey)); w->addButton ("Cancel", 0);
        const auto file = bank.info (i).file;
        w->enterModalState (true, juce::ModalCallbackFunction::create ([sp = juce::Component::SafePointer<SnapshotBar> (this), w, file] (int res) {
            if (! sp || res != 1) return; const int k = sp->bank.indexOfFile (file);
            if (k >= 0 && sp->bank.rename (k, w->getTextEditorContents ("n"))) { sp->refresh(); sp->message ("Renamed: " + sp->bank.info (k).name); } }), true);
    }
    void timerCallback() override { snapBtn.flash = juce::jmax (0.0f, snapBtn.flash - 1.0f / 54.0f); snapBtn.repaint(); if (snapBtn.flash <= 0) stopTimer(); }
};

} // namespace ghosty
