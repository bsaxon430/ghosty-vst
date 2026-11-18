#pragma once
// Ghosty Spectrum's tabs, in order. ONE table drives the tab bar at the top of the editor; each page is made by
// SpecEditor::Content::makePage(kind). To add a page: add a Kind, a line here and a case in makePage.
#include <juce_core/juce_core.h>
#include <vector>

namespace sp {
enum class TabKind { Pads, Loop, Lab, Process, Arcs, Arrange, Voice };
struct TabDef { TabKind kind; const char* label; juce::uint32 colour; const char* tip; const char* id; int width; };
inline const std::vector<TabDef>& tabs() {
    static const std::vector<TabDef> t {
        { TabKind::Pads,    "PAINT PADS",   0xffff4fd8, "Paint ringing notes and pads",                                                  "tab_pads",    88 },
        { TabKind::Loop,    "PAINT LOOP",   0xff45e6ff, "Draw a loop in time with sounds and drums",                                     "tab_loop",    88 },
        { TabKind::Lab,     "SPECTRAL LAB", 0xff3cff8f, "Paint the sound itself: pictures to sound, ghosts, band rhythms, chaos, morph", "labButton",   98 },
#if GHOSTY_PROCESS_LAB
        { TabKind::Process, "PROCESS",      0xffa855f7, "Destructive processes on the Lab's sound (reverse, stretch, spectral ...), then BOUNCE NEW", "tab_process",  78 },
#endif
        { TabKind::Arcs,    "ARCS",         0xffffc35a, "Draw lines that become voices",                                                 "tab_arcs",     56 },
        { TabKind::Arrange, "ARRANGE",      0xff60a5fa, "Sequence the sounds you made into a piece",                                     "tab_arrange",  78 },
#if GHOSTY_VOICE
        { TabKind::Voice,   "VOICE",        0xffff4fd8, "Pitch and formants of a voice: move, borrow, draw, vowels, tune, double, harmony",  "tab_voice",    62 },
#endif
    };
    return t;
}
} // namespace sp
