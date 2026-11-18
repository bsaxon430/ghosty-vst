// Ghost Matter - a free, open-source sound-design instrument by GhostBlock. AGPL-3.0-only (see LICENSE, NOTICE.md).
//
// The settings of the three render steps (MAKE, STRETCH, MORPH). They are not host parameters: changing one re-renders
// the "matter" on a background thread. They are saved with the session (and in presets) as a ValueTree.
#pragma once
#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <juce_graphics/juce_graphics.h>
#include <vector>
#include "HoodDefs.h"

namespace matter
{
constexpr double kRate = 48000.0;          // the matter is rendered at 48 kHz; the player resamples to the host's rate
constexpr int kRootNote = 48;              // C3: the made sounds are rendered at this pitch (MIDI keys play relative to it)
constexpr double kMaxSourceSeconds = 12.0; // a made / dropped sound is at most 12 s
constexpr double kMaxMatterSeconds = 60.0; // STRETCH stops at 60 s

enum class Source { physical = 0, voice, noise, words, own };
const juce::StringArray& sourceNames();                 // PHYSICAL, VOICE, NOISE, WORDS, YOUR SOUND
const juce::StringArray& bodyNames (Source s);          // the bodies of each source (plain words)

struct MakeSettings
{
    Source source = Source::physical;
    int body = 6;              // physical: BELL
    float loud = 0.5f;         // 0..1 the engine's own level control (PM-Synth DC / breath, Chatterbox + Disyn master gain)
    float bright = 0.5f;       // 0..1 where its brightness (spectral centre) sits
    float rough = 0.2f;        // 0..1 how inharmonic / gritty
    float shape = 0.5f;        // 0..1 the body's own character (strike force, vowel glide, generator colour)
    float seconds = 4.0f;      // 0.5..12
    int note = kRootNote;      // the pitch it is made at
    juce::MemoryBlock ownAudio;   // YOUR SOUND: the dropped audio (FLAC), saved with the session
    juce::String ownName;
    juce::String words { "ghost matter" };   // WORDS: what it says (body = which synthetic speaker)
    juce::MemoryBlock wordsAudio;  // WORDS made by the optional Bark helper (FLAC), kept so the session sounds the same anywhere
    hood::Pins pins;               // UNDER THE HOOD: the Flues controls you moved (pm. / cb. / ds.) and Bark's (bark.), in the engines' own units
    bool operator== (const MakeSettings& o) const;
};

struct StretchSettings
{
    float amount = 1.0f;       // 1..64 (1 = off)
    float size = 0.7f;         // 0..1 PaulXStretch's "FFT size" (its default 0.7): small = grainy detail, big = smooth wash
    float spread = 0.0f;       // 0..1 smears each partial (PaulXStretch "spread")
    float up = 0.0f;           // 0..1 an octave up mixed in (PaulXStretch "octave mixer")
    float down = 0.0f;         // 0..1 an octave down
    float pitch = 0.0f;        // -12..+12 semitones (PaulXStretch "pitch shift")
    float volume = -3.0f;      // -24..+12 dB PaulXStretch's own "Main volume" (its default -3): gain-staging with its control
    hood::Pins pins;           // UNDER THE HOOD: every other PaulXStretch parameter you moved (pxs.), in its own units
    juce::ValueTree freeFilter;   // UNDER THE HOOD: the plug-in's own free-filter envelope (its breakpoint_envelope::saveState tree); invalid = its default flat curve
    bool operator== (const StretchSettings& o) const;
};

enum class Timbre { none = 0, voice, metal, air, glass, throat, wood, own, count };
const juce::StringArray& timbreNames();                 // OFF, VOICE, METAL, AIR, GLASS, THROAT, WOOD, YOUR SOUND

struct MorphSettings
{
    Timbre timbre = Timbre::none;
    float amount = 0.6f;       // 0..1 CDP combine cross: how far our spectral amplitudes are replaced by the other sound's
    float detail = 0.0f;       // 0..1 GLIDE, CDP morph morph: the whole spectrum glides into the other sound (1 = all the way by the end)
    juce::MemoryBlock ownAudio;   // YOUR SOUND to morph with (FLAC)
    juce::String ownName;
    // NEURAL: Magenta's DDSP-VST (magenta/ddsp-vst, Apache-2.0), its own processor run offline on the sound (Ddsp.h). The
    // model listens to the pitch and loudness of the sound and plays them back as the instrument it was trained on.
    juce::String ddspModel;    // the model's name ("" = off): one of its 11 built in, or a .tflite in Documents/Magenta/DDSP/Models
    float neural = 0.0f;       // 0..1 dry / DDSP
    // CHAOS (ours, on top: they only move DDSP's own controls and what it is fed; at 0 the model plays as it is)
    float wobble = 0.0f;       // 0..1 its own pitch shift / input pitch / input gain jump about, held a moment each time
    float hop = 0.0f;          // 0..1 switches between its models while it plays (0 = never .. 1 = every 40 ms)
    float refeed = 0.0f;       // 0..1 its output fed back through it again (1 .. 4 passes)
    hood::Pins pins;           // UNDER THE HOOD: CDP (cdp.) and DDSP (ddsp.) controls you moved, in their own units
    bool operator== (const MorphSettings& o) const;
};

struct RenderSettings
{
    MakeSettings make;
    StretchSettings stretch;
    MorphSettings morph;
    hood::Pins livePins;       // UNDER THE HOOD: Pertylizer (panner. fdn. euclid. turing.) and Box2D (box2d.) controls you moved
    bool operator== (const RenderSettings& o) const { return make == o.make && stretch == o.stretch && morph == o.morph; }   // live pins never re-render
    bool operator!= (const RenderSettings& o) const { return ! (*this == o); }

    juce::ValueTree toTree() const;
    static RenderSettings fromTree (const juce::ValueTree& t);
};
}
