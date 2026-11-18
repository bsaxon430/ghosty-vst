#include "GhostyPages.h"
#include "GhostyProcessor.h"
#include "GhostyPresets.h"
#include "GhostyMorphSets.h"
#include "synth_parameters.h"
#include "sound_engine.h"
#include "wavetable.h"
#include "line_generator.h"

namespace ghosty {

namespace {
  std::string osc(int i, const char* p) { return "osc_" + std::to_string(i + 1) + "_" + p; }
  std::string env(int i, const char* p) { return "env_" + std::to_string(i + 1) + "_" + p; }
  std::string lfo(int i, const char* p) { return "lfo_" + std::to_string(i + 1) + "_" + p; }
  uint64 hashFloats(std::initializer_list<float> v) {
    uint64 h = 1469598103934665603ull;
    for (float f : v) { uint32 b; std::memcpy(&b, &f, 4); h = (h ^ b) * 1099511628211ull; }
    return h;
  }
  Rectangle<int> inner(Rectangle<int> card) { return card.reduced(14, 10).withTrimmedTop(22); }
}

// ------------------------------------------------------------------ Page helpers
VKnob& Page::knob(const std::string& control, const String& caption) {
  knobs.push_back(std::make_unique<VKnob>(proc, control, caption));
  addAndMakeVisible(*knobs.back());
  return *knobs.back();
}

VSwitch& Page::toggle(const std::string& control, const String& text) {
  switches.push_back(std::make_unique<VSwitch>(proc, control, text));
  addAndMakeVisible(*switches.back());
  return *switches.back();
}

VChoice& Page::choice(const std::string& control) {
  choices.push_back(std::make_unique<VChoice>(proc, control));
  addAndMakeVisible(*choices.back());
  return *choices.back();
}

void Page::syncAll() {
  for (auto& k : knobs) k->sync();
  for (auto& s : switches) s->sync();
  for (auto& c : choices) c->sync();
}

void Page::row(Rectangle<int> r, const std::vector<Component*>& items, int w) {
  const int total = w * (int) items.size();
  int x = r.getX() + jmax(0, (r.getWidth() - total) / 2);
  for (auto* c : items) {
    c->setBounds(x, r.getY(), w, r.getHeight());
    x += w;
  }
}

// ------------------------------------------------------------------ EXTRAS
ExtrasPage::ExtrasPage(GhostyProcessor& p) : Page(p) {
  // KEYS
  auto& k = proc.keys();
  arpOn.onClick = [this] { proc.keys().arp = !proc.keys().arp.load(); };
  arpOn.setTooltip("Plays the notes you hold one by one, locked to your song's tempo");
  holdOn.setButtonText("HOLD");
  holdOn.onClick = [this] { proc.keys().hold = !proc.keys().hold.load(); };
  holdOn.setTooltip("Notes keep playing after you let go; a new chord replaces them; HOLD off lets them go");
  arpOn.setTint(1); holdOn.setTint(1);
  addAndMakeVisible(arpOn); addAndMakeVisible(holdOn);
  auto combo = [this](ComboBox& c, const StringArray& items, const String& tip, std::function<void(int)> set) {
    c.addItemList(items, 1);
    c.setTooltip(tip);
    c.onChange = [&c, set] { if (c.getSelectedId() > 0) set(c.getSelectedId() - 1); };
    addAndMakeVisible(c);
  };
  combo(arpRate, arpRateNames(), "Arp speed, in time with your song (T = triplet, D = dotted)", [this](int i) { proc.keys().arpRate = i; });
  combo(arpMode, arpModeNames(), "The order the arp plays your notes", [this](int i) { proc.keys().arpMode = i; });
  combo(chordPick, chordNames(), "One key plays a whole chord (in the scale when SCALE LOCK is on)", [this](int i) { proc.keys().chord = i; });
  combo(keyPick, keyNames(), "The scale's key", [this](int i) { proc.keys().key = i; });
  combo(scalePick, scaleNames(), "Every key you play lands on a note of this scale", [this](int i) { proc.keys().scale = i; });
  arpOct.setRange(1.0, 4.0, 1.0);
  arpOct.updateSnap();
  arpOct.setDoubleClickReturnValue(true, 1.0);
  arpOct.setValue(k.arpOctaves.load(), dontSendNotification);
  arpOct.onValueChange = [this] { proc.keys().arpOctaves = (int) arpOct.getValue(); };
  arpOct.setTooltip("OCTAVES: how many octaves the arp climbs. Double-click resets");
  arpGate.setRange(0.05, 0.98);
  arpGate.setDoubleClickReturnValue(true, 0.7);
  arpGate.setValue(k.arpGate.load(), dontSendNotification);
  arpGate.onValueChange = [this] { proc.keys().arpGate = (float) arpGate.getValue(); };
  arpGate.setTooltip("GATE: how long each arp note plays, short and plucky .. smooth. Double-click resets");
  addAndMakeVisible(arpOct); addAndMakeVisible(arpGate);

  gateOn.setButtonText("GHOST GATE  OFF");
  gateOn.onClick = [this] { proc.extras().setGateOn(!proc.extras().gateOn()); };
  addAndMakeVisible(gateOn);
  const StringArray pats = Extras::gatePatternNames();
  for (int i = 0; i < 16; ++i) {
    gatePat[i].setButtonText(pats[i]);
    gatePat[i].onClick = [this, i] { proc.extras().setGatePattern(i); if (!proc.extras().gateOn()) proc.extras().setGateOn(true); };
    addAndMakeVisible(gatePat[i]);
  }
  const char* lens[] { "2 BEATS", "1 BAR", "2 BARS" };
  for (int i = 0; i < 3; ++i) {
    gateLen[i].setButtonText(lens[i]);
    gateLen[i].onClick = [this, i] { proc.extras().setGateLength(i); };
    addAndMakeVisible(gateLen[i]);
  }
  gateNote.setText("A tempo-synced volume and time pattern over the whole sound: chops, pumps, triplet gates, tape stops. "
                   "Pick a pattern to switch it on.", dontSendNotification);
  hauntOn.setButtonText("HAUNT  OFF");
  hauntOn.onClick = [this] { proc.extras().setHauntOn(!proc.extras().hauntOn()); };
  addAndMakeVisible(hauntOn);
  const StringArray pads = Extras::hauntPadNames();
  for (int i = 0; i < 16; ++i) {
    hauntPad[i].setButtonText(pads[i]);
    hauntPad[i].setTooltip("Hold to stutter in time; let go to come back");
    hauntPad[i].onStateChange = [this] {
      int held = -1;
      for (int k = 0; k < 16; ++k) if (hauntPad[k].isDown()) held = k;
      proc.extras().holdHauntPad(held);
    };
    addAndMakeVisible(hauntPad[i]);
  }
  hauntNote.setText("Hold a pad while you play: tempo-synced stutters, builds and gates on everything you hear. "
                    "Switch HAUNT on first.", dontSendNotification);
  for (auto* l : { &gateNote, &hauntNote }) {
    l->setFont(LnF::font(13.0f, false));
    l->setJustificationType(Justification::topLeft);
    addAndMakeVisible(*l);
  }
}

void ExtrasPage::sync() {
  auto& k = proc.keys();
  arpOn.setToggleState(k.arp.load(), dontSendNotification);
  arpOn.setButtonText(k.arp.load() ? "ARPEGGIATOR  ON" : "ARPEGGIATOR  OFF");
  holdOn.setToggleState(k.hold.load(), dontSendNotification);
  auto pick = [](ComboBox& c, int i) { if (!c.isPopupActive() && c.getSelectedId() != i + 1) c.setSelectedId(i + 1, dontSendNotification); };
  pick(arpRate, k.arpRate.load()); pick(arpMode, k.arpMode.load()); pick(chordPick, k.chord.load());
  pick(keyPick, k.key.load()); pick(scalePick, k.scale.load());
  if (!arpOct.isMouseButtonDown() && !arpOct.orbit.spinning()) arpOct.setValue(k.arpOctaves.load(), dontSendNotification);
  if (!arpGate.isMouseButtonDown() && !arpGate.orbit.spinning()) arpGate.setValue(k.arpGate.load(), dontSendNotification);
  arpOct.colours = arpGate.colours = orbitColours();
  keyPick.setEnabled(k.scale.load() > 0);
  const int count = k.arpCounter.load();
  if (count != lastArpCount) { lastArpCount = count; arpFlash = 1.0f; }
  else arpFlash *= 0.75f;
  repaint(rKeys);
  auto& x = proc.extras();
  gateOn.setToggleState(x.gateOn(), dontSendNotification);
  gateOn.setButtonText(x.gateOn() ? "GHOST GATE  ON" : "GHOST GATE  OFF");
  hauntOn.setToggleState(x.hauntOn(), dontSendNotification);
  hauntOn.setButtonText(x.hauntOn() ? "HAUNT  ON" : "HAUNT  OFF");
  for (int i = 0; i < 16; ++i) gatePat[i].setToggleState(x.gateOn() && x.gatePattern() == i, dontSendNotification);
  for (int i = 0; i < 3; ++i) gateLen[i].setToggleState(x.gateLength() == i, dontSendNotification);
  for (auto& b : hauntPad) b.setEnabled(x.hauntOn());
}

void ExtrasPage::resized() {
  // short windows (a host may make it as small as 960 x 640): the same parts, tighter, the notes under GATE / HAUNT hidden,
  // the pad rows as tall as fit; nothing overlaps or leaves the page at any size
  auto all = getLocalBounds();
  compact_ = getHeight() < 560;
  rKeys = all.removeFromTop(compact_ ? 170 : 194);
  all.removeFromTop(compact_ ? 6 : 8);
  all.removeFromBottom(compact_ ? 24 : 26);
  rGate = all.withWidth(getWidth() / 2 - 4);
  rHaunt = all.withTrimmedLeft(getWidth() / 2 + 4);
  {
    auto r = rKeys.reduced(18, 12).withTrimmedTop(36);
    rArp = r.removeFromLeft(r.getWidth() * 50 / 100);
    r.removeFromLeft(16);
    rChord = r.removeFromLeft(r.getWidth() * 40 / 100);
    r.removeFromLeft(16);
    rScale = r;
    auto a = rArp;
    rArpKnobs = a.removeFromRight(190);
    auto row = a.removeFromTop(34);
    arpOn.setBounds(row.removeFromLeft(jmin(200, row.getWidth() - 98)));
    row.removeFromLeft(8);
    holdOn.setBounds(row.removeFromLeft(90));
    a.removeFromTop(26);
    row = a.removeFromTop(30);
    arpRate.setBounds(row.removeFromLeft(jmin(110, row.getWidth() / 2 - 4)));
    row.removeFromLeft(8);
    arpMode.setBounds(row.removeFromLeft(jmin(150, row.getWidth())));
    auto kn = rArpKnobs.withTrimmedTop(16).withTrimmedBottom(16);
    const int ks = jmin(80, kn.getHeight());
    arpOct.setBounds(kn.removeFromLeft(95).withSizeKeepingCentre(ks, ks));
    arpGate.setBounds(kn.withSizeKeepingCentre(ks, ks));
    auto c = rChord;
    c.removeFromTop(26);
    chordPick.setBounds(c.removeFromTop(32).withWidth(jmin(200, c.getWidth())));
    auto sc = rScale;
    sc.removeFromTop(26);
    auto srow = sc.removeFromTop(32);
    keyPick.setBounds(srow.removeFromLeft(70));
    srow.removeFromLeft(8);
    scalePick.setBounds(srow.withWidth(jmin(200, srow.getWidth())));
  }
  auto grid = [](Rectangle<int> r, Pill* pills, int n, int cols, int h, int gap) {
    const int w = r.getWidth() / cols;
    for (int i = 0; i < n; ++i)
      pills[i].setBounds(r.getX() + (i % cols) * w + 3, r.getY() + (i / cols) * (h + gap), w - 6, h);
  };
  const int gap = compact_ ? 5 : 8;
  gateNote.setVisible(!compact_);
  hauntNote.setVisible(!compact_);
  auto r = rGate.reduced(18, compact_ ? 8 : 12).withTrimmedTop(compact_ ? 24 : 28);
  gateOn.setBounds(r.removeFromTop(compact_ ? 30 : 40).withWidth(jmin(240, r.getWidth())));
  r.removeFromTop(compact_ ? 6 : 6);
  if (!compact_) { gateNote.setBounds(r.removeFromTop(36)); r.removeFromTop(4); }
  {
    // 4 rows of patterns, a little space, 1 row of lengths
    const int h = jlimit(16, 30, (r.getHeight() - 6 - 5 * gap) / 5);
    grid(r.removeFromTop(4 * (h + gap)), gatePat, 16, 4, h, gap);
    r.removeFromTop(6);
    grid(r.removeFromTop(h + gap), gateLen, 3, 3, h, gap);
  }
  r = rHaunt.reduced(18, compact_ ? 8 : 12).withTrimmedTop(compact_ ? 24 : 28);
  hauntOn.setBounds(r.removeFromTop(compact_ ? 30 : 40).withWidth(jmin(240, r.getWidth())));
  r.removeFromTop(6);
  if (!compact_) { hauntNote.setBounds(r.removeFromTop(36)); r.removeFromTop(4); }
  {
    const int h = jlimit(16, 40, (r.getHeight() - 4 * gap) / 4);
    grid(r.removeFromTop(4 * (h + gap)), hauntPad, 16, 4, h, gap);
  }
}

void ExtrasPage::paint(Graphics& g) {
  const auto& p = pal();
  drawCard(g, rKeys.toFloat(), "KEYS", p.accent);
  g.setFont(LnF::font(10.5f));
  g.setColour(p.dim);
  g.drawText("ARPEGGIATOR", rArp.withHeight(14).translated(0, -18), Justification::centredLeft);
  g.drawText("RATE", arpRate.getBounds().translated(0, -16).withHeight(14), Justification::centredLeft);
  g.drawText("ORDER", arpMode.getBounds().translated(0, -16).withHeight(14), Justification::centredLeft);
  g.drawText("CHORDS", rChord.withHeight(14).translated(0, -18), Justification::centredLeft);
  g.drawText("SCALE LOCK", rScale.withHeight(14).translated(0, -18), Justification::centredLeft);
  g.drawText("KEY", keyPick.getBounds().translated(0, -16).withHeight(14), Justification::centredLeft);
  g.drawText("SCALE", scalePick.getBounds().translated(0, -16).withHeight(14), Justification::centredLeft);
  g.drawText("OCTAVES", arpOct.getBounds().withHeight(14).translated(0, -16), Justification::centred);
  g.drawText("GATE", arpGate.getBounds().withHeight(14).translated(0, -16), Justification::centred);
  g.setColour(p.ink);
  g.setFont(LnF::font(11.0f));
  g.drawText(String(proc.keys().arpOctaves.load()) + (proc.keys().arpOctaves.load() == 1 ? " OCT" : " OCTS"),
             arpOct.getBounds().withHeight(14).translated(0, arpOct.getHeight() + 2), Justification::centred);
  g.drawText(String(roundToInt(proc.keys().arpGate.load() * 100.0f)) + "%", arpGate.getBounds().withHeight(14).translated(0, arpGate.getHeight() + 2), Justification::centred);
  // the arp's step light
  if (proc.keys().arp.load()) {
    auto dot = arpOn.getBounds().toFloat().withX((float) arpOn.getRight() - 22.0f).withWidth(10.0f).withSizeKeepingCentre(10.0f, 10.0f);
    g.setColour(p.green.withAlpha(0.25f + 0.75f * arpFlash));
    g.fillEllipse(dot.translated(0, 0).withX((float) holdOn.getRight() + 12.0f));
  }
  g.setColour(p.dim);
  g.setFont(LnF::font(12.0f, false));
  auto note = [&g](Rectangle<int> r, int top, const String& text) { g.drawFittedText(text, r.withTrimmedTop(top), Justification::topLeft, 4); };
  if (!compact_) note(rArp.withTrimmedRight(196), 102, "Hold notes and the arp plays them one by one in time with your song. Works with CHORDS and HOLD.");
  note(rChord, 68, "One key plays a whole chord. With SCALE LOCK on, the chords follow the scale.");
  note(rScale, 68, "Every key you play lands on a note of the scale: no wrong notes.");
  drawCard(g, rGate.toFloat(), "GHOST GATE", p.accent3);
  drawCard(g, rHaunt.toFloat(), "HAUNT", p.wave1);
  g.setColour(p.dim);
  g.setFont(LnF::font(11.5f, false));
  g.drawText("Ghosty Pro's own extras: KEYS change the notes the synth plays, GATE and HAUNT come after its sound. Off until you switch them on.",
             getLocalBounds().removeFromBottom(26).reduced(18, 4), Justification::centredRight);
}

// ------------------------------------------------------------------ MORPH
MorphPage::MorphPage(GhostyProcessor& p) : Page(p) {
  auto& m = proc.morph();
  board_ = std::make_unique<spirit::BoardView>(m.board);
  board_->palette = []() -> const themes::Palette& { return boardPalette(); };
  board_->onPuckMoved = [this](float x, float y) { proc.morph().puck_x = x; proc.morph().puck_y = y; };
  board_->onAddGhost = [this](float x, float y) { addGhostFromCurrent(x, y); };
  board_->requestScene = [this](int k) { ghostMenu(k); };
  board_->onRemoveGhost = [this](int k) {
    auto& mm = proc.morph();
    if (isPositiveAndBelow(k, (int) mm.board.ghosts.size())) { mm.ghost_values.erase(mm.board.ghosts[(size_t) k].id); mm.board.remove(k); }
    edited();
  };
  board_->onGhostMoved = [this](int, float, float) { edited(); };
  board_->onRenamed = [this](int) { edited(); };
  // the puck the audio is using (MOTION moves it). While no audio block has run lately (FL Studio switches a silent instrument
  // off, or the project is stopped) the board still shows the motion, on the UI's own clock (Morph::idlePuck): a picture only.
  board_->livePuck = [this] {
    auto& mm = proc.morph();
    if (mm.enabled.load() && mm.motion.load() != Morph::kStill)
      return mm.shared.audioFresh() ? Point<float>(mm.shared.livePx.load(), mm.shared.livePy.load()) : mm.idlePuck();
    return Point<float>(mm.puck_x.load(), mm.puck_y.load());
  };
  board_->liveWeights = [this] {
    std::vector<float> w; auto& mm = proc.morph();
    const int n = mm.shared.liveN.load();
    if (mm.enabled.load() && mm.shared.audioFresh())
      for (int k = 0; k < n; ++k) w.push_back(mm.shared.liveW[(size_t) k].load());
    if (w.empty()) { const auto pk = board_->livePuck(); mm.board.weightsAt(pk.x, pk.y, w); }
    return w;
  };
  addAndMakeVisible(*board_);

  on_.setButtonText("MORPH ON");
  on_.onClick = [this] {
    auto& mm = proc.morph();
    if (!mm.enabled.load() && mm.board.ghosts.empty()) {   // the first switch-on: the current sound becomes ghost A
      addGhostFromCurrent(0.25f, 0.5f);
    }
    mm.enabled = !mm.enabled.load();
  };
  addAndMakeVisible(on_);
  sets_.setButtonText("MORPH SETS");
  sets_.setTooltip("Ready-made boards: a sound and four ghosts that blend well, with their motion");
  sets_.onClick = [this] { setsMenu(); };
  addAndMakeVisible(sets_);
  addCurrent_.setButtonText("+ GHOST: THIS SOUND");
  addCurrent_.setTooltip("Adds a ghost holding the knobs of the sound playing now (tweak, then add another to morph between them)");
  addCurrent_.onClick = [this] { auto& b = proc.morph().board; addGhostFromCurrent(0.2f + 0.6f * spirit::hash01((uint32) b.ghosts.size() * 7 + 1), 0.2f + 0.6f * spirit::hash01((uint32) b.ghosts.size() * 7 + 2)); };
  addAndMakeVisible(addCurrent_);
  addPreset_.setButtonText("+ GHOST: A PRESET");
  addPreset_.setTooltip("Adds a ghost holding another preset's knob settings (its wavetables and routings stay the loaded sound's)");
  addPreset_.onClick = [this] { addFromPresetMenu(); };
  addAndMakeVisible(addPreset_);
  circle_.setButtonText("ARRANGE");
  circle_.onClick = [this] { proc.morph().board.arrangeInCircle(); edited(); };
  addAndMakeVisible(circle_);
  clear_.setButtonText("CLEAR");
  clear_.onClick = [this] { auto& mm = proc.morph(); mm.enabled = false; mm.board.ghosts.clear(); mm.ghost_values.clear(); mm.ghost_names.clear(); edited(); };
  addAndMakeVisible(clear_);
  for (int i = 0; i < Morph::kNumMotions; ++i) {
    motionPick_[i].setButtonText(Morph::motionName(i));
    motionPick_[i].onClick = [this, i] {
      if (i == Morph::kDrawn && !proc.morph().hasDrawnPath()) { drawArmed_ = true; return; }   // draw one first
      proc.morph().motion = i;
    };
    addAndMakeVisible(motionPick_[i]);
  }
  motionPick_[Morph::kStill].setTooltip("The puck stays where you put it");
  motionPick_[Morph::kCircle].setTooltip("Circles around where you put the puck (SIZE = how far)");
  motionPick_[Morph::kEight].setTooltip("A figure 8 around the puck");
  motionPick_[Morph::kWander].setTooltip("Wanders to a new random spot every period");
  motionPick_[Morph::kGlide].setTooltip("Glides smoothly from ghost to ghost, A -> B -> C ... one leg per period");
  motionPick_[Morph::kStep].setTooltip("One ghost at a time: jumps to the next ghost every period");
  motionPick_[Morph::kDrawn].setTooltip("Follows the path you draw (DRAW, then drag on the board)");
  for (int i = 0; i < (int) Morph::divisions().size(); ++i) division_.addItem(Morph::divisions()[(size_t) i].name, i + 1);
  division_.setTooltip("How long one period lasts, locked to your DAW's tempo");
  division_.onChange = [this] { if (division_.getSelectedId() > 0) proc.morph().division = division_.getSelectedId() - 1; };
  addAndMakeVisible(division_);
  sync_.setButtonText("TEMPO SYNC");
  sync_.setTooltip("On: periods follow the DAW's tempo and beat. Off: a free rate in Hz");
  sync_.onClick = [this] { proc.morph().sync = !proc.morph().sync.load(); };
  addAndMakeVisible(sync_);
  draw_.setButtonText("DRAW A PATH");
  draw_.setTooltip("Then drag on the board: the puck will follow your path, at the speed you drew it, one loop per period");
  draw_.onClick = [this] { drawArmed_ = !drawArmed_; };
  addAndMakeVisible(draw_);
  snap_.setButtonText("SNAP");
  snap_.setTooltip("When you let go of the puck it lands on the nearest ghost");
  snap_.onClick = [this] { auto& b = proc.morph().board; b.snapToGhost = !b.snapToGhost; edited(); };
  addAndMakeVisible(snap_);
  blend_.addItemList({ "BLEND: SMOOTH", "BLEND: SOFT ISLANDS", "BLEND: NEAREST 2" }, 1);
  blend_.setTooltip("How the ghosts mix: everywhere, in soft islands around each ghost, or only the two nearest");
  blend_.onChange = [this] {
    auto& w = proc.morph().board.weights;
    const int id = blend_.getSelectedId();
    w.mode = id == 2 ? spirit::WeightMode::Gaussian : id == 3 ? spirit::WeightMode::NearestN : spirit::WeightMode::InverseDistance;
    w.nearest = 2;
    edited();
  };
  addAndMakeVisible(blend_);
  for (auto* sl : { &hz_, &depth_, &power_ })
    addAndMakeVisible(*sl);
  hz_.setRange(0.02, 16.0); hz_.setSkewFactorFromMidPoint(0.5); hz_.setDoubleClickReturnValue(true, 0.25);
  depth_.setDoubleClickReturnValue(true, 0.3);
  power_.setDoubleClickReturnValue(true, 2.0);
  hz_.setTooltip("RATE: how fast the free motion moves (when TEMPO SYNC is off). Flick it, Shift for fine, double-click resets");
  depth_.setTooltip("SIZE: how far circle / eight / wander travel. Double-click resets");
  power_.setTooltip("GHOST PULL: soft blends .. sharp islands. Double-click resets");
  hz_.onValueChange = [this] { proc.morph().hz = (float) hz_.getValue(); };
  depth_.setRange(0.0, 0.5);
  depth_.onValueChange = [this] { proc.morph().depth = (float) depth_.getValue(); };
  power_.setRange(0.5, 8.0); power_.setSkewFactorFromMidPoint(2.0);
  power_.onValueChange = [this] { proc.morph().board.weights.power = (float) power_.getValue(); edited(); };
  // DRAW: while armed, the board's left mouse draws a path instead of moving things
  board_->onBoardMouse = [this](const MouseEvent& e, int phase) {
    if (!drawArmed_ && !drawing_) return false;
    const auto p = board_->toBoard(e.position);
    const double now = Time::getMillisecondCounterHiRes() * 0.001;
    if (phase == 0) { drawing_ = true; stroke_.clear(); strokeSecs_.clear(); }
    if (drawing_) { stroke_.push_back(p); strokeSecs_.push_back(now); board_->repaint(); }
    if (phase == 2 && drawing_) {
      drawing_ = false; drawArmed_ = false;
      if (stroke_.size() >= 3) {
        auto path = spirit::OrbitPath::fromStroke(stroke_, strokeSecs_, 1);
        if (!path.empty()) { proc.morph().setDrawnPath(path); proc.morph().motion = Morph::kDrawn; }
      }
      stroke_.clear();
      edited();
    }
    return true;
  };
  board_->paintPath = [this](Graphics& g) {
    const auto& pl = pal();
    Path line;
    if (drawing_ && stroke_.size() > 1) {
      for (size_t i = 0; i < stroke_.size(); ++i) { auto q = board_->toScreen(stroke_[i].x, stroke_[i].y); if (i == 0) line.startNewSubPath(q); else line.lineTo(q); }
    }
    else if (proc.morph().hasDrawnPath()) {
      std::vector<float> xy((size_t) spirit::kPathTable * 2);
      proc.morph().board.paths[0].bake(xy.data());
      for (int k = 0; k <= spirit::kPathTable; k += 8) {
        const int i = k % spirit::kPathTable;
        auto q = board_->toScreen(xy[(size_t) (2 * i)], xy[(size_t) (2 * i + 1)]);
        if (k == 0) line.startNewSubPath(q); else line.lineTo(q);
      }
    }
    if (!line.isEmpty()) {
      g.setColour(pl.wave1.withAlpha(proc.morph().motion.load() == Morph::kDrawn || drawing_ ? 0.85f : 0.3f));
      g.strokePath(line, PathStrokeType(2.5f, PathStrokeType::curved, PathStrokeType::rounded));
    }
  };
  tips_.setText("Each ghost holds a sound's knobs. Drag the glowing puck between them: the synth's knobs blend smoothly; switches "
                "and choices follow the strongest ghost. Click empty board to add a ghost of the sound playing now; right-click a "
                "ghost for more. MOTION moves the puck for you, in time with your song. Off = the preset exactly as it is.", dontSendNotification);
  tips_.setFont(LnF::font(12.5f, false));
  tips_.setJustificationType(Justification::topLeft);
  addAndMakeVisible(tips_);
}

void MorphPage::setsMenu() {
  PopupMenu menu;
  menu.addSectionHeader("MORPH SETS: a sound + four ghosts that blend well");
  const auto& sets = morphSets();
  for (int i = 0; i < (int) sets.size(); ++i)
    menu.addItem(1 + i, String(sets[(size_t) i].name) + "   -   " + sets[(size_t) i].hint);
  menu.showMenuAsync(PopupMenu::Options().withTargetComponent(&sets_), [this](int id) {
    if (id <= 0) return;
    drawArmed_ = drawing_ = false;
    String err;
    if (!applyMorphSet(proc, id - 1, err)) { tips_.setText(err, dontSendNotification); return; }
    edited();
    sync();
  });
}

void MorphPage::edited() {
  proc.morph().publish();
  board_->refresh();
}

void MorphPage::addGhostFromCurrent(float x, float y) {
  auto& m = proc.morph();
  const int k = m.board.add(x, y);
  if (k < 0) return;
  const auto& g = m.board.ghosts[(size_t) k];
  m.ghost_values[g.id] = m.captureCurrent();
  m.ghost_names[g.id] = proc.currentPresetName();
  m.board.ghosts[(size_t) k].userData = proc.currentPresetName();
  edited();
}

void MorphPage::addFromPresetMenu() {
  PopupMenu menu, ours;
  const auto& list = factoryPresets();
  std::map<String, PopupMenu> cats;
  for (int i = 0; i < (int) list.size(); ++i) cats[list[(size_t) i].category].addItem(1 + i, list[(size_t) i].name);
  for (auto& c : cats) ours.addSubMenu(c.first, c.second);
  menu.addSubMenu("Ghosty Pro presets", ours);
  std::vector<FoundPreset> found = foundPresets ? foundPresets() : std::vector<FoundPreset>();
  PopupMenu mine;
  std::map<String, PopupMenu> groups;
  for (int i = 0; i < (int) found.size() && i < 4000; ++i) groups[found[(size_t) i].group].addItem(10000 + i, found[(size_t) i].name);
  for (auto& gg : groups) mine.addSubMenu(gg.first, gg.second);
  menu.addSubMenu("My presets", mine, !found.empty());
  menu.showMenuAsync(PopupMenu::Options().withTargetComponent(&addPreset_), [this, found](int id) {
    if (id <= 0) return;
    String text, name;
    if (id < 10000) { const auto& p = factoryPresets()[(size_t) (id - 1)]; text = p.text; name = p.name; }
    else { const auto& f = found[(size_t) (id - 10000)]; text = f.file.loadFileAsString(); name = f.name; }
    auto& m = proc.morph();
    const int k = m.board.add(0.2f + 0.6f * spirit::hash01((uint32) m.board.ghosts.size() * 13 + 5), 0.2f + 0.6f * spirit::hash01((uint32) m.board.ghosts.size() * 13 + 6));
    if (k < 0) return;
    const auto id2 = m.board.ghosts[(size_t) k].id;
    m.ghost_values[id2] = m.captureFromPresetJson(text);
    m.ghost_names[id2] = name;
    m.board.ghosts[(size_t) k].userData = name;
    edited();
  });
}

void MorphPage::ghostMenu(int k) {
  auto& m = proc.morph();
  if (!isPositiveAndBelow(k, (int) m.board.ghosts.size())) return;
  PopupMenu menu;
  const String id = m.board.ghosts[(size_t) k].id;
  menu.addSectionHeader("Ghost " + m.board.ghosts[(size_t) k].label + ": " + m.ghost_names[id]);
  menu.addItem(1, "Set to the sound playing now");
  menu.addItem(2, "Remove");
  menu.showMenuAsync(PopupMenu::Options(), [this, id](int r) {
    auto& mm = proc.morph();
    const int kk = mm.board.indexOf(id);
    if (kk < 0) return;
    if (r == 1) { mm.ghost_values[id] = mm.captureCurrent(); mm.ghost_names[id] = proc.currentPresetName(); mm.board.ghosts[(size_t) kk].userData = proc.currentPresetName(); }
    if (r == 2) { mm.ghost_values.erase(id); mm.board.remove(kk); }
    edited();
  });
}

void MorphPage::sync() {
  auto& m = proc.morph();
  on_.setToggleState(m.enabled.load(), dontSendNotification);
  for (int i = 0; i < Morph::kNumMotions; ++i) motionPick_[i].setToggleState(m.motion.load() == i, dontSendNotification);
  draw_.setToggleState(drawArmed_ || drawing_, dontSendNotification);
  draw_.setButtonText(drawArmed_ || drawing_ ? "DRAW ON THE BOARD..." : "DRAW A PATH");
  sync_.setToggleState(m.sync.load(), dontSendNotification);
  snap_.setToggleState(m.board.snapToGhost, dontSendNotification);
  division_.setVisible(m.sync.load());
  hz_.setEnabled(!m.sync.load());
  for (auto* k : { &hz_, &depth_, &power_ }) k->colours = orbitColours();
  if (!division_.isPopupActive()) division_.setSelectedId(m.division.load() + 1, dontSendNotification);
  if (!blend_.isPopupActive()) blend_.setSelectedId(m.board.weights.mode == spirit::WeightMode::Gaussian ? 2 : m.board.weights.mode == spirit::WeightMode::NearestN ? 3 : 1, dontSendNotification);
  if (!hz_.isMouseButtonDown() && !hz_.orbit.spinning()) hz_.setValue(m.hz.load(), dontSendNotification);
  if (!depth_.isMouseButtonDown() && !depth_.orbit.spinning()) depth_.setValue(m.depth.load(), dontSendNotification);
  if (!power_.isMouseButtonDown() && !power_.orbit.spinning()) power_.setValue(m.board.weights.power, dontSendNotification);
  if (board_->syncLive()) board_->repaint();
  m.collect();
  repaint(rSide);
}

void MorphPage::resized() {
  rSide = getLocalBounds().removeFromRight(320);
  rBoard = getLocalBounds().withTrimmedRight(328);
  board_->setBounds(rBoard.reduced(14, 10).withTrimmedTop(26));
  auto r = rSide.reduced(14, 10).withTrimmedTop(26);
  {
    auto top = r.removeFromTop(32);
    on_.setBounds(top.removeFromLeft(top.getWidth() / 2 - 3));
    top.removeFromLeft(6);
    sets_.setBounds(top);
  }
  r.removeFromTop(8);
  auto two = r.removeFromTop(28);
  addCurrent_.setBounds(two.removeFromLeft(two.getWidth() / 2 - 3));
  two.removeFromLeft(6);
  addPreset_.setBounds(two);
  r.removeFromTop(6);
  two = r.removeFromTop(26);
  const int w3 = two.getWidth() / 3;
  circle_.setBounds(two.removeFromLeft(w3).reduced(2, 0));
  snap_.setBounds(two.removeFromLeft(w3).reduced(2, 0));
  clear_.setBounds(two.reduced(2, 0));
  r.removeFromTop(22);   // MOTION caption
  for (int row = 0; row < 2; ++row) {
    auto line = r.removeFromTop(28);
    const int per = row == 0 ? 4 : 3, w = line.getWidth() / 4;
    for (int i = 0; i < per; ++i) motionPick_[row * 4 + i].setBounds(line.removeFromLeft(w).reduced(2, 0));
    if (row == 1) draw_.setBounds(Rectangle<int>());
    r.removeFromTop(4);
  }
  draw_.setBounds(r.removeFromTop(28));
  r.removeFromTop(8);
  auto rateRow = r.removeFromTop(28);
  sync_.setBounds(rateRow.removeFromLeft(118));
  rateRow.removeFromLeft(8);
  division_.setBounds(rateRow);
  r.removeFromTop(8);
  blend_.setBounds(r.removeFromTop(26));
  r.removeFromTop(6);
  rKnobs = r.removeFromTop(104);
  {
    auto k = rKnobs.withTrimmedTop(14).withTrimmedBottom(16);
    const int w = k.getWidth() / 3;
    for (auto* kn : { &hz_, &depth_, &power_ }) {
      auto cell = k.removeFromLeft(w);
      kn->setBounds(cell.withSizeKeepingCentre(jmin(cell.getWidth(), cell.getHeight()), cell.getHeight()));
    }
  }
  r.removeFromTop(4);
  tips_.setBounds(r);
}

void MorphPage::paint(Graphics& g) {
  const auto& p = pal();
  drawCard(g, rBoard.toFloat(), "SPIRIT BOARD", p.accent);
  drawCard(g, rSide.toFloat(), "MORPH", p.accent2);
  g.setColour(p.dim);
  g.setFont(LnF::font(10.5f));
  g.drawText("MOTION", motionPick_[0].getBounds().translated(0, -18).withHeight(14).withWidth(200), Justification::centredLeft);
  // the three GHOST ORBIT knobs: caption above, value below
  const int w = rKnobs.getWidth() / 3;
  const String names[] { "RATE", "SIZE", "PULL" };
  const String values[] { proc.morph().sync.load() ? String("SYNCED") : String(proc.morph().hz.load(), 2) + " Hz",
                          String(roundToInt(proc.morph().depth.load() * 200.0f)) + "%",
                          String(proc.morph().board.weights.power, 1) };
  for (int i = 0; i < 3; ++i) {
    auto cell = rKnobs.withX(rKnobs.getX() + i * w).withWidth(w);
    g.setColour(p.dim);
    g.setFont(LnF::font(10.5f));
    g.drawText(names[i], cell.removeFromTop(14), Justification::centred);
    g.setColour(p.ink);
    g.setFont(LnF::font(11.0f));
    g.drawText(values[i], cell.removeFromBottom(16), Justification::centred);
  }
}

} // namespace ghosty
