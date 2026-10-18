#pragma once
// Ghosty Beats 2 data: 16 tracks, 16 patterns (16/32/64 steps), a song made of pattern blocks, and kits.
// The message thread edits a Model; the audio thread only ever reads an immutable Snapshot.
#include "DrumKit.h"
#include <juce_data_structures/juce_data_structures.h>
#include <array>

namespace gb {

constexpr int kTracks = 16, kMaxSteps = 64, kPatterns = 16;

struct Step {   // 4 bytes
    uint8_t vel = 0;      // 0 = off, 1..127
    uint8_t prob = 100;   // chance %
    uint8_t roll = 1;     // hits inside the step: 1, 2, 3 (triplet), 4, 6, 8
    int8_t nudge = 0;     // -50..50 % of a step
};

struct Track {
    juce::String sound;   // built-in sound name, or the user sample's name
    SoundPtr sample;      // user sample (null = built-in)
    float pitch = 0, decay = 1, vol = 0.8f, pan = 0, tone = 0.5f, drive = 0;
    bool reverse = false, mute = false, solo = false;
    int choke = 0;        // 0 = none, tracks sharing a group cut each other (open/closed hats)
};

struct Pattern {
    int length = 16;
    std::array<std::array<Step, kMaxSteps>, kTracks> steps {};
    void clear() { for (auto& t : steps) for (auto& s : t) s = Step(); }
};

struct SongBlock { int pattern = 0, startBar = 0, bars = 1; };

struct Model {
    std::array<Track, kTracks> tracks;
    std::array<Pattern, kPatterns> patterns;
    std::vector<SongBlock> song;
    juce::String kit { "Trap" };
};

// What the audio thread reads: plain data + resolved sound pointers (kept alive by the processor).
struct Snapshot {
    std::array<Track, kTracks> tracks;   // (strings / shared_ptrs are only touched on the message thread)
    std::array<Pattern, kPatterns> patterns;
    std::array<SongBlock, 128> song {}; int songCount = 0;
    std::array<const Sound*, kTracks> sound {};
    bool anySolo = false;
};

// ---------------- kits: 16 sounds + a starter pattern (A) and a variation (B)
struct KitDef {
    const char* name; int bpm;
    std::array<const char*, kTracks> sounds;
    std::array<const char*, kTracks> a, b;   // 16-step strings: x=hit o=ghost X=accent 2/3/4/6=roll .=off
    std::array<int, kTracks> choke;
};

inline const std::vector<KitDef>& kits() {
    static const std::vector<KitDef> k {
        { "Trap", 140,
          { "808 Long", "Punch Kick", "Trap Snare", "Trap Clap", "Trap Hat", "Open Hat", "Rim", "Perc Pop", "Tom Low", "Tom High", "Crash", "Cowbell", "Shaker", "Snap", "Zap", "Riser" },
          { "x......x..x.....", "x......x..x.....", "....x.......x...", "....x.......x...", "x.x.x.x.x3x.x.x4", "", "", "", "", "", "", "", "", "", "", "" },
          { "x..x...x..x..x..", "x..x...x..x..x..", "....x.......x.o.", "....x.......x...", "x.xxx.x.6.x.x3xx", "..............x.", "", "", "", "", "x...............", "", "", "", "", "" },
          { 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { "Drill", 142,
          { "808 Glide", "Hard Kick", "Drill Snare", "Clap", "Trap Hat", "Short Open Hat", "Rim", "Perc Pop", "Tom Low", "Tom Mid", "Crash", "Tick", "Shaker", "Snap", "Laser", "Reverse Crash" },
          { "x.....x...x.....", "x.....x...x.....", "......x.......x.", "......x.......x.", "x..x..x.x..x.x3.", "", "...x.......x....", "", "", "", "", "", "", "", "", "" },
          { "x.....x..x..x...", "x.....x..x..x...", "......x.......x.", "......x.......x.", "x..x3.x.x..x.x6.", "", "...x.......x...x", "", "", "", "", "", "", "", "", "" },
          { 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { "Boom Bap", 90,
          { "Boom Bap Kick", "Soft Kick", "Boom Bap Snare", "Clap", "Hat", "Open Hat", "Rim", "Shaker", "Tom Low", "Conga High", "Crash", "Cowbell", "Tambourine", "Snap", "Vinyl Crackle", "Ghost Vox" },
          { "x.......xx......", "", "....x.......x...", "", "x.x.x.x.x.x.x.x.", "", "", "", "", "", "", "", "", "", "x...............", "" },
          { "x.....x.xx...x..", "", "....x..o....x...", "", "x.x.x.xxx.x.x.x.", "..............x.", "", "", "", "", "", "", "....x.......x...", "", "x...............", "" },
          { 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { "Lofi", 82,
          { "Lofi Kick", "Soft Kick", "Lofi Snare", "Snap", "Lofi Hat", "Short Open Hat", "Rim", "Shaker", "Tom Low", "Conga Low", "Ride", "Wood Block", "Tambourine", "Clap", "Vinyl Crackle", "Water Drop" },
          { "x......x.x......", "", "....x.......x...", "", "x.o.x.o.x.o.x.o.", "", "", "", "", "", "", "", "", "", "x...............", "" },
          { "x......x.x....x.", "", "....x.......x..o", "", "x.o.x.oxx.o.x.o.", "", "", "o..o............", "", "", "", "", "", "", "x...............", "" },
          { 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { "House", 124,
          { "House Kick", "Sub Kick", "Clap", "Snap", "Hat", "Open Hat", "Rim", "Shaker", "Tom Low", "Conga High", "Crash", "Cowbell", "Tambourine", "Big Clap", "Swell", "Ride" },
          { "x...x...x...x...", "", "....x.......x...", "", "..x...x...x...x.", "..x...x...x...x.", "", "x.xxx.xxx.xxx.xx", "", "", "", "", "", "", "", "" },
          { "x...x...x...x...", "", "....x.......x...", "", "xxx.xxx.xxx.xxx.", "..x...x...x...x.", "...x..x....x....", "x.xxx.xxx.xxx.xx", "", "", "", "", "", "", "", "" },
          { 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { "Dubstep", 140,
          { "Hard Kick", "808 Dirty", "Big Snare", "Big Clap", "Hat", "Open Hat", "Rim", "Tick", "Tom Low", "Tom High", "Crash", "Laser", "Shaker", "Snap", "Zap", "Riser" },
          { "x.........x.....", "", "........x.......", "........x.......", "x.x.x.x.x.x.x.x.", "", "", "", "", "", "x...............", "", "", "", "", "" },
          { "x.....x...x.....", "", "........x.....o.", "........x.......", "x.x.x.x.x.x.x4x6", "", "", "", "", "", "", "", "", "", "..............x.", "" },
          { 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { "Afrobeats", 108,
          { "Punch Kick", "808 Short", "Rim Snare", "Clap", "Hat", "Open Hat", "Rim", "Shaker", "Conga Low", "Conga High", "Crash", "Cowbell", "Tambourine", "Bongo", "Perc Pop", "Clave" },
          { "x..x...x..x.....", "", "", "....x.......x...", "x.xxx.xxx.xxx.xx", "", "...x..x....x..x.", "xxxxxxxxxxxxxxxx", "x.....x.........", "..x.x....x.x....", "", "", "", "", "", "x..x..x...x.x..." },
          { "x..x...x..x...x.", "", "", "....x.......x...", "x.xxx.xxx.xxx.xx", "", "...x..x....x..x.", "xxxxxxxxxxxxxxxx", "x.....x...x.....", "..x.x....x.x..x.", "", "", "", "", "", "x..x..x...x.x..." },
          { 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { "Phonk", 130,
          { "808 Dirty", "Hard Kick", "Trap Snare", "Big Clap", "Trap Hat", "Open Hat", "Rim", "Cowbell", "Tom Low", "Tom High", "Crash", "Tick", "Shaker", "Snap", "Ghost Vox", "Riser" },
          { "x..x..x...x.....", "x..x..x...x.....", "....x.......x...", "....x.......x...", "x.x.x.x.x.x.x.x.", "", "", "x.xx.x.xx.x.x.x.", "", "", "", "", "", "", "", "" },
          { "x..x..x...x..x..", "x..x..x...x..x..", "....x.......x...", "....x.......x...", "x.x.x.x.x3x.x4x.", "", "", "x.xx.x.xx.xxx.xx", "", "", "", "", "", "", "", "" },
          { 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { "Reggaeton", 95,
          { "Punch Kick", "808 Short", "Rim Snare", "Clap", "Hat", "Open Hat", "Rim", "Shaker", "Tom Low", "Conga High", "Crash", "Cowbell", "Tambourine", "Snap", "Perc Pop", "Zap" },
          { "x...x...x...x...", "", "...x..x....x..x.", "", "x.x.x.x.x.x.x.x.", "", "", "", "", "", "", "", "", "", "", "" },
          { "x...x...x...x...", "", "...x..x....x..x.", "...x..x....x..x.", "xxx.xxx.xxx.xxx.", "", "", "", "", "", "", "", "", "", "", "" },
          { 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
        { "Jersey Club", 140,
          { "Punch Kick", "808 Punchy", "Trap Snare", "Trap Clap", "Hat", "Open Hat", "Rim", "Shaker", "Tom Low", "Tom High", "Crash", "Cowbell", "Ghost Vox", "Snap", "Zap", "Riser" },
          { "x..x..x.x.x.....", "", "....x.......x...", "....x.......x...", "x.x.x.x.x.x.x.x.", "", "", "", "", "", "", "", "..x.....x.x.....", "", "", "" },
          { "x..x..x.x.x..x.x", "", "....x.......x...", "....x.......x...", "x.x.x.x.x.x.x.x.", "", "", "", "", "", "", "", "..x.....x.x...x.", "", "", "" },
          { 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 } },
    };
    return k;
}

inline void writeSteps(std::array<Step, kMaxSteps>& row, const char* s) {
    for (int i = 0; i < kMaxSteps; ++i) row[(size_t) i] = Step();
    if (!s) return;
    for (int i = 0; s[i] && i < 16; ++i) {
        Step st; const char c = s[i];
        if (c == 'x') st.vel = 105; else if (c == 'X') st.vel = 127; else if (c == 'o') st.vel = 55;
        else if (c >= '2' && c <= '8') { st.vel = 95; st.roll = (uint8_t) (c - '0'); }
        row[(size_t) i] = st;
    }
}

inline void applyKit(Model& m, const KitDef& k, bool patternsToo) {
    m.kit = k.name;
    for (int t = 0; t < kTracks; ++t) {
        auto& tr = m.tracks[(size_t) t]; tr = Track(); tr.sound = k.sounds[(size_t) t]; tr.choke = k.choke[(size_t) t];
        if (t >= 6) tr.vol = 0.7f;
    }
    if (patternsToo) {
        for (auto& p : m.patterns) { p.clear(); p.length = 16; }
        for (int t = 0; t < kTracks; ++t) { writeSteps(m.patterns[0].steps[(size_t) t], k.a[(size_t) t]); writeSteps(m.patterns[1].steps[(size_t) t], k.b[(size_t) t]); }
        m.song = { { 0, 0, 2 }, { 1, 2, 2 }, { 0, 4, 2 }, { 1, 6, 2 } };
    }
}

// ---------------- save / load
inline juce::String patternToString(const Pattern& p) { juce::MemoryBlock mb(p.steps.data(), sizeof(p.steps)); return mb.toBase64Encoding(); }
inline void patternFromString(Pattern& p, const juce::String& s) { juce::MemoryBlock mb; if (mb.fromBase64Encoding(s) && mb.getSize() == sizeof(p.steps)) std::memcpy(p.steps.data(), mb.getData(), sizeof(p.steps)); }

inline juce::String soundToBase64(const Sound& s) {
    const int n = juce::jmin((int) s.data.size(), (int) (s.rate * 12));
    juce::MemoryBlock mb((size_t) n * 2); auto* d = static_cast<int16_t*>(mb.getData());
    for (int i = 0; i < n; ++i) d[i] = (int16_t) juce::jlimit(-32767.0f, 32767.0f, s.data[(size_t) i] * 32767.0f);
    return mb.toBase64Encoding();
}
inline SoundPtr soundFromBase64(const juce::String& b64, double srcRate, double rate, const juce::String& name) {
    juce::MemoryBlock mb; if (!mb.fromBase64Encoding(b64) || mb.getSize() < 64) return nullptr;
    const int n = (int) mb.getSize() / 2; auto* d = static_cast<const int16_t*>(mb.getData());
    std::vector<float> mono((size_t) n); for (int i = 0; i < n; ++i) mono[(size_t) i] = d[i] / 32767.0f;
    return DrumLibrary::fromMono(mono, srcRate, rate, name);
}

inline juce::ValueTree modelToTree(const Model& m) {
    juce::ValueTree t("BEATS2"); t.setProperty("kit", m.kit, nullptr);
    for (int i = 0; i < kTracks; ++i) {
        const auto& tr = m.tracks[(size_t) i]; juce::ValueTree x("TRACK");
        x.setProperty("i", i, nullptr); x.setProperty("sound", tr.sound, nullptr); x.setProperty("pitch", tr.pitch, nullptr); x.setProperty("decay", tr.decay, nullptr);
        x.setProperty("vol", tr.vol, nullptr); x.setProperty("pan", tr.pan, nullptr); x.setProperty("tone", tr.tone, nullptr); x.setProperty("drive", tr.drive, nullptr);
        x.setProperty("rev", tr.reverse, nullptr); x.setProperty("mute", tr.mute, nullptr); x.setProperty("solo", tr.solo, nullptr); x.setProperty("choke", tr.choke, nullptr);
        if (tr.sample) { x.setProperty("srate", tr.sample->rate, nullptr); x.setProperty("sample", soundToBase64(*tr.sample), nullptr); }
        t.appendChild(x, nullptr);
    }
    for (int i = 0; i < kPatterns; ++i) { juce::ValueTree x("PATTERN"); x.setProperty("i", i, nullptr); x.setProperty("len", m.patterns[(size_t) i].length, nullptr); x.setProperty("steps", patternToString(m.patterns[(size_t) i]), nullptr); t.appendChild(x, nullptr); }
    for (auto& b : m.song) { juce::ValueTree x("BLOCK"); x.setProperty("p", b.pattern, nullptr); x.setProperty("start", b.startBar, nullptr); x.setProperty("bars", b.bars, nullptr); t.appendChild(x, nullptr); }
    return t;
}
inline void modelFromTree(Model& m, const juce::ValueTree& t, double rate) {
    m.kit = t.getProperty("kit", "Trap").toString(); m.song.clear();
    for (auto x : t) {
        if (x.hasType("TRACK")) {
            const int i = x.getProperty("i"); if (!juce::isPositiveAndBelow(i, kTracks)) continue;
            auto& tr = m.tracks[(size_t) i]; tr = Track();
            tr.sound = x.getProperty("sound").toString(); tr.pitch = (float) (double) x.getProperty("pitch", 0.0); tr.decay = (float) (double) x.getProperty("decay", 1.0);
            tr.vol = (float) (double) x.getProperty("vol", 0.8); tr.pan = (float) (double) x.getProperty("pan", 0.0); tr.tone = (float) (double) x.getProperty("tone", 0.5); tr.drive = (float) (double) x.getProperty("drive", 0.0);
            tr.reverse = x.getProperty("rev", false); tr.mute = x.getProperty("mute", false); tr.solo = x.getProperty("solo", false); tr.choke = x.getProperty("choke", 0);
            if (x.hasProperty("sample")) tr.sample = soundFromBase64(x.getProperty("sample").toString(), (double) x.getProperty("srate", 44100.0), rate, tr.sound);
        } else if (x.hasType("PATTERN")) {
            const int i = x.getProperty("i"); if (!juce::isPositiveAndBelow(i, kPatterns)) continue;
            m.patterns[(size_t) i].length = juce::jlimit(4, kMaxSteps, (int) x.getProperty("len", 16)); patternFromString(m.patterns[(size_t) i], x.getProperty("steps").toString());
        } else if (x.hasType("BLOCK")) m.song.push_back({ (int) x.getProperty("p"), (int) x.getProperty("start"), juce::jmax(1, (int) x.getProperty("bars", 1)) });
    }
}

} // namespace gb
