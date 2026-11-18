// Poltergeist BASS test harness.
//   PoltergeistBassTests ref DIR    -> renders every factory preset, Spectrum-style engine snaps, an old saved project and the
//                                      sample-based old projects (SAMPLE / granular / wavetable / slice / CHOP / Lab keys / PROCESS LAB)
//   PoltergeistBassTests bounds FILE-> the layout of the kept pages (the old build's goes in DIR/layout.txt for the compare)
//                                      (run with the build from BEFORE a change: the reference)
//   PoltergeistBassTests [DIR]      -> every check; with DIR, also compares against the reference renders in DIR
//   PoltergeistBassTests shots DIR  -> PNG screenshots (needs a display / Xvfb)
//   PoltergeistBassTests demo DIR   -> WAV demos of the new bass presets
//   PoltergeistBassTests tune       -> prints the per-preset level trims (BassTrim.h)
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ProcessLab.h"
#include "WaveRegion.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <new>
#include <thread>

static std::atomic<long> gAllocs { 0 };
static thread_local bool gCount = false;
void* operator new(std::size_t n) { if (gCount) ++gAllocs; if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[](std::size_t n) { if (gCount) ++gAllocs; if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
// the nothrow forms too (std::stable_sort's temporary buffer uses them): every new here pairs with the free() below
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { if (gCount) ++gAllocs; return std::malloc(n ? n : 1); }
void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { if (gCount) ++gAllocs; return std::malloc(n ? n : 1); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

static int failures = 0;
static void check(bool ok, const juce::String& what) { std::printf("%s  %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8()); std::fflush(stdout); if (!ok) ++failures; }

struct Ev { double t; int note; float vel; };   // vel 0 = note off
using Stereo = std::vector<float>;              // L block then R block

// the reference phrase: a chord, a melody note, a low note, overlapping notes and releases
static const std::vector<Ev>& legacyPhrase() {
    static const std::vector<Ev> e { { 0.0, 48, 0.9f }, { 0.0, 55, 0.7f }, { 0.5, 48, 0 }, { 0.5, 55, 0 }, { 0.6, 60, 0.8f }, { 1.0, 64, 0.6f }, { 1.6, 60, 0 },
                                     { 1.7, 64, 0 }, { 2.0, 36, 1.0f }, { 2.4, 67, 0.5f }, { 2.6, 67, 0 }, { 3.2, 36, 0 } };
    return e;
}

static Stereo renderProc(PoltergeistProcessor& p, double sr, const std::vector<Ev>& evs, double secs, int block = 512) {
    p.setRateAndBufferSizeDetails(sr, block); p.prepareToPlay(sr, block);
    const int n = (int) (secs * sr); Stereo out((size_t) n * 2, 0.0f);
    juce::AudioBuffer<float> buf(2, block); juce::MidiBuffer midi;
    for (int pos = 0; pos < n; pos += block) {
        midi.clear();
        for (auto& e : evs) { const int at = (int) std::llround(e.t * sr) - pos; if (at >= 0 && at < block) midi.addEvent(e.vel > 0 ? juce::MidiMessage::noteOn(1, e.note, e.vel) : juce::MidiMessage::noteOff(1, e.note), at); }
        buf.clear(); p.processBlock(buf, midi);
        for (int i = 0; i < block && pos + i < n; ++i) { out[(size_t) (pos + i)] = buf.getSample(0, i); out[(size_t) (n + pos + i)] = buf.getSample(1, i); }
    }
    return out;
}
static Stereo renderEngine(const gs::Snap& snap, double sr, const std::vector<Ev>& evs, double secs, int block = 512) {
    auto e = std::make_unique<gs::Engine>(); e->prepare(sr, block);
    const int n = (int) (secs * sr); Stereo out((size_t) n * 2, 0.0f);
    juce::AudioBuffer<float> buf(2, block); juce::MidiBuffer midi;
    for (int pos = 0; pos < n; pos += block) {
        midi.clear();
        for (auto& ev : evs) { const int at = (int) std::llround(ev.t * sr) - pos; if (at >= 0 && at < block) midi.addEvent(ev.vel > 0 ? juce::MidiMessage::noteOn(1, ev.note, ev.vel) : juce::MidiMessage::noteOff(1, ev.note), at); }
        e->process(buf, midi, snap);
        for (int i = 0; i < block && pos + i < n; ++i) { out[(size_t) (pos + i)] = buf.getSample(0, i); out[(size_t) (n + pos + i)] = buf.getSample(1, i); }
    }
    return out;
}
static bool writeRaw(const juce::File& f, const Stereo& s) { f.deleteFile(); return f.replaceWithData(s.data(), s.size() * sizeof(float)); }
static Stereo readRaw(const juce::File& f) { juce::MemoryBlock mb; if (!f.loadFileAsData(mb)) return {}; Stereo s(mb.getSize() / sizeof(float)); std::memcpy(s.data(), mb.getData(), s.size() * sizeof(float)); return s; }

static const std::vector<std::pair<const char*, uint32_t>>& specWords() {
    static const std::vector<std::pair<const char*, uint32_t>> w { { "sub", 100 }, { "bass dark", 101 }, { "keys bright", 102 }, { "pad warm wide", 103 }, { "lead dirty", 105 },
        { "growl heavy dirty", 13 }, { "wobble haunted", 9 }, { "808 slide hard", 23 }, { "reese dark", 4 }, { "bubbly pluck", 113 } };
    return w;
}
static void setP(PoltergeistProcessor& p, const char* id, float v) { if (auto* pr = p.apvts.getParameter(id)) pr->setValueNotifyingHost(pr->convertTo0to1(v)); }

// ---- sample-based OLD PROJECTS (owner 2026-10-26: the SAMPLE / SAMPLE LAB / PROCESS LAB / CHOP pages left Poltergeist;
// projects and sounds that use samples must still load and play bit-identically). `ref` writes each one's state + render with
// the build from before the change; the new build loads the same state bytes and must render exactly the same samples.
static std::shared_ptr<gs::SampleData> refGrowl(double secs, double f0, uint32_t seed) {   // a deterministic "monster growl / vocal"
    auto s = std::make_shared<gs::SampleData>(); s->rate = 44100; s->name = "ref growl " + juce::String((int) seed); const int n = (int) (secs * s->rate); s->data.resize((size_t) n);
    juce::Random r((juce::int64) seed); double ph = 0;
    for (int i = 0; i < n; ++i) {
        const double t = i / s->rate, f = f0 * (1.0 + 0.03 * std::sin(2 * juce::MathConstants<double>::pi * 5.5 * t));
        ph += f / s->rate; double x = 0; for (int h = 1; h <= 12; ++h) x += std::sin(2 * juce::MathConstants<double>::pi * h * ph) * (1.0 / h) * (h == 3 || h == 4 ? 2.2 : 1.0);
        const double env = juce::jmin(1.0, t / 0.02) * (0.6 + 0.4 * std::abs(std::sin(t * 7.0)));
        s->data[(size_t) i] = (float) (0.3 * env * x + 0.02 * (r.nextFloat() * 2 - 1));
    }
    s->buildFrames(); return s;
}
struct SampleRefCase { const char* name; std::function<void(PoltergeistProcessor&)> make; };
static std::vector<SampleRefCase> sampleRefCases() {
    auto sp = [](PoltergeistProcessor& p, const char* id, float v) { setP(p, id, v); };
    return {
        { "classic", [sp](PoltergeistProcessor& p) { p.loadPreset(6); p.setSampleGhost(refGrowl(1.6, 110, 1)); sp(p, "smp_level", 0.7f); sp(p, "smp_fm", 0.45f); sp(p, "smp_loop", 1);
                                                     sp(p, "smp_start", 0.05f); sp(p, "smp_lst", 0.3f); sp(p, "smp_lend", 0.8f); sp(p, "smp_pitch", 3); } },
        { "classic-vintage", [sp](PoltergeistProcessor& p) { p.loadPreset(2); p.setSampleGhost(refGrowl(1.2, 147, 2)); sp(p, "smp_level", 0.8f); sp(p, "smp_vc", 2); sp(p, "smp_vc_amt", 0.8f); p.applyCharacter(); } },
        { "granular", [sp](PoltergeistProcessor& p) { p.loadPreset(13); p.setSampleGhost(refGrowl(2.0, 196, 3)); sp(p, "smp_mode", 1); sp(p, "smp_level", 0.8f); sp(p, "smp_pos", 0.4f); sp(p, "smp_spray", 0.5f); } },
        { "wavetable", [sp](PoltergeistProcessor& p) { p.loadPreset(7); p.setSampleGhost(refGrowl(1.5, 98, 4)); sp(p, "smp_mode", 2); sp(p, "smp_level", 0.7f); sp(p, "smp_pos", 0.3f); sp(p, "smp_scan", 0.6f); sp(p, "smp_fm", 0.2f); } },
        { "slice", [sp](PoltergeistProcessor& p) { p.loadPreset(0); p.setSampleGhost(refGrowl(2.0, 220, 5)); sp(p, "smp_mode", 3); sp(p, "smp_level", 0.9f); sp(p, "smp_slices", 1); } },
        { "chop", [sp](PoltergeistProcessor& p) { p.loadPreset(0); auto s = refGrowl(2.4, 165, 6); for (int k = 1; k < 6; ++k) for (int i = 0; i < 2205; ++i) s->data[(size_t) (k * 17640 + i)] = 0.0f;
                                                  p.setSampleGhost(s); p.chopLoad(s, false); if (p.chop.count() > 2) { p.chop.slices[1].pitch = 5; p.chop.slices[2].reverse = true; } p.chopRenderNow();
                                                  sp(p, "smp_mode", 4); sp(p, "smp_level", 0.9f); } },
        { "lab-keys", [](PoltergeistProcessor& p) { p.loadPreset(11); for (int k : { 36, 48, 55, 60, 64, 67 }) p.labMapKey(k, refGrowl(0.6, 80 + k, (uint32_t) k)); } },
        { "lab-plab", [](PoltergeistProcessor& p) { p.loadPreset(4); const int id = p.lab.addSource(refGrowl(1.0, 130, 9));
#if GHOSTY_PROCESS_LAB
                                                      plab::Audio a; a.rate = 44100; a.ch.push_back(p.lab.sources[id]->data); std::reverse(a.ch[0].begin(), a.ch[0].end()); p.replaceLabSource(id, a);
                                                      juce::ValueTree pl("PLAB"); pl.setProperty("seed", 777, nullptr); p.setPlabState(pl);
#endif
                                                      p.labMapKey(60, p.lab.sources[id]); p.labMapKey(48, p.lab.sources[id]); } },
    };
}
static void makeSampleRefs(const juce::File& dir) {
    int n = 0;
    for (auto& c : sampleRefCases()) {
        PoltergeistProcessor p; c.make(p);
        juce::MemoryBlock mb; p.getStateInformation(mb); dir.getChildFile(juce::String("smp_") + c.name + ".bin").replaceWithData(mb.getData(), mb.getSize());
        PoltergeistProcessor q; q.restoreState(mb.getData(), (int) mb.getSize()); q.chopRenderNow();
        writeRaw(dir.getChildFile(juce::String("smp_") + c.name + ".f32"), renderProc(q, 44100, legacyPhrase(), 4.5)); ++n;
    }
    std::printf("reference: %d sample-based projects\n", n);
}

// ---- the layout of every page the trim kept (owner 2026-10-26: closing the sample pages must not move the remaining controls).
// One line per visible component: page, its type, its bounds in the editor (the page tabs + the status line excluded: the tab row
// lost three tabs). `bounds FILE` writes it with the build from before the change; the new build compares (testLayoutKept).
static juce::StringArray layoutLines(PoltergeistEditor& ed) {
    juce::StringArray out;
    for (int pg : { 0, 2, 3, 4, 7, 8, 9 }) {
        ed.testShow(pg, 0, 0); if (pg == 0) ed.selectGhost(0);
        std::function<void(juce::Component&)> walk = [&](juce::Component& c) {
            for (auto* k : c.getChildren()) {
                if (!k->isVisible()) continue;
                const auto r = ed.getLocalArea(k, k->getLocalBounds());
                if (auto* b = dynamic_cast<juce::TextButton*>(k); b && r.getY() == 98 && r.getHeight() == 28 && r.getX() < 700) continue;   // a page tab
                if (dynamic_cast<juce::Label*>(k) && r.getY() == 102 && r.getHeight() == 20) continue;                                      // the status line
                if (dynamic_cast<gsui::Knob*>(&c)) continue;   // (a knob's own slider + label: the knob's bounds say it)
                juce::String t = typeid(*k).name(); if (t.contains("Keyboard")) t = "Keyboard";   // (DropKeyboard -> SampleKeysKeyboard: same place, no drop target)
                out.add(juce::String(pg) + " " + t + " " + r.toString());
                walk(*k);
            }
        };
        walk(ed);
    }
    out.sort(false);
    return out;
}

// ---- reference renders (old build) / comparison (new build)
static void makeRef(const juce::File& dir) {
    dir.createDirectory();
    const int np = (int) gs::factoryPresets().size();
    for (int i = 0; i < np; ++i) { PoltergeistProcessor p; p.loadPreset(i); writeRaw(dir.getChildFile("legacy_" + juce::String(i) + ".f32"), renderProc(p, 44100, legacyPhrase(), 4.5)); }
    int k = 0;
    for (auto& [w, s] : specWords()) writeRaw(dir.getChildFile("spec_" + juce::String(k++) + ".f32"), renderEngine(gs::snapFromPatch(gs::Summoner::summon(w, s)), 48000, legacyPhrase(), 4.0));
    // an "old project": a preset with knobs turned, saved by the old build
    PoltergeistProcessor p; p.loadPreset(6);
    setP(p, "op2_ratio", 3.0f); setP(p, "flt_cut", 900.0f); setP(p, "macro_drift", 0.4f); setP(p, "mono", 0.0f); setP(p, "cmb_amt", 0.3f);
    juce::MemoryBlock mb; p.getStateInformation(mb); dir.getChildFile("oldstate.bin").replaceWithData(mb.getData(), mb.getSize());
    PoltergeistProcessor q; q.restoreState(mb.getData(), (int) mb.getSize());
    writeRaw(dir.getChildFile("oldstate.f32"), renderProc(q, 44100, legacyPhrase(), 4.5));
    std::printf("reference: %d presets, %d engine snaps, 1 saved project -> %s\n", np, k, dir.getFullPathName().toRawUTF8());
    makeSampleRefs(dir);
}
static double diffDb(const Stereo& a, const Stereo& b) {
    if (a.size() != b.size() || a.empty()) return 999.0;
    double pk = 1e-9, md = 0; for (size_t i = 0; i < a.size(); ++i) { pk = juce::jmax(pk, (double) std::abs(a[i])); md = juce::jmax(md, (double) std::abs(a[i] - b[i])); }
    return md <= 0 ? -300.0 : 20.0 * std::log10(md / pk);
}
static void compareSampleRefs(const juce::File& dir);
static void testLayoutKept(const juce::File& dir);
static void compareRef(const juce::File& dir) {
    const int np = (int) juce::jmin((int) gs::factoryPresets().size(), 38);
    double worst = -300; int bad = 0, have = 0;
    for (int i = 0; i < np; ++i) {
        auto ref = readRaw(dir.getChildFile("legacy_" + juce::String(i) + ".f32")); if (ref.empty()) continue; ++have;
        PoltergeistProcessor p; p.loadPreset(i); const double d = diffDb(ref, renderProc(p, 44100, legacyPhrase(), 4.5));
        worst = juce::jmax(worst, d); if (d > -90) { ++bad; std::printf("   preset %d %s differs: %.1f dB\n", i, gs::factoryPresets()[(size_t) i].name, d); }
    }
    check(have == np && bad == 0, "old presets: " + juce::String(have) + " rendered vs the pre-change build, worst difference " + juce::String(worst, 1) + " dB (limit -90)");
    double ws = -300; int k = 0, sb = 0;
    for (auto& [w, s] : specWords()) {
        auto ref = readRaw(dir.getChildFile("spec_" + juce::String(k++) + ".f32")); if (ref.empty()) { ++sb; continue; }
        const double d = diffDb(ref, renderEngine(gs::snapFromPatch(gs::Summoner::summon(w, s)), 48000, legacyPhrase(), 4.0)); ws = juce::jmax(ws, d); if (d > -90) ++sb;
    }
    check(sb == 0, "Ghosty Spectrum engine snaps: " + juce::String(k) + " sounds, worst difference " + juce::String(ws, 1) + " dB");
    juce::MemoryBlock mb; dir.getChildFile("oldstate.bin").loadFileAsData(mb);
    auto ref = readRaw(dir.getChildFile("oldstate.f32"));
    // load it into a processor that has a NEW bass preset on (every new feature switched on) - the old project must still sound the same
    PoltergeistProcessor q; q.loadPreset(q.getNumPrograms() - 1); q.restoreState(mb.getData(), (int) mb.getSize());
    const double d = diffDb(ref, renderProc(q, 44100, legacyPhrase(), 4.5));
    check(mb.getSize() > 0 && d <= -90, "old saved project loads and renders the same (over a new preset): " + juce::String(d, 1) + " dB");
    // a brand-new instance runs the current engine; an old project restored into a brand-new instance goes back to the original one
    PoltergeistProcessor fresh; const float evFresh = fresh.apvts.getRawParameterValue("eng_ver")->load();
    PoltergeistProcessor fr2; fr2.restoreState(mb.getData(), (int) mb.getSize()); const float evOld = fr2.apvts.getRawParameterValue("eng_ver")->load();
    const double d2 = diffDb(ref, renderProc(fr2, 44100, legacyPhrase(), 4.5));
    PoltergeistProcessor fr3; fr3.loadPreset(0); const float evLegacy = fr3.apvts.getRawParameterValue("eng_ver")->load();
    check(evFresh == 2.0f && evOld == 0.0f && evLegacy == 0.0f && d2 <= -90, "engine revision: brand-new instance " + juce::String((int) evFresh) + ", old project " + juce::String((int) evOld)
          + " (renders " + juce::String(d2, 1) + " dB vs the old build), original preset picked from the list " + juce::String((int) evLegacy));
    compareSampleRefs(dir);
    if (dir.getChildFile("layout.txt").existsAsFile()) testLayoutKept(dir);
}
// the new build: each old sample-based project loads from the bytes the old build saved and renders exactly the same samples
static void compareSampleRefs(const juce::File& dir) {
    int have = 0, same = 0; juce::String txt;
    for (auto& c : sampleRefCases()) {
        juce::MemoryBlock mb; if (!dir.getChildFile(juce::String("smp_") + c.name + ".bin").loadFileAsData(mb) || mb.getSize() == 0) continue;
        const auto ref = readRaw(dir.getChildFile(juce::String("smp_") + c.name + ".f32")); if (ref.empty()) continue; ++have;
        PoltergeistProcessor q; q.loadPreset(q.getNumPrograms() - 1); q.restoreState(mb.getData(), (int) mb.getSize()); q.chopRenderNow();
        const auto now = renderProc(q, 44100, legacyPhrase(), 4.5);
        const bool exact = now.size() == ref.size() && std::memcmp(now.data(), ref.data(), ref.size() * sizeof(float)) == 0;
        double pk = 0; for (float x : ref) pk = juce::jmax(pk, (double) std::abs(x));
        if (exact) ++same; txt << c.name << (exact ? " exact" : " DIFFERS (" + juce::String(diffDb(ref, now), 1) + " dB)") << " (peak " << juce::String(pk, 2) << "); ";
        // ... and saving it again keeps every sample / Lab / CHOP / PROCESS LAB part it had (the same children, the same audio)
        juce::MemoryBlock again; q.getStateInformation(again);
        auto a = juce::AudioProcessor::getXmlFromBinary(mb.getData(), (int) mb.getSize()), b = juce::AudioProcessor::getXmlFromBinary(again.getData(), (int) again.getSize());
        bool kept = a && b;
        if (kept) for (auto* tag : { "SAMPLE", "LAB", "CHOP", "KEYS", "PLAB", "PLABSRC", "LABMARKS" }) {
            auto* x = a->getChildByName(tag); auto* y = b->getChildByName(tag);
            if ((x == nullptr) != (y == nullptr) || (x && !x->isEquivalentTo(y, false))) { kept = false; txt << "(" << tag << " changed on re-save) "; }
        }
        if (!kept) --same;
    }
    check(have == (int) sampleRefCases().size() && same == have, "old sample-based projects (state saved by the pre-trim build): " + juce::String(same) + " / " + juce::String(have)
          + " render bit-identical and re-save with every sample part unchanged: " + txt.dropLastCharacters(2));
}
static void testLayoutKept(const juce::File& dir) {
    juce::StringArray want; want.addLines(dir.getChildFile("layout.txt").loadFileAsString()); want.removeEmptyStrings();
    PoltergeistProcessor p; std::unique_ptr<PoltergeistEditor> ed(dynamic_cast<PoltergeistEditor*>(p.createEditor())); ed->setSize(1040, 744);
    const auto now = layoutLines(*ed);
    juce::StringArray gone, added;
    for (auto& l : want) if (!now.contains(l)) gone.add(l);
    for (auto& l : now) if (!want.contains(l)) added.add(l);
    check(!want.isEmpty() && gone.isEmpty() && added.isEmpty(), "layout: the " + juce::String(now.size()) + " visible controls of the kept pages (GHOSTS, MOVE, FX, PLAY, BASS, WOBBLE, MORPH) sit exactly where the pre-trim build put them ("
          + juce::String(want.size()) + " before)" + (gone.isEmpty() ? juce::String() : "; MOVED / GONE: " + gone.joinIntoString(" | ").substring(0, 600)) + (added.isEmpty() ? juce::String() : "; NEW: " + added.joinIntoString(" | ").substring(0, 600)));
}


#if __has_include("BassPresets.h")
#include "BassTestsNew.inc"
#else
static int runNew(int, char**) { return 0; }
#endif

int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc > 2 && juce::String(argv[1]) == "ref") { makeRef(juce::File(argv[2])); return 0; }
    if (argc > 2 && juce::String(argv[1]) == "bounds") { PoltergeistProcessor p; std::unique_ptr<PoltergeistEditor> ed(dynamic_cast<PoltergeistEditor*>(p.createEditor())); ed->setSize(1040, 744);
        juce::File(argv[2]).replaceWithText(layoutLines(*ed).joinIntoString("\n")); return 0; }
    const int r = runNew(argc, argv); if (r >= 0 && argc > 1 && juce::String(argv[1]) != "all") return r;
    if (argc > 2 && juce::String(argv[1]) == "all") compareRef(juce::File(argv[2]));
    std::printf("%d failure(s)\n", failures);
    return failures;
}
