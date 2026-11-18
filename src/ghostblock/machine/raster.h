/*
 * Poltergeist 2 (GhostBlock) - the ghost-machine editor: a small software compositor.
 *
 * Everything the machine draws ends up in one opaque frame buffer at the screen's own pixel size. The painted
 * layers and the sprites are kept run-length encoded (premultiplied, clear pixels cost nothing) and are put down
 * with plain loops, clipped to the cells of the picture that changed. Text and paths are JUCE's software renderer.
 * The same code runs on every platform, so a screenshot made here is the picture on the Mac.
 *
 * UI only: nothing in this folder touches the engine or its sound.
 */
#pragma once

#include <cstdint>
#include <vector>
#include <juce_graphics/juce_graphics.h>

namespace ghostblock::p2::machine
{
using Px = uint32_t; // premultiplied ARGB, 0xAARRGGBB (the memory layout of juce::PixelARGB)
using IRect = juce::Rectangle<int>;

// a destination: device pixels somebody else owns
struct Surface
{
    Px *data{nullptr};
    int w{0}, h{0}, stride{0}; // stride in pixels
    Px *row(int y) const { return data + (size_t)y * (size_t)stride; }
    IRect bounds() const { return {0, 0, w, h}; }
};

// owned premultiplied pixels
struct Bitmap
{
    int w{0}, h{0};
    std::vector<Px> px;
    Bitmap() = default;
    Bitmap(int ww, int hh) : w(ww), h(hh), px((size_t)ww * (size_t)hh, 0u) {}
    bool empty() const { return w <= 0 || h <= 0; }
    Px *row(int y) { return px.data() + (size_t)y * (size_t)w; }
    const Px *row(int y) const { return px.data() + (size_t)y * (size_t)w; }
    Surface surface() { return {px.data(), w, h, w}; }

    static Bitmap fromImage(const juce::Image &);
    juce::Image toImage() const;
    Bitmap half() const;                       // 2 x 2 box
    Bitmap resampled(int newW, int newH) const; // bilinear
    Bitmap cropped(IRect r) const;
    IRect contentBounds() const; // the pixels that are not clear
    void brighten(float k);      // rgb x k (clamped to its alpha)
};

// a run-length sprite: clear pixels are not stored, solid runs are copied
struct Sprite
{
    int w{0}, h{0}; // its nominal size (device px)
    IRect box;      // where it has pixels
    struct Span
    {
        int32_t x, n;
        uint32_t off;
        uint32_t solid;
    };
    std::vector<uint32_t> rowStart; // box.getHeight() + 1
    std::vector<Span> spans;
    std::vector<Px> pix;

    bool empty() const { return spans.empty(); }
    static Sprite fromBitmap(const Bitmap &);
    size_t bytes() const { return pix.size() * 4 + spans.size() * sizeof(Span) + rowStart.size() * 4; }
};

enum class Blend
{
    Over,
    Screen // light: dst + src - dst * src (the mock-up draws every "add" sprite this way)
};

// ---- all positions are device pixels of the destination. alpha256: 256 = as painted; Screen takes more (a brighter light)
void blit(const Surface &dst, IRect clip, const Sprite &s, int x, int y, int alpha256 = 256, Blend b = Blend::Over,
          const uint8_t *lut = nullptr);
void blitBitmap(const Surface &dst, IRect clip, const Bitmap &b, int x, int y, int alpha256 = 256, Blend bl = Blend::Over);
// nearest neighbour, the bitmap enlarged `scale` times (soft things kept small: the sun shafts, the lightning glow, the cloud)
void blitScaled(const Surface &dst, IRect clip, const Bitmap &b, float x, float y, float scale, int alpha256, Blend bl,
                bool mirrorX = false);
// bilinear, any affine map from the bitmap's pixels to the destination's; gain256 brightens (hover / held)
void blitAffine(const Surface &dst, IRect clip, const Bitmap &b, const juce::AffineTransform &toDst, int alpha256 = 256,
                Blend bl = Blend::Over, int gain256 = 256);
void copyRect(const Surface &dst, const Surface &src, IRect r); // same coordinates in both
void fillOver(const Surface &dst, IRect r, juce::Colour c, int alpha256);
void applyLut(const Surface &dst, IRect r, const uint8_t *lr, const uint8_t *lg, const uint8_t *lb);
void lerpRect(const Surface &dst, const Surface &a, const Surface &b, IRect r, int t256); // dst = a + (b - a) t

// a soft copy of a bitmap's alpha in one colour (label shadows, the halo round a part under the pointer)
Bitmap blurredAlpha(const Bitmap &src, float sigma, juce::Colour c, float gain, int pad);
} // namespace ghostblock::p2::machine
