// Ghost Voice - AGPL-3.0-only (see LICENSE, NOTICE.md).
#include "Editor.h"
#include "ChopPage.h"
#include "Look.h"
#include "LiquidToggle.h"
#include "BubbleFx.h"
#include "TerrainView.h"
#include "UnderTheHood.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>

namespace gv
{
namespace th = ghosty::themes;

// ================================================================================================ small pieces
namespace
{
juce::String mmss (double s) { const int t = juce::jmax (0, (int) std::round (s)); return juce::String (t / 60) + ":" + juce::String (t % 60).paddedLeft ('0', 2); }

// a file's waveform peaks and long-term spectrum (computed once per file)
struct Analysis
{
    juce::String key; std::vector<float> peaks, spectrumDb; double seconds = 0;
    static constexpr int kPeaks = 220, kBands = 72;
    static float bandHz (int i) { return 60.0f * std::pow (12000.0f / 60.0f, (float) i / (float) (kBands - 1)); }
    // streamed in 64k-sample blocks (build 3): drawing a card never holds the whole file in memory, however long it is
    void set (const juce::File& f)
    {
        const auto k = f.getFullPathName() + juce::String (f.getLastModificationTime().toMilliseconds());
        if (k == key) return;
        key = k; peaks.clear(); spectrumDb.clear(); seconds = 0;
        if (! f.existsAsFile()) return;
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (f));
        if (rd == nullptr || rd->sampleRate <= 0 || rd->lengthInSamples <= 0) return;
        const auto len = rd->lengthInSamples; const int fs = (int) rd->sampleRate, nch = (int) juce::jmax (1u, rd->numChannels);
        seconds = (double) len / fs;
        peaks.assign (kPeaks, 0.0f);
        // long-term average spectrum: 2048-point Hann frames (hop 1024), at most 600 frames, power averaged
        constexpr int order = 11, N = 1 << order, kBlock = 65536;
        juce::dsp::FFT fft (order);
        std::vector<float> buf (2 * N), acc (N / 2 + 1, 0.0f), ring;   // ring: the samples not yet framed
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
    void paintWave (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float playhead = -1.0f) const
    {
        if (peaks.empty()) return;
        const float w = r.getWidth() / (float) peaks.size();
        for (size_t i = 0; i < peaks.size(); ++i)
        {
            const float h = juce::jmax (1.5f, peaks[i] * r.getHeight());
            const bool before = playhead >= 0 && (float) i / (float) peaks.size() < playhead;
            juce::ColourGradient gr (c.brighter (0.5f), 0, r.getCentreY() - h * 0.5f, c, 0, r.getCentreY(), true);
            g.setGradientFill (gr);
            g.setOpacity (playhead >= 0 && ! before ? 0.55f : 1.0f);
            g.fillRoundedRectangle (r.getX() + w * (float) i, r.getCentreY() - h * 0.5f, juce::jmax (1.0f, w * 0.7f), h, 0.6f);
        }
        g.setOpacity (1.0f);
        if (playhead >= 0) { g.setColour (juce::Colours::white.withAlpha (0.8f)); g.fillRect (r.getX() + r.getWidth() * playhead - 1.0f, r.getY(), 2.0f, r.getHeight()); }
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
        if (fill) { juce::Path f = p; f.lineTo (r.getRight(), r.getBottom()); f.lineTo (r.getX(), r.getBottom()); f.closeSubPath(); g.setColour (c.withAlpha (0.18f)); g.fillPath (f); }
        g.setColour (c); g.strokePath (p, juce::PathStrokeType (1.8f));
    }
};

// the ghost avatar of a voice (never a face)
void avatar (juce::Graphics& g, juce::Point<float> c, float size, int look, juce::Colour tint, float alpha = 1.0f)
{
    g.setColour (tint.withAlpha (0.18f * alpha)); g.fillEllipse (c.x - size * 0.62f, c.y - size * 0.62f, size * 1.24f, size * 1.24f);
    auto L = ghosty::lookFor (look); L.body = tint.brighter (0.6f); L.slime = tint; L.pupil = tint.darker (0.6f);
    L.lines = false;   // build 10: no speed lines (they crossed the card's edge); the ghost stays inside its card
    ghosty::drawGhost (g, L, c.x, c.y + size * 0.05f, size * 0.95f, 0.0f, false, alpha);
}

// the icons the tabs and the list use, drawn in code
void icon (juce::Graphics& g, int which, juce::Rectangle<float> r, juce::Colour c)
{
    g.setColour (c);
    const auto cx = r.getCentreX(), cy = r.getCentreY(), s = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f;
    juce::PathStrokeType st (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    juce::Path p;
    switch (which)
    {
        case 0: p.addRoundedRectangle (cx - s * 0.32f, cy - s * 0.9f, s * 0.64f, s * 1.1f, s * 0.32f); p.addCentredArc (cx, cy - s * 0.05f, s * 0.6f, s * 0.6f, 0, juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi * 3, true);
                p.startNewSubPath (cx, cy + s * 0.55f); p.lineTo (cx, cy + s * 0.9f); break;                      // microphone
        case 1: for (int k = 0; k < 3; ++k) { const float yy = cy - s * 0.5f + s * 0.45f * (float) k; p.startNewSubPath (cx - s * 0.9f, yy); p.lineTo (cx, yy + s * 0.35f); p.lineTo (cx + s * 0.9f, yy); } break;   // layers
        case 2: p.addEllipse (cx - s * 0.75f, cy + s * 0.25f, s * 0.6f, s * 0.5f); p.startNewSubPath (cx - s * 0.15f, cy + s * 0.5f); p.lineTo (cx - s * 0.15f, cy - s * 0.85f); p.lineTo (cx + s * 0.75f, cy - s * 0.55f); break;   // note
        case 3: for (int k = 0; k < 3; ++k) { const float yy = cy - s * 0.6f + s * 0.6f * (float) k; p.startNewSubPath (cx - s * 0.9f, yy); p.lineTo (cx + s * 0.9f, yy); p.addEllipse (cx - s * 0.9f + s * (0.4f + 0.5f * (float) ((k * 7) % 3)) - 3, yy - 3, 6, 6); } break;   // sliders
        case 4: p.addEllipse (cx - s * 0.35f, cy - s * 0.35f, s * 0.7f, s * 0.7f); for (int k = 0; k < 8; ++k) { const float a = (float) k * juce::MathConstants<float>::pi / 4; p.startNewSubPath (cx + std::cos (a) * s * 0.55f, cy + std::sin (a) * s * 0.55f); p.lineTo (cx + std::cos (a) * s * 0.85f, cy + std::sin (a) * s * 0.85f); } break;   // gear
        case 5: for (int k = 0; k < 9; ++k) { const float hh = s * (0.25f + 0.75f * std::abs (std::sin ((float) k * 1.3f))); const float xx = cx - s + s * 0.25f * (float) k; p.startNewSubPath (xx, cy - hh); p.lineTo (xx, cy + hh); } break;   // waveform
        case 6: p.addRectangle (cx - s * 0.8f, cy, s * 0.35f, s * 0.8f); p.addRectangle (cx - s * 0.2f, cy - s * 0.7f, s * 0.35f, s * 1.5f); p.addRectangle (cx + s * 0.4f, cy - s * 0.3f, s * 0.35f, s * 1.1f); break;   // bars
        default: break;
    }
    g.strokePath (p, st);
}
} // namespace

// ================================================================================================ the voice cards (A, B, C)
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
    }
    juce::File file() const { return slot < 0 ? ed.proc.preview : (ed.proc.takes[slot].isEmpty() ? juce::File() : juce::File (ed.proc.takes[slot][0])); }
    juce::Colour tint() const { const auto& P = ed.pal(); return slot == slotA ? col::voiceA (P) : slot == slotB ? col::voiceB (P) : slot < 0 ? col::voiceC (P) : P.accent3; }
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
        rec.setButtonText (ed.proc.recordingSlot() == slot ? "STOP" : "REC");
        repaint();
    }
    juce::Rectangle<float> playButton() const { return { 16.0f, 52.0f, 40.0f, 40.0f }; }
    juce::Rectangle<float> closeButton() const { return { (float) getWidth() - 32.0f, 12.0f, 20.0f, 20.0f }; }
    void mouseUp (const juce::MouseEvent& e) override
    {
        if (e.mouseWasDraggedSinceMouseDown()) return;
        const auto f = file();
        if (playButton().contains (e.position) && f.existsAsFile()) { if (ed.proc.isPlaying (f)) ed.proc.stopPlaying(); else ed.proc.play (f); }
        else if (slot >= 0 && closeButton().contains (e.position) && ! ed.proc.takes[slot].isEmpty()) ed.proc.clearSlot (slot);
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {   // voice C's preview can be dragged out too
        if (slot >= 0 || dragging || e.getDistanceFromDragStart() < 6 || ! file().existsAsFile()) return;
        dragging = true;
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file().getFullPathName() }, false, this, [this] { dragging = false; });
    }
    void paint (juce::Graphics& g) override
    {
        const auto& P = ed.pal();
        auto r = getLocalBounds().toFloat().reduced (6.0f);
        const bool isC = slot < 0;
        const float pulse = 0.6f + 0.4f * std::sin (ed.clock * 2.2f);
        panel (g, P, r, 14.0f, isC ? col::voiceC (P).withMultipliedAlpha (ed.proc.preview.existsAsFile() ? pulse : 0.45f) : (hover ? tint() : juce::Colour()));
        // title row: avatar, name, (role)
        avatar (g, { r.getX() + 26, r.getY() + 24 }, 24.0f, slot == slotA ? 0 : slot == slotB ? 3 : slot < 0 ? 4 : 6, tint());
        const juce::String name = slot == slotA ? "Voice A" : slot == slotB ? "Voice B" : slot < 0 ? ed.proc.voiceName : "Performance";
        const juce::String role = slot < 0 ? " (new identity)" : slot == slotPerf ? " (what is sung)" : " (reference)";
        g.setColour (P.ink); g.setFont (gfont (16.0f));
        const float nw = ghosty::textWidth (gfont (16.0f), name);
        g.drawText (name, juce::Rectangle<float> (r.getX() + 46, r.getY() + 8, nw + 4, 28), juce::Justification::centredLeft);
        g.setColour (P.dim); g.setFont (gfont (12.0f, false));
        g.drawText (role, juce::Rectangle<float> (r.getX() + 46 + nw, r.getY() + 8, 160, 28), juce::Justification::centredLeft);
        if (isC && ed.proc.previewId.valid)
        {   // measured: the check
            auto c = closeButton().translated (-4, 0).expanded (3);
            g.setColour (P.good); g.fillEllipse (c);
            juce::Path t; t.startNewSubPath (c.getX() + c.getWidth() * 0.28f, c.getCentreY()); t.lineTo (c.getX() + c.getWidth() * 0.45f, c.getBottom() - c.getHeight() * 0.3f); t.lineTo (c.getRight() - c.getWidth() * 0.25f, c.getY() + c.getHeight() * 0.3f);
            g.setColour (P.onAccent); g.strokePath (t, juce::PathStrokeType (2.0f));
        }
        else if (! isC && ! ed.proc.takes[slot].isEmpty())
        {
            auto c = closeButton().reduced (5);
            g.setColour (P.dim); g.drawLine (c.getX(), c.getY(), c.getRight(), c.getBottom(), 1.6f); g.drawLine (c.getRight(), c.getY(), c.getX(), c.getBottom(), 1.6f);
        }
        // the wave strip
        auto ws = juce::Rectangle<float> (r.getX() + 8, r.getY() + 42, r.getWidth() - 16, 60);
        { const auto& hb = splash::handBox (ws, 10, splash::seedOf (ws)); g.setColour (P.waveBg); g.fillPath (hb); splash::inkLine (g, hb, splash::seedOf (ws) + 1, P.ink.withAlpha (0.55f), 0.9f, 0.6f); }
        const auto f = file();
        const bool playing = f.existsAsFile() && ed.proc.isPlaying (f);
        auto pb = playButton().translated (r.getX() - 6, r.getY() - 6 + 0);
        g.setColour (f.existsAsFile() ? P.cell.brighter (0.2f) : P.cell); g.fillEllipse (pb);
        g.setColour (f.existsAsFile() ? P.ink : P.dim);
        if (playing) { g.fillRect (pb.getCentreX() - 6, pb.getCentreY() - 7, 4.0f, 14.0f); g.fillRect (pb.getCentreX() + 2, pb.getCentreY() - 7, 4.0f, 14.0f); }
        else { juce::Path tri; tri.addTriangle (pb.getCentreX() - 5, pb.getCentreY() - 8, pb.getCentreX() - 5, pb.getCentreY() + 8, pb.getCentreX() + 8, pb.getCentreY()); g.fillPath (tri); }
        auto waveArea = ws.withTrimmedLeft (54).reduced (6, 8).withTrimmedBottom (8);
        if (! an.peaks.empty()) an.paintWave (g, waveArea, tint(), playing ? ed.proc.playPosition() : -1.0f);
        else
        {
            g.setColour (P.dim); g.setFont (gfont (12.0f, false));
            const juce::String hint = slot < 0 ? "press CREATE NEW VOICE C" : ed.proc.recordingSlot() == slot ? "recording...  " + juce::String (ed.proc.recordedSeconds(), 1) + " s" : "drop a recording here";
            g.drawText (hint, waveArea, juce::Justification::centred);
        }
        g.setColour (P.dim); g.setFont (gfont (10.5f, false));
        g.drawText (mmss (playing ? an.seconds * ed.proc.playPosition() : 0) + " / " + mmss (an.seconds), ws.reduced (8, 3), juce::Justification::bottomRight);
        // the file line
        juce::String line;
        if (slot >= 0)
        {
            const auto& t = ed.proc.takes[slot];
            line = t.isEmpty() ? "no recording yet" : (t.size() == 1 ? juce::File (t[0]).getFileName() : juce::File (t[0]).getFileName() + "  + " + juce::String (t.size() - 1) + " more");
        }
        else if (ed.proc.preview.existsAsFile() && ed.proc.previewQuick)
            line = "NATURAL instant preview  -  " + juce::String (juce::roundToInt (ed.proc.previewBalance * 100)) + " % B  -  NEURAL is still singing";
        else if (ed.proc.preview.existsAsFile())
            line = engineName (ed.proc.previewEngine) + "  -  " + juce::String (juce::roundToInt (ed.proc.previewBalance * 100)) + " % B"
                 + (ed.proc.previewEngine == Engine::seedvc || ed.proc.previewEngine == Engine::best ? "  -  novelty " + juce::String (juce::roundToInt (ed.proc.previewSettings.novelty * 100)) + " %" : juce::String())
                 + "   (drag it out)";
        else line = "not created yet";
        g.setColour (P.dim); g.setFont (gfont (11.5f, false));
        g.drawText (line, juce::Rectangle<float> (r.getX() + 12, r.getBottom() - 30, r.getWidth() - (slot >= 0 ? 150 : 24), 22), juce::Justification::centredLeft, true);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (6).removeFromBottom (32).reduced (8, 5);
        rec.setBounds (r.removeFromRight (58)); r.removeFromRight (6); add.setBounds (r.removeFromRight (58));
    }
    Editor& ed; const int slot;
    juce::TextButton add { "ADD" }, rec { "REC" };
    Analysis an; bool hover = false, dragging = false;
    std::unique_ptr<juce::FileChooser> chooser;
};

// ================================================================================================ tabs, list, buttons
class TabButton : public juce::Button
{
public:
    TabButton (Editor& e, int pg, int ic, juce::String t, juce::String s) : juce::Button (t), ed (e), page (pg), iconId (ic), title (std::move (t)), sub (std::move (s))
    { setMouseCursor (juce::MouseCursor::PointingHandCursor); onClick = [this] { ed.showPage (page); }; }
    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const auto& P = ed.pal();
        auto r = getLocalBounds().toFloat().reduced (2);
        const bool on = ed.page() == page;
        if (on) { juce::ColourGradient gr (P.violet.brighter (0.15f), r.getX(), r.getY(), P.violet.darker (0.35f), r.getRight(), r.getBottom(), false); g.setGradientFill (gr); }
        else g.setColour (over ? P.cell.brighter (0.12f) : P.cell);
        g.setColour (juce::Colours::transparentBlack);   // (hand-drawn below)
        splash::handButton (g, P, r, on, true, over, 900 + page);
        const auto ink = on ? juce::Colours::white : P.ink;
        // build 10: the title and the line under it ALWAYS fit whole (never cut off with "..."): the icon narrows and the text
        // shrinks to fit the tab (titleSize / subSize, checked by the tests)
        const float iw = r.getWidth() > 150 ? 40.0f : 28.0f;
        // build 10.1: the icon + the two lines sit as one block CENTRED in the tab (short names like Tools / TERRAIN too)
        const float room = r.getWidth() - iw - 6;
        const float ts = titleSize (room), ss = subSize (room);
        const float textW = juce::jmin (room, juce::jmax (ghosty::textWidth (gfont (ts), title), ghosty::textWidth (gfont (ss, false), sub)) + 2.0f);
        const float blockW = iw + textW;
        auto blk = r.withSizeKeepingCentre (blockW, r.getHeight());
        icon (g, iconId, blk.removeFromLeft (iw).reduced (iw > 30 ? 9.0f : 5.0f, 9.0f), ink);
        g.setColour (ink); g.setFont (gfont (ts)); g.drawText (title, blk.withTrimmedBottom (blk.getHeight() * 0.45f).withTrimmedTop (4), juce::Justification::bottomLeft, false);
        g.setColour (on ? juce::Colours::white.withAlpha (0.85f) : P.dim); g.setFont (gfont (ss, false));
        g.drawText (sub, blk.withTrimmedTop (blk.getHeight() * 0.55f).withTrimmedBottom (2), juce::Justification::topLeft, false);
    }
    float titleSize (float w) const { return juce::jlimit (8.5f, 14.0f, 14.0f * (w - 2.0f) / juce::jmax (1.0f, ghosty::textWidth (gfont (14.0f), title))); }
    float subSize (float w) const { return juce::jlimit (8.0f, 11.0f, 11.0f * (w - 2.0f) / juce::jmax (1.0f, ghosty::textWidth (gfont (11.0f, false), sub))); }
    bool textFits() const
    {   // the same sums as paint: both lines fit their room at the chosen sizes
        auto r = getLocalBounds().toFloat().reduced (2); const float iw = r.getWidth() > 150 ? 40.0f : 28.0f; const float w = r.getWidth() - iw - 6;
        return ghosty::textWidth (gfont (titleSize (w)), title) <= w + 0.5f && ghosty::textWidth (gfont (subSize (w), false), sub) <= w + 0.5f;
    }
    Editor& ed; int page, iconId; juce::String title, sub;
};

class NavItem : public juce::Button
{
public:
    NavItem (Editor& e, int pg, int ic, juce::String t, juce::String s = {}) : juce::Button (t), ed (e), page (pg), iconId (ic), title (std::move (t)), sub (std::move (s))
    { setMouseCursor (juce::MouseCursor::PointingHandCursor); onClick = [this] { if (onPick) onPick(); ed.showPage (page); }; }
    std::function<void()> onPick; std::function<bool()> isOn;
    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const auto& P = ed.pal();
        auto r = getLocalBounds().toFloat().reduced (6, 3);
        const bool on = isOn ? isOn() : ed.page() == page && sub.isNotEmpty() == (page == Editor::lab);
        if (on) { const auto& hb = splash::handBox (r, 10, 700 + page * 3 + (sub.isNotEmpty() ? 1 : 0)); splash::watercolour (g, hb, P.violet.withAlpha (0.3f), 700 + page); splash::inkLine (g, hb, 701 + page, P.violet, 1.2f, 0.8f); }
        else if (over) { const auto& hb = splash::handBox (r, 10, 700 + page * 3); g.setColour (P.card); g.fillPath (hb); splash::inkLine (g, hb, 702 + page, P.ink.withAlpha (0.5f), 0.9f, 0.6f); }
        icon (g, iconId, r.removeFromLeft (42).reduced (11), P.ink);
        g.setColour (P.ink); g.setFont (gfont (13.5f, sub.isNotEmpty()));
        if (sub.isEmpty()) g.drawText (title, r, juce::Justification::centredLeft);
        else { g.drawText (title, r.withTrimmedBottom (r.getHeight() * 0.5f), juce::Justification::bottomLeft); g.setColour (P.dim); g.setFont (gfont (11.5f, false)); g.drawText (sub, r.withTrimmedTop (r.getHeight() * 0.5f), juce::Justification::topLeft); }
    }
    Editor& ed; int page, iconId; juce::String title, sub;
};

// the big CREATE NEW VOICE C button
class BigButton : public juce::Button
{
public:
    BigButton (Editor& e, juce::String t, juce::String s) : juce::Button (t), ed (e), title (std::move (t)), sub (std::move (s)) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const auto& P = ed.pal();
        auto r = getLocalBounds().toFloat().reduced (8);
        const float a = isEnabled() ? 1.0f : 0.45f, pulse = 0.5f + 0.5f * std::sin (ed.clock * 2.0f);
        juce::ignoreUnused (pulse);
        const auto c0 = P.isLight ? P.violet.brighter (over ? 0.35f : 0.2f) : splash::art().on0.brighter (over ? 0.2f : 0.0f);   // build 12: DARK keeps it deep so white reads on it
        const auto c1 = P.isLight ? col::voiceC (P).darker (down ? 0.6f : 0.4f) : splash::art().on1.darker (down ? 0.4f : 0.15f);
        juce::ColourGradient gr (c0.withMultipliedAlpha (a), r.getX(), r.getY(), c1.withMultipliedAlpha (a), r.getRight(), r.getBottom(), false);
        { const auto& hb = splash::handBox (r, 16, 4242); g.setGradientFill (gr); g.fillPath (hb); splash::hatch (g, hb, 4242, splash::art().onHatch.withAlpha (0.22f * a), 5.0f); splash::inkLine (g, hb, 4243, splash::art().onInk, 1.6f, 1.0f); }

        // sparkle
        auto sp = r.removeFromLeft (r.getHeight()).reduced (r.getHeight() * 0.26f);
        juce::Path star; const float cx = sp.getCentreX(), cy = sp.getCentreY(), s = sp.getWidth() * 0.5f;
        star.startNewSubPath (cx, cy - s); star.quadraticTo (cx, cy, cx + s, cy); star.quadraticTo (cx, cy, cx, cy + s); star.quadraticTo (cx, cy, cx - s, cy); star.quadraticTo (cx, cy, cx, cy - s); star.closeSubPath();
        g.setColour (juce::Colours::white.withAlpha (a)); g.fillPath (star);
        g.fillEllipse (cx + s * 0.9f, cy - s * 1.1f, s * 0.35f, s * 0.35f);
        g.setColour (juce::Colours::white.withAlpha (a)); g.setFont (gfont (22.0f));
        g.drawText (title, r.withTrimmedBottom (r.getHeight() * 0.42f), juce::Justification::bottomLeft);
        g.setColour (juce::Colours::white.withAlpha (0.85f * a)); g.setFont (gfont (13.0f, false));
        g.drawText (sub, r.withTrimmedTop (r.getHeight() * 0.6f), juce::Justification::topLeft);
    }
    Editor& ed; juce::String title, sub;
};

// a knob with its name and value under it
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
        auto r = getLocalBounds().toFloat().withTrimmedTop ((float) getWidth() * 0.82f);
        g.setColour (ink); g.setFont (gfont (12.5f)); g.drawText (name, r.removeFromTop (16), juce::Justification::centred);
        g.setColour (dim); g.setFont (gfont (12.0f, false)); g.drawText (format (s.getValue()), r.removeFromTop (16), juce::Justification::centred);
    }
    void resized() override { s.setBounds (getLocalBounds().removeFromTop ((int) ((float) getWidth() * 0.82f)).reduced (6)); }
    juce::Slider s; juce::String name; std::function<juce::String (double)> format; std::function<void (double)> onChange;
    juce::Colour ink, dim;
};

// a row of mutually exclusive choices (segmented)
struct Segments : public juce::Component, public juce::SettableTooltipClient
{
    Segments (Editor& e, juce::StringArray n) : ed (e), names (std::move (n)) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
    void paint (juce::Graphics& g) override
    {
        const auto& P = ed.pal();
        auto r = getLocalBounds().toFloat();
        { const auto& hb = splash::handBox (r, 8, splash::seedOf (r)); g.setColour (P.card); g.fillPath (hb); splash::inkLine (g, hb, splash::seedOf (r) + 1, P.ink, 1.0f, 0.6f); }
        const float w = r.getWidth() / (float) names.size();
        for (int i = 0; i < names.size(); ++i)
        {
            auto c = juce::Rectangle<float> (r.getX() + w * (float) i, r.getY(), w, r.getHeight()).reduced (3);
            if (i == selected) splash::handButton (g, P, c.expanded (1.5f), true, isEnabled(), false, splash::seedOf (c));
            g.setColour (i == selected ? juce::Colours::white : (enabled.size() > i && ! enabled[i] ? P.dim.withAlpha (0.5f) : P.ink)); g.setFont (gfont (12.5f, i == selected));
            g.drawText (names[i], c, juce::Justification::centred);
        }
    }
    void mouseUp (const juce::MouseEvent& e) override
    {
        const int i = juce::jlimit (0, names.size() - 1, (int) (e.position.x / ((float) getWidth() / (float) names.size())));
        if (enabled.size() > i && ! enabled[i]) return;
        selected = i; repaint(); if (onChange) onChange (i);
    }
    Editor& ed; juce::StringArray names; int selected = 0; std::vector<bool> enabled; std::function<void (int)> onChange;
};

// ================================================================================================ the take (song) view: drag it out
class TakeView : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit TakeView (Editor& e) : ed (e) { setMouseCursor (juce::MouseCursor::DraggingHandCursor); setTooltip ("Drag the take into your DAW (a WAV file). Click to play / stop"); }
    void paint (juce::Graphics& g) override
    {
        const auto& P = ed.pal();
        auto r = getLocalBounds().toFloat();
        { const auto& hb = splash::handBox (r, 10, splash::seedOf (r)); g.setColour (P.waveBg); g.fillPath (hb); splash::inkLine (g, hb, splash::seedOf (r) + 1, P.ink.withAlpha (0.55f), 0.9f, 0.6f); }
        an.set (ed.proc.result);
        if (an.peaks.empty()) { g.setColour (P.dim); g.setFont (gfont (12.0f, false)); g.drawText ("your new take appears here", r, juce::Justification::centred); return; }
        an.paintWave (g, r.reduced (10, 12), col::voiceC (P), ed.proc.isPlaying (ed.proc.result) ? ed.proc.playPosition() : -1.0f);
        g.setColour (P.dim); g.setFont (gfont (10.0f)); g.drawText ("DRAG ME OUT", r.reduced (10, 5), juce::Justification::topRight);
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

// ================================================================================================ the parts
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
    LiquidToggle matchRange { "Match Range" }, promptFree { "Prompt-Free" }, gpu { "Apple GPU" }, rights { "I have the rights to these voices" };
    LiquidToggle keepCons { "Keep My Consonants" }, instant { "Instant Preview (NATURAL)" };
    juce::TextButton perfRec { "REC MY MELODY" }, perfAdd { "ADD AUDIO GUIDE" };
    // VOICE LIBRARY
    juce::Viewport libView; juce::Component libContent;
    std::vector<std::unique_ptr<juce::Component>> libCards;
    // SING ENGINE
    VoiceCard songPerf;
    ghosty::spirit::BoardView board;
    juce::ComboBox motion, rate; juce::Slider depth { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    std::unique_ptr<BigButton> sing;
    juce::TextButton stop { "STOP" };
    double progressValue = 0; juce::ProgressBar progress { progressValue };
    TakeView take;
    LiquidToggle rights2 { "I have the rights to these voices" };
    // TOOLS
    ghost::terrain::TerrainView terrainView;
    juce::Slider terrainAmt { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    // SETTINGS
    juce::TextButton install { "INSTALL VOICE ENGINE" }, reveal { "SHOW ENGINE FOLDER" };
    Segments theme;
    LiquidToggle cpuOnly { "CPU only (skip the Apple GPU)" }, bubbleToggle { "Bubbles on presses" };
    BubbleFx bubbles;
    // UNDER THE HOOD
    std::unique_ptr<HoodPage> hood;
    // CHOP (build 8)
    std::unique_ptr<ChopPage> chop;
    // ABOUT / CREDITS (the one place the open-source parts are named)
    juce::TextButton about { "ABOUT / CREDITS" }, creditsClose { "CLOSE" };
    juce::TextEditor creditsBox;
    bool creditsOn = false;
    // shared
    Segments engineSel;                     // ENGINE (IDENTITY LAB + SING ENGINE)
    juce::TooltipWindow tips { &ed, 500 };
    Analysis anA, anB, anC;
    struct Mote { float t, lane, speed, size; };
    std::vector<Mote> motes;
};

// ================================================================================================ editor
Editor::Editor (Processor& p) : juce::AudioProcessorEditor (p), proc (p), lnf (std::make_unique<Look>()), parts (std::make_unique<Parts> (*this))
{
    auto& q = *parts;
    setLookAndFeel (lnf.get());
    const char* tabNames[][2] { { "Identity Lab", "Create New Voice" }, { "Voice Library", "Saved Voices" }, { "Sing Engine", "Generate Singing" }, { "Tools", "TERRAIN" }, { "Settings", "Engine & Look" }, { "Under the Hood", "Original Controls" }, { "Chop", "Play & Chop" } };
    const int tabIcons[] { 0, 1, 2, 3, 4, 3, 5 };
    for (int i : { (int) chop, 0, 1, 2, 3, 4, 5 })   // CHOP (build 8) is the first tab: the instrument
    { q.tabs.push_back (std::make_unique<TabButton> (*this, i, tabIcons[i], tabNames[i][0], tabNames[i][1])); addAndMakeVisible (*q.tabs.back()); }
    q.nav.push_back (std::make_unique<NavItem> (*this, lab, 0, "Ghost Voice", "Identity Lab"));
    q.nav.push_back (std::make_unique<NavItem> (*this, lab, 2, "Ghost Voice Classic", "NATURAL / CLASSIC"));
    q.nav.back()->onPick = [this] { if (! isDsp (proc.engine)) { proc.engine = Engine::natural; refresh(); } };
    q.nav.back()->isOn = [this] { return pageIndex == lab && isDsp (proc.engine); };
    q.nav[0]->isOn = [this] { return pageIndex == lab && ! isDsp (proc.engine); };
    q.nav.push_back (std::make_unique<NavItem> (*this, sing, 5, "Voice Conversion"));
    q.nav.push_back (std::make_unique<NavItem> (*this, library, 1, "Voice Library"));
    q.nav.push_back (std::make_unique<NavItem> (*this, tools, 6, "TERRAIN"));
    q.nav.push_back (std::make_unique<NavItem> (*this, prefs, 4, "Preferences"));
    q.nav.push_back (std::make_unique<NavItem> (*this, hood, 3, "Under the Hood", "Original controls"));
    for (auto& n : q.nav) addAndMakeVisible (*n);
    q.hood = std::make_unique<HoodPage> (*this);
    addChildComponent (*q.hood);
    q.chop = std::make_unique<ChopPage> (proc, [this]() -> const ghosty::themes::Palette& { return pal(); }, [this] { showPage (lab); });
    addChildComponent (*q.chop);

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
    q.board.hint = "the puck is the voice balance   -   drag it, or let MOTION move it in time with the song";
    q.board.palette = [this] () -> const th::Palette& { return pal(); };
    q.board.onPuckMoved = [this] (float, float) { refresh(); };
    q.board.onGhostMoved = [this] (int, float, float) { refresh(); };
    q.board.livePuck = [this] { return proc.motionOn() ? ghosty::spirit::orbitPuck (proc.board.orbit, proc.board.activePath(), { proc.board.puckX, proc.board.puckY }, (double) clock * proc.bpm() / 60.0) : juce::Point<float> { proc.board.puckX, proc.board.puckY }; };
    q.board.paintPath = [this] (juce::Graphics& g)
    {
        auto& b = proc.board; if (b.ghosts.size() < 2) return;
        const auto a = parts->board.area();
        auto at = [&] (float x, float y) { return juce::Point<float> (a.getX() + x * a.getWidth(), a.getY() + y * a.getHeight()); };
        const auto pa = at (b.ghosts[0].x, b.ghosts[0].y), pb = at (b.ghosts[1].x, b.ghosts[1].y), pp = at (parts->board.shownPuck.x, parts->board.shownPuck.y);
        for (auto& m : parts->motes)
        {   // motes flow A -> the puck -> B, tinted from A's colour to B's (our own small particle drawing)
            const float t = m.t;
            const auto p1 = t < 0.5f ? pa + (pp - pa) * (t * 2.0f) : pp + (pb - pp) * ((t - 0.5f) * 2.0f);
            const float bend = std::sin (t * juce::MathConstants<float>::pi) * m.lane * a.getHeight() * 0.16f;
            g.setColour (col::voiceA (pal()).interpolatedWith (col::voiceB (pal()), t).withAlpha (0.15f + 0.5f * std::sin (t * juce::MathConstants<float>::pi)));
            g.fillEllipse (p1.x - m.size, p1.y + bend - m.size, 2 * m.size, 2 * m.size);
        }
    };
    auto& rng = juce::Random::getSystemRandom();
    for (int i = 0; i < 70; ++i) q.motes.push_back ({ rng.nextFloat(), rng.nextFloat() * 2.0f - 1.0f, 0.002f + 0.004f * rng.nextFloat(), 1.0f + 2.2f * rng.nextFloat() });
    q.motion.addItemList ({ "MOTION OFF", "CIRCLE", "FIGURE 8", "WANDER" }, 1);
    q.motion.setTooltip ("MOTION: the puck moves in time with the song, so the voice drifts between A and B as it sings (each blend step is sung, then crossfaded)");
    q.motion.onChange = [this] { const int i = parts->motion.getSelectedItemIndex(); proc.board.orbit.mode = i == 1 ? ghosty::spirit::Orbit::Circle : i == 2 ? ghosty::spirit::Orbit::Figure8 : i == 3 ? ghosty::spirit::Orbit::RandomWalk : ghosty::spirit::Orbit::Off; refresh(); };
    q.rate.addItemList ({ "1 BAR", "2 BARS", "4 BARS", "8 BARS" }, 1);
    q.rate.setTooltip ("How long one trip of the puck takes, in bars of the song's tempo");
    q.rate.onChange = [this] { const int bars = 1 << juce::jmax (0, parts->rate.getSelectedItemIndex()); proc.board.orbit.rate = 1.0f / (4.0f * (float) bars); proc.board.orbit.sync = true; refresh(); };
    q.depth.setRange (0.02, 0.5, 0.01); q.depth.setTooltip ("How far the puck travels");
    q.depth.onValueChange = [this] { proc.board.orbit.depth = (float) parts->depth.getValue(); };
    q.sing->onClick = [this] { proc.renderSong(); refresh(); };
    q.stop.onClick = [this] { proc.cancelAll(); refresh(); };
    q.rights2.setTooltip (q.rights.getTooltip());
    q.rights2.onChange = [this] (bool on) { proc.rights = on; refresh(); };

    // ---- TOOLS
    addChildComponent (q.terrainView); addChildComponent (q.terrainAmt);
    q.terrainAmt.setRange (0, 1, 0.01); q.terrainAmt.setTooltip ("How much the land moves the engine's own controls");
    q.terrainAmt.onValueChange = [this] { proc.terrainAmount = (float) parts->terrainAmt.getValue(); refresh(); };

    // ---- SETTINGS
    for (juce::Component* c : std::initializer_list<juce::Component*> { &q.install, &q.reveal, &q.theme, &q.cpuOnly, &q.bubbleToggle }) addChildComponent (c);
    q.bubbleToggle.setTooltip ("A soap bubble blooms on every press (off when macOS Reduce motion is on)");
    q.bubbleToggle.onChange = [this] (bool on) { proc.bubbles = on; parts->bubbles.enabled = on; };
    // BUBBLES: on top of everything, never taking the mouse; buttons, switches and the segment pickers bloom
    addAndMakeVisible (q.bubbles);
    q.bubbles.wants = [] (juce::Component* c) { return dynamic_cast<juce::Button*> (c) != nullptr || dynamic_cast<LiquidToggle*> (c) != nullptr || dynamic_cast<Segments*> (c) != nullptr; };
    addMouseListener (&q.bubbles, true);
    q.install.setTooltip ("Downloads the open-source neural voice engine and its models (about 2.5 GB, once) into the engine folder. Nothing is uploaded, ever. Who made it: ABOUT / CREDITS");
    q.install.onClick = [this] { proc.installEngine(); refresh(); };
    q.reveal.onClick = [] { auto d = engineDir(); if (! d.exists()) d = d.getParentDirectory(); d.revealToUser(); };
    q.theme.onChange = [this] (int i) { proc.theme = i == 1 ? th::Theme::Dark : th::Theme::Light; applyTheme(); };   // build 12: LIGHT (SPLASH) / DARK
    q.cpuOnly.onChange = [this] (bool on) { proc.settings.cpuOnly = on; refresh(); };

    proc.onChange = [sp = juce::Component::SafePointer<Editor> (this)] { if (sp != nullptr) juce::MessageManager::callAsync ([sp] { if (sp != nullptr) sp->refresh(); }); };
    // ---- ABOUT / CREDITS
    addAndMakeVisible (q.about); q.about.setTooltip ("Who made the open-source parts inside Ghost Voice, their licences and where their source code is");
    q.about.onClick = [this] { showCredits (! parts->creditsOn); };
    addChildComponent (q.creditsBox); addChildComponent (q.creditsClose);
    q.creditsBox.setMultiLine (true, true); q.creditsBox.setReadOnly (true); q.creditsBox.setScrollbarsShown (true); q.creditsBox.setCaretVisible (false);
    q.creditsBox.setText (creditsText(), false);
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
    if (auto* l = dynamic_cast<Look*> (&getLookAndFeel())) l->setPalette (pal());
    auto& q = *parts;
    const auto& P = pal();
    q.theme.selected = proc.theme == th::Theme::Light ? 0 : 1;
    for (auto* k : { &q.quality, &q.follow, &q.pitch, &q.tone, &q.timbre, &q.texture, &q.breath }) { k->ink = P.ink; k->dim = P.dim; }
    q.balance.getProperties().set ("c0", (juce::int64) col::voiceA (P).getARGB()); q.balance.getProperties().set ("c1", (juce::int64) col::voiceB (P).getARGB());
    q.novelty.getProperties().set ("c0", (juce::int64) P.violet.getARGB()); q.novelty.getProperties().set ("c1", (juce::int64) col::voiceC (P).getARGB());
    q.bubbles.c0 = P.violet; q.bubbles.c1 = col::voiceC (P); q.bubbles.c2 = P.isLight ? juce::Colour (0xff5f9e12) : juce::Colour (0xffe8f27c);
    for (auto* t : { &q.matchRange, &q.promptFree, &q.gpu, &q.rights, &q.rights2, &q.cpuOnly, &q.bubbleToggle, &q.keepCons, &q.instant })
    { t->on = P.violet; t->off = P.cell.brighter (0.1f); t->text = P.ink; t->ink = P.isLight ? juce::Colours::white : juce::Colour (0xfff5f1ff); t->edge = col::edge (P); }
    auto tp = ghost::terrain::Palette::ghostSlice();
    if (P.isLight) { tp.ground = P.waveBg; tp.ring = P.ink.withAlpha (0.8f); tp.ringDim = P.ink.withAlpha (0.35f); tp.text = P.ink; tp.dim = P.dim; tp.button = P.cell; tp.buttonHi = P.line; }
    else if (proc.theme == th::Theme::Dark) { tp.ground = P.window; }
    tp.font = [] (float h, bool bold) { return gfont (h, bold); };
    q.terrainView.setPalette (tp);
    if (q.hood) q.hood->applyTheme();
    q.creditsBox.setColour (juce::TextEditor::backgroundColourId, P.panel); q.creditsBox.setColour (juce::TextEditor::textColourId, P.ink);
    q.creditsBox.setColour (juce::TextEditor::outlineColourId, col::edge (P)); q.creditsBox.setColour (juce::TextEditor::focusedOutlineColourId, P.violet);
    q.creditsBox.applyFontToAllText (gfont (13.5f, false)); q.creditsBox.applyColourToAllText (P.ink);
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
    q.bubbleToggle.setToggleState (proc.bubbles, false); q.bubbles.enabled = proc.bubbles;
    q.rights.setToggleState (proc.rights, false); q.rights2.setToggleState (proc.rights, false);
    const bool busy = proc.busy();
    const auto whyC = proc.whyNotPreview(), whyS = proc.whyNotSong();
    q.create.setEnabled (! busy && whyC.isEmpty()); q.create.setTooltip (whyC.isEmpty() ? "Sing a short preview with the new voice (" + engineName (proc.engine) + ", on your Mac; nothing is uploaded) and measure it" : whyC);
    q.sing->setEnabled (! busy && whyS.isEmpty()); q.sing->setTooltip (whyS.isEmpty() ? "Sing the whole performance with the new voice" : whyS);
    q.analyze.setEnabled (! busy && proc.engineReady() && ! proc.takes[slotA].isEmpty() && ! proc.takes[slotB].isEmpty());
    q.analyze.setButtonText (proc.analysisAB.isObject() ? juce::String::fromUTF8 ("ANALYZE VOICES  \xe2\x9c\x93") : "ANALYZE VOICES");
    q.save.setEnabled (! busy && proc.previewIsCurrent());
    q.useInSong.setEnabled (proc.previewIsCurrent());
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
    // the library cards
    if (pageIndex == library) resized();
    repaint();
}

void Editor::timerCallback()
{
    auto& q = *parts;
    clock += 1.0f / 30.0f;
    for (auto& m : q.motes) { m.t += m.speed; if (m.t > 1.0f) m.t -= 1.0f; }
    if (pageIndex == sing) { q.board.syncLive(); q.board.repaint(); }
    if (proc.busy()) q.progressValue = proc.progressValue();
    if (pageIndex == lab) { q.cardC.repaint(); q.create.repaint(); }
    if (proc.isPlaying() || proc.recordingSlot() >= 0) { q.cardA.repaint(); q.cardB.repaint(); q.cardC.repaint(); q.cardPerf.repaint(); q.songPerf.repaint(); q.take.repaint(); }
    if (++tick % 6 == 0) repaint (getLocalBounds().removeFromBottom (70));
}

HoodPage* Editor::hoodPage() { return parts->hood.get(); }
ChopPage* Editor::chopPage() { return parts->chop.get(); }
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
    q.creditsBox.setVisible (on); q.creditsClose.setVisible (on);
    if (on) { q.creditsBox.toFront (false); q.creditsClose.toFront (false); q.bubbles.toFront (false); q.creditsBox.setCaretPosition (0); }
    resized();
}

juce::String Editor::creditsText()
{
    return juce::String::fromUTF8 (
        "GHOST VOICE " GHOSTVOICE_VERSION "  -  free and open source, by GhostBlock\n"
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
        "The switches' shape is after Bencho's liquid toggle (MIT, https://bencho.dev/licence); no code from it is used.\n\n"
        "Test singers in our demos: VocalSet (Wilkins, Seetharaman, Wahl & Pardo, ISMIR 2018, CC BY 4.0).\n\n"
        "TERRAIN, the Spirit Board, the ghost art, the themes, the window, MOTION and everything else: GhostBlock, released here\n"
        "under the AGPL-3.0. The full list is in NOTICE.md (in the Licenses folder).");
}

void Editor::showPage (int i)
{
    pageIndex = juce::jlimit (0, kPages - 1, i);
    auto& q = *parts;
    auto vis = [] (std::initializer_list<juce::Component*> cs, bool v) { for (auto* c : cs) c->setVisible (v); };
    vis ({ &q.cardA, &q.cardB, &q.cardC, &q.balance, &q.novelty, &q.varDown, &q.varUp, &q.analyze, &q.create, &q.save, &q.useInSong,
           &q.pitch, &q.viewTabs, &q.source, &q.rights }, pageIndex == lab);
    vis ({ &q.engineSel }, pageIndex == lab || pageIndex == sing);
    if (pageIndex != lab) vis ({ &q.quality, &q.follow, &q.tone, &q.timbre, &q.texture, &q.breath, &q.matchRange, &q.promptFree, &q.gpu, &q.keepCons, &q.instant }, false);
    q.perfRec.setVisible (pageIndex == lab && q.source.selected == 0); q.perfAdd.setVisible (pageIndex == lab && q.source.selected == 1);
    vis ({ &q.libView }, pageIndex == library);
    vis ({ &q.songPerf, &q.board, &q.motion, &q.rate, &q.depth, q.sing.get(), &q.progress, &q.take, &q.rights2 }, pageIndex == sing);
    vis ({ &q.terrainView, &q.terrainAmt }, pageIndex == tools);
    vis ({ &q.install, &q.reveal, &q.theme, &q.cpuOnly, &q.bubbleToggle }, pageIndex == prefs);
    vis ({ q.hood.get() }, pageIndex == hood);
    vis ({ q.chop.get() }, pageIndex == chop);
    for (auto& n : q.nav) n->setVisible (pageIndex != chop);   // CHOP has the whole window under the header
    q.about.setVisible (pageIndex != chop);
    if (pageIndex == chop && q.creditsOn) showCredits (false);
    if (pageIndex == tools) q.terrainView.playOpening();
    if (pageIndex == library) proc.reloadLibrary();
    resized();
    refresh();
    for (auto& t : q.tabs) t->repaint();
    for (auto& n : q.nav) n->repaint();
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
    }
    void paint (juce::Graphics& g) override
    {
        const auto& P = ed.pal(); const auto& v = ed.proc.library[(size_t) index];
        auto r = getLocalBounds().toFloat().reduced (6);
        panel (g, P, r, 14, v.name == ed.proc.voiceName ? col::voiceC (P) : juce::Colour());
        avatar (g, { r.getX() + 40, r.getY() + 42 }, 48.0f, 4 + index % 3, col::voiceC (P).withRotatedHue (0.08f * (float) (index % 5)));
        g.setColour (P.ink); g.setFont (gfont (16.0f)); g.drawText (v.name, juce::Rectangle<float> (r.getX() + 78, r.getY() + 14, r.getWidth() - 90, 22), juce::Justification::centredLeft, true);
        g.setColour (P.dim); g.setFont (gfont (11.5f, false));
        g.drawText (v.created.formatted ("%d %b %Y  %H:%M") + "  -  " + engineName (v.engine) + "  -  " + juce::String (juce::roundToInt (v.balance * 100)) + " % B"
                    + (v.engine == Engine::seedvc || v.engine == Engine::best ? "  -  novelty " + juce::String (juce::roundToInt (v.settings.novelty * 100)) + " %" : juce::String()),
                    juce::Rectangle<float> (r.getX() + 78, r.getY() + 36, r.getWidth() - 90, 18), juce::Justification::centredLeft, true);
        juce::String m = v.identity.valid ? "measured distance  vs A " + juce::String (v.identity.distA(), 2) + "   vs B " + juce::String (v.identity.distB(), 2) : "not measured";
        const bool vu = v.identity.unique() && v.engine != Engine::classic;   // (see the badge: CLASSIC always measures far)
        if (vu) m << "   -   UNIQUE";
        g.setColour (vu ? P.good : P.dim); g.drawText (m, juce::Rectangle<float> (r.getX() + 78, r.getY() + 54, r.getWidth() - 90, 18), juce::Justification::centredLeft, true);
    }
    void resized() override { auto r = getLocalBounds().reduced (16, 12).removeFromBottom (28); use.setBounds (r.removeFromLeft (80)); r.removeFromLeft (8); playB.setBounds (r.removeFromLeft (80)); del.setBounds (r.removeFromRight (80)); }
    Editor& ed; int index; juce::TextButton use, playB, del;
};
}

// ------------------------------------------------------------------------------------------------ paint
void Editor::paint (juce::Graphics& g)
{
    const auto& P = pal();
    splash::backdrop (g, getLocalBounds());   // build 9: Brandon's painting, the washes kept to the edges
    {   // the logo: GHOST VOICE written in thousands of little hairs, every one a colour of the palette
        splash::hairWord (g, "GHOST VOICE", juce::Rectangle<float> (14, 6, 300, 42));
        g.setColour (P.dim); g.setFont (gfont (10.5f, false)); g.drawText ("OPEN-SOURCE VOICE CREATION SUITE", juce::Rectangle<float> (18, 49, 296, 16), juce::Justification::centredLeft);
    }
    { juce::Path rule; rule.startNewSubPath (0, 72.5f); rule.lineTo ((float) getWidth(), 72.5f); splash::sketch (g, rule, 72, 4, 0.7f, 0.5f); }
    if (pageIndex == chop) return;   // CHOP paints the rest itself (it has the whole window under the header)
    // left list footer + bottom strip
    g.setColour (P.dim); g.setFont (gfont (11.5f, false));
    g.drawText (juce::String ("v") + GHOSTVOICE_VERSION + "  FREE  -  AGPL-3.0", juce::Rectangle<float> (22, (float) getHeight() - 104, 200, 18), juce::Justification::centredLeft);
    g.setColour (col::edge (P)); g.fillRect (210, 73, 1, getHeight() - 73 - 72);
    auto strip = getLocalBounds().removeFromBottom (72).toFloat();
    g.setColour (P.panel.withAlpha (0.92f)); g.fillRect (strip); g.setColour (col::edge (P)); g.fillRect (strip.removeFromTop (1));
    {
        auto s = strip.reduced (18, 10);
        icon (g, 1, s.removeFromLeft (30), P.ink);
        g.setColour (P.ink); g.setFont (gfont (16.0f)); g.drawText ("Voice Library (" + juce::String ((int) proc.library.size()) + ")", s.removeFromLeft (170), juce::Justification::centredLeft);
        // the chips: A, B, the current C, the last two saved voices
        auto chip = [&] (const juce::String& t, const juce::String& sub, juce::Colour c, int look, bool hi)
        {
            auto cr = s.removeFromLeft (196).reduced (6, 0); s.removeFromLeft (4);
            panel (g, P, cr, 10, hi ? c : juce::Colour());
            avatar (g, { cr.getX() + 24, cr.getCentreY() }, 26.0f, look, c);
            g.setColour (P.ink); g.setFont (gfont (13.5f)); g.drawText (t, cr.withTrimmedLeft (46).withTrimmedBottom (cr.getHeight() * 0.48f), juce::Justification::bottomLeft, true);
            g.setColour (P.dim); g.setFont (gfont (11.0f, false)); g.drawText (sub, cr.withTrimmedLeft (46).withTrimmedTop (cr.getHeight() * 0.52f), juce::Justification::topLeft, true);
        };
        chip ("Voice A", proc.takes[slotA].isEmpty() ? "empty" : "Reference", col::voiceA (P), 0, false);
        chip ("Voice B", proc.takes[slotB].isEmpty() ? "empty" : "Reference", col::voiceB (P), 3, false);
        chip (proc.voiceName, proc.previewIsCurrent() ? "New Identity" : "not made yet", col::voiceC (P), 4, proc.previewIsCurrent());
        // the status pill (real state)
        auto pill = strip.reduced (18, 14).removeFromRight (juce::jmin (420.0f, strip.getWidth() - 840.0f));
        if (pill.getWidth() > 120)
        {
            const bool bad = proc.runner.state() == Runner::State::failed && ! proc.runner.busy();
            const bool ready = ! proc.runner.busy() && ! bad && proc.previewIsCurrent();
            const auto c = bad ? P.bad : ready ? P.good : P.violet;
            { const auto& hb = splash::handBox (pill, 10, 925); g.setColour (P.card); g.fillPath (hb); g.setColour (c.withAlpha (0.14f)); g.fillPath (hb); splash::inkLine (g, hb, 926, c, 1.2f, 0.7f); }
            g.setColour (P.ink); g.setFont (gfont (12.5f, false)); g.drawFittedText (statusText(), pill.reduced (12, 2).toNearestInt(), juce::Justification::centredLeft, 2);
            if (proc.runner.busy()) { g.setColour (c); g.fillRoundedRectangle (pill.withTop (pill.getBottom() - 3).withWidth (pill.getWidth() * proc.runner.progress()), 1.5f); }
        }
    }
    auto content = juce::Rectangle<float> (226, 84, (float) getWidth() - 244, (float) getHeight() - 84 - 84);
    const bool picker = pageIndex == lab || pageIndex == sing;
    auto heading = [&] (const juce::String& a, const juce::String& b, const juce::String& sub)
    {
        auto hc = picker ? content.withTrimmedRight ((float) parts->engineSel.getWidth() + 16.0f) : content;
        g.setColour (P.ink); g.setFont (gfont (28.0f)); const float w = ghosty::textWidth (gfont (28.0f), a);
        g.drawText (a, hc.withHeight (36), juce::Justification::centredLeft);
        g.setColour (col::voiceC (P)); g.drawText (b, hc.withHeight (36).withTrimmedLeft (w + 8), juce::Justification::centredLeft, true);
        g.setColour (P.dim); g.setFont (gfont (14.0f, false)); g.drawText (sub, hc.withTrimmedTop (36).withHeight (20), juce::Justification::centredLeft, true);
        if (picker)
        {   // the ENGINE picker's label + what the engine in use does
            const auto er = parts->engineSel.getBounds().toFloat();
            g.setColour (P.dim); g.setFont (gfont (10.5f));
            g.drawText ("ENGINE", er.translated (0, -14).withHeight (12), juce::Justification::centredLeft);
            g.setColour (P.ink); g.setFont (gfont (11.5f, false));
            g.drawText (engineBlurb (proc.engine), er.translated (0, er.getHeight() + 3).withHeight (16), juce::Justification::centredRight, true);
        }
    };
    auto& q = *parts;
    if (pageIndex == lab)
    {
        heading ("Identity Lab -", "Create a New Voice", "Fuse two vocal identities into a new singing voice. Every number here is measured.");
        // the arrow between B and C
        const auto ar = juce::Rectangle<float> ((float) q.cardB.getRight(), (float) q.cardB.getY(), (float) (q.cardC.getX() - q.cardB.getRight()), (float) q.cardB.getHeight());
        g.setColour (col::voiceC (P)); juce::Path chev;
        for (int k = 0; k < 2; ++k) { const float x0 = ar.getCentreX() - 10 + (float) k * 10; chev.startNewSubPath (x0, ar.getCentreY() - 10); chev.lineTo (x0 + 9, ar.getCentreY()); chev.lineTo (x0, ar.getCentreY() + 10); }
        g.strokePath (chev, juce::PathStrokeType (2.6f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
        // the three panels
        for (auto& pr : panels) panel (g, P, pr.r, 14);
        auto title = [&] (juce::Rectangle<float> r, int ic, const juce::String& t) { icon (g, ic, r.removeFromLeft (30).withHeight (30).translated (12, 10), P.ink); g.setColour (P.ink); g.setFont (gfont (17.0f)); g.drawText (t, juce::Rectangle<float> (r.getX() + 16, r.getY() + 10, r.getWidth() - 30, 30), juce::Justification::centredLeft); };
        if (panels.size() == 3)
        {
            title (panels[0].r, 6, "Identity Fusion"); title (panels[1].r, 5, "Voice C Preview & Analysis"); title (panels[2].r, 2, "Singing Engine");
            // fusion: labels
            g.setFont (gfont (13.5f)); g.setColour (P.ink);
            auto lab1 = q.balance.getBounds().toFloat();
            g.drawText ("Voice Balance", lab1.translated (0, -28).withHeight (18), juce::Justification::centredLeft);
            const int pb = juce::roundToInt (proc.blendB() * 100.0f);
            g.setFont (gfont (13.0f, false));
            g.drawText ("A " + juce::String (100 - pb) + "%", lab1.withX (lab1.getX() - 62).withWidth (58), juce::Justification::centredRight);
            g.drawText ("B " + juce::String (pb) + "%", lab1.withX (lab1.getRight() + 6).withWidth (60), juce::Justification::centredLeft);
            auto lab2 = q.novelty.getBounds().toFloat();
            g.setFont (gfont (13.5f)); g.drawText ("Novelty (Uniqueness)", lab2.translated (0, -26).withHeight (18), juce::Justification::centredLeft);
            g.setFont (gfont (13.0f, false)); g.drawText (juce::String (juce::roundToInt (proc.settings.novelty * 100)) + "%", lab2.withX (lab2.getRight() + 6).withWidth (60), juce::Justification::centredLeft);
            g.setColour (P.dim); g.setFont (gfont (11.5f, false));
            if (proc.noveltyWorks())
                g.drawText ("variation " + juce::String (proc.settings.variation), q.varDown.getBounds().toFloat().withRight ((float) q.varUp.getRight()).translated (0, 24).withHeight (14), juce::Justification::centred);
            else
                g.drawFittedText ("Greyed out: " + engineName (proc.engine) + " has no voice space to turn in. NOVELTY needs NEURAL or BEST OF ALL.",
                                  juce::Rectangle<int> (q.novelty.getX(), q.novelty.getBottom() + 2, q.varUp.getRight() - q.novelty.getX(), 28), juce::Justification::topLeft, 2);
            g.setColour (P.ink); g.setFont (gfont (13.5f));
            g.drawText (isDsp (proc.engine) ? engineName (proc.engine) + " controls" : proc.engine == Engine::best ? "NEURAL controls (NATURAL keeps its own)" : "NEURAL controls",
                        juce::Rectangle<float> ((float) knobRow.getX(), (float) knobRow.getY() - 22, 320, 18), juce::Justification::centredLeft);
            // analysis: the view
            auto view = viewArea;
            { const auto& hb = splash::handBox (view, 10, splash::seedOf (view)); g.setColour (P.waveBg); g.fillPath (hb); splash::inkLine (g, hb, splash::seedOf (view) + 1, P.ink.withAlpha (0.55f), 0.9f, 0.6f); }
            q.anA.set (proc.takes[slotA].isEmpty() ? juce::File() : juce::File (proc.takes[slotA][0]));
            q.anB.set (proc.takes[slotB].isEmpty() ? juce::File() : juce::File (proc.takes[slotB][0]));
            q.anC.set (proc.preview);
            auto inner = view.reduced (12, 10);
            if (q.viewTabs.selected == 0)
            {
                if (q.anC.peaks.empty()) { g.setColour (P.dim); g.setFont (gfont (12.0f, false)); g.drawText ("voice C's preview appears here", inner, juce::Justification::centred); }
                else q.anC.paintWave (g, inner, col::voiceC (P), proc.isPlaying (proc.preview) ? proc.playPosition() : -1.0f);
            }
            else
            {
                const bool cmp = q.viewTabs.selected == 2;
                if (cmp) { q.anA.paintSpectrum (g, inner, col::voiceA (P), false); q.anB.paintSpectrum (g, inner, col::voiceB (P), false); }
                q.anC.paintSpectrum (g, inner, col::voiceC (P), ! cmp);
                g.setColour (P.dim); g.setFont (gfont (10.0f, false));
                for (float hz : { 100.0f, 1000.0f, 10000.0f })
                {
                    const float x = inner.getX() + inner.getWidth() * std::log (hz / 60.0f) / std::log (12000.0f / 60.0f);
                    g.drawText (hz >= 1000 ? juce::String ((int) (hz / 1000)) + " kHz" : juce::String ((int) hz) + " Hz", juce::Rectangle<float> (x - 30, inner.getBottom() - 12, 60, 12), juce::Justification::centred);
                }
                if (cmp) { g.setFont (gfont (11.0f)); g.setColour (col::voiceA (P)); g.drawText ("A", inner.removeFromTop (14), juce::Justification::topLeft); g.setColour (col::voiceB (P)); g.drawText ("B", inner.translated (14, -14).removeFromTop (14), juce::Justification::topLeft); g.setColour (col::voiceC (P)); g.drawText ("C", inner.translated (28, -14).removeFromTop (14), juce::Justification::topLeft); }
                if (q.anC.spectrumDb.empty() && ! cmp) { g.setColour (P.dim); g.setFont (gfont (12.0f, false)); g.drawText ("no voice C yet", view, juce::Justification::centred); }
            }
            // identity distance (measured by the voice-print meter) + the badge
            auto idr = idArea;
            g.setColour (P.ink); g.setFont (gfont (13.5f));
            g.drawText ("Identity Distance", idr.removeFromTop (20), juce::Justification::centredLeft);
            const auto& id = proc.previewId;
            auto bar = [&] (const juce::String& n, float d, juce::Colour c)
            {
                auto row = idr.removeFromTop (24); g.setColour (P.ink); g.setFont (gfont (12.5f, false));
                g.drawText (n, row.removeFromLeft (52), juce::Justification::centredLeft);
                auto val = row.removeFromRight (40);
                auto tr = row.reduced (4, 8);
                g.setColour (P.cell); g.fillRoundedRectangle (tr, 4);
                if (id.valid) { juce::ColourGradient gr (P.violet, tr.getX(), 0, c, tr.getRight(), 0, false); g.setGradientFill (gr); g.fillRoundedRectangle (tr.withWidth (tr.getWidth() * juce::jlimit (0.0f, 1.0f, d)), 4); }
                g.setColour (P.ink); g.drawText (id.valid ? juce::String (d, 2) : "-", val, juce::Justification::centredRight);
            };
            bar ("vs. A", id.distA(), col::voiceA (P)); bar ("vs. B", id.distB(), col::voiceB (P));
            g.setColour (P.dim); g.setFont (gfont (10.5f, false));
            juce::String foot = id.valid ? "Voice prints, 1 - cosine.  A to B: " + juce::String (1.0f - id.cosAB, 2) + ".  To the guide's singer: " + juce::String (1.0f - id.cosPerf, 2)
                                         : proc.analysisAB.isObject() ? "A to B measured: " + juce::String (1.0 - (double) proc.analysisAB["cos_a_b"], 2) + ".  Create voice C to measure it." : "Measured after CREATE NEW VOICE C (voice prints, 1 - cosine).";
            g.drawFittedText (foot, idr.removeFromTop (28).toNearestInt(), juce::Justification::topLeft, 2);
            // the badge, only when the measurement passes
            auto badge = badgeArea;
            if (id.valid)
            {
                // CLASSIC measures 0.34-0.41 from A and B even at 0 % / 100 % B on real singers (CI e2e, build 2): its re-synthesis
                // moves the voice print away from everyone, so passing the rule there says nothing about a new identity
                const bool classicFar = proc.previewEngine == Engine::classic && id.unique();
                const bool u = id.unique() && ! classicFar; const auto c = u ? P.good : P.warn;
                { const auto& hb = splash::handBox (badge, 10, 1037); g.setColour (c.withAlpha (0.12f)); g.fillPath (hb); splash::inkLine (g, hb, 1038, c, 1.2f, 0.7f); }
                g.setColour (c); g.setFont (gfont (14.0f)); g.drawText (u ? "Unique Identity" : classicFar ? "Not counted" : "Not unique yet", badge.reduced (12, 8).removeFromTop (20), juce::Justification::centredLeft);
                g.setColour (P.ink); g.setFont (gfont (11.0f, false));
                g.drawFittedText (u ? "Measured: voice C is at least 0.30 from A and from B (past where a render of A or B itself lands) and it is not the guide's singer."
                                    : classicFar ? "Measured far from A and B, but CLASSIC measures that far even at 0 % B (its re-synthesis changes the voice print), so it is not counted as a new identity."
                                    : "Measured: voice C is still close to " + juce::String (id.cosA > id.cosB ? "A" : "B") + juce::String (id.cosPerf > juce::jmax (id.cosA, id.cosB) ? " (or to the guide's singer)" : "") + ". Try more NOVELTY or a middle BALANCE.",
                                  badge.reduced (12, 8).withTrimmedTop (22).toNearestInt(), juce::Justification::topLeft, 4);
            }
            // singing engine: labels
            g.setColour (P.ink); g.setFont (gfont (13.5f));
            g.drawText ("Performance Source", juce::Rectangle<float> ((float) q.source.getX(), (float) q.source.getY() - 22, 200, 18), juce::Justification::centredLeft);
            g.setColour (P.dim); g.setFont (gfont (11.5f, false));
            const auto& pt = proc.takes[slotPerf];
            g.drawText (pt.isEmpty() ? "none: the preview sings voice A's first take" : juce::File (pt[0]).getFileName() + "  -  " + juce::String (proc.takeSeconds (slotPerf), 1) + " s",
                        juce::Rectangle<float> ((float) q.source.getX(), (float) q.perfRec.getBottom() + 4, (float) q.source.getWidth(), 16), juce::Justification::centredLeft, true);
        }
    }
    else if (pageIndex == library)
    {
        heading ("Voice Library -", "Saved Voices", "Every voice C you saved: its recipe, its takes and what was measured on it.");
        if (proc.library.empty()) { g.setColour (P.dim); g.setFont (gfont (14.0f, false)); g.drawText ("No saved voices yet. Make one in the IDENTITY LAB and press SAVE VOICE C.", content.withTrimmedTop (80), juce::Justification::centredTop); }
    }
    else if (pageIndex == sing)
    {
        heading ("Sing Engine -", "Generate Singing", "Your performance, sung by " + proc.voiceName + " (" + engineName (proc.engine) + "). MOTION moves it between A and B with the song.");
        for (auto& pr : panels) panel (g, P, pr.r, 14);
        g.setColour (P.ink); g.setFont (gfont (13.5f));
        g.drawText ("YOUR NEW TAKE", juce::Rectangle<float> ((float) q.take.getX(), (float) q.take.getY() - 20, 200, 16), juce::Justification::centredLeft);
        g.setColour (P.dim); g.setFont (gfont (12.0f, false));
        g.drawFittedText (uiWords (proc.resultInfo), juce::Rectangle<int> (q.take.getX(), q.take.getBottom() + 6, q.take.getWidth(), 34), juce::Justification::topLeft, 2);
        const int pb = juce::roundToInt (proc.blendB() * 100.0f);
        g.drawText (juce::String (100 - pb) + " % A  -  " + juce::String (pb) + " % B" + (proc.motionOn() ? "   -   MOTION: " + juce::String ((int) proc.makeSongJob().mixes.size()) + " blends are sung and crossfaded" : ""),
                    juce::Rectangle<float> ((float) q.board.getX(), (float) q.board.getBottom() + 4, (float) q.board.getWidth(), 18), juce::Justification::centredLeft);
    }
    else if (pageIndex == tools)
    {
        heading ("Tools -", "TERRAIN", "The land moves " + engineName (proc.engine) + "'s own controls, never a sound of ours. Each move is heard on the next render.");
        g.setColour (P.dim); g.setFont (gfont (12.0f, false));
        const juce::String seedMap = "Tallest mountain = FOLLOW.  More land = QUALITY.  Jagged land = SINGER DETAIL (prompt on).";
        const juce::String dspMap = "Tallest mountain = RESONANCE.  More land = WARMTH.  Jagged land = GRIT.";
        g.drawFittedText ((isDsp (proc.engine) ? dspMap : proc.engine == Engine::best ? "NEURAL parts: " + seedMap + "  NATURAL parts: " + dspMap : seedMap)
                          + "  Lean left / right = towards A / B.  Valleys = PITCH down, spires = PITCH up.  Movement = MOTION.",
                          juce::Rectangle<int> (q.terrainAmt.getRight() + 20, q.terrainAmt.getY() - 6, (int) content.getRight() - q.terrainAmt.getRight() - 20, 40), juce::Justification::topLeft, 3);
        g.setColour (P.ink); g.setFont (gfont (12.5f)); g.drawText ("TERRAIN AMOUNT", q.terrainAmt.getBounds().toFloat().translated (0, -16).withHeight (14), juce::Justification::centredLeft);
    }
    else if (pageIndex == hood)
    {
        heading ("Under the Hood -", "Original Controls", "Every option of the neural engine's singing conversion, under its own name, default and range. Each one goes straight into the job. NATURAL and CLASSIC are ours, so they are not here.");
        g.setColour (P.dim); g.setFont (gfont (12.0f, false));
        int live = 0, given = 0, unused = 0;
        for (auto& o : gv::hood::options()) { if (o.kind == gv::hood::Kind::given || o.kind == gv::hood::Kind::modelConfig) ++given; else if (o.kind == gv::hood::Kind::notUsed) ++unused; else ++live; }
        g.drawText (juce::String ((int) gv::hood::options().size()) + " original options listed: " + juce::String (live) + " live controls, " + juce::String (given) + " set for you by the cards and the checkpoint, " + juce::String (unused) + " not used on a Mac",
                    content.withTrimmedTop (62).withHeight (18), juce::Justification::centredLeft, true);
    }
    else if (pageIndex == prefs)
    {
        heading ("Settings -", "Engine & Look", "Ghost Voice is free and open source. The voice engine runs on your Mac; nothing is uploaded.");
        auto r = content.withTrimmedTop (70);
        g.setColour (P.ink); g.setFont (gfont (16.0f)); g.drawText ("NEURAL voice engine: " + engineSummary() + "     NATURAL + CLASSIC: built in, nothing to download", r.removeFromTop (24), juce::Justification::centredLeft, true);
        g.setColour (P.dim); g.setFont (gfont (12.5f, false));
        g.drawFittedText ("The neural engine is open-source software that runs as a separate helper on your Mac. About 2.5 GB, downloaded once into:\n" + engineDir().getFullPathName()
                          + "\nNATURAL and CLASSIC are built in. With the voice engine installed, their takes are measured with the same voice-print meter. Who made each part: ABOUT / CREDITS.",
                          r.removeFromTop (64).toNearestInt(), juce::Justification::topLeft, 5);
        r = juce::Rectangle<float> (r.getX(), (float) q.reveal.getBottom() + 12, r.getWidth(), 400);
        auto msg = r.removeFromTop (20);   // the installer's progress line (its row is always kept, so nothing moves)
        if (proc.task() == Processor::Task::install || (proc.runner.state() == Runner::State::failed && proc.runner.log().contains ("GV_STEP")))
        { g.setColour (P.ink); g.drawFittedText (uiWords (proc.runner.message()) + "  " + juce::String (juce::roundToInt (proc.runner.progress() * 100)) + " %", msg.toNearestInt(), juce::Justification::topLeft, 1); }
        r.removeFromTop (20);
        g.setColour (P.ink); g.setFont (gfont (14.0f)); g.drawText ("Look", r.removeFromTop (20), juce::Justification::centredLeft);
        r = r.withTop ((float) q.theme.getBottom() + 24);
        g.setColour (P.ink); g.setFont (gfont (14.0f)); g.drawText ("Voices and consent", r.removeFromTop (20), juce::Justification::centredLeft);
        g.setColour (P.dim); g.setFont (gfont (13.0f, false));
        g.drawFittedText ("Use only your own voice, or voices you have the singer's permission to use. Ghost Voice ships no voices and no famous-voice presets, and it will not render until you tick "
                          "\"I have the rights to these voices\". The test singers in our demos are credited in ABOUT / CREDITS.",
                          r.removeFromTop (60).toNearestInt(), juce::Justification::topLeft, 4, 1.0f);
    }
}

// ------------------------------------------------------------------------------------------------ layout
// the knobs and switches of the engine in use (IDENTITY LAB): SEED-VC's, NATURAL / CLASSIC's, or for BEST OF ALL SEED-VC's
void Editor::layoutEngineControls()
{
    auto& q = *parts;
    const bool lab_ = pageIndex == lab, dsp = isDsp (proc.engine), seed = ! dsp;
    std::vector<Knob*> ks;
    if (seed) ks = { &q.quality, &q.follow, &q.pitch };
    else ks = { &q.tone, &q.timbre, &q.texture, &q.breath, &q.pitch };
    for (auto* k : { &q.quality, &q.follow, &q.tone, &q.timbre, &q.texture, &q.breath }) k->setVisible (false);
    auto kr = knobRow;
    // as wide as the row allows, and never taller than the row (the knob's name and value sit under it)
    const int kw = juce::jmin (90, kr.getWidth() / juce::jmax (1, (int) ks.size()), (int) ((float) (kr.getHeight() - 36) / 0.82f));
    for (auto* k : ks) { k->setBounds (kr.removeFromLeft (kw).withHeight ((int) (kw * 0.82f) + 36)); k->setVisible (lab_); }
    q.pitch.s.setTooltip (seed ? "PITCH: the neural engine's semitone shift of the performance" : "PITCH: the engine's own transpose (the formants stay where they are)");
    std::vector<LiquidToggle*> ts;
    if (seed) ts = { &q.matchRange, &q.promptFree, &q.gpu, &q.instant };
    else ts = { &q.keepCons };
    for (auto* t : { &q.matchRange, &q.promptFree, &q.gpu, &q.instant, &q.keepCons }) t->setVisible (false);
    auto r = toggleArea;
    const int th = juce::jlimit (22, 28, (r.getHeight() + 2) / juce::jmax (1, (int) ts.size()) - 2);   // they always fit the panel
    for (auto* t : ts) { t->setBounds (r.removeFromTop (th)); r.removeFromTop (2); t->setVisible (lab_); }
}

void Editor::resized()
{
    auto& q = *parts;
    q.bubbles.setBounds (getLocalBounds()); q.bubbles.toFront (false);
    panels.clear();
    auto all = getLocalBounds();
    auto header = all.removeFromTop (72);
    {
        auto t = header.reduced (12, 10).withTrimmedLeft (330);
        const int w = juce::jmin (210, t.getWidth() / kPages);
        for (auto& b : q.tabs) { b->setBounds (t.removeFromLeft (w).reduced (4, 0)); }
    }
    all.removeFromBottom (72);
    auto left = all.removeFromLeft (210).reduced (0, 12);
    q.about.setBounds (left.getX() + 16, getHeight() - 72 - 70, 170, 28);   // just above the version line
    for (auto& n : q.nav) n->setBounds (left.removeFromTop (n->sub.isNotEmpty() ? 62 : 52));
    auto content = all.reduced (16, 12).withTrimmedTop (64);
    if (q.creditsOn)
    {   // ABOUT / CREDITS sits over the page (the page underneath keeps its own layout)
        auto cr = all.reduced (16, 12);
        q.creditsClose.setBounds (cr.removeFromBottom (34).removeFromRight (120)); cr.removeFromBottom (8);
        q.creditsBox.setBounds (cr);
    }
    {   // the ENGINE picker: top right of the IDENTITY LAB and the SING ENGINE
        auto top = all.reduced (16, 12);
        const int w = juce::jlimit (300, 460, top.getWidth() / 2 - 40);
        q.engineSel.setBounds (top.getRight() - w, top.getY() + 16, w, 28);
    }
    if (pageIndex == lab)
    {
        // row 1: A, B, (arrow), C
        auto row = content.removeFromTop (150);
        const int cw = (row.getWidth() - 56) / 3;
        q.cardA.setBounds (row.removeFromLeft (cw)); q.cardB.setBounds (row.removeFromLeft (cw)); row.removeFromLeft (56); q.cardC.setBounds (row);
        content.removeFromTop (8);
        // row 3 (bottom): analyze, CREATE, save, use in song
        auto bottom = content.removeFromBottom (78);
        content.removeFromBottom (8);
        // row 2: the three panels
        const int pw = content.getWidth();
        auto p1 = content.removeFromLeft ((int) (pw * 0.33f)).reduced (4, 0); auto p2 = content.removeFromLeft ((int) (pw * 0.36f)).reduced (4, 0); auto p3 = content.reduced (4, 0);
        panels.push_back ({ p1.toFloat() }); panels.push_back ({ p2.toFloat() }); panels.push_back ({ p3.toFloat() });
        {
            auto r = p1.reduced (20, 0).withTrimmedTop (66);
            q.balance.setBounds (r.removeFromTop (26).withTrimmedLeft (60).withTrimmedRight (64)); r.removeFromTop (32);
            auto nr = r.removeFromTop (26); q.novelty.setBounds (nr.withTrimmedRight (64).withTrimmedRight (70));
            auto vr = juce::Rectangle<int> (q.novelty.getRight() + 52, nr.getY(), 60, 24).withRight (p1.getRight() - 16).withLeft (p1.getRight() - 16 - 60);
            q.varDown.setBounds (vr.removeFromLeft (28)); vr.removeFromLeft (4); q.varUp.setBounds (vr.removeFromLeft (28));
            q.novelty.setBounds (q.novelty.getBounds().withRight (q.varDown.getX() - 52));
            r.removeFromTop (52);   // room for the greyed-out reason under NOVELTY and the knobs' title
            knobRow = r.removeFromTop (juce::jmin (112, r.getHeight()));
        }
        {
            auto r = p2.reduced (16, 0).withTrimmedTop (50);
            q.viewTabs.setBounds (r.removeFromTop (30)); r.removeFromTop (8);
            viewArea = r.removeFromTop (juce::jmax (60, r.getHeight() - 112)).toFloat();
            r.removeFromTop (8);
            auto lower = r.withTrimmedBottom (10);
            badgeArea = lower.removeFromRight (juce::jmin (180, lower.getWidth() / 2 - 10)).toFloat();
            lower.removeFromRight (10);
            idArea = lower.toFloat();
        }
        {
            auto r = p3.reduced (16, 0).withTrimmedTop (66);
            q.source.setBounds (r.removeFromTop (30)); r.removeFromTop (6);
            auto b = r.removeFromTop (28); q.perfRec.setBounds (b); q.perfAdd.setBounds (b);
            r.removeFromTop (20);
            q.cardPerf.setBounds (0, 0, 0, 0);
            q.rights.setBounds (r.removeFromBottom (30).translated (0, -6));
            toggleArea = r.withTrimmedBottom (8);
        }
        layoutEngineControls();
        {
            auto b = bottom;
            const int bw = b.getWidth();
            q.analyze.setBounds (b.removeFromLeft ((int) (bw * 0.2f)).reduced (10, 14));
            auto right = b.removeFromRight ((int) (bw * 0.36f));
            q.save.setBounds (right.removeFromLeft (right.getWidth() / 2).reduced (10, 14)); q.useInSong.setBounds (right.reduced (10, 14));
            q.create.setBounds (b);
        }
    }
    else if (pageIndex == library)
    {
        q.libView.setBounds (content);
        q.libCards.clear();
        const int cols = juce::jmax (1, content.getWidth() / 380), cw = content.getWidth() / cols - 4, ch = 150;
        for (int i = 0; i < (int) proc.library.size(); ++i)
        {
            auto c = std::make_unique<LibCard> (*this, i);
            c->setBounds ((i % cols) * (cw + 4), (i / cols) * (ch + 6), cw, ch);
            q.libContent.addAndMakeVisible (*c); q.libCards.push_back (std::move (c));
        }
        q.libContent.setSize (content.getWidth() - 10, ((int) proc.library.size() + cols - 1) / cols * (ch + 6));
    }
    else if (pageIndex == sing)
    {
        auto left2 = content.removeFromLeft (300);
        q.songPerf.setBounds (left2.removeFromTop (150)); left2.removeFromTop (12);
        q.rights2.setBounds (left2.removeFromTop (30).reduced (8, 0));
        content.removeFromLeft (12);
        auto right2 = content.removeFromRight (330);
        panels.push_back ({ right2.toFloat() });
        {
            auto r = right2.reduced (18, 18);
            q.sing->setBounds (r.removeFromTop (88)); r.removeFromTop (8);
            q.progress.setBounds (r.removeFromTop (10)); r.removeFromTop (6);
            q.stop.setBounds (r.removeFromTop (30).removeFromRight (90)); r.removeFromTop (30);
            q.take.setBounds (r.removeFromTop (120));
        }
        content.removeFromRight (12);
        auto m = content.removeFromBottom (34);
        const int mw = m.getWidth();
        q.motion.setBounds (m.removeFromLeft (juce::jmin (140, mw * 2 / 5))); m.removeFromLeft (8);
        q.rate.setBounds (m.removeFromLeft (juce::jmin (110, mw * 3 / 10))); m.removeFromLeft (8);
        q.depth.setBounds (m.removeFromLeft (juce::jmin (160, m.getWidth())));
        content.removeFromBottom (28);
        q.board.setBounds (content);
    }
    else if (pageIndex == tools)
    {
        auto bottom = content.removeFromBottom (56);
        q.terrainAmt.setBounds (bottom.removeFromLeft (280).withTrimmedTop (20).withHeight (26));
        content.removeFromBottom (8);
        q.terrainView.setBounds (content);
    }
    else if (pageIndex == hood)
    {
        q.hood->setBounds (content.withTrimmedTop (22));
    }
    else if (pageIndex == prefs)
    {
        auto r = content.withTrimmedTop (100);
        auto b = r.removeFromTop (40);
        q.install.setBounds (b.removeFromLeft (240)); b.removeFromLeft (10); q.reveal.setBounds (b.removeFromLeft (200)); b.removeFromLeft (16);
        q.cpuOnly.setBounds (b.removeFromLeft (320));
        r.removeFromTop (78);   // the installer's line + "Look" (painted)
        auto tr = r.removeFromTop (34); q.theme.setBounds (tr.removeFromLeft (330)); tr.removeFromLeft (24); q.bubbleToggle.setBounds (tr.removeFromLeft (260));
    }
    if (q.chop != nullptr)
    {   // CHOP: the whole window under the header (the instrument needs the room)
        q.chop->setBounds (getLocalBounds().withTrimmedTop (73).withTrimmedBottom (18));   // (the resize corner keeps its own strip)
        if (q.creditsOn) { q.creditsBox.toFront (false); q.creditsClose.toFront (false); }
        q.bubbles.toFront (false);
    }
    repaint();
}
}
