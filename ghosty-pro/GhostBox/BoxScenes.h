#pragma once
// GHOST SCENES, the message side (Ghost Box): 8 scene slots that each hold a whole sound in the SNAPSHOT format (BoxProcessor::
// captureSound: every unit, setting, order, pattern, curve, MORPH / MOTION / MACROS; never the look, BYPASS, the SPIRIT BOARD, the
// IN / OUT gains or FIXED LATENCY), their names, and how scenes change (CUT / MORPH / SPILL, quantise, morph time, MIDI keys).
// Saved inside the session / host preset (<GHOSTSCENES> next to the parameters); an older session has none: 8 empty slots, nothing
// changes. The audio side is gpfx::SceneEngine (GhostyPro/fx/FxBoxScenes.h).
//
// Slots start EMPTY with the default names CLEAN, FAT, DREAM, BROKEN, SPACE, FREEZE, CHAOS, DROP (names only: the factory sound is
// unchanged). Clicking an empty slot stores what you hear into it. Every slot edit (store / clear / rename / copy / starter) is one step
// of the scene undo (32 deep).
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include "FxBoxScenes.h"
#include <array>
#include <vector>

namespace boxscenes {

constexpr int kSlots = gpfx::kScenes;
inline const juce::StringArray& defaultNames() { static juce::StringArray s { "CLEAN", "FAT", "DREAM", "BROKEN", "SPACE", "FREEZE", "CHAOS", "DROP" }; return s; }
inline juce::String noteName (int n) { return juce::MidiMessage::getMidiNoteName (juce::jlimit (0, 127, n), true, true, 3); }   // C3 = 60 (the most common DAW naming)

struct Slot
{
    juce::String name; juce::ValueTree sound;
    bool filled() const { return sound.isValid(); }
    bool operator== (const Slot& o) const { return name == o.name && filled() == o.filled() && (! filled() || sound.isEquivalentTo (o.sound)); }
};
struct Settings
{
    int mode = gpfx::smCut, quant = 3, morphLen = 4;   // CUT, BEAT, 1 BAR
    bool midiOn = true, midiLatch = false; int midiBase = 36;   // C1 (= MIDI note 36) .. G1 trigger scenes 1..8
    bool operator== (const Settings& o) const { return mode == o.mode && quant == o.quant && morphLen == o.morphLen && midiOn == o.midiOn && midiLatch == o.midiLatch && midiBase == o.midiBase; }
};

class Bank
{
public:
    std::array<Slot, kSlots> slots; Settings set;
    Bank() { reset(); }
    void reset() { for (int i = 0; i < kSlots; ++i) slots[(size_t) i] = { defaultNames()[i], {} }; set = {}; undoStack.clear(); }
    bool anyFilled() const { for (auto& s : slots) if (s.filled()) return true; return false; }

    juce::ValueTree toTree() const
    {
        juce::ValueTree t ("GHOSTSCENES");
        t.setProperty ("version", 1, nullptr);
        t.setProperty ("mode", gpfx::sceneModeNames()[set.mode], nullptr); t.setProperty ("quant", gpfx::sceneQuantNames()[set.quant], nullptr);
        t.setProperty ("morph", gpfx::sceneMorphNames()[set.morphLen], nullptr);
        t.setProperty ("midi", set.midiOn, nullptr); t.setProperty ("midiBase", set.midiBase, nullptr); t.setProperty ("midiLatch", set.midiLatch, nullptr);
        for (int i = 0; i < kSlots; ++i)
        {
            const auto& s = slots[(size_t) i];
            juce::ValueTree c ("SCENE"); c.setProperty ("i", i, nullptr); c.setProperty ("name", s.name, nullptr);
            if (s.filled()) c.appendChild (s.sound.createCopy(), nullptr);
            t.appendChild (c, nullptr);
        }
        return t;
    }
    void fromTree (const juce::ValueTree& t)
    {   // unknown / missing values keep their defaults (an older or newer session never fails to load)
        reset();
        if (! t.isValid() || ! t.hasType ("GHOSTSCENES")) return;
        auto pick = [] (const juce::StringArray& names, const juce::var& v, int def) { const int i = names.indexOf (v.toString()); return i >= 0 ? i : def; };
        set.mode = pick (gpfx::sceneModeNames(), t.getProperty ("mode"), set.mode);
        set.quant = pick (gpfx::sceneQuantNames(), t.getProperty ("quant"), set.quant);
        set.morphLen = pick (gpfx::sceneMorphNames(), t.getProperty ("morph"), set.morphLen);
        set.midiOn = (bool) t.getProperty ("midi", set.midiOn); set.midiLatch = (bool) t.getProperty ("midiLatch", set.midiLatch);
        set.midiBase = juce::jlimit (0, 120, (int) t.getProperty ("midiBase", set.midiBase));
        for (auto c : t)
        {
            if (! c.hasType ("SCENE")) continue;
            const int i = c.getProperty ("i", -1);
            if (! juce::isPositiveAndBelow (i, kSlots)) continue;
            auto& s = slots[(size_t) i];
            const auto nm = c.getProperty ("name").toString().trim();
            if (nm.isNotEmpty()) s.name = nm.substring (0, 24);
            s.sound = c.getNumChildren() > 0 ? c.getChild (0).createCopy() : juce::ValueTree();
        }
    }

    // ---- slot edits (each one undoable)
    void pushUndo() { undoStack.push_back (slots); if (undoStack.size() > 32) undoStack.erase (undoStack.begin()); }
    bool canUndo() const { return ! undoStack.empty(); }
    bool undo() { if (undoStack.empty()) return false; slots = undoStack.back(); undoStack.pop_back(); return true; }
    void store (int i, const juce::ValueTree& sound) { if (! valid (i)) return; pushUndo(); slots[(size_t) i].sound = sound.createCopy(); }
    void clear (int i) { if (! valid (i) || ! slots[(size_t) i].filled()) return; pushUndo(); slots[(size_t) i].sound = {}; }
    void rename (int i, const juce::String& n) { if (! valid (i) || n.trim().isEmpty() || n.trim() == slots[(size_t) i].name) return; pushUndo(); slots[(size_t) i].name = n.trim().substring (0, 24); }
    void copy (int from, int to) { if (! valid (from) || ! valid (to) || from == to) return; pushUndo(); slots[(size_t) to].sound = slots[(size_t) from].sound.createCopy(); }
    static bool valid (int i) { return juce::isPositiveAndBelow (i, kSlots); }

private:
    std::vector<std::array<Slot, kSlots>> undoStack;
};

} // namespace boxscenes
