/*
 * SEANCE offline checks (no ears, no screen):
 *   shots <dir>   the editor as PNGs; drag a spirit onto another = a thread; grey-outs; every factory sound loads
 *   audio <wav>   the CLAP plugin itself: notes in, sound out (no NaN, no clipping, silence after release)
 *   all <dir>     both
 */
#include "ghostblock/seance/seance-editor.h"
#include "synth/synth.h"
#include "ghostblock/spectral/spectral-fx.h"
#include "ghostblock/sub/sub-bass.h"
#include "ghostblock/morph.h"
#include "ghostblock/seance/steps.h"
#include "ghostblock/seance/out-guard.h"
#include "ghostblock/seance/blend-guard.h"
#include "ghostblock/seance/step-patterns.h"
#include "synth/matrix_index.h"
#include <clap/clap.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <memory>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace baconpaul::six_sines
{
extern const clap_plugin *makePlugin(const clap_host *, bool);
extern ghostblock::seance::StepSeq *getStepsFromPlugin(const clap_plugin_t *);
extern Synth *getSynthFromPlugin(const clap_plugin_t *);
extern ghostblock::p2::Morph *getMorphFromPlugin(const clap_plugin_t *);
extern ghostblock::seance::BlendGuard *getBlendGuardFromPlugin(const clap_plugin_t *);
}
#if JUCE_LINUX
#include <dlfcn.h>
namespace juce::detail
{
bool dispatchNextMessageOnSystemQueue(bool returnIfNoPendingMessages);
}
// A bare X server (xvfb, no window manager) has none of the window manager's atoms, and JUCE sets some of them on
// every new desktop window without checking (BadAtom ends the process). Create them once, from a connection of our own.
static void prepareBareX()
{
    void *x = dlopen("libX11.so.6", RTLD_NOW | RTLD_LOCAL);
    if (x == nullptr)
        return;
    using Open = void *(*)(const char *);
    using Intern = unsigned long (*)(void *, const char *, int);
    using Close = int (*)(void *);
    auto open = (Open)dlsym(x, "XOpenDisplay");
    auto intern = (Intern)dlsym(x, "XInternAtom");
    auto close = (Close)dlsym(x, "XCloseDisplay");
    if (open && intern && close)
        if (void *d = open(nullptr))
        {
            for (const char *n : {"WM_PROTOCOLS", "WM_TAKE_FOCUS", "WM_DELETE_WINDOW", "_NET_WM_PING", "WM_CHANGE_STATE", "WM_STATE", "_NET_WM_WINDOW_TYPE", "_NET_WM_STATE",
                                  "_NET_WM_STATE_HIDDEN", "_NET_WM_STATE_MAXIMIZED_HORZ", "_NET_WM_STATE_MAXIMIZED_VERT", "_NET_WM_STATE_FULLSCREEN", "_NET_WM_MOVERESIZE",
                                  "_NET_FRAME_EXTENTS", "_NET_WORKAREA", "_MOTIF_WM_HINTS", "_NET_WM_ALLOWED_ACTIONS", "_NET_WM_WINDOW_TYPE_COMBO", "_NET_WM_WINDOW_TYPE_NORMAL",
                                  "_NET_WM_ACTION_RESIZE", "_NET_WM_ACTION_FULLSCREEN", "_NET_WM_ACTION_CLOSE", "_NET_WM_STATE_SKIP_TASKBAR", "_NET_WM_STATE_ABOVE"})
                intern(d, n, 0);
            juce::ignoreUnused(close); // kept open: a bare Xvfb forgets its atoms when its last client goes
        }
}
#elif JUCE_MAC
#include <CoreFoundation/CoreFoundation.h>
#endif
// deliver what is waiting on the message queue (callAsync), for about `ms`
static void pumpMessages(int ms)
{
    const auto until = juce::Time::getMillisecondCounter() + (juce::uint32)ms;
    while (juce::Time::getMillisecondCounter() < until)
    {
#if JUCE_LINUX
        while (juce::detail::dispatchNextMessageOnSystemQueue(true)) {}
        juce::Thread::sleep(2);
#elif JUCE_MAC
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.005, false);
#else
        juce::Thread::sleep(2);
#endif
    }
}
using namespace ghostblock::seance;

static int fails = 0;
#define CHECK(c, msg)                                                                                                   \
    do                                                                                                                  \
    {                                                                                                                   \
        if (!(c)) { ++fails; std::printf("FAIL: %s\n", msg); }                                                          \
        else std::printf("ok:   %s\n", msg);                                                                            \
    } while (0)

static void shot(juce::Component &c, const juce::File &dir, const juce::String &name)
{
    auto img = c.createComponentSnapshot(c.getLocalBounds(), true, 1.0f);
    juce::PNGImageFormat png;
    auto f = dir.getChildFile(name + ".png");
    f.deleteFile();
    juce::FileOutputStream os(f);
    png.writeImageToStream(img, os);
    std::printf("shot: %s (%dx%d)\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
}

static juce::MouseEvent mev(juce::Component &c, juce::Point<float> p, juce::Point<float> down, bool dragged)
{
    auto src = juce::Desktop::getInstance().getMainMouseSource();
    return juce::MouseEvent(src, p, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0, &c, &c,
                            juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, dragged);
}

static void dragOn(juce::Component &c, juce::Point<float> a, juce::Point<float> b)
{
    c.mouseDown(mev(c, a, a, false));
    c.mouseDrag(mev(c, a + (b - a) * 0.3f, a, true));
    c.mouseDrag(mev(c, a + (b - a) * 0.7f, a, true));
    c.mouseDrag(mev(c, b, a, true));
    c.mouseUp(mev(c, b, a, true));
}

static void pushSound(ss::Synth &synth, int samples, double &ph)
{
    std::vector<float> L((size_t)samples), R((size_t)samples);
    juce::Random rnd(7);
    for (int i = 0; i < samples; ++i, ph += 1.0 / 48000.0)
    {
        double v = 0;
        for (int k = 1; k <= 9; ++k)
            v += std::sin(2.0 * juce::MathConstants<double>::pi * 220.0 * k * ph) * 0.32 / k;
        v += 0.05 * std::sin(2.0 * juce::MathConstants<double>::pi * (3000.0 + 900.0 * std::sin(ph * 2.0)) * ph);
        v += 0.004 * (rnd.nextFloat() * 2.0 - 1.0);
        L[(size_t)i] = (float)v;
        R[(size_t)i] = (float)(v * 0.9);
    }
    synth.audioOutputRing.push(L.data(), R.data(), (size_t)samples);
}


static void shot2x(juce::Component &c, const juce::File &dir, const juce::String &name)
{ // at the MacBook's 2x
    auto img = c.createComponentSnapshot(c.getLocalBounds(), true, 2.0f);
    juce::PNGImageFormat png;
    auto f = dir.getChildFile(name + ".png");
    f.deleteFile();
    juce::FileOutputStream os(f);
    png.writeImageToStream(img, os);
    std::printf("shot: %s (%dx%d)\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
}
static juce::MouseEvent mevR(juce::Component &c, juce::Point<float> p)
{
    auto src = juce::Desktop::getInstance().getMainMouseSource();
    return juce::MouseEvent(src, p, juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier), 1.0f, 0, 0, 0, 0, &c, &c, juce::Time::getCurrentTime(), p,
                            juce::Time::getCurrentTime(), 1, false);
}
// run the engine for a moment with a chord held, so the editor's meters and scope get real numbers
static void playInto(ss::Synth &synth, SeanceEditor &ed, int blocks)
{
    for (int k : {45, 52, 57}) synth.voiceManager->processNoteOnEvent(0, 0, k, -1, 0.9f, 0.f);
    std::vector<float> L, R;
    static clap_output_events sink{nullptr, [](const clap_output_events *, const clap_event_header_t *) { return true; }};
    for (int b = 0; b < blocks; ++b)
    {
        synth.process(&sink);
        for (size_t i = 0; i < synth.blockSize; ++i) { L.push_back(synth.output[0][i]); R.push_back(synth.output[1][i]); }
        if (b % 64 == 63) { synth.audioOutputRing.push(L.data(), R.data(), L.size()); L.clear(); R.clear(); ed.testIdle(); }
    }
}
// shown controls: big enough to hit, inside the window, none on top of another
static void checkControls(SeanceEditor &ed, const char *what)
{
    auto v = ed.testControls();
    bool big = true, inside = true, apart = true;
    for (size_t i = 0; i < v.size(); ++i)
    {
        if (!v[i].visible) continue;
        if (v[i].r.getWidth() < 26 || v[i].r.getHeight() < 24) { big = false; std::printf("   small: %s %s\n", v[i].name.toRawUTF8(), v[i].r.toString().toRawUTF8()); }
        if (!juce::Rectangle<int>(0, 0, SeanceEditor::W, SeanceEditor::H).contains(v[i].r)) inside = false;
        for (size_t j = i + 1; j < v.size(); ++j)
            if (v[j].visible && v[i].r.intersects(v[j].r)) { apart = false; std::printf("   overlap: %s / %s\n", v[i].name.toRawUTF8(), v[j].name.toRawUTF8()); }
    }
    CHECK(big && inside && apart, (std::string(what) + ": every control is big enough, inside the window, and overlaps nothing").c_str());
}
static bool ctl(SeanceEditor &ed, const char *name, bool &enabled)
{
    for (auto &c : ed.testControls())
        if (c.visible && c.name == name) { enabled = c.enabled; return true; }
    return false;
}

static int runShots(const juce::File &dir)
{
    dir.createDirectory();
    auto synthHeap = std::make_unique<ss::Synth>(false); // on the heap: the Mac main thread has an 8 MB stack
    auto &synth = *synthHeap;
    synth.setSampleRate(48000);
    UiNoteQueue q;
    auto spectralHeap = std::make_unique<ghostblock::p2::spectral::SpectralFx>();
    auto stepsHeap = std::make_unique<ghostblock::seance::StepSeq>();
    auto guardHeap = std::make_unique<ghostblock::seance::OutGuard>();
    std::atomic<bool> hostDirty{false};
    auto morphHeap = std::make_unique<ghostblock::p2::Morph>();
    morphHeap->activate(48000);
    auto subHeap = std::make_unique<ghostblock::p2::sub::SubBass>();
    subHeap->activate(48000);
    SeanceEditor ed(synth.patchMain, synth.wavetableHandoff, synth.audioToMain, synth.mainToAudio, synth.audioOutputRing, synth.editorActive,
                    synth.uiForceRebuild, synth.dawStateMain, nullptr, q, morphHeap.get(), spectralHeap.get(), subHeap.get(), stepsHeap.get(), guardHeap.get(), &hostDirty);
    auto &P = synth.patchMain;
    auto &tb = *ed.testTable();
    CHECK(ed.getWidth() == 1180 && ed.getHeight() == 760, "opens at 1180 x 760 (fits a 1440 x 900 MacBook Pro screen with the DAW around it)");
    ed.testIdle();
    shot(ed, dir, "01-init");
    checkControls(ed, "spirit panel");

    // drag spirit 1 onto spirit 5: a thread
    dragOn(tb, ed.testSeatPos(0), ed.testSeatPos(4));
    {
        auto &m = P.matrixNodes[ss::MatrixIndex::positionForSourceTarget(0, 4)];
        CHECK(m.active.value > 0.5f && std::abs(m.level.value - 0.5f) < 1e-4f, "drag spirit 1 onto spirit 5 = 1 possesses 5 at half depth");
        CHECK(P.sourceNodes[4].active.value > 0.5f, "spirit 5 woke up");
        CHECK(ed.testPanelTitle() == "1 possesses 5", "the panel shows the new thread");
        bool sawSet = false;
        while (auto m2 = synth.mainToAudio.pop())
            if (m2->action == ss::Synth::MainToAudioMsg::SET_PARAM && m2->paramId == m.level.meta.id) sawSet = true;
        CHECK(sawSet, "the thread's depth went to the audio thread (SET_PARAM)");
    }
    checkControls(ed, "thread panel");
    // the other way round works too (any spirit onto any other), and both directions of a pair can be on at once
    dragOn(tb, ed.testSeatPos(8), ed.testSeatPos(2));
    CHECK(P.matrixNodes[ss::MatrixIndex::positionForSourceTarget(8, 2)].active.value > 0.5f && ed.testPanelTitle() == "9 possesses 3", "9 onto 3 = 9 possesses 3 (a higher seat onto a lower one)");
    dragOn(tb, ed.testSeatPos(4), ed.testSeatPos(0));
    ed.testIdle();
    CHECK(P.matrixNodes[ss::MatrixIndex::positionForSourceTarget(4, 0)].active.value > 0.5f && P.matrixNodes[ss::MatrixIndex::positionForSourceTarget(0, 4)].active.value > 0.5f &&
              ed.testThreadPos(0, 4).getDistanceFrom(ed.testThreadPos(4, 0)) > 12.f,
          "1 > 5 and 5 > 1 together: two threads side by side");
    // one of the new links (beyond the six-operator matrix): 3 -> 12, and 11 -> 12
    dragOn(tb, ed.testSeatPos(2), ed.testSeatPos(11));
    dragOn(tb, ed.testSeatPos(10), ed.testSeatPos(11));
    dragOn(tb, ed.testSeatPos(4), ed.testSeatPos(8));
    dragOn(tb, ed.testSeatPos(6), ed.testSeatPos(8));
    CHECK(P.matrixNodes[ss::MatrixIndex::positionForSourceTarget(2, 11)].active.value > 0.5f && P.matrixNodes[ss::MatrixIndex::positionForSourceTarget(10, 11)].active.value > 0.5f,
          "threads to seat 12 (3 > 12, 11 > 12) connect");
    // make a few of them heard, with feedback on one
    for (int s : {4, 8, 11}) tb.mouseDoubleClick(mev(tb, ed.testSeatPos(s), ed.testSeatPos(s), false));
    CHECK(P.mixerNodes[8].active.value > 0.5f && P.mixerNodes[8].level.value > 0.05f, "double-click a spirit = it is heard");
    ed.setOnce(P.selfNodes[8].active, 1.f);
    ed.setOnce(P.selfNodes[8].fbLevel, 0.4f);
    ed.testIdle();
    // click a thread: its panel; drag it: its depth
    {
        ed.testIdle();
        auto tp = ed.testThreadPos(0, 4);
        CHECK(tp.x > 0, "the thread 1 > 5 is drawn on the table");
        auto &m = P.matrixNodes[ss::MatrixIndex::positionForSourceTarget(0, 4)];
        const float before = m.level.value;
        dragOn(tb, tp, tp + juce::Point<float>(0, -60));
        CHECK(ed.testPanelTitle() == "1 possesses 5" && m.level.value > before + 0.1f, "click a thread = its panel; drag it up = more depth");
    }
    playInto(synth, ed, 4000);
    shot(ed, dir, "02-threads-playing");
    shot2x(ed, dir, "02-threads-playing-2x");

    // the middle = the circle
    tb.mouseDown(mev(tb, ed.testCentre(), ed.testCentre(), false));
    tb.mouseUp(mev(tb, ed.testCentre(), ed.testCentre(), false));
    CHECK(ed.testPanelTitle() == "The circle", "click the middle = the whole sound's panel");
    checkControls(ed, "circle panel");
    {
        bool en = true;
        CHECK(ctl(ed, "SPREAD", en) && !en, "SPREAD is greyed while UNISON is 1 (it would do nothing)");
        ed.setOnce(P.output.unisonCount, 3.f);
        ed.testIdle();
        CHECK(ctl(ed, "SPREAD", en) && en, "SPREAD wakes up when UNISON is above 1");
        ed.setOnce(P.output.unisonCount, 1.f);
    }
    playInto(synth, ed, 600);
    shot(ed, dir, "03-circle");

    // a sleeping spirit: its controls are greyed
    ed.selectSpirit(4);
    ed.setOnce(P.sourceNodes[4].active, 0.f);
    ed.testIdle();
    {
        bool a = true, b = true, c = true;
        CHECK(ctl(ed, "RATIO", a) && !a && ctl(ed, "VOLUME", b) && !b && ctl(ed, "ATTACK", c) && !c, "a sleeping spirit's knobs are greyed");
        CHECK(ctl(ed, "AWAKE", a) && a, "its AWAKE button still works");
    }
    playInto(synth, ed, 600);
    shot(ed, dir, "04-spirit-asleep");
    ed.setOnce(P.sourceNodes[4].active, 1.f);
    ed.testIdle();
    {
        bool a = false, b = true;
        CHECK(ctl(ed, "PAN", a) && a, "PAN is live on a heard spirit");
        ed.setOnce(P.mixerNodes[4].active, 0.f);
        ed.testIdle();
        CHECK(ctl(ed, "PAN", b) && !b, "PAN is greyed on a spirit that is not heard");
    }
    // right-click a thread = cut
    {
        ed.testIdle();
        auto tp = ed.testThreadPos(2, 11);
        tb.mouseDown(mevR(tb, tp));
        CHECK(P.matrixNodes[ss::MatrixIndex::positionForSourceTarget(2, 11)].active.value < 0.5f, "right-click a thread = cut");
    }
    synth.voiceManager->allSoundsOff();

    // a beast: a seat whose tone is one of the Surge oscillators
    if (ed.testHasBeast())
    {
        ed.testPickSpecies(6, 3); // seat 7 becomes a STRING beast
        ed.testIdle();
        CHECK(std::abs(P.sourceNodes[6].species.value - 3.f) < 0.01f, "SPECIES: seat 7 is a String beast");
        bool wave = true, body = false;
        CHECK(ctl(ed, "WAVE", wave) && !wave, "WAVE is greyed on a beast (it shapes the sine operator only)");
        int shown = 0;
        for (auto &c : ed.testControls())
            if (c.visible && (c.name == "EXCITER LEVEL" || c.name == "STRING 1 DECAY" || c.name == "STIFFNESS")) ++shown;
        CHECK(shown == 3 && ctl(ed, "BODY", body) && body, "BODY shows the String oscillator's own knobs, by their own names");
        checkControls(ed, "beast panel");
        ed.testPickSpecies(1, 5); // seat 2: ALIAS
        ed.testPickSpecies(6, 3);
        for (int sIdx : {1, 6}) { ed.setOnce(P.mixerNodes[(size_t)sIdx].active, 1.f); ed.setOnce(P.mixerNodes[(size_t)sIdx].level, 0.6f); }
        playInto(synth, ed, 3000);
        shot(ed, dir, "08-beast");
        synth.voiceManager->allSoundsOff();
        ed.testPickSpecies(6, 0);
        ed.testIdle();
        CHECK(ctl(ed, "WAVE", wave) && wave && ctl(ed, "BODY", body) && !body, "back to a sine: WAVE wakes, BODY is greyed");
        ed.testPickSpecies(1, 0);
    }
    else
        std::printf("note: BEAST is not in this build: beast checks skipped\n");

    // SHADE: the wavetable species
    {
        using SN = ss::Patch::SourceNode;
        ed.selectSpirit(1);
        ed.setOnce(P.sourceNodes[1].active, 1.f);
        ed.testPickSpecies(1, SN::shadeSpecies);
        ed.testIdle();
        bool a = false, b = true, c = true;
        CHECK((int)std::round(P.sourceNodes[1].species.value) == SN::shadeSpecies && ctl(ed, "TABLE", a) && a && ctl(ed, "FRAME", a) && a && ctl(ed, "UNISON", a) && a,
              "SPECIES = Wavetable: the BODY tab shows the shade's own knobs (TABLE, FRAME, UNISON ...)");
        CHECK(ctl(ed, "MORPH", b) && !b && ctl(ed, "WARP", c) && !c, "MORPH and WARP are greyed while their types are Off");
        ed.setOnce(P.sourceNodes[1].beastP[2], 0.5f);
        ed.setOnce(P.sourceNodes[1].beastP[4], 1.f / 6.f);
        ed.setOnce(P.sourceNodes[1].beastP[1], 0.6f);
        ed.testIdle();
        CHECK(ctl(ed, "MORPH", b) && b && ctl(ed, "WARP", c) && c, "pick a morph type and a warp type: MORPH and WARP wake");
        checkControls(ed, "shade panel");
        tb.mouseDoubleClick(mev(tb, ed.testSeatPos(1), ed.testSeatPos(1), false));
        dragOn(tb, ed.testSeatPos(0), ed.testSeatPos(1));
        ed.selectSpirit(1);
        ed.testEnvTab(3);
        playInto(synth, ed, 2500);
        shot(ed, dir, "09-shade");
        synth.voiceManager->allSoundsOff();
    }

    // a shade's own table from a file in Vital's format (a table of ours, written here)
    {
        const std::string mine = R"({"groups":[{"components":[{"keyframes":[{"num_points":3,"points":[0.0,1.0,0.25,-1.0,1.0,1.0],"position":0,"powers":[0.0,0.0,0.0]},{"num_points":3,"points":[0.0,1.0,0.85,-1.0,1.0,1.0],"position":256,"powers":[0.0,0.0,0.0]}],"num_points":3,"type":1}]}]})";
        ed.selectSpirit(1);
        ed.testEnvTab(3);
        bool a = false, b = true;
        CHECK(ctl(ed, "TABLE FILE", a) && a && ctl(ed, "TABLE", b) && b, "a shade shows TABLE FILE (where WAVE is on a sine spirit); the TABLE knob is live on built-in tables");
        CHECK(ed.testLoadShadeTable(1, "My Ramp", mine) && synth.patchMain.shadeTables[1].name == "My Ramp", "a Vital-format table file loads into the seat");
        ed.testIdle();
        CHECK(ctl(ed, "TABLE", b) && !b, "with a file loaded the TABLE knob is greyed (it only picks built-in tables)");
        CHECK(!ed.testLoadShadeTable(1, "junk", "not a table") && synth.patchMain.shadeTables[1].json.empty(), "a file that is not a table is refused; the seat goes back to built-in");
        ed.testLoadShadeTable(1, "My Ramp", mine);
        ed.testIdle();
        {   // a .wav wavetable made here: 4 cycles of 2048, 16-bit
            const int cyc = 2048, nf = 4;
            std::vector<unsigned char> wav(44 + (size_t)cyc * nf * 2, 0);
            auto w32 = [&](size_t o, uint32_t v) { for (int i = 0; i < 4; ++i) wav[o + (size_t)i] = (unsigned char)(v >> (8 * i)); };
            auto w16 = [&](size_t o, uint16_t v) { wav[o] = (unsigned char)v; wav[o + 1] = (unsigned char)(v >> 8); };
            memcpy(&wav[0], "RIFF", 4); w32(4, (uint32_t)wav.size() - 8); memcpy(&wav[8], "WAVEfmt ", 8); w32(16, 16); w16(20, 1); w16(22, 1);
            w32(24, 44100); w32(28, 88200); w16(32, 2); w16(34, 16); memcpy(&wav[36], "data", 4); w32(40, (uint32_t)cyc * nf * 2);
            for (int f = 0; f < nf; ++f)
                for (int i = 0; i < cyc; ++i)
                    w16(44 + ((size_t)f * cyc + (size_t)i) * 2, (uint16_t)(int16_t)std::lround(20000.0 * std::sin(6.283185307 * i / cyc * (1 + f))));
            CHECK(ed.testLoadWav(1, "Test Wav", wav.data(), wav.size()) && synth.patchMain.shadeTables[1].name == "Test Wav", "a .wav wavetable loads into the seat");
            CHECK(!ed.testLoadWav(1, "junk", "this is not a wav file, only a line of text, long enough", 56), "bytes that are not a wav are refused");
            ed.testLoadShadeTable(1, "My Ramp", mine);
            ed.testIdle();
        }
        checkControls(ed, "shade panel with a table file");
        shot(ed, dir, "10-shade-table-file");
    }

    // the shade through the engine on this machine (the Mac takes a different FFT path inside Vital's code than Linux)
    {
        auto s2 = std::make_unique<ss::Synth>(false);
        s2->setSampleRate(48000);
        auto &p2 = s2->patch;
        for (auto &m : p2.mixerNodes) m.active = 0;
        p2.mixerNodes[0].active = 1; p2.mixerNodes[0].level = 1;
        p2.sourceNodes[0].active = 1; p2.sourceNodes[0].species = (float)ss::Patch::SourceNode::shadeSpecies;
        bool fin = true; double e = 0; int zc = 0, cnt = 0; float prev = 0, pk = 0;
        for (int pass = 0; pass < 3; ++pass)
        { // plain table, then a spectral morph (the FFT path), then a warp
            if (pass == 1) { p2.sourceNodes[0].beastSet = 1; for (int k = 0; k < 7; ++k) p2.sourceNodes[0].beastP[k] = shade_param_default01(k); p2.sourceNodes[0].beastP[2] = 6.f / 9.f; p2.sourceNodes[0].beastP[3] = 0.3f; }
            if (pass == 2) { p2.sourceNodes[0].beastP[2] = 0; p2.sourceNodes[0].beastP[4] = 1.f / 6.f; p2.sourceNodes[0].beastP[5] = 0.7f; }
            s2->voiceManager->processNoteOnEvent(0, 0, 57, -1, 0.9f, 0.f);
            size_t bp = 0;
            for (int i = 0; i < 48000; ++i)
            {
                if (bp == 0) s2->process(nullptr);
                const float y = s2->output[0][bp];
                fin &= std::isfinite(y); pk = std::max(pk, std::fabs(y));
                if (pass == 0 && i > 9600) { e += (double)y * y; if (prev <= 0 && y > 0) zc++; cnt++; }
                prev = y;
                if (++bp == s2->blockSize) bp = 0;
            }
            s2->voiceManager->processNoteOffEvent(0, 0, 57, -1, 0.9f);
            size_t bq = 0;
            for (int i = 0; i < 24000; ++i) { if (bq == 0) s2->process(nullptr); if (++bq == s2->blockSize) bq = 0; }
        }
        const double hz = zc * 48000.0 / std::max(1, cnt), r = std::sqrt(e / std::max(1, cnt));
        std::printf("shade: %.1f Hz, rms %.3f, peak %.2f\n", hz, r, pk);
        CHECK(fin && std::fabs(hz - 220.0) < 3.0 && r > 0.02 && pk < 2.0f, "a shade plays in tune and in range on this machine (plain, with a spectral morph, with a warp)");
    }

    // THROAT: a Surge filter on a spirit
    {
        ed.selectSpirit(8);
        ed.setOnce(P.sourceNodes[8].active, 1.f);
        ed.testEnvTab(4);
        ed.testIdle();
        bool a = true, b = false;
        CHECK(ctl(ed, "THROAT", b) && b && ctl(ed, "CUTOFF", a) && !a, "THROAT tab: the filter picker is live, CUTOFF is greyed while no filter is picked");
        ed.setOnce(P.throatNodes[8].kind, 75.f);
        ed.testIdle();
        CHECK(ctl(ed, "CUTOFF", a) && a && ctl(ed, "RESONANCE", b) && b, "a filter picked: CUTOFF and RESONANCE wake");
        checkControls(ed, "throat tab");
        playInto(synth, ed, 1500);
        shot(ed, dir, "08-throat");
        synth.voiceManager->allSoundsOff();
        ed.testEnvTab(0);
    }

    // MOVE: the spirit's LFO and PITCH envelope sweep its filter and, on a shade, one BODY knob
    {
        ed.selectSpirit(5); // a plain sine spirit with no filter
        ed.setOnce(P.sourceNodes[5].active, 1.f);
        ed.setOnce(P.sourceNodes[5].species, 0.f);
        ed.setOnce(P.throatNodes[5].kind, 0.f);
        ed.testEnvTab(5);
        ed.testIdle();
        bool a = false, b = true, c = true;
        CHECK(ctl(ed, "LFO RATE", a) && a && ctl(ed, "LFO > CUTOFF", b) && !b && ctl(ed, "LFO > FRAME", c) && !c,
              "MOVE on a sine spirit with no filter: LFO RATE is live, the cutoff and wavetable sweeps are greyed");
        ed.setOnce(P.throatNodes[5].kind, 75.f);
        ed.testIdle();
        CHECK(ctl(ed, "LFO > CUTOFF", b) && b && ctl(ed, "ENV > CUTOFF", c) && c, "with a filter picked the cutoff sweeps wake");
        ed.selectSpirit(1); // the shade
        ed.testEnvTab(5);
        ed.setOnce(P.throatNodes[1].bodyTarget, 2.f);
        ed.testIdle();
        CHECK(ctl(ed, "LFO > WARP", a) && a && ctl(ed, "ENV > WARP", b) && b && ctl(ed, "MOVES", c) && c, "on a shade the wavetable sweeps are live and follow MOVES (here WARP)");
        ed.setOnce(P.throatNodes[1].lfoToBody, 0.4f);
        ed.setOnce(P.throatNodes[1].bodyTarget, 0.f);
        {   // a Surge spirit: MOVES offers its own knobs
            ed.selectSpirit(5);
            ed.setOnce(P.sourceNodes[5].species, 1.f);
            ed.setOnce(P.throatNodes[5].bodyTarget, 0.f);
            ed.testEnvTab(5);
            ed.testIdle();
            bool x = false, y = false, z = false;
            CHECK(ctl(ed, "MOVES", x) && x && ctl(ed, "LFO > SHAPE", y) && y && ctl(ed, "ENV > SHAPE", z) && z, "on a Surge spirit MOVES is live and the sweeps push its first knob (SHAPE)");
            ed.setOnce(P.sourceNodes[5].species, 0.f);
            ed.selectSpirit(1);
            ed.testEnvTab(5);
            ed.testIdle();
        }
        checkControls(ed, "move tab");
        playInto(synth, ed, 1500);
        shot(ed, dir, "11-move");
        synth.voiceManager->allSoundsOff();
        ed.testEnvTab(0);
    }

    // the pages over the table
    {
        using SB = ghostblock::p2::sub::SubBass;
        ed.showPage(1);
        ed.testIdle();
        CHECK(ed.testPage() == 1 && ed.testPageComponent(1)->isVisible() && !ed.testTable()->isVisible(), "FX opens over the table");
        shot(ed, dir, "06-fx");
        ed.showPage(2);
        ed.testIdle();
        CHECK(ed.testPage() == 2 && ed.testPageComponent(2)->isVisible(), "SUB opens over the table");
        CHECK(!subHeap->on() && !ed.testSubKnobEnabled("LEVEL"), "SUB off: its knobs are greyed");
        ed.subEdit(SB::pOn, 1.0, 0); ed.subEdit(SB::pOn, 1.0, 1); ed.subEdit(SB::pOn, 1.0, 2);
        ed.subEdit(SB::pBlend, 0.0, 0); ed.subEdit(SB::pBlend, 0.0, 1); ed.subEdit(SB::pBlend, 0.0, 2);
        ed.showPage(2);
        CHECK(subHeap->on() && ed.testSubKnobEnabled("LEVEL") && !ed.testSubKnobEnabled("DRIVE"), "SUB on, BLEND at clean: LEVEL is live, DRIVE is greyed (it only shapes the dirty side)");
        ed.subEdit(SB::pBlend, 60.0, 0); ed.subEdit(SB::pBlend, 60.0, 1); ed.subEdit(SB::pBlend, 60.0, 2);
        ed.showPage(2);
        CHECK(ed.testSubKnobEnabled("DRIVE"), "BLEND up: DRIVE wakes");
        shot(ed, dir, "07-sub");
        {   // a sound opens the way it was saved: FX and SUB too
            using SF = ghostblock::p2::spectral::SpectralFx;
            ed.spectralEdit(SF::pOn, 1.0, 0); ed.spectralEdit(SF::pOn, 1.0, 1); ed.spectralEdit(SF::pOn, 1.0, 2);
            const bool fxWasOn = spectralHeap->toState().find("\non 1\n") != std::string::npos;
            const auto f = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("seance-rack-test.sxsnp");
            ed.testSaveSoundTo(f.getFullPathName().toStdString());
            const auto first = ed.testLoadFactory(0); // a sound saved without FX or SUB
            ed.testIdle();
            const bool offNow = spectralHeap->toState().find("\non 0\n") != std::string::npos && !subHeap->on();
            ed.testLoadSoundFrom(f.getFullPathName().toStdString());
            ed.testIdle();
            const bool back = spectralHeap->toState().find("\non 1\n") != std::string::npos && subHeap->on();
            CHECK(fxWasOn && f.existsAsFile() && offNow && back, "a sound saved with FX and SUB on opens with them on; a sound saved without them turns them off");
            if (!(fxWasOn && offNow && back)) std::printf("   on-before %d, off-after-other-sound %d, back-on %d\n", (int)fxWasOn, (int)offNow, (int)back);
            f.deleteFile();
            ed.showPage(2);
        }
        {   // BLEND: four sounds at four corners
            using M = ghostblock::p2::Morph;
            ed.showPage(3);
            ed.testIdle();
            CHECK(ed.testPage() == 3 && ed.testPageComponent(3)->isVisible() && !ed.testTable()->isVisible(), "CROSS opens over the table");
            CHECK(ed.testBlendEnabled("VOLUME A") == 0 && ed.testBlendEnabled("ORBIT") == 0 && ed.testBlendEnabled("CORNER B") == 1, "CROSS off: volumes and ORBIT are greyed, the corners can still be filled");
            int twelve = -1, choir = -1;
            for (int i = 0; i < ed.testFactoryCount(); ++i)
            { // find two sounds by loading names is costly: use the folder order instead (Seance folders exist)
                (void)i;
            }
            twelve = ed.testFactoryCount() - 1;
            choir = ed.testFactoryCount() / 2;
            ed.testBlendCorner(1, twelve);
            ed.testBlendCorner(2, choir);
            ed.testBlendCorner(3, -1);
            ed.morphEdit(M::pOn, 1.0, 0); ed.morphEdit(M::pOn, 1.0, 1); ed.morphEdit(M::pOn, 1.0, 2);
            ed.morphEdit(M::pX, 0.7, 1); ed.morphEdit(M::pY, 0.35, 1);
            ed.showPage(3);
            ed.testIdle();
            CHECK(morphHeap->on.load() && morphHeap->kind[1].load() == M::OWN && morphHeap->kind[3].load() == M::OWN && ed.testBlendEnabled("VOLUME B") == 1 && ed.testBlendEnabled("OCTAVE D") == 1,
                  "CROSS on with corners filled: their volume, pan and octave are live");
            CHECK(ed.testBlendEnabled("ORBIT") == 1 && ed.testBlendEnabled("EVERY") == 0 && ed.testBlendEnabled("SIZE") == 0, "ORBIT off: its speed and size are greyed");
            ed.morphEdit(M::pOrbit, 1.0, 1);
            ed.showPage(3);
            CHECK(ed.testBlendEnabled("EVERY") == 1 && ed.testBlendEnabled("SIZE") == 1, "an orbit picked: speed and size wake");
            float w[4]{};
            const bool any = morphHeap->weights(w);
            CHECK(any && std::fabs(w[0] + w[1] + w[2] + w[3] - 1.f) < 1e-3f && w[1] > w[2], "the flame's place gives each corner its share (they add up to all of it)");
            // each corner's own engine really plays its sound
            bool heardAll = true;
            static clap_output_events sink2{nullptr, [](const clap_output_events *, const clap_event_header_t *) { return true; }};
            for (int c = 1; c < 4; ++c)
            {
                auto *e = morphHeap->owned[(size_t)c].get();
                if (!e) { heardAll = false; continue; }
                for (int b = 0; b < 64; ++b) e->process(&sink2); // (the load itself arrives first and silences the engine)
                for (int k : {45, 57}) e->voiceManager->processNoteOnEvent(0, 0, k, -1, 0.9f, 0.f);
                double sum = 0; bool fin = true;
                for (int b = 0; b < 3000; ++b)
                {
                    e->process(&sink2);
                    for (size_t i = 0; i < e->blockSize; ++i) { const float y = e->output[0][i]; fin = fin && std::isfinite(y); sum += (double)y * y; }
                }
                e->voiceManager->allSoundsOff();
                const double r = std::sqrt(sum / (3000.0 * (double)e->blockSize));
                if (c == 1 && std::string(e->patch.name).empty()) heardAll = false;
                std::printf("   corner %d plays \"%s\": rms %.4f\n", c, e->patchMain.name, r);
                if (!fin || r < 1e-3) { heardAll = false; std::printf("   corner %d: rms %.5f finite %d\n", c, r, (int)fin); }
            }
            if (!heardAll)
            { // is the sound on the table itself audible?
                for (int k : {45, 57}) synth.voiceManager->processNoteOnEvent(0, 0, k, -1, 0.9f, 0.f);
                double sum = 0;
                for (int b = 0; b < 3000; ++b) { synth.process(&sink2); for (size_t i = 0; i < synth.blockSize; ++i) sum += (double)synth.output[0][i] * synth.output[0][i]; }
                synth.voiceManager->allSoundsOff();
                std::printf("   the table's own sound: rms %.5f  name %s\n", std::sqrt(sum / (3000.0 * (double)synth.blockSize)), P.name);
            }
            CHECK(heardAll, "every filled corner's own engine plays its sound");
            shot(ed, dir, "08-blend");
            {   // the ready-made crossroads: every corner finds its sound, and the four corners come out about equally loud
                const int n = ed.testCrossroads(-1);
                int full = 0, matched = 0;
                for (int r = 0; r < n; ++r)
                {
                    const int got = ed.testCrossroads(r);
                    full += got == 4;
                    double lo = 1e9, hi = -1e9;
                    for (int c = 0; c < 4; ++c)
                    {
                        auto *e = morphHeap->owned[(size_t)c].get();
                        if (!e || morphHeap->kind[(size_t)c].load() != M::OWN) continue;
                        for (int b = 0; b < 64; ++b) e->process(&sink2);
                        for (int k : {48, 55, 60}) e->voiceManager->processNoteOnEvent(0, 0, k, -1, 0.85f, 0.f);
                        double sum = 0; size_t cnt = 0;
                        for (int b = 0; b < 48000 * 3 / 2 / (int)e->blockSize; ++b)
                        {
                            e->process(&sink2);
                            for (size_t i = 0; i < e->blockSize; ++i) { const double y = 0.5 * (e->output[0][i] + e->output[1][i]); sum += y * y; ++cnt; }
                        }
                        e->voiceManager->allSoundsOff();
                        for (int b = 0; b < 3000; ++b) e->process(&sink2);
                        const double db = 10.0 * std::log10(std::max(1e-12, sum / (double)std::max<size_t>(1, cnt))) + morphHeap->volDb[(size_t)c].load();
                        lo = std::min(lo, db); hi = std::max(hi, db);
                    }
                    std::printf("   crossroads %-20s corners %d, loudness spread %.1f dB\n", morphHeap->names[0].c_str(), got, hi - lo);
                    matched += (hi - lo) <= 6.0;
                }
                CHECK(n >= 12 && full == n, "every ready-made crossroads loads a sound into all four corners");
                CHECK(matched == n, "in every ready-made crossroads the four corners are within 6 dB of each other");
                CHECK(morphHeap->on.load() && ed.testCrossroadsSaveLoad("Test Crossroads") && morphHeap->kind[0].load() == M::OWN, "a crossroads saves under YOURS and loads back");
                shot(ed, dir, "08b-crossroads");
            }
            ed.morphEdit(M::pOn, 0.0, 1);
        }
        ed.showPage(0);
        CHECK(ed.testTable()->isVisible(), "TABLE brings the table back");
        {   // STEPS: the sequencer page, ArpEZator's MIDNIGHT and DUSK, Seance's own patterns
            using namespace ghostblock::seance;
            using namespace ghostblock::seance::steppat;
            auto &sq = *stepsHeap;
            ed.showPage(4);
            ed.testIdle();
            CHECK(ed.testPage() == 4 && ed.testPageComponent(4)->isVisible(), "STEPS opens over the table");
            CHECK(arp::recipe().mode[arp::Midnight].hasRecipe() && arp::recipe().mode[arp::Dusk].hasRecipe() && !arp::recipe().mode[arp::Midnight].figures.empty(),
                  "ArpEZator's recipe is loaded (its own figure templates and scoring for MIDNIGHT and DUSK)");
            CHECK(ed.testStepsEnabled("PATTERN") == 1 && ed.testStepsEnabled("SPEED") == 0 && ed.testStepsEnabled("SWING") == 0 && ed.testStepsEnabled("ANOTHER") == 0 && ed.testStepsEnabled("LANE ARP") == 0,
                  "STEPS off and empty: the pattern picker is live; speed, swing, ANOTHER and the lane switches are greyed");
            auto pp = std::make_unique<StepPattern>();
            // a MIDNIGHT idea, as ArpEZator writes it
            const bool mOk = ed.testStepsMake(IDEA_MIDNIGHT, 128);
            sq.getPattern(*pp);
            int lanes[4]{0, 0, 0, 0}, lo = 999, hi = -999, outside = 0;
            for (int i = 0; i < pp->n; ++i) { lanes[pp->notes[i].lane & 3]++; lo = std::min(lo, (int)pp->notes[i].off); hi = std::max(hi, (int)pp->notes[i].off); outside += pp->notes[i].t >= pp->loopTicks; }
            std::printf("   %s: %d bars, chords %d arp %d melody %d bass %d, offsets %d..%d, lanes on %x\n", pp->name, pp->loopTicks / 384, lanes[0], lanes[1], lanes[2], lanes[3], lo, hi, sq.laneMask.load());
            CHECK(mOk && pp->ladder == 0 && pp->loopTicks >= 384 * 4 && lanes[0] >= 4 && lanes[1] >= 32 && lanes[2] >= 4 && lanes[3] >= 4 && outside == 0 && lo >= -40 && hi <= 48 && juce::String(pp->name).startsWith("MIDNIGHT"),
                  "a MIDNIGHT idea from ArpEZator's own engine: chords, arp, melody and bass, as offsets from one key");
            // the same seed gives the same idea; another seed another one
            auto p2 = std::make_unique<StepPattern>(), p3 = std::make_unique<StepPattern>();
            ed.testStepsMake(IDEA_MIDNIGHT, 128); sq.getPattern(*p2);
            ed.testStepsMake(IDEA_MIDNIGHT, 9001); sq.getPattern(*p3);
            const bool same = p2->n == pp->n && std::memcmp(p2->notes, pp->notes, sizeof(StepNote) * (size_t)pp->n) == 0;
            const bool other = p3->n != pp->n || std::memcmp(p3->notes, pp->notes, sizeof(StepNote) * (size_t)std::min(p3->n, pp->n)) != 0;
            CHECK(same && other, "the same seed writes the same idea, a new seed a different one");
            CHECK(ed.testStepsEnabled("ANOTHER") == 1 && ed.testStepsEnabled("LANE ARP") == 1 && ed.testStepsEnabled("LANE BASS") == 1, "an idea: ANOTHER and the four lane switches wake");
            sq.on = true;
            ed.showPage(4);
            CHECK(ed.testStepsEnabled("SPEED") == 1 && ed.testStepsEnabled("SWING") == 1 && ed.testStepsEnabled("LATCH") == 1, "STEPS on: speed, swing and LATCH wake");
            shot(ed, dir, "15-steps-midnight");
            // DUSK, and both figures over a held chord
            const bool dOk = ed.testStepsMake(IDEA_DUSK, 77);
            sq.getPattern(*pp);
            int dArp = 0, dMel = 0;
            for (int i = 0; i < pp->n; ++i) { dArp += pp->notes[i].lane == 1; dMel += pp->notes[i].lane == 2; }
            CHECK(dOk && juce::String(pp->name).startsWith("DUSK") && dArp + dMel >= 16, "a DUSK idea from the same engine");
            std::printf("   %s: arp %d, melody %d\n", pp->name, dArp, dMel);
            shot(ed, dir, "16-steps-dusk");
            bool figOk = true;
            for (int k : {(int)FIG_MIDNIGHT, (int)FIG_DUSK})
                for (uint32_t seed : {5u, 6u, 7u})
                {
                    figOk = figOk && ed.testStepsMake(k, seed);
                    sq.getPattern(*pp);
                    int maxIdx = 0;
                    for (int i = 0; i < pp->n; ++i) maxIdx = std::max(maxIdx, pp->notes[i].off & 63);
                    figOk = figOk && pp->ladder == 1 && pp->loopTicks == 384 * 4 && pp->n >= 16 && maxIdx <= 12;
                }
            CHECK(figOk && ed.testStepsEnabled("LANE ARP") == 0, "MIDNIGHT and DUSK arp figures become ladder patterns over the held chord (lane switches greyed: one part)");
            shot(ed, dir, "17-steps-figure");
            // Seance's own lines and arps all parse into notes
            int bad = 0;
            for (size_t i = 0; i < lines().size(); ++i) { if (!ed.testStepsMake(LINE + (int)i, 1)) ++bad; sq.getPattern(*pp); if (pp->n < 4 || pp->ladder) ++bad; }
            for (size_t i = 0; i < heldArps().size(); ++i) { if (!ed.testStepsMake(HELD_ARP + (int)i, 1)) ++bad; sq.getPattern(*pp); if (pp->n < 8 || !pp->ladder) ++bad; }
            CHECK(bad == 0 && ed.testStepsEnabled("ANOTHER") == 0, "every built-in line and held-chord arp makes a pattern (ANOTHER greyed: nothing to re-roll)");
            // drawing: an empty line, click to add, click again to remove
            ed.testStepsMake(EMPTY_KEY, 1);
            ed.testStepsClick(48.f, 7);
            sq.getPattern(*pp);
            const bool added = pp->n == 1 && pp->notes[0].t == 48 && pp->notes[0].off == 7;
            ed.testStepsClick(50.f, 7);
            sq.getPattern(*pp);
            CHECK(added && pp->n == 0, "click an empty square = a note there; click the note = gone");
            {   // the grid's other gestures and tools
                ed.testStepsClick(2.f, 3);                 // a note at the start, row 3
                ed.testStepsDrag(18.f, 3, 90.f, 3);        // its right end out to the fourth sixteenth
                sq.getPattern(*pp);
                const bool longer = pp->n == 1 && pp->notes[0].t == 0 && pp->notes[0].d == 92;
                ed.testStepsDrag(4.f, 3, 100.f, 5);        // the note itself: later and higher
                sq.getPattern(*pp);
                const bool moved = pp->n == 1 && pp->notes[0].t == 96 && pp->notes[0].off == 5 && pp->notes[0].d == 92;
                CHECK(longer && moved, "drag a note's right end = its length; drag the note = it moves in time and pitch");
                ed.testStepsTool(5, 2);                    // DRAW ON: MELODY
                ed.testStepsClick(200.f, 9);
                sq.getPattern(*pp);
                bool mel = false;
                for (int i = 0; i < pp->n; ++i) mel = mel || (pp->notes[i].t == 192 && pp->notes[i].lane == 2);
                CHECK(mel && ed.testStepsEnabled("LANE MELODY") == 1, "DRAW ON picks the part a new note joins; with two parts in use the part switches wake");
                ed.testStepsTool(0, 4);                    // four bars
                ed.testStepsTool(3, 0);                    // bar 1 onto every bar
                sq.getPattern(*pp);
                const bool copied = pp->loopTicks == 384 * 4 && pp->n == 8;
                ed.testStepsTool(1, 0);                    // twice as long, repeated
                sq.getPattern(*pp);
                const bool doubled = pp->loopTicks == 384 * 8 && pp->n == 16;
                ed.testStepsTool(4, 1);                    // clear bar 2
                ed.testStepsTool(0, 2);                    // two bars: the rest goes
                sq.getPattern(*pp);
                CHECK(copied && doubled && pp->loopTicks == 384 * 2 && pp->n == 2, "LOOP length, copy a bar onto every bar, repeat the loop, clear a bar");
                const auto mid = ed.testStepsMidi();
                CHECK(mid.size() > 40 && std::memcmp(mid.data(), "MThd", 4) == 0 && ed.testStepsEnabled("DRAG MIDI") == 1, "DRAG MIDI OUT writes a standard MIDI file of the pattern");
            }
            {   // an idea over YOUR chords, and exact values for a note
                const bool ok = ed.testStepsMake(IDEA_MIDNIGHT, 128, 1);
                sq.getPattern(*pp);
                int ln[4]{0, 0, 0, 0}, nudged = 0, below = 0, maxIdx = 0;
                for (int i = 0; i < pp->n; ++i)
                {
                    const auto &x = pp->notes[i];
                    ln[x.laneOf()]++;
                    nudged += x.nudge() != 0;
                    below += x.off < 0;
                    maxIdx = std::max(maxIdx, x.off & 63);
                }
                std::printf("   %s: chords %d arp %d melody %d bass %d; off a chord tone %d, below the chord %d, tones used %d\n", pp->name, ln[0], ln[1], ln[2], ln[3], nudged, below, maxIdx + 1);
                CHECK(ok && pp->ladder == 1 && pp->kind == IDEA_MIDNIGHT && ln[0] >= 4 && ln[1] >= 32 && ln[2] >= 4 && ln[3] >= 4 && below > 0 && maxIdx <= 6 && ed.testStepsEnabled("CHORDS FROM") == 1,
                      "CHORDS: YOURS re-writes a MIDNIGHT idea over the held chord (same parts, bass under the chord, at most a few chord tones)");
                ed.showPage(4);
                shot(ed, dir, "19-steps-your-chords");
                ed.testStepsMake(IDEA_MIDNIGHT, 128, 0);
                sq.getPattern(*pp);
                const bool backToOwn = pp->ladder == 0;
                ed.testStepsMake(LINE + 0, 1);
                const bool greyed = ed.testStepsEnabled("CHORDS FROM") == 0;
                const bool exact = ed.testStepsExact(0, 50.0, 2.5);
                sq.getPattern(*pp);
                CHECK(backToOwn && greyed && exact && pp->notes[0].vel == 64 && pp->notes[0].d == 60, "CHORDS: ITS OWN puts it back; a line has no chords to swap (greyed); a note takes an exact loudness and length");
            }
            {   // the other three ArpEZator moods
                bool ok = true;
                for (int k : {(int)IDEA_SUNRISE, (int)IDEA_TRAP, (int)IDEA_HIPHOP})
                {
                    ok = ok && ed.testStepsMake(k, 31);
                    sq.getPattern(*pp);
                    int ln[4]{0, 0, 0, 0};
                    for (int i = 0; i < pp->n; ++i) ln[pp->notes[i].lane & 3]++;
                    std::printf("   %s: %d bars, chords %d arp %d melody %d bass %d\n", pp->name, pp->loopTicks / 384, ln[0], ln[1], ln[2], ln[3]);
                    ok = ok && pp->n >= 16 && ln[2] >= 4 && !pp->ladder;
                }
                ok = ok && ed.testStepsMake(FIG_SUNRISE, 31);
                sq.getPattern(*pp);
                CHECK(ok && pp->ladder == 1 && pp->n >= 16, "SUNRISE, TRAP and HIP HOP ideas (and a SUNRISE figure) from the same engine");
            }
            // a sound keeps its sequencer
            ed.testStepsMake(LINE + 5, 1);
            sq.swing = 0.3f;
            const auto f = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("seance-steps-test.sxsnp");
            ed.testSaveSoundTo(f.getFullPathName().toStdString());
            ed.testStepsMake(EMPTY_KEY, 1);
            sq.swing = 0.f; sq.on = false;
            ed.testLoadFactory(0);
            ed.testIdle();
            sq.getPattern(*pp);
            const bool leftAlone = pp->n == 0 && !sq.on.load(); // a factory sound has no sequencer stored: yours stays as it is
            ed.testLoadSoundFrom(f.getFullPathName().toStdString());
            ed.testIdle();
            sq.getPattern(*pp);
            CHECK(leftAlone && pp->n > 8 && sq.on.load() && std::fabs(sq.swing.load() - 0.3f) < 1e-4f && juce::String(pp->name).startsWith("Acid"), "a sound saved with STEPS brings its pattern and settings back; a sound without leaves yours alone");
            f.deleteFile();
            ed.showPage(4);
            shot(ed, dir, "18-steps-line");
            sq.on = false;
            ed.showPage(0);
        }
        {   // the meter and limiter, the sound search, favourites
            CHECK(ed.testLimiterOn(), "the limiter starts on");
            ed.testClickMeter();
            const bool off = !ed.testLimiterOn() && hostDirty.load();
            ed.testClickMeter();
            CHECK(off && ed.testLimiterOn(), "a click on the meter switches the limiter off and on, and the host is told");
            const int twelve = ed.testSearch("twelve"), bells = ed.testSearch("BELL"), none = ed.testSearch("zzzqqq");
            CHECK(twelve >= 10 && bells >= 2 && none == 0, "FIND A SOUND matches names and folders, any case");
            const int before = ed.testFavourite(0);
            ed.testLoadFactory(3);
            const int nowFav = ed.testFavourite(1);
            const int after = ed.testFavourite(0);
            const int gone = ed.testFavourite(1);
            CHECK(after == before + (nowFav ? 1 : -1) && ed.testFavourite(0) == before && gone != nowFav, "a sound goes into favourites and comes out again");
        }
        {   // HANDS / FOLLOWS / MACROS
            ed.selectSpirit(0);
            ed.setOnce(P.sourceNodes[0].active, 1.f);
            ed.setOnce(P.mixerNodes[0].active, 1.f);
            ed.setOnce(P.selfNodes[0].active, 0.f);
            ed.testEnvTab(6);
            ed.testIdle();
            bool a = false, b = true, c = true, d = true;
            CHECK(ctl(ed, "VOLUME FOLLOWS", a) && a && ctl(ed, "VOLUME AMT", b) && !b && ctl(ed, "FEEDBACK FOLLOWS", c) && !c && ctl(ed, "PITCH FOLLOWS", d) && d,
                  "HANDS: VOLUME FOLLOWS is live on a heard spirit, its amount greyed until something is picked; FEEDBACK FOLLOWS greyed with no feedback");
            ed.testFollow(0, 5000);
            ed.testIdle();
            CHECK(ctl(ed, "VOLUME AMT", b) && b && (int)P.mixerNodes[0].modsource[0].value == 5000 && (int)P.mixerNodes[0].modtarget[0].value == 10 && P.mixerNodes[0].moddepth[0].value > 0.4f,
                  "pick VELOCITY: the amount wakes at half, and the engine's own modulation slot is set");
            ed.testFollow(4, 401); // and also: MACRO 2
            ed.testIdle();
            bool e2 = false;
            CHECK(ctl(ed, "VOLUME AMT 2", e2) && e2 && (int)P.mixerNodes[0].modsource[1].value == 401 && (int)P.mixerNodes[0].modtarget[1].value == 10 && (int)P.mixerNodes[0].modsource[0].value == 5000,
                  "a second FOLLOWS beside the first: volume follows velocity and also MACRO 2");
            checkControls(ed, "hands tab");
            shot(ed, dir, "12-hands");
            ed.testFollow(4, 0);
            ed.testFollow(0, 0);
            ed.testIdle();
            CHECK(ctl(ed, "VOLUME AMT", b) && !b && (int)P.mixerNodes[0].modtarget[0].value == 0, "pick NOTHING: it lets go");
            ed.setOnce(P.sourceNodes[4].active, 1.f);
            auto &lk = P.matrixNodes[ss::MatrixIndex::positionForSourceTarget(0, 4)];
            ed.setOnce(lk.active, 1.f);
            ed.selectThread(0, 4);
            ed.testIdle();
            CHECK(ctl(ed, "DEPTH FOLLOWS", a) && a && ctl(ed, "FOLLOW AMT", b) && !b, "a thread has DEPTH FOLLOWS");
            ed.testFollow(3, 400);
            ed.testIdle();
            CHECK(ctl(ed, "FOLLOW AMT", b) && b && (int)lk.modsource[0].value == 400, "the thread's depth follows MACRO 1");
            checkControls(ed, "thread panel with FOLLOWS");
            shot(ed, dir, "13-thread-follows");
            ed.selectCircle();
            ed.testMacroView(true);
            ed.testIdle();
            CHECK(ctl(ed, "MACRO 1", a) && a && ctl(ed, "MACRO 6", b) && b && !ctl(ed, "ATTACK", c) && !ctl(ed, "GLIDE", d), "MACROS: six knobs take the envelope's place in the circle's panel");
            ed.testSetMacroName(0, "Growl");
            ed.testIdle();
            bool g1 = false, g2 = false;
            CHECK(ctl(ed, "GROWL", g1) && g1 && !ctl(ed, "MACRO 1", g2) && std::string(P.macroNames[0].data()) == "Growl", "a macro named Growl shows its name on its knob");
            checkControls(ed, "macros");
            shot(ed, dir, "14-macros");
            ed.testSetMacroName(0, "");
            ed.testIdle();
            ed.testMacroView(false);
            ed.testIdle();
            CHECK(ctl(ed, "ATTACK", c) && ctl(ed, "GLIDE", d) && !ctl(ed, "MACRO 1", a), "MACROS off: the envelope is back");
        }
        {   // SUMMON: a new sound by rule, in each character
            int ok = 0;
            juce::String names;
            for (int st = 0; st < 6; ++st)
            {
                const auto nm = ed.testSummon(st);
                ed.testIdle();
                int heard = 0;
                for (size_t o = 0; o < 12; ++o) heard += P.mixerNodes[o].active.value > 0.5f;
                if (nm.isNotEmpty() && heard >= 1 && juce::String(P.name) == nm) ++ok;
                names += nm + (st < 5 ? ", " : "");
            }
            std::printf("   summoned: %s\n", names.toRawUTF8());
            CHECK(ok == 6, "SUMMON makes a named, audible sound in each of the six characters");
            CHECK(!spectralHeap->on(), "a summoned sound starts with the FX off");
            {   // MUTATE and bring back
                const std::string nameBefore = P.name;
                std::vector<float> before;
                for (auto *pp : P.params) before.push_back(pp->value);
                int okN = 0;
                for (int k = 0; k < 3; ++k) okN += ed.testMutate(k == 2 ? 1.f : 0.35f) ? 1 : 0;
                int changed = 0;
                for (size_t i = 0; i < P.params.size(); ++i) changed += std::fabs(P.params[i]->value - before[i]) > 1e-6f;
                CHECK(okN >= 2 && changed > 3 && nameBefore == P.name, "MUTATE nudges the sound on the table (same name, different values) and it still plays");
                ed.testMutate(0.35f);
                std::vector<float> mid;
                for (auto *pp : P.params) mid.push_back(pp->value);
                const bool back = ed.testBringBack();
                int diffs = 0;
                for (size_t i = 0; i < P.params.size(); ++i) diffs += std::fabs(P.params[i]->value - mid[i]) > 1e-6f;
                CHECK(back && diffs > 0, "Bring back restores the sound from before the last change");
                int okOne = 0;
                for (int b = 0; b < 5; ++b) okOne += ed.testMutate(0.8f, 1 << b) ? 1 : 0;
                const bool voiced = ed.testMutate(1.f, -1);
                CHECK(okOne >= 4 && voiced, "Change only... (depths, tuning, wavetables, filters, shapes) and New voices each give a sound that plays");
                {   // summon around this sound: keep the wiring
                    std::vector<int> wiring;
                    for (auto &m : P.matrixNodes) wiring.push_back(m.active.value > 0.5f);
                    const float r3 = P.sourceNodes[3].ratio.value;
                    const auto nm2 = ed.testSummonAround(2, 1);
                    bool sameWires = true;
                    size_t i = 0;
                    for (auto &m : P.matrixNodes) sameWires = sameWires && wiring[i++] == (int)(m.active.value > 0.5f);
                    CHECK(nm2.isNotEmpty() && sameWires && P.sourceNodes[3].ratio.value == r3 && juce::String(P.name) == nm2, "Summon around this sound keeps the wiring and tuning and makes the rest new");
                }
            }
            checkControls(ed, "after SUMMON");
            playInto(synth, ed, 1500);
            shot(ed, dir, "09-summoned");
        }
    }

    // every factory sound loads into the twelve-seat engine
    const int n = ed.testFactoryCount();
    int bad = 0;
    for (int i = 0; i < n; ++i)
    {
        const auto nm = ed.testLoadFactory(i);
        ed.testIdle();
        if (nm.isEmpty()) ++bad;
    }
    std::printf("factory sounds: %d\n", n);
    CHECK(n > 20 && bad == 0, "every factory sound loads");
    ed.testLoadFactory(n / 3);
    ed.testIdle();
    ed.selectSpirit(0);
    playInto(synth, ed, 3000);
    shot(ed, dir, "05-factory-sound");
    return 0;
}

struct EvList
{
    std::vector<clap_event_note> notes;
    std::vector<const clap_event_header_t *> all;
    static uint32_t size(const clap_input_events *l) { return (uint32_t)((EvList *)l->ctx)->all.size(); }
    static const clap_event_header_t *get(const clap_input_events *l, uint32_t i) { return ((EvList *)l->ctx)->all[i]; }
};
static bool tryPush(const clap_output_events *, const clap_event_header_t *) { return true; }

struct MemIn
{
    std::string d;
    size_t pos{0};
    static int64_t read(const clap_istream *s, void *b, uint64_t n)
    {
        auto *m = (MemIn *)s->ctx;
        const size_t k = std::min<size_t>((size_t)n, m->d.size() - m->pos);
        memcpy(b, m->d.data() + m->pos, k);
        m->pos += k;
        return (int64_t)k;
    }
};

// renders 3 s through the CLAP plugin: notes C3 + G3 from 21 ms, released at `offAt` seconds
struct PhaseReport { float cancelDb{0}, afterDb{0}; int flipped{0}; };
// phase: 0 no crossroads test; 1 two corners, the second a phase-inverted copy of the first, flame half way, FIX off;
// 2 the same with FIX on; 3 only the first corner (the reference level)
static juce::AudioBuffer<float> render(double offAt, bool cross = false, bool withSteps = false, int phase = 0, PhaseReport *rep = nullptr)
{
    clap_host host{};
    host.clap_version = CLAP_VERSION;
    host.name = "seance-tests";
    host.vendor = "ghostblock";
    host.url = "";
    host.version = "1";
    host.get_extension = [](const clap_host *, const char *) -> const void * { return nullptr; };
    host.request_restart = [](const clap_host *) {};
    host.request_process = [](const clap_host *) {};
    host.request_callback = [](const clap_host *) {};
    auto *pl = ss::makePlugin(&host, false);
    CHECK(pl != nullptr, "the plugin is created");
    pl->init(pl);
    CHECK(std::string(pl->desc->id) == "io.ghostblock.seance", "its id is io.ghostblock.seance");
    CHECK(std::string(pl->desc->name) == "Seance", "its name is Seance");
    {   // a new instance opens on "A Supporting Role"; these tests then start from the plain INIT they were written for
        auto *sy = ss::getSynthFromPlugin(pl);
        int heard = 0;
        for (auto &m : sy->patch.mixerNodes) heard += m.active.value > 0.5f;
        CHECK(std::string(sy->patchMain.name) == "A Supporting Role" && std::string(sy->patch.name) == "A Supporting Role" && heard >= 1 && !sy->patchMain.dirty,
              "a new instance opens on the factory sound A Supporting Role (in both copies of the patch)");
        sy->patchMain.resetToInit();
        sy->patch.copyValuesFrom(sy->patchMain);
        sy->postLoad();
    }
    if (withSteps)
    { // STEPS on: one very short note at the start of every beat, played from the held key
        auto *sq = ss::getStepsFromPlugin(pl);
        auto pat = std::make_unique<ghostblock::seance::StepPattern>();
        pat->loopTicks = 96;
        pat->add(0, 8, 0, 120, 1);
        sq->setPattern(*pat);
        sq->on = true;
    }
    const double sr = 48000;
    const uint32_t bs = 256;
    pl->activate(pl, sr, 1, bs);
    pl->start_processing(pl);
    if (phase)
    {
        auto *m = ss::getMorphFromPlugin(pl);
        auto a = std::make_unique<ss::Patch>(); // (on the heap: a patch is large)
        a->resetToInit();
        a->output.release = 0.15f;
        for (auto &mx : a->mixerNodes) mx.release = 0.15f;
        auto b = std::make_unique<ss::Patch>();
        b->copyValuesFrom(*a);
        b->sourceNodes[0].startingPhase = 0.5f; // half a cycle late = the same sine upside down
        m->setOwn(0, a->toState(), "Sine");
        if (phase != 3) m->setOwn(1, b->toState(), "Sine upside down");
        m->x = phase == 3 ? 0.f : 0.5f;
        m->y = 0.f;
        m->on = true;
        ss::getBlendGuardFromPlugin(pl)->on = phase == 2;
    }
    const int total = (int)(sr * 3.0);
    juce::AudioBuffer<float> out(2, total);
    out.clear();
    std::vector<float> inL(bs, 0.f), inR(bs, 0.f), oL(bs), oR(bs);
    EvList ev;
    clap_input_events in{&ev, &EvList::size, &EvList::get};
    clap_output_events o{nullptr, &tryPush};
    ss::Patch ref;
    std::vector<clap_event_param_value> params;
    auto setp = [&](ss::Param &pr, double v) {
        clap_event_param_value pv{};
        pv.header.size = sizeof(pv);
        pv.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        pv.header.type = CLAP_EVENT_PARAM_VALUE;
        pv.param_id = pr.meta.id;
        pv.note_id = -1; pv.port_index = -1; pv.channel = -1; pv.key = -1;
        pv.value = v;
        params.push_back(pv);
    };
    setp(ref.mixerNodes[0].release, 0.15);
    setp(ref.output.release, 0.15);
    // seat 12 heard, possessed by seat 7 (a link that only exists with twelve operators), through host parameters
    setp(ref.sourceNodes[6].active, 1); setp(ref.sourceNodes[11].active, 1);
    setp(ref.mixerNodes[11].active, 1); setp(ref.mixerNodes[11].level, 0.7); setp(ref.mixerNodes[11].release, 0.15);
    auto &lk = ref.matrixNodes[ss::MatrixIndex::positionForSourceTarget(6, 11)];
    setp(lk.active, 1); setp(lk.level, 0.6);
    if (cross)
    { // the crossroads on (corner A = the sound on the table): the plugin's blend path does the mixing
        setp(ref.output.release, 0.15);
        params.back().param_id = 900001;
        params.back().value = 1.0;
    }
    auto note = [&](uint16_t type, int16_t key, uint32_t t) {
        clap_event_note n{};
        n.header.size = sizeof(n);
        n.header.time = t;
        n.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        n.header.type = type;
        n.note_id = -1;
        n.port_index = 0;
        n.channel = 0;
        n.key = key;
        n.velocity = 0.8;
        ev.notes.push_back(n);
    };
    for (int pos = 0; pos < total; pos += (int)bs)
    {
        ev.notes.clear();
        ev.all.clear();
        ev.notes.reserve(8);
        if (pos == 0) for (auto &pv : params) ev.all.push_back(&pv.header);
        if (pos == (int)bs * 4) { note(CLAP_EVENT_NOTE_ON, 48, 0); note(CLAP_EVENT_NOTE_ON, 55, 0); }
        const int off = (int)(sr * offAt);
        if (pos <= off && off < pos + (int)bs) { note(CLAP_EVENT_NOTE_OFF, 48, (uint32_t)(off - pos)); note(CLAP_EVENT_NOTE_OFF, 55, (uint32_t)(off - pos)); }
        for (auto &n : ev.notes) ev.all.push_back(&n.header);
        float *ip[2] = {inL.data(), inR.data()};
        float *op[2] = {oL.data(), oR.data()};
        clap_audio_buffer ib{ip, nullptr, 2, 0, 0}, ob{op, nullptr, 2, 0, 0};
        clap_process p{};
        p.steady_time = pos;
        p.frames_count = bs;
        p.audio_inputs = &ib;
        p.audio_inputs_count = 1;
        p.audio_outputs = &ob;
        p.audio_outputs_count = 1;
        p.in_events = &in;
        p.out_events = &o;
        pl->process(pl, &p);
        for (uint32_t i = 0; i < bs && pos + (int)i < total; ++i)
        {
            out.setSample(0, pos + (int)i, oL[i]);
            out.setSample(1, pos + (int)i, oR[i]);
        }
    }
    if (rep)
    {
        auto *bg = ss::getBlendGuardFromPlugin(pl);
        rep->cancelDb = bg->cancelDb.load(); rep->afterDb = bg->afterDb.load(); rep->flipped = bg->flipped.load();
    }
    pl->stop_processing(pl);
    pl->deactivate(pl);
    pl->destroy(pl);
    return out;
}
static int runAudio(const juce::File &wav)
{
    auto out = render(1.0);
    const double sr = 48000;
    const int total = out.getNumSamples();
    bool nan = false;
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < total; ++i) nan |= !std::isfinite(out.getSample(c, i));
    const float peak = out.getMagnitude(0, (int)sr), tail = out.getMagnitude((int)(sr * 2.5), (int)(sr * 0.5));
    std::printf("peak %.3f  tail after release %.6f\n", peak, tail);
    CHECK(!nan, "no NaN / inf");
    CHECK(peak > 0.05f && peak < 1.0f, "the notes sound, below full scale");
    CHECK(tail < 1e-4f, "silence after the release");
    {
        {   // the phase guard: a sine against the same sine upside down, half and half
            PhaseReport off, onr, solo;
            const int a0 = (int)(sr * 0.3), n = (int)(sr * 0.5); // while the notes are held (the measurement has settled)
            auto rmsOf = [&](juce::AudioBuffer<float> &b) { return b.getRMSLevel(0, a0, n); };
            auto xOff = render(1.0, false, false, 1, &off);
            // (the report is read at the end, after release: take the held part from a second run that stops early)
            PhaseReport offHeld, onHeld;
            render(0.9, false, false, 1, nullptr);
            auto xOn = render(1.0, false, false, 2, &onr);
            auto xSolo = render(1.0, false, false, 3, &solo);
            const float rOff = rmsOf(xOff), rOn = rmsOf(xOn), rSolo = rmsOf(xSolo);
            bool fin = true;
            for (int i = 0; i < xOn.getNumSamples(); ++i) fin = fin && std::isfinite(xOn.getSample(0, i));
            std::printf("phase guard: one corner %.4f, upside-down pair plain %.5f, with the guard %.4f\n", rSolo, rOff, rOn);
            CHECK(rSolo > 0.01f && rOff < rSolo * 0.1f, "two corners playing the same sine upside down cancel in the plain mix (FIX off)");
            CHECK(fin && rOn > rSolo * 0.6f && rOn < rSolo * 1.5f, "FIX CANCELLING turns one over: the pair plays at about the level of one corner");
        }
        {   // what the readout is told, measured while the notes are still held
            PhaseReport plain, fixed;
            render(5.0, false, false, 1, &plain);
            render(5.0, false, false, 2, &fixed);
            std::printf("phase readout: plain %.1f dB, fixed %.1f dB after, flipped %d\n", plain.cancelDb, fixed.afterDb, fixed.flipped);
            CHECK(plain.cancelDb < -10.f && fixed.flipped != 0 && fixed.afterDb > -1.5f, "the readout says the plain pair is cancelling, and that the guard turned a corner over and fixed it");
        }
        auto x = render(1.0, true);
        bool bad = false;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < x.getNumSamples(); ++i) bad |= !std::isfinite(x.getSample(c, i));
        const float pk = x.getMagnitude(0, (int)sr), tl = x.getMagnitude((int)(sr * 2.5), (int)(sr * 0.5));
        std::printf("crossroads on: peak %.3f  tail %.6f\n", pk, tl);
        CHECK(!bad && pk > 0.05f && pk < 1.0f && tl < 1e-4f, "with the crossroads on (the table's sound in corner A) the plugin still plays it, and falls silent");
    }
    {   // STEPS in the plugin: the held keys do not sound themselves, the pattern's notes do (120 bpm with no transport: a note every half second)
        auto x = render(2.0, false, true);
        const int t0 = 256 * 4;
        auto pk = [&](double a, double b) { return x.getMagnitude(0, t0 + (int)(a * sr), (int)((b - a) * sr)); };
        const float n1 = pk(0.0, 0.08), gap1 = pk(0.38, 0.48), n2 = pk(0.5, 0.58), gap2 = pk(0.88, 0.98), n3 = pk(1.0, 1.08), tl = x.getMagnitude((int)(sr * 2.6), (int)(sr * 0.4));
        bool bad = false;
        for (int i = 0; i < x.getNumSamples(); ++i) bad |= !std::isfinite(x.getSample(0, i));
        std::printf("steps on: notes %.3f %.3f %.3f, between them %.4f %.4f, tail %.6f\n", n1, n2, n3, gap1, gap2, tl);
        CHECK(!bad && n1 > 0.03f && n2 > 0.03f && n3 > 0.03f && gap1 < n1 * 0.35f && gap2 < n2 * 0.35f && tl < 1e-4f,
              "STEPS on in the plugin: a held key plays the pattern (a short note each beat, quiet between), and it stops with the key");
    }
    juce::WavAudioFormat fmt;
    wav.deleteFile();
    if (auto w = std::unique_ptr<juce::AudioFormatWriter>(fmt.createWriterFor(new juce::FileOutputStream(wav), sr, 2, 24, {}, 0)))
        w->writeFromAudioSampleBuffer(out, 0, total);
    return 0;
}

int main(int argc, char **argv)
{
#if JUCE_LINUX
    prepareBareX();
#endif
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::String mode = argc > 1 ? argv[1] : "all";
    const juce::File arg(argc > 2 ? juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]) : juce::File::getCurrentWorkingDirectory().getChildFile("seance-shots"));
    if (mode == "shots") runShots(arg);
    else if (mode == "audio") runAudio(arg);
    else { runShots(arg); runAudio(arg.getChildFile("render.wav")); }
    std::printf(fails ? "FAILURES: %d\n" : "ALL PASS\n", fails);
    return fails ? 1 : 0;
}
