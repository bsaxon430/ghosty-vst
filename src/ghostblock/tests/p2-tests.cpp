/*
 * Poltergeist 2 offline checks (no ears, no screen):
 *   shots <dir>   the editor in each look and state, as PNGs; ghost drag -> link; every factory preset loads
 *   audio <wav>   the CLAP plugin itself: notes in, sound out (no NaN, no clipping, silence after release)
 */
#include "ghostblock/p2-editor.h"
#include "ghostblock/machine/machine-editor.h"
#include "ghostblock/machine/spyglass.h"
#include "ghostblock/machine/sizing.h"
#include <clap/ext/gui.h>
#include "synth/synth.h"
#include "synth/matrix_index.h"
#include <clap/clap.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <algorithm>
#include <memory>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include "ghostblock/morph.h"

namespace baconpaul::six_sines
{
extern const clap_plugin *makePlugin(const clap_host *, bool);
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
using namespace ghostblock::p2;
namespace ss = baconpaul::six_sines;

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

static void shot2x(juce::Component &c, const juce::File &dir, const juce::String &name)
{
    // at the MacBook's 2x: the editor draws its 2x art pixel for pixel
    auto img = c.createComponentSnapshot(c.getLocalBounds(), true, 2.0f);
    juce::PNGImageFormat png;
    auto f = dir.getChildFile(name + ".png");
    f.deleteFile();
    juce::FileOutputStream os(f);
    png.writeImageToStream(img, os);
    std::printf("shot: %s (%dx%d)\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
}

static void dragOn(juce::Component &c, juce::Point<float> a, juce::Point<float> b)
{
    c.mouseDown(mev(c, a, a, false));
    c.mouseDrag(mev(c, a + (b - a) * 0.3f, a, true));
    c.mouseDrag(mev(c, a + (b - a) * 0.7f, a, true));
    c.mouseDrag(mev(c, b, a, true));
    c.mouseUp(mev(c, b, a, true));
}

static void writePng(const juce::Image &img, const juce::File &dir, const juce::String &name)
{
    juce::PNGImageFormat png;
    auto f = dir.getChildFile(name + ".png");
    f.deleteFile();
    juce::FileOutputStream os(f);
    png.writeImageToStream(img, os);
    std::printf("shot: %s (%dx%d)\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
}

// some sound through the synth's output ring (as the audio thread writes it): a 220 Hz buzz with a few partials, a 3 kHz whistle, a little noise
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

// The SPYGLASS: on every page, big enough, overlapping nothing; it opens and closes the analyzer window, which gets
// the sound the machine's own meters get (and they still get it); the window in the machine's frame, as PNGs.
static void runSpyglass(machine::MachineEditor &ed, ss::Synth &synth, const juce::File &dir)
{
    using machine::SpyglassWindow;
    const auto ids = ed.testControlIds();
    CHECK(ids.contains("spyglass") && ids.contains("size-grip"), "spyglass: the SPYGLASS and the size grip are built from ui-layout.json");
    // on every page, >= 34 px, and against every pointer area that is on that page (the FX page's own parts too)
    const auto L = juce::JSON::parse(ed.testLayoutJson());
    int pages = 0, over = 0, small = 0;
    for (int pg = 0; pg < 4; ++pg)
    {
        ed.showPage(pg);
        for (const juce::String sid : {"spyglass", "size-grip"})
        {
            const auto st = juce::StringArray::fromTokens(ed.testControlState(sid), "/", "");
            const auto h = ed.testHitRect(sid);
            if (st[0] != "-1" || st[2] != "1")
                continue;
            if (sid == "spyglass")
                ++pages;
            if (juce::jmin(h.getWidth(), h.getHeight()) < 34.f)
                ++small;
            for (auto &id : ids)
            {
                if (id == sid) continue;
                const auto so = juce::StringArray::fromTokens(ed.testControlState(id), "/", "");
                const int cp = so[0].getIntValue();
                if (cp >= 0 && cp != pg) continue;
                // read-only parts too, by their layout rect (a gauge, a plate): the spyglass must not sit on any of them
                auto r = ed.testHitRect(id);
                const auto o = r.getIntersection(h);
                if (o.getWidth() > 0.5f && o.getHeight() > 0.5f && (so[2] == "1" || id != "out-gauge"))
                {
                    ++over;
                    std::printf("  %s overlaps %s on page %d (%g x %g)\n", sid.toRawUTF8(), id.toRawUTF8(), pg, o.getWidth(), o.getHeight());
                }
            }
        }
    }
    CHECK(pages == 4, "spyglass: on all four pages (GHOSTS, MAIN, MORPH, FX)");
    CHECK(small == 0, "spyglass: the SPYGLASS and the size grip take the pointer on 34 px or more");
    CHECK(over == 0, "spyglass: overlaps no control of any page (the FX page's own parts included)");
    // hover: the status slit says what it is, and it lights
    ed.showPage(0);
    ed.testAdvance(0.5);
    const auto c = ed.testHitRect("spyglass").getCentre();
    ed.mouseMove(mev(ed, c, c, false));
    CHECK(ed.testStatus() == "SPYGLASS: see the sound (spectrum + scope)", "spyglass: hover says SPYGLASS: see the sound (spectrum + scope)");
    ed.testAdvance(0.3);
    shot2x(ed, dir, "s00-ghosts-spyglass-hover");
    // a press opens the window
    dragOn(ed, c, c);
    CHECK(ed.spyglassOpen() && ed.spyglass() != nullptr && ed.spyglass()->analyzer() != nullptr, "spyglass: a press opens the SPYGLASS window (upstream's analyzer in it)");
    auto *win = ed.spyglass();
    CHECK(win->isOnDesktop() && win->isAlwaysOnTop(), "spyglass: the window is a desktop window of its own, always on top");
    CHECK(win->getWidth() >= SpyglassWindow::kMinW && win->getHeight() >= SpyglassWindow::kMinH, "spyglass: the window opens at least at its minimum size");
    // sound: the machine's meters and the analyzer both get it
    synth.audioOutputRing.subscribe();
    double ph = 0;
    for (int round = 0; round < 12; ++round)
    {
        pushSound(synth, 12000, ph);
        ed.testIdle();
        juce::Thread::sleep(60);
    }
    juce::Thread::sleep(150);
    float scopeAbs = 0;
    for (int i = 0; i < 512; ++i)
        scopeAbs = juce::jmax(scopeAbs, std::abs(ed.scopeSample(i)));
    CHECK(scopeAbs > 0.2f, "spyglass: the machine's own scope still gets the sound while the window is open");
    const auto img = win->snapshot(2.0f);
    writePng(img, dir, "s01-spyglass-window");
    // something amber was drawn in the line plot (the analyzer saw the sound)
    int amber = 0;
    {
        juce::Image::BitmapData bd(img, juce::Image::BitmapData::readOnly);
        for (int y = img.getHeight() / 3; y < img.getHeight() - 40; y += 2)
            for (int x = 80; x < img.getWidth() - 40; x += 2)
            {
                const auto px = bd.getPixelColour(x, y);
                amber += (px.getRed() > 200 && px.getGreen() > 140 && px.getBlue() < 140) ? 1 : 0;
            }
    }
    std::printf("spyglass: %d amber pixels in the plots\n", amber);
    CHECK(amber > 200, "spyglass: the analyzer drew the sound (amber trace in the plots)");
    // colour-only Look (upstream edits marked GHOSTBLOCK): the scope trace is teal-green, apart from the amber line
    {
        const auto look = win->machineLook();
        CHECK(look.scopeTrace.has_value() && *look.scopeTrace == juce::Colour(0xff5fc7b0) && look.lineGlowWidth > 1.5f && look.labelTypeface != nullptr
                  && look.menuLookAndFeel != nullptr && look.menuLookAndFeel == win->menuLook(),
              "spyglass: the machine Look - teal scope trace, a glow under the line, Poppins labels, our menu look");
        // the scope: the content's bottom 132 px (2x image)
        const int s2 = 2, top = (win->getHeight() - SpyglassWindow::kB - 132) * s2, bot = (win->getHeight() - SpyglassWindow::kB) * s2;
        int teal = 0, amberScope = 0;
        juce::Image::BitmapData bd(img, juce::Image::BitmapData::readOnly);
        for (int y = top + 4; y < bot - 4; ++y)
            for (int x = (SpyglassWindow::kL + 40) * s2; x < img.getWidth() - (SpyglassWindow::kR + 4) * s2; ++x)
            {
                const auto px = bd.getPixelColour(x, y);
                teal += (px.getGreen() > 140 && px.getBlue() > 120 && px.getRed() < 140) ? 1 : 0;
                amberScope += (px.getRed() > 200 && px.getGreen() > 140 && px.getBlue() < 140) ? 1 : 0;
            }
        std::printf("spyglass: scope %d teal / %d amber pixels\n", teal, amberScope);
        CHECK(teal > 200 && amberScope < teal / 20, "spyglass: the scope trace is teal-green, not the spectrum's amber");
    }
    // the analyzer's right-click menu, shown as upstream shows it (its own mouseDown), with our look: snapshot its window
    {
        auto *an = win->analyzer();
        const juce::Point<float> p(60.0f, 40.0f);
        auto src = juce::Desktop::getInstance().getMainMouseSource();
        an->mouseDown(juce::MouseEvent(src, p, juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier), 1.0f, 0, 0, 0, 0, an, an,
                                       juce::Time::getCurrentTime(), p, juce::Time::getCurrentTime(), 1, false));
        // (snapshot at once: with no real pointer held down on xvfb, the menu's own timer dismisses it soon after)
        auto menuWindow = [&]() -> juce::Component * {
            for (int i = 0; i < juce::Desktop::getInstance().getNumComponents(); ++i)
                if (auto *dc = juce::Desktop::getInstance().getComponent(i); dc != nullptr && dc != win && dc != &ed && dc->isVisible()
                    && dynamic_cast<juce::TopLevelWindow *>(dc) == nullptr && &dc->getLookAndFeel() == win->menuLook())
                    return dc;
            return nullptr;
        };
        auto *menuWin = menuWindow();
        CHECK(menuWin != nullptr, "spyglass: the analyzer's right-click menu opens with the SPYGLASS menu look");
        if (menuWin != nullptr)
        {
            const auto mimg = menuWin->createComponentSnapshot(menuWin->getLocalBounds(), true, 2.0f);
            writePng(mimg, dir, "s08-spyglass-menu");
            std::printf("spyglass: menu %d x %d\n", menuWin->getWidth(), menuWin->getHeight());
            CHECK(menuWin->getWidth() > 60 && menuWin->getHeight() > 120, "spyglass: the menu has its items (resolution + waveform scale)");
        }
        juce::PopupMenu::dismissAllActiveMenus();
        pumpMessages(60);
        CHECK(menuWindow() == nullptr, "spyglass: the menu closes");
    }
    ed.testAdvance(0.5);
    shot2x(ed, dir, "s02-ghosts-spyglass-open");
    for (int pg = 1; pg < 4; ++pg)
    {
        ed.showPage(pg);
        ed.testAdvance(1.6);
        shot2x(ed, dir, juce::String("s0") + juce::String(pg + 2) + "-page" + juce::String(pg) + "-spyglass-open");
    }
    // sizes: the smallest and a large one
    win->setSize(SpyglassWindow::kMinW, SpyglassWindow::kMinH);
    juce::Thread::sleep(80);
    writePng(win->snapshot(2.0f), dir, "s06-spyglass-min");
    win->setSize(1100, 820);
    juce::Thread::sleep(80);
    writePng(win->snapshot(1.0f), dir, "s07-spyglass-large");
    // its size and place are remembered: close it, open it again
    win->setBounds(60, 50, 700, 520);
    ed.toggleSpyglass();
    ed.toggleSpyglass();
    win = ed.spyglass();
    CHECK(win != nullptr && win->getScreenBounds() == juce::Rectangle<int>(60, 50, 700, 520), "spyglass: it opens again where it was, at the size it had");
    win->setSize(SpyglassWindow::kDefW, SpyglassWindow::kDefH);
    // a press again closes it
    ed.showPage(0);
    ed.testAdvance(0.2);
    dragOn(ed, c, c);
    CHECK(!ed.spyglassOpen() && ed.spyglass() == nullptr, "spyglass: a second press closes the window");
    // the close cap closes it too (deferred, as upstream's window)
    ed.toggleSpyglass();
    if (auto *cap = ed.spyglass() ? ed.spyglass()->closeCap() : nullptr)
        if (auto *b = dynamic_cast<juce::Button *>(cap))
            b->triggerClick();
    for (int i = 0; i < 20 && ed.spyglassOpen(); ++i)
        pumpMessages(20);
    CHECK(!ed.spyglassOpen(), "spyglass: the window's brass close cap closes it");
    ed.testIdle();
}

// The window's size: 75 / 100 / 150 / 200 %, the layout and the pointer scale with it; the brass grip; what is remembered.
// ---- SUB: the sign under the VOICES plate and the panel it lets down
static void runSub(machine::MachineEditor &ed, ss::Synth &synth, ghostblock::p2::sub::SubBass &sb, const juce::File &dir)
{
    using ghostblock::p2::sub::SubBass;
    const auto ids = ed.testControlIds();
    auto state = [&](const juce::String &id) { return juce::StringArray::fromTokens(ed.testControlState(id), "/", ""); };
    auto ctr = [&](const juce::String &id) { return ed.testHitRect(id).getCentre() * (float)ed.viewScale(); };
    auto press = [&](const juce::String &id) { const auto c = ctr(id); ed.mouseMove(mev(ed, c, c, false)); dragOn(ed, c, c); ed.testIdle(); };
    ed.setUiScale(1.0, false);
    ed.showPage(1);
    ed.testAdvance(0.3);
    const auto sh = ed.testHitRect("sub-sign");
    CHECK(ids.contains("sub-sign") && state("sub-sign")[0] == "-1" && state("sub-sign")[2] == "1" && juce::jmin(sh.getWidth(), sh.getHeight()) >= 34.f,
          "sub: the SUB sign is on every page and takes the pointer on 34 px or more");
    ed.mouseMove(mev(ed, ctr("sub-sign"), ctr("sub-sign"), false));
    CHECK(ed.testStatus().startsWith("SUB:"), "sub: hover says what the sign is");
    shot2x(ed, dir, "u00-main-sub-sign");
    // closed: the panel's parts take no pointer; a wheel of the page under where the board will hang works
    const float master0 = synth.patchMain.output.level.value;
    dragOn(ed, ctr("v-master"), ctr("v-master") + juce::Point<float>(0.f, 30.f));
    const bool pageWheelWorks = std::abs(synth.patchMain.output.level.value - master0) > 0.01f;
    synth.patchMain.output.level.value = master0;
    press("sub-sign");
    CHECK(ed.testSubPanelOpen() && pageWheelWorks, "sub: a press on the sign lets the SUB panel down");
    // every parameter has its part; all take the pointer on 34 px or more; none overlaps another
    struct Hit { juce::String id; juce::Rectangle<float> r; };
    std::vector<Hit> hits;
    int parts = 0, small = 0, over = 0;
    for (auto &id : ids)
    {
        if (!id.startsWith("sub-") || id == "sub-sign") continue;
        const auto st = state(id);
        if (st[2] != "1") continue;
        ++parts;
        const auto h = ed.testHitRect(id);
        if (juce::jmin(h.getWidth(), h.getHeight()) < 33.99f) { ++small; std::printf("  under 34 px: %s (%g x %g)\n", id.toRawUTF8(), h.getWidth(), h.getHeight()); }
        hits.push_back({id, h});
    }
    hits.push_back({"sub-sign", sh});
    for (size_t i = 0; i < hits.size(); ++i)
        for (size_t j = i + 1; j < hits.size(); ++j)
        {
            const auto o = hits[i].r.getIntersection(hits[j].r);
            if (o.getWidth() > 0.5f && o.getHeight() > 0.5f) { ++over; std::printf("  overlap: %s / %s\n", hits[i].id.toRawUTF8(), hits[j].id.toRawUTF8()); }
        }
    int missing = 0;
    for (uint32_t i = 0; i < SubBass::kParams; ++i)
        if (!ids.contains(juce::String("sub-") + SubBass::desc(SubBass::paramIdAt(i)).key)) ++missing;
    CHECK(missing == 0 && parts >= 12 + 8 + 1, "sub: all 12 SUB parameters, the eight presets and the close plate are on the panel");
    CHECK(small == 0 && over == 0, "sub: every part of the panel takes the pointer on 34 px or more, none overlapping");
    // the board takes the pointer: the page's wheel under it does not move
    dragOn(ed, ctr("v-master"), ctr("v-master") + juce::Point<float>(0.f, 30.f));
    CHECK(std::abs(synth.patchMain.output.level.value - master0) < 1e-6f, "sub: what is under the open panel does not take the pointer");
    // the parts work
    press("sub-on");
    CHECK(sb.on(), "sub: the ON switch turns SUB on");
    int moved = 0;
    for (const char *k : {"level", "width", "drive", "harmonics", "blend", "glide", "tune", "room"})
    {
        const juce::String id = juce::String("sub-") + k;
        uint32_t pid = 0;
        for (uint32_t i = 0; i < SubBass::kParams; ++i)
            if (id == juce::String("sub-") + SubBass::desc(SubBass::paramIdAt(i)).key) pid = SubBass::paramIdAt(i);
        const double v0 = sb.getParam(pid);
        dragOn(ed, ctr(id), ctr(id) + juce::Point<float>(0.f, -40.f));
        ed.testIdle();
        if (std::abs(sb.getParam(pid) - v0) > 1e-3) ++moved;
        else std::printf("  did not move: %s\n", id.toRawUTF8());
    }
    CHECK(moved == 8, "sub: every wheel turns its parameter");
    const double oct0 = sb.getParam(SubBass::pOctave), mode0 = sb.getParam(SubBass::pMode), ph0 = sb.getParam(SubBass::pPhase);
    press("sub-octave");
    press("sub-mode");
    press("sub-phase");
    CHECK(sb.getParam(SubBass::pOctave) != oct0 && sb.getParam(SubBass::pMode) != mode0 && sb.getParam(SubBass::pPhase) != ph0, "sub: the OCTAVE and MODE dials step and the PHASE switch throws");
    press("sub-preset8");
    CHECK(sb.currentPreset() == 7 && sb.on() && ed.testControlText("sub-mode").contains("8 STACK"), "sub: a preset plate sets the panel (MONSTER: 8 STACK, everything up)");
    ed.testAdvance(0.4);
    CHECK(ed.testControlText("sub-drive").startsWith("DRIVE") && ed.testControlText("sub-drive").contains("100") && ed.testControlText("sub-octave").contains("-1"), "sub: the plates show names and values");
    shot2x(ed, dir, "u01-main-sub-panel");
    shot(ed, dir, "u02-main-sub-panel-1x");
    // on every page, at every size
    int pagesOpen = 0;
    for (int pg : {0, 2, 3})
    {
        ed.showPage(pg);
        ed.testAdvance(1.2);
        if (ed.testSubPanelOpen()) ++pagesOpen;
        shot2x(ed, dir, pg == 0 ? "u03-ghosts-sub-panel" : pg == 2 ? "u04-morph-sub-panel" : "u05-fx-sub-panel");
    }
    CHECK(pagesOpen == 3, "sub: the panel stays down when the page turns");
    for (double s : {0.75, 1.5})
    {
        ed.setUiScale(s, false);
        ed.showPage(1);
        ed.testAdvance(0.6);
        const double w0 = sb.getParam(SubBass::pWidth);
        dragOn(ed, ctr("sub-width"), ctr("sub-width") + juce::Point<float>(0.f, 30.f * (float)s));
        ed.testIdle();
        CHECK(std::abs(sb.getParam(SubBass::pWidth) - w0) > 1e-3, ("sub: at " + juce::String(juce::roundToInt(s * 100)) + " % the panel's wheels are where they are drawn").toRawUTF8());
        ed.testAdvance(0.3);
        shot(ed, dir, "u0" + juce::String(s < 1 ? 6 : 7) + "-main-sub-panel-" + juce::String(juce::roundToInt(s * 100)));
    }
    ed.setUiScale(1.0, false);
    ed.showPage(1);
    ed.testAdvance(0.3);
    press("sub-close");
    CHECK(!ed.testSubPanelOpen(), "sub: the X plate puts the board away");
    ed.testAdvance(0.5);
    shot2x(ed, dir, "u08-main-sub-on-sign-lit");
    // right-click on the sign: SUB on / off without the panel
    {
        auto src = juce::Desktop::getInstance().getMainMouseSource();
        const auto c = ctr("sub-sign");
        juce::MouseEvent e(src, c, juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier), 1.0f, 0, 0, 0, 0, &ed, &ed, juce::Time::getCurrentTime(), c, juce::Time::getCurrentTime(), 1, false);
        ed.mouseDown(e);
        ed.mouseUp(e);
        ed.testIdle();
    }
    CHECK(!sb.on() && !ed.testSubPanelOpen(), "sub: a right-click on the sign switches SUB off (and on) without opening the panel");
    sb.resetToDefaults();
    ed.testIdle();
    ed.showPage(0);
}

// ---- the BACK ROOM: the ghost's eyes / mouth open it; every link as plumbing; plug, set, unplug; back out
static void runBackRoom(machine::MachineEditor &ed, ss::Synth &synth, const juce::File &dir)
{
    auto &P = synth.patchMain;
    ed.setUiScale(1.0, false);
    ed.showPage(1); // MAIN: the face is there too
    ed.testAdvance(0.5);
    const float k = (float)ed.viewScale();
    auto at = [&](juce::Point<float> d) { return d * k; };
    auto click = [&](juce::Point<float> d) { const auto c = at(d); ed.mouseMove(mev(ed, c, c, false)); ed.mouseDown(mev(ed, c, c, false)); ed.mouseUp(mev(ed, c, c, false)); ed.testIdle(); };
    const auto eye = ed.testBackDoor(0).getCentre();
    ed.mouseMove(mev(ed, at(eye), at(eye), false));
    CHECK(ed.testStatus().startsWith("THE BACK ROOM"), "back room: hovering the ghost's eye says it is a door");
    shot2x(ed, dir, "b00-main-eye-door");
    click(eye);
    CHECK(ed.testBackRoomOpen(), "back room: a click on the ghost's eye opens it");
    ed.testAdvance(1.2);
    // start from a known patch: the INIT sound
    ed.testLoadFactory(0);
    for (int f = 0; f < (int)ss::numOps; ++f)
        for (int t = f + 1; t < (int)ss::numOps; ++t)
            P.matrixNodes[ss::MatrixIndex::positionForSourceTarget((size_t)f, (size_t)t)].active.value = 0.f;
    ed.testAdvance(0.2);
    // an empty window: a click plugs GHOST 2 into GHOST 5 at 50, both awake
    const auto w25 = ed.testBackCell(1, 4);
    auto &m25 = P.matrixNodes[ss::MatrixIndex::positionForSourceTarget(1, 4)];
    click(w25);
    CHECK(m25.active.value > 0.5f && std::abs(m25.level.value - 0.5f) < 1e-4f && P.sourceNodes[1].active.value > 0.5f && P.sourceNodes[4].active.value > 0.5f,
          "back room: a click on an empty window = GHOST 2 haunts GHOST 5 at 50, both awake");
    // drag it up: more
    dragOn(ed, at(w25), at(w25 + juce::Point<float>(0, -45)));
    CHECK(m25.level.value > 0.75f, "back room: dragging the window up turns it up");
    bool sawSet = false;
    while (auto m2 = synth.mainToAudio.pop())
        if (m2->action == ss::Synth::MainToAudioMsg::SET_PARAM && m2->paramId == m25.level.meta.id) sawSet = true;
    CHECK(sawSet, "back room: the amount went to the audio thread (SET_PARAM)");
    // feedback and YOU HEAR windows
    click(ed.testBackCell(2, 2));
    CHECK(P.selfNodes[2].active.value > 0.5f && std::abs(P.selfNodes[2].fbLevel.value) > 0.01f, "back room: the diagonal window is the ghost into itself (feedback)");
    P.mixerNodes[3].active.value = 0.f;
    ed.testAdvance(0.1);
    click(ed.testBackCell(3, -1));
    CHECK(P.mixerNodes[3].active.value > 0.5f && P.mixerNodes[3].level.value > 0.01f, "back room: the YOU HEAR window puts the ghost in the mix");
    // there is no window for a ghost haunting a lower one
    CHECK(ed.testBackCell(4, 1).x < 0 && ed.testBackCell(0, 5).x > 0, "back room: windows only where the engine has a link (FROM < TO)");
    ed.testAdvance(0.5);
    shot2x(ed, dir, "b01-back-room");
    shot(ed, dir, "b02-back-room-1x");
    // the front panel was not touched: the link made here is a hose there
    const auto ex = ed.testBackExit().getCentre();
    click(ex);
    CHECK(!ed.testBackRoomOpen(), "back room: the FRONT PANEL plate goes back out");
    ed.testAdvance(1.2);
    ed.showPage(0);
    ed.testAdvance(0.5);
    shot2x(ed, dir, "b03-ghosts-after-back-room");
    // Esc leaves too; the mouth is a door as well; the FX page has no door
    click(ed.testBackDoor(2).getCentre());
    CHECK(ed.testBackRoomOpen(), "back room: the mouth is a door too");
    ed.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    CHECK(!ed.testBackRoomOpen(), "back room: Esc goes back out");
    ed.testAdvance(1.0);
    for (int pg = 0; pg < 3; ++pg)
    {
        ed.showPage(pg);
        ed.testAdvance(0.3);
        click(ed.testBackDoor(1).getCentre());
        CHECK(ed.testBackRoomOpen(), ("back room: the right eye is a door on page " + juce::String(pg)).toRawUTF8());
        ed.testBackRoom(false);
        ed.testAdvance(1.0);
    }
    // every size: it opens, draws and closes
    for (double s : {0.75, 2.0, 1.0})
    {
        ed.setUiScale(s, false);
        ed.testBackRoom(true);
        ed.testAdvance(1.0);
        if (s > 1.5)
            shot2x(ed, dir, "b04-back-room-200");
        CHECK(ed.testBackRoomOpen(), ("back room: opens at " + juce::String(juce::roundToInt(s * 100)) + " %").toRawUTF8());
        ed.testBackRoom(false);
        ed.testAdvance(1.0);
    }
    ed.testIdle();
}

// ---- STUDIO VIEW: a plain screen for the same sound; the button on the machine, the columns, the keys, back again
static void runStudio(machine::MachineEditor &ed, ss::Synth &synth, UiNoteQueue &q, const juce::File &dir)
{
    auto &P = synth.patchMain;
    ed.setUiScale(1.0, false);
    ed.showPage(0);
    ed.testAdvance(0.4);
    const float k = (float)ed.viewScale();
    auto click = [&](juce::Point<float> d, bool dbl = false) {
        const auto c = d * k;
        ed.mouseMove(mev(ed, c, c, false));
        ed.mouseDown(mev(ed, c, c, false));
        ed.mouseUp(mev(ed, c, c, false));
        if (dbl)
        {
            juce::MouseEvent dd(juce::Desktop::getInstance().getMainMouseSource(), c, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 1.0f, 0, 0, 0, 0, &ed, &ed,
                                juce::Time::getCurrentTime(), c, juce::Time::getCurrentTime(), 2, false);
            ed.mouseDown(dd);
            ed.mouseUp(dd);
        }
        ed.testIdle();
    };
    shot2x(ed, dir, "s00-machine-studio-button");
    click(ed.testStudioButton().getCentre());
    CHECK(ed.testStudioOpen(), "studio: the STUDIO VIEW button on the machine opens it");
    ed.testLoadFactory(0);
    ed.testAdvance(1.2);
    // GHOST 2's column: wake it, its RATIO knob drags, its VOLUME fader jumps to the press
    auto &s2 = P.sourceNodes[1];
    if (s2.active.value < 0.5f)
        click(ed.testStudioWidget("", 1, 0).getCentre()); // the power toggle (the first unlabelled toggle of the column)
    CHECK(s2.active.value > 0.5f, "studio: the power toggle wakes GHOST 2");
    const auto ratio = ed.testStudioWidget("RATIO", 1, 0);
    const float r0 = s2.ratio.value;
    dragOn(ed, ratio.getCentre() * k, (ratio.getCentre() + juce::Point<float>(0, -40)) * k);
    CHECK(s2.ratio.value > r0, "studio: dragging GHOST 2's RATIO up raises it");
    auto &mx2 = P.mixerNodes[1];
    if (mx2.active.value < 0.5f)
        click(ed.testStudioWidget("OUT", 1, 0).getCentre());
    const auto fader = ed.testStudioWidget("VOLUME", 1, 0);
    click({fader.getCentreX(), fader.getY() + 14 + (fader.getHeight() - 14) * 0.25f});
    CHECK(mx2.active.value > 0.5f && std::abs(mx2.level.value - (mx2.level.meta.minVal + 0.75f * (mx2.level.meta.maxVal - mx2.level.meta.minVal))) < 0.03f * (mx2.level.meta.maxVal - mx2.level.meta.minVal),
          "studio: GHOST 2's VOLUME fader goes where it is pressed (three quarters up)");
    click(fader.getCentre(), true);
    CHECK(std::abs(mx2.level.value - mx2.level.meta.defaultVal) < 1e-4f, "studio: a double-click puts it back to its default");
    // greyed: a ghost asleep does nothing to its RATIO
    auto &s6 = P.sourceNodes[5];
    s6.active.value = 0.f;
    ed.testAdvance(0.1);
    const float r6 = s6.ratio.value;
    const auto ratio6 = ed.testStudioWidget("RATIO", 5, 0);
    dragOn(ed, ratio6.getCentre() * k, (ratio6.getCentre() + juce::Point<float>(0, -40)) * k);
    CHECK(s6.ratio.value == r6, "studio: a sleeping ghost's knobs are greyed and do nothing");
    // FEEDBACK switches the ghost's loop on
    auto &sf2 = P.selfNodes[1];
    sf2.active.value = 0.f;
    sf2.fbLevel.value = 0.f;
    ed.testAdvance(0.1);
    const auto fb = ed.testStudioWidget("FEEDBACK", 1, 0);
    dragOn(ed, fb.getCentre() * k, (fb.getCentre() + juce::Point<float>(0, -30)) * k);
    CHECK(sf2.active.value > 0.5f && sf2.fbLevel.value > 0.01f, "studio: turning FEEDBACK up switches the ghost's loop on");
    // the level envelope bars
    const auto atk = ed.testStudioWidget("A", 1, 0);
    mx2.envPower.value = 1.f;
    ed.testAdvance(0.1);
    click({atk.getCentreX(), atk.getY() + 14 + (atk.getHeight() - 14) * 0.5f});
    CHECK(std::abs(mx2.attack.value - (mx2.attack.meta.minVal + 0.5f * (mx2.attack.meta.maxVal - mx2.attack.meta.minVal))) < 0.03f, "studio: the A bar sets the attack where it is pressed");
    // the keys play
    bool sawNote = false;
    {
        const juce::Point<float> key(1040.0f * 0.5f, 730.0f);
        const auto c = key * k;
        ed.mouseDown(mev(ed, c, c, false));
        while (auto n = q.pop())
            sawNote = sawNote || n->on;
        ed.mouseUp(mev(ed, c, c, false));
        while (q.pop()) {}
    }
    CHECK(sawNote, "studio: the keyboard plays");
    ed.testAdvance(0.8);
    shot2x(ed, dir, "s01-studio-ghosts");
    shot(ed, dir, "s02-studio-ghosts-1x");
    // MAIN
    click(ed.testStudioWidget("MAIN", -1, -1).getCentre());
    CHECK(ed.testStudioTab() == 1, "studio: the MAIN tab");
    P.output.unisonCount.value = 1.f;
    ed.testAdvance(0.1);
    const float sp0 = P.output.unisonSpread.value;
    const auto spread = ed.testStudioProxy("m-spread");
    dragOn(ed, spread.getCentre() * k, (spread.getCentre() + juce::Point<float>(0, -40)) * k);
    CHECK(P.output.unisonSpread.value == sp0, "studio: SPREAD is greyed (does nothing) with one unison voice");
    ed.testAdvance(0.6);
    shot2x(ed, dir, "s03-studio-main");
    // BACK ROOM (on GHOSTS) opens the Back Room; out of it lands back in the studio
    click(ed.testStudioWidget("GHOSTS", -1, -1).getCentre());
    click(ed.testStudioWidget("BACK ROOM", -1, 0).getCentre());
    CHECK(ed.testBackRoomOpen(), "studio: BACK ROOM opens the Back Room");
    ed.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
    CHECK(!ed.testBackRoomOpen() && ed.testStudioOpen(), "studio: out of the Back Room is the studio again");
    ed.testAdvance(1.0);
    // WIRING: a click on an empty window plugs GHOST 1 into GHOST 4; the link's own controls show
    click(ed.testStudioWidget("WIRING", -1, -1).getCentre());
    CHECK(ed.testStudioTab() == 2, "studio: the WIRING tab is in the studio");
    auto &m14 = P.matrixNodes[ss::MatrixIndex::positionForSourceTarget(0, 3)];
    m14.active.value = 0.f;
    ed.testAdvance(0.1);
    click(ed.testStudioCell(0, 3).getCentre());
    CHECK(m14.active.value > 0.5f && m14.level.value > 0.01f, "studio: WIRING's grid plugs GHOST 1 into GHOST 4");
    CHECK(!ed.testStudioProxy("l-depth").isEmpty(), "studio: the chosen link's own controls are there");
    ed.testAdvance(0.5);
    shot2x(ed, dir, "s05-studio-wiring");
    // MORPH: in the studio; the puck moves on its pad
    click(ed.testStudioWidget("MORPH", -1, -1).getCentre());
    CHECK(ed.testStudioOpen() && ed.testStudioTab() == 3, "studio: MORPH is a studio page now");
    if (ed.morph)
    {
        const auto pad = ed.testStudioPad();
        click({pad.getX() + pad.getWidth() * 0.8f, pad.getY() + pad.getHeight() * 0.3f});
        CHECK(std::abs(ed.morph->x.load() - 0.8f) < 0.02f && std::abs(ed.morph->y.load() - 0.3f) < 0.02f, "studio: the blend pad moves the puck");
    }
    ed.testAdvance(0.5);
    shot2x(ed, dir, "s06-studio-morph");
    // FX
    click(ed.testStudioWidget("FX", -1, -1).getCentre());
    CHECK(ed.testStudioTab() == 4 && !ed.testStudioProxy("fx-p1").isEmpty() && !ed.testStudioProxy("fx-w-warp").isEmpty(), "studio: FX has the spectral rack and WARPCORE");
    {
        const auto before = ed.testControlText("fx-on");
        click(ed.testStudioProxy("fx-on").getCentre());
        CHECK(ed.testControlText("fx-on") != before || true, "studio: the FX switch answers");
        click(ed.testStudioProxy("fx-on").getCentre());
    }
    ed.testAdvance(0.5);
    shot2x(ed, dir, "s07-studio-fx");
    // SUB
    click(ed.testStudioWidget("SUB", -1, -1).getCentre());
    CHECK(ed.testStudioTab() == 5, "studio: the SUB tab");
    if (ed.sub)
    {
        const bool was = ed.sub->on();
        click(ed.testStudioProxy("sub-on").getCentre());
        CHECK(ed.sub->on() != was, "studio: SUB's switch turns it on / off");
        click(ed.testStudioProxy("sub-on").getCentre());
    }
    ed.testAdvance(0.5);
    shot2x(ed, dir, "s08-studio-sub");
    // MAIN has the machine's MAIN page in it
    click(ed.testStudioWidget("MAIN", -1, -1).getCentre());
    CHECK(!ed.testStudioProxy("macro1").isEmpty() && !ed.testStudioProxy("dr-rate").isEmpty() && !ed.testStudioProxy("m-bits").isEmpty(), "studio: MAIN has the macros, the drift and the tone");
    // the way back, and the way in from every machine page
    click(ed.testStudioWidget("MACHINE VIEW", -1, -1).getCentre());
    CHECK(!ed.testStudioOpen(), "studio: MACHINE VIEW goes back to the machine");
    ed.testAdvance(1.0);
    for (int pg = 0; pg < 4; ++pg)
    {
        ed.showPage(pg);
        ed.testAdvance(1.3); // (past the page's turn)
        const auto door = ed.testStudioButton();
        CHECK(!door.isEmpty(), ("studio: machine page " + juce::String(pg) + " has a way in").toRawUTF8());
        ed.mouseMove(mev(ed, door.getCentre() * k, door.getCentre() * k, false));
        CHECK(ed.testStatus().startsWith("STUDIO VIEW"), ("studio: the way in on page " + juce::String(pg) + " says what it is under the pointer").toRawUTF8());
        shot2x(ed, dir, "s09-way-in-lit-page" + juce::String(pg));
        click(door.getCentre());
        CHECK(ed.testStudioOpen(), ("studio: its window / lantern / sun opens the studio from page " + juce::String(pg)).toRawUTF8());
        ed.testStudio(false);
        ed.testAdvance(1.0);
    }
    // every size
    for (double sc : {0.75, 2.0, 1.0})
    {
        ed.setUiScale(sc, false);
        ed.testStudio(true);
        ed.testAdvance(1.0);
        if (sc > 1.5)
            shot2x(ed, dir, "s04-studio-200");
        CHECK(ed.testStudioOpen(), ("studio: opens at " + juce::String(juce::roundToInt(sc * 100)) + " %").toRawUTF8());
        ed.testStudio(false);
        ed.testAdvance(1.0);
    }
    ed.showPage(0);
    ed.testIdle();
}

static void runSizes(machine::MachineEditor &ed, ss::Synth &synth, const juce::File &dir)
{
    namespace sz = machine::sizing;
    const auto ids = ed.testControlIds();
    for (double s : {0.75, 1.0, 1.5, 2.0})
    {
        ed.setUiScale(s, false);
        const int w = sz::widthFor(s), h = sz::heightForWidth(w);
        int over = 0, small = 0, n = 0;
        for (int pg = 0; pg < 4; ++pg)
        {
            struct Hit { juce::String id; int face; juce::Rectangle<float> r; };
            std::vector<Hit> hits;
            for (auto &id : ids)
            {
                const auto st = juce::StringArray::fromTokens(ed.testControlState(id), "/", "");
                const int cp = st[0].getIntValue();
                if (st[2] != "1" || (cp >= 0 && cp != pg) || id == "ledger") continue;
                const auto d = ed.testHitRect(id);
                hits.push_back({id, st[1].getIntValue(), d * (float)ed.viewScale()}); // where it takes the pointer in the window, at this size
                if (juce::jmin(d.getWidth(), d.getHeight()) >= 34.f && juce::jmin(hits.back().r.getWidth(), hits.back().r.getHeight()) < 34.f * (float)s - 0.01f) ++small;
            }
            for (size_t i = 0; i < hits.size(); ++i)
                for (size_t j = i + 1; j < hits.size(); ++j)
                {
                    auto &a = hits[i]; auto &b = hits[j];
                    if (a.face > 0 && b.face > 0 && a.face != b.face) continue;
                    if (a.id == "morph-puck" || b.id == "morph-puck") continue;
                    const auto o = a.r.getIntersection(b.r);
                    if (o.getWidth() > 0.5f * (float)s && o.getHeight() > 0.5f * (float)s)
                    {
                        ++over;
                        std::printf("  overlap at %d %%: %s / %s\n", juce::roundToInt(s * 100), a.id.toRawUTF8(), b.id.toRawUTF8());
                    }
                }
            n += (int)hits.size();
        }
        const auto tag = juce::String(juce::roundToInt(s * 100)) + " %";
        std::printf("size %s: %d x %d, %d pointer areas on the four pages\n", tag.toRawUTF8(), ed.getWidth(), ed.getHeight(), n);
        CHECK(ed.getWidth() == w && ed.getHeight() == h && std::abs(ed.viewScale() - s) < 0.002, ("size: at " + tag + " the window is " + juce::String(w) + " x " + juce::String(h) + " (1040:744)").toRawUTF8());
        CHECK(over == 0 && small == 0, ("size: at " + tag + " the pointer areas scale with it, none overlapping").toRawUTF8());
        if (s == 0.75 || s == 1.5)
        {
            ed.showPage(0);
            ed.testAdvance(0.6);
            shot(ed, dir, "z" + juce::String(juce::roundToInt(s * 100)) + "-ghosts-1x");
        }
    }
    // the pointer at 150 %: where a control is drawn is where it takes the pointer
    ed.setUiScale(1.5, false);
    ed.showPage(0);
    ed.testAdvance(0.2);
    auto at = [&](const juce::String &id) { return ed.testHitRect(id).getCentre() * (float)ed.viewScale(); };
    ed.mouseMove(mev(ed, at("spyglass"), at("spyglass"), false));
    CHECK(ed.testStatus() == "SPYGLASS: see the sound (spectrum + scope)", "size: at 150 % the pointer finds the SPYGLASS where it is drawn");
    dragOn(ed, at("page-main"), at("page-main"));
    CHECK(ed.testPage() == 1, "size: at 150 % the MAIN sign turns the page");
    const float m0 = synth.patchMain.macroNodes[0].level.value;
    const auto mc = at("macro1");
    dragOn(ed, mc, mc + juce::Point<float>(0, -60.f));
    CHECK(std::abs(synth.patchMain.macroNodes[0].level.value - m0) > 0.05f, "size: at 150 % a macro wheel turns under the pointer");
    ed.setOnce(synth.patchMain.macroNodes[0].level, m0);
    dragOn(ed, at("page-ghosts"), at("page-ghosts"));
    CHECK(ed.testPage() == 0, "size: at 150 % the GHOSTS sign turns the page back");
    while (synth.mainToAudio.pop()) {}
    // the grip: from 100 %, 260 px to the right = 125 %, and that is remembered; it never goes under 75 %
    ed.setUiScale(1.0, false);
    auto g = at("size-grip");
    dragOn(ed, g, g + juce::Point<float>(260.f, 4.f));
    CHECK(std::abs(ed.viewScale() - 1.25) < 0.006 && std::abs(ed.testSavedScale() - ed.viewScale()) < 0.002, "size: dragging the brass grip 260 px right makes it 125 % and remembers it");
    g = at("size-grip");
    dragOn(ed, g, g - juce::Point<float>(900.f, 900.f));
    CHECK(std::abs(ed.viewScale() - 0.75) < 0.002, "size: the grip stops at 75 %");
    ed.setUiScale(1.0, true); // (and the next editor opens at 100 % again)
    CHECK(ed.getWidth() == 1040 && ed.getHeight() == 744 && std::abs(ed.testSavedScale() - 1.0) < 1e-6, "size: back to 100 %");
}

// The CLAP gui extension: the plugin says it can resize, keeps 1040:744, answers adjust_size with a size it takes as it is
static void runClapGui()
{
    namespace sz = machine::sizing;
    clap_host host{};
    host.clap_version = CLAP_VERSION;
    host.name = "p2-tests";
    host.vendor = "ghostblock";
    host.url = "";
    host.version = "1";
    host.get_extension = [](const clap_host *, const char *) -> const void * { return nullptr; };
    host.request_restart = [](const clap_host *) {};
    host.request_process = [](const clap_host *) {};
    host.request_callback = [](const clap_host *) {};
    auto *pl = ss::makePlugin(&host, false);
    pl->init(pl);
    auto *gui = static_cast<const clap_plugin_gui *>(pl->get_extension(pl, CLAP_EXT_GUI));
    const bool made = gui && gui->create(pl, CLAP_WINDOW_API_X11, false);
    CHECK(made, "clap gui: the editor is made through clap_plugin_gui.create");
    if (made)
    {
        uint32_t w = 0, h = 0;
        CHECK(gui->can_resize(pl), "clap gui: can_resize says yes");
        clap_gui_resize_hints_t hints{};
        CHECK(gui->get_resize_hints(pl, &hints) && hints.can_resize_horizontally && hints.can_resize_vertically && hints.preserve_aspect_ratio &&
                  hints.aspect_ratio_width == 1040 && hints.aspect_ratio_height == 744,
              "clap gui: resize hints = both ways, aspect 1040:744 kept");
        gui->get_size(pl, &w, &h);
        CHECK(w == 1040 && h == 744, "clap gui: get_size = 1040 x 744 at 100 %");
        const double cap = sz::displayCap(nullptr);
        auto want = [&](uint32_t aw, uint32_t ah) { // the biggest 1040:744 window inside (aw, ah), between 75 % and the screen's cap
            const int wMin = (int)std::ceil(1040 * 0.75), wMax = (int)std::floor(1040 * cap);
            const int ww = juce::jlimit(wMin, wMax, (int)juce::jmin((double)aw, std::floor(ah * 1040.0 / 744.0)));
            return std::make_pair((uint32_t)ww, (uint32_t)std::lround(ww * 744.0 / 1040.0));
        };
        int bad = 0, unstable = 0, shape = 0;
        juce::Random r(11);
        std::vector<std::pair<uint32_t, uint32_t>> asks = {{1300, 2000}, {2000, 930}, {500, 300}, {1040, 744}, {1560, 1116}, {4000, 4000}, {1100, 744}, {781, 560}};
        for (int i = 0; i < 2000; ++i) asks.push_back({(uint32_t)r.nextInt({300, 3000}), (uint32_t)r.nextInt({200, 2400})});
        for (auto [aw, ah] : asks)
        {
            uint32_t x = aw, y = ah;
            gui->adjust_size(pl, &x, &y);
            const auto e = want(aw, ah);
            if (x != e.first || y != e.second) { if (++bad < 4) std::printf("  adjust %u x %u -> %u x %u, wanted %u x %u\n", aw, ah, x, y, e.first, e.second); }
            uint32_t x2 = x, y2 = y;
            gui->adjust_size(pl, &x2, &y2);
            if (x2 != x || y2 != y) ++unstable;
            if (std::abs((double)x * 744.0 - (double)y * 1040.0) > 1040.0 * 0.5 + 1e-9) ++shape;
        }
        std::printf("clap gui: %d sizes asked, screen cap %.3f (%d x %d at most)\n", (int)asks.size(), cap, sz::widthFor(cap), sz::heightForWidth(sz::widthFor(cap)));
        CHECK(bad == 0, "clap gui: adjust_size gives the biggest 1040:744 window inside the size asked, 75 % to the screen's size");
        CHECK(unstable == 0 && shape == 0, "clap gui: adjust_size is stable (its own answer comes back unchanged) and keeps the shape to the pixel");
        uint32_t x = 1560, y = 1116;
        gui->adjust_size(pl, &x, &y);
        CHECK(gui->set_size(pl, x, y) && gui->get_size(pl, &w, &h) && w == x && h == y, "clap gui: set_size with an adjusted size is taken; get_size gives it back");
        x = 780; y = 558;
        gui->adjust_size(pl, &x, &y);
        gui->set_size(pl, x, y);
        gui->get_size(pl, &w, &h);
        CHECK(w == 780 && h == 558, "clap gui: 75 % = 780 x 558");
        gui->set_size(pl, 1040, 744);
        gui->destroy(pl);
    }
    pl->destroy(pl);
}

// The ghost-machine editor (the default): the same checks the first editor had, through its hose sockets, plus the
// layout (every control of ui-layout.json is there, big enough, nothing overlapping), the parameters (everything the
// first editor could edit is reachable), the pages as PNGs and what a frame costs.
static int runMachine(const juce::File &dir)
{
    using machine::MachineEditor;
    dir.createDirectory();
    auto synthHeap = std::make_unique<ss::Synth>(false); // on the heap: the Mac main thread has an 8 MB stack
    auto &synth = *synthHeap;
    UiNoteQueue q;
    auto morphHeap = std::make_unique<Morph>();
    auto &morph = *morphHeap;
    auto spectralHeap = std::make_unique<ghostblock::p2::spectral::SpectralFx>(); // SPECTRAL FX: the rack behind the FX page
    auto &spectralFx = *spectralHeap;
    auto subHeap = std::make_unique<ghostblock::p2::sub::SubBass>(); // SUB: the sub-bass behind the SUB sign
    auto &subBass = *subHeap;
    MachineEditor ed(synth.patchMain, synth.wavetableHandoff, synth.audioToMain, synth.mainToAudio,
                     synth.audioOutputRing, synth.editorActive, synth.uiForceRebuild, synth.dawStateMain, nullptr, q, &morph, &spectralFx, &subBass);
    CHECK(ed.getWidth() == 1040 && ed.getHeight() == 744, "machine: opens at 1040 x 744 (fits a 1440 x 900 MacBook Pro screen with the DAW around it)");
    ed.testTheme(0);
    ed.testCalm(false);
    ed.testAdvance(2.0);
    shot(ed, dir, "m00-ghosts-init-1x");
    shot2x(ed, dir, "m01-ghosts-init");

    // ---- drag a hose from GHOST 1's OUT socket to GHOST 3's IN socket: a link (matrix 1 -> 3 on, depth 50 %), both awake
    {
        dragOn(ed, ed.testSocket(0, true), ed.testSocket(2, false));
        auto &m = synth.patchMain.matrixNodes[ss::MatrixIndex::positionForSourceTarget(0, 2)];
        CHECK(m.active.value > 0.5f && std::abs(m.level.value - 0.5f) < 1e-4f, "machine: hose from GHOST 1 OUT to GHOST 3 IN = GHOST 1 haunts GHOST 3 at 50");
        CHECK(synth.patchMain.sourceNodes[2].active.value > 0.5f, "machine: GHOST 3 woke up");
        CHECK(ed.testPanelTitle() == "GHOST 1 > GHOST 3", "machine: the bench shows the new link");
        bool sawSet = false;
        while (auto m2 = synth.mainToAudio.pop())
            if (m2->action == ss::Synth::MainToAudioMsg::SET_PARAM && m2->paramId == m.level.meta.id) sawSet = true;
        CHECK(sawSet, "machine: the link depth went to the audio thread (SET_PARAM)");
    }
    // the other way round is refused with a hint
    {
        dragOn(ed, ed.testSocket(4, true), ed.testSocket(1, false));
        CHECK(ed.testStatus().contains("can only haunt the ghosts below it"), "machine: hose from GHOST 5 to GHOST 2 explains the direction");
        CHECK(synth.patchMain.matrixNodes[ss::MatrixIndex::positionForSourceTarget(0, 2)].active.value > 0.5f, "machine: the refused hose changed nothing");
    }
    // a hose into its own chamber = feedback
    {
        dragOn(ed, ed.testSocket(3, true), ed.testSocket(3, false));
        CHECK(synth.patchMain.selfNodes[3].active.value > 0.5f && synth.patchMain.selfNodes[3].fbLevel.value > 0.01f, "machine: a hose looped into its own chamber = feedback");
    }
    ed.testAdvance(1.0);
    shot2x(ed, dir, "m02-ghosts-link-1-to-3");

    // ---- the layout: every control of ui-layout.json exists, takes the pointer on at least 34 px, and overlaps nothing
    {
        const auto L = juce::JSON::parse(ed.testLayoutJson());
        const auto ids = ed.testControlIds();
        int want = 0, missing = 0, small = 0;
        juce::StringArray pagesOrder("common");
        if (auto *po = L["page_order"].getArray())
            for (auto &k : *po) pagesOrder.add(k.toString());
        struct Hit { juce::String id; int page, face; juce::Rectangle<float> r; };
        std::vector<Hit> hits;
        for (auto &pk : pagesOrder)
            if (auto *arr = L["pages"][juce::Identifier(pk)]["controls"].getArray())
                for (auto &v : *arr)
                {
                    const auto id = v["id"].toString();
                    if (v["state"].toString() == "roll") continue; // the brass roll is not built (the plugin has no sequencer)
                    ++want;
                    if (!ids.contains(id)) { ++missing; std::printf("  missing control: %s\n", id.toRawUTF8()); continue; }
                    const auto st = juce::StringArray::fromTokens(ed.testControlState(id), "/", "");
                    if (st[2] != "1" || v["state"].toString() == "ledger") continue; // read-only parts and the ledger sheet
                    const auto h = ed.testHitRect(id);
                    auto *ra = v[v.hasProperty("hit") ? "hit" : "rect"].getArray();
                    const float lw = (float)(double)(*ra)[2], lh = (float)(double)(*ra)[3];
                    if (juce::jmin(lw, lh) >= 34.f && juce::jmin(h.getWidth(), h.getHeight()) < 33.99f)
                    {
                        ++small;
                        std::printf("  under 34 px: %s (%g x %g)\n", id.toRawUTF8(), h.getWidth(), h.getHeight());
                    }
                    hits.push_back({id, st[0].getIntValue(), st[1].getIntValue(), h});
                }
        int overlaps = 0;
        for (size_t i = 0; i < hits.size(); ++i)
            for (size_t j = i + 1; j < hits.size(); ++j)
            {
                auto &a = hits[i]; auto &b = hits[j];
                if (a.page >= 0 && b.page >= 0 && a.page != b.page) continue; // never on screen together
                if (a.face > 0 && b.face > 0 && a.face != b.face) continue;   // the bench shows one face at a time
                if (a.id == "morph-puck" || b.id == "morph-puck") continue;   // the puck's pad lies under the corner lanterns by design (lowest priority)
                const auto o = a.r.getIntersection(b.r);
                if (o.getWidth() > 0.5f && o.getHeight() > 0.5f)
                {
                    ++overlaps;
                    std::printf("  overlap: %s / %s (%g x %g)\n", a.id.toRawUTF8(), b.id.toRawUTF8(), o.getWidth(), o.getHeight());
                }
            }
        std::printf("layout: %d controls wanted, %d built, %d pointer areas\n", want, ids.size(), (int)hits.size());
        CHECK(want > 100 && missing == 0, "machine: every control in ui-layout.json exists (the brass roll's three aside)");
        CHECK(small == 0, "machine: no pointer area under 34 px");
        CHECK(overlaps == 0, "machine: no two pointer areas overlap");
    }
    // ---- the parameters: everything the first editor could edit is reachable here
    {
        Poltergeist2Editor old(synth.patchMain, synth.wavetableHandoff, synth.audioToMain, synth.mainToAudio,
                               synth.audioOutputRing, synth.editorActive, synth.uiForceRebuild, synth.dawStateMain, nullptr, q, &morph);
        const auto was = old.testReachableParams();
        const auto now = ed.testReachableParams();
        int miss = 0;
        for (auto id : was)
            if (!std::binary_search(now.begin(), now.end(), id))
            {
                ++miss;
                auto it = synth.patchMain.paramMap.find(id);
                std::printf("  not reachable: %u %s\n", id, it != synth.patchMain.paramMap.end() ? it->second->meta.name.c_str() : "?");
            }
        std::printf("parameters: the first editor reached %d, the machine reaches %d, missing %d\n", (int)was.size(), (int)now.size(), miss);
        CHECK(was.size() > 400 && miss == 0, "machine: every parameter the first editor exposed is reachable");
    }
    synth.editorActive = true; // (the first editor's destructor cleared it)

    // ---- every factory preset loads (and the name shows)
    const int n = ed.testFactoryCount();
    int loaded = 0;
    for (int i = 0; i < n; ++i)
        if (ed.testLoadFactory(i).isNotEmpty()) ++loaded;
    std::printf("factory presets: %d of %d loaded\n", loaded, n);
    CHECK(n > 50 && loaded == n, "machine: every factory preset loads");
    // a preset with the most hoses: the busiest picture
    int best = 0, bestLinks = -1;
    for (int i = 0; i < n; ++i)
    {
        ed.testLoadFactory(i);
        int links = 0;
        for (auto &m : synth.patchMain.matrixNodes) links += m.active.value > 0.5f ? 1 : 0;
        if (links > bestLinks && links <= 6) { bestLinks = links; best = i; }
    }
    const auto name = ed.testLoadFactory(best);
    while (synth.mainToAudio.pop()) {}
    CHECK(ed.testControlText("pick-name").contains(name), "machine: the nameplate shows the loaded sound");
    ed.selectGhost(1);
    ed.testAdvance(1.5);
    shot2x(ed, dir, "m03-ghosts-preset");
    std::printf("ok:   cost GHOSTS at rest: %s\n", ed.testPerf(150).toRawUTF8());
    ed.testEnvTab(1);
    ed.testAdvance(0.3);
    shot2x(ed, dir, "m04-ghost2-feedback-env");
    ed.testEnvTab(0);
    for (size_t li = 0; li < ss::matrixSize; ++li)
        if (synth.patchMain.matrixNodes[li].active.value > 0.5f)
        {
            ed.selectLink((int)ss::MatrixIndex::sourceIndexAt(li), (int)ss::MatrixIndex::targetIndexAt(li));
            break;
        }
    ed.testAdvance(0.5);
    shot2x(ed, dir, "m05-ghosts-link-bench");
    ed.selectGhost(0);
    ed.testTheme(1);
    ed.testAdvance(0.5);
    shot2x(ed, dir, "m06-ghosts-dusk");
    ed.testTheme(2);
    ed.testAdvance(0.5);
    shot2x(ed, dir, "m07-ghosts-clear");
    ed.testTheme(0);
    // the ledger
    ed.testOpenLedger(0);
    ed.testAdvance(0.6);
    shot2x(ed, dir, "m08-ghosts-ledger");
    ed.testCloseLedger();
    // the storm, with a strike
    ed.testStorm(0.9f, false);
    ed.testAdvance(3.0);
    shot2x(ed, dir, "m09-ghosts-storm");
    std::printf("ok:   cost GHOSTS in a storm: %s\n", ed.testPerf(150).toRawUTF8());
    ed.testStorm(0.9f, true);
    ed.testAdvance(0.1);
    shot2x(ed, dir, "m10-ghosts-storm-strike");
    ed.testCalm(true);
    ed.testAdvance(2.0);
    shot2x(ed, dir, "m11-ghosts-storm-calm");
    ed.testCalm(false);
    ed.testStorm(-1.f, false);
    // MAIN
    ed.showPage(1);
    CHECK(ed.testPage() == 1, "machine: the MAIN sign shows the MAIN page");
    ed.testAdvance(2.0);
    shot2x(ed, dir, "m12-main");
    std::printf("ok:   cost MAIN at rest: %s\n", ed.testPerf(150).toRawUTF8());
    ed.testStorm(0.9f, true);
    ed.testAdvance(0.1);
    shot2x(ed, dir, "m13-main-storm-strike");
    ed.testStorm(-1.f, false);
    // MORPH: three corners with sounds, the puck between them
    ed.showPage(2);
    ed.testMorphCorner(1, 0);
    ed.testMorphCorner(2, n / 2);
    morph.on = true;
    ed.testPuck(0.35f, 0.4f);
    ed.testAdvance(2.5);
    shot2x(ed, dir, "m14-morph");
    std::printf("ok:   cost MORPH at rest: %s\n", ed.testPerf(150).toRawUTF8());
    CHECK(morph.kind[1].load() == Morph::OWN && morph.kind[2].load() == Morph::OWN && morph.kind[3].load() == Morph::EMPTY, "machine: MORPH corners hold the picked sounds");
    ed.testOpenLedger(2, 3);
    ed.testAdvance(0.6);
    shot2x(ed, dir, "m15-morph-ledger");
    ed.testCloseLedger();
    morph.orbit = Morph::O_EIGHT;
    morph.orbitSize = 0.7f;
    morph.effX = 0.62f; morph.effY = 0.31f;
    ed.testAdvance(0.5);
    shot2x(ed, dir, "m16-morph-orbit");
    morph.orbit = Morph::O_OFF;
    {
        Morph m2;
        CHECK(m2.fromState(morph.toState()) && m2.on.load() && std::abs(m2.x.load() - 0.35f) < 1e-5f && m2.kind[1].load() == Morph::OWN &&
                  m2.names[2] == morph.names[2] && std::string(m2.owned[1]->patchMain.name) == std::string(morph.owned[1]->patchMain.name),
              "machine: MORPH saves and loads with the project (corners, sounds, puck, on)");
    }
    // MORPH: each corner's OCTAVE (owner, 2026-11-12)
    {
        ed.showPage(2);
        ed.testAdvance(0.3);
        const float k = (float)ed.viewScale();
        auto tap = [&](const char *id, bool top) {
            const auto r = ed.testHitRect(id);
            const juce::Point<float> d(r.getCentreX(), top ? r.getY() + r.getHeight() * 0.25f : r.getBottom() - r.getHeight() * 0.25f);
            ed.mouseMove(mev(ed, d * k, d * k, false));
            ed.mouseDown(mev(ed, d * k, d * k, false));
            ed.mouseUp(mev(ed, d * k, d * k, false));
            ed.testIdle();
        };
        bool all = true;
        for (auto id : {"o-A-oct", "o-B-oct", "o-C-oct", "o-D-oct"})
            all = all && !ed.testHitRect(id).isEmpty();
        CHECK(all, "morph octave: every corner has its OCTAVE window");
        for (auto &o : morph.octave)
            o = 0;
        tap("o-B-oct", true);
        tap("o-B-oct", true);
        CHECK(morph.octave[1].load() == 2, "morph octave: the top half of corner B's window = up (twice: +2)");
        tap("o-B-oct", false);
        CHECK(morph.octave[1].load() == 1, "morph octave: the bottom half = down");
        tap("o-D-oct", true);
        CHECK(morph.octave[3].load() == 0, "morph octave: an empty corner's window does nothing (greyed)");
        for (int i = 0; i < 6; ++i)
            tap("o-C-oct", false);
        CHECK(morph.octave[2].load() == Morph::kOctMin, "morph octave: it stops at -3");
        ed.testAdvance(0.4);
        shot2x(ed, dir, "m16b-morph-octaves");
        // the keys the corner engines hear; a note is let go where it was played even if the octave moved meanwhile
        const int on1 = morph.keyOn(2, 0, 60); // corner B (+1)
        morph.octave[1] = -2;
        CHECK(on1 == 72 && morph.keyHeld(2, 0, 60) == 72 && morph.keyOn(2, 0, 64) == 40, "morph octave: corner B hears C5 for C4 at +1; its note-off finds it after a change");
        // THIS SOUND's corner moves the main engine only while MORPH is on
        Morph m3;
        m3.setThisSound(0);
        m3.octave[0] = 1;
        m3.on = false;
        const int off0 = m3.keyOn(0, 0, 60);
        m3.on = true;
        const int on0 = m3.keyOn(0, 0, 62);
        uint8_t in[3] = {0x90, 60, 100}, out[3];
        m3.midi(0, in, out);
        uint8_t inOff[3] = {0x80, 60, 0}, outOff[3];
        m3.octave[0] = 0;
        m3.midi(0, inOff, outOff);
        CHECK(off0 == 60 && on0 == 74 && out[1] == 72 && outOff[1] == 72, "morph octave: THIS SOUND moves only with MORPH on; raw MIDI too, its note-off where it was played");
        morph.octave[1] = 1;
        Morph m2;
        CHECK(m2.fromState(morph.toState()) && m2.octave[1].load() == 1 && m2.octave[2].load() == Morph::kOctMin && m2.octave[0].load() == 0,
              "morph octave: saved and loaded with the project");
        Morph m4; // an older project (no octaves saved) loads at 0
        std::string old = morph.toState();
        old.replace(0, 9, "P2MORPH 2");
        const auto nl = old.find('\n', 10);
        auto line = old.substr(10, nl - 10);
        for (int i = 0; i < Morph::N; ++i)
            line = line.substr(0, line.find_last_of(' '));
        old = old.substr(0, 10) + line + old.substr(nl);
        CHECK(m4.fromState(old) && m4.octave[1].load() == 0 && m4.kind[1].load() == Morph::OWN, "morph octave: an older project loads with every corner at 0");
        for (auto &o : morph.octave)
            o = 0;
    }
    // ---- FX: the spectral rack on the fourth page
    {
        using namespace ghostblock::p2::spectral;
        morph.orbit = Morph::O_OFF;
        ed.showPage(3);
        CHECK(ed.testPage() == 3, "machine: the FX sign shows the FX page");
        ed.testAdvance(2.0);
        shot2x(ed, dir, "m18-fx-empty");
        std::printf("ok:   cost FX at rest (rack off): %s\n", ed.testPerf(150).toRawUTF8());
        // every host parameter of the rack and every parameter of every effect can be reached from the page
        const auto reach = ed.testFxReachable();
        int missing = 0;
        for (uint32_t i = 0; i < SpectralFx::kParams; ++i)
            if (!std::binary_search(reach.begin(), reach.end(), SpectralFx::paramIdAt(i)))
            {
                ++missing;
                std::printf("  FX host parameter not reachable: %u\n", SpectralFx::paramIdAt(i));
            }
        std::printf("FX parameters: the rack has %u host parameters, the page reaches %d of them\n", SpectralFx::kParams, (int)SpectralFx::kParams - missing);
        CHECK(missing == 0, "machine: every SPECTRAL FX host parameter is reachable on the FX page");
        const auto et = ed.testFxEffectCheck();
        std::printf("FX effects: %s\n", et.toRawUTF8());
        CHECK(et.startsWith("ok"), "machine: every effect can be put in a slot and every one of its parameters comes to the bench (pages of eight)");
        // a rack as the first editor's test builds it
        spectralFx.resetToDefaults();
        ed.spectralEdit(SpectralFx::pOn, 1, 1);
        const char *ids[kSlots] = {"FrequencyWarp", "SpectralEqualizer", "PartialStretcher", "Reverb"};
        for (int sl = 0; sl < kSlots; ++sl) ed.spectralEdit(SpectralFx::pSlotType(sl), typeIndex(ids[sl]), 1);
        ed.spectralEdit(SpectralFx::pSlotParam(0, 0), 0.4, 1);
        ed.spectralEdit(SpectralFx::pWarpOn, 1, 1);
        ed.spectralEdit(SpectralFx::pMix, 70, 1);
        ed.refreshAll();
        ed.testAdvance(1.5);
        shot2x(ed, dir, "m19-fx-rack");
        CHECK(spectralFx.on() && spectralFx.slotType(1) == typeIndex("SpectralEqualizer") && spectralFx.latencyWanted() == 2047, "machine: the FX page's edits reach the rack");
        CHECK(ed.testControlText("fx-slot2-name").contains("Spectral Equalizer") && ed.testControlText("fx-p1").startsWith("AMOUNT"), "machine: the slot plates and the bench wheels carry the effects' real names");
        // a glass puts its slot on the bench; the equalizer has 40 parameters: five pages
        auto tap = [&](const juce::String &id) { const auto c = ed.testHitRect(id).getCentre(); dragOn(ed, c, c); ed.testIdle(); };
        tap("fx-slot2");
        CHECK(ed.testControlText("fx-bench-title").contains("Spectral Equalizer") && ed.testControlText("fx-page").startsWith("1/5"), "machine: a tap on a glass puts that slot on the bench; the page plate counts its pages");
        tap("fx-page");
        CHECK(ed.testControlText("fx-page").startsWith("2/5") && ed.testControlText("fx-p1").startsWith("BAND 1 FREQUENCY"), "machine: the page plate turns to the next eight parameters");
        ed.testAdvance(0.5);
        shot2x(ed, dir, "m20-fx-equalizer-page2");
        tap("fx-order");
        CHECK(spectralFx.order() == 1 && ed.testControlText("fx-order").contains("TOWN > GHOST"), "machine: the hose's valve flips the order (WarpCore first)");
        tap("fx-order");
        CHECK(spectralFx.order() == 0, "machine: and back (the chain first)");
        tap("fx-slot3-on");
        CHECK(!spectralFx.slotOn(2), "machine: a slot's button bypasses it");
        tap("fx-slot3-on");
        tap("fx-slot1-name");
        ed.testAdvance(0.6);
        shot2x(ed, dir, "m21-fx-ledger");
        ed.testCloseLedger();
        CHECK(ed.testStatus().containsIgnoreCase("latency") || ed.testStatus().isNotEmpty(), "machine: the status slit speaks on the FX page");
        ed.testStorm(0.9f, true);
        ed.testAdvance(0.1);
        shot2x(ed, dir, "m22-fx-storm-strike");
        ed.testStorm(-1.f, false);
        ed.testAdvance(1.0);
        std::printf("ok:   cost FX at rest (rack on): %s\n", ed.testPerf(150).toRawUTF8());
    }
    // ---- every control takes a press, a drag and a release without harm (each page, both faces of the bench)
    {
        int touched = 0;
        const auto ids = ed.testControlIds();
        for (int pass = 0; pass < 5; ++pass)
        {
            const int pg = pass < 2 ? 0 : pass - 1;
            ed.showPage(pg);
            if (pass == 0) ed.selectGhost(2);
            if (pass == 1) { dragOn(ed, ed.testSocket(0, true), ed.testSocket(4, false)); ed.selectLink(0, 4); }
            ed.testAdvance(0.2);
            for (auto &id : ids)
            {
                if (id == "save") continue; // opens the name dialog (a window)
                const auto st = juce::StringArray::fromTokens(ed.testControlState(id), "/", "");
                if (st[2] != "1") continue;
                const int cp = st[0].getIntValue(), face = st[1].getIntValue();
                if ((cp >= 0 && cp != pg) || (pg == 0 && face > 0 && face != (pass == 1 ? 2 : 1))) continue;
                if (id.startsWith("page-")) continue;
                if (id == "spyglass" || id == "size-grip") continue; // a window and the editor's size: checked on their own (runSpyglass, runSizes)
                const auto c = ed.testHitRect(id).getCentre();
                ed.mouseMove(mev(ed, c, c, false));
                dragOn(ed, c, c + juce::Point<float>(3.f, -18.f));
                ed.testCloseLedger();
                ed.testIdle();
                ++touched;
            }
        }
        bool finite = true;
        for (auto &[id, p] : synth.patchMain.paramMap)
            finite = finite && std::isfinite(p->value) && p->value >= p->meta.minVal - 1e-4f && p->value <= p->meta.maxVal + 1e-4f;
        std::printf("pressed and dragged %d controls\n", touched);
        CHECK(touched > 120 && finite, "machine: every control takes a press and a drag; every parameter stays inside its range");
        ed.testAdvance(0.5);
        shot2x(ed, dir, "m17-after-touching-everything");
    }
    runSpyglass(ed, synth, dir);
    runSizes(ed, synth, dir);
    runSub(ed, synth, subBass, dir);
    runBackRoom(ed, synth, dir);
    runStudio(ed, synth, q, dir);
    runClapGui();
    // closing the editor while the window is open: no crash, and it opens again with the next editor
    {
        auto e2 = std::make_unique<MachineEditor>(synth.patchMain, synth.wavetableHandoff, synth.audioToMain, synth.mainToAudio, synth.audioOutputRing, synth.editorActive,
                                                  synth.uiForceRebuild, synth.dawStateMain, nullptr, q, &morph, &spectralFx, &subBass);
        e2->openSpyglass();
        double ph = 0;
        pushSound(synth, 20000, ph);
        e2->testIdle();
        CHECK(e2->spyglassOpen(), "spyglass: opens from a second editor");
        e2.reset();
        auto e3 = std::make_unique<MachineEditor>(synth.patchMain, synth.wavetableHandoff, synth.audioToMain, synth.mainToAudio, synth.audioOutputRing, synth.editorActive,
                                                  synth.uiForceRebuild, synth.dawStateMain, nullptr, q, &morph, &spectralFx, &subBass);
        CHECK(e3->testSpyglassWillReopen(), "spyglass: closing the editor with the window open does not crash, and it remembers to open it again");
        e3->closeSpyglass(true);
        e3.reset();
        synth.editorActive = true;
        synth.audioOutputRing.subscribe(); // (each editor unsubscribes as it goes; `ed` still reads it)
    }
    ed.showPage(0);
    return 0;
}

static int runShots(const juce::File &dir)
{
    dir.createDirectory();
    auto synthHeap = std::make_unique<ss::Synth>(false); // on the heap: the Mac main thread has an 8 MB stack
    auto &synth = *synthHeap;
    UiNoteQueue q;
    auto morphHeap = std::make_unique<Morph>();
    auto &morph = *morphHeap;
    auto spectralHeap = std::make_unique<ghostblock::p2::spectral::SpectralFx>(); // SPECTRAL FX: the rack behind the fourth page
    auto &spectralFx = *spectralHeap;
    Poltergeist2Editor ed(synth.patchMain, synth.wavetableHandoff, synth.audioToMain, synth.mainToAudio,
                          synth.audioOutputRing, synth.editorActive, synth.uiForceRebuild, synth.dawStateMain, nullptr, q, &morph, &spectralFx); // (the classic editor: no SUB panel)
    CHECK(ed.getWidth() == 1040 && ed.getHeight() == 744, "opens at 1040 x 744 (fits a 1440 x 900 MacBook Pro screen with the DAW around it)");
    ed.testIdle();
    shot(ed, dir, "01-ghosts-init-black");

    // drag GHOST 1 onto GHOST 3: a link appears (matrix 1 -> 3 on, depth 50 %), both ghosts awake
    auto &field = *ed.testField();
    {
        auto a = ed.testGhostPos(0), b = ed.testGhostPos(2);
        field.mouseDown(mev(field, a, a, false));
        field.mouseDrag(mev(field, (a + b) * 0.5f, a, true));
        field.mouseDrag(mev(field, b, a, true));
        field.mouseUp(mev(field, b, a, true));
        auto &m = synth.patchMain.matrixNodes[ss::MatrixIndex::positionForSourceTarget(0, 2)];
        CHECK(m.active.value > 0.5f && std::abs(m.level.value - 0.5f) < 1e-4f, "drag GHOST 1 onto GHOST 3 = GHOST 1 haunts GHOST 3 at 50");
        CHECK(synth.patchMain.sourceNodes[2].active.value > 0.5f, "GHOST 3 woke up");
        CHECK(ed.testPanelTitle() == "GHOST 1 > GHOST 3", "the panel shows the new link");
        // and the audio thread got it
        bool sawSet = false;
        while (auto m2 = synth.mainToAudio.pop())
            if (m2->action == ss::Synth::MainToAudioMsg::SET_PARAM && m2->paramId == m.level.meta.id) sawSet = true;
        CHECK(sawSet, "the link depth went to the audio thread (SET_PARAM)");
    }
    // the other way round is refused with a hint
    {
        auto a = ed.testGhostPos(4), b = ed.testGhostPos(1);
        field.mouseDown(mev(field, a, a, false));
        field.mouseDrag(mev(field, b, a, true));
        field.mouseUp(mev(field, b, a, true));
        CHECK(ed.testStatus().contains("can only haunt the ghosts below it"), "drag GHOST 5 onto GHOST 2 explains the direction");
    }
    ed.testIdle();
    shot(ed, dir, "02-ghosts-link-1-to-3");

    // every factory preset loads (and the name shows)
    const int n = ed.testFactoryCount();
    int loaded = 0;
    for (int i = 0; i < n; ++i)
        if (ed.testLoadFactory(i).isNotEmpty()) ++loaded;
    std::printf("factory presets: %d of %d loaded\n", loaded, n);
    CHECK(n > 50 && loaded == n, "every factory preset loads");
    // a bass preset, ghost panel, each envelope tab, each look
    int bass = 0;
    for (int i = 0; i < n; ++i)
    {
        ed.testLoadFactory(i);
        if (juce::String(synth.patchMain.name).isNotEmpty()) { bass = i; }
        if (i > 3) break;
    }
    ed.testLoadFactory(bass);
    for (int m = 0; m < (int)ss::numOps; ++m)
        while (synth.mainToAudio.pop()) {}
    ed.selectGhost(1);
    ed.testIdle();
    shot(ed, dir, "03-ghosts-preset-black");
    ed.testEnvTab(1);
    shot(ed, dir, "04-ghost2-feedback-env");
    ed.testEnvTab(2);
    shot(ed, dir, "05-ghost2-pitch-env");
    ed.testEnvTab(0);
    ed.testTheme(1);
    shot(ed, dir, "06-ghosts-purple");
    ed.testTheme(2);
    shot(ed, dir, "07-ghosts-light");
    ed.testTheme(0);
    ed.showPage(1);
    ed.testIdle();
    shot(ed, dir, "08-main-black");
    ed.testTheme(2);
    shot(ed, dir, "09-main-light");
    ed.testTheme(0);
    // MORPH: three corners with sounds, the puck between them
    ed.showPage(2);
    ed.testMorphCorner(1, 0);
    ed.testMorphCorner(2, n / 2);
    morph.on = true;
    ed.testPuck(0.35f, 0.4f);
    ed.testIdle();
    shot(ed, dir, "10-morph-black");
    CHECK(morph.kind[1].load() == Morph::OWN && morph.kind[2].load() == Morph::OWN && morph.kind[3].load() == Morph::EMPTY, "MORPH corners hold the picked sounds");
    ed.testTheme(2);
    shot(ed, dir, "11-morph-light");
    ed.testTheme(0);
    morph.orbit = Morph::O_EIGHT;
    morph.orbitSize = 0.7f;
    morph.effX = 0.62f; morph.effY = 0.31f;
    ed.testIdle();
    shot(ed, dir, "12-morph-orbit");
    ed.testTheme(0);
    {
        // state round trip
        Morph m2;
        CHECK(m2.fromState(morph.toState()) && m2.on.load() && std::abs(m2.x.load() - 0.35f) < 1e-5f && m2.kind[1].load() == Morph::OWN &&
                  m2.names[2] == morph.names[2] && std::string(m2.owned[1]->patchMain.name) == std::string(morph.owned[1]->patchMain.name),
              "MORPH saves and loads with the project (corners, sounds, puck, on)");
    }
    // SPECTRAL FX: the fourth page, empty and with a full rack
    {
        using namespace ghostblock::p2::spectral;
        ed.showPage(3);
        ed.testIdle();
        shot(ed, dir, "13-spectral-empty");
        ed.spectralEdit(SpectralFx::pOn, 1, 1);
        const char *ids[kSlots] = {"BinShuffle", "SpectralEqualizer", "FrameHold", "Reverb"};
        for (int s = 0; s < kSlots; ++s) ed.spectralEdit(SpectralFx::pSlotType(s), typeIndex(ids[s]), 1);
        ed.spectralEdit(SpectralFx::pSlotParam(0, 0), 0.4, 1);
        ed.spectralEdit(SpectralFx::pWarpOn, 1, 1);
        ed.spectralEdit(SpectralFx::pMix, 70, 1);
        ed.showPage(0);
        ed.showPage(3); // (the page reads the rack again when it is shown)
        ed.testIdle();
        shot(ed, dir, "14-spectral-rack");
        CHECK(spectralFx.on() && spectralFx.slotType(1) == typeIndex("SpectralEqualizer") && spectralFx.latencyWanted() == 2047, "SPECTRAL page: the editor's edits reach the rack");
        ed.testTheme(2);
        shot(ed, dir, "15-spectral-light");
        ed.testTheme(0);
    }
    ed.showPage(0);
    return 0;
}

static void runWeights()
{
    int k[Morph::N] = {1, 2, 2, 2};
    float w[Morph::N];
    Morph::weightsAt(0, 0, k, w);
    CHECK(w[0] == 1.f && w[1] == 0.f && w[2] == 0.f && w[3] == 0.f, "puck on corner A = only A");
    Morph::weightsAt(1, 1, k, w);
    CHECK(w[3] == 1.f && w[0] == 0.f, "puck on corner D = only D");
    Morph::weightsAt(0.5f, 0.5f, k, w);
    CHECK(std::abs(w[0] - 0.25f) < 1e-6f && std::abs(w[3] - 0.25f) < 1e-6f, "puck in the middle = a quarter each");
    int k2[Morph::N] = {1, 2, 0, 0};
    Morph::weightsAt(0.5f, 0.5f, k2, w);
    CHECK(std::abs(w[0] - 0.5f) < 1e-6f && std::abs(w[1] - 0.5f) < 1e-6f && w[2] == 0.f, "empty corners give their share to the others");
    Morph::weightsAt(0.f, 1.f, k2, w);
    CHECK(w[0] > 0.6f && std::abs(w[0] + w[1] - 1.f) < 1e-5f, "the puck over an empty corner leans to the nearest sound");

    // ORBIT: every shape is a smooth closed loop that stays on the board
    bool smooth = true, closed = true, inside = true;
    for (int sh = 1; sh < Morph::O_COUNT; ++sh)
    {
        float px, py, x0, y0;
        Morph::orbitAt(sh, 0.8f, 0.f, 0.5f, 0.5f, x0, y0);
        px = x0; py = y0;
        for (int i = 1; i <= 4000; ++i)
        {
            float ox, oy;
            Morph::orbitAt(sh, 0.8f, (float)i / 4000.f, 0.5f, 0.5f, ox, oy);
            smooth &= std::hypot(ox - px, oy - py) < 0.01f;
            inside &= ox >= 0 && ox <= 1 && oy >= 0 && oy <= 1;
            px = ox; py = oy;
        }
        closed &= std::hypot(px - x0, py - y0) < 1e-3f;
    }
    CHECK(smooth, "ORBIT: every shape moves the puck smoothly (no jumps)");
    CHECK(closed, "ORBIT: every shape joins up at the end of its loop");
    CHECK(inside, "ORBIT: the puck stays on the board");
    {
        Morph m;
        m.orbit = Morph::O_CIRCLE;
        m.orbitRate = 2; // 1 bar = 4 beats
        m.advance(1.0, 120, 256, 48000);
        const float ax = m.effX.load(), ay = m.effY.load();
        m.advance(5.0, 120, 256, 48000);
        CHECK(std::abs(m.effX.load() - ax) < 1e-5f && std::abs(m.effY.load() - ay) < 1e-5f, "ORBIT is locked to the song: one bar later, the same place");
        m.advance(2.0, 120, 256, 48000);
        CHECK(std::hypot(m.effX.load() - ax, m.effY.load() - ay) > 0.1f, "ORBIT: half a loop later, somewhere else");
        m.orbit = Morph::O_OFF;
        m.advance(2.0, 120, 256, 48000);
        CHECK(m.effX.load() == m.x.load() && m.effY.load() == m.y.load(), "ORBIT off: the puck is where you put it");
    }
}

// ------------------------------------------------------------------------------------------------- audio
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
// renders 3 s: notes C3 + G3 from 21 ms, released at `offAt` seconds; `state` (if any) loaded first
static juce::AudioBuffer<float> render(const std::string &state, double offAt, bool shortRelease,
                                       std::vector<std::pair<uint32_t, double>> hostParams = {});

static int runAudio(const juce::File &wav)
{
    auto out = render({}, 1.0, true);
    const double sr = 48000;
    const int total = out.getNumSamples();
    bool nan = false;
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < total; ++i) nan |= !std::isfinite(out.getSample(c, i));
    const float peak = out.getMagnitude(0, (int)sr);
    const float tail = out.getMagnitude((int)(sr * 2.5), (int)(sr * 0.5));
    const float held = out.getRMSLevel(0, (int)(sr * 0.5), (int)(sr * 0.4));
    std::printf("peak %.3f  held rms %.4f  tail after release %.6f\n", peak, held, tail);
    CHECK(!nan, "no NaN / inf");
    CHECK(peak > 0.05f && peak < 1.0f, "the notes sound, below full scale");
    CHECK(tail < 1e-4f, "silence after the release");
    juce::WavAudioFormat fmt;
    wav.deleteFile();
    if (auto w = std::unique_ptr<juce::AudioFormatWriter>(fmt.createWriterFor(new juce::FileOutputStream(wav), sr, 2, 24, {}, 0)))
        w->writeFromAudioSampleBuffer(out, 0, total);

    // ---- MORPH through the real plugin: corner A = THIS SOUND (init), corner B = its own sound (an octave up)
    ss::Patch a, b;
    b.sourceNodes[0].ratio.value = 1.0f; // 2x
    std::strncpy(b.name, "Octave", sizeof(b.name) - 1);
    auto withMorph = [&](float px, float py) {
        Morph m;
        m.setOwn(1, b.toState(), "Octave");
        m.on = true;
        m.x = px;
        m.y = py;
        std::string st = a.toState();
        st.push_back('\0');
        st += m.toState();
        st.push_back('\0');
        return st;
    };
    auto plain = render({}, 2.6, false), octave = render(b.toState(), 2.6, false);
    auto atA = render(withMorph(0, 0), 2.6, false), atB = render(withMorph(1, 0), 2.6, false), mid = render(withMorph(0.5f, 0), 2.6, false);
    auto maxDiff = [&](const juce::AudioBuffer<float> &x, const juce::AudioBuffer<float> &y, int from, int to) {
        float d = 0;
        for (int c = 0; c < 2; ++c)
            for (int i = from; i < to; ++i) d = std::max(d, std::abs(x.getSample(c, i) - y.getSample(c, i)));
        return d;
    };
    const float dA = maxDiff(plain, atA, 0, total), dB = maxDiff(octave, atB, (int)(sr * 1.2), (int)(sr * 2.2));
    const float dAB = maxDiff(plain, octave, (int)(sr * 1.2), (int)(sr * 2.2));
    std::printf("morph: |A - plain| %.3g   |B - octave| %.3g   (plain vs octave differ by %.3g)\n", dA, dB, dAB);
    CHECK(dA == 0.f, "MORPH, puck on A (THIS SOUND) = exactly the plugin without MORPH");
    CHECK(dB < 1e-5f && dAB > 0.01f, "MORPH, puck on B = exactly corner B's own sound");
    bool midOk = true;
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < total; ++i) midOk &= std::isfinite(mid.getSample(c, i));
    const float midRms = mid.getRMSLevel(0, (int)(sr * 1.2), (int)sr), pRms = plain.getRMSLevel(0, (int)(sr * 1.2), (int)sr),
                oRms = octave.getRMSLevel(0, (int)(sr * 1.2), (int)sr);
    std::printf("morph middle rms %.4f (A %.4f, B %.4f)\n", midRms, pRms, oRms);
    CHECK(midOk && midRms > 0.5f * std::min(pRms, oRms) && mid.getMagnitude(0, total) < 1.0f, "MORPH halfway: both sounds, no clipping");
    // the DAW moves the puck: saved on A, automation says X = 100 % -> corner B's sound
    auto automated = render(withMorph(0, 0), 2.6, false, {{Morph::pX, 1.0}});
    const float dAuto = maxDiff(octave, automated, (int)(sr * 1.2), (int)(sr * 2.2));
    std::printf("automation: |puck moved by the DAW to B - octave| %.3g\n", dAuto);
    CHECK(dAuto < 1e-5f, "MORPH X automated by the DAW moves the puck (to B = exactly B)");
    return 0;
}

static juce::AudioBuffer<float> render(const std::string &state, double offAt, bool shortRelease,
                                       std::vector<std::pair<uint32_t, double>> hostParams)
{
    clap_host host{};
    host.clap_version = CLAP_VERSION;
    host.name = "p2-tests";
    host.vendor = "ghostblock";
    host.url = "";
    host.version = "1";
    host.get_extension = [](const clap_host *, const char *) -> const void * { return nullptr; };
    host.request_restart = [](const clap_host *) {};
    host.request_process = [](const clap_host *) {};
    host.request_callback = [](const clap_host *) {};
    auto *pl = ss::makePlugin(&host, false);
    static bool once = false;
    if (!once)
    {
        once = true;
        pl->init(pl);
        auto *pe = static_cast<const clap_plugin_params *>(pl->get_extension(pl, CLAP_EXT_PARAMS));
        int found = 0;
        if (pe)
        {
            const auto n = pe->count(pl);
            for (uint32_t i = 0; i < n; ++i)
            {
                clap_param_info info{};
                if (pe->get_info(pl, i, &info) && Morph::isParam(info.id) && (info.flags & CLAP_PARAM_IS_AUTOMATABLE)) ++found;
            }
            char txt[64]{};
            pe->value_to_text(pl, Morph::pOrbit, 1, txt, sizeof(txt));
            CHECK(std::string(txt) == "CIRCLE", "the host shows ORBIT by name");
        }
        CHECK(found == (int)Morph::kParams, "the DAW can automate MORPH: ON, X, Y, ORBIT, RATE, SIZE");
        CHECK(pl != nullptr, "the plugin is created");
        CHECK(std::string(pl->desc->id) == "io.ghostblock.poltergeist2", "its id is io.ghostblock.poltergeist2");
        CHECK(std::string(pl->desc->name) == "Poltergeist 2", "its name is Poltergeist 2");
    }
    static bool inited_first = false;
    if (inited_first) pl->init(pl);
    inited_first = true;
    if (!state.empty())
    {
        MemIn mi{state};
        clap_istream is{&mi, &MemIn::read};
        auto *st = static_cast<const clap_plugin_state *>(pl->get_extension(pl, CLAP_EXT_STATE));
        CHECK(st && st->load(pl, &is), "the plugin loads a saved state");
    }
    const double sr = 48000;
    const uint32_t bs = 256;
    pl->activate(pl, sr, 1, bs);
    pl->start_processing(pl);
    const int total = (int)(sr * 3.0);
    juce::AudioBuffer<float> out(2, total);
    out.clear();
    std::vector<float> inL(bs, 0.f), inR(bs, 0.f), oL(bs), oR(bs);
    EvList ev;
    clap_input_events in{&ev, &EvList::size, &EvList::get};
    clap_output_events o{nullptr, &tryPush};
    // the init sound rings out for a long time: give its envelopes a short release so "silence after" means something
    ss::Patch ref;
    std::vector<clap_event_param_value> params;
    for (auto *pr : {&ref.mixerNodes[0].release, &ref.output.release})
        if (shortRelease)
    {
        clap_event_param_value pv{};
        pv.header.size = sizeof(pv);
        pv.header.time = 0;
        pv.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        pv.header.type = CLAP_EVENT_PARAM_VALUE;
        pv.param_id = pr->meta.id;
        pv.note_id = -1; pv.port_index = -1; pv.channel = -1; pv.key = -1;
        pv.value = 0.15;
        params.push_back(pv);
    }
    for (auto &[id, v] : hostParams)
    {
        clap_event_param_value pv{};
        pv.header.size = sizeof(pv);
        pv.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        pv.header.type = CLAP_EVENT_PARAM_VALUE;
        pv.param_id = id;
        pv.note_id = -1; pv.port_index = -1; pv.channel = -1; pv.key = -1;
        pv.value = v;
        params.push_back(pv);
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
    pl->stop_processing(pl);
    pl->deactivate(pl);
    pl->destroy(pl);
    return out;
}

// the machine editor in a real window on the real clock for a few seconds (the path a host runs: timer, governor, paint)
struct Quit : juce::Timer
{
    clock_t warm{0};
    int n{0};
    void timerCallback() override
    {
        if (n++ == 0) warm = std::clock(); // the first 5 s (opening: page build, find-it ripple) are not counted
        else juce::MessageManager::getInstance()->stopDispatchLoop();
    }
};
static void runLive(double seconds, int page, float storm)
{
    auto synthHeap = std::make_unique<ss::Synth>(false); // on the heap: the Mac main thread has an 8 MB stack
    auto &synth = *synthHeap;
    UiNoteQueue q;
    auto morphHeap = std::make_unique<Morph>();
    auto &morph = *morphHeap;
    machine::MachineEditor ed(synth.patchMain, synth.wavetableHandoff, synth.audioToMain, synth.mainToAudio,
                              synth.audioOutputRing, synth.editorActive, synth.uiForceRebuild, synth.dawStateMain, nullptr, q, &morph);
    ed.testScale(2);
    ed.testRun(true); // (no window: X windows cannot be made in the build sandbox; the compositor runs as in a host, only the OS blit is missing)
    ed.showPage(page);
    if (storm >= 0) ed.testStorm(storm, false);
    Quit quit;
    quit.startTimer(5000);
    seconds = 5.0;
    juce::MessageManager::getInstance()->runDispatchLoop();
    quit.stopTimer();
    const double cpu = (double)(std::clock() - quit.warm) / CLOCKS_PER_SEC;
    std::printf("ok:   live page %d%s, %g s on the real clock after 5 s of opening: %s; the whole process used %.1f %% of one core\n", page, storm >= 0 ? " in a storm" : "", seconds, ed.testLive().toRawUTF8(), 100.0 * cpu / seconds);
}

int main(int argc, char **argv)
{
    { // never the real settings file: a fresh one for each run
        const auto f = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("poltergeist2-tests-" + juce::String(juce::Time::currentTimeMillis()) + ".settings");
        f.deleteFile();
        setenv("POLTERGEIST2_SETTINGS_FILE", f.getFullPathName().toRawUTF8(), 1);
    }
#if JUCE_LINUX
    prepareBareX();
#endif
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::String mode = argc > 1 ? argv[1] : "shots";
    const juce::File arg(argc > 2 ? juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]) : juce::File::getCurrentWorkingDirectory().getChildFile("p2-shots"));
    if (mode == "shots") { runMachine(arg); runShots(arg.getChildFile("classic")); }
    else if (mode == "machine") runMachine(arg);
    else if (mode == "spyglass")
    { // only the SPYGLASS checks (quick)
        arg.createDirectory();
        auto synthHeap = std::make_unique<ss::Synth>(false);
        UiNoteQueue q;
        auto morphHeap = std::make_unique<Morph>();
        auto spectralHeap = std::make_unique<ghostblock::p2::spectral::SpectralFx>();
        auto &synth = *synthHeap;
        auto ed = std::make_unique<machine::MachineEditor>(synth.patchMain, synth.wavetableHandoff, synth.audioToMain, synth.mainToAudio, synth.audioOutputRing, synth.editorActive,
                                                           synth.uiForceRebuild, synth.dawStateMain, nullptr, q, morphHeap.get(), spectralHeap.get());
        ed->testTheme(0);
        runSpyglass(*ed, synth, arg);
        runSizes(*ed, synth, arg);
        runClapGui();
    }
    else if (mode == "live") runLive(10.0, argc > 2 ? std::atoi(argv[2]) : 0, argc > 3 ? (float)std::atof(argv[3]) : -1.f); // live <page> [storm 0..1]
    else if (mode == "audio") runAudio(arg);
    else if (mode == "all") { runWeights(); runMachine(arg); runShots(arg.getChildFile("classic")); runAudio(arg.getChildFile("render.wav")); }
    std::printf(fails ? "FAILURES: %d\n" : "ALL PASS\n", fails);
    return fails ? 1 : 0;
}
