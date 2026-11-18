// MUTANT ARP view: the melodic sequence generator's panel over the lane editor (see Mutant.h for the engine).
#include "ArpEditor.h"

namespace arpui
{
using namespace arp;
static juce::Font MF (float h, bool b = true) { return ghosty::font (h, b); }

void ArpEditor::openMutant (bool open)
{
    if (mutantPanel == nullptr) return;
    if (open)
    {
        if (more != nullptr) more->setVisible (false);
        if (hookLab != nullptr && hookLab->isVisible()) openHookLab (false);
        if (samplePanel != nullptr) samplePanel->setVisible (false);
        selectLane (LaneArp);
        if (! proc.idea().mutant.active) { proc.mutantSetActive (true); toast ("MUTANT ARP: the ARP lane is now a mutant sequence. GENERATE / MUTATE, lock the notes you like, drag the MIDI out", 4200); }
        mutantPanel->sync(); mutantPanel->setVisible (true); mutantPanel->toFront (false); mutantPanel->grabKeyboardFocus();
    }
    else mutantPanel->setVisible (false);
    refresh();
}

MutantPanel::MutantPanel (ArpEditor& e)
    : ed (e), modeSeg (e.th, { "RAGE", "ALIEN", "DIGITAL", "DREAM", "HYPER", "CYBERPOP" }), rateSeg (e.th, { "1/4", "1/8", "1/16", "1/32", "1/64" }), lenSeg (e.th, { "AUTO", "8", "12", "16", "24", "32" }),
      dirSeg (e.th, { "FORWARD", "REVERSE", "PING-PONG" }), evolveSeg (e.th, { "OFF", "4 BARS", "8 BARS", "16 BARS" }), varySeg (e.th, { "AUTO", "1", "2", "4" }),
      twoSeg (e.th, { "ONE LINE", "ANSWER", "INTERLOCK", "SHADOW" }), bRegSeg (e.th, { "B BELOW", "B ABOVE" }), rotS (e.th, "ROTATE", 0, 31), shadowS (e.th, "SHADOW", 1, 16),
      genB (e.th, "GENERATE"), mutB (e.th, "MUTATE"), octB (e.th, "OCTAVE CHAOS"), rhyB (e.th, "RHYTHM CHAOS"), lockFirstB (e.th, "LOCK FIRST 4"), lockSelB (e.th, "LOCK SELECTED"),
      unlockB (e.th, "UNLOCK ALL"), followB (e.th, "CHORD FOLLOW"), scaleB (e.th, "SCALE LOCK"), classicB (e.th, "CLASSIC ARP"), close (e.th, "CLOSE"), seedPrev (e.th, "<"), seedNext (e.th, ">")
{
    auto& th = e.th;
    setWantsKeyboardFocus (true);
    pill = std::make_unique<DragPill> (e, 1 << LaneArp, "DRAG MIDI");
    for (auto* c : std::initializer_list<juce::Component*> { &modeSeg, &rateSeg, &lenSeg, &dirSeg, &evolveSeg, &varySeg, &twoSeg, &bRegSeg, &rotS, &shadowS, &genB, &mutB, &octB, &rhyB, &lockFirstB, &lockSelB,
                                                             &unlockB, &followB, &scaleB, &classicB, &close, &seedPrev, &seedNext, pill.get() }) addAndMakeVisible (c);
    genB.big = true; genB.tint = [&th] { return th.p.a1; };
    mutB.big = true; mutB.tint = [&th] { return th.p.gold; };
    octB.tint = [&th] { return th.p.pink; }; rhyB.tint = [&th] { return th.p.cyan; };
    lockFirstB.tint = [&th] { return th.p.gold; }; lockSelB.tint = [&th] { return th.p.gold; }; unlockB.tint = [&th] { return th.p.dim; };
    followB.toggle = true; followB.tint = [&th] { return th.p.green; }; scaleB.toggle = true; scaleB.tint = [&th] { return th.p.green; };
    classicB.tint = [&th] { return th.p.dim; };
    modeSeg.tint = [&th] { return th.p.a1; }; twoSeg.tint = [&th] { return th.p.pink; }; evolveSeg.tint = [&th] { return th.p.cyan; }; rotS.tint = [&th] { return th.p.cyan; }; shadowS.tint = [&th] { return th.p.pink; };
    genB.setTooltip ("A new mutant cell over the current chords (locked notes stay exactly). G");
    mutB.setTooltip ("A variation of the cell: a few unlocked steps change (pitch, octave, rest, ratchet, a fill). M");
    octB.setTooltip ("OCTAVE CHAOS: only the register moves (octave pops, leaps to the top). O");
    rhyB.setTooltip ("RHYTHM CHAOS: only the rhythm changes (rests, holds, ratchets, 1/64 bursts, a fill). R");
    lockFirstB.setTooltip ("LOCK NOTES: keep the first four steps of the cell exactly through GENERATE / MUTATE / EVOLVE (click any step below to lock it alone). L");
    lockSelB.setTooltip ("Lock the steps of the ARP notes selected in the roll");
    unlockB.setTooltip ("Unlock every step");
    followB.setTooltip ("CHORD FOLLOW: every step is re-pitched to the chord under it (off = the cell stays on the first chord, a pure ostinato)");
    scaleB.setTooltip ("SCALE LOCK: nothing outside the key; off lets ALIEN write chromatic approach notes that resolve by a semitone");
    classicB.setTooltip ("Hand the ARP lane back to the classic figure engine (UNDO brings the mutant back)");
    modeSeg.setTooltip ("RAGE: fast octave-jumping sequences. ALIEN: unusual intervals, chromatic approaches. DIGITAL: video-game plucks, alternating pitches. DREAM: floating overlapping lines with glides. HYPER: bright sugar-rush chaos on a club-kick accent grid, stutters, whiplash octaves. CYBERPOP: futuristic pop hooks drawn from the style table learned from MIDI packs (aggregate statistics only)");
    rateSeg.setTooltip ("Step size"); lenSeg.setTooltip ("Cell length in steps (AUTO = the mode picks: a 12 or 24 step cell drifts against the bar and re-syncs every variation)");
    dirSeg.setTooltip ("How the cell is read"); rotS.setTooltip ("Start the cell this many steps in");
    evolveSeg.setTooltip ("EVOLVE: the cell mutates a little more every variation, wrapping after 4 / 8 / 16 bars (the idea grows to that length)");
    varySeg.setTooltip ("A variation arrives every this many bars (AUTO = the mode's own: RAGE / DIGITAL / HYPER every 2, ALIEN / DREAM every 4)");
    twoSeg.setTooltip ("TWO LINES: a second line in the MELODY lane. ANSWER = call and response (the first line calls, the second answers an octave away, mirrored every other time). INTERLOCK = a hocket: its own cell only where the first line rests. SHADOW = a canon: the first line again, late, an octave away");
    bRegSeg.setTooltip ("Where the second line sits"); shadowS.setTooltip ("SHADOW: how many steps late the second line follows");
    pill->setTooltip ("Drag the mutant line(s) out as a .mid file (ARP, + MELODY with TWO LINES)");
    knobNames = { "NOTE DENSITY", "OCTAVE JUMP", "INTERVAL CHAOS", "RETRIGGER", "STEP SKIP", "VELOCITY RANDOM", "GATE", "SWING", "PATTERN EVOLVE", "GLIDE" };
    static const char* tips[] { "How many steps sound (left = sparse, right = every step, extra ratchets)", "How often the line jumps an octave (and how wide its register is)",
                                "Leaps, neighbour notes and (SCALE LOCK off) chromatic approaches; also loosens the turn-back-after-a-leap rule", "1/32 pairs, triplets and 1/64 bursts",
                                "Step probability: unlocked, unaccented steps sometimes skip", "Velocity jitter around the accent pattern", "Note length as a share of the step (right = overlapping)",
                                "Swing on the off 16ths (baked into the notes and the MIDI)", "How much changes at every variation / EVOLVE step", "Glides (portamento) between close notes" };
    for (int k = 0; k < 10; ++k)
    {
        auto* s = knobs.add (new juce::Slider (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox));
        s->setRange (0.0, 1.0, 0.01); s->setPopupDisplayEnabled (true, true, this); s->setTooltip (tips[k]);
        s->textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100)) + "%"; };
        s->onValueChange = [this, k] { if (syncing) return; pendingRegen = pendingRegen || k <= 4; repaint (knobRow1); repaint (knobRow2); startTimer (150); };
        addAndMakeVisible (s);
    }
    auto settings = [this] (bool regen) { if (! syncing) commit (regen); };
    modeSeg.onChange = [this] (int b) { if (syncing) return; const int m = mutantModeOfButton (b); auto P = ArpProcessor::mutantDefaults (m, ed.proc.idea().g.bpm); const auto& old = ed.proc.idea().mutant.P; P.seed = old.seed; P.div = old.div; P.evolveBars = old.evolveBars; P.twoLine = old.twoLine; P.chordFollow = old.chordFollow; ed.proc.mutantSetParams (P, true); sync(); };
    rateSeg.onChange = [settings] (int) { settings (false); };
    lenSeg.onChange = [settings] (int) { settings (true); };
    dirSeg.onChange = [settings] (int) { settings (false); };
    rotS.onChange = [settings] (int) { settings (false); };
    evolveSeg.onChange = [settings] (int) { settings (false); };
    varySeg.onChange = [settings] (int) { settings (false); };
    twoSeg.onChange = [settings] (int) { settings (false); };
    bRegSeg.onChange = [settings] (int) { settings (false); };
    shadowS.onChange = [settings] (int) { settings (false); };
    followB.onClick = [settings] { settings (false); };
    scaleB.onClick = [settings] { settings (true); };
    genB.onClick = [this] { ed.proc.mutantGenerate (true); sync(); ed.toast ("New mutant cell (locked steps kept)"); };
    mutB.onClick = [this] { ed.proc.mutantMutate (0, 0.35); sync(); ed.toast ("MUTATE: generation " + juce::String (ed.proc.idea().mutant.generation)); };
    octB.onClick = [this] { ed.proc.mutantMutate (1, 0.35); sync(); ed.toast ("OCTAVE CHAOS"); };
    rhyB.onClick = [this] { ed.proc.mutantMutate (2, 0.4); sync(); ed.toast ("RHYTHM CHAOS"); };
    lockFirstB.onClick = [this] { ed.proc.mutantLockFirst (4); sync(); ed.toast ("The first 4 steps are locked: GENERATE / MUTATE / EVOLVE keep them exactly"); };
    lockSelB.onClick = [this] { if (ed.proc.selLane != LaneArp || ed.roll.sel.empty()) { ed.toast ("Select some ARP notes in the roll first (close the panel, drag a box, open it again)"); return; } ed.proc.mutantLockSelected (ed.roll.sel); sync(); ed.toast (juce::String (mutantLockedCount (ed.proc.idea().mutant.cell)) + " steps locked"); };
    unlockB.onClick = [this] { ed.proc.mutantLockFirst (0); sync(); };
    classicB.onClick = [this] { ed.proc.mutantSetActive (false); ed.openMutant (false); ed.toast ("Classic ARP figure is back (UNDO brings the mutant back)"); };
    seedPrev.onClick = [this] { ed.proc.mutantSeedStep (-1); sync(); };
    seedNext.onClick = [this] { ed.proc.mutantSeedStep (1); sync(); };
    close.onClick = [this] { ed.openMutant (false); };
}

MutantParams MutantPanel::paramsFromUi() const
{
    MutantParams P = ed.proc.idea().mutant.P;
    P.mode = modeSeg.sel < 0 ? P.mode : mutantModeOfButton (modeSeg.sel);   // (no button lit = an older session's FLUTE: kept)
    static const int divs[] { 4, 8, 16, 32, 64 }; P.div = divs[juce::jlimit (0, 4, rateSeg.sel)];
    static const int lens[] { 0, 8, 12, 16, 24, 32 }; P.cellSteps = lens[juce::jlimit (0, 5, lenSeg.sel)];
    P.direction = dirSeg.sel; P.rotate = rotS.value;
    static const int ev[] { 0, 4, 8, 16 }; P.evolveBars = ev[juce::jlimit (0, 3, evolveSeg.sel)];
    static const int vb[] { 0, 1, 2, 4 }; P.variationBars = vb[juce::jlimit (0, 3, varySeg.sel)];
    P.twoLine = twoSeg.sel; P.lineBReg = bRegSeg.sel == 0 ? -1 : 1; P.shadowSteps = shadowS.value;
    P.chordFollow = followB.on; P.scaleLock = scaleB.on;
    const double* dst[] { &P.density, &P.octaveJump, &P.intervalChaos, &P.retrigger, &P.stepSkip, &P.velRandom, &P.gate, &P.swing, &P.evolve, &P.glide };
    for (int k = 0; k < 10 && k < knobs.size(); ++k) *const_cast<double*> (dst[k]) = knobs[k]->getValue();
    return P;
}
void MutantPanel::commit (bool regenerate)
{
    stopTimer();
    const auto P = paramsFromUi(); const bool regen = regenerate || pendingRegen; pendingRegen = false;
    ed.proc.mutantSetParams (P, regen);
    sync();
}
void MutantPanel::timerCallback()
{
    for (auto* s : knobs) if (s->isMouseButtonDown()) return;
    commit (false);
}

void MutantPanel::sync()
{
    syncing = true;
    const auto& Mu = ed.proc.idea().mutant; const auto& P = Mu.P;
    modeSeg.sel = mutantButtonOfMode (juce::jlimit (0, kMutantModes - 1, P.mode));   // -1 = FLUTE (older sessions): no button lit, the blurb says so
    rateSeg.sel = P.div <= 4 ? 0 : P.div <= 8 ? 1 : P.div <= 16 ? 2 : P.div <= 32 ? 3 : 4;
    lenSeg.sel = P.cellSteps <= 0 ? 0 : P.cellSteps <= 8 ? 1 : P.cellSteps <= 12 ? 2 : P.cellSteps <= 16 ? 3 : P.cellSteps <= 24 ? 4 : 5;
    dirSeg.sel = juce::jlimit (0, 2, P.direction); rotS.hi = juce::jmax (1, Mu.cell.len - 1); rotS.value = juce::jlimit (0, rotS.hi, P.rotate);
    evolveSeg.sel = P.evolveBars >= 16 ? 3 : P.evolveBars >= 8 ? 2 : P.evolveBars >= 4 ? 1 : 0;
    varySeg.sel = P.variationBars >= 4 ? 3 : P.variationBars >= 2 ? 2 : P.variationBars >= 1 ? 1 : 0;
    twoSeg.sel = juce::jlimit (0, kTwoLines - 1, P.twoLine); bRegSeg.sel = P.lineBReg > 0 ? 1 : 0; shadowS.value = juce::jlimit (1, 16, P.shadowSteps);
    bRegSeg.setEnabled (P.twoLine != TwoOff); shadowS.setEnabled (P.twoLine == TwoShadow);
    followB.on = P.chordFollow; scaleB.on = P.scaleLock;
    const double vals[] { P.density, P.octaveJump, P.intervalChaos, P.retrigger, P.stepSkip, P.velRandom, P.gate, P.swing, P.evolve, P.glide };
    for (int k = 0; k < 10 && k < knobs.size(); ++k) if (! knobs[k]->isMouseButtonDown()) knobs[k]->setValue (vals[k], juce::dontSendNotification);
    pill->mask = (1 << LaneArp) | (P.twoLine != TwoOff ? (1 << LaneMelody) : 0);
    pill->text = P.twoLine != TwoOff ? "DRAG MIDI (2 LINES)" : "DRAG MIDI";
    lockFirstB.text = mutantLockedCount (Mu.cell) >= 4 && Mu.cell.len >= 4 && Mu.cell.s[0].lock && Mu.cell.s[1].lock && Mu.cell.s[2].lock && Mu.cell.s[3].lock ? "FIRST 4 LOCKED" : "LOCK FIRST 4";
    syncing = false;
    for (auto* c : getChildren()) c->repaint();
    repaint();
}

void MutantPanel::resized()
{
    auto r = getLocalBounds().reduced (14, 10);
    auto top = r.removeFromTop (30);
    close.setBounds (top.removeFromRight (80).reduced (0, 2)); top.removeFromRight (8);
    classicB.setBounds (top.removeFromRight (110).reduced (0, 2)); top.removeFromRight (8);
    seedNext.setBounds (top.removeFromRight (26).reduced (0, 2)); seedArea = top.removeFromRight (96); seedPrev.setBounds (top.removeFromRight (26).reduced (0, 2));
    top.removeFromRight (8); top.removeFromLeft (150); titleText = top;
    r.removeFromTop (6);
    auto a = r.removeFromTop (30);
    modeSeg.setBounds (a.removeFromLeft (500).reduced (0, 2)); a.removeFromLeft (12); blurbRow = a;
    r.removeFromTop (8);
    auto b = r.removeFromTop (36);
    genB.setBounds (b.removeFromLeft (150)); b.removeFromLeft (6); mutB.setBounds (b.removeFromLeft (120)); b.removeFromLeft (6);
    octB.setBounds (b.removeFromLeft (124)); b.removeFromLeft (4); rhyB.setBounds (b.removeFromLeft (124)); b.removeFromLeft (10);
    pill->setBounds (b.removeFromRight (150).reduced (0, 2)); b.removeFromRight (10);
    unlockB.setBounds (b.removeFromRight (96)); b.removeFromRight (4); lockSelB.setBounds (b.removeFromRight (112)); b.removeFromRight (4); lockFirstB.setBounds (b.removeFromRight (juce::jmax (100, b.getWidth())));
    r.removeFromTop (8);
    rowC = r.removeFromTop (28);
    {
        auto c = rowC; c.removeFromLeft (40); rateSeg.setBounds (c.removeFromLeft (200).reduced (0, 2)); c.removeFromLeft (12 + 52);
        lenSeg.setBounds (c.removeFromLeft (220).reduced (0, 2)); c.removeFromLeft (12 + 30);
        dirSeg.setBounds (c.removeFromLeft (200).reduced (0, 2)); c.removeFromLeft (10);
        rotS.setBounds (c.removeFromLeft (juce::jmin (110, c.getWidth())).reduced (0, 1));
    }
    r.removeFromTop (6);
    rowD = r.removeFromTop (28);
    {
        auto d = rowD; d.removeFromLeft (52); evolveSeg.setBounds (d.removeFromLeft (230).reduced (0, 2)); d.removeFromLeft (12 + 36);
        varySeg.setBounds (d.removeFromLeft (150).reduced (0, 2)); d.removeFromLeft (12);
        scaleB.setBounds (d.removeFromRight (96).reduced (0, 1)); d.removeFromRight (6); followB.setBounds (d.removeFromRight (112).reduced (0, 1));
    }
    r.removeFromTop (6);
    rowE = r.removeFromTop (28);
    {
        auto d = rowE; d.removeFromLeft (64); twoSeg.setBounds (d.removeFromLeft (330).reduced (0, 2)); d.removeFromLeft (12);
        bRegSeg.setBounds (d.removeFromLeft (150).reduced (0, 2)); d.removeFromLeft (12); shadowS.setBounds (d.removeFromLeft (130).reduced (0, 1));
    }
    r.removeFromTop (8);
    knobRow1 = r.removeFromTop (40); r.removeFromTop (4); knobRow2 = r.removeFromTop (40);
    for (int k = 0; k < knobs.size(); ++k)
    {
        auto row = k < 5 ? knobRow1 : knobRow2; const int w = row.getWidth() / 5;
        auto cell = row.withX (row.getX() + (k % 5) * w).withWidth (w).reduced (6, 0); cell.removeFromTop (14);
        knobs[k]->setBounds (cell.reduced (0, 4));
    }
    r.removeFromTop (8);
    cellStrip = r.removeFromTop (juce::jmax (90, r.getHeight() * 45 / 100));
    r.removeFromTop (8);
    preview = r;
}

juce::Rectangle<float> MutantPanel::stepRect (int i) const
{
    const auto& c = ed.proc.idea().mutant.cell; const int n = juce::jmax (1, c.len);
    auto s = cellStrip.toFloat(); s.removeFromTop (16);
    const float w = s.getWidth() / (float) n;
    return { s.getX() + i * w, s.getY(), w, s.getHeight() };
}
int MutantPanel::stepAt (juce::Point<float> p) const
{
    const auto& c = ed.proc.idea().mutant.cell; if (! c.valid()) return -1;
    for (int i = 0; i < c.len; ++i) if (stepRect (i).contains (p)) return i;
    return -1;
}
void MutantPanel::mouseMove (const juce::MouseEvent& e) { const int h = stepAt (e.position); if (h != hoverStep) { hoverStep = h; repaint (cellStrip); } }
void MutantPanel::mouseUp (const juce::MouseEvent& e)
{
    const int i = stepAt (e.position); if (i < 0) return;
    const auto& c = ed.proc.idea().mutant.cell; const bool on = ! c.s[(size_t) i].lock;
    ed.proc.mutantLockStep (i, on); sync(); ed.toast (on ? "Step " + juce::String (i + 1) + " locked" : "Step " + juce::String (i + 1) + " unlocked");
}
bool MutantPanel::keyPressed (const juce::KeyPress& k)
{
    const auto ch = (juce::juce_wchar) juce::CharacterFunctions::toUpperCase (k.getTextCharacter());
    if (ch == 'G') { genB.onClick(); return true; }
    if (ch == 'M') { mutB.onClick(); return true; }
    if (ch == 'O') { octB.onClick(); return true; }
    if (ch == 'R') { rhyB.onClick(); return true; }
    if (ch == 'L') { lockFirstB.onClick(); return true; }
    if (k == juce::KeyPress::escapeKey) { ed.openMutant (false); return true; }
    return false;
}

void MutantPanel::paint (juce::Graphics& g)
{
    const auto& P = ed.th.p; auto R = getLocalBounds().toFloat();
    fillCard (g, R, 14, P); g.setColour (P.pink); g.drawRoundedRectangle (R.reduced (1), 14, 1.8f);
    const auto& I = ed.proc.idea(); const auto& Mu = I.mutant; const auto& MP = Mu.P;
    g.setColour (P.ink); g.setFont (MF (17.0f)); g.drawText ("MUTANT ARP", 16, 10, 150, 26, juce::Justification::centredLeft);
    g.setColour (P.dim); g.setFont (MF (10.5f, false));
    g.drawText ("MELODIC SEQUENCE GENERATOR  -  G generate, M mutate, O octave, R rhythm, L lock first 4  -  click a step to lock it", titleText, juce::Justification::centredLeft, true);
    g.setFont (MF (9.5f)); g.drawText ("SEED " + juce::String ((int) (MP.seed % 1000000)) + (Mu.generation > 0 ? "  +" + juce::String (Mu.generation) : juce::String()), seedArea, juce::Justification::centred, true);
    g.setColour (P.a1); g.setFont (MF (10.5f, false)); g.drawText (mutantModeBlurb (MP.mode), blurbRow, juce::Justification::centredLeft, true);
    g.setColour (P.dim); g.setFont (MF (10.0f));
    g.drawText ("RATE", rowC.getX(), rowC.getY(), 38, rowC.getHeight(), juce::Justification::centredLeft);
    g.drawText ("LENGTH", rateSeg.getRight() + 12, rowC.getY(), 52, rowC.getHeight(), juce::Justification::centredLeft);
    g.drawText ("READ", lenSeg.getRight() + 12, rowC.getY(), 30, rowC.getHeight(), juce::Justification::centredLeft);
    g.drawText ("EVOLVE", rowD.getX(), rowD.getY(), 50, rowD.getHeight(), juce::Justification::centredLeft);
    g.drawText ("VARY", evolveSeg.getRight() + 12, rowD.getY(), 36, rowD.getHeight(), juce::Justification::centredLeft);
    g.drawText ("2 LINES", rowE.getX(), rowE.getY(), 62, rowE.getHeight(), juce::Justification::centredLeft);
    for (int k = 0; k < knobs.size(); ++k)
    {
        auto row = k < 5 ? knobRow1 : knobRow2; const int w = row.getWidth() / 5; auto cell = row.withX (row.getX() + (k % 5) * w).withWidth (w).reduced (6, 0);
        g.setColour (P.dim); g.setFont (MF (9.5f)); g.drawText (knobNames[k], cell.removeFromTop (14), juce::Justification::centredLeft);
        g.setColour (P.ink); g.drawText (juce::String (juce::roundToInt (knobs[k]->getValue() * 100)) + "%", cell.withTrimmedBottom (26), juce::Justification::centredRight);
    }
    // ---- the cell strip: each step a bar of height = register, locks, rests, ratchets, accents
    {
        auto s = cellStrip.toFloat(); auto head = s.removeFromTop (16);
        g.setColour (P.dim); g.setFont (MF (9.5f));
        g.drawText ("CELL  " + juce::String (Mu.cell.len) + " STEPS  -  " + juce::String (mutantLockedCount (Mu.cell)) + " LOCKED  -  gold = locked, dot = rest, bars = ratchet, > = accent, ~ = glide, p = pedal, ^ = top ping, g = grace, t = trill", head, juce::Justification::centredLeft, true);
        g.setColour (P.roll); g.fillRoundedRectangle (s, 8);
        if (Mu.cell.valid())
        {
            int lo = 99, hi = -99; std::vector<int> pitch ((size_t) Mu.cell.len, 0);
            for (int i = 0; i < Mu.cell.len; ++i) { const auto& st = Mu.cell.s[(size_t) i]; if (st.rest) continue; pitch[(size_t) i] = mutantPitch (st, I.H, 0, MP); lo = juce::jmin (lo, pitch[(size_t) i]); hi = juce::jmax (hi, pitch[(size_t) i]); }
            if (hi < lo) { lo = 60; hi = 72; }
            const Colour arpC = ed.laneColour (LaneArp);
            for (int i = 0; i < Mu.cell.len; ++i)
            {
                const auto& st = Mu.cell.s[(size_t) i]; auto rc = stepRect (i).reduced (1.5f, 4.0f);
                if (i == hoverStep) { g.setColour (P.ink.withAlpha (0.08f)); g.fillRoundedRectangle (rc, 4); }
                if (st.lock) { g.setColour (P.gold.withAlpha (0.18f)); g.fillRoundedRectangle (rc, 4); g.setColour (P.gold); g.drawRoundedRectangle (rc, 4, 1.2f); }
                else { g.setColour (P.grid); g.drawRoundedRectangle (rc, 4, 0.8f); }
                auto body = rc.reduced (2, 14);
                if (st.rest) { g.setColour (P.dim.withAlpha (0.6f)); g.fillEllipse (body.getCentreX() - 2.5f, body.getBottom() - 6, 5, 5); }
                else
                {
                    const float y = body.getBottom() - (float) (pitch[(size_t) i] - lo) / (float) juce::jmax (1, hi - lo) * (body.getHeight() - 10);
                    const int bars = st.ratchet == 4 ? 4 : st.ratchet;
                    const float bw = (body.getWidth() - (bars - 1) * 1.5f) / (float) bars;
                    for (int b = 0; b < bars; ++b)
                    {
                        auto nb = juce::Rectangle<float> (body.getX() + b * (bw + 1.5f), y - 4, bw, st.hold ? 16.0f : 9.0f);
                        { Colour nc = st.col != 0 ? P.cyan : st.role == 1 ? P.a3 : st.role == 2 ? P.gold : arpC; g.setColour (st.acc ? nc : nc.withAlpha (0.65f)); }
                        g.fillRoundedRectangle (nb, 2.5f);
                    }
                    juce::String tag; if (st.acc) tag << ">"; if (st.glide) tag << "~"; if (st.role == 1) tag << "p"; if (st.role == 2) tag << "^"; if (st.col == 2 || st.col == -2) tag << "#"; if (st.grace) tag << "g"; if (st.trill) tag << "t"; if (st.prob < 0.999) tag << "?";
                    g.setColour (P.dim); g.setFont (MF (9.0f)); g.drawText (tag, rc.withTrimmedBottom (rc.getHeight() - 13), juce::Justification::centred);
                }
                g.setColour (P.dim.withAlpha (0.8f)); g.setFont (MF (8.5f, false)); g.drawText (juce::String (i + 1), rc.withTop (rc.getBottom() - 12), juce::Justification::centred);
            }
            // the playhead's step
            const double pt = ed.proc.playTick.load();
            if (pt >= 0) { const int cs = mutantStepAt (Mu.cell, MP, (int) pt); if (cs >= 0) { g.setColour (P.gold.withAlpha (0.5f)); g.drawRoundedRectangle (stepRect (cs).reduced (1.5f, 4.0f), 4, 2.0f); } }
        }
    }
    // ---- preview: the ARP lane (+ MELODY with TWO LINES) over the whole idea
    {
        auto pv = preview.toFloat(); auto head = pv.removeFromTop (14);
        g.setColour (P.dim); g.setFont (MF (9.5f));
        g.drawText ("PREVIEW  " + juce::String (I.total / BAR) + " BARS  -  ARP" + (MP.twoLine != TwoOff ? juce::String ("  +  MELODY (line B)") : juce::String()) + "  -  the variation bar is shaded", head, juce::Justification::centredLeft, true);
        g.setColour (P.roll); g.fillRoundedRectangle (pv, 8);
        auto body = pv.reduced (6, 4); const int total = juce::jmax (BAR, I.total);
        const int V = juce::jmax (1, mutantVariationBars (MP));
        for (int b = 0; b < total / BAR; ++b)
        {
            const float x0 = body.getX() + body.getWidth() * (float) (b * BAR) / total, x1 = body.getX() + body.getWidth() * (float) ((b + 1) * BAR) / total;
            if (b % V == V - 1 && (V >= 2 || b > 0)) { g.setColour (P.pink.withAlpha (0.07f)); g.fillRect (x0, body.getY(), x1 - x0, body.getHeight()); }
            g.setColour (P.grid); g.drawVerticalLine ((int) x0, body.getY(), body.getBottom());
        }
        int lo = 127, hi = 0;
        for (int l : { (int) LaneArp, (int) LaneMelody }) { if (l == LaneMelody && MP.twoLine == TwoOff) continue; for (auto& n : I.lanes[l].notes) { lo = juce::jmin (lo, n.p); hi = juce::jmax (hi, n.p); } }
        if (hi < lo) { lo = 60; hi = 72; } lo -= 1; hi += 1;
        const float rh = body.getHeight() / (float) (hi - lo + 1);
        for (int l : { (int) LaneMelody, (int) LaneArp })
        {
            if (l == LaneMelody && MP.twoLine == TwoOff) continue;
            const Colour c = ed.laneColour (l);
            for (auto& n : I.lanes[l].notes)
            {
                const float x = body.getX() + body.getWidth() * (float) n.t / total, w = juce::jmax (1.5f, body.getWidth() * (float) n.d / total - 0.5f);
                const float y = body.getBottom() - (float) (n.p - lo + 1) * rh;
                g.setColour (n.lock ? P.gold : c.withAlpha (0.45f + 0.55f * n.v)); g.fillRect (x, y, w, juce::jmax (1.5f, rh - 0.8f));
            }
        }
        const double pt = ed.proc.playTick.load();
        if (pt >= 0 && pt < total) { const float x = body.getX() + body.getWidth() * (float) pt / total; g.setColour (P.gold); g.fillRect (x - 0.5f, body.getY(), 1.5f, body.getHeight()); }
    }
}
} // namespace arpui
