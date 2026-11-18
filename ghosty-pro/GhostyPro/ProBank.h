#pragma once
#include <juce_core/juce_core.h>
#include <vector>
#include <map>

class ProProcessor;

// FACTORY PRESETS (2026-10-26, docs/research/PRO-FACTORY-PRESETS-OFF.md): the shipped plugin has NO factory sound presets
// (CMake option GHOSTY_FACTORY_PRESETS, default OFF; ProTests builds it ON so the tests keep covering the data). OFF:
//   factoryBank() is empty: no BROWSE rows, no host programs, no categories; the bank / host programs are INIT + your sounds.
//   GHOSTY_FACTORY_LAYER_COMPAT (default ON) keeps the old sounds' data compiled in, hidden, ONLY so a project or a saved sound
//   that plays one of them as a STACK layer (it stores the layer by name) still sounds the same: factoryLibrary().
//   OFF + LAYER_COMPAT OFF: none of the old data is in the binary at all.
#ifndef GHOSTY_FACTORY_PRESETS
 #define GHOSTY_FACTORY_PRESETS 0
#endif
#ifndef GHOSTY_FACTORY_LAYER_COMPAT
 #define GHOSTY_FACTORY_LAYER_COMPAT 1
#endif
#define GHOSTY_FACTORY_DATA (GHOSTY_FACTORY_PRESETS || GHOSTY_FACTORY_LAYER_COMPAT)

// The Ghosty Pro factory bank. Each preset is a short text "spec" of parameter=value pairs applied on top of the
// INIT patch (every sound parameter at its default), plus an optional Ghost FX rack scene.
namespace gpro
{
struct FactoryPreset { juce::String cat, name, scene, spec; };
const std::vector<FactoryPreset>& factoryBank();        // the BROWSE / host-program factory sounds (empty when GHOSTY_FACTORY_PRESETS is OFF)
const std::vector<FactoryPreset>& factoryLibrary();     // every old factory sound, for STACK layers by name (= factoryBank() when ON;
                                                        // hidden when OFF; empty when OFF without GHOSTY_FACTORY_LAYER_COMPAT)
constexpr bool factoryPresetsBuilt() { return GHOSTY_FACTORY_PRESETS != 0; }
const juce::StringArray& bankCategories();          // in display order
void applySpec (ProProcessor&, const juce::String& spec);
void applyParams (ProProcessor&, const juce::String& spec);   // plain parameter=value tokens only (no mod / macro / layer tokens; the MACRO map is left alone)
juce::String expandSpec (const juce::String& spec);  // the spec with its @templates written out
bool isLegacy (const juce::String& name);            // SOUND ENGINE V2: hidden from BROWSE unless SHOW LEGACY (still loadable)
const juce::String& legacyCategory();                // "Legacy": the browser's category for them
float presetTrimDb (const juce::String& name);       // measured level trim (ProTrims.inc), 0 if unknown

// PRESET BROWSER (round 3 item 6): tags. Derived tags are computed from what a sound sets (unison, fm, sync, warp, mono, glide, arp,
// reverb, layered, clean); category tags are the words of its category ("Trap Bass" -> trap, bass). Never stored in a sound.
const juce::StringArray& derivedTagNames();                                       // in chip order
juce::StringArray categoryTags (const juce::String& cat);
juce::StringArray tagsFromValues (const std::map<juce::String, juce::String>& values, const juce::String& scene);   // parameter id -> value (a name or a number)
const juce::StringArray& factoryTags (int program);                               // derived + category tags of a factory sound (computed once)
juce::StringArray userPresetTags (const juce::File& file);                        // the same from a saved sound's parameters (cached by file + time)
}
