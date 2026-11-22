// Ghost Mosaic studio: the standard screenshot set and the paint-cost meter of the new mode, and the studio's own checks.
// Owned by the harness engineer. Included by MosaicTests.cpp.
//
//   GhostMosaicTests studio <dir>      the seven standard screenshots, 0-contact.png, shots.txt, paint-cost.txt
//   GhostMosaicTests studio selftest   the new button, the page, the frame clock, the wordmark, an existing control
//   GhostMosaicTests studio small <dir>  extra pictures at the smallest window (1000 x 700): the new mode, PLAY, UNDER THE HOOD
//   GhostMosaicTests studio pages <dir>  the standard set of the EXISTING pages (PLAY, TERRAIN, UNDER THE HOOD) at three window sizes
//
// Everything here is repeatable: a fixed std::srand, and no wall clock. Time moves only by processing audio
// (44.1 kHz, 512-sample buffers, as a host would call it); for every 1/60 s of audio the message loop is emptied
// and the editor is stepped by exactly one frame (Editor::setTestClock stops its own timer).
#include <ctime>

namespace studioh
{
constexpr double kRate = 44100.0;
constexpr int kBuffer = 512;
constexpr int kFrameSamples = 735;          // 1/60 s at 44.1 kHz
constexpr double kDt = 1.0 / 60.0;
constexpr int kCostRuns = 5;

static double cpuMs()
{
    timespec ts;
    clock_gettime (CLOCK_THREAD_CPUTIME_ID, &ts);      // this thread's own CPU time: other programs on the machine do not count
    return (double) ts.tv_sec * 1000.0 + (double) ts.tv_nsec * 1.0e-6;
}

// every message that is waiting is handled (twice over, for the ones those messages post); no sleeping
static void drain()
{
    for (int round = 0; round < 2; ++round)
    {
        bool done = false;
        juce::MessageManager::callAsync ([&done] { done = true; });
        while (! done) pump (1);   // (not 0: on a Mac the dispatch loop does nothing at all when asked to run for 0 ms, and this spun forever)
    }
}

struct Cost { std::vector<double> paint, frame; double flat = 0; };

struct Stats { double mean = 0, p95 = 0, max = 0; };
static Stats statsOf (std::vector<double> v)
{
    Stats s;
    if (v.empty()) return s;
    std::sort (v.begin(), v.end());
    for (double d : v) s.mean += d;
    s.mean /= (double) v.size();
    s.p95 = v[(size_t) juce::jmin ((int) v.size() - 1, (int) std::ceil (0.95 * (double) v.size()) - 1)];
    s.max = v.back();
    return s;
}
static juce::String ms3 (const Stats& s) { return juce::String (s.mean, 2) + " / " + juce::String (s.p95, 2) + " / " + juce::String (s.max, 2); }

// One plug-in with its editor, as a new instance in a host: the built-in drums are the library and the built-in
// sung vowels the target (the effect: the same sung line comes in on the track).
struct Rig
{
    std::unique_ptr<Processor> proc;
    std::unique_ptr<Editor> ed;
    juce::AudioBuffer<float> io { 2, kBuffer }, voice;
    juce::MidiBuffer midi;
    juce::int64 samples = 0, nextFrame = kFrameSamples;
    int voicePos = 0, frames = 0;
    bool feed = true;                       // (the effect) the sung line is coming in on the track

    Rig()
    {
        std::srand (4242);
        juce::Random::getSystemRandom().setSeed (4242);
        Processor::preloadNewInstances = true;
        proc = std::make_unique<Processor>();
        Processor::preloadNewInstances = false;
        proc->prepareToPlay (kRate, kBuffer);
        if (proc->isEffect()) voice = demo::vowelPhrase (kRate, 6);
        ed = std::make_unique<Editor> (*proc);
        ed->setTestClock (true);
        waitIdle (*proc);                   // the built-in sounds are cut and analysed (AUTO BUILD)
        drain();
    }

    // the GENERATE button: builds what changed, then plays from the top
    void generate()
    {
        feed = true;
        proc->generate();
        waitIdle (*proc);
        drain();
        std::srand (4242);                  // (the engine's first piece after a restart is a random one)
    }

    // nothing plays: the instrument has simply not been started; the effect (which plays whatever comes in) is
    // paused and its track is silent
    void silent()
    {
        if (! proc->isEffect()) return;
        proc->engine().pause();
        feed = false;
        drain();
    }

    void set (const char* id, float v) { setP (*proc, id, v); }

    // one frame: 1/60 s of audio, the waiting messages, then the editor, the new mode's page and the wordmark
    void frame (Cost* cost = nullptr, juce::Image* canvas = nullptr)
    {
        while (samples < nextFrame)
        {
            if (proc->isEffect() && feed)
            {
                for (int c = 0; c < 2; ++c) io.copyFrom (c, 0, voice, juce::jmin (c, voice.getNumChannels() - 1), voicePos, kBuffer);
                voicePos = (voicePos + kBuffer) % (voice.getNumSamples() - kBuffer);
            }
            else io.clear();
            proc->processBlock (io, midi);
            if (auto* lt = proc->engine().liveTarget()) lt->waitIdle (200);     // (the effect's live analysis: finished before the next buffer)
            samples += kBuffer;
        }
        nextFrame += kFrameSamples;
        drain();
        ed->frame();                                    // the editor's own tick (status words, buttons)
        auto& page = ed->scopePage();
        page.stepModelForTests (kDt);
        const double t0 = cpuMs();
        page.stepViewForTests (kDt);
        const double t1 = cpuMs();
        ed->wordmark().advance ((float) kDt);
        if (cost != nullptr && canvas != nullptr)
        {
            // a FULL repaint of the page at retina scale 2, into an image made beforehand
            const double p0 = cpuMs();
            {
                juce::Graphics g (*canvas);
                g.addTransform (juce::AffineTransform::scale (2.0f));
                page.paintEntireComponent (g, false);
            }
            cost->paint.push_back (cpuMs() - p0);
            cost->frame.push_back (t1 - t0);
        }
        ++frames;
    }

    void run (double seconds) { for (int i = juce::roundToInt (seconds * 60.0); --i >= 0;) frame(); }

    // The existing pages take their time from the wall clock (PLAY's live view above all), but that clock never
    // counts more than 2.5 frames for one tick. So for their pictures every tick is given exactly that: 2.5 frames
    // of audio (1/24 s) and at least 46 ms of real time since the tick before. The clock's own limit then makes
    // every tick the same size, whatever the machine is doing, and two runs give the same pictures.
    double lastSlowTickMs = 0;
    void slowFrame()
    {
        nextFrame += kFrameSamples * 5 / 2 + (frames & 1);       // 1837.5 samples, taken as 1837 and 1838 in turn
        while (samples < nextFrame)
        {
            if (proc->isEffect() && feed)
            {
                for (int c = 0; c < 2; ++c) io.copyFrom (c, 0, voice, juce::jmin (c, voice.getNumChannels() - 1), voicePos, kBuffer);
                voicePos = (voicePos + kBuffer) % (voice.getNumSamples() - kBuffer);
            }
            else io.clear();
            proc->processBlock (io, midi);
            if (auto* lt = proc->engine().liveTarget()) lt->waitIdle (200);
            samples += kBuffer;
        }
        drain();
        while (juce::Time::getMillisecondCounterHiRes() - lastSlowTickMs < 46.0) juce::Thread::sleep (2);
        lastSlowTickMs = juce::Time::getMillisecondCounterHiRes();
        ed->frame();
        ed->scopePage().stepForTests (kDt * 2.5);
        ed->wordmark().advance ((float) (kDt * 2.5));
        ++frames;
    }

    juce::Image picture() { return ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f); }
    juce::Image pagePicture() { auto& pg = ed->scopePage(); return pg.createComponentSnapshot (pg.getLocalBounds(), true, 1.0f); }

    // The paint cost over `count` frames in a row, after some unmeasured ones (first use of fonts and caches). Five
    // such runs, and the one with the lowest mean counts: this thread's CPU time does not include waiting for other
    // programs, but a busy machine still slows the memory down (in bursts), and noise only ever adds time, so the
    // quietest run is the nearest to the truth.
    Cost measure (int count)
    {
        auto& page = ed->scopePage();
        for (int i = 0; i < 30; ++i) frame();
        juce::Image canvas (juce::Image::ARGB, page.getWidth() * 2, page.getHeight() * 2, true);
        for (int i = 0; i < 12; ++i) { Cost warm; frame (&warm, &canvas); }
        Cost best;
        for (int pass = 0; pass < kCostRuns; ++pass)
        {
            Cost c;
            for (int i = 0; i < count; ++i) frame (&c, &canvas);
            if (pass == 0 || statsOf (c.paint).mean < statsOf (best.paint).mean) best = c;
        }
        // for scale: one flat coat of paint over every pixel of the page, the least any full repaint can cost
        best.flat = 1.0e9;
        for (int i = 0; i < 8; ++i)
        {
            const double t0 = cpuMs();
            {
                juce::Graphics g (canvas);
                g.addTransform (juce::AffineTransform::scale (2.0f));
                g.fillAll (look::ground);
            }
            best.flat = juce::jmin (best.flat, cpuMs() - t0);
        }
        return best;
    }
};

static void savePng (const juce::Image& img, const juce::File& f)
{
    f.deleteFile();
    juce::FileOutputStream os (f);
    juce::PNGImageFormat png;
    png.writeImageToStream (img, os);
}

// half size, each pixel the average of four (a clean small copy, where plain resampling would make the thin text crawl)
static juce::Image halved (const juce::Image& in)
{
    const auto src = in.convertedToFormat (juce::Image::ARGB);
    const int w = src.getWidth() / 2, h = src.getHeight() / 2;
    juce::Image out (juce::Image::ARGB, juce::jmax (1, w), juce::jmax (1, h), true);
    juce::Image::BitmapData a (src, juce::Image::BitmapData::readOnly), b (out, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < h; ++y)
    {
        const auto* l0 = a.getLinePointer (y * 2);
        const auto* l1 = a.getLinePointer (y * 2 + 1);
        auto* d = b.getLinePointer (y);
        for (int x = 0; x < w; ++x)
            for (int c = 0; c < 4; ++c)
                d[x * 4 + c] = (juce::uint8) ((l0[x * 8 + c] + l0[x * 8 + 4 + c] + l1[x * 8 + c] + l1[x * 8 + 4 + c] + 2) / 4);
    }
    return out;
}

static juce::Image fitted (juce::Image img, int w, int h)
{
    while (img.getWidth() >= w * 2 && img.getHeight() >= h * 2) img = halved (img);
    const double s = juce::jmin ((double) w / img.getWidth(), (double) h / img.getHeight());
    return img.rescaled (juce::jmax (1, juce::roundToInt (img.getWidth() * s)), juce::jmax (1, juce::roundToInt (img.getHeight() * s)), juce::Graphics::highResamplingQuality);
}

static bool samePixels (const juce::Image& x, const juce::Image& y)
{
    if (x.getWidth() != y.getWidth() || x.getHeight() != y.getHeight() || x.getFormat() != y.getFormat()) return false;
    juce::Image::BitmapData a (x, juce::Image::BitmapData::readOnly), b (y, juce::Image::BitmapData::readOnly);
    for (int row = 0; row < x.getHeight(); ++row)
        if (std::memcmp (a.getLinePointer (row), b.getLinePointer (row), (size_t) (x.getWidth() * a.pixelStride)) != 0) return false;
    return true;
}

// ---------------------------------------------------------------- the standard set
static int standardSet (const juce::File& dir)
{
    dir.createDirectory();
    juce::StringArray notes;
    auto keep = [&] (const juce::Image& img, const juce::String& name, const juce::String& what)
    {
        savePng (img, dir.getChildFile (name));
        notes.add (name.paddedRight (' ', 22) + what);
        return img;
    };
    juce::Image shots[7];
    Cost costSmall, costLarge;
    juce::String viewName;
    {
        Rig r;
        viewName = r.ed->scopePage().buttonName();
        r.ed->setPage (3);
        r.generate();
        // 1: default settings, about 2 s in
        r.run (2.0);
        shots[0] = keep (r.picture(), "1-inside-a.png", "the new mode, 2.0 s after GENERATE, default settings (algorithm basic)");
        // 2: the same run, later: a synaptic algorithm, novelty and boredom turned up
        r.set ("algo", 2); r.set ("novelty", 0.6f); r.set ("boredom", 0.98f);
        r.run (2.5);
        shots[1] = keep (r.picture(), "2-inside-b.png", "the same run 2.5 s later: algorithm synaptic, novelty 0.60, boredom 0.98");
        // 3: stickyness and search stretch turned up (novelty and boredom back where they were)
        r.set ("algo", 0); r.set ("novelty", 0.0f); r.set ("boredom", 0.9f); r.set ("stickyness", 0.8f); r.set ("stretch", 4);
        r.run (2.5);
        shots[2] = keep (r.picture(), "3-inside-c.png", "2.5 s later: algorithm basic, stickyness 0.80, search stretch 4");
        // 4: four moments 100 ms apart, default settings again, each at half size
        r.set ("stickyness", 0.0f); r.set ("stretch", 1);
        r.run (1.5);
        {
            juce::Image strip (juce::Image::ARGB, r.ed->getWidth() * 2, r.ed->getHeight() / 2, true);
            juce::Graphics g (strip);
            g.fillAll (look::ground);
            for (int k = 0; k < 4; ++k)
            {
                g.drawImageAt (halved (r.picture()), k * (r.ed->getWidth() / 2), 0);
                if (k < 3) for (int i = 0; i < 6; ++i) r.frame();
            }
            shots[3] = strip;
        }
        keep (shots[3], "4-inside-strip.png", "four moments 100 ms apart (6 frames), default settings, each at half size");
        // the paint cost at the default size, playing
        costSmall = r.measure (120);
        // 5: UNDER THE HOOD, the main controls page, with the new button in the header
        r.ed->setPage (2);
        for (int i = 0; i < 12; ++i) r.frame();
        shots[4] = keep (r.picture(), "5-controls.png", "UNDER THE HOOD at 1200 x 760, playing, default settings");
        // 6: the new mode at the largest window, playing; and its paint cost there
        r.ed->setPage (3);
        r.ed->setSize (2000, 1400);
        r.run (1.0);
        shots[5] = keep (r.picture(), "6-inside-large.png", "the new mode at 2000 x 1400, playing, default settings");
        costLarge = r.measure (120);
    }
    {
        // 7: a new instance that has not played yet: nothing is playing
        Rig r;
        r.ed->setPage (3);
        r.silent();
        r.run (1.5);
        shots[6] = keep (r.picture(), "7-inside-idle.png", "the new mode in a new instance, nothing playing (GENERATE not pressed yet)");
    }
    // 0: all seven on one sheet
    {
        const int W = 1800, m = 12, label = 20, cw = (W - 4 * m) / 3, ch = cw * 760 / 1200, sh = (W - 2 * m) * 380 / 2400;
        juce::Image sheet (juce::Image::RGB, W, m + (label + ch) + m + (label + sh) + m + (label + ch) + m, true);
        juce::Graphics g (sheet);
        g.fillAll (look::ground);
        const char* names[] = { "1-inside-a", "2-inside-b", "3-inside-c", "4-inside-strip", "5-controls", "6-inside-large", "7-inside-idle" };
        auto place = [&] (int i, juce::Rectangle<int> cell)
        {
            g.setColour (look::inkDim);
            g.setFont (look::spaced (11.0f, 1, 0.1f));
            g.drawText (names[i], cell.removeFromTop (label), juce::Justification::centredLeft, false);
            const auto small = fitted (shots[i], cell.getWidth(), cell.getHeight());
            g.drawImageAt (small, cell.getX() + (cell.getWidth() - small.getWidth()) / 2, cell.getY() + (cell.getHeight() - small.getHeight()) / 2);
        };
        int y = m;
        for (int i = 0; i < 3; ++i) place (i, { m + i * (cw + m), y, cw, label + ch });
        y += label + ch + m;
        place (3, { m, y, W - 2 * m, label + sh });
        y += label + sh + m;
        for (int i = 0; i < 3; ++i) place (4 + i, { m + i * (cw + m), y, cw, label + ch });
        savePng (sheet, dir.getChildFile ("0-contact.png"));
    }
    dir.getChildFile ("shots.txt").replaceWithText ("Ghost Mosaic studio: the standard screenshot set (view: " + viewName + "). Built-in drums as the library, built-in sung vowels as the target.\n"
                                                    "0-contact.png         all seven on one sheet\n" + notes.joinIntoString ("\n") + "\n");
    // The paint cost against the rulebook's budget. The verdict is for 1200 x 760, the size the window opens at: mean 8 ms
    // or less, no frame over 16 ms. 2000 x 1400 is reported only, with a note when its mean is over 20 ms.
    const auto ps = statsOf (costSmall.paint), pl = statsOf (costLarge.paint), fs = statsOf (costSmall.frame), fl = statsOf (costLarge.frame);
    // The budget is Brandon's Mac's: mean 8 ms or less, no frame over 16 ms. Measured there on 2026-11-21 (build 21) the same
    // pictures cost 1.6 times (mean) to 1.9 times (slow frames) what this Linux stand-in reports. So off the Mac the stand-in's
    // own budget is the Mac's divided by that: mean 4.5 ms or less, and 95% of the frames under 8.5 ms (the stand-in's single
    // worst frame is too noisy on a shared machine to carry a verdict; it is still printed).
#if JUCE_MAC
    const double kMean = 8.0, kSlow = 16.0; const double slowS = ps.max; const juce::String kSlowWord = "no frame over 16";
#else
    const double kMean = 4.5, kSlow = 8.5; const double slowS = ps.p95; const juce::String kSlowWord = "95% under 8.5, the Mac's 8 / 16 scaled";
#endif
    const bool okS = ps.mean <= kMean && slowS <= kSlow, largeHeavy = pl.mean > 20.0;
    const juce::String largeNote = largeHeavy ? "over 20 ms: draw the heavy layers at a lower inner resolution when the window is huge" : "under 20 ms";
    const juce::String line = juce::String ("PAINT-COST ") + (okS ? "PASS" : "FAIL") + "  full repaint, mean / 95% / max ms:  1200x760 " + ms3 (ps) + " (budget " + juce::String (kMean, 1) + ", " + kSlowWord + ": " + (okS ? "ok" : "OVER") + ")"
                              + "  |  2000x1400 " + ms3 (pl) + " (reported only, " + largeNote + ")  |  frame(dt) " + ms3 (fs) + " and " + ms3 (fl);
    dir.getChildFile ("paint-cost.txt").replaceWithText (
        "Ghost Mosaic studio: paint cost of the new mode (view: " + viewName + ")\n"
        "A FULL repaint of the mode's page per frame, at retina scale 2, software renderer, while playing: " + juce::String ((int) costSmall.paint.size()) + " frames in a row\n"
        "(the quietest of " + juce::String (kCostRuns) + " such runs), this thread's CPU time (clock_gettime CLOCK_THREAD_CPUTIME_ID).\n"
        "The verdict is for 1200 x 760 (the size the window opens at). On Brandon's Mac: mean 8 ms or less, no frame over 16 ms. On the Linux stand-in,\n"
        "which measures 1.6 to 1.9 times faster than his Mac: mean 4.5 ms or less, 95% of the frames under 8.5 ms. 2000 x 1400 is reported only.\n\n"
        "window        full repaint ms (mean / 95% / max)    budget (mean / one frame)    frame(dt) ms (mean / 95% / max)    result\n"
        + juce::String ("1200 x 760    ") + ms3 (ps).paddedRight (' ', 38) + (juce::String (kMean, 1) + " / " + juce::String (kSlow, 1)).paddedRight (' ', 29) + ms3 (fs).paddedRight (' ', 35) + (okS ? "PASS" : "FAIL") + "\n"
        + juce::String ("2000 x 1400   ") + ms3 (pl).paddedRight (' ', 38) + juce::String ("reported only").paddedRight (' ', 29) + ms3 (fl).paddedRight (' ', 35) + largeNote + "\n\n"
        + "For scale: one flat coat of paint over every pixel of the page costs " + juce::String (costSmall.flat, 2) + " ms at 1200 x 760 and " + juce::String (costLarge.flat, 2)
        + " ms at 2000 x 1400 on this machine right now.\n\n"
        + line + "\n");
    std::printf ("%s\n", line.toRawUTF8());
    return okS ? 0 : 1;
}

// ---------------------------------------------------------------- the existing pages
// The second standard set, for whoever redraws the EXISTING pages: PLAY, TERRAIN and UNDER THE HOOD, each the whole
// editor window exactly as a user sees it at that size (a page that scrolls is shown scrolled to the top). The same
// test sounds and the same fixed seed as the mode's set, playing all the way through.
static int pagesSet (const juce::File& dir)
{
    dir.createDirectory();
    const char* names[9] = { "p1-play", "p2-terrain", "p3-hood", "p4-hood-synaptic", "p5-hood-net", "p6-play-large", "p7-hood-large", "p8-play-small", "p9-hood-small" };
    juce::Image shots[9];
    {
        Rig r;
        auto& ed = *r.ed;
        auto steps = [&] (int n) { for (int i = 0; i < n; ++i) r.slowFrame(); };
        auto snap = [&] (int i)
        {
            if (auto* vp = ed.hood().findParentComponentOfClass<juce::Viewport>()) vp->setViewPosition (0, 0);
            shots[i] = r.picture();
            savePng (shots[i], dir.getChildFile (juce::String (names[i]) + ".png"));
        };
        auto click = [&] (const char* id) { if (auto* b = dynamic_cast<juce::Button*> (ed.findById (id))) b->triggerClick(); };
        r.generate();                                   // GENERATE: it plays from the top, and keeps playing
        ed.setPage (0); steps (48); snap (0);           // p1: PLAY, 2 s in
        ed.setPage (1); steps (6);                      // p2: TERRAIN (its opening move finished)
        ed.terrainPage().view().settle(); steps (6); snap (1);
        ed.setPage (2); steps (8); snap (2);            // p3: UNDER THE HOOD, search tab, default settings
        r.set ("algo", 3); r.set ("novelty", 0.5f);     // p4: algorithm synaptic slide, novelty 0.50
        steps (12); snap (3);
        r.set ("algo", 0); r.set ("novelty", 0.0f);     // p5: the net tab (defaults again)
        click ("hoodTabNet"); steps (8); snap (4);
        click ("hoodTabSearch"); steps (4);
        ed.setSize (2000, 1400);                        // p6, p7: the largest window
        ed.setPage (0); steps (24); snap (5);
        ed.setPage (2); steps (8); snap (6);
        ed.setSize (1000, 700);                         // p8, p9: the smallest
        ed.setPage (0); steps (24); snap (7);
        ed.setPage (2); steps (8); snap (8);
    }
    // p0: all nine on one sheet
    const int W = 1800, m = 12, label = 20, cw = (W - 4 * m) / 3, ch = cw * 760 / 1200;
    juce::Image sheet (juce::Image::RGB, W, m + 3 * (label + ch + m), true);
    {
        juce::Graphics g (sheet);
        g.fillAll (look::ground);
        for (int i = 0; i < 9; ++i)
        {
            juce::Rectangle<int> cell (m + (i % 3) * (cw + m), m + (i / 3) * (label + ch + m), cw, label + ch);
            g.setColour (look::inkDim);
            g.setFont (look::spaced (11.0f, 1, 0.1f));
            g.drawText (juce::String (names[i]) + "   " + juce::String (shots[i].getWidth()) + " x " + juce::String (shots[i].getHeight()), cell.removeFromTop (label), juce::Justification::centredLeft, false);
            const auto small = fitted (shots[i], cell.getWidth(), cell.getHeight());
            g.drawImageAt (small, cell.getX() + (cell.getWidth() - small.getWidth()) / 2, cell.getY() + (cell.getHeight() - small.getHeight()) / 2);
        }
    }
    savePng (sheet, dir.getChildFile ("p0-contact.png"));
    std::printf ("PAGES p0-contact.png p1-play.png p2-terrain.png p3-hood.png p4-hood-synaptic.png p5-hood-net.png p6-play-large.png p7-hood-large.png p8-play-small.png p9-hood-small.png\n");
    return 0;
}

// ---------------------------------------------------------------- the smallest window
// Not part of the judged set: the new mode, PLAY and UNDER THE HOOD at 1000 x 700, to look at the header and the grid there.
static int smallWindow (const juce::File& dir)
{
    dir.createDirectory();
    Rig r;
    r.ed->setSize (1000, 700);
    r.ed->setPage (3);
    r.generate();
    r.run (2.0);
    savePng (r.picture(), dir.getChildFile ("small-inside.png"));
    r.ed->setPage (0);
    r.run (0.5);
    savePng (r.picture(), dir.getChildFile ("small-play.png"));
    r.ed->setPage (2);
    r.run (0.5);
    savePng (r.picture(), dir.getChildFile ("small-controls.png"));
    std::printf ("small-inside.png small-play.png small-controls.png in %s\n", dir.getFullPathName().toRawUTF8());
    return 0;
}

// ---------------------------------------------------------------- the studio's own checks
static juce::Rectangle<int> bodyOf (Editor& ed) { return { 24, 110, ed.getWidth() - 48, ed.getHeight() - 110 - 94 }; }

static int inkPixels (juce::Component& c)
{
    const auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f).convertedToFormat (juce::Image::ARGB);
    juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
    int n = 0;
    for (int y = 0; y < img.getHeight(); ++y) for (int x = 0; x < img.getWidth(); ++x) n += bd.getPixelColour (x, y).getAlpha() > 128 ? 1 : 0;
    return n;
}

static int selftest()
{
    const int sizes[3][2] = { { 1000, 700 }, { 1200, 760 }, { 2000, 1400 } };
    {
        Processor p; p.prepareToPlay (kRate, kBuffer);
        Editor ed (p);
        ed.setTestClock (true);
        auto* tab = dynamic_cast<juce::Button*> (ed.findById ("tabScope"));
        juce::Button* tabs[4] = { btn (ed, "tabPlay"), btn (ed, "tabTerrain"), btn (ed, "tabHood"), tab };
        check (tab != nullptr && tabs[0] && tabs[1] && tabs[2] && tab->getButtonText() == ed.scopePage().buttonName() && tab->getButtonText().isNotEmpty() && tab->getTooltip().isNotEmpty(),
               "the new mode has its own header button beside PLAY, TERRAIN and UNDER THE HOOD, named by the view", tab != nullptr ? "\"" + tab->getButtonText() + "\"" : juce::String ("missing"));
        if (tab == nullptr) return gFails;
        // ---- the header and the page at the three window sizes
        for (auto& sz : sizes)
        {
            const juce::String at = juce::String (sz[0]) + " x " + juce::String (sz[1]);
            ed.setSize (sz[0], sz[1]);
            ed.setPage (3);
            drain();
            bool line = true, order = true, fits = true; juce::String why;
            const auto font = look::spaced (13.0f, 1, 0.14f);
            for (int i = 0; i < 4; ++i)
            {
                const auto b = tabs[i]->getBounds();
                line = line && tabs[i]->isVisible() && b.getY() == tabs[0]->getY() && b.getHeight() == tabs[0]->getHeight() && b.getY() >= 0 && b.getBottom() <= 96;
                if (i > 0) order = order && b.getX() - tabs[i - 1]->getRight() == tabs[1]->getX() - tabs[0]->getRight() && b.getX() > tabs[i - 1]->getRight();
                const int need = (int) std::ceil (juce::GlyphArrangement::getStringWidth (font, tabs[i]->getButtonText())) + 12;
                if (need > b.getWidth()) { fits = false; why << tabs[i]->getButtonText() << " needs " << need << " px, has " << b.getWidth() << "; "; }
            }
            const auto status = ed.statusArea();
            check (line && order && tab->getRight() == sz[0] - 24 && status.getRight() <= tabs[0]->getX() && status.getX() >= ed.wordmark().getRight(),
                   "header at " + at + ": the four page buttons sit on one line inside the header, equal gaps, the new one last, right edge on the margin",
                   "buttons " + juce::String (tabs[0]->getX()) + ".." + juce::String (tab->getRight()) + ", status " + juce::String (status.getWidth()) + " px");
            check (fits, "header at " + at + ": every page button is wide enough for its words", why);
            check (status.getWidth() >= 250, "header at " + at + ": the status keeps its room (250 px or more)", juce::String (status.getWidth()) + " px");
            auto& page = ed.scopePage();
            const auto body = bodyOf (ed);
            check (page.isVisible() && page.getBounds() == body && page.view().getBounds() == page.getLocalBounds() && page.view().isVisible()
                       && body.getX() == sz[0] - body.getRight() && body.getY() - 96 == (sz[1] - 80) - body.getBottom(),
                   "page at " + at + ": the view fills the whole body between header and bottom bar, equal margins",
                   page.getBounds().toString() + " of " + body.toString());
            bool bar = true;
            for (auto* id : { "undo", "redo", "ab", "presets", "generate", "mutate", "dragWav", "session" })
            {
                auto* c = ed.findById (id);
                bar = bar && c != nullptr && c->isVisible() && ed.getLocalBounds().contains (c->getBounds()) && c->getY() >= body.getBottom();
            }
            check (bar, "page at " + at + ": the bottom bar (UNDO ... GENERATE ... SESSION) stays on screen in the new mode");
        }
        // ---- the pages
        ed.setSize (1200, 760);
        ed.setPage (3);
        const bool in = ed.page() == 3 && ed.scopePage().isVisible() && tab->getToggleState() && ! ed.hoodShown() && ! ed.sources().isVisible() && ! ed.terrainPage().isVisible() && ed.scopePage().onScreen();
        ed.setPage (2);
        const bool hood = ed.page() == 2 && ed.hoodShown() && ! ed.scopePage().isVisible() && ! tab->getToggleState() && ! ed.scopePage().onScreen();
        tab->triggerClick(); drain(); pump (30);
        const bool clicked = ed.page() == 3 && ed.scopePage().isVisible();
        ed.setPage (0);
        const bool back = ed.page() == 0 && ed.sources().isVisible() && ! ed.scopePage().isVisible() && ! ed.scopePage().onScreen();
        ed.setPage (9);
        const bool clamped = ed.page() == 3;
        ed.setPage (0);
        check (in && hood && clicked && back && clamped, "setPage (3) shows the new mode, the button opens it too, and every other page comes back as it was",
               juce::String (in ? "" : "in ") + (hood ? "" : "hood ") + (clicked ? "" : "click ") + (back ? "" : "back ") + (clamped ? "" : "clamp"));
        // ---- frames by hand: exactly one per step, and none on another page
        auto& page = ed.scopePage();
        const int f0 = page.framesRun();
        ed.stepForTests (kDt); ed.stepForTests (kDt);
        const int offPage = page.framesRun() - f0;
        ed.setPage (3);
        pump (60);
        const int idle = page.framesRun() - f0;
        ed.stepForTests (kDt); ed.stepForTests (kDt); ed.stepForTests (kDt);
        const int stepped = page.framesRun() - f0;
        check (offPage == 0 && idle == 0 && stepped == 3, "test clock: stepForTests gives the view exactly one frame, only while the new mode shows, and the wall clock gives none",
               juce::String (offPage) + " on another page, " + juce::String (idle) + " while waiting, " + juce::String (stepped) + " after 3 steps");
        // ---- the real 60 Hz timer: frames run while the mode shows and stop completely when it does not
        ed.setTestClock (false);
        const int t0 = page.framesRun();
        pump (400);
        const int running = page.framesRun() - t0;
        ed.setPage (1);
        const int t1 = page.framesRun();
        pump (300);
        const int afterLeaving = page.framesRun() - t1;
        ed.setPage (3); pump (100);
        check (running >= 4 && afterLeaving == 0 && page.framesRun() > t1, "the editor's 60 Hz timer drives the view while the new mode shows; on another page no frame runs at all",
               juce::String (running) + " frames in 0.4 s showing, " + juce::String (afterLeaving) + " in 0.3 s on TERRAIN");
        // ---- the wordmark stays drawn when the window is resized (it used to start growing in again from nothing)
        ed.setPage (0);
        ed.setSize (1200, 760);
        pump (1300);                                   // (it grows in once, over 0.9 s, when it first appears)
        const int before = inkPixels (ed.wordmark());
        ed.setSize (1180, 1400);                       // (the capture that showed it: mosaic-under-the-hood.png)
        pump (80);
        const int after = inkPixels (ed.wordmark());
        ed.setSize (1000, 700);
        pump (80);
        const int afterSmall = inkPixels (ed.wordmark());
        check (before > 1500 && after > before * 3 / 4 && afterSmall > before / 2 && ed.wordmark().grown() >= 1.0f,
               "the GHOST MOSAIC wordmark stays drawn when the window is resized",
               juce::String (before) + " px of ink before, " + juce::String (after) + " and " + juce::String (afterSmall) + " px 80 ms after a resize");
    }
    {
        // ---- an existing control placed by a view is the same control as on UNDER THE HOOD
        Processor p; p.prepareToPlay (kRate, kBuffer);
        juce::AudioBuffer<float> io (2, kBuffer); juce::MidiBuffer midi;
        auto audio = [&] { for (int i = 0; i < 3; ++i) { io.clear(); p.processBlock (io, midi); } };
        look::LookAndFeel lnf;
        auto nov = ParamRow::make (p, "novelty"), syn = ParamRow::make (p, "synapses"), slide = ParamRow::make (p, "slide_error"), sticky = ParamRow::make (p, "stickyness"), bore = ParamRow::make (p, "boredom");
        const bool made = nov && syn && slide && sticky && bore && nov->slider && syn->slider && ParamRow::make (p, "no_such_setting") == nullptr;
        check (made && nov->mainText == "novelty" && nov->subText == "NOVELTY" && syn->mainText == "num synpases" && syn->subText == "CONNECTIONS",
               "an existing control placed by a view carries the original's lowercase name and our plain-words line");
        if (made)
        {
            for (auto* r : { nov.get(), syn.get(), slide.get(), sticky.get(), bore.get() }) { r->setLookAndFeel (&lnf); r->setSize (300, ParamRow::kHeight); }
            nov->slider->setValue (0.37, juce::sendNotificationSync); drain();
            const bool toParam = std::abs (p.getParam ("novelty") - 0.37f) < 0.006f;
            p.setParam ("novelty", 0.12f); drain(); pump (20);
            const bool fromParam = std::abs (nov->slider->getValue() - 0.12) < 0.006;
            p.undo().undo(); drain(); pump (20);
            const bool undone = std::abs (p.getParam ("novelty") - 0.37f) < 0.006f && std::abs (nov->slider->getValue() - 0.37) < 0.006;
            check (toParam && fromParam && undone, "...and is the same parameter: the control sets it, follows it, and UNDO takes a change back",
                   juce::String (toParam ? "" : "control>setting ") + (fromParam ? "" : "setting>control ") + (undone ? "" : "undo"));
            // rule 3: greyed while the setting does nothing, with the reason; alive again when it does something
            auto sync = [&] { audio(); for (auto* r : { nov.get(), syn.get(), slide.get(), sticky.get(), bore.get() }) r->sync (p.engine()); };
            setP (p, "algo", 0); setP (p, "novelty", 0); sync();
            const bool basic = syn->idle() && slide->idle() && ! sticky->idle() && bore->idle() && ! nov->idle() && ! syn->slider->isEnabled() && syn->getTooltip().startsWith ("Does nothing right now");
            setP (p, "algo", 1); setP (p, "novelty", 0.3f); sync();
            const bool reversed = syn->idle() && slide->idle() && sticky->idle() && ! bore->idle();
            setP (p, "algo", 2); sync();
            const bool synaptic = ! syn->idle() && slide->idle() && ! sticky->idle() && syn->slider->isEnabled() && ! syn->getTooltip().startsWith ("Does nothing");
            setP (p, "algo", 3); sync();
            const bool slideOn = ! syn->idle() && ! slide->idle() && ! sticky->idle();
            check (basic && reversed && synaptic && slideOn, "...and greys itself, with the reason in its tooltip, while its setting does nothing (the engine map's table)",
                   juce::String (basic ? "" : "basic ") + (reversed ? "" : "reversed ") + (synaptic ? "" : "synaptic ") + (slideOn ? "" : "slide"));
            for (auto* r : { nov.get(), syn.get(), slide.get(), sticky.get(), bore.get() }) r->setLookAndFeel (nullptr);
        }
    }
    {
        // ---- nothing moves while the engine is silent, and the picture is repeatable (the same steps twice, from scratch)
        juce::Image still[2][2], playing[2];
        for (int pass = 0; pass < 2; ++pass)
        {
            Rig r;
            r.ed->setPage (3);
            r.silent();
            r.run (1.0); still[pass][0] = r.pagePicture();
            r.run (1.0); still[pass][1] = r.pagePicture();
            r.generate();
            r.run (1.0); playing[pass] = r.pagePicture();
        }
        check (samePixels (still[0][0], still[0][1]) && samePixels (still[1][0], still[1][1]),
               "nothing in the view moves while the engine is silent (a new instance, not playing: the same picture a second later)");
        check (samePixels (playing[0], playing[1]) && samePixels (still[0][1], still[1][1]),
               "the view is repeatable: the same steps twice (2 s silent, GENERATE, 1 s playing) give the same pictures, pixel for pixel");
        check (! samePixels (playing[0], still[0][1]), "the view shows something else once the engine plays");
    }
    std::printf ("\n%d passed, %d failed\n", gPasses, gFails);
    return gFails;
}
}

static int runStudio (const juce::StringArray& args)
{
    if (args[1] == "selftest") return studioh::selftest();
    if (args[1] == "small" && args[2].isNotEmpty()) return studioh::smallWindow (juce::File::getCurrentWorkingDirectory().getChildFile (args[2]));
    if (args[1] == "pages" && args[2].isNotEmpty()) return studioh::pagesSet (juce::File::getCurrentWorkingDirectory().getChildFile (args[2]));
    if (args[1].isEmpty())
    {
        std::printf ("usage: studio <output folder>   the standard screenshot set and the paint cost of the new mode\n       studio selftest          the studio's own checks\n"
                     "       studio small <folder>    extra pictures at the smallest window (1000 x 700)\n"
                     "       studio pages <folder>    the standard set of the existing pages (PLAY, TERRAIN, UNDER THE HOOD)\n");
        return 2;
    }
    return studioh::standardSet (juce::File::getCurrentWorkingDirectory().getChildFile (args[1]));
}
