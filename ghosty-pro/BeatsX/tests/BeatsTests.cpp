// Ghosty Beats test harness.  BeatsTests [test | shots <dir> | demos <dir>]
#include "../BeatsXProcessor.h"
#include "../BeatsXEditor.h"
#include "../EditorParts.h"
#include "../RhythmLab.h"
#include "../MixPage.h"
#include "../AudioToMidi.h"
#include "WaveRegion.h"
#include "../MelodyTrackerView.h"
#include "legacy/BeatEngine.h"
#include <cstdio>

static int fails = 0, passes = 0;
// allocations on the thread that calls processBlock (the audio thread): counted only while gRtCount is set on that thread
#include <new>
#include <cstdlib>
static thread_local bool gRtCount = false; static std::atomic<long> gRtAllocs { 0 };
static void* rtAlloc(std::size_t n) { if (gRtCount) ++gRtAllocs; if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new(std::size_t n) { return rtAlloc(n); } void* operator new[](std::size_t n) { return rtAlloc(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept { if (gRtCount) ++gRtAllocs; return std::malloc(n ? n : 1); } void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { if (gRtCount) ++gRtAllocs; return std::malloc(n ? n : 1); }
void operator delete(void* p) noexcept { std::free(p); } void operator delete(void* p, std::size_t) noexcept { std::free(p); } void operator delete[](void* p) noexcept { std::free(p); } void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); } void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
#define CHECK(cond, ...) do { if (cond) { ++passes; } else { ++fails; std::printf("  FAIL %s:%d  ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
static void section(const char* s) { std::printf("== %s\n", s); }

// ---------------------------------------------------------------- helpers
static gb::SoundPtr makeSound(const juce::String& name, int n, std::function<float(int)> f) { auto s = std::make_shared<gb::Sound>(); s->name = name; s->category = "Test"; s->rate = 48000; s->data.resize((size_t) n); for (int i = 0; i < n; ++i) s->data[(size_t) i] = f(i); return s; }
static gb::EngineParams cleanParams(double bpm) { gb::EngineParams P; P.glue = 0; P.volume = 0.5f; P.bpm = (float) bpm; P.limiter = 2; return P; }   // (OFF: sample-accurate checks without the limiter's look-ahead)
// render a snapshot with a private engine (blocks of `block`, or random sizes when block < 0)
static juce::AudioBuffer<float> render(const gb::Snapshot& S, const gb::EngineParams& P, double seconds, int block = 512, int seed = 1) {
    gb::BeatEngine e; e.prepare(48000, 4096); e.setSnapshot(&S); e.internalPlay = true;
    const int total = (int) (seconds * 48000); juce::AudioBuffer<float> out(2, total), buf(2, 4096); juce::MidiBuffer none; juce::Random R(seed);
    const int lat = gb::BeatEngine::latencyFor(P, e.lookahead()); juce::AudioBuffer<float> raw(2, total + lat);
    for (int pos = 0; pos < total + lat;) { const int n = juce::jmin(total + lat - pos, block > 0 ? block : 1 + R.nextInt(1500)); buf.setSize(2, n, false, false, true);
        e.process(buf, none, P, P.bpm, 0, false); for (int c = 0; c < 2; ++c) raw.copyFrom(c, pos, buf, c, 0, n); pos += n; }
    for (int c = 0; c < 2; ++c) out.copyFrom(c, 0, raw, c, lat, total);   // like the drag-out: the CLEAN limiter's look-ahead is trimmed
    return out;
}
static std::vector<int> onsets(const juce::AudioBuffer<float>& b, float thr = 0.02f, int quiet = 24) {
    std::vector<int> o; const float* x = b.getReadPointer(0); int last = -100000;
    if (b.getNumSamples() > 0 && std::abs(x[0]) > thr) { o.push_back(0); last = 0; }
    for (int i = 1; i < b.getNumSamples(); ++i) if (std::abs(x[i]) > thr && std::abs(x[i - 1]) <= thr && i - last > quiet) { bool silent = true; for (int k = juce::jmax(0, i - quiet); k < i - 1; ++k) if (std::abs(x[k]) > thr) silent = false; if (silent) o.push_back(i); last = i; }
    return o;
}
static gb::Snapshot oneTrack(const gb::Sound* snd) { gb::Snapshot S; S.sound[0] = snd; S.tracks[0].vol = 1; S.tracks[0].sound = "t"; S.scTrack = -1; return S; }
static double corr(const float* a, const float* b, int n) { double ab = 0, aa = 0, bb = 0; for (int i = 0; i < n; ++i) { ab += a[i] * b[i]; aa += a[i] * a[i]; bb += b[i] * b[i]; } return ab / std::sqrt(aa * bb + 1e-30); }
static juce::AudioBuffer<float> readWav(const juce::String& path) { juce::AudioFormatManager fm; fm.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(juce::File(path))); juce::AudioBuffer<float> b; if (!r) return b; b.setSize((int) r->numChannels, (int) r->lengthInSamples); r->read(&b, 0, (int) r->lengthInSamples, 0, true, true); return b; }
static juce::MemoryBlock state(BeatsXProcessor& p) { juce::MemoryBlock m; p.getStateInformation(m); return m; }

// ---------------------------------------------------------------- 1. library
static void testLibrary() {
    section("library");
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    auto lib = gb::loadBeatsLibrary(48000.0, false);
    const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
    std::map<juce::String, int> folders; std::set<juce::int64> hashes; int bad = 0;
    for (auto& s : lib->sounds()) {
        folders[gb::folderOf(s->category)]++;
        float pk = 0; bool finite = true; for (auto v : s->data) { if (!std::isfinite(v)) finite = false; pk = std::max(pk, std::abs(v)); }
        const double len = s->data.size() / s->rate;
        if (!finite || pk < 0.85f || pk > 0.9f || len < 0.02 || len > 6.0) { ++bad; std::printf("  bad sound %s peak %.3f len %.2f finite %d\n", s->name.toRawUTF8(), pk, len, (int) finite); }
        uint64_t h = 1469598103934665603ULL; for (size_t i = 0; i < s->data.size(); i += 7) h = (h ^ (uint64_t) std::lround(s->data[i] * 30000)) * 1099511628211ULL; hashes.insert((juce::int64) h);
    }
    std::printf("  %d sounds built in %.0f ms:", (int) lib->sounds().size(), ms); for (auto& f : folders) std::printf(" %s %d,", f.first.toRawUTF8(), f.second); std::printf("\n");
    CHECK(lib->sounds().size() >= 300, "only %d sounds", (int) lib->sounds().size());
    CHECK(bad == 0, "%d sounds failed render checks", bad);
    CHECK((int) hashes.size() == (int) lib->sounds().size(), "%d duplicate sounds", (int) lib->sounds().size() - (int) hashes.size());
    CHECK(folders["KICKS"] >= 60 && folders["SNARES"] >= 40 && folders["CLAPS"] >= 30 && folders["HATS"] >= 50 && folders["PERC"] >= 50 && folders["CYMBALS"] >= 15 && folders["FX"] >= 30 && folders["808s"] >= 20 && folders["BREAKS"] >= 6, "folder counts below target");
    int missing = 0; for (auto& k : gb::allKits()) for (auto* n : k.sounds) if (!lib->byName(n)) { ++missing; std::printf("  kit %s: missing sound %s\n", k.name, n); }
    CHECK(missing == 0, "%d kit sounds missing", missing);
    std::printf("  %d kits (%d genre kits)\n", (int) gb::allKits().size(), (int) gb::genreKits().size());
    CHECK(gb::genreKits().size() >= 20, "genre kits");
    // classic 61 identical to the plain DrumLibrary (Spectrum / Mickens / old projects hear the same thing)
    gb::DrumLibrary classic; classic.build(48000.0); bool same = true;
    for (size_t i = 0; i < classic.sounds().size(); ++i) same = same && classic.sounds()[i]->data == lib->sounds()[i]->data && classic.sounds()[i]->name == lib->sounds()[i]->name;
    CHECK(same && classic.sounds().size() == 61, "classic sounds changed");
    // disk cache: written once, then read back bit-identical
    gb::beatsCacheDir().deleteRecursively();
    auto c1 = gb::loadBeatsLibrary(48000.0, true); const auto t1 = juce::Time::getMillisecondCounterHiRes();
    auto c2 = gb::loadBeatsLibrary(48000.0, true); const auto cms = juce::Time::getMillisecondCounterHiRes() - t1;
    bool eq = c1->sounds().size() == c2->sounds().size() && c1->sounds().size() == lib->sounds().size();
    for (size_t i = 0; eq && i < c1->sounds().size(); ++i) eq = c1->sounds()[i]->data == c2->sounds()[i]->data && c1->sounds()[i]->data == lib->sounds()[i]->data && c1->sounds()[i]->beats == c2->sounds()[i]->beats;
    std::printf("  cached load %.0f ms\n", cms);
    CHECK(eq, "cache round trip differs");
}


// ---------------------------------------------------------------- cachegate: the drum cache on disk is scrambled, the sound is bit-exact
// The library hash below was measured with the pre-scramble code (cache v7, plain 16-bit PCM): any change to what plays fails here.
static uint64_t libraryHash(const gb::DrumLibrary& lib) {
    uint64_t h = 1469598103934665603ULL; auto mix = [&](uint64_t v) { h = (h ^ v) * 1099511628211ULL; };
    for (auto& s : lib.sounds()) {
        if (gb::isPackSound(s->name)) continue;   // the DRILL / LATIN packs are appended after the cached library (checked on their own below)
        for (auto c : s->name) mix((uint64_t) c); for (auto c : s->category) mix((uint64_t) c);
        uint64_t b; std::memcpy(&b, &s->beats, 8); mix(b); uint32_t t; std::memcpy(&t, &s->trim, 4); mix(t); mix(s->data.size());
        for (auto v : s->data) { uint32_t u; std::memcpy(&u, &v, 4); mix(u); }
        for (auto v : s->right) { uint32_t u; std::memcpy(&u, &v, 4); mix(u); }
    }
    return h;
}
static uint64_t renderHash() {   // a real kit render through the processor (which loads the library through the cache)
    BeatsXProcessor p; p.applyKit(3, true); auto spec = p.makeSpec({ false, false, -1 }); spec.bars = 2;
    auto b = render(*spec.snap, spec.P, 2 * 4 * 60.0 / spec.P.bpm);
    uint64_t h = 1469598103934665603ULL;
    for (int c = 0; c < b.getNumChannels(); ++c) for (int i = 0; i < b.getNumSamples(); ++i) { const float v = b.getSample(c, i); uint32_t u; std::memcpy(&u, &v, 4); h = (h ^ u) * 1099511628211ULL; }
    return h;
}
static int cacheGateTest() {
    auto home = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("beats-cachegate-" + juce::String(juce::Time::currentTimeMillis()));
    home.createDirectory(); setenv("HOME", home.getFullPathName().toRawUTF8(), 1); unsetenv("XDG_DOCUMENTS_DIR");
    int fails = 0; auto ok = [&](bool c, const char* what) { std::printf("  %s %s\n", c ? "ok  " : "FAIL", what); if (!c) ++fails; };
    const char* hashEnv = std::getenv("BEATS_CACHE_REF");   // (how the reference was taken: BEATS_CACHE_REF=print)
    auto fresh = gb::loadBeatsLibrary(48000.0, false); const auto hf = libraryHash(*fresh);
    const auto dir = gb::beatsCacheDir(); dir.deleteRecursively(); dir.createDirectory();
    const auto stale = dir.getChildFile("library-v7-48000.bin"); stale.replaceWithText("an old cache"); const auto other = dir.getChildFile("notes.txt"); other.replaceWithText("not ours");
    auto c1 = gb::loadBeatsLibrary(48000.0, true); const auto h1 = libraryHash(*c1);   // builds + writes the cache
    auto c2 = gb::loadBeatsLibrary(48000.0, true); const auto h2 = libraryHash(*c2);   // reads it back
    const auto rh = renderHash();   // the processor's shared library comes from the cache now
    std::printf("cachegate: library fresh %016llx, written %016llx, read back %016llx, render %016llx\n", (unsigned long long) hf, (unsigned long long) h1, (unsigned long long) h2, (unsigned long long) rh);
    if (hashEnv != nullptr) return 0;
    constexpr uint64_t kRefLibrary = 0x7970282f0a2f54e7ULL, kRefRender = 0x4f28447203cb220fULL;   // measured with the v7 (unscrambled) code
    ok(hf == kRefLibrary, "fresh library bit-exact to the pre-scramble build");
    ok(h1 == kRefLibrary && h2 == kRefLibrary, "cache write + read back bit-exact (every sound, trim, length)");
    ok(rh == kRefRender, "a kit render bit-exact to the pre-scramble build");
    { juce::StringArray packs = gb::drillPackNames(); packs.addArray(gb::latinPackNames()); packs.addArray(gb::classicPackNames());   // the DRILL pack, the LATIN pack, the classic-character packs
      int last = 0; const int np = packs.size(); bool same = c1->sounds().size() == fresh->sounds().size() && c2->sounds().size() == fresh->sounds().size();
      for (int i = 0; i < np; ++i) { const int k = (int) fresh->sounds().size() - np + i; if (k >= 0 && fresh->sounds()[(size_t) k]->name == packs[i]) ++last;
          if (same && k >= 0) same = fresh->sounds()[(size_t) k]->data == c2->sounds()[(size_t) k]->data && fresh->sounds()[(size_t) k]->trim == c2->sounds()[(size_t) k]->trim; }
      ok(last == np && same, "the DRILL + LATIN + classic packs: appended last (after the cached library), identical on a fresh build and a cached load"); }
    juce::Array<juce::File> bins; dir.findChildFiles(bins, juce::File::findFiles, false, "library-v*.bin");
    ok(bins.size() == 1 && bins[0].getFileName() != "library-v7-48000.bin", "one cache file, new version (the stale v7 one was removed)");
    ok(other.existsAsFile(), "an unrelated file in the folder is left alone");
    if (bins.size() == 1) {
        juce::MemoryBlock mb; bins[0].loadFileAsData(mb);
        // the first extended sound's first 32 samples as the old 16-bit PCM bytes: must not appear in the file
        const gb::Sound* probe = nullptr;   // the first extended (cached) sound: the classic 61 are always built, never cached
        for (size_t i = 61; i < c2->sounds().size() && probe == nullptr; ++i) if (c2->sounds()[i]->data.size() > 400) probe = c2->sounds()[i].get();
        int16_t pcm[32] = {}; if (probe) for (int k = 0; k < 32; ++k) pcm[k] = (int16_t) juce::jlimit(-32767.0f, 32767.0f, std::round(probe->data[(size_t) (k + 200)] * 32767.0f));
        bool found = false; const auto* d = (const char*) mb.getData();
        for (size_t i = 0; probe && i + 64 <= mb.getSize() && !found; ++i) found = std::memcmp(d + i, pcm, 64) == 0;
        bool nameClear = probe && juce::String::fromUTF8(d, (int) mb.getSize()).contains(probe->name);
        std::printf("  probe sound '%s', file %d bytes\n", probe ? probe->name.toRawUTF8() : "-", (int) mb.getSize());
        ok(probe != nullptr && !found, "the PCM is not in the file in clear (a 32-sample run of a known sound is not found)");
        ok(!nameClear, "sound names are not in clear either");
        juce::AudioFormatManager fm; fm.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(bins[0]));
        ok(r == nullptr, "no audio reader opens the cache file");
    }
    home.deleteRecursively();
    std::printf("cachegate: %s\n", fails == 0 ? "PASS" : "FAIL");
    return fails;
}

// ---------------------------------------------------------------- 2. timing
static void testTiming() {
    section("timing (sample-accurate onsets, 120 bpm: 1/16 = 6000 samples)");
    auto click = makeSound("click", 48, [](int i) { return std::exp(-i / 6.0f); });
    auto run = [&](std::function<void(gb::Snapshot&, gb::EngineParams&)> setup, double sec, int block = 512) { auto S = oneTrack(click.get()); auto P = cleanParams(120); setup(S, P); return onsets(render(S, P, sec, block)); };
    auto expectOn = [&](const char* name, const std::vector<int>& got, const std::vector<int>& want) {
        bool ok = got.size() >= want.size(); int worst = 0;
        for (size_t i = 0; ok && i < want.size(); ++i) { worst = std::max(worst, std::abs(got[i] - want[i])); ok = std::abs(got[i] - want[i]) <= 1; }
        std::printf("  %-28s %s (worst %d samples, %d onsets)\n", name, ok ? "ok" : "BAD", worst, (int) got.size());
        CHECK(ok, "%s onsets", name); if (!ok) { for (size_t i = 0; i < std::min<size_t>(got.size(), 12); ++i) std::printf(" %d", got[i]); std::printf("\n"); }
    };
    expectOn("straight 1/16", run([](auto& S, auto&) { for (int s = 0; s < 16; s += 4) S.patterns[0].steps[0][(size_t) s].vel = 100; }, 1.9), { 0, 24000, 48000, 72000 });
    expectOn("nudge +25%", run([](auto& S, auto&) { S.patterns[0].steps[0][4].vel = 100; S.patterns[0].steps[0][4].nudge = 25; }, 0.6), { 25500 });
    expectOn("global swing 50%", run([](auto& S, auto& P) { P.swing = 0.5f; S.patterns[0].steps[0][1].vel = 100; S.patterns[0].steps[0][2].vel = 100; }, 0.4), { 7500, 12000 });
    expectOn("track swing + Swing 58%", run([](auto& S, auto& P) { P.groove = 2; S.tracks[0].swing = 0.2f; S.patterns[0].steps[0][1].vel = 100; }, 0.3), { 6000 + 960 + 600 });
    expectOn("roll x4 in a step", run([](auto& S, auto&) { auto& st = S.patterns[0].steps[0][2]; st.vel = 100; st.roll = 4; }, 0.5), { 12000, 13500, 15000, 16500 });
    expectOn("flam 20 ms", run([](auto& S, auto&) { auto& st = S.patterns[0].steps[0][4]; st.vel = 100; st.flam = 20; }, 0.6), { 24000 - 960, 24000 });
    expectOn("1/32 grid", run([](auto& S, auto&) { S.patterns[0].res = 2; S.patterns[0].length = 32; S.patterns[0].steps[0][3].vel = 100; }, 0.3), { 9000 });
    expectOn("1/16T grid", run([](auto& S, auto&) { S.patterns[0].res = 3; S.patterns[0].length = 24; S.patterns[0].steps[0][2].vel = 100; S.patterns[0].steps[0][3].vel = 100; }, 0.4), { 8000, 12000 });
    expectOn("polymeter: 3-step track", run([](auto& S, auto&) { S.patterns[0].trackLen[0] = 3; S.patterns[0].steps[0][0].vel = 100; }, 1.6), { 0, 18000, 36000, 54000, 72000 });
    expectOn("convergence: realign each bar", run([](auto& S, auto&) { S.converge = 1; S.patterns[0].trackLen[0] = 3; S.patterns[0].steps[0][0].vel = 100; }, 2.1), { 0, 18000, 36000, 54000, 72000, 90000, 96000 });
    expectOn("retrig 1/64 x4", run([](auto& S, auto&) { auto& st = S.patterns[0].steps[0][4]; st.vel = 100; st.roll = 4; st.rate = 16; }, 0.7), { 24000, 25500, 27000, 28500 });
    expectOn("retrig 1/128 x6", run([](auto& S, auto&) { auto& st = S.patterns[0].steps[0][4]; st.vel = 100; st.roll = 6; st.rate = 32; }, 0.7), { 24000, 24750, 25500, 26250, 27000, 27750 });
    expectOn("retrig accelerating", run([](auto& S, auto&) { auto& st = S.patterns[0].steps[0][0]; st.vel = 100; st.roll = 4; st.rate = 8; st.accel = 100; }, 0.4), { 0, 3000, 4500, 5250 });
    expectOn("odd block sizes", run([](auto& S, auto&) { for (int s = 0; s < 16; ++s) { S.patterns[0].steps[0][(size_t) s].vel = 100; S.patterns[0].steps[0][(size_t) s].nudge = (int8_t) ((s * 7) % 21 - 10); } }, 0.5, -1),
             { 6000 - 180, 12000 + 240, 18000 - 600, 24000 - 180 });
    // song mode across blocks with a different grid per block
    { auto S = oneTrack(click.get()); auto P = cleanParams(120); P.songMode = true;
      S.patterns[0].steps[0][0].vel = 100; S.patterns[1].res = 0; S.patterns[1].length = 8; S.patterns[1].steps[0][1].vel = 100;
      S.song[0] = { 0, 0, 1 }; S.song[1] = { 1, 1, 1 }; S.songCount = 2;
      expectOn("song: A then B (1/8 grid)", onsets(render(S, P, 4.1)), { 0, 96000 + 12000 }); }
    // pitch ramp across a retrigger: 1 kHz sine, 2 hits a quarter apart, +12 semitones -> 2 kHz
    { auto sine = makeSound("sine", 4800, [](int i) { return (float) std::sin(juce::MathConstants<double>::twoPi * 1000.0 * i / 48000.0); });
      auto S = oneTrack(sine.get()); auto P = cleanParams(120); auto& st = S.patterns[0].steps[0][0]; st.vel = 127; st.roll = 2; st.rate = 2; st.pitchRamp = 12; st.cents = 0;
      auto b = render(S, P, 0.8); const float* x = b.getReadPointer(0);
      auto freq = [&](int a, int n) { int z = 0; for (int i = a + 1; i < a + n; ++i) if ((x[i - 1] < 0) != (x[i] < 0)) ++z; return z * 48000.0 / (2.0 * n); };
      const double f1 = freq(200, 2000), f2 = freq(12200, 1500);
      std::printf("  pitch ramp: %.0f Hz -> %.0f Hz\n", f1, f2); CHECK(std::abs(f1 - 1000) < 30 && std::abs(f2 - 2000) < 60, "pitch ramp");
      st.roll = 1; st.pitchRamp = 0; st.cents = 50; auto b2 = render(S, P, 0.2); x = b2.getReadPointer(0); const double fc = freq(200, 4000);
      std::printf("  +50 cents: %.1f Hz (want %.1f)\n", fc, 1000 * std::exp2(50 / 1200.0)); CHECK(std::abs(fc - 1029.3) < 8, "cents"); }
    // reverse + sample offset + cut, on a rising ramp
    { auto ramp = makeSound("ramp", 4000, [](int i) { return (float) i / 4000.0f; });
      auto S = oneTrack(ramp.get()); auto P = cleanParams(120); auto& st = S.patterns[0].steps[0][0]; st.vel = 127;
      auto peakAt = [&](const juce::AudioBuffer<float>& b, int i) { return std::abs(b.getSample(0, i)); };
      auto fwd = render(S, P, 0.2); const float full = peakAt(fwd, 3990);
      st.flags = 1; auto rev = render(S, P, 0.2);
      std::printf("  reverse: start %.3f end %.3f (forward start %.3f end %.3f)\n", peakAt(rev, 2), peakAt(rev, 3980), peakAt(fwd, 2), full);
      CHECK(peakAt(rev, 2) > 0.9f * full && peakAt(rev, 3980) < 0.05f * full, "reverse");
      st.flags = 0; st.offset = 50; auto off = render(S, P, 0.2);
      std::printf("  offset 50%%: first sample %.3f of %.3f\n", peakAt(off, 0), full); CHECK(std::abs(peakAt(off, 0) / full - 0.5f) < 0.03f && peakAt(off, 2100) < 1e-4f, "offset");
      auto longS = makeSound("long", 48000, [](int i) { return 0.5f * (float) std::sin(i * 0.05); });
      auto S2 = oneTrack(longS.get()); auto& c = S2.patterns[0].steps[0][0]; c.vel = 127; c.cut = 50;
      auto cut = render(S2, P, 0.3); float after = 0, before = 0; for (int i = 3300; i < 6000; ++i) after = std::max(after, std::abs(cut.getSample(0, i))); for (int i = 2000; i < 2900; ++i) before = std::max(before, std::abs(cut.getSample(0, i)));
      std::printf("  cut 50%% of a 1/16: level before 3000 = %.3f, after 3220 = %.5f\n", before, after); CHECK(before > 0.1f && after < 1e-4f, "cut"); }
    // slices: a 4-click 'break' sliced, step 0 plays slice 3
    { auto br = makeSound("br", 8000, [](int i) { return (i % 2000) < 100 ? (float) (i / 2000 + 1) * 0.2f : 0.0f; });
      auto S = oneTrack(br.get()); S.tracks[0].slices = { 0, 2000, 4000, 6000 }; auto P = cleanParams(120); auto& st = S.patterns[0].steps[0][0]; st.vel = 127; st.slice = 3;
      auto b = render(S, P, 0.2); float l = 0; for (int i = 0; i < 50; ++i) l = std::max(l, std::abs(b.getSample(0, i))); float rest = 0; for (int i = 2100; i < 9000; ++i) rest = std::max(rest, std::abs(b.getSample(0, i)));
      std::printf("  slice 3: level %.3f (want ~0.6 x gain), after the slice %.5f\n", l, rest); CHECK(l > 0.15f && rest < 1e-5f, "slice playback"); }
}

// ---------------------------------------------------------------- 3. MIDI export
static void testMidi(BeatsXProcessor& p) {
    section("MIDI export");
    p.applyKit(0, true); for (auto& pt : p.model.patterns) pt.clear();
    if (auto* b = p.apvts.getParameter("bpm")) b->setValueNotifyingHost(b->convertTo0to1(120.0f));
    if (auto* g = p.apvts.getParameter("swing")) g->setValueNotifyingHost(0.0f);
    if (auto* g = p.apvts.getParameter("human")) g->setValueNotifyingHost(0.0f);
    if (auto* pp = p.apvts.getParameter("pattern")) pp->setValueNotifyingHost(0.0f);
    auto& P = p.model.patterns[0]; P.length = 16;   // Trap kit: 0 808, 1 kick, 2 snare, 3 clap, 4 hat, 5 open hat
    P.steps[1][0].vel = 127; P.steps[2][4].vel = 90; P.steps[2][4].nudge = 20; P.steps[4][8].vel = 64; P.steps[4][8].roll = 3; P.steps[5][14].vel = 100;
    P.steps[0][0].vel = 100; P.steps[0][0].pitch = 3;
    p.publish();
    for (int ch : { 1, 10 }) {
        p.midiChannel = ch; auto f = p.exportMidi(false);
        juce::MidiFile mf; juce::FileInputStream in(f); CHECK(in.openedOk() && mf.readFrom(in), "read midi");
        std::vector<std::tuple<int, int, int, int>> notes;   // tick, note, vel, ch
        for (int tr = 0; tr < mf.getNumTracks(); ++tr) for (auto* e : *mf.getTrack(tr)) if (e->message.isNoteOn()) notes.push_back({ (int) std::lround(e->message.getTimeStamp()), e->message.getNoteNumber(), e->message.getVelocity(), e->message.getChannel() });
        std::sort(notes.begin(), notes.end());
        const std::vector<std::tuple<int, int, int, int>> want { { 0, 24 + 3, 100, ch }, { 0, 36, 127, ch }, { 1008, 38, 90, ch }, { 1920, 42, (int) std::lround(64 * 0.7), ch }, { 2000, 42, (int) std::lround(64 * 0.85), ch }, { 2080, 42, 64, ch }, { 3360, 46, 100, ch } };
        bool ok = notes == want;
        std::printf("  channel %d: %d notes %s\n", ch, (int) notes.size(), ok ? "ok (kick 36, snare 38 +20%% nudge, hat 42 roll x3 as 3 notes, open hat 46, 808 C0+3)" : "BAD");
        if (!ok) for (auto& n : notes) std::printf("    tick %d note %d vel %d ch %d\n", std::get<0>(n), std::get<1>(n), std::get<2>(n), std::get<3>(n));
        CHECK(ok, "midi notes ch %d", ch);
    }
    p.midiChannel = 1;
    auto one = p.exportMidi(false, 2); juce::MidiFile mf; juce::FileInputStream in(one); mf.readFrom(in); int n = 0; for (auto* e : *mf.getTrack(0)) if (e->message.isNoteOn()) ++n;
    CHECK(n == 1, "per-track MIDI drag (got %d notes)", n);
}

// ---------------------------------------------------------------- 4. audio export
static void testAudioExport(BeatsXProcessor& p) {
    section("audio export: mix vs sum of stems");
    p.applyKit(4, true);   // House kit
    if (auto* g = p.apvts.getParameter("glue")) g->setValueNotifyingHost(g->convertTo0to1(0.5f)); if (auto* g = p.apvts.getParameter("clip")) g->setValueNotifyingHost(g->convertTo0to1(0.3f));   // a hot bus: stems are pre-bus, so it must not matter
    auto mix = p.renderNow({ false, false, -1 }), stems = p.renderNow({ false, true, -1 });
    auto dry = p.makeSpec({ false, false, -1 }); dry.P.bypassMaster = true; auto dryF = p.renderSpec(dry, nullptr);
    CHECK(mix.size() == 1 && stems.size() >= 4 && dryF.size() == 1, "export files (%d stems)", stems.size());
    if (mix.isEmpty()) return;
    auto m = readWav(mix[0]), d = readWav(dryF[0]); juce::AudioBuffer<float> sum(2, d.getNumSamples()); sum.clear();
    for (auto& f : stems) { auto s = readWav(f); for (int c = 0; c < 2; ++c) sum.addFrom(c, 0, s, c, 0, juce::jmin(s.getNumSamples(), d.getNumSamples())); }
    double md = 0; for (int c = 0; c < 2; ++c) for (int i = 0; i < d.getNumSamples(); ++i) md = std::max(md, (double) std::abs(d.getSample(c, i) - sum.getSample(c, i)));
    const double cL = corr(m.getReadPointer(0), sum.getReadPointer(0), juce::jmin(m.getNumSamples(), sum.getNumSamples()));
    { int bestLag = 0; double best = -2; const int n = juce::jmin(m.getNumSamples(), d.getNumSamples()) - 16;   // the processed MIX must sit exactly on the dry mix (the limiter's look-ahead is trimmed)
      for (int lag = -100; lag <= 100; ++lag) { const double c = corr(m.getReadPointer(0) + 300 + lag, d.getReadPointer(0) + 300, n - 600); if (c > best) { best = c; bestLag = lag; } }
      std::printf("  processed MIX vs dry mix: best alignment at lag %d samples (corr %.3f)\n", bestLag, best); CHECK(bestLag == 0, "drag-out MIX is time-aligned with the stems"); }
    juce::WavAudioFormat w; std::unique_ptr<juce::AudioFormatReader> r(w.createReaderFor(new juce::FileInputStream(juce::File(mix[0])), true));
    std::printf("  %d stems (pre-bus): sum vs dry mix max diff %.2e (24-bit lsb 1.2e-7 x %d stems); vs the processed MIX corr %.3f; %d-bit %.0f Hz, %.2f s\n", stems.size(), md, stems.size(), cL, r ? (int) r->bitsPerSample : 0, r ? r->sampleRate : 0.0, m.getNumSamples() / 48000.0);
    CHECK(md < 2e-6 * stems.size(), "stems sum exactly to the dry mix");
    CHECK(r && r->bitsPerSample == 24, "24-bit");
    if (auto* g = p.apvts.getParameter("glue")) g->setValueNotifyingHost(g->convertTo0to1(0.25f)); if (auto* g = p.apvts.getParameter("clip")) g->setValueNotifyingHost(0.0f);
    auto again = p.waitAudioRender({ false, false, -1 }); auto again2 = p.waitAudioRender({ false, false, -1 });
    CHECK(again == again2 && again.size() == 1, "render cache reuses the file");
    auto f1 = p.renderNow({ false, false, -1 }); CHECK(f1[0] != mix[0], "unique filenames");
}

// ---------------------------------------------------------------- 5. dropped samples
static void testSamples() {
    section("drop a sample");
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("ghostybeats-test"); dir.createDirectory();
    auto wavf = dir.getChildFile("My Kick.wav"); wavf.deleteFile();
    { juce::WavAudioFormat w; auto os = std::unique_ptr<juce::OutputStream>(wavf.createOutputStream());
      std::unique_ptr<juce::AudioFormatWriter> wr(w.createWriterFor(os.get(), 44100, 2, 24, {}, 0)); os.release();
      juce::AudioBuffer<float> b(2, 44100); b.clear(); for (int i = 0; i < 20000; ++i) { const float v = 0.4f * (float) std::sin(i * 0.02) * std::exp(-i / 6000.0f); b.setSample(0, 4410 + i, v); b.setSample(1, 4410 + i, v); }
      wr->writeFromAudioSampleBuffer(b, 0, b.getNumSamples()); }
    BeatsXProcessor a; juce::String err;
    CHECK(a.loadSampleToTrack(3, wavf, err), "load sample: %s", err.toRawUTF8());
    const auto& s = a.model.tracks[3].sample;
    CHECK(s && a.model.tracks[3].sound == "My Kick", "sample name");
    float pk = 0; for (auto v : s->data) pk = std::max(pk, std::abs(v));
    std::printf("  loaded \"%s\": %.3f s (leading silence trimmed), peak %.3f\n", a.model.tracks[3].sound.toRawUTF8(), s->data.size() / 48000.0, pk);
    CHECK(s->data.size() < 30000 && std::abs(pk - 0.89f) < 0.01f, "trim + normalise");
    a.normaliseSamples = false; a.loadSampleToTrack(4, wavf, err); float pk2 = 0; for (auto v : a.model.tracks[4].sample->data) pk2 = std::max(pk2, std::abs(v));
    CHECK(pk2 < 0.45f && pk2 > 0.3f, "normalise off keeps the level (%.3f)", pk2);
    juce::String msg; const int n = a.loadSamples(6, { wavf.getFullPathName(), wavf.getFullPathName(), wavf.getFullPathName() }, msg);
    CHECK(n == 3 && a.model.tracks[8].sample, "several files fill consecutive tracks");
    auto st = state(a); BeatsXProcessor b; b.restoreState(st.getData(), (int) st.getSize());
    const auto& s2 = b.model.tracks[3].sample; double md = 0;
    if (s2 && s2->data.size() == s->data.size()) for (size_t i = 0; i < s->data.size(); ++i) md = std::max(md, (double) std::abs(s->data[i] - s2->data[i])); else md = 1;
    std::printf("  saved with the project + reloaded: max diff %.6f\n", md);
    CHECK(md < 1.0 / 16000.0, "sample saved with the project");
    auto st2 = state(b); BeatsXProcessor c; c.restoreState(st2.getData(), (int) st2.getSize()); auto st3 = state(c);
    CHECK(st2 == st3, "save -> load -> save is stable (no drift)");
    if (st2 != st3) { auto xa = juce::AudioProcessor::getXmlFromBinary(st2.getData(), (int) st2.getSize()), xb = juce::AudioProcessor::getXmlFromBinary(st3.getData(), (int) st3.getSize());
        if (xa && xb) { auto sa = xa->toString(), sb = xb->toString(); int i = 0; while (i < sa.length() && i < sb.length() && sa[i] == sb[i]) ++i; std::printf("  (debug) sizes %d vs %d, first difference at %d: ...%s... vs ...%s...\n", sa.length(), sb.length(), i, sa.substring(juce::jmax(0, i - 80), i + 40).toRawUTF8(), sb.substring(juce::jmax(0, i - 80), i + 40).toRawUTF8()); } }
}

// ---------------------------------------------------------------- 6. back-compat with old projects
static void testBackCompat() {
    section("back-compat: old projects load + sound identical");
    gbold::DrumLibrary oldLib; oldLib.build(48000.0);
    int kitsOk = 0, bitExact = 0; double worstCorr = 1;
    for (int k = 0; k < (int) gbold::kits().size(); ++k) {
        gbold::Model om; gbold::applyKit(om, gbold::kits()[(size_t) k], true);
        om.patterns[0].steps[2][6].nudge = 17; om.patterns[0].steps[4][3].prob = 60; om.patterns[0].steps[4][3].vel = 80; om.patterns[1].length = 32; om.tracks[1].pitch = 0; om.tracks[4].decay = 0.4f; om.tracks[2].tone = 0.3f; om.tracks[0].drive = 0.3f;   // (pitched playback is deliberately different now: windowed sinc, see the alias test)
        om.patterns[1].steps[3][20].vel = 100; om.patterns[1].steps[3][20].roll = 3;
        // the old processor's state: APVTS + BEATS2
        juce::ValueTree st("GHOSTYBEATS2"); st.setProperty("swing", 0.3, nullptr); st.appendChild(gbold::modelToTree(om), nullptr);
        juce::MemoryBlock mb; if (auto xml = st.createXml()) juce::AudioProcessor::copyXmlToBinary(*xml, mb);
        BeatsXProcessor np; np.restoreState(mb.getData(), (int) mb.getSize());
        bool same = true;
        for (int t = 0; t < 16; ++t) { const auto& a = om.tracks[(size_t) t]; const auto& b = np.model.tracks[(size_t) t]; same = same && a.sound == b.sound && a.pitch == b.pitch && a.decay == b.decay && a.vol == b.vol && a.tone == b.tone && a.drive == b.drive && a.choke == b.choke; }
        for (int pi = 0; pi < 16; ++pi) { same = same && om.patterns[(size_t) pi].length == np.model.patterns[(size_t) pi].length;
            for (int t = 0; t < 16; ++t) for (int s = 0; s < 64; ++s) { const auto& a = om.patterns[(size_t) pi].steps[(size_t) t][(size_t) s]; const auto& b = np.model.patterns[(size_t) pi].steps[(size_t) t][(size_t) s];
                same = same && a.vel == b.vel && a.prob == b.prob && a.roll == b.roll && a.nudge == b.nudge && b.flam == 0 && b.pitch == 0 && b.decay == 100 && b.rate == 0; } }
        same = same && om.song.size() == np.model.song.size();
        // render both: old engine vs new engine, song mode, same knobs
        gbold::Snapshot os; os.tracks = om.tracks; os.patterns = om.patterns; os.songCount = (int) om.song.size(); for (int i = 0; i < os.songCount; ++i) os.song[(size_t) i] = om.song[(size_t) i];
        for (int t = 0; t < 16; ++t) os.sound[(size_t) t] = oldLib.byName(om.tracks[(size_t) t].sound).get();
        // HUMAN stays 0 here: the old engine dropped a hit that HUMAN (or a negative nudge) pulled early across an audio-buffer
        // boundary (it only scanned steps up to the buffer end). The new engine plays it; everything else is bit-identical.
        static int variant = juce::SystemStats::getEnvironmentVariable("COMPAT_VARIANT", "1").getIntValue();
        gbold::EngineParams oP; oP.swing = 0.3f; oP.glue = 0.25f; oP.songMode = true; oP.bpm = 128; oP.human = 0.2f; oP.echo = 0.2f; oP.verb = 0.15f;
        if (variant & 1) oP.human = 0; if (variant & 2) oP.swing = 0; if (variant & 4) { oP.echo = 0; oP.verb = 0; } if (variant & 8) oP.glue = 0; if (variant & 16) oP.songMode = false;
        gbold::BeatEngine oe; oe.prepare(48000, 512); oe.setSnapshot(&os); oe.internalPlay = true;
        auto nP = np.readParams(128); nP.swing = oP.swing; nP.glue = oP.glue; nP.songMode = oP.songMode; nP.bpm = 128; nP.human = oP.human; nP.echo = oP.echo; nP.verb = oP.verb; nP.ceiling = 0; nP.limiter = 1;   // the old limiter top + the v1 tanh stage (a loaded v1 project selects it by itself)
        CHECK(np.model.ver < 4 && (int) np.apvts.getRawParameterValue("limiter")->load() == 1, "a v1 project keeps the v1 output stage");
        gb::BeatEngine ne; ne.prepare(48000, 512); gb::Snapshot ns = *np.makeSpec({ true, false, -1 }).snap; ne.setSnapshot(&ns); ne.internalPlay = true; ne.legacyTiming = true;
        juce::AudioBuffer<float> a(2, 512), b(2, 512); juce::MidiBuffer none; double md = 0, energy = 0; int firstBad = -1;
        juce::AudioBuffer<float> A(1, 48000 * 8), B(1, 48000 * 8);
        for (int blk = 0; blk < 48000 * 8 / 512; ++blk) { oe.process(a, none, oP, 128, 0, false); ne.process(b, none, nP, 128, 0, false); A.copyFrom(0, blk * 512, a, 0, 0, 512); B.copyFrom(0, blk * 512, b, 0, 0, 512);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) { const double d = std::abs(a.getSample(c, i) - b.getSample(c, i)); if (d > 1e-4 && firstBad < 0) firstBad = blk * 512 + i; md = std::max(md, d); energy += a.getSample(c, i) * a.getSample(c, i); } }
        juce::ignoreUnused(firstBad);
        const double cc = corr(A.getReadPointer(0), B.getReadPointer(0), A.getNumSamples()); worstCorr = std::min(worstCorr, cc); if (md < 1e-6) ++bitExact;
        if (same && md < 1e-6 && energy > 1) ++kitsOk; else std::printf("  kit %s: model %s, corr %.5f, max diff %.2e\n", gbold::kits()[(size_t) k].name, same ? "same" : "DIFFERENT", cc, md);
    }
    std::printf("  %d / %d old kits+edits: same model and bit-identical audio vs the old engine (8 s, song mode, swing, glue, echo, reverb; event times rounded the old way)\n", kitsOk, (int) gbold::kits().size());
    juce::ignoreUnused(worstCorr, bitExact);
    CHECK(kitsOk == (int) gbold::kits().size(), "back-compat");
    // a pre-Beats-2 project (8x16 string grid)
    juce::ValueTree v1("GHOSTYBEATS"); v1.setProperty("steps", "1000100010001000|0000100000001000|0010001000100010|0|0|0|0|0", nullptr);
    juce::MemoryBlock mb; if (auto xml = v1.createXml()) juce::AudioProcessor::copyXmlToBinary(*xml, mb);
    BeatsXProcessor p1; p1.restoreState(mb.getData(), (int) mb.getSize());
    CHECK(p1.model.patterns[0].steps[1][4].vel == 105 && p1.model.patterns[0].steps[2][12].vel == 105, "Beats 1 project import");
}

// ---------------------------------------------------------------- 7. undo / redo
static void testUndo(BeatsXProcessor& p) {
    section("undo / redo");
    p.applyKit(1, true); while (p.undo()) {}
    std::vector<juce::MemoryBlock> states { state(p) };
    for (int i = 0; i < 35; ++i) { p.pushUndo(); auto& st = p.model.patterns[0].steps[(size_t) (i % 16)][(size_t) (i * 3 % 64)]; st.vel = (uint8_t) (st.vel ? 0 : 100); if (i % 5 == 0) p.model.tracks[2].sat = i * 0.02f; if (i % 7 == 0) p.generatePattern(i % 2); p.publish(); states.push_back(state(p)); }
    bool ok = true; for (int i = 34; i >= 0; --i) { ok = ok && p.undo() && state(p) == states[(size_t) i]; }
    bool ok2 = true; for (int i = 1; i <= 35; ++i) { ok2 = ok2 && p.redo() && state(p) == states[(size_t) i]; }
    std::printf("  35 edits: undo %s, redo %s (byte-identical state)\n", ok ? "ok" : "BAD", ok2 ? "ok" : "BAD");
    CHECK(ok && ok2, "undo/redo byte-identical");
}

// ---------------------------------------------------------------- 8. add-on: chop, evolve, DNA, breed, rules, macros, bake
static void testRhythm(BeatsXProcessor& p) {
    section("break chop on synthetic breaks (onsets known)");
    auto lib = p.lib; int totalOn = 0, found = 0, extras = 0; double worst = 0;
    for (int b = 0; b < 4; ++b) {
        juce::Random R(100 + b); const char* snd[] { "Club Kick Punch", "Dubstep Snare Crack", "Closed Hat Crisp", "Tom Deep", "Lofi Snare Dusty" };
        auto brk = std::make_shared<gb::Sound>(); brk->rate = 48000; brk->data.assign(48000 * 3, 0.0f); std::vector<int> on;
        int t = 2000; while (t < 48000 * 3 - 20000) { on.push_back(t); auto s = lib->byName(snd[R.nextInt(5)]); const float v = 0.5f + R.nextFloat() * 0.5f;
            const int len = juce::jmin((int) s->data.size(), 48000 * 3 - t); for (int k = 0; k < len; ++k) brk->data[(size_t) (t + k)] += s->data[(size_t) k] * v; t += 4000 + R.nextInt(8000); }   // 83..250 ms apart, overlapping tails
        auto sl = gb::rhythm::chop(*brk, 0.5f);
        if (juce::SystemStats::getEnvironmentVariable("CHOPDBG", "").isNotEmpty()) { std::printf("  break %d onsets:", b); for (int o : on) std::printf(" %d", o / 48); std::printf("\n  slices (ms):"); for (int s : sl) std::printf(" %d", s / 48); std::printf("\n"); }
        int extra = 0; for (int s : sl) { double best = 1e9; for (int o : on) best = std::min(best, (double) std::abs(s - o)); if (best > 480 && s > 0) ++extra; } extras += extra;
        for (int o : on) { double best = 1e9; for (int s : sl) best = std::min(best, (double) std::abs(s - o)); ++totalOn; if (best <= 480) ++found; worst = std::max(worst, best / 48.0); }
    }
    std::printf("  %d / %d onsets found within 10 ms (worst %.1f ms), %d extra slices\n", found, totalOn, worst, extras);
    CHECK(found >= totalOn * 95 / 100, "chop accuracy");
    // a built-in break: chop + extracted rhythm on a track, then REARRANGE
    p.applyKit(10, true); p.setTrackSound(15, "Break Rolling");
    const int sl = (int) p.model.tracks[15].slices.size(); int hits = 0; for (int s = 0; s < 64; ++s) if (p.model.patterns[0].steps[15][(size_t) s].vel) ++hits;
    std::printf("  \"Break Rolling\": %d slices, %d hits laid on the track\n", sl, hits);
    CHECK(sl >= 8 && sl <= 64 && hits >= 8, "built-in break chop");
    p.rearrangeTrack(15); auto roles = p.sliceRolesOf(15); int kOnBeat = 0, beats = 0;
    for (int s = 0; s < p.model.patterns[0].lenOf(15); s += 8) if (auto& st = p.model.patterns[0].steps[15][(size_t) s]; st.vel) { ++beats; if (roles[(size_t) st.slice - 1] == gb::RoleKick) ++kOnBeat; }
    std::printf("  REARRANGE: %d of %d strong-beat hits use kick-like slices\n", kOnBeat, beats);

    section("EVOLVE");
    gb::Snapshot S; auto click = makeSound("c", 32, [](int i) { return i < 4 ? 1.0f : 0.0f; });
    for (int t = 0; t < 4; ++t) { S.sound[(size_t) t] = click.get(); S.tracks[(size_t) t].vol = 1; }
    S.role = { gb::RoleKick, gb::RoleSnare, gb::RoleHat, gb::RolePerc };
    auto& P0 = S.patterns[0]; P0.length = 16; for (int s = 0; s < 16; s += 4) P0.steps[0][(size_t) s].vel = 110; P0.steps[1][4].vel = 110; P0.steps[1][12].vel = 110;
    for (int s = 0; s < 16; s += 2) P0.steps[2][(size_t) s].vel = 90; P0.steps[3][3].vel = 70; P0.steps[3][11].vel = 70;
    P0.evolve = 0.6f; P0.evolveStyle = 0; P0.seed = 7;
    auto events = [&](const gb::Snapshot& s, const gb::EngineParams& prm, double beats) { std::vector<std::tuple<int, long, int>> ev; gb::collectEvents(s, prm, 120, 0, beats, [&](int t, double b, float v, const gb::EvInfo&) { ev.push_back({ t, std::lround(b * 960), (int) std::lround(v * 127) }); }); std::sort(ev.begin(), ev.end()); return ev; };
    auto prm = cleanParams(120);
    auto e1 = events(S, prm, 64), e2 = events(S, prm, 64);
    std::set<std::vector<std::pair<int, long>>> barSet; int anchorsMissing = 0;
    for (int bar = 0; bar < 16; ++bar) { std::vector<std::pair<int, long>> sig; for (auto& e : e1) if (std::get<1>(e) / 3840 == bar) sig.push_back({ std::get<0>(e), std::get<1>(e) % 3840 }); barSet.insert(sig);
        for (long a : { 0L, 960L, 1920L, 2880L }) if (std::find(sig.begin(), sig.end(), std::make_pair(0, a)) == sig.end()) ++anchorsMissing;
        for (long a : { 960L, 2880L }) if (std::find(sig.begin(), sig.end(), std::make_pair(1, a)) == sig.end()) ++anchorsMissing; }
    std::printf("  INTRICATE 0.6: %d distinct bars of 16, anchors missing %d, seeded repeat %s\n", (int) barSet.size(), anchorsMissing, e1 == e2 ? "identical" : "DIFFERENT");
    CHECK(barSet.size() >= 10 && anchorsMissing == 0 && e1 == e2, "evolve intricate");
    S.patterns[0].seed = 8; CHECK(events(S, prm, 64) != e1, "different seed -> different variations");
    S.patterns[0].evolveStyle = 1; S.patterns[0].evolve = 0.7f; S.patterns[0].fillEvery = 4; auto mg = events(S, prm, 64);
    int retr = 0; for (size_t i = 1; i < mg.size(); ++i) if (std::get<0>(mg[i]) == std::get<0>(mg[i - 1]) && std::get<1>(mg[i]) - std::get<1>(mg[i - 1]) <= 60) ++retr;
    std::printf("  MACHINE GUN 0.7 + fill every 4: %d events, %d hits closer than a 1/64\n", (int) mg.size(), retr);
    CHECK(retr > 20, "machine gun retriggers");
    // macros neutral = as written; rules
    { gb::Snapshot Z = S; Z.patterns[0].evolve = 0; Z.patterns[0].fillEvery = 0; auto plain = events(Z, prm, 16); auto prm2 = prm; prm2.memory = 0.5f; prm2.density = 0.5f; prm2.gravity = 0.5f;
      CHECK(events(Z, prm2, 16) == plain, "neutral macros");
      auto dense = prm; dense.density = 1.0f; auto sparse = prm; sparse.density = 0.0f; const auto nd = events(Z, dense, 16).size(), ns = events(Z, sparse, 16).size();
      std::printf("  DENSITY: %d events at 0, %d plain, %d at 1\n", (int) ns, (int) plain.size(), (int) nd); CHECK(ns < plain.size() && nd > plain.size(), "density macro");
      auto fr = prm; fr.fracture = 1.0f; const auto nf = events(Z, fr, 16).size(); std::printf("  FRACTURE 1: %d events (fragments)\n", (int) nf); CHECK(nf > plain.size() * 2, "fracture");
      Z.tracks[3].rules[0] = { gb::RuleBarOfN, 1, 4, 100 }; auto r = events(Z, prm, 64); bool onlyBar = true; for (auto& e : r) if (std::get<0>(e) == 3 && (std::get<1>(e) / 3840) % 4 != 1) onlyBar = false;
      CHECK(onlyBar, "rule: play only on bar 2 of 4");
      Z.tracks[3].rules[0] = { gb::RuleGhostAfter, 1, 1, 100 }; auto g = events(Z, prm, 16); int ghosts = 0; for (auto& e : g) if (std::get<0>(e) == 3 && std::get<2>(e) < 40) ++ghosts;
      CHECK(ghosts >= 2, "rule: ghost after snare (%d)", ghosts); }

    section("RHYTHM DNA / KEEP / BLEND / BREED");
    p.applyKit(0, true); p.generatePattern(0);
    const auto src = p.model.patterns[0]; const auto rl = p.roles(); const auto locks = p.lockedTracks();
    for (float keep : { 0.5f, 0.8f }) {
        auto a = gb::rhythm::keepIdentity(src, rl, locks, keep, 11), b = gb::rhythm::keepIdentity(src, rl, locks, keep, 11);
        const float ov = gb::rhythm::overlap(a, src); const float d = gb::rhythm::distance(gb::rhythm::extract(a, rl), gb::rhythm::extract(src, rl));
        std::printf("  KEEP %.0f%%: overlap %.2f, hits %d vs %d, DNA distance %.2f, seeded %s\n", keep * 100, ov, gb::rhythm::hitCount(a), gb::rhythm::hitCount(src), d, gb::rhythm::hamming(a, b) == 0 ? "repeatable" : "NOT");
        CHECK(std::abs(ov - keep) < 0.15f && gb::rhythm::hamming(a, b) == 0, "keep identity %.1f", keep);
    }
    { gb::Pattern B = src; gb::rhythm::generate(B, rl, locks, 1, 99); auto m = gb::rhythm::blend(src, B, rl, locks, 0.5f, 5);
      const auto da = gb::rhythm::distance(gb::rhythm::extract(m, rl), gb::rhythm::extract(src, rl)), db = gb::rhythm::distance(gb::rhythm::extract(m, rl), gb::rhythm::extract(B, rl)), dab = gb::rhythm::distance(gb::rhythm::extract(src, rl), gb::rhythm::extract(B, rl));
      std::printf("  BLEND 50%%: distance to A %.2f, to B %.2f (A-B %.2f)\n", da, db, dab); CHECK(da < dab && db < dab, "blend sits between"); }
    p.breedPool.clear(); p.breedNext(16); CHECK(p.breedPool.size() == 16, "breed 16 tiles");
    for (int i : { 2, 5, 9 }) p.breedStar[(size_t) i] = true;
    std::vector<gb::Pattern> starred { p.breedPool[2], p.breedPool[5], p.breedPool[9] }; auto mean = gb::rhythm::extract(starred[0], rl);
    for (size_t i = 1; i < starred.size(); ++i) mean = gb::rhythm::lerp(mean, gb::rhythm::extract(starred[i], rl), 1.0f / (float) (i + 1));
    double d0 = 0; for (auto& c : p.breedPool) d0 += gb::rhythm::distance(gb::rhythm::extract(c, rl), mean); d0 /= (double) p.breedPool.size();
    p.breedNext(16); double d1 = 0; for (auto& c : p.breedPool) d1 += gb::rhythm::distance(gb::rhythm::extract(c, rl), mean); d1 /= (double) p.breedPool.size();
    std::printf("  BREED: gen 0 mean distance to the starred %.2f -> gen 1 %.2f\n", d0, d1); CHECK(d1 < d0, "breed leans toward favourites");

    section("evolving performance: export == live playback == baked song");
    p.applyKit(10, true); auto& ep = p.model.patterns[0]; ep.evolve = 0.5f; ep.evolveStyle = 1; ep.fillEvery = 4; p.evolveBars = 16; p.publish();
    if (auto* g = p.apvts.getParameter("glue")) g->setValueNotifyingHost(0.0f);
    auto spec = p.makeSpec({ false, false, -1 }); const int bars = spec.bars;
    auto exported = readWav(p.renderSpec(spec, nullptr)[0]);
    auto live = render(*spec.snap, spec.P, bars * 4 * 60.0 / spec.P.bpm, -1, 3);
    double md = 0; int fb = -1; for (int c = 0; c < 2; ++c) for (int i = 0; i < live.getNumSamples(); ++i) { const double d = std::abs(live.getSample(c, i) - exported.getSample(c, i)); if (d > 1e-4 && (fb < 0 || i < fb)) fb = i; md = std::max(md, d); }
    { auto l512 = render(*spec.snap, spec.P, bars * 4 * 60.0 / spec.P.bpm, 512); double m2 = 0; for (int i = 0; i < l512.getNumSamples(); ++i) m2 = std::max(m2, (double) std::abs(l512.getSample(0, i) - exported.getSample(0, i)));
      std::printf("  (debug) first diff at %d, fixed-512 live vs export %.2e, export len %d\n", fb, m2, exported.getNumSamples()); }
    std::printf("  %d evolving bars: export vs live (random host block sizes) max diff %.2e (24-bit quantisation ~6e-8)\n", bars, md);
    CHECK(bars == 16 && md < 1e-5, "export matches live");
    p.pushUndo(); const int made = p.bakePerformance(16); auto spec2 = p.makeSpec({ true, false, -1 }); auto baked = render(*spec2.snap, spec2.P, bars * 4 * 60.0 / spec.P.bpm, 512);
    // (the song loops now: the next pass's first hits can start a hair before bar 17 (swing / nudge / human) -- compared up to the last 16th)
    const int cmpN = baked.getNumSamples() - (int) std::lround(0.25 * 60.0 / spec.P.bpm * 48000.0);
    double mb = 0; int mbAt = -1; for (int c = 0; c < 2; ++c) for (int i = 0; i < cmpN; ++i) { const double d = std::abs(live.getSample(c, i) - baked.getSample(c, i)); if (d > mb) { mb = d; mbAt = i; } }
    std::printf("  (worst diff at sample %d of %d)\n", mbAt, baked.getNumSamples());
    std::printf("  baked into %d patterns + song: max diff vs live %.2e\n", made, mb); CHECK(made >= 2 && mb < 1e-5, "bake == live");   // (128-step patterns: 8 bars of 1/16 each)
    p.undo();
}

// ---------------------------------------------------------------- loudness (ITU-R BS.1770-4 integrated, 48 kHz)
static double lufs(const juce::AudioBuffer<float>& b) {
    const int n = b.getNumSamples(), chs = b.getNumChannels(); std::vector<double> z;
    std::vector<std::vector<double>> y((size_t) chs, std::vector<double>((size_t) n));
    for (int c = 0; c < chs; ++c) {
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0, u1 = 0, u2 = 0, w1 = 0, w2 = 0;
        for (int i = 0; i < n; ++i) { const double x = b.getSample(c, i);
            const double s1 = 1.53512485958697 * x - 2.69169618940638 * x1 + 1.19839281085285 * x2 + 1.69065929318241 * y1 - 0.73248077421585 * y2; x2 = x1; x1 = x; y2 = y1; y1 = s1;
            const double s2 = s1 - 2 * u1 + u2 + 1.99004745483398 * w1 - 0.99007225036621 * w2; u2 = u1; u1 = s1; w2 = w1; w1 = s2; y[(size_t) c][(size_t) i] = s2; }
    }
    const int blk = 19200, hop = 4800;
    for (int a = 0; a + blk <= n; a += hop) { double s = 0; for (int c = 0; c < chs; ++c) for (int i = a; i < a + blk; ++i) s += y[(size_t) c][(size_t) i] * y[(size_t) c][(size_t) i]; z.push_back(s / blk); }
    auto L = [](double v) { return -0.691 + 10 * std::log10(v + 1e-20); };
    double sum = 0; int k = 0; for (double v : z) if (L(v) > -70) { sum += v; ++k; } if (!k) return -100;
    const double rel = L(sum / k) - 10; sum = 0; k = 0; for (double v : z) if (L(v) > -70 && L(v) > rel) { sum += v; ++k; }
    return k ? L(sum / k) : -100;
}
static float peakOf(const juce::AudioBuffer<float>& b) { float pk = 0; for (int c = 0; c < b.getNumChannels(); ++c) pk = std::max(pk, b.getMagnitude(c, 0, b.getNumSamples())); return pk; }

static void testLevels() {
    section("levels: every kit at default settings (pattern A, 8 bars) -> about -14 LUFS, peaks <= -1 dBFS");
    BeatsXProcessor p; double worst = 0; float worstPk = 0; int ok = 0;
    for (int k = 0; k < (int) gb::allKits().size(); ++k) {
        p.applyKit(k, true); auto spec = p.makeSpec({ false, false, -1 });
        auto b = render(*spec.snap, spec.P, 8 * 4 * 60.0 / spec.P.bpm); const double l = lufs(b); const float pk = peakOf(b);
        worst = std::max(worst, std::abs(l + 14)); worstPk = std::max(worstPk, pk);
        if (std::abs(l + 14) <= 1.5 && pk <= 0.8913f) ++ok; else std::printf("  %-18s %6.1f LUFS  peak %5.1f dBFS\n", gb::allKits()[(size_t) k].name, l, juce::Decibels::gainToDecibels(pk));
        if (juce::SystemStats::getEnvironmentVariable("LEVELS_ALL", "").isNotEmpty()) std::printf("  %-18s %6.1f LUFS  peak %5.1f dBFS  (trim needed %+.1f dB)\n", gb::allKits()[(size_t) k].name, l, juce::Decibels::gainToDecibels(pk), -14 - l);
    }
    std::printf("  %d / %d kits within -14 +-1.5 LUFS (worst %.1f LU off), highest peak %.2f dBFS, bus clipper off by default\n", ok, (int) gb::allKits().size(), worst, juce::Decibels::gainToDecibels(worstPk));
    CHECK(ok == (int) gb::allKits().size(), "kit levels");
    CHECK(p.apvts.getRawParameterValue("clip")->load() == 0.0f, "clipper off by default");
}

// ---------------------------------------------------------------- phrase locks + motifs
static void testLocks(BeatsXProcessor& p) {
    section("PHRASE LOCKS + MOTIFS");
    auto click = makeSound("c", 32, [](int i) { return i < 4 ? 1.0f : 0.0f; });
    gb::Snapshot S; for (int t = 0; t < 4; ++t) { S.sound[(size_t) t] = click.get(); S.tracks[(size_t) t].vol = 1; }
    S.role = { gb::RoleKick, gb::RoleSnare, gb::RoleHat, gb::RolePerc };
    auto& P0 = S.patterns[0]; for (int s = 0; s < 16; s += 4) P0.steps[0][(size_t) s].vel = 110; P0.steps[1][4].vel = 110; P0.steps[1][12].vel = 110;
    for (int s = 0; s < 16; s += 2) P0.steps[2][(size_t) s].vel = 90; P0.steps[3][3].vel = 70;
    P0.evolve = 0.9f; P0.evolveStyle = 1; P0.fillEvery = 2; P0.seed = 3;
    auto bars = [&](const gb::Snapshot& snap) { std::map<long, std::vector<std::tuple<int, long, int>>> m; gb::collectEvents(snap, cleanParams(120), 120, 0, 64, [&](int t, double b, float v, const gb::EvInfo&) { m[(long) std::floor(b / 4)].push_back({ t, std::lround(std::fmod(b, 4.0) * 960), (int) std::lround(v * 127) }); }); for (auto& kv : m) std::sort(kv.second.begin(), kv.second.end()); return m; };
    gb::Snapshot plain = S; plain.patterns[0].evolve = 0; plain.patterns[0].fillEvery = 0; const auto asWritten = bars(plain)[0];
    S.locks[0] = { 4, 4, 0xffff }; const auto ev = bars(S);
    int lockedSame = 0, freeDiff = 0; for (long b = 0; b < 16; ++b) { const bool locked = b >= 4 && b < 8; auto it = ev.find(b); const auto got = it == ev.end() ? std::vector<std::tuple<int, long, int>>() : it->second;
        // the first locked bar may also hear a retrigger tail running over from the (free) bar before it
        const bool same = b == 4 ? std::includes(got.begin(), got.end(), asWritten.begin(), asWritten.end()) : got == asWritten;
        if (locked && same) ++lockedSame; if (!locked && got != asWritten) ++freeDiff; }
    std::printf("  lock bars 5-8: %d / 4 locked bars exactly as written, %d / 12 free bars evolve\n", lockedSame, freeDiff);
    CHECK(lockedSame == 4 && freeDiff >= 9, "phrase lock under EVOLVE");
    S.locks[0] = {}; S.motifs[0].track = 2; S.motifs[0].start = 0; S.motifs[0].len = 4; S.motifs[0].steps[0].vel = 120; S.motifs[0].steps[1].vel = 0; S.motifs[0].steps[2].vel = 60; S.motifs[0].steps[3].vel = 60; S.motifs[0].steps[3].roll = 2;
    const auto mv = bars(S); int keep = 0;
    for (long b = 0; b < 16; ++b) { std::set<long> at; auto it = mv.find(b); if (it != mv.end()) for (auto& e : it->second) if (std::get<0>(e) == 2) at.insert(std::get<1>(e));
        if (at.count(0) && !at.count(240) && at.count(480) && at.count(720) && at.count(840)) ++keep; }   // hits at 1, 3, 4 (+ its roll), nothing on 2
    std::printf("  motif (hat, 1st beat): kept in %d / 16 evolving bars\n", keep); CHECK(keep == 16, "motif under EVOLVE");
    // generators: GENERATE / KEEP / BLEND / BREED keep a locked phrase and a motif
    p.applyKit(10, true); auto& pt = p.model.patterns[0]; pt.length = 64;
    for (int s = 16; s < 64; ++s) for (int t = 0; t < 8; ++t) pt.steps[(size_t) t][(size_t) s] = pt.steps[(size_t) t][(size_t) (s % 16)];
    p.model.locks[0] = { 1, 1, 0xffff }; p.captureMotif(1, 8, 4); p.publish();
    const auto before = pt;
    auto phraseKept = [&](const gb::Pattern& q) { for (int t = 0; t < gb::kTracks; ++t) for (int s = 16; s < 32; ++s) { const auto& a = before.steps[(size_t) t][(size_t) s]; const auto& b2 = q.steps[(size_t) t][(size_t) s];
        if (t == 1 && s % 16 >= 8 && s % 16 < 12) continue; if (a.vel != b2.vel || (b2.flags & 8)) return false; } return true; };
    auto motifKept = [&](const gb::Pattern& q) { for (int b = 0; b < 4; ++b) for (int k = 0; k < 4; ++k) if (q.steps[1][(size_t) (b * 16 + 8 + k)].vel != before.steps[1][(size_t) (8 + k)].vel) return false; return true; };
    p.keepIdentity(0.3f); const bool k1 = phraseKept(pt) && motifKept(pt);
    p.generatePattern(1); const bool k2 = motifKept(pt);
    p.breedPool.clear(); p.breedNext(12); bool k3 = true; for (auto& c : p.breedPool) k3 = k3 && motifKept(c);
    std::printf("  KEEP 30%%: phrase + motif kept %s; GENERATE: motif kept %s; BREED: motif in all 12 tiles %s\n", k1 ? "yes" : "NO", k2 ? "yes" : "NO", k3 ? "yes" : "NO");
    CHECK(k1 && k2 && k3, "locks in generators");
    p.model.locks = {}; p.model.motifs = {}; p.publish();
}

// ---------------------------------------------------------------- destruction phase 2
static void testDestruction2(BeatsXProcessor& p) {
    section("SPECTRAL FREEZE + FREQUENCY-DEPENDENT REVERSE (offline, cached, seeded)");
    auto snd = p.lib->byName("Dubstep Snare Crack");
    auto f1 = gb::destruct::freeze(*snd, 7), f2 = gb::destruct::freeze(*snd, 7);
    CHECK(f1 && f2 && f1->data == f2->data, "freeze is reproducible");
    // stationary after the freeze point: 100 ms windows from 0.25 s on
    std::vector<double> e; for (int w = 0; w < 8; ++w) { double a = 0; const int s0 = 12000 + w * 4800; for (int i = s0; i < s0 + 4800; ++i) a += f1->data[(size_t) i] * f1->data[(size_t) i]; e.push_back(10 * std::log10(a / 4800 + 1e-20)); }
    const double spread = *std::max_element(e.begin(), e.end()) - *std::min_element(e.begin(), e.end());
    std::printf("  freeze: %.2f s long, held texture level spread %.2f dB over 0.8 s, source decays %.1f dB over the same span\n", f1->data.size() / 48000.0, spread,
        10 * std::log10((std::pow(snd->data[(size_t) std::min<size_t>(12100, snd->data.size() - 1)], 2) + 1e-12) / (std::pow(snd->data[(size_t) std::min<size_t>(snd->data.size() - 1, 15000)], 2) + 1e-12)));
    CHECK(f1->data.size() > 48000 * 2 && spread < 3.0, "freeze holds a stationary spectrum");
    {   // a synthetic hit: a 100 Hz body decaying slowly + a 7 kHz click at the start. Reverse the low band only / the high band only.
        auto syn = makeSound("syn", 24000, [](int i) { const double t = i / 48000.0; return (float) (0.6 * std::sin(juce::MathConstants<double>::twoPi * 100 * t) * std::exp(-t / 0.12)
                                                                                            + 0.4 * std::sin(juce::MathConstants<double>::twoPi * 7000 * t) * std::exp(-t / 0.01)); });
        auto band = [](const gb::Sound& x, bool high, int a, int b) { gb::synth::Buf v = x.data; for (int k = 0; k < 4; ++k) gb::synth::filt(v, 48000, high ? 2 : 0, high ? 3000.0 : 300.0, 0.707); double e = 0; for (int i = a; i < b; ++i) e += v[(size_t) i] * v[(size_t) i]; return e + 1e-12; };
        for (int mask : { 1, 4, 5 }) {
            auto r = gb::destruct::bandReverse(*syn, mask); const int n = (int) r->data.size();
            const double loFirst = band(*r, false, 200, 4800), loLast = band(*r, false, n - 4800, n - 200), hiFirst = band(*r, true, 200, 4800), hiLast = band(*r, true, n - 4800, n - 200);
            const bool loRev = loLast > loFirst * 4, hiRev = hiLast > hiFirst * 4, loFwd = loFirst > loLast * 4, hiFwd = hiFirst > hiLast * 4;
            const bool ok = ((mask & 1) ? loRev : loFwd) && ((mask & 4) ? hiRev : hiFwd);
            if (juce::SystemStats::getEnvironmentVariable("BANDDBG", "").isNotEmpty()) std::printf("    lo %.3g %.3g hi %.3g %.3g\n", loFirst, loLast, hiFirst, hiLast);
            std::printf("  band reverse mask %d: low band %s, high band %s\n", mask, loRev ? "backwards" : (loFwd ? "forwards" : "?"), hiRev ? "backwards" : (hiFwd ? "forwards" : "?"));
            CHECK(ok, "band reverse %d", mask);
        }
        auto a1 = gb::destruct::bandReverse(*syn, 3), a2 = gb::destruct::bandReverse(*syn, 3); CHECK(a1->data == a2->data, "band reverse reproducible");
        auto all = gb::destruct::bands(*syn); double md = 0; for (int i = 0; i < 24000; ++i) md = std::max(md, (double) std::abs(all[0][(size_t) i] + all[1][(size_t) i] + all[2][(size_t) i] - syn->data[(size_t) i]));
        CHECK(md < 1e-5, "bands add back up to the hit (%.2e)", md);
    }
    // in the engine: a step with FREEZE 30 (300 ms) holds, then fades out
    auto S = oneTrack(snd.get()); auto fz = gb::destruct::freeze(*snd, 1); S.variant[0][0] = fz.get(); auto& st = S.patterns[0].steps[0][0]; st.vel = 127; st.freeze = 30;
    auto out = render(S, cleanParams(120), 0.6); auto lvl = [&](int a, int b) { float m = 0; for (int i = a; i < b; ++i) m = std::max(m, std::abs(out.getSample(0, i))); return m; };
    std::printf("  engine: level 200-280 ms %.3f, after 300 ms + 60 ms fade %.5f\n", lvl(9600, 13400), lvl(17400, 20000));
    CHECK(lvl(9600, 13400) > 0.01f && lvl(17400, 20000) < 1e-4f, "freeze hold + fade in the engine");
    // variants come from a worker thread (never the message or audio thread): a step with FREEZE on every track asks for them, publish() picks them up
    p.applyKit(10, true); for (int t = 0; t < 16; ++t) p.model.patterns[0].steps[(size_t) t][0].freeze = 10;
    const auto t0 = juce::Time::getMillisecondCounterHiRes(); p.publish(); const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
    int waited = 0; while (p.variantsMissing() && waited < 20000) { juce::MessageManager::getInstance()->runDispatchLoopUntil(20); waited += 20; }
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    auto spec = p.makeSpec({ false, false, -1 }); int have = 0; for (int t = 0; t < 16; ++t) if (spec.snap->sound[(size_t) t] && spec.snap->variant[(size_t) t][0] && spec.snap->variant[(size_t) t][7]) ++have;
    std::printf("  publish() that asked for 16 x 8 variants took %.1f ms on the message thread; the worker delivered %d tracks' variants after ~%d ms\n", ms, have, waited); CHECK(have >= 15 && ms < 60, "variant cache");
}

// ---------------------------------------------------------------- scripts

// (a look inside a saved beat: GHOSTY_BEAT=file.ghostbeat BeatsTests test showbeat prints patterns A..C as scripts)
static void testShowBeat() {
    const char* f = std::getenv("GHOSTY_BEAT"); if (! f) return; BeatsXProcessor p; juce::MemoryBlock mb; juce::File(juce::String::fromUTF8(f)).loadFileAsData(mb);
    p.restoreState(mb.getData(), (int) mb.getSize(), true);
    for (int i = 0; i < 3; ++i) std::printf("%s\n", gb::script::beatAsScript(p.model, i, juce::String::charToString((juce::juce_wchar) ('A' + i))).toRawUTF8());
}

// the grid edits like FL (Brandon 2026-11-21): a click selects a note (never deletes it); drag moves; shift-drag copies; cmd+C / cmd+V; right-click deletes
static void testGridEdit() {
    std::printf("[gridedit]\n");
    BeatsXProcessor p; p.applyKit(0, true); std::unique_ptr<juce::AudioProcessorEditor> e(p.createEditor()); e->setSize(1200, 760); e->setVisible(true);
    for (int i = 0; i < 3; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    bx::StepGrid* g = nullptr; std::function<void(juce::Component&)> find = [&](juce::Component& c) { if (auto* x = dynamic_cast<bx::StepGrid*>(&c)) g = x; for (auto* ch : c.getChildren()) find(*ch); }; find(*e);
    CHECK(g != nullptr, "found the grid"); if (! g) return;
    if (g->getWidth() < 100) g->setBounds(0, 0, 640, 320);
    auto& P = g->ed.pat(); for (auto& row : P.steps) for (auto& st : row) st = gb::Step(); P.length = 16; for (auto& l : P.trackLen) l = 0; p.publish();
    auto at = [&](int t, int s) { return juce::Point<float>((s % 16 + 0.5f) * g->colW(), (t + 0.5f) * g->rowH()); };
    auto src = juce::Desktop::getInstance().getMainMouseSource(); const auto now = juce::Time::getCurrentTime();
    auto ev = [&](juce::Point<float> pos, juce::Point<float> down, juce::ModifierKeys m) { return juce::MouseEvent(src, pos, m, 1.0f, 0, 0, 0, 0, g, g, now, down, now, 1, pos != down); };
    auto click = [&](int t, int s, juce::ModifierKeys m) { auto q = at(t, s); g->mouseDown(ev(q, q, m)); g->mouseUp(ev(q, q, m)); };
    auto drag = [&](int t, int s, int t2, int s2, juce::ModifierKeys m) { auto a = at(t, s), b = at(t2, s2); g->mouseDown(ev(a, a, m)); g->mouseDrag(ev(b, a, m)); g->mouseUp(ev(b, a, m)); };
    const auto L = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier);
    click(1, 0, L); CHECK(P.steps[1][0].vel == 100, "a click on an empty cell draws a note");
    click(1, 0, L); CHECK(P.steps[1][0].vel == 100 && g->sel.size() == 1, "a click on a note selects it (and keeps it)");
    drag(1, 0, 2, 3, L); CHECK(P.steps[1][0].vel == 0 && P.steps[2][3].vel == 100, "dragging a note moves it (down a track and along)");
    drag(2, 3, 2, 6, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier)); CHECK(P.steps[2][3].vel == 100 && P.steps[2][6].vel == 100, "shift-drag copies it");
    click(2, 3, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::commandModifier));
    CHECK(g->sel.size() == 2, "cmd-click adds a second note to the selection (%d)", (int) g->sel.size());
    g->keyPressed(juce::KeyPress('c', juce::ModifierKeys::commandModifier, 'c'));
    click(5, 8, L); CHECK(g->sel.empty() && P.steps[5][8].vel == 0, "with notes selected, a click on an empty spot only deselects (and marks the paste spot)");
    g->keyPressed(juce::KeyPress('v', juce::ModifierKeys::commandModifier, 'v'));
    CHECK(P.steps[5][8].vel == 100 && P.steps[5][11].vel == 100, "cmd+V pastes the copied notes at the clicked spot, same spacing");
    click(5, 8, juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier)); CHECK(P.steps[5][8].vel == 0, "right-click deletes a note");
    g->sel = { { 5, 11 } }; g->keyPressed(juce::KeyPress(juce::KeyPress::backspaceKey)); CHECK(P.steps[5][11].vel == 0, "backspace deletes the selection");
    p.undo(); CHECK(P.steps[5][11].vel == 100, "UNDO brings it back");
}

// SCRIPTS: THIS BEAT (Brandon 2026-11-21): the pattern -> script -> pattern round trip is exact, and edits to a picture or a rule land in the pattern
static void testScriptBeat() {
    std::printf("[scriptbeat]\n");
    BeatsXProcessor p; int kits = 0;
    for (int k = 0; k < 8; ++k) {
        p.applyKit(k, true); auto& pt = p.model.patterns[0];
        if (k == 1) { pt.steps[2][3].roll = 3; pt.steps[2][3].rate = 8; pt.steps[2][3].accel = 30; pt.steps[2][3].vel = 90; pt.steps[1][5].vel = 70; pt.steps[1][5].pitch = -5; pt.steps[1][5].prob = 60; pt.steps[1][5].decay = 40; pt.steps[1][5].freeze = 12; pt.steps[1][5].flags = 1; pt.trackLen[3] = 12; }
        const auto txt = gb::script::beatAsScript(p.model, 0, "A");
        auto copy = pt; juce::String err; const bool ok = gb::script::beatFromScript(txt, copy, p.roles(), err);
        CHECK(ok, "kit %d: the generated script reads back (%s)", k, err.toRawUTF8());
        int diff = 0; for (int t = 0; t < gb::kTracks; ++t) for (int i = 0; i < gb::kMaxSteps; ++i) if (std::memcmp(&copy.steps[(size_t) t][(size_t) i], &pt.steps[(size_t) t][(size_t) i], sizeof(gb::Step)) != 0) ++diff;
        CHECK(diff == 0, "kit %d: round trip changes %d steps", k, diff); ++kits;
    }
    // edits: a picture change adds a hit; a rule change sets a velocity; a deleted rule removes hits
    p.applyKit(0, true); auto& pt = p.model.patterns[0]; for (auto& row : pt.steps) for (auto& st : row) st = gb::Step(); pt.length = 16;
    pt.steps[0][0].vel = 110; pt.steps[0][8].vel = 110; pt.steps[1][4].vel = 100;
    auto txt = gb::script::beatAsScript(p.model, 0, "A");
    auto edited = txt.replace("#   x . . . | . . . . | x . . .", "#   x . x . | . . . . | x . . .");   // kick picture: a hit on step 2
    CHECK(edited != txt, "the kick picture is where the test expects it");
    edited = edited.replace("if track == 2 and step == 4 then hit(100)", "if track == 2 and step == 4 then hit(50)");
    auto copy = pt; juce::String err; CHECK(gb::script::beatFromScript(edited, copy, p.roles(), err), "edited script reads (%s)", err.toRawUTF8());
    CHECK(copy.steps[0][2].vel == 100 && copy.steps[0][0].vel == 110 && copy.steps[0][8].vel == 110, "picture x -> a new hit, the others stay");
    CHECK(copy.steps[1][4].vel == 50, "a rule's hit(50) sets the velocity (picture left alone)");
    auto cleared = pt; CHECK(gb::script::beatFromScript("if track == 1 and step % 4 == 0 then hit(90)\n", cleared, p.roles(), err), "a hand-written rule reads");
    CHECK(cleared.steps[0][4].vel == 90 && cleared.steps[0][12].vel == 90 && cleared.steps[1][4].vel == 0, "a rule written by hand makes the beat; tracks it leaves out go quiet");
    auto bad = pt; CHECK(! gb::script::beatFromScript("if track == then hit(1)\n", bad, p.roles(), err) && bad.steps[0][0].vel == 110, "an error leaves the beat as it was");
    std::printf("  %d kits round-tripped\n", kits);
}

static void testScripts(BeatsXProcessor& p) {
    section("SCRIPTS");
    for (auto& ex : gb::script::examples()) { auto pr = gb::script::Compiler::compile(ex.code); CHECK(pr->ok(), "example %s: %s", ex.name, pr->error.toRawUTF8()); }
    std::printf("  %d example modules compile\n", (int) gb::script::examples().size());
    struct E { const char* code; const char* want; int line, col; };
    for (auto& e : std::vector<E> { { "if bar % 4 = 3 then hit()", "==", 1, 12 }, { "if bar > 1 then explode()", "unknown action", 1, 17 }, { "\nif step > then vel(3)", "'then'", 2, 11 },
                                    { "retrig(rate=4)", "needs 'n'", 1, 1 }, { "if x > 1 then mute()", "unknown name", 1, 4 }, { "vel(10", "expected", 1, 7 } }) {
        auto pr = gb::script::Compiler::compile(e.code);
        const bool ok = !pr->ok() && pr->error.contains(e.want) && pr->errLine == e.line && pr->errCol == e.col;
        if (!ok) std::printf("  error case [%s] -> line %d col %d: %s\n", juce::String(e.code).replace("\n", "\\n").toRawUTF8(), pr->errLine, pr->errCol, pr->error.toRawUTF8());
        CHECK(ok, "script error message for '%s'", e.code);
    }
    // fuzz: random text never crashes, compiles fast
    juce::Random R(5); const char* bits[] { "if ", "then ", "else ", "and ", "or ", "not ", "bar ", "step ", "% ", "== ", "( ", ") ", "retrig(", "4", ", ", "accel=", "0.3", "vel(", "chance(", "hit()", "\n", "-", "*", "/", "=", "x", ";", "#c\n", "rand()", "mute()" };
    int compiled = 0; const auto t0 = juce::Time::getMillisecondCounterHiRes();
    for (int i = 0; i < 3000; ++i) { juce::String src; const int n = 1 + R.nextInt(30); for (int k = 0; k < n; ++k) src << bits[R.nextInt(30)]; auto pr = gb::script::Compiler::compile(src); if (pr->ok()) { ++compiled; gb::script::Env env; gb::Step st; st.vel = 100; gb::script::run(*pr, env, st); } }
    std::printf("  fuzz: 3000 random scripts, %d valid ones ran, %.0f ms, no crash\n", compiled, juce::Time::getMillisecondCounterHiRes() - t0);
    { juce::String big = "if "; for (int i = 0; i < 2500; ++i) big << "bar + "; big << "1 > 0 then hit()"; auto pr = gb::script::Compiler::compile(big); CHECK(!pr->ok() && pr->error.contains("too long"), "size limit"); }
    // semantics on the engine: the turnaround example only touches hats/snares in the last steps of every 4th bar
    gb::Snapshot S; auto click = makeSound("c", 32, [](int i) { return i < 4 ? 1.0f : 0.0f; });
    for (int t = 0; t < 3; ++t) { S.sound[(size_t) t] = click.get(); S.tracks[(size_t) t].vol = 1; } S.role = { gb::RoleKick, gb::RoleSnare, gb::RoleHat };
    for (int s = 0; s < 16; s += 4) S.patterns[0].steps[0][(size_t) s].vel = 110; for (int s = 0; s < 16; ++s) S.patterns[0].steps[2][(size_t) s].vel = 80;
    auto prog = gb::script::Compiler::compile(gb::script::examples()[0].code); S.progs[0] = prog; S.nProgs = 1;
    std::map<std::pair<long, int>, int> perStep; gb::collectEvents(S, cleanParams(120), 120, 0, 32, [&](int t, double b, float, const gb::EvInfo&) { if (t == 2) perStep[{ (long) std::floor(b / 4), (int) std::floor(std::fmod(b, 4.0) / 0.25 + 1e-6) }]++; });
    bool right = true; for (long bar = 0; bar < 8; ++bar) for (int pos = 0; pos < 16; ++pos) { const int want = (bar % 4 == 3 && pos > 12) ? 4 : 1; if (perStep[{ bar, pos }] != want) right = false; }
    std::printf("  'turnaround rolls': hats roll x4 exactly on steps 14-16 of bars 4 and 8, single hits elsewhere: %s\n", right ? "yes" : "NO"); CHECK(right, "script semantics");
    // seeded chance(): same seed = same result, other seed = different
    auto pr2 = gb::script::Compiler::compile("if role == 3 and chance(0.5) then mute()"); S.progs[0] = pr2;
    auto count = [&](int seed) { S.patterns[0].seed = seed; std::vector<long> ev; gb::collectEvents(S, cleanParams(120), 120, 0, 64, [&](int t, double b, float, const gb::EvInfo&) { if (t == 2) ev.push_back(std::lround(b * 960)); }); return ev; };
    const auto a = count(1), b = count(1), c = count(2);
    std::printf("  chance(0.5) mutes %d of 256 hats; same seed identical %s, new seed differs %s\n", 256 - (int) a.size(), a == b ? "yes" : "NO", a != c ? "yes" : "NO");
    CHECK(a == b && a != c && a.size() > 90 && a.size() < 170, "seeded chance");
    // modules saved with the beat + undo
    p.applyKit(10, true); p.pushUndo(); p.model.scripts.push_back({ "x", "if hit then vel(50)", true }); p.publish();
    juce::MemoryBlock mb; p.getStateInformation(mb); BeatsXProcessor q; q.restoreState(mb.getData(), (int) mb.getSize());
    CHECK(q.model.scripts.size() == 1 && q.model.scripts[0].code == "if hit then vel(50)", "scripts saved with the beat");
    p.undo(); CHECK(p.model.scripts.empty(), "script edits undo");
}

// ---------------------------------------------------------------- track levels (meters), shared sounds, theme, focus
static void testLevelsSharedTheme(BeatsXProcessor& p) {
    section("TRACK LEVELS (meters) + SHARED SOUNDS folder + THEME + FOCUS");
    // meters: two tracks play (a loud kick-like click and a quiet one), two stay silent
    auto loud = makeSound("loud", 2400, [](int i) { return 0.9f * std::exp(-i / 400.0f); }), quiet = makeSound("quiet", 2400, [](int i) { return 0.1f * std::exp(-i / 400.0f); });
    gb::Snapshot S; S.sound[0] = loud.get(); S.sound[2] = quiet.get(); for (int t = 0; t < 4; ++t) { S.tracks[(size_t) t].vol = 1; S.tracks[(size_t) t].sound = "t"; }
    S.patterns[0].steps[0][0].vel = 127; S.patterns[0].steps[2][0].vel = 127; S.patterns[0].steps[1][0].vel = 127;   // track 1 has a hit but no sound
    gb::BeatEngine e; e.prepare(48000, 512); e.setSnapshot(&S); e.internalPlay = true; juce::AudioBuffer<float> buf(2, 512); juce::MidiBuffer none; auto P = cleanParams(120);
    e.process(buf, none, P, 120, 0, false);
    const float m0 = e.trackPeak[0].load(), m1 = e.trackPeak[1].load(), m2 = e.trackPeak[2].load(), m3 = e.trackPeak[3].load(), ms = e.masterPeak[0].load();
    std::printf("  after one block: track 1 %.3f, track 2 (no sound) %.3f, track 3 %.3f, track 4 (silent) %.3f, master L %.3f\n", m0, m1, m2, m3, ms);
    CHECK(m0 > 0.5f && m2 > 0.05f && m2 < 0.2f && m1 == 0.0f && m3 == 0.0f && ms > 0.3f, "track meters follow what each track plays");
    for (int i = 0; i < 100; ++i) e.process(buf, none, P, 120, 0, false);   // ~1 s later the meters have fallen back (next hit is at the next bar)
    std::printf("  1 s later: track 1 %.4f (decays), master %.4f\n", e.trackPeak[0].load(), e.masterPeak[0].load());
    CHECK(e.trackPeak[0].load() < m0 * 0.2f, "meters decay");
    S.tracks[0].mute = true; for (int i = 0; i < 400; ++i) e.process(buf, none, P, 120, 0, false);
    CHECK(e.trackPeak[0].load() == 0.0f, "a muted track meters 0 (got %.4f)", e.trackPeak[0].load());
    // SHARED SOUNDS: another plugin drops a WAV into Documents/Ghosty/Shared Sounds; the browser lists it, the track keeps it in the project
    const auto dir = BeatsXProcessor::sharedSoundsDir(); const auto name = "Shared Test Hit " + juce::String(juce::Time::currentTimeMillis() % 100000);
    auto wavf = dir.getChildFile(name + ".wav"); wavf.deleteFile();
    { juce::WavAudioFormat w; auto os = std::unique_ptr<juce::OutputStream>(wavf.createOutputStream()); std::unique_ptr<juce::AudioFormatWriter> wr(w.createWriterFor(os.get(), 44100, 1, 16, {}, 0)); os.release();
      juce::AudioBuffer<float> b(1, 22050); for (int i = 0; i < 22050; ++i) b.setSample(0, i, 0.7f * (float) std::sin(i * 0.05) * std::exp(-i / 5000.0f)); wr->writeFromAudioSampleBuffer(b, 0, 22050); }
    const int added = p.rescanShared(); const auto snd = p.lib->byName(name);
    std::printf("  rescan found %d new file(s); \"%s\" category %s, %.2f s\n", added, name.toRawUTF8(), snd ? snd->category.toRawUTF8() : "-", snd ? snd->data.size() / 48000.0 : 0.0);
    CHECK(added >= 1 && snd && snd->category == "Shared" && gb::folderOf(snd->category) == "SHARED" && gb::browserFolders().contains("SHARED"), "shared sound listed in the SHARED folder");
    CHECK(p.rescanShared() == 0, "a second scan adds nothing");
    p.applyKit(0, true); p.setTrackSound(5, name);
    CHECK(p.model.tracks[5].sample != nullptr && p.model.tracks[5].sound == name, "a shared sound is kept inside the project (like a dropped sample)");
    juce::MemoryBlock mb; p.getStateInformation(mb); wavf.deleteFile(); BeatsXProcessor q; q.restoreState(mb.getData(), (int) mb.getSize());
    CHECK(q.model.tracks[5].sample && q.model.tracks[5].sample->data.size() > 10000 && q.model.tracks[5].sound == name, "project reopens with the sound even after the shared file is gone");
    // THEME: both palettes keep the text and the 8 track colours readable on the cards
    auto lum = [](juce::Colour c) { auto f = [](float v) { return v <= 0.03928f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f); }; return 0.2126f * f(c.getFloatRed()) + 0.7152f * f(c.getFloatGreen()) + 0.0722f * f(c.getFloatBlue()); };
    auto contrast = [&](juce::Colour a, juce::Colour b) { const float la = lum(a), lb = lum(b); return (std::max(la, lb) + 0.05f) / (std::min(la, lb) + 0.05f); };
    for (int ti = 0; ti < 3; ++ti) {   // the house themes: BLACK, PURPLE, LIGHT
        const auto t = ghosty::themes::fromInt(ti); bx::applyTheme(t); float worstTrack = 99, worstText = 99, worstAccent = 99;
        for (auto bg : { bx::BG, bx::CARD, bx::CARD2, bx::POP }) { worstText = std::min({ worstText, contrast(bx::INK, bg), contrast(bx::GREY, bg) });
            for (auto a : { bx::PURPLE, bx::GREEN, bx::PINK, bx::AMBER, bx::BLUE, bx::RED, bx::CYAN, bx::LIME }) if (bg != bx::POP) worstAccent = std::min(worstAccent, contrast(a, bg)); }
        for (int k = 0; k < 8; ++k) worstTrack = std::min(worstTrack, contrast(bx::trackColour(k), bx::CARD2));
        int maxCh = 0; for (auto c : { bx::BG, bx::CARD, bx::CARD2, bx::POP }) maxCh = std::max({ maxCh, (int) c.getRed(), (int) c.getGreen(), (int) c.getBlue() });
        std::set<juce::uint32> distinct; for (int k = 0; k < 8; ++k) distinct.insert(bx::trackColour(k).getARGB());
        std::printf("  %-6s theme: worst text %.1f:1, worst accent on a surface %.1f:1, weakest track colour on a row %.1f:1, %d distinct track colours; brightest surface channel %d; all surfaces opaque %d\n",
                    ghosty::themes::displayName(t).toRawUTF8(), worstText, worstAccent, worstTrack, (int) distinct.size(), maxCh, (int) (bx::BG.isOpaque() && bx::CARD.isOpaque() && bx::CARD2.isOpaque() && bx::POP.isOpaque()));
        CHECK(worstText >= 4.5f && worstAccent >= 4.4f && worstTrack >= 3.0f && distinct.size() == 8 && (t != ghosty::themes::Theme::Light || maxCh <= 0xED) && bx::BG.isOpaque() && bx::POP.isOpaque(), "%s theme contrast", ghosty::themes::displayName(t).toRawUTF8());
    }
    {   // the saved look: "theme_name"; the old "dark" switch still loads; nothing saved -> BLACK
        auto file = juce::File::createTempFile(".settings"); juce::PropertiesFile::Options o; juce::PropertiesFile pf(file, o);
        const auto none = BeatsXProcessor::themeFromSettings(pf); pf.setValue("dark", false); const auto oldLight = BeatsXProcessor::themeFromSettings(pf);
        pf.setValue("dark", true); const auto oldDark = BeatsXProcessor::themeFromSettings(pf); pf.setValue("theme_name", "purple"); const auto named = BeatsXProcessor::themeFromSettings(pf); file.deleteFile();
        std::printf("  saved look: none -> %s, old dark=false -> %s, old dark=true -> %s, theme_name purple -> %s\n", ghosty::themes::toString(none).toRawUTF8(), ghosty::themes::toString(oldLight).toRawUTF8(), ghosty::themes::toString(oldDark).toRawUTF8(), ghosty::themes::toString(named).toRawUTF8());
        CHECK(none == ghosty::themes::Theme::Dark && oldLight == ghosty::themes::Theme::Light && oldDark == ghosty::themes::Theme::Dark && named == ghosty::themes::Theme::Purple, "theme state: new key + old key");
    }
    bx::applyTheme(false);
}


// ---------------------------------------------------------------- FINAL GATE punch list (BeatsX fixes, one check per item)
struct Spec { std::vector<float> mag; double sr = 48000; int N = 1; float peakIn(double lo, double hi, double* at = nullptr) const { float m = 0; int bi = 0; for (int b = (int) (lo * N / sr); b <= (int) (hi * N / sr) && b < (int) mag.size(); ++b) if (mag[(size_t) b] > m) { m = mag[(size_t) b]; bi = b; } if (at) *at = bi * sr / N; return m; } };
static Spec spectrum(const float* x, int n0, double sr) { const int order = 14, N = 1 << order; juce::dsp::FFT fft(order); std::vector<float> buf((size_t) 2 * N, 0.0f);
    for (int k = 0; k < N; ++k) buf[(size_t) k] = x[n0 + k] * (0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * k / N)); fft.performFrequencyOnlyForwardTransform(buf.data()); Spec s; s.mag.assign(buf.begin(), buf.begin() + N / 2); s.sr = sr; s.N = N; return s; }
static float dBr(float a, float ref) { return 20.0f * std::log10(juce::jmax(1e-9f, a) / juce::jmax(1e-9f, ref)); }
static juce::AudioBuffer<float> runProc(BeatsXProcessor& p, double sr, int block, double seconds) {
    p.prepareToPlay(sr, block); p.engine.internalPlay = true; const int total = (int) (sr * seconds); juce::AudioBuffer<float> out(2, total), b(2, block); juce::MidiBuffer m;
    for (int pos = 0; pos < total; pos += block) { const int n = juce::jmin(block, total - pos); b.setSize(2, n, false, false, true); b.clear(); p.processBlock(b, m); for (int c = 0; c < 2; ++c) out.copyFrom(c, pos, b, c, 0, n); }
    p.engine.internalPlay = false; return out;
}
static double zcFreq(const float* x, int a, int n, double sr) { int z = 0; for (int i = a + 1; i < a + n; ++i) if ((x[i - 1] < 0) != (x[i] < 0)) ++z; return z * sr / (2.0 * n); }
struct FakeHead : juce::AudioPlayHead { double bpm = 120, ppq = 0; bool playing = true, haveBpm = true; juce::Optional<PositionInfo> getPosition() const override { PositionInfo i; if (haveBpm) i.setBpm(bpm); i.setPpqPosition(ppq); i.setIsPlaying(playing); return i; } };
// STOP in the DAW stops Beats, even a beat started with Beats' own PLAY (Brandon 2026-11-21)
static void testDawStop() {
    std::printf("[dawstop]\n");
    BeatsXProcessor p; p.applyKit(0, true); p.prepareToPlay(48000, 512); FakeHead h; h.playing = false; p.setPlayHead(&h);
    juce::AudioBuffer<float> b(2, 512); juce::MidiBuffer m;
    auto rms = [&](int blocks) { double e = 0; for (int i = 0; i < blocks; ++i) { b.clear(); p.processBlock(b, m); if (h.playing) h.ppq += 512.0 / 48000.0 * h.bpm / 60.0; e += b.getRMSLevel(0, 0, 512); } return e / blocks; };
    rms(4); p.engine.internalPlay = true; const double own = rms(40);
    CHECK(own > 0.001, "Beats' own PLAY makes sound while the DAW is stopped (%.4f)", own);
    h.playing = true; rms(40); CHECK(! p.engine.internalPlay, "the DAW starting takes over from Beats' own PLAY");
    h.playing = false; rms(2); const double after = rms(60);
    CHECK(after < 1e-4 && ! p.engine.internalPlay, "after STOP in the DAW, Beats is silent (%.6f)", after);
    p.engine.internalPlay = true; rms(10); h.playing = true; rms(10); h.playing = false; rms(2);
    CHECK(rms(40) < 1e-4, "own PLAY, then DAW play + stop: silent");
    p.setPlayHead(nullptr);
}
// FOLLOW (Brandon 2026-11-21): while the song plays, the letters and the grid follow it; a 32-step pattern turns its page with the playhead
static void setParam(BeatsXProcessor& p, const char* id, float v);
// (Brandon's Trap default, 2026-11-21: GHOSTY_BEAT=in GHOSTY_OUT=out BeatsTests test makedefault -- patterns A..D one 16-step page each, LINEAR off, nothing
//  past step 16, no EVOLVE / FILL surprises, no CONVERGE; his song (A B C D A B C D, it loops) and every sound / setting kept as saved)
static void testMakeDefault() {
    const char* in = std::getenv("GHOSTY_BEAT"); const char* out = std::getenv("GHOSTY_OUT"); if (! in || ! out) return;
    BeatsXProcessor p; juce::MemoryBlock mb; juce::File(juce::String::fromUTF8(in)).loadFileAsData(mb); p.restoreState(mb.getData(), (int) mb.getSize(), true);
    for (int i = 0; i < 4; ++i) { auto& pt = p.model.patterns[(size_t) i]; pt.length = 16; for (auto& l : pt.trackLen) l = 0; pt.evolve = 0; pt.fillEvery = 0;
        for (auto& row : pt.steps) for (int s = 16; s < gb::kMaxSteps; ++s) row[(size_t) s] = gb::Step();
        for (auto& row : pt.steps) for (auto& st : row) st.flags = (uint8_t) (st.flags & ~2); }
    p.model.converge = 0; p.model.linear.on = false; setParam(p, "pattern", 1); setParam(p, "songmode", 1); setParam(p, "bypass", 0); p.publish();
    juce::MemoryBlock o; p.getStateInformation(o); juce::File(juce::String::fromUTF8(out)).replaceWithData(o.getData(), o.getSize());
    for (int i = 0; i < 4; ++i) std::printf("%s\n", gb::script::beatAsScript(p.model, i, juce::String::charToString((juce::juce_wchar) ('A' + i))).toRawUTF8());
    std::printf("song:"); for (auto& b : p.model.song) std::printf(" %c@%d", 'A' + b.pattern, b.startBar); std::printf("\n");
}

static void setParam(BeatsXProcessor& p, const char* id, float v);
static void testFollow() {
    std::printf("[follow]\n");
    BeatsXProcessor p; p.applyKit(0, true);
    for (int i = 0; i < 4; ++i) { auto& pt = p.model.patterns[(size_t) i]; pt.length = 32; pt.res = 1; pt.steps[1][0].vel = 100; }
    p.model.song.clear(); for (int i = 0; i < 4; ++i) p.model.song.push_back({ i, i * 2, 2 });
    setParam(p, "songmode", 1); setParam(p, "pattern", 1); p.publish(); p.prepareToPlay(48000, 512);
    FakeHead h; h.playing = true; h.bpm = 120; p.setPlayHead(&h);
    std::unique_ptr<juce::AudioProcessorEditor> e(p.createEditor()); e->setSize(1200, 760); e->setVisible(true);
    bx::StepGrid* g = nullptr; std::function<void(juce::Component&)> find = [&](juce::Component& c) { if (auto* x = dynamic_cast<bx::StepGrid*>(&c)) g = x; for (auto* ch : c.getChildren()) find(*ch); }; find(*e);
    juce::AudioBuffer<float> b(2, 512); juce::MidiBuffer m;
    auto at = [&](double beat) { h.ppq = beat; b.clear(); p.processBlock(b, m); for (int i = 0; i < 12; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(20); };
    at(1.0);  CHECK(p.currentPattern() == 0, "bar 1: A");
    at(9.0);  CHECK(p.currentPattern() == 1, "bar 3: the letters follow to B (%d)", p.currentPattern());
    at(17.0); CHECK(p.currentPattern() == 2, "bar 5: C");
    at(29.0); CHECK(p.currentPattern() == 3 && g && g->ed.stepPage == 1, "bar 8: D, and the grid shows steps 17-32 (page %d)", g ? g->ed.stepPage : -1);
    at(25.0); CHECK(g && g->ed.stepPage == 0, "back on D's first bar: steps 1-16");
    at(33.0); CHECK(p.currentPattern() == 0, "bar 9: the song loops back to A (%d)", p.currentPattern());
    at(41.0); CHECK(p.currentPattern() == 1, "bar 11: B again");
    e.reset(); p.setPlayHead(nullptr);
}



static int kitIndexOf(const char* n);
static void setParam(BeatsXProcessor& p, const char* id, float v);
static void testLinearMelody();
static void testPlayOnNotes();
static void testSync();
static void testPolish();
static void testRecreate();
namespace a2mb { static void bench(const juce::File& renders, int kitStride, int kitOffset); static void trainMode(bool split, const juce::File& out); static void debugOne(int k, int pi, int v); static void dump(const juce::File& out, int variants); }
static void testThemePixels();
static void testGhostUi(const juce::File& shots);
static void testRegion();
static void testTrackerExtras();
static void testGate2();
// ---------------------------------------------------------------- legacy hashes: what every older kit and both older GENERATE styles play
// (FNV over the rendered floats and the generated steps). The reference values were measured with the code before the DRILL style.
static uint64_t fnvBuf(const juce::AudioBuffer<float>& b) { uint64_t h = 1469598103934665603ULL; for (int c = 0; c < b.getNumChannels(); ++c) for (int i = 0; i < b.getNumSamples(); ++i) { const float v = b.getSample(c, i); uint32_t u; std::memcpy(&u, &v, 4); h = (h ^ u) * 1099511628211ULL; } return h; }
static uint64_t fnvPattern(const gb::Pattern& p) { uint64_t h = 1469598103934665603ULL; const auto* d = reinterpret_cast<const uint8_t*>(p.steps.data()); for (size_t i = 0; i < sizeof(p.steps); ++i) h = (h ^ d[i]) * 1099511628211ULL;
    for (auto v : { p.length, p.res, p.fillEvery, p.seed, p.evolveStyle }) h = (h ^ (uint64_t) (uint32_t) v) * 1099511628211ULL; uint32_t e; std::memcpy(&e, &p.evolve, 4); h = (h ^ e) * 1099511628211ULL; for (auto l : p.trackLen) h = (h ^ l) * 1099511628211ULL; return h; }
static std::vector<uint64_t> legacyHashes(int kitsBefore) {
    std::vector<uint64_t> out; BeatsXProcessor p;
    for (int k = 0; k < kitsBefore; ++k) { p.applyKit(k, true); auto spec = p.makeSpec({ false, false, -1 }); out.push_back(fnvBuf(render(*spec.snap, spec.P, 2 * 4 * 60.0 / spec.P.bpm))); }
    for (const char* kit : { "Trap", "Drum & Bass", "Lo-fi Tape", "Drill" }) for (int style = 0; style < 2; ++style) for (int seed : { 3, 41 }) {
        int ki = 0; for (int i = 0; i < (int) gb::allKits().size(); ++i) if (juce::String(gb::allKits()[(size_t) i].name) == kit) ki = i;
        p.applyKit(ki, true); p.model.toolSeed = seed; p.generatePattern(style); out.push_back(fnvPattern(p.model.patterns[0]));
        auto spec = p.makeSpec({ false, false, -1 }); out.push_back(fnvBuf(render(*spec.snap, spec.P, 2 * 4 * 60.0 / spec.P.bpm)));
    }
    return out;
}

static const uint64_t kLegacyRef[] = {
0x8a2cd0d729e991bbULL, 0x894046ab52f8afadULL, 0xc2914c5a1311ea8bULL, 0x4f28447203cb220fULL,
0x7f9f36ae5a834b9bULL, 0x85a80933e999350dULL, 0x2536be434a3e27d9ULL, 0x3176f55ce54129bbULL,
0xf549050aa552ebd7ULL, 0x43b4149d85406d67ULL, 0x74b7a17238b9c691ULL, 0x446f5e94cec05723ULL,
0xd8c686d347c02535ULL, 0xbf9ea523ddbf1eb3ULL, 0x408fd00ca304c223ULL, 0xf739274d0475e8d7ULL,
0x5984516f98eb5c2bULL, 0x51765e75fa21d2b3ULL, 0xb8de4c8032e5548fULL, 0x05af90fe8ed87db3ULL,
0xc8b5c6c01e79d803ULL, 0x4925ed39bef13ec5ULL, 0x1e10c13007aa51e3ULL, 0xdd71cbae8e1412a3ULL,
0xfa1b2a0bfc9a03ebULL, 0xfc4cd79f4019e5fbULL, 0xba12f0d5a9fdc633ULL, 0x2e55fd1f4872e5fbULL,
0x190e9173f7ff0543ULL, 0xa51e8fc1bf7ba3efULL, 0xda780b4d3864d903ULL, 0x5f612f37e5c1c1b1ULL,
0x9a21b4782a18a0c3ULL, 0x96bfe1bba400ab27ULL, 0x80838fda0d5cb8b1ULL, 0x7a4f9bd73e49ce53ULL,
0x7ae0d817c21b398dULL, 0xd2c7cd2d2e37f630ULL, 0x9c3ae975ad232de5ULL, 0xbbfc0884fe948691ULL,
0x906dbac60219693fULL, 0x5ff00b1474369ae7ULL, 0xc91cecc8edc1bd87ULL, 0x3880496ab9c71853ULL,
0x7b7c13f4564de609ULL, 0xff92d3db4b0a9030ULL, 0xcdbcf8322dbfb293ULL, 0x54a7d4e9173fc1b1ULL,
0xe5a68c366ddfa62bULL, 0x3d81a51af53eab27ULL, 0xa115784d59d272b3ULL, 0x50cdd7f33dc71853ULL,
0x0042357583d58b1bULL, 0x18dbe48b440c7630ULL, 0x5345e28cc02987afULL, 0x5f612f37e5c1c1b1ULL,
0x8179eace3776d25dULL, 0x96bfe1bba400ab27ULL, 0xfc4eed11948b3573ULL, 0x7a4f9bd73e49ce53ULL,
0x355721cda1808635ULL, 0xd2c7cd2d2e37f630ULL, 0x9092c7417b583503ULL
};
static const uint64_t kDrill27Ref[] = {   // measured with installer #27's code (before GRIT); the first three (the kit's starter beats, now at GRIT 70) re-measured
0xd0bca197435d0c55ULL, 0xc9f0e3af86cb9c55ULL, 0x8ad183b5ece70605ULL, 0x5eaedf609aa4c8f9ULL,
0x1bcffc64a119e633ULL, 0x33acc826f05209bdULL, 0x3e1f31d8ccdae323ULL, 0xccb9d519d76f90c4ULL,
0x5a5415724b18ca51ULL, 0xbfe194228b2388f9ULL, 0x4a589d2f842dbafdULL, 0xe4aad4b3fbd7d1bdULL,
0xdb87b0158174f487ULL, 0x37559dc5ff5d2ec4ULL, 0xfaa2aef36061affbULL, 0xbfe194228b2388f9ULL,
0xfb7107451101b813ULL, 0xe4aad4b3fbd7d1bdULL, 0x131d0db9b3b75e8bULL, 0x37559dc5ff5d2ec4ULL,
0x188b2a38c4a684bdULL,
};
// the DRILL kit starter beats (GRIT 70) + GENERATE DRILL as installer #27 shipped it (GRIT 0): the DRILL kit's starter beats + GENERATE DRILL on three kits
static std::vector<uint64_t> drillHashes() {
    std::vector<uint64_t> out; BeatsXProcessor p;
    { p.applyKit(kitIndexOf("DRILL"), true); out.push_back(fnvPattern(p.model.patterns[0])); out.push_back(fnvPattern(p.model.patterns[1])); auto spec = p.makeSpec({ false, false, -1 }); out.push_back(fnvBuf(render(*spec.snap, spec.P, 2 * 4 * 60.0 / spec.P.bpm))); }
    for (const char* kit : { "DRILL", "Trap", "Drill Dark" }) for (int seed : { 3, 41, 77 }) {
        p.applyKit(kitIndexOf(kit), true); p.model.drillGrit = 0; p.model.toolSeed = seed; p.generatePattern(2); out.push_back(fnvPattern(p.model.patterns[0]));
        auto spec = p.makeSpec({ false, false, -1 }); out.push_back(fnvBuf(render(*spec.snap, spec.P, 2 * 4 * 60.0 / spec.P.bpm)));
    }
    return out;
}
// 4x oversampled peak (windowed sinc, 16 taps a side): the true peak a DAC / encoder sees between samples
static float truePeak4x(const juce::AudioBuffer<float>& b) {
    static const std::array<std::array<float, 32>, 3> ker = [] { std::array<std::array<float, 32>, 3> k {}; for (int ph = 1; ph <= 3; ++ph) for (int j = -15; j <= 16; ++j) {
        const double x = j - ph / 4.0, w = 0.5 + 0.5 * std::cos(juce::MathConstants<double>::pi * x / 16.5); k[(size_t) ph - 1][(size_t) (j + 15)] = (float) (std::abs(x) < 1e-12 ? 1.0 : std::sin(juce::MathConstants<double>::pi * x) / (juce::MathConstants<double>::pi * x) * w); } return k; }();
    float pk = 0;
    for (int c = 0; c < b.getNumChannels(); ++c) { const float* x = b.getReadPointer(c); const int n = b.getNumSamples();
        for (int i = 0; i < n; ++i) { pk = std::max(pk, std::abs(x[i])); if (i < 15 || i + 16 >= n) continue;
            for (int ph = 0; ph < 3; ++ph) { float y = 0; const auto& K = ker[(size_t) ph]; for (int j = 0; j < 32; ++j) y += x[i - 15 + j] * K[(size_t) j]; pk = std::max(pk, std::abs(y)); } } }
    return pk;
}
// the fundamental of a bass note: zero crossings after a 4-pole low-pass (the drive's harmonics would double the count)
static double lowZc(const float* x, int a, int n, double cut = 110) { std::vector<float> y((size_t) n); const float k = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * (float) cut / 48000.0f); float s[4] {};
    const int pre = juce::jmin(a, 4800); for (int i = a - pre; i < a + n; ++i) { float v = x[i]; for (auto& z : s) { z += k * (v - z); v = z; } if (i >= a) y[(size_t) (i - a)] = v; } return zcFreq(y.data(), 0, n, 48000); }
// the first full cycle of a sound (between its first two downward zero crossings, after a gentle low-pass), as a frequency
static double firstCycleHz(const float* x, int n, double cut = 300) { std::vector<float> y((size_t) n); const float k = 1.0f - std::exp(-juce::MathConstants<float>::twoPi * (float) cut / 48000.0f); float s[2] {};
    for (int i = 0; i < n; ++i) { float v = x[i]; for (auto& z : s) { z += k * (v - z); v = z; } y[(size_t) i] = v; }
    std::vector<int> dn; for (int i = 1; i < n && dn.size() < 2; ++i) if (y[(size_t) i - 1] > 0 && y[(size_t) i] <= 0) dn.push_back(i);
    return dn.size() >= 2 ? 48000.0 / (dn[1] - dn[0]) : 0.0; }
static double rmsDb(const float* x, int a, int n) { double s = 0; for (int i = a; i < a + n; ++i) s += (double) x[i] * x[i]; return 10.0 * std::log10(s / juce::jmax(1, n) + 1e-20); }
static juce::AudioBuffer<float> renderBars(BeatsXProcessor& p, int bars, int limiter = -1) {
    auto spec = p.makeSpec({ false, false, -1 }); if (limiter >= 0) spec.P.limiter = limiter;
    return render(*spec.snap, spec.P, bars * 4 * 60.0 / spec.P.bpm);
}
static void writeWav24(const juce::AudioBuffer<float>& b, const juce::File& f) {
    f.deleteFile(); juce::WavAudioFormat wav; std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(new juce::FileOutputStream(f), 48000.0, (unsigned) b.getNumChannels(), 24, {}, 0));
    if (w) w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
}
static void testDrillSounds() {
    section("DRILL sounds: the pack, step SLIDE / MONO / HOLD, older kits + styles bit-exact");
    // ---- 1. the sound pack: appended last, levelled like the rest, the 808 to spec
    auto lib = gb::loadBeatsLibrary(48000.0, false);
    const int n = (int) lib->sounds().size(); int found = 0, atEnd = 0, catOk = 0, peakOk = 0, trimOk = 0;
    static const char* cats[] { "808", "808", "808", "Kick", "Snare", "Clap", "Hat", "Open Hat", "Perc", "808", "Snare" };
    const int np = gb::drillPackNames().size(), nl = gb::latinPackNames().size(), nc = gb::classicPackNames().size();
    std::map<juce::String, std::vector<float>> older; for (auto& s : lib->sounds()) if (s->category != "Break" && !gb::isPackSound(s->name)) older[gb::folderOf(s->category)].push_back(gb::loudest200ms(*s));
    for (auto& kv : older) std::sort(kv.second.begin(), kv.second.end());
    for (int i = 0; i < gb::drillPackNames().size(); ++i) {
        const int at = lib->indexOf(gb::drillPackNames()[i]); if (at < 0) continue; ++found; const auto& s = *lib->sounds()[(size_t) at];
        if (at >= n - nc - nl - np && at < n - nc - nl) ++atEnd;   // (the LATIN pack, then the classic-character packs follow it)
        if (s.category == cats[i]) ++catOk;
        float pk = 0; for (auto v : s.data) pk = std::max(pk, std::abs(v)); if (pk > 0.85f && pk < 0.9f) ++peakOk;
        const auto& v = older[gb::folderOf(s.category)]; const float med = v[v.size() / 2], lv = gb::loudest200ms(s) + juce::Decibels::gainToDecibels(s.trim);
        if (std::abs(lv - med - gb::packLevelOffsetDb(s.name)) < 0.6f) ++trimOk; else std::printf("  %s: level %.1f dB vs folder median %.1f\n", s.name.toRawUTF8(), lv, med);
    }
    CHECK(np == 11 && found == np && atEnd == np && catOk == np, "DRILL pack: %d found, %d at the end of the library, %d in the right folder", found, atEnd, catOk);
    CHECK(peakOk == np && trimOk == np, "DRILL pack levels: %d peak-normalised, %d levelled to their folder median", peakOk, trimOk);
    const auto* b8 = lib->byName("DRILL 808 LONG").get(); const auto* kk = lib->byName("DRILL KICK").get();
    if (b8 && kk) {
        const float* x = b8->data.data(); const int sr = 48000;
        const double f0 = lowZc(x, (int) (0.6 * sr), (int) (0.5 * sr)), fEarly = firstCycleHz(x, (int) (0.2 * sr));
        const double top = rmsDb(x, 0, (int) (0.1 * sr)), at1 = rmsDb(x, (int) (1.0 * sr), (int) (0.1 * sr)) - top, at26 = rmsDb(x, (int) (2.55 * sr), (int) (0.05 * sr)) - top;
        std::vector<float> kpad(1 << 14, 0.0f); std::copy_n(kk->data.begin(), juce::jmin(kk->data.size(), kpad.size()), kpad.begin());   // (the kick is shorter than the FFT: pad it)
        const auto sk = spectrum(kpad.data(), 0, sr); const auto s8 = spectrum(x, 0, sr);
        const float kSub = dBr(sk.peakIn(30, 60), sk.peakIn(100, 400)), bSub = dBr(s8.peakIn(40, 60), s8.peakIn(100, 400));
        std::printf("  DRILL 808 LONG: %.1f Hz (G1 = 49.0), first cycle %.0f Hz (pitch env), level at 1 s %.1f dB, at 2.6 s %.1f dB | DRILL KICK sub vs knock %.1f dB (808: %+.1f dB)\n", f0, fEarly, at1, at26, kSub, bSub);
        CHECK(f0 > 46 && f0 < 52, "808 pitch %.1f Hz", f0);
        CHECK(fEarly > f0 * 1.05, "808 pitch envelope: first cycle %.0f Hz vs %.0f Hz", fEarly, f0);
        CHECK(at1 > -30 && at26 < -40 && at26 > -80, "808 decay: %.1f dB at 1 s, %.1f dB at 2.6 s", at1, at26);
        CHECK(kSub < -12 && bSub > 0, "DRILL KICK high-passed above the 808 (%.1f dB sub)", kSub);
    } else CHECK(false, "DRILL 808 LONG / DRILL KICK missing");

    // ---- 2. the engine: a SLIDE glides the ringing note (no retrigger), MONO cuts it, HOLD gates it; old steps behave as before
    if (b8) {
        auto runOne = [&](std::function<void(gb::Pattern&)> fill, double seconds, std::vector<int>* voicesAt = nullptr, std::vector<double> at = {}) {
            gb::Snapshot S = oneTrack(b8); auto& pt = S.patterns[0]; pt.length = 16; pt.res = 1; fill(pt);
            const auto P = cleanParams(120); gb::BeatEngine e; e.prepare(48000, 512); e.setSnapshot(&S); e.internalPlay = true;
            const int total = (int) (seconds * 48000); juce::AudioBuffer<float> out(2, total), buf(2, 512); juce::MidiBuffer none2; size_t ai = 0;
            for (int pos = 0; pos < total; pos += 512) { const int m = juce::jmin(512, total - pos); buf.setSize(2, m, false, false, true); e.process(buf, none2, P, 120, 0, false);
                for (int c = 0; c < 2; ++c) out.copyFrom(c, pos, buf, c, 0, m);
                while (voicesAt && ai < at.size() && (pos + m) / 48000.0 >= at[ai]) { voicesAt->push_back(e.activeVoices()); ++ai; } }
            return out;
        };
        // 120 BPM: a step is 125 ms. Note 1 at step 0 (MONO, HOLD 5 = over the slide target), a SLIDE up a 5th at step 4 (140 ms), HOLD 4 (to 1.0 s)
        std::vector<int> vs;
        auto o = runOne([](gb::Pattern& pt) { auto& a = pt.steps[0][0]; a.vel = 110; a.flags = 32; a.hold = 5; auto& b = pt.steps[0][4]; b.vel = 110; b.flags = 16; b.glideT = 14; b.pitch = 7; b.hold = 4; }, 1.5, &vs, { 0.3, 0.6, 0.9 });
        const float* x = o.getReadPointer(0);
        const double fa = lowZc(x, (int) (0.25 * 48000), (int) (0.2 * 48000)), fb = lowZc(x, (int) (0.7 * 48000), (int) (0.25 * 48000)), fmid = lowZc(x, (int) (0.505 * 48000), (int) (0.14 * 48000));
        const double ring = rmsDb(x, (int) (0.7 * 48000), (int) (0.2 * 48000)), after = rmsDb(x, (int) (1.2 * 48000), (int) (0.25 * 48000)) - ring;
        std::printf("  SLIDE: %.1f Hz -> (gliding %.1f Hz) -> %.1f Hz (a 5th up = %.1f), voices %d / %d / %d | HOLD: %.1f dB after the note's end\n", fa, fmid, fb, fa * 1.4983, vs.size() > 0 ? vs[0] : -1, vs.size() > 1 ? vs[1] : -1, vs.size() > 2 ? vs[2] : -1, after);
        CHECK(std::abs(fa - 49) < 3 && std::abs(fb / fa - 1.4983) < 0.04 && fmid > fa * 1.08 && fmid < fb * 0.95, "SLIDE glides 49 Hz -> a 5th up");
        CHECK(vs.size() == 3 && vs[0] == 1 && vs[1] == 1 && vs[2] == 1, "SLIDE never retriggers (one voice)");
        CHECK(after < -60, "HOLD gates the note (%.1f dB after its end)", after);
        std::vector<int> vm, vo;
        runOne([](gb::Pattern& pt) { pt.steps[0][0].vel = 110; auto& b = pt.steps[0][4]; b.vel = 110; b.flags = 32; b.pitch = 12; }, 0.8, &vm, { 0.62 });
        runOne([](gb::Pattern& pt) { pt.steps[0][0].vel = 110; auto& b = pt.steps[0][4]; b.vel = 110; b.pitch = 12; }, 0.8, &vo, { 0.62 });
        CHECK(vm.size() == 1 && vm[0] == 1 && vo.size() == 1 && vo[0] == 2, "MONO cuts the ringing note (%d voice); a plain step still overlaps as before (%d voices)", vm.empty() ? -1 : vm[0], vo.empty() ? -1 : vo[0]);
    }

    // ---- 3. every older kit and both older GENERATE styles: bit-exact to the code before DRILL
    {
        const auto h = legacyHashes(31); int same = 0; const int nRef = (int) (sizeof(kLegacyRef) / sizeof(kLegacyRef[0]));
        for (int i = 0; i < (int) h.size() && i < nRef; ++i) if (h[(size_t) i] == kLegacyRef[i]) ++same; else std::printf("  legacy hash %d differs: %016llx\n", i, (unsigned long long) h[(size_t) i]);
        std::printf("  older kits (31 renders) + INTRICATE / MACHINE GUN (16 patterns + renders): %d / %d hashes bit-exact\n", same, nRef);
        CHECK(same == nRef && (int) h.size() == nRef, "older kits / styles changed");
        const auto d = drillHashes(); int sd = 0; const int nd = (int) (sizeof(kDrill27Ref) / sizeof(kDrill27Ref[0]));
        for (int i = 0; i < (int) d.size() && i < nd; ++i) if (d[(size_t) i] == kDrill27Ref[i]) ++sd; else std::printf("  DRILL hash %d differs: %016llx\n", i, (unsigned long long) d[(size_t) i]);
        std::printf("  DRILL reference (kit starter beats at GRIT 70 + GENERATE DRILL at GRIT 0 on three kits = installer #27): %d / %d hashes bit-exact\n", sd, nd);
        CHECK(sd == nd && (int) d.size() == nd, "DRILL at GRIT 0 changed");
    }
}
static void testDrillStyle() {
    section("DRILL style: GENERATE DRILL rules, the DRILL kit, state round trip, clean levels");
    // ---- 1. GENERATE DRILL: the rules over 300 generations on the DRILL kit
    const int dk = kitIndexOf("DRILL");
    CHECK(juce::String(gb::allKits()[(size_t) dk].name) == "DRILL" && dk == 31, "the DRILL kit keeps its index (%d)", dk);
    BeatsXProcessor p; p.applyKit(dk, true); const auto roles = p.roles(); std::array<bool, gb::kTracks> none {};
    int tSn = -1, t808 = -1, tHat = -1, tKick = -1; for (int t = 0; t < gb::kTracks; ++t) { const int r = roles[(size_t) t];
        if (r == gb::RoleSnare && tSn < 0) tSn = t; if (r == gb::Role808 && t808 < 0) t808 = t; if (r == gb::RoleHat && tHat < 0) tHat = t; if (r == gb::RoleKick && tKick < 0) tKick = t; }
    CHECK(tSn == 2 && t808 == 0 && tHat == 4 && tKick == 1, "DRILL kit roles (snare %d, 808 %d, hat %d, kick %d)", tSn, t808, tHat, tKick);
    const int N = 300; int disp = 0, slidesOk = 0, rollsOk = 0, rangeOk = 0, holdOk = 0, kickOk = 0, swingOk = 0, rot = 0, tempoOk = 0; std::set<int> fams, spots, cells, tempos; int slideMin = 99, slideMax = 0, slideTotal = 0;
    for (int seed = 1; seed <= N; ++seed) {
        gb::Pattern pt; pt.clear(); gb::rhythm::generate(pt, roles, none, gb::drill::kStyle, seed);
        const auto& sn = pt.steps[(size_t) tSn]; auto strong = [&](int s) { return sn[(size_t) s].vel >= 100; };
        bool bar1 = strong(8) && !strong(4) && !strong(12), bar2 = (strong(27) || strong(28)) && !strong(24) && !strong(20);
        bool counter = false; for (int s : { 30, 31 }) if (sn[(size_t) s].vel >= 40 && sn[(size_t) s].vel <= 70) counter = true;
        if (bar1 && bar2 && counter && pt.length == 32) ++disp;
        const int sl = gb::drill::countSlides(pt, t808, -1), sl0 = gb::drill::countSlides(pt, t808, 0), sl1 = gb::drill::countSlides(pt, t808, 1);
        slideMin = std::min(slideMin, sl); slideMax = std::max(slideMax, sl); slideTotal += sl;
        if (sl >= 2 && sl <= 6 && sl0 <= 3 && sl1 <= 3) ++slidesOk;
        int rolls = 0; for (int s = 0; s < 32; ++s) if (pt.steps[(size_t) tHat][(size_t) s].vel && pt.steps[(size_t) tHat][(size_t) s].roll > 1) ++rolls;
        if (rolls >= 1 && rolls <= 2) ++rollsOk;
        bool inRange = true, holds = true, swingFine = true; int lastOn = -1;
        for (int t = 0; t < gb::kTracks; ++t) for (int s = 0; s < gb::kMaxSteps; ++s) { const auto& st = pt.steps[(size_t) t][(size_t) s]; if (!st.vel) continue;
            if (s >= 32 || st.vel > 127 || st.nudge < -50 || st.nudge > 50 || st.hold > gb::kMaxSteps || st.roll < 1 || st.roll > 16) inRange = false;
            if (t == t808) { if (st.pitch < gb::drill::kLowest || st.pitch > gb::drill::kHighest || st.hold < 1) inRange = false;
                             if (st.slide() && !(st.glideT == 9 || st.glideT == 14 || st.glideT == 30)) inRange = false;
                             if (st.slide() == st.mono()) inRange = false;   // every 808 note is either a slide or a MONO attack
                             if (st.slide()) { if (lastOn < 0 || lastOn + pt.steps[(size_t) t][(size_t) lastOn].hold <= s) holds = false; }   // the note it slides from still sounds
                             lastOn = s; }
            else if (t == tHat) { if (st.nudge < 0 || st.nudge > 8) swingFine = false; if (st.nudge && !(s & 1)) swingFine = false; }
            else if (st.nudge != 0 || st.slide() || st.hold) swingFine = false; }
        if (inRange) ++rangeOk; if (holds) ++holdOk; if (swingFine) ++swingOk;
        int k0 = 0, k1 = 0; for (int s = 0; s < 32; ++s) if (pt.steps[(size_t) tKick][(size_t) s].vel) (s < 16 ? k0 : k1)++;
        if (k0 >= 1 && k0 <= 4 && k1 <= 4) ++kickOk;
        fams.insert(gb::drill::bassFamily(seed)); spots.insert(gb::drill::snareSpot(seed)); cells.insert(gb::drill::hatCell(seed));
        if (seed > 1 && gb::drill::bassFamily(seed) != gb::drill::bassFamily(seed - 1) && gb::drill::snareSpot(seed) != gb::drill::snareSpot(seed - 1) && gb::drill::hatCell(seed) != gb::drill::hatCell(seed - 1)) ++rot;
        const int bpm = gb::drill::tempoFor(seed); tempos.insert(bpm); if (bpm >= 138 && bpm <= 146) ++tempoOk;
    }
    std::printf("  %d generations: bar-2 snare displacement %d, slides in range %d (min %d, max %d, mean %.1f per 2 bars), hat rolls 1-2 %d, notes in range %d, slide overlaps %d, swing on hats only %d, sparse kicks %d\n",
                N, disp, slidesOk, slideMin, slideMax, slideTotal / (double) N, rollsOk, rangeOk, holdOk, swingOk, kickOk);
    std::printf("  rotation: %d 808 patterns, %d snare spots, %d hat cells seen; all three change on every new generation %d / %d | tempo %d..%d BPM (%d values)\n",
                (int) fams.size(), (int) spots.size(), (int) cells.size(), rot, N - 1, *tempos.begin(), *tempos.rbegin(), (int) tempos.size());
    CHECK(disp >= N * 9 / 10, "bar-2 snare displacement in %d / %d generations", disp, N);
    CHECK(slidesOk == N, "808 slides 2-6 per 2 bars, <= 3 per bar: %d / %d", slidesOk, N);
    CHECK(rollsOk == N, "hat rolls <= 2 per 2 bars: %d / %d", rollsOk, N);
    CHECK(rangeOk == N && holdOk == N, "notes in range %d, slide overlaps %d (of %d)", rangeOk, holdOk, N);
    CHECK(swingOk == N && kickOk == N, "swing on the hats only %d, sparse kicks %d", swingOk, kickOk);
    CHECK(fams.size() == 12 && spots.size() == 3 && cells.size() == 3 && rot == N - 1, "rotation between generations");
    CHECK(tempoOk == N && tempos.size() == 9, "tempo mapping 138-146");
    {   // through the processor: GENERATE DRILL sets the tempo lane, the 808 duck; INTRICATE leaves both alone; the grid (1/32) keeps HOLD lengths in time
        BeatsXProcessor q; q.applyKit(kitIndexOf("Trap"), true); setParam(q, "songmode", 0); const float bpm0 = q.apvts.getRawParameterValue("bpm")->load(); q.model.toolSeed = 9; q.generatePattern(0);
        const bool untouched = q.apvts.getRawParameterValue("bpm")->load() == bpm0 && q.model.tracks[0].duck == 0.0f;
        q.generatePattern(gb::drill::kStyle); const float bpm = q.apvts.getRawParameterValue("bpm")->load();
        std::printf("  Trap kit + GENERATE DRILL (seed %d): %.0f BPM (wants %d), 808 duck %.2f, kit DRILL 808 duck %.2f | INTRICATE left tempo + duck alone %d\n", q.model.toolSeed, bpm, gb::drill::tempoFor(q.model.toolSeed), q.model.tracks[0].duck, p.model.tracks[0].duck, (int) untouched);
        CHECK(std::lround(bpm) == gb::drill::tempoFor(q.model.toolSeed) && std::abs(q.model.tracks[0].duck - 0.4f) < 1e-6f && std::abs(p.model.tracks[0].duck - 0.4f) < 1e-6f && untouched, "GENERATE DRILL tempo + duck");
        gb::Pattern a; a.clear(); gb::rhythm::generate(a, roles, none, gb::drill::kStyle, 5); gb::Pattern b2; b2.clear(); b2.res = 2; gb::rhythm::generate(b2, roles, none, gb::drill::kStyle, 5);
        bool same = b2.length == 64; for (int s = 0; s < 32 && same; ++s) { const auto& x = a.steps[0][(size_t) s]; const auto& y = b2.steps[0][(size_t) (s * 2)]; if (x.vel != y.vel || x.flags != y.flags || (x.vel && y.hold != x.hold * 2)) same = false; }
        CHECK(same, "GENERATE DRILL on a 1/32 pattern: same notes, HOLD lengths doubled");
    }

    // ---- 2. state round trip: a generated DRILL beat (slides, holds, MONO, glide times, duck) saves and loads exactly
    {
        BeatsXProcessor a; a.applyKit(dk, true); setParam(a, "songmode", 0); a.model.toolSeed = 30; a.generatePattern(gb::drill::kStyle); a.publish();
        const auto st = state(a); BeatsXProcessor b; b.setStateInformation(st.getData(), (int) st.getSize());
        bool same = true; for (int i = 0; i < 2; ++i) same = same && std::memcmp(a.model.patterns[(size_t) i].steps.data(), b.model.patterns[(size_t) i].steps.data(), sizeof(gb::Pattern::steps)) == 0 && a.model.patterns[(size_t) i].length == b.model.patterns[(size_t) i].length;
        same = same && b.model.tracks[0].duck == a.model.tracks[0].duck && b.model.kit == "DRILL" && b.model.tracks[0].sound == "DRILL 808 DIRTY" && b.model.drillGrit == 70 && a.model.drillGrit == 70;
        const auto ha = fnvBuf(renderBars(a, 2)), hb = fnvBuf(renderBars(b, 2));
        CHECK(same && ha == hb && gb::drill::countSlides(b.model.patterns[0], 0, -1) >= 2, "DRILL state round trip (steps, duck, kit, render %016llx vs %016llx)", (unsigned long long) ha, (unsigned long long) hb);
    }

    // ---- 3. clean: the DRILL kit (both starter beats + generations) at default settings: true peak under the ceiling, the limiter barely works
    {
        BeatsXProcessor q; q.applyKit(dk, true); setParam(q, "songmode", 0);
        const float ceiling = juce::Decibels::decibelsToGain(q.apvts.getRawParameterValue("ceiling")->load());
        auto work = [&](const juce::AudioBuffer<float>& off, float& gr) { int busy = 0, wins = 0; gr = 0; for (int a = 0; a + 480 <= off.getNumSamples(); a += 480) { float m = 0; for (int c = 0; c < 2; ++c) m = std::max(m, off.getMagnitude(c, a, 480));
            const float d = juce::Decibels::gainToDecibels(m / ceiling); gr = std::max(gr, d); if (d > 1.0f) ++busy; ++wins; } return 100.0 * busy / juce::jmax(1, wins); };
        for (const char* ref : { "Trap", "Drill", "Drill Dark", "Trap Hard" }) {   // (for reference: the older 808 kits at default settings, pattern A)
            BeatsXProcessor r; r.applyKit(kitIndexOf(ref), true); setParam(r, "songmode", 0); const auto on = renderBars(r, 8), off = renderBars(r, 8, 2); float gr = 0; const double busy = work(off, gr);
            std::printf("    reference %-10s %.1f LUFS, true peak %.2f dBFS, limiter work max %.1f dB, > 1 dB in %.1f %% of 10 ms windows\n", ref, lufs(on), juce::Decibels::gainToDecibels(truePeak4x(on)), gr, busy);
        }
        float worstTp = 0, worstGr = 0; double worstBusy = 0, lmin = 0, lmax = -100, lStart = 0;
        for (int g = 0; g < 12; ++g) {
            if (g == 1) setParam(q, "pattern", 2); if (g == 2) setParam(q, "pattern", 1);
            if (g >= 2) { q.model.toolSeed = 100 + g * 7; q.generatePattern(gb::drill::kStyle); }
            const auto on = renderBars(q, 8), off = renderBars(q, 8, 2);
            const float tp = truePeak4x(on); const double l = lufs(on); worstTp = std::max(worstTp, tp); lmin = std::min(lmin, l); lmax = std::max(lmax, l); if (g < 2) lStart = std::min(lStart, l);
            // gain reduction the limiter has to do = how far the un-limited mix goes over the ceiling, in 10 ms windows
            float gr = 0; const double busy = work(off, gr); worstGr = std::max(worstGr, gr); worstBusy = std::max(worstBusy, busy);
        }
        std::printf("  DRILL kit, 12 beats (A, B + 10 generations) x 8 bars: %.1f..%.1f LUFS, true peak %.2f dBFS (ceiling %.2f), limiter work: max %.1f dB, > 1 dB in %.1f %% of 10 ms windows\n", lmin, lmax, juce::Decibels::gainToDecibels(worstTp), juce::Decibels::gainToDecibels(ceiling), worstGr, worstBusy);
        CHECK(worstTp <= ceiling, "true peak %.2f dBFS over the ceiling", juce::Decibels::gainToDecibels(worstTp));
        CHECK(worstGr < 1.5f && worstBusy < 1.0, "the 808 sits under the limiter without pumping (max %.1f dB, %.1f %% busy)", worstGr, worstBusy);
        CHECK(lStart >= -15.5 && lmin >= -16.5 && lmax <= -12.0, "DRILL beats near -14 LUFS (starter beats within 1.5 LU, generations within 2.5 LU: %.1f..%.1f)", lmin, lmax);
    }

}
static void drillRenders(const juce::File& dir) {
    dir.createDirectory(); BeatsXProcessor p; p.applyKit(kitIndexOf("DRILL"), true); setParam(p, "songmode", 0);
    for (int i = 1; i <= 2; ++i) {
        p.model.toolSeed = 40 + i * 3; p.generatePattern(gb::drill::kStyle);
        const auto b = renderBars(p, 8); const auto f = dir.getChildFile("DRILL_" + juce::String(i) + ".wav"); writeWav24(b, f);
        const auto fam = gb::drill::bassPatterns()[(size_t) gb::drill::bassFamily(p.model.toolSeed)].name;
        std::printf("  %s: seed %d, %.0f BPM, 808 %s, snare spot %d, hat cell %d, %.1f s, %.1f LUFS, peak %.2f dBFS, true peak %.2f dBFS\n", f.getFullPathName().toRawUTF8(), p.model.toolSeed, p.apvts.getRawParameterValue("bpm")->load(), fam,
                    gb::drill::snareSpot(p.model.toolSeed), gb::drill::hatCell(p.model.toolSeed), b.getNumSamples() / 48000.0, lufs(b), juce::Decibels::gainToDecibels(peakOf(b)), juce::Decibels::gainToDecibels(truePeak4x(b)));
    }
}

// energy above the sub (100-1500 Hz) vs the sub (25-90 Hz), in dB, over the whole buffer (left channel, 16k-point frames)
static double harmonicRatioDb(const juce::AudioBuffer<float>& b) {
    const int N = 1 << 14; double hi = 0, sub = 0; const float* x = b.getReadPointer(0);
    for (int a = 0; a + N <= b.getNumSamples(); a += N / 2) { const auto sp = spectrum(x, a, 48000);
        for (int k = 1; k < (int) sp.mag.size(); ++k) { const double f = k * 48000.0 / N, e = (double) sp.mag[(size_t) k] * sp.mag[(size_t) k]; if (f >= 25 && f < 90) sub += e; else if (f >= 100 && f < 1500) hi += e; } }
    return 10.0 * std::log10((hi + 1e-20) / (sub + 1e-20));
}
static void testDrillGrit() {
    section("DRILL GRIT: GRIT 0 = installer #27, GRIT 100 = tough + dirty (slides, low 808, late dirty snare), still clean");
    const int dk = kitIndexOf("DRILL");
    BeatsXProcessor p; p.applyKit(dk, true); const auto roles = p.roles(); std::array<bool, gb::kTracks> none {};
    const int t808 = 0, tSn = 2, tClap = 3, tHat = 4, tKick = 1;
    struct Stat { double slides = 0, pitch = 0, notes = 0, lateMs = 0, clapMs = 0, kicks = 0, hatVel = 0, hats = 0, holdSteps = 0; int slidesOk = 0, overlapOk = 0, rangeOk = 0, dispOk = 0, smin = 99, smax = 0; };
    auto run = [&](int grit) { Stat S; const int N = 300;
        for (int seed = 1; seed <= N; ++seed) {
            gb::Pattern pt; pt.clear(); gb::rhythm::generate(pt, roles, none, gb::drill::kStyle, seed, grit);
            const int sl = gb::drill::countSlides(pt, t808, -1), b0 = gb::drill::countSlides(pt, t808, 0), b1 = gb::drill::countSlides(pt, t808, 1);
            S.slides += sl; S.smin = std::min(S.smin, sl); S.smax = std::max(S.smax, sl);
            if (grit >= 100 ? (sl >= 4 && sl <= 7 && b0 <= 4 && b1 <= 4) : (sl >= 2 && sl <= 6 && b0 <= 3 && b1 <= 3)) ++S.slidesOk;
            bool ovl = true, rng = true; int lastOn = -1;
            for (int s = 0; s < 32; ++s) { const auto& st = pt.steps[t808][(size_t) s]; if (!st.vel) continue; S.pitch += st.pitch; S.notes += 1;
                if (st.pitch < gb::drill::kLowest || st.pitch > gb::drill::kHighest || st.hold < 1 || st.slide() == st.mono()) rng = false;
                if (st.slide() && (lastOn < 0 || lastOn + pt.steps[t808][(size_t) lastOn].hold <= s)) ovl = false; lastOn = s; }
            for (int t = 0; t < gb::kTracks; ++t) for (int s = 32; s < gb::kMaxSteps; ++s) if (pt.steps[(size_t) t][(size_t) s].vel) rng = false;
            if (ovl) ++S.overlapOk; if (rng) ++S.rangeOk;
            { std::array<bool, 32> on {}; for (int s = 0; s < 32; ++s) { const auto& st = pt.steps[t808][(size_t) s]; if (st.vel) for (int k = s; k < juce::jmin(32, s + st.hold); ++k) on[(size_t) k] = true; } for (bool b : on) S.holdSteps += b; }   // steps the 808 sounds
            const double stepMs = 15000.0 / gb::drill::tempoFor(seed); const auto& sn = pt.steps[tSn];
            S.lateMs += sn[8].nudge * stepMs / 100.0; S.clapMs += pt.steps[tClap][8].nudge * stepMs / 100.0;
            if (sn[8].vel >= 100 && (sn[27].vel >= 100 || sn[28].vel >= 100) && !(sn[24].vel >= 100) && !(sn[20].vel >= 100)) ++S.dispOk;
            for (int s = 0; s < 32; ++s) { if (pt.steps[tKick][(size_t) s].vel) S.kicks += 1; const auto& h = pt.steps[tHat][(size_t) s]; if (h.vel) { S.hats += 1; S.hatVel += h.vel; } }
        }
        S.slides /= N; S.lateMs /= N; S.clapMs /= N; S.kicks /= N; S.hatVel /= juce::jmax(1.0, S.hats); S.hats /= N; S.pitch /= juce::jmax(1.0, S.notes); S.holdSteps /= N; S.notes /= N; return S; };
    const auto A = run(0), B = run(70), C = run(100);
    for (auto* x : { &A, &B, &C }) std::printf("  GRIT %3d: slides %.1f per 2 bars (%d..%d, in range %d / 300), 808 %.1f notes, mean pitch %+.2f st (from G1), sounding %.1f of 32 steps, snare %+.1f ms, clap %+.1f ms, kicks %.2f per 2 bars, hats %.1f at vel %.0f | displaced snare %d, overlaps %d, in range %d\n",
                                                x == &A ? 0 : (x == &B ? 70 : 100), x->slides, x->smin, x->smax, x->slidesOk, x->notes, x->pitch, x->holdSteps, x->lateMs, x->clapMs, x->kicks, x->hats, x->hatVel, x->dispOk, x->overlapOk, x->rangeOk);
    CHECK(C.slidesOk == 300 && C.slides > A.slides + 1.5, "GRIT 100: 4-7 slides per 2 bars, <= 4 per bar in %d / 300 (mean %.1f vs %.1f)", C.slidesOk, C.slides, A.slides);
    CHECK(C.pitch < A.pitch - 1.0 && B.pitch < A.pitch, "GRIT: the 808 sits lower (mean %+.2f st at 100, %+.2f at 70, %+.2f at 0)", C.pitch, B.pitch, A.pitch);
    CHECK(C.lateMs >= 10 && C.lateMs <= 20 && B.lateMs >= 10 && B.lateMs <= 20 && A.lateMs == 0 && C.clapMs > C.lateMs, "GRIT: the snare 10-20 ms late (%.1f at 100, %.1f at 70), the clap a hair behind it", C.lateMs, B.lateMs);
    CHECK(C.kicks < A.kicks && C.hatVel < A.hatVel && C.hats < A.hats && C.holdSteps > A.holdSteps, "GRIT: sparser kicks, softer + more gapped hats, longer 808 tails");
    CHECK(C.dispOk == 300 && C.overlapOk == 300 && C.rangeOk == 300 && B.overlapOk == 300 && B.rangeOk == 300, "GRIT keeps the drill rules (displaced bar-2 snare %d, slide overlaps %d, notes in range %d)", C.dispOk, C.overlapOk, C.rangeOk);
    {   // the DIRTY sounds: heavier 808 harmonics with the same clean sub; the processor swaps the DRILL pack sounds at GRIT 50+ and back below
        auto lib = gb::loadBeatsLibrary(48000.0, false); const auto* d = lib->byName("DRILL 808 DIRTY").get(); const auto* sl = lib->byName("DRILL 808 SLIDE").get(); const auto* sd = lib->byName("DRILL SNARE DIRTY").get();
        if (d && sl && sd) {
            const auto sp = spectrum(d->data.data(), 0, 48000), sq = spectrum(sl->data.data(), 0, 48000);
            const float hd = dBr(sp.peakIn(100, 700), sp.peakIn(40, 60)), hs = dBr(sq.peakIn(100, 700), sq.peakIn(40, 60));
            double f0 = 0; { const auto sf = spectrum(d->data.data(), (int) (0.5 * 48000), 48000); sf.peakIn(30, 150, &f0); }   // the strongest partial = the sub
            const double top = rmsDb(d->data.data(), 0, 4800), tail = rmsDb(d->data.data(), (int) (2.5 * 48000), 4800) - top, tailS = rmsDb(sl->data.data(), (int) (2.5 * 48000), 4800) - rmsDb(sl->data.data(), 0, 4800);
            std::printf("  DRILL 808 DIRTY: %.1f Hz, low-mid vs sub %+.1f dB (SLIDE %+.1f), level at 2.5 s %.1f dB (SLIDE %.1f) | DRILL SNARE DIRTY %.2f s\n", f0, hd, hs, tail, tailS, sd->data.size() / 48000.0);
            CHECK(f0 > 46 && f0 < 52 && hd > hs + 3 && tail > tailS + 3, "DRILL 808 DIRTY: same G1 sub, more growl, longer tail");
        } else CHECK(false, "the DIRTY sounds are missing");
        BeatsXProcessor q; q.applyKit(dk, true); setParam(q, "songmode", 0);
        q.model.drillGrit = 0; q.model.toolSeed = 20; q.generatePattern(gb::drill::kStyle); const bool clean0 = q.model.tracks[0].sound == "DRILL 808 SLIDE" && q.model.tracks[2].sound == "DRILL SNARE";
        for (auto& tr : q.model.tracks) tr.solo = false; q.model.tracks[0].solo = true; q.publish(); const double h0 = harmonicRatioDb(renderBars(q, 4));
        q.model.drillGrit = 100; q.model.toolSeed = 20; q.generatePattern(gb::drill::kStyle); const bool dirty = q.model.tracks[0].sound == "DRILL 808 DIRTY" && q.model.tracks[2].sound == "DRILL SNARE DIRTY" && q.model.tracks[9].sound == "DRILL 808 DIRTY";
        q.publish(); const double h1 = harmonicRatioDb(renderBars(q, 4));
        q.model.drillGrit = 30; q.generatePattern(gb::drill::kStyle); const bool back = q.model.tracks[0].sound == "DRILL 808 SLIDE" && q.model.tracks[2].sound == "DRILL SNARE";
        BeatsXProcessor t; t.applyKit(kitIndexOf("Trap"), true); t.model.drillGrit = 100; t.generatePattern(gb::drill::kStyle); const bool userKept = t.model.tracks[0].sound == "808 Long" && t.model.tracks[2].sound == "Trap Snare";
        std::printf("  the 808 alone (4 bars, seed 21): harmonics vs sub %+.1f dB at GRIT 0, %+.1f dB at GRIT 100 | sounds: GRIT 0 clean %d, 100 dirty %d, 30 back %d, other kits untouched %d\n", h0, h1, (int) clean0, (int) dirty, (int) back, (int) userKept);
        CHECK(h1 > h0 + 3 && clean0 && dirty && back && userKept, "GRIT 100: more 808 harmonic content; the DIRTY swap only touches the DRILL pack sounds");
    }
    {   // state: GRIT is saved with the beat; a beat saved before GRIT existed loads as GRIT 0; a new beat starts at 70
        BeatsXProcessor a; const int fresh = a.model.drillGrit; a.model.drillGrit = 37; const auto st = state(a); BeatsXProcessor b; b.setStateInformation(st.getData(), (int) st.getSize());
        auto tree = gb::modelToTree(a.model); tree.removeProperty("grit", nullptr); gb::Model old; gb::modelFromTree(old, tree, 48000.0);
        CHECK(fresh == 70 && b.model.drillGrit == 37 && old.drillGrit == 0, "GRIT state: new beat %d, round trip %d, an older beat loads as %d", fresh, b.model.drillGrit, old.drillGrit);
    }
    {   // clean at GRIT 70 and 100: the DRILL kit, 16 generations x 8 bars
        BeatsXProcessor q; q.applyKit(dk, true); setParam(q, "songmode", 0);
        const float ceiling = juce::Decibels::decibelsToGain(q.apvts.getRawParameterValue("ceiling")->load());
        float worstTp = 0, worstGr = 0; double worstBusy = 0, lmin = 0, lmax = -100;
        for (int g = 0; g < 16; ++g) {
            q.model.drillGrit = g < 8 ? 70 : 100; q.model.toolSeed = 300 + g * 11; q.generatePattern(gb::drill::kStyle);
            const auto on = renderBars(q, 8), off = renderBars(q, 8, 2); const float tp = truePeak4x(on); const double l = lufs(on);
            worstTp = std::max(worstTp, tp); lmin = std::min(lmin, l); lmax = std::max(lmax, l);
            int busy = 0, wins = 0; for (int a = 0; a + 480 <= off.getNumSamples(); a += 480) { float m = 0; for (int c = 0; c < 2; ++c) m = std::max(m, off.getMagnitude(c, a, 480));
                const float d = juce::Decibels::gainToDecibels(m / ceiling); worstGr = std::max(worstGr, d); if (d > 1.0f) ++busy; ++wins; }
            worstBusy = std::max(worstBusy, 100.0 * busy / juce::jmax(1, wins));
        }
        std::printf("  GRIT 70 / 100, 16 generations x 8 bars: %.1f..%.1f LUFS, true peak %.2f dBFS (ceiling %.2f), limiter work max %.1f dB, > 1 dB in %.1f %% of 10 ms windows\n", lmin, lmax, juce::Decibels::gainToDecibels(worstTp), juce::Decibels::gainToDecibels(ceiling), worstGr, worstBusy);
        CHECK(worstTp <= ceiling && worstGr < 1.5f && worstBusy < 1.0, "GRIT beats clean (true peak %.2f dBFS, limiter max %.1f dB)", juce::Decibels::gainToDecibels(worstTp), worstGr);
        CHECK(lmin >= -16.5 && lmax <= -12.0, "GRIT beats near -14 LUFS (%.1f..%.1f)", lmin, lmax);
    }
}
static void gritRenders(const juce::File& dir) {
    dir.createDirectory(); BeatsXProcessor p; p.applyKit(kitIndexOf("DRILL"), true); setParam(p, "songmode", 0);
    for (int i = 0; i < 2; ++i) {   // the DRILL kit's starter beats (GRIT 70) as the kit loads: patterns A and B
        setParam(p, "pattern", (float) (i + 1)); const auto b = renderBars(p, 8); const auto f = dir.getChildFile(juce::String("DRILL_STARTER_") + (i ? "B" : "A") + ".wav"); writeWav24(b, f);
        std::printf("  %s: DRILL kit starter %c (GRIT %d), %.0f BPM, %d slides, %.1f s, %.1f LUFS, true peak %.2f dBFS\n", f.getFullPathName().toRawUTF8(), i ? 'B' : 'A', gb::drill::kStarterGrit, p.apvts.getRawParameterValue("bpm")->load(),
                    gb::drill::countSlides(p.model.patterns[(size_t) i], 0, -1), b.getNumSamples() / 48000.0, lufs(b), juce::Decibels::gainToDecibels(truePeak4x(b)));
    }
    setParam(p, "pattern", 1);
    for (int i = 1; i <= 2; ++i) {
        p.model.drillGrit = i == 1 ? 85 : 100; p.model.toolSeed = 60 + i * 5; p.generatePattern(gb::drill::kStyle);
        const auto b = renderBars(p, 8); const auto f = dir.getChildFile("DRILL_GRIT_" + juce::String(i) + ".wav"); writeWav24(b, f);
        std::printf("  %s: GRIT %d, seed %d, %.0f BPM, 808 %s (%d slides), snare spot %d, %.1f s, %.1f LUFS, peak %.2f dBFS, true peak %.2f dBFS\n", f.getFullPathName().toRawUTF8(), p.model.drillGrit, p.model.toolSeed, p.apvts.getRawParameterValue("bpm")->load(),
                    gb::drill::bassPatterns()[(size_t) gb::drill::bassFamily(p.model.toolSeed)].name, gb::drill::countSlides(p.model.patterns[(size_t) p.currentPattern()], 0, -1), gb::drill::snareSpot(p.model.toolSeed), b.getNumSamples() / 48000.0, lufs(b), juce::Decibels::gainToDecibels(peakOf(b)), juce::Decibels::gainToDecibels(truePeak4x(b)));
    }
}

static void testReggaeton() {
    section("REGGAETON style + the LATIN kit: the dembow, the pack, tempo, layers, levels, state");
    namespace L = gb::latin;
    // ---- 1. the LATIN pack: appended last, in the right folders, levelled like the rest, tuned
    {
        auto lib = gb::loadBeatsLibrary(48000.0, false); const int n = (int) lib->sounds().size(), nl = gb::latinPackNames().size(), nc = gb::classicPackNames().size();
        static const char* cats[] { "Kick", "Snare", "Snare", "Perc", "Perc", "Perc", "Perc", "Perc", "Perc", "Perc", "Perc", "Perc", "Perc", "Clap" };
        std::map<juce::String, std::vector<float>> older; for (auto& sp : lib->sounds()) if (sp->category != "Break" && !gb::isPackSound(sp->name)) older[gb::folderOf(sp->category)].push_back(gb::loudest200ms(*sp));
        for (auto& kv : older) std::sort(kv.second.begin(), kv.second.end());
        int found = 0, atEnd = 0, catOk = 0, peakOk = 0, trimOk = 0;
        for (int i = 0; i < nl; ++i) { const int at = lib->indexOf(gb::latinPackNames()[i]); if (at < 0) continue; ++found; const auto& sd = *lib->sounds()[(size_t) at];
            if (at >= n - nc - nl && at < n - nc) ++atEnd; if (sd.category == cats[i]) ++catOk;   // (the classic-character packs follow it)
            float pk = 0; for (auto v : sd.data) pk = std::max(pk, std::abs(v)); if (pk > 0.85f && pk < 0.9f) ++peakOk;
            const auto& v = older[gb::folderOf(sd.category)]; const float med = v[v.size() / 2], lv = gb::loudest200ms(sd) + juce::Decibels::gainToDecibels(sd.trim);
            if (std::abs(lv - med) < 0.6f) ++trimOk; else std::printf("  %s: level %.1f dB vs folder median %.1f\n", sd.name.toRawUTF8(), lv, med); }
        CHECK(nl == 14 && found == nl && atEnd == nl && catOk == nl, "LATIN pack: %d found, %d at the end of the library, %d in the right folder", found, atEnd, catOk);
        CHECK(peakOk == nl && trimOk == nl, "LATIN pack levels: %d peak-normalised, %d levelled to their folder median", peakOk, trimOk);
        auto pitch = [&](const char* nm, double lo, double hi) { const auto* sd = lib->byName(nm).get(); if (!sd) return 0.0; std::vector<float> x(1 << 14, 0.0f); std::copy_n(sd->data.begin(), juce::jmin(sd->data.size(), x.size()), x.begin());
            double at = 0; spectrum(x.data(), 0, 48000).peakIn(lo, hi, &at); return at; };
        const double kick = pitch("REGGAETON KICK", 30, 200), chi = pitch("LATIN CONGA HI", 150, 600), clo = pitch("LATIN CONGA LO", 120, 400), bhi = pitch("LATIN BONGO HI", 300, 900), blo = pitch("LATIN BONGO LO", 250, 700),
                     thi = pitch("TIMBALE HI", 300, 800), tlo = pitch("TIMBALE LO", 250, 600), clave = pitch("LATIN CLAVE", 1000, 4000), bell = pitch("LATIN COWBELL", 400, 1500), shk = pitch("LATIN SHAKER", 1000, 20000);
        std::printf("  tuning: kick %.0f Hz, conga %.0f / %.0f, bongo %.0f / %.0f, timbale %.0f / %.0f, clave %.0f, cowbell %.0f, shaker energy at %.0f Hz | kick %.2f s, snare %.2f s\n", kick, chi, clo, bhi, blo, thi, tlo, clave, bell, shk,
                    lib->byName("REGGAETON KICK") ? lib->byName("REGGAETON KICK")->data.size() / 48000.0 : 0.0, lib->byName("DEMBOW SNARE") ? lib->byName("DEMBOW SNARE")->data.size() / 48000.0 : 0.0);
        CHECK(kick < 80 && chi > clo && bhi > blo && bhi > chi && thi > tlo && clave > 2000 && shk > 4000, "LATIN pack tuning: hi drums above lo, bongos above congas, kick low, clave + shaker bright");
    }
    // ---- 2. GENERATE REGGAETON: the rules over 300 generations on the LATIN kit
    const int lk = kitIndexOf("LATIN");
    CHECK(juce::String(gb::allKits()[(size_t) lk].name) == "LATIN" && lk == 32, "the LATIN kit keeps its index (%d; the classic-character kits follow it)", lk);
    BeatsXProcessor p; p.applyKit(lk, true); const auto roles = p.roles(); std::array<bool, gb::kTracks> none {};
    const auto kinds = L::kindsOf(p.model, [&](const juce::String& nm) { return p.categoryOf(nm); });
    {   juce::String ks; for (auto k : kinds) ks << (int) k << " "; std::printf("  LATIN kit kinds: %s\n", ks.toRawUTF8());
        bool distinct = true; for (int a = 0; a < gb::kTracks; ++a) for (int b = a + 1; b < gb::kTracks; ++b) if (kinds[(size_t) a] == kinds[(size_t) b]) distinct = false;
        CHECK(distinct && kinds[0] == L::KKick && kinds[2] == L::KSnare && kinds[3] == L::KRim && kinds[5] == L::KShaker && kinds[11] == L::KTimbHi && kinds[12] == L::KTimbLo && kinds[13] == L::KTimbRim && kinds[15] == L::KClap, "LATIN kit: every track its own part"); }
    auto trackOf = [&](int kind) { for (int t = 0; t < gb::kTracks; ++t) if (kinds[(size_t) t] == kind) return t; return -1; };
    const int N = 300; int dembow = 0, kickOk = 0, lenOk = 0, shakerOk = 0, claveOk = 0, congaOk = 0, fillOk = 0, layersOk = 0, rangeOk = 0, swingOk = 0, rot = 0, tempoOk = 0, bassOk = 0;
    long barsTotal = 0, barsDembow = 0; std::set<int> fams, tempos; int layersMin = 99, layersMax = 0;
    for (int seed = 1; seed <= N; ++seed) {
        gb::Pattern pt; pt.clear(); gb::rhythm::generate(pt, roles, none, L::kStyle, seed, 0, &kinds);
        const auto& F = L::families()[(size_t) L::family(seed)]; const int bars = pt.length / 16;
        if ((pt.length == 64 || pt.length == 128) && bars == F.fillBars && pt.res == 1 && pt.fillEvery == 0) ++lenOk;
        bool all = true, kk = true; for (int b = 0; b < bars; ++b) { ++barsTotal; if (L::dembowInBar(pt, kinds, b)) ++barsDembow; else all = false;
            const auto& kr = pt.steps[(size_t) trackOf(L::KKick)]; if (!kr[(size_t) (b * 16)].vel || !kr[(size_t) (b * 16 + 8)].vel) kk = false; }
        if (all) ++dembow; if (kk) ++kickOk;
        { const auto& sh = pt.steps[(size_t) trackOf(L::KShaker)]; int c = 0; for (int s = 0; s < pt.length; ++s) c += sh[(size_t) s].vel > 0; if (c == pt.length) ++shakerOk; }
        { const auto& cl = pt.steps[(size_t) trackOf(L::KClave)]; int c = 0; for (int s = 0; s < pt.length; ++s) c += cl[(size_t) s].vel > 0; if (c == bars / 2 * 5) ++claveOk; }
        { int hands = 0; for (int k : { L::KCongaHi, L::KCongaLo, L::KBongoHi, L::KBongoLo }) { const auto& r = pt.steps[(size_t) trackOf(k)]; for (int s = 0; s < 16; ++s) hands += r[(size_t) s].vel > 0; }
          if (hands >= 3 && hands <= 20) ++congaOk; }
        {   // the timbale fill in the last bar (>= 4 hits over its second half, building), nothing else from the timbales in bars 1-3 but pickups
            const auto& th = pt.steps[(size_t) trackOf(L::KTimbHi)]; const auto& tl = pt.steps[(size_t) trackOf(L::KTimbLo)]; const int o = (bars - 1) * 16; int c = 0, lastV = 0, firstV = 0;
            for (int s = o + 8; s < o + 16; ++s) for (auto* r : { &th, &tl }) if ((*r)[(size_t) s].vel) { ++c; if (!firstV) firstV = (*r)[(size_t) s].vel; lastV = (*r)[(size_t) s].vel; }
            if (c >= 4 && lastV > firstV) ++fillOk; }
        { int active = 0; for (int k : { L::KShaker, L::KClave, L::KCongaHi, L::KCongaLo, L::KBongoHi, L::KBongoLo, L::KTimbHi, L::KTimbLo, L::KTimbRim, L::KBell }) { const auto& r = pt.steps[(size_t) trackOf(k)]; bool any = false; for (int s = 0; s < pt.length; ++s) any = any || r[(size_t) s].vel; active += any; }
          layersMin = std::min(layersMin, active); layersMax = std::max(layersMax, active); if (active >= 5 && active <= 10) ++layersOk; }
        bool inRange = true, sw = true;
        for (int t = 0; t < gb::kTracks; ++t) for (int s = 0; s < gb::kMaxSteps; ++s) { const auto& st = pt.steps[(size_t) t][(size_t) s]; if (!st.vel) continue;
            if (s >= pt.length || st.vel > 127 || st.roll != 1) inRange = false;
            const int k = kinds[(size_t) t]; if (k == L::KHat || k == L::KShaker) { if (st.nudge < 0 || st.nudge > 6 || (st.nudge && !(s & 1))) sw = false; } else if (st.nudge) sw = false; }
        if (inRange) ++rangeOk; if (sw) ++swingOk;
        { const auto& br = pt.steps[(size_t) trackOf(L::KBass)]; bool ok2 = true; for (int b = 0; b < bars; ++b) { const auto& a = br[(size_t) (b * 16)]; if (!a.vel || !a.mono() || a.pitch < -8 || a.pitch > 8) ok2 = false; } if (ok2) ++bassOk; }
        fams.insert(L::family(seed)); if (seed > 1 && L::family(seed) != L::family(seed - 1)) ++rot;
        const int bpm = L::tempoFor(seed); tempos.insert(bpm); if (bpm >= 88 && bpm <= 100) ++tempoOk;
    }
    std::printf("  %d generations: dembow figure in every bar %d (%ld / %ld bars), kick on 1 + 3 %d, 4 / 8-bar lengths %d, shaker 16ths %d, clave 5 per 2 bars %d, hand drums %d, timbale fill %d, perc layers %d..%d (in range %d), bass %d, steps in range %d, swing on hats + shaker only %d\n",
                N, dembow, barsDembow, barsTotal, kickOk, lenOk, shakerOk, claveOk, congaOk, fillOk, layersMin, layersMax, layersOk, bassOk, rangeOk, swingOk);
    std::printf("  rotation: %d families seen, the family changes on every new generation %d / %d | tempo %d..%d BPM (%d values)\n", (int) fams.size(), rot, N - 1, *tempos.begin(), *tempos.rbegin(), (int) tempos.size());
    CHECK(dembow >= N * 95 / 100, "the dembow figure in %d / %d generations", dembow, N);
    CHECK(kickOk == N && lenOk == N && bassOk == N, "kick on 1 and 3 %d, lengths %d, bass %d", kickOk, lenOk, bassOk);
    CHECK(shakerOk == N && claveOk == N && congaOk == N && fillOk == N && layersOk == N, "perc layers: shaker %d, clave %d, hand drums %d, timbale fills %d, layer count %d", shakerOk, claveOk, congaOk, fillOk, layersOk);
    CHECK(rangeOk == N && swingOk == N, "steps in range %d, swing %d", rangeOk, swingOk);
    CHECK(fams.size() == L::families().size() && fams.size() >= 8 && rot == N - 1, "%d pattern families, rotation", (int) fams.size());
    CHECK(tempoOk == N && tempos.size() == 13, "tempo mapping 88-100");
    {   // another kit (no Latin sounds): the dembow still lands (snare / rim), perc tracks get the hand parts; through the processor: the tempo lane
        BeatsXProcessor q; q.applyKit(kitIndexOf("Trap"), true); setParam(q, "songmode", 0); int ok = 0;
        const auto qk = L::kindsOf(q.model, [&](const juce::String& nm) { return q.categoryOf(nm); });
        for (int seed = 1; seed <= 50; ++seed) { gb::Pattern pt; pt.clear(); gb::rhythm::generate(pt, q.roles(), none, L::kStyle, seed, 0, &qk); bool all = true; for (int b = 0; b < pt.length / 16; ++b) all = all && L::dembowInBar(pt, qk, b); ok += all; }
        q.model.toolSeed = 12; q.generatePattern(L::kStyle); const float bpm = q.apvts.getRawParameterValue("bpm")->load();
        const auto& pt = q.model.patterns[(size_t) q.currentPattern()];
        std::printf("  Trap kit: dembow in %d / 50 generations | GENERATE REGGAETON (seed %d): %.0f BPM (wants %d), %d bars\n", ok, q.model.toolSeed, bpm, L::tempoFor(q.model.toolSeed), pt.length / 16);
        CHECK(ok == 50 && std::lround(bpm) == L::tempoFor(q.model.toolSeed), "REGGAETON on another kit + tempo lane");
    }
    {   // state round trip: a generated REGGAETON beat saves and loads exactly
        BeatsXProcessor a; a.applyKit(lk, true); setParam(a, "songmode", 0); a.model.toolSeed = 33; a.generatePattern(L::kStyle); a.publish();
        const auto st = state(a); BeatsXProcessor b; b.setStateInformation(st.getData(), (int) st.getSize());
        bool same = b.model.kit == "LATIN"; for (int i = 0; i < 2; ++i) same = same && std::memcmp(a.model.patterns[(size_t) i].steps.data(), b.model.patterns[(size_t) i].steps.data(), sizeof(gb::Pattern::steps)) == 0 && a.model.patterns[(size_t) i].length == b.model.patterns[(size_t) i].length;
        for (int t = 0; t < gb::kTracks; ++t) same = same && a.model.tracks[(size_t) t].sound == b.model.tracks[(size_t) t].sound && a.model.tracks[(size_t) t].vol == b.model.tracks[(size_t) t].vol;
        const auto ha = fnvBuf(renderBars(a, 4)), hb = fnvBuf(renderBars(b, 4));
        CHECK(same && ha == hb, "REGGAETON state round trip (steps, kit, mix, render %016llx vs %016llx)", (unsigned long long) ha, (unsigned long long) hb);
    }
    {   // clean + the kit rule: starter beats A / B and 10 generations, 8 bars each: near -14 LUFS, true peak under the ceiling, the limiter resting
        BeatsXProcessor q; q.applyKit(lk, true); setParam(q, "songmode", 0);
        const float ceiling = juce::Decibels::decibelsToGain(q.apvts.getRawParameterValue("ceiling")->load());
        float worstTp = 0, worstGr = 0; double worstBusy = 0, lmin = 0, lmax = -100, sMin = 0, sMax = -100;
        for (int g = 0; g < 12; ++g) {
            if (g == 1) setParam(q, "pattern", 2); if (g == 2) setParam(q, "pattern", 1);
            if (g >= 2) { q.model.toolSeed = 200 + g * 13; q.generatePattern(L::kStyle); }
            const auto on = renderBars(q, 8), off = renderBars(q, 8, 2); const float tp = truePeak4x(on); const double l = lufs(on);
            worstTp = std::max(worstTp, tp); lmin = std::min(lmin, l); lmax = std::max(lmax, l); if (g < 2) { sMin = std::min(sMin, l); sMax = std::max(sMax, l); }
            int busy = 0, wins = 0; for (int a = 0; a + 480 <= off.getNumSamples(); a += 480) { float m = 0; for (int c = 0; c < 2; ++c) m = std::max(m, off.getMagnitude(c, a, 480));
                const float d = juce::Decibels::gainToDecibels(m / ceiling); worstGr = std::max(worstGr, d); if (d > 1.0f) ++busy; ++wins; }
            worstBusy = std::max(worstBusy, 100.0 * busy / juce::jmax(1, wins));
        }
        std::printf("  LATIN kit, 12 beats (A, B + 10 generations) x 8 bars: %.1f..%.1f LUFS (starters %.1f..%.1f), true peak %.2f dBFS (ceiling %.2f), limiter work max %.1f dB, > 1 dB in %.1f %% of 10 ms windows\n",
                    lmin, lmax, sMin, sMax, juce::Decibels::gainToDecibels(worstTp), juce::Decibels::gainToDecibels(ceiling), worstGr, worstBusy);
        CHECK(worstTp <= ceiling && worstGr < 1.5f && worstBusy < 1.0, "REGGAETON clean (true peak %.2f dBFS, limiter max %.1f dB)", juce::Decibels::gainToDecibels(worstTp), worstGr);
        CHECK(sMin >= -15.5 && sMax <= -12.5 && lmin >= -16.5 && lmax <= -11.5, "LATIN kit near -14 LUFS (starters within 1.5 LU, generations within 2.5 LU: %.1f..%.1f)", lmin, lmax);
    }
}
static void reggaetonRenders(const juce::File& dir) {
    dir.createDirectory(); BeatsXProcessor p; p.applyKit(kitIndexOf("LATIN"), true); setParam(p, "songmode", 0);
    for (int i = 1; i <= 2; ++i) {
        p.model.toolSeed = i == 1 ? 9 : 13; p.generatePattern(gb::latin::kStyle); const int sd = p.model.toolSeed;
        const auto b = renderBars(p, 8); const auto f = dir.getChildFile("REGGAETON_" + juce::String(i) + ".wav"); writeWav24(b, f);
        std::printf("  %s: seed %d, %.0f BPM, %s (%d-bar cycle), clave %s, %.1f s, %.1f LUFS, peak %.2f dBFS, true peak %.2f dBFS\n", f.getFullPathName().toRawUTF8(), sd, p.apvts.getRawParameterValue("bpm")->load(),
                    gb::latin::families()[(size_t) gb::latin::family(sd)].name, gb::latin::families()[(size_t) gb::latin::family(sd)].fillBars, gb::latin::claveSide(sd) ? "2-3" : "3-2", b.getNumSamples() / 48000.0, lufs(b), juce::Decibels::gainToDecibels(peakOf(b)), juce::Decibels::gainToDecibels(truePeak4x(b)));
    }
}

#include "CharacterTests.inc"
#include "StyleTests.inc"

static void testPunchList() {
    section("FINAL GATE: blockers");
    {   // 3. script compiler: bounded recursion + source cap, and the runtime budget covers every script that compiles
        juce::String deep = "if "; for (int i = 0; i < 40000; ++i) deep << "("; deep << "1"; for (int i = 0; i < 40000; ++i) deep << ")"; deep << " then mute()";
        auto a = gb::script::Compiler::compile(deep); juce::String u = "vel("; for (int i = 0; i < 200000; ++i) u << "-"; u << "1)"; auto b = gb::script::Compiler::compile(u);
        juce::String ok = "vel("; for (int i = 0; i < 40; ++i) ok << "("; ok << "1"; for (int i = 0; i < 40; ++i) ok << ")"; ok << ")"; auto c = gb::script::Compiler::compile(ok);
        juce::String deep2 = "vel("; for (int i = 0; i < 100; ++i) deep2 << "("; deep2 << "1"; for (int i = 0; i < 100; ++i) deep2 << ")"; deep2 << ")"; auto d2 = gb::script::Compiler::compile(deep2);
        std::printf("  40000 nested parens: %s | 200000 unary minus: %s | 100 levels: %s | 40 levels: %s\n", a->error.toRawUTF8(), b->error.toRawUTF8(), d2->error.toRawUTF8(), c->ok() ? "ok" : c->error.toRawUTF8());
        CHECK(!a->ok() && (a->error.contains("deep") || a->error.contains("long")) && !b->ok() && !d2->ok() && d2->error.contains("deep") && c->ok(), "script depth / length limits");
        juce::String big; for (int i = 0; i < 600; ++i) big << "vel(vel + 1)\n"; auto d = gb::script::Compiler::compile(big); gb::script::Env e; gb::Step st; st.vel = 10; const bool ran = gb::script::run(*d, e, st);
        std::printf("  2400-op module: compiled %d, ran %d, vel 10 -> %d\n", (int) d->ok(), (int) ran, (int) st.vel); CHECK(d->ok() && ran && st.vel == 127, "runtime budget >= compile cap");
    }
    {   // 2. aliasing: a 15 kHz tone pitched +4 st at 48 k, and played in a 44.1 k host
        BeatsXProcessor p; p.applyKit(0, true); for (auto& pt : p.model.patterns) pt.clear(); auto& tr = p.model.tracks[0]; tr.sample = makeSound("sine", 48000, [](int i) { return 0.5f * (float) std::sin(juce::MathConstants<double>::twoPi * 15000.0 * i / 48000.0); }); tr.sound = "sine"; tr.vol = 1;
        p.model.patterns[0].length = 16; p.model.patterns[0].steps[0][0].vel = 127; p.publish(); setParam(p, "glue", 0); setParam(p, "songmode", 0); setParam(p, "bpm", 60); setParam(p, "volume", 0.2f);
        tr.pitch = 4; p.publish(); auto out = runProc(p, 48000, 512, 0.9); auto sp = spectrum(out.getReadPointer(0), 48000 / 5, 48000);
        double fa = 0, fw = 0; const float f = sp.peakIn(18700, 19100, &fa), w = sp.peakIn(300, 14000, &fw);
        tr.pitch = 0; p.publish(); auto out2 = runProc(p, 44100, 512, 0.9); auto sp2 = spectrum(out2.getReadPointer(0), 44100 / 5, 44100);
        double fb = 0, fw2 = 0; const float f2 = sp2.peakIn(14900, 15100, &fb), w2 = sp2.peakIn(300, 14000, &fw2);   // (the library sound keeps its own pitch at any host rate)
        std::printf("  48k +4 st: tone %.0f Hz, worst spur 0.3-14 kHz %.0f Hz at %.1f dB | 44.1k host: tone %.0f Hz, worst spur %.0f Hz at %.1f dB (target < -60)\n", fa, fw, dBr(w, f), fb, fw2, dBr(w2, f2));
        { std::vector<std::pair<float, double>> pk; for (int b = 2; b + 1 < (int) sp2.mag.size(); ++b) if (sp2.mag[(size_t) b] > sp2.mag[(size_t) b - 1] && sp2.mag[(size_t) b] >= sp2.mag[(size_t) b + 1]) pk.push_back({ sp2.mag[(size_t) b], b * sp2.sr / sp2.N });
          std::printf("    (engine look-ahead %d samples = %.0f Hz rate; zero-crossing freq of the 44.1k output %.0f Hz, of the 48k +4 output %.0f Hz)\n", p.engine.lookahead(), p.engine.lookahead() / 0.0015, zcFreq(out2.getReadPointer(0), 8820, 8820, 44100), zcFreq(out.getReadPointer(0), 9600, 9600, 48000));
          std::sort(pk.rbegin(), pk.rend()); std::printf("    44.1k top peaks:"); for (int i = 0; i < 6 && i < (int) pk.size(); ++i) std::printf(" %.0fHz@%.1fdB", pk[(size_t) i].second, 20 * std::log10(pk[(size_t) i].first / pk[0].first)); std::printf("  (out peak %.3f)\n", out2.getMagnitude(0, 0, out2.getNumSamples())); }
        CHECK(dBr(w, f) < -60 && dBr(w2, f2) < -60, "aliasing below -60 dB");
    }
    {   // 1. popups do not outlive the editor
        BeatsXProcessor p; std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor()); ed->setVisible(true); for (int i = 0; i < 3; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        bx::StepGrid* grid = nullptr; std::function<void(juce::Component&)> find = [&](juce::Component& c) { if (auto* g = dynamic_cast<bx::StepGrid*>(&c)) grid = g; for (auto* ch : c.getChildren()) find(*ch); }; find(*ed);
        if (grid) bx::StepEditor::open(grid->ed, 0, 0, *grid, {}); for (int i = 0; i < 3; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        int boxes = 0; for (int i = 0; i < juce::Desktop::getInstance().getNumComponents(); ++i) if (dynamic_cast<juce::CallOutBox*>(juce::Desktop::getInstance().getComponent(i))) ++boxes;
        ed.reset(); for (int i = 0; i < 5; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
        int after = 0; for (int i = 0; i < juce::Desktop::getInstance().getNumComponents(); ++i) if (dynamic_cast<juce::CallOutBox*>(juce::Desktop::getInstance().getComponent(i))) ++after;
        std::printf("  step editor popups: %d open with the editor, %d after the editor closed\n", boxes, after); CHECK(grid && boxes == 1 && after == 0, "popup closes with the editor");
    }
    section("FINAL GATE: bugs");
    {   // 4. hostile project data is clamped
        BeatsXProcessor p; const char junk[] = "<?xml version=\"1.0\"?><GHOSTYBEATS2><BEATS2 ver=\"3\"><PATTERN i=\"40\" res=\"9\"/><BLOCK p=\"99\" start=\"-7\" bars=\"0\"/><TRACK i=\"-1\"/><MOTIF track=\"99\" len=\"99\"/><SCRIPT code=\"if (\"/></BEATS2></GHOSTYBEATS2>";
        juce::MemoryBlock mb; auto x = juce::XmlDocument::parse(juce::String(junk)); p.copyXmlToBinary(*x, mb); p.restoreState(mb.getData(), (int) mb.getSize());
        const bool ok = !p.model.song.empty() && p.model.song[0].pattern == 15 && p.model.song[0].startBar == 0 && p.model.song[0].bars == 1;
        setParam(p, "songmode", 1); auto f = p.renderNow({ true, true, -1 }); std::printf("  song block (99, -7, 0) loads as (%d, %d, %d); stems render of it survives (%d files)\n", p.model.song[0].pattern, p.model.song[0].startBar, p.model.song[0].bars, f.size());
        CHECK(ok, "state clamps");
    }
    {   // 7. .ghostpat keeps 1/64; 6. a BREAK / GENERATE keep the grid
        BeatsXProcessor p; p.applyKit(0, true); p.model.patterns[0].res = 5; p.model.patterns[0].length = 64; p.publish(); p.savePattern(0, "gate-res64"); auto f = p.userDir().getChildFile("gate-res64.ghostpat"); p.loadPattern(f, 1); f.deleteFile();
        CHECK(p.model.patterns[1].res == 5, ".ghostpat 1/64 round trip (got %s)", gb::resNames()[p.model.patterns[1].res].toRawUTF8());
        p.applyKit(0, true); auto& pt = p.model.patterns[0]; pt.res = 2; pt.length = 32; pt.steps[4][3].vel = 100; p.publish(); p.setTrackSound(15, "Break Rolling");
        const bool brk = pt.res == 2 && pt.length == 32 && pt.steps[4][3].vel == 100 && !p.model.tracks[15].slices.empty();
        p.applyKit(0, true); p.model.patterns[0].res = 2; p.model.patterns[0].length = 64; p.publish(); p.generatePattern(0); int hits = gb::rhythm::hitCount(p.model.patterns[0]);
        std::printf("  BREAK on a 1/32 pattern: res %s, %d steps, hat still at step 4 | GENERATE on 1/32: res %s, %d steps, %d hits\n", gb::resNames()[pt.res].toRawUTF8(), pt.length, gb::resNames()[p.model.patterns[0].res].toRawUTF8(), p.model.patterns[0].length, hits);
        CHECK(brk && p.model.patterns[0].res == 2 && p.model.patterns[0].length == 64 && hits > 20, "grid resolution kept");
        p.model.patterns[0].res = 2; p.model.patterns[0].length = 128; CHECK(p.model.patterns[0].bars() == 4 && gb::kMaxSteps == 128, "128 steps = 4 bars of 1/32");
    }
    {   // 10. swing on the 16th grid: a 1/32 pattern swings its 16ths exactly like a 1/16 pattern
        auto click = makeSound("click", 48, [](int i) { return std::exp(-i / 6.0f); });
        auto S16 = oneTrack(click.get()); auto P = cleanParams(120); P.swing = 0.5f; P.groove = 2; for (int s = 0; s < 8; ++s) S16.patterns[0].steps[0][(size_t) s].vel = 100;
        auto S32 = oneTrack(click.get()); S32.patterns[0].res = 2; S32.patterns[0].length = 32; for (int s = 0; s < 16; s += 2) S32.patterns[0].steps[0][(size_t) s].vel = 100;
        const auto a = onsets(render(S16, P, 1.0)), b = onsets(render(S32, P, 1.0));
        std::printf("  1/16 vs 1/32 onsets with swing 50%% + Swing 58%%: %d / %d hits, identical %s (2nd hit at %d vs %d)\n", (int) a.size(), (int) b.size(), a == b ? "yes" : "NO", a.size() > 1 ? a[1] : -1, b.size() > 1 ? b[1] : -1);
        CHECK(a == b && a.size() == 8 && a[1] == 6000 + 960 + 1500, "swing by 16th position");   // Swing 58 % = 0.16 step, swing 50 % = 0.25 step
    }
    {   // 11. MIDI notes record; 12. a host without a tempo; 23. the hit cap
        BeatsXProcessor p; p.applyKit(4, true); setParam(p, "songmode", 0); FakeHead ph; p.setPlayHead(&ph); p.prepareToPlay(48000, 512); juce::AudioBuffer<float> b(2, 512); juce::MidiBuffer m;
        auto pass = [&](double beats, bool haveBpm) { ph.haveBpm = haveBpm; const double spb = 48000 * 60.0 / 120; const uint32_t t0 = p.engine.trig[0].load(); for (long i = 0; i * 512 < beats * spb; ++i) { ph.ppq = i * 512 / spb; b.clear(); p.processBlock(b, m); } return (int) (p.engine.trig[0].load() - t0); };
        const int withBpm = pass(8, true), noBpm = pass(8, false); std::printf("  host playing 8 beats at 120: %d kicks with a tempo, %d without one\n", withBpm, noBpm); CHECK(withBpm == 8 && noBpm == 8, "no-tempo host");
        p.recording.store(true); ph.haveBpm = true; ph.ppq = 1.0; m.addEvent(juce::MidiMessage::noteOn(1, 36 + 7, (juce::uint8) 100), 0); b.clear(); p.processBlock(b, m); m.clear(); p.recording.store(false);
        const int ready = p.engine.recFifo.getNumReady(); p.drainRecorded(); const auto& st = p.model.patterns[0].steps[7][4];
        std::printf("  MIDI note (track 8) at beat 1 while REC: %d recorded event(s) -> step 5 vel %d\n", ready, (int) st.vel); CHECK(ready == 1 && st.vel == 100, "MIDI-in recording");
        p.setPlayHead(nullptr);
        for (auto& pt : p.model.patterns) pt.clear(); auto& pt = p.model.patterns[0]; pt.res = 5; pt.length = 64; for (int t = 0; t < gb::kTracks; ++t) for (int s = 0; s < 64; ++s) { auto& x = pt.steps[(size_t) t][(size_t) s]; x.vel = 100; x.roll = 16; x.rate = 64; } p.publish(); p.hostBpm.store(0); setParam(p, "bpm", 174);
        auto spec = p.makeSpec({ false, false, -1 }); int wanted = 0; gb::collectEvents(*spec.snap, spec.P, spec.P.bpm, 0.0, 8192 / (48000 * 60.0 / spec.P.bpm), [&](int, double, float, const gb::EvInfo&) { ++wanted; });
        p.prepareToPlay(48000, 8192); p.engine.internalPlay = true; juce::AudioBuffer<float> big(2, 8192); uint32_t t0 = 0; for (int t = 0; t < gb::kTracks; ++t) t0 += p.engine.trig[t].load(); big.clear(); p.processBlock(big, m); uint32_t t1 = 0; for (int t = 0; t < gb::kTracks; ++t) t1 += p.engine.trig[t].load(); p.engine.internalPlay = false;
        std::printf("  machine-gun worst case: %d hits wanted in one 8192-sample block at 174 bpm, %u played (cap %d)\n", wanted, t1 - t0, gb::BeatEngine::kMaxHits); CHECK(wanted > 1024 && t1 - t0 == (uint32_t) wanted && gb::BeatEngine::kMaxHits >= 4096, "hit cap");
    }
    {   // 9. the output stage: a clean 50 Hz sub stays clean; the v1 stage is still there as SOFT
        BeatsXProcessor p; p.applyKit(0, true); for (auto& pt : p.model.patterns) pt.clear(); auto& tr = p.model.tracks[0]; tr.sample = makeSound("sub", 72000, [](int i) { return 0.89f * (float) std::sin(juce::MathConstants<double>::twoPi * 50.0 * i / 48000.0); }); tr.sound = "sub"; tr.vol = 1.0f;
        p.model.patterns[0].length = 16; p.model.patterns[0].steps[0][0].vel = 127; p.publish(); setParam(p, "glue", 0); setParam(p, "songmode", 0); setParam(p, "bpm", 60);
        auto thd = [&](int mode, float vol) { setParam(p, "limiter", (float) mode); setParam(p, "volume", vol); auto out = runProc(p, 48000, 512, 1.2); auto sp = spectrum(out.getReadPointer(0), 48000 * 3 / 10, 48000);
            const float h1 = sp.peakIn(40, 60); double hh = 0; for (int h = 2; h <= 9; ++h) { const float x = sp.peakIn(50.0 * h - 8, 50.0 * h + 8); hh += x * x; } return std::make_pair(100 * std::sqrt(hh) / h1, out.getMagnitude(0, 0, out.getNumSamples())); };
        const auto clean = thd(0, 0.8f), soft = thd(1, 0.8f), off = thd(2, 0.8f), hot = thd(0, 1.0f);
        std::printf("  50 Hz sub: CLEAN THD %.3f%% (peak %.2f) | SOFT (v1) %.2f%% | OFF %.3f%% | CLEAN at VOLUME 1.0 (limiting): THD %.2f%%, peak %.3f (ceiling 0.891)\n", clean.first, clean.second, soft.first, off.first, hot.first, hot.second);
        CHECK(clean.first < 0.1 && off.first < 0.05 && soft.first > 1.0 && hot.second <= 0.892f, "clean limiter / linear volume / v1 soft stage");
        setParam(p, "limiter", 0); setParam(p, "volume", 0.8f);
    }
    {   // real-time rule: no heap allocation on the audio thread (busy pattern: machine gun + 3 scripts + macros + FRACTURE + stutter + tape + multi-out)
        BeatsXProcessor p; p.applyKit(kitIndexOf("Drum & Bass"), true); setParam(p, "songmode", 0); p.model.toolSeed = 41; p.generatePattern(1);
        p.model.scripts = { { "a", gb::script::examples()[0].code, true }, { "b", gb::script::examples()[3].code, true }, { "c", gb::script::examples()[5].code, true } }; p.publish();
        setParam(p, "fracture", 0.3f); setParam(p, "chaos", 0.5f); setParam(p, "tension", 0.5f); setParam(p, "echo", 0.3f); setParam(p, "verb", 0.3f); setParam(p, "room", 0.2f); setParam(p, "clip", 0.3f);
        int waited = 0; while (p.variantsMissing() && waited < 20000) { juce::MessageManager::getInstance()->runDispatchLoopUntil(20); waited += 20; }   // (the worker renders the destruction variants off-thread first)
        p.enableAllBuses(); p.prepareToPlay(48000, 512); p.engine.internalPlay = true; juce::AudioBuffer<float> b(p.getTotalNumOutputChannels(), 512); juce::MidiBuffer m;
        for (int i = 0; i < 50; ++i) { b.clear(); p.processBlock(b, m); }
        p.stutter.store(3); p.engine.pushPad(2, 0.9f); gRtAllocs = 0; gRtCount = true;
        for (int i = 0; i < 300; ++i) { b.clear(); if (i == 100) p.tape.store(true); if (i == 200) { p.tape.store(false); p.stutter.store(0); } p.processBlock(b, m); }
        gRtCount = false; p.engine.internalPlay = false;
        std::printf("  audio-thread heap allocations in 300 busy blocks: %ld\n", gRtAllocs.load()); CHECK(gRtAllocs.load() == 0, "no allocation in processBlock");
    }
    {   // 5. + 13. no message-thread stalls: superseded renders, audioKey, kit switch
        BeatsXProcessor p; p.applyKit(kitIndexOf("Drum & Bass"), true); setParam(p, "songmode", 0); p.model.toolSeed = 41; p.generatePattern(1); p.evolveBars = 16;
        BeatsXProcessor::AudioJob j; j.stems = true; p.startAudioRender(j); juce::Thread::sleep(150); p.model.patterns[0].steps[5][2].vel = 99; p.publish();
        auto t0 = juce::Time::getMillisecondCounterHiRes(); p.startAudioRender(j); const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        auto files = p.waitAudioRender(j);
        for (int t = 0; t < gb::kTracks; ++t) { p.model.tracks[(size_t) t].sample = makeSound("long", 48000 * 12, [t](int i) { return 0.5f * (float) std::sin(i * 0.01 * (t + 1)); }); p.model.tracks[(size_t) t].sound = "long"; }
        t0 = juce::Time::getMillisecondCounterHiRes(); p.publish(); const double pub = juce::Time::getMillisecondCounterHiRes() - t0;
        t0 = juce::Time::getMillisecondCounterHiRes(); auto key = p.audioKey({ false, false, -1 }); const double km = juce::Time::getMillisecondCounterHiRes() - t0;
        t0 = juce::Time::getMillisecondCounterHiRes(); p.applyKit(kitIndexOf("Cinematic"), true); const double kit = juce::Time::getMillisecondCounterHiRes() - t0;
        std::printf("  second startAudioRender while one runs: %.1f ms (%d files in the end) | publish() with 16 x 12 s samples %.1f ms | audioKey %.2f ms | applyKit %.1f ms\n", ms, files.size(), pub, km, kit);
        CHECK(ms < 50 && !files.isEmpty() && pub < 100 && km < 20 && kit < 150, "no stalls");
        juce::MemoryBlock mb; p.getStateInformation(mb); std::printf("  project state with 16 shared-pointer samples: %.1f MB\n", mb.getSize() / 1e6);
    }
    section("FINAL GATE: for the bar");
    {   // 14. multi-out
        BeatsXProcessor p; p.applyKit(4, true); setParam(p, "songmode", 0); p.enableAllBuses(); const int chans = p.getTotalNumOutputChannels();
        p.prepareToPlay(48000, 512); p.engine.internalPlay = true; juce::AudioBuffer<float> b(chans, 512); juce::MidiBuffer m; float mainPk = 0, t1 = 0, t2 = 0, others = 0;
        for (int i = 0; i < 20; ++i) { b.clear(); p.processBlock(b, m); mainPk = std::max(mainPk, b.getMagnitude(0, 0, 512)); t1 = std::max(t1, b.getMagnitude(2, 0, 512)); t2 = std::max(t2, b.getMagnitude(4, 0, 512)); for (int c = 6; c < chans; ++c) others = std::max(others, b.getMagnitude(c, 0, 512)); }
        p.engine.internalPlay = false;
        std::printf("  %d output channels: main %.2f, Track 1 bus (kick) %.2f, Track 2 bus (empty) %.2f, others up to %.2f\n", chans, mainPk, t1, t2, others); CHECK(chans == 34 && mainPk > 0.1f && t1 > 0.1f && t2 == 0.0f, "multi-out");
    }
    {   // 15. per-track automation; 20. velocity law
        BeatsXProcessor p; p.applyKit(4, true); setParam(p, "songmode", 0); setParam(p, "glue", 0); setParam(p, "limiter", 2); for (int t = 1; t < gb::kTracks; ++t) p.model.tracks[(size_t) t].mute = true; p.publish();
        auto pk = [&] { auto out = runProc(p, 48000, 512, 0.3); return out.getMagnitude(0, 0, out.getNumSamples()); };
        const float a = pk(); setParam(p, "t1vol", -6.0f); const float b = pk(); setParam(p, "t1vol", 0.0f);
        std::printf("  T1 Volume -6 dB: kick peak %.3f -> %.3f (%.1f dB)\n", a, b, 20 * std::log10(b / a)); CHECK(std::abs(20 * std::log10(b / a) + 6) < 0.3f, "per-track automation");
        std::printf("  velocity law: vel 1 %.1f dB, 32 %.1f dB, 64 %.1f dB, 100 %.1f dB (v1 law at 1: %.1f dB)\n", 20 * std::log10(gb::velGain(1 / 127.0f, 1, 4)), 20 * std::log10(gb::velGain(32 / 127.0f, 1, 4)), 20 * std::log10(gb::velGain(64 / 127.0f, 1, 4)), 20 * std::log10(gb::velGain(100 / 127.0f, 1, 4)), 20 * std::log10(gb::velGain(1 / 127.0f, 1, 3)));
        CHECK(gb::velGain(1 / 127.0f, 1, 4) < 0.02f && gb::velGain(1.0f, 1, 4) == 1.0f && std::abs(gb::velGain(0.5f, 1, 3) - 0.625f) < 1e-6f, "velocity law");
    }
    {   // 16. stereo samples
        auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("ghostybeats-test"); dir.createDirectory(); auto f = dir.getChildFile("stereo.wav"); f.deleteFile();
        { juce::WavAudioFormat w; auto os = std::unique_ptr<juce::OutputStream>(f.createOutputStream()); std::unique_ptr<juce::AudioFormatWriter> wr(w.createWriterFor(os.get(), 48000, 2, 24, {}, 0)); os.release();
          juce::AudioBuffer<float> b(2, 24000); for (int i = 0; i < 24000; ++i) { b.setSample(0, i, 0.5f * (float) std::sin(i * 0.05)); b.setSample(1, i, 0.5f * (float) std::sin(i * 0.11)); } wr->writeFromAudioSampleBuffer(b, 0, 24000); }
        BeatsXProcessor p; p.applyKit(0, true); for (auto& pt : p.model.patterns) pt.clear(); juce::String err; p.loadSampleToTrack(2, f, err); p.model.patterns[0].steps[2][0].vel = 127; p.model.tracks[2].pan = 0; p.publish(); setParam(p, "songmode", 0); setParam(p, "limiter", 2);
        auto out = runProc(p, 48000, 512, 0.3); const double c = corr(out.getReadPointer(0), out.getReadPointer(1), 12000);
        juce::MemoryBlock mb; p.getStateInformation(mb); BeatsXProcessor q; q.restoreState(mb.getData(), (int) mb.getSize());
        std::printf("  stereo WAV: loaded stereo %d, L/R correlation while playing %.2f, stereo after save/load %d\n", (int) (p.model.tracks[2].sample && p.model.tracks[2].sample->stereo()), c, (int) (q.model.tracks[2].sample && q.model.tracks[2].sample->stereo()));
        CHECK(p.model.tracks[2].sample && p.model.tracks[2].sample->stereo() && std::abs(c) < 0.3 && q.model.tracks[2].sample && q.model.tracks[2].sample->stereo(), "stereo samples");
    }
    {   // 17. 808 glide + envelopes
        BeatsXProcessor p; p.applyKit(0, true); for (auto& pt : p.model.patterns) pt.clear(); auto& tr = p.model.tracks[0]; tr.sample = makeSound("sine100", 96000, [](int i) { return 0.5f * (float) std::sin(juce::MathConstants<double>::twoPi * 100.0 * i / 48000.0); }); tr.sound = "s"; tr.vol = 1; tr.decay = 1;
        auto& P0 = p.model.patterns[0]; P0.length = 16; P0.steps[0][0].vel = 127; P0.steps[0][4].vel = 127; P0.steps[0][4].pitch = 12; setParam(p, "songmode", 0); setParam(p, "bpm", 120); setParam(p, "limiter", 2);
        tr.glide = 0; p.publish(); auto a = runProc(p, 48000, 512, 1.2); tr.glide = 300; p.publish(); auto b = runProc(p, 48000, 512, 1.2);
        const double f0 = zcFreq(a.getReadPointer(0), 14400, 9600, 48000), fRe = zcFreq(a.getReadPointer(0), 26400, 4800, 48000), fMid = zcFreq(b.getReadPointer(0), 26400, 4800, 48000), fEnd = zcFreq(b.getReadPointer(0), 48000, 4800, 48000);
        std::printf("  808 line 100 Hz -> +12 st at beat 1: retrigger %.0f -> %.0f Hz | GLIDE 300 ms: %.0f Hz mid-slide, %.0f Hz after\n", f0, fRe, fMid, fEnd);
        CHECK(std::abs(f0 - 100) < 8 && std::abs(fRe - 200) < 10 && fMid > 112 && fMid < 185 && std::abs(fEnd - 200) < 10, "glide / legato");
        tr.glide = 0; tr.attack = 100; p.publish(); auto c = runProc(p, 48000, 512, 0.3); const float early = c.getMagnitude(0, 0, 960), late = c.getMagnitude(0, 6000, 2000);
        tr.attack = 0; tr.pitchEnv = 12; tr.pitchEnvTime = 100; p.publish(); auto d = runProc(p, 48000, 512, 0.5); const double fs = zcFreq(d.getReadPointer(0), 200, 1200, 48000), fl = zcFreq(d.getReadPointer(0), 19200, 4800, 48000);
        std::printf("  AMP ATTACK 100 ms: level in the first 20 ms %.3f vs later %.3f | PITCH ENV +12 st / 100 ms: %.0f Hz at the start -> %.0f Hz after 400 ms\n", early, late, fs, fl);
        CHECK(early < late * 0.35f && fs > 150 && std::abs(fl - 100) < 6, "attack + pitch envelope");
    }
    {   // 18. per-step pan + filter, the IDM kit
        BeatsXProcessor p; p.applyKit(0, true); for (auto& pt : p.model.patterns) pt.clear(); auto& tr = p.model.tracks[0]; tr.sample = makeSound("nz", 9600, [](int i) { return 0.5f * (float) std::sin(i * 1.7) * (float) std::sin(i * 0.37); }); tr.sound = "nz"; tr.vol = 1; tr.pan = 0;
        auto& st = p.model.patterns[0].steps[0][0]; st.vel = 127; st.pan = 50; setParam(p, "songmode", 0); setParam(p, "limiter", 2); p.publish();
        auto a = runProc(p, 48000, 512, 0.25); const float l = a.getMagnitude(0, 0, 9600), r = a.getMagnitude(1, 0, 9600);
        st.pan = 0; st.filt = 8; p.publish(); auto b = runProc(p, 48000, 512, 0.25); st.filt = 0; p.publish(); auto c = runProc(p, 48000, 512, 0.25);
        auto hf = [](const juce::AudioBuffer<float>& x) { double e = 0; const float* d = x.getReadPointer(0); for (int i = 1; i < 9600; ++i) e += (d[i] - d[i - 1]) * (d[i] - d[i - 1]); return e; };
        std::printf("  step PAN +50: L %.3f R %.3f | step FILTER LP 8: high-frequency energy %.1f%% of unfiltered\n", l, r, 100 * hf(b) / hf(c));
        CHECK(r > l * 4 && hf(b) < hf(c) * 0.2, "per-step pan + filter");
        int idm = -1; for (int i = 0; i < (int) gb::allKits().size(); ++i) if (juce::String(gb::allKits()[(size_t) i].name) == "IDM Glitch") idm = i;
        int glitch = 0; for (auto& s : p.lib->sounds()) if (s->category == "Glitch") ++glitch;
        std::printf("  IDM Glitch kit index %d, %d GLITCH sounds in the library\n", idm, glitch); CHECK(idm >= 0 && glitch >= 16, "IDM kit");
    }
    {   // 19. 808 MIDI note lengths
        BeatsXProcessor p; p.applyKit(0, true); setParam(p, "songmode", 0); for (auto& pt : p.model.patterns) pt.clear(); auto& P0 = p.model.patterns[0]; P0.length = 16; P0.steps[0][0].vel = 100; P0.steps[0][7].vel = 100; P0.steps[0][7].pitch = 3; P0.steps[0][11].vel = 100; p.publish();
        auto f = p.exportMidi(false); juce::MidiFile mf; juce::FileInputStream in(f); mf.readFrom(in); const auto* t = mf.getTrack(0); std::vector<double> lens; bool sig = false;
        for (int i = 0; i < t->getNumEvents(); ++i) { auto* e = t->getEventPointer(i); if (e->message.isTimeSignatureMetaEvent()) sig = true; if (e->message.isNoteOn() && e->noteOffObject) lens.push_back((e->noteOffObject->message.getTimeStamp() - e->message.getTimeStamp()) / 960.0); }
        std::printf("  808 notes at steps 1, 8, 12: lengths %.2f, %.2f, %.2f beats (time signature meta %d)\n", lens.size() > 0 ? lens[0] : 0, lens.size() > 1 ? lens[1] : 0, lens.size() > 2 ? lens[2] : 0, (int) sig);
        CHECK(lens.size() == 3 && std::abs(lens[0] - 1.74) < 0.03 && std::abs(lens[1] - 0.99) < 0.03 && lens[2] > 1.0 && sig, "808 MIDI lengths"); f.deleteFile();
    }
    {   // compact project state: distinct samples as 16-bit FLAC; v1 / v2 "sample" base64 still loads
        BeatsXProcessor p; p.applyKit(0, true); juce::Random R(3); const auto& all = p.lib->sounds();
        for (int t = 0; t < gb::kTracks; ++t) {   // 12 s of real drum material per track: library hits every 100 ms at random levels (what a dropped loop looks like)
            auto snd = std::make_shared<gb::Sound>(); snd->name = "long" + juce::String(t); snd->category = "Sample"; snd->rate = 48000; snd->data.assign(48000 * 12, 0.0f);
            for (int at = 0; at < 48000 * 12 - 4800; at += 4800) { const auto& src = all[(size_t) R.nextInt((int) all.size())]; if (src->category == "Break") continue; const float g = 0.3f + 0.6f * R.nextFloat(); for (size_t k = 0; k < src->data.size() && at + (int) k < 48000 * 12; ++k) snd->data[(size_t) at + k] += src->data[k] * g; }
            for (auto& v : snd->data) v = juce::jlimit(-0.95f, 0.95f, v);
            p.model.tracks[(size_t) t].sample = snd; p.model.tracks[(size_t) t].sound = snd->name; }
        p.publish(); juce::MemoryBlock mb; auto t0 = juce::Time::getMillisecondCounterHiRes(); p.getStateInformation(mb); const double saveMs = juce::Time::getMillisecondCounterHiRes() - t0;
        BeatsXProcessor q; t0 = juce::Time::getMillisecondCounterHiRes(); q.restoreState(mb.getData(), (int) mb.getSize()); const double loadMs = juce::Time::getMillisecondCounterHiRes() - t0;
        double md = 0; bool sizes = true; for (int t = 0; t < gb::kTracks; ++t) { const auto& a = p.model.tracks[(size_t) t].sample; const auto& b = q.model.tracks[(size_t) t].sample; if (!b || b->data.size() != a->data.size()) { sizes = false; continue; } for (size_t i = 0; i < a->data.size(); ++i) md = std::max(md, (double) std::abs(a->data[i] - b->data[i])); }
        juce::MemoryBlock mb2; q.getStateInformation(mb2); BeatsXProcessor q2; q2.restoreState(mb2.getData(), (int) mb2.getSize()); juce::MemoryBlock mb3; q2.getStateInformation(mb3);
        std::printf("  16 distinct 12 s drum-loop samples: state %.1f MB (raw 16-bit would be %.1f MB, v1 base64 %.1f MB), save %.0f ms, load %.0f ms, max diff vs the floats %.2e (16-bit lsb 3.1e-5), second round trip byte-identical %d\n",
                    mb.getSize() / 1e6, 16 * 12 * 48000 * 2 / 1e6, 16 * 12 * 48000 * 2 * 4 / 3 / 1e6, saveMs, loadMs, md, (int) (mb2 == mb3));
        CHECK(sizes && md < 1.0 / 32768.0 + 1e-6 && mb2 == mb3 && mb.getSize() < 16 * 12 * 48000 * 2 && loadMs < 3000, "FLAC sample state");
        // a v2 state (raw 16-bit base64 "sample" + "sampleR") still loads
        juce::ValueTree st("GHOSTYBEATS2"); juce::ValueTree m("BEATS2"); m.setProperty("ver", 3, nullptr); juce::ValueTree tr("TRACK"); tr.setProperty("i", 2, nullptr); tr.setProperty("sound", "old", nullptr);
        auto os = makeSound("old", 9600, [](int i) { return 0.4f * (float) std::sin(i * 0.03); }); tr.setProperty("srate", 48000.0, nullptr); tr.setProperty("sample", gb::soundToBase64(*os), nullptr); m.appendChild(tr, nullptr); st.appendChild(m, nullptr);
        juce::MemoryBlock ob; if (auto xml = st.createXml()) juce::AudioProcessor::copyXmlToBinary(*xml, ob); BeatsXProcessor o; o.restoreState(ob.getData(), (int) ob.getSize());
        CHECK(o.model.tracks[2].sample && o.model.tracks[2].sample->data.size() == 9600, "v1 / v2 raw-16-bit samples still load");
    }
    {   // CHARACTER (Shared/VintageCharacter.h): off = untouched; 12-bit sampler darkens a hat; amount 0 = off; automation offset; saved
        BeatsXProcessor p; p.applyKit(4, true); setParam(p, "songmode", 0); setParam(p, "glue", 0); setParam(p, "limiter", 2); for (int t = 0; t < gb::kTracks; ++t) p.model.tracks[(size_t) t].mute = t != 4; p.publish();   // hats only
        auto hf = [](const juce::AudioBuffer<float>& x) { double e = 0, a = 0; const float* d = x.getReadPointer(0); for (int i = 1; i < x.getNumSamples(); ++i) { e += (d[i] - d[i - 1]) * (d[i] - d[i - 1]); a += d[i] * d[i]; } return e / (a + 1e-12); };
        auto off = runProc(p, 48000, 512, 0.5); p.model.tracks[4].character = 2; p.model.tracks[4].charAmount = 0; p.publish(); auto zero = runProc(p, 48000, 512, 0.5);
        double dz = 0; for (int i = 0; i < off.getNumSamples(); ++i) dz = std::max(dz, (double) std::abs(off.getSample(0, i) - zero.getSample(0, i)));
        p.model.tracks[4].charAmount = 1; p.publish(); auto full = runProc(p, 48000, 512, 0.5); p.model.tracks[4].character = 1; p.publish(); auto bit8 = runProc(p, 48000, 512, 0.5);
        p.model.tracks[4].character = 2; p.model.tracks[4].charAmount = 0.0f; p.publish(); setParam(p, "t5char", 1.0f); auto autom = runProc(p, 48000, 512, 0.5); setParam(p, "t5char", 0.0f);
        double da = 0; for (int i = 0; i < full.getNumSamples(); ++i) da = std::max(da, (double) std::abs(full.getSample(0, i) - autom.getSample(0, i)));
        juce::MemoryBlock mb; p.getStateInformation(mb); BeatsXProcessor q; q.restoreState(mb.getData(), (int) mb.getSize());
        std::printf("  hats: brightness off %.3f, 12-bit sampler %.3f, 8-bit home computer %.3f; amount 0 vs off max diff %.1e; T5 Character +1 automation == amount 1: max diff %.1e; saved mode %d\n", hf(off), hf(full), hf(bit8), dz, da, q.model.tracks[4].character);
        CHECK(dz == 0.0 && hf(full) < hf(off) * 0.8 && hf(bit8) < hf(off) * 0.5 && da < 1e-6 && q.model.tracks[4].character == 2, "CHARACTER per track");
    }
    {   // 28. library trims; 26./27. fades
        auto lib = gb::loadBeatsLibrary(48000.0, false); std::map<juce::String, std::pair<float, float>> rng; for (auto& s : lib->sounds()) { if (s->category == "Break") continue; const float l = gb::loudest200ms(*s) + juce::Decibels::gainToDecibels(s->trim); auto& r = rng[gb::folderOf(s->category)]; if (r.first == 0 && r.second == 0) r = { l, l }; r.first = std::min(r.first, l); r.second = std::max(r.second, l); }
        float worst = 0; for (auto& kv : rng) worst = std::max(worst, kv.second.second - kv.second.first); int dc = 0; for (auto& s : lib->sounds()) { double m = 0; for (auto v : s->data) m += v; if (std::abs(m / (double) s->data.size()) > 0.01) ++dc; }
        std::printf("  with trims the loudest-200 ms spread inside a folder is at most %.1f dB; %d sounds with |DC| > 0.01\n", worst, dc); CHECK(worst < 13.0f && dc == 0, "library trims + DC");
    }
}
static int kitIndexOf(const char* n) { for (int i = 0; i < (int) gb::allKits().size(); ++i) if (juce::String(gb::allKits()[(size_t) i].name) == n) return i; return 0; }

// ---------------------------------------------------------------- LINEAR DRUMMING + MELODIC TRACKER wiring (the engines themselves are tested in LinearMelodyTests)
static void testLinearMelody() {
    section("LINEAR DRUMMING wiring");
    using Evs = std::vector<std::tuple<long, int, int>>;   // (tick, track, vel), time-ordered
    auto events = [](const gb::Snapshot& S, const gb::EngineParams& P, double beats) { Evs ev; gb::linear::collect(S, P, P.bpm, 0.0, beats, S.linear, [&](int t, double b, float v, const gb::EvInfo&) { ev.push_back({ std::lround(b * 3840), t, (int) std::lround(v * 127) }); }); std::sort(ev.begin(), ev.end()); return ev; };
    auto plain = [](const gb::Snapshot& S, const gb::EngineParams& P, double beats) { Evs ev; gb::collectEvents(S, P, P.bpm, 0.0, beats, [&](int t, double b, float v, const gb::EvInfo&) { ev.push_back({ std::lround(b * 3840), t, (int) std::lround(v * 127) }); }); std::sort(ev.begin(), ev.end()); return ev; };
    BeatsXProcessor p; p.applyKit(kitIndexOf("Drum & Bass"), true); setParam(p, "songmode", 0); p.model.toolSeed = 41; p.generatePattern(1); p.evolveBars = 16;
    auto spec = p.makeSpec({ false, false, -1 }); const auto off = events(*spec.snap, spec.P, 64), raw = plain(*spec.snap, spec.P, 64);   // 16 bars
    CHECK(off == raw && !p.model.linear.on, "LINEAR off = collectEvents exactly (%d events)", (int) off.size());
    p.pushUndo(); p.model.linear.on = true; p.model.linear.rule = gb::linear::RuleAccents; p.model.linear.strictness = 0.5f; p.model.linear.move = true; p.publish();
    auto spec2 = p.makeSpec({ false, false, -1 }); const auto on = events(*spec2.snap, spec2.P, 64);
    int collisions = 0; const long win = (long) std::lround(gb::linear::windowBeats(p.model.linear) * 3840);
    for (size_t i = 1; i < on.size(); ++i) if (std::get<1>(on[i]) != std::get<1>(on[i - 1]) && std::get<0>(on[i]) - std::get<0>(on[i - 1]) < win) ++collisions;
    const auto strip = p.linearView(0.0);
    std::printf("  MACHINE GUN beat, 16 bars: %d hits as written -> LINEAR (accents rule, strictness 50%%, MOVE) %d hits, %d collisions inside the window; bar 1 strip kept %d moved %d dropped %d\n", (int) raw.size(), (int) on.size(), collisions, strip.st.kept, strip.st.moved, strip.st.dropped);
    CHECK(collisions == 0 && on.size() >= 100 && strip.st.kept + strip.st.moved > 0, "LINEAR on: one voice per instant");
    // export == playback through the same path; variation changes it; toggling back is bit-exact; undo restores; state keeps the settings + LOCK lane
    auto f = p.exportMidi(false); juce::MidiFile mf; juce::FileInputStream in(f); mf.readFrom(in); int midiNotes = 0; for (auto* e : *mf.getTrack(0)) if (e->message.isNoteOn()) ++midiNotes; f.deleteFile();
    p.model.linear.variation = 2; p.model.linear.lock = 1u << 2; p.publish(); auto spec3 = p.makeSpec({ false, false, -1 }); const auto var2 = events(*spec3.snap, spec3.P, 64);
    juce::MemoryBlock mb; p.getStateInformation(mb); BeatsXProcessor q; q.restoreState(mb.getData(), (int) mb.getSize());
    p.model.linear.on = false; p.publish(); auto spec4 = p.makeSpec({ false, false, -1 }); const auto back = events(*spec4.snap, spec4.P, 64);
    p.undo(); std::printf("  DRAG MIDI carries the linear result (%d notes = %d hits); VARIATION 2 differs %s; off again == original %s; saved: on %d rule %d var %d lock %d; UNDO -> on %d\n", midiNotes, (int) on.size(), var2 != on ? "yes" : "NO", back == raw ? "yes" : "NO", (int) q.model.linear.on, (int) q.model.linear.rule, q.model.linear.variation, (int) q.model.linear.lock, (int) p.model.linear.on);
    CHECK(midiNotes == (int) on.size() && var2 != on && back == raw && q.model.linear.on && q.model.linear.rule == gb::linear::RuleAccents && q.model.linear.variation == 2 && q.model.linear.lock == 4 && !p.model.linear.on, "LINEAR export / variation / toggle / state / undo");

    section("MELODIC TRACKER wiring");
    BeatsXProcessor m; m.applyKit(0, true); setParam(m, "songmode", 0); setParam(m, "limiter", 2); setParam(m, "glue", 0); setParam(m, "bpm", 120); for (auto& pt : m.model.patterns) pt.clear();
    m.model.tracks[3].sample = makeSound("sine220", 48000 * 2, [](int i) { return 0.5f * (float) std::sin(juce::MathConstants<double>::twoPi * 220.0 * i / 48000.0); }); m.model.tracks[3].sound = "sine220"; m.model.tracks[3].vol = 1; m.publish();
    auto silent = runProc(m, 48000, 512, 1.5);   // no melody yet (and no drum steps): nothing plays
    m.pushUndo(); m.melody.on = true; m.melody.lpb = 4; m.melody.speed = 6; m.melody.inst[0].src = 3; m.melody.inst[0].root = 57; m.melody.inst[0].vol = 1.0f;   // A-3 = 220 Hz unpitched
    auto& P0 = m.melody.pat(0); P0.rows = 16; P0.cells[0][0] = { 58, 1, gb::mel::kNoVol, 0, 0 }; P0.cells[0][8] = { 70, 1, gb::mel::kNoVol, 0, 0 }; m.publish();   // A-3 at row 0, A-4 at row 8 (beat 2)
    auto out = runProc(m, 48000, 512, 1.5); const double f1 = zcFreq(out.getReadPointer(0), 9600, 9600, 48000), f2 = zcFreq(out.getReadPointer(0), 50000, 9600, 48000);
    const int bars = m.exportBars(false); auto mid = m.exportMelodyMidi(false); juce::MidiFile mm; juce::FileInputStream min(mid); mm.readFrom(min); int mn = 0; for (int t = 0; t < mm.getNumTracks(); ++t) for (auto* e : *mm.getTrack(t)) if (e->message.isNoteOn()) ++mn; mid.deleteFile();
    juce::MemoryBlock mb2; m.getStateInformation(mb2); BeatsXProcessor q2; q2.restoreState(mb2.getData(), (int) mb2.getSize());
    const bool kept = q2.melody.on && q2.melody.inst[0].route == 3 && q2.melody.inst[0].hasSound() && q2.melody.inst[0].src < 0 && q2.melody.pat(0).cells[0][8].note == 70 && q2.melody.pat(0).rows == 16;
    m.undo(); const bool undone = !m.melody.on;
    std::printf("  tracker: A-3 then A-4 on a 220 Hz sample: %.0f Hz -> %.0f Hz (silent before: peak %.4f); export covers %d bar(s); DRAG MELODY MIDI %d notes; saved + reloaded %s; UNDO turns it off %s\n", f1, f2, silent.getMagnitude(0, 0, silent.getNumSamples()), bars, mn, kept ? "yes" : "NO", undone ? "yes" : "NO");
    CHECK(silent.getMagnitude(0, 0, silent.getNumSamples()) == 0.0f && std::abs(f1 - 220) < 6 && std::abs(f2 - 440) < 10 && bars >= 1 && mn == 2 && kept && undone, "melody wiring");
    section("BYPASS (Shared/Bypass.h)");
    {
        BeatsXProcessor b; b.applyKit(kitIndexOf("Drum & Bass"), true); setParam(b, "songmode", 0); setParam(b, "echo", 0.4f); setParam(b, "verb", 0.4f); setParam(b, "room", 0.3f); b.enableAllBuses();
        CHECK(b.getBypassParameter() != nullptr && b.getBypassParameter()->getName(64) == "Bypass" && b.getBypassParameter()->isAutomatable(), "bypass parameter");
        const int chans = b.getTotalNumOutputChannels(); b.prepareToPlay(48000, 512); b.engine.internalPlay = true; juce::AudioBuffer<float> buf(chans, 512); juce::MidiBuffer m;
        // off: the processBlock path is bit-identical to the engine alone (same snapshot, same params)
        auto spec = b.makeSpec({ false, false, -1 }); gb::BeatEngine ref; ref.prepare(48000, 512); ref.setSnapshot(spec.snap.get()); ref.internalPlay = true; juce::AudioBuffer<float> rb(2, 512); double md = 0;
        for (int i = 0; i < 40; ++i) { buf.clear(); b.processBlock(buf, m); rb.clear(); ref.process(rb, m, spec.P, spec.P.bpm, 0, false); for (int c = 0; c < 2; ++c) for (int k = 0; k < 512; ++k) md = std::max(md, (double) std::abs(buf.getSample(c, k) - rb.getSample(c, k))); }
        CHECK(md == 0.0, "bypass off: bit-identical (max diff %.1e)", md);
        // engage while loud + wet: < -120 dBFS on main and every Track bus within 20 ms, no click in the fade
        float peakBefore = 0, maxStepBefore = 0; { float prev = 0; for (int i = 0; i < 4; ++i) { buf.clear(); b.processBlock(buf, m); for (int k = 0; k < 512; ++k) { const float v = buf.getSample(0, k); peakBefore = std::max(peakBefore, std::abs(v)); maxStepBefore = std::max(maxStepBefore, std::abs(v - prev)); prev = v; } } }
        setParam(b, "bypass", 1.0f);
        float maxStepFade = 0, tail = 0, prev = buf.getSample(0, 511); std::vector<float> trace; int silentAt = -1;
        for (int i = 0; i < 100; ++i) { buf.clear(); b.processBlock(buf, m); float pk = 0; for (int c = 0; c < chans; ++c) pk = std::max(pk, buf.getMagnitude(c, 0, 512));
            for (int k = 0; k < 512; ++k) { const float v = buf.getSample(0, k); if (i < 2) maxStepFade = std::max(maxStepFade, std::abs(v - prev)); prev = v; }
            if (pk < 1e-6f && silentAt < 0) silentAt = i; if (i >= 2) tail = std::max(tail, pk); }
        std::printf("  engage: silent on all %d channels after %d block(s) = %.1f ms (limit 20); fade's largest sample step %.4f vs %.4f in the music before (peak %.2f); residual over the next second %.1e\n", chans, silentAt + 1, (silentAt + 1) * 512 / 48.0, maxStepFade, maxStepBefore, peakBefore, tail);
        CHECK(silentAt >= 0 && (silentAt + 1) * 512 <= 960 && maxStepFade <= maxStepBefore * 1.2f + 1e-4f && tail < 1e-6f, "bypass engages cleanly");
        // MIDI in + pads are ignored while bypassed (no trigger, nothing recorded)
        b.recording.store(true); const uint32_t t0 = b.engine.trig[0].load(); m.addEvent(juce::MidiMessage::noteOn(1, 36, (juce::uint8) 100), 0); b.engine.pushPad(1, 0.9f); buf.clear(); b.processBlock(buf, m); m.clear(); b.recording.store(false);
        CHECK(b.engine.trig[0].load() == t0 && b.engine.recFifo.getNumReady() == 0 && buf.getMagnitude(0, 0, 512) == 0.0f, "MIDI / pads ignored while bypassed");
        // release onto an empty pattern: nothing leaks back (echo / reverb / voices were cut on engage); the clock kept running
        for (auto& pt : b.model.patterns) pt.clear(); b.publish(); const int stepBefore = b.engine.uiStep.load(); setParam(b, "bypass", 0.0f);
        float leak = 0; for (int i = 0; i < 60; ++i) { buf.clear(); b.processBlock(buf, m); for (int c = 0; c < chans; ++c) leak = std::max(leak, buf.getMagnitude(c, 0, 512)); }
        std::printf("  release onto silence: leak %.1e; the clock ran while bypassed (step %d)\n", leak, stepBefore); CHECK(leak < 1e-6f && stepBefore >= 0, "no tails leak after release");
        // never touched by kits / undo of musical edits; saved with the session; old sessions load with it off
        setParam(b, "bypass", 1.0f); b.pushUndo(); b.applyKit(0, true); b.undo(); const bool still = b.getBypassParameter()->getValue() > 0.5f;
        juce::MemoryBlock mb; b.getStateInformation(mb); BeatsXProcessor q; q.restoreState(mb.getData(), (int) mb.getSize());
        juce::ValueTree st("GHOSTYBEATS2"); juce::ValueTree bt("BEATS2"); bt.setProperty("ver", 3, nullptr); st.appendChild(bt, nullptr); juce::MemoryBlock ob; if (auto xml = st.createXml()) juce::AudioProcessor::copyXmlToBinary(*xml, ob);
        BeatsXProcessor o; setParam(o, "bypass", 1.0f); o.restoreState(ob.getData(), (int) ob.getSize());   // (an instance that was bypassed opens an old session: off)
        // a saved beat (LOAD BEAT) never changes BYPASS either way
        const auto beatFile = juce::File::createTempFile(".ghostbeat"); beatFile.replaceWithData(mb.getData(), mb.getSize());
        BeatsXProcessor lb; lb.loadBeat(beatFile); const bool beatKeepsOff = lb.getBypassParameter()->getValue() < 0.5f; beatFile.deleteFile();
        std::printf("  kit change + undo keep it on: %d; saved state reloads on: %d; an old session loads off (even into a bypassed instance): %d; LOAD BEAT of a bypassed save leaves it off: %d\n", (int) still, (int) (q.getBypassParameter()->getValue() > 0.5f), (int) (o.getBypassParameter()->getValue() < 0.5f), (int) beatKeepsOff);
        CHECK(still && q.getBypassParameter()->getValue() > 0.5f && o.getBypassParameter()->getValue() < 0.5f && beatKeepsOff, "bypass state rules");
        // the parameter is the last one (existing automation keeps its index)
        CHECK(b.getParameters().getLast() == b.getBypassParameter(), "bypass is the last parameter");
    }
    // an old project (no LINEAR / MELODY children) opens with both off
    juce::ValueTree st("GHOSTYBEATS2"); juce::ValueTree bt("BEATS2"); bt.setProperty("ver", 3, nullptr); st.appendChild(bt, nullptr); juce::MemoryBlock ob; if (auto xml = st.createXml()) juce::AudioProcessor::copyXmlToBinary(*xml, ob);
    BeatsXProcessor o; o.restoreState(ob.getData(), (int) ob.getSize()); CHECK(!o.model.linear.on && !o.melody.on && o.model.linear == gb::linear::Settings(), "old projects: LINEAR off, MELODY empty");
}

// ---------------------------------------------------------------- demos
static void setParam(BeatsXProcessor& p, const char* id, float v);
static void toMp3(const juce::String& wav, const juce::File& mp3) { mp3.deleteFile(); juce::ChildProcess cp; cp.start(juce::StringArray { "ffmpeg", "-loglevel", "error", "-y", "-i", wav, "-codec:a", "libmp3lame", "-b:a", "192k", mp3.getFullPathName() }); cp.waitForProcessToFinish(60000); }
static void renderDemo(BeatsXProcessor& p, const juce::File& dir, const juce::String& name, bool song) {
    juce::StringArray f; const auto vols = [&] { std::array<float, gb::kTracks> v {}; for (int t = 0; t < gb::kTracks; ++t) v[(size_t) t] = p.model.tracks[(size_t) t].vol; return v; }();
    for (int it = 0; it < 5; ++it) {   // gain staging: bring the mix to -14 LUFS with the track faders (the limiter ceiling keeps peaks <= -1 dBFS)
        f = p.renderNow({ song, false, -1 }); if (f.isEmpty()) { std::printf("  demo %s: no file\n", name.toRawUTF8()); return; }
        const double l = lufs(readWav(f[0])); if (std::abs(l + 14) < 0.3) break;
        const float g = juce::Decibels::decibelsToGain((float) (-14 - l) * 0.8f); for (auto& tr : p.model.tracks) tr.vol = juce::jlimit(0.0f, 2.0f, tr.vol * g); p.publish();
    }
    { auto w = readWav(f[0]); std::printf("  %s: %.1f LUFS, peak %.2f dBFS\n", name.toRawUTF8(), lufs(w), juce::Decibels::gainToDecibels(peakOf(w))); }
    toMp3(f[0], dir.getChildFile(name + ".mp3")); std::printf("  %s (%.1f s)\n", dir.getChildFile(name + ".mp3").getFullPathName().toRawUTF8(), readWav(f[0]).getNumSamples() / 48000.0);
    // the reference set for the owner's ears: same beat at -18 LUFS with the limiter OFF (nothing between the drums and the DAW)
    { const float lim = p.apvts.getRawParameterValue("limiter")->load(); setParam(p, "limiter", 2);
      for (int it = 0; it < 4; ++it) { f = p.renderNow({ song, false, -1 }); const double l = lufs(readWav(f[0])); if (std::abs(l + 18) < 0.3) break; const float g = juce::Decibels::decibelsToGain((float) (-18 - l)); for (auto& tr : p.model.tracks) tr.vol = juce::jlimit(0.0f, 2.0f, tr.vol * g); p.publish(); }
      auto w = readWav(f[0]); std::printf("  %s-ref18: %.1f LUFS, peak %.2f dBFS (limiter off)\n", name.toRawUTF8(), lufs(w), juce::Decibels::gainToDecibels(peakOf(w))); toMp3(f[0], dir.getChildFile("ref18").getChildFile(name + "-ref18.mp3"));
      setParam(p, "limiter", lim); for (int t = 0; t < gb::kTracks; ++t) p.model.tracks[(size_t) t].vol = vols[(size_t) t]; p.publish(); }
}
static void setParam(BeatsXProcessor& p, const char* id, float v) { if (auto* q = p.apvts.getParameter(id)) q->setValueNotifyingHost(q->convertTo0to1(v)); }
static void demos(const juce::File& dir) {
    dir.createDirectory(); dir.getChildFile("ref18").createDirectory(); BeatsXProcessor p; section("demos"); setParam(p, "ceiling", -1.3f);   // a little margin for the MP3 encoder
    auto kitIndex = [](const char* n) { for (int i = 0; i < (int) gb::allKits().size(); ++i) if (juce::String(gb::allKits()[(size_t) i].name) == n) return i; return 0; };
    // 1. progressive house, four on the floor, 126
    p.applyKit(kitIndex("Club Progressive"), true); setParam(p, "glue", 0.35f); setParam(p, "room", 0.15f); setParam(p, "clip", 0.2f);
    p.model.song = { { 0, 0, 2 }, { 1, 2, 2 }, { 0, 4, 2 }, { 1, 6, 2 } }; p.model.patterns[1].steps[12][0].vel = 0; renderDemo(p, dir, "beats2-progressive-house-126", true);
    // 2. dubstep half-time 140
    p.applyKit(kitIndex("Dubstep Heavy"), true); setParam(p, "glue", 0.4f); setParam(p, "clip", 0.35f); setParam(p, "room", 0.1f);
    renderDemo(p, dir, "beats2-dubstep-halftime-140", true);
    // 3. trap 145 with an 808 line
    p.applyKit(kitIndex("Trap Hard"), true); setParam(p, "glue", 0.3f); setParam(p, "clip", 0.15f);
    { const int8_t mel[] { 0, 0, 0, 3, 0, 0, 0, 5, 0, 0, 7, 0, 0, 0, 3, 0 }; for (int pi = 0; pi < 2; ++pi) for (int s = 0; s < 16; ++s) p.model.patterns[(size_t) pi].steps[0][(size_t) s].pitch = mel[s]; p.publish(); }
    renderDemo(p, dir, "beats2-trap-145", true);
    // 4. MACHINE GUN and 5. INTRICATE (generated + evolving, 16 bars)
    setParam(p, "songmode", 0); p.evolveBars = 16;
    p.applyKit(kitIndex("Drum & Bass"), true); p.model.toolSeed = 41; p.generatePattern(1); setParam(p, "fracture", 0.15f); setParam(p, "glue", 0.35f); setParam(p, "clip", 0.25f);
    renderDemo(p, dir, "beats2-machine-gun-174", false); setParam(p, "fracture", 0.0f);
    p.applyKit(kitIndex("Lo-fi Tape"), true); setParam(p, "bpm", 92); p.model.toolSeed = 12; p.generatePattern(0); setParam(p, "swing", 0.25f); setParam(p, "room", 0.2f);
    renderDemo(p, dir, "beats2-intricate-92", false); setParam(p, "swing", 0.0f);
    // 6. drop a break -> DNA keep 80% -> evolve 16 bars
    p.applyKit(kitIndex("Drum & Bass"), true); for (auto& pt : p.model.patterns) pt.clear();
    p.setTrackSound(15, "Break Rolling"); p.model.tracks[15].vol = 0.9f; p.model.patterns[0].steps[1][0].vel = 120; p.model.patterns[0].steps[1][10].vel = 100; p.publish();
    p.model.toolSeed = 80; p.keepIdentity(0.8f); auto& bp = p.model.patterns[0]; bp.evolve = 0.45f; bp.evolveStyle = 0; bp.fillEvery = 4; p.publish();
    setParam(p, "glue", 0.35f); setParam(p, "clip", 0.2f);
    renderDemo(p, dir, "beats2-break-keep80-evolve16-174", false);
    // 7. IDM: tracker-level control. Scripts (turnaround rolls, glitch tail, freeze accents), per-hit destruction on the hats,
    //    spectral freeze on every 4th snare, band-reversed percussion, bars 1-2 of each 16 phrase-locked, 16 bars at 160.
    p.applyKit(kitIndex("Drum & Bass"), true); setParam(p, "bpm", 160); for (auto& pt : p.model.patterns) pt.clear();
    { auto& P0 = p.model.patterns[0]; P0.length = 16;   // 0 kick, 2 snare, 4 hat, 7 rim, 8 tom, 14 zap
      for (int s : { 0, 6, 10 }) P0.steps[0][(size_t) s].vel = 118; for (int s : { 4, 12 }) P0.steps[2][(size_t) s].vel = 120; P0.steps[2][12].destroy = 15;
      for (int s = 0; s < 16; s += 2) { P0.steps[4][(size_t) s].vel = (uint8_t) (s % 4 == 0 ? 96 : 70); P0.steps[4][(size_t) s].destroy = 25; }
      for (int s : { 3, 7, 11, 15 }) { P0.steps[7][(size_t) s].vel = 84; P0.steps[7][(size_t) s].frev = (uint8_t) (s == 15 ? 5 : 1); }
      P0.steps[8][14].vel = 100; P0.steps[8][14].roll = 6; P0.steps[8][14].rate = 32; P0.steps[8][14].pitchRamp = -12; P0.steps[8][14].velRamp = -50;
      P0.steps[14][7].vel = 70; P0.steps[14][7].pitch = 3; P0.steps[14][11].vel = 70; P0.steps[14][11].pitch = -2;
      P0.evolve = 0.35f; P0.evolveStyle = 1; P0.fillEvery = 4; P0.seed = 21;
      p.model.locks[0] = { 0, 2, 0xffff };
      p.model.scripts = { { "Turnaround rolls", gb::script::examples()[0].code, true }, { "Glitch tail", gb::script::examples()[3].code, true }, { "Freeze accents", gb::script::examples()[5].code, true } };
      p.publish(); }
    setParam(p, "glue", 0.3f); setParam(p, "clip", 0.2f); setParam(p, "fracture", 0.1f); setParam(p, "tension", 0.2f);
    renderDemo(p, dir, "beats2-idm-scripts-freeze-160", false); setParam(p, "fracture", 0.0f); setParam(p, "tension", 0.0f); p.model.scripts.clear(); p.model.locks = {};
    // 8. LINEAR DRUMMING on a generated MACHINE GUN beat: one voice at a time, accents + downbeats win, the rest move (16 bars)
    p.applyKit(kitIndex("Garage"), true); setParam(p, "bpm", 134); p.model.toolSeed = 23; p.generatePattern(1); p.model.linear.on = true; p.model.linear.rule = gb::linear::RuleAccents; p.model.linear.strictness = 0.6f; p.model.linear.move = true; p.model.linear.lock = 1u; p.publish();
    setParam(p, "glue", 0.3f); setParam(p, "clip", 0.15f); renderDemo(p, dir, "beats2-linear-generated-134", false); p.model.linear = {}; p.publish();
    // 9. MELODIC TRACKER: bass line (808), arpeggio (tom) and a lead with vibrato / glide (cowbell) played by the drum tracks' own sounds, drums under it, 8 bars
    p.applyKit(0, true); setParam(p, "bpm", 124); setParam(p, "songmode", 1); p.model.song = { { 0, 0, 4 }, { 1, 4, 4 } };
    { auto& s = p.melody; s = gb::mel::Song(); s.on = true; s.lpb = 4; s.speed = 6;
      s.inst[0].sound = "808 Long"; s.inst[0].name = "808 Bass"; s.inst[0].route = 0; s.inst[0].root = 33; s.inst[0].vol = 0.9f; s.inst[0].release = 0.08f;                   // the 808 as the bass
      s.inst[1].sound = "Metal Bell"; s.inst[1].name = "Bell Pluck"; s.inst[1].route = 8; s.inst[1].root = 81; s.inst[1].vol = 0.5f; s.inst[1].pan = -0.3f; s.inst[1].release = 0.12f;   // a modal bell (880 Hz = A-5) as the arp
      s.inst[2].sound = "Metal Chime"; s.inst[2].name = "Chime Lead"; s.inst[2].route = 11; s.inst[2].root = 93; s.inst[2].vol = 0.4f; s.inst[2].pan = 0.3f; s.inst[2].release = 0.2f; s.inst[2].attack = 0.004f;   // chime lead
      for (int pi = 0; pi < 2; ++pi) { auto& P0 = s.pat(pi); P0.rows = 64; const int bass[] { 37, 37, 40, 42, 37, 37, 44, 42, 33, 33, 40, 42, 45, 44, 42, 40 };
        for (int i = 0; i < 16; ++i) { P0.cells[0][(size_t) (i * 4)] = { (uint8_t) (bass[i] + (pi ? 3 : 0)), 1, gb::mel::kNoVol, 0, 0 }; P0.cells[0][(size_t) (i * 4 + 3)] = { gb::mel::kNoteOff, 0, gb::mel::kNoVol, 0, 0 }; }
        for (int r = 0; r < 64; r += 2) P0.cells[1][(size_t) r] = { (uint8_t) (61 + (r / 16) % 4 * 2 + (pi ? 3 : 0)), 2, (uint8_t) (0x2c - (r % 8) * 3), gb::mel::FxArp, (uint8_t) (r % 32 < 16 ? 0x37 : 0x47) };
        const int lead[] { 73, 0, 76, 78, 0, 80, 0, 78, 76, 0, 73, 0, 71, 0, 73, 0 }; for (int i = 0; i < 16; ++i) if (lead[i]) P0.cells[2][(size_t) (i * 4)] = { (uint8_t) (lead[i] + (pi ? 3 : 0)), 3, gb::mel::kNoVol, (uint8_t) (i % 4 == 0 ? gb::mel::FxVibrato : gb::mel::FxGlide), (uint8_t) (i % 4 == 0 ? 0x43 : 0x0c) }; }
      p.publish(); }
    setParam(p, "glue", 0.25f); setParam(p, "clip", 0.1f); setParam(p, "verb", 0.15f); renderDemo(p, dir, "beats2-melody-tracker-124", true); p.melody = gb::mel::Song(); setParam(p, "verb", 0.0f); setParam(p, "songmode", 0); p.publish();
}

// ---------------------------------------------------------------- screenshots
static void shots(const juce::File& dir);

#include "DemoGateTests.inc"
#include "LookShots.inc"
#if JUCE_LINUX
#include <dlfcn.h>
// (Xlib, which JUCE dlopens) a headless Xvfb without a window manager answers some window-type properties with BadAtom; Xlib's default handler would exit the process
static void ignoreXErrors() { if (void* x11 = dlopen("libX11.so.6", RTLD_NOW | RTLD_GLOBAL)) { typedef int (*H)(void*, void*); typedef H (*SetH)(H); if (auto set = (SetH) dlsym(x11, "XSetErrorHandler")) set([](void*, void*) -> int { return 0; }); } }
#endif

// a big MY BANKS library (owner's report 2026-10-30: 5,176 sounds made FL slow to open GB and crash on save): listing must be quick and decode nothing
static long rssKb() { juce::StringArray l; l.addLines(juce::File("/proc/self/status").loadFileAsString()); for (auto& x : l) if (x.startsWith("VmRSS:")) return x.fromFirstOccurrenceOf(":", false, false).trim().getLargeIntValue(); return 0; }
static int bigBanksTest(int count) {
    namespace ub = ghosty::userbanks;
    auto root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("gb-bigbanks"); root.deleteRecursively(); root.getChildFile("sounds").createDirectory(); ub::rootOverride() = root;
    const char* types[] = { "kick", "snare", "clap", "hat", "ohat", "perc", "bass", "fx" };
    for (int f = 0; f < 40; ++f) {   // 40 real files (4 s stereo each), listed many times
        juce::AudioBuffer<float> b(2, 48000 * 4); for (int i = 0; i < b.getNumSamples(); ++i) { const float v = std::sin(i * 0.01f * (f + 1)) * std::exp(-i / 20000.0f); b.setSample(0, i, v); b.setSample(1, i, v * 0.7f); }
        auto out = root.getChildFile("sounds/f" + juce::String(f) + ".wav"); juce::WavAudioFormat w; std::unique_ptr<juce::AudioFormatWriter> wr(w.createWriterFor(out.createOutputStream().release(), 48000, 2, 24, {}, 0)); wr->writeFromAudioSampleBuffer(b, 0, b.getNumSamples()); }
    auto L = ub::fresh(); auto* snd = L["snd"].getDynamicObject(); juce::Array<juce::var> bank;
    for (int u = 0; u < count; ++u) { auto* o = new juce::DynamicObject(); o->setProperty("n", juce::String(types[u % 8]) + " " + juce::String(u) + (u % 5 == 0 ? " C#min" : "")); o->setProperty("c", types[u % 8]); o->setProperty("f", "sounds/f" + juce::String(u % 40) + ".wav"); snd->setProperty(juce::String(u), juce::var(o)); bank.add(u); }
    auto* bo = new juce::DynamicObject(); bo->setProperty("id", "my4"); bo->setProperty("name", "BIG TEST BANK"); bo->setProperty("s", bank); L["banks"].getArray()->add(juce::var(bo)); ub::save(L);
    BeatsXProcessor p; p.prepareToPlay(48000, 512);
    const long r0 = rssKb(); auto t0 = juce::Time::getMillisecondCounterHiRes();
    const int changed = p.rescanBanks(true); p.loadBuiltinBanks();
    const double ms = juce::Time::getMillisecondCounterHiRes() - t0; const long r1 = rssKb();
    std::printf("  %d MY BANKS sounds + the built-in banks listed in %.0f ms (changed %d), memory +%ld MB\n", count, ms, changed, (r1 - r0) / 1024);
    bool ok = ms < 8000 && (r1 - r0) < 300 * 1024;
    t0 = juce::Time::getMillisecondCounterHiRes(); const int n = p.pickFromBanks({ "my4" }, false); const double pm = juce::Time::getMillisecondCounterHiRes() - t0;
    int played = 0; for (int t = 0; t < gb::kTracks; ++t) { const auto& tr = p.model.tracks[(size_t) t]; if (p.isMine(tr.sound) && tr.sample && ! tr.sample->data.empty()) ++played; }
    std::printf("  GENERATE from it: %d tracks changed in %.0f ms, %d tracks play a decoded MY BANKS sound, memory now +%ld MB\n", n, pm, played, (rssKb() - r0) / 1024);
    ok = ok && n > 0 && played == n;
    juce::MemoryBlock st; t0 = juce::Time::getMillisecondCounterHiRes(); p.getStateInformation(st); const double sm = juce::Time::getMillisecondCounterHiRes() - t0;
    BeatsXProcessor q; q.prepareToPlay(48000, 512); q.restoreState(st.getData(), (int) st.getSize());
    int same = 0; for (int t = 0; t < gb::kTracks; ++t) if (q.model.tracks[(size_t) t].sound == p.model.tracks[(size_t) t].sound) ++same;
    std::printf("  project state %.1f MB saved in %.0f ms; reopened: %d / %d tracks the same\n", st.getSize() / 1048576.0, sm, same, gb::kTracks);
    ok = ok && same == gb::kTracks && st.getSize() < (size_t) 64 << 20;
    for (int k = 0; k < 2000; ++k) p.setTrackSound(k % gb::kTracks, p.model.tracks[(size_t) (k % gb::kTracks)].sound);   // 2000 edits while audio never runs
    std::printf("  2000 edits with the audio paused: memory +%ld MB\n", (rssKb() - r0) / 1024);
    ok = ok && (rssKb() - r0) < 600 * 1024;
    // a re-typed sound: a new entry, the old one untouched (other threads may hold it)
    auto L2 = ub::load(); L2["snd"].getDynamicObject()->getProperty("1").getDynamicObject()->setProperty("c", "perc"); ub::save(L2);
    juce::Thread::sleep(1100); ub::save(L2); p.rescanBanks(true);
    std::printf("  re-typed \"snare 1\" -> %s\n", p.categoryOf("snare 1").toRawUTF8()); ok = ok && p.categoryOf("snare 1") == "Perc";
    ub::rootOverride() = juce::File(); root.deleteRecursively();
    std::printf("  BIG BANKS %s\n", ok ? "OK" : "FAILED"); return ok ? 0 : 1;
}

// ---- 2026-11-07: opens at the fitting size (about 1200 x 760, at most 1240 x 780) with nothing off the edge, and a fresh start every time
static juce::String beatsStateOf(juce::AudioProcessor& p) { juce::MemoryBlock mb; p.getStateInformation(mb); return mb.toBase64Encoding(); }
static void testFresh() {
    section("size: opens at about 1200 x 760 (at most 1240 x 780), resizable, nothing off the edge or overlapping");
    { BeatsXProcessor p; p.prepareToPlay(48000, 512);
      std::unique_ptr<juce::AudioProcessorEditor> e(p.createEditor()); auto* be = dynamic_cast<BeatsXEditor*>(e.get()); CHECK(be != nullptr, "editor"); if (!be) return;
      for (int i = 0; i < 3; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
      CHECK(e->getWidth() <= 1240 && e->getHeight() <= 780 && e->getWidth() >= 1100 && e->getHeight() >= 720, "opens at %d x %d", e->getWidth(), e->getHeight());
      CHECK(e->isResizable(), "resizable");
      if (const char* d = std::getenv("GHOSTY_SHOTS_DIR")) if (*d) { juce::File dir(juce::String::fromUTF8(d)); dir.createDirectory(); const auto img = e->createComponentSnapshot(e->getLocalBounds(), true, 1.0f);
          juce::FileOutputStream os(dir.getChildFile("beats-open-size.png")); if (os.openedOk()) { os.setPosition(0); os.truncate(); juce::PNGImageFormat().writeImageToStream(img, os); } }
      auto* content = be->contentForTest();
      for (auto sz : { juce::Point<int>(bx::kOpenW, bx::kOpenH), juce::Point<int>(1240, 780), juce::Point<int>(1000, 700), juce::Point<int>(885, 593), juce::Point<int>(1440, 860), juce::Point<int>(1500, 760), juce::Point<int>(1100, 900), juce::Point<int>(1600, 1000) }) {
          e->setSize(sz.x, sz.y); juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
          const auto r = content->getBoundsInParent().toFloat(); const auto win = e->getLocalBounds().toFloat().expanded(1.0f);
          CHECK(win.contains(r), "at %d x %d the whole window content is inside the window (content %.0f,%.0f %.0fx%.0f)", sz.x, sz.y, r.getX(), r.getY(), r.getWidth(), r.getHeight());
          CHECK(r.reduced(1.0f).contains(e->getLocalBounds().toFloat().reduced(2.0f)), "at %d x %d the content fills the whole window: no black bars (content %.0f,%.0f %.0fx%.0f)", sz.x, sz.y, r.getX(), r.getY(), r.getWidth(), r.getHeight());
          // the buttons on the first page (grid) do not overlap each other
          be->showPageForTest(0); juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
          std::vector<juce::Component*> btns; std::function<void(juce::Component&)> walk = [&](juce::Component& c) { for (auto* ch : c.getChildren()) { if (!ch->isVisible()) continue; if (dynamic_cast<juce::TextButton*>(ch) && ch->getParentComponent() == content) btns.push_back(ch); walk(*ch); } };
          walk(*content); int clash = 0; juce::String which;
          for (size_t i = 0; i < btns.size(); ++i) for (size_t j = i + 1; j < btns.size(); ++j) if (btns[i]->getBounds().intersects(btns[j]->getBounds())) { ++clash; which << "\"" << static_cast<juce::Button*>(btns[i])->getButtonText() << "\"/\"" << static_cast<juce::Button*>(btns[j])->getButtonText() << "\" "; }
          std::printf("  %d x %d: content %.0f x %.0f, %d buttons on the first page, %d overlapping\n", sz.x, sz.y, r.getWidth(), r.getHeight(), (int) btns.size(), clash);
          CHECK(clash == 0 && btns.size() > 20, "at %d x %d no buttons overlap (%d of %d): %s", sz.x, sz.y, clash, (int) btns.size(), which.toRawUTF8()); }
      e->setSize(1600, 1000); e.reset();
      std::unique_ptr<juce::AudioProcessorEditor> e2(p.createEditor()); juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
      CHECK(e2->getWidth() == bx::kOpenW && e2->getHeight() == bx::kOpenH, "reopened at %d x %d (the fitting size, not the last size)", e2->getWidth(), e2->getHeight()); }

    section("fresh start: a new Ghosty Beats is always brand new (nothing carries over from another instance or project)");
    juce::String factory; { BeatsXProcessor f; f.prepareToPlay(48000, 512); factory = beatsStateOf(f); }
    { BeatsXProcessor a, b; a.prepareToPlay(48000, 512); b.prepareToPlay(48000, 512); const auto bStart = beatsStateOf(b);
      a.applyKit(7, true); setParam(a, "volume", 0.3f); a.normaliseSamples = false; a.midiChannel = 10; a.focusMode = true; a.saveSettings();   // (these used to be read back by every new Ghosty Beats)
      a.model.patterns[0].steps[1][3].vel = 99; a.publish();
      CHECK(beatsStateOf(a) != factory, "the first has another kit, a new step and new settings");
      CHECK(bStart == factory && beatsStateOf(b) == factory && b.currentKit == 0, "the second, open at the same time, is still at factory defaults");
      BeatsXProcessor c; c.prepareToPlay(48000, 512);
      CHECK(beatsStateOf(c) == factory && c.normaliseSamples && c.midiChannel == 1 && !c.focusMode && c.currentKit == 0, "a third made after the change starts at factory defaults (NORMALISE on, MIDI channel 1, no FOCUS, kit 1)");
      // a project keeps its own NORMALISE / channel
      juce::MemoryBlock mb; a.getStateInformation(mb); BeatsXProcessor d; d.setStateInformation(mb.getData(), (int) mb.getSize());
      CHECK(!d.normaliseSamples && d.midiChannel == 10, "a project reopened keeps its own NORMALISE and MIDI channel"); }
    { BeatsXProcessor g; g.prepareToPlay(44100, 256); CHECK(beatsStateOf(g) == factory && !g.focusMode, "destroyed and made again: factory defaults");
      std::unique_ptr<juce::AudioProcessorEditor> e(g.createEditor()); juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
      CHECK(e->getWidth() == bx::kOpenW && e->getHeight() == bx::kOpenH, "its window opens at the fitting size"); }
    { BeatsXProcessor z; z.normaliseSamples = true; z.midiChannel = 1; z.focusMode = false; z.saveSettings(); }   // (leave the settings file as a new user has it)
}


// ---- 2026-11-07: TEMPO ½x | 1x | 2x (Shared/TempoMul.h)
static void testTempoMul() {
    section("TEMPO 1/2x | 1x | 2x: locked to the DAW's bars, MIDI lands as heard at the DAW's tempo, audio rendered as heard, saved with the project");
    const int B = 512; const double sr = 48000;
    auto run = [&](int mul, double hostBpm, double startPpq, int blocks) {   // the processor on a fake DAW transport
        BeatsXProcessor p; FakeHead ph; ph.bpm = hostBpm; ph.ppq = startPpq; p.applyKit(kitIndexOf("Drum & Bass"), true); setParam(p, "songmode", 0); setParam(p, "echo", 0.3f);
        p.tempoMul.store(mul); p.setPlayHead(&ph); p.prepareToPlay(sr, B);
        std::vector<float> out; juce::AudioBuffer<float> buf(2, B); juce::MidiBuffer m;
        for (int i = 0; i < blocks; ++i) { buf.clear(); p.processBlock(buf, m); for (int k = 0; k < B; ++k) out.push_back(buf.getSample(0, k)); ph.ppq += B * hostBpm / 60.0 / sr; }
        p.setPlayHead(nullptr); return out; };
    auto same = [](const std::vector<float>& a, const std::vector<float>& b) { if (a.size() != b.size()) return false; double md = 0; for (size_t i = 0; i < a.size(); ++i) md = std::max(md, (double) std::abs(a[i] - b[i])); return md == 0.0; };
    // 1) sync: 2x on a DAW at 60 = 1x at 120 (the same beat, the same bars), 1/2x at 240 = 1x at 120, from bar 1 and from mid-song
    for (double start : { 0.0, 13.0 }) {
        const auto ref = run(1, 120, start * 2.0, 300);
        CHECK(same(run(2, 60, start, 300), ref), "2x at 60 bpm from DAW beat %.0f plays exactly what 1x at 120 bpm plays (locked to the DAW's bars)", start);
        const auto ref2 = run(1, 120, start * 0.5, 300);
        CHECK(same(run(0, 240, start, 300), ref2), "1/2x at 240 bpm from DAW beat %.0f plays exactly what 1x at 120 bpm plays", start);
    }
    CHECK(same(run(1, 133, 7.25, 120), run(1, 133, 7.25, 120)), "1x is the DAW's tempo as before");
    // 2) MIDI: the notes land where they are heard, in the DAW's beats; the file says the DAW's tempo
    auto notesOf = [](const juce::File& f, double& tempo) { std::vector<double> t; juce::MidiFile mf; juce::FileInputStream in(f); mf.readFrom(in); tempo = 0;
        for (int k = 0; k < mf.getNumTracks(); ++k) for (auto* e : *mf.getTrack(k)) { if (e->message.isNoteOn()) t.push_back(e->message.getTimeStamp() / 960.0); if (e->message.isTempoMetaEvent()) tempo = 60.0 / e->message.getTempoSecondsPerQuarterNote(); }
        std::sort(t.begin(), t.end()); return t; };
    {
        BeatsXProcessor p; p.applyKit(kitIndexOf("Drum & Bass"), true); setParam(p, "songmode", 0); p.hostBpm.store(130.0);
        double t1 = 0, t2 = 0, th = 0;
        p.tempoMul.store(1); const auto f1 = p.exportMidi(false); const auto n1 = notesOf(f1, t1);
        p.tempoMul.store(2); const auto f2 = p.exportMidi(false); const auto n2 = notesOf(f2, t2);
        p.tempoMul.store(0); const auto fh = p.exportMidi(false); const auto nh = notesOf(fh, th);
        bool ok2 = n1.size() == n2.size() && !n1.empty(), okh = n1.size() == nh.size();
        for (size_t i = 0; ok2 && i < n1.size(); ++i) ok2 = std::abs(n2[i] - n1[i] * 0.5) < 4.0 / 960.0;   // (within 4 ticks: the 1x file is rounded to whole ticks itself)
        for (size_t i = 0; okh && i < n1.size(); ++i) okh = std::abs(nh[i] - n1[i] * 2.0) < 4.0 / 960.0;
        { double m2 = 0, mh = 0; for (size_t i = 0; i < std::min(n1.size(), std::min(n2.size(), nh.size())); ++i) { m2 = std::max(m2, std::abs(n2[i] - n1[i] * 0.5)); mh = std::max(mh, std::abs(nh[i] - n1[i] * 2.0)); } std::printf("  MIDI timing error vs 1x: %.5f beats (2x) / %.5f (1/2x); notes %zu / %zu / %zu\n", m2, mh, n1.size(), n2.size(), nh.size()); }
        std::printf("  MIDI at 130 bpm: %zu notes; last note at beat %.3f (1x) / %.3f (2x) / %.3f (1/2x); file tempo %.1f / %.1f / %.1f\n", n1.size(), n1.empty() ? 0 : n1.back(), n2.empty() ? 0 : n2.back(), nh.empty() ? 0 : nh.back(), t1, t2, th);
        CHECK(ok2 && std::abs(t2 - 130) < 0.01, "2x: every MIDI note at half the DAW beat it had at 1x, the file at the DAW's 130 bpm");
        CHECK(okh && std::abs(th - 130) < 0.01, "1/2x: every MIDI note at twice the DAW beat, the file at 130 bpm");
        CHECK(f2.getFileName().contains("260bpm") && fh.getFileName().contains("65bpm") && !f1.getFileName().contains("bpm"), "MIDI file names say the BPM heard: %s / %s", f2.getFileName().toRawUTF8(), fh.getFileName().toRawUTF8());
        // 3) audio: rendered at the tempo heard (the loop part is half as long at 2x, twice at 1/2x; 1.5 s of tail after it), the name says the BPM
        auto loopSeconds = [&](int mul, juce::String& name) { p.tempoMul.store(mul); auto f = p.bounce(false, 1); name = f.getFileName(); juce::WavAudioFormat w; std::unique_ptr<juce::AudioFormatReader> r(w.createReaderFor(new juce::FileInputStream(f), true));
            return r ? (double) r->lengthInSamples / r->sampleRate - 1.5 : -1.0; };
        juce::String a1, a2, ah; const double l1 = loopSeconds(1, a1), l2 = loopSeconds(2, a2), lh = loopSeconds(0, ah);
        std::printf("  audio, one bar: %.4f s (1x) / %.4f s (2x) / %.4f s (1/2x)   %s | %s | %s\n", l1, l2, lh, a1.toRawUTF8(), a2.toRawUTF8(), ah.toRawUTF8());
        CHECK(std::abs(l1 - 4 * 60.0 / 130) < 0.002 && std::abs(l2 - l1 * 0.5) < 0.002 && std::abs(lh - l1 * 2.0) < 0.002, "audio length follows the tempo heard");
        CHECK(a2.contains("260bpm") && ah.contains("65bpm") && a1.contains("130bpm"), "audio file names say the BPM heard");
        // 4) saved with the project; a new one starts at 1x
        p.tempoMul.store(2); juce::MemoryBlock mb; p.getStateInformation(mb);
        BeatsXProcessor q; CHECK(q.tempoMul.load() == 1, "a new Ghosty Beats starts at 1x"); q.setStateInformation(mb.getData(), (int) mb.getSize());
        CHECK(q.tempoMul.load() == 2, "the project reopens at 2x");
        p.tempoMul.store(1); juce::MemoryBlock m1; p.getStateInformation(m1); BeatsXProcessor r; r.setStateInformation(m1.getData(), (int) m1.getSize()); CHECK(r.tempoMul.load() == 1, "a 1x project reopens at 1x");
    }
    // 5) the switch: in the header, nothing under it, at 1200 x 760 and larger; clicking it sets TEMPO
    {
        BeatsXProcessor p; p.prepareToPlay(48000, 512); p.hostBpm.store(130.0);
        std::unique_ptr<juce::AudioProcessorEditor> e(p.createEditor()); auto* be = dynamic_cast<BeatsXEditor*>(e.get()); if (!be) { CHECK(false, "editor"); return; }
        for (auto sz : { juce::Point<int>(bx::kOpenW, bx::kOpenH), juce::Point<int>(1600, 1000) }) {
            e->setSize(sz.x, sz.y); be->syncUiForTest(); juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
            auto* sw = be->partForTest("tempo"); auto* content = be->contentForTest(); int clash = 0; juce::String which;
            for (auto* c : content->getChildren()) if (c != sw && c->isVisible() && c->getBounds().intersects(sw->getBounds()) && c->getWidth() < 600) { ++clash; which << c->getName() << "@" << c->getBounds().toString() << " "; }
            CHECK(sw->isVisible() && content->getLocalBounds().contains(sw->getBounds()) && clash == 0, "at %d x %d the TEMPO switch is in the header with nothing under it (%s)", sz.x, sz.y, which.toRawUTF8());
            auto* g = be->partForTest("groove"); CHECK(g->isVisible() && content->getLocalBounds().contains(g->getBounds()), "GROOVE is still there");
        }
        auto* sw = dynamic_cast<ghosty::tempomul::Switch*>(be->partForTest("tempo"));
        const auto c2 = sw->segment(2).getCentre(); sw->mouseDown(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), c2, juce::ModifierKeys(), 0, 0, 0, 0, 0, sw, sw, juce::Time::getCurrentTime(), c2, juce::Time::getCurrentTime(), 1, false));
        CHECK(p.tempoMul.load() == 2 && std::abs(p.effectiveBpm() - 260) < 0.01, "clicking 2x sets TEMPO 2x (260 bpm heard at 130)");
        if (const char* d = std::getenv("GHOSTY_SHOTS_DIR")) if (*d) { e->setSize(bx::kOpenW, bx::kOpenH); juce::MessageManager::getInstance()->runDispatchLoopUntil(10); juce::File dir(juce::String::fromUTF8(d)); dir.createDirectory();
            const auto img = e->createComponentSnapshot(e->getLocalBounds(), true, 1.0f); juce::FileOutputStream os(dir.getChildFile("beats-tempo-2x.png")); if (os.openedOk()) { os.setPosition(0); os.truncate(); juce::PNGImageFormat().writeImageToStream(img, os); } }
    }
}


// ---- 2026-11-07: the sound's waveform (bottom right): a click plays the sound; its WAV drag is exactly what you hear (pitch / tune / settings,
// the host's sample rate)
static double acPitch(const float* x, int n, double sr) {   // autocorrelation pitch (Hz) over n samples, 30..1500 Hz, parabolic peak
    const int lo = (int) (sr / 1500.0), hi = juce::jmin(n / 2, (int) (sr / 30.0)); double best = -1e9; int bl = 0; std::vector<double> r((size_t) hi + 2, 0.0);
    double e0 = 0; for (int i = 0; i < n - hi; ++i) e0 += (double) x[i] * x[i]; if (e0 <= 0) return 0;
    for (int l = lo; l <= hi + 1; ++l) { double a = 0; for (int i = 0; i < n - hi; ++i) a += (double) x[i] * x[i + l]; r[(size_t) l] = a / e0; }
    for (int l = lo + 1; l <= hi; ++l) if (r[(size_t) l] > r[(size_t) l - 1] && r[(size_t) l] >= r[(size_t) l + 1] && r[(size_t) l] > best) { best = r[(size_t) l]; bl = l; }
    if (bl == 0) return 0; const double a = r[(size_t) bl - 1], b = r[(size_t) bl], c = r[(size_t) bl + 1]; const double den = a - 2 * b + c; const double d = std::abs(den) < 1e-12 ? 0.0 : 0.5 * (a - c) / den;
    return sr / (bl + d);
}
static int onsetOf(const std::vector<float>& x) { for (size_t i = 0; i < x.size(); ++i) if (std::abs(x[i]) > 0.01f) return (int) i; return -1; }
static void testSoundDrag() {
    section("the sound's waveform: a click plays it, the WAV drag is exactly what you hear (pitch, tune, settings) at 44.1 and 48 kHz");
    for (double sr : { 44100.0, 48000.0 }) {
        BeatsXProcessor p; p.applyKit(kitIndexOf("Trap"), true); setParam(p, "songmode", 0); setParam(p, "echo", 0.0f); setParam(p, "verb", 0.0f);
        const int t = 0;   // (the track the editor shows first: Trap's 808)
        p.model.tracks[(size_t) t].pitch = 3.0f; p.publish();   // tuned up 3 semitones in the kit (the old drag ignored it)
        p.prepareToPlay(sr, 512);
        // what you hear: a click on the waveform, through the plugin's output (DAW stopped)
        std::unique_ptr<juce::AudioProcessorEditor> e(p.createEditor()); auto* be = dynamic_cast<BeatsXEditor*>(e.get()); auto* rg = be ? be->regionForTest() : nullptr;
        if (!rg) { CHECK(false, "region editor"); return; }
        e->setVisible(true); be->showPageForTest(0); be->showSlicesForTest(false); e->setSize(1180, 790); juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        // (select the 808 track like a click on its name would)
        if (auto* c = be->contentForTest()) (void) c;
        p.auditionTrack(t, 0.0f);   // (nothing: velocity 0 is silent; make sure the queue is empty)
        { juce::AudioBuffer<float> b(2, 512); juce::MidiBuffer m; for (int i = 0; i < 4; ++i) p.processBlock(b, m); }
        const auto mid = rg->waveArea().getCentre();
        { auto* top = e->getComponentAt(e->getLocalPoint(rg, mid.toInt())); juce::String who = top ? top->getName() + " " + juce::String(typeid(*top).name()) : juce::String("none");
          CHECK(top == rg || (top && rg->isParentOf(top)), "%.0f Hz: a real click on the waveform reaches it (the topmost thing there: %s)", sr, who.toRawUTF8()); }
        rg->mouseDown(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), mid, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0, 0, 0, 0, 0, rg, rg, juce::Time::getCurrentTime(), mid, juce::Time::getCurrentTime(), 1, false));
        rg->mouseUp(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), mid, juce::ModifierKeys(), 0, 0, 0, 0, 0, rg, rg, juce::Time::getCurrentTime(), mid, juce::Time::getCurrentTime(), 1, false));
        std::vector<float> heard; { juce::AudioBuffer<float> b(2, 512); juce::MidiBuffer m; for (int i = 0; i < (int) (sr * 1.5 / 512); ++i) { b.clear(); p.processBlock(b, m); for (int k = 0; k < 512; ++k) heard.push_back(b.getSample(0, k)); } }
        float pk = 0; for (auto v : heard) pk = std::max(pk, std::abs(v));
        CHECK(pk > 0.05f, "%.0f Hz: a click on the waveform plays the sound (peak %.2f)", sr, pk);
        {   // a click on the START flag (top strip) or the strip under the wave plays it too (no drag: nothing moves)
            const auto before = p.model.tracks[(size_t) t].regStart;
            for (auto pt : { juce::Point<float>(12.0f, 6.0f), juce::Point<float>(rg->waveArea().getCentreX() - 40.0f, (float) rg->getHeight() - 5.0f) }) {
                rg->mouseDown(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pt, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0, 0, 0, 0, 0, rg, rg, juce::Time::getCurrentTime(), pt, juce::Time::getCurrentTime(), 1, false));
                rg->mouseUp(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pt, juce::ModifierKeys(), 0, 0, 0, 0, 0, rg, rg, juce::Time::getCurrentTime(), pt, juce::Time::getCurrentTime(), 1, false));
                float q = 0; juce::AudioBuffer<float> b(2, 512); juce::MidiBuffer m; for (int i = 0; i < (int) (sr * 0.4 / 512); ++i) { b.clear(); p.processBlock(b, m); q = std::max(q, b.getMagnitude(0, 0, 512)); }
                CHECK(q > 0.05f, "%.0f Hz: a click at (%.0f, %.0f) on the sound plays it too (peak %.2f)", sr, pt.x, pt.y, q);
                juce::AudioBuffer<float> b2(2, 512); for (int i = 0; i < (int) (sr * 1.2 / 512); ++i) { b2.clear(); p.processBlock(b2, m); }   // let it ring out
            }
            CHECK(p.model.tracks[(size_t) t].regStart == before, "%.0f Hz: those clicks moved nothing", sr);
        }
        // the drag (the editor's own track = the one shown; export that track's whole region, as the WAV grip does)
        auto f = p.exportSoundPart(t, 0.0f, 1.0f, "region");
        juce::WavAudioFormat w; std::unique_ptr<juce::AudioFormatReader> r(w.createReaderFor(new juce::FileInputStream(f), true));
        CHECK(r != nullptr && std::abs(r->sampleRate - sr) < 0.5, "%.0f Hz: the dragged WAV is at the host's rate (%.0f)", sr, r ? r->sampleRate : 0.0);
        if (!r) continue;
        juce::AudioBuffer<float> fb(2, (int) r->lengthInSamples); r->read(&fb, 0, (int) r->lengthInSamples, 0, true, true);
        std::vector<float> dragged(fb.getReadPointer(0), fb.getReadPointer(0) + fb.getNumSamples());
        // the raw sample (what the old drag wrote)
        const auto snd = p.lib->loaded(p.model.tracks[(size_t) t].sound); std::vector<float> raw = snd ? snd->data : std::vector<float>();
        const int h0 = onsetOf(heard), d0 = onsetOf(dragged), r0 = onsetOf(raw); const int N = 8192;
        auto at = [&](const std::vector<float>& x, int o, double rate) { const int s0 = o + (int) (rate * 0.06); return s0 >= 0 && s0 + N < (int) x.size() ? acPitch(x.data() + s0, N, rate) : 0.0; };
        const double fh = at(heard, h0, sr), fd = at(dragged, d0, sr), fr = snd ? at(raw, r0, snd->rate) : 0.0;
        const double cents = fh > 0 && fd > 0 ? 1200.0 * std::log2(fd / fh) : 9999.0;
        std::printf("  %.0f Hz: heard %.1f Hz, dragged %.1f Hz (%+.1f cents), the raw sample %.1f Hz (%s, %.0f Hz file)\n", sr, fh, fd, cents, fr, f.getFileName().toRawUTF8(), r->sampleRate);
        CHECK(std::abs(cents) < 10.0, "%.0f Hz: the dragged sound is at the pitch you hear (%+.1f cents)", sr, cents);
        CHECK(fr > 0 && 1200.0 * std::log2(fd / fr) > 150.0, "%.0f Hz: it carries the kit's tune (+%.0f cents over the raw sample, which the old drag wrote)", sr, fr > 0 ? 1200.0 * std::log2(fd / fr) : 0.0);
    }
}

int main(int argc, char** argv) {
    BeatsXProcessor::useUserDefault = false;   // (the tests never pick up a default beat saved on the Mac)
    std::setvbuf(stdout, nullptr, _IONBF, 0);
#if JUCE_LINUX
    ignoreXErrors();
#endif
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::String mode = argc > 1 ? argv[1] : "test";
    if (mode == "demos") { demos(juce::File(argc > 2 ? argv[2] : "/tmp")); return 0; }
    if (mode == "bigbanks") return bigBanksTest(argc > 2 ? juce::String(argv[2]).getIntValue() : 5176);
    if (mode == "shots") { shots(juce::File(argc > 2 ? argv[2] : "/tmp")); return 0; }
    if (mode == "lookshots") { lookShots(juce::File(argc > 2 ? argv[2] : "/tmp/beats-lookshots"), argc > 3 && juce::String(argv[3]) == "ghost", argc > 3 && juce::String(argv[3]) == "neon", argc > 3 && juce::String(argv[3]) == "two"); return 0; }   // (deterministic: old looks compared across builds)
    if (mode == "sounddrag") {   // ⇣ in the SOUNDS list (owner 2026-11-02)
        int fails = 0; auto ok = [&](bool c, const juce::String& m) { std::printf("  %s %s\n", c ? "ok  " : "FAIL", m.toRawUTF8()); if (! c) ++fails; };
        BeatsXProcessor q; q.applyKit(0, true); ghosty::License::testForce().store(1);
        const auto name = q.model.tracks[0].sound; const auto s = q.lib->loaded(name);
        const auto f = q.exportSound(name); juce::WavAudioFormat wf; std::unique_ptr<juce::AudioFormatReader> r(f.existsAsFile() ? wf.createReaderFor(f.createInputStream().release(), true) : nullptr);
        ok(r != nullptr && f.getFileNameWithoutExtension().startsWith(juce::File::createLegalFileName(name)), "a WAV named after the sound: " + f.getFileName());
        if (r != nullptr && s) { juce::AudioBuffer<float> b((int) r->numChannels, (int) r->lengthInSamples); r->read(&b, 0, (int) r->lengthInSamples, 0, true, true); float err = 0;
            for (int i = 0; i < juce::jmin(b.getNumSamples(), (int) s->data.size()); ++i) err = juce::jmax(err, std::abs(b.getSample(0, i) - s->data[(size_t) i]));
            ok(r->bitsPerSample == 24 && r->sampleRate == s->rate && (int) r->lengthInSamples == (int) s->data.size() && (int) r->numChannels == (s->stereo() ? 2 : 1) && err < 1.0e-6f,
               "24-bit, " + juce::String(r->sampleRate, 0) + " Hz, " + juce::String((int) r->numChannels) + " ch, every sample (" + juce::String((int) r->lengthInSamples) + ")"); }
        const auto f2 = q.exportSound(name); ok(f2.existsAsFile() && f2 != f, "a second drag = a fresh file (" + f2.getFileName() + ")");
        ok(! q.exportSound("no such sound").existsAsFile(), "an unknown sound writes nothing");
        ghosty::License::testForce().store(0); ok(! q.exportSound(name).existsAsFile(), "demo mode: nothing is written"); ghosty::License::testForce().store(1);
        std::unique_ptr<juce::AudioProcessorEditor> e(q.createEditor()); auto* be = dynamic_cast<BeatsXEditor*>(e.get()); be->showPageForTest(1);
        auto* sl = dynamic_cast<bx::SoundList*>(be->partForTest("sounds")); ok(sl != nullptr && ! sl->items.empty(), "the SOUNDS list has sounds");
        if (sl != nullptr && ! sl->items.empty()) { if (sl->getWidth() < 300) sl->setSize(495, 200); juce::StringArray got; sl->performDrag = [&](const juce::StringArray& x) { got = x; };
            auto ev = [&](juce::Point<float> at, float dy) { return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at.translated(0, dy), juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0, 0, 0, 0, 0, sl, sl, juce::Time::getCurrentTime(), at, juce::Time::getCurrentTime(), 1, dy != 0); };
            const auto g0 = sl->grip(0).getCentre().toFloat(); sl->mouseMove(ev(g0, 0)); ok(sl->getTooltip().contains("into your DAW for the WAV"), "hover ⇣: " + sl->getTooltip());
            sl->mouseDown(ev(g0, 0)); sl->mouseDrag(ev(g0, 20)); sl->mouseUp(ev(g0, 20));
            ok(got.size() == 1 && juce::File(got[0]).hasFileExtension(".wav") && juce::File(got[0]).getFileNameWithoutExtension().startsWith(juce::File::createLegalFileName(sl->nameAt(0))), "dragging ⇣ drags " + (got.isEmpty() ? juce::String("nothing") : juce::File(got[0]).getFileName()));
            got.clear(); const auto c0 = sl->cell(0).getCentre().toFloat().withX((float) sl->cell(0).getX() + 20); sl->mouseDown(ev(c0, 0)); sl->mouseDrag(ev(c0, 20)); sl->mouseUp(ev(c0, 20));
            ok(got.isEmpty(), "dragging the name (not ⇣) does not drag a WAV out (it copies onto a bank, as before)");
            ghosty::License::testForce().store(0); got.clear(); sl->mouseDown(ev(g0, 0)); sl->mouseDrag(ev(g0, 20)); sl->mouseUp(ev(g0, 20)); ok(got.isEmpty(), "demo mode: ⇣ drags nothing"); ghosty::License::testForce().store(1); }
        std::printf(fails ? "sounddrag: %d FAILED\n" : "sounddrag: ALL PASSED\n", fails); return fails ? 1 : 0; }
    if (mode == "demogate") return demoGateTest() == 0 ? 0 : 1;   // the demo export gate (docs/DEMO-EXPORT-GATE.md)
    if (mode == "cachegate") return cacheGateTest() == 0 ? 0 : 1;   // the drum cache on disk is scrambled; the sound is bit-exact
    if (mode == "drillrenders") { drillRenders(juce::File(argc > 2 ? argv[2] : "/root/work/wt-beats-drill-renders")); return 0; }   // DRILL_1.wav + DRILL_2.wav: 8 bars, two generations
    if (mode == "reggaetonrenders") { reggaetonRenders(juce::File(argc > 2 ? argv[2] : "/root/work/wt-beats-latin-renders")); return 0; }   // REGGAETON_1.wav + _2.wav: 8 bars
    if (mode == "gritrenders") { gritRenders(juce::File(argc > 2 ? argv[2] : "/root/work/wt-beats-latin-renders")); return 0; }   // DRILL_GRIT_1.wav + _2.wav: 8 bars
    if (mode == "gritdbg") {   // (diagnostic) one DRILL generation: true peak / un-limited peak with each track muted
        BeatsXProcessor q; q.applyKit(kitIndexOf("DRILL"), true); setParam(q, "songmode", 0); q.model.drillGrit = juce::String(argv[2]).getIntValue(); q.model.toolSeed = juce::String(argv[3]).getIntValue(); q.generatePattern(gb::drill::kStyle);
        { const auto& pt = q.model.patterns[(size_t) q.currentPattern()]; for (int t = 0; t < 7; ++t) { std::printf("  t%d %-18s ", t, q.model.tracks[(size_t) t].sound.toRawUTF8()); for (int k = 0; k < pt.length; ++k) { const auto& st = pt.steps[(size_t) t][(size_t) k]; if (!st.vel) std::printf("."); else if (t == 0) std::printf("%c", st.slide() ? '/' : 'X'); else std::printf("%c", st.vel >= 100 ? 'X' : 'x'); }
            if (t == 0) { std::printf("  |"); for (int k = 0; k < pt.length; ++k) { const auto& st = pt.steps[0][(size_t) k]; if (st.vel) std::printf(" %d:%+d h%d v%d", k, st.pitch, st.hold, st.vel); } } std::printf("\n"); } }
        { for (int t = 0; t < gb::kTracks; ++t) q.model.tracks[(size_t) t].mute = t != 0; q.publish(); const auto on = renderBars(q, 8); std::printf("  808 alone: %.1f LUFS, rms %.1f dB\n", lufs(on), rmsDb(on.getReadPointer(0), 0, on.getNumSamples())); }
        for (int m = -1; m < 3; ++m) { for (int t = 0; t < gb::kTracks; ++t) q.model.tracks[(size_t) t].mute = t == m; q.publish(); const auto on = renderBars(q, 8), off = renderBars(q, 8, 2);
            std::printf("  mute %2d: %.1f LUFS, tp %.2f, unlimited peak %.2f dBFS\n", m, lufs(on), juce::Decibels::gainToDecibels(truePeak4x(on)), juce::Decibels::gainToDecibels(peakOf(off))); }
        return 0; }
    if (mode == "beatdbg") {   // (diagnostic) beatdbg KIT STYLE SEED: one generation, LUFS / un-limited peak with each track muted
        BeatsXProcessor q; q.applyKit(kitIndexOf(argv[2]), true); setParam(q, "songmode", 0); const int st = juce::String(argv[3]).getIntValue(), sd = juce::String(argv[4]).getIntValue();
        if (sd > 0) { q.model.toolSeed = sd - 1; q.generatePattern(st); }
        if (const char* pe = std::getenv("PAT")) setParam(q, "pattern", (float) std::atoi(pe));
        const bool solo = std::getenv("SOLO") != nullptr;
        for (int m = -1; m < gb::kTracks; ++m) { for (int t = 0; t < gb::kTracks; ++t) q.model.tracks[(size_t) t].mute = solo ? (m >= 0 && t != m) : t == m; q.publish(); const auto on = renderBars(q, 8), off = renderBars(q, 8, 2);
            if (m >= 0) if (auto sp = q.lib->byName(q.model.tracks[(size_t) m].sound)) std::printf("  [trim %+.1f dB, vol %.2f] ", juce::Decibels::gainToDecibels(sp->trim), q.model.tracks[(size_t) m].vol);
            std::printf("  %s %2d %-16s %.1f LUFS, tp %.2f, unlimited peak %.2f dBFS\n", solo ? "solo" : "mute", m, m >= 0 ? q.model.tracks[(size_t) m].sound.toRawUTF8() : "", lufs(on), juce::Decibels::gainToDecibels(truePeak4x(on)), juce::Decibels::gainToDecibels(peakOf(off)));
            if (m < 0 && std::getenv("PEAKAT")) { int at = 0; float pk = 0; for (int c = 0; c < 2; ++c) for (int i = 0; i < off.getNumSamples(); ++i) if (std::abs(off.getSample(c, i)) > pk) { pk = std::abs(off.getSample(c, i)); at = i; }
                const double step = 48000.0 * 15.0 / q.apvts.getRawParameterValue("bpm")->load(); std::printf("    peak at sample %d = 16th %.2f\n", at, at / step); } }
        return 0; }
    if (mode == "librenders") { const juce::File dir(argc > 2 ? argv[2] : "/root/work/wt-beats-lib-renders"); dir.createDirectory();   // a 2-bar demo of every classic-character kit
        for (auto* k : { "ANALOG BOOM", "ANALOG PUNCH", "DIGITAL EIGHTIES", "CRUNCH TWELVE", "SWING TWELVE", "TRAP GOLD", "TEXAS BOUNCE", "BOOM BAP CLASSIC", "POP SHINE", "TRAP CLASSIC" }) kitDemo(k, dir);
        trapClassicRenders(dir); return 0; }   // + TRAP_CLASSIC_1.wav / _2.wav: 8 bars, two generations
    if (mode == "stylegrid") { styleGrid(argv[2], argc > 3 ? std::atoi(argv[3]) : 3); return 0; }   // (diagnostic) stylegrid KIT [N]: N GENERATE <STYLE> beats + a VARY as x / . grids
    if (mode == "drillhashes") { for (auto h : drillHashes()) std::printf("0x%016llxULL,\n", (unsigned long long) h); return 0; }
    if (mode == "levels") {   // loudness of every kit at default settings (pattern A, 4 bars)
        BeatsXProcessor p;
        for (int k = 0; k < (int) gb::allKits().size(); ++k) { p.applyKit(k, true); auto spec = p.makeSpec({ false, false, -1 }); spec.bars = 4;
            auto b = render(*spec.snap, spec.P, 4 * 4 * 60.0 / spec.P.bpm); double rms = 0; int hot = 0; float pk = 0;
            for (int i = 0; i < b.getNumSamples(); ++i) { const float v = b.getSample(0, i); rms += v * v; if (std::abs(v) > 0.8f) ++hot; pk = std::max(pk, std::abs(v)); }
            std::printf("  %-18s rms %6.1f dB  peak %.2f  limiter zone %5.2f%%\n", gb::allKits()[(size_t) k].name, 10 * std::log10(rms / b.getNumSamples() + 1e-12), pk, 100.0 * hot / b.getNumSamples()); }
        return 0;
    }
    if (mode == "recreatedbg") { a2mb::debugOne(std::atoi(argv[2]), std::atoi(argv[3]), argc > 4 ? std::atoi(argv[4]) : 0); return 0; }   // (diagnostic) KIT PATTERN [VARIANT]
    if (mode == "recreatedump") { a2mb::dump(juce::File(argv[2]), argc > 3 ? std::atoi(argv[3]) : 1); return 0; }   // (diagnostic) corpus events + features -> CSV
    if (mode == "recreatetrain") { a2mb::trainMode(argc > 2 && juce::String(argv[2]) == "split", argc > 3 ? juce::File(argv[3]) : juce::File()); return 0; }   // fit the RE-CREATE drum classifier on the corpus (split = held-out kits scored)
    if (mode == "recreatebench") { a2mb::bench(argc > 2 ? juce::File(argv[2]) : juce::File(), 1, 0); return 0; }   // RE-CREATE DRUMS: per-class F1 over the factory-kit corpus (+ renders into argv[2])
    const juce::String only = argc > 2 ? argv[2] : "";
    auto want = [&](const char* n) { return only.isEmpty() || only.contains(n); };
    if (want("lib")) testLibrary(); if (want("timing")) testTiming();
    { BeatsXProcessor p; if (want("midi")) testMidi(p); if (want("audio")) testAudioExport(p); if (want("undo")) testUndo(p); if (want("rhythm")) testRhythm(p); }
    if (want("samples")) testSamples(); if (want("compat")) testBackCompat();
    { BeatsXProcessor p; if (want("locks")) testLocks(p); if (want("destruct")) testDestruction2(p); if (want("scripts")) testScripts(p); if (want("scriptbeat")) testScriptBeat(); if (want("gridedit")) testGridEdit(); if (want("follow")) testFollow(); if (want("makedefault")) testMakeDefault(); if (want("dawstop")) testDawStop(); if (want("showbeat")) testShowBeat(); if (want("meters")) testLevelsSharedTheme(p); }
    if (want("gate")) testPunchList();
    if (want("linmel")) testLinearMelody();
    if (want("playnotes")) testPlayOnNotes();
    if (want("sync")) testSync();
    if (want("polish")) testPolish();
    if (want("recreate")) testRecreate();
    if (want("themes")) testThemePixels();
    if (want("fresh")) testFresh();
    if (want("tempo")) testTempoMul();
    if (want("sounddrag2")) testSoundDrag();
    if (want("ghostui")) { const char* d = std::getenv("GHOSTY_SHOTS_DIR"); testGhostUi(d && *d ? juce::File(juce::String::fromUTF8(d)) : juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("beats-uishots")); }
    if (want("region")) testRegion();
    if (want("tracker")) testTrackerExtras();
    if (want("gate2")) testGate2();
    if (want("drillsounds")) testDrillSounds();
    if (want("drillstyle")) testDrillStyle();
    if (want("drillgrit")) testDrillGrit();
    if (want("reggaeton")) testReggaeton();
    if (want("era")) testEraBridged();
    if (want("analog")) testAnalogKits();
    if (want("sampled")) testSampledEraKits();
    if (want("modern")) testModernKits();
    if (want("trapclassic")) testTrapClassic();
    if (want("styles")) testStyleEngine();
    if (want("levels")) testLevels();
    std::printf("\n%d checks passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}

// screenshots of the editor pages (run under Xvfb)

// PLAY: ALWAYS | ON NOTES (ROADMAP 16, FL channel-mute follow)
static void testPlayOnNotes() {
    section("PLAY: ALWAYS | ON NOTES (FL channel-mute follow)");
    const int B = 512; const double sr = 48000, bpm = 120, spbS = sr * 60.0 / bpm;   // 24000 samples per beat
    struct Ev { long at; int note; bool on; int ch = 1; };
    // renders nBlocks from absolute sample 'start' with the host playing (ppq follows), main L into 'L', every channel's peak into 'pk'
    auto run = [&](BeatsXProcessor& p, FakeHead& ph, long& t, int nBlocks, const std::vector<Ev>& evs, std::vector<float>& L, float* pk = nullptr) {
        const int chans = p.getTotalNumOutputChannels(); juce::AudioBuffer<float> buf(chans, B); juce::MidiBuffer m;
        for (int i = 0; i < nBlocks; ++i, t += B) {
            m.clear(); for (auto& e : evs) if (e.at >= t && e.at < t + B) m.addEvent(e.on ? juce::MidiMessage::noteOn(e.ch, e.note, (juce::uint8) 100) : juce::MidiMessage::noteOff(e.ch, e.note), (int) (e.at - t));
            ph.ppq = t / spbS; buf.clear(); p.processBlock(buf, m);
            for (int k = 0; k < B; ++k) L.push_back(buf.getSample(0, k));
            if (pk) for (int c = 0; c < chans; ++c) *pk = std::max(*pk, buf.getMagnitude(c, 0, B));
        }
    };
    auto make = [&](BeatsXProcessor& p, FakeHead& ph, int mode) { p.applyKit(kitIndexOf("Drum & Bass"), true); setParam(p, "songmode", 0); setParam(p, "verb", 0.3f); setParam(p, "echo", 0.3f); p.enableAllBuses();
                                                                 p.setPlayHead(&ph); ph.bpm = bpm; ph.playing = true; p.prepareToPlay(sr, B); p.playMode.store(mode); };
    // 1) ALWAYS: the processBlock path is bit-identical to the engine alone (MIDI notes still play the pads)
    {
        BeatsXProcessor p; FakeHead ph; make(p, ph, 0); auto spec = p.makeSpec({ false, false, -1 }); spec.P.bpm = (float) bpm; gb::BeatEngine ref; ref.prepare(sr, B); ref.setSnapshot(spec.snap.get());
        const int chans = p.getTotalNumOutputChannels(); juce::AudioBuffer<float> buf(chans, B), rb(2, B); juce::MidiBuffer m; double md = 0;
        for (int i = 0; i < 80; ++i) { m.clear(); if (i % 7 == 3) m.addEvent(juce::MidiMessage::noteOn(1, 38, (juce::uint8) 110), 100); if (i % 7 == 5) m.addEvent(juce::MidiMessage::noteOff(1, 38), 10);
            ph.ppq = i * B / spbS; buf.clear(); p.processBlock(buf, m); rb.clear(); ref.process(rb, m, spec.P, bpm, ph.ppq, true);
            for (int c = 0; c < 2; ++c) for (int k = 0; k < B; ++k) md = std::max(md, (double) std::abs(buf.getSample(c, k) - rb.getSample(c, k))); }
        std::printf("  ALWAYS (default): processBlock vs the engine alone over 80 blocks with pad notes: max diff %.1e\n", md);
        CHECK(md == 0.0 && p.playMode.load() == 0, "PLAY ALWAYS is bit-identical");
        p.setPlayHead(nullptr);
    }
    // 2..4) ON NOTES: no note = exact silence; a note at bar 2 starts the beat on that sample; note-off fades (~6 ms) then exact silence on every bus
    {
        BeatsXProcessor p; FakeHead ph; make(p, ph, 1); long t = 0; std::vector<float> L; float pkNo = 0;
        run(p, ph, t, 94, {}, L, &pkNo);                                                    // ~1 s, host playing, no notes
        const long S0 = 4 * (long) spbS, S1 = S0 + 3 * (long) spbS + 3000;               // note-on at bar 2 (beat 4, block 187 offset 256), off 3 beats later, mid-sound
        L.clear(); const long base = t;
        run(p, ph, t, (int) ((S1 + 24000 - t) / B) + 1, { { S0, 60, true }, { S1, 60, false } }, L, nullptr);
        long first = -1; for (size_t i = 0; i < L.size(); ++i) if (std::abs(L[i]) > 1e-6f) { first = base + (long) i; break; }
        bool silentBefore = true; for (long i = base; i < S0 && i - base < (long) L.size(); ++i) if (L[(size_t) (i - base)] != 0.0f) silentBefore = false;
        const int la = p.engine.lookahead();
        // reference: the same beat in ALWAYS, from the same host position, cut at S0 (same voices from the bar line): onset sample of the first hit
        // reference: a fresh instance in ALWAYS whose host starts playing at that same bar line: ON NOTES must play the same thing from the note-on
        BeatsXProcessor q; FakeHead qh; make(q, qh, 0); long tq = S0; std::vector<float> Lq; run(q, qh, tq, 8, {}, Lq); q.setPlayHead(nullptr);
        long firstQ = -1; for (size_t i = 0; i < Lq.size(); ++i) if (std::abs(Lq[i]) > 1e-6f) { firstQ = S0 + (long) i; break; }
        float refDiff = 0; size_t refAt = 0; for (size_t i = 0; i < Lq.size(); ++i) { const float dd = std::abs(L[(size_t) (S0 - base) + i] - Lq[i]); if (dd > refDiff) { refDiff = dd; refAt = i; } }
        (void) refAt; long sameFor = (long) Lq.size(); for (size_t i = 0; i < Lq.size(); ++i) if (std::abs(L[(size_t) (S0 - base) + i] - Lq[i]) > 1e-6f) { sameFor = (long) i; break; }   // (after that the seeded per-block randomness, which ran on while the gate was closed, differs from the fresh instance)
        float maxStepMusic = 0, maxStepFade = 0; for (long i = S0 + 1; i < S1; ++i) maxStepMusic = std::max(maxStepMusic, std::abs(L[(size_t) (i - base)] - L[(size_t) (i - base - 1)]));
        for (long i = S1; i < S1 + 400; ++i) maxStepFade = std::max(maxStepFade, std::abs(L[(size_t) (i - base)] - L[(size_t) (i - base - 1)]));
        float peakMusic = 0; for (long i = S0; i < S1; ++i) peakMusic = std::max(peakMusic, std::abs(L[(size_t) (i - base)]));
        long lastNz = -1; for (long i = (long) L.size() - 1; i >= 0; --i) if (L[(size_t) i] != 0.0f) { lastNz = base + i; break; }
        float pkAfter = 0; std::vector<float> L2; run(p, ph, t, 94, {}, L2, &pkAfter);    // one more second with no note: every bus
        std::printf("  no note, host playing 1 s: peak on all %d channels %.1e (exact 0 wanted)\n", p.getTotalNumOutputChannels(), pkNo);
        std::printf("  note-on at sample %ld (bar 2): silent before %s; first sound at %ld = note + %ld (limiter look-ahead %d); a host starting at that bar plays its first sound at %ld; identical to it for the first %ld samples (%.1f ms; largest difference over 4096 samples %.1e)\n", S0, silentBefore ? "yes" : "NO", first, first - S0, la, firstQ, sameFor, sameFor * 1000.0 / sr, refDiff);
        std::printf("  note-off at %ld: last non-zero sample %ld = +%.2f ms (6 ms fade); fade's largest step %.4f vs %.4f in the music (peak %.2f); next second peak on every bus %.1e\n",
                    S1, lastNz, (lastNz - S1) * 1000.0 / sr, maxStepFade, maxStepMusic, peakMusic, pkAfter);
        CHECK(pkNo == 0.0f, "ON NOTES: no note -> exact silence");
        CHECK(silentBefore && first >= S0 && first <= S0 + la + 8 && peakMusic > 0.05f && first == firstQ && sameFor >= 1024, "ON NOTES: the first hit lands on the grid at the note-on");
        CHECK(lastNz >= S1 && lastNz <= S1 + (long) (0.0062 * sr) + 1 && maxStepFade <= maxStepMusic * 1.2f + 1e-4f && pkAfter == 0.0f, "ON NOTES: note-off fades then exact silence (no click)");
        // 5) overlapping notes (two keys, two channels, the same key twice): plays until the LAST note-off
        std::vector<float> L3; const long a = t + 1000, b = a + 6000, c = a + 30000, d = a + 60000, e = a + 70000, f = a + 90000;
        run(p, ph, t, (int) ((f + 30000 - t) / B), { { a, 60, true }, { b, 64, true, 2 }, { c, 60, false }, { d, 64, true, 2 }, { e, 64, false, 2 }, { f, 64, false, 2 } }, L3);
        const long base3 = f + 30000 - (long) L3.size() - 0; (void) base3;
        auto energy = [&](long from, long to, long origin) { double s2 = 0; for (long i = from; i < to; ++i) { const float v = L3[(size_t) (i - origin)]; s2 += v * v; } return s2; };
        const long o3 = a - 1000; const double e1 = energy(c + 400, e, o3), e2 = energy(e + 400, f, o3), e3 = energy(f + 400, f + 29000, o3);
        std::printf("  overlapping notes: energy after the 1st note-off %.2f, after one of two held 64s is released %.2f, after the last note-off %.1e\n", e1, e2, e3);
        CHECK(e1 > 0.1 && e2 > 0.1 && e3 == 0.0, "ON NOTES: overlapping notes gate until the last note-off");
        // 6) host stop / start with a note held: stop -> silence; start again without a new note -> stays silent (stop releases held notes); next note -> plays
        std::vector<float> L4; const long g0 = t + 512 * 3; run(p, ph, t, 40, { { g0, 60, true } }, L4); float pkPlay = 0; for (size_t i = (size_t) (g0 - (t - 40 * B)); i < L4.size(); ++i) pkPlay = std::max(pkPlay, std::abs(L4[i]));
        ph.playing = false; std::vector<float> L5; float pkStop = 0; run(p, ph, t, 4, {}, L5); run(p, ph, t, 30, {}, L5, &pkStop);
        ph.playing = true; std::vector<float> L6; float pkRestart = 0; run(p, ph, t, 40, {}, L6, &pkRestart);
        const long n0 = (t / (long) spbS + 1) * (long) spbS; std::vector<float> L7; float pkNew = 0; run(p, ph, t, (int) ((n0 + 24000 - t) / B), { { n0, 61, true } }, L7, &pkNew);
        std::printf("  transport with a note held: playing peak %.2f; stopped peak %.1e; restarted without a new note %.1e; next note-on %.2f\n", pkPlay, pkStop, pkRestart, pkNew);
        CHECK(pkPlay > 0.05f && pkStop == 0.0f && pkRestart == 0.0f && pkNew > 0.05f, "ON NOTES: host stop / start never leaves it stuck");
        // 7) in ON NOTES the notes only gate: no pad hit / recording from MIDI; the on-screen pads still play while closed
        { run(p, ph, t, 1, { { t + 600, 61, false } }, L7); std::vector<float> Lx; run(p, ph, t, 4, {}, Lx);
          p.recording.store(true); std::vector<float> Lr; run(p, ph, t, 20, { { t + 100, 36, true }, { t + 9000, 36, false } }, Lr); const int recd = p.engine.recFifo.getNumReady(); p.recording.store(false);
          std::vector<float> Lp; run(p, ph, t, 6, {}, Lp); p.engine.pushPad(0, 0.9f); float pkPad = 0; std::vector<float> Lq2; run(p, ph, t, 4, {}, Lq2, &pkPad);
          std::printf("  notes in ON NOTES: recorded hits %d (0 wanted); an on-screen pad with no note held: peak %.2f\n", recd, pkPad);
          CHECK(recd == 0 && pkPad > 0.05f, "ON NOTES: MIDI notes gate only, on-screen pads still play"); }
        // 8) BYPASS wins: note held + bypass -> silent; note-off while bypassed; release -> still silent (no stuck gate)
        { std::vector<float> Lb; const long h0 = t + 100; run(p, ph, t, 10, { { h0, 62, true } }, Lb); setParam(p, "bypass", 1.0f); float pkB = 0; std::vector<float> Lb2; run(p, ph, t, 3, {}, Lb2); run(p, ph, t, 20, { { t + 4000, 62, false } }, Lb2, &pkB);
          setParam(p, "bypass", 0.0f); float pkRel = 0; std::vector<float> Lb3; run(p, ph, t, 40, {}, Lb3, &pkRel);
          std::printf("  BYPASS over ON NOTES: bypassed peak %.1e; released with no note held %.1e\n", pkB, pkRel); CHECK(pkB == 0.0f && pkRel == 0.0f, "BYPASS wins over PLAY ON NOTES"); }
        p.setPlayHead(nullptr);
    }
    // 9) saved per project, never from a beat / kit / undo; old projects load ALWAYS
    {
        BeatsXProcessor p; p.playMode.store(1); p.pushUndo(); p.applyKit(3, true); p.undo(); const bool kept = p.playMode.load() == 1;
        juce::MemoryBlock mb; p.getStateInformation(mb); BeatsXProcessor q; q.restoreState(mb.getData(), (int) mb.getSize());
        BeatsXProcessor a0; juce::MemoryBlock ma; a0.getStateInformation(ma); BeatsXProcessor a1; a1.playMode.store(1); a1.restoreState(ma.getData(), (int) ma.getSize());
        juce::ValueTree st("GHOSTYBEATS2"); juce::ValueTree bt("BEATS2"); bt.setProperty("ver", 3, nullptr); st.appendChild(bt, nullptr); juce::MemoryBlock ob; if (auto xml = st.createXml()) juce::AudioProcessor::copyXmlToBinary(*xml, ob);
        BeatsXProcessor o; o.playMode.store(1); o.restoreState(ob.getData(), (int) ob.getSize());
        const auto beatFile = juce::File::createTempFile(".ghostbeat"); beatFile.replaceWithData(mb.getData(), mb.getSize()); BeatsXProcessor lb; lb.loadBeat(beatFile); beatFile.deleteFile();
        const bool alwaysStateClean = !juce::ValueTree::readFromData(ma.getData(), ma.getSize()).hasProperty("playmode");
        std::printf("  kit + undo keep ON NOTES: %d; saved ON NOTES reloads: %d; an ALWAYS save loads ALWAYS (and has no new property: %d): %d; an old project loads ALWAYS: %d; LOAD BEAT of an ON NOTES save leaves ALWAYS: %d\n",
                    (int) kept, q.playMode.load(), (int) alwaysStateClean, (int) (a1.playMode.load() == 0), (int) (o.playMode.load() == 0), (int) (lb.playMode.load() == 0));
        CHECK(kept && q.playMode.load() == 1 && a1.playMode.load() == 0 && alwaysStateClean && o.playMode.load() == 0 && lb.playMode.load() == 0, "PLAY mode state rules");
    }
}


// SYNC TO DAW (Shared/SyncTransport.h): DAW | OWN
static void testSync() {
    section("SYNC: DAW | OWN (Shared/SyncTransport.h)");
    const int B = 512; const double sr = 48000;
    auto setup = [&](BeatsXProcessor& p, FakeHead& ph) { p.applyKit(kitIndexOf("Drum & Bass"), true); setParam(p, "songmode", 0); setParam(p, "verb", 0.3f); setParam(p, "echo", 0.3f);
                                                         p.setPlayHead(&ph); p.prepareToPlay(sr, B); };
    auto block = [&](BeatsXProcessor& p, FakeHead* ph, juce::AudioBuffer<float>& buf, double hostBpm) { juce::MidiBuffer m; if (ph && ph->playing) ph->ppq += B * hostBpm / 60.0 / sr; buf.clear(); p.processBlock(buf, m); };
    // 1) DAW (default): processBlock == the engine fed the host's transport, at a host tempo that differs from the kit's
    {
        BeatsXProcessor p; FakeHead ph; ph.bpm = 133; ph.ppq = 7.25; setup(p, ph); auto spec = p.makeSpec({ false, false, -1 }); spec.P.bpm = 133.0f;
        gb::BeatEngine ref; ref.prepare(sr, B); ref.setSnapshot(spec.snap.get()); juce::AudioBuffer<float> buf(2, B), rb(2, B); juce::MidiBuffer m; double md = 0;
        for (int i = 0; i < 120; ++i) { if (i == 60) ph.ppq = 1.0; buf.clear(); p.processBlock(buf, m); rb.clear(); ref.process(rb, m, spec.P, 133, ph.ppq, true); ph.ppq += B * 133 / 60.0 / sr;
            for (int c = 0; c < 2; ++c) for (int k = 0; k < B; ++k) md = std::max(md, (double) std::abs(buf.getSample(c, k) - rb.getSample(c, k))); }
        std::printf("  DAW (default) at 133 bpm with a locate: processBlock vs the engine on the host clock: max diff %.1e\n", md);
        CHECK(!p.ownClock() && md == 0.0, "SYNC DAW is bit-identical");
        p.setPlayHead(nullptr);
    }
    // 2) OWN: the output depends only on the own tempo + own PLAY (two very different host transports -> identical); 3) PLAY starts at the pattern start
    {
        auto renderOwn = [&](double hostBpm, bool hostPlaying, double hostPpq, std::vector<float>& out) {
            BeatsXProcessor p; FakeHead ph; ph.bpm = hostBpm; ph.playing = hostPlaying; ph.ppq = hostPpq; setup(p, ph);
            p.sync.setMode(ghosty::SyncTransport::Mode::Own); p.sync.setOwnBpm(97); juce::AudioBuffer<float> buf(2, B);
            for (int i = 0; i < 6; ++i) block(p, &ph, buf, hostBpm);                         // own clock stopped: silence whatever the host does
            float pre = 0; for (int i = 0; i < 6; ++i) { block(p, &ph, buf, hostBpm); pre = std::max(pre, buf.getMagnitude(0, 0, B)); }
            p.sync.startOwn(0.0);
            for (int i = 0; i < 200; ++i) { if (i == 100) { ph.playing = !ph.playing; ph.ppq = 3.0; ph.bpm = 61; } block(p, &ph, buf, ph.bpm); for (int k = 0; k < B; ++k) out.push_back(buf.getSample(0, k)); }
            p.setPlayHead(nullptr); return pre;
        };
        std::vector<float> a, b; const float preA = renderOwn(174, true, 33.3, a), preB = renderOwn(90, false, 0, b); double md = 0; for (size_t i = 0; i < a.size(); ++i) md = std::max(md, (double) std::abs(a[i] - b[i]));
        // reference: the engine on a clock that starts at beat 0 at 97 bpm, advanced like the own clock
        BeatsXProcessor p; FakeHead ph; setup(p, ph); auto spec = p.makeSpec({ false, false, -1 }); spec.P.bpm = 97.0f; p.setPlayHead(nullptr);
        gb::BeatEngine ref; ref.prepare(sr, B); ref.setSnapshot(spec.snap.get()); juce::AudioBuffer<float> rb(2, B); juce::MidiBuffer m;
        for (int i = 0; i < 12; ++i) { rb.clear(); ref.process(rb, m, spec.P, 97, 0, false); }   // (stopped for the same 12 blocks first: the seeded randomness runs on)
        double rppq = 0, mr = 0; long first = -1, firstRef = -1, divAt = -1;
        for (int i = 0; i < 200; ++i) { rb.clear(); ref.process(rb, m, spec.P, 97, rppq, true); rppq += (double) B * 97 / 60.0 / sr;
            for (int k = 0; k < B; ++k) { const double dd = std::abs(rb.getSample(0, k) - a[(size_t) (i * B + k)]); if (dd > 1e-6 && divAt < 0) divAt = i * B + k; mr = std::max(mr, dd); if (firstRef < 0 && rb.getSample(0, k) != 0.0f) firstRef = i * B + k; if (first < 0 && a[(size_t) (i * B + k)] != 0.0f) first = i * B + k; } }
        std::printf("  OWN at 97 bpm: host A (174 bpm, playing, then stops + jumps) vs host B (90 bpm, stopped, then starts at 61 bpm): max diff %.1e; silent before PLAY: %.1e / %.1e\n", md, preA, preB);
        std::printf("  OWN PLAY: first sound at sample %ld (look-ahead %d), the engine started at beat 0: %ld; first difference to it at sample %ld (-1 = none; largest difference over 2.1 s %.1e)\n", first, p.engine.lookahead(), firstRef, divAt, mr);
        CHECK(md == 0.0 && preA == 0.0f && preB == 0.0f, "SYNC OWN ignores the DAW's transport + tempo");
        CHECK(first == firstRef && first == p.engine.lookahead() && mr == 0.0, "SYNC OWN PLAY starts at the pattern start, on the grid");
    }
    // 4) OWN tempo change while playing: next block, no click, the position stays continuous
    {
        BeatsXProcessor p; FakeHead ph; setup(p, ph); p.sync.setMode(ghosty::SyncTransport::Mode::Own); p.sync.setOwnBpm(120); p.sync.startOwn(0.0); juce::AudioBuffer<float> buf(2, B);
        float stepMusic = 0, stepChange = 0, prev = 0; double ppqBefore = 0, ppqAfter = 0;
        for (int i = 0; i < 160; ++i) { if (i == 80) { ppqBefore = p.sync.getOwnPpq(); p.sync.setOwnBpm(150); } block(p, &ph, buf, 120); if (i == 80) ppqAfter = p.sync.getOwnPpq();
            for (int k = 0; k < B; ++k) { const float v = buf.getSample(0, k); const float d = std::abs(v - prev); prev = v; if (i < 80 && i > 2) stepMusic = std::max(stepMusic, d); if (i >= 80 && i < 82) stepChange = std::max(stepChange, d); } }
        const double adv = ppqAfter - ppqBefore, want = B * 150.0 / 60.0 / sr;
        std::printf("  OWN 120 -> 150 bpm mid-play: the next block advances %.6f beats (150 bpm: %.6f); largest sample step at the change %.4f vs %.4f in the music\n", adv, want, stepChange, stepMusic);
        CHECK(std::abs(adv - want) < 1e-9 && stepChange <= stepMusic * 1.2f + 1e-4f, "SYNC OWN tempo change: next block, no click");
        p.setPlayHead(nullptr);
    }
    // 5) mode switch mid-play: DAW (host playing) -> OWN (stopped): the beat stops and its tails die away (no stuck voices); OWN playing -> DAW (host stopped): stops
    {
        BeatsXProcessor p; FakeHead ph; ph.bpm = 140; setup(p, ph); juce::AudioBuffer<float> buf(2, B); float loud = 0;
        for (int i = 0; i < 100; ++i) { block(p, &ph, buf, 140); loud = std::max(loud, buf.getMagnitude(0, 0, B)); }
        p.sync.setMode(ghosty::SyncTransport::Mode::Own); float tail = 0; for (int i = 0; i < 400; ++i) { block(p, &ph, buf, 140); if (i >= 300) tail = std::max(tail, buf.getMagnitude(0, 0, B)); }
        const int active = p.engine.activeVoices(); const int stepOwnStopped = p.engine.uiStep.load();
        p.sync.startOwn(0.0); float ownLoud = 0; for (int i = 0; i < 100; ++i) { block(p, &ph, buf, 140); ownLoud = std::max(ownLoud, buf.getMagnitude(0, 0, B)); }
        ph.playing = false; p.sync.setMode(ghosty::SyncTransport::Mode::Daw); p.sync.stopOwn(); float tail2 = 0; for (int i = 0; i < 400; ++i) { block(p, &ph, buf, 140); if (i >= 300) tail2 = std::max(tail2, buf.getMagnitude(0, 0, B)); }
        const int active2 = p.engine.activeVoices();
        std::printf("  DAW playing (peak %.2f) -> OWN stopped: 3.2-4.3 s later peak %.1e, voices left %d, playhead %d; OWN PLAY peak %.2f -> DAW stopped: later peak %.1e, voices left %d\n", loud, tail, active, stepOwnStopped, ownLoud, tail2, active2);
        CHECK(loud > 0.05f && tail < 1e-4f && active == 0 && stepOwnStopped == -1 && ownLoud > 0.05f && tail2 < 1e-4f && active2 == 0, "SYNC mode switch mid-play stops cleanly");
        p.setPlayHead(nullptr);
    }
    // 6) SPACE (editor focus): OWN toggles Beats' own PLAY (consumed); DAW leaves it to the DAW (not consumed)
    {
        BeatsXProcessor p; std::unique_ptr<juce::AudioProcessorEditor> e(p.createEditor()); auto* be = dynamic_cast<BeatsXEditor*>(e.get()); p.prepareToPlay(sr, B); juce::AudioBuffer<float> buf(2, B);
        const juce::KeyPress space(juce::KeyPress::spaceKey);
        const bool dawTaken = be->keyPressForTest(space); block(p, nullptr, buf, 120); const bool dawPlays = p.sync.isOwnPlaying() || p.engine.internalPlay;
        p.sync.setMode(ghosty::SyncTransport::Mode::Own); be->syncUiForTest(); const bool ownTaken = be->keyPressForTest(space); block(p, nullptr, buf, 120); const bool ownPlays = p.sync.isOwnPlaying();
        const bool ownTaken2 = be->keyPressForTest(space); block(p, nullptr, buf, 120); const bool ownStops = !p.sync.isOwnPlaying();
        std::printf("  SPACE in DAW: consumed %d, plays %d | in OWN: consumed %d, starts %d, again consumed %d, stops %d\n", (int) dawTaken, (int) dawPlays, (int) ownTaken, (int) ownPlays, (int) ownTaken2, (int) ownStops);
        CHECK(!dawTaken && !dawPlays && ownTaken && ownPlays && ownTaken2 && ownStops, "SPACE: OWN toggles, DAW not consumed");
        e.reset();
    }
    // 7) saved per project (mode + own tempo), never from a beat / kit / undo; old projects load DAW (and a DAW project saves no new properties)
    {
        BeatsXProcessor p; p.sync.setMode(ghosty::SyncTransport::Mode::Own); p.sync.setOwnBpm(87); p.pushUndo(); p.applyKit(5, true); p.undo(); const bool kept = p.ownClock() && p.sync.getOwnBpm() == 87.0;
        juce::MemoryBlock mb; p.getStateInformation(mb); BeatsXProcessor q; q.restoreState(mb.getData(), (int) mb.getSize());
        BeatsXProcessor d; juce::MemoryBlock md; d.getStateInformation(md); const auto dt = juce::ValueTree::readFromData(md.getData(), md.getSize()); const bool clean = !dt.hasProperty("syncmode") && !dt.hasProperty("ownbpm");
        juce::ValueTree st("GHOSTYBEATS2"); juce::ValueTree bt("BEATS2"); bt.setProperty("ver", 3, nullptr); st.appendChild(bt, nullptr); juce::MemoryBlock ob; if (auto xml = st.createXml()) juce::AudioProcessor::copyXmlToBinary(*xml, ob);
        BeatsXProcessor o; o.sync.setMode(ghosty::SyncTransport::Mode::Own); o.sync.setOwnBpm(150); o.restoreState(ob.getData(), (int) ob.getSize());
        const auto beatFile = juce::File::createTempFile(".ghostbeat"); beatFile.replaceWithData(mb.getData(), mb.getSize()); BeatsXProcessor lb; lb.loadBeat(beatFile); beatFile.deleteFile();
        std::printf("  kit + undo keep OWN 87: %d; saved OWN 87 reloads: %d / %.0f; a DAW project has no new properties: %d; an old project loads DAW 120: %d / %.0f; LOAD BEAT leaves DAW: %d\n",
                    (int) kept, (int) q.ownClock(), q.sync.getOwnBpm(), (int) clean, (int) !o.ownClock(), o.sync.getOwnBpm(), (int) !lb.ownClock());
        CHECK(kept && q.ownClock() && q.sync.getOwnBpm() == 87.0 && clean && !o.ownClock() && o.sync.getOwnBpm() == 120.0 && !lb.ownClock() && lb.sync.getOwnBpm() == 120.0, "SYNC state rules");
    }
}


// FEATURE AUDIT polish: brand-free names, PADS help in ON NOTES, the slider convention, the version, the demo export gate
static juce::MouseEvent mouseAt(juce::Component& c, juce::Point<float> p, juce::ModifierKeys mods, int clicks = 1, bool dragged = false) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, juce::Time::getCurrentTime(), p, juce::Time::getCurrentTime(), clicks, dragged);
}
static void testPolish() {
    section("POLISH: names, PADS help, slider convention, version, demo export gate");
    {   // 1) no product / brand names in what the user sees (the genre word '808' is fine); grooves keep their stored index + timing
        const char* brands[] = { "MPC", "Akai", "Roland", "TR-", "Linn", "Serato", "Maschine", "Ableton", "SP-1200", "SP1200", "E-mu", "Emulator", "Fairlight", "Mellotron", "Simmons", "Oberheim", "DMX", "Korg", "Yamaha", "Casio", "Moog", "Rhodes", "Wurli", "Melodyne", "Splice", "Elektron", "Ensoniq", "Amiga", "Commodore", "Atari", "Nintendo", "Game Boy", "Sega", "Amen" };
        juce::StringArray ui; ui.addArray(gb::grooveNames()); ui.addArray(gb::ruleNames()); ui.addArray(gb::linear::ruleNames()); ui.addArray(gb::browserFolders()); ui.addArray(bx::LaneEditor::names());
        for (auto& k : gb::allKits()) ui.add(k.name); for (int i = 0; i < vc::kNumModes; ++i) ui.add(vc::kModeNames[i]);
        BeatsXProcessor p; for (auto& snd : p.lib->sounds()) ui.add(snd->name);
        juce::StringArray hits; for (auto& u : ui) for (auto* b : brands) if (u.contains(b)) hits.add(u + " (" + b + ")");
        std::printf("  %d names checked (grooves, rules, folders, lanes, kits, CHARACTER modes, %d sounds): %d brand words%s%s; groove 2 = \"%s\" offset %.2f\n", ui.size(), (int) p.lib->sounds().size(), hits.size(), hits.isEmpty() ? "" : ": ", hits.joinIntoString(", ").toRawUTF8(), gb::grooveNames()[2].toRawUTF8(), gb::grooveOffset(2, 1));
        CHECK(hits.isEmpty() && gb::grooveNames()[2] == "Swing 58%" && gb::grooveNames().size() == 7 && std::abs(gb::grooveOffset(2, 1) - 0.16) < 1e-9, "no brand names in the UI");
    }
    BeatsXProcessor p; p.applyKit(kitIndexOf("Drum & Bass"), true); std::unique_ptr<juce::AudioProcessorEditor> e(p.createEditor()); auto* be = dynamic_cast<BeatsXEditor*>(e.get());
    {   // 2) PADS help follows PLAY mode
        const auto a = be->padHelpForTest(); p.playMode.store(1); const auto b = be->padHelpForTest(); p.playMode.store(0);
        std::printf("  PADS help ALWAYS: \"...%s\" | ON NOTES: \"...%s\"\n", a.fromLastOccurrenceOf("rolls. ", false, false).toRawUTF8(), b.fromLastOccurrenceOf("rolls. ", false, false).toRawUTF8());
        CHECK(a.contains("C1-D#2 play pads") && !b.contains("C1-D#2 play pads") && b.contains("only start / stop the beat") && b.contains("on-screen pads still play"), "PADS help in ON NOTES");
    }
    {   // 3a) the house Bar: click sets, drag scrubs, shift = fine, wheel = one step, double-click = default, typed value; ONE gesture each
        bx::Bar b; b.setRange(0, 100, 1); b.setDoubleClickReturnValue(true, 40); b.setValue(40, juce::dontSendNotification); b.setBounds(0, 0, 202, 24); int gestures = 0; b.onDragStart = [&] { ++gestures; };
        const auto none = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), shift = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier);
        b.mouseDown(mouseAt(b, { 151, 12 }, none)); const double clickV = b.getValue(); b.mouseDrag(mouseAt(b, { 51, 12 }, none, 1, true)); const double dragV = b.getValue(); b.mouseUp(mouseAt(b, { 51, 12 }, {}, 1, true)); const int g1 = gestures;
        juce::Thread::sleep(500);
        b.mouseDown(mouseAt(b, { 100, 12 }, shift)); const double fineStart = b.getValue(); b.mouseDrag(mouseAt(b, { 180, 12 }, shift, 1, true)); const double fineV = b.getValue(); b.mouseUp(mouseAt(b, { 180, 12 }, {}, 1, true)); const int g2 = gestures;
        juce::Thread::sleep(500);
        juce::MouseWheelDetails w; w.deltaX = 0; w.deltaY = 0.1f; w.isReversed = false; w.isSmooth = false; w.isInertial = false; const double beforeWheel = b.getValue();
        for (int i = 0; i < 3; ++i) { auto ev = mouseAt(b, { 100, 12 }, {}); b.mouseWheelMove(ev.withNewPosition(juce::Point<float>(100.0f + i, 12.0f)), w); juce::Thread::sleep(5); }
        const double wheelV = b.getValue(); const int g3 = gestures;
        juce::Thread::sleep(500); b.mouseDoubleClick(mouseAt(b, { 20, 12 }, none, 2)); const double dbl = b.getValue(); const int g4 = gestures;
        juce::Thread::sleep(500); b.setValue(73, juce::sendNotificationSync); const int g5 = gestures;   // what ghosty::TypeIn does on Enter
        juce::Thread::sleep(500); b.mouseDown(mouseAt(b, { 30, 12 }, juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier))); b.mouseUp(mouseAt(b, { 30, 12 }, {})); const double afterRight = b.getValue();
        std::printf("  Bar: click at 75%% -> %.0f, drag to 25%% -> %.0f (1 gesture: %d) | shift-drag +80 px from %.0f -> %.0f (fine 1/8; gestures %d) | 3 wheel steps %.0f -> %.0f (gestures %d) | double-click -> %.0f (%d) | typed 73 (%d) | right-click leaves %.0f\n",
                    clickV, dragV, g1, fineStart, fineV, g2, beforeWheel, wheelV, g3, dbl, g4, g5, afterRight);
        CHECK(std::abs(clickV - 75) <= 1 && std::abs(dragV - 25) <= 1 && g1 == 1 && fineStart == dragV && std::abs(fineV - (fineStart + 5)) <= 1 && g2 == 2 && wheelV == beforeWheel + 3 && g3 == 3 && dbl == 40 && g4 == 4 && g5 == 5 && afterRight == 73,
              "slider convention on bars (one gesture each)");
    }
    {   // 3c) hover / text values in real units (model-bound knobs), and typing them back
        bx::Bar g(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox), pn(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox), st(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox);
        bx::setUnits(g, bx::Unit::Gain); bx::setUnits(pn, bx::Unit::Pan); bx::setUnits(st, bx::Unit::Semis);
        const auto a = g.getTextFromValue(0.5), b = pn.getTextFromValue(-0.3), c = st.getTextFromValue(7); const double back = pn.getValueFromText("L 30"), backG = g.getValueFromText("-6");
        std::printf("  units: gain 0.5 -> \"%s\", pan -0.3 -> \"%s\", pitch 7 -> \"%s\"; typed \"L 30\" -> %.2f, \"-6\" dB -> %.3f\n", a.toRawUTF8(), b.toRawUTF8(), c.toRawUTF8(), back, backG);
        CHECK(a == "-6.0 dB" && b == "L 30" && c == "+7 st" && std::abs(back + 0.3) < 1e-9 && std::abs(backG - 0.501) < 0.001, "values in real units");
    }
    {   // 3b) MIX faders + pan (custom strips): click sets, double-click = unity, wheel = 0.5 dB, right-click types dB; one undo step per gesture
        bx::MixPage* mix = nullptr; bx::LaneEditor* lane = nullptr; std::function<void(juce::Component&)> find = [&](juce::Component& c) { if (auto* m = dynamic_cast<bx::MixPage*>(&c)) mix = m; if (auto* l = dynamic_cast<bx::LaneEditor*>(&c)) if (l->isVisible() && l->getWidth() > 0 && lane == nullptr) lane = l; for (auto* ch : c.getChildren()) find(*ch); }; find(*e);
        be->showPageForTest(8); auto& tr = p.model.tracks[0]; const int u0 = p.undoDepth(); const auto none = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier);
        auto fa = mix->faderArea(0).toFloat(); const float y60 = fa.getBottom() - 10 - (fa.getHeight() - 20) * 0.6f;
        mix->mouseDown(mouseAt(*mix, { fa.getCentreX(), y60 }, none)); mix->mouseUp(mouseAt(*mix, { fa.getCentreX(), y60 }, {})); const float clickVol = tr.vol; const int u1 = p.undoDepth();
        mix->mouseDoubleClick(mouseAt(*mix, { fa.getCentreX(), y60 }, none, 2)); const float dblVol = tr.vol; const int u2 = p.undoDepth();
        juce::Thread::sleep(600); juce::MouseWheelDetails w; w.deltaX = 0; w.deltaY = 0.1f; w.isReversed = false; w.isSmooth = false; w.isInertial = false;
        for (int i = 0; i < 3; ++i) mix->mouseWheelMove(mouseAt(*mix, { fa.getCentreX(), y60 }, {}), w); const float wheelDb = juce::Decibels::gainToDecibels(tr.vol) - juce::Decibels::gainToDecibels(dblVol); const int u3 = p.undoDepth(); const float wheelVol = tr.vol;
        juce::Thread::sleep(600); mix->mouseDown(mouseAt(*mix, { fa.getCentreX(), y60 }, juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier))); const bool typing = mix->typer.isVisible();
        mix->typer.setText("-6", false); mix->commitTyped(); const float typedDb = juce::Decibels::gainToDecibels(tr.vol); const int u4 = p.undoDepth();
        p.undo(); const float undone = tr.vol;
        std::printf("  MIX fader: click at 60%% -> gain %.3f (want %.3f; +%d undo) | double-click -> %.2f (unity 0.80; still +%d) | 3 wheel steps %+.2f dB (+%d undo) | right-click types: %d, \"-6\" -> %.2f dB (+%d) | UNDO -> %.2f\n",
                    clickVol, bx::MixPage::posToGain(0.6f), u1 - u0, dblVol, u2 - u0, wheelDb, u3 - u2, (int) typing, typedDb, u4 - u3, undone);
        CHECK(std::abs(clickVol - bx::MixPage::posToGain(0.6f)) < 0.02f && u1 - u0 == 1 && std::abs(dblVol - 0.8f) < 1e-6f && u2 == u1 && std::abs(wheelDb - 1.5f) < 0.01f && u3 - u2 == 1 && typing && std::abs(typedDb + 6.0f) < 0.01f && u4 - u3 == 1 && std::abs(undone - wheelVol) < 1e-6f,
              "MIX faders follow the slider convention");
        // lanes: right-click types the value of that step
        be->showPageForTest(0); lane = nullptr; find(*e); p.model.patterns[(size_t) p.currentPattern()].steps[0][0].vel = 100; p.publish(); lane->lane = 0; const int ul = p.undoDepth();
        lane->mouseDown(mouseAt(*lane, { 5.0f, lane->getHeight() * 0.5f }, juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier))); const bool lt = lane->typer.isVisible(); lane->typer.setText("64", false); lane->commitTyped();
        const int v = p.model.patterns[(size_t) p.currentPattern()].steps[0][0].vel;
        std::printf("  VEL lane: right-click types: %d; \"64\" -> %d (+%d undo)\n", (int) lt, v, p.undoDepth() - ul); CHECK(lt && v == 64 && p.undoDepth() - ul == 1, "lanes: right-click types a value");
    }
    {   // 4) the version is visible (wordmark tooltip + painted under the BEATS tag)
        juce::String tip; std::function<void(juce::Component&)> find = [&](juce::Component& c) { if (auto* t = dynamic_cast<juce::SettableTooltipClient*>(&c)) if (t->getTooltip().startsWith("GHOST KIT ")) tip = t->getTooltip(); /* GHOST KIT (brand): the visible name */ for (auto* ch : c.getChildren()) find(*ch); }; find(*e);
        std::printf("  version %s; wordmark tooltip \"%s\"\n", BeatsXProcessor::kVersion, tip.toRawUTF8()); CHECK(juce::String(BeatsXProcessor::kVersion) == "3.0.0" && tip.contains("3.0.0"), "version visible");
    }
    e.reset();
    {   // 5) demo-locked: every export is refused (no file); unlocked: files come out
        BeatsXProcessor q; q.applyKit(0, true); q.melody.on = true; q.publish(); q.demoForTest = true;
        const bool midi = q.exportMidi(false).existsAsFile(), trk = q.exportMidi(false, 0).existsAsFile(), mel = q.exportMelodyMidi(false).existsAsFile(), both = q.exportBeatAndMelodyMidi(false).existsAsFile();
        const int audio = q.waitAudioRender({ false, false, -1 }).size(), stems = q.waitAudioRender({ false, true, -1 }).size(), tAudio = q.waitAudioRender({ false, false, 0 }).size();
        const bool b1 = q.bounce(false, 1).existsAsFile(), saved = q.saveBeat("demo test beat");
        q.demoForTest = false; const auto okMidi = q.exportMidi(false); const bool unlockedMidi = okMidi.existsAsFile(); okMidi.deleteFile();
        std::printf("  demo-locked: MIDI %d, track MIDI %d, MELODY MIDI %d, BEAT+MELODY %d, AUDIO %d file(s), STEMS %d, track WAV %d, bounce %d, SAVE BEAT %d | unlocked MIDI %d\n", (int) midi, (int) trk, (int) mel, (int) both, audio, stems, tAudio, (int) b1, (int) saved, (int) unlockedMidi);
        q.beatsDir().getChildFile("demo test beat.ghostbeat").deleteFile();
        CHECK(!midi && !trk && !mel && !both && audio == 0 && stems == 0 && tAudio == 0 && !b1 && saved && unlockedMidi, "demo-locked exports refused (SAVE BEAT = own format, allowed)");
    }
}


// RE-CREATE AS MIDI: synthetic loops with known parts must come back on the right tracks / notes
namespace a2mt {
struct Synth {
    double sr; std::vector<float> x; juce::Random rnd { 4242 };
    Synth(double rate, double seconds) : sr(rate), x((size_t) (rate * seconds), 0.0f) {}
    void kick(double t, float a = 0.9f) { const int s = (int) (t * sr); double ph = 0; for (int i = 0; i < (int) (sr * 0.25) && s + i < (int) x.size(); ++i) { const double u = i / sr; const double f = 50 + 70 * std::exp(-u / 0.03); ph += juce::MathConstants<double>::twoPi * f / sr; x[(size_t) (s + i)] += a * (float) (std::sin(ph) * std::exp(-u / 0.12) * juce::jmin(1.0, (0.25 - u) / 0.02)); } }
    void noise(double t, float a, double decay, double hp, double lp, double len) {
        const int s = (int) (t * sr); auto H = gb::a2m::Biquad::make(true, hp, sr), H2 = H, Lp = gb::a2m::Biquad::make(false, lp, sr);
        for (int i = 0; i < (int) (sr * len) && s + i < (int) x.size(); ++i) { const double u = i / sr; float v = rnd.nextFloat() * 2 - 1; v = Lp.run(H2.run(H.run(v))); x[(size_t) (s + i)] += a * v * (float) std::exp(-u / decay) * (float) juce::jmin(1.0, u / 0.0005) * (float) juce::jmin(1.0, (len - u) / 0.01); }
    }
    void snare(double t) { const int s = (int) (t * sr); for (int i = 0; i < (int) (sr * 0.2) && s + i < (int) x.size(); ++i) { const double u = i / sr; x[(size_t) (s + i)] += 0.35f * (float) (std::sin(juce::MathConstants<double>::twoPi * 190 * u) * std::exp(-u / 0.06) * juce::jmin(1.0, (0.2 - u) / 0.01)); } noise(t, 0.9f, 0.09, 400, 5500, 0.3); }
    void hat(double t, bool open = false) { noise(t, 0.6f, open ? 0.25 : 0.022, 8000, 16000, open ? 0.6 : 0.12); }
    void note(double t, double len, int midi, bool saw) { const int s = (int) (t * sr); const double f = 440.0 * std::pow(2.0, (midi - 69) / 12.0); double ph = 0;
        for (int i = 0; i < (int) (len * sr) && s + i < (int) x.size(); ++i) { const double u = i / sr; ph += f / sr; ph -= std::floor(ph); const double env = juce::jmin(1.0, u / 0.003) * juce::jmin(1.0, (len - u) / 0.01);
            x[(size_t) (s + i)] += 0.5f * (float) (env * (saw ? (2 * ph - 1) : std::sin(juce::MathConstants<double>::twoPi * ph))); } }
};
// kick on beats 1 + 3, snare on 2 + 4, closed hats on every 8th (open hat on the last 8th of bar 2); swing delays the off-beat 8ths
struct Expect { int role, step; };
static gb::a2m::Loop beat(double bpm, int bars, double swingPct, double humanMs, std::vector<Expect>& ex, double sr = 44100) {
    const double beatSec = 60.0 / bpm; Synth S(sr, bars * 4 * beatSec); juce::Random hr(77);
    auto at = [&](int step16) { double b = step16 / 4.0; if (step16 % 4 == 2) b += (swingPct - 50.0) / 50.0 * 0.5; return b * beatSec + (humanMs > 0 ? (hr.nextDouble() * 2 - 1) * humanMs * 0.001 : 0.0) + 0.0005; };
    for (int bar = 0; bar < bars; ++bar) for (int s = 0; s < 16; ++s) {
        const int g = bar * 16 + s;
        if (s == 0 || s == 8) { S.kick(juce::jmax(0.0, at(g))); ex.push_back({ gb::a2m::Kick, g }); }
        if (s == 4 || s == 12) { S.snare(at(g)); ex.push_back({ gb::a2m::Snare, g }); }
        if (s % 2 == 0) { const bool open = bar == bars - 1 && s == 14; S.hat(juce::jmax(0.0, at(g)), open); ex.push_back({ open ? gb::a2m::HatOpen : gb::a2m::HatClosed, g }); }
    }
    gb::a2m::Loop L; L.sr = sr; L.x = S.x; L.name = "test loop"; return L;
}
}
#include "../RecreatePanel.h"
#include "RecreateBench.inc"
static void testRecreate() {
    section("RE-CREATE AS MIDI (audio loop -> pattern / bassline)");
    BeatsXProcessor p; p.applyKit(kitIndexOf("Drum & Bass"), true);
    const auto map = gb::a2m::mapRoles(p.model.tracks, [&](const juce::String& n) { return p.categoryOf(n); });
    std::printf("  kit tracks by role: kick %d, snare %d, clap %d, hat %d, open hat %d, tom %d, perc %d\n", map[0], map[1], map[2], map[3], map[4], map[5], map[6]);
    struct Case { const char* name; double bpm; int bars; double swing, human; };
    for (const auto& c : { Case { "straight 92 BPM, 2 bars", 92, 2, 50, 0 }, Case { "swung 58% + 20 ms humanize, 124 BPM, 4 bars", 124, 4, 58, 20 }, Case { "straight 140 BPM, 1 bar, 48 kHz", 140, 1, 50, 0 } }) {
        std::vector<a2mt::Expect> ex; auto L = a2mt::beat(c.bpm, c.bars, c.swing, c.human, ex, c.bpm == 140 ? 48000 : 44100);
        const auto t0 = juce::Time::getMillisecondCounterHiRes(); auto R = gb::a2m::analyse(L, {}); const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        gb::Pattern P; gb::a2m::writeDrums(P, p.model.tracks, R, map, {});
        int right = 0; for (auto& e : ex) { const int t = map[(size_t) e.role]; if (t >= 0 && P.steps[(size_t) t][(size_t) e.step].vel) ++right; }
        int total = 0; for (int t = 0; t < gb::kTracks; ++t) for (int s = 0; s < P.length; ++s) if (P.steps[(size_t) t][(size_t) s].vel) ++total;
        const int extra = total - right;
        std::printf("  %s: %s, %.2f BPM (%s), %d bar(s), %d onsets, swing %.0f%%; roles kick %d snare %d clap %d hat %d open %d tom %d perc %d; %d / %d hits right at 1/16 (%.0f%%), %d extra; %.0f ms\n",
                    c.name, R.pitched ? "PITCHED" : "DRUMS", R.bpm, R.bpmFrom.toRawUTF8(), R.bars, R.onsets, R.swing, R.perRole[0], R.perRole[1], R.perRole[2], R.perRole[3], R.perRole[4], R.perRole[5], R.perRole[6], right, (int) ex.size(), 100.0 * right / ex.size(), extra, ms);
        CHECK(!R.pitched && std::abs(R.bpm - c.bpm) / c.bpm < 0.01 && R.bars == c.bars && right >= 0.9 * ex.size() && extra <= 0.1 * ex.size(), "re-create %s", c.name);
    }
    {   // the file name's tempo (what Mr. Mickens / Beats drag out) + a 2 s tail
        std::vector<a2mt::Expect> ex; auto L = a2mt::beat(100, 2, 50, 0, ex); L.x.resize(L.x.size() + (size_t) (L.sr * 2.0), 0.0f); L.name = "Mr Mickens 100bpm Bay 1";
        auto R = gb::a2m::analyse(L, {}); std::printf("  named \"%s\" + 2 s tail: %.1f BPM (%s), %d bars\n", L.name.toRawUTF8(), R.bpm, R.bpmFrom.toRawUTF8(), R.bars);
        CHECK(R.bpm == 100 && R.bars == 2, "tempo from the file name, tail ignored");
    }
    {   // a bassline (saw, then sine) -> the right notes on the melody tracker, onsets within a 1/16
        const double bpm = 100, beatSec = 0.6; const int mids[] { 36, 43, 39, 41, 48, 34, 38, 45 }; const double starts[] { 0, 1, 1.5, 2.5, 3, 4, 5.5, 6.5 }, lens[] { 0.9, 0.45, 0.9, 0.45, 0.9, 1.4, 0.9, 1.3 };
        for (bool saw : { true, false }) {
            a2mt::Synth S(44100, 8 * beatSec); for (int i = 0; i < 8; ++i) S.note(starts[i] * beatSec, lens[i] * beatSec, mids[i], saw);
            gb::a2m::Loop L; L.sr = 44100; L.x = S.x; L.name = "bass"; auto R = gb::a2m::analyse(L, {});
            gb::mel::Pattern M; gb::a2m::writeNotes(M, R);
            int right = 0; for (int i = 0; i < 8; ++i) { const int row = (int) std::lround(starts[i] * 4); for (int d = -1; d <= 1; ++d) { const int r = row + d; if (r >= 0 && r < M.rows && M.cells[0][(size_t) r].note == mids[i] + 1) { ++right; break; } } }
            int notes = 0; for (int r = 0; r < M.rows; ++r) if (M.cells[0][(size_t) r].note && M.cells[0][(size_t) r].note != gb::mel::kNoteOff) ++notes;
            juce::String got; for (auto& nt : R.notes) got << nt.midi << "@" << juce::String((nt.sec) / beatSec, 2) << " ";
            std::printf("  %s bassline 100 BPM: %s (voiced %.0f%%), %.2f BPM, %d bars; %d / 8 notes right (+-0 st, within a 1/16), %d written: %s\n", saw ? "saw" : "sine", R.pitched ? "PITCHED" : "DRUMS", 100 * R.voicedRatio, R.bpm, R.bars, right, notes, got.toRawUTF8());
            CHECK(R.pitched && right == 8 && notes == 8, "bassline (%s) -> notes", saw ? "saw" : "sine");
        }
    }
    {   // the editor path: a WAV dropped on the grid -> analysed on a background thread -> applied as ONE undo step; a long file is read in part
        std::vector<a2mt::Expect> ex; auto L = a2mt::beat(96, 2, 50, 0, ex);
        auto wavOf = [](const gb::a2m::Loop& lp, const juce::String& name, int ch) { auto f = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(name); f.deleteFile();
            juce::WavAudioFormat wav; juce::AudioBuffer<float> b(ch, (int) lp.x.size()); for (int c = 0; c < ch; ++c) b.copyFrom(c, 0, lp.x.data(), (int) lp.x.size());
            if (auto os = std::unique_ptr<juce::OutputStream>(f.createOutputStream())) if (auto* w = wav.createWriterFor(os.get(), lp.sr, (unsigned) ch, 24, {}, 0)) { os.release(); std::unique_ptr<juce::AudioFormatWriter> wr(w); wr->writeFromAudioSampleBuffer(b, 0, b.getNumSamples()); }
            return f; };
        const auto f = wavOf(L, "ghosty test break.wav", 2);
        BeatsXProcessor q; q.applyKit(kitIndexOf("Drum & Bass"), true); std::unique_ptr<juce::AudioProcessorEditor> e(q.createEditor()); auto* be = dynamic_cast<BeatsXEditor*>(e.get());
        const int u0 = q.undoDepth(); be->startRecreateForTest(f);
        for (int i = 0; i < 400 && be->recreateBusyForTest(); ++i) { juce::Thread::sleep(10); be->recreateTickForTest(); }
        const auto& P = q.model.patterns[(size_t) q.currentPattern()]; int hits = 0; for (int t = 0; t < gb::kTracks; ++t) for (int st = 0; st < P.length; ++st) if (P.steps[(size_t) t][(size_t) st].vel) ++hits;
        const int u1 = q.undoDepth(), plen = P.length; const auto status = be->recreateStatusForTest(); q.undo(); int after = 0; const auto& P2 = q.model.patterns[(size_t) q.currentPattern()]; for (int t = 0; t < gb::kTracks; ++t) for (int st = 0; st < P2.length; ++st) if (P2.steps[(size_t) t][(size_t) st].vel) ++after;
        std::printf("  dropped WAV (stereo 24-bit): %d hits written, %d steps, +%d undo; status \"%s\"; UNDO -> %d hits (the kit's pattern again)\n", hits, plen, u1 - u0, status.upToFirstOccurrenceOf("\n", false, false).toRawUTF8(), after);
        CHECK(hits >= 22 && plen == 32 && u1 - u0 == 1 && status.contains("Found 2 bars, 96 BPM") && after != hits, "drop on the grid -> pattern, one undo step");
        e.reset(); f.deleteFile();
        gb::a2m::Loop longL; longL.sr = 8000; longL.x.assign((size_t) (8000 * 130), 0.0f); for (size_t i = 0; i < longL.x.size(); i += 4000) longL.x[i] = 0.8f;
        const auto lf = wavOf(longL, "ghosty long test.wav", 1); gb::a2m::Loop rd; juce::String err; const bool ok = gb::a2m::readFile(lf, rd, err);
        std::printf("  130 s file: read %d, %.1f s of it (truncated %d)\n", (int) ok, rd.seconds(), (int) rd.truncated); CHECK(ok && rd.truncated && std::abs(rd.seconds() - 120.0) < 0.01, "long files are read in part"); lf.deleteFile();
    }
    {   // silence, a click, odd rates: no crash, sensible results
        gb::a2m::Loop z; z.sr = 44100; z.x.assign(44100 * 2, 0.0f); auto R0 = gb::a2m::analyse(z, {});
        gb::a2m::Loop tiny; tiny.sr = 8000; tiny.x.assign(500, 0.0f); tiny.x[10] = 1; auto R1 = gb::a2m::analyse(tiny, {});
        std::vector<a2mt::Expect> ex; auto odd = a2mt::beat(110, 1, 50, 0, ex, 11025); auto R2 = gb::a2m::analyse(odd, {});
        gb::Pattern P; gb::a2m::writeDrums(P, p.model.tracks, R0, map, {}); int any = 0; for (auto& row : P.steps) for (auto& st : row) if (st.vel) ++any;
        std::printf("  silence: ok %d, %d hits, pattern hits %d | 500-sample click at 8 kHz: ok %d, %d hits | 11.025 kHz loop: %.1f BPM, %d hits\n", (int) R0.ok, (int) R0.hits.size(), any, (int) R1.ok, (int) R1.hits.size(), R2.bpm, (int) R2.hits.size());
        CHECK(R0.ok && R0.hits.empty() && any == 0 && R1.ok && std::abs(R2.bpm - 110) < 1.1, "silence / tiny / odd rate");
    }
    // ---- DRUMS v2 (docs/research/RECREATE-DRUMS.md): loops built from the factory kits at known steps, scored per drum (+-30 ms).
    //      Here every 4th kit, the 5 patterns clean + 2 processed variants, the 3 hard cases, the 8 factory breaks (BeatsTests
    //      "recreatebench" runs them all, "recreatetrain split" scores kits the classifier never saw).
    {
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        a2mb::Score per[3]; std::array<a2mb::Score, 3> hard {}; int autoDrums = 0, autoN = 0; BeatsXProcessor q;
        for (int k = 0; k < (int) gb::allKits().size(); k += 4) {
            const auto K = a2mb::kitOf(k); q.applyKit(k, true); const auto mp = gb::a2m::mapRoles(q.model.tracks, [&](const juce::String& n) { return q.categoryOf(n); });
            for (int pi = 0; pi < 5; ++pi) for (int v : { 0, 1, 4 }) {
                juce::Random rnd(1000 + k * 7 + pi * 13 + v); auto C = a2mb::build(K, a2mb::patterns()[(size_t) pi], rnd); if (C.truth.empty()) continue; a2mb::process(C, v, rnd);
                gb::a2m::Options o; o.mode = 0; const auto R = gb::a2m::analyse(C.L, o); per[v == 0 ? 0 : v == 1 ? 1 : 2].add(a2mb::scoreLoop(C, R, mp, q.model.tracks));
                if (v == 0 && K.s[a2mb::cHat]) { ++autoN; if (!gb::a2m::analyse(C.L, {}).pitched) ++autoDrums; }
            }
            for (int h = 0; h < 3; ++h) { auto C = a2mb::hardCase(h, K); if (C.truth.empty()) continue; gb::a2m::Options o; o.mode = 0; hard[(size_t) h].add(a2mb::scoreLoop(C, gb::a2m::analyse(C.L, o), mp, q.model.tracks)); }
        }
        const auto br = a2mb::runBreaks(); const double secs = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
        const auto& c = per[0]; using namespace a2mb;
        std::printf("  DRUMS v2, %d clean loops (every 4th kit): overall F1 %.3f | kick %.3f snare %.3f clap %.3f hat %.3f open hat %.3f tom %.3f perc %.3f cymbal %.3f 808 %.3f | velocity r %.2f | track accuracy %.1f %% | tempo %d / %d\n",
                    c.loops, c.overall(), c.f1(cKick), c.f1(cSnare), c.f1(cClap), c.f1(cHat), c.f1(cOpen), c.f1(cTom), c.f1(cPerc), c.f1(cCym), c.f1(cBass), c.velCorrMean(), 100.0 * c.trackOk / juce::jmax(1, c.trackN), c.tempoOk, c.loops);
        std::printf("    reverb %.3f, room + drive %.3f | kick + hat together %.3f, snare + clap layered %.3f, hats over a long 808 %.3f | factory breaks (never trained on) %.3f | AUTO -> DRUMS %d / %d | %.1f s\n",
                    per[1].overall(), per[2].overall(), hard[0].overall(), hard[1].overall(), hard[2].overall(), br.overall(), autoDrums, autoN, secs);
        CHECK(c.f1(cKick) >= 0.9 && c.f1(cSnare) >= 0.85 && c.f1(cClap) >= 0.8 && c.f1(cHat) >= 0.85 && c.f1(cOpen) >= 0.7 && c.overall() >= 0.85, "DRUMS v2 targets on clean kit loops (kick >= 0.9, snare >= 0.85, clap >= 0.8, hat >= 0.85, open hat >= 0.7, overall >= 0.85)");
        CHECK(per[1].overall() >= 0.8 && per[2].overall() >= 0.8, "DRUMS v2 degrades gracefully: reverb / room + drive overall >= 0.8");
        CHECK(hard[0].f1(cKick) >= 0.95 && hard[0].f1(cHat) >= 0.9 && hard[1].f1(cSnare) >= 0.85 && hard[1].f1(cClap) >= 0.5 && hard[2].f1(cHat) >= 0.9, "DRUMS v2 hard cases: kick + hat both found, snare + clap layered, hats over a long 808");
        CHECK(br.overall() >= 0.75 && autoDrums == autoN && c.tempoOk == c.loops, "DRUMS v2: the factory breaks (another mixer) >= 0.75; AUTO keeps every drum loop with hats in DRUMS; tempo right");
    }
    // ---- the panel: DRUMS HEARD (summary + one chip per drum found); a chip leaves that drum out, still ONE undo step, and back
    {
        BeatsXProcessor q; q.applyKit(kitIndexOf("Trap"), true); std::unique_ptr<juce::AudioProcessorEditor> e(q.createEditor()); auto* be = dynamic_cast<BeatsXEditor*>(e.get());
        const auto K = a2mb::kitOf(kitIndexOf("Trap")); juce::Random rnd(5); auto C = a2mb::build(K, a2mb::patterns()[0], rnd);
        auto f = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("ghosty trap test loop.wav"); a2mb::wav(C.L.x, C.L.sr, f);
        const int u0 = q.undoDepth(); be->startRecreateForTest(f);
        for (int i = 0; i < 600 && be->recreateBusyForTest(); ++i) { juce::Thread::sleep(10); be->recreateTickForTest(); }
        auto* panel = dynamic_cast<bx::RecreatePanel*>(be->recreatePanelForTest());
        int shown = 0, tips = 0, found = 0; for (int r = 0; r < gb::a2m::kNumRoles; ++r) { if (panel && panel->chips[(size_t) r].isVisible()) { ++shown; if (panel->chips[(size_t) r].getTooltip().contains("track")) ++tips; } if (panel && panel->result.perRole[(size_t) r] > 0) ++found; }
        const auto map = gb::a2m::mapRoles(q.model.tracks, [&](const juce::String& n) { return q.categoryOf(n); }); const int ht = map[gb::a2m::HatClosed];
        auto hitsOn = [&](int t) { int n = 0; const auto& P = q.model.patterns[(size_t) q.currentPattern()]; for (int st = 0; st < P.length; ++st) if (P.steps[(size_t) t][(size_t) st].vel) ++n; return n; };
        const int hatsBefore = ht >= 0 ? hitsOn(ht) : -1; int hatsOff = -1, hatsBack = -1, u1 = -1; juce::String txt;
        if (panel && ht >= 0) { txt = panel->heard.getText(); auto& c = panel->chips[gb::a2m::HatClosed]; c.setToggleState(false, juce::dontSendNotification); c.onClick(); hatsOff = hitsOn(ht); u1 = q.undoDepth();
                                c.setToggleState(true, juce::dontSendNotification); c.onClick(); hatsBack = hitsOn(ht); }
        std::printf("  panel: \"%s\" | %d chips shown for %d drums found (%d with a track tooltip) | hat track %d: %d hits, chip off %d, on again %d | undo +%d\n", txt.toRawUTF8(), shown, found, tips, ht, hatsBefore, hatsOff, hatsBack, u1 - u0);
        CHECK(panel && txt.startsWith("Drums heard:") && txt.contains("kick") && txt.contains("hat") && shown == found && tips == shown && hatsBefore > 8 && hatsOff == 0 && hatsBack == hatsBefore && u1 - u0 == 1,
              "DRUMS HEARD: summary + a chip per drum (tooltips name the track); a chip off leaves that drum out and back on restores it, one undo step");
        e.reset(); f.deleteFile();
    }
}


// THEMES on screen: every page (+ FOCUS, the RE-CREATE panel, the step editor pop-up) in BLACK, PURPLE and LIGHT. For every visible, enabled
// control with text (labels, buttons, combo boxes, toggles) the rendered pixels are checked: background = the most common colour in its
// area, text = the pixel with the most contrast to it; >= 4.5:1 wanted. And nothing is blacked out (no near-black area on PURPLE / LIGHT).
static double pxContrast(const juce::Image& img, juce::Rectangle<int> r, juce::Colour& bgOut) {
    r = r.getIntersection(img.getBounds()); if (r.getWidth() < 4 || r.getHeight() < 4) return 99;
    std::map<juce::uint32, int> hist; for (int y = r.getY(); y < r.getBottom(); ++y) for (int x = r.getX(); x < r.getRight(); ++x) { const auto c = img.getPixelAt(x, y); hist[(c.getARGB() & 0xfff8f8f8u) | 0xff000000u]++; }
    juce::uint32 mode = 0; int mc = -1; for (auto& [k, v] : hist) if (v > mc) { mc = v; mode = k; }
    const auto bg = juce::Colour(mode); bgOut = bg; double best = 1;
    for (int y = r.getY(); y < r.getBottom(); ++y) for (int x = r.getX(); x < r.getRight(); ++x) best = std::max(best, ghosty::themes::contrast(img.getPixelAt(x, y).withAlpha(1.0f), bg));
    return best;
}
static bool shownIn(juce::Component* c, juce::Component* root) { for (; c && c != root; c = c->getParentComponent()) if (!c->isVisible()) return false; return c == root; }
static void testThemePixels() {
    section("THEMES on screen: BLACK / PURPLE / LIGHT, every page, pixel contrast");
    BeatsXProcessor p; p.applyKit(10, true); std::unique_ptr<juce::AudioProcessorEditor> e(p.createEditor()); auto* be = dynamic_cast<BeatsXEditor*>(e.get()); e->setSize(1180, 790);
    std::vector<a2mt::Expect> ex; auto L = a2mt::beat(96, 1, 50, 0, ex); auto wavf = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("ghosty theme loop 96bpm.wav"); wavf.deleteFile();
    { juce::WavAudioFormat wav; juce::AudioBuffer<float> b(1, (int) L.x.size()); b.copyFrom(0, 0, L.x.data(), (int) L.x.size()); if (auto os = std::unique_ptr<juce::OutputStream>(wavf.createOutputStream())) if (auto* w = wav.createWriterFor(os.get(), L.sr, 1, 24, {}, 0)) { os.release(); std::unique_ptr<juce::AudioFormatWriter> wr(w); wr->writeFromAudioSampleBuffer(b, 0, b.getNumSamples()); } }
    const char* pages[] { "grid", "sounds", "process", "tracker", "lab", "pads", "song", "scripts", "mix", "melody" };
    for (int ti = 0; ti < 3; ++ti) {
        be->setThemeForTest(ti); const auto t = ghosty::themes::fromInt(ti);
        double worst = 99; juce::String worstWhat; int checked = 0, failed = 0; double blackWorst = 0;
        auto scan = [&](juce::Component& root, const juce::String& where) {
            for (int i = 0; i < 3; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
            const auto img = root.createComponentSnapshot(root.getLocalBounds(), true, 1.0f);
            int nearBlack = 0; for (int y = 0; y < img.getHeight(); y += 2) for (int x = 0; x < img.getWidth(); x += 2) { const auto c = img.getPixelAt(x, y); if (c.getRed() < 4 && c.getGreen() < 4 && c.getBlue() < 4) ++nearBlack; }
            blackWorst = std::max(blackWorst, (double) nearBlack / ((img.getWidth() / 2) * (img.getHeight() / 2)));
            std::function<void(juce::Component&)> walk = [&](juce::Component& c) {
                for (auto* ch : c.getChildren()) walk(*ch);
                if (&c == &root || !shownIn(&c, &root) || !c.isEnabled()) return;
                juce::String text;
                if (auto* l = dynamic_cast<juce::Label*>(&c)) { if (l->isBeingEdited() || dynamic_cast<juce::ComboBox*>(l->getParentComponent()) || dynamic_cast<juce::Slider*>(l->getParentComponent())) return; text = l->getText(); }
                else if (auto* b = dynamic_cast<juce::TextButton*>(&c)) text = b->getButtonText();
                else if (auto* tb = dynamic_cast<juce::ToggleButton*>(&c)) text = tb->getButtonText();
                else if (auto* cb = dynamic_cast<juce::ComboBox*>(&c)) text = cb->getText();
                else if (dynamic_cast<ghosty::BypassButton*>(&c) || dynamic_cast<ghosty::SyncButton*>(&c)) text = static_cast<juce::Button&>(c).getButtonText();   // the shared buttons, themed via setTheme
                if (text.trim().isEmpty()) return;
                const auto r = root.getLocalArea(&c, c.getLocalBounds()); juce::Colour bg; const double k = pxContrast(img, r, bg); ++checked;
                if (k < worst) { worst = k; worstWhat = where + ": \"" + text.substring(0, 24) + "\""; }
                if (k < 4.5) { ++failed; if (failed <= 40) std::printf("    LOW %.2f:1  %s \"%s\" on #%06x\n", k, where.toRawUTF8(), text.substring(0, 30).toRawUTF8(), (unsigned) (bg.getARGB() & 0xffffff)); }
            };
            walk(root);
        };
        for (int pg = 0; pg < 10; ++pg) { be->showPageForTest(pg); scan(*e, pages[pg]); }
        p.focusMode = true; be->showPageForTest(0); scan(*e, "focus"); p.focusMode = false; be->showPageForTest(0);
        e->setSize(1600, 1000); scan(*e, "large grid"); be->showPageForTest(4); scan(*e, "large lab"); e->setSize(1180, 790); be->showPageForTest(0);
        be->startRecreateForTest(wavf); for (int i = 0; i < 400 && be->recreateBusyForTest(); ++i) { juce::Thread::sleep(10); be->recreateTickForTest(); } scan(*be->recreatePanelForTest(), "re-create panel"); be->closeRecreateForTest();
        bx::Ed ed(p); bx::StepEditor se(ed, 0, 0, nullptr); se.setSize(se.getWidth(), se.getHeight()); scan(se, "step editor");
        p.undo();
        std::printf("  %-6s: %d text controls checked, worst %.2f:1 (%s), %d under 4.5:1; near-black area at most %.2f%%\n", ghosty::themes::displayName(t).toRawUTF8(), checked, worst, worstWhat.toRawUTF8(), failed, 100 * blackWorst);
        CHECK(failed == 0 && checked > 200 && (t == ghosty::themes::Theme::Dark || blackWorst < 0.001), "%s: text >= 4.5:1 everywhere, nothing blacked out", ghosty::themes::displayName(t).toRawUTF8());
    }
    e.reset(); wavf.deleteFile();
}


// SAMPLE REGION (Shared/WaveRegion.h) on the selected track's sound + SLICE drag-out
static void testRegion() {
    section("SAMPLE REGION: START / END / LOOP + WAV / slice drag-out");
    BeatsXProcessor p; p.applyKit(0, true); for (auto& pt : p.model.patterns) pt.clear();
    setParam(p, "songmode", 0); setParam(p, "verb", 0); setParam(p, "echo", 0); setParam(p, "glue", 0); setParam(p, "bpm", 120);
    auto& tr = p.model.tracks[0]; tr.sample = makeSound("tone", 48000, [](int i) { return 0.5f * (float) std::sin(i * 0.05); }); tr.sound = "tone"; tr.vol = 1; tr.decay = 1;
    p.model.patterns[0].length = 16; p.model.patterns[0].steps[0][0].vel = 127; p.publish();
    auto dur = [&](double sec) { auto spec = p.makeSpec({ false, false, -1 }); auto b = render(*spec.snap, spec.P, sec); int last = -1; for (int i = 0; i < b.getNumSamples(); ++i) if (std::abs(b.getSample(0, i)) > 1e-3f) last = i; return std::make_pair(last / 48000.0, b); };
    const auto [d0, b0] = dur(1.9);
    tr.regStart = 0; tr.regEnd = 1; tr.loopStart = 0; tr.loopEnd = 1; tr.loopOn = false; p.publish(); const auto [d0b, b0b] = dur(1.9);
    double md = 0; for (int i = 0; i < b0.getNumSamples(); ++i) md = std::max(md, (double) std::abs(b0.getSample(0, i) - b0b.getSample(0, i)));
    tr.regStart = 0.5f; tr.regEnd = 0.75f; p.publish(); const auto [d1, b1] = dur(1.9);
    tr.loopOn = true; tr.loopStart = 0.55f; tr.loopEnd = 0.65f; p.publish(); const auto [d2, b2] = dur(1.9);
    double eLate = 0; for (int i = (int) (1.0 * 48000); i < (int) (1.9 * 48000); ++i) eLate += b2.getSample(0, i) * b2.getSample(0, i);
    std::printf("  1 s sound, one hit per 2 s bar: whole %.3f s (defaults set explicitly: max diff %.1e) | START 0.5 END 0.75: %.3f s | + LOOP 0.55-0.65: sounding until %.3f s (energy 1.0-1.9 s %.1f)\n", d0, md, d1, d2, eLate);
    CHECK(md == 0.0 && std::abs(d0 - 1.0) < 0.02 && std::abs(d1 - 0.25) < 0.02 && d2 > 1.85 && eLate > 1.0, "region START / END / LOOP play as set");
    // state: saved + reloaded; an untouched track writes nothing new (old projects save the same bytes)
    juce::MemoryBlock mb; p.getStateInformation(mb); BeatsXProcessor q; q.restoreState(mb.getData(), (int) mb.getSize()); const auto& qt = q.model.tracks[0];
    BeatsXProcessor fresh; fresh.applyKit(0, true); const auto tree = gb::modelToTree(fresh.model); bool anyRegionProp = false; for (int i = 0; i < tree.getNumChildren(); ++i) if (tree.getChild(i).hasProperty("rstart") || tree.getChild(i).hasProperty("loop")) anyRegionProp = true;
    std::printf("  reloaded: start %.2f end %.2f loop %d %.2f-%.2f; an untouched kit writes region properties: %d\n", qt.regStart, qt.regEnd, (int) qt.loopOn, qt.loopStart, qt.loopEnd, (int) anyRegionProp);
    CHECK(std::abs(qt.regStart - 0.5f) < 1e-6f && std::abs(qt.regEnd - 0.75f) < 1e-6f && qt.loopOn && std::abs(qt.loopStart - 0.55f) < 1e-6f && !anyRegionProp && fresh.model.tracks[0].regionIsDefault(), "region state round trip, old state unchanged");
    // the editor: drag the START flag, right-click = START, right-drag = LOOP; one undo step per gesture
    tr.regStart = 0; tr.regEnd = 1; tr.loopOn = false; tr.loopStart = 0; tr.loopEnd = 1; p.publish();
    std::unique_ptr<juce::AudioProcessorEditor> e(p.createEditor()); auto* be = dynamic_cast<BeatsXEditor*>(e.get()); be->showPageForTest(0); be->showSlicesForTest(false);
    auto& w = *be->regionForTest(); const auto wa = w.waveArea(); const float y = wa.getY() + wa.getHeight() * 0.4f;
    auto ev = [&](float x, float yy, bool right, float dx, int clicks = 1) { return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), { x + dx, yy }, right ? juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier) : juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &w, &w, juce::Time::getCurrentTime(), { x, yy }, juce::Time::getCurrentTime(), clicks, std::abs(dx) > 0.0f); };
    auto gesture = [&](float x0, float x1, float yy, bool right) { w.mouseDown(ev(x0, yy, right, 0)); if (std::abs(x1 - x0) > 0) w.mouseDrag(ev(x0, yy, right, x1 - x0)); w.mouseUp(ev(x0, yy, right, x1 - x0)); juce::Thread::sleep(550); };
    const int u0 = p.undoDepth();
    gesture(w.xOf(0.0f) + 1, w.xOf(0.3f), y, false); const float s1 = tr.regStart; const int u1 = p.undoDepth();
    gesture(w.xOf(0.2f), w.xOf(0.2f), y, true); const float s2 = tr.regStart; const int u2 = p.undoDepth();
    gesture(w.xOf(0.4f), w.xOf(0.6f), y, true); const bool lp = tr.loopOn; const float la = tr.loopStart, lb = tr.loopEnd; const int u3 = p.undoDepth();
    std::printf("  editor: drag START flag -> %.2f (+%d undo) | right-click -> START %.2f (+%d) | right-drag -> LOOP %d %.2f-%.2f (+%d)\n", s1, u1 - u0, s2, u2 - u1, (int) lp, la, lb, u3 - u2);
    CHECK(std::abs(s1 - 0.3f) < 0.03f && u1 - u0 == 1 && std::abs(s2 - 0.2f) < 0.03f && u2 - u1 == 1 && lp && std::abs(la - 0.4f) < 0.03f && std::abs(lb - 0.6f) < 0.03f && u3 - u2 == 1, "region editor wiring");
    // WAV grip + slice drag-out: refused in demo, files when unlocked
    juce::StringArray dragged; w.performDrag = [&](const juce::StringArray& f) { dragged = f; };
    p.demoForTest = true; const bool gateDemo = w.canExport && w.canExport(); const bool fileDemo = p.exportSoundPart(0, 0.2f, 0.4f, "x").existsAsFile();
    p.demoForTest = false; const auto f1 = p.exportSoundPart(0, 0.2f, 0.4f, "test"); juce::int64 frames = 0; { juce::AudioFormatManager fm; fm.registerBasicFormats(); if (auto r = std::unique_ptr<juce::AudioFormatReader>(fm.createReaderFor(f1))) frames = r->lengthInSamples; } f1.deleteFile();
    tr.slices = { 0, 12000, 24000, 36000 }; p.publish(); be->showSlicesForTest(true); auto* sv = be->slicesForTest(); sv->setSize(240, 80); juce::StringArray sd; sv->performDrag = [&](const juce::StringArray& f) { sd = f; };
    auto sev = [&](float x, float dy) { return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), { x, 40 + dy }, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, sv, sv, juce::Time::getCurrentTime(), { x, 40 }, juce::Time::getCurrentTime(), 1, dy != 0); };
    p.demoForTest = true; sv->mouseDown(sev(150, 0)); sv->mouseDrag(sev(150, 30)); sv->mouseUp(sev(150, 30)); const bool sliceDemo = !sd.isEmpty();
    p.demoForTest = false; const auto nSl = tr.slices.size(); sv->mouseDown(sev(150, 0)); sv->mouseDrag(sev(150, 30)); sv->mouseUp(sev(150, 30)); const bool sliceOk = sd.size() == 1 && juce::File(sd[0]).existsAsFile();
    juce::int64 sFrames = 0; if (sliceOk) { juce::AudioFormatManager fm; fm.registerBasicFormats(); if (auto r = std::unique_ptr<juce::AudioFormatReader>(fm.createReaderFor(juce::File(sd[0])))) sFrames = r->lengthInSamples; juce::File(sd[0]).deleteFile(); }
    std::printf("  demo: grip allowed %d, region file %d, slice drag %d | unlocked: region WAV %lld frames (want 9600), slice 3 dragged %d (%lld frames, want 12000), no line added by the drag %d\n", (int) gateDemo, (int) fileDemo, (int) sliceDemo, (long long) frames, (int) sliceOk, (long long) sFrames, (int) (tr.slices.size() == nSl));
    CHECK(!gateDemo && !fileDemo && !sliceDemo && frames == 9600 && sliceOk && sFrames == 12000 && tr.slices.size() == nSl, "drag-out gate + region / slice WAVs");
    e.reset();
}


// REGION loop crossfade, tracker Fxx tempo + E6x pattern loop, XM / IT import + export (round trip, gate, fuzz)
static void testTrackerExtras() {
    section("REGION loop crossfade; tracker F20+ tempo, E6x loop; XM / IT files");
    {   // a loop whose ends don't match (a sine cut mid-cycle): the wrap must not jump
        BeatsXProcessor p; p.applyKit(0, true); for (auto& pt : p.model.patterns) pt.clear(); setParam(p, "songmode", 0); setParam(p, "verb", 0); setParam(p, "echo", 0); setParam(p, "glue", 0); setParam(p, "bpm", 120); setParam(p, "limiter", 2);
        auto& tr = p.model.tracks[0]; tr.sample = makeSound("tone", 48000, [](int i) { return 0.5f * (float) std::sin(i * 0.05); }); tr.sound = "tone"; tr.vol = 1;
        p.model.patterns[0].length = 16; p.model.patterns[0].steps[0][0].vel = 127; tr.regStart = 0.1f; tr.regEnd = 0.9f; tr.loopOn = true; tr.loopStart = 0.5f; tr.loopEnd = 0.5f + 1030.0f / 48000.0f;
        auto measure = [&](bool rev, double& eLate) { tr.reverse = rev; p.publish(); auto spec = p.makeSpec({ false, false, -1 }); auto b = render(*spec.snap, spec.P, 1.5);
            float step = 0, music = 0; eLate = 0;
            for (int i = 2000; i < b.getNumSamples(); ++i) { const float d = std::abs(b.getSample(0, i) - b.getSample(0, i - 1)); if (i < 48000 * 0.3) music = std::max(music, d); else step = std::max(step, d); }
            for (int i = (int) (1.0 * 48000); i < (int) (1.4 * 48000); ++i) eLate += b.getSample(0, i) * b.getSample(0, i);
            return std::make_pair(step, music); };
        double e1 = 0, e2 = 0; const auto [stepF, musicF] = measure(false, e1); const auto [stepR, musicR] = measure(true, e2);
        // the same wrap without a crossfade would jump by about 2 * 0.5 * |sin(a) - sin(b)| over one sample
        const float noXf = 0.5f * (float) std::abs(std::sin(0.05 * (0.5 * 48000 + 1030)) - std::sin(0.05 * (0.5 * 48000)));
        std::printf("  loop wrap, forwards: largest sample step while looping %.4f vs %.4f in the plain tone (a hard wrap would jump ~%.3f); reversed: %.4f vs %.4f; energy 1.0-1.4 s fwd %.0f rev %.0f\n", stepF, musicF, noXf, stepR, musicR, e1, e2);
        CHECK(stepF <= musicF * 1.25f + 1e-4f && stepR <= musicR * 1.25f + 1e-4f && e1 > 100 && e2 > 100, "REGION loop: crossfaded wrap, reversed tracks loop too");
    }
    {   // E6x pattern loop: E60 at row 2, E62 at row 4 -> rows 2..4 play three times in all
        gb::mel::Pattern pt; pt.rows = 8; pt.cells[0][2] = { 0, 0, gb::mel::kNoVol, gb::mel::FxFine, 0x60 }; pt.cells[0][4] = { 0, 0, gb::mel::kNoVol, gb::mel::FxFine, 0x62 };
        const auto rows = gb::mel::loopRows(pt); juce::String seq; for (auto r : rows) seq << (int) r << " ";
        gb::mel::Song s; s.on = true; s.pat(0) = pt; s.prepare(); gb::mel::TickPos tp; const bool okT = gb::mel::tickAt(s, nullptr, false, 0, 5 * s.rowBeats() + 1e-6, tp);
        gb::mel::Song o = s; o.ownOrder = true; o.order = { 0 }; o.prepare();
        std::printf("  E6x: play order %s(%d rows); row at beat 5 = %d; OWN ORDER pass = %ld rows\n", seq.toRawUTF8(), (int) rows.size(), tp.row, o.timeline ? o.timeline->total : -1);
        CHECK(rows.size() == 14 && rows[5] == 2 && rows[8] == 2 && rows[11] == 5 && okT && tp.row == 2 && o.timeline && o.timeline->total == 14, "E6x pattern loop (ProTracker semantics)");
    }
    {   // F20..FF tempo: SYNC OWN -> Beats' tempo follows; SYNC DAW -> a marker only
        auto run = [&](bool own) {
            BeatsXProcessor p; p.applyKit(0, true); setParam(p, "songmode", 0); FakeHead ph; ph.bpm = 120; p.setPlayHead(&ph); p.prepareToPlay(48000, 512);
            p.melody.on = true; p.melody.lpb = 4; gb::mel::takeTrackSound(p.melody.inst[0], p.model.tracks[0], 0); p.melody.pat(0).rows = 16; p.melody.pat(0).cells[0][4] = { 61, 1, gb::mel::kNoVol, gb::mel::FxSpeed, 0x8C }; p.publish();
            if (own) { p.sync.setMode(ghosty::SyncTransport::Mode::Own); p.sync.setOwnBpm(120); p.sync.startOwn(0.0); }
            juce::AudioBuffer<float> b(2, 512); juce::MidiBuffer m; for (int i = 0; i < 60; ++i) { ph.ppq += 512 * 120 / 60.0 / 48000; b.clear(); p.processBlock(b, m); }
            p.setPlayHead(nullptr); return p.sync.getOwnBpm(); };
        const double own = run(true), daw = run(false);
        std::printf("  F8C (140 BPM) on row 4: SYNC OWN -> own tempo %.1f; SYNC DAW -> own tempo stays %.1f (the DAW owns the tempo)\n", own, daw);
        CHECK(own == 140.0 && daw == 120.0, "F20+ tempo command");
    }
    // XM / IT round trip
    gb::mel::Song s; s.on = true; s.speed = 6; s.lpb = 4;
    auto& in = s.inst[0]; in.name = "Test Bass"; in.sample = makeSound("sine", 24000, [](int i) { return 0.6f * (float) std::sin(i * 0.02); }); in.root = 48; in.loop = true; in.loopStart = 0.25f; in.loopEnd = 0.75f;
    auto& in2 = s.inst[1]; in2.name = "Bell"; in2.sample = makeSound("bell", 12000, [](int i) { return 0.4f * (float) std::sin(i * 0.11) * std::exp(-i / 4000.0f); }); in2.root = 72;
    using C = gb::mel::Cell; namespace F = gb::mel;
    const std::vector<C> fxCells { { 49, 1, 40, F::FxArp, 0x37 }, { 50, 1, gb::mel::kNoVol, F::FxSlideUp, 0x10 }, { 51, 2, 64, F::FxSlideDown, 0x08 }, { 52, 1, gb::mel::kNoVol, F::FxGlide, 0x20 },
        { 53, 1, 10, F::FxVibrato, 0x44 }, { 54, 2, gb::mel::kNoVol, F::FxTremolo, 0x23 }, { 55, 1, gb::mel::kNoVol, F::FxPan, 0xC0 }, { 56, 1, gb::mel::kNoVol, F::FxVolSlide, 0x04 },
        { 57, 1, gb::mel::kNoVol, F::FxCut, 0x03 }, { 58, 1, gb::mel::kNoVol, F::FxDelay, 0x02 }, { 59, 1, gb::mel::kNoVol, F::FxFine, 0x13 }, { 60, 1, gb::mel::kNoVol, F::FxFine, 0x24 },
        { 61, 1, gb::mel::kNoVol, F::FxFine, 0xA5 }, { 62, 1, gb::mel::kNoVol, F::FxFine, 0xB6 }, { 0, 0, gb::mel::kNoVol, F::FxFine, 0x60 }, { 0, 0, gb::mel::kNoVol, F::FxFine, 0x61 },
        { 63, 1, gb::mel::kNoVol, F::FxSpeed, 0x04 }, { 64, 1, gb::mel::kNoVol, F::FxSpeed, 0x8C }, { 65, 1, gb::mel::kNoVol, F::FxRetrig, 0x93 }, { 66, 1, gb::mel::kNoVol, F::FxPanSlide, 0x20 },
        { gb::mel::kNoteOff, 0, gb::mel::kNoVol, 0, 0 }, { 70, 2, 32, 0, 0 } };
    auto& A = s.pat(0); A.rows = 64; for (size_t k = 0; k < fxCells.size(); ++k) A.cells[k % 8][(size_t) k * 2 + 1] = fxCells[k];
    auto& B = s.pat(1); B.rows = 64; B.cells[3][10] = { 40, 2, 20, F::FxJump, 0x00 };
    auto soundOf = [](const gb::mel::Instrument& i) { return i.sample; };
    auto compare = [&](const gb::mel::Song& b, int& diffCells, juce::String& firstDiff) {
        diffCells = 0; for (int p = 0; p < 2; ++p) for (int c = 0; c < 8; ++c) for (int r = 0; r < 64; ++r) { const auto& x = s.pat(p).cells[(size_t) c][(size_t) r]; const auto& y = b.pat(p).cells[(size_t) c][(size_t) r];
            const bool same = x.note == y.note && x.inst == y.inst && x.vol == y.vol && x.fx == y.fx && x.param == y.param;
            if (!same && !(p == 0 && c == 0 && r == 0 && x.empty() && y.fx == F::FxSpeed && y.param == 125)) { ++diffCells; if (firstDiff.isEmpty()) firstDiff << "p" << p << " c" << c << " r" << r << ": " << (int) x.note << "/" << (int) x.fx << ":" << (int) x.param << " -> " << (int) y.note << "/" << (int) y.fx << ":" << (int) y.param; } } };
    for (bool it : { false, true }) {
        gb::mel::files::Report wr; const auto data = it ? gb::mel::files::writeIT(s, soundOf, "rt", 125, wr) : gb::mel::files::writeXM(s, soundOf, "rt", 125, wr);
        gb::mel::Song b; gb::mel::files::Report rr; const bool ok = it ? gb::mel::files::readIT(data.getData(), data.getSize(), b, rr) : gb::mel::files::readXM(data.getData(), data.getSize(), b, rr);
        int diff = 0; juce::String fd; compare(b, diff, fd);
        const auto& bi = b.inst[0]; const double pitchRatio = bi.sample ? (bi.sample->rate * std::pow(2.0, (48 - bi.root) / 12.0)) / (in.sample->rate * std::pow(2.0, (48 - in.root) / 12.0)) : 0;
        const bool samplesOk = bi.sample && bi.sample->data.size() == in.sample->data.size() && std::abs(bi.sample->data[1000] - in.sample->data[1000]) < 1e-3f && bi.loop && std::abs(bi.loopStart - 0.25f) < 1e-3f && std::abs(bi.loopEnd - 0.75f) < 1e-3f && b.inst[1].sample != nullptr;
        std::printf("  %s: %d bytes, %d patterns, %d samples, %d effects dropped | read back %d: %d cells differ%s%s; instrument \"%s\", pitch ratio %.5f, samples + loop equal %d; order %d entries\n", it ? "IT" : "XM", (int) data.getSize(), wr.patterns, wr.samples, wr.droppedFx,
                    (int) ok, diff, fd.isEmpty() ? "" : " (first: ", fd.isEmpty() ? "" : (fd + ")").toRawUTF8(), bi.name.toRawUTF8(), pitchRatio, (int) samplesOk, (int) b.order.size());
        CHECK(ok && diff == 0 && std::abs(pitchRatio - 1.0) < 2e-3 && samplesOk && bi.name == "Test Bass" && wr.droppedFx == 0, "%s round trip", it ? "IT" : "XM");
        // robustness: truncated, corrupted, random
        juce::Random R(it ? 99 : 7); int okCount = 0, err = 0;
        for (int k = 0; k < 200; ++k) {
            juce::MemoryBlock m2(data); auto* bytes = static_cast<uint8_t*>(m2.getData());
            const int kind = k % 4;
            if (kind == 0) m2.setSize((size_t) R.nextInt((int) m2.getSize()));
            else if (kind == 1) for (int f = 0; f < 1 + R.nextInt(30); ++f) bytes[R.nextInt((int) m2.getSize())] = (uint8_t) R.nextInt(256);
            else if (kind == 2) { for (int f = 0; f < 8; ++f) { const int at = 16 + R.nextInt((int) m2.getSize() - 20); bytes[at] = 0xFF; bytes[at + 1] = 0xFF; bytes[at + 2] = 0xFF; bytes[at + 3] = 0x7F; } }
            else { for (size_t f = 4; f < m2.getSize(); ++f) bytes[f] = (uint8_t) R.nextInt(256); }
            gb::mel::Song z; gb::mel::files::Report zr; const bool zok = it ? gb::mel::files::readIT(m2.getData(), m2.getSize(), z, zr) : gb::mel::files::readXM(m2.getData(), m2.getSize(), z, zr);
            if (zok) { ++okCount; z.prepare(); } else ++err;
        }
        gb::mel::Song z; gb::mel::files::Report zr; const bool emptyOk = it ? gb::mel::files::readIT(nullptr, 0, z, zr) : gb::mel::files::readXM(nullptr, 0, z, zr);
        std::printf("  %s fuzz: 200 truncated / corrupted / random files: %d read (partially), %d refused, 0 crashes; empty file refused %d\n", it ? "IT" : "XM", okCount, err, (int) !emptyOk);
        CHECK(okCount + err == 200 && !emptyOk, "%s malformed files never crash", it ? "IT" : "XM");
    }
    {   // the processor: export gate, import (always), one undo step in the editor
        BeatsXProcessor p; p.melody = s; p.publish(); const auto f = juce::File::createTempFile(".xm"); juce::String msg;
        p.demoForTest = true; const bool demoExp = p.exportTracker(f, false, msg); p.demoForTest = false; const bool exp = p.exportTracker(f, false, msg);
        BeatsXProcessor q; q.demoForTest = true; std::unique_ptr<juce::AudioProcessorEditor> e(q.createEditor()); auto* be = dynamic_cast<BeatsXEditor*>(e.get()); const int u0 = q.undoDepth();
        be->importModuleForTest(f); const bool imported = q.melody.on && q.melody.inst[0].name == "Test Bass" && q.melody.pat(1).cells[3][10].note == 40; const int u1 = q.undoDepth(); q.undo(); const bool undone = q.melody.inst[0].name.isEmpty();
        std::printf("  export in demo %d, unlocked %d (%s) | import into a demo copy %d, +%d undo, UNDO restores %d\n", (int) demoExp, (int) exp, msg.toRawUTF8(), (int) imported, u1 - u0, (int) undone);
        CHECK(!demoExp && exp && imported && u1 - u0 == 1 && undone, "XM / IT: gate on export, import always, one undo step");
        e.reset(); f.deleteFile();
    }
}

static void shots(const juce::File& dir) {
    dir.createDirectory();
    BeatsXProcessor p; p.applyKit(10, true);
    std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
    auto* be = dynamic_cast<BeatsXEditor*>(ed.get());
    for (auto [page, name] : std::vector<std::pair<int, const char*>> { { 0, "grid" }, { 1, "sounds" }, { 2, "process" }, { 3, "tracker" }, { 4, "breed" }, { 5, "pads" }, { 6, "song" } }) {
        if (page == 0) { auto& P = p.model.patterns[0]; auto& a = P.steps[4][14]; a.roll = 4; a.rate = 16; a.pitchRamp = 7; P.steps[2][7].vel = 70; P.steps[2][7].nudge = 25; P.steps[2][15].vel = 40; P.steps[2][15].prob = 50; P.steps[6][9].flam = 15; P.trackLen[7] = 12; P.steps[7][3].vel = 90; P.steps[7][9].vel = 90; p.publish(); }
        if (page == 2) { p.model.tracks[0].layers[0].sound = "Sub Kick Floor"; p.model.tracks[0].layers[0].level = 0.6f; p.model.tracks[0].rules[0] = { gb::RuleQuieterAfterLoud, 0, 2, 30 }; p.publish(); }
        if (page == 3) { if (auto* pp = p.apvts.getParameter("pattern")) pp->setValueNotifyingHost(pp->convertTo0to1(3.0f)); p.model.toolSeed = 5; p.generatePattern(1); }
        if (page == 4) { p.model.patterns[2].evolve = 0.4f; p.breedPool.clear(); p.breedNext(16); p.breedStar[2] = p.breedStar[7] = true; }
        if (be) be->showPageForTest(page);
        for (int i = 0; i < 3; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
        auto img = ed->createComponentSnapshot(ed->getLocalBounds());
        juce::PNGImageFormat png; auto f = dir.getChildFile(juce::String("beats-") + name + ".png"); f.deleteFile();
        if (auto os = std::unique_ptr<juce::FileOutputStream>(f.createOutputStream())) png.writeImageToStream(img, *os);
        std::printf("  %s\n", f.getFullPathName().toRawUTF8());
    }
    auto snap = [&](const juce::String& name) { for (int i = 0; i < 3; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(30);
        auto img = ed->createComponentSnapshot(ed->getLocalBounds()); juce::PNGImageFormat png; auto f = dir.getChildFile("beats-" + name + ".png"); f.deleteFile();
        if (auto os = std::unique_ptr<juce::FileOutputStream>(f.createOutputStream())) png.writeImageToStream(img, *os); std::printf("  %s\n", f.getFullPathName().toRawUTF8()); };
    // SCRIPTS page with two modules (one with an error being edited)
    p.model.scripts = { { "Turnaround rolls", gb::script::examples()[0].code, true }, { "Ghost answers", gb::script::examples()[1].code, false } }; p.publish();
    if (be) { be->showPageForTest(7); be->setScriptTextForTest(juce::String(gb::script::examples()[0].code) + "if bar % 2 = 1 then hit(30)\n"); } snap("scripts");
    // the LARGE layout at real window sizes
    p.model.motifs[0].track = 4; p.model.motifs[0].start = 0; p.model.motifs[0].len = 4; p.model.locks[0] = { 0, 2, 0xffff }; p.publish();
    for (auto sz : { std::pair<int, int> { 1280, 800 }, { 1600, 1000 }, { 1920, 1200 } }) {
        ed->setSize(sz.first, sz.second); if (be) be->showPageForTest(0); snap("large-" + juce::String(sz.first) + "x" + juce::String(sz.second));
    }
    if (be) be->showPageForTest(2); snap("large-process-1600"); ed->setSize(1600, 1000); if (be) be->showPageForTest(1); snap("large-sounds-1600"); if (be) be->showPageForTest(4); snap("large-lab-1600");
    ed->setSize(1180, 790); if (be) be->showPageForTest(0); snap("compact-again");
    // MIX page with meters live (a few blocks of audio through the processor first)
    { p.prepareToPlay(48000, 512); juce::AudioBuffer<float> b(2, 512); juce::MidiBuffer none; p.engine.internalPlay = true; for (int i = 0; i < 40; ++i) p.processBlock(b, none); p.engine.internalPlay = false; }
    if (be) be->showPageForTest(8); snap("mix");
    // FOCUS: just the beat
    p.focusMode = true; if (be) be->showPageForTest(0); snap("focus"); ed->setSize(1600, 1000); snap("focus-large-1600"); ed->setSize(1180, 790);
    p.focusMode = false; if (be) be->showPageForTest(0);
    // LINEAR (RHYTHM LAB) + MELODY tab
    p.model.linear.on = true; p.model.linear.rule = gb::linear::RuleAccents; p.model.linear.lock = 1u << 1; p.publish(); if (be) be->showPageForTest(4); snap("linear");
    { p.melody.on = true; auto& I0 = p.melody.inst[0]; I0.sound = "808 Long"; I0.name = "808 Bass"; I0.route = 0; I0.root = 33; auto& I1 = p.melody.inst[1]; I1.sound = "Metal Bell"; I1.name = "Bell Pluck"; I1.route = 8; I1.root = 81;
      auto& I2 = p.melody.inst[2]; I2.sound = "Metal Chime"; I2.name = "Chime Lead"; I2.route = 11; I2.root = 93; auto& P0 = p.melody.pat(0); P0.rows = 32;
      const int bass[] { 37, 37, 40, 42, 37, 37, 44, 42 }; for (int i = 0; i < 8; ++i) { P0.cells[0][(size_t) (i * 4)] = { (uint8_t) bass[i], 1, gb::mel::kNoVol, 0, 0 }; P0.cells[0][(size_t) (i * 4 + 3)] = { gb::mel::kNoteOff, 0, gb::mel::kNoVol, 0, 0 }; }
      for (int r = 0; r < 32; r += 2) P0.cells[1][(size_t) r] = { (uint8_t) (61 + (r / 8) % 3 * 2), 2, (uint8_t) (0x30 - (r % 8) * 2), gb::mel::FxArp, 0x37 };
      P0.cells[2][0] = { 73, 3, gb::mel::kNoVol, gb::mel::FxVibrato, 0x43 }; P0.cells[2][16] = { 76, 3, gb::mel::kNoVol, gb::mel::FxGlide, 0x08 }; p.publish(); }
    if (auto* pp = p.apvts.getParameter("pattern")) pp->setValueNotifyingHost(0.0f);   // the melody follows the drum pattern: show A, where the notes are
    if (be) be->showPageForTest(9); snap("melody"); ed->setSize(1600, 1000); snap("melody-large-1600"); ed->setSize(1180, 790);
    // BYPASSED (the button top-right), light + dark, focus + large
    setParam(p, "bypass", 1.0f); if (be) be->showPageForTest(0); snap("bypassed"); p.focusMode = true; if (be) be->showPageForTest(0); snap("bypassed-focus"); p.focusMode = false;
    ed->setSize(1600, 1000); if (be) be->showPageForTest(0); snap("bypassed-large-1600"); ed->setSize(1180, 790);
    if (be) { be->setThemeForTest(true); be->showPageForTest(0); } snap("dark-bypassed"); if (be) be->setThemeForTest(false); setParam(p, "bypass", 0.0f);
    // PLAY: ON NOTES (the switch under BYPASS), light + dark, compact + large
    p.playMode.store(1); if (be) be->showPageForTest(0); snap("play-onnotes"); ed->setSize(1600, 1000); snap("play-onnotes-large-1600"); ed->setSize(1180, 790);
    if (be) { be->setThemeForTest(true); be->showPageForTest(0); } snap("dark-play-onnotes"); if (be) be->setThemeForTest(false); p.playMode.store(0);
    // SYNC: DAW (tempo read-only, from the DAW) and OWN (own tempo + TAP + PLAY), light + dark, compact + large + FOCUS
    p.hostBpm.store(128.0); if (be) { be->syncUiForTest(); be->showPageForTest(0); } snap("sync-daw"); if (be) be->setThemeForTest(true); snap("dark-sync-daw"); if (be) be->setThemeForTest(false);
    p.sync.setMode(ghosty::SyncTransport::Mode::Own); p.sync.setOwnBpm(92); if (be) be->syncUiForTest(); snap("sync-own"); p.focusMode = true; if (be) be->showPageForTest(0); snap("sync-own-focus"); p.focusMode = false; if (be) be->showPageForTest(0);
    ed->setSize(1600, 1000); snap("sync-own-large-1600"); ed->setSize(1180, 790); if (be) be->setThemeForTest(true); snap("dark-sync-own"); if (be) be->setThemeForTest(false);
    p.sync.setMode(ghosty::SyncTransport::Mode::Daw); p.sync.setOwnBpm(120); if (be) be->syncUiForTest();
    // RE-CREATE AS MIDI: the panel after a drop, light + dark
    { std::vector<a2mt::Expect> ex; auto L = a2mt::beat(96, 2, 50, 0, ex); auto f = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("ghosty shot loop 96bpm.wav"); f.deleteFile();
      juce::WavAudioFormat wav; juce::AudioBuffer<float> b(1, (int) L.x.size()); b.copyFrom(0, 0, L.x.data(), (int) L.x.size());
      if (auto os = std::unique_ptr<juce::OutputStream>(f.createOutputStream())) if (auto* w = wav.createWriterFor(os.get(), L.sr, 1, 24, {}, 0)) { os.release(); std::unique_ptr<juce::AudioFormatWriter> wr(w); wr->writeFromAudioSampleBuffer(b, 0, b.getNumSamples()); }
      if (be) { be->showPageForTest(0); be->startRecreateForTest(f); for (int i = 0; i < 400 && be->recreateBusyForTest(); ++i) { juce::Thread::sleep(10); be->recreateTickForTest(); } }
      snap("recreate"); if (be) be->setThemeForTest(true); snap("dark-recreate"); if (be) be->setThemeForTest(false); f.deleteFile();
      if (be) { const int px = 300, py = 300; be->fileDragMove({ f.getFullPathName() }, px, py); } snap("recreate-drop-hint"); if (be) be->fileDragExit({}); }
    // the three house themes on every page (+ FOCUS, the large layout, the RE-CREATE panel)
    { const char* names[] = { "grid", "sounds", "process", "tracker", "lab", "pads", "song", "scripts", "mix", "melody" };
      if (be) be->closeRecreateForTest();
      for (int ti = 0; ti < 3; ++ti) { if (!be) break; be->setThemeForTest(ti); const auto tn = ghosty::themes::toString(ghosty::themes::fromInt(ti));
          for (int pg = 0; pg < 10; ++pg) { be->showPageForTest(pg); snap("theme-" + tn + "-" + names[pg]); }
          p.focusMode = true; be->showPageForTest(0); snap("theme-" + tn + "-focus"); p.focusMode = false; be->showPageForTest(0);
          ed->setSize(1600, 1000); snap("theme-" + tn + "-large-1600"); ed->setSize(1180, 790); }
      if (be) be->setThemeForTest(false); }
    // DARK theme (the toggle next to FOCUS)
    if (be) { be->setThemeForTest(true); be->showPageForTest(0); } snap("dark-grid"); if (be) be->showPageForTest(8); snap("dark-mix"); if (be) be->showPageForTest(3); snap("dark-tracker"); if (be) be->showPageForTest(7); snap("dark-scripts"); if (be) be->showPageForTest(9); snap("dark-melody");
    p.melody.on = false; p.melody = gb::mel::Song(); p.model.linear = {}; p.publish();
    if (be) { be->setThemeForTest(false); be->showPageForTest(0); } snap("light-again");
    // the soft LIGHT theme on every page (for the owner): compact, then FOCUS and the large layout
    { setParam(p, "bypass", 0.0f); if (auto* pp = p.apvts.getParameter("pattern")) pp->setValueNotifyingHost(0.0f); p.model.linear.on = true; p.publish();
      const char* names[] = { "grid", "sounds", "process", "tracker", "lab", "pads", "song", "scripts", "mix", "melody" };
      for (int pg = 0; pg < 10; ++pg) { if (be) be->showPageForTest(pg); snap(juce::String("light-soft-") + names[pg]); }
      p.focusMode = true; if (be) be->showPageForTest(0); snap("light-soft-focus"); p.focusMode = false; if (be) be->showPageForTest(0);
      ed->setSize(1600, 1000); if (be) be->showPageForTest(0); snap("light-soft-large-1600"); if (be) be->showPageForTest(4); snap("light-soft-lab-1600"); ed->setSize(1180, 790); p.model.linear.on = false; p.publish(); }
    ed.reset();
}

// ---------------------------------------------------------------- LINEAR + MELODY final-gate fixes and the owner fixes (STEP EDITOR, STUTTER)
static juce::MouseEvent mev(juce::Component& c, float x, float y, juce::ModifierKeys mods = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), int clicks = 1) {
    auto src = juce::Desktop::getInstance().getMainMouseSource(); const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent(src, { x, y }, mods, juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation, juce::MouseInputSource::defaultRotation,
                            juce::MouseInputSource::defaultTiltX, juce::MouseInputSource::defaultTiltY, &c, &c, now, { x, y }, now, clicks, false);
}
static bool opaqueImage(juce::Component& c) {
    juce::Image img(juce::Image::ARGB, c.getWidth(), c.getHeight(), true); { juce::Graphics g(img); c.paintEntireComponent(g, true); }
    juce::Image::BitmapData bd(img, juce::Image::BitmapData::readOnly); for (int y = 0; y < img.getHeight(); ++y) for (int x = 0; x < img.getWidth(); ++x) if (bd.getPixelColour(x, y).getAlpha() != 255) return false;
    return true;
}
static void snapComp(juce::Component& c, const juce::File& f) { auto img = c.createComponentSnapshot(c.getLocalBounds(), true, 1.0f); f.deleteFile(); juce::PNGImageFormat png; juce::FileOutputStream o(f); if (o.openedOk()) png.writeImageToStream(img, o); std::printf("  %s\n", f.getFullPathName().toRawUTF8()); }
static void pump(int n = 3) { for (int i = 0; i < n; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil(15); }

static void testGate2() {
    const auto shotDir = juce::File("/root/work/linmel/shots"); shotDir.createDirectory();
    section("OWNER FIX: STEP EDITOR bars (click anywhere = set, drag scrubs, shift fine, right-click types, double-click resets, wheel steps)");
    {
        BeatsXProcessor p; p.applyKit(0, true); bx::Ed ed(p); juce::Component owner;
        auto& pat = p.model.patterns[(size_t) p.currentPattern()]; pat.steps[0][0] = gb::Step(); pat.steps[0][0].vel = 100; pat.steps[1][0].vel = 90; pat.steps[2][4].vel = 80; p.publish();
        bx::StepEditor se(ed, 0, 0, &owner); se.resized();
        auto& vel = *se.sl[0]; auto& pitch = *se.sl[6]; const float w = (float) vel.getWidth();
        auto xAt = [&](float u) { return 1.0f + u * (w - 2.0f); };
        int got[3]; const float us[] { 0.25f, 0.5f, 0.75f }; const int want[] { 32, 64, 95 };
        for (int k = 0; k < 3; ++k) { vel.mouseDown(mev(vel, xAt(us[k]), 10)); vel.mouseUp(mev(vel, xAt(us[k]), 10)); got[k] = pat.steps[0][0].vel; }
        pitch.mouseDown(mev(pitch, xAt(0.5f), 10)); pitch.mouseUp(mev(pitch, xAt(0.5f), 10)); const int centre = pat.steps[0][0].pitch;
        const int u0 = p.undoDepth(); vel.mouseDown(mev(vel, xAt(0.25f), 10)); vel.mouseDrag(mev(vel, xAt(0.5f), 10)); vel.mouseDrag(mev(vel, xAt(0.75f), 10)); vel.mouseUp(mev(vel, xAt(0.75f), 10));
        const int afterDrag = pat.steps[0][0].vel, undoSteps = p.undoDepth() - u0;
        const auto sh = juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier);
        const int before = pat.steps[0][0].vel; vel.mouseDown(mev(vel, xAt(0.2f), 10, sh)); vel.mouseDrag(mev(vel, xAt(0.7f), 10, sh)); vel.mouseUp(mev(vel, xAt(0.7f), 10, sh)); const int fineDelta = pat.steps[0][0].vel - before;
        vel.mouseDoubleClick(mev(vel, xAt(0.1f), 10, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier), 2)); const int reset = pat.steps[0][0].vel;
        vel.mouseDown(mev(vel, xAt(0.5f), 10, juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier))); const bool typer = vel.typer.isVisible(); vel.typer.setText("77", false); vel.commitTyped(); const int typed = pat.steps[0][0].vel;
        juce::MouseWheelDetails wd; wd.deltaX = 0; wd.deltaY = 0.2f; wd.isReversed = false; wd.isSmooth = false; wd.isInertial = false; vel.mouseWheelMove(mev(vel, 10, 10), wd); const int wheeled = pat.steps[0][0].vel;
        bx::StepEditor multi(ed, 0, 0, &owner, { { 0, 0 }, { 1, 0 }, { 2, 4 } }); multi.resized(); auto& mv = *multi.sl[0]; mv.mouseDown(mev(mv, xAt(0.5f), 10)); mv.mouseUp(mev(mv, xAt(0.5f), 10));
        const bool allThree = pat.steps[0][0].vel == 64 && pat.steps[1][0].vel == 64 && pat.steps[2][4].vel == 64;
        bx::applyTheme(false); const bool opLight = opaqueImage(se); bx::applyTheme(true); se.repaint(); const bool opDark = opaqueImage(se);
        snapComp(se, shotDir.getChildFile("step-editor-dark.png")); bx::applyTheme(false); se.repaint(); snapComp(se, shotDir.getChildFile("step-editor-light.png"));
        std::printf("  click at 25/50/75%% of VELOCITY -> %d / %d / %d (want 32 / 64 / 95) | PITCH centre -> %d | click 25%% + drag to 75%% -> %d with %d undo step(s)\n", got[0], got[1], got[2], centre, afterDrag, undoSteps);
        std::printf("  shift-drag half the bar -> %+d (fine: 1/8 speed) | double-click -> %d (default 100) | right-click opens the type-in %d, typed 77 -> %d | wheel -> %d | 3 steps selected -> all 64: %d | opaque light %d dark %d\n",
                    fineDelta, reset, (int) typer, typed, wheeled, (int) allThree, (int) opLight, (int) opDark);
        CHECK(got[0] == 32 && got[1] == 64 && got[2] == 95 && centre == 0 && afterDrag == 95 && undoSteps == 1, "step editor: click sets, drag scrubs, one undo per gesture");
        CHECK(fineDelta >= 6 && fineDelta <= 10 && reset == 100 && typer && typed == 77 && wheeled == 78 && allThree && opLight && opDark, "step editor: fine / reset / type / wheel / multi / opaque");
    }
    section("OWNER FIX: PERFORM > STUTTER strip (press, drag across 1/4 1/8 1/16 1/32, release)");
    {
        const double sr = 48000; const int B = 256;
        BeatsXProcessor p; p.applyKit(kitIndexOf("House"), true); setParam(p, "bpm", 120); setParam(p, "limiter", 2); setParam(p, "songmode", 0); p.prepareToPlay(sr, B); p.engine.internalPlay = true;
        std::unique_ptr<juce::AudioProcessorEditor> e(p.createEditor()); auto* be = dynamic_cast<BeatsXEditor*>(e.get()); e->setVisible(true); pump();
        auto* st = dynamic_cast<bx::StutterStrip*>(be->partForTest("stutter"));
        const double spb = sr * 60.0 / 120.0; juce::AudioBuffer<float> all(2, (int) (sr * 14)), b(2, B); juce::MidiBuffer m; int pos = 0;
        auto blocks = [&](int n) { for (int i = 0; i < n && pos + B <= all.getNumSamples(); ++i) { b.clear(); p.processBlock(b, m); for (int c = 0; c < 2; ++c) all.copyFrom(c, pos, b, c, 0, B); pos += B; } };
        blocks((int) (2 * spb / B));
        const float segW = st->getWidth() / 4.0f; auto cx = [&](int i) { return (i + 0.5f) * segW; };
        std::vector<int> seq; std::vector<double> swBeat; std::vector<int> swSample;
        st->mouseDown(mev(*st, cx(0), 10)); blocks(1); seq.push_back(p.engine.uiStutter.load()); blocks((int) (2.2 * spb / B));
        const double D[] { 0, 1.0, 0.5, 0.25, 0.125 };
        for (int target : { 1, 2, 3, 0 }) {
            const long sw0 = p.engine.stutterSwitches.load(); st->mouseDrag(mev(*st, cx(target), 10));
            for (int k = 0; k < 400 && p.engine.stutterSwitches.load() == sw0; ++k) blocks(1);
            seq.push_back(p.engine.uiStutter.load()); swBeat.push_back(p.engine.stutterSwitchBeat.load()); swSample.push_back((int) std::lround(p.engine.stutterSwitchBeat.load() * spb));
            blocks((int) (2.2 * spb / B));
        }
        // periodicity after each switch: the output repeats every new division exactly
        int periodic = 0; double worstErr = 0; bool aligned = true;
        for (size_t k = 0; k < swSample.size(); ++k) {
            const int d = seq[k + 1]; const int L = (int) std::lround(spb * D[d]); const int a = swSample[k] + 128; double err = 0, mag = 0;
            for (int i = a; i < a + L && i + L < pos; ++i) { err += std::abs(all.getSample(0, i) - all.getSample(0, i + L)); mag += std::abs(all.getSample(0, i)); }
            const double rel = err / juce::jmax(1e-9, mag); worstErr = juce::jmax(worstErr, rel); if (rel < 0.01) ++periodic;
            const double grid = swBeat[k] / D[d]; if (std::abs(grid - std::round(grid)) > 1.0 / (spb * D[d])) aligned = false;
        }
        // clicks: the largest sample step within +-200 samples of a switch vs the music's own largest step
        double musicStep = 0; for (int i = 1; i < (int) (2 * spb); ++i) musicStep = juce::jmax(musicStep, (double) std::abs(all.getSample(0, i) - all.getSample(0, i - 1)));
        double switchStep = 0; for (int sx : swSample) for (int i = juce::jmax(1, sx - 200); i < juce::jmin(pos, sx + 200); ++i) switchStep = juce::jmax(switchStep, (double) std::abs(all.getSample(0, i) - all.getSample(0, i - 1)));
        // release (inside the strip), release outside the strip, the editor closing while held
        st->mouseUp(mev(*st, cx(0), 10)); blocks(1); const bool stopped = p.stutter.load() == 0 && p.engine.uiStutter.load() == 0;
        st->mouseDown(mev(*st, cx(2), 10)); blocks(4); st->mouseUp(mev(*st, -80.0f, 200.0f)); blocks(1); const bool outsideStops = p.stutter.load() == 0 && p.engine.uiStutter.load() == 0;
        // screenshots with a segment lit (light + dark), then bypass mutes it
        st->mouseDown(mev(*st, cx(2), 10)); blocks(2); { auto& ctn = *e; auto img = ctn.createComponentSnapshot(juce::Rectangle<int>(460, ctn.getHeight() - 104, 320, 100)); auto f = shotDir.getChildFile("perform-stutter-light.png"); f.deleteFile(); juce::PNGImageFormat png; juce::FileOutputStream o(f); png.writeImageToStream(img, o); std::printf("  %s\n", f.getFullPathName().toRawUTF8()); }
        be->setThemeForTest(true); pump(); { auto& ctn = *e; auto img = ctn.createComponentSnapshot(juce::Rectangle<int>(460, ctn.getHeight() - 104, 320, 100)); auto f = shotDir.getChildFile("perform-stutter-dark.png"); f.deleteFile(); juce::PNGImageFormat png; juce::FileOutputStream o(f); png.writeImageToStream(img, o); std::printf("  %s\n", f.getFullPathName().toRawUTF8()); }
        be->setThemeForTest(false);
        setParam(p, "bypass", 1.0f); blocks(10); float bypassed = 0; { b.clear(); p.processBlock(b, m); bypassed = b.getMagnitude(0, 0, B); }
        setParam(p, "bypass", 0.0f); blocks(4);
        e.reset(); const bool closeStops = p.stutter.load() == 0;   // (held while the editor closed)
        juce::String sq; for (int v : seq) sq << v << " ";
        std::printf("  divisions while dragging 1/4 -> 1/8 -> 1/16 -> 1/32 -> back to 1/4: %s(want 1 2 3 4 1) | each switch on the new division's grid %d | output repeats every new division after each switch: %d / 4 (worst error %.4f)\n", sq.toRawUTF8(), (int) aligned, periodic, worstErr);
        std::printf("  largest step around the switches %.4f vs the music's own %.4f | release stops %d, release outside the strip stops %d, closing the editor stops %d | bypassed peak %.1e\n", switchStep, musicStep, (int) stopped, (int) outsideStops, (int) closeStops, bypassed);
        CHECK(sq == "1 2 3 4 1 " && aligned && periodic == 4, "stutter: drag switches the division on its grid, the output repeats at the new length");
        CHECK(switchStep <= musicStep * 1.05 && stopped && outsideStops && closeStops && bypassed == 0.0f, "stutter: click-free, never hangs, bypass mutes it");
    }
    section("LINEAR panel: seed kept, PRIORITY drag list, undo per gesture, old projects reset");
    {
        BeatsXProcessor p; p.applyKit(0, true); std::unique_ptr<juce::AudioProcessorEditor> e(p.createEditor()); auto* be = dynamic_cast<BeatsXEditor*>(e.get()); e->setVisible(true); be->showPageForTest(4); pump();
        auto* lp = dynamic_cast<bx::LinearPanel*>(be->partForTest("linear")); auto* lab = dynamic_cast<bx::RhythmLab*>(be->partForTest("lab")); lab->setLinearTab(true); pump();
        p.model.toolSeed = 41; lp->onBtn.onClick(); const int seed1 = p.model.linear.seed; lp->onBtn.onClick(); p.model.toolSeed = 99; p.generatePattern(0); p.publish(); lp->onBtn.onClick(); const int seed2 = p.model.linear.seed;
        lp->varNext.onClick(); lp->varNext.onClick(); lp->varPrev.onClick(); lp->varNext.onClick(); const int seed3 = p.model.linear.seed;
        const int u0 = p.undoDepth(); p.model.linear.variation = 0; lp->varPrev.onClick(); const bool noEmpty = p.undoDepth() == u0;
        // PRIORITY: drag the hat (track 5 in this kit is a hat) to rank 2
        auto roles = p.roles(); int hat = -1; for (int t = 0; t < 16; ++t) if (roles[(size_t) t] == gb::RoleHat) { hat = t; break; }
        auto& ol = lp->orderList; auto ord = ol.order(); int from = 0; for (int r = 0; r < 16; ++r) if (ord[(size_t) r] == hat) from = r;
        auto c1 = ol.row(from).getCentre(), c2 = ol.row(1).getCentre(); ol.mouseDown(mev(ol, c1.x - 40, c1.y)); ol.mouseDrag(mev(ol, c2.x - 40, c2.y)); ol.mouseUp(mev(ol, c2.x - 40, c2.y));
        const auto o2 = gb::linear::resolvedOrder(p.model.linear, p.roles()); std::set<int> ranks(p.model.linear.order.begin(), p.model.linear.order.end());
        const bool kickFirst = roles[(size_t) o2[0]] == gb::RoleKick, hatSecond = o2[1] == hat;
        auto lc = ol.lockChip(3).getCentre(); const uint16_t lock0 = p.model.linear.lock; ol.mouseDown(mev(ol, lc.x, lc.y)); const bool lockToggled = p.model.linear.lock != lock0;
        // STRICTNESS by wheel / typing: one undo step for a burst, none when nothing changed
        const int u1 = p.undoDepth(); lp->strict.setValue(40, juce::sendNotificationSync); lp->strict.setValue(45, juce::sendNotificationSync); const int burst = p.undoDepth() - u1;
        const int u2 = p.undoDepth(); lp->strict.setValue(45, juce::sendNotificationSync); const bool same = p.undoDepth() == u2;
        snapComp(*lab, shotDir.getChildFile("linear-panel-light.png")); be->setThemeForTest(true); pump(); lp->sync(); snapComp(*lab, shotDir.getChildFile("linear-panel-dark.png")); be->setThemeForTest(false);
        e->setSize(1600, 1000); pump(); snapComp(*e, shotDir.getChildFile("linear-panel-large-1600.png")); e->setSize(1180, 790); pump();
        // an old (v1) project resets LINEAR and the melody
        p.model.linear.on = true; p.melody.on = true; juce::ValueTree old("GHOSTYBEATS"); old.setProperty("steps", "10001000|00001000|10101010|0|0|0|0|0", nullptr);
        juce::MemoryBlock mb; if (auto xml = old.createXml()) juce::AudioProcessor::copyXmlToBinary(*xml, mb); p.restoreState(mb.getData(), (int) mb.getSize());
        std::printf("  seed on first switch-on %d, after off / GENERATE (tool seed 99) / on %d, after > > < > %d | VARIATION < at 0 pushes no undo %d | hat dragged to rank 2: kick still first %d, hat second %d, 16 unique ranks %d | lock chip %d\n",
                    seed1, seed2, seed3, (int) noEmpty, (int) kickFirst, (int) hatSecond, (int) ranks.size(), (int) lockToggled);
        std::printf("  STRICTNESS 40 then 45 within a gesture: %d undo step(s), unchanged value: none %d | v1 project: LINEAR off %d, melody empty %d\n", burst, (int) same, (int) !p.model.linear.on, (int) !p.melody.on);
        CHECK(seed1 == 41 && seed2 == 41 && seed3 == 41 && noEmpty && kickFirst && hatSecond && ranks.size() == 16 && lockToggled && burst == 1 && same && !p.model.linear.on && !p.melody.on, "LINEAR panel");
    }
    section("MELODY page: SPACE is the transport, audition never records, stems, kit change, track PITCH, follow, MIDI REC, editing");
    {
        const double sr = 48000; const int B = 512;
        BeatsXProcessor p; p.applyKit(0, true); for (auto& pt : p.model.patterns) pt.clear(); setParam(p, "songmode", 0); setParam(p, "limiter", 2);
        auto sine = makeSound("sine220", 48000 * 3, [](int i) { return 0.5f * (float) std::sin(juce::MathConstants<double>::twoPi * 220.0 * i / 48000.0); });
        p.model.tracks[9].sample = sine; p.model.tracks[9].sound = "sine220"; p.model.tracks[9].vol = 1.0f;
        auto& M = p.melody; M = gb::mel::Song(); M.on = true; gb::mel::takeTrackSound(M.inst[0], p.model.tracks[9], 9); M.inst[0].root = 57; M.inst[0].vol = 1.0f; M.inst[0].loop = true; M.inst[0].loopStart = 0.2f; M.inst[0].loopEnd = 0.9f;
        M.pat(0).rows = 16; M.pat(0).cells[0][0] = { 58, 1, gb::mel::kNoVol, 0, 0 }; p.publish();
        // stems: track 10 has no drum steps, only the melody
        p.prepareToPlay(sr, B); auto stems = p.bounceStems(false, 1); bool melStem = false; for (auto& f : stems) if (juce::File(f).getFileName().startsWith("10 ")) melStem = true;
        // kit change: the instrument keeps its own sound; track PITCH +12 transposes it
        const auto* before = p.engine.snapshotInUse(); juce::ignoreUnused(before); p.applyKit(kitIndexOf("House"), false); p.model.tracks[9].sample = nullptr; p.model.tracks[9].sound = "Hat"; p.publish();
        const bool keeps = M.inst[0].sample == sine;
        p.model.tracks[9].pitch = 12; p.publish(); auto out = runProc(p, sr, B, 0.6); const double f12 = zcFreq(out.getReadPointer(0), 6000, 12000, sr); p.model.tracks[9].pitch = 0; p.publish();
        // audition: pitched (+7 = E-4 on an A-3 root), and never recorded into the drums even with REC on and the transport running
        p.recording.store(true); p.engine.internalPlay = true; M.on = false; p.publish();
        int drumBefore = 0; for (auto& row : p.model.patterns[0].steps) for (auto& stp : row) if (stp.vel) ++drumBefore;
        p.previewMelodyNote(0, 64); juce::AudioBuffer<float> ab(2, (int) (0.4 * sr)), bb(2, B); juce::MidiBuffer mm; for (int q = 0; q < ab.getNumSamples(); q += B) { bb.clear(); p.processBlock(bb, mm); for (int c = 0; c < 2; ++c) ab.copyFrom(c, q, bb, c, 0, juce::jmin(B, ab.getNumSamples() - q)); }
        p.previewMelodyNote(0, -1); p.drainRecorded(); int drumAfter = 0; for (auto& row : p.model.patterns[0].steps) for (auto& stp : row) if (stp.vel) ++drumAfter;
        const double fa = zcFreq(ab.getReadPointer(0), 4000, 8000, sr); p.recording.store(false); p.engine.internalPlay = false; M.on = true; p.publish();
        // the editor: SPACE, no hover focus, follow, MIDI REC, typing
        p.sync.setMode(ghosty::SyncTransport::Mode::Own); std::unique_ptr<juce::AudioProcessorEditor> e(p.createEditor()); auto* be = dynamic_cast<BeatsXEditor*>(e.get()); e->setVisible(true); be->showPageForTest(9); be->syncUiForTest(); pump();
        auto* mv = dynamic_cast<bx::MelodyTrackerView*>(be->partForTest("melody"));
        mv->mouseEnter(mev(*mv, 400, 300)); const bool noHoverFocus = !mv->hasKeyboardFocus(false);
        const juce::KeyPress space(juce::KeyPress::spaceKey); const bool viewTakes = mv->keyPressed(space); const bool was = p.sync.isOwnPlaying(); be->keyPressForTest(space); juce::AudioBuffer<float> tb(2, B); juce::MidiBuffer tm; p.processBlock(tb, tm); const bool toggled = p.sync.isOwnPlaying() != was; be->keyPressForTest(space); p.processBlock(tb, tm);
        M.pat(0).rows = 64; mv->follow = true; mv->playRow = [] { return 50; }; mv->playPat = [] { return 0; }; mv->scroll = 0; mv->tick(); const bool follows = mv->scroll > 0 && 50 >= mv->scroll && 50 < mv->scroll + mv->visRows();
        // MIDI REC while stopped: the note goes to the cursor (channel 2, row 4), no drum pad plays
        mv->chan = 1; mv->row = 4; mv->inst = 0; mv->changeStrip(bx::MelodyTrackerView::SRec, 1);
        const uint32_t trigBefore = p.engine.trig[0].load() + p.engine.trig[1].load(); juce::MidiBuffer note; note.addEvent(juce::MidiMessage::noteOn(1, 64, (juce::uint8) 100), 0); p.processBlock(tb, note); note.clear(); note.addEvent(juce::MidiMessage::noteOff(1, 64), 0); p.processBlock(tb, note);
        p.drainMelodyMidi([&](int n2, int v2, double beat, bool on) { if (on) { int pp = -1, rr = -1; if (beat >= 0) p.melodyRowAt(beat, pp, rr); mv->recordNote(n2, v2, pp, rr); } });
        const bool recorded = M.pat(0).cells[1][4].note == 65 && M.pat(0).cells[1][4].inst == 1 && p.engine.trig[0].load() + p.engine.trig[1].load() == trigBefore;
        mv->changeStrip(bx::MelodyTrackerView::SRec, 1); const bool recOff = !p.engine.melodyMidi.load();
        // typing: instrument "0" "3" = 03 (a lone "2" doesn't jump to 16); Insert / shift+Delete; transpose; interpolate
        mv->row = 8; mv->chan = 0; mv->field = 1; mv->nib = 0; mv->keyPressed(juce::KeyPress('0', {}, '0')); const int afterOne = M.pat(0).cells[0][8].inst; mv->keyPressed(juce::KeyPress('3', {}, '3')); const int inst03 = M.pat(0).cells[0][8].inst;
        M.pat(0).cells[0][10] = { 61, 1, gb::mel::kNoVol, 0, 0 }; mv->row = 10; mv->field = 0; mv->keyPressed(juce::KeyPress(juce::KeyPress::insertKey)); const bool inserted = M.pat(0).cells[0][11].note == 61 && M.pat(0).cells[0][10].note == 0;
        mv->row = 10; mv->keyPressed(juce::KeyPress(juce::KeyPress::deleteKey, juce::ModifierKeys::shiftModifier, 0)); const bool deleted = M.pat(0).cells[0][10].note == 61;
        mv->row = 10; mv->selR0 = -1; mv->keyPressed(juce::KeyPress(juce::KeyPress::upKey, juce::ModifierKeys::commandModifier, 0)); const bool transposed = M.pat(0).cells[0][10].note == 62;
        M.pat(0).cells[2][20].vol = 0; M.pat(0).cells[2][24].vol = 64; mv->chan = 2; mv->row = 20; mv->selR0 = 24; mv->selC0 = 2; mv->keyPressed(juce::KeyPress('i', juce::ModifierKeys::commandModifier, 'i')); const bool interp = M.pat(0).cells[2][22].vol == 32; mv->selR0 = -1;
        // BEAT + MELODY MIDI in one file
        auto both = p.exportBeatAndMelodyMidi(false); juce::MidiFile bm; { juce::FileInputStream in(both); bm.readFrom(in); } both.deleteFile();
        // screenshots: light / dark, compact / large
        M.inst[1].name = "Bell Pluck"; M.inst[1].sound = "Metal Bell"; M.inst[1].route = 6; M.inst[1].root = 81; M.inst[2].name = "808 Bass"; M.inst[2].sound = "808 Long"; M.inst[2].route = 0; M.inst[2].root = 33; M.inst[2].attack = 0.005f; M.inst[2].release = 0.2f;
        for (int r = 0; r < 32; r += 2) M.pat(0).cells[3][(size_t) r] = { (uint8_t) (61 + (r / 8) % 3 * 2), 2, (uint8_t) (0x30 - (r % 8) * 2), gb::mel::FxArp, 0x37 };
        M.pat(0).cells[4][0] = { 37, 3, gb::mel::kNoVol, gb::mel::FxGlide, 0 }; M.pat(0).cells[4][8] = { 44, 3, gb::mel::kNoVol, gb::mel::FxGlide, 0x10 }; M.pat(0).cells[5][4] = { 73, 1, 0x30, gb::mel::FxVibrato, 0x43 }; M.pat(0).cells[5][12] = { 0, 0, gb::mel::kNoVol, gb::mel::FxCutoff, 0x60 };
        p.publish(); mv->follow = false; mv->scroll = 0; mv->row = 4; mv->chan = 3; mv->repaint(); pump();
        snapComp(*e, shotDir.getChildFile("melody-page-light.png")); be->setThemeForTest(true); pump(); snapComp(*e, shotDir.getChildFile("melody-page-dark.png"));
        e->setSize(1600, 1000); pump(); snapComp(*e, shotDir.getChildFile("melody-page-dark-large-1600.png")); be->setThemeForTest(false); pump(); snapComp(*e, shotDir.getChildFile("melody-page-light-large-1600.png")); e->setSize(1180, 790); pump();
        mv->help = true; mv->repaint(); pump(); snapComp(*e, shotDir.getChildFile("melody-help-light.png")); mv->help = false;
        std::printf("  STEMS include the melody-only track 10: %d | kit change: the instrument keeps its sample %d | track PITCH +12: %.0f Hz (want 440) | audition E-4: %.0f Hz (want 330), drum steps before %d after %d (REC on, running)\n",
                    (int) melStem, (int) keeps, f12, fa, drumBefore, drumAfter);
        std::printf("  no focus on hover %d | SPACE: the view passes it on %d, OWN play toggled %d | FOLLOW scrolls to row 50: %d (scroll %d) | MIDI REC note at the cursor, no drum pad %d, REC off restores pads %d\n",
                    (int) noHoverFocus, (int) !viewTakes, (int) toggled, (int) follows, mv->scroll, (int) recorded, (int) recOff);
        std::printf("  instrument digits: after '0' %d, after '3' %d | insert row %d, delete row %d, transpose +1 %d, interpolate 0..64 -> row 22 = %d | BEAT + MELODY MIDI: %d tracks\n",
                    afterOne, inst03, (int) inserted, (int) deleted, (int) transposed, (int) M.pat(0).cells[2][22].vol, bm.getNumTracks());
        CHECK(melStem && keeps && std::abs(f12 - 440) < 8 && std::abs(fa - 329.6) < 6 && drumAfter == drumBefore, "melody: stems / own sound / track pitch / audition never records");
        CHECK(noHoverFocus && !viewTakes && toggled && follows && recorded && recOff, "melody page: SPACE / focus / follow / MIDI REC");
        CHECK(afterOne == 0 && inst03 == 3 && inserted && deleted && transposed && interp && bm.getNumTracks() >= 2, "melody editing + BEAT + MELODY MIDI");
    }
    section("keepSounds: a replaced sample is freed once no snapshot uses it");
    {
        BeatsXProcessor p; p.applyKit(0, true); auto s1 = makeSound("dropped", 48000, [](int i) { return (float) std::sin(i * 0.01) * 0.3f; }); std::weak_ptr<const gb::Sound> weak = s1;
        p.model.tracks[3].sample = s1; p.model.tracks[3].sound = "dropped"; p.publish(); s1.reset();
        p.model.tracks[3].sample = nullptr; p.model.tracks[3].sound = "Rim"; for (int i = 0; i < 12; ++i) p.publish();   // older snapshots retire
        p.keepMs = 0; p.trimKeeps(true); const bool freed = weak.expired(); p.keepMs = 30000;
        std::printf("  dropped sample freed after it was replaced and every snapshot moved on: %d (kept sounds now %d)\n", (int) freed, (int) p.keptSounds());
        CHECK(freed, "keepSounds trim");
    }
}

#include "GhostLookTests.inc"
