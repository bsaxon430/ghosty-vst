// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
//
// Ported to JUCE from Bencho's "Heat map" (HeatMap.tsx, the Heat word), MIT licence
// (bencho.dev/licence, full text in NOTICE.md / Licenses). Bencho's comments are kept as
// written (marked "Bencho:"); ours say what changed for Ghost Mosaic.
#include "HeatWord.h"
#include "Look.h"

namespace mosaic
{
/* Bencho:
   ══ Heat word ════════════════════════════════════════════
   A big word in flat, dull grey — until the hand comes. Around
   the cursor the word turns into a heat map of its own shape:
   deep navy in the middle of every stroke, through blue, cyan,
   yellow, orange and red, to a pink rim. Right under the pointer
   the heat opens up and the strokes thin, like something melting;
   leave and it fades back to grey.

   ── A FIELD, THEN A PALETTE ─────────────────────────────
   The word is drawn once, blurred, into a grey field: bright in
   the thick of a stroke, fading at its edge. The cursor adds a
   soft round of heat to that field. Every pixel's brightness is
   then looked up in a 256-entry table of colours. Two strokes
   close together overlap in the field, so they fuse where the
   heat pools — that is the liquid, and nothing animates it but
   the field.

   ── THE BLUR IS A SHADOW ────────────────────────────────
   ctx.filter is not in every Safari, shadowBlur is. The word is
   drawn far off the canvas with its shadow thrown back on, so
   only the blurred shadow lands. */
// Ghost Mosaic: JUCE has neither, so the field is blurred here with three box passes (close
// to a gaussian). The word is ours, "GHOST MOSAIC", in Poppins Bold. Our palette ("Ghost")
// keeps the shape of Bencho's Neon - a light rim, a bright band, a dark core - in the GhostBlock
// colours. A press does not step to another shape: this is the name on the plug-in. While the
// engine plays, a second, softer heat follows the target's playhead along the word.

static float clampf (float v, float a, float b) { return juce::jmin (b, juce::jmax (a, v)); }

namespace heat
{
/* Bencho: the palettes, rim (0) to core (1); below the floor it is the card. */
static const std::array<std::pair<float, std::array<int, 3>>, 9> kGhost { {
    { 0.0f, { 232, 255, 196 } },
    { 0.1f, { 196, 250, 110 } },
    { 0.2f, { 166, 242, 58 } },     // acid green
    { 0.32f, { 236, 255, 170 } },
    { 0.42f, { 246, 240, 255 } },   // the white band
    { 0.54f, { 206, 170, 255 } },
    { 0.68f, { 166, 107, 255 } },   // electric purple
    { 0.84f, { 88, 40, 190 } },
    { 1.0f, { 30, 12, 72 } },
} };
static constexpr int FLOOR = 34;

const juce::uint8* lut()
{
    static const auto t = []
    {
        std::array<juce::uint8, 256 * 4> t {};
        for (int i = 0; i < 256; ++i)
        {
            if (i < FLOOR) { t[(size_t) i * 4 + 3] = 0; continue; }   /* Bencho: under the floor: nothing, so the card shows through */
            const float u = (float) (i - FLOOR) / (float) (255 - FLOOR);
            size_t k = 0;
            while (k < kGhost.size() - 2 && u > kGhost[k + 1].first) ++k;
            const auto& [u0, c0] = kGhost[k];
            const auto& [u1, c1] = kGhost[k + 1];
            const float f = clampf ((u - u0) / (u1 - u0), 0, 1);
            for (int c = 0; c < 3; ++c) t[(size_t) i * 4 + (size_t) c] = (juce::uint8) juce::roundToInt ((float) c0[(size_t) c] + (float) (c1[(size_t) c] - c0[(size_t) c]) * f);
            /* Bencho: the rim fades in over the first stretch, so the pink is a
               glow at the edge rather than a hard line */
            t[(size_t) i * 4 + 3] = (juce::uint8) juce::roundToInt (255 * clampf ((float) (i - FLOOR) / 26.0f, 0, 1));
        }
        return t;
    }();
    return t.data();
}

/* Bencho: ── and at rest, just grey ────────────────────────────────
   The word sits on the card as a flat, dull grey — solid where
   the heat would be, soft at the edge — and the heat map is only
   ever shown where the hand is. GREY_A is the grey's alpha per
   brightness: the letter's body at full, a short soft edge. */
const juce::uint8* greyAlpha()
{
    static const auto a = []
    {
        std::array<juce::uint8, 256> a {};
        for (int i = 0; i < 256; ++i) a[(size_t) i] = (juce::uint8) juce::roundToInt (255 * clampf ((float) (i - 70) / 45.0f, 0, 1));
        return a;
    }();
    return a.data();
}
}

HeatWord::HeatWord (juce::String word) : m_word (std::move (word))
{
    setOpaque (false);
    setInterceptsMouseClicks (true, false);
}

// Bencho: the shape grows in from nothing when it changes. Ghost Mosaic: the word never changes, only its size does
// (the window being resized), so it grows in once, when it first appears, and stays drawn through every resize.
// (Before, every change of its width started the 0.9 s grow again from nothing: the name blinked out, came back
//  only after a quarter of a second, and stayed gone for as long as the window's edge was being dragged.)
void HeatWord::resized()
{
    if (! m_built) m_grow = 0;
    m_built = true;
    build();
    run();
}

// box blur, one direction, radius r, on a w*h byte field (in place via a scratch row)
static void boxPass (std::vector<float>& f, int w, int h, int r, bool horizontal)
{
    const int n = horizontal ? w : h, lines = horizontal ? h : w;
    std::vector<float> row ((size_t) n);
    for (int l = 0; l < lines; ++l)
    {
        auto at = [&] (int i) -> float& { return horizontal ? f[(size_t) l * (size_t) w + (size_t) i] : f[(size_t) i * (size_t) w + (size_t) l]; };
        float acc = 0;
        for (int i = -r; i <= r; ++i) acc += at (juce::jlimit (0, n - 1, i));
        for (int i = 0; i < n; ++i)
        {
            row[(size_t) i] = acc / (float) (2 * r + 1);
            acc += at (juce::jlimit (0, n - 1, i + r + 1)) - at (juce::jlimit (0, n - 1, i - r));
        }
        for (int i = 0; i < n; ++i) at (i) = row[(size_t) i];
    }
}

/* Bencho: the word, blurred, into the base field — redrawn when the
   word or the softness changes */
void HeatWord::build()
{
    const float scale = 2.0f;   // the field at twice the component's size (Bencho: devicePixelRatio, up to 2)
    m_w = juce::jmax (8, (int) (getWidth() * scale));
    m_h = juce::jmax (8, (int) (getHeight() * scale));
    juce::Image img (juce::Image::SingleChannel, m_w, m_h, true);
    {
        juce::Graphics g (img);
        g.setColour (juce::Colours::white);
        /* Bencho: as large as fits */
        // Ghost Mosaic (build 18): fitted by the letters' real ink, not the advance widths, so the word sits
        // exactly between the insets (equal room left and right) and in the middle of the strip
        const float inset = m_inset * scale;
        const float availW = juce::jmax (8.0f, (float) m_w - 2 * inset - (m_inset > 0 ? 0.0f : (float) m_w * 0.06f));
        const float availH = (float) m_h * 0.80f;
        auto arrange = [&] (float size)
        {
            juce::GlyphArrangement ga;
            ga.addLineOfText (look::font (size, 2).withExtraKerningFactor (0.04f), m_word, 0, 0);
            return ga;
        };
        float size = (float) m_h * 0.96f;
        auto ga = arrange (size);
        for (int pass = 0; pass < 2; ++pass)   // (kerning and hinting are not quite linear: measure twice)
        {
            const auto ink = ga.getBoundingBox (0, -1, false);
            const float fit = juce::jmin (availW / juce::jmax (1.0f, ink.getWidth()), availH / juce::jmax (1.0f, ink.getHeight()));
            size *= fit;
            ga = arrange (size);
        }
        const auto ink = ga.getBoundingBox (0, -1, false);
        const float dx = ((float) m_w - ink.getWidth()) * 0.5f - ink.getX();
        const float dy = ((float) m_h - ink.getHeight()) * 0.5f - ink.getY();
        ga.moveRangeOfGlyphs (0, -1, dx, dy);
        ga.draw (g);
        m_ink = ink.translated (dx, dy) / scale;
    }
    // Bencho's softness 50 is a 14 px blur on a 150 px word. Ours is two words in a long, low strip, so
    // the blur follows the letters' size instead (about 3% of it), in three box passes (close to a gaussian)
    std::vector<float> f ((size_t) m_w * (size_t) m_h);
    {
        juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < m_h; ++y) for (int x = 0; x < m_w; ++x) f[(size_t) y * (size_t) m_w + (size_t) x] = (float) bd.getLinePointer (y)[x * bd.pixelStride];
    }
    const int r = juce::jmax (1, (int) std::lround (m_h * 0.96f * 0.032f));
    for (int k = 0; k < 3; ++k) { boxPass (f, m_w, m_h, r, true); boxPass (f, m_w, m_h, r, false); }
    m_base.resize (f.size());
    /* Bencho: twice over, so the thick of a stroke saturates */
    for (size_t i = 0; i < f.size(); ++i) m_base[i] = (juce::uint8) juce::jlimit (0, 255, (int) (f[i] * 1.9f));
    m_shown = juce::Image (juce::Image::ARGB, m_w, m_h, true);
    paintField();
}

/* Bencho: one frame: base, plus heat at the cursor, through the palette */
void HeatWord::paintField()
{
    if (m_base.empty() || ! m_shown.isValid()) return;
    const auto* LUT = heat::lut();
    const auto* GA = heat::greyAlpha();
    const float w = (float) m_w, h = (float) m_h;
    // two hands: the pointer, and the engine (softer, at the playhead)
    struct Hand { float cx, cy, on, coolR, revealR, amt; };
    Hand hands[2] = {
        { m_px * w, m_py * h, m_power, (60 + 0.6f * 50) * 2.0f, (95 + 0.6f * 55) * 2.0f, m_power * (0.35f + 0.6f * 0.55f) },
        // the music's hand: as present as the pointer's, and it breathes with the level (a bigger, hotter round on the loud bits)
        { m_ax * w, m_ay * h, m_audioPower, (58 + 34 * m_pulse) * 2.0f, (92 + 58 * m_pulse) * 2.0f, m_audioPower * (0.45f + 0.3f * m_pulse) },
    };
    juce::Image::BitmapData out (m_shown, juce::Image::BitmapData::writeOnly);
    for (int y = 0; y < m_h; ++y)
    {
        auto* line = reinterpret_cast<juce::PixelARGB*> (out.getLinePointer (y));
        for (int x = 0; x < m_w; ++x)
        {
            float fv = (float) m_base[(size_t) y * (size_t) m_w + (size_t) x] * m_grow;
            /* Bencho: the shape grows in from nothing when it changes */
            float m = 0;
            for (auto& hd : hands)
            {
                if (hd.on <= 0.002f) continue;
                const float dx = (float) x - hd.cx, dy = (float) y - hd.cy;
                const float d2 = dx * dx + dy * dy;
                /* Bencho: the cursor COOLS the field: a multiply that dims it toward
                   the middle of the pointer, so the navy cores open up into
                   rings of blue, cyan, yellow and red and the strokes thin
                   there — the letters melting under the hand */
                const float dc = std::sqrt (d2) / hd.coolR;
                if (dc < 1)
                {
                    const float k = dc < 0.45f ? 1 - (1 - 0.6f) * dc / 0.45f : 0.6f * (1 - (dc - 0.45f) / 0.55f);
                    fv *= 1 - hd.amt * k;
                }
                /* Bencho: the reveal: how much heat map each pixel shows, a soft disc
                   around the cursor scaled by how present the hand is. Outside
                   it the word is grey. */
                const float q = d2 / (hd.revealR * hd.revealR);
                if (q < 1) { const float t = 1 - q; m = juce::jmax (m, hd.on * t * t * (3 - 2 * t)); }
            }
            const int f0 = juce::jlimit (0, 255, (int) ((float) m_base[(size_t) y * (size_t) m_w + (size_t) x] * m_grow));
            const int f = juce::jlimit (0, 255, (int) fv);
            const float ga = (float) GA[f0];
            float r, g, b, a;
            if (m <= 0) { r = heat::kGrey[0]; g = heat::kGrey[1]; b = heat::kGrey[2]; a = ga; }
            else
            {
                const int v = f * 4;
                const float k = 1 - m;
                r = heat::kGrey[0] * k + LUT[v] * m;
                g = heat::kGrey[1] * k + LUT[v + 1] * m;
                b = heat::kGrey[2] * k + LUT[v + 2] * m;
                a = ga * k + LUT[v + 3] * m;
            }
            const float af = a / 255.0f;
            line[x].setARGB ((juce::uint8) a, (juce::uint8) (r * af), (juce::uint8) (g * af), (juce::uint8) (b * af));   // premultiplied
        }
    }
}

void HeatWord::paint (juce::Graphics& g)
{
    if (! m_shown.isValid()) return;
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.drawImage (m_shown, getLocalBounds().toFloat());
}

void HeatWord::mouseEnter (const juce::MouseEvent& e)
{
    m_tx = e.position.x / (float) juce::jmax (1, getWidth()); m_ty = e.position.y / (float) juce::jmax (1, getHeight());
    m_px = m_tx; m_py = m_ty; m_want = 1; run();
}
void HeatWord::mouseMove (const juce::MouseEvent& e)
{
    m_tx = e.position.x / (float) juce::jmax (1, getWidth()); m_ty = e.position.y / (float) juce::jmax (1, getHeight());
    m_want = 1; run();
}
void HeatWord::mouseExit (const juce::MouseEvent&) { m_want = 0; run(); }

void HeatWord::setAudioHeat (float where, float level)
{
    m_roam = where < 0;
    if (! m_roam) m_audioX = clampf (where, 0, 1);
    m_audioLevel = clampf (level, 0, 1);
    if (m_audioLevel > 0.002f || m_audioPower > 0.002f) run();
}

/* Bencho: the loop runs only while something is moving */
void HeatWord::run()
{
    if (m_manual) { m_moving = true; return; }
    if (! isTimerRunning()) { m_prev = juce::Time::getMillisecondCounterHiRes(); startTimerHz (60); }
}

void HeatWord::setManualClock (bool on)
{
    if (on == m_manual) return;
    m_manual = on;
    if (on) { stopTimer(); m_moving = true; }
    else run();
}

void HeatWord::advance (float dt) { if (m_manual && m_moving) m_moving = step (juce::jmin (0.05f, dt)); }

void HeatWord::timerCallback()
{
    const double t = juce::Time::getMillisecondCounterHiRes();
    const float dt = (float) juce::jmin (0.05, (t - m_prev) / 1000.0);
    m_prev = t;
    if (! step (dt)) stopTimer();
}

bool HeatWord::step (float dt)
{
    const float k = 1 - std::pow (0.002f, dt);
    m_px += (m_tx - m_px) * k;
    m_py += (m_ty - m_py) * k;
    m_power += (m_want - m_power) * (1 - std::pow (0.01f, dt));
    // the engine's heat: glides along with the playhead (the word spans the whole target); with no playhead to
    // follow (the song playing, the track coming in) it roams the whole word on its own, end to end and back,
    // drifting a little up and down like the hand does
    const float awant = m_audioLevel > 0.002f ? juce::jmin (1.0f, 0.55f + m_audioLevel * 1.2f) : 0.0f;
    float aw = 0.08f + 0.84f * m_audioX;
    float ay = 0.5f;
    if (m_roam)
    {
        if (awant > 0) m_roamPhase += dt * juce::MathConstants<float>::twoPi / 5.5f;   // one trip across and back in 5.5 s
        aw = 0.5f - 0.44f * std::cos (m_roamPhase);
        ay = 0.5f + 0.16f * std::sin (m_roamPhase * 2.3f);
    }
    m_ax += (aw - m_ax) * (1 - std::pow (m_roam ? 0.0005f : 0.02f, dt));
    m_ay += (ay - m_ay) * (1 - std::pow (0.02f, dt));
    m_audioPower += (awant - m_audioPower) * (1 - std::pow (0.05f, dt));
    // the pulse: up fast on a hit, down slower, so the round swells with the beat
    const float pw = clampf (m_audioLevel * 1.4f, 0, 1);
    m_pulse += (pw - m_pulse) * (1 - std::pow (pw > m_pulse ? 0.000001f : 0.02f, dt));
    m_grow = juce::jmin (1.0f, m_grow + dt / 0.9f);
    paintField();
    repaint();
    const bool still = m_grow >= 1 && std::abs (m_want - m_power) < 0.004f && std::abs (m_tx - m_px) < 0.001f && std::abs (m_ty - m_py) < 0.001f
                       && std::abs (awant - m_audioPower) < 0.004f && (awant == 0 || (! m_roam && std::abs (aw - m_ax) < 0.001f && m_pulse < 0.004f));
    if (still && m_want == 0) m_power = 0;
    if (still && awant == 0) m_audioPower = 0;
    if (still) { paintField(); repaint(); }
    return ! still;
}
}
