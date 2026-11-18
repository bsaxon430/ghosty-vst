#include "BlockStepEditor.h"
#include "BlockStepBin.h"

namespace {
const juce::Colour kBg (0xff05040a), kStripBg (0xff0b0a13), kInk (0xfff5f3ff), kGrey (0xff9ca3af), kGreen (0xff00ff88), kPurple (0xffa855f7);
}

juce::WebBrowserComponent::Options BlockStepEditor::options(BlockStepEditor& e) {
    auto o = juce::WebBrowserComponent::Options {}
        .withNativeIntegrationEnabled()
        .withKeepPageLoadedWhenBrowserIsHidden()
        .withResourceProvider([&e](const juce::String& url) { return e.resource(url); })
        .withEventListener("bs_ready", [&e](juce::var) { e.pageReady = true; e.sendRestore(); e.banks.send(); })
        .withEventListener("bs_state", [&e](juce::var v) { e.proc.setUiState(v["json"].toString()); })
        .withEventListener("bs_audition", [&e](juce::var v) { e.proc.getEngine().audition(bstep::cellFromVar(v["cell"])); })
        .withEventListener("bs_transport", [&e](juce::var v) { e.proc.getEngine().setInternalPlay((bool) v["play"]); })
        .withEventListener("bs_dragch", [&e](juce::var v) { e.dragCh = (int) v["ch"]; e.updTarget(); })
        .withEventListener("bs_seek", [&e](juce::var v) { e.proc.getEngine().seek((int) v["step"]); });
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
    target.onClick = [this] { auto P = proc.getUiPattern(); const int n = P ? P->ch : 1; dragCh = dragCh == -1 ? -2 : dragCh == -2 ? 0 : (dragCh + 1 >= n ? -1 : dragCh + 1); updTarget(); };   // ALL -> PAGE -> CH1..n
    addAndMakeVisible(target); updTarget();
    status.setFont(juce::FontOptions(12.5f, juce::Font::bold));
    status.setColour(juce::Label::textColourId, kGrey);
    status.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(status);
    unlock = std::make_unique<ghosty::UnlockButton>(proc.license);
    unlock->setColour(juce::TextButton::buttonColourId, juce::Colour(0x33a855f7));
    unlock->setColour(juce::TextButton::textColourOffId, kInk);
    addChildComponent(*unlock);
    bypassBtn = std::make_unique<ghosty::BypassButton>(*proc.bypassParam); bypassBtn->setDark(true); addAndMakeVisible(*bypassBtn);
    unlock->refresh();
    addAndMakeVisible(web);
    web.goToURL(juce::WebBrowserComponent::getResourceProviderRoot());
    setResizable(true, true);
    setResizeLimits(960, 620, 2560, 1700);   // under 960 the page would drop to its phone layout
    // a Mac-size window by default (the page switches to its desktop layout at 900 px wide), never taller or wider
    // than the screen, so the resize corner is always reachable
    // first open: as big as the screen allows (FL's title bar and the menu bar still fit); after that, the size you left it at,
    // which is also saved in the project. An old small saved size (from before this) opens big again.
    int w = 1280, h = 800;
    if (const auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()) {
        const auto ua = d->userArea; const int bw = ua.getWidth() - 40, bh = ua.getHeight() - 90;
        const bool saved = proc.uiW >= 1200 && proc.uiH >= 740;
        w = saved ? juce::jmin(proc.uiW, bw) : bw; h = saved ? juce::jmin(proc.uiH, bh) : bh; }
    else if (proc.uiW >= 960 && proc.uiH >= 620) { w = proc.uiW; h = proc.uiH; }
    setSize(juce::jmax(960, w), juce::jmax(620, h));
    startTimerHz(30);
}

BlockStepEditor::~BlockStepEditor() { stopTimer(); }

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
    drag.setBounds(top.removeFromLeft(84)); top.removeFromLeft(4); dragWav.setBounds(top.removeFromLeft(84)); top.removeFromLeft(4); target.setBounds(top.removeFromLeft(62));
    bypassBtn->setBounds(top.removeFromRight(96)); top.removeFromRight(6);
    if (unlock->isVisible()) unlock->setBounds(top.removeFromRight(92));
    status.setBounds(top);
    web.setBounds(r);
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
    banks.tick();
    auto& P = proc.getEngine().pos;
    if (P.host.load()) hostBpm = P.bpm.load();
    if (pageReady) {
        auto* o = new juce::DynamicObject();
        o->setProperty("playing", P.playing.load()); o->setProperty("step", P.step.load()); o->setProperty("frac", (double) P.frac.load());
        o->setProperty("host", P.host.load()); o->setProperty("bpm", (double) P.bpm.load());
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
    auto P = proc.getUiPattern(); if (P != nullptr && dragCh >= P->ch) dragCh = -1;   // (-2 = PAGE)
    target.setButtonText(dragCh == -1 ? "ALL" : dragCh == -2 ? "PAGE" : "CH" + juce::String(dragCh + 1));
}

// a fresh file per drag: FL keeps clips by file path, so a reused name could bring back an older sound
static juce::File exportFile(int piano, int onlyCh, const char* ext) {
    static int n = 0;
    auto dir = juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("Block Step Exports");
    if (! dir.createDirectory()) dir = juce::File::getSpecialLocation(juce::File::tempDirectory);
    const juce::String nm = juce::String("Block Step ") + (piano == 2 ? "SYNTH " : piano == 1 ? "PIANO " : "DRUMS ") + (onlyCh >= 0 ? "CH" + juce::String(onlyCh + 1) : juce::String(onlyCh == -2 ? "PAGE" : "ALL"))
                          + " " + juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M-%S") + " " + juce::String(++n);
    return dir.getNonexistentChildFile(nm, ext, false);
}

juce::File BlockStepEditor::writeWav(int onlyCh) const {
    auto P = proc.getUiPattern(); if (P == nullptr || P->rows <= 0) return {};
    const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 44100.0;
    auto buf = bstep::Engine::bounce(proc.kitStore(), proc.kitSynth(), *P, sr, proc.dawBpm.load() > 0 ? proc.dawBpm.load() : (hostBpm > 0 ? hostBpm : P->bpm), onlyCh);
    if (buf.getNumSamples() <= 0) return {};
    const juce::File f = exportFile(proc.pageKind.load(), onlyCh, ".wav");
    f.deleteFile();
    std::unique_ptr<juce::OutputStream> os = f.createOutputStream(); if (os == nullptr) return {};
    juce::WavAudioFormat wavFmt;
    std::unique_ptr<juce::AudioFormatWriter> w(wavFmt.createWriterFor(os.get(), sr, 2, 24, {}, 0)); if (w == nullptr) return {};
    os.release();   // the writer owns the stream now
    w->writeFromAudioSampleBuffer(buf, 0, buf.getNumSamples()); w.reset();
    return f;
}

juce::File BlockStepEditor::writeMidi(int onlyCh) const {
    auto P = proc.getUiPattern(); if (P == nullptr || P->rows <= 0) return {};
    static constexpr int kNote[bstep::kInst] = { 36, 38, 39, 42, 46, 45, 37, 49, 35, 56, 60, 48 };   // GM drums: kick snare clap hat ohat tom rim crash, sub = 35, fx = 56
    const int tps = 24, ppq = 96;   // a step is a 16th = 24 ticks
    juce::MidiMessageSequence seq;
    seq.addEvent(juce::MidiMessage::tempoMetaEvent((int) std::lround(60000000.0 / (proc.dawBpm.load() > 0 ? proc.dawBpm.load() : P->bpm))), 0);
    seq.addEvent(juce::MidiMessage::timeSignatureMetaEvent(4, 4), 0);
    int mch = 10;   // drums on channel 10, the piano on channel 1
    auto note = [&](int n, int vel, double t, double len) {
        seq.addEvent(juce::MidiMessage::noteOn(mch, n, (juce::uint8) juce::jlimit(1, 127, vel)), t);
        seq.addEvent(juce::MidiMessage::noteOff(mch, n), t + len);
    };
    std::vector<const bstep::Pattern*> pages; if (onlyCh != -1 || ! P->selfMuted) pages.push_back(P.get()); if (onlyCh == -1) for (auto& a : P->along) if (a) pages.push_back(a.get());   // ALL: every layer playing
    for (const bstep::Pattern* Q : pages) for (int r = 0; r < Q->rows; ++r) for (int c = 0; c < Q->ch; ++c) {
        const auto& x = Q->at(r, c); if (! x.on() || x.m || (onlyCh >= 0 ? (Q != P.get() || c != onlyCh) : ! Q->chOn[(size_t) c])) continue;
        const int n = juce::jlimit(0, 127, kNote[x.i] + (x.i == 8 ? x.p : x.i == bstep::kPianoInst ? x.p + (int) std::lround(bstep::keyShift(Q, x.i, 60.0, x.p)) : 0)), vel = (int) std::lround(x.v * 1.27 * Q->drumVol[(size_t) x.i] * Q->chVol[(size_t) c]);
        const double rd = Q->rowD[(size_t) r], tq = tps * rd, t = Q->rowT[(size_t) r] * tps + x.o * tq + (rd == 1.0 && (std::llround(Q->rowT[(size_t) r]) & 1) ? Q->swingDelay(1, tps) : 0.0);   // the page's GRID: each row at its own time
        mch = x.i == bstep::kPianoInst ? 1 : x.i == bstep::kSynthInst ? 2 : 10;
        if (x.i == bstep::kSynthInst) { const int snd = juce::jlimit(0, gsyn::kSounds - 1, x.s >= 0 ? (int) x.s : 0), base = gsyn::baseNote(snd);
            const int sn = juce::jlimit(0, 127, base + x.p + (int) std::lround(bstep::keyShift(Q, x.i, base, x.p)));
            double ln = x.l > 0 ? x.l * tq : tq * (gsyn::isMono(snd) ? 0.55 : 0.9); if (x.sl) { const double dt = Q->nextSynthSteps(r, snd); if (dt > 0) ln = dt * tps + 2; }
            note(sn, x.ac ? 127 : juce::jmin(110, vel), t, ln); continue; }
        const double ln0 = x.l > 0 ? x.l * tq : tq * 0.9;
        { const double pn = juce::jlimit(-1.0, 1.0, x.pn / 100.0 + Q->chPan[(size_t) c]);   // pan: CC10 just before the note
          seq.addEvent(juce::MidiMessage::controllerEvent(mch, 10, juce::jlimit(0, 127, (int) std::lround(64 + pn * 63.5))), juce::jmax(0.0, t - 0.5)); }
        if (x.fx == bstep::FX_ECHO || x.fx == bstep::FX_PING || x.fx == bstep::FX_TAPE) { static constexpr double T[9] = { 8, 6, 4, 3, 2, 1.5, 1, 0.75, 0.5 }; const double dt = T[juce::jmin((int) x.pr, 8)] * tps, fb = 0.9 - juce::jmin((int) x.q, 16) * 0.045; double g = 1;
            note(n, vel, t, ln0); for (int k = 1; k <= 32; ++k) { g *= fb; if (g < 0.03) break; note(n, (int) std::lround(vel * g), t + k * dt, juce::jmin(ln0, dt * 0.95)); } }
        else if (x.fx == bstep::FX_STUT) { static constexpr double R[9] = { 1, 1.5, 2, 3, 4, 6, 8, 12, 16 }; const double sp = tps / R[juce::jmin((int) x.pr, 8)], gm = 0.97 - juce::jmin((int) x.q, 16) * 0.03; double g = 1;
            note(n, vel, t, sp * 0.9); for (int k = 1; k <= 128 && k * sp < tps * 32; ++k) { g *= gm; if (g < 0.04) break; note(n, (int) std::lround(vel * g), t + k * sp, sp * 0.9); } }
        else if (x.fx == bstep::FX_RETRIG && x.pr > 0) { const int k = juce::jmin((int) x.pr, 8); const double sp = tq / (k + 1); note(n, vel, t, sp * 0.8); for (int j = 1; j <= k; ++j) note(n, (int) (vel * juce::jmax(0.2, 0.8 - j * 0.06)), t + j * sp, sp * 0.8); }
        else if (x.fx == bstep::FX_CUT || x.fx == bstep::FX_NOTEOFF) note(n, vel, t, tq * 0.5);
        else note(n, vel, t, x.l > 0 ? x.l * tq : tq * 0.9);   // long notes keep their length in the clip
    }
    seq.updateMatchedPairs(); seq.sort();
    juce::MidiFile mf; mf.setTicksPerQuarterNote(ppq); mf.addTrack(seq);
    const juce::File f = exportFile(proc.pageKind.load(), onlyCh, ".mid");
    f.deleteFile();
    juce::FileOutputStream out(f);
    if (! out.openedOk()) return {};
    mf.writeTo(out); out.flush();
    return f;
}
