#pragma once
// MORPH SETS (owner, 2026-11-16: "morph presets that blend sounds well"): ready-made SPIRIT BOARDS.
// Each set loads one of Ghosty Pro's own presets and puts four ghosts on the board, A holding that sound exactly and B, C, D
// holding variations of ITS knobs (filter, wavetable position, unison, the effects it already uses). The board only moves
// knobs (the wavetables, samples and routings are the loaded sound's), so ghosts made from one sound always blend smoothly.
// Each ghost's VOLUME was set so all four play at the base sound's loudness, and the amount of each change was kept where
// the blend between any two ghosts stays within about 1.5 dB of it (measured offline with Vital's own headless renderer:
// one note, every ghost and every halfway point between two ghosts, ghosty/tools/morph_sets/).
// The set also sets MOTION (and its tempo division), BLEND and SIZE, and turns MORPH on.
#include "JuceHeader.h"
#include <utility>
#include <vector>

class GhostyProcessor;

namespace ghosty {

struct MorphSetGhost {
  const char* name;
  float x, y;
  std::vector<std::pair<const char*, float>> values;   // controls that differ from the base sound (Vital's own value domain)
};

struct MorphSetDef {
  const char* name;
  const char* hint;
  const char* base;    // a Ghosty Pro preset (FactoryPreset::name)
  int motion;          // Morph::Motion
  int division;        // index into Morph::divisions()
  int blend;           // 0 SMOOTH, 1 SOFT ISLANDS, 2 NEAREST 2
  float depth;         // SIZE
  std::vector<MorphSetGhost> ghosts;   // A (the base itself), B, C, D
};

const std::vector<MorphSetDef>& morphSets();

// message thread: load the set's sound, put its four ghosts on the board (replacing the board), set the motion, MORPH on
bool applyMorphSet(GhostyProcessor& proc, int index, String& error);

} // namespace ghosty
