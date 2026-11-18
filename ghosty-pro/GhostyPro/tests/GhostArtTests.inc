// Included by TrapTests.cpp after TrapTestsNew.inc (uses makeProc, progOf, pump, CHECK).
//   ProTests ghosts [dir]  -> the four XY-pad ghost characters (GhostyPro/ProGhostArt.h) at R = 22 / 44 / 120 px in the three themes,
//                             idle / dragged x parameters low / high, one PNG each + contact_sheet.png + the real pad in each theme;
//                             rim contrast on every backdrop, distinct silhouettes, the pad hook installed, paint time per frame.
#include "ProGhostArt.h"
#include <ctime>

namespace ghosttest
{
using proghost::Kind;

static void savePng (const juce::Image& img, const juce::File& f)
{
    f.deleteFile();
    if (auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream())) juce::PNGImageFormat().writeImageToStream (img, *os);
}

struct State { const char* name; bool held; float amt; };
static const State states[] { { "idle_low", false, 0.05f }, { "idle_high", false, 0.95f }, { "drag_low", true, 0.05f }, { "drag_high", true, 0.95f } };
static const Kind kinds[] { Kind::Heat, Kind::Space, Kind::Morph, Kind::Tone };
static const char* kindName (Kind k) { return k == Kind::Heat ? "HEAT" : k == Kind::Space ? "SPACE" : k == Kind::Morph ? "MORPH" : "TONE"; }

static proghost::Pose poseFor (const State& s, juce::Colour bg, float t = 1.3f)
{
    proghost::Pose p; p.t = t; p.x = s.amt; p.y = s.amt; p.held = s.held; p.glitch = s.held ? 0.35f : 0.0f; p.level = s.held ? 0.3f : 0.0f; p.backdrop = bg;
    return p;
}

static juce::Image tile (Kind k, float R, const proghost::Pose& p, int side)
{
    juce::Image img (juce::Image::ARGB, side, side, true, juce::SoftwareImageType());
    juce::Graphics g (img);
    g.fillAll (p.backdrop);
    const float c = (float) side * 0.5f;
    proghost::drawGlow (g, k, c, c, R, p);
    proghost::drawGhost (g, k, c, c, R, p);
    return img;
}

// time per frame (ms) of the four ghosts (glow + ghost) at pad size into a pad-sized image, minus the background fill; mask = which characters
static double benchFrames (int N, bool held, int mask = 15)
{
    const juce::Colour padBg (0xff06070a);
    juce::Image img (juce::Image::ARGB, 340, 258, true, juce::SoftwareImageType());
    const juce::Point<float> at[] { { 80, 70 }, { 250, 80 }, { 110, 190 }, { 260, 180 } };
    auto frame = [&] (float t)
    {
        juce::Graphics g (img); g.fillAll (padBg);
        proghost::Pose p[4];
        for (int i = 0; i < 4; ++i) { p[i] = poseFor (states[held ? 3 : 1], padBg, t); p[i].held = held && i == 0; p[i].glitch = p[i].held ? 0.35f : 0.0f; }
        for (int i = 0; i < 4; ++i) if (mask & (1 << i)) proghost::drawGlow (g, kinds[i], at[i].x, at[i].y, proghost::radiusForPadSize (p[i].held ? 40.0f : 30.0f), p[i]);
        for (int i = 0; i < 4; ++i) if (mask & (1 << i)) proghost::drawGhost (g, kinds[i], at[i].x, at[i].y, proghost::radiusForPadSize (p[i].held ? 40.0f : 30.0f), p[i]);
    };
    // CPU time of this thread (the build box is shared: wall time would count other jobs' time slices)
    auto cpu = [] { timespec ts {}; clock_gettime (CLOCK_THREAD_CPUTIME_ID, &ts); return (double) ts.tv_sec + (double) ts.tv_nsec * 1.0e-9; };
    for (int i = 0; i < 50; ++i) frame ((float) i / 30.0f);
    const double t0 = cpu();
    for (int i = 0; i < N; ++i) frame ((float) i / 30.0f);
    const double t1 = cpu();
    for (int i = 0; i < N; ++i) { juce::Graphics g (img); g.fillAll (padBg); }
    const double t2 = cpu();
    return ((t1 - t0) - (t2 - t1)) * 1000.0 / N;
}

static int ghostBench (int N, int mask)
{
    if (mask > 0) { printf ("  mask %d: %.3f ms\n", mask, benchFrames (N, (mask & 32) != 0, mask & 31)); return 0; }
    for (int i = 0; i < 4; ++i) printf ("  %-5s alone: %.3f ms\n", kindName (kinds[i]), benchFrames (N / 4, false, 1 << i));
    printf ("  all four idle: %.3f ms, dragged: %.3f ms\n", benchFrames (N, false), benchFrames (N, true));
    return 0;
}

static int ghostTests (const juce::File& dir)
{
    namespace th = ghosty::themes;
    dir.createDirectory();
    section ("GHOSTS: the four XY-pad characters (ProGhostArt.h)");
    const juce::Colour padBg (0xff06070a);
    const th::Theme themes[] { th::Theme::Dark, th::Theme::Purple, th::Theme::Light };
    const float sizes[] { 22.0f, 44.0f, 120.0f };
    auto sideFor = [] (float R) { return (int) std::ceil (R * 4.2f); };

    // ---- contrast: the neon rim must read on every backdrop it can sit on (the pad is dark in every theme; the theme windows as a check)
    for (auto k : kinds)
        for (auto bg : { padBg, th::get (th::Theme::Dark).window, th::get (th::Theme::Purple).window, th::get (th::Theme::Light).window })
        {
            const bool light = proghost::isLightBackdrop (bg);
            const double c = th::contrast (proghost::rimColour (k, light), bg);
            CHECK (c >= 3.0, "%-5s rim on #%s: %.1f:1 (>= 3)", kindName (k), bg.toDisplayString (false).toRawUTF8(), c);
        }

    // ---- every ghost x size x theme x state -> PNG, and the contact sheet (rows = character x theme, columns = state x size)
    int colW[3]; for (int i = 0; i < 3; ++i) colW[i] = sideFor (sizes[i]);
    const int labelW = 150, headH = 44, cellW = colW[0] + colW[1] + colW[2] + 12, rowH = colW[2];
    juce::Image sheet (juce::Image::RGB, labelW + 4 * cellW, headH + 12 * rowH, true, juce::SoftwareImageType());
    juce::Graphics sg (sheet);
    sg.fillAll (juce::Colour (0xff101014));
    sg.setColour (juce::Colours::white); sg.setFont (ghosty::font (15.0f));
    for (int s = 0; s < 4; ++s) sg.drawText (juce::String (states[s].name) + "   (R 22 / 44 / 120)", labelW + s * cellW, 0, cellW, headH, juce::Justification::centred);
    int row = 0, files = 0;
    int glowMin = 999; juce::String glowWorst;
    for (auto k : kinds)
        for (auto t : themes)
        {
            const auto bg = th::get (t).window;
            const int y0 = headH + row * rowH;
            sg.setColour (bg); sg.fillRect (0, y0, sheet.getWidth(), rowH);
            sg.setColour (th::get (t).ink); sg.setFont (ghosty::font (16.0f));
            sg.drawText (juce::String (kindName (k)) + "\n" + th::displayName (t), 8, y0, labelW - 8, rowH, juce::Justification::centredLeft);
            for (int s = 0; s < 4; ++s)
            {
                int x = labelW + s * cellW;
                for (int z = 0; z < 3; ++z)
                {
                    const auto p = poseFor (states[s], bg);
                    auto img = tile (k, sizes[z], p, colW[z]);
                    savePng (img, dir.getChildFile (juce::String (kindName (k)).toLowerCase() + "_" + th::toString (t) + "_" + juce::String ((int) sizes[z]) + "_" + states[s].name + ".png"));
                    ++files;
                    sg.drawImageAt (img, x, y0 + (rowH - colW[z]) / 2);
                    // the glow must tint the backdrop around the ghost: the median (over 16 points on a ring at 1.3 R; the median skips the
                    // odd star / wave / flame on the ring) of the largest channel difference from the backdrop, in 8-bit levels
                    {
                        const float c = (float) colW[z] * 0.5f;
                        std::vector<int> ds;
                        for (int q = 0; q < 16; ++q)
                        {
                            const float a = (float) q / 16.0f * juce::MathConstants<float>::twoPi;
                            const auto px = img.getPixelAt ((int) (c + std::cos (a) * sizes[z] * 1.3f), (int) (c + std::sin (a) * sizes[z] * 1.3f));
                            ds.push_back (juce::jmax (std::abs (px.getRed() - bg.getRed()), std::abs (px.getGreen() - bg.getGreen()), std::abs (px.getBlue() - bg.getBlue())));
                        }
                        std::sort (ds.begin(), ds.end());
                        const int med = (ds[7] + ds[8]) / 2;
                        if (med < glowMin) { glowMin = med; glowWorst = juce::String (kindName (k)) + " " + th::displayName (t) + " R" + juce::String ((int) sizes[z]) + " " + states[s].name; }
                    }
                    x += colW[z] + 6;
                }
            }
            ++row;
        }
    CHECK (files == 144, "%d ghost PNGs written to %s", files, dir.getFullPathName().toRawUTF8());
    CHECK (glowMin >= 16, "the glow tints the backdrop around every ghost on every backdrop: lowest median on the 1.3 R ring %d levels (%s; >= 16)", glowMin, glowWorst.toRawUTF8());
    savePng (sheet, dir.getChildFile ("contact_sheet.png"));
    // one row of the four at R 120 on the pad's backdrop (idle, params high)
    {
        const int s120 = sideFor (120.0f);
        juce::Image strip (juce::Image::RGB, s120 * 4, s120, true, juce::SoftwareImageType());
        juce::Graphics rg (strip);
        for (int i = 0; i < 4; ++i) rg.drawImageAt (tile (kinds[i], 120.0f, poseFor (states[1], padBg), s120), i * s120, 0);
        savePng (strip, dir.getChildFile ("row_120.png"));
    }
    printf ("  contact sheet: %s (%d x %d)\n", dir.getChildFile ("contact_sheet.png").getFullPathName().toRawUTF8(), sheet.getWidth(), sheet.getHeight());

    // ---- the name on the body: inside the silhouette (every size the pad uses, both parameter ends, a few animation frames, both facings)
    //      and readable on the body under it (>= 3:1 against every body pixel sampled under the letters, every theme backdrop)
    {
        int outside = 0, checked = 0; juce::String firstBad;
        for (auto k : kinds)
            for (float R : { 16.5f, 20.0f, 23.0f, 26.0f, 44.0f, 120.0f })
                for (float amt : { 0.0f, 0.5f, 1.0f })
                    for (float tt : { 0.0f, 0.7f, 1.9f, 3.3f })
                        for (bool fl : { false, true })
                        {
                            proghost::Pose p; p.t = tt; p.x = p.y = amt; p.flip = fl;
                            const float c = 300.0f;
                            auto body = proghost::bodyPathFor (k, p, c, c, R);
                            auto tb = proghost::titleBounds (k, c, c, R).reduced (0.5f);
                            ++checked;
                            bool in = true;
                            for (auto pt : { tb.getTopLeft(), tb.getTopRight(), tb.getBottomLeft(), tb.getBottomRight(),
                                             juce::Point<float> (tb.getCentreX(), tb.getY()), juce::Point<float> (tb.getCentreX(), tb.getBottom()),
                                             juce::Point<float> (tb.getX(), tb.getCentreY()), juce::Point<float> (tb.getRight(), tb.getCentreY()) })
                                in &= body.contains (pt);
                            if (! in) { if (outside++ == 0) firstBad = juce::String (kindName (k)) + " R" + juce::String (R, 1) + " amt " + juce::String (amt, 1) + " t " + juce::String (tt, 1) + (fl ? " flipped" : ""); }
                        }
        CHECK (outside == 0, "the name sits inside the body in %d of %d poses%s%s", checked - outside, checked, outside ? ", first outside: " : "", firstBad.toRawUTF8());
        for (auto k : kinds)
        {
            double worst = 99.0; float pxAt16 = proghost::titlePx (k, 16.5f), pxAt20 = proghost::titlePx (k, 20.0f);
            for (auto t : themes)
                for (float R : { 20.0f, 44.0f, 120.0f })
                    for (float amt : { 0.05f, 0.95f })
                    {
                        const int sd = sideFor (R);
                        proghost::Pose p = poseFor ({ "x", false, amt }, th::get (t).window); p.title = false;
                        auto img = tile (k, R, p, sd);
                        const auto tb = proghost::titleBounds (k, (float) sd * 0.5f, (float) sd * 0.5f, R).reduced (1.0f);
                        for (int gy = 0; gy < 3; ++gy)
                            for (int gx = 0; gx < 7; ++gx)
                            {
                                const auto px = img.getPixelAt ((int) (tb.getX() + tb.getWidth() * ((float) gx + 0.5f) / 7.0f), (int) (tb.getY() + tb.getHeight() * ((float) gy + 0.5f) / 3.0f));
                                worst = juce::jmin (worst, th::contrast (proghost::titleColour (k), px));
                            }
                    }
            CHECK (worst >= 3.0, "%-5s name on its body: lowest contrast %.1f:1 (>= 3) | %.1f px letters at R 16.5, %.1f px at R 20", kindName (k), worst, pxAt16, pxAt20);
        }
    }

    // ---- distinct characters: all four share the piano GHOST CURSOR's body now (owner, the cursor-style restyle), so they must tell apart
    //      by their colours and extras: any two tiles (R 44, idle, no glow) differ clearly (RGB distance > 96) in > 40 % of their ink pixels
    {
        const float R = 44.0f; const int side = sideFor (R);
        std::vector<juce::Image> imgs;
        for (auto k : kinds)
        {
            juce::Image img (juce::Image::ARGB, side, side, true, juce::SoftwareImageType());
            { juce::Graphics g (img); proghost::Pose p; p.t = 0.0f; p.x = p.y = 0.5f; p.backdrop = padBg; proghost::drawGhost (g, k, (float) side * 0.5f, (float) side * 0.5f, R, p); }
            imgs.push_back (img);
        }
        double worst = 1.0; juce::String pair;
        for (int a = 0; a < 4; ++a) for (int b = a + 1; b < 4; ++b)
        {
            int ink = 0, differ = 0;
            for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x)
            {
                const auto pa = imgs[(size_t) a].getPixelAt (x, y), pb = imgs[(size_t) b].getPixelAt (x, y);
                if (pa.getAlpha() < 128 && pb.getAlpha() < 128) continue;
                ++ink;
                differ += std::abs (pa.getRed() - pb.getRed()) + std::abs (pa.getGreen() - pb.getGreen()) + std::abs (pa.getBlue() - pb.getBlue()) + std::abs (pa.getAlpha() - pb.getAlpha()) > 96 ? 1 : 0;
            }
            const double f = ink > 0 ? (double) differ / ink : 0.0;
            if (f < worst) { worst = f; pair = juce::String (kindName (kinds[a])) + "/" + kindName (kinds[b]); }
        }
        CHECK (worst > 0.4, "distinct characters: the most alike pair (%s) differs in %.0f %% of its pixels (> 40 %%)", pair.toRawUTF8(), worst * 100.0);
    }

    // ---- the parameters show: low vs high differ visibly for each character (pixels changed at R 44, idle)
    for (auto k : kinds)
    {
        auto lo = tile (k, 44.0f, poseFor (states[0], padBg), sideFor (44.0f)), hi = tile (k, 44.0f, poseFor (states[1], padBg), sideFor (44.0f));
        int diff = 0;
        for (int y = 0; y < lo.getHeight(); ++y) for (int x = 0; x < lo.getWidth(); ++x)
        { auto a = lo.getPixelAt (x, y), b = hi.getPixelAt (x, y); diff += std::abs (a.getRed() - b.getRed()) + std::abs (a.getGreen() - b.getGreen()) + std::abs (a.getBlue() - b.getBlue()) > 48 ? 1 : 0; }
        CHECK (diff > lo.getWidth() * lo.getHeight() / 20, "%-5s parameter low -> high changes %d px (> 5 %% of the tile)", kindName (k), diff);
    }
    // ---- the idle animation moves (two frames 0.5 s apart differ) and is cheap
    for (auto k : kinds)
    {
        auto a = tile (k, 22.0f, poseFor (states[0], padBg, 1.0f), sideFor (22.0f)), b = tile (k, 22.0f, poseFor (states[0], padBg, 1.5f), sideFor (22.0f));
        int diff = 0; for (int y = 0; y < a.getHeight(); ++y) for (int x = 0; x < a.getWidth(); ++x) diff += a.getPixelAt (x, y) != b.getPixelAt (x, y) ? 1 : 0;
        CHECK (diff > 20, "%-5s idle animation: %d px change in 0.5 s at R 22", kindName (k), diff);
    }

    // ---- paint time: the four ghosts (glow + ghost) at pad size, into a pad-sized image, software renderer
    {
        const int N = 200;
        juce::Image img (juce::Image::ARGB, 340, 258, true, juce::SoftwareImageType());
        const juce::Point<float> at[] { { 80, 70 }, { 250, 80 }, { 110, 190 }, { 260, 180 } };
        for (bool held : { false, true })
        {
            // the best of 25 short runs of thread CPU time: the build box is shared with other jobs (their cache / core contention only
            // ever adds time; the minimum is the paint's own cost)
            double ms = 1.0e9; for (int run = 0; run < 25; ++run) ms = juce::jmin (ms, benchFrames (N, held));
            CHECK (ms < 0.3, "paint time, four ghosts at pad size (%s): %.3f ms per frame (< 0.3 ms)", held ? "one dragged + glitching, params high" : "idle, params high", ms);
        }
        // for reference: the classic Shared/GhostArt ghosts with the pad's old radial glow
        auto t0 = juce::Time::getHighResolutionTicks();
        for (int n = 0; n < N; ++n)
        {
            juce::Graphics g (img); g.fillAll (padBg);
            for (int i = 0; i < 4; ++i)
            {
                const float R = 26.0f;
                juce::ColourGradient glow (ghosty::col::ecto.withAlpha (0.42f), at[i].x, at[i].y, ghosty::col::ecto.withAlpha (0.0f), at[i].x + R * 2.2f, at[i].y, true);
                g.setGradientFill (glow); g.fillEllipse (at[i].x - R * 2.2f, at[i].y - R * 2.2f, R * 4.4f, R * 4.4f);
            }
            for (int i = 0; i < 4; ++i) ghosty::drawGhost (g, ghosty::lookFor (4 + i * 6), at[i].x, at[i].y, 30.0f);
        }
        printf ("  (classic ghosts + old glow, same frame: %.3f ms incl. background fill)\n", juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0) * 1000.0 / N);
    }

    // ---- the real pad in the editor: the hook is installed and the four characters draw in it (shots in every theme)
    {
        auto p = makeProc (progOf ("Trap Starter Stack"));
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        auto* pe = dynamic_cast<ProEditor*> (ed.get());
        pe->setSize (ProEditor::W, ProEditor::H); pump (30);
        ghosty::XYGhostPad* pad = nullptr;
        std::function<void (juce::Component&)> walk = [&] (juce::Component& c) { for (auto* k : c.getChildren()) { if (auto* x = dynamic_cast<ghosty::XYGhostPad*> (k)) pad = x; walk (*k); } };
        walk (*pe);
        CHECK (pad != nullptr && (bool) pad->ghostPainter, "the Synth page's XY pad uses the round-3 ghost art (ghostPainter installed)");
        if (pad != nullptr)
        {
            pad->setSize (juce::jmax (pad->getWidth(), 300), juce::jmax (pad->getHeight(), 258));
            for (int t : { 0, 1, 2 })
            {
                pe->setTheme (t); pump (5);
                savePng (pad->createComponentSnapshot (pad->getLocalBounds(), true, 2.0f), dir.getChildFile (juce::String ("pad_") + (t == 0 ? "black" : t == 1 ? "purple" : "light") + ".png"));
            }
            pe->setTheme (0);
            // the same pad with the first round-3 idle size (16.5 px): the owner asked to see both
            proghost::padIdleRadius() = 16.5f;
            savePng (pad->createComponentSnapshot (pad->getLocalBounds(), true, 2.0f), dir.getChildFile ("pad_black_idle16.png"));
            proghost::padIdleRadius() = 20.0f;
            // the classic look is still there for anyone who does not opt in
            auto keep = pad->ghostPainter; pad->ghostPainter = nullptr;
            savePng (pad->createComponentSnapshot (pad->getLocalBounds(), true, 2.0f), dir.getChildFile ("pad_classic_before.png"));
            pad->ghostPainter = keep;
        }
    }
    return 0;
}
} // namespace ghosttest
