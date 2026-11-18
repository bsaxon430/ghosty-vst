// Included by TrapTests.cpp after GhostArtTests.inc / GlassTests.inc (uses makeProc, progOf, pump, CHECK, findChild, ghosttest::*).
//   ProTests padghosts [dir]  -> the XY pad ghosts look where they move (like the keyboard's GHOST CURSOR) and glitch while dragged:
//                                 8 drag directions with synthetic mouse events -> the gaze follows; neutral at rest; eases back after
//                                 the release; automation moves -> a gentler look; the drag glitch rises with the drag and decays to 0;
//                                 shots: every direction for every ghost and theme (pad-ghost-<theme>-<ghost>-<dir>.png), a contact sheet
//                                 of the 8 directions, drag + glitch frames of the real pad, and the cursor-style restyle at rest.
namespace padghosttest
{
using proghost::Kind;

static const char* dirName[] { "right", "down-right", "down", "down-left", "left", "up-left", "up", "up-right" };
static juce::Point<float> dirVec (int d) { const float a = (float) d * juce::MathConstants<float>::pi * 0.25f; return { std::cos (a), std::sin (a) }; }   // screen space (y down)

static juce::Image ghostTile (Kind k, float R, proghost::Pose p, int side)
{
    juce::Image img (juce::Image::ARGB, side, side, true, juce::SoftwareImageType());
    juce::Graphics g (img);
    g.fillAll (p.backdrop);
    const float c = (float) side * 0.5f;
    proghost::drawGlow (g, k, c, c, R, p);
    proghost::drawGhost (g, k, c, c, R, p);
    return img;
}

static int padGhostTests (const juce::File& dir)
{
    namespace th = ghosty::themes;
    dir.createDirectory();
    section ("PAD GHOSTS: the XY pad ghosts look where they move and glitch while dragged (like the keyboard's GHOST CURSOR)");
    static const char* themeName[] { "black", "purple", "light" };
    const th::Theme themes[] { th::Theme::Dark, th::Theme::Purple, th::Theme::Light };

    // ---- 1) the pure drawing: every ghost x theme x direction (gaze length 1, no glitch) + one glitch frame, and the contact sheet
    {
        const float R = 44.0f; const int side = (int) std::ceil (R * 4.2f);
        juce::Image sheet (juce::Image::RGB, side * 9, side * 4, true, juce::SoftwareImageType());
        juce::Graphics sg (sheet);
        int files = 0, moved = 0;
        for (int ti = 0; ti < 3; ++ti)
            for (int ki = 0; ki < 4; ++ki)
            {
                const auto k = ghosttest::kinds[ki];
                const auto bg = th::get (themes[ti]).window;
                proghost::Pose rest = ghosttest::poseFor (ghosttest::states[1], bg);
                const auto restImg = ghostTile (k, R, rest, side);
                for (int d = 0; d < 8; ++d)
                {
                    auto p = rest; p.gaze = dirVec (d);
                    auto img = ghostTile (k, R, p, side);
                    ghosttest::savePng (img, dir.getChildFile (juce::String ("pad-ghost-") + themeName[ti] + "-" + juce::String (ghosttest::kindName (k)).toLowerCase() + "-" + dirName[d] + ".png"));
                    ++files;
                    int diff = 0; for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) diff += img.getPixelAt (x, y) != restImg.getPixelAt (x, y) ? 1 : 0;
                    moved += diff > 40 ? 1 : 0;
                    if (ti == 0) sg.drawImageAt (img, d * side, ki * side);
                }
                auto gp = rest; gp.held = true; gp.glitch = 0.6f; gp.dragGlitch = 0.9f; gp.gaze = dirVec (0);
                auto gimg = ghostTile (k, R, gp, side);
                ghosttest::savePng (gimg, dir.getChildFile (juce::String ("pad-ghost-") + themeName[ti] + "-" + juce::String (ghosttest::kindName (k)).toLowerCase() + "-glitch.png"));
                ++files;
                if (ti == 0) sg.drawImageAt (gimg, 8 * side, ki * side);
            }
        sg.setColour (juce::Colours::white); sg.setFont (ghosty::font (14.0f));
        for (int d = 0; d < 8; ++d) sg.drawText (dirName[d], d * side, 4, side, 18, juce::Justification::centred);
        sg.drawText ("glitch", 8 * side, 4, side, 18, juce::Justification::centred);
        ghosttest::savePng (sheet, dir.getChildFile ("pad-ghost-directions-sheet.png"));
        CHECK (files == 3 * 4 * 9, "%d direction / glitch PNGs + pad-ghost-directions-sheet.png in %s", files, dir.getFullPathName().toRawUTF8());
        CHECK (moved == 3 * 4 * 8, "every direction changes the drawing (eyes / lean): %d of %d", moved, 3 * 4 * 8);
    }

    // ---- 2) the pupils move to the side of the eye the ghost looks to (pupil centroid vs the whole eye's, looking one way vs the
    //      opposite way; TONE at R 29: the
    //      biggest size without the crisp rim, which has the pupils' cyan)
    {
        const float R = 29.0f; const int side = (int) std::ceil (R * 4.2f);
        int ok = 0; juce::String report;
        // the pupil centroid relative to the eye centroid, for a gaze g (only pixels well inside the body: not the backdrop round the head)
        auto pupilInEye = [&] (juce::Point<float> gz)
        {
            proghost::Pose q = ghosttest::poseFor (ghosttest::states[0], juce::Colour (0xff06070a)); q.title = false; q.gaze = gz;
            auto b = ghostTile (Kind::Tone, R, q, side);
            const auto body = proghost::bodyPathFor (Kind::Tone, q, (float) side * 0.5f, (float) side * 0.5f, R);
            double ex = 0, ey = 0, en = 0, px = 0, py = 0, pn = 0;
            const juce::Rectangle<int> face ((int) ((float) side * 0.5f - 0.15f * R), (int) ((float) side * 0.5f - 0.6f * R), (int) (0.95f * R), (int) (0.65f * R));
            for (int y = face.getY(); y < face.getBottom(); ++y) for (int x = face.getX(); x < face.getRight(); ++x)
            {
                const float fx = (float) x + 0.5f, fy = (float) y + 0.5f;
                if (! (body.contains (fx - 2.5f, fy) && body.contains (fx + 2.5f, fy) && body.contains (fx, fy - 2.5f) && body.contains (fx, fy + 2.5f))) continue;
                auto c = b.getPixelAt (x, y);
                const bool pupil = c.getBlue() > 200 && c.getGreen() > 180 && c.getRed() < 140, eye = c.getRed() < 60 && c.getGreen() < 70 && c.getBlue() < 80;
                if (pupil) { px += x; py += y; ++pn; }
                if (pupil || eye) { ex += x; ey += y; ++en; }
            }
            return juce::Point<float> ((float) (px / juce::jmax (1.0, pn) - ex / juce::jmax (1.0, en)), (float) (py / juce::jmax (1.0, pn) - ey / juce::jmax (1.0, en)));
        };
        for (int d = 0; d < 8; ++d)
        {
            const auto v = dirVec (d), m = pupilInEye (v) - pupilInEye (-v);    // looking one way vs the opposite way
            float err = std::abs (std::atan2 (m.y, m.x) - std::atan2 (v.y, v.x)); if (err > juce::MathConstants<float>::pi) err = juce::MathConstants<float>::twoPi - err;
            const bool good = m.getDistanceFromOrigin() > 0.8f && err < juce::degreesToRadians (25.0f);
            ok += good ? 1 : 0;
            report << dirName[d] << " " << juce::String (juce::radiansToDegrees (std::atan2 (m.y, m.x)), 0) << "deg/" << juce::String (m.getDistanceFromOrigin(), 1) << "px" << (good ? "" : "(WRONG)") << "  ";
        }
        CHECK (ok == 8, "the pupils sit towards the gaze inside the eyes in %d of 8 directions (within 25 deg)", ok);
        printf ("      pupil angle: %s\n", report.toRawUTF8());
    }

    // ---- 3) the real pad: synthetic drags in the 8 directions
    auto p = makeProc (progOf ("Trap Starter Stack"));
    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
    auto* pe = dynamic_cast<ProEditor*> (ed.get());
    pe->setSize (ProEditor::W, ProEditor::H); pe->setTheme (0); pe->showTab (0); pump (60);
    auto* pad = findChild<ghosty::XYGhostPad> (*ed);
    CHECK (pad != nullptr && pad->numGhosts() == 4, "the SYNTH page's XY pad with its four ghosts");
    if (pad == nullptr || pad->numGhosts() < 4) return failures;
    auto src = juce::Desktop::getInstance().getMainMouseSource();
    const juce::ModifierKeys lb (juce::ModifierKeys::leftButtonModifier);
    auto ev = [&] (juce::Point<float> at)
    { const auto now = juce::Time::getCurrentTime(); return juce::MouseEvent (src, at, lb, 1.0f, 0, 0, 0, 0, pad, pad, now, at, now, 1, false); };
    auto settle = [&] (int n = 40) { for (int i = 0; i < n; ++i) pad->stepForTest(); };
    const int gi = 0;   // TONE
    const auto centre = pad->getLocalBounds().toFloat().getCentre();
    auto dragTo = [&] (juce::Point<float> to)
    {   // pick the ghost up where it is and carry it (no gaze checks): parks it
        auto at = pad->ghostPos (gi);
        pad->mouseDown (ev (at));
        for (int s = 1; s <= 10; ++s) { pad->mouseDrag (ev (at + (to - at) * ((float) s / 10.0f))); pad->stepForTest(); }
        pad->mouseUp (ev (to));
        settle();
    };
    // keep the other three out of the way (the pad picks the nearest ghost)
    settle();
    {
        bool rest = true;
        for (int i = 0; i < 4; ++i) rest &= pad->gazeOf (i).isOrigin() && pad->dragGlitchOf (i) == 0.0f;
        CHECK (rest, "at rest every ghost has the neutral gaze (0, 0) and no drag glitch");
    }
    int okDir = 0; juce::String dirReport;
    for (int d = 0; d < 8; ++d)
    {
        dragTo (centre);
        const auto v = dirVec (d);
        auto at = pad->ghostPos (gi);
        pad->mouseDown (ev (at));
        float glitchPeak = 0;
        for (int s = 1; s <= 6; ++s) { pad->mouseDrag (ev (at + v * (8.0f * (float) s))); pad->stepForTest(); glitchPeak = juce::jmax (glitchPeak, pad->dragGlitchOf (gi)); }
        const auto gz = pad->gazeOf (gi);
        const float want = std::atan2 (v.y, v.x), got = std::atan2 (gz.y, gz.x);
        float err = std::abs (got - want); if (err > juce::MathConstants<float>::pi) err = juce::MathConstants<float>::twoPi - err;
        const bool good = gz.getDistanceFromOrigin() > 0.5f && err < juce::degreesToRadians (12.0f) && glitchPeak > 0.3f;
        okDir += good ? 1 : 0;
        dirReport << dirName[d] << " " << juce::String (juce::radiansToDegrees (got), 0) << "deg/" << juce::String (gz.getDistanceFromOrigin(), 2) << (good ? "" : " (WRONG)") << "  ";
        // a shot of the real pad mid-drag (gaze + glitch) in each theme for this direction
        for (int t = 0; t < 3; ++t)
        {
            pe->setTheme (t);
            auto img = pad->createComponentSnapshot (pad->getLocalBounds(), true, 2.0f);
            ghosttest::savePng (img, dir.getChildFile (juce::String ("pad-ghost-pad-") + themeName[t] + "-" + dirName[d] + ".png"));
        }
        pe->setTheme (0);
        pad->mouseUp (ev (at + v * 48.0f));
    }
    CHECK (okDir == 8, "dragging TONE in 8 directions: the gaze follows the drag (within 12 deg, strength > 0.5, glitch > 0.3) in %d of 8", okDir);
    printf ("      %s\n", dirReport.toRawUTF8());

    // ---- 4) the release: the gaze eases back to neutral within ~0.4 s, the glitch decays to 0 within ~0.35 s (40 frames per second)
    {
        dragTo (centre);
        auto at = pad->ghostPos (gi);
        pad->mouseDown (ev (at));
        for (int s = 1; s <= 6; ++s) { pad->mouseDrag (ev (at + juce::Point<float> (10.0f * (float) s, 0.0f))); pad->stepForTest(); }
        const float g0 = pad->dragGlitchOf (gi);
        // held still: it keeps looking the way it went, the glitch fades
        for (int i = 0; i < 4; ++i) pad->stepForTest();
        const float heldGaze = pad->gazeOf (gi).x;
        pad->mouseUp (ev (at + juce::Point<float> (60.0f, 0.0f)));
        int framesGaze = -1, framesGlitch = -1;
        std::vector<float> curve;
        for (int i = 1; i <= 40; ++i)
        {
            pad->stepForTest(); curve.push_back (pad->dragGlitchOf (gi));
            if (framesGaze < 0 && pad->gazeOf (gi).isOrigin()) framesGaze = i;
            if (framesGlitch < 0 && pad->dragGlitchOf (gi) == 0.0f) framesGlitch = i;
        }
        CHECK (g0 > 0.5f, "the drag glitch rises while dragging fast: %.2f", g0);
        CHECK (heldGaze > 0.5f, "held still after the drag, the ghost keeps looking the way it went (gaze x %.2f)", heldGaze);
        CHECK (framesGaze > 0 && framesGaze <= 16, "after the release the gaze is back to neutral in %d frames = %.2f s (<= 0.4 s)", framesGaze, framesGaze / 40.0);
        CHECK (framesGlitch > 0 && framesGlitch <= 14, "the drag glitch decays to 0 in %d frames = %.2f s (~0.3 s)", framesGlitch, framesGlitch / 40.0);
        bool mono = true; for (size_t i = 1; i < curve.size(); ++i) mono &= curve[i] <= curve[i - 1];
        CHECK (mono, "the glitch only decays after the release (monotonic)");
    }

    // ---- 5) automation / MORPH / macros: the value moves without a drag -> it looks the way it moves, gentler; then back to neutral
    {
        settle();
        auto* cut = p->apvts.getParameter ("cutoff");
        cut->setValueNotifyingHost (0.3f); settle();
        float mx = 0, my = 0; float gl = 0;
        for (int s = 1; s <= 8; ++s) { cut->setValueNotifyingHost (0.3f + 0.04f * (float) s); pad->stepForTest(); mx = juce::jmax (mx, pad->gazeOf (gi).x); my = juce::jmax (my, std::abs (pad->gazeOf (gi).y)); gl = juce::jmax (gl, pad->dragGlitchOf (gi)); }
        CHECK (mx > 0.2f && mx <= 0.56f && my < 0.05f, "automation moving TONE right: it looks right, gentler (gaze x %.2f <= 0.55, y %.2f), no drag glitch (%.2f)", mx, my, gl);
        CHECK (gl == 0.0f, "no drag glitch from automation");
        settle (20);
        CHECK (pad->gazeOf (gi).isOrigin(), "and back to the neutral gaze once it stops");
    }

    // ---- 6) shots of the restyle at rest in every theme (the real pad), next to main's pad shots if a 'before' folder is given
    for (int t = 0; t < 3; ++t)
    {
        pe->setTheme (t); settle();
        ghosttest::savePng (pad->createComponentSnapshot (pad->getLocalBounds(), true, 2.0f), dir.getChildFile (juce::String ("pad-ghost-restyle-pad-") + themeName[t] + ".png"));
    }
    pe->setTheme (0);

    // ---- 7) every name a pad ghost can have, in both layouts (and STUDIO's OSC 1 / OSC 2 / BOTH switch for the MORPH ghost),
    //      draws the new art (never the classic fallback): kindFor() knows it and the painter takes it
    {
        auto allPads = [&]
        {
            std::vector<ghosty::XYGhostPad*> v;
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c) { for (auto* k : c.getChildren()) { if (auto* x = dynamic_cast<ghosty::XYGhostPad*> (k)) v.push_back (x); walk (*k); } };
            walk (*pe); return v;
        };
        auto studioSwitch = [&] (const juce::String& text) -> juce::TextButton*
        {
            juce::TextButton* found = nullptr;
            std::function<void (juce::Component&)> walk = [&] (juce::Component& c) { for (auto* k : c.getChildren()) { if (auto* b = dynamic_cast<juce::TextButton*> (k)) if (b->getButtonText() == text && b->getRadioGroupId() == 0x7a11) found = b; walk (*k); } };
            walk (*pe); return found;
        };
        juce::StringArray names; int unknown = 0;
        auto collect = [&] { for (auto* pd : allPads()) for (auto& n : pd->ghostNames()) { names.addIfNotAlreadyThere (n); if (proghost::kindFor (n) == Kind::Classic || ! (bool) pd->ghostPainter) ++unknown; } };
        pe->setLayout (0); pe->showTab (0); pump (40); collect();
        // (bring the MORPH ghosts out from under SPACE / HEAT so the shots show their names)
        for (auto id : { "o1_pos", "o2_pos" }) if (auto* q = p->apvts.getParameter (id)) q->setValueNotifyingHost (0.5f);
        for (auto id : { "o1_det", "o2_det" }) if (auto* q = p->apvts.getParameter (id)) q->setValueNotifyingHost (0.7f);
        pe->setLayout (1); pe->showTab (0); pump (60); collect();
        static const char* targets[] { "OSC 1", "OSC 2", "BOTH" };
        int studioShots = 0;
        for (int tg = 0; tg < 3; ++tg)
        {
            auto* b = studioSwitch (targets[tg]);
            CHECK (b != nullptr, "STUDIO's MORPH-ghost switch has %s", targets[tg]);
            if (b == nullptr) continue;
            b->setToggleState (true, juce::sendNotificationSync); pump (30);
            collect();
            ghosty::XYGhostPad* sp = nullptr;
            for (auto* pd : allPads()) if (pd->isShowing() || sp == nullptr) sp = pd;
            if (sp == nullptr) continue;
            const juce::String tname = juce::String (targets[tg]).replace (" ", "").toLowerCase();
            for (int t = 0; t < 3; ++t)
            {
                pe->setTheme (t); pump (10);
                ghosttest::savePng (sp->createComponentSnapshot (sp->getLocalBounds(), true, 2.0f), dir.getChildFile (juce::String ("pad-ghost-studio-") + themeName[t] + "-" + tname + ".png"));
                ++studioShots;
            }
            pe->setTheme (0);
        }
        if (auto* b = studioSwitch ("OSC 1")) { b->setToggleState (true, juce::sendNotificationSync); pump (20); }
        pe->setLayout (0); pump (20);
        CHECK (unknown == 0 && names.contains ("MORPH 2") && names.contains ("MORPH 1+2"), "every pad ghost name in CLASSIC + STUDIO draws the new art (%d fall back): %s",
               unknown, names.joinIntoString (", ").toRawUTF8());
        CHECK (studioShots == 9, "%d shots of the real STUDIO pad (3 themes x OSC 1 / OSC 2 / BOTH): pad-ghost-studio-*.png", studioShots);
    }

    // ---- 8) the STUDIO name variants through the pad's painter: the restyled MORPH silhouette (not the classic ghost), name on the body
    {
        const float size = 30.0f, R = proghost::radiusForPadSize (size); const int side = 140;
        auto painter = proghost::padPainter();
        auto mask = [&] (const juce::Image& img) { std::vector<bool> m ((size_t) (side * side)); for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) m[(size_t) (y * side + x)] = img.getPixelAt (x, y).getAlpha() > 128; return m; };
        auto iou = [&] (const std::vector<bool>& a, const std::vector<bool>& b) { int i = 0, u = 0; for (size_t k = 0; k < a.size(); ++k) { i += a[k] && b[k]; u += a[k] || b[k]; } return u > 0 ? (double) i / u : 0.0; };
        // the reference: the restyled MORPH drawn directly
        juce::Image ref (juce::Image::ARGB, side, side, true, juce::SoftwareImageType());
        { juce::Graphics g (ref); proghost::Pose p; p.t = 1.3f; proghost::drawGhost (g, Kind::Morph, side * 0.5f, side * 0.5f, R, p); }
        juce::Image classic (juce::Image::ARGB, side, side, true, juce::SoftwareImageType());
        { juce::Graphics g (classic); ghosty::drawGhost (g, ghosty::lookFor (9), side * 0.5f, side * 0.5f, size); }
        const auto refM = mask (ref), clsM = mask (classic);
        juce::Image base;
        for (auto nm : { "MORPH", "MORPH 2", "MORPH 1+2" })
        {
            ghosty::XYGhostPad::Ghost gh; gh.name = nm;
            juce::Image img (juce::Image::ARGB, side, side, true, juce::SoftwareImageType());
            bool used = false;
            { juce::Graphics g (img); used = painter (g, { gh, 1, { side * 0.5f, side * 0.5f }, size, 0.0f, 0.0f, 1.3f, false, juce::Colour (0xff06070a) }); }
            const auto m = mask (img);
            const double toMorph = iou (m, refM), toClassic = iou (m, clsM);
            int lineTwo = 0;
            if (base.isValid()) for (int y = 0; y < side; ++y) for (int x = 0; x < side; ++x) lineTwo += img.getPixelAt (x, y) != base.getPixelAt (x, y) ? 1 : 0;
            if (! base.isValid()) base = img;
            ghosttest::savePng (img, dir.getChildFile (juce::String ("pad-ghost-studio-name-") + juce::String (nm).replace (" ", "_").replace ("+", "plus") + ".png"));
            CHECK (used && toMorph > 0.9 && toClassic < 0.75 && (juce::String (nm) == "MORPH" || lineTwo > 20),
                   "\"%s\": the restyled MORPH art (silhouette IoU %.2f with MORPH, %.2f with the classic ghost)%s", nm, toMorph, toClassic,
                   juce::String (nm) == "MORPH" ? "" : juce::String (", its second word on the hem (" + juce::String (lineTwo) + " px)").toRawUTF8());
        }
    }
    return failures;
}
} // namespace padghosttest
