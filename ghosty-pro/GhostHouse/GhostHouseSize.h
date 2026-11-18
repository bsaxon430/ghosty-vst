#pragma once
// Ghost House window size (owner 2026-11-06 15:39 + 15:44): every new window opens at the FITTING size for Brandon's
// MacBook Pro (about 1200 x 760 points on a 1440 x 900 screen, never wider than 1240 or taller than 780), so FL's own
// "fit to screen" button is never needed. Resizable, in proportion (the page zooms as one picture, see WebZoom.h).
// A project that saved its own window size (from build 105 on) reopens at it, kept on the screen.
// Plain ints so the local test can check it without a GUI.
namespace ghouse::winsize {

constexpr int kDefW = 1200, kDefH = 760;      // the fitting size
constexpr int kMaxDefW = 1240, kMaxDefH = 780; // the default is never bigger than this
constexpr int kMinW = 720, kMinH = 456;       // smallest (same shape)
constexpr int kMaxW = 2560, kMaxH = 1621;     // biggest (same shape)
// the page is laid out at this CSS size and zoomed to the window (WebZoom): at 1200 x 760 that is a zoom of ~0.87,
// the 3-row desktop layout with everything in view (checked for overlaps by tests/page_tests.js)
// (owner 2026-11-13) 1390 x 847: the WIDE layout (transport | BREAK ENGINE | logo panel across the top) at the shape of the
// page area in the fitting window (1188 x 724), so it fills it with no bars
constexpr int kPageW = 1390, kPageH = 847;

struct Size { int w, h; };

// screenW / screenH: the screen's usable area (0 = unknown). savedW / savedH: the size this project saved (0 = none).
inline Size initial(int screenW, int screenH, int savedW, int savedH) {
    int w = kDefW, h = kDefH;
    if (savedW >= kMinW && savedH >= kMinH && savedW <= kMaxW && savedH <= kMaxH) { w = savedW; h = savedH; }
    if (screenW > 0 && screenH > 0) {                     // never bigger than the screen
        const int bw = screenW - 40, bh = screenH - 60;   // (FL's own title bar ~30 + room to spare; the menu bar and the Dock are already outside the usable area)
        if (w > bw || h > bh) {
            const double s = (bw / (double) w < bh / (double) h) ? bw / (double) w : bh / (double) h;
            w = (int) (w * s); h = (int) (h * s);
        }
    }
    if (w < kMinW) w = kMinW;
    if (h < kMinH) h = kMinH;
    return { w, h };
}

} // namespace ghouse::winsize
