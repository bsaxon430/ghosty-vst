/*
 * SEANCE (GhostBlock) - ready-made crossroads: four factory sounds for the four corners of the CROSS page, with the
 * flame's place and orbit. Each corner's volume trim was measured with `seance-sounds levels` (a held C3 chord, RMS):
 * the trims bring every corner to about -20 dB so that moving the flame changes the sound, not the loudness. The
 * octave settings put every corner's played note in the same register. JUCE-free, data only.
 */
#pragma once
#include <vector>

namespace ghostblock::seance
{
struct CrossCorner
{
    const char *folder{nullptr}, *file{nullptr}; // a factory sound (no extension); null = the corner stays empty
    float volDb{0.f}, pan{0.f};
    int octave{0};
};
struct CrossRecipe
{
    const char *name, *what;
    CrossCorner c[4]; // top-left, top-right, bottom-left, bottom-right
    float x{0.5f}, y{0.5f};
    int orbit{0}, rate{3}; // Morph::Orbit (0 off, 1 circle, 2 figure 8, 3 square, 4 swing, 5 drift); rate 0 = 1/4 bar ... 6 = 16 bars
    float size{0.6f};
};
inline const std::vector<CrossRecipe> &crossRecipes()
{
    static const std::vector<CrossRecipe> v = {
        {"Low End Council", "Four basses that sit together; the flame swings between them every two bars",
         {{"Bass", "A Supporting Role", 0.5f, 0, 0}, {"Bass", "Growler", -0.9f, 0, 0}, {"Bass", "Polite Discourse", -1.3f, 0, 0}, {"Bass", "Bass Is The Place", -2.f, 0, 0}},
         0.5f, 0.5f, 4, 3, 0.5f},
        {"Reese Rising", "Reese, growl and FM bass taking turns round a circle, one every bar",
         {{"Seance Bass", "Beast Reese", -2.f, 0, -1}, {"Seance Bass", "Ladder Growl", -2.5f, 0, -1}, {"Bass", "Classic 4op Bass", -5.3f, 0, -1}, {"Bass", "Power Through It", -1.4f, 0, 0}},
         0.5f, 0.5f, 1, 4, 0.55f},
        {"Glass Into Choir", "A struck glass bell slowly turning into a wavetable choir and back",
         {{"Seance Bells", "Glass Ring", 3.6f, -0.15f, 0}, {"Bells", "Bell Pad", 1.f, 0.15f, 0}, {"Seance Pads", "Shade Choir", 6.f, -0.2f, 0}, {"Pads", "Crystal Blue", -3.f, 0.2f, 0}},
         0.3f, 0.3f, 5, 5, 0.7f},
        {"Night Keys", "Tines, electric piano, drawbars and a glass organ swaying through a progression",
         {{"Seance Keys", "FM Tines", 4.2f, -0.1f, 0}, {"Keys", "EP 3", 0.4f, 0.1f, 0}, {"Seance Keys", "Drawbar Ghost", -1.3f, -0.1f, 0}, {"Keys", "Moving Glass Organ", -1.4f, 0.1f, 0}},
         0.4f, 0.4f, 4, 4, 0.55f},
        {"Lead Shapeshifter", "Pulse, folded sine, sync and supersaw: one lead that keeps changing its skin",
         {{"Leads", "Rounded PWM", -3.1f, 0, 0}, {"Seance Leads", "Fold Lead", -0.6f, 0, 0}, {"Leads", "Rounded Sync", -3.5f, 0, 0}, {"Leads", "Sorta Super Saw", -1.1f, 0, 0}},
         0.5f, 0.5f, 2, 3, 0.65f},
        {"Slow Weather", "Four pads drifting into each other over sixteen bars; hold long chords",
         {{"Seance Pads", "Iron Lung", -0.6f, -0.25f, 0}, {"Seance Pads", "String Wraith", 5.8f, 0.25f, 0}, {"Pads", "Subtle Starlight", 6.f, -0.25f, 0}, {"Pads", "Pictures of Nebulas", 2.7f, 0.25f, 0}},
         0.5f, 0.5f, 5, 6, 0.8f},
        {"Coven of Twelve", "Four of the all-twelve-spirit sounds in a slow circle",
         {{"Seance Twelve", "Coven Choir", 6.f, -0.2f, 0}, {"Seance Twelve", "Wraith Swarm", -2.1f, 0.2f, 0}, {"Seance Twelve", "Three Towers", 1.4f, -0.2f, 0}, {"Seance Twelve", "Six Marriages", 3.4f, 0.2f, 0}},
         0.5f, 0.5f, 1, 5, 0.6f},
        {"Bell Garden", "Bells and a marimba-bell hopping corner to corner on the beat",
         {{"Bells", "Five Primes Bell", 3.1f, -0.3f, 0}, {"Bells", "Mirage Bell", 4.5f, 0.3f, 0}, {"Mallets", "Marimbell", 1.2f, -0.3f, 0}, {"Seance Bells", "Metal Tongue", 6.f, 0.3f, 0}},
         0.5f, 0.5f, 3, 2, 0.6f},
        {"Bowed Light", "Strings, bowed glass and a soft brass pad breathing into each other",
         {{"Strings", "Familiar Strings", -0.1f, -0.15f, 0}, {"Strings", "Bowed Glass", -0.2f, 0.15f, 0}, {"Strings", "Bowing 1", 1.f, -0.15f, 0}, {"Pads", "PD Brass", 5.5f, 0.15f, 0}},
         0.5f, 0.5f, 5, 4, 0.7f},
        {"Organ Loft", "Twelve drawbars, a glass organ, an EP that turns into an organ, and a ghostly drawbar",
         {{"Seance Twelve", "Twelve Drawbars", -4.2f, 0, 0}, {"Keys", "Moving Glass Organ", -1.4f, 0, 0}, {"Keys", "EP to Organ", -8.7f, 0, 0}, {"Seance Keys", "Drawbar Ghost", -1.3f, 0, 0}},
         0.5f, 0.5f, 4, 3, 0.5f},
        {"Supersaw Dawn", "Bright supersaw keys circling into two big pads",
         {{"Keys", "Hello Supersaw", -0.9f, -0.1f, 0}, {"Keys", "Bright and Synthy", 2.1f, 0.1f, 0}, {"Pads", "Hope Evolution", -6.6f, -0.1f, 0}, {"Pads", "Beauty Grit", -7.1f, 0.1f, 0}},
         0.5f, 0.5f, 1, 3, 0.6f},
        {"Weight and Air", "A LAYER, no movement: a bass under a choir, half and half. Drag the flame down for the other pair",
         {{"Bass", "A Supporting Role", 0.5f, 0, 0}, {"Seance Pads", "Shade Choir", 6.f, 0, 0}, {"Bass", "Deep Fried Low End", 1.f, 0, 0}, {"Seance Twelve", "Coven Choir", 6.f, 0, 0}},
         0.5f, 0.f, 0, 3, 0.6f},
    };
    return v;
}
} // namespace ghostblock::seance
