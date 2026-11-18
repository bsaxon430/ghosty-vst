#pragma once
// GhostSynth: the music synths of Block Step (🔊 ACID page) and Ghost House (🎛 SYNTH page).
//   303 ACID / 303 SQUARE  - Open303 (Robin Schmidt, MIT; third_party/open303-mit): the TB-303 voice, slide + accent like the box
//   101 BASS               - Plaits virtual-analog saw (Emilie Gillet, MIT; third_party/eurorack-mit) + a sub square, Open303's
//                            24 dB ladder, a filter envelope: an SH-101 style mono bass
//   FM BASS / FM BELL      - Plaits 2-op FM, its brightness falling with the note (the DX side of Aphex)
//   SUPERSAW               - two Plaits swarms (8 saws each, its own random stream per side = wide stereo), ladder filter, ADSR
//   PLUCK / STAB           - Plaits VA saws through the ladder with a fast filter envelope (deadmau5 plucks, house chord stabs)
// Mono sounds (303, 101, FM bass) play one note per channel and SLIDE into the next note when asked; the rest are polyphonic.
// Knobs (0..1 each): CUTOFF, RESO, ENV, DECAY, ACCENT, DRIVE per note, plus the bus's SPACE (reverb) and ECHO (tempo delay).
// Audio thread: no allocation, no locks. Everything is built in prepare().
#include <cstdint>
#include <memory>

namespace gsyn {

enum Sound : int { S_ACID = 0, S_ACIDSQ, S_SAW101, S_FMBASS, S_FMBELL, S_SUPERSAW, S_PLUCK, S_STAB, kSounds };
inline bool isMono(int s) { return s == S_ACID || s == S_ACIDSQ || s == S_SAW101 || s == S_FMBASS; }
inline int baseNote(int s) { return s == S_SUPERSAW || s == S_PLUCK || s == S_STAB || s == S_FMBELL ? 60 : 36; }   // p = 0 plays this MIDI note

struct Knobs { float cut = 0.45f, res = 0.6f, env = 0.55f, dec = 0.35f, acc = 0.6f, drv = 0.25f; };
Knobs defaultKnobs(int sound);

class Bus {
public:
    Bus(); ~Bus();
    static constexpr int kLanes = 16;
    void prepare(double sampleRate, int maxBlock);   // message thread
    void reset();                                      // all notes off, tails cleared
    void need303();                                    // message thread: build the 303 voices (once; a pattern with 303 notes calls it)
    // a note: starts at sample t (absolute, the engine's clock), its gate ends at off (a slid note's gate reaches the next note)
    void note(int64_t t, int64_t off, int lane, int sound, int midi, float vel, bool accent, bool slide, float pan, const Knobs& k, float gain = 1.0f);   // gain: the MIX level (0 = silent), separate from velocity
    void setFx(float space, float echo, double bpm);
    // adds into L / R: n samples starting at absolute sample `now`
    void render(float* L, float* R, int n, int64_t now);
    int activeVoices() const;
    bool silent() const;
private:
    struct Impl; std::unique_ptr<Impl> d;
};

} // namespace gsyn
