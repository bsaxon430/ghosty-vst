#pragma once
// EdgeResize (owner 2026-11-01, "like FabFilter Pro-L 2"): grab ANY edge or corner of the plugin window to resize it, not just the
// bottom-right corner. A thin frame (kEdge px) around the page catches the mouse; the drag scales the window in proportion
// (the editor's fixed aspect ratio + WebZoom keep the layout). The host keeps the window's top-left in place.
#include <juce_gui_basics/juce_gui_basics.h>

namespace ghosty {

struct EdgeResizer : juce::Component {
    static constexpr int kEdge = 6;
    explicit EdgeResizer(juce::Component& ed) : editor(ed) { setInterceptsMouseClicks(true, false); setAlwaysOnTop(true); }
    bool hitTest(int x, int y) override { return zone(x, y) != 0; }
    void paint(juce::Graphics&) override {}
    void mouseMove(const juce::MouseEvent& e) override { setMouseCursor(cursorFor(zone(e.x, e.y))); }
    void mouseDown(const juce::MouseEvent& e) override { z0 = zone(e.x, e.y); w0 = editor.getWidth(); h0 = editor.getHeight(); }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (z0 == 0 || w0 <= 0 || h0 <= 0) return;
        const int dx = e.getDistanceFromDragStartX(), dy = e.getDistanceFromDragStartY();
        double sx = 1.0, sy = 1.0;   // how much bigger each edge asks for (outward = bigger)
        if (z0 & L) sx = (w0 - dx) / (double) w0; if (z0 & R) sx = (w0 + dx) / (double) w0;
        if (z0 & T) sy = (h0 - dy) / (double) h0; if (z0 & B) sy = (h0 + dy) / (double) h0;
        const bool horiz = (z0 & (L | R)) != 0, vert = (z0 & (T | B)) != 0;
        const double s = horiz && vert ? juce::jmax(sx, sy) : horiz ? sx : sy;   // corners follow the bigger pull
        auto b = juce::Rectangle<int>(editor.getX(), editor.getY(), juce::roundToInt(w0 * s), juce::roundToInt(h0 * s));
        if (auto* ped = dynamic_cast<juce::AudioProcessorEditor*>(&editor)) if (auto* c = ped->getConstrainer()) {
            c->checkBounds(b, editor.getBounds(), juce::Desktop::getInstance().getDisplays().getDisplayForRect(editor.getScreenBounds())->userArea, false, false, true, true); }
        editor.setSize(b.getWidth(), b.getHeight());
    }
    void mouseUp(const juce::MouseEvent&) override { z0 = 0; }
private:
    enum { L = 1, R = 2, T = 4, B = 8 };
    int zone(int x, int y) const { int z = 0; const int w = getWidth(), h = getHeight(), k = kEdge, c = 14;
        if (x < k) z |= L; else if (x >= w - k) z |= R; if (y < k) z |= T; else if (y >= h - k) z |= B;
        if (z & (L | R)) { if (y < c) z |= T; else if (y >= h - c) z |= B; }   // a bigger grab area at the corners
        if (z & (T | B)) { if (x < c) z |= L; else if (x >= w - c) z |= R; }
        return z; }
    static juce::MouseCursor cursorFor(int z) {
        using MC = juce::MouseCursor;
        if (z == (L | T) || z == (R | B)) return MC::TopLeftCornerResizeCursor;
        if (z == (R | T) || z == (L | B)) return MC::TopRightCornerResizeCursor;
        if (z & (L | R)) return MC::LeftRightResizeCursor; if (z & (T | B)) return MC::UpDownResizeCursor; return MC::NormalCursor; }
    juce::Component& editor; int z0 = 0, w0 = 0, h0 = 0;
};

} // namespace ghosty
