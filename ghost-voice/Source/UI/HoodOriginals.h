// Ghost Voice - AGPL-3.0-only (see LICENSE, NOTICE.md).
//
// UNDER THE HOOD's pages for the open-source engines other than the neural one (Brandon 2026-11-22: "the Original Controls page
// must truly match the original plug-ins' controls"). One page per engine, each laid out like that project's own UI, under its own
// control names, order, grouping, ranges and units, with a short plain-words line under every control, and greyed when it does
// nothing now:
//   INTERSECT  SOUND      its SignalChainBar (context row SAMPLE | SLICE n, ROOT, VOICES; TIME/PITCH, FILTER, AMP, PLAYBACK)
//   INTERSECT  AUTO CHOP  its AutoChopPanel (SENS, MIN, SPLIT TRANSIENTS, DIV, SPLIT EQUAL)
//   OPENVPI    PHRASES    audio-slicer's slicer2.py options (db_thresh, min_length, min_interval, hop_size, max_sil_kept)
//   FLAYR CHOP            its key / tempo read-outs and its AUTO-CHOP section
// Every control calls the engine's own existing parameters through ChopEngine (the same values the CHOP page shows); no DSP here.
#pragma once
#include "Choir.h"
#include <functional>

namespace gv { class Editor; }

namespace gv::hoodui
{
// one INTERSECT-style cell: its NAME, its value (drag up / down, the wheel, a click toggles or steps a choice) and a plain line under it
struct Cell : public juce::Component, public juce::SettableTooltipClient
{
    enum class Kind { number, choice, toggle, readOnly, button };
    Cell() { setRepaintsOnMouseActivity (true); }
    juce::String name, plain, tip, why;          // why: the reason it is greyed (shown as its tooltip)
    Kind kind = Kind::number;
    float lo = 0, hi = 1, step = 1, perPx = 0.01f, value = 0;
    bool logDrag = false;                        // CUT: drags in octaves, like INTERSECT's own cutoff mapping
    int cycle = 0;                               // how many choices a click steps through (0 = all)
    juce::StringArray choices;
    std::function<juce::String (float)> fmt;
    std::function<void (float value, bool dragging)> onChange;
    std::function<void()> onPress;
    bool own = false;                            // the chop sets this itself (INTERSECT's override), not the SAMPLE level
    bool on = true;
    bool showPlain = true;
    bool compact = false;                        // no name row: the box only (a module header's control)
    int id = -1;                                 // the engine field / option it drives (tests find cells by it)
    const choir::Pal* P = nullptr;

    void set (float v);                          // shows a value read back from the engine (no notification)
    void setOn (bool enabled, const juce::String& reason = {});
    bool press (float v);                        // what a user's edit does: the value, then onChange (false = greyed)
    juce::String valueText() const;
    static constexpr int kNameH = 16, kBoxH = 25, kPlainH = 28, kHeight = kNameH + kBoxH + 3 + kPlainH;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
private:
    void apply (float v, bool dragging);
    float quantise (float v) const;
    int startY = 0; float startV = 0; bool dragged = false;
    choir::Cache art;
};

// one engine's page
class Panel : public juce::Component
{
public:
    virtual ~Panel() = default;
    virtual void sync() = 0;                                 // follow the engine (called often; repaints only what changed)
    virtual juce::String title() const = 0;                  // the sub-tab's title (the project's own name)
    virtual juce::String subLine() const = 0;                // the page heading's line
    virtual juce::String countLine() const = 0;              // the gold line under it
    virtual std::vector<std::pair<juce::String, juce::String>> sideRows() const = 0;   // the left column's facts
    // tests: drive a control the way a user would
    virtual bool touch (int id, float value) { return press (id, value); }
    virtual bool hasControl (int id) const { return find (id) != nullptr; }
    virtual bool pressButton (const juce::String&) { return false; }
    virtual void setScope (bool /*slice*/) {}
    Cell* find (int id) const;
    juce::Array<Cell*> cells() const;
protected:
    bool press (int id, float v) { auto* c = find (id); return c != nullptr && c->press (v); }
};

std::unique_ptr<Panel> makeIntersectSound (Editor&);
std::unique_ptr<Panel> makeIntersectAutoChop (Editor&);
std::unique_ptr<Panel> makePhrases (Editor&);
std::unique_ptr<Panel> makeFlayr (Editor&);

// ids for the controls that are not INTERSECT fields (tests and the pages share them)
enum Ids { idVoices = 1000, idRoot, idSlices, idNote, idSetBpm, idSens = 1100, idMin, idDiv, idDb = 1200, idMinLength, idMinInterval, idHop, idMaxSil,
           idFlayrMode = 1300, idFlayrPads };
}
