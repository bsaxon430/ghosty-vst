#pragma once
// RE-CREATE AS MIDI panel: drop an audio loop on the pattern grid (or LOOP -> MIDI...). The file is analysed on a background thread
// (progress + CANCEL); the result is applied at once as ONE undo step (pattern shown, toast in the status bar), and this panel stays
// up to fine-tune it: DRUMS | PITCHED, the tempo (x2, /2, type it), QUANTIZE strength + GRID, "use the loop's own hits as the sounds".
// Each change re-applies in place of the last result (still one undo step). USE AS SOUND / CHOP TO SLICES = the old drop behaviour.
// DRUMS HEARD (below): what the loop was heard as ("8 kick, 4 snare, 16 hat ..."), one chip per drum: click to leave that drum out of
// the pattern (or put it back); the pattern re-applies at once, so it is heard before CLOSE.
#include "EditorParts.h"
#include "AudioToMidi.h"

namespace bx {

struct RecreatePanel : juce::Component, private juce::Timer {
    Ed& ed;
    std::function<void(const juce::File&, int track)> useAsSound, chopToSlices;   // the old drop behaviour (Content)
    std::function<void()> refresh;                                                // after an apply: the editor re-reads the model
    juce::File file; int dropTrack = -1;
    std::shared_ptr<gb::a2m::Loop> loop; gb::a2m::Result result; gb::a2m::Options opts;
    bool applied = false; int appliedDepth = -1;
    // the analysis job
    struct Job : juce::Thread {
        RecreatePanel* owner; juce::File f; gb::a2m::Options o; std::shared_ptr<gb::a2m::Loop> have;
        std::atomic<bool> cancel { false }; std::atomic<float> progress { 0 }; std::shared_ptr<gb::a2m::Loop> loop; gb::a2m::Result r; juce::String err; std::atomic<bool> done { false };
        Job() : juce::Thread("Ghosty Beats loop analysis") {}
        void run() override {
            if (have) loop = have; else { loop = std::make_shared<gb::a2m::Loop>(); if (!gb::a2m::readFile(f, *loop, err)) { done = true; return; } }
            progress.store(0.05f); if (!cancel.load()) r = gb::a2m::analyse(*loop, o, &cancel, &progress); done = true;
        }
    };
    std::unique_ptr<Job> job;

    juce::Label title, info, status; juce::TextButton drumsBtn { "DRUMS" }, pitchedBtn { "PITCHED" }, x2 { "x2" }, half { "/2" }, cancelBtn { "CANCEL" },
        recreateBtn { "RE-CREATE AS PATTERN" }, soundBtn { "USE AS SOUND ON THIS TRACK" }, chopBtn { "CHOP TO SLICES" }, closeBtn { "CLOSE" };
    Bar bpm { juce::Slider::LinearBar, juce::Slider::TextBoxRight }, quant { juce::Slider::LinearBar, juce::Slider::TextBoxRight };
    juce::ComboBox grid; juce::ToggleButton useHits { "also use the loop's own hits as the sounds" };
    juce::Label heard; std::array<juce::TextButton, gb::a2m::kNumRoles> chips; uint32_t roleMask = 0xFFFFFFFFu;   // DRUMS HEARD
    float prog = 0; bool busy = false;

    explicit RecreatePanel(Ed& e) : ed(e) {
        title.setText("RE-CREATE AS MIDI", juce::dontSendNotification); title.setFont(font(15.0f)); title.setColour(juce::Label::textColourId, INK);
        for (auto* l : { &info, &status }) { l->setFont(font(12.0f, false)); l->setColour(juce::Label::textColourId, GREY); l->setJustificationType(juce::Justification::topLeft); }
        for (juce::Component* c : std::initializer_list<juce::Component*> { &title, &info, &status, &drumsBtn, &pitchedBtn, &x2, &half, &bpm, &quant, &grid, &useHits, &cancelBtn, &recreateBtn, &soundBtn, &chopBtn, &closeBtn }) addAndMakeVisible(c);
        for (auto* b : { &drumsBtn, &pitchedBtn }) { b->setClickingTogglesState(false); b->getProperties().set("tint", (juce::int64) GREEN.getARGB()); }
        recreateBtn.getProperties().set("accent", true);
        bpm.setRange(20, 999, 0.1); bpm.setTextValueSuffix(" BPM"); bpm.setDoubleClickReturnValue(false, 120); bpm.setColour(juce::Slider::trackColourId, juce::Colours::transparentBlack);
        bpm.setTooltip("The loop's tempo (found from the file name, its length or its hits). Right-click to type it; x2 / /2 fix a double / half tempo.");
        quant.setRange(0, 100, 1); quant.setValue(100, juce::dontSendNotification); quant.setTextValueSuffix(" % quantize"); quant.setDoubleClickReturnValue(true, 100); quant.setColour(juce::Slider::trackColourId, PURPLE.withAlpha(0.4f));
        quant.setTooltip("100 % = every hit on the grid; less keeps that much of the loop's own timing (as step NUDGE)");
        grid.addItemList({ "grid 1/8", "grid 1/16", "grid 1/32" }, 1); grid.setSelectedItemIndex(1, juce::dontSendNotification);
        useHits.setColour(juce::ToggleButton::textColourId, INK); useHits.setColour(juce::ToggleButton::tickColourId, GREEN);
        useHits.setTooltip("Off: MIDI only (the kit's own sounds play the new pattern). On: the loop's own strongest hit of each kind becomes that track's sound.");
        heard.setFont(font(11.5f, false)); heard.setColour(juce::Label::textColourId, INK); heard.setJustificationType(juce::Justification::centredLeft); addAndMakeVisible(heard);
        for (int r = 0; r < gb::a2m::kNumRoles; ++r) { auto& c = chips[(size_t) r]; c.setClickingTogglesState(true); c.getProperties().set("tint", (juce::int64) GREEN.getARGB()); addChildComponent(c);
            c.onClick = [this, r] { const auto bit = 1u << r; roleMask = chips[(size_t) r].getToggleState() ? (roleMask | bit) : (roleMask & ~bit); apply(); }; }
        drumsBtn.onClick = [this] { if (result.pitched || opts.mode != 0) { opts.mode = 0; reanalyse(); } };
        pitchedBtn.onClick = [this] { if (!result.pitched || opts.mode != 1) { opts.mode = 1; reanalyse(); } };
        x2.onClick = [this] { opts.bpm = juce::jlimit(20.0, 999.0, result.bpm * 2); reanalyse(); };
        half.onClick = [this] { opts.bpm = juce::jlimit(20.0, 999.0, result.bpm / 2); reanalyse(); };
        bpm.onDragEnd = [this] { if (std::abs(bpm.getValue() - result.bpm) > 0.05) { opts.bpm = bpm.getValue(); reanalyse(); } };
        bpm.onValueChange = [this] { if (!bpm.isMouseButtonDown() && std::abs(bpm.getValue() - result.bpm) > 0.05) { opts.bpm = bpm.getValue(); reanalyse(); } };   // typed / wheel
        quant.onDragEnd = [this] { apply(); }; quant.onValueChange = [this] { if (!quant.isMouseButtonDown()) apply(); };
        grid.onChange = [this] { apply(); }; useHits.onClick = [this] { apply(); };
        cancelBtn.onClick = [this] { if (job) job->cancel.store(true); };
        recreateBtn.onClick = [this] { apply(); };
        soundBtn.onClick = [this] { undoApplied(); if (useAsSound) useAsSound(file, dropTrack); setVisible(false); };
        chopBtn.onClick = [this] { undoApplied(); if (chopToSlices) chopToSlices(file, dropTrack); setVisible(false); };
        closeBtn.onClick = [this] { setVisible(false); };
        setWantsKeyboardFocus(true);
    }
    ~RecreatePanel() override { stopTimer(); stopJob(); }
    void stopJob() { if (job) { job->cancel.store(true); job->stopThread(20000); job.reset(); } }

    // a new file: analyse it (auto mode, auto tempo) and apply as soon as it's done
    void start(const juce::File& f, int track) {
        stopJob(); file = f; dropTrack = track; loop.reset(); result = {}; opts = {}; opts.hintBpm = ed.proc.hostBpm.load(); applied = false; appliedDepth = -1; roleMask = 0xFFFFFFFFu;
        info.setText(f.getFileName(), juce::dontSendNotification); setVisible(true); toFront(true); launch();
    }
    void reanalyse() { if (!loop) return; launch(); }
    void launch() {
        stopJob(); job = std::make_unique<Job>(); job->owner = this; job->f = file; job->o = opts; job->have = loop;
        busy = true; prog = 0; status.setText("Listening to the loop...", juce::dontSendNotification); sync(); job->startThread(); startTimerHz(20);
    }
public:
    void timerCallback() override {
        if (!job) { stopTimer(); return; }
        prog = job->progress.load(); repaint();
        if (!job->done.load()) return;
        stopTimer(); job->stopThread(1000); auto j = std::move(job); busy = false;
        if (j->err.isNotEmpty()) { status.setText(j->err, juce::dontSendNotification); ed.say(j->err); sync(); return; }
        if (j->cancel.load() || !j->r.ok) { status.setText("Cancelled.", juce::dontSendNotification); sync(); return; }
        loop = j->loop; result = j->r; apply();
    }
    void undoApplied() { if (applied && ed.proc.undoDepth() == appliedDepth) { ed.proc.undo(); applied = false; if (refresh) refresh(); } }
    gb::a2m::WriteOpts writeOpts() const { gb::a2m::WriteOpts w; static const int res[] { 0, 1, 2 }; w.res = res[juce::jlimit(0, 2, grid.getSelectedItemIndex())]; w.quantize = (float) quant.getValue() / 100.0f; w.useHits = useHits.getToggleState(); w.roles = roleMask; return w; }
    // apply the result: replaces the previous apply (so the whole thing stays ONE undo step)
    void apply() {
        if (busy || !loop || !result.ok) return;
        undoApplied();
        juce::String msg; ed.proc.pushUndo(); msg = ed.proc.applyRecreate(*loop, result, writeOpts()); applied = true; appliedDepth = ed.proc.undoDepth();
        juce::String extra; const double host = opts.hintBpm;
        if (host > 0 && std::abs(host - result.bpm) > 0.5) extra = "\nThe loop is " + juce::String(result.bpm, 1) + " BPM, your project " + juce::String(host, 1) + ": the pattern is in beats, so it plays at the project tempo.";
        status.setText(msg + "\nTempo from the " + result.bpmFrom + (loop->truncated ? " (only the first 2 minutes were read)" : juce::String()) + "." + extra, juce::dontSendNotification);
        ed.say(msg); if (refresh) refresh(); sync();
    }
    void sync() {
        drumsBtn.setToggleState(!result.pitched, juce::dontSendNotification); pitchedBtn.setToggleState(result.pitched, juce::dontSendNotification);
        bpm.setValue(result.bpm, juce::dontSendNotification); bpm.updateText();
        for (juce::Component* c : std::initializer_list<juce::Component*> { &drumsBtn, &pitchedBtn, &x2, &half, &bpm, &quant, &grid, &useHits, &recreateBtn }) c->setEnabled(!busy && loop != nullptr);
        quant.setEnabled(!busy && !result.pitched); grid.setEnabled(!busy && !result.pitched);
        cancelBtn.setVisible(busy); syncHeard(); repaint();
    }
    // DRUMS HEARD: the summary + a chip per drum found (on = written); each chip's tooltip names the track it goes to
    void syncHeard() {
        const bool drums = !busy && loop != nullptr && result.ok && !result.pitched;
        juce::StringArray parts; for (int r = 0; r < gb::a2m::kNumRoles; ++r) if (result.perRole[(size_t) r] > 0) parts.add(juce::String(result.perRole[(size_t) r]) + " " + juce::String(gb::a2m::roleName(r)).toLowerCase());
        heard.setText(!drums ? juce::String() : parts.isEmpty() ? juce::String("Drums heard: none.") : "Drums heard: " + parts.joinIntoString(", ") + ".", juce::dontSendNotification);
        heard.setVisible(drums);
        const auto map = gb::a2m::mapRoles(ed.proc.model.tracks, [this](const juce::String& n) { return ed.proc.categoryOf(n); });
        for (int r = 0; r < gb::a2m::kNumRoles; ++r) {
            auto& c = chips[(size_t) r]; const int n = result.perRole[(size_t) r]; const int t = map[(size_t) r];
            c.setVisible(drums && n > 0); c.setButtonText(juce::String(gb::a2m::roleName(r)) + " " + juce::String(n)); c.setToggleState((roleMask >> r) & 1u, juce::dontSendNotification);
            c.setEnabled(t >= 0 && !ed.proc.model.tracks[(size_t) t].lock);
            const auto lname = juce::String(gb::a2m::roleName(r)).toLowerCase();
            c.setTooltip(t < 0 ? "No track in this kit takes the " + lname + " hits."
                         : juce::String(n) + " " + lname + " hits, written on track " + juce::String(t + 1) + " (" + ed.proc.model.tracks[(size_t) t].sound + ")"
                           + (ed.proc.model.tracks[(size_t) t].lock ? ", which is locked." : ". Click to leave them out of the pattern, click again to put them back."));
        }
        resized();
    }
    bool keyPressed(const juce::KeyPress& k) override { if (k == juce::KeyPress::escapeKey) { setVisible(false); return true; } return false; }
    void mouseDown(const juce::MouseEvent&) override {}   // (the panel swallows clicks: the grid under it isn't touched)
    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat().reduced(1.0f);
        g.setColour(SHADOW); g.fillRoundedRectangle(r.translated(0, 3), 14);
        g.setColour(POP); g.fillRoundedRectangle(r, 14); g.setColour(PURPLE.withAlpha(0.6f)); g.drawRoundedRectangle(r, 14, 1.5f);
        if (busy) { auto b = juce::Rectangle<float>(18.0f, 92.0f, getWidth() - 36.0f - 90.0f, 10.0f); g.setColour(CARD2); g.fillRoundedRectangle(b, 5); g.setColour(PURPLE); g.fillRoundedRectangle(b.withWidth(b.getWidth() * juce::jlimit(0.02f, 1.0f, prog)), 5); }
        g.setColour(GREY); g.setFont(font(10.5f)); g.drawText("MODE", 18, 118, 60, 14, juce::Justification::left); g.drawText("TEMPO", 18, 156, 60, 14, juce::Justification::left); g.drawText("GRID", 18, 194, 60, 14, juce::Justification::left);
        if (heard.isVisible()) { g.setColour(PURPLE.withAlpha(0.35f)); g.fillRect(16.0f, 362.0f, (float) getWidth() - 32.0f, 1.0f); }
    }
    void resized() override {
        const int w = getWidth();
        title.setBounds(16, 12, w - 32, 22); info.setBounds(16, 34, w - 32, 18); status.setBounds(16, 54, w - 32, 36);
        cancelBtn.setBounds(w - 104, 86, 88, 24);
        drumsBtn.setBounds(84, 112, 90, 26); pitchedBtn.setBounds(178, 112, 90, 26);
        bpm.setBounds(84, 150, 150, 26); half.setBounds(240, 150, 40, 26); x2.setBounds(284, 150, 40, 26);
        grid.setBounds(84, 188, 110, 26); quant.setBounds(200, 188, w - 216, 26);
        useHits.setBounds(80, 222, w - 96, 24);
        recreateBtn.setBounds(16, 258, w - 32, 34);
        soundBtn.setBounds(16, 298, (w - 40) * 3 / 5, 28); chopBtn.setBounds(16 + (w - 40) * 3 / 5 + 8, 298, w - 40 - (w - 40) * 3 / 5, 28);
        closeBtn.setBounds(w - 96, 334, 80, 26);
        // DRUMS HEARD: the summary, then the chips in rows (as many as fit across)
        heard.setBounds(16, 366, w - 32, 18);
        int x = 16, y = 388; for (auto& c : chips) { if (!c.isVisible()) continue; const int cw = juce::jlimit(60, 120, (int) ghosty::textWidth(font(11.0f), c.getButtonText()) + 22);
            if (x + cw > w - 16) { x = 16; y += 28; } c.setBounds(x, y, cw, 24); x += cw + 6; }
    }
    static constexpr int kW = 470, kH = 450;
};

// while audio is dragged over the editor: which drop does what
struct DropOverlay : juce::Component {
    juce::Rectangle<int> recreate, sound; int over = -1;   // 1 = recreate zone, 0 = sound zone
    DropOverlay() { setInterceptsMouseClicks(false, false); }
    void paint(juce::Graphics& g) override {
        auto zone = [&](juce::Rectangle<int> r, bool on, const juce::String& t1, const juce::String& t2, juce::Colour c) {
            if (r.isEmpty()) return; auto f = r.toFloat().reduced(3); g.setColour(c.withAlpha(on ? 0.22f : 0.08f)); g.fillRoundedRectangle(f, 12);
            g.setColour(c.withAlpha(on ? 0.95f : 0.45f)); g.drawRoundedRectangle(f, 12, on ? 2.5f : 1.5f);
            auto box = f.withSizeKeepingCentre(juce::jmin(f.getWidth() - 12, 380.0f), 56.0f); g.setColour(CARD.withAlpha(0.92f)); g.fillRoundedRectangle(box, 10);
            g.setColour(INK); g.setFont(font(15.0f)); g.drawText(t1, box.withTrimmedBottom(26), juce::Justification::centredBottom);
            g.setColour(GREY); g.setFont(font(11.5f, false)); g.drawText(t2, box.withTrimmedTop(32), juce::Justification::centredTop);
        };
        zone(recreate, over == 1, "DROP: RE-CREATE AS MIDI", "the loop's hits / notes become an editable pattern", PURPLE);
        zone(sound, over == 0, "DROP: USE AS SOUND", "on the track under the mouse", GREEN);
    }
};

} // namespace bx
