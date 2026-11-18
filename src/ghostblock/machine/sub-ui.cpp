/*
 * Poltergeist 2 (GhostBlock) - the ghost-machine editor: the SUB sign and the SUB panel (src/ghostblock/sub).
 *
 * The sign hangs under the VOICES plate on every page (ui-layout.json, pages.common: sub-sign); it is lit while SUB is
 * on. A press lets a board down beside it - the SUB panel (common/sub/sub.json, art made from the machine's own by
 * scripts/ghostblock/make_sub_art.py): twelve parts for SUB's twelve host parameters, eight preset plates, a readout.
 * The panel's parts are ordinary controls of this editor on a "page" of their own (kSubPage): they are on screen
 * while the panel is open, whatever page is showing, drawn with their board over everything else, and the board takes
 * the pointer over its whole area. Their values go through the FX page's plumbing (fxValue / fxSet / fxText) to
 * MachineEditor::subEdit - SubBass::uiEdit, which tells the host. Nothing here touches the sub's sound.
 */
#include "ghostblock/machine/machine-impl.h"

namespace ghostblock::p2::machine
{
using sub::SubBass;

void MachineEditor::Impl::subBuildControls()
{
    subRect = rectOf(as->subJson["panel"]);
    subCells = cellsOf(subRect.expanded(2));
    if (!ed.sub)
        return;
    for (auto &c : ctls)
    {
        if (c.page != kSubPage)
            continue;
        const auto tail = c.id.fromFirstOccurrenceOf("sub-", false, false);
        if (tail.startsWith("preset"))
        {
            c.fx = FX_SubPreset;
            c.index = tail.getTrailingIntValue() - 1;
            c.name = sub::presetName(c.index);
            c.label = c.rect.reduced(2, 4);
            c.hasLabel = true;
            c.lcls = lcBtn;
            c.spriteName.clear();
        }
        else if (tail == "close")
        {
            c.fx = FX_SubClose;
            c.label = c.rect.reduced(2, 4);
            c.hasLabel = true;
            c.lcls = lcBtn;
            c.spriteName.clear();
        }
        else if (tail == "title")
            c.fx = FX_SubTitle;
        else if (tail == "readout")
            c.fx = FX_SubRead;
        else
            for (uint32_t i = 0; i < SubBass::kParams; ++i)
            {
                const auto &d = SubBass::desc(SubBass::paramIdAt(i));
                if (tail == d.key)
                {
                    c.fx = FX_Sub;
                    c.index = (int)i;
                    c.name = d.label;
                    c.two = true;
                }
            }
    }
}

bool MachineEditor::Impl::subValue(const Ctl &c, FxV &v) const
{
    if (!ed.sub || c.fx != FX_Sub)
        return false;
    const uint32_t id = SubBass::paramIdAt((uint32_t)juce::jlimit(0, (int)SubBass::kParams - 1, c.index));
    const auto &d = SubBass::desc(id);
    v.ok = true;
    v.id = id;
    v.lo = d.min;
    v.hi = d.max;
    v.def = d.def;
    v.stepped = d.stepped;
    v.val = (float)ed.sub->getParam(id);
    return true;
}

juce::String MachineEditor::Impl::subReadout() const
{
    if (!ed.sub)
        return {};
    if (!ed.sub->on())
        return "OFF";
    const float hz = ed.sub->pitchHz();
    if (hz <= 0.f)
        return "ON  -  play a note";
    static const char *nm[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const int key = (int)std::lround(69.0 + 12.0 * std::log2((double)hz / 440.0));
    return juce::String(nm[((key % 12) + 12) % 12]) + juce::String(key / 12 - 1) + "   " + juce::String(hz, hz < 100.f ? 1 : 0) + " Hz";
}

// the plates that are not a parameter (the parameters go the FX page's way: fxRefresh)
bool MachineEditor::Impl::subRefresh(Ctl &c)
{
    bool on = false, off = false;
    juce::String t1 = c.name;
    switch (c.fx)
    {
    case FX_SubPreset:
        on = ed.sub != nullptr && ed.sub->currentPreset() == c.index;
        break;
    case FX_SubRead:
        t1 = subReadout();
        off = ed.sub == nullptr || !ed.sub->on();
        break;
    default:
        break;
    }
    if (on != c.on || off != c.labOff || t1 != c.t1)
    {
        c.on = on;
        c.labOff = off;
        c.t1 = t1;
        touch(c);
    }
    return true;
}

bool MachineEditor::Impl::subClick(Ctl &c)
{
    if (!ed.sub)
        return false;
    switch (c.fx)
    {
    case FX_SubPreset:
        touchCtl(c, true);
        c.pressAt = clock;
        ed.subPreset(c.index);
        ed.status(juce::String("SUB preset ") + sub::presetName(c.index) + ": SUB is on. LEVEL stays where you set it.");
        return true;
    case FX_SubClose:
        subToggle(false);
        return true;
    case FX_SubTitle:
    case FX_SubRead:
        return true;
    default:
        return false; // a wheel, a dial, a switch: handled like any other
    }
}

void MachineEditor::Impl::subToggle(bool open)
{
    if (open && !ed.sub)
        return;
    if (open == subOpen)
        return;
    if (open)
        closeLedger();
    if (hoverCtl >= 0 && hoverCtl < (int)ctls.size())
    {
        ctls[(size_t)hoverCtl].hover = false;
        hoverCtl = -1;
    }
    subOpen = open;
    sceneDirty.mark(subCells);
    ed.uiDirty = true;
    if (open)
        ed.status("SUB: a clean sub-bass under the lowest note. Top row: the note. Bottom row: its dirt. Below: eight presets.");
}

void MachineEditor::Impl::subTick(double)
{
    auto *s = ed.sub;
    if (s == nullptr)
        return;
    if (s->changeCount() != subChange)
    { // the host, a preset or a loaded state changed something: read it again (never while a part is held)
        subChange = s->changeCount();
        if (!drag.active)
            ed.uiDirty = true;
    }
    if (s->on() != subOnDrawn)
    {
        subOnDrawn = s->on();
        ed.uiDirty = true;
    }
    if (subOpen)
        if (auto *r = find("sub-readout"); r != nullptr && r->t1 != subReadout())
            subRefresh(*r);
}

// the board and its parts, over everything else on the page
void MachineEditor::Impl::subDraw(const Ctx &c)
{
    if (!subOpen || !c.touches(subRect.expanded(2)))
        return;
    c.affine(as->kit("sub-panel").bmp, juce::AffineTransform::translation(subRect.getX(), subRect.getY()), 1.0f);
    for (auto &k : ctls)
        if (k.page == kSubPage)
            drawCtlArt(c, k);
    for (auto &k : ctls)
        if (k.page == kSubPage)
            drawCtlText(c, k);
}
} // namespace ghostblock::p2::machine
