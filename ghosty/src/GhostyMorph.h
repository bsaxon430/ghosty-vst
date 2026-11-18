#pragma once
// MORPH (Ghosty Pro's SPIRIT BOARD on Vital's engine). Ghosts on a 2-D board each hold the knob settings of a sound
// (the current sound when the ghost was made, or any .vital preset's settings); the puck blends them.
//
//   continuous Vital controls: blended per block on the audio thread in the control's own value domain (the knob
//       position Vital stores), slewed (30 ms to cross the whole range); a ghost alone gives exactly its own values.
//   switches and choices (Vital's indexed controls): the ghost with the highest weight decides.
//   wavetables, samples, LFO shapes and modulation routings: those of the loaded sound (the board only moves knobs).
// Values are written with vital::Value::set, the same call Vital's own SynthBase::valueChanged makes. Nothing
// allocates on the audio thread: the compiled set is handed over by pointer (pending -> live -> trash, freed on the
// message thread). OFF by default: while off, nothing is written.
#include "JuceHeader.h"
#include "SpiritBoard.h"
#include "synth_types.h"
#include "load_save.h"
#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace ghosty {

struct MorphSet {
  int ghosts = 0, slots = 0;
  std::vector<float> values;   // ghosts x slots
};

class Morph {
  public:
    Morph() = default;
    ~Morph() { delete pending_.exchange(nullptr); delete trash_.exchange(nullptr); delete live_; }

    // message thread, once: the controls of the synth this board moves
    void init(vital::control_map& controls);
    int numSlots() const { return (int) names_.size(); }
    const std::string& slotName(int s) const { return names_[(size_t) s]; }
    int slotOf(const std::string& name) const { for (size_t s = 0; s < names_.size(); ++s) if (names_[s] == name) return (int) s; return -1; }

    // message thread: a sound's values for every slot (from the live controls or from a preset's "settings")
    std::vector<float> captureCurrent() const;
    std::vector<float> captureFromPresetJson(const String& preset_text) const;

    // the board (message thread) and each ghost's values, keyed by the ghost's id
    ghosty::spirit::Board board;
    std::map<String, std::vector<float>> ghost_values;
    std::map<String, String> ghost_names;     // where each ghost's sound came from
    void publish();                           // after any edit: hands the compiled set + positions to the audio thread
    void collect() { delete trash_.exchange(nullptr, std::memory_order_acq_rel); }

    std::atomic<bool> enabled { false };
    std::atomic<float> puck_x { 0.5f }, puck_y { 0.5f };
    ghosty::spirit::SharedBoard shared;

    // MOTION: how the puck moves by itself
    //   STILL   where you put it            CIRCLE / EIGHT / WANDER   around where you put it (SIZE = how far)
    //   GLIDE   smoothly from ghost to ghost, in order (A -> B -> C ... -> A), one leg per period
    //   STEP    one ghost at a time, jumping to the next each period (Vital's knobs still glide over ~30 ms: no clicks)
    //   DRAWN   along a path you draw on the board, one loop per period
    // The period is a tempo division locked to the host's beat position (1/16 triplet .. 8 bars) or, with SYNC off, a rate in Hz.
    enum Motion { kStill = 0, kCircle, kEight, kWander, kGlide, kStep, kDrawn, kNumMotions };
    static const char* motionName(int m);
    struct Division { const char* name; double beats; };
    static const std::vector<Division>& divisions();
    std::atomic<int> motion { kStill };
    std::atomic<int> division { 12 };          // index into divisions() (12 = 1 BAR)
    std::atomic<bool> sync { true };
    std::atomic<float> hz { 0.25f };           // periods per second with SYNC off
    std::atomic<float> depth { 0.25f };        // CIRCLE / EIGHT / WANDER size (board units, 0 .. 0.5)
    std::atomic<double> drawn_beats { 4.0 };   // (the drawn path's own loop length; the period rescales it)
    void setDrawnPath(const ghosty::spirit::OrbitPath& path);   // message thread
    bool hasDrawnPath() const { return !board.paths[0].empty(); }
    // where the puck is at a time (beats when synced and playing, else seconds); audio thread
    juce::Point<float> puckAt(double periods, const float* xs, const float* ys, int n) const noexcept;

    // message thread, a picture only: where the puck would be now when no audio block has run lately (a host that suspends a silent
    // instrument, e.g. FL Studio's smart disable, or a stopped project): the same motion on the UI's own clock at the last tempo
    juce::Point<float> idlePuck() const;

    // audio thread, once per block before the engine runs; true when it wrote anything
    bool process(int num_samples, double sample_rate, double ppq, double bpm, bool playing, double free_seconds) noexcept;

    // state
    ValueTree toTree() const;
    void fromTree(const ValueTree& tree);

  private:
    std::vector<std::string> names_;
    std::vector<vital::Value*> values_;
    std::vector<uint8_t> indexed_;
    std::vector<float> lo_, hi_, def_;
    std::vector<float> current_;
    // on/off switches with a level: the switch stays on while any ghost using it has weight (or its level is still
    // gliding out), and that level blends to 0 for ghosts where it is off, so effects and oscillators fade, never cut
    std::vector<int> pair_of_;   // switch slot -> its level slot (-1 = a plain switch)
    MorphSet* live_ = nullptr;
    std::atomic<MorphSet*> pending_ { nullptr }, trash_ { nullptr };
    bool was_on_ = false, snap_ = false;
};

} // namespace ghosty
