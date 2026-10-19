#include "SynthProcessor.h"
#include "SynthEditor.h"

using namespace ghosty;

// ------------------------------------------------------------------ presets
namespace
{
struct Preset
{
    const char* cat; const char* name; int A, B; float morph; int uni; float width, sub, a, d, s, r, cut, res, fenv, echo, verb, drive, chorus, phaser, crush, haunt;
};
// Shapes: 0 Sine 1 Triangle 2 Saw 3 Square 4 Pulse 5 Organ 6 Glass 7 Growl 8 Digital 9 Vox
const Preset presets[] = {
    { "Keys", "Ghost Piano", 6, 1, 0.35f, 1, 0.0f, 0.0f, 0.002f, 1.6f, 0.0f, 0.8f, 0.62f, 0.1f, 0.55f, 0.1f, 0.35f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Keys", "Dream EP", 0, 1, 0.3f, 2, 0.15f, 0.0f, 0.003f, 1.4f, 0.25f, 0.7f, 0.6f, 0.1f, 0.3f, 0.15f, 0.35f, 0.0f, 0.6f, 0.2f, 0.0f, 0.0f },
    { "Keys", "Glass Keys", 6, 0, 0.3f, 3, 0.2f, 0.0f, 0.002f, 1.2f, 0.2f, 1.0f, 0.66f, 0.2f, 0.5f, 0.3f, 0.45f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Keys", "Digital Bell", 8, 6, 0.6f, 1, 0.0f, 0.0f, 0.001f, 1.5f, 0.0f, 1.4f, 0.86f, 0.1f, 0.2f, 0.35f, 0.55f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Keys", "Music Box", 0, 8, 0.5f, 1, 0.0f, 0.0f, 0.001f, 1.2f, 0.0f, 1.5f, 0.9f, 0.1f, 0.1f, 0.45f, 0.6f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Keys", "Hollow Organ", 5, 1, 0.2f, 2, 0.15f, 0.2f, 0.02f, 0.2f, 0.9f, 0.3f, 0.7f, 0.05f, 0.0f, 0.15f, 0.4f, 0.15f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Keys", "Church Organ", 5, 5, 0.0f, 3, 0.2f, 0.2f, 0.05f, 0.5f, 1.0f, 0.8f, 0.8f, 0.0f, 0.0f, 0.0f, 0.75f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Keys", "Lofi Keys", 1, 6, 0.4f, 2, 0.1f, 0.0f, 0.005f, 1.0f, 0.3f, 0.6f, 0.45f, 0.1f, 0.2f, 0.2f, 0.4f, 0.0f, 0.4f, 0.0f, 0.35f, 0.0f },
    { "Keys", "Wurly", 14, 1, 0.3f, 1, 0.0f, 0.0f, 0.003f, 1.2f, 0.3f, 0.5f, 0.5f, 0.15f, 0.4f, 0.1f, 0.3f, 0.15f, 0.3f, 0.0f, 0.0f, 0.0f },
    { "Keys", "Clav Funk", 4, 12, 0.3f, 1, 0.0f, 0.0f, 0.001f, 0.6f, 0.2f, 0.2f, 0.45f, 0.3f, 0.7f, 0.0f, 0.2f, 0.2f, 0.0f, 0.2f, 0.0f, 0.0f },
    { "Keys", "Toy Piano", 6, 8, 0.4f, 1, 0.0f, 0.0f, 0.001f, 0.8f, 0.0f, 0.6f, 0.85f, 0.1f, 0.2f, 0.3f, 0.4f, 0.0f, 0.0f, 0.0f, 0.2f, 0.0f },
    { "Keys", "Vintage Organ", 5, 14, 0.4f, 2, 0.1f, 0.1f, 0.01f, 0.3f, 0.95f, 0.2f, 0.65f, 0.05f, 0.0f, 0.0f, 0.35f, 0.25f, 0.0f, 0.4f, 0.0f, 0.0f },
    { "Keys", "Celeste Dust", 0, 6, 0.6f, 2, 0.2f, 0.0f, 0.001f, 1.6f, 0.0f, 1.8f, 0.95f, 0.05f, 0.1f, 0.5f, 0.7f, 0.0f, 0.2f, 0.0f, 0.0f, 0.2f },
    { "Keys", "Honky Keys", 2, 6, 0.5f, 3, 0.12f, 0.0f, 0.002f, 1.3f, 0.1f, 0.6f, 0.55f, 0.1f, 0.45f, 0.1f, 0.3f, 0.1f, 0.1f, 0.0f, 0.0f, 0.0f },
    { "Pads", "Vapor Pad", 9, 5, 0.5f, 7, 0.45f, 0.15f, 1.2f, 1.0f, 0.9f, 2.5f, 0.52f, 0.2f, 0.1f, 0.3f, 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Pads", "Haunted Halls", 9, 6, 0.6f, 5, 0.5f, 0.0f, 1.0f, 1.0f, 0.9f, 3.0f, 0.5f, 0.3f, 0.0f, 0.0f, 0.85f, 0.0f, 0.3f, 0.0f, 0.0f, 0.8f },
    { "Pads", "Trance Pad", 2, 3, 0.1f, 7, 0.5f, 0.2f, 0.05f, 0.5f, 1.0f, 0.8f, 0.7f, 0.05f, 0.0f, 0.2f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Pads", "Future Bass Chords", 2, 9, 0.25f, 7, 0.6f, 0.3f, 0.01f, 0.6f, 0.85f, 0.5f, 0.75f, 0.1f, 0.2f, 0.0f, 0.4f, 0.0f, 0.3f, 0.0f, 0.0f, 0.0f },
    { "Pads", "Phaser Sky", 5, 9, 0.4f, 5, 0.4f, 0.0f, 0.8f, 0.5f, 0.9f, 2.0f, 0.6f, 0.1f, 0.0f, 0.0f, 0.7f, 0.0f, 0.0f, 0.8f, 0.0f, 0.0f },
    { "Pads", "Choir Aahs", 9, 9, 0.0f, 7, 0.3f, 0.0f, 0.4f, 1.0f, 0.9f, 1.8f, 0.55f, 0.15f, 0.0f, 0.2f, 0.8f, 0.0f, 0.3f, 0.0f, 0.0f, 0.0f },
    { "Pads", "Slow Strings", 2, 2, 0.0f, 7, 0.35f, 0.0f, 0.6f, 1.0f, 0.9f, 1.6f, 0.5f, 0.1f, 0.05f, 0.1f, 0.7f, 0.0f, 0.2f, 0.0f, 0.0f, 0.0f },
    { "Pads", "Glass Cathedral", 6, 0, 0.5f, 5, 0.4f, 0.0f, 1.5f, 1.0f, 0.9f, 3.0f, 0.6f, 0.2f, 0.0f, 0.35f, 0.9f, 0.0f, 0.0f, 0.0f, 0.0f, 0.3f },
    { "Pads", "Dark Matter", 7, 9, 0.6f, 7, 0.5f, 0.3f, 1.2f, 1.0f, 0.9f, 2.5f, 0.35f, 0.4f, 0.1f, 0.0f, 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f },
    { "Pads", "Ahh Choir", 10, 11, 0.4f, 7, 0.4f, 0.0f, 0.5f, 1.0f, 0.9f, 2.0f, 0.6f, 0.1f, 0.0f, 0.2f, 0.8f, 0.0f, 0.3f, 0.0f, 0.0f, 0.0f },
    { "Pads", "Ooh Ghosts", 11, 9, 0.3f, 5, 0.4f, 0.0f, 0.8f, 1.0f, 0.9f, 2.5f, 0.5f, 0.15f, 0.0f, 0.25f, 0.85f, 0.0f, 0.2f, 0.0f, 0.0f, 0.4f },
    { "Pads", "Warm Blanket", 14, 1, 0.4f, 7, 0.5f, 0.2f, 0.9f, 1.0f, 0.9f, 2.5f, 0.4f, 0.1f, 0.0f, 0.1f, 0.6f, 0.0f, 0.3f, 0.0f, 0.0f, 0.0f },
    { "Pads", "Bright Horizon", 15, 2, 0.3f, 7, 0.55f, 0.2f, 0.4f, 1.0f, 0.9f, 2.0f, 0.75f, 0.1f, 0.1f, 0.2f, 0.6f, 0.0f, 0.2f, 0.0f, 0.0f, 0.0f },
    { "Pads", "Sync Shimmer", 12, 6, 0.5f, 5, 0.5f, 0.0f, 1.0f, 1.0f, 0.9f, 2.5f, 0.65f, 0.25f, 0.2f, 0.3f, 0.8f, 0.0f, 0.0f, 0.4f, 0.0f, 0.2f },
    { "Pads", "Frozen Lake", 6, 11, 0.5f, 7, 0.45f, 0.0f, 1.5f, 1.0f, 0.9f, 3.5f, 0.55f, 0.1f, 0.0f, 0.4f, 0.9f, 0.0f, 0.2f, 0.0f, 0.0f, 0.4f },
    { "Pads", "Lofi Tape Pad", 14, 9, 0.4f, 5, 0.3f, 0.1f, 0.7f, 1.0f, 0.85f, 1.5f, 0.4f, 0.1f, 0.0f, 0.2f, 0.5f, 0.0f, 0.5f, 0.0f, 0.4f, 0.0f },
    { "Bass", "Growl Bass", 7, 2, 0.4f, 3, 0.2f, 0.6f, 0.005f, 0.3f, 0.7f, 0.2f, 0.36f, 0.45f, 0.6f, 0.05f, 0.1f, 0.55f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Bass", "Reese Bass", 2, 2, 0.0f, 5, 0.35f, 0.5f, 0.01f, 0.5f, 0.9f, 0.3f, 0.35f, 0.2f, 0.0f, 0.0f, 0.1f, 0.3f, 0.5f, 0.0f, 0.0f, 0.0f },
    { "Bass", "Acid 303", 2, 3, 0.2f, 1, 0.0f, 0.0f, 0.003f, 0.25f, 0.3f, 0.15f, 0.15f, 0.8f, 0.9f, 0.3f, 0.15f, 0.45f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Bass", "Sub Bass", 0, 1, 0.1f, 1, 0.0f, 0.5f, 0.005f, 0.5f, 1.0f, 0.25f, 0.5f, 0.05f, 0.0f, 0.0f, 0.05f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Bass", "808 Glide", 0, 1, 0.0f, 1, 0.0f, 0.3f, 0.002f, 1.6f, 0.0f, 0.4f, 0.6f, 0.0f, 0.0f, 0.0f, 0.05f, 0.35f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Bass", "Grime Bass", 3, 7, 0.5f, 1, 0.0f, 0.5f, 0.005f, 0.2f, 0.8f, 0.15f, 0.4f, 0.5f, 0.5f, 0.0f, 0.05f, 0.6f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Bass", "Finger Bass", 1, 0, 0.3f, 1, 0.0f, 0.3f, 0.003f, 0.9f, 0.2f, 0.2f, 0.35f, 0.1f, 0.3f, 0.0f, 0.1f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Bass", "Hoover Bass", 2, 4, 0.3f, 7, 0.6f, 0.4f, 0.01f, 0.4f, 0.8f, 0.3f, 0.55f, 0.2f, 0.3f, 0.0f, 0.1f, 0.4f, 0.3f, 0.0f, 0.0f, 0.0f },
    { "Bass", "Fold Bass", 13, 0, 0.4f, 1, 0.0f, 0.5f, 0.003f, 0.4f, 0.7f, 0.2f, 0.45f, 0.3f, 0.4f, 0.0f, 0.05f, 0.4f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Bass", "Sync Bass", 12, 2, 0.3f, 1, 0.0f, 0.4f, 0.003f, 0.3f, 0.7f, 0.15f, 0.35f, 0.4f, 0.6f, 0.0f, 0.05f, 0.35f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Bass", "Deep House Bass", 14, 0, 0.3f, 1, 0.0f, 0.4f, 0.003f, 0.4f, 0.5f, 0.2f, 0.3f, 0.2f, 0.4f, 0.0f, 0.05f, 0.1f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Bass", "Wobble-ish", 7, 12, 0.6f, 3, 0.2f, 0.5f, 0.005f, 0.3f, 0.85f, 0.2f, 0.5f, 0.55f, 0.3f, 0.0f, 0.05f, 0.5f, 0.0f, 0.6f, 0.0f, 0.0f },
    { "Bass", "Pluck Bass", 3, 0, 0.3f, 1, 0.0f, 0.3f, 0.001f, 0.35f, 0.0f, 0.2f, 0.25f, 0.35f, 0.8f, 0.0f, 0.05f, 0.2f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Bass", "Moog-ish", 2, 3, 0.4f, 2, 0.1f, 0.3f, 0.005f, 0.5f, 0.6f, 0.2f, 0.3f, 0.6f, 0.5f, 0.0f, 0.05f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Leads", "Neon Supersaw", 2, 3, 0.15f, 7, 0.55f, 0.25f, 0.01f, 0.4f, 0.8f, 0.6f, 0.82f, 0.1f, 0.2f, 0.25f, 0.35f, 0.1f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Leads", "Vox Lead", 9, 2, 0.25f, 3, 0.25f, 0.1f, 0.01f, 0.3f, 0.7f, 0.3f, 0.6f, 0.3f, 0.3f, 0.4f, 0.3f, 0.25f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Leads", "Chip Lead", 3, 8, 0.3f, 1, 0.0f, 0.0f, 0.002f, 0.2f, 0.7f, 0.1f, 0.9f, 0.05f, 0.0f, 0.3f, 0.2f, 0.0f, 0.0f, 0.0f, 0.55f, 0.0f },
    { "Leads", "Screamer", 2, 7, 0.3f, 3, 0.3f, 0.0f, 0.005f, 0.3f, 0.9f, 0.3f, 0.7f, 0.35f, 0.2f, 0.3f, 0.25f, 0.8f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Leads", "Glitch Ghost", 8, 9, 0.5f, 3, 0.3f, 0.0f, 0.005f, 0.3f, 0.8f, 0.4f, 0.7f, 0.3f, 0.0f, 0.3f, 0.3f, 0.0f, 0.0f, 0.0f, 0.25f, 0.3f },
    { "Leads", "Whistle", 0, 0, 0.0f, 1, 0.0f, 0.0f, 0.03f, 0.3f, 1.0f, 0.4f, 0.8f, 0.0f, 0.0f, 0.4f, 0.4f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Leads", "Ghost Flute", 1, 0, 0.3f, 1, 0.0f, 0.0f, 0.06f, 0.3f, 0.9f, 0.4f, 0.55f, 0.1f, 0.0f, 0.2f, 0.45f, 0.0f, 0.2f, 0.0f, 0.0f, 0.0f },
    { "Leads", "Hoover Lead", 2, 4, 0.3f, 7, 0.6f, 0.4f, 0.01f, 0.4f, 0.8f, 0.4f, 0.65f, 0.2f, 0.2f, 0.15f, 0.3f, 0.5f, 0.3f, 0.0f, 0.0f, 0.0f },
    { "Leads", "Brass Stab", 2, 3, 0.2f, 3, 0.2f, 0.0f, 0.01f, 0.35f, 0.6f, 0.25f, 0.35f, 0.15f, 0.7f, 0.1f, 0.3f, 0.2f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Leads", "Sync Lead", 12, 2, 0.4f, 3, 0.3f, 0.0f, 0.005f, 0.3f, 0.85f, 0.3f, 0.7f, 0.3f, 0.2f, 0.3f, 0.3f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Leads", "Fold Lead", 13, 6, 0.5f, 1, 0.0f, 0.0f, 0.005f, 0.3f, 0.8f, 0.3f, 0.65f, 0.2f, 0.3f, 0.35f, 0.3f, 0.2f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Leads", "Ahh Lead", 10, 2, 0.3f, 3, 0.25f, 0.0f, 0.02f, 0.3f, 0.8f, 0.4f, 0.65f, 0.25f, 0.2f, 0.35f, 0.35f, 0.15f, 0.2f, 0.0f, 0.0f, 0.0f },
    { "Leads", "Bright Lead", 15, 3, 0.3f, 5, 0.35f, 0.1f, 0.005f, 0.3f, 0.85f, 0.35f, 0.75f, 0.15f, 0.2f, 0.3f, 0.3f, 0.2f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Leads", "Square Hero", 3, 4, 0.3f, 3, 0.2f, 0.0f, 0.005f, 0.3f, 0.9f, 0.3f, 0.7f, 0.1f, 0.1f, 0.35f, 0.3f, 0.1f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Leads", "Haunted Theremin", 0, 11, 0.4f, 1, 0.0f, 0.0f, 0.15f, 0.3f, 1.0f, 0.8f, 0.7f, 0.0f, 0.0f, 0.4f, 0.6f, 0.0f, 0.3f, 0.0f, 0.0f, 0.6f },
    { "Plucks", "Pluck Fever", 3, 4, 0.4f, 3, 0.3f, 0.0f, 0.001f, 0.3f, 0.0f, 0.35f, 0.3f, 0.35f, 0.8f, 0.45f, 0.35f, 0.1f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Plucks", "Hyper Pluck", 3, 2, 0.5f, 5, 0.4f, 0.0f, 0.001f, 0.2f, 0.0f, 0.3f, 0.2f, 0.4f, 0.9f, 0.5f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Plucks", "Kalimba", 0, 6, 0.3f, 1, 0.0f, 0.0f, 0.001f, 0.7f, 0.0f, 0.5f, 0.7f, 0.1f, 0.3f, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Plucks", "Marimba", 0, 1, 0.2f, 1, 0.0f, 0.0f, 0.001f, 0.5f, 0.0f, 0.3f, 0.75f, 0.0f, 0.2f, 0.0f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Plucks", "Harp", 1, 6, 0.2f, 1, 0.0f, 0.0f, 0.002f, 1.8f, 0.0f, 1.2f, 0.6f, 0.1f, 0.4f, 0.2f, 0.6f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Plucks", "Steel Drum", 6, 0, 0.4f, 1, 0.0f, 0.0f, 0.001f, 0.9f, 0.0f, 0.6f, 0.8f, 0.1f, 0.2f, 0.2f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Plucks", "Sitar", 7, 2, 0.5f, 1, 0.0f, 0.0f, 0.002f, 1.6f, 0.1f, 0.8f, 0.55f, 0.5f, 0.5f, 0.1f, 0.4f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Plucks", "Nylon Pluck", 1, 2, 0.25f, 1, 0.0f, 0.0f, 0.002f, 1.5f, 0.0f, 0.7f, 0.45f, 0.15f, 0.6f, 0.0f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Plucks", "Fold Pluck", 13, 0, 0.5f, 3, 0.3f, 0.0f, 0.001f, 0.4f, 0.0f, 0.4f, 0.4f, 0.3f, 0.7f, 0.45f, 0.35f, 0.1f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Plucks", "Sync Pluck", 12, 3, 0.4f, 3, 0.3f, 0.0f, 0.001f, 0.3f, 0.0f, 0.3f, 0.3f, 0.4f, 0.85f, 0.45f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Plucks", "Glass Drops", 6, 0, 0.4f, 1, 0.0f, 0.0f, 0.001f, 0.5f, 0.0f, 0.5f, 0.8f, 0.2f, 0.3f, 0.6f, 0.6f, 0.0f, 0.0f, 0.0f, 0.0f, 0.2f },
    { "Plucks", "Koto", 7, 1, 0.3f, 1, 0.0f, 0.0f, 0.002f, 1.2f, 0.0f, 0.8f, 0.55f, 0.35f, 0.5f, 0.15f, 0.4f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Plucks", "Banjo-ish", 4, 2, 0.3f, 1, 0.0f, 0.0f, 0.001f, 0.6f, 0.0f, 0.3f, 0.6f, 0.2f, 0.6f, 0.0f, 0.2f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Plucks", "Future Pluck", 15, 12, 0.4f, 5, 0.45f, 0.0f, 0.001f, 0.3f, 0.0f, 0.4f, 0.3f, 0.3f, 0.85f, 0.5f, 0.35f, 0.0f, 0.2f, 0.0f, 0.0f, 0.0f },
    { "Bells", "Crystal Bell", 6, 8, 0.5f, 1, 0.0f, 0.0f, 0.001f, 2.5f, 0.0f, 2.5f, 0.9f, 0.1f, 0.2f, 0.4f, 0.7f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Bells", "Tubular", 14, 6, 0.5f, 1, 0.0f, 0.0f, 0.001f, 3.0f, 0.0f, 3.0f, 0.75f, 0.05f, 0.1f, 0.3f, 0.6f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Bells", "Gamelan", 8, 13, 0.5f, 1, 0.0f, 0.0f, 0.001f, 2.0f, 0.0f, 2.0f, 0.8f, 0.1f, 0.1f, 0.35f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.2f },
    { "Bells", "Ghost Chimes", 0, 6, 0.7f, 3, 0.3f, 0.0f, 0.001f, 3.0f, 0.0f, 3.0f, 0.95f, 0.05f, 0.0f, 0.5f, 0.85f, 0.0f, 0.3f, 0.0f, 0.0f, 0.5f },
    { "FX", "Riser Saw", 2, 15, 0.5f, 7, 0.7f, 0.2f, 3.0f, 1.0f, 1.0f, 1.0f, 0.4f, 0.6f, 0.8f, 0.3f, 0.6f, 0.3f, 0.3f, 0.5f, 0.0f, 0.0f },
    { "FX", "Dark Drone", 7, 13, 0.6f, 7, 0.6f, 0.5f, 2.0f, 1.0f, 1.0f, 4.0f, 0.25f, 0.5f, 0.0f, 0.2f, 0.9f, 0.4f, 0.0f, 0.0f, 0.0f, 0.7f },
    { "FX", "Bitcrush Zap", 8, 12, 0.5f, 1, 0.0f, 0.0f, 0.001f, 0.2f, 0.0f, 0.2f, 0.9f, 0.6f, 0.9f, 0.4f, 0.3f, 0.3f, 0.0f, 0.0f, 0.7f, 0.0f },
    { "FX", "Ghost Wind", 11, 15, 0.5f, 7, 0.8f, 0.0f, 2.0f, 1.0f, 1.0f, 3.0f, 0.35f, 0.7f, 0.0f, 0.4f, 0.9f, 0.0f, 0.4f, 0.6f, 0.0f, 0.6f },
};
constexpr int numPresets = (int) (sizeof (presets) / sizeof (presets[0]));
}

// ------------------------------------------------------------------ LFO / filter names
const juce::StringArray& SynthProcessor::lfoDivNames()    { static juce::StringArray s { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/2 T", "1/4 T", "1/8 T", "1/16 T", "1/32 T" }; return s; }
const juce::StringArray& SynthProcessor::lfoShapeNames()  { static juce::StringArray s { "Sine", "Triangle", "Saw", "Square", "S&H" }; return s; }
const juce::StringArray& SynthProcessor::lfoDestNames()   { static juce::StringArray s { "Cutoff", "Morph", "Pitch (vibrato)", "Volume (tremolo)", "Pan" }; return s; }
const juce::StringArray& SynthProcessor::filterTypeNames(){ static juce::StringArray s { "Low pass", "Band pass", "High pass", "Notch" }; return s; }
static double lfoDivQuarterNotes (int i)
{
    static const double q[] { 4.0, 2.0, 1.0, 0.5, 0.25, 0.125, 4.0 / 3.0, 2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0, 1.0 / 12.0 };
    return q[juce::jlimit (0, 10, i)];
}

float SynthProcessor::lfoValue (int shape, double ph) noexcept
{
    const double cyc = std::floor (ph);
    const float fr = (float) (ph - cyc);
    switch (shape)
    {
        case 1: return 1.0f - 4.0f * std::abs (fr - 0.5f);
        case 2: return 2.0f * fr - 1.0f;
        case 3: return fr < 0.5f ? 1.0f : -1.0f;
        case 4:
        {
            auto x = (uint32_t) (int64_t) cyc;             // sample & hold: a fixed random value per cycle
            x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16;
            return (float) (x / 4294967295.0) * 2.0f - 1.0f;
        }
        default: return (float) std::sin (juce::MathConstants<double>::twoPi * fr);
    }
}

// ------------------------------------------------------------------ voice
static void updateEnvelopes (ghosty::LiveEnv& amp, ghosty::LiveEnv& fenv, const SynthProcessor& p)
{
    const float a = p.attack->load(), d = p.decay->load(), s = p.sustain->load(), r = p.release->load();
    amp.set (a, d, s, r);
    if (p.fLinkP->load() > 0.5f) fenv.set (a * 0.8f + 0.001f, d, 0.0f, r);      // the original behaviour
    else fenv.set (p.fAttackP->load(), p.fDecayP->load(), p.fSustainP->load(), p.fReleaseP->load());
}

void GhostVoice::prepare (double s, int block)
{
    sr = s; amp.sr = s; fenv.sr = s;
    filter.prepare ({ s, (juce::uint32) juce::jmax (1, block), 2 });
    filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
    filterType = 0;
}

void GhostVoice::startNote (int midi, float vel, juce::SynthesiserSound*, int wheel)
{
    velRaw = vel;
    velocity = 0.35f + vel * 0.65f;
    pitchWheelMoved (wheel);
    auto& r = juce::Random::getSystemRandom();
    for (auto& p : phase) p = r.nextDouble();
    subPhase = 0;
    logTarget = std::log2 (juce::MidiMessage::getMidiNoteInHertz (midi));
    auto& c = proc.ctl;
    logNow = (c.glide > 0.0005f && c.haveLastNote) ? c.lastNoteLog2 : logTarget;
    c.lastNoteLog2 = logTarget; c.haveLastNote = true;
    amp.reset(); fenv.reset();
    updateEnvelopes (amp, fenv, proc);
    amp.on(); fenv.on();
    filter.reset();
    snapSmoothers();
}

void GhostVoice::legatoTo (int midi, float vel, bool retrigger)
{
    logTarget = std::log2 (juce::MidiMessage::getMidiNoteInHertz (midi));
    auto& c = proc.ctl;
    if (c.glide <= 0.0005f) logNow = logTarget;
    c.lastNoteLog2 = logTarget; c.haveLastNote = true;
    if (retrigger)
    {
        velRaw = vel; velocity = 0.35f + vel * 0.65f;
        updateEnvelopes (amp, fenv, proc);
        amp.on(); fenv.on();                       // restarts the attack from the current level: no click
    }
}

void GhostVoice::snapSmoothers()
{
    const auto& c = proc.ctl;
    const float wheelMod = c.wheel * c.wheelAmt * 0.5f;
    sMorph = juce::jlimit (0.0f, 1.0f, proc.morph->load() + wheelMod);
    sWidth = proc.width->load();
    sCut = juce::jlimit (0.0f, 1.0f, proc.cutoff->load() + wheelMod);
    sRes = proc.res->load();
    sFDrive = proc.fDriveP->load();
    sVol = 1.0f; sPan = 0.0f;
}

void GhostVoice::stopNote (float, bool allowTail)
{
    if (allowTail) { amp.off(); fenv.off(); }
    else { amp.reset(); fenv.reset(); clearCurrentNote(); }
}

void GhostVoice::renderNextBlock (juce::AudioBuffer<float>& out, int start, int num)
{
    if (! isVoiceActive()) return;
    const auto& c = proc.ctl;
    if (c.waves == nullptr || c.waves->a.t[0].empty()) return;
    const auto& A = c.waves->a;
    const auto& B = c.waves->b;

    updateEnvelopes (amp, fenv, proc);              // envelope knobs act on held notes too

    const int uni = juce::jlimit (1, 7, (int) proc.unison->load());
    const float pMorph = proc.morph->load(), pWidth = proc.width->load(), sub = proc.sub->load();
    const float pCut = proc.cutoff->load(), pRes = proc.res->load(), fAmt = proc.fenvAmt->load(), pDrive = proc.fDriveP->load();
    const int ft = juce::jlimit (0, 3, (int) proc.fTypeP->load());
    if (ft != filterType)
    {
        filterType = ft;
        using T = juce::dsp::StateVariableTPTFilterType;
        filter.setType (ft == 0 ? T::lowpass : ft == 2 ? T::highpass : T::bandpass);   // notch = input - bandpass
    }
    const float wheelMod = c.wheel * c.wheelAmt * 0.5f;
    constexpr int CTRL = 16;
    const float aCtl = 1.0f - (float) std::exp (-CTRL / (0.012 * sr));   // ~12 ms smoothing
    const float aSmp = 1.0f - (float) std::exp (-1.0 / (0.012 * sr));
    const double gCoef = c.glide > 0.0005f ? 1.0 - std::exp (-CTRL / (c.glide * 0.35 * sr)) : 1.0;
    const float maxHz = (float) juce::jmin (19000.0, sr * 0.45);
    const float norm = 0.22f * velocity / std::sqrt ((float) uni);
    const float subGain = sub * 0.8f * std::sqrt ((float) uni);

    std::array<double, 7> inc {}; std::array<float, 7> pl {}, pr {};
    int mip = 0; double f0 = 440;
    float morphTarget = sMorph, volStep = 0, panStep = 0, R2 = 1.0f, driveK = 1.0f, driveNorm = 1.0f;

    auto* L = out.getWritePointer (0, start);
    auto* R = out.getNumChannels() > 1 ? out.getWritePointer (1, start) : nullptr;

    for (int i = 0; i < num; ++i)
    {
        if (i % CTRL == 0)
        {
            const int n = juce::jmin (CTRL, num - i);
            const float lfoRaw = c.lfoDepth > 0.0f ? SynthProcessor::lfoValue (c.lfoShape, c.lfoPhase + (double) (start + i) * c.lfoInc) : 0.0f;
            const float lfo = lfoRaw * c.lfoDepth;
            morphTarget = juce::jlimit (0.0f, 1.0f, pMorph + wheelMod + (c.lfoDest == 1 ? lfo * 0.5f : 0.0f));
            sWidth  += (pWidth - sWidth) * aCtl;
            sCut    += (juce::jlimit (0.0f, 1.0f, pCut + wheelMod) - sCut) * aCtl;
            sRes    += (pRes - sRes) * aCtl;
            sFDrive += (pDrive - sFDrive) * aCtl;
            const float volT = c.lfoDest == 3 ? 1.0f - c.lfoDepth * (0.5f - 0.5f * lfoRaw) : 1.0f;
            const float panT = c.lfoDest == 4 ? lfo : 0.0f;
            volStep = (volT - sVol) / (float) n; panStep = (panT - sPan) / (float) n;

            logNow += (logTarget - logNow) * gCoef;
            f0 = std::exp2 (logNow) * bend * (c.lfoDest == 2 ? std::exp2 (lfo * 2.0 / 12.0) : 1.0);   // vibrato up to +-2 semitones
            mip = ghosty::MipSet::mipFor (f0 * 1.03);
            for (int u = 0; u < uni; ++u)
            {
                const double spread = uni == 1 ? 0.0 : ((double) u / (uni - 1) * 2.0 - 1.0);
                inc[(size_t) u] = f0 * std::pow (2.0, spread * sWidth * 50.0 / 1200.0) / sr;
                const float pan = (float) spread * juce::jmin (1.0f, sWidth * 1.6f);
                pl[(size_t) u] = std::sqrt (0.5f * (1.0f - pan)); pr[(size_t) u] = std::sqrt (0.5f * (1.0f + pan));
            }

            const float fe = fenv.next();
            const float oct = fAmt * 5.0f * fe + c.velFilt * velRaw * 4.0f + (c.lfoDest == 0 ? lfo * 4.0f : 0.0f);
            filter.setCutoffFrequency (juce::jlimit (30.0f, maxHz, SynthProcessor::cutoffHz (sCut) * std::exp2 (oct)));
            const float q = 0.7f + sRes * 9.0f;
            filter.setResonance (q); R2 = 1.0f / q;
            driveK = 1.0f + 6.0f * sFDrive; driveNorm = 1.0f / std::tanh (driveK);
        }
        else fenv.next();

        sMorph += (morphTarget - sMorph) * aSmp;
        float l = 0, r = 0;
        for (int u = 0; u < uni; ++u)
        {
            auto& ph = phase[(size_t) u];
            const float a = A.read (mip, ph), b = B.read (mip, ph);
            const float v = a + (b - a) * sMorph;
            l += v * pl[(size_t) u]; r += v * pr[(size_t) u];
            ph += inc[(size_t) u]; if (ph >= 1.0) ph -= 1.0;
        }
        const float sv = (float) std::sin (subPhase * juce::MathConstants<double>::twoPi) * subGain;
        subPhase += f0 * 0.5 / sr; if (subPhase >= 1.0) subPhase -= 1.0;
        l += sv; r += sv;

        if (sFDrive > 1.0e-4f)                   // filter drive; exactly bypassed at 0 so old sounds stay the same
        {
            l += sFDrive * (std::tanh (driveK * l) * driveNorm - l);
            r += sFDrive * (std::tanh (driveK * r) * driveNorm - r);
        }
        float fl = filter.processSample (0, l), fr = filter.processSample (1, r);
        if (filterType == 3) { fl = l - R2 * fl; fr = r - R2 * fr; }

        sVol += volStep; sPan += panStep;
        const float env = amp.next() * norm * sVol;
        float gl = env, gr = env;
        if (sPan != 0.0f) { gl *= std::sqrt (juce::jmax (0.0f, 1.0f - sPan)); gr *= std::sqrt (juce::jmax (0.0f, 1.0f + sPan)); }
        L[i] += fl * gl; if (R) R[i] += fr * gr;
    }
    if (! amp.active()) clearCurrentNote();
}

// ------------------------------------------------------------------ mono / legato
void GhostSynth::setMono (bool m, bool l)
{
    if (m != mono) allNotesOff (0, true);
    mono = m; legato = l;
}

void GhostSynth::removeFromStack (int note)
{
    int w = 0;
    for (int i = 0; i < stackN; ++i) if (stack[(size_t) i] != note) stack[(size_t) w++] = stack[(size_t) i];
    stackN = w;
}

void GhostSynth::noteOn (int ch, int note, float vel)
{
    if (! mono || voices.isEmpty() || sounds.isEmpty()) { juce::Synthesiser::noteOn (ch, note, vel); return; }
    removeFromStack (note);
    if (stackN < (int) stack.size()) stack[(size_t) stackN++] = note;
    auto* v = static_cast<GhostVoice*> (voices.getUnchecked (0));
    if (v->isVoiceActive()) v->legatoTo (note, vel, ! (legato && monoNote >= 0));
    else startVoice (v, sounds.getUnchecked (0), ch, note, vel);
    monoNote = note;
}

void GhostSynth::noteOff (int ch, int note, float vel, bool allowTail)
{
    if (! mono || voices.isEmpty()) { juce::Synthesiser::noteOff (ch, note, vel, allowTail); return; }
    removeFromStack (note);
    if (note != monoNote) return;
    auto* v = static_cast<GhostVoice*> (voices.getUnchecked (0));
    if (stackN > 0) { monoNote = stack[(size_t) stackN - 1]; v->legatoTo (monoNote, vel, false); }   // fall back to the key still held
    else { monoNote = -1; if (v->isVoiceActive()) stopVoice (v, vel, allowTail); }
}

void GhostSynth::allNotesOff (int ch, bool allowTail)
{
    stackN = 0; monoNote = -1;
    juce::Synthesiser::allNotesOff (ch, allowTail);
}

void GhostSynth::handleController (int ch, int cc, int value)
{
    if (cc == 1) proc.ctl.wheel = (float) value / 127.0f;
    juce::Synthesiser::handleController (ch, cc, value);
}

// ------------------------------------------------------------------ processor
juce::AudioProcessorValueTreeState::ParameterLayout SynthProcessor::layout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pct = [] (const String& id, const String& nm, float def)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, nm, NormalisableRange<float> (0.0f, 1.0f), def,
                   AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100)) + "%"; }));
    };
    auto secs = [] (const String& id, const String& nm, float lo, float hi, float def)
    {
        NormalisableRange<float> r (lo, hi); r.setSkewForCentre (lo + (hi - lo) * 0.12f);
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, nm, r, def,
                   AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 1 ? String (roundToInt (v * 1000)) + " ms" : String (v, 2) + " s"; }));
    };
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "waveA", 1 }, "Wave A", shapeNames(), 2));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "waveB", 1 }, "Wave B", shapeNames(), 3));
    l.add (pct ("morph", "Morph", 0.15f));
    l.add (std::make_unique<AudioParameterInt> (ParameterID { "unison", 1 }, "Unison", 1, 7, 7));
    l.add (pct ("width", "Width", 0.55f));
    l.add (pct ("sub", "Sub", 0.25f));
    l.add (secs ("attack", "Attack", 0.001f, 4.0f, 0.01f));
    l.add (secs ("decay", "Decay", 0.01f, 4.0f, 0.4f));
    l.add (pct ("sustain", "Sustain", 0.8f));
    l.add (secs ("release", "Release", 0.01f, 6.0f, 0.6f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "cutoff", 1 }, "Cutoff", NormalisableRange<float> (0.0f, 1.0f), 0.82f,
               AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { const float f = cutoffHz (v); return f >= 1000 ? String (f / 1000.0f, 1) + " kHz" : String (roundToInt (f)) + " Hz"; })));
    l.add (pct ("res", "Resonance", 0.1f));
    l.add (pct ("fenv", "Filter Env", 0.2f));
    l.add (pct ("drive", "Drive", 0.1f));
    l.add (pct ("crush", "Crush", 0.0f));
    l.add (pct ("chorus", "Chorus", 0.0f));
    l.add (pct ("phaser", "Phaser", 0.0f));
    l.add (pct ("echo", "Echo", 0.25f));
    l.add (pct ("verb", "Reverb", 0.35f));
    l.add (pct ("haunt", "Haunt", 0.0f));
    l.add (pct ("gain", "Volume", 0.75f));
    // play helpers: scale lock, chords, arpeggiator
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "key", 1 }, "Key", keyNames(), 9));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "scale", 1 }, "Scale", scaleNames(), 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "chord", 1 }, "Chords", chordNames(), 0));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "arp", 1 }, "Arp", false));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "arpRate", 1 }, "Arp Rate", arpRateNames(), 2));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "arpMode", 1 }, "Arp Mode", arpModeNames(), 0));
    l.add (std::make_unique<AudioParameterInt> (ParameterID { "arpOct", 1 }, "Arp Octaves", 1, 3, 2));
    l.add (pct ("arpGate", "Arp Gate", 0.7f));

    // ---- v2 additions. Every default leaves the sound exactly as before.
    {
        NormalisableRange<float> r (0.05f, 20.0f); r.setSkewForCentre (2.0f);
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { "lfoRate", 2 }, "LFO Rate", r, 2.0f,
                   AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (v, v < 1 ? 2 : 1) + " Hz"; })));
    }
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "lfoSync", 2 }, "LFO Sync", false));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "lfoDiv", 2 }, "LFO Division", lfoDivNames(), 2));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "lfoShape", 2 }, "LFO Shape", lfoShapeNames(), 0));
    l.add (pct ("lfoDepth", "LFO Depth", 0.0f));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "lfoDest", 2 }, "LFO Target", lfoDestNames(), 0));
    l.add (secs ("glide", "Glide", 0.0f, 2.0f, 0.0f));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "mono", 2 }, "Mono", false));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "legato", 2 }, "Legato", false));
    l.add (pct ("wheelAmt", "Mod Wheel Amount", 0.5f));
    l.add (pct ("velFilt", "Velocity to Filter", 0.0f));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "fType", 2 }, "Filter Type", filterTypeNames(), 0));
    l.add (pct ("fDrive", "Filter Drive", 0.0f));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "fLink", 2 }, "Filter Env Link", true));
    l.add (secs ("fAttack", "Filter Attack", 0.001f, 4.0f, 0.009f));
    l.add (secs ("fDecay", "Filter Decay", 0.01f, 4.0f, 0.4f));
    l.add (pct ("fSustain", "Filter Sustain", 0.0f));
    l.add (secs ("fRelease", "Filter Release", 0.01f, 6.0f, 0.6f));
    return l;
}

SynthProcessor::SynthProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "GHOSTY", layout())
{
    auto P = [this] (const char* id) { auto* v = apvts.getRawParameterValue (id); jassert (v != nullptr); return v; };
    morph = P ("morph"); unison = P ("unison"); width = P ("width"); sub = P ("sub");
    attack = P ("attack"); decay = P ("decay"); sustain = P ("sustain"); release = P ("release");
    cutoff = P ("cutoff"); res = P ("res"); fenvAmt = P ("fenv"); gain = P ("gain");
    drive = P ("drive"); crush = P ("crush"); chorus = P ("chorus"); phaser = P ("phaser"); echo = P ("echo"); verb = P ("verb"); haunt = P ("haunt");
    keyP = P ("key"); scaleP = P ("scale"); chordP = P ("chord"); arpP = P ("arp"); arpRateP = P ("arpRate"); arpModeP = P ("arpMode"); arpOctP = P ("arpOct"); arpGateP = P ("arpGate");
    lfoRateP = P ("lfoRate"); lfoSyncP = P ("lfoSync"); lfoDivP = P ("lfoDiv"); lfoShapeP = P ("lfoShape"); lfoDepthP = P ("lfoDepth"); lfoDestP = P ("lfoDest");
    glideP = P ("glide"); monoP = P ("mono"); legatoP = P ("legato"); wheelAmtP = P ("wheelAmt"); velFiltP = P ("velFilt");
    fTypeP = P ("fType"); fDriveP = P ("fDrive"); fLinkP = P ("fLink"); fAttackP = P ("fAttack"); fDecayP = P ("fDecay"); fSustainP = P ("fSustain"); fReleaseP = P ("fRelease");

    frameA = makeShape (2); frameB = makeShape (3);
    savedA = frameA; savedB = frameB;
    for (int i = 0; i < 16; ++i) synth.addVoice (new GhostVoice (*this));
    synth.addSound (new GhostSound());
    apvts.addParameterListener ("waveA", this);
    apvts.addParameterListener ("waveB", this);
    keyboardState.addListener (this);
    merged.ensureSize (midiCapacity); performed.ensureSize (midiCapacity);
    performer.setOutputCapacity (midiCapacity);
    builtRate = sampleRateNow.load();
    waves.post (WaveSet::make (frameA, frameB, builtRate));
    startTimerHz (20);
}

SynthProcessor::~SynthProcessor()
{
    stopTimer();
    keyboardState.removeListener (this);
    apvts.removeParameterListener ("waveA", this);
    apvts.removeParameterListener ("waveB", this);
}

// any thread (host automation can arrive on the audio thread): only raise a flag, the message thread does the work
void SynthProcessor::parameterChanged (const juce::String& id, float)
{
    if (id == "waveA") needA = true; else if (id == "waveB") needB = true;
}

void SynthProcessor::timerCallback()
{
    flushPendingWaves();
    waves.collect();
}

void SynthProcessor::postFrames (const Frame& a, const Frame& b, int shapeA, int shapeB)
{
    {
        const juce::SpinLock::ScopedLockType sl (reqLock);
        request.has = true; request.a = a; request.b = b; request.shapeA = shapeA; request.shapeB = shapeB;
    }
    requestPending = true;
    if (juce::MessageManager::existsAndIsCurrentThread()) flushPendingWaves();
}

void SynthProcessor::rebuildWaves()
{
    if (! juce::MessageManager::existsAndIsCurrentThread()) { needRate = true; return; }   // the timer will do it
    builtRate = sampleRateNow.load();
    waves.post (WaveSet::make (frameA, frameB, builtRate));
    const juce::SpinLock::ScopedLockType sl (reqLock);
    savedA = frameA; savedB = frameB;
}

void SynthProcessor::flushPendingWaves()
{
    if (! juce::MessageManager::existsAndIsCurrentThread()) return;
    bool changed = false;
    if (requestPending.exchange (false))
    {
        FrameRequest req;
        {
            const juce::SpinLock::ScopedLockType sl (reqLock);
            req = request; request.has = false;
        }
        if (req.has) { frameA = req.a; frameB = req.b; frameShapeA = req.shapeA; frameShapeB = req.shapeB; changed = true; }
    }
    // A wave-choice change only replaces a frame when the choice really differs from the one the frame was made from.
    // So the echo of a state load (same choice) never wipes the drawn frames that came with that state.
    if (needA.exchange (false)) { const int s = (int) apvts.getRawParameterValue ("waveA")->load(); if (s != frameShapeA) { frameA = makeShape (s); frameShapeA = s; changed = true; } }
    if (needB.exchange (false)) { const int s = (int) apvts.getRawParameterValue ("waveB")->load(); if (s != frameShapeB) { frameB = makeShape (s); frameShapeB = s; changed = true; } }
    if (needRate.exchange (false) || std::abs (sampleRateNow.load() - builtRate) > 1.0) changed = true;
    if (changed) rebuildWaves();
}

void SynthProcessor::handleNoteOn (juce::MidiKeyboardState*, int, int note, float vel)
{
    uiFifo.write (1).forEach ([&] (int i) { uiNotes[(size_t) i] = { (int8_t) note, (uint8_t) juce::jlimit (1, 127, juce::roundToInt (vel * 127.0f)), true }; });
}
void SynthProcessor::handleNoteOff (juce::MidiKeyboardState*, int, int note, float)
{
    uiFifo.write (1).forEach ([&] (int i) { uiNotes[(size_t) i] = { (int8_t) note, 0, false }; });
}

void SynthProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sampleRateNow = sampleRate;
    synth.setCurrentPlaybackSampleRate (sampleRate);
    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* v = dynamic_cast<GhostVoice*> (synth.getVoice (i))) v->prepare (sampleRate, samplesPerBlock);
    fx.prepare (sampleRate, samplesPerBlock);
    performer.prepare (sampleRate);
    merged.ensureSize (midiCapacity); performed.ensureSize (midiCapacity);
    lastGain = -1.0f;
    needRate = true;
    if (juce::MessageManager::existsAndIsCurrentThread()) flushPendingWaves();
}

bool SynthProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void SynthProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    buffer.clear();
    ctl.waves = waves.acquire();

    // host MIDI + flow-strip notes into a pre-allocated buffer (never grows here)
    constexpr int header = (int) (sizeof (int32_t) + sizeof (uint16_t));
    merged.clear();
    for (const auto meta : midi)
        if (merged.data.size() + header + meta.numBytes <= midiCapacity) merged.addEvent (meta.data, meta.numBytes, meta.samplePosition);
    uiFifo.read (uiFifo.getNumReady()).forEach ([&] (int i)
    {
        const auto& u = uiNotes[(size_t) i];
        if (merged.data.size() + header + 3 <= midiCapacity)
            merged.addEvent (u.on ? juce::MidiMessage::noteOn (1, u.note, (juce::uint8) u.vel) : juce::MidiMessage::noteOff (1, u.note), 0);
    });

    Performer::Settings ps;
    ps.key = (int) keyP->load(); ps.scale = (int) scaleP->load(); ps.chord = (int) chordP->load();
    ps.arp = arpP->load() > 0.5f; ps.arpRate = (int) arpRateP->load(); ps.arpMode = (int) arpModeP->load();
    ps.arpOct = (int) arpOctP->load(); ps.arpGate = arpGateP->load();
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) ps.bpm = *b;
            ps.hostPlaying = pos->getIsPlaying();
            if (auto q = pos->getPpqPosition()) { ps.ppq = *q; ps.havePpq = true; }
        }
    performer.process (merged, performed, n, ps);
    for (const auto meta : performed)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn()) heldNotes[(size_t) m.getNoteNumber()].store (1, std::memory_order_relaxed);
        else if (m.isNoteOff()) heldNotes[(size_t) m.getNoteNumber()].store (0, std::memory_order_relaxed);
        else if (m.isAllNotesOff() || m.isAllSoundOff()) for (auto& h : heldNotes) h.store (0, std::memory_order_relaxed);
    }

    // per-block controls for the voices
    const double sr = sampleRateNow.load();
    synth.setMono (monoP->load() > 0.5f, legatoP->load() > 0.5f);
    ctl.lfoShape = (int) lfoShapeP->load(); ctl.lfoDest = (int) lfoDestP->load(); ctl.lfoDepth = lfoDepthP->load();
    ctl.wheelAmt = wheelAmtP->load(); ctl.velFilt = velFiltP->load(); ctl.glide = glideP->load();
    if (lfoSyncP->load() > 0.5f)
    {
        const double qn = lfoDivQuarterNotes ((int) lfoDivP->load());
        ctl.lfoInc = ps.bpm / 60.0 / sr / qn;
        ctl.lfoPhase = (ps.hostPlaying && ps.havePpq) ? ps.ppq / qn : lfoFree;     // locked to the host bar grid while playing
    }
    else { ctl.lfoInc = lfoRateP->load() / sr; ctl.lfoPhase = lfoFree; }
    lfoFree = ctl.lfoPhase + ctl.lfoInc * n;
    if (lfoFree > 1.0e6) lfoFree -= 1.0e6;

    synth.renderNextBlock (buffer, performed, 0, n);

    const float g = gain->load() * 1.3f;                 // ramped per block: no zipper on the volume knob
    if (lastGain < 0.0f) lastGain = g;
    buffer.applyGainRamp (0, n, lastGain, g);
    lastGain = g;

    FxParams p;
    p.drive = drive->load(); p.crush = crush->load(); p.chorus = chorus->load(); p.phaser = phaser->load();
    p.echo = echo->load(); p.verb = verb->load(); p.haunt = haunt->load();
    p.bpm = ps.bpm;
    fx.process (buffer, p);

    // safety limiter: untouched below -1 dBFS, smooth knee above, never reaches full scale
    constexpr float T = 0.891f, K = 1.0f - T;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        for (int i = 0; i < n; ++i)
        {
            const float x = d[i], ax = std::abs (x);
            if (ax > T) d[i] = std::copysign (T + K * std::tanh ((ax - T) / K), x);
            else if (! (ax <= T)) d[i] = 0.0f;           // NaN guard
        }
    }
    const float rms = buffer.getRMSLevel (0, 0, n);
    level.store (level.load() * 0.8f + juce::jmin (1.0f, rms * 4.0f) * 0.2f);
}

int SynthProcessor::getNumPrograms() { return numPresets; }
const juce::String SynthProcessor::getProgramName (int i) { return juce::isPositiveAndBelow (i, numPresets) ? presets[i].name : ""; }
juce::String SynthProcessor::getPresetCategory (int i) { return juce::isPositiveAndBelow (i, numPresets) ? presets[i].cat : ""; }

void SynthProcessor::setParam (const char* id, float v)
{
    if (auto* prm = apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

void SynthProcessor::resetParamsToDefault (bool soundOnly)
{
    static const char* v2[] { "lfoRate", "lfoSync", "lfoDiv", "lfoShape", "lfoDepth", "lfoDest", "glide", "mono", "legato", "wheelAmt", "velFilt",
                              "fType", "fDrive", "fLink", "fAttack", "fDecay", "fSustain", "fRelease" };
    if (soundOnly)
    {
        for (auto* id : v2) if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->getDefaultValue());
        return;
    }
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (rp->getParameterID() != "key" && rp->getParameterID() != "scale") rp->setValueNotifyingHost (rp->getDefaultValue());
}

void SynthProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, numPresets)) return;
    currentProgram = index;
    const auto& p = presets[index];
    resetParamsToDefault (true);           // LFO, glide, filter extras back to neutral: the preset sounds as it always did
    auto set = [this] (const char* id, float v) { setParam (id, v); };
    set ("waveA", (float) p.A); set ("waveB", (float) p.B); set ("morph", p.morph); set ("unison", (float) p.uni);
    set ("width", p.width); set ("sub", p.sub); set ("attack", juce::jmax (0.001f, p.a)); set ("decay", juce::jmax (0.01f, p.d));
    set ("sustain", p.s); set ("release", juce::jmax (0.01f, p.r)); set ("cutoff", p.cut); set ("res", p.res); set ("fenv", p.fenv);
    set ("echo", p.echo); set ("verb", p.verb); set ("drive", p.drive); set ("chorus", p.chorus); set ("phaser", p.phaser);
    set ("crush", p.crush); set ("haunt", p.haunt);
    {
        const juce::SpinLock::ScopedLockType sl (reqLock);
        patchIsUser = false; currentPatchName = {};
    }
    postFrames (makeShape (p.A), makeShape (p.B), p.A, p.B);
}

void SynthProcessor::initPatch()
{
    resetParamsToDefault (false);
    setParam ("waveA", 2); setParam ("waveB", 2); setParam ("morph", 0); setParam ("unison", 1); setParam ("width", 0); setParam ("sub", 0);
    setParam ("attack", 0.003f); setParam ("decay", 0.3f); setParam ("sustain", 1.0f); setParam ("release", 0.15f);
    setParam ("cutoff", 1.0f); setParam ("res", 0); setParam ("fenv", 0); setParam ("drive", 0); setParam ("echo", 0); setParam ("verb", 0);
    {
        const juce::SpinLock::ScopedLockType sl (reqLock);
        patchIsUser = false; currentPatchName = "Init Saw";
    }
    postFrames (makeShape (2), makeShape (2), 2, 2);
}

static juce::String frameToString (const Frame& f) { juce::MemoryBlock mb (f.data(), sizeof (float) * FRAME); return mb.toBase64Encoding(); }
static bool frameFromString (const juce::String& s, Frame& f)
{
    juce::MemoryBlock mb; if (! mb.fromBase64Encoding (s) || mb.getSize() != sizeof (float) * FRAME) return false;
    Frame t; std::memcpy (t.data(), mb.getData(), mb.getSize());
    for (auto v : t) if (! std::isfinite (v)) return false;
    f = t; return true;
}

juce::ValueTree SynthProcessor::makeStateTree()
{
    auto state = apvts.copyState();
    Frame a, b; juce::String name; bool user;
    {
        const juce::SpinLock::ScopedLockType sl (reqLock);
        a = request.has ? request.a : savedA;
        b = request.has ? request.b : savedB;
        name = currentPatchName; user = patchIsUser;
    }
    state.setProperty ("frameA", frameToString (a), nullptr);
    state.setProperty ("frameB", frameToString (b), nullptr);
    state.setProperty ("program", currentProgram, nullptr);
    state.setProperty ("patchName", name, nullptr);
    state.setProperty ("patchUser", user, nullptr);
    state.setProperty ("ghostyVersion", 2, nullptr);
    return state;
}

void SynthProcessor::applyStateTree (juce::ValueTree state)
{
    if (! state.isValid() || ! state.hasType (apvts.state.getType())) return;
    // Projects saved before v2 have no entry for the new parameters: give them their defaults (not whatever is set now).
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (! state.getChildWithProperty ("id", rp->getParameterID()).isValid())
            {
                juce::ValueTree c ("PARAM");
                c.setProperty ("id", rp->getParameterID(), nullptr);
                c.setProperty ("value", rp->convertFrom0to1 (rp->getDefaultValue()), nullptr);
                state.appendChild (c, nullptr);
            }
    apvts.replaceState (state);
    const int sa = (int) apvts.getRawParameterValue ("waveA")->load(), sb = (int) apvts.getRawParameterValue ("waveB")->load();
    Frame a = makeShape (sa), b = makeShape (sb);
    frameFromString (state.getProperty ("frameA").toString(), a);      // drawn frames saved with the state always win
    frameFromString (state.getProperty ("frameB").toString(), b);
    currentProgram = juce::jlimit (0, numPresets - 1, (int) state.getProperty ("program", 0));
    {
        const juce::SpinLock::ScopedLockType sl (reqLock);
        currentPatchName = state.getProperty ("patchName", "").toString();
        patchIsUser = (bool) state.getProperty ("patchUser", false);
    }
    postFrames (a, b, sa, sb);
}

void SynthProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = makeStateTree().createXml()) copyXmlToBinary (*xml, dest);
}

void SynthProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size)) applyStateTree (juce::ValueTree::fromXml (*xml));
}

// ------------------------------------------------------------------ user sounds
juce::File SynthProcessor::userPresetDir()
{
    auto d = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Ghosty").getChildFile ("Ghosty");
    d.createDirectory();
    return d;
}

juce::Array<juce::File> SynthProcessor::listUserPresets() const
{
    auto files = userPresetDir().findChildFiles (juce::File::findFiles, false, "*.ghostysound");
    struct ByName { static int compareElements (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()); } } cmp;
    files.sort (cmp);
    return files;
}

bool SynthProcessor::saveUserPreset (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty()) return false;
    {
        const juce::SpinLock::ScopedLockType sl (reqLock);
        currentPatchName = clean; patchIsUser = true;
    }
    auto xml = makeStateTree().createXml();
    return xml != nullptr && xml->writeTo (userPresetDir().getChildFile (clean + ".ghostysound"));
}

bool SynthProcessor::loadUserPreset (const juce::File& file)
{
    auto xml = juce::parseXML (file);
    if (xml == nullptr) return false;
    auto state = juce::ValueTree::fromXml (*xml);
    if (! state.hasType (apvts.state.getType())) return false;
    state.setProperty ("patchName", file.getFileNameWithoutExtension(), nullptr);
    state.setProperty ("patchUser", true, nullptr);
    applyStateTree (state);
    return true;
}

juce::String SynthProcessor::patchName() const
{
    const juce::SpinLock::ScopedLockType sl (reqLock);
    return currentPatchName;
}
bool SynthProcessor::isUserPatch() const
{
    const juce::SpinLock::ScopedLockType sl (reqLock);
    return patchIsUser;
}

juce::AudioProcessorEditor* SynthProcessor::createEditor() { return new SynthEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SynthProcessor(); }
