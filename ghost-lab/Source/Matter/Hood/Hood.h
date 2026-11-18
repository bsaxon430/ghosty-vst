// Ghost Matter - AGPL-3.0-only (see LICENSE, NOTICE.md).
//
// UNDER THE HOOD, the logic: what each control shows (the value the engine is getting), what moving it does (a pin, or the
// page setting it is linked to), and the pins the audio thread reads. Message thread only, except livePins() results.
#pragma once
#include "HoodDefs.h"
#include "Settings.h"

namespace matter::hood
{
// what the live engines are being given by the other pages right now (so a control you have not moved shows that)
struct LiveView
{
    float spaceX = 0.5f, spaceY = 0.2f, room = 0.3f, tail = 0.25f, width = 1.0f;
    int steps = 16, hits = 5, rotate = 0;
    float mutate = 0.25f, range = 1.0f, gravity = 0.5f, springy = 0.75f;
    double matterSeconds = 4.0;
};

float value (const RenderSettings& r, int tableIndex, const LiveView* lv = nullptr);
bool isSet (const RenderSettings& r, int tableIndex);
void set (RenderSettings& r, int tableIndex, float v);
void clear (RenderSettings& r, int tableIndex);
void clearAll (RenderSettings& r);
int numSet (const RenderSettings& r);
bool isLinked (int tableIndex);                    // lives in a page's own setting (no pin)
bool isRenderEngine (const char* engine);          // changing it makes a new matter

LivePins livePins (const RenderSettings& r);

// for the stretch stage: PaulXStretch's own parameter by its id (the page's setting, your pin, or the plug-in's default)
float pxs (const StretchSettings& s, const char* id);
bool anyPxsPin (const StretchSettings& s);
}
