// ArpEZatorTests: the professor's held prop per mood (ArpArt.h drawProfessor). --props <shot dir> [baseline dir] runs it alone.
//   SUNRISE / DUSK / MIDNIGHT keep the flask pixel-identically (vs a baseline dumped from main: --props-dump <dir>); TRAP holds the
//   double foam cup, HIP HOP the 40 in a paper bag: present, outlined >= 3:1 against what is behind them, animated, paths built once;
//   the header art is the same in all four looks (it paints its own sky); screenshots + a contact sheet.
namespace proptest {
static const char* lookNames[4] { "dark", "purple", "light", "ghost" };
static const double times[3] { 0.0, 1.37, 2.9 };
static void png (const juce::Image& img, const juce::File& f) { f.deleteFile(); juce::FileOutputStream o (f); if (o.openedOk()) { juce::PNGImageFormat p; p.writeImageToStream (img, o); } }
static juce::Image solo (int mode, double t, int size, bool withProp = true)
{
    juce::Image img (juce::Image::ARGB, size, size, true);
    juce::Graphics g (img);
    arpui::drawProfessor (g, { 0, 0, (float) size, (float) size }, t, mode, 1.0f, withProp);
    return img;
}
static bool same (const juce::Image& a, const juce::Image& b)
{
    if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight()) return false;
    const juce::Image::BitmapData A (a, juce::Image::BitmapData::readOnly), B (b, juce::Image::BitmapData::readOnly);
    for (int y = 0; y < a.getHeight(); ++y) for (int x = 0; x < a.getWidth(); ++x) if (A.getPixelColour (x, y) != B.getPixelColour (x, y)) return false;
    return true;
}
// every render the baseline compares: solo art (3 times x 2 sizes) + the editor header in each look (1x + 3x), per mood
struct Shots { std::vector<std::pair<juce::String, juce::Image>> list; };
static Shots render (int mode)
{
    Shots S;
    for (int k = 0; k < 3; ++k) for (int size : { 62, 186 }) S.list.push_back ({ "solo-m" + juce::String (mode) + "-t" + juce::String (k) + "-" + juce::String (size), solo (mode, times[k], size) });
    ArpProcessor p; p.persistTaste = false; p.theme = "dark"; p.hintsSeen = true;
    p.change ([mode] (Idea& X) { X.mode = (decltype (X.mode)) mode; X.seed = 51; applyModeDefaults (X); newIdea (X); for (auto& L : X.lanes) L.mute = false; }, false);
    std::unique_ptr<ArpEditor> ed (dynamic_cast<ArpEditor*> (p.createEditor())); ed->setSize (1240, 820);
    for (auto* look : lookNames)
    {
        p.theme = look; ed->applyThemeNow (mode); ed->selectLane (LaneMelody); ed->refresh();
        for (float sc : { 1.0f, 3.0f })
            S.list.push_back ({ "hdr-" + juce::String (look) + "-m" + juce::String (mode) + "-" + juce::String ((int) sc) + "x", ed->createComponentSnapshot ({ 8, 0, 70, 70 }, true, sc) });
    }
    return S;
}
static void dump (const juce::File& dir)
{
    dir.createDirectory();
    for (int m = 0; m < kMoods; ++m) for (auto& s : render (m).list) png (s.second, dir.getChildFile (s.first + ".png"));
}
// the prop's outline against what is behind it: the prop = pixels that change when it is drawn; along its edge (the 2nd pixel ring in),
// the best contrast between the backdrop there and any prop pixel within 3 px. Returns the share of edge pixels >= 3:1 (+ the count).
static double outlineShare (const juce::Image& A, const juce::Image& B, int& edgeOut, int& changedOut)
{
    const int w = A.getWidth(), h = A.getHeight();
    const juce::Image::BitmapData a (A, juce::Image::BitmapData::readOnly), b (B, juce::Image::BitmapData::readOnly);
    std::vector<uint8_t> M ((size_t) (w * h), 0);
    auto at = [&] (const std::vector<uint8_t>& v, int x, int y) { return x >= 0 && y >= 0 && x < w && y < h && v[(size_t) (y * w + x)]; };
    int changed = 0;
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
    {
        const auto ca = a.getPixelColour (x, y), cb = b.getPixelColour (x, y);
        const int d = std::abs (ca.getRed() - cb.getRed()) + std::abs (ca.getGreen() - cb.getGreen()) + std::abs (ca.getBlue() - cb.getBlue());
        if (d > 24) { M[(size_t) (y * w + x)] = 1; ++changed; }
    }
    auto erode = [&] (const std::vector<uint8_t>& v) { std::vector<uint8_t> o (v.size(), 0);
        for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x) o[(size_t) (y * w + x)] = (uint8_t) (at (v, x, y) && at (v, x - 1, y) && at (v, x + 1, y) && at (v, x, y - 1) && at (v, x, y + 1)); return o; };
    const auto M1 = erode (M), M2 = erode (M1);
    int edge = 0, good = 0;
    for (int y = 0; y < h; ++y) for (int x = 0; x < w; ++x)
    {
        if (! at (M1, x, y) || at (M2, x, y)) continue;
        ++edge; const auto bg = b.getPixelColour (x, y).withAlpha (1.0f); double best = 0;
        for (int dy = -3; dy <= 3; ++dy) for (int dx = -3; dx <= 3; ++dx) if (at (M, x + dx, y + dy)) best = std::max (best, ghosty::themes::contrast (a.getPixelColour (x + dx, y + dy).withAlpha (1.0f), bg));
        if (best >= 3.0) ++good;
    }
    edgeOut = edge; changedOut = changed;
    return edge ? (double) good / edge : 0.0;
}
static juce::Image loadPng (const juce::File& f) { return f.existsAsFile() ? juce::ImageFileFormat::loadFrom (f) : juce::Image(); }
static juce::Image viaPng (const juce::Image& img)
{   // the same PNG round trip the baseline went through (semi-transparent edge pixels are stored un-premultiplied)
    juce::MemoryOutputStream o; juce::PNGImageFormat p; p.writeImageToStream (img, o);
    return juce::ImageFileFormat::loadFrom (o.getData(), o.getDataSize());
}
static juce::Image sheet (const std::map<juce::String, juce::Image>& hdr)
{   // rows = the four looks; columns: MIDNIGHT flask 3x (unchanged, for reference), TRAP 1x + 3x, HIP HOP 1x + 3x
    const int pad = 14, lab = 22, colW[5] { 210, 70, 210, 70, 210 };
    int W = pad; for (int c : colW) W += c + pad;
    const int H = lab + pad + 4 * (210 + lab + pad);
    juce::Image img (juce::Image::ARGB, W, H, true); juce::Graphics g (img);
    g.fillAll (juce::Colour (0xff202028)); g.setColour (juce::Colours::white); g.setFont (juce::FontOptions (14.0f));
    const char* heads[5] { "MIDNIGHT 3x (flask)", "TRAP 1x", "TRAP 3x", "HIP HOP 1x", "HIP HOP 3x" };
    const int mode[5] { 2, 3, 3, 4, 4 }, scale[5] { 3, 1, 3, 1, 3 };
    for (int r = 0; r < 4; ++r)
    {
        const int y0 = lab + pad + r * (210 + lab + pad); int x = pad;
        g.setColour (juce::Colours::white); g.drawText (juce::String (lookNames[r]).toUpperCase() + " look", pad, y0, 300, lab, juce::Justification::centredLeft);
        for (int c = 0; c < 5; ++c)
        {
            if (r == 0) g.drawText (heads[c], x, 4, colW[c] + pad, lab, juce::Justification::centredLeft);
            auto it = hdr.find ("hdr-" + juce::String (lookNames[r]) + "-m" + juce::String (mode[c]) + "-" + juce::String (scale[c]) + "x");
            if (it != hdr.end()) g.drawImageAt (it->second, x, y0 + lab);
            x += colW[c] + pad;
        }
    }
    return img;
}
} // namespace proptest

void propChecks (Counter& C, const juce::String& shotDir, const juce::String& baselineDir = {})
{
    using namespace proptest;
    std::printf ("== the professor's prop per mood\n");
    std::map<juce::String, juce::Image> all;
    for (int m = 0; m < kMoods; ++m) for (auto& s : render (m).list) all[s.first] = s.second;
    {   // SUNRISE / DUSK / MIDNIGHT: the flask exactly as before
        bool flagSame = true;
        for (int m = 0; m < 3; ++m) for (double t : times) for (int sz : { 62, 186 }) flagSame = flagSame && same (solo (m, t, sz, true), solo (m, t, sz, false));
        C.ok (flagSame, "PROPS: SUNRISE / DUSK / MIDNIGHT ignore the prop switch (the flask path is untouched)");
        if (baselineDir.isNotEmpty())
        {
            int n = 0, bad = 0; juce::String first;
            for (auto& kv : all)
            {
                if (! (kv.first.contains ("-m0") || kv.first.contains ("-m1") || kv.first.contains ("-m2"))) continue;
                ++n; const auto base = loadPng (juce::File (baselineDir).getChildFile (kv.first + ".png"));
                if (! base.isValid() || ! same (base.convertedToFormat (juce::Image::ARGB), viaPng (kv.second).convertedToFormat (juce::Image::ARGB))) { ++bad; if (first.isEmpty()) first = kv.first; }
            }
            C.ok (n == 42 && bad == 0, ("PROPS: SUNRISE / DUSK / MIDNIGHT pixel-identical to the baseline (" + juce::String (n - bad) + "/" + juce::String (n) + " renders: solo art x3 times x2 sizes, header in BLACK / PURPLE / LIGHT / GHOST at 1x + 3x)" + (bad ? " first diff " + first : juce::String())).toStdString());
        }
    }
    {   // the header art is the same in every look (it paints its own sky): the inside of the 62 px box, 1x + 3x
        bool ok = true;
        for (int m = 0; m < kMoods; ++m) for (int sc : { 1, 3 })
        {
            const auto ref = all["hdr-dark-m" + juce::String (m) + "-" + juce::String (sc) + "x"].getClippedImage ({ 10 * sc, 10 * sc, 50 * sc, 50 * sc });
            for (int l = 1; l < 4; ++l) ok = ok && same (ref, all["hdr-" + juce::String (lookNames[l]) + "-m" + juce::String (m) + "-" + juce::String (sc) + "x"].getClippedImage ({ 10 * sc, 10 * sc, 50 * sc, 50 * sc }));
        }
        C.ok (ok, "PROPS: the professor (flask, double cup, 40) renders the same in BLACK / PURPLE / LIGHT / GHOST");
    }
    for (int m : { (int) Trap, (int) HipHop })
    {
        const char* nm = m == Trap ? "TRAP double cup" : "HIP HOP 40 in a bag";
        double worst = 1; int minChanged = 1 << 30, minEdge = 1 << 30;
        for (double t : times) for (int sz : { 124, 186 })
        {
            int edge = 0, changed = 0; const double sh = outlineShare (solo (m, t, sz, true), solo (m, t, sz, false), edge, changed);
            worst = std::min (worst, sh); minChanged = std::min (minChanged, changed); minEdge = std::min (minEdge, edge);
        }
        C.ok (minChanged > 300 && worst >= 0.95, (juce::String ("PROPS: ") + nm + " drawn (>= " + juce::String (minChanged) + " px at 2x/3x) with its outline >= 3:1 against the sky / arm behind it on "
                                                 + juce::String (100 * worst, 1) + "% of " + juce::String (minEdge) + "+ edge px (worst of 3 frames x 2 sizes)").toStdString());
        const bool differsFromFlask = ! same (solo (m, 1.37, 186), solo (Midnight, 1.37, 186).getClippedImage ({ 0, 0, 186, 186 }));
        int e = 0, ch = 0; (void) outlineShare (solo (m, 0.0, 186), solo (m, 1.37, 186), e, ch);   // the prop moves (slosh / tilt) between frames
        C.ok (differsFromFlask && ch > 40, (juce::String ("PROPS: ") + nm + " replaces the flask and animates (" + juce::String (ch) + " px change between frames)").toStdString());
    }
    {   // no allocation per frame in the props: their paths are built once, ever
        const int b0 = arpui::propPathBuilds();
        juce::Image img (juce::Image::ARGB, 62, 62, true); juce::Graphics g (img);
        for (int i = 0; i < 120; ++i) arpui::drawProfessor (g, { 0, 0, 62, 62 }, i / 60.0, i % 2 ? Trap : HipHop);
        C.ok (arpui::propPathBuilds() == 1 && b0 == 1, ("PROPS: 120 animated frames reuse the props' paths (built " + juce::String (arpui::propPathBuilds()) + "x, pre-stroked; frames only transform them)").toStdString());
    }
    if (shotDir.isNotEmpty())
    {
        const juce::File d (shotDir); d.createDirectory();
        for (auto& kv : all) if (kv.first.startsWith ("hdr-") && (kv.first.contains ("-m3-") || kv.first.contains ("-m4-"))) png (kv.second, d.getChildFile ("prof-" + kv.first.fromFirstOccurrenceOf ("hdr-", false, false).replace ("-m3-", "-trap-").replace ("-m4-", "-hiphop-") + ".png"));
        png (sheet (all), d.getChildFile ("props-contact-sheet.png"));
        for (int m : { (int) Trap, (int) HipHop }) png (solo (m, 1.37, 600), d.getChildFile (juce::String ("prof-") + (m == Trap ? "trap" : "hiphop") + "-closeup-600.png"));
        std::printf ("  shots + contact sheet -> %s\n", d.getChildFile ("props-contact-sheet.png").getFullPathName().toRawUTF8());
    }
}
