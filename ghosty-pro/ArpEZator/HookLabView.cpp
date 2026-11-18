// HOOK LAB view: the tile grid + its buttons, and the arp-vs-progression alignment strip.
#include "ArpEditor.h"

namespace arpui
{
using namespace arp;
static juce::Font HF (float h, bool b = true) { return ghosty::font (h, b); }

void ArpEditor::openHookLab (bool open)
{
    if (hookLab == nullptr) return;
    if (open)
    {
        if (more != nullptr) more->setVisible (false);
        hookLab->sync(); hookLab->setVisible (true); hookLab->toFront (false);
        hookLab->grabKeyboardFocus();
        if (proc.lab.tiles.empty() && ! proc.lab.busy) { proc.labGenerateAsync (proc.lab.bars); toast ("16 hooks over your chords: click (or 1-9) to hear one, S stars a keeper, Enter uses it"); }
    }
    else { hookLab->setVisible (false); proc.labAudition (-1); }
    refresh();
}

// ---------------------------------------------------------------- alignment strip
void AlignStrip::paint (juce::Graphics& g)
{
    const auto& P = ed.th.p; auto r = getLocalBounds().toFloat();
    g.setColour (P.card2); g.fillRoundedRectangle (r, 6);
    const auto& I = ed.proc.idea();
    const auto marks = arpAlignment (I);
    const int span = juce::jmax (BAR, juce::jmin (I.total, juce::jmax (I.H.loopTicks, 4 * BAR)));
    auto in = r.reduced (8, 4); in.removeFromLeft (64);
    // the legend (right): arp starts | chord changes | they meet
    const bool roomy = in.getWidth() >= 300.0f;   // the legend only when there's room (the tooltip explains it otherwise)
    auto leg = roomy ? in.removeFromRight (150.0f) : juce::Rectangle<float>(); if (roomy) in.removeFromRight (8);
    if (roomy)
    {
        g.setFont (HF (9.0f, false));
        const float w3 = leg.getWidth() / 3.0f, cy = leg.getCentreY();
        auto item = [&] (int k, const juce::String& t) {
            const float x = leg.getX() + k * w3;
            if (k == 0) { g.setColour (ed.laneColour (LaneArp)); g.fillRect (x, cy - 1, 2.0f, 7.0f); }
            else if (k == 1) { g.setColour (ed.laneColour (LaneChords)); g.fillRect (x, cy - 6, 2.0f, 7.0f); }
            else { g.setColour (P.gold); g.fillEllipse (x - 2, cy - 4, 8, 8); }
            g.setColour (P.dim); g.drawText (t, juce::Rectangle<float> (x + 8, leg.getY(), w3 - 8, leg.getHeight()), juce::Justification::centredLeft, true); };
        item (0, "arp"); item (1, "chords"); item (2, "meet");
    }
    g.setColour (P.dim); g.setFont (HF (9.5f));
    g.drawText ("ALIGN " + juce::String (juce::roundToInt (arpAlignmentScore (I) * 100)) + "%", juce::Rectangle<float> (r.getX() + 6, r.getY(), 66, r.getHeight()), juce::Justification::centredLeft);
    for (int b = 0; b <= span / BAR; ++b) { const float x = in.getX() + in.getWidth() * (float) (b * BAR) / span; g.setColour (P.grid); g.drawVerticalLine ((int) x, in.getY(), in.getBottom()); }
    for (auto& m : marks)
    {
        const float x = in.getX() + in.getWidth() * (float) m.t / span;
        if (m.kind == 0) { g.setColour (ed.laneColour (LaneArp).withAlpha (0.8f)); g.fillRect (x - 1, in.getY() + in.getHeight() * 0.55f, 2.0f, in.getHeight() * 0.45f); }
        else if (m.kind == 1) { g.setColour (ed.laneColour (LaneChords)); g.fillRect (x - 1, in.getY(), 2.0f, in.getHeight() * 0.5f); }
        else { g.setColour (P.gold); g.fillEllipse (x - 4, in.getCentreY() - 4, 8, 8); }
    }
}

// ---------------------------------------------------------------- the panel
HookLabPanel::HookLabPanel (ArpEditor& e)
    : ed (e), barsSeg (e.th, { "4 BARS", "8 BARS" }), styleSeg (e.th, { "AUTO", "HYPNOTIC", "ANTHEMIC", "MINIMAL", "EMOTIONAL", "PLAYFUL" }),
      candySeg (e.th, { "NO CANDY", "SUBTLE", "HOOK", "EUPHORIC", "UNHINGED" }), songSeg (e.th, { "16", "32" }), regSeg (e.th, { "LOW", "MID", "HIGH" }),
      genB (e.th, "GENERATE 16 HOOKS"), magicB (e.th, "FIND THE MAGIC"), songB (e.th, "MAKE IT A SONG"), combineB (e.th, "COMBINE"), useB (e.th, "USE THIS"),
      respondB (e.th, "RESPONSE"), motifB (e.th, "LOCK MOTIF"), harvestB (e.th, "HARVEST MOTIFS"), close (e.th, "CLOSE"), pianoB (e.th, "PIANO TEST"),
      autoB (e.th, "AUTO-PLAY"), prevB (e.th, "<"), nextB (e.th, ">"), cancelB (e.th, "CANCEL"),
      meterSeg (e.th, { "4/4", "3/4", "5/4", "7/8", "9/8" }), feelSeg (e.th, { "NORMAL", "HALF-TIME", "DROP" }), euclidS (e.th, "EUCLID", 0, 16), rotS (e.th, "ROTATE", 0, 15)
{
    auto& th = e.th;
    setWantsKeyboardFocus (true);
    for (auto* c : std::initializer_list<juce::Component*> { &barsSeg, &styleSeg, &candySeg, &songSeg, &regSeg, &genB, &magicB, &songB, &combineB, &useB, &respondB, &motifB, &harvestB, &close, &pianoB,
                                                             &autoB, &prevB, &nextB, &tension, &similarity, &meterSeg, &feelSeg, &euclidS, &rotS }) addAndMakeVisible (c);
    addChildComponent (cancelB);
    genB.big = true; genB.tint = [&th] { return th.p.a1; };
    useB.big = true; useB.tint = [&th] { return th.p.green; };
    magicB.tint = [&th] { return th.p.gold; }; songB.tint = [&th] { return th.p.a3; }; combineB.tint = [&th] { return th.p.pink; }; combineB.toggle = true;
    respondB.tint = [&th] { return th.p.a1; }; motifB.tint = [&th] { return th.p.gold; }; harvestB.tint = [&th] { return th.p.cyan; };
    pianoB.toggle = true; pianoB.tint = [&th] { return th.p.a2; }; autoB.toggle = true; autoB.tint = [&th] { return th.p.green; }; cancelB.tint = [&th] { return th.p.pink; };
    genB.setTooltip ("16 new hooks over the current chords with the lab's STYLE / CANDY / TENSION / REGISTER (starred tiles stay)");
    magicB.setTooltip ("Variations of the selected hook that keep what makes it work (motif rhythm, interval signature, peak, contour) and change the rest: each tile says how much is new");
    songB.setTooltip ("Develop the selected hook into a 16 / 32 bar melody (A A' B A''), in the hook's register. The other lanes are looped to the new length (your edits kept)");
    combineB.setTooltip ("COMBINE: click three tiles: RHYTHM from the first, OPENING from the second, ENDING from the third");
    useB.setTooltip ("Put the selected hook in the MELODY lane (undo brings the old one back). Enter");
    harvestB.setTooltip ("MOTIF HARVESTER: the best fragments of the arp + melody, ranked by what you keep. Click one to lock it as the motif");
    pianoB.setTooltip ("PIANO TEST: hear the tiles as plain piano over the chords (flat velocity, no glides, no FX, no arp / bass). A real hook still works like this; each tile shows whether it passes");
    autoB.setTooltip ("AUTO-PLAY: after one pass of a tile, the next one plays");
    regSeg.setTooltip ("REGISTER: where the hooks sit. LOW = a counter-line under the arp, MID = the lead (G4..F#5), HIGH = a top line. Nothing goes above E6");
    prevB.setTooltip ("The grid before"); nextB.setTooltip ("The next grid");
    meterSeg.setTooltip ("METER: the hook repeats every 3/4, 5/4, 7/8 or 9/8 cycle against the 4/4 bars (it phases over the chords), accents on the meter's groups (7/8 = 2+2+3)");
    euclidS.setTooltip ("EUCLID: the hook's rhythm = this many hits spread as evenly as possible over the cycle (16ths of the METER). OFF = the engine's own rhythm");
    rotS.setTooltip ("ROTATE the euclidean rhythm by 16ths");
    feelSeg.setTooltip ("FEEL: HALF-TIME = the hook at half speed; DROP = on a 3-3-2 kick grid, sparser, wider leaps, a register lower");
    euclidS.fmt = [this] (int v) { return v == 0 ? juce::String ("OFF") : juce::String (v) + "/" + juce::String (meterSteps (meterSeg.sel)); };
    for (auto* st : { &euclidS, &rotS }) st->tint = [&th] { return th.p.cyan; };
    meterSeg.tint = [&th] { return th.p.cyan; }; feelSeg.tint = [&th] { return th.p.pink; };
    meterSeg.onChange = [this] (int) { commitRhythm(); };
    feelSeg.onChange = [this] (int) { commitRhythm(); };
    euclidS.onChange = [this] (int) { commitRhythm(); };
    rotS.onChange = [this] (int) { commitRhythm(); };
    barsSeg.onChange = [this] (int i) { ed.proc.lab.bars = i ? 8 : 4; };
    styleSeg.onChange = [this] (int) { commitParams(); };
    candySeg.onChange = [this] (int) { commitParams(); };
    regSeg.onChange = [this] (int) { commitParams(); };
    for (auto* sl : { &tension, &similarity }) { sl->setSliderStyle (juce::Slider::LinearHorizontal); sl->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0); sl->setPopupDisplayEnabled (true, true, this); }
    tension.setRange (-0.1, 1.0, 0.05); tension.setDoubleClickReturnValue (true, -0.1);
    tension.textFromValueFunction = [] (double v) { return v < 0 ? juce::String ("AUTO") : juce::String (juce::roundToInt (v * 100)) + "%"; };
    similarity.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100)) + "%"; };
    tension.setTooltip ("MELODIC TENSION of the lab's hooks: stable chord tones <-> passing tones, suspensions, extensions, resolved chromatic notes (far left = AUTO)");
    tension.onValueChange = [this] { repaint (rowC); startTimer (150); };
    similarity.setRange (0, 1, 0.05); similarity.setValue (ed.proc.lab.similarity, juce::dontSendNotification);
    similarity.onValueChange = [this] { ed.proc.lab.similarity = similarity.getValue(); repaint (rowC); };
    genB.onClick = [this] { ed.proc.labGenerateAsync (ed.proc.lab.bars); combineStep = -1; ed.toast ("16 hooks: click (or 1-9) to hear, S stars the keepers"); };
    magicB.onClick = [this] { if (ed.proc.lab.selected < 0) { ed.toast ("Pick a tile first"); return; } ed.proc.labMagicAsync(); ed.toast ("FIND THE MAGIC: tile 1 is your hook, the rest keep its motif, contour and peak"); };
    songB.onClick = [this] {
        const int t = ed.proc.lab.selected; if (t < 0) { ed.toast ("Pick a tile first"); return; }
        const int bars = songSeg.sel ? 32 : 16; const auto also = ed.proc.labSong (t, bars);
        ed.toast ("MAKE IT A SONG: " + juce::String (bars) + " bars in the MELODY lane" + (also.isNotEmpty() ? "; " + also : juce::String()) + " (UNDO to go back)", 4200); };
    combineB.onClick = [this] { combineStep = combineB.on ? 0 : -1; pick[0] = pick[1] = pick[2] = -1; ed.toast (combineB.on ? "COMBINE: click the tile with the RHYTHM" : "COMBINE off"); repaint(); };
    useB.onClick = [this] { const int t = ed.proc.lab.selected; if (t < 0) { ed.toast ("Pick a tile first"); return; } ed.proc.labUse (t); ed.toast ("In the MELODY lane (UNDO to go back)"); };
    respondB.onClick = [this] { ed.proc.respond (similarity.getValue()); ed.toast ("RESPONSE written into the MELODY lane"); };
    motifB.onClick = [this] {   // the roll's selection only counts when the roll shows the MELODY lane
        const std::vector<bool> sel = ed.proc.selLane == LaneMelody ? ed.roll.sel : std::vector<bool> {};
        ed.proc.lockMotif (sel); ed.toast (ed.proc.idea().motif.empty() ? "Motif unlocked (your own locks stay)" : "MOTIF LOCKED: it comes back in every phrase"); };
    harvestB.onClick = [this] { ed.proc.labHarvestRank(); repaint(); };
    pianoB.onClick = [this] { ed.proc.labPianoTest (pianoB.on); if (pianoB.on && ed.proc.lab.playing < 0 && ed.proc.lab.selected >= 0) ed.proc.labAudition (ed.proc.lab.selected);
                              ed.toast (pianoB.on ? "PIANO TEST on: tiles play as plain piano over the chords" : "PIANO TEST off: tiles play with your sound"); };
    autoB.onClick = [this] { ed.proc.lab.autoPlay = autoB.on; if (autoB.on && ed.proc.lab.playing < 0 && ! ed.proc.lab.tiles.empty()) play (juce::jmax (0, ed.proc.lab.selected)); };
    prevB.onClick = [this] { ed.proc.labStepHistory (-1); };
    nextB.onClick = [this] { ed.proc.labStepHistory (1); };
    cancelB.onClick = [this] { ed.proc.labCancel(); ed.toast ("Stopped"); };
    close.onClick = [this] { ed.openHookLab (false); };
}

void HookLabPanel::commitParams()
{
    stopTimer();
    const auto& L = ed.proc.lab;
    const double tv = tension.getValue() < 0 ? -1.0 : tension.getValue();
    const int style = styleAt (ed.proc.idea().mode, styleSeg.sel);   // (TRAP / HIP HOP: three more style buttons after the classic six)
    if (style == L.style && candySeg.sel == L.candy && regSeg.sel == L.reg && std::abs (tv - L.tension) < 1e-9) return;
    ed.proc.labSetParams (style, candySeg.sel, tv, regSeg.sel);   // the tiles follow (stars stay); the MELODY lane is untouched
}
void HookLabPanel::commitRhythm()
{
    const int steps = meterSteps (meterSeg.sel);
    euclidS.hi = steps; rotS.hi = steps - 1;
    if (euclidS.value > steps) euclidS.value = steps;
    if (rotS.value > steps - 1) rotS.value = 0;
    ed.proc.labSetRhythm (meterSeg.sel, euclidS.value, rotS.value, feelSeg.sel);
    repaint();
}
void HookLabPanel::timerCallback() { if (! tension.isMouseButtonDown()) commitParams(); }

void HookLabPanel::play (int tile)
{
    auto& p = ed.proc;
    if (tile < 0 || tile >= (int) p.lab.tiles.size()) return;
    p.labAudition (tile); repaint();
}

void HookLabPanel::sync()
{
    const auto& L = ed.proc.lab;
    {   // the style buttons of the idea's mood (the classic six, + DARK / TRIUMPHANT / HYPNO LOOP in TRAP, BOOM BAP / SOULFUL / LAID BACK in HIP HOP)
        const int md = ed.proc.idea().mode; juce::StringArray it; for (int i = 0; i < styleCountFor (md); ++i) it.add (styleName (styleAt (md, i)));
        if (it != styleSeg.items) { styleSeg.items = it; styleSeg.repaint(); resized(); }
        styleSeg.sel = styleButton (md, L.style);
    }
    barsSeg.sel = L.bars >= 8 ? 1 : 0; candySeg.sel = L.candy; regSeg.sel = L.reg;
    meterSeg.sel = L.meter; feelSeg.sel = L.feel; euclidS.hi = meterSteps (L.meter); rotS.hi = euclidS.hi - 1; euclidS.value = L.euclidHits; rotS.value = L.euclidRot; rotS.setEnabled (L.euclidHits > 0);
    if (! tension.isMouseButtonDown() && ! isTimerRunning()) tension.setValue (L.tension < 0 ? -0.1 : L.tension, juce::dontSendNotification);
    const auto& I = ed.proc.idea();
    motifB.on = ! I.motif.empty(); motifB.text = motifB.on ? "MOTIF LOCKED" : "LOCK MOTIF";
    pianoB.on = L.piano; autoB.on = L.autoPlay;
    const bool sel = L.selected >= 0 && ! L.busy;
    useB.setEnabled (sel); magicB.setEnabled (sel); songB.setEnabled (sel); genB.setEnabled (true);
    prevB.setEnabled (L.histIdx > 0); nextB.setEnabled (L.histIdx >= 0 && L.histIdx + 1 < (int) L.history.size());
    cancelB.setVisible (L.busy);
    for (auto* c : getChildren()) c->repaint();
    repaint();
}

void HookLabPanel::resized()
{
    auto r = getLocalBounds().reduced (14, 10);
    auto top = r.removeFromTop (30);
    close.setBounds (top.removeFromRight (80).reduced (0, 2)); top.removeFromRight (8);
    autoB.setBounds (top.removeFromRight (96).reduced (0, 2)); top.removeFromRight (8);
    nextB.setBounds (top.removeFromRight (26).reduced (0, 2)); seedArea = top.removeFromRight (86); prevB.setBounds (top.removeFromRight (26).reduced (0, 2));
    top.removeFromRight (8); top.removeFromLeft (126); titleText = top;
    r.removeFromTop (6);
    auto a = r.removeFromTop (36);
    barsSeg.setBounds (a.removeFromLeft (120).reduced (0, 3)); a.removeFromLeft (6);
    genB.setBounds (a.removeFromLeft (196)); a.removeFromLeft (6);
    useB.setBounds (a.removeFromRight (110)); a.removeFromRight (6);
    combineB.setBounds (a.removeFromRight (96)); a.removeFromRight (6);
    songSeg.setBounds (a.removeFromRight (64).reduced (0, 3)); a.removeFromRight (4);
    songB.setBounds (a.removeFromRight (130)); a.removeFromRight (6);
    magicB.setBounds (a);
    r.removeFromTop (8);
    rowB = r.removeFromTop (30);
    {
        auto b = rowB; b.removeFromLeft (48);
        const int w = b.getWidth();
        styleSeg.setBounds (b.removeFromLeft (w * (styleSeg.items.size() > kClassicStyles + 3 ? 70 : styleSeg.items.size() > kClassicStyles ? 63 : 55) / 100).reduced (0, 2)); b.removeFromLeft (10 + 50);   // (TRAP: ten styles, HIP HOP: nine)
        candySeg.setBounds (b.reduced (0, 2));
    }
    r.removeFromTop (6);
    rowC = r.removeFromTop (30);
    {
        auto c = rowC;
        pianoB.setBounds (c.removeFromRight (106).reduced (0, 1)); c.removeFromRight (8);
        regSeg.setBounds (c.removeFromRight (150).reduced (0, 3)); c.removeFromRight (8);
        c.removeFromLeft (98);
        const int flex = juce::jmax (180, c.getWidth() - 98 - 12 - 72 - 12 - 116);
        tension.setBounds (c.removeFromLeft (flex * 55 / 100).reduced (0, 5)); c.removeFromLeft (12);
        respondB.setBounds (c.removeFromLeft (98)); c.removeFromLeft (72);
        similarity.setBounds (c.removeFromLeft (flex * 45 / 100).reduced (0, 5)); c.removeFromLeft (12);
        motifB.setBounds (c.removeFromLeft (116));
    }
    r.removeFromTop (6);
    rowD = r.removeFromTop (30);
    {
        auto d = rowD; d.removeFromLeft (40);
        meterSeg.setBounds (d.removeFromLeft (250).reduced (0, 2)); d.removeFromLeft (10);
        euclidS.setBounds (d.removeFromLeft (124).reduced (0, 2)); d.removeFromLeft (6);
        rotS.setBounds (d.removeFromLeft (110).reduced (0, 2)); d.removeFromLeft (18 + 40);
        feelSeg.setBounds (d.removeFromLeft (juce::jmin (300, d.getWidth())).reduced (0, 2));
    }
    r.removeFromTop (8);
    auto bottom = r.removeFromBottom (34); r.removeFromBottom (8);
    harvestB.setBounds (bottom.removeFromLeft (140)); bottom.removeFromLeft (10);
    harvestArea = bottom;
    grid = r;
    cancelB.setBounds (grid.getCentreX() - 60, grid.getCentreY() + 22, 120, 30);
}

juce::Rectangle<int> HookLabPanel::tileRect (int i) const
{
    const int cols = 4, rows = 4, gap = 8;
    const int w = (grid.getWidth() - gap * (cols - 1)) / cols, h = (grid.getHeight() - gap * (rows - 1)) / rows;
    return { grid.getX() + (i % cols) * (w + gap), grid.getY() + (i / cols) * (h + gap), w, h };
}
juce::Rectangle<int> HookLabPanel::chipRect (int k) const
{
    const int w = juce::jmax (40, (harvestArea.getWidth() - 3 * 6) / 4);
    return { harvestArea.getX() + k * (w + 6), harvestArea.getY() + 2, w, harvestArea.getHeight() - 4 };
}

void HookLabPanel::paint (juce::Graphics& g)
{
    const auto& P = ed.th.p; auto R = getLocalBounds().toFloat();
    fillCard (g, R, 14, P); g.setColour (P.gold); g.drawRoundedRectangle (R.reduced (1), 14, 1.8f);
    g.setColour (P.ink); g.setFont (HF (17.0f)); g.drawText ("HOOK LAB", 16, 10, 140, 26, juce::Justification::centredLeft);
    const auto& lab = ed.proc.lab; const auto& I = ed.proc.idea();
    if (combineStep >= 0)
    {
        static const char* ask[] { "COMBINE: click the tile with the RHYTHM", "COMBINE: now the OPENING", "COMBINE: now the ENDING" };
        g.setColour (P.pink); g.setFont (HF (10.5f)); g.drawText (ask[juce::jlimit (0, 2, combineStep)], titleText, juce::Justification::centredLeft, true);
    }
    else if (lab.chordsChanged)
    { g.setColour (P.gold); g.setFont (HF (10.5f)); g.drawText ("Chords changed: starred tiles were refit (KEPT), the rest cleared. GENERATE for new hooks", titleText, juce::Justification::centredLeft, true); }
    else
    { g.setColour (P.dim); g.setFont (HF (10.5f, false)); g.drawText ("Click or 1-9: hear it  S: star  Enter: use  drag: MIDI", titleText, juce::Justification::centredLeft, true); }
    g.setColour (P.dim); g.setFont (HF (9.5f)); g.drawText ("LAB SEED " + juce::String ((int) (lab.seed % 100000)), seedArea, juce::Justification::centred, true);
    g.setFont (HF (10.0f));
    g.drawText ("STYLE", rowB.getX(), rowB.getY(), 46, rowB.getHeight(), juce::Justification::centredLeft);
    g.drawText ("CANDY", styleSeg.getRight() + 14, rowB.getY(), 46, rowB.getHeight(), juce::Justification::centredLeft);
    const double tv = tension.getValue();
    g.drawText (tv < 0 ? "TENSION AUTO" : "TENSION " + juce::String (juce::roundToInt (tv * 100)) + "%", rowC.getX(), rowC.getY(), 94, rowC.getHeight(), juce::Justification::centredLeft, true);
    g.drawText ("SIMILAR " + juce::String (juce::roundToInt (similarity.getValue() * 100)) + "%", respondB.getRight() + 6, rowC.getY(), 68, rowC.getHeight(), juce::Justification::centredLeft, true);
    g.drawText ("IDM", rowD.getX(), rowD.getY(), 38, rowD.getHeight(), juce::Justification::centredLeft);
    g.drawText ("FEEL", rotS.getRight() + 16, rowD.getY(), 40, rowD.getHeight(), juce::Justification::centredLeft);
    const Colour melC = ed.laneColour (LaneMelody);
    const double pt = ed.proc.playTick.load();
    constexpr int kLo = 60, kHi = 96;   // one pitch scale for every tile (C4..C7): registers compare at a glance
    for (int i = 0; i < 16; ++i)
    {
        auto tr = tileRect (i).toFloat();
        const bool has = i < (int) lab.tiles.size();
        const bool selT = i == lab.selected, playT = i == lab.playing, hov = i == hover;
        g.setColour (has ? P.roll : P.card2); g.fillRoundedRectangle (tr, 9);
        g.setColour (playT ? P.gold : selT ? P.a1 : hov ? P.ink.withAlpha (0.4f) : P.line); g.drawRoundedRectangle (tr.reduced (0.5f), 9, playT || selT ? 2.2f : 1.0f);
        if (! has) { g.setColour (P.dim); g.setFont (HF (10.0f)); g.drawText (lab.busy ? "" : "-", tr, juce::Justification::centred); continue; }
        const auto& h = lab.tiles[(size_t) i];
        auto body = tr.reduced (8, 6); auto head = body.removeFromTop (16); auto foot = body.removeFromBottom (14); body.reduce (0, 2);
        const int ticks = h.ticks();
        for (auto& c : I.H.chords)
            for (int b = 0; b < ticks; b += juce::jmax (BAR, I.H.loopTicks))
                if (b + c.t > 0 && b + c.t < ticks) { const float x = body.getX() + body.getWidth() * (float) (b + c.t) / ticks; g.setColour (P.gridB); g.drawVerticalLine ((int) x, body.getY(), body.getBottom()); }
        if (h.meter > 0)
        {   // the meter cycle against the 4/4 bars
            const int cyc = meterSteps (h.meter) * 24;
            for (int t = cyc; t < ticks; t += cyc) { const float x = body.getX() + body.getWidth() * (float) t / ticks; g.setColour (P.cyan.withAlpha (0.35f)); g.drawVerticalLine ((int) x, body.getY(), body.getBottom()); }
        }
        const float rh = body.getHeight() / (float) (kHi - kLo + 1);
        const float yc5 = body.getBottom() - (72 - kLo + 0.5f) * rh; g.setColour (P.grid); g.drawHorizontalLine ((int) yc5, body.getX(), body.getRight());
        int lo = 127, hi = 0; for (auto& n : h.notes) { lo = juce::jmin (lo, n.p); hi = juce::jmax (hi, n.p); }
        for (auto& n : h.notes)
        {
            const float x = body.getX() + body.getWidth() * (float) n.t / ticks, w = juce::jmax (2.0f, body.getWidth() * (float) n.d / ticks - 1.0f);
            const float y = body.getBottom() - (juce::jlimit (kLo, kHi, n.p) - kLo + 1) * rh;
            g.setColour (melC.withAlpha (0.5f + 0.5f * n.v)); g.fillRoundedRectangle (x, y, w, juce::jmax (2.5f, rh * 1.6f), 1.5f);
        }
        { g.setColour (P.card2); g.fillRoundedRectangle (juce::Rectangle<float> (body.getRight() - 16, yc5 - 9, 16, 9), 2); g.setColour (P.dim); g.setFont (HF (7.5f, false)); g.drawText ("C5", juce::Rectangle<float> (body.getRight() - 16, yc5 - 9, 15, 8), juce::Justification::centredRight); }   // (over the notes: always readable)
        if (playT && pt >= 0) { const float x = body.getX() + body.getWidth() * (float) std::fmod (pt, (double) ticks) / ticks; g.setColour (P.gold.withAlpha (0.8f)); g.drawVerticalLine ((int) x, body.getY(), body.getBottom()); }
        g.setColour (P.ink); g.setFont (HF (11.0f)); g.drawText (juce::String (i + 1), head.removeFromLeft (22), juce::Justification::centredLeft);
        if (h.kept) { auto kb = head.removeFromLeft (34).reduced (0, 2); g.setColour (P.gold.withAlpha (0.85f)); g.fillRoundedRectangle (kb, 3); g.setColour (P.textOn (P.gold)); g.setFont (HF (8.0f)); g.drawText ("KEPT", kb, juce::Justification::centred); head.removeFromLeft (4); }
        g.setColour (P.dim); g.setFont (HF (9.5f, false)); g.drawText (juce::String (h.label), head.withTrimmedRight (24), juce::Justification::centredLeft, true);
        {
            auto sr = starRect (i).toFloat().reduced (5); juce::Path st; st.addStar (sr.getCentre(), 5, sr.getWidth() * 0.22f, sr.getWidth() * 0.55f, 0.0f);
            g.setColour (h.star ? P.gold : P.dim.withAlpha (0.5f)); if (h.star) g.fillPath (st); else g.strokePath (st, juce::PathStrokeType (1.2f));
        }
        const bool pianoOk = h.piano >= kPianoMin;
        g.setFont (HF (9.0f, false)); g.setColour (P.dim);
        const juce::String dot = juce::String::fromUTF8 (" \xc2\xb7 ");
        const juce::String ft = juce::String (h.bars) + " bars" + dot + juce::String ((int) h.notes.size()) + " notes" + dot + "range " + juce::String (juce::jmax (0, hi - lo)) + dot;
        g.drawText (ft, foot, juce::Justification::centredLeft, true);
        const float ftW = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), ft);
        g.setColour (pianoOk ? P.green : P.pink); g.drawText (pianoOk ? "piano ok" : "piano weak", foot.withTrimmedLeft (ftW), juce::Justification::centredLeft, true);
        for (int k = 0; k < 3; ++k) if (pick[k] == i)
        {
            static const char* tag[] { "RHYTHM", "OPENING", "ENDING" };
            auto b = juce::Rectangle<float> (tr.getRight() - 70, tr.getBottom() - 20, 62, 14);
            g.setColour (P.pink); g.fillRoundedRectangle (b, 4); g.setColour (P.textOn (P.pink)); g.setFont (HF (9.0f)); g.drawText (tag[k], b, juce::Justification::centred);
        }
    }
    if (lab.busy)
    {   // generating in the background: progress + CANCEL
        auto gr = grid.toFloat();
        g.setColour (P.card.withAlpha (0.72f)); g.fillRoundedRectangle (gr, 9);
        const int want = 16, got = juce::jlimit (0, want, ed.proc.labProgress.load());
        g.setColour (P.ink); g.setFont (HF (14.0f));
        g.drawText (lab.busyKind == 2 ? "FINDING THE MAGIC..." : "GENERATING HOOKS...", gr.withHeight (24).withCentre ({ gr.getCentreX(), gr.getCentreY() - 22 }), juce::Justification::centred);
        auto bar = juce::Rectangle<float> (gr.getCentreX() - 140, gr.getCentreY() + 2, 280, 8);
        g.setColour (P.card2); g.fillRoundedRectangle (bar, 4); g.setColour (P.gold); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * (float) got / want), 4);
    }
    for (int k = 0; k < 4 && k < (int) lab.harvest.size(); ++k)
    {
        const auto& F = lab.harvest[(size_t) k]; auto cr = chipRect (k).toFloat();
        g.setColour (P.card2); g.fillRoundedRectangle (cr, 6); g.setColour (ed.laneColour (F.lane)); g.drawRoundedRectangle (cr.reduced (0.5f), 6, 1.2f);
        int lo = 127, hi = 0; for (auto& n : F.notes) { lo = juce::jmin (lo, n.p); hi = juce::jmax (hi, n.p); }
        auto body = cr.reduced (6, 5); body.removeFromRight (30); const float span = (float) juce::jmax (1, F.t1 - F.t0), rh = body.getHeight() / (float) juce::jmax (6, hi - lo + 2);
        for (auto& n : F.notes) g.fillRect (body.getX() + body.getWidth() * (n.t - F.t0) / span, body.getBottom() - (n.p - lo + 1) * rh, juce::jmax (2.0f, body.getWidth() * n.d / span - 1), juce::jmax (2.0f, rh - 0.5f));
        g.setColour (P.dim); g.setFont (HF (9.0f)); g.drawText (juce::String (juce::roundToInt (F.score * 100)), cr.withTrimmedLeft (cr.getWidth() - 30), juce::Justification::centred);
    }
    if (lab.harvest.empty() && harvestArea.getWidth() > 60)
    { g.setColour (P.dim); g.setFont (HF (9.5f, false)); g.drawText ("learned from " + juce::String (ed.proc.taste.keeps) + " keeps", harvestArea, juce::Justification::centredLeft, true); }
}

void HookLabPanel::mouseMove (const juce::MouseEvent& e)
{
    int h = -1; for (int i = 0; i < 16; ++i) if (tileRect (i).contains (e.getPosition())) h = i;
    if (h != hover)
    {
        hover = h; repaint();
        setTooltip (h >= 0 && h < (int) ed.proc.lab.tiles.size() ? "Click = hear it from bar 1, star = keep, right-click = more, drag out = MIDI. piano ok / weak: does it still work as a dry piano line (the PIANO TEST)" : "");
    }
}

void HookLabPanel::mouseDown (const juce::MouseEvent&) { draggedOut = false; grabKeyboardFocus(); }

void HookLabPanel::mouseDrag (const juce::MouseEvent& e)
{   // a tile dragged out of the plugin = its .mid
    if (draggedOut || e.getDistanceFromDragStart() < 8 || combineStep >= 0) return;
    const auto p0 = e.getMouseDownPosition();
    for (int i = 0; i < (int) ed.proc.lab.tiles.size() && i < 16; ++i)
        if (tileRect (i).contains (p0) && ! starRect (i).contains (p0))
        {
            draggedOut = true;
            if (! ed.proc.exportAllowed (this, "arp.hook.midi-drag")) { ed.toast ("Drag-out is part of the full version (UNLOCK)"); return; }
            const auto f = ed.proc.writeHookMidi (i);
            if (f.existsAsFile()) ghosty::exportgate::dragFiles (ed.proc.license, { f.getFullPathName() }, this);
            return;
        }
}

void HookLabPanel::mouseUp (const juce::MouseEvent& e)
{
    if (draggedOut) { draggedOut = false; return; }
    auto& lab = ed.proc.lab;
    for (int k = 0; k < 4 && k < (int) lab.harvest.size(); ++k)
        if (chipRect (k).contains (e.getPosition())) { ed.proc.labHarvestKeep (k); ed.toast ("Motif kept + locked: the harvester learns from it"); return; }
    if (lab.busy) return;
    for (int i = 0; i < (int) lab.tiles.size() && i < 16; ++i)
    {
        if (! tileRect (i).contains (e.getPosition())) continue;
        if (starRect (i).contains (e.getPosition())) { ed.proc.labStar (i); repaint(); return; }
        if (combineStep >= 0)
        {
            pick[combineStep++] = i;
            if (combineStep >= 3)
            {
                const int at = ed.proc.labCombine (pick[0], pick[1], pick[2]);
                combineStep = -1; combineB.on = false; pick[0] = pick[1] = pick[2] = -1;
                ed.toast (at >= 0 ? "COMBINED into tile " + juce::String (at + 1) : at == -2 ? "Every other tile is starred: unstar one to make room" : "COMBINE needs three tiles");
            }
            repaint(); return;
        }
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu m;
            m.addItem (1, "Use this (MELODY lane)"); m.addItem (2, "Find the magic"); m.addItem (3, "Make it a song (16 bars)"); m.addItem (4, "Make it a song (32 bars)"); m.addItem (5, lab.tiles[(size_t) i].star ? "Unstar" : "Star");
            juce::Component::SafePointer<HookLabPanel> sp (this);
            m.showMenuAsync (juce::PopupMenu::Options(), [sp, i] (int r) {
                if (sp == nullptr) return;
                auto& p = sp->ed.proc; p.lab.selected = i;
                if (r == 1) p.labUse (i); else if (r == 2) p.labMagicAsync(); else if (r == 3) p.labSong (i, 16); else if (r == 4) p.labSong (i, 32); else if (r == 5) p.labStar (i);
                sp->repaint(); });
            return;
        }
        if (lab.playing == i) ed.proc.labAudition (-1); else play (i);
        repaint(); return;
    }
}

bool HookLabPanel::keyPressed (const juce::KeyPress& k)
{
    auto& p = ed.proc; auto& lab = p.lab;
    const int n = (int) lab.tiles.size();
    const auto m = k.getModifiers();
    if (m.isCommandDown() || m.isCtrlDown() || m.isAltDown()) return false;   // undo / save go to the editor
    const int kc = juce::CharacterFunctions::toUpperCase ((juce::juce_wchar) k.getTextCharacter());
    if (k == juce::KeyPress::escapeKey) { ed.openHookLab (false); return true; }
    if (n == 0 || lab.busy) return k == juce::KeyPress::spaceKey;
    const int cur = lab.selected < 0 ? 0 : lab.selected;
    auto go = [&] (int t) { t = juce::jlimit (0, n - 1, t); if (lab.playing >= 0) play (t); else { lab.selected = t; repaint(); } };
    if (k == juce::KeyPress::leftKey) { go (cur - 1); return true; }
    if (k == juce::KeyPress::rightKey) { go (cur + 1); return true; }
    if (k == juce::KeyPress::upKey) { go (cur - 4); return true; }
    if (k == juce::KeyPress::downKey) { go (cur + 4); return true; }
    if (k == juce::KeyPress::spaceKey) { if (lab.playing >= 0) p.labAudition (-1); else play (cur); repaint(); return true; }
    if (k == juce::KeyPress::returnKey) { p.labUse (cur); ed.toast ("In the MELODY lane (UNDO to go back)"); return true; }
    if (kc >= '1' && kc <= '9') { play (kc - '1'); return true; }
    if (kc == '0') { play (9); return true; }
    if (kc == 'S') { p.labStar (cur); repaint(); return true; }
    if (kc == 'M') { lab.selected = cur; p.labMagicAsync(); return true; }
    if (kc == 'P') { pianoB.on = ! pianoB.on; pianoB.onClick(); return true; }
    if (kc == 'A') { autoB.on = ! autoB.on; autoB.onClick(); return true; }
    return false;
}
} // namespace arpui
