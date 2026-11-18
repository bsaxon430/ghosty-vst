#pragma once
// Ghosty Beats 2 data: 16 tracks, 16 patterns (16/32/64 steps), a song made of pattern blocks, and kits.
// The message thread edits a Model; the audio thread only ever reads an immutable Snapshot.
#include "DrumSynth.h"
#include "LinearSettings.h"
#include <juce_data_structures/juce_data_structures.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <map>

namespace gb {
namespace mel { struct Song; struct Mips; }
namespace linear { struct Perf; }   // Linear.h: the resolved LINEAR performance a snapshot carries

constexpr int kTracks = 16, kMaxSteps = 128, kPatterns = 16, kLayers = 2;
constexpr int kStepsV1 = 64;   // patterns before this version held 64 steps

struct StepV1 { uint8_t vel, prob, roll; int8_t nudge; };   // the 4-byte step older versions saved

struct Step {   // 24 bytes (saved raw, gzipped). Grid, tracker, break chop and the generators all edit this.
    uint8_t vel = 0;      // 0 = off, 1..127 (120+ counts as an accent)
    uint8_t prob = 100;   // chance %
    uint8_t roll = 1;     // hits: 1..16 (rate 0 = squeezed into the step: 2, 3 = triplet, 4, 6, 8)
    int8_t nudge = 0;     // -50..50 % of a step (micro-timing / tracker DELAY)
    uint8_t flam = 0;     // 0 = off, else a grace hit this many ms before (1..40)
    int8_t pitch = 0;     // semitones on top of the track pitch (808 melodies), -24..24
    uint8_t decay = 100;  // % of the track's decay (5..100)
    uint8_t flags = 0;    // bit 0 reverse, bit 1 LOCK (generators / EVOLVE / macros leave it alone), bit 2 TIMING LOCK, (bit 3: the tools' temporary
                          // "protected" mark), bit 4 SLIDE (glide from the note still ringing on this track), bit 5 MONO (cut this track's ringing note first)
    uint8_t rate = 0;     // retrigger: 0 = roll inside the step, else N hits per beat (8 = 1/32, 16 = 1/64, 32 = 1/128, 12 = 1/48 ...)
    int8_t velRamp = 0;   // velocity ramp across the hits, -100 (fade out) .. 100 (fade in)
    int8_t pitchRamp = 0; // pitch ramp across the hits, semitones -24..24
    int8_t cents = 0;     // fine pitch -99..99
    uint8_t offset = 0;   // sample start 0..99 %
    uint8_t cut = 0;      // gate: 0 = let it ring, 1..100 = % of a step (of one retrigger when retriggering)
    uint8_t slice = 0;    // 0 = the whole sound, 1..64 = that slice of the track's chopped break
    int8_t accel = 0;     // retrigger acceleration -100 (slowing down) .. 100 (speeding up)
    uint8_t destroy = 0;  // per-hit DESTRUCTION chance 0..100 % (seeded; FRACTURE macro adds to it)
    uint8_t freeze = 0;   // SPECTRAL FREEZE: 0 = off, else hold the frozen spectrum this many x10 ms
    uint8_t frev = 0;     // FREQUENCY-DEPENDENT REVERSE: band mask (1 low, 2 mid, 4 high; 0 = off)
    int8_t pan = 0;       // per-step pan -50 (left) .. 50 (right), added to the track pan
    uint8_t filt = 0;     // per-step filter: 0 = off, 1..63 low-pass (dark -> open), 64 = open, 65..127 high-pass (gentle -> thin)
    uint8_t glideT = 0;   // SLIDE time in 10 ms units (0 = 140 ms); only read when the SLIDE flag is set (was a spare byte: old projects hold 0)
    uint8_t hold = 0;     // note length in steps (0 = let it ring; else gated after this many steps with a 120 ms release: drill 808 notes)
    uint8_t spare6 = 0;
    bool reversed() const { return (flags & 1) != 0; }
    bool slide() const { return (flags & 16) != 0; }
    bool mono() const { return (flags & 32) != 0; }
    float slideMs() const { return glideT ? glideT * 10.0f : 140.0f; }
    bool locked() const { return (flags & 2) != 0; }
    bool timeLocked() const { return (flags & 4) != 0; }
    bool hasFx() const { return rate || velRamp || pitchRamp || cents || offset || cut || (flags & 1) || slice || roll > 1 || accel || destroy || freeze || frev || pan || filt; }
};
static_assert(sizeof(Step) == 24, "Step layout is saved raw");
struct StepV3 { uint8_t b[20]; };   // the 20-byte step v1 of Beats 2 saved (the first 20 bytes of Step)

struct Layer { juce::String sound; float level = 0.7f, pitch = 0, offset = 0; };   // offset in ms (0..50)

struct Track {
    juce::String sound;   // built-in sound name, or the user sample's name
    SoundPtr sample;      // user sample (null = built-in)
    float pitch = 0, decay = 1, vol = 0.8f, pan = 0, tone = 0.5f, drive = 0;
    bool reverse = false, mute = false, solo = false;
    int choke = 0;        // 0 = none, tracks sharing a group cut each other (open/closed hats)
    // groove
    float swing = 0, accent = 0, human = 0;     // per-track extra swing, accent contrast, humanize (seeded)
    // processing (all neutral by default = the old sound exactly)
    float tAttack = 0, tSustain = 0;            // transient shaper -1..1
    float sat = 0, comp = 0;                    // saturate, punch compressor 0..1
    float lowCut = 0, tilt = 0, highCut = 1;    // EQ: low cut (0 = off), tilt -1..1, high cut (1 = off)
    float duck = 0;                             // sidechain duck from the kick track 0..1
    float glide = 0;                            // legato glide time in ms (0 = every hit retriggers; > 0 = a hit while the track still rings slides its pitch, 808 style)
    float attack = 0;                           // amp attack in ms (0..200)
    float pitchEnv = 0, pitchEnvTime = 60;      // pitch envelope: semitones at the start (-24..24) falling to 0 over pitchEnvTime ms
    float velAmt = 1;                           // VELOCITY -> LEVEL amount 0..1 (1 = the full curve)
    int character = 0;                          // CHARACTER: 0 off (bit-exact), 1 8-bit home computer, 2 12-bit classic sampler, 3 early-90s lo-fi (Shared/VintageCharacter.h)
    float charAmount = 1, charDrive = 0;        // 0..1 each
    std::array<Layer, kLayers> layers;          // extra sounds stacked on this track (empty name = off)
    std::vector<int> slices;                    // break chop: slice start positions (samples, first = 0); empty = not chopped
    float chopSens = 0.5f;                      // the sensitivity the slices were found with
    // SAMPLE REGION (the WaveRegion editor): START / END (fractions of the sound) and a sustain LOOP. Defaults = the whole sound, no loop
    // (bit-exact the old playback). A looping hit keeps cycling until the track's next hit, a gate / CUT, the track's DECAY or 8 s.
    float regStart = 0, regEnd = 1, loopStart = 0, loopEnd = 1; bool loopOn = false;
    bool regionIsDefault() const { return regStart <= 0.0f && regEnd >= 1.0f && !loopOn; }
    bool lock = false;                          // generators / EVOLVE / macros leave this whole track alone
    struct Rule { uint8_t type = 0, src = 0, n = 2, pct = 50; };   // conditional probability (see ruleNames)
    std::array<Rule, 4> rules {};
};
enum RuleType : uint8_t { RuleNone = 0, RuleGhostAfter, RuleRetrigEveryN, RuleFillSilence, RuleQuieterAfterLoud, RuleDropEveryN, RuleBarOfN, RuleSkipIfTrack, RuleAccentEveryN };
inline const juce::StringArray& ruleNames() {
    static juce::StringArray r { "-", "If [track] played the step before -> ghost note", "Every Nth bar -> more retriggers", "Silent for N steps -> hit",
                                 "After a loud hit -> next one quieter", "Every Nth bar -> drop hits", "Play only on bar [k] of N", "If [track] plays here -> skip", "Every Nth step -> accent" };
    return r;
}

// grid resolutions: step length in beats
inline double stepBeats(int res) { static const double b[] { 0.5, 0.25, 0.125, 1.0 / 6.0, 1.0 / 3.0, 0.0625 }; return b[juce::jlimit(0, 5, res)]; }
inline const juce::StringArray& resNames() { static juce::StringArray n { "1/8", "1/16", "1/32", "1/16T", "1/8T", "1/64" }; return n; }
inline bool isTriplet(int res) { return res == 3 || res == 4; }
inline double retrigBeats(int rate) { return rate > 0 ? 1.0 / rate : 0.0; }   // N hits per beat

struct Pattern {
    int length = 16;      // steps (1..64): the loop / song-block length
    int res = 1;          // 0 1/8, 1 1/16, 2 1/32, 3 1/16T, 4 1/8T, 5 1/64
    float evolve = 0;     // EVOLVE: bar-by-bar variation amount (0 = plays as written)
    int evolveStyle = 0;  // 0 INTRICATE, 1 MACHINE GUN
    int fillEvery = 0;    // 0 = off, else a fill in the last beat of every N bars (2, 4, 8)
    int seed = 1;         // EVOLVE seed (same seed = same variations)
    std::array<uint8_t, kTracks> trackLen {};   // 0 = follow `length`, else 1..64 (polymeter)
    std::array<std::array<Step, kMaxSteps>, kTracks> steps {};
    void clear() { for (auto& t : steps) for (auto& s : t) s = Step(); trackLen.fill(0); evolve = 0; evolveStyle = 0; fillEvery = 0; seed = 1; }
    int lenOf(int t) const { const int l = trackLen[(size_t) t]; return l > 0 ? l : length; }
    double beats() const { return length * stepBeats(res); }
    int bars() const { return juce::jmax(1, (int) std::ceil(beats() / 4.0 - 1e-9)); }
    double loopBeats() const { int m = length; for (int t = 0; t < (int) trackLen.size(); ++t) m = juce::jmax(m, lenOf(t)); return m * stepBeats(res); }   // longest track
    int loopBars() const { return juce::jmax(1, (int) std::ceil(loopBeats() / 4.0 - 1e-9)); }
};

struct SongBlock { int pattern = 0, startBar = 0, bars = 1; };

// The 16 patterns, heap-owned with value semantics: a Model stays a few kB, so undo copies, host objects and test harnesses never
// carry 0.8 MB of steps on the stack. Indexing and range-for work as on the array it wraps.
struct PatternBank {
    std::unique_ptr<std::array<Pattern, kPatterns>> p;
    PatternBank() : p(std::make_unique<std::array<Pattern, kPatterns>>()) {}
    PatternBank(const PatternBank& o) : p(std::make_unique<std::array<Pattern, kPatterns>>(*o.p)) {}
    PatternBank(PatternBank&& o) noexcept : PatternBank() { p.swap(o.p); }
    PatternBank& operator=(const PatternBank& o) { if (this != &o) *p = *o.p; return *this; }
    PatternBank& operator=(PatternBank&& o) noexcept { if (this != &o) p.swap(o.p); return *this; }
    Pattern& operator[](size_t i) { return (*p)[i]; } const Pattern& operator[](size_t i) const { return (*p)[i]; }
    Pattern* begin() { return p->data(); } Pattern* end() { return p->data() + kPatterns; } const Pattern* begin() const { return p->data(); } const Pattern* end() const { return p->data() + kPatterns; }
    size_t size() const { return kPatterns; }
    operator const std::array<Pattern, kPatterns>&() const { return *p; }
};

namespace script { struct Program; }
// PHRASE LOCK: bars [start, start+bars) of the 16-bar phrase (and of every pattern) stay exactly as written on the masked tracks
struct PhraseLock { int start = 0, bars = 0; uint16_t mask = 0xffff; bool active() const { return bars > 0; } bool covers(long bar, int t) const { const long b = ((bar % 16) + 16) % 16; return bars > 0 && (mask >> t & 1) && b >= start && b < start + bars; } };
// MOTIF LOCK: a figure on one track (1/16 positions inside a bar) that every bar keeps, whatever EVOLVE / BREED / DNA do
struct Motif { int track = -1, start = 0, len = 0; std::array<Step, 16> steps {}; bool active() const { return track >= 0 && len > 0; } };
struct ScriptModule { juce::String name, code; bool on = true; };
constexpr int kLocks = 4, kMotifs = 4, kScripts = 8;
inline std::array<uint8_t, 16> defaultArc() { return { 20, 20, 25, 25, 40, 50, 45, 55, 60, 70, 80, 90, 100, 70, 40, 20 }; }   // establish / vary / build / resolve
constexpr int kStateVersion = 4;   // 3 = Beats 2 v1 (tanh master, 12 dB velocity law, Freeverb); 4 = clean limiter, new velocity law, FDN reverb
struct Model {
    int ver = kStateVersion;   // the version a loaded project was saved with: older projects keep their old sound (legacy master / velocity law / reverb)
    std::array<Track, kTracks> tracks;
    PatternBank patterns;
    std::vector<SongBlock> song;
    juce::String kit { "Trap" };
    int scTrack = -1;     // sidechain source (-1 = the first kick track)
    std::array<uint8_t, 16> arc = defaultArc();   // PHRASE: 16-bar intensity curve EVOLVE follows (0..100)
    int converge = 0;     // CONVERGENCE: polymeter tracks realign every N bars (0 = never)
    int toolSeed = 1;     // GENERATE / KEEP / BLEND / BREED / tool seed (part of the beat, so UNDO brings it back too)
    int drillGrit = 70;   // DRILL GRIT 0..100 for GENERATE DRILL (DrillStyle.h): new beats 70; a beat saved without it loads as 0 (the DRILL of installer #27)
    std::array<PhraseLock, kLocks> locks {};
    std::array<Motif, kMotifs> motifs {};
    std::vector<ScriptModule> scripts;   // up to kScripts
    linear::Settings linear;             // LINEAR DRUMMING (Linear.h); off = the performance exactly as collectEvents() plays it
};

// What the audio thread reads: plain data + resolved sound pointers (kept alive by the processor).
struct Snapshot {
    std::array<Track, kTracks> tracks;   // (strings / shared_ptrs are only touched on the message thread)
    std::array<Pattern, kPatterns> patterns;
    std::array<SongBlock, 128> song {}; int songCount = 0;
    std::array<const Sound*, kTracks> sound {};
    std::array<std::array<const Sound*, kLayers>, kTracks> layer {};
    bool anySolo = false;
    int scTrack = -1;
    std::array<uint8_t, kTracks> role {};   // what each track is (for EVOLVE): see Role
    std::array<uint8_t, 16> arc = defaultArc(); int converge = 0;
    std::array<PhraseLock, kLocks> locks {}; std::array<Motif, kMotifs> motifs {};
    std::array<std::shared_ptr<const script::Program>, kScripts> progs {}; int nProgs = 0;
    int ver = kStateVersion;
    linear::Settings linear;
    uint64_t serial = 0;                           // content serial (publish() counts up): caches key on this, never on the address
    std::shared_ptr<const linear::Perf> linPerf;   // the LINEAR performance resolved for this content (null = LINEAR off / not built)
    std::shared_ptr<const linear::Perf> linPerfPrev;   // the one before it: kept alive here so the audio thread can finish its bar on it
    std::shared_ptr<const mel::Song> melody; std::array<const Sound*, 16> melSound {}; std::array<const mel::Mips*, 16> melMips {};   // MELODIC TRACKER: the song + each instrument's sound
    std::array<std::array<const Sound*, 8>, kTracks> variant {};   // per-hit destruction sounds: [0] freeze, [1..7] band-reverse masks
};
enum Role : uint8_t { RoleOther = 0, RoleKick, RoleSnare, RoleHat, RolePerc, Role808, RoleOpenHat, RoleBreak };
inline uint8_t roleOf(const juce::String& category, const juce::String& name) {
    if (category == "Kick") return RoleKick; if (category == "Snare" || category == "Clap") return RoleSnare;
    if (category == "Open Hat") return RoleOpenHat; if (category == "Hat" || category == "Pedal Hat") return RoleHat;
    if (category == "Perc" || category == "Tom") return RolePerc; if (category == "808") return Role808; if (category == "Break") return RoleBreak;
    const auto n = name.toLowerCase();
    if (n.contains("kick") || n.contains("bd")) return RoleKick; if (n.contains("snare") || n.contains("clap")) return RoleSnare; if (n.contains("hat")) return RoleHat;
    return RoleOther;
}

// ---------------- kits: 16 sounds + a starter pattern (A) and a variation (B)
struct KitDef {
    const char* name; int bpm;
    std::array<const char*, kTracks> sounds;
    std::array<const char*, kTracks> a, b;   // 16-step strings: x=hit o=ghost X=accent g=soft 2/3/4/6/8=roll f=flam .=off; "L12:" prefix = 12-step track
    std::array<int, kTracks> choke;
    int style = 0;        // 0 = classic kit (no processing), 1 club, 2 heavy, 3 hip-hop, 4 clean, 5 hard
    float hatSwing = 0;   // per-track swing given to the hat / shaker tracks
    void (*starter)(Model&, const std::function<juce::String(const juce::String&)>&) = nullptr;   // writes patterns A + B itself (the DRILL kit: 2-bar beats with slides)
    const float* vols = nullptr;   // per-track VOLUME (16 values) instead of the default 0.8 / 0.7 (the DRILL kit's own mix)
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

inline const std::vector<KitDef>& genreKits();
inline const std::vector<KitDef>& allKits() {
    static const std::vector<KitDef> all = [] { auto v = kits(); for (auto& k : genreKits()) v.push_back(k); return v; }();
    return all;
}

inline int writeSteps(std::array<Step, kMaxSteps>& row, const char* s) {   // returns the track length ("L12:" prefix), 0 = pattern length
    for (int i = 0; i < kMaxSteps; ++i) row[(size_t) i] = Step();
    if (!s) return 0;
    int len = 0;
    if (s[0] == 'L' && s[1] >= '0' && s[1] <= '9') { len = std::atoi(s + 1); while (*s && *s != ':') ++s; if (*s) ++s; }
    for (int i = 0; s[i] && i < 16; ++i) {
        Step st; const char c = s[i];
        if (c == 'x') st.vel = 105; else if (c == 'X') st.vel = 127; else if (c == 'o') st.vel = 55; else if (c == 'g') st.vel = 35;
        else if (c == 'f') { st.vel = 110; st.flam = 18; }
        else if (c >= '2' && c <= '8') { st.vel = 95; st.roll = (uint8_t) (c - '0'); }
        row[(size_t) i] = st;
    }
    return juce::jlimit(0, kMaxSteps, len);
}

// the processing a genre kit starts with, by what each track holds
inline void styleTrack(Track& tr, const juce::String& cat, int style, float hatSwing) {
    const bool kick = cat == "Kick", snare = cat == "Snare" || cat == "Clap", hat = cat == "Hat" || cat == "Open Hat" || cat == "Pedal Hat", bass = cat == "808";
    if (hat || tr.sound.startsWith("Shaker")) { tr.swing = hatSwing; tr.lowCut = 0.25f; if (style != 0) tr.human = 0.15f; }
    switch (style) {
        case 1: if (kick) { tr.tAttack = 0.3f; tr.sat = 0.15f; tr.comp = 0.25f; } if (snare) tr.comp = 0.2f; if (bass) tr.duck = 0.6f; break;
        case 2: if (kick) { tr.tAttack = 0.5f; tr.sat = 0.35f; tr.comp = 0.4f; } if (snare) { tr.tAttack = 0.4f; tr.sat = 0.3f; tr.comp = 0.4f; } break;
        case 3: if (kick) { tr.sat = 0.2f; tr.comp = 0.2f; } if (bass) tr.sat = 0.2f; if (snare) tr.comp = 0.15f; break;
        case 4: if (kick || snare) tr.comp = 0.2f; break;
        case 5: if (kick) { tr.sat = 0.5f; tr.comp = 0.3f; tr.tAttack = 0.3f; } if (snare) tr.comp = 0.3f; break;
        case 6: if (kick) tr.sat = 0.25f; if (snare) tr.sat = 0.3f; if (hat) tr.sat = 0.1f; if (bass) { tr.duck = 0.4f; tr.sat = 0.2f; } break;   // drill: peaks shaved per drum (so the bus limiter rests), a light duck of the 808 under the kick (about -4 dB)
        case 7: if (kick) tr.sat = 0.35f; if (snare) { tr.tAttack = -0.3f; tr.tSustain = 0.2f; tr.sat = 0.35f; } if (cat == "Perc") { tr.tAttack = -0.25f; tr.tSustain = 0.1f; tr.sat = 0.2f; }
                if (hat) tr.sat = 0.15f; if (bass) { tr.duck = 0.3f; tr.sat = 0.15f; } break;   // latin: a dense, transient-rich groove: the snare / hand-drum spikes rounded, a little more body, peaks shaved per drum
        case 8: if (kick) tr.sat = 0.25f; if (snare) tr.sat = 0.35f; if (hat) tr.sat = 0.25f; if (cat == "Perc" || cat == "Tom") tr.sat = 0.3f; if (cat == "Cymbal") tr.sat = 0.15f;
                if (bass) { tr.duck = 0.3f; tr.sat = 0.15f; } break;   // classic-character kits (ClassicPacks.h): raw machine voices, every drum's peak rounded so the bus limiter rests
        case 9: if (kick) tr.sat = 0.3f; if (snare) tr.sat = 0.35f; if (hat) tr.sat = 0.25f; if (cat == "Perc" || cat == "Tom") tr.sat = 0.3f; if (cat == "Cymbal") tr.sat = 0.15f;
                if (bass) { tr.duck = 0.65f; tr.sat = 0.2f; } break;   // 808-led classic kits: as 8, and the 808 ducks under the kick (the two never stack into one peak)
        default: break;
    }
}

inline float kitTrimDb(const juce::String& kit);   // GenreKits.h: gain staging so every kit starts near -14 LUFS
inline void applyKit(Model& m, const KitDef& k, bool patternsToo, const std::function<juce::String(const juce::String&)>& categoryOf = {}) {
    m.kit = k.name;
    const float trim = juce::Decibels::decibelsToGain(kitTrimDb(k.name));
    for (int t = 0; t < kTracks; ++t) {
        auto& tr = m.tracks[(size_t) t]; tr = Track(); tr.sound = k.sounds[(size_t) t]; tr.choke = k.choke[(size_t) t];
        if (t >= 6) tr.vol = 0.7f;
        if (k.vols) tr.vol = k.vols[t];
        tr.vol = juce::jlimit(0.0f, 2.0f, tr.vol * trim);
        if (k.style != 0 && categoryOf) styleTrack(tr, categoryOf(tr.sound), k.style, k.hatSwing);
    }
    if (k.style != 0) m.scTrack = -1;
    if (patternsToo) {
        for (auto& p : m.patterns) { p.clear(); p.length = 16; p.res = 1; }
        for (int t = 0; t < kTracks; ++t) {
            m.patterns[0].trackLen[(size_t) t] = (uint8_t) writeSteps(m.patterns[0].steps[(size_t) t], k.a[(size_t) t]);
            m.patterns[1].trackLen[(size_t) t] = (uint8_t) writeSteps(m.patterns[1].steps[(size_t) t], k.b[(size_t) t]);
        }
        m.song = { { 0, 0, 2 }, { 1, 2, 2 }, { 0, 4, 2 }, { 1, 6, 2 } };
        if (k.starter) k.starter(m, categoryOf);
    }
}

// ---------------- save / load
// steps are saved three times: "steps4" (24-byte steps x 128, this version), "steps3" (the 20-byte x 64 layout of Beats 2 v1)
// and "steps" (the 4-byte x 64 layout of Beats 1), so older versions still open new projects. Loading prefers steps4.
inline juce::String patternToString(const Pattern& p) {   // gzip + base64 of the raw steps
    juce::MemoryOutputStream mo; { juce::GZIPCompressorOutputStream gz(mo, 9); gz.write(p.steps.data(), sizeof(p.steps)); gz.flush(); }
    return "z" + mo.getMemoryBlock().toBase64Encoding();
}
inline juce::String patternToStringV3(const Pattern& p) {   // the first 64 steps, 20 bytes each
    juce::MemoryBlock mb(sizeof(StepV3) * kTracks * kStepsV1); auto* d = static_cast<StepV3*>(mb.getData());
    for (int t = 0; t < kTracks; ++t) for (int s = 0; s < kStepsV1; ++s) std::memcpy(d[t * kStepsV1 + s].b, &p.steps[(size_t) t][(size_t) s], sizeof(StepV3));
    juce::MemoryOutputStream mo; { juce::GZIPCompressorOutputStream gz(mo, 9); gz.write(mb.getData(), mb.getSize()); gz.flush(); }
    return "z" + mo.getMemoryBlock().toBase64Encoding();
}
inline juce::String patternToStringV1(const Pattern& p) {
    juce::MemoryBlock mb(sizeof(StepV1) * kTracks * kStepsV1); auto* d = static_cast<StepV1*>(mb.getData());
    for (int t = 0; t < kTracks; ++t) for (int s = 0; s < kStepsV1; ++s) { const auto& x = p.steps[(size_t) t][(size_t) s]; d[t * kStepsV1 + s] = { x.vel, x.prob, x.roll, x.nudge }; }
    return mb.toBase64Encoding();
}
inline bool patternFromString(Pattern& p, const juce::String& s) {   // accepts every layout
    juce::MemoryBlock mb;
    if (s.startsWithChar('z')) {
        juce::MemoryBlock z; if (!z.fromBase64Encoding(s.substring(1))) return false;
        juce::MemoryInputStream mi(z, false); juce::GZIPDecompressorInputStream gz(mi); juce::MemoryOutputStream mo; mo.writeFromInputStream(gz, (juce::int64) sizeof(p.steps) + 16); mb = mo.getMemoryBlock();
    } else if (!mb.fromBase64Encoding(s)) return false;
    for (auto& row : p.steps) for (auto& st : row) st = Step();
    if (mb.getSize() == sizeof(p.steps)) { std::memcpy(p.steps.data(), mb.getData(), sizeof(p.steps)); }
    else if (mb.getSize() == sizeof(StepV3) * kTracks * kStepsV1) {
        const auto* d = static_cast<const StepV3*>(mb.getData());
        for (int t = 0; t < kTracks; ++t) for (int k = 0; k < kStepsV1; ++k) std::memcpy(&p.steps[(size_t) t][(size_t) k], d[t * kStepsV1 + k].b, sizeof(StepV3));
    } else if (mb.getSize() == sizeof(StepV1) * kTracks * kStepsV1) {
        const auto* d = static_cast<const StepV1*>(mb.getData());
        for (int t = 0; t < kTracks; ++t) for (int k = 0; k < kStepsV1; ++k) { const auto& x = d[t * kStepsV1 + k]; Step st; st.vel = x.vel; st.prob = x.prob; st.roll = x.roll; st.nudge = x.nudge; p.steps[(size_t) t][(size_t) k] = st; }
    } else return false;
    for (auto& row : p.steps) for (auto& st : row) {   // hostile / corrupt data never reaches the engine out of range
        st.roll = (uint8_t) juce::jlimit(1, 16, (int) st.roll); st.prob = (uint8_t) juce::jlimit(1, 100, (int) st.prob); st.nudge = (int8_t) juce::jlimit(-50, 50, (int) st.nudge); st.flam = (uint8_t) juce::jmin(40, (int) st.flam);
        st.pitch = (int8_t) juce::jlimit(-24, 24, (int) st.pitch); st.decay = (uint8_t) juce::jlimit(5, 100, (int) st.decay); st.rate = (uint8_t) juce::jmin(64, (int) st.rate); st.velRamp = (int8_t) juce::jlimit(-100, 100, (int) st.velRamp);
        st.pitchRamp = (int8_t) juce::jlimit(-24, 24, (int) st.pitchRamp); st.cents = (int8_t) juce::jlimit(-99, 99, (int) st.cents); st.offset = (uint8_t) juce::jmin(99, (int) st.offset); st.cut = (uint8_t) juce::jmin(100, (int) st.cut);
        st.slice = (uint8_t) juce::jmin(64, (int) st.slice); st.accel = (int8_t) juce::jlimit(-100, 100, (int) st.accel); st.destroy = (uint8_t) juce::jmin(100, (int) st.destroy); st.frev &= 7; st.pan = (int8_t) juce::jlimit(-50, 50, (int) st.pan); st.filt = (uint8_t) juce::jmin(127, (int) st.filt);
        st.glideT = (uint8_t) juce::jmin(200, (int) st.glideT); st.hold = (uint8_t) juce::jmin(kMaxSteps, (int) st.hold);
    }
    return true;
}
inline juce::String trackLenToString(const Pattern& p) { juce::StringArray a; for (auto l : p.trackLen) a.add(juce::String((int) l)); return a.joinIntoString(","); }
inline void trackLenFromString(Pattern& p, const juce::String& s) { auto a = juce::StringArray::fromTokens(s, ",", ""); for (int i = 0; i < kTracks; ++i) p.trackLen[(size_t) i] = (uint8_t) juce::jlimit(0, kMaxSteps, i < a.size() ? a[i].getIntValue() : 0); }

inline juce::String channelToBase64(const std::vector<float>& x, double rate) {
    const int n = juce::jmin((int) x.size(), (int) (rate * 12));
    juce::MemoryBlock mb((size_t) n * 2); auto* d = static_cast<int16_t*>(mb.getData());
    for (int i = 0; i < n; ++i) d[i] = (int16_t) juce::jlimit(-32767.0f, 32767.0f, x[(size_t) i] * 32767.0f);
    return mb.toBase64Encoding();
}
inline juce::String soundToBase64(const Sound& s) { return channelToBase64(s.data, s.rate); }
inline bool channelFromBase64(std::vector<float>& out, const juce::String& b64, double srcRate, double rate) {
    juce::MemoryBlock mb; if (!mb.fromBase64Encoding(b64) || mb.getSize() < 64) return false;
    const int n = (int) mb.getSize() / 2; auto* d = static_cast<const int16_t*>(mb.getData());
    const double ratio = srcRate / rate; const int m = juce::jmax(1, (int) std::floor((n - 1) / ratio) + 1); out.resize((size_t) m);
    for (int i = 0; i < m; ++i) { const double q = i * ratio; const int k = juce::jmin((int) q, n - 1); const float t = (float) (q - k); const float x0 = d[k] / 32767.0f, x1 = d[juce::jmin(k + 1, n - 1)] / 32767.0f; out[(size_t) i] = x0 + (x1 - x0) * t; }
    return true;
}
// Samples are stored as 16-bit FLAC (lossless at the stored depth; mono or stereo) as a binary property of the (binary) state.
// The 16-bit quantisation is ours (round to nearest, k / 32768 back), so save -> load -> save is byte-identical.
inline juce::MemoryBlock soundToFlac(const Sound& s) {
    juce::MemoryBlock out; juce::FlacAudioFormat flac;
    const int n = juce::jmin((int) s.data.size(), (int) (s.rate * 12)), ch = s.stereo() ? 2 : 1; if (n <= 0) return out;
    std::unique_ptr<juce::AudioFormatWriter> w(flac.createWriterFor(new juce::MemoryOutputStream(out, false), s.rate, (unsigned) ch, 16, {}, 5));   // NOT-EXPORT: the user's own sample, kept inside the project state (memory)
    if (!w) return {};
    std::vector<int> q[2]; const int* ptrs[2] {};
    for (int c = 0; c < ch; ++c) { const auto& src = c == 0 ? s.data : s.right; q[c].resize((size_t) n); for (int i = 0; i < n; ++i) q[c][(size_t) i] = (int) ((unsigned) juce::jlimit(-32767, 32767, (int) std::lrint(src[(size_t) i] * 32768.0f)) << 16); /* left-aligned 16-bit, shifted as unsigned: no UB on negatives */ ptrs[c] = q[c].data(); }
    w->write(ptrs, n); w.reset();   // (the writer flushes and deletes the stream)
    return out;
}
inline SoundPtr soundFromFlac(const juce::MemoryBlock& mb, double rate, const juce::String& name, double beats = 0) {
    if (mb.getSize() < 32) return nullptr;
    juce::FlacAudioFormat flac; std::unique_ptr<juce::AudioFormatReader> r(flac.createReaderFor(new juce::MemoryInputStream(mb, false), true));
    if (!r || r->lengthInSamples <= 0 || r->numChannels < 1 || r->usesFloatingPointData) return nullptr;
    const int n = (int) r->lengthInSamples, ch = juce::jmin(2, (int) r->numChannels);
    std::vector<int> raw[2]; int* dest[2] {}; for (int c = 0; c < ch; ++c) { raw[c].resize((size_t) n); dest[c] = raw[c].data(); }
    if (!r->read(dest, ch, 0, n, true)) return nullptr;
    auto s = std::make_shared<Sound>(); s->name = name; s->category = "Sample"; s->rate = rate; s->beats = beats;
    auto take = [&](int c, std::vector<float>& dst) {   // the stored rate normally equals the engine rate; otherwise a linear resample (as v1 did)
        std::vector<float> x((size_t) n); for (int i = 0; i < n; ++i) x[(size_t) i] = (float) (raw[c][(size_t) i] >> 16) / 32768.0f;
        const double ratio = r->sampleRate / rate; if (std::abs(ratio - 1.0) < 1e-9) { dst = std::move(x); return; }
        const int m = juce::jmax(1, (int) std::floor((n - 1) / ratio) + 1); dst.resize((size_t) m);
        for (int i = 0; i < m; ++i) { const double q = i * ratio; const int k = juce::jmin((int) q, n - 1); const float t = (float) (q - k); dst[(size_t) i] = x[(size_t) k] + (x[(size_t) juce::jmin(k + 1, n - 1)] - x[(size_t) k]) * t; } };
    take(0, s->data); if (ch == 2) take(1, s->right);
    return s;
}
inline juce::String soundToFlacBase64(const Sound& s) { return soundToFlac(s).toBase64Encoding(); }
inline SoundPtr soundFromFlacBase64(const juce::String& b64, double rate, const juce::String& name, double beats = 0) { juce::MemoryBlock mb; if (!mb.fromBase64Encoding(b64)) return nullptr; return soundFromFlac(mb, rate, name, beats); }
inline SoundPtr soundFromBase64(const juce::String& b64, double srcRate, double rate, const juce::String& name, double beats = 0, const juce::String& b64Right = {}) {
    // restores exactly what was saved (no second trim / normalise, so slice positions stay put)
    auto s = std::make_shared<Sound>(); s->name = name; s->category = "Sample"; s->rate = rate; s->beats = beats;
    if (!channelFromBase64(s->data, b64, srcRate, rate)) return nullptr;
    if (b64Right.isNotEmpty()) { if (!channelFromBase64(s->right, b64Right, srcRate, rate) || s->right.size() != s->data.size()) s->right.clear(); }
    return s;
}

inline juce::ValueTree modelToTree(const Model& m) {
    juce::ValueTree t("BEATS2"); t.setProperty("kit", m.kit, nullptr); t.setProperty("ver", m.ver, nullptr); t.setProperty("sc", m.scTrack, nullptr);
    std::map<const Sound*, int> savedSample;   // a sample shared by several tracks is stored once
    { juce::StringArray a; for (auto v : m.arc) a.add(juce::String((int) v)); t.setProperty("arc", a.joinIntoString(","), nullptr); } t.setProperty("converge", m.converge, nullptr); t.setProperty("toolseed", m.toolSeed, nullptr); t.setProperty("grit", m.drillGrit, nullptr);
    for (int i = 0; i < kTracks; ++i) {
        const auto& tr = m.tracks[(size_t) i]; juce::ValueTree x("TRACK");
        x.setProperty("i", i, nullptr); x.setProperty("sound", tr.sound, nullptr); x.setProperty("pitch", tr.pitch, nullptr); x.setProperty("decay", tr.decay, nullptr);
        x.setProperty("vol", tr.vol, nullptr); x.setProperty("pan", tr.pan, nullptr); x.setProperty("tone", tr.tone, nullptr); x.setProperty("drive", tr.drive, nullptr);
        x.setProperty("rev", tr.reverse, nullptr); x.setProperty("mute", tr.mute, nullptr); x.setProperty("solo", tr.solo, nullptr); x.setProperty("choke", tr.choke, nullptr);
        const std::pair<const char*, float> f[] { { "swing", tr.swing }, { "accent", tr.accent }, { "human", tr.human }, { "tatt", tr.tAttack }, { "tsus", tr.tSustain }, { "sat", tr.sat },
                                                  { "comp", tr.comp }, { "lowcut", tr.lowCut }, { "tilt", tr.tilt }, { "highcut", tr.highCut }, { "duck", tr.duck },
                                                  { "glide", tr.glide }, { "attack", tr.attack }, { "penv", tr.pitchEnv }, { "ptime", tr.pitchEnvTime }, { "velamt", tr.velAmt }, { "chamt", tr.charAmount }, { "chdrv", tr.charDrive } };
        x.setProperty("chmode", tr.character, nullptr);
        for (auto& kv : f) x.setProperty(kv.first, kv.second, nullptr);
        for (int l = 0; l < kLayers; ++l) { const auto& L = tr.layers[(size_t) l]; const juce::String k = "l" + juce::String(l);
            x.setProperty(k + "s", L.sound, nullptr); x.setProperty(k + "v", L.level, nullptr); x.setProperty(k + "p", L.pitch, nullptr); x.setProperty(k + "o", L.offset, nullptr); }
        x.setProperty("lock", tr.lock, nullptr);
        { juce::StringArray a; for (auto& r : tr.rules) a.add(juce::String(r.type) + ":" + juce::String(r.src) + ":" + juce::String(r.n) + ":" + juce::String(r.pct)); x.setProperty("rules", a.joinIntoString(","), nullptr); }
        if (!tr.slices.empty()) { juce::StringArray a; for (int v : tr.slices) a.add(juce::String(v)); x.setProperty("slices", a.joinIntoString(","), nullptr); x.setProperty("chopsens", tr.chopSens, nullptr); }
        if (!tr.regionIsDefault() || tr.loopStart > 0.0f || tr.loopEnd < 1.0f) { x.setProperty("rstart", tr.regStart, nullptr); x.setProperty("rend", tr.regEnd, nullptr); x.setProperty("lstart", tr.loopStart, nullptr); x.setProperty("lend", tr.loopEnd, nullptr); x.setProperty("loop", tr.loopOn, nullptr); }   // (written only when edited: old projects save as before)
        if (tr.sample) {
            auto it = savedSample.find(tr.sample.get());
            if (it != savedSample.end()) x.setProperty("sampleref", it->second, nullptr);   // same data as that track
            else { savedSample[tr.sample.get()] = i; x.setProperty("srate", tr.sample->rate, nullptr); x.setProperty("flac", juce::var(soundToFlac(*tr.sample)), nullptr); if (tr.sample->beats > 0) x.setProperty("sbeats", tr.sample->beats, nullptr); }
        }
        t.appendChild(x, nullptr);
    }
    for (int i = 0; i < kPatterns; ++i) { const auto& p = m.patterns[(size_t) i]; juce::ValueTree x("PATTERN"); x.setProperty("i", i, nullptr); x.setProperty("len", p.length, nullptr);
        x.setProperty("res", p.res, nullptr); x.setProperty("tlen", trackLenToString(p), nullptr);
        x.setProperty("steps", patternToStringV1(p), nullptr); x.setProperty("steps3", patternToStringV3(p), nullptr); x.setProperty("steps4", patternToString(p), nullptr);
        x.setProperty("evolve", p.evolve, nullptr); x.setProperty("evstyle", p.evolveStyle, nullptr); x.setProperty("fill", p.fillEvery, nullptr); x.setProperty("seed", p.seed, nullptr); t.appendChild(x, nullptr); }
    for (int i = 0; i < kLocks; ++i) if (m.locks[(size_t) i].active()) { juce::ValueTree x("LOCK"); x.setProperty("start", m.locks[(size_t) i].start, nullptr); x.setProperty("bars", m.locks[(size_t) i].bars, nullptr); x.setProperty("mask", (int) m.locks[(size_t) i].mask, nullptr); t.appendChild(x, nullptr); }
    for (int i = 0; i < kMotifs; ++i) if (m.motifs[(size_t) i].active()) { const auto& mo = m.motifs[(size_t) i]; juce::ValueTree x("MOTIF"); x.setProperty("track", mo.track, nullptr); x.setProperty("start", mo.start, nullptr); x.setProperty("len", mo.len, nullptr);
        juce::MemoryBlock mb(mo.steps.data(), sizeof(mo.steps)); x.setProperty("steps", mb.toBase64Encoding(), nullptr); t.appendChild(x, nullptr); }
    for (auto& sc : m.scripts) { juce::ValueTree x("SCRIPT"); x.setProperty("name", sc.name, nullptr); x.setProperty("code", sc.code, nullptr); x.setProperty("on", sc.on, nullptr); t.appendChild(x, nullptr); }
    for (auto& b : m.song) { juce::ValueTree x("BLOCK"); x.setProperty("p", b.pattern, nullptr); x.setProperty("start", b.startBar, nullptr); x.setProperty("bars", b.bars, nullptr); t.appendChild(x, nullptr); }
    return t;
}
inline void modelFromTree(Model& m, const juce::ValueTree& t, double rate) {
    m.kit = t.getProperty("kit", "Trap").toString(); m.song.clear(); m.locks = {}; m.motifs = {}; m.scripts.clear(); m.scTrack = juce::jlimit(-1, kTracks - 1, (int) t.getProperty("sc", -1));
    m.ver = juce::jlimit(1, kStateVersion, (int) t.getProperty("ver", 3));   // a project without a version is a v1 project: it keeps the v1 sound
    std::vector<std::pair<int, int>> sampleRefs;
    m.arc = defaultArc(); { auto a = juce::StringArray::fromTokens(t.getProperty("arc").toString(), ",", ""); if (a.size() == 16) for (int i = 0; i < 16; ++i) m.arc[(size_t) i] = (uint8_t) juce::jlimit(0, 100, a[i].getIntValue()); }
    m.converge = juce::jlimit(0, 64, (int) t.getProperty("converge", 0)); m.toolSeed = (int) t.getProperty("toolseed", 1); m.drillGrit = juce::jlimit(0, 100, (int) t.getProperty("grit", 0));
    for (auto x : t) {
        if (x.hasType("TRACK")) {
            const int i = x.getProperty("i"); if (!juce::isPositiveAndBelow(i, kTracks)) continue;
            auto& tr = m.tracks[(size_t) i]; tr = Track();
            auto F = [&](const char* k, float d) { return (float) (double) x.getProperty(k, (double) d); };
            tr.sound = x.getProperty("sound").toString(); tr.pitch = F("pitch", 0); tr.decay = F("decay", 1);
            tr.vol = F("vol", 0.8f); tr.pan = F("pan", 0); tr.tone = F("tone", 0.5f); tr.drive = F("drive", 0);
            tr.reverse = x.getProperty("rev", false); tr.mute = x.getProperty("mute", false); tr.solo = x.getProperty("solo", false); tr.choke = x.getProperty("choke", 0);
            tr.swing = F("swing", 0); tr.accent = F("accent", 0); tr.human = F("human", 0); tr.tAttack = F("tatt", 0); tr.tSustain = F("tsus", 0); tr.sat = F("sat", 0);
            tr.comp = F("comp", 0); tr.lowCut = F("lowcut", 0); tr.tilt = F("tilt", 0); tr.highCut = F("highcut", 1); tr.duck = F("duck", 0);
            tr.character = juce::jlimit(0, 3, (int) x.getProperty("chmode", 0)); tr.charAmount = juce::jlimit(0.0f, 1.0f, F("chamt", 1)); tr.charDrive = juce::jlimit(0.0f, 1.0f, F("chdrv", 0));
            tr.glide = juce::jlimit(0.0f, 2000.0f, F("glide", 0)); tr.attack = juce::jlimit(0.0f, 500.0f, F("attack", 0)); tr.pitchEnv = juce::jlimit(-24.0f, 24.0f, F("penv", 0)); tr.pitchEnvTime = juce::jlimit(1.0f, 2000.0f, F("ptime", 60)); tr.velAmt = juce::jlimit(0.0f, 1.0f, F("velamt", 1));
            tr.pitch = juce::jlimit(-24.0f, 24.0f, tr.pitch); tr.decay = juce::jlimit(0.05f, 1.0f, tr.decay); tr.vol = juce::jlimit(0.0f, 2.0f, tr.vol); tr.pan = juce::jlimit(-1.0f, 1.0f, tr.pan); tr.tone = juce::jlimit(0.0f, 1.0f, tr.tone); tr.drive = juce::jlimit(0.0f, 1.0f, tr.drive); tr.choke = juce::jlimit(0, 4, tr.choke);
            for (int l = 0; l < kLayers; ++l) { auto& L = tr.layers[(size_t) l]; const juce::String k = "l" + juce::String(l);
                L.sound = x.getProperty(k + "s").toString(); L.level = (float) (double) x.getProperty(k + "v", 0.7); L.pitch = (float) (double) x.getProperty(k + "p", 0.0); L.offset = (float) (double) x.getProperty(k + "o", 0.0); }
            if (x.hasProperty("flac")) { const auto v = x.getProperty("flac"); tr.sample = v.isBinaryData() ? soundFromFlac(*v.getBinaryData(), rate, tr.sound, (double) x.getProperty("sbeats", 0.0)) : soundFromFlacBase64(v.toString(), rate, tr.sound, (double) x.getProperty("sbeats", 0.0)); }
            else if (x.hasProperty("sample")) tr.sample = soundFromBase64(x.getProperty("sample").toString(), (double) x.getProperty("srate", 44100.0), rate, tr.sound, (double) x.getProperty("sbeats", 0.0), x.getProperty("sampleR").toString());   // v1 / v2 projects
            else if (x.hasProperty("sampleref")) sampleRefs.push_back({ i, (int) x.getProperty("sampleref") });
            tr.lock = x.getProperty("lock", false);
            { auto a = juce::StringArray::fromTokens(x.getProperty("rules").toString(), ",", ""); for (int k = 0; k < juce::jmin(4, a.size()); ++k) { auto f = juce::StringArray::fromTokens(a[k], ":", "");
                if (f.size() == 4) tr.rules[(size_t) k] = { (uint8_t) juce::jlimit(0, 8, f[0].getIntValue()), (uint8_t) juce::jlimit(0, kTracks - 1, f[1].getIntValue()), (uint8_t) juce::jlimit(1, 64, f[2].getIntValue()), (uint8_t) juce::jlimit(0, 100, f[3].getIntValue()) }; } }
            tr.regStart = juce::jlimit(0.0f, 0.998f, F("rstart", 0)); tr.regEnd = juce::jlimit(tr.regStart + 0.002f, 1.0f, F("rend", 1)); tr.loopStart = juce::jlimit(0.0f, 1.0f, F("lstart", 0)); tr.loopEnd = juce::jlimit(tr.loopStart, 1.0f, F("lend", 1)); tr.loopOn = (bool) x.getProperty("loop", false) && tr.loopEnd - tr.loopStart > 0.001f;
            if (x.hasProperty("slices")) { for (auto& v : juce::StringArray::fromTokens(x.getProperty("slices").toString(), ",", "")) if (tr.slices.size() < 64) tr.slices.push_back(juce::jmax(0, v.getIntValue())); std::sort(tr.slices.begin(), tr.slices.end()); tr.chopSens = (float) (double) x.getProperty("chopsens", 0.5); }
        } else if (x.hasType("PATTERN")) {
            const int i = x.getProperty("i"); if (!juce::isPositiveAndBelow(i, kPatterns)) continue;
            auto& p = m.patterns[(size_t) i]; p.clear();
            p.length = juce::jlimit(1, kMaxSteps, (int) x.getProperty("len", 16)); p.res = juce::jlimit(0, 5, (int) x.getProperty("res", 1));
            trackLenFromString(p, x.getProperty("tlen").toString());
            if (!(x.hasProperty("steps4") && patternFromString(p, x.getProperty("steps4").toString()))) if (!(x.hasProperty("steps3") && patternFromString(p, x.getProperty("steps3").toString()))) patternFromString(p, x.getProperty("steps").toString());
            p.evolve = juce::jlimit(0.0f, 1.0f, (float) (double) x.getProperty("evolve", 0.0)); p.evolveStyle = juce::jlimit(0, 1, (int) x.getProperty("evstyle", 0)); p.fillEvery = juce::jlimit(0, 64, (int) x.getProperty("fill", 0)); p.seed = (int) x.getProperty("seed", 1);
        } else if (x.hasType("LOCK")) { for (auto& L : m.locks) if (!L.active()) { L.start = juce::jlimit(0, 15, (int) x.getProperty("start", 0)); L.bars = juce::jlimit(0, 16, (int) x.getProperty("bars", 0)); L.mask = (uint16_t) (int) x.getProperty("mask", 0xffff); break; } }
        else if (x.hasType("MOTIF")) { for (auto& mo : m.motifs) if (!mo.active()) { mo.track = juce::jlimit(0, kTracks - 1, (int) x.getProperty("track", 0)); mo.start = juce::jlimit(0, 15, (int) x.getProperty("start", 0)); mo.len = juce::jlimit(0, 16, (int) x.getProperty("len", 0));
                juce::MemoryBlock mb; if (mb.fromBase64Encoding(x.getProperty("steps").toString()) && mb.getSize() == sizeof(mo.steps)) std::memcpy(mo.steps.data(), mb.getData(), sizeof(mo.steps)); else mo.len = 0; break; } }
        else if (x.hasType("SCRIPT")) { if ((int) m.scripts.size() < kScripts) m.scripts.push_back({ x.getProperty("name").toString(), x.getProperty("code").toString(), (bool) x.getProperty("on", true) }); }
        else if (x.hasType("BLOCK")) { if (m.song.size() < 128) m.song.push_back({ juce::jlimit(0, kPatterns - 1, (int) x.getProperty("p")), juce::jlimit(0, 511, (int) x.getProperty("start")), juce::jlimit(1, 128, (int) x.getProperty("bars", 1)) }); }
    }
    for (auto& r : sampleRefs) if (juce::isPositiveAndBelow(r.second, kTracks) && m.tracks[(size_t) r.second].sample) m.tracks[(size_t) r.first].sample = m.tracks[(size_t) r.second].sample;
    for (auto& tr : m.tracks) { for (auto& L : tr.layers) { L.level = juce::jlimit(0.0f, 1.5f, L.level); L.pitch = juce::jlimit(-24.0f, 24.0f, L.pitch); L.offset = juce::jlimit(0.0f, 100.0f, L.offset); }
        if (tr.sample) for (int v : tr.slices) if (v >= (int) tr.sample->data.size()) { tr.slices.clear(); break; } }
}

} // namespace gb
#include "GenreKits.h"
