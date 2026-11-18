/*
 * Poltergeist 2 (GhostBlock) - the ghost-machine editor: the software compositor's loops. See raster.h.
 */
#include "ghostblock/machine/raster.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ghostblock::p2::machine
{
namespace
{
inline Px scale256(Px c, uint32_t a) noexcept // a: 0..256
{
    const uint32_t rb = (((c & 0x00ff00ffu) * a) >> 8) & 0x00ff00ffu;
    const uint32_t ag = (((c >> 8) & 0x00ff00ffu) * a) & 0xff00ff00u;
    return rb | ag;
}
inline Px gainPx(Px c, uint32_t g) noexcept // g may be above 256: rgb x g, never above its alpha (still a legal pixel)
{
    const uint32_t a = c >> 24;
    const uint32_t r = std::min(a, (((c >> 16) & 0xffu) * g) >> 8), gr = std::min(a, (((c >> 8) & 0xffu) * g) >> 8),
                   b = std::min(a, ((c & 0xffu) * g) >> 8);
    return (a << 24) | (r << 16) | (gr << 8) | b;
}
inline Px lightPx(Px c, uint32_t g) noexcept // for Screen: every channel x g, clamped to 255
{
    const uint32_t r = std::min(255u, (((c >> 16) & 0xffu) * g) >> 8), gr = std::min(255u, (((c >> 8) & 0xffu) * g) >> 8),
                   b = std::min(255u, ((c & 0xffu) * g) >> 8);
    return (r << 16) | (gr << 8) | b;
}
inline Px overPx(Px d, Px s) noexcept // the destination is opaque
{
    const uint32_t sa = s >> 24;
    const uint32_t ia = 256u - sa - (sa >> 7);
    return (s + scale256(d, ia)) | 0xff000000u;
}
inline uint32_t screen8(uint32_t d, uint32_t s) noexcept { return d + s - ((d * s * 257u + 32768u) >> 16); }
inline Px screenPx(Px d, Px s) noexcept
{
    return 0xff000000u | (screen8((d >> 16) & 0xffu, (s >> 16) & 0xffu) << 16) | (screen8((d >> 8) & 0xffu, (s >> 8) & 0xffu) << 8) |
           screen8(d & 0xffu, s & 0xffu);
}
inline Px lutPx(Px c, const uint8_t *lut) noexcept
{
    return (c & 0xff000000u) | ((uint32_t)lut[(c >> 16) & 0xffu] << 16) | ((uint32_t)lut[(c >> 8) & 0xffu] << 8) | (uint32_t)lut[c & 0xffu];
}
inline Px lerpPx(Px a, Px b, uint32_t t) noexcept // t: 0..256
{
    const uint32_t it = 256u - t;
    const uint32_t rb = (((a & 0x00ff00ffu) * it + (b & 0x00ff00ffu) * t) >> 8) & 0x00ff00ffu;
    const uint32_t ag = (((a >> 8) & 0x00ff00ffu) * it + ((b >> 8) & 0x00ff00ffu) * t) & 0xff00ff00u;
    return rb | ag;
}
} // namespace

// ============================================================================================== Bitmap
Bitmap Bitmap::fromImage(const juce::Image &src)
{
    if (!src.isValid())
        return {};
    const auto img = src.getFormat() == juce::Image::ARGB ? src : src.convertedToFormat(juce::Image::ARGB);
    Bitmap b(img.getWidth(), img.getHeight());
    const juce::Image::BitmapData bd(img, juce::Image::BitmapData::readOnly);
    for (int y = 0; y < b.h; ++y)
        std::memcpy(b.row(y), bd.getLinePointer(y), (size_t)b.w * 4);
    return b;
}

juce::Image Bitmap::toImage() const
{
    if (empty())
        return {};
    juce::Image img(juce::Image::ARGB, w, h, false, juce::SoftwareImageType());
    juce::Image::BitmapData bd(img, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < h; ++y)
        std::memcpy(bd.getLinePointer(y), row(y), (size_t)w * 4);
    return img;
}

Bitmap Bitmap::half() const
{
    Bitmap o((w + 1) / 2, (h + 1) / 2);
    for (int y = 0; y < o.h; ++y)
    {
        const Px *r0 = row(std::min(h - 1, y * 2)), *r1 = row(std::min(h - 1, y * 2 + 1));
        Px *d = o.row(y);
        for (int x = 0; x < o.w; ++x)
        {
            const int x0 = std::min(w - 1, x * 2), x1 = std::min(w - 1, x * 2 + 1);
            const Px a = r0[x0], b = r0[x1], c = r1[x0], e = r1[x1];
            const uint32_t rb = ((a & 0x00ff00ffu) + (b & 0x00ff00ffu) + (c & 0x00ff00ffu) + (e & 0x00ff00ffu) + 0x00020002u) >> 2;
            const uint32_t ag = (((a >> 8) & 0x00ff00ffu) + ((b >> 8) & 0x00ff00ffu) + ((c >> 8) & 0x00ff00ffu) + ((e >> 8) & 0x00ff00ffu) + 0x00020002u) >> 2;
            d[x] = (rb & 0x00ff00ffu) | ((ag & 0x00ff00ffu) << 8);
        }
    }
    return o;
}

Bitmap Bitmap::resampled(int nw, int nh) const
{
    Bitmap o(nw, nh);
    if (empty() || nw <= 0 || nh <= 0)
        return o;
    const double sx = (double)w / nw, sy = (double)h / nh;
    for (int y = 0; y < nh; ++y)
    {
        const double fy = std::clamp((y + 0.5) * sy - 0.5, 0.0, (double)(h - 1));
        const int y0 = (int)fy, y1 = std::min(h - 1, y0 + 1);
        const uint32_t ty = (uint32_t)((fy - y0) * 256.0);
        const Px *r0 = row(y0), *r1 = row(y1);
        Px *d = o.row(y);
        for (int x = 0; x < nw; ++x)
        {
            const double fx = std::clamp((x + 0.5) * sx - 0.5, 0.0, (double)(w - 1));
            const int x0 = (int)fx, x1 = std::min(w - 1, x0 + 1);
            const uint32_t tx = (uint32_t)((fx - x0) * 256.0);
            d[x] = lerpPx(lerpPx(r0[x0], r0[x1], tx), lerpPx(r1[x0], r1[x1], tx), ty);
        }
    }
    return o;
}

Bitmap Bitmap::cropped(IRect r) const
{
    r = r.getIntersection({0, 0, w, h});
    Bitmap o(r.getWidth(), r.getHeight());
    for (int y = 0; y < o.h; ++y)
        std::memcpy(o.row(y), row(r.getY() + y) + r.getX(), (size_t)o.w * 4);
    return o;
}

IRect Bitmap::contentBounds() const
{
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; ++y)
    {
        const Px *r = row(y);
        int a = 0;
        while (a < w && (r[a] >> 24) == 0)
            ++a;
        if (a == w)
            continue;
        int b = w - 1;
        while (b > a && (r[b] >> 24) == 0)
            --b;
        x0 = std::min(x0, a);
        x1 = std::max(x1, b);
        y0 = std::min(y0, y);
        y1 = y;
    }
    return x1 < 0 ? IRect() : IRect(x0, y0, x1 - x0 + 1, y1 - y0 + 1);
}

void Bitmap::brighten(float k)
{
    const uint32_t g = (uint32_t)std::lround(k * 256.0f);
    for (auto &p : px)
        if (p >> 24)
            p = gainPx(p, g);
}

// ============================================================================================== Sprite
Sprite Sprite::fromBitmap(const Bitmap &b)
{
    Sprite s;
    s.w = b.w;
    s.h = b.h;
    s.box = b.contentBounds();
    if (s.box.isEmpty())
        return s;
    s.rowStart.reserve((size_t)s.box.getHeight() + 1);
    for (int y = s.box.getY(); y < s.box.getBottom(); ++y)
    {
        s.rowStart.push_back((uint32_t)s.spans.size());
        const Px *r = b.row(y);
        int x = s.box.getX();
        const int xe = s.box.getRight();
        while (x < xe)
        {
            while (x < xe && (r[x] >> 24) == 0)
                ++x;
            if (x >= xe)
                break;
            const bool solid = (r[x] >> 24) == 0xffu;
            int e = x + 1;
            if (solid)
                while (e < xe && (r[e] >> 24) == 0xffu)
                    ++e;
            else
                while (e < xe && (r[e] >> 24) != 0 && (r[e] >> 24) != 0xffu)
                    ++e;
            s.spans.push_back({x, e - x, (uint32_t)s.pix.size(), solid ? 1u : 0u});
            s.pix.insert(s.pix.end(), r + x, r + e);
            x = e;
        }
    }
    s.rowStart.push_back((uint32_t)s.spans.size());
    return s;
}

// ============================================================================================== blits
void blit(const Surface &dst, IRect clip, const Sprite &s, int x, int y, int alpha256, Blend bl, const uint8_t *lut)
{
    if (s.empty() || alpha256 <= 0)
        return;
    clip = clip.getIntersection(dst.bounds()).getIntersection(s.box.translated(x, y));
    if (clip.isEmpty())
        return;
    const int cx0 = clip.getX() - x, cx1 = clip.getRight() - x; // in the sprite's coordinates
    const uint32_t a = (uint32_t)alpha256;
    const bool plain = bl == Blend::Over && a >= 256u && lut == nullptr;
    for (int dy = clip.getY(); dy < clip.getBottom(); ++dy)
    {
        const size_t r = (size_t)(dy - y - s.box.getY());
        const Sprite::Span *sp = s.spans.data() + s.rowStart[r], *end = s.spans.data() + s.rowStart[r + 1];
        Px *drow = dst.row(dy);
        for (; sp != end; ++sp)
        {
            if (sp->x >= cx1)
                break;
            int x0 = sp->x, x1 = sp->x + sp->n;
            if (x1 <= cx0)
                continue;
            x0 = std::max(x0, cx0);
            x1 = std::min(x1, cx1);
            const Px *src = s.pix.data() + sp->off + (size_t)(x0 - sp->x);
            Px *d = drow + (x + x0);
            const int n = x1 - x0;
            if (plain)
            {
                if (sp->solid)
                    std::memcpy(d, src, (size_t)n * 4);
                else
                    for (int i = 0; i < n; ++i)
                        d[i] = overPx(d[i], src[i]);
            }
            else if (bl == Blend::Over)
            {
                const uint32_t aa = std::min(256u, a);
                if (lut != nullptr)
                {
                    if (sp->solid && aa >= 256u)
                        for (int i = 0; i < n; ++i)
                            d[i] = lutPx(src[i], lut);
                    else if (aa >= 256u)
                        for (int i = 0; i < n; ++i)
                            d[i] = overPx(d[i], lutPx(src[i], lut));
                    else
                        for (int i = 0; i < n; ++i)
                            d[i] = overPx(d[i], scale256(lutPx(src[i], lut), aa));
                }
                else
                    for (int i = 0; i < n; ++i)
                        d[i] = overPx(d[i], scale256(src[i], aa));
            }
            else
            {
                if (a == 256u)
                    for (int i = 0; i < n; ++i)
                        d[i] = screenPx(d[i], src[i]);
                else
                    for (int i = 0; i < n; ++i)
                        d[i] = screenPx(d[i], lightPx(src[i], a));
            }
        }
    }
}

void blitBitmap(const Surface &dst, IRect clip, const Bitmap &b, int x, int y, int alpha256, Blend bl)
{
    if (b.empty() || alpha256 <= 0)
        return;
    clip = clip.getIntersection(dst.bounds()).getIntersection({x, y, b.w, b.h});
    if (clip.isEmpty())
        return;
    const uint32_t a = (uint32_t)alpha256;
    for (int dy = clip.getY(); dy < clip.getBottom(); ++dy)
    {
        const Px *src = b.row(dy - y) + (clip.getX() - x);
        Px *d = dst.row(dy) + clip.getX();
        const int n = clip.getWidth();
        if (bl == Blend::Over)
        {
            const uint32_t aa = std::min(256u, a);
            for (int i = 0; i < n; ++i)
                if (src[i] >> 24)
                    d[i] = overPx(d[i], aa >= 256u ? src[i] : scale256(src[i], aa));
        }
        else
            for (int i = 0; i < n; ++i)
                if (src[i] & 0x00ffffffu)
                    d[i] = screenPx(d[i], a == 256u ? src[i] : lightPx(src[i], a));
    }
}

void blitScaled(const Surface &dst, IRect clip, const Bitmap &b, float x, float y, float scale, int alpha256, Blend bl, bool mirrorX)
{
    if (b.empty() || alpha256 <= 0 || scale <= 0)
        return;
    const int ix = (int)std::floor(x), iy = (int)std::floor(y);
    const IRect area(ix, iy, (int)std::ceil(b.w * scale), (int)std::ceil(b.h * scale));
    clip = clip.getIntersection(dst.bounds()).getIntersection(area);
    if (clip.isEmpty())
        return;
    const int64_t step = (int64_t)std::llround(65536.0 / scale);
    const uint32_t a = (uint32_t)alpha256;
    for (int dy = clip.getY(); dy < clip.getBottom(); ++dy)
    {
        const int sy = std::clamp((int)((((int64_t)(dy - iy)) * step + step / 2) >> 16), 0, b.h - 1);
        const Px *src = b.row(sy);
        Px *d = dst.row(dy);
        int64_t fx = ((int64_t)(clip.getX() - ix)) * step + step / 2;
        for (int dx = clip.getX(); dx < clip.getRight(); ++dx, fx += step)
        {
            int sx = std::clamp((int)(fx >> 16), 0, b.w - 1);
            if (mirrorX)
                sx = b.w - 1 - sx;
            const Px p = src[sx];
            if (bl == Blend::Over)
            {
                if (p >> 24)
                    d[dx] = overPx(d[dx], a >= 256u ? p : scale256(p, a));
            }
            else if (p & 0x00ffffffu)
                d[dx] = screenPx(d[dx], a == 256u ? p : lightPx(p, a));
        }
    }
}

void blitAffine(const Surface &dst, IRect clip, const Bitmap &b, const juce::AffineTransform &toDst, int alpha256, Blend bl, int gain256)
{
    if (b.empty() || alpha256 <= 0 || toDst.isSingularity())
        return;
    // where it lands
    float xs[4] = {0, (float)b.w, 0, (float)b.w}, ys[4] = {0, 0, (float)b.h, (float)b.h};
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    for (int i = 0; i < 4; ++i)
    {
        toDst.transformPoint(xs[i], ys[i]);
        minX = std::min(minX, xs[i]);
        maxX = std::max(maxX, xs[i]);
        minY = std::min(minY, ys[i]);
        maxY = std::max(maxY, ys[i]);
    }
    clip = clip.getIntersection(dst.bounds()).getIntersection(IRect::leftTopRightBottom((int)std::floor(minX) - 1, (int)std::floor(minY) - 1, (int)std::ceil(maxX) + 1, (int)std::ceil(maxY) + 1));
    if (clip.isEmpty())
        return;
    const auto inv = toDst.inverted();
    const double du = inv.mat00, dv = inv.mat10;
    const int64_t duF = (int64_t)std::llround(du * 65536.0), dvF = (int64_t)std::llround(dv * 65536.0);
    const uint32_t a = (uint32_t)alpha256, g = (uint32_t)gain256;
    const int W = b.w, H = b.h;
    const Px *pix = b.px.data();
    for (int dy = clip.getY(); dy < clip.getBottom(); ++dy)
    {
        // texel coordinates of the first pixel centre of this row (texel centres at whole numbers)
        const double py = dy + 0.5, px0 = clip.getX() + 0.5;
        const double u0 = inv.mat00 * px0 + inv.mat01 * py + inv.mat02 - 0.5, v0 = inv.mat10 * px0 + inv.mat11 * py + inv.mat12 - 0.5;
        // the stretch of this row that falls on the bitmap: -1 < u < W and -1 < v < H
        double lo = 0, hi = clip.getWidth();
        auto cut = [&lo, &hi](double p0, double d, double mn, double mx) {
            if (std::abs(d) < 1e-9)
            {
                if (p0 <= mn || p0 >= mx)
                    hi = lo - 1;
                return;
            }
            double t0 = (mn - p0) / d, t1 = (mx - p0) / d;
            if (t0 > t1)
                std::swap(t0, t1);
            lo = std::max(lo, t0);
            hi = std::min(hi, t1);
        };
        cut(u0, du, -1.0, (double)W);
        cut(v0, dv, -1.0, (double)H);
        if (hi < lo)
            continue;
        const int i0 = std::max(0, (int)std::ceil(lo)), i1 = std::min(clip.getWidth() - 1, (int)std::floor(hi));
        if (i1 < i0)
            continue;
        int64_t uF = (int64_t)std::llround((u0 + du * i0) * 65536.0), vF = (int64_t)std::llround((v0 + dv * i0) * 65536.0);
        Px *d = dst.row(dy) + clip.getX() + i0;
        for (int i = i0; i <= i1; ++i, ++d, uF += duF, vF += dvF)
        {
            const int tx = (int)(uF >> 16), ty = (int)(vF >> 16);
            const uint32_t fx = (uint32_t)((uF >> 8) & 0xff), fy = (uint32_t)((vF >> 8) & 0xff);
            Px p00, p10, p01, p11;
            if (tx >= 0 && ty >= 0 && tx + 1 < W && ty + 1 < H)
            {
                const Px *q = pix + (size_t)ty * (size_t)W + (size_t)tx;
                p00 = q[0];
                p10 = q[1];
                p01 = q[W];
                p11 = q[W + 1];
            }
            else
            {
                auto at = [&](int xx, int yy) -> Px { return (xx >= 0 && yy >= 0 && xx < W && yy < H) ? pix[(size_t)yy * (size_t)W + (size_t)xx] : 0u; };
                p00 = at(tx, ty);
                p10 = at(tx + 1, ty);
                p01 = at(tx, ty + 1);
                p11 = at(tx + 1, ty + 1);
            }
            if ((p00 | p10 | p01 | p11) == 0u)
                continue;
            Px p = lerpPx(lerpPx(p00, p10, fx), lerpPx(p01, p11, fx), fy);
            if (bl == Blend::Over)
            {
                if ((p >> 24) == 0)
                    continue;
                if (g != 256u)
                    p = gainPx(p, g);
                *d = overPx(*d, a >= 256u ? p : scale256(p, a));
            }
            else
            {
                const uint32_t ga = (a * g) >> 8;
                *d = screenPx(*d, ga == 256u ? p : lightPx(p, ga));
            }
        }
    }
}

void copyRect(const Surface &dst, const Surface &src, IRect r)
{
    r = r.getIntersection(dst.bounds()).getIntersection(src.bounds());
    for (int y = r.getY(); y < r.getBottom(); ++y)
        std::memcpy(dst.row(y) + r.getX(), src.row(y) + r.getX(), (size_t)r.getWidth() * 4);
}

void fillOver(const Surface &dst, IRect r, juce::Colour c, int alpha256)
{
    r = r.getIntersection(dst.bounds());
    if (r.isEmpty() || alpha256 <= 0)
        return;
    const uint32_t a = std::min(256u, (uint32_t)alpha256 * (uint32_t)c.getAlpha() / 255u);
    const Px s = scale256(0xff000000u | ((uint32_t)c.getRed() << 16) | ((uint32_t)c.getGreen() << 8) | (uint32_t)c.getBlue(), a);
    for (int y = r.getY(); y < r.getBottom(); ++y)
    {
        Px *d = dst.row(y) + r.getX();
        for (int i = 0, n = r.getWidth(); i < n; ++i)
            d[i] = overPx(d[i], s);
    }
}

void applyLut(const Surface &dst, IRect r, const uint8_t *lr, const uint8_t *lg, const uint8_t *lb)
{
    r = r.getIntersection(dst.bounds());
    for (int y = r.getY(); y < r.getBottom(); ++y)
    {
        Px *d = dst.row(y) + r.getX();
        for (int i = 0, n = r.getWidth(); i < n; ++i)
        {
            const Px p = d[i];
            d[i] = 0xff000000u | ((uint32_t)lr[(p >> 16) & 0xffu] << 16) | ((uint32_t)lg[(p >> 8) & 0xffu] << 8) | (uint32_t)lb[p & 0xffu];
        }
    }
}

void lerpRect(const Surface &dst, const Surface &a, const Surface &b, IRect r, int t256)
{
    r = r.getIntersection(dst.bounds()).getIntersection(a.bounds()).getIntersection(b.bounds());
    const uint32_t t = (uint32_t)std::clamp(t256, 0, 256);
    for (int y = r.getY(); y < r.getBottom(); ++y)
    {
        Px *d = dst.row(y) + r.getX();
        const Px *pa = a.row(y) + r.getX(), *pb = b.row(y) + r.getX();
        for (int i = 0, n = r.getWidth(); i < n; ++i)
            d[i] = lerpPx(pa[i], pb[i], t) | 0xff000000u;
    }
}

// ============================================================================================== soft alpha
Bitmap blurredAlpha(const Bitmap &src, float sigma, juce::Colour c, float gain, int pad)
{
    const int w = src.w + 2 * pad, h = src.h + 2 * pad;
    Bitmap out(w, h);
    if (src.empty())
        return out;
    std::vector<float> a((size_t)w * (size_t)h, 0.f), t((size_t)w * (size_t)h, 0.f);
    for (int y = 0; y < src.h; ++y)
    {
        const Px *r = src.row(y);
        float *d = a.data() + (size_t)(y + pad) * (size_t)w + (size_t)pad;
        for (int x = 0; x < src.w; ++x)
            d[x] = (float)(r[x] >> 24) / 255.0f;
    }
    // three box blurs are close to a gaussian
    const int rad = std::max(1, (int)std::lround(std::sqrt(sigma * sigma * 4.0f + 1.0f) * 0.5f));
    const float norm = 1.0f / (float)(2 * rad + 1);
    for (int pass = 0; pass < 3 && sigma > 0.05f; ++pass)
    {
        for (int y = 0; y < h; ++y)
        { // along x
            const float *s = a.data() + (size_t)y * (size_t)w;
            float *d = t.data() + (size_t)y * (size_t)w;
            float acc = 0;
            for (int x = -rad; x <= rad; ++x)
                acc += (x >= 0 && x < w) ? s[x] : 0.f;
            for (int x = 0; x < w; ++x)
            {
                d[x] = acc * norm;
                const int add = x + rad + 1, sub = x - rad;
                acc += (add < w ? s[add] : 0.f) - (sub >= 0 ? s[sub] : 0.f);
            }
        }
        for (int x = 0; x < w; ++x)
        { // along y
            float acc = 0;
            for (int y = -rad; y <= rad; ++y)
                acc += (y >= 0 && y < h) ? t[(size_t)y * (size_t)w + (size_t)x] : 0.f;
            for (int y = 0; y < h; ++y)
            {
                a[(size_t)y * (size_t)w + (size_t)x] = acc * norm;
                const int add = y + rad + 1, sub = y - rad;
                acc += (add < h ? t[(size_t)add * (size_t)w + (size_t)x] : 0.f) - (sub >= 0 ? t[(size_t)sub * (size_t)w + (size_t)x] : 0.f);
            }
        }
    }
    const float cr = c.getFloatRed(), cg = c.getFloatGreen(), cb = c.getFloatBlue(), ca = c.getFloatAlpha() * gain;
    for (size_t i = 0; i < a.size(); ++i)
    {
        const float v = std::clamp(a[i] * ca, 0.f, 1.f);
        if (v <= 0.003f)
            continue;
        const uint32_t A = (uint32_t)std::lround(v * 255.0f);
        out.px[i] = (A << 24) | ((uint32_t)std::lround(cr * v * 255.0f) << 16) | ((uint32_t)std::lround(cg * v * 255.0f) << 8) | (uint32_t)std::lround(cb * v * 255.0f);
    }
    return out;
}
} // namespace ghostblock::p2::machine
