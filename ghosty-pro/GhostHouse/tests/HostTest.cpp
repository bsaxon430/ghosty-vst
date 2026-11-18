// Ghost House host test: loads the real, built plug-in (VST3) as an INSTRUMENT the way FL Studio does and checks
//   1. it makes sound (MIDI keys play the kit);
//   2. FRESH START: two instances in one process; change the first, the second is still at factory defaults; destroy
//      and recreate = factory defaults; the page in a new window hands the plugin the factory settings;
//   3. SIZE: a new window opens at the fitting size (about 1200 x 760, never wider than 1240 or taller than 780), the
//      native strip's controls never overlap at any size, a new instance never takes another window's size, and a
//      project reopens at its own saved size.
//   GhostHouseHostTest <Ghost House.vst3> [shots folder]      exit code = number of failures
#include <juce_audio_utils/juce_audio_utils.h>
#include <cstdio>

static int gFails = 0, gPasses = 0;
static void check(bool ok, const juce::String& name, const juce::String& detail = {}) {
    std::printf("%s %s%s\n", ok ? "PASS" : "FAIL", name.toRawUTF8(), detail.isNotEmpty() ? (" -- " + detail).toRawUTF8() : "");
    std::fflush(stdout); (ok ? gPasses : gFails)++;
}
static void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

static juce::MemoryBlock stateOf(juce::AudioPluginInstance& p) { juce::MemoryBlock mb; p.getStateInformation(mb); return mb; }
// The VST3 host wraps the plug-in's own state in its XML ("VST3PluginState" > "IComponent" = base64); inside that is
// Ghost House's state: "GHSE1" + the page's JSON + the window size.
static juce::MemoryBlock innerOf(const juce::MemoryBlock& mb) {
    juce::MemoryBlock inner;
    if (auto x = juce::AudioProcessor::getXmlFromBinary(mb.getData(), (int) mb.getSize()))
        if (auto* c = x->getChildByName("IComponent")) inner.fromBase64Encoding(c->getAllSubText());
    return inner;
}
static juce::String jsonOf(const juce::MemoryBlock& mb) {
    const auto inner = innerOf(mb);
    juce::MemoryInputStream in(inner, false); if (in.readString() != "GHSE1") return "<not Ghost House state>"; return in.readString(); }
static juce::MemoryBlock makeState(const juce::String& json, int w = 0, int h = 0, bool mark = false) {
    juce::MemoryBlock inner; { juce::MemoryOutputStream o(inner, false); o.writeString("GHSE1"); o.writeString(json); o.writeInt(w); o.writeInt(h); if (mark) o.writeInt(0x47485A32); }
    juce::XmlElement x("VST3PluginState"); x.createNewChildElement("IComponent")->addTextElement(inner.toBase64Encoding());
    juce::MemoryBlock mb; juce::AudioProcessor::copyXmlToBinary(x, mb); return mb; }

struct Host {
    juce::AudioPluginFormatManager fm; juce::PluginDescription desc; bool ok = false;
    explicit Host(const juce::File& bundle) {
        fm.addDefaultFormats(); juce::OwnedArray<juce::PluginDescription> types;
        for (auto* f : fm.getFormats()) if (f->fileMightContainThisPluginType(bundle.getFullPathName())) { f->findAllTypesForFile(types, bundle.getFullPathName()); if (! types.isEmpty()) break; }
        if (! types.isEmpty()) { desc = *types[0]; ok = true; }
    }
    std::unique_ptr<juce::AudioPluginInstance> make() {
        juce::String err; auto p = fm.createPluginInstance(desc, 44100.0, 512, err); if (p == nullptr) return nullptr;
        auto layout = p->getBusesLayout(); for (auto& b : layout.inputBuses) b = juce::AudioChannelSet::disabled();
        if (! layout.outputBuses.isEmpty()) layout.outputBuses.getReference(0) = juce::AudioChannelSet::stereo();
        p->setBusesLayout(layout); p->prepareToPlay(44100.0, 512); pump(50); return p;
    }
};

static float playKick(juce::AudioPluginInstance& p) {   // MIDI C1 = KICK; the kit decodes on a thread, so keep trying for a few seconds
    juce::AudioBuffer<float> buf(2, 512); float peak = 0;
    for (int b = 0; b < 400 && peak < 0.01f; ++b) {
        juce::MidiBuffer m; if (b % 40 == 0) m.addEvent(juce::MidiMessage::noteOn(1, 36, (juce::uint8) 110), 0);
        buf.clear(); p.processBlock(buf, m); peak = juce::jmax(peak, buf.getMagnitude(0, 512));
        if (b % 20 == 0) pump(20);
    }
    return peak;
}

static juce::File stripFile;
static juce::AudioProcessorEditor* openEditor(juce::AudioPluginInstance& p) {
    stripFile.deleteFile();
    auto* ed = p.createEditorIfNeeded(); if (ed == nullptr) return nullptr;
    ed->addToDesktop(0); ed->setTopLeftPosition(40, 60); ed->setVisible(true);
    pump(300); return ed;
}
static void closeEditor(juce::AudioPluginInstance& p) { if (auto* ed = p.getActiveEditor()) { p.editorBeingDeleted(ed); delete ed; } pump(100); }

// the native strip (MIDI / WAV drag, ALL, status, GHOST ANIMATION, BYPASS...): the plug-in checks its own controls in
// resized() and writes the result to $GHOSTHOUSE_STRIPCHECK (the host only sees the VST3 view, not the plug-in's controls)
// the plug-in's own window size, as its resized() reported it ("ok W x H"); falls back to the host's view
static std::pair<int, int> pluginSize(juce::Component& ed) {
    const auto t = stripFile.loadFileAsString().trim();
    if (t.startsWith("ok ")) { const auto a = juce::StringArray::fromTokens(t.fromFirstOccurrenceOf("ok ", false, false), " x", ""); if (a.size() >= 2) return { a[0].getIntValue(), a[a.size() - 1].getIntValue() }; }
    return { ed.getWidth(), ed.getHeight() };
}
static void printPageLog() {
    const auto f = juce::File(stripFile.getFullPathName() + ".page.log");
    for (auto& l : juce::StringArray::fromLines(f.loadFileAsString())) if (l.isNotEmpty()) std::printf("  [page] %s\n", l.toRawUTF8());
    f.deleteFile(); std::fflush(stdout);
}

static bool waitForPage(juce::AudioPluginInstance& p, int ms) {   // the page hands the plugin its state once it has loaded
    for (int t = 0; t < ms; t += 100) { if (jsonOf(stateOf(p)).contains("\"pats\"")) return true; pump(100); }
    return false;
}

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) { std::printf("usage: GhostHouseHostTest <Ghost House.vst3> [shots folder]\n"); return 1; }
    const juce::File bundle(juce::File::getCurrentWorkingDirectory().getChildFile(juce::String(juce::CharPointer_UTF8(argv[1]))));
    const juce::File shots = argc > 2 ? juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]) : juce::File();
    stripFile = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("ghosthouse-stripcheck.txt");
    ::setenv("GHOSTHOUSE_STRIPCHECK", stripFile.getFullPathName().toRawUTF8(), 1);
    Host host(bundle);
    check(host.ok, "the host finds the plug-in", bundle.getFullPathName());
    if (! host.ok) return 1;
    check(host.desc.isInstrument, "it is an instrument", host.desc.name + ", " + host.desc.pluginFormatName);

    // ---- 1. sound
    auto A = host.make(); check(A != nullptr, "the host makes an instance"); if (A == nullptr) return 1;
    const float pk = playKick(*A); check(pk > 0.01f, "MIDI C1 plays the kick (it makes sound)", "peak " + juce::String(pk, 3));

    // ---- 2. fresh start (the plugin side)
    const auto F0 = stateOf(*A);
    auto B = host.make();
    check(B != nullptr && innerOf(stateOf(*B)) == innerOf(F0), "two new instances start the same (factory)");
    const juce::String changed = "{\"pats\":[{\"bars\":1,\"ch\":5,\"rows\":[[{\"i\":0,\"p\":0,\"v\":100,\"fx\":\"\",\"pr\":4},null,null,null,null]],\"loop\":null}],\"cur\":0,\"bpm\":99,\"swing\":60,\"eng\":\"fresh\",\"apx\":60}";
    A->setStateInformation(makeState(changed, 1000, 633, true).getData(), (int) makeState(changed, 1000, 633, true).getSize()); pump(50);
    // (the test runs with an empty sandbox home, so there is no licence: demo mode, where a reopened project starts at the
    // default beat on purpose; then only the window size comes back)
    const bool demo = ! jsonOf(stateOf(*A)).contains("\"bpm\":99");
    if (demo) std::printf("NOTE demo mode (no licence in the test's sandbox): the project's beat is not restored, by design\n");
    else check(true, "the first instance takes its project's state");
    check(innerOf(stateOf(*B)) == innerOf(F0), "...and the second is still at factory defaults");
    B.reset(); pump(50); B = host.make();
    check(B != nullptr && innerOf(stateOf(*B)) == innerOf(F0), "destroy and recreate: factory defaults");

    // ---- 3. size + the page
    if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()) std::printf("screen user area %s\n", d->userArea.toString().toRawUTF8());
    if (auto* ed = openEditor(*B)) {
        const auto [w, h] = pluginSize(*ed);
        check(w <= 1240 && h <= 780 && w >= 1100 && h >= 700, "a new window opens at the fitting size (about 1200 x 760, at most 1240 x 780)", juce::String(w) + " x " + juce::String(h));
        check(ed->isResizable(), "the window is resizable");
        const std::pair<int, int> sizes[] = { { 720, 456 }, { 900, 570 }, { 1200, 760 }, { 1240, 785 }, { 1600, 1013 } };
        for (const auto& [rw, rh] : sizes) {
            stripFile.deleteFile(); ed->setSize(rw, rh); pump(150);
            const auto pr = stripFile.loadFileAsString().trim();   // the plugin's own check of its strip, written by its resized()
            check(pr.startsWith("ok"), "the strip's controls fit and don't overlap at about " + juce::String(rw) + " x " + juce::String(rh), pr.isEmpty() ? "no report from the plug-in" : pr);
        }
        ed->setSize(1200, 760); pump(100);
        const bool ready = waitForPage(*B, 30000); printPageLog();
        check(ready, "the page loads and hands the plugin its state");
        const auto js = jsonOf(stateOf(*B));
        check(js.contains("\"eng\":\"classic\"") && js.contains("\"apx\":0") && js.contains("\"bpm\":124"), "a new window's page is at factory settings (CLASSIC, GLITCH 0, 124 BPM)");
        if (shots != juce::File()) { shots.createDirectory(); auto img = ed->createComponentSnapshot(ed->getLocalBounds());
            juce::FileOutputStream fo(shots.getChildFile("ghosthouse-window-native-1200x760.png")); fo.setPosition(0); fo.truncate(); juce::PNGImageFormat().writeImageToStream(img, fo); }
        ed->setSize(900, 570); pump(100);   // Brandon makes this window small...
        closeEditor(*B);
    } else check(false, "the window opens");

    auto C = host.make();   // ...a new instance does not take that size
    if (auto* ed = openEditor(*C)) { const auto [w, h] = pluginSize(*ed); check(w >= 1100 && w <= 1240 && h <= 780, "a new instance opens at the fitting size, not another window's size", juce::String(w) + " x " + juce::String(h));
        const bool ready = waitForPage(*C, 30000); printPageLog(); const auto js = jsonOf(stateOf(*C));
        check(ready && js.contains("\"eng\":\"classic\"") && js.contains("\"apx\":0"), "a second window's page is also at factory settings");
        closeEditor(*C); } else check(false, "the second window opens");
    std::printf("step: reopen the first instance's window\n"); std::fflush(stdout);
    if (auto* ed = openEditor(*A)) { waitForPage(*A, 30000); printPageLog(); const auto [w, h] = pluginSize(*ed); check(std::abs(w - 1000) <= 2 && std::abs(h - 633) <= 2, "a project reopens at its own saved window size", juce::String(w) + " x " + juce::String(h));
        closeEditor(*A); } else check(false, "the first window opens");
    std::printf("step: an old project\n"); std::fflush(stdout);
    auto O = host.make();   // an old project (before build 105) saved the old screen-filling size: it opens at the fitting size
    O->setStateInformation(makeState(changed, 1400, 785).getData(), (int) makeState(changed, 1400, 785).getSize());
    if (auto* ed = openEditor(*O)) { waitForPage(*O, 30000); printPageLog(); const auto [w, h] = pluginSize(*ed); check(w <= 1240 && h <= 780 && w >= 1100, "an older project opens at the fitting size, not its old big one", juce::String(w) + " x " + juce::String(h));
        closeEditor(*O); } else check(false, "the old project's window opens");

    O.reset(); pump(200);
    // FL Studio: open a plug-in window and close it straight away, before its page has loaded. Must not crash.
    std::printf("step: open and close a window at once\n"); std::fflush(stdout);
    for (int k = 0; k < 3; ++k) { if (openEditor(*C) != nullptr) closeEditor(*C); }
    pump(1500); printPageLog();
    check(true, "open and close a window at once (before the page loads), 3 times: no crash");
    std::printf("step: close everything\n"); std::fflush(stdout);
    A.reset(); B.reset(); C.reset(); pump(200);
    std::printf("%d passed, %d failed\n", gPasses, gFails);
    return gFails;
}
