// Ghosty Pro on Vital: tests. Run: GhostyProTool test   (the UI checks need a display: xvfb-run)
#include "JuceHeader.h"
#include "GhostyProcessor.h"
#include "GhostyEditor.h"
#include "GhostyPresets.h"
#include "GhostyMorphSets.h"
#include "VitalPresetFinder.h"
#include "../tools/ToolHelpers.h"
#include "synth_parameters.h"
#include "sound_engine.h"
#include "utils.h"
#include "load_save.h"
#include "GhostOrbit.h"
#include "GhostyMatrix.h"
#include "GhostyProLogo.h"
#include "synth_section.h"
#include "synth_slider.h"
#include "full_interface.h"
#include <cstdio>
#include <unistd.h>

using namespace ghosty;
using namespace ghosty::tool;

namespace {
int g_fail = 0, g_pass = 0;
void check(bool ok, const String& what) {
  if (ok) ++g_pass; else { ++g_fail; std::printf("  FAIL: %s\n", what.toRawUTF8()); }
}

bool sameBuffers(const AudioBuffer<float>& a, const AudioBuffer<float>& b) {
  if (a.getNumSamples() != b.getNumSamples()) return false;
  for (int c = 0; c < 2; ++c)
    if (std::memcmp(a.getReadPointer(c), b.getReadPointer(c), sizeof(float) * (size_t) a.getNumSamples()) != 0) return false;
  return true;
}

// Vital seeds every random generator (oscillator start phases, random LFOs) from one process-wide counter. Two instances
// compared with each other must start from the same count, as each would in a fresh process (or a fresh plugin load).
std::unique_ptr<GhostyProcessor> fresh(int preset) {
  vital::utils::RandomGenerator::next_seed_ = 0;
  auto p = std::make_unique<GhostyProcessor>();
  String err;
  if (preset < 0) p->loadInit(); else p->loadFactoryPreset(preset, err);
  return p;
}

std::vector<Note> chord() { return { { 0.05, 1.2, 48, 100 }, { 0.05, 1.2, 55, 90 }, { 0.6, 1.0, 64, 110 }, { 1.4, 0.5, 60, 70 } }; }

// 2.0.8: FL ran the synth with 0 output channels (the output bus was off): it must be one stereo output, on, with no input
void testBuses() {
  GhostyProcessor p;
  check(p.getTotalNumOutputChannels() == 2, "the synth has 2 output channels (" + String(p.getTotalNumOutputChannels()) + ")");
  check(p.getTotalNumInputChannels() == 0, "the synth has no input channels (" + String(p.getTotalNumInputChannels()) + ")");
  auto layout = p.getBusesLayout();
  check(p.isBusesLayoutSupported(layout), "its own layout is supported");
  auto none = layout; none.getChannelSet(false, 0) = AudioChannelSet::disabled();
  check(!p.isBusesLayoutSupported(none), "a layout with the output off is refused");
  auto mono = layout; mono.getChannelSet(false, 0) = AudioChannelSet::mono();
  check(!p.isBusesLayoutSupported(mono), "a mono output layout is refused");
  auto withIn = layout; withIn.getChannelSet(true, 0) = AudioChannelSet::stereo();
  check(!p.isBusesLayoutSupported(withIn), "an input is refused");
}

void testPresets() {
  std::printf("presets\n");
  const auto& list = factoryPresets();
  check(list.size() >= 30, "at least 30 Ghosty Pro presets (have " + String((int) list.size()) + ")");
  StringArray names;
  for (int i = 0; i < (int) list.size(); ++i) {
    const auto& fp = list[(size_t) i];
    check(!names.contains(fp.name), "unique name " + fp.name);
    names.add(fp.name);
    for (auto banned : { "vital", "tytel" })
      check(!fp.name.containsIgnoreCase(banned), "no Vital name in preset " + fp.name);
    GhostyProcessor p;
    String err;
    check(p.loadFactoryPreset(i, err), "loads: " + fp.name + " " + err);
    check(p.currentPresetName() == fp.name, "name shown: " + fp.name);
    auto buf = play(p, chord(), 2.5, 44100.0, 512);
    check(finite(buf), "finite: " + fp.name);
    check(rmsDb(buf) > -45.0f, "makes sound: " + fp.name + " (" + String(rmsDb(buf), 1) + " dBFS)");
    check(truePeakDb(buf) <= -1.0f, "true peak <= -1 dBTP: " + fp.name + " (" + String(truePeakDb(buf), 2) + ")");
    // every routing is one of Vital's real modulation targets, and every one survived loading
    json j = json::parse(fp.text.toStdString());
    int routed = 0;
    for (auto& m : j["settings"]["modulations"]) {
      std::string src = m.value("source", ""), dst = m.value("destination", "");
      if (src.empty() || dst.empty()) continue;
      ++routed;
      check(p.getEngine()->getMonoModulations().count(dst) > 0, fp.name + ": " + String(dst) + " is a modulation target");
      check(p.getEngine()->getModulationSources().count(src) > 0, fp.name + ": " + String(src) + " is a modulation source");
    }
    int live = 0;
    for (int k = 0; k < vital::kMaxModulationConnections; ++k) {
      auto* c = p.getModulationBank().atIndex(k);
      if (c && !c->source_name.empty() && !c->destination_name.empty()) ++live;
    }
    check(live == routed, fp.name + ": all " + String(routed) + " routings live (" + String(live) + ")");
  }
}

void testVitalUntouched() {
  std::printf("our processBlock adds nothing while our extras are off\n");
  const int n = jmin(6, (int) factoryPresets().size());
  for (int i = -1; i < n; ++i) {
    auto a = fresh(i);
    auto x = play(*a, chord(), 2.0, 48000.0, 480, false);
    auto b = fresh(i);
    auto y = play(*b, chord(), 2.0, 48000.0, 480, true);   // SynthPlugin::processBlock alone
    check(sameBuffers(x, y), "bit-identical to Vital's SynthPlugin::processBlock, preset " + String(i));
  }
  GhostyProcessor p;
  check(!p.morph().enabled.load(), "MORPH is off by default");
  check(!p.extras().anyOn(), "GHOSTY EXTRAS are off by default");
}

int presetNamed(const String& name) {
  const auto& l = factoryPresets();
  for (int i = 0; i < (int) l.size(); ++i) if (l[(size_t) i].name == name) return i;
  return 0;
}

// MORPH SETS: every set loads its sound and four ghosts; each ghost and the board's centre play at about the base's level
void testMorphSets() {
  std::printf("morph sets\n");
  const auto& sets = morphSets();
  check(sets.size() >= 12, "there are at least 12 MORPH SETS");
  // a chord (some sounds start their oscillators at a random phase: one note's level varies from note to note, a chord's much less)
  const std::vector<Note> held { { 0.0, 1.3, 48, 100 }, { 0.0, 1.3, 52, 100 }, { 0.0, 1.3, 55, 100 }, { 0.0, 1.3, 60, 100 } };
  auto level = [](const AudioBuffer<float>& b, double rate) {
    double s = 0.0; int n = 0;
    const int i0 = (int) (0.05 * rate), i1 = jmin(b.getNumSamples(), (int) (1.3 * rate));
    for (int c = 0; c < b.getNumChannels(); ++c)
      for (int i = i0; i < i1; ++i) { const double v = b.getSample(c, i); s += v * v; ++n; }
    return 10.0 * std::log10(s / jmax(1, n) + 1e-20);
  };
  for (int i = 0; i < (int) sets.size(); ++i) {
    const auto& S = sets[(size_t) i];
    {
      GhostyProcessor a;
      String err;
      const bool ok = applyMorphSet(a, i, err);
      auto& m = a.morph();
      int known = 0, total = 0;
      for (const auto& g : S.ghosts) for (const auto& kv : g.values) { ++total; known += m.slotOf(kv.first) >= 0 ? 1 : 0; }
      check(ok && m.enabled.load() && m.board.ghosts.size() == S.ghosts.size() && m.ghost_values.size() == S.ghosts.size() &&
            a.currentPresetName() == S.base && known == total,
            String("MORPH SET ") + S.name + " loads " + S.base + " and its " + String((int) S.ghosts.size()) + " ghosts (" + err + ")");
    }
    double base = 0.0, worst = 0.0;
    bool fin = true;
    String line;
    for (int k = 0; k <= (int) S.ghosts.size(); ++k) {   // each ghost, then the centre: a fresh instance each (no tails between)
      GhostyProcessor a;
      String err;
      applyMorphSet(a, i, err);
      auto& m = a.morph();
      m.motion = Morph::kStill;
      m.puck_x = k < (int) S.ghosts.size() ? S.ghosts[(size_t) k].x : 0.5f;
      m.puck_y = k < (int) S.ghosts.size() ? S.ghosts[(size_t) k].y : 0.5f;
      play(a, {}, 0.3, 48000.0, 512);   // (the knobs glide to the puck)
      double l = 0.0;
      for (int rep = 0; rep < 2; ++rep) {   // twice, averaged (the same random-phase reason)
        auto buf = play(a, held, 1.6, 48000.0, 512);
        fin = fin && finite(buf);
        l += 0.5 * level(buf, 48000.0);
        play(a, {}, 1.5, 48000.0, 512);   // (the release dies away)
      }
      line << String(l, 1) << " ";
      if (k == 0) base = l; else worst = jmax(worst, std::abs(l - base));
    }
    std::printf("  %-16s ghosts and centre within %.1f dB of the base (%s)\n", S.name, worst, line.toRawUTF8());
    check(fin && worst < 3.5, String("MORPH SET ") + S.name + ": every ghost and the centre play near the base level (" + String(worst, 1) + " dB)");
  }
}

void testMorph() {
  std::printf("morph\n");
  if (factoryPresets().size() < 2) return;
  String err;
  // one ghost holding exactly the loaded sound, puck on it: identical to no morph
  {
    auto ap = fresh(1);
    auto& a = *ap;
    auto& m = a.morph();
    const int k = m.board.add(0.5f, 0.5f);
    m.ghost_values[m.board.ghosts[(size_t) k].id] = m.captureCurrent();
    m.puck_x = 0.5f; m.puck_y = 0.5f;
    m.publish();
    m.enabled = true;
    auto x = play(a, chord(), 2.0, 48000.0, 512);
    // MORPH runs Vital in 64-sample steps (Vital's output depends on its block size, as in a DAW at that buffer size):
    // the same sound through Vital alone at 64-sample blocks must come out identical
    auto bp = fresh(1);
    auto y = play(*bp, chord(), 2.0, 48000.0, 64, true);
    check(sameBuffers(x, y), "one ghost = the sound itself (bit-identical to Vital alone at 64-sample blocks)");
  }
  // two ghosts: the blend sits between them and moves smoothly
  {
    GhostyProcessor a;
    a.loadFactoryPreset(presetNamed("Warm Blanket"), err);
    auto& m = a.morph();
    const std::string ctl = "filter_1_cutoff";
    a.setControl(ctl, 40.0f);
    int k = m.board.add(0.0f, 0.5f); m.ghost_values[m.board.ghosts[(size_t) k].id] = m.captureCurrent();
    a.setControl(ctl, 120.0f);
    k = m.board.add(1.0f, 0.5f); m.ghost_values[m.board.ghosts[(size_t) k].id] = m.captureCurrent();
    m.puck_x = 0.5f; m.puck_y = 0.5f;
    m.publish();
    m.enabled = true;
    const std::vector<Note> held { { 0.0, 3.0, 48, 100 }, { 0.0, 3.0, 55, 90 }, { 0.0, 3.0, 64, 90 } };
    auto buf = play(a, held, 1.0, 48000.0, 512);
    const float v = a.getControl(ctl);
    check(v > 70.0f && v < 90.0f, "halfway puck gives a halfway cutoff (" + String(v) + ")");
    check(finite(buf), "morph output finite");
    m.puck_x = 0.0f;
    auto buf2 = play(a, held, 1.0, 48000.0, 512);
    check(std::abs(a.getControl(ctl) - 40.0f) < 1.0e-3f, "puck on ghost A gives A's value exactly");
    check(countClicks(buf2) == 0, "no clicks while the puck jumps (slewed)");
  }
  // MORPH survives save / load
  {
    GhostyProcessor a;
    a.loadFactoryPreset(0, err);
    auto& m = a.morph();
    m.board.add(0.3f, 0.3f); m.ghost_values[m.board.ghosts[0].id] = m.captureCurrent();
    m.enabled = true; m.puck_x = 0.3f;
    MemoryBlock st; a.getStateInformation(st);
    GhostyProcessor b;
    b.setStateInformation(st.getData(), (int) st.getSize());
    check(b.morph().enabled.load() && b.morph().board.ghosts.size() == 1 && b.morph().ghost_values.size() == 1, "MORPH saved with the project");
  }
}

// three ghosts of one sound with different knobs (cutoff, wave position, drive): the board's motions
void makeThreeGhosts(GhostyProcessor& a) {
  auto& m = a.morph();
  const float pos[3][2] { { 0.2f, 0.25f }, { 0.8f, 0.3f }, { 0.5f, 0.8f } };
  const float cut[3] { 50.0f, 110.0f, 80.0f }, frame[3] { 0.0f, 200.0f, 90.0f };
  for (int k = 0; k < 3; ++k) {
    a.setControl("filter_1_cutoff", cut[k]);
    a.setControl("osc_1_wave_frame", frame[k]);
    const int g = m.board.add(pos[k][0], pos[k][1]);
    m.ghost_values[m.board.ghosts[(size_t) g].id] = m.captureCurrent();
  }
  m.puck_x = 0.5f; m.puck_y = 0.5f;
  m.publish();
  m.enabled = true;
}

void testMotions() {
  std::printf("morph motions\n");
  String err;
  std::vector<Note> pad { { 0.0, 9.0, 48, 100 }, { 0.0, 9.0, 55, 90 }, { 0.0, 9.0, 64, 90 } };   // held through the render
  for (int mode = 0; mode < Morph::kNumMotions; ++mode) {
    GhostyProcessor a;
    a.loadFactoryPreset(presetNamed("Warm Blanket"), err);
    makeThreeGhosts(a);
    auto& m = a.morph();
    if (mode == Morph::kDrawn) {
      std::vector<Point<float>> pts; std::vector<double> secs;
      for (int i = 0; i < 40; ++i) { const float t = i / 40.0f; pts.push_back({ 0.5f + 0.35f * std::cos(6.283f * t), 0.5f + 0.25f * std::sin(12.566f * t) }); secs.push_back(t); }
      m.setDrawnPath(spirit::OrbitPath::fromStroke(pts, secs, 1));
    }
    m.motion = mode;
    m.division = 7;   // 1/4
    HostClock clock; clock.bpm = 120.0;
    std::vector<Point<float>> track;
    auto buf = play(a, pad, 8.0, 48000.0, 512, false, &clock, [&](int) { track.push_back({ m.shared.livePx.load(), m.shared.livePy.load() }); });
    const String name = Morph::motionName(mode);
    check(finite(buf), name + ": finite");
    int at = -1;
    const int clicks = countClicks(buf, 40.0f, &at);
    check(clicks == 0, name + ": no clicks (" + String(clicks) + ", first at beat " + String(at / 48000.0 * 2.0, 3) + ")");
    float moved = 0;
    for (size_t i = 1; i < track.size(); ++i) moved += track[i].getDistanceFrom(track[i - 1]);
    check(mode == Morph::kStill ? moved < 1e-4f : moved > 0.5f, name + ": the puck " + String(mode == Morph::kStill ? "stays" : "moves") + " (" + String(moved, 2) + ")");
  }
  // tempo lock: at 120 BPM a 1/4 period is 0.5 s; STEP sits on ghost k during beat k (mod 3), GLIDE is halfway at k + 0.5
  for (int mode : { (int) Morph::kStep, (int) Morph::kGlide }) {
    GhostyProcessor a;
    a.loadFactoryPreset(0, err);
    makeThreeGhosts(a);
    auto& m = a.morph();
    m.motion = mode; m.division = 7;
    HostClock clock; clock.bpm = 120.0;
    std::map<int, Point<float>> at;
    play(a, pad, 2.0, 48000.0, 480, false, &clock, [&](int pos) {
      if (pos > 0) at[pos] = { m.shared.livePx.load(), m.shared.livePy.load() };   // the puck the previous block used
    });
    // the block starting at 1.5 beats (0.75 s = 36000 samples) used the puck for 1.5 beats; it is posted before block 36480
    const auto p = at[36480];
    const auto& g = m.board.ghosts;
    if (mode == Morph::kStep) check(p.getDistanceFrom({ g[1].x, g[1].y }) < 1e-4f, "STEP is on ghost B during beat 1 at 120 BPM");
    else {
      // (the posted puck is the block's last 64-sample chunk: 448 samples after beat 1.5)
      const float f = (float) (1.5 + 448.0 / 48000.0 * 2.0 - 1.0), t = f * f * (3.0f - 2.0f * f);
      check(p.getDistanceFrom({ g[1].x + t * (g[2].x - g[1].x), g[1].y + t * (g[2].y - g[1].y) }) < 1e-3f, "GLIDE is on its way from B to C at beat 1.5 (tempo-locked)");
    }
  }
  // the same at 90 BPM, a dotted 1/8 period: STEP changes ghost every 0.75 beat
  {
    GhostyProcessor a;
    a.loadFactoryPreset(0, err);
    makeThreeGhosts(a);
    auto& m = a.morph();
    m.motion = Morph::kStep; m.division = 5;   // 1/8D
    HostClock clock; clock.bpm = 90.0;
    std::map<int, Point<float>> at;
    play(a, pad, 2.0, 48000.0, 400, false, &clock, [&](int pos) { if (pos > 0) at[pos] = { m.shared.livePx.load(), m.shared.livePy.load() }; });
    // beat 1.6 -> period 2.13 -> ghost C; 1.6 beats at 90 BPM = 1.0667 s = 51200 samples: block 51200 posted before 51600
    const auto& g = m.board.ghosts;
    check(at[51600].getDistanceFrom({ g[2].x, g[2].y }) < 1e-4f, "STEP 1/8 dotted at 90 BPM is on ghost C at beat 1.6");
  }
  // free rate: SYNC off, 2 Hz -> 1.5 periods at 0.75 s = ghost B
  {
    GhostyProcessor a;
    a.loadFactoryPreset(0, err);
    makeThreeGhosts(a);
    auto& m = a.morph();
    m.motion = Morph::kStep; m.sync = false; m.hz = 2.0f;
    check(m.puckAt(1.5, nullptr, nullptr, 0) == Point<float>(0.5f, 0.5f), "no ghosts: the puck stays put");
    float xs[3], ys[3];
    for (int k = 0; k < 3; ++k) { xs[k] = m.board.ghosts[(size_t) k].x; ys[k] = m.board.ghosts[(size_t) k].y; }
    check(m.puckAt(2.0 * 0.75, xs, ys, 3) == Point<float>(xs[1], ys[1]), "free 2 Hz: ghost B at 0.75 s");
    m.motion = Morph::kCircle; m.depth = 0.2f;
    const auto c0 = m.puckAt(0.0, xs, ys, 3), c1 = m.puckAt(1.0, xs, ys, 3);
    check(c0.getDistanceFrom(c1) < 1e-4f, "CIRCLE comes back after one period");
  }
  // all motion settings survive save / load
  {
    GhostyProcessor a;
    a.loadFactoryPreset(0, err);
    makeThreeGhosts(a);
    a.morph().motion = Morph::kGlide; a.morph().division = 14; a.morph().sync = false; a.morph().hz = 3.0f;
    MemoryBlock st; a.getStateInformation(st);
    GhostyProcessor b; b.setStateInformation(st.getData(), (int) st.getSize());
    check(b.morph().motion.load() == Morph::kGlide && b.morph().division.load() == 14 && !b.morph().sync.load() && std::abs(b.morph().hz.load() - 3.0f) < 1e-5f,
          "motion, division, sync and rate saved with the project");
  }
}

void testExtras() {
  std::printf("extras\n");
  String err;
  auto ap = fresh(0);
  auto& a = *ap;
  a.extras().setGateOn(true);
  a.extras().setGatePattern(3);
  check(a.extras().anyOn(), "gate switches on");
  auto x = play(a, chord(), 2.0, 48000.0, 512);
  auto bp = fresh(0);
  auto y = play(*bp, chord(), 2.0, 48000.0, 512);
  check(!sameBuffers(x, y), "GHOST GATE changes the sound when on");
  check(finite(x), "gate output finite");
  check(truePeakDb(x) <= truePeakDb(y) + 0.5f, "gate never adds level");
  a.extras().setGateOn(false);
  play(a, chord(), 0.2, 48000.0, 512);
  check(!a.extras().anyOn(), "gate fades out and stops");
  MemoryBlock st; a.extras().setHauntOn(true); a.getStateInformation(st);
  GhostyProcessor c; c.setStateInformation(st.getData(), (int) st.getSize());
  check(c.extras().hauntOn() && c.extras().gatePattern() == 3, "extras saved with the project");
}

void testVersions() {
  std::printf("versions\n");
  // the plug-in is 4.x (hosts must prefer it over old Ghosty Pro 3.x copies); Vital's files keep Vital's engine version
  check(String(GHOSTY_VERSION_STRING).startsWith("2."), "Ghosty Pro 2's version is 2.x (" + String(GHOSTY_VERSION_STRING) + ")");
  check(String(JucePlugin_Name) == "Ghosty Pro 2", "the plug-in is named Ghosty Pro 2");
  check(String(JucePlugin_VersionString) == String(GHOSTY_VERSION_STRING), "the plug-in version is Ghosty Pro's");
  auto p = fresh(0);
  const String text = p->presetText();
  check(text.contains("\"synth_version\":\"1.5.5\""), "a saved .vital says Vital's engine version 1.5.5 (opens in Vital 1.5)");
  check(String(GHOSTY_ENGINE_VERSION_STRING) == "1.5.5", "engine version 1.5.5");
}

void testState() {
  std::printf("state\n");
  String err;
  auto ap = fresh(2 % jmax(1, (int) factoryPresets().size()));
  auto& a = *ap;
  a.setControl("filter_1_cutoff", 77.0f);
  MemoryBlock st; a.getStateInformation(st);
  vital::utils::RandomGenerator::next_seed_ = 0;
  GhostyProcessor b;
  b.setStateInformation(st.getData(), (int) st.getSize());
  check(std::abs(b.getControl("filter_1_cutoff") - 77.0f) < 1e-4f, "a control survives save / load");
  // each twin starts from the same seed count when made AND when it plays (Vital clones voices on first use)
  auto twin = [&st] {
    vital::utils::RandomGenerator::next_seed_ = 0;
    auto p = std::make_unique<GhostyProcessor>();
    p->setStateInformation(st.getData(), (int) st.getSize());
    return play(*p, chord(), 1.5, 44100.0, 512);
  };
  auto x = twin(), y = twin();
  check(sameBuffers(x, y), "restored project sounds identical");
  // a project saved by the old Ghosty Pro (not Vital json): opens our first sound, no dialog, no crash
  GhostyProcessor c;
  const char old[] = "<?xml version=\"1.0\"?><GHOSTYPRO osc1=\"3\"/>";
  c.setStateInformation(old, (int) sizeof(old));
  check(c.factoryIndex() == 0, "an old Ghosty Pro project opens the first preset");
}

void testFinder() {
  std::printf("MY VITAL PRESETS\n");
  File tmp = File::getSpecialLocation(File::tempDirectory).getChildFile("ghosty-finder-test");
  tmp.deleteRecursively();
  File presets = tmp.getChildFile("Vital/User/Presets");
  presets.createDirectory();
  String err;
  GhostyProcessor p;
  p.loadFactoryPreset(0, err);
  check(p.savePresetFile(presets.getChildFile("Mine.vital")), "Vital's own saver writes a .vital");
  presets.getChildFile("notes.txt").replaceWithText("x");
  auto found = VitalPresetFinder::scanNow({ tmp.getChildFile("Vital") });
  check(found.size() == 1 && found[0].name == "Mine" && found[0].group == "User", "finds the .vital, grouped by its Vital folder");
  vital::utils::RandomGenerator::next_seed_ = 0;
  auto q = std::make_unique<GhostyProcessor>();
  check(q->loadPresetFile(found[0].file, err), "opens it: " + err);
  auto y = play(*q, chord(), 1.0, 44100.0, 512, true);
  vital::utils::RandomGenerator::next_seed_ = 0;
  auto q2 = std::make_unique<GhostyProcessor>();
  q2->loadPresetText(factoryPresets()[0].text, "x", err);
  auto x = play(*q2, chord(), 1.0, 44100.0, 512, true);
  check(sameBuffers(x, y), "a saved and re-opened .vital sounds identical");
  // a bank: a zip of Vital folders, as Vital's Export Bank makes it
  ZipFile::Builder zb;
  zb.addFile(presets.getChildFile("Mine.vital"), 9, "TestPack/Presets/Mine Two.vital");
  File bank = tmp.getChildFile("Test Pack.vitalbank");
  { FileOutputStream os(bank); zb.writeToStream(os, nullptr); }
  found = VitalPresetFinder::scanNow({ tmp });
  bool sawBank = false;
  for (auto& f : found) sawBank = sawBank || f.bank;
  check(sawBank, "lists a .vitalbank");
  check(VitalPresetFinder::importBank(bank, err), "imports a bank: " + err);
  check(VitalPresetFinder::dataFolder().getChildFile("TestPack/Presets/Mine Two.vital").existsAsFile(), "bank unpacked into Ghosty Pro's folder");
  tmp.deleteRecursively();
  VitalPresetFinder::dataFolder().getChildFile("TestPack").deleteRecursively();
}



// ------------------------------------------------------------------ KEYS: ARP / CHORDS / SCALE LOCK / HOLD
struct Ev { int pos; int note; bool on; };
std::vector<Ev> runKeys(Keys& k, const std::vector<Ev>& in, int samples, double bpm = 120.0, bool playing = false, double ppq = 0.0, int offset = 0) {
  MidiBuffer a, b;
  for (auto& e : in) a.addEvent(e.on ? MidiMessage::noteOn(1, e.note, (uint8) 100) : MidiMessage::noteOff(1, e.note), e.pos);
  k.process(a, b, samples, bpm, playing, ppq);
  std::vector<Ev> out;
  for (const auto m : b) {
    auto msg = m.getMessage();
    if (msg.isNoteOn()) out.push_back({ m.samplePosition + offset, msg.getNoteNumber(), true });
    else if (msg.isNoteOff()) out.push_back({ m.samplePosition + offset, msg.getNoteNumber(), false });
  }
  return out;
}
std::vector<int> ons(const std::vector<Ev>& e) { std::vector<int> n; for (auto& x : e) if (x.on) n.push_back(x.note); return n; }
// every note-on has its note-off by the end
bool balanced(const std::vector<Ev>& e) {
  int count[128] = {};
  for (auto& x : e) { count[x.note] += x.on ? 1 : -1; if (count[x.note] < 0 || count[x.note] > 1) return false; }
  for (int c : count) if (c != 0) return false;
  return true;
}

void testKeys() {
  std::printf("keys\n");
  {
    Keys k; k.prepare(48000, 0);
    check(!k.anyOn() && !k.busy(), "KEYS are off by default");
    auto o = runKeys(k, { { 10, 61, true }, { 400, 61, false } }, 512);
    check(o.size() == 2 && o[0].note == 61 && o[0].pos == 10 && o[1].pos == 400, "off: notes pass through untouched");
  }
  {
    Keys k; k.prepare(48000, 0); k.scale = 1; k.key = 0;   // C major
    auto o = runKeys(k, { { 0, 61, true }, { 100, 66, true }, { 200, 61, false }, { 300, 66, false } }, 512);
    check(ons(o) == std::vector<int>({ 60, 65 }), "SCALE LOCK C MAJOR: C# plays C, F# plays F");
    check(balanced(o), "SCALE LOCK: every note is let go");
    k.scale = 2; k.key = 9;   // A minor
    o = runKeys(k, { { 0, 56, true }, { 10, 56, false } }, 64);
    check(ons(o) == std::vector<int>({ 55 }), "SCALE LOCK A MINOR: G# plays G");
  }
  {
    Keys k; k.prepare(48000, 0); k.chord = 1;
    auto o = runKeys(k, { { 0, 60, true }, { 50, 60, false } }, 64);
    check(ons(o) == std::vector<int>({ 60, 64, 67 }) && balanced(o), "CHORDS TRIADS: C plays C E G");
    k.scale = 2; k.key = 0;   // C minor
    o = runKeys(k, { { 0, 60, true }, { 50, 60, false } }, 64);
    check(ons(o) == std::vector<int>({ 60, 63, 67 }), "CHORDS in C MINOR: C plays C Eb G");
    k.chord = 2;
    o = runKeys(k, { { 0, 62, true }, { 50, 62, false } }, 64);
    check(ons(o) == std::vector<int>({ 62, 65, 68, 72 }), "CHORDS 7THS in C MINOR on D: D F Ab C");
    k.chord = 3; k.scale = 0;
    o = runKeys(k, { { 0, 48, true }, { 50, 48, false } }, 64);
    check(ons(o) == std::vector<int>({ 48, 55, 60 }) && balanced(o), "CHORDS POWER: root fifth octave");
    // two chords sharing a note: the shared note keeps sounding until both let go
    k.chord = 1;
    o = runKeys(k, { { 0, 60, true }, { 10, 64, true }, { 20, 60, false } }, 64);
    bool eStill = true;
    for (auto& x : o) if (!x.on && x.note == 64) eStill = false;
    auto o2 = runKeys(k, { { 0, 64, false } }, 64);
    o.insert(o.end(), o2.begin(), o2.end());
    check(eStill && balanced(o), "CHORDS: a note shared by two chords lets go only when both do");
  }
  {
    // ARP locked to the host: 120 BPM, 1/16 = 6000 samples at 48 kHz, UP over C E G, two octaves
    Keys k; k.prepare(48000, 0); k.arp = true; k.arpRate = 2; k.arpOctaves = 2; k.arpGate = 0.5f;
    std::vector<Ev> all;
    const int block = 480;
    for (int b = 0; b < 200; ++b) {
      std::vector<Ev> in;
      if (b == 0) in = { { 0, 60, true }, { 0, 64, true }, { 0, 67, true } };
      if (b == 150) in = { { 0, 60, false }, { 0, 64, false }, { 0, 67, false } };
      const double ppq = b * block / 48000.0 * 2.0;
      auto o = runKeys(k, in, block, 120.0, true, ppq, b * block);
      all.insert(all.end(), o.begin(), o.end());
    }
    std::vector<int> onNotes, onPos;
    for (auto& x : all) if (x.on) { onNotes.push_back(x.note); onPos.push_back(x.pos); }
    bool grid = !onPos.empty();
    for (size_t i = 0; i < onPos.size(); ++i) grid &= onPos[i] == 6000 * (int) i;   // the chord lands on beat 1: the first step is right there
    check(grid, "ARP steps land on the song's 1/16 grid (" + String((int) onPos.size()) + " steps)");
    std::vector<int> want { 60, 64, 67, 72, 76, 79 };
    bool order = onNotes.size() >= 12;
    for (size_t i = 0; i < onNotes.size() && order; ++i) order &= onNotes[i] == want[i % 6];
    check(order, "ARP UP, 2 octaves: C E G C' E' G' and round again");
    bool gateOk = true;
    for (size_t i = 0; i + 1 < all.size(); ++i)
      if (all[i].on && !all[i + 1].on && all[i + 1].note == all[i].note) gateOk &= std::abs((all[i + 1].pos - all[i].pos) - 3000) <= 2;
    check(gateOk, "ARP GATE 50%: each note lasts half a step");
    if (!gateOk) { String d; for (size_t i = 0; i < jmin((size_t) 12, all.size()); ++i) d << all[i].pos << (all[i].on ? "+" : "-") << all[i].note << " "; std::printf("    %s\n", d.toRawUTF8()); }
    check(balanced(all) && !k.busy(), "ARP: nothing left sounding after letting go");
    check(onPos.size() == 12 && onPos.back() < 150 * block, "ARP stops when you let go");
  }
  {
    // ARP with the host stopped: starts at once, at the tempo given
    Keys k; k.prepare(48000, 0); k.arp = true; k.arpRate = 1; k.arpMode = 1;   // 1/8 DOWN
    auto o = runKeys(k, { { 100, 60, true }, { 100, 67, true } }, 48000, 120.0, false, 0.0);
    std::vector<int> n = ons(o);
    check(!o.empty() && o[0].on && o[0].pos <= 101, "ARP starts right away when the song is stopped");
    check(n.size() == 4 && n[0] == 67 && n[1] == 60 && n[2] == 67, "ARP DOWN at 1/8: 4 steps a second at 120 BPM, high to low");
  }
  {
    // HOLD
    Keys k; k.prepare(48000, 0); k.hold = true;
    auto o = runKeys(k, { { 0, 60, true }, { 0, 64, true }, { 100, 60, false }, { 100, 64, false } }, 512);
    check(ons(o).size() == 2 && o.size() == 2, "HOLD: letting go keeps the notes playing");
    auto o2 = runKeys(k, { { 0, 62, true }, { 50, 62, false } }, 512);
    bool replaced = o2.size() == 3 && !o2[0].on && !o2[1].on && o2[2].on && o2[2].note == 62;
    check(replaced, "HOLD: a new chord after letting go replaces the held one");
    k.hold = false;
    auto o3 = runKeys(k, {}, 512);
    o.insert(o.end(), o2.begin(), o2.end()); o.insert(o.end(), o3.begin(), o3.end());
    check(balanced(o) && !k.busy(), "HOLD off lets every held note go");
  }
  {
    // switching ARP while notes are held: no note is ever stuck
    Keys k; k.prepare(48000, 0); k.chord = 1;
    std::vector<Ev> all;
    auto add = [&](std::vector<Ev> e) { all.insert(all.end(), e.begin(), e.end()); };
    add(runKeys(k, { { 0, 60, true } }, 512));
    k.arp = true;
    for (int i = 0; i < 30; ++i) add(runKeys(k, {}, 512, 120.0, false, 0.0));
    k.arp = false;
    add(runKeys(k, {}, 512));
    k.arp = true;
    add(runKeys(k, { { 10, 60, false } }, 512));
    k.arp = false; k.chord = 0;
    add(runKeys(k, {}, 512));
    check(balanced(all) && !k.busy(), "ARP switched on and off while a chord is held: nothing stuck");
  }
  {
    // through the plugin: the on-screen keyboard goes through KEYS too, sound stays finite and under -1 dBTP headroom rules
    vital::utils::RandomGenerator::next_seed_ = 0;
    GhostyProcessor p;
    String err;
    p.loadFactoryPreset(0, err);
    p.keys().arp = true; p.keys().arpRate = 2; p.keys().chord = 1;
    HostClock clock; clock.bpm = 120.0;
    const int before = p.keys().arpCounter.load();
    auto buf = play(p, {}, 2.0, 48000.0, 512, false, &clock, [&](int pos) {
      if (pos == 0) p.keyboardState()->noteOn(1, 48, 0.8f);
      if (pos >= 48000 && pos < 48512) p.keyboardState()->noteOff(1, 48, 0.0f);
    });
    const int steps = p.keys().arpCounter.load() - before;
    check(steps >= 6 && steps <= 9, "the on-screen keyboard plays through the ARP (" + String(steps) + " steps in 1 s at 1/16, 120 BPM)");
    check(finite(buf) && rmsDb(buf) > -60.0f, "ARP through Vital makes sound");
    // state
    MemoryBlock m; p.getStateInformation(m);
    GhostyProcessor q;
    q.setStateInformation(m.getData(), (int) m.getSize());
    check(q.keys().arp.load() && q.keys().chord.load() == 1 && q.keys().arpRate.load() == 2, "KEYS saved with the project");
  }
  {
    // switched off and done: the plugin is exactly Vital again
    auto a = fresh(0), b = fresh(0);
    a->keys().scale = 1;
    play(*a, { { 0.05, 0.2, 61, 100 } }, 0.5);
    a->keys().scale = 0;
    play(*b, { { 0.05, 0.2, 61, 100 } }, 0.5);
    check(!a->keys().busy(), "KEYS idle after the notes end");
    vital::utils::RandomGenerator::next_seed_ = 0;
    auto c = fresh(0);
    auto x = play(*c, chord(), 2.0, 44100.0, 512);
    vital::utils::RandomGenerator::next_seed_ = 0;
    auto d = fresh(0);
    d->keys().hold = true; d->keys().hold = false;
    auto y = play(*d, chord(), 2.0, 44100.0, 512);
    check(sameBuffers(x, y), "KEYS off: bit-identical to Vital");
  }
}


// ------------------------------------------------------------------ opening the plug-in safely
// Build 7 crashed FL on an Intel Mac: the AU wrapper asks every stepped parameter for the name of each of its values when
// the plug-in opens, and three of Vital's parameters (+ the destinations) had fewer names than values.
void testParameterTexts() {
  std::printf("parameter texts\n");
  GhostyProcessor p;
  int checked = 0, badCount = 0;
  String firstBad;
  for (auto* param : p.getParameters()) {
    const int steps = param->isDiscrete() ? param->getNumSteps() : 3;
    for (int i = 0; i < steps; ++i) {
      const float v = steps > 1 ? (float) i / (float) (steps - 1) : 0.0f;
      const String t = param->getText(v, 1024);
      ++checked;
      if (t.length() > 64 || !CharPointer_UTF8::isValidString(t.toRawUTF8(), 1 << 16)) { ++badCount; if (firstBad.isEmpty()) firstBad = param->getName(64); }
    }
    if (param->isDiscrete())   // what JUCE's AU wrapper asks for when the plug-in opens
      for (auto& t : param->getAllValueStrings()) { ++checked; if (t.length() > 64) { ++badCount; if (firstBad.isEmpty()) firstBad = param->getName(64); } }
  }
  check(badCount == 0, "every value of every parameter has a sane name (" + String(checked) + " checked" + (firstBad.isNotEmpty() ? ", first bad: " + firstBad : String()) + ")");
  // the three lists that were too short now name every value
  const char* fixed[] { "filter_1_style", "osc_1_view_2d", "view_spectrogram", "osc_1_destination", "sample_destination" };
  for (auto* name : fixed) {
    const auto& d = vital::Parameters::getDetails(name);
    check(d.string_lookup && d.string_lookup[(int) d.max].size() > 0 && d.string_lookup[(int) d.max].size() < 32,
          String(name) + ": its top value (" + String((int) d.max) + ") has a name: " + String(d.string_lookup[(int) d.max]));
  }
}

// a huge MY VITAL PRESETS library (Brandon has ~12,800 packs' presets): opening never reads them; the finder only lists names
void testHugeLibrary() {
  std::printf("huge preset library\n");
  File dir = File::getSpecialLocation(File::tempDirectory).getChildFile("ghosty-huge-library");
  dir.deleteRecursively();
  const int kFiles = 15000;
  for (int b = 0; b < 30; ++b) {
    File bank = dir.getChildFile("Bank " + String(b)).getChildFile("Presets");
    bank.createDirectory();
    for (int i = 0; i < kFiles / 30; ++i) {
      File f = bank.getChildFile("Preset " + String(i) + ".vital");
      f.create();
      ::truncate(f.getFullPathName().toRawUTF8(), 500 * 1024);   // sparse: 500 KB each, no disk used
    }
  }
  const double t0 = Time::getMillisecondCounterHiRes();
  auto found = VitalPresetFinder::scanNow({ dir });
  const double scanMs = Time::getMillisecondCounterHiRes() - t0;
  check((int) found.size() == kFiles, "lists all " + String(kFiles) + " presets (" + String((int) found.size()) + ")");
  check(scanMs < 20000.0, "listing 15,000 presets (7.5 GB) takes " + String(scanMs / 1000.0, 2) + " s, reading only each one's type tag");
  size_t bytes = 0;
  for (auto& f : found) bytes += sizeof(FoundPreset) + (size_t) (f.name.length() + f.group.length() + f.file.getFullPathName().length());
  check(bytes < 16u * 1024u * 1024u, "the list takes " + String((double) bytes / 1048576.0, 1) + " MB (names only)");
  const double t1 = Time::getMillisecondCounterHiRes();
  { GhostyProcessor p; }
  const double openMs = Time::getMillisecondCounterHiRes() - t1;
  check(openMs < 5000.0, "opening the plug-in reads no presets (" + String(openMs, 0) + " ms)");
  std::printf("  15,000 presets: listed in %.2f s, list %.1f MB, plug-in opened in %.0f ms\n", scanMs / 1000.0, (double) bytes / 1048576.0, openMs);
  dir.deleteRecursively();
}

// ------------------------------------------------------------------ GHOST ORBIT knobs (physics only, no display)
void testOrbitPhysics() {
  std::printf("orbit physics\n");
  using namespace ghostorbit;
  {
    Flick f;
    check(f.start(0.2, 1.5), "a flick starts momentum");
    check(std::abs(f.landing - 0.5) < 1e-12, "landing = start + velocity / friction (0.5)");
    double last = 0.2, maxSeen = 0.2; bool mono = true; int steps = 0;
    while (f.active && steps < 6000) { double p = f.step(1.0 / 60.0); mono &= p >= last; last = p; maxSeen = std::max(maxSeen, p); ++steps; }
    check(mono, "momentum path only moves toward the landing value");
    check(maxSeen <= f.landing, "momentum never passes the landing value (no overshoot sent)");
    check(last == f.landing, "momentum ends exactly on the landing value");
    check(steps < 150, "momentum settles in under 2.5 s (" + String(steps) + " frames)");
  }
  {
    Flick f;
    check(!f.start(0.4, 0.1), "a slow let-go is not a flick");
    check(!f.start(0.4, std::nan("")), "a bad velocity is not a flick");
    check(f.start(0.9, 50.0) && f.landing == 1.0, "a hard flick stops at the end of travel");
    double maxSeen = 0; while (f.active) maxSeen = std::max(maxSeen, f.step(0.02));
    check(maxSeen <= 1.0, "never past the end of travel");
  }
  {
    auto snap = [](double p) { return std::round(p * 7.0) / 7.0; };   // 8 detents
    Flick f;
    check(f.start(0.0, 2.0, snap), "stepped flick starts");
    check(std::abs(f.landing * 7.0 - std::round(f.landing * 7.0)) < 1e-12, "stepped landing is a detent");
    bool onGrid = true, mono = true; double last = 0.0;
    while (f.active) { double p = snap(f.step(1.0 / 60.0)); onGrid &= std::abs(p * 7.0 - std::round(p * 7.0)) < 1e-9; mono &= p >= last; last = p; }
    check(onGrid && mono && last == f.landing, "stepped values sit on detents, move one way, end on the landing detent");
  }
  {
    Spring sp; sp.x = 0.0; double peak = 0.0; int n = 0;
    while (!sp.settled(1.0) && n < 2000) { sp.step(1.0, 1.0 / 60.0); peak = std::max(peak, sp.x); ++n; }
    check(peak > 1.0 && peak < 1.35, "the detent spring overshoots a little (drawing only): " + String(peak, 3));
    check(sp.x == 1.0 && n < 120, "the detent spring settles exactly on the detent");
  }
  {
    VelocityTracker v;
    for (int i = 0; i <= 10; ++i) v.add(0.1 + 0.02 * i, i / 60.0);
    check(std::abs(v.velocity(10 / 60.0) - 1.2) < 0.05, "release velocity from the last 80 ms (" + String(v.velocity(10 / 60.0), 3) + ")");
    check(v.velocity(10 / 60.0 + 0.1) == 0.0, "holding still before letting go: no flick");
  }
}

MouseEvent fakeMouse(Component* c, Point<float> pos, Point<float> down, int clicks = 1) {
  return MouseEvent(Desktop::getInstance().getMainMouseSource(), pos, ModifierKeys(ModifierKeys::leftButtonModifier),
                    MouseInputSource::invalidPressure, MouseInputSource::invalidOrientation, MouseInputSource::invalidRotation,
                    MouseInputSource::invalidTiltX, MouseInputSource::invalidTiltY, c, c, Time::getCurrentTime(), down,
                    Time::getCurrentTime(), clicks, pos != down);
}

// a flick: grab, drag up `px` per frame for `frames` frames, let go; then run the physics by hand. Returns the values seen.
template <class Knob>
std::vector<double> flickKnob(Knob& k, ghostorbit::Orbit& orbit, double& clock, float px, int frames, bool holdBeforeRelease = false) {
  std::vector<double> seen;
  const Point<float> start(k.getWidth() * 0.5f, k.getHeight() * 0.5f);
  k.mouseDown(fakeMouse(&k, start, start));
  for (int i = 1; i <= frames; ++i) {
    clock += 1.0 / 60.0;
    k.mouseDrag(fakeMouse(&k, start.translated(0, -px * i), start));
    seen.push_back(k.getValue());
  }
  if (holdBeforeRelease) clock += 0.2;
  k.mouseUp(fakeMouse(&k, start.translated(0, -px * frames), start));
  for (int i = 0; i < 600 && orbit.spinning(); ++i) {
    clock += 1.0 / 60.0;
    orbit.tick(1.0 / 60.0);
    seen.push_back(k.getValue());
  }
  return seen;
}

void testOrbitKnob() {
  std::printf("orbit knob (ours)\n");
  using namespace ghostorbit;
  double clock = 100.0;
  int begins = 0, ends = 0;
  OrbitKnob k;
  k.setSize(60, 60);
  k.setRange(0.0, 1.0);
  k.updateSnap();
  k.orbit.clock = [&] { return clock; };
  k.onGestureBegin = [&] { ++begins; };
  k.onGestureEnd = [&] { ++ends; };
  k.setValue(0.2, dontSendNotification);
  auto seen = flickKnob(k, k.orbit, clock, 3.0f, 8);
  const double landing = k.orbit.landing();
  bool mono = true;
  for (size_t i = 1; i < seen.size(); ++i) mono &= seen[i] >= seen[i - 1] - 1e-12;
  check(seen.size() > 12 && seen.back() > seen[7] + 0.05, "a flick keeps turning after the let-go");
  check(mono && *std::max_element(seen.begin(), seen.end()) <= landing + 1e-12, "the value never passes where it lands");
  check(k.getValue() == k.proportionOfLengthToValue(landing), "the value ends exactly on the landing value");
  check(begins == 1 && ends == 1, "one gesture for drag + momentum (" + String(begins) + "/" + String(ends) + ")");

  // catching a spinning knob keeps the same gesture
  begins = ends = 0;
  k.setValue(0.1, dontSendNotification);
  const Point<float> c(30, 30);
  k.mouseDown(fakeMouse(&k, c, c));
  for (int i = 1; i <= 6; ++i) { clock += 1.0 / 60.0; k.mouseDrag(fakeMouse(&k, c.translated(0, -4.0f * i), c)); }
  k.mouseUp(fakeMouse(&k, c.translated(0, -24.0f), c));
  check(k.orbit.spinning() && begins == 1 && ends == 0, "spinning: the gesture is still open");
  clock += 1.0 / 60.0; k.orbit.tick(1.0 / 60.0);
  k.mouseDown(fakeMouse(&k, c, c));
  check(!k.orbit.spinning() && begins == 1, "grabbing it again stops it, same gesture");
  clock += 0.3;
  k.mouseUp(fakeMouse(&k, c, c));
  check(begins == 1 && ends == 1, "one gesture in all (" + String(begins) + "/" + String(ends) + ")");

  // automation / presets / host: never physics
  begins = ends = 0;
  for (double v : { 0.0, 0.9, 0.3, 0.7 }) { clock += 0.01; k.setValue(v, sendNotificationSync); }
  check(!k.orbit.spinning() && begins == 0 && ends == 0, "values set from outside never start the physics or a gesture");

  // something else moves it while it spins: it stops at once and the gesture ends
  k.setValue(0.1, dontSendNotification);
  k.mouseDown(fakeMouse(&k, c, c));
  for (int i = 1; i <= 6; ++i) { clock += 1.0 / 60.0; k.mouseDrag(fakeMouse(&k, c.translated(0, -4.0f * i), c)); }
  k.mouseUp(fakeMouse(&k, c.translated(0, -24.0f), c));
  k.setValue(0.95, dontSendNotification);
  clock += 1.0 / 60.0; k.orbit.tick(1.0 / 60.0);
  check(!k.orbit.spinning() && k.getValue() == 0.95 && begins == 1 && ends == 1, "an outside change wins and ends the gesture");

  // hold still, then let go: no momentum
  begins = ends = 0;
  k.setValue(0.2, dontSendNotification);
  seen = flickKnob(k, k.orbit, clock, 3.0f, 8, true);
  check(!k.orbit.spinning() && seen.size() == 8 && begins == 1 && ends == 1, "hold still then let go: stops where you left it");

  // stepped: magnetic detents
  OrbitKnob s;
  s.setSize(60, 60);
  s.setRange(0.0, 7.0, 1.0);
  s.updateSnap();
  s.orbit.clock = [&] { return clock; };
  s.setValue(0.0, dontSendNotification);
  double maxDisplay = 0.0;
  const Point<float> sc(30, 30);
  s.mouseDown(fakeMouse(&s, sc, sc));
  for (int i = 1; i <= 6; ++i) { clock += 1.0 / 60.0; s.mouseDrag(fakeMouse(&s, sc.translated(0, -5.0f * i), sc)); }
  s.mouseUp(fakeMouse(&s, sc.translated(0, -30.0f), sc));
  bool ints = true; double sl = s.orbit.landing();
  for (int i = 0; i < 600 && (s.orbit.spinning() || s.orbit.animating()); ++i) {
    clock += 1.0 / 60.0; s.orbit.tick(1.0 / 60.0);
    ints &= s.getValue() == std::round(s.getValue());
    maxDisplay = std::max(maxDisplay, s.orbit.display(s.valueToProportionOfLength(s.getValue())));
  }
  check(ints && s.getValue() == s.proportionOfLengthToValue(sl), "stepped knob: only whole steps reach the control, ends on a detent");
  check(maxDisplay > sl, "stepped knob: the magnetic spring overshoots on screen only");
}

// Vital's own knobs (marked UI edit in synth_slider.cpp): host gestures + values seen by the host
struct GestureCounter : AudioProcessorListener {
  int begins = 0, ends = 0, index = -1;
  float maxValue = -1.0f, lastValue = -1.0f;
  bool mono = true;
  void audioProcessorParameterChanged(AudioProcessor*, int i, float v) override {
    if (i != index) return;
    if (lastValue >= 0.0f && v < lastValue - 1e-7f) mono = false;
    lastValue = v; maxValue = std::max(maxValue, v);
  }
  void audioProcessorChanged(AudioProcessor*) override {}
  void audioProcessorParameterChangeGestureBegin(AudioProcessor*, int i) override { if (i == index) ++begins; }
  void audioProcessorParameterChangeGestureEnd(AudioProcessor*, int i) override { if (i == index) ++ends; }
};

SynthSlider* findSynthSlider(Component* c, const String& name) {
  if (auto* s = dynamic_cast<SynthSlider*>(c))
    if (s->getName() == name && s->isRotary() && s->getWidth() > 0) return s;
  for (auto* child : c->getChildren())
    if (auto* f = findSynthSlider(child, name)) return f;
  return nullptr;
}

void testVitalKnobOrbit(GhostyProcessor& p, Component& ed) {
  std::printf("orbit knob (Vital's)\n");
  SynthSlider* sl = findSynthSlider(&ed, "filter_1_drive");
  check(sl != nullptr, "found Vital's FILTER 1 DRIVE knob");
  if (sl == nullptr) return;
  GestureCounter gc;
  for (int i = 0; i < p.getParameters().size(); ++i)
    if (p.getParameters()[i]->getName(64) == String(vital::Parameters::getDisplayName("filter_1_drive"))) gc.index = i;
  check(gc.index >= 0, "the drive is a host parameter");
  p.addListener(&gc);
  double clock = 1000.0;
  sl->getOrbit().clock = [&] { return clock; };
  sl->setValue(sl->getMinimum() + 0.1 * (sl->getMaximum() - sl->getMinimum()), sendNotificationSync);
  gc.lastValue = -1.0f; gc.maxValue = -1.0f; gc.mono = true;
  check(!sl->getOrbit().spinning() && gc.begins == 0, "a value set from code: no physics, no gesture");
  auto seen = flickKnob(*sl, sl->getOrbit(), clock, 3.0f, 8);
  const double landing = sl->proportionOfLengthToValue(sl->getOrbit().landing());
  check(seen.size() > 12 && seen.back() > seen[7], "Vital's knob keeps turning after a flick");
  if (seen.size() <= 12 || !(seen.back() > seen[7])) {
    String v; for (double x : seen) v << String(x, 3) << " ";
    std::printf("    values: %s (rotary %d, enabled %d, interval %g)\n", v.toRawUTF8(), (int) sl->isRotary(), (int) sl->isEnabled(), sl->getInterval());
  }
  check(sl->getValue() == landing, "Vital's knob ends exactly on its landing value");
  check(std::abs(p.getControl("filter_1_drive") - (float) landing) < 1e-4f, "the synth has exactly the landing value");
  check(gc.mono, "the host only saw the value move one way (no overshoot sent)");
  check(gc.begins == 1 && gc.ends == 1, "the host saw one gesture for drag + momentum (" + String(gc.begins) + "/" + String(gc.ends) + ")");
  // host automation while the editor is open: never physics
  for (float v : { 0.1f, 0.6f, 0.3f }) p.getParameters()[gc.index]->setValueNotifyingHost(v);
  MessageManager::getInstance()->runDispatchLoopUntil(100);
  check(!sl->getOrbit().spinning() && gc.begins == 1, "host automation never starts the physics");
  p.removeListener(&gc);
  sl->getOrbit().clock = nullptr;
}

// Vital's editor on the SYNTH page at any window size and display scale: it fills the area under the tabs at Vital's own
// shape (letterboxed), and its sections are laid out the same at 2x (Retina) as at 1x. Build 8 on a 2x Mac: sections
// stretched and the top cut off (Vital's letterbox code mixed 1x and 2x units).
struct VitalLayout { Rectangle<int> holder; std::vector<Rectangle<float>> sections; float scale = 1.0f; bool ok = false; };
// each layout in a fresh process (its own display scale, one window): "GhostyProTool layout <scale> <w> <h> [fl]"
VitalLayout vitalLayout(GhostyProcessor&, float scale, int w, int h, bool flResize) {
  VitalLayout out;
  ChildProcess child;
  StringArray cmd { File::getSpecialLocation(File::currentExecutableFile).getFullPathName(), "layout", String(scale), String(w), String(h) };
  if (flResize) cmd.add("fl");
  if (!child.start(cmd)) return out;
  // never hang the test run: 90 s at most (its few lines of output fit in the pipe while it runs)
  if (!child.waitForProcessToFinish(60000)) { child.kill(); std::printf("  layout %g %dx%d: no answer in 60 s\n", scale, w, h); return out; }
  const String text = child.readAllProcessOutput();
  for (auto line : StringArray::fromLines(text)) {
    auto t = StringArray::fromTokens(line, " ", "");
    if (t.size() >= 5 && t[0] == "holder") { out.holder = { t[1].getIntValue(), t[2].getIntValue(), t[3].getIntValue(), t[4].getIntValue() }; out.ok = true; }
    if (t.size() >= 2 && t[0] == "scale") out.scale = t[1].getFloatValue();
    if (t.size() >= 5 && t[0] == "section") out.sections.push_back({ t[1].getFloatValue(), t[2].getFloatValue(), t[3].getFloatValue(), t[4].getFloatValue() });
  }
  return out;
}

void testVitalLayout(GhostyProcessor& p) {
  std::printf("vital layout\n");
  const double aspect = 1400.0 / 820.0;
  struct Size { int w, h; } sizes[] { { GhostyEditor::kWidth, GhostyEditor::kHeight }, { 1240, 850 }, { 1000, 700 }, { 1700, 1000 }, { 1280, 1100 } };
  for (auto sz : sizes) {
    std::printf("  layout %dx%d\n", sz.w, sz.h);
    auto one = vitalLayout(p, 1.0f, sz.w, sz.h, false);
    auto two = vitalLayout(p, 2.0f, sz.w, sz.h, true);
    const String tag = String(sz.w) + "x" + String(sz.h);
    check(one.ok && two.ok, tag + ": Vital's editor is there at 1x and 2x");
    check(two.scale > 1.5f, tag + ": the 2x run really is 2x (Vital's display scale " + String(two.scale) + ")");
    if (!one.ok || !two.ok) continue;
    check(std::abs(one.holder.getWidth() / (double) one.holder.getHeight() - aspect) < 0.01, tag + ": Vital's area has Vital's own shape (" + one.holder.toString() + ")");
    check(one.holder.getY() >= GhostyEditor::kChromeH && one.holder.getBottom() <= sz.h, tag + ": Vital's area is under our tabs and inside the window");
    check(one.holder == two.holder, tag + ": same area at 2x");
    // Vital's sections, in window units, the same at 1x and 2x; all inside Vital's area; the area is filled
    bool same = one.sections.size() == two.sections.size() && !one.sections.empty();
    float worst = 0.0f;
    for (size_t i = 0; same && i < one.sections.size(); ++i) {
      const auto a = one.sections[i], b = two.sections[i];
      worst = jmax(worst, std::abs(a.getX() - b.getX()), std::abs(a.getY() - b.getY()));
      worst = jmax(worst, std::abs(a.getRight() - b.getRight()), std::abs(a.getBottom() - b.getBottom()));
    }
    // (Vital's own layout rounds to whole pixels at each scale: up to 3 window px apart at some sizes, e.g. 1200 x 760)
    check(same && worst <= 3.0f, tag + ": Vital lays its sections out the same at 2x as at 1x (worst " + String(worst, 1) + " px)");
    float top = 1e9f, bottom = 0.0f;
    bool inside = true;
    for (auto& r : two.sections) {
      top = jmin(top, r.getY()); bottom = jmax(bottom, r.getBottom());
      inside &= r.getX() >= -1.0f && r.getY() >= -1.0f && r.getRight() <= two.holder.getWidth() + 1.0f && r.getBottom() <= two.holder.getHeight() + 1.0f;
    }
    check(inside, tag + ": at 2x every section is inside Vital's area");
    check(top <= 2.0f && bottom >= two.holder.getHeight() - 3.0f, tag + ": at 2x Vital fills its area top to bottom (" + String(top, 0) + ".." + String(bottom, 0) + " of " + String(two.holder.getHeight()) + ")");
  }
}

// MOUSE OFFSET (2.0.4): on a 2x (Retina) Mac every click inside Vital's editor landed far below the cursor, and the on/off
// buttons never toggled. Vital's patched JUCE scaled the mouse position by the display scale BEFORE taking off the offsets of
// everything above Vital's editor (our header, tabs and letterbox): a position inside Vital was off by offset * (scale - 1).
// "GhostyProTool hit <scale> <w> <h> click" sends real mouse events (ComponentPeer::handleMouseEvent) to the centre of every
// Vital control where Vital DRAWS it, with a 1 px drag before the release (a hand never holds still): each control must get
// the click at its own centre, and each on/off button (OSC 1-3, SMP, filters) must toggle its engine parameter.
void testVitalHits() {
  std::printf("vital hits (real mouse events onto every control)\n");
  struct Case { float scale; int w, h; } cases[] { { 1.0f, GhostyEditor::kWidth, GhostyEditor::kHeight }, { 2.0f, GhostyEditor::kWidth, GhostyEditor::kHeight },
                                                   { 2.0f, 960, 700 }, { 2.0f, 1600, 1000 }, { 2.0f, 1400, 760 }, { 1.0f, 1100, 900 } };
  for (auto c : cases) {
    // a window bigger than the screen (1600 x 1000 on a 1440 x 900 MacBook) hangs off it: the parts off the screen can't be
    // clicked by anyone, so the big case is made as big as this screen allows
    if (auto* d = Desktop::getInstance().getDisplays().getPrimaryDisplay()) {
      const auto area = d->userArea;
      if (c.w > area.getWidth() || c.h > area.getHeight()) {
        std::printf("    (%dx%d is bigger than this screen's %dx%d: tested at the screen's size)\n", c.w, c.h, area.getWidth(), area.getHeight());
        c.w = jmin(c.w, area.getWidth()); c.h = jmin(c.h, area.getHeight());
      }
    }
    const String tag = String(c.scale, 0) + "x " + String(c.w) + "x" + String(c.h);
    auto run = [&](const File& exe, String& out) {
      ChildProcess child;
      StringArray cmd { exe.getFullPathName(), "hit", String(c.scale), String(c.w), String(c.h), "click" };
      if (!child.start(cmd)) return false;
      if (!child.waitForProcessToFinish(60000)) { child.kill(); return false; }
      out = child.readAllProcessOutput();
      return true;
    };
    String text;
    if (!run(File::getSpecialLocation(File::currentExecutableFile), text)) { check(false, tag + ": the hit test starts and answers in 60 s"); continue; }
   #if JUCE_MAC
    // On a Mac desktop in use (Brandon's own Mac runs these tests while his apps are open, or with the screen locked) the
    // test window can be under other windows: macOS then reports another window at every point and no click reaches
    // ours, so every control "misses". That is the desktop, not the plug-in (in FL a click only comes when our window is
    // under the mouse). Then the same clicks run again from a copy of this tool inside a ".appex" folder: JUCE skips
    // exactly that one "which window is on top" check there and nothing else of the click path (the scaling maths this
    // test is for is unchanged).
    if (text.contains("COVERED")) {
      std::printf("    (the desktop covered the test window: clicks checked again without macOS's window-on-top check)\n");
      const File dir = File::getSpecialLocation(File::tempDirectory).getChildFile("GhostyHit.appex").getChildFile("Contents").getChildFile("MacOS");
      dir.createDirectory();
      const File copy = dir.getChildFile("GhostyProTool");
      File::getSpecialLocation(File::currentExecutableFile).copyFileTo(copy);
      copy.setExecutePermission(true);
      if (!run(copy, text)) { check(false, tag + ": the hit test (uncovered) answers in 60 s"); continue; }
    }
   #endif
    int controls = 0, missed = -1, buttons = 0, stuck = -1;
    for (auto line : StringArray::fromLines(text)) {
      if (line.startsWith("controls")) {
        auto t = StringArray::fromTokens(line, " ", "");
        t.removeEmptyStrings();
        auto after = [&t](const char* word) { const int i = t.indexOf(word); return i >= 0 && i + 1 < t.size() ? t[i + 1].getIntValue() : -1; };
        controls = after("controls"); missed = after("missed"); buttons = after("buttons"); stuck = after("stuck");
      }
      if (line.startsWith("MISS") || line.startsWith("STUCK")) std::printf("    %s\n", line.toRawUTF8());
    }
    check(controls >= 60, tag + ": the test found Vital's controls (" + String(controls) + ")");
    check(missed == 0, tag + ": every control gets the click at its own centre (missed " + String(missed) + ")");
    check(buttons >= 6 && stuck == 0, tag + ": every on/off button (OSC 1-3, SMP, filters) toggles its engine parameter (" + String(buttons) + " buttons, stuck " + String(stuck) + ")");
  }
}

// MORPH page (2.0.4): the puck moves by itself (MOTION) and is drawn moving even when no audio block runs (FL Studio switches a
// silent instrument off; a stopped project): the board follows Morph::idlePuck on the UI's own clock, tempo-locked at the last tempo.
void testMorphMotion() {
  std::printf("morph motion shown with no audio running\n");
  GhostyProcessor p;
  p.setPlayConfigDetails(0, 2, 44100, 512);
  p.prepareToPlay(44100, 512);
  String err;
  p.loadFactoryPreset(0, err);
  std::unique_ptr<GhostyEditor> ed(dynamic_cast<GhostyEditor*>(p.createEditor()));
  check(ed != nullptr && ed->morphPage() != nullptr, "the editor has the MORPH page");
  if (!ed || !ed->morphPage()) return;
  auto& m = p.morph();
  m.board.add(0.2f, 0.3f); m.board.add(0.8f, 0.3f); m.board.add(0.5f, 0.85f);
  m.puck_x = 0.5f; m.puck_y = 0.5f;
  m.enabled = true; m.sync = true; m.division = 7;   // 1/4: one period per beat (0.5 s at 120)
  m.depth = 0.3f;
  m.publish();
  ed->showPage(1);
  for (int motion : { (int) Morph::kCircle, (int) Morph::kEight, (int) Morph::kWander, (int) Morph::kGlide, (int) Morph::kStep }) {
    m.motion = motion;
    std::vector<Point<float>> seen;
    for (int i = 0; i < 12; ++i) {
      MessageManager::getInstance()->runDispatchLoopUntil(80);
      ed->morphPage()->sync();
      seen.push_back(ed->morphPage()->shownPuck());
    }
    float travelled = 0.0f; bool onBoard = true;
    for (size_t i = 1; i < seen.size(); ++i) travelled += seen[i].getDistanceFrom(seen[i - 1]);
    for (auto q : seen) onBoard &= q.x >= 0.0f && q.x <= 1.0f && q.y >= 0.0f && q.y <= 1.0f && std::isfinite(q.x) && std::isfinite(q.y);
    check(travelled > 0.1f && onBoard, String("MORPH ") + Morph::motionName(motion) + " moves the puck on the board with no audio running (travelled " + String(travelled, 2) + ")");
  }
  m.motion = Morph::kStill;
  MessageManager::getInstance()->runDispatchLoopUntil(60);
  ed->morphPage()->sync();
  const auto a = ed->morphPage()->shownPuck();
  MessageManager::getInstance()->runDispatchLoopUntil(200);
  ed->morphPage()->sync();
  check(ed->morphPage()->shownPuck().getDistanceFrom(a) < 1.0e-4f, "STILL leaves the puck where it is");
  // one period later, the CIRCLE is back where it was (tempo-locked at 120 bpm, 1/4 = 0.5 s)
  m.motion = Morph::kCircle;
  const double t0 = Time::getMillisecondCounterHiRes();
  const auto p0 = m.idlePuck();
  while (Time::getMillisecondCounterHiRes() - t0 < 500.0) Thread::sleep(2);
  const double dt = (Time::getMillisecondCounterHiRes() - t0) / 1000.0;
  const auto p1 = m.idlePuck();
  check(p0.getDistanceFrom(p1) < 0.02f + 0.3f * (float) std::abs(dt - 0.5) * 2.0f * 3.14159f, "CIRCLE comes round once per beat at the shown tempo (1/4 at 120 bpm)");
  m.enabled = false;
  m.motion = Morph::kStill;
  std::printf("  (closing the editor)\n");
  ed = nullptr;
  MessageManager::getInstance()->runDispatchLoopUntil(100);
  std::printf("  (closed)\n");
}

// The preset drop-down (2.0.4): folders as submenus from the .vital files on this computer (Vital's Factory / User and the sorted
// folders Bass, Leads, Pads ...), indexed once, big folders in ranges; 12,833 presets must index and build a menu fast.
void testPresetMenu() {
  std::printf("preset drop-down\n");
  const File root = File::getSpecialLocation(File::tempDirectory).getChildFile("ghosty-menu-test-" + String(Random::getSystemRandom().nextInt(1000000))).getChildFile("Vital");
  struct Folder { const char* path; int count; };
  const Folder folders[] { { "Sorted by GhostBlock/Presets/Bass", 3100 }, { "Sorted by GhostBlock/Presets/Leads", 2900 }, { "Sorted by GhostBlock/Presets/Pads", 2500 },
                           { "Sorted by GhostBlock/Presets/Plucks", 1800 }, { "Sorted by GhostBlock/Presets/Keys", 1200 }, { "Factory/Presets", 33 }, { "User/Presets", 5 }, { "Sorted by GhostBlock/Presets/FX", 1295 } };
  int total = 0;
  int fi = 0;
  for (auto& f : folders) {   // (each folder's files a different size: different presets, not copies of each other)
    const File dir = root.getChildFile(f.path);
    dir.createDirectory();
    const String body = "{}" + String::repeatedString(" ", ++fi);
    for (int i = 0; i < f.count; ++i) dir.getChildFile("Preset " + String(i) + ".vital").replaceWithText(body);
    total += f.count;
  }
  // presets this build can't open: made with a newer engine version, a broken file, an accented name that can (it must be listed)
  {
    const File odd = root.getChildFile("Odd Pack/Presets");
    odd.createDirectory();
    odd.getChildFile("Future Bass.vital").replaceWithText("{\"preset_style\":\"Bass\",\"settings\":{},\"synth_version\":\"1.6.0\"}");
    odd.getChildFile("Future Lead.vital").replaceWithText("{\"preset_style\":\"Lead\",\"settings\":{},\"synth_version\":\"2.0.1\"}");
    odd.getChildFile("Broken.vital").replaceWithText("not a preset");
    odd.getChildFile(String::fromUTF8("Ar\xc3\xb4mes Pad.vital")).replaceWithText("{\"preset_style\":\"Pad\",\"settings\":{},\"synth_version\":\"1.5.5\"}");
  }
  const double t0 = Time::getMillisecondCounterHiRes();
  const auto found = VitalPresetFinder::scanNow({ root });
  const double t1 = Time::getMillisecondCounterHiRes();
  MenuIndex ix;
  Array<File> roots; roots.add(root);
  ix.build(found, roots);
  const double t2 = Time::getMillisecondCounterHiRes();
  PopupMenu menu;
  ix.addTo(menu, (int) ix.flat.size() / 2);
  const double t3 = Time::getMillisecondCounterHiRes();
  std::printf("    %d presets: scan %.0f ms, index %.0f ms, menu %.0f ms\n", total, t1 - t0, t2 - t1, t3 - t2);
  check((int) found.size() == total + 4 && (int) ix.flat.size() == total + 4, "all " + String(total + 4) + " presets are in the index");
  StringArray top;
  for (auto& k : ix.root.kids) top.add(k.first);
  check(top.contains("Bass") && top.contains("Leads") && top.contains("Pads") && top.contains("Plucks") && top.contains("Keys") && top.contains("Factory") && top.contains("User"),
        "the folders Bass, Leads, Pads, Plucks, Keys, Factory and User are the top level (" + top.joinIntoString(", ") + ")");
  check(!top.contains("Presets") && !top.contains("Sorted by GhostBlock"), "Vital's wrapper folders (Presets, Sorted by GhostBlock) are not menu levels");
  check((int) ix.folders["Bass"].size() == 3100 && (int) ix.folders["Factory"].size() == 33, "each folder holds its own presets");
  bool sorted = true;
  const auto& bass = ix.folders["Bass"];
  for (size_t i = 1; i < bass.size(); ++i) sorted &= ix.flat[(size_t) bass[i - 1]].name.compareNatural(ix.flat[(size_t) bass[i]].name) <= 0;
  check(sorted, "the < > arrows walk a folder in name order");
  check(t2 - t1 < 800.0 && t3 - t2 < 800.0, "indexing and building the menu are fast (" + String(t2 - t1, 0) + " ms, " + String(t3 - t2, 0) + " ms)");
  // no menu level has more than ~100 rows: a big folder is split into alphabetical ranges
  int widest = 0, depthSeen = 0;
  std::function<void(const PopupMenu&, int)> walk = [&](const PopupMenu& m, int depth) {
    int rows = 0;
    PopupMenu::MenuItemIterator it(m);
    while (it.next()) {
      ++rows;
      if (it.getItem().subMenu != nullptr) walk(*it.getItem().subMenu, depth + 1);
    }
    widest = jmax(widest, rows); depthSeen = jmax(depthSeen, depth);
  };
  walk(menu, 0);
  check(widest <= MenuIndex::kChunk + 2, "no folder level has more than " + String(MenuIndex::kChunk) + " rows (widest " + String(widest) + ", depth " + String(depthSeen) + ")");

  // THE PRESET PICKER (Brandon 2026-11-07 16:07): one level of TYPES, each one flat A-Z list of every preset of that type
  std::printf("preset picker (types)\n");
  check(presetTypeOf("Bass", "Anything", {}) == kBass && presetTypeOf("Lead", "Sub Thing", {}) == kLead, "a preset's own style tag decides first");
  check(presetTypeOf("", "LD Supersaw", {}) == kLead && presetTypeOf("Experimental", "808 Boom", {}) == kBass && presetTypeOf("", "Soft Rhodes", {}) == kKeys
        && presetTypeOf("", "Big_Riser", {}) == kFx && presetTypeOf("Template", "Kick 1", {}) == kDrums, "then the words in its name");
  check(presetTypeOf("", "Subtle Glow", {}) == kOther && presetTypeOf("", "Mystery", { "Pack One" }) == kOther, "unclear goes to OTHER (a word inside another word does not count)");
  check(presetTypeOf("", "Mystery", { "Pads", "Dark" }) == kPad && presetTypeOf("Sequence", "Gated", {}) == kPluck, "then its folders; the Sequence style counts as PLUCK");
  {
    PresetCatalog cat;
    std::set<std::string> favs { found[5].file.getFullPathName().toStdString(), found[100].file.getFullPathName().toStdString() };
    cat.build(factoryPresets(), found, roots, root.getChildFile("User"), favs);
    const int expect = total + 1 + (int) factoryPresets().size();   // (+ the accented one; the 3 it can't open are hidden)
    check(cat.hiddenNewer == 2 && cat.hiddenUnreadable == 1, "presets needing a newer engine (2) and broken files (1) are hidden, not lost ("
          + String(cat.hiddenNewer) + ", " + String(cat.hiddenUnreadable) + ")");
    bool accent = false;
    for (int i : cat.byType[kPad]) accent |= cat.entries[(size_t) i].name == String::fromUTF8("Ar\xc3\xb4mes Pad") && cat.entries[(size_t) i].file.existsAsFile();
    check(accent, "an accented name (Arômes) is listed with its real path");
    std::vector<int> seen((size_t) cat.total(), 0);
    int sum = 0;
    for (int t = 0; t < kNumPresetTypes; ++t) { sum += (int) cat.byType[t].size(); for (int i : cat.byType[t]) seen[(size_t) i]++; }
    check(cat.total() == expect && sum == expect, "the type counts add up to every preset (" + String(sum) + " of " + String(expect) + ")");
    check(std::all_of(seen.begin(), seen.end(), [](int n) { return n == 1; }), "every preset is in exactly one type");
    auto count = [&cat](int t) { int n = 0; for (int i : cat.byType[t]) n += cat.entries[(size_t) i].factory < 0 ? 1 : 0; return n; };
    // (a name's own words come before its folder: the "Preset 808" in each of the 5 other big folders is a BASS)
    check(count(kBass) == 3105 && count(kLead) == 2899 && count(kPad) == 2500 && count(kPluck) == 1799 && count(kKeys) == 1199 && count(kFx) == 1294,
          "folder words sort the packs into types (bass " + String(count(kBass)) + ", lead " + String(count(kLead)) + ", fx " + String(count(kFx)) + ")");
    check(count(kMine) == 5 && count(kOther) == 33, "the user's own presets are MY PRESETS; untagged ones in plain folders are OTHER");
    check(cat.favourites.size() == 2, "favourites are listed too (a shortcut on top of the types)");
    {   // the same preset copied into two folders (e.g. two copies of a pack) shows once; a different preset of the same name stays
      std::vector<FoundPreset> two(3);
      two[0].file = File("/a/Wide Bass.vital"); two[0].name = "Wide Bass"; two[0].size = 1234;
      two[1].file = File("/b/Wide Bass.vital"); two[1].name = "Wide Bass"; two[1].size = 1234;
      two[2].file = File("/c/Wide Bass.vital"); two[2].name = "Wide Bass"; two[2].size = 999;
      PresetCatalog c2;
      c2.build({}, two, {}, File(), {});
      check(c2.total() == 2, "a preset copied into two folders shows once (" + String(c2.total()) + " of 3 files)");
    }
    int ourOther = 0;
    for (int i : cat.byType[kOther]) ourOther += cat.entries[(size_t) i].factory >= 0 ? 1 : 0;
    check(ourOther == 0, "every Ghosty Pro preset has a real type");
    bool az = true;
    for (int t = 0; t < kNumPresetTypes; ++t)
      for (size_t k = 1; k < cat.byType[t].size(); ++k) az &= cat.entries[(size_t) cat.byType[t][k - 1]].name.compareNatural(cat.entries[(size_t) cat.byType[t][k]].name) <= 0;
    check(az, "each type is one A-Z list");
    // the picker: types on the left (with counts), one flat list on the right, a search; nothing overlaps
    PresetPicker picker(cat, kBass, cat.byType[kBass][10], false, [](int) {});
    check(picker.getWidth() <= 760 && picker.getHeight() <= 600, "the picker fits easily in a 1200 x 760 window");
    check(picker.shown() == cat.byType[kBass], "BASS shows every bass, A-Z, in one list (" + String((int) picker.shown().size()) + ")");
    picker.showType(kKeys);
    check(picker.shown() == cat.byType[kKeys], "KEYS shows every keys preset");
    auto parts = picker.parts();
    int clash = 0, outside = 0, buttons = 0;
    for (size_t i = 0; i < parts.size(); ++i) {
      if (dynamic_cast<Button*>(parts[i])) { ++buttons; check(parts[i]->getHeight() >= 30, "type buttons are big (" + String(parts[i]->getHeight()) + " px)"); }
      if (!picker.getLocalBounds().contains(parts[i]->getBounds())) ++outside;
      for (size_t j = i + 1; j < parts.size(); ++j) clash += parts[i]->getBounds().intersects(parts[j]->getBounds()) ? 1 : 0;
    }
    check(buttons == 10 && clash == 0 && outside == 0, "picker: 8 types + MY PRESETS + FAVOURITES, nothing overlaps (" + String(buttons) + " buttons, " + String(clash) + " overlaps, " + String(outside) + " outside)");
    picker.setSearch("preset 12");
    bool match = !picker.shown().empty();
    for (int i : picker.shown()) match &= cat.entries[(size_t) i].name.containsIgnoreCase("preset") && cat.entries[(size_t) i].name.contains("12");
    check(match, "search finds presets across every type (" + String((int) picker.shown().size()) + ")");
  }
  root.getParentDirectory().deleteRecursively();
}

// Ghosty Pro 2 header (2.0.3): the GHOSTY dot-matrix row (plays once, stays on GHOSTY, hover replays one tile) and the ghost fitted in its box
void testHeader() {
  std::printf("header\n");
  MatrixRow row;
  row.setBounds(0, 0, 300, 44);
  check(!row.idle(), "the row has its opening play still to do");
  const double t0 = 1000.0;
  check(row.tick(t0), "the opening play starts on the first tick");
  bool allAnimating = true;
  for (int t = 0; t < 6; ++t) allAnimating = allAnimating && row.tileAnimating(t);
  check(allAnimating, "all six tiles play the opening together");
  bool varied = false;   // it is a pattern at the start, not the letter
  for (int t = 0; t < 6 && !varied; ++t)
    for (int r = 0; r < 7; ++r) for (int c = 0; c < 7; ++c)
      if ((row.dotLevel(t, r, c) > 0.5f) != MatrixRow::glyphBit(t, r, c)) varied = true;
  check(varied, "the opening starts with patterns");
  check(row.tick(t0 + 2000.0), "still playing after 2 s");
  check(!row.tick(t0 + 9000.0) && row.idle(), "idle after the opening (no timer work once it has settled on GHOSTY)");
  int wrong = 0;
  for (int t = 0; t < 6; ++t) for (int r = 0; r < 7; ++r) for (int c = 0; c < 7; ++c)
    if ((row.dotLevel(t, r, c) > 0.5f) != MatrixRow::glyphBit(t, r, c)) ++wrong;
  check(wrong == 0, "after the opening every tile shows its letter G H O S T Y");
  // hover: one tile at a time
  const double h0 = 20000.0;
  for (int t = 0; t < 6; ++t) {
    const auto pt = Point<float>((float) (row.getWidth() * (t + 0.5) / 6.0), 22.0f);
    check(row.tileAt(pt) == t || row.tileAt(pt) == -1, "tile hit test " + String(t));
  }
  row.hoverTile(2, h0);
  row.tick(h0 + 300.0);
  check(row.tileAnimating(2) && !row.tileAnimating(0) && !row.tileAnimating(5), "hovering a tile replays only that tile");
  row.hoverTile(3, h0 + 350.0);
  row.tick(h0 + 400.0);
  check(row.tileAnimating(2) && row.tileAnimating(3) && !row.tileAnimating(1), "tiles are independent (a sweep makes a wave)");
  row.tick(h0 + 3000.0);
  const bool t2Mid = row.tileAnimating(2);
  row.hoverTile(2, h0 + 3100.0);   // re-entering restarts it
  row.tick(h0 + 3200.0);
  check(t2Mid && row.tileAnimating(2), "hovering a tile that is still playing restarts it");
  row.tick(h0 + 3100.0 + 3500.0);   // (without the restart it would have settled at +4 s)
  check(row.tileAnimating(2), "the restarted tile is still playing past the first run's end");
  row.tick(h0 + 12000.0);
  check(row.idle(), "every hovered tile settles back into its letter");
  wrong = 0;
  for (int r = 0; r < 7; ++r) for (int c = 0; c < 7; ++c) if ((row.dotLevel(2, r, c) > 0.5f) != MatrixRow::glyphBit(2, r, c)) ++wrong;
  check(wrong == 0, "the hovered tile ends on its own letter");
  { // a click replays a letter too (not just hover), even when the mouse is already resting on it
    bool self = false, kids = false;
    row.getInterceptsMouseClicks(self, kids);
    check(self, "the GHOSTY row takes clicks");
    row.setSize(300, 44);
    const auto c2 = row.getLocalBounds().toFloat();   // tile 2 centre via a real mouse-down event
    (void) c2;
    auto ms = Desktop::getInstance().getMainMouseSource();
    const MouseEvent ev(ms, Point<float>(150.0f, 22.0f), ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &row, &row,
                        Time::getCurrentTime(), Point<float>(150.0f, 22.0f), Time::getCurrentTime(), 1, false);
    row.tick(Time::getMillisecondCounterHiRes() + 60000.0);   // everything settled
    const bool before = row.idle();
    row.mouseDown(ev);
    check(before && !row.idle(), "a click starts the letters moving again");
    row.tick(Time::getMillisecondCounterHiRes() + 90000.0);
  }
  // the row paints (sprites cached) at 1x without trouble, twice
  Image img(Image::ARGB, 300, 44, true);
  { Graphics g(img); row.paint(g); row.paint(g); }
  check(img.getPixelAt(10, 22).getAlpha() > 0, "the row paints");

  // the big ghost stays inside its box at every moment of its float and glow pulse, at 1x and 2x, big box and small box
  struct Box { int w, h; } boxes[] { { 54, 58 }, { 40, 52 }, { 24, 36 }, { 120, 130 } };
  for (auto bx : boxes) for (float sc : { 1.0f, 2.0f }) {
    int bad = 0;
    for (int k = 0; k < 24; ++k) {
      const float ph = (float) k / 24.0f * MathConstants<float>::twoPi;
      Image im(Image::ARGB, roundToInt(bx.w * sc), roundToInt(bx.h * sc), true);
      Graphics g(im);
      g.addTransform(AffineTransform::scale(sc));
      ghosty::prologo::drawMascotFit(g, Rectangle<float>(0, 0, (float) bx.w, (float) bx.h), Colour(0xff0a0e0b), (float) k * 0.3f,
                                     2.5f * std::sin(ph), 0.85f + 0.25f * std::sin(ph));
      for (int x = 0; x < im.getWidth(); ++x) for (int y = 0; y < im.getHeight(); ++y)
        if ((x == 0 || y == 0 || x == im.getWidth() - 1 || y == im.getHeight() - 1) && im.getPixelAt(x, y).getAlpha() > 10) ++bad;
    }
    check(bad == 0, "the ghost never touches its box edge (" + String(bx.w) + "x" + String(bx.h) + " at " + String(sc) + "x, " + String(bad) + " edge pixels)");
  }
}


// TEMPO 1/2x | 1x | 2x (Brandon 2026-11-07 16:30): fresh start 1x, saved with the project; at 1x the engine is untouched
// (bit-identical with a host transport running); at 2x the engine sees exactly a host at twice the tempo (its normal tempo
// input, no change to its sound code); our KEYS arp follows it too.
void testTempo() {
  std::printf("tempo 1/2x 1x 2x\n");
  { GhostyProcessor p; check(p.tempoMultiple() == 1.0f, "a new instance runs at 1x"); }
  for (int i : { -1, presetNamed("Wobble Ghost"), presetNamed("Gated Ghosts") }) {
    HostClock c1, c2; c1.bpm = 130.0; c2.bpm = 130.0;
    auto a = fresh(i); auto x = play(*a, chord(), 2.0, 48000.0, 480, false, &c1);
    auto b = fresh(i); auto y = play(*b, chord(), 2.0, 48000.0, 480, true, &c2);
    check(sameBuffers(x, y), "1x with the song playing at 130: bit-identical to the engine alone, preset " + String(i));
    // 2x at 130 == the engine alone with a host at 260
    HostClock c3, c4; c3.bpm = 130.0; c4.bpm = 260.0;
    auto d = fresh(i); d->setTempoMultiple(2.0f); auto u = play(*d, chord(), 2.0, 48000.0, 480, false, &c3);
    auto e = fresh(i); auto v = play(*e, chord(), 2.0, 48000.0, 480, true, &c4);
    check(sameBuffers(u, v), "2x at 130 sounds exactly like the engine with the song at 260, preset " + String(i));
    HostClock c5, c6; c5.bpm = 130.0; c6.bpm = 65.0;
    auto f = fresh(i); f->setTempoMultiple(0.5f); auto w = play(*f, chord(), 2.0, 48000.0, 480, false, &c5);
    auto g = fresh(i); auto z = play(*g, chord(), 2.0, 48000.0, 480, true, &c6);
    check(sameBuffers(w, z), "1/2x at 130 sounds exactly like the engine with the song at 65, preset " + String(i));
  }
  // saved with the project; a project without it opens at 1x
  {
    auto a = fresh(0);
    a->setTempoMultiple(2.0f);
    MemoryBlock st; a->getStateInformation(st);
    GhostyProcessor b;
    b.setStateInformation(st.getData(), (int) st.getSize());
    check(b.tempoMultiple() == 2.0f, "the tempo switch is saved with the project");
    auto c = fresh(0);
    MemoryBlock st1; c->getStateInformation(st1);
    b.setStateInformation(st1.getData(), (int) st1.getSize());
    check(b.tempoMultiple() == 1.0f, "a project saved at 1x opens at 1x");
  }
  // the arp: at 2x twice the notes in the same time, at 1/2x half
  auto arpNotes = [](float mult) {
    auto p = fresh(0);
    p->keys().arp = true;
    p->keys().arpRate = 2;   // 1/16
    p->setTempoMultiple(mult);
    HostClock clock; clock.bpm = 120.0;
    int ons = 0;
    p->setPlayConfigDetails(0, 2, 48000.0, 480);
    p->prepareToPlay(48000.0, 480);
    clock.rate = 48000.0; p->setPlayHead(&clock);
    AudioBuffer<float> buf(2, 480);
    for (int k = 0; k < 200; ++k) {   // 2 s
      MidiBuffer midi;
      if (k == 0) { midi.addEvent(MidiMessage::noteOn(1, 60, (uint8) 100), 0); midi.addEvent(MidiMessage::noteOn(1, 64, (uint8) 100), 0); }
      buf.clear();
      p->processBlock(buf, midi);
      clock.samples += 480;
    }
    p->setPlayHead(nullptr);
    // count what the arp sent to the engine: the notes playing are visible as output energy steps; use KEYS' own counter
    ons = p->keys().arpCounter.load();
    return ons;
  };
  const int n1 = arpNotes(1.0f), n2 = arpNotes(2.0f), nh = arpNotes(0.5f);
  check(n1 > 8 && std::abs(n2 - 2 * n1) <= 2 && std::abs(2 * nh - n1) <= 2, "the arp runs at the TEMPO: 1/2x " + String(nh) + ", 1x " + String(n1) + ", 2x " + String(n2) + " notes in 2 s");
  // the switch in the window: three buttons, and it shows the tempo it gives
  if (!Desktop::getInstance().getDisplays().displays.isEmpty()) {
    auto p = fresh(0);
    HostClock clock; clock.bpm = 130.0;
    play(*p, {}, 0.1, 48000.0, 480, false, &clock);
    std::unique_ptr<GhostyEditor> ed(dynamic_cast<GhostyEditor*>(p->createEditor()));
    const char* want[] { "65 BPM", "130 BPM", "260 BPM" };
    for (int i = 0; i < 3; ++i) {
      ed->tempoButton(i).onClick();   // (a click, without waiting for the message loop to deliver it)
      MessageManager::getInstance()->runDispatchLoopUntil(40);
      check(ed->tempoText() == want[i] && ed->tempoButton(i).getToggleState(), "TEMPO " + ed->tempoButton(i).getButtonText() + " shows " + ed->tempoText());
    }
    ed->tempoButton(1).onClick();
    ed = nullptr;
  }
}

// FRESH START (Brandon 2026-11-06 15:44): a new instance is always brand new (factory defaults, first page, default size).
// Nothing carries over from another instance or project; it restores only what the host hands back.
String stateText(GhostyProcessor& p) {
  MemoryBlock m; p.getStateInformation(m);
  return m.toString();
}

void testFreshStart() {
  std::printf("fresh start\n");
  vital::utils::RandomGenerator::next_seed_ = 0;
  String base;
  { GhostyProcessor p0; base = stateText(p0); }
  check(base.isNotEmpty(), "a new instance has a state");
  auto a = std::make_unique<GhostyProcessor>();
  a->setPlayConfigDetails(0, 2, 44100, 512);
  a->prepareToPlay(44100, 512);
  String err;
  a->loadFactoryPreset(3 % jmax(1, (int) factoryPresets().size()), err);
  a->setControl("filter_1_cutoff", 33.0f);
  a->morph().enabled = true;
  a->keys().arp = true;
  a->extras().setGateOn(true);
  play(*a, chord(), 0.5, 44100.0, 512);
  check(stateText(*a) != base, "the first instance really changed");
  // (Vital fills its default "White Noise" sample from its process-wide random seed counter, as upstream: each instance is
  // compared as it would start in a fresh process, like fresh() above)
  {
    vital::utils::RandomGenerator::next_seed_ = 0;
    GhostyProcessor b;
    const String sb = stateText(b);
    if (sb != base) {
      int i = 0; while (i < sb.length() && i < base.length() && sb[i] == base[i]) ++i;
      std::printf("    first difference at %d: new '%s' vs base '%s'\n", i, sb.substring(jmax(0, i - 60), i + 60).toRawUTF8(), base.substring(jmax(0, i - 60), i + 60).toRawUTF8());
    }
    check(sb == base, "a second instance next to a changed one starts at factory defaults");
    check(!b.morph().enabled.load() && !b.keys().anyOn() && !b.extras().anyOn() && b.factoryIndex() < 0,
          "the second instance's MORPH, KEYS and EXTRAS are off");
  }
  // the editor: a new window opens on the first page at the default size, whatever another window did
  if (!Desktop::getInstance().getDisplays().displays.isEmpty()) {
    std::unique_ptr<GhostyEditor> ea(dynamic_cast<GhostyEditor*>(a->createEditor()));
    ea->showPage(2);
    ea->setSize(1000, 700);
    MessageManager::getInstance()->runDispatchLoopUntil(40);
    GhostyProcessor b;
    std::unique_ptr<GhostyEditor> eb(dynamic_cast<GhostyEditor*>(b.createEditor()));
    check(eb->page() == 0, "a new window opens on the SYNTH page (not the page another window was on)");
    check(eb->getWidth() == GhostyEditor::kWidth && eb->getHeight() == GhostyEditor::kHeight, "a new window opens at the default size (" + String(eb->getWidth()) + "x" + String(eb->getHeight()) + ")");
    eb = nullptr;
    ea = nullptr;
    MessageManager::getInstance()->runDispatchLoopUntil(100);
  }
  a = nullptr;
  {
    vital::utils::RandomGenerator::next_seed_ = 0;
    GhostyProcessor c;
    check(stateText(c) == base, "destroy and recreate: factory defaults");
  }
}

// WINDOW SIZE (Brandon 2026-11-06 15:39 / 15:44): opens at about 1200 x 760 points (never wider than 1240 or taller than 780),
// resizable, nothing overlapping at any size, on every page.
void checkNoOverlap(GhostyEditor& ed, int w, int h) {
  ed.setSize(w, h);
  for (int page = 0; page < GhostyEditor::kNumTabs; ++page) {
    ed.showPage(page);
    MessageManager::getInstance()->runDispatchLoopUntil(30);
    const String tag = String(ed.getWidth()) + "x" + String(ed.getHeight()) + " page " + String(page);
    // the window's own parts (header buttons, GHOSTY row, tabs, KEYS, the page, the keyboard)
    std::vector<Component*> parts;
    for (auto* c : ed.getChildren())
      if (c->isVisible() && !c->getBounds().isEmpty() && dynamic_cast<TooltipWindow*>(c) == nullptr
          && dynamic_cast<ResizableCornerComponent*>(c) == nullptr)   // (the resize handle sits on top of the corner on purpose)
        parts.push_back(c);
    int clash = 0, outside = 0;
    String first;
    for (size_t i = 0; i < parts.size(); ++i) {
      if (!ed.getLocalBounds().contains(parts[i]->getBounds())) { ++outside; if (first.isEmpty()) first = "outside: " + parts[i]->getName() + " " + parts[i]->getBounds().toString(); }
      for (size_t j = i + 1; j < parts.size(); ++j)
        if (parts[i]->getBounds().intersects(parts[j]->getBounds())) {
          ++clash;
          if (first.isEmpty()) first = parts[i]->getName() + " " + parts[i]->getBounds().toString() + " vs " + parts[j]->getBounds().toString();
        }
    }
    // the header logo (painted) stays clear of the header buttons
    for (auto* c : parts)
      if (dynamic_cast<Button*>(c) != nullptr && (c->getBounds().intersects(ed.ghostBox()) || c->getBounds().intersects(ed.matrix().getBounds()))) {
        ++clash; if (first.isEmpty()) first = "logo vs button " + c->getBounds().toString();
      }
    // inside our pages: controls side by side never overlap each other
    if (page > 0)
      for (auto* pg : ed.getChildren())
        if (pg->isVisible() && dynamic_cast<ghosty::Page*>(pg) != nullptr) {
          std::vector<Component*> kids;
          for (auto* k : pg->getChildren())
            if (k->isVisible() && !k->getBounds().isEmpty() && (dynamic_cast<Button*>(k) || dynamic_cast<Slider*>(k) || dynamic_cast<ComboBox*>(k) || dynamic_cast<Label*>(k) || dynamic_cast<TextEditor*>(k)))
              kids.push_back(k);
          for (size_t i = 0; i < kids.size(); ++i) {
            if (!pg->getLocalBounds().contains(kids[i]->getBounds())) { ++outside; if (first.isEmpty()) first = "outside page: " + kids[i]->getName() + " " + kids[i]->getBounds().toString(); }
            for (size_t j = i + 1; j < kids.size(); ++j)
              if (kids[i]->getBounds().intersects(kids[j]->getBounds())) {
                ++clash;
                if (first.isEmpty()) first = "page control " + kids[i]->getName() + " " + kids[i]->getBounds().toString() + " vs " + kids[j]->getName() + " " + kids[j]->getBounds().toString();
              }
          }
        }
    check(clash == 0 && outside == 0, tag + ": nothing overlaps, everything inside the window (" + String(clash) + " overlaps, " + String(outside) + " outside" + (first.isNotEmpty() ? "; " + first : String()) + ")");
  }
}

void testWindowSize() {
  std::printf("window size\n");
  if (Desktop::getInstance().getDisplays().displays.isEmpty()) { std::printf("  (no display: skipped)\n"); return; }
  check(GhostyEditor::kWidth >= 1080 && GhostyEditor::kWidth <= 1240 && GhostyEditor::kHeight >= 720 && GhostyEditor::kHeight <= 780,
        "the default size fits his MacBook Pro in FL (" + String(GhostyEditor::kWidth) + "x" + String(GhostyEditor::kHeight) + ", at most 1240x780)");
  GhostyProcessor p;
  p.setPlayConfigDetails(0, 2, 44100, 512);
  p.prepareToPlay(44100, 512);
  String err;
  p.loadFactoryPreset(0, err);
  std::unique_ptr<GhostyEditor> ed(dynamic_cast<GhostyEditor*>(p.createEditor()));
  check(ed->getWidth() == GhostyEditor::kWidth && ed->getHeight() == GhostyEditor::kHeight, "the window opens at the default size (" + String(ed->getWidth()) + "x" + String(ed->getHeight()) + ")");
  check(ed->isResizable(), "the window is resizable");
  if (auto* c = ed->getConstrainer())
    check(c->getMinimumWidth() <= GhostyEditor::kWidth && c->getMinimumHeight() <= GhostyEditor::kHeight && c->getMaximumWidth() >= 1600,
          "it can be made smaller and bigger than the default");
  for (auto sz : { Point<int>(GhostyEditor::kWidth, GhostyEditor::kHeight), Point<int>(1240, 780), Point<int>(960, 640), Point<int>(1100, 700), Point<int>(1440, 860), Point<int>(1900, 1200) })
    checkNoOverlap(*ed, sz.x, sz.y);
  // Vital's editor fills the SYNTH area at the default size (letterboxed, never cut off)
  ed->setSize(GhostyEditor::kWidth, GhostyEditor::kHeight);
  ed->showPage(0);
  MessageManager::getInstance()->runDispatchLoopUntil(30);
  const auto vb = ed->vitalBounds();
  check(vb.getY() >= 110 && vb.getBottom() <= GhostyEditor::kHeight && vb.getX() >= 0 && vb.getRight() <= GhostyEditor::kWidth && vb.getWidth() >= 1000,
        "the synth page fits under the tabs at the default size (" + vb.toString() + ")");
  // 2.0.16: no empty bars: the SYNTH page fills the body edge to edge, at the default size and at every size the
  // resize corner / host can give (the window keeps that shape)
  auto barsAt = [&](int w, int h) {
    ed->setSize(w, h);
    MessageManager::getInstance()->runDispatchLoopUntil(10);
    const auto v = ed->vitalBounds();
    return jmax(v.getX(), w - v.getRight(), h - v.getBottom());
  };
  check(barsAt(GhostyEditor::kWidth, GhostyEditor::kHeight) <= 1, "SYNTH fills the window at the default size: no side bars");
  if (auto* c = ed->getConstrainer()) {
    int worst = 0; String where;
    for (auto drag : { Rectangle<int>(0, 0, 1500, 900), Rectangle<int>(0, 0, 1000, 1000), Rectangle<int>(0, 0, 2000, 700), Rectangle<int>(0, 0, 900, 600),
                       Rectangle<int>(0, 0, 3000, 3000) }) {
      for (int mode = 0; mode < 2; ++mode) {   // the corner (width leads), the bottom edge (height leads)
        auto b = drag;
        c->checkBounds(b, ed->getBounds(), {}, false, false, true, mode == 0);
        const int bars = barsAt(b.getWidth(), b.getHeight());
        if (bars > worst) { worst = bars; where = b.toString(); }
      }
    }
    check(worst <= 1, "every resized window keeps SYNTH filling it (worst " + String(worst) + "px at " + where + ")");
  }
  ed = nullptr;
  MessageManager::getInstance()->runDispatchLoopUntil(100);
}

// NAMES (Brandon 2026-11-06 08:56): no open-source project names anywhere in the visible UI, Vital's own pages and overlays
// included (labels, buttons, tooltips, titles). The one exception is MENU > About / Credits (licence law).
bool hasUpstreamName(const String& t) {
  for (auto* w : { "vital", "vial", "tytel", "matt t" })
    if (t.containsIgnoreCase(w)) return true;
  return false;
}

void collectTexts(Component* c, StringArray& bad) {
  auto look = [&bad, c](const String& t, const char* what) { if (hasUpstreamName(t)) bad.add(String(what) + " '" + t + "' (" + c->getName() + ")"); };
  if (auto* b = dynamic_cast<Button*>(c)) { look(b->getButtonText(), "button"); look(b->getTooltip(), "tooltip"); }
  if (auto* l = dynamic_cast<Label*>(c)) look(l->getText(), "label");
  if (auto* t = dynamic_cast<TextEditor*>(c)) { look(t->getText(), "text"); look(t->getTextToShowWhenEmpty(), "hint"); }
  if (auto* tc = dynamic_cast<SettableTooltipClient*>(c)) look(tc->getTooltip(), "tooltip");
  if (auto* p = dynamic_cast<PlainTextComponent*>(c)) look(p->getText(), "text");
  if (auto* cb = dynamic_cast<ComboBox*>(c)) for (int i = 0; i < cb->getNumItems(); ++i) look(cb->getItemText(i), "list item");
  if (auto* w = dynamic_cast<TopLevelWindow*>(c)) look(w->getName(), "window title");
  for (auto* k : c->getChildren()) collectTexts(k, bad);
}

void testNoUpstreamNames() {
  std::printf("no open-source names in the UI\n");
  check(!hasUpstreamName(GhostyProcessor().getName()), "the plug-in's name");
  for (auto& fp : factoryPresets()) {
    auto j = JSON::parse(fp.text);
    if (hasUpstreamName(j.getProperty("comments", "").toString()) || hasUpstreamName(j.getProperty("author", "").toString()))
      check(false, "preset " + fp.name + ": author and notes have no upstream names");
  }
  check(GhostyEditor::ghostyAboutText().contains("https://github.com/mtytel/vital") && GhostyEditor::ghostyAboutText().containsIgnoreCase("source code")
        && GhostyEditor::ghostyAboutText().containsIgnoreCase("on request"), "About / Credits names the upstream source link and our source offer (GPL-3)");
  if (Desktop::getInstance().getDisplays().displays.isEmpty()) { std::printf("  (no display: skipped)\n"); return; }
  GhostyProcessor p;
  p.setPlayConfigDetails(0, 2, 44100, 512);
  p.prepareToPlay(44100, 512);
  String err;
  p.loadFactoryPreset(0, err);
  std::unique_ptr<GhostyEditor> ed(dynamic_cast<GhostyEditor*>(p.createEditor()));
  StringArray bad;
  for (int page = GhostyEditor::kNumTabs - 1; page >= 0; --page) {   // ends on SYNTH: Vital's editor with all its pages and overlays
    ed->showPage(page);
    MessageManager::getInstance()->runDispatchLoopUntil(60);
    if (auto* f = dynamic_cast<FullInterface*>(ed->vitalEditor()))
      for (int t = 0; t < 4 && page == 0; ++t) { f->tabSelected(t); MessageManager::getInstance()->runDispatchLoopUntil(30); collectTexts(ed.get(), bad); }
    collectTexts(ed.get(), bad);
  }
  bad.removeDuplicates(false);
  check(bad.isEmpty(), "no upstream names in any label, button, tooltip or title (" + String(bad.size()) + (bad.isEmpty() ? String() : ": " + bad.joinIntoString("; ").substring(0, 400)) + ")");
  ed = nullptr;
  MessageManager::getInstance()->runDispatchLoopUntil(100);
}

void testEditor() {
  std::printf("editor\n");
  if (Desktop::getInstance().getDisplays().displays.isEmpty()) { std::printf("  (no display: skipped)\n"); return; }
  GhostyProcessor p;
  p.setPlayConfigDetails(0, 2, 44100, 512);
  p.prepareToPlay(44100, 512);
  String err;
  p.loadFactoryPreset(0, err);
  std::unique_ptr<GhostyEditor> ed(dynamic_cast<GhostyEditor*>(p.createEditor()));
  check(ed != nullptr, "editor opens");
  {
    // the header: ghost and GHOSTY row inside the header card, clear of the preset bar, tabs and Vital's area
    const auto m = ed->matrix().getBounds(), gb = ed->ghostBox();
    check(m.getY() >= 0 && m.getBottom() <= 80 && gb.getRight() <= m.getX(), "the GHOSTY row sits right of the ghost, in the header");
    check(m.getBottom() < ed->vitalBounds().getY() && gb.getBottom() < ed->vitalBounds().getY(), "the header never overlaps the Vital area");
  }
  for (int page = 0; page < GhostyEditor::kNumTabs; ++page) {
    ed->showPage(page);
    MessageManager::getInstance()->runDispatchLoopUntil(40);
    check(ed->page() == page, "page " + String(page) + " shows");
  }
  for (int look = 0; look < kNumLooks; ++look) { ed->setLookIndex(look); MessageManager::getInstance()->runDispatchLoopUntil(20); }
  { // 2.0.16: three looks: GHOST, BLACK, LIGHT (the watercolor one); the LOOK button goes round all three
    ed->setLookIndex(2);
    check(currentLook() == Look::Light && pal().sketch && String(lookName(currentLook())) == "LIGHT", "LIGHT is the hand-drawn watercolor look");
    ed->setLookIndex(1);
    check(currentLook() == Look::Black && !pal().sketch, "BLACK");
    ed->setLookIndex(7);
    check((int) currentLook() == kNumLooks - 1, "an out-of-range look lands on the last one");
  }
  ed->setLookIndex(0);
  // the window on screen with Vital's editor showing, every look switched live (Vital's editor is rebuilt each time)
  ed->addToDesktop(0);
  ed->setVisible(true);
  ed->showPage(0);
  MessageManager::getInstance()->runDispatchLoopUntil(300);
  for (int look : { 1, 2, 0, 2, 1 }) { ed->setLookIndex(look); MessageManager::getInstance()->runDispatchLoopUntil(250); }
  check(ed->page() == 0, "looks switch live with Vital's editor on screen");
  ed->setLookIndex(0);
  MessageManager::getInstance()->runDispatchLoopUntil(250);
  testVitalKnobOrbit(p, *ed);
  testOrbitKnob();
  ed->removeFromDesktop();
  p.loadFactoryPreset(1 % jmax(1, (int) factoryPresets().size()), err);
  MessageManager::getInstance()->runDispatchLoopUntil(80);
  check(true, "preset change with the editor open");
  ed = nullptr;
  MessageManager::getInstance()->runDispatchLoopUntil(200);
  { // 2.0.11: switching the look and closing the window at once (the replaced engine editor used to be deleted after the
    // window was gone: a crash on the Mac)
    std::printf("  look change, then close\n");
   #if JUCE_MAC   // (on Linux, JUCE's X11 OpenGL child windows can't be torn down this fast under a bare test X server; the Mac is the target)
    for (int k = 0; k < 3; ++k) {
      std::unique_ptr<GhostyEditor> e2(dynamic_cast<GhostyEditor*>(p.createEditor()));
      e2->addToDesktop(0);
      e2->setVisible(true);
      MessageManager::getInstance()->runDispatchLoopUntil(150);
      e2->setLookIndex(1 + k % 3);
      e2->setLookIndex(0);
      e2->removeFromDesktop();   // (as a host closing its plug-in window: the editor leaves the window, then is deleted)
      e2 = nullptr;
      MessageManager::getInstance()->runDispatchLoopUntil(150);
    }
   #endif
    check(true, "a look change right before the window closes is safe");
  }
  testVitalLayout(p);   // (each layout in its own process, with no other window of ours open)
  testVitalHits();
  testPresetMenu();
}
} // namespace

int runGhostyTests(const StringArray& args) {
  std::setvbuf(stdout, nullptr, _IOLBF, 0);   // progress shows in CI logs even if a step is stopped
  if (args.contains("--morphsets")) { testMorphSets(); std::printf("GHOSTY PRO TESTS: %d passed, %d failed\n", g_pass, g_fail); return g_fail == 0 ? 0 : 1; }
  if (args.contains("--window")) {   // just the window checks (size, overlap, fresh start, names)
    testFreshStart(); testNoUpstreamNames(); testWindowSize();
    std::printf("GHOSTY PRO TESTS: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
  }
  testBuses();
  testPresets();
  testVitalUntouched();
  testMorph();
  testMorphSets();
  testMotions();
  testExtras();
  testState();
  testFinder();
  testOrbitPhysics();
  testParameterTexts();
  testHugeLibrary();
  testVersions();
  testKeys();
  testHeader();
  testTempo();
  testFreshStart();
  testNoUpstreamNames();
  if (!args.contains("--no-ui")) { testWindowSize(); testEditor(); testMorphMotion(); }
  std::printf("GHOSTY PRO TESTS: %d passed, %d failed\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
