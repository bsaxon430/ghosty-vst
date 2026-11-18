/*
 * Poltergeist 2 (GhostBlock) - the ghost-machine editor: the pointer and the keys.
 *
 * What a press does is the mock-up's (src/rt_ui.js, onDown): wheels turn (170 px up / down = full travel, sideways for
 * fine work), dials step, switches throw, plungers dip, hoses are pulled from OUT sockets and dropped on IN sockets.
 * Kept from the first editor: SHIFT while dragging RATIO = any ratio, right-click = type a value (dials: pick from a
 * list), double-click = the default, the mouse wheel, and the same edit messages to the engine.
 */
#include "ghostblock/machine/machine-impl.h"
#include "synth/matrix_index.h"

namespace ghostblock::p2::machine
{
using MD = sst::basic_blocks::params::ParamMetaData;

static bool steppedParam(const Param &p) { return p.meta.type == MD::INT || p.meta.type == MD::BOOL; }
static juce::String toStr(const std::optional<std::string> &s) { return s ? juce::String(juce::CharPointer_UTF8(s->c_str())) : juce::String(); }

static FPoint chamberPoint(const Assets &as, int i, const char *what) { return pointOf(as.layout["pages"]["ghosts"]["chambers"][i][what]); }
static FPoint bezAt(const FPoint g[4], float t)
{
    const float m = 1 - t;
    return g[0] * (m * m * m) + g[1] * (3 * m * m * t) + g[2] * (3 * m * t * t) + g[3] * (t * t * t);
}

// ============================================================================================== values
float MachineEditor::Impl::getCtl01(Ctl &c)
{
    if (c.fx != 0)
    {
        FxV v;
        return fxValue(c, v) ? fxNorm(v, v.val) : 0.f;
    }
    if (auto *p = paramOf(c))
    {
        const float lo = p->meta.minVal, hi = p->meta.maxVal;
        return hi > lo ? juce::jlimit(0.f, 1.f, (p->value - lo) / (hi - lo)) : 0.f;
    }
    if (!ed.morph)
        return 0;
    if (c.role == R_OSize)
        return ed.morph->orbitSize.load();
    if (c.role == R_CornerVol)
        return juce::jlimit(0.f, 1.f, (ed.morph->volDb[(size_t)c.corner].load() + 24.0f) / 30.0f);
    if (c.role == R_CornerPan)
        return juce::jlimit(0.f, 1.f, (ed.morph->pan[(size_t)c.corner].load() + 1.0f) * 0.5f);
    return c.v01;
}

float MachineEditor::Impl::defaultCtl01(Ctl &c)
{
    if (c.fx != 0)
    {
        FxV v;
        return fxValue(c, v) ? fxNorm(v, v.def) : 0.f;
    }
    if (auto *p = paramOf(c))
    {
        const float lo = p->meta.minVal, hi = p->meta.maxVal;
        return hi > lo ? juce::jlimit(0.f, 1.f, (p->meta.defaultVal - lo) / (hi - lo)) : 0.f;
    }
    return c.role == R_OSize ? 0.6f : c.role == R_CornerVol ? 0.8f : 0.5f;
}

// phase: 0 the gesture begins, 1 a value, 2 it ends
void MachineEditor::Impl::setCtl01(Ctl &c, float v, int phase, bool free)
{
    v = juce::jlimit(0.f, 1.f, v);
    if (c.fx != 0)
    {
        FxV q;
        if (fxValue(c, q))
            fxSet(c, phase == 1 ? fxReal(q, v) : q.val, phase);
        return;
    }
    if (auto *p = paramOf(c))
    {
        if (phase == 0)
        {
            // touching OUT / FEEDBACK wakes what is needed; a pitch DROP falls back (as the first editor)
            if (c.role == R_GOut)
                wake(sel, true, false);
            else if (c.role == R_ChMix)
                wake(c.op, true, false);
            else if (c.role == R_GFb)
                wake(sel, false, true);
            else if (c.id == "pd-amount")
            {
                auto &f2 = P.fineTuneMod;
                if (f2.sustain.value > 0.99f && std::abs(f2.envCoarseDepth.value) < 1e-4f)
                {
                    ed.setOnce(f2.sustain, 0.f);
                    if (f2.decay.value < 0.05f)
                        ed.setOnce(f2.decay, 0.3f);
                    if (f2.envPower.value < 0.5f)
                        ed.setOnce(f2.envPower, 1.f);
                }
            }
            ed.beginEdit(*p);
        }
        else if (phase == 1)
        {
            float x = p->meta.minVal + v * (p->meta.maxVal - p->meta.minVal);
            if (steppedParam(*p))
                x = std::round(x);
            else if (c.role == R_GRatio && !free)
            { // RATIO snaps to musical ratios (hold SHIFT for any ratio); the engine stores it as a power of two
                static const double R[] = {1.0 / 32, 1.0 / 16, 1.0 / 8, 0.25, 0.5, 0.75, 1, 1.4142, 1.5, 2, 2.5, 3, 3.5, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 20, 24, 32};
                double best = 0, bd = 1e9;
                for (double r : R)
                {
                    const double l = std::log2(r);
                    if (std::abs(l - x) < bd)
                    {
                        bd = std::abs(l - x);
                        best = l;
                    }
                }
                x = (float)best;
            }
            if (x != p->value)
                ed.setValue(*p, x);
        }
        else
            ed.endEdit(*p);
        return;
    }
    if (!ed.morph)
        return;
    if (c.role == R_OSize)
    {
        ed.morphEdit(Morph::pSize, v, phase);
        if (phase == 2)
            ed.markStateDirty();
    }
    else if (c.role == R_CornerVol || c.role == R_CornerPan)
    {
        if (phase == 1)
        {
            if (c.role == R_CornerVol)
                ed.morph->volDb[(size_t)c.corner].store(-24.0f + v * 30.0f);
            else
                ed.morph->pan[(size_t)c.corner].store(v * 2.0f - 1.0f);
        }
        else if (phase == 2)
            ed.markStateDirty();
    }
    if (phase == 1)
        env.poke(gOrbit, 0.5f);
}

int MachineEditor::Impl::selectorCount(Ctl &c)
{
    if (c.fx != 0)
    {
        FxV v;
        return fxValue(c, v) ? (int)std::lround(v.hi - v.lo) + 1 : 1;
    }
    if (auto *p = paramOf(c))
        return (int)std::lround(p->meta.maxVal - p->meta.minVal) + 1;
    if (c.role == R_CornerOct)
        return Morph::kOctMax - Morph::kOctMin + 1;
    return c.role == R_Theme ? 3 : c.role == R_Orbit ? (int)Morph::O_COUNT : c.role == R_Rate ? Morph::kRates : 1;
}
int MachineEditor::Impl::selectorIndex(Ctl &c)
{
    if (c.fx != 0)
    {
        FxV v;
        return fxValue(c, v) ? (int)std::lround(v.val - v.lo) : 0;
    }
    if (auto *p = paramOf(c))
        return (int)std::lround(p->value - p->meta.minVal);
    if (c.role == R_Theme)
        return env.theme;
    if (!ed.morph)
        return 0;
    if (c.role == R_CornerOct)
        return ed.morph->octave[(size_t)c.corner].load() - Morph::kOctMin;
    return c.role == R_Orbit ? ed.morph->orbit.load() : c.role == R_Rate ? ed.morph->orbitRate.load() : 0;
}
juce::String MachineEditor::Impl::selectorText(Ctl &c, int i)
{
    if (c.fx != 0)
    {
        FxV v;
        return fxValue(c, v) ? fxText(v, v.lo + (float)i) : juce::String();
    }
    if (auto *p = paramOf(c))
        return valueText(*p, p->meta.minVal + (float)i);
    static const char *sky[3] = {"STORM", "DUSK", "CLEAR"};
    if (c.role == R_Theme)
        return sky[juce::jlimit(0, 2, i)];
    if (c.role == R_Orbit)
        return Morph::orbitNames[juce::jlimit(0, (int)Morph::O_COUNT - 1, i)];
    if (c.role == R_Rate)
        return juce::String("EVERY ") + Morph::rateNames[juce::jlimit(0, Morph::kRates - 1, i)];
    if (c.role == R_CornerOct)
    {
        const int o = Morph::kOctMin + i;
        return o == 0 ? juce::String("0  (as played)") : (o > 0 ? "+" : "") + juce::String(o) + (std::abs(o) == 1 ? " octave" : " octaves");
    }
    return {};
}
void MachineEditor::Impl::stepSelector(Ctl &c, int to)
{
    const int n = selectorCount(c);
    if (n <= 0)
        return;
    to = ((to % n) + n) % n;
    if (c.fx != 0)
    {
        FxV v;
        if (fxValue(c, v))
        {
            fxSet(c, v.lo + (float)to, 0);
            fxSet(c, v.lo + (float)to, 1);
            fxSet(c, v.lo + (float)to, 2);
            if (c.fx == FX_Fft || c.fx == FX_Window || c.fx == FX_Overlap)
                ed.status(c.name + ": " + fxText(v, v.lo + (float)to) + ".  " + fxLatencyText());
        }
    }
    else if (auto *p = paramOf(c))
        ed.setOnce(*p, p->meta.minVal + (float)to);
    else if (c.role == R_Theme)
    {
        static const char *look[3] = {"BLACK", "PURPLE", "LIGHT"};
        setTheme(to, true);
        ed.status("SKY: " + selectorText(c, to) + "  (the plugin's " + look[to] + " look)");
    }
    else if (c.role == R_Orbit && ed.morph)
    {
        ed.morphSet(Morph::pOrbit, to);
        ed.markStateDirty();
        ed.status(to > 0 ? "ORBIT: the clockwork arm carries the puck round where you put it while MORPH is on. SIZE = how far it goes." : "ORBIT off: the puck stays where you put it.");
        if (to > 0 && !ed.morph->on.load())
            ed.morphSet(Morph::pOn, 1.0);
    }
    else if (c.role == R_Rate && ed.morph)
    {
        ed.morphSet(Morph::pRate, to);
        ed.markStateDirty();
    }
    else if (c.role == R_CornerOct)
        setCornerOct(c.corner, Morph::kOctMin + to);
    ed.uiDirty = true;
    refresh();
}

// MORPH: a corner's OCTAVE (the keys its sound hears; the sound itself is not changed)
void MachineEditor::Impl::setCornerOct(int corner, int oct)
{
    if (!ed.morph || corner < 0 || corner >= Morph::N)
        return;
    static const char *L[Morph::N] = {"A", "B", "C", "D"};
    oct = juce::jlimit((int)Morph::kOctMin, (int)Morph::kOctMax, oct);
    ed.morph->octave[(size_t)corner].store(oct);
    ed.morph->changed++;
    ed.markStateDirty();
    const bool thisSound = ed.morph->kind[(size_t)corner].load() == Morph::THIS_SOUND;
    ed.status("CORNER " + juce::String(L[corner]) + " OCTAVE: " + (oct == 0 ? juce::String("as played") : (oct > 0 ? "+" : "") + juce::String(oct)) +
              (thisSound ? "  (THIS SOUND moves only while MORPH is on)" : "") + ".  Top half = up, bottom half = down.");
    env.poke(gOrbit, 0.4f);
    ed.uiDirty = true;
    if (auto *k = find(juce::String("o-") + L[corner] + "-oct"))
        touchCtl(*k, true);
}

bool MachineEditor::Impl::switchOn(Ctl &c)
{
    if (c.fx != 0)
    {
        FxV v;
        return fxValue(c, v) && v.val > 0.5f;
    }
    if (auto *p = paramOf(c))
        return p->value > 0.5f;
    return c.role == R_MorphOn && ed.morph && ed.morph->on.load();
}
void MachineEditor::Impl::setSwitch(Ctl &c, bool on)
{
    if (c.fx != 0)
    {
        fxSet(c, on ? 1.f : 0.f, 0);
        fxSet(c, on ? 1.f : 0.f, 1);
        fxSet(c, on ? 1.f : 0.f, 2);
        if (c.fx == FX_On)
            ed.status(on ? "SPECTRAL FX is on.  " + fxLatencyText() : juce::String("SPECTRAL FX is off: the plugin's output is not touched."));
        else if (c.fx == FX_WarpOn)
            ed.status(on ? "WARPCORE runs (it needs SPECTRAL FX on to be heard)." : "WARPCORE is off.");
        else
            ed.status(c.name + (on ? ": on" : ": off"));
        ed.uiDirty = true;
        refresh();
        return;
    }
    if (auto *p = paramOf(c))
    {
        ed.setOnce(*p, on ? 1.f : 0.f);
        if (c.role == R_GAwake)
            ed.status("GHOST " + juce::String(sel + 1) + (on ? " is awake." : " is asleep."));
        else if (c.role == R_LLinked)
            ed.status(on ? "Linked." : "Link broken.");
        else if (c.id == "v-mono")
            ed.status(on ? "MONO: one note at a time." : "POLY.");
    }
    else if (c.role == R_MorphOn && ed.morph)
    {
        ed.morphSet(Morph::pOn, on ? 1.0 : 0.0);
        ed.markStateDirty();
        ed.status(on ? "MORPH is on: drag the puck to blend the corners (double-click a corner = exactly that sound)." : "MORPH is off: you hear THIS SOUND only.");
    }
    ed.uiDirty = true;
    refresh();
}

// ============================================================================================== feedback
void MachineEditor::Impl::spyglassChanged()
{
    if (auto *c = find("spyglass"))
    {
        c->on = ed.spyglassOpen();
        touch(*c);
    }
}

void MachineEditor::Impl::touchCtl(Ctl &c, bool strong)
{
    c.flashAt = clock;
    touch(c);
    env.bump(strong ? 0.3f : 0.12f);
    env.poke(c.page == 0 ? gFlank : c.page == 1 ? gYard : c.page == 2 ? gOrbit : gJaw, 0.5f);
}
void MachineEditor::Impl::vent(const Ctl &c)
{
    if (stageBuilt[page] && !env.calm)
        stage[page].puff(c.rect.getRight() - 2, c.rect.getY() + 2, pkSteam, 0.55f, 0.6f, -1, env, rng);
}

// ============================================================================================== finding what is under the pointer
int MachineEditor::Impl::ctlAt(FPoint p) const
{
    int best = -1;
    float bestArea = 1e12f;
    const bool inSub = subOpen && subRect.contains(p);
    for (int i = 0; i < (int)ctls.size(); ++i)
    {
        const auto &c = ctls[(size_t)i];
        if (!c.interactive || !visible(c) || c.type == CType::Pin || c.type == CType::Keys)
            continue;
        if (inSub != (c.page == kSubPage)) // the open SUB panel takes the pointer over its whole board
            continue;
        FRect h = c.hit;
        if (c.type == CType::Lens)
            h = rectOf(as->layout["pages"]["morph"]["regions"]["pad"]).expanded(12); // the puck's whole travel takes the pointer, under everything else
        if (!h.contains(p))
            continue;
        const float a = c.type == CType::Lens ? 1e11f : h.getWidth() * h.getHeight();
        if (a < bestArea)
        {
            bestArea = a;
            best = i;
        }
    }
    return best;
}

int MachineEditor::Impl::keyAt(FPoint p) const
{
    const auto *keys = as ? as->layout["keys"].getArray() : nullptr;
    if (keys == nullptr)
        return -1;
    for (int pass = 0; pass < 2; ++pass) // the black keys lie on the white ones
        for (auto &k : *keys)
            if ((bool)k["black"] == (pass == 0) && rectOf(k["rect"]).contains(p))
                return (int)k["note"];
    return -1;
}

void MachineEditor::Impl::noteOn(int note, float vel)
{
    note = juce::jlimit(0, 127, note);
    if (keysDown[note]++ == 0)
    {
        ed.noteQueue.push(UiNote{true, (int16_t)note, vel});
        if (auto *k = find("kbd"))
            touch(*k);
    }
    env.noteOn();
}
void MachineEditor::Impl::noteOff(int note)
{
    note = juce::jlimit(0, 127, note);
    if (keysDown[note] > 0 && --keysDown[note] == 0)
    {
        ed.noteQueue.push(UiNote{false, (int16_t)note, 0.f});
        if (auto *k = find("kbd"))
            touch(*k);
    }
}
void MachineEditor::Impl::allNotesOff()
{
    for (int n = 0; n < 128; ++n)
        if (keysDown[n] > 0)
        {
            keysDown[n] = 0;
            ed.noteQueue.push(UiNote{false, (int16_t)n, 0.f});
        }
    typed.fill(false);
    if (auto *k = find("kbd"))
        touch(*k);
}

// ============================================================================================== hover
void MachineEditor::Impl::hoverAt(FPoint p, bool inside)
{
    int ci = -1, hv = -1, pin = -1;
    if (backOpen)
    {
        if (!drag.active)
            backHoverAt(inside ? p : FPoint(-1000, -1000));
        return;
    }
    if (studioOpen && ledger.mode == Ledger::Closed)
    {
        if (!drag.active)
            studioHoverAt(inside ? p : FPoint(-1000, -1000));
        return;
    }
    if (inside && as && !drag.active)
    {
        if (ledger.mode != Ledger::Closed && ledgerArea().contains(p))
        {
            ledgerHover(p);
            p = {-1000, -1000};
        }
        else if (ledger.mode != Ledger::Closed)
            ledgerHover({-1000, -1000});
        const bool inSub = subOpen && subRect.contains(p);
        if (page == 0 && !inSub)
            for (int i = 0; i < (int)hoses.size(); ++i)
                if (!hoses[(size_t)i].dragging && p.getDistanceFrom(hoses[(size_t)i].vq) < 15)
                    hv = i;
        static const char *ids[3] = {"g-env", "l-env", "m-env"};
        for (int w = 0; w < 3 && hv < 0 && !inSub; ++w)
            if (auto *e = find(ids[w]); e != nullptr && visible(*e))
                for (int k = 0; k < 5; ++k)
                    if (p.getDistanceFrom(envGeom[w].pin[k]) < 12)
                        pin = w * 8 + k;
        if (hv < 0 && pin < 0)
            ci = ctlAt(p);
    }
    if (drag.active)
        return;
    setDoorHover(inside && ci < 0 && hv < 0 && pin < 0 && ledger.mode == Ledger::Closed ? doorAt(p) : -1);
    if (const bool sb = inside && ci < 0 && ledger.mode == Ledger::Closed && studioButtonAt(p); sb != stButtonHover)
    {
        stButtonHover = sb;
        sceneDirty.mark(studioDoor().expanded(16));
        if (sb)
            ed.status("STUDIO VIEW: a plain, ordinary synth screen for the whole plugin (MACHINE VIEW brings you back here).");
    }
    if (ci != hoverCtl)
    {
        if (hoverCtl >= 0 && hoverCtl < (int)ctls.size())
        {
            ctls[(size_t)hoverCtl].hover = false;
            touch(ctls[(size_t)hoverCtl]);
        }
        hoverCtl = ci;
        if (ci >= 0)
        {
            ctls[(size_t)ci].hover = true;
            touch(ctls[(size_t)ci]);
            if (ctls[(size_t)ci].role == R_Spyglass)
                ed.status("SPYGLASS: see the sound (spectrum + scope)");
            else if (ctls[(size_t)ci].role == R_Sub)
                ed.status("SUB: a sub-bass under the lowest note (click: its panel; right-click: on / off)");
            else if (ctls[(size_t)ci].role == R_Grip)
                ed.status("SIZE: drag the corner to make the window bigger or smaller (right-click: 75 to 200 %)");
        }
    }
    if (hv != hoverHoseValve)
    {
        for (int i : {hoverHoseValve, hv})
            if (i >= 0 && i < (int)hoses.size())
                sceneDirty.mark(hoses[(size_t)i].cells);
        hoverHoseValve = hv;
    }
    if (pin != hoverPin)
    {
        for (int q : {hoverPin, pin})
            if (q >= 0)
                sceneDirty.mark(envCells[q / 8]);
        hoverPin = pin;
    }
}

// ============================================================================================== a wheel in the hand
void MachineEditor::Impl::wheelDrag(Ctl &c, const juce::MouseEvent &e, FPoint at, bool dbl)
{
    const juce::String id = c.id;
    env.holdTarget = 1;
    touchCtl(c, false);
    c.held = true;
    setCtl01(c, 0, 0, false); // the gesture begins (and wakes what must be awake)
    if (dbl)
        setCtl01(c, defaultCtl01(c), 1, true);
    const float v0 = getCtl01(c);
    const FPoint p0 = at;
    auto moves = std::make_shared<int>(0);
    ed.uiDirty = true;
    juce::ignoreUnused(e);
    drag.move = [this, id, v0, p0, moves](FPoint p, const juce::MouseEvent &ev) {
        auto *k = find(id);
        if (k == nullptr)
            return;
        const float fine = (ev.mods.isCommandDown() || ev.mods.isCtrlDown() || (ev.mods.isShiftDown() && k->role != R_GRatio)) ? 0.2f : 1.0f;
        setCtl01(*k, v0 + (-(p.y - p0.y) / 170.0f + (p.x - p0.x) / 400.0f) * fine, 1, ev.mods.isShiftDown());
        env.bump(0.08f);
        if (k->role == R_Macro)
            ed.status(k->t1 + ": " + macroHint[juce::jlimit(0, (int)ss::numMacros - 1, k->index)]);
        else if (auto *p2 = paramOf(*k); p2 != nullptr && !k->two)
            ed.status(juce::String(juce::CharPointer_UTF8(p2->meta.name.c_str())) + ": " + toStr(p2->meta.valueToString(p2->value)));
        if (k->id == "dr-rate")
            env.poke(gWindmill, 0.6f);
        else if (k->id == "tone-drive")
            env.poke(gYard, 1.0f);
        else if (k->id == "pd-amount")
            env.pdEnv = 1;
        if (((*moves)++ % 9) == 1)
            vent(*k);
        ed.uiDirty = true;
    };
    drag.up = [this, id](FPoint, const juce::MouseEvent &) {
        env.holdTarget = 0;
        if (auto *k = find(id))
        {
            k->held = false;
            setCtl01(*k, 0, 2, false);
            touch(*k);
        }
        ed.uiDirty = true;
    };
    drag.active = true;
}

void MachineEditor::Impl::pressPlunger(Ctl &c)
{
    touchCtl(c, true);
    c.pressAt = clock;
    switch (c.role)
    {
    case R_Save:
        saveSound();
        break;
    case R_Load:
        openLedger(Ledger::Yours, 0);
        break;
    case R_Init:
        pm->loadInit(P, ed.mainToAudio);
        ed.status("INIT: one ghost awake, nothing plugged in. Drag a hose from an OUT socket to a ghost below it.");
        break;
    case R_GTab:
    {
        static const char *tip[3] = {"HEARD: the envelope of how loud you hear this ghost", "FEEDBACK: the envelope of this ghost's feedback ring",
                                     "PITCH: the envelope that bends this ghost's pitch (ENV > PITCH sets how far)"};
        envTab = juce::jlimit(0, 2, c.index);
        ed.status(tip[envTab]);
        break;
    }
    case R_LBreak:
        if (selLink >= 0)
        {
            const int src = (int)ss::MatrixIndex::sourceIndexAt((size_t)selLink);
            ed.setOnce(P.matrixNodes[(size_t)selLink].active, 0.f);
            ed.status("Link broken.");
            selectGhost(src, true);
        }
        break;
    default:
        break;
    }
    ed.uiDirty = true;
    refresh();
}

void MachineEditor::Impl::pullLever(Ctl &c)
{
    touchCtl(c, true);
    c.pressAt = clock;
    if (c.role == R_Calm)
    {
        setCalm(!env.calm, true);
        ed.status(env.calm ? "CALM: the machine keeps still. No lightning, no rain, slow clouds; the lights and the controls work as before."
                           : "CALM off: the weather follows the sound again.");
    }
    else
    {
        stepPreset(c.role == R_Prev ? -1 : 1);
        flash("pick-name");
    }
    ed.uiDirty = true;
}

// ============================================================================================== down
void MachineEditor::Impl::down(const juce::MouseEvent &e)
{
    lastTouchMs = juce::Time::getMillisecondCounterHiRes();
    if (!as)
        return;
    const FPoint xy = ed.toDesign(e.position);
    drag = Drag();
    // ---- the back room takes the whole window
    if (backDown(e, xy))
        return;
    // ---- the studio view takes the whole window (its sound list is the ledger, below)
    if (studioDown(e, xy))
        return;
    if (!e.mods.isPopupMenu() && ledger.mode == Ledger::Closed && studioButtonAt(xy))
    {
        studioToggle(true);
        return;
    }
    // ---- the ghost's eyes and mouth: the doors to the back room (a control painted over them still comes first)
    if (!e.mods.isPopupMenu() && ledger.mode == Ledger::Closed && doorAt(xy) >= 0 && ctlAt(xy) < 0 && keyAt(xy) < 0)
    {
        backToggle(true);
        return;
    }
    // ---- the ledger (a press outside it closes it, and still does what it would have done)
    if (ledger.mode != Ledger::Closed && ledgerDown(xy))
        return;
    if (studioOpen)
        return; // nothing of the machine answers under the studio
    // ---- right-click: type a value (a dial: pick from its list)
    if (e.mods.isPopupMenu())
    {
        const int ci = ctlAt(xy);
        if (ci >= 0)
        {
            auto &c = ctls[(size_t)ci];
            if (c.type == CType::Wheel)
                typeIn(c);
            else if (c.type == CType::Selector || c.type == CType::Drum || c.role == R_CornerOct)
                popupFor(c);
            else if (c.type == CType::Grip)
                ed.sizeMenu();
            else if (c.role == R_Sub && ed.sub)
            { // the sign's short cut: SUB on / off
                const double to = ed.sub->on() ? 0.0 : 1.0;
                for (int ph = 0; ph < 3; ++ph)
                    ed.subEdit(sub::SubBass::pOn, to, ph);
                c.pressAt = clock;
                touchCtl(c, true);
                ed.status(to > 0.5 ? "SUB is on: a sub-bass under the lowest note you play." : "SUB is off.");
            }
        }
        return;
    }
    const bool inSub = subOpen && subRect.contains(xy); // on the SUB panel only its own parts answer
    // ---- keys
    const int note = inSub ? -1 : keyAt(xy);
    if (note >= 0)
    {
        ed.grabKeyboardFocus();
        auto velAt = [this](FPoint p, int n) {
            float v = 0.85f;
            if (const auto *keys = as->layout["keys"].getArray())
                for (auto &k : *keys)
                    if ((int)k["note"] == n)
                    {
                        const auto r = rectOf(k["rect"]);
                        v = juce::jlimit(0.3f, 1.0f, 0.3f + 0.7f * (p.y - r.getY()) / juce::jmax(1.0f, r.getHeight()));
                    }
            return v;
        };
        heldKeyNote = note;
        noteOn(note, velAt(xy, note));
        drag.move = [this, velAt](FPoint p, const juce::MouseEvent &) {
            const int n = keyAt(p);
            if (n >= 0 && n != heldKeyNote)
            {
                noteOff(heldKeyNote);
                heldKeyNote = n;
                noteOn(n, velAt(p, n));
            }
        };
        drag.up = [this](FPoint, const juce::MouseEvent &) {
            noteOff(heldKeyNote);
            heldKeyNote = -1;
        };
        drag.active = true;
        return;
    }
    // ---- hose parts: the nearest OUT socket wins, then a valve, then a plug, then the hose itself
    if (page == 0 && !inSub)
    {
        int best = -1;
        float bd = 17.0f;
        for (int i = 0; i < (int)ss::numOps; ++i)
        {
            const float d = xy.getDistanceFrom(chamberPoint(*as, i, "out"));
            if (d < bd)
            {
                bd = d;
                best = i;
            }
        }
        const bool valveNearer = std::any_of(hoses.begin(), hoses.end(), [&](const Hose &h) { return !h.dragging && xy.getDistanceFrom(h.vq) < bd; });
        if (best >= 0 && !valveNearer)
        {
            startHose(best, xy, -1);
            flash("ch" + juce::String(best + 1) + "-out");
            drag.move = [this](FPoint p, const juce::MouseEvent &) { moveHose(p); };
            drag.up = [this](FPoint p, const juce::MouseEvent &) { endHose(p); };
            drag.active = true;
            return;
        }
        for (int hi = 0; hi < (int)hoses.size(); ++hi)
        {
            auto &h = hoses[(size_t)hi];
            if (h.dragging || xy.getDistanceFrom(h.vq) >= 17.0f)
                continue;
            // the valve on the hose: how hard (a loop's valve: the feedback, both ways from the middle)
            const bool fb = h.fb;
            const int src = h.src, dst = h.dst;
            auto *p = hoseParam(h);
            const float v0 = fb ? (p->value + 1.0f) * 0.5f : p->value, y0 = xy.y;
            auto moved = std::make_shared<bool>(false);
            ed.beginEdit(*p);
            env.holdTarget = 1;
            drag.move = [this, p, fb, src, dst, v0, y0, moved](FPoint q, const juce::MouseEvent &ev) {
                if (std::abs(q.y - y0) > 2)
                    *moved = true;
                const float fine = (ev.mods.isCommandDown() || ev.mods.isCtrlDown() || ev.mods.isShiftDown()) ? 0.2f : 1.0f;
                const float v = juce::jlimit(0.f, 1.f, v0 - (q.y - y0) / 160.0f * fine);
                ed.setValue(*p, fb ? v * 2.0f - 1.0f : v);
                for (auto &hh : hoses)
                    if (!hh.dragging && hh.fb == fb && hh.src == src && hh.dst == dst)
                        hh.pulse = 1;
                env.bump(0.1f);
                ed.status(juce::String(juce::CharPointer_UTF8(p->meta.name.c_str())) + ": " + toStr(p->meta.valueToString(p->value)));
                ed.uiDirty = true;
            };
            drag.up = [this, p, fb, src, dst, moved](FPoint, const juce::MouseEvent &) {
                ed.endEdit(*p);
                env.holdTarget = 0;
                if (fb)
                {
                    if (std::abs(p->value) < 0.005f)
                        ed.setOnce(P.selfNodes[(size_t)src].active, 0.f); // turned right down: the loop comes off
                    selectGhost(src, true);
                }
                else
                    selectLink(src, dst, *moved);
            };
            drag.active = true;
            return;
        }
        for (int hi = 0; hi < (int)hoses.size(); ++hi)
        {
            auto &h = hoses[(size_t)hi];
            if (h.dragging)
                continue;
            const float aB = std::atan2(h.cur[2].y - h.cur[3].y, h.cur[2].x - h.cur[3].x);
            const FPoint grab(h.cur[3].x + std::cos(aB) * 12.0f, h.cur[3].y + std::sin(aB) * 12.0f);
            if (std::abs(xy.x - grab.x) > 17.0f || std::abs(xy.y - grab.y) > 17.0f)
                continue;
            // pull the plug: the link (or the loop) comes out with the hose; drop it in the open to remove it
            const float keep = hoseDepth(h);
            const int src = h.src;
            if (h.fb)
                ed.setOnce(P.selfNodes[(size_t)src].active, 0.f);
            else
            {
                const int li = posOf(h.src, h.dst);
                ed.setOnce(P.matrixNodes[(size_t)li].active, 0.f);
                if (selLink == li)
                {
                    selLink = -1;
                    if (stageBuilt[0])
                        stage[0].setFace(1, sceneDirty);
                }
            }
            rebuildHoses();
            startHose(src, xy, keep);
            drag.move = [this](FPoint q, const juce::MouseEvent &) { moveHose(q); };
            drag.up = [this](FPoint q, const juce::MouseEvent &) { endHose(q); };
            drag.active = true;
            ed.uiDirty = true;
            return;
        }
        for (auto &h : hoses)
        {
            if (h.dragging)
                continue;
            bool on = false;
            for (int i = 1; i < 20 && !on; ++i)
                on = xy.getDistanceFrom(bezAt(h.cur, (float)i / 20.0f)) < 8.0f;
            if (!on)
                continue;
            if (h.fb)
                selectGhost(h.src, false);
            else
                selectLink(h.src, h.dst, false);
            return;
        }
    }
    // ---- envelope pins on the bent rail: drag x = time, the decay pin's y = sustain
    if (!inSub)
    {
        static const char *ids[3] = {"g-env", "l-env", "m-env"};
        for (int w = 0; w < 3; ++w)
        {
            auto *c = find(ids[w]);
            if (c == nullptr || !visible(*c))
                continue;
            int k = -1;
            float bd = 17.0f;
            for (int i = 0; i < 5; ++i)
            {
                const float d = xy.getDistanceFrom(envGeom[w].pin[i]);
                if (d < bd - 0.01f)
                {
                    bd = d;
                    k = i;
                }
            }
            if (k < 0)
                continue;
            static const int which[5] = {0, 1, 2, 3, 5}; // delay, attack, hold, decay (+ sustain), release
            Param *px = envParam(w, which[k]), *ps = k == 3 ? envParam(w, 4) : nullptr;
            const float x0 = px->value, s0 = ps ? ps->value : 0.f, span = c->rect.getWidth() - 20.0f, ht = envGeom[w].ht;
            const FPoint p0 = xy;
            ed.beginEdit(*px);
            if (ps)
                ed.beginEdit(*ps);
            env.holdTarget = 1;
            hoverPin = w * 8 + k;
            sceneDirty.mark(envCells[w]);
            drag.move = [this, px, ps, x0, s0, span, ht, p0, k, w](FPoint q, const juce::MouseEvent &) {
                static const char *nm[5] = {"DELAY ", "ATTACK ", "HOLD ", "DECAY ", "RELEASE "};
                ed.setValue(*px, juce::jlimit(0.f, 1.f, x0 + (q.x - p0.x) / span * 1.6f));
                if (ps)
                    ed.setValue(*ps, juce::jlimit(0.f, 1.f, s0 - (q.y - p0.y) / juce::jmax(1.0f, ht)));
                env.bump(0.1f);
                ed.status(juce::String(nm[k]) + toStr(px->meta.valueToString(px->value)) + (ps ? "   SUSTAIN " + toStr(ps->meta.valueToString(ps->value)) : juce::String()));
                sceneDirty.mark(envCells[w]);
                ed.uiDirty = true;
            };
            drag.up = [this, px, ps, w](FPoint, const juce::MouseEvent &) {
                ed.endEdit(*px);
                if (ps)
                    ed.endEdit(*ps);
                env.holdTarget = 0;
                hoverPin = -1;
                sceneDirty.mark(envCells[w]);
            };
            drag.active = true;
            return;
        }
    }
    // ---- everything else, by its rect
    const int ci = ctlAt(xy);
    if (ci < 0)
        return;
    pressCtl(ctls[(size_t)ci], e, xy);
}

// a press on one control (the machine's own; the STUDIO VIEW presses the same controls through here)
void MachineEditor::Impl::pressCtl(Ctl &c, const juce::MouseEvent &e, FPoint xy)
{
    const juce::String id = c.id;
    const double tNow = (double)e.eventTime.toMilliseconds(); // when the finger came down (a slow frame must not break a double tap)
    const bool dbl = lastTapId == id && tNow - lastTapAt < 480.0 && tNow >= lastTapAt;
    lastTapId = id;
    lastTapAt = tNow;
    if (c.fx >= FX_Sub && subClick(c)) // the SUB panel's plates
        return;
    if (c.fx != 0 && fxClick(c)) // the FX page's own parts (glasses, name plates, the page plate, the order valve)
        return;
    switch (c.type)
    {
    case CType::Wheel:
        wheelDrag(c, e, xy, dbl);
        break;
    case CType::Selector:
    case CType::Drum:
    {
        touchCtl(c, true);
        const int i0 = selectorIndex(c);
        const float y0 = xy.y;
        auto moved = std::make_shared<bool>(false);
        const bool steps = c.role != R_Folder && c.role != R_Theme && c.type == CType::Selector;
        drag.move = [this, id, i0, y0, moved, steps](FPoint p, const juce::MouseEvent &) {
            const int st = juce::roundToInt((y0 - p.y) / 14.0f);
            if (st == 0)
                return;
            *moved = true;
            auto *k = find(id);
            if (k == nullptr || !steps)
                return;
            const int to = juce::jlimit(0, selectorCount(*k) - 1, i0 + st);
            if (to != selectorIndex(*k))
                stepSelector(*k, to);
        };
        drag.up = [this, id, moved](FPoint, const juce::MouseEvent &ev) {
            auto *k = find(id);
            if (k == nullptr)
                return;
            if (!*moved)
            {
                const int d = ev.mods.isShiftDown() ? -1 : 1;
                if (k->role == R_Folder)
                    nextFolder(d);
                else
                    stepSelector(*k, selectorIndex(*k) + d);
            }
            if (k->role != R_Folder && k->role != R_Theme && k->role != R_Orbit)
                ed.status(k->name + ": " + selectorText(*k, selectorIndex(*k)));
            vent(*k);
            ed.uiDirty = true;
        };
        drag.active = true;
        break;
    }
    case CType::Switch:
        c.pressAt = clock;
        touchCtl(c, true);
        setSwitch(c, !switchOn(c));
        break;
    case CType::Plunger:
        pressPlunger(c);
        break;
    case CType::Lever:
        pullLever(c);
        break;
    case CType::Sign:
        c.pressAt = clock;
        touchCtl(c, true);
        if (c.role == R_Sub)
            subToggle(!subOpen);
        else
            setPage(c.index, false);
        break;
    case CType::Plate:
        touchCtl(c, true);
        if (c.role == R_PickName)
            openLedger(Ledger::Sounds, 0);
        else if (c.role == R_CornerSound)
            openLedger(Ledger::CornerPick, c.corner);
        else if (c.role == R_CornerOct && ed.morph)
        {
            if (ed.morph->kind[(size_t)c.corner].load() == Morph::EMPTY)
                ed.status("Pick a sound for this corner first; then its OCTAVE moves it up or down.");
            else // the top half goes up, the bottom half down
                setCornerOct(c.corner, ed.morph->octave[(size_t)c.corner].load() + (xy.y < c.rect.getCentreY() ? 1 : -1));
        }
        break;
    case CType::Lantern:
    {
        touchCtl(c, true);
        if (!ed.morph)
            break;
        static const char *L[Morph::N] = {"A", "B", "C", "D"};
        const int k = ed.morph->kind[(size_t)c.corner].load();
        if (dbl && k != Morph::EMPTY)
        { // double tap on a filled corner: the puck jumps onto it (exactly that sound)
            closeLedger();
            ed.morphSet(Morph::pX, (double)(c.corner % 2));
            ed.morphSet(Morph::pY, (double)(c.corner / 2));
            ed.markStateDirty();
            ed.status("The puck jumps onto corner " + juce::String(L[c.corner]) + ": exactly that sound.");
        }
        else if (k == Morph::EMPTY)
            openLedger(Ledger::CornerPick, c.corner);
        else
        {
            ed.status("CORNER " + juce::String(L[c.corner]) + ": " + juce::String(juce::CharPointer_UTF8(ed.morph->names[(size_t)c.corner].c_str())) +
                      ".  Double tap = the puck jumps onto it.  Its sound plate picks another sound.");
            flash(juce::String("o-") + L[c.corner] + "-sound");
        }
        ed.uiDirty = true;
        break;
    }
    case CType::Lens:
    {
        if (!ed.morph)
            break;
        env.holdTarget = 1;
        touchCtl(c, false);
        c.held = true;
        const auto pad = rectOf(as->layout["pages"]["morph"]["regions"]["pad"]);
        auto setp = [this, pad](FPoint p, int type) {
            ed.morphEdit(Morph::pX, juce::jlimit(0.f, 1.f, (p.x - pad.getX()) / pad.getWidth()), type);
            ed.morphEdit(Morph::pY, juce::jlimit(0.f, 1.f, (p.y - pad.getY()) / pad.getHeight()), type);
            env.bump(0.06f);
        };
        setp(xy, 0);
        setp(xy, 1);
        drag.move = [setp](FPoint p, const juce::MouseEvent &) { setp(p, 1); };
        drag.up = [this, setp, id](FPoint p, const juce::MouseEvent &) {
            setp(p, 2);
            ed.markStateDirty();
            env.holdTarget = 0;
            if (auto *k = find(id))
            {
                k->held = false;
                touch(*k);
            }
            float w[Morph::N]{};
            if (ed.morph->weights(w))
            {
                static const char *L[Morph::N] = {"A", "B", "C", "D"};
                juce::String s = "Blend:";
                for (int i = 0; i < Morph::N; ++i)
                    if (ed.morph->kind[(size_t)i].load() != Morph::EMPTY)
                        s << "  " << L[i] << " " << juce::roundToInt(w[i] * 100) << "%";
                ed.status(s);
            }
        };
        drag.active = true;
        break;
    }
    case CType::Chamber:
    {
        const int op = c.op;
        const FPoint p0 = xy;
        auto started = std::make_shared<bool>(false);
        drag.move = [this, op, p0, started](FPoint p, const juce::MouseEvent &) {
            if (!*started && p.getDistanceFrom(p0) > 7.0f)
            {
                *started = true;
                startHose(op, p, -1);
            }
            if (*started)
                moveHose(p);
        };
        drag.up = [this, op, id, started](FPoint p, const juce::MouseEvent &) {
            if (*started)
                endHose(p);
            else
            {
                selectGhost(op, false);
                if (auto *k = find(id))
                    touchCtl(*k, true);
            }
        };
        drag.active = true;
        break;
    }
    case CType::Grip:
    { // drag the corner: the window keeps its shape; the farther of the two directions wins; whole percents
        touchCtl(c, false);
        c.held = true;
        const auto start = e.getScreenPosition().toFloat();
        const float w0 = (float)ed.getWidth(), h0 = (float)ed.getHeight();
        drag.move = [this, start, w0, h0](FPoint, const juce::MouseEvent &ev) {
            const auto d = ev.getScreenPosition().toFloat() - start;
            double s = juce::jmax((w0 + d.x) / (float)CW, (h0 + d.y) / (float)CH);
            s = std::round(s * 100.0) / 100.0;
            ed.setUiScale(s, false);
            ed.status("SIZE: " + juce::String(juce::roundToInt(ed.viewScale() * 100.0)) + " %");
        };
        drag.up = [this, id](FPoint, const juce::MouseEvent &) {
            ed.setUiScale(ed.viewScale(), true);
            if (auto *k = find(id))
            {
                k->held = false;
                touch(*k);
            }
        };
        drag.active = true;
        break;
    }
    case CType::Spyglass:
        touchCtl(c, true);
        c.pressAt = clock;
        ed.toggleSpyglass();
        ed.status(ed.spyglassOpen() ? "SPYGLASS: the spectrum (and its waterfall) and the scope of what the machine plays. Touch the spyglass again to close it."
                                    : "SPYGLASS closed.");
        break;
    case CType::Socket:
        if (c.role == R_SockOut)
        {
            startHose(c.op, xy, -1);
            drag.move = [this](FPoint p, const juce::MouseEvent &) { moveHose(p); };
            drag.up = [this](FPoint p, const juce::MouseEvent &) { endHose(p); };
            drag.active = true;
        }
        else
            ed.status("This is GHOST " + juce::String(c.op + 1) + "'s IN socket: drag a hose here from a ghost above it.");
        break;
    default:
        break;
    }
}

void MachineEditor::Impl::move(const juce::MouseEvent &e)
{
    lastTouchMs = juce::Time::getMillisecondCounterHiRes();
    hoverAt(ed.toDesign(e.position), true);
}

// ============================================================================================== the component's events
void MachineEditor::mouseMove(const juce::MouseEvent &e) { impl->move(e); }
void MachineEditor::mouseExit(const juce::MouseEvent &) { impl->hoverAt({-1000, -1000}, false); }
void MachineEditor::mouseDown(const juce::MouseEvent &e)
{
    impl->down(e);
    raiseSpyglass();
}
void MachineEditor::mouseDrag(const juce::MouseEvent &e)
{
    if (impl->drag.active && impl->drag.move)
        impl->drag.move(toDesign(e.position), e);
}
void MachineEditor::mouseUp(const juce::MouseEvent &e)
{
    if (impl->drag.active)
    {
        auto up = std::move(impl->drag.up);
        impl->drag = Impl::Drag();
        if (up)
            up(toDesign(e.position), e);
    }
    impl->move(e);
    raiseSpyglass();
}

void MachineEditor::mouseWheelMove(const juce::MouseEvent &e, const juce::MouseWheelDetails &wd)
{
    if (!impl->as || impl->drag.active)
        return;
    const FPoint xy = toDesign(e.position);
    const float dy = std::abs(wd.deltaY) > std::abs(wd.deltaX) ? wd.deltaY : -wd.deltaX;
    if (impl->backWheel(xy, dy * (wd.isReversed ? -1.0f : 1.0f)))
        return;
    if (impl->studioWheel(xy, dy * (wd.isReversed ? -1.0f : 1.0f)))
        return;
    if (impl->ledger.mode != Ledger::Closed && impl->ledgerArea().contains(xy))
    {
        impl->ledgerWheel(xy, dy);
        return;
    }
    const int ci = impl->ctlAt(xy);
    if (ci < 0 || dy == 0.f)
        return;
    auto &c = impl->ctls[(size_t)ci];
    if (c.type == CType::Wheel)
    { // as the first editor's knobs: the wheel turns it
        float step = dy * (wd.isReversed ? -1.0f : 1.0f) * 0.15f;
        if (auto *p = impl->paramOf(c); p != nullptr && steppedParam(*p))
        {
            const float one = 1.0f / juce::jmax(1.0f, p->meta.maxVal - p->meta.minVal);
            step = (step > 0 ? 1.0f : -1.0f) * juce::jmax(one, std::abs(step));
        }
        const float v = impl->getCtl01(c) + step;
        impl->setCtl01(c, 0, 0, false);
        impl->setCtl01(c, v, 1, e.mods.isShiftDown());
        impl->setCtl01(c, 0, 2, false);
        impl->touchCtl(c, false);
        uiDirty = true;
    }
    else if (c.role == R_CornerOct && impl->ed.morph && impl->ed.morph->kind[(size_t)c.corner].load() != Morph::EMPTY)
    {
        impl->wheelAcc += dy * (wd.isReversed ? -1.0f : 1.0f);
        if (std::abs(impl->wheelAcc) >= 0.25f)
        {
            const int d = impl->wheelAcc > 0 ? 1 : -1;
            impl->wheelAcc = 0;
            impl->setCornerOct(c.corner, impl->ed.morph->octave[(size_t)c.corner].load() + d);
        }
    }
    else if (c.type == CType::Selector)
    {
        impl->wheelAcc += dy * (wd.isReversed ? -1.0f : 1.0f);
        if (std::abs(impl->wheelAcc) >= 0.25f)
        {
            const int d = impl->wheelAcc > 0 ? 1 : -1;
            impl->wheelAcc = 0;
            const int to = juce::jlimit(0, impl->selectorCount(c) - 1, impl->selectorIndex(c) + d);
            if (c.role != R_Theme && to != impl->selectorIndex(c))
                impl->stepSelector(c, to);
        }
    }
}

// the on-screen keyboard also plays from the computer's keys while the window has the focus (as before)
static const char *kTyping = "awsedftgyhujkolp;";
bool MachineEditor::keyPressed(const juce::KeyPress &k)
{
    if (k == juce::KeyPress::escapeKey && impl->backOpen)
    {
        impl->backToggle(false);
        return true;
    }
    const auto ch = juce::CharacterFunctions::toLowerCase(k.getTextCharacter());
    if (k.getModifiers().isCommandDown() || k.getModifiers().isCtrlDown() || k.getModifiers().isAltDown())
        return false;
    return ch != 0 && juce::String(kTyping).containsChar(ch);
}
bool MachineEditor::keyStateChanged(bool)
{
    bool any = false;
    for (int i = 0; kTyping[i] != 0; ++i)
    {
        const bool dn = juce::KeyPress::isKeyCurrentlyDown(kTyping[i]) || juce::KeyPress::isKeyCurrentlyDown(juce::CharacterFunctions::toUpperCase((juce::juce_wchar)kTyping[i]));
        if (dn != impl->typed[(size_t)i])
        {
            impl->typed[(size_t)i] = dn;
            if (dn)
                impl->noteOn(48 + i, 0.8f);
            else
                impl->noteOff(48 + i);
            any = true;
        }
    }
    return any;
}
void MachineEditor::focusLost(FocusChangeType) { impl->allNotesOff(); }
} // namespace ghostblock::p2::machine
