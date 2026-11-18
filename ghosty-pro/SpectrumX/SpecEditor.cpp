#include "SpecEditor.h"
#include "SpecSliders.h"
#include "SpecGuide.h"
#include "SpecTheme.h"
#include "GhostArt.h"
#include "TypeIn.h"
#include "LabPanel.h"
#include "SpecTabs.h"
#include <map>
#include "ArrangePage.h"
#include "ArcsPage.h"
#include "ArcsFactory.h"
#include "LabProcessPage.h"
#include "SpecVoiceTab.h"
#include "GhostVoicePage.h"

namespace {
// the palette (SpecTheme.h): opaque surfaces, values follow DARK / LIGHT at paint time
juce::Colour &BG = sp::pal().bg, &CARD = sp::pal().panel, &CARD2 = sp::pal().panel2, &INK = sp::pal().ink, &GREY = sp::pal().grey, &DIM = sp::pal().dim,
             &PURPLE = sp::pal().purple, &VIOLET = sp::pal().violet, &GREEN = sp::pal().green, &PINK = sp::pal().pink, &AMBER = sp::pal().amber, &CYAN = sp::pal().cyan, &RED = sp::pal().red;
constexpr int W = 1200, H = 790;
juce::Colour layerColour(int i) { const juce::Colour C[] = { GREEN, PINK, CYAN, AMBER, PURPLE, juce::Colour(0xfff87171), juce::Colour(0xffa3e635), juce::Colour(0xff60a5fa) }; return C[(size_t) (i % 8)]; }
// each brush has its own colour; the tone (x) pushes it from soft / pale (pure) to hot / saturated (bright, driven)
juce::Colour brushColour(int v) { const juce::Colour C[] = { AMBER, CYAN, juce::Colour(0xffb36bff), juce::Colour(0xffff6b4a) }; return C[(size_t) juce::jlimit(0, 3, v)]; }
juce::Colour dotColour(int v, float tone) { const auto c = brushColour(v); return c.withSaturation(juce::jlimit(0.45f, 1.0f, c.getSaturation() * (0.7f + 0.4f * tone))).brighter(0.15f * (1.0f - tone)); }
const char* brushBlurb(int v) {
    static const char* B[] = { "Rings out and fades slowly. Inharmonic overtones: glassy on the left, a big church bell on the right.",
                               "Grand-piano strings. Related notes ring each other. Soft felt on the left, hard bright hammer on the right.",
                               "A bowed swell that holds as long as you like. Warm and pure on the left, bright and grainy on the right.",
                               "Guitar feedback near the amp: it blooms, wobbles and climbs the overtones. Drag it to bend. Wilder to the right." };
    return B[(size_t) juce::jlimit(0, 3, v)];
}
// the aurora palette for the live spectrum: bass = magenta, mids = blue / cyan / green, air = gold; loud = white-hot
juce::Colour aurora(float yNorm, float m) {
    float hue = 0.92f - yNorm * 0.8f; if (hue < 0) hue += 1.0f;
    return juce::Colour::fromHSV(hue, juce::jlimit(0.0f, 1.0f, 0.95f - m * m * 0.7f), juce::jlimit(0.0f, 1.0f, 0.45f + m * 0.8f), juce::jlimit(0.0f, 1.0f, std::pow(m, 1.7f) * 1.25f));
}
juce::String noteName(int m) { static const char* N[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; return juce::String(N[((m % 12) + 12) % 12]) + juce::String(m / 12 - 1); }

struct LnF : juce::LookAndFeel_V4 {
    LnF() { applyColours(); }
    void applyColours() {   // again after DARK / LIGHT
        setColour(juce::ComboBox::backgroundColourId, CARD2); setColour(juce::ComboBox::textColourId, INK); setColour(juce::ComboBox::arrowColourId, PURPLE);
        setColour(juce::PopupMenu::backgroundColourId, CARD2); setColour(juce::PopupMenu::textColourId, INK); setColour(juce::PopupMenu::highlightedBackgroundColourId, PURPLE.withAlpha(0.35f));
        setColour(juce::TextButton::textColourOffId, INK); setColour(juce::TextButton::textColourOnId, INK); setColour(juce::Label::textColourId, INK);
        setColour(juce::TooltipWindow::backgroundColourId, CARD2); setColour(juce::TooltipWindow::textColourId, INK); setColour(juce::TooltipWindow::outlineColourId, PURPLE);
        setColour(juce::TextEditor::backgroundColourId, CARD2); setColour(juce::TextEditor::textColourId, INK); setColour(juce::TextEditor::highlightColourId, PURPLE.withAlpha(0.5f));
    }
    void drawButtonBackground(juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override {
        auto r = b.getLocalBounds().toFloat().reduced(1); const bool on = b.getToggleState() || (bool) b.getProperties()["accent"];
        const auto tint = b.getProperties().contains("tint") ? juce::Colour((juce::uint32) (juce::int64) b.getProperties()["tint"]) : PURPLE;
        if (on) { g.setGradientFill(juce::ColourGradient(fillA(tint), r.getX(), r.getY(), fillB(tint), r.getRight(), r.getBottom(), false)); g.fillRoundedRectangle(r, 9); }
        else { g.setColour(sp::pal().field); g.fillRoundedRectangle(r, 9); }
        g.setColour(on ? onTint(tint).withAlpha(over ? 0.95f : 0.5f) : tint.withAlpha(over ? 0.95f : 0.6f)); g.drawRoundedRectangle(r, 9, down ? 2.0f : 1.1f);
    }
    // a filled (on) button: on LIGHT the fill is deepened so a light label reads on it; on the dark looks the neon stays and a dark label reads
    static juce::Colour fillA(juce::Colour tint) { return sp::pal().dark ? tint : ghosty::themes::readableOn(tint, sp::tokens().window, 4.8); }
    static juce::Colour fillB(juce::Colour tint) { return sp::pal().dark ? tint.interpolatedWith(PINK, 0.45f) : ghosty::themes::readableOn(tint.interpolatedWith(PINK, 0.45f), sp::tokens().window, 4.8); }
    static juce::Colour onTint(juce::Colour tint) { return sp::textOn(fillA(tint), fillB(tint), sp::tokens()); }   // a label that reads on the filled button
    void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) override {
        const bool on = b.getToggleState() || (bool) b.getProperties()["accent"];
        const auto tint = b.getProperties().contains("tint") ? juce::Colour((juce::uint32) (juce::int64) b.getProperties()["tint"]) : PURPLE;
        g.setFont(getTextButtonFont(b, b.getHeight())); g.setColour(on ? onTint(tint) : (b.isEnabled() ? INK : DIM));
        g.drawFittedText(b.getButtonText(), b.getLocalBounds().reduced(4, 2), juce::Justification::centred, 1, 0.8f);
    }
    juce::Font getTextButtonFont(juce::TextButton&, int h) override { return juce::FontOptions(juce::jmin(13.0f, h * 0.46f), juce::Font::bold); }
    void drawComboBox(juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& cb) override {
        // a page with its own theme (ARRANGE, ARCS light) sets the box colours: honour them; Spectrum's own boxes keep the house look
        auto pick = [&cb](int id, juce::Colour def) { return cb.isColourSpecified(id) ? cb.findColour(id) : def; };
        auto r = juce::Rectangle<float>(0, 0, (float) w, (float) h).reduced(1);
        g.setColour(pick(juce::ComboBox::backgroundColourId, sp::pal().field)); g.fillRoundedRectangle(r, 9);
        g.setColour(pick(juce::ComboBox::outlineColourId, PURPLE.withAlpha(0.5f))); g.drawRoundedRectangle(r, 9, 1.1f);
        juce::Path a; a.addTriangle((float) w - 16, h * 0.5f - 2, (float) w - 8, h * 0.5f - 2, (float) w - 12, h * 0.5f + 3); g.setColour(pick(juce::ComboBox::arrowColourId, PURPLE)); g.fillPath(a);
    }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return juce::FontOptions(12.5f, juce::Font::bold); }
    void drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override {
        auto r = juce::Rectangle<float>((float) x, (float) y, (float) w, (float) h).reduced(3); const float sz = juce::jmin(r.getWidth(), r.getHeight()); auto c = r.withSizeKeepingCentre(sz, sz);
        const auto tint = s.getProperties().contains("tint") ? juce::Colour((juce::uint32) (juce::int64) s.getProperties()["tint"]) : GREEN;
        const float th = 4, ang = a0 + pos * (a1 - a0);
        g.setGradientFill(juce::ColourGradient(tint.withAlpha(0.18f * pos + 0.04f), c.getCentreX(), c.getCentreY(), tint.withAlpha(0.0f), c.getCentreX(), c.getY() - 6, true)); g.fillEllipse(c.expanded(5));
        g.setColour(sp::pal().knobFace); g.fillEllipse(c.reduced(th + 1));
        juce::Path tr; tr.addCentredArc(c.getCentreX(), c.getCentreY(), sz / 2 - th / 2, sz / 2 - th / 2, 0, a0, a1, true); g.setColour(sp::pal().track); g.strokePath(tr, juce::PathStrokeType(th));
        juce::Path ar; ar.addCentredArc(c.getCentreX(), c.getCentreY(), sz / 2 - th / 2, sz / 2 - th / 2, 0, a0, ang, true);
        g.setGradientFill(juce::ColourGradient(PURPLE, c.getX(), c.getBottom(), tint, c.getRight(), c.getY(), false)); g.strokePath(ar, juce::PathStrokeType(th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(INK); g.drawLine({ c.getCentre().getPointOnCircumference(sz * 0.12f, ang), c.getCentre().getPointOnCircumference(sz / 2 - th - 4, ang) }, 2.0f);
    }
};
struct Knob : juce::Component {
    juce::Slider s { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox }; juce::Label l;
    explicit Knob(const juce::String& n, juce::Colour tint = GREEN) {
        s.setPopupDisplayEnabled(true, true, nullptr); s.getProperties().set("tint", (juce::int64) tint.getARGB()); addAndMakeVisible(s);
        l.setText(n, juce::dontSendNotification); l.setJustificationType(juce::Justification::centred);
        l.setFont(juce::FontOptions(10.0f, juce::Font::bold)); l.setColour(juce::Label::textColourId, GREY); l.setInterceptsMouseClicks(false, false); addAndMakeVisible(l); }
    void resized() override { auto r = getLocalBounds(); l.setBounds(r.removeFromBottom(14)); s.setBounds(r); }
    void lookAndFeelChanged() override { l.setColour(juce::Label::textColourId, GREY); }   // the label follows the look
};
void tintButton(juce::Button& b, juce::Colour c) { b.getProperties().set("tint", (juce::int64) c.getARGB()); }
} // namespace

// =====================================================================================================
// THE CANVAS
//   PAD mode:  y = pitch, x = tone colour. Every dot keeps ringing; drag it to hear it beat against the others.
//   LOOP mode: y = frequency (20 Hz .. 20 kHz), x = time in the loop.
struct Canvas : juce::Component, juce::DragAndDropTarget {
    // the canvas is a dark night sky in BOTH themes: everything drawn on it uses the dark palette (these shadow the themed names)
    static const sp::Palette& dk() { static const sp::Palette p = sp::Palette::make(true); return p; }
    static inline const juce::Colour &BG = dk().bg, &CARD = dk().panel, &CARD2 = dk().panel2, &INK = dk().ink, &GREY = dk().grey, &DIM = dk().dim,
                                     &PURPLE = dk().purple, &VIOLET = dk().violet, &GREEN = dk().green, &PINK = dk().pink, &AMBER = dk().amber, &CYAN = dk().cyan, &RED = dk().red;
    SpecProcessor& proc;
    enum Tool { Draw, Drop, Connect, Move, Erase } tool = Draw;
    int currentLayer = -1;                              // the sound DRAW uses
    std::function<int()> ensureLayer;                    // make a layer for the selected bank sound if needed
    std::function<void(int layer, double beat, float pitch)> onDropBank;
    std::function<void()> onEdit, onNeedSound;
    std::function<void(const juce::String&)> onSay;
    // visuals
    juce::Image bgImg, spec, bloom; std::vector<juce::Image> ghostImgs; float clockT = 0; int frame = 0; double lastBeat = -1; int lastMode = -1;
    juce::dsp::FFT fftObj { 12 }; std::vector<float> fftBuf = std::vector<float>(8192), rowMag, bloomMag;
    std::map<uint32_t, float> glow;                      // smoothed loudness per dot (by id)
    float loudness = 0;
    // editing
    std::vector<juce::Point<float>> stroke; int moveNote = -1, connectFrom = -1, dragDot = -1, hoverDot = -1; bool moveLen = false, rightErase = false; sp::Note orig; juce::Point<float> down;
    static constexpr float kLoopLo = 16, kLoopHi = 135, kPadLo = 30, kPadHi = 105;

    explicit Canvas(SpecProcessor& p) : proc(p) {
        for (int i = 0; i < 6; ++i) {   // ghost sprites for the drifting ghost world
            juce::Image im(juce::Image::ARGB, 180, 180, true); juce::Graphics g(im);
            ghosty::drawGhost(g, ghosty::lookFor(i * 6 + 2), 90, 92, 120); ghostImgs.push_back(im);
        }
    }
    bool padMode() const { return proc.model.mode == 0; }
    float lo() const { return padMode() ? kPadLo : kLoopLo; }
    float hi() const { return padMode() ? kPadHi : kLoopHi; }
    double beats() const { return proc.model.bars * 4.0; }
    float xOf(double b) const { return (float) (b / beats() * getWidth()); }
    double beatOf(float x) const { return juce::jlimit(0.0, beats(), (double) x / getWidth() * beats()); }
    float yOf(float midi) const { return (float) getHeight() * (1.0f - (midi - lo()) / (hi() - lo())); }
    float midiOf(float y) const { return lo() + (1.0f - y / (float) getHeight()) * (hi() - lo()); }
    float toneOf(float x) const { return juce::jlimit(0.0f, 1.0f, (x - 14.0f) / juce::jmax(1.0f, getWidth() - 28.0f)); }
    float xOfTone(float t) const { return 14.0f + t * (getWidth() - 28.0f); }
    double grid() const { return 4.0 / juce::jmax(1, proc.model.gridDiv); }
    double snapB(double b) const { return proc.model.quantize ? std::floor(b / grid() + 1e-6) * grid() : b; }
    float pitchFor(float y, int layer) const {
        float m = midiOf(y);
        if (layer >= 0 && layer < (int) proc.model.layers.size() && proc.model.layers[(size_t) layer].kind == 1) return std::round(m);   // drums: tune by height
        return proc.model.snap ? sp::snapPitch(m, proc.model.root, proc.model.scale) : std::round(m);
    }

    // ---------------------------------------------------------------- the ghost world background (cached)
    void resized() override { buildBackground(); clearSpectrum(); }
    void clearSpectrum() {
        spec = juce::Image(juce::Image::ARGB, juce::jmax(1, getWidth() / 2), juce::jmax(1, getHeight() / 2), true);
        bloom = juce::Image(juce::Image::ARGB, juce::jmax(1, getWidth() / 4), juce::jmax(1, getHeight() / 4), true);
        rowMag.assign((size_t) spec.getHeight(), 0.0f); bloomMag.assign((size_t) bloom.getHeight(), 0.0f); lastBeat = -1;
    }
    void buildBackground() {
        const int w = juce::jmax(1, getWidth()), h = juce::jmax(1, getHeight());
        bgImg = juce::Image(juce::Image::ARGB, w, h, true); juce::Graphics g(bgImg);
        juce::ColourGradient base(juce::Colour(0xff150a36), 0, 0, juce::Colour(0xff031a22), 0, (float) h, false); base.addColour(0.5, juce::Colour(0xff0b1036)); g.setGradientFill(base); g.fillRect(0, 0, w, h);
        juce::Random r(42); const auto dk = sp::Palette::make(true); const juce::Colour neb[] = { dk.pink, dk.violet, dk.cyan, dk.green, juce::Colour(0xff6d28d9), dk.amber, dk.pink, dk.cyan };   // the night sky stays dark in both themes
        for (int i = 0; i < 9; ++i) {
            const float cx = r.nextFloat() * w, cy = r.nextFloat() * h, rad = 140.0f + r.nextFloat() * 260.0f; const auto c = neb[(size_t) (i % 8)];
            juce::ColourGradient gr(c.withAlpha(0.10f + r.nextFloat() * 0.12f), cx, cy, c.withAlpha(0.0f), cx + rad, cy, true); gr.addColour(0.45, c.withAlpha(0.06f));
            g.setGradientFill(gr); g.fillEllipse(cx - rad, cy - rad, rad * 2, rad * 2);
        }
        for (int i = 0; i < 260; ++i) { const float x = r.nextFloat() * w, y = r.nextFloat() * h, s = r.nextFloat() < 0.08f ? 2.2f : 1.2f; g.setColour(juce::Colours::white.withAlpha(0.15f + r.nextFloat() * 0.5f)); g.fillEllipse(x, y, s, s); }
        juce::ColourGradient vig(juce::Colours::transparentBlack, w * 0.5f, h * 0.5f, juce::Colours::black.withAlpha(0.55f), 0, 0, true); g.setGradientFill(vig); g.fillRect(0, 0, w, h);
    }

    // ---------------------------------------------------------------- live spectrum -> aurora (half-res image + a soft bloom image)
    void tick() {
        clockT += 1.0f / 30.0f; ++frame;
        if (lastMode != proc.model.mode) { lastMode = proc.model.mode; clearSpectrum(); }
        feedSpectrum();
        for (auto& d : proc.model.pads) { auto& gl = glow[d.id]; const float l = proc.padLevel(d); gl += (l - gl) * (l > gl ? 0.5f : 0.08f); }
    }
    void feedSpectrum() {
        if (!spec.isValid()) return;
        const int N = 4096, end = proc.engine.ringPos.load(std::memory_order_acquire); float e = 0;
        for (int i = 0; i < N; ++i) { const float x = proc.engine.ring[(end - N + i + sp::SpecEngine::kRing) % sp::SpecEngine::kRing]; e += x * x; fftBuf[(size_t) i] = x * (0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * i / (N - 1))); }
        loudness += (std::sqrt(e / N) * 4.0f - loudness) * 0.2f;
        std::fill(fftBuf.begin() + N, fftBuf.end(), 0.0f);
        fftObj.performFrequencyOnlyForwardTransform(fftBuf.data());
        const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0;
        auto magAt = [&](float yFull0, float yFull1) {   // strongest bin between two canvas rows, in 0..1
            const float b0 = (float) (sp::midiToHz(midiOf(yFull0)) / sr * N), b1 = (float) (sp::midiToHz(midiOf(yFull1)) / sr * N);
            float m = 0;
            if (b1 - b0 < 1.0f) { const float bc = juce::jlimit(1.0f, N / 2 - 2.0f, (b0 + b1) * 0.5f); const int k = (int) bc; const float f = bc - k; m = fftBuf[(size_t) k] + (fftBuf[(size_t) k + 1] - fftBuf[(size_t) k]) * f; }
            else for (int k = juce::jlimit(1, N / 2 - 1, (int) b0); k <= juce::jlimit(1, N / 2 - 1, (int) b1); ++k) m = juce::jmax(m, fftBuf[(size_t) k]);
            return juce::jlimit(0.0f, 1.0f, (juce::Decibels::gainToDecibels(m / (N * 0.25f) + 1e-7f) + 64.0f) / 56.0f);
        };
        auto writeColumns = [&](juce::Image& im, std::vector<float>& mags, int x0, int x1, float decay) {
            const int h = im.getHeight(); const float sc = (float) getHeight() / h;
            juce::Image::BitmapData bd(im, juce::Image::BitmapData::readWrite);
            for (int y = 0; y < h; ++y) {
                const float m = magAt((y + 1.0f) * sc, (float) y * sc); auto& sm = mags[(size_t) y]; sm = juce::jmax(m, sm * decay);
                const auto c = aurora(1.0f - (float) y / h, sm);
                for (int x = juce::jmax(0, x0); x < x1 && x < im.getWidth(); ++x) bd.setPixelColour(x, y, c);
            }
        };
        if (padMode()) {   // the aurora flows right -> left behind the dots
            spec.moveImageSection(0, 0, 1, 0, spec.getWidth() - 1, spec.getHeight());
            writeColumns(spec, rowMag, spec.getWidth() - 1, spec.getWidth(), 0.80f);
            if (frame % 2 == 0) { bloom.moveImageSection(0, 0, 1, 0, bloom.getWidth() - 1, bloom.getHeight()); writeColumns(bloom, bloomMag, bloom.getWidth() - 1, bloom.getWidth(), 0.75f); }
            return;
        }
        const float beat = proc.engine.uiBeat.load(); if (beat < 0) { lastBeat = -1; return; }   // LOOP: paint the column under the playhead
        const float b0 = (float) (lastBeat < 0 || beat < lastBeat ? beat : lastBeat);
        const float fx0 = (float) (b0 / beats()), fx1 = (float) (beat / beats());
        writeColumns(spec, rowMag, (int) (fx0 * spec.getWidth()), juce::jmax((int) (fx0 * spec.getWidth()) + 1, (int) (fx1 * spec.getWidth())), 0.6f);
        writeColumns(bloom, bloomMag, (int) (fx0 * bloom.getWidth()), juce::jmax((int) (fx0 * bloom.getWidth()) + 1, (int) (fx1 * bloom.getWidth())), 0.6f);
        lastBeat = beat;
    }

    // ---------------------------------------------------------------- paint
    void paintWorld(juce::Graphics& g) {
        g.drawImageAt(bgImg, 0, 0);
        for (int i = 0; i < (int) ghostImgs.size(); ++i) {   // translucent ghosts drifting through the nebula
            const float sp = 9.0f + i * 4.0f, sz = 0.45f + 0.12f * (i % 3), span = getWidth() + 260.0f;
            const float x = std::fmod(i * 233.0f + clockT * sp * (i % 2 ? -1.0f : 1.0f) + span * 8, span) - 130.0f;
            const float y = 40.0f + std::fmod(i * 157.0f, juce::jmax(60.0f, getHeight() - 160.0f)) + std::sin(clockT * 0.45f + i) * 26.0f;
            g.setOpacity(juce::jlimit(0.0f, 0.4f, 0.10f + 0.03f * (i % 3) + loudness * 0.12f));
            g.drawImageTransformed(ghostImgs[(size_t) i], juce::AffineTransform::translation(-90, -90).scaled(i % 2 ? -sz : sz, sz).rotated(std::sin(clockT * 0.3f + i) * 0.12f).translated(x, y));
        }
        g.setOpacity(1.0f);
        g.setImageResamplingQuality(juce::Graphics::mediumResamplingQuality);
        g.setOpacity(0.75f); g.drawImage(bloom, getLocalBounds().toFloat().expanded(6));
        g.setOpacity(0.85f); g.drawImage(spec, getLocalBounds().toFloat());
        g.setOpacity(1.0f);
    }
    void paint(juce::Graphics& g) override {
        juce::Path clip; clip.addRoundedRectangle(getLocalBounds().toFloat(), 14); g.saveState(); g.reduceClipRegion(clip);
        paintWorld(g);
        if (padMode()) paintPad(g); else paintLoop(g);
        paintHint(g);
        g.restoreState();
        g.setColour(PURPLE.withAlpha(0.35f)); g.drawRoundedRectangle(getLocalBounds().toFloat().reduced(0.5f), 14, 1.2f);
    }
    void paintHint(juce::Graphics& g) {
        const juce::String t = padMode() ? "CLICK  paint a ringing note     DRAG  bend it and hear it beat     RIGHT-CLICK  remove     WHEEL  louder / softer"
                                         : "LEFT  paint / drag notes     RIGHT-CLICK  delete     DRAG A NOTE'S END  longer";
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold)); const float w = ghosty::textWidth(g.getCurrentFont(), t) + 24;
        auto r = juce::Rectangle<float>(10, 8, w, 22); g.setColour(juce::Colour(0xff0a0620).withAlpha(0.62f)); g.fillRoundedRectangle(r, 11);
        g.setColour(PURPLE.withAlpha(0.45f)); g.drawRoundedRectangle(r, 11, 1.0f); g.setColour(INK.withAlpha(0.88f)); g.drawText(t, r, juce::Justification::centred);
    }

    // --- PAD: scale rows, tone zones, relations (beating / calm), overtone ladders, dots
    void paintPad(juce::Graphics& g) {
        const float Wd = (float) getWidth(), Hd = (float) getHeight();
        const auto& sc = sp::scaleSteps(proc.model.scale);
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        for (int m = (int) std::ceil(lo()); m <= (int) hi(); ++m) {
            const int rel = ((m - proc.model.root) % 12 + 12) % 12; const bool inKey = std::find(sc.begin(), sc.end(), rel) != sc.end(); const float y = yOf((float) m);
            if (rel == 0) { g.setColour(GREEN.withAlpha(0.13f)); g.fillRect(0.0f, y - 0.5f, Wd, 1.2f); g.setColour(GREEN.withAlpha(0.65f)); g.drawText(noteName(m), (int) Wd - 40, (int) y - 12, 34, 11, juce::Justification::right); }
            else if (proc.model.snap && inKey) { g.setColour(INK.withAlpha(0.045f)); g.drawHorizontalLine((int) y, 0, Wd); }
            if (m % 12 == 0 && rel != 0) { g.setColour(GREY.withAlpha(0.5f)); g.drawText(noteName(m), (int) Wd - 40, (int) y - 12, 34, 11, juce::Justification::right); }
        }
        // tone zones along the bottom
        auto strip = juce::Rectangle<float>(14, Hd - 22, Wd - 28, 14);
        juce::ColourGradient tg(CYAN.withAlpha(0.35f), strip.getX(), 0, RED.withAlpha(0.45f), strip.getRight(), 0, false); tg.addColour(0.5, PURPLE.withAlpha(0.35f));
        g.setGradientFill(tg); g.fillRoundedRectangle(strip, 7);
        g.setColour(INK.withAlpha(0.85f)); g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
        const char* zones[] = { "PURE", "SOFT", "WARM", "BRIGHT", "DRIVEN" };
        for (int i = 0; i < 5; ++i) g.drawText(zones[i], strip.withWidth(strip.getWidth() / 5).translated(i * strip.getWidth() / 5, 0), juce::Justification::centred);
        // loop progress (the pattern loops; bells and strings ring again each time round)
        const float beat = proc.engine.uiBeat.load();
        if (beat >= 0) { const float p = (float) (beat / beats()); g.setColour(INK.withAlpha(0.12f)); g.fillRect(0.0f, 0.0f, Wd, 3.0f); g.setGradientFill(juce::ColourGradient(CYAN, 0, 0, PINK, Wd, 0, false)); g.fillRect(0.0f, 0.0f, Wd * p, 3.0f); }

        auto& D = proc.model.pads; const int nd = (int) D.size();
        auto lvl = [this](const sp::PadDot& d) { auto it = glow.find(d.id); return it == glow.end() ? 0.0f : it->second; };
        // relations between dots: find the closest pair of overtones; exact = calm glow, a little off = shimmering interference
        for (int i = 0; i < nd; ++i) for (int j = i + 1; j < nd; ++j) {
            const auto& A = D[(size_t) i]; const auto& B = D[(size_t) j]; const float fa = sp::midiToHz(A.pitch), fb = sp::midiToHz(B.pitch);
            float bestDev = 9, beatHz = 0; int ba = 1, bb = 1;
            for (int a = 1; a <= 5; ++a) for (int b = 1; b <= 5; ++b) {
                const float dev = std::abs(12.0f * std::log2(a * fa / (b * fb)));
                if (dev < 0.7f && dev + 0.02f * (a * b) < bestDev) { bestDev = dev + 0.02f * (a * b); ba = a; bb = b; beatHz = std::abs(a * fa - b * fb); }
            }
            if (bestDev > 0.75f) continue;
            const float dev = std::abs(12.0f * std::log2(ba * fa / (bb * fb)));
            const juce::Point<float> pa { xOfTone(A.tone), yOf(A.pitch + 12.0f * std::log2((float) ba)) }, pb { xOfTone(B.tone), yOf(B.pitch + 12.0f * std::log2((float) bb)) };
            const float act = 0.6f + 0.4f * juce::jmax(lvl(A), lvl(B)), weight = 1.0f / std::sqrt((float) (ba * bb));
            if (dev < 0.05f) {   // consonant: a calm golden thread
                juce::Path p; p.startNewSubPath(pa); p.quadraticTo((pa + pb) * 0.5f + juce::Point<float>(0, -18.0f), pb);
                g.setColour(AMBER.withAlpha(0.16f * act * weight)); g.strokePath(p, juce::PathStrokeType(8.0f));
                g.setColour(AMBER.withAlpha(0.7f * act * weight)); g.strokePath(p, juce::PathStrokeType(1.4f));
                continue;
            }
            // beating: wavy interference lines; colour from slow shimmer (cyan) to rough clash (pink / red)
            const float rate = juce::jlimit(0.4f, 9.0f, beatHz), rough = juce::jlimit(0.0f, 1.0f, (beatHz - 3.0f) / 30.0f);
            const auto col = CYAN.interpolatedWith(PINK, juce::jmin(1.0f, rough * 1.6f)).interpolatedWith(RED, juce::jmax(0.0f, rough - 0.6f) * 2.0f);
            const float len = pa.getDistanceFrom(pb); if (len < 2) continue;
            const auto dir = (pb - pa) / len; const juce::Point<float> nrm { -dir.y, dir.x };
            const float cycles = juce::jlimit(2.0f, 30.0f, len / 14.0f), amp = (3.0f + 6.0f * rough) * act * weight + 1.0f;
            for (int k = 0; k < 3; ++k) {
                juce::Path p; const float ph = clockT * rate * juce::MathConstants<float>::twoPi * 0.5f + k * 2.1f;
                for (int s = 0; s <= 48; ++s) { const float u = s / 48.0f; const float env = std::sin(u * juce::MathConstants<float>::pi);
                    const auto q = pa + (pb - pa) * u + nrm * (std::sin(u * cycles * juce::MathConstants<float>::twoPi + ph) * amp * env * (1.0f - 0.25f * k));
                    if (s == 0) p.startNewSubPath(q); else p.lineTo(q); }
                g.setColour(col.withAlpha((0.42f - 0.1f * k) * act * weight)); g.strokePath(p, juce::PathStrokeType(k == 0 ? 1.6f : 1.0f));
            }
            g.setColour(col.withAlpha(0.18f * act)); g.fillEllipse(juce::Rectangle<float>(10, 10).withCentre((pa + pb) * 0.5f).expanded(2.0f * std::abs(std::sin(clockT * rate * 3.14159f))));
        }
        // overtone ladders, then the dots
        for (int i = 0; i < nd; ++i) {
            const auto& d = D[(size_t) i]; const float x = xOfTone(d.tone), y = yOf(d.pitch), l = lvl(d); const auto col = dotColour(d.voice, d.tone);
            const auto P = sp::designPartials(d.voice, d.tone, sp::midiToHz(d.pitch));
            float top = y;
            for (int k = 0; k < P.count; ++k) {
                if (std::abs(P.ratio[k] - 1.0f) < 0.01f) continue;
                const float oy = yOf(d.pitch + 12.0f * std::log2(P.ratio[k])); if (oy < 0 || oy > getHeight()) continue;
                top = juce::jmin(top, oy); const float a = std::sqrt(P.amp[k]);
                const float r = 2.0f + 3.0f * a + 3.0f * l * a;
                g.setColour(col.withAlpha((0.10f + 0.35f * l) * a)); g.fillEllipse(juce::Rectangle<float>(r * 3, r * 3).withCentre({ x, oy }));
                g.setColour(col.withAlpha((0.35f + 0.55f * l) * a)); g.fillEllipse(juce::Rectangle<float>(r, r).withCentre({ x, oy }));
            }
            if (top < y - 4) { g.setGradientFill(juce::ColourGradient(col.withAlpha(0.28f + 0.3f * l), x, y, col.withAlpha(0.0f), x, top, false)); g.fillRect(x - 0.75f, top, 1.5f, y - top); }
        }
        for (int i = 0; i < nd; ++i) {
            const auto& d = D[(size_t) i]; const float x = xOfTone(d.tone), y = yOf(d.pitch), l = lvl(d); const auto col = dotColour(d.voice, d.tone);
            const float gl = juce::jmax(l, 0.25f + 0.25f * d.gain), rad = 12.0f + 36.0f * gl;
            juce::ColourGradient halo(col.withAlpha(0.18f + 0.5f * gl), x, y, col.withAlpha(0.0f), x + rad, y, true); halo.addColour(0.35, col.withAlpha(0.12f + 0.25f * gl));
            g.setGradientFill(halo); g.fillEllipse(x - rad, y - rad, rad * 2, rad * 2);
            const float r = 6.0f + 3.0f * d.gain;
            juce::ColourGradient core(juce::Colours::white, x - r * 0.3f, y - r * 0.3f, col, x + r, y + r, true); g.setGradientFill(core); g.fillEllipse(x - r, y - r, r * 2, r * 2);
            g.setColour(col.brighter(0.4f));
            switch (d.voice) {   // a little mark for the brush
                case sp::VBell: g.drawEllipse(x - r - 3, y - r - 3, r * 2 + 6, r * 2 + 6, 1.2f); break;
                case sp::VPiano: g.fillRect(x - 0.75f, y - r - 7, 1.5f, r * 2 + 14); break;
                case sp::VFeedback: { juce::Path zz; for (int s = 0; s <= 16; ++s) { const float a = s / 16.0f * juce::MathConstants<float>::twoPi, rr = r + 4 + (s % 2 ? 2.5f : 0.0f) + 1.5f * l * std::sin(clockT * 9 + s);
                    const juce::Point<float> q { x + std::cos(a) * rr, y + std::sin(a) * rr }; if (s == 0) zz.startNewSubPath(q); else zz.lineTo(q); } g.strokePath(zz, juce::PathStrokeType(1.2f)); } break;
                default: break;
            }
            if (i == dragDot || i == hoverDot) { g.setColour(INK.withAlpha(0.9f)); g.drawEllipse(x - r - 6, y - r - 6, r * 2 + 12, r * 2 + 12, 1.4f);
                g.setFont(juce::FontOptions(11.0f, juce::Font::bold)); const int m = (int) std::round(d.pitch); const float cents = (d.pitch - m) * 100.0f;
                g.drawText(noteName(m) + (std::abs(cents) >= 1 ? juce::String(cents > 0 ? " +" : " ") + juce::String((int) std::round(cents)) + "c" : juce::String()) + "  " + sp::voiceName(d.voice), (int) x + 14, (int) y - 22, 140, 14, juce::Justification::left); }
        }
        if (D.empty()) {
            const auto box = getLocalBounds().withSizeKeepingCentre(700, 150).translated(0, -6).toFloat();
            g.setGradientFill(juce::ColourGradient(juce::Colour(0xff07041a).withAlpha(0.7f), box.getCentreX(), box.getCentreY(), juce::Colour(0xff07041a).withAlpha(0.0f), box.getCentreX(), box.getY() - 30, true));
            g.fillEllipse(box.expanded(40, 30));
            g.setColour(INK.withAlpha(0.92f)); g.setFont(juce::FontOptions(20.0f, juce::Font::bold));
            g.drawFittedText("Click anywhere to paint a note that rings.", getLocalBounds().withSizeKeepingCentre(620, 40).translated(0, -36), juce::Justification::centred, 1);
            g.setColour(GREY); g.setFont(juce::FontOptions(14.0f));
            g.drawFittedText("Higher = higher pitch.   Left = pure, right = bright and driven.\nAdd more notes and drag them close together: hear them beat, then settle into harmony.\nPick a brush on the left (BELL, PIANO, PAD, FEEDBACK) or try a STARTING POINT.",
                             getLocalBounds().withSizeKeepingCentre(660, 80).translated(0, 20), juce::Justification::centred, 4);
        }
    }

    // --- LOOP: frequency lines, beats, glides, notes, stroke, playhead
    void paintLoop(juce::Graphics& g) {
        auto R = getLocalBounds().toFloat();
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        for (float hz : { 30.f, 60.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f, 16000.f }) {
            const float y = yOf(sp::hzToMidi(hz)); g.setColour(juce::Colours::white.withAlpha(0.06f)); g.drawHorizontalLine((int) y, 0, R.getRight());
            g.setColour(GREY.withAlpha(0.7f)); g.drawText(hz >= 1000 ? juce::String(hz / 1000, 0) + "k" : juce::String((int) hz), (int) R.getRight() - 34, (int) y - 12, 30, 11, juce::Justification::right);
        }
        g.setColour(GREY.withAlpha(0.6f)); g.drawText("BASS", 8, (int) yOf(sp::hzToMidi(60)) - 12, 60, 11, juce::Justification::left);
        g.drawText("MIDS", 8, (int) yOf(sp::hzToMidi(800)) - 12, 60, 11, juce::Justification::left); g.drawText("AIR", 8, (int) yOf(sp::hzToMidi(9000)) - 12, 60, 11, juce::Justification::left);
        if (proc.model.quantize) for (double b = 0; b < beats(); b += grid()) { g.setColour(juce::Colours::white.withAlpha(0.025f)); g.drawVerticalLine((int) xOf(b), 0, R.getBottom()); }
        for (int b = 0; b <= (int) beats(); ++b) { g.setColour(juce::Colours::white.withAlpha(b % 4 == 0 ? 0.18f : 0.07f)); g.drawVerticalLine((int) xOf(b), 0, R.getBottom()); }
        auto& N = proc.model.notes; const float beat = proc.engine.uiBeat.load();
        for (size_t i = 0; i < N.size(); ++i) {
            if (!N[i].glide) continue;
            int prev = -1; double best = -1; for (size_t j = 0; j < N.size(); ++j) if (N[j].layer == N[i].layer && N[j].start < N[i].start && N[j].start > best) { best = N[j].start; prev = (int) j; }
            if (prev < 0) continue;
            const auto& a = N[(size_t) prev]; juce::Path p; const float ax = xOf(a.start + a.len), ay = yOf(a.pitch), bx = xOf(N[i].start), by = yOf(N[i].pitch);
            p.startNewSubPath(ax, ay); p.cubicTo(ax + (bx - ax) * 0.5f, ay, ax + (bx - ax) * 0.5f, by, bx, by);
            g.setColour(layerColour(a.layer).withAlpha(0.35f)); g.strokePath(p, juce::PathStrokeType(7.0f)); g.setColour(layerColour(a.layer)); g.strokePath(p, juce::PathStrokeType(2.0f));
        }
        for (size_t i = 0; i < N.size(); ++i) {
            const auto& n = N[i]; if (n.layer >= (int) proc.model.layers.size()) continue;
            const auto col = layerColour(n.layer); const bool drum = proc.model.layers[(size_t) n.layer].kind == 1;
            const bool on = beat >= 0 && beat >= n.start && beat < n.start + juce::jmax(0.2, n.len);
            auto r = juce::Rectangle<float>(xOf(n.start), yOf(n.pitch) - 6, juce::jmax(8.0f, xOf(n.start + n.len) - xOf(n.start) - 2), 12);
            if (drum) r = juce::Rectangle<float>(16, 16).withCentre({ xOf(n.start) + 6, yOf(n.pitch) });
            const float e = on ? 14.0f : 6.0f;
            g.setGradientFill(juce::ColourGradient(col.withAlpha(on ? 0.55f : 0.28f), r.getCentreX(), r.getCentreY(), col.withAlpha(0.0f), r.getCentreX() + r.getWidth() * 0.5f + e, r.getCentreY(), true));
            g.fillRoundedRectangle(r.expanded(e), 12);
            g.setColour(col.withAlpha(0.5f + 0.5f * n.vel)); if (drum) g.fillEllipse(r); else g.fillRoundedRectangle(r, 6);
            g.setColour(juce::Colours::white.withAlpha(on ? 0.75f : 0.3f)); if (drum) g.fillEllipse(r.reduced(5)); else g.fillRoundedRectangle(r.reduced(2, 4), 2);
            if ((int) i == connectFrom) { g.setColour(INK); g.drawRoundedRectangle(r.expanded(3), 8, 2); }
        }
        if (stroke.size() > 1) { juce::Path p; p.startNewSubPath(stroke[0]); for (auto& q : stroke) p.lineTo(q); g.setColour(layerColour(juce::jmax(0, currentLayer)).withAlpha(0.85f)); g.strokePath(p, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded)); }
        if (beat >= 0) { const float x = xOf(beat); g.setGradientFill(juce::ColourGradient(INK.withAlpha(0.0f), x - 18, 0, INK.withAlpha(0.18f), x, 0, false)); g.fillRect(x - 18, 0.0f, 18.0f, R.getHeight()); g.setColour(INK.withAlpha(0.85f)); g.fillRect(x - 1, 0.0f, 2.0f, R.getHeight()); }
        if (N.empty()) {
            const auto box = getLocalBounds().withSizeKeepingCentre(600, 110).toFloat();
            g.setGradientFill(juce::ColourGradient(juce::Colour(0xff07041a).withAlpha(0.7f), box.getCentreX(), box.getCentreY(), juce::Colour(0xff07041a).withAlpha(0.0f), box.getCentreX(), box.getY() - 30, true));
            g.fillEllipse(box.expanded(40, 30));
            g.setColour(INK.withAlpha(0.9f)); g.setFont(juce::FontOptions(17.0f, juce::Font::bold));
            g.drawFittedText("Pick a sound on the left, then DRAW a line here.\nHigher = higher pitch. Left to right = time.\nOr drag any sound straight onto the canvas.", getLocalBounds().reduced(60), juce::Justification::centred, 4);
        }
    }

    // ---------------------------------------------------------------- mouse
    int noteAt(juce::Point<float> p, bool& onEdge) const {
        auto& N = proc.model.notes; onEdge = false;
        for (int i = (int) N.size() - 1; i >= 0; --i) {
            const auto& n = N[(size_t) i]; const float x0 = xOf(n.start), x1 = juce::jmax(x0 + 12, xOf(n.start + n.len)), y = yOf(n.pitch);
            if (p.x >= x0 - 4 && p.x <= x1 + 2 && std::abs(p.y - y) < 10) { onEdge = x1 - p.x < 8; return i; }
        }
        return -1;
    }
    int dotAt(juce::Point<float> p) const {
        auto& D = proc.model.pads; int best = -1; float bd = 15;
        for (int i = 0; i < (int) D.size(); ++i) { const float d = p.getDistanceFrom({ xOfTone(D[(size_t) i].tone), yOf(D[(size_t) i].pitch) }); if (d < bd) { bd = d; best = i; } }
        return best;
    }
    void eraseAt(juce::Point<float> p) {
        if (padMode()) { const int h = dotAt(p); if (h >= 0) { proc.model.pads.erase(proc.model.pads.begin() + h); hoverDot = -1; proc.syncPads(); repaint(); if (onEdit) onEdit(); } return; }
        bool edge; const int h = noteAt(p, edge); if (h >= 0) { proc.model.notes.erase(proc.model.notes.begin() + h); edited(); }
    }
    void mouseMove(const juce::MouseEvent& e) override { if (padMode()) { const int h = dotAt(e.position); if (h != hoverDot) { hoverDot = h; repaint(); } } }
    void mouseExit(const juce::MouseEvent&) override { hoverDot = -1; }
    void mouseDown(const juce::MouseEvent& e) override {
        down = e.position;
        // FL-style: right click (two-finger click on a Mac trackpad, or ctrl-click) deletes what's under the cursor, any tool
        if (e.mods.isPopupMenu()) { rightErase = true; eraseAt(e.position); return; }
        if (padMode()) { padDown(e); return; }
        bool edge = false; const int hit = noteAt(e.position, edge);
        if (tool == Draw || tool == Drop) {
            if (hit >= 0) { moveNote = hit; orig = proc.model.notes[(size_t) hit]; moveLen = edge; return; }   // grab a note to move / stretch it
            if (tool == Drop) { const int l = ensureLayer ? ensureLayer() : currentLayer; if (l < 0) { if (onNeedSound) onNeedSound(); return; } addNote(l, snapB(beatOf(e.position.x)), pitchFor(e.position.y, l)); return; }
            stroke = { e.position }; return;
        }
        if (tool == Erase) { eraseAt(e.position); return; }
        if (tool == Move) { moveNote = hit; if (hit >= 0) { orig = proc.model.notes[(size_t) hit]; moveLen = edge; } return; }
        if (tool == Connect) {
            if (hit < 0) { connectFrom = -1; repaint(); return; }
            if (connectFrom < 0) { connectFrom = hit; repaint(); return; }
            auto& N = proc.model.notes;
            if (N[(size_t) connectFrom].layer == N[(size_t) hit].layer && hit != connectFrom) {
                const int fi = N[(size_t) connectFrom].start < N[(size_t) hit].start ? connectFrom : hit, si = fi == hit ? connectFrom : hit;
                N[(size_t) si].glide = true; N[(size_t) fi].len = juce::jmax(0.125, N[(size_t) si].start - N[(size_t) fi].start);
                edited();
            }
            connectFrom = -1; repaint();
        }
    }
    void padDown(const juce::MouseEvent& e) {
        const int hit = dotAt(e.position);
        if (hit >= 0) { dragDot = hit; proc.engine.padStopped.store(false); return; }
        if ((int) proc.model.pads.size() >= sp::kMaxDots) { if (onSay) onSay("32 notes is the most a pad can hold. Right-click one to remove it."); return; }
        sp::PadDot d; d.tone = toneOf(e.position.x); d.voice = proc.model.brush; d.gain = 0.8f;
        d.pitch = proc.model.snap ? sp::snapPitch(midiOf(e.position.y), proc.model.root, proc.model.scale) : midiOf(e.position.y);
        proc.model.pads.push_back(d); dragDot = (int) proc.model.pads.size() - 1; proc.syncPads(); proc.engine.padStopped.store(false); repaint();
        if (onEdit) onEdit();
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (rightErase) { eraseAt(e.position); return; }
        if (padMode()) {
            if (dragDot >= 0 && dragDot < (int) proc.model.pads.size()) {   // glide live: no snapping while you drag, so you hear every shade of beating
                auto& d = proc.model.pads[(size_t) dragDot];
                d.tone = toneOf(e.position.x); d.pitch = juce::jlimit(lo(), hi(), midiOf(e.position.y)); proc.syncPads(); repaint();
            }
            return;
        }
        if (!stroke.empty()) { stroke.push_back(e.position); repaint(); return; }
        if (moveNote >= 0) {
            auto& n = proc.model.notes[(size_t) moveNote];
            if (moveLen) n.len = juce::jmax(proc.model.quantize ? grid() : 0.0625, proc.model.quantize ? snapB(beatOf(e.position.x) + grid()) - n.start : beatOf(e.position.x) - n.start);
            else { n.start = juce::jlimit(0.0, beats() - 0.0625, snapB(orig.start + beatOf(e.position.x) - beatOf(down.x))); n.pitch = pitchFor(yOf(orig.pitch) + e.position.y - down.y, n.layer); }
            repaint();
        }
        if (tool == Erase) eraseAt(e.position);
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (rightErase) { rightErase = false; return; }
        if (padMode()) {
            if (dragDot >= 0 && dragDot < (int) proc.model.pads.size()) {
                auto& d = proc.model.pads[(size_t) dragDot];
                if (proc.model.snap && !e.mods.isAltDown()) d.pitch = sp::snapPitch(d.pitch, proc.model.root, proc.model.scale);   // lands in key (hold ALT to keep it free)
                proc.syncPads(); if (onEdit) onEdit();
            }
            dragDot = -1; repaint(); return;
        }
        if (!stroke.empty()) { commitStroke(); stroke.clear(); repaint(); }
        if (moveNote >= 0) { moveNote = -1; edited(); }
    }
    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override {
        if (!padMode()) return;
        const int h = dotAt(e.position); if (h < 0) return;
        auto& d = proc.model.pads[(size_t) h]; d.gain = juce::jlimit(0.05f, 1.0f, d.gain + w.deltaY * 0.25f); proc.syncPads(); repaint();
    }
    // a drawn line -> notes: one per grid step the line crosses; same pitch in a row = one longer note
    void commitStroke() {
        const int l = ensureLayer ? ensureLayer() : currentLayer;
        if (l < 0) { if (onNeedSound) onNeedSound(); return; }
        const bool drum = proc.model.layers[(size_t) l].kind == 1;
        if (stroke.size() < 3) { addNote(l, snapB(beatOf(stroke[0].x)), pitchFor(stroke[0].y, l)); return; }
        std::map<int, float> cells;   // grid cell -> y
        for (size_t i = 1; i < stroke.size(); ++i) {
            const auto a = stroke[i - 1], b = stroke[i];
            for (int k = 0; k <= 8; ++k) { const auto q = a + (b - a) * (k / 8.0f); cells[(int) (beatOf(q.x) / grid())] = q.y; }
        }
        sp::Note cur; bool open = false;
        for (auto& [c, y] : cells) {
            const double st = c * grid(); if (st >= beats()) continue;
            const float pitch = pitchFor(y, l);
            if (!drum && open && std::abs(pitch - cur.pitch) < 0.01f && std::abs(cur.start + cur.len - st) < 1e-6) { cur.len += grid(); continue; }
            if (open) proc.model.notes.push_back(cur);
            cur = { l, st, grid(), pitch, 0.85f, false }; open = true;
        }
        if (open) proc.model.notes.push_back(cur);
        proc.audition(l, (int) std::round(cur.pitch)); edited();
    }
    void addNote(int l, double beat, float pitch) { proc.model.notes.push_back({ l, juce::jlimit(0.0, beats() - 0.0625, beat), proc.model.layers[(size_t) l].kind == 1 ? grid() : juce::jmax(0.5, grid() * 2), pitch, 0.85f, false }); proc.audition(l, (int) std::round(pitch)); edited(); }
    void edited() { repaint(); proc.publish(); if (onEdit) onEdit(); }

    bool isInterestedInDragSource(const SourceDetails& d) override { return !padMode() && d.description.toString().startsWith("bank:"); }
    void itemDropped(const SourceDetails& d) override { if (onDropBank) onDropBank(d.description.toString().fromFirstOccurrenceOf("bank:", false, false).getIntValue(), beatOf((float) d.localPosition.x), midiOf((float) d.localPosition.y)); }
};

// SOUND BANK (LOOP mode): tabs (SYNTHS / DRUMS / MY SAMPLES) -> groups -> sounds. Click = hear + pick for DRAW. Drag = onto the canvas.
struct Bank : juce::Component {
    SpecProcessor& proc; int tab = 0, group = 0, selected = -1; float scroll = 0;
    std::vector<int> items;   // indexes into the current list
    std::function<void(int tab, int index)> onPick;
    explicit Bank(SpecProcessor& p) : proc(p) {}
    juce::StringArray groups() const { if (tab == 0) return sp::synthGroups(); if (tab == 1) return gb::DrumLibrary::categories(); return { "My samples" }; }
    void rebuild() {
        items.clear(); const auto gname = groups()[juce::jlimit(0, groups().size() - 1, group)];
        if (tab == 0) { const auto& b = sp::synthBank(); for (int i = 0; i < (int) b.size(); ++i) if (b[(size_t) i].group == gname) items.push_back(i); }
        else if (tab == 1) { const auto& s = proc.drums->sounds(); for (int i = 0; i < (int) s.size(); ++i) if (s[(size_t) i]->category == gname) items.push_back(i); }
        else for (int i = 0; i < (int) proc.userSamples.size(); ++i) items.push_back(i);
        scroll = 0; repaint();
    }
    juce::String nameOf(int idx) const { if (tab == 0) return sp::synthBank()[(size_t) idx].name; if (tab == 1) return proc.drums->sounds()[(size_t) idx]->name; return proc.userSamples[(size_t) idx]->name; }
    static constexpr int kTabH = 30, kGroupRowH = 24, kItemH = 26;
    int groupRows() const { return (groups().size() + 2) / 3; }
    int listTop() const { return kTabH + 6 + groupRows() * kGroupRowH + 8; }
    void paint(juce::Graphics& g) override {
        auto R = getLocalBounds().toFloat(); g.setColour(CARD); g.fillRoundedRectangle(R, 12); g.setColour(PURPLE.withAlpha(0.25f)); g.drawRoundedRectangle(R.reduced(0.5f), 12, 1.0f);
        const char* tabs[] = { "SYNTHS", "DRUMS", "MY SAMPLES" };
        for (int t = 0; t < 3; ++t) { auto r = juce::Rectangle<float>(4 + t * (R.getWidth() - 8) / 3, 4, (R.getWidth() - 8) / 3 - 4, (float) kTabH - 4);
            g.setColour(t == tab ? PURPLE.withAlpha(0.85f) : CARD2); g.fillRoundedRectangle(r, 7); g.setColour(INK); g.setFont(juce::FontOptions(11.5f, juce::Font::bold)); g.drawText(tabs[t], r, juce::Justification::centred); }
        auto gs = groups();
        for (int i = 0; i < gs.size(); ++i) { auto r = juce::Rectangle<float>(4 + (i % 3) * (R.getWidth() - 8) / 3, (float) kTabH + 6 + (i / 3) * kGroupRowH, (R.getWidth() - 8) / 3 - 4, (float) kGroupRowH - 4);
            g.setColour(i == group ? GREEN.withAlpha(0.25f) : CARD2); g.fillRoundedRectangle(r, 6); g.setColour(i == group ? GREEN : GREY); g.setFont(juce::FontOptions(11.0f, juce::Font::bold)); g.drawText(gs[i], r, juce::Justification::centred); }
        g.saveState(); g.reduceClipRegion(0, listTop(), getWidth(), getHeight() - listTop());
        for (int i = 0; i < (int) items.size(); ++i) {
            auto r = juce::Rectangle<float>(6, listTop() + i * kItemH - scroll, R.getWidth() - 12, (float) kItemH - 3);
            g.setColour(items[(size_t) i] == selected ? PURPLE.withAlpha(0.35f) : CARD2); g.fillRoundedRectangle(r, 6);
            g.setColour(INK); g.setFont(juce::FontOptions(12.5f, juce::Font::bold)); g.drawText(nameOf(items[(size_t) i]), r.reduced(10, 0), juce::Justification::centredLeft);
            g.setColour(GREY); g.setFont(juce::FontOptions(10.0f)); g.drawText("drag", r.reduced(8, 0), juce::Justification::centredRight);
        }
        g.restoreState();
        if (tab == 2 && items.empty()) { g.setColour(GREY); g.setFont(juce::FontOptions(12.5f)); g.drawFittedText("Drop audio files anywhere on Spectrum and they show up here.", getLocalBounds().withTrimmedTop(listTop()).reduced(14), juce::Justification::centredTop, 3); }
    }
    void mouseDown(const juce::MouseEvent& e) override {
        const auto p = e.position;
        if (p.y < kTabH) { tab = juce::jlimit(0, 2, (int) (p.x / (getWidth() / 3.0f))); group = 0; rebuild(); return; }
        if (p.y < listTop()) { const int i = (int) ((p.y - kTabH - 6) / kGroupRowH) * 3 + (int) (p.x / (getWidth() / 3.0f)); if (i < groups().size()) { group = i; rebuild(); } return; }
        const int i = (int) ((p.y - listTop() + scroll) / kItemH); if (!juce::isPositiveAndBelow(i, (int) items.size())) return;
        selected = items[(size_t) i]; repaint(); if (onPick) onPick(tab, selected);
    }
    void mouseDrag(const juce::MouseEvent& e) override {
        if (selected < 0 || e.mouseDownPosition.y < listTop() || e.getDistanceFromDragStart() < 8) return;
        if (auto* c = juce::DragAndDropContainer::findParentDragContainerFor(this)) if (!c->isDragAndDropActive()) c->startDragging("bank:" + juce::String(tab * 10000 + selected), this);
    }
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails& w) override { scroll = juce::jlimit(0.0f, juce::jmax(0.0f, items.size() * (float) kItemH - (getHeight() - listTop())), scroll - w.deltaY * 120); repaint(); }
};

// BRUSHES + STARTING POINTS (PAD mode, left panel)
struct PadPanel : juce::Component {
    SpecProcessor& proc; std::function<void(int)> onBrush, onStarter;
    explicit PadPanel(SpecProcessor& p) : proc(p) {}
    static constexpr int kTileH = 58, kTop = 30;
    juce::Rectangle<float> tile(int i) const { return { 8.0f, (float) (kTop + i * (kTileH + 6)), getWidth() - 16.0f, (float) kTileH }; }
    juce::Rectangle<float> starter(int i) const { const float y0 = (float) (kTop + 4 * (kTileH + 6) + 30); return { 8.0f + (i % 2) * (getWidth() - 16.0f) / 2, y0 + (i / 2) * 40.0f, (getWidth() - 16.0f) / 2 - 4, 34.0f }; }
    void paint(juce::Graphics& g) override {
        auto R = getLocalBounds().toFloat(); g.setColour(CARD); g.fillRoundedRectangle(R, 12); g.setColour(PURPLE.withAlpha(0.25f)); g.drawRoundedRectangle(R.reduced(0.5f), 12, 1.0f);
        g.setColour(GREY); g.setFont(juce::FontOptions(11.0f, juce::Font::bold)); g.drawText("BRUSH  (what a click paints)", 12, 8, (int) R.getWidth() - 20, 16, juce::Justification::left);
        for (int i = 0; i < sp::kVoiceTypes; ++i) {
            auto r = tile(i); const bool on = proc.model.brush == i; const auto c = brushColour(i);
            g.setColour(on ? c.withAlpha(0.22f) : CARD2.withAlpha(0.8f)); g.fillRoundedRectangle(r, 9);
            g.setColour(c.withAlpha(on ? 0.95f : 0.35f)); g.drawRoundedRectangle(r, 9, on ? 2.0f : 1.0f);
            const auto dot = juce::Rectangle<float>(18, 18).withCentre({ r.getX() + 20, r.getY() + 18 });
            g.setGradientFill(juce::ColourGradient(juce::Colours::white, dot.getX() + 4, dot.getY() + 4, c, dot.getRight(), dot.getBottom(), true)); g.fillEllipse(dot);
            g.setColour(INK); g.setFont(juce::FontOptions(14.0f, juce::Font::bold)); g.drawText(sp::voiceName(i), (int) r.getX() + 36, (int) r.getY() + 8, 140, 18, juce::Justification::left);
            g.setColour(GREY); g.setFont(juce::FontOptions(10.5f)); g.drawFittedText(brushBlurb(i), r.toNearestInt().withTrimmedLeft(36).withTrimmedTop(26).withTrimmedRight(6), juce::Justification::topLeft, 2, 0.9f);
        }
        const float y0 = (float) (kTop + 4 * (kTileH + 6) + 8);
        g.setColour(GREY); g.setFont(juce::FontOptions(11.0f, juce::Font::bold)); g.drawText("STARTING POINTS  (in your key)", 12, (int) y0, (int) R.getWidth() - 20, 16, juce::Justification::left);
        const char* st[] = { "Bell chord", "Grand piano", "Ambient pad", "Feedback rise" };
        for (int i = 0; i < 4; ++i) { auto r = starter(i); g.setColour(CARD2); g.fillRoundedRectangle(r, 8); g.setColour(brushColour(i).withAlpha(0.6f)); g.drawRoundedRectangle(r, 8, 1.1f);
            g.setColour(INK); g.setFont(juce::FontOptions(12.0f, juce::Font::bold)); g.drawText(st[i], r, juce::Justification::centred); }
        // legend
        float ly = starter(3).getBottom() + 14;
        g.setColour(GREY); g.setFont(juce::FontOptions(11.0f, juce::Font::bold)); g.drawText("WHAT YOU SEE", 12, (int) ly, 200, 14, juce::Justification::left); ly += 20;
        auto legend = [&](juce::Colour c, bool wavy, const juce::String& txt) {
            juce::Path p; p.startNewSubPath(14, ly + 6); for (int s = 1; s <= 20; ++s) p.lineTo(14 + s * 1.6f, ly + 6 + (wavy ? std::sin(s * 0.9f) * 3.5f : -std::sin(s / 20.0f * 3.14f) * 4.0f));
            g.setColour(c); g.strokePath(p, juce::PathStrokeType(1.6f)); g.setColour(INK.withAlpha(0.85f)); g.setFont(juce::FontOptions(11.0f)); g.drawText(txt, 54, (int) ly, (int) R.getWidth() - 60, 13, juce::Justification::left); ly += 19; };
        legend(CYAN, true, "slow shimmer: two notes gently beating");
        legend(PINK, true, "rough clash: dissonance, move to resolve");
        legend(AMBER, false, "golden thread: in harmony, overtones agree");
        g.setColour(GREY.withAlpha(0.8f)); g.setFont(juce::FontOptions(10.5f));
        g.drawFittedText("The small dots above each note are its overtones. The pattern loops: bells and piano ring again every time round.", juce::Rectangle<int>(12, (int) ly + 4, (int) R.getWidth() - 22, 44), juce::Justification::topLeft, 3);
    }
    void mouseDown(const juce::MouseEvent& e) override {
        for (int i = 0; i < sp::kVoiceTypes; ++i) if (tile(i).contains(e.position)) { if (onBrush) onBrush(i); repaint(); return; }
        for (int i = 0; i < 4; ++i) if (starter(i).contains(e.position)) { if (onStarter) onStarter(i); return; }
    }
};

// =====================================================================================================
// the version shown in the header + tab bar tooltip (the plugin's JucePlugin_VersionString; the test build has none)
static juce::String spectrumVersion() {
#ifdef JucePlugin_VersionString
    return JucePlugin_VersionString;
#else
    return "3.0.0";
#endif
}
struct SpecEditor::Content : juce::Component {
    SpecProcessor& proc; Canvas canvas; Bank bank; PadPanel padPanel;
    ghosty::UnlockButton unlock { proc.license };
    juce::TextButton modePad { "PAINT PADS" }, modeLoop { "PAINT LOOP" }, playBtn { "PLAY" }, saveBtn { "SAVE" }, loadBtn { "LOAD" }, clearBtn { "CLEAR" },
                     snapBtn { "SNAP TO KEY" }, detectBtn { "DETECT KEY" }, quantBtn { "QUANTIZE" }, copyBtn { "COPY" }, holdBtn { "HOLD" }, freezeBtn { "FREEZE" }, stopBtn { "STOP" };
    juce::OwnedArray<juce::TextButton> toolBtns, layerBtns, patBtns;
    juce::ComboBox barsBox, keyBox, scaleBox, gridBox;
    sp::BarSlider bpm { juce::Slider::LinearBar, juce::Slider::TextBoxRight }; std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> bpmAtt;   // the slider convention (SpecSliders.h)
    juce::Label hostTempo;
    juce::OwnedArray<Knob> knobs, padKnobs; juce::OwnedArray<juce::AudioProcessorValueTreeState::SliderAttachment> padAtts;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> holdAtt, freezeAtt;
    juce::TextButton muteBtn { "MUTE" }, delLayer { "REMOVE" };
    juce::Label layerTitle, status;
    int pickTab = 0, pickIdx = -1;
    struct DragOut : juce::Component {
        juce::String text; std::function<juce::StringArray()> make; bool started = false;
        DragOut(juce::String t, std::function<juce::StringArray()> m) : text(std::move(t)), make(std::move(m)) {}
        void paint(juce::Graphics& g) override { auto r = getLocalBounds().toFloat().reduced(1); g.setColour(GREEN.withAlpha(0.15f)); g.fillRoundedRectangle(r, 8); g.setColour(GREEN.withAlpha(0.9f)); g.drawRoundedRectangle(r, 8, 1.3f);
            g.setColour(INK); g.setFont(juce::FontOptions(12.0f, juce::Font::bold)); g.drawText(text, r, juce::Justification::centred); }
        void mouseDown(const juce::MouseEvent&) override { started = false; }
        void mouseDrag(const juce::MouseEvent& e) override { if (started || e.getDistanceFromDragStart() < 6) return; started = true; auto f = make(); f.removeEmptyStrings(); if (!f.isEmpty()) ghosty::exportgate::dragFiles(f, this); }   // make() asked the gate
    };
    DragOut wavOut { "DRAG PATTERN OUT (WAV)", [this] { const auto f = proc.bounce(&wavOut); return f.existsAsFile() ? juce::StringArray(f.getFullPathName()) : juce::StringArray(); } };
    DragOut stemsOut { "DRAG STEMS OUT", [this] { return proc.bounceStems(&stemsOut); } };
    juce::TextButton arrangeOut { "SEND TO ARRANGE" }, arcsArrange { "SEND TO ARRANGE" };   // arcsArrange sits in the ARCS header (free space next to PAGE A)   // what you hear -> an ARRANGE clip (rendered on a worker, tagged with the tempo)
    juce::TextButton labBtn { "SPECTRAL LAB" }; std::unique_ptr<LabPanel> lab;   // the SPECTRAL LAB tab (covers the window while open)
    // ---- tabs (the bar above is built from sp::tabs()); every page other than PAINT PADS / LOOP covers this area
    sp::TabKind current = sp::TabKind::Pads, lastMain = sp::TabKind::Pads;
    std::map<sp::TabKind, std::unique_ptr<juce::Component>> pages;   // made on first use, kept (their view state survives)
    std::function<void(sp::TabKind)> onTabChanged;
    // SYNC: OWN - SPACE plays / stops Spectrum's own clock on every page (a page's own SPACE preview applies in SYNC: DAW). Key listeners run
    // before the page's keyPressed; a focused text box still gets its SPACE first. In SYNC: DAW nothing is taken here.
    struct OwnSpace : juce::KeyListener {
        SpecProcessor& p; explicit OwnSpace(SpecProcessor& pr) : p(pr) {}
        bool keyPressed(const juce::KeyPress& k, juce::Component*) override { if (k == juce::KeyPress(juce::KeyPress::spaceKey) && p.ownWanted()) { p.toggleOwnPlay(); return true; } return false; }
    } ownSpace { proc };
    void pushArrangeView(arr::ArrangePage* pg) {   // ARRANGE's PLAY light + tempo label follow SYNC: OWN 133 BPM / DAW 128.5 BPM (read-only)
        arr::ArrangePage::TransportView v; v.active = true; v.own = proc.ownWanted();
        v.bpm = v.own ? proc.sync.getOwnBpm() : proc.hostBpm(); v.playing = v.own ? proc.sync.isOwnPlaying() : proc.hostPlayingUi.load();
        pg->setTransportView(v);
    }
    void addOwnSpace(juce::Component& c) {   // the page and everything in it (inner panels with their own SPACE preview), but never a text box
        if (dynamic_cast<juce::TextEditor*>(&c) != nullptr) return;
        c.addKeyListener(&ownSpace); c.getProperties().set("ownSpace", true); for (auto* ch : c.getChildren()) addOwnSpace(*ch);
    }
    juce::Component* makePage(sp::TabKind k) {   // ADD NEW PAGES HERE (and a line in SpecTabs.h)
        switch (k) {
            case sp::TabKind::Arcs: { auto* pg = new arcs::ArcsPage(proc.arcsPage, false); pg->setComponentID("arcsPage"); pg->allowExport = [this](juce::Component* ui, const char* id) { return proc.allowExport(ui, id); }; pg->exportOpen = [this] { return proc.exportOpen(); }; pg->getProperties().set("foreignTheme", true); pg->onLeave = [this] { showTab(lastMain); }; pg->setLook(sp::pal().look); pg->setThemeButtonVisible(false); return pg; }
            case sp::TabKind::Arrange: { auto* pg = new arr::ArrangePage(proc.arrange); pg->setComponentID("arrangePage"); pg->getProperties().set("foreignTheme", true); pg->setLook(sp::pal().look); pg->setThemeButtonVisible(false);
                pg->onOwnPlayToggle = [this] { proc.toggleOwnPlay(); }; pg->setOwnsTooltips(false); pg->setOwnsTypeIn(false); pg->allowExport = [this](juce::Component* ui, const char* id) { return proc.allowExport(ui, id); }; pg->exportOpen = [this] { return proc.exportOpen(); }; pushArrangeView(pg); return pg; }   // the editor owns the tooltip window + the type-in; SYNC: OWN - ARRANGE PLAY / SPACE drive the one own clock
#if GHOSTY_PROCESS_LAB
            case sp::TabKind::Process: { auto* pg = new LabProcessPage(proc); pg->setComponentID("processPage"); pg->onBack = [this] { showTab(sp::TabKind::Lab); }; return pg; }
#endif
#if GHOSTY_VOICE
            case sp::TabKind::Voice: { auto* pg = new SpecVoiceTab(proc); pg->setComponentID("voiceTab"); return pg; }
            case sp::TabKind::GhostVoice: { auto* pg = new GhostVoicePage(proc.ghost); pg->setComponentID("ghostVoicePage"); pg->getProperties().set("foreignTheme", true); pg->setOwnsTooltips(false);
                pg->allowExport = [this](juce::Component* ui, const char* id) { return proc.allowExport(ui, id); }; pg->tempoFn = [this] { return proc.tempo(); }; pg->setLook(sp::pal().look);
                pg->onSendToArrange = [this, pg] { sendGhostVoiceToArrange(*pg); }; return pg; }
#endif
            default: return nullptr;
        }
    }
    void themeChanged(bool dark) {
        bpm.setColour(juce::Slider::trackColourId, PURPLE.interpolatedWith(CARD2, 0.5f)); bpm.setColour(juce::Slider::textBoxTextColourId, INK);
        hostTempo.setColour(juce::Label::backgroundColourId, sp::pal().field); hostTempo.setColour(juce::Label::textColourId, ghosty::themes::readableOn(GREEN, sp::pal().field, 4.6));
        status.setColour(juce::Label::textColourId, ghosty::themes::readableOn(GREEN, BG, 4.6)); layerTitle.setColour(juce::Label::textColourId, INK);
        if (lab) lab->themeChanged();
        if (auto it = pages.find(sp::TabKind::Arrange); it != pages.end() && it->second) if (auto* pg = dynamic_cast<arr::ArrangePage*>(it->second.get())) pg->setLook(sp::pal().look);
        if (auto it = pages.find(sp::TabKind::Arcs); it != pages.end() && it->second) if (auto* pg = dynamic_cast<arcs::ArcsPage*>(it->second.get())) pg->setLook(sp::pal().look);
#if GHOSTY_VOICE
        if (auto it = pages.find(sp::TabKind::Voice); it != pages.end() && it->second) if (auto* pg = dynamic_cast<SpecVoiceTab*>(it->second.get())) pg->themeChanged();
        if (auto it = pages.find(sp::TabKind::GhostVoice); it != pages.end() && it->second) if (auto* pg = dynamic_cast<GhostVoicePage*>(it->second.get())) pg->setLook(sp::pal().look);
#endif
#if GHOSTY_PROCESS_LAB
        if (auto it = pages.find(sp::TabKind::Process); it != pages.end() && it->second) if (auto* pg = dynamic_cast<LabProcessPage*>(it->second.get())) pg->themeChanged();
#endif
        repaint();
    }
    void showTab(sp::TabKind k) {
        if (k != sp::TabKind::Lab && proc.lab.open.load()) openLab(false);
        for (auto& [kind, pg] : pages) if (kind != k && pg) pg->setVisible(false);
        if (k == sp::TabKind::Pads || k == sp::TabKind::Loop) { lastMain = k; setMode(k == sp::TabKind::Pads ? 0 : 1); }
        else if (k == sp::TabKind::Lab) openLab(true);
        else {
            auto& pg = pages[k]; if (!pg) { pg.reset(makePage(k)); if (pg) { addChildComponent(*pg); addOwnSpace(*pg); } }
            if (pg) { pg->setBounds(getLocalBounds()); pg->setVisible(true); pg->toFront(true); pg->grabKeyboardFocus(); }
        }
        arcsArrange.setVisible(k == sp::TabKind::Arcs); if (k == sp::TabKind::Arcs) arcsArrange.toFront(false);
        current = k; if (onTabChanged) onTabChanged(k);
    }
    // ---- TRY IT (HOW IT WORKS): a small built-in example per page, never over the user's work where that can be avoided
    void freshPattern() {   // PAINT: an empty pattern for the example (the current one stays as it is)
        if (!proc.model.notes.empty() || !proc.model.pads.empty()) for (int i = 0; i < sp::kPatterns; ++i)
            if (i != proc.model.current && !proc.model.patterns[(size_t) i].used()) { proc.switchPattern(i); afterPatternChange(); break; }
    }
    void labDemo() {   // a minor chord that swells, then a rising glide (NEW canvas: undoable)
        auto& L = proc.lab; L.newCanvas(4.0); const int T = L.canvas.T;
        lab::Brush b; b.size = 1.2f; b.hardness = 0.85f; b.level = 0.5f;
        const double chord[] = { 261.63, 311.13, 392.0, 523.25 };
        for (int t = 0; t < (int) (T * 0.62); ++t) { const float env = juce::jmin(1.0f, t / (T * 0.08f)) * juce::jmin(1.0f, (T * 0.62f - t) / (T * 0.1f));
            for (double f : chord) for (int h = 1; h <= 3; ++h) { b.level = 0.55f * env / (float) h; lab::paintAt(L.canvas, (float) t, (float) lab::hzRow(f * h), b, f * h); } }
        for (int t = (int) (T * 0.5); t < T - 2; ++t) { const double u = (t - T * 0.5) / (T * 0.5), hz = 220.0 * std::exp2(2.5 * u); b.level = 0.5f;
            for (int h = 1; h <= 2; ++h) lab::paintAt(L.canvas, (float) t, (float) lab::hzRow(hz * h), b, hz * h); }
        L.edited(); L.requestPreview(); if (lab) lab->canvasReplaced();
    }
    bool tryIt(sp::TabKind k) {
        switch (k) {
            case sp::TabKind::Pads: { showTab(sp::TabKind::Pads); freshPattern(); starter(0); if (auto* h = proc.apvts.getParameter("padHold")) h->setValueNotifyingHost(1.0f);
                proc.engine.padStopped.store(false); say("TRY IT: a bell chord. Drag a bell a little off its note and listen to it beat."); return !proc.model.pads.empty(); }
            case sp::TabKind::Loop: { showTab(sp::TabKind::Loop); freshPattern();
                auto pickGroup = [](const char* g) { for (auto& b : sp::synthBank()) if (b.group == g) return &b; return &sp::synthBank().front(); };
                const auto* bass = pickGroup("Bass"); const auto* pl = pickGroup("Plucks");
                const int lb = proc.addLayer(0, bass->name, bass->words, bass->seed), lp = proc.addLayer(0, pl->name, pl->words, pl->seed);
                const float root = 36.0f + (float) proc.model.root; const int deg[] = { 0, 3, 7, 10, 12, 10, 7, 3 };
                if (lb >= 0) for (int k2 = 0; k2 < 4; ++k2) { sp::Note n; n.layer = lb; n.start = k2 * 2.0; n.len = 1.5; n.pitch = root + (k2 == 2 ? 5.0f : 0.0f); proc.model.notes.push_back(n); }
                if (lp >= 0) for (int k2 = 0; k2 < 16; ++k2) { sp::Note n; n.layer = lp; n.start = k2 * 0.5; n.len = 0.4; n.pitch = root + 24.0f + (float) deg[k2 % 8]; proc.model.notes.push_back(n); }
                proc.model.bars = 2; proc.publish(); afterPatternChange(); refreshLayers(); canvas.repaint(); proc.engine.playing = true;
                say("TRY IT: a bass and a pluck line. Pick a sound on the left and DRAW over it."); return !proc.model.notes.empty(); }
            case sp::TabKind::Lab: { showTab(sp::TabKind::Lab); labDemo(); if (lab) lab->showPage(0); return proc.lab.canvas.T > 0; }
            case sp::TabKind::Process: { labDemo(); showTab(sp::TabKind::Process); return proc.lab.canvas.T > 0; }
            case sp::TabKind::Arcs: { const auto& f = arcs::factory::pages(); proc.arcsPage.edit() = f[(size_t) juce::jmin(1, (int) f.size() - 1)].make(); proc.arcsPage.commit("TRY IT");
                showTab(sp::TabKind::Arcs); return true; }
            case sp::TabKind::Arrange: {
                proc.arrange.receive(arr::makePayload(sp::demoClip(0, 48000), 48000, "Demo pluck", {}, {}, 120), 0, 0);
                proc.arrange.receive(arr::makePayload(sp::demoClip(1, 48000), 48000, "Demo pad", {}, {}, 0), 1, 0);
                showTab(sp::TabKind::Arrange); return true; }
            case sp::TabKind::Voice: {
#if GHOSTY_VOICE
                juce::String err; const bool ok = proc.voice.setAudio(0, sp::demoVoice(48000, 3.0), 48000, "Ghost demo voice", true, &err);
                showTab(sp::TabKind::Voice); return ok;
#else
                return false;
#endif
            }
            case sp::TabKind::GhostVoice: {
#if GHOSTY_VOICE
                juce::String err; const bool ok = proc.ghost.setTake(0, sp::demoVoice(48000, 3.0), 48000, "Ghost demo voice", true, &err);
                proc.ghost.setIdentity(1, fusion::factoryIdentities()[0], fusion::factoryIdentities()[0].name); auto p = proc.ghost.params(); p.wB = 0.5f; proc.ghost.setParams(p);
                showTab(sp::TabKind::GhostVoice); return ok;
#else
                return false;
#endif
            }
        }
        return false;
    }
#if GHOSTY_VOICE
    void sendGhostVoiceToArrange(GhostVoicePage& pg) {   // the GHOST VOICE result -> an ARRANGE clip (a plain sound: the page's state is big, so no re-render source)
        juce::String err; auto r = proc.ghost.result(); if (!r || proc.ghost.resultStale()) { if (proc.ghost.renderNow(err)) r = proc.ghost.result(); }
        if (!r || r->audio.L.empty()) { say(err.isNotEmpty() ? err : juce::String("Nothing to send yet: record your voice first.")); return; }
        auto a = proc.ghost.slot(0); const juce::String name = (a ? a->name() : juce::String("Voice")) + " (ghost voice)";
        const auto& x = r->audio; juce::AudioBuffer<float> b(x.stereo() ? 2 : 1, (int) x.L.size()); b.copyFrom(0, 0, x.L.data(), (int) x.L.size()); if (x.stereo()) b.copyFrom(1, 0, x.R.data(), (int) x.R.size());
        const auto id = proc.arrange.receive(arr::makePayload(b, x.fs, name, {}, {}, 0)); say(id != 0 ? "\"" + name + "\" is on the ARRANGE timeline." : juce::String("Couldn't send it.")); juce::ignoreUnused(pg);
    }
#endif
    void openLab(bool on) { proc.lab.open = on; if (!on) { proc.lab.previewPlaying = false; proc.lab.scrubPos = -1.0; } lab->setVisible(on); if (on) { lab->toFront(true); lab->grabKeyboardFocus(); } }

    explicit Content(SpecProcessor& p) : proc(p), canvas(p), bank(p), padPanel(p) {
        addAndMakeVisible(canvas); addChildComponent(bank); addChildComponent(padPanel);
        bank.onPick = [this](int tab, int idx) { pick(tab, idx); };
        bank.rebuild();
        padPanel.onBrush = [this](int b) { proc.model.brush = b; say(juce::String(sp::voiceName(b)) + " brush: " + brushBlurb(b)); };
        padPanel.onStarter = [this](int s) { starter(s); };
        canvas.ensureLayer = [this] { return ensureLayer(); };
        canvas.onDropBank = [this](int code, double beat, float midi) { dropBank(code / 10000, code % 10000, beat, midi); };
        canvas.onEdit = [this] { refreshLayers(); refreshPatterns(); };
        canvas.onNeedSound = [this] { say("Pick a sound in the bank first (left), then draw."); };
        canvas.onSay = [this](const juce::String& s) { say(s); };
        for (auto* b : { &modePad, &modeLoop }) { b->setClickingTogglesState(false); addAndMakeVisible(b); }
        tintButton(modePad, PINK); tintButton(modeLoop, CYAN);
        modePad.onClick = [this] { setMode(0); }; modeLoop.onClick = [this] { setMode(1); };
        modePad.setTooltip("Paint a pad: every dot is a ringing note. Up = pitch, left/right = tone.");
        modeLoop.setTooltip("Paint a loop: notes in time, with synths, drums and your samples.");
        const char* tools[] = { "DRAW", "DROP", "CONNECT", "MOVE", "ERASE" };
        const char* tipsT[] = { "Draw a line of notes (click = one note, grab a note to move it)", "Click to drop single notes", "Click two notes to slide between them", "Move notes (drag the end to stretch)", "Erase notes (right-click erases with any tool)" };
        for (int i = 0; i < 5; ++i) { auto* b = toolBtns.add(new juce::TextButton(tools[i])); b->setTooltip(tipsT[i]); b->onClick = [this, i] { canvas.tool = (Canvas::Tool) i; refreshTools(); }; addAndMakeVisible(b); }
        playBtn.setClickingTogglesState(true); playBtn.onClick = [this] { proc.engine.playing = playBtn.getToggleState(); }; tintButton(playBtn, GREEN);
        for (auto* b : { &playBtn, &saveBtn, &loadBtn, &clearBtn, &snapBtn, &detectBtn, &quantBtn, &copyBtn, &muteBtn, &delLayer, &holdBtn, &freezeBtn, &stopBtn }) addAndMakeVisible(b);
        addChildComponent(unlock); unlock.refresh();
        bpm.setRange(60, 200, 1); bpm.setTextBoxStyle(juce::Slider::TextBoxRight, false, 58, 24); bpm.setNumDecimalPlacesToDisplay(0); bpm.setTextValueSuffix(" bpm"); bpmAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(proc.apvts, "bpm", bpm);
        bpm.setColour(juce::Slider::trackColourId, PURPLE.interpolatedWith(CARD2, 0.5f)); bpm.setColour(juce::Slider::textBoxTextColourId, INK); bpm.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack); addAndMakeVisible(bpm);
        bpm.setTooltip("Tempo when there's no host: click / drag to set (Shift = fine, wheel = 1 bpm), right-click to type, double-click = 120"); if (auto* prm = proc.apvts.getParameter("bpm")) bpm.setDoubleClickReturnValue(true, prm->convertFrom0to1(prm->getDefaultValue()));
        hostTempo.setFont(juce::FontOptions(12.0f, juce::Font::bold)); hostTempo.setJustificationType(juce::Justification::centred); hostTempo.setColour(juce::Label::textColourId, GREEN);
        hostTempo.setColour(juce::Label::backgroundColourId, juce::Colour(0xff160f36)); hostTempo.setTooltip("Following your project's tempo (loop, drag-outs and stems use it)"); addChildComponent(hostTempo);
        barsBox.addItemList({ "1 bar", "2 bars", "4 bars" }, 1); barsBox.onChange = [this] { static const int B[] = { 1, 2, 4 }; proc.model.bars = B[juce::jmax(0, barsBox.getSelectedItemIndex())]; proc.publish(); canvas.clearSpectrum(); canvas.repaint(); };
        barsBox.setTooltip("Pattern length (the canvas loops)");
        keyBox.addItemList({ "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }, 1); keyBox.onChange = [this] { proc.model.root = keyBox.getSelectedItemIndex(); proc.syncPads(); canvas.repaint(); };
        scaleBox.addItemList(sp::scaleNames(), 1); scaleBox.onChange = [this] { proc.model.scale = scaleBox.getSelectedItemIndex(); proc.syncPads(); canvas.repaint(); };
        gridBox.addItemList({ "1/4", "1/8", "1/16" }, 1); gridBox.onChange = [this] { static const int G[] = { 4, 8, 16 }; proc.model.gridDiv = G[juce::jmax(0, gridBox.getSelectedItemIndex())]; canvas.repaint(); };
        gridBox.setTooltip("Note grid for QUANTIZE");
        for (auto* c : { &barsBox, &keyBox, &scaleBox, &gridBox }) addAndMakeVisible(c);
        snapBtn.setClickingTogglesState(true); snapBtn.onClick = [this] { proc.model.snap = snapBtn.getToggleState(); canvas.repaint(); };
        snapBtn.setTooltip("New notes land on the key's notes (pads: when you let go of a dot; hold ALT to keep it free)");
        quantBtn.setClickingTogglesState(true); quantBtn.onClick = [this] { proc.model.quantize = quantBtn.getToggleState(); gridBox.setEnabled(proc.model.quantize); canvas.repaint(); };
        quantBtn.setTooltip("Snap notes to the beat grid when placing and moving them (LOOP)");
        detectBtn.setTooltip("Listen for a few seconds to the sidechain input (or Spectrum itself when nothing comes in) and set the key + scale");
        detectBtn.onClick = [this] { if (proc.keyListening()) return; proc.startKeyListen(); detectBtn.setButtonText("LISTENING..."); detectBtn.getProperties().set("accent", true); detectBtn.repaint();
            say("Listening... play your track (route it into Spectrum's sidechain) or press PLAY."); };
        clearBtn.onClick = [this] {
            if (canvas.padMode()) { proc.model.pads.clear(); proc.syncPads(); say("Pad cleared."); } else { proc.model.notes.clear(); proc.publish(); say("Loop notes cleared."); }
            canvas.clearSpectrum(); canvas.repaint(); refreshLayers(); refreshPatterns(); };
        clearBtn.setTooltip("Clear this pattern's pad dots (PADS) or loop notes (LOOP)");
        saveBtn.setTooltip("Save the whole canvas (all patterns, sounds and pad effects) to Documents/Ghosty/Spectrum");
        loadBtn.setTooltip("Load a canvas you saved with SAVE (all patterns, sounds and pad effects)");
        playBtn.setTooltip("Play the pattern here (while your DAW is stopped). When your DAW plays, the pattern plays with it");
        muteBtn.setTooltip("Mute / unmute the selected sound layer");
        saveBtn.onClick = [this] { const auto n = "Spectrum " + juce::Time::getCurrentTime().formatted("%m-%d %H%M"); say(proc.saveCanvas(n) ? "Saved \"" + n + "\" (all patterns) in Documents/Ghosty/Spectrum." : "Couldn't save."); };
        loadBtn.onClick = [this] { juce::PopupMenu m; auto files = proc.userDir().findChildFiles(juce::File::findFiles, false, "*.ghostspec"); files.sort();
            for (int i = 0; i < files.size(); ++i) m.addItem(i + 1, files[i].getFileNameWithoutExtension()); if (files.isEmpty()) m.addItem(-1, "Nothing saved yet (use SAVE)", false);
            m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&loadBtn), [this, files](int r) { if (r > 0) loadCanvasFile(files[r - 1]); }); };
        for (int i = 0; i < sp::kPatterns; ++i) {
            auto* b = patBtns.add(new juce::TextButton(juce::String::charToString((juce::juce_wchar) ('A' + i)))); tintButton(*b, AMBER);
            b->setTooltip("Pattern " + b->getButtonText() + " (switch any time, even while playing). Right-click for copy / clear.");
            b->onClick = [this, i] { if (patRight) { patRight = false; return; } proc.switchPattern(i); afterPatternChange(); };
            b->addMouseListener(this, false); addAndMakeVisible(b);
        }
        copyBtn.setTooltip("Copy this pattern into the next empty one, to make a variation"); tintButton(copyBtn, AMBER);
        copyBtn.onClick = [this] { const int j = proc.copyPatternToNextFree(); if (j < 0) say("All 8 patterns are in use. Clear one first (right-click it)."); else { afterPatternChange(); say(juce::String("Copied to pattern ") + (char) ('A' + j) + ". Change it to make a variation."); } };
        const char* kn[] = { "VOLUME", "TONE", "SPACE", "PAN" };
        for (int i = 0; i < 4; ++i) {
            auto* k = knobs.add(new Knob(kn[i])); addAndMakeVisible(k);
            k->s.setRange(i == 3 ? -1.0 : 0.0, 1.0); k->s.setDoubleClickReturnValue(true, i == 0 ? 0.8 : (i == 1 ? 0.5 : (i == 2 ? 0.2 : 0.0)));
            if (i == 0) sp::gainDbText(k->s); else if (i == 3) sp::panText(k->s); else sp::percentText(k->s); k->s.setTooltip(juce::String("Layer ") + kn[i] + "  (right-click to type a value, double-click to reset)");
            k->s.onValueChange = [this, i, k] { if (canvas.currentLayer < 0 || canvas.currentLayer >= (int) proc.model.layers.size()) return; auto& L = proc.model.layers[(size_t) canvas.currentLayer];
                const float v = (float) k->s.getValue(); if (i == 0) L.vol = v; else if (i == 1) L.tone = v; else if (i == 2) L.space = v; else L.pan = v; proc.publish(); };
        }
        struct PK { const char* id; const char* label; juce::Colour c; const char* tip; };
        const PK pk[] = { { "padBloom", "BLOOM", PINK, "How slowly pads swell in and fade away" }, { "padSpace", "SPACE", CYAN, "Size and amount of the big reverb" },
                          { "padShimmer", "SHIMMER", juce::Colour(0xffc084fc), "Octave-up sparkle that rises out of the reverb" }, { "padTape", "TAPE", AMBER, "Warbly tape echo (dotted 1/8 at the project tempo)" },
                          { "padDrive", "DRIVE", RED, "Gentle saturation on the whole pad" }, { "padPedal", "STRINGS", GREEN, "Sustain pedal down: open strings tuned to your key ring along" },
                          { "padDrift", "DRIFT", CYAN, "Slow, dreamy pitch wander" }, { "padStrum", "STRUM", AMBER, "Each time the loop comes round, bells / piano ring low to high instead of all at once" },
                          { "padLevel", "LEVEL", INK, "Pad volume" } };
        for (auto& d : pk) {
            auto* k = padKnobs.add(new Knob(d.label, d.c)); k->s.setTooltip(juce::String(d.tip) + "  (right-click to type a value, double-click to reset)"); addAndMakeVisible(k);
            padAtts.add(new juce::AudioProcessorValueTreeState::SliderAttachment(proc.apvts, d.id, k->s));
            if (auto* prm = proc.apvts.getParameter(d.id)) k->s.setDoubleClickReturnValue(true, prm->convertFrom0to1(prm->getDefaultValue()));
        }
        holdBtn.setClickingTogglesState(true); freezeBtn.setClickingTogglesState(true); tintButton(holdBtn, GREEN); tintButton(freezeBtn, CYAN);
        holdAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "padHold", holdBtn);
        freezeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(proc.apvts, "padFreeze", freezeBtn);
        holdBtn.setTooltip("On: the pad always sounds. Off: it sounds while you hold MIDI keys (and follows them)");
        freezeBtn.setTooltip("Freeze the reverb: the current sound hangs in the air forever");
        tintButton(stopBtn, juce::Colour(0xffff5d73));
        stopBtn.setTooltip("Stop the pad (fades out, even with HOLD on). Touch a note, play a key, press START or play your DAW to start again");
        stopBtn.onClick = [this] { const bool nowStopped = ! proc.engine.padStopped.load(); proc.engine.padStopped.store(nowStopped); syncStop(); };
        muteBtn.setClickingTogglesState(true); muteBtn.onClick = [this] { if (canvas.currentLayer >= 0) { proc.model.layers[(size_t) canvas.currentLayer].mute = muteBtn.getToggleState(); proc.publish(); refreshLayers(); } };
        delLayer.onClick = [this] { if (canvas.currentLayer >= 0) { proc.removeLayer(canvas.currentLayer); canvas.currentLayer = -1; refreshLayers(); canvas.repaint(); } };
        delLayer.setTooltip("Remove this sound and its notes");
        for (int i = 0; i < sp::kLayers; ++i) { auto* b = layerBtns.add(new juce::TextButton()); b->setTooltip("A sound layer of this loop: click to select it (VOLUME / TONE / SPACE / PAN and MUTE / REMOVE work on it)"); b->onClick = [this, i] { selectLayer(i); }; addAndMakeVisible(b); }
        addAndMakeVisible(wavOut); addAndMakeVisible(stemsOut); wavOut.setComponentID("patternWavOut"); stemsOut.setComponentID("patternStemsOut");
        arrangeOut.setComponentID("patternToArrange"); tintButton(arrangeOut, juce::Colour(0xff60a5fa)); addAndMakeVisible(arrangeOut);
        arrangeOut.setTooltip("Put what you hear (this pattern, with the pad) on the ARRANGE timeline as a clip, at the current tempo");
        arcsArrange.setComponentID("arcsToArrange"); tintButton(arcsArrange, juce::Colour(0xff60a5fa)); addChildComponent(arcsArrange);
        arcsArrange.setTooltip("Put the ARCS page (one pass, as you hear it) on the ARRANGE timeline as a clip; its lines come along as MIDI notes");
        arcsArrange.onClick = [this] { arcsArrange.setEnabled(false); juce::Component::SafePointer<Content> sp(this);
            proc.sendArcsToArrange([sp](juce::uint32 id) { if (!sp) return; sp->arcsArrange.setEnabled(true); sp->arcsArrange.setButtonText(id != 0 ? "SENT TO ARRANGE" : "NOTHING TO SEND");
                juce::Timer::callAfterDelay(1500, [sp] { if (sp) sp->arcsArrange.setButtonText("SEND TO ARRANGE"); }); }); };
        arrangeOut.onClick = [this] { arrangeOut.setEnabled(false); say("Rendering for ARRANGE...");
            juce::Component::SafePointer<Content> sp(this);
            proc.sendPatternToArrange([sp](juce::uint32 id) { if (!sp) return; sp->arrangeOut.setEnabled(true); sp->say(id != 0 ? "On the ARRANGE timeline - open ARRANGE to place it." : "Nothing to send."); }); };
        layerTitle.setFont(juce::FontOptions(13.0f, juce::Font::bold)); addAndMakeVisible(layerTitle);
        status.setFont(juce::FontOptions(12.5f, juce::Font::bold)); status.setColour(juce::Label::textColourId, GREEN); addAndMakeVisible(status);
        addAndMakeVisible(labBtn); labBtn.setComponentID("labButton"); tintButton(labBtn, GREEN); labBtn.getProperties().set("accent", true);
        labBtn.setTooltip("SPECTRAL LAB: paint the sound itself, turn pictures into sound, ghosts, band rhythms, chaos and morphing");
        lab = std::make_unique<LabPanel>(proc); lab->setComponentID("spectralLab"); addChildComponent(*lab); addOwnSpace(*lab);
        lab->onLeave = [this](int m) { showTab(m == 0 ? sp::TabKind::Pads : sp::TabKind::Loop); };
        labBtn.onClick = [this] { showTab(sp::TabKind::Lab); };
        labBtn.setComponentID({}); labBtn.setVisible(false); modePad.setVisible(false); modeLoop.setVisible(false);   // the tab bar above has them now
        current = lastMain = proc.model.mode == 0 ? sp::TabKind::Pads : sp::TabKind::Loop;
        if (proc.lab.open.load()) { openLab(true); current = sp::TabKind::Lab; }
        syncFromModel();
        say(proc.model.mode == 0 ? "Welcome to the ghost world. Pick a brush, then click the canvas - or try a STARTING POINT." : "Pick a sound on the left and draw in time.");
    }
    // right-click on a pattern button: copy / clear
    bool patRight = false;
    void mouseDown(const juce::MouseEvent& e) override {
        patRight = e.mods.isPopupMenu();
        if (!e.mods.isPopupMenu()) return;
        for (int i = 0; i < patBtns.size(); ++i) if (e.eventComponent == patBtns[i]) {
            juce::PopupMenu m; m.addItem(1, "Copy current pattern here"); m.addItem(2, "Clear this pattern");
            m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(patBtns[i]), [this, i](int r) {
                if (r == 1) { proc.model.storeCurrent(); proc.model.patterns[(size_t) i] = proc.model.patterns[(size_t) proc.model.current]; proc.switchPattern(i); afterPatternChange(); }
                if (r == 2) { if (i == proc.model.current) { proc.model.notes.clear(); proc.model.pads.clear(); proc.publish(); } else proc.model.patterns[(size_t) i] = sp::Pattern(); afterPatternChange(); }
            });
        }
    }
    void say(const juce::String& s) { status.setText(s, juce::dontSendNotification); }
    bool loadCanvasFile(const juce::File& f) {   // LOAD: a canvas saved on the other PAINT page opens on its own tab (tab highlight + page follow it)
        if (!proc.loadCanvas(f)) return false;
        syncFromModel(); showTab(proc.model.mode == 0 ? sp::TabKind::Pads : sp::TabKind::Loop); say("Loaded."); return true;
    }
    void setMode(int m) { proc.model.mode = m; canvas.clearSpectrum(); refreshMode(); canvas.repaint(); say(m == 0 ? "PAINT PADS: click to paint ringing notes." : "PAINT LOOP: pick a sound on the left and draw in time."); }
    void afterPatternChange() { static const int B[] = { 1, 2, 4 }; for (int i = 0; i < 3; ++i) if (B[i] == proc.model.bars) barsBox.setSelectedItemIndex(i, juce::dontSendNotification);
        canvas.clearSpectrum(); canvas.repaint(); refreshPatterns(); refreshLayers(); }
    void refreshMode() {
        const bool pad = proc.model.mode == 0;
        modePad.setToggleState(pad, juce::dontSendNotification); modeLoop.setToggleState(!pad, juce::dontSendNotification);
        bank.setVisible(!pad); padPanel.setVisible(pad);
        for (auto* b : toolBtns) b->setVisible(!pad);
        for (auto* k : padKnobs) k->setVisible(pad); holdBtn.setVisible(pad); freezeBtn.setVisible(pad); stopBtn.setVisible(pad);
        quantBtn.setVisible(!pad); gridBox.setVisible(!pad); repaint();
        refreshLayers(); resized();
    }
    void syncFromModel() {
        static const int B[] = { 1, 2, 4 }; for (int i = 0; i < 3; ++i) if (B[i] == proc.model.bars) barsBox.setSelectedItemIndex(i, juce::dontSendNotification);
        keyBox.setSelectedItemIndex(proc.model.root, juce::dontSendNotification); scaleBox.setSelectedItemIndex(proc.model.scale, juce::dontSendNotification); snapBtn.setToggleState(proc.model.snap, juce::dontSendNotification);
        quantBtn.setToggleState(proc.model.quantize, juce::dontSendNotification); gridBox.setSelectedItemIndex(proc.model.gridDiv == 4 ? 0 : (proc.model.gridDiv == 8 ? 1 : 2), juce::dontSendNotification); gridBox.setEnabled(proc.model.quantize);
        canvas.currentLayer = proc.model.layers.empty() ? -1 : 0; refreshTools(); refreshMode(); refreshPatterns(); canvas.clearSpectrum(); canvas.repaint(); padPanel.repaint();
    }
    void refreshTools() { for (int i = 0; i < 5; ++i) toolBtns[i]->setToggleState((int) canvas.tool == i, juce::dontSendNotification); }
    void refreshPatterns() {
        proc.model.storeCurrent();
        for (int i = 0; i < patBtns.size(); ++i) {
            auto* b = patBtns[i]; const bool used = proc.model.patterns[(size_t) i].used();
            b->setToggleState(i == proc.model.current, juce::dontSendNotification);
            b->setButtonText(juce::String::charToString((juce::juce_wchar) ('A' + i)) + (used && i != proc.model.current ? "." : ""));
        }
    }
    void refreshLayers() {
        const bool pad = proc.model.mode == 0;
        for (int i = 0; i < sp::kLayers; ++i) {
            auto* b = layerBtns[i]; const bool has = !pad && i < (int) proc.model.layers.size(); b->setVisible(has);
            if (has) { const auto& L = proc.model.layers[(size_t) i]; b->setButtonText((L.mute ? "(muted) " : "") + L.name); b->setToggleState(i == canvas.currentLayer, juce::dontSendNotification); tintButton(*b, layerColour(i)); }
        }
        const bool sel = !pad && canvas.currentLayer >= 0 && canvas.currentLayer < (int) proc.model.layers.size();
        for (auto* k : knobs) k->setVisible(sel); muteBtn.setVisible(sel); delLayer.setVisible(sel); layerTitle.setVisible(!pad);
        if (sel) {
            const auto& L = proc.model.layers[(size_t) canvas.currentLayer];
            layerTitle.setText("SOUND " + juce::String(canvas.currentLayer + 1) + ":  " + L.name, juce::dontSendNotification); layerTitle.setColour(juce::Label::textColourId, layerColour(canvas.currentLayer));
            const double v[] = { L.vol, L.tone, L.space, L.pan }; for (int i = 0; i < 4; ++i) knobs[i]->s.setValue(v[i], juce::dontSendNotification);
            muteBtn.setToggleState(L.mute, juce::dontSendNotification);
        } else { layerTitle.setText("Sounds in the loop show here", juce::dontSendNotification); layerTitle.setColour(juce::Label::textColourId, GREY); }
    }
    void selectLayer(int i) { canvas.currentLayer = i; pickIdx = -1; refreshLayers(); proc.audition(i, 60); }
    // a bank click: hear it, and make it the sound DRAW will use
    void pick(int tab, int idx) {
        pickTab = tab; pickIdx = idx;
        const int kind = tab == 0 ? 0 : (tab == 1 ? 1 : 2);
        const juce::String name = tab == 0 ? sp::synthBank()[(size_t) idx].name : (tab == 1 ? proc.drums->sounds()[(size_t) idx]->name : proc.userSamples[(size_t) idx]->name);
        const int existing = proc.layerFor(kind, name);
        if (existing >= 0) { selectLayer(existing); return; }
        if (tab == 1) proc.auditionDrum(name); else { const int l = ensureLayer(); if (l >= 0) { proc.audition(l, 60); selectLayer(l); } }
        say("\"" + name + "\" picked. DRAW on the canvas to use it.");
    }
    int ensureLayer() {
        if (pickIdx < 0) return canvas.currentLayer;
        const int kind = pickTab == 0 ? 0 : (pickTab == 1 ? 1 : 2);
        juce::String name, words; uint32_t seed = 1; std::shared_ptr<gs::SampleData> smp;
        if (pickTab == 0) { const auto& b = sp::synthBank()[(size_t) pickIdx]; name = b.name; words = b.words; seed = b.seed; }
        else if (pickTab == 1) name = proc.drums->sounds()[(size_t) pickIdx]->name;
        else { smp = proc.userSamples[(size_t) pickIdx]; name = smp->name; }
        int l = proc.layerFor(kind, name);
        if (l < 0) l = proc.addLayer(kind, name, words, seed, smp);
        if (l < 0) { say("8 sounds is the max - remove one first (REMOVE)."); return -1; }
        pickIdx = -1; canvas.currentLayer = l; refreshLayers(); return l;
    }
    void dropBank(int tab, int idx, double beat, float midi) {
        pick(tab, idx); const int l = ensureLayer(); if (l < 0) return;
        canvas.addNote(l, canvas.snapB(beat), canvas.pitchFor(canvas.yOf(midi), l)); refreshLayers();
    }
    // STARTING POINTS: a ready-made picture in the current key, so the first click is already beautiful
    void starter(int s) {
        const auto& sc = sp::scaleSteps(proc.model.scale); const int size = (int) sc.size(), root = proc.model.root;
        auto deg = [&](int base, int i) { const int o = (int) std::floor((double) i / size); return (float) (base + root + 12 * o + sc[(size_t) (((i % size) + size) % size)]); };
        auto& D = proc.model.pads; D.clear();
        auto add = [&](int voice, float pitch, float tone, float gain) { sp::PadDot d; d.voice = voice; d.pitch = pitch; d.tone = tone; d.gain = gain; D.push_back(d); };
        const int third = size >= 7 ? 2 : 1, fifth = size >= 7 ? 4 : (size == 5 ? 2 : 4);
        switch (s) {
            case 0: add(sp::VBell, deg(48, 0), 0.62f, 0.7f); add(sp::VBell, deg(60, 0), 0.18f, 0.8f); add(sp::VBell, deg(60, third), 0.34f, 0.7f); add(sp::VBell, deg(60, fifth), 0.48f, 0.7f); add(sp::VBell, deg(72, 0), 0.26f, 0.55f); add(sp::VBell, deg(72, fifth), 0.12f, 0.45f); break;
            case 1: add(sp::VPiano, deg(36, 0), 0.35f, 0.8f); add(sp::VPiano, deg(48, 0), 0.4f, 0.7f); add(sp::VPiano, deg(48, fifth), 0.45f, 0.65f); add(sp::VPiano, deg(60, third), 0.5f, 0.65f); add(sp::VPiano, deg(60, size + 1 < 9 ? 1 : 1) + 12, 0.3f, 0.5f); add(sp::VPiano, deg(72, fifth), 0.25f, 0.45f); break;
            case 2: add(sp::VPad, deg(36, 0), 0.08f, 0.8f); add(sp::VPad, deg(48, 0), 0.2f, 0.7f); add(sp::VPad, deg(48, fifth), 0.3f, 0.65f); add(sp::VPad, deg(60, third), 0.42f, 0.6f); add(sp::VPad, deg(60, size >= 7 ? 6 : 4), 0.55f, 0.45f); add(sp::VBell, deg(72, fifth), 0.15f, 0.45f); break;
            default: add(sp::VPad, deg(36, 0), 0.15f, 0.75f); add(sp::VPad, deg(48, fifth), 0.25f, 0.6f); add(sp::VFeedback, deg(48, 0), 0.45f, 0.75f); add(sp::VFeedback, deg(60, fifth), 0.7f, 0.55f); break;
        }
        proc.model.brush = juce::jlimit(0, 3, s); proc.syncPads(); canvas.repaint(); padPanel.repaint(); refreshPatterns();
        static const char* msg[] = { "A bell chord. Drag a bell a little off its note and listen to it beat, then let it settle.", "Piano strings: related notes make each other ring. Turn up STRINGS for the open-pedal shimmer.",
                                     "An ambient pad. Move notes right to brighten them, left to make them pure.", "Feedback rising over a pad. Give it a few seconds to bloom, drag it to bend." };
        say(msg[juce::jlimit(0, 3, s)]);
    }
    void syncStop() { const bool st = proc.engine.padStopped.load(); stopBtn.setButtonText(st ? "START" : "STOP"); stopBtn.setToggleState(st, juce::dontSendNotification); }
    void timerTick() {
        syncStop();
        if (current == sp::TabKind::Pads || current == sp::TabKind::Loop) { canvas.tick(); canvas.repaint(); }   // a page covers it: no repaints behind it
        if (playBtn.getToggleState() != proc.engine.playing) playBtn.setToggleState(proc.engine.playing, juce::dontSendNotification);
        const bool own = proc.ownClock(); const double h = own ? proc.sync.getOwnBpm() : proc.hostBpm(); hostTempo.setVisible(own || h > 0); bpm.setVisible(!own && h <= 0);
        if (own || h > 0) { const auto t = juce::String(own ? "OWN " : "HOST ") + juce::String(h, h == std::floor(h) ? 0 : 1) + " BPM"; if (hostTempo.getText() != t) hostTempo.setText(t, juce::dontSendNotification); }
        if (auto it = pages.find(sp::TabKind::Arrange); it != pages.end() && it->second) if (auto* pg = dynamic_cast<arr::ArrangePage*>(it->second.get())) pushArrangeView(pg);
        if (proc.keyListening()) {
            juce::String msg; float prog = 0; const int r = proc.pollKeyListen(msg, prog);
            if (r == 0) detectBtn.setButtonText("LISTENING " + juce::String((int) (prog * 100)) + "%");
            else { detectBtn.setButtonText("DETECT KEY"); detectBtn.getProperties().set("accent", false); detectBtn.repaint(); say(msg);
                if (r > 0) { keyBox.setSelectedItemIndex(proc.model.root, juce::dontSendNotification); scaleBox.setSelectedItemIndex(proc.model.scale, juce::dontSendNotification); proc.model.snap = true; snapBtn.setToggleState(true, juce::dontSendNotification); } }
        }
    }
    void paint(juce::Graphics& g) override {
        g.setColour(BG); g.fillRect(getLocalBounds());   // solid: no translucent full-window glows (some hosts / renderers drew them opaque)
        ghosty::drawGhost(g, ghosty::lookFor(3), 34, 36, 38);
        g.setColour(INK); g.setFont(juce::FontOptions(23.0f, juce::Font::bold)); g.drawText("GHOSTY SPECTRUM", 60, 12, 250, 28, juce::Justification::centredLeft);
        g.setColour(CYAN.withAlpha(0.9f)); g.setFont(juce::FontOptions(9.5f, juce::Font::bold)); g.drawText("PAINT WITH SOUND IN A GHOST WORLD", 62, 40, 250, 12, juce::Justification::centredLeft);
        g.setColour(GREY); g.setFont(juce::FontOptions(9.5f, juce::Font::bold)); g.drawText("v" + spectrumVersion(), 248, 40, 50, 12, juce::Justification::centredLeft);
        g.setColour(GREY.withAlpha(0.8f)); g.setFont(juce::FontOptions(10.0f, juce::Font::bold)); g.drawText("PATTERN", 306, 50, 60, 28, juce::Justification::centredLeft);
        g.setColour(CARD); g.fillRoundedRectangle(16.0f, 690.0f, (float) W - 32, 92.0f, 12.0f); g.setColour(PURPLE.withAlpha(0.25f)); g.drawRoundedRectangle(16.5f, 690.5f, (float) W - 33, 91.0f, 12.0f, 1.0f);
        if (proc.model.mode == 0) { g.setColour(GREY); g.setFont(juce::FontOptions(10.5f, juce::Font::bold)); g.drawText("PAD EFFECTS", 30, 694, 200, 12, juce::Justification::left); }
    }
    void resized() override {
        // the tabs live in the bar above the window now; this row keeps the transport + files where they always were
        playBtn.setBounds(540, 12, 70, 32); bpm.setBounds(616, 15, 120, 26); hostTempo.setBounds(616, 15, 120, 26);
        barsBox.setBounds(742, 12, 84, 32); quantBtn.setBounds(832, 12, 92, 32); gridBox.setBounds(928, 12, 66, 32);
        saveBtn.setBounds(1002, 12, 58, 32); loadBtn.setBounds(1064, 12, 58, 32); clearBtn.setBounds(1126, 12, 60, 32);
        for (int i = 0; i < patBtns.size(); ++i) patBtns[i]->setBounds(362 + i * 33, 50, 31, 28);
        copyBtn.setBounds(628, 50, 56, 28); keyBox.setBounds(696, 50, 58, 28); scaleBox.setBounds(758, 50, 138, 28); snapBtn.setBounds(900, 50, 102, 28); detectBtn.setBounds(1006, 50, 108, 28); unlock.setBounds(1118, 50, 68, 28);
        bank.setBounds(16, 60, 282, 624); padPanel.setBounds(16, 60, 282, 624);
        const bool pad = proc.model.mode == 0;
        for (int i = 0; i < 5; ++i) toolBtns[i]->setBounds(306 + i * 84, 86, 80, 30);
        status.setBounds(pad ? 306 : 730, 88, pad ? 880 : 456, 26);
        if (lab) lab->setBounds(0, 0, W, H);
        for (auto& [kind, pg] : pages) if (pg) pg->setBounds(0, 0, W, H);
        canvas.setBounds(306, 120, 880, 564);
        for (int i = 0; i < sp::kLayers; ++i) layerBtns[i]->setBounds(28 + (i % 4) * 150, 700 + (i / 4) * 38, 146, 32);
        layerTitle.setBounds(640, 696, 320, 18);
        for (int i = 0; i < 4; ++i) knobs[i]->setBounds(640 + i * 62, 716, 58, 62);
        muteBtn.setBounds(892, 722, 68, 26); delLayer.setBounds(892, 752, 68, 26);
        for (int i = 0; i < padKnobs.size(); ++i) padKnobs[i]->setBounds(26 + i * 66, 708, 62, 70);
        holdBtn.setBounds(626, 702, 84, 26); freezeBtn.setBounds(626, 731, 84, 26); stopBtn.setBounds(626, 760, 84, 26);
        wavOut.setBounds(968, 698, 218, 26); stemsOut.setBounds(968, 727, 218, 26); arrangeOut.setBounds(968, 756, 218, 26); arcsArrange.setBounds(650, 11, 170, 26);
    }
};

// =====================================================================================================
// ---------------------------------------------------------------- the tab bar (one table: SpecTabs.h) + BYPASS, above every page
struct SpecEditor::TabBar : juce::Component, juce::SettableTooltipClient {
    juce::OwnedArray<juce::TextButton> btns; std::function<void(sp::TabKind)> onSelect; std::function<void()> onTheme; ghosty::BypassButton bypassBtn; juce::TextButton themeBtn { "LIGHT" }, playBtn { "PLAY: ALWAYS" };
    // SYNC: DAW | OWN + the transport cluster: OWN shows the own tempo (type it / TAP) and PLAY; DAW shows the DAW's tempo read-only
    ghosty::SyncButton syncBtn; juce::Label bpmBox, dawTempo; juce::TextButton tapBtn { "TAP" }, ownPlay { "PLAY" }; double taps[8] {}; int nTaps = 0;
    // HOW IT WORKS: the '?' at the left of the bar (WHAT IS SPECTRUM) + a small '?' badge on every tab (that tab's card)
    juce::TextButton helpBtn { "?" }; juce::OwnedArray<juce::TextButton> badges; std::function<void(int card)> onHelp;
    SpecProcessor& proc;
    explicit TabBar(SpecProcessor& p) : bypassBtn(*p.bypassParam), syncBtn(p.sync), proc(p) {
        for (auto& d : sp::tabs()) {
            auto* b = btns.add(new juce::TextButton(d.label)); tintButton(*b, juce::Colour(d.colour)); b->setTooltip(d.tip); b->setComponentID(d.id);
            const auto k = d.kind; b->onClick = [this, k] { if (onSelect) onSelect(k); }; addAndMakeVisible(b);
            b->setTooltip(sp::roleOf(k) + "\n" + d.tip + "  (the small ? on the tab = how it works)");
            auto* q = badges.add(new juce::TextButton("?")); q->setComponentID(juce::String("help_") + d.id); tintButton(*q, juce::Colour(d.colour));
            q->setTooltip(juce::String("How ") + d.label + " works: steps, gestures, drag-out, TRY IT"); q->onClick = [this, k] { if (onHelp) onHelp(sp::guideCardFor(k)); }; addAndMakeVisible(q);
        }
        helpBtn.setComponentID("howItWorks"); tintButton(helpBtn, CYAN); helpBtn.setTooltip("HOW IT WORKS: what Spectrum is, what every tab does, and a TRY IT example for each");
        helpBtn.onClick = [this] { if (onHelp) onHelp(0); }; addAndMakeVisible(helpBtn);
        setTooltip("Ghosty Spectrum v" + spectrumVersion());
        bypassBtn.setComponentID("bypassButton"); bypassBtn.setDark(true); addAndMakeVisible(bypassBtn);
        themeBtn.setComponentID("themeButton"); tintButton(themeBtn, PURPLE); themeBtn.setTooltip("The look of every page: BLACK -> PURPLE -> LIGHT (the button shows the next one; saved with the project)");
        themeBtn.onClick = [this] { if (onTheme) onTheme(); }; addAndMakeVisible(themeBtn); setOpaque(true);
        playBtn.setComponentID("playModeButton"); tintButton(playBtn, CYAN);
        playBtn.setTooltip("PLAY: ALWAYS = the Lab's host loop, ARRANGE and ARCS play whenever the song plays (as always).\n"
                           "PLAY: ON NOTES = they play only while a MIDI note is held (any key; draw one long note per pattern), so muting the FL channel stops them "
                           "and the next note starts them again, in time with the song. Every held key opens the gate AND still does its usual job "
                           "(plays the Lab in KEYS, the pad, ARCS MIDI-in). BYPASS always wins. Saved with the project, never in presets.");
        playBtn.onClick = [this] { if (auto* prm = proc.apvts.getParameter("play_mode")) { const bool on = prm->getValue() > 0.5f; prm->beginChangeGesture(); prm->setValueNotifyingHost(on ? 0.0f : 1.0f); prm->endChangeGesture(); } tick(); };
        addAndMakeVisible(playBtn);
        syncBtn.setComponentID("syncButton"); addAndMakeVisible(syncBtn);
        syncBtn.setTooltip("SYNC: DAW (default) = Spectrum follows the DAW's play button, tempo and song position (PAINT LOOP, the Lab's host loop, ARRANGE, ARCS); "
                           "SPACE stays with the DAW.\nSYNC: OWN = Spectrum runs by itself: its own tempo (type it in or TAP), its own PLAY, and SPACE plays / stops it "
                           "while this window has focus. The DAW's transport is ignored. Saved with the project, never in presets.");
        syncBtn.onModeChanged = [this] { tick(); };
        bpmBox.setComponentID("ownBpm"); bpmBox.setEditable(false, true, false); bpmBox.setJustificationType(juce::Justification::centred); bpmBox.setFont(juce::FontOptions(12.5f, juce::Font::bold));
        bpmBox.setTooltip("Spectrum's own tempo (SYNC: OWN): double-click and type 20 - 999, Enter to set");
        bpmBox.onTextChange = [this] { const double v = bpmBox.getText().retainCharacters("0123456789.").getDoubleValue(); if (v > 0) proc.sync.setOwnBpm(v); bpmBox.setText(juce::String(proc.sync.getOwnBpm(), 1), juce::dontSendNotification); };
        tapBtn.setComponentID("tapButton"); tintButton(tapBtn, CYAN); tapBtn.setTooltip("Tap the own tempo (SYNC: OWN): click in time, tap 2 or more times");
        tapBtn.onClick = [this] { const double now = juce::Time::getMillisecondCounterHiRes();
            if (nTaps > 0 && now - taps[nTaps - 1] > 2000) nTaps = 0;
            if (nTaps == 8) { std::memmove(taps, taps + 1, sizeof(double) * 7); nTaps = 7; } taps[nTaps++] = now;
            if (nTaps >= 2) { proc.sync.setOwnBpm(60000.0 * (nTaps - 1) / (taps[nTaps - 1] - taps[0])); bpmBox.setText(juce::String(proc.sync.getOwnBpm(), 1), juce::dontSendNotification); } };
        ownPlay.setComponentID("ownPlay"); tintButton(ownPlay, GREEN); ownPlay.setTooltip("Play / stop Spectrum's own clock from bar 1 (SYNC: OWN; SPACE does the same)");
        ownPlay.onClick = [this] { proc.toggleOwnPlay(); };
        dawTempo.setComponentID("dawTempo"); dawTempo.setJustificationType(juce::Justification::centred); dawTempo.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        dawTempo.setTooltip("SYNC: DAW - the tempo comes from your project. Switch SYNC to OWN to set Spectrum's own tempo.");
        for (auto* c : std::initializer_list<juce::Component*> { &bpmBox, &tapBtn, &ownPlay, &dawTempo }) addChildComponent(c);
        applyTheme(); tick();
    }
    void applyTheme() {
        syncBtn.setTheme(sp::pal().look);   // house tokens (BLACK / PURPLE / LIGHT)
        bpmBox.setColour(juce::Label::backgroundColourId, sp::pal().field); bpmBox.setColour(juce::Label::textColourId, sp::pal().ink); bpmBox.setColour(juce::Label::outlineColourId, sp::pal().line);
        dawTempo.setColour(juce::Label::backgroundColourId, sp::pal().panel); dawTempo.setColour(juce::Label::textColourId, sp::pal().dim); dawTempo.setColour(juce::Label::outlineColourId, sp::pal().line);
    }
    void tick() {
        const bool onN = proc.playModeP->load() > 0.5f;
        const juce::String t = onN ? (proc.gateOpenUi.load() ? "PLAY: ON NOTES  >" : "PLAY: ON NOTES  ||") : "PLAY: ALWAYS";
        if (playBtn.getButtonText() != t) playBtn.setButtonText(t);
        if (playBtn.getToggleState() != onN) playBtn.setToggleState(onN, juce::dontSendNotification);
        const bool own = proc.ownWanted();
        bpmBox.setVisible(own); tapBtn.setVisible(own); ownPlay.setVisible(own); dawTempo.setVisible(!own);
        if (own) { if (!bpmBox.isBeingEdited()) { const auto t = juce::String(proc.sync.getOwnBpm(), 1); if (bpmBox.getText() != t) bpmBox.setText(t, juce::dontSendNotification); }
                   const bool o = proc.sync.isOwnPlaying(); const juce::String pt = o ? "STOP" : "PLAY"; if (ownPlay.getButtonText() != pt) ownPlay.setButtonText(pt); if (ownPlay.getToggleState() != o) ownPlay.setToggleState(o, juce::dontSendNotification); }
        else { const double h = proc.hostBpm(); const juce::String t = h > 0 ? juce::String(h, 1) + " BPM  DAW" : "DAW TEMPO"; if (dawTempo.getText() != t) dawTempo.setText(t, juce::dontSendNotification); }
        syncBtn.repaint();
    }
    void setCurrent(sp::TabKind k) { const auto& t = sp::tabs(); for (int i = 0; i < btns.size(); ++i) btns[i]->setToggleState(t[(size_t) i].kind == k, juce::dontSendNotification); }
    void paint(juce::Graphics& g) override {
        g.setColour(CARD); g.fillRect(getLocalBounds()); g.setColour(sp::pal().line); g.drawHorizontalLine(getHeight() - 1, 0, (float) getWidth());
    }
    void resized() override {
        int x = kTabsX; const auto& t = sp::tabs();
        helpBtn.setBounds(8, (getHeight() - 26) / 2, 24, 26);
        for (int i = 0; i < btns.size(); ++i) { btns[i]->setBounds(x, 6, t[(size_t) i].width, getHeight() - 12); badges[i]->setBounds(btns[i]->getRight() - 13, 1, 15, 15); badges[i]->toFront(false); x += t[(size_t) i].width + 4; }
        const int y = (getHeight() - 26) / 2; int r = getWidth() - 12;
        auto place = [&](juce::Component& c, int w) { r -= w; c.setBounds(r, y, w, 26); r -= 6; };
        place(bypassBtn, 84); place(themeBtn, 64); place(playBtn, 126); r -= 4;
        // the SYNC cluster: [SYNC][BPM][TAP][PLAY] in OWN, [SYNC][DAW tempo] in DAW
        place(ownPlay, 56); place(tapBtn, 40); place(bpmBox, 58);
        dawTempo.setBounds(bpmBox.getX(), y, ownPlay.getRight() - bpmBox.getX(), 26);
        place(syncBtn, 96);
    }
    static constexpr int kTabsX = 38;
};

SpecEditor::SpecEditor(SpecProcessor& p) : AudioProcessorEditor(p), proc(p) {
    lnf = std::make_unique<LnF>(); setLookAndFeel(lnf.get());
    content = std::make_unique<Content>(p); addAndMakeVisible(*content); content->setBounds(0, 0, W, H);
    tabBar = std::make_unique<TabBar>(p); addAndMakeVisible(*tabBar); tabBar->setBounds(0, 0, W, kBarH);
    tabBar->onSelect = [this](sp::TabKind k) { content->showTab(k); };
    content->onTabChanged = [this](sp::TabKind k) { tabBar->setCurrent(k); maybeHint(k); };
    tabBar->onHelp = [this](int c) { openGuide(c); };
    guide = std::make_unique<sp::SpecGuide>(); guide->setComponentID("guide"); addChildComponent(*guide); guide->setBounds(0, 0, W, H + kBarH);
    guide->onClose = [this] { closeGuide(); }; guide->onTryIt = [this](int c) { tryIt(c); };
    hint = std::make_unique<sp::FirstRunHint>(); hint->setComponentID("firstRunHint"); addChildComponent(*hint); hint->setBounds(270, kBarH + 300, 660, 92);
    hint->onDismiss = [this] { dismissHint(); }; hint->onMore = [this] { const auto k = hint->tab(); dismissHint(); openGuide(sp::guideCardFor(k)); };
    content->addMouseListener(&hintWatch, true);   // the first click on the page puts its hint away
    tabBar->setCurrent(content->current);
    tabBar->onTheme = [this] { setLook(ghosty::themes::next(sp::pal().look)); };
    setLook(proc.uiLook, true);
    typeIn = std::make_unique<ghosty::TypeIn>(*this);
    tips = std::make_unique<juce::TooltipWindow>(this, 700);   // the ONE tooltip window for every page (pages don't make their own)
    setResizable(true, true); setResizeLimits(W * 3 / 4, (H + kBarH) * 3 / 4, W * 2, (H + kBarH) * 2); if (auto* c = getConstrainer()) c->setFixedAspectRatio((double) W / (H + kBarH));
    setSize(W, H + kBarH); startTimerHz(30);
    setWantsKeyboardFocus(true);   // clicks on parts that take no focus land here, so SPACE reaches keyPressed (SYNC: OWN)
}
// ---- HOW IT WORKS + first-run hints
void SpecEditor::openGuide(int card) { if (hint) hint->setVisible(false); guide->showCard(card); guide->setVisible(true); guide->toFront(true); guide->grabKeyboardFocus(); }
void SpecEditor::closeGuide() { guide->setVisible(false); }
bool SpecEditor::guideOpen() const { return guide && guide->isVisible(); }
bool SpecEditor::tryIt(int card) {
    const auto& c = sp::guideCards()[(size_t) juce::jlimit(0, (int) sp::guideCards().size() - 1, card)];
    const auto k = c.tab < 0 ? sp::TabKind::Pads : (sp::TabKind) c.tab;
    closeGuide(); markSeen(k); const bool ok = content->tryIt(k); if (hint) hint->setVisible(false); return ok;
}
void SpecEditor::markSeen(sp::TabKind k) { proc.guideSeen.fetch_or(1 << (int) k); }
void SpecEditor::maybeHint(sp::TabKind k) {
    if (!hint) return;
    if (proc.guideSeen.load() & (1 << (int) k)) { hint->setVisible(false); return; }
    hint->showFor(k); hintArmedMs = juce::Time::getMillisecondCounter();
}
void SpecEditor::dismissHint() { if (hint && hint->isVisible()) { markSeen(hint->tab()); hint->setVisible(false); } }
void SpecEditor::HintWatch::mouseDown(const juce::MouseEvent&) { if (ed.hint && ed.hint->isVisible() && juce::Time::getMillisecondCounter() - ed.hintArmedMs > 250) ed.dismissHint(); }   // the first use of the page
juce::Component* SpecEditor::content_() const { return content.get(); }
bool SpecEditor::loadCanvasFile(const juce::File& f) { return content->loadCanvasFile(f); }
void SpecEditor::setTheme(bool dark, bool force) { setLook(dark ? sp::Look::Dark : sp::Look::Light, force); }
// the house looks (Shared/GhostThemes.h): BLACK -> PURPLE -> LIGHT; the button names the NEXT look
void SpecEditor::setLook(sp::Look look, bool force) {
    if (!force && sp::pal().look == look) return;
    sp::setTheme(look, this); proc.uiLook = look; const bool dark = sp::pal().dark;
    if (auto* l = dynamic_cast<LnF*>(lnf.get())) l->applyColours();
    sendLookAndFeelChange();
    tabBar->bypassBtn.setTheme(look); tabBar->themeBtn.setButtonText(ghosty::themes::displayName(ghosty::themes::next(look))); tabBar->applyTheme();
    content->themeChanged(dark); if (guide) guide->repaint(); if (hint) hint->repaint(); repaint();
}
juce::KeyListener* SpecEditor::ownSpaceKeys() const { return content ? &content->ownSpace : nullptr; }
bool SpecEditor::keyPressed(const juce::KeyPress& k) {   // PAINT PADS / LOOP and the bar: SPACE only in SYNC: OWN (in DAW it goes to the DAW)
    if (k == juce::KeyPress(juce::KeyPress::spaceKey) && proc.ownWanted()) { proc.toggleOwnPlay(); return true; }
    return false;
}
SpecEditor::~SpecEditor() { stopTimer(); if (content) content->removeMouseListener(&hintWatch); guide.reset(); hint.reset(); tips.reset(); typeIn.reset(); tabBar.reset(); content.reset(); setLookAndFeel(nullptr); }
void SpecEditor::paint(juce::Graphics& g) { g.fillAll(BG); }
void SpecEditor::resized() {
    const float sc = (float) getWidth() / W;   // everything is laid out at 1200 wide and scaled together
    if (tabBar) tabBar->setTransform(juce::AffineTransform::scale(sc));
    if (content) content->setTransform(juce::AffineTransform::translation(0, (float) kBarH).scaled(sc));
    if (guide) guide->setTransform(juce::AffineTransform::scale(sc));
    if (hint) hint->setTransform(juce::AffineTransform::scale(sc));
}
void SpecEditor::timerCallback() { content->timerTick(); if (tabBar) tabBar->tick(); }
static bool isAudio(const juce::String& f) { return juce::File(f).hasFileExtension("wav;aif;aiff;mp3;flac;ogg;m4a;caf"); }
bool SpecEditor::isInterestedInFileDrag(const juce::StringArray& f) { for (auto& x : f) if (isAudio(x)) return true; return false; }
void SpecEditor::filesDropped(const juce::StringArray& files, int, int) {
    for (auto& f : files) if (isAudio(f)) { juce::String err; if (auto s = gs::SampleData::fromFile(juce::File(f), err)) proc.userSamples.push_back(s); }
    content->showTab(sp::TabKind::Loop);
    content->bank.tab = 2; content->bank.rebuild(); content->say("Your sounds are in MY SAMPLES. Click one, then DRAW.");
}
