#pragma once
// GHOST HUM - the window (build 2: the owner's pictures). Flat, near-black, soft green / teal light wisps, drifting particles and a few
// faint ghosts behind glass panels with glowing green edges. Everything is real text and readable.
//
//   top       the glowing ghost logo + GHOST HUM / GHOSTBLOCK STUDIO / the slogan, the MELODY | BEATBOX switch,
//             GHOST ANIMATION: ON | OFF, BYPASS, ? (the key commands) (+ UNLOCK when the store is set)
//   middle    TAP TO RECORD: a thick neon ring with an electric crackle (teal with concentric rings in MELODY); while recording the ring
//             fills and says TAP TO STOP; a count-in shows 4 3 2 1; then "GHOST IS LISTENING..."; the status line under it
//             TEMPO FREE | BPM (BPM number to drag, TAP, HOST when a DAW plays, CLICK, COUNT-IN), INPUT AUTO | TRACK | MAC MIC (build 3:
//             where Ghost listens; AUTO falls back to the Mac's mic when the track has no sound, so FL's channel rack works), PITCH
//             ORIGINAL | NEAREST NOTE | SNAP TO KEY + the key (melody), the FREE offers after a take: [KEEP ORIGINAL] [94 BPM] [188 BPM]
//   roll      YOUR MELODY (FL-style piano roll) / YOUR BEAT (FL-style step sequencer) - HumRoll.h; its header carries the next hint
//   bottom    PLAY  CLEAN  QUANTIZE OFF 1/4 1/8 1/16 1/32  STRENGTH  SWING (beats)  ON GRID | ORIGINAL  /  UNDO  REDO  SOUND  DRAG MIDI
//             DRAG WAV  TEACH GHOST YOUR SOUNDS (beats)  NEW         footer: GHOST HUM · VERSION 1.0 · BUILD 3
//   Keys      Space play / stop, R record, Q quantize, G ghost animation, Cmd+Z / Shift+Cmd+Z, Cmd+A, Delete, arrows (Shift = octave /
//             beat), Cmd+D, Cmd+C / Cmd+V, T tap tempo, ? the list, Esc closes a panel. Drag-box selection, Alt-drag copies.
#include "HumProcessor.h"
#include "HumLook.h"
#include "HumRoll.h"
#include "Bypass.h"
#include <juce_gui_extra/juce_gui_extra.h>

#ifndef GHOSTHUM_VERSION
 #define GHOSTHUM_VERSION "1.0"
#endif
#ifndef GHOSTHUM_BUILD
 #define GHOSTHUM_BUILD "3"
#endif

namespace hum::ui {

// BYPASS in the glass look (the shared button's behaviour, this window's paint)
struct GlassBypass : ghosty::BypassButton {
    using ghosty::BypassButton::BypassButton;
    void paintButton(juce::Graphics& g, bool over, bool down) override {
        const auto r = getLocalBounds().toFloat().reduced(1.0f); const bool on = getToggleState(); const auto amber = juce::Colour(0xffffb020);
        glassPill(g, r, on, over, isEnabled(), on ? amber : look::green); if (down) { g.setColour(look::ink.withAlpha(0.1f)); g.fillRoundedRectangle(r, r.getHeight() * 0.5f); }
        g.setFont(look::label(11.0f)); if (on) { g.setColour(juce::Colour(0xff201400)); g.drawText("BYPASSED", getLocalBounds(), juce::Justification::centred); } else glowText(g, "BYPASS", getLocalBounds(), juce::Justification::centred, look::ink, look::green, 0.5f);
    }
};

// ---------------------------------------------------------------- TEACH GHOST YOUR SOUNDS: a glowing glass card with light rays behind the RECORD button
struct TeachPanel : juce::Component {
    HumProcessor& proc; std::function<void()> onClose; int step = 0;   // 0 kick, 1 snare, 2 hat, 3 open hat, 4 clap, 5 done
    Pill rec { "RECORD" }, next { "SKIP" }, done { "DONE" }, forget { "FORGET WHAT I TAUGHT" }; juce::String msg; float phase = 0;
    static int drumOfStep(int s) { static const int d[] { Kick, Snare, HatClosed, HatOpen, Clap }; return s >= 0 && s < 5 ? d[s] : -1; }
    explicit TeachPanel(HumProcessor& p) : proc(p) {
        for (auto* b : { &rec, &next, &done, &forget }) addAndMakeVisible(b); rec.on = true; rec.fontH = 14.0f; rec.bloom = 1.0f;
        rec.setTooltip("Record this sound three times (R)"); next.setTooltip("Skip this sound"); done.setTooltip("Close (Esc)"); forget.setTooltip("Go back to the built-in ears");
        rec.onClick = [this] { if (proc.isRecording()) { proc.stopRecording(); return; } proc.teachingDrum = drumOfStep(step); proc.startRecording(); rec.setButtonText("TAP WHEN DONE"); repaint(); };
        next.onClick = [this] { advance(); }; done.onClick = [this] { if (onClose) onClose(); };
        forget.onClick = [this] { proc.forgetTaught(); msg = "Forgot them. Ghost uses its built-in ears again."; repaint(); };
        proc.onTaught = [this](int d, int got) { rec.setButtonText("RECORD"); msg = got > 0 ? juce::String("Got your ") + drumName(d) + ". " : juce::String("Ghost heard no ") + drumName(d) + ". Try again. "; if (got > 0) advance(); else repaint(); };
    }
    ~TeachPanel() override { proc.onTaught = nullptr; }
    void advance() { step = juce::jmin(5, step + 1); if (step == 5 && proc.mode() == Beatbox) proc.retranscribe(); rec.setButtonText("RECORD"); repaint(); }
    void resized() override { auto r = getLocalBounds().reduced(26); auto b = r.removeFromBottom(36); done.setBounds(b.removeFromRight(110)); forget.setBounds(b.removeFromLeft(220)); auto m = r.withSizeKeepingCentre(320, 60).translated(0, 36); rec.setBounds(m.removeFromLeft(210)); m.removeFromLeft(12); next.setBounds(m.withSizeKeepingCentre(m.getWidth(), 38)); }
    void paint(juce::Graphics& g) override {
        const auto r = getLocalBounds().toFloat().reduced(2.0f);
        g.setColour(look::bg.withAlpha(0.55f)); g.fillRoundedRectangle(r, 16.0f);
        // light rays behind the RECORD button
        { const auto c = rec.getBounds().toFloat().getCentre(); juce::Random rr(3); g.saveState(); g.reduceClipRegion(getLocalBounds().reduced(3));
          for (int i = 0; i < 18; ++i) { const float a = rr.nextFloat() * juce::MathConstants<float>::twoPi + phase * 0.05f, w = 0.03f + rr.nextFloat() * 0.06f, len = 420.0f + rr.nextFloat() * 300.0f;
              juce::Path p; p.startNewSubPath(c); p.lineTo(c.getPointOnCircumference(len, a - w)); p.lineTo(c.getPointOnCircumference(len, a + w)); p.closeSubPath();
              juce::ColourGradient gr(look::green.withAlpha(0.16f), c.x, c.y, look::green.withAlpha(0.0f), c.getPointOnCircumference(len, a).x, c.getPointOnCircumference(len, a).y, true); g.setGradientFill(gr); g.fillPath(p); }
          g.restoreState(); }
        glassPanel(g, r, 16.0f, look::green, 1.4f, 0.80f);
        g.setFont(look::label(11.0f)); glowText(g, "TEACH GHOST YOUR SOUNDS", getLocalBounds().withTrimmedTop(22).withHeight(18), juce::Justification::centredTop, look::green, look::greenGlow, 1.0f);
        const int d = drumOfStep(step); g.setFont(look::font(24.0f, true));
        const juce::String what = step >= 5 ? "All done. Ghost knows your sounds now." : juce::String("Make your ") + drumName(d) + " 3 times" + (d == Clap ? " (or SKIP)" : "");
        glowText(g, what, getLocalBounds().withTrimmedTop(46).withHeight(34), juce::Justification::centredTop, look::ink, look::greenGlow, 1.2f);
        g.setColour(look::dim); g.setFont(look::font(12.0f));
        g.drawText(step >= 5 ? "Your sounds are saved on this computer and used every time." : "Tap RECORD, make the sound three times with a little space between, tap again.", getLocalBounds().withTrimmedTop(84).withHeight(20), juce::Justification::centredTop);
        juce::String have; for (int k = 0; k < kDrums; ++k) { const int n = proc.cal.countFor(k); if (n > 0) have << drumName(k) << " " << n << "   "; }
        g.setColour(look::faint); g.drawText(have.isEmpty() ? "Nothing taught yet (the built-in ears work without this)." : "Taught: " + have.trim(), getLocalBounds().withTrimmedTop(104).withHeight(18), juce::Justification::centredTop);
        g.setColour(look::green.withAlpha(0.95f)); g.drawText(msg, getLocalBounds().withTrimmedTop(getHeight() / 2 + 62).withHeight(20), juce::Justification::centredTop);
        rec.setVisible(step < 5); next.setVisible(step < 5); next.setButtonText("SKIP");
    }
};

// a dark veil over the window behind a card (click closes the help card)
struct Scrim : juce::Component { std::function<void()> onClick; void paint(juce::Graphics& g) override { g.fillAll(look::bg.withAlpha(0.62f)); } void mouseDown(const juce::MouseEvent&) override { if (onClick) onClick(); } };

// ---------------------------------------------------------------- "?": every key command
struct HelpPanel : juce::Component {
    std::function<void()> onClose;
    static const std::vector<std::pair<const char*, const char*>>& rows() { static const std::vector<std::pair<const char*, const char*>> r {
        { "Space", "Play / stop" }, { "R", "Record (tap again to stop)" }, { "Q", "Quantize: next grid (OFF, 1/4, 1/8, 1/16, 1/32)" }, { "G", "Ghost animation on / off" }, { "T", "Tap tempo (BPM mode)" },
        { "Cmd+Z / Shift+Cmd+Z", "Undo / redo" }, { "Cmd+A", "Select all" }, { "Delete / Backspace", "Delete the selection" }, { "Arrows", "Move the selection: a step sideways, a semitone / lane up or down" },
        { "Shift+Arrows", "A bigger step: a beat sideways, an octave up or down" }, { "Cmd+D", "Duplicate the selection (or the whole take) after itself" }, { "Cmd+C / Cmd+V", "Copy / paste (paste lands where you last clicked)" },
        { "Drag an empty spot", "Box selection (Shift / Cmd adds)" }, { "Alt-drag", "Drag a copy" }, { "Wheel over a note / pad", "Louder / softer" }, { "Double-click (melody)", "Add a note" }, { "Click a pad (beat)", "Add or remove a hit" },
        { "Right-click", "Which drum, soft / medium / hard, octave, delete" }, { "Esc", "Close this" } }; return r; }
    void paint(juce::Graphics& g) override {
        const auto r = getLocalBounds().toFloat().reduced(2.0f); glassPanel(g, r, 16.0f, look::green, 1.2f, 0.82f);
        g.setFont(look::label(11.0f)); glowText(g, "KEY COMMANDS", getLocalBounds().withTrimmedTop(18).withHeight(18), juce::Justification::centredTop, look::green, look::greenGlow, 1.0f);
        auto b = getLocalBounds().reduced(26, 0).withTrimmedTop(46).withTrimmedBottom(16); const int n = (int) rows().size(); const int rh = juce::jmax(14, juce::jmin(22, b.getHeight() / n));
        for (auto& [k, what] : rows()) { auto row = b.removeFromTop(rh); g.setColour(look::green); g.setFont(look::font(12.0f, true)); g.drawText(k, row.removeFromLeft(190), juce::Justification::centredLeft); g.setColour(look::ink); g.setFont(look::font(12.0f)); g.drawFittedText(what, row, juce::Justification::centredLeft, 1); }
    }
    void mouseDown(const juce::MouseEvent&) override { if (onClose) onClose(); }
};

} // namespace hum::ui

namespace hum {

class HumEditor : public juce::AudioProcessorEditor, public juce::DragAndDropContainer, private juce::Timer {
public:
    explicit HumEditor(HumProcessor& p) : juce::AudioProcessorEditor(p), proc(p), roll(p) {
        using namespace ui;
        addAndMakeVisible(modeSwitch); modeSwitch.set(proc.mode()); modeSwitch.onChange = [this](int m) { proc.setParam("mode", (float) m); roll.clearSelection(); refresh(); }; modeSwitch.fontH = 12.0f; modeSwitch.setTooltip("What the next recording is. Each mode keeps its own take.");
        addAndMakeVisible(record); record.onTap = [this] { tapRecord(); }; record.state = [this] { RecordButton::State s; s.melody = proc.mode() == Melody; s.recording = proc.isRecording(); s.busy = proc.isAnalysing(); s.countingIn = proc.isCountingIn(); s.countLeft = proc.countInLeft(); s.level = proc.inputLevel(); s.seconds = proc.recordedSeconds(); s.maxSeconds = HumProcessor::kMaxRecordSeconds; s.teach = proc.teachingDrum >= 0; return s; };
        record.setTooltip("Tap, then sing / hum or beatbox. Tap again when you are done (R). It stops by itself after 90 seconds.");
        addAndMakeVisible(tempoSwitch); tempoSwitch.set(proc.tempoFree() ? 0 : 1); tempoSwitch.onChange = [this](int i) { proc.setParam("tempomode", (float) i); refresh(); }; tempoSwitch.setTooltip("FREE: record without a click, Ghost guesses the tempo afterwards. BPM: set the tempo first, with a click and a count-in.");
        addAndMakeVisible(bpm); bpm.name = "BPM"; bpm.lo = 40; bpm.hi = 240; bpm.def = 120; bpm.step = 1; bpm.val = proc.setBpm(); bpm.format = [](double v) { return juce::String((int) v); }; bpm.setTooltip("The tempo: drag up / down, wheel, double-click = 120");
        bpm.onChange = [this](double v) { proc.setParam("bpm", (float) v); };
        addAndMakeVisible(tap); tap.onClick = [this] { tapTempo(); }; tap.setTooltip("Tap the beat here (or T) and the BPM follows");
        addChildComponent(host); host.onClick = [this] { if (proc.hostBpm() > 0) { proc.setParam("bpm", (float) proc.hostBpm()); refresh(); } }; host.setTooltip("Your DAW's tempo: tap to use it");
        addAndMakeVisible(metro); metro.onClick = [this] { proc.setParam("metro", metro.on ? 0.0f : 1.0f); refresh(); }; metro.setTooltip("A metronome while you record. Ghost knows when it clicked and keeps the clicks out of your take.");
        addAndMakeVisible(countIn); countIn.onClick = [this] { proc.setParam("countin", countIn.on ? 0.0f : 1.0f); refresh(); }; countIn.setTooltip("4 3 2 1 before the recording starts (BPM mode)");
        addAndMakeVisible(inputSwitch); inputSwitch.set(proc.inputChoice()); inputSwitch.onChange = [this](int i) { proc.setParam("input", (float) i); refresh(); };
        inputSwitch.setTooltip("Where Ghost listens. AUTO: this track when sound comes into it, otherwise your Mac's microphone (so it works in FL's channel rack too). TRACK: only the audio coming into this track (in the app: the input chosen in Options). MAC MIC: always your Mac's microphone.");
        addAndMakeVisible(pitchSwitch); pitchSwitch.set(proc.pitchMode()); pitchSwitch.onChange = [this](int i) { proc.setParam("pitch", (float) i); refresh(); }; pitchSwitch.accent = look::teal; pitchSwitch.setTooltip("ORIGINAL: exactly the pitch you sang (bends). NEAREST NOTE: rounded to the semitone. SNAP TO KEY: moved onto the key.");
        addAndMakeVisible(key); key.name = "KEY"; { juce::StringArray ks { "DETECTED" }; for (int sc = 0; sc < 2; ++sc) for (int k = 0; k < 12; ++k) ks.add(juce::String(keyName(k)) + (sc ? " MINOR" : " MAJOR")); key.items = ks; } key.sel = proc.keyChoice() + 1; key.setTooltip("The key to snap to (DETECTED = the one Ghost heard)");
        key.onChange = [this](int i) { proc.setParam("key", (float) i); refresh(); };
        for (auto& o : offers) { addChildComponent(o); o.setTooltip("Hear the take at this tempo (ON GRID snaps it to 1/16)"); }
        addAndMakeVisible(roll); roll.onEdit = [this] { refresh(); }; roll.onKey = [this](const juce::KeyPress& k) { return command(k); };
        roll.setTooltip("Drag notes / hits to move them, drag an empty spot for a box selection, Alt-drag copies, wheel = velocity, right-click = menu. ? lists the keys.");
        addAndMakeVisible(play); play.onClick = [this] { if (proc.isPlaying()) proc.stop(); else proc.play(); refresh(); }; play.setTooltip("Play / stop (Space). Sends the notes to your DAW as MIDI while it plays.");
        addAndMakeVisible(clean); clean.onClick = [this] { proc.clean(); refresh(); }; clean.setTooltip("Tidy the take: blips, doubles, shaky notes, velocities. One UNDO away.");
        addAndMakeVisible(quantSwitch); quantSwitch.set(proc.quant()); quantSwitch.onChange = [this](int i) { proc.setParam("quant", (float) i); refresh(); }; quantSwitch.setTooltip("The grid your notes snap to (Q steps through). OFF keeps your exact timing.");
        addAndMakeVisible(strength); strength.name = "STRENGTH"; strength.lo = 0; strength.hi = 100; strength.def = 100; strength.step = 5; strength.unit = "%"; strength.val = proc.quantStrength() * 100; strength.onChange = [this](double v) { proc.setParam("qstrength", (float) v); }; strength.setTooltip("How far notes move to the grid: 100% all the way, 50% half way");
        addAndMakeVisible(swing); swing.name = "SWING"; swing.lo = 50; swing.hi = 75; swing.def = 50; swing.step = 1; swing.unit = "%"; swing.val = proc.swing(); swing.onChange = [this](double v) { proc.setParam("swing", (float) v); }; swing.setTooltip("Delays the off-beats (50% straight)");
        addAndMakeVisible(sourceSwitch); sourceSwitch.set(proc.playOriginal() ? 1 : 0); sourceSwitch.onChange = [this](int i) { proc.setParam("source", (float) i); refresh(); }; sourceSwitch.setTooltip("ON GRID: your notes on the grid (QUANTIZE / SWING applied). ORIGINAL: exactly where you performed them.");
        addAndMakeVisible(undoB); undoB.onClick = [this] { proc.doUndo(); roll.clearSelection(); refresh(); }; undoB.setTooltip("Undo (Cmd+Z)");
        addAndMakeVisible(redoB); redoB.onClick = [this] { proc.doRedo(); roll.clearSelection(); refresh(); }; redoB.setTooltip("Redo (Shift+Cmd+Z)");
        addAndMakeVisible(sound); sound.name = "SOUND"; for (int v = 0; v < sound::kVoices; ++v) sound.items.add(sound::voiceName(v)); sound.sel = proc.voice(); sound.onChange = [this](int i) { proc.setParam("sound", (float) i); }; sound.setTooltip("What the melody plays on");
        addAndMakeVisible(dragMidi); dragMidi.onClick = [this] { dragOut(true); }; dragMidi.setTooltip("Drag a MIDI file of the take into your DAW (also saved in Documents/Ghosty/Ghost Hum/MIDI)");
        addAndMakeVisible(dragWav); dragWav.onClick = [this] { dragOut(false); }; dragWav.setTooltip("Drag a WAV of what you hear into your DAW (also saved in Documents/Ghosty/Ghost Hum/Audio)");
        addAndMakeVisible(teach); teach.onClick = [this] { openTeach(); }; teach.setTooltip("Record your own kick / snare / hi-hat / open hat / clap, so Ghost sorts your hits better");
        addAndMakeVisible(clearB); clearB.onClick = [this] { proc.clearTake(); roll.clearSelection(); refresh(); }; clearB.setTooltip("Clear the take (undoable)");
        addAndMakeVisible(anim); anim.onClick = [this] { proc.setGhostAnim(!proc.ghostAnim); refresh(); }; anim.fontH = 9.5f; anim.setTooltip("The ghosts, wisps, particles, ring crackle and logo pulse (G). OFF stops every animation and its timer.");
        addAndMakeVisible(help); help.onClick = [this] { openHelp(); }; help.fontH = 14.0f; help.setTooltip("Every key command");
        addAndMakeVisible(bypassButton); addChildComponent(scrim);
        addChildComponent(unlock); unlock.refresh(); unlock.onChange = [this] { refresh(); };
        proc.onChanged = [this] { refresh(); };
        setWantsKeyboardFocus(true); setResizable(true, true); setResizeLimits(760, 600, 2200, 1500); setSize(960, 660); refresh(); applyAnimation();
    }
    ~HumEditor() override { proc.onChanged = nullptr; stopTimer(); proc.micIdle(); }   // (the window closed: a mic Ghost opened itself goes back to the system, unless a take is running)

    // ---- two timers. The ANIMATION timer (24 fps) runs only while GHOST ANIMATION is on and the window shows; OFF stops it completely
    // and the last frame stands. The POLL (10 Hz) only looks for changes from the processor (a take finished on the background thread,
    // the 90 s stop, the recording clock) and repaints what moved.
    void applyAnimation() { const bool on = proc.ghostAnim; record.animate = on; if (on && isShowing()) { if (!isTimerRunning()) startTimerHz(24); } else stopTimer(); repaint(); }
    void timerCallback() override {
        const bool showing = isShowing() && !(getPeer() != nullptr && getPeer()->isMinimised());
        if (!proc.ghostAnim) { stopTimer(); return; }
        if (!showing) return;   // hidden: nothing is drawn (the timer idles; visibilityChanged restarts the frames)
        const float dt = 1.0f / 24.0f; phase += dt; backdrop.step(dt); record.advance(dt); if (teachPanel) { teachPanel->phase += dt; teachPanel->repaint(); }
        repaint();
    }
    void pollNow() {
        if (proc.uiCounter.load() != seenCounter) { seenCounter = proc.uiCounter.load(); refresh(); }
        else if (proc.isRecording() || proc.isAnalysing() || proc.isCountingIn()) { repaint(statusArea); if (!proc.ghostAnim) record.repaint(); roll.hint = nextHint(); roll.repaint(); }
        if ((proc.hostBpm() > 0) != host.isVisible() && !proc.tempoFree()) resized();
        { const auto note = inputNote(); if (note != lastInputNote) { lastInputNote = note; repaint(inputNoteArea); } }   // (AUTO's note follows the sound)
    }
    struct Poll : juce::Timer { HumEditor& e; explicit Poll(HumEditor& x) : e(x) { startTimerHz(10); } void timerCallback() override { e.pollNow(); } };
    void visibilityChanged() override { applyAnimation(); }
    void parentHierarchyChanged() override { applyAnimation(); }
    void paint(juce::Graphics& g) override {
        using namespace ui; backdrop.paint(g, getLocalBounds(), phase);
        // header: the glowing ghost, GHOST HUM, GHOSTBLOCK STUDIO, the slogan
        drawLogoGhost(g, juce::Rectangle<float>(10, 4, 64, 64), phase, proc.ghostAnim);
        g.setFont(look::techno(19.0f)); glowText(g, "GHOST HUM", juce::Rectangle<int>(80, 10, 260, 24), juce::Justification::centredLeft, look::ink, look::greenGlow, 1.1f);
        g.setFont(look::label(9.0f)); glowText(g, "GHOSTBLOCK STUDIO", juce::Rectangle<int>(81, 33, 220, 14), juce::Justification::centredLeft, look::dim, look::green, 0.5f);
        g.setColour(look::faint.brighter(0.2f)); g.setFont(look::font(10.5f)); g.drawText(tagline, 81, 47, 300, 14, juce::Justification::centredLeft);
        // the status line under the big button
        g.setColour(look::dim); g.setFont(look::font(12.0f)); g.drawFittedText(statusText(), statusArea, juce::Justification::centred, 2);
        // the settings panel (glass) + section labels
        glassPanel(g, settingsArea.toFloat(), 14.0f, look::accent(proc.mode() == Melody), 0.7f, 0.5f);
        g.setFont(look::small(9.5f)); glowText(g, "TEMPO", tempoLabel, juce::Justification::centredLeft, look::dim, look::green, 0.4f);
        glowText(g, "INPUT", inputLabel, juce::Justification::centredLeft, look::dim, look::green, 0.4f);
        { g.setColour(look::faint.brighter(0.2f)); g.setFont(look::font(10.5f)); g.drawFittedText(inputNote(), inputNoteArea, juce::Justification::centredLeft, 1); }   // what AUTO is doing right now
        if (proc.mode() == Melody) glowText(g, "PITCH", pitchLabel, juce::Justification::centredLeft, look::dim, look::teal, 0.4f);
        if (!offerArea.isEmpty()) { g.setColour(look::dim); g.setFont(look::font(11.0f)); g.drawFittedText("Ghost guessed the tempo.\nPick what sounds right:", offerArea, juce::Justification::centredLeft, 2); }
        if (proc.mode() == Beatbox && offerArea.isEmpty() && !tipArea.isEmpty()) { g.setColour(look::dim); g.setFont(look::font(11.0f));
            g.drawFittedText("Kick = \"boom\" or \"b\", snare = \"pah\" / \"ka\", hi-hat = \"ts\", open hat = \"tss\", clap = a sharp \"k\" or a real clap.\nGhost sorts each hit into its lane. A faint pad means it was not sure: drag it where it belongs, or TEACH GHOST YOUR SOUNDS.", tipArea, juce::Justification::topLeft, 4); }
        // the bottom bar (glass)
        glassPanel(g, barArea.toFloat(), 14.0f, look::green, 0.7f, 0.5f);
        g.setFont(look::small(9.5f)); glowText(g, "QUANTIZE", quantLabel, juce::Justification::centredLeft, look::dim, look::green, 0.4f);
        // footer
        g.setFont(look::label(9.0f)); glowText(g, juce::String::fromUTF8("GHOST HUM  \xc2\xb7  VERSION ") + GHOSTHUM_VERSION + juce::String::fromUTF8("  \xc2\xb7  BUILD ") + GHOSTHUM_BUILD, getLocalBounds().removeFromBottom(18).withTrimmedBottom(4), juce::Justification::centred, look::green.withAlpha(0.8f), look::green, 0.6f);
    }
    juce::String statusText() const {
        if (proc.isCountingIn()) return "Count-in... the take starts on the next downbeat.";
        if (proc.isRecording()) return proc.status.isNotEmpty() ? proc.status : proc.listeningText();   // (the processor says where it listens, and "No sound is coming in" after 2 s)
        if (proc.isAnalysing()) return "Ghost is listening to what you did...";
        if (proc.status.isNotEmpty()) return proc.status;
        return proc.mode() == Melody ? "Sing your melody. Ghost will listen." : "Make your beat. Ghost sorts the hits.";
    }
    // a few words next to INPUT: which source AUTO is on (or will take)
    juce::String inputNote() const {
        const bool app = juce::JUCEApplicationBase::isStandaloneApp(); const juce::String track = app ? "the app's input" : "this track";
        if (proc.isRecording() || proc.isCountingIn()) { const int s = proc.recordingSource(); return s == SrcMic ? "now: your Mac's mic" : s == SrcHost ? "now: " + track : "now: waiting for sound"; }
        if (proc.inputChoice() != InputAuto) return {};
        return proc.hostHasSound() ? "sound on " + track : (proc.getTotalNumInputChannels() == 0 ? "no track audio: your Mac's mic" : "no sound on " + track + ": your Mac's mic");
    }
    // the next step, shown in the roll's header
    juce::String nextHint() const {
        const auto& t = proc.take(); const bool mel = proc.mode() == Melody;
        if (proc.isRecording()) return mel ? "Sing... tap the big button when you're done." : "Beatbox... tap the big button when you're done.";
        if (t.empty()) return mel ? "Tap the big button and sing. Your notes will show up here." : "Tap the big button and beatbox. Your hits will show up here.";
        if (!t.bpmKnown && !t.bpmOffers.empty() && proc.tempoFree()) return "Next: pick a tempo above (or KEEP ORIGINAL), PLAY to check, then DRAG MIDI into your DAW.";
        return mel ? "Next: PLAY to hear it, drag a note to fix it (CLEAN tidies), then DRAG MIDI into your DAW." : "Next: PLAY to hear it, drag a hit to fix it (faint = Ghost is not sure), then DRAG MIDI into your DAW.";
    }
    void resized() override {
        using namespace ui; auto r = getLocalBounds(); const int W = r.getWidth(); const bool mel = proc.mode() == Melody;
        // header: the right cluster first (GHOST ANIMATION, ?, BYPASS, UNLOCK), then the mode switch centred in what is left of the line;
        // when the window is narrow the switch moves down to the top of the settings panel
        int x = W - 12; bypassButton.setBounds(x - 84, 18, 84, 28); x -= 92; help.setBounds(x - 30, 18, 30, 28); x -= 38; anim.setBounds(x - 150, 18, 150, 28); x -= 158; unlock.setBounds(x - 86, 18, 86, 28); if (unlock.isVisible()) x -= 94;
        const int logoRight = 340, avail = x - 8 - logoRight; const bool modeInPanel = avail < 170; if (!modeInPanel) modeSwitch.setBounds(juce::Rectangle<int>(logoRight + (avail - juce::jmin(240, avail)) / 2, 18, juce::jmin(240, avail), 34));
        r.removeFromTop(74); r.removeFromBottom(18);
        // the bottom bar: two rows (three when narrow) in a glass panel
        const int rows = W < 940 ? 3 : 2; barArea = r.removeFromBottom(rows * 36 + 8 * (rows - 1) + 16).reduced(12, 0); auto bar = barArea.reduced(10, 8);
        auto row1 = bar.removeFromTop(36); bar.removeFromTop(8); auto row2 = rows == 3 ? bar.removeFromTop(36) : row1; if (rows == 3) bar.removeFromTop(8); auto row3 = bar.removeFromTop(36);
        play.setBounds(row1.removeFromLeft(84)); row1.removeFromLeft(8); clean.setBounds(row1.removeFromLeft(74)); row1.removeFromLeft(12);
        quantLabel = row1.removeFromLeft(66); quantSwitch.setBounds(row1.removeFromLeft(juce::jmin(260, juce::jmax(200, W / 4)))); row1.removeFromLeft(8);
        if (rows == 2) row2 = row1;
        strength.setBounds(row2.removeFromLeft(118)); row2.removeFromLeft(8);
        swing.setBounds(row2.removeFromLeft(mel ? 0 : 104)); swing.setVisible(!mel); if (!mel) row2.removeFromLeft(8);
        sourceSwitch.setBounds(row2.removeFromLeft(juce::jmin(200, juce::jmax(166, row2.getWidth()))));
        undoB.setBounds(row3.removeFromLeft(74)); row3.removeFromLeft(6); redoB.setBounds(row3.removeFromLeft(74)); row3.removeFromLeft(12);
        sound.setVisible(mel); if (mel) { sound.setBounds(row3.removeFromLeft(juce::jmin(170, juce::jmax(120, row3.getWidth() / 4)))); row3.removeFromLeft(12); }
        dragMidi.setBounds(row3.removeFromLeft(112)); row3.removeFromLeft(8); dragWav.setBounds(row3.removeFromLeft(112)); row3.removeFromLeft(12);
        clearB.setBounds(row3.removeFromRight(76)); row3.removeFromRight(8);
        teach.setVisible(!mel); if (!mel) { teach.setBounds(row3.removeFromRight(juce::jmin(230, row3.getWidth()))); row3.removeFromRight(8); }
        r.removeFromBottom(10);
        // top half: the big button on the left, the settings panel on the right; the roll below
        // (the panel needs its rows: TEMPO, INPUT, PITCH / the beatbox tip, the offers; the roll takes what is left)
        const int need = 20 + (modeInPanel ? 40 : 0) + 4 * 32 + 3 * 8;
        auto top = r.removeFromTop(juce::jlimit(need, 320, r.getHeight() * 46 / 100)).reduced(12, 0);
        auto left = top.removeFromLeft(juce::jmax(190, W * 34 / 100)); statusArea = left.removeFromBottom(30); const int btn = juce::jmin(left.getHeight() + 10, 260);
        record.setBounds(left.withSizeKeepingCentre(btn, btn));
        top.removeFromLeft(8); settingsArea = top; auto s = top.reduced(12, 10);
        if (modeInPanel) { auto l0 = s.removeFromTop(34); modeSwitch.setBounds(l0.withSizeKeepingCentre(juce::jmin(240, l0.getWidth()), 34)); s.removeFromTop(6); }
        auto l1 = s.removeFromTop(32); tempoLabel = l1.removeFromLeft(52); tempoSwitch.setBounds(l1.removeFromLeft(140)); l1.removeFromLeft(8);
        if (!proc.tempoFree()) { bpm.setBounds(l1.removeFromLeft(100)); l1.removeFromLeft(6); tap.setBounds(l1.removeFromLeft(52)); l1.removeFromLeft(6);
            if (proc.hostBpm() > 0 && l1.getWidth() > 260) { host.setBounds(l1.removeFromLeft(84)); l1.removeFromLeft(6); host.setVisible(true); } else host.setVisible(false);
            metro.setBounds(l1.removeFromLeft(juce::jmin(84, juce::jmax(60, l1.getWidth() / 2 - 4)))); l1.removeFromLeft(6); countIn.setBounds(l1.removeFromLeft(juce::jmin(110, l1.getWidth()))); }
        else { host.setVisible(false); }
        // INPUT (build 3): AUTO | TRACK | MAC MIC, and a few words on what AUTO is doing
        s.removeFromTop(8); auto li = s.removeFromTop(32); inputLabel = li.removeFromLeft(52); inputSwitch.setBounds(li.removeFromLeft(juce::jmin(236, li.getWidth()))); li.removeFromLeft(10); inputNoteArea = li;
        s.removeFromTop(8); auto l2 = s.removeFromTop(32); pitchLabel = l2.removeFromLeft(52);
        if (mel) { pitchSwitch.setBounds(l2.removeFromLeft(juce::jmin(320, l2.getWidth() - 150))); l2.removeFromLeft(8); key.setBounds(l2.removeFromLeft(juce::jmin(150, l2.getWidth()))); } else { pitchSwitch.setBounds({}); key.setBounds({}); }
        pitchSwitch.setVisible(mel); key.setVisible(mel && proc.pitchMode() == PitchSnapKey);
        s.removeFromTop(8); offerRow = s.removeFromTop(32); layoutOffers(); tipArea = mel ? juce::Rectangle<int>() : juce::Rectangle<int>(pitchLabel.getX(), pitchLabel.getY(), settingsArea.getRight() - 16 - pitchLabel.getX(), settingsArea.getBottom() - 10 - pitchLabel.getY());
        r.removeFromTop(8); roll.setBounds(r.reduced(12, 0));
        scrim.setBounds(getLocalBounds()); scrim.setVisible(teachPanel != nullptr || helpPanel != nullptr);
        if (teachPanel) teachPanel->setBounds(getLocalBounds().withSizeKeepingCentre(juce::jmin(580, W - 40), 300));
        if (helpPanel) helpPanel->setBounds(getLocalBounds().withSizeKeepingCentre(juce::jmin(620, W - 40), juce::jmin(520, getHeight() - 40)));
    }
    void layoutOffers() {
        const auto& t = proc.take(); const bool show = !t.empty() && !t.bpmKnown && !t.bpmOffers.empty() && proc.tempoFree();
        offerArea = {}; for (auto& o : offers) o.setVisible(false);
        if (!show) return; auto r = offerRow; offerArea = r.removeFromLeft(juce::jmin(190, r.getWidth() / 3)); r.removeFromLeft(8);
        offers[0].setButtonText("KEEP ORIGINAL"); offers[0].on = !t.bpmKnown; offers[0].setBounds(r.removeFromLeft(118)); offers[0].setVisible(true); r.removeFromLeft(6);
        offers[0].onClick = [this] { proc.chooseBpm(proc.take().bpm, true); refresh(); };
        for (size_t i = 0; i < t.bpmOffers.size() && i < 3; ++i) { auto& o = offers[i + 1]; const double b = t.bpmOffers[i]; o.setButtonText(juce::String(juce::roundToInt(b)) + " BPM"); o.on = false; o.setBounds(r.removeFromLeft(juce::jmin(86, juce::jmax(60, r.getWidth() / (int) (t.bpmOffers.size() - i) - 6)))); o.setVisible(true); r.removeFromLeft(6);
            o.onClick = [this, b] { proc.chooseBpm(b, false); refresh(); }; }
    }
    void refresh() {
        using namespace ui; const auto& t = proc.take(); const bool mel = proc.mode() == Melody;
        modeSwitch.set(proc.mode()); tempoSwitch.set(proc.tempoFree() ? 0 : 1); pitchSwitch.set(proc.pitchMode()); inputSwitch.set(proc.inputChoice()); key.sel = proc.keyChoice() + 1; quantSwitch.set(proc.quant()); sourceSwitch.set(proc.playOriginal() ? 1 : 0);
        bpm.set(proc.setBpm(), false); strength.set(proc.quantStrength() * 100, false); swing.set(proc.swing(), false); sound.sel = proc.voice();
        bpm.setVisible(!proc.tempoFree()); tap.setVisible(!proc.tempoFree()); metro.setVisible(!proc.tempoFree()); countIn.setVisible(!proc.tempoFree()); metro.on = proc.metronome(); metro.setButtonText(metro.on ? "CLICK ON" : "CLICK OFF"); countIn.on = proc.countIn(); countIn.setButtonText(countIn.on ? "COUNT-IN ON" : "COUNT-IN OFF");
        host.setButtonText("HOST " + juce::String(juce::roundToInt(proc.hostBpm())));
        key.setVisible(mel && proc.pitchMode() == PitchSnapKey); if (key.sel == 0 && t.key >= 0) key.name = "KEY (" + keyText(t).toUpperCase() + ")"; else key.name = "KEY";
        play.setButtonText(proc.isPlaying() ? "STOP" : "PLAY"); play.on = proc.isPlaying();
        const bool have = !t.empty(); for (auto* b : { &play, &clean, &dragMidi, &dragWav, &clearB }) b->setEnabled(have);   // (the export buttons wait for a take)
        undoB.setEnabled(proc.undo().canUndo()); redoB.setEnabled(proc.undo().canRedo()); strength.setEnabled(proc.quant() != QOff);
        quantSwitch.enabledItem.assign((size_t) quantSwitch.items.size(), true);
        anim.on = proc.ghostAnim; anim.setButtonText(proc.ghostAnim ? "GHOST ANIMATION: ON" : "GHOST ANIMATION: OFF"); anim.dimmed = !proc.ghostAnim;
        tagline = mel ? "Sing it. Make music." : "Beatbox it. Make music."; roll.hint = nextHint(); roll.fit();
        if (record.animate != proc.ghostAnim) applyAnimation();
        layoutOffers(); resized(); repaint();
    }
    // ---- the big button
    void tapRecord() { if (proc.isRecording() || proc.isCountingIn()) proc.stopRecording(); else if (!proc.isAnalysing()) proc.startRecording(); refresh(); }
    // ---- TAP tempo: the average of the last taps (up to 8; a 2 s pause starts over)
    void tapTempo(double now = -1) {
        if (now < 0) now = juce::Time::getMillisecondCounterHiRes() * 0.001; if (now - lastTap > 2.0) taps.clear(); lastTap = now; taps.push_back(now); if (taps.size() > 8) taps.erase(taps.begin());
        if (proc.tempoFree()) proc.setParam("tempomode", 1);
        if (taps.size() >= 2) { const double b = 60.0 * (double) (taps.size() - 1) / (taps.back() - taps.front()); proc.setParam("bpm", (float) juce::jlimit(40.0, 240.0, std::round(b))); proc.status = "TAP: " + juce::String(juce::roundToInt(juce::jlimit(40.0, 240.0, b))) + " BPM"; }
        else proc.status = "TAP: keep tapping the beat...";
        refresh();
    }
    // ---- DRAG MIDI / DRAG WAV: the export gate first, then the file, then the drag
    void dragOut(bool midi) {
        if (proc.take().empty()) return;
        if (!proc.exportAllowed(this, midi ? "ghosthum.drag-midi" : "ghosthum.drag-wav")) return;
        const auto f = midi ? proc.writeMidiFile() : proc.writeWavFile(); if (!f.existsAsFile()) { proc.status = "Could not write the file."; repaint(); return; }
        proc.status = (midi ? "MIDI file ready: " : "Audio file ready: ") + f.getFileName() + "  (drop it in your DAW; it is also in Documents/Ghosty/Ghost Hum)"; repaint();
        ghosty::exportgate::dragFiles(proc.license, { f.getFullPathName() }, midi ? (juce::Component*) &dragMidi : (juce::Component*) &dragWav);
    }
    void openTeach() { if (teachPanel) return; closeHelp(); teachPanel = std::make_unique<ui::TeachPanel>(proc); teachPanel->onClose = [this] { juce::MessageManager::callAsync([sp = juce::Component::SafePointer<HumEditor>(this)] { if (sp) { sp->teachPanel.reset(); sp->refresh(); } }); };
        scrim.onClick = nullptr; scrim.toFront(false); addAndMakeVisible(*teachPanel); resized(); }
    void openHelp() { if (helpPanel) { closeHelp(); return; } if (teachPanel) return; helpPanel = std::make_unique<ui::HelpPanel>(); helpPanel->onClose = [this] { juce::MessageManager::callAsync([sp = juce::Component::SafePointer<HumEditor>(this)] { if (sp) sp->closeHelp(); }); };
        scrim.onClick = helpPanel->onClose; scrim.toFront(false); addAndMakeVisible(*helpPanel); resized(); }
    void closeHelp() { helpPanel.reset(); resized(); }
    // ---- key commands (the roll forwards its keys here too)
    bool command(const juce::KeyPress& k) {
        const auto m = k.getModifiers(); const bool cmd = m.isCommandDown() || m.isCtrlDown(); const int c = k.getKeyCode(); const juce::juce_wchar tc = k.getTextCharacter();
        auto is = [&](char lower) { return c == lower || c == (lower - 32) || tc == (juce::juce_wchar) lower || tc == (juce::juce_wchar) (lower - 32); };   // (the key code is upper case on a real keyboard; tests build KeyPress('q'))
        if (k == juce::KeyPress::escapeKey) { if (helpPanel) { closeHelp(); return true; } if (teachPanel) { teachPanel->onClose(); return true; } roll.clearSelection(); return true; }
        if (k == juce::KeyPress::spaceKey) { if (proc.isPlaying()) proc.stop(); else proc.play(); refresh(); return true; }
        if (cmd && (c == 'Z' || c == 'z')) { if (m.isShiftDown()) proc.doRedo(); else proc.doUndo(); roll.clearSelection(); refresh(); return true; }
        if (cmd && (c == 'Y' || c == 'y')) { proc.doRedo(); roll.clearSelection(); refresh(); return true; }
        if (cmd && (c == 'A' || c == 'a')) { roll.selectAll(); return true; }
        if (cmd && (c == 'D' || c == 'd')) { roll.duplicate(); return true; }
        if (cmd && (c == 'C' || c == 'c')) { roll.copy(); return true; }
        if (cmd && (c == 'V' || c == 'v')) { roll.paste(); return true; }
        if (cmd) return false;
        if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) { roll.removeSelected(); return true; }
        if (c == juce::KeyPress::upKey) return roll.nudge(0, 1, m.isShiftDown()); if (c == juce::KeyPress::downKey) return roll.nudge(0, -1, m.isShiftDown());   // (key codes: Shift is allowed)
        if (c == juce::KeyPress::leftKey) return roll.nudge(-1, 0, m.isShiftDown()); if (c == juce::KeyPress::rightKey) return roll.nudge(1, 0, m.isShiftDown());
        if (is('r')) { tapRecord(); return true; }
        if (is('q')) { const int q = (proc.quant() + 1) % 5; proc.setParam("quant", (float) q); proc.status = juce::String("QUANTIZE ") + quantName(q); refresh(); return true; }
        if (is('g')) { proc.setGhostAnim(!proc.ghostAnim); refresh(); return true; }
        if (is('t')) { tapTempo(); return true; }
        if (tc == '?' || c == '?' || c == '/' || tc == '/') { openHelp(); return true; }
        return false; }
    bool keyPressed(const juce::KeyPress& k) override { return command(k); }
    bool animationRunning() const { return isTimerRunning(); }   // (tests: GHOST ANIMATION OFF stops the timer)

    // (tests reach the controls)
    HumProcessor& proc;
    ui::Seg modeSwitch { { "MELODY", "BEATBOX" } }, tempoSwitch { { "FREE", "BPM" } }, pitchSwitch { { "ORIGINAL", "NEAREST NOTE", "SNAP TO KEY" } }, quantSwitch { { "OFF", "1/4", "1/8", "1/16", "1/32" } }, sourceSwitch { { "ON GRID", "ORIGINAL" } }, inputSwitch { { "AUTO", "TRACK", "MAC MIC" } };
    ui::RecordButton record; ui::Roll roll; ui::Dial bpm, strength, swing; ui::Picker key, sound; ui::Pill metro { "CLICK OFF" }, countIn { "COUNT-IN ON" }, tap { "TAP" }, host { "HOST" };
    std::array<ui::Pill, 4> offers { ui::Pill("KEEP ORIGINAL"), ui::Pill(""), ui::Pill(""), ui::Pill("") };
    ui::Pill play { "PLAY" }, clean { "CLEAN" }, undoB { "UNDO" }, redoB { "REDO" }, dragMidi { "DRAG MIDI" }, dragWav { "DRAG WAV" }, teach { "TEACH GHOST YOUR SOUNDS" }, clearB { "NEW" }, anim { "GHOST ANIMATION: ON" }, help { "?" };
    ui::GlassBypass bypassButton { *proc.bypassParam() }; ghosty::UnlockButton unlock { proc.license };
    std::unique_ptr<ui::TeachPanel> teachPanel; std::unique_ptr<ui::HelpPanel> helpPanel; ui::Scrim scrim;
    ui::Backdrop backdrop; float phase = 0;
    juce::Rectangle<int> statusArea, tempoLabel, inputLabel, inputNoteArea, pitchLabel, quantLabel, offerRow, offerArea, settingsArea, barArea, tipArea; juce::String tagline, lastInputNote; int seenCounter = -1;
    std::vector<double> taps; double lastTap = -10;
    juce::TooltipWindow tips { this, 700 }; Poll poll { *this };
};

} // namespace hum
