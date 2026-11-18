#pragma once
// WAVETABLE EDITOR window content (round 3 item 2b, part 2, 2026-10-25). Opened from the SYNTH page's EDITOR button (in the free
// space of the wave toolbar: nothing existing moves). Edits a gpro::wt::Doc; LIVE pushes every change into the synth's "User" table so
// the OSC display above follows; "OSC n PLAYS IT" switches that oscillator to the User table (and back).
//   frame strip (click = select, SHIFT-click = key frame on / off), the current frame drawn big (drag to draw, SHIFT = line),
//   harmonic bars (drag = magnitude) + phase bars (drag = phase), ops as buttons, GENERATE recipes, IMPORT a sample at a chosen
//   cycle length (0 = auto), LOAD / SAVE / SAVE AS in the user bank (~/Documents/Ghosty/Ghosty Pro/Wavetables), EXPORT WAV.
// Colours come from pt::pal() at paint time (all three themes); the graph areas are displays (dark in LIGHT, like the OSC screen).
#include "ProProcessor.h"
#include "ProTheme.h"
#include "ProGlass.h"      // GLASS (round 3)
#include "ExportGate.h"    // the demo export gate (EXPORT WAV, SAVE / SAVE AS: a .ghostywt is float32 PCM in base64, trivially a WAV)

namespace gpro::wt
{
// a frame of any power-of-two length -> TBL points (4-point cubic around the cycle; TBL stays as it is)
inline HiFrame toTbl (const HiFrame& f)
{
    if ((int) f.size() == TBL) return f;
    const int n = (int) f.size(); HiFrame o ((size_t) TBL);
    if (n < 4) return HiFrame ((size_t) TBL, n > 0 ? f[0] : 0.0f);
    for (int i = 0; i < TBL; ++i)
    {
        const double pos = (double) i / TBL * n; const int a = (int) pos; const float t = (float) (pos - a);
        auto at = [&] (int k) { return f[(size_t) ((k % n + n) % n)]; };
        const float ym1 = at (a - 1), y0 = at (a), y1 = at (a + 1), y2 = at (a + 2);
        const float c1 = 0.5f * (y1 - ym1), c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2, c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
        o[(size_t) i] = ((c3 * t + c2) * t + c1) * t + y0;
    }
    return o;
}
} // namespace gpro::wt

class WavetableEditor : public juce::Component, private juce::Timer, public juce::FileDragAndDropTarget
{
public:
    static constexpr int W = 880, H = 600;
    static constexpr int kBars = 64;         // harmonics shown / editable in the bars

    WavetableEditor (ProProcessor& p, int oscIndex) : proc (p), osc (oscIndex)
    {
        for (auto* b : { &loadBtn, &saveBtn, &saveAsBtn, &importBtn, &exportBtn, &playBtn, &liveBtn, &undoBtn, &redoBtn,
                         &normBtn, &dcBtn, &invBtn, &revBtn, &smoothBtn, &alignBtn, &keyBtn, &morphXBtn, &morphSBtn, &addBtn, &delBtn, &clearKeysBtn,
                         &genFrameBtn, &genTableBtn, &recutBtn, &seedBtn })
            addAndMakeVisible (b);
        addAndMakeVisible (nameBox); addAndMakeVisible (recipeA); addAndMakeVisible (recipeB); addAndMakeVisible (framesSl); addAndMakeVisible (harmSl); addAndMakeVisible (cycleBox);
        nameBox.setText ("User", juce::dontSendNotification); nameBox.setJustification (juce::Justification::centredLeft);
        nameBox.onTextChange = [this] { doc.name = nameBox.getText().trim(); };
        nameBox.setTooltip ("The table's name (the file name in the user bank)");
        recipeA.addItemList (gpro::wt::recipeNames(), 1); recipeB.addItemList (gpro::wt::recipeNames(), 1);
        recipeA.setSelectedId (1 + gpro::wt::genSaw, juce::dontSendNotification); recipeB.setSelectedId (1 + gpro::wt::genSquare, juce::dontSendNotification);
        recipeA.setTooltip ("GENERATE: the recipe (GEN FRAME puts it into the current frame; GEN TABLE morphs from this ...)"); recipeB.setTooltip ("GENERATE: ... to this recipe over FRAMES frames");
        framesSl.setRange (1, gpro::wt::kMaxFrames, 1); framesSl.setValue (16, juce::dontSendNotification); framesSl.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 44, 20);
        framesSl.setTooltip ("FRAMES: how many frames GEN TABLE and a sample import make (1 .. 256)");
        harmSl.setRange (1, 512, 1); harmSl.setValue (64, juce::dontSendNotification); harmSl.setSkewFactorFromMidPoint (48); harmSl.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 44, 20);
        harmSl.setTooltip ("HARMONICS: how many partials the recipes use (more = brighter)");
        cycleBox.setText ("0", juce::dontSendNotification); cycleBox.setInputRestrictions (6, "0123456789"); cycleBox.setJustification (juce::Justification::centred);
        cycleBox.setTooltip ("CYCLE: the cycle length in samples a sample is cut at. 0 = detect the pitch. Change it and press RE-CUT");
        loadBtn.setTooltip ("Load a table from the user bank, the sound's User table, or what this oscillator plays");
        saveBtn.setTooltip ("Save into the user bank under the name above (overwrites)"); saveAsBtn.setTooltip ("Save under a new name");
        importBtn.setTooltip ("Import a WAV / AIFF / FLAC: a 2048-point wavetable file is taken as it is, anything else is cut into cycles (CYCLE, FRAMES)");
        exportBtn.setTooltip ("Export a float32 WAV: the frames one after another (2048 points each)");
        playBtn.setTooltip ("Switch this oscillator to the User table (off = back to the table it had)"); liveBtn.setTooltip ("Push every edit into the synth's User table as you work");
        undoBtn.setTooltip ("Undo (Ctrl / Cmd + Z)"); redoBtn.setTooltip ("Redo (Ctrl / Cmd + Shift + Z)");
        normBtn.setTooltip ("Peak of the frame to 1"); dcBtn.setTooltip ("Remove the frame's offset"); invBtn.setTooltip ("Flip the frame upside down"); revBtn.setTooltip ("Play the frame backwards");
        smoothBtn.setTooltip ("Round the frame's corners"); alignBtn.setTooltip ("Rotate the frame so its fundamental starts at 0 (frames then line up)");
        keyBtn.setTooltip ("Make the current frame a key frame (or not): MORPH fills the frames between key frames. SHIFT-click a frame in the strip does the same");
        morphXBtn.setTooltip ("Fill the frames between key frames with a cross-fade"); morphSBtn.setTooltip ("Fill the frames between key frames in the spectral domain (harmonics glide)");
        addBtn.setTooltip ("Insert a copy of the current frame after it"); delBtn.setTooltip ("Delete the current frame"); clearKeysBtn.setTooltip ("Forget the key frames");
        genFrameBtn.setTooltip ("Recipe A into the current frame"); genTableBtn.setTooltip ("A new table: recipe A morphing to recipe B over FRAMES frames");
        recutBtn.setTooltip ("Cut the last imported sample again with the CYCLE length and FRAMES above"); seedBtn.setTooltip ("Start from what this oscillator plays now");
        for (auto* b : { &playBtn, &liveBtn }) b->setClickingTogglesState (true);
        liveBtn.setToggleState (true, juce::dontSendNotification);

        loadBtn.onClick = [this] { loadMenu(); };
        saveBtn.onClick = [this] { save (false); }; saveAsBtn.onClick = [this] { save (true); };
        importBtn.onClick = [this] { chooseImport(); }; exportBtn.onClick = [this] { chooseExport(); };
        playBtn.onClick = [this] { setPlays (playBtn.getToggleState()); };
        liveBtn.onClick = [this] { if (liveBtn.getToggleState()) { dirty = true; push(); } };
        undoBtn.onClick = [this] { undo(); }; redoBtn.onClick = [this] { redo(); };
        normBtn.onClick = [this] { op ("normalise"); }; dcBtn.onClick = [this] { op ("dc"); }; invBtn.onClick = [this] { op ("invert"); }; revBtn.onClick = [this] { op ("reverse"); };
        smoothBtn.onClick = [this] { op ("smooth"); }; alignBtn.onClick = [this] { op ("align"); };
        keyBtn.onClick = [this] { op ("key"); }; morphXBtn.onClick = [this] { op ("morphx"); }; morphSBtn.onClick = [this] { op ("morphs"); };
        addBtn.onClick = [this] { op ("add"); }; delBtn.onClick = [this] { op ("del"); }; clearKeysBtn.onClick = [this] { op ("clearkeys"); };
        genFrameBtn.onClick = [this] { op ("genframe"); }; genTableBtn.onClick = [this] { op ("gentable"); };
        recutBtn.onClick = [this] { recut(); }; seedBtn.onClick = [this] { seedFromOsc(); };

        // start: the sound's User table if it has one, else what this oscillator plays
        auto cur = proc.userTable();
        if (cur.valid()) setDoc (cur, false); else seedFromOsc (false);
        playBtn.setToggleState ((int) tableOf() == gpro::tUser, juce::dontSendNotification);
        setSize (W, H); setWantsKeyboardFocus (true);
        startTimerHz (20);
    }
    ~WavetableEditor() override { setLookAndFeel (nullptr); }

    // ------------------------------------------------------------ model (also the test hooks)
    const gpro::wt::Doc& document() const { return doc; }
    int oscIndex() const { return osc; }
    int currentFrame() const { return cur; }
    void setDoc (gpro::wt::Doc d, bool keepUndo = true)
    {
        if (! d.valid()) return;
        if (keepUndo) pushUndo();
        doc = std::move (d); for (auto& f : doc.frames) f = gpro::wt::toTbl (f);
        cur = juce::jlimit (0, (int) doc.frames.size() - 1, cur);
        nameBox.setText (doc.name, juce::dontSendNotification);
        spec = gpro::wt::analyze (doc.frames[(size_t) cur]);
        changed();
    }
    void selectFrame (int i) { cur = juce::jlimit (0, (int) doc.frames.size() - 1, i); spec = gpro::wt::analyze (doc.frames[(size_t) cur]); repaint(); }
    void setKey (int frame, bool on)
    {
        auto& k = doc.keys; auto it = std::find (k.begin(), k.end(), frame);
        if (on && it == k.end()) k.push_back (frame); else if (! on && it != k.end()) k.erase (it);
        std::sort (k.begin(), k.end()); repaint();
    }
    bool isKey (int frame) const { return std::find (doc.keys.begin(), doc.keys.end(), frame) != doc.keys.end(); }
    // an op by name (the buttons call these): normalise dc invert reverse smooth align key morphx morphs add del clearkeys genframe gentable
    void op (const juce::String& name)
    {
        namespace wt = gpro::wt;
        auto& f = doc.frames[(size_t) cur];
        if (name == "key") { setKey (cur, ! isKey (cur)); status = isKey (cur) ? "frame " + juce::String (cur + 1) + " is a key frame" : "frame " + juce::String (cur + 1) + " is no key frame"; repaint(); return; }
        if (name == "clearkeys") { doc.keys.clear(); status = "key frames cleared"; repaint(); return; }
        pushUndo();
        if (name == "normalise") f = wt::normalise (f);
        else if (name == "dc") f = wt::removeDC (f);
        else if (name == "invert") f = wt::invert (f);
        else if (name == "reverse") f = wt::reverse (f);
        else if (name == "smooth") f = wt::smooth (f, 2);
        else if (name == "align") f = wt::alignFundamental (f);
        else if (name == "morphx" || name == "morphs")
        {
            if (doc.keys.size() < 2) { status = "MORPH needs two or more key frames (SHIFT-click frames in the strip, or KEY)"; undoStack.pop_back(); repaint(); return; }
            wt::morph (doc, name == "morphx" ? wt::morphCrossfade : wt::morphSpectral);
            status = juce::String (name == "morphx" ? "cross-fade" : "spectral") + " morph between " + juce::String ((int) doc.keys.size()) + " key frames";
        }
        else if (name == "add")
        {
            if ((int) doc.frames.size() >= wt::kMaxFrames) { status = "256 frames is the limit"; undoStack.pop_back(); repaint(); return; }
            doc.frames.insert (doc.frames.begin() + cur + 1, f);
            for (auto& k : doc.keys) if (k > cur) ++k;
            cur = cur + 1;
        }
        else if (name == "del")
        {
            if (doc.frames.size() <= 1) { status = "the last frame stays"; undoStack.pop_back(); repaint(); return; }
            doc.frames.erase (doc.frames.begin() + cur);
            doc.keys.erase (std::remove (doc.keys.begin(), doc.keys.end(), cur), doc.keys.end());
            for (auto& k : doc.keys) if (k > cur) --k;
            cur = juce::jlimit (0, (int) doc.frames.size() - 1, cur);
        }
        else if (name == "genframe") { doc.made = true; doc.frames[(size_t) cur] = wt::generate (recipeA.getSelectedId() - 1, (int) harmSl.getValue(), ++seed); status = recipeA.getText() + " into frame " + juce::String (cur + 1); }
        else if (name == "gentable")
        {
            auto d = wt::generateTable (recipeA.getSelectedId() - 1, recipeB.getSelectedId() - 1, (int) framesSl.getValue(), (int) harmSl.getValue(), ++seed);
            d.name = doc.name.isEmpty() ? d.name : doc.name;
            doc = std::move (d); doc.made = true; cur = 0; status = juce::String ((int) doc.frames.size()) + " frames: " + recipeA.getText() + " > " + recipeB.getText();
        }
        spec = wt::analyze (doc.frames[(size_t) cur]);
        changed();
    }
    // the bars: harmonic h (1 .. kBars) magnitude 0 .. 1 / phase -pi .. pi, resynthesised at once
    void setHarmonic (int h, float mag) { if (h < 1 || h >= gpro::wt::kBins) return; spec.mag[(size_t) h] = juce::jlimit (0.0f, 1.0f, mag); resynth(); }
    void setPhase (int h, float ph) { if (h < 1 || h >= gpro::wt::kBins) return; spec.phase[(size_t) h] = ph; resynth(); }
    // draw into the current frame: a value -1 .. 1 at a phase 0 .. 1 (the strip between the last call and this one is filled)
    void drawPoint (float phase, float value, bool first)
    {
        auto& f = doc.frames[(size_t) cur]; doc.made = true;   // a drawn frame is made here
        const int i = juce::jlimit (0, gpro::TBL - 1, (int) std::lround (phase * (gpro::TBL - 1)));
        const float v = juce::jlimit (-1.0f, 1.0f, value);
        if (first || lastI < 0) f[(size_t) i] = v;
        else { const int a = juce::jmin (lastI, i), b = juce::jmax (lastI, i); const float va = lastI < i ? lastV : v, vb = lastI < i ? v : lastV;
               for (int k = a; k <= b; ++k) f[(size_t) k] = b == a ? v : va + (vb - va) * (float) (k - a) / (float) (b - a); }
        lastI = i; lastV = v; drawing = true; repaint();
    }
    void endDraw() { lastI = -1; drawing = false; spec = gpro::wt::analyze (doc.frames[(size_t) cur]); changed(); }
    bool importFile (const juce::File& file, int cycleLen)
    {
        gpro::wt::Doc d; juce::String msg;
        if (! gpro::wt::importAudio (file, d, cycleLen, (int) framesSl.getValue(), &msg)) { status = msg; repaint(); return false; }
        lastSample = file; setDoc (d); cur = 0; selectFrame (0);
        cycleBox.setText (juce::String (doc.cycleLen), juce::dontSendNotification);
        status = file.getFileName() + ": " + msg; repaint();
        return true;
    }
    void recut() { if (lastSample.existsAsFile()) importFile (lastSample, cycleBox.getText().getIntValue()); else { status = "import a sample first (IMPORT, or drop a WAV here)"; repaint(); } }
    bool saveTo (const juce::File& f) { const bool ok = gpro::wt::save (doc, f); status = ok ? "saved " + f.getFileName() : "could not write " + f.getFullPathName(); repaint(); return ok; }
    void setPlays (bool on)
    {
        const juce::String id = osc == 0 ? "o1_tbl" : "o2_tbl";
        if (on) { if ((int) tableOf() != gpro::tUser) { before = (int) tableOf(); proc.setParam (id, (float) gpro::tUser); } dirty = true; push(); }
        else if ((int) tableOf() == gpro::tUser) proc.setParam (id, (float) juce::jlimit (0, (int) gpro::numTables - 1, before));
        playBtn.setToggleState (on, juce::dontSendNotification); repaint();
    }
    void seedFromOsc (bool keepUndo = true)
    {
        gpro::wt::Doc d; d.name = doc.name.isEmpty() ? "User" : doc.name; d.made = true;   // a built-in table's frames = plugin-made
        const int id = (int) tableOf();
        d.frames = proc.sourceFramesOf (id == gpro::tUser ? gpro::tGhost : id);
        for (auto& f : d.frames) f = gpro::wt::toTbl (f);
        if (d.frames.size() > (size_t) gpro::wt::kMaxFrames) d.frames.resize ((size_t) gpro::wt::kMaxFrames);
        if (d.frames.empty()) d.frames.push_back (gpro::wt::generate (gpro::wt::genSaw, 64));
        setDoc (d, keepUndo); cur = 0; selectFrame (0);
        status = "started from " + gpro::tableNames()[id == gpro::tUser ? (int) gpro::tGhost : id] + " (" + juce::String ((int) d.frames.size()) + " frames)";
    }
    void undo() { if (undoStack.empty()) return; redoStack.push_back ({ doc, cur }); doc = undoStack.back().first; cur = juce::jlimit (0, (int) doc.frames.size() - 1, undoStack.back().second); undoStack.pop_back(); afterHistory(); }
    void redo() { if (redoStack.empty()) return; undoStack.push_back ({ doc, cur }); doc = redoStack.back().first; cur = juce::jlimit (0, (int) doc.frames.size() - 1, redoStack.back().second); redoStack.pop_back(); afterHistory(); }
    juce::String statusText() const { return status; }

    // ------------------------------------------------------------ layout
    void lookAndFeelChanged() override
    {   // a TextEditor keeps the ink colour its text was set with: re-apply the theme's
        for (auto* e : { &nameBox, &cycleBox }) e->applyColourToAllText (getLookAndFeel().findColour (juce::TextEditor::textColourId), true);
    }
    void resized() override
    {
        const int w = getWidth();
        // top bar
        nameBox.setBounds (200, 8, 140, 26);
        int x = 348; for (auto* b : { &loadBtn, &saveBtn, &saveAsBtn, &importBtn, &exportBtn }) { b->setBounds (x, 8, b == &saveAsBtn ? 66 : (b == &exportBtn ? 86 : b == &importBtn ? 60 : 56), 26); x += b->getWidth() + 4; }
        liveBtn.setBounds (w - 8 - 56, 8, 56, 26); playBtn.setBounds (liveBtn.getX() - 4 - 100, 8, 100, 26);
        strip = { 8, 44, w - 16, 74 };
        waveArea = { 8, 126, w - 16 - 300, 290 };
        harmArea = { waveArea.getRight() + 8, 126, 292, 170 }; phaseArea = { harmArea.getX(), harmArea.getBottom() + 6, 292, 114 };
        int y = 424; x = 8;
        for (auto* b : { &undoBtn, &redoBtn, &normBtn, &dcBtn, &invBtn, &revBtn, &smoothBtn, &alignBtn }) { b->setBounds (x, y, 100, 26); x += 104; }
        y = 456; x = 8;
        for (auto* b : { &keyBtn, &morphXBtn, &morphSBtn, &addBtn, &delBtn, &clearKeysBtn }) { b->setBounds (x, y, b == &morphXBtn || b == &morphSBtn ? 128 : 100, 26); x += b->getWidth() + 4; }
        y = 488;
        recipeA.setBounds (92, y, 130, 26); recipeB.setBounds (248, y, 130, 26); harmSl.setBounds (470, y, 150, 26); framesSl.setBounds (690, y, 150, 26);
        y = 520;
        genFrameBtn.setBounds (92, y, 100, 26); genTableBtn.setBounds (196, y, 100, 26); cycleBox.setBounds (380, y, 70, 26); recutBtn.setBounds (454, y, 80, 26); seedBtn.setBounds (w - 8 - 150, y, 150, 26);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& c = pt::pal();
        g.fillAll (c.bg);
        g.setColour (c.ecto); g.setFont (ghosty::font (13.0f));
        g.drawText ("WAVETABLE EDITOR  -  OSC " + juce::String (osc + 1), 10, 8, 186, 26, juce::Justification::centredLeft);
        g.setColour (c.dim); g.setFont (ghosty::font (9.5f));
        g.drawText ("GENERATE", 8, 488, 80, 26, juce::Justification::centredLeft); g.drawText (">", 224, 488, 22, 26, juce::Justification::centred);
        g.drawText ("HARMONICS", 384, 488, 82, 26, juce::Justification::centredRight); g.drawText ("FRAMES", 626, 488, 60, 26, juce::Justification::centredRight);
        g.drawText ("SAMPLE   CYCLE", 300, 520, 76, 26, juce::Justification::centredLeft);
        g.drawText (status, 8, 554, getWidth() - 16, 20, juce::Justification::centredLeft);
        paintStrip (g); paintWave (g); paintBars (g);
    }

    // ------------------------------------------------------------ mouse
    void mouseDown (const juce::MouseEvent& e) override
    {
        const auto p = e.getPosition();
        if (strip.contains (p))
        {
            const int i = frameAt (p.x); if (i < 0) return;
            if (e.mods.isShiftDown()) { setKey (i, ! isKey (i)); status = isKey (i) ? "frame " + juce::String (i + 1) + " is a key frame" : "frame " + juce::String (i + 1) + " is no key frame"; }
            selectFrame (i); return;
        }
        if (waveArea.contains (p)) { pushUndo(); strokeBase = doc.frames[(size_t) cur]; down = p; drawPoint (phaseAt (p.x), valueAt (p.y), true); return; }
        if (harmArea.contains (p) || phaseArea.contains (p)) { pushUndo(); dragBar (p); }
    }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        const auto p = e.getPosition();
        if (drawing)
        {
            if (e.mods.isShiftDown())
            {   // a straight line from where the stroke began, over the frame as it was
                auto& f = doc.frames[(size_t) cur]; f = strokeBase;
                const int i0 = juce::jlimit (0, gpro::TBL - 1, (int) std::lround (phaseAt (down.x) * (gpro::TBL - 1))), i1 = juce::jlimit (0, gpro::TBL - 1, (int) std::lround (phaseAt (p.x) * (gpro::TBL - 1)));
                const float v0 = valueAt (down.y), v1 = valueAt (p.y); const int a = juce::jmin (i0, i1), b = juce::jmax (i0, i1);
                for (int k = a; k <= b; ++k) f[(size_t) k] = b == a ? v1 : (i0 < i1 ? v0 + (v1 - v0) * (float) (k - a) / (float) (b - a) : v1 + (v0 - v1) * (float) (k - a) / (float) (b - a));
                repaint();
            }
            else drawPoint (phaseAt (p.x), valueAt (p.y), false);
            return;
        }
        if (barDrag) dragBar (p);
    }
    void mouseUp (const juce::MouseEvent&) override { if (drawing) endDraw(); barDrag = false; }
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (! k.getModifiers().isCommandDown()) return false;
        if (k.getKeyCode() == 'Z' || k.getKeyCode() == 'z') { if (k.getModifiers().isShiftDown()) redo(); else undo(); return true; }
        if (k.getKeyCode() == 'Y' || k.getKeyCode() == 'y') { redo(); return true; }
        return false;
    }
    bool isInterestedInFileDrag (const juce::StringArray& files) override { for (auto& f : files) if (juce::File (f).hasFileExtension ("wav;aif;aiff;flac;ogg;ghostywt")) return true; return false; }
    void filesDropped (const juce::StringArray& files, int, int) override
    {
        for (auto& s : files)
        {
            juce::File f (s);
            if (f.hasFileExtension ("ghostywt")) { gpro::wt::Doc d; if (gpro::wt::load (f, d)) { setDoc (d); status = "loaded " + f.getFileName(); repaint(); } else status = "not a Ghosty wavetable: " + f.getFileName(); return; }
            if (f.hasFileExtension ("wav;aif;aiff;flac;ogg")) { importFile (f, cycleBox.getText().getIntValue()); return; }
        }
    }

private:
    ProProcessor& proc; const int osc;
    gpro::wt::Doc doc; gpro::wt::Spectrum spec; int cur = 0; int before = (int) gpro::tSaw; juce::uint32 seed = 1;
    juce::String status { "drag on the big wave to draw; drag the bars to shape harmonics; SHIFT-click frames = key frames, then MORPH" };
    juce::File lastSample;
    bool dirty = false, drawing = false, barDrag = false; int sinceEdit = 0, lastI = -1; float lastV = 0; juce::Point<int> down; gpro::HiFrame strokeBase;
    std::vector<std::pair<gpro::wt::Doc, int>> undoStack, redoStack;
    juce::Rectangle<int> strip, waveArea, harmArea, phaseArea;
    std::unique_ptr<juce::FileChooser> chooser;
    std::unique_ptr<juce::AlertWindow> ask;

    juce::TextEditor nameBox, cycleBox;
    juce::ComboBox recipeA, recipeB;
    juce::Slider framesSl { juce::Slider::LinearHorizontal, juce::Slider::TextBoxLeft }, harmSl { juce::Slider::LinearHorizontal, juce::Slider::TextBoxLeft };
    juce::TextButton loadBtn { "LOAD" }, saveBtn { "SAVE" }, saveAsBtn { "SAVE AS" }, importBtn { "IMPORT" }, exportBtn { "EXPORT WAV" }, playBtn { "OSC PLAYS IT" }, liveBtn { "LIVE" };
    juce::TextButton undoBtn { "UNDO" }, redoBtn { "REDO" }, normBtn { "NORMALISE" }, dcBtn { "REMOVE DC" }, invBtn { "INVERT" }, revBtn { "REVERSE" }, smoothBtn { "SMOOTH" }, alignBtn { "ALIGN" };
    juce::TextButton keyBtn { "KEY FRAME" }, morphXBtn { "MORPH: CROSSFADE" }, morphSBtn { "MORPH: SPECTRAL" }, addBtn { "+ FRAME" }, delBtn { "- FRAME" }, clearKeysBtn { "CLEAR KEYS" };
    juce::TextButton genFrameBtn { "GEN FRAME" }, genTableBtn { "GEN TABLE" }, recutBtn { "RE-CUT" }, seedBtn { "START FROM OSC" };

    std::atomic<float>& tableOf() const { return *proc.apvts.getRawParameterValue (osc == 0 ? "o1_tbl" : "o2_tbl"); }
    void pushUndo() { undoStack.push_back ({ doc, cur }); redoStack.clear(); const size_t cap = doc.frames.size() > 64 ? 8 : 32; while (undoStack.size() > cap) undoStack.erase (undoStack.begin()); }
    void afterHistory() { nameBox.setText (doc.name, juce::dontSendNotification); spec = gpro::wt::analyze (doc.frames[(size_t) cur]); changed(); }
    void changed() { dirty = true; sinceEdit = 0; repaint(); }
    void resynth() { doc.frames[(size_t) cur] = gpro::wt::synth (spec); doc.made = true; changed(); }   // (harmonic / phase bars: made here)
    void push() { if (! liveBtn.getToggleState() || ! dirty) return; dirty = false; if (doc.valid()) proc.setUserTable (doc); }
    pt::Theme themeSeen = pt::pal().theme;
    void timerCallback() override
    {
        if (pt::pal().theme != themeSeen) { themeSeen = pt::pal().theme; sendLookAndFeelChange(); repaint(); }   // the main window's theme button: this window follows
        if (dirty && ++sinceEdit >= 2 && ! drawing && ! barDrag) push();
        undoBtn.setEnabled (! undoStack.empty()); redoBtn.setEnabled (! redoStack.empty());
        const bool plays = (int) tableOf() == gpro::tUser;
        if (plays != playBtn.getToggleState()) playBtn.setToggleState (plays, juce::dontSendNotification);
        keyBtn.setToggleState (isKey (cur), juce::dontSendNotification);
    }

    // ---- geometry
    int frameAt (int x) const { const int n = (int) doc.frames.size(); const int i = (int) ((float) (x - strip.getX()) / (float) strip.getWidth() * n); return i >= 0 && i < n ? i : -1; }
    float phaseAt (int x) const { return juce::jlimit (0.0f, 1.0f, (float) (x - waveArea.getX() - 8) / (float) (waveArea.getWidth() - 16)); }
    float valueAt (int y) const { return juce::jlimit (-1.0f, 1.0f, -(float) (y - waveArea.getCentreY()) / ((float) waveArea.getHeight() * 0.5f - 14.0f)); }
    void dragBar (juce::Point<int> p)
    {
        barDrag = true;
        const bool mag = harmArea.contains (p) || (! phaseArea.contains (p) && p.y < phaseArea.getY());
        const auto a = mag ? harmArea : phaseArea;
        const int h = 1 + juce::jlimit (0, kBars - 1, (int) ((float) (p.x - a.getX() - 6) / (float) (a.getWidth() - 12) * kBars));
        if (mag) { const float t = juce::jlimit (0.0f, 1.0f, (float) (a.getBottom() - 16 - p.y) / (float) (a.getHeight() - 36)); setHarmonic (h, t * t); }   // bar height = sqrt (mag): quiet partials stay visible
        else setPhase (h, juce::jlimit (-1.0f, 1.0f, -(float) (p.y - (a.getY() + 14 + (a.getHeight() - 30) * 0.5f)) / ((float) (a.getHeight() - 30) * 0.5f)) * juce::MathConstants<float>::pi);
    }

    // ---- painting
    void paintStrip (juce::Graphics& g)
    {
        pt::ScopedDark screen; const auto& c = pt::pal();
        auto r = strip.toFloat();
        if (progl::on (c.theme)) progl::screen (g, r, 8); else { g.setColour (c.deep); g.fillRoundedRectangle (r, 8); }    // GLASS (round 3)
        const int n = (int) doc.frames.size(); const float fw = r.getWidth() / (float) n;
        const auto acc = osc == 0 ? c.pink : c.ice;
        for (int i = 0; i < n; ++i)
        {
            juce::Rectangle<float> cell (r.getX() + i * fw, r.getY() + 12, fw, r.getHeight() - 16);
            if (i == cur) { g.setColour (acc.withAlpha (0.18f)); g.fillRoundedRectangle (cell.expanded (0, 2), 4); }
            const auto& f = doc.frames[(size_t) i];
            const int pts = juce::jlimit (4, 64, (int) fw);
            juce::Graphics::ScopedSaveState clip (g); g.reduceClipRegion (cell.expanded (0, 2).toNearestInt());
            juce::Path p;
            for (int k = 0; k <= pts; ++k)
            {
                const float v = f[(size_t) juce::jmin (gpro::TBL - 1, (int) ((float) k / pts * (gpro::TBL - 1)))];
                const float x = cell.getX() + 1 + (float) k / pts * (cell.getWidth() - 2), y = cell.getCentreY() - v * cell.getHeight() * 0.42f;
                if (k == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            g.setColour (i == cur ? acc : c.slime.withAlpha (0.55f)); g.strokePath (p, juce::PathStrokeType (i == cur ? 1.6f : 1.0f));
            if (isKey (i))
            {   // a key frame: a diamond above the cell
                juce::Path d; const float cx = cell.getCentreX(), cy = r.getY() + 6; d.addQuadrilateral (cx, cy - 4, cx + 4, cy, cx, cy + 4, cx - 4, cy);
                g.setColour (c.gold); g.fillPath (d);
            }
        }
        g.setColour (c.dim); g.setFont (ghosty::font (9.0f));
        g.drawText ("FRAME " + juce::String (cur + 1) + " / " + juce::String (n) + (doc.cycleLen > 0 ? "   cycle " + juce::String (doc.cycleLen) : juce::String()) + (doc.keys.empty() ? juce::String() : "   " + juce::String ((int) doc.keys.size()) + " key frames (diamonds)"),
                    strip.withTrimmedLeft (8).removeFromTop (12), juce::Justification::centredLeft);
    }
    void paintWave (juce::Graphics& g)
    {
        pt::ScopedDark screen; const auto& c = pt::pal();
        auto r = waveArea.toFloat();
        if (progl::on (c.theme)) progl::screen (g, r, 10); else { g.setColour (c.deep); g.fillRoundedRectangle (r, 10); }    // GLASS (round 3)
        const auto a = r.reduced (8, 14);
        g.setColour (c.white.withAlpha (0.07f));
        for (int i = 0; i <= 16; ++i) { const float x = a.getX() + i / 16.0f * a.getWidth(); g.drawLine (x, a.getY(), x, a.getBottom()); }
        for (int i = 0; i <= 8; ++i) { const float y = a.getY() + i / 8.0f * a.getHeight(); g.drawLine (a.getX(), y, a.getRight(), y); }
        g.setColour (c.white.withAlpha (0.25f)); g.drawLine (a.getX(), a.getCentreY(), a.getRight(), a.getCentreY());
        const auto acc = osc == 0 ? c.pink : c.ice;
        // the neighbours faint (how the table moves), the current frame bright and filled
        auto pathOf = [&] (const gpro::HiFrame& f)
        {
            juce::Path p;
            for (int i = 0; i <= 512; ++i) { const float v = f[(size_t) juce::jmin (gpro::TBL - 1, i * 4)]; const float x = a.getX() + (float) i / 512 * a.getWidth(), y = a.getCentreY() - v * a.getHeight() * 0.5f; if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y); }
            return p;
        };
        juce::Graphics::ScopedSaveState clip (g); g.reduceClipRegion (r.reduced (2).toNearestInt());      // (a frame over 1 is drawn cut off, not over the buttons)
        if (cur > 0) { g.setColour (c.slime.withAlpha (0.28f)); g.strokePath (pathOf (doc.frames[(size_t) cur - 1]), juce::PathStrokeType (1.2f)); }
        if (cur + 1 < (int) doc.frames.size()) { g.setColour (c.ecto.withAlpha (0.28f)); g.strokePath (pathOf (doc.frames[(size_t) cur + 1]), juce::PathStrokeType (1.2f)); }
        auto mp = pathOf (doc.frames[(size_t) cur]);
        auto fill = mp; fill.lineTo (a.getRight(), a.getCentreY()); fill.lineTo (a.getX(), a.getCentreY()); fill.closeSubPath();
        g.setColour (acc.withAlpha (0.16f)); g.fillPath (fill);
        g.setColour (acc.withAlpha (0.25f)); g.strokePath (mp, juce::PathStrokeType (6.0f));
        g.setColour (acc); g.strokePath (mp, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (c.dim); g.setFont (ghosty::font (9.0f));
        g.drawText ("FRAME " + juce::String (cur + 1) + "  -  drag to draw, SHIFT = straight line  (faint: the frame before / after)", r.getX() + 10, r.getY() + 2, r.getWidth() - 20, 12, juce::Justification::centredLeft);
        float pk = 0; for (auto v : doc.frames[(size_t) cur]) pk = std::max (pk, std::abs (v));
        g.drawText ("peak " + juce::String (pk, 2), r.getX() + 10, r.getBottom() - 14, r.getWidth() - 20, 12, juce::Justification::centredRight);
    }
    void paintBars (juce::Graphics& g)
    {
        pt::ScopedDark screen; const auto& c = pt::pal();
        for (int pass = 0; pass < 2; ++pass)
        {
            const bool mag = pass == 0; auto r = (mag ? harmArea : phaseArea).toFloat();
            if (progl::on (c.theme)) progl::screen (g, r, 10); else { g.setColour (c.deep); g.fillRoundedRectangle (r, 10); }    // GLASS (round 3)
            const auto a = r.reduced (6, 0).withTrimmedTop (14).withTrimmedBottom (16);
            const float bw = a.getWidth() / kBars;
            float mx = 1.0e-6f; for (int h = 1; h <= kBars; ++h) mx = std::max (mx, spec.mag[(size_t) h]);
            for (int h = 1; h <= kBars; ++h)
            {
                const float x = a.getX() + (h - 1) * bw;
                if (mag)
                {
                    const float v = std::sqrt (juce::jlimit (0.0f, 1.0f, spec.mag[(size_t) h]));       // bar height = sqrt (magnitude)
                    const float hh = v * a.getHeight();
                    g.setColour ((h & 1) ? c.slime : c.ecto); g.fillRect (x + 1, a.getBottom() - hh, juce::jmax (1.0f, bw - 2), hh);
                }
                else
                {
                    const float v = spec.phase[(size_t) h] / juce::MathConstants<float>::pi;              // -1 .. 1 around the middle
                    const float cy = a.getCentreY(), hh = -v * a.getHeight() * 0.5f;
                    const bool quiet = spec.mag[(size_t) h] < 0.02f * mx;
                    g.setColour ((quiet ? c.dim : c.gold).withAlpha (quiet ? 0.35f : 0.9f)); g.fillRect (x + 1, juce::jmin (cy, cy + hh), juce::jmax (1.0f, bw - 2), std::abs (hh) + 1.0f);
                }
            }
            if (! mag) { g.setColour (c.white.withAlpha (0.25f)); g.drawLine (a.getX(), a.getCentreY(), a.getRight(), a.getCentreY()); }
            g.setColour (c.dim); g.setFont (ghosty::font (9.0f));
            g.drawText (mag ? "HARMONICS 1 .. 64  -  drag = level" : "PHASES  -  drag = -180 .. 180 deg  (grey = quiet)", r.getX() + 8, r.getY() + 2, r.getWidth() - 16, 12, juce::Justification::centredLeft);
            for (int h : { 1, 8, 16, 24, 32, 48, 64 }) g.drawText (juce::String (h), a.getX() + (h - 1) * bw - 10, r.getBottom() - 14, bw + 20, 12, juce::Justification::centred);
        }
    }

    // ---- files
    void loadMenu()
    {
        juce::PopupMenu m;
        m.addSectionHeader ("USER BANK  (" + gpro::wt::userBankDir().getFullPathName() + ")");
        auto files = gpro::wt::listUserTables();
        int id = 100; for (auto& f : files) m.addItem (id++, f.getFileNameWithoutExtension());
        if (files.isEmpty()) m.addItem (1, "(no saved tables yet: SAVE puts one here)", false);
        m.addSeparator();
        m.addItem (2, "The sound's User table", proc.userTable().valid());
        m.addItem (3, "What OSC " + juce::String (osc + 1) + " plays now");
        m.addItem (4, "Import an audio file...");
        juce::Component::SafePointer<WavetableEditor> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&loadBtn), [safe, files] (int r)
        {
            if (safe == nullptr || r == 0) return;
            auto& w = *safe;
            if (r >= 100) { gpro::wt::Doc d; const auto f = files[r - 100]; if (gpro::wt::load (f, d)) { w.setDoc (d); w.cur = 0; w.selectFrame (0); w.status = "loaded " + f.getFileName(); } else w.status = "could not read " + f.getFileName(); }
            else if (r == 2) { w.setDoc (w.proc.userTable()); w.status = "the sound's User table"; }
            else if (r == 3) w.seedFromOsc();
            else if (r == 4) w.chooseImport();
            w.repaint();
        });
    }
    // demo mode: a .ghostywt holds the frames as raw float32 PCM (base64 in JSON), which any script turns into a WAV, so saving
    // one is an export like EXPORT WAV (the table still plays, LIVE, in the sound)
    void save (bool as)
    {
        if (! ghosty::exportAllowed (proc.license, as ? (juce::Component*) &saveAsBtn : (juce::Component*) &saveBtn, as ? "pro.wt.save-as" : "pro.wt.save"))
        { status = "Saving tables is part of the full version (UNLOCK)"; repaint(); return; }
        if (doc.name.isEmpty()) doc.name = "User";
        if (! as) { saveTo (gpro::wt::userBankDir().getChildFile (juce::File::createLegalFileName (doc.name) + gpro::wt::fileExtension())); return; }
       #if GHOSTY_TEST_HOOKS   // tests answer the name box with testAnswerSaveAs (no window: xvfb has no window manager)
        askingForName = true;
       #else
        ask = std::make_unique<juce::AlertWindow> ("SAVE AS", "A name for the table (it goes into the user bank)", juce::MessageBoxIconType::NoIcon, this);
        ask->addTextEditor ("name", doc.name); ask->addButton ("SAVE", 1); ask->addButton ("CANCEL", 0);
        juce::Component::SafePointer<WavetableEditor> safe (this);
        ask->enterModalState (true, juce::ModalCallbackFunction::create ([safe] (int r)
        {
            if (safe == nullptr) return;
            auto& w = *safe;
            if (r == 1 && w.ask != nullptr) w.saveAsNamed (w.ask->getTextEditorContents ("name").trim());
            w.ask.reset();
        }), false);
       #endif
    }
    void saveAsNamed (const juce::String& nm)
    {   // (the gate was asked when SAVE AS was pressed; the licence is checked again silently)
        if (nm.isEmpty() || ! ghosty::exportUnlocked (proc.license)) return;
        doc.name = nm; nameBox.setText (nm, juce::dontSendNotification);
        saveTo (gpro::wt::userBankDir().getChildFile (juce::File::createLegalFileName (nm) + gpro::wt::fileExtension()));
    }
    void chooseImport()
    {
        chooser = std::make_unique<juce::FileChooser> ("Import a sample or a wavetable WAV", juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.ghostywt");
        juce::Component::SafePointer<WavetableEditor> safe (this);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [safe] (const juce::FileChooser& fc)
        {
            if (safe == nullptr) return;
            auto f = fc.getResult(); if (! f.existsAsFile()) return;
            if (f.hasFileExtension ("ghostywt")) { gpro::wt::Doc d; if (gpro::wt::load (f, d)) { safe->setDoc (d); safe->status = "loaded " + f.getFileName(); safe->repaint(); } return; }
            safe->importFile (f, safe->cycleBox.getText().getIntValue());
        });
    }
    void chooseExport()
    {
        if (! ghosty::exportAllowed (proc.license, &exportBtn, "pro.wt.export-wav")) { status = "EXPORT WAV is part of the full version (UNLOCK)"; repaint(); return; }
        juce::Component::SafePointer<WavetableEditor> safe (this);
        ghosty::exportgate::saveDialog (chooser, "Export the table as a float32 WAV", gpro::wt::userBankDir().getChildFile (juce::File::createLegalFileName (doc.name) + ".wav"), "*.wav", [safe] (const juce::File& picked)
        {
            if (safe == nullptr || ! ghosty::exportUnlocked (safe->proc.license)) return;
            auto f = picked;
            if (! f.hasFileExtension ("wav")) f = f.withFileExtension ("wav");
            safe->status = gpro::wt::exportWav (safe->doc, f) ? "exported " + f.getFileName() : "could not write " + f.getFullPathName(); safe->repaint();
        }, true);
    }
   #if GHOSTY_TEST_HOOKS
public:
    // tests: press SAVE / SAVE AS / EXPORT WAV as a user would, and answer the SAVE AS name box
    void testPress (const juce::String& which) { (which == "save" ? saveBtn : which == "saveas" ? saveAsBtn : exportBtn).triggerClick(); }
    bool testAnswerSaveAs (const juce::String& name) { if (! askingForName) return false; askingForName = false; saveAsNamed (name.trim()); return true; }
    bool askingForName = false;
private:
   #endif
};
