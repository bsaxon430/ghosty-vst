#pragma once
// MELODIC TRACKER (Ghosty Beats): tracker channels that play sounds as pitched instruments.
// Columns per channel: NOTE (C-0..G-9, === = note off), INSTRUMENT (01..10 hex = 1..16), VOLUME (00..40 hex = 0..64), EFFECT + PARAM.
// Effects (00 = use the last value of that effect on this channel, "effect memory"):
//   0xy ARPEGGIO    note, +x, +y semitones, one step per tick
//   1xx SLIDE UP    xx/16 semitone per tick           2xx SLIDE DOWN          (continuous across the whole row: SPEED ticks)
//   3xx GLIDE       slide to the row's note at xx/16 st per tick, no retrigger (00 = last speed; a fresh channel starts at 10)
//   4xy VIBRATO     x/64 cycle per tick, depth y/8 semitone          7xy TREMOLO  x/64 cycle per tick, depth y/15 of the volume
//   8xx PAN         00 left, 80 centre, FF right                    Pxy PAN SLIDE  right x / left y per tick (1/64 of the field)
//   9xx OFFSET      start at xx/256 of the sound                    Rxx REVERSE    R01 backwards, R00 forwards (from where it is)
//   Axy VOL SLIDE   +x or -y per tick                               Exy FINE       E1x/E2x pitch +-x/16 st once, EAx/EBx volume +-x once
//   Qxy RETRIG      every y ticks, x = volume change (1..5: -1 -2 -4 -8 -16, 6 x2/3, 7 x1/2, 9..D: +1 +2 +4 +8 +16, E x3/2, F x2)
//   Cxx NOTE CUT    at tick xx (C00 on a note row = that note stays silent)          Dxx NOTE DELAY  xx ticks
//   Zxx CUTOFF      low-pass 00 dark .. FF open                    Yxx RESONANCE  00 .. FF
//   Fxx SPEED       ticks per row from this row on (01..1F; the row length stays 1/LPB beat, so the tracker stays on the beat grid)
//   Bxx JUMP        OWN ORDER only: after this row, continue at order position xx
// Timing: a row is 1/LPB beat (LPB 4 = 1/16 rows), SPEED ticks per row. Everything is computed from the beat position, so any block
// size, offline renders and the MIDI export agree. Real-time safe: no allocation, no locks in Player::process.
// ORDER: by default melody pattern i plays with drum pattern i (and follows the SONG blocks); OWN ORDER plays the melody's own sequence
// of patterns (with Bxx jumps) from the start, independent of the drum patterns and the song.
// INSTRUMENTS own their sound (a library sound by name, or a sample kept in the song): a kit change never touches them. Each plays
// through a ROUTE track (its mixer strip, chain and PITCH), has ROOT / TUNE / VOL / PAN, an ADSR volume envelope, START / END, a
// sustain LOOP (crossfaded) and a low-pass CUTOFF / RESONANCE.
// Voices: per sound, decimated "mip" levels are prepared off the audio thread (63-tap half-band, -90 dB); a voice reads the level
// whose rate is <= ~1 with a cheap 24-tap polyphase sinc, and from +0 to +12 st the full sound with a pitch-following windowed sinc,
// so nothing aliases at any pitch; an exact sample copy when a note plays its root unpitched; level changes and every cut / steal /
// retrigger fade over ~2 ms.
#include "BeatEngine.h"
#include <memory>

namespace gb::mel {

constexpr int kChannels = 8, kRows = 128, kInstruments = 16, kPats = kPatterns, kOrderMax = 128, kMaxLevels = 7;
constexpr uint8_t kNoteOff = 255, kNoVol = 255;   // note 1..128 = MIDI note 0..127; 0 = empty
// (the numbers are saved raw: new effects are appended)
enum Fx : uint8_t { FxNone = 0, FxArp, FxSlideUp, FxSlideDown, FxGlide, FxVibrato, FxPan, FxOffset, FxVolSlide, FxRetrig, FxCut, FxDelay,
                    FxTremolo, FxJump, FxFine, FxSpeed, FxPanSlide, FxReverse, FxReso, FxCutoff, FxCount };
inline char fxChar(int fx) { static const char c[] = "-0123489AQCD7BEFPRYZ"; return fx >= 0 && fx < FxCount ? c[fx] : '?'; }
inline int fxFromChar(juce::juce_wchar ch) { const auto u = juce::CharacterFunctions::toUpperCase(ch); for (int i = 1; i < FxCount; ++i) if (fxChar(i) == (char) u) return i; return -1; }
inline const char* fxName(int fx) {
    static const char* n[] { "", "ARPEGGIO", "SLIDE UP", "SLIDE DOWN", "GLIDE TO NOTE", "VIBRATO", "PAN", "SAMPLE OFFSET", "VOLUME SLIDE", "RETRIG", "NOTE CUT", "NOTE DELAY",
                             "TREMOLO", "JUMP", "FINE", "SPEED", "PAN SLIDE", "REVERSE", "RESONANCE", "CUTOFF" };
    return fx >= 0 && fx < FxCount ? n[fx] : "";
}
inline const char* fxHelp(int fx) {
    static const char* h[] { "no effect",
        "0xy  plays note, +x, +y semitones, one step per tick (00 = last)",
        "1xx  pitch up xx/16 semitone per tick, continuous over the row (00 = last)",
        "2xx  pitch down xx/16 semitone per tick, continuous over the row (00 = last)",
        "3xx  slide to this row's note at xx/16 st per tick, no retrigger (00 = last; starts at 10)",
        "4xy  vibrato: x = speed (x/64 cycle per tick), y = depth (y/8 semitone) (0 = last)",
        "8xx  pan: 00 left, 80 centre, FF right",
        "9xx  start the sound at xx/256 of its length",
        "Axy  volume up x or down y per tick (00 = last)",
        "Qxy  retrigger every y ticks; x changes the volume each time (00 = last)",
        "Cxx  cut the note at tick xx (C00 on a note row: silent)",
        "Dxx  delay the row's note by xx ticks",
        "7xy  tremolo: x = speed (x/64 cycle per tick), y = depth (y/15 of the volume)",
        "Bxx  OWN ORDER: after this row, continue at order position xx",
        "Exy  E1x / E2x fine pitch +- x/16 st, EAx / EBx fine volume +- x (once, on the row), E60 / E6x pattern loop: back to E60, x times",
        "Fxx  01..1F ticks per row from here; 20..FF tempo in BPM (SYNC: OWN sets Beats' tempo; SYNC: DAW: a marker only, the DAW owns the tempo)",
        "Pxy  pan slide: right x or left y per tick (00 = last)",
        "Rxx  R01 plays backwards from where it is, R00 forwards again",
        "Yxx  filter resonance 00..FF",
        "Zxx  low-pass cutoff 00 (dark) .. FF (open)" };
    return fx >= 0 && fx < FxCount ? h[fx] : "";
}
inline juce::String noteName(int cellNote) {
    if (cellNote == 0) return "---"; if (cellNote == kNoteOff) return "===";
    static const char* n[] { "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-" };
    const int m = cellNote - 1; return juce::String(n[m % 12]) + juce::String(juce::jmax(0, m / 12 - 1)).substring(0, 1);   // C-4 = MIDI 60
}

struct Cell { uint8_t note = 0, inst = 0, vol = kNoVol, fx = 0, param = 0; bool empty() const { return !note && !inst && vol == kNoVol && !fx; } };
static_assert(sizeof(Cell) == 5, "Cell layout is saved raw");
struct Pattern { int rows = 64; std::array<std::array<Cell, kRows>, kChannels> cells {}; };

struct Instrument {
    juce::String name;                 // shown in the instrument list ("" = the sound's name)
    juce::String sound;                // a library sound by name ...
    SoundPtr sample;                   // ... or the instrument's own sample (a dropped file, or a track's sample copied in; saved as FLAC)
    int route = -1;                    // the track whose mixer strip, chain and PITCH it plays through (-1 = not set up)
    std::vector<int> slices; int slice = 0;   // a chopped sound: play slice n (1..) of these start positions (0 = the whole sound)
    int root = 60;                     // the MIDI note at which the sound plays unpitched (C-4)
    float tune = 0, vol = 0.8f, pan = 0;
    float attack = 0, decay = 0, sustain = 1, release = 0.03f;   // volume envelope: seconds (sustain = level 0..1)
    float start = 0, end = 1;          // sample start / end (fractions)
    bool loop = false; float loopStart = 0.5f, loopEnd = 1.0f;    // forward sustain loop (fractions), crossfaded
    float cutoff = 1, reso = 0;        // low-pass (1 = open)
    bool followPitch = true;           // the route track's PITCH (+ its automation) transposes the instrument too
    int src = -1;                      // (older projects: the track it followed; turned into sound + route on load)
    bool hasSound() const { return sample != nullptr || sound.isNotEmpty(); }
    bool used() const { return route >= 0 && hasSound(); }
    int routeTrack() const { return juce::jlimit(0, kTracks - 1, route < 0 ? 0 : route); }
    juce::String label() const { return name.isNotEmpty() ? name : sample ? sample->name : sound; }
};

// the song's row timeline: speed per row (Fxx) and, in OWN ORDER, the sequence of pattern rows with Bxx jumps applied
struct Timeline {
    std::array<std::array<uint8_t, kRows>, kPats> speed {};
    std::array<std::vector<uint8_t>, kPats> seq;   // E6x pattern loops: the rows in play order (empty = 0..rows-1)
    struct Seg { int order = 0, pat = 0, row0 = 0, row1 = 0; long abs0 = 0; };
    std::vector<Seg> segs; long total = 0, loopFrom = 0;   // own order: after `total` rows it continues at `loopFrom`
};

struct Song {
    bool on = false; int lpb = 4, speed = 6;
    bool ownOrder = false; std::vector<uint8_t> order { 0 };   // OWN ORDER: the melody's own pattern sequence
    std::array<Instrument, kInstruments> inst {};
    std::unique_ptr<std::array<Pattern, kPats>> pats = std::make_unique<std::array<Pattern, kPats>>();
    std::shared_ptr<const Timeline> timeline;   // prepare() (the snapshot copy is prepared at publish; offline renders prepare a copy)
    Song() = default;
    Song(const Song& o) : on(o.on), lpb(o.lpb), speed(o.speed), ownOrder(o.ownOrder), order(o.order), inst(o.inst), pats(std::make_unique<std::array<Pattern, kPats>>(*o.pats)), timeline(o.timeline) {}
    Song& operator=(const Song& o) { if (this != &o) { on = o.on; lpb = o.lpb; speed = o.speed; ownOrder = o.ownOrder; order = o.order; inst = o.inst; *pats = *o.pats; timeline = o.timeline; } return *this; }
    Pattern& pat(int i) { return (*pats)[(size_t) juce::jlimit(0, kPats - 1, i)]; }
    const Pattern& pat(int i) const { return (*pats)[(size_t) juce::jlimit(0, kPats - 1, i)]; }
    double rowBeats() const { return 1.0 / juce::jlimit(1, 16, lpb); }
    double tickBeats() const { return rowBeats() / juce::jlimit(1, 32, speed); }
    void prepare();
    double orderBeats() const;   // OWN ORDER: one pass of the sequence (until it loops), in beats
    int usedChannels(int pattern) const;
};

// What one channel does during one tick: the ONE description the audio voices and the MIDI export both follow.
struct ChanCtl {
    bool trigger = false, cut = false, release = false, active = false, reverse = false, flip = false;
    int inst = -1; float offset = 0;   // trigger: instrument (0-based), start fraction
    float p0 = 60, p1 = 60;            // pitch (MIDI note, fractional) at the start / end of the tick (slides, glide), before arp / vibrato
    int arp = 0;                       // arpeggio offset this tick (semitones)
    float vib = 0, vph0 = 0, vphInc = 0;   // vibrato depth (semitones peak), phase at the tick start and per tick (cycles)
    float trem = 0, tph0 = 0, tphInc = 0;  // tremolo depth (0..1), phase
    float v0 = 1, v1 = 1, pan0 = 0, pan1 = 0;   // volume 0..1 and pan -1..1 at the start / end of the tick
    float cutoff = 1, reso = 0;        // channel filter (Zxx / Yxx; the instrument's on a new note)
    float pitchAt(float u) const { return p0 + (p1 - p0) * u + (float) arp + (vib != 0 ? vib * std::sin(juce::MathConstants<float>::twoPi * (vph0 + vphInc * u)) : 0.0f); }
    float volAt(float u) const { float v = v0 + (v1 - v0) * u; if (trem > 0) v *= 1.0f - trem * 0.5f * (1.0f - std::sin(juce::MathConstants<float>::twoPi * (tph0 + tphInc * u))); return v; }
};

struct Sequencer {
    struct Chan { bool active = false, reverse = false; int inst = -1; float pitch = 60, target = 60, vol = 64, pan = 0, vibSpeed = 0, vibDepth = 0, vibPhase = 0, tremSpeed = 0, tremDepth = 0, tremPhase = 0, offset = 0, cutoff = 1, reso = 0;
                  uint8_t fx = 0, param = 0; Cell cell {}; std::array<uint8_t, FxCount> mem {}; bool silentRow = false; };
    std::array<Chan, kChannels> ch {};
    int tempo = 0;   // an F20..FF tempo command on the row that just started (0 = none)
    void reset() { ch = {}; tempo = 0; for (auto& c : ch) c.mem[FxGlide] = 0x10; }
    Sequencer() { reset(); }
    void tick(const Song& s, const Pattern& p, int row, int tick, int speed, std::array<ChanCtl, kChannels>& out);
};

// one tick of the timeline (false = silence)
struct TickPos { int pat = 0; double origin = 0; long rowAbs = 0; int row = 0, tick = 0, speed = 6; double rowStart = 0; };
bool tickAt(const Song& s, const Snapshot* S, bool songMode, int pattern, double beat, TickPos& out);
// E6x pattern loops of one pattern: the rows in play order (ProTracker: E60 sets the loop start, E6x plays back to it x more times); empty = none
std::vector<uint8_t> loopRows(const Pattern& pt);
inline bool nextTick(const TickPos& a, const TickPos& b) {   // is b the tick right after a?
    if (a.pat != b.pat || a.origin != b.origin) return false;
    return (b.rowAbs == a.rowAbs && b.tick == a.tick + 1) || (b.rowAbs == a.rowAbs + 1 && b.tick == 0 && a.tick == a.speed - 1);
}

// decimated copies of a sound (level k = 1/2^k of the rate), made off the audio thread; level 0 is the sound itself
struct Mips {
    int levels = 1; std::array<std::vector<float>, kMaxLevels> L, R;   // [0] unused (the sound's own data)
    static std::shared_ptr<const Mips> make(const Sound& s);
};

// Kaiser-windowed sinc readers
struct HiSinc {   // pitch-following (stretch >= 1 widens the kernel: anti-aliased when pitching up)
    static constexpr int HALF = 8, PH = 256, N = HALF * PH + 2; static constexpr double kCut = 0.86; static constexpr int kMaxSpan = 64;
    static const float* table();
    static int weights(double p, double stretch, float* w, int& first);   // stretch is clamped to 2.5
};
struct FastSinc { // fixed (rate <= 1): 24 taps, precomputed polyphase rows
    static constexpr int HALF = 12, TAPS = 2 * HALF, PH = 256;
    static const float* table();   // (PH + 1) rows x TAPS
    static void weights(double frac, float* w);
};

// The real-time player (one per engine). Channels are monophonic like a classic tracker; each keeps 3 voice slots so a
// retrigger / note change fades the old voice out while the new one starts. Channel kChannels is the AUDITION voice.
struct Player {
    void prepare(double rate);
    void reset();
    // add [b0, b0 + n / spb) into the route tracks' stereo buffers (lr[2t], lr[2t+1]; null pointers are skipped).
    // mips (may be null) = per instrument decimated levels; P (may be null) = the route tracks' PITCH automation.
    void process(const Song& s, const Sound* const* snd, const Mips* const* mips, const Snapshot* S, const EngineParams* P, bool songMode, int pattern,
                 double b0, double spb, int n, float* const* lr, bool running);
    // audition (message thread -> audio thread, lock-free): note >= 0 starts instrument `inst` at that note, note < 0 releases it
    void audition(int inst, int note, float vel = 0.85f);
    uint32_t triggers = 0;             // (tests) voices started
    int tempoOut = 0;                  // the last F20..FF tempo command played (the engine takes it)
    int activeVoices() const;
    int levelOf(int c) const;          // (tests) the mip level channel c's voice reads

    struct Voice {
        const Sound* snd = nullptr; const Mips* mips = nullptr; double pos = 0, rate = 1, srcRatio = 1; int len = 0, a = 0, z = 0, route = 0, inst = 0, dir = 1;
        float gain = 0, target = 0, ivol = 1, ipan = 0, panL = 0.7071f, panR = 0.7071f, root = 60, tune = 0, trackPitch = 0;
        int fade = -1, fadeLen = 96, fadeIn = 0, fadeInLen = 96; bool loop = false; double ls = 0, le = 0, xf = 0; bool owned = false; uint32_t cnt = 0;
        int level = 0, prevLevel = -1, xfade = 0;      // mip level (+ the one being faded out)
        int envStage = 0; float env = 1, envAtt = 1, envDec = 0, envSus = 1, envRel = 0;   // 0 attack 1 decay 2 sustain 3 release 4 done
        float cut = 1, cutTarget = 1, reso = 0, f1[2] {}, f2[2] {}, fa1 = 0, fa2 = 0, fa3 = 0, fk = 2; bool filt = false;
    };
private:
    double sr = 48000; int f2 = 96;
    std::array<std::array<Voice, 3>, kChannels + 1> v {}; std::array<int, kChannels + 1> cur {};
    Sequencer seq; std::array<ChanCtl, kChannels> ctl {}; ChanCtl prevCtl {};
    bool have = false; TickPos last; double tickStart = 0, tickLen = 1;
    struct Aud { int inst = 0, note = -1; float vel = 0.85f; };
    juce::AbstractFifo audFifo { 64 }; std::array<Aud, 64> audBuf {};
    void start(int c, const Song& s, const Sound* const* snd, const Mips* const* mips, const Snapshot* S, const EngineParams* P, int inst, float offset, float v0, float pan, bool reverse);
    void apply(const Song& s, const Sound* const* snd, const Mips* const* mips, const Snapshot* S, const EngineParams* P);
    void stopChannel(int c, int len);
    void releaseChannel(int c, const Song& s);
    void render(int c, const ChanCtl& o, double b0, double spb, int i0, int i1, float* const* lr);
};

// offline: the same Player into one stereo buffer (n = beats + tail)
juce::AudioBuffer<float> renderAudio(const Song& s, const Sound* const* snd, const Snapshot* S, bool songMode, int pattern, double sr, double bpm, double beats, double tailSec = 1.0);
// DRAG MIDI: one track + MIDI channel per tracker channel; RPN bend range, bends for slides / glide / vibrato, notes for arp / retrig,
// CC11 volume, CC74 / CC71 cutoff / resonance; velocity includes the instrument's VOL
juce::MidiFile toMidi(const Song& s, const Snapshot* S, bool songMode, int pattern, double bpm, double beats, int bendRange = 24);

juce::ValueTree toTree(const Song& s);
void fromTree(Song& s, const juce::ValueTree& t, double rate = 48000.0);
// turn an older instrument (src = a track index) into one that owns its sound: the track's sound / sample, routed to that track
void migrate(Song& s, const std::array<Track, kTracks>& tracks);
// pick a track's current sound for instrument i (copies the sound reference + slices; routes the instrument to that track)
void takeTrackSound(Instrument& in, const Track& tr, int t);
// a stable hash of everything that changes the sound (for render caches)
void hashInto(const Song& s, const std::function<void(const void*, size_t)>& mix);

} // namespace gb::mel
