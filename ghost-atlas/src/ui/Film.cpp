// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Film.h"

namespace atlas::ui
{
// ============================================================================================ one world's run of frames
juce::File Film::folder()
{
    if (const char* e = std::getenv ("GHOST_ATLAS_FILM")) if (*e != 0) return juce::File (juce::String::fromUTF8 (e));
    return atlasRoot().getChildFile ("Film");
}
bool Film::exists (const juce::String& worldKey) { return worldKey.isNotEmpty() && folder().getChildFile (worldKey + ".gafilm").existsAsFile(); }

bool Film::open (const juce::String& worldKey)
{
    key.clear(); where.clear(); kept.clear(); data.reset(); w = h = 0;
    const auto file = folder().getChildFile (worldKey + ".gafilm");
    if (! file.existsAsFile() || ! file.loadFileAsData (data) || data.getSize() < 32) return false;
    const auto* bytes = (const char*) data.getData();
    const size_t size = data.getSize();
    if (std::memcmp (bytes, "GAFILM1\n", 8) != 0) return false;
    size_t at = 8, eol = at;
    while (eol < size && bytes[eol] != '\n') ++eol;
    if (eol >= size) return false;
    const auto head = juce::JSON::parse (juce::String::fromUTF8 (bytes + at, (int) (eol - at)));
    at = eol + 1;
    w = (int) prop (head, "width", 0); h = (int) prop (head, "height", 0);
    secondsOnScreen = juce::jlimit (0.08f, 0.8f, (float) (double) prop (head, "pace", 0.2));
    while (at + 4 <= size)
    {
        const auto* p = (const juce::uint8*) bytes + at;
        const size_t len = (size_t) p[0] | ((size_t) p[1] << 8) | ((size_t) p[2] << 16) | ((size_t) p[3] << 24);
        at += 4;
        if (len == 0 || at + len > size) break;
        where.emplace_back (at, len);
        at += len;
    }
    if (where.size() < 2 || w < 16 || h < 16) { where.clear(); return false; }
    key = worldKey;
    return true;
}

std::shared_ptr<const Film::Plain> Film::frame (int index)
{
    if (index < 0 || index >= frames()) return nullptr;
    for (auto& k : kept) if (k.first == index) return k.second;
    const auto& at = where[(size_t) index];
    const juce::Image img = juce::ImageFileFormat::loadFrom ((const char*) data.getData() + at.first, at.second);
    if (! img.isValid()) return nullptr;
    auto p = std::make_shared<Plain>();
    p->w = img.getWidth(); p->h = img.getHeight();
    p->rgb.resize ((size_t) p->w * (size_t) p->h * 3);
    {
        // (a picture's bytes are laid out differently from one machine and one kind of picture to another: they are read
        //  through JUCE's own pixel types, once, and everything after this works on plain red, green, blue)
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < p->h; ++y)
        {
            const juce::uint8* src = bd.getLinePointer (y);
            juce::uint8* dst = p->rgb.data() + (size_t) y * (size_t) p->w * 3;
            if (bd.pixelFormat == juce::Image::RGB)
                for (int x = 0; x < p->w; ++x, dst += 3) { const auto* px = (const juce::PixelRGB*) (src + x * bd.pixelStride); dst[0] = px->getRed(); dst[1] = px->getGreen(); dst[2] = px->getBlue(); }
            else if (bd.pixelFormat == juce::Image::ARGB)
                for (int x = 0; x < p->w; ++x, dst += 3) { const auto* px = (const juce::PixelARGB*) (src + x * bd.pixelStride); dst[0] = px->getRed(); dst[1] = px->getGreen(); dst[2] = px->getBlue(); }
            else
                for (int x = 0; x < p->w; ++x, dst += 3) { const auto v = src[x * bd.pixelStride]; dst[0] = dst[1] = dst[2] = v; }
        }
    }
    if (kept.size() >= 4) kept.erase (kept.begin());
    kept.emplace_back (index, p);
    return p;
}

// ============================================================================================ playing it
FilmPlayer::FilmPlayer() : juce::Thread ("Ghost Atlas film") {}
FilmPlayer::~FilmPlayer()
{
    cancelPendingUpdate();
    signalThreadShouldExit(); notify(); stopThread (4000);
}

void FilmPlayer::ask (const Ask& a)
{
    {
        const juce::ScopedLock sl (lock);
        if (want == a) return;
        want = a;
    }
    if (a.running && ! isThreadRunning()) startThread (juce::Thread::Priority::low);
    notify();
}

bool FilmPlayer::latest (const juce::String& world, Shown& out)
{
    const juce::ScopedLock sl (lock);
    if (frontWorld != world || front.image.isNull()) return false;
    out = front;
    return true;
}

// one picture of the window's own size: two neighbouring frames blended for the moment between them, then brought to size
// with the painting's own framing (each row is first made from the two rows it lies between, then stretched along)
void FilmPlayer::render (const Film::Plain& a, const Film::Plain* b, int blend256, const Ask& how, juce::Image& into)
{
    const juce::uint8* src = a.rgb.data();
    if (b != nullptr && blend256 > 0 && b->w == a.w && b->h == a.h)
    {
        mixed.resize (a.rgb.size());
        const juce::uint8* pa = a.rgb.data(); const juce::uint8* pb = b->rgb.data(); juce::uint8* pm = mixed.data();
        const int wb = juce::jlimit (0, 256, blend256), wa = 256 - wb;
        for (size_t i = 0, n = a.rgb.size(); i < n; ++i) pm[i] = (juce::uint8) ((pa[i] * wa + pb[i] * wb) >> 8);
        src = mixed.data();
    }
    const int W = into.getWidth(), H = into.getHeight(), sw = a.w, sh = a.h;
    const double du = (double) sw / (double) juce::jmax (1.0f, how.width), dv = (double) sh / (double) juce::jmax (1.0f, how.height);
    std::vector<int> x0 ((size_t) W), x1 ((size_t) W), fx ((size_t) W);
    for (int X = 0; X < W; ++X)
    {
        const double u = juce::jlimit (0.0, (double) sw - 1.0, ((double) X + 0.5 - (double) how.left) * du - 0.5);
        const int i = (int) u;
        x0[(size_t) X] = i * 3; x1[(size_t) X] = juce::jmin (sw - 1, i + 1) * 3; fx[(size_t) X] = (int) ((u - (double) i) * 256.0);
    }
    std::vector<juce::uint16> row ((size_t) sw * 3);
    juce::Image::BitmapData bd (into, juce::Image::BitmapData::writeOnly);
    for (int Y = 0; Y < H; ++Y)
    {
        const double v = juce::jlimit (0.0, (double) sh - 1.0, ((double) Y + 0.5 - (double) how.top) * dv - 0.5);
        const int j = (int) v, fy = (int) ((v - (double) j) * 256.0);
        const juce::uint8* r0 = src + (size_t) j * (size_t) sw * 3;
        const juce::uint8* r1 = src + (size_t) juce::jmin (sh - 1, j + 1) * (size_t) sw * 3;
        for (int k = 0, n = sw * 3; k < n; ++k) row[(size_t) k] = (juce::uint16) (r0[k] * (256 - fy) + r1[k] * fy);          // (8.8 fixed point)
        auto* out = (juce::PixelARGB*) bd.getLinePointer (Y);
        const juce::uint16* r = row.data();
        for (int X = 0; X < W; ++X)
        {
            const juce::uint16* p = r + x0[(size_t) X]; const juce::uint16* q = r + x1[(size_t) X];
            const int f = fx[(size_t) X], g = 256 - f;
            out[X].setARGB (255, (juce::uint8) ((p[0] * g + q[0] * f) >> 16), (juce::uint8) ((p[1] * g + q[1] * f) >> 16), (juce::uint8) ((p[2] * g + q[2] * f) >> 16));
        }
    }
}

void FilmPlayer::run()
{
    Film film;
    double play = 0.0;                                    // seconds this run has been playing
    auto last = juce::Time::getMillisecondCounterHiRes();
    while (! threadShouldExit())
    {
        Ask a;
        { const juce::ScopedLock sl (lock); a = want; }
        const auto now = juce::Time::getMillisecondCounterHiRes();
        const double dt = juce::jlimit (0.0, 0.25, (now - last) * 0.001);
        last = now;
        if (! a.running || a.pixelW < 16 || a.pixelH < 16 || a.world.isEmpty()) { wait (150); continue; }
        if (film.world() != a.world)
        {
            if (! film.open (a.world)) { wait (600); continue; }
            play = 0.0;                                   // every run starts from its painting
        }
        play += dt;
        const int n = film.frames();
        const double cycle = 2.0 * (double) (n - 1) * (double) film.pace();
        // out and back, easing to a stop at both ends: the far end, and the painting
        const float pos = (float) ((double) (n - 1) * 0.5 * (1.0 - std::cos (juce::MathConstants<double>::twoPi * play / cycle)));
        const int i = juce::jlimit (0, n - 1, (int) pos), j = juce::jmin (n - 1, i + 1);
        const int blend = (int) std::lround ((pos - (float) i) * 256.0f);
        const auto fa = film.frame (i);
        const auto fb = blend > 0 && j != i ? film.frame (j) : nullptr;
        if (fa == nullptr) { wait (400); continue; }

        juce::Image* target = nullptr;                    // a picture nobody else is holding
        for (auto& im : pool) if (im.isNull() || im.getReferenceCount() <= 1) { target = &im; break; }
        if (target == nullptr) { wait (12); continue; }
        if (target->isNull() || target->getWidth() != a.pixelW || target->getHeight() != a.pixelH) *target = juce::Image (juce::Image::ARGB, a.pixelW, a.pixelH, false);
        render (*fa, fb.get(), blend, a, *target);

        Shown s;
        s.image = *target; s.position = pos; s.frames = n;
        { const juce::ScopedLock sl (lock); front = s; frontWorld = a.world; }
        const double took = juce::Time::getMillisecondCounterHiRes() - now;
        makeMs.store ((float) took); count.fetch_add (1);
        triggerAsyncUpdate();
        wait (juce::jmax (10, (int) ((double) pictureEveryMs() - took)));       // fifteen pictures a second, fewer where the window is slow to draw
    }
}
} // namespace atlas::ui
