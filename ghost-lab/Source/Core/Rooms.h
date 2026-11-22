#pragma once
// THE ROOMS (build 12): the engines that moved in from Ghost Matter, each one a room of the lab with one job.
//   GROW     a sound out of nothing: Flues (danja/flues, MIT), its own LV2 plug-in code at its own defaults
//   STRETCH  the extreme stretch: PaulXStretch (essej/paulxstretch, GPL-3), its own offline render chain
//   REBORN   your sound played back by a neural instrument: Magenta's DDSP-VST (Apache-2.0), its own processor
//   SPACE    where the sound sits around your head: Pertylizer (pertyjons/pertylizer, MIT), its own Rust crates
//   SPECTRE  (build 17) the sound rebuilt from up to 1,024 sine waves, bent live: our own engine (Spectre.h)
// Every engine runs unmodified (Source/Matter is the glue Ghost Matter already used; third_party/ is the upstream code).
// Our controls only set the engines' own controls. Each run is one step in the lab's history, like any card.
// This file is AGPL-3.0-only with the rest of Ghost Lab from build 12 on (see LICENSE, NOTICE.md).
#include "AudioTools.h"
#include "Actions.h"
#include <juce_graphics/juce_graphics.h>

namespace glab::rooms
{
enum class Room { Lab = 0, Grow, Stretch, Reborn, Space, Spectre, Count };   // (Spectre, build 17: the page Ghost Lab opens on)
struct RoomInfo
{
    Room room;
    Op op;
    const char *name, *does, *bestOn, *engine;   // engine: only shown with the "i" (credits)
};
const RoomInfo& info (Room r);
Room roomOf (Op op);                       // Room::Lab for every op that is not a room's

// ---- the choices the rooms offer (the engines' own lists)
const juce::StringArray& growSources();                    // PHYSICAL, VOICE, NOISE
const juce::StringArray& growBodies (int source);          // the bodies of that source
juce::StringArray rebornInstruments();                     // the neural models (empty when this build has none)
bool rebornAvailable();
void pretendInstrumentsForShots (const juce::StringArray& names);   // screenshots only: the page as it looks with instruments (nothing renders)
// the notes a real instrument of that name plays comfortably (MIDI low, high); the picture's range band and AUTO use it
juce::Range<float> instrumentRange (const juce::String& instrument);

// ---- build 13: each room's page is laid out like the original plug-in, with the original's own controls (its names,
// ranges and defaults, from the engines' control tables: Source/Matter/Hood). A control's value lives in the room's Step
// under the engine's own key ("pxs.fftsize0", "ddsp.PitchShift", "panner.x", "fdn.decay"); one you have not moved is the
// original's default.
struct Ctl
{
    juce::String key, name, plain, group, unit;   // name: the original's own name; plain: one line in plain words (ours)
    char kind = 'f';                               // 'f' decimal, 'i' whole number, 'b' on / off, 'c' a choice
    float lo = 0, hi = 1, def = 0;
    juce::StringArray choices;
    int module = -1;                               // STRETCH: the spectral module (0..8) whose switch turns this control on
};
const std::vector<Ctl>& controls (Room r);
const Ctl* control (Room r, const juce::String& key);
float val (const Step& s, const juce::String& key);          // the value the engine gets for that key
void spacePins (const Step& s, float* values17);             // SPACE: the 9 panner + 8 reverb values, in the engines' order
constexpr int kSpaceControls = 17;
}
namespace matter::hood { struct LivePins; }
namespace glab::rooms
{
matter::hood::LivePins livePinsFor (const float* values17);   // those 17 as the space engine takes them
const juce::StringArray& stretchModules();                   // PaulXStretch's nine spectral modules, in its chain order
juce::String moduleKey (int module);                         // "pxs.enab_specmodule3"

// ---- the renders (worker thread; never the audio thread)
// GROW makes a new sound (no input). Empty + error on failure.
std::shared_ptr<Audio> grow (const Step& s, juce::String& error);
// STRETCH / REBORN / SPACE work on `in`. Finished like every lab result (fades, level, true peak).
RenderResult render (const Step& s, const Audio& in, const std::atomic<bool>* cancel = nullptr);
bool isRoomOp (Op op);
juce::String describe (const Step& s);     // "STRETCH 12x", "REBORN as VIOLIN", "GROW BELL", "SPACE left, far"

// ---- what the pages draw (message thread; cached by the caller per sound)
// the sound as a picture: time across, pitch up (40 Hz .. 16 kHz, log), brightness = loudness. ARGB, cols x rows.
juce::Image spectrogram (const Audio& a, int cols, int rows, juce::Colour low, juce::Colour high);
// the note the sound is singing over time (MIDI note numbers; 0 where there is no clear note) and how loud (0..1)
struct Melody { std::vector<float> note, loud; float median = 0.0f; float voiced = 0.0f; };   // voiced: 0..1 how much of it has a clear note
Melody melody (const Audio& a, int columns = 160);
// the semitone shift that puts the sound's middle note in the middle of the instrument's range (whole octaves)
int autoShift (const Melody& m, juce::Range<float> range);
}
