#pragma once
// GHOST CAPTURE page (a tab of the rack panel, after BOARD): [CAPTURE] [bars] [HISTORY], the history status, and the shelf: the last 8
// captures as waveform tiles. Drag a tile into the DAW (a WAV), SAVE AS..., LOOP preview, REVERSE, SLICE (the WAV + a .ghostclip.json
// sidecar for Ghost Slice, both dragged), x = remove. Every way out is gated by the demo export gate (Shared/ExportGate.h): drag
// 'box.capture.drag', save 'box.capture.save', slice 'box.capture.slice'. Themed in all 4 looks (GHOST: glass tiles, indigo ink).
#include "BoxProcessor.h"
#include "FxUi.h"
#include "GhostGlass.h"

class BoxCapturePage : public juce::Component, private juce::Timer
{
public:
    explicit BoxCapturePage (BoxProcessor& p) : proc (p)
    {
        capBtn.setButtonText ("CAPTURE"); capBtn.setColour (juce::TextButton::buttonOnColourId, gpfx::ui::col::pink);
        capBtn.setTooltip ("CAPTURE (Shift+Alt+C): the last bars Ghost Box played (its real output), ending on the latest bar line, onto the shelf");
        capBtn.onClick = [this] { captureClicked(); };
        capBtn.setToggleState (true, juce::dontSendNotification);   // (always lit: the big button of the page)
        bars.addItemList (boxcapture::barNames(), 1); bars.setTooltip ("How many bars CAPTURE takes");
        bars.onChange = [this] { proc.captureBarsIdx = juce::jlimit (0, 3, bars.getSelectedItemIndex()); };
        hist.setClickingTogglesState (false); hist.setColour (juce::TextButton::buttonOnColourId, gpfx::ui::col::ice);
        hist.onClick = [this] { proc.setHistoryOn (! proc.history.isOn()); refresh(); };
        for (auto* c : std::initializer_list<juce::Component*> { &capBtn, &bars, &hist }) addAndMakeVisible (c);
        refresh();
        startTimerHz (15);
    }
    ~BoxCapturePage() override { stopTimer(); }

    void refresh()
    {
        bars.setSelectedItemIndex (proc.captureBarsIdx, juce::dontSendNotification);
        hist.setToggleState (proc.history.isOn(), juce::dontSendNotification);
        hist.setButtonText (proc.history.isOn() ? "HISTORY ON" : "HISTORY OFF");
        hist.setTooltip (proc.history.isOn() ? "Ghost Box keeps the last " + juce::String (proc.history.capacitySeconds(), 0) + " s of its output in memory (" + juce::String ((double) proc.history.bytes() / 1048576.0, 1) + " MB). Click: off (frees it)"
                                             : "HISTORY is off: nothing is kept, CAPTURE has nothing to take. Click: on");
        rebuildTiles();
    }
    // the tiles (tests drive them through these, the mouse calls the same)
    int tileCount() const { return tiles.size(); }
    void tileDragOut (int k) { if (auto c = clipAt (k)) say (proc.clipDrag (c->id, tiles[k]) ? "" : juce::String()); }
    void tileSaveAs (int k) { if (auto c = clipAt (k)) proc.clipSaveAs (c->id, tiles[k]); }
    void tileSlice (int k) { if (auto c = clipAt (k)) proc.clipSendToSlice (c->id, tiles[k]); }
    void tileLoop (int k) { if (auto c = clipAt (k)) { proc.clipPreview (c->id, proc.previewingId() != c->id); repaintTiles(); } }
    void tileReverse (int k) { if (auto c = clipAt (k)) { proc.clipReverse (c->id); repaintTiles(); } }
    void tileRemove (int k) { if (auto c = clipAt (k)) { proc.clipRemove (c->id); } }
    juce::Component* tile (int k) { return juce::isPositiveAndBelow (k, tiles.size()) ? tiles[k] : nullptr; }
    struct Text { juce::Rectangle<int> r; juce::Colour c; juce::String what; };
    std::vector<Text> texts();
    std::function<void (const juce::String&)> onMessage;

    void paint (juce::Graphics& g) override
    {
        using namespace gpfx::ui;
        const auto status = statusText();
        drawCaption (g, statusR.toFloat(), status, ghost() ? ghosty::glass::ink2() : col::dim.get(), 10.5f, juce::Justification::centredRight);
        if (tiles.isEmpty())
            drawCaption (g, shelfR.toFloat(), "Play something, then hit CAPTURE: the last " + juce::String (boxcapture::barsOf (proc.captureBarsIdx)) + " bars of what Ghost Box played land here. Drag a tile into your DAW.",
                         ghost() ? ghosty::glass::ink2() : col::dim.get(), 12.0f, juce::Justification::centred);
        if (message.isNotEmpty() && juce::Time::getMillisecondCounterHiRes() < messageUntil)
            drawCaption (g, msgR.toFloat(), message, ghost() ? ghosty::glass::violetDeep() : col::slime.get(), 10.5f);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (6, 4);
        auto top = r.removeFromTop (30);
        capBtn.setBounds (top.removeFromLeft (120)); top.removeFromLeft (8);
        bars.setBounds (top.removeFromLeft (96).reduced (0, 2)); top.removeFromLeft (8);
        hist.setBounds (top.removeFromLeft (104)); top.removeFromLeft (12);
        msgR = top.removeFromLeft (top.getWidth() / 2); statusR = top;
        r.removeFromTop (8);
        shelfR = r;
        layoutTiles();
    }

private:
    struct Tile : juce::Component, juce::SettableTooltipClient
    {
        Tile (BoxCapturePage& o, std::shared_ptr<boxcapture::Clip> c) : page (o), clip (std::move (c))
        {
            const char* names[] { "LOOP", "REVERSE", "SAVE AS", "SLICE", "x" };
            const char* tips[] { "LOOP: hear this capture looping (through Ghost Box's output)", "REVERSE: flip it backwards (the drag, SAVE AS and SLICE use it too)",
                                 "SAVE AS...: save it as a WAV file", "SLICE: write the WAV + a .ghostclip.json (bpm, bars) next to it and drag both (drop on Ghost Slice)", "Remove from the shelf" };
            for (int i = 0; i < 5; ++i)
            {
                auto* b = btns.add (new juce::TextButton (names[i])); b->setTooltip (tips[i]); b->setClickingTogglesState (false);
                b->setColour (juce::TextButton::buttonOnColourId, i == 0 ? gpfx::ui::col::slime : gpfx::ui::col::ecto);
                addAndMakeVisible (b);
            }
            btns[0]->onClick = [this] { page.tileLoop (index()); };
            btns[1]->onClick = [this] { page.tileReverse (index()); };
            btns[2]->onClick = [this] { page.tileSaveAs (index()); };
            btns[3]->onClick = [this] { page.tileSlice (index()); };
            btns[4]->onClick = [this] { page.tileRemove (index()); };
            setTooltip ("Drag me into your DAW: a WAV of this capture");
        }
        BoxCapturePage& page; std::shared_ptr<boxcapture::Clip> clip; juce::OwnedArray<juce::TextButton> btns; bool dragging = false;
        int index() const { return page.tiles.indexOf (const_cast<Tile*> (this)); }
        juce::Rectangle<int> wave() const { return getLocalBounds().reduced (8, 0).withTrimmedTop (22).withTrimmedBottom (32); }
        void resized() override
        {
            auto r = getLocalBounds().reduced (6, 5).removeFromBottom (24);
            btns[4]->setBounds (r.removeFromRight (24)); r.removeFromRight (4);
            // widths from the labels (REVERSE / SAVE AS never become "REVER...")
            float tw[4], sum = 0; for (int i = 0; i < 4; ++i) { tw[i] = ghosty::textWidth (gpfx::ui::font (11.0f), btns[i]->getButtonText()) + 14.0f; sum += tw[i]; }
            const float k = (float) r.getWidth() / juce::jmax (1.0f, sum); int x = r.getX();
            for (int i = 0; i < 4; ++i) { const int w = i == 3 ? r.getRight() - x : juce::roundToInt (tw[i] * k); btns[i]->setBounds (juce::Rectangle<int> (x, r.getY(), w, r.getHeight()).reduced (2, 0)); x += w; }
        }
        void paint (juce::Graphics& g) override;
        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragging || ! wave().contains (e.getMouseDownPosition()) || e.getDistanceFromDragStart() < 6) return;
            dragging = true; page.tileDragOut (index());
        }
        void mouseUp (const juce::MouseEvent&) override { dragging = false; }
    };
    BoxProcessor& proc;
    juce::TextButton capBtn, hist; juce::ComboBox bars;
    juce::OwnedArray<Tile> tiles; int seenVersion = -1, seenPreview = -2;
    juce::Rectangle<int> statusR, shelfR, msgR;
    juce::String message; double messageUntil = 0;

    std::shared_ptr<boxcapture::Clip> clipAt (int k) const { return juce::isPositiveAndBelow (k, tiles.size()) ? tiles[k]->clip : nullptr; }
    void say (const juce::String& m) { if (m.isEmpty()) return; message = m; messageUntil = juce::Time::getMillisecondCounterHiRes() + 2500.0; repaint (msgR); if (onMessage) onMessage (m); }
    void captureClicked() { say (proc.captureNow()); }
    juce::String statusText() const
    {
        if (! proc.history.isOn()) return "HISTORY off";
        const double bpm = proc.lastBpm.load(), sec = boxcapture::barsOf (proc.captureBarsIdx) * 4.0 * 60.0 / juce::jmax (20.0, bpm);
        return "history " + juce::String (proc.history.seconds(), 1) + " s of " + juce::String (proc.history.capacitySeconds(), 0) + " s   |   "
               + juce::String (boxcapture::barsOf (proc.captureBarsIdx)) + " bars = " + juce::String (sec, 1) + " s at " + juce::String (bpm, 1) + " BPM";
    }
    void rebuildTiles()
    {
        tiles.clear();
        for (auto& c : proc.shelf) { auto* t = tiles.add (new Tile (*this, c)); addAndMakeVisible (t); fxtheme::retint (*t); }
        seenVersion = proc.shelfVersion.load();
        layoutTiles(); repaint();
    }
    void layoutTiles()
    {
        if (shelfR.isEmpty()) return;
        const int cols = 4, rows = 2, gap = 8;
        const int w = (shelfR.getWidth() - gap * (cols - 1)) / cols, h = (shelfR.getHeight() - gap * (rows - 1)) / rows;
        for (int k = 0; k < tiles.size(); ++k)
            tiles[k]->setBounds (shelfR.getX() + (k % cols) * (w + gap), shelfR.getY() + (k / cols) * (h + gap), w, h);
    }
    void repaintTiles() { for (auto* t : tiles) t->repaint(); }
    void timerCallback() override
    {
        if (proc.shelfVersion.load() != seenVersion) rebuildTiles();
        if (proc.previewingId() != seenPreview) { seenPreview = proc.previewingId(); for (auto* t : tiles) t->btns[0]->setToggleState (t->clip->id == seenPreview, juce::dontSendNotification); repaintTiles(); }
        repaint (statusR);
        if (message.isNotEmpty() && juce::Time::getMillisecondCounterHiRes() > messageUntil) { message = {}; repaint (msgR); }
    }
};

inline void BoxCapturePage::Tile::paint (juce::Graphics& g)
{
    using namespace gpfx::ui; namespace gg = ghosty::glass;
    auto r = getLocalBounds().toFloat();
    const bool gl = ghost();
    if (gl) gg::body (g, r.reduced (1.0f), 12.0f, juce::Colours::white, 0.42f);
    else
    {
        g.setColour (T (juce::Colour (0xff151824))); g.fillRoundedRectangle (r.reduced (0.5f), 10);
        g.setColour (col::pink.withAlpha (0.25f)); g.drawRoundedRectangle (r.reduced (0.5f), 10, 1.0f);
    }
    const auto ink = gl ? gg::ink() : col::white.get(), ink2 = gl ? gg::ink2() : col::dim.get();
    auto head = getLocalBounds().reduced (10, 4).removeFromTop (16);
    g.setColour (ink); g.setFont (font (11.5f));
    g.drawText (juce::String (index() + 1) + "   " + juce::String (clip->bars) + " bars  " + juce::String (clip->bpm, 1) + " BPM" + (clip->reversed ? "  REVERSED" : ""), head, juce::Justification::centredLeft, true);
    g.setColour (ink2); g.setFont (font (9.5f));
    g.drawText (juce::Time (clip->timeMs).formatted ("%H:%M:%S"), head, juce::Justification::centredRight, false);
    // the waveform (min / max per column), in a well
    auto w = wave().toFloat();
    if (gl) gg::recessed (g, w, 6.0f); else { g.setColour (T (juce::Colour (0xff07080c))); g.fillRoundedRectangle (w, 6); }
    const int n = (int) clip->peaks.size() / 2;
    if (n > 0)
    {
        const float mid = w.getCentreY(), half = w.getHeight() * 0.45f;
        juce::Path p;
        for (int i = 0; i < n; ++i) { const float x = w.getX() + w.getWidth() * (float) i / (float) n; p.addRectangle (x, mid - half * juce::jlimit (0.0f, 1.0f, clip->peaks[(size_t) i * 2 + 1]), juce::jmax (1.0f, w.getWidth() / (float) n), juce::jmax (1.0f, half * (juce::jlimit (0.0f, 1.0f, clip->peaks[(size_t) i * 2 + 1]) + juce::jlimit (0.0f, 1.0f, -clip->peaks[(size_t) i * 2])))); }
        if (gl) g.setGradientFill (juce::ColourGradient (gg::violetDeep(), w.getX(), 0, gg::teal().darker (0.35f), w.getRight(), 0, false));
        else g.setGradientFill (juce::ColourGradient (col::pink.get(), w.getX(), 0, col::ice.get(), w.getRight(), 0, false));
        g.fillPath (p);
    }
    if (page.proc.previewingId() == clip->id)
    {   // the loop's play head
        const float x = w.getX() + w.getWidth() * page.proc.previewPos();
        g.setColour (ink.withAlpha (0.8f)); g.drawVerticalLine ((int) x, w.getY() + 2, w.getBottom() - 2);
    }
    if (isMouseOver (false) && ! gl) { g.setColour (col::pink.withAlpha (0.12f)); g.fillRoundedRectangle (w, 6); }
}

inline std::vector<BoxCapturePage::Text> BoxCapturePage::texts()
{
    using namespace gpfx::ui; std::vector<Text> v;
    for (int k = 0; k < tiles.size(); ++k)
    {
        auto* t = tiles[k]; auto head = t->getBounds().reduced (10, 4).removeFromTop (16);
        v.push_back ({ head.withWidth (juce::jmin (head.getWidth(), 150)).withSizeKeepingCentre (juce::jmin (head.getWidth(), 150), 9), ghost() ? ghosty::glass::ink() : col::white.get(), "tile " + juce::String (k + 1) });
    }
    v.push_back ({ statusR.withTrimmedLeft (juce::jmax (0, statusR.getWidth() - (int) ghosty::textWidth (font (10.5f), statusText()))).withSizeKeepingCentre (juce::jmin (statusR.getWidth(), (int) ghosty::textWidth (font (10.5f), statusText())), 9), ghost() ? ghosty::glass::ink2() : col::dim.get(), "status" });
    return v;
}
