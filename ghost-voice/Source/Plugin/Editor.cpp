// Ghost Voice - AGPL-3.0-only (see LICENSE, NOTICE.md).
//
// THE GHOST CHOIR (design studio round 1): the whole window as a cathedral of spectral singers. The header is the rood screen: the
// GHOST VOICE mark in its reserved place and seven hymn-board tabs (lancet plaques numbered I..VII like the hymns on a board).
// The left list is the side chapels, the bottom strip the choir's front row (voice A, B and C as hooded singers) and a ribbon with
// the status. Every page is a set of hymn-book pages (gilded double rules), voices are hooded singers who sing while they play,
// meters are organ pipes, switches are candles. Static art is painted once into an Image per page / size / look (chrome).
#include "Editor.h"
#include "ChopPage.h"
#include "ChoirFonts.h"
#include "BubbleFx.h"
#include "TerrainView.h"
#include "UnderTheHood.h"
#include "LogoData.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>

namespace gv
{
namespace th = ghosty::themes;

// ================================================================================================ the fonts (Crimson Pro, Gloock: OFL)
namespace choir
{
namespace
{
// The four faces are made inside the constructor of ONE static object, on purpose. On macOS, JUCE's memory typefaces register
// with a function-local static registry (a std::mutex + set, made by the first createSystemTypefaceFor) and unregister in their
// destructor. Statics are destroyed in reverse order of construction COMPLETING, so the registry must finish being built before
// our holder does: building the faces in the holder's constructor guarantees that. (A plain `static Typeface::Ptr f[4]` filled
// lazily finished first, so at exit the registry died first and ~CoreTextTypeface locked a destroyed mutex -> libc++abi abort.)
struct Faces
{
    juce::Typeface::Ptr f[4];
    Faces()
    {
        f[0] = juce::Typeface::createSystemTypefaceFor (fontdata::crimsonRegular, sizeof (fontdata::crimsonRegular));
        f[1] = juce::Typeface::createSystemTypefaceFor (fontdata::crimsonBold, sizeof (fontdata::crimsonBold));
        f[2] = juce::Typeface::createSystemTypefaceFor (fontdata::crimsonItalic, sizeof (fontdata::crimsonItalic));
        f[3] = juce::Typeface::createSystemTypefaceFor (fontdata::gloock, sizeof (fontdata::gloock));
    }
};
juce::Typeface::Ptr face (int which)
{
    static const Faces faces;
    return faces.f[juce::jlimit (0, 3, which)];
}
}
// (round 3) small italics were hard to read: under 15 px every "italic" line is set upright (Crimson Pro regular)
juce::Font serif (float h, int style) { if (style == italic && h < 15.0f) style = regular; return juce::Font (juce::FontOptions (face (juce::jlimit (0, 2, style))).withHeight (h)); }
juce::Font display (float h) { return juce::Font (juce::FontOptions (face (3)).withHeight (h)); }
}

using choir::serif; using choir::display;
using Candle = choir::Candle;

// ================================================================================================ small pieces
namespace
{
juce::String mmss (double s) { const int t = juce::jmax (0, (int) std::round (s)); return juce::String (t / 60) + ":" + juce::String (t % 60).paddedLeft ('0', 2); }
const char* const kNumerals[] { "I", "II", "III", "IV", "V", "VI", "VII", "VIII" };

// (round 3) "sung in 42.4947 s" -> "sung in 42.5 s": a number shown in words never carries more than two decimals
juce::String tidyNumbers (const juce::String& t)
{
    juce::String out; int i = 0; const int n = t.length();
    while (i < n)
    {
        if (juce::CharacterFunctions::isDigit (t[i]))
        {
            int j = i; while (j < n && juce::CharacterFunctions::isDigit (t[j])) ++j;
            if (j + 1 < n && t[j] == '.' && juce::CharacterFunctions::isDigit (t[j + 1]))
            {
                int k = j + 1; while (k < n && juce::CharacterFunctions::isDigit (t[k])) ++k;
                if (k - j - 1 > 2) { out << juce::String (t.substring (i, k).getDoubleValue(), 1); i = k; continue; }
                out << t.substring (i, k); i = k; continue;
            }
            out << t.substring (i, j); i = j; continue;
        }
        out << t[i]; ++i;
    }
    return out;
}
// (round 3) a line of "a  -  b  -  c" parts set on up to `maxLines` lines without ever breaking inside a part
void drawParts (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, const juce::Font& f, int maxLines, juce::Justification j = juce::Justification::centredLeft)
{
    juce::StringArray parts;
    for (auto rest = text;;) { const int at = rest.indexOf (" - "); if (at < 0) { parts.add (rest); break; } parts.add (rest.substring (0, at)); rest = rest.substring (at + 3); }
    parts.trim(); parts.removeEmptyStrings();
    juce::StringArray lines; juce::String cur;
    for (auto& p : parts)
    {
        const auto tryLine = cur.isEmpty() ? p : cur + "  -  " + p;
        if (cur.isNotEmpty() && choir::width (f, tryLine) > r.getWidth()) { lines.add (cur); cur = p; }
        else cur = tryLine;
    }
    if (cur.isNotEmpty()) lines.add (cur);
    if (lines.size() > maxLines) { g.setFont (f); g.drawFittedText (text, r.toNearestInt(), j, maxLines, 0.8f); return; }
    if (lines.size() == 2 && parts.size() > 2)
    {   // (polish 1) two lines of even length: never one lone part on a line of its own
        int best = 1; float bestW = 1.0e9f;
        for (int k = 1; k < parts.size(); ++k)
        {
            juce::StringArray a, b; for (int i = 0; i < parts.size(); ++i) (i < k ? a : b).add (parts[i]);
            const float w = juce::jmax (choir::width (f, a.joinIntoString ("  -  ")), choir::width (f, b.joinIntoString ("  -  ")));
            if (w <= r.getWidth() && w < bestW) { bestW = w; best = k; }
        }
        juce::StringArray a, b; for (int i = 0; i < parts.size(); ++i) (i < best ? a : b).add (parts[i]);
        lines = { a.joinIntoString ("  -  "), b.joinIntoString ("  -  ") };
    }
    const float lh = f.getHeight() * 1.15f;
    g.setFont (f);
    for (int i = 0; i < lines.size(); ++i) g.drawText (lines[i], r.withY (r.getY() + lh * (float) i).withHeight (lh), j, false);
}

// a file's waveform peaks and long-term spectrum (computed once per file)
struct Analysis
{
    juce::String key; std::vector<float> peaks, spectrumDb; double seconds = 0;
    float level = 0;   // (polish 3) the file's real peak level (the peaks are kept normalised for the A / B bars)
    static constexpr int kPeaks = 220, kBands = 72;
    static float bandHz (int i) { return 60.0f * std::pow (12000.0f / 60.0f, (float) i / (float) (kBands - 1)); }
    // streamed in 64k-sample blocks (build 3): drawing a card never holds the whole file in memory, however long it is
    void set (const juce::File& f)
    {
        const auto k = f.getFullPathName() + juce::String (f.getLastModificationTime().toMilliseconds());
        if (k == key) return;
        key = k; peaks.clear(); spectrumDb.clear(); seconds = 0; level = 0;
        if (! f.existsAsFile()) return;
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
        if (rd == nullptr || rd->sampleRate <= 0 || rd->lengthInSamples <= 0) return;
        const auto len = rd->lengthInSamples; const int fs = (int) rd->sampleRate, nch = (int) juce::jmax (1u, rd->numChannels);
        seconds = (double) len / fs;
        peaks.assign (kPeaks, 0.0f);
        constexpr int order = 11, N = 1 << order, kBlock = 65536;
        juce::dsp::FFT fft (order);
        std::vector<float> buf (2 * N), acc (N / 2 + 1, 0.0f), ring;
        int frames = 0;
        juce::AudioBuffer<float> b (nch, kBlock);
        for (juce::int64 pos = 0; pos < len; pos += kBlock)
        {
            const int n = (int) std::min<juce::int64> ((juce::int64) kBlock, len - pos);
            rd->read (&b, 0, n, pos, true, true);
            for (int i = 0; i < n; ++i)
            {
                float v = 0; for (int c = 0; c < nch; ++c) v += b.getSample (c, i); v /= (float) nch;
                auto& p = peaks[(size_t) (((pos + i) * kPeaks) / len)]; p = juce::jmax (p, std::abs (v));
                if (frames < 600) ring.push_back (v);
            }
            while (frames < 600 && ring.size() >= (size_t) N)
            {
                std::fill (buf.begin(), buf.end(), 0.0f);
                for (int i = 0; i < N; ++i) buf[(size_t) i] = ring[(size_t) i] * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N));
                fft.performFrequencyOnlyForwardTransform (buf.data(), true);
                for (int i = 0; i <= N / 2; ++i) acc[(size_t) i] += buf[(size_t) i] * buf[(size_t) i];
                ring.erase (ring.begin(), ring.begin() + N / 2); ++frames;
            }
            if (frames >= 600) ring.clear();
        }
        const float mx = *std::max_element (peaks.begin(), peaks.end());
        level = juce::jmin (1.0f, mx);
        if (mx > 0) for (auto& p : peaks) p /= mx;
        if (frames == 0) return;
        spectrumDb.assign (kBands, -90.0f);
        float top = -200;
        for (int bnd = 0; bnd < kBands; ++bnd)
        {
            const float lo = bandHz (bnd) / std::pow (12000.0f / 60.0f, 0.5f / (kBands - 1)), hi = bandHz (bnd) * std::pow (12000.0f / 60.0f, 0.5f / (kBands - 1));
            const int i0 = juce::jlimit (1, N / 2, (int) (lo * N / fs)), i1 = juce::jlimit (i0, N / 2, (int) (hi * N / fs));
            double e = 0; for (int i = i0; i <= i1; ++i) e += acc[(size_t) i];
            spectrumDb[(size_t) bnd] = (float) (10.0 * std::log10 (e / (i1 - i0 + 1) / frames + 1e-20));
            top = juce::jmax (top, spectrumDb[(size_t) bnd]);
        }
        for (auto& d : spectrumDb) d -= top;
    }
    // the wave as one mirrored path of light (one fill: cheap)
    // (polish 1) the wave as a row of rounded bars, each bar the real peak of its stretch of the file (one path: cheap). A steady
    // tone reads as a steady row of bars, never as a solid slab that looks broken
    void paintWave (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) const
    {
        if (peaks.empty()) return;
        const float mid = r.getCentreY(), h = r.getHeight() * 0.5f * 0.92f;
        const int bars = juce::jmax (8, (int) (r.getWidth() / 4.0f));
        const float bw = r.getWidth() / (float) bars;
        juce::Path p;
        for (int b = 0; b < bars; ++b)
        {
            const size_t i0 = (size_t) ((float) b / (float) bars * (float) peaks.size()), i1 = juce::jmax (i0 + 1, (size_t) ((float) (b + 1) / (float) bars * (float) peaks.size()));
            float pk = 0; for (size_t i = i0; i < juce::jmin (i1, peaks.size()); ++i) pk = juce::jmax (pk, peaks[i]);
            const float hh = juce::jmax (1.0f, pk * h);
            p.addRoundedRectangle (r.getX() + (float) b * bw + bw * 0.18f, mid - hh, bw * 0.64f, hh * 2.0f, juce::jmin (bw * 0.32f, hh));
        }
        juce::ColourGradient gr (c.brighter (0.45f), 0, mid - h, c.withMultipliedAlpha (0.85f), 0, mid, true);
        g.setGradientFill (gr); g.fillPath (p);
    }
    juce::String noteText() const { return juce::String (seconds, seconds < 10 ? 1 : 0) + " s preview"; }
    juce::Rectangle<float> envelopeArea (juce::Rectangle<float> r, double refSeconds, float noteSize) const
    {   // where the envelope is drawn (the playhead runs over this)
        if (seconds <= 0) return r;
        const float frac = refSeconds > seconds ? juce::jlimit (0.30f, 1.0f, (float) (seconds / refSeconds)) : 1.0f;
        const float noteW = noteSize > 0 ? choir::width (serif (noteSize, choir::italic), noteText()) + 14.0f : 0.0f;
        return r.withWidth (juce::jmin (r.getWidth() * frac, r.getWidth() - (frac < 1.0f ? noteW : 0.0f)));
    }
    // (polish 3) voice C's preview: its real envelope at its real level, on a time scale shared with the voices it is made of (a
    // short preview is drawn short, left-aligned like a timeline), with a tick every second and a note of its length.
    // `refSeconds` = the longer of voice A's and voice B's takes (0 = fill the box)
    void paintEnvelope (juce::Graphics& g, juce::Rectangle<float> r0, juce::Colour c, juce::Colour note, double refSeconds, float noteSize) const
    {
        if (peaks.empty() || seconds <= 0) return;
        auto r = r0;
        const float frac = refSeconds > seconds ? juce::jlimit (0.30f, 1.0f, (float) (seconds / refSeconds)) : 1.0f;
        const juce::String txt = noteText();
        const auto nf = serif (juce::jmax (8.0f, noteSize), choir::italic);
        const float noteW = choir::width (nf, txt) + 14.0f;
        if (frac >= 1.0f && noteSize > 0) r = r.withTrimmedTop (noteSize + 4.0f);   // a full-length preview: its note sits over it, not on it
        auto env = envelopeArea (r, refSeconds, noteSize);
        const float mid = env.getCentreY(), H = env.getHeight() * 0.5f * 0.92f;
        const int n = juce::jmax (16, (int) (env.getWidth() / 3.0f));
        std::vector<float> a ((size_t) n + 1);
        for (int i = 0; i <= n; ++i)
        {   // a gently smoothed envelope (the peak of each stretch, averaged with its neighbours)
            auto pk = [&] (int j) { j = juce::jlimit (0, n, j); const size_t k0 = (size_t) ((float) j / (float) (n + 1) * (float) peaks.size()), k1 = juce::jmax (k0 + 1, (size_t) ((float) (j + 1) / (float) (n + 1) * (float) peaks.size())); float m = 0; for (size_t k = k0; k < juce::jmin (k1, peaks.size()); ++k) m = juce::jmax (m, peaks[k]); return m; };
            a[(size_t) i] = juce::jmax (0.02f, (pk (i - 1) + 2.0f * pk (i) + pk (i + 1)) * 0.25f * std::sqrt (level));   // (its level on a gentle, ear-like scale)
        }
        a.front() = a.back() = 0.0f;   // the ends close on the centre line
        juce::Path p;
        for (int i = 0; i <= n; ++i) { const float x = env.getX() + env.getWidth() * (float) i / (float) n; if (i == 0) p.startNewSubPath (x, mid - a[0] * H); else p.lineTo (x, mid - a[(size_t) i] * H); }
        for (int i = n; i >= 0; --i) p.lineTo (env.getX() + env.getWidth() * (float) i / (float) n, mid + a[(size_t) i] * H);
        p.closeSubPath();
        juce::ColourGradient gr (c.withAlpha (0.75f), 0, mid - H, c.withAlpha (0.30f), 0, mid, true);
        g.setGradientFill (gr); g.fillPath (p);
        g.setColour (c.brighter (0.25f)); g.strokePath (p, juce::PathStrokeType (1.3f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (c.withAlpha (0.35f)); g.fillRect (env.getX(), mid - 0.5f, r.getWidth() * (frac < 1.0f ? 1.0f : 1.0f), 1.0f);   // the time line runs on
        for (int sec = 1; sec < (int) std::ceil (seconds); ++sec)
        {   // a tick every second
            const float x = env.getX() + env.getWidth() * (float) (sec / seconds);
            g.setColour (note.withAlpha (0.55f)); g.fillRect (x, mid + H * 0.92f - 4.0f, 1.0f, 4.0f);
        }
        if (noteSize <= 0) return;   // (the card shows the length in its own time readout)
        g.setColour (note); g.setFont (nf);
        if (frac < 1.0f) g.drawText (txt, juce::Rectangle<float> (env.getRight() + 8.0f, mid - noteSize - 2.0f, noteW, noteSize + 2.0f), juce::Justification::centredLeft, false);
        else g.drawText (txt, juce::Rectangle<float> (r.getX(), r.getY() - noteSize - 4.0f, r.getWidth(), noteSize + 2.0f), juce::Justification::centredRight, false);
    }
    void paintSpectrum (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool fill) const
    {
        if (spectrumDb.empty()) return;
        juce::Path p;
        for (int b = 0; b < kBands; ++b)
        {
            const float px = r.getX() + r.getWidth() * (float) b / (float) (kBands - 1);
            const float py = r.getY() + r.getHeight() * juce::jlimit (0.0f, 1.0f, -spectrumDb[(size_t) b] / 70.0f);
            if (b == 0) p.startNewSubPath (px, py); else p.lineTo (px, py);
        }
        if (fill) { juce::Path f = p; f.lineTo (r.getRight(), r.getBottom()); f.lineTo (r.getX(), r.getBottom()); f.closeSubPath(); g.setColour (c.withAlpha (0.2f)); g.fillPath (f); }
        g.setColour (c); g.strokePath (p, juce::PathStrokeType (1.8f));
    }
};

// a voice's singer (A sapphire, B ruby, C amethyst) in a box, singing when `open` > 0
void voiceSinger (juce::Graphics& g, const choir::Pal& P, juce::Rectangle<float> box, juce::Colour robe, float open = 0, float lit = 0, float alpha = 1.0f)
{
    const float h = juce::jmin (box.getHeight(), box.getWidth() * 1.4f), w = h / 1.4f;
    choir::singer (g, P, juce::Rectangle<float> (box.getCentreX() - w * 0.5f, box.getCentreY() - h * 0.5f, w, h), robe, open, lit, alpha);
}

// THE LOGO (round 3): the lettered GHOST VOICE lockup (singing ghost + brush letters + a sung waveform), drawn as SVG in
// Source/UI/logo and embedded as PNG (LogoData.h). Decoded once per look; the chrome cache draws it once per page / size.
const juce::Image& logoImage (bool dark)
{
    static juce::Image d, l;
    auto& im = dark ? d : l;
    if (! im.isValid())
        im = dark ? juce::ImageFileFormat::loadFrom (logodata::header_dark, sizeof (logodata::header_dark))
                  : juce::ImageFileFormat::loadFrom (logodata::header_light, sizeof (logodata::header_light));
    return im;
}
void logoMark (juce::Graphics& g, const choir::Pal& P, juce::Rectangle<float> r)
{
    const auto& im = logoImage (P.dark);
    if (! im.isValid()) return;
    const float sc = juce::jmin (r.getWidth() / (float) im.getWidth(), r.getHeight() / (float) im.getHeight());
    const auto dst = r.withSizeKeepingCentre ((float) im.getWidth() * sc, (float) im.getHeight() * sc);
    g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
    g.setOpacity (1.0f);
    g.drawImage (im, dst, juce::RectanglePlacement::centred);
}
// (polish 3) the two candles by the logo, drawn to be seen in both looks: a gilt dish, a wax stick with an edge, and a flame with a
// deep amber body, a pale core, an inked rim in LIGHT (cream would swallow a pale flame) and a warm halo in DARK
void headerCandle (juce::Graphics& g, const choir::Pal& P, juce::Rectangle<float> r, float flick)
{
    const float w = r.getWidth();
    const float stickH = r.getHeight() * 0.50f, stickW = w * 0.62f;
    auto stick = juce::Rectangle<float> (r.getCentreX() - stickW * 0.5f, r.getBottom() - stickH - 3.0f, stickW, stickH);
    // the halo
    const float fh = r.getHeight() - stickH - 7.0f, fy = stick.getY() - 2.0f;
    const float fx = r.getCentreX() + (flick - 0.5f) * w * 0.30f;
    {
        const auto halo = P.dark ? juce::Colour (0xffffb347) : juce::Colour (0xffe8892a);
        juce::ColourGradient hg (halo.withAlpha (P.dark ? 0.42f : 0.26f), r.getCentreX(), fy - fh * 0.45f, halo.withAlpha (0.0f), r.getCentreX() + w * 1.5f, fy - fh * 0.45f, true);
        g.setGradientFill (hg); g.fillEllipse (r.getCentreX() - w * 1.5f, fy - fh * 0.45f - w * 1.5f, w * 3.0f, w * 3.0f);
    }
    // the dish and the stick
    g.setColour (P.gold); g.fillRoundedRectangle (r.getCentreX() - w * 0.75f, r.getBottom() - 3.5f, w * 1.5f, 3.0f, 1.5f);
    juce::ColourGradient sg (P.dark ? juce::Colour (0xfff3ead6) : juce::Colour (0xfffffbf1), stick.getX(), 0, P.dark ? juce::Colour (0xffb9a98a) : juce::Colour (0xffd9cbb0), stick.getRight(), 0, false);
    g.setGradientFill (sg); g.fillRoundedRectangle (stick, 1.5f);
    if (! P.dark) { g.setColour (juce::Colour (0xff8f7a5a).withAlpha (0.85f)); g.drawRoundedRectangle (stick, 1.5f, 0.8f); }
    g.setColour (juce::Colour (0xff3a2a1a)); g.fillRect (r.getCentreX() - 0.6f, fy - 1.0f, 1.2f, 3.5f);   // the wick
    // the flame
    juce::Path f; f.startNewSubPath (r.getCentreX() - w * 0.36f, fy);
    f.quadraticTo (r.getCentreX() - w * 0.52f, fy - fh * 0.45f, fx, fy - fh);
    f.quadraticTo (r.getCentreX() + w * 0.52f, fy - fh * 0.45f, r.getCentreX() + w * 0.36f, fy);
    f.quadraticTo (r.getCentreX(), fy + w * 0.28f, r.getCentreX() - w * 0.36f, fy); f.closeSubPath();
    juce::ColourGradient fg (juce::Colour (0xffffe9a8), r.getCentreX(), fy, juce::Colour (P.dark ? 0xffff8a1e : 0xffe0661a), fx, fy - fh, false);
    fg.addColour (0.45, juce::Colour (P.dark ? 0xffffc04a : 0xfff5a623));
    g.setGradientFill (fg); g.fillPath (f);
    if (! P.dark) { g.setColour (juce::Colour (0xffa8480f).withAlpha (0.9f)); g.strokePath (f, juce::PathStrokeType (0.9f)); }
    // the pale core
    g.setColour (juce::Colours::white.withAlpha (P.dark ? 0.85f : 0.75f));
    g.fillEllipse (r.getCentreX() - w * 0.14f, fy - fh * 0.38f, w * 0.28f, fh * 0.32f);
}
} // namespace

// ================================================================================================ the voice cards (A, B, C): a singer in a niche
class VoiceCard : public juce::Component, public juce::FileDragAndDropTarget, public juce::SettableTooltipClient
{
public:
    // slot: slotA / slotB / slotPerf; -1 = voice C (its preview)
    VoiceCard (Editor& e, int s) : ed (e), slot (s)
    {
        for (auto* b : { &add, &rec }) { addAndMakeVisible (*b); b->setMouseCursor (juce::MouseCursor::PointingHandCursor); }
        add.setTooltip ("Add a recording (WAV / AIFF / MP3 / FLAC)" + juce::String (slot == slotPerf || slot < 0 ? "" : ". More takes = a steadier voice"));
        rec.setTooltip ("Record from the plug-in's input (press again to stop)");
        add.onClick = [this] { choose(); };
        rec.onClick = [this] { if (ed.proc.recordingSlot() == slot) ed.proc.stopRecording(); else ed.proc.startRecording (slot); };
        add.setVisible (slot >= 0); rec.setVisible (slot >= 0);
        setTooltip (slot < 0 ? "Voice C, the new voice: press its play button to hear the preview, drag the card out to get the WAV" : "Drop recordings here, or press ADD / REC");
    }
    juce::File file() const { return slot < 0 ? ed.proc.preview : (ed.proc.takes[slot].isEmpty() ? juce::File() : juce::File (ed.proc.takes[slot][0])); }
    juce::Colour tint() const { const auto& P = ed.cp(); return slot == slotA ? P.voiceA() : slot == slotB ? P.voiceB() : slot < 0 ? P.voiceC() : P.glass[3]; }
    void choose()
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose " + slotName (slot), juce::File(), "*.wav;*.aif;*.aiff;*.mp3;*.flac");
        juce::Component::SafePointer<VoiceCard> sp (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | (slot == slotPerf ? 0 : juce::FileBrowserComponent::canSelectMultipleItems),
                              [sp] (const juce::FileChooser& c) { if (sp == nullptr) return; juce::StringArray f; for (auto& r : c.getResults()) f.add (r.getFullPathName()); sp->ed.proc.addTakes (sp->slot, f); });
    }
    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        if (slot < 0) return false;
        for (auto& f : files) if (juce::File (f).hasFileExtension ("wav;aif;aiff;mp3;flac")) return true;
        return false;
    }
    void fileDragEnter (const juce::StringArray&, int, int) override { hover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { hover = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override
    {
        hover = false; juce::StringArray f;
        for (auto& x : files) if (juce::File (x).hasFileExtension ("wav;aif;aiff;mp3;flac")) f.add (x);
        ed.proc.addTakes (slot, f);
    }
    void update()
    {
        an.set (file());
        rec.setButtonText (recordingHere() ? "STOP" : "REC");
        repaint();
    }
    bool recordingHere() const { return slot >= 0 && ed.proc.recordingSlot() == slot; }   // (polish 1) voice C read "0.0 s": -1 == -1
    juce::Rectangle<float> singerBox() const { return { 8.0f, 8.0f, 54.0f, (float) getHeight() - 16.0f }; }
    juce::Rectangle<float> playButton() const { return { 70.0f, 42.0f, 30.0f, 30.0f }; }
    juce::Rectangle<float> closeButton() const { return { (float) getWidth() - 28.0f, 10.0f, 18.0f, 18.0f }; }
    juce::Rectangle<float> waveBox() const { return { 106.0f, 38.0f, (float) getWidth() - 116.0f, 40.0f }; }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasDraggedSinceMouseDown()) return;
        const auto f = file();
        if ((playButton().expanded (4).contains (e.position) || singerBox().contains (e.position)) && f.existsAsFile()) { if (ed.proc.isPlaying (f)) ed.proc.stopPlaying(); else ed.proc.play (f); }
        else if (slot >= 0 && closeButton().contains (e.position) && ! ed.proc.takes[slot].isEmpty()) ed.proc.clearSlot (slot);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {   // voice C's preview can be dragged out too
        if (slot >= 0 || dragging || e.getDistanceFromDragStart() < 6 || ! file().existsAsFile()) return;
        dragging = true;
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file().getFullPathName() }, false, this, [this] { dragging = false; });
    }
    juce::String fileLine() const
    {
        if (slot >= 0)
        {
            const auto& t = ed.proc.takes[slot];
            return t.isEmpty() ? "no recording yet" : (t.size() == 1 ? juce::File (t[0]).getFileName() : juce::File (t[0]).getFileName() + "  + " + juce::String (t.size() - 1) + " more");
        }
        if (ed.proc.preview.existsAsFile() && ed.proc.previewQuick)
            return "NATURAL instant preview  -  " + juce::String (juce::roundToInt (ed.proc.previewBalance * 100)) + " % B  -  NEURAL is still singing";
        if (ed.proc.preview.existsAsFile())
            return engineName (ed.proc.previewEngine) + "  -  " + juce::String (juce::roundToInt (ed.proc.previewBalance * 100)) + " % B"
                 + (ed.proc.previewEngine == Engine::seedvc || ed.proc.previewEngine == Engine::best ? "  -  novelty " + juce::String (juce::roundToInt (ed.proc.previewSettings.novelty * 100)) + " %" : juce::String())
                 + "   (drag it out)";
        return "not created yet";
    }
    void paintArt (juce::Graphics& g)
    {
        const auto& P = ed.cp();
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const bool isC = slot < 0;
        choir::page (g, P, r, hover ? tint() : isC && ed.proc.preview.existsAsFile() ? P.voiceC() : juce::Colour());
        // the niche and its singer
        auto nb = singerBox();
        auto lp = choir::lancet (nb, 0.34f);
        juce::ColourGradient ng (tint().withAlpha (P.dark ? 0.22f : 0.14f), nb.getX(), nb.getY(), P.panel.withAlpha (0.0f), nb.getX(), nb.getBottom(), false);
        g.setGradientFill (ng); g.fillPath (lp);
        g.setColour (P.edgeDim); g.strokePath (lp, juce::PathStrokeType (1.0f));
        voiceSinger (g, P, nb.reduced (6, 10).withTrimmedTop (6), tint(), 0.0f, 0.0f, file().existsAsFile() ? 1.0f : 0.5f);
        // the name and its role
        const juce::String name = slot == slotA ? "Voice A" : slot == slotB ? "Voice B" : slot < 0 ? ed.proc.voiceName : "Performance";
        const juce::String role = slot < 0 ? "the new identity" : slot == slotPerf ? "what is sung" : "reference voice";
        auto tl = juce::Rectangle<float> (70, 8, r.getWidth() - 70 - 34, 26);
        const auto nf = choir::fitted (choir::disp, name, tl.getWidth() * 0.62f, 19.0f, 12.0f);
        g.setColour (P.ink); g.setFont (nf); g.drawText (name, tl, juce::Justification::centredLeft, false);
        const float nw = choir::width (nf, name) + 8;
        g.setColour (P.dim); g.setFont (choir::fitted (choir::italic, role, tl.getWidth() - nw, 15.0f, 9.0f));
        g.drawText (role, tl.withTrimmedLeft (nw), juce::Justification::centredLeft, false);
        if (isC && ed.proc.previewId.valid)
        {   // measured: a gold seal with a tick
            auto c = closeButton().expanded (2);
            g.setColour (P.good); g.fillEllipse (c);
            juce::Path t; t.startNewSubPath (c.getX() + c.getWidth() * 0.28f, c.getCentreY()); t.lineTo (c.getX() + c.getWidth() * 0.45f, c.getBottom() - c.getHeight() * 0.3f); t.lineTo (c.getRight() - c.getWidth() * 0.25f, c.getY() + c.getHeight() * 0.3f);
            g.setColour (P.panel); g.strokePath (t, juce::PathStrokeType (2.0f));
        }
        else if (! isC && ! ed.proc.takes[slot].isEmpty())
        {
            auto c = closeButton().reduced (4);
            g.setColour (P.dim); g.drawLine (c.getX(), c.getY(), c.getRight(), c.getBottom(), 1.6f); g.drawLine (c.getRight(), c.getY(), c.getX(), c.getBottom(), 1.6f);
        }
        // the wave strip
        auto ws = waveBox();
        g.setColour (P.waveBg); g.fillRoundedRectangle (ws, 4); g.setColour (P.edgeDim); g.drawRoundedRectangle (ws, 4, 1.0f);
        const auto f = file();
        auto pb = playButton();
        g.setColour (f.existsAsFile() ? P.gold : P.cell); g.fillEllipse (pb);
        g.setColour (f.existsAsFile() ? P.onGold : P.faint);
        juce::Path tri; tri.addTriangle (pb.getCentreX() - 4, pb.getCentreY() - 7, pb.getCentreX() - 4, pb.getCentreY() + 7, pb.getCentreX() + 7, pb.getCentreY()); g.fillPath (tri);
        auto wa = ws.reduced (8, 5);
        if (! an.peaks.empty() && slot < 0) an.paintEnvelope (g, wa.withTrimmedRight (78), tint(), P.dim, juce::jmax (ed.proc.takeSeconds (slotA), ed.proc.takeSeconds (slotB)), 0.0f);
        else if (! an.peaks.empty()) an.paintWave (g, wa.withTrimmedRight (78), tint());
        else
        {
            g.setColour (P.dim); g.setFont (serif (15.0f, choir::italic));
            const juce::String hint = slot < 0 ? "press CREATE NEW VOICE C" : recordingHere() ? "recording..." : "drop a recording here";
            g.drawText (hint, wa, juce::Justification::centred, false);
        }
        // the file line
        g.setColour (P.dim);
        const auto line = fileLine();
        auto lr = juce::Rectangle<float> (70, r.getBottom() - 31, r.getWidth() - 70 - (slot >= 0 ? 140 : 12), 24);
        g.setFont (choir::fitted (choir::regular, line, lr.getWidth(), 14.5f, 9.5f)); g.drawText (line, lr, juce::Justification::centredLeft, false);
    }
    void paint (juce::Graphics& g) override
    {
        const auto& P = ed.cp();
        const auto f = file();
        juce::String k; k << (int) P.dark << an.key << "|" << getWidth() << "x" << getHeight() << "|" << (int) hover << "|" << (int) recordingHere() << "|" << fileLine() << ed.proc.voiceName
                         << (int) ed.proc.previewId.valid << (slot >= 0 ? ed.proc.takes[slot].size() : 0)
                         << "|" << (slot < 0 ? juce::jmax (ed.proc.takeSeconds (slotA), ed.proc.takeSeconds (slotB)) : 0.0);
        art.draw (g, k, getLocalBounds(), [this] (juce::Graphics& ig, juce::Rectangle<int>) { paintArt (ig); });
        const bool playing = f.existsAsFile() && ed.proc.isPlaying (f);
        auto ws = waveBox();
        // (polish 1) an empty player shows only its hint (painted in the art), never a "0:00 / 0:00" on top of it
        if (recordingHere() || ! an.peaks.empty())
        {
            g.setColour (P.dim); g.setFont (serif (13.0f, choir::bold));
            g.drawText (recordingHere() ? juce::String (ed.proc.recordedSeconds(), 1) + " s" : mmss (playing ? an.seconds * ed.proc.playPosition() : 0) + " / " + mmss (an.seconds),
                        ws.reduced (6, 3).removeFromRight (76), juce::Justification::centredRight, false);
        }
        if (playing)
        {   // it sings: the singer's mouth opens, the play button shows pause, the playhead runs
            auto nb = singerBox();
            g.setColour (P.panel); g.fillRect (nb.reduced (6, 10).withTrimmedTop (6).expanded (6, 6).withTop (nb.getY() + 4).withBottom (nb.getBottom() - 4));
            juce::ColourGradient ng (tint().withAlpha (P.dark ? 0.30f : 0.18f), nb.getX(), nb.getY(), P.panel.withAlpha (0.0f), nb.getX(), nb.getBottom(), false);
            g.setGradientFill (ng); g.fillPath (choir::lancet (nb, 0.34f));
            g.setColour (P.edgeDim); g.strokePath (choir::lancet (nb, 0.34f), juce::PathStrokeType (1.0f));
            voiceSinger (g, P, nb.reduced (6, 10).withTrimmedTop (6), tint(), 0.6f + 0.4f * std::sin (ed.clock * 9.0f), 1.0f);
            auto pb = playButton();
            g.setColour (P.gold); g.fillEllipse (pb); g.setColour (P.onGold);
            g.fillRect (pb.getCentreX() - 5, pb.getCentreY() - 6, 3.5f, 12.0f); g.fillRect (pb.getCentreX() + 1.5f, pb.getCentreY() - 6, 3.5f, 12.0f);
            auto wa = an.envelopeArea (ws.reduced (8, 5).withTrimmedRight (78), juce::jmax (ed.proc.takeSeconds (slotA), ed.proc.takeSeconds (slotB)), 0.0f);
            g.setColour (P.play); g.fillRect (wa.getX() + wa.getWidth() * ed.proc.playPosition() - 1.0f, ws.getY() + 2, 2.0f, ws.getHeight() - 4);
        }
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 8).removeFromBottom (26);
        rec.setBounds (r.removeFromRight (58)); r.removeFromRight (6); add.setBounds (r.removeFromRight (58));
        art.clear();
    }
    Editor& ed; const int slot;
    juce::TextButton add { "ADD" }, rec { "REC" };
    Analysis an; bool hover = false, dragging = false;
    choir::Cache art;
    std::unique_ptr<juce::FileChooser> chooser;
};

// ================================================================================================ tabs (the hymn board), the list, buttons
class TabButton : public juce::Button
{
public:
    TabButton (Editor& e, int pg, int num, juce::String t, juce::String s) : juce::Button (t), ed (e), page (pg), numeral (num), title (std::move (t)), sub (std::move (s))
    { setMouseCursor (juce::MouseCursor::PointingHandCursor); onClick = [this] { ed.showPage (page); }; setTooltip (title + ": " + sub); }
    juce::Rectangle<float> textRoom() const { return getLocalBounds().toFloat().reduced (14, 0); }   // (polish 1) clear of the arch's sides
    float sharedTitle = 17.0f, sharedSub = 14.0f;   // set by the Editor: the same for every tab
    float titleSize() const { return juce::jmin (sharedTitle, choir::fit (choir::disp, title, textRoom().getWidth(), 17.0f, 9.0f)); }
    float subSize() const { return juce::jmin (sharedSub, choir::fit (choir::italic, sub, textRoom().getWidth(), 14.0f, 8.5f)); }
    bool textFits() const
    {
        const float w = textRoom().getWidth();
        return choir::width (display (titleSize()), title) <= w + 0.5f && choir::width (serif (subSize(), choir::italic), sub) <= w + 0.5f;
    }
    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const auto& P = ed.cp();
        auto r = getLocalBounds().toFloat().reduced (1.0f, 1.0f);
        const bool on = ed.page() == page && ! ed.creditsShown();
        auto lp = choir::lancet (r, 0.30f);
        if (on)
        {   // lit glass: two jewel colours with lead lines across
            juce::ColourGradient gr (P.glass[4], r.getX(), r.getY(), P.glass[1], r.getRight(), r.getBottom(), false);
            gr.addColour (0.55, P.glass[0].interpolatedWith (P.glass[4], 0.5f));
            g.setGradientFill (gr); g.fillPath (lp);
            juce::Graphics::ScopedSaveState ss (g); g.reduceClipRegion (lp);
            g.setColour (P.lead.withAlpha (0.35f));
            for (float x = r.getX() + 14; x < r.getRight(); x += 26) g.drawLine (x, r.getY(), x - 22, r.getBottom(), 1.2f);
            g.setColour (juce::Colours::white.withAlpha (0.10f)); g.fillRect (r.withHeight (r.getHeight() * 0.42f));
        }
        else
        {
            juce::ColourGradient gr (over ? P.cell : P.card, 0, r.getY(), P.panel, 0, r.getBottom(), false);
            g.setGradientFill (gr); g.fillPath (lp);
        }
        g.setColour (on ? P.goldHi : over ? P.gold : P.edgeDim); g.strokePath (lp, juce::PathStrokeType (on ? 1.6f : 1.0f));
        auto t = textRoom();
        auto numRow = t.removeFromTop (22).withTrimmedTop (5);
        g.setColour (on ? P.goldHi : P.gold); g.setFont (display (12.0f)); g.drawText (kNumerals[numeral], numRow, juce::Justification::centred, false);
        // (polish 1) every tab's title and its line sit at the same heights, in the same sizes
        const float ts = sharedTitle, ss = sharedSub;
        auto tr = t.removeFromTop (21.0f);
        g.setColour (on ? juce::Colours::white : P.ink); g.setFont (display (ts)); g.drawText (title, tr, juce::Justification::centred, false);
        g.setColour (on ? juce::Colours::white.withAlpha (0.88f) : P.dim); g.setFont (serif (ss, choir::italic));
        g.drawText (sub, t.removeFromTop (18.0f), juce::Justification::centred, false);
    }
    Editor& ed; int page, numeral; juce::String title, sub;
};

class NavItem : public juce::Button
{
public:
    NavItem (Editor& e, int pg, int glassIndex, juce::String t, juce::String s = {}) : juce::Button (t), ed (e), page (pg), glass (glassIndex), title (std::move (t)), sub (std::move (s))
    { setMouseCursor (juce::MouseCursor::PointingHandCursor); onClick = [this] { if (onPick) onPick(); ed.showPage (page); }; }
    std::function<void()> onPick; std::function<bool()> isOn;
    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const auto& P = ed.cp();
        auto r = getLocalBounds().toFloat().reduced (8, 2);
        const bool on = ! ed.creditsShown() && (isOn ? isOn() : ed.page() == page);
        if (on) { g.setColour (P.glass[glass].withAlpha (P.dark ? 0.20f : 0.13f)); g.fillRoundedRectangle (r, 4); g.setColour (P.gold); g.fillRect (r.getX(), r.getY() + 4, 3.0f, r.getHeight() - 8); }
        else if (over) { g.setColour (P.cell.withAlpha (0.6f)); g.fillRoundedRectangle (r, 4); }
        // a little lancet window in the chapel's glass colour
        auto w = r.removeFromLeft (34).reduced (9, juce::jmax (4.0f, (r.getHeight() - 30) * 0.5f));
        auto lp = choir::lancet (w, 0.40f);
        g.setColour (P.glass[glass].withAlpha (on ? 0.95f : 0.55f)); g.fillPath (lp);
        g.setColour (on ? P.goldHi : P.edgeDim); g.strokePath (lp, juce::PathStrokeType (1.0f));
        r.removeFromLeft (4);
        g.setColour (on ? P.ink : P.ink.withAlpha (0.88f));
        if (sub.isEmpty()) { g.setFont (choir::fitted (choir::bold, title, r.getWidth(), 16.5f, 10.0f)); g.drawText (title, r, juce::Justification::centredLeft, false); }
        else
        {
            g.setFont (choir::fitted (choir::bold, title, r.getWidth(), 16.5f, 10.0f)); g.drawText (title, r.withTrimmedBottom (r.getHeight() * 0.48f), juce::Justification::bottomLeft, false);
            g.setColour (P.dim); g.setFont (choir::fitted (choir::italic, sub, r.getWidth(), 14.0f, 9.0f)); g.drawText (sub, r.withTrimmedTop (r.getHeight() * 0.54f), juce::Justification::topLeft, false);
        }
    }
    Editor& ed; int page, glass; juce::String title, sub;
};

// the big CREATE NEW VOICE C / SING IT button: a gilded processional plaque, its glass lit
class BigButton : public juce::Button
{
public:
    BigButton (Editor& e, juce::String t, juce::String s) : juce::Button (t), ed (e), title (std::move (t)), sub (std::move (s)) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paintBody (juce::Graphics& g, bool over, bool down)
    {
        const auto& P = ed.cp();
        auto r = getLocalBounds().toFloat().reduced (3);
        const bool en = isEnabled(); const float a = en ? 1.0f : 0.38f;
        g.setColour (P.shadow); g.fillRoundedRectangle (r.translated (0, 3), 8);
        if (! en)
        {   // (polish 1) greyed: a plain stone plaque (no lit glass), its words in greyed ink at 3:1 or more
            g.setColour (P.cell); g.fillRoundedRectangle (r, 8);
            g.setColour (P.faint); g.drawRoundedRectangle (r, 8, 1.5f);
            auto t = r.reduced (r.getHeight() * 0.5f + 16, 4);
            const auto ink = choir::greyInk (P, P.ink, choir::greyA (P), P.cell);
            g.setColour (ink); g.setFont (display (choir::fit (choir::disp, title, t.getWidth(), juce::jmin (28.0f, r.getHeight() * 0.42f), 12.0f)));
            g.drawText (title, t.withTrimmedBottom (t.getHeight() * 0.42f), juce::Justification::centredBottom, false);
            g.setFont (choir::fitted (choir::italic, sub, t.getWidth(), 16.0f, 9.0f));
            g.drawText (sub, t.withTrimmedTop (t.getHeight() * 0.6f), juce::Justification::centredTop, false);
            return;
        }
        juce::ColourGradient gr ((down ? P.glass[1] : P.glass[4]).withMultipliedAlpha (a), r.getX(), r.getY(), (over ? P.glass[0] : P.glass[1]).withMultipliedAlpha (a), r.getRight(), r.getBottom(), false);
        g.setGradientFill (gr); g.fillRoundedRectangle (r, 8);
        { juce::Graphics::ScopedSaveState ss (g); g.reduceClipRegion (r.toNearestInt());
          g.setColour (P.lead.withAlpha (0.28f * a)); for (float x = r.getX() + 40; x < r.getRight() + 60; x += 48) g.drawLine (x, r.getY(), x - 60, r.getBottom(), 1.4f);
          g.setColour (juce::Colours::white.withAlpha (0.12f * a)); g.fillRect (r.withHeight (r.getHeight() * 0.45f)); }
        g.setColour ((en ? (over ? P.goldHi : P.gold) : P.faint).withMultipliedAlpha (a)); g.drawRoundedRectangle (r, 8, 2.0f);
        g.setColour (P.goldHi.withAlpha (0.35f * a)); g.drawRoundedRectangle (r.reduced (4), 5, 0.8f);
        auto t = r.reduced (r.getHeight() * 0.5f + 16, 4);
        const float ts = choir::fit (choir::disp, title, t.getWidth(), juce::jmin (28.0f, r.getHeight() * 0.42f), 12.0f);
        g.setColour (juce::Colours::white.withAlpha (a)); g.setFont (display (ts));
        g.drawText (title, t.withTrimmedBottom (t.getHeight() * 0.42f), juce::Justification::centredBottom, false);
        g.setColour (juce::Colours::white.withAlpha (0.86f * a)); g.setFont (choir::fitted (choir::italic, sub, t.getWidth(), 16.0f, 9.0f));
        g.drawText (sub, t.withTrimmedTop (t.getHeight() * 0.6f), juce::Justification::centredTop, false);
    }
    choir::Cache art;
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto& P = ed.cp();
        juce::String k; k << (int) P.dark << (int) over << (int) down << (int) isEnabled() << getWidth() << "x" << getHeight() << title << sub;
        art.draw (g, k, getLocalBounds(), [this, over, down] (juce::Graphics& ig, juce::Rectangle<int>) { paintBody (ig, over, down); });
        auto r = getLocalBounds().toFloat().reduced (3);
        const bool en = isEnabled(); const float a = en ? 1.0f : 0.38f;
        // a candle at each end
        const float ch = r.getHeight() * 0.62f;
        choir::candle (g, P, juce::Rectangle<float> (r.getX() + 16, r.getCentreY() - ch * 0.5f, ch * 0.34f, ch), en, 0.5f + 0.4f * std::sin (ed.clock * 5.0f), a);
        choir::candle (g, P, juce::Rectangle<float> (r.getRight() - 16 - ch * 0.34f, r.getCentreY() - ch * 0.5f, ch * 0.34f, ch), en, 0.5f + 0.4f * std::cos (ed.clock * 4.0f), a);
    }
    Editor& ed; juce::String title, sub;
};

// an organ stop knob with its name and value under it
struct Knob : public juce::Component
{
    Knob (juce::String n, double lo, double hi, double step, std::function<juce::String (double)> fmt) : name (std::move (n)), format (std::move (fmt))
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag); s.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        s.setRange (lo, hi, step); s.onValueChange = [this] { repaint(); if (onChange) onChange (s.getValue()); };
        addAndMakeVisible (s);
    }
    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().withTrimmedTop ((float) s.getBottom());
        const float a = isEnabled() ? 1.0f : choir::greyA (*P);
        g.setColour (choir::greyInk (*P, P->ink, a)); g.setFont (serif (nameSize, choir::bold)); g.drawText (name, r.removeFromTop (17), juce::Justification::centred, false);
        g.setColour (choir::greyInk (*P, P->dark ? P->gold : P->edge.darker (0.35f), a)); g.setFont (serif (14.0f, choir::bold)); g.drawText (format (s.getValue()), r.removeFromTop (16), juce::Justification::centred, false);
    }
    void resized() override
    {
        const int side = juce::jmax (20, juce::jmin (getWidth() - 4, getHeight() - 34, 64));
        s.setBounds (getLocalBounds().removeFromTop (getHeight() - 34).withSizeKeepingCentre (side, side));
    }
    juce::Slider s; juce::String name; std::function<juce::String (double)> format; std::function<void (double)> onChange;
    float nameSize = 14.0f;   // (round 3) one size for every name in a row of knobs, small enough that no two names come near
    const choir::Pal* P = &choir::pal (true);
};

// a row of mutually exclusive choices: a hymn board strip, the chosen one lit glass
struct Segments : public juce::Component, public juce::SettableTooltipClient
{
    Segments (Editor& e, juce::StringArray n) : ed (e), names (std::move (n)) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics& g) override
    {
        const auto& P = ed.cp();
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (P.cell); g.fillRoundedRectangle (r, 4); g.setColour (P.edge); g.drawRoundedRectangle (r, 4, 1.0f);
        const float w = r.getWidth() / (float) names.size();
        for (int i = 0; i < names.size(); ++i)
        {
            auto c = juce::Rectangle<float> (r.getX() + w * (float) i, r.getY(), w, r.getHeight()).reduced (2.5f);
            const bool off = enabled.size() > (size_t) i && ! enabled[(size_t) i];
            if (i == selected)
            {
                juce::ColourGradient gr (P.glass[4], c.getX(), c.getY(), P.glass[1], c.getRight(), c.getBottom(), false);
                g.setGradientFill (gr); g.fillRoundedRectangle (c, 3); g.setColour (P.goldHi); g.drawRoundedRectangle (c, 3, 1.0f);
            }
            else if (i > 0 && i != selected + 1) { g.setColour (P.edgeDim); g.fillRect (c.getX() - 2.5f, c.getY() + 5, 1.0f, c.getHeight() - 10); }
            g.setColour (i == selected ? juce::Colours::white : (off ? choir::greyInk (P, P.ink, choir::greyA (P)) : P.ink));
            g.setFont (choir::fitted (choir::bold, names[i], c.getWidth() - 8, juce::jmin (15.0f, c.getHeight() * 0.62f), 9.0f));
            g.drawText (names[i], c, juce::Justification::centred, false);
        }
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        const int i = juce::jlimit (0, names.size() - 1, (int) (e.position.x / ((float) getWidth() / (float) names.size())));
        if (enabled.size() > (size_t) i && ! enabled[(size_t) i]) return;
        selected = i; repaint(); if (onChange) onChange (i);
    }
    Editor& ed; juce::StringArray names; int selected = 0; std::vector<bool> enabled; std::function<void (int)> onChange;
};

// ================================================================================================ the take (song) view: drag it out
class TakeView : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit TakeView (Editor& e) : ed (e) { setBufferedToImage (true); setMouseCursor (juce::MouseCursor::DraggingHandCursor); setTooltip ("Drag the take into your DAW (a WAV file). Click to play / stop"); }
    void paint (juce::Graphics& g) override
    {
        const auto& P = ed.cp();
        auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (P.waveBg); g.fillRoundedRectangle (r, 5); g.setColour (P.edge); g.drawRoundedRectangle (r, 5, 1.0f);
        an.set (ed.proc.result);
        if (an.peaks.empty())
        {
            voiceSinger (g, P, r.withSizeKeepingCentre (40, 56).translated (0, -14), P.voiceC(), 0, 0, 0.45f);
            g.setColour (P.dim); g.setFont (serif (15.0f, choir::italic)); g.drawText ("your new take appears here", r.withTrimmedTop (r.getHeight() * 0.62f), juce::Justification::centredTop, false);
            return;
        }
        auto wr = r.reduced (10, 16);
        an.paintWave (g, wr, P.voiceC());
        if (ed.proc.isPlaying (ed.proc.result)) { g.setColour (P.play); g.fillRect (wr.getX() + wr.getWidth() * ed.proc.playPosition() - 1.0f, r.getY() + 3, 2.0f, r.getHeight() - 6); }
        g.setColour (P.gold); g.setFont (serif (13.0f, choir::bold)); g.drawText ("DRAG ME OUT", r.reduced (10, 2).removeFromTop (16), juce::Justification::topRight, false);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging || e.getDistanceFromDragStart() < 6 || ! ed.proc.result.existsAsFile()) return;
        dragging = true;
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ ed.proc.result.getFullPathName() }, false, this, [this] { dragging = false; });
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        dragging = false;
        if (! e.mouseWasDraggedSinceMouseDown() && ed.proc.result.existsAsFile()) { if (ed.proc.isPlaying (ed.proc.result)) ed.proc.stopPlaying(); else ed.proc.play (ed.proc.result); }
    }
    Editor& ed; mutable Analysis an; bool dragging = false;
};

// ================================================================================================ ABOUT / CREDITS (polish 1)
// Its own page over the window: a heading, the licence in a few plain lines, then one card per open-source part (what it does in
// Ghost Voice, its name, who made it, its licence and where its source is), in two columns that scroll. creditsText() keeps the
// same facts as plain text (the tests read it).
// (polish 3) a soft fade over the foot of a scroll area: a card cut by its edge melts into the page instead of being sliced
class FadeFoot : public juce::Component
{
public:
    explicit FadeFoot (std::function<juce::Colour()> c) : colour (std::move (c)) { setInterceptsMouseClicks (false, false); }
    void paint (juce::Graphics& g) override
    {
        const auto c = colour();
        juce::ColourGradient gr (c.withAlpha (0.0f), 0, 0, c, 0, (float) getHeight() * 0.85f, false);
        g.setGradientFill (gr); g.fillRect (getLocalBounds());
    }
    std::function<juce::Colour()> colour;
};

class CreditsView : public juce::Component
{
public:
    struct Card { juce::String role, name, by, licence, note, links; juce::Rectangle<float> r; };
    explicit CreditsView (Editor& e) : ed (e)
    {
        auto add = [this] (const char* role, const char* name, const char* by, const char* lic, const char* note, const char* links)
        { cards.push_back ({ juce::String::fromUTF8 (role), juce::String::fromUTF8 (name), juce::String::fromUTF8 (by), juce::String::fromUTF8 (lic), juce::String::fromUTF8 (note), juce::String::fromUTF8 (links), {} }); };
        add ("THE NEURAL ENGINE", "Seed-VC  @ 51383ef", "Plachtaa", "GPL-3.0", "Not built into the plug-in: INSTALL VOICE ENGINE downloads it, and it runs as a separate helper on your Mac.", "github.com/Plachtaa/seed-vc");
        add ("MODELS THE VOICE ENGINE DOWNLOADS", "Singing DiT, BigVGAN, Whisper, RMVPE, CAMPPlus",
             "Plachtaa (GPL-3.0)  \xc2\xb7  NVIDIA (MIT)  \xc2\xb7  OpenAI (Apache-2.0)  \xc2\xb7  lj1995 / RVC project (MIT)  \xc2\xb7  FunASR (Apache-2.0)", "",
             "The singing model, the 44 kHz voice decoder (BigVGAN v2), the word listener (Whisper small), the pitch tracker and the voice-print meter.",
             "github.com/NVIDIA/BigVGAN  \xc2\xb7  github.com/openai/whisper  \xc2\xb7  github.com/RVC-Project/Retrieval-based-Voice-Conversion-WebUI  \xc2\xb7  github.com/modelscope/FunASR");
        add ("THE VOICE BLEND (BALANCE, NOVELTY)", "Ghost Voice Identity Lab v0.6", "Ghost Voice Identity Lab contributors", "MIT", "", "");
        add ("NATURAL AND CLASSIC", "GhostBlock's own engines, on WORLD v1.0.1", "Masanori Morise (WORLD)", "modified BSD", "", "github.com/mmorise/World");
        add ("CHOP'S SOUND ENGINE", "INTERSECT  @ b40c2fb", "tucktuckg00se", "GPL-3.0", "", "github.com/tucktuckg00se/INTERSECT");
        add ("INTERSECT'S STRETCH ENGINES AND IMPORT", "Signalsmith Stretch, Bungee, Eigen, PFFFT, VelociLoops",
             "Signalsmith Audio (MIT)  \xc2\xb7  Parabolic Labs (MPL-2.0)  \xc2\xb7  Eigen (MPL-2.0)  \xc2\xb7  Julien Pommier (BSD-like)  \xc2\xb7  kunitoki", "", "",
             "github.com/Signalsmith-Audio/signalsmith-stretch  \xc2\xb7  github.com/bungee-audio-stretch/bungee  \xc2\xb7  eigen.tuxfamily.org  \xc2\xb7  github.com/kunitoki/VelociLoops");
        add ("CHOP'S TEMPO, KEY AND HIT FINDER", "Flayr Chop's analysis  @ c7baf83", "FlayrLabs", "AGPL-3.0", "AUTO CHOP's HITS, EVERY BEAT, EVERY 2 BEATS, EVERY BAR and EVEN.", "github.com/FlayrLabs/flayr-chop");
        add ("CHOP'S PHRASES", "OpenVPI audio-slicer  @ 9958eed", "OpenVPI", "MIT", "", "github.com/openvpi/audio-slicer");
        add ("CHOP'S WORDS", "WhisperX 3.3.1", "Max Bain", "BSD-2-Clause", "Not built in: INSTALL WORD FINDER downloads it (with faster-whisper \"small\", MIT, and the wav2vec2 word aligner) and it runs as a separate helper.", "github.com/m-bain/whisperX");
        add ("THE INSTALLER'S PRIVATE PYTHON", "uv 0.5.11", "Astral", "MIT or Apache-2.0", "", "github.com/astral-sh/uv");
        add ("THE PLUG-IN FRAMEWORK", "JUCE 8.0.14", "Raw Material Software", "AGPLv3 option", "", "github.com/juce-framework/JUCE");
        add ("THE WINDOW'S TYPE", "Crimson Pro and Gloock", "The Crimson Pro Project Authors  \xc2\xb7  The Gloock Project Authors", "SIL Open Font License 1.1", "", "OFL-CrimsonPro.txt and OFL-Gloock.txt in the Licenses folder");
        add ("THE TEST SINGERS IN OUR DEMOS", "VocalSet", "Wilkins, Seetharaman, Wahl & Pardo (ISMIR 2018)", "CC BY 4.0", "", "");
        add ("CHOP'S TOOLS AND KEYS", "Block Step's", "GhostBlock", "AGPL-3.0", "Block Step's tools and letters, which are FL Studio's.", "");
        add ("EVERYTHING ELSE", "TERRAIN, the Spirit Board, the ghost choir, the themes, MOTION", "GhostBlock", "AGPL-3.0", "The full list is in NOTICE.md, in the Licenses folder.", "");
    }
    static juce::TextLayout wrap (const juce::String& t, const juce::Font& f, juce::Colour c, float w)
    {
        juce::AttributedString a; a.append (t, f, c); a.setWordWrap (juce::AttributedString::byWord); juce::TextLayout tl; tl.createLayout (a, w); return tl;
    }
    float introH = 0;
    int layoutFor (int width)
    {
        const auto& P = ed.cp();
        const float pad = 22, gap = 14;
        introH = 40 + 26 + wrap (intro(), serif (16.0f), P.ink, (float) width - 2 * pad).getHeight() + 18 + 34;
        const float colW = ((float) width - 2 * pad - gap) * 0.5f;
        float colY[2] { introH, introH };
        for (auto& c : cards)
        {
            const float inner = colW - 28;
            float h = 12 + 16 + 26 + wrap (meta (c), serif (15.0f), P.dim, inner).getHeight() + 2;
            if (c.note.isNotEmpty()) h += wrap (c.note, serif (15.0f), P.dim, inner).getHeight() + 4;
            if (c.links.isNotEmpty()) h += wrap (c.links, serif (14.0f, choir::bold), P.gold, inner).getHeight() + 4;
            h += 12;
            const int col = colY[0] <= colY[1] ? 0 : 1;
            c.r = { pad + (float) col * (colW + gap), colY[col], colW, h };
            colY[col] += h + gap;
        }
        return (int) std::ceil (juce::jmax (colY[0], colY[1]) + 24);   // visible padding under the last card
    }
    static juce::String meta (const Card& c) { return "by " + c.by + (c.licence.isNotEmpty() ? juce::String::fromUTF8 ("   \xc2\xb7   ") + c.licence : juce::String()); }
    static juce::String intro()
    {
        return "Free and open source, by GhostBlock. Licence: the GNU Affero General Public License v3.0 only. The full source of this build ships in "
               "Licenses/Ghost Voice source.zip, and the licence texts are in the Licenses folder next to the plug-in.";
    }
    void paint (juce::Graphics& g) override
    {
        const auto& P = ed.cp();
        const float pad = 22;
        auto top = juce::Rectangle<float> (pad, 14, (float) getWidth() - 2 * pad, 38);
        g.setColour (P.ink); g.setFont (display (30.0f)); g.drawText ("About Ghost Voice", top, juce::Justification::centredLeft, false);
        g.setColour (P.gold); g.setFont (serif (17.0f, choir::italic));
        g.drawText (juce::String ("version 1 (beta)") + "  -  who made the parts inside it", top.translated (0, 34).withHeight (24), juce::Justification::centredLeft, false);
        auto it = wrap (intro(), serif (16.0f), P.ink, (float) getWidth() - 2 * pad);
        it.draw (g, { pad, 80, (float) getWidth() - 2 * pad, it.getHeight() });
        choir::title (g, P, juce::Rectangle<float> (pad, introH - 36, (float) getWidth() - 2 * pad, 26), "The Open-Source Parts", "");
        for (auto& c : cards)
        {
            auto r = c.r;
            g.setColour (P.card.withAlpha (P.dark ? 0.55f : 0.85f)); g.fillRoundedRectangle (r, 5);
            g.setColour (P.edgeDim); g.drawRoundedRectangle (r.reduced (0.5f), 5, 1.0f);
            g.setColour (P.gold); g.fillRect (r.getX(), r.getY() + 8, 3.0f, r.getHeight() - 16);
            auto t = r.reduced (14, 12);
            g.setColour (P.dark ? P.gold : P.edge.darker (0.3f)); g.setFont (serif (13.0f, choir::bold)); g.drawText (c.role, t.removeFromTop (16), juce::Justification::centredLeft, false);
            g.setColour (P.ink); g.setFont (choir::fitted (choir::disp, c.name, t.getWidth(), 19.0f, 12.0f)); g.drawText (c.name, t.removeFromTop (26), juce::Justification::centredLeft, false);
            { auto l = wrap (meta (c), serif (15.0f), P.dim, t.getWidth()); l.draw (g, t.removeFromTop (l.getHeight())); t.removeFromTop (2); }
            if (c.note.isNotEmpty()) { auto l = wrap (c.note, serif (15.0f), P.dim, t.getWidth()); l.draw (g, t.removeFromTop (l.getHeight())); t.removeFromTop (4); }
            if (c.links.isNotEmpty()) { auto l = wrap (c.links, serif (14.0f, choir::bold), P.dark ? P.gold : P.edge.darker (0.3f), t.getWidth()); l.draw (g, t.removeFromTop (l.getHeight())); }
        }
    }
    Editor& ed;
    std::vector<Card> cards;
};

// ================================================================================================ TERRAIN's flat land (polish 1)
// Over the land while it is still flat: a soft glow of candle light where the land will rise, and a plaque saying how to raise the
// first mountain. Drawing only (never takes the mouse); gone as soon as the land has a mountain.
class TerrainHint : public juce::Component
{
public:
    explicit TerrainHint (Editor& e, ghost::terrain::TerrainView& v) : ed (e), view (v) { setInterceptsMouseClicks (false, false); getProperties().set ("overlay", true); }   // an intended overlay: clicks pass through to the land
    // where the plaque sits (over the land's lower part, clear of the land's own toolbar at the foot)
    static constexpr int kPlaqueH = 62;
    void paint (juce::Graphics& g) override
    {
        if (ed.proc.land().any) return;
        const auto& P = ed.cp();
        const auto r = getLocalBounds().toFloat();
        // (polish 3) the ghost of a mountain is laid ON the land's own ground, through the view's own projection, so it sits inside
        // the coast as the land rests (this overlay starts 60 px under the view's top)
        const auto off = juce::Point<float> ((float) (view.getX() - getX()), (float) (view.getY() - getY()));
        auto at = [&] (float rho, float ang, float h) { return view.project (0.5f + rho * std::cos (ang), 0.5f - rho * std::sin (ang), h) + off; };
        const auto c = at (0.0f, 0.0f, 0.0f);
        const float coastPx = (at (kCoast, 0.0f, 0.0f).x - c.x);
        const auto artR = juce::Rectangle<float> (c.x - coastPx * 1.4f, c.y - coastPx * 1.9f, coastPx * 3.6f, coastPx * 2.8f).getIntersection (r).toNearestInt();
        art.draw (g, juce::String ((int) P.dark) + r.toString() + c.toString() + juce::String (coastPx), artR, [&, artR] (juce::Graphics& ig, juce::Rectangle<int>)
        {
            ig.addTransform (juce::AffineTransform::translation ((float) -artR.getX(), (float) -artR.getY()));
            {   // one soft halo on the ground inside the coast
                const float hx = coastPx * 1.1f, hy = (at (kCoast, juce::MathConstants<float>::halfPi, 0.0f).y - c.y);
                juce::ColourGradient gr (P.glass[4].withAlpha (P.dark ? 0.22f : 0.15f), c.x, c.y, P.glass[4].withAlpha (0.0f), c.x + hx, c.y, true);
                ig.setGradientFill (gr); ig.fillEllipse (c.x - hx, c.y - std::abs (hy) * 1.1f, hx * 2.0f, std::abs (hy) * 2.2f);
            }
            // a ghost of the mountain that could stand here: dashed contour rings rising from the ground, a faint singer on the
            // summit, and an arrow from the coast saying where to drag. Decoration only: it is not land and moves nothing.
            const float dash[] { 5.0f, 5.0f };
            constexpr int kRings = 5;
            juce::Point<float> top;
            for (int k = 0; k < kRings; ++k)
            {
                const float rr = kCoast * (0.82f - 0.165f * (float) k), hh = 0.075f * (float) k;
                juce::Path ring;
                for (int a = 0; a <= 72; ++a)
                {
                    const float t = juce::MathConstants<float>::twoPi * (float) a / 72.0f;
                    const float wob = 1.0f + 0.07f * std::sin (t * 3.0f + (float) k) + 0.04f * std::sin (t * 5.0f + 1.3f * (float) k);
                    const auto pt = at (rr * wob, t, hh);
                    if (a == 0) ring.startNewSubPath (pt); else ring.lineTo (pt);
                }
                ring.closeSubPath();
                juce::Path dd; juce::PathStrokeType (1.3f).createDashedStroke (dd, ring, dash, 2);
                ig.setColour ((P.dark ? P.glow : P.glass[4]).withAlpha (0.60f - 0.07f * (float) k)); ig.fillPath (dd);
                top = at (0.0f, 0.0f, hh);
            }
            {
                const float sw = juce::jmax (14.0f, coastPx * 0.16f);
                choir::singer (ig, P, { top.x - sw * 0.5f, top.y - sw * 1.15f, sw, sw * 1.4f }, P.voiceC(), 0.3f, 0.6f, 0.55f, false);
            }
            {   // the arrow: from the coast's right edge, up and out
                const auto a0 = at (kCoast * 1.12f, 0.25f, 0.0f), a1 = a0.translated (coastPx * 0.55f, -coastPx * 0.62f);
                juce::Path ar; ar.startNewSubPath (a0); ar.quadraticTo (a0.translated (coastPx * 0.5f, -coastPx * 0.02f), a1);
                ig.setColour (P.dark ? P.goldHi : P.edge.darker (0.2f)); ig.strokePath (ar, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                juce::Path head; head.addTriangle (a1.x, a1.y - 7, a1.x - 7, a1.y + 5, a1.x + 6, a1.y + 4); ig.fillPath (head);
                ig.setFont (serif (15.0f, choir::bold));
                ig.drawText ("drag the coast up", juce::Rectangle<float> (a1.x - 70, a1.y - 28, 140, 18), juce::Justification::centred, false);
            }
        });
        const juce::String t1 = "A flat land, waiting", t2 = "Drag up from the outline to raise your first mountain, or pick a ready-made land from SHAPES.";
        const auto f2 = serif (15.5f);
        const float pw0 = juce::jmin (r.getWidth() - 40, choir::width (f2, t2) + 50);
        // (polish 3) over the land, in the open sky between SHAPES and the ghost mountain (the land's own toolbar keeps the foot)
        const float landTop = at (0.0f, 0.0f, 0.6f).y - coastPx * 0.45f;
        const float plTop = juce::jlimit (r.getY() + 12.0f, juce::jmax (r.getY() + 12.0f, landTop - (float) kPlaqueH - 10.0f), (landTop - (float) kPlaqueH) * 0.5f);
        const auto pl0 = juce::Rectangle<float> (r.getCentreX() - pw0 * 0.5f, plTop, pw0, (float) kPlaqueH - 2);
        const auto plR = pl0.toNearestInt().expanded (2, 4);
        plaque.draw (g, juce::String ((int) P.dark) + plR.toString(), plR, [&, plR, pl0] (juce::Graphics& ig, juce::Rectangle<int>)
        {
            ig.addTransform (juce::AffineTransform::translation ((float) -plR.getX(), (float) -plR.getY()));
            const auto pl = pl0; const float pw = pl.getWidth(), ph = pl.getHeight();
            ig.setColour (P.shadow); ig.fillRoundedRectangle (pl.translated (0, 2), 6);
            ig.setColour (P.panel.withAlpha (0.94f)); ig.fillRoundedRectangle (pl, 6);
            ig.setColour (P.edge); ig.drawRoundedRectangle (pl.reduced (0.5f), 6, 1.0f);
            choir::diamond (ig, { pl.getX(), pl.getCentreY() }, 4.0f, P.gold); choir::diamond (ig, { pl.getRight(), pl.getCentreY() }, 4.0f, P.gold);
            ig.setColour (P.ink); ig.setFont (display (19.0f)); ig.drawText (t1, pl.withTrimmedBottom (ph * 0.46f).withTrimmedTop (6), juce::Justification::centred, false);
            ig.setColour (P.dim); ig.setFont (choir::fitted (choir::regular, t2, pw - 24, 15.5f, 10.0f)); ig.drawText (t2, pl.withTrimmedTop (ph * 0.52f).withTrimmedBottom (6), juce::Justification::centred, false);
        });
    }
    static constexpr float kCoast = 0.25f;   // the flat land's coast, in landscape units from its centre
    ghost::terrain::TerrainView& view;
    Editor& ed; choir::Cache art, plaque;
};

// ================================================================================================ the parts
// (fixes) the Spirit Board with this window's own ghost painter: the shared hook holds it only while this board paints, so two
// Ghost Voice windows never change (or switch off) each other's ghosts
struct ChoirBoard : public ghosty::spirit::BoardView
{
    using ghosty::spirit::BoardView::BoardView;
    ghosty::GhostPainterFn painter;
    void paint (juce::Graphics& g) override
    {
        if (! painter) { BoardView::paint (g); return; }
        ghosty::ScopedGhostPainter own (painter);
        BoardView::paint (g);
    }
};

struct Editor::Parts
{
    explicit Parts (Editor& e)
        : ed (e), cardA (e, slotA), cardB (e, slotB), cardC (e, -1), cardPerf (e, slotPerf), songPerf (e, slotPerf),
          analyze ("ANALYZE VOICES"), create (e, "CREATE NEW VOICE C", "sing a preview with the new identity and measure it"),
          save ("SAVE VOICE C"), useInSong ("USE IN SONG"),
          quality ("QUALITY", 1, 200, 1, [] (double v) { return juce::String ((int) v) + " steps"; }),
          tone ("TONE", -1, 1, 0.01, [] (double v) { return (v > 0.005 ? "+" : "") + juce::String (juce::roundToInt (v * 100)) + " %"; }),
          timbre ("TIMBRE", -6, 6, 0.1, [] (double v) { return (v > 0.05 ? "+" : "") + juce::String (v, 1) + " st"; }),
          texture ("TEXTURE", 0, 1, 0.01, [] (double v) { return juce::String (juce::roundToInt (v * 100)) + " %"; }),
          breath ("BREATH", -1, 1, 0.01, [] (double v) { return (v > 0.005 ? "+" : "") + juce::String (juce::roundToInt (v * 100)) + " %"; }),
          follow ("FOLLOW", 0, 1, 0.01, [] (double v) { return juce::String (v, 2); }),
          pitch ("PITCH", -24, 24, 1, [] (double v) { return (v > 0 ? "+" : "") + juce::String ((int) v) + " st"; }),
          viewTabs (e, { "Waveform", "Spectrum", "Compare" }), source (e, { "Use My Melody", "Audio Guide" }),
          board (e.proc.board), take (e), terrainView (e.proc.terrain), theme (e, { "LIGHT", "DARK" }),
          engineSel (e, { "NEURAL", "NATURAL", "CLASSIC", "BEST OF ALL" })
    {}
    Editor& ed;
    std::vector<std::unique_ptr<TabButton>> tabs;
    std::vector<std::unique_ptr<NavItem>> nav;
    // IDENTITY LAB
    VoiceCard cardA, cardB, cardC, cardPerf;
    juce::Slider balance { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox }, novelty { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::TextButton varDown { "<" }, varUp { ">" };
    juce::TextButton analyze; BigButton create; juce::TextButton save, useInSong;
    Knob quality, tone, timbre, texture, breath, follow, pitch;
    Segments viewTabs, source;
    Candle matchRange { "Match Range" }, promptFree { "Prompt-Free" }, gpu { "Apple GPU" }, rights { "I have the rights to these voices" };
    Candle keepCons { "Keep My Consonants" }, instant { "Instant Preview (NATURAL)" };
    juce::TextButton perfRec { "REC MY MELODY" }, perfAdd { "ADD AUDIO GUIDE" };
    // VOICE LIBRARY
    juce::Viewport libView; juce::Component libContent;
    std::vector<std::unique_ptr<juce::Component>> libCards;
    // SING ENGINE
    VoiceCard songPerf;
    ChoirBoard board;
    juce::ComboBox motion, rate; juce::Slider depth { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    std::unique_ptr<BigButton> sing;
    juce::TextButton stop { "STOP" };
    double progressValue = 0; juce::ProgressBar progress { progressValue };
    TakeView take;
    Candle rights2 { "I have the rights to these voices" };
    // TOOLS
    ghost::terrain::TerrainView terrainView;
    std::unique_ptr<TerrainHint> terrainHint;
    juce::Slider terrainAmt { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    // SETTINGS
    juce::TextButton install { "INSTALL VOICE ENGINE" }, reveal { "SHOW ENGINE FOLDER" };
    Segments theme;
    Candle cpuOnly { "CPU only (skip the Apple GPU)" }, bubbleToggle { "Bubbles on presses" };
    BubbleFx bubbles;
    // UNDER THE HOOD
    std::unique_ptr<HoodPage> hood;
    // CHOP (build 8)
    std::unique_ptr<ChopPage> chop;
    // ABOUT / CREDITS (the one place the open-source parts are named)
    juce::TextButton about { "About & credits" }, creditsClose { "CLOSE" };
    juce::Viewport creditsBox;              // (polish 1) the ABOUT / CREDITS page: cards in a scroll area
    std::unique_ptr<CreditsView> credits;
    std::unique_ptr<FadeFoot> creditsFade;
    bool creditsOn = false;
    bool boardSang = false;                 // (fixes) the board's singers were singing at the last tick
    // shared
    Segments engineSel;                     // ENGINE (IDENTITY LAB + SING ENGINE)
    juce::TooltipWindow tips { &ed, 500 };
    Analysis anA, anB, anC;
    struct Mote { float t, lane, speed, size; };
    std::vector<Mote> motes;
    choir::Cache floor;                     // the Spirit Board's labyrinth floor
    choir::Cache strip;                     // the bottom strip
    choir::Cache voices, pipes;            // SING ENGINE's voices and its progress pipes
    choir::Cache side;                      // the left column's facts (polish 2)
    choir::Cache analysis;                  // IDENTITY LAB's preview view, distance pipes and seal
    juce::Rectangle<int> idPipes, singPipes, lookPreview[2], voicesPanel;
};

// ================================================================================================ editor
Editor::Editor (Processor& p) : juce::AudioProcessorEditor (p), proc (p), lnf (std::make_unique<choir::Look>()), parts (std::make_unique<Parts> (*this))
{
    auto& q = *parts;
    setLookAndFeel (lnf.get());
    setOpaque (true);
    // the Spirit Board's ghosts are drawn as the choir's hooded singers
    // (fixes) this window's own painter, used only while its board paints. The singers' mouths move only while something plays
    // (the board then repaints with them); at rest they hold still and the board repaints only when the puck or a ghost moves
    q.board.painter = [this] (juce::Graphics& g, const ghosty::GhostLook& L, const juce::AffineTransform& t, bool, float alpha)
    {
        auto box = juce::Rectangle<float> (78, 30, 104, 150).transformedBy (t);
        const float open = boardSings() ? 0.5f + 0.5f * std::sin (clock * 6.0f + box.getX()) : 0.5f;
        choir::singer (g, cp(), box, L.slime, open, 0.5f, alpha);
        return true;
    };
    const char* tabNames[][2] { { "Identity Lab", "Create New Voice" }, { "Voice Library", "Saved Voices" }, { "Sing Engine", "Generate Singing" }, { "Tools", "Terrain" }, { "Settings", "Engine & Look" }, { "Under the Hood", "Original Controls" }, { "Chop", "Play & Chop" } };
    int n = 0;
    for (int i : { (int) chop, 0, 1, 2, 3, 4, 5 })   // CHOP (build 8) is the first tab: the instrument
    { q.tabs.push_back (std::make_unique<TabButton> (*this, i, n++, tabNames[i][0], tabNames[i][1])); addAndMakeVisible (*q.tabs.back()); }
    // (polish 2) no page list in the left column: the tabs are the one nav. The column holds each page's own facts and tools.
    for (auto& nv : q.nav) addAndMakeVisible (*nv);
    q.hood = std::make_unique<HoodPage> (*this);
    q.hood->onTabChange = [this] { chrome.clear(); repaint(); };
    addChildComponent (*q.hood);
    q.chop = std::make_unique<ChopPage> (proc, [this]() -> const ghosty::themes::Palette& { return pal(); }, [this] { showPage (lab); });
    addChildComponent (*q.chop);
    for (auto* c : q.chop->sideControls()) addChildComponent (c);   // ADD WAV, REC, DRAG WAV, CONVERT VOICE: in the left column on CHOP
    addChildComponent (q.chop->statusStrip());   // (fixes) CHOP's status line, under the page and clear of the resize corner
    // (fixes) the old left list's shortcut: Ghost Voice Classic opens the IDENTITY LAB on NATURAL (CLASSIC stays if it is picked)
    q.nav.push_back (std::make_unique<NavItem> (*this, lab, 2, "Ghost Voice Classic", "NATURAL / CLASSIC"));
    q.nav.back()->onPick = [this] { if (! isDsp (proc.engine)) { proc.engine = Engine::natural; refresh(); } };
    q.nav.back()->isOn = [this] { return pageIndex == lab && isDsp (proc.engine); };
    q.nav.back()->setTooltip ("Ghost Voice Classic: opens the Identity Lab on the NATURAL engine (instant, keeps the performer's own recording)");
    addAndMakeVisible (*q.nav.back());

    // ---- IDENTITY LAB
    for (juce::Component* c : std::initializer_list<juce::Component*> { &q.cardA, &q.cardB, &q.cardC, &q.balance, &q.novelty, &q.varDown, &q.varUp, &q.analyze, &q.create, &q.save, &q.useInSong,
                                                                         &q.quality, &q.follow, &q.pitch, &q.viewTabs, &q.source, &q.matchRange, &q.promptFree, &q.gpu, &q.rights, &q.perfRec, &q.perfAdd,
                                                                         &q.tone, &q.timbre, &q.texture, &q.breath, &q.keepCons, &q.instant, &q.engineSel })
        addChildComponent (c);
    q.engineSel.setTooltip ("ENGINE.  NEURAL: an AI re-sing, a new singer (needs the voice engine).  NATURAL: keeps the performer's own recording, subtle blends, instant.  "
                            "CLASSIC: the voice re-synthesised, instant.  BEST OF ALL: NEURAL near voice A and voice B, NATURAL in the middle, crossfaded.");
    q.engineSel.onChange = [this] (int i) { proc.engine = engineFromInt (i); refresh(); resized(); };
    q.tone.onChange = [this] (double v) { proc.dsp.tone = (float) v; refresh(); };
    q.timbre.onChange = [this] (double v) { proc.dsp.timbre = (float) v; refresh(); };
    q.texture.onChange = [this] (double v) { proc.dsp.texture = (float) v; refresh(); };
    q.breath.onChange = [this] (double v) { proc.dsp.breath = (float) v; refresh(); };
    q.tone.s.setTooltip ("TONE: the engine's own BRIGHTNESS (darker / brighter)");
    q.timbre.s.setTooltip ("TIMBRE: the engine's own FORMANT shift: a bigger (down) or smaller (up) voice, the melody stays");
    q.texture.s.setTooltip ("TEXTURE: the engine's own GRIT (rasp)");
    q.breath.s.setTooltip ("BREATH: the engine's own BREATH (less / more air)");
    q.keepCons.setTooltip ("KEEP MY CONSONANTS: s, t, k and breaths stay the performer's own (the engine's own switch; on by default)");
    q.keepCons.onChange = [this] (bool on) { proc.dsp.keepConsonants = on; refresh(); };
    q.instant.setTooltip ("While NEURAL sings (minutes), NATURAL renders the same blend in a second or two so you hear a preview at once");
    q.instant.onChange = [this] (bool on) { proc.instantPreview = on; refresh(); };
    q.balance.setRange (0, 1, 0.01); q.balance.getProperties().set ("full", true);
    q.balance.setTooltip ("VOICE BALANCE: how much of voice A and of voice B the new voice is made of (a blend of their measured voice prints)");
    q.balance.onValueChange = [this] { proc.setBalance ((float) parts->balance.getValue()); refresh(); };
    q.novelty.setRange (0, 1, 0.01);
    q.novelty.setTooltip ("NOVELTY: turns the new voice away from BOTH A and B (at right angles to everything A and B are). The distance it really ends up at is measured after CREATE");
    q.novelty.onValueChange = [this] { proc.settings.novelty = (float) parts->novelty.getValue(); refresh(); };
    q.varDown.onClick = [this] { proc.settings.variation = juce::jmax (1, proc.settings.variation - 1); refresh(); };
    q.varUp.onClick = [this] { proc.settings.variation = juce::jmin (999, proc.settings.variation + 1); refresh(); };
    q.varDown.setTooltip ("VARIATION: which new direction NOVELTY turns towards"); q.varUp.setTooltip (q.varDown.getTooltip());
    q.quality.onChange = [this] (double v) { proc.settings.steps = (int) v; refresh(); };
    q.follow.onChange = [this] (double v) { proc.settings.follow = (float) v; refresh(); };
    q.pitch.onChange = [this] (double v) { proc.settings.semitones = (int) v; refresh(); };
    q.quality.s.setTooltip ("QUALITY: the neural engine's diffusion steps (its default 30). More = cleaner and slower");
    q.follow.s.setTooltip ("FOLLOW: the neural engine's guidance strength (its default 0.70). Higher holds harder to the new voice");
    q.pitch.s.setTooltip ("PITCH: the neural engine's semitone shift of the performance");
    q.viewTabs.onChange = [this] (int) { repaint(); };
    q.source.onChange = [this] (int i) { parts->perfRec.setVisible (pageIndex == lab && i == 0); parts->perfAdd.setVisible (pageIndex == lab && i == 1); };
    q.perfRec.onClick = [this] { if (proc.recordingSlot() == slotPerf) proc.stopRecording(); else proc.startRecording (slotPerf); };
    q.perfAdd.onClick = [this] { parts->cardPerf.choose(); };
    q.matchRange.setTooltip ("MATCH RANGE: the neural engine moves the melody into the new voice's range (off = your melody exactly as sung)");
    q.promptFree.setTooltip ("PROMPT-FREE: the new voice comes from the blended voice vectors alone (the blend alone). Off = the neural engine also hears a recording of the nearer voice: more of that singer's detail, but it leans to them");
    q.gpu.setTooltip ("Render on the Apple GPU when it works (off = the CPU)");
    q.rights.setTooltip ("Ghost Voice only works on voices that are yours or that you have permission to use. No famous voices, no impersonation.");
    q.matchRange.onChange = [this] (bool on) { proc.settings.autoPitch = on; refresh(); };
    q.promptFree.onChange = [this] (bool on) { proc.settings.detail = ! on; refresh(); };
    q.gpu.onChange = [this] (bool on) { proc.settings.cpuOnly = ! on; refresh(); };
    q.rights.onChange = [this] (bool on) { proc.rights = on; refresh(); };
    q.analyze.setTooltip ("Measure the voice prints of voice A and voice B: how far apart they are");
    q.analyze.onClick = [this] { proc.analyzeVoices(); refresh(); };
    q.create.onClick = [this] { proc.createVoiceC(); refresh(); };
    q.save.setTooltip ("Keep voice C in the VOICE LIBRARY (its recipe, its takes and what was measured)");
    q.save.onClick = [this]
    {
        auto* w = new juce::AlertWindow ("Save voice C", "A name for the new voice:", juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor ("name", proc.voiceName == "Voice C" ? "Voice C " + juce::String ((int) proc.library.size() + 1) : proc.voiceName);
        w->addButton ("SAVE", 1, juce::KeyPress (juce::KeyPress::returnKey)); w->addButton ("CANCEL", 0);
        juce::Component::SafePointer<Editor> sp (this);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([sp, w] (int r)
        {
            if (r != 1 || sp == nullptr) return;
            juce::String why;
            if (! sp->proc.saveVoiceC (w->getTextEditorContents ("name"), why)) juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Ghost Voice", why);
            sp->refresh();
        }), true);
    };
    q.useInSong.setTooltip ("Sing a whole performance with this voice (SING ENGINE)");
    q.useInSong.onClick = [this] { showPage (sing); };

    // ---- LIBRARY
    addChildComponent (q.libView); q.libView.setViewedComponent (&q.libContent, false); q.libView.setScrollBarsShown (true, false);

    // ---- SING ENGINE
    q.sing = std::make_unique<BigButton> (*this, "SING IT", "in your new voice");
    for (juce::Component* c : std::initializer_list<juce::Component*> { &q.songPerf, &q.board, &q.motion, &q.rate, &q.depth, q.sing.get(), &q.stop, &q.progress, &q.take, &q.rights2 })
        addChildComponent (c);
    q.board.fillBehind = false; q.board.setOpaque (false);   // (polish 3) the board sits in its own panel: no box of a second colour round it
    q.board.hint = {};   // (polish 1) said under the board in the window's own type, at a readable size
    q.board.palette = [this] () -> const th::Palette& { return pal(); };
    q.board.onPuckMoved = [this] (float, float) { refresh(); };
    q.board.onGhostMoved = [this] (int, float, float) { refresh(); };
    q.board.livePuck = [this] { return proc.motionOn() ? ghosty::spirit::orbitPuck (proc.board.orbit, proc.board.activePath(), { proc.board.puckX, proc.board.puckY }, (double) clock * proc.bpm() / 60.0) : juce::Point<float> { proc.board.puckX, proc.board.puckY }; };
    q.board.paintPath = [this] (juce::Graphics& g)
    {
        auto& b = proc.board;
        const auto a = parts->board.area();
        const auto& P = cp();
        // the cathedral floor under the board: a labyrinth of rings, cached
        parts->floor.draw (g, juce::String ((int) P.dark) + a.toString(), a.toNearestInt(), [&P] (juce::Graphics& ig, juce::Rectangle<int> r)
        {
            // (polish 3) the outer ring sits well inside the board's rounded frame
            const auto c = r.toFloat().getCentre(); const float R = juce::jmin ((float) r.getWidth(), (float) r.getHeight()) * 0.44f;
            for (int k = 1; k <= 7; ++k)
            {
                const float rr = R * (float) k / 7.0f;
                juce::Path ring; ring.addCentredArc (c.x, c.y, rr, rr, 0, 0.35f * (float) (k % 3), juce::MathConstants<float>::twoPi - 0.25f + 0.35f * (float) (k % 3), true);
                ig.setColour (P.edge.withAlpha (k == 7 ? 0.35f : 0.14f)); ig.strokePath (ring, juce::PathStrokeType (k == 7 ? 1.4f : 1.0f));
            }
            for (int k = 0; k < 12; ++k)
            {
                const float an = juce::MathConstants<float>::twoPi * (float) k / 12.0f;
                ig.setColour (P.glass[k % 8].withAlpha (P.dark ? 0.10f : 0.08f));
                juce::Path petal; petal.addCentredArc (c.x, c.y, R * 0.16f, R * 0.16f, 0, an - 0.22f, an + 0.22f, true); petal.lineTo (c); petal.closeSubPath();
                ig.fillPath (petal);
            }
        });
        if (b.ghosts.size() < 2) return;
        auto at = [&] (float x, float y) { return juce::Point<float> (a.getX() + x * a.getWidth(), a.getY() + y * a.getHeight()); };
        const auto pa = at (b.ghosts[0].x, b.ghosts[0].y), pb = at (b.ghosts[1].x, b.ghosts[1].y), pp = at (parts->board.shownPuck.x, parts->board.shownPuck.y);
        for (auto& m : parts->motes)
        {   // motes of candle light flow A -> the puck -> B, from A's glass colour to B's
            const float t = m.t;
            const auto p1 = t < 0.5f ? pa + (pp - pa) * (t * 2.0f) : pp + (pb - pp) * ((t - 0.5f) * 2.0f);
            const float bend = std::sin (t * juce::MathConstants<float>::pi) * m.lane * a.getHeight() * 0.16f;
            g.setColour (P.voiceA().interpolatedWith (P.voiceB(), t).withAlpha (0.15f + 0.5f * std::sin (t * juce::MathConstants<float>::pi)));
            g.fillEllipse (p1.x - m.size, p1.y + bend - m.size, 2 * m.size, 2 * m.size);
        }
    };
    auto& rng = juce::Random::getSystemRandom();
    for (int i = 0; i < 70; ++i) q.motes.push_back ({ rng.nextFloat(), rng.nextFloat() * 2.0f - 1.0f, 0.002f + 0.004f * rng.nextFloat(), 1.0f + 2.2f * rng.nextFloat() });
    q.motion.addItemList ({ "MOTION OFF", "CIRCLE", "FIGURE 8", "WANDER" }, 1);
    q.motion.setTooltip ("MOTION: the puck moves in time with the song, so the voice drifts between A and B as it sings (each blend step is sung, then crossfaded)");
    q.motion.onChange = [this] { const int i = parts->motion.getSelectedItemIndex(); proc.board.orbit.mode = i == 1 ? ghosty::spirit::Orbit::Circle : i == 2 ? ghosty::spirit::Orbit::Figure8 : i == 3 ? ghosty::spirit::Orbit::RandomWalk : ghosty::spirit::Orbit::Off; refresh(); };
    q.rate.addItemList ({ "1 BAR", "2 BARS", "4 BARS", "8 BARS" }, 1);
    q.rate.setTooltip ("How long one trip of the puck takes, in bars of the song's tempo (greyed with MOTION OFF)");
    q.rate.onChange = [this] { const int bars = 1 << juce::jmax (0, parts->rate.getSelectedItemIndex()); proc.board.orbit.rate = 1.0f / (4.0f * (float) bars); proc.board.orbit.sync = true; refresh(); };
    q.depth.setRange (0.02, 0.5, 0.01); q.depth.setTooltip ("DEPTH: how far the puck travels (greyed with MOTION OFF)");
    q.depth.onValueChange = [this] { proc.board.orbit.depth = (float) parts->depth.getValue(); repaint(); };
    q.sing->onClick = [this] { proc.renderSong(); refresh(); };
    q.stop.onClick = [this] { proc.cancelAll(); refresh(); };
    q.rights2.setTooltip (q.rights.getTooltip());
    q.rights2.onChange = [this] (bool on) { proc.rights = on; refresh(); };

    // ---- TOOLS
    addChildComponent (q.terrainView); addChildComponent (q.terrainAmt);
    q.terrainHint = std::make_unique<TerrainHint> (*this, q.terrainView); addChildComponent (*q.terrainHint);
    q.terrainAmt.setRange (0, 1, 0.01); q.terrainAmt.setTooltip ("How much the land moves the engine's own controls");
    q.terrainAmt.onValueChange = [this] { proc.terrainAmount = (float) parts->terrainAmt.getValue(); refresh(); };

    // ---- SETTINGS
    for (juce::Component* c : std::initializer_list<juce::Component*> { &q.install, &q.reveal, &q.theme, &q.cpuOnly, &q.bubbleToggle }) addChildComponent (c);
    q.bubbleToggle.setTooltip ("A bubble of light blooms on every press (off when macOS Reduce motion is on)");
    q.bubbleToggle.onChange = [this] (bool on) { proc.bubbles = on; parts->bubbles.enabled = on; };
    addAndMakeVisible (q.bubbles);
    q.bubbles.wants = [] (juce::Component* c) { return dynamic_cast<juce::Button*> (c) != nullptr || dynamic_cast<Candle*> (c) != nullptr || dynamic_cast<Segments*> (c) != nullptr; };
    addMouseListener (&q.bubbles, true);
    q.install.setTooltip ("Downloads the open-source neural voice engine and its models (about 2.5 GB, once) into the engine folder. Nothing is uploaded, ever. Who made it: Settings, About & credits");
    q.install.onClick = [this] { proc.installEngine(); refresh(); };
    q.reveal.setTooltip ("Opens the folder the voice engine lives in");
    q.reveal.onClick = [] { auto d = engineDir(); if (! d.exists()) d = d.getParentDirectory(); d.revealToUser(); };
    q.theme.setTooltip ("LIGHT (morning: limestone and sunlit glass) or DARK (night: stone, gold and candle-lit glass). Saved with the project");
    q.theme.onChange = [this] (int i) { proc.theme = i == 1 ? th::Theme::Dark : th::Theme::Light; applyTheme(); };
    q.cpuOnly.setTooltip ("Render on the CPU even when the Apple GPU works");
    q.cpuOnly.onChange = [this] (bool on) { proc.settings.cpuOnly = on; refresh(); };

    proc.onChange = [sp = juce::Component::SafePointer<Editor> (this)] { if (sp != nullptr) juce::MessageManager::callAsync ([sp] { if (sp != nullptr) sp->refresh(); }); };
    // ---- ABOUT / CREDITS
    addAndMakeVisible (q.about); q.about.setTooltip ("Who made the open-source parts inside Ghost Voice, their licences and where their source code is");
    q.about.onClick = [this] { showCredits (! parts->creditsOn); };
    addChildComponent (q.creditsBox); addChildComponent (q.creditsClose);
    q.creditsFade = std::make_unique<FadeFoot> ([this] { return cp().panel; }); addChildComponent (*q.creditsFade);
    q.credits = std::make_unique<CreditsView> (*this);
    q.creditsBox.setViewedComponent (q.credits.get(), false); q.creditsBox.setScrollBarsShown (true, false); q.creditsBox.setScrollBarThickness (12);
    q.creditsClose.onClick = [this] { showCredits (false); };

    setResizable (true, true);
    setResizeLimits (defaultW, defaultH, maxW, maxH);
    setSize (defaultW, defaultH);
    applyTheme();
    showPage (chop);
    startTimerHz (30);
}

Editor::~Editor() { stopTimer(); proc.onChange = nullptr; removeMouseListener (&parts->bubbles); setLookAndFeel (nullptr); parts.reset(); }

void Editor::applyTheme()
{
    const auto& P = cp();
    if (auto* l = dynamic_cast<choir::Look*> (&getLookAndFeel())) l->setPal (P);
    auto& q = *parts;
    q.theme.selected = proc.theme == th::Theme::Light ? 0 : 1;
    for (auto* k : { &q.quality, &q.follow, &q.pitch, &q.tone, &q.timbre, &q.texture, &q.breath }) k->P = &P;
    q.balance.getProperties().set ("c0", (juce::int64) P.voiceA().getARGB()); q.balance.getProperties().set ("c1", (juce::int64) P.voiceB().getARGB());
    q.novelty.getProperties().set ("c0", (juce::int64) P.glass[6].getARGB()); q.novelty.getProperties().set ("c1", (juce::int64) P.voiceC().getARGB());
    q.terrainAmt.getProperties().set ("c0", (juce::int64) P.glass[2].getARGB()); q.terrainAmt.getProperties().set ("c1", (juce::int64) P.glass[3].getARGB());
    q.depth.getProperties().set ("c0", (juce::int64) P.voiceA().getARGB()); q.depth.getProperties().set ("c1", (juce::int64) P.voiceB().getARGB());
    q.bubbles.c0 = P.glass[3]; q.bubbles.c1 = P.voiceC(); q.bubbles.c2 = P.glass[6];
    for (auto* t : { &q.matchRange, &q.promptFree, &q.gpu, &q.rights, &q.rights2, &q.cpuOnly, &q.bubbleToggle, &q.keepCons, &q.instant }) t->P = &P;
    auto tp = ghost::terrain::Palette::ghostSlice();
    tp.ground = P.waveBg; tp.ring = P.ink.withAlpha (0.85f); tp.ringDim = P.ink.withAlpha (0.35f);
    tp.accent = P.gold; tp.accentTop = P.play; tp.handle = P.play; tp.handleDim = P.gold.withAlpha (0.5f); tp.onHandle = P.onGold;
    tp.valley = P.glass[1].withAlpha (0.5f); tp.button = P.cell; tp.buttonHi = P.card; tp.text = P.ink; tp.dim = P.dim;
    tp.font = [] (float h, bool bold) { return serif (h * 1.12f, bold ? choir::bold : choir::regular); };
    q.terrainView.setPalette (tp);
    if (q.hood) q.hood->applyTheme();
    if (q.credits) q.credits->repaint();
    q.floor.clear(); chrome.clear();
    sendLookAndFeelChange();
    refresh();
}

juce::String Editor::statusText() const
{
    if (proc.busy()) return proc.progressText() + "  " + juce::String (juce::roundToInt (proc.progressValue() * 100)) + " %";
    if (proc.lastError.isNotEmpty()) return proc.lastError;
    if (proc.runner.state() == Runner::State::failed && proc.task() == Processor::Task::none && proc.runner.lastWasAnalyze()) return proc.runner.message();
    if (! proc.engineReady() && ! isDsp (proc.engine) && proc.currentRecipe().usesSeed()) return "NEURAL is not installed: SETTINGS > INSTALL VOICE ENGINE (NATURAL and CLASSIC work now)";
    if (proc.recordingSlot() >= 0) return "Recording " + slotName (proc.recordingSlot()) + "...";
    if (proc.previewIsCurrent()) return "Ready: " + proc.voiceName + " is made and ready to sing.";
    const auto why = proc.whyNotPreview();
    return why.isNotEmpty() ? why : "Ready: press CREATE NEW VOICE C.";
}

void Editor::refresh()
{
    auto& q = *parts;
    for (auto* c : { &q.cardA, &q.cardB, &q.cardC, &q.cardPerf, &q.songPerf }) c->update();
    q.balance.setValue (proc.blendB(), juce::dontSendNotification);
    q.novelty.setValue (proc.settings.novelty, juce::dontSendNotification);
    q.quality.s.setValue (proc.settings.steps, juce::dontSendNotification);
    q.follow.s.setValue (proc.settings.follow, juce::dontSendNotification);
    q.pitch.s.setValue (proc.settings.semitones, juce::dontSendNotification);
    q.tone.s.setValue (proc.dsp.tone, juce::dontSendNotification); q.timbre.s.setValue (proc.dsp.timbre, juce::dontSendNotification);
    q.texture.s.setValue (proc.dsp.texture, juce::dontSendNotification); q.breath.s.setValue (proc.dsp.breath, juce::dontSendNotification);
    q.keepCons.setToggleState (proc.dsp.keepConsonants, false); q.instant.setToggleState (proc.instantPreview, false);
    q.engineSel.selected = (int) proc.engine; q.engineSel.repaint();
    q.novelty.setEnabled (proc.noveltyWorks()); q.varDown.setEnabled (proc.noveltyWorks()); q.varUp.setEnabled (proc.noveltyWorks());
    q.novelty.setTooltip (proc.noveltyWorks() ? (proc.engine == Engine::best ? "NOVELTY: turns the NEURAL parts (near A and B) away from both voices. The distance it really ends up at is measured after CREATE"
                                                                               : "NOVELTY: turns the new voice away from BOTH A and B (at right angles to everything A and B are). The distance it really ends up at is measured after CREATE")
                                              : proc.noveltyWhy());
    q.matchRange.setToggleState (proc.settings.autoPitch, false);
    q.promptFree.setToggleState (! proc.settings.detail, false);
    q.gpu.setToggleState (! proc.settings.cpuOnly, false);
    q.cpuOnly.setToggleState (proc.settings.cpuOnly, false);
    {   // (polish 3, rule 3) switches that only steer the neural engine are greyed while it can't run, with the reason in the tooltip
        const bool seedOk = proc.engineReady();
        const juce::String why = "Greyed: the neural voice engine isn't installed (SETTINGS > INSTALL VOICE ENGINE)";
        q.matchRange.setEnabled (seedOk); q.promptFree.setEnabled (seedOk); q.gpu.setEnabled (seedOk); q.instant.setEnabled (seedOk);
        q.matchRange.setTooltip (seedOk ? "MATCH RANGE: the neural engine moves the melody into the new voice's range (off = your melody exactly as sung)" : why);
        q.promptFree.setTooltip (seedOk ? "PROMPT-FREE: the new voice comes from the blended voice vectors alone (the blend alone). Off = the neural engine also hears a recording of the nearer voice: more of that singer's detail, but it leans to them" : why);
        q.gpu.setTooltip (seedOk ? "Render on the Apple GPU when it works (off = the CPU)" : why);
        q.instant.setTooltip (seedOk ? "While NEURAL sings (minutes), NATURAL renders the same blend in a second or two so you hear a preview at once" : why);
        // SETTINGS: CPU only steers the installed engine, so it follows that page's own "installed / not installed" line
        const bool installed = engineInstalled();
        q.cpuOnly.setEnabled (installed);
        q.cpuOnly.setTooltip (installed ? "Render on the CPU even when the Apple GPU works" : "Greyed: there is no voice engine to steer yet. Install it first (INSTALL VOICE ENGINE)");
    }
    q.bubbleToggle.setToggleState (proc.bubbles, false); q.bubbles.enabled = proc.bubbles;
    q.rights.setToggleState (proc.rights, false); q.rights2.setToggleState (proc.rights, false);
    const bool busy = proc.busy();
    const auto whyC = proc.whyNotPreview(), whyS = proc.whyNotSong();
    q.create.setEnabled (! busy && whyC.isEmpty()); q.create.setTooltip (whyC.isEmpty() ? "Sing a short preview with the new voice (" + engineName (proc.engine) + ", on your Mac; nothing is uploaded) and measure it" : whyC);
    q.sing->setEnabled (! busy && whyS.isEmpty()); q.sing->setTooltip (whyS.isEmpty() ? "Sing the whole performance with the new voice" : whyS);
    q.analyze.setEnabled (! busy && proc.engineReady() && ! proc.takes[slotA].isEmpty() && ! proc.takes[slotB].isEmpty());
    q.analyze.setButtonText (proc.analysisAB.isObject() ? juce::String::fromUTF8 ("ANALYZE VOICES  \xe2\x9c\x93") : "ANALYZE VOICES");
    q.analyze.setTooltip (q.analyze.isEnabled() ? "Measure the voice prints of voice A and voice B: how far apart they are"
                                                : busy ? "Greyed: Ghost Voice is busy" : ! proc.engineReady() ? "Greyed: measuring needs the NEURAL voice engine (SETTINGS > INSTALL VOICE ENGINE)" : "Greyed: add voice A and voice B first");
    q.save.setEnabled (! busy && proc.previewIsCurrent());
    q.save.setTooltip (q.save.isEnabled() ? "Keep voice C in the VOICE LIBRARY (its recipe, its takes and what was measured)" : "Greyed: CREATE NEW VOICE C first (the preview must match the settings)");
    q.useInSong.setEnabled (proc.previewIsCurrent());
    q.useInSong.setTooltip (q.useInSong.isEnabled() ? "Sing a whole performance with this voice (SING ENGINE)" : "Greyed: CREATE NEW VOICE C first");
    q.perfRec.setButtonText (proc.recordingSlot() == slotPerf ? "STOP RECORDING" : "REC MY MELODY");
    q.install.setEnabled (! busy);
    q.install.setButtonText (engineInstalled() ? "REINSTALL VOICE ENGINE" : "INSTALL VOICE ENGINE");
    const auto& o = proc.board.orbit;
    q.motion.setSelectedItemIndex (o.mode == ghosty::spirit::Orbit::Circle ? 1 : o.mode == ghosty::spirit::Orbit::Figure8 ? 2 : o.mode == ghosty::spirit::Orbit::RandomWalk ? 3 : 0, juce::dontSendNotification);
    const float bars = 1.0f / juce::jmax (1.0e-3f, 4.0f * o.rate);
    q.rate.setSelectedItemIndex (bars <= 1.5f ? 0 : bars <= 3.0f ? 1 : bars <= 6.0f ? 2 : 3, juce::dontSendNotification);
    q.depth.setValue (o.depth, juce::dontSendNotification);
    q.rate.setEnabled (proc.motionOn()); q.depth.setEnabled (proc.motionOn());
    q.stop.setVisible (busy && pageIndex == sing);
    if (pageIndex == lab) layoutEngineControls();
    if (q.hood) q.hood->sync();
    q.terrainAmt.setValue (proc.terrainAmount, juce::dontSendNotification);
    std::vector<ghost::terrain::Meter> ms; const auto l = proc.land();
    for (int i = 0; i < l.meterCount; ++i) ms.push_back (l.meters[(size_t) i]);
    q.terrainView.setMeters (ms);
    q.board.refresh();
    if (pageIndex == library) resized();
    repaint();
}

void Editor::timerCallback()
{
    auto& q = *parts;
    clock += 1.0f / 30.0f;
    for (auto& m : q.motes) { m.t += m.speed; if (m.t > 1.0f) m.t -= 1.0f; }
    if (pageIndex == sing)
    {   // (fixes) the board repaints only when the puck or the weights moved, or while its singers sing (and once after, at rest)
        const bool moved = q.board.syncLive();
        const bool singing = boardSings();
        if (singing || q.boardSang) q.board.repaint();
        q.boardSang = singing;
        if (moved && proc.motionOn()) repaint (boardLine());
    }
    if (proc.busy()) { q.progressValue = proc.progressValue(); if (pageIndex == sing) repaint (q.singPipes.withHeight (q.singPipes.getHeight() + 22)); }
    else if (q.progressValue != 0) { q.progressValue = 0; q.progress.repaint(); }
    if (pageIndex == lab && (tick % 2) == 0 && q.create.isEnabled()) q.create.repaint();
    if (pageIndex == sing && (tick % 2) == 0 && q.sing->isEnabled()) q.sing->repaint();
    if (proc.isPlaying() || proc.recordingSlot() >= 0) { for (auto* c : { &q.cardA, &q.cardB, &q.cardC, &q.songPerf }) if (c->isVisible()) c->repaint(); if (q.take.isVisible()) q.take.repaint(); }
    // the two candles by the logo flicker, their smoke rises (two small regions)
    if ((tick % 2) == 0)
        for (int s : { -1, 1 })
            repaint (juce::Rectangle<int> (s < 0 ? logoSlot.getX() - 6 : logoSlot.getRight() - 32, 2, 38, 72));   // (polish 3: the halo is wider)
    if (++tick % 6 == 0 && hasStrip()) repaint (getLocalBounds().removeFromBottom (70));
}

HoodPage* Editor::hoodPage() { return parts->hood.get(); }
bool Editor::hasStrip() const { return (pageIndex == lab || pageIndex == sing) && ! parts->creditsOn; }

// (polish 2) THE LEFT COLUMN: what this page needs at hand, read from the session at paint time (cached until any of it changes).
// IDENTITY LAB: the recipe and what is still missing. VOICE LIBRARY: the choir. SING ENGINE: what the song needs. TOOLS: the land.
// SETTINGS: this Mac. UNDER THE HOOD: our controls and the original flags they set. CHOP: THE CLIP's buttons, then the key commands.
void Editor::paintSide (juce::Graphics& g)
{
    const auto& P = cp();
    auto& q = *parts;
    struct Row { juce::String k, v; int state; };   // state: 1 done, 0 still needed, -1 a plain fact
    std::vector<Row> rows; juce::String title;
    const auto tick = [] (bool b) { return b ? 1 : 0; };
    auto takes = [&] (int sl) { const int n = proc.takes[sl].size(); return n == 0 ? juce::String ("needed") : n == 1 ? choir::friendlyPath (proc.takes[sl][0]).fromLastOccurrenceOf ("/", false, false) : juce::String (n) + " takes"; };
    const int pb = juce::roundToInt (proc.blendB() * 100.0f);
    if (q.creditsOn)
    {
        title = "Ghost Voice";
        rows = { { "VERSION", "v1 beta", -1 }, { "LICENCE", "AGPL-3.0 only", -1 }, { "PRICE", "free and open source", -1 }, { "SOURCE", "in the Licenses folder", -1 } };
    }
    else switch (pageIndex)
    {
        case lab:
            title = "The Recipe";
            rows = { { "VOICE A", takes (slotA), tick (! proc.takes[slotA].isEmpty()) }, { "VOICE B", takes (slotB), tick (! proc.takes[slotB].isEmpty()) },
                     { "RIGHTS", proc.rights ? "ticked" : "tick the box first", tick (proc.rights) }, { "ENGINE", engineName (proc.engine), -1 },
                     { "BALANCE", juce::String (100 - pb) + " % A  -  " + juce::String (pb) + " % B", -1 },
                     { "NOVELTY", proc.noveltyWorks() ? juce::String (juce::roundToInt (proc.settings.novelty * 100)) + " %" : juce::String ("not with ") + engineName (proc.engine), -1 },
                     { proc.voiceName.toUpperCase(), proc.previewIsCurrent() ? (proc.previewId.valid ? "made and measured" : "made") : "not made yet", tick (proc.previewIsCurrent()) } };
            break;
        case library:
        {
            title = "The Choir";
            int unique = 0; for (auto& v : proc.library) unique += v.identity.unique() && v.engine != Engine::classic ? 1 : 0;
            rows = { { "SAVED VOICES", juce::String ((int) proc.library.size()), -1 }, { "UNIQUE IDENTITIES", juce::String (unique) + " of " + juce::String ((int) proc.library.size()), -1 },
                     { "IN THE LAB NOW", proc.voiceName, -1 }, { "USE", "loads its recipe in the lab", -1 }, { "PLAY", "hears its preview", -1 } };
            break;
        }
        case sing:
        {
            title = "Before You Sing";
            const auto& o = proc.board.orbit;
            const juce::String mode = o.mode == ghosty::spirit::Orbit::Circle ? "CIRCLE" : o.mode == ghosty::spirit::Orbit::Figure8 ? "FIGURE 8" : o.mode == ghosty::spirit::Orbit::RandomWalk ? "WANDER" : "off";
            rows = { { proc.voiceName.toUpperCase(), proc.previewIsCurrent() ? "ready" : "make it in the Identity Lab", tick (proc.previewIsCurrent()) },
                     { "PERFORMANCE", takes (slotPerf), tick (! proc.takes[slotPerf].isEmpty()) }, { "RIGHTS", proc.rights ? "ticked" : "tick the box first", tick (proc.rights) },
                     { "ENGINE", engineName (proc.engine), -1 }, { "MOTION", mode, -1 }, { "TAKE", proc.result.existsAsFile() ? "ready to drag out" : "none yet", -1 } };
            break;
        }
        case tools:
        {
            title = "The Land";
            const auto l = proc.land();
            rows = { { "MOVES", engineName (proc.engine) + "'s own controls", -1 }, { "THE LAND NOW", l.any ? "shaped" : "flat", -1 },
                     { "TERRAIN AMOUNT", juce::String (juce::roundToInt (proc.terrainAmount * 100)) + " %", -1 },
                     { "TO RAISE A MOUNTAIN", "drag the coast up", -1 }, { "TO MOVE IT", "drag the mountain", -1 }, { "READY-MADE LANDS", "SHAPES, top right", -1 } };
            break;
        }
        case prefs:
            title = "This Mac";
            rows = { { "NEURAL ENGINE", engineInstalled() ? "installed" : "not installed", tick (engineInstalled()) }, { "NATURAL + CLASSIC", "built in", 1 },
                     { "LOOK", proc.theme == ghosty::themes::Theme::Light ? "LIGHT (Matins)" : "DARK (Vespers)", -1 }, { "BUBBLES", proc.bubbles ? "on" : "off", -1 },
                     { "PROCESSOR", proc.settings.cpuOnly ? "CPU only" : "Apple GPU when it works", -1 } };
            break;
        case hood:
            if (q.hood != nullptr && q.hood->tab() != HoodPage::seedvc)
            {   // the sub-tab's own facts
                title = juce::StringArray { "Your Controls", "The Sound", "Auto Chop", "Phrases", "Flayr Chop" }[q.hood->tab()];
                for (auto& [k, v] : q.hood->sideRows()) rows.push_back ({ k, v, -1 });
                break;
            }
            title = "Your Controls";
            rows = { { "QUALITY", "--diffusion-steps " + juce::String (proc.settings.steps), -1 }, { "FOLLOW", "--inference-cfg-rate " + juce::String (proc.settings.follow, 2), -1 },
                     { "PITCH", "--semi-tone-shift " + juce::String (proc.settings.semitones), -1 }, { "MATCH RANGE", juce::String ("--auto-f0-adjust ") + (proc.settings.autoPitch ? "True" : "False"), -1 },
                     { "OPTIONS LISTED", juce::String ((int) gv::hood::options().size()) + ", in the engine's own order", -1 } };
            break;
        case chop:
            title = "Key Commands";
            rows = { { "SPACE", "play / stop", -1 }, { "E C D Z Y", "the five tools", -1 }, { "Cmd+U", "AUTO CHOP", -1 }, { "Cmd+Z", "undo (Shift: redo)", -1 },
                     { "Cmd+C X V", "copy, cut, paste", -1 }, { "Cmd+B", "duplicate", -1 }, { "Cmd+L", "loop the selection", -1 }, { "PgUp PgDn", "zoom", -1 } };
            break;
        default: break;
    }
    const bool compact = pageIndex == chop && ! q.creditsOn;
    const int top = compact ? q.chop->sideControls().back()->getBottom() + 22 : 92;
    // (polish 3) no void above the pipes: the rows spread a little, and the pipes grow up to meet them
    const int bottomLimit = (q.nav.empty() ? getHeight() - (hasStrip() ? 70 : 0) - 32 : q.nav.front()->getY()) - 22;   // (fixes) above the Classic shortcut
    const int n = (int) rows.size();
    const float H = (float) (bottomLimit - top), gap = 18.0f, base = 4.0f;
    float pitch = compact ? 24.0f : 44.0f;
    const float pipeMin = compact ? 70.0f : 92.0f, pipeMax = compact ? 150.0f : 190.0f;
    {
        const float spare = H - (36.0f + pitch * (float) n) - gap - base - pipeMax;
        if (spare > 0 && n > 0) pitch += juce::jmin (spare / (float) n, compact ? 4.0f : 12.0f);
    }
    const float rowsH = 36.0f + pitch * (float) n - (compact ? 2.0f : 4.0f);
    const float pipesH = juce::jmin (pipeMax + 60.0f, H - rowsH - gap - base);
    const bool pipesOn = pipesH >= pipeMin * 0.66f;
    juce::String key; key << (int) P.dark << title << top << bottomLimit;
    for (auto& r : rows) key << "|" << r.k << "=" << r.v << r.state;
    key << "|" << pitch << "|" << pipesH;
    q.side.draw (g, key, juce::Rectangle<int> (0, top, 206, juce::jmax (1, bottomLimit - top)), [&] (juce::Graphics& ig, juce::Rectangle<int> area)
    {
        ig.setColour (P.panel.withAlpha (0.0f));
        choir::title (ig, P, juce::Rectangle<float> (18, 0, 170, 24), title, "");
        float y = 36;
        for (auto& r : rows)
        {
            const float limit = pipesOn ? (float) area.getHeight() - pipesH - gap - base : (float) area.getHeight();
            if (compact)
            {   // one line: the key in gold, what it does in ink
                if (y + 22 > limit) break;
                ig.setColour (P.dark ? P.gold : P.edge.darker (0.3f)); ig.setFont (serif (14.0f, choir::bold)); ig.drawText (r.k, juce::Rectangle<float> (18, y, 74, 22), juce::Justification::centredLeft, false);
                ig.setColour (P.ink); ig.setFont (choir::fitted (choir::regular, r.v, 96, 14.5f, 10.0f)); ig.drawText (r.v, juce::Rectangle<float> (94, y, 96, 22), juce::Justification::centredLeft, false);
                y += pitch; continue;
            }
            if (y + 40 > limit) break;
            float x = 18;
            if (r.state >= 0)
            {   // a tick when it is there, an open ring while it is still needed
                auto m = juce::Rectangle<float> (18, y + 3, 13, 13);
                if (r.state == 1) { ig.setColour (P.good); ig.fillEllipse (m); juce::Path t; t.startNewSubPath (m.getX() + 3, m.getCentreY()); t.lineTo (m.getX() + 5.5f, m.getBottom() - 3.5f); t.lineTo (m.getRight() - 3, m.getY() + 3.5f); ig.setColour (P.panel); ig.strokePath (t, juce::PathStrokeType (1.6f)); }
                else { ig.setColour (P.warn); ig.drawEllipse (m.reduced (0.5f), 1.4f); }
                x += 20;
            }
            ig.setColour (P.dark ? P.gold : P.edge.darker (0.3f)); ig.setFont (choir::fitted (choir::bold, r.k, 188 - x, 13.0f, 9.0f));
            ig.drawText (r.k, juce::Rectangle<float> (x, y, 188 - x, 18), juce::Justification::centredLeft, false);
            ig.setColour (r.state == 0 ? P.warn : P.ink); ig.setFont (choir::fitted (choir::regular, r.v, 188 - x, 15.0f, 10.0f));
            ig.drawText (r.v, juce::Rectangle<float> (x, y + 17, 188 - x, 20), juce::Justification::centredLeft, false);
            y += pitch;
        }
        if (pipesOn)
        {   // the pipes (decoration: the organ's front), tallest in the middle, standing on a gilded ledge
            const float bottom = (float) area.getHeight() - base - 2.0f, ph = pipesH;
            const int np = 9; const float pw = 14, pg = 5, x0 = 103.0f - (np * pw + (np - 1) * pg) * 0.5f;
            for (int k = 0; k < np; ++k)
            {
                const float hgt = ph * (0.55f + 0.45f * (1.0f - std::abs ((float) k - 4.0f) / 4.0f));
                choir::pipe (ig, P, juce::Rectangle<float> (x0 + (float) k * (pw + pg), bottom - hgt, pw, hgt), 0.0f, P.gold);
            }
            ig.setColour (P.edge); ig.fillRect (20.0f, bottom + 2, 166.0f, 2.0f);
        }
    });
}

juce::Rectangle<int> Editor::boardLine() const { const auto b = parts->board.getBounds(); return { b.getX() + 14, b.getBottom() + 2, b.getWidth() - 28, 20 }; }
ChopPage* Editor::chopPage() { return parts->chop.get(); }
bool Editor::isInterestedInFileDrag (const juce::StringArray& files)
{
    return pageIndex == chop && ! parts->creditsOn && parts->chop != nullptr && parts->chop->isInterestedInFileDrag (files);
}
void Editor::filesDropped (const juce::StringArray& files, int x, int y)
{
    if (isInterestedInFileDrag (files)) parts->chop->filesDropped (files, x, y);   // the same load as a drop on the page
}
juce::String Editor::tabsThatDontFit() const
{
    juce::String bad;
    for (auto& t : parts->tabs) if (! t->textFits()) bad << " [" << t->title << " / " << t->sub << " in " << t->getWidth() << " px]";
    return bad;
}

bool Editor::creditsShown() const { return parts->creditsOn; }

void Editor::showCredits (bool on)
{
    auto& q = *parts;
    q.creditsOn = on;
    if (on)
    {   // over the page: the page's own controls step aside (they come back when it closes)
        for (auto* c : getChildren())
            if (c != &q.creditsBox && c != &q.creditsClose && c != q.creditsFade.get() && c != &q.bubbles
                && dynamic_cast<TabButton*> (c) == nullptr && dynamic_cast<NavItem*> (c) == nullptr && c->isVisible() && dynamic_cast<juce::ResizableCornerComponent*> (c) == nullptr)
                c->setVisible (false);
    }
    q.creditsBox.setVisible (on); q.creditsClose.setVisible (on); q.creditsFade->setVisible (on);
    if (! on) { showPage (pageIndex); return; }
    q.creditsBox.toFront (false); q.creditsFade->toFront (false); q.creditsClose.toFront (false); q.bubbles.toFront (false); q.creditsBox.setViewPosition (0, 0);
    for (auto& t : q.tabs) t->repaint();
    for (auto& nv : q.nav) nv->repaint();
    chrome.clear();
    resized();
}

juce::String Editor::creditsText()
{
    return juce::String::fromUTF8 (
        "GHOST VOICE v1 beta  -  free and open source, by GhostBlock\n"
        "Licence: GNU Affero General Public License v3.0 only. The full source of this build ships in Licenses/Ghost Voice source.zip,\n"
        "and the licence texts in the Licenses folder next to the plug-in.\n\n"
        "THE OPEN-SOURCE PARTS, THEIR AUTHORS AND LICENCES\n\n"
        "NEURAL engine = Seed-VC by Plachtaa, @ 51383ef. GPL-3.0. https://github.com/Plachtaa/seed-vc\n"
        "  Not built into the plug-in: INSTALL VOICE ENGINE downloads it, and it runs as a separate helper on your Mac.\n"
        "  Models it downloads: Seed-VC singing DiT (Plachtaa, GPL-3.0); BigVGAN v2 44 kHz (NVIDIA, MIT,\n"
        "  https://github.com/NVIDIA/BigVGAN); Whisper small (OpenAI, Apache-2.0, https://github.com/openai/whisper);\n"
        "  RMVPE pitch tracker (lj1995 / RVC project, MIT, https://github.com/RVC-Project/Retrieval-based-Voice-Conversion-WebUI);\n"
        "  CAMPPlus voice-print model (FunASR, Apache-2.0, https://github.com/modelscope/FunASR).\n"
        "Voice blend (BALANCE, NOVELTY) = Ghost Voice Identity Lab v0.6, MIT, Ghost Voice Identity Lab contributors.\n"
        "NATURAL and CLASSIC = GhostBlock's own engines, on WORLD v1.0.1 by Masanori Morise, modified BSD.\n"
        "  https://github.com/mmorise/World\n"
        "Installer's private Python = uv 0.5.11 by Astral, MIT or Apache-2.0. https://github.com/astral-sh/uv\n"
        "CHOP's sound engine = INTERSECT by tucktuckg00se, @ b40c2fb. GPL-3.0. https://github.com/tucktuckg00se/INTERSECT\n"
        "  with its stretch engines: Signalsmith Stretch (Signalsmith Audio, MIT, https://github.com/Signalsmith-Audio/signalsmith-stretch),\n"
        "  Bungee (Parabolic Labs, MPL-2.0, https://github.com/bungee-audio-stretch/bungee) on Eigen (MPL-2.0, https://eigen.tuxfamily.org)\n"
        "  and PFFFT (Julien Pommier, BSD-like); REX2 import: VelociLoops (kunitoki, https://github.com/kunitoki/VelociLoops).\n"
        "CHOP's tempo, key and hit finder, AUTO CHOP's HITS / BEATS / BAR / EVEN = Flayr Chop's analysis by FlayrLabs, @ c7baf83.\n"
        "  AGPL-3.0. https://github.com/FlayrLabs/flayr-chop\n"
        "CHOP's PHRASES = OpenVPI's audio-slicer, @ 9958eed. MIT. https://github.com/openvpi/audio-slicer\n"
        "CHOP's WORDS = WhisperX 3.3.1 by Max Bain. BSD-2-Clause. https://github.com/m-bain/whisperX\n"
        "  Not built in: INSTALL WORD FINDER downloads it (with faster-whisper \"small\", MIT, and the wav2vec2 word aligner) and it runs as a\n"
        "  separate helper on your Mac.\n"
        "CHOP's tools and keys are Block Step's (GhostBlock), which are FL Studio's.\n"
        "Plug-in framework = JUCE 8.0.14 by Raw Material Software, used under its AGPLv3 option. https://github.com/juce-framework/JUCE\n"
        "The window's type: Crimson Pro (The Crimson Pro Project Authors) and Gloock (The Gloock Project Authors), SIL Open Font\n"
        "  License 1.1 (OFL-CrimsonPro.txt, OFL-Gloock.txt in the Licenses folder).\n\n"
        "Test singers in our demos: VocalSet (Wilkins, Seetharaman, Wahl & Pardo, ISMIR 2018, CC BY 4.0).\n\n"
        "TERRAIN, the Spirit Board, the ghost choir art, the themes, the window, MOTION and everything else: GhostBlock, released here\n"
        "under the AGPL-3.0. The full list is in NOTICE.md (in the Licenses folder).");
}

void Editor::showPage (int i)
{
    pageIndex = juce::jlimit (0, kPages - 1, i);
    auto& q = *parts;
    if (q.creditsOn) { q.creditsOn = false; q.creditsBox.setVisible (false); q.creditsClose.setVisible (false); q.creditsFade->setVisible (false); }   // another page closes ABOUT / CREDITS
    auto vis = [] (std::initializer_list<juce::Component*> cs, bool v) { for (auto* c : cs) c->setVisible (v); };
    vis ({ &q.cardA, &q.cardB, &q.cardC, &q.balance, &q.novelty, &q.varDown, &q.varUp, &q.analyze, &q.create, &q.save, &q.useInSong,
           &q.pitch, &q.viewTabs, &q.source, &q.rights }, pageIndex == lab);
    vis ({ &q.engineSel }, pageIndex == lab || pageIndex == sing);
    if (pageIndex != lab) vis ({ &q.quality, &q.follow, &q.tone, &q.timbre, &q.texture, &q.breath, &q.matchRange, &q.promptFree, &q.gpu, &q.keepCons, &q.instant }, false);
    q.perfRec.setVisible (pageIndex == lab && q.source.selected == 0); q.perfAdd.setVisible (pageIndex == lab && q.source.selected == 1);
    vis ({ &q.libView }, pageIndex == library);
    vis ({ &q.songPerf, &q.board, &q.motion, &q.rate, &q.depth, q.sing.get(), &q.progress, &q.take, &q.rights2 }, pageIndex == sing);
    vis ({ &q.terrainView, &q.terrainAmt, q.terrainHint.get() }, pageIndex == tools);
    vis ({ &q.install, &q.reveal, &q.theme, &q.cpuOnly, &q.bubbleToggle }, pageIndex == prefs);
    vis ({ q.hood.get() }, pageIndex == hood);
    vis ({ q.chop.get() }, pageIndex == chop);
    for (auto& nv : q.nav) nv->setVisible (true);   // (polish 1) every page, CHOP too, has the list on the left
    q.about.setVisible (pageIndex == prefs);   // (fix-blur round) the one About & credits entry lives at the foot of Settings only
    for (auto* c : q.chop->sideControls()) c->setVisible (pageIndex == chop);
    q.chop->statusStrip()->setVisible (pageIndex == chop);
    if (pageIndex == tools) q.terrainView.playOpening();
    if (pageIndex == library) proc.reloadLibrary();
    chrome.clear();
    resized();
    refresh();
    for (auto& t : q.tabs) t->repaint();
    for (auto& nv : q.nav) nv->repaint();
}

// ------------------------------------------------------------------------------------------------ library cards
namespace
{
class LibCard : public juce::Component
{
public:
    LibCard (Editor& e, int i) : ed (e), index (i)
    {
        for (auto* b : { &use, &playB, &del }) addAndMakeVisible (*b);
        use.onClick = [this] { ed.proc.useVoice (index); ed.showPage (Editor::lab); };
        playB.onClick = [this] { const auto& v = ed.proc.library[(size_t) index]; if (ed.proc.isPlaying (v.preview)) ed.proc.stopPlaying(); else ed.proc.play (v.preview); };
        del.onClick = [this]
        {
            juce::Component::SafePointer<Editor> sp (&ed); const int k = index;
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Delete voice", "Delete " + ed.proc.library[(size_t) k].name + " from the library?", "DELETE", "CANCEL", nullptr,
                juce::ModalCallbackFunction::create ([sp, k] (int r) { if (r == 1 && sp != nullptr) { sp->proc.deleteVoice (k); sp->showPage (Editor::library); } }));
        };
        use.setButtonText ("USE"); playB.setButtonText ("PLAY"); del.setButtonText ("DELETE");
        use.setTooltip ("Load this voice's recipe into the IDENTITY LAB"); playB.setTooltip ("Hear its preview"); del.setTooltip ("Remove it from the library (asks first)");
    }
    void paint (juce::Graphics& g) override
    {
        const auto& P = ed.cp(); const auto& v = ed.proc.library[(size_t) index];
        auto r = getLocalBounds().toFloat().reduced (4);
        const auto robe = P.glass[(index * 3 + 4) % 8];
        choir::page (g, P, r, v.name == ed.proc.voiceName ? P.voiceC() : juce::Colour());
        auto niche = juce::Rectangle<float> (r.getX() + 12, r.getY() + 12, 62, r.getHeight() - 60);
        g.setColour (robe.withAlpha (0.16f)); g.fillPath (choir::lancet (niche, 0.34f)); g.setColour (P.edgeDim); g.strokePath (choir::lancet (niche, 0.34f), juce::PathStrokeType (1.0f));
        const bool playing = ed.proc.isPlaying (v.preview);
        voiceSinger (g, P, niche.reduced (8, 8).withTrimmedTop (6), robe, playing ? 0.8f : 0.0f, playing ? 1.0f : 0.0f);
        auto t = juce::Rectangle<float> (niche.getRight() + 12, r.getY() + 12, r.getRight() - niche.getRight() - 24, 26);
        g.setColour (P.ink); g.setFont (choir::fitted (choir::disp, v.name, t.getWidth(), 21.0f, 12.0f)); g.drawText (v.name, t, juce::Justification::centredLeft, false);
        const juce::String l1 = v.created.formatted ("%d %b %Y  %H:%M") + "  -  " + engineName (v.engine) + "  -  " + juce::String (juce::roundToInt (v.balance * 100)) + " % B"
                              + (v.engine == Engine::seedvc || v.engine == Engine::best ? "  -  novelty " + juce::String (juce::roundToInt (v.settings.novelty * 100)) + " %" : juce::String());
        g.setColour (P.dim); g.setFont (choir::fitted (choir::regular, l1, t.getWidth(), 14.5f, 9.0f)); g.drawText (l1, t.translated (0, 26).withHeight (20), juce::Justification::centredLeft, false);
        juce::String m = v.identity.valid ? "measured distance  vs A " + juce::String (v.identity.distA(), 2) + "   vs B " + juce::String (v.identity.distB(), 2) : "not measured";
        const bool vu = v.identity.unique() && v.engine != Engine::classic;
        if (vu) m << "   -   UNIQUE";
        g.setColour (vu ? P.good : P.dim); g.setFont (choir::fitted (choir::italic, m, t.getWidth(), 14.5f, 9.0f)); g.drawText (m, t.translated (0, 46).withHeight (20), juce::Justification::centredLeft, false);
    }
    void resized() override { auto r = getLocalBounds().reduced (16, 14).removeFromBottom (28); use.setBounds (r.removeFromLeft (80)); r.removeFromLeft (8); playB.setBounds (r.removeFromLeft (80)); del.setBounds (r.removeFromRight (80)); }
    Editor& ed; int index; juce::TextButton use, playB, del;
};
}

// ------------------------------------------------------------------------------------------------ paint
// everything static of the page shown: the nave, the rood-screen header with the logo, the side chapels (list), the choir's front
// row (bottom strip) and the page's panels with their titles
void Editor::paintChrome (juce::Graphics& g)
{
    const auto& P = cp();
    auto& q = *parts;
    choir::nave (g, P, getLocalBounds());
    // the header: a dark band of carved stone with a gilded rule, a small arcade along its foot
    {
        auto h = juce::Rectangle<float> (0, 0, (float) getWidth(), 78);
        juce::ColourGradient hg (P.dark ? juce::Colour (0xff120e19) : juce::Colour (0xffe9dfca), 0, 0, P.dark ? juce::Colour (0xff1c1626) : juce::Colour (0xfff4ecdb), 0, 78, false);
        g.setGradientFill (hg); g.fillRect (h);
        g.setColour (P.gold); g.fillRect (0.0f, 76.0f, (float) getWidth(), 1.5f);
        g.setColour (P.edgeDim); g.fillRect (0.0f, 73.0f, (float) getWidth(), 1.0f);
        for (float x = 0; x < (float) getWidth(); x += 24) { juce::Path a; a.addCentredArc (x + 12, 76, 10, 6, 0, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, true); g.setColour (P.edgeDim.withMultipliedAlpha (0.5f)); g.strokePath (a, juce::PathStrokeType (0.8f)); }
        // the logo's reserved place (a temporary mark until the logo team's)
        logoMark (g, P, logoSlot.toFloat().reduced (26, 1));   // between the two candles, with even margins
    }
    const bool chopLook = pageIndex == chop && ! q.creditsOn;
    // the side chapels: the list's column, with a rank of organ pipes under the list (on CHOP: the clip's buttons), and the version
    {
        auto col = juce::Rectangle<float> (0, 78, 206, (float) getHeight() - 78 - (hasStrip() ? 70 : 0));
        g.setColour (P.panel.withAlpha (P.dark ? 0.78f : 0.70f)); g.fillRect (col);
        g.setColour (P.edgeDim); g.fillRect (col.getRight() - 1, col.getY(), 1.0f, col.getHeight());
        if (chopLook) choir::title (g, P, juce::Rectangle<float> (18, 92, 170, 24), "The Clip", "");
        // (polish 3) the organ pipes are drawn with the column's facts (paintSide): they grow to fill what the list leaves
        g.setColour (P.dim); g.setFont (serif (14.0f, choir::italic));
        g.drawText (juce::String ("v1 beta") + "  FREE  -  AGPL-3.0", juce::Rectangle<float> (16, col.getBottom() - 28, 180, 22), juce::Justification::centred, false);
    }
    if (chopLook)
    {   // CHOP paints the rest itself; the status strip's ribbon runs on under the resize corner's own place
        const auto rb = juce::Rectangle<int> (cornerBox().getX(), getHeight() - 26, getWidth() - cornerBox().getX(), 26);
        g.setColour (P.panel); g.fillRect (rb);
        g.setColour (P.edgeDim); g.fillRect (rb.getX(), rb.getY(), rb.getWidth(), 1);
        return;
    }
    // the choir's front row: the bottom strip (IDENTITY LAB and SING ENGINE only)
    if (hasStrip())
    {
        auto strip = getLocalBounds().removeFromBottom (70).toFloat();
        juce::ColourGradient sg (P.panelHi, 0, strip.getY(), P.panel, 0, strip.getBottom(), false);
        g.setGradientFill (sg); g.fillRect (strip);
        g.setColour (P.gold); g.fillRect (strip.getX(), strip.getY(), strip.getWidth(), 1.5f);
    }
    // the page's heading (and the ENGINE picker's words)
    if (! q.creditsOn)
    {
        auto content = contentArea.toFloat();
        const bool picker = pageIndex == lab || pageIndex == sing;
        juce::String a, sub;
        switch (pageIndex)
        {
            case lab: a = juce::String (juce::CharPointer_UTF8 ("Identity Lab \xe2\x80\x93 Create a New Voice")); sub = "Fuse two vocal identities into a new singing voice. Every number here is measured."; break;
            case library: a = juce::String (juce::CharPointer_UTF8 ("Voice Library \xe2\x80\x93 Saved Voices")); sub = "Every voice C you saved: its recipe, its takes and what was measured on it."; break;
            case sing: a = juce::String (juce::CharPointer_UTF8 ("Sing Engine \xe2\x80\x93 Generate Singing")); sub = "Your performance, sung by " + proc.voiceName + " (" + engineName (proc.engine) + "). MOTION moves it between A and B with the song."; break;
            case tools: a = juce::String (juce::CharPointer_UTF8 ("Tools \xe2\x80\x93 TERRAIN")); sub = "The land moves " + engineName (proc.engine) + "'s own controls, never a sound of ours. Each move is heard on the next render."; break;
            case hood: a = juce::String (juce::CharPointer_UTF8 ("Under the Hood \xe2\x80\x93 Original Controls")); sub = q.hood->subLine(); break;
            case prefs: a = juce::String (juce::CharPointer_UTF8 ("Settings \xe2\x80\x93 Engine & Look")); sub = "Ghost Voice is free and open source. The voice engine runs on your Mac; nothing is uploaded."; break;
            default: break;
        }
        auto hc = picker ? content.withTrimmedRight ((float) q.engineSel.getWidth() + 20.0f) : content;
        g.setColour (P.ink); g.setFont (choir::fitted (choir::disp, a, hc.getWidth(), 30.0f, 18.0f)); g.drawText (a, hc.withHeight (34), juce::Justification::centredLeft, false);
        g.setColour (P.dim); g.setFont (choir::fitted (choir::italic, sub, hc.getWidth(), 17.0f, 11.0f)); g.drawText (sub, hc.withTrimmedTop (34).withHeight (22), juce::Justification::centredLeft, false);
        if (picker)
        {
            const auto er = q.engineSel.getBounds().toFloat();
            g.setColour (P.gold); g.setFont (display (12.5f)); g.drawText ("ENGINE", er.translated (0, -17).withHeight (15), juce::Justification::centredLeft, false);
            const auto bl = engineBlurb (proc.engine);
            g.setColour (P.ink); g.setFont (choir::fitted (choir::italic, bl, er.getWidth(), 14.5f, 9.0f));
            g.drawText (bl, er.translated (0, er.getHeight() + 2).withHeight (18), juce::Justification::centredRight, false);
        }
        if (pageIndex == hood)
        {
            const juce::String l = q.hood->countLine();
            g.setColour (P.gold); g.setFont (choir::fitted (choir::bold, l, content.getWidth(), 15.0f, 10.0f));
            g.drawText (l, content.withTrimmedTop (58).withHeight (20), juce::Justification::centredLeft, false);
        }
    }
    // the page's panels and their titles
    if (q.creditsOn) { choir::page (g, P, contentArea.toFloat().expanded (4)); return; }   // ABOUT / CREDITS: only its own page shows
    for (auto& pr : panels) choir::page (g, P, pr.r);
    if (pageIndex == lab && panels.size() == 3 && isDsp (proc.engine) && ! q.creditsOn)
    {   // NATURAL / CLASSIC have one switch: what the engine does fills the rest of the panel
        auto r = toggleArea.toFloat().withTrimmedTop (48).withTrimmedBottom (20);   // (polish 2) clear of the rights switch under it
        const juce::String t = proc.engine == Engine::natural
            ? "NATURAL reshapes the performer's own recording toward the new voice. Instant, on your Mac."
            : "CLASSIC re-synthesises the voice from its pitch and timbre: a cleaner, more made voice. Instant, on your Mac.";
        // (polish 3) the rule and the words sit in the middle of the room between the switch and the rights tick (no gap under them)
        juce::AttributedString as; as.append (t, serif (15.0f, choir::italic), P.dim); as.setWordWrap (juce::AttributedString::byWord);
        juce::TextLayout tl; tl.createLayout (as, r.getWidth());
        const float h = juce::jmin (r.getHeight() - 10.0f, tl.getHeight());
        const float y = r.getY() - 8.0f + juce::jmax (0.0f, (r.getHeight() + 8.0f - (h + 10.0f)) * 0.5f);
        g.setColour (P.edgeDim); g.fillRect (r.getX(), y, r.getWidth(), 1.0f);
        tl.draw (g, juce::Rectangle<float> (r.getX(), y + 10.0f, r.getWidth(), h));
    }
    if (pageIndex == prefs && panels.size() == 3 && ! q.creditsOn)
    {
        auto r = panels[0].r.reduced (20, 0).withTrimmedTop (44);
        const auto st = "NEURAL voice engine: " + engineSummary() + "     NATURAL + CLASSIC: built in, nothing to download";
        g.setColour (P.ink); g.setFont (choir::fitted (choir::bold, st, r.getWidth(), 17.0f, 11.0f)); g.drawText (st, r.removeFromTop (24), juce::Justification::centredLeft, false);
        g.setColour (P.dim); g.setFont (serif (15.0f));
        // (polish 1) the folder by a short, friendly name (never a raw developer path); SHOW ENGINE FOLDER opens the real one
        g.drawFittedText ("The neural engine is open-source software that runs as a separate helper on your Mac. About 2.5 GB, downloaded once into "
                          + choir::friendlyPath (engineDir().getFullPathName()) + " (SHOW ENGINE FOLDER opens it).\n"
                          + "NATURAL and CLASSIC are built in. With the voice engine installed, their takes are measured with the same voice-print meter. Who made each part: About & credits, below.",
                          r.removeFromTop (60).toNearestInt(), juce::Justification::topLeft, 3, 1.0f);
        auto lr = panels[1].r.reduced (20, 0);
        g.setColour (P.dim); g.setFont (serif (15.0f, choir::italic));
        g.drawText ("Opens in DARK. A project saved in LIGHT reopens in LIGHT.", juce::Rectangle<float> (lr.getX(), (float) q.theme.getBottom() + 6, lr.getWidth(), 20), juce::Justification::centredLeft, false);
        auto cr = panels[2].r.reduced (20, 0).withTrimmedTop (48).withTrimmedBottom (28);   // (polish 1) the arches stand 12 px clear of the frame
        auto glassRow = cr.removeFromBottom (juce::jmin (170.0f, cr.getHeight() * 0.5f));
        g.setColour (P.ink); g.setFont (serif (16.5f));
        g.drawFittedText ("Use only your own voice, or voices you have the singer's permission to use.\n\nGhost Voice ships no voices and no famous-voice presets, and it will not render until you tick "
                          "\"I have the rights to these voices\".\n\nThe test singers in our demos are credited in About & credits, below.",
                          cr.toNearestInt(), juce::Justification::topLeft, 12, 1.0f);
        // three tall windows: voice A, the new voice, voice B
        const juce::Colour robes[] { P.voiceA(), P.voiceC(), P.voiceB() };
        const float ww = juce::jmin (90.0f, (glassRow.getWidth() - 40) / 3.0f);
        for (int k = 0; k < 3; ++k)
        {
            auto w = juce::Rectangle<float> (glassRow.getCentreX() + ((float) k - 1.0f) * (ww + 20) - ww * 0.5f, glassRow.getY(), ww, glassRow.getHeight());
            auto lp = choir::lancet (w, 0.32f);
            g.setColour (robes[k].withAlpha (0.14f)); g.fillPath (lp); g.setColour (P.edgeDim); g.strokePath (lp, juce::PathStrokeType (1.0f));
            voiceSinger (g, P, w.reduced (12, 14).withTrimmedTop (w.getHeight() * 0.18f), robes[k], 0.0f, k == 1 ? 0.6f : 0.0f);
        }
    }

    auto ttl = [&] (int i, const juce::String& t, const juce::String& num) { if ((int) panels.size() > i) choir::title (g, P, panels[(size_t) i].r.reduced (16, 0).withTrimmedTop (10).withHeight (26), t, num); };
    if (pageIndex == lab) { ttl (0, "Identity Fusion", "i"); ttl (1, "Voice C Preview & Analysis", "ii"); ttl (2, "Singing Engine", "iii"); }
    if (pageIndex == sing) { ttl (0, "The Voices", "i"); ttl (2, "The Spirit Board", "ii"); ttl (1, "The Performance", "iii"); }
    if (pageIndex == library && panels.size() == 2) ttl (1, "The Choir Loft", "");
    if (pageIndex == prefs) { ttl (0, "The Voice Engine", "i"); ttl (1, "The Look", "ii"); ttl (2, "Voices and Consent", "iii"); }
    if (pageIndex == tools) ttl (1, "What the Land Moves", "");
    if (pageIndex == tools && panels.size() == 2)
    {   // under the land: how to work it, in a line
        const juce::String how = "Drag a mountain to move it, drag its line to make it taller  -  SHAPES has ready-made lands  -  the list on the right reads the land";
        g.setColour (P.edgeDim); g.fillRect ((float) terrainHow.getX(), (float) terrainHow.getY(), (float) terrainHow.getWidth(), 1.0f);
        g.setColour (P.dim); g.setFont (choir::fitted (choir::regular, how, (float) terrainHow.getWidth(), 14.5f, 10.0f));
        g.drawText (how, terrainHow.toFloat(), juce::Justification::centred, false);
    }
    if (pageIndex == library && ! proc.library.empty() && panels.size() == 2)
    {
        // under the last saved voice: the next stall, which fills the rest of the list and says how to fill it
        const auto vb = q.libView.getBounds().toFloat();
        const float used = (float) proc.library.size() * 156.0f;
        if (vb.getHeight() - used >= 110.0f)
        {
            auto c = vb.withTrimmedTop (used).reduced (4, 4);
            g.setColour (P.cell.withAlpha (0.22f)); g.fillRoundedRectangle (c, 6);
            { juce::Path d; d.addRoundedRectangle (c, 6); juce::Path dd; const float dash[] { 6.0f, 5.0f }; juce::PathStrokeType (1.0f).createDashedStroke (dd, d, dash, 2); g.setColour (P.edge.withAlpha (0.6f)); g.fillPath (dd); }
            // (polish 1) the card is filled top to bottom: a tall empty stall at the left, three steps spread down the card, and what a
            // stall keeps at its foot
            auto niche = juce::Rectangle<float> (c.getX() + 18, c.getY() + 18, 84, c.getHeight() - 36);
            g.setColour (P.voiceC().withAlpha (0.08f)); g.fillPath (choir::lancet (niche, 0.22f));
            g.setColour (P.edgeDim); g.strokePath (choir::lancet (niche, 0.22f), juce::PathStrokeType (1.0f));
            voiceSinger (g, P, niche.reduced (10, 10).withTrimmedTop (niche.getHeight() * 0.35f), P.voiceC(), 0.0f, 0.0f, 0.35f);
            g.setColour (P.faint); g.setFont (display (16.0f)); g.drawText (kNumerals[juce::jlimit (0, 7, (int) proc.library.size())], niche.withHeight (niche.getHeight() * 0.36f).withTrimmedTop (18), juce::Justification::centred, false);
            auto t = c.withTrimmedLeft (124).reduced (0, 18).withTrimmedRight (18);
            g.setColour (P.ink); g.setFont (display (23.0f)); g.drawText ("The next stall", t.removeFromTop (30), juce::Justification::centredLeft, false);
            g.setColour (P.dim); g.setFont (serif (16.0f, choir::italic)); g.drawText ("how a new voice joins the choir", t.removeFromTop (22), juce::Justification::centredLeft, false);
            auto foot = t.removeFromBottom (44);
            g.setColour (P.edgeDim); g.fillRect (foot.getX(), foot.getY() + 4, foot.getWidth(), 1.0f);
            g.setColour (P.dim); g.setFont (serif (15.0f));
            g.drawFittedText ("Each stall keeps its voice's recipe (engine, balance, novelty), its takes, and what was measured on it.", foot.withTrimmedTop (10).toNearestInt(), juce::Justification::centredLeft, 2, 1.0f);
            const char* heads[] { "Make it", "Hear it", "Keep it" };
            const char* steps[] { "In the IDENTITY LAB, add voice A and voice B and set the balance.",
                                  "Press CREATE NEW VOICE C and listen to the preview.",
                                  "Press SAVE VOICE C: the new voice takes this stall." };
            t.removeFromTop (8);
            const float stepH = t.getHeight() / 3.0f;
            for (int k = 0; k < 3; ++k)
            {
                auto line = t.removeFromTop (stepH);
                auto block = line.withSizeKeepingCentre (line.getWidth(), juce::jmin (line.getHeight(), 46.0f));
                auto num = block.removeFromLeft (34).withSizeKeepingCentre (28, 28);
                g.setColour (P.gold); g.drawEllipse (num, 1.3f); g.setFont (display (16.0f)); g.drawText (juce::String (k + 1), num, juce::Justification::centred, false);
                block.removeFromLeft (8);
                g.setColour (P.ink); g.setFont (serif (17.0f, choir::bold)); g.drawText (heads[k], block.removeFromTop (22), juce::Justification::centredLeft, false);
                g.setColour (P.dim); g.setFont (choir::fitted (choir::regular, steps[k], block.getWidth(), 15.5f, 10.0f)); g.drawText (steps[k], block, juce::Justification::centredLeft, false);
            }
        }
        // the choir loft: a stall per voice (lit, robed in its glass), the empty stalls dim and numbered
        auto lr = panels[1].r.reduced (18, 0).withTrimmedTop (48).withTrimmedBottom (16);
        const int n = (int) proc.library.size();
        const int cols = 4, rowsN = 2;
        auto facts = lr.removeFromBottom (juce::jmin (150.0f, lr.getHeight() * 0.36f));
        const float sw = lr.getWidth() / (float) cols, sh = lr.getHeight() / (float) rowsN;
        for (int i = 0; i < cols * rowsN; ++i)
        {
            auto cell = juce::Rectangle<float> (lr.getX() + sw * (float) (i % cols), lr.getY() + sh * (float) (i / cols), sw, sh).reduced (6, 4);
            auto nm = cell.removeFromBottom (20);
            auto lp = choir::lancet (cell, 0.32f);
            const bool has = i < n;
            const auto robe = P.glass[(i * 3 + 4) % 8];
            g.setColour (robe.withAlpha (has ? (P.dark ? 0.22f : 0.16f) : 0.06f)); g.fillPath (lp);
            g.setColour (has ? P.edge : P.edgeDim); g.strokePath (lp, juce::PathStrokeType (has ? 1.2f : 0.8f));
            if (has)
            {
                const auto& v = proc.library[(size_t) i];
                voiceSinger (g, P, cell.reduced (8, 8).withTrimmedTop (cell.getHeight() * 0.18f), robe, 0.0f, v.name == proc.voiceName ? 0.6f : 0.0f);
                g.setColour (P.ink); g.setFont (choir::fitted (choir::bold, v.name, nm.getWidth(), 14.5f, 9.0f)); g.drawText (v.name, nm, juce::Justification::centred, false);
            }
            else
            {
                g.setColour (P.faint); g.setFont (display (16.0f)); g.drawText (kNumerals[juce::jlimit (0, 7, i)], cell, juce::Justification::centred, false);
                g.setFont (serif (14.0f)); g.drawText (i == n ? "next" : "empty", nm, juce::Justification::centred, false);
            }
        }
        // what the choir holds (all of it read from the saved voices)
        int unique = 0, nat = 0, cla = 0, neu = 0, bst = 0;
        for (auto& v : proc.library)
        {
            unique += v.identity.unique() && v.engine != Engine::classic ? 1 : 0;
            nat += v.engine == Engine::natural; cla += v.engine == Engine::classic; neu += v.engine == Engine::seedvc; bst += v.engine == Engine::best;
        }
        g.setColour (P.edgeDim); g.fillRect (facts.getX(), facts.getY() + 6, facts.getWidth(), 1.0f);
        facts.removeFromTop (14);
        auto fact = [&] (const juce::String& k, const juce::String& v)
        {
            auto line = facts.removeFromTop (26);
            g.setColour (P.dim); g.setFont (serif (15.0f)); g.drawText (k, line, juce::Justification::centredLeft, false);
            g.setColour (P.gold); g.setFont (serif (15.0f, choir::bold)); g.drawText (v, line, juce::Justification::centredRight, false);
        };
        fact ("Voices in the choir", juce::String (n));
        fact ("Measured as a unique identity", juce::String (unique) + " of " + juce::String (n));
        juce::StringArray eng;
        if (neu) eng.add ("NEURAL " + juce::String (neu)); if (nat) eng.add ("NATURAL " + juce::String (nat));
        if (cla) eng.add ("CLASSIC " + juce::String (cla)); if (bst) eng.add ("BEST OF ALL " + juce::String (bst));
        fact ("Sung by", eng.joinIntoString ("  -  "));
        fact ("Stalls free in the loft", juce::String (juce::jmax (0, cols * rowsN - n)) + (n > cols * rowsN ? " (the list scrolls)" : ""));
    }
    if (pageIndex == library && proc.library.empty() && ! panels.empty())
    {   // an empty row of stalls, waiting for voices
        auto r = panels[0].r.reduced (40, 30);
        const int n = 7; const float sw = juce::jmin (100.0f, r.getWidth() / (float) n - 16);
        r = r.withSizeKeepingCentre (r.getWidth(), sw * 1.6f + 150).translated (0, -10);
        {   // a rack of votive candles along the foot of the page
            auto rack = panels[0].r.reduced (30, 20).removeFromBottom (44);
            const int nc = (int) (rack.getWidth() / 26);
            for (int k = 0; k < nc; ++k) choir::candle (g, P, juce::Rectangle<float> (rack.getX() + 6 + (float) k * 26, rack.getY() + (float) (k % 3) * 4.0f, 9, rack.getHeight() - (float) (k % 3) * 4.0f), k % 4 != 1, choir::hash01 (k));
            g.setColour (P.edge); g.fillRect (rack.getX(), rack.getBottom(), rack.getWidth(), 2.0f);
        }
        for (int k = 0; k < n; ++k)
        {
            auto s = juce::Rectangle<float> (r.getCentreX() + ((float) k - 3.0f) * (sw + 16) - sw * 0.5f, r.getY() + 20, sw, sw * 1.6f);
            g.setColour (P.glass[k % 8].withAlpha (0.10f)); g.fillPath (choir::lancet (s, 0.34f)); g.setColour (P.edgeDim); g.strokePath (choir::lancet (s, 0.34f), juce::PathStrokeType (1.0f));
            if (k == 3) voiceSinger (g, P, s.reduced (8, 10).withTrimmedTop (8), P.voiceC(), 0.0f, 0.0f, 0.5f);
        }
        g.setColour (P.ink); g.setFont (display (24.0f));
        g.drawText ("The stalls are empty", r.withTop (r.getY() + 40 + sw * 1.6f).withHeight (34), juce::Justification::centred, false);
        g.setColour (P.dim); g.setFont (serif (17.0f, choir::italic));
        g.drawText ("No saved voices yet. Make one in the IDENTITY LAB and press SAVE VOICE C.", r.withTop (r.getY() + 78 + sw * 1.6f).withHeight (26), juce::Justification::centred, false);
    }
    if (pageIndex == prefs && panels.size() == 3)
    {   // two small windows: the look in the morning and at night
        for (int k = 0; k < 2; ++k)
        {
            const auto& L = choir::pal (k == 1);
            auto w = q.lookPreview[k].toFloat();
            auto lp = choir::lancet (w, 0.32f);
            { juce::Graphics::ScopedSaveState ss (g); g.reduceClipRegion (lp);
              juce::ColourGradient bg (L.bg1, 0, w.getY(), L.bg0, 0, w.getBottom(), false); g.setGradientFill (bg); g.fillRect (w);
              for (int j = 0; j < 3; ++j) { g.setColour (L.glass[(j * 3 + k) % 8].withAlpha (0.28f)); g.fillRect (w.getX() + w.getWidth() * (0.15f + 0.25f * (float) j), w.getY(), w.getWidth() * 0.18f, w.getHeight()); }
              voiceSinger (g, L, w.reduced (w.getWidth() * 0.25f, 12).withTrimmedTop (w.getHeight() * 0.25f), L.glass[4], 0.6f, 0.6f); }
            g.setColour (proc.theme == (k == 1 ? th::Theme::Dark : th::Theme::Light) ? P.goldHi : P.edgeDim); g.strokePath (lp, juce::PathStrokeType (proc.theme == (k == 1 ? th::Theme::Dark : th::Theme::Light) ? 2.0f : 1.0f));
            g.setColour (P.dim); g.setFont (serif (14.0f, choir::italic));
            g.drawText (k == 0 ? "Matins: limestone, sunlit glass" : "Vespers: stone, gold, candle light", w.withY (w.getBottom() + 4).withHeight (18).expanded (30, 0), juce::Justification::centred, false);
        }
        if (q.about.isVisible())
        {   // (fix-blur round) beside About & credits: what it holds, in one line that runs to the page's right edge
            const auto ab = q.about.getBounds().toFloat();
            const auto line = juce::Rectangle<float> (ab.getRight() + 16, ab.getY(), (float) contentArea.getRight() - ab.getRight() - 16 - 20, ab.getHeight());
            const juce::String t = juce::String ("Ghost Voice v1 beta") + "  -  free and open source (AGPL-3.0)  -  who made each part, their licences and source links";
            g.setColour (P.dim); g.setFont (choir::fitted (choir::regular, t, line.getWidth(), 15.0f, 10.0f));
            g.drawText (t, line, juce::Justification::centredLeft, false);
        }
    }
}

void Editor::paint (juce::Graphics& g)
{
    const auto& P = cp();
    auto& q = *parts;
    juce::String key; key << pageIndex << "|" << (int) P.dark << "|" << getWidth() << "x" << getHeight() << "|" << (int) q.creditsOn << "|" << (int) proc.library.size() << "|" << (int) proc.theme;
    for (auto& pr : panels) key << pr.r.toString();
    key << proc.voiceName << (int) proc.engine << engineSummary() << (int) q.theme.selected << toggleArea.toString();
    if (pageIndex == hood && q.hood != nullptr) key << "|hood" << q.hood->tab() << q.hood->subLine() << q.hood->countLine();
    chrome.draw (g, key, getLocalBounds(), [this] (juce::Graphics& ig, juce::Rectangle<int>) { paintChrome (ig); }, true);
    // the two candles by the logo (they flicker; their smoke drifts)
    for (int s : { 0, 1 })
    {
        const float x = s == 0 ? (float) logoSlot.getX() + 10 : (float) logoSlot.getRight() - 18;   // (polish 2) both candles 10 px in from the slot
        const float fl = 0.5f + 0.35f * std::sin (clock * (5.0f + (float) s) + (float) s * 2.0f) + 0.1f * std::sin (clock * 13.0f);
        headerCandle (g, P, juce::Rectangle<float> (x - 1, 26, 10, 44), fl);
        juce::Path smoke; const float sx = x + 4 + (fl - 0.5f) * 2.0f;
        smoke.startNewSubPath (sx, 25);
        smoke.cubicTo (sx + 4 * std::sin (clock * 1.3f + (float) s), 18, sx - 4 * std::sin (clock * 1.7f), 11, sx + 2 * std::sin (clock * 0.9f), 4);
        g.setColour ((P.dark ? P.faint : P.dim).withAlpha (P.dark ? 0.40f : 0.45f)); g.strokePath (smoke, juce::PathStrokeType (1.2f));
    }
    paintSide (g);
    if (pageIndex == chop && ! q.creditsOn) return;
    // the bottom strip: the library count, voices A / B / C as three singers, and the status ribbon (cached until something in it changes)
    if (hasStrip())
    {
        auto first = [&] (int sl) { return proc.takes[sl].isEmpty() ? juce::File() : juce::File (proc.takes[sl][0]); };
        juce::String sk; sk << (int) P.dark << getWidth() << "|" << statusText() << "|" << (int) proc.library.size() << (int) proc.takes[slotA].isEmpty() << (int) proc.takes[slotB].isEmpty()
                            << proc.voiceName << (int) proc.previewIsCurrent() << (int) proc.isPlaying (first (slotA)) << (int) proc.isPlaying (first (slotB)) << (int) proc.isPlaying (proc.preview)
                            << (int) proc.runner.busy() << juce::roundToInt (proc.runner.progress() * 200) << (int) proc.runner.state();
        const auto stripR = getLocalBounds().removeFromBottom (70);
        q.strip.draw (g, sk, stripR, [&, stripR] (juce::Graphics& g2, juce::Rectangle<int>)
        {
            auto& g = g2;
            g.addTransform (juce::AffineTransform::translation (0.0f, (float) -stripR.getY()));
            { const auto sr = stripR.toFloat(); juce::ColourGradient sg (P.panelHi, 0, sr.getY(), P.panel, 0, sr.getBottom(), false); g.setGradientFill (sg); g.fillRect (sr); g.setColour (P.gold); g.fillRect (sr.getX(), sr.getY(), sr.getWidth(), 1.5f); }
        auto strip = getLocalBounds().removeFromBottom (70).toFloat();
        auto s = strip.reduced (18, 8);
        auto lib = s.removeFromLeft (182);
        g.setColour (P.ink); g.setFont (display (19.0f)); g.drawText ("Voice Library", lib.withTrimmedBottom (lib.getHeight() * 0.45f), juce::Justification::bottomLeft, false);
        g.setColour (P.gold); g.setFont (serif (15.0f, choir::italic)); g.drawText (juce::String ((int) proc.library.size()) + (proc.library.size() == 1 ? " voice saved" : " voices saved"), lib.withTrimmedTop (lib.getHeight() * 0.55f), juce::Justification::topLeft, false);
        auto chip = [&] (const juce::String& t, const juce::String& sub, juce::Colour c, bool hi, bool sings)
        {
            auto cr = s.removeFromLeft (184).reduced (4, 0); s.removeFromLeft (6);
            g.setColour (hi ? c.withAlpha (0.16f) : P.cell.withAlpha (0.5f)); g.fillRoundedRectangle (cr, 5);
            g.setColour (hi ? c : P.edgeDim); g.drawRoundedRectangle (cr.reduced (0.5f), 5, 1.0f);
            voiceSinger (g, P, cr.removeFromLeft (40).reduced (4, 3), c, sings ? 0.7f : 0.0f, hi ? 0.6f : 0.0f, hi || sub != "empty" ? 1.0f : 0.55f);
            g.setColour (P.ink); g.setFont (choir::fitted (choir::bold, t, cr.getWidth() - 6, 16.0f, 10.0f)); g.drawText (t, cr.withTrimmedBottom (cr.getHeight() * 0.48f), juce::Justification::bottomLeft, false);
            g.setColour (P.dim); g.setFont (serif (14.0f, choir::italic)); g.drawText (sub, cr.withTrimmedTop (cr.getHeight() * 0.52f), juce::Justification::topLeft, false);
        };
        chip ("Voice A", proc.takes[slotA].isEmpty() ? "empty" : "reference", P.voiceA(), false, proc.isPlaying (first (slotA)));
        chip ("Voice B", proc.takes[slotB].isEmpty() ? "empty" : "reference", P.voiceB(), false, proc.isPlaying (first (slotB)));
        chip (proc.voiceName, proc.previewIsCurrent() ? "new identity" : "not made yet", P.voiceC(), proc.previewIsCurrent(), proc.isPlaying (proc.preview));
        // the status ribbon (real state)
        auto pill = s.reduced (8, 4);
        if (pill.getWidth() > 120)
        {
            const bool bad = proc.runner.state() == Runner::State::failed && ! proc.runner.busy();
            const bool ready = ! proc.runner.busy() && ! bad && proc.previewIsCurrent();
            const auto c = bad ? P.bad : ready ? P.good : P.gold;
            juce::Path rb; rb.startNewSubPath (pill.getX(), pill.getY()); rb.lineTo (pill.getRight(), pill.getY()); rb.lineTo (pill.getRight() - 10, pill.getCentreY()); rb.lineTo (pill.getRight(), pill.getBottom());
            rb.lineTo (pill.getX(), pill.getBottom()); rb.lineTo (pill.getX() + 10, pill.getCentreY()); rb.closeSubPath();
            g.setColour (P.card); g.fillPath (rb); g.setColour (c.withAlpha (0.12f)); g.fillPath (rb); g.setColour (c); g.strokePath (rb, juce::PathStrokeType (1.2f));
            const auto st = statusText();
            auto tr = pill.reduced (22, 2);
            const float fh = choir::fit (choir::regular, st, tr.getWidth(), 15.0f, 11.5f);
            g.setColour (P.ink); g.setFont (serif (fh));
            if (choir::width (serif (fh), st) <= tr.getWidth()) g.drawText (st, tr, juce::Justification::centredLeft, false);
            else g.drawFittedText (st, tr.toNearestInt(), juce::Justification::centredLeft, 2, 1.0f);
            if (proc.runner.busy()) { g.setColour (c); g.fillRect (pill.getX() + 10, pill.getBottom() - 3, (pill.getWidth() - 20) * proc.runner.progress(), 2.0f); }
        }
            }, true);
    }
    if (q.creditsOn) return;
    auto content = contentArea.toFloat();
    if (pageIndex == lab)
    {
        // the arrow between B and C: a gold chevron with a diamond
        const auto ar = juce::Rectangle<float> ((float) q.cardB.getRight(), (float) q.cardB.getY(), (float) (q.cardC.getX() - q.cardB.getRight()), (float) q.cardB.getHeight());
        juce::Path chev;
        for (int k = 0; k < 2; ++k) { const float x0 = ar.getCentreX() - 9 + (float) k * 9; chev.startNewSubPath (x0, ar.getCentreY() - 10); chev.lineTo (x0 + 8, ar.getCentreY()); chev.lineTo (x0, ar.getCentreY() + 10); }
        g.setColour (P.gold); g.strokePath (chev, juce::PathStrokeType (2.4f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
        if (panels.size() == 3)
        {
            // fusion: labels
            auto lab1 = q.balance.getBounds().toFloat();
            g.setColour (P.ink); g.setFont (serif (16.0f, choir::bold)); g.drawText ("Voice Balance", lab1.translated (0, -24).withHeight (20), juce::Justification::centredLeft, false);
            const int pb = juce::roundToInt (proc.blendB() * 100.0f);
            g.setFont (serif (15.0f, choir::bold));
            g.setColour (P.voiceA()); g.drawText ("A " + juce::String (100 - pb) + "%", lab1.withX (lab1.getX() - 60).withWidth (56), juce::Justification::centredRight, false);
            g.setColour (P.voiceB()); g.drawText ("B " + juce::String (pb) + "%", lab1.withX (lab1.getRight() + 6).withWidth (56), juce::Justification::centredLeft, false);
            auto lab2 = q.novelty.getBounds().toFloat();
            const float na = proc.noveltyWorks() ? 1.0f : 0.4f;
            g.setColour (P.ink.withMultipliedAlpha (na)); g.setFont (serif (16.0f, choir::bold)); g.drawText ("Novelty (Uniqueness)", lab2.translated (0, -24).withHeight (20), juce::Justification::centredLeft, false);
            g.setFont (serif (15.0f, choir::bold)); g.drawText (juce::String (juce::roundToInt (proc.settings.novelty * 100)) + "%", lab2.withX (lab2.getRight() + 6).withWidth (44), juce::Justification::centredLeft, false);
            if (proc.noveltyWorks())
            { g.setColour (P.dim); g.setFont (serif (14.0f, choir::italic)); g.drawText ("variation " + juce::String (proc.settings.variation), q.varDown.getBounds().toFloat().withRight ((float) q.varUp.getRight()).translated (0, 25).withHeight (16), juce::Justification::centred, false); }
            else
            {
                const auto why = "Greyed out: " + engineName (proc.engine) + " has no voice space to turn in. NOVELTY needs NEURAL or BEST OF ALL.";
                g.setColour (P.warn); g.setFont (serif (14.0f, choir::italic));
                g.drawFittedText (why, juce::Rectangle<int> (q.novelty.getX(), q.novelty.getBottom() + 3, q.varUp.getRight() - q.novelty.getX(), 34), juce::Justification::topLeft, 2, 1.0f);
            }
            g.setColour (P.gold); g.setFont (display (14.0f));
            g.drawText (isDsp (proc.engine) ? engineName (proc.engine) + " controls" : proc.engine == Engine::best ? "NEURAL controls (NATURAL keeps its own)" : "NEURAL controls",
                        juce::Rectangle<float> ((float) knobRow.getX(), (float) knobRow.getY() - 22, (float) knobRow.getWidth(), 18), juce::Justification::centredLeft, false);
            q.anA.set (proc.takes[slotA].isEmpty() ? juce::File() : juce::File (proc.takes[slotA][0]));
            q.anB.set (proc.takes[slotB].isEmpty() ? juce::File() : juce::File (proc.takes[slotB][0]));
            q.anC.set (proc.preview);
            {   // the view, the distance pipes and the seal: cached until the preview, the measurement or the view changes
                const auto& id0 = proc.previewId;
                const auto area = viewArea.getUnion (idArea).getUnion (badgeArea).toNearestInt().expanded (2);
                juce::String ak; ak << (int) P.dark << q.viewTabs.selected << q.anA.key << q.anB.key << q.anC.key << (int) id0.valid << id0.distA() << id0.distB() << id0.cosAB << id0.cosPerf
                                    << (int) proc.previewEngine << (int) proc.analysisAB.isObject() << area.toString();
                q.analysis.draw (g, ak, area, [&, area] (juce::Graphics& g2, juce::Rectangle<int>)
                {
                    auto& g = g2;
                    g.addTransform (juce::AffineTransform::translation ((float) -area.getX(), (float) -area.getY()));
                // analysis: the view
                auto view = viewArea;
                g.setColour (P.waveBg); g.fillRoundedRectangle (view, 5); g.setColour (P.edgeDim); g.drawRoundedRectangle (view, 5, 1.0f);
                auto inner = view.reduced (12, 10);
                if (q.viewTabs.selected == 0)
                {
                    if (q.anC.peaks.empty()) { g.setColour (P.dim); g.setFont (serif (15.0f, choir::italic)); g.drawText ("voice C's preview appears here", inner, juce::Justification::centred, false); }
                    else q.anC.paintEnvelope (g, inner, P.voiceC(), P.dim, juce::jmax (q.anA.seconds, q.anB.seconds), 14.5f);
                }
                else
                {
                    const bool cmp = q.viewTabs.selected == 2;
                    auto sp = inner.withTrimmedBottom (14);
                    if (cmp) { q.anA.paintSpectrum (g, sp, P.voiceA(), false); q.anB.paintSpectrum (g, sp, P.voiceB(), false); }
                    q.anC.paintSpectrum (g, sp, P.voiceC(), ! cmp);
                    g.setColour (P.dim); g.setFont (serif (12.5f, choir::bold));
                    for (float hz : { 100.0f, 1000.0f, 10000.0f })
                    {
                        const float x = inner.getX() + inner.getWidth() * std::log (hz / 60.0f) / std::log (12000.0f / 60.0f);
                        g.drawText (hz >= 1000 ? juce::String ((int) (hz / 1000)) + " kHz" : juce::String ((int) hz) + " Hz", juce::Rectangle<float> (x - 30, inner.getBottom() - 13, 60, 13), juce::Justification::centred, false);
                    }
                    if (cmp) { g.setFont (serif (14.0f, choir::bold)); g.setColour (P.voiceA()); g.drawText ("A", inner.withSize (14, 14), juce::Justification::centred, false); g.setColour (P.voiceB()); g.drawText ("B", inner.withSize (14, 14).translated (16, 0), juce::Justification::centred, false); g.setColour (P.voiceC()); g.drawText ("C", inner.withSize (14, 14).translated (32, 0), juce::Justification::centred, false); }
                    if (q.anC.spectrumDb.empty() && ! cmp) { g.setColour (P.dim); g.setFont (serif (15.0f, choir::italic)); g.drawText ("no voice C yet", view, juce::Justification::centred, false); }
                }
                // identity distance: two organ pipes, measured (1 - cosine of the voice prints)
                auto idr = idArea;
                g.setColour (P.ink); g.setFont (serif (16.0f, choir::bold)); g.drawText ("Identity Distance", idr.removeFromTop (20), juce::Justification::centredLeft, false);
                const auto& id = proc.previewId;
                auto foot = idr.removeFromBottom (34);
                auto pipes = idr.reduced (0, 2);
                auto onePipe = [&] (juce::Rectangle<float> pr, const juce::String& nm, float d, juce::Colour c)
                {
                    auto label = pr.removeFromLeft (56);
                    g.setColour (P.ink); g.setFont (serif (15.0f, choir::bold)); g.drawText (nm, label.withTrimmedBottom (label.getHeight() * 0.5f), juce::Justification::bottomLeft, false);
                    g.setColour (P.gold); g.drawText (id.valid ? juce::String (d, 2) : "-", label.withTrimmedTop (label.getHeight() * 0.5f), juce::Justification::topLeft, false);
                    // a rank of small pipes lying across: one lit per 0.1 of distance
                    const int n = 10; const float pw = (pr.getWidth() - (float) (n - 1) * 3) / (float) n;
                    for (int k = 0; k < n; ++k)
                    {
                        const float hgt = pr.getHeight() * (0.55f + 0.45f * (float) k / (float) (n - 1));
                        const bool lit = id.valid && d * 10.0f > (float) k + 0.5f;
                        choir::pipe (g, P, juce::Rectangle<float> (pr.getX() + (float) k * (pw + 3), pr.getBottom() - hgt, pw, hgt), lit ? 1.0f : 0.0f, c);
                    }
                };
                onePipe (pipes.removeFromTop (pipes.getHeight() * 0.5f).reduced (0, 2), "vs. A", id.distA(), P.voiceA());
                onePipe (pipes.reduced (0, 2), "vs. B", id.distB(), P.voiceB());
                g.setColour (P.dim); g.setFont (serif (12.5f));
                juce::String ft = id.valid ? "Voice prints, 1 - cosine.  A to B: " + juce::String (1.0f - id.cosAB, 2) + ".  To the guide's singer: " + juce::String (1.0f - id.cosPerf, 2)
                                           : proc.analysisAB.isObject() ? "A to B measured: " + juce::String (1.0 - (double) proc.analysisAB["cos_a_b"], 2) + ".  Create voice C to measure it." : "Measured after CREATE NEW VOICE C (voice prints, 1 - cosine).";
                g.drawFittedText (ft, foot.toNearestInt().withTrimmedTop (-4), juce::Justification::centredLeft, 3, 1.0f);
                // the badge, only when the measurement passes
                auto badge = badgeArea;
                if (id.valid)
                {
                    // CLASSIC measures 0.34-0.41 from A and B even at 0 % / 100 % B on real singers (CI e2e, build 2): its re-synthesis
                    // moves the voice print away from everyone, so passing the rule there says nothing about a new identity
                    const bool classicFar = proc.previewEngine == Engine::classic && id.unique();
                    const bool u = id.unique() && ! classicFar; const auto c = u ? P.good : P.warn;
                    g.setColour (c.withAlpha (0.12f)); g.fillRoundedRectangle (badge, 5); g.setColour (c); g.drawRoundedRectangle (badge, 5, 1.2f);
                    g.setFont (display (16.0f)); g.drawText (u ? "Unique Identity" : classicFar ? "Not counted" : "Not unique yet", badge.reduced (12, 10).removeFromTop (20), juce::Justification::centredLeft, false);
                    g.setColour (P.ink); g.setFont (serif (13.0f));
                    g.drawFittedText (u ? "Measured: voice C is at least 0.30 from A and from B, and it is not the guide's singer."
                                        : classicFar ? "CLASSIC measures this far even at 0 % B (its re-synthesis moves every voice print), so it is not counted."
                                        : "Measured: voice C is still close to " + juce::String (id.cosA > id.cosB ? "A" : "B") + juce::String (id.cosPerf > juce::jmax (id.cosA, id.cosB) ? " (or to the guide's singer)" : "") + ". Try more NOVELTY or a middle BALANCE.",
                                      badge.reduced (12, 10).withTrimmedTop (24).toNearestInt(), juce::Justification::topLeft, 6, 1.0f);
                }
                else
                {   // until it is measured: a closed seal
                    g.setColour (P.edgeDim); g.drawRoundedRectangle (badge, 5, 1.0f);
                    choir::diamond (g, badge.getCentre().translated (0, -12), 9.0f, P.edgeDim);
                    g.setColour (P.dim); g.setFont (serif (14.0f, choir::italic));
                    g.drawFittedText ("The seal appears when voice C is measured", badge.reduced (10, 4).withTrimmedTop (badge.getHeight() * 0.5f).toNearestInt(), juce::Justification::centredTop, 2, 1.0f);
                }

                }, false);
                if (q.viewTabs.selected == 0 && proc.isPlaying (proc.preview) && ! q.anC.peaks.empty())
                {   // the playhead runs live over the cache
                    auto inner = q.anC.envelopeArea (viewArea.reduced (12, 10), juce::jmax (q.anA.seconds, q.anB.seconds), 14.5f);
                    g.setColour (P.play); g.fillRect (inner.getX() + inner.getWidth() * proc.playPosition() - 1.0f, viewArea.getY() + 3, 2.0f, viewArea.getHeight() - 6);
                }
            }
            // singing engine: labels
            g.setColour (P.ink); g.setFont (serif (16.0f, choir::bold));
            g.drawText ("Performance Source", juce::Rectangle<float> ((float) q.source.getX(), (float) q.source.getY() - 22, (float) q.source.getWidth(), 20), juce::Justification::centredLeft, false);
            const auto& pt = proc.takes[slotPerf];
            const juce::String pl = pt.isEmpty() ? "none: the preview sings voice A's first take" : juce::File (pt[0]).getFileName() + "  -  " + juce::String (proc.takeSeconds (slotPerf), 1) + " s";
            g.setColour (P.dim); g.setFont (choir::fitted (choir::italic, pl, (float) q.source.getWidth(), 14.5f, 9.0f));
            g.drawText (pl, juce::Rectangle<float> ((float) q.source.getX(), (float) q.perfRec.getBottom() + 4, (float) q.source.getWidth(), 18), juce::Justification::centredLeft, false);
        }
    }
    else if (pageIndex == sing)
    {
        if (panels.size() >= 2)
        {
            {
                const int pbk = juce::roundToInt (proc.blendB() * 100.0f);
                const bool singingK = proc.isPlaying (proc.result);
                const auto area = q.voicesPanel;
                juce::String vk; vk << (int) P.dark << pbk << proc.voiceName << (int) proc.previewIsCurrent() << (int) singingK << area.toString();
                q.voices.draw (g, vk, area, [&, area] (juce::Graphics& g2, juce::Rectangle<int>)
                {
                    auto& g = g2;
                    g.addTransform (juce::AffineTransform::translation ((float) -area.getX(), (float) -area.getY()));
                    // the voices: A, B and the new C in their stalls, with how much of each the song holds
                    auto vp = q.voicesPanel.toFloat();
                    const int pb = juce::roundToInt (proc.blendB() * 100.0f);
                    const juce::String who[] { "Voice A", "Voice B", proc.voiceName };
                    const juce::String what[] { juce::String (100 - pb) + " %", juce::String (pb) + " %", proc.previewIsCurrent() ? "ready" : "not made" };
                    const juce::Colour robes[] { P.voiceA(), P.voiceB(), P.voiceC() };
                    const float w3 = vp.getWidth() / 3.0f;
                    for (int k = 0; k < 3; ++k)
                    {
                        auto cell = juce::Rectangle<float> (vp.getX() + w3 * (float) k, vp.getY(), w3, vp.getHeight()).reduced (4, 0);
                        auto lp = choir::lancet (cell.withTrimmedBottom (40), 0.3f);
                        g.setColour (robes[k].withAlpha (0.12f)); g.fillPath (lp); g.setColour (P.edgeDim); g.strokePath (lp, juce::PathStrokeType (1.0f));
                        const bool singing = proc.isPlaying (proc.result) || q.sing->isMouseButtonDown();
                        voiceSinger (g, P, cell.withTrimmedBottom (40).reduced (10, 14).withTrimmedTop (10), robes[k], singing ? 0.7f : 0.0f, k == 2 && proc.previewIsCurrent() ? 0.6f : 0.0f);
                        auto t = cell.removeFromBottom (40);
                        g.setColour (P.ink); g.setFont (choir::fitted (choir::bold, who[k], t.getWidth(), 15.0f, 9.0f)); g.drawText (who[k], t.removeFromTop (20), juce::Justification::centred, false);
                        g.setColour (P.gold); g.setFont (serif (14.5f, choir::italic)); g.drawText (what[k], t, juce::Justification::centredTop, false);
                    }

                }, false);
            }
            g.setColour (P.ink); g.setFont (serif (16.0f, choir::bold));
            g.drawText ("YOUR NEW TAKE", juce::Rectangle<float> ((float) q.take.getX(), (float) q.take.getY() - 22, (float) q.take.getWidth(), 20), juce::Justification::centredLeft, false);
            g.setColour (P.dim);
            // (polish 1) the line says it is about THIS take (as it was sung), and says so when the settings have moved on since
            auto info = tidyNumbers (uiWords (proc.resultInfo));
            if (info.isNotEmpty())
            {
                info = "This take: " + info;
                if (info.contains ("MOTION") != proc.motionOn()) info << " - settings changed since: SING IT again";
            }
            const auto ir = juce::Rectangle<float> ((float) q.take.getX(), (float) q.take.getBottom() + 6, (float) q.take.getWidth(), 40);
            if (info.isNotEmpty()) drawParts (g, info, ir, serif (15.0f), 2, juce::Justification::topLeft);
            else { g.setFont (serif (15.0f)); g.drawFittedText ("SING IT renders the whole performance; the take lands here, ready to drag into a track.", ir.toNearestInt(), juce::Justification::topLeft, 2, 1.0f); }
            {
                const int litK = proc.busy() ? (int) (q.progressValue * 16.0) : -1;
                const auto area = q.singPipes.withHeight (q.singPipes.getHeight() + 22);
                juce::String pk; pk << (int) P.dark << litK << (proc.busy() ? proc.progressText() : juce::String()) << area.toString();
                q.pipes.draw (g, pk, area, [&, area] (juce::Graphics& g2, juce::Rectangle<int>)
                {
                    auto& g = g2;
                    g.addTransform (juce::AffineTransform::translation ((float) -area.getX(), (float) -area.getY()));
                    // progress as a rank of organ pipes lighting one by one
                    auto sp = q.singPipes.toFloat();
                    const int n = 16; const float pw = (sp.getWidth() - (float) (n - 1) * 3) / (float) n;
                    const float pv = proc.busy() ? (float) q.progressValue : 0.0f;
                    for (int k = 0; k < n; ++k)
                    {
                        const float hgt = sp.getHeight() * (0.5f + 0.5f * std::abs (std::sin ((float) k * 0.6f)));
                        choir::pipe (g, P, juce::Rectangle<float> (sp.getX() + (float) k * (pw + 3), sp.getBottom() - hgt, pw, hgt), pv * (float) n > (float) k ? 1.0f : 0.0f, P.play);
                    }
                    g.setColour (P.dim); g.setFont (serif (14.0f, choir::italic));
                    g.drawText (proc.busy() ? proc.progressText() : "the pipes fill as it sings", sp.withY (sp.getBottom() + 2).withHeight (18), juce::Justification::centredLeft, false);

                }, false);
            }
            // the board's line and MOTION's labels
            // (polish 1) the same numbers the ghosts on the board show: with MOTION on, where the puck is NOW, and the centre it moves round
            const int pb = juce::roundToInt (proc.blendB() * 100.0f);
            juce::String bl;
            if (proc.motionOn())
            {
                const auto lp = q.board.livePuck ? q.board.livePuck() : juce::Point<float> { proc.board.puckX, proc.board.puckY };
                std::vector<float> w; proc.board.weightsAt (lp.x, lp.y, w);
                const int nb = w.size() >= 2 ? juce::roundToInt (100.0f * w[1] / juce::jmax (1.0e-6f, w[0] + w[1])) : pb;
                bl = "Now " + juce::String (100 - nb) + " % A  -  " + juce::String (nb) + " % B   (MOTION moves it round " + juce::String (100 - pb) + " / " + juce::String (pb) + ")";
            }
            else bl = juce::String (100 - pb) + " % A  -  " + juce::String (pb) + " % B   (MOTION is off)";
            g.setColour (P.ink); g.setFont (choir::fitted (choir::bold, bl, (float) boardLine().getWidth(), 15.0f, 9.0f));
            g.drawText (bl, boardLine().toFloat(), juce::Justification::centredLeft, false);
            const juce::String hl = (proc.motionOn() ? juce::String ((int) proc.makeSongJob().mixes.size()) + " blends are sung. " : juce::String ("One blend is sung. ")) + "Drag the puck, or let MOTION move it.";
            g.setColour (P.dim); g.setFont (choir::fitted (choir::italic, hl, (float) boardLine().getWidth(), 15.0f, 11.0f));
            g.drawText (hl, boardLine().toFloat().translated (0, 20), juce::Justification::centredLeft, false);
            const float ma = proc.motionOn() ? 1.0f : 0.4f;
            g.setColour (P.gold); g.setFont (display (12.5f));
            g.drawText ("MOTION", q.motion.getBounds().toFloat().translated (0, -17).withHeight (15), juce::Justification::centredLeft, false);
            g.setColour (P.gold.withMultipliedAlpha (ma));
            g.drawText ("RATE", q.rate.getBounds().toFloat().translated (0, -17).withHeight (15), juce::Justification::centredLeft, false);
            g.drawText ("DEPTH  " + juce::String (juce::roundToInt (proc.board.orbit.depth * 100)) + " %", q.depth.getBounds().toFloat().translated (0, -17).withHeight (15), juce::Justification::centredLeft, false);
        }
    }
    else if (pageIndex == tools && panels.size() == 2)
    {   // (round 3) what the land moves, live: each of the land's features, the engine control it moves, and by how much now
        const auto l = proc.land();
        struct LandRow { const char* feature; juce::String control, value; float v; int glyph; juce::Colour c; };
        std::vector<LandRow> rows;
        auto pct = [] (float x) { return juce::String (juce::roundToInt (x * 100.0f)) + " %"; };
        const bool dsp = isDsp (proc.engine), best = proc.engine == Engine::best;
        if (! dsp)
        {
            rows.push_back ({ "Tallest mountain", "FOLLOW (guidance)", "+" + juce::String (l.follow, 2), l.follow / 0.3f, 0, P.glass[4] });
            rows.push_back ({ "More land", "QUALITY (steps)", "+" + juce::String (l.steps) + " steps", (float) l.steps / 30.0f, 1, P.glass[1] });
            rows.push_back ({ "Jagged land", "SINGER DETAIL", l.detail ? "ON" : "off", l.detail ? 1.0f : 0.0f, 2, P.glass[0] });
        }
        if (dsp || best)
        {
            rows.push_back ({ "Tallest mountain", juce::String (best ? "NATURAL " : "") + "RESONANCE", "+" + pct (l.resonance), l.resonance / 0.6f, 0, P.glass[6] });
            rows.push_back ({ "More land", juce::String (best ? "NATURAL " : "") + "WARMTH", "+" + pct (l.warmth), l.warmth / 0.5f, 1, P.glass[3] });
            rows.push_back ({ "Jagged land", juce::String (best ? "NATURAL " : "") + "GRIT", "+" + pct (l.grit), l.grit / 0.4f, 2, P.glass[5] });
        }
        rows.push_back ({ "Lean left / right", l.lean < 0 ? "the blend, towards A" : "the blend, towards B", pct (std::abs (l.lean)), std::abs (l.lean) / 0.25f, 3, l.lean < 0 ? P.voiceA() : P.voiceB() });
        rows.push_back ({ "Valleys / spires", "PITCH down / up", (l.semitones > 0 ? "+" : "") + juce::String (l.semitones) + " st", (float) std::abs (l.semitones) / 12.0f, 4, P.glass[7] });
        rows.push_back ({ "Movement", "MOTION depth", pct (l.motionDepth), l.motionDepth / 0.25f, 5, P.glass[2] });
        auto area = terrainRows.toFloat();
        const float rh = juce::jlimit (34.0f, 92.0f, area.getHeight() / (float) rows.size());   // (polish 3) the rows share the whole panel: no gap above TERRAIN AMOUNT
        for (size_t i = 0; i < rows.size(); ++i)
        {
            const auto& w = rows[i];
            auto rr = area.removeFromTop (rh).reduced (0, 3);
            const bool live = w.v > 0.01f;
            if (i > 0) { g.setColour (P.edgeDim.withMultipliedAlpha (0.5f)); g.fillRect (rr.getX(), rr.getY() - 3.5f, rr.getWidth(), 1.0f); }
            // the feature's little drawing
            auto gb = rr.removeFromLeft (34).withSizeKeepingCentre (28, 22);
            juce::Path p;
            switch (w.glyph)
            {
                case 0: p.startNewSubPath (gb.getX(), gb.getBottom()); p.lineTo (gb.getCentreX(), gb.getY()); p.lineTo (gb.getRight(), gb.getBottom()); break;
                case 1: p.startNewSubPath (gb.getX(), gb.getBottom()); p.quadraticTo (gb.getX() + 6, gb.getY() + 4, gb.getCentreX(), gb.getY() + 6); p.quadraticTo (gb.getRight() - 4, gb.getY() + 2, gb.getRight(), gb.getBottom()); break;
                case 2: p.startNewSubPath (gb.getX(), gb.getBottom()); for (int k = 1; k <= 6; ++k) p.lineTo (gb.getX() + gb.getWidth() * (float) k / 6.0f, k % 2 ? gb.getY() + 2 : gb.getBottom() - 6); break;
                case 3: p.startNewSubPath (gb.getX(), gb.getCentreY()); p.lineTo (gb.getRight(), gb.getCentreY());
                        p.startNewSubPath (gb.getX() + 6, gb.getCentreY() - 5); p.lineTo (gb.getX(), gb.getCentreY()); p.lineTo (gb.getX() + 6, gb.getCentreY() + 5);
                        p.startNewSubPath (gb.getRight() - 6, gb.getCentreY() - 5); p.lineTo (gb.getRight(), gb.getCentreY()); p.lineTo (gb.getRight() - 6, gb.getCentreY() + 5); break;
                case 4: p.startNewSubPath (gb.getX(), gb.getCentreY()); p.lineTo (gb.getX() + 7, gb.getBottom()); p.lineTo (gb.getCentreX(), gb.getCentreY()); p.lineTo (gb.getRight() - 7, gb.getY()); p.lineTo (gb.getRight(), gb.getCentreY()); break;
                default: p.startNewSubPath (gb.getX(), gb.getCentreY()); for (int k = 1; k <= 20; ++k) p.lineTo (gb.getX() + gb.getWidth() * (float) k / 20.0f, gb.getCentreY() - 7.0f * std::sin ((float) k * 0.63f)); break;
            }
            g.setColour ((live ? w.c : P.faint)); g.strokePath (p, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            rr.removeFromLeft (8);
            auto top = rr.removeFromTop (rr.getHeight() * 0.5f);
            const auto vf = serif (15.0f, choir::bold);
            const float vw = choir::width (vf, w.value) + 6;
            g.setColour (live ? P.gold : P.faint); g.setFont (vf); g.drawText (w.value, top.removeFromRight (vw), juce::Justification::centredRight, false);
            g.setColour (P.ink); g.setFont (choir::fitted (choir::bold, w.feature, top.getWidth() - 6, 15.0f, 10.0f)); g.drawText (w.feature, top, juce::Justification::centredLeft, false);
            auto bot = rr;
            auto bar = bot.removeFromRight (juce::jmin (110.0f, bot.getWidth() * 0.4f)).withSizeKeepingCentre (juce::jmin (110.0f, bot.getWidth() * 0.4f), 5.0f);
            const juce::String mv = "moves " + w.control;
            g.setColour (P.dim); g.setFont (choir::fitted (choir::regular, mv, bot.getWidth() - 10, 14.5f, 10.0f)); g.drawText (mv, bot, juce::Justification::centredLeft, false);
            g.setColour (P.cell); g.fillRoundedRectangle (bar, 2.5f);
            if (live) { g.setColour (w.c); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, w.v)), 2.5f); }
        }
        g.setColour (P.gold); g.setFont (display (13.0f));
        g.drawText ("TERRAIN AMOUNT  " + juce::String (juce::roundToInt (proc.terrainAmount * 100)) + " %", q.terrainAmt.getBounds().toFloat().translated (0, -19).withHeight (16), juce::Justification::centredLeft, false);
        g.setColour (P.dim); g.setFont (serif (14.5f));
        g.drawText (l.any ? "Each move is heard on the next render." : "Raise a mountain on the land to move these.", q.terrainAmt.getBounds().toFloat().translated (0, 30).withHeight (20), juce::Justification::centredLeft, false);
    }
    else if (pageIndex == hood)
    {
    }
    else if (pageIndex == prefs && panels.size() == 3)
    {
        auto r = panels[0].r.reduced (20, 0);
        auto msg = juce::Rectangle<float> (r.getX(), (float) q.install.getBottom() + 6, r.getWidth(), 20);   // the installer's progress line
        if (! q.cpuOnly.isEnabled()) msg.setRight ((float) q.cpuOnly.getX() - 12.0f);   // (the reason CPU only is greyed sits on its right)
        if (proc.task() == Processor::Task::install || (proc.runner.state() == Runner::State::failed && proc.runner.log().contains ("GV_STEP")))
        { g.setColour (P.ink); g.setFont (serif (15.0f)); g.drawFittedText (uiWords (proc.runner.message()) + "  " + juce::String (juce::roundToInt (proc.runner.progress() * 100)) + " %", msg.toNearestInt(), juce::Justification::centredLeft, 1, 1.0f); }
        else { g.setColour (P.faint); g.setFont (serif (14.5f, choir::italic)); g.drawText ("the installer's progress shows here while it works", msg, juce::Justification::centredLeft, false); }
        if (! q.cpuOnly.isEnabled())
        {   // (polish 3) why CPU only is greyed, in one line under it (as NOVELTY says why it is greyed)
            const auto cb = q.cpuOnly.getBounds().toFloat();
            const juce::String w = "Greyed: install the voice engine first";
            g.setColour (P.dim); g.setFont (choir::fitted (choir::italic, w, juce::jmax (cb.getWidth(), r.getRight() - cb.getX()), 14.5f, 10.0f));
            g.drawText (w, juce::Rectangle<float> (cb.getX() + 2, (float) q.install.getBottom() + 6, r.getRight() - cb.getX() - 2, 20), juce::Justification::centredLeft, false);
        }
    }
}

// ------------------------------------------------------------------------------------------------ layout
// the knobs and switches of the engine in use (IDENTITY LAB): the neural engine's, NATURAL / CLASSIC's, or for BEST OF ALL the neural's
void Editor::layoutEngineControls()
{
    auto& q = *parts;
    const bool lab_ = pageIndex == lab, dsp = isDsp (proc.engine), seed = ! dsp;
    std::vector<Knob*> ks;
    if (seed) ks = { &q.quality, &q.follow, &q.pitch };
    else ks = { &q.tone, &q.timbre, &q.texture, &q.breath, &q.pitch };
    for (auto* k : { &q.quality, &q.follow, &q.tone, &q.timbre, &q.texture, &q.breath }) k->setVisible (false);
    auto kr = knobRow;
    const int kw = kr.getWidth() / juce::jmax (1, (int) ks.size());
    float ns = 14.0f;
    for (auto* k : ks) ns = juce::jmin (ns, choir::fit (choir::bold, k->name, (float) kw - 12.0f, 14.0f, 10.5f));
    for (auto* k : ks) { k->setBounds (kr.removeFromLeft (kw)); k->setVisible (lab_); k->nameSize = ns; k->repaint(); }
    q.pitch.s.setTooltip (seed ? "PITCH: the neural engine's semitone shift of the performance" : "PITCH: the engine's own transpose (the formants stay where they are)");
    std::vector<Candle*> ts;
    if (seed) ts = { &q.matchRange, &q.promptFree, &q.gpu, &q.instant };
    else ts = { &q.keepCons };
    for (auto* t : { &q.matchRange, &q.promptFree, &q.gpu, &q.instant, &q.keepCons }) t->setVisible (false);
    auto r = toggleArea;
    const int th = juce::jlimit (24, 32, (r.getHeight() + 4) / juce::jmax (1, (int) ts.size()) - 4);
    for (auto* t : ts) { t->setBounds (r.removeFromTop (th)); r.removeFromTop (4); t->setVisible (lab_); }
}

void Editor::resized()
{
    auto& q = *parts;
    q.bubbles.setBounds (getLocalBounds()); q.bubbles.toFront (false);
    panels.clear();
    auto all = getLocalBounds();
    auto header = all.removeFromTop (78);
    logoSlot = header.removeFromLeft (410).reduced (10, 3);   // (polish 2) the logo about 30 % bigger
    {   // the hymn board: seven lancet plaques, each as wide as its words need (polish 1: no title runs into its arch), one type size
        auto t = header.reduced (8, 6).withTrimmedLeft (4);
        std::vector<float> need; float sum = 0;
        for (auto& b : q.tabs) { const float w = juce::jmax (choir::width (display (17.0f), b->title), choir::width (serif (14.0f, choir::italic), b->sub)) + 34.0f; need.push_back (w); sum += w; }
        const float spare = juce::jmax (0.0f, (float) t.getWidth() - sum) / (float) q.tabs.size(), squeeze = juce::jmin (1.0f, (float) t.getWidth() / juce::jmax (1.0f, sum));
        float x = (float) t.getX();
        for (size_t i = 0; i < q.tabs.size(); ++i)
        {
            const float w = need[i] * squeeze + spare;
            q.tabs[i]->setBounds (juce::Rectangle<int> (juce::roundToInt (x), t.getY(), juce::roundToInt (x + w) - juce::roundToInt (x), t.getHeight()).reduced (3, 0));
            x += w;
        }
        float ts = 17.0f, ss = 14.0f;
        for (auto& b : q.tabs)
        {
            const float room = b->textRoom().getWidth();
            ts = juce::jmin (ts, choir::fit (choir::disp, b->title, room, 17.0f, 9.0f)); ss = juce::jmin (ss, choir::fit (choir::italic, b->sub, room, 14.0f, 8.5f));
        }
        for (auto& b : q.tabs) { b->sharedTitle = ts; b->sharedSub = ss; }
    }
    if (hasStrip()) all.removeFromBottom (70);
    auto left = all.removeFromLeft (206).reduced (0, 12);
    // (fix-blur round) no ABOUT / CREDITS in the column any more (it is at the foot of Settings): the shortcut moves down into its
    // place, just above the version line, and the column's facts / organ pipes grow to fill what it freed
    for (auto& nv : q.nav) nv->setBounds (left.getX() + 10, getHeight() - (hasStrip() ? 70 : 0) - 32 - 4 - 50, 186, 50);
    {   // CHOP's clip buttons, under THE CLIP at the top of the column
        auto side = q.chop->sideControls();
        const int x = 18, w = 170; int y = 128;
        const int half = (w - 6) / 2;
        side[0]->setBounds (x, y, half, 32); side[1]->setBounds (x + half + 6, y, w - half - 6, 32); y += 40;
        side[2]->setBounds (x, y, w, 32); y += 40;
        side[3]->setBounds (x, y, w, 32);
    }
    contentArea = all.reduced (14, 12);
    auto content = contentArea.withTrimmedTop (64);
    if (q.creditsOn)
    {   // ABOUT / CREDITS sits over the page (the page underneath keeps its own layout)
        auto cr = all.reduced (14, 12);
        q.creditsClose.setBounds (cr.removeFromBottom (32).removeFromRight (120)); cr.removeFromBottom (8);
        q.creditsBox.setBounds (cr.reduced (6, 6));
        q.creditsFade->setBounds (q.creditsBox.getBounds().removeFromBottom (40).withTrimmedRight (q.creditsBox.getScrollBarThickness() + 2));
        const int cw = q.creditsBox.getWidth() - q.creditsBox.getScrollBarThickness() - 4;
        q.credits->setSize (cw, q.credits->layoutFor (cw));
    }
    {   // the ENGINE picker: top right of the IDENTITY LAB and the SING ENGINE
        auto top = contentArea;
        const int w = juce::jlimit (300, 440, top.getWidth() / 2 - 60);
        q.engineSel.setBounds (top.getRight() - w, top.getY() + 16, w, 28);
    }
    if (pageIndex == lab)
    {
        content = contentArea.withTrimmedTop (68);
        auto row = content.removeFromTop (128);
        const int cw = (row.getWidth() - 44) / 3;
        q.cardA.setBounds (row.removeFromLeft (cw)); row.removeFromLeft (4); q.cardB.setBounds (row.removeFromLeft (cw)); row.removeFromLeft (40); q.cardC.setBounds (row);
        content.removeFromTop (10);
        auto bottom = content.removeFromBottom (62);
        content.removeFromBottom (10);
        const int pw = content.getWidth();
        auto p1 = content.removeFromLeft ((int) (pw * 0.355f)); content.removeFromLeft (10);   // (round 3) room for five knob names
        auto p2 = content.removeFromLeft ((int) (pw * 0.345f)); content.removeFromLeft (10);
        auto p3 = content;
        panels.push_back ({ p1.toFloat() }); panels.push_back ({ p2.toFloat() }); panels.push_back ({ p3.toFloat() });
        {
            auto r = p1.reduced (18, 0).withTrimmedTop (62);
            q.balance.setBounds (r.removeFromTop (24).withTrimmedLeft (58).withTrimmedRight (58)); r.removeFromTop (30);
            auto nr = r.removeFromTop (24);
            q.varUp.setBounds (nr.removeFromRight (26)); nr.removeFromRight (4); q.varDown.setBounds (nr.removeFromRight (26)); nr.removeFromRight (52);
            q.novelty.setBounds (nr);
            r.removeFromTop (60);   // the reason under NOVELTY (or the variation) and the knobs' title
            knobRow = r.withTrimmedBottom (12).expanded (8, 0);
        }
        {
            auto r = p2.reduced (16, 0).withTrimmedTop (44);
            q.viewTabs.setBounds (r.removeFromTop (28)); r.removeFromTop (8);
            auto lower = r.removeFromBottom (juce::jmin (164, r.getHeight() - 70)).withTrimmedBottom (12);
            r.removeFromBottom (8);
            viewArea = r.toFloat();
            badgeArea = lower.removeFromRight (lower.getWidth() * 46 / 100).toFloat();
            lower.removeFromRight (12);
            idArea = lower.toFloat();
        }
        {
            auto r = p3.reduced (16, 0).withTrimmedTop (66);
            q.source.setBounds (r.removeFromTop (28)); r.removeFromTop (6);
            auto b = r.removeFromTop (28); q.perfRec.setBounds (b); q.perfAdd.setBounds (b);
            r.removeFromTop (26);
            q.cardPerf.setBounds (0, 0, 0, 0);
            q.rights.setBounds (r.removeFromBottom (32).translated (0, -10));
            toggleArea = r.withTrimmedBottom (16);
        }
        layoutEngineControls();
        {
            auto b = bottom;
            const int bw = b.getWidth();
            q.analyze.setBounds (b.removeFromLeft ((int) (bw * 0.19f)).reduced (0, 12));
            auto right = b.removeFromRight ((int) (bw * 0.36f));
            q.useInSong.setBounds (right.removeFromRight (right.getWidth() / 2).reduced (0, 12).withTrimmedLeft (5)); q.save.setBounds (right.reduced (0, 12).withTrimmedLeft (10).withTrimmedRight (5));
            q.create.setBounds (b.reduced (10, 0));
        }
    }
    else if (pageIndex == library)
    {
        // (round 3) with voices saved: the list of saved voices (right) and the choir loft (left: every saved voice standing in its
        // stall, what the choir holds, and how to add the next voice). With none: one page of empty stalls.
        if (! proc.library.empty())
        {
            auto loft = content.removeFromLeft (content.getWidth() * 40 / 100); content.removeFromLeft (12);
            panels.push_back ({ content.toFloat() }); panels.push_back ({ loft.toFloat() });
        }
        else panels.push_back ({ content.toFloat() });
        content = content.reduced (14, 14);
        q.libView.setBounds (content);
        q.libCards.clear();
        const int cols = 1, cw = content.getWidth() - (proc.library.size() * 156 > (size_t) content.getHeight() ? 12 : 0), ch = 150;
        for (int i = 0; i < (int) proc.library.size(); ++i)
        {
            auto c = std::make_unique<LibCard> (*this, i);
            c->setBounds ((i % cols) * (cw + 4), (i / cols) * (ch + 6), cw, ch);
            q.libContent.addAndMakeVisible (*c); q.libCards.push_back (std::move (c));
        }
        q.libContent.setSize (cw, ((int) proc.library.size() + cols - 1) / cols * (ch + 6));
    }
    else if (pageIndex == sing)
    {
        content = contentArea.withTrimmedTop (68);
        auto left2 = content.removeFromLeft (290);
        q.songPerf.setBounds (left2.removeFromTop (128)); left2.removeFromTop (8);
        q.rights2.setBounds (left2.removeFromTop (32).reduced (4, 0)); left2.removeFromTop (8);
        panels.push_back ({ left2.toFloat() });
        q.voicesPanel = left2.reduced (14, 0).withTrimmedTop (44).withTrimmedBottom (12);
        content.removeFromLeft (12);
        auto right2 = content.removeFromRight (310);
        panels.push_back ({ right2.toFloat() });
        {
            auto r = right2.reduced (16, 0).withTrimmedTop (46).withTrimmedBottom (14);
            q.sing->setBounds (r.removeFromTop (84)); r.removeFromTop (12);
            auto pr = r.removeFromTop (30);
            q.stop.setBounds (pr.removeFromRight (80)); pr.removeFromRight (8);
            q.progress.setBounds (pr.withSizeKeepingCentre (pr.getWidth(), 10));
            if (! q.stop.isVisible()) q.progress.setBounds (pr.withWidth (pr.getWidth() + 88).withSizeKeepingCentre (pr.getWidth() + 88, 10));
            r.removeFromTop (8);
            q.singPipes = r.removeFromTop (52);
            r.removeFromTop (30);
            r.removeFromTop (22);
            q.take.setBounds (r.removeFromTop (juce::jmax (80, r.getHeight() - 50)));
        }
        content.removeFromRight (12);
        // (polish 3) the board's column is a panel like i and ii: the board (its own 14 px margin) inside, MOTION at its foot
        panels.push_back ({ content.toFloat() });
        auto col = content.reduced (2, 2);
        auto m = col.removeFromBottom (32 + 14).withTrimmedBottom (14).reduced (14, 0);
        const int mw = m.getWidth();
        q.motion.setBounds (m.removeFromLeft (juce::jmin (124, mw * 2 / 5))); m.removeFromLeft (10);
        q.rate.setBounds (m.removeFromLeft (juce::jmin (96, mw * 3 / 10))); m.removeFromLeft (14);
        q.depth.setBounds (m);
        col.removeFromBottom (62);
        q.board.setBounds (col.withTrimmedTop (36));
    }
    else if (pageIndex == tools)
    {   // (round 3) TERRAIN fills the page: the land (left) and what it moves, live (right), with TERRAIN AMOUNT under that
        content = contentArea.withTrimmedTop (64);
        auto right = content.removeFromRight (juce::jmax (330, content.getWidth() * 34 / 100));
        content.removeFromRight (12);
        panels.push_back ({ content.toFloat() });   // a gilded hymn-book frame round the land
        panels.push_back ({ right.toFloat() });
        q.terrainView.setBounds (content.reduced (9).withTrimmedBottom (30));
        q.terrainHint->setBounds (q.terrainView.getBounds().withTrimmedTop (60));   // (under SHAPES and the land's own toolbar)
        q.terrainHint->toFront (false);
        terrainHow = content.reduced (16, 9).removeFromBottom (30);
        auto rr = right.reduced (18, 0);
        q.terrainAmt.setBounds (rr.removeFromBottom (62).removeFromTop (30).withTrimmedTop (2));
        terrainRows = rr.withTrimmedTop (46).withTrimmedBottom (34);
    }
    else if (pageIndex == hood)
    {
        q.hood->setBounds (content.withTrimmedTop (22).withBottom (juce::jmin (content.getBottom(), cornerBox().getY())));   // (fixes) clear of the resize corner
    }
    else if (pageIndex == prefs)
    {
        auto r = content;
        // (fix-blur round) the foot of Settings: the one small About & credits entry, the version and licence line beside it
        auto foot = r.removeFromBottom (30); r.removeFromBottom (10);
        q.about.setBounds (foot.getX(), foot.getY(), 170, 30);
        auto p1 = r.removeFromTop (juce::jmin (216, r.getHeight() / 2 - 14)); r.removeFromTop (12);
        auto p2 = r.removeFromLeft (r.getWidth() / 2 - 6); r.removeFromLeft (12);
        panels.push_back ({ p1.toFloat() }); panels.push_back ({ p2.toFloat() }); panels.push_back ({ r.toFloat() });
        {
            // (polish 1) the buttons follow the words (no empty band above them); the installer's line under them, clear of the frame
            auto b = p1.reduced (20, 0).withTrimmedTop (44 + 24 + 60 + 6).withHeight (32);
            q.install.setBounds (b.removeFromLeft (250)); b.removeFromLeft (10); q.reveal.setBounds (b.removeFromLeft (210)); b.removeFromLeft (24);
            q.cpuOnly.setBounds (b.removeFromLeft (juce::jmin (330, b.getWidth())));
        }
        {
            auto lr = p2.reduced (20, 0).withTrimmedTop (48).withTrimmedBottom (18);   // (round 3) never jammed at the panel's foot
            q.theme.setBounds (lr.removeFromTop (32).withWidth (juce::jmin (300, lr.getWidth())));
            lr.removeFromTop (34);
            q.bubbleToggle.setBounds (lr.removeFromBottom (34).withTrimmedBottom (4).withWidth (juce::jmin (300, lr.getWidth())));
            lr.removeFromBottom (26);
            const int pw = juce::jmin (110, (lr.getWidth() - 40) / 2), ph = lr.getHeight() - 4;
            q.lookPreview[0] = juce::Rectangle<int> (lr.getCentreX() - pw - 30, lr.getY(), pw, ph);
            q.lookPreview[1] = juce::Rectangle<int> (lr.getCentreX() + 30, lr.getY(), pw, ph);
        }
    }
    if (q.chop != nullptr)
    {   // CHOP: everything right of the list and under the header
        auto cb = getLocalBounds().withTrimmedTop (78).withTrimmedLeft (206);
        // (fixes) the status strip under the page stops where the resize corner's own place starts
        q.chop->statusStrip()->setBounds (cb.removeFromBottom (26).withRight (cornerBox().getX()));
        q.chop->setBounds (cb);
        if (q.creditsOn) { q.creditsBox.toFront (false); q.creditsFade->toFront (false); q.creditsClose.toFront (false); }
        q.bubbles.toFront (false);
    }
    repaint();
}
}
