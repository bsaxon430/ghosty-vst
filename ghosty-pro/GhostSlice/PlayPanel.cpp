#include "PlayPanel.h"

namespace gslice::ui
{
PlayPanel::Field::Field (const juce::String& name, double lo, double hi, double step, const juce::String& suffix, double def)
{
    cap.setText (name, juce::dontSendNotification); cap.setFont (LnF::uiFont (11.5f)); cap.setJustificationType (juce::Justification::centredLeft);
    s.setRange (lo, hi, step); s.setTextValueSuffix (suffix); s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 20);
    s.setDoubleClickReturnValue (true, juce::jlimit (lo, hi, def)); s.setValue (juce::jlimit (lo, hi, def), juce::dontSendNotification);
    addAndMakeVisible (cap); addAndMakeVisible (s);
    setComponentID ("play." + name.toLowerCase());
}

PlayPanel::PlayPanel (SliceProcessor& p) : proc (p)
{
    auto add = [this] (juce::Component& c, const juce::String& id, const juce::String& tip)
    {
        c.setComponentID (id); addAndMakeVisible (c);
        if (auto* s = dynamic_cast<juce::SettableTooltipClient*> (&c); s != nullptr && tip.isNotEmpty()) s->setTooltip (tip);
    };
    for (auto* b : { &regionsBtn, &equalBtn, &manualBtn }) addChildComponent (*b);
    regionsBtn.setTooltip ("Slices = the arrangement's regions (cut them in EDIT)");
    equalBtn.setTooltip ("Slices = N equal parts of the arrangement (cuts snap to a zero crossing within 2 ms)");
    manualBtn.setTooltip ("Slices = your own cuts: click the strip to add one, drag to move, double-click to remove");
    add (chopBox, "play.chop", "CHOP: where the slices come from - REGIONS, EQUAL parts, MANUAL cuts, TRANSIENT (attacks), SYLLABLE (sung / spoken syllables), NOTE (pitched notes), BEAT (the tempo grid)");
    for (int i = 0; i < kSliceModes; ++i) chopBox.addItem (juce::String ("CHOP: ") + play::modeName ((SliceMode) i), i + 1);
    chopBox.onChange = [this] { if (! refreshing) setMode ((SliceMode) (chopBox.getSelectedId() - 1)); };
    add (divBox, "play.div", "BEAT: a cut every 1/N note of the project tempo");
    for (int d : { 4, 8, 16, 32 }) divBox.addItem ("1/" + juce::String (d), d);
    divBox.onChange = [this] { if (! refreshing) globalChanged ([this] (PlayMap& m) { m.beatDiv = divBox.getSelectedId(); }, "Beat division"); };
    add (splitSlicesBtn, "play.splitslices", "SPLIT @ SLICES: cut the arrangement into regions at the slice points (rearrange them in the lane strip)");
    splitSlicesBtn.onClick = [this] { splitAtSlices(); };
    addAndMakeVisible (sensF); sensF.capW = 44;
    sensF.s.setTooltip ("SENSITIVITY: more = more cuts (weaker attacks / syllables / notes count too)");
    sensF.s.onValueChange = [this] { if (! refreshing) globalChanged ([this] (PlayMap& m) { m.sensitivity = sensF.s.getValue() / 100.0; }, "Chop sensitivity"); };
    sensF.s.onDragStart = [this] { proc.beginGesture(); }; sensF.s.onDragEnd = [this] { proc.endGesture(); };
    equalF.s.setTooltip ("EQUAL: how many parts");
    add (instrBtn, "play.instrument", "TURN INTO INSTRUMENT: every slice on a key from C1 upward (keeps the settings of keys that stay)");
    add (exportBtn, "play.exportmidi", "Export the mapping as MIDI: each slice's key at its place in the arrangement (play it back = the arrangement)");
    add (prevBtn, "play.prev", "Previous 16 pads"); add (nextBtn, "play.next", "Next 16 pads");
    add (oneShotBtn, "play.oneshot", "ONE-SHOT: the slice plays to its end. GATE: it stops (RELEASE) when the key is let go");
    add (reverseBtn, "play.reverse", "Play this key's slice backwards");
    add (vowelBtn, "play.vowelhold", "HOLD VOWEL: hold the key to sustain the slice's vowel as long as you like (a grain loop that follows the pitch; makes the key GATE)");
    add (repeatBox, "play.repeat", "REPEAT: retrigger the slice while it sounds (follows the tempo)");
    add (pads, "play.pads", ""); add (keys, "play.keys", ""); add (strip, "play.strip", "");
    for (int i = 0; i <= 5; ++i) repeatBox.addItem ("REPEAT " + play::repeatName (i), i + 1);
    repeatBox.onChange = [this] { if (! refreshing) keyChanged ([this] (KeySlot& k) { k.repeat = repeatBox.getSelectedId() - 1; }, "Key repeat"); };
    regionsBtn.onClick = [this] { setMode (SliceMode::Regions); };
    equalBtn.onClick = [this] { setMode (SliceMode::Equal); };
    manualBtn.onClick = [this] { setMode (SliceMode::Manual); };
    instrBtn.onClick = [this] { turnIntoInstrument(); };
    exportBtn.onClick = [this] { exportMappingClicked(); };
    prevBtn.onClick = [this] { page = juce::jmax (0, page - 1); refresh(); repaint(); };
    nextBtn.onClick = [this] { page = page + 1; refresh(); repaint(); };
    oneShotBtn.onClick = [this] { keyChanged ([] (KeySlot& k) { k.oneShot = ! k.oneShot; }, "Key one-shot"); };
    reverseBtn.onClick = [this] { keyChanged ([] (KeySlot& k) { k.reverse = ! k.reverse; }, "Key reverse"); };
    vowelBtn.onClick = [this] { keyChanged ([] (KeySlot& k) { k.vowelHold = ! k.vowelHold; if (k.vowelHold) k.oneShot = false; }, "Key hold vowel"); };
    addAndMakeVisible (equalF);
    equalF.s.onValueChange = [this] { if (! refreshing) globalChanged ([this] (PlayMap& m) { m.equalN = (int) equalF.s.getValue(); }, "Equal parts"); };
    equalF.s.onDragStart = [this] { proc.beginGesture(); }; equalF.s.onDragEnd = [this] { proc.endGesture(); };
    for (auto* f : keyFields())
    {
        addAndMakeVisible (*f);
        f->s.onDragStart = [this] { proc.beginGesture(); }; f->s.onDragEnd = [this] { proc.endGesture(); };
        f->s.onValueChange = [this, f]
        {
            if (refreshing) return;
            const double v = f->s.getValue();
            keyChanged ([this, f, v] (KeySlot& k)
            {
                if (f == &pitchF) k.pitchSemis = v; else if (f == &startF) k.startMs = v; else if (f == &attackF) k.attackMs = v;
                else if (f == &decayF) k.decayMs = v; else if (f == &sustainF) k.sustain = v / 100.0; else if (f == &releaseF) k.releaseMs = v;
                else if (f == &gainF) k.gainDb = v; else if (f == &panF) k.pan = v / 100.0; else if (f == &chokeF) k.choke = (int) v;
            }, "Key " + f->cap.getText().toLowerCase());
        };
    }
    for (auto* f : globalFields())
    {
        addAndMakeVisible (*f);
        f->s.onDragStart = [this] { proc.beginGesture(); }; f->s.onDragEnd = [this] { proc.endGesture(); };
        f->s.onValueChange = [this, f]
        {
            if (refreshing) return;
            const double v = f->s.getValue();
            globalChanged ([this, f, v] (PlayMap& m)
            {
                if (f == &polyF) m.polyphony = (int) v; else if (f == &bendF) m.bendRange = (int) v;
                else if (f == &velGainF) m.velToGain = v / 100.0; else m.velToFilter = v / 100.0;
            }, f->cap.getText());
        };
    }
    pitchF.s.setTooltip ("The key's pitch (tape style: higher = shorter); pitch bend comes on top (BEND range)");
    attackF.s.setTooltip ("ATTACK: the key's fade-in"); decayF.s.setTooltip ("DECAY: the time to fall to the SUSTAIN level");
    sustainF.s.setTooltip ("SUSTAIN: the level held while the key is down (GATE keys)"); releaseF.s.setTooltip ("RELEASE: the fade-out after the key is let go");
    gainF.s.setTooltip ("The key's level"); panF.s.setTooltip ("The key's stereo position");
    startF.s.setTooltip ("Start this far into the slice"); chokeF.s.setTooltip ("CHOKE group: a key cuts the others in its group (like open / closed hats); 0 = none");
    polyF.s.setTooltip ("Voices (stealing is click-free: a stolen voice fades out in 3 ms)"); bendF.s.setTooltip ("Pitch-bend range (+- semitones)");
    velGainF.s.setTooltip ("How much velocity sets the loudness"); velFiltF.s.setTooltip ("How much a soft note closes a low-pass filter");
    startTimerHz (30);
}

PlayPanel::~PlayPanel() { stopTimer(); }

std::vector<PlayPanel::Field*> PlayPanel::keyFields() { return { &pitchF, &startF, &attackF, &decayF, &sustainF, &releaseF, &gainF, &panF, &chokeF }; }
std::vector<PlayPanel::Field*> PlayPanel::globalFields() { return { &polyF, &bendF, &velGainF, &velFiltF }; }
std::vector<juce::Component*> PlayPanel::controls()
{
    std::vector<juce::Component*> v { &chopBox, &splitSlicesBtn, &instrBtn, &exportBtn, &prevBtn, &nextBtn, &oneShotBtn, &reverseBtn, &vowelBtn, &repeatBox, &pads, &keys, &strip };
    for (juce::Component* c : { (juce::Component*) &equalF, (juce::Component*) &sensF, (juce::Component*) &divBox }) if (c->isVisible()) v.push_back (c);
    for (auto* f : keyFields()) v.push_back (f);
    for (auto* f : globalFields()) v.push_back (f);
    return v;
}

void PlayPanel::setPalette (const Palette& p)
{
    P = p;
    for (auto* f : keyFields()) f->cap.setColour (juce::Label::textColourId, P.dim);
    for (auto* f : globalFields()) f->cap.setColour (juce::Label::textColourId, P.dim);
    equalF.cap.setColour (juce::Label::textColourId, P.dim); sensF.cap.setColour (juce::Label::textColourId, P.dim);
    repaint();
}

int PlayPanel::noteForSlice (int slice) const
{
    int n = -1; for (auto& k : proc.project().play.keys) if (k.slice == slice && (n < 0 || k.note < n)) n = k.note; return n;
}
const KeySlot* PlayPanel::selectedKey() const { const int n = noteForSlice (selSlice); return n >= 0 ? proc.project().play.key (n) : nullptr; }

void PlayPanel::keyChanged (const std::function<void (KeySlot&)>& f, const juce::String& label)
{
    const int note = noteForSlice (selSlice);
    if (note < 0) { if (toast) toast ("This slice is on no key yet: TURN INTO INSTRUMENT first"); refresh(); return; }
    PlayMap m = proc.project().play;
    if (auto* k = m.key (note)) f (*k);
    if (auto q = ops::setPlay (proc.project(), m)) proc.apply (*q, label);
    refresh();
}
void PlayPanel::globalChanged (const std::function<void (PlayMap&)>& f, const juce::String& label)
{
    PlayMap m = proc.project().play; f (m);
    if (auto q = ops::setPlay (proc.project(), m)) proc.apply (*q, label);
    refresh();
}

void PlayPanel::setMode (SliceMode md)
{
    PlayMap m = proc.project().play;
    if (m.mode == md) return;
    if (md == SliceMode::Manual && m.manual.empty())
    {   // start from the current cuts
        const auto sl = proc.currentSlices();
        for (size_t i = 1; i < sl.size(); ++i) m.manual.push_back (proc.hfToPf ((double) sl[i].dst0));
        if (m.manual.empty() && proc.currentPlan().length > 0) m.manual.push_back (proc.hfToPf ((double) proc.currentPlan().length * 0.5));
    }
    m.mode = md;
    if (auto q = ops::setPlay (proc.project(), m)) proc.apply (*q, "Slice mode");
    page = 0; refresh(); repaint();
}

void PlayPanel::splitAtSlices()
{
    const auto sl = proc.currentSlices();
    if (sl.size() < 2) { if (toast) toast ("Nothing to split: the slice mode gives one slice"); return; }
    std::vector<double> cuts; for (size_t i = 1; i < sl.size(); ++i) cuts.push_back (proc.hfToPf ((double) sl[i].dst0));
    int made = 0;
    if (auto q = ops::splitAtSlices (proc.project(), cuts, &made)) { proc.apply (*q, "Split at slices"); if (toast) toast (juce::String (made + 1) + " regions: drag the blocks in the lane strip to rearrange them"); }
    else if (toast) toast ("Could not split there (too close to a region edge)");
    page = 0; refresh(); repaint();
}

void PlayPanel::turnIntoInstrument()
{
    const int n = (int) proc.currentSlices().size();
    if (n == 0) { if (toast) toast ("Load audio first"); return; }
    if (auto q = ops::turnIntoInstrument (proc.project(), n)) proc.apply (*q, "Turn into instrument");
    if (toast) toast (juce::String (juce::jmin (n, 92)) + " slices on the keys from C1 (" + play::noteName (36) + " - " + play::noteName (36 + juce::jmin (n, 92) - 1) + ")");
    refresh(); repaint();
}

void PlayPanel::exportMappingClicked()
{
    if (! ghosty::exportAllowed (proc.license, &exportBtn, "slice.export.playmidi")) return;
    if (proc.project().play.keys.empty()) { if (toast) toast ("Map the slices first (TURN INTO INSTRUMENT)"); return; }
    const auto base = proc.audioFile() != juce::File() ? proc.audioFile().getFileNameWithoutExtension() : juce::String ("Ghost Slice");
    const auto dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Ghost Slice");
    juce::Component::SafePointer<PlayPanel> sp (this);
    ghosty::exportgate::saveDialog (chooser, "Export the PLAY mapping as MIDI", dir.getChildFile (base + " (play map).mid"), "*.mid", [sp] (const juce::File& picked)
    {
        if (sp == nullptr) return;
        const auto f = picked.hasFileExtension (".mid") ? picked : picked.withFileExtension (".mid");
        f.getParentDirectory().createDirectory();
        juce::String err;
        if (sp->proc.exportPlayMidi (f, err)) { if (sp->toast) sp->toast ("Exported " + f.getFileName()); }
        else if (sp->toast) sp->toast (err);
    });
}

bool PlayPanel::dragPadOut (int slice)
{
    if (! ghosty::exportAllowed (proc.license, &pads, "slice.drag.padwav")) return false;
    auto b = proc.getBank();
    if (b == nullptr || slice < 0 || slice >= (int) b->audio.size()) { if (toast) toast ("That slice is still rendering"); return false; }
    const auto base = proc.audioFile() != juce::File() ? proc.audioFile().getFileNameWithoutExtension() : juce::String ("Ghost Slice");
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Ghost Slice drag");
    dir.createDirectory();
    const auto f = dir.getChildFile (juce::File::createLegalFileName (base + " slice " + juce::String (slice + 1) + ".wav"));
    juce::String err;
    if (! proc.writeSliceWav (slice, f, err)) { if (toast) toast (err); return false; }
    return ghosty::exportgate::dragFiles (proc.license, { f.getFullPathName() }, &pads);
}

void PlayPanel::selectPad (int slice) { selSlice = juce::jmax (0, slice); page = selSlice / kPads; refresh(); repaint(); }

void PlayPanel::refresh()
{
    refreshing = true;
    const auto& m = proc.project().play;
    const bool has = proc.getSource() != nullptr;
    const int n = (int) proc.currentSlices().size();
    const int pages = juce::jmax (1, (n + kPads - 1) / kPads);
    page = juce::jlimit (0, pages - 1, page);
    regionsBtn.setToggleState (m.mode == SliceMode::Regions, juce::dontSendNotification);
    equalBtn.setToggleState (m.mode == SliceMode::Equal, juce::dontSendNotification);
    manualBtn.setToggleState (m.mode == SliceMode::Manual, juce::dontSendNotification);
    equalF.setVisible (m.mode == SliceMode::Equal); equalF.s.setValue (m.equalN, juce::dontSendNotification);
    chopBox.setSelectedId ((int) m.mode + 1, juce::dontSendNotification);
    sensF.setVisible (m.mode == SliceMode::Transient || m.mode == SliceMode::Syllable || m.mode == SliceMode::Note);
    sensF.s.setValue (m.sensitivity * 100.0, juce::dontSendNotification);
    divBox.setVisible (m.mode == SliceMode::Beat); divBox.setSelectedId (m.beatDiv, juce::dontSendNotification);
    splitSlicesBtn.setEnabled (has && m.mode != SliceMode::Regions && n > 1);
    instrBtn.setEnabled (has && n > 0); exportBtn.setEnabled (has && ! m.keys.empty());
    prevBtn.setEnabled (page > 0); nextBtn.setEnabled (page + 1 < pages);
    const auto* k = selectedKey();
    for (auto* f : keyFields()) f->setEnabled (k != nullptr);
    oneShotBtn.setEnabled (k != nullptr); reverseBtn.setEnabled (k != nullptr); repeatBox.setEnabled (k != nullptr); vowelBtn.setEnabled (k != nullptr);
    if (k != nullptr)
    {
        pitchF.s.setValue (k->pitchSemis, juce::dontSendNotification); startF.s.setValue (k->startMs, juce::dontSendNotification);
        attackF.s.setValue (k->attackMs, juce::dontSendNotification); decayF.s.setValue (k->decayMs, juce::dontSendNotification);
        sustainF.s.setValue (k->sustain * 100.0, juce::dontSendNotification); releaseF.s.setValue (k->releaseMs, juce::dontSendNotification);
        gainF.s.setValue (k->gainDb, juce::dontSendNotification); panF.s.setValue (k->pan * 100.0, juce::dontSendNotification);
        chokeF.s.setValue (k->choke, juce::dontSendNotification);
        oneShotBtn.setButtonText (k->oneShot ? "ONE-SHOT" : "GATE"); oneShotBtn.setToggleState (k->oneShot, juce::dontSendNotification);
        reverseBtn.setToggleState (k->reverse, juce::dontSendNotification); vowelBtn.setToggleState (k->vowelHold && ! k->oneShot, juce::dontSendNotification);
        repeatBox.setSelectedId (k->repeat + 1, juce::dontSendNotification);
    }
    else { oneShotBtn.setButtonText ("ONE-SHOT"); oneShotBtn.setToggleState (false, juce::dontSendNotification); reverseBtn.setToggleState (false, juce::dontSendNotification); vowelBtn.setToggleState (false, juce::dontSendNotification); repeatBox.setSelectedId (1, juce::dontSendNotification); }
    polyF.s.setValue (m.polyphony, juce::dontSendNotification); bendF.s.setValue (m.bendRange, juce::dontSendNotification);
    velGainF.s.setValue (m.velToGain * 100.0, juce::dontSendNotification); velFiltF.s.setValue (m.velToFilter * 100.0, juce::dontSendNotification);
    refreshing = false;
    resized();
    repaint();
}

void PlayPanel::timerCallback()
{
    if (! isShowing()) return;
    uint64 bits[2] {};
    for (int nt = 0; nt < 128; ++nt) if (proc.getPlayEngine().isSounding (nt)) bits[nt >> 6] |= (uint64) 1 << (nt & 63);
    const auto b = proc.getBank(); const juce::String bk = b != nullptr ? b->key : juce::String();
    if (bits[0] != lastBits[0] || bits[1] != lastBits[1]) { lastBits[0] = bits[0]; lastBits[1] = bits[1]; pads.repaint(); keys.repaint(); }
    if (bk != lastBank) { lastBank = bk; refresh(); }
}

void PlayPanel::resized()
{
    auto r = getLocalBounds().reduced (12, 8);
    topArea = r.removeFromTop (32); r.removeFromTop (8);
    {
        auto t = topArea;
        auto place = [&t] (juce::Component& c, int w, int gap = 4) { c.setBounds (t.removeFromLeft (w)); t.removeFromLeft (gap); };
        place (chopBox, 156, 8);
        if (equalF.isVisible()) place (equalF, 170, 8);
        if (sensF.isVisible()) place (sensF, 170, 8);
        if (divBox.isVisible()) place (divBox, 76, 8);
        place (splitSlicesBtn, 128, 8);
        nextBtn.setBounds (t.removeFromRight (32)); t.removeFromRight (4); prevBtn.setBounds (t.removeFromRight (32)); t.removeFromRight (12);
        exportBtn.setBounds (t.removeFromRight (136)); t.removeFromRight (6);
        instrBtn.setBounds (t.removeFromRight (juce::jmin (184, juce::jmax (0, t.getWidth()))));
    }
    strip.setBounds (r.removeFromTop (56)); r.removeFromTop (8);
    keys.setBounds (r.removeFromBottom (64)); r.removeFromBottom (16);
    settingsArea = r.removeFromRight (juce::jmin (440, r.getWidth() / 2)); r.removeFromRight (12);
    pads.setBounds (r);
    auto s = settingsArea.reduced (10, 6);
    settingsTitle = s.removeFromTop (22); s.removeFromTop (4);
    const int colW = (s.getWidth() - 10) / 2, rowH = 26, gap = 5;
    auto row = [&] { auto x = s.removeFromTop (rowH); s.removeFromTop (gap); return x; };
    auto kf = keyFields();
    for (size_t i = 0; i < kf.size(); i += 2)
    {
        auto x = row();
        kf[i]->setBounds (x.removeFromLeft (colW));
        if (i + 1 < kf.size()) { x.removeFromLeft (10); kf[i + 1]->setBounds (x.removeFromLeft (colW)); }
        else { x.removeFromLeft (10); auto y = x.removeFromLeft (colW); repeatBox.setBounds (y.withSizeKeepingCentre (y.getWidth(), 24)); }
    }
    {
        auto x = row();
        oneShotBtn.setBounds (x.removeFromLeft (colW / 2 + 20)); x.removeFromLeft (6);
        reverseBtn.setBounds (x.removeFromLeft (colW / 2 + 10)); x.removeFromLeft (6);
        vowelBtn.setBounds (x.removeFromLeft (juce::jmin (x.getWidth(), colW - 26)));
    }
    s.removeFromTop (8);
    auto gf = globalFields();
    for (size_t i = 0; i < gf.size(); i += 2) { auto x = row(); gf[i]->setBounds (x.removeFromLeft (colW)); x.removeFromLeft (10); gf[i + 1]->setBounds (x.removeFromLeft (colW)); }
    for (auto* f : kf) f->capW = 66;
    for (auto* f : gf) f->capW = 82;
    equalF.capW = 52;
}

void PlayPanel::paint (juce::Graphics& g)
{
    if (P.glass)
    {   // GHOST: the editor's aurora shows through; the settings float on a glass panel
        ghosty::glass::panel (g, settingsArea.toFloat(), 16.0f, juce::Colours::white, 0.36f, false, 8.0f);
    }
    else
    {
        g.fillAll (P.window);
        g.setColour (P.panel); g.fillRoundedRectangle (settingsArea.toFloat(), 8.0f);
        g.setColour (P.line); g.drawRoundedRectangle (settingsArea.toFloat().reduced (0.5f), 8.0f, 1.0f);
    }
    const auto* k = selectedKey();
    g.setFont (LnF::uiFont (13.0f)); g.setColour (P.ink);
    const auto sl = proc.currentSlices();
    juce::String t = "SLICE " + juce::String (selSlice + 1) + (selSlice < (int) sl.size() ? "   " + juce::String (juce::roundToInt ((double) sl[(size_t) selSlice].length() / proc.hostRate() * 1000.0)) + " ms" : juce::String());
    t << (k != nullptr ? "   KEY " + play::noteName (k->note) : juce::String ("   no key"));
    g.drawText (t, settingsTitle, juce::Justification::centredLeft);
    // the divider above the global settings
    g.setColour (P.line); g.fillRect (settingsArea.getX() + 10, polyF.getY() - 7, settingsArea.getWidth() - 20, 1);
    g.setFont (LnF::uiFont (10.5f, false)); g.setColour (P.dim);
    const auto b = proc.getBank();
    juce::String st = juce::String ((int) sl.size()) + (sl.size() == 1 ? " slice" : " slices") + "   " + juce::String ((int) proc.project().play.keys.size()) + " keys";
    if (proc.bankPending()) st << "   rendering...";
    st << "   voices " << proc.getPlayEngine().activeVoices.load();
    g.drawText (st, juce::Rectangle<int> (pads.getX(), pads.getBottom() + 1, pads.getWidth(), 10), juce::Justification::centredLeft);
}

// ---------------------------------------------------------------- pads
juce::Rectangle<float> PlayPanel::Pads::padRect (int k) const
{
    const float gap = 8.0f;
    const float s = std::min (((float) getWidth() - 3 * gap) / 4.0f, ((float) getHeight() - 12.0f - 3 * gap) / 4.0f);
    const float x0 = ((float) getWidth() - (4 * s + 3 * gap)) * 0.5f;
    return { x0 + (float) (k % 4) * (s + gap), (float) (3 - k / 4) * (s + gap), s, s };   // bottom row = the first pads (like hardware)
}
int PlayPanel::Pads::padAt (juce::Point<float> p) const { for (int k = 0; k < kPads; ++k) if (padRect (k).contains (p)) return k; return -1; }

void PlayPanel::Pads::paint (juce::Graphics& g)
{
    const auto& pal = panel.P;
    const auto sl = panel.proc.currentSlices();
    const auto bank = panel.proc.getBank();
    const bool bankOk = bank != nullptr && bank->slices.size() == sl.size();
    for (int k = 0; k < kPads; ++k)
    {
        const int s = panel.page * kPads + k;
        const auto R = padRect (k);
        const bool exists = s < (int) sl.size();
        const int note = exists ? panel.noteForSlice (s) : -1;
        const bool on = note >= 0 && panel.proc.getPlayEngine().isSounding (note);
        if (pal.glass)
        {   // GHOST: glass pads (an empty pad = faint glass), a sounding pad glows violet, the selected one has a teal rim
            const auto B = R.reduced (3.0f, 2.0f).withTrimmedBottom (3.0f);
            if (exists) ghosty::glass::panel (g, B, 12.0f, juce::Colours::white, 0.42f, false, 4.0f);
            else ghosty::glass::body (g, B, 12.0f, juce::Colours::white, 0.16f);
            if (on) { g.setColour (ghosty::glass::violet().withAlpha (0.30f)); g.fillRoundedRectangle (B, 12.0f); }
            if (s == panel.selSlice && exists) { g.setColour (pal.marker); g.drawRoundedRectangle (B.reduced (1.0f), 11.0f, 2.0f); }
        }
        else
        {
            g.setColour (exists ? pal.card : pal.panel); g.fillRoundedRectangle (R, 8.0f);
            if (on) { g.setColour (pal.wave.withAlpha (0.30f)); g.fillRoundedRectangle (R, 8.0f); }
            g.setColour (s == panel.selSlice && exists ? pal.marker : pal.line); g.drawRoundedRectangle (R.reduced (0.75f), 8.0f, s == panel.selSlice && exists ? 2.0f : 1.0f);
        }
        if (! exists) continue;
        auto in = R.reduced (8.0f);
        g.setFont (LnF::uiFont (13.0f)); g.setColour (pal.ink);
        g.drawText (juce::String (s + 1), in.removeFromTop (16.0f).toNearestInt(), juce::Justification::centredLeft);
        g.setFont (LnF::uiFont (11.0f)); g.setColour (note >= 0 ? pal.marker : pal.dim);
        g.drawText (note >= 0 ? play::noteName (note) : juce::String ("-"), juce::Rectangle<float> (in.getX(), R.getY() + 8.0f, in.getWidth(), 16.0f).toNearestInt(), juce::Justification::centredRight);
        // the thumbnail
        auto th = in.withTrimmedTop (4.0f);
        const float mid = th.getCentreY(), h = th.getHeight() * 0.5f;
        g.setColour (pal.wave.withAlpha (bankOk ? 1.0f : 0.35f));
        if (bankOk)
        {
            const auto& T = bank->thumbs[(size_t) s];
            float pk = 1e-6f; for (float v : T) pk = std::max (pk, v);
            const float bw = th.getWidth() / 48.0f;
            for (int i = 0; i < 48; ++i) { const float a = T[(size_t) i] / pk * h; g.fillRect (th.getX() + (float) i * bw, mid - a, std::max (1.0f, bw - 1.0f), 2.0f * a); }
        }
        else g.fillRect (th.getX(), mid - 0.5f, th.getWidth(), 1.0f);
    }
}
void PlayPanel::Pads::mouseDown (const juce::MouseEvent& e)
{
    downPad = padAt (e.position); dragged = false; heldNote = -1;
    if (downPad < 0) return;
    const int s = panel.page * kPads + downPad;
    if (s >= (int) panel.proc.currentSlices().size()) return;
    panel.selectPad (s);
    const int note = panel.noteForSlice (s);
    if (note >= 0)
    {   // velocity from where the pad is hit (top = full)
        const auto R = padRect (downPad);
        const float vel = juce::jlimit (0.2f, 1.0f, 1.0f - 0.8f * (e.position.y - R.getY()) / juce::jmax (1.0f, R.getHeight()) + 0.2f);
        panel.proc.audition (note, vel, true); heldNote = note;
    }
}
void PlayPanel::Pads::mouseDrag (const juce::MouseEvent& e)
{
    if (downPad < 0 || dragged || e.getDistanceFromDragStart() < 10) return;
    if (padRect (downPad).contains (e.position)) return;   // (a drag OUT of the pad)
    dragged = true;
    if (heldNote >= 0) { panel.proc.audition (heldNote, 0, false); heldNote = -1; }
    panel.dragPadOut (panel.page * kPads + downPad);
}
void PlayPanel::Pads::mouseUp (const juce::MouseEvent&) { if (heldNote >= 0) panel.proc.audition (heldNote, 0, false); heldNote = -1; downPad = -1; }

// ---------------------------------------------------------------- keyboard
static bool isBlack (int n) { const int k = (n % 12 + 12) % 12; return k == 1 || k == 3 || k == 6 || k == 8 || k == 10; }
juce::Rectangle<float> PlayPanel::Keys::keyRect (int note) const
{
    int whites = 0; for (int n = kLow; n <= kHigh; ++n) if (! isBlack (n)) ++whites;
    const float ww = (float) getWidth() / (float) whites;
    int wi = 0; for (int n = kLow; n < note; ++n) if (! isBlack (n)) ++wi;
    if (! isBlack (note)) return { (float) wi * ww, 0.0f, ww, (float) getHeight() };
    return { (float) wi * ww - ww * 0.32f, 0.0f, ww * 0.64f, (float) getHeight() * 0.6f };
}
int PlayPanel::Keys::noteAt (juce::Point<float> p) const
{
    for (int n = kLow; n <= kHigh; ++n) if (isBlack (n) && keyRect (n).contains (p)) return n;
    for (int n = kLow; n <= kHigh; ++n) if (! isBlack (n) && keyRect (n).contains (p)) return n;
    return -1;
}
void PlayPanel::Keys::paint (juce::Graphics& g)
{
    const auto& pal = panel.P;
    const auto& m = panel.proc.project().play;
    const int selNote = panel.noteForSlice (panel.selSlice);
    for (int pass = 0; pass < 2; ++pass)
        for (int n = kLow; n <= kHigh; ++n)
        {
            if (isBlack (n) != (pass == 1)) continue;
            const auto R = keyRect (n).reduced (0.5f, 0.0f);
            const bool mapped = m.key (n) != nullptr, on = panel.proc.getPlayEngine().isSounding (n);
            juce::Colour c = pass == 0 ? (pal.light ? juce::Colour (0xfff7f5fa) : juce::Colour (0xffd9d5e2)) : (pal.light ? juce::Colour (0xff3a3448) : juce::Colour (0xff17161c));
            if (mapped) c = c.interpolatedWith (pal.wave, pass == 0 ? 0.30f : 0.45f);
            if (on) c = pal.marker;
            g.setColour (c); g.fillRoundedRectangle (R, 3.0f);
            g.setColour (pal.line); g.drawRoundedRectangle (R, 3.0f, 1.0f);
            if (n == selNote) { g.setColour (pal.marker); g.fillRect (R.getX() + 2, R.getBottom() - 4, R.getWidth() - 4, 3.0f); }
            if (pass == 0 && n % 12 == 0)
            {
                g.setFont (LnF::uiFont (10.0f)); g.setColour (juce::Colour (0xff2a2633));
                g.drawText (play::noteName (n), R.withTrimmedTop (R.getHeight() - 18).toNearestInt(), juce::Justification::centred);
            }
        }
}
void PlayPanel::Keys::mouseDown (const juce::MouseEvent& e)
{
    held = noteAt (e.position); if (held < 0) return;
    if (const auto* k = panel.proc.project().play.key (held)) panel.selectPad (k->slice);
    panel.proc.audition (held, juce::jlimit (0.2f, 1.0f, e.position.y / juce::jmax (1.0f, (float) getHeight()) + 0.25f), true);
    repaint();
}
void PlayPanel::Keys::mouseUp (const juce::MouseEvent&) { if (held >= 0) panel.proc.audition (held, 0, false); held = -1; repaint(); }

// ---------------------------------------------------------------- the slice strip
double PlayPanel::Strip::hfAt (float x) const { const double L = (double) juce::jmax<int64> (1, panel.proc.currentPlan().length); return juce::jlimit (0.0, L, (double) x / juce::jmax (1, getWidth()) * L); }
float PlayPanel::Strip::xOf (double hf) const { const double L = (double) juce::jmax<int64> (1, panel.proc.currentPlan().length); return (float) (hf / L * getWidth()); }
int PlayPanel::Strip::cutNear (float x) const
{
    const auto& m = panel.proc.project().play; if (m.mode != SliceMode::Manual) return -1;
    for (size_t i = 0; i < m.manual.size(); ++i) if (std::abs (xOf (panel.proc.pfToHf (m.manual[i])) - x) <= 5.0f) return (int) i;
    return -1;
}
void PlayPanel::Strip::paint (juce::Graphics& g)
{
    const auto& pal = panel.P;
    auto R = getLocalBounds().toFloat();
    if (pal.glass) ghosty::glass::recessed (g, R, 10.0f);   // GHOST: a recessed glass well
    else { g.setColour (pal.waveBg); g.fillRoundedRectangle (R, 6.0f); }
    const auto sl = panel.proc.currentSlices();
    const auto bank = panel.proc.getBank();
    const bool bankOk = bank != nullptr && bank->slices.size() == sl.size();
    const float mid = R.getCentreY() + 4.0f, h = R.getHeight() * 0.36f;
    for (size_t i = 0; i < sl.size(); ++i)
    {
        const float x0 = xOf ((double) sl[i].dst0), x1 = xOf ((double) sl[i].dst1);
        if ((int) i == panel.selSlice)
        {   // the selected slice: a green slime tint + its top edge
            g.setColour (pal.slime.withAlpha (pal.light ? 0.16f : 0.13f)); g.fillRect (x0, R.getY(), x1 - x0, R.getHeight());
            g.setColour (pal.slime); g.fillRect (x0, R.getY(), x1 - x0, 2.5f);
        }
        if (bankOk)
        {
            const auto& T = bank->thumbs[i]; float pk = 1e-6f; for (float v : T) pk = std::max (pk, v);
            g.setColour (pal.wave);
            const float bw = (x1 - x0) / 48.0f;
            for (int k = 0; k < 48; ++k) { const float a = T[(size_t) k] * h; g.fillRect (x0 + (float) k * bw, mid - a, std::max (1.0f, bw), 2.0f * a); }
        }
        g.setColour (pal.marker); g.fillRect (x0, R.getY(), 1.5f, R.getHeight());
        g.setFont (LnF::uiFont (10.0f)); g.setColour (pal.ink);
        if (x1 - x0 > 14) g.drawText (juce::String ((int) i + 1), juce::Rectangle<float> (x0 + 3, R.getY() + 2, 30, 12).toNearestInt(), juce::Justification::centredLeft);
    }
    g.setFont (LnF::uiFont (10.0f, false)); g.setColour (pal.dim);
    const auto mode = panel.proc.project().play.mode;
    g.drawText (mode == SliceMode::Manual ? "click = add a cut, drag = move, double-click = remove" : mode == SliceMode::Equal ? "equal parts of the arrangement"
                : mode == SliceMode::Regions ? "the arrangement's regions" : juce::String (play::modeName (mode)).toLowerCase() + " cuts (SENS / DIV), SPLIT @ SLICES makes them regions",
                getLocalBounds().reduced (6, 2), juce::Justification::bottomRight);
}
void PlayPanel::Strip::mouseDown (const juce::MouseEvent& e)
{
    moved = false; dragCut = cutNear (e.position.x);
    const auto sl = panel.proc.currentSlices();
    for (size_t i = 0; i < sl.size(); ++i) if (e.position.x >= xOf ((double) sl[i].dst0) && e.position.x < xOf ((double) sl[i].dst1)) { panel.selectPad ((int) i); break; }
    if (dragCut >= 0) panel.proc.beginGesture();
}
void PlayPanel::Strip::mouseDrag (const juce::MouseEvent& e)
{
    if (dragCut < 0 || e.getDistanceFromDragStart() < 3) return;
    moved = true;
    PlayMap m = panel.proc.project().play;
    if (dragCut >= (int) m.manual.size()) return;
    m.manual[(size_t) dragCut] = panel.proc.hfToPf (hfAt (e.position.x));
    const double moving = m.manual[(size_t) dragCut];
    if (auto q = ops::setPlay (panel.proc.project(), m)) panel.proc.apply (*q, "Move a cut");
    const auto& mm = panel.proc.project().play.manual;
    for (size_t i = 0; i < mm.size(); ++i) if (mm[i] == moving) dragCut = (int) i;   // (sorted again)
    repaint();
}
void PlayPanel::Strip::mouseUp (const juce::MouseEvent& e)
{
    if (dragCut >= 0) panel.proc.endGesture();
    if (! moved && dragCut < 0 && panel.proc.project().play.mode == SliceMode::Manual && e.getNumberOfClicks() == 1)
    {
        PlayMap m = panel.proc.project().play; m.manual.push_back (panel.proc.hfToPf (hfAt (e.position.x)));
        if (auto q = ops::setPlay (panel.proc.project(), m)) panel.proc.apply (*q, "Add a cut");
    }
    dragCut = -1; panel.refresh(); repaint();
}
void PlayPanel::Strip::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int c = cutNear (e.position.x); if (c < 0) return;
    PlayMap m = panel.proc.project().play; m.manual.erase (m.manual.begin() + c);
    if (auto q = ops::setPlay (panel.proc.project(), m)) panel.proc.apply (*q, "Remove a cut");
    panel.refresh(); repaint();
}
} // namespace gslice::ui
