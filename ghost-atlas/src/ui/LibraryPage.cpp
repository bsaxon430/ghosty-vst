// SPDX-License-Identifier: AGPL-3.0-or-later
// GHOST ATLAS - the Library page (Dawn Harbor, where new sounds come ashore): every instrument that has been
// imported, what the analysis heard in it, where it came from and under what licence; and the way in for more -
// drop a folder of samples on the page (or choose one) and it is copied in, mapped, listened to and listed.
// Also here: the instrument picker and the save dialog, which use the same list.
#include "Pages.h"

namespace atlas::ui
{
namespace
{
std::vector<Instrument> filtered (const std::vector<Instrument>& all, const juce::String& query, const juce::String& category)
{
    std::vector<Instrument> out;
    const auto words = juce::StringArray::fromTokens (query.toLowerCase(), " ", "");
    for (const auto& i : all)
    {
        if (i.hidden) continue;
        if (category.isNotEmpty() && i.category != category) continue;
        bool match = true;
        if (! words.isEmpty())
        {
            const auto hay = (i.name + " " + i.category + " " + i.libraryName + " " + i.tags.joinIntoString (" ")).toLowerCase();
            for (const auto& w : words) if (w.isNotEmpty() && ! hay.contains (w)) { match = false; break; }
        }
        if (match) out.push_back (i);
    }
    std::stable_sort (out.begin(), out.end(), [] (const Instrument& a, const Instrument& b) { return a.name.compareIgnoreCase (b.name) < 0; });
    return out;
}

void paintInstrumentRow (juce::Graphics& g, juce::Rectangle<float> r, const Instrument& i, bool selected, bool marked)
{
    const auto& l = look();
    if (selected) { g.setColour (l.lantern.withAlpha (0.15f)); g.fillRoundedRectangle (r.reduced (2.0f, 1.0f), 7.0f); }
    if (marked) { g.setColour (l.lantern); g.fillEllipse (r.getX() + 9.0f, r.getCentreY() - 3.0f, 6.0f, 6.0f); }
    g.setColour (selected ? l.lantern : l.text (0.94f));
    drawFitted (g, i.name, r.withTrimmedLeft (24.0f).withWidth (r.getWidth() * 0.46f), juce::Justification::centredLeft, selected ? sansBold (15.5f) : sans (15.5f), 0.72f);
    g.setColour (juce::Colour (world (worldOfCategory (i.category)).accent).withAlpha (0.85f));
    drawFitted (g, i.category, juce::Rectangle<float> (r.getX() + r.getWidth() * 0.5f, r.getY(), r.getWidth() * 0.22f, r.getHeight()), juce::Justification::centredLeft, sans (13.5f), 0.75f);
    g.setColour (l.text (0.55f));
    drawFitted (g, i.libraryName, juce::Rectangle<float> (r.getX() + r.getWidth() * 0.73f, r.getY(), r.getWidth() * 0.27f - 10.0f, r.getHeight()), juce::Justification::centredRight, sans (13.0f), 0.72f);
}

void paintPreview (juce::Graphics& g, juce::Rectangle<float> r, const Instrument& i)
{
    const auto& l = look();
    g.setColour (l.text (0.06f)); g.fillRoundedRectangle (r, 8.0f);
    if (i.preview.empty()) return;
    const float w = (r.getWidth() - 16.0f) / (float) i.preview.size();
    for (size_t k = 0; k < i.preview.size(); ++k)
    {
        const float h = juce::jmax (1.5f, i.preview[k] * (r.getHeight() - 14.0f));
        g.setColour (l.accent.withAlpha (0.35f + 0.6f * i.preview[k]));
        g.fillRoundedRectangle (r.getX() + 8.0f + w * (float) k + w * 0.15f, r.getCentreY() - h * 0.5f, w * 0.7f, h, juce::jmin (1.5f, w * 0.35f));
    }
}

juce::String keysText (const Instrument& i) { return noteName (i.loKey) + " to " + noteName (i.hiKey); }
juce::String sizeText (juce::int64 bytes) { return bytes >= 1048576 ? juce::String ((double) bytes / 1048576.0, 1) + " MB" : juce::String ((double) bytes / 1024.0, 0) + " KB"; }

void putOnLayer (Shell& shell, int layer, const Instrument& inst)
{
    auto p = shell.proc.capture();
    LayerState ls = p.layers[(size_t) layer];
    const bool wasEmpty = ls.instrument.isEmpty();
    ls.instrument = inst.id; ls.label.clear(); ls.role.clear();
    ls.p[lpOn] = 1.0f;
    shell.proc.engine().setLayerSource (layer, ls);
    if (wasEmpty) shell.proc.engine().setLayerParam (layer, lpSendC, 0.3f);      // a new layer arrives with a little of the space on it
    shell.proc.setHost (Processor::hpLevel1 + layer, matchedLevel (inst));           // and at a like loudness to any other
}

// ============================================================================================ importing, off the screen's thread
class Importer final : public juce::Thread
{
public:
    Importer (Library& l, juce::File f) : juce::Thread ("Ghost Atlas import"), lib (l), folder (std::move (f)) { startThread(); }
    // (or any other piece of library work that reports like an import: a changed mapping)
    Importer (Library& l, std::function<ImportReport (const Progress&)> w) : juce::Thread ("Ghost Atlas import"), lib (l), work (std::move (w)) { startThread(); }
    ~Importer() override { stopThread (60000); }
    void run() override
    {
        const Progress progress = [this] (float f, const juce::String& what) {
            fraction.store (f);
            { const juce::ScopedLock sl (lock); doing = what; }
            return ! threadShouldExit();
        };
        report = work ? work (progress) : lib.importFolder (folder, true, progress);
        done.store (true);
    }
    std::function<ImportReport (const Progress&)> work;
    juce::String now() const { const juce::ScopedLock sl (lock); return doing; }
    Library& lib; juce::File folder;
    std::atomic<float> fraction { 0.0f };
    std::atomic<bool> done { false };
    ImportReport report;
private:
    juce::CriticalSection lock;
    juce::String doing;
};

// ============================================================================================ the page
class LibraryPage final : public Page, public juce::FileDragAndDropTarget
{
public:
    explicit LibraryPage (Shell& s) : Page (s)
    {
        search.setHint ("Search instruments");
        search.onChange = [this] (const juce::String& t) { query = t.trim(); rebuild(); };
        addAndMakeVisible (search);
        kind.menuParent = shell.canvas;
        kind.choices.add ("Every kind");
        for (const auto& c : categories()) kind.choices.add (c.name);
        kind.get = [this] { for (size_t i = 0; i < categories().size(); ++i) if (categoryFilter == categories()[i].name) return (float) (i + 1); return 0.0f; };
        kind.set = [this] (float v) { categoryFilter = v < 0.5f ? juce::String() : juce::String (categories()[(size_t) ((int) v - 1)].name); rebuild(); };
        addAndMakeVisible (kind);

        list.setRowHeight (32);
        list.count = [this] { return (int) shown.size(); };
        list.paintRow = [this] (int row, juce::Graphics& g, juce::Rectangle<float> r, bool, bool) { if (row >= 0 && row < (int) shown.size()) paintInstrumentRow (g, r, shown[(size_t) row], shown[(size_t) row].id == selectedId, false); };
        list.chosen = [this] (int row) { if (row >= 0 && row < (int) shown.size()) { selectedId = shown[(size_t) row].id; view = 0; showSelected(); } };
        addAndMakeVisible (list);

        add.text = "Add a folder"; add.style = TextBtn::lit;
        add.onClick = [this] { chooseFolder(); };
        add.usable = [this] { return importer == nullptr; };
        addAndMakeVisible (add);
        sources.text = "Where the sounds come from";
        sources.onClick = [this] { view = view == 2 ? 0 : 2; text.setText (shell.proc.library().manifestText(), false); showSelected(); };
        addAndMakeVisible (sources);
        stop.text = "Stop"; stop.onClick = [this] { if (importer != nullptr) importer->signalThreadShouldExit(); };
        addChildComponent (stop);

        setKind.menuParent = shell.canvas; setKind.label = "Listed under"; setKind.labelWidth = 92.0f;
        for (const auto& c : categories()) setKind.choices.add (c.name);
        setKind.get = [this] { for (size_t i = 0; i < categories().size(); ++i) if (selected.category == categories()[i].name) return (float) i; return 0.0f; };
        setKind.set = [this] (float v) { auto& lib = shell.proc.library(); lib.setCategory (selected.id, categories()[(size_t) (int) v].name); lib.save(); rebuild(); showSelected(); };
        addChildComponent (setKind);
        use.style = TextBtn::lit;
        use.onClick = [this] { putOnLayer (shell, shell.layer, selected); if (shell.say) shell.say (selected.name + " is on layer " + juce::String (shell.layer + 1)); if (shell.soundChanged) shell.soundChanged(); };
        addChildComponent (use);
        find.text = "Find its files";
        find.onClick = [this] { relink(); };
        addChildComponent (find);
        back.text = "Back to the instrument"; back.onClick = [this] { view = 0; showSelected(); };
        addChildComponent (back);

        // the mapping editor: for an instrument made from loose recordings, which key each recording sits on
        editMap.text = "Change its mapping";
        editMap.setTooltip ("See which key each of its recordings sits on, and move any that are on the wrong one.");
        editMap.onClick = [this] { mapping = mappingWas = shell.proc.library().mappingOf (selected); view = 3; mapList.updateContent(); showSelected(); };
        addChildComponent (editMap);
        applyMap.text = "Apply"; applyMap.style = TextBtn::lit;
        applyMap.usable = [this] { return importer == nullptr && mapping.size() == mappingWas.size() && ! changedRows().empty(); };
        applyMap.onClick = [this] {
            auto changed = changedRows();
            if (changed.empty() || importer != nullptr) return;
            const auto inst = selected;
            importer = std::make_unique<Importer> (shell.proc.library(), [this, inst, changed] (const Progress& progress) { return shell.proc.library().remap (inst, changed, progress); });
            if (shell.say) shell.say ("Moving " + juce::String ((int) changed.size()) + (changed.size() == 1 ? " recording" : " recordings") + " and listening again");
            showSelected();
        };
        addChildComponent (applyMap);
        mapList.setRowHeight (28);
        mapList.count = [this] { return (int) mapping.size(); };
        mapList.paintRow = [this] (int row, juce::Graphics& g, juce::Rectangle<float> r, bool, bool) {
            if (row < 0 || row >= (int) mapping.size()) return;
            const auto& l = look();
            const bool moved = row < (int) mappingWas.size() && mapping[(size_t) row].note != mappingWas[(size_t) row].note;
            g.setColour (l.text (0.9f));
            drawFitted (g, mapping[(size_t) row].file.fromLastOccurrenceOf ("/", false, false), r.withTrimmedLeft (10.0f).withTrimmedRight (150.0f), juce::Justification::centredLeft, sans (14.0f), 0.7f);
            const auto pill = juce::Rectangle<float> (r.getRight() - 132.0f, r.getY() + 3.0f, 76.0f, r.getHeight() - 6.0f);
            g.setColour (moved ? l.lantern.withAlpha (0.9f) : l.text (0.1f)); g.fillRoundedRectangle (pill, 6.0f);
            g.setColour (moved ? l.ink() : l.text (0.92f)); g.setFont (moved ? sansBold (13.5f) : sans (13.5f));
            g.drawText (noteName (mapping[(size_t) row].note), pill, juce::Justification::centred, false);
            if (moved) { g.setColour (l.text (0.5f)); g.setFont (sans (12.0f)); g.drawText ("was " + noteName (mappingWas[(size_t) row].note), juce::Rectangle<float> (r.getRight() - 54.0f, r.getY(), 50.0f, r.getHeight()), juce::Justification::centredLeft, false); }
        };
        mapList.clicked = [this] (int row, const juce::MouseEvent&) {
            if (row < 0 || row >= (int) mapping.size() || importer != nullptr) return;
            juce::PopupMenu m;
            m.setLookAndFeel (&chartLookAndFeel());
            for (int oct = 0; oct <= 8; ++oct)
            {
                juce::PopupMenu notes;
                for (int k = 0; k < 12; ++k) { const int n = 12 * (oct + 1) + k; notes.addItem (n + 1, noteName (n), true, n == mapping[(size_t) row].note); }
                m.addSubMenu ("Octave " + juce::String (oct) + (oct == 4 ? " (middle C)" : ""), notes, true, nullptr, mapping[(size_t) row].note / 12 - 1 == oct);
            }
            auto opts = juce::PopupMenu::Options().withMousePosition();
            if (shell.canvas != nullptr) opts = opts.withParentComponent (shell.canvas);
            m.showMenuAsync (opts, [safe = juce::Component::SafePointer<LibraryPage> (this), row] (int r) {
                if (safe == nullptr || r <= 0 || row >= (int) safe->mapping.size()) return;
                safe->mapping[(size_t) row].note = r - 1;
                safe->mapList.repaint(); safe->applyMap.repaint(); safe->repaint();
            });
        };
        addChildComponent (mapList);

        text.setLookAndFeel (&chartLookAndFeel());
        text.setMultiLine (true); text.setReadOnly (true); text.setScrollbarsShown (true); text.setCaretVisible (false);
        text.setWantsKeyboardFocus (false);     // (it is only read; the keyboard stays with the host)
        text.setFont (sans (14.5f)); text.setLineSpacing (1.25f);
        text.setColour (juce::TextEditor::textColourId, look().text (0.9f));
        addChildComponent (text);
        refresh();
    }
    ~LibraryPage() override { importer.reset(); text.setLookAndFeel (nullptr); }

    std::vector<Stage::Pane> panes() const override
    {
        return { { { 0.0f, 0.0f, (float) kBaseW, 104.0f }, 0.0f, Stage::scrimTop },
                 { { 16.0f, 68.0f, 544.0f, 636.0f }, 14.0f, Stage::glass },
                 { { 576.0f, 68.0f, 728.0f, 636.0f }, 14.0f, Stage::glass } };
    }
    int worldOverride() const override { return kHarbor; }
    void showFor (const juce::String& what) override
    {
        // "instrument": the first instrument's details; "mapping": the mapping of the first one made from loose recordings
        for (const auto& i : all)
            if (what == "instrument" || i.format == "mapped")
            {
                selectedId = i.id; view = 0; rebuild(); showSelected();
                if (what == "mapping") { mapping = mappingWas = shell.proc.library().mappingOf (selected); if (mapping.size() > 2) mapping[2].note += 12; view = 3; mapList.updateContent(); showSelected(); }
                return;
            }
    }
    bool secondView() const override { return false; }

    void resized() override
    {
        search.setBounds (28, 80, 300, 34); kind.setBounds (336, 82, 212, 30);
        list.setBounds (20, 124, 536, 512);
        add.setBounds (28, 656, 132, 34); sources.setBounds (170, 656, 232, 34); stop.setBounds (476, 656, 72, 34);
        setKind.setBounds (600, 628, 330, 30); use.setBounds (1100, 626, 184, 36); find.setBounds (944, 628, 140, 32);
        back.setBounds (600, 660, 200, 30);
        text.setBounds (600, 126, 684, 520);
        editMap.setBounds (944, 664, 140, 28);
        mapList.setBounds (596, 168, 692, 478); applyMap.setBounds (1174, 656, 110, 36);
    }

    void refresh() override
    {
        all = shell.proc.library().all();
        rebuild();
        showSelected();
    }

    void tick() override
    {
        if (importer == nullptr) return;
        if (importer->done.load())
        {
            const auto rep = importer->report;
            importer.reset();
            text.setText ((rep.added + rep.updated > 0 ? juce::String (rep.added + rep.updated) + " instrument" + (rep.added + rep.updated == 1 ? "" : "s") + " came ashore.\n\n" : juce::String()) + rep.text(), false);
            view = 1;
            if (! rep.instrumentIds.isEmpty() && ! rep.instrumentIds.contains (selectedId)) selectedId = rep.instrumentIds[0];
            if (shell.say) shell.say (rep.added + rep.updated > 0 ? "The import is finished" : "Nothing new was imported");
            if (shell.soundChanged) shell.soundChanged(); else refresh();
            return;
        }
        repaint (16, 640, 544, 64);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& l = look();
        // ---- the list's foot: how many, or how the import is going
        if (importer != nullptr)
        {
            const auto bar = juce::Rectangle<float> (170.0f, 668.0f, 296.0f, 6.0f);
            g.setColour (l.text (0.15f)); g.fillRoundedRectangle (bar, 3.0f);
            g.setColour (l.lantern); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.02f, 1.0f, importer->fraction.load())), 3.0f);
            g.setColour (l.text (0.85f));
            drawFitted (g, importer->now(), { 170.0f, 646.0f, 296.0f, 18.0f }, juce::Justification::centredLeft, sans (13.5f), 0.7f);
        }
        else
        {
            g.setColour (l.text (0.55f)); g.setFont (sans (13.5f));
            g.drawText (juce::String ((int) shown.size()) + " of " + juce::String ((int) all.size()), juce::Rectangle<float> (410.0f, 656.0f, 138.0f, 34.0f), juce::Justification::centredRight, false);
        }
        if (all.empty() && importer == nullptr)
        {
            g.setColour (l.text (0.8f)); g.setFont (sans (15.5f));
            g.drawFittedText ("No instruments yet. Drop a folder of samples anywhere on this page, or choose Add a folder. "
                              "SFZ instruments, SoundFonts, Decent Sampler packs and plain folders of WAV, AIFF or FLAC files named by note all work.", 40, 150, 490, 120, juce::Justification::topLeft, 6);
        }
        // ---- the right side
        if (dragging)
        {
            const auto zone = juce::Rectangle<float> (592.0f, 84.0f, 696.0f, 604.0f);
            g.setColour (l.lantern.withAlpha (0.1f)); g.fillRoundedRectangle (zone, 12.0f);
            const float dashes[2] = { 7.0f, 6.0f };
            juce::Path edge, dashed; edge.addRoundedRectangle (zone, 12.0f);
            juce::PathStrokeType (1.6f).createDashedStroke (dashed, edge, dashes, 2);
            g.setColour (l.lantern); g.fillPath (dashed);
            g.setFont (fellRoman (30.0f)); g.drawText ("Let go, and it comes ashore", zone, juce::Justification::centred, false);
            return;
        }
        if (view == 3)
        {
            g.setColour (l.text (0.98f));
            drawFitted (g, "Where " + selected.name + " sits on the keys", { 600.0f, 78.0f, 680.0f, 40.0f }, juce::Justification::centredLeft, fellRoman (30.0f), 0.6f);
            g.setColour (l.text (0.72f)); g.setFont (sans (14.5f));
            g.drawFittedText ("Each recording and the key it plays at its own pitch. Click a key to move a recording to another; Apply writes a new mapping "
                              "(the recordings themselves are not touched) and listens to the instrument again.", 600, 118, 680, 44, juce::Justification::topLeft, 2);
            return;
        }
        if (view != 0)
        {
            g.setColour (l.text (0.98f)); g.setFont (fellRoman (30.0f));
            g.drawText (view == 1 ? "What came ashore" : "Where the sounds come from", juce::Rectangle<float> (600.0f, 78.0f, 680.0f, 40.0f), juce::Justification::centredLeft, false);
            return;
        }
        if (selected.id.isEmpty())
        {
            g.setColour (l.text (0.98f)); g.setFont (fellRoman (30.0f));
            g.drawText ("Dawn Harbor", juce::Rectangle<float> (600.0f, 78.0f, 680.0f, 40.0f), juce::Justification::centredLeft, false);
            g.setColour (l.text (0.78f)); g.setFont (sans (15.5f));
            g.drawFittedText ("This is where new sounds come ashore. Drop a folder of samples anywhere on this page and it is copied into the library, "
                              "mapped across the keys, listened to and listed. The recordings themselves are never changed. "
                              "Choose an instrument on the left to see what was heard in it and where it came from.", 600, 126, 672, 70, juce::Justification::topLeft, 3);
            // what is in the harbor: every library, how much it holds, and the licence it came under
            juce::int64 bytes = 0; for (const auto& s : libraries) bytes += s.bytes;
            headingAt (g, "What is here", 600.0f, 214.0f);
            g.setColour (l.text (0.6f)); g.setFont (sans (13.5f));
            g.drawText (juce::String ((int) all.size()) + " instruments from " + juce::String ((int) libraries.size()) + (libraries.size() == 1 ? " library, " : " libraries, ")
                        + (bytes >= 1073741824 ? juce::String ((double) bytes / 1073741824.0, 1) + " GB" : sizeText (bytes)), juce::Rectangle<float> (760.0f, 212.0f, 524.0f, 20.0f), juce::Justification::centredRight, false);
            const int perColumn = 19, most = perColumn * 2;
            for (int k = 0; k < (int) libraries.size() && k < most; ++k)
            {
                const auto& s = libraries[(size_t) k];
                const float x = 600.0f + (float) (k / perColumn) * 348.0f, y = 244.0f + (float) (k % perColumn) * 21.5f;
                const bool cut = k == most - 1 && (int) libraries.size() > most;
                g.setColour (l.text (0.9f));
                drawFitted (g, cut ? "and " + juce::String ((int) libraries.size() - most + 1) + " more (Where the sounds come from lists them all)" : s.name,
                            { x, y, cut ? 330.0f : 186.0f, 20.0f }, juce::Justification::centredLeft, sans (13.5f), 0.7f);
                if (cut) break;
                g.setColour (l.accent.withAlpha (0.9f)); g.setFont (sans (13.0f));
                g.drawText (juce::String (s.instruments), juce::Rectangle<float> (x + 190.0f, y, 30.0f, 20.0f), juce::Justification::centredRight, false);
                g.setColour (l.text (0.5f));
                drawFitted (g, s.license.isEmpty() ? juce::String ("licence not stated") : s.license, { x + 228.0f, y, 104.0f, 20.0f }, juce::Justification::centredLeft, sans (12.0f), 0.7f);
            }
            return;
        }
        const auto& i = selected;
        g.setColour (l.text (0.98f));
        drawFitted (g, i.name, { 600.0f, 78.0f, 680.0f, 40.0f }, juce::Justification::centredLeft, fellRoman (32.0f), 0.7f);
        g.setColour (l.text (0.7f));
        const juce::String made = i.format == "mapped" ? "Mapped from file names" : (i.format == "sf2" ? "From a SoundFont" : (i.format == "dspreset" ? "From a Decent Sampler pack" : "SFZ instrument"));
        drawFitted (g, made + ": " + juce::String (i.samples) + " recordings in " + juce::String (i.regions) + " zones, " + keysText (i)
                       + (i.velocityLayers > 1 ? ", " + juce::String (i.velocityLayers) + " velocity layers" : juce::String()) + ", " + sizeText (i.bytes),
                    { 601.0f, 116.0f, 680.0f, 20.0f }, juce::Justification::centredLeft, sans (14.5f), 0.72f);
        paintPreview (g, { 600.0f, 148.0f, 684.0f, 84.0f }, i);

        headingAt (g, "What was heard", 600.0f, 246.0f);
        auto fact = [&g, &l] (int col, int row, const juce::String& name, const juce::String& value) {
            const float x = 600.0f + (float) col * 228.0f, y = 274.0f + (float) row * 24.0f;
            g.setColour (l.text (0.6f)); g.setFont (sans (14.0f)); g.drawText (name, juce::Rectangle<float> (x, y, 96.0f, 20.0f), juce::Justification::centredLeft, false);
            g.setColour (l.text (0.95f)); g.setFont (sans (14.5f)); g.drawText (value, juce::Rectangle<float> (x + 96.0f, y, 126.0f, 20.0f), juce::Justification::centredLeft, true);
        };
        if (i.f.valid)
        {
            const auto pct = [] (float v) { return juce::String ((int) std::lround (v * 100.0f)) + " %"; };
            fact (0, 0, "Loudness", juce::String (i.f.loudnessDb, 1) + " dB");
            fact (0, 1, "Attack", i.f.attackMs >= 1000.0f ? juce::String (i.f.attackMs / 1000.0f, 1) + " s" : juce::String ((int) i.f.attackMs) + " ms");
            fact (0, 2, "Holds", i.f.sustain > 0.6f ? "as long as the key" : (i.f.sustain > 0.2f ? "then fades" : "a short sound"));
            fact (1, 0, "Brightness", pct (i.f.brightness));
            fact (1, 1, "Width", pct (juce::jmin (1.0f, i.f.width * 2.0f)));
            fact (1, 2, "Movement", pct (i.f.movement));
            fact (2, 0, "Tuning", i.f.harmonicity < 0.5f ? juce::String ("no clear pitch") : (std::abs (i.f.tuneCents) < 3.0f ? juce::String ("in tune") : juce::String (i.f.tuneCents > 0 ? "+" : "") + juce::String ((int) std::lround (i.f.tuneCents)) + " cents"));
            fact (2, 1, "Touch", juce::String ((int) std::lround (i.f.velocityDb)) + " dB range");
            fact (2, 2, "Loop seams", i.f.loopClick > 0.3f ? "can be heard" : "clean");
        }
        g.setColour (l.text (0.75f)); g.setFont (sans (14.0f));
        g.drawText (i.tags.isEmpty() ? juce::String() : "Sounds " + i.tags.joinIntoString (", "), juce::Rectangle<float> (600.0f, 350.0f, 684.0f, 20.0f), juce::Justification::centredLeft, true);

        headingAt (g, "Where it comes from", 600.0f, 384.0f);
        g.setColour (l.text (0.92f)); g.setFont (sans (14.5f));
        g.drawText (i.libraryName + (i.author.isNotEmpty() ? ", by " + i.author : juce::String()), juce::Rectangle<float> (600.0f, 410.0f, 684.0f, 20.0f), juce::Justification::centredLeft, true);
        g.setColour (i.license.isEmpty() ? l.danger : l.text (0.92f));
        g.drawText (i.license.isEmpty() ? juce::String ("Licence not stated: check before sharing anything made with it") : "Licence: " + i.license, juce::Rectangle<float> (600.0f, 432.0f, 684.0f, 20.0f), juce::Justification::centredLeft, true);
        g.setColour (l.text (0.62f));
        g.drawText (i.source.isEmpty() ? juce::String ("Imported from this computer") : i.source, juce::Rectangle<float> (600.0f, 454.0f, 684.0f, 20.0f), juce::Justification::centredLeft, true);
        if (i.restrictions.isNotEmpty() && i.restrictions != "none") g.drawText ("Restrictions: " + i.restrictions, juce::Rectangle<float> (600.0f, 476.0f, 684.0f, 20.0f), juce::Justification::centredLeft, true);

        juce::StringArray notes = i.notes;
        if (! i.unsupported.isEmpty()) notes.add ("The engine does not play " + juce::String (i.unsupported.size()) + " of its SFZ settings: " + i.unsupported.joinIntoString (", "));
        if (missingFile) notes.insert (0, "Its files are not where they were. Choose Find its files to point at the folder they are in now.");
        if (! notes.isEmpty())
        {
            headingAt (g, "Worth knowing", 600.0f, 508.0f);
            g.setColour (missingFile ? l.danger : l.text (0.78f)); g.setFont (sans (14.0f));
            g.drawFittedText (notes.joinIntoString ("\n"), 600, 534, 684, 84, juce::Justification::topLeft, 5, 0.85f);
        }
    }

    // ---- a folder dropped on the page
    bool isInterestedInFileDrag (const juce::StringArray& files) override { for (const auto& f : files) if (juce::File (f).isDirectory()) return importer == nullptr; return false; }
    void fileDragEnter (const juce::StringArray&, int, int) override { dragging = true; showSelected(); }
    void fileDragExit (const juce::StringArray&) override { dragging = false; showSelected(); }
    void filesDropped (const juce::StringArray& files, int, int) override
    {
        dragging = false;
        for (const auto& f : files) if (juce::File (f).isDirectory()) { startImport (juce::File (f)); break; }
        showSelected();
    }

private:
    void rebuild()
    {
        shown = filtered (all, query, categoryFilter);
        libraries = shell.proc.library().sources();
        std::stable_sort (libraries.begin(), libraries.end(), [] (const LibrarySource& a, const LibrarySource& b) { return a.instruments > b.instruments; });
        list.updateContent();
        int row = -1;
        for (size_t i = 0; i < shown.size(); ++i) if (shown[i].id == selectedId) row = (int) i;
        if (row >= 0) list.selectRow (row, true, true); else list.deselectAllRows();
        list.repaint(); kind.repaint(); repaint();
    }
    void showSelected()
    {
        selected = Instrument();
        for (const auto& i : all) if (i.id == selectedId) selected = i;
        missingFile = selected.id.isNotEmpty() && ! shell.proc.library().sfzFile (selected).existsAsFile();
        const bool detail = view == 0 && selected.id.isNotEmpty() && ! dragging;
        setKind.setVisible (detail); use.setVisible (detail); find.setVisible (detail && missingFile);
        use.text = "Put it on layer " + juce::String (shell.layer + 1);
        text.setVisible ((view == 1 || view == 2) && ! dragging); back.setVisible (view != 0 && ! dragging && selected.id.isNotEmpty());
        editMap.setVisible (detail && selected.format == "mapped" && ! missingFile);
        mapList.setVisible (view == 3 && ! dragging); applyMap.setVisible (view == 3 && ! dragging);
        applyMap.repaint();
        stop.setVisible (importer != nullptr);
        sources.selected = view == 2;
        add.repaint(); sources.repaint(); setKind.repaint(); use.repaint();
        repaint();
    }
    void startImport (const juce::File& folder)
    {
        if (importer != nullptr || ! folder.isDirectory()) return;
        importer = std::make_unique<Importer> (shell.proc.library(), folder);
        if (shell.say) shell.say ("Bringing in " + folder.getFileName());
        showSelected();
    }
    void chooseFolder()
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose a folder of samples", juce::File::getSpecialLocation (juce::File::userHomeDirectory));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [safe = juce::Component::SafePointer<LibraryPage> (this)] (const juce::FileChooser& fc) { if (safe != nullptr && fc.getResult().isDirectory()) safe->startImport (fc.getResult()); });
    }
    void relink()
    {
        chooser = std::make_unique<juce::FileChooser> ("Where is \"" + selected.libraryName + "\" now?", juce::File::getSpecialLocation (juce::File::userHomeDirectory));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [safe = juce::Component::SafePointer<LibraryPage> (this)] (const juce::FileChooser& fc) {
                                  if (safe == nullptr || ! fc.getResult().isDirectory()) return;
                                  auto& lib = safe->shell.proc.library();
                                  if (lib.relink (safe->selected.libraryId, fc.getResult())) { lib.save(); if (safe->shell.soundChanged) safe->shell.soundChanged(); }
                              });
    }

    TextBox search;
    Choice kind, setKind;
    RowList list;
    TextBtn add, sources, stop, use, find, back, editMap, applyMap;
    RowList mapList;
    std::vector<Library::Mapped> mapping, mappingWas;      // the recordings of the instrument in hand: as they are being moved, and as they were
    std::vector<Library::Mapped> changedRows() const
    {
        std::vector<Library::Mapped> c;
        for (size_t i = 0; i < mapping.size() && i < mappingWas.size(); ++i) if (mapping[i].note != mappingWas[i].note) c.push_back (mapping[i]);
        return c;
    }
    juce::TextEditor text;
    std::vector<Instrument> all, shown;
    std::vector<LibrarySource> libraries;         // most instruments first
    Instrument selected;
    juce::String query, categoryFilter, selectedId;
    int view = 0;                                 // 0 the instrument, 1 the last import's report, 2 where the sounds come from, 3 its mapping
    bool dragging = false, missingFile = false;
    std::unique_ptr<Importer> importer;
    std::unique_ptr<juce::FileChooser> chooser;
};

// ============================================================================================ choosing an instrument for a layer
class Picker final : public Overlay
{
public:
    Picker (Shell& s, int layerIndex) : shell (s), layer (layerIndex)
    {
        before = shell.proc.capture().layers[(size_t) layer];
        beforeGain = shell.proc.getHost (Processor::hpLevel1 + layer);
        all = shell.proc.library().all();
        search.setHint ("Search instruments");
        search.onChange = [this] (const juce::String& t) { query = t.trim(); rebuild(); };
        addAndMakeVisible (search);
        kind.menuParent = shell.canvas;
        kind.choices.add ("Every kind");
        for (const auto& c : categories()) kind.choices.add (c.name);
        kind.get = [this] { for (size_t i = 0; i < categories().size(); ++i) if (categoryFilter == categories()[i].name) return (float) (i + 1); return 0.0f; };
        kind.set = [this] (float v) { categoryFilter = v < 0.5f ? juce::String() : juce::String (categories()[(size_t) ((int) v - 1)].name); rebuild(); };
        addAndMakeVisible (kind);
        list.setRowHeight (32);
        list.count = [this] { return (int) shown.size(); };
        list.paintRow = [this] (int row, juce::Graphics& g, juce::Rectangle<float> r, bool, bool) { if (row >= 0 && row < (int) shown.size()) paintInstrumentRow (g, r, shown[(size_t) row], shown[(size_t) row].id == chosenId, false); };
        list.chosen = [this] (int row) {
            if (quiet || row < 0 || row >= (int) shown.size()) return;
            chosenId = shown[(size_t) row].id; chosen = shown[(size_t) row];
            putOnLayer (shell, layer, chosen);          // it is on the layer at once, so it can be played before deciding
            repaint();
        };
        addAndMakeVisible (list);
        done.text = "Keep it"; done.style = TextBtn::lit; done.onClick = [this] { close(); };
        undo.text = "Put it back as it was";
        undo.onClick = [this] { shell.proc.engine().setLayerSource (layer, before); shell.proc.setHost (Processor::hpLevel1 + layer, beforeGain); close(); };
        hear.text = "Play a few notes"; hear.onClick = [this] { shell.proc.engine().audition (0, chosen.id.isNotEmpty() ? chosen.nearestKey (60) : 60); };
        for (auto* b : { &done, &undo, &hear }) addAndMakeVisible (*b);
        chosenId = before.instrument;
        for (const auto& i : all) if (i.id == chosenId) chosen = i;
        if (chosen.id.isEmpty() && chosenId.isNotEmpty()) { shell.proc.library().findLoose (chosenId, chosen); chosenId = chosen.id; }
        rebuild();
    }
    void resized() override
    {
        panel = juce::Rectangle<float> (900.0f, 588.0f).withCentre ({ (float) kBaseW * 0.5f, (float) kBaseH * 0.5f + 14.0f });
        const int x = (int) panel.getX(), y = (int) panel.getY();
        search.setBounds (x + 24, y + 70, 300, 34); kind.setBounds (x + 334, y + 72, 204, 30);
        list.setBounds (x + 16, y + 114, 530, 458);
        hear.setBounds (x + 572, y + 420, 304, 32);
        done.setBounds (x + 572, y + 534, 120, 36); undo.setBounds (x + 702, y + 536, 174, 32);
        for (size_t i = 0; i < shown.size(); ++i) if (shown[i].id == chosenId) list.scrollToEnsureRowIsOnscreen ((int) i);
    }
    void paint (juce::Graphics& g) override
    {
        const auto& l = look();
        g.fillAll (l.deep.darker (0.85f).withAlpha (0.66f));
        g.setColour (l.deep.darker (0.62f).withAlpha (0.97f)); g.fillRoundedRectangle (panel, 16.0f);
        g.setColour (l.text (0.14f)); g.drawRoundedRectangle (panel.reduced (0.5f), 16.0f, 1.0f);
        g.setColour (l.text (0.98f)); g.setFont (fellRoman (30.0f));
        g.drawText ("An instrument for layer " + juce::String (layer + 1), panel.reduced (26.0f, 16.0f).removeFromTop (40.0f), juce::Justification::centredLeft, false);
        const float x = panel.getX() + 572.0f, y = panel.getY() + 114.0f;
        if (chosen.id.isEmpty())
        {
            g.setColour (l.text (0.75f)); g.setFont (sans (15.0f));
            g.drawFittedText (all.empty() ? "The library is empty. Close this, open Library, and add a folder of samples."
                                          : "Click an instrument and it is on the layer at once: play the keys to hear it, then keep it or put the layer back as it was.",
                              (int) x, (int) y, 304, 120, juce::Justification::topLeft, 6);
            return;
        }
        g.setColour (l.text (0.97f));
        drawFitted (g, chosen.name, { x, y, 304.0f, 30.0f }, juce::Justification::centredLeft, fellRoman (25.0f), 0.7f);
        g.setColour (juce::Colour (world (worldOfCategory (chosen.category)).accent).withAlpha (0.9f)); g.setFont (sans (14.5f));
        g.drawText (chosen.category, juce::Rectangle<float> (x, y + 32.0f, 304.0f, 20.0f), juce::Justification::centredLeft, true);
        paintPreview (g, { x, y + 62.0f, 304.0f, 70.0f }, chosen);
        g.setColour (l.text (0.82f)); g.setFont (sans (14.0f));
        juce::StringArray lines;
        lines.add ("Keys " + keysText (chosen) + (chosen.keyMap.isNotEmpty() ? " (with gaps)" : ""));
        if (! chosen.tags.isEmpty()) lines.add ("Sounds " + chosen.tags.joinIntoString (", "));
        lines.add ("From " + chosen.libraryName);
        lines.add (chosen.license.isEmpty() ? juce::String ("Licence not stated") : "Licence: " + chosen.license);
        g.drawFittedText (lines.joinIntoString ("\n"), (int) x, (int) y + 146, 304, 110, juce::Justification::topLeft, 6, 0.85f);
        g.setColour (l.text (0.6f)); g.setFont (sans (13.5f));
        g.drawFittedText ("It arrives at a level matched to the other layers. Play the keys to hear it in the sound.", (int) x, (int) y + 264, 304, 40, juce::Justification::topLeft, 2, 0.9f);
    }
    void mouseDown (const juce::MouseEvent& e) override { if (! panel.contains (e.position)) close(); }
private:
    void rebuild()
    {
        shown = filtered (all, query, categoryFilter);
        quiet = true;
        list.updateContent();
        int row = -1;
        for (size_t i = 0; i < shown.size(); ++i) if (shown[i].id == chosenId) row = (int) i;
        if (row >= 0) list.selectRow (row, true, true); else list.deselectAllRows();
        quiet = false;
        list.repaint(); kind.repaint(); repaint();
    }
    Shell& shell; int layer;
    LayerState before; float beforeGain = 0.0f;
    TextBox search; Choice kind; RowList list; TextBtn done, undo, hear;
    std::vector<Instrument> all, shown;
    Instrument chosen;
    juce::String query, categoryFilter, chosenId;
    juce::Rectangle<float> panel;
    bool quiet = false;
};

// ============================================================================================ saving the sound
class SaveDialog final : public Overlay
{
public:
    explicit SaveDialog (Shell& s) : shell (s)
    {
        sound = shell.proc.capture();
        name.setHint ("A name for this sound");
        name.editor.setText (sound.factory || sound.file.isEmpty() ? sound.name + (sound.factory ? " (mine)" : "") : sound.name, false);
        tags.setHint ("A few words, with commas: airy, slow, dark");
        tags.editor.setText (sound.tags.joinIntoString (", "), false);
        kind.menuParent = shell.canvas;
        for (const auto& c : categories()) kind.choices.add (c.name);
        kind.get = [this] { for (size_t i = 0; i < categories().size(); ++i) if (sound.category == categories()[i].name) return (float) i; return 0.0f; };
        kind.set = [this] (float v) { sound.category = categories()[(size_t) (int) v].name; sound.world.clear(); repaint(); };
        save.text = "Save"; save.style = TextBtn::lit; save.onClick = [this] { commit(); };
        cancel.text = "Cancel"; cancel.onClick = [this] { close(); };
        name.editor.onReturnKey = [this] { commit(); };
        tags.editor.onReturnKey = [this] { commit(); };
        for (auto* c : std::initializer_list<juce::Component*> { &name, &tags, &kind, &save, &cancel }) addAndMakeVisible (*c);
    }
    // Save was asked for, so a name is what is typed next: its box takes the keyboard as the dialog appears. (Every other
    // text box waits to be clicked. The keyboard goes back to the host when the dialog closes.)
    void parentHierarchyChanged() override
    {
        if (! isShowing() || asked) return;
        asked = true;
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<SaveDialog> (this)] {
            if (safe != nullptr && safe->isShowing()) { safe->name.take(); safe->name.editor.selectAll(); }
        });
    }
    void resized() override
    {
        panel = juce::Rectangle<float> (520.0f, 330.0f).withCentre ({ (float) kBaseW * 0.5f, (float) kBaseH * 0.5f });
        const int x = (int) panel.getX() + 28, y = (int) panel.getY();
        name.setBounds (x, y + 92, 464, 36); kind.setBounds (x, y + 158, 464, 32); tags.setBounds (x, y + 220, 464, 36);
        save.setBounds (x, y + 274, 110, 36); cancel.setBounds (x + 122, y + 276, 96, 32);
    }
    void paint (juce::Graphics& g) override
    {
        const auto& l = look();
        g.fillAll (l.deep.darker (0.85f).withAlpha (0.66f));
        g.setColour (l.deep.darker (0.62f).withAlpha (0.97f)); g.fillRoundedRectangle (panel, 16.0f);
        g.setColour (l.text (0.14f)); g.drawRoundedRectangle (panel.reduced (0.5f), 16.0f, 1.0f);
        g.setColour (l.text (0.98f)); g.setFont (fellRoman (30.0f));
        g.drawText ("Keep this sound", panel.reduced (28.0f, 16.0f).removeFromTop (40.0f), juce::Justification::centredLeft, false);
        g.setColour (l.text (0.62f)); g.setFont (sans (13.5f));
        const float x = panel.getX() + 28.0f, y = panel.getY();
        g.drawText ("Name", juce::Rectangle<float> (x, y + 72.0f, 200.0f, 18.0f), juce::Justification::centredLeft, false);
        g.drawText ("Kind (it will live in " + juce::String (world (worldOfCategory (sound.category)).name) + ")", juce::Rectangle<float> (x, y + 138.0f, 460.0f, 18.0f), juce::Justification::centredLeft, false);
        g.drawText ("How it sounds", juce::Rectangle<float> (x, y + 200.0f, 200.0f, 18.0f), juce::Justification::centredLeft, false);
        if (problem.isNotEmpty()) { g.setColour (l.danger); g.drawText (problem, juce::Rectangle<float> (x + 230.0f, y + 276.0f, 240.0f, 32.0f), juce::Justification::centredLeft, true); }
    }
    void mouseDown (const juce::MouseEvent& e) override { if (! panel.contains (e.position)) close(); }
private:
    void commit()
    {
        sound.name = name.editor.getText().trim();
        if (sound.name.isEmpty()) { problem = "It needs a name"; repaint(); return; }
        sound.tags.clear();
        for (auto t : juce::StringArray::fromTokens (tags.editor.getText(), ",", "")) { t = t.trim().toLowerCase(); if (t.isNotEmpty()) sound.tags.addIfNotAlreadyThere (t); }
        if (sound.factory || sound.author.isEmpty() || sound.author == "Ghost Atlas") sound.author = juce::SystemStats::getFullUserName().upToFirstOccurrenceOf (" ", false, false);
        sound.factory = false;
        juce::File f;
        { const juce::ScopedLock sl (shell.proc.bankLock()); f = shell.proc.bank().saveUser (sound); }
        if (f == juce::File()) { problem = "It could not be written to the Presets folder"; repaint(); return; }
        sound.file = f.getFullPathName();
        shell.proc.loadPreset (sound);
        if (shell.say) shell.say ("Saved among your own sounds");
        close();
    }
    Shell& shell;
    Preset sound;
    TextBox name, tags; Choice kind; TextBtn save, cancel;
    bool asked = false;
    juce::Rectangle<float> panel;
    juce::String problem;
};
} // namespace

std::unique_ptr<Page> makeLibraryPage (Shell& s) { return std::make_unique<LibraryPage> (s); }
std::unique_ptr<Overlay> makeInstrumentPicker (Shell& s, int layer) { return std::make_unique<Picker> (s, layer); }
std::unique_ptr<Overlay> makeSaveDialog (Shell& s) { return std::make_unique<SaveDialog> (s); }
} // namespace atlas::ui
