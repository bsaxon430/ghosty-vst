#pragma once
// Ghosty Pro's own presets: .vital files made on Vital's engine (ghosty/presets), compiled into the plugin.
// (Vital's own factory presets are under a licence that forbids sharing them; none are here.)
#include "JuceHeader.h"
#include <vector>

namespace ghosty {

struct FactoryPreset {
  String name;       // from the file name
  String category;   // the preset's own "preset_style" (Bass, Lead, Pad, Keys, Pluck, Sequence, SFX, ...)
  String text;       // the .vital json
};

const std::vector<FactoryPreset>& factoryPresets();
StringArray factoryCategories();

// Ghosty Pro's settings file (theme, added preset folders, last page); never in a project or a sound
PropertiesFile& settings();

} // namespace ghosty
