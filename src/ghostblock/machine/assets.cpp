/*
 * Poltergeist 2 (GhostBlock) - the ghost-machine editor: the embedded art, the layout files, Poppins, the label
 * cache, and the small shared pieces (the cell grid, the compose context).
 */
#include "ghostblock/machine/machine-impl.h"

#include <cmrc/cmrc.hpp>

CMRC_DECLARE(p2machine);

namespace ghostblock::p2::machine
{
// ============================================================================================== colours of the design
juce::Colour ghostColour(int op)
{
    static const juce::uint32 c[ss::numOps] = {0xfffff0cc, 0xff84e092, 0xffbda2ff, 0xff86d8ff, 0xffffb340, 0xffff93a8};
    return juce::Colour(c[(size_t)juce::jlimit(0, (int)ss::numOps - 1, op)]);
}
juce::Colour cornerColour(int c)
{
    static const juce::uint32 k[Morph::N] = {0xff84e092, 0xffff93a8, 0xff86d8ff, 0xffffb340};
    return juce::Colour(k[(size_t)juce::jlimit(0, Morph::N - 1, c)]);
}
juce::Colour clothColour(int op)
{
    static const juce::uint32 c[ss::numOps] = {0xffb7ab8c, 0xff6d8a62, 0xff8576a8, 0xff5f8fa6, 0xffa8823e, 0xffa8686e};
    return juce::Colour(c[(size_t)juce::jlimit(0, (int)ss::numOps - 1, op)]);
}

// ============================================================================================== the cell grid
void CellSet::fill()
{
    for (int i = 0; i < NCELLS; ++i)
        set(i);
}
bool CellSet::any() const
{
    for (auto v : w)
        if (v)
            return true;
    return false;
}
int CellSet::count() const
{
    int n = 0;
    for (int i = 0; i < NCELLS; ++i)
        n += test(i) ? 1 : 0;
    return n;
}
void CellSet::mark(FRect r)
{
    if (r.isEmpty())
        return;
    const int x0 = juce::jlimit(0, COLS - 1, (int)std::floor(r.getX() / CELL)), x1 = juce::jlimit(0, COLS - 1, (int)std::floor((r.getRight() - 0.001f) / CELL));
    const int y0 = juce::jlimit(0, ROWS - 1, (int)std::floor(r.getY() / CELL)), y1 = juce::jlimit(0, ROWS - 1, (int)std::floor((r.getBottom() - 0.001f) / CELL));
    if (r.getRight() <= 0 || r.getBottom() <= 0 || r.getX() >= CW || r.getY() >= CH)
        return;
    for (int y = y0; y <= y1; ++y)
        for (int x = x0; x <= x1; ++x)
            set(y * COLS + x);
}
void CellSet::rects(std::vector<IRect> &out, int S) const
{
    out.clear();
    struct Run
    {
        int x0, x1, y0;
        bool kept;
    };
    std::vector<Run> open, now;
    auto emit = [&](const Run &r, int y1) {
        const int px = r.x0 * CELL * S, py = r.y0 * CELL * S;
        out.push_back({px, py, juce::jmin(CW * S, r.x1 * CELL * S) - px, juce::jmin(CH * S, y1 * CELL * S) - py});
    };
    for (int y = 0; y <= ROWS; ++y)
    {
        now.clear();
        if (y < ROWS)
            for (int x = 0; x < COLS;)
            {
                if (!test(y * COLS + x))
                {
                    ++x;
                    continue;
                }
                int e = x + 1;
                while (e < COLS && test(y * COLS + e))
                    ++e;
                now.push_back({x, e, y, false});
                x = e;
            }
        for (auto &o : open)
        {
            o.kept = false;
            for (auto &n : now)
                if (n.x0 == o.x0 && n.x1 == o.x1)
                {
                    n.kept = true;
                    o.kept = true;
                    break;
                }
            if (!o.kept)
                emit(o, y);
        }
        open.erase(std::remove_if(open.begin(), open.end(), [](const Run &r) { return !r.kept; }), open.end());
        for (auto &n : now)
            if (!n.kept)
                open.push_back(n);
    }
}
FRect CellSet::bounds() const
{
    int x0 = COLS, y0 = ROWS, x1 = -1, y1 = -1;
    for (int i = 0; i < NCELLS; ++i)
        if (test(i))
        {
            const int x = i % COLS, y = i / COLS;
            x0 = juce::jmin(x0, x);
            x1 = juce::jmax(x1, x);
            y0 = juce::jmin(y0, y);
            y1 = juce::jmax(y1, y);
        }
    if (x1 < 0)
        return {};
    return FRect((float)(x0 * CELL), (float)(y0 * CELL), (float)((x1 - x0 + 1) * CELL), (float)((y1 - y0 + 1) * CELL)).getIntersection({0, 0, (float)CW, (float)CH});
}
std::vector<uint16_t> cellsOf(FRect r)
{
    CellSet s;
    s.mark(r);
    std::vector<uint16_t> out;
    for (int i = 0; i < NCELLS; ++i)
        if (s.test(i))
            out.push_back((uint16_t)i);
    return out;
}
std::vector<uint16_t> cellsOf(const Sprite &sp, float dx, float dy, int S)
{
    CellSet s;
    if (!sp.empty())
    {
        const int ox = (int)std::lround(dx * S), oy = (int)std::lround(dy * S), cs = CELL * S;
        for (int r = 0; r < sp.box.getHeight(); ++r)
        {
            const int y = oy + sp.box.getY() + r;
            if (y < 0 || y >= CH * S)
                continue;
            const int cy = y / cs;
            for (uint32_t k = sp.rowStart[(size_t)r]; k < sp.rowStart[(size_t)r + 1]; ++k)
            {
                const auto &q = sp.spans[k];
                const int a = juce::jmax(0, ox + q.x), b = juce::jmin(CW * S - 1, ox + q.x + q.n - 1);
                for (int cx = a / cs; cx <= b / cs && a <= b; ++cx)
                    s.set(cy * COLS + cx);
            }
        }
    }
    std::vector<uint16_t> out;
    for (int i = 0; i < NCELLS; ++i)
        if (s.test(i))
            out.push_back((uint16_t)i);
    return out;
}

// ============================================================================================== composing
void Canvas::alloc(int w, int h)
{
    img = juce::Image(juce::Image::ARGB, w, h, true, juce::SoftwareImageType());
    juce::Image::BitmapData bd(img, juce::Image::BitmapData::readWrite);
    s = {(Px *)(void *)bd.data, w, h, bd.lineStride / 4};
    for (int y = 0; y < h; ++y)
        std::fill(s.row(y), s.row(y) + w, 0xff000000u);
}

bool Ctx::touches(FRect r) const
{
    const IRect d((int)std::floor(r.getX() * S), (int)std::floor(r.getY() * S), (int)std::ceil(r.getWidth() * S) + 1, (int)std::ceil(r.getHeight() * S) + 1);
    if (!d.intersects(bounds))
        return false;
    for (auto &c : *clips)
        if (c.intersects(d))
            return true;
    return false;
}
void Ctx::sprite(const Sprite &s, float x, float y, float alpha, Blend b, const uint8_t *lut) const
{
    if (s.empty() || alpha <= 0.003f)
        return;
    const int dx = (int)std::lround(x * S), dy = (int)std::lround(y * S);
    if (!s.box.translated(dx, dy).intersects(bounds))
        return;
    const int a = (int)std::lround(alpha * 256.0f);
    for (auto &c : *clips)
        blit(dst, c, s, dx, dy, a, b, lut);
}
void Ctx::affine(const Bitmap &b, const juce::AffineTransform &t, float alpha, Blend bl, float gain) const
{
    if (b.empty() || alpha <= 0.003f)
        return;
    const auto dev = juce::AffineTransform::scale(1.0f / (float)S).followedBy(t).scaled((float)S);
    const int a = (int)std::lround(alpha * 256.0f), g = (int)std::lround(gain * 256.0f);
    for (auto &c : *clips)
        blitAffine(dst, c, b, dev, a, bl, g);
}
void Ctx::scaled(const Bitmap &b, float x, float y, float scale, float alpha, Blend bl, bool mirror) const
{
    if (b.empty() || alpha <= 0.003f)
        return;
    const int a = (int)std::lround(alpha * 256.0f);
    for (auto &c : *clips)
        blitScaled(dst, c, b, x * S, y * S, scale * S, a, bl, mirror);
}
void Ctx::fill(FRect r, juce::Colour col, float alpha) const
{
    const IRect d((int)std::lround(r.getX() * S), (int)std::lround(r.getY() * S), (int)std::lround(r.getWidth() * S), (int)std::lround(r.getHeight() * S));
    for (auto &c : *clips)
        fillOver(dst, c.getIntersection(d), col, (int)std::lround(alpha * 256.0f));
}

// ============================================================================================== the embedded files
static cmrc::embedded_filesystem resFs() { return cmrc::p2machine::get_filesystem(); }

bool Assets::has(const juce::String &path) { return resFs().exists(path.toStdString()); }

juce::String Assets::textFile(const juce::String &path)
{
    auto fs = resFs();
    if (!fs.exists(path.toStdString()))
        return {};
    auto f = fs.open(path.toStdString());
    return juce::String::fromUTF8(f.begin(), (int)f.size());
}

juce::Image Assets::image(const juce::String &path)
{
    auto fs = resFs();
    if (!fs.exists(path.toStdString()))
        return {};
    auto f = fs.open(path.toStdString());
    return juce::ImageFileFormat::loadFrom(f.begin(), f.size());
}

juce::Typeface::Ptr Assets::typeface(bool semi)
{
    static juce::Typeface::Ptr tf[2];
    auto &t = tf[semi ? 1 : 0];
    if (t == nullptr)
    {
        auto fs = resFs();
        const char *p = semi ? "fonts/Poppins-SemiBold.ttf" : "fonts/Poppins-Medium.ttf";
        if (fs.exists(p))
        {
            auto f = fs.open(p);
            t = juce::Typeface::createSystemTypefaceFor(f.begin(), f.size());
        }
    }
    return t;
}

Bitmap Assets::bitmap(const juce::String &path, double pngScale) const
{
    auto fs = resFs();
    if (!fs.exists(path.toStdString()))
    {
        jassertfalse;
        return {};
    }
    auto f = fs.open(path.toStdString());
    auto b = Bitmap::fromImage(juce::ImageFileFormat::loadFrom(f.begin(), f.size()));
    const double k = (double)S / (pngScale > 0 ? pngScale : 2.0); // wanted over stored
    if (std::abs(k - 1.0) < 1e-6 || b.empty())
        return b;
    if (std::abs(k - 0.5) < 1e-6)
        return b.half();
    return b.resampled(juce::jmax(1, (int)std::lround(b.w * k)), juce::jmax(1, (int)std::lround(b.h * k)));
}

Assets::Assets(int scale) : S(scale)
{
    layout = juce::JSON::parse(textFile("ui-layout.json"));
    common = juce::JSON::parse(textFile("common/layers.json"));
    kitJson = juce::JSON::parse(textFile("common/ui.json"));
    spyJson = juce::JSON::parse(textFile("common/spyglass/spyglass.json"));
    subJson = juce::JSON::parse(textFile("common/sub/sub.json"));
    // the pages are whatever "page_order" lists (the first three are GHOSTS, MAIN, MORPH; the code that is special to
    // one of them asks for it by that place); a page added to the layout and to assets/<key>/ is picked up here
    if (auto *po = layout["page_order"].getArray())
        for (auto &k : *po)
            if (resFs().exists((k.toString() + "/layers.json").toStdString()))
                pageKeys.add(k.toString());
    for (auto &k : pageKeys)
        pages.push_back(juce::JSON::parse(textFile(k + "/layers.json")));
    auto face = [](const char *p) -> juce::Typeface::Ptr {
        auto fs = resFs();
        if (!fs.exists(p))
            return {};
        auto f = fs.open(p);
        return juce::Typeface::createSystemTypefaceFor(f.begin(), f.size());
    };
    medium = face("fonts/Poppins-Medium.ttf");
    semibold = face("fonts/Poppins-SemiBold.ttf");
}

juce::String Assets::resolvePath(const juce::String &dir, const juce::String &file)
{
    juce::StringArray parts, out;
    parts.addTokens(dir + "/" + file, "/", "");
    for (auto &p : parts)
    {
        if (p == "..")
            out.remove(out.size() - 1);
        else if (p.isNotEmpty() && p != ".")
            out.add(p);
    }
    return out.joinIntoString("/");
}

const Kit &Assets::kit(const juce::String &name)
{
    auto it = kits.find(name);
    if (it != kits.end())
        return it->second;
    Kit k;
    if (auto *arr = kitJson["assets"].getArray())
        for (auto &a : *arr)
            if (a["name"].toString() == name)
            {
                k.w = numOf(a["w"]);
                k.h = numOf(a["h"]);
                k.pivot = a.hasProperty("pivot") ? pointOf(a["pivot"]) : FPoint(k.w * 0.5f, k.h * 0.5f);
                k.bmp = bitmap("common/ui/" + a["file"].toString(), 2.0);
                break;
            }
    if (k.bmp.empty()) // the SPYGLASS and the size grip have a manifest of their own (spyglass.py)
        if (auto *arr = spyJson["assets"].getArray())
            for (auto &a : *arr)
                if (a["name"].toString() == name)
                {
                    k.w = numOf(a["w"]);
                    k.h = numOf(a["h"]);
                    k.pivot = a.hasProperty("pivot") ? pointOf(a["pivot"]) : FPoint(k.w * 0.5f, k.h * 0.5f);
                    k.bmp = bitmap("common/spyglass/" + a["file"].toString(), 2.0);
                    break;
                }
    if (k.bmp.empty()) // the SUB sign and panel have a manifest of their own too
        if (auto *arr = subJson["assets"].getArray())
            for (auto &a : *arr)
                if (a["name"].toString() == name)
                {
                    k.w = numOf(a["w"]);
                    k.h = numOf(a["h"]);
                    k.pivot = a.hasProperty("pivot") ? pointOf(a["pivot"]) : FPoint(k.w * 0.5f, k.h * 0.5f);
                    k.bmp = bitmap("common/sub/" + a["file"].toString(), 2.0);
                    break;
                }
    if (k.bmp.empty()) // a page may bring kit parts of its own (layers.json "ui_extra": the FX page's sign)
        for (auto &pj : pages)
            if (auto *arr = pj["ui_extra"].getArray())
                for (auto &a : *arr)
                    if (a["name"].toString() == name && k.bmp.empty())
                    {
                        k.w = numOf(a["w"]);
                        k.h = numOf(a["h"]);
                        k.pivot = a.hasProperty("pivot") ? pointOf(a["pivot"]) : FPoint(k.w * 0.5f, k.h * 0.5f);
                        k.bmp = bitmap(resolvePath("common/ui", a["file"].toString()), 2.0);
                    }
    jassert(!k.bmp.empty());
    return kits.emplace(name, std::move(k)).first->second;
}

// ============================================================================================== text
juce::Font Assets::font(float designPx, bool semi, float trackEm, float hscale) const
{
    auto tf = semi ? semibold : medium;
    auto o = juce::FontOptions().withPointHeight(designPx * (float)S).withMetricsKind(juce::TypefaceMetricsKind::portable);
    if (tf != nullptr)
        o = o.withTypeface(tf);
    juce::Font f(o);
    const float h = f.getHeight(), pt = f.getHeightInPoints();
    if (trackEm != 0.f && h > 0)
        f.setExtraKerningFactor(trackEm * pt / h);
    if (std::abs(hscale - 1.0f) > 1e-3f)
        f.setHorizontalScale(hscale);
    return f;
}

float Assets::textWidth(const juce::String &t, const TextStyle &st) const
{
    return juce::GlyphArrangement::getStringWidth(font(st.size, st.semi, st.track, 1.0f), t) / (float)S;
}

static void overInto(Bitmap &dst, const Bitmap &src, int ox, int oy)
{
    for (int y = 0; y < src.h; ++y)
    {
        const int dy = y + oy;
        if (dy < 0 || dy >= dst.h)
            continue;
        const Px *s = src.row(y);
        Px *d = dst.row(dy);
        for (int x = 0; x < src.w; ++x)
        {
            const int dx = x + ox;
            const Px p = s[x];
            if (dx < 0 || dx >= dst.w || (p >> 24) == 0)
                continue;
            const uint32_t ia = 255u - (p >> 24);
            const Px q = d[dx];
            auto ch = [&](int sh) { return juce::jmin(255u, ((p >> sh) & 0xffu) + ((((q >> sh) & 0xffu) * ia + 127u) / 255u)); };
            d[dx] = (ch(24) << 24) | (ch(16) << 16) | (ch(8) << 8) | ch(0);
        }
    }
}

const TextSprite &Assets::text(const juce::String &t, const TextStyle &st, float squeeze)
{
    std::string key = t.toStdString();
    key += '\x01';
    key += std::to_string((int)std::lround(st.size * 100)) + (st.semi ? "s" : "m") + std::to_string((int)std::lround(st.track * 1000)) + "c" +
           std::to_string(st.col.getARGB()) + (st.shadow ? "h" : "-") + std::to_string(st.glow.getARGB()) + "q" + std::to_string((int)std::lround(squeeze * 500));
    auto it = texts.find(key);
    if (it != texts.end())
        return it->second;
    if (texts.size() > 1500)
        texts.clear(); // (the status line and the values make new strings all day)

    TextSprite out;
    const auto f = font(st.size, st.semi, st.track, squeeze);
    const float em = st.size * (float)S, asc = 1.05f * em, lineH = 1.4f * em;
    const float wDev = juce::GlyphArrangement::getStringWidth(f, t);
    const float padD = (st.shadow || !st.glow.isTransparent()) ? 5.0f : 1.0f;
    const int pad = (int)std::ceil(padD * S);
    const int iw = juce::jmax(1, (int)std::ceil(wDev)) + 2 * pad, ih = (int)std::ceil(lineH) + 2 * pad;
    juce::Image img(juce::Image::ARGB, iw, ih, true, juce::SoftwareImageType());
    {
        juce::Graphics g(img);
        g.setFont(f);
        g.setColour(st.col);
        g.drawSingleLineText(t, pad, pad + (int)std::lround(asc), juce::Justification::left);
    }
    const auto glyphs = Bitmap::fromImage(img);
    Bitmap acc(iw, ih);
    if (st.shadow)
    { // the mock-up's text-shadow: 0 0 3px rgba(0,0,0,.95), 0 1px 1px #000
        overInto(acc, blurredAlpha(glyphs, 1.5f * S, juce::Colours::black, 1.9f, 0), 0, 0);
        overInto(acc, blurredAlpha(glyphs, 0.5f * S, juce::Colours::black, 1.0f, 0), 0, S);
    }
    if (!st.glow.isTransparent())
        overInto(acc, blurredAlpha(glyphs, 2.4f * S, st.glow, 1.6f, 0), 0, 0);
    overInto(acc, glyphs, 0, 0);
    out.spr = Sprite::fromBitmap(acc);
    out.w = wDev / (float)S;
    out.pad = (float)pad / (float)S;
    return texts.emplace(std::move(key), std::move(out)).first->second;
}

Bitmap Assets::renderBitmap(float dw, float dh, const std::function<void(juce::Graphics &)> &draw) const
{
    juce::Image img(juce::Image::ARGB, juce::jmax(1, (int)std::ceil(dw * S)), juce::jmax(1, (int)std::ceil(dh * S)), true, juce::SoftwareImageType());
    {
        juce::Graphics g(img);
        g.addTransform(juce::AffineTransform::scale((float)S));
        draw(g);
    }
    return Bitmap::fromImage(img);
}
Sprite Assets::render(float dw, float dh, const std::function<void(juce::Graphics &)> &draw) const { return Sprite::fromBitmap(renderBitmap(dw, dh, draw)); }
} // namespace ghostblock::p2::machine
