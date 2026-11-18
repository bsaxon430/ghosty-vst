#pragma once
// GHOSTPRINT page (a tab of the rack panel, "PRINT"; also a shortcut button in the strip's free tile cell and an entry in the scene bar's
// "..." menu): drop a short audio reference (or pick a CAPTURE clip), see its gesture (ENVELOPE, ACCENTS, BRIGHTNESS, STEREO) over one
// loop, choose where each curve goes, shape it (INTENSITY / SMOOTH / RHYTHM-ONLY / INVERT) and APPLY it as GHOST MOTION timeline lanes.
// It is a gesture extractor: the reference's audio and timbre are never used. Themed in all 4 looks. (GhostBox/BoxPrint.h = the analysis.)
#include "BoxProcessor.h"
#include "FxUi.h"
#include "GhostGlass.h"

class BoxPrintPage : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    static constexpr juce::uint32 kAccent = 0xff7FFFB0;
    explicit BoxPrintPage (BoxProcessor& p) : proc (p)
    {
        auto btn = [this] (juce::TextButton& b, const char* text, const char* tip) { b.setButtonText (text); b.setTooltip (tip); b.setColour (juce::TextButton::buttonOnColourId, acc()); addAndMakeVisible (b); };
        btn (loadB, "LOAD FILE...", "Pick a short audio reference (WAV, AIFF, FLAC, MP3, OGG; up to 60 s). Or drop a file anywhere on this page");
        btn (clipB, "FROM CAPTURE", "Use one of your GHOST CAPTURE clips as the reference (its tempo and bars are known)");
        btn (applyB, "APPLY", "Write the pattern into the GHOST MOTION timeline (MOTION tab, TIMELINE): one lane per curve, the loop set to its length. Lanes on the same settings are overwritten. One UNDO step");
        btn (rhythmB, "RHYTHM-ONLY", "RHYTHM-ONLY: the VOLUME lane becomes a gate that opens on the accents (the envelope's shape is ignored)");
        btn (invertB, "INVERT", "INVERT: loud becomes quiet, bright becomes dark, wide becomes narrow, accents duck instead of push");
        rhythmB.setClickingTogglesState (true); invertB.setClickingTogglesState (true);
        rhythmB.onClick = [this] { proc.printSet.rhythmOnly = rhythmB.getToggleState(); repaint(); };
        invertB.onClick = [this] { proc.printSet.invert = invertB.getToggleState(); repaint(); };
        loadB.onClick = [this] { chooseFile(); };
        clipB.onClick = [this] { clipMenu(); };
        applyB.onClick = [this] { proc.printApply(); refresh(); };
        auto slider = [this] (juce::Slider& s, double lo, double hi, double step, const char* tip)
        {
            s.setSliderStyle (juce::Slider::LinearHorizontal); s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 16); s.setRange (lo, hi, step);
            s.setColour (juce::Slider::rotarySliderFillColourId, acc()); s.setColour (juce::Slider::thumbColourId, acc()); s.setTooltip (tip); addAndMakeVisible (s);
        };
        slider (intensity, 0.0, 100.0, 1.0, "INTENSITY: how far the lanes move their settings (100 % = the reference's full gesture)");
        slider (smooth, 0.0, 100.0, 1.0, "SMOOTH: rounds the curves off (0 % = every 1/16 as measured)");
        intensity.setTextValueSuffix (" %"); smooth.setTextValueSuffix (" %");
        intensity.setDoubleClickReturnValue (true, 100.0); smooth.setDoubleClickReturnValue (true, 0.0);
        intensity.onValueChange = [this] { proc.printSet.intensity = (float) intensity.getValue() / 100.0f; repaint(); };
        smooth.onValueChange = [this] { proc.printSet.smooth = (float) smooth.getValue() / 100.0f; repaint(); };
        bpm.setSliderStyle (juce::Slider::IncDecButtons); bpm.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 60, 18); bpm.setRange (0.0, 250.0, 0.5);
        bpm.textFromValueFunction = [] (double v) { return v < 30.0 ? juce::String ("AUTO") : juce::String (v, 1); };
        bpm.valueFromTextFunction = [] (const juce::String& t) { return t.trim().equalsIgnoreCase ("auto") ? 0.0 : juce::jlimit (0.0, 250.0, t.getDoubleValue()); };
        bpm.updateText();
        bpm.setTooltip ("The reference's tempo. AUTO detects it (and snaps to a whole number of bars); type a BPM when you know it");
        bpm.setColour (juce::Slider::textBoxOutlineColourId, acc().withAlpha (0.4f));
        bpm.onValueChange = [this] { const double v = bpm.getValue(); proc.printOpt.bpm = v < 30.0 ? 0.0 : v; proc.printReanalyse(); refresh(); };
        addAndMakeVisible (bpm);
        bars.addItemList ({ "AUTO", "1 BAR", "2 BARS", "4 BARS", "8 BARS" }, 1);
        bars.setTooltip ("How many bars the reference is (sets its tempo from its length). AUTO detects the tempo");
        bars.setColour (juce::ComboBox::outlineColourId, acc());
        bars.onChange = [this] { static const int b[] { 0, 1, 2, 4, 8 }; proc.printOpt.bars = b[juce::jlimit (0, 4, bars.getSelectedItemIndex())]; if (proc.printOpt.bars > 0) { proc.printOpt.bpm = 0; bpm.setValue (0.0, juce::dontSendNotification); } proc.printReanalyse(); refresh(); };
        addAndMakeVisible (bars);
        for (int c = 0; c < boxprint::kCurves; ++c)
        {
            auto* cb = destBox.add (new juce::ComboBox());
            for (int k = 0; k < (int) boxprint::dests (c).size(); ++k) cb->addItem (boxprint::dests (c)[(size_t) k].name, k + 1);
            cb->setSelectedItemIndex (proc.printSet.dest[(size_t) c], juce::dontSendNotification);
            cb->setColour (juce::ComboBox::outlineColourId, curveColour (c));
            static const char* tips[] { "Where the ENVELOPE goes: VOLUME (a clean gain before the limiter), PHANTOM FILTER's cutoff, or GHOST SPACE's distance",
                                        "Where the ACCENTS go: short throws on ECHO, SPECTRAL SPACE or PHANTOM FILTER (OFF = shown only; RHYTHM-ONLY uses them for the gate)",
                                        "Where the BRIGHTNESS goes: TONE (a clean low-pass before the limiter) or PHANTOM FILTER's cutoff",
                                        "Where the STEREO movement goes: WIDTH (mid / side) or GHOST SPACE's left / right place" };
            cb->setTooltip (tips[c]);
            cb->onChange = [this, c] { proc.printSet.dest[(size_t) c] = destBox[c]->getSelectedItemIndex(); repaint(); };
            addAndMakeVisible (cb);
        }
        refresh();
        startTimerHz (10);
    }
    ~BoxPrintPage() override { stopTimer(); }

    // ---- drag and drop (a bad file gives a message, never a crash)
    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        for (auto& f : files) if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg")) return true;
        return ! files.isEmpty();   // (anything else is still taken: it gets a message)
    }
    void fileDragEnter (const juce::StringArray&, int, int) override { hover = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override { hover = false; repaint(); }
    void filesDropped (const juce::StringArray& files, int, int) override
    {
        hover = false;
        if (files.isEmpty()) return;
        proc.printLoadFile (juce::File (files[0]));
        refresh();
    }

    void refresh()
    {
        const bool has = proc.printResult.ok;
        applyB.setEnabled (has && ! proc.printBusy.load());
        rhythmB.setToggleState (proc.printSet.rhythmOnly, juce::dontSendNotification); invertB.setToggleState (proc.printSet.invert, juce::dontSendNotification);
        intensity.setValue (juce::roundToInt (proc.printSet.intensity * 100.0f), juce::dontSendNotification);
        smooth.setValue (juce::roundToInt (proc.printSet.smooth * 100.0f), juce::dontSendNotification);
        bpm.setValue (proc.printOpt.bpm, juce::dontSendNotification);
        { const int b = proc.printOpt.bars; bars.setSelectedItemIndex (b == 1 ? 1 : b == 2 ? 2 : b == 4 ? 3 : b == 8 ? 4 : 0, juce::dontSendNotification); }
        for (int c = 0; c < destBox.size(); ++c) destBox[c]->setSelectedItemIndex (proc.printSet.dest[(size_t) c], juce::dontSendNotification);
        clipB.setEnabled (! proc.shelf.empty());
        repaint();
    }
    void paint (juce::Graphics& g) override
    {
        using namespace gpfx::ui;
        const bool gl = ghost();
        const auto ink = gl ? ghosty::glass::ink() : col::white.get(), ink2 = gl ? ghosty::glass::ink2() : col::dim.get();
        drawCaption (g, bpmCap.toFloat(), "BPM", ink2, 10.0f);
        drawCaption (g, intCap.toFloat(), "INTENSITY", ink2, 10.0f);
        drawCaption (g, smCap.toFloat(), "SMOOTH", ink2, 10.0f);
        const auto msg = proc.printBusy.load() ? (proc.printMessage.isNotEmpty() ? proc.printMessage : juce::String ("reading the gesture...")) : proc.printMessage;
        drawCaption (g, msgR.toFloat(), msg.isNotEmpty() ? (proc.printName.isNotEmpty() ? proc.printName + ":  " : juce::String()) + msg : juce::String ("No reference yet"), gl ? ghosty::glass::violetDeep() : acc().brighter (0.2f), 10.5f);
        // the curves over one loop
        auto view = viewR.toFloat();
        drawScreen (g, view, acc());
        if (hover) { g.setColour (acc().withAlpha (0.18f)); g.fillRoundedRectangle (view, 10.0f); }
        const auto& A = proc.printResult;
        if (! A.ok)
        {
            g.setColour (ink); g.setFont (font (15.0f));
            g.drawText ("Drop a short audio reference here", view.withTrimmedBottom (view.getHeight() * 0.5f - 4), juce::Justification::centredBottom, false);
            g.setColour (ink2); g.setFont (font (11.0f));
            g.drawText ("WAV, AIFF, FLAC, MP3 or OGG, or a GHOST CAPTURE clip. Ghost Box takes its movement (volume, accents, brightness, stereo) and never its sound",
                        view.withTrimmedTop (view.getHeight() * 0.5f + 4), juce::Justification::centredTop, false);
        }
        const auto P = proc.printPattern();
        for (int c = 0; c < boxprint::kCurves; ++c)
        {
            auto row = rowR[(size_t) c].toFloat();
            drawCaption (g, labR[(size_t) c].toFloat(), boxprint::curveName (c), ink, 11.0f);
            if (! A.ok) continue;
            const int n = A.steps; const float w = row.getWidth() / (float) n;
            g.setColour (ink2.withAlpha (0.25f));
            for (int j = 0; j <= n; j += 4) g.fillRect (row.getX() + j * w, row.getY(), j % A.stepsPerBar == 0 ? 1.5f : 0.6f, row.getHeight());
            const auto col = curveColour (c);
            const auto& v = P.preview[(size_t) c];
            if (c == boxprint::cEnv || c == boxprint::cAccent)
            {
                for (int j = 0; j < n && j < (int) v.size(); ++j)
                {
                    const float h = juce::jlimit (0.0f, 1.0f, c == boxprint::cAccent ? v[(size_t) j] : v[(size_t) j]) * (row.getHeight() - 4);
                    if (c == boxprint::cAccent && v[(size_t) j] <= 0.0f) continue;
                    g.setColour (col.withAlpha (c == boxprint::cAccent ? 0.95f : 0.75f));
                    g.fillRoundedRectangle (row.getX() + j * w + 1.0f, row.getBottom() - 2 - h, std::max (1.5f, w - 2.0f), h, 1.5f);
                }
            }
            else
            {
                const bool pan = c == boxprint::cStereo && ! A.stereoMeasurable && A.panMoves;
                juce::Path pth;
                for (int j = 0; j < n && j < (int) v.size(); ++j)
                {
                    const float val = pan ? 0.5f + 0.5f * v[(size_t) j] : v[(size_t) j];
                    const float px = row.getX() + (j + 0.5f) * w, py = row.getBottom() - 2 - juce::jlimit (0.0f, 1.0f, val) * (row.getHeight() - 4);
                    if (j == 0) pth.startNewSubPath (px, py); else pth.lineTo (px, py);
                }
                glowStroke (g, pth, col, 1.8f, gl ? 0.0f : 1.0f);
            }
            const juce::String note = c == boxprint::cEnv ? juce::String (A.envRangeDb, 1) + " dB"
                                    : c == boxprint::cBright ? (A.brightMoves ? juce::String ((int) A.brightLoHz) + " - " + juce::String ((int) A.brightHiHz) + " Hz" : juce::String ("steady"))
                                    : c == boxprint::cStereo ? (A.stereoMeasurable ? juce::String ("width moves") : A.panMoves ? juce::String ("pan moves") : juce::String ("not measurable"))
                                    : juce::String (std::count_if (A.accent.begin(), A.accent.end(), [] (float x) { return x > 0; })) + " hits";
            drawCaption (g, noteR[(size_t) c].toFloat(), note, ink2, 10.0f);
        }
        if (A.ok)
        {
            g.setColour (ink2); g.setFont (font (10.0f));
            for (int b = 0; b < A.loopBars; ++b)
                g.drawText ("BAR " + juce::String (b + 1), juce::Rectangle<float> (rowR[0].getX() + b * rowR[0].getWidth() / (float) A.loopBars + 4.0f, (float) viewR.getY() + 2.0f, 60.0f, 12.0f), juce::Justification::centredLeft, false);
        }
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (6, 2);
        auto row1 = r.removeFromTop (30);
        loadB.setBounds (row1.removeFromLeft (120).reduced (0, 2)); row1.removeFromLeft (6);
        clipB.setBounds (row1.removeFromLeft (120).reduced (0, 2)); row1.removeFromLeft (14);
        bpmCap = row1.removeFromLeft (30); bpm.setBounds (row1.removeFromLeft (120).reduced (0, 3)); row1.removeFromLeft (8);
        bars.setBounds (row1.removeFromLeft (100).reduced (0, 3)); row1.removeFromLeft (14);
        msgR = row1;
        auto bottom = r.removeFromBottom (32);
        intCap = bottom.removeFromLeft (64); intensity.setBounds (bottom.removeFromLeft (190).reduced (0, 4)); bottom.removeFromLeft (14);
        smCap = bottom.removeFromLeft (52); smooth.setBounds (bottom.removeFromLeft (190).reduced (0, 4)); bottom.removeFromLeft (14);
        rhythmB.setBounds (bottom.removeFromLeft (120).reduced (0, 3)); bottom.removeFromLeft (6);
        invertB.setBounds (bottom.removeFromLeft (90).reduced (0, 3)); bottom.removeFromLeft (14);
        applyB.setBounds (bottom.removeFromRight (140).reduced (0, 2));
        r.removeFromBottom (6); r.removeFromTop (6);
        viewR = r;
        auto in = r.reduced (10, 16);
        const int rh = in.getHeight() / boxprint::kCurves;
        for (int c = 0; c < boxprint::kCurves; ++c)
        {
            auto row = in.removeFromTop (rh).reduced (0, 3);
            auto left = row.removeFromLeft (170);
            labR[(size_t) c] = left.removeFromTop (16);
            destBox[c]->setBounds (left.removeFromTop (24).reduced (0, 1).withWidth (160));
            noteR[(size_t) c] = left.removeFromTop (14);
            row.removeFromLeft (8);
            rowR[(size_t) c] = row;
        }
    }
    // tests
    struct Text { juce::Rectangle<int> r; juce::Colour c; juce::String what; };
    std::vector<Text> texts() const
    {
        using namespace gpfx::ui; std::vector<Text> v;
        const auto ink = ghost() ? ghosty::glass::ink() : col::white.get(), ink2 = ghost() ? ghosty::glass::ink2() : col::dim.get();
        v.push_back ({ bpmCap.withSizeKeepingCentre (bpmCap.getWidth(), 9), ink2, "BPM" });
        v.push_back ({ intCap.withSizeKeepingCentre (intCap.getWidth(), 9), ink2, "INTENSITY" });
        v.push_back ({ labR[0].withWidth (70).withSizeKeepingCentre (70, 10), ink, "ENVELOPE" });
        return v;
    }
    juce::TextButton& applyButton() { return applyB; }

private:
    static juce::Colour acc() { return fxtheme::T (juce::Colour (kAccent)); }
    static juce::Colour curveColour (int c)
    {
        if (gpfx::ui::ghost()) { static const juce::uint32 g[] { 0xff5b3fd0, 0xffc0306f, 0xff1f7f9a, 0xff8a5a00 }; return juce::Colour (g[juce::jlimit (0, 3, c)]); }
        static const juce::uint32 k[] { 0xff7FFFB0, 0xffFF4FD8, 0xff45E6FF, 0xffFFC35A };
        return fxtheme::T (juce::Colour (k[juce::jlimit (0, 3, c)]));
    }
    BoxProcessor& proc;
    juce::TextButton loadB, clipB, applyB, rhythmB, invertB; juce::Slider intensity, smooth, bpm; juce::ComboBox bars;
    juce::OwnedArray<juce::ComboBox> destBox;
    juce::Rectangle<int> bpmCap, intCap, smCap, msgR, viewR;
    std::array<juce::Rectangle<int>, boxprint::kCurves> rowR, labR, noteR;
    std::unique_ptr<juce::FileChooser> chooser;
    bool hover = false; int seenVersion = -1;
    void chooseFile()
    {
        chooser = std::make_unique<juce::FileChooser> ("Pick an audio reference for GHOSTPRINT", juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [sp = juce::Component::SafePointer<BoxPrintPage> (this)] (const juce::FileChooser& fc)
        {
            if (sp == nullptr) return;
            const auto f = fc.getResult();
            if (f != juce::File()) { sp->proc.printLoadFile (f); sp->refresh(); }
        });
    }
    void clipMenu()
    {
        juce::PopupMenu m; m.addSectionHeader ("GHOST CAPTURE clips");
        for (auto& c : proc.shelf) if (c != nullptr) m.addItem (c->id, (c->name.isNotEmpty() ? c->name : "capture " + juce::String (c->id)) + "  (" + juce::String (c->bars) + " bars, " + juce::String (c->bpm, 1) + " BPM)");
        if (proc.shelf.empty()) m.addItem (-1, "No captures yet (CAPTURE tab)", false);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&clipB), [sp = juce::Component::SafePointer<BoxPrintPage> (this)] (int r) { if (sp != nullptr && r > 0) { sp->proc.printLoadClip (r); sp->refresh(); } });
    }
    void timerCallback() override { if (proc.printVersion.load() != seenVersion || proc.printBusy.load()) { seenVersion = proc.printVersion.load(); refresh(); } }
};
