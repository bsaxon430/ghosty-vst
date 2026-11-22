#include "BlockStepEditor.h"
#include "WebZoom.h"
#include "BlockStepSize.h"
#include "WebViewTeardown.h"
#include "BlockStepBin.h"

namespace {
const juce::Colour kBg (0xff05040a), kStripBg (0xff0b0a13), kInk (0xfff5f3ff), kGrey (0xff9ca3af), kGreen (0xff00ff88), kPurple (0xffa855f7);
}

// host test only ($BLOCKSTEP_STRIPCHECK set by tests/HostTest.cpp): what the page did, in <that file>.page.log
static void testLog(const juce::String& t) {
    const char* path = std::getenv("BLOCKSTEP_STRIPCHECK"); if (path == nullptr || *path == 0) return;
    juce::File(juce::String(path) + ".page.log").appendText(t + "\n");
}

juce::WebBrowserComponent::Options BlockStepEditor::options(BlockStepEditor& e) {
    auto o = juce::WebBrowserComponent::Options {}
        .withNativeIntegrationEnabled()
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withResourceProvider([&e](const juce::String& url) { return e.resource(url); })
        .withEventListener("bs_log", [](juce::var v) { testLog("page: " + v["text"].toString()); })
        .withEventListener("bs_ready", [&e](juce::var) { testLog("page ready"); e.pageReady = true; e.sendRestore(); e.banks.send();
            auto* g = new juce::DynamicObject(); g->setProperty("ready", true); e.web.emitEventIfBrowserIsVisible("bs_sndgrip", juce::var(g)); })   // this build has ⇣
        .withEventListener("bs_state", [&e](juce::var v) { const auto js = v["json"].toString(); e.proc.setUiState(js);
            const bool off = js.contains("\"gho\":true"); if (off != e.ghostsOff) { e.ghostsOff = off; e.updGho(); } })
        .withEventListener("bs_audition", [&e](juce::var v) { e.proc.getEngine().audition(bstep::cellFromVar(v["cell"])); })
        .withEventListener("bs_transport", [&e](juce::var v) { e.proc.getEngine().setInternalPlay((bool) v["play"]); })
        .withEventListener("bs_dragch", [&e](juce::var v) { e.dragCh = (int) v["ch"]; e.dragMask = 0;
            if (auto* a = v["chs"].getArray(); a != nullptr && a->size() > 1) { for (auto& x : *a) if ((int) x >= 0 && (int) x < 32) e.dragMask |= 1u << (int) x; e.dragCh = -3; }
            e.updTarget(); })
        .withEventListener("bs_seek", [&e](juce::var v) { e.proc.getEngine().seek((int) v["step"]); })
        .withEventListener("bs_sndhover", [&e](juce::var v) { e.sndHover(v); });
    o = e.banks.listen(std::move(o));   // MY BANKS
   #if JUCE_WINDOWS
    o = o.withBackend(juce::WebBrowserComponent::Options::Backend::webview2)
         .withWinWebView2Options(juce::WebBrowserComponent::Options::WinWebView2 {}
             .withUserDataFolder(juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("BlockStepWebView"))
             .withBackgroundColour(kBg));
   #endif
    return o;
}

BlockStepEditor::BlockStepEditor(BlockStepProcessor& p)
    : juce::AudioProcessorEditor(&p), proc(p), web(options(*this)) {
    addAndMakeVisible(drag); addAndMakeVisible(dragWav);
    target.setTooltip("What the drag gives you: ALL = the whole beat, CH = just that channel (click to step through, or open a channel's panel in the page)");
    target.setColour(juce::TextButton::buttonColourId, juce::Colour(0x2260a5fa)); target.setColour(juce::TextButton::textColourOffId, kInk);
    target.onClick = [this] { auto P = proc.getUiPattern(); const int n = P ? P->ch : 1; dragCh = dragCh == -1 || dragCh == -3 ? -2 : dragCh == -2 ? 0 : (dragCh + 1 >= n ? -1 : dragCh + 1); updTarget(); };   // ALL -> PAGE -> CH1..n
    addAndMakeVisible(target); updTarget();
    byDrum.setButtonText(juce::String::fromUTF8("\xe2\x87\xa3 BY DRUM"));
    byDrum.setTooltip("Drag out every kick, every snare, every hat... on its own, as WAV or MIDI, whatever channel the hits are in");
    byDrum.setColour(juce::TextButton::buttonColourId, juce::Colour(0x2200ff88)); byDrum.setColour(juce::TextButton::textColourOffId, kInk);
    byDrum.onClick = [this] { if (! pageReady) return; auto* o = new juce::DynamicObject(); o->setProperty("open", true); web.emitEventIfBrowserIsVisible("bs_xdrag", juce::var(o)); };
    addAndMakeVisible(byDrum);
    status.setFont(juce::FontOptions(12.5f, juce::Font::bold));
    status.setColour(juce::Label::textColourId, kGrey);
    status.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(status);
    unlock = std::make_unique<ghosty::UnlockButton>(proc.license);
    unlock->setColour(juce::TextButton::buttonColourId, juce::Colour(0x33a855f7));
    unlock->setColour(juce::TextButton::textColourOffId, kInk);
    addChildComponent(*unlock);
    ghostsOff = false;   // (owner 2026-11-13) every window opens with GHOST ANIMATION: ON
    ghoBtn.setTooltip("GHOST ANIMATION: the swirling ghosts and the falling glyphs behind everything. OFF: they stand still (lighter on your Mac)");
    ghoBtn.onClick = [this] { ghostsOff = ! ghostsOff; updGho();
        if (pageReady) { auto* o = new juce::DynamicObject(); o->setProperty("off", ghostsOff); web.emitEventIfBrowserIsVisible("bs_ghosts", juce::var(o)); } };
    addAndMakeVisible(ghoBtn); updGho();
    infoBtn.setButtonText("i"); infoBtn.setTooltip("About / Credits: the code, data and sounds by other people in this plug-in, and their licences");
    infoBtn.setColour(juce::TextButton::buttonColourId, juce::Colour(0x33a855f7)); infoBtn.setColour(juce::TextButton::textColourOffId, kInk);
    infoBtn.onClick = [] { int n = 0; const char* t = (const char*) BlockStepKit::resource("CREDITS.txt", n);
        juce::AlertWindow::showAsync(juce::MessageBoxOptions().withIconType(juce::MessageBoxIconType::InfoIcon).withTitle("About / Credits")
            .withMessage(t != nullptr && n > 0 ? juce::String::fromUTF8(t, n) : juce::String("See CREDITS.txt in the download.")).withButton("OK"), nullptr); };
    addAndMakeVisible(infoBtn);
    bypassBtn = std::make_unique<ghosty::BypassButton>(*proc.bypassParam); bypassBtn->setDark(true); addAndMakeVisible(*bypassBtn);
    unlock->refresh();
    addAndMakeVisible(web);
    addAndMakeVisible(edges);
    sgrip.make = [this] { if (! proc.license.isUnlocked()) { if (unlock) unlock->nudge(); return juce::File(); }   // demo: export is off
        if (sgripSnd.hasProperty("xi")) return writeInst((int) sgripSnd["xi"], sgripSnd["xk"].toString() != "midi");   // ⇣ BY DRUM
        const juce::File f = writeSound(sgripSnd);
        if (! f.existsAsFile() && pageReady) { auto* o = new juce::DynamicObject(); o->setProperty("text", "Could not make the WAV for that sound"); web.emitEventIfBrowserIsVisible("bs_toast", juce::var(o)); }
        return f; };
    sgrip.drag = [this](const juce::File& f) { juce::DragAndDropContainer::performExternalDragDropOfFiles({ f.getFullPathName() }, false, &sgrip); };
    auto tell = [this](const char* what) { if (! pageReady) return; auto* o = new juce::DynamicObject(); o->setProperty(what, true); web.emitEventIfBrowserIsVisible("bs_sndgrip", juce::var(o)); };
    sgrip.tapped = [tell] { tell("tap"); };
    sgrip.wheel = [tell] { tell("gone"); };   // the wheel scrolls the page: the grip steps aside until the mouse moves again
    testLog("editor: loading " + juce::WebBrowserComponent::getResourceProviderRoot());
    web.goToURL(juce::WebBrowserComponent::getResourceProviderRoot());
    setResizable(true, true);
    // the FITTING size (BlockStepSize.h, owner 2026-11-06): every new window opens at about 1200 x 760 (never wider than
    // 1240 or taller than 780), so FL's "fit to screen" button is never needed. A project's own saved size reopens.
    // The page is laid out at one CSS size (kPageW x kPageH) and zoomed to the window (WebZoom, FabFilter style), so its
    // layout is the same at every window size: nothing can move into anything else.
    namespace ws = bstep::winsize;
    zoomRefW = ws::kPageW; zoomRefH = ws::kPageH;
    int sw = 0, sh = 0;
    if (const auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()) { sw = d->userArea.getWidth(); sh = d->userArea.getHeight(); }
    const auto sz = ws::initial(sw, sh, proc.uiW, proc.uiH);
    setResizeLimits(ws::kMinW, ws::kMinH, ws::kMaxW, ws::kMaxH);
    // aspectlock (owner 2026-11-01): dragging any edge scales the whole window in proportion
    if (auto* c = getConstrainer()) c->setFixedAspectRatio((double) ws::kDefW / (double) ws::kDefH);
    setSize(sz.w, sz.h);
    startTimerHz(30);
}

BlockStepEditor::~BlockStepEditor() { stopTimer(); sgrip.removeFromDesktop(); testLog("editor: closed"); ghosty::webview::detach(web); }   // (detach: closing the window while the page loads must not crash the DAW)

void BlockStepEditor::paint(juce::Graphics& g) {
    g.fillAll(kBg);
    g.setColour(kStripBg); g.fillRect(0, 0, getWidth(), kStrip);
    g.setGradientFill(juce::ColourGradient(kPurple.withAlpha(0.0f), 0, (float) kStrip - 1, kPurple.withAlpha(0.7f), getWidth() * 0.5f, (float) kStrip - 1, false));
    g.fillRect(0, kStrip - 1, getWidth(), 1);
}

void BlockStepEditor::resized() {
    proc.uiW = getWidth(); proc.uiH = getHeight();
    auto r = getLocalBounds();
    auto top = r.removeFromTop(kStrip).reduced(6, 4);
    drag.setBounds(top.removeFromLeft(84)); top.removeFromLeft(4); dragWav.setBounds(top.removeFromLeft(84)); top.removeFromLeft(4); target.setBounds(top.removeFromLeft(62)); top.removeFromLeft(4); byDrum.setBounds(top.removeFromLeft(92));
    bypassBtn->setBounds(top.removeFromRight(96)); top.removeFromRight(6);
    if (unlock->isVisible()) { unlock->setBounds(top.removeFromRight(92)); top.removeFromRight(6); }
    { const bool sh = top.getWidth() < 178 + 6 + 34 + 60; if (sh != ghoShort) { ghoShort = sh; updGho(); } }   // (a narrow window: the short label, so nothing overlaps)
    ghoBtn.setBounds(top.removeFromRight(ghoShort ? 112 : 178)); top.removeFromRight(6);
    infoBtn.setBounds(top.removeFromRight(28)); top.removeFromRight(6);   // About / Credits   // left of LICENSED
    status.setBounds(top);
    {   // lockedlayout (owner 2026-11-13): the page ALWAYS keeps its one desktop layout, at any window size or shape. It sits
        // centred at the page's own shape (kPageW x kPageH) inside the window, so its CSS size is always kPageW x kPageH and it
        // can never reach the wide (>= 1380 px) layout, even when the DAW stretches the window out of proportion.
        const auto area = r.withTrimmedLeft(ghosty::EdgeResizer::kEdge).withTrimmedRight(ghosty::EdgeResizer::kEdge).withTrimmedBottom(ghosty::EdgeResizer::kEdge);   // (a thin frame the page never covers: the edge grips)
        const int pw = zoomRefW > 0 ? zoomRefW : bstep::winsize::kPageW, ph = zoomRefH > 0 ? zoomRefH : bstep::winsize::kPageH;
        const double s = juce::jmin(area.getWidth() / (double) pw, area.getHeight() / (double) ph);
        const int w = juce::jmax(1, (int) std::floor(pw * s)), h = juce::jmax(1, (int) std::floor(ph * s));
        web.setBounds(area.getX() + (area.getWidth() - w) / 2, area.getY() + (area.getHeight() - h) / 2, w, h);
    }
    edges.setBounds(getLocalBounds()); edges.toFront(false);
    stripCheck();
    sgrip.setVisible(false);   // (the page sends where ⇣ is again on the next mouse move)
}

// host test only ($BLOCKSTEP_STRIPCHECK set by tests/HostTest.cpp): the strip's controls are inside the window and no two
// overlap. Writes "ok W x H" or the problems to that file. Never runs in a DAW.
void BlockStepEditor::stripCheck() {
    const char* path = std::getenv("BLOCKSTEP_STRIPCHECK"); if (path == nullptr || *path == 0) return;
    juce::StringArray bad; juce::Array<juce::Component*> kids;
    for (auto* c : { (juce::Component*) &drag, (juce::Component*) &dragWav, (juce::Component*) &target, (juce::Component*) &byDrum, (juce::Component*) &status, (juce::Component*) unlock.get(), (juce::Component*) &ghoBtn, (juce::Component*) &infoBtn, (juce::Component*) bypassBtn.get() })
        if (c != nullptr && c->isVisible()) kids.add(c);
    const auto strip = getLocalBounds().removeFromTop(kStrip);
    for (auto* c : kids) { if (c->getWidth() < 8 || c->getHeight() < 8) bad.add("too small: " + c->getBounds().toString()); if (! strip.contains(c->getBounds())) bad.add("outside: " + c->getBounds().toString()); }
    for (int i = 0; i < kids.size(); ++i) for (int j = i + 1; j < kids.size(); ++j)
        if (kids[i]->getBounds().intersects(kids[j]->getBounds())) bad.add("overlap: " + kids[i]->getBounds().toString() + " x " + kids[j]->getBounds().toString());
    if (web.getBounds().intersects(strip)) bad.add("the page covers the strip");
    juce::File(juce::String(path)).replaceWithText(bad.isEmpty() ? "ok " + juce::String(getWidth()) + " x " + juce::String(getHeight()) : bad.joinIntoString("; "));
}

std::optional<juce::WebBrowserComponent::Resource> BlockStepEditor::resource(const juce::String& url) const {
    const juce::String path = url.upToFirstOccurrenceOf("?", false, false);
    if (path != "/" && path != "/index.html") return std::nullopt;
    int size = 0; const void* data = BlockStepKit::resource("index.html", size);
    if (data == nullptr || size <= 0) return std::nullopt;
    juce::WebBrowserComponent::Resource res;
    res.data.resize((size_t) size);
    std::memcpy(res.data.data(), data, (size_t) size);
    res.mimeType = "text/html";
    return res;
}

void BlockStepEditor::sendRestore() {
    if (! pageReady) return;
    auto* o = new juce::DynamicObject();
    o->setProperty("json", proc.getUiState());
    web.emitEventIfBrowserIsVisible("bs_restore", juce::var(o));
}

void BlockStepEditor::timerCallback() {
    {   // webzoom: smaller / bigger window = the same layout, smaller / bigger (FabFilter style)
        const double z = ghosty::webzoom::zoomFor(web.getWidth(), web.getHeight(), zoomRefW, zoomRefH);
        if (std::abs(z - zoomNow) > 0.002 && ghosty::webzoom::apply(web, z)) { zoomNow = z; sgrip.setVisible(false); }
    }
    banks.tick();
    auto& P = proc.getEngine().pos;
    if (P.host.load()) hostBpm = P.bpm.load();
    if (pageReady) {
        auto* o = new juce::DynamicObject();
        o->setProperty("playing", P.playing.load()); o->setProperty("step", P.step.load()); o->setProperty("frac", (double) P.frac.load());
        o->setProperty("host", P.host.load()); o->setProperty("bpm", (double) P.bpm.load());
        o->setProperty("daw", proc.dawBpm.load());   // the DAW's tempo (also when stopped): the page shows it, x the TEMPO multiple
        web.emitEventIfBrowserIsVisible("bs_pos", juce::var(o));
    }
    const juce::String s = P.host.load() ? "FOLLOWING THE DAW  " + juce::String(juce::roundToInt(P.bpm.load())) + " BPM"
                         : P.playing.load() ? "PREVIEW  " + juce::String(juce::roundToInt(P.bpm.load())) + " BPM"
                         : (proc.license.isUnlocked() ? juce::String("PRESS PLAY IN YOUR DAW") : juce::String("DEMO MODE"));
    if (status.getText() != s) status.setText(s, juce::dontSendNotification);
    const bool showUnlock = ghosty::License::required();
    if (unlock->isVisible() != showUnlock) { unlock->setVisible(showUnlock); resized(); }
}

// ---------------------------------------------------------------- drag the pattern out as MIDI
void BlockStepEditor::DragOut::paint(juce::Graphics& g) {
    auto r = getLocalBounds().toFloat().reduced(0.5f);
    g.setColour(juce::Colour(0x2200ff88)); g.fillRoundedRectangle(r, 9.0f);
    g.setColour(kGreen.withAlpha(isMouseOver() ? 1.0f : 0.7f)); g.drawRoundedRectangle(r, 9.0f, 1.2f);
    g.setColour(kInk); g.setFont(juce::FontOptions(12.5f, juce::Font::bold));
    g.drawText(juce::String::fromUTF8(wav ? "\xe2\x87\xa3 WAV" : "\xe2\x87\xa3 MIDI"), getLocalBounds(), juce::Justification::centred);
}

void BlockStepEditor::DragOut::mouseDrag(const juce::MouseEvent& e) {
    if (dragging || e.getDistanceFromDragStart() < 6) return;
    dragging = true;
    if (! ed.proc.license.isUnlocked()) { if (ed.unlock) ed.unlock->nudge(); return; }   // demo: export is off
    const juce::File f = wav ? ed.writeWav(ed.dragCh) : ed.writeMidi(ed.dragCh);
    if (f.existsAsFile()) juce::DragAndDropContainer::performExternalDragDropOfFiles({ f.getFullPathName() }, false, this);
}

void BlockStepEditor::updTarget() {
    auto P = proc.getUiPattern(); if (P != nullptr && dragCh >= P->ch) dragCh = -1;   // (-2 = PAGE, -3 = the CH panel's channels)
    if (dragCh == -3 && dragMask == 0) dragCh = -1;
    target.setButtonText(dragCh == -1 ? "ALL" : dragCh == -2 ? "PAGE" : dragCh == -3 ? chNames() : "CH" + juce::String(dragCh + 1));
}

// a fresh file per drag: FL keeps clips by file path, so a reused name could bring back an older sound
static juce::String maskNames(juce::uint32 m) { juce::StringArray n; for (int c = 0; c < 32; ++c) if ((m >> c) & 1u) n.add(juce::String(c + 1)); return "CH" + n.joinIntoString("+"); }
juce::String BlockStepEditor::chNames() const { return maskNames(dragMask); }
static juce::File exportFile(int piano, int onlyCh, const char* ext, juce::uint32 mask, double bpm) {   // bpm: the tempo it plays at (TEMPO multiple included), in the name
    static int n = 0;
    auto dir = juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("Block Step Exports");
    if (! dir.createDirectory()) dir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    const juce::String nm = juce::String("Block Step ") + (piano == 2 ? "SYNTH " : piano == 1 ? "PIANO " : "DRUMS ") + (onlyCh >= 0 ? "CH" + juce::String(onlyCh + 1) : onlyCh == -3 ? maskNames(mask) : juce::String(onlyCh == -2 ? "PAGE" : "ALL"))
                          + " " + juce::String(bpm, std::abs(bpm - std::round(bpm)) < 0.01 ? 0 : 1) + " BPM"
                          + " " + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M-%S") + " " + juce::String(++n);
    return dir.getNonexistentChildFile(nm, ext, false);
}

juce::File BlockStepEditor::writeWav(int onlyCh) const {
    auto P = proc.getUiPattern(); if (P == nullptr || P->rows <= 0) return {};
    const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 44100.0;
    auto buf = bstep::Engine::bounce(proc.kitStore(), proc.kitSynth(), *P, sr, baseBpmFor(*P), onlyCh, dragMask);   // (TEMPO multiple applied inside: as heard)
    if (buf.getNumSamples() <= 0) return {};
    const juce::File f = exportFile(proc.pageKind.load(), onlyCh, ".wav", dragMask, effBpmFor(*P));
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> os = f.createOutputStream(); if (os == nullptr) return {};
    juce::WavAudioFormat wavFmt;
    std::unique_ptr<juce::AudioFormatWriter> w(wavFmt.createWriterFor(os.get(), sr, 2, 24, {}, 0)); if (w == nullptr) return {};
    os.release();   // the writer owns the stream now
    w->writeFromAudioSampleBuffer(buf, 0, buf.getNumSamples()); w.reset();
    return f;
}

// ⇣ BY DRUM (owner 2026-11-21): every hit of ONE drum (all the kicks, all the snares...), whatever channel each hit is in, so a
// roll that spilled into another channel still comes out with its drum. Taken from the layer that holds that drum (the page on
// screen, or a layer playing along with it); everything else in that layer is left out. Muted channels count (like CH);
// muted notes stay muted. WAV = as heard, MIDI = the same notes as ⇣ MIDI gives them.
juce::File BlockStepEditor::writeInst(int inst, bool wav) {
    static const char* const names[bstep::kInst] = { "KICK", "SNARE", "CLAP", "HAT", "OPEN HAT", "PERC", "RIM", "CRASH", "SUB", "FX", "PIANO", "ACID" };
    auto P = proc.getUiPattern(); if (P == nullptr || P->rows <= 0 || inst < 0 || inst >= bstep::kInst) return {};
    auto has = [inst](const bstep::Pattern& Q) { for (int r = 0; r < Q.rows; ++r) for (int c = 0; c < Q.ch; ++c) if (Q.at(r, c).i == inst) return true; return false; };
    const bstep::Pattern* src = has(*P) ? P.get() : nullptr;
    if (src == nullptr) for (auto& a : P->along) if (a && has(*a)) { src = a.get(); break; }
    auto say = [this](const juce::String& t) { if (! pageReady) return; auto* o = new juce::DynamicObject(); o->setProperty("text", t); web.emitEventIfBrowserIsVisible("bs_toast", juce::var(o)); };
    if (src == nullptr) { say(juce::String("No ") + names[inst] + " in the beat to drag out"); return {}; }
    auto Q = std::make_shared<bstep::Pattern>(*src);
    for (auto& x : Q->cells) if (x.i != inst) x = bstep::Cell {};
    Q->chOn.fill(true); Q->along.clear(); Q->selfMuted = false;
    const double bpm = baseBpmFor(*P);
    static int n = 0;
    auto dir = juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("Block Step Exports");
    if (! dir.createDirectory()) dir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    const double eb = bpm * Q->mul;
    const juce::File f = dir.getNonexistentChildFile(juce::String("Block Step ") + names[inst] + " (every hit) " + juce::String(eb, std::abs(eb - std::round(eb)) < 0.01 ? 0 : 1) + " BPM "
                                                     + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M-%S") + " " + juce::String(++n), wav ? ".wav" : ".mid", false);
    f.deleteFile();
    if (! wav) { auto mf = bstep::midiClip(*Q, -2, 0, bpm); juce::FileOutputStream out(f); if (! out.openedOk()) return {}; mf.writeTo(out); out.flush(); return f; }
    const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 44100.0;
    auto buf = bstep::Engine::bounce(proc.kitStore(), proc.kitSynth(), *Q, sr, bpm, -2);
    if (buf.getNumSamples() <= 0) return {};
    std::unique_ptr<juce::OutputStream> os = f.createOutputStream(); if (os == nullptr) return {};
    juce::WavAudioFormat wavFmt;
    std::unique_ptr<juce::AudioFormatWriter> w(wavFmt.createWriterFor(os.get(), sr, 2, 24, {}, 0)); if (w == nullptr) return {};
    os.release();
    w->writeFromAudioSampleBuffer(buf, 0, buf.getNumSamples()); w.reset();
    return f;
}

juce::File BlockStepEditor::writeMidi(int onlyCh) const {
    auto P = proc.getUiPattern(); if (P == nullptr || P->rows <= 0) return {};
    auto mf = bstep::midiClip(*P, onlyCh, dragMask, baseBpmFor(*P));
    const juce::File f = exportFile(proc.pageKind.load(), onlyCh, ".mid", dragMask, effBpmFor(*P));
    f.deleteFile();
    juce::FileOutputStream out(f);
    if (! out.openedOk()) return {};
    mf.writeTo(out); out.flush();
    return f;
}

void BlockStepEditor::updGho() {
    ghoBtn.setButtonText(ghoShort ? (ghostsOff ? "GHOSTS: OFF" : "GHOSTS: ON") : (ghostsOff ? "GHOST ANIMATION: OFF" : "GHOST ANIMATION: ON"));
    ghoBtn.setColour(juce::TextButton::buttonColourId, ghostsOff ? juce::Colour(0xff3a3d44) : juce::Colour(0xff16a34a));   // green ON, grey OFF
    ghoBtn.setColour(juce::TextButton::textColourOffId, ghostsOff ? juce::Colour(0xffc4c7cc) : juce::Colour(0xffffffff));
    ghoBtn.setColour(juce::ComboBox::outlineColourId, ghostsOff ? juce::Colour(0xff6b7280) : juce::Colour(0xff4ade80));
}

// ---------------------------------------------------------------- ⇣: one sound from the sound browsers into the DAW (owner 2026-11-02)
void BlockStepEditor::sndHover(const juce::var& v) {
    if (! (bool) v["on"]) { sgrip.setVisible(false); return; }
    if (! sgrip.attach(*this)) return;
    sgripSnd = v;
    sgrip.setTooltip(v.hasProperty("xi") ? juce::String::fromUTF8("drag \xe2\x87\xa3 into your DAW: ") + v["n"].toString() + (v["xk"].toString() == "midi" ? " as MIDI" : " as a WAV")
                                       : juce::String::fromUTF8("drag \xe2\x87\xa3 into your DAW for the WAV"));
    sgrip.place(ghosty::sounddrag::pageToEditor(web.getBounds(), zoomNow > 0 ? zoomNow : 1.0, (double) v["x"], (double) v["y"], (double) v["w"], (double) v["h"]));
    if (sgrip.isVisible()) sgrip.toFront(false);
}

juce::File BlockStepEditor::writeSound(const juce::var& v) const {
    const juce::String cat = v["cat"].toString(); const int s = (int) v["s"], i = (int) v["i"];
    juce::AudioBuffer<float> buf; double sr = 0;
    if (s == -2) {   // the built-in synth drum of that instrument (BANK 3)
        if (i < 0 || i >= bstep::kInst) return {};
        const auto& k = proc.kitSynth(); buf.makeCopyOf(k.buf[(size_t) bstep::kInstSynth[i]]); sr = k.sr;
    } else {
        int c = -1; for (int k = 0; k < bstep::kCats; ++k) if (cat == bstep::kCatNames[k]) c = k;
        if (c < 0) return {};
        int idx = s;
        if (c == bstep::kCatPiano) {   // a piano sound is a set of notes: the one nearest middle C
            auto& st = proc.kitStore(); int best = -1; double bd = 1e9;
            for (int k = 0; k < st.count(c); ++k) if (st.groupOf(c, k) == s) { const double d = std::abs(st.rootOf(c, k) - 60.0); if (d < bd) { bd = d; best = k; } }
            idx = best; }
        if (idx < 0 || ! proc.kitStore().exportSound(c, idx, buf, sr)) return {};
    }
    const juce::File f = ghosty::sounddrag::newFile("Block Step", v["n"].toString().isNotEmpty() ? v["n"].toString() : cat + " " + juce::String(s));
    return ghosty::sounddrag::writeWav24(f, buf, sr) ? f : juce::File();
}
