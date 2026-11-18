/*
 * Poltergeist 2 (GhostBlock) - the ghost-machine editor: the FX page (SPECTRAL FX, src/ghostblock/spectral).
 *
 * The page's art and rects are the artist's (ui-layout.json pages.fx, resources/ghostblock/machine/fx). It was drawn
 * against an early header; the binding here follows the engine as it is:
 *   the tall ghost  = the chain: four Spectral Corruptor slots (glass = put it on the bench, name plate = effect ledger,
 *                     button = the slot's bypass), the mouth wheel = master MIX, the four drums = FFT size / window /
 *                     overlap / frequency limit;
 *   the bench       = the slot on it: SLOT MIX and eight wheels that take the names, ranges and words of the effect's
 *                     parameters (the page plate turns to the next eight when an effect has more); with no slot on it
 *                     (tap the lit glass again) it holds the chain's own CHAIN MIX and the ORDER;
 *   the flying town = WarpCore, a section of its own: the WARPCORE switch is its on / off, nine parameters;
 *   the hose        = the signal between the two; its valve flips the order (GHOST > TOWN or TOWN > GHOST);
 *   SPECTRAL FX     = the master switch; IN / OUT = the rack's real meters.
 * Every edit goes through MachineEditor::spectralEdit / SpectralFx::uiSlotValue: the same rules as the first editor's
 * SPECTRAL page (src/ghostblock/spectral/README.md). Nothing here touches the rack's code.
 */
#include "ghostblock/machine/machine-impl.h"

namespace ghostblock::p2::machine
{
using spectral::SpectralFx;
FPoint bezAt(const FPoint g[4], float t);
juce::Path bezierPath(const FPoint g[4]);
juce::Colour shade(juce::Colour c, float k);
extern const juce::Colour kAmber;

static juce::String str(const std::string &s) { return juce::String(juce::CharPointer_UTF8(s.c_str())); }
static const juce::Colour kSlotCol[4] = {juce::Colour(0xffff93a8), juce::Colour(0xffbda2ff), juce::Colour(0xff84e092), juce::Colour(0xff86d8ff)};

// ============================================================================================== what a control edits
int MachineEditor::Impl::fxBenchPages() const
{
    if (!ed.spectral || benchSlot < 0)
        return 1;
    const auto &t = spectral::types()[(size_t)juce::jlimit(0, (int)spectral::types().size() - 1, ed.spectral->slotType(benchSlot))];
    return juce::jmax(1, ((int)t.params.size() + 7) / 8);
}

bool MachineEditor::Impl::fxValue(const Ctl &c, FxV &v) const
{
    v = FxV();
    if (c.fx >= FX_Sub)
        return subValue(c, v);
    auto *fx = ed.spectral;
    if (fx == nullptr)
        return false;
    auto host = [&](uint32_t id, float lo, float hi, float def, bool stepped, float val) {
        v.ok = true;
        v.id = id;
        v.lo = lo;
        v.hi = hi;
        v.def = def;
        v.stepped = stepped;
        v.val = val;
        return true;
    };
    int kind = c.fx, s = c.slot;
    if (kind == FX_P && benchSlot < 0)
    { // nothing on the bench: it holds the chain's own two settings
        if (c.index == 0)
            kind = FX_ChainMix;
        else if (c.index == 1)
            kind = FX_Order;
        else
            return false;
    }
    switch (kind)
    {
    case FX_On:
        return host(SpectralFx::pOn, 0, 1, 0, true, fx->on() ? 1.f : 0.f);
    case FX_Mix:
        return host(SpectralFx::pMix, 0, 100, 100, false, fx->mix());
    case FX_Fft:
        return host(SpectralFx::pFft, 0, 7, 4, true, (float)fx->fftIndex());
    case FX_Window:
        return host(SpectralFx::pWindow, 0, 8, 0, true, (float)fx->windowIndex());
    case FX_Overlap:
        return host(SpectralFx::pOverlap, 2, 16, 4, true, std::round(fx->overlap()));
    case FX_Limit:
        return host(SpectralFx::pLimit, 0, 6, 2, true, (float)fx->limitIndex());
    case FX_ChainMix:
        return host(SpectralFx::pChainMix, 0, 100, 100, false, fx->chainMix());
    case FX_Order:
        return host(SpectralFx::pOrder, 0, 1, 0, true, (float)fx->order());
    case FX_SlotOn:
        return host(SpectralFx::pSlotOn(s), 0, 1, 1, true, fx->slotOn(s) ? 1.f : 0.f);
    case FX_SlotMix:
        if (benchSlot < 0)
            return false;
        return host(SpectralFx::pSlotMix(benchSlot), 0, 100, 100, false, fx->slotMix(benchSlot));
    case FX_WarpOn:
        return host(SpectralFx::pWarpOn, 0, 1, 0, true, fx->warpOn() ? 1.f : 0.f);
    case FX_Warp:
    {
        const auto &d = spectral::warpParams()[(size_t)juce::jlimit(0, spectral::kWarpParams - 1, c.index)];
        host(SpectralFx::pWarpParam(c.index), d.min, d.max, d.def, d.stepped(), fx->warpValue(c.index));
        v.d = &d;
        return true;
    }
    case FX_P:
    {
        const auto &t = spectral::types()[(size_t)juce::jlimit(0, (int)spectral::types().size() - 1, fx->slotType(benchSlot))];
        const int p = juce::jlimit(0, fxBenchPages() - 1, benchPage) * 8 + c.index;
        if (p >= (int)t.params.size())
            return false;
        const auto &d = t.params[(size_t)p];
        v.ok = true;
        v.d = &d;
        v.slot = benchSlot;
        v.p = p;
        v.id = p < spectral::kHostParamsPerSlot ? SpectralFx::pSlotParam(benchSlot, p) : 0;
        v.lo = d.min;
        v.hi = d.max;
        v.def = d.def;
        v.stepped = d.stepped();
        v.val = fx->slotValue(benchSlot, p);
        return true;
    }
    default:
        return false;
    }
}

float MachineEditor::Impl::fxNorm(const FxV &v, float real) const
{
    if (v.d != nullptr)
        return juce::jlimit(0.f, 1.f, v.d->toNorm(real));
    return v.hi > v.lo ? juce::jlimit(0.f, 1.f, (real - v.lo) / (v.hi - v.lo)) : 0.f;
}
float MachineEditor::Impl::fxReal(const FxV &v, float n) const
{
    float r = v.d != nullptr ? v.d->fromNorm(juce::jlimit(0.f, 1.f, n)) : v.lo + juce::jlimit(0.f, 1.f, n) * (v.hi - v.lo);
    if (v.stepped)
        r = std::round(r);
    return juce::jlimit(v.lo, v.hi, r);
}

// phase: 0 the gesture begins, 1 a value, 2 it ends
void MachineEditor::Impl::fxSet(const Ctl &c, float real, int phase)
{
    FxV v;
    if (!fxValue(c, v))
        return;
    real = juce::jlimit(v.lo, v.hi, v.stepped ? std::round(real) : real);
    if (c.fx >= FX_Sub)
    {
        ed.subEdit(v.id, (double)real, phase);
        return;
    }
    if (v.slot >= 0)
    { // a parameter of the slot's effect, in its own units: the first eight also go to the host, the others are ours
        ed.spectral->uiSlotValue(v.slot, v.p, real, phase);
        ed.spectralService();
        if (v.p >= spectral::kHostParamsPerSlot && phase == 2)
            ed.markStateDirty();
    }
    else
        ed.spectralEdit(v.id, (double)real, phase);
    if (phase == 1)
        env.poke(gSpectral, 0.4f);
    ed.uiDirty = true;
}

juce::String MachineEditor::Impl::fxText(const FxV &v, float real) const
{
    if (v.ok && sub::SubBass::isParam(v.id) && ed.sub)
    { // SUB's own words
        if (v.id == sub::SubBass::pMode)
            return sub::modeShortName((int)std::lround(real));
        if (v.id == sub::SubBass::pBlend)
            return real < 0.5f ? juce::String("CLEAN") : real > 99.5f ? juce::String("DIRTY") : juce::String(juce::roundToInt(real)) + " %";
        char b[64] = {};
        ed.sub->valueToText(v.id, (double)real, b, sizeof(b));
        return juce::String(juce::CharPointer_UTF8(b)).toUpperCase().replace("CENTS", "ct").replace("MS", "ms").replace("DB", "dB");
    }
    if (!v.ok || !ed.spectral)
        return {};
    if (v.slot >= 0 && v.d != nullptr)
        return compact(str(v.d->text(real)));
    if (v.id == SpectralFx::pOrder)
        return real > 0.5f ? "TOWN > GHOST" : "GHOST > TOWN";
    char buf[96] = {};
    if (ed.spectral->valueToText(v.id, (double)real, buf, sizeof(buf)))
        return compact(juce::String(juce::CharPointer_UTF8(buf)));
    return juce::String(real, 2);
}

juce::String MachineEditor::Impl::fxName(const Ctl &c) const
{
    if (c.fx == FX_P)
    {
        if (benchSlot < 0)
            return c.index == 0 ? "CHAIN MIX" : c.index == 1 ? "ORDER" : "-";
        FxV v;
        return fxValue(c, v) && v.d != nullptr ? str(v.d->name).toUpperCase() : juce::String("-");
    }
    return c.name;
}

juce::String MachineEditor::Impl::fxLatencyText() const
{
    if (!ed.spectral)
        return {};
    const auto told = ed.spectral->latency(), want = ed.spectral->latencyWanted();
    juce::String s = "Latency " + juce::String((int)want) + " smp (" + juce::String((double)want / 48.0, 1) + " ms)";
    if (told != want)
        s << ", the host knows " << juce::String((int)told) << " until it restarts the plugin";
    return s;
}

// ============================================================================================== building
void MachineEditor::Impl::fxBuildControls()
{
    const int pg = as->pageKeys.indexOf("fx");
    if (pg < 0)
        return;
    static const std::map<juce::String, int> kinds = {{"fx-mix", FX_Mix},       {"fx-fft", FX_Fft},     {"fx-window", FX_Window}, {"fx-overlap", FX_Overlap},
                                                      {"fx-limit", FX_Limit},   {"fx-on", FX_On},       {"fx-slot-mix", FX_SlotMix}, {"fx-in", FX_In},
                                                      {"fx-out", FX_Out},       {"fx-w-on", FX_WarpOn}, {"fx-w-gauge", FX_WGauge}, {"fx-bench-title", FX_Title},
                                                      {"fx-from", FX_From}};
    // the town's parts -> WarpCore's parameters, by upstream's ids
    static const std::map<juce::String, const char *> warp = {{"fx-w-warp", "warp"},   {"fx-w-freqmode", "freq_mode"}, {"fx-w-fhigh", "f_high"},      {"fx-w-scale", "scale"},
                                                              {"fx-w-poles", "poles"}, {"fx-w-pitch", "pitch"},        {"fx-w-formant", "pitch_affect"}, {"fx-w-fill", "fill_gap"},
                                                              {"fx-w-drywet", "drywet"}};
    const auto &wp = spectral::warpParams();
    for (auto &c : ctls)
    {
        if (c.page != pg)
            continue;
        if (auto it = kinds.find(c.id); it != kinds.end())
            c.fx = it->second;
        else if (auto w = warp.find(c.id); w != warp.end())
        {
            c.fx = FX_Warp;
            c.index = 0;
            for (int k = 0; k < (int)wp.size(); ++k)
                if (wp[(size_t)k].id == w->second)
                    c.index = k;
        }
        else if (c.id.startsWith("fx-slot") && c.id.endsWith("-name"))
            c.fx = FX_Name;
        else if (c.id.startsWith("fx-slot") && c.id.endsWith("-on"))
            c.fx = FX_SlotOn;
        else if (c.id.startsWith("fx-slot"))
            c.fx = FX_Glass;
        else if (c.id.startsWith("fx-p"))
        {
            c.fx = FX_P;
            c.index = c.id.getTrailingIntValue() - 1;
        }
        if (c.fx == FX_Name || c.fx == FX_SlotOn || c.fx == FX_Glass)
            c.slot = juce::jlimit(0, spectral::kSlots - 1, c.id.substring(7, 8).getIntValue() - 1);
        if (c.fx == FX_Name)
        {
            c.two = true;
            c.lcls = lcSnd;
            c.label = c.rect.reduced(2, 3);
            c.hasLabel = true;
        }
        if (c.fx == FX_Glass)
            c.interactive = true;
        if (c.fx == FX_Title)
        {
            c.interactive = true;
            c.lcls = lcBTitle;
            // the right end of the title plate is the page plate (effects with more than eight parameters)
            c.rect = c.rect.withWidth(c.rect.getWidth() - 44.0f);
            c.hit = c.rect;
            c.label = c.rect;
            c.hasLabel = true;
        }
        if (c.id == "fx-w-on")
            c.name = "WARPCORE";
        if (c.id == "fx-w-formant")
            c.name = "PITCH AFFECT";
        if (c.id == "fx-limit")
            c.name = "TOP";
    }
    auto add = [&](Ctl c) {
        c.page = pg;
        c.hit = c.rect;
        ctlIx[c.id] = (int)ctls.size();
        ctls.push_back(std::move(c));
    };
    if (auto *t = find("fx-bench-title"))
    { // the page plate: "1/5", a tap turns to the next eight parameters
        Ctl c;
        c.id = "fx-page";
        c.name = "PAGE";
        c.type = CType::Plunger;
        c.fx = FX_Page;
        c.interactive = true;
        c.rect = {t->rect.getRight() + 4.0f, t->rect.getY(), 40.0f, t->rect.getHeight()};
        c.centre = c.pivot = c.rect.getCentre();
        c.label = c.rect.reduced(2, 5);
        c.hasLabel = true;
        c.lcls = lcBtn;
        add(c);
    }
    { // the order valve rides the hose a quarter of the way from the ghost; its tag says which way the signal runs
        FPoint g[4];
        fxHoseGeom(g);
        const auto q = bezAt(g, 0.25f);
        Ctl c;
        c.id = "fx-order";
        c.name = "ORDER";
        c.type = CType::Wheel;
        c.fx = FX_Order;
        c.interactive = true;
        c.spriteName = "valve-s";
        c.sprScale = 1.25f;
        c.glowSize = 40.0f;
        c.centre = c.pivot = {std::round(q.x), std::round(q.y)};
        c.hasCentre = true;
        c.rect = {c.centre.x - 17.0f, c.centre.y - 17.0f, 34.0f, 34.0f};
        c.label = {c.centre.x - 44.0f, c.centre.y + 17.0f, 88.0f, 11.0f};
        c.hasLabel = true;
        c.lcls = lcTag;
        add(c);
    }
    // the common keys on the brow that the FX page has no room for stay as they are; nothing else to do
    if (stageBuilt.size() > (size_t)pg)
        stage[(size_t)pg].hook = [this](const Ctx &c) { fxDrawHose(c); };
    fxGlassCells.clear();
    for (auto &c : ctls)
        if (c.fx == FX_Glass)
            for (auto cell : cellsOf(c.rect.expanded(2)))
                fxGlassCells.push_back(cell);
    FPoint g[4];
    fxHoseGeom(g);
    CellSet hs;
    for (int i = 0; i <= 24; ++i)
    {
        const auto q = bezAt(g, (float)i / 24.0f);
        hs.mark(FRect(q.x - 12, q.y - 12, 24, 24));
    }
    if (auto *sm = as->layout["pages"]["fx"]["signal"]["main"].getArray())
        for (int i = 0; i + 1 < sm->size(); ++i)
        {
            const auto a = pointOf((*sm)[i]), b = pointOf((*sm)[i + 1]);
            hs.mark(FRect(juce::jmin(a.x, b.x) - 8, juce::jmin(a.y, b.y) - 8, std::abs(a.x - b.x) + 16, std::abs(a.y - b.y) + 16));
        }
    fxHoseCells.clear();
    for (int i = 0; i < NCELLS; ++i)
        if (hs.test(i))
            fxHoseCells.push_back((uint16_t)i);
}

void MachineEditor::Impl::fxHoseGeom(FPoint g[4]) const
{
    const auto &sg = as->layout["pages"]["fx"]["signal"];
    g[0] = pointOf(sg["out"]);
    g[3] = pointOf(sg["inn"]);
    g[1] = g[0] + FPoint(70, 128);
    g[2] = g[3] + FPoint(-70, 120);
}

// ============================================================================================== what it shows
bool MachineEditor::Impl::fxRefresh(Ctl &c)
{
    if (c.fx > FX_Sub)
        return subRefresh(c);
    auto *fx = ed.spectral;
    float v01 = c.v01, ang = c.angle;
    int step = c.step, steps = c.steps;
    bool on = c.on, off = c.labOff;
    juce::String t1 = c.t1, t2 = c.t2;
    juce::Colour col = c.colour;
    FxV v;
    const bool has = fxValue(c, v);
    switch (c.fx)
    {
    case FX_Glass:
        on = benchSlot == c.slot;
        break;
    case FX_Name:
    {
        const auto &t = spectral::types()[(size_t)juce::jlimit(0, (int)spectral::types().size() - 1, fx ? fx->slotType(c.slot) : 0)];
        const bool empty = t.index == 0;
        t1 = juce::String(c.slot + 1) + " - " + (empty ? juce::String("EMPTY") : str(t.category));
        t2 = empty ? juce::String("+ pick an effect") : str(t.name);
        off = empty;
        on = benchSlot == c.slot;
        col = kSlotCol[c.slot];
        break;
    }
    case FX_SlotOn:
        on = has && v.val > 0.5f;
        t1 = on ? "ON" : "OFF";
        break;
    case FX_Title:
        if (benchSlot < 0)
            t1 = "THE CHAIN";
        else
        {
            const auto &t = spectral::types()[(size_t)juce::jlimit(0, (int)spectral::types().size() - 1, fx ? fx->slotType(benchSlot) : 0)];
            t1 = "SLOT " + juce::String(benchSlot + 1) + " - " + (t.index == 0 ? juce::String("empty") : str(t.name));
            col = kSlotCol[benchSlot];
        }
        break;
    case FX_Page:
    {
        const int n = fxBenchPages();
        t1 = n > 1 ? juce::String(juce::jlimit(0, n - 1, benchPage) + 1) + "/" + juce::String(n) : juce::String();
        off = n <= 1;
        on = n > 1;
        break;
    }
    case FX_From:
        t1 = c.name;
        break;
    case FX_In:
    case FX_Out:
        t1 = c.name;
        break;
    case FX_WGauge:
        break;
    default:
        t1 = fxName(c);
        off = !has;
        if (c.type == CType::Switch)
        {
            on = has && v.val > 0.5f;
            ang = on ? 0.0f : 52.0f;
            if (c.fx == FX_Sub)
                t2 = has ? fxText(v, v.val) : juce::String();
            if (c.fx == FX_Warp && has && v.d != nullptr && v.d->id == "pitch_affect")
                t1 = "PITCH AFFECT"; // (the plate was drawn as FORMANT: the engine's parameter is pitch_affect)
        }
        else if (c.type == CType::Selector || c.type == CType::Drum)
        {
            steps = has ? (int)std::lround(v.hi - v.lo) + 1 : 1;
            step = has ? juce::jlimit(0, steps - 1, (int)std::lround(v.val - v.lo)) : 0;
            t2 = has ? fxText(v, v.val) : juce::String();
            if (c.type == CType::Drum)
            {
                t1 = c.fx == FX_Fft ? "FFT " + t2 : c.fx == FX_Overlap ? "OVERLAP " + t2 : c.fx == FX_Limit ? "TOP " + t2 : t2;
                t2.clear();
            }
            else
            { // (the SUB panel's dials sit in wells painted for the machine's own stepped dials)
                const float span = juce::jmin(130.0f, 26.0f * (float)(steps - 1));
                ang = steps <= 1 ? 0.f : c.fx == FX_Sub ? -span + 2.0f * span * (float)step / (float)(steps - 1) : -135.0f + 270.0f * (float)step / (float)(steps - 1);
            }
        }
        else
        { // a wheel
            v01 = has ? fxNorm(v, v.val) : 0.f;
            t2 = has ? fxText(v, v.val) : juce::String();
            if (c.fx == FX_Order)
            {
                t1 = has ? fxText(v, v.val) : juce::String("ORDER");
                t2.clear();
                v01 = has && v.val > 0.5f ? 1.0f : 0.0f;
            }
            ang = -135.0f + 270.0f * v01;
        }
        break;
    }
    const bool changed = std::abs(v01 - c.v01) > 1e-4f || std::abs(ang - c.angle) > 0.05f || step != c.step || steps != c.steps || on != c.on || off != c.labOff || t1 != c.t1 || t2 != c.t2 || col != c.colour;
    c.v01 = v01;
    c.angle = ang;
    c.step = step;
    c.steps = steps;
    c.on = on;
    c.labOff = off;
    c.t1 = t1;
    c.t2 = t2;
    c.colour = col;
    if (changed)
        touch(c);
    return true;
}

void MachineEditor::Impl::fxSelectSlot(int s)
{
    benchSlot = benchSlot == s ? -1 : juce::jlimit(0, spectral::kSlots - 1, s);
    benchPage = 0;
    if (benchSlot < 0)
        ed.status("THE CHAIN: CHAIN MIX = how much of the four slots you hear; ORDER = which runs first, the ghost's chain or the town (WarpCore).");
    else if (ed.spectral)
    {
        const auto &t = spectral::types()[(size_t)ed.spectral->slotType(benchSlot)];
        ed.status(t.index == 0 ? "SLOT " + juce::String(benchSlot + 1) + " is empty: tap its name plate to pick an effect."
                               : "SLOT " + juce::String(benchSlot + 1) + ": " + str(t.name) + " (" + str(t.category) + ") is on the workbench.");
    }
    ed.uiDirty = true;
    sceneDirty.mark(fxGlassCells);
}

// a tap on one of the page's own parts; false = not ours
bool MachineEditor::Impl::fxClick(Ctl &c)
{
    switch (c.fx)
    {
    case FX_Glass:
        touchCtl(c, true);
        fxSelectSlot(c.slot);
        return true;
    case FX_Name:
        touchCtl(c, true);
        if (benchSlot != c.slot)
            fxSelectSlot(c.slot);
        openLedger(Ledger::FxPick, c.slot);
        return true;
    case FX_Title:
        touchCtl(c, true);
        if (benchSlot >= 0)
            openLedger(Ledger::FxPick, benchSlot);
        return true;
    case FX_Page:
    {
        touchCtl(c, true);
        c.pressAt = clock;
        const int n = fxBenchPages();
        if (n > 1)
        {
            benchPage = (juce::jlimit(0, n - 1, benchPage) + 1) % n;
            ed.status("Parameters " + juce::String(benchPage * 8 + 1) + " to " + juce::String(benchPage * 8 + 8) + " of this effect." +
                      (benchPage > 0 ? "  (These are saved with the project; the host automates the first eight.)" : ""));
        }
        ed.uiDirty = true;
        return true;
    }
    case FX_SlotOn:
    {
        touchCtl(c, true);
        c.pressAt = clock;
        FxV v;
        if (fxValue(c, v))
        {
            const float to = v.val > 0.5f ? 0.f : 1.f;
            fxSet(c, to, 0);
            fxSet(c, to, 1);
            fxSet(c, to, 2);
            ed.status("SLOT " + juce::String(c.slot + 1) + (to > 0.5f ? " runs." : " is bypassed."));
        }
        return true;
    }
    case FX_Order:
    {
        touchCtl(c, true);
        FxV v;
        if (fxValue(c, v))
        {
            const float to = v.val > 0.5f ? 0.f : 1.f;
            fxSet(c, to, 0);
            fxSet(c, to, 1);
            fxSet(c, to, 2);
            ed.status(to > 0.5f ? "ORDER: the town (WarpCore) first, then the ghost's chain." : "ORDER: the ghost's chain first, then the town (WarpCore).");
        }
        return true;
    }
    default:
        return false;
    }
}

void MachineEditor::Impl::fxSetSlotType(int slot, int type)
{
    if (!ed.spectral)
        return;
    slot = juce::jlimit(0, spectral::kSlots - 1, slot);
    const double t = (double)juce::jlimit(0, (int)spectral::types().size() - 1, type);
    ed.spectralEdit(SpectralFx::pSlotType(slot), t, 0);
    ed.spectralEdit(SpectralFx::pSlotType(slot), t, 1);
    ed.spectralEdit(SpectralFx::pSlotType(slot), t, 2);
    benchSlot = slot;
    benchPage = 0;
    const auto &td = spectral::types()[(size_t)t];
    ed.status(td.index == 0 ? "SLOT " + juce::String(slot + 1) + " is empty."
                            : "SLOT " + juce::String(slot + 1) + ": " + str(td.name) + ".  " + fxLatencyText());
    ed.uiDirty = true;
    sceneDirty.mark(fxGlassCells);
}

// ============================================================================================== the clock
void MachineEditor::Impl::fxTick(double dt)
{
    auto *fx = ed.spectral;
    if (fx == nullptr)
        return;
    if (fx->changeCount() != fxChange)
    { // the host or a loaded state changed something: the page reads the rack again (never while a part is held)
        fxChange = fx->changeCount();
        if (!drag.active)
            ed.uiDirty = true;
    }
    auto &e = env.fx;
    const float k = juce::jmin(1.0f, (float)dt * 9.0f);
    e.on = fx->on();
    e.warpOn = e.on && fx->warpOn();
    e.in += (juce::jmin(1.0f, fx->inLevel()) - e.in) * k;
    e.out += (juce::jmin(1.0f, fx->outLevel()) - e.out) * k;
    float act = 0;
    for (int s = 0; s < spectral::kSlots; ++s)
    {
        const bool runs = e.on && fx->slotType(s) != 0 && fx->slotOn(s);
        const float lv = runs ? juce::jmin(1.0f, fx->slotLevel(s)) : 0.0f;
        fxSlotLv[s] += (lv - fxSlotLv[s]) * k;
        act = juce::jmax(act, runs ? 0.25f + 0.75f * fxSlotLv[s] : 0.0f);
    }
    e.spectral += (act - e.spectral) * k;
    e.wet = fx->mix() / 100.0f;
    e.order = (float)fx->order();
    const auto &wp = spectral::warpParams();
    for (int i = 0; i < (int)wp.size(); ++i)
    {
        const float n = wp[(size_t)i].toNorm(fx->warpValue(i));
        if (wp[(size_t)i].id == "warp")
            e.warp01 = n;
        else if (wp[(size_t)i].id == "f_high")
            e.fhigh = n;
        else if (wp[(size_t)i].id == "scale")
            e.scale01 = n;
        else if (wp[(size_t)i].id == "poles")
            e.poles = (int)std::lround(fx->warpValue(i));
    }
    const int pg = as->pageKeys.indexOf("fx");
    if (page != pg || pg < 0)
        return;
    // the glasses: what leaves the spectral slots, as high as each slot is busy (10 times a second)
    fxGlassAcc += dt;
    if (fxGlassAcc >= 0.1)
    {
        fxGlassAcc = 0;
        double rate = 48000;
        const bool fresh = e.on && fx->spectrum(fxMags, rate, false) && fxMags.size() > 8;
        bool moved = false;
        for (int b = 0; b < kFxBars; ++b)
        {
            float tgt = 0;
            if (fresh)
            { // log-spaced bands from 60 Hz to 16 kHz
                const double f0 = 60.0 * std::pow(16000.0 / 60.0, (double)b / kFxBars), f1 = 60.0 * std::pow(16000.0 / 60.0, (double)(b + 1) / kFxBars);
                const double hz = rate * 0.5 / (double)(fxMags.size() - 1);
                const int i0 = juce::jlimit(1, (int)fxMags.size() - 1, (int)(f0 / hz)), i1 = juce::jlimit(i0 + 1, (int)fxMags.size(), (int)(f1 / hz) + 1);
                float m = 0;
                for (int i = i0; i < i1; ++i)
                    m = juce::jmax(m, fxMags[(size_t)i]);
                tgt = juce::jlimit(0.0f, 1.0f, (juce::Decibels::gainToDecibels(m, -90.0f) + 72.0f) / 66.0f);
            }
            else if (!e.on)
                tgt = 0;
            else
                tgt = fxBars[b] * 0.8f;
            const float nv = fxBars[b] + (tgt - fxBars[b]) * 0.5f;
            moved = moved || std::abs(nv - fxBarsDrawn[b]) > 0.03f;
            fxBars[b] = nv;
        }
        for (int s = 0; s < spectral::kSlots; ++s)
            moved = moved || std::abs(fxSlotLv[s] - fxSlotLvDrawn[s]) > 0.04f;
        if (moved)
        {
            for (int b = 0; b < kFxBars; ++b)
                fxBarsDrawn[b] = fxBars[b];
            for (int s = 0; s < spectral::kSlots; ++s)
                fxSlotLvDrawn[s] = fxSlotLv[s];
            sceneDirty.mark(fxGlassCells);
        }
        // the signal's beads in the main and in the hose (they run while the rack is on)
        const float al = e.on ? 0.25f + 0.75f * juce::jmin(1.0f, e.in * 1.5f) : 0.0f;
        if (al > 0.02f && !env.calm)
            fxDash = std::fmod(fxDash + 0.1f * (30.0f + 50.0f * e.in), 24.0f);
        if (std::abs(al - fxDashAlpha) > 0.05f || (al > 0.02f && std::abs(fxDash - fxDashDrawn) > 0.5f) || e.order != fxOrderDrawn)
        {
            fxDashAlpha = al;
            fxDashDrawn = fxDash;
            fxOrderDrawn = e.order;
            sceneDirty.mark(fxHoseCells);
        }
    }
}

// the gauges of the page (tickLights asks)
float MachineEditor::Impl::fxGaugeAngle(const Ctl &k) const
{
    if (k.fx == FX_In)
        return -110.0f + 220.0f * juce::jlimit(0.f, 1.f, env.fx.in * 1.2f);
    if (k.fx == FX_Out)
        return -110.0f + 220.0f * juce::jlimit(0.f, 1.f, env.fx.out * 1.2f);
    if (k.fx == FX_WGauge && ed.spectral)
    { // the boiler gauge reads WarpCore's PITCH
        const auto &wp = spectral::warpParams();
        for (int i = 0; i < (int)wp.size(); ++i)
            if (wp[(size_t)i].id == "pitch")
                return -110.0f + 220.0f * wp[(size_t)i].toNorm(ed.spectral->warpValue(i));
    }
    return k.angle;
}

// ============================================================================================== drawing
// the hose between the two craft (behind the bridge and the bench): the signal's road; its beads run the way the order says
void MachineEditor::Impl::fxDrawHose(const Ctx &c)
{
    FPoint g[4];
    fxHoseGeom(g);
    float x0 = g[0].x, x1 = x0, y0 = g[0].y, y1 = y0;
    for (int i = 1; i < 4; ++i)
    {
        x0 = juce::jmin(x0, g[i].x);
        x1 = juce::jmax(x1, g[i].x);
        y0 = juce::jmin(y0, g[i].y);
        y1 = juce::jmax(y1, g[i].y);
    }
    const FRect bb = FRect(x0, y0, x1 - x0, y1 - y0).expanded(14.0f).getSmallestIntegerContainer().toFloat();
    if (!c.touches(bb))
        return;
    auto it = as->made.find("fxhose");
    if (it == as->made.end())
    {
        const juce::Colour cloth(0xff6f5a44);
        auto spr = as->render(bb.getWidth(), bb.getHeight(), [&](juce::Graphics &G) {
            G.addTransform(juce::AffineTransform::translation(-bb.getX(), -bb.getY()));
            const auto path = bezierPath(g);
            const auto round = juce::PathStrokeType::rounded;
            G.setColour(juce::Colour(0xff04050a).withAlpha(0.4f));
            G.strokePath(path, juce::PathStrokeType(13.0f, juce::PathStrokeType::curved, round), juce::AffineTransform::translation(1.6f, 3.8f));
            G.setColour(juce::Colour(0xff17110e));
            G.strokePath(path, juce::PathStrokeType(11.4f, juce::PathStrokeType::curved, round));
            G.setColour(shade(cloth, -0.2f));
            G.strokePath(path, juce::PathStrokeType(9.0f, juce::PathStrokeType::curved, round));
            juce::Path weave;
            const float dash[] = {3.0f, 3.4f};
            juce::PathStrokeType(7.2f, juce::PathStrokeType::curved, juce::PathStrokeType::butt).createDashedStroke(weave, path, dash, 2);
            G.setColour(shade(cloth, 0.35f).withAlpha(0.45f));
            G.fillPath(weave);
            G.setColour(shade(cloth, 0.7f).withAlpha(0.6f));
            G.strokePath(path, juce::PathStrokeType(1.2f, juce::PathStrokeType::curved, round), juce::AffineTransform::translation(-1.2f, -2.0f));
        });
        it = as->made.emplace("fxhose", std::move(spr)).first;
    }
    c.sprite(it->second, bb.getX(), bb.getY(), 1.0f, Blend::Over, c.dim);
    if (fxDashAlpha <= 0.02f)
        return;
    const auto &bead = pulseSprite(*as, kAmber);
    const float half = 0.5f * (float)bead.w / (float)c.S;
    constexpr int N = 32;
    FPoint pts[N + 1];
    float len[N + 1];
    len[0] = 0;
    pts[0] = g[0];
    for (int i = 1; i <= N; ++i)
    {
        pts[i] = bezAt(g, (float)i / (float)N);
        len[i] = len[i - 1] + pts[i].getDistanceFrom(pts[i - 1]);
    }
    const float off = env.fx.order > 0.5f ? 24.0f - fxDashDrawn : fxDashDrawn; // TOWN > GHOST: they run back
    int seg = 1;
    for (float d0 = off - 24.0f; d0 < len[N]; d0 += 24.0f)
        for (float u = 0; u <= 9.0f; u += 3.0f)
        {
            const float sAt = d0 + u;
            if (sAt < 4.0f || sAt > len[N] - 4.0f)
                continue;
            while (seg < N && len[seg] < sAt)
                ++seg;
            while (seg > 1 && len[seg - 1] > sAt)
                --seg;
            const float f = (sAt - len[seg - 1]) / juce::jmax(1e-4f, len[seg] - len[seg - 1]);
            const auto q = pts[seg - 1] + (pts[seg] - pts[seg - 1]) * f;
            c.sprite(bead, q.x - half, q.y - half, 0.55f * fxDashAlpha, Blend::Screen);
        }
}

// the slot glasses and the beads in the signal main (over the machine, under the text)
void MachineEditor::Impl::fxDraw(const Ctx &c)
{
    if (c.g != nullptr)
        for (auto &k : ctls)
        {
            if (k.fx != FX_Glass || !c.touches(k.rect.expanded(3)))
                continue;
            auto &G = *c.g;
            const auto r = k.rect.reduced(6.0f, 7.0f);
            const bool sel = benchSlot == k.slot;
            const float lv = fxSlotLvDrawn[k.slot];
            const bool runs = ed.spectral && env.fx.on && ed.spectral->slotType(k.slot) != 0 && ed.spectral->slotOn(k.slot);
            if (runs)
            {
                juce::Path p;
                for (int b = 0; b < kFxBars; ++b)
                {
                    const float x = r.getX() + r.getWidth() * (float)b / (float)(kFxBars - 1);
                    const float y = r.getBottom() - r.getHeight() * juce::jlimit(0.04f, 1.0f, fxBarsDrawn[b] * (0.35f + 0.65f * lv));
                    if (b == 0)
                        p.startNewSubPath(x, y);
                    else
                        p.lineTo(x, y);
                }
                G.setColour(kSlotCol[k.slot].withAlpha(0.28f));
                G.strokePath(p, juce::PathStrokeType(3.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                G.setColour(kSlotCol[k.slot].withAlpha(0.95f));
                G.strokePath(p, juce::PathStrokeType(1.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            else
            {
                G.setColour(kSlotCol[k.slot].withAlpha(0.22f));
                G.drawLine(r.getX(), r.getBottom() - 1.0f, r.getRight(), r.getBottom() - 1.0f, 1.0f);
            }
            if (sel || k.hover)
            { // the glass on the bench wears a lit rim
                G.setColour(kAmber.withAlpha(sel ? 0.85f : 0.4f));
                G.drawRoundedRectangle(k.rect.reduced(2.5f), 7.0f, 1.4f);
            }
        }
    if (fxDashAlpha > 0.02f)
        if (auto *sm = as->layout["pages"]["fx"]["signal"]["main"].getArray())
        { // beads down the signal main, from the organ rank to the hose
            const auto &bead = pulseSprite(*as, kAmber);
            const float half = 0.5f * (float)bead.w / (float)c.S;
            float along = 0;
            for (int i = 0; i + 1 < sm->size(); ++i)
            {
                const auto a = pointOf((*sm)[i]), b = pointOf((*sm)[i + 1]);
                const float L = juce::jmax(1.0f, a.getDistanceFrom(b));
                for (float u = 0; u < L; u += 3.0f)
                    if (std::fmod(along + u - fxDashDrawn + 2400.0f, 24.0f) < 9.0f)
                    {
                        const auto q = a + (b - a) * (u / L);
                        c.sprite(bead, q.x - half, q.y - half, 0.5f * fxDashAlpha, Blend::Screen);
                    }
                along += L;
            }
        }
}
} // namespace ghostblock::p2::machine
