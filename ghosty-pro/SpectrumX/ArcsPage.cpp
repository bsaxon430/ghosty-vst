#include "ArcsPage.h"
#include "ArcsFactory.h"
#include "ExportGate.h"   // Shared: the demo export gate (drags + save dialogs)

namespace arcs {
namespace {

// ---------------------------------------------------------------- theme (light default; dark = Spectrum's palette)
struct Theme {
    bool dark = false; ghosty::themes::Theme look = ghosty::themes::Theme::Light;
    juce::Colour bg0, bg1, card, card2, btn, ink, grey, dim, canvas, rowKey, grid, gridBar, purple, green, pink, amber, cyan, red, blue;
};
// the house looks (Shared/GhostThemes.h): every surface opaque, accents readable on the canvas and the cards
Theme makeTheme(ghosty::themes::Theme look) {
    const auto& T = ghosty::themes::get(look); Theme t; t.look = look; t.dark = !T.isLight;
    auto rd = [&](juce::Colour c) { return ghosty::themes::readableOn(ghosty::themes::readableOn(c, T.waveBg, 3.0), T.panel, 3.0); };
    t.bg0 = T.windowTop; t.bg1 = T.windowBottom; t.card = T.panel; t.card2 = T.card; t.btn = T.cell; t.ink = T.ink; t.grey = T.dim; t.dim = T.dim;
    t.canvas = T.waveBg; t.rowKey = T.overlay.withAlpha(0.05f); t.grid = T.overlay.withAlpha(0.07f); t.gridBar = T.overlay.withAlpha(0.16f);
    t.purple = rd(T.violet); t.green = rd(T.accent3); t.pink = rd(T.accent1); t.amber = rd(T.warn); t.cyan = rd(T.accent2); t.red = rd(T.bad);
    t.blue = rd(T.isLight ? juce::Colour(0xff2456c8) : juce::Colour(0xff6aa8ff));
    return t;
}
Theme makeTheme(bool dark) { return makeTheme(dark ? ghosty::themes::Theme::Dark : ghosty::themes::Theme::Light); }
juce::Colour onFill(juce::Colour a, juce::Colour b, const Theme& th) {
    const auto& T = ghosty::themes::get(th.look); const juce::Colour cand[] = { T.ink, T.onAccent, juce::Colour(0xff0b0814), juce::Colour(0xfff4eefc) };
    juce::Colour best = cand[0]; double bk = -1; for (auto c : cand) { const double k = juce::jmin(ghosty::themes::contrast(c, a), ghosty::themes::contrast(c, b)); if (k > bk) { bk = k; best = c; } } return best;
}
juce::Colour arcColour(const Theme& t, int i) {
    const juce::Colour C[] = { t.pink, t.cyan, t.amber, t.green, t.purple, t.blue, t.red, ghosty::themes::readableOn(juce::Colour(t.dark ? 0xffa3e635 : 0xff4d7c0f), t.canvas, 3.0) };
    return C[(size_t) (((i % 8) + 8) % 8)];
}
juce::String noteName(int m) { static const char* N[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; return juce::String(N[((m % 12) + 12) % 12]) + juce::String(m / 12 - 1); }
juce::String hzText(double hz) { return hz >= 1000 ? juce::String(hz / 1000.0, 2) + " kHz" : juce::String(hz, 1) + " Hz"; }
void tint(juce::Component& b, juce::Colour c) { b.getProperties().set("tint", (juce::int64) c.getARGB()); }

struct LnF : juce::LookAndFeel_V4 {
    const Theme* t = nullptr;
    void apply(const Theme& th) {
        t = &th;
        setColour(juce::ComboBox::backgroundColourId, th.btn); setColour(juce::ComboBox::textColourId, th.ink); setColour(juce::ComboBox::arrowColourId, th.purple);
        setColour(juce::PopupMenu::backgroundColourId, th.card2); setColour(juce::PopupMenu::textColourId, th.ink); setColour(juce::PopupMenu::highlightedBackgroundColourId, th.purple.withAlpha(0.35f));
        setColour(juce::PopupMenu::highlightedTextColourId, th.ink);
        setColour(juce::TextButton::textColourOffId, th.ink); setColour(juce::TextButton::textColourOnId, th.ink); setColour(juce::Label::textColourId, th.ink);
        setColour(juce::TooltipWindow::backgroundColourId, th.card2); setColour(juce::TooltipWindow::textColourId, th.ink); setColour(juce::TooltipWindow::outlineColourId, th.purple);
        setColour(juce::TextEditor::backgroundColourId, th.btn); setColour(juce::TextEditor::textColourId, th.ink); setColour(juce::TextEditor::highlightColourId, th.purple.withAlpha(0.4f));
        setColour(juce::TextEditor::outlineColourId, th.purple.withAlpha(0.4f)); setColour(juce::TextEditor::focusedOutlineColourId, th.purple);
        setColour(juce::CaretComponent::caretColourId, th.ink);
    }
    juce::Colour tintOf(const juce::Component& c, juce::Colour def) const { return c.getProperties().contains("tint") ? juce::Colour((juce::uint32) (juce::int64) c.getProperties()["tint"]) : def; }
    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override {
        auto r = b.getLocalBounds().toFloat().reduced(1); const bool on = b.getToggleState(); const auto c = tintOf(b, t->purple);
        if (on) { g.setGradientFill(juce::ColourGradient(c.withAlpha(0.95f), r.getX(), r.getY(), c.interpolatedWith(t->pink, 0.4f).withAlpha(0.88f), r.getRight(), r.getBottom(), false)); g.fillRoundedRectangle(r, 8); }
        else { g.setColour(t->btn.withAlpha(b.isEnabled() ? 0.95f : 0.5f)); g.fillRoundedRectangle(r, 8); }
        g.setColour((on ? (t->dark ? t->ink : juce::Colours::white) : c).withAlpha(over ? 0.95f : (on ? 0.55f : 0.45f))); g.drawRoundedRectangle(r, 8, down ? 2.0f : 1.1f);
    }
    void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) override {
        g.setFont(getTextButtonFont(b, b.getHeight()));
        const auto c = tintOf(b, t->purple);   // the label colour that reads on the filled button
        g.setColour(b.getToggleState() ? onFill(c, c.interpolatedWith(t->pink, 0.4f), *t) : t->ink.withAlpha(b.isEnabled() ? 0.92f : 0.35f));
        g.drawFittedText(b.getButtonText(), b.getLocalBounds().reduced(3, 2), juce::Justification::centred, 1, 0.75f);
    }
    juce::Font getTextButtonFont(juce::TextButton&, int h) override { return juce::FontOptions(juce::jmin(12.0f, h * 0.46f), juce::Font::bold); }
    void drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& cb) override {
        auto r = juce::Rectangle<float>(0, 0, (float) w, (float) h).reduced(1); g.setColour(t->btn); g.fillRoundedRectangle(r, 8); g.setColour(tintOf(cb, t->purple).withAlpha(0.5f)); g.drawRoundedRectangle(r, 8, 1.1f);
        juce::Path a; a.addTriangle((float) w - 15, h * 0.5f - 2, (float) w - 7, h * 0.5f - 2, (float) w - 11, h * 0.5f + 3); g.setColour(tintOf(cb, t->purple)); g.fillPath(a);
    }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return juce::FontOptions(12.0f, juce::Font::bold); }
    juce::Font getPopupMenuFont() override { return juce::FontOptions(13.0f); }
    void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override { label.setBounds(6, 1, box.getWidth() - 22, box.getHeight() - 2); label.setFont(getComboBoxFont(box)); }
    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override {
        auto r = juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h).reduced(3); const float sz = juce::jmin(r.getWidth(), r.getHeight()); auto c = r.withSizeKeepingCentre(sz, sz);
        const auto tn = tintOf(s, t->green); const float th = 4, ang = a0 + pos * (a1 - a0);
        g.setColour(t->btn); g.fillEllipse(c.reduced(th + 1));
        juce::Path tr; tr.addCentredArc(c.getCentreX(), c.getCentreY(), sz / 2 - th / 2, sz / 2 - th / 2, 0, a0, a1, true); g.setColour(t->ink.withAlpha(0.1f)); g.strokePath(tr, juce::PathStrokeType(th));
        juce::Path ar; ar.addCentredArc(c.getCentreX(), c.getCentreY(), sz / 2 - th / 2, sz / 2 - th / 2, 0, a0, ang, true);
        g.setGradientFill(juce::ColourGradient(t->purple, c.getX(), c.getBottom(), tn, c.getRight(), c.getY(), false)); g.strokePath(ar, juce::PathStrokeType(th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(t->ink); g.drawLine({ c.getCentre().getPointOnCircumference(sz * 0.12f, ang), c.getCentre().getPointOnCircumference(sz / 2 - th - 4, ang) }, 2.0f);
    }
};

struct Knob : juce::Component {
    juce::Slider s { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox }; juce::Label l;
    std::function<void(double)> onChange; std::function<void()> onEnd; bool dragging = false;
    Knob(const juce::String& n, double lo, double hi, double def, const juce::String& tip) {
        s.setRange(lo, hi); s.setValue(def, juce::dontSendNotification); s.setDoubleClickReturnValue(true, def); s.setPopupDisplayEnabled(true, true, nullptr);
        // a drag commits once at its end; anything else (double-click reset, wheel, keys) commits at once
        s.onDragStart = [this] { dragging = true; };
        s.onValueChange = [this] { if (onChange) onChange(s.getValue()); if (!dragging && onEnd) onEnd(); };
        s.onDragEnd = [this] { dragging = false; if (onEnd) onEnd(); };
        s.setTooltip(tip + "  (double-click: default)"); addAndMakeVisible(s);
        l.setText(n, juce::dontSendNotification); l.setJustificationType(juce::Justification::centred); l.setFont(juce::FontOptions(9.5f, juce::Font::bold)); l.setInterceptsMouseClicks(false, false); addAndMakeVisible(l);
    }
    void resized() override { auto r = getLocalBounds(); l.setBounds(r.removeFromBottom(13)); s.setBounds(r); }
};

double gridStepBeats(double pxPerBeat) { double st = 1.0; while (pxPerBeat * st < 14 && st < 4096) st *= 2; while (pxPerBeat * st > 90 && st > 1.0 / 16) st *= 0.5; return st; }

} // namespace


// =====================================================================================================================
struct ArcsPage::Impl {
    ArcsPage& owner; ArcsHost& host;
    Theme th; LnF lnf;
    int tool = Draw, curveShape = 0; std::vector<int> sel; bool snap = true, mpe = false;
    Arc brush;                                         // what new lines start as (edited when nothing is selected)
    bool dirty = false;                                // a live edit waits to be published (30 Hz)
    juce::uint32 seenEdit = 0;
    juce::String statusText; double statusTime = 0;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::TooltipWindow> tips;
    bool wasRendering = false; juce::int64 randomSeed = 1;

    struct Canvas; struct Speed; struct CurveEd; struct MidiDrag; struct AudioDrag;
    std::unique_ptr<Canvas> canvas; std::unique_ptr<Speed> speed; std::unique_ptr<CurveEd> waveEd, envEd, panEd, cutEd;
    std::unique_ptr<MidiDrag> midiDrag; std::unique_ptr<AudioDrag> audioDrag;
    juce::OwnedArray<juce::TextButton> buttons; juce::OwnedArray<Knob> knobs;
    juce::TextButton* toolB[5] {}; juce::TextButton *undoB {}, *redoB {}, *playB {}, *stopB {}, *hostB {}, *loopB {}, *revB {}, *speedB {}, *autoB {}, *syncB {}, *quantB {}, *snapB {}, *mpeB {},
        *themeB {}, *muteB {}, *modB {}, *midiInB {}, *trackB {}, *repeatB {}, *factoryB {}, *randomB {};
    std::array<juce::TextButton*, kSlots> bankB {};
    std::vector<juce::TextButton*> needSel;            // stamp buttons (enabled with a selection)
    juce::TextEditor lengthEd, bpmEd;
    juce::ComboBox keyCombo, scaleCombo, fmCombo, gridCombo, jumpCombo, bendCombo, inkCombo, filterCombo, rateCombo;
    juce::TextButton *lenB {}, *patchB {};   // menus that look like live buttons (a combo placeholder reads as disabled)
    Knob *masterK {}, *gainK {}, *fmK {}, *uniK {}, *detK {}, *sprK {}, *subK {}, *drvK {}, *resK {};
    juce::Label lenUnit, bpmLabel;

    Impl(ArcsPage& o, ArcsHost& h, bool ownTips);
    ~Impl();
    // ---- helpers
    Page& P() { return host.edit(); }
    const Page& cP() const { return host.page(); }
    double bpm() const { return host.bpm(); }
    double pageBeats() const { const auto& p = cP(); return p.tempoSync ? p.lengthBeats : p.lengthSeconds * bpm() / 60.0; }
    void say(const juce::String& s) { statusText = s; statusTime = juce::Time::getMillisecondCounterHiRes(); owner.repaint(0, owner.getHeight() - 30, owner.getWidth(), 30); }
    void done(const juce::String& what) { validateSel(); if (!sel.empty()) commit(what); refreshArcEditors(); }          // a preset applied
    void edited(bool fin, const juce::String& what) { validateSel(); if (sel.empty()) return; if (fin) commit(what); else live(); }   // a drawn curve / knob
    void commit(const juce::String& what) { dirty = false; host.commit(what); seenEdit = host.editCount(); afterEdit(); }
    void live() { dirty = true; afterEdit(); }
    void afterEdit();
    void validateSel() { sel.erase(std::remove_if(sel.begin(), sel.end(), [this](int id) { return cP().find(id) == nullptr; }), sel.end()); }
    Arc* focus() { validateSel(); return sel.empty() ? nullptr : P().find(sel.front()); }
    template <typename F> void forTargets(F&& f) { validateSel(); if (sel.empty()) f(brush); else for (int id : sel) if (auto* a = P().find(id)) f(*a); }
    void applyTheme();
    void refreshControls();
    void refreshArcEditors();
    void setTool(int t);
    void stamp(int which);
    void undo() { if (host.undo()) { validateSel(); refreshControls(); say("Undo"); } }
    void redo() { if (host.redo()) { validateSel(); refreshControls(); say("Redo"); } }
    void layout();
    void placeBpmLabel() { if (bpmEd.isVisible()) bpmLabel.setBounds(bpmEd.getRight() + 4, bpmEd.getY(), 44, 28); else bpmLabel.setBounds(bpmEd.getX(), bpmEd.getY(), 120, 28); }
    juce::TextButton* button(const juce::String& n, juce::Colour c, std::function<void()> f, const juce::String& tip);
    Knob* knob(const juce::String& n, double lo, double hi, double def, juce::Colour c, const juce::String& tip, std::function<void(Arc&, double)> set, const juce::String& what);
    // page-level helpers used by the canvas and the keys
    double gridStepBeatsNow() const;                   // the snap step in beats (0 = free)
    float snapX(float x) const;
    float snapPitch(float y) const { const auto& p = cP(); return p.quantise ? snapToKey(y, p.root, p.scale) : (snap ? std::round(y) : y); }
    float timeOfPos(float x) const;                    // seconds at a page position (speed lane included)
    void copySel(bool cut);
    void paste();
    void nudge(float dxBeats, float dySemis);
    void jumpTo(float x);
    void loadFactory();
    void randomise();
    bool exportAllowed(juce::Component* ui, const char* id) { if (!owner.allowExport || !owner.allowExport(ui != nullptr ? ui : &owner, id)) { say("Exporting is part of the full version"); return false; } return true; }
    juce::String loopRefusedKey;   // the demo: the loop's WAV is not pre-rendered to disk (asked once per loop)
    // ---- the loop's WAV grip: rendered in the background whenever the loop (or the page) changes, debounced, cached
    juce::File loopWav; juce::String loopKey, loopWantKey; double loopChangedAt = 0; bool loopRendering = false; std::shared_ptr<bool> loopAlive = std::make_shared<bool>(true); int loopRenderCount = 0;
    std::function<void(const juce::StringArray&)> performDragForTests;   // tests swap the OS drag
    juce::String loopKeyNow() const { const auto& p = cP(); return p.loopOn ? juce::String((juce::int64) host.editCount()) + ":" + juce::String(p.loopA, 5) + ":" + juce::String(p.loopB, 5) + ":" + juce::String(host.bpm(), 2) : juce::String(); }
    bool loopWavReady() const { const auto k = loopKeyNow(); return k.isNotEmpty() && loopKey == k && loopWav.existsAsFile(); }
    void tickLoopRender() {
        const auto k = loopKeyNow(); if (k.isEmpty() || k == loopKey) return;
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        if (k != loopWantKey) { loopWantKey = k; loopChangedAt = now; return; }   // wait until the loop stops moving
        if (loopRendering || now - loopChangedAt < 0.35) return;
        if (k == loopRefusedKey && !(owner.exportOpen && owner.exportOpen())) return;
        if (!owner.allowExport || !owner.allowExport(nullptr, "spec.arcs.loop-prerender")) { loopRefusedKey = k; return; }   // demo: no WAV on disk (silent: no user action)
        loopRefusedKey.clear();
        loopRendering = true; ++loopRenderCount; auto req = host.prepareRender(); std::weak_ptr<bool> alive = loopAlive;
        const auto f = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("GhostyArcs").getChildFile(cP().name.replaceCharacters(" /\\:", "____") + " loop " + juce::String(loopRenderCount) + ".wav");
        juce::Thread::launch([this, req, alive, k, f] {
            juce::AudioBuffer<float> buf; bool ok = ArcsHost::renderLoopToBuffer(req, buf, 48000.0);
            if (ok) { f.getParentDirectory().createDirectory(); f.deleteFile(); juce::WavAudioFormat wav; ok = false;
                if (auto os = std::unique_ptr<juce::OutputStream>(f.createOutputStream())) if (auto* w = wav.createWriterFor(os.get(), 48000.0, (unsigned) buf.getNumChannels(), 24, {}, 0)) { os.release(); std::unique_ptr<juce::AudioFormatWriter> wr(w); ok = wr->writeFromAudioSampleBuffer(buf, 0, buf.getNumSamples()); } }   // GATE: spec.arcs.loop-prerender
            juce::MessageManager::callAsync([this, alive, ok, k, f] { if (alive.expired()) return; loopRendering = false; if (ok && k == loopWantKey) { loopWav.deleteFile(); loopWav = f; loopKey = k; } else f.deleteFile(); });
        });
    }
    void dragLoop(bool midi, juce::Component* from) {
        if (!cP().loopOn) return;
        if (!exportAllowed(from, midi ? "spec.arcs.loop-midi-drag" : "spec.arcs.loop-wav-drag")) return;
        if (!midi && !loopWavReady()) { say("Rendering the loop... drag WAV in a moment"); return; }
        juce::File f = loopWav;
        if (midi) { ArcsHost::MidiReport r; f = host.writeLoopMidi(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("GhostyArcs").getChildFile(cP().name.replaceCharacters(" /\\:", "____") + " loop" + (mpe ? "_mpe" : "") + ".mid"), mpe, &r);
                    if (!f.existsAsFile()) { say("Could not write the MIDI file"); return; } say(juce::String(r.notes) + " notes in the loop"); }
        const juce::StringArray files { f.getFullPathName() };
        if (performDragForTests) performDragForTests(files); else ghosty::exportgate::dragFiles(files, from);
    }
    bool startAudioRender(juce::Component* ui) { if (!exportAllowed(ui, "spec.arcs.audio-render")) return false;
        const auto f = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("GhostyArcs").getChildFile(cP().name.replaceCharacters(" /\\:", "____") + ".wav");
        const bool ok = host.startRender(f); say(ok ? "Rendering in the background..." : "Could not start the render"); return ok; }
};

// =====================================================================================================================
// THE CANVAS: x = page position (time), y = MIDI pitch (log frequency). Ruler on top: click = jump, drag = loop region,
// double-click = fit. Wheel = zoom time, shift + wheel = zoom pitch, horizontal wheel / middle-drag = scroll.
struct ArcsPage::Impl::Canvas : juce::Component {
    Impl& I;
    static constexpr int kRuler = 30, kKeys = 46;   // ruler rows: bar numbers, loop band, gesture hint
    double vx0 = 0, vx1 = 1; float vy0 = 33, vy1 = 99;
    juce::Image layer; bool layerDirty = true;
    enum Mode { None, Stroke, LineM, CurveM, EraseM, Band, Move, StretchT, StretchP, Rotate, Pan, RulerM, RulerR, LoopA, LoopB, LoopMove, GripW, GripM } mode = None;
    bool gripStarted = false;
    // the loop brace's grips: drag only the loop out as WAV (rendered in the background, cached) or MIDI (notes in the loop, from its start)
    juce::Rectangle<float> gripRect(bool midi) const { const auto& p = I.cP(); const float xb = px(p.loopB), xa = px(p.loopA), w = 36, gap = 3;
        float x = xb - 2 * w - gap - 4; if (x < xa + 4) x = xb + 6; return { x + (midi ? w + gap : 0.0f), 12.0f, w, 12.0f }; }
    float loopA0 = 0, loopB0 = 1; bool loopWasOn = false;
    std::vector<Pt> stroke; int strokeDir = 0; Pt lineA, lineB; juce::Point<float> down, hover { -1, -1 }; bool erased = false, rightErase = false, eraseRefused = false;
    Page orig; Box origBox; juce::Rectangle<float> origPix; double panVx0 = 0; float panVy0 = 0; float rulerX0 = 0;

    explicit Canvas(Impl& i) : I(i) { setMouseCursor(juce::MouseCursor::CrosshairCursor); setWantsKeyboardFocus(false); }
    juce::Rectangle<float> area() const { return { (float) kKeys, (float) kRuler, (float) getWidth() - kKeys, (float) getHeight() - kRuler }; }
    float px(double x) const { const auto a = area(); return a.getX() + (float) ((x - vx0) / (vx1 - vx0)) * a.getWidth(); }
    float py(float m) const { const auto a = area(); return a.getBottom() - (m - vy0) / (vy1 - vy0) * a.getHeight(); }
    float xAt(float p) const { const auto a = area(); return (float) (vx0 + (p - a.getX()) / a.getWidth() * (vx1 - vx0)); }
    float yAt(float p) const { const auto a = area(); return vy0 + (a.getBottom() - p) / a.getHeight() * (vy1 - vy0); }
    float unitsPerPxX() const { return (float) ((vx1 - vx0) / area().getWidth()); }
    float unitsPerPxY() const { return (vy1 - vy0) / area().getHeight(); }
    double autoGridBeats() const { const double beats = I.pageBeats(); const double ppb = area().getWidth() / ((vx1 - vx0) * beats); return gridStepBeats(ppb); }
    void invalidate() { layerDirty = true; repaint(); }
    void clampView() {
        const double w = juce::jlimit(0.002, 1.0, vx1 - vx0); vx0 = juce::jlimit(0.0, 1.0 - w, vx0); vx1 = vx0 + w;
        const float h = juce::jlimit(6.0f, kMaxMidi - kMinMidi + 2.0f, vy1 - vy0); vy0 = juce::jlimit(kMinMidi - 1.0f, kMaxMidi + 1.0f - h, vy0); vy1 = vy0 + h;
    }
    void fit() { vx0 = 0; vx1 = 1; const auto& p = I.cP(); if (p.arcs.empty()) { vy0 = 33; vy1 = 99; } else { float lo = 1e9f, hi = -1e9f; for (auto& a : p.arcs) { if (a.modOnly) continue; lo = juce::jmin(lo, a.minPitch()); hi = juce::jmax(hi, a.maxPitch()); }
        if (lo > hi) { lo = 48; hi = 84; } const float c = 0.5f * (lo + hi), h = juce::jmax(24.0f, hi - lo + 12.0f); vy0 = c - h / 2; vy1 = c + h / 2; } clampView(); invalidate(); }

    // ---- hit tests
    float distToArc(const Arc& a, juce::Point<float> p) const {
        float best = 1.0e9f;
        for (size_t k = 0; k + 1 < a.pts.size(); ++k) {
            const juce::Line<float> l({ px(a.pts[k].x), py(a.pts[k].y) }, { px(a.pts[k + 1].x), py(a.pts[k + 1].y) });
            juce::Point<float> np; best = juce::jmin(best, l.getDistanceFromPoint(p, np));
        }
        return best;
    }
    int arcAt(juce::Point<float> p) const { int best = -1; float bd = 7.0f; for (auto& a : I.cP().arcs) { const float d = distToArc(a, p); if (d < bd) { bd = d; best = a.id; } } return best; }
    juce::Rectangle<float> selPix() const {
        const Box b = boxOf(I.cP(), I.sel); if (b.empty()) return {};
        return juce::Rectangle<float>::leftTopRightBottom(px(b.x0), py(b.y1), px(b.x1), py(b.y0)).expanded(6);
    }
    juce::Point<float> hT(juce::Rectangle<float> r) const { return { r.getRight(), r.getCentreY() }; }
    juce::Point<float> hP(juce::Rectangle<float> r) const { return { r.getCentreX(), r.getY() }; }
    juce::Point<float> hR(juce::Rectangle<float> r) const { return { r.getRight() + 14, r.getY() - 14 }; }

    // ---- painting
    void drawLine(juce::Graphics& g, const Arc& arc, float dx, float alphaMul, bool isSel, bool quant, const Page& p) {
        const auto& T = I.th; const auto c = arcColour(T, arc.colour); const float alpha = (arc.mute ? 0.3f : (arc.modOnly ? 0.55f : 1.0f)) * alphaMul;
        const float x0p = px(arc.x0() + dx), x1p = px(arc.x0() + dx + arc.playLen()); const auto a = area();
        if (x1p < a.getX() - 5 || x0p > a.getRight() + 5) return;
        const int steps = juce::jlimit(2, 2000, (int) ((x1p - x0p) / 2.5f));
        auto at = [&](float u) { return juce::Point<float> { x0p + (x1p - x0p) * u, py(arc.pitchAtU(u)) }; };
        if (isSel) { juce::Path glow; for (int k = 0; k <= steps; ++k) { const auto q = at((float) k / steps); if (k == 0) glow.startNewSubPath(q); else glow.lineTo(q); }
            g.setColour(c.withAlpha(0.22f * alphaMul)); g.strokePath(glow, juce::PathStrokeType(9.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }
        juce::Point<float> prev; float prevW = 0;
        for (int k = 0; k <= steps; ++k) {
            const float u = (float) k / steps; const auto q = at(u);
            const float w = 1.2f + 4.2f * arc.envAt(u) * (0.35f + 0.65f * arc.gain) * (arc.unison > 1 ? 1.3f : 1.0f);
            if (k > 0) {
                const float pn = arc.panAt(u);   // pan shown as a hue lean: left = cooler, right = warmer
                g.setColour(c.interpolatedWith(pn < 0 ? T.blue : T.amber, std::abs(pn) * 0.35f).withAlpha(alpha));
                if ((arc.modOnly || arc.source == SrcNoise) && (k / 3) % 2 == 1) {} else g.drawLine({ prev, q }, 0.5f * (w + prevW));
            }
            prev = q; prevW = w;
        }
        if (quant && !arc.mute) {
            juce::Path qp; for (int k = 0; k <= steps; ++k) { const float u = (float) k / steps; const juce::Point<float> q { x0p + (x1p - x0p) * u, py(snapToKey(arc.pitchAtU(u), p.root, p.scale)) }; if (k == 0) qp.startNewSubPath(q); else qp.lineTo(q); }
            g.setColour(T.ink.withAlpha(0.45f * alphaMul)); g.strokePath(qp, juce::PathStrokeType(1.0f));
        }
        g.setColour(c.withAlpha(alpha)); g.fillEllipse(x0p - 3, py(arc.pts.front().y) - 3, 6, 6);
    }
    void paintLayer() {
        layer = juce::Image(juce::Image::ARGB, juce::jmax(1, getWidth()), juce::jmax(1, getHeight()), true); juce::Graphics g(layer);
        const auto& T = I.th; const auto& p = I.cP(); const auto a = area();
        g.setColour(T.canvas); g.fillRect(a);
        // pitch rows: key notes shaded when quantise is on, octave lines + labels (labels kept inside the canvas)
        g.setColour(T.btn); g.fillRect(0.0f, a.getY(), (float) kKeys, a.getHeight());
        const float rowH = a.getHeight() / (vy1 - vy0);
        { juce::Graphics::ScopedSaveState ss(g); g.reduceClipRegion(juce::Rectangle<float>(0, a.getY(), (float) getWidth(), a.getHeight()).toNearestInt());
        for (int m = (int) std::floor(vy0); m <= (int) std::ceil(vy1); ++m) {
            const float y = py((float) m + 0.5f);
            if (p.quantise && inScale(m, p.root, p.scale)) { g.setColour(T.rowKey); g.fillRect(a.getX(), y, a.getWidth(), rowH); }
            const bool black = ((1 << (((m % 12) + 12) % 12)) & 0x54A) != 0;
            if (rowH >= 5) { g.setColour(black ? T.ink.withAlpha(0.18f) : T.canvas); g.fillRect(kKeys - 12.0f, y + 0.5f, 11.0f, juce::jmax(1.0f, rowH - 1.0f)); }
            if (m % 12 == 0) {
                g.setColour(T.gridBar); g.drawHorizontalLine((int) py((float) m - 0.5f), a.getX(), a.getRight());
                const float ly = juce::jlimit(a.getY() + 1.0f, a.getBottom() - 15.0f, py((float) m) - 7.0f);
                g.setColour(T.grey); g.setFont(juce::FontOptions(9.5f, juce::Font::bold)); g.drawText(noteName(m), 2, (int) ly, kKeys - 14, 14, juce::Justification::centredLeft);
            } else if (rowH >= 9) { g.setColour(T.grid); g.drawHorizontalLine((int) py((float) m - 0.5f), a.getX(), a.getRight()); }
        } }
        // time grid (the chosen grid when it is dense enough, else the zoom's)
        const double beats = I.pageBeats(); const double ppb = a.getWidth() / ((vx1 - vx0) * beats);
        double st = I.gridStepBeatsNow(); if (st <= 0 || st * ppb < 7) st = autoGridBeats();
        for (double b = std::floor(vx0 * beats / st) * st; b <= vx1 * beats + 1e-9; b += st) {
            const float x = px(b / beats); if (x < a.getX() - 1) continue;
            const bool bar = std::fmod(b + 1e-7, 4.0) < 1e-5; const bool beat = std::fmod(b + 1e-7, 1.0) < 1e-5;
            g.setColour(bar ? T.gridBar : (beat ? T.grid.withMultipliedAlpha(1.6f) : T.grid)); g.drawVerticalLine((int) x, a.getY(), a.getBottom());
        }
        if (p.loopOn) { g.setColour(T.amber.withAlpha(0.06f)); g.fillRect(juce::Rectangle<float>::leftTopRightBottom(px(p.loopA), a.getY(), px(p.loopB), a.getBottom()));
            g.setColour(T.amber.withAlpha(0.5f)); g.drawVerticalLine((int) px(p.loopA), a.getY(), a.getBottom()); g.drawVerticalLine((int) px(p.loopB), a.getY(), a.getBottom()); }
        g.reduceClipRegion(a.toNearestInt());
        // FM links
        for (auto& t : p.arcs) if (t.fmSource >= 0) if (auto* s = p.find(t.fmSource)) {
            const juce::Point<float> A { px(0.5f * (s->x0() + s->x1())), py(s->pitchAt(0.5f * (s->x0() + s->x1()))) }, B { px(0.5f * (t.x0() + t.x1())), py(t.pitchAt(0.5f * (t.x0() + t.x1()))) };
            juce::Path pth; pth.startNewSubPath(A); pth.quadraticTo((A.x + B.x) / 2, juce::jmin(A.y, B.y) - 30, B.x, B.y);
            juce::Path dashed; const float d[] = { 5, 4 }; juce::PathStrokeType(1.4f).createDashedStroke(dashed, pth, d, 2);
            g.setColour(arcColour(T, s->colour).withAlpha(0.35f + 0.6f * t.fmDepth)); g.fillPath(dashed); g.fillEllipse(B.x - 3, B.y - 3, 6, 6);
        }
        // lines (+ faint repeats), then their numbers (stable ids; a label that would overlap another is skipped)
        std::vector<juce::Rectangle<float>> labels;
        for (const auto& arc : p.arcs) {
            if (arc.pts.size() < 2) continue;
            const bool isSel = std::find(I.sel.begin(), I.sel.end(), arc.id) != I.sel.end();
            drawLine(g, arc, 0.0f, 1.0f, isSel, p.quantise, p);
            if (arc.repeat) { const float len = arc.playLen(); for (int k = 1; k < 64 && arc.x0() + k * len < vx1 && arc.x0() + k * len < 1.0f; ++k) drawLine(g, arc, k * len, 0.35f, false, false, p); }
        }
        g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        for (const auto& arc : p.arcs) {
            if (arc.pts.size() < 2) continue;
            const bool isSel = std::find(I.sel.begin(), I.sel.end(), arc.id) != I.sel.end();
            if (p.arcs.size() > 48 && !isSel) continue;
            const auto r = juce::Rectangle<float>(px(arc.x0()) - 26, py(arc.pts.front().y) - 7, 22, 14);
            bool clash = false; for (auto& l : labels) if (l.intersects(r)) { clash = true; break; } if (clash) continue;
            labels.push_back(r); g.setColour(T.grey.withAlpha(0.9f)); g.drawText(juce::String(arc.id), r, juce::Justification::centredRight);
        }
        layerDirty = false;
    }
    void paint(juce::Graphics& g) override {
        const auto& T = I.th; const auto& p = I.cP(); const auto a = area();
        if (layerDirty || layer.getWidth() != getWidth() || layer.getHeight() != getHeight()) paintLayer();
        g.drawImageAt(layer, 0, 0);
        // ruler
        g.setColour(T.card2); g.fillRect(0, 0, getWidth(), kRuler);
        const double beats = I.pageBeats(); const double ppb = a.getWidth() / ((vx1 - vx0) * beats); const double st = juce::jmax(1.0, gridStepBeats(ppb) * 2);
        g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
        for (double b = std::floor(vx0 * beats / st) * st; b <= vx1 * beats + 1e-9; b += st) {
            const float x = px(b / beats); if (x < a.getX() - 1) continue;
            const bool bar = std::fmod(b + 1e-9, 4.0) < 1e-6;
            g.setColour(bar ? T.ink.withAlpha(0.75f) : T.grey.withAlpha(0.7f)); g.drawVerticalLine((int) x, bar ? 3.0f : 10.0f, (float) kRuler);
            if (bar) g.drawText(p.tempoSync ? juce::String((int) (b / 4) + 1) : juce::String(I.timeOfPos((float) (b / beats)), 1) + "s", (int) x + 3, 1, 40, 12, juce::Justification::centredLeft);
        }
        if (a.getWidth() > 420) { g.setColour(T.grey.withAlpha(0.55f)); g.setFont(juce::FontOptions(8.5f, juce::Font::bold));   // the gesture hint (its own row, under the loop band)
            g.drawText("right-click: play from here  |  right-drag: loop", juce::Rectangle<float>(a.getRight() - 262, (float) kRuler - 11, 256, 10), juce::Justification::centredRight); }
        if (p.loopOn) { auto lr = juce::Rectangle<float>::leftTopRightBottom(px(p.loopA), 14, px(p.loopB), 21); g.setColour(T.amber.withAlpha(0.75f)); g.fillRoundedRectangle(lr, 2);
            g.setColour(T.amber); g.fillRect(px(p.loopA) - 1.5f, 2.0f, 3.0f, (float) kRuler - 4); g.fillRect(px(p.loopB) - 1.5f, 2.0f, 3.0f, (float) kRuler - 4);   // left-drag these to resize
            for (int k = 0; k < 2; ++k) { const auto gr = gripRect(k == 1); const auto c = k == 1 ? T.cyan : T.amber; g.setColour(T.card2); g.fillRoundedRectangle(gr, 3); g.setColour(c); g.drawRoundedRectangle(gr, 3, 1.2f);
                g.setColour(T.ink); g.setFont(juce::FontOptions(9.0f, juce::Font::bold)); g.drawText(k == 1 ? "MIDI" : (I.loopWavReady() ? "WAV" : "WAV.."), gr, juce::Justification::centred); } }
        g.setColour(T.grey); g.setFont(juce::FontOptions(8.5f, juce::Font::bold)); g.drawText(p.tempoSync ? "BAR" : "TIME", 2, 2, kKeys - 4, 14, juce::Justification::centredLeft);
        // live gesture previews
        const auto nc = arcColour(T, (int) p.arcs.size());
        if (mode == Stroke && stroke.size() > 1) { juce::Path s; s.startNewSubPath(px(stroke[0].x), py(stroke[0].y)); for (auto& q : stroke) s.lineTo(px(q.x), py(q.y)); g.setColour(nc.withAlpha(0.9f)); g.strokePath(s, juce::PathStrokeType(2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }
        if (mode == LineM || mode == CurveM) {
            g.setColour(nc.withAlpha(0.9f));
            if (mode == LineM) g.drawLine(px(lineA.x), py(lineA.y), px(lineB.x), py(lineB.y), 2.5f);
            else { const auto c = makeCurve(lineA, lineB, I.curveShape); juce::Path s; for (size_t k = 0; k < c.size(); ++k) { if (k == 0) s.startNewSubPath(px(c[k].x), py(c[k].y)); else s.lineTo(px(c[k].x), py(c[k].y)); } g.strokePath(s, juce::PathStrokeType(2.5f)); }
            g.setFont(juce::FontOptions(11.0f, juce::Font::bold)); g.setColour(T.ink); g.drawText(noteName((int) std::round(lineB.y)), (int) px(lineB.x) + 8, (int) py(lineB.y) - 18, 60, 14, juce::Justification::left); }
        if (mode == Band) { auto r = juce::Rectangle<float>(down, hover); g.setColour(T.cyan.withAlpha(0.08f)); g.fillRect(r); g.setColour(T.cyan); g.drawRect(r, 1.2f); }
        // selection box + handles
        if (!I.sel.empty() && I.tool == Select) {
            auto r = selPix(); if (!r.isEmpty()) {
                const float d[] = { 4, 3 }; juce::Path rp; rp.addRectangle(r); juce::Path ds; juce::PathStrokeType(1.0f).createDashedStroke(ds, rp, d, 2); g.setColour(T.cyan.withAlpha(0.9f)); g.fillPath(ds);
                auto handle = [&](juce::Point<float> c, bool round) { auto hr = juce::Rectangle<float>(10, 10).withCentre(c); g.setColour(T.canvas); if (round) g.fillEllipse(hr); else g.fillRect(hr); g.setColour(T.cyan); if (round) g.drawEllipse(hr, 1.6f); else g.drawRect(hr, 1.6f); };
                handle(hT(r), false); handle(hP(r), false); handle(hR(r), true);
                g.setColour(T.cyan.withAlpha(0.6f)); g.drawLine(r.getRight(), r.getY(), hR(r).x - 4, hR(r).y + 4, 1.0f);
            }
        }
        // erase brush / hover readout (real time includes the speed lane)
        if (hover.x >= a.getX() && hover.y >= a.getY()) {
            if (I.tool == Erase || rightErase) { g.setColour(T.red.withAlpha(0.8f)); g.drawEllipse(hover.x - 12, hover.y - 12, 24, 24, 1.3f); }
            const float m = yAt(hover.y); const float qm = p.quantise ? snapToKey(m, p.root, p.scale) : m; const float hx = xAt(hover.x);
            const double secs = I.timeOfPos(hx);
            const juce::String txt = noteName((int) std::round(qm)) + "  " + hzText(midiToHz(qm)) + "   " + (p.tempoSync ? "beat " + juce::String(secs * I.bpm() / 60.0 + 1.0, 2) : juce::String(secs, 2) + " s");
            g.setFont(juce::FontOptions(10.5f, juce::Font::bold)); auto tr = juce::Rectangle<float>(a.getRight() - 196, a.getY() + 6, 188, 18);
            g.setColour(T.card2.withAlpha(0.9f)); g.fillRoundedRectangle(tr, 5); g.setColour(T.ink); g.drawText(txt, tr, juce::Justification::centred);
        }
        // playhead (+ where a pending quantised jump will land)
        const float pp = I.host.engine.playPos.load(); if (pp >= 0) { const float x = px(pp); if (x >= a.getX() && x <= a.getRight()) { g.setColour(T.green); g.drawLine(x, 0, x, a.getBottom(), 1.6f); } }
        if (I.host.engine.pendingJumpStatus.load() && pendingJumpX >= 0) { const float x = px(pendingJumpX); g.setColour(T.green.withAlpha(0.5f)); const float d[] = { 3, 3 }; juce::Path l; l.startNewSubPath(x, 0); l.lineTo(x, a.getBottom()); juce::Path ds; juce::PathStrokeType(1.2f).createDashedStroke(ds, l, d, 2); g.fillPath(ds); }
        if (p.arcs.empty() && mode == None) {
            g.setColour(T.grey.withAlpha(0.85f)); g.setFont(juce::FontOptions(16.0f, juce::Font::bold));
            g.drawFittedText("Draw a line: left to right is time, up is pitch.\nEvery line is a voice that glides along what you drew.\nOr start from a FACTORY page.", a.toNearestInt().reduced(40), juce::Justification::centred, 4);
        }
        g.setColour(T.purple.withAlpha(0.35f)); g.drawRect(getLocalBounds().toFloat(), 1.0f);
    }
    float pendingJumpX = -1;

    // ---- mouse
    void mouseMove(const juce::MouseEvent& e) override {
        hover = e.position;
        if (I.tool == Select && !I.sel.empty()) { const auto r = selPix(); const auto pos = e.position;
            if (pos.getDistanceFrom(hR(r)) < 9) setMouseCursor(juce::MouseCursor::DraggingHandCursor);
            else if (pos.getDistanceFrom(hT(r)) < 8) setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
            else if (pos.getDistanceFrom(hP(r)) < 8) setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
            else if (r.contains(pos)) setMouseCursor(juce::MouseCursor::DraggingHandCursor);
            else setMouseCursor(juce::MouseCursor::CrosshairCursor);
        } else setMouseCursor(juce::MouseCursor::CrosshairCursor);
        repaint();
    }
    void mouseExit(const juce::MouseEvent&) override { hover = { -1, -1 }; repaint(); }
    Pt at(juce::Point<float> p) const { return { juce::jlimit(0.0f, 1.0f, xAt(p.x)), juce::jlimit(kMinMidi, kMaxMidi, yAt(p.y)) }; }
    void eraseHere(juce::Point<float> p) { const Pt q = at(p); bool refused = false; if (eraseAt(I.P(), q.x, q.y, 12 * unitsPerPxX(), 12 * unitsPerPxY(), &refused)) { erased = true; I.live(); } eraseRefused = eraseRefused || refused; }

    void mouseDown(const juce::MouseEvent& e) override {
        I.owner.grabKeyboardFocus();
        down = hover = e.position; erased = false; rightErase = false; eraseRefused = false;
        if (e.mods.isMiddleButtonDown() || (e.mods.isAltDown() && e.mods.isCommandDown())) { mode = Pan; panVx0 = vx0; panVy0 = vy0; return; }
        if (e.position.y < kRuler) {   // the GhostBlock ruler gestures: right-click = play from here, right-drag = loop; left only edits an existing loop
            const auto& p0 = I.cP(); rulerX0 = at(e.position).x; loopA0 = p0.loopA; loopB0 = p0.loopB; loopWasOn = p0.loopOn;
            if (p0.loopOn && !e.mods.isPopupMenu()) for (int k = 0; k < 2; ++k) if (gripRect(k == 1).contains(e.position)) { mode = k == 1 ? GripM : GripW; gripStarted = false; return; }
            if (e.mods.isPopupMenu()) { mode = RulerR; return; }
            mode = RulerM;
            if (p0.loopOn) { const float xa = px(p0.loopA), xb = px(p0.loopB);
                if (std::abs(e.position.x - xa) <= 6) mode = LoopA; else if (std::abs(e.position.x - xb) <= 6) mode = LoopB; else if (e.position.x > xa && e.position.x < xb) mode = LoopMove; }
            return; }
        if (e.position.x < kKeys) { mode = None; return; }
        auto& p = I.P();
        if (e.mods.isPopupMenu() && I.tool != Select) { rightErase = true; mode = EraseM; eraseHere(e.position); return; }
        switch (I.tool) {
            case Draw: { Pt q = at(e.position); q.x = I.snapX(q.x); stroke = { q }; strokeDir = 0; mode = Stroke; break; }
            case Line: case Curve: { lineA = lineB = at(e.position); lineA.x = I.snapX(lineA.x); lineA.y = I.snapPitch(lineA.y); lineB = lineA; mode = I.tool == Line ? LineM : CurveM; break; }
            case Erase: mode = EraseM; eraseHere(e.position); break;
            case Select: {
                const auto r = selPix();
                if (!I.sel.empty() && e.position.getDistanceFrom(hR(r)) < 9) { mode = Rotate; }
                else if (!I.sel.empty() && e.position.getDistanceFrom(hT(r)) < 8) { mode = StretchT; }
                else if (!I.sel.empty() && e.position.getDistanceFrom(hP(r)) < 8) { mode = StretchP; }
                else {
                    const int hit = arcAt(e.position);
                    const bool inBox = !I.sel.empty() && r.contains(e.position);
                    if (hit >= 0) {
                        const bool selected = std::find(I.sel.begin(), I.sel.end(), hit) != I.sel.end();
                        if (e.mods.isShiftDown()) { if (selected) I.sel.erase(std::find(I.sel.begin(), I.sel.end(), hit)); else I.sel.push_back(hit); }
                        else if (!selected) I.sel = { hit };
                        mode = Move;
                    } else if (inBox) mode = Move;
                    else { if (!e.mods.isShiftDown()) I.sel.clear(); mode = Band; }
                    I.refreshArcEditors(); invalidate();
                }
                if (mode == Move && e.mods.isAltDown() && !I.sel.empty()) {
                    int skipped = 0; const auto n = copyArcs(p, I.sel, 0.0f, &skipped);
                    if (!n.empty()) { I.sel = n; I.live(); }
                    I.say(skipped ? "Copied " + juce::String((int) n.size()) + " lines; " + juce::String(skipped) + " did not fit (a page holds " + juce::String(kMaxArcs) + ")" : juce::String("Copy: drag the copy where it goes"));
                }
                orig = I.cP(); origBox = boxOf(orig, I.sel); origPix = selPix();
                break;
            }
            default: break;
        }
        repaint();
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (mode == GripW || mode == GripM) { if (!gripStarted && e.getDistanceFromDragStart() >= 6) { gripStarted = true; I.dragLoop(mode == GripM, this); } return; }
        hover = e.position;
        auto& p = I.P();
        switch (mode) {
            case None: break;
            case Pan: { const auto a = area(); const double w = vx1 - vx0; const float h = vy1 - vy0;
                vx0 = panVx0 - (e.position.x - down.x) / a.getWidth() * w; vx1 = vx0 + w; vy0 = panVy0 + (e.position.y - down.y) / a.getHeight() * h; vy1 = vy0 + h; clampView(); invalidate(); I.owner.repaint(); return; }
            case RulerM: break;
            case RulerR: {   // right-drag: a loop over the span (live), only once the mouse really moved
                if (std::abs(e.position.x - down.x) < 6) break;
                const float x = at(e.position).x; p.loopOn = true; p.loopA = I.snapX(juce::jmin(rulerX0, x)); p.loopB = I.snapX(juce::jmax(rulerX0, x));
                if (p.loopB - p.loopA < 0.001f) { p.loopB = juce::jmin(1.0f, p.loopA + 0.001f); } I.live(); break; }
            case LoopA: case LoopB: case LoopMove: {
                const float dx = (e.position.x - down.x) * unitsPerPxX();
                if (mode == LoopA) p.loopA = juce::jlimit(0.0f, loopB0 - 0.001f, I.snapX(loopA0 + dx));
                else if (mode == LoopB) p.loopB = juce::jlimit(loopA0 + 0.001f, 1.0f, I.snapX(loopB0 + dx));
                else { const float w = loopB0 - loopA0; const float a0 = juce::jlimit(0.0f, 1.0f - w, I.snapX(loopA0 + dx)); p.loopA = a0; p.loopB = a0 + w; }
                I.live(); break; }
            case Stroke: {   // either direction: the first clear move decides; points must keep going that way
                const Pt q = at(e.position); const float dxp = e.position.x - down.x;
                if (strokeDir == 0 && std::abs(dxp) > 3) strokeDir = dxp > 0 ? 1 : -1;
                if (strokeDir != 0) { const float d = (q.x - stroke.back().x) * (float) strokeDir;
                    if (d > 0.25f * unitsPerPxX()) stroke.push_back(q); else if (stroke.size() > 1 && std::abs(d) < 2 * unitsPerPxX()) stroke.back().y = q.y; }
                break; }
            case LineM: case CurveM: { lineB = at(e.position); lineB.x = I.snapX(lineB.x); lineB.y = I.snapPitch(lineB.y); if (e.mods.isShiftDown()) lineB.y = lineA.y; break; }
            case EraseM: eraseHere(e.position); break;
            case Band: break;
            case Move: {
                if (I.sel.empty()) break;
                float dx = (e.position.x - down.x) * unitsPerPxX(), dy = -(e.position.y - down.y) * unitsPerPxY();
                dx = I.snapX(origBox.x0 + dx) - origBox.x0; if (I.snap) dy = std::round(dy);
                if (e.mods.isShiftDown()) { if (std::abs(e.position.x - down.x) > std::abs(e.position.y - down.y)) dy = 0; else dx = 0; }
                p = orig; moveArcs(p, I.sel, dx, dy); I.live(); break; }
            case StretchT: {
                const float w0 = origPix.getRight() - origPix.getX() - 12; const float w1 = juce::jmax(2.0f, e.position.x - origPix.getX() - 6);
                p = orig; const float f = juce::jmin(w1 / juce::jmax(1.0f, w0), maxStretch(orig, I.sel)); stretchArcs(p, I.sel, f, 1.0f); I.live(); I.say("Stretch time x" + juce::String(f, 2)); break; }
            case StretchP: {
                const float cy = origPix.getCentreY(); const float h0 = cy - origPix.getY() - 6; const float h1 = cy - e.position.y - 6;
                const float f = juce::jlimit(-4.0f, 4.0f, h1 / juce::jmax(1.0f, h0));
                p = orig; stretchArcs(p, I.sel, 1.0f, f); I.live(); I.say("Stretch pitch x" + juce::String(f, 2) + (f < 0 ? " (upside down)" : "")); break; }
            case Rotate: {
                const auto c = origPix.getCentre(); const float a0 = std::atan2(down.y - c.y, down.x - c.x), a1 = std::atan2(e.position.y - c.y, e.position.x - c.x);
                float deg = -(a1 - a0) * 180.0f / juce::MathConstants<float>::pi; if (I.snap) deg = std::round(deg / 5.0f) * 5.0f;
                const auto a = area(); const float aspect = (12.0f * a.getHeight() / (vy1 - vy0)) / (float) (a.getWidth() / (vx1 - vx0));
                p = orig; rotateArcs(p, I.sel, deg, aspect); I.live(); I.say("Rotate " + juce::String((int) deg) + " deg"); break; }
        }
        repaint();
    }
    bool addLine(std::vector<Pt> pts, const juce::String& what) {
        auto& p = I.P();
        if (p.room() <= 0) { I.say("The page holds " + juce::String(kMaxArcs) + " lines: erase some first"); return false; }
        Arc a = I.brush; a.pts = std::move(pts); a.colour = (int) p.arcs.size(); a.fmSource = -1;
        if (auto* n = p.add(std::move(a))) { I.sel = { n->id }; I.commit(what); I.refreshArcEditors(); return true; }
        return false;
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (mode == GripW || mode == GripM) { if (!gripStarted) I.say(mode == GripM ? "Drag MIDI into your DAW: only the notes in the loop, from its start" : (I.loopWavReady() ? "Drag WAV into your DAW: only the loop" : "Rendering the loop... drag WAV in a moment")); mode = None; return; }
        auto& p = I.P(); const Mode m = mode; mode = None;
        switch (m) {
            case RulerM: I.say("Ruler: right-click plays from here, right-drag makes a loop (two fingers on a trackpad)"); break;   // a plain click never moves anything
            case RulerR: {
                if (std::abs(e.position.x - down.x) < 6) { p.loopOn = loopWasOn; p.loopA = loopA0; p.loopB = loopB0; I.dirty = false; I.jumpTo(at(e.position).x); }
                else { I.commit("Loop region"); I.refreshControls(); I.say("Loop region set (LOOP turns it off; left-drag its ends to resize, its middle to move)"); }
                break; }
            case LoopA: case LoopB: case LoopMove: if (I.dirty) { I.commit("Loop region"); I.refreshControls(); } break;
            case Stroke: {
                if (stroke.size() >= 2 && std::abs(px(stroke.back().x) - px(stroke.front().x)) >= 3) {
                    if (strokeDir < 0) std::reverse(stroke.begin(), stroke.end());   // drawn right to left: same line, played left to right
                    makeMonotonic(stroke);
                    stroke.back().x = juce::jmax(stroke.back().x, I.snapX(stroke.back().x));
                    smoothStroke(stroke, I.cP().ink == 0 ? 0.0f : (I.cP().ink == 1 ? 0.4f : 1.0f));
                    simplifyStroke(stroke, 0.6f * unitsPerPxX(), 0.25f);
                    if (stroke.size() >= 2) addLine(stroke, "Draw");
                } else { const int hit = arcAt(e.position); if (hit >= 0) { I.sel = { hit }; I.refreshArcEditors(); } }
                stroke.clear(); break; }
            case LineM: case CurveM: {
                Pt A = lineA, B = lineB; if (B.x < A.x) std::swap(A, B);
                if (px(B.x) - px(A.x) >= 3) addLine(m == LineM ? std::vector<Pt> { A, B } : makeCurve(A, B, I.curveShape), m == LineM ? "Line" : "Curve");
                break; }
            case EraseM: if (erased) { I.validateSel(); I.commit("Erase"); I.refreshArcEditors(); } if (eraseRefused) I.say("The page is full: cutting a line in two needs room for one more line"); break;
            case Band: {
                const auto r = juce::Rectangle<float>(down, e.position);
                for (auto& a : p.arcs) for (auto& q : a.pts) if (r.contains(px(q.x), py(q.y))) { if (std::find(I.sel.begin(), I.sel.end(), a.id) == I.sel.end()) I.sel.push_back(a.id); break; }
                I.refreshArcEditors(); break; }
            case Move: case StretchT: case StretchP: case Rotate:
                if (I.dirty || e.getDistanceFromDragStart() > 2) { I.commit(m == Move ? "Move" : (m == Rotate ? "Rotate" : "Stretch")); }
                break;
            case None: case Pan: break;
        }
        rightErase = false; invalidate();
    }
    void mouseDoubleClick(const juce::MouseEvent& e) override { if (e.position.y < kRuler) { fit(); I.say("View fitted"); } }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override {
        const auto a = area();
        if (std::abs(w.deltaX) > std::abs(w.deltaY)) { const double sh = -w.deltaX * (vx1 - vx0) * 0.5; vx0 += sh; vx1 += sh; }
        else if (e.mods.isShiftDown()) { const float c = yAt(e.position.y), f = std::pow(0.85f, w.deltaY * 4.0f); vy0 = c + (vy0 - c) * f; vy1 = c + (vy1 - c) * f; }
        else { const double c = xAt(juce::jlimit(a.getX(), a.getRight(), e.position.x)), f = std::pow(0.85, w.deltaY * 4.0); vx0 = c + (vx0 - c) * f; vx1 = c + (vx1 - c) * f; }
        clampView(); invalidate(); I.owner.repaint();
    }
};

// =====================================================================================================================
// THE SPEED LANE: the playhead's speed across the page, drawn (x 1/4 .. x 4, log). Right-click / double-click = flat.
struct ArcsPage::Impl::Speed : juce::Component, juce::SettableTooltipClient {
    Impl& I; std::array<float, 256> v {}; bool drawing = false; int lastI = -1; float lastV = 0;
    explicit Speed(Impl& i) : I(i) { setMouseCursor(juce::MouseCursor::CrosshairCursor); setTooltip("Playhead speed across the page (x1/4 .. x4). Draw it; right-click or double-click = flat"); }
    juce::Rectangle<float> area() const { return { (float) Canvas::kKeys, 2.0f, (float) getWidth() - Canvas::kKeys, (float) getHeight() - 4 }; }
    float px(double x) const { return I.canvas->px(x); }
    float yOf(float s) const { const auto a = area(); return a.getCentreY() - s / kMaxSpeedOct * (a.getHeight() * 0.5f - 2); }
    float sOf(float y) const { const auto a = area(); return juce::jlimit(-kMaxSpeedOct, kMaxSpeedOct, (a.getCentreY() - y) / (a.getHeight() * 0.5f - 2) * kMaxSpeedOct); }
    void paint(juce::Graphics& g) override {
        const auto& T = I.th; const auto& p = I.cP(); const auto a = area();
        g.setColour(T.canvas); g.fillRect(getLocalBounds()); g.setColour(T.btn); g.fillRect(0, 0, Canvas::kKeys, getHeight());
        g.setColour(T.grey); g.setFont(juce::FontOptions(8.5f, juce::Font::bold)); g.drawFittedText("SPEED", 2, 2, Canvas::kKeys - 4, getHeight() - 4, juce::Justification::centredLeft, 2);
        for (int s = -2; s <= 2; ++s) { g.setColour(s == 0 ? T.gridBar : T.grid); g.drawHorizontalLine((int) yOf((float) s), a.getX(), a.getRight()); }
        g.setFont(juce::FontOptions(8.0f, juce::Font::bold)); g.setColour(T.dim); g.drawText("x4", (int) a.getRight() - 22, (int) yOf(2) - 1, 20, 10, juce::Justification::centredRight); g.drawText("x1/4", (int) a.getRight() - 28, (int) yOf(-2) - 9, 26, 10, juce::Justification::centredRight);
        juce::Path pth; const int N = juce::jmax(2, (int) a.getWidth() / 2);
        for (int k = 0; k <= N; ++k) { const float x = a.getX() + a.getWidth() * k / N; const float pos = I.canvas->xAt(x); const float y = yOf(curveAt(p.speed, pos, 0.0f)); if (k == 0) pth.startNewSubPath(x, y); else pth.lineTo(x, y); }
        g.setColour((p.speedOn ? T.green : T.dim).withAlpha(p.speedOn ? 0.95f : 0.6f)); g.strokePath(pth, juce::PathStrokeType(1.8f));
        if (!p.speedOn) { g.setColour(T.grey.withAlpha(0.8f)); g.setFont(juce::FontOptions(10.0f, juce::Font::bold)); g.drawText("Draw here to bend the playhead's speed (SPEED turns it on/off)", a.reduced(8, 0), juce::Justification::centredLeft); }
        const float pp = I.host.engine.playPos.load(); if (pp >= 0) { const float x = px(pp); if (x >= a.getX()) { g.setColour(T.green.withAlpha(0.7f)); g.drawVerticalLine((int) x, 0, (float) getHeight()); } }
        g.setColour(T.purple.withAlpha(0.35f)); g.drawRect(getLocalBounds().toFloat(), 1.0f);
    }
    int idxAt(float x) const { return juce::jlimit(0, 255, (int) std::round(juce::jlimit(0.0f, 1.0f, I.canvas->xAt(x)) * 255.0f)); }
    void mouseDown(const juce::MouseEvent& e) override {
        auto& p = I.P();
        if (e.mods.isPopupMenu()) { p.speed.clear(); I.commit("Speed flat"); repaint(); return; }
        for (int k = 0; k < 256; ++k) v[(size_t) k] = curveAt(p.speed, k / 255.0f, 0.0f);
        drawing = true; lastI = idxAt(e.position.x); lastV = sOf(e.position.y); v[(size_t) lastI] = lastV; write(false);
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (!drawing) { return; } const int i = idxAt(e.position.x); const float s = sOf(e.position.y);
        const int a = juce::jmin(i, lastI), b = juce::jmax(i, lastI);
        for (int k = a; k <= b; ++k) { const float t = b == a ? 1.0f : (float) (k - lastI) / (float) (i - lastI); v[(size_t) k] = lastV + (s - lastV) * juce::jlimit(0.0f, 1.0f, t); }
        lastI = i; lastV = s; write(false);
    }
    void mouseUp(const juce::MouseEvent&) override { if (drawing) { drawing = false; write(true); } }
    void mouseDoubleClick(const juce::MouseEvent&) override { I.P().speed.clear(); I.commit("Speed flat"); repaint(); }
    void write(bool final) {
        auto& p = I.P(); p.speed.resize(256); for (int k = 0; k < 256; ++k) p.speed[(size_t) k] = { k / 255.0f, std::abs(v[(size_t) k]) < 0.03f ? 0.0f : v[(size_t) k] };
        if (!p.speedOn) { p.speedOn = true; I.refreshControls(); }
        if (final) I.commit("Speed lane"); else I.live();
        repaint();
    }
};

// =====================================================================================================================
// A DRAWABLE CURVE: the line's single cycle (256 points, -1..1), its envelope (0..1), pan (-1..1, top = right) or filter
// cutoff (0..1). The label sits in its own strip above the drawing.
struct ArcsPage::Impl::CurveEd : juce::Component, juce::SettableTooltipClient {
    enum Kind { Wave, Env, Pan, Cut } kind; Impl& I; std::vector<float> v; int lastI = -1; float lastV = 0; juce::String label;
    std::function<void(bool final)> onEdit;
    static constexpr int kLabel = 12;
    CurveEd(Impl& i, Kind k) : kind(k), I(i), v((size_t) (k == Wave ? kWaveLen : 64), 0.0f) { setMouseCursor(juce::MouseCursor::CrosshairCursor); }
    float lo() const { return kind == Env || kind == Cut ? 0.0f : -1.0f; }
    juce::Rectangle<float> box() const { return getLocalBounds().toFloat().withTrimmedTop((float) kLabel); }
    juce::Rectangle<float> area() const { return box().reduced(4, 4); }
    float yOf(float val) const { const auto a = area(); return a.getBottom() - (val - lo()) / (1.0f - lo()) * a.getHeight(); }
    float vOf(float y) const { const auto a = area(); return juce::jlimit(lo(), 1.0f, lo() + (a.getBottom() - y) / a.getHeight() * (1.0f - lo())); }
    int iOf(float x) const { const auto a = area(); return juce::jlimit(0, (int) v.size() - 1, (int) std::round((x - a.getX()) / a.getWidth() * (float) (v.size() - 1))); }
    void paint(juce::Graphics& g) override {
        const auto& T = I.th; const auto a = area(); const auto c = kind == Wave ? T.pink : (kind == Env ? T.green : (kind == Pan ? T.cyan : T.amber));
        g.setColour(T.grey.withAlpha(0.9f)); g.setFont(juce::FontOptions(8.5f, juce::Font::bold)); g.drawText(label, 2, 0, getWidth() - 4, kLabel, juce::Justification::centredLeft);
        g.setColour(T.canvas); g.fillRoundedRectangle(box(), 6);
        g.setColour(T.grid); for (int k = 1; k < 4; ++k) g.drawVerticalLine((int) (a.getX() + a.getWidth() * k / 4), a.getY(), a.getBottom());
        const float base = kind == Cut ? 0.0f : 0.0f;
        g.setColour(T.gridBar); g.drawHorizontalLine((int) yOf(base), a.getX(), a.getRight());
        juce::Path line, fill; fill.startNewSubPath(a.getX(), yOf(base));
        for (size_t k = 0; k < v.size(); ++k) { const float x = a.getX() + a.getWidth() * (float) k / (float) (v.size() - 1), y = yOf(v[k]); if (k == 0) line.startNewSubPath(x, y); else line.lineTo(x, y); fill.lineTo(x, y); }
        fill.lineTo(a.getRight(), yOf(base)); fill.closeSubPath();
        g.setColour(c.withAlpha(0.14f)); g.fillPath(fill); g.setColour(c); g.strokePath(line, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        if (kind == Pan) { g.setColour(T.dim); g.setFont(juce::FontOptions(8.5f, juce::Font::bold)); g.drawText("R", a.withHeight(10).toNearestInt(), juce::Justification::topRight); g.drawText("L", a.withTrimmedTop(a.getHeight() - 10).toNearestInt(), juce::Justification::bottomRight); }
        if (kind == Cut && !isEnabled()) { g.setColour(T.canvas.withAlpha(0.7f)); g.fillRoundedRectangle(box(), 6); g.setColour(T.grey); g.setFont(juce::FontOptions(9.0f, juce::Font::bold)); g.drawText("pick a FILTER", box(), juce::Justification::centred); }
        g.setColour(c.withAlpha(0.45f)); g.drawRoundedRectangle(box().reduced(0.5f), 6, 1.0f);
    }
    void mouseDown(const juce::MouseEvent& e) override { lastI = iOf(e.position.x); lastV = vOf(e.position.y); v[(size_t) lastI] = lastV; repaint(); if (onEdit) onEdit(false); }
    void mouseDrag(const juce::MouseEvent& e) override {
        const int i = iOf(e.position.x); const float s = vOf(e.position.y); const int a = juce::jmin(i, lastI), b = juce::jmax(i, lastI);
        for (int k = a; k <= b; ++k) { const float t = b == a ? 1.0f : (float) (k - lastI) / (float) (i - lastI); v[(size_t) k] = lastV + (s - lastV) * juce::jlimit(0.0f, 1.0f, t); }
        lastI = i; lastV = s; repaint(); if (onEdit) onEdit(false);
    }
    void mouseUp(const juce::MouseEvent&) override { if (onEdit) onEdit(true); }
    std::vector<Pt> asPts() const { std::vector<Pt> p(v.size()); for (size_t k = 0; k < v.size(); ++k) p[k] = { (float) k / (float) (v.size() - 1), v[k] }; return p; }
    void fromPts(const std::vector<Pt>& p, float def) { for (size_t k = 0; k < v.size(); ++k) v[k] = curveAt(p, (float) k / (float) (v.size() - 1), def); repaint(); }
};

// =====================================================================================================================
// DRAG OUT: MIDI (written when the drag starts; click = save as) and AUDIO (click = render in the background, then drag)
struct ArcsPage::Impl::MidiDrag : juce::Component, juce::SettableTooltipClient {
    Impl& I; bool started = false;
    explicit MidiDrag(Impl& i) : I(i) { setTooltip("Drag into your DAW: one note per line, glides as pitch bend, envelope as CC11 and pan as CC10 (MPE: pressure + CC74 pan, one channel per voice). Click to save a .mid"); setMouseCursor(juce::MouseCursor::DraggingHandCursor); }
    juce::File temp() const { return juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("GhostyArcs").getChildFile(I.cP().name.replaceCharacters(" /\\:", "____") + (I.mpe ? "_mpe" : "") + ".mid"); }
    void paint(juce::Graphics& g) override { const auto& T = I.th; auto r = getLocalBounds().toFloat().reduced(1); g.setColour(T.cyan.withAlpha(isMouseOver() ? 0.22f : 0.1f)); g.fillRoundedRectangle(r, 8); g.setColour(T.cyan); g.drawRoundedRectangle(r, 8, 1.3f);
        g.setColour(T.ink); g.setFont(juce::FontOptions(11.0f, juce::Font::bold)); g.drawText("DRAG MIDI", r, juce::Justification::centred); }
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    void mouseDown(const juce::MouseEvent&) override { started = false; }
    void report(const ArcsHost::MidiReport& r) { I.say(juce::String(r.notes) + " notes" + (r.splits ? ", " + juce::String(r.splits) + " wide glides split into extra notes (raise BEND to keep them whole)" : juce::String())
                                                 + (r.tracks > 1 ? ", " + juce::String(r.tracks) + " tracks (more voices than channels)" : juce::String())); }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (started || e.getDistanceFromDragStart() < 6) { return; } started = true; if (!I.exportAllowed(this, "spec.arcs.midi-drag")) return; ArcsHost::MidiReport r;
        const auto f = I.host.writeMidi(temp(), I.mpe, &r); if (f.existsAsFile()) { report(r); ghosty::exportgate::dragFiles({ f.getFullPathName() }, this); }
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (started || e.getDistanceFromDragStart() >= 6) return;
        if (!I.exportAllowed(this, "spec.arcs.midi-save")) return;
        ghosty::exportgate::saveDialog(I.chooser, "Save the page as MIDI", juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile(I.cP().name + ".mid"), "*.mid", [this](const juce::File& f) {
            ArcsHost::MidiReport r; if (I.host.writeMidi(f.withFileExtension("mid"), I.mpe, &r).existsAsFile()) report(r); else I.say("Could not write the MIDI file"); });
    }
};
struct ArcsPage::Impl::AudioDrag : juce::Component, juce::SettableTooltipClient {
    Impl& I; bool started = false;
    explicit AudioDrag(Impl& i) : I(i) { setTooltip("Click: render the page to audio in the background (one pass at your project's sample rate, speed lane + reverse included). Then drag the WAV into your DAW"); }
    bool fresh() const { return I.host.renderReady() && I.host.renderedEdit() == I.host.editCount(); }
    void paint(juce::Graphics& g) override {
        const auto& T = I.th; auto r = getLocalBounds().toFloat().reduced(1); const bool busy = I.host.rendering(); const bool ok = fresh();
        g.setColour(T.amber.withAlpha(isMouseOver() ? 0.22f : 0.1f)); g.fillRoundedRectangle(r, 8);
        if (busy) { g.setColour(T.amber.withAlpha(0.35f)); g.fillRoundedRectangle(r.withWidth(r.getWidth() * I.host.renderProgress() / 100.0f), 8); }
        g.setColour(T.amber); g.drawRoundedRectangle(r, 8, 1.3f);
        g.setColour(T.ink); g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.drawText(busy ? "RENDERING " + juce::String(I.host.renderProgress()) + "%" : (ok ? "DRAG AUDIO" : "RENDER AUDIO"), r, juce::Justification::centred);
    }
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit(const juce::MouseEvent&) override { repaint(); }
    void mouseDown(const juce::MouseEvent&) override { started = false; }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (started || e.getDistanceFromDragStart() < 6) { return; } started = true; if (!I.exportAllowed(this, "spec.arcs.audio-drag")) return;
        if (!fresh()) { I.say("Click RENDER AUDIO first, then drag it"); return; }
        ghosty::exportgate::dragFiles({ I.host.renderedFile().getFullPathName() }, this);
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (started || e.getDistanceFromDragStart() >= 6) return;
        if (I.host.rendering()) { I.host.cancelRender(); I.say("Render cancelled"); repaint(); return; }
        if (fresh()) { I.say("Rendered: drag it into your DAW"); return; }
        I.startAudioRender(this); repaint();
    }
};

// =====================================================================================================================
juce::TextButton* ArcsPage::Impl::button(const juce::String& n, juce::Colour c, std::function<void()> f, const juce::String& tip) {
    auto* b = buttons.add(new juce::TextButton(n)); tint(*b, c); b->onClick = std::move(f); b->setTooltip(tip); b->setClickingTogglesState(false);
    b->setWantsKeyboardFocus(false); owner.addAndMakeVisible(b); return b;
}
Knob* ArcsPage::Impl::knob(const juce::String& n, double lo, double hi, double def, juce::Colour c, const juce::String& tip, std::function<void(Arc&, double)> set, const juce::String& what) {
    auto* k = knobs.add(new Knob(n, lo, hi, def, tip)); tint(k->s, c); owner.addAndMakeVisible(k);
    k->onChange = [this, set](double v) { forTargets([&](Arc& a) { set(a, v); }); edited(false, {}); };
    k->onEnd = [this, what] { edited(true, what); };
    return k;
}
double ArcsPage::Impl::gridStepBeatsNow() const { const double g = gridBeats(cP().grid); return g < 0 ? canvas->autoGridBeats() : g; }
float ArcsPage::Impl::snapX(float x) const {
    if (!snap) { return x; } const double st = gridStepBeatsNow(); if (st <= 0) { return x; }
    const double g = st / juce::jmax(1.0e-9, pageBeats()); return (float) juce::jlimit(0.0, 1.0, std::round(x / g) * g);
}
float ArcsPage::Impl::timeOfPos(float x) const {
    const auto& p = cP(); const double D = p.seconds(bpm()); if (!p.speedOn) return (float) (x * D);
    double t = 0; const int n = 256; for (int k = 0; k < n; ++k) { const float a = x * k / n, b = x * (k + 1) / n; t += (b - a) / p.speedAt(0.5f * (a + b)); } return (float) (t * D);
}
void ArcsPage::Impl::jumpTo(float x) {
    const auto& p = cP(); x = snapX(x);
    static const float Q[] = { 0, 1, 4 }; const float q = Q[(size_t) juce::jlimit(0, 2, p.jumpQuant)];
    host.engine.jumpQuantBeats.store(q); host.engine.jumpTo.store(x); canvas->pendingJumpX = x;
    if (!host.engine.isPlaying.load() && !p.midiIn) { host.engine.internalPlay.store(true); refreshControls(); }
    say(q > 0 && host.engine.isPlaying.load() ? juce::String("Jump on the next ") + (q > 1 ? "bar" : "beat") : "Jump to " + juce::String(x * 100.0f, 1) + "% of the page");
}
void ArcsPage::Impl::copySel(bool cut) {
    validateSel(); if (sel.empty()) { say("Select some lines first"); return; }
    juce::SystemClipboard::copyTextToClipboard(linesToText(cP(), sel));   // NOT-EXPORT: ARCS line shapes (its own page format, pasted back into ARCS), no audio / MIDI
    if (cut) { for (int id : sel) P().remove(id); say("Cut " + juce::String((int) sel.size()) + " lines"); sel.clear(); commit("Cut"); refreshArcEditors(); }
    else say("Copied " + juce::String((int) sel.size()) + " lines (paste on any page, in any instance)");
}
void ArcsPage::Impl::paste() {
    const auto text = juce::SystemClipboard::getTextFromClipboard();
    float at = canvas->hover.x >= Canvas::kKeys ? canvas->xAt(canvas->hover.x) : (host.engine.playPos.load() >= 0 ? host.engine.playPos.load() : 0.0f);
    int skipped = 0; const auto ids = pasteLines(P(), text, snapX(at), &skipped);
    if (ids.empty()) { say(skipped ? "The page is full" : "Nothing to paste (copy lines first)"); return; }
    sel = ids; commit("Paste"); setTool(Select); refreshArcEditors();
    say("Pasted " + juce::String((int) ids.size()) + " lines" + (skipped ? "; " + juce::String(skipped) + " did not fit" : juce::String()));
}
void ArcsPage::Impl::nudge(float dxBeats, float dySemis) {
    validateSel(); if (sel.empty()) return;
    moveArcs(P(), sel, (float) (dxBeats / juce::jmax(1.0e-9, pageBeats())), dySemis); commit("Nudge"); canvas->invalidate();
}
void ArcsPage::Impl::loadFactory() {
    juce::PopupMenu m; std::map<juce::String, juce::PopupMenu> groups; const auto& defs = factory::pages();
    for (int i = 0; i < (int) defs.size(); ++i) groups[defs[(size_t) i].group].addItem(i + 1, defs[(size_t) i].name);
    for (auto& g : groups) m.addSubMenu(g.first, g.second);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(factoryB), [this](int r) {
        if (r <= 0) { return; } const auto& d = factory::pages()[(size_t) r - 1]; Page p = d.make();
        P() = p; sel.clear(); commit("Factory page"); refreshControls(); canvas->fit(); say("Factory page: " + juce::String(d.name) + " (UNDO brings your page back)"); });
}
void ArcsPage::Impl::randomise() {
    validateSel(); if (sel.empty()) { say("Select some lines first (SELECT tool)"); return; }
    juce::PopupMenu m; m.addItem(1, "Subtle (a little drift)"); m.addItem(2, "Medium"); m.addItem(3, "Wild");
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(randomB), [this](int r) {
        if (r <= 0) { return; } static const float A[] = { 0, 0.15f, 0.45f, 1.0f }; const juce::int64 seed = randomSeed++;
        randomiseArcs(P(), sel, A[r], seed); commit("Randomise"); refreshArcEditors(); canvas->invalidate(); say("Randomised (seed " + juce::String(seed) + ", UNDO to take it back)"); });
}

ArcsPage::Impl::Impl(ArcsPage& o, ArcsHost& h, bool ownTips) : owner(o), host(h) {
    if (ownTips) tips = std::make_unique<juce::TooltipWindow>(&owner, 700);
    th = makeTheme(host.dark); lnf.apply(th); owner.setLookAndFeel(&lnf);
    brush.env = { { 0.0f, 0.0f }, { 0.04f, 1.0f }, { 0.85f, 0.8f }, { 1.0f, 0.0f } };   // a soft start and end for new lines
    brush.setWave(waveShape(ShapeSoft));
    canvas = std::make_unique<Canvas>(*this); owner.addAndMakeVisible(*canvas);
    speed = std::make_unique<Speed>(*this); owner.addAndMakeVisible(*speed);
    waveEd = std::make_unique<CurveEd>(*this, CurveEd::Wave); envEd = std::make_unique<CurveEd>(*this, CurveEd::Env); panEd = std::make_unique<CurveEd>(*this, CurveEd::Pan); cutEd = std::make_unique<CurveEd>(*this, CurveEd::Cut);
    for (auto* c : { waveEd.get(), envEd.get(), panEd.get(), cutEd.get() }) owner.addAndMakeVisible(c);
    waveEd->label = "WAVEFORM (ONE CYCLE)"; envEd->label = "ENVELOPE"; panEd->label = "PAN"; cutEd->label = "FILTER CUTOFF";
    waveEd->onEdit = [this](bool fin) { std::array<float, kWaveLen> w {}; for (int k = 0; k < kWaveLen; ++k) w[(size_t) k] = waveEd->v[(size_t) k]; forTargets([&](Arc& a) { a.setWave(w); a.source = SrcWave; }); edited(fin, "Draw waveform"); };
    envEd->onEdit = [this](bool fin) { const auto p = envEd->asPts(); forTargets([&](Arc& a) { a.env = p; }); edited(fin, "Draw envelope"); };
    panEd->onEdit = [this](bool fin) { const auto p = panEd->asPts(); forTargets([&](Arc& a) { a.pan = p; }); edited(fin, "Draw pan"); };
    cutEd->onEdit = [this](bool fin) { const auto p = cutEd->asPts(); forTargets([&](Arc& a) { a.cutoff = p; }); edited(fin, "Draw cutoff"); };
    waveEd->setTooltip("The line's single cycle: draw any shape. It is band-limited (and FM / drive run oversampled), so it does not alias");
    envEd->setTooltip("Loudness along the line, start to end"); panEd->setTooltip("Pan along the line (top = right, bottom = left)");
    cutEd->setTooltip("Filter cutoff along the line (20 Hz .. 20 kHz; with TRACK: +-4 octaves around the line's pitch)");
    midiDrag = std::make_unique<MidiDrag>(*this); audioDrag = std::make_unique<AudioDrag>(*this); owner.addAndMakeVisible(*midiDrag); owner.addAndMakeVisible(*audioDrag); midiDrag->setComponentID("arcsMidiDrag"); audioDrag->setComponentID("arcsAudioDrag");

    // ---- toolbar
    const char* toolNames[] = { "DRAW", "LINE", "CURVE EXP", "ERASE", "SELECT" };
    const char* toolTips[] = { "Draw freehand lines (D), either direction. Right-drag erases. INK sets how much a shaky hand is smoothed",
                               "Straight lines: click-drag from start to end (L). Shift = flat. Ends snap to the key when QUANTISE is on",
                               "Shaped risers (C): click-drag start to end. Click CURVE again to switch EXP / S / LOG",
                               "Erase parts of lines (E); cutting a line in the middle makes two",
                               "Select (S): click / shift-click / drag a box. Drag = move, alt-drag = copy, handles stretch time / pitch, round handle rotates. Arrows nudge (shift = bar / octave)" };
    for (int t = 0; t < 5; ++t) toolB[t] = button(toolNames[t], th.purple, [this, t] { if (t == Curve && tool == Curve) { curveShape = (curveShape + 1) % 3; toolB[Curve]->setButtonText("CURVE " + curveShapeNames()[curveShape]); } setTool(t); }, toolTips[t]);
    auto stampB = [this](const juce::String& n, int w, const juce::String& tip) { auto* b = button(n, th.cyan, [this, w] { stamp(w); }, tip); needSel.push_back(b); return b; };
    stampB("COPY", 0, "Copy the selected lines right after themselves (ctrl+D). ctrl+C / ctrl+V copy between pages and instances");
    stampB("MIRROR", 1, "Play the selected group backwards in time, envelopes and pan too (M)");
    stampB("INVERT", 2, "Turn the selected group upside down in pitch (I)");
    stampB("ROT -", 3, "Rotate the group 15 degrees clockwise (or drag the round handle)");
    stampB("ROT +", 4, "Rotate the group 15 degrees anticlockwise");
    stampB("TO KEY", 5, "Snap the selected lines' points onto the key (destructive; QUANTISE does it on playback)");
    randomB = stampB("RANDOM", 7, "Controlled chaos on the selection: pitch, timing, pan, level and the cycle (undoable, new seed each time)");
    stampB("DELETE", 6, "Delete the selected lines (Del)");
    undoB = button("UNDO", th.grey, [this] { undo(); }, "Undo (ctrl+Z)");
    redoB = button("REDO", th.grey, [this] { redo(); }, "Redo (ctrl+shift+Z)");
    button("NEW", th.red, [this] { auto& p = P(); const auto keep = p; p = Page(); p.tempoSync = keep.tempoSync; p.lengthBeats = keep.lengthBeats; p.lengthSeconds = keep.lengthSeconds; p.root = keep.root; p.scale = keep.scale; p.grid = keep.grid; sel.clear(); commit("New page"); refreshControls(); say("New page (UNDO brings the old one back)"); }, "Start an empty page (undoable)");
    button("SAVE", th.green, [this] {
        chooser = std::make_unique<juce::FileChooser>("Save the page", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(cP().name + ".ghostarcs"), "*.ghostarcs");
        chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this](const juce::FileChooser& fc) {   // NOT-EXPORT: the .ghostarcs page (lines, no audio)
            auto f = fc.getResult(); if (f == juce::File()) return; f = f.withFileExtension("ghostarcs"); P().name = f.getFileNameWithoutExtension(); say(host.saveToFile(f) ? "Saved " + f.getFileName() : "Could not save"); });
    }, "Save this page to a file");
    button("LOAD", th.green, [this] {
        chooser = std::make_unique<juce::FileChooser>("Load a page", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.ghostarcs");
        chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
            const auto f = fc.getResult(); if (f == juce::File()) return;
            if (host.loadFromFile(f)) { sel.clear(); seenEdit = host.editCount(); refreshControls(); canvas->fit(); const auto m = host.takeMessage(); say("Loaded " + f.getFileName() + (m.isNotEmpty() ? ": " + m : juce::String())); }
            else say(host.takeMessage()); });
    }, "Load a page file into this slot (undoable)");
    inkCombo.addItemList({ "INK RAW", "INK SOFT", "INK SMOOTH" }, 1); inkCombo.setTooltip("How much freehand strokes are smoothed (a shaky hand does not become vibrato)");
    inkCombo.onChange = [this] { if (inkCombo.getSelectedId() > 0 && inkCombo.getSelectedId() - 1 != cP().ink) { P().ink = inkCombo.getSelectedId() - 1; commit("Ink"); } };
    themeB = button("DARK", th.purple, [this] { owner.setDark(!th.dark); }, "Light / dark");
    factoryB = button("FACTORY", th.pink, [this] { loadFactory(); }, "Start from a factory page (bass dives, risers, FM growls, drones, polyrhythms, arps...); undoable");
    button("BACK", th.grey, [this] { if (owner.onLeave) owner.onLeave(); }, "Back to the other tabs");

    // ---- transport
    playB = button("PLAY", th.green, [this] { auto& e = host.engine; if (e.hostRunning.load()) { say("Your DAW is playing the page (HOST): stop it there, or turn HOST off"); return; } e.internalPlay.store(!e.internalPlay.load()); refreshControls(); }, "Play / pause the page on its own (space). While your DAW plays and HOST is on, the DAW runs it");
    stopB = button("STOP", th.red, [this] { host.engine.internalPlay.store(false); host.engine.stopRequest.store(true); refreshControls(); }, "Stop and go back to the start (of the loop)");
    hostB = button("HOST", th.cyan, [this] { host.engine.followHost.store(!host.engine.followHost.load()); refreshControls(); }, "Follow the DAW: the page plays in time with your project while it plays (and stops with it)");
    loopB = button("LOOP", th.amber, [this] { P().loopOn = !cP().loopOn; commit("Loop"); refreshControls(); }, "Loop the region (right-drag in the bar ruler to set it; left-drag its ends to resize)");
    revB = button("REVERSE", th.pink, [this] { P().reverse = !cP().reverse; commit("Reverse"); refreshControls(); }, "Play the page backwards");
    speedB = button("SPEED", th.green, [this] { P().speedOn = !cP().speedOn; commit("Speed lane"); refreshControls(); }, "Use the drawn speed lane (playhead faster / slower across the page)");
    autoB = button("AUTO", th.amber, [this] { P().autoGain = !cP().autoGain; commit("Auto gain"); refreshControls(); }, "AUTO GAIN: the level follows how many lines sound, so a crowd does not pile into the limiter (OUT meter turns amber when it limits)");
    syncB = button("SYNC", th.cyan, [this] { auto& p = P(); if (p.tempoSync) p.lengthSeconds = juce::jlimit(kMinSeconds, kMaxSeconds, p.lengthBeats * 60.0 / bpm()); else p.lengthBeats = juce::jlimit(kMinBeats, kMaxBeats, std::round(p.lengthSeconds * bpm() / 60.0)); p.tempoSync = !p.tempoSync; commit("Tempo sync"); refreshControls(); }, "Page length in beats (follows the tempo) or in seconds");
    quantB = button("QUANTISE", th.purple, [this] { P().quantise = !cP().quantise; commit("Quantise"); refreshControls(); }, "Lines play on the notes of the key (your drawing is kept); LINE / CURVE ends snap to it too");
    snapB = button("SNAP", th.purple, [this] { snap = !snap; refreshControls(); }, "Snap starts / ends / moves / jumps to the GRID and straight lines to semitones");
    mpeB = button("MPE", th.cyan, [this] { mpe = !mpe; refreshControls(); }, "MIDI export as MPE (one channel per voice) instead of plain multi-channel");
    midiInB = button("MIDI IN", th.pink, [this] { P().midiIn = !cP().midiIn; commit("MIDI in"); refreshControls(); say(cP().midiIn ? "MIDI IN: hold a note to play the page transposed (C4 = as drawn); notes C-1..G-1 pick pages A..H" : "MIDI IN off"); },
                       "Play the page from MIDI: a held note restarts it transposed (C4 = as drawn), release stops it. Notes C-1..G-1 switch pages A..H");
    for (int s = 0; s < kSlots; ++s) bankB[(size_t) s] = button(juce::String::charToString((juce::juce_wchar) ('A' + s)), th.green, [this, s] { host.selectSlot(s); sel.clear(); seenEdit = host.editCount(); refreshControls(); canvas->fit(); say("Page " + juce::String::charToString((juce::juce_wchar) ('A' + s))); },
                                                 "Page bank: 8 pages per instance (switch live; MIDI notes C-1..G-1 switch too while MIDI IN is on)");
    muteB = button("MUTE", th.red, [this] { validateSel(); if (sel.empty()) return; const bool m = !(focus() && focus()->mute); forTargets([&](Arc& a) { a.mute = m; }); commit("Mute"); refreshArcEditors(); }, "Silence the selected lines (they still modulate)");
    modB = button("FM ONLY", th.amber, [this] { validateSel(); if (sel.empty()) return; const bool m = !(focus() && focus()->modOnly); forTargets([&](Arc& a) { a.modOnly = m; }); commit("Modulator only"); refreshArcEditors(); }, "Only heard through the lines it modulates");
    trackB = button("TRACK", th.amber, [this] { const bool t = !(focus() ? focus()->track : brush.track); forTargets([&](Arc& a) { a.track = t; }); done("Filter tracking"); }, "The cutoff lane is relative to the line's pitch (+-4 octaves), so the filter follows glides");
    repeatB = button("REPEAT", th.cyan, [this] { const bool t = !(focus() ? focus()->repeat : brush.repeat); forTargets([&](Arc& a) { a.repeat = t; }); done("Repeat"); canvas->invalidate(); }, "The line loops at its own length for the rest of the page (with RATE: polyrhythm against the page)");

    // waveform / envelope / pan presets
    const char* shapes[] = { "SIN", "TRI", "SAW", "SQR", "PLS", "SFT" };
    for (int s = 0; s < 6; ++s) button(shapes[s], th.pink, [this, s] { const auto w = waveShape(s); forTargets([&](Arc& a) { a.setWave(w); a.source = SrcWave; }); done("Waveform"); }, juce::String("Start the cycle from a ") + waveShapeNames()[s] + " (then draw on it)")->getProperties().set("row", "wave");
    button("NOISE", th.pink, [this] { forTargets([&](Arc& a) { a.source = SrcNoise; if (a.filter == FiltOff) { a.filter = FiltBP; a.track = true; a.res = 0.6f; } }); done("Noise"); }, "White noise instead of a cycle; with the band-pass tracking the line it sweeps like a riser")->getProperties().set("row", "wave");
    button("SMOOTH", th.pink, [this] { forTargets([&](Arc& a) { auto w = a.wave; for (int pass = 0; pass < 2; ++pass) { auto c = w; for (int k = 0; k < kWaveLen; ++k) w[(size_t) k] = 0.25f * c[(size_t) ((k + kWaveLen - 1) % kWaveLen)] + 0.5f * c[(size_t) k] + 0.25f * c[(size_t) ((k + 1) % kWaveLen)]; } a.setWave(w); }); done("Smooth waveform"); }, "Round off the drawn cycle (fewer high harmonics)")->getProperties().set("row", "tools");
    button("GRIT", th.pink, [this] { juce::Random r((juce::int64) juce::Time::getMillisecondCounter()); forTargets([&](Arc& a) { auto w = a.wave; for (int k = 0; k < kWaveLen; ++k) w[(size_t) k] = juce::jlimit(-1.0f, 1.0f, w[(size_t) k] + (r.nextFloat() * 2 - 1) * 0.12f); a.setWave(w); }); done("Grit"); }, "Roughen the cycle: more upper harmonics, still alias-free")->getProperties().set("row", "tools");
    const char* envs[] = { "FLAT", "SWELL", "PLUCK", "FADE", "TREM" };
    for (int s = 0; s < 5; ++s) button(envs[s], th.green, [this, s] {
        std::vector<Pt> e;
        switch (s) { case 0: e = {}; break; case 1: e = { { 0, 0 }, { 0.6f, 1 }, { 1, 0 } }; break; case 2: e = { { 0, 1 }, { 0.15f, 0.45f }, { 0.5f, 0.15f }, { 1, 0 } }; break; case 3: e = { { 0, 1 }, { 1, 0 } }; break;
                     default: for (int k = 0; k <= 32; ++k) e.push_back({ k / 32.0f, (k % 2) ? 0.25f : 1.0f }); break; }
        forTargets([&](Arc& a) { a.env = e; }); done("Envelope"); }, "Envelope preset (then draw on it)")->getProperties().set("row", "env");
    const char* pans[] = { "MID", "L>R", "R>L", "WOB" };
    for (int s = 0; s < 4; ++s) button(pans[s], th.cyan, [this, s] {
        std::vector<Pt> e;
        switch (s) { case 0: e = {}; break; case 1: e = { { 0, -0.9f }, { 1, 0.9f } }; break; case 2: e = { { 0, 0.9f }, { 1, -0.9f } }; break; default: for (int k = 0; k <= 32; ++k) e.push_back({ k / 32.0f, 0.8f * std::sin(k / 32.0f * 6 * juce::MathConstants<float>::pi) }); break; }
        forTargets([&](Arc& a) { a.pan = e; }); done("Pan"); }, "Pan preset (then draw on it)")->getProperties().set("row", "pan");

    // ---- fields
    for (auto* ed : { &lengthEd, &bpmEd }) { ed->setJustification(juce::Justification::centred); ed->setFont(juce::FontOptions(12.5f, juce::Font::bold)); ed->setSelectAllWhenFocused(true); owner.addAndMakeVisible(ed); }
    lengthEd.setTooltip("Page length (beats when SYNC is on, else seconds): 1 beat up to many minutes");
    bpmEd.setTooltip("The page's own tempo (used while your DAW does not send one)");
    lengthEd.onReturnKey = lengthEd.onFocusLost = [this] { const double v = lengthEd.getText().getDoubleValue(); if (v > 0) { auto& p = P(); if (p.tempoSync) p.lengthBeats = juce::jlimit(kMinBeats, kMaxBeats, v); else p.lengthSeconds = juce::jlimit(kMinSeconds, kMaxSeconds, v); commit("Length"); } refreshControls(); };
    bpmEd.onReturnKey = bpmEd.onFocusLost = [this] { const double v = bpmEd.getText().getDoubleValue(); if (v >= 20) { P().ownBpm = juce::jlimit(20.0, 999.0, v); commit("Tempo"); } refreshControls(); };
    lenB = button(juce::String("LENGTHS ") + juce::String::charToString((juce::juce_wchar) 0x25BE), th.cyan, [this] {
        juce::PopupMenu m; const bool sync = cP().tempoSync;
        const juce::StringArray beatsN { "1 beat", "2 beats", "1 bar", "2 bars", "4 bars", "8 bars", "16 bars", "32 bars", "64 bars", "128 bars" }, secsN { "1 s", "2 s", "4 s", "8 s", "15 s", "30 s", "1 min", "2 min", "5 min", "10 min" };
        for (int i = 0; i < 10; ++i) m.addItem(i + 1, (sync ? beatsN : secsN)[i]);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(lenB), [this](int id) {
            if (id <= 0) { return; } auto& p = P(); static const double beatsV[] = { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512 }; static const double secsV[] = { 1, 2, 4, 8, 15, 30, 60, 120, 300, 600 };
            if (p.tempoSync) p.lengthBeats = beatsV[id - 1]; else p.lengthSeconds = secsV[id - 1]; commit("Length"); refreshControls(); }); }, "Quick page lengths");
    keyCombo.addItemList(keyNames(), 1); keyCombo.setTooltip("Key");
    scaleCombo.addItemList(scaleNames(), 1); scaleCombo.setTooltip("Scale");
    keyCombo.onChange = [this] { if (keyCombo.getSelectedId() > 0 && keyCombo.getSelectedId() - 1 != cP().root) { P().root = keyCombo.getSelectedId() - 1; commit("Key"); canvas->invalidate(); } };
    scaleCombo.onChange = [this] { if (scaleCombo.getSelectedId() > 0 && scaleCombo.getSelectedId() - 1 != cP().scale) { P().scale = scaleCombo.getSelectedId() - 1; commit("Scale"); canvas->invalidate(); } };
    gridCombo.addItemList(gridNames(), 1); gridCombo.setTooltip("Grid for snap, moves, nudges and jumps: straight, triplet (T), quintuplet (1/5), septuplet (1/7) or free");
    gridCombo.onChange = [this] { if (gridCombo.getSelectedId() > 0 && gridCombo.getSelectedId() - 1 != cP().grid) { P().grid = gridCombo.getSelectedId() - 1; commit("Grid"); canvas->invalidate(); } };
    jumpCombo.addItemList({ "JUMP NOW", "JUMP BEAT", "JUMP BAR" }, 1); jumpCombo.setTooltip("Right-clicking the ruler plays from there: now, or on the next beat / bar so the page stays on the grid while your DAW plays");
    jumpCombo.onChange = [this] { if (jumpCombo.getSelectedId() > 0 && jumpCombo.getSelectedId() - 1 != cP().jumpQuant) { P().jumpQuant = jumpCombo.getSelectedId() - 1; commit("Jump"); } };
    bendCombo.addItemList({ "BEND 12", "BEND 24", "BEND 48" }, 1); bendCombo.setTooltip("MIDI export pitch-bend range (MPE doubles it: 24 / 48 / 96). Set your synth to match; glides wider than it are split into notes");
    bendCombo.onChange = [this] { if (bendCombo.getSelectedId() > 0 && bendCombo.getSelectedId() - 1 != cP().bendRange) { P().bendRange = bendCombo.getSelectedId() - 1; commit("Bend range"); } };
    fmCombo.setTooltip("FM: another line modulates this one (its sound bends this line's phase)");
    fmCombo.onChange = [this] { validateSel(); if (sel.empty()) return; const int id = fmCombo.getSelectedId() - 2; forTargets([&](Arc& a) { a.fmSource = (id >= 0 && id != a.id) ? id : -1; if (a.fmSource >= 0 && a.fmDepth <= 0) a.fmDepth = 0.3f; }); commit("FM source"); refreshArcEditors(); };
    filterCombo.addItemList(filterNames(), 1); filterCombo.setTooltip("The line's filter (draw its cutoff in FILTER CUTOFF)");
    filterCombo.onChange = [this] { const int f = filterCombo.getSelectedId() - 1; if (f < 0) return; forTargets([&](Arc& a) { a.filter = f; }); done("Filter"); };
    rateCombo.addItemList(rateNames(), 1); rateCombo.setTooltip("The line's own speed against the page: x3/4 against x1 = 3 against 4 (polyrhythm). With REPEAT the line loops at that rate");
    rateCombo.onChange = [this] { const int r = rateCombo.getSelectedId() - 1; if (r < 0) return; forTargets([&](Arc& a) { a.rate = rateValue(r); }); done("Rate"); canvas->invalidate(); };
    patchB = button(juce::String("PATCHES ") + juce::String::charToString((juce::juce_wchar) 0x25BE), th.pink, [this] {
        juce::PopupMenu m; std::map<juce::String, juce::PopupMenu> groups; const auto& P2 = factory::patches();
        for (int i = 0; i < (int) P2.size(); ++i) groups[P2[(size_t) i].group].addItem(i + 1, P2[(size_t) i].name);
        for (auto& g : groups) m.addSubMenu(g.first, g.second);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(patchB), [this](int r) { if (r <= 0) return; const auto& pt = factory::patches()[(size_t) r - 1];
            forTargets([&](Arc& a) { pt.apply(a); }); done(juce::String("Patch ") + pt.name); say(juce::String("Patch: ") + pt.name + (sel.empty() ? " (for new lines)" : "")); }); },
        "Line sound presets (wave, envelope, unison, sub, drive, filter); with nothing selected they set what new lines start as");
    for (auto* c : { &keyCombo, &scaleCombo, &fmCombo, &gridCombo, &jumpCombo, &bendCombo, &inkCombo, &filterCombo, &rateCombo }) { c->setWantsKeyboardFocus(false); owner.addAndMakeVisible(c); }
    for (auto* l : { &lenUnit, &bpmLabel }) { l->setFont(juce::FontOptions(10.0f, juce::Font::bold)); l->setJustificationType(juce::Justification::centredLeft); l->setInterceptsMouseClicks(false, false); owner.addAndMakeVisible(l); }
    masterK = knobs.add(new Knob("MASTER", 0, 1, 0.5, "Page output level")); tint(masterK->s, th.green); owner.addAndMakeVisible(masterK);
    masterK->onChange = [this](double v) { P().master = (float) v; live(); }; masterK->onEnd = [this] { commit("Master"); };
    gainK = knob("LEVEL", 0, 1, 0.8, th.pink, "The line's level", [](Arc& a, double v) { a.gain = (float) v; }, "Level");
    fmK = knob("FM DEPTH", 0, 1, 0, th.amber, "How hard the FM source bends this line", [](Arc& a, double v) { a.fmDepth = (float) v; }, "FM depth");
    uniK = knob("UNISON", 1, 7, 1, th.cyan, "Stacked voices (1..7)", [](Arc& a, double v) { a.unison = (int) std::round(v); }, "Unison"); uniK->s.setRange(1, 7, 1);
    detK = knob("DETUNE", 0, 1, 0.3, th.cyan, "Unison detune (up to +-50 cents)", [](Arc& a, double v) { a.detune = (float) v; }, "Detune");
    sprK = knob("SPREAD", 0, 1, 0.6, th.cyan, "Unison stereo spread", [](Arc& a, double v) { a.spread = (float) v; }, "Spread");
    subK = knob("SUB", 0, 1, 0, th.green, "Sine one octave down", [](Arc& a, double v) { a.sub = (float) v; }, "Sub");
    drvK = knob("DRIVE", 0, 1, 0, th.red, "Soft-clip drive (oversampled)", [](Arc& a, double v) { a.drive = (float) v; }, "Drive");
    resK = knob("RES", 0, 1, 0.2, th.amber, "Filter resonance", [](Arc& a, double v) { a.res = (float) v; }, "Resonance");

    seenEdit = host.editCount();
    setTool(Draw); applyTheme(); refreshControls();
}
ArcsPage::Impl::~Impl() { loopAlive.reset(); loopWav.deleteFile(); owner.setLookAndFeel(nullptr); }

void ArcsPage::Impl::afterEdit() { canvas->invalidate(); speed->repaint(); }

void ArcsPage::Impl::applyTheme() {
    lnf.apply(th);
    themeB->setButtonText(th.dark ? "LIGHT" : "DARK");
    for (auto* ed : { &lengthEd, &bpmEd }) { ed->setColour(juce::TextEditor::backgroundColourId, th.btn); ed->setColour(juce::TextEditor::textColourId, th.ink); ed->setColour(juce::TextEditor::outlineColourId, th.purple.withAlpha(0.4f)); ed->applyColourToAllText(th.ink); }
    for (auto* l : { &lenUnit, &bpmLabel }) l->setColour(juce::Label::textColourId, th.grey);
    for (auto* k : knobs) k->l.setColour(juce::Label::textColourId, th.grey);
    // tints made in any of the three looks move to this look's accent of the same role
    std::vector<std::pair<juce::Colour, juce::Colour>> pairs;
    for (auto lk : { ghosty::themes::Theme::Dark, ghosty::themes::Theme::Purple, ghosty::themes::Theme::Light }) { const auto o = makeTheme(lk);
        for (auto pr : { std::make_pair(o.purple, th.purple), std::make_pair(o.green, th.green), std::make_pair(o.pink, th.pink), std::make_pair(o.amber, th.amber), std::make_pair(o.cyan, th.cyan), std::make_pair(o.red, th.red), std::make_pair(o.grey, th.grey) }) pairs.push_back(pr); }
    auto retint = [&](juce::Component& c) { if (!c.getProperties().contains("tint")) return; const auto col = juce::Colour((juce::uint32) (juce::int64) c.getProperties()["tint"]); for (auto& pr : pairs) if (col == pr.first) { tint(c, pr.second); return; } };
    for (auto* b : buttons) retint(*b);
    for (auto* k : knobs) retint(k->s);
    owner.sendLookAndFeelChange(); canvas->invalidate(); owner.repaint();
}

void ArcsPage::Impl::setTool(int t) {
    tool = juce::jlimit(0, 4, t);
    for (int k = 0; k < 5; ++k) toolB[k]->setToggleState(k == tool, juce::dontSendNotification);
    const char* hints[] = { "DRAW: drag in either direction. Every line is a voice gliding along it", "LINE: click-drag a straight line (shift = flat)",
                            "CURVE: click-drag a riser; click CURVE again for EXP / S / LOG", "ERASE: rub out parts of lines (cutting one in the middle makes two)",
                            "SELECT: box / click / shift-click. Drag = move, alt-drag = copy, handles = stretch / rotate, arrows nudge" };
    say(hints[tool]); canvas->invalidate();
}

void ArcsPage::Impl::stamp(int which) {
    validateSel(); if (sel.empty()) { say("Select some lines first (SELECT tool)"); return; }
    auto& p = P();
    switch (which) {
        case 0: { int skipped = 0; const auto n = copyArcs(p, sel, -1.0f, &skipped); if (n.empty()) { say("The page is full (" + juce::String(kMaxArcs) + " lines)"); return; }
            sel = n; commit("Copy"); say(skipped ? "Copied " + juce::String((int) n.size()) + "; " + juce::String(skipped) + " did not fit (a page holds " + juce::String(kMaxArcs) + ")" : juce::String("Copied")); break; }
        case 1: mirrorArcs(p, sel); commit("Mirror"); break;
        case 2: invertArcs(p, sel); commit("Invert"); break;
        case 3: case 4: { const auto a = canvas->area(); const float aspect = (12.0f * a.getHeight() / (canvas->vy1 - canvas->vy0)) / (float) (a.getWidth() / (canvas->vx1 - canvas->vx0)); rotateArcs(p, sel, which == 3 ? -15.0f : 15.0f, aspect); commit("Rotate"); break; }
        case 5: quantiseArcs(p, sel, p.root, p.scale); commit("To key"); say("Snapped to " + keyNames()[p.root] + " " + scaleNames()[p.scale]); break;
        case 6: for (int id : sel) p.remove(id); sel.clear(); commit("Delete"); break;
        case 7: randomise(); return;
        default: break;
    }
    refreshArcEditors(); canvas->invalidate();
}

void ArcsPage::Impl::refreshControls() {
    const auto& p = cP(); auto& e = host.engine;
    const bool hostRun = e.hostRunning.load();
    playB->setToggleState(e.internalPlay.load() && !hostRun, juce::dontSendNotification); playB->setButtonText(hostRun ? "HOST" : (e.internalPlay.load() ? "PAUSE" : "PLAY")); playB->setAlpha(hostRun ? 0.5f : 1.0f);
    hostB->setToggleState(e.followHost.load(), juce::dontSendNotification);
    loopB->setToggleState(p.loopOn, juce::dontSendNotification); revB->setToggleState(p.reverse, juce::dontSendNotification);
    speedB->setToggleState(p.speedOn, juce::dontSendNotification); syncB->setToggleState(p.tempoSync, juce::dontSendNotification); autoB->setToggleState(p.autoGain, juce::dontSendNotification);
    quantB->setToggleState(p.quantise, juce::dontSendNotification); snapB->setToggleState(snap, juce::dontSendNotification); mpeB->setToggleState(mpe, juce::dontSendNotification);
    midiInB->setToggleState(p.midiIn, juce::dontSendNotification);
    for (int s = 0; s < kSlots; ++s) { bankB[(size_t) s]->setToggleState(s == host.slot(), juce::dontSendNotification); bankB[(size_t) s]->setAlpha(host.slotEmpty(s) && s != host.slot() ? 0.8f : 1.0f); }
    if (!lengthEd.hasKeyboardFocus(true)) lengthEd.setText(p.tempoSync ? juce::String(p.lengthBeats, std::abs(p.lengthBeats - std::floor(p.lengthBeats)) < 1.0e-9 ? 0 : 2) : juce::String(p.lengthSeconds, 2), false);
    const double bars = p.tempoSync ? p.lengthBeats / 4.0 : 0.0;
    lenUnit.setText(p.tempoSync ? "BEATS" + (bars >= 1 ? " (" + juce::String(bars, std::abs(bars - std::floor(bars)) < 1.0e-9 ? 0 : 1) + " bars)" : juce::String()) : "SECONDS", juce::dontSendNotification);
    if (host.hostHasTempo()) { bpmEd.setVisible(false); bpmLabel.setText("HOST " + juce::String(host.bpm(), 1) + " BPM", juce::dontSendNotification); }
    else { bpmEd.setVisible(true); if (!bpmEd.hasKeyboardFocus(true)) bpmEd.setText(juce::String(p.ownBpm, 1), false); bpmLabel.setText("BPM", juce::dontSendNotification); }
    placeBpmLabel();
    keyCombo.setSelectedId(p.root + 1, juce::dontSendNotification); scaleCombo.setSelectedId(p.scale + 1, juce::dontSendNotification);
    gridCombo.setSelectedId(p.grid + 1, juce::dontSendNotification); jumpCombo.setSelectedId(p.jumpQuant + 1, juce::dontSendNotification);
    bendCombo.setSelectedId(p.bendRange + 1, juce::dontSendNotification); inkCombo.setSelectedId(p.ink + 1, juce::dontSendNotification);
    masterK->s.setValue(p.master, juce::dontSendNotification);
    undoB->setEnabled(host.canUndo()); redoB->setEnabled(host.canRedo());
    refreshArcEditors(); canvas->invalidate(); speed->repaint(); owner.repaint();
}

void ArcsPage::Impl::refreshArcEditors() {
    validateSel();
    const Arc* a = sel.empty() ? &brush : cP().find(sel.front());
    if (!a) a = &brush;
    for (int k = 0; k < kWaveLen; ++k) waveEd->v[(size_t) k] = a->wave[(size_t) k];
    waveEd->repaint(); envEd->fromPts(a->env, 1.0f); panEd->fromPts(a->pan, 0.0f); cutEd->fromPts(a->cutoff, 0.7f); cutEd->setEnabled(a->filter != FiltOff);
    gainK->s.setValue(a->gain, juce::dontSendNotification); fmK->s.setValue(a->fmDepth, juce::dontSendNotification);
    uniK->s.setValue(a->unison, juce::dontSendNotification); detK->s.setValue(a->detune, juce::dontSendNotification); sprK->s.setValue(a->spread, juce::dontSendNotification);
    subK->s.setValue(a->sub, juce::dontSendNotification); drvK->s.setValue(a->drive, juce::dontSendNotification); resK->s.setValue(a->res, juce::dontSendNotification);
    filterCombo.setSelectedId(a->filter + 1, juce::dontSendNotification); rateCombo.setSelectedId(rateIndex(a->rate) + 1, juce::dontSendNotification);
    trackB->setToggleState(a->track, juce::dontSendNotification); repeatB->setToggleState(a->repeat, juce::dontSendNotification);
    const bool any = !sel.empty();
    for (auto* b : needSel) b->setEnabled(any);
    muteB->setEnabled(any); modB->setEnabled(any); fmCombo.setEnabled(any); fmK->setEnabled(any);
    muteB->setToggleState(any && a->mute, juce::dontSendNotification); modB->setToggleState(any && a->modOnly, juce::dontSendNotification);
    fmCombo.clear(juce::dontSendNotification); fmCombo.addItem("FM: none", 1);
    for (const auto& o : cP().arcs) if (!any || o.id != a->id) fmCombo.addItem("FM from line " + juce::String(o.id), o.id + 2);
    fmCombo.setSelectedId(any && a->fmSource >= 0 ? a->fmSource + 2 : 1, juce::dontSendNotification);
    undoB->setEnabled(host.canUndo()); redoB->setEnabled(host.canRedo());
    owner.repaint();
}

// ---------------------------------------------------------------- layout (designed at 1200 x 790)
void ArcsPage::Impl::layout() {
    const int W = owner.getWidth(), H = owner.getHeight();
    const int top = 88, speedH = 42, cardH = 212;
    const int by = H - 30 - cardH;                      // bottom cards
    const int canvasH = juce::jmax(160, by - 6 - speedH - 4 - top);
    canvas->setBounds(16, top, W - 32, canvasH);
    speed->setBounds(16, top + canvasH + 4, W - 32, speedH);
    // toolbar
    int x = 16; const int ty = 52;
    auto place = [&](juce::Component* c, int w, int y, int h = 28) { c->setBounds(x, y, w, h); x += w + 5; };
    const int toolW[] = { 52, 48, 78, 56, 60 };
    for (int t = 0; t < 5; ++t) place(toolB[t], toolW[t], ty);
    place(&inkCombo, 84, ty);
    x += 6; const int stampW[] = { 48, 56, 54, 44, 44, 54, 60, 56 }; for (size_t i = 0; i < needSel.size(); ++i) place(needSel[i], stampW[i], ty);
    x += 6; place(undoB, 48, ty); place(redoB, 48, ty);
    x += 6;
    for (auto* b : buttons) if (b->getButtonText() == "NEW" || b->getButtonText() == "SAVE" || b->getButtonText() == "LOAD") place(b, 46, ty);
    for (auto* b : buttons) if (b->getButtonText() == "BACK") b->setBounds(W - 16 - 56, 12, 56, 26);
    themeB->setBounds(W - 16 - 56 - 5 - 58, 12, 58, 26);
    factoryB->setBounds(W - 16 - 56 - 5 - 58 - 5 - 76, 12, 76, 26);
    // transport card (left)
    const int lx = 28; int y = by + 20; x = lx;
    place(playB, 60, y); place(stopB, 52, y); place(hostB, 52, y); place(loopB, 52, y); place(revB, 72, y); place(speedB, 58, y); place(autoB, 52, y);
    x = lx; y += 34;
    place(syncB, 50, y); place(&lengthEd, 54, y); place(lenB, 84, y); lenUnit.setBounds(x, y, 86, 28); x += 88;
    place(&bpmEd, 50, y); placeBpmLabel();
    x = lx; y += 34;
    place(&keyCombo, 52, y); place(&scaleCombo, 112, y); place(quantB, 78, y); place(snapB, 50, y); place(&gridCombo, 100, y);
    x = lx; y += 34;
    for (int s = 0; s < kSlots; ++s) { bankB[(size_t) s]->setBounds(x, y, 23, 28); x += 25; } x += 4;
    place(midiInB, 64, y); place(&jumpCombo, 98, y);
    x = lx; y += 34;
    place(midiDrag.get(), 92, y); place(mpeB, 44, y); place(&bendCombo, 82, y); place(audioDrag.get(), 116, y);
    masterK->setBounds(lx + 400, by + 120, 52, 62);
    // line card (right)
    const int ax = 514, ey = by + 18, edH = 90;
    waveEd->setBounds(ax, ey, 192, edH); envEd->setBounds(ax + 198, ey, 168, edH);
    panEd->setBounds(ax + 372, ey, 150, edH / 2 - 2); cutEd->setBounds(ax + 372, ey + edH / 2 + 2, 150, edH / 2 - 2);
    int wx = ax, ex = ax + 198, px2 = ax + 372;
    for (auto* b : buttons) {
        const auto row = b->getProperties()["row"].toString();
        if (row == "wave") { const int w = b->getButtonText() == "NOISE" ? 42 : 24; b->setBounds(wx, ey + edH + 4, w, 22); wx += w + 1; }
        else if (row == "env") { b->setBounds(ex, ey + edH + 4, 33, 22); ex += 34; }
        else if (row == "pan") { b->setBounds(px2, ey + edH + 4, 37, 22); px2 += 38; }
    }
    const int ky = ey + edH + 30; int kx = ax;
    for (auto* k : { uniK, detK, sprK, subK, drvK, resK }) { k->setBounds(kx, ky, 46, 56); kx += 48; }
    filterCombo.setBounds(kx + 2, ky + 2, 100, 24); trackB->setBounds(kx + 2, ky + 30, 48, 22);
    rateCombo.setBounds(kx + 106, ky + 2, 66, 24); repeatB->setBounds(kx + 52, ky + 30, 56, 22);
    int tx = kx + 112; for (auto* b : buttons) if (b->getProperties()["row"].toString() == "tools") { b->setBounds(tx, ky + 30, 56, 22); tx += 58; }
    const int rx = ax + 530;
    gainK->setBounds(rx, ey - 4, 52, 58); fmK->setBounds(rx + 56, ey - 4, 58, 58);
    fmCombo.setBounds(rx, ey + 58, 114, 24);
    muteB->setBounds(rx, ey + 86, 56, 22); modB->setBounds(rx + 58, ey + 86, 56, 22);
    patchB->setBounds(rx, ey + 114, 114, 24);
}

// =====================================================================================================================
ArcsPage::ArcsPage(ArcsHost& h, bool ownTips) : host(h) {
    setWantsKeyboardFocus(true); setOpaque(true);
    impl = std::make_unique<Impl>(*this, h, ownTips);
    setSize(1200, 790);
    impl->canvas->fit();
    startTimerHz(30);
}
ArcsPage::~ArcsPage() { stopTimer(); impl.reset(); }

void ArcsPage::setDark(bool d) { setLook(d ? ghosty::themes::Theme::Dark : ghosty::themes::Theme::Light); }
void ArcsPage::setLook(ghosty::themes::Theme lk) { if (impl->th.look == lk) return; impl->th = makeTheme(lk); host.dark = impl->th.dark; impl->applyTheme(); if (onThemeChanged) onThemeChanged(impl->th.dark); }
bool ArcsPage::loopWavReady() const { return impl->loopWavReady(); }
void ArcsPage::dragLoopOut(bool midi, std::function<void(const juce::StringArray&)> drag) { impl->performDragForTests = std::move(drag); impl->dragLoop(midi, this); impl->performDragForTests = nullptr; }
void ArcsPage::setThemeButtonVisible(bool v) { if (impl->themeB) impl->themeB->setVisible(v); }
bool ArcsPage::isDark() const { return impl->th.dark; }
void ArcsPage::selectTool(int t) { impl->setTool(t); }
void ArcsPage::setSelection(const std::vector<int>& ids) { impl->sel = ids; impl->validateSel(); impl->refreshArcEditors(); impl->canvas->invalidate(); }
std::vector<int> ArcsPage::selection() const { return impl->sel; }
void ArcsPage::fitView() { impl->canvas->fit(); }
juce::String ArcsPage::status() const { return impl->statusText; }
void ArcsPage::refresh() { impl->seenEdit = host.editCount(); impl->validateSel(); impl->refreshControls(); }
void ArcsPage::resized() { impl->layout(); }
void ArcsPage::setOwnsTooltips(bool own) { if (own == (impl->tips != nullptr)) return; if (own) impl->tips = std::make_unique<juce::TooltipWindow>(this, 700); else impl->tips.reset(); }
bool ArcsPage::ownsTooltips() const { return impl->tips != nullptr; }
bool ArcsPage::testExport(bool audio, const juce::File& midiTarget) {
    auto& I = *impl; if (audio) return I.startAudioRender(this);
    if (!I.exportAllowed(this, "spec.arcs.midi-save")) { return false; } return host.writeMidi(midiTarget, I.mpe).existsAsFile();
}
static juce::MouseEvent fakeEvent(juce::Component* c, juce::Point<float> pos, juce::Point<float> downPos, juce::ModifierKeys mods, bool dragged) {
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 1.0f, 0, 0, 0, 0, c, c, now, downPos, now, 1, dragged);
}
void ArcsPage::testGesture(const std::vector<std::pair<float, float>>& pts, juce::ModifierKeys extra) {
    if (pts.empty()) { return; } auto& c = *impl->canvas;
    std::vector<juce::Point<float>> px; for (auto& q : pts) px.push_back({ c.px(q.first), c.py(q.second) });
    const auto mods = extra.withFlags(juce::ModifierKeys::leftButtonModifier);
    c.mouseDown(fakeEvent(&c, px.front(), px.front(), mods, false));
    for (size_t i = 1; i < px.size(); ++i) c.mouseDrag(fakeEvent(&c, px[i], px.front(), mods, true));
    c.mouseUp(fakeEvent(&c, px.back(), px.front(), extra, px.size() > 1));
}
void ArcsPage::testRuler(float fromPos, float toPos, bool right) {
    auto& c = *impl->canvas; const juce::Point<float> a { c.px(fromPos), 8.0f }, b { c.px(toPos), 8.0f };
    const juce::ModifierKeys mods(right ? juce::ModifierKeys::rightButtonModifier : juce::ModifierKeys::leftButtonModifier);
    c.mouseDown(fakeEvent(&c, a, a, mods, false));
    if (a != b) for (int k = 1; k <= 8; ++k) c.mouseDrag(fakeEvent(&c, a + (b - a) * (k / 8.0f), a, mods, true));
    c.mouseUp(fakeEvent(&c, b, a, {}, a != b));
}
void ArcsPage::mouseDown(const juce::MouseEvent&) { grabKeyboardFocus(); }

void ArcsPage::paint(juce::Graphics& g) {
    const auto& T = impl->th; const float W = (float) getWidth(), H = (float) getHeight();
    juce::ColourGradient bg(T.bg0, 0, 0, T.bg1, 0, H, false); g.setGradientFill(bg); g.fillAll();
    g.setGradientFill(juce::ColourGradient(T.pink.withAlpha(T.dark ? 0.10f : 0.06f), W * 0.12f, -40.0f, T.pink.withAlpha(0.0f), W * 0.12f, 220.0f, true)); g.fillRect(getLocalBounds());
    g.setGradientFill(juce::ColourGradient(T.cyan.withAlpha(T.dark ? 0.09f : 0.05f), W * 0.88f, H + 40.0f, T.cyan.withAlpha(0.0f), W * 0.88f, H - 300.0f, true)); g.fillRect(getLocalBounds());
    g.setGradientFill(juce::ColourGradient(T.ink, 18, 0, T.pink, 120, 0, false)); g.setFont(juce::FontOptions(23.0f, juce::Font::bold)); g.drawText("ARCS", 18, 8, 100, 26, juce::Justification::centredLeft);
    g.setColour(T.cyan.withAlpha(0.95f)); g.setFont(juce::FontOptions(9.0f, juce::Font::bold)); g.drawText("GHOSTY SPECTRUM  -  DRAW THE MUSIC, EVERY LINE IS A VOICE", 20, 33, 360, 12, juce::Justification::centredLeft);
    const auto& p = host.page();
    g.setColour(T.grey); g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
    const int note = host.engine.midiNote.load();
    g.drawText("PAGE " + juce::String::charToString((juce::juce_wchar) ('A' + host.slot())) + "   " + juce::String((int) p.arcs.size()) + " lines   " + juce::String(host.engine.activeVoices.load()) + " sounding   "
               + (impl->sel.empty() ? juce::String() : juce::String((int) impl->sel.size()) + " selected   ") + (p.midiIn ? (note >= 0 ? "MIDI " + noteName(note) : juce::String("MIDI IN: hold a note")) : juce::String()),
               390, 14, 400, 20, juce::Justification::centredLeft);
    auto m = juce::Rectangle<float>((float) impl->factoryB->getX() - 132, 16, 120, 18);
    g.setColour(T.grey); g.setFont(juce::FontOptions(9.0f, juce::Font::bold)); g.drawText("OUT", m.withWidth(26), juce::Justification::centredLeft);
    auto bar = m.withTrimmedLeft(28).reduced(0, 4); g.setColour(T.card2); g.fillRoundedRectangle(bar, 3);
    const float lv = juce::jmax(host.engine.meterL.load(), host.engine.meterR.load()); const float db = juce::jlimit(0.0f, 1.0f, (juce::Decibels::gainToDecibels(lv, -60.0f) + 60.0f) / 60.0f);
    const bool limiting = host.engine.kneeActive.load();
    g.setColour(limiting ? T.amber : T.green); g.fillRoundedRectangle(bar.withWidth(bar.getWidth() * db), 3);
    if (limiting) { g.setColour(T.amber); g.setFont(juce::FontOptions(8.5f, juce::Font::bold)); g.drawText("LIMIT", bar.translated(0, 11).toNearestInt(), juce::Justification::centredRight); }
    const int by = impl->speed->getBottom() + 6;
    auto card = [&](juce::Rectangle<int> r, juce::Colour edge, const juce::String& title) {
        g.setColour(T.card); g.fillRoundedRectangle(r.toFloat(), 12.0f); g.setColour(edge.withAlpha(0.3f)); g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 12.0f, 1.0f);
        g.setColour(T.grey); g.setFont(juce::FontOptions(10.5f, juce::Font::bold)); g.drawText(title, r.getX() + 12, r.getY() + 3, 300, 14, juce::Justification::centredLeft);
    };
    card({ 16, by, 478, (int) H - by - 28 }, T.green, "TRANSPORT  /  PAGE");
    const Arc* f = impl->sel.empty() ? nullptr : p.find(impl->sel.front());
    card({ 502, by, (int) W - 518, (int) H - by - 28 }, T.pink, f ? (impl->sel.size() > 1 ? juce::String((int) impl->sel.size()) + " LINES (edits go to all of them)" : "LINE " + juce::String(f->id)) : "NEW LINES START AS");
    const bool fresh = juce::Time::getMillisecondCounterHiRes() - impl->statusTime < 6000;
    g.setColour(fresh ? T.ink.withAlpha(0.85f) : T.grey.withAlpha(0.8f)); g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
    g.drawText(fresh ? impl->statusText : juce::String("Wheel = zoom time, shift+wheel = zoom pitch, middle-drag = scroll. Ruler: right-click = play from here, right-drag = loop. ctrl+C / ctrl+V copy lines between pages."), 18, (int) H - 24, (int) W - 36, 20, juce::Justification::centredLeft);
}

bool ArcsPage::keyPressed(const juce::KeyPress& k) {
    auto& I = *impl; const auto mods = k.getModifiers(); const int c = k.getKeyCode();
    if (mods.isCommandDown() && (c == 'Z' || c == 'z')) { if (mods.isShiftDown()) I.redo(); else I.undo(); return true; }
    if (mods.isCommandDown() && (c == 'Y' || c == 'y')) { I.redo(); return true; }
    if (mods.isCommandDown() && (c == 'A' || c == 'a')) { I.sel.clear(); for (auto& a : host.page().arcs) I.sel.push_back(a.id); I.setTool(Select); I.refreshArcEditors(); return true; }
    if (mods.isCommandDown() && (c == 'D' || c == 'd')) { I.stamp(0); return true; }
    if (mods.isCommandDown() && (c == 'C' || c == 'c')) { I.copySel(false); return true; }
    if (mods.isCommandDown() && (c == 'X' || c == 'x')) { I.copySel(true); return true; }
    if (mods.isCommandDown() && (c == 'V' || c == 'v')) { I.paste(); return true; }
    if (c == juce::KeyPress::deleteKey || c == juce::KeyPress::backspaceKey) { I.stamp(6); return true; }
    if (c == juce::KeyPress::spaceKey) { I.playB->onClick(); return true; }
    if (c == juce::KeyPress::escapeKey) { I.sel.clear(); I.refreshArcEditors(); I.canvas->invalidate(); return true; }
    if (c == juce::KeyPress::leftKey || c == juce::KeyPress::rightKey) { double st = I.gridStepBeatsNow(); if (st <= 0) st = 0.25; if (mods.isShiftDown()) st = 4; I.nudge((float) (c == juce::KeyPress::leftKey ? -st : st), 0); return true; }
    if (c == juce::KeyPress::upKey || c == juce::KeyPress::downKey) { const float s = mods.isShiftDown() ? 12.0f : 1.0f; I.nudge(0, c == juce::KeyPress::upKey ? s : -s); return true; }
    if (!mods.isAnyModifierKeyDown()) switch (juce::CharacterFunctions::toUpperCase(k.getTextCharacter())) {
        case 'D': I.setTool(Draw); return true; case 'L': I.setTool(Line); return true; case 'C': I.setTool(Curve); return true; case 'E': I.setTool(Erase); return true; case 'S': I.setTool(Select); return true;
        case 'M': I.stamp(1); return true; case 'I': I.stamp(2); return true; default: break;
    }
    return false;
}

void ArcsPage::timerCallback() {
    auto& I = *impl;
    if (I.dirty) { I.dirty = false; host.changed(); I.seenEdit = host.editCount(); }
    I.tickLoopRender();
    if (host.editCount() != I.seenEdit) { I.seenEdit = host.editCount(); I.validateSel(); I.refreshControls(); }   // changed from outside (state load, MIDI page switch)
    { const auto m = host.takeMessage(); if (m.isNotEmpty()) I.say(m); }
    const bool playing = host.engine.isPlaying.load();
    I.canvas->repaint(); if (playing) I.speed->repaint();
    const bool hostRun = host.engine.hostRunning.load();
    if (I.playB->getButtonText() != (hostRun ? "HOST" : (host.engine.internalPlay.load() ? "PAUSE" : "PLAY"))) I.refreshControls();
    const bool showHostTempo = host.hostHasTempo(); if (showHostTempo == I.bpmEd.isVisible()) I.refreshControls();
    if (host.rendering() || I.audioDrag->isMouseOver()) I.audioDrag->repaint();
    const bool r = host.rendering(); if (I.wasRendering && !r) { I.audioDrag->repaint(); I.say(host.renderReady() ? "Rendered: drag AUDIO into your DAW" : (host.renderError().isNotEmpty() ? host.renderError() : juce::String("Render stopped"))); } I.wasRendering = r;
    repaint(380, 8, 420, 30); repaint(I.factoryB->getX() - 140, 10, 140, 34);
}

} // namespace arcs
