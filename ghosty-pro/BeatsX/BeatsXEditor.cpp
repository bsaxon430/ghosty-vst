#include "BeatsXEditor.h"
#include "EditorParts.h"
#include "TrackerView.h"
#include "RhythmLab.h"
#include "MixPage.h"
#include "MelodyTrackerView.h"
#include "RecreatePanel.h"
#include "WaveRegion.h"
#include "GhostKitArt.h"   // GHOST KIT (brand): the mascot + dripping wordmark
#include "NeonCity.h"      // NEON look: the rainy city backdrop

using namespace bx;

namespace {
// the 4x4 pads
struct Pads : juce::Component {
    BeatsXProcessor& proc; uint32_t last[16] {}; float glow[16] {}; int downPad = -1;
    explicit Pads(BeatsXProcessor& p) : proc(p) {}
    juce::Rectangle<float> padRect(int i) const { const int col = i % 4, row = 3 - i / 4; const float w = getWidth() / 4.0f, h = getHeight() / 4.0f; return { col * w + 6, row * h + 6, w - 12, h - 12 }; }
    void paint(juce::Graphics& g) override {
        for (int i = 0; i < 16; ++i) {
            const uint32_t tc = proc.engine.trig[i].load(); if (tc != last[i]) { glow[i] = 1; last[i] = tc; } glow[i] *= 0.85f;
            auto r = padRect(i); const auto col = trackColour(i);
            if (GLASS) glassui::sprites().draw(g, r, 14.0f, glassui::Pad); else { g.setColour(CARD2); g.fillRoundedRectangle(r, 12); }   // GHOST: a glass pad (cached)
            g.setColour(col.withAlpha((GLASS ? 0.06f : 0.18f) + (GLASS ? 0.45f : 0.7f) * glow[i])); g.fillRoundedRectangle(r.reduced(3), 10);
            g.setColour(col.withAlpha(0.8f)); g.drawRoundedRectangle(r, 12, 1.6f);
            g.setColour(INK); g.setFont(font(13.0f)); g.drawFittedText(proc.model.tracks[(size_t) i].sound, r.reduced(8).toNearestInt(), juce::Justification::centred, 2);
            g.setColour(GREY); g.setFont(font(10.5f)); g.drawText("PAD " + juce::String(i + 1), r.reduced(10, 8), juce::Justification::topLeft);
        }
    }
    int padAt(juce::Point<float> p) const { for (int i = 0; i < 16; ++i) if (padRect(i).contains(p)) return i; return -1; }
    void mouseDown(const juce::MouseEvent& e) override {
        downPad = padAt(e.position); if (downPad < 0) return;
        const float vel = juce::jlimit(0.35f, 1.0f, 1.0f - (e.position.y - padRect(downPad).getY()) / padRect(downPad).getHeight() * 0.65f);   // higher on the pad = harder
        proc.engine.pushPad(downPad, vel); proc.engine.heldPads.fetch_or(1u << downPad);
    }
    void mouseUp(const juce::MouseEvent&) override { if (downPad >= 0) proc.engine.heldPads.fetch_and(~(1u << downPad)); downPad = -1; }
};

// SONG arranger: drag a pattern chip onto the bars, slide blocks, drag the right edge to repeat
struct SongLane : juce::Component, juce::DragAndDropTarget {
    Ed& ed; int selected = -1, drag = 0; gb::SongBlock orig; float downX = 0; bool pushed = false;
    static constexpr int kBars = 128; int view = 64, first = 0;   // bars shown / first bar shown (wheel scrolls, cmd+wheel zooms 16..128)
    explicit SongLane(Ed& e) : ed(e) {}
    float barW() const { return getWidth() / (float) view; }
    float xOf(int bar) const { return (bar - first) * barW(); }
    int barAt(float x) const { return juce::jlimit(0, kBars - 1, first + (int) std::floor(x / barW())); }
    void paint(juce::Graphics& g) override {
        auto R = getLocalBounds().toFloat();
        if (GLASS) ghosty::glass::recessed(g, R, 12.0f); else { g.setColour(CARD2); g.fillRoundedRectangle(R, 10); }
        for (int b = first; b <= first + view; ++b) { g.setColour(INK.withAlpha(b % 4 == 0 ? 0.2f : 0.06f)); g.drawVerticalLine((int) xOf(b), 0, R.getBottom());
            if (b % (view > 64 ? 16 : 8) == 0 && b < kBars) { g.setColour(GREY); g.setFont(font(10.0f)); g.drawText(juce::String(b + 1), (int) xOf(b) + 3, 2, 30, 12, juce::Justification::left); } }
        auto& song = ed.proc.model.song;
        for (int i = 0; i < (int) song.size(); ++i) {
            const auto& b = song[(size_t) i];
            auto r = juce::Rectangle<float>(xOf(b.startBar) + 1, 18, b.bars * barW() - 2, R.getHeight() - 26);
            const auto col = patColour(b.pattern);
            if (GLASS) ghosty::glass::body(g, r, 6.0f, col, 0.30f, 0.8f); else { g.setColour(col.withAlpha(0.35f)); g.fillRoundedRectangle(r, 5); }
            g.setColour(i == selected ? INK : col); g.drawRoundedRectangle(r, 5, i == selected ? 2.2f : 1.2f);
            g.setColour(INK); g.setFont(font(15.0f)); g.drawText(patName(b.pattern), r.reduced(4, 4), juce::Justification::centredLeft);
        }
        if (ed.proc.songModeOn()) { const int bar = ed.proc.engine.uiBar.load(); g.setColour(GREEN); g.fillRect(xOf(bar), 14.0f, 2.0f, R.getHeight() - 16); }
        g.setColour(GREY); g.setFont(font(9.5f, false)); g.drawText("bars " + juce::String(first + 1) + "-" + juce::String(first + view) + " of 128   wheel: scroll   cmd/ctrl+wheel: zoom", R.reduced(8, 2), juce::Justification::bottomRight);
        if (song.empty()) { g.setColour(GREY); g.setFont(font(13.0f, false)); g.drawFittedText("Drag a pattern letter from the left onto the bars. Slide blocks to rearrange, drag a block's right edge to repeat it. RHYTHM LAB > BAKE fills this with an evolving performance.", getLocalBounds().reduced(20), juce::Justification::centred, 2); }
    }
    int blockAt(float x) const { const int bar = barAt(x); auto& s = ed.proc.model.song; for (int i = (int) s.size() - 1; i >= 0; --i) if (bar >= s[(size_t) i].startBar && bar < s[(size_t) i].startBar + s[(size_t) i].bars) return i; return -1; }
    void mouseDown(const juce::MouseEvent& e) override {
        selected = blockAt(e.position.x); downX = e.position.x; drag = 0; pushed = false;
        if (selected >= 0) { orig = ed.proc.model.song[(size_t) selected]; const float right = xOf(orig.startBar + orig.bars); drag = right - e.position.x < 10 ? 2 : 1; }
        repaint();
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (selected < 0 || drag == 0) return;
        auto& b = ed.proc.model.song[(size_t) selected]; const int d = (int) std::round((e.position.x - downX) / barW());
        const gb::SongBlock nb { orig.pattern, drag == 1 ? juce::jlimit(0, kBars - 1, orig.startBar + d) : orig.startBar, drag == 2 ? juce::jlimit(1, kBars, orig.bars + d) : orig.bars };
        if (nb.startBar == b.startBar && nb.bars == b.bars) return;
        if (!pushed) { ed.proc.pushUndo(); pushed = true; }   // one undo step, only when something moved
        b = nb; repaint();
    }
    void mouseUp(const juce::MouseEvent&) override { if (pushed) ed.live(); drag = 0; }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override {
        if (e.mods.isCommandDown()) { const int nv = juce::jlimit(16, kBars, (int) std::lround(view * (w.deltaY > 0 ? 0.8 : 1.25))); const int anchor = barAt(e.position.x); view = nv; first = juce::jlimit(0, kBars - view, anchor - (int) (e.position.x / barW())); }
        else first = juce::jlimit(0, kBars - view, first - (int) std::round((w.deltaY != 0 ? w.deltaY : w.deltaX) * 8));
        repaint();
    }
    bool isInterestedInDragSource(const SourceDetails& d) override { return d.description.toString().startsWith("pattern:"); }
    void itemDropped(const SourceDetails& d) override {
        const int p = juce::jlimit(0, gb::kPatterns - 1, d.description.toString().fromFirstOccurrenceOf(":", false, false).getIntValue());
        const int bar = barAt((float) d.localPosition.x);
        ed.edit([&] { if (ed.proc.model.song.size() < 128) ed.proc.model.song.push_back({ p, bar, ed.proc.model.patterns[(size_t) p].bars() }); });
        selected = (int) ed.proc.model.song.size() - 1; repaint();
    }
};

struct PatternChip : juce::Component {
    int p; explicit PatternChip(int i) : p(i) {}
    void paint(juce::Graphics& g) override { auto r = getLocalBounds().toFloat().reduced(2);
        if (GLASS) { ghosty::glass::shadow(g, r, 9.0f, 3.0f, 1.0f, 3); ghosty::glass::body(g, r, 9.0f, patColour(p), 0.30f, 0.8f); }
        else { g.setColour(patColour(p).withAlpha(0.4f)); g.fillRoundedRectangle(r, 7); } g.setColour(patColour(p)); g.drawRoundedRectangle(r, GLASS ? 9.0f : 7.0f, 1.4f);
        g.setColour(INK); g.setFont(font(16.0f)); g.drawText(patName(p), r, juce::Justification::centred); }
    void mouseDrag(const juce::MouseEvent& e) override { if (e.getDistanceFromDragStart() > 6) if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor(this)) c->startDragging("pattern:" + juce::String(p), this); }
};

// SCRIPTS: the modules of this beat (click = edit, the box = on / off)
struct ScriptList : juce::Component {
    Ed& ed; int sel = 0; std::function<void(int)> onSelect;
    explicit ScriptList(Ed& e) : ed(e) {}
    void paint(juce::Graphics& g) override {
        auto& sc = ed.proc.model.scripts;
        for (int i = 0; i < (int) sc.size(); ++i) { auto r = juce::Rectangle<float>(0, i * 32.0f, (float) getWidth(), 29);
            if (GLASS) { glassui::sprites().draw(g, r, 7.0f, glassui::Row); if (i == sel) { g.setColour(PURPLE.withAlpha(0.22f)); g.fillRoundedRectangle(r, 7.0f); } }
            else { g.setColour(i == sel ? PURPLE.withAlpha(0.45f) : CARD2); g.fillRoundedRectangle(r, 6); }
            auto box = r.removeFromLeft(26).reduced(6); g.setColour(sc[(size_t) i].on ? GREEN : DIM); g.fillRoundedRectangle(box, 3);
            g.setColour(INK); g.setFont(font(12.0f)); g.drawText(sc[(size_t) i].name, r.reduced(4, 0), juce::Justification::centredLeft, true); }
        if (sc.empty()) { g.setColour(GREY); g.setFont(font(11.5f, false)); g.drawFittedText("No modules yet. NEW starts one, LOAD MODULE has 6 examples.", getLocalBounds().reduced(6).withHeight(60), juce::Justification::topLeft, 3); }
    }
    void mouseDown(const juce::MouseEvent& e) override {
        auto& sc = ed.proc.model.scripts; const int i = e.y / 32; if (!juce::isPositiveAndBelow(i, (int) sc.size())) return;
        if (e.x < 26) { ed.edit([&] { sc[(size_t) i].on = !sc[(size_t) i].on; }); repaint(); return; }
        sel = i; if (onSelect) onSelect(i); repaint();
    }
};

// a folder list for the sound browser
struct FolderList : juce::Component, juce::DragAndDropTarget {
    juce::StringArray items; int selected = 0, dropRow = -1; std::function<void(int)> onPick;
    std::function<bool(int)> canDrop; std::function<void(int, const juce::String&)> onDropSound;   // MY BANKS: drag a sound onto your bank (or + NEW BANK) = copy it there
    int rowAt(int y) const { const int i = y / 30; return i >= 0 && i < items.size() ? i : -1; }
    bool isInterestedInDragSource(const SourceDetails& d) override { return d.description.toString().startsWith("sound:") && canDrop; }
    void itemDragMove(const SourceDetails& d) override { const int r = rowAt(d.localPosition.y), v = r >= 0 && canDrop(r) ? r : -1; if (v != dropRow) { dropRow = v; repaint(); } }
    void itemDragExit(const SourceDetails&) override { dropRow = -1; repaint(); }
    void itemDropped(const SourceDetails& d) override { const int r = dropRow; dropRow = -1; repaint(); if (r >= 0 && onDropSound) onDropSound(r, d.description.toString().fromFirstOccurrenceOf("sound:", false, false)); }
    void paint(juce::Graphics& g) override {
        for (int i = 0; i < items.size(); ++i) { auto r = juce::Rectangle<float>(0, i * 30.0f, (float) getWidth(), 27);
            if (GLASS) { glassui::sprites().draw(g, r, 7.0f, glassui::Row); if (i == selected) { g.setColour(PURPLE.withAlpha(0.22f)); g.fillRoundedRectangle(r, 7.0f); } }
            else { g.setColour(i == selected ? PURPLE.withAlpha(0.5f) : CARD2); g.fillRoundedRectangle(r, 6); }
            if (i == dropRow) { g.setColour(GREEN.withAlpha(0.25f)); g.fillRoundedRectangle(r, 6); g.setColour(GREEN); g.drawRoundedRectangle(r.reduced(0.5f), 6, 1.5f); }
            g.setColour(items[i].startsWith("+") ? GREEN : INK); g.setFont(font(12.0f)); g.drawFittedText(items[i], r.reduced(8, 0).toNearestInt(), juce::Justification::centredLeft, 1, 0.5f); }   // long bank names squeeze to fit instead of "..."
    }
    void mouseDown(const juce::MouseEvent& e) override { const int i = e.y / 30; if (i >= 0 && i < items.size()) { selected = i; repaint(); if (onPick) onPick(i); } }
};
} // namespace

// =====================================================================================================
struct BeatsXEditor::Content : juce::Component {
    BeatsXProcessor& proc; BeatsXEditor& ed_; Ed ed { proc };
    ghosty::UnlockButton unlock { proc.license };
    int page = 0; std::vector<gb::Step> trackClip; int trackClipLen = 0; int copiedPattern = -1;
    // header
    juce::ComboBox kitBox, grooveBox, lenBox, resBox, keyBox;   // keyBox: SONG KEY (key-matched drums)
    juce::TextButton playBtn { "PLAY" }, undoBtn { "UNDO" }, redoBtn { "REDO" }, patBtn { "PATTERN" }, songBtn { "SONG" }, mixBtn { "MIX" }, stemsBtn { "STEMS" }, chBtn { "CH 1" };
    juce::TextButton copyPat { "COPY" }, pastePat { "PASTE" }, clearPat { "CLEAR" }, songModeBtn { "SONG MODE" }, themeBtn { "PURPLE" }, focusBtn { "FOCUS" };
    DragOut midiOut { "DRAG MIDI", true }, audioOut { "DRAG AUDIO", true };
    ghosty::BypassButton bypassBtn { *proc.bypassParam };   // top-right, every GhostBlock plugin (Shared/Bypass.h)
    juce::TooltipWindow tips { this, 700 };
    struct VersionTip : juce::Component, juce::SettableTooltipClient { std::function<void()> onClick; void mouseUp(const juce::MouseEvent& e) override { if (onClick && contains(e.getPosition())) onClick(); } } versionTip;   // GHOST KIT (brand): click = the about card   // hover the wordmark: product + version                 // hover help (themed: a child of the editor)
    // GHOST KIT (brand): the about / splash card (click the wordmark; click anywhere or ESC closes it)
    struct AboutCard : juce::Component, private juce::Timer {
        float t = 0;
        void show() { if (auto* p = getParentComponent()) setBounds(p->getLocalBounds()); setWantsKeyboardFocus(true); setVisible(true); toFront(true); grabKeyboardFocus(); startTimerHz(24); }
        void hide() { stopTimer(); setVisible(false); }
        void timerCallback() override { t += 1.0f / 24.0f; repaint(); }
        juce::Rectangle<float> card() const { const float w = juce::jmin((float) getWidth() - 80.0f, 800.0f); return juce::Rectangle<float>(w, w * 9.0f / 16.0f).withCentre(getLocalBounds().toFloat().getCentre()); }
        void paint(juce::Graphics& g) override {
            g.fillAll(SHADOW.withAlpha(0.6f)); const auto c = card();
            juce::Graphics::ScopedSaveState ss(g); juce::Path clip; clip.addRoundedRectangle(c, 16.0f); g.reduceClipRegion(clip);
            g.setGradientFill(juce::ColourGradient(juce::Colour(0xff1C0B2E), c.getX(), c.getY(), juce::Colour(0xff07040C), c.getX(), c.getBottom(), false)); g.fillRect(c);   // GHOSTY BEATS: the logo artwork, big
            drawLogo(g, juce::RectanglePlacement(juce::RectanglePlacement::centred).appliedTo(logoArt().getBounds().toFloat(), c.reduced(c.getWidth() * 0.08f, c.getHeight() * 0.18f)));
            g.setColour(juce::Colour(0xffE9E3F5)); g.setFont(font(12.0f, false));
            g.drawText(juce::String("v") + BeatsXProcessor::kVersion + "  by GhostBlock  -  click to close", c.reduced(18, 10).removeFromBottom(18), juce::Justification::centredRight);
            g.setColour(PURPLE.withAlpha(0.7f)); g.drawRoundedRectangle(c.reduced(0.5f), 16.0f, 1.5f);
        }
        void mouseUp(const juce::MouseEvent&) override { hide(); }
        bool keyPressed(const juce::KeyPress& k) override { if (k == juce::KeyPress::escapeKey) { hide(); return true; } return false; }
    } about;
    // GHOSTY BEATS (brand, owner 2026-10-30: "make Ghosty Beats Ghosty Beats again"; GHOST KIT = its own drum sounds): the standalone window name
    void parentHierarchyChanged() override {
        if (proc.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
            if (auto* top = getTopLevelComponent(); top != nullptr && top != this && dynamic_cast<juce::AudioProcessorEditor*>(top) == nullptr) top->setName("GHOSTY BEATS");
    }
    RecreatePanel recreate { ed }; DropOverlay dropOverlay;   // RE-CREATE AS MIDI (drop a loop on the grid) + the drop-zone hint while dragging
    juce::TextButton trkBtn { "XM / IT..." };                 // MELODY page: tracker modules in / out (TrackerFiles.h)
    juce::TextButton loopBtn { juce::String(juce::CharPointer_UTF8("LOOP \xe2\x86\x92 MIDI...")) };
    PlayModeSwitch playModeSw;                              // PLAY: ALWAYS | ON NOTES, under BYPASS (ROADMAP 16)
    ghosty::tempomul::Switch tempoSw;                       // TEMPO ½x | 1x | 2x (Shared/TempoMul.h)
    ghosty::SyncButton syncBtn { proc.sync };               // SYNC: DAW | OWN (Shared/SyncTransport.h)
    juce::TextButton tapBtn { "TAP" }; double tapTimes[5] {}; int nTaps = 0;
    int syncUiState = -1;                                   // 0 = DAW drives (tempo read-only), 1 = OWN, 2 = no DAW (standalone: PLAY + BPM parameter, as always)
    Bar bpm;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bpmAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> grooveAtt, repeatAtt;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>> btnAtts;
    juce::OwnedArray<juce::TextButton> patBtns, pageBtns;
    juce::Label status;
    static constexpr int kPages = 10;
    std::vector<juce::Component*> pageComps[kPages], largeOnly, focusHidden, bottomComps;
    int lw = W, lh = H; bool large = false;   // layout size (logical pixels) and the LARGE layout switch
    th::Theme myTheme = th::Theme::Dark;      // the palette this editor's children were coloured with
    int myLook = 0;                           // ... and the look (0..2 = the theme, 3 = GHOST)
    ghosty::glass::Backdrop backdrop;         // GHOST: the aurora, rendered once per size
    ghosty::glass::PanelCache panels;         // GHOST: the big glass cards, cached per size
    neoncity::City city;                      // NEON: the rainy city, rendered once per size
    neoncity::Logo neonLogo;                  // NEON: GHOSTY BEATS as a neon sign
    sumi::Paper paperBg;                      // LIGHT: the washi paper, ink and watercolour, rendered once per size
    int neonTick = 0;
    bool laidOutFocus = false;                // the FOCUS state the last resized() used
    bool focusOn() const { return proc.focusMode && page == 0; }   // FOCUS: the sequencer page shows only tracks + grid + lanes, as big as the window
    MixPage mix { ed };
    MelodyTrackerView melView; DragOut melOut { "DRAG MELODY MIDI", false }, melBoth { "DRAG BEAT + MELODY MIDI", false }; juce::Label melHelp;   // MELODIC TRACKER (MelodyTracker.h)
    // scripts
    ScriptList scriptList { ed }; juce::TextEditor scriptName, scriptCode; juce::Label scriptErr, scriptHelp;
    juce::TextButton scrNew { "NEW" }, scrApply { "APPLY" }, scrDel { "DELETE" }, scrSave { "SAVE MODULE" }, scrLoad { "LOAD MODULE" };
    // large layout extras
    LaneEditor lanesVel { ed }; TrackerView trackerMini { ed };
    // sequencer
    TrackList tl0 { ed }, tl1 { ed }, tl2 { ed }; SliceView slices { ed };
    // SAMPLE REGION editor (Shared/WaveRegion.h) for the selected track's sound, sharing the slot with SLICES (two tabs)
    ghosty::WaveRegion region; juce::TextButton regTab { "REGION" }, sliceTab { "SLICES" }; bool showSlices = false; const void* regionFor = nullptr; int regionTrack = -1;
    StepGrid grid { ed }; LaneEditor lanes { ed };
    juce::OwnedArray<juce::TextButton> laneBtns, toolBtns; juce::TextButton pagePrev { "<" }, pageNext { ">" }; juce::Label pageLbl;
    juce::ComboBox limiterBox; std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> limiterAtt;
    juce::ComboBox charBox; juce::OwnedArray<Knob> charKnobs;   // CHARACTER: old-sampler colour per track (Shared/VintageCharacter.h)
    juce::Component::SafePointer<juce::AlertWindow> saveDlg; int fractureTick = 0;
    Bar density;
    juce::Label trackTitle, soundName;
    juce::TextButton prevSnd { "<" }, nextSnd { ">" }, browseBtn { "BROWSE" }, loadSmp { "LOAD SAMPLE" }, revBtn { "REVERSE" }, lockBtn { "LOCK" }, chopBtn { "CHOP" }, rearrBtn { "REARRANGE" };
    juce::ComboBox tLenBox, chokeBox; Bar chopSens;
    juce::OwnedArray<Knob> trackKnobs;
    // sounds
    FolderList folders; juce::Viewport folderView; juce::TextEditor search; juce::Viewport soundView; SoundList sounds { ed };   // folderView: the folder list scrolls inside its column (owner 2026-11-01: it ran over the MASTER strip)
    juce::TextButton loadOnTrack { "LOAD ON SELECTED TRACK" }, loadFile { "LOAD A FILE..." }, normBtn { "NORMALISE DROPPED SAMPLES" };
    juce::TextButton myBanksBtn { "MY BANKS..." }, genBanksBtn { "SOUNDS FROM: AUTO" }, useBankBtn { "USE THIS BANK" };   // MY BANKS (Shared/UserBanks.h)
    std::unique_ptr<juce::FileChooser> bankChooser; juce::AudioFormatManager bankFm;
    juce::Label soundInfo, dropHelp;
    // process
    juce::OwnedArray<Knob> fxKnobs, layerKnobs, busKnobs; juce::OwnedArray<juce::TextButton> layerBtns; juce::ComboBox scBox;
    juce::OwnedArray<juce::ComboBox> ruleType, ruleSrc, ruleN; juce::OwnedArray<Bar> rulePct; juce::Label procTitle, rulesTitle, layersTitle, busTitle;
    // tracker, lab, pads, song
    TrackerView tracker { ed }; juce::TextButton bank1 { "TRACKS 1-8" }, bank2 { "TRACKS 9-16" }; juce::ComboBox previewBox; juce::Label trackerHelp;
    RhythmLab lab { ed };
    Pads pads { proc }; juce::TextButton recBtn { "REC" }, repeatBtn { "NOTE REPEAT" }; juce::ComboBox repeatDiv; juce::Label padHelp;
    SongLane lane { ed }; juce::OwnedArray<PatternChip> chips; juce::TextButton delBlock { "DELETE BLOCK" }, dupBlock { "DUPLICATE" }, clrSong { "CLEAR SONG" }; juce::Label songHelp;
    // bottom
    juce::OwnedArray<Knob> masterKnobs, macroKnobs; juce::OwnedArray<HoldButton> perf; StutterStrip stutter; juce::Label stutterLbl; juce::TextButton saveBeatBtn { "SAVE BEAT" }, loadBeatBtn { "LOAD BEAT" };
    std::unique_ptr<juce::FileChooser> chooser;

    void add(int pg, juce::Component& c) { addAndMakeVisible(c); if (pg >= 0) pageComps[pg].push_back(&c); }
    ~Content() override {   // popups never outlive the editor; attachments go before the buttons / boxes they listen to (some are declared after them)
        if (ed.stepBox) { ed.stepBox->setLookAndFeel(nullptr); ed.stepBox->dismiss(); } if (saveDlg) { saveDlg->setLookAndFeel(nullptr); saveDlg->exitModalState(0); } chooser.reset();
        btnAtts.clear(); repeatAtt.reset(); grooveAtt.reset(); bpmAtt.reset(); limiterAtt.reset();
    }

    Content(BeatsXProcessor& p, BeatsXEditor& e) : proc(p), ed_(e) {
        setWantsKeyboardFocus(true);
        ed.refresh = [this] { refreshAll(); };
        ed.say = [this](const juce::String& s) { status.setText(s, juce::dontSendNotification); };
        // ---------------- header
        for (auto& k : gb::allKits()) kitBox.addItem(juce::String(k.name) + " kit", kitBox.getNumItems() + 1);
        kitBox.setSelectedItemIndex(proc.currentKit, juce::dontSendNotification);
        kitBox.onChange = [this] { if (proc.kitSource != "stock" && ! proc.builtinBanksLoaded) ed.say("Loading the banks...");
            ed.edit([&] { proc.applyKit(kitBox.getSelectedItemIndex(), true); proc.applySource(); }); refreshAll(); updGenBtn();
            ed.say("Loaded the " + kitBox.getText() + ": patterns A and B are a starter beat. Sounds from " + proc.sourceLabel() + "."); };
        add(-1, kitBox);
        // SONG KEY (owner 2026-10-30): drums with a key in their name follow it (picks + a swap to the same pack's in-key sound)
        keyBox.addItem("KEY: OFF", 1); for (int q = 0; q < 2; ++q) for (int r = 0; r < 12; ++r) keyBox.addItem(juce::String(ghosty::keys::noteName(r)) + (q == 0 ? " MIN" : " MAJ"), 2 + q * 12 + r);
        keyBox.setTooltip("SONG KEY: drums that have a key in their name (like CHIMAERA's kicks) follow it: GENERATE, USE THIS BANK and AUTO pick the ones in this key, and changing the key swaps them to the same pack's sound in the new key");
        keyBox.onChange = [this] { const int id = keyBox.getSelectedId(); int n = 0;
            ed.edit([&] { n = id <= 1 ? proc.setSongKey(-1, true) : proc.setSongKey((id - 2) % 12, id < 14); }); refreshAll();
            ed.say(id <= 1 ? juce::String("Key off: drums are picked from every key") : proc.songKeyLabel().fromFirstOccurrenceOf(": ", false, false) + ": " + (n > 0 ? juce::String(n) + " drum" + (n > 1 ? "s" : "") + " swapped to the same pack's sound in this key" : juce::String("drums with a key in their name are picked in this key"))); };
        add(-1, keyBox);
        playBtn.setClickingTogglesState(true); add(-1, playBtn);
        playBtn.onClick = [this] { if (proc.ownClock()) proc.sync.toggleOwn(0.0); else proc.engine.internalPlay = playBtn.getToggleState(); };   // OWN: starts at the pattern / song start
        add(-1, syncBtn); syncBtn.setTheme(THEME); add(-1, tapBtn);
        add(-1, tempoSw); tempoSw.setTheme(THEME); tempoSw.set(proc.tempoMul.load(), dawTempo());
        tempoSw.onChange = [this](int c) { proc.tempoMul.store(c); proc.updateHostDisplay(juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged(true)); updateSyncUi(true);
            ed.say("TEMPO " + ghosty::tempomul::multName(c) + ": " + ghosty::tempomul::bpmText(proc.effectiveBpm()) + " bpm (locked to the DAW's bars)."); };
        syncBtn.onModeChanged = [this] {
            if (proc.ownClock()) { proc.engine.internalPlay = false; ed.say("SYNC: OWN. Beats runs by itself: its own tempo, its own PLAY (or SPACE). The DAW's transport and tempo are ignored."); }
            else { proc.sync.stopOwn(); ed.say("SYNC: DAW. The DAW's play button, tempo and position drive Beats again (SPACE goes to the DAW)."); }
            proc.updateHostDisplay(juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged(true)); updateSyncUi(true); };
        tapBtn.setTooltip("TAP the tempo (4 taps or more)"); tapBtn.onClick = [this] { tap(); };
        bpm.setRange(60, 200, 1); bpmAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, "bpm", bpm);
        bpm.setColour(juce::Slider::trackColourId, PURPLE.withAlpha(0.5f)); bpm.textFromValueFunction = [this](double v) { return juce::String(juce::roundToInt(v)) + (syncUiState == 0 ? " from DAW" : " bpm"); };
        bpm.valueFromTextFunction = [](const juce::String& t) { return t.retainCharacters("0123456789.").getDoubleValue(); }; bpm.updateText(); add(-1, bpm);
        updateSyncUi(true);   // tempo box / PLAY for the current SYNC mode
        grooveBox.addItemList(gb::grooveNames(), 1); grooveAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc.apvts, "groove", grooveBox); add(-1, grooveBox);
        undoBtn.onClick = [this] { if (proc.undo()) { refreshAll(); ed.say("Undone."); } }; redoBtn.onClick = [this] { if (proc.redo()) { refreshAll(); ed.say("Redone."); } };
        add(-1, undoBtn); add(-1, redoBtn);
        auto job = [this](bool audio) { gb::EngineParams P; (void) P; BeatsXProcessor::AudioJob j; j.song = songBtn.getToggleState(); j.stems = stemsBtn.getToggleState(); (void) audio; return j; };
        midiOut.make = [this] { if (!proc.exportGate(&midiOut, "beats.drag-midi")) return juce::StringArray(); auto f = proc.exportMidi(songBtn.getToggleState()); return f.existsAsFile() ? juce::StringArray { f.getFullPathName() } : juce::StringArray(); };
        audioOut.prepare = [this, job] { proc.startAudioRender(job(true)); };
        audioOut.make = [this, job] { const auto j = job(true); if (!proc.exportGate(&audioOut, j.stems ? "beats.drag-stems" : "beats.drag-audio")) return juce::StringArray(); return proc.waitAudioRender(j); };
        audioOut.progress = [this] { return proc.renderProgress.load(); };
        add(-1, midiOut); add(-1, audioOut); add(-1, versionTip); versionTip.setTooltip(juce::String("GHOSTY BEATS ") + BeatsXProcessor::kVersion + " by GhostBlock" + (proc.license.isUnlocked() ? "" : " (demo)") + ". Click for the about card.");   // GHOST KIT (brand)
        versionTip.setMouseCursor(juce::MouseCursor::PointingHandCursor); versionTip.onClick = [this] { about.show(); }; addChildComponent(about);   // GHOST KIT (brand)
        add(-1, bypassBtn); bypassBtn.setTheme(THEME); add(-1, playModeSw);
        playModeSw.get = [this] { return proc.playMode.load(); };
        playModeSw.set = [this](int v) { proc.playMode.store(v); proc.updateHostDisplay(juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged(true));
                                          ed.say(v == 1 ? "PLAY: ON NOTES. The beat runs only while a MIDI note is held (draw one long note per bar). Notes no longer play the pads; the on-screen pads still do."
                                                        : "PLAY: ALWAYS. The beat runs whenever the host or PLAY runs; MIDI notes play the pads again."); };
        for (auto* b : { &patBtn, &songBtn, &mixBtn, &stemsBtn }) { b->setClickingTogglesState(false); add(-1, *b); b->getProperties().set("tint", (juce::int64) GREEN.getARGB()); }
        patBtn.onClick = [this] { patBtn.setToggleState(true, juce::dontSendNotification); songBtn.setToggleState(false, juce::dontSendNotification); subs(); };
        songBtn.onClick = [this] { songBtn.setToggleState(true, juce::dontSendNotification); patBtn.setToggleState(false, juce::dontSendNotification); subs(); };
        mixBtn.onClick = [this] { mixBtn.setToggleState(true, juce::dontSendNotification); stemsBtn.setToggleState(false, juce::dontSendNotification); subs(); };
        stemsBtn.onClick = [this] { stemsBtn.setToggleState(true, juce::dontSendNotification); mixBtn.setToggleState(false, juce::dontSendNotification); subs(); };
        patBtn.setToggleState(true, juce::dontSendNotification); mixBtn.setToggleState(true, juce::dontSendNotification);
        chBtn.onClick = [this] { proc.midiChannel = proc.midiChannel == 10 ? 1 : 10; proc.saveSettings(); subs(); }; add(-1, chBtn);
        for (int i = 0; i < gb::kPatterns; ++i) {
            auto* b = patBtns.add(new juce::TextButton(patName(i))); b->getProperties().set("tint", (juce::int64) patColour(i).getARGB());
            b->onClick = [this, i] { if (auto* pp = proc.apvts.getParameter("pattern")) pp->setValueNotifyingHost(pp->convertTo0to1((float) (i + 1))); ed.stepPage = 0; refreshAll(); };
            add(-1, *b);
        }
        for (int i = 1; i <= gb::kMaxSteps; ++i) lenBox.addItem(juce::String(i) + " steps", i);
        lenBox.onChange = [this] { const int L = lenBox.getSelectedId(); if (L > 0 && L != ed.pat().length) ed.edit([&] { ed.pat().length = L; ed.stepPage = 0; }); };
        resBox.addItemList(gb::resNames(), 1); resBox.onChange = [this] { const int r = resBox.getSelectedItemIndex(); if (r >= 0 && r != ed.pat().res) ed.edit([&] { ed.pat().res = r; }); };
        add(-1, lenBox); add(-1, resBox);
        copyPat.onClick = [this] { copiedPattern = ed.patIdx(); ed.say("Copied pattern " + patName(copiedPattern) + ". Pick another letter and PASTE."); };
        pastePat.onClick = [this] { if (copiedPattern < 0) return; ed.edit([&] { proc.model.patterns[(size_t) ed.patIdx()] = proc.model.patterns[(size_t) copiedPattern]; }); ed.say("Pasted into " + patName(ed.patIdx()) + "."); };
        clearPat.onClick = [this] { ed.edit([&] { auto& pt = ed.pat(); for (int t = 0; t < gb::kTracks; ++t) if (!proc.model.tracks[(size_t) t].lock) for (auto& st : pt.steps[(size_t) t]) if (!st.locked()) st = gb::Step(); }); };
        add(-1, copyPat); add(-1, pastePat); add(-1, clearPat);
        const char* pages[] = { "SEQUENCER", "SOUNDS", "PROCESS", "TRACKER", "RHYTHM LAB", "PADS", "SONG", "SCRIPTS", "MIX", "MELODY" };
        for (int i = 0; i < kPages; ++i) { auto* b = pageBtns.add(new juce::TextButton(pages[i])); b->onClick = [this, i] { setPage(i); }; add(-1, *b); }
        btnAtts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "songmode", songModeBtn)); songModeBtn.setClickingTogglesState(true); add(-1, songModeBtn);
        status.setFont(font(11.5f, false)); status.setColour(juce::Label::textColourId, GREEN); status.setColour(juce::Label::backgroundColourId, BG);   // (opaque: the header ghosts never pass under the text) add(-1, status);
        myTheme = THEME; myLook = LOOK; themeBtn.setButtonText(lookName(nextLook(LOOK)));
        if (NEON) { bypassBtn.setColours(CARD2, INK, AMBER, ON); syncBtn.setColours(CARD2, INK, CYAN, ON); tempoSw.setColours(CARD2, INK, CYAN, ON); }
        if (SUMI) { bypassBtn.setColours(CARD2, INK, RED, ON); syncBtn.setColours(CARD2, INK, PURPLE, ON); tempoSw.setColours(CARD2, INK, PURPLE, ON); }
        themeBtn.setTooltip("Switch the look: NEON (rainy 90s neon city) <-> LIGHT (ink + watercolour on rice paper) (the button names the other one)");
        themeBtn.onClick = [this] { setLook(nextLook(LOOK)); }; add(-1, themeBtn);
        focusBtn.setClickingTogglesState(true); focusBtn.setToggleState(proc.focusMode, juce::dontSendNotification); focusBtn.getProperties().set("tint", (juce::int64) AMBER.getARGB());
        focusBtn.onClick = [this] { proc.focusMode = focusBtn.getToggleState(); proc.saveSettings(); if (page != 0) setPage(0); else { setPage(0); resized(); } ed.say(proc.focusMode ? "FOCUS: just the beat (FOCUS again for everything)." : "Full view."); }; add(-1, focusBtn);
        addChildComponent(unlock); unlock.refresh();

        // ---------------- SEQUENCER
        add(0, tl0); add(0, grid); add(0, lanes);
        for (int i = 0; i < LaneEditor::names().size(); ++i) { auto* b = laneBtns.add(new juce::TextButton(LaneEditor::names()[i])); b->onClick = [this, i] { lanes.lane = i; refreshAll(); }; add(0, *b); }
        pagePrev.onClick = [this] { ed.stepPage = juce::jmax(0, ed.stepPage - 1); refreshAll(); }; pageNext.onClick = [this] { ed.stepPage = juce::jmin((maxTrackLen() - 1) / 16, ed.stepPage + 1); refreshAll(); };
        pageLbl.setFont(font(10.5f)); pageLbl.setJustificationType(juce::Justification::centred); pageLbl.setColour(juce::Label::textColourId, GREY); add(0, pagePrev); add(0, pageNext); add(0, pageLbl);
        const char* tools[] = { "FILL", "EVERY 2", "EVERY 4", "RANDOM", "<<", ">>", "REVERSE", "DOUBLE", "HALF", "COPY", "PASTE", "CLEAR" };
        for (int i = 0; i < 12; ++i) { auto* b = toolBtns.add(new juce::TextButton(tools[i])); b->onClick = [this, i] { trackTool(i); }; add(0, *b); }
        density.setRange(5, 100, 1); density.setValue(40); density.setDoubleClickReturnValue(true, 40); density.setTextValueSuffix(" % density"); density.setColour(juce::Slider::trackColourId, PURPLE.withAlpha(0.4f)); add(0, density);
        trackTitle.setFont(font(15.0f)); add(0, trackTitle);
        soundName.setFont(font(12.0f, false)); soundName.setColour(juce::Label::textColourId, GREY); add(0, soundName);
        prevSnd.onClick = [this] { stepSound(-1); }; nextSnd.onClick = [this] { stepSound(1); }; browseBtn.onClick = [this] { setPage(1); };
        loadSmp.getProperties().set("accent", true); loadSmp.onClick = [this] { pickFile(ed.track); };
        revBtn.setClickingTogglesState(true); revBtn.onClick = [this] { ed.edit([&] { ed.tr().reverse = revBtn.getToggleState(); }); };
        lockBtn.setClickingTogglesState(true); lockBtn.getProperties().set("tint", (juce::int64) AMBER.getARGB()); lockBtn.onClick = [this] { ed.edit([&] { ed.tr().lock = lockBtn.getToggleState(); }); };
        chopBtn.onClick = [this] { ed.edit([&] { proc.chopTrack(ed.track, (float) chopSens.getValue() / 100.0f, true); }); ed.say("Chopped into " + juce::String((int) ed.tr().slices.size()) + " slices, laid on the track."); };
        rearrBtn.onClick = [this] { if (ed.tr().slices.empty()) { ed.say("CHOP the track first."); return; } ed.edit([&] { proc.rearrangeTrack(ed.track); }); ed.say("New slice order, same groove."); };
        chopSens.setRange(0, 100, 1); chopSens.setValue(50); chopSens.setDoubleClickReturnValue(true, 50); chopSens.setTextValueSuffix(" % sens"); chopSens.setColour(juce::Slider::trackColourId, CYAN.withAlpha(0.4f));
        tLenBox.addItem("Length = pattern", 100); for (int i = 1; i <= gb::kMaxSteps; ++i) tLenBox.addItem("Length " + juce::String(i), i);
        tLenBox.onChange = [this] { const int id = tLenBox.getSelectedId(); if (id <= 0) return; ed.edit([&] { ed.pat().trackLen[(size_t) ed.track] = (uint8_t) (id == 100 ? 0 : id); }); };
        chokeBox.addItemList({ "No choke", "Choke 1", "Choke 2", "Choke 3", "Choke 4" }, 1); chokeBox.onChange = [this] { ed.edit([&] { ed.tr().choke = chokeBox.getSelectedItemIndex(); }); };
        for (juce::Component* c : std::initializer_list<juce::Component*> { &prevSnd, &nextSnd, &browseBtn, &loadSmp, &revBtn, &lockBtn, &chopBtn, &rearrBtn, &tLenBox, &chokeBox, &chopSens, &slices }) add(0, *c);
        const char* tk[] = { "PITCH", "DECAY", "VOLUME", "PAN", "TONE", "DRIVE", "SWING", "ACCENT", "HUMANIZE" };
        static const double lo[] = { -24, 0.05, 0, -1, 0, 0, 0, 0, 0 }, hi[] = { 24, 1, 2.0, 1, 1, 1, 1, 1, 1 }, def[] = { 0, 1, 0.8, 0, 0.5, 0, 0, 0, 0 };
        for (int i = 0; i < 9; ++i) {
            auto* k = trackKnobs.add(new Knob(tk[i])); add(0, *k);
            k->s.setRange(lo[i], hi[i], i == 0 ? 1.0 : 0.0); k->s.setDoubleClickReturnValue(true, def[i]);
            { static const Unit U[] = { Unit::Semis, Unit::Pct, Unit::Gain, Unit::Pan, Unit::Centred, Unit::Pct, Unit::Pct, Unit::Pct, Unit::Pct }; setUnits(k->s, U[i]); }
            k->s.onDragStart = [this] { proc.pushUndo(); };
            k->s.onValueChange = [this, i, k] { trackParam(i) = (float) k->s.getValue(); ed.live(); };
            k->s.onDragEnd = [this] { proc.auditionTrack(ed.track); };
        }
        for (auto* b : toolBtns) focusHidden.push_back(b); for (auto* k : trackKnobs) focusHidden.push_back(k);
        for (juce::Component* c : std::initializer_list<juce::Component*> { &density, &trackTitle, &soundName, &prevSnd, &nextSnd, &browseBtn, &loadSmp, &revBtn, &lockBtn, &chopBtn, &rearrBtn, &tLenBox, &chokeBox, &chopSens, &slices }) focusHidden.push_back(c);
        tl0.onDragOut = tl1.onDragOut = tl2.onDragOut = [this](int t, bool midi) { trackDrag(t, midi); };
        tl0.onMenu = tl1.onMenu = tl2.onMenu = [this](int t) { trackMenu(t); };

        // ---------------- SOUNDS
        add(1, tl1); proc.rescanBanks(); refreshFolders();
        folders.onPick = [this](int i) { const auto f = folders.items[i];
            if (f == kNewBank) { bankEdit([&](juce::var& L) { ghosty::userbanks::newBank(L, {}); return true; }, "New bank: drag sounds onto it, or drop files / folders from Finder."); if (! proc.myBanks.empty()) { const int k = folders.items.indexOf(myPrefix() + proc.myBanks.back().name); if (k >= 0) { folders.selected = k; folders.repaint(); fillSounds(); } } return; }
            if (f == "SHARED") proc.rescanShared(); if (f.startsWith(myPrefix()) && proc.rescanBanks() > 0) refreshFolders();
            if (kitFolderId(f).isNotEmpty() && ! proc.builtinBanksLoaded) { ed.say("Loading the Block Step + Ghost House banks..."); proc.loadBuiltinBanks(); }
            fillSounds(); updGenBtn(); };
        folders.canDrop = [this](int r) { return folders.items[r] == kNewBank || myFolderId(folders.items[r]).isNotEmpty(); };
        folders.onDropSound = [this](int r, const juce::String& n) { const auto id = myFolderId(folders.items[r]); if (id.isNotEmpty() && proc.myBankOf[n] == id) { ed.say("Already in that bank."); return; } copyToBank({ n }, id, {}); };
        folderView.setViewedComponent(&folders, false); folderView.setScrollBarsShown(true, false); folderView.setScrollBarThickness(6); add(1, folderView);
        search.setTextToShowWhenEmpty("search all sounds (kick, crack, 808, riser...)", GREY); search.onTextChange = [this] { fillSounds(); }; search.setFont(font(13.0f, false)); add(1, search);
        soundView.setViewedComponent(&sounds, false); soundView.setScrollBarsShown(true, false); add(1, soundView);
        sounds.onPick = [this](int i) { auto s = proc.lib->loaded(proc.lib->sounds()[(size_t) sounds.items[(size_t) i]]->name); if (! s) { soundInfo.setText("Can't read that sound.", juce::dontSendNotification); return; } soundInfo.setText(s->name + "\n" + gb::folderOf(s->category) + " / " + s->category + "\n" + juce::String(s->data.size() / s->rate, 2) + " s" + (s->beats > 0 ? "  (" + juce::String((int) s->beats) + "-beat loop: CHOPS automatically)" : juce::String())
            + (s->category == "Shared" ? "\nfrom Documents/Ghosty/Shared Sounds (Ghosty Spectrum's SEND TO BEATS lands here); on a track it is saved inside the project" : juce::String()) + "\n\nDouble-click or drag it onto a track.", juce::dontSendNotification); };
        loadOnTrack.getProperties().set("accent", true);
        loadOnTrack.onClick = [this] { if (sounds.selected < 0) { ed.say("Click a sound first."); return; } const auto n = sounds.nameAt(sounds.selected); const int t = ed.track; ed.edit([&] { proc.setTrackSound(t, n); }); ed.say("Track " + juce::String(t + 1) + " now plays " + n + "."); };
        loadFile.onClick = [this] { pickFile(ed.track); };
        normBtn.setClickingTogglesState(true); normBtn.setToggleState(proc.normaliseSamples, juce::dontSendNotification); normBtn.onClick = [this] { proc.normaliseSamples = normBtn.getToggleState(); proc.saveSettings(); };
        soundInfo.setFont(font(12.0f, false)); soundInfo.setJustificationType(juce::Justification::topLeft); soundInfo.setText("Click a sound to hear it.\nDouble-click (or drag it onto a track) to load it.\n* = favourite.", juce::dontSendNotification);
        dropHelp.setText("DROP A SAMPLE: drag any WAV / AIFF / FLAC / MP3 from Finder or straight from FL's browser onto a track row or a pad. Several files fill the next tracks; drop on an empty spot to use a free track. Long loops are chopped into slices automatically. Silence is trimmed; your samples are saved inside the project.", juce::dontSendNotification);
        dropHelp.setFont(font(11.5f, false)); dropHelp.setColour(juce::Label::textColourId, GREY); dropHelp.setJustificationType(juce::Justification::topLeft);
        for (juce::Component* c : std::initializer_list<juce::Component*> { &loadOnTrack, &loadFile, &normBtn, &soundInfo, &dropHelp }) add(1, *c);
        bankFm.registerBasicFormats();
        myBanksBtn.setTooltip("MY BANKS: your own drum banks, shared with Block Step and Ghost House (Music/GhostBlock Banks). Drop sounds or whole folders on the folder list (each folder becomes a bank), or use this menu to add, rename, delete and fix a sound's type.");
        genBanksBtn.setTooltip("GENERATE SOUNDS: pick banks here and every GENERATE also gives each unlocked track a new sound of the same kind from them. KEEP = GENERATE leaves your sounds alone.");
        myBanksBtn.onClick = [this] { bankMenu(); }; genBanksBtn.onClick = [this] { genMenu(); }; add(1, myBanksBtn); add(1, genBanksBtn);
        useBankBtn.getProperties().set("accent", true); add(1, useBankBtn);
        useBankBtn.setTooltip("USE THIS BANK: every track gets the closest sound of its kind from the bank you're looking at (a track keeps its sound when the bank has nothing like it). GENERATE and the styles keep using it.");
        useBankBtn.onClick = [this] { useSelectedBank(); };
        if (proc.freshBeat && ! proc.sourceApplied && proc.kitSource != "stock") proc.applySource();   // a new beat starts on the banks, not Ghost Kit's own sounds
        updGenBtn();

        // ---------------- PROCESS
        add(2, tl2);
        const char* fk[] = { "TRANSIENT", "SUSTAIN", "SATURATE", "PUNCH", "LOW CUT", "TILT", "HIGH CUT", "SC DUCK", "AMP ATT ms", "GLIDE ms", "PITCH ENV", "ENV ms", "VEL>LEVEL" };
        static const double flo[] = { -1, -1, 0, 0, 0, -1, 0, 0, 0, 0, -24, 1, 0 }, fhi[] = { 1, 1, 1, 1, 1, 1, 1, 1, 200, 500, 24, 1000, 1 }, fdef[] = { 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 60, 1 };
        for (int i = 0; i < 13; ++i) { auto* k = fxKnobs.add(new Knob(fk[i])); add(2, *k); k->s.setRange(flo[i], fhi[i], i >= 8 && i <= 11 ? 1.0 : 0.0); k->s.setDoubleClickReturnValue(true, fdef[i]);
            { static const Unit U[] = { Unit::BiPct, Unit::BiPct, Unit::Pct, Unit::Pct, Unit::Pct, Unit::BiPct, Unit::Pct, Unit::Pct, Unit::Ms, Unit::Ms, Unit::Semis, Unit::Ms, Unit::Pct }; setUnits(k->s, U[i]); }
            k->s.setSkewFactorFromMidPoint(i == 9 ? 80.0 : (i == 11 ? 150.0 : (i == 8 ? 30.0 : (flo[i] + fhi[i]) * 0.5)));
            k->s.onDragStart = [this] { proc.pushUndo(); }; k->s.onValueChange = [this, i, k] { fxParam(i) = (float) k->s.getValue(); ed.live(); }; k->s.onDragEnd = [this] { proc.auditionTrack(ed.track); }; }
        scBox.addItem("Sidechain: first kick", 100); for (int t = 0; t < gb::kTracks; ++t) scBox.addItem("Sidechain: track " + juce::String(t + 1), t + 1);
        scBox.onChange = [this] { const int id = scBox.getSelectedId(); ed.edit([&] { proc.model.scTrack = id == 100 ? -1 : id - 1; }); }; add(2, scBox);
        for (int l = 0; l < gb::kLayers; ++l) {
            auto* b = layerBtns.add(new juce::TextButton("LAYER " + juce::String(l + 1) + ": none")); b->onClick = [this, l] { layerMenu(l); }; add(2, *b);
            const char* lk[] = { "LEVEL", "PITCH", "OFFSET ms" }; static const double llo[] = { 0, -24, 0 }, lhi[] = { 1.5, 24, 50 };
            for (int j = 0; j < 3; ++j) { auto* k = layerKnobs.add(new Knob(lk[j])); add(2, *k); k->s.setRange(llo[j], lhi[j], j == 0 ? 0.0 : 1.0); k->s.setDoubleClickReturnValue(true, j == 0 ? 0.7 : 0.0); setUnits(k->s, j == 0 ? Unit::Gain : j == 1 ? Unit::Semis : Unit::Ms);
                k->s.onDragStart = [this] { proc.pushUndo(); }; k->s.onValueChange = [this, l, j, k] { auto& L = ed.tr().layers[(size_t) l]; (j == 0 ? L.level : (j == 1 ? L.pitch : L.offset)) = (float) k->s.getValue(); ed.live(); }; }
        }
        const char* bk[] = { "GLUE", "CLIPPER", "ROOM", "FILTER RESO", "CRUSH", "CEILING dB" }; const char* bid[] = { "glue", "clip", "room", "res", "crush", "ceiling" };
        for (int i = 0; i < 6; ++i) { auto* k = busKnobs.add(new Knob(bk[i])); k->bind(proc.apvts, bid[i]); add(2, *k); }
        for (int i = 0; i < vc::kNumModes; ++i) charBox.addItem(juce::String("Character: ") + vc::kModeNames[i], i + 1);
        charBox.onChange = [this] { const int m = charBox.getSelectedItemIndex(); if (m >= 0 && m != ed.tr().character) { ed.edit([&] { ed.tr().character = m; }); proc.auditionTrack(ed.track); } }; add(2, charBox);
        { const char* ck[] = { "AMOUNT", "DRIVE" }; for (int j = 0; j < 2; ++j) { auto* k = charKnobs.add(new Knob(ck[j])); add(2, *k); k->s.setRange(0, 1, 0); k->s.setDoubleClickReturnValue(true, j == 0 ? 1.0 : 0.0); setUnits(k->s, Unit::Pct);
            k->s.onDragStart = [this] { proc.pushUndo(); }; k->s.onValueChange = [this, j, k] { (j == 0 ? ed.tr().charAmount : ed.tr().charDrive) = (float) k->s.getValue(); ed.live(); }; k->s.onDragEnd = [this] { proc.auditionTrack(ed.track); }; } }
        limiterBox.addItemList({ "Limiter: CLEAN (look-ahead)", "Limiter: SOFT (v1 tanh)", "Limiter: OFF (mix in the DAW)" }, 1); limiterAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc.apvts, "limiter", limiterBox); add(2, limiterBox);
        for (int r = 0; r < 4; ++r) {
            auto* ty = ruleType.add(new juce::ComboBox()); ty->addItemList(gb::ruleNames(), 1); auto* src = ruleSrc.add(new juce::ComboBox()); fillRuleSrc(*src, 0);
            auto* n = ruleN.add(new juce::ComboBox()); for (int v = 1; v <= 16; ++v) n->addItem("N = " + juce::String(v), v);
            auto* pc = rulePct.add(new Bar()); pc->setRange(0, 100, 1); pc->setTextValueSuffix(" %"); pc->setColour(juce::Slider::trackColourId, PURPLE.withAlpha(0.4f));
            auto upd = [this, r] { ed.edit([&] { auto& R = ed.tr().rules[(size_t) r]; R.type = (uint8_t) juce::jmax(0, ruleType[r]->getSelectedItemIndex()); R.src = (uint8_t) juce::jmax(0, ruleSrc[r]->getSelectedId() - 1); R.n = (uint8_t) juce::jmax(1, ruleN[r]->getSelectedId()); R.pct = (uint8_t) rulePct[r]->getValue(); }); fillRuleSrc(*ruleSrc[r], ed.tr().rules[(size_t) r].type); ruleSrc[r]->setSelectedId(ed.tr().rules[(size_t) r].src + 1, juce::dontSendNotification); };
            ty->onChange = upd; src->onChange = upd; n->onChange = upd; pc->setDoubleClickReturnValue(true, 50);
            pc->onDragStart = [this] { proc.pushUndo(); };   // (Bar: once per gesture) then the % follows live
            pc->onValueChange = [this, r] { ed.tr().rules[(size_t) r].pct = (uint8_t) rulePct[r]->getValue(); ed.live(); };
            for (juce::Component* c : std::initializer_list<juce::Component*> { ty, src, n, pc }) add(2, *c);
        }
        for (auto [l, t] : { std::pair<juce::Label*, const char*> { &procTitle, "TRACK PROCESSING: TRANSIENT / SATURATE / PUNCH / EQ / SIDECHAIN  |  ENVELOPES: AMP ATTACK, GLIDE (808 legato), PITCH ENV, VELOCITY > LEVEL" }, { &layersTitle, "LAYERS: stack up to 2 more sounds on this track (kick = punch layer + sub layer)   |   CHARACTER: play this track through an old sampler" },
                                { &busTitle, "BUS (all tracks): GLUE comp, CLIPPER, ROOM, output CEILING + LIMITER" }, { &rulesTitle, "CONDITIONAL RULES for this track (seeded, checked every step)" } }) { l->setText(t, juce::dontSendNotification); l->setFont(font(11.5f)); l->setColour(juce::Label::textColourId, GREY); add(2, *l); }

        // ---------------- TRACKER
        add(3, tracker);
        bank1.onClick = [this] { tracker.bank = 0; refreshAll(); }; bank2.onClick = [this] { tracker.bank = 1; refreshAll(); }; add(3, bank1); add(3, bank2);
        previewBox.addItem("Edit the pattern", 100); for (int b = 1; b <= 64; ++b) previewBox.addItem("Preview evolved bar " + juce::String(b), b);
        previewBox.onChange = [this] { const int id = previewBox.getSelectedId(); tracker.previewBar = id == 100 || id <= 0 ? -1 : id - 1; if (tracker.previewBar >= 0) tracker.previewSnap = proc.makeSpec({ false, false, -1 }).snap; tracker.row = juce::jmin(tracker.row, tracker.rowsShown() - 1); tracker.repaint(); };
        previewBox.setSelectedId(100, juce::dontSendNotification); add(3, previewBox);
        trackerHelp.setText("keys: arrows  Tab  0-9 velocity  R hits  T retrig/beat  A accel  V vel ramp  P pitch  F cents  G pitch ramp  D delay  L flam  E decay  O start  C cut  S slice  X destroy  Y chance  B reverse  Enter  Del  shift+arrows + Cmd/Ctrl C/V", juce::dontSendNotification);
        trackerHelp.setFont(font(10.5f, false)); trackerHelp.setColour(juce::Label::textColourId, GREY); add(3, trackerHelp);
        // ---------------- RHYTHM LAB
        add(4, lab);
        // ---------------- large-layout extras on the SEQUENCER page: a velocity lane always on top + a 4-track tracker beside the lanes
        lanesVel.lane = 0; trackerMini.cols = 4; trackerMini.followTrack = true; add(0, lanesVel); largeOnly = { &lanesVel }; focusHidden.push_back(&lanesVel);
        // (Brandon 2026-11-13: no tracker on the SEQUENCER page when it's expanded; the lanes take the whole width. The tracker lives on its own TRACKER page.)
        // ---------------- MELODY: the melodic tracker (channels play the tracks' sounds as instruments)
        add(9, melView); melView.song = &proc.melody; melView.edit = [this](const std::function<void()>& f) { ed.edit(f); };
        melView.playRow = [this] { return proc.engine.uiMelRow.load(); }; melView.playPat = [this] { return proc.engine.uiMelRow.load() >= 0 ? proc.engine.uiMelPat.load() : -1; };
        melView.trackName = [this](int t) { return proc.model.tracks[(size_t) t].sound; }; melView.say = [this](const juce::String& s) { ed.say(s); };
        melView.preview = [this](int inst, int note) { proc.previewMelodyNote(inst, note); };   // at its pitch, through its route track; never recorded
        melView.librarySounds = [this](juce::PopupMenu& m) {
            std::map<juce::String, juce::PopupMenu> byCat; const auto& all = proc.lib->sounds();
            for (int i = 0; i < (int) all.size(); ++i) byCat[gb::folderOf(all[(size_t) i]->category)].addItem(1000 + i, all[(size_t) i]->name);
            for (auto& kv : byCat) m.addSubMenu(kv.first, kv.second); };
        melView.soundForId = [this](int id) { const auto& all = proc.lib->sounds(); const int i = id - 1000; return juce::isPositiveAndBelow(i, (int) all.size()) ? all[(size_t) i]->name : juce::String(); };
        melView.takeTrack = [this](int ii, int t) { ed.edit([&] { gb::mel::takeTrackSound(proc.melody.inst[(size_t) ii], proc.model.tracks[(size_t) t], t); });
                                                     ed.say("Instrument " + juce::String(ii + 1) + " now owns track " + juce::String(t + 1) + "'s sound (" + proc.model.tracks[(size_t) t].sound + ") and plays through that track: a kit change won't touch it."); };
        melView.loadSample = [this](int ii) {
            chooser = std::make_unique<juce::FileChooser>("A sample for instrument " + juce::String(ii + 1), juce::File(), "*.wav;*.aif;*.aiff;*.mp3;*.flac;*.ogg;*.m4a");
            juce::Component::SafePointer<Content> safe(this);
            chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe, ii](const juce::FileChooser& fc) {
                if (!safe || !fc.getResult().existsAsFile()) return; juce::String err; auto snd = gb::DrumLibrary::fromFile(fc.getResult(), 48000.0, err, safe->proc.normaliseSamples);
                if (!snd) { safe->ed.say(err.isNotEmpty() ? err : "No sound in that file."); return; }
                safe->ed.edit([&] { auto& in = safe->proc.melody.inst[(size_t) ii]; in.sample = snd; in.sound = {}; in.slices.clear(); in.slice = 0; if (in.route < 0) in.route = safe->ed.track; });
                safe->ed.say("Instrument " + juce::String(ii + 1) + " plays " + snd->name + " (kept inside the project)."); safe->melView.repaint(); }); };
        melView.midiRec = [this](bool on) { proc.engine.melodyInst.store(melView.inst); proc.engine.melodyMidi.store(on); };
        melView.orderChanged = [this] { if (!proc.melody.ownOrder) melView.pat = ed.patIdx(); melView.repaint(); };
        melOut.make = [this] { if (!proc.exportGate(&melOut, "beats.drag-melody-midi")) return juce::StringArray(); auto f = proc.exportMelodyMidi(songBtn.getToggleState()); return f.existsAsFile() ? juce::StringArray { f.getFullPathName() } : juce::StringArray(); }; add(9, melOut);
        melBoth.make = [this] { if (!proc.exportGate(&melBoth, "beats.drag-beat-melody-midi")) return juce::StringArray(); auto f = proc.exportBeatAndMelodyMidi(songBtn.getToggleState()); return f.existsAsFile() ? juce::StringArray { f.getFullPathName() } : juce::StringArray(); }; add(9, melBoth);
        melHelp.setText("MELODY ON plays the tracker with the drums (pattern A with drum pattern A, or its OWN ORDER). Instruments own their sound and play through a ROUTE track. Notes: Z S X D C V..., 1 = off, ? = effect help, Enter = hear the row.", juce::dontSendNotification);
        melHelp.setFont(font(10.5f, false)); melHelp.setColour(juce::Label::textColourId, GREY); add(9, melHelp);
        melLook();
        // ---------------- MIX: 16 strips with TRACK LEVELS, faders, pan, mute / solo + the master
        add(8, mix); mix.getMaster = [this] { return proc.apvts.getRawParameterValue("volume")->load(); };
        mix.setMaster = [this](float v) { if (auto* q = proc.apvts.getParameter("volume")) q->setValueNotifyingHost(q->convertTo0to1(v)); };
        // ---------------- SCRIPTS
        add(7, scriptList); scriptList.onSelect = [this](int) { loadScriptEditor(); };
        scriptName.setFont(font(13.0f)); scriptName.setTextToShowWhenEmpty("module name", GREY); add(7, scriptName);
        scriptCode.setMultiLine(true, false); scriptCode.setReturnKeyStartsNewLine(true); scriptCode.setTabKeyUsedAsCharacter(true); scriptCode.setFont(mono(13.5f));
        scriptCode.setScrollbarsShown(true); add(7, scriptCode);
        scriptErr.setFont(mono(12.0f)); scriptErr.setJustificationType(juce::Justification::topLeft); add(7, scriptErr);
        scriptHelp.setFont(mono(11.0f)); scriptHelp.setColour(juce::Label::textColourId, GREY); scriptHelp.setJustificationType(juce::Justification::topLeft);
        scriptHelp.setText("RULES, one per line:\n  if <condition> then <actions>\n  [else <actions>]\n\nVALUES\n  bar step pos beat track role\n  hit vel pitch prev_hit next_hit\n  density len seed\n  role: 1 kick 2 snare 3 hat\n        4 perc 5 808 6 open 7 break\n  + - * / %  == != < <= > >=\n  and or not  ( )\n  chance(p) rand() min max abs\n  floor every(n)\n\nACTIONS (comma between)\n  hit(vel) mute() vel(v)\n  accent(x) pitch(semis)\n  retrig(n, rate=16, accel=0,\n         ramp=0, vramp=0)\n  reverse() offset(%) cut(%)\n  nudge(%) prob(%) flam(ms)\n  decay(%) slice(n) destroy(%)\n  freeze(ms) frev(mask)\n\nSafe: no loops, no files,\nseeded (same beat = same\nresult), runs every step.", juce::dontSendNotification);
        add(7, scriptHelp);
        scrNew.onClick = [this] { if ((int) proc.model.scripts.size() >= gb::kScripts) { ed.say("8 modules at most."); return; }
            ed.edit([&] { proc.model.scripts.push_back({ "Module " + juce::String((int) proc.model.scripts.size() + 1), "# if bar % 4 == 3 and step > 12 then retrig(4, accel=0.3)\n", true }); });
            scriptList.sel = (int) proc.model.scripts.size() - 1; loadScriptEditor(); };
        scrApply.getProperties().set("accent", true); scrApply.onClick = [this] { applyScript(); };
        scrDel.onClick = [this] { auto& sc = proc.model.scripts; if (!juce::isPositiveAndBelow(scriptList.sel, (int) sc.size())) return; ed.edit([&] { sc.erase(sc.begin() + scriptList.sel); }); scriptList.sel = juce::jmax(0, scriptList.sel - 1); loadScriptEditor(); };
        scrSave.onClick = [this] { auto n = juce::File::createLegalFileName(scriptName.getText().trim()); if (n.isEmpty()) n = "Module"; auto d = scriptsDir();
            ed.say(d.getChildFile(n + ".ghostscript").replaceWithText(scriptCode.getText()) ? "Saved \"" + n + "\" in Documents/Ghosty/Ghosty Beats/Scripts (use it in any beat with LOAD MODULE)." : "Couldn't save."); };
        scrLoad.onClick = [this] {
            juce::PopupMenu m, ex; int id = 1; for (auto& e : gb::script::examples()) ex.addItem(id++, e.name); m.addSubMenu("Examples", ex);
            auto files = scriptsDir().findChildFiles(juce::File::findFiles, false, "*.ghostscript"); files.sort(); for (int i = 0; i < files.size(); ++i) m.addItem(100 + i, files[i].getFileNameWithoutExtension());
            if (files.isEmpty()) m.addItem(-1, "(your saved modules appear here)", false);
            juce::Component::SafePointer<Content> safe(this);
            m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&scrLoad), [safe, files](int r) {
                if (!safe) return; auto* self = safe.getComponent(); auto& proc = self->proc; auto& ed = self->ed; auto& scriptList = self->scriptList;
                juce::String name, code;
                if (r >= 1 && r <= (int) gb::script::examples().size()) { name = gb::script::examples()[(size_t) r - 1].name; code = gb::script::examples()[(size_t) r - 1].code; }
                else if (r >= 100) { name = files[r - 100].getFileNameWithoutExtension(); code = files[r - 100].loadFileAsString(); } else return;
                if ((int) proc.model.scripts.size() >= gb::kScripts) { ed.say("8 modules at most."); return; }
                ed.edit([&] { proc.model.scripts.push_back({ name, code, true }); }); scriptList.sel = (int) proc.model.scripts.size() - 1; self->loadScriptEditor(); self->applyScript(); });
        };
        for (auto* b : { &scrNew, &scrApply, &scrDel, &scrSave, &scrLoad }) add(7, *b);
        // ---------------- PADS
        add(5, pads);
        recBtn.setClickingTogglesState(true); recBtn.getProperties().set("tint", (juce::int64) RED.getARGB());
        recBtn.onClick = [this] { proc.recording.store(recBtn.getToggleState()); if (recBtn.getToggleState() && playBtn.isEnabled() && !playBtn.getToggleState()) playBtn.setToggleState(true, juce::sendNotification); };
        btnAtts.push_back(std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "repeat", repeatBtn)); repeatBtn.setClickingTogglesState(true);
        repeatDiv.addItemList({ "1/8", "1/16", "1/32", "1/16T" }, 1); repeatAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(proc.apvts, "repeatdiv", repeatDiv);
        updatePadHelp();
        padHelp.setFont(font(12.5f, false)); padHelp.setColour(juce::Label::textColourId, GREY); padHelp.setJustificationType(juce::Justification::topLeft);
        for (juce::Component* c : std::initializer_list<juce::Component*> { &recBtn, &repeatBtn, &repeatDiv, &padHelp }) add(5, *c);
        // ---------------- SONG
        add(6, lane);
        for (int i = 0; i < gb::kPatterns; ++i) add(6, *chips.add(new PatternChip(i)));
        delBlock.onClick = [this] { if (juce::isPositiveAndBelow(lane.selected, (int) proc.model.song.size())) { ed.edit([&] { proc.model.song.erase(proc.model.song.begin() + lane.selected); }); lane.selected = -1; } };
        dupBlock.onClick = [this] { if (juce::isPositiveAndBelow(lane.selected, (int) proc.model.song.size())) { ed.edit([&] { auto b = proc.model.song[(size_t) lane.selected]; b.startBar += b.bars; proc.model.song.push_back(b); }); lane.selected = (int) proc.model.song.size() - 1; } };
        clrSong.onClick = [this] { ed.edit([&] { proc.model.song.clear(); }); lane.selected = -1; };
        songHelp.setText("SONG MODE on: when the DAW (or PLAY) runs, the beat follows this arrangement bar by bar. Off: the selected pattern loops (EVOLVE keeps varying it). Switch patterns live with the letters (the DAW can automate \"Pattern\").", juce::dontSendNotification);
        songHelp.setFont(font(12.5f, false)); songHelp.setColour(juce::Label::textColourId, GREY); songHelp.setJustificationType(juce::Justification::topLeft);
        for (juce::Component* c : std::initializer_list<juce::Component*> { &delBlock, &dupBlock, &clrSong, &songHelp }) add(6, *c);

        // ---------------- bottom: master, performance, macros
        const char* mk[] = { "SWING", "HUMAN", "GLITCH", "DJ FILTER", "ECHO", "REVERB", "DRIVE", "VOLUME" }; const char* mid[] = { "swing", "human", "glitch", "filter", "echo", "verb", "drive", "volume" };
        for (int i = 0; i < 8; ++i) { auto* k = masterKnobs.add(new Knob(mk[i])); k->bind(proc.apvts, mid[i]); add(-1, *k); }
        const char* mc[] = { "CHAOS", "DENSITY", "MUTATION", "FRACTURE", "GRAVITY", "MEMORY", "TENSION", "RESOLVE" }; const char* mcid[] = { "chaos", "density", "mutation", "fracture", "gravity", "memory", "tension", "resolve" };
        for (int i = 0; i < 8; ++i) { auto* k = macroKnobs.add(new Knob(mc[i])); k->bind(proc.apvts, mcid[i]); k->l.setColour(juce::Label::textColourId, PINK); add(-1, *k); }
        stutter.onRate = [this](int r) { proc.stutter.store(r); };   // PERFORM > STUTTER: press / drag across / release
        stutterLbl.setText("STUTTER", juce::dontSendNotification); stutterLbl.setFont(font(10.5f)); stutterLbl.setColour(juce::Label::textColourId, GREY); stutterLbl.setJustificationType(juce::Justification::centredLeft);
        add(-1, stutterLbl); add(-1, stutter);
        { auto* b = perf.add(new HoldButton("TAPE STOP")); b->getProperties().set("tint", (juce::int64) PINK.getARGB()); b->onHold = [this](bool on) { proc.tape.store(on); }; add(-1, *b); }
        saveBeatBtn.getProperties().set("accent", true); add(-1, saveBeatBtn); add(-1, loadBeatBtn);
        for (auto* k : masterKnobs) bottomComps.push_back(k); for (auto* k : macroKnobs) bottomComps.push_back(k); for (auto* b : perf) bottomComps.push_back(b); bottomComps.push_back(&stutter); bottomComps.push_back(&stutterLbl); bottomComps.push_back(&saveBeatBtn); bottomComps.push_back(&loadBeatBtn);
        saveBeatBtn.onClick = [this] {
            if (saveDlg) return;
            auto* w = new juce::AlertWindow("Save this beat", "Name it (kit, samples, all patterns and the song are saved):", juce::MessageBoxIconType::NoIcon); saveDlg = w; w->setLookAndFeel(&getLookAndFeel());   // themed (released in ~Content)
            w->addTextEditor("name", kitBox.getText().upToFirstOccurrenceOf(" kit", false, false) + " beat"); w->addButton("SAVE", 1, juce::KeyPress(juce::KeyPress::returnKey)); w->addButton("Cancel", 0);
            juce::Component::SafePointer<Content> safe(this);
            w->enterModalState(true, juce::ModalCallbackFunction::create([safe, w](int r) { if (!safe) return; if (r == 1) { const auto n = w->getTextEditorContents("name").trim(); if (n.isNotEmpty()) safe->ed.say(safe->proc.saveBeat(n) ? "Saved \"" + n + "\" in Documents/Ghosty/Beats." : "Couldn't save."); } }), true);
        };
        loadBeatBtn.onClick = [this] {
            juce::PopupMenu m; auto files = proc.beatsDir().findChildFiles(juce::File::findFiles, false, "*.ghostbeat"); files.sort();
            for (int i = 0; i < files.size(); ++i) m.addItem(i + 1, files[i].getFileNameWithoutExtension());
            if (files.isEmpty()) m.addItem(-1, "No saved beats yet (use SAVE BEAT)", false);
            juce::Component::SafePointer<Content> safe(this);
            m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&loadBeatBtn), [safe, files](int r) { if (!safe) return; if (r > 0 && safe->proc.loadBeat(files[r - 1])) { safe->refreshAll(); safe->ed.say("Loaded \"" + files[r - 1].getFileNameWithoutExtension() + "\"."); } });
        };
        // ghosts drifting through the header, like the Ghosty synth
        juce::Random r(7);
        for (int i = 0; i < 10; ++i) bgGhosts.push_back({ ghosty::lookFor((i * 4 + 1) % 35), ghosty::lookFor((i * 4 + 1) % 35), r.nextFloat() * 6.28f, 0.3f + r.nextFloat() * 0.55f,
                                                        (0.004f + r.nextFloat() * 0.008f) * (r.nextBool() ? 1.0f : -1.0f), 24 + r.nextFloat() * 28, 0.12f + r.nextFloat() * 0.1f, r.nextFloat() * 6.28f });
        mascot = mascotBase = ghosty::lookFor(0); mascot.acc = 3; mascot.eyes = 3; mascot.mouth = 1;   // the beats ghost wears headphones
        ghostsForTheme();
        // SAMPLE REGION (START / END / LOOP of the track's sound) + SLICES, sharing the slot under CHOP
        add(0, region); add(0, regTab); add(0, sliceTab); focusHidden.push_back(&region); focusHidden.push_back(&regTab); focusHidden.push_back(&sliceTab);
        for (auto* b : { &regTab, &sliceTab }) { b->setClickingTogglesState(false); b->getProperties().set("tint", (juce::int64) CYAN.getARGB()); }
        regTab.onClick = [this] { showSlices = false; syncRegionTabs(); }; sliceTab.onClick = [this] { showSlices = true; syncRegionTabs(); };
        regTab.setTooltip("The track's sound: START / END and a LOOP (drag the flags; right-click = START, right-drag = LOOP; drag WAV out)");
        sliceTab.setTooltip("The track's slices (BREAK CHOP): click to add, drag to move, right-click to remove, drag a slice out as a WAV");
        region.hint = "right-click: START   |   right-drag: LOOP   |   drag flags   |   WAV grip: drag out";
        region.palette = [] { ghosty::WaveRegion::Palette c; c.bg = CARD2; c.line = DIM; c.wave = CYAN; c.waveOut = GREY; c.ink = INK; c.dim = GREY; c.start = GREEN; c.end = PINK; c.loop = AMBER; c.label = ON; c.card = POP; return c; };
        region.onChange = [this](const ghosty::WaveRegion::Region& r) { ed.gestureUndo(&region); auto& tr = ed.tr(); tr.regStart = r.start; tr.regEnd = r.end; tr.loopStart = r.loopStart; tr.loopEnd = r.loopEnd; tr.loopOn = r.loopOn; ed.live(); };
        region.onAudition = [this](bool down, float) { if (down) proc.auditionTrack(ed.track); }; region.clickAnywhereAuditions = true;   // (Brandon 2026-11-07: a click anywhere on the sound plays it)
        region.canExport = [this] { return proc.exportGate(&region, "beats.region-wav-drag"); };
        region.exportRegion = [this](float a, float b) { return proc.exportSoundPart(ed.track, a, b, "region"); };
        region.onMessage = [this](const juce::String& m) { ed.say(m); };
        // MELODY: XM / IT import + export
        add(9, trkBtn); trkBtn.setTooltip("Tracker modules: IMPORT an XM (FastTracker 2) or IT (Impulse Tracker) file into the melody tracker, or EXPORT the melody as XM / IT (patterns, instruments, samples)");
        trkBtn.onClick = [this] {
            juce::PopupMenu m; m.addItem(1, "Import an XM / IT file..."); m.addSeparator(); m.addItem(2, "Export as XM..."); m.addItem(3, "Export as IT...");
            juce::Component::SafePointer<Content> safe(this);
            m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&trkBtn), [safe](int r) {
                if (!safe || r <= 0) return; auto& c = *safe;
                if (r == 1) { c.chooser = std::make_unique<juce::FileChooser>("Import a tracker module", juce::File(), "*.xm;*.it");
                    c.chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe](const juce::FileChooser& fc) { if (safe && fc.getResult().existsAsFile()) safe->importModule(fc.getResult()); }); }
                else c.exportModule(r == 3);
            });
        };
        // RE-CREATE AS MIDI
        add(0, loopBtn); focusHidden.push_back(&loopBtn);
        loopBtn.setTooltip("Turn an audio loop (drums, a break, a bassline) into an editable pattern / melody. Or just drag the file onto the grid.");
        loopBtn.onClick = [this] {
            chooser = std::make_unique<juce::FileChooser>("Pick an audio loop to turn into MIDI", juce::File(), "*.wav;*.aif;*.aiff;*.mp3;*.flac;*.ogg;*.m4a");
            juce::Component::SafePointer<Content> safe(this);
            chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe](const juce::FileChooser& fc) { if (safe && fc.getResult().existsAsFile()) safe->startRecreate(fc.getResult(), safe->ed.track); });
        };
        addChildComponent(recreate); addChildComponent(dropOverlay);
        recreate.refresh = [this] { refreshAll(); };
        recreate.useAsSound = [this](const juce::File& f, int t) { dropFiles({ f.getFullPathName() }, t); };
        recreate.chopToSlices = [this](const juce::File& f, int t) { dropFiles({ f.getFullPathName() }, t); if (proc.model.tracks[(size_t) ed.track].slices.empty()) ed.edit([&] { proc.chopTrack(ed.track, 0.5f, true); }); refreshAll(); };
        fillSounds(); subs(); setPage(0); refreshAll();
    }
    // MELODY > Export as XM / IT: a tracker module (notes + the instruments' samples) = an export (the demo gate first)
    void exportModule(bool it) {
        if (!proc.exportGate(&trkBtn, it ? "beats.export-it" : "beats.export-xm")) { ed.say("Exporting is part of the full version (UNLOCK)."); return; }
        juce::Component::SafePointer<Content> safe(this);
        ghosty::exportgate::saveDialog(chooser, it ? "Export the melody as IT" : "Export the melody as XM", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(it ? "Ghosty melody.it" : "Ghosty melody.xm"), it ? "*.it" : "*.xm",
            [safe, it](const juce::File& f) { if (!safe) return; juce::String msg; safe->proc.exportTracker(f.withFileExtension(it ? "it" : "xm"), it, msg); safe->ed.say(msg); }, true);
    }
    void importModule(const juce::File& f) { bool ok = false; juce::String msg; ed.edit([&] { msg = proc.importTracker(f, ok); }); if (!ok) proc.undo(); melView.pat = proc.melody.ownOrder && !proc.melody.order.empty() ? proc.melody.order[0] : melView.pat; refreshAll(); ed.say(msg); }
    void syncRegionTabs() {
        const bool onGrid = page == 0 && !focusOn();
        regTab.setToggleState(!showSlices, juce::dontSendNotification); sliceTab.setToggleState(showSlices, juce::dontSendNotification);
        region.setVisible(onGrid && !showSlices); slices.setVisible(onGrid && showSlices); regTab.setVisible(onGrid); sliceTab.setVisible(onGrid);
    }
    // the region editor shows the selected track's sound (peaks rebuilt only when the sound / track changes)
    void syncRegion() {
        const auto& tr = ed.tr(); const auto snd = tr.sample ? tr.sample : proc.lib->loaded(tr.sound);
        if (snd.get() != regionFor || ed.track != regionTrack) {
            regionFor = snd.get(); regionTrack = ed.track; std::vector<float> pk;
            if (snd && !snd->data.empty()) { const int bins = 600, n = (int) snd->data.size(); pk.resize(bins);
                for (int b = 0; b < bins; ++b) { const int a = (int) ((juce::int64) b * n / bins), z = juce::jmax(a + 1, (int) ((juce::int64) (b + 1) * n / bins)); float m = 0; for (int k = a; k < z && k < n; ++k) m = juce::jmax(m, std::abs(snd->data[(size_t) k])); pk[(size_t) b] = juce::jmin(1.0f, m); } }
            region.setPeaks(pk); region.setLengthSeconds(snd ? snd->data.size() / snd->rate : 0.0);
        }
        region.setRegion({ tr.regStart, tr.regEnd, tr.loopStart, tr.loopEnd, tr.loopOn });
    }
    void startRecreate(const juce::File& f, int track) { if (!f.existsAsFile()) return; recreate.setBounds(lw - RecreatePanel::kW - 12, juce::jmax(130, lh - RecreatePanel::kH - 108), RecreatePanel::kW, RecreatePanel::kH); recreate.start(f, track); }   // (bottom-right: the new pattern stays in view)
    // where a dragged audio file goes: 1 = RE-CREATE AS MIDI (the pattern grid / lanes, the melody tracker), 0 = a track's sound, -1 = the old default
    int dropZoneAt(juce::Point<int> p) const {
        if (page == 0) { if (grid.getBounds().contains(p) || lanes.getBounds().contains(p) || (lanesVel.isVisible() && lanesVel.getBounds().contains(p)) || (trackerMini.isVisible() && trackerMini.getBounds().contains(p))) return 1;
                         if (tl0.getBounds().contains(p)) return 0; }
        if (page == 9 && melView.getBounds().contains(p)) return 1;
        return -1;
    }
    void showDropHint(juce::Point<int> p, bool on) {
        if (!on) { dropOverlay.setVisible(false); return; }
        dropOverlay.recreate = page == 0 ? grid.getBounds() : page == 9 ? melView.getBounds() : juce::Rectangle<int>();
        dropOverlay.sound = page == 0 ? tl0.getBounds() : juce::Rectangle<int>();
        dropOverlay.over = dropZoneAt(p); dropOverlay.setBounds(getLocalBounds()); dropOverlay.setVisible(!dropOverlay.recreate.isEmpty()); dropOverlay.toFront(false); dropOverlay.repaint();
    }

    // ---- ghost layer
    struct BgGhost { ghosty::GhostLook look, base; float a, r, sp, sz, al, ph; };
    std::vector<BgGhost> bgGhosts; ghosty::GhostLook mascot, mascotBase; float phase = 0, bounce = 0; uint32_t lastTrig = 0;
    void melLook() { melView.look = { CARD, CARD2, INK, GREY, DIM, PURPLE, GREEN, AMBER, PINK, CYAN, BLUE, RED, LIME, ON }; melView.repaint(); }
    void ghostsForTheme() {   // pale ghosts vanish on the light page: give them a soft lavender body there
        auto fix = [](ghosty::GhostLook& L, const ghosty::GhostLook& base) { L = base; if (!isDark && base.body.getBrightness() > 0.8f) L.body = juce::Colour(0xffCFC9EE);
            if (NEON) { L.body = L.body.interpolatedWith(juce::Colour(0xffb9d2c8), 0.75f); L.slime = neon::teal(); L.pupil = juce::Colour(0xff0c1415); } };   // NEON: pale teal holograms
        for (auto& q : bgGhosts) fix(q.look, q.base); mascotBase.acc = 3; mascotBase.eyes = 3; mascotBase.mouth = 1; fix(mascot, mascotBase);
    }
    // LOOK: NEON <-> LIGHT (Brandon 2026-11-13; the older looks are gone). The palette globals change, every child's stored colour is remapped, the look-and-feel re-reads its colours.
    void setTheme(th::Theme t) { setLook(th::toInt(t)); }
    void setLook(int look) {
        look = sanitizeLook(look); const auto t = look == kLookGhost ? th::Theme::Light : look == kLookNeon ? th::Theme::Dark : th::fromInt(look);
        const bool dark = !th::get(t).isLight;
        const juce::Colour* P[] { &INK, &GREY, &PURPLE, &VIOLET, &GREEN, &PINK, &AMBER, &BLUE, &RED, &CYAN, &LIME, &BG, &CARD, &CARD2, &POP, &DIM, &ON };
        std::vector<juce::Colour> before; for (auto* c : P) before.push_back(*c);
        if (ed.stepBox) { ed.stepBox->setLookAndFeel(nullptr); ed.stepBox->dismiss(); }   // (its toggles keep the old ink otherwise: reopen it in the new theme)
        applyLook(look); myTheme = t; myLook = LOOK;
        std::vector<std::pair<juce::Colour, juce::Colour>> m; for (size_t i = 0; i < before.size(); ++i) m.push_back({ before[i], *P[i] });
        LnF::remap(*this, m); ed_.refreshLook(); ghostsForTheme(); melLook(); bypassBtn.setTheme(t); syncBtn.setTheme(t); tempoSw.setTheme(t); (void) dark;
        if (NEON) { bypassBtn.setColours(CARD2, INK, AMBER, ON); syncBtn.setColours(CARD2, INK, CYAN, ON); tempoSw.setColours(CARD2, INK, CYAN, ON); }
        if (SUMI) { bypassBtn.setColours(CARD2, INK, RED, ON); syncBtn.setColours(CARD2, INK, PURPLE, ON); tempoSw.setColours(CARD2, INK, PURPLE, ON); }
        searchHint(); scriptName.setTextToShowWhenEmpty("module name", GREY);
        themeBtn.setButtonText(lookName(nextLook(look)));
        focusBtn.getProperties().set("tint", (juce::int64) AMBER.getARGB()); lockBtn.getProperties().set("tint", (juce::int64) AMBER.getARGB()); recBtn.getProperties().set("tint", (juce::int64) RED.getARGB());
        for (auto* b : { &patBtn, &songBtn, &mixBtn, &stemsBtn }) b->getProperties().set("tint", (juce::int64) GREEN.getARGB());
        for (int i = 0; i < gb::kPatterns; ++i) patBtns[i]->getProperties().set("tint", (juce::int64) patColour(i).getARGB());
        for (auto* b : perf) b->getProperties().set("tint", (juce::int64) PINK.getARGB());
        if (proc.look() != look) { proc.setLook(look); proc.saveSettings(); }
        repaint(); for (auto* c : getChildren()) c->repaint();
    }
    void tickGhosts() {
        phase += 1.0f / 30.0f; uint32_t sum = 0; for (int t = 0; t < gb::kTracks; ++t) sum += proc.engine.trig[t].load();
        if (sum != lastTrig) { lastTrig = sum; bounce = 1.0f; } bounce *= 0.82f;
        for (auto& q : bgGhosts) q.a += q.sp * (1.0f + bounce * 3.0f);
        repaint(0, 0, lw, 130);
        if (NEON && (++neonTick % 6) == 0 && !focusOn()) repaint(8, lh - 96, lw - 16, 90);   // NEON: the street's ripples under the bottom strip (5 a second)
    }

    // ---- helpers
    static void fillRuleSrc(juce::ComboBox& b, int type) {   // the source field means "which track" for most rules and "which bar" for "play only on bar k of N"
        const bool bar = type == gb::RuleBarOfN; const juce::String want = bar ? "bar 1" : "track 1"; if (b.getNumItems() == gb::kTracks && b.getItemText(0) == want) return;
        const int sel = b.getSelectedId(); b.clear(juce::dontSendNotification); for (int t = 0; t < gb::kTracks; ++t) b.addItem((bar ? "bar " : "track ") + juce::String(t + 1), t + 1); b.setSelectedId(sel, juce::dontSendNotification);
    }
    float& trackParam(int i) { auto& t = ed.tr(); switch (i) { case 0: return t.pitch; case 1: return t.decay; case 2: return t.vol; case 3: return t.pan; case 4: return t.tone; case 5: return t.drive; case 6: return t.swing; case 7: return t.accent; default: return t.human; } }
    float& fxParam(int i) { auto& t = ed.tr(); switch (i) { case 0: return t.tAttack; case 1: return t.tSustain; case 2: return t.sat; case 3: return t.comp; case 4: return t.lowCut; case 5: return t.tilt; case 6: return t.highCut; case 7: return t.duck;
                                                              case 8: return t.attack; case 9: return t.glide; case 10: return t.pitchEnv; case 11: return t.pitchEnvTime; default: return t.velAmt; } }
    // the header's tempo / PLAY follow the SYNC mode: DAW = the DAW's tempo, read-only, no own PLAY; OWN = own tempo (20..999, TAP) + own PLAY;
    // no DAW at all (standalone) = the BPM parameter + PLAY, exactly as before
    void updateSyncUi(bool force = false) {
        const bool own = proc.ownClock(), standalone = proc.wrapperType == juce::AudioProcessor::wrapperType_Standalone && !proc.hostPresent.load();
        const int st = own ? 1 : standalone ? 2 : 0;
        if (st != syncUiState || force) {
            syncUiState = st; bpmAtt.reset(); bpm.onValueChange = nullptr;
            if (st == 2) { bpm.setRange(60, 200, 1); bpmAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, "bpm", bpm);
                           if (auto* q = proc.apvts.getParameter("bpm")) bpm.setDoubleClickReturnValue(true, q->convertFrom0to1(q->getDefaultValue())); }
            else { bpm.setDoubleClickReturnValue(st == 1, 120.0); bpm.setRange(20, 999, 1); bpm.setValue(st == 1 ? proc.sync.getOwnBpm() : dawTempo(), juce::dontSendNotification);
                   if (st == 1) bpm.onValueChange = [this] { proc.sync.setOwnBpm(bpm.getValue()); }; }
            bpm.setColour(juce::Slider::trackColourId, st == 2 ? PURPLE.withAlpha(0.5f) : juce::Colours::transparentBlack);   // (a 20-999 bar would be a sliver: just the number)
            bpm.setEnabled(st != 0); tapBtn.setEnabled(st != 0); playBtn.setEnabled(st != 0); if (st == 0) playBtn.setToggleState(false, juce::dontSendNotification);
            bpm.setTooltip(st == 0 ? "The DAW's tempo (SYNC: DAW). Switch SYNC to OWN to give Beats its own tempo." : st == 1 ? "Beats' own tempo (SYNC: OWN): drag, double-click to type (20-999), or TAP." : "Tempo");
            playBtn.setTooltip(st == 0 ? "SYNC: DAW: the DAW's play button runs Beats. Switch SYNC to OWN for Beats' own PLAY / SPACE." : st == 1 ? "Play / stop Beats only (SPACE), from the start of the pattern / song." : "Play / stop");
            bpm.updateText(); repaint();
        }
        if (st == 0 && std::abs(bpm.getValue() - dawTempo()) > 0.01) { bpm.setValue(dawTempo(), juce::dontSendNotification); bpm.updateText(); }
        if (st == 1 && !bpm.isMouseButtonDown() && std::abs(bpm.getValue() - proc.sync.getOwnBpm()) > 0.01) bpm.setValue(proc.sync.getOwnBpm(), juce::dontSendNotification);
        tempoSw.set(proc.tempoMul.load(), st == 1 ? proc.sync.getOwnBpm() : dawTempo());   // TEMPO: each choice shows the BPM it plays at
    }
    double dawTempo() const { const double h = proc.hostBpm.load(); return h > 0 ? h : (double) proc.apvts.getRawParameterValue("bpm")->load(); }
    void tap() {
        const double now = juce::Time::getMillisecondCounterHiRes();
        if (nTaps > 0 && now - tapTimes[(nTaps - 1) % 5] > 2000.0) nTaps = 0;   // a pause starts a new count
        tapTimes[nTaps % 5] = now; ++nTaps;
        if (nTaps < 2) { ed.say("TAP: keep tapping on the beat..."); return; }
        const int k = juce::jmin(nTaps, 5); const double span = tapTimes[(nTaps - 1) % 5] - tapTimes[(nTaps - k) % 5];
        const double b = juce::jlimit(20.0, 999.0, std::round(60000.0 * (k - 1) / juce::jmax(1.0, span)));
        if (syncUiState == 1) proc.sync.setOwnBpm(b); else if (auto* q = proc.apvts.getParameter("bpm")) q->setValueNotifyingHost(q->convertTo0to1((float) b));
        bpm.setValue(b, juce::dontSendNotification); bpm.updateText(); ed.say("TAP: " + juce::String((int) b) + " bpm.");
    }
    // the PADS help follows PLAY: ALWAYS | ON NOTES (in ON NOTES the MIDI notes only start / stop the beat; the on-screen pads still play)
    int padHelpMode = -1;
    void updatePadHelp() {
        padHelpMode = proc.playMode.load();
        padHelp.setText(padHelpMode == 1
            ? "Tap pads to play (higher on a pad = harder). REC + PLAY writes what you tap into the current pattern. Hold a pad with NOTE REPEAT on for rolls. PLAY: ON NOTES is on: MIDI notes only start / stop the beat (they do not play or record pads); these on-screen pads still play."
            : "Tap pads to play (higher on a pad = harder). REC + PLAY writes what you tap into the current pattern. Hold a pad with NOTE REPEAT on for rolls. MIDI notes C1-D#2 play pads 1-16.", juce::dontSendNotification);
    }
    void subs() {
        const bool song = songBtn.getToggleState(), stems = stemsBtn.getToggleState();
        midiOut.sub = juce::String(song ? "song" : "pattern") + " | ch " + juce::String(proc.midiChannel) + " | GM"; audioOut.sub = juce::String(song ? "song" : "pattern") + " | " + (stems ? "stems" : "mix") + " | 24-bit";
        chBtn.setButtonText("CH " + juce::String(proc.midiChannel)); midiOut.repaint(); audioOut.repaint();
    }
    void setPage(int p) {
        const bool wasFocus = focusOn(); page = p;
        for (int i = 0; i < kPages; ++i) { pageBtns[i]->setToggleState(i == p, juce::dontSendNotification); for (auto* c : pageComps[i]) c->setVisible(false); }
        for (auto* c : pageComps[p]) { bool v = large || std::find(largeOnly.begin(), largeOnly.end(), c) == largeOnly.end(); if (focusOn() && std::find(focusHidden.begin(), focusHidden.end(), c) != focusHidden.end()) v = false; c->setVisible(v); }
        for (auto* c : bottomComps) c->setVisible(!focusOn());
        if (p == 0) { const bool multi = maxTrackLen() > 16; pagePrev.setVisible(multi); pageNext.setVisible(multi); pageLbl.setVisible(multi); }
        if (p == 7) loadScriptEditor();
        if (p == 1) rescanIfNeeded();   // MY BANKS changed (here or in Block Step / Ghost House)
        if (p == 8) ed.say("MIX: drag faders (shift = fine, double-click = reset), pan bars, M / S. Red = over -0.5 dBFS pre limiter.");
        if (p == 9 && !proc.melody.ownOrder) melView.pat = ed.patIdx();
        if (wasFocus != focusOn() || laidOutFocus != focusOn()) resized();
        syncRegionTabs(); refreshAll(); repaint();
    }
    int maxTrackLen() { int m = ed.pat().length; for (int t = 0; t < gb::kTracks; ++t) m = juce::jmax(m, ed.pat().lenOf(t)); return m; }
    void refreshAll() {
        keyBox.setSelectedId(proc.songKeyRoot < 0 ? 1 : 2 + (proc.songKeyMinor ? 0 : 12) + proc.songKeyRoot, juce::dontSendNotification);
        const int pi = ed.patIdx(); auto& P = ed.pat(); const auto& tr = ed.tr();
        for (int i = 0; i < gb::kPatterns; ++i) patBtns[i]->setToggleState(i == pi, juce::dontSendNotification);
        lenBox.setSelectedId(P.length, juce::dontSendNotification); resBox.setSelectedItemIndex(P.res, juce::dontSendNotification);
        ed.stepPage = juce::jlimit(0, juce::jmax(0, (maxTrackLen() - 1) / 16), ed.stepPage);
        { const bool multi = maxTrackLen() > 16; pageLbl.setText("steps " + juce::String(ed.stepPage * 16 + 1) + "-" + juce::String(ed.stepPage * 16 + 16), juce::dontSendNotification);
          if (page == 0) { pagePrev.setVisible(multi); pageNext.setVisible(multi); pageLbl.setVisible(multi); } }
        for (int i = 0; i < laneBtns.size(); ++i) laneBtns[i]->setToggleState(lanes.lane == i, juce::dontSendNotification);
        syncRegion();
        trackTitle.setText("TRACK " + juce::String(ed.track + 1), juce::dontSendNotification); trackTitle.setColour(juce::Label::textColourId, trackColour(ed.track));
        soundName.setText(tr.sound + (tr.sample ? "  (your sample)" : "") + (tr.slices.empty() ? juce::String() : "  " + juce::String((int) tr.slices.size()) + " slices"), juce::dontSendNotification);
        for (int i = 0; i < 9; ++i) trackKnobs[i]->s.setValue(trackParam(i), juce::dontSendNotification);
        for (int i = 0; i < 13; ++i) fxKnobs[i]->s.setValue(fxParam(i), juce::dontSendNotification);
        charBox.setSelectedItemIndex(tr.character, juce::dontSendNotification); charKnobs[0]->s.setValue(tr.charAmount, juce::dontSendNotification); charKnobs[1]->s.setValue(tr.charDrive, juce::dontSendNotification);
        for (int l = 0; l < gb::kLayers; ++l) { const auto& L = tr.layers[(size_t) l]; layerBtns[l]->setButtonText("LAYER " + juce::String(l + 1) + ": " + (L.sound.isEmpty() ? juce::String("none (click)") : L.sound));
            layerKnobs[l * 3]->s.setValue(L.level, juce::dontSendNotification); layerKnobs[l * 3 + 1]->s.setValue(L.pitch, juce::dontSendNotification); layerKnobs[l * 3 + 2]->s.setValue(L.offset, juce::dontSendNotification); }
        scBox.setSelectedId(proc.model.scTrack < 0 ? 100 : proc.model.scTrack + 1, juce::dontSendNotification);
        for (int r = 0; r < 4; ++r) { const auto& R = tr.rules[(size_t) r]; ruleType[r]->setSelectedItemIndex(R.type, juce::dontSendNotification); fillRuleSrc(*ruleSrc[r], R.type); ruleSrc[r]->setSelectedId(R.src + 1, juce::dontSendNotification); ruleN[r]->setSelectedId(juce::jmax(1, (int) R.n), juce::dontSendNotification); rulePct[r]->setValue(R.pct, juce::dontSendNotification); }
        revBtn.setToggleState(tr.reverse, juce::dontSendNotification); lockBtn.setToggleState(tr.lock, juce::dontSendNotification); chokeBox.setSelectedItemIndex(tr.choke, juce::dontSendNotification);
        tLenBox.setSelectedId(P.trackLen[(size_t) ed.track] == 0 ? 100 : P.trackLen[(size_t) ed.track], juce::dontSendNotification);
        bank1.setToggleState(tracker.bank == 0, juce::dontSendNotification); bank2.setToggleState(tracker.bank == 1, juce::dontSendNotification);
        kitBox.setSelectedItemIndex(proc.currentKit, juce::dontSendNotification);
        undoBtn.setEnabled(proc.canUndo()); redoBtn.setEnabled(proc.canRedo());
        lab.sync(); if (!proc.melody.ownOrder) melView.pat = ed.patIdx();
        for (auto* c : std::initializer_list<juce::Component*> { &grid, &lanes, &tl0, &tl1, &tl2, &tracker, &pads, &lane, &slices, &lanesVel, &trackerMini, &scriptList, &mix, &melView }) c->repaint();
    }
    static juce::File scriptsDir() { auto d = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Ghosty").getChildFile("Ghosty Beats").getChildFile("Scripts"); d.createDirectory(); return d; }
    void loadScriptEditor() {
        auto& sc = proc.model.scripts; scriptList.repaint();
        if (juce::isPositiveAndBelow(scriptList.sel, (int) sc.size())) { scriptName.setText(sc[(size_t) scriptList.sel].name, false); scriptCode.setText(sc[(size_t) scriptList.sel].code, false); checkScript(false); }
        else { scriptName.clear(); scriptCode.clear(); scriptErr.setText("", juce::dontSendNotification); }
    }
    bool checkScript(bool moveCaret) {
        auto prog = gb::script::Compiler::compile(scriptCode.getText());
        if (!prog->ok()) {
            scriptErr.setColour(juce::Label::textColourId, RED); scriptErr.setText("line " + juce::String(prog->errLine) + ", column " + juce::String(prog->errCol) + ": " + prog->error, juce::dontSendNotification);
            if (moveCaret) { auto lines = juce::StringArray::fromLines(scriptCode.getText()); int pos = 0; for (int i = 0; i < prog->errLine - 1 && i < lines.size(); ++i) pos += lines[i].length() + 1;
                scriptCode.setHighlightedRegion({ pos + prog->errCol - 1, pos + prog->errCol }); scriptCode.grabKeyboardFocus(); }
            return false;
        }
        scriptErr.setColour(juce::Label::textColourId, GREEN); scriptErr.setText("OK: " + juce::String((int) prog->code.size()) + " operations, runs on every step of every track.", juce::dontSendNotification);
        return true;
    }
    void applyScript() {
        if (!checkScript(true)) return;
        auto& sc = proc.model.scripts; const auto nm = scriptName.getText().trim().isEmpty() ? juce::String("Module") : scriptName.getText().trim();
        ed.edit([&] { if (!juce::isPositiveAndBelow(scriptList.sel, (int) sc.size())) { if ((int) sc.size() >= gb::kScripts) return; sc.push_back({ nm, scriptCode.getText(), true }); scriptList.sel = (int) sc.size() - 1; }
                      else { sc[(size_t) scriptList.sel].name = nm; sc[(size_t) scriptList.sel].code = scriptCode.getText(); } });
        scriptList.repaint(); ed.say("Module \"" + nm + "\" is live.");
    }
    void setLayout(int w, int h, bool lg) {
        const bool changed = lg != large; lw = w; lh = h; large = lg;
        if (large && lanes.lane == 0) lanes.lane = 1;
        setBounds(0, 0, lw, lh); if (changed) setPage(page);
    }
    void fillSounds() {   // Brandon 2026-11-13: the search looks only inside the folder you're on (KICKS -> kicks, a pack -> that pack); ALL searches every sound
        auto& items = sounds.items; items.clear(); const auto q = search.getText().trim().toLowerCase(); const auto& all = proc.lib->sounds();
        const juce::String folder = folders.items[folders.selected];
        const auto words = juce::StringArray::fromTokens(q, " ", "");
        for (int i = 0; i < (int) all.size(); ++i) {
            const auto& s = all[(size_t) i];
            if (proc.isMine(s->name) && proc.myBankOf[s->name].isEmpty()) continue;   // removed from its bank
            bool in;
            if (folder == "ALL") in = true;
            else if (folder.startsWith(myPrefix())) in = proc.isMine(s->name) && proc.myBankOf[s->name] == myFolderId(folder);
            else if (kitFolderId(folder).isNotEmpty()) { const auto kb = proc.kitBankOf.find(s->name); in = kb != proc.kitBankOf.end() && kb->second == kitFolderId(folder); }
            else in = folder == "FAVOURITES" ? proc.favourites.contains(s->name) : (gb::folderOf(s->category) == folder && ! proc.isMine(s->name) && ! proc.isBankSound(s->name));
            if (! in) continue;
            if (q.isNotEmpty()) { bool ok = true; const auto hay = (s->name + " " + s->category + " " + gb::folderOf(s->category)).toLowerCase(); for (auto& w : words) ok = ok && hay.contains(w); if (! ok) continue; }
            items.push_back(i);
        }
        searchHint(); sounds.selected = -1; sounds.layout();
    }
    void searchHint() {   // the empty search box says what it will search
        const auto folder = folders.selected >= 0 && folders.selected < folders.items.size() ? folders.items[folders.selected] : juce::String("ALL");
        search.setTextToShowWhenEmpty(folder == "ALL" ? juce::String("search all sounds (kick, crack, 808, riser...)") : "search " + (folder.startsWith(myPrefix()) ? folder.fromFirstOccurrenceOf(myPrefix(), false, false) : folder) + " only", GREY);
        search.repaint();
    }
    void stepSound(int d) {
        auto& all = proc.lib->sounds(); const auto& tr = ed.tr(); int i = proc.lib->indexOf(tr.sound); if (i < 0) i = 0;
        const auto folder = gb::folderOf(all[(size_t) i]->category);
        for (int k = 0; k < (int) all.size(); ++k) { i = (i + d + (int) all.size()) % (int) all.size(); if (gb::folderOf(all[(size_t) i]->category) == folder) break; }
        const auto n = all[(size_t) i]->name; const int t = ed.track; ed.edit([&] { proc.setTrackSound(t, n); }); proc.auditionTrack(t);
    }
    void trackTool(int i) {
        auto& P = ed.pat(); auto& row = P.steps[(size_t) ed.track]; const int L = P.lenOf(ed.track);
        if (ed.tr().lock) { ed.say("This track is locked (LOCK)."); return; }
        if (i == 9) { trackClip.assign(row.begin(), row.end()); trackClipLen = P.trackLen[(size_t) ed.track]; ed.say("Copied track " + juce::String(ed.track + 1) + "."); return; }
        ed.edit([&] {
            juce::Random R((juce::int64) (ed.track * 7919 + proc.model.toolSeed++ * 31));
            auto freeStep = [&](int s) { return !row[(size_t) s].locked(); };
            switch (i) {
                case 0: case 1: case 2: { const int every = i == 0 ? 1 : (i == 1 ? 2 : 4); for (int s = 0; s < L; ++s) if (freeStep(s)) { row[(size_t) s] = gb::Step(); if (s % every == 0) row[(size_t) s].vel = 100; } } break;
                case 3: { const float d = (float) density.getValue() / 100.0f; for (int s = 0; s < L; ++s) if (freeStep(s)) { row[(size_t) s] = gb::Step(); const float w = (s % 4 == 0 ? 1.4f : (s % 2 == 0 ? 1.0f : 0.6f)) * d; if (R.nextFloat() < w) { row[(size_t) s].vel = (uint8_t) (60 + R.nextInt(67)); if (R.nextFloat() < 0.12f) row[(size_t) s].roll = (uint8_t) (R.nextBool() ? 3 : 2); } } } break;
                case 4: std::rotate(row.begin(), row.begin() + 1, row.begin() + L); break;
                case 5: std::rotate(row.begin(), row.begin() + L - 1, row.begin() + L); break;
                case 6: std::reverse(row.begin(), row.begin() + L); break;
                case 7: if (L * 2 <= gb::kMaxSteps) { for (int s = 0; s < L; ++s) row[(size_t) (s + L)] = row[(size_t) s]; P.trackLen[(size_t) ed.track] = (uint8_t) (L * 2 == P.length ? 0 : L * 2); if (L * 2 > P.length) { P.length = L * 2; P.trackLen[(size_t) ed.track] = 0; } } break;
                case 8: if (L >= 2) { for (int s = L / 2; s < L; ++s) row[(size_t) s] = gb::Step(); P.trackLen[(size_t) ed.track] = (uint8_t) (L / 2); } break;
                case 10: if (!trackClip.empty()) { std::copy(trackClip.begin(), trackClip.end(), row.begin()); P.trackLen[(size_t) ed.track] = (uint8_t) trackClipLen; } break;
                case 11: for (int s = 0; s < gb::kMaxSteps; ++s) if (freeStep(s)) row[(size_t) s] = gb::Step(); break;
                default: break;
            }
        });
    }
    void layerMenu(int l) {
        juce::PopupMenu m; m.addItem(1, "none"); int id = 10; std::vector<juce::String> names;
        for (auto& f : gb::browserFolders()) { juce::PopupMenu sub; for (auto& s : proc.lib->sounds()) if (gb::folderOf(s->category) == f && s->category != "Break") { sub.addItem(id++, s->name); names.push_back(s->name); } m.addSubMenu(f, sub); }
        juce::Component::SafePointer<Content> safe(this);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(layerBtns[l]), [safe, l, names](int r) { if (!safe || r <= 0) return; auto& ed = safe->ed; ed.edit([&] { auto& L = ed.tr().layers[(size_t) l]; L.sound = r == 1 ? juce::String() : names[(size_t) (r - 10)]; }); safe->proc.auditionTrack(ed.track); });
    }
    void trackMenu(int t) {
        juce::PopupMenu m; m.addItem(1, "Load sample..."); m.addItem(2, "Chop as a break (slices + rhythm)"); m.addItem(3, "Clear the sound"); m.addSeparator();
        m.addItem(4, juce::String(proc.model.tracks[(size_t) t].lock ? "Unlock" : "Lock") + " this track (generators skip it)"); m.addItem(5, "MIDI note: " + juce::MidiMessage::getMidiNoteName(proc.midiNoteFor(t), true, true, 3) + " (" + juce::String(proc.midiNoteFor(t)) + ")", false);
        juce::Component::SafePointer<Content> safe(this);
        m.showMenuAsync(juce::PopupMenu::Options(), [safe, t](int r) {
            if (!safe) return; auto* self = safe.getComponent(); auto& ed = self->ed; auto& proc = self->proc;
            if (r == 1) self->pickFile(t);
            else if (r == 2) ed.edit([&] { proc.chopTrack(t, 0.5f, true); });
            else if (r == 3) ed.edit([&] { auto& tr = proc.model.tracks[(size_t) t]; tr.sound = {}; tr.sample = nullptr; tr.slices.clear(); });
            else if (r == 4) ed.edit([&] { proc.model.tracks[(size_t) t].lock = !proc.model.tracks[(size_t) t].lock; });
        });
    }
    void trackDrag(int t, bool midi) {
        juce::StringArray files;
        if (!proc.exportGate(&tl0, midi ? "beats.track-midi-drag" : "beats.track-wav-drag")) { ed.say("Drag-out is part of the full version (UNLOCK)."); return; }
        if (midi) { auto f = proc.exportMidi(songBtn.getToggleState(), t); if (f.existsAsFile()) files.add(f.getFullPathName()); }
        else { BeatsXProcessor::AudioJob j; j.song = songBtn.getToggleState(); j.track = t; files = proc.waitAudioRender(j); }
        if (!files.isEmpty()) ghosty::exportgate::dragFiles(proc.license, files, &tl0);
    }
    // ---------------- MY BANKS
    static juce::String myPrefix() { return juce::String::fromUTF8("MY \xc2\xb7 "); }   // "MY · 808 KIT"
    juce::String myFolderId(const juce::String& folder) const { for (auto& b : proc.myBanks) if (myPrefix() + b.name == folder) return b.id; return {}; }
    static constexpr const char* kNewBank = "+ NEW BANK";
    static const juce::StringArray& kitFolders() { static juce::StringArray f { "BLOCK STEP BANK 1", "BLOCK STEP BANK 2", "BLOCK STEP BANK 3", "GHOST HOUSE BANK 1", "GHOST HOUSE BANK 2", "GHOST HOUSE BANK 3" }; return f; }
    static juce::String kitFolderId(const juce::String& folder) { const int i = kitFolders().indexOf(folder); static const char* id[] { "bs1", "bs2", "syn", "gh1", "gh2", "syn" }; return i >= 0 ? juce::String(id[i]) : juce::String(); }
    static juce::String genIdOf(const juce::String& folder) { const int i = kitFolders().indexOf(folder); static const char* id[] { "bs1", "bs2", "bs3", "gh1", "gh2", "gh3" }; return i >= 0 ? juce::String(id[i]) : juce::String(); }
    // COPY sounds into one of your banks (into = its id) or a new bank called newName: yours stay yours, the rest are copied out as files
    void copyToBank(const juce::StringArray& names, const juce::String& into, const juce::String& newName) {
        namespace ub = ghosty::userbanks; auto L = ub::load(); juce::String id = into; ub::Added a;
        if (id.isEmpty() || ub::bankById(L, id) == nullptr) { id = ub::newBank(L, newName); a.banks = 1; }
        for (auto& n : names) {
            if (proc.isMine(n)) { ub::copySound(L, id, proc.myUid[n], a); continue; }
            juce::MemoryBlock mb; juce::String ext, type; const auto shown = n.fromFirstOccurrenceOf(juce::String::fromUTF8(" \xc2\xb7 "), false, false);
            if (proc.soundBytes(n, mb, ext, type)) ub::addBytes(L, id, shown.isNotEmpty() ? shown : n, ext, mb, type, a); else ++a.skipped;
        }
        if (! ub::save(L)) { ed.say("Couldn't save MY BANKS (is Music/GhostBlock Banks writable?)"); return; }
        juce::String bn; if (auto* b = ub::bankById(L, id)) bn = (*b)["name"].toString();
        banksChanged("Copied " + juce::String(a.sounds) + " sound" + (a.sounds == 1 ? "" : "s") + " into " + bn + (a.skipped ? " (" + juce::String(a.skipped) + " couldn't be copied)" : juce::String()) + ".");
    }
    juce::StringArray folderSounds() const { juce::StringArray n; for (auto i : sounds.items) n.add(proc.lib->sounds()[(size_t) i]->name); return n; }
    juce::String selectedBank() const { return folders.selected >= 0 && folders.selected < folders.items.size() ? myFolderId(folders.items[folders.selected]) : juce::String(); }
    void refreshFolders() {
        const auto was = folders.selected >= 0 && folders.selected < folders.items.size() ? folders.items[folders.selected] : juce::String();
        folders.items = juce::StringArray { "ALL" }; folders.items.addArray(gb::browserFolders()); for (auto& f : kitFolders()) folders.items.add(f); for (auto& b : proc.myBanks) folders.items.add(myPrefix() + b.name); folders.items.add("FAVOURITES"); folders.items.add(kNewBank);
        const int i = folders.items.indexOf(was); folders.selected = i >= 0 ? i : 0; layoutFolders(); folders.repaint();
    }
    void layoutFolders() { if (! folders.getParentComponent()) return; folders.setSize(folders.getWidth(), juce::jmax(10, folders.items.size()) * 30); }
    void banksChanged(const juce::String& msg) { proc.rescanBanks(true); refreshFolders(); fillSounds(); updGenBtn(); if (msg.isNotEmpty()) ed.say(msg); }
    template <class Op> void bankEdit(Op op, const juce::String& msg) {
        auto L = ghosty::userbanks::load(); if (op(L) && ghosty::userbanks::save(L)) banksChanged(msg); else ed.say("Couldn't change MY BANKS (is Music/GhostBlock Banks writable?)");
    }
    static juce::String added(const ghosty::userbanks::Added& a) {
        return "MY BANKS: added " + juce::String(a.sounds) + " sound" + (a.sounds == 1 ? "" : "s") + (a.banks ? " in " + juce::String(a.banks) + " new bank" + (a.banks == 1 ? "" : "s") : juce::String())
             + (a.skipped ? " (" + juce::String(a.skipped) + " skipped" + (a.why.isNotEmpty() ? ": " + a.why : juce::String()) + ")" : juce::String()) + ". Their folders are in the list on the left.";
    }
    void addToBanks(const juce::StringArray& paths, const juce::String& into) {
        juce::Array<juce::File> fs; for (auto& p : paths) fs.add(juce::File(p));
        auto L = ghosty::userbanks::load(); const auto a = ghosty::userbanks::addPaths(L, fs, into, bankFm);
        if (! ghosty::userbanks::save(L)) { ed.say("Couldn't save MY BANKS (is Music/GhostBlock Banks writable?)"); return; }
        banksChanged(a.sounds || a.skipped ? added(a) : juce::String("No sounds found there (WAV, AIFF, FLAC, MP3, OGG)."));
        if (a.lastBank.isNotEmpty()) for (auto& b : proc.myBanks) if (b.id == a.lastBank) { const int i = folders.items.indexOf(myPrefix() + b.name); if (i >= 0) { folders.selected = i; folders.repaint(); fillSounds(); } }
    }
    // files dropped on the window: folders always become banks; files dropped on the folder list go into the bank under them (or a new one)
    bool dropToBanks(const juce::StringArray& files, juce::Point<int> at) {
        bool dirs = false; for (auto& f : files) dirs = dirs || juce::File(f).isDirectory();
        const bool overList = page == 1 && folderView.isVisible() && folderView.getBounds().contains(at);
        if (! dirs && ! overList) return false;
        juce::String into; if (overList) { const int row = (at.y - folderView.getY() + folderView.getViewPositionY()) / 30; if (juce::isPositiveAndBelow(row, folders.items.size())) into = myFolderId(folders.items[row]); }
        addToBanks(files, into); return true;
    }
    void pickForBanks(bool folderMode) {
        const auto into = selectedBank();
        bankChooser = std::make_unique<juce::FileChooser>(folderMode ? "Pick drum folders: each folder becomes a bank" : "Pick drum sounds for your bank", juce::File::getSpecialLocation(juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        juce::Component::SafePointer<Content> safe(this);
        bankChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectMultipleItems | (folderMode ? juce::FileBrowserComponent::canSelectDirectories : juce::FileBrowserComponent::canSelectFiles),
            [safe, into](const juce::FileChooser& fc) { if (! safe) return; juce::StringArray p; for (auto& f : fc.getResults()) p.add(f.getFullPathName()); if (! p.isEmpty()) safe->addToBanks(p, into); });
    }
    void renameBank(const juce::String& id, const juce::String& name) {
        auto* w = new juce::AlertWindow("Rename bank", "A new name for " + name + " (every GhostBlock drum plugin sees it):", juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor("n", name); w->addButton("RENAME", 1, juce::KeyPress(juce::KeyPress::returnKey)); w->addButton("CANCEL", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        juce::Component::SafePointer<Content> safe(this);
        w->enterModalState(true, juce::ModalCallbackFunction::create([safe, w, id](int r) {
            const auto nn = w->getTextEditorContents("n").trim(); if (! safe || r != 1 || nn.isEmpty()) return;
            safe->bankEdit([&](juce::var& L) { return ghosty::userbanks::renameBank(L, id, nn); }, "Bank renamed.");
        }), true);
    }
    void bankMenu() {
        rescanIfNeeded(); const auto id = selectedBank(); juce::String bname; for (auto& b : proc.myBanks) if (b.id == id) bname = b.name;
        const juce::String snd = sounds.selected >= 0 ? sounds.nameAt(sounds.selected) : juce::String(); const bool mineSel = snd.isNotEmpty() && proc.isMine(snd);
        juce::PopupMenu m, ty;
        m.addSectionHeader("MY BANKS: shared with Block Step + Ghost House");
        m.addItem(1, "New empty bank");
        m.addItem(2, id.isNotEmpty() ? "Add sounds to " + bname + "..." : "Add sounds (as a new bank)...");
        m.addItem(3, "Add folders... (each folder becomes a bank)");
        const auto folderName = folders.selected >= 0 && folders.selected < folders.items.size() ? folders.items[folders.selected] : juce::String();
        juce::PopupMenu to; to.addItem(40, "A new bank"); for (size_t i = 0; i < proc.myBanks.size(); ++i) to.addItem(41 + (int) i, proc.myBanks[i].name);
        if (snd.isNotEmpty()) { m.addSeparator(); m.addSubMenu("Copy " + snd + " to", to); }
        if (! sounds.items.empty() && folderName != kNewBank) m.addItem(8, "Copy all " + juce::String((int) sounds.items.size()) + " sounds here into a new bank");
        if (id.isNotEmpty()) { m.addSeparator(); m.addItem(4, "Rename " + bname + "..."); m.addItem(5, "Delete " + bname + " and its sounds..."); }
        if (mineSel) { static const char* N[] = { "KICK", "SNARE", "CLAP", "HAT", "OPEN HAT", "PERC", "808 / BASS", "FX", "CRASH" };
            const auto cur = proc.categoryOf(snd); static const char* C[] = { "Kick", "Snare", "Clap", "Hat", "Open Hat", "Perc", "808", "FX", "Cymbal" };
            for (int i = 0; i < 9; ++i) ty.addItem(20 + i, N[i], true, cur == C[i]); m.addSeparator(); m.addSubMenu("What kind of drum is " + snd + "?", ty);
            m.addItem(6, "Remove " + snd + " from its bank"); }
        m.addSeparator(); m.addItem(7, "Show the GhostBlock Banks folder");
        juce::Component::SafePointer<Content> safe(this);
        const auto all = folderSounds(); const juce::String copyName = (id.isNotEmpty() ? bname : folderName.fromFirstOccurrenceOf(myPrefix(), false, false).isNotEmpty() ? folderName.fromFirstOccurrenceOf(myPrefix(), false, false) : folderName) + " COPY";
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&myBanksBtn), [safe, id, bname, snd, all, copyName](int r) {
            if (! safe || r == 0) return; auto& c = *safe; namespace ub = ghosty::userbanks;
            const int uid = c.proc.myUid.count(snd) ? c.proc.myUid[snd] : -1;
            if (r == 1) c.bankEdit([&](juce::var& L) { ub::newBank(L, {}); return true; }, "New bank: drop sounds on its folder in the list.");
            if (r == 2) c.pickForBanks(false);
            if (r == 3) c.pickForBanks(true);
            if (r == 4) c.renameBank(id, bname);
            if (r == 5) juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon, "Delete " + bname + "?", "This removes the bank and its copied sounds from Music/GhostBlock Banks for every GhostBlock drum plugin. Beats that use them keep their copy.", "DELETE", "CANCEL", safe.getComponent(),
                juce::ModalCallbackFunction::create([safe, id](int ok) { if (safe && ok == 1) safe->bankEdit([&](juce::var& L) { return ghosty::userbanks::deleteBank(L, id); }, "Bank deleted."); }));
            if (r == 6 && uid >= 0) c.bankEdit([&](juce::var& L) { return ub::deleteSound(L, uid); }, "Sound removed from the bank.");
            if (r == 7) { ub::dir().createDirectory(); ub::dir().revealToUser(); }
            if (r == 8) c.copyToBank(all, {}, copyName);
            if (r == 40) c.copyToBank({ snd }, {}, {});
            if (r > 40 && r - 41 < (int) c.proc.myBanks.size()) c.copyToBank({ snd }, c.proc.myBanks[(size_t) (r - 41)].id, {});
            if (r >= 20 && r < 29 && uid >= 0) { static const char* T[] = { "kick", "snare", "clap", "hat", "ohat", "perc", "bass", "fx", "crash" };
                c.bankEdit([&](juce::var& L) { return ub::setType(L, uid, T[r - 20]); }, snd + " is now a " + juce::String(T[r - 20]).toUpperCase() + "."); }
        });
    }
    void rescanIfNeeded() { if (proc.rescanBanks() > 0) { refreshFolders(); fillSounds(); updGenBtn(); } }
    void updGenBtn() {
        genBanksBtn.setButtonText("SOUNDS FROM: " + proc.sourceLabel()); genBanksBtn.setToggleState(proc.kitSource != "stock", juce::dontSendNotification);
        const auto f = folders.selected >= 0 && folders.selected < folders.items.size() ? folders.items[folders.selected] : juce::String();
        const auto id = kitFolderId(f).isNotEmpty() ? kitFolderId(f) : myFolderId(f);
        useBankBtn.setButtonText(id.isNotEmpty() ? (proc.kitSource == id ? juce::String("USING THIS BANK") : juce::String("USE THIS BANK")) : juce::String("USE GHOST KIT SOUNDS"));
        useBankBtn.setEnabled(id.isNotEmpty() || gb::browserFolders().contains(f));
    }
    void setSource(const juce::String& src) {
        if (src != "stock" && ! proc.builtinBanksLoaded) ed.say("Loading the banks...");
        proc.kitSource = src; int n = 0;
        ed.edit([&] { if (src == "stock") { proc.applyKit(proc.currentKit, false); proc.applySource(); } else n = proc.applySource(); });   // (stock: the style's own Ghost Kit sounds back)
        proc.rememberSource(); refreshAll(); updGenBtn();
        ed.say(src == "stock" ? juce::String("The GHOST KIT sounds are back on every track.")
                              : "Sounds from " + proc.sourceLabel() + ": " + juce::String(n) + " track" + (n == 1 ? "" : "s") + " got the closest match (a track keeps its sound when the bank has nothing like it). GENERATE and the styles keep using it.");
    }
    void useSelectedBank() {
        const auto f = folders.selected >= 0 && folders.selected < folders.items.size() ? folders.items[folders.selected] : juce::String();
        const auto id = kitFolderId(f).isNotEmpty() ? kitFolderId(f) : myFolderId(f);
        if (id.isNotEmpty()) setSource(id); else if (gb::browserFolders().contains(f)) setSource("stock"); else ed.say("Pick a bank folder on the left first (BLOCK STEP / GHOST HOUSE / MY ·).");
    }
    void genMenu() {
        rescanIfNeeded(); juce::PopupMenu m; const auto& k = proc.kitSource;
        m.addSectionHeader("The kit's sounds come from:");
        m.addItem(1, "AUTO: the bank that fits each style (your packs first)", true, k == "auto");
        m.addItem(2, "GHOST KIT (Ghosty Beats' own sounds)", true, k == "stock");
        m.addSeparator();
        static const char* ids[] { "bs1", "bs2", "syn", "gh1", "gh2" }; static const char* names[] { "BLOCK STEP BANK 1", "BLOCK STEP BANK 2", "BANK 3: BUILT-IN SYNTH (both)", "GHOST HOUSE BANK 1", "GHOST HOUSE BANK 2" };
        for (int i = 0; i < 5; ++i) m.addItem(20 + i, names[i], true, k == ids[i]);
        if (! proc.myBanks.empty()) m.addSeparator();
        for (size_t i = 0; i < proc.myBanks.size(); ++i) m.addItem(100 + (int) i, juce::String::fromUTF8("MY \xc2\xb7 ") + proc.myBanks[i].name + "  (" + juce::String(proc.myBanks[i].count) + ")", true, k == proc.myBanks[i].id);
        m.addSeparator(); m.addItem(3, "Load them into the kit again now", k != "stock");
        juce::Component::SafePointer<Content> safe(this);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&genBanksBtn), [safe](int r) {
            if (! safe || r == 0) return; auto& c = *safe;
            static const char* ids[] { "bs1", "bs2", "syn", "gh1", "gh2" };
            if (r == 1) c.setSource("auto"); if (r == 2) c.setSource("stock"); if (r == 3) c.setSource(c.proc.kitSource);
            if (r >= 20 && r < 25) c.setSource(ids[r - 20]);
            if (r >= 100 && r - 100 < (int) c.proc.myBanks.size()) c.setSource(c.proc.myBanks[(size_t) (r - 100)].id);
        });
    }
    void pickFile(int t) {
        chooser = std::make_unique<juce::FileChooser>("Pick a sound for track " + juce::String(t + 1), juce::File(), "*.wav;*.aif;*.aiff;*.mp3;*.flac;*.ogg;*.m4a");
        juce::Component::SafePointer<Content> safe(this);
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe, t](const juce::FileChooser& fc) { if (safe && fc.getResult().existsAsFile()) safe->dropFiles({ fc.getResult().getFullPathName() }, t); });
    }
    void dropFiles(const juce::StringArray& files, int t) {
        if (t < 0) t = proc.firstFreeTrack(); if (t < 0) t = ed.track;
        juce::String msg; int n = 0;
        ed.edit([&] { n = proc.loadSamples(t, files, msg);
            for (int k = 0; k < n; ++k) { const auto& tr = proc.model.tracks[(size_t) (t + k)]; if (tr.sample && tr.sample->data.size() > tr.sample->rate * 1.5) proc.chopTrack(t + k, 0.5f, true); } });   // long loops = breaks
        if (n > 0) { ed.track = t; refreshAll(); proc.auditionTrack(t); if (n == 1 && !proc.model.tracks[(size_t) t].slices.empty()) msg += " It's a loop, so it was chopped into " + juce::String((int) proc.model.tracks[(size_t) t].slices.size()) + " slices."; }
        ed.say(msg);
    }
    int trackAt(juce::Point<int> local) {
        for (auto* tl : { &tl0, &tl1, &tl2 }) if (tl->isVisible() && tl->getBounds().contains(local)) return tl->rowAt((float) (local.y - tl->getY()));
        if (grid.isVisible() && grid.getBounds().contains(local)) return (int) ((local.y - grid.getY()) / grid.rowH());
        if (pads.isVisible() && pads.getBounds().contains(local)) return pads.padAt((local - pads.getPosition()).toFloat());
        if (mix.isVisible() && mix.getBounds().contains(local)) { const int t = mix.stripAt((local - mix.getPosition()).toFloat()); return t < gb::kTracks ? t : -1; }
        return -1;   // empty area: a free track
    }
    bool keyPressed(const juce::KeyPress& k) override {
        const auto m = k.getModifiers();
        if (k.getKeyCode() == juce::KeyPress::spaceKey && !m.isAnyModifierKeyDown()) {   // SPACE: OWN = play / stop Beats only; DAW = left to the DAW
            if (proc.ownClock()) { proc.sync.toggleOwn(0.0); return true; }
            if (proc.wrapperType == juce::AudioProcessor::wrapperType_Standalone) { playBtn.setToggleState(!playBtn.getToggleState(), juce::sendNotification); return true; }
            return false;
        }
        if (m.isCommandDown() && k.getKeyCode() == 'Z') { if (m.isShiftDown() ? proc.redo() : proc.undo()) refreshAll(); return true; }
        if (m.isCommandDown() && k.getKeyCode() == 'Y') { if (proc.redo()) refreshAll(); return true; }
        if (m.isCommandDown() && k.getKeyCode() == 'C' && page == 0) { trackTool(9); return true; }
        if (m.isCommandDown() && k.getKeyCode() == 'V' && page == 0) { trackTool(10); return true; }
        return false;
    }

    // layout helpers: compact (1180 x 790) keeps its exact positions; LARGE (1280 x 800 and up) spreads out
    int cb() const { return lh - 104; }                        // bottom of the main card's content
    int rp() const { return large ? lw - 276 : lw - 258; }     // right panel x (compact: 922 at the 1180-wide design; a wider window moves it right, the grid grows)
    int listW() const { return large ? 260 : 220; }
    int gx() const { return large ? 282 : 240; }
    int rowH() const { return large ? juce::jlimit(20, 30, (cb() - 150 - 250) / 16) : 21; }
    int dxp() const { return large ? 40 : 0; }                 // PROCESS page shift

    void paint(juce::Graphics& g) override {
        if (GLASS) { paintGlass(g); return; }
        const float fw = (float) lw, fh = (float) lh;
        if (NEON) { city.draw(g, lw, lh); city.overlay(g, lw, 127.0f, phase); }   // NEON: the rainy city (cached) + live rain / sign flicker in the header
        else if (SUMI) { paperBg.draw(g, lw, lh); sumi::Paper::overlay(g, lw, 127.0f, phase, bounce); }   // LIGHT: the paper (cached) + petals drifting through the header
        else {
        g.fillAll(BG);
        g.setGradientFill(juce::ColourGradient(VIOLET.withAlpha(0.2f), fw * 0.85f, -40.0f, VIOLET.withAlpha(0.0f), fw * 0.85f, 360.0f, true)); g.fillRect(getLocalBounds());
        g.setGradientFill(juce::ColourGradient(PURPLE.withAlpha(0.16f), 0, 0, BG.withAlpha(0.0f), fw * 0.5f, fh * 0.4f, true)); g.fillRect(getLocalBounds());
        }
        if (!NEON && !SUMI) for (auto& q : bgGhosts) {   // (NEON: no floating ghosts, just the city)
            const float x = fw * 0.5f + std::cos(q.a) * q.r * fw * 0.55f, y = 60 + std::sin(q.a) * q.r * 70.0f + std::sin(phase * 1.6f + q.ph) * 4.0f;
            ghosty::drawGhost(g, q.look, x, y, q.sz, 0.0f, false, q.al + bounce * 0.2f, std::cos(q.a + 1.57f) < 0);
        }
        paintLogo(g);   // GHOSTY BEATS (brand): the owner's logo artwork, as is
        auto card = [&](juce::Rectangle<int> r) {
            if (NEON) {   // NEON: a dark painted panel; the city shows faintly through; a hairline of teal light along its top edge
                const auto f = r.toFloat(); g.setColour(juce::Colour(0x66000000)); g.fillRoundedRectangle(f.translated(0, 3.0f).expanded(1.0f), 10);
                const bool low = f.getY() > (float) lh - 120.0f;   // the bottom strip: thinner, the wet street's reflections show through it
                g.setGradientFill(juce::ColourGradient(CARD.withAlpha(low ? 0.78f : 0.90f), 0, f.getY(), BG.withAlpha(low ? 0.58f : 0.94f), 0, f.getY() + (low ? 90.0f : 220.0f), false)); g.fillRoundedRectangle(f, 8);
                g.setColour(DIM.withAlpha(0.8f)); g.drawRoundedRectangle(f.reduced(0.5f), 8, 1);
                {   // cold fluorescent light falling in from above, and the street's amber neon bleeding up from a low corner
                    juce::Graphics::ScopedSaveState ss(g); juce::Path clip; clip.addRoundedRectangle(f, 8); g.reduceClipRegion(clip);
                    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff9fd8cc).withAlpha(0.06f), f.getCentreX(), f.getY(), juce::Colour(0x009fd8cc), f.getCentreX(), f.getY() + 90.0f, false)); g.fillRect(f);
                    g.setGradientFill(juce::ColourGradient(neon::amber().withAlpha(0.07f), f.getRight(), f.getBottom(), neon::amber().withAlpha(0.0f), f.getRight() - 380.0f, f.getBottom(), true)); g.fillRect(f);
                    g.setGradientFill(juce::ColourGradient(neon::magenta().withAlpha(0.05f), f.getX(), f.getBottom(), neon::magenta().withAlpha(0.0f), f.getX() + 300.0f, f.getBottom(), true)); g.fillRect(f);
                    if (low) neoncity::City::reflections(g, f, phase);   // the wet street under the bottom strip
                }
                g.setGradientFill(juce::ColourGradient(PURPLE.withAlpha(0.0f), f.getX(), 0, PURPLE.withAlpha(0.55f), f.getCentreX(), 0, false)); g.fillRect(f.getX() + 10.0f, f.getY() + 0.5f, f.getWidth() * 0.5f - 10.0f, 1.0f);
                g.setGradientFill(juce::ColourGradient(PURPLE.withAlpha(0.55f), f.getCentreX(), 0, PURPLE.withAlpha(0.0f), f.getRight(), 0, false)); g.fillRect(f.getCentreX(), f.getY() + 0.5f, f.getWidth() * 0.5f - 10.0f, 1.0f);
                return; }
            if (SUMI) {   // LIGHT: a sheet of paper laid on the paper, a faint wash bleeding in at two corners, a hand-inked frame that scribbles on the hits
                const auto f = r.toFloat(); juce::Random rnd(r.getY() * 7349 + r.getHeight());
                g.setColour(CARD.withAlpha(0.82f)); g.fillRect(f);
                { juce::Graphics::ScopedSaveState ss(g); g.reduceClipRegion(r);
                  sumi::wash(g, { f.getRight() - 70.0f, f.getBottom() - 30.0f }, juce::jmin(160.0f, f.getHeight() * 0.9f), sumi::indigo(), 0.055f, rnd);
                  sumi::wash(g, { f.getX() + 50.0f, f.getY() + 26.0f }, juce::jmin(110.0f, f.getHeight() * 0.7f), sumi::vermilion(), 0.045f, rnd); }
                if (bounce > 0.08f) rnd.setSeed(r.getY() * 7349 + (juce::int64) (phase * 30.0f));   // a hit: the ink line goes frantic for a moment
                sumi::inkFrame(g, f, rnd, INK.withAlpha(0.80f), 3.0f, 0.6f + bounce * 4.0f);
                sumi::inkFrame(g, f.translated(0.8f, -0.6f), rnd, INK.withAlpha(0.30f), 1.1f, 1.4f + bounce * 5.0f);   // a quick charcoal re-trace beside the ink
                return; }
            g.setColour(CARD.withAlpha(isDark ? 0.88f : 0.96f)); g.fillRoundedRectangle(r.toFloat(), 12); g.setColour(PURPLE.withAlpha(0.22f)); g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 12, 1); };
        if (focusOn()) card({ 8, 128, lw - 16, lh - 136 });
        else {
            card({ 8, 128, lw - 16, lh - 230 }); card({ 8, lh - 96, lw - 16, 90 });
            g.setColour(GREY); g.setFont(font(9.5f));
            g.drawText("MASTER", 18, lh - 94, 120, 11, juce::Justification::left); g.drawText("PERFORM", 474, lh - 94, 120, 11, juce::Justification::left);
            g.drawText("MACROS (automatable)", macroX(), lh - 94, 200, 11, juce::Justification::left);
        }
        if (page == 0) {   // step numbers over the grid
            auto& P = ed.pat(); const float cw = grid.getWidth() / 16.0f;
            for (int c = 0; c < 16; ++c) { const int st = ed.stepPage * 16 + c; const bool beat = std::abs(std::fmod(st * gb::stepBeats(P.res), 1.0)) < 1e-6;
                g.setColour(beat ? AMBER : GREY.withAlpha(0.7f)); g.setFont(font(9.5f)); g.drawText(juce::String(st + 1), (int) (grid.getX() + c * cw), grid.getY() - 13, (int) cw, 12, juce::Justification::centred); }
        }
        if ((page == 0 && !focusOn()) || page == 1) { g.setColour(CARD2.withAlpha(0.6f)); g.fillRoundedRectangle((float) rp() - 10, 134.0f, (float) (lw - 8 - rp() + 4), (float) cb() - 134 + 2, 10.0f); }
        if (page == 2) { g.setColour(CARD2.withAlpha(0.5f)); const int x0 = 244 + dxp(), w0 = lw - 12 - x0;
            for (auto r : { juce::Rectangle<int>(x0, 134, w0, 126), { x0, 266, w0, 132 }, { x0, 404, w0, 92 }, { x0, 502, w0, cb() - 502 + 2 } }) g.fillRoundedRectangle(r.toFloat(), 10); }
        if (page == 7) { g.setColour(CARD2.withAlpha(0.5f)); g.fillRoundedRectangle((float) scriptHelp.getX() - 8, 134.0f, (float) (lw - 8 - scriptHelp.getX() + 4), (float) cb() - 134 + 2, 10.0f); }
    }
    int macroX() const { return large ? lw - 16 - 8 * 56 : lw - 412; }   // (compact: 768 at the 1180-wide design)
    // GHOST: the aurora (cached per size), the floating glass cards (cached per size), the plates under the wordmark + mascot: no text (the contrast
    // tests sample it). paintGlass adds the header ghosts, the wordmark, the mascot and the same captions in deep indigo.
    void paintGlassSurfaces(juce::Graphics& g) {
        g.setOpacity(1.0f); backdrop.draw(g, lw, lh);
        auto card = [&](juce::Rectangle<int> r) { g.setOpacity(1.0f); panels.draw(g, r.toFloat(), 18.0f, 0.36f, 12.0f); };   // (a blit takes the colour's alpha)
        if (focusOn()) card({ 8, 128, lw - 16, lh - 136 });
        else { card({ 8, 128, lw - 16, lh - 230 }); card({ 8, lh - 96, lw - 16, 90 }); }
        auto sub = [&](juce::Rectangle<int> r) { g.setOpacity(1.0f); panels.draw(g, r.toFloat(), 14.0f, 0.26f, 0.0f); };
        if ((page == 0 && !focusOn()) || page == 1) sub({ rp() - 10, 134, lw - 8 - rp() + 4, cb() - 134 + 2 });
        if (page == 2) { const int x0 = 244 + dxp(), w0 = lw - 12 - x0;
            for (auto r : { juce::Rectangle<int>(x0, 134, w0, 126), { x0, 266, w0, 132 }, { x0, 404, w0, 92 }, { x0, 502, w0, cb() - 502 + 2 } }) sub(r); }
        if (page == 7) sub({ scriptHelp.getX() - 8, 134, lw - 8 - scriptHelp.getX() + 4, cb() - 134 + 2 });
    }
    juce::Rectangle<float> logoRect() const { return { 6.0f, 1.0f, 208.0f, 55.0f }; }   // the GHOSTY BEATS artwork (3.68 : 1; the kit name box starts at 222, the pattern pads at y 56)
    juce::Rectangle<float> mascotRect() const { return { 150.0f, 1.0f, 56.0f, 46.0f }; }   // the headphones ghost inside the artwork
    // the logo artwork is made for a dark backdrop: on LIGHT and GHOST (glass) it sits on a dark plate of its own
    void paintLogo(juce::Graphics& g) {
        const auto r = logoRect();
        if (NEON) { neonLogo.draw(g, r, BeatsXProcessor::kVersion, phase); return; }   // NEON: the neon-sign wordmark
        if (SUMI) { drawLogo(g, r, true); return; }   // LIGHT: the artwork faded like a print on the paper (no black plate)
        if (GLASS || ! isDark) { g.setColour(juce::Colour(0xff100718).withAlpha(0.94f)); g.fillRoundedRectangle(r.expanded(4.0f, 1.0f).withTrimmedTop(-1.0f), 10.0f); }
        drawLogo(g, r);
    }
    void paintGlass(juce::Graphics& g) {
        const float fw = (float) lw;
        paintGlassSurfaces(g);
        for (auto& q : bgGhosts) {
            const float x = fw * 0.5f + std::cos(q.a) * q.r * fw * 0.55f, y = 60 + std::sin(q.a) * q.r * 70.0f + std::sin(phase * 1.6f + q.ph) * 4.0f;
            ghosty::drawGhost(g, q.look, x, y, q.sz, 0.0f, false, q.al + bounce * 0.2f, std::cos(q.a + 1.57f) < 0);
        }
        paintLogo(g);
        if (!focusOn()) {
            g.setColour(GREY); g.setFont(font(9.5f));
            g.drawText("MASTER", 18, lh - 94, 120, 11, juce::Justification::left); g.drawText("PERFORM", 474, lh - 94, 120, 11, juce::Justification::left);
            g.drawText("MACROS (automatable)", macroX(), lh - 94, 200, 11, juce::Justification::left);
        }
        if (page == 0) {   // step numbers over the grid
            auto& P = ed.pat(); const float cw = grid.getWidth() / 16.0f;
            for (int c = 0; c < 16; ++c) { const int st = ed.stepPage * 16 + c; const bool beat = std::abs(std::fmod(st * gb::stepBeats(P.res), 1.0)) < 1e-6;
                g.setColour(beat ? AMBER : GREY); g.setFont(font(9.5f)); g.drawText(juce::String(st + 1), (int) (grid.getX() + c * cw), grid.getY() - 13, (int) cw, 12, juce::Justification::centred); }
        }
    }

    void resized() override {
        const int CB = cb(), RP = rp(), LW = listW(), GX = gx(); laidOutFocus = focusOn();
        // header row 1 (the drag handles sit at the right edge)
        versionTip.setBounds(6, 1, 208, 55);
        // (2026-11-07: TEMPO ½x | 1x | 2x sits next to the BPM and TAP; GROOVE moved to the third row, next to SONG MODE)
        kitBox.setBounds(222, 12, 150, 32); playBtn.setBounds(376, 12, 50, 32); bpm.setBounds(430, 15, 96, 26); tapBtn.setBounds(530, 14, 34, 28); tempoSw.setBounds(568, 9, 146, 38);
        undoBtn.setBounds(718, 12, 50, 32); redoBtn.setBounds(772, 12, 50, 32);
        // BYPASS (84 x 26) top-right with the PLAY: ALWAYS | ON NOTES switch under it; the drag handles to their left
        const int colW = 106, hw = large ? 140 : 112, ax = lw - 12 - colW - 8 - hw, mx = ax - hw - 4;
        midiOut.setBounds(mx, 6, hw, 46); audioOut.setBounds(ax, 6, hw, 46); bypassBtn.setBounds(lw - 12 - 84 - (colW - 84) / 2, 4, 84, 26); playModeSw.setBounds(lw - 12 - colW, 33, colW, 19);
        // row 2
        for (int i = 0; i < gb::kPatterns; ++i) patBtns[i]->setBounds(16 + i * 30, 56, 28, 28);
        lenBox.setBounds(500, 56, 92, 28); resBox.setBounds(596, 56, 70, 28); copyPat.setBounds(670, 56, 50, 28); pastePat.setBounds(722, 56, 52, 28); clearPat.setBounds(776, 56, 50, 28);
        { int x = mx; auto put = [&](juce::Component& c, int w) { c.setBounds(x, 56, w, 24); x += w + 3; };   // under the drag handles + BYPASS, left of the mascot
          put(patBtn, 62); put(songBtn, 48); put(chBtn, 38); put(mixBtn, large ? 64 : 56); put(stemsBtn, large ? 76 : 66); }
        // row 3
        int pagesEnd = 16; { int x = 16; const int wd[] = { 80, 58, 60, 60, 76, 42, 44, 58, 36, 58 }; for (int i = 0; i < kPages; ++i) { pageBtns[i]->setBounds(x, 92, wd[i], 30); x += wd[i] + 4; } pagesEnd = x; }
        const bool lic = unlock.isVisible(); const int rightX = lw - 12 - (lic ? 86 : 0);
        themeBtn.setBounds(rightX - 56, 92, 56, 30); focusBtn.setBounds(rightX - 56 - 64, 92, 60, 30); syncBtn.setBounds(rightX - 56 - 64 - 100, 94, 96, 26); keyBox.setBounds(rightX - 56 - 64 - 100 - 100, 94, 96, 26);
        const int smx = juce::jmax(620, pagesEnd + 2); songModeBtn.setBounds(smx, 92, 88, 30); grooveBox.setBounds(smx + 92, 92, 94, 30); status.setBounds(smx + 190, 94, juce::jmax(0, rightX - 56 - 64 - 100 - 100 - smx - 194), 26); unlock.setBounds(lw - 94, 92, 82, 30);
        // SEQUENCER
        int rh = rowH(), gw = RP - 10 - GX - (large ? 6 : 8), gridH = rh * 16;
        if (focusOn()) { const int lanesH = 150; rh = juce::jlimit(21, 34, (lh - 16 - 150 - lanesH - 34) / 16); gridH = rh * 16; gw = lw - 16 - GX; }
        tl0.setBounds(16, 150, LW, gridH); grid.setBounds(GX, 150, gw, gridH);
        const int y0 = 150 + gridH + 6;
        if (focusOn()) {
            { const int tabW = (gw - 120) / laneBtns.size(); for (int i = 0; i < laneBtns.size(); ++i) laneBtns[i]->setBounds(GX + i * tabW, y0, tabW - 3, 22); }
            pagePrev.setBounds(lw - 16 - 110, y0, 24, 22); pageLbl.setBounds(lw - 16 - 84, y0, 58, 22); pageNext.setBounds(lw - 16 - 24, y0, 24, 22);
            lanes.setBounds(GX, y0 + 26, gw, lh - 16 - (y0 + 26));
        } else if (!large) {
            const int lanesW = RP - 18 - 240, lanesR = 240 + lanesW, lanesH = juce::jmax(164, CB - 4 - 518);   // 664 x 164 at the 1180 x 790 design; grows with the window
            { const int tabW = (lanesW - 112) / laneBtns.size(); for (int i = 0; i < laneBtns.size(); ++i) laneBtns[i]->setBounds(240 + i * tabW, 492, tabW - 2, 22); }
            pagePrev.setBounds(lanesR - 110, 492, 24, 22); pageLbl.setBounds(lanesR - 84, 492, 58, 22); pageNext.setBounds(lanesR - 24, 492, 24, 22);
            lanes.setBounds(240, 518, lanesW, lanesH);
            for (int i = 0; i < 12; ++i) toolBtns[i]->setBounds(16 + (i % 3) * 74, 492 + (i / 3) * 30, 70, 26);
            density.setBounds(16, 614, 218, 24); loopBtn.setBounds(16, 646, 218, 30);
        } else {
            const int lanesW = gw; int tabW = (lanesW - 116) / laneBtns.size(); const bool twoRows = tabW < 46; int tabsH = 22;   // narrow windows: the lane tabs take two rows instead of truncating
            if (twoRows) { tabW = (lanesW - 116) / 6; tabsH = 46; }
            for (int i = 0; i < laneBtns.size(); ++i) laneBtns[i]->setBounds(GX + (twoRows ? (i % 6) : i) * tabW, y0 + (twoRows && i >= 6 ? 24 : 0), tabW - 2, 22);
            pagePrev.setBounds(GX + lanesW - 110, y0, 24, 22); pageLbl.setBounds(GX + lanesW - 84, y0, 58, 22); pageNext.setBounds(GX + lanesW - 24, y0, 24, 22);
            const int lh2 = (CB - (y0 + tabsH + 4)) / 2 - 2;
            lanesVel.setBounds(GX, y0 + tabsH + 4, lanesW, lh2); lanes.setBounds(GX, y0 + tabsH + 8 + lh2, lanesW, CB - (y0 + tabsH + 8 + lh2));
            for (int i = 0; i < 12; ++i) toolBtns[i]->setBounds(16 + (i % 3) * 87, y0 + (i / 3) * 30, 83, 26);
            density.setBounds(16, y0 + 122, LW, 24); loopBtn.setBounds(16, y0 + 154, LW, 30);
        }
        trackTitle.setBounds(RP, 140, 236, 22); soundName.setBounds(RP, 162, 236, 18);
        prevSnd.setBounds(RP, 184, 30, 28); browseBtn.setBounds(RP + 34, 184, 80, 28); loadSmp.setBounds(RP + 118, 184, 90, 28); nextSnd.setBounds(RP + 212, 184, 28, 28);
        for (int i = 0; i < 9; ++i) trackKnobs[i]->setBounds(RP + 2 + (i % 3) * 80, 218 + (i / 3) * 76, 76, 72);
        tLenBox.setBounds(RP, 448, 116, 28); chokeBox.setBounds(RP + 120, 448, 120, 28);
        revBtn.setBounds(RP, 482, 116, 28); lockBtn.setBounds(RP + 120, 482, 120, 28);
        chopSens.setBounds(RP, 518, 240, 24); chopBtn.setBounds(RP, 546, 116, 28); rearrBtn.setBounds(RP + 120, 546, 120, 28); regTab.setBounds(RP, 578, 118, 20); sliceTab.setBounds(RP + 122, 578, 118, 20); slices.setBounds(RP, 600, 240, CB - 602); region.setBounds(RP, 600, 240, CB - 602);
        // SOUNDS
        tl1.setBounds(16, 150, LW, gridH); { const int fw = large ? 170 : 150; folderView.setBounds(GX + 4, 136, fw, juce::jmax(60, CB - 136)); folders.setBounds(0, 0, fw - 8, juce::jmax(10, folders.items.size()) * 30); }
        { const int sx = GX + (large ? 182 : 162); search.setBounds(sx, 136, RP - 18 - sx, 28); soundView.setBounds(sx, 170, RP - 18 - sx, CB - 170); sounds.setSize(RP - 30 - sx, sounds.getHeight()); sounds.layout(); }
        loadOnTrack.setBounds(RP, 144, 236, 32); loadFile.setBounds(RP, 182, 236, 28); normBtn.setBounds(RP, 216, 236, 28);
        useBankBtn.setBounds(RP, 250, 236, 32); myBanksBtn.setBounds(RP, 286, 236, 28); genBanksBtn.setBounds(RP, 318, 236, 28);
        soundInfo.setBounds(RP, 352, 236, 96); dropHelp.setBounds(RP, 452, 236, 160);
        // PROCESS
        { const int d = dxp(); tl2.setBounds(16, 150, LW, gridH);
          procTitle.setBounds(252 + d, 136, 700, 16); { const int kw = (lw - 12 - (254 + d) - 8) / 13; for (int i = 0; i < 13; ++i) fxKnobs[i]->setBounds(254 + d + i * kw, 154, kw - 4, 76); }
          { const int cx = lw - 12 - 8 - 300; scBox.setBounds(cx, 266, 300, 24); charBox.setBounds(cx, 296, 170, 26); charKnobs[0]->setBounds(cx + 176, 290, 60, 56); charKnobs[1]->setBounds(cx + 240, 290, 60, 56); }
          layersTitle.setBounds(252 + d, 268, lw - 12 - 8 - 300 - 8 - (252 + d), 16);   // stops before the sidechain / CHARACTER block
          for (int l = 0; l < gb::kLayers; ++l) { layerBtns[l]->setBounds(254 + d, 296 + l * 52, 330, 30); for (int j = 0; j < 3; ++j) layerKnobs[l * 3 + j]->setBounds(596 + d + j * 76, 284 + l * 54, 70, 54); }
          busTitle.setBounds(252 + d, 406, 700, 16); for (int i = 0; i < 6; ++i) busKnobs[i]->setBounds(254 + d + i * 86, 420, 76, 72); limiterBox.setBounds(254 + d + 6 * 86 + 8, 440, juce::jmin(260, lw - 12 - 8 - (254 + d + 6 * 86 + 8)), 28);
          rulesTitle.setBounds(252 + d, 504, 700, 16);
          const int rw = lw - 12 - (254 + d) - 8;
          for (int r = 0; r < 4; ++r) { const int y = 526 + r * 38; ruleType[r]->setBounds(254 + d, y, rw - 474, 30); ruleSrc[r]->setBounds(254 + d + rw - 468, y, 150, 30); ruleN[r]->setBounds(254 + d + rw - 312, y, 90, 30); rulePct[r]->setBounds(254 + d + rw - 216, y + 2, 216, 26); } }
        // TRACKER
        tracker.setBounds(16, 168, lw - 28, CB - 168); bank1.setBounds(16, 136, 110, 28); bank2.setBounds(130, 136, 110, 28); previewBox.setBounds(246, 136, 200, 28); trackerHelp.setBounds(454, 136, lw - 468, 28);
        // RHYTHM LAB
        lab.setBounds(12, 132, lw - 24, CB - 134);
        // MIX
        mix.setBounds(16, 140, lw - 32, CB - 142);
        // MELODY
        melOut.setBounds(lw - 16 - 160, 134, 160, 30); melBoth.setBounds(lw - 16 - 160 - 6 - 196, 134, 196, 30); melHelp.setBounds(16, 136, lw - 16 - 160 - 6 - 196 - 28, 28); melView.setBounds(16, 168, lw - 28, CB - 168); trkBtn.setBounds(melView.getRight() - 150, melView.getBottom() - 44, 142, 22);
        // PADS
        { const int ph = juce::jmin(CB - 146, large ? 700 : 540), pw = ph * 600 / 540; pads.setBounds(240, 140, pw, ph);
          const int px = 240 + pw + 30; recBtn.setBounds(px, 150, 130, 40); repeatBtn.setBounds(px, 200, 130, 34); repeatDiv.setBounds(px + 136, 200, 150, 34); padHelp.setBounds(px, 250, juce::jmin(290, lw - px - 16), 200); }
        // SONG
        for (int i = 0; i < gb::kPatterns; ++i) chips[i]->setBounds(24 + (i % 4) * 46, 150 + (i / 4) * 46, 44, 44);
        lane.setBounds(220, 150, lw - 240, large ? 240 : 180); { const int y = large ? 400 : 340; delBlock.setBounds(220, y, 130, 30); dupBlock.setBounds(356, y, 110, 30); clrSong.setBounds(472, y, 110, 30); songHelp.setBounds(220, y + 40, lw - 240, 60); }
        // SCRIPTS
        { const int hx = lw - (large ? 330 : 280); scriptList.setBounds(16, 140, 230, CB - 140); scriptName.setBounds(254, 140, juce::jmax(160, hx - 254 - 5 * 98 - 12), 28);
          int bx = hx - 12 - 5 * 98; for (auto* b : { &scrNew, &scrApply, &scrDel, &scrSave, &scrLoad }) { b->setBounds(bx, 140, 94, 28); bx += 98; }
          scriptCode.setBounds(254, 174, hx - 254 - 14, CB - 174 - 50); scriptErr.setBounds(254, CB - 46, hx - 254 - 14, 46); scriptHelp.setBounds(hx, 140, lw - hx - 16, CB - 140); }
        // bottom
        for (int i = 0; i < 8; ++i) masterKnobs[i]->setBounds(16 + i * 56, lh - 82, 54, 72);
        { stutterLbl.setBounds(474, lh - 80, 62, 30); stutter.setBounds(538, lh - 80, 222, 30); perf[0]->setBounds(474, lh - 44, 100, 30); saveBeatBtn.setBounds(578, lh - 44, 92, 30); loadBeatBtn.setBounds(674, lh - 44, 86, 30); }
        for (int i = 0; i < 8; ++i) macroKnobs[i]->setBounds(macroX() + i * (large ? 56 : 50), lh - 82, large ? 56 : 50, 72);
    }
};

// =====================================================================================================
BeatsXEditor::BeatsXEditor(BeatsXProcessor& p) : AudioProcessorEditor(p), proc(p) {
    applyLook(p.look());   // before any child picks its colours
    lnf = std::make_unique<LnF>(); setLookAndFeel(lnf.get());
    content = std::make_unique<Content>(p, *this); addAndMakeVisible(*content); content->setBounds(0, 0, W, H);
    content->sendLookAndFeelChange();   // sliders built their text boxes with the default look before they had a parent: re-read ours
    setResizable(true, true); setResizeLimits(W * 3 / 4, H * 3 / 4, 2560, 1600);   // any shape: 1280 x 800 and up switches to the LARGE layout
    // Brandon 2026-11-06: open at the size that fits FL's plug-in window on his 1440 x 900 MacBook Pro (about 1200 x 760, never wider
    // than 1240 or taller than 780): the 1180 x 790 design, scaled to fit. Always this size on open, never the last size.
    setSize(kOpenW, kOpenH); startTimerHz(30);
}
BeatsXEditor::~BeatsXEditor() { stopTimer(); content.reset(); setLookAndFeel(nullptr); }
void BeatsXEditor::paint(juce::Graphics& g) { g.fillAll(BG); }
void BeatsXEditor::resized() {
    if (!content) return;
    const float w = (float) getWidth(), h = (float) getHeight();
    const bool large = w >= 1280 && h >= 780;
    if (large) { scale = juce::jmax(0.85f, juce::jmin(w / 1600.0f, h / 1000.0f)); content->setLayout((int) (w / scale), (int) (h / scale), true); offX = offY = 0; }
    // Brandon 2026-11-12: no black bars at the sides (or top / bottom). The 1180 x 790 design is scaled to fit, then the layout itself is made
    // wider (or taller) to fill the rest of the window: the grid, the lanes and the city backdrop stretch, nothing is letterboxed.
    else { scale = juce::jmin(w / W, h / H); content->setLayout(juce::jmax(W, (int) std::ceil(w / scale)), juce::jmax(H, (int) std::ceil(h / scale)), false); offX = offY = 0; }
    content->setTransform(juce::AffineTransform::scale(scale).translated(offX, offY));
}
void BeatsXEditor::showPageForTest(int page) { if (content) content->setPage(page); }
void BeatsXEditor::setScriptTextForTest(const juce::String& code) { if (content) { content->scriptCode.setText(code, false); content->checkScript(false); } }
void BeatsXEditor::setThemeForTest(bool dark) { if (content) content->setTheme(dark ? th::Theme::Dark : th::Theme::Light); }
void BeatsXEditor::setThemeForTest(int theme) { if (content) content->setLook(theme); }
juce::TextButton* BeatsXEditor::themeButtonForTest() { return content ? &content->themeBtn : nullptr; }
int BeatsXEditor::lookForTest() const { return content ? content->myLook : -1; }
juce::Component* BeatsXEditor::contentForTest() { return content.get(); }
void BeatsXEditor::paintGlassSurfacesForTest(juce::Graphics& g) { if (content) content->paintGlassSurfaces(g); }
juce::Rectangle<float> BeatsXEditor::mascotRectForTest() const { return content ? content->mascotRect() : juce::Rectangle<float>(); }
int BeatsXEditor::glassBuildsForTest(int which) const { return !content ? -1 : which == 0 ? content->backdrop.builds : which == 1 ? content->panels.builds : glassui::sprites().builds; }
bool BeatsXEditor::keyPressForTest(const juce::KeyPress& k) { return content && content->keyPressed(k); }
void BeatsXEditor::syncUiForTest() { if (content) content->updateSyncUi(); }
juce::String BeatsXEditor::padHelpForTest() { if (!content) return {}; content->updatePadHelp(); return content->padHelp.getText(); }
juce::Component* BeatsXEditor::partForTest(const juce::String& w) {
    if (!content) return nullptr; auto& c = *content;
    if (w == "stutter") return &c.stutter; if (w == "melody") return &c.melView; if (w == "linear") return &c.lab.linear; if (w == "lab") return &c.lab;
    if (w == "tracker") return &c.tracker; if (w == "grid") return &c.grid; if (w == "lanes") return &c.lanes;
    if (w == "midiOut") return &c.midiOut; if (w == "audioOut") return &c.audioOut; if (w == "melOut") return &c.melOut; if (w == "melBoth") return &c.melBoth;
    if (w == "stems") return &c.stemsBtn; if (w == "tl0") return &c.tl0; if (w == "sounds") return &c.sounds; if (w == "trkBtn") return &c.trkBtn; if (w == "tempo") return &c.tempoSw; if (w == "groove") return &c.grooveBox; return nullptr;
}
void BeatsXEditor::exportModuleForTest(bool it) { if (content) content->exportModule(it); }
void BeatsXEditor::refreshLook() { if (auto* l = dynamic_cast<LnF*>(lnf.get())) l->applyColours(); sendLookAndFeelChange(); }
void BeatsXEditor::timerCallback() {
    proc.drainRecorded();
    auto& c = *content;
    proc.drainMelodyMidi([&](int note, int vel, double beat, bool on) { if (!on) return; int pp = -1, rr = -1; if (beat >= 0) proc.melodyRowAt(beat, pp, rr); c.melView.recordNote(note, vel, pp, rr); });   // MELODY MIDI REC
    if (c.myLook != LOOK) c.setLook(LOOK);   // another Ghosty Beats window switched the look
    c.tickGhosts();
    switch (c.page) { case 0: c.grid.repaint(); c.tl0.repaint(); c.lanes.repaint(); if (c.large && !c.focusOn()) { c.lanesVel.repaint(); c.trackerMini.repaint(); } break; case 1: c.tl1.repaint(); break; case 2: c.tl2.repaint(); break; case 3: c.tracker.repaint(); break;
                      case 4: c.lab.arc.repaint(); c.lab.tickStrip(); break; case 5: c.pads.repaint(); break; case 6: c.lane.repaint(); break; case 8: c.mix.repaint(); break; case 9: c.melView.tick(); c.melView.repaint(); break; default: break; }
    if (++c.fractureTick >= 30) { c.fractureTick = 0; if (proc.apvts.getRawParameterValue("fracture")->load() > 0.001f && proc.variantsMissing()) proc.requestVariants(true); }   // FRACTURE needs the destruction renders
    if (c.page == 1 && ++sharedTick >= 60) { sharedTick = 0; if (proc.rescanShared() > 0) { c.fillSounds(); c.ed.say("New shared sound(s) found in Documents/Ghosty/Shared Sounds (SHARED folder)."); } c.rescanIfNeeded(); }   // every 2 s while the browser is open
    c.updateSyncUi(); if (proc.playMode.load() != c.padHelpMode) c.updatePadHelp();
    { const bool on = proc.ownClock() ? proc.sync.isOwnPlaying() : proc.engine.internalPlay; if (c.playBtn.getToggleState() != on) c.playBtn.setToggleState(on, juce::dontSendNotification); }
    const int pi = c.ed.patIdx(); if (!c.patBtns[pi]->getToggleState()) c.refreshAll();   // pattern changed by automation
    if (c.undoBtn.isEnabled() != proc.canUndo() || c.redoBtn.isEnabled() != proc.canRedo()) { c.undoBtn.setEnabled(proc.canUndo()); c.redoBtn.setEnabled(proc.canRedo()); }
}
static bool isAudioFile(const juce::String& f) { return juce::File(f).hasFileExtension("wav;aif;aiff;mp3;flac;ogg;m4a;caf"); }
bool BeatsXEditor::isInterestedInFileDrag(const juce::StringArray& files) { for (auto& f : files) if (isAudioFile(f) || juce::File(f).isDirectory()) return true; return false; }
void BeatsXEditor::filesDropped(const juce::StringArray& files, int x, int y) {
    const auto local = juce::Point<int>((int) ((x - offX) / scale), (int) ((y - offY) / scale));
    if (content->dropToBanks(files, local)) { content->showDropHint(local, false); return; }   // MY BANKS: folders, or files dropped on the folder list
    juce::StringArray audio; for (auto& f : files) if (isAudioFile(f)) audio.add(f);
    content->showDropHint(local, false); if (audio.isEmpty()) return;
    if (content->dropZoneAt(local) == 1) { content->startRecreate(juce::File(audio[0]), content->trackAt(local)); return; }   // on the grid: RE-CREATE AS MIDI
    content->dropFiles(audio, content->trackAt(local));
}
void BeatsXEditor::fileDragEnter(const juce::StringArray& files, int x, int y) { fileDragMove(files, x, y); }
void BeatsXEditor::fileDragMove(const juce::StringArray&, int x, int y) { content->showDropHint(juce::Point<int>((int) ((x - offX) / scale), (int) ((y - offY) / scale)), true); }
void BeatsXEditor::fileDragExit(const juce::StringArray&) { content->showDropHint({}, false); }
void BeatsXEditor::startRecreateForTest(const juce::File& f) { if (content) content->startRecreate(f, 0); }
bool BeatsXEditor::recreateBusyForTest() { return content && (content->recreate.job != nullptr); }
juce::String BeatsXEditor::recreateStatusForTest() { return content ? content->recreate.status.getText() : juce::String(); }
ghosty::WaveRegion* BeatsXEditor::regionForTest() { return content ? &content->region : nullptr; }
bx::SliceView* BeatsXEditor::slicesForTest() { return content ? &content->slices : nullptr; }
void BeatsXEditor::showSlicesForTest(bool on) { if (content) { content->showSlices = on; content->syncRegionTabs(); } }
juce::Component* BeatsXEditor::recreatePanelForTest() { return content ? &content->recreate : nullptr; }
void BeatsXEditor::importModuleForTest(const juce::File& f) { if (content) content->importModule(f); }
void BeatsXEditor::closeRecreateForTest() { if (content) content->recreate.setVisible(false); }
void BeatsXEditor::recreateTickForTest() { if (content && content->recreate.job) content->recreate.timerCallback(); }
