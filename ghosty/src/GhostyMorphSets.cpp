#include "GhostyMorphSets.h"
#include "GhostyProcessor.h"
#include "GhostyPresets.h"

namespace ghosty {

const std::vector<MorphSetDef>& morphSets() {
  static const std::vector<MorphSetDef> sets {
  { "SUB TO SNARL", "sub bass that opens up and snarls", "Rubber Sub", 0, 12, 0, 0.25f,
    {
      { "SUB", 0.25f, 0.25f, { {"volume", 6066.58f} } },
      { "OPEN", 0.75f, 0.25f, { {"filter_1_cutoff", 62.74f}, {"filter_1_resonance", 0.318f}, {"volume", 5906.31f} } },
      { "SNARL", 0.75f, 0.75f, { {"distortion_drive", 6.86f}, {"distortion_mix", 0.902f}, {"filter_1_cutoff", 56.86f}, {"volume", 5228.38f} } },
      { "BITE", 0.25f, 0.75f, { {"filter_1_cutoff", 66.66f}, {"filter_1_resonance", 0.4062f}, {"osc_3_level", 0.2425f}, {"volume", 5930.16f} } }
    } },
  { "REESE ROTATION", "one reese, four colours, gliding", "Neon Reese", 4, 13, 0, 0.25f,
    {
      { "REESE", 0.25f, 0.25f, { {"volume", 6448.09f} } },
      { "TURN", 0.75f, 0.25f, { {"filter_1_cutoff", 63.6f}, {"osc_1_wave_frame", 185.2f}, {"osc_2_wave_frame", 175.0f}, {"volume", 6563.31f} } },
      { "HOLLOW", 0.75f, 0.75f, { {"filter_1_blend", 0.7f}, {"filter_1_cutoff", 65.0f}, {"volume", 6814.43f} } },
      { "SWARM", 0.25f, 0.75f, { {"chorus_dry_wet", 0.6f}, {"filter_1_cutoff", 70.0f}, {"osc_1_unison_detune", 5.44f}, {"osc_2_unison_detune", 4.08f}, {"volume", 6389.48f} } }
    } },
  { "WOBBLE BOARD", "wobble bass, figure 8 every half bar", "Wobble Ghost", 2, 10, 0, 0.35f,
    {
      { "WOB", 0.25f, 0.25f, { {"volume", 6376.02f} } },
      { "CLOSED", 0.75f, 0.25f, { {"filter_1_cutoff", 44.6f}, {"volume", 6376.02f} } },
      { "SCREAM", 0.75f, 0.75f, { {"filter_1_cutoff", 79.6f}, {"filter_1_resonance", 0.616f}, {"volume", 6376.02f} } },
      { "CRUNCH", 0.25f, 0.75f, { {"distortion_drive", 12.1f}, {"filter_1_cutoff", 68.4f}, {"osc_1_distortion_amount", 0.545f}, {"volume", 6101.94f} } }
    } },
  { "ACID SWAP", "acid line: squelch, deep, burn", "Acid Ghost", 2, 12, 0, 0.30f,
    {
      { "ACID", 0.25f, 0.25f, { {"volume", 6913.84f} } },
      { "SQUELCH", 0.75f, 0.25f, { {"filter_1_cutoff", 69.0f}, {"filter_1_resonance", 0.799f}, {"volume", 6414.48f} } },
      { "DEEP", 0.75f, 0.75f, { {"filter_1_cutoff", 43.8f}, {"filter_1_resonance", 0.554f}, {"volume", 7399.44f} } },
      { "BURN", 0.25f, 0.75f, { {"distortion_drive", 18.0f}, {"distortion_mix", 1.0f}, {"filter_1_cutoff", 63.0f}, {"volume", 3989.07f} } }
    } },
  { "HALO SKY", "supersaw lead: wide, soft, spacey", "Supersaw Halo", 4, 14, 0, 0.25f,
    {
      { "HALO", 0.25f, 0.25f, { {"volume", 5972.39f} } },
      { "WIDE", 0.75f, 0.25f, { {"chorus_dry_wet", 0.7f}, {"osc_1_unison_detune", 6.48f}, {"osc_2_unison_detune", 5.04f}, {"volume", 5877.21f} } },
      { "SOFT", 0.75f, 0.75f, { {"filter_1_cutoff", 74.0f}, {"volume", 6020.56f} } },
      { "SPACE", 0.25f, 0.75f, { {"filter_1_cutoff", 92.0f}, {"reverb_dry_wet", 0.55f}, {"volume", 6240.18f} } }
    } },
  { "CHOIR FOG", "choir pad that wanders", "Haunted Choir", 3, 14, 0, 0.30f,
    {
      { "CHOIR", 0.25f, 0.25f, { {"volume", 6234.49f} } },
      { "VOWEL", 0.75f, 0.25f, { {"osc_1_wave_frame", 172.0f}, {"volume", 6281.7f} } },
      { "DUSK", 0.75f, 0.75f, { {"filter_1_cutoff", 70.0f}, {"volume", 6956.0f} } },
      { "CATHEDRAL", 0.25f, 0.75f, { {"chorus_dry_wet", 0.695f}, {"reverb_dry_wet", 0.525f}, {"volume", 6472.62f} } }
    } },
  { "GLASS TIDES", "glassy pad gliding slowly", "Glass Fog", 4, 14, 0, 0.25f,
    {
      { "FOG", 0.25f, 0.25f, { {"volume", 6037.57f} } },
      { "SHIMMER", 0.75f, 0.25f, { {"filter_1_cutoff", 90.0f}, {"osc_3_level", 0.45f}, {"osc_3_wave_frame", 250.0f}, {"volume", 5925.47f} } },
      { "DEEP", 0.75f, 0.75f, { {"filter_1_cutoff", 54.0f}, {"osc_2_level", 0.6f}, {"volume", 5583.66f} } },
      { "SWIRL", 0.25f, 0.75f, { {"osc_1_unison_detune", 1.44f}, {"phaser_dry_wet", 0.9f}, {"volume", 6326.0f} } }
    } },
  { "RHODES MOODS", "electric piano: felt, suitcase, driven", "Ghost Rhodes", 0, 12, 0, 0.25f,
    {
      { "RHODES", 0.25f, 0.25f, { {"volume", 5473.04f} } },
      { "FELT", 0.75f, 0.25f, { {"filter_1_cutoff", 60.0f}, {"volume", 5342.73f} } },
      { "SUITCASE", 0.75f, 0.75f, { {"chorus_dry_wet", 0.75f}, {"osc_2_wave_frame", 140.0f}, {"volume", 5317.07f} } },
      { "DRIVEN", 0.25f, 0.75f, { {"distortion_drive", 24.0f}, {"filter_1_cutoff", 98.0f}, {"volume", 5271.69f} } }
    } },
  { "STAB SWEEP", "chord stab with a filter sweep", "Club Chord Stab", 4, 12, 0, 0.25f,
    {
      { "STAB", 0.25f, 0.25f, { {"volume", 6084.15f} } },
      { "DARK", 0.75f, 0.25f, { {"filter_1_cutoff", 46.0f}, {"volume", 6050.0f} } },
      { "BRIGHT", 0.75f, 0.75f, { {"filter_1_cutoff", 92.0f}, {"filter_1_resonance", 0.6f}, {"volume", 6199.25f} } },
      { "SHIFT", 0.25f, 0.75f, { {"osc_1_wave_frame", 220.0f}, {"osc_2_wave_frame", 40.0f}, {"volume", 6271.37f} } }
    } },
  { "PLUCK SHAPES", "pluck that steps each beat", "Crystal Pluck", 5, 7, 2, 0.25f,
    {
      { "PLUCK", 0.25f, 0.25f, { {"volume", 6226.34f} } },
      { "GLASS", 0.75f, 0.25f, { {"filter_1_cutoff", 56.0f}, {"osc_1_wave_frame", 163.0f}, {"volume", 5920.68f} } },
      { "MUTED", 0.75f, 0.75f, { {"filter_1_cutoff", 33.6f}, {"volume", 6143.54f} } },
      { "ECHO", 0.25f, 0.75f, { {"delay_dry_wet", 0.427f}, {"reverb_dry_wet", 0.39f}, {"volume", 6027.19f} } }
    } },
  { "ORGAN TIDE", "organ pad: drawbars, chapel, jet", "Tidal Organ Pad", 4, 14, 0, 0.25f,
    {
      { "ORGAN", 0.25f, 0.25f, { {"volume", 5602.73f} } },
      { "DRAWBAR", 0.75f, 0.25f, { {"osc_1_wave_frame", 220.0f}, {"osc_2_wave_frame", 40.0f}, {"volume", 5414.14f} } },
      { "CHAPEL", 0.75f, 0.75f, { {"filter_1_cutoff", 70.0f}, {"reverb_dry_wet", 0.55f}, {"volume", 6086.27f} } },
      { "JET", 0.25f, 0.75f, { {"filter_1_cutoff", 98.0f}, {"flanger_dry_wet", 0.5f}, {"volume", 5783.57f} } }
    } },
  { "SEANCE DRIFT", "dark texture drifting over 8 bars", "Seance Texture", 3, 15, 0, 0.35f,
    {
      { "TEXTURE", 0.25f, 0.25f, { {"volume", 7169.48f} } },
      { "TURN", 0.75f, 0.25f, { {"osc_1_wave_frame", 200.0f}, {"volume", 7399.44f} } },
      { "BLOOM", 0.75f, 0.75f, { {"filter_1_cutoff", 73.92f}, {"flanger_dry_wet", 0.5f}, {"osc_1_unison_detune", 5.568f}, {"volume", 7165.48f} } },
      { "DUST", 0.25f, 0.75f, { {"filter_1_cutoff", 61.18f}, {"reverb_dry_wet", 0.498f}, {"volume", 7049.94f} } }
    } }
  };
  return sets;
}

bool applyMorphSet(GhostyProcessor& proc, int index, String& error) {
  const auto& sets = morphSets();
  if (!isPositiveAndBelow(index, (int) sets.size())) { error = "No such morph set."; return false; }
  const auto& S = sets[(size_t) index];
  const auto& list = factoryPresets();
  int base = -1;
  for (int i = 0; i < (int) list.size(); ++i)
    if (list[(size_t) i].name == S.base) { base = i; break; }
  if (base < 0) { error = String("The sound ") + S.base + " is missing."; return false; }
  auto& m = proc.morph();
  m.enabled = false;   // (nothing is written while the board is rebuilt)
  if (!proc.loadFactoryPreset(base, error))
    return false;
  const std::vector<float> baseValues = m.captureFromPresetJson(list[(size_t) base].text);
  m.board.ghosts.clear();
  m.ghost_values.clear();
  m.ghost_names.clear();
  for (const auto& g : S.ghosts) {
    const int k = m.board.add(g.x, g.y);
    if (k < 0) break;
    auto& ghost = m.board.ghosts[(size_t) k];
    ghost.label = g.name;
    ghost.userData = String(S.name) + ": " + g.name;
    std::vector<float> v = baseValues;
    for (const auto& kv : g.values) {
      const int s = m.slotOf(kv.first);
      if (s >= 0) v[(size_t) s] = kv.second;
    }
    m.ghost_values[ghost.id] = v;
    m.ghost_names[ghost.id] = ghost.userData;
  }
  m.board.weights.mode = S.blend == 1 ? spirit::WeightMode::Gaussian : S.blend == 2 ? spirit::WeightMode::NearestN : spirit::WeightMode::InverseDistance;
  m.board.weights.nearest = 2;
  m.board.weights.power = 2.0f;
  m.board.snapToGhost = false;
  m.motion = S.motion;
  m.division = jlimit(0, (int) Morph::divisions().size() - 1, S.division);
  m.sync = true;
  m.depth = jlimit(0.0f, 0.5f, S.depth);
  // the puck: in the middle for the motions that move around it, on ghost A (the sound itself) otherwise
  const bool around = S.motion == Morph::kCircle || S.motion == Morph::kEight || S.motion == Morph::kWander;
  m.puck_x = around ? 0.5f : S.ghosts.front().x;
  m.puck_y = around ? 0.5f : S.ghosts.front().y;
  m.publish();
  m.enabled = true;
  return true;
}

} // namespace ghosty
