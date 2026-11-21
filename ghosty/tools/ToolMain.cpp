// GhostyProTool: Ghosty Pro's processor and editor outside a plugin, for tests, renders, the null test and screenshots.
//   GhostyProTool test                                   run the tests (GhostyTests.cpp)
//   GhostyProTool render <preset.vital|init> <out.wav> [seconds] [notes]   Vital's own SynthBase::renderAudioToFile on OUR
//                                                        processor, after loading through OUR loader (null-test side B)
//   GhostyProTool nulltest <UpstreamHeadless> <out dir> <.vital/.vitalbank/folder>...   A/B against Vital's own headless
//   GhostyProTool demo <preset index|file> <out.wav> <pattern> [bpm]   a musical phrase through processBlock (as a DAW)
//   GhostyProTool levels                                 every Ghosty Pro preset: true peak, loudness, clicks
//   GhostyProTool shots <out dir>                        screenshots of every page (needs a display: xvfb-run)
//   GhostyProTool dump-params                            every Vital control: name, range, default, scale
#include "JuceHeader.h"
#include "GhostyProcessor.h"
#include "GhostyEditor.h"
#include "GhostyPresets.h"
#include "VitalPresetFinder.h"
#include "ToolHelpers.h"
#include "GhostyProLogo.h"
#include "synth_parameters.h"
#include "load_save.h"
#include "sound_engine.h"
#include "synth_section.h"
#include "synth_slider.h"
#include "full_interface.h"
#include "synth_button.h"
#include "GhostyHit.h"
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <mutex>
#include <atomic>
#include <set>

using namespace ghosty;
using namespace ghosty::tool;

int runGhostyTests(const StringArray& args);

namespace {

std::vector<int> parseNotes(const String& s) {
  std::vector<int> out;
  for (auto& t : StringArray::fromTokens(s, ",", "")) {
    int m = Tuning::noteToMidiKey(t.trim());
    if (m >= 0) out.push_back(m);
  }
  if (out.empty()) out = { 48 };
  return out;
}

bool loadAny(GhostyProcessor& p, const String& what, String& err) {
  if (what == "init") { p.loadInit(); return true; }
  if (what.containsOnly("0123456789")) return p.loadFactoryPreset(what.getIntValue(), err);
  File f = File::getCurrentWorkingDirectory().getChildFile(what);
  if (f.existsAsFile()) return p.loadPresetFile(f, err);
  const auto& list = factoryPresets();
  for (int i = 0; i < (int) list.size(); ++i)
    if (list[(size_t) i].name.equalsIgnoreCase(what)) return p.loadFactoryPreset(i, err);
  err = "no such preset: " + what;
  return false;
}

// a phrase per pattern name, at 'bpm'
std::vector<Note> phrase(const String& pattern, double bpm, double& seconds) {
  const double b = 60.0 / bpm;
  std::vector<Note> n;
  auto add = [&](double beat, double len, int note, int vel = 100) { n.push_back({ beat * b, len * b, note, vel }); };
  if (pattern == "bass") {        // trap / house bass line
    const int line[] { 36, 36, 39, 36, 43, 41, 39, 34 };
    for (int bar = 0; bar < 2; ++bar) for (int i = 0; i < 8; ++i) add(bar * 4 + i * 0.5, i % 4 == 3 ? 0.9 : 0.4, line[i] + (bar == 1 && i > 5 ? 2 : 0), 110);
  }
  else if (pattern == "808") {
    const double at[] { 0, 1.5, 2.75, 4, 5.5, 6.5, 7 };
    const int nt[] { 36, 36, 39, 36, 34, 36, 31 };
    for (int i = 0; i < 7; ++i) add(at[i], i == 6 ? 1.0 : 1.2, nt[i], 120);
  }
  else if (pattern == "chords") { // pads / keys
    const int ch[4][4] { { 57, 60, 64, 67 }, { 53, 57, 60, 64 }, { 55, 59, 62, 65 }, { 52, 55, 59, 64 } };
    for (int c = 0; c < 4; ++c) for (int k = 0; k < 4; ++k) add(c * 2, 1.9, ch[c][k], 90);
  }
  else if (pattern == "lead") {
    const double at[] { 0, 0.5, 1, 1.5, 2, 3, 3.5, 4, 4.5, 5, 6 };
    const int nt[] { 72, 74, 76, 79, 76, 74, 72, 74, 76, 72, 69 };
    for (int i = 0; i < 11; ++i) add(at[i], i == 10 ? 1.8 : 0.45, nt[i], 105);
  }
  else if (pattern == "pluck") {
    const int arp[] { 60, 64, 67, 72, 67, 64, 62, 65, 69, 74, 69, 65, 59, 62, 67, 71 };
    for (int i = 0; i < 16; ++i) add(i * 0.5, 0.3, arp[i], 100);
  }
  else {                          // fx / one hit
    add(0, 3.0, 60, 110);
    add(4, 2.0, 67, 100);
  }
  double last = 0;
  for (auto& x : n) last = jmax(last, x.start + x.length);
  seconds = last + 2.5;
  return n;
}

int cmdRender(const StringArray& a) {
  if (a.size() < 3) return 2;
  GhostyProcessor p;
  String err;
  if (!loadAny(p, a[1], err)) { std::printf("%s\n", err.toRawUTF8()); return 1; }
  float seconds = a.size() > 3 ? a[3].getFloatValue() : 5.0f;
  p.renderAudioToFile(File::getCurrentWorkingDirectory().getChildFile(a[2]), seconds, 120.0f, parseNotes(a.size() > 4 ? a[4] : "C3"), false);
  return 0;
}

struct NullResult { String name; int samples = 0; int maxDiff = 0; double nullDb = 0; bool identical = false; float rms = -200; };

NullResult compareWavs(const File& a, const File& b) {
  NullResult r;
  AudioBuffer<float> x, y; double ra = 0, rb = 0;
  if (!readWav(a, x, ra) || !readWav(b, y, rb) || x.getNumSamples() != y.getNumSamples()) { r.maxDiff = -1; return r; }
  r.samples = x.getNumSamples();
  double e = 0, s = 0;
  float md = 0;
  for (int c = 0; c < 2; ++c) for (int i = 0; i < x.getNumSamples(); ++i) {
    const float d = x.getSample(c, i) - y.getSample(c, i);
    md = jmax(md, std::abs(d));
    e += d * (double) d; s += x.getSample(c, i) * (double) x.getSample(c, i);
  }
  r.maxDiff = (int) std::round(md * 32768.0f);   // in 16-bit steps (the headless renders are 16-bit)
  r.identical = md == 0.0f;
  r.nullDb = e <= 0 ? -999.0 : 10.0 * std::log10(e / jmax(1.0e-30, s));
  r.rms = rmsDb(x);
  return r;
}

int cmdNullTest(const StringArray& a) {
  if (a.size() < 4) return 2;
  File upstream = File::getCurrentWorkingDirectory().getChildFile(a[1]);
  File outDir = File::getCurrentWorkingDirectory().getChildFile(a[2]);
  outDir.createDirectory();
  // collect presets: .vital files, folders, and .vitalbank banks (imported with Vital's own Import Bank steps)
  Array<File> presets;
  for (int i = 3; i < a.size(); ++i) {
    File f = File::getCurrentWorkingDirectory().getChildFile(a[i]);
    if (f.isDirectory()) {
      for (auto& fp : VitalPresetFinder::scanNow({ f }))
        if (!fp.bank) presets.add(fp.file);
    }
    else if (f.hasFileExtension("vitalbank")) {
      String err;
      if (!VitalPresetFinder::importBank(f, err)) { std::printf("bank %s: %s\n", f.getFileName().toRawUTF8(), err.toRawUTF8()); continue; }
      File before = VitalPresetFinder::dataFolder();   // (after the import: the import is what sets the folder up)
      ZipFile zip(f);
      for (int k = 0; k < zip.getNumEntries(); ++k) {
        String n = zip.getEntry(k)->filename;
        if (n.endsWithIgnoreCase(".vital") && !n.contains("__MACOSX")) presets.add(before.getChildFile(n));
      }
    }
    else if (f.existsAsFile()) presets.add(f);
  }
  int identical = 0, total = 0, worst = 0;
  const String notes = "C3,E3,G3";
  for (auto& f : presets) {
    File up = outDir.getChildFile("up.wav"), ours = outDir.getChildFile("ours.wav");
    ChildProcess cp;
    StringArray cmd { upstream.getFullPathName(), "--headless", f.getFullPathName(), "-o", up.getFullPathName(), "-m", notes, "-l", "3" };
    up.deleteFile();
    if (!cp.start(cmd)) { std::printf("can't run upstream\n"); return 1; }
    cp.readAllProcessOutput();
    cp.waitForProcessToFinish(120000);
    // our side in its own fresh process too: Vital seeds its random generators from a process-wide counter, so each
    // side must be the first synth of its process (as each Vital instance is the first in the upstream render)
    ours.deleteFile();
    ChildProcess mine;
    StringArray cmd2 { File::getSpecialLocation(File::currentExecutableFile).getFullPathName(), "render", f.getFullPathName(), ours.getFullPathName(), "3", notes };
    if (!mine.start(cmd2)) { std::printf("can't run myself\n"); return 1; }
    String said = mine.readAllProcessOutput();
    mine.waitForProcessToFinish(120000);
    if (!ours.existsAsFile()) { std::printf("LOAD FAIL %s: %s\n", f.getFileName().toRawUTF8(), said.trim().toRawUTF8()); ++total; continue; }
    NullResult r = compareWavs(up, ours);
    ++total;
    if (r.identical) ++identical;
    worst = jmax(worst, r.maxDiff);
    std::printf("%s  %-48s samples %d  max diff %d (16-bit steps)  null %.1f dB  level %.1f dBFS\n", r.identical ? "IDENTICAL" : (r.maxDiff < 0 ? "ERROR    " : "DIFFERENT"),
                f.getFileNameWithoutExtension().substring(0, 48).toRawUTF8(), r.samples, r.maxDiff, r.nullDb, r.rms);
  }
  std::printf("NULL TEST: %d of %d presets bit-identical to Vital's own headless render (worst difference %d steps)\n", identical, total, worst);
  return identical == total && total > 0 ? 0 : 1;
}

// preset-folders: the folders the preset picker looks in on this computer, one per line (CI passes them to nulltest-all)
int cmdPresetFolders(const StringArray&) {
  Array<File> roots = VitalPresetFinder::vitalFolders();
  const File data = VitalPresetFinder::dataFolder();
  if (data != File() && data.isDirectory() && !roots.contains(data)) roots.add(data);
  roots.addArray(VitalPresetFinder::addedFolders());
  for (auto& r : roots) std::printf("%s\n", r.getFullPathName().toRawUTF8());
  return 0;
}

// nulltest-all <upstream headless> <work dir> [--seconds s] [--jobs n] [--shard i/n] <folder>...
// The null test over EVERY preset in those folders (read only; copies - same name and size - checked once, as the picker
// shows them once): each preset rendered by the engine's own headless build and by Ghosty Pro 2, each in its own fresh
// process, compared sample by sample. A preset that differs is rendered by the headless build a second time: if the engine
// differs from itself, the preset is random by design (noise, random phase), not us.
int cmdNullTestAll(const StringArray& a) {
  if (a.size() < 4) return 2;
  std::setvbuf(stdout, nullptr, _IOLBF, 0);
  const File upstream = File::getCurrentWorkingDirectory().getChildFile(a[1]);
  const File work = File::getCurrentWorkingDirectory().getChildFile(a[2]);
  work.createDirectory();
  double seconds = 1.0, budgetMinutes = 0.0; int jobs = jmax(1, SystemStats::getNumCpus() - 1), shard = 0, shards = 1;
  Array<File> folders;
  for (int i = 3; i < a.size(); ++i) {
    if (a[i] == "--seconds" && i + 1 < a.size()) seconds = a[++i].getDoubleValue();
    else if (a[i] == "--jobs" && i + 1 < a.size()) jobs = jmax(1, a[++i].getIntValue());
    else if (a[i] == "--minutes" && i + 1 < a.size()) budgetMinutes = a[++i].getDoubleValue();
    else if (a[i] == "--shard" && i + 1 < a.size()) { auto t = StringArray::fromTokens(a[++i], "/", ""); shard = t[0].getIntValue(); shards = jmax(1, t[1].getIntValue()); }
    else if (File::isAbsolutePath(a[i]) && File(a[i]).isDirectory()) {
      const File f(a[i]);
      bool inside = false;   // (a folder given twice, or inside another one given: scanned once)
      for (auto& g : folders) inside |= f == g || f.isAChildOf(g);
      if (!inside) { for (int k = folders.size(); --k >= 0;) if (folders[k].isAChildOf(f)) folders.remove(k); folders.add(f); }
    }
    else std::printf("SKIPPED folder %s (not a folder)\n", a[i].toRawUTF8());
  }
  std::vector<FoundPreset> all;
  std::set<String> seen;
  int copies = 0, hiddenNewer = 0, hiddenBad = 0;
  for (auto& fp : VitalPresetFinder::scanNow(folders)) {
    if (fp.bank) continue;
    if (fp.unusable.isNotEmpty()) { (fp.unusable == "newer" ? hiddenNewer : hiddenBad)++; continue; }   // the picker hides these too
    if (fp.size > 0 && !seen.insert(fp.name.toLowerCase() + "|" + String(fp.size)).second) { ++copies; continue; }
    all.push_back(fp);
  }
  std::vector<FoundPreset> mine;
  for (size_t i = 0; i < all.size(); ++i) if ((int) (i % (size_t) shards) == shard) mine.push_back(all[i]);
  std::printf("hidden by the picker (not checked): %d need a newer synth version, %d can't be read\n", hiddenNewer, hiddenBad);
  std::printf("presets: %d in %d folders (%d copies counted once); this run checks %d (part %d of %d), %.1f s each, %d at a time\n",
              (int) all.size(), folders.size(), copies, (int) mine.size(), shard + 1, shards, seconds, jobs);
  const String notes = "C3,E3,G3", secs = String(seconds);
  const File self = File::getSpecialLocation(File::currentExecutableFile);
  auto render = [](const File& exe, const StringArray& args, const File& out, String& said) {
    out.deleteFile();
    ChildProcess cp;
    StringArray cmd { exe.getFullPathName() };
    cmd.addArray(args);
    if (!cp.start(cmd)) { said = "could not start"; return false; }
    said = cp.readAllProcessOutput();
    if (!cp.waitForProcessToFinish(60000)) { cp.kill(); said = "took over 60 s"; return false; }
    return out.existsAsFile() && out.getSize() > 44;
  };
  std::mutex lock;
  std::atomic<int> next { 0 }, done { 0 };
  int identical = 0, differ = 0, randomByDesign = 0, unreadable = 0, worst = 0;
  const double t0 = Time::getMillisecondCounterHiRes();
  auto worker = [&](int id) {
    const File up = work.getChildFile("up" + String(id) + ".wav"), ours = work.getChildFile("ours" + String(id) + ".wav"),
               up2 = work.getChildFile("again" + String(id) + ".wav");
    for (int k; (k = next++) < (int) mine.size();) {
      if (budgetMinutes > 0.0 && Time::getMillisecondCounterHiRes() - t0 > budgetMinutes * 60000.0) { next = (int) mine.size(); break; }
      const auto& fp = mine[(size_t) k];
      String said, said2, line;
      int kind = 0, diff = 0;   // 0 identical, 1 differs, 2 random by design, 3 unreadable
      if (!render(upstream, { "--headless", fp.file.getFullPathName(), "-o", up.getFullPathName(), "-m", notes, "-l", secs }, up, said)) {
        kind = 3; line = "UNREADABLE  " + fp.file.getFullPathName() + "  (the engine's own build could not render it: " + said.trim().replace("\n", " ").substring(0, 120) + ")";
      }
      else if (!render(self, { "render", fp.file.getFullPathName(), ours.getFullPathName(), secs, notes }, ours, said2)) {
        kind = 3; line = "UNREADABLE  " + fp.file.getFullPathName() + "  (Ghosty Pro 2 could not open it: " + said2.trim().replace("\n", " ").substring(0, 120) + ")";
      }
      else {
        const NullResult r = compareWavs(up, ours);
        if (!r.identical) {
          diff = r.maxDiff;
          String s3;
          const bool again = render(upstream, { "--headless", fp.file.getFullPathName(), "-o", up2.getFullPathName(), "-m", notes, "-l", secs }, up2, s3);
          const NullResult self2 = again ? compareWavs(up, up2) : NullResult();
          kind = again && !self2.identical ? 2 : 1;
          line = String(kind == 2 ? "RANDOM BY DESIGN  " : "DIFFERENT  ") + fp.file.getFullPathName() + "  max diff " + String(r.maxDiff) + " steps, null " + String(r.nullDb, 1) + " dB"
                 + (kind == 2 ? "  (the engine's own build differs from itself by " + String(self2.maxDiff) + " steps)" : String());
        }
      }
      std::lock_guard<std::mutex> g(lock);
      if (kind == 0) ++identical; else if (kind == 1) { ++differ; worst = jmax(worst, diff); } else if (kind == 2) ++randomByDesign; else ++unreadable;
      if (line.isNotEmpty()) std::printf("%s\n", line.toRawUTF8());
      if (++done % 500 == 0) std::printf("  ... %d of %d checked (%.0f s)\n", done.load(), (int) mine.size(), (Time::getMillisecondCounterHiRes() - t0) / 1000.0);
    }
  };
  std::vector<std::thread> threads;
  for (int j = 0; j < jobs; ++j) threads.emplace_back(worker, j);
  for (auto& t : threads) t.join();
  const double took = (Time::getMillisecondCounterHiRes() - t0) / 1000.0;
  const int checked = identical + differ + randomByDesign + unreadable;
  std::printf("EVERY-PRESET NULL TEST: %d of %d checked in %.0f s (%.2f s each): %d bit-identical, %d different (worst %d steps), %d random by design (the engine differs from itself), %d unreadable%s\n",
              checked, (int) mine.size(), took, took / jmax(1, checked), identical, differ, worst, randomByDesign, unreadable,
              checked < (int) mine.size() ? (", " + String((int) mine.size() - checked) + " NOT REACHED (time budget)").toRawUTF8() : "");
  return differ == 0 ? 0 : 1;
}

int cmdDemo(const StringArray& a) {
  if (a.size() < 4) return 2;
  GhostyProcessor p;
  String err;
  if (!loadAny(p, a[1], err)) { std::printf("%s\n", err.toRawUTF8()); return 1; }
  const double bpm = a.size() > 4 ? a[4].getDoubleValue() : 140.0;
  p.getEngine()->setBpm((float) bpm);
  double seconds = 0;
  auto notes = phrase(a[3], bpm, seconds);
  auto buf = play(p, notes, seconds, 48000.0, 512);
  writeWav(File::getCurrentWorkingDirectory().getChildFile(a[2]), buf, 48000.0, 24);
  std::printf("%s: TP %.2f dBTP, RMS %.1f dBFS, clicks %d\n", p.currentPresetName().toRawUTF8(), truePeakDb(buf), rmsDb(buf), countClicks(buf));
  return 0;
}

// demo-morph <preset> <out.wav> <pattern> <bpm> <control=value,...>: ghost A = the preset, ghost B = the preset with
// those controls changed; the puck glides A -> B -> A over the phrase (the MORPH board doing what a hand would)
int cmdDemoMorph(const StringArray& a) {
  if (a.size() < 6) return 2;
  GhostyProcessor p;
  String err;
  if (!loadAny(p, a[1], err)) { std::printf("%s\n", err.toRawUTF8()); return 1; }
  const double bpm = a[4].getDoubleValue();
  p.getEngine()->setBpm((float) bpm);
  auto& m = p.morph();
  int k = m.board.add(0.15f, 0.5f);
  m.ghost_values[m.board.ghosts[(size_t) k].id] = m.captureCurrent();
  std::map<std::string, float> before;
  for (auto& kv : StringArray::fromTokens(a[5], ",", "")) {
    std::string c = kv.upToFirstOccurrenceOf("=", false, false).trim().toStdString();
    before[c] = p.getControl(c);
    p.setControl(c, kv.fromFirstOccurrenceOf("=", false, false).getFloatValue());
  }
  k = m.board.add(0.85f, 0.5f);
  m.ghost_values[m.board.ghosts[(size_t) k].id] = m.captureCurrent();
  for (auto& b : before) p.setControl(b.first, b.second);
  m.puck_x = 0.15f; m.puck_y = 0.5f;
  m.publish();
  m.enabled = true;
  double seconds = 0;
  auto notes = phrase(a[3], bpm, seconds);
  const double rate = 48000.0; const int block = 512;
  p.setPlayConfigDetails(0, 2, rate, block);
  p.prepareToPlay(rate, block);
  const int total = (int) (seconds * rate);
  AudioBuffer<float> out(2, total), buf(2, block);
  for (int pos = 0; pos < total; pos += block) {
    const int n = jmin(block, total - pos);
    const double t = (double) pos / total;   // 0 .. 1 over the phrase: A -> B -> A
    m.puck_x = (float) (0.15 + 0.7 * (0.5 - 0.5 * std::cos(2.0 * MathConstants<double>::pi * t)));
    buf.setSize(2, n, false, false, true); buf.clear();
    MidiBuffer midi;
    for (auto& nt : notes) {
      const int on = (int) (nt.start * rate), off = (int) ((nt.start + nt.length) * rate);
      if (on >= pos && on < pos + n) midi.addEvent(MidiMessage::noteOn(1, nt.note, (uint8) nt.velocity), on - pos);
      if (off >= pos && off < pos + n) midi.addEvent(MidiMessage::noteOff(1, nt.note), off - pos);
    }
    p.processBlock(buf, midi);
    for (int c = 0; c < 2; ++c) out.copyFrom(c, pos, buf, c, 0, n);
  }
  writeWav(File::getCurrentWorkingDirectory().getChildFile(a[2]), out, rate, 24);
  std::printf("morph demo: TP %.2f dBTP, RMS %.1f dBFS, clicks %d\n", truePeakDb(out), rmsDb(out), countClicks(out));
  return 0;
}

// demo-motion <preset> <out.wav> <pattern> <bpm> <motion 0..6> <division index | -hz>: three ghosts of the preset (its own
// knobs, then filter / wave position / detune / effects moved), the puck moved by MOTION, a host playing at <bpm>
int cmdDemoMotion(const StringArray& a) {
  if (a.size() < 7) return 2;
  GhostyProcessor p;
  String err;
  if (!loadAny(p, a[1], err)) { std::printf("%s\n", err.toRawUTF8()); return 1; }
  const double bpm = a[4].getDoubleValue();
  auto& m = p.morph();
  struct G { float x, y; std::vector<std::pair<std::string, float>> set; };
  const std::vector<G> ghosts {
    { 0.2f, 0.25f, {} },
    { 0.8f, 0.3f, { { "filter_1_cutoff", 118.0f }, { "osc_1_wave_frame", 220.0f }, { "osc_1_unison_detune", 6.0f }, { "chorus_on", 1.0f }, { "chorus_dry_wet", 0.5f } } },
    { 0.5f, 0.8f, { { "filter_1_cutoff", 52.0f }, { "filter_1_resonance", 0.7f }, { "osc_1_wave_frame", 120.0f }, { "distortion_on", 1.0f }, { "distortion_drive", 8.0f }, { "distortion_mix", 0.6f } } } };
  const auto base = m.captureCurrent();
  for (auto& gh : ghosts) {
    for (auto& s : gh.set) p.setControl(s.first, s.second);
    const int k = m.board.add(gh.x, gh.y);
    m.ghost_values[m.board.ghosts[(size_t) k].id] = m.captureCurrent();
    for (int s = 0; s < m.numSlots(); ++s) p.setControl(m.slotName(s), base[(size_t) s]);
  }
  const int mode = a[5].getIntValue();
  if (mode == Morph::kDrawn) {
    std::vector<Point<float>> pts; std::vector<double> secs;
    for (int i = 0; i < 48; ++i) { const float t = i / 48.0f; pts.push_back({ 0.5f + 0.36f * std::sin(6.283f * t), 0.52f + 0.3f * std::sin(12.566f * t + 0.6f) }); secs.push_back(t * (1.0 + 0.6 * std::sin(6.283 * t))); }
    m.setDrawnPath(spirit::OrbitPath::fromStroke(pts, secs, 1));
  }
  m.motion = mode;
  if (a[6].startsWithChar('-')) { m.sync = false; m.hz = (float) -a[6].getDoubleValue(); }
  else m.division = a[6].getIntValue();
  m.depth = 0.3f;
  m.puck_x = 0.5f; m.puck_y = 0.45f;
  m.publish();
  m.enabled = true;
  double seconds = 0;
  auto notes = phrase(a[3], bpm, seconds);
  HostClock clock; clock.bpm = bpm;
  p.getEngine()->setBpm((float) bpm);
  auto buf = play(p, notes, seconds, 48000.0, 512, false, &clock);
  writeWav(File::getCurrentWorkingDirectory().getChildFile(a[2]), buf, 48000.0, 24);
  std::printf("motion %s demo: TP %.2f dBTP, RMS %.1f dBFS, clicks %d\n", Morph::motionName(mode), truePeakDb(buf), rmsDb(buf), countClicks(buf));
  return 0;
}

// demo-gate <preset> <out.wav> <pattern> <bpm> <gate pattern 0..15>: GHOST GATE on (a host playing, so it follows the beat)
int cmdDemoGate(const StringArray& a) {
  if (a.size() < 6) return 2;
  GhostyProcessor p;
  String err;
  if (!loadAny(p, a[1], err)) { std::printf("%s\n", err.toRawUTF8()); return 1; }
  const double bpm = a[4].getDoubleValue();
  p.getEngine()->setBpm((float) bpm);
  p.extras().setGateOn(true);
  p.extras().setGatePattern(a[5].getIntValue());
  double seconds = 0;
  auto notes = phrase(a[3], bpm, seconds);   // (sets seconds: never in the same call as its use)
  auto buf = play(p, notes, seconds, 48000.0, 512);
  writeWav(File::getCurrentWorkingDirectory().getChildFile(a[2]), buf, 48000.0, 24);
  std::printf("gate demo: TP %.2f dBTP, RMS %.1f dBFS, clicks %d\n", truePeakDb(buf), rmsDb(buf), countClicks(buf));
  return 0;
}

// demo-arp <preset> <out.wav> <bpm>: KEYS demo, the host playing. One key per bar (A F C G, twice); SCALE LOCK A MINOR +
// CHORDS TRIADS turn each key into a chord and the ARP plays it: 1/16 UP over 2 octaves, then 1/16 T UP + DOWN.
int cmdDemoArp(const StringArray& a) {
  if (a.size() < 4) return 2;
  GhostyProcessor p;
  String err;
  if (!loadAny(p, a[1], err)) { std::printf("%s\n", err.toRawUTF8()); return 1; }
  const double bpm = a[3].getDoubleValue(), beat = 60.0 / bpm;
  p.getEngine()->setBpm((float) bpm);
  auto& k = p.keys();
  k.key = 9; k.scale = 2; k.chord = 1; k.arp = true; k.arpRate = 2; k.arpMode = 0; k.arpOctaves = 2; k.arpGate = 0.55f;
  std::vector<Note> notes;
  const int roots[] { 57, 53, 60, 55, 57, 53, 60, 55 };
  for (int bar = 0; bar < 8; ++bar) notes.push_back({ bar * 4 * beat, 4 * beat - 0.02, roots[bar], 100 });
  HostClock clock; clock.bpm = bpm;
  const double seconds = 8 * 4 * beat + 1.5;
  auto buf = play(p, notes, seconds, 48000.0, 512, false, &clock, [&](int pos) {
    if (pos >= (int) (4 * 4 * beat * 48000.0)) { k.arpRate = 5; k.arpMode = 2; }
  });
  writeWav(File::getCurrentWorkingDirectory().getChildFile(a[2]), buf, 48000.0, 24);
  std::printf("arp demo: %d arp steps, TP %.2f dBTP, RMS %.1f dBFS, clicks %d\n", k.arpCounter.load(), truePeakDb(buf), rmsDb(buf), countClicks(buf));
  return 0;
}

// demo-haunt <preset> <out.wav> <pattern> <bpm> <pad 0..15>: HAUNT on, the pad held during beats 2-3 and 6-7 (a host playing)
int cmdDemoHaunt(const StringArray& a) {
  if (a.size() < 6) return 2;
  GhostyProcessor p;
  String err;
  if (!loadAny(p, a[1], err)) { std::printf("%s\n", err.toRawUTF8()); return 1; }
  const double bpm = a[4].getDoubleValue();
  p.getEngine()->setBpm((float) bpm);
  p.extras().setHauntOn(true);
  const int pad = a[5].getIntValue();
  double seconds = 0;
  auto notes = phrase(a[3], bpm, seconds);
  HostClock clock; clock.bpm = bpm;
  auto buf = play(p, notes, seconds, 48000.0, 512, false, &clock, [&](int pos) {
    const double beat = pos / 48000.0 * bpm / 60.0;
    const bool hold = (beat >= 2.0 && beat < 3.0) || (beat >= 6.0 && beat < 7.0);
    p.extras().holdHauntPad(hold ? pad : -1);
  });
  writeWav(File::getCurrentWorkingDirectory().getChildFile(a[2]), buf, 48000.0, 24);
  std::printf("haunt demo: TP %.2f dBTP, RMS %.1f dBFS, clicks %d\n", truePeakDb(buf), rmsDb(buf), countClicks(buf));
  return 0;
}

int cmdLevels(const StringArray&) {
  const auto& list = factoryPresets();
  int bad = 0;
  for (int i = 0; i < (int) list.size(); ++i) {
    GhostyProcessor p;
    String err;
    p.loadFactoryPreset(i, err);
    const String cat = list[(size_t) i].category.toLowerCase();
    const String pat = cat == "bass" ? "bass" : cat == "lead" ? "lead" : cat == "pluck" ? "pluck" : (cat == "pad" || cat == "keys") ? "chords" : "fx";
    double secs = 0;
    auto notes = phrase(pat, 140.0, secs);
    auto buf = play(p, notes, secs, 48000.0, 512);
    const float tp = truePeakDb(buf), rms = rmsDb(buf);
    const int clicks = countClicks(buf);
    const bool ok = tp <= -1.0f && rms > -40.0f && finite(buf);
    bad += ok ? 0 : 1;
    std::printf("%s %3d %-28s %-8s TP %6.2f dBTP  RMS %6.1f dBFS  clicks %d\n", ok ? "ok  " : "FAIL", i, list[(size_t) i].name.toRawUTF8(),
                list[(size_t) i].category.toRawUTF8(), tp, rms, clicks);
  }
  return bad == 0 ? 0 : 1;
}

int cmdShots(const StringArray& a) {
  File out = File::getCurrentWorkingDirectory().getChildFile(a.size() > 1 ? a[1] : "shots");
  out.createDirectory();
  GhostyProcessor p;
  p.setPlayConfigDetails(0, 2, 44100, 512);
  p.prepareToPlay(44100, 512);
  String err;
  p.loadFactoryPreset(a.size() > 2 ? a[2].getIntValue() : 0, err);
  // a board to look at: three ghosts of this sound with different knobs, a drawn path, the puck following it
  {
    auto& m = p.morph();
    const float pos[3][2] { { 0.22f, 0.3f }, { 0.78f, 0.28f }, { 0.5f, 0.78f } };
    const float cut[3] { 60.0f, 115.0f, 85.0f };
    for (int k = 0; k < 3; ++k) {
      p.setControl("filter_1_cutoff", cut[k]);
      const int g = m.board.add(pos[k][0], pos[k][1]);
      m.ghost_values[m.board.ghosts[(size_t) g].id] = m.captureCurrent();
      m.ghost_names[m.board.ghosts[(size_t) g].id] = p.currentPresetName();
    }
    std::vector<Point<float>> pts; std::vector<double> secs;
    for (int i = 0; i < 48; ++i) { const float t = i / 48.0f; pts.push_back({ 0.5f + 0.33f * std::sin(6.283f * t), 0.52f + 0.28f * std::sin(12.566f * t + 0.6f) }); secs.push_back(t); }
    m.setDrawnPath(spirit::OrbitPath::fromStroke(pts, secs, 1));
    m.motion = Morph::kDrawn; m.division = 13; m.puck_x = 0.45f; m.puck_y = 0.5f;
    m.publish();
    m.enabled = true;
    AudioBuffer<float> b(2, 512); MidiBuffer mb;
    for (int k = 0; k < 40; ++k) p.processBlock(b, mb);   // let the puck move a little
  }
  std::unique_ptr<GhostyEditor> ed(dynamic_cast<GhostyEditor*>(p.createEditor()));
  // (SYNTH is Vital's OpenGL editor: a snapshot can't see it; use `window 0 <seconds>` and a screen grab for that one)
  const char* names[] { "synth", "morph", "extras" };
  for (int look = 0; look < kNumLooks; ++look) {
    ed->setLookIndex(look);
    for (int page = 1; page < GhostyEditor::kNumTabs; ++page) {
      ed->showPage(page);
      for (int k = 0; k < 6; ++k) MessageManager::getInstance()->runDispatchLoopUntil(30);
      Image img = ed->createComponentSnapshot(ed->getLocalBounds(), true, 1.0f);
      File f = out.getChildFile(String(names[page]) + "-" + lookName((Look) look) + ".png");
      f.deleteFile();
      FileOutputStream s(f);
      PNGImageFormat().writeImageToStream(img, s);
      std::printf("wrote %s\n", f.getFullPathName().toRawUTF8());
    }
  }
  ed->setLookIndex(0);
  ed = nullptr;
  return 0;
}

// the category's phrase + a loud test chord: the material a preset's level is fitted on
AudioBuffer<float> levelMaterial(GhostyProcessor& p, const String& category) {
  const String cat = category.toLowerCase();
  const String pat = cat == "bass" ? "bass" : cat == "lead" ? "lead" : cat == "pluck" ? "pluck" : (cat == "pad" || cat == "keys") ? "chords" : "fx";
  double secs = 0;
  auto notes = phrase(pat, 140.0, secs);
  // plus a loud 4-note chord at the top velocity (the worst case for summed peaks)
  const double t0 = secs;
  for (int k : { 48, 55, 60, 64 }) notes.push_back({ t0, 2.0, k, 127 });
  return play(p, notes, t0 + 4.0, 48000.0, 512);
}

// fit-level <in.vital> <out.vital> <target dBTP>: sets the preset's own "volume" control (Vital's master volume knob,
// within its range) so that its true peak on its phrase + a loud chord lands just under the target. The level lives in
// the preset itself; Ghosty Pro has no limiter of its own.
int cmdFitLevel(const StringArray& a) {
  if (a.size() < 4) return 2;
  File in = File::getCurrentWorkingDirectory().getChildFile(a[1]), out = File::getCurrentWorkingDirectory().getChildFile(a[2]);
  const float target = (float) a[3].getDoubleValue();
  String text = in.loadFileAsString(), category;
  try { json j = json::parse(text.toStdString()); if (j.count("preset_style")) category = String(j["preset_style"].get<std::string>()); } catch (...) { }
  // the worst of three instances: each Vital instance starts its oscillators at different random phases, so the
  // summed peak of a chord moves by up to a dB from instance to instance
  auto measure = [&](float vol, float& rms) {
    float worst = -200.0f;
    for (int k = 0; k < 3; ++k) {
      GhostyProcessor p; String err;
      p.loadPresetText(text, in.getFileNameWithoutExtension(), err);
      p.setControl("volume", vol);
      auto b = levelMaterial(p, category);
      rms = rmsDb(b);
      worst = jmax(worst, truePeakDb(b));
    }
    return worst;
  };
  float rms = 0;
  GhostyProcessor p0; String err;
  p0.loadPresetText(text, in.getFileNameWithoutExtension(), err);
  // Vital's volume knob reads sqrt(value) - 80 dB, so a change of d dB is sqrt(value) + d
  const float maxVol = vital::Parameters::getDetails("volume").max;
  auto withDb = [maxVol](float vol, float db) { const float r = jlimit(0.0f, std::sqrt(maxVol), std::sqrt(jmax(0.0f, vol)) + db); return r * r; };
  float best = p0.getControl("volume");
  float tp = measure(best, rms);
  for (int it = 0; it < 4 && (tp > target || tp < target - 0.6f); ++it) {
    const float next = withDb(best, target - 0.25f - tp);
    if (next == best) break;
    best = next;
    tp = measure(best, rms);
  }
  GhostyProcessor p; p.loadPresetText(text, in.getFileNameWithoutExtension(), err);
  p.setControl("volume", best);
  json j = json::parse(p.presetText().toStdString());
  // keep the preset's own metadata
  try { json orig = json::parse(text.toStdString()); for (auto k : { "author", "comments", "preset_style", "preset_name", "macro1", "macro2", "macro3", "macro4" }) if (orig.count(k)) j[k] = orig[k]; } catch (...) { }
  out.replaceWithText(String(j.dump()));
  std::printf("%-30s volume %.1f  TP %.2f dBTP  RMS %.1f dBFS\n", in.getFileNameWithoutExtension().toRawUTF8(), best, tp, rms);
  return tp <= target + 0.01f ? 0 : 1;
}

int cmdInitJson(const StringArray& a) {
  GhostyProcessor p;
  p.loadInit();
  File f = File::getCurrentWorkingDirectory().getChildFile(a.size() > 1 ? a[1] : "init.vital");
  f.replaceWithText(p.presetText());
  return 0;
}

// window <page> <seconds> [preset]: the editor in a real window on the screen (for screen grabs of the OpenGL ENGINE page)
int cmdWindow(const StringArray& a) {
  // GHOSTY_SCALE=2: a 2x (Retina-like) display, as JUCE sees one (every display's scale = 2)
  const double scale = SystemStats::getEnvironmentVariable("GHOSTY_SCALE", "1").getDoubleValue();
  if (scale > 0.0 && scale != 1.0) Desktop::getInstance().setGlobalScaleFactor((float) scale);
  GhostyProcessor p;
  p.setPlayConfigDetails(0, 2, 44100, 512);
  p.prepareToPlay(44100, 512);
  String err;
  if (a.size() > 3) loadAny(p, a[3], err);
  std::unique_ptr<GhostyEditor> ed(dynamic_cast<GhostyEditor*>(p.createEditor()));
  ed->setTopLeftPosition(0, 0);
  ed->addToDesktop(ComponentPeer::windowHasDropShadow);
  ed->setVisible(true);
  if (a.size() > 4) ed->setLookIndex(a[4].getIntValue());
  {   // GHOSTY_SIZE=WxH: a host-chosen window size
    auto sz = StringArray::fromTokens(SystemStats::getEnvironmentVariable("GHOSTY_SIZE", ""), "x", "");
    if (sz.size() == 2) ed->setSize(sz[0].getIntValue(), sz[1].getIntValue());
  }
  if (a.contains("keys")) {   // KEYS switched on for the pictures
    auto& k = p.keys();
    k.arp = true; k.chord = 1; k.key = 9; k.scale = 2; k.arpOctaves = 2; k.arpRate = 2;
  }
  ed->showPage(a.size() > 1 ? a[1].getIntValue() : 0);
  {   // GHOSTY_MENU=preset|main|search: that menu open for the picture; GHOSTY_TYPE=text: typed into the search
    const String which = SystemStats::getEnvironmentVariable("GHOSTY_MENU", "");
    if (which.isNotEmpty()) {
      MessageManager::getInstance()->runDispatchLoopUntil(2500);   // (the preset scan finishes)
      std::printf("opening menu %s (%d presets indexed)\n", which.toRawUTF8(), (int) ed->menuIndex().flat.size()); std::fflush(stdout);
      ed->openMenu(which == "preset" ? 0 : which == "main" ? 1 : 2);
    }
  }
  const int vtab = a.contains("effects") ? 1 : SystemStats::getEnvironmentVariable("GHOSTY_VTAB", "-1").getIntValue();   // GHOSTY_VTAB=n: that tab of the synth page
  if (vtab >= 0) {
    MessageManager::getInstance()->runDispatchLoopUntil(1500);
    std::function<FullInterface*(Component*)> find = [&](Component* c) -> FullInterface* {
      if (auto* f = dynamic_cast<FullInterface*>(c)) return f;
      for (auto* ch : c->getChildren()) if (auto* f = find(ch)) return f;
      return nullptr;
    };
    if (auto* f = find(ed.get())) f->tabSelected(vtab);
  }
  MessageManager::getInstance()->runDispatchLoopUntil((int) (1000 * (a.size() > 2 ? a[2].getDoubleValue() : 6.0)));
  ed->removeFromDesktop();
  ed = nullptr;
  return 0;
}


// scanprobe [secs]: DIAGNOSTIC - a real editor on the screen (as in a host), then once a second: is the preset finder still
// searching, and how many presets has it found
int cmdScanProbe(const StringArray& a) {
  GhostyProcessor p;
  p.setPlayConfigDetails(0, 2, 44100, 512);
  p.prepareToPlay(44100, 512);
  std::unique_ptr<GhostyEditor> ed(dynamic_cast<GhostyEditor*>(p.createEditor()));
  ed->setTopLeftPosition(0, 0);
  ed->addToDesktop(ComponentPeer::windowHasDropShadow);
  ed->setVisible(true);
  const int secs = a.size() > 1 ? a[1].getIntValue() : 90;
  const double t0 = Time::getMillisecondCounterHiRes();
  for (int i = 0; i < secs; ++i) {
    MessageManager::getInstance()->runDispatchLoopUntil(1000);
    const bool sc = ed->finder().scanning();
    std::printf("t=%5.1f s  scanning=%d  found=%d\n", (Time::getMillisecondCounterHiRes() - t0) / 1000.0, sc ? 1 : 0, (int) ed->finder().results().size());
    std::fflush(stdout);
    if (!sc) { std::printf("indexed %d\n", (int) ed->menuIndex().flat.size()); break; }
  }
  ed->removeFromDesktop();
  ed = nullptr;
  return 0;
}

// flick <look> <secs>: the editor in a real window with scripted GHOST ORBIT flicks on Vital's knobs and our MORPH knobs,
// for recording (ffmpeg x11grab). Prints each knob's screen bounds. Synthetic mouse events, real time, real physics.
SynthSlider* findSlider(Component* c, const String& name) {
  if (auto* s = dynamic_cast<SynthSlider*>(c))
    if (s->getName() == name && s->isRotary() && s->getWidth() > 0) return s;
  for (auto* child : c->getChildren())
    if (auto* f = findSlider(child, name)) return f;
  return nullptr;
}

MouseEvent toolMouse(Component* c, Point<float> pos, Point<float> down, ModifierKeys mods = ModifierKeys(ModifierKeys::leftButtonModifier)) {
  return MouseEvent(Desktop::getInstance().getMainMouseSource(), pos, mods, MouseInputSource::invalidPressure,
                    MouseInputSource::invalidOrientation, MouseInputSource::invalidRotation, MouseInputSource::invalidTiltX,
                    MouseInputSource::invalidTiltY, c, c, Time::getCurrentTime(), down, Time::getCurrentTime(), 1, pos != down);
}

void pump(int ms) { MessageManager::getInstance()->runDispatchLoopUntil(ms); }

// grab, hold a moment (the lift), drag `frames` frames at `px` per frame, let go, watch it settle
void scriptedFlick(Component& k, float px, int frames, int holdMs, int settleMs) {
  const Point<float> c(k.getWidth() * 0.5f, k.getHeight() * 0.5f);
  k.mouseDown(toolMouse(&k, c, c));
  pump(holdMs);
  for (int i = 1; i <= frames; ++i) {
    k.mouseDrag(toolMouse(&k, c.translated(0, -px * i), c));
    pump(16);
  }
  k.mouseUp(toolMouse(&k, c.translated(0, -px * frames), c));
  pump(settleMs);
}

int cmdFlick(const StringArray& a) {
  GhostyProcessor p;
  p.setPlayConfigDetails(0, 2, 44100, 512);
  p.prepareToPlay(44100, 512);
  String err;
  p.loadFactoryPreset(0, err);
  std::unique_ptr<GhostyEditor> ed(dynamic_cast<GhostyEditor*>(p.createEditor()));
  ed->setTopLeftPosition(0, 0);
  ed->addToDesktop(ComponentPeer::windowHasDropShadow);
  ed->setVisible(true);
  if (a.size() > 1) ed->setLookIndex(a[1].getIntValue());
  ed->showPage(0);
  pump(2500);
  for (const char* n : { "filter_1_drive", "osc_1_unison_voices", "osc_1_level", "env_1_attack" })
    if (auto* s = findSlider(ed.get(), n)) {
      auto b = s->getScreenBounds();
      std::printf("knob %s %d %d %d %d\n", n, b.getX(), b.getY(), b.getWidth(), b.getHeight());
    }
  std::fflush(stdout);
  pump(500);
  if (auto* s = findSlider(ed.get(), "filter_1_drive")) {
    s->setValue(s->getMinimum(), sendNotificationSync); pump(400);
    scriptedFlick(*s, 9.0f, 6, 600, 2000);      // flick up: spins on and settles
    scriptedFlick(*s, -10.0f, 5, 300, 2000);    // flick back down
  }
  if (auto* s = findSlider(ed.get(), "osc_1_unison_voices")) {
    scriptedFlick(*s, 5.0f, 6, 300, 1500);      // stepped: magnetic detents
    scriptedFlick(*s, -5.0f, 5, 200, 1500);
  }
  // our MORPH knobs
  ed->showPage(1);
  pump(800);
  std::function<Component*(Component*)> firstOrbit = [&](Component* c) -> Component* {
    if (dynamic_cast<ghostorbit::OrbitKnob*>(c) && c->isShowing() && c->isEnabled()) return c;
    for (auto* ch : c->getChildren()) if (auto* f = firstOrbit(ch)) return f;
    return nullptr;
  };
  if (auto* k = firstOrbit(ed.get())) {
    auto b = k->getScreenBounds();
    std::printf("knob morph %d %d %d %d\n", b.getX(), b.getY(), b.getWidth(), b.getHeight());
    std::fflush(stdout);
    scriptedFlick(*k, 8.0f, 6, 600, 2000);
    scriptedFlick(*k, -9.0f, 5, 300, 2000);
  }
  pump((int) (1000 * (a.size() > 2 ? a[2].getDoubleValue() : 1.0)));
  ed->removeFromDesktop();
  ed = nullptr;
  return 0;
}

// layout <scale> <w> <h> [fl]: the editor in a window on a <scale>x display at <w> x <h> (fl: FL-like resizes first);
// prints Vital's area and its sections (in window units) for the layout test
int cmdLayout(const StringArray& a) {
  if (a.size() < 4) return 2;
  const float scale = a[1].getFloatValue();
  std::setvbuf(stdout, nullptr, _IOLBF, 0);
  Desktop::getInstance().setGlobalScaleFactor(scale);
  GhostyProcessor p;
  p.setPlayConfigDetails(0, 2, 44100, 512);
  p.prepareToPlay(44100, 512);
  String err;
  p.loadFactoryPreset(0, err);
  const int w = a[2].getIntValue(), h = a[3].getIntValue();
  std::unique_ptr<GhostyEditor> ed(dynamic_cast<GhostyEditor*>(p.createEditor()));
  ed->setTopLeftPosition(0, 0);
  ed->addToDesktop(ComponentPeer::windowHasDropShadow);
  ed->setVisible(true);
  ed->setLookIndex(0);
  ed->showPage(0);
  pump(400);
  if (a.contains("fl"))
    for (auto sz : { Point<int>(w / 2, h / 2), Point<int>(w + 300, h), Point<int>(w, h + 200) }) { ed->setSize(sz.x, sz.y); pump(80); }
  ed->setSize(w, h);
  pump(600);
  if (auto* gui = dynamic_cast<FullInterface*>(ed->vitalEditor())) {
    const auto hb = ed->vitalBounds();
    const float ds = gui->getPixelScaling();
    std::printf("holder %d %d %d %d\nscale %g\n", hb.getX(), hb.getY(), hb.getWidth(), hb.getHeight(), ds);
    for (auto* c : gui->getChildren())
      if (c->isVisible() && c->getWidth() > 0 && c->getHeight() > 0 && dynamic_cast<SynthSection*>(c) != nullptr) {
        const auto r = c->getBounds().toFloat() / ds;
        std::printf("section %.1f %.1f %.1f %.1f %s\n", r.getX(), r.getY(), r.getWidth(), r.getHeight(), c->getName().toRawUTF8());
      }
  }
  if (a.size() > 5 && a[5].endsWith(".png")) {   // a picture too (the window as drawn)
    pump(800);
  }
  std::printf("done\n");
  std::fflush(stdout);
  // the answer is out: leave at once (an OpenGL teardown on a CI virtual Mac can hang; nothing here needs saving)
  std::_Exit(0);
}


// hit <scale> <w> <h> [click]: a real mouse path (ComponentPeer::handleMouseEvent -> JUCE's mouse dispatch, as the host's
// window delivers it) onto the centre of every Vital control, where Vital DRAWS it (OpenGlComponent::setViewPort's maths).
// Every control must get the click, with the click at its own centre. `click`: the on/off buttons must also toggle their
// engine parameter. Prints the power buttons and every miss; exit 1 if any control is missed.
struct HitListener : public MouseListener {
  Component* down = nullptr;
  Point<float> pos;
  void mouseDown(const MouseEvent& e) override { down = e.eventComponent; pos = e.position; }
};

int cmdHit(const StringArray& a) {
  if (a.size() < 4) return 2;
  std::setvbuf(stdout, nullptr, _IOLBF, 0);
  // "2": every display reports scale 2 (a Retina Mac); "g2": the window itself scaled 2x (Desktop::setGlobalScaleFactor)
  const bool globalScale = a[1].startsWithIgnoreCase("g");
  const float scale = a[1].trimCharactersAtStart("gG").getFloatValue();
  if (globalScale) Desktop::getInstance().setGlobalScaleFactor(scale);
  std::unique_ptr<GhostyScaleKeeper> keeper;
  if (!globalScale && scale > 0.0f) keeper = std::make_unique<GhostyScaleKeeper>(scale);
  GhostyProcessor p;
  p.setPlayConfigDetails(0, 2, 44100, 512);
  p.prepareToPlay(44100, 512);
  String err;
  p.loadFactoryPreset(0, err);
  std::unique_ptr<GhostyEditor> ed(dynamic_cast<GhostyEditor*>(p.createEditor()));
  ed->setTopLeftPosition(0, 0);
  ed->addToDesktop(ComponentPeer::windowHasDropShadow);
  ed->setVisible(true);
  ed->setLookIndex(0);
  ed->showPage(0);
  // on a real Mac desktop (Brandon's Mac runs the tests while his own apps are open): our test window must be the front
  // window, or macOS says another window is under the mouse and JUCE (rightly) gives the click to nobody
  // (Mac only: there it is a window level; on Linux JUCE would re-create the window under Vital's OpenGL context)
 #if JUCE_MAC
  ed->setAlwaysOnTop(true);
  Process::makeForegroundProcess();
  ed->toFront(true);
 #endif
  pump(400);
  if (!globalScale) ghostyPretendDisplayScale(scale);   // (the X server re-reads the displays when a window changes: say it again)
  ed->setSize(a[2].getIntValue(), a[3].getIntValue());
  pump(300);
  if (!globalScale) ghostyPretendDisplayScale(scale);
  if (auto* g = dynamic_cast<FullInterface*>(ed->vitalEditor())) g->checkShouldReposition(true);
  pump(600);
  auto* gui = dynamic_cast<FullInterface*>(ed->vitalEditor());
  auto* peer = ed->getPeer();
  if (!gui || !peer) return 1;
  // is our window really the one under the mouse there? (no: another app's window, a locked or sleeping screen covers it)
  {
    const auto probe = ed->getLocalPoint(ed->vitalHolder(), ed->vitalHolder()->getLocalBounds().getCentre());
    if (!ed->contains(probe))
      std::printf("COVERED: the desktop puts another window over the test window at %d,%d (the click would go to that window)\n", probe.x, probe.y);
  }
  HitListener listener;
  ed->addMouseListener(&listener, true);
  const bool clicks = a.contains("click");
  auto mouse = [&](Point<float> at, bool downNow) {
    peer->handleMouseEvent(MouseInputSource::InputSourceType::mouse, at,
                           downNow ? ModifierKeys(ModifierKeys::leftButtonModifier) : ModifierKeys(), MouseInputSource::invalidPressure,
                           MouseInputSource::invalidOrientation, Time::currentTimeMillis());
  };
  int n = 0, bad = 0, toggled = 0, toggleBad = 0;
  std::function<void(Component*)> walk = [&](Component* c) {
    for (auto* ch : c->getChildren()) {
      const bool isButton = dynamic_cast<SynthButton*>(ch) != nullptr;
      const bool target = isButton || dynamic_cast<SynthSlider*>(ch) != nullptr;
      if (target && ch->isShowing() && ch->getWidth() > 4 && ch->getHeight() > 4) {
        const Point<float> drawn = ghostyDrawnCentre(*ed, *gui, *ch);
        const Point<float> want = ch->getLocalBounds().toFloat().getCentre();
        listener.down = nullptr;
        mouse(drawn, false);
        mouse(drawn, true);
        Component* got = listener.down;
        const Point<float> pos = listener.pos;
        const bool isOn = isButton && ch->getName().endsWith("_on");
        const bool before = isOn ? static_cast<SynthButton*>(ch)->getToggleState() : false;
        mouse(drawn + Point<float>(1.0f, 0.0f), true);   // a hand never holds the mouse perfectly still: a 1 px drag before the release
        mouse(drawn + Point<float>(1.0f, 0.0f), false);
        // the component that got it: the control itself or something drawn inside it
        const bool ok = got != nullptr && (got == ch || ch->isParentOf(got)) && std::abs(pos.x - want.x) <= 3.0f && std::abs(pos.y - want.y) <= 3.0f
                        && (got != ch || true);
        ++n; if (!ok) ++bad;
        if (!ok) std::printf("MISS %-24s drawn %.0f,%.0f  got %s at %.1f,%.1f  wanted %.1f,%.1f\n", ch->getName().toRawUTF8(), drawn.x, drawn.y,
                             got ? got->getName().toRawUTF8() : "-", pos.x, pos.y, want.x, want.y);
        if (isOn && clicks) {
          pump(50);
          const bool after = static_cast<SynthButton*>(ch)->getToggleState();
          const float param = p.getControl(ch->getName().toRawUTF8());
          ++toggled;
          const bool flipped = after != before && (param >= 0.5f) == after;
          if (!flipped) ++toggleBad;
          std::printf("%s %-12s was %d now %d  engine %g\n", flipped ? "ok  " : "STUCK", ch->getName().toRawUTF8(), (int) before, (int) after, param);
          // and back (leave the preset as it was)
          mouse(drawn, false); mouse(drawn, true); mouse(drawn + Point<float>(1.0f, 0.0f), true); mouse(drawn + Point<float>(1.0f, 0.0f), false); pump(50);
        }
      }
      walk(ch);
    }
  };
  walk(gui);
  std::printf("gui %d x %d; holder at %d,%d; Vital display scale %g\n", gui->getWidth(), gui->getHeight(), ed->vitalBounds().getX(), ed->vitalBounds().getY(), gui->getPixelScaling());
  std::printf("controls %d  missed %d  on/off buttons %d  stuck %d\n", n, bad, toggled, toggleBad);
  std::fflush(stdout);
  std::_Exit(bad || toggleBad ? 1 : 0);
}

// icon <out.png> <size>: Ghosty Pro's app icon (GhostyProLogo.h drawIcon); header <out dir>: the header in all four looks
int cmdIcon(const StringArray& a) {
  const int size = a.size() > 2 ? a[2].getIntValue() : 1024;
  Image img(Image::ARGB, size, size, true);
  { Graphics g(img); ghosty::prologo::drawIcon(g, Rectangle<float>(0, 0, (float) size, (float) size)); }
  File f = File::getCurrentWorkingDirectory().getChildFile(a[1]);
  f.deleteFile();
  FileOutputStream s(f);
  return PNGImageFormat().writeImageToStream(img, s) ? 0 : 1;
}

// types: how the presets on this computer sort into the picker's TYPES (counts, and a few OTHER names to check the words)
int cmdTypes(const StringArray&) {
  Array<File> roots = VitalPresetFinder::vitalFolders();
  const File data = VitalPresetFinder::dataFolder();
  if (data != File() && data.isDirectory() && !roots.contains(data)) roots.add(data);
  roots.addArray(VitalPresetFinder::addedFolders());
  const auto found = VitalPresetFinder::scanNow(roots);
  PresetCatalog cat;
  std::set<std::string> favs;
  try { favs = LoadSave::getFavorites(); } catch (...) { }
  cat.build(factoryPresets(), found, roots, data == File() ? File() : data.getChildFile("User"), favs);
  int sum = 0;
  String line = "preset types:";
  for (int t = 0; t < kNumPresetTypes; ++t) { sum += (int) cat.byType[t].size(); line << " " << presetTypeName(t) << " " << (int) cat.byType[t].size() << ","; }
  line << " FAVOURITES " << (int) cat.favourites.size() << "; total " << cat.total() << (sum == cat.total() ? " (adds up)" : " (DOES NOT ADD UP)");
  std::printf("%s\n", line.toRawUTF8());
  int shown = 0;
  for (int i : cat.byType[kOther]) { if (++shown > 15) break; std::printf("  OTHER: %s  (%s)\n", cat.entries[(size_t) i].name.toRawUTF8(), cat.entries[(size_t) i].where.toRawUTF8()); }
  return sum == cat.total() ? 0 : 1;
}

int cmdDumpParams(const StringArray&) {
  for (int i = 0; i < vital::Parameters::getNumParameters(); ++i) {
    auto* d = vital::Parameters::getDetails(i);
    std::printf("%s\t%g\t%g\t%g\t%d\n", d->name.c_str(), d->min, d->max, d->default_value, (int) d->value_scale);
  }
  return 0;
}

} // namespace

int main(int argc, char* argv[]) {
  // a crash prints where it happened (the Mac CI log shows it: no debugger needed on Brandon's Mac)
  SystemStats::setApplicationCrashHandler([](void*) {
    std::fprintf(stdout, "\nCRASH (signal) - stack:\n%s\n", SystemStats::getStackBacktrace().toRawUTF8());
    std::fflush(stdout);
    std::_Exit(139);
  });
  ScopedJuceInitialiser_GUI gui;
  StringArray a;
  for (int i = 1; i < argc; ++i) a.add(String::fromUTF8(argv[i]));   // (UTF-8: accented file names like Arômes)
  if (a.isEmpty()) { std::printf("usage: see ToolMain.cpp\n"); return 2; }
  int rc = 2;
  const String c = a[0];
  if (c == "test") rc = runGhostyTests(a);
  else if (c == "render") rc = cmdRender(a);
  else if (c == "nulltest") rc = cmdNullTest(a);
  else if (c == "demo") rc = cmdDemo(a);
  else if (c == "levels") rc = cmdLevels(a);
  else if (c == "shots") rc = cmdShots(a);
  else if (c == "dump-params") rc = cmdDumpParams(a);
  else if (c == "types") rc = cmdTypes(a);
  else if (c == "scanprobe") rc = cmdScanProbe(a);
  else if (c == "preset-folders") rc = cmdPresetFolders(a);
  else if (c == "nulltest-all") rc = cmdNullTestAll(a);
  else if (c == "fit-level") rc = cmdFitLevel(a);
  else if (c == "init-json") rc = cmdInitJson(a);
  else if (c == "window") rc = cmdWindow(a);
  else if (c == "flick") rc = cmdFlick(a);
  else if (c == "layout") rc = cmdLayout(a);
  else if (c == "hit") rc = cmdHit(a);
  else if (c == "demo-morph") rc = cmdDemoMorph(a);
  else if (c == "demo-gate") rc = cmdDemoGate(a);
  else if (c == "demo-motion") rc = cmdDemoMotion(a);
  else if (c == "demo-haunt") rc = cmdDemoHaunt(a);
  else if (c == "demo-arp") rc = cmdDemoArp(a);
  else if (c == "icon") rc = cmdIcon(a);
  DeletedAtShutdown::deleteAll();
  return rc;
}
