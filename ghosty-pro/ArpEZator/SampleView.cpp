// YOUR SAMPLE view: the panel (waveform + markers + settings + lanes) and the editor's open / load.
#include "ArpEditor.h"

namespace arpui
{
using namespace arp;
static juce::Font SF (float h, bool b = true) { return ghosty::font (h, b); }

void ArpEditor::openSample (bool open)
{
    if (samplePanel == nullptr) return;
    if (open)
    {
        if (hookLab != nullptr && hookLab->isVisible()) openHookLab (false);
        if (more != nullptr) more->setVisible (false);
        samplePanel->sync(); samplePanel->setVisible (true); samplePanel->toFront (false);
    }
    else samplePanel->setVisible (false);
    refresh();
}
void ArpEditor::loadSampleFile (const juce::File& f)
{
    juce::String msg; const bool ok = proc.loadSample (f, msg);
    toast (msg, 4200);
    if (ok) openSample (true);
    refresh();
}

// ---------------------------------------------------------------- the panel
SamplePanel::SamplePanel (ArpEditor& e)
    : ed (e), thr (e.th), loadB (e.th, "LOAD..."), recentB (e.th, "RECENT"), clearB (e.th, "CLEAR"), autoLoopB (e.th, "AUTO LOOP"), revB (e.th, "REVERSE"), formantB (e.th, "KEEP FORMANTS"), close (e.th, "CLOSE"),
      modeSeg (e.th, { "ONE-SHOT", "LOOP", "NOTE LENGTH", "SLICES" }), rootS (e.th, "ROOT", 0, 127), fineS (e.th, "FINE", -100, 100)
{
    auto& th = e.th;
    for (auto* c : std::initializer_list<juce::Component*> { &loadB, &recentB, &clearB, &autoLoopB, &revB, &formantB, &close, &modeSeg, &rootS, &fineS }) addAndMakeVisible (c);
    for (auto* nm : { "CHORDS", "ARP", "MELODY", "BASS" }) { auto* b = laneB.add (new Btn (th, nm)); addAndMakeVisible (b); b->toggle = true; }
    addAndMakeVisible (waveC);
    waveC.palette = [this] {
        const auto& P = ed.th.p; ghosty::WaveRegion::Palette c;
        c.bg = P.roll; c.line = P.line; c.wave = P.cyan; c.waveOut = P.dim; c.ink = P.ink; c.dim = P.dim; c.start = P.green; c.end = P.pink; c.loop = P.gold; c.card = P.card;
        c.label = P.textOn (P.green); return c; };
    waveC.onChange = [this] (const ghosty::WaveRegion::Region& r) {
        push ([r] (arpsmp::Params& p) {
            const bool wasLoop = p.mode == 1 || (p.mode == 2 && p.fitLoop);
            p.start = r.start; p.end = r.end; p.loopStart = r.loopStart; p.loopEnd = r.loopEnd;
            if (r.loopOn != wasLoop) { if (p.mode == 2) p.fitLoop = r.loopOn; else p.mode = r.loopOn ? 1 : 0; } });   // (NOTE LENGTH: the loop = HOLD IN LOOP)
        sync(); };
    waveC.onAudition = [this] (bool down, float) { ed.proc.auditionSampleRoot (down); };
    waveC.canExport = [this] { return ed.proc.exportAllowed (&waveC, "arp.sample.region-wav"); };
    waveC.performDrag = [this] (const juce::StringArray& f) { ghosty::exportgate::dragFiles (ed.proc.license, f, &waveC); };
    waveC.exportRegion = [this] (float a, float b) { return writeRegion (a, b); };
    waveC.onMessage = [this] (const juce::String& m) { ed.toast (m, 2200); };
    waveC.emptyText = "drop a sample here";
    for (int l = 0; l < 4; ++l) laneB[l]->tint = [this, l] { return ed.laneColour (l); };
    revB.toggle = true; formantB.toggle = true;
    for (auto* b : { &loadB, &recentB, &autoLoopB }) b->tint = [&th] { return th.p.cyan; };
    revB.tint = [&th] { return th.p.pink; }; formantB.tint = [&th] { return th.p.a2; }; modeSeg.tint = [&th] { return th.p.cyan; };
    rootS.fmt = [] (int v) { return juce::MidiMessage::getMidiNoteName (v, true, true, 4); };
    fineS.fmt = [] (int v) { return (v > 0 ? "+" : "") + juce::String (v) + " ct"; };
    for (auto* st : { &rootS, &fineS }) st->tint = [&th] { return th.p.gold; };
    loadB.setTooltip ("Load a sample (WAV, AIFF, FLAC, OGG, MP3; up to 60 s). Or drop one on this panel / on PREVIEW SOUND");
    modeSeg.setTooltip ("ONE-SHOT: every note plays the region from START to END (a vocal phrase rings out). LOOP: holds inside the loop points (crossfaded). NOTE LENGTH: the note's length decides how much plays (TRIM or STRETCH to fit). SLICES: the sample is chopped at its hits, one slice per note");
    revB.setTooltip ("Play the region backwards"); formantB.setTooltip ("KEEP FORMANTS: pitch it without the chipmunk / giant effect (a voice stays a voice)");
    autoLoopB.setTooltip ("Find a seamless loop inside START..END");
    rootS.setTooltip ("ROOT: the note the sample plays at its own pitch (detected on load)");
    for (int l = 0; l < 4; ++l) laneB[l]->setTooltip ("PLAY SAMPLE ON this lane: lit = the lane plays YOUR SAMPLE, off = the lane's own sound");
    struct K { juce::Slider* s; double lo, hi, def; const char* tip; };
    const K ks[] { { &aS, 0.0005, 2.0, 0.003, "ATTACK (s)" }, { &dS, 0.01, 4.0, 0.5, "DECAY (s)" }, { &sS, 0.0, 1.0, 1.0, "SUSTAIN" }, { &rS, 0.005, 4.0, 0.25, "RELEASE (s)" },
                   { &cutS, 0.0, 1.0, 1.0, "FILTER: low-pass cutoff" }, { &toneS, -1.0, 1.0, 0.0, "TONE: darker <-> brighter" }, { &volS, -24.0, 12.0, 0.0, "VOLUME (dB)" }, { &xfS, 0.002, 0.25, 0.03, "LOOP CROSSFADE (s)" } };
    for (auto& k : ks)
    {
        addAndMakeVisible (*k.s); k.s->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag); k.s->setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        k.s->setRange (k.lo, k.hi); k.s->setDoubleClickReturnValue (true, k.def); k.s->setTooltip (k.tip); k.s->setPopupDisplayEnabled (true, true, this);
        if (k.s == &aS || k.s == &dS || k.s == &rS || k.s == &xfS) k.s->setSkewFactorFromMidPoint (k.s == &xfS ? 0.03 : 0.25);
    }
    auto bind = [this] (juce::Slider& s, std::function<void (arpsmp::Params&, float)> fn) { s.onValueChange = [this, &s, fn] { const float v = (float) s.getValue(); push ([fn, v] (arpsmp::Params& p) { fn (p, v); }); }; };
    bind (aS, [] (arpsmp::Params& p, float v) { p.a = v; }); bind (dS, [] (arpsmp::Params& p, float v) { p.d = v; }); bind (sS, [] (arpsmp::Params& p, float v) { p.s = v; }); bind (rS, [] (arpsmp::Params& p, float v) { p.r = v; });
    bind (cutS, [] (arpsmp::Params& p, float v) { p.cutoff = v; }); bind (toneS, [] (arpsmp::Params& p, float v) { p.tone = v; }); bind (volS, [] (arpsmp::Params& p, float v) { p.volDb = v; }); bind (xfS, [] (arpsmp::Params& p, float v) { p.xfade = v; });
    modeSeg.onChange = [this] (int i) {
        push ([i] (arpsmp::Params& p) { p.mode = i; });
        if (i == 3 && ed.proc.sampleParams().nSlices == 0) { const int n = ed.proc.sliceSample (0.5f); ed.toast ("SLICES: " + juce::String (n) + " slices, one per note (IN ORDER: note 1 = slice 1, ...)", 3000); }
        sync(); resized(); };
    for (auto* c : std::initializer_list<juce::Component*> { &fitSeg, &orderSeg, &fitLoopB, &sliceB, &pitchedB, &patternB, &fadeS }) addChildComponent (c);
    fitLoopB.toggle = pitchedB.toggle = true;
    for (auto* b : { &fitLoopB, &pitchedB }) b->tint = [&th] { return th.p.gold; };
    for (auto* b : { &sliceB, &patternB }) b->tint = [&th] { return th.p.cyan; };
    fitSeg.tint = orderSeg.tint = [&th] { return th.p.gold; }; fadeS.tint = [&th] { return th.p.gold; };
    fadeS.fmt = [] (int v) { return juce::String (v) + " ms"; }; fadeS.reset = 20;
    fitSeg.setTooltip ("NOTE LENGTH: TRIM plays at the key's pitch and stops where the note ends; STRETCH time-stretches START..END to fill each note exactly (the pitch still follows the key)");
    fitLoopB.setTooltip ("HOLD IN LOOP: a note longer than START..END keeps playing inside the loop points (else it stops at END)");
    fadeS.setTooltip ("END FADE: how fast a note fades when it ends (NOTE LENGTH and SLICES)");
    sliceB.setTooltip ("SLICE TO NOTES: chop START..END at its hits (transients); each note then plays its own slice. Click again to re-slice (right-click: finer)");
    orderSeg.setTooltip ("IN ORDER: the 1st note of the lane plays slice 1, the 2nd slice 2, ... BY PITCH: the note's distance from ROOT picks the slice (C = 1, C# = 2, ...)");
    pitchedB.setTooltip ("PITCHED: slices follow the notes' pitch (off = every slice at its own pitch, like a chopped break)");
    patternB.setTooltip ("PATTERN FROM SLICES: write the selected lane's notes on the slices' own rhythm (one undo step)");
    fitSeg.onChange = [this] (int i) { push ([i] (arpsmp::Params& p) { p.fitStretch = i == 1; }); };
    orderSeg.onChange = [this] (int i) { push ([i] (arpsmp::Params& p) { p.sliceOrder = i; }); };
    fitLoopB.onClick = [this] { const bool on = fitLoopB.on; push ([on] (arpsmp::Params& p) { p.fitLoop = on; }); sync(); };
    pitchedB.onClick = [this] { const bool on = pitchedB.on; push ([on] (arpsmp::Params& p) { p.slicePitched = on; }); };
    fadeS.onChange = [this] (int v) { push ([v] (arpsmp::Params& p) { p.fitFade = (float) v * 0.001f; }); };
    sliceB.onClick = [this] { const int n = ed.proc.sliceSample (0.5f); ed.toast ("SLICE TO NOTES: " + juce::String (n) + " slices", 2200); sync(); };
    sliceB.onRight = [this] { const int n = ed.proc.sliceSample (0.9f); ed.toast ("SLICE TO NOTES (finer): " + juce::String (n) + " slices", 2200); sync(); };
    patternB.onClick = [this] {
        int ln = ed.lane(); if (ln == LaneChords) ln = LaneMelody;
        if (ed.proc.slicePattern (ln)) { ed.toast ("PATTERN FROM SLICES: " + ArpEditor::laneName (ln) + " now plays the slices' rhythm (UNDO to go back)", 3000); ed.refresh(); } };
    revB.onClick = [this] { const bool r = revB.on; push ([r] (arpsmp::Params& p) { p.reverse = r; }); };
    formantB.onClick = [this] { const bool f = formantB.on; push ([f] (arpsmp::Params& p) { p.formant = f; }); };
    rootS.onChange = [this] (int v) { push ([v] (arpsmp::Params& p) { p.root = v; }); };
    fineS.onChange = [this] (int v) { push ([v] (arpsmp::Params& p) { p.fine = (float) v; }); };
    autoLoopB.onClick = [this] {
        auto* d = ed.proc.currentSample(); if (d == nullptr || d->length() < 64) return;
        std::vector<float> mono (d->lv[0][0].begin(), d->lv[0][0].end()); if (d->chans > 1) for (size_t i = 0; i < mono.size(); ++i) mono[i] = 0.5f * (mono[i] + d->lv[0][1][i]);
        const auto& p = ed.proc.sampleParams(); int ls = 0, le = 0;
        arpsmp::autoLoop (mono, (int) (p.start * d->length()), (int) (p.end * d->length()), d->rate, ls, le);
        const float a = (float) ls / d->length(), b = (float) le / d->length();
        push ([a, b] (arpsmp::Params& q) { q.loopStart = a; q.loopEnd = b; q.mode = 1; });
        ed.toast ("AUTO LOOP: a seamless loop inside START..END"); };
    for (int l = 0; l < 4; ++l)
        laneB[l]->onClick = [this, l] {
            if (ed.proc.linkAll) ed.proc.linkAll = false;
            // OFF must be a real sound: "linked" would follow the main sound, which is YOUR SAMPLE itself (the button could never switch off)
            static const int own[4] { arpsyn::Keys, arpsyn::Neon, arpsyn::Pluck, arpsyn::Pluck };
            const bool mainIsSample = (int) ed.proc.apvts.getRawParameterValue ("preset")->load() >= arpsyn::kPresets;
            ed.proc.setLaneChoice (l, laneB[l]->on ? arpsyn::kPresets + 1 : mainIsSample ? own[l] + 1 : 0);
            ed.refresh(); };
    loadB.onClick = [this] {
        auto fc = std::make_shared<juce::FileChooser> ("Load a sample", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
        juce::Component::SafePointer<SamplePanel> sp (this);
        fc->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [sp, fc] (const juce::FileChooser& c) {
            if (sp == nullptr || c.getResult() == juce::File()) return; sp->ed.loadSampleFile (c.getResult()); }); };
    recentB.onClick = [this] {
        juce::PopupMenu m; const auto rec = ed.proc.recentSamples();
        if (rec.isEmpty()) m.addItem (1, "(no recent samples yet)", false);
        for (int i = 0; i < rec.size(); ++i) m.addItem (10 + i, juce::File (rec[i]).getFileName(), juce::File (rec[i]).existsAsFile());
        juce::Component::SafePointer<SamplePanel> sp (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&recentB), [sp, rec] (int r) { if (sp != nullptr && r >= 10) sp->ed.loadSampleFile (juce::File (rec[r - 10])); }); };
    clearB.onClick = [this] { ed.proc.clearSample(); ed.toast ("YOUR SAMPLE removed (the lanes are back on their own sounds)"); ed.refresh(); };
    close.onClick = [this] { ed.openSample (false); };
}

void SamplePanel::push (const std::function<void (arpsmp::Params&)>& fn)
{
    auto p = ed.proc.sampleParams(); fn (p); ed.proc.setSampleParams (p);
    const auto& q = ed.proc.sampleParams(); waveC.setRegion ({ q.start, q.end, q.loopStart, q.loopEnd, q.mode == 1 || (q.mode == 2 && q.fitLoop) }); repaint();
}

juce::File SamplePanel::writeRegion (float a, float b)
{   // from the project's own copy (FLAC, bit-exact): the message thread never touches what the audio thread plays
    if (! ed.proc.hasSample() || ed.proc.sampleFlac.getSize() == 0) return {};
    juce::FlacAudioFormat flac;
    std::unique_ptr<juce::AudioFormatReader> rd (flac.createReaderFor (new juce::MemoryInputStream (ed.proc.sampleFlac, false), true));
    if (rd == nullptr || rd->lengthInSamples < 2) return {};
    const int n = (int) rd->lengthInSamples, ch = (int) juce::jlimit (1u, 2u, rd->numChannels);
    const int i0 = juce::jlimit (0, n - 1, (int) std::floor (a * n)), i1 = juce::jlimit (i0 + 1, n, (int) std::ceil (b * n));
    juce::AudioBuffer<float> buf (ch, i1 - i0); rd->read (&buf, 0, i1 - i0, i0, true, ch > 1);
    if (ed.proc.sampleParams().reverse) buf.reverse (0, buf.getNumSamples());
    const int fade = juce::jmin (64, buf.getNumSamples() / 4);   // no clicks at the cut
    for (int c = 0; c < ch; ++c) { buf.applyGainRamp (c, 0, fade, 0.0f, 1.0f); buf.applyGainRamp (c, buf.getNumSamples() - fade, fade, 1.0f, 0.0f); }
    const auto dir = ArpProcessor::presetDir().getChildFile ("Audio"); dir.createDirectory();
    const auto nm = juce::File::createLegalFileName (juce::File (ed.proc.sampleInfo.name).getFileNameWithoutExtension() + " region.wav");
    const auto f = dir.getChildFile (nm.isEmpty() ? juce::String ("sample region.wav") : nm).getNonexistentSibling (true);
    return ArpProcessor::writeWav (f, buf, rd->sampleRate) ? f : juce::File();
}

void SamplePanel::sync()
{
    const auto& p = ed.proc.sampleParams(); const bool has = ed.proc.hasSample();
    {   // the waveform
        const auto& si = ed.proc.sampleInfo;
        waveC.setPeaks (has ? si.peaks : std::vector<float>()); waveC.setLengthSeconds (has && si.rate > 0 ? si.len / si.rate : 0.0);
        waveC.setRegion ({ p.start, p.end, p.loopStart, p.loopEnd, p.mode == 1 || (p.mode == 2 && p.fitLoop) });
    }
    modeSeg.sel = p.mode; fitSeg.sel = p.fitStretch ? 1 : 0; orderSeg.sel = p.sliceOrder; fitLoopB.on = p.fitLoop; pitchedB.on = p.slicePitched; fadeS.value = juce::roundToInt (p.fitFade * 1000.0f);
    for (auto* c : std::initializer_list<juce::Component*> { &fitSeg, &fitLoopB, &fadeS }) c->setVisible (p.mode == 2);
    for (auto* c : std::initializer_list<juce::Component*> { &sliceB, &orderSeg, &pitchedB, &patternB }) c->setVisible (p.mode == 3);
    if (p.mode == 3) fadeS.setVisible (true);
    for (auto* c : std::initializer_list<juce::Component*> { &fitSeg, &orderSeg, &fitLoopB, &sliceB, &pitchedB, &patternB, &fadeS }) c->setEnabled (has);
    revB.on = p.reverse; formantB.on = p.formant; rootS.value = p.root; fineS.value = (int) std::lround (p.fine);
    aS.setValue (p.a, juce::dontSendNotification); dS.setValue (p.d, juce::dontSendNotification); sS.setValue (p.s, juce::dontSendNotification); rS.setValue (p.r, juce::dontSendNotification);
    cutS.setValue (p.cutoff, juce::dontSendNotification); toneS.setValue (p.tone, juce::dontSendNotification); volS.setValue (p.volDb, juce::dontSendNotification); xfS.setValue (p.xfade, juce::dontSendNotification);
    for (int l = 0; l < 4; ++l) laneB[l]->on = ed.proc.laneChoice (l) == arpsyn::kPresets + 1 || (ed.proc.laneChoice (l) == 0 && (l == LaneArp || l == LaneMelody) && (int) ed.proc.apvts.getRawParameterValue ("preset")->load() == arpsyn::kPresets);
    waveC.setEnabled (true);
    for (auto* c : std::initializer_list<juce::Component*> { &clearB, &autoLoopB, &revB, &formantB, &modeSeg, &rootS, &fineS, &aS, &dS, &sS, &rS, &cutS, &toneS, &volS, &xfS }) c->setEnabled (has);
    for (auto* b : laneB) b->setEnabled (has);
    for (auto* c : getChildren()) c->repaint();
    repaint();
}

void SamplePanel::resized()
{
    auto r = getLocalBounds().reduced (14, 10);
    auto top = r.removeFromTop (30);
    close.setBounds (top.removeFromRight (80).reduced (0, 2)); top.removeFromRight (8);
    clearB.setBounds (top.removeFromRight (70).reduced (0, 2)); top.removeFromRight (6);
    recentB.setBounds (top.removeFromRight (80).reduced (0, 2)); top.removeFromRight (6);
    loadB.setBounds (top.removeFromRight (90).reduced (0, 2));
    r.removeFromTop (8);
    lblRow = r.removeFromTop (18);
    r.removeFromTop (4);
    waveC.setBounds (r.removeFromTop (juce::jmax (150, r.getHeight() - 246)));
    r.removeFromTop (10);
    {   // PLAY MODE, REVERSE, KEEP FORMANTS, AUTO LOOP, ROOT, FINE
        auto a = r.removeFromTop (30);
        modeSeg.setBounds (a.removeFromLeft (juce::jlimit (180, 280, a.getWidth() - 606)).reduced (0, 2)); a.removeFromLeft (8);   // (wider for its 4 modes; the rest as before)
        autoLoopB.setBounds (a.removeFromLeft (100).reduced (0, 2)); a.removeFromLeft (8);
        revB.setBounds (a.removeFromLeft (90).reduced (0, 2)); a.removeFromLeft (8);
        formantB.setBounds (a.removeFromLeft (128).reduced (0, 2)); a.removeFromLeft (14);
        rootS.setBounds (a.removeFromLeft (124).reduced (0, 2)); a.removeFromLeft (6);
        fineS.setBounds (a.removeFromLeft (120).reduced (0, 2));
    }
    r.removeFromTop (6);
    {   // NOTE LENGTH / SLICES options (the row says what the mode does otherwise)
        modeRow = r.removeFromTop (30); auto a = modeRow;
        fitSeg.setBounds (a.removeFromLeft (180).reduced (0, 2)); a.removeFromLeft (8); fitLoopB.setBounds (a.removeFromLeft (130).reduced (0, 2)); a.removeFromLeft (8);
        auto b = modeRow;
        sliceB.setBounds (b.removeFromLeft (140).reduced (0, 2)); b.removeFromLeft (8); orderSeg.setBounds (b.removeFromLeft (190).reduced (0, 2)); b.removeFromLeft (8);
        pitchedB.setBounds (b.removeFromLeft (90).reduced (0, 2)); b.removeFromLeft (8); patternB.setBounds (b.removeFromLeft (180).reduced (0, 2)); b.removeFromLeft (8);
        fadeS.setBounds (modeRow.withTrimmedLeft (modeRow.getWidth() - 150).reduced (0, 2));
    }
    r.removeFromTop (10);
    {   // the knobs
        knobRow = r.removeFromTop (92);
        auto k = knobRow; k.removeFromBottom (16);
        const int w = k.getWidth() / 8;
        for (auto* s : { &aS, &dS, &sS, &rS, &cutS, &toneS, &volS, &xfS }) s->setBounds (k.removeFromLeft (w).reduced (10, 0));
    }
    r.removeFromTop (8);
    {   // the lanes that play it
        auto l = r.removeFromTop (30); l.removeFromLeft (150);
        const int w = juce::jmin (120, l.getWidth() / 4);
        for (auto* b : laneB) b->setBounds (l.removeFromLeft (w).reduced (3, 2));
    }
}

void SamplePanel::paintOverChildren (juce::Graphics& g)
{   // SLICES: a numbered marker at each slice start
    const auto& p = ed.proc.sampleParams(); if (p.mode != 3 || p.nSlices < 1 || ! ed.proc.hasSample()) return;
    const auto& P = ed.th.p; const auto wa = waveC.waveArea().translated ((float) waveC.getX(), (float) waveC.getY());
    g.setFont (SF (8.5f));
    for (int k = 0; k < p.nSlices; ++k)
    {
        const float f = p.reverse ? 1.0f - p.slices[(size_t) k] : p.slices[(size_t) k];
        const float x = wa.getX() + f * wa.getWidth();
        g.setColour (P.gold.withAlpha (0.8f)); g.fillRect (x - 0.5f, wa.getY() + 22, 1.0f, wa.getHeight() - 22);
        auto lr = juce::Rectangle<float> (x + 1, wa.getBottom() - 30, 16, 11); g.setColour (P.card); g.fillRoundedRectangle (lr, 2); g.setColour (P.ink); g.drawText (juce::String (k + 1), lr, juce::Justification::centred);
    }
}

void SamplePanel::paint (juce::Graphics& g)
{
    const auto& P = ed.th.p; auto R = getLocalBounds().toFloat();
    fillCard (g, R, 14, P); g.setColour (P.cyan); g.drawRoundedRectangle (R.reduced (1), 14, 1.8f);
    g.setColour (P.ink); g.setFont (SF (17.0f)); g.drawText ("YOUR SAMPLE", 16, 10, 160, 26, juce::Justification::centredLeft);
    const auto& si = ed.proc.sampleInfo; const auto& p = ed.proc.sampleParams();
    g.setColour (P.dim); g.setFont (SF (10.5f, false));
    if (si.len > 0)
    {
        const juce::String det = si.detectedHz > 0 ? "detected " + juce::String (si.detectedHz, 1) + " Hz" : juce::String ("no clear pitch");
        g.drawText (si.name + "   " + juce::String (si.len / si.rate, 2) + " s, " + juce::String (si.chans == 2 ? "stereo" : "mono") + ", " + juce::String (juce::roundToInt (si.rate)) + " Hz   ROOT " +
                    juce::MidiMessage::getMidiNoteName (p.root, true, true, 4) + " (" + det + ")", lblRow, juce::Justification::centredLeft, true);
    }
    else g.drawText ("A sample plays your lanes: drop one here (WAV, AIFF, FLAC, MP3) or LOAD", lblRow, juce::Justification::centredLeft, true);
    // the knob labels
    static const char* kl[] { "ATTACK", "DECAY", "SUSTAIN", "RELEASE", "FILTER", "TONE", "VOLUME", "XFADE" };
    const int w = knobRow.getWidth() / 8;
    g.setFont (SF (9.5f)); g.setColour (P.dim);
    for (int k = 0; k < 8; ++k) g.drawText (kl[k], knobRow.getX() + k * w, knobRow.getBottom() - 14, w, 14, juce::Justification::centred);
    auto lanesRow = juce::Rectangle<int> (getLocalBounds().reduced (14, 10).getX(), laneB[0]->getY(), 150, laneB[0]->getHeight());
    g.setColour (P.ink); g.drawText ("PLAY SAMPLE ON:", lanesRow, juce::Justification::centredLeft);
    if (p.mode <= 1)
    {
        g.setColour (P.dim); g.setFont (SF (10.0f, false));
        g.drawText (p.mode == 0 ? "ONE-SHOT: every note plays START..END to the end. NOTE LENGTH: the note decides how much plays. SLICES: one hit per note."
                                : "LOOP: notes hold inside the loop points (crossfaded). NOTE LENGTH: the note decides how much plays. SLICES: one hit per note.", modeRow, juce::Justification::centredLeft, true);
    }
}

} // namespace arpui
