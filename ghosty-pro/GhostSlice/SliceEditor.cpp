#include "SliceEditor.h"
#include "Chords.h"

using namespace gslice;
using namespace gslice::ui;

SliceEditor::Field::Field (const juce::String& name, double lo, double hi, double step, const juce::String& suffix)
{
    cap.setText (name, juce::dontSendNotification); cap.setFont (LnF::uiFont (11.5f)); cap.setJustificationType (juce::Justification::centredLeft);
    s.setRange (lo, hi, step); s.setTextValueSuffix (suffix); s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 20);
    s.setDoubleClickReturnValue (true, juce::jlimit (lo, hi, 0.0));
    addAndMakeVisible (cap); addAndMakeVisible (s);
    setComponentID (name);
}

static const char* kTabNames[4] { "EDIT", "FLEX", "PITCH", "PLAY" };

SliceEditor::SliceEditor (SliceProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (view); addAndMakeVisible (lane);
    view.setComponentID ("view"); lane.setComponentID ("lane");
    view.toast = [this] (const juce::String& s) { toast (s); };
    view.selectionChanged = [this] { refresh(); lane.repaint(); };
    for (int i = 0; i < 4; ++i)
    {
        auto& t = tabs[i]; t.setButtonText (kTabNames[i]); t.setClickingTogglesState (false); t.setComponentID (juce::String ("tab.") + kTabNames[i]);
        t.setTooltip (i < 4 ? juce::String (kTabNames[i]) + " workspace (key " + juce::String (i + 1) + ")" : juce::String (kTabNames[i]) + " workspace: coming in a later phase");
        t.onClick = [this, i] { setWorkspace ((Workspace) i); };
        addAndMakeVisible (t);
    }
    for (auto* l : { &bpmLbl, &keyLbl, &gridLbl }) { l->setFont (LnF::uiFont (14.0f)); l->setJustificationType (juce::Justification::centredLeft); addAndMakeVisible (*l); }
    bpmLbl.setComponentID ("bpm"); keyLbl.setComponentID ("key"); gridLbl.setComponentID ("grid");
    bpmLbl.setEditable (false, true); bpmLbl.setTooltip ("Project tempo: double-click to type it (or TAP BPM / USE BPM in FLEX). Under it: SET, or the detected tempo of the audio");
    bpmLbl.onTextChange = [this] { if (auto q = ops::setTempo (proc.project(), bpmLbl.getText().retainCharacters ("0123456789.").getDoubleValue())) proc.apply (*q, "Tempo"); refresh(); };
    keyLbl.setTooltip ("The source's key: detected from its notes (with the confidence under it), or SET by hand in PITCH");
    gridLbl.setTooltip ("The snap grid (set it with SNAP below)");

    auto add = [this] (juce::Component& c, const juce::String& id, const juce::String& tip = {})
    {
        c.setComponentID (id); addAndMakeVisible (c);
        if (auto* s = dynamic_cast<juce::SettableTooltipClient*> (&c); s != nullptr && tip.isNotEmpty()) s->setTooltip (tip);
    };
    add (playBtn, "play", "Play / stop (SPACE). Plays from the cursor; right-click the waveform to move the cursor.");
    add (loopBtn, "loop", "Loop the selection (L). Right-drag on the waveform or drag in the ruler to set a loop.");
    add (loadBtn, "load", "Load a WAV / AIFF / FLAC / MP3 (or drop it on the waveform)");
    add (projBtn, "project", "Open / save a Ghost Slice project (.ghostslice)");
    add (relinkBtn, "relink", "The project's audio was moved: pick the original file");
    add (themeBtn, "theme", "The look: BLACK / PURPLE / LIGHT / GHOST (liquid glass). Click for the next one");
    add (syncBtn, "sync"); add (unlock, "unlock");
    playBtn.onClick = [this] { proc.play (! proc.isPlaying()); refresh(); };
    loopBtn.onClick = [this]
    {
        if (view.sel.hasRange()) proc.setLoop (view.sel.a, view.sel.b, ! (proc.looping() && proc.loopRange() == std::make_pair (view.sel.a, view.sel.b)));
        else { const auto [a, b] = proc.loopRange(); proc.setLoop (a, b, ! proc.looping() && b > a); if (b <= a) toast ("Select a range first (or right-drag on the waveform)"); }
        refresh(); view.repaint();
    };
    loadBtn.onClick = [this] { loadClicked(); };
    projBtn.onClick = [this] { projectMenu(); };
    relinkBtn.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Relink " + proc.project().source.fileName, juce::File(), source::wildcard());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult(); if (f == juce::File()) return;
            juce::String err; if (! proc.relinkAudio (f, err)) toast (err); else { toast ("Relinked " + f.getFileName()); view.zoomToFit(); }
            refresh();
        });
    };
    themeBtn.onClick = [this] { setLook (nextLook (shownLook)); };
    syncBtn.onModeChanged = [this] { refresh(); };
    unlock.onChange = [this] { proc.unlockedNow(); refresh(); };

    add (cutBtn, "cut", "CUT at the cursor, or at both edges of the selection (S)");
    add (splitMkBtn, "splitmk", "Split the selected region at every warp marker");
    add (delBtn, "delete", "Delete the selected range (silence), the region, or in FLEX the marker (Delete)");
    add (dupBtn, "duplicate", "Duplicate the region right after itself (Cmd/Ctrl+D)");
    add (revBtn, "reverse", "Reverse the region (R)");
    add (undoBtn, "undo", "Undo (Cmd/Ctrl+Z)"); add (redoBtn, "redo", "Redo (Cmd/Ctrl+Shift+Z)");
    add (snapBox, "snap", "Snap to the grid");
    add (tripBtn, "triplet", "Triplet grid");
    add (abBtn, "ab", "A/B: hear the original audio (B) against your edit (A)");
    add (qualityBtn, "quality", "LIVE: stretch in real time. STUDIO: high-quality renders replace LIVE in the background (at safe points)");
    add (exportWavBtn, "exportwav", "Export the arrangement as a WAV (STUDIO render)");
    add (exportMidiBtn, "exportmidi", "Export the slice positions as MIDI (one note per slice from C1)");
    cutBtn.onClick = [this] { view.cutAtCursorOrSelection(); refresh(); };
    splitMkBtn.onClick = [this] { view.splitAtMarkers(); refresh(); };
    delBtn.onClick = [this] { view.deleteSelection(); refresh(); };
    dupBtn.onClick = [this] { view.duplicateSelection(); refresh(); };
    revBtn.onClick = [this] { view.reverseSelection(); refresh(); };
    undoBtn.onClick = [this] { proc.undo(); refresh(); };
    redoBtn.onClick = [this] { proc.redo(); refresh(); };
    snapBox.addItemList ({ "SNAP OFF", "1/4", "1/8", "1/16", "1/32", "1/64" }, 1);
    snapBox.onChange = [this]
    {
        if (refreshing) return;
        static const int divs[] { 0, 4, 8, 16, 32, 64 };
        Project q = proc.project(); q.gridDiv = divs[juce::jlimit (0, 5, snapBox.getSelectedItemIndex())]; proc.apply (q, "Grid");
        refresh();
    };
    tripBtn.onClick = [this] { Project q = proc.project(); q.gridTriplet = ! q.gridTriplet; proc.apply (q, "Triplet grid"); refresh(); };
    abBtn.onClick = [this] { proc.setCompareOriginal (! proc.compareOriginal()); refresh(); };
    qualityBtn.onClick = [this] { proc.setStudioMode (! proc.studioMode()); refresh(); };
    exportWavBtn.onClick = [this] { exportWavClicked(); };
    exportMidiBtn.onClick = [this] { exportMidiClicked(); };

    for (auto* f : { &gainF, &fadeInF, &fadeOutF, &pitchF, &centsF, &formantF, &snapStrF })
    {
        addAndMakeVisible (*f);
        f->s.onDragStart = [this] { proc.beginGesture(); };
        f->s.onDragEnd = [this] { proc.endGesture(); };
        f->s.onValueChange = [this, f] { sliderToProject (*f); };
    }
    gainF.s.setTooltip ("The selected region's gain"); fadeInF.s.setTooltip ("Fade in (or drag the square at the region's top left)");
    fadeOutF.s.setTooltip ("Fade out (or drag the square at the region's top right)");
    pitchF.s.setTooltip ("Global PITCH (semitones) - the whole project, formants kept"); centsF.s.setTooltip ("Global fine pitch (cents)");
    formantF.s.setTooltip ("Global FORMANT shift (0 = keep the voice's own formants)");
    snapStrF.s.setTooltip ("How hard a dragged marker snaps to the grid (Cmd/Ctrl = full, Alt = none)");
    snapStrF.s.setValue (100.0, juce::dontSendNotification);

    add (moveBtn, "mode.move", "MOVE: drag a marker, its neighbours stretch, the length stays");
    add (stretchBtn, "mode.stretch", "STRETCH: drag a marker, the part before it stretches and everything after moves with it");
    add (slipBtn, "mode.slip", "SLIP: drag inside the region, the audio slides under fixed markers");
    add (addMkBtn, "addmarker", "Add a warp marker at the cursor (or double-click on the waveform)");
    add (remMkBtn, "removemarker", "Remove the selected marker (Delete)");
    add (anchorBtn, "anchor", "ANCHOR LOCK: the selected marker never moves (A)");
    add (tapBtn, "tapbpm", "TAP BPM: set the project tempo from the selected marker and the next one (the beats between them land in 70-180 BPM; the grid starts at the first)");
    add (lockBtn, "tlock", "Transient lock: attacks play at 1x inside stretched parts");
    addAndMakeVisible (snapStrF);
    auto mode = [this] (FlexMode m) { view.flexMode = m; refresh(); };
    moveBtn.onClick = [mode] { mode (FlexMode::Move); };
    stretchBtn.onClick = [mode] { mode (FlexMode::Stretch); };
    slipBtn.onClick = [mode] { mode (FlexMode::Slip); };
    addMkBtn.onClick = [this] { view.addMarkerAtCursor(); refresh(); };
    remMkBtn.onClick = [this] { view.removeSelectedMarker(); refresh(); };
    anchorBtn.onClick = [this] { view.toggleAnchor(); refresh(); };
    tapBtn.onClick = [this] { view.bpmFromMarkers(); refresh(); };
    lockBtn.onClick = [this] { Project q = proc.project(); q.transientLock = ! q.transientLock; proc.apply (q, "Transient lock"); refresh(); };

    // GHOST ALIGN row
    add (materialBox, "material", "Material: how the audio is analysed (onsets, syllables). AUTO picks DRUMS / VOCAL / MELODIC");
    materialBox.addItemList ({ "AUTO", "DRUMS", "VOCAL", "MELODIC" }, 1);
    materialBox.onChange = [this]
    {
        if (refreshing) return;
        Project q = proc.project(); q.material = analysis::materialName ((analysis::Material) juce::jlimit (0, 3, materialBox.getSelectedItemIndex()));
        proc.apply (q, "Material " + q.material); view.alignPreview = nullptr; refresh();
    };
    add (alignBtn, "autoalign", "AUTO ALIGN: propose moving the detected events to the grid (amber ghosts). APPLY keeps them");
    add (alignModeBox, "alignmode", "TIGHT: onto the grid. GROOVE: the grid + the groove of the strong hits (keeps a shared swing / push). MANUAL: no automatic moves");
    alignModeBox.addItemList ({ "TIGHT", "GROOVE", "MANUAL" }, 1); alignModeBox.setSelectedItemIndex (0, juce::dontSendNotification);
    alignModeBox.onChange = [this] { if (! refreshing && view.alignPreview != nullptr) proposeAlign (false); refresh(); };
    add (applyAlignBtn, "applyalign", "APPLY the alignment as AUTO markers (one undo step). Your own markers and anchors never move");
    add (clearAutoBtn, "clearauto", "Remove the AUTO markers (your own markers stay)");
    add (useBpmBtn, "usebpm", "USE BPM: the detected tempo and downbeat become the project tempo and grid");
    alignBtn.onClick = [this] { proposeAlign(); refresh(); };
    applyAlignBtn.onClick = [this] { applyAlign(); refresh(); };
    clearAutoBtn.onClick = [this]
    {
        const auto q = align::withoutAuto (proc.project());
        if (! proc.apply (q, "Clear auto markers")) toast ("No AUTO markers"); else toast ("AUTO markers removed");
        view.alignPreview = nullptr; refresh();
    };
    useBpmBtn.onClick = [this] { useDetectedBpm(); refresh(); };
    add (f0Btn, "f0curve", "Show the detected pitch curve over the note blobs");
    add (chordsBtn, "chords", "CHORDS: show the chord lane (read-only: the chords detected from the audio in the background, as heard after PITCH). "
                              "'?' = the root only, N.C. = no chord. KEY MODE USE CHORDS matches the key by them");
    chordsBtn.onClick = [this] { view.showChords = ! view.showChords; view.repaint(); refresh(); };
    add (srcKeyBox, "srckey", "The source's key: DETECTED (from its notes) or set by hand");
    add (projKeyBox, "projkey", "The project / song key that MATCH KEY moves the source to");
    add (matchKeyBtn, "matchkey", "MATCH KEY: shift the whole source (global PITCH, chromatic) by the smallest step into the project key");
    {
        juce::StringArray keys; for (int m = 0; m < 2; ++m) for (int k = 0; k < 12; ++k) keys.add (analysis::pitchClassName (k) + (m ? " MIN" : " MAJ"));
        srcKeyBox.addItem ("SOURCE: DETECTED", 1); projKeyBox.addItem ("PROJECT: --", 1);
        srcKeyBox.addItemList (keys, 2); projKeyBox.addItemList (keys, 2);
    }
    auto keyFromBox = [] (const juce::ComboBox& b) { return b.getSelectedId() <= 1 ? juce::String ("--") : b.getItemText (b.getSelectedItemIndex()); };
    srcKeyBox.onChange = [this, keyFromBox] { if (refreshing) return; Project q = proc.project(); q.key = keyFromBox (srcKeyBox); proc.apply (q, "Source key"); refresh(); };
    projKeyBox.onChange = [this, keyFromBox] { if (refreshing) return; Project q = proc.project(); q.projectKey = keyFromBox (projKeyBox); proc.apply (q, "Project key"); refresh(); };
    matchKeyBtn.onClick = [this] { matchProjectKey(); refresh(); };
    add (keyModeBox, "keymode", "KEY MODE for MATCH KEY - CHROMATIC: shift the whole source into the key. SCALE SNAP: snap every note to the key's scale. "
                                "MELODY PRESERVE: move the melody tonic to tonic, intervals kept exactly. MANUAL: edit the notes yourself. "
                                "USE CHORDS: shift by the detected chords' roots onto the key's chords (for harmony: pads, keys, guitar)");
    for (int i = 0; i < keyops::kModes; ++i) keyModeBox.addItem (keyops::modeName ((keyops::Mode) i), i + 1);
    keyModeBox.onChange = [this] { if (refreshing) return; Project q = proc.project(); q.keyMode = keyModeBox.getSelectedId() - 1; proc.apply (q, "Key mode"); refresh(); };
    keyLbl.addMouseListener (this, false);
    keyLbl.setTooltip ("The source's key (detected with its confidence, or SET by hand). Click: pick the target key and the KEY MODE for MATCH KEY");
    addAndMakeVisible (tonalityF);
    tonalityF.s.setSkewFactorFromMidPoint (6000.0); tonalityF.s.setDoubleClickReturnValue (true, 8000.0); tonalityF.capW = 66;
    tonalityF.s.onDragStart = [this] { proc.beginGesture(); }; tonalityF.s.onDragEnd = [this] { proc.endGesture(); };
    tonalityF.s.onValueChange = [this] { sliderToProject (tonalityF); };
    tonalityF.s.setTooltip ("TONALITY: above this frequency the PITCH shift stops keeping tonal (harmonic) detail - lower for noisy / breathy sources");
    f0Btn.onClick = [this] { view.showF0 = ! view.showF0; view.repaint(); refresh(); };
    for (auto* f : { &strengthF, &confF, &feelF })
    {
        addAndMakeVisible (*f); f->s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 48, 20);
        f->s.onValueChange = [this, f] { sliderToProject (*f); };
    }
    strengthF.capW = 64; confF.capW = 40; feelF.capW = 36;
    strengthF.s.setValue (100.0, juce::dontSendNotification); confF.s.setValue (45.0, juce::dontSendNotification); feelF.s.setValue (100.0, juce::dontSendNotification);
    strengthF.s.setDoubleClickReturnValue (true, 100.0); confF.s.setDoubleClickReturnValue (true, 45.0); feelF.s.setDoubleClickReturnValue (true, 100.0);
    strengthF.s.setTooltip ("ALIGN STRENGTH: how far the events move toward their targets (50 % = half way)");
    confF.s.setTooltip ("Confidence threshold: weaker events (and breaths) stay where they are");
    feelF.s.setTooltip ("GROOVE: how much of the extracted groove the targets keep");

    // PITCH note editing (phase 14)
    for (auto* f : noteFields())
    {
        addAndMakeVisible (*f); f->capW = 58; f->s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 54, 20);
        f->setComponentID ("note." + f->cap.getText());
        f->s.onDragStart = [this] { proc.beginGesture(); }; f->s.onDragEnd = [this] { proc.endGesture(); };
        f->s.onValueChange = [this, f] { sliderToProject (*f); };
    }
    driftF.s.setDoubleClickReturnValue (true, 100.0); vibF.s.setDoubleClickReturnValue (true, 100.0); transF.s.setDoubleClickReturnValue (true, 40.0);
    fineF.s.setTooltip ("FINE: the selected notes' cents off the semitone (drag a blob with Shift for free cents)");
    nFormF.s.setTooltip ("The selected notes' own FORMANT shift (0 = the voice as sung, at any pitch)");
    driftF.s.setTooltip ("DRIFT: the slow pitch wander inside the notes (0 % = flat, 100 % = as sung, 200 % = exaggerated)");
    vibF.s.setTooltip ("VIBRATO: scale the detected vibrato (0 % = none, 100 % = as sung, 200 % = double)");
    transF.s.setTooltip ("TRANSITION: how long the pitch takes to move into the selected notes (ms)");
    add (glideBtn, "note.glide", "GLIDE: the transition into the selected notes is a straight line between the targets (off: the sung glide shape stays)");
    add (splitNoteBtn, "note.split", "SPLIT the selected note at the cursor (or its middle). Double-click a blob to split it there");
    add (mergeBtn, "note.merge", "MERGE the selected neighbouring notes into one");
    add (resetNoteBtn, "note.reset", "RESET the selected notes to how they were sung (pitch, formant, drift, vibrato, drawn curves)");
    add (drawBtn, "note.draw", "PITCH DRAW: drag on the piano roll to draw the pitch (one undo step per stroke)");
    add (snapSelBtn, "note.snap", "SNAP the selected notes to the SCALE");
    add (snapAllBtn, "note.snapall", "SNAP ALL notes to the SCALE (notes under 50 % confidence stay as they are)");
    add (scaleBox, "note.scale", "The scale SNAP uses: CHROMATIC or a key (the source key by default)");
    scaleBox.addItem ("SCALE: SOURCE KEY", 1); scaleBox.addItem ("CHROMATIC", 2);
    { int id = 3; for (int m = 0; m < 2; ++m) for (int k = 0; k < 12; ++k) scaleBox.addItem (analysis::pitchClassName (k) + (m ? " MIN" : " MAJ"), id++); }
    scaleBox.setSelectedId (1, juce::dontSendNotification);
    add (engineBox, "note.engine", "ENGINE for the note edits (the selected notes, or the project when none is selected). VOCAL: our pitch-synchronous engine "
                                   "(PSOLA) - the voice's formants stay, vibrato / drift edits are followed period by period, up to +-12 st. WIDE: the stretcher "
                                   "+ formant correction, any shift. AUTO: VOCAL on voiced notes within +-4 st, WIDE beyond. (LIVE previews with WIDE; STUDIO renders the choice)");
    engineBox.addItem ("ENGINE: AUTO", 1); engineBox.addItem ("ENGINE: VOCAL", 2); engineBox.addItem ("ENGINE: WIDE", 3);
    engineBox.setSelectedId (1, juce::dontSendNotification);
    engineBox.onChange = [this]
    {
        if (refreshing) return;
        const int e = juce::jlimit (0, 2, engineBox.getSelectedId() - 1);
        if (! view.selectedNotes().empty()) view.editSelected ([e] (PitchEdit& x) { x.engine = e; }, "Note engine");
        else { Project q = proc.project(); q.pitchEngine = e; proc.apply (q, "Pitch engine"); }
        toast ("ENGINE: " + pitchedit::engineName (e) + (view.selectedNotes().empty() ? juce::String (" for the project's note edits") : juce::String (" for the selected notes")));
        refresh();
    };
    glideBtn.onClick = [this] { const bool on = ! glideBtn.getToggleState(); view.editSelected ([on] (PitchEdit& e) { e.glide = on; }, on ? "Glide on" : "Glide off"); refresh(); };
    splitNoteBtn.onClick = [this] { view.splitSelected(); refresh(); };
    mergeBtn.onClick = [this] { view.mergeSelected(); refresh(); };
    resetNoteBtn.onClick = [this] { view.resetSelected(); refresh(); };
    drawBtn.onClick = [this] { view.pitchDraw = ! view.pitchDraw; if (view.pitchDraw) toast ("PITCH DRAW: drag over the notes to draw where the pitch goes"); view.repaint(); refresh(); };
    snapSelBtn.onClick = [this] { view.snapSelected (chosenScale(), false); refresh(); };
    snapAllBtn.onClick = [this] { view.snapSelected (chosenScale(), true); refresh(); };

    // GHOST CHOP (EDIT row 3)
    add (chopBox, "chop.mode", "CHOP: where the slices come from - REGIONS, EQUAL parts, MANUAL cuts (in PLAY), TRANSIENT (attacks), SYLLABLE (sung / spoken syllables), NOTE (pitched notes), BEAT (the tempo grid)");
    for (int i = 0; i < kSliceModes; ++i) chopBox.addItem (juce::String ("CHOP: ") + play::modeName ((SliceMode) i), i + 1);
    chopBox.onChange = [this] { if (! refreshing) chopChanged ([this] (PlayMap& m) { m.mode = (SliceMode) (chopBox.getSelectedId() - 1); }, "Slice mode"); };
    add (chopDivBox, "chop.div", "BEAT: a cut every 1/N note of the project tempo");
    for (int d : { 4, 8, 16, 32 }) chopDivBox.addItem ("1/" + juce::String (d), d);
    chopDivBox.onChange = [this] { if (! refreshing) chopChanged ([this] (PlayMap& m) { m.beatDiv = chopDivBox.getSelectedId(); }, "Beat division"); };
    for (auto* f : { &chopSensF, &chopPartsF })
    {
        addAndMakeVisible (*f); f->capW = 46; f->s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 20); f->setComponentID ("chop." + f->cap.getText());
        f->s.onDragStart = [this] { proc.beginGesture(); }; f->s.onDragEnd = [this] { proc.endGesture(); };
    }
    chopSensF.s.onValueChange = [this] { if (! refreshing) chopChanged ([this] (PlayMap& m) { m.sensitivity = chopSensF.s.getValue() / 100.0; }, "Chop sensitivity"); };
    chopPartsF.s.onValueChange = [this] { if (! refreshing) chopChanged ([this] (PlayMap& m) { m.equalN = (int) chopPartsF.s.getValue(); }, "Equal parts"); };
    chopSensF.s.setDoubleClickReturnValue (true, 50.0); chopPartsF.s.setDoubleClickReturnValue (true, 16.0);
    chopSensF.s.setTooltip ("SENSITIVITY: more = more cuts (weaker attacks / syllables / notes count too)");
    chopPartsF.s.setTooltip ("EQUAL: how many parts");
    add (splitSlicesBtn, "chop.split", "SPLIT @ SLICES: cut the arrangement into regions at the dashed slice points; then drag the blocks in the lane strip to rearrange (words, hits, notes)");
    splitSlicesBtn.onClick = [this] { playPanel.splitAtSlices(); refresh(); view.repaint(); lane.repaint(); };
    add (chopInfo, "chop.info"); chopInfo.setFont (LnF::uiFont (11.5f, false)); chopInfo.setJustificationType (juce::Justification::centredLeft);
    chopInfo.setTooltip ("How many slices the CHOP mode makes");

    add (helpBtn, "help", "Keyboard shortcuts and gestures (?)");
    helpBtn.onClick = [this] { showHelp (! help.isVisible()); };
    addChildComponent (help); help.setComponentID ("helpoverlay");

    addChildComponent (playPanel); playPanel.setComponentID ("playpanel");
    view.addAndMakeVisible (overlay); overlay.setComponentID ("overlay");
    overlay.toast = [this] (const juce::String& s) { toast (s); };
    overlay.onSelectRepeat = [this] (int64 id) { showRepeat (id); };
    addChildComponent (repeatStrip);
    repeatStrip.onClose = [this] { overlay.selRepeat = 0; repeatStrip.setVisible (false); layoutStrips(); refresh(); };
    add (repeatBtn, "repeatbtn", "GHOST REPEAT: retrigger the selected range (1/4 .. 1/128, triplet / dotted, ratchet curve); drag its handle to set the length");
    add (holdBtn, "holdbtn", "GHOST HOLD: hold the stable part of the selected range (a vowel, a note); drag the right edge to set the held length");
    repeatBtn.onClick = [this] { repeatClicked(); };
    holdBtn.onClick = [this] { holdClicked(); };
    addChildComponent (holdStrip);
    holdStrip.toast = [this] (const juce::String& s) { toast (s); };
    holdStrip.onClose = [this] { overlay.selHold = overlay.selHoldRegion = 0; holdStrip.setVisible (false); layoutStrips(); refresh(); };
    overlay.onSelectHold = [this] (int64 r, int64 h) { showHold (r, h); };
    playPanel.toast = [this] (const juce::String& s) { toast (s); };
    proc.onChanged = [this] { refresh(); view.repaint(); lane.repaint(); };
    proc.onToast = [this] (const juce::String& s) { toast (s); view.zoomToFit(); };
    setLook (proc.uiTheme.load());
    setResizable (true, true);
    setResizeLimits (1160, 700, 2600, 1600);
    setSize (1280, 780);
    setWantsKeyboardFocus (true);
    setWorkspace (Workspace::Edit);
    startTimerHz (10);
}

SliceEditor::~SliceEditor()
{
    stopTimer();
    proc.onChanged = nullptr; proc.onToast = nullptr;
    setLookAndFeel (nullptr);
}

void SliceEditor::setTheme (ghosty::themes::Theme t) { setLook (ghosty::themes::toInt (t)); }

void SliceEditor::setLook (int look)
{
    look = look == kLookGhost ? kLookGhost : ghosty::themes::toInt (ghosty::themes::fromInt (look));   // (unknown values: BLACK, as before)
    const bool ghost = look == kLookGhost, wasGlass = P.glass;
    const auto t = ghost ? ghosty::themes::Theme::Light : ghosty::themes::fromInt (look);
    shownTheme = t; shownLook = look; proc.uiTheme.store (look);
    P = palette (t, ghost); lnf.set (P);
    syncBtn.glass = ghost;
    help.P = P; help.repaint();
    view.setPalette (P); lane.setPalette (P); playPanel.setPalette (P); repeatStrip.setPalette (P); overlay.setPalette (P); holdStrip.setPalette (P);
    themeBtn.setButtonText (lookName (nextLook (look)));
    syncBtn.setTheme (t);
    for (auto* f : { &gainF, &fadeInF, &fadeOutF, &pitchF, &centsF, &formantF, &snapStrF, &strengthF, &confF, &feelF, &tonalityF, &fineF, &nFormF, &driftF, &vibF, &transF, &chopSensF, &chopPartsF }) f->cap.setColour (juce::Label::textColourId, P.dim);
    for (auto* l : { &bpmLbl, &keyLbl, &gridLbl }) l->setColour (juce::Label::textColourId, P.ink);
    sendLookAndFeelChange();
    if (wasGlass != P.glass && ! getLocalBounds().isEmpty()) resized();   // GHOST floats its panels (gutters)
    repaint();
}

void SliceEditor::setWorkspace (Workspace w)
{
    view.setWorkspace (w);
    const bool playWs = w == Workspace::Play;
    playPanel.setVisible (playWs); view.setVisible (! playWs); lane.setVisible (! playWs);
    if (playWs) playPanel.refresh();
    if (w != Workspace::Edit) { repeatStrip.setVisible (false); holdStrip.setVisible (false); }
    else if (overlay.selRepeat != 0 && proc.project().findRepeat (overlay.selRepeat) != nullptr) repeatStrip.setVisible (true);
    else if (overlay.selHold != 0 && holdStrip.hold() != nullptr) holdStrip.setVisible (true);
    layoutStrips();
    for (int i = 0; i < 4; ++i) tabs[i].setToggleState (i == (int) w, juce::dontSendNotification);
    resized(); refresh(); repaint();
}

void SliceEditor::toast (const juce::String& s) { toastText = s; toastUntil = juce::Time::getMillisecondCounterHiRes() + 3200; proc.lastMessage = s; repaint (infoArea); }

void SliceEditor::sliderToProject (Field& f)
{
    if (refreshing) return;
    const double v = f.s.getValue();
    if (&f == &snapStrF) { view.snapStrength = (float) (v / 100.0); return; }
    if (&f == &strengthF || &f == &confF || &f == &feelF) { if (view.alignPreview != nullptr) proposeAlign (false); return; }
    if (&f == &fineF) { view.editSelected ([v] (PitchEdit& e) { e.cents = std::round (e.target() / 100.0) * 100.0 + v - e.centre; }, "Fine"); return; }
    if (&f == &nFormF) { view.editSelected ([v] (PitchEdit& e) { e.formantCents = v * 100.0; }, "Note formant"); return; }
    if (&f == &driftF) { view.editSelected ([v] (PitchEdit& e) { e.drift = v / 100.0; }, "Drift"); return; }
    if (&f == &vibF) { view.editSelected ([v] (PitchEdit& e) { e.vibrato = v / 100.0; }, "Vibrato"); return; }
    if (&f == &transF) { view.editSelected ([v] (PitchEdit& e) { e.transitionMs = v; }, "Transition"); return; }
    const auto& p = proc.project();
    std::optional<Project> q;
    if ((&f == &pitchF || &f == &centsF || &f == &formantF) && regionPitchScope())
    {   // EDIT with a region selected: its own PITCH / CENTS / FORMANT (like the ENGINE box follows the selection)
        const auto* r = view.selectedRegion();
        q = ops::setRegionPitch (p, r->id, &f == &pitchF ? v : r->pitchSemis, &f == &centsF ? v : r->pitchCents, &f == &formantF ? v : r->formantSemis);
        if (q) proc.apply (*q, "Region " + f.cap.getText().toLowerCase());
        return;
    }
    if (&f == &pitchF || &f == &centsF || &f == &formantF || &f == &tonalityF)
    {
        Project n = p; if (&f == &pitchF) n.pitchSemis = v; else if (&f == &centsF) n.pitchCents = v; else if (&f == &formantF) n.formantSemis = v; else n.tonalityHz = v; q = n;
    }
    else if (const auto* r = view.selectedRegion())
    {
        if (&f == &gainF) q = ops::setGain (p, r->id, v);
        else if (&f == &fadeInF) q = ops::setFades (p, r->id, v, r->fadeOutMs);
        else q = ops::setFades (p, r->id, r->fadeInMs, v);
    }
    if (q) proc.apply (*q, f.cap.getText());
}

void SliceEditor::refresh()
{
    refreshing = true;
    const auto& p = proc.project();
    const bool has = proc.getSource() != nullptr;
    const auto* r = view.selectedRegion();
    bpmLbl.setText (juce::String (p.bpm, 2), juce::dontSendNotification);
    keyLbl.setText (keyText(), juce::dontSendNotification);
    gridLbl.setText (p.gridDiv > 0 ? "1/" + juce::String (p.gridDiv) + (p.gridTriplet ? "T" : "") : "OFF", juce::dontSendNotification);
    static const int divs[] { 0, 4, 8, 16, 32, 64 };
    for (int i = 0; i < 6; ++i) if (divs[i] == p.gridDiv) snapBox.setSelectedItemIndex (i, juce::dontSendNotification);
    tripBtn.setToggleState (p.gridTriplet, juce::dontSendNotification);
    playBtn.setButtonText (proc.isPlaying() ? "STOP" : "PLAY"); playBtn.setToggleState (proc.isPlaying(), juce::dontSendNotification);
    loopBtn.setToggleState (proc.looping(), juce::dontSendNotification);
    abBtn.setToggleState (proc.compareOriginal(), juce::dontSendNotification); abBtn.setButtonText (proc.compareOriginal() ? "B: ORIG" : "A/B");
    qualityBtn.setButtonText (proc.studioMode() ? "STUDIO" : "LIVE"); qualityBtn.setToggleState (proc.studioMode(), juce::dontSendNotification);
    undoBtn.setEnabled (proc.getHistory().canUndo()); redoBtn.setEnabled (proc.getHistory().canRedo());
    for (auto* b : { &cutBtn, &delBtn, &exportWavBtn, &exportMidiBtn, &abBtn, &addMkBtn, &tapBtn }) b->setEnabled (has);
    for (auto* b : { &splitMkBtn, &dupBtn, &revBtn }) b->setEnabled (has && r != nullptr);
    remMkBtn.setEnabled (view.sel.markerId != 0); anchorBtn.setEnabled (view.sel.markerId != 0);
    if (r != nullptr && view.sel.markerId != 0)
        if (const auto* mr = p.find (view.sel.markerRegion)) for (auto& w : mr->warp) if (w.id == view.sel.markerId) anchorBtn.setToggleState (w.anchor, juce::dontSendNotification);
    if (view.sel.markerId == 0) anchorBtn.setToggleState (false, juce::dontSendNotification);
    moveBtn.setToggleState (view.flexMode == FlexMode::Move, juce::dontSendNotification);
    stretchBtn.setToggleState (view.flexMode == FlexMode::Stretch, juce::dontSendNotification);
    slipBtn.setToggleState (view.flexMode == FlexMode::Slip, juce::dontSendNotification);
    lockBtn.setToggleState (p.transientLock, juce::dontSendNotification);
    for (auto* f : { &gainF, &fadeInF, &fadeOutF }) f->setEnabled (r != nullptr);
    if (r != nullptr) { gainF.s.setValue (r->gainDb, juce::dontSendNotification); fadeInF.s.setValue (r->fadeInMs, juce::dontSendNotification); fadeOutF.s.setValue (r->fadeOutMs, juce::dontSendNotification); }
    if (regionPitchScope())
    {   // the selected region's own PITCH / CENTS / FORMANT (accent captions say so)
        pitchF.s.setValue (r->pitchSemis, juce::dontSendNotification); centsF.s.setValue (r->pitchCents, juce::dontSendNotification); formantF.s.setValue (r->formantSemis, juce::dontSendNotification);
    }
    else { pitchF.s.setValue (p.pitchSemis, juce::dontSendNotification); centsF.s.setValue (p.pitchCents, juce::dontSendNotification); formantF.s.setValue (p.formantSemis, juce::dontSendNotification); }
    {
        const bool rs = regionPitchScope();
        for (auto* f : { &pitchF, &centsF, &formantF }) f->cap.setColour (juce::Label::textColourId, rs ? P.marker : P.dim);
        pitchF.s.setTooltip (rs ? "This region's PITCH (semitones, formants kept) on top of the global PITCH. Click empty space for the global one" : "Global PITCH (semitones) - the whole project, formants kept. Select a region for its own");
        centsF.s.setTooltip (rs ? "This region's fine pitch (cents)" : "Global fine pitch (cents)");
        formantF.s.setTooltip (rs ? "This region's FORMANT shift on top of the global FORMANT" : "Global FORMANT shift (0 = keep the voice's own formants)");
    }
    relinkBtn.setVisible (proc.needsRelink());
    {
        const auto mi = juce::jmax (0, materialBox.indexOfItemId (1 + (int) proc.material()) );
        materialBox.setSelectedItemIndex (mi, juce::dontSendNotification);
        if (auto a = proc.analysis(); a != nullptr && proc.material() == analysis::Material::Auto)
            materialBox.changeItemText (1, juce::String ("AUTO: ") + analysis::materialName (a->onsets.used));
        else materialBox.changeItemText (1, "AUTO");
        if (view.alignPreview != nullptr && view.alignPreviewFor != proc.getHistory().currentPtr()) view.alignPreview = nullptr;   // the project changed under it
        for (auto* b : { &alignBtn, &applyAlignBtn }) b->setEnabled (has);
        clearAutoBtn.setEnabled (has && align::countAuto (p) > 0);
        auto a = proc.analysis(); useBpmBtn.setEnabled (has && a != nullptr && a->tempo.bpm > 0);
        feelF.setEnabled (alignModeBox.getSelectedItemIndex() == 1);
        alignBtn.setToggleState (view.alignPreview != nullptr, juce::dontSendNotification);
        f0Btn.setToggleState (view.showF0, juce::dontSendNotification);
        chordsBtn.setToggleState (view.showChords, juce::dontSendNotification);
        tonalityF.s.setValue (p.tonalityHz, juce::dontSendNotification);
        auto idOf = [] (const juce::String& k) { bool mn = false; const int pc = analysis::parseKey (k, mn); return pc < 0 ? 1 : 2 + pc + (mn ? 12 : 0); };
        srcKeyBox.setSelectedId (idOf (p.key), juce::dontSendNotification); projKeyBox.setSelectedId (idOf (p.projectKey), juce::dontSendNotification);
        if (auto an = proc.analysis(); an != nullptr && an->key.valid())
            srcKeyBox.changeItemText (1, "DETECTED: " + an->key.readout() + (an->chords != nullptr && an->chords->chordShare() >= 0.05 ? " +CHORDS" : ""));
        else srcKeyBox.changeItemText (1, "SOURCE: DETECTED");
        matchKeyBtn.setEnabled (has && p.projectKey != "--" && p.keyMode != (int) keyops::Mode::Manual);
        keyModeBox.setSelectedId (juce::jlimit (0, keyops::kModes - 1, p.keyMode) + 1, juce::dontSendNotification);
        notesToFields();
    }
    {   // GHOST CHOP
        const auto& m = p.play;
        chopBox.setSelectedId ((int) m.mode + 1, juce::dontSendNotification);
        chopSensF.s.setValue (m.sensitivity * 100.0, juce::dontSendNotification); chopPartsF.s.setValue (m.equalN, juce::dontSendNotification);
        chopDivBox.setSelectedId (m.beatDiv, juce::dontSendNotification);
        const auto sl = has ? proc.currentSlices() : std::vector<play::Slice>();
        view.chopPreview.clear();
        if (m.mode != SliceMode::Regions) for (size_t i = 1; i < sl.size(); ++i) view.chopPreview.push_back (sl[i].dst0);
        splitSlicesBtn.setEnabled (has && m.mode != SliceMode::Regions && sl.size() > 1);
        chopInfo.setText (juce::String ((int) sl.size()) + (sl.size() == 1 ? " slice" : " slices") + (m.mode == SliceMode::Regions ? " (the regions)" : ""), juce::dontSendNotification);
        chopInfo.setColour (juce::Label::textColourId, P.dim);
        if (view.workspace() == Workspace::Edit && ! toolArea.isEmpty()) layoutRow2();   // SENS / PARTS / DIV follow the mode
    }
    refreshing = false;
    if (playPanel.isVisible()) playPanel.refresh();
    if (repeatStrip.isVisible())
    {
        if (proc.project().findRepeat (repeatStrip.repeatId()) == nullptr) { repeatStrip.setVisible (false); overlay.selRepeat = 0; layoutStrips(); }
        else repeatStrip.refresh();
    }
    if (holdStrip.isVisible())
    {
        if (holdStrip.hold() == nullptr) { holdStrip.setVisible (false); overlay.selHold = overlay.selHoldRegion = 0; layoutStrips(); }
        else holdStrip.refresh();
    }
    for (auto* b : { &repeatBtn, &holdBtn }) b->setEnabled (has);
    overlay.repaint();
    repaint (infoArea); repaint (headerArea);
}

void SliceEditor::notesToFields()
{
    // (called inside refresh: refreshing is set, so the value changes below do not edit)
    const auto sel = view.selectedNotes();
    const bool any = ! sel.empty();
    for (auto* f : noteFields()) f->setEnabled (any);
    for (auto* b : { &glideBtn, &resetNoteBtn, &snapSelBtn }) b->setEnabled (any);
    splitNoteBtn.setEnabled (sel.size() == 1); mergeBtn.setEnabled (sel.size() >= 2);
    snapAllBtn.setEnabled (! view.pitchNotes().empty());
    drawBtn.setToggleState (view.pitchDraw, juce::dontSendNotification);
    PitchEdit e;   // the defaults
    if (any)
    {
        const auto& n = sel.front();
        e.centre = n.centre * 100.0; e.cents = (n.target - n.centre) * 100.0;
        if (const auto* r = proc.project().find (n.regionId)) for (auto& x : r->pitch) if (x.id == n.editId) e = x;
    }
    fineF.s.setValue (any ? e.target() - std::round (e.target() / 100.0) * 100.0 : 0.0, juce::dontSendNotification);
    nFormF.s.setValue (e.formantCents / 100.0, juce::dontSendNotification);
    driftF.s.setValue (e.drift * 100.0, juce::dontSendNotification); vibF.s.setValue (e.vibrato * 100.0, juce::dontSendNotification);
    transF.s.setValue (e.transitionMs, juce::dontSendNotification);
    glideBtn.setToggleState (e.glide, juce::dontSendNotification);
    engineBox.setSelectedId (juce::jlimit (0, 2, any && e.engine >= 0 ? e.engine : proc.project().pitchEngine) + 1, juce::dontSendNotification);
    if (auto a = proc.analysis(); a != nullptr && a->key.valid()) scaleBox.changeItemText (1, "SCALE: " + a->key.name()); else scaleBox.changeItemText (1, "SCALE: SOURCE KEY");
}

void SliceEditor::chopChanged (const std::function<void (PlayMap&)>& f, const juce::String& label)
{
    PlayMap m = proc.project().play; f (m);
    if (auto q = ops::setPlay (proc.project(), m)) proc.apply (*q, label);
    refresh(); view.repaint();
}

gslice::pitchedit::Scale SliceEditor::chosenScale() const
{
    pitchedit::Scale sc;
    const int id = scaleBox.getSelectedId();
    if (id >= 3) { sc.chromatic = false; sc.tonic = (id - 3) % 12; sc.minor = id - 3 >= 12; return sc; }
    if (id == 1)
    {   // the source key: set by hand, else the detected one
        bool mn = false; int pc = analysis::parseKey (proc.project().key, mn);
        if (pc < 0) if (auto a = proc.analysis(); a != nullptr && a->key.valid()) { pc = a->key.tonic; mn = a->key.minor; }
        if (pc >= 0) { sc.chromatic = false; sc.tonic = pc; sc.minor = mn; }
    }
    return sc;
}

void SliceEditor::timerCallback()
{
    static int tick = 0;
    if (++tick % 3 == 0 || proc.isPlaying()) { playBtn.setButtonText (proc.isPlaying() ? "STOP" : "PLAY"); playBtn.setToggleState (proc.isPlaying(), juce::dontSendNotification); }
    repaint (infoArea);
    if (toastUntil > 0 && juce::Time::getMillisecondCounterHiRes() > toastUntil) { toastUntil = 0; repaint (infoArea); }
}

// ---------------------------------------------------------------- readouts: what is SET vs what the analysis DETECTED
juce::String SliceEditor::keyText() const
{
    const auto& p = proc.project();
    if (p.key.isNotEmpty() && p.key != "--") return p.key;
    if (auto a = proc.analysis(); a != nullptr && a->key.valid()) return a->key.name();
    return "--";
}
juce::String SliceEditor::keySub() const
{
    const auto& p = proc.project();
    if (p.key.isNotEmpty() && p.key != "--") return p.projectKey != "--" ? "SET > " + p.projectKey : juce::String ("SET");
    if (auto a = proc.analysis(); a != nullptr && a->key.valid()) return juce::String (juce::roundToInt (a->key.confidence * 100.0)) + " %" + (p.projectKey != "--" ? juce::String (" > ") + p.projectKey : juce::String());
    return proc.analysing() ? "..." : juce::String();
}
juce::String SliceEditor::bpmSub() const
{
    auto a = proc.analysis();
    if (a == nullptr || a->tempo.bpm <= 0) return proc.analysing() ? "SET  ..." : "SET";
    if (std::abs (a->tempo.bpm - proc.project().bpm) < 0.05) return "DETECTED";
    return "SET  det " + juce::String (a->tempo.bpm, a->tempo.bpm >= 100 ? 1 : 2);
}

juce::String SliceEditor::infoLeft() const
{
    auto s = proc.getSource();
    if (s == nullptr) return proc.needsRelink() ? "audio missing: " + proc.project().source.fileName : juce::String ("no audio loaded");
    return s->file.getFileName() + "   " + s->describe() + "   sha256 " + gslice::shortFp (s->fingerprint);
}
juce::String SliceEditor::infoRight() const
{
    const auto& pl = proc.currentPlan();
    juce::String r = juce::String ((int) pl.pieces.size()) + (pl.pieces.size() == 1 ? " piece" : " pieces");
    if (proc.studioMode()) { const auto n = proc.studioPending(); r << "   STUDIO " << (n > 0 ? juce::String ((int) n) + " rendering " + juce::String (juce::roundToInt (proc.studioProgress() * 100.0f)) + " %" : juce::String ("ready")); }
    else r << "   LIVE";
    r << "   latency " << proc.ceilingLatency() << " smp";
    return r;
}

void SliceEditor::paint (juce::Graphics& g)
{
    if (P.glass) { paintGlass (g); return; }
    g.fillAll (P.window);
    g.setColour (P.header); g.fillRect (headerArea);
    g.setColour (P.line); g.fillRect (headerArea.getX(), headerArea.getBottom() - 1, headerArea.getWidth(), 1);
    // ghost mascot + wordmark
    {
        auto L = logoArea.toFloat();
        gslice::ui::drawSliceGhost (g, gslice::ui::mascotColours (P), L.getX() + 15, L.getCentreY() + 1, 34.0f);
        const auto f = LnF::uiFont (16.5f);
        g.setFont (f); g.setColour (P.ink);
        const float base = L.getCentreY() + f.getAscent() * 0.5f - 2.0f;
        g.drawSingleLineText ("GHOST", (int) L.getX() + 38, (int) base);
        g.setColour (P.wave); g.drawSingleLineText ("SLICE", (int) (L.getX() + 38 + ghosty::textWidth (f, "GHOST ")), (int) base);
    }
    // readout captions
    g.setFont (LnF::uiFont (10.5f)); g.setColour (P.dim);
    for (auto* l : { &bpmLbl, &keyLbl, &gridLbl })
    {
        auto b = l->getBounds().withHeight (kReadoutH);   // the label is the top line; the sub-line is painted under it
        g.setColour (P.cell); g.fillRoundedRectangle (b.toFloat().withX ((float) b.getX() - 36).withWidth ((float) b.getWidth() + 38), 6.0f);
        g.setFont (LnF::uiFont (10.5f));
        g.setColour (P.dim); g.drawText (l == &bpmLbl ? "BPM" : l == &keyLbl ? "KEY" : "GRID", b.withX (b.getX() - 32).withWidth (32).withHeight (l == &gridLbl ? kReadoutH : 20), juce::Justification::centredLeft);
        const auto sub = l == &bpmLbl ? bpmSub() : l == &keyLbl ? keySub() : juce::String();
        if (sub.isNotEmpty())
        {
            g.setFont (LnF::uiFont (9.5f, false)); g.setColour (P.dim);
            g.drawText (sub, juce::Rectangle<int> (b.getX() - 32, b.getY() + 19, b.getWidth() + 32, kReadoutH - 20), juce::Justification::centredLeft, true);
        }
    }
    // info strip
    g.setColour (P.panel); g.fillRect (infoArea);
    g.setFont (LnF::uiFont (11.5f, false)); g.setColour (P.dim);
    auto ia = infoArea.reduced (10, 0).withTrimmedRight (26);
    const auto right = infoRight();
    const int rw = (int) ghosty::textWidth (LnF::uiFont (11.5f, false), right) + 12;
    g.drawText (right, ia.removeFromRight (rw), juce::Justification::centredRight);
    progressArea = {};
    if (proc.studioMode() && proc.studioPending() > 0)
    {   // the STUDIO render progress (the bar fills as the background renders land)
        progressArea = ia.removeFromRight (90).withSizeKeepingCentre (84, 6);
        const auto pa = progressArea.toFloat();
        g.setColour (P.cell); g.fillRoundedRectangle (pa, 3.0f);
        g.setColour (P.marker); g.fillRoundedRectangle (pa.withWidth (juce::jmax (6.0f, pa.getWidth() * proc.studioProgress())), 3.0f);
        ia.removeFromRight (6);
    }
    const bool showToast = toastUntil > 0 && juce::Time::getMillisecondCounterHiRes() < toastUntil;
    const auto& pl = proc.currentPlan();
    if (showToast) { g.setColour (P.marker); g.drawText (toastText, ia, juce::Justification::centredLeft, true); }
    else
    {
        g.setColour (P.ink); g.drawText (infoLeft(), ia, juce::Justification::centredLeft, true);
        if (pl.extremeCount > 0)
        {
            const juce::String w = "  amber: " + juce::String (pl.extremeCount) + " stretched beyond 0.5x - 2x";
            const int ww = (int) ghosty::textWidth (LnF::uiFont (11.5f, false), w) + 8;
            g.setColour (P.warn); g.drawText (w, ia.removeFromRight (ww), juce::Justification::centredRight);
        }
    }
    g.setColour (P.panel); g.fillRect (toolArea);
    g.setColour (P.line); g.fillRect (toolArea.getX(), toolArea.getY(), toolArea.getWidth(), 1);
}

// GHOST: the aurora (cached), floating glass panels (cached per size), the controls on them (LnF glass), the same texts in deep indigo
void SliceEditor::paintGlassSurfaces (juce::Graphics& g)
{
    namespace gg = ghosty::glass;
    backdrop.draw (g, getWidth(), getHeight());
    const int G = kGlassGutter;
    panels.draw (g, headerArea.reduced (6, 5).toFloat(), 18.0f, 0.38f, 10.0f);
    if (view.isVisible()) panels.draw (g, view.getBounds().toFloat(), 16.0f, 0.30f, 12.0f);
    for (juce::Component* c : { (juce::Component*) &lane, (juce::Component*) &repeatStrip, (juce::Component*) &holdStrip })
        if (c->isVisible() && ! c->getBounds().isEmpty()) panels.draw (g, c->getBounds().toFloat(), juce::jmin (14.0f, (float) c->getHeight() * 0.5f), 0.30f, 8.0f);
    panels.draw (g, toolArea.reduced (G - 2, 2).toFloat(), 12.0f, 0.34f, 10.0f);
    auto L = logoArea.toFloat();
    gg::panel (g, juce::Rectangle<float> (L.getX() - 1.0f, L.getCentreY() - 16.0f, 34.0f, 34.0f), 17.0f, juce::Colours::white, 0.55f, false, 4.0f);   // the mascot's disc
    for (auto* l : { &bpmLbl, &keyLbl, &gridLbl })
    {
        auto b = l->getBounds().withHeight (kReadoutH);
        gg::recessed (g, b.toFloat().withX ((float) b.getX() - 36).withWidth ((float) b.getWidth() + 38), 9.0f);
    }
    gg::recessed (g, infoArea.reduced (G, 1).toFloat(), 11.0f);
}

void SliceEditor::paintGlass (juce::Graphics& g)
{
    paintGlassSurfaces (g);
    const int G = kGlassGutter;
    // the mascot (on its glass disc) + the wordmark
    {
        auto L = logoArea.toFloat();
        const float cx = L.getX() + 16.0f, cy = L.getCentreY() + 1.0f;
        gslice::ui::drawSliceGhost (g, gslice::ui::mascotColours (P), cx - 1.0f, cy, 30.0f);
        const auto f = LnF::uiFont (16.5f);
        g.setFont (f); g.setColour (P.ink);
        const float base = L.getCentreY() + f.getAscent() * 0.5f - 2.0f;
        g.drawSingleLineText ("GHOST", (int) L.getX() + 40, (int) base);
        g.setColour (P.wave); g.drawSingleLineText ("SLICE", (int) (L.getX() + 40 + ghosty::textWidth (f, "GHOST ")), (int) base);
    }
    // readouts in recessed wells
    for (auto* l : { &bpmLbl, &keyLbl, &gridLbl })
    {
        auto b = l->getBounds().withHeight (kReadoutH);
        g.setFont (LnF::uiFont (10.5f));
        g.setColour (P.dim); g.drawText (l == &bpmLbl ? "BPM" : l == &keyLbl ? "KEY" : "GRID", b.withX (b.getX() - 32).withWidth (32).withHeight (l == &gridLbl ? kReadoutH : 20), juce::Justification::centredLeft);
        const auto sub = l == &bpmLbl ? bpmSub() : l == &keyLbl ? keySub() : juce::String();
        if (sub.isNotEmpty())
        {
            g.setFont (LnF::uiFont (9.5f, false)); g.setColour (P.dim);
            g.drawText (sub, juce::Rectangle<int> (b.getX() - 32, b.getY() + 19, b.getWidth() + 32, kReadoutH - 20), juce::Justification::centredLeft, true);
        }
    }
    // the info strip (on its recessed glass well)
    g.setFont (LnF::uiFont (11.5f, false)); g.setColour (P.dim);
    auto ia = infoArea.reduced (G + 10, 0).withTrimmedRight (26);
    const auto right = infoRight();
    const int rw = (int) ghosty::textWidth (LnF::uiFont (11.5f, false), right) + 12;
    g.drawText (right, ia.removeFromRight (rw), juce::Justification::centredRight);
    progressArea = {};
    if (proc.studioMode() && proc.studioPending() > 0)
    {
        progressArea = ia.removeFromRight (90).withSizeKeepingCentre (84, 6);
        const auto pa = progressArea.toFloat();
        ghosty::glass::track (g, pa, pa.getX(), pa.getX() + juce::jmax (6.0f, pa.getWidth() * proc.studioProgress()));
        ia.removeFromRight (6);
    }
    const bool showToast = toastUntil > 0 && juce::Time::getMillisecondCounterHiRes() < toastUntil;
    const auto& pl = proc.currentPlan();
    if (showToast) { g.setColour (P.marker); g.drawText (toastText, ia, juce::Justification::centredLeft, true); }
    else
    {
        g.setColour (P.ink); g.drawText (infoLeft(), ia, juce::Justification::centredLeft, true);
        if (pl.extremeCount > 0)
        {
            const juce::String w = "  amber: " + juce::String (pl.extremeCount) + " stretched beyond 0.5x - 2x";
            const int ww = (int) ghosty::textWidth (LnF::uiFont (11.5f, false), w) + 8;
            g.setColour (P.warn); g.drawText (w, ia.removeFromRight (ww), juce::Justification::centredRight);
        }
    }
}

std::vector<juce::Component*> SliceEditor::headerItems() { return { &tabs[0], &tabs[1], &tabs[2], &tabs[3], &bpmLbl, &keyLbl, &gridLbl, &playBtn, &loopBtn, &loadBtn, &projBtn, &syncBtn, &themeBtn, &unlock }; }
std::vector<juce::Component*> SliceEditor::toolItems() { return { &cutBtn, &splitMkBtn, &delBtn, &dupBtn, &revBtn, &undoBtn, &redoBtn, &snapBox, &tripBtn, &abBtn, &qualityBtn, &exportWavBtn, &exportMidiBtn }; }

void SliceEditor::resized()
{
    auto r = getLocalBounds();
    headerArea = r.removeFromTop (kHeader);
    infoArea = r.removeFromTop (kInfo);
    toolArea = r.removeFromBottom (toolsHeight());
    helpBtn.setBounds (infoArea.getRight() - 30, infoArea.getY() + 2, 24, infoArea.getHeight() - 4);
    help.setBounds (getLocalBounds().withTrimmedTop (kHeader));
    playPanel.setBounds (r);
    layoutStrips();
    relinkBtn.setBounds (view.getBounds().withSizeKeepingCentre (120, 30).translated (0, 52));
    // header
    auto h = headerArea.reduced (10, 13);
    logoArea = h.removeFromLeft (156);
    h.removeFromLeft (6);
    for (auto& t : tabs) { t.setBounds (h.removeFromLeft (52)); h.removeFromLeft (3); }
    h.removeFromLeft (8);
    for (auto* l : { &bpmLbl, &keyLbl, &gridLbl })
    {
        h.removeFromLeft (32);
        const auto b = h.removeFromLeft (l == &keyLbl ? 56 : 46); h.removeFromLeft (6);
        l->setBounds (l == &gridLbl ? b : b.withHeight (20));
    }
    unlock.setBounds (h.removeFromRight (76)); h.removeFromRight (4);
    themeBtn.setBounds (h.removeFromRight (66)); h.removeFromRight (4);
    syncBtn.setBounds (h.removeFromRight (86)); h.removeFromRight (6);
    projBtn.setBounds (h.removeFromRight (74)); h.removeFromRight (4);
    loadBtn.setBounds (h.removeFromRight (54)); h.removeFromRight (8);
    loopBtn.setBounds (h.removeFromRight (54)); h.removeFromRight (4);
    playBtn.setBounds (h.removeFromRight (58));
    // tool row 1
    auto t = toolArea.reduced (10, 6).withTrimmedRight (12);   // (clear of the resize corner)
    auto row1 = t.removeFromTop (32); t.removeFromTop (6);
    auto place = [&row1] (juce::Component& c, int w, int gap = 4) { c.setBounds (row1.removeFromLeft (w)); row1.removeFromLeft (gap); };
    place (cutBtn, 54); place (splitMkBtn, 122); place (delBtn, 66); place (dupBtn, 86); place (revBtn, 78); place (undoBtn, 58); place (redoBtn, 58, 14);
    place (snapBox, 96); place (tripBtn, 30, 14); place (abBtn, 66); place (qualityBtn, 76, 14);
    exportMidiBtn.setBounds (row1.removeFromRight (108)); row1.removeFromRight (5);
    exportWavBtn.setBounds (row1.removeFromRight (100));
    layoutRow2();
}

void SliceEditor::layoutStrips()
{
    auto r = playPanel.getBounds();
    if (r.isEmpty()) return;
    if (P.glass)
    {   // GHOST: the same stack as floating glass panels with gutters between them
        const int g = kGlassGutter;
        r = r.reduced (g, 0).withTrimmedTop (g - 2);
        lane.setBounds (r.removeFromBottom (kLane).reduced (0, g / 2 + 1));
        if (repeatStrip.isVisible()) repeatStrip.setBounds (r.removeFromBottom (gslice::ui::RepeatStrip::kHeight).withTrimmedTop (g / 2));
        if (holdStrip.isVisible()) holdStrip.setBounds (r.removeFromBottom (gslice::ui::HoldStrip::kHeight).withTrimmedTop (g / 2));
        view.setBounds (r.withTrimmedBottom (g / 2));
        overlay.setBounds (view.getLocalBounds());
        relinkBtn.setBounds (view.getBounds().withSizeKeepingCentre (120, 30).translated (0, 52));
        return;
    }
    lane.setBounds (r.removeFromBottom (kLane));
    if (repeatStrip.isVisible()) repeatStrip.setBounds (r.removeFromBottom (gslice::ui::RepeatStrip::kHeight));
    if (holdStrip.isVisible()) holdStrip.setBounds (r.removeFromBottom (gslice::ui::HoldStrip::kHeight));
    view.setBounds (r);
    overlay.setBounds (view.getLocalBounds());
    relinkBtn.setBounds (view.getBounds().withSizeKeepingCentre (120, 30).translated (0, 52));
}

void SliceEditor::showHold (int64 regionId, int64 holdId)
{
    overlay.selHold = holdId; overlay.selHoldRegion = regionId; overlay.selRepeat = 0; repeatStrip.setVisible (false);
    holdStrip.show (regionId, holdId); holdStrip.setVisible (view.workspace() == Workspace::Edit);
    layoutStrips(); refresh(); view.repaint();
}

void SliceEditor::holdClicked()
{
    auto src = proc.getSource();
    if (src == nullptr) { toast ("Load audio first"); return; }
    if (! view.sel.hasRange())
    {
        const double c = proc.hfToPf ((double) proc.cursor());
        for (auto& r : proc.project().regions)
            for (auto& h : r.holds) { const auto sp = ops::holdSpan (proc.project(), r, h); if (sp.ok && c >= sp.selA && c < sp.selB) { showHold (r.id, h.id); return; } }
        toast ("Select a range to HOLD (a vowel, a note: the stable part is found for you)");
        return;
    }
    const double a = proc.hfToPf ((double) view.sel.a), b = proc.hfToPf ((double) view.sel.b);
    const Region* rg = nullptr;
    for (auto& r : proc.project().regions) if (a >= r.dstStart && b <= r.dstEnd() + 0.5) rg = &r;   // the topmost holding the whole selection
    if (rg == nullptr) { toast ("HOLD works inside one region: select inside it"); return; }
    const auto m = rg->map();
    const int64 sa = (int64) std::llround (m.srcAt (a - rg->dstStart)), sb = (int64) std::llround (m.srcAt (std::min (b, rg->dstEnd()) - rg->dstStart));
    const auto an = proc.analysis();   // phase 16: the vowel of the syllable under the selection (consonants stay at 1x)
    const auto st = hold::pickStable (*src, sa, sb, an != nullptr && an->fingerprint == src->fingerprint ? &an->syllables : nullptr);
    const double kps = proc.project().source.sampleRate / proc.project().sampleRate;
    const double total = 3.0 * (double) (sb - sa) / kps;   // first held to 3x (drag the right edge for more / less)
    const double held = std::max ((double) (st.b - st.a) / kps, total - (double) (st.a - sa) / kps - (double) (sb - st.b) / kps);
    int64 id = 0; juce::String why;
    if (auto q = ops::addHold (proc.project(), rg->id, sa, sb, st.a, st.b, held, HoldMode::Sustain, &id, &why))
    {
        const auto rid = rg->id;
        proc.apply (*q, "Hold"); view.sel.a = view.sel.b = 0; showHold (rid, id);
        toast (st.unstable ? "GHOST HOLD: unstable region, TEXTURE may sound better" : "GHOST HOLD: drag the right edge to set the held length");
        return;
    }
    toast (why);
}

void SliceEditor::showRepeat (int64 id)
{
    overlay.selRepeat = id; overlay.selHold = 0; holdStrip.setVisible (false);
    repeatStrip.show (id); repeatStrip.setVisible (view.workspace() == Workspace::Edit);
    layoutStrips(); refresh(); view.repaint();
}

void SliceEditor::repeatClicked()
{
    if (proc.getSource() == nullptr) { toast ("Load audio first"); return; }
    if (view.sel.hasRange())
    {
        int64 id = 0; juce::String why;
        if (auto q = ops::addRepeat (proc.project(), proc.hfToPf ((double) view.sel.a), proc.hfToPf ((double) view.sel.b), &id, &why))
        {
            proc.apply (*q, "Repeat"); view.sel.a = view.sel.b = 0; showRepeat (id);
            toast ("GHOST REPEAT: drag its right handle to set the length");
            return;
        }
        toast (why); return;
    }
    const double c = proc.hfToPf ((double) proc.cursor());
    for (auto& r : proc.project().repeats) if (c >= r.start && c < r.end()) { showRepeat (r.id); return; }
    toast ("Select a range to REPEAT (or put the cursor in a REPEAT span)");
}

void SliceEditor::layoutRow2()
{
    auto t = toolArea.reduced (10, 6).withTrimmedRight (12); t.removeFromTop (38);
    auto row = t.withHeight (32);
    const bool flex = view.workspace() == Workspace::Flex, pitchWs = view.workspace() == Workspace::Pitch, playWs = view.workspace() == Workspace::Play;
    for (auto* c : std::initializer_list<juce::Component*> { &gainF, &fadeInF, &fadeOutF }) c->setVisible (! flex && ! pitchWs && ! playWs);
    for (auto* c : std::initializer_list<juce::Component*> { &pitchF, &centsF, &formantF }) c->setVisible (! flex && ! playWs);
    for (auto* c : std::initializer_list<juce::Component*> { &repeatBtn, &holdBtn }) c->setVisible (! flex && ! pitchWs && ! playWs);
    for (auto* c : std::initializer_list<juce::Component*> { &moveBtn, &stretchBtn, &slipBtn, &addMkBtn, &remMkBtn, &anchorBtn, &snapStrF, &tapBtn, &lockBtn }) c->setVisible (flex);
    tonalityF.setVisible (pitchWs); engineBox.setVisible (pitchWs);
    if (pitchWs)
    {   // the global PITCH / CENTS / FORMANT / TONALITY + the ENGINE
        engineBox.setBounds (row.removeFromRight (150).reduced (0, 2)); row.removeFromRight (16);
        const int w = (row.getWidth() - 3 * 12) / 4;
        for (auto* f : { &pitchF, &centsF, &formantF, &tonalityF }) { f->setBounds (row.removeFromLeft (w)); row.removeFromLeft (12); }
    }
    else if (! flex)
    {   // region GAIN / FADES, then the global PITCH / CENTS / FORMANT
        holdBtn.setBounds (row.removeFromRight (70)); row.removeFromRight (6);
        repeatBtn.setBounds (row.removeFromRight (84)); row.removeFromRight (14);
        const int w = (row.getWidth() - 5 * 8 - 8) / 6;
        for (auto* f : { &gainF, &fadeInF, &fadeOutF, &pitchF, &centsF, &formantF })
        {
            f->setBounds (row.removeFromLeft (w)); row.removeFromLeft (f == &fadeOutF ? 16 : 8);
        }
    }
    else
    {
        auto place = [&row] (juce::Component& c, int w, int gap = 4) { c.setBounds (row.removeFromLeft (w)); row.removeFromLeft (gap); };
        place (moveBtn, 58); place (stretchBtn, 76); place (slipBtn, 54, 12);
        place (addMkBtn, 96); place (remMkBtn, 72); place (anchorBtn, 72, 12);
        place (tapBtn, 82); place (lockBtn, 70, 16);
        snapStrF.setBounds (row.withWidth (juce::jmin (row.getWidth(), 320)));
    }
    // row 3 (FLEX): GHOST ALIGN; (PITCH): the analysis display
    auto r3 = toolArea.reduced (10, 6).withTrimmedRight (12); r3.removeFromTop (76); r3 = r3.withHeight (32);
    for (auto* c : std::initializer_list<juce::Component*> { &f0Btn, &srcKeyBox, &projKeyBox, &keyModeBox, &matchKeyBtn, &glideBtn, &splitNoteBtn, &mergeBtn, &resetNoteBtn,
                                                             &drawBtn, &snapSelBtn, &snapAllBtn, &scaleBox }) c->setVisible (pitchWs);
    for (auto* f : noteFields()) f->setVisible (pitchWs);
    chordsBtn.setVisible (false);   // (shown below where it fits: PITCH row 4, EDIT row 3)
    if (pitchWs)
    {   // row 3: the selected notes
        auto rr = r3;
        resetNoteBtn.setBounds (rr.removeFromRight (64)); rr.removeFromRight (4);
        mergeBtn.setBounds (rr.removeFromRight (64)); rr.removeFromRight (4);
        splitNoteBtn.setBounds (rr.removeFromRight (58)); rr.removeFromRight (4);
        glideBtn.setBounds (rr.removeFromRight (60)); rr.removeFromRight (12);
        const int fw = (rr.getWidth() - 4 * 10) / 5;
        for (auto* f : noteFields()) { f->setBounds (rr.removeFromLeft (fw)); rr.removeFromLeft (10); }
        // row 4: keys, scale snap, draw, the f0 overlay
        auto r4 = toolArea.reduced (10, 6).withTrimmedRight (12); r4.removeFromTop (114); r4 = r4.withHeight (32);
        f0Btn.setBounds (r4.removeFromRight (90)); r4.removeFromRight (6);
        drawBtn.setBounds (r4.removeFromRight (62)); r4.removeFromRight (12);
        snapAllBtn.setBounds (r4.removeFromRight (82)); r4.removeFromRight (4);
        snapSelBtn.setBounds (r4.removeFromRight (58)); r4.removeFromRight (4);
        scaleBox.setBounds (r4.removeFromRight (150)); r4.removeFromRight (12);
        const int kw = juce::jmax (100, (r4.getWidth() - 100 - 3 * 8) / 3);
        srcKeyBox.setBounds (r4.removeFromLeft (juce::jmin (200, kw + 30))); r4.removeFromLeft (8);
        projKeyBox.setBounds (r4.removeFromLeft (juce::jmin (150, kw - 15))); r4.removeFromLeft (8);
        keyModeBox.setBounds (r4.removeFromLeft (juce::jmin (164, kw - 15))); r4.removeFromLeft (8);
        matchKeyBtn.setBounds (r4.removeFromLeft (juce::jmin (100, r4.getWidth())));
        r4.removeFromLeft (8);   // CHORDS: in what is left (nothing above moves)
        chordsBtn.setBounds (r4.removeFromLeft (juce::jmin (84, r4.getWidth()))); chordsBtn.setVisible (chordsBtn.getWidth() >= 70);
    }
    {
        const bool editWs = view.workspace() == Workspace::Edit;
        const auto mode = proc.project().play.mode;
        chopBox.setVisible (editWs); splitSlicesBtn.setVisible (editWs); chopInfo.setVisible (editWs);
        chopSensF.setVisible (editWs && (mode == SliceMode::Transient || mode == SliceMode::Syllable || mode == SliceMode::Note));
        chopPartsF.setVisible (editWs && mode == SliceMode::Equal);
        chopDivBox.setVisible (editWs && mode == SliceMode::Beat);
        if (editWs)
        {
            auto rr = r3;
            auto place = [&rr] (juce::Component& c, int w, int gap = 8) { c.setBounds (rr.removeFromLeft (w)); rr.removeFromLeft (gap); };
            place (chopBox, 168);
            if (chopSensF.isVisible()) place (chopSensF, 220);
            if (chopPartsF.isVisible()) place (chopPartsF, 220);
            if (chopDivBox.isVisible()) place (chopDivBox, 80);
            place (splitSlicesBtn, 132, 12);
            chopInfo.setBounds (rr.removeFromLeft (juce::jmin (240, rr.getWidth())));
            rr.removeFromLeft (8);   // CHORDS: in what is left (nothing above moves)
            chordsBtn.setBounds (rr.removeFromRight (juce::jmin (84, rr.getWidth()))); chordsBtn.setVisible (chordsBtn.getWidth() >= 70);
        }
    }
    for (auto* c : std::initializer_list<juce::Component*> { &materialBox, &alignBtn, &alignModeBox, &strengthF, &confF, &feelF, &applyAlignBtn, &clearAutoBtn, &useBpmBtn }) c->setVisible (flex);
    if (flex)
    {
        auto place = [&r3] (juce::Component& c, int w, int gap = 6) { c.setBounds (r3.removeFromLeft (w)); r3.removeFromLeft (gap); };
        place (materialBox, 128, 12); place (alignBtn, 96); place (alignModeBox, 98, 10);
        const int sw = juce::jmax (150, (r3.getWidth() - 66 - 96 - 82 - 12 - 6 * 6) / 3);
        place (strengthF, sw + 30); place (confF, sw - 15); place (feelF, sw - 15, 10);
        place (applyAlignBtn, 66, 12); place (clearAutoBtn, 96); place (useBpmBtn, 82);
    }
}

int SliceEditor::toolsHeight() const
{
    const auto w = view.workspace();
    return w == Workspace::Pitch ? kTools4 : w == Workspace::Flex || w == Workspace::Edit ? kTools3 : kTools;
}

// ---------------------------------------------------------------- GHOST ALIGN
align::Settings SliceEditor::alignSettings() const
{
    align::Settings s;
    s.mode = (align::Mode) juce::jlimit (0, 2, alignModeBox.getSelectedItemIndex());
    s.strength = strengthF.s.getValue() / 100.0; s.minConfidence = confF.s.getValue() / 100.0; s.grooveFeel = feelF.s.getValue() / 100.0;
    return s;
}

bool SliceEditor::proposeAlign (bool announce)
{
    if (proc.getSource() == nullptr) { toast ("Load audio first"); return false; }
    const auto ev = proc.alignEvents();
    auto pr = std::make_shared<align::Proposals> (align::propose (proc.project(), ev, alignSettings()));
    view.alignPreview = pr; view.alignPreviewFor = proc.getHistory().currentPtr();
    if (announce || pr->moving == 0) toast (juce::String ("ALIGN ") + align::modeName (pr->settings.mode) + ": " + pr->summary() + (pr->moving > 0 ? "  -  APPLY keeps it" : ""));
    view.repaint();
    return pr->moving > 0;
}

bool SliceEditor::applyAlign()
{
    if (proc.getSource() == nullptr) { toast ("Load audio first"); return false; }
    const auto pr = align::propose (proc.project(), proc.alignEvents(), alignSettings());
    juce::String why;
    const auto q = align::apply (proc.project(), pr, &why);
    view.alignPreview = nullptr;
    if (! q.has_value()) { toast (why); view.repaint(); return false; }
    proc.apply (*q, "Auto align");
    toast ("Aligned: " + pr.summary());
    view.repaint();
    return true;
}

bool SliceEditor::matchProjectKey()
{
    auto a = proc.analysis();
    juce::String why; int shift = 0, snapped = 0;
    const auto mode = (keyops::Mode) juce::jlimit (0, keyops::kModes - 1, proc.project().keyMode);
    const auto from = keyText();
    const auto q = keyops::matchKey (proc.project(), a.get(), &why, &shift, &snapped);
    if (! q.has_value()) { toast (why); return false; }
    proc.apply (*q, juce::String ("Match key (") + keyops::modeName (mode) + ")");
    if (mode == keyops::Mode::ScaleSnap) toast ("MATCH KEY (SCALE SNAP): " + juce::String (snapped) + " notes on " + proc.project().projectKey);
    else toast ("MATCH KEY (" + juce::String (keyops::modeName (mode)) + "): " + juce::String (shift > 0 ? "+" : "") + juce::String (shift) + " st (" + from + " -> " + proc.project().projectKey + ")");
    view.repaint();
    return true;
}

void SliceEditor::showHelp (bool on)
{
    help.P = P; help.setVisible (on); helpBtn.setToggleState (on, juce::dontSendNotification);
    if (on) help.toFront (false);
}

juce::StringArray HelpOverlay::lines()
{
    return {
        "#EVERYWHERE", "SPACE|play / stop (standalone, or SYNC: OWN)", "1 2 3 4|EDIT / FLEX / PITCH / PLAY", "Cmd/Ctrl+Z|undo (Shift: redo)", "Cmd/Ctrl+Y|redo",
        "L|loop the selection", "Z|zoom to the selection", "+ / -|zoom in / out", "HOME|cursor to the start", "?|this list (Esc closes it)",
        "right-click|the cursor here", "right-drag|a loop over that span", "wheel|scroll (Cmd/Ctrl: zoom)",
        "look button|BLACK / PURPLE / LIGHT / GHOST (glass)",
        "#EDIT", "S|cut at the cursor (or both edges of the selection)", "R|reverse the region", "Cmd/Ctrl+D|duplicate the region", "DELETE|delete the selection / region",
        "LEFT / RIGHT|nudge the region (Shift: fine)", "drag the top bar|move (snaps to the grid)", "drag an edge|trim (Alt: stretch)", "Cmd/Ctrl-drag|slip the audio",
        "PITCH / FORMANT|of the selected region (none: all)", "HOLD|holds the vowel, keeps consonants",
        "#FLEX", "double-click|add a warp marker (snaps to an onset)", "M|add a marker at the cursor", "A|anchor the selected marker", "drag a marker|MOVE / STRETCH (Alt: no snap, Shift: fine)",
        "#PITCH", "click / Cmd-click|select notes (drag on empty space: a band)", "drag a blob|pitch, semitone snap (Shift: free cents)", "Cmd/Ctrl-drag a blob|TIMING: move the note",
        "drag a blob edge|TIMING: stretch the note's start / end", "double-click a blob|split the note there", "UP / DOWN|selected notes +-1 semitone (Shift: +-10 ct)",
        "D|PITCH DRAW on / off (drag = draw the pitch)", "DELETE|reset the selected notes",
        "CHORDS|the chord lane on / off (read-only; also in EDIT)", "USE CHORDS|KEY MODE: MATCH KEY by the chord roots",
        "#PLAY", "click a pad|play it (hold = gate)", "drag a pad out|its WAV (full version)", "MIDI C1 up|the slices",
        "HOLD VOWEL|hold the key: the vowel sustains (GATE)", "CHOP: SYLLABLE|a slice per syllable (consonant + vowel)" };
}

void HelpOverlay::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().reduced (28, 18);
    auto head = r.removeFromTop (124);
    if (P.glass)
    {   // GHOST: a frosted sheet over the aurora + a glass card, the mascot on a glass disc
        g.setColour (juce::Colour (0xfff4f1fc).withAlpha (0.94f)); g.fillRect (getLocalBounds());
        ghosty::glass::panel (g, getLocalBounds().reduced (12, 8).toFloat(), 22.0f, juce::Colours::white, 0.40f, false, 8.0f);
        ghosty::glass::panel (g, juce::Rectangle<float> (64.0f - 58.0f, 0, 116.0f, 116.0f).withCentre ({ (float) head.getX() + 64.0f, (float) head.getCentreY() }), 58.0f, juce::Colours::white, 0.5f, false, 6.0f);
    }
    else { g.setColour (P.window.withAlpha (0.985f)); g.fillRect (getLocalBounds()); }
    gslice::ui::drawSliceGhost (g, gslice::ui::mascotColours (P), (float) head.getX() + 64.0f, (float) head.getCentreY(), 120.0f);
    g.setColour (P.ink); g.setFont (LnF::uiFont (24.0f));
    g.drawText ("GHOST", head.withTrimmedLeft (140).removeFromTop (70), juce::Justification::bottomLeft);
    g.setColour (P.wave); g.drawText ("SLICE", head.withTrimmedLeft (140 + (int) ghosty::textWidth (LnF::uiFont (24.0f), "GHOST ")).removeFromTop (70), juce::Justification::bottomLeft);
    g.setColour (P.dim); g.setFont (LnF::uiFont (12.0f, false));
    g.drawText (juce::String ("version ") + GSLICE_VERSION + "  -  keyboard shortcuts and gestures  -  click or Esc to close", head.withTrimmedLeft (140).withTrimmedTop (76).removeFromTop (20), juce::Justification::topLeft);
    const auto L = lines();
    const int cols = r.getWidth() > 1100 ? 3 : 2;
    const int colW = r.getWidth() / cols;
    const float rowH = 19.0f;
    int sections = 0; for (auto& x : L) sections += x.startsWith ("#") ? 1 : 0;
    const int maxRows = juce::jmin (juce::jmax (8, (int) ((float) r.getHeight() / rowH)), (L.size() + sections + cols - 1) / cols + 4);   // balanced columns
    int c = 0, k = -1;
    for (int i = 0; i < L.size(); ++i)
    {
        if (L[i].startsWith ("#"))
        {   // a section stays in one column: it moves on when it does not fit below the previous one
            int len = 1; while (i + len < L.size() && ! L[i + len].startsWith ("#")) ++len;
            if (k >= 0) k += 1;   // a blank row between sections
            if (k >= 0 && k + len > maxRows && c + 1 < cols) { ++c; k = -1; }
        }
        ++k;
        auto row = juce::Rectangle<float> ((float) (r.getX() + c * colW), (float) r.getY() + k * rowH, (float) colW - 16.0f, rowH);
        const auto& s = L[i];
        if (s.startsWith ("#")) { g.setColour (P.slime); g.setFont (LnF::uiFont (12.5f)); g.drawText (s.substring (1), row, juce::Justification::bottomLeft); continue; }
        g.setColour (P.ink); g.setFont (LnF::uiFont (11.5f));
        g.drawText (s.upToFirstOccurrenceOf ("|", false, false), row.withWidth (150.0f), juce::Justification::centredLeft, true);
        g.setColour (P.dim); g.setFont (LnF::uiFont (11.5f, false));
        g.drawText (s.fromFirstOccurrenceOf ("|", false, false), row.withTrimmedLeft (156.0f), juce::Justification::centredLeft, true);
    }
}

void SliceEditor::mouseUp (const juce::MouseEvent& e)
{
    if (e.eventComponent == &keyLbl && ! e.mouseWasDraggedSinceMouseDown()) keyPicker();
}

void SliceEditor::keyPicker()
{
    juce::PopupMenu keys, modes, m;
    const auto& p = proc.project();
    for (int mn = 0; mn < 2; ++mn)
        for (int k = 0; k < 12; ++k) { const auto nm = analysis::pitchClassName (k) + (mn ? " MIN" : " MAJ"); keys.addItem (100 + mn * 12 + k, nm, true, nm == p.projectKey); }
    keys.addItem (99, "-- (none)", true, p.projectKey == "--");
    for (int i = 0; i < keyops::kModes; ++i) modes.addItem (200 + i, keyops::modeName ((keyops::Mode) i), true, p.keyMode == i);
    m.addSectionHeader ("SOURCE " + keyText() + (keySub().isNotEmpty() ? "  (" + keySub() + ")" : juce::String()));
    m.addSubMenu ("TARGET KEY: " + p.projectKey, keys);
    m.addSubMenu (juce::String ("KEY MODE: ") + keyops::modeName ((keyops::Mode) juce::jlimit (0, keyops::kModes - 1, p.keyMode)), modes);
    m.addSeparator();
    m.addItem (300, "MATCH PROJECT KEY", p.projectKey != "--" && proc.getSource() != nullptr);
    juce::Component::SafePointer<SliceEditor> sp (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&keyLbl), [sp] (int r)
    {
        if (sp == nullptr || r == 0) return;
        Project q = sp->proc.project();
        if (r == 99) q.projectKey = "--";
        else if (r >= 100 && r < 124) q.projectKey = analysis::pitchClassName ((r - 100) % 12) + (r >= 112 ? " MIN" : " MAJ");
        else if (r >= 200 && r < 200 + keyops::kModes) q.keyMode = r - 200;
        if (r == 300) sp->matchProjectKey(); else sp->proc.apply (q, r >= 200 ? "Key mode" : "Project key");
        sp->refresh();
    });
}

bool SliceEditor::useDetectedBpm()
{
    auto a = proc.analysis();
    if (a == nullptr || a->tempo.bpm <= 0) { toast ("No tempo detected yet"); return false; }
    const auto& p = proc.project();
    Project q = p; q.bpm = juce::jlimit (20.0, 999.0, a->tempo.bpm);
    // the downbeat's output time: through the first forward region that plays that source position
    const double srcPos = a->tempo.downbeat * p.source.sampleRate;
    double off = a->tempo.downbeat * p.sampleRate;
    for (auto& r : p.regions) if (! r.reverse && srcPos >= (double) r.srcIn() && srcPos < (double) r.srcOut()) { off = r.dstStart + r.map().outAt (srcPos); break; }
    const double bar = q.secondsPerBeat() * q.sampleRate * 4.0;
    q.beatOffset = std::fmod (off, bar);
    if (! proc.apply (q, "Detected tempo")) { toast ("Already at the detected tempo"); return false; }
    toast ("BPM " + juce::String (q.bpm, 2) + " (detected, " + juce::String (juce::roundToInt (a->tempo.confidence * 100)) + " %), the grid follows the downbeat");
    return true;
}

bool SliceEditor::keyPressed (const juce::KeyPress& k)
{
    const auto mods = k.getModifiers(); const int code = k.getKeyCode();
    const bool cmd = mods.isCommandDown(), shift = mods.isShiftDown();
    if (cmd && (code == 'Z' || code == 'z')) { shift ? proc.redo() : proc.undo(); refresh(); return true; }
    if (cmd && (code == 'Y' || code == 'y')) { proc.redo(); refresh(); return true; }
    if (cmd && (code == 'D' || code == 'd')) { view.duplicateSelection(); refresh(); return true; }
    if (cmd) return false;
    if (k.getTextCharacter() == '?' || (code == '/' && shift)) { showHelp (! help.isVisible()); return true; }
    if (code == juce::KeyPress::escapeKey && help.isVisible()) { showHelp (false); return true; }
    if (code == juce::KeyPress::spaceKey)
    {   // in a DAW with SYNC: DAW the space bar belongs to the DAW; standalone / SYNC: OWN plays this
        if (proc.isHosted() && proc.sync.getMode() == ghosty::SyncTransport::Mode::Daw) return false;
        proc.play (! proc.isPlaying()); refresh(); return true;
    }
    if (code >= '1' && code <= '4') { setWorkspace ((Workspace) (code - '1')); return true; }
    switch (juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) code))
    {
        case 'S': view.cutAtCursorOrSelection(); refresh(); return true;
        case 'R': view.reverseSelection(); refresh(); return true;
        case 'L': loopBtn.triggerClick(); return true;
        case 'Z': view.zoomToSelection(); return true;
        case 'A': if (view.workspace() == Workspace::Flex) { view.toggleAnchor(); refresh(); return true; } break;
        case 'M': if (view.workspace() == Workspace::Flex) { view.addMarkerAtCursor(); refresh(); return true; } break;
        default: break;
    }
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
    {
        if (view.workspace() == Workspace::Pitch) view.resetSelected(); else view.deleteSelection();
        refresh(); return true;
    }
    if (view.workspace() == Workspace::Pitch && (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey))
    {   // the selected notes +-1 semitone (Shift: +-10 cents)
        view.shiftSelected ((code == juce::KeyPress::upKey ? 1.0 : -1.0) * (shift ? 10.0 : 100.0), ! shift); refresh(); return true;
    }
    if (view.workspace() == Workspace::Pitch && (code == 'D' || code == 'd')) { drawBtn.triggerClick(); return true; }
    if (code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey) { view.nudge (code == juce::KeyPress::leftKey ? -1 : 1, shift); refresh(); return true; }
    if (code == '+' || code == '=') { view.zoomAround (0.5, (float) view.getWidth() * 0.5f); return true; }
    if (code == '-') { view.zoomAround (2.0, (float) view.getWidth() * 0.5f); return true; }
    if (code == juce::KeyPress::homeKey) { proc.setCursor (0); view.repaint(); return true; }
    return false;
}

// ---------------------------------------------------------------- files
void SliceEditor::loadClicked()
{
    chooser = std::make_unique<juce::FileChooser> ("Load audio", juce::File::getSpecialLocation (juce::File::userMusicDirectory), source::wildcard());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
    {
        const auto f = fc.getResult(); if (f == juce::File()) return;
        view.sel = {}; proc.loadAudioAsync (f);
    });
}

void SliceEditor::projectMenu()
{
    juce::PopupMenu m;
    m.addItem (1, "Open project...");
    m.addItem (2, "Save project", proc.getSource() != nullptr && proc.projectLocation() != juce::File());
    m.addItem (3, "Save project as...", proc.getSource() != nullptr || proc.needsRelink());
    m.addSeparator();
    m.addItem (4, "Relink audio...", proc.needsRelink());
    juce::Component::SafePointer<SliceEditor> sp (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&projBtn), [sp] (int r)
    {
        if (sp == nullptr || r == 0) return;
        auto& ed = *sp;
        if (r == 1)
        {
            ed.chooser = std::make_unique<juce::FileChooser> ("Open a Ghost Slice project", juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.ghostslice;*.gslice.json;project.json");
            ed.chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::canSelectDirectories, [sp] (const juce::FileChooser& fc)
            {
                if (sp == nullptr) return;
                auto f = fc.getResult(); if (f == juce::File()) return;
                if (f.getFileName() == "project.json" && f.getParentDirectory().hasFileExtension (".ghostslice")) f = f.getParentDirectory();
                juce::String err; sp->view.sel = {};
                if (! sp->proc.openProject (f, err)) sp->toast (err == "relink" ? sp->proc.lastMessage : err); else sp->toast ("Opened " + f.getFileName());
                sp->view.zoomToFit(); sp->refresh();
            });
        }
        else if (r == 2) { juce::String err; if (! ed.proc.saveProject (ed.proc.projectLocation(), err)) ed.toast (err); else ed.toast ("Saved " + ed.proc.projectLocation().getFileName()); }
        else if (r == 3)
        {
            const auto base = ed.proc.audioFile() != juce::File() ? ed.proc.audioFile().getFileNameWithoutExtension() : juce::String ("Untitled");
            ed.chooser = std::make_unique<juce::FileChooser> ("Save the Ghost Slice project", juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile (base + ".ghostslice"), "*.ghostslice");
            ed.chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [sp] (const juce::FileChooser& fc)   // NOT-EXPORT: the project's own format (JSON + an audio reference, no audio)
            {
                if (sp == nullptr) return;
                auto f = fc.getResult(); if (f == juce::File()) return;
                if (! f.hasFileExtension (".ghostslice")) f = f.withFileExtension (".ghostslice");
                juce::String err; if (! sp->proc.saveProject (f, err)) sp->toast (err); else sp->toast ("Saved " + f.getFileName());
            });
        }
        else if (r == 4) ed.relinkBtn.triggerClick();
    });
}

void SliceEditor::exportWavClicked()
{
    if (! ghosty::exportAllowed (proc.license, &exportWavBtn, "slice.export.wav")) return;
    if (proc.getSource() == nullptr) { toast ("Load audio first"); return; }
    const auto base = proc.audioFile().getFileNameWithoutExtension();
    const auto dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Ghost Slice");
    juce::Component::SafePointer<SliceEditor> sp (this);
    ghosty::exportgate::saveDialog (chooser, "Export the arrangement as WAV", dir.getChildFile (base + " (slice).wav"), "*.wav", [sp] (const juce::File& picked)
    {
        if (sp == nullptr) return;
        const auto f = picked.hasFileExtension (".wav") ? picked : picked.withFileExtension (".wav");
        f.getParentDirectory().createDirectory();
        juce::String err;
        if (sp->proc.exportWav (f, err)) sp->toast ("Exported " + f.getFileName()); else sp->toast (err);
    });
}

void SliceEditor::exportMidiClicked()
{
    if (! ghosty::exportAllowed (proc.license, &exportMidiBtn, "slice.export.midi")) return;
    if (proc.getSource() == nullptr) { toast ("Load audio first"); return; }
    const auto base = proc.audioFile().getFileNameWithoutExtension();
    const auto dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Ghost Slice");
    juce::Component::SafePointer<SliceEditor> sp (this);
    ghosty::exportgate::saveDialog (chooser, "Export the slice positions as MIDI", dir.getChildFile (base + " (slices).mid"), "*.mid", [sp] (const juce::File& picked)
    {
        if (sp == nullptr) return;
        const auto f = picked.hasFileExtension (".mid") ? picked : picked.withFileExtension (".mid");
        f.getParentDirectory().createDirectory();
        juce::String err;
        if (sp->proc.exportMidi (f, err)) sp->toast ("Exported " + f.getFileName()); else sp->toast (err);
    });
}
