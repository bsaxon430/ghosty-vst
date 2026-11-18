// Tests Shared/WaveRegion.h: the gestures (handles, right-click START, right-drag LOOP, loop move / toggle),
// that a plain click never edits, the hit zones, the export gate and the drag-out.
#include <juce_gui_basics/juce_gui_basics.h>
#include "WaveRegion.h"
#include <cmath>
#include <cstdio>

static int fails = 0;
#define CHECK(c) do { if (c) std::printf("  ok    %s\n", #c); else { std::printf("  FAIL  %s\n", #c); ++fails; } } while (0)

static juce::MouseEvent ev(juce::Component& c, float x, float y, bool right, float dx = 0, int clicks = 1) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), { x + dx, y },
                            right ? juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier) : juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
                            0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, juce::Time::getCurrentTime(), { x, y }, juce::Time::getCurrentTime(), clicks, std::abs(dx) > 0.0f);
}
static void gesture(ghosty::WaveRegion& w, float x0, float x1, float y, bool right) {
    w.mouseDown(ev(w, x0, y, right)); if (std::abs(x1 - x0) > 0.0f) w.mouseDrag(ev(w, x0, y, right, x1 - x0)); w.mouseUp(ev(w, x0, y, right, x1 - x0));
}
static bool near(float a, float b) { return std::abs(a - b) < 0.01f; }

int main() {
    juce::ScopedJuceInitialiser_GUI init;
    ghosty::WaveRegion w; w.setSize(800, 200);
    std::vector<float> pk(4000); for (size_t i = 0; i < pk.size(); ++i) pk[i] = 0.5f + 0.4f * std::sin((float) i * 0.01f);
    w.setPeaks(pk); w.setLengthSeconds(4.0);
    int changes = 0, auds = 0; juce::String msg; ghosty::WaveRegion::Region last;
    w.onChange = [&](const ghosty::WaveRegion::Region& r) { ++changes; last = r; };
    w.onAudition = [&](bool, float) { ++auds; };
    w.onMessage = [&](const juce::String& m) { msg = m; };
    const auto wa = w.waveArea(); const float y = wa.getY() + wa.getHeight() * 0.3f;

    std::printf("a plain click (and a 4 px wobble) auditions, never edits\n");
    gesture(w, w.xOf(0.5f), w.xOf(0.5f), y, false); gesture(w, w.xOf(0.5f), w.xOf(0.5f) + 4, y, false);
    CHECK(changes == 0); CHECK(auds == 4); CHECK(! w.region().loopOn);

    std::printf("right-click = START there\n");
    gesture(w, w.xOf(0.2f), w.xOf(0.2f), y, true);
    CHECK(near(w.region().start, 0.2f)); CHECK(! w.region().loopOn); CHECK(msg.startsWith("START at 0.8"));

    std::printf("left-drag the START / END handles (the line and the labelled flag)\n");
    gesture(w, w.xOf(0.2f), w.xOf(0.1f), y, false); CHECK(near(w.region().start, 0.1f));
    const auto ef = w.handleFlag(1); gesture(w, ef.getCentreX(), ef.getCentreX() - (w.xOf(1.0f) - w.xOf(0.9f)), ef.getCentreY(), false);
    CHECK(near(w.region().end, 0.9f));
    gesture(w, w.xOf(0.1f), w.xOf(0.95f), y, false); CHECK(w.region().start < w.region().end);   // START can't cross END

    std::printf("right-drag = a loop over that span, ON\n");
    w.setRegion({ 0.1f, 0.9f, 0.0f, 0.0f, false });
    gesture(w, w.xOf(0.6f), w.xOf(0.4f), y, true);   // backwards works too
    CHECK(w.region().loopOn); CHECK(near(w.region().loopStart, 0.4f)); CHECK(near(w.region().loopEnd, 0.6f)); CHECK(near(w.region().start, 0.1f));
    gesture(w, w.xOf(0.05f), w.xOf(0.95f), y, true);   // wider than START..END: they widen
    CHECK(near(w.region().start, 0.05f)); CHECK(near(w.region().end, 0.95f));

    std::printf("loop ends + move via the loop bar, toggle via the pill and double-click\n");
    w.setRegion({ 0.1f, 0.9f, 0.4f, 0.6f, true });
    gesture(w, w.xOf(0.6f), w.xOf(0.7f), y, false); CHECK(near(w.region().loopEnd, 0.7f));
    const float by = wa.getBottom() + 3; gesture(w, w.xOf(0.5f), w.xOf(0.6f), by, false);
    CHECK(near(w.region().loopStart, 0.5f)); CHECK(near(w.region().loopEnd, 0.8f));
    const auto pill = w.loopPill(); gesture(w, pill.getCentreX(), pill.getCentreX(), pill.getCentreY(), false); CHECK(! w.region().loopOn);
    gesture(w, pill.getCentreX(), pill.getCentreX(), pill.getCentreY(), false); CHECK(w.region().loopOn);
    w.mouseDoubleClick(ev(w, w.xOf(0.65f), y, false, 0, 2)); CHECK(! w.region().loopOn);
    CHECK(last == w.region());

    std::printf("hit zones: the grip, the pill, the handles don't overlap\n");
    w.setRegion({ 0.1f, 0.9f, 0.4f, 0.6f, true });
    CHECK(! w.gripRect().intersects(w.loopPill())); CHECK(! w.gripRect().intersects(w.handleFlag(0))); CHECK(! w.gripRect().intersects(w.handleFlag(1)));
    CHECK(w.handleAt(w.gripRect().getCentreX()) < 0);
    CHECK(! w.handleFlag(0).intersects(w.handleFlag(1)));

    std::printf("drag-out: the highlighted region, behind the export gate\n");
    bool allowed = false; float ea = -1, eb = -1; juce::StringArray dragged;
    const auto tmp = juce::File::createTempFile(".wav"); tmp.replaceWithText("x");
    w.canExport = [&] { return allowed; }; w.exportRegion = [&](float a, float b) { ea = a; eb = b; return tmp; }; w.performDrag = [&](const juce::StringArray& f) { dragged = f; };
    const auto g = w.gripRect(); gesture(w, g.getCentreX(), g.getCentreX() + 30, g.getCentreY(), false);
    CHECK(dragged.isEmpty()); CHECK(ea < 0); CHECK(msg.contains("UNLOCK"));
    allowed = true; gesture(w, g.getCentreX(), g.getCentreX() + 30, g.getCentreY(), false);
    CHECK(dragged.size() == 1); CHECK(near(ea, 0.4f)); CHECK(near(eb, 0.6f));
    w.setRegion({ 0.1f, 0.9f, 0.4f, 0.6f, false }); dragged.clear();
    const auto g2 = w.gripRect(); gesture(w, g2.getCentreX(), g2.getCentreX() + 30, g2.getCentreY(), false);
    CHECK(near(ea, 0.1f)); CHECK(near(eb, 0.9f));   // no loop: START..END
    tmp.deleteFile();

    std::printf("paints (dark + light, empty + full) without trouble\n");
    for (int k = 0; k < 4; ++k) {
        w.palette = [k] { return k % 2 ? ghosty::WaveRegion::Palette::light() : ghosty::WaveRegion::Palette(); };
        if (k >= 2) w.setPeaks({});
        auto img = w.createComponentSnapshot(w.getLocalBounds()); CHECK(img.isValid());
    }
    std::printf(fails ? "WaveRegionTests: %d FAILED\n" : "WaveRegionTests: all passed\n", fails);
    return fails ? 1 : 0;
}
