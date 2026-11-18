#pragma once
// LAYOUT 2 (round 3): the per-user LAYOUT choice (CLASSIC / STUDIO) and STUDIO's AMBIENT setting, kept in the user's settings file,
// never in a project or a sound; and the tracer wish (STUDIO shows the live envelope / LFO dots on its front page).
// The STUDIO layout itself (ProPanels2.inc, ProLayout2Studio.inc) is compiled inside ProEditor.cpp: it reuses the editor's panels.
// Modulation (source tiles, amount rings, the knob menu) is ProModUI's in both layouts: STUDIO places ModUi::makeTile tiles.
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace gpro::layout2
{
enum Layout { classic = 0, studio = 1 };

// the user's settings file (Ghosty Pro, folder GhostBlock); GHOSTY_SETTINGS_FILE overrides it (tests)
juce::File settingsFile();
int savedLayout();                 // STUDIO unless the user picked CLASSIC (owner 2026-10-25: STUDIO is the Ghosty Pro UI;
                                   // CLASSIC stays as a fallback until tested in FL, then goes: see ProLayout2Studio.inc's header)
void saveLayout (int layout);
juce::String layoutName (int layout);
bool ambientOn();                  // STUDIO's AMBIENT neon rain (default on)
void saveAmbient (bool on);

// the envelope / LFO tracer is published by the audio thread only while something shows it; STUDIO shows it on its front page
void wantTrace (const void* processor, bool on);
bool traceWanted (const void* processor);
} // namespace gpro::layout2
