// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
//
// Every setting of the original Samplebrain, in plain words, with its original name.
// Ranges and steps are the Qt controls' (gui/samplebrain.ui); values reach the engine with the
// same scaling the Qt app's OSC messages used (app/MainWindow.h, app/audio_thread.cpp).
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace mosaic
{
enum class Group { Search, Target, Mix, Library, Play, Ours };

struct ParamInfo
{
    const char* id;
    const char* label;        // GhostBlock name (plain words)
    const char* original;     // the Qt app's name
    const char* help;         // one plain sentence
    Group group;
    float min, max, step, def;
    bool rebuildsLibrary = false, rebuildsTarget = false;
    const char* choices = nullptr;   // "|"-separated, for menus
};

// window shapes, in the engine's order (window::type)
inline const char* kWindowChoices = "Soft ramp (dodgy)|Triangle (bartlett)|Blackman|Flat top|Gaussian|Hamming|Hann|Hard edge (rectangle)";
inline const char* kAlgoChoices = "Closest (basic)|Most different (reversed)|Network (synaptic)|Network + wait (slide)";

inline const std::vector<ParamInfo>& paramTable()
{
    static const std::vector<ParamInfo> t = {
        // ---- search ("brain tweaks")
        { "ratio", "TONE / TIMBRE", "fft / mfcc", "Match by raw frequencies (left) or by how the sound is heard (right), or a mix.", Group::Search, 0, 1, 0.01f, 0.5f },
        { "n_ratio", "SHAPE / PITCH ONLY", "freq & dynamics / freq only", "Match pitch and loudness changes (left), or pitch only, ignoring loudness (right).", Group::Search, 0, 1, 0.01f, 0 },
        { "fft_start", "LOWEST BAND", "fft subsection: start", "Lowest of the 100 frequency bands used for matching.", Group::Search, 0, 100, 1, 0 },
        { "fft_end", "HIGHEST BAND", "fft subsection: end", "Highest of the 100 frequency bands used for matching.", Group::Search, 0, 100, 1, 100 },
        { "novelty", "NOVELTY", "novelty", "Prefer pieces that haven't been used yet over the closest match.", Group::Search, 0, 1, 0.01f, 0 },
        { "boredom", "BOREDOM", "boredom", "How long a piece stays 'used' before it can come back.", Group::Search, 0, 1, 0.01f, 0.9f },
        { "stickyness", "SMOOTH", "stickyness", "Keep playing the next piece of the same source instead of jumping. Longer chunks.", Group::Search, 0, 1, 0.01f, 0 },
        { "stretch", "REPEAT", "search stretch", "Search each target piece this many times (a simple time-stretch).", Group::Search, 1, 50, 1, 1 },
        { "algo", "SEARCH STYLE", "algorithm", "How the match is found. Network is fast with big libraries.", Group::Search, 0, 3, 1, 0, false, false, kAlgoChoices },
        { "synapses", "CONNECTIONS", "num synapses", "How many neighbours to check in Network styles (closest first).", Group::Search, 0, 1000, 1, 20 },
        { "slide_error", "WAIT UNTIL CLOSE", "synaptic slide error", "In Network + wait: how close a match must be before the target moves on.", Group::Search, 0, 10000, 100, 1000 },
        // ---- target sound
        { "tgt_block", "TARGET PIECE SIZE", "target: block size", "Length of each piece of the target, in samples.", Group::Target, 10, 99999, 1, 7237, false, true },
        { "tgt_overlap", "TARGET OVERLAP", "target: block overlap", "How much the target pieces overlap.", Group::Target, 0, 0.99f, 0.01f, 0.81f, false, true },
        { "tgt_window", "TARGET FADE SHAPE", "target: window shape", "The fade on each target piece before it is analysed.", Group::Target, 0, 7, 1, 5, false, true, kWindowChoices },
        // ---- mix
        { "autotune", "AUTOTUNE", "autotune", "Bend each source piece towards the target's pitch.", Group::Mix, 0, 1, 0.01f, 0 },
        { "n_mix", "FLATTEN", "normalised", "Mix in level-flattened pieces (no loudness changes). Pairs with PITCH ONLY.", Group::Mix, 0, 1, 0.01f, 0 },
        { "target_mix", "ORIGINAL MIX", "brain / target", "Blend the target's own sound back in.", Group::Mix, 0, 1, 0.01f, 0 },
        { "stereo", "STEREO", "stereo mode", "Separate searches for left and right.", Group::Mix, 0, 1, 1, 0 },
        // ---- library ("brain contents")
        { "src_block", "PIECE SIZE", "brain: block size", "Length of each piece the source sounds are cut into, in samples.", Group::Library, 10, 99999, 1, 5304, true },
        { "src_overlap", "PIECE OVERLAP", "brain: block overlap", "How much the source pieces overlap.", Group::Library, 0, 0.99f, 0.01f, 0.82f, true },
        { "src_window", "PIECE FADE SHAPE", "brain: window shape", "The fade on each source piece.", Group::Library, 0, 7, 1, 5, true, false, kWindowChoices },
        // ---- playback
        { "volume", "VOLUME", "volume", "Output level (the original dial).", Group::Play, 0, 0.99f, 0.01f, 0.1f },
        // ---- Ghost Mosaic additions
        { "play_mode", "PLAY BY", "(new)", "Free: the play button. Keys: hold a MIDI note. Host: follows your DAW's play.", Group::Ours, 0, 2, 1, 0, false, false, "Play button|MIDI keys|DAW play" },
        { "key_pitch", "KEYS CHANGE PITCH", "(new)", "MIDI notes transpose the source pieces (C3 = as is).", Group::Ours, 0, 1, 1, 0 },
        { "live_input", "TARGET = LIVE INPUT", "use mic input", "The effect's audio input is the target, live.", Group::Ours, 0, 1, 1, 1 },
        { "dry", "DRY", "(new)", "The untouched input, delayed to line up (effect only).", Group::Ours, 0, 1, 0.01f, 0 },
        // (build 13, Brandon: open-source engines stay pure. Both extras are OFF by default: the output is Samplebrain's own.
        //  Sessions saved with them on keep them on.)
        { "limiter", "SAFETY LIMITER", "(new)", "Extra, off by default: keeps peaks under -1 dB (true peak), adding a short delay. Off = the original engine's output, untouched.", Group::Ours, 0, 1, 1, 0 },
        { "declick", "SOFT START/STOP", "(new)", "Extra, off by default: short fades when playback starts or stops. Off = starts and stops like the original.", Group::Ours, 0, 1, 1, 0 },
        // build 15: how lively TERRAIN is. Random wandering of the controls the land maps to (amount scaled by how much land there is,
        // so flat land has none), in steps on the beat or free. 0 = TERRAIN moves only what the shape says.
        { "move_amount", "MOVEMENT", "(new)", "TERRAIN: how much the controls it moves wander at random around where the land puts them. 0 = only the shape moves them.", Group::Ours, 0, 1, 0.01f, 0.6f },
        { "move_rate", "MOVEMENT RATE", "(new)", "How often MOVEMENT picks new places: free, or on the beat (your DAW's tempo).", Group::Ours, 0, 5, 1, 1, false, false, "Free|1 beat|2 beats|1 bar|2 bars|4 bars" },
        { "auto_build", "AUTO BUILD", "(new)", "Rebuild the library and target by itself when sounds or sizes change.", Group::Ours, 0, 1, 1, 1 },
    };
    return t;
}

// the original's name as the screen shows it (the screen never says "brain": it is the library)
inline juce::String shownOriginal (const ParamInfo& p)
{
    return juce::String (p.original).replace ("brain", "library");
}

inline const ParamInfo& paramInfo (const juce::String& id)
{
    for (auto& p : paramTable()) if (id == p.id) return p;
    jassertfalse; return paramTable().front();
}

inline juce::AudioProcessorValueTreeState::ParameterLayout makeLayout (bool isEffect)
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    for (auto& p : paramTable())
    {
        if (! isEffect && (juce::String (p.id) == "live_input" || juce::String (p.id) == "dry")) continue;
        juce::String name = juce::String (p.label).toLowerCase();
        name = name.substring (0, 1).toUpperCase() + name.substring (1);
        if (p.choices != nullptr)
            l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { p.id, 1 }, name, juce::StringArray::fromTokens (p.choices, "|", ""), (int) p.def));
        else if (p.min == 0 && p.max == 1 && p.step == 1)
            l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { p.id, 1 }, name, p.def > 0.5f));
        else
            l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { p.id, 1 }, name, juce::NormalisableRange<float> (p.min, p.max, p.step), p.def));
    }
    return l;
}
}
