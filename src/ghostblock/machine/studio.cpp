/*
 * Poltergeist 2 (GhostBlock) - the ghost-machine editor: STUDIO VIEW, a plain, flat screen for the whole plugin.
 *
 * The machine stays the main face (owner, 2026-11-14: "I love our UI so much"); the studio is the way out of it into an
 * ordinary synth screen, and everything is in it (owner, 2026-11-14: "everything should be in there as the new UI look"):
 *   GHOSTS  a column per ghost: power, its envelope (LEVEL / FB / PITCH) as bars, RATIO OCTAVE FEEDBACK PAN WAVE ENV AMT,
 *           OUT and a VOLUME fader; the whole sound's essentials on the left
 *   MAIN    the whole sound: envelope, voice, output, tone, drift and pitch drop, the six macros
 *   WIRING  who haunts who as a grid (the Back Room stays a machine thing: no button for it here) (click to plug in, drag for how much, right-click to unplug) and the chosen wire's
 *           own controls (a link's mode, LFO, envelope, drive; a loop's feedback; a ghost's volume / pan)
 *   MORPH   the four corners (sound, volume, pan, octave), the blend pad with its puck, MORPH on, orbit, rate, size
 *   FX      SPECTRAL CORRUPTOR (slots, the chosen slot's effect) and WARPCORE
 *   SUB     the sub bass and its presets
 * The way in: a lit window of a house on the machine (GHOSTS, MAIN), the lighthouse lantern (FX), the sun (MORPH).
 * MACHINE VIEW goes back. The view used last is remembered.
 *
 * Most controls here ARE the machine's own controls, drawn flat: the studio calls the same press / drag / step code
 * (pressCtl, getCtl01, selectorText, stepSelector), so nothing behaves differently. The ghost columns and envelope bars
 * edit their parameters directly, with the same BEGIN_EDIT / SET_PARAM / END_EDIT messages. A control that does nothing
 * right now is greyed and does not move.
 */
#include "ghostblock/machine/machine-impl.h"

namespace ghostblock::p2::machine
{
using PMD = sst::basic_blocks::params::ParamMetaData;

namespace st
{
static const juce::Colour bg{0xff14161d}, panel{0xff1c1f29}, panel2{0xff232735}, line{0xff2e3344}, text{0xffd9dce6}, dim{0xff8a90a3},
    amber{0xffffc25a}, tabOn{0xffeef0f5}, ink{0xff15161b};
}
static const char *kTabNames[] = {"GHOSTS", "MAIN", "WIRING", "MORPH", "FX", "SUB"};
static constexpr int kTabs = 6;
static constexpr float kTop = 76.0f, kBottom = 620.0f; // the content area

// the ways in, per machine page (design px): a lit window of a house, the lighthouse lantern, the sun
FRect MachineEditor::Impl::studioDoor() const
{
    if (!as || page < 0 || page >= as->pageKeys.size())
        return {};
    const auto &k = as->pageKeys[page];
    if (k == "ghosts")
        return {124, 118, 36, 58};
    if (k == "main")
        return {178, 156, 42, 50};
    if (k == "fx")
        return {852, 82, 42, 50};
    if (k == "morph")
        return {104, 78, 64, 64};
    return {};
}

// ============================================================================================== building the screen
Param *MachineEditor::Impl::stParam(const StW &w) const { return w.pf ? w.pf() : w.p; }
Ctl *MachineEditor::Impl::stCtl(const StW &w)
{
    return w.ctlIx >= 0 && w.ctlIx < (int)ctls.size() ? &ctls[(size_t)w.ctlIx] : nullptr;
}
const Ctl *MachineEditor::Impl::stCtl(const StW &w) const
{
    return w.ctlIx >= 0 && w.ctlIx < (int)ctls.size() ? &ctls[(size_t)w.ctlIx] : nullptr;
}

Param *MachineEditor::Impl::stEnvParam(int o, int k)
{
    auto pick = [k](auto &n) -> Param * {
        Param *e[4] = {&n.attack, &n.decay, &n.sustain, &n.release};
        return e[juce::jlimit(0, 3, k)];
    };
    const int which = stEnvSel[(size_t)o];
    return which == 1 ? pick(P.selfNodes[(size_t)o]) : which == 2 ? pick(P.sourceNodes[(size_t)o]) : pick(P.mixerNodes[(size_t)o]);
}

void MachineEditor::Impl::studioBuild()
{
    stW.clear();
    stPanels.clear();
    stHover = stHeld = -1;
    auto add = [&](StW w) {
        stW.push_back(std::move(w));
        return (int)stW.size() - 1;
    };
    auto mk = [](StW::K k, FRect r, juce::String label, juce::Colour col, int tab) {
        StW w;
        w.k = k;
        w.r = r;
        w.label = std::move(label);
        w.col = col;
        w.tab = tab;
        return w;
    };
    auto panel = [&](int tab, FRect r, juce::String title, juce::Colour col = st::amber) { stPanels.push_back({tab, r, std::move(title), col}); };
    // one of the machine's own controls, drawn flat here (its kind from what it is on the machine)
    auto proxy = [&](const juce::String &id, FRect r, int tab, juce::Colour col, juce::String label = {}) -> int {
        int ix = -1;
        for (int i = 0; i < (int)ctls.size(); ++i)
            if (ctls[(size_t)i].id == id)
                ix = i;
        if (ix < 0)
            return -1;
        const auto &c = ctls[(size_t)ix];
        StW::K k = StW::Button;
        if (c.type == CType::Wheel)
            k = StW::Knob;
        else if (c.type == CType::Selector || c.type == CType::Drum || c.role == R_CornerOct)
            k = StW::Choice;
        else if (c.type == CType::Switch)
            k = StW::Toggle;
        if (k == StW::Choice && label.isEmpty() && c.role != R_CornerOct)
            label = c.name; // (a dial's own words often already say its value: its name instead)
        StW w = mk(k, r, label, col, tab);
        w.ctlIx = ix;
        w.tip = c.name.isNotEmpty() ? c.name : label;
        w.bip = c.role == R_CornerPan || c.id.endsWith("-pan");
        return add(w);
    };
    // lay a row of the machine's controls into a panel: knobs in slots, a choice / toggle / button centred in its slot
    auto row = [&](int tab, FRect area, std::initializer_list<const char *> ids, juce::Colour col = st::amber, float slotW = 0) {
        const int n = (int)ids.size();
        const float sw = slotW > 0 ? slotW : area.getWidth() / (float)juce::jmax(1, n);
        int i = 0;
        for (auto *id : ids)
        {
            const FRect slot(area.getX() + sw * (float)i, area.getY(), sw, area.getHeight());
            if (const Ctl *c = find(id); c != nullptr)
            {
                FRect r = slot.reduced(4, 0);
                if (c->type == CType::Switch)
                    r = FRect(slot.getCentreX() - 26, slot.getY() + 34, 52, 22);
                else if (c->type != CType::Wheel)
                    r = FRect(slot.getX() + 6, slot.getY() + 32, sw - 12, 26);
                proxy(id, r, tab, col);
            }
            ++i;
        }
    };

    // ---- the top bar (every tab)
    {
        auto w = mk(StW::Button, {8, 9, 26, 26}, "<", st::text, -1);
        w.act = [this] { stepPreset(-1); };
        w.tip = "The sound before";
        add(w);
        w = mk(StW::Button, {36, 9, 300, 26}, "", st::text, -1);
        w.text = [this] { return juce::String(juce::CharPointer_UTF8(P.name)) + (P.dirty ? " *" : ""); };
        w.act = [this] { openLedger(Ledger::Sounds, 0); };
        w.tip = "Pick a sound";
        w.big = true;
        add(w);
        w = mk(StW::Button, {338, 9, 26, 26}, ">", st::text, -1);
        w.act = [this] { stepPreset(1); };
        w.tip = "The next sound";
        add(w);
        w = mk(StW::Button, {372, 9, 52, 26}, "SAVE", st::text, -1);
        w.act = [this] { saveSound(); };
        w.tip = "Save this sound";
        add(w);
        w = mk(StW::Button, {428, 9, 52, 26}, "LOAD", st::text, -1);
        w.act = [this] { openLedger(Ledger::Yours, 0); };
        w.tip = "Your saved sounds";
        add(w);
        w = mk(StW::Button, {484, 9, 46, 26}, "INIT", st::text, -1);
        w.act = [this] {
            pm->loadInit(P, ed.mainToAudio);
            ed.status("INIT: one ghost awake, nothing plugged in.");
        };
        w.tip = "Start from one plain ghost";
        add(w);
        w = mk(StW::Button, {760, 9, 76, 26}, "SPECTRUM", st::text, -1);
        w.act = [this] { ed.toggleSpyglass(); };
        w.tip = "The spectrum and the scope, in a window of their own";
        add(w);
        w = mk(StW::Button, {840, 9, 48, 26}, "SIZE", st::text, -1);
        w.act = [this] { ed.sizeMenu(); };
        w.tip = "Window size";
        add(w);
        w = mk(StW::Button, {892, 9, 140, 26}, "MACHINE VIEW", st::amber, -1);
        w.act = [this] { studioToggle(false); };
        w.tip = "Back to the ghost machine";
        w.accent = true;
        add(w);
    }
    // ---- the tabs
    {
        const float tw = 1040.0f / (float)kTabs;
        for (int i = 0; i < kTabs; ++i)
        {
            auto w = mk(StW::Tab, {tw * (float)i, 44, tw, 26}, kTabNames[i], st::text, -1);
            w.tabIx = i;
            w.act = [this, i] {
                stTab = i;
                markAllDirty();
            };
            add(w);
        }
    }

    // ================================================================================== GHOSTS
    const float x0 = 158, cw = colW();
    for (int o = 0; o < (int)ss::numOps; ++o)
    {
        auto &s = P.sourceNodes[(size_t)o];
        auto &mx = P.mixerNodes[(size_t)o];
        auto &sf = P.selfNodes[(size_t)o];
        const auto col = ghostColour(o);
        const float x = x0 + cw * (float)o;
        auto awakeF = [this, o] { return awake(o); };

        auto w = mk(StW::Toggle, {x + cw - 34, 80, 26, 20}, "", col, 0);
        w.p = &s.active;
        w.op = o;
        w.tip = "Wake / sleep GHOST " + juce::String(o + 1);
        add(w);
        // which envelope the bars show: LEVEL (how loud), FB (its feedback), PITCH (its ratio)
        const char *en3[3] = {"LEVEL", "FB", "PITCH"};
        const float ew = (cw - 16) / 3.0f;
        for (int e3 = 0; e3 < 3; ++e3)
        {
            w = mk(StW::Seg, {x + 8 + ew * (float)e3, 104, ew - 2, 16}, en3[e3], col, 0);
            w.op = o;
            w.tabIx = e3;
            w.act = [this, o, e3] {
                stEnvSel[(size_t)o] = e3;
                sceneDirty.mark(FRect(x0Of(o), kTop, colW(), kBottom - kTop));
            };
            w.lit = [this, o, e3] { return stEnvSel[(size_t)o] == e3; };
            w.tip = e3 == 0 ? "Show the LEVEL envelope (how loud the ghost is over a note)"
                            : e3 == 1 ? "Show the FEEDBACK envelope (how its loop moves over a note)" : "Show the PITCH envelope (how its ratio moves over a note)";
            w.enabled = awakeF;
            add(w);
        }
        auto envOnP = [this, o]() -> Param * {
            const int k = stEnvSel[(size_t)o];
            return k == 1 ? &P.selfNodes[(size_t)o].envPower : k == 2 ? &P.sourceNodes[(size_t)o].envPower : &P.mixerNodes[(size_t)o].envPower;
        };
        const FRect box(x + 8, 146, cw - 16, 104);
        const char *en[4] = {"A", "D", "S", "R"};
        const float bw = box.getWidth() / 4.0f;
        for (int k = 0; k < 4; ++k)
        {
            w = mk(StW::Bar, {box.getX() + bw * (float)k, box.getY(), bw, box.getHeight()}, en[k], col, 0);
            w.pf = [this, o, k] { return stEnvParam(o, k); };
            w.op = o;
            w.group = box;
            w.enabled = [this, o, envOnP] { return awake(o) && envOnP()->value > 0.5f; };
            w.tip = juce::String("GHOST ") + juce::String(o + 1) + " envelope " + en[k];
            add(w);
        }
        w = mk(StW::Toggle, {x + cw - 34, 124, 26, 16}, "", col, 0);
        w.pf = envOnP;
        w.op = o;
        w.enabled = awakeF;
        w.small = true;
        w.tip = "This envelope on / off";
        add(w);
        const float kx[2] = {x + 8, x + 8 + (cw - 54) * 0.5f};
        const float kw = (cw - 54) * 0.5f, ky[3] = {262, 360, 458};
        struct KD
        {
            Param *p;
            const char *n;
            int row, colIx;
        } kd[] = {{&s.ratio, "RATIO", 0, 0}, {&s.octTranspose, "OCTAVE", 0, 1}, {&sf.fbLevel, "FEEDBACK", 1, 0}, {&mx.pan, "PAN", 1, 1}};
        for (auto &d : kd)
        {
            w = mk(StW::Knob, {kx[d.colIx], ky[d.row], kw, 92}, d.n, col, 0);
            w.p = d.p;
            w.op = o;
            if (d.p == &mx.pan)
                w.enabled = [this, o] { return awake(o) && P.mixerNodes[(size_t)o].active.value > 0.5f; };
            else if (d.p == &sf.fbLevel)
            {
                w.enabled = awakeF;
                w.onEdit = [this, o] { // turning FEEDBACK up switches the ghost's loop on (what the machine's loop does)
                    auto &sn = P.selfNodes[(size_t)o];
                    if (sn.active.value < 0.5f && std::abs(sn.fbLevel.value) > 0.005f)
                        ed.setOnce(sn.active, 1.f);
                };
                w.lit = [this, o] { return P.selfNodes[(size_t)o].active.value > 0.5f; };
            }
            else
                w.enabled = awakeF;
            w.tip = juce::String("GHOST ") + juce::String(o + 1) + " " + d.n;
            add(w);
        }
        w = mk(StW::Knob, {kx[1], ky[2], kw, 92}, "ENV AMT", col, 0);
        w.pf = [this, o]() -> Param * {
            const int k = stEnvSel[(size_t)o];
            return k == 1 ? &P.selfNodes[(size_t)o].envToFB : k == 2 ? &P.sourceNodes[(size_t)o].envToRatio : &P.mixerNodes[(size_t)o].envToLevel;
        };
        w.op = o;
        w.enabled = [this, o, envOnP] { return awake(o) && envOnP()->value > 0.5f; };
        w.tip = juce::String("GHOST ") + juce::String(o + 1) + " how far the envelope moves it";
        add(w);
        w = mk(StW::Choice, {kx[0], ky[2] + 28, kw + 4, 26}, "WAVE", col, 0);
        w.p = &s.waveForm;
        w.op = o;
        w.enabled = awakeF;
        w.tip = juce::String("GHOST ") + juce::String(o + 1) + " wave";
        add(w);
        w = mk(StW::Toggle, {x + cw - 44, 262, 36, 18}, "OUT", col, 0);
        w.p = &mx.active;
        w.op = o;
        w.enabled = awakeF;
        w.tip = "In the mix (you hear it) / only haunting others";
        add(w);
        w = mk(StW::Fader, {x + cw - 42, 296, 32, 254}, "VOLUME", col, 0);
        w.p = &mx.level;
        w.op = o;
        w.enabled = [this, o] { return awake(o) && P.mixerNodes[(size_t)o].active.value > 0.5f; };
        w.tip = juce::String("GHOST ") + juce::String(o + 1) + " volume in the mix";
        add(w);
    }
    {
        panel(0, {0, 70, x0 - 2, kBottom - 66}, "THE SOUND");
        auto &out = P.output;
        struct KD
        {
            Param *p;
            const char *n;
            float x, y;
        } kd[] = {{&out.level, "MASTER", 10, 104}, {&out.pan, "PAN", 80, 104}, {&out.octTranspose, "OCTAVE", 10, 210},
                  {&out.fineTune, "TUNE", 80, 210},  {&out.portaTime, "GLIDE", 10, 316}, {&out.velSensitivity, "VELOCITY", 80, 316}};
        for (auto &d : kd)
        {
            auto w = mk(StW::Knob, {d.x, d.y, 66, 92}, d.n, st::amber, 0);
            w.p = d.p;
            w.tip = juce::String("The whole sound: ") + d.n;
            if (d.p == &out.portaTime)
                w.enabled = [this] { return P.output.playMode.value > 0.5f; }; // glide needs MONO / LEGATO
            add(w);
        }
        auto w = mk(StW::Button, {12, 430, 134, 28}, "WIRING", st::amber, 0);
        w.act = [this] {
            stTab = 2;
            markAllDirty();
        };
        w.tip = "Who haunts who, and how much";
        add(w);
    }

    // ================================================================================== MAIN
    {
        auto &out = P.output;
        panel(1, {8, kTop, 412, 214}, "ENVELOPE  (THE WHOLE SOUND)");
        const FRect box(24, 120, 380, 156);
        Param *env[6] = {&out.delay, &out.attack, &out.hold, &out.decay, &out.sustain, &out.release};
        const char *en[6] = {"DELAY", "ATTACK", "HOLD", "DECAY", "SUSTAIN", "RELEASE"};
        const float bw = box.getWidth() / 6.0f;
        for (int k = 0; k < 6; ++k)
        {
            auto w = mk(StW::Bar, {box.getX() + bw * (float)k, box.getY(), bw, box.getHeight()}, en[k], st::amber, 1);
            w.p = env[k];
            w.group = box;
            w.enabled = [this] { return P.output.envPower.value > 0.5f; };
            w.tip = juce::String("The whole sound's envelope: ") + en[k];
            add(w);
        }
        auto w = mk(StW::Toggle, {box.getRight() - 30, 92, 26, 16}, "", st::amber, 1);
        w.p = &out.envPower;
        w.small = true;
        w.tip = "The whole sound's envelope on / off";
        add(w);
        panel(1, {8, 296, 412, 278}, "VOICE");
        row(1, {14, 318, 400, 96}, {"v-mono", "m-voices", "v-glide", "m-glidefrom"});
        row(1, {14, 420, 400, 96}, {"v-octave", "v-unison", "m-spread", "m-stereo"});
        panel(1, {428, kTop, 604, 120}, "OUTPUT");
        row(1, {432, 98, 596, 96}, {"v-master", "m-gain", "m-pan", "m-vel", "m-tune", "m-bendup", "m-benddn"});
        panel(1, {428, 202, 604, 120}, "TONE");
        row(1, {432, 224, 596, 96}, {"tone-sat", "tone-drive", "tone-lp", "tone-hp", "m-bitrate", "m-bits"});
        panel(1, {428, 328, 604, 120}, "DRIFT  /  PITCH DROP");
        row(1, {432, 350, 596, 96}, {"dr-rate", "dr-amount", "dr-shape", "dr-sync", "pd-amount", "pd-decay"});
        panel(1, {428, 454, 604, 120}, "MACROS");
        row(1, {432, 476, 596, 96}, {"macro1", "macro2", "macro3", "macro4", "macro5", "macro6"});
        // greyed when they do nothing: glide without MONO, spread / stereo with one unison voice, drive without saturation
        auto greyWhen = [&](const char *id, std::function<bool()> f) {
            for (auto &x : stW)
                if (x.tab == 1 && x.ctlIx >= 0 && ctls[(size_t)x.ctlIx].id == id)
                    x.enabled = f;
        };
        auto mono = [this] { return P.output.playMode.value > 0.5f; };
        auto unison = [this] { return P.output.unisonCount.value > 1.5f; };
        greyWhen("v-glide", mono);
        greyWhen("m-glidefrom", mono);
        greyWhen("m-spread", unison);
        greyWhen("m-stereo", unison);
        greyWhen("tone-drive", [this] { return P.output.saturationType.value > 0.5f; });
        greyWhen("pd-decay", [this] { return std::abs(P.fineTuneMod.envCoarseDepth.value) > 1e-4f; });
    }

    // ================================================================================== WIRING
    {
        panel(2, {8, kTop, 532, kBottom - kTop}, "WHO HAUNTS WHO");
        for (int i = 0; i < (int)backCells.size(); ++i)
        {
            const auto &k = backCells[(size_t)i];
            const int colIx = k.kind == 2 ? 6 : k.to;
            auto w = mk(StW::Cell, {kWireX + kWireP * (float)colIx, kWireY + kWireP * (float)k.from, kWireP - 8, kWireP - 8}, "", ghostColour(k.from), 2);
            w.cell = i;
            w.op = k.from;
            w.tip = backWhat(k);
            add(w);
        }
        panel(2, {548, kTop, 484, kBottom - kTop}, "THE CHOSEN WIRE");
        auto selKind = [this] { return stSelCell >= 0 && stSelCell < (int)backCells.size() ? backCells[(size_t)stSelCell].kind : -1; };
        // a link's own controls (the machine's link bench: they follow the chosen link)
        const int first = (int)stW.size();
        row(2, {556, 150, 468, 96}, {"l-depth", "l-mode", "l-lfo", "l-envamt"});
        row(2, {556, 256, 468, 96}, {"l-envon", "l-drive"}, st::amber, 117.0f);
        for (int i = first; i < (int)stW.size(); ++i)
            stW[(size_t)i].visible = [selKind] { return selKind() == 0; };
        // a ghost into itself / what you hear of it
        auto selFrom = [this] { return stSelCell >= 0 && stSelCell < (int)backCells.size() ? backCells[(size_t)stSelCell].from : 0; };
        auto addSel = [&](const char *n, std::function<Param *()> pf, int kind, float x, float y, StW::K kk = StW::Knob) {
            auto w = mk(kk, kk == StW::Knob ? FRect(x, y, 100, 96) : FRect(x + 24, y + 34, 52, 22), n, st::amber, 2);
            w.pf = std::move(pf);
            w.visible = [selKind, kind] { return selKind() == kind; };
            w.tip = n;
            add(w);
        };
        addSel("FEEDBACK", [this, selFrom] { return &P.selfNodes[(size_t)selFrom()].fbLevel; }, 1, 560, 150);
        addSel("ENV AMT", [this, selFrom] { return &P.selfNodes[(size_t)selFrom()].envToFB; }, 1, 676, 150);
        addSel("LFO AMT", [this, selFrom] { return &P.selfNodes[(size_t)selFrom()].lfoToFB; }, 1, 792, 150);
        addSel("OVERDRIVE", [this, selFrom] { return &P.selfNodes[(size_t)selFrom()].overdrive; }, 1, 908, 150, StW::Toggle);
        addSel("VOLUME", [this, selFrom] { return &P.mixerNodes[(size_t)selFrom()].level; }, 2, 560, 150);
        addSel("PAN", [this, selFrom] { return &P.mixerNodes[(size_t)selFrom()].pan; }, 2, 676, 150);
        addSel("ENV AMT", [this, selFrom] { return &P.mixerNodes[(size_t)selFrom()].envToLevel; }, 2, 792, 150);
        auto w = mk(StW::Button, {560, 520, 140, 28}, "UNPLUG", st::text, 2);
        w.act = [this] {
            if (stSelCell >= 0 && stSelCell < (int)backCells.size() && backActive(backCells[(size_t)stSelCell]))
                backUnplug(backCells[(size_t)stSelCell]);
        };
        w.visible = [this] { return stSelCell >= 0 && stSelCell < (int)backCells.size() && backActive(backCells[(size_t)stSelCell]); };
        w.tip = "Unplug this wire";
        add(w);
    }

    // ================================================================================== MORPH
    {
        static const char *L[Morph::N] = {"A", "B", "C", "D"};
        const FRect cards[Morph::N] = {{8, kTop, 300, 266}, {732, kTop, 300, 266}, {8, 350, 300, 270}, {732, 350, 300, 270}};
        for (int c = 0; c < Morph::N; ++c)
        {
            const auto r = cards[c];
            const auto col = cornerColour(c);
            panel(3, r, juce::String("CORNER ") + L[c], col);
            proxy(juce::String("o-") + L[c] + "-sound", {r.getX() + 12, r.getY() + 30, r.getWidth() - 24, 30}, 3, col);
            const int iv = proxy(juce::String("o-") + L[c] + "-vol", {r.getX() + 10, r.getY() + 74, 90, 96}, 3, col, "VOLUME");
            const int ip = proxy(juce::String("o-") + L[c] + "-pan", {r.getX() + 104, r.getY() + 74, 90, 96}, 3, col, "PAN");
            const int io = proxy(juce::String("o-") + L[c] + "-oct", {r.getX() + 200, r.getY() + 106, 88, 26}, 3, col, "OCTAVE");
            auto filled = [this, c] { return ed.morph && ed.morph->kind[(size_t)c].load() != Morph::EMPTY; };
            for (int ix : {iv, ip, io})
                if (ix >= 0)
                    stW[(size_t)ix].enabled = filled;
            auto w = mk(StW::Button, {r.getX() + 12, r.getY() + 190, r.getWidth() - 24, 28}, "PUCK HERE  (JUST THIS SOUND)", col, 3);
            w.act = [this, c] {
                if (!ed.morph || ed.morph->kind[(size_t)c].load() == Morph::EMPTY)
                    return;
                ed.morphSet(Morph::pX, (double)(c % 2));
                ed.morphSet(Morph::pY, (double)(c / 2));
                ed.markStateDirty();
                ed.uiDirty = true;
                ed.status(juce::String("The puck is on corner ") + L[c] + ": exactly that sound.");
            };
            w.enabled = filled;
            w.tip = juce::String("Put the puck on corner ") + L[c];
            add(w);
        }
        panel(3, {316, kTop, 408, kBottom - kTop}, "THE BLEND");
        auto w = mk(StW::XY, stPad, "", st::amber, 3);
        w.tip = "Drag the puck: the blend of the four corners";
        add(w);
        proxy("morph-on", {339, 466, 60, 22}, 3, st::amber, "MORPH");
        const int isz = proxy("o-size", {416, 432, 90, 96}, 3, st::amber, "SIZE");
        proxy("morph-orbit", {516, 464, 92, 26}, 3, st::amber, "ORBIT");
        const int irt = proxy("morph-rate", {616, 464, 96, 26}, 3, st::amber, "EVERY");
        auto orbiting = [this] { return ed.morph && ed.morph->orbit.load() != Morph::O_OFF; }; // SIZE and EVERY: only with an orbit
        for (int ix : {isz, irt})
            if (ix >= 0)
                stW[(size_t)ix].enabled = orbiting;
    }

    // ================================================================================== FX
    {
        panel(4, {8, kTop, 640, kBottom - kTop}, "SPECTRAL CORRUPTOR");
        row(4, {14, 96, 628, 96}, {"fx-on", "fx-mix", "fx-fft", "fx-window", "fx-overlap", "fx-limit"});
        for (int sl = 1; sl <= 4; ++sl)
        {
            const float y = 204 + 44.0f * (float)(sl - 1);
            const auto n = juce::String(sl);
            proxy("fx-slot" + n, {20, y, 70, 36}, 4, st::amber, "SLOT " + n);
            proxy("fx-slot" + n + "-name", {96, y, 230, 36}, 4, st::text);
            proxy("fx-slot" + n + "-on", {332, y, 56, 36}, 4, st::amber, "ON");
        }
        proxy("fx-bench-title", {396, 204, 240, 36}, 4, st::text);
        proxy("fx-slot-mix", {396, 250, 100, 96}, 4, st::amber, "SLOT MIX");
        row(4, {14, 396, 628, 96}, {"fx-p1", "fx-p2", "fx-p3", "fx-p4"}, st::amber, 157.0f);
        row(4, {14, 500, 628, 96}, {"fx-p5", "fx-p6", "fx-p7", "fx-p8"}, st::amber, 157.0f);
        panel(4, {656, kTop, 376, kBottom - kTop}, "WARPCORE");
        row(4, {662, 96, 364, 96}, {"fx-w-on", "fx-w-warp", "fx-w-freqmode"});
        row(4, {662, 200, 364, 96}, {"fx-w-fhigh", "fx-w-scale", "fx-w-poles"});
        row(4, {662, 304, 364, 96}, {"fx-w-pitch", "fx-w-formant", "fx-w-fill"});
        row(4, {662, 408, 364, 96}, {"fx-w-drywet"}, st::amber, 121.0f);
    }

    // ================================================================================== SUB
    {
        panel(5, {8, kTop, 1024, 300}, "SUB BASS  (UNDER THE LOWEST NOTE YOU PLAY)");
        row(5, {14, 100, 1012, 96}, {"sub-on", "sub-level", "sub-octave", "sub-tune", "sub-glide", "sub-phase"});
        row(5, {14, 220, 1012, 96}, {"sub-width", "sub-drive", "sub-mode", "sub-harmonics", "sub-blend", "sub-room"});
        panel(5, {8, 384, 1024, 110}, "PRESETS");
        for (int i = 1; i <= 8; ++i)
            proxy("sub-preset" + juce::String(i), {20.0f + 126.0f * (float)(i - 1), 420, 118, 34}, 5, st::amber);
        if (!ed.sub)
            panel(5, {8, 500, 1024, 40}, "(SUB is not part of this copy of the plugin)", st::dim);
    }

    for (auto &w : stW)
        w.cells = cellsOf((w.group.isEmpty() ? w.r : w.group).expanded(w.k == StW::Toggle ? 30 : 16));
}

float MachineEditor::Impl::x0Of(int o) const { return 158.0f + colW() * (float)o; }
float MachineEditor::Impl::colW() const { return (1040.0f - 158.0f) / (float)ss::numOps; }

bool MachineEditor::Impl::studioEnabled(const StW &w) const
{
    if (w.enabled)
        return w.enabled();
    if (auto *c = stCtl(w); c != nullptr && w.k != StW::Button)
        return !c->labOff;
    return true;
}
bool MachineEditor::Impl::stVisible(const StW &w) const { return (w.tab < 0 || w.tab == stTab) && (!w.visible || w.visible()); }

float MachineEditor::Impl::studioNorm(const Param &p) const
{
    const float lo = p.meta.minVal, hi = p.meta.maxVal;
    return hi > lo ? juce::jlimit(0.f, 1.f, (p.value - lo) / (hi - lo)) : 0.f;
}

// what a widget shows: its 0..1, its words, on / lit
float MachineEditor::Impl::stNorm(StW &w)
{
    if (auto *c = stCtl(w))
        return getCtl01(*c);
    auto *p = stParam(w);
    return p ? studioNorm(*p) : 0.f;
}
juce::String MachineEditor::Impl::stValText(StW &w)
{
    if (auto *c = stCtl(w))
        return w.k == StW::Choice ? selectorText(*c, selectorIndex(*c)) : c->t2;
    auto *p = stParam(w);
    return p ? compact(valueText(*p, p->value)) : juce::String();
}
juce::String MachineEditor::Impl::stLabel(StW &w)
{
    if (w.label.isNotEmpty())
        return w.label;
    if (auto *c = stCtl(w))
        return c->t1.isNotEmpty() ? c->t1 : c->name;
    return {};
}
bool MachineEditor::Impl::stOn(StW &w)
{
    if (auto *c = stCtl(w))
        return c->type == CType::Switch ? switchOn(*c) : (c->on || c->lit);
    if (w.lit && w.k != StW::Knob)
        return w.lit();
    auto *p = stParam(w);
    return p && p->value > 0.5f;
}

// ============================================================================================== open / close
void MachineEditor::Impl::studioToggle(bool open)
{
    if (!as || open == studioOpen)
        return;
    closeLedger();
    if (subOpen)
        subToggle(false);
    drag = Drag();
    if (hoverCtl >= 0 && hoverCtl < (int)ctls.size())
        ctls[(size_t)hoverCtl].hover = false;
    hoverCtl = hoverHoseValve = hoverPin = -1;
    doorHover = -1;
    stHover = -1;
    stButtonHover = false;
    pageFrom = open ? page : page + 1;
    startTurn(page);
    studioOpen = open;
    for (auto &w : stW)
        w.sig = ~0ull;
    if (prefs)
        prefs->setValue("view", open ? "studio" : "machine");
    markAllDirty();
    refresh();
    ed.uiDirty = true;
    ed.status(open ? "STUDIO VIEW: the whole plugin on a plain screen. MACHINE VIEW (top right) goes back to the ghost machine." : "The ghost machine.");
}

// ============================================================================================== the clock: what changed
uint64_t MachineEditor::Impl::studioSig(StW &w)
{
    uint64_t h = (uint64_t)w.tab + 1;
    auto mixIn = [&h](uint64_t v) { h = (h ^ v) * 0x100000001b3ull; };
    const bool vis = stVisible(w);
    mixIn(vis ? 11 : 12);
    if (!vis)
        return h;
    if (w.k == StW::Cell)
    {
        if (w.cell >= 0 && w.cell < (int)backCells.size())
        {
            auto &k = backCells[(size_t)w.cell];
            mixIn(backActive(k) ? 1 : 2);
            mixIn((uint64_t)(backShown(k) + 1000));
            mixIn(stSelCell == w.cell ? 3 : 4);
            mixIn(awake(k.from) ? 5 : 6);
        }
        return h;
    }
    if (w.k == StW::XY && ed.morph)
    {
        mixIn((uint64_t)std::lround(ed.morph->effX.load() * 400.0f));
        mixIn((uint64_t)std::lround(ed.morph->effY.load() * 400.0f) + 7);
        for (int c = 0; c < Morph::N; ++c)
            mixIn((uint64_t)ed.morph->kind[(size_t)c].load() + 10 * (uint64_t)c);
        mixIn(ed.morph->on.load() ? 1 : 2);
        return h;
    }
    if (auto *c = stCtl(w))
    {
        mixIn((uint64_t)std::lround(getCtl01(*c) * 2000.0f));
        mixIn((uint64_t)c->t1.hashCode64());
        mixIn((uint64_t)c->t2.hashCode64());
        mixIn(stOn(w) ? 21 : 22);
        mixIn(c->labOff ? 23 : 24);
        if (w.k == StW::Choice)
            mixIn((uint64_t)selectorIndex(*c) + 100);
    }
    else if (auto *p = stParam(w))
    {
        uint32_t b;
        std::memcpy(&b, &p->value, 4);
        mixIn(b);
        mixIn((uint64_t)(uintptr_t)p);
    }
    mixIn(studioEnabled(w) ? 1 : 2);
    mixIn(w.lit && w.lit() ? 3 : 4);
    mixIn(w.text ? (uint64_t)w.text().hashCode64() : 5);
    mixIn(w.k == StW::Tab ? (uint64_t)(stTab == w.tabIx) + 7 : 6);
    return h;
}

void MachineEditor::Impl::studioTick(double)
{
    if (!studioOpen || backOpen)
        return;
    uint32_t aw = 0; // a ghost woken / put to sleep re-colours its whole column
    for (int o = 0; o < (int)ss::numOps; ++o)
        aw |= (awake(o) ? 1u : 0u) << o;
    if (aw != stAwake)
    {
        stAwake = aw;
        sceneDirty.fill();
    }
    for (int i = 0; i < (int)stW.size(); ++i)
    {
        auto &w = stW[(size_t)i];
        if (w.tab >= 0 && w.tab != stTab)
            continue;
        const uint64_t s = studioSig(w) ^ (uint64_t)((stHover == i) ? 0x55 : 0) ^ (uint64_t)((stHeld == i) ? 0xaa00 : 0);
        if (s != w.sig)
        {
            w.sig = s;
            sceneDirty.mark(w.cells);
        }
    }
    uint64_t kh = 0; // the keys held on the screen light up
    for (int n = kLoKey; n <= kHiKey; ++n)
        if (keysDown[n] > 0)
            kh = (kh * 131) ^ (uint64_t)n;
    if (kh != stKeysSig)
    {
        stKeysSig = kh;
        sceneDirty.mark(FRect(0, kKeysY - 2, 1040, 744 - kKeysY + 2));
    }
}

// ============================================================================================== drawing
void MachineEditor::Impl::stText(const Ctx &c, FRect r, const juce::String &t, float size, bool semi, juce::Colour col, int just, float alpha)
{
    if (t.isEmpty())
        return;
    TextStyle s;
    s.size = size;
    s.semi = semi;
    s.track = 0.04f;
    s.col = col;
    s.shadow = false;
    const float mx = r.getWidth() - 4.0f;
    const float w = as->textWidth(t, s), q = w > mx && w > 0 ? mx / w : 1.0f;
    const auto &ts = as->text(t, s, q);
    const float base = r.getY() + (r.getHeight() - size) * 0.5f + 0.85f * size;
    const float x = just < 0 ? r.getX() + 2 : just > 0 ? r.getRight() - 2 - ts.w : r.getCentreX() - ts.w * 0.5f;
    c.sprite(ts.spr, x - ts.pad, base - 1.05f * size - ts.pad, alpha);
}

void MachineEditor::Impl::studioDraw(const Ctx &c)
{
    if (!studioOpen || c.g == nullptr)
        return;
    auto &g = *c.g;
    g.setColour(st::bg);
    g.fillRect(FRect(0, 0, 1040, 744));
    g.setColour(st::panel);
    g.fillRect(FRect(0, 0, 1040, 44));
    if (c.touches(FRect(536, 0, 220, 44)))
    {
        stText(c, FRect(536, 6, 220, 20), "POLTERGEIST 2", 13.0f, true, st::text, 0);
        stText(c, FRect(536, 24, 220, 14), "STUDIO VIEW", 7.0f, true, st::dim, 0);
    }
    for (auto &pn : stPanels)
    {
        if (pn.tab != stTab || !c.touches(pn.r))
            continue;
        g.setColour(st::panel);
        g.fillRoundedRectangle(pn.r, 6.0f);
        stText(c, FRect(pn.r.getX() + 12, pn.r.getY() + 4, pn.r.getWidth() - 24, 18), pn.title, 9.0f, true, pn.col, -1);
    }
    if (stTab == 0)
    {
        for (int o = 0; o < (int)ss::numOps; ++o)
        {
            const float x = x0Of(o), cw = colW();
            const FRect colR(x, 70, cw - 2, kBottom - 66);
            if (!c.touches(colR))
                continue;
            const auto col = ghostColour(o);
            g.setColour(st::panel);
            g.fillRect(colR);
            g.setColour(col.withAlpha(awake(o) ? 0.16f : 0.05f));
            g.fillRect(colR);
            stText(c, FRect(x + 8, 78, cw - 50, 24), "GHOST " + juce::String(o + 1), 12.5f, true, awake(o) ? col.brighter(0.2f) : st::dim, -1);
            stText(c, FRect(x + 8, 124, cw - 50, 16), "ENVELOPE", 6.6f, true, st::dim, -1);
        }
    }
    else if (stTab == 2)
    { // the grid's heads: FROM down the side, TO along the top
        for (int o = 0; o < (int)ss::numOps; ++o)
        {
            const auto col = ghostColour(o);
            const FRect rh(kWireX - 44, kWireY + kWireP * (float)o, 40, kWireP - 8);
            if (c.touches(rh))
                stText(c, rh, juce::String(o + 1), 13.0f, true, awake(o) ? col : st::dim, 0);
            const FRect ch(kWireX + kWireP * (float)o, kWireY - 26, kWireP - 8, 20);
            if (c.touches(ch))
                stText(c, ch, juce::String(o + 1), 13.0f, true, awake(o) ? col : st::dim, 0);
        }
        const FRect oh(kWireX + kWireP * 6.0f, kWireY - 26, kWireP - 8, 20);
        if (c.touches(oh))
            stText(c, oh, "HEAR", 8.0f, true, st::amber, 0);
        const FRect legend(24, 560, 500, 40);
        if (c.touches(legend))
        {
            stText(c, legend.withHeight(18), "Row = the ghost that haunts.  Column = the ghost it haunts.  Diagonal = into itself.", 7.4f, false, st::dim, -1);
            stText(c, legend.withTrimmedTop(20), "Click: plug in / choose.  Drag up / down: how much.  Right-click: unplug.", 7.4f, false, st::dim, -1);
        }
        const FRect tr(556, 104, 468, 30);
        if (c.touches(tr))
        {
            juce::String t = "Choose a wire in the grid";
            juce::Colour tc = st::dim;
            if (stSelCell >= 0 && stSelCell < (int)backCells.size())
            {
                auto &k = backCells[(size_t)stSelCell];
                t = backWhat(k).toUpperCase() + (backActive(k) ? juce::String() : juce::String("  (not plugged in)"));
                tc = ghostColour(k.from);
            }
            stText(c, tr, t, 11.0f, true, tc, -1);
        }
    }

    std::vector<FRect> boxes;
    for (int i = 0; i < (int)stW.size(); ++i)
    {
        auto &w = stW[(size_t)i];
        if (!stVisible(w))
            continue;
        if (w.k == StW::Bar && std::find(boxes.begin(), boxes.end(), w.group) == boxes.end())
            boxes.push_back(w.group);
        if (!c.touches((w.group.isEmpty() ? w.r : w.group).expanded(24)))
            continue;
        studioDrawW(c, w, i);
    }
    for (auto &box : boxes)
    { // the envelope shapes over their bars
        if (!c.touches(box))
            continue;
        std::vector<StW *> bars;
        for (auto &w : stW)
            if (w.k == StW::Bar && w.group == box && stVisible(w))
                bars.push_back(&w);
        if (bars.empty())
            continue;
        const bool on = studioEnabled(*bars[0]);
        float sus = 1.0f;
        for (auto *b : bars)
            if (b->label == "S" || b->label == "SUSTAIN")
                sus = stNorm(*b);
        juce::Path pth;
        const float bot = box.getBottom() - 2, top = box.getY() + 14, h = bot - top;
        float x = box.getX() + 2;
        pth.startNewSubPath(x, bot);
        const float unit = (box.getWidth() - 4) / ((float)bars.size() + 0.6f);
        float lvl = 0;
        for (auto *b : bars)
        {
            const auto &L = b->label;
            if (L == "S" || L == "SUSTAIN")
            {
                x += unit * 0.6f;
                pth.lineTo(x, bot - h * sus);
                continue;
            }
            x += unit * (0.08f + 0.92f * stNorm(*b));
            if (L == "A" || L == "ATTACK")
                lvl = 1;
            else if (L == "D" || L == "DECAY")
                lvl = sus;
            else if (L == "R" || L == "RELEASE")
                lvl = 0;
            pth.lineTo(x, bot - h * lvl);
        }
        g.setColour((on ? bars[0]->col : st::dim).withAlpha(on ? 0.9f : 0.4f));
        g.strokePath(pth, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved));
    }

    if (c.touches(FRect(0, kKeysY, 1040, 744 - kKeysY)))
    {
        g.setColour(st::panel);
        g.fillRect(FRect(0, kKeysY - 2, 1040, 744 - kKeysY + 2));
        const float ww = 1040.0f / (float)studioWhiteCount();
        int wi = 0;
        for (int n = kLoKey; n <= kHiKey; ++n)
        {
            if (studioBlack(n))
                continue;
            const FRect r(ww * (float)wi, kKeysY, ww - 1, 744 - kKeysY);
            g.setColour(keysDown[n] > 0 ? st::amber : juce::Colour(0xfff1eee8));
            g.fillRect(r);
            if (n % 12 == 0)
                stText(c, FRect(r.getX(), r.getBottom() - 14, r.getWidth(), 12), "C" + juce::String(n / 12 - 1), 6.4f, true, juce::Colour(0xff7b7f8c), 0);
            ++wi;
        }
        for (int n = kLoKey; n <= kHiKey; ++n)
            if (studioBlack(n))
            {
                g.setColour(keysDown[n] > 0 ? st::amber.darker(0.2f) : juce::Colour(0xff15161b));
                g.fillRect(studioKeyRect(n));
            }
    }
    if (c.touches(stStatusR))
    {
        g.setColour(st::bg);
        g.fillRect(stStatusR);
        stText(c, stStatusR, statusText, 8.0f, false, st::dim, -1);
    }
}

void MachineEditor::Impl::studioDrawW(const Ctx &c, StW &w, int i)
{
    auto &g = *c.g;
    const bool on = studioEnabled(w), hov = stHover == i, held = stHeld == i;
    const auto col = on ? w.col : st::dim;
    const float a = on ? 1.0f : 0.45f;
    auto *k = stCtl(w);
    switch (w.k)
    {
    case StW::Button:
    {
        const bool lit = k != nullptr ? stOn(w) : (w.lit && w.lit());
        auto fillC = w.accent ? st::amber.withAlpha(hov ? 0.30f : 0.18f) : (hov ? st::panel2.brighter(0.15f) : st::panel2);
        if (lit && on)
            fillC = w.col.withAlpha(hov ? 0.42f : 0.30f);
        g.setColour(fillC);
        g.fillRoundedRectangle(w.r, 4.0f);
        g.setColour(w.accent ? st::amber : (lit && on ? w.col : st::line));
        g.drawRoundedRectangle(w.r.reduced(0.5f), 4.0f, 1.0f);
        const juce::String t = w.text ? w.text() : stLabel(w);
        const bool twoLines = k != nullptr && w.label.isEmpty() && k->t2.isNotEmpty() && w.r.getHeight() >= 30;
        if (twoLines)
        {
            stText(c, w.r.withHeight(w.r.getHeight() * 0.5f).translated(0, 2), t, 8.0f, true, on ? st::text : st::dim, 0, a);
            stText(c, w.r.withTrimmedTop(w.r.getHeight() * 0.5f).translated(0, -2), k->t2, 7.0f, false, on ? st::dim.brighter(0.3f) : st::dim, 0, a);
        }
        else
            stText(c, w.r, t, w.big ? 11.0f : 8.6f, true, w.accent ? st::amber : (on ? st::text : st::dim), 0, a);
        break;
    }
    case StW::Tab:
    {
        const bool sel = stTab == w.tabIx;
        g.setColour(sel ? st::tabOn : (hov ? st::panel2.brighter(0.1f) : st::panel2));
        g.fillRect(w.r.reduced(0.5f, 0));
        stText(c, w.r, w.label, 8.6f, true, sel ? st::ink : st::text, 0);
        break;
    }
    case StW::Seg:
    {
        const bool sel = w.lit && w.lit();
        g.setColour(sel && on ? w.col.withAlpha(0.85f) : (hov ? st::panel2.brighter(0.15f) : st::panel2));
        g.fillRoundedRectangle(w.r, 3.0f);
        stText(c, w.r, w.label, 6.4f, true, sel && on ? st::ink : (on ? st::text : st::dim), 0);
        break;
    }
    case StW::Toggle:
    {
        const bool v = stOn(w);
        const auto r = w.r;
        const bool named = k != nullptr || (w.label.isNotEmpty() && w.tab >= 2); // a named switch: its name above, ON / OFF in it
        if (named)
            stText(c, FRect(r.getX() - 30, r.getY() - 22, r.getWidth() + 60, 14), stLabel(w), 7.4f, true, on ? st::text : st::dim, 0);
        g.setColour(v && on ? w.col.withAlpha(0.9f) : st::panel2);
        g.fillRoundedRectangle(r, r.getHeight() * 0.5f);
        g.setColour(hov ? st::text : st::line.brighter(0.3f));
        g.drawRoundedRectangle(r.reduced(0.5f), r.getHeight() * 0.5f, 1.0f);
        if (named)
            stText(c, r, v ? "ON" : "OFF", 7.0f, true, v && on ? st::ink : st::dim, 0);
        else if (w.label.isNotEmpty())
            stText(c, r, w.label, 7.0f, true, v && on ? st::ink : st::dim, 0);
        else
        { // a power dot
            const float d = r.getHeight() * 0.42f;
            const auto ctr = v ? FPoint(r.getRight() - r.getHeight() * 0.5f, r.getCentreY()) : FPoint(r.getX() + r.getHeight() * 0.5f, r.getCentreY());
            g.setColour(v && on ? st::ink : st::dim);
            g.fillEllipse(ctr.x - d, ctr.y - d, 2 * d, 2 * d);
        }
        break;
    }
    case StW::Bar:
    {
        const float n = stNorm(w);
        const auto r = w.r.reduced(1.5f, 0);
        g.setColour(st::panel2);
        g.fillRect(r);
        const float top = r.getY() + 14, h = r.getBottom() - top;
        const FRect fill(r.getX(), r.getBottom() - h * n, r.getWidth(), h * n);
        g.setColour(col.withAlpha(on ? (hov || held ? 0.75f : 0.55f) : 0.2f));
        g.fillRect(fill);
        g.setColour(st::line);
        g.drawRect(r, 1.0f);
        stText(c, FRect(r.getX(), juce::jmax(top, fill.getY() - 13), r.getWidth(), 12), stValText(w), 7.0f, true, on ? st::text : st::dim, 0, a);
        stText(c, FRect(r.getX(), r.getY() + 1, r.getWidth(), 12), w.label, 6.6f, true, st::dim, 0);
        break;
    }
    case StW::Knob:
    {
        const auto r = w.r;
        stText(c, FRect(r.getX(), r.getY(), r.getWidth(), 14), stLabel(w), 7.4f, true, on ? st::text : st::dim, 0);
        const float rad = juce::jmin(r.getWidth() * 0.5f - 6, 24.0f);
        const FPoint ctr(r.getCentreX(), r.getY() + 20 + rad);
        const float a0 = juce::MathConstants<float>::pi * -0.75f, a1 = juce::MathConstants<float>::pi * 0.75f;
        auto *p = stParam(w);
        const bool bip = w.bip || (p != nullptr && p->meta.minVal < 0 && p->meta.maxVal > 0);
        const float n = stNorm(w);
        const float zero = p != nullptr && bip ? -p->meta.minVal / (p->meta.maxVal - p->meta.minVal) : 0.5f;
        const float an = a0 + (a1 - a0) * n, az = bip ? a0 + (a1 - a0) * zero : a0;
        juce::Path track, val;
        track.addCentredArc(ctr.x, ctr.y, rad, rad, 0, a0, a1, true);
        g.setColour(st::line);
        g.strokePath(track, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        val.addCentredArc(ctr.x, ctr.y, rad, rad, 0, juce::jmin(az, an), juce::jmax(az, an), true);
        g.setColour(col.withAlpha(on ? 1.0f : 0.5f));
        g.strokePath(val, juce::PathStrokeType(hov || held ? 3.8f : 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const FPoint tip = ctr + FPoint(std::sin(an), -std::cos(an)) * (rad - 1);
        g.fillEllipse(tip.x - 3, tip.y - 3, 6, 6);
        const bool lit = !w.lit || w.lit();
        stText(c, FRect(ctr.x - rad + 2, ctr.y - 8, 2 * rad - 4, 16), stValText(w), 7.6f, true, on && lit ? st::text : st::dim, 0, a);
        break;
    }
    case StW::Fader:
    {
        const auto r = w.r;
        stText(c, FRect(r.getX() - 8, r.getY() - 2, r.getWidth() + 16, 14), w.label, 6.8f, true, on ? st::text : st::dim, 0);
        const FRect tr(r.getX(), r.getY() + 14, r.getWidth(), r.getHeight() - 14);
        g.setColour(st::panel2);
        g.fillRect(tr);
        const float n = stNorm(w);
        const FRect fill(tr.getX(), tr.getBottom() - tr.getHeight() * n, tr.getWidth(), tr.getHeight() * n);
        g.setColour(col.withAlpha(on ? (hov || held ? 0.95f : 0.8f) : 0.25f));
        g.fillRect(fill);
        g.setColour(st::line);
        g.drawRect(tr, 1.0f);
        const float ty = juce::jlimit(tr.getY() + 1, tr.getBottom() - 13, fill.getY() + 2);
        const bool onFill = ty + 6 > fill.getY();
        stText(c, FRect(tr.getX() - 6, ty, tr.getWidth() + 12, 12), stValText(w), 6.8f, true, on && onFill ? st::ink : (on ? st::text : st::dim), 0, a);
        break;
    }
    case StW::Choice:
    {
        const auto r = w.r;
        g.setColour(hov ? st::panel2.brighter(0.15f) : st::panel2);
        g.fillRoundedRectangle(r, 4.0f);
        g.setColour(on ? w.col.withAlpha(0.7f) : st::line);
        g.drawRoundedRectangle(r.reduced(0.5f), 4.0f, 1.0f);
        stText(c, FRect(r.getX(), r.getY() - 16, r.getWidth(), 13), stLabel(w), 7.0f, true, on ? st::text : st::dim, -1);
        stText(c, r.withTrimmedRight(14).withTrimmedLeft(4), stValText(w), 8.0f, true, on ? st::text : st::dim, -1, a);
        juce::Path tri;
        const float cx = r.getRight() - 9, cy = r.getCentreY();
        tri.addTriangle(cx - 4, cy - 2, cx + 4, cy - 2, cx, cy + 3);
        g.setColour(on ? st::text : st::dim);
        g.fillPath(tri);
        break;
    }
    case StW::Cell:
    {
        if (w.cell < 0 || w.cell >= (int)backCells.size())
            break;
        auto &bc = backCells[(size_t)w.cell];
        const bool act = backActive(bc), sel = stSelCell == w.cell;
        const auto gc = ghostColour(bc.from);
        const auto r = w.r;
        g.setColour(st::panel2);
        g.fillRoundedRectangle(r, 5.0f);
        if (act)
        {
            auto *bp = backParam(bc);
            const float amt = juce::jlimit(0.f, 1.f, std::abs(bp->value) / juce::jmax(1e-4f, std::abs(bp->meta.maxVal)));
            g.setColour(gc.withAlpha(0.22f + 0.55f * amt));
            g.fillRoundedRectangle(r, 5.0f);
            stText(c, r, juce::String(backShown(bc)), 11.0f, true, st::text, 0);
        }
        else
            stText(c, r, "+", 13.0f, false, st::dim, 0, hov ? 1.0f : 0.6f);
        if (bc.kind == 1)
            stText(c, FRect(r.getX(), r.getBottom() - 12, r.getWidth(), 10), "LOOP", 5.4f, true, act ? st::ink : st::dim, 0);
        g.setColour(sel ? st::tabOn : (hov ? gc : st::line));
        g.drawRoundedRectangle(r.reduced(0.5f), 5.0f, sel ? 2.2f : 1.0f);
        break;
    }
    case StW::XY:
    {
        const auto r = w.r;
        g.setColour(st::panel2);
        g.fillRoundedRectangle(r, 6.0f);
        g.setColour(st::line);
        for (int q = 1; q < 4; ++q)
        {
            g.drawLine(r.getX() + r.getWidth() * (float)q / 4.0f, r.getY(), r.getX() + r.getWidth() * (float)q / 4.0f, r.getBottom(), 1.0f);
            g.drawLine(r.getX(), r.getY() + r.getHeight() * (float)q / 4.0f, r.getRight(), r.getY() + r.getHeight() * (float)q / 4.0f, 1.0f);
        }
        if (!ed.morph)
            break;
        static const char *L[Morph::N] = {"A", "B", "C", "D"};
        float wt[Morph::N]{};
        const bool hasW = ed.morph->weights(wt);
        for (int q = 0; q < Morph::N; ++q)
        {
            const bool filled = ed.morph->kind[(size_t)q].load() != Morph::EMPTY;
            const FPoint cp(q % 2 == 0 ? r.getX() + 18 : r.getRight() - 18, q / 2 == 0 ? r.getY() + 18 : r.getBottom() - 18);
            g.setColour(filled ? cornerColour(q) : st::line);
            g.fillEllipse(cp.x - 13, cp.y - 13, 26, 26);
            stText(c, FRect(cp.x - 13, cp.y - 9, 26, 18), L[q], 10.0f, true, filled ? st::ink : st::dim, 0);
            if (filled && hasW)
                stText(c, FRect(cp.x - 30, q / 2 == 0 ? cp.y + 14 : cp.y - 30, 60, 14), juce::String(juce::roundToInt(wt[q] * 100)) + " %", 7.4f, true, st::text, 0);
        }
        const float px = ed.morph->effX.load(), py = ed.morph->effY.load();
        const FPoint pp(r.getX() + r.getWidth() * px, r.getY() + r.getHeight() * py);
        const bool mOn = ed.morph->on.load();
        g.setColour(st::amber.withAlpha(mOn ? 0.25f : 0.1f));
        g.fillEllipse(pp.x - 18, pp.y - 18, 36, 36);
        g.setColour(mOn ? st::amber : st::dim);
        g.fillEllipse(pp.x - 9, pp.y - 9, 18, 18);
        if (!mOn)
            stText(c, FRect(r.getX(), r.getBottom() - 40, r.getWidth(), 14), "MORPH is off: turn it on to hear the blend", 7.4f, false, st::dim, 0);
        break;
    }
    }
}

// ============================================================================================== the keyboard
bool MachineEditor::Impl::studioBlack(int n)
{
    const int k = n % 12;
    return k == 1 || k == 3 || k == 6 || k == 8 || k == 10;
}
int MachineEditor::Impl::studioWhiteCount() const
{
    int w = 0;
    for (int n = kLoKey; n <= kHiKey; ++n)
        w += studioBlack(n) ? 0 : 1;
    return w;
}
FRect MachineEditor::Impl::studioKeyRect(int note) const
{
    const float ww = 1040.0f / (float)studioWhiteCount();
    int wi = 0;
    for (int n = kLoKey; n < note; ++n)
        wi += studioBlack(n) ? 0 : 1;
    if (!studioBlack(note))
        return {ww * (float)wi, (float)kKeysY, ww, 744.0f - (float)kKeysY};
    const float bw = ww * 0.62f;
    return {ww * (float)wi - bw * 0.5f, (float)kKeysY, bw, (744.0f - (float)kKeysY) * 0.6f};
}
int MachineEditor::Impl::studioKeyAt(FPoint p) const
{
    if (p.y < (float)kKeysY)
        return -1;
    for (int n = kLoKey; n <= kHiKey; ++n)
        if (studioBlack(n) && studioKeyRect(n).contains(p))
            return n;
    for (int n = kLoKey; n <= kHiKey; ++n)
        if (!studioBlack(n) && studioKeyRect(n).contains(p))
            return n;
    return -1;
}

// ============================================================================================== the pointer
int MachineEditor::Impl::studioAt(FPoint p) const
{
    for (int i = (int)stW.size() - 1; i >= 0; --i)
    {
        auto &w = stW[(size_t)i];
        if (stVisible(w) && w.r.contains(p))
            return i;
    }
    return -1;
}

void MachineEditor::Impl::studioHoverAt(FPoint p)
{
    const int h = studioAt(p);
    if (h == stHover)
        return;
    stHover = h;
    if (h < 0)
        return;
    auto &w = stW[(size_t)h];
    juce::String s = w.tip.isNotEmpty() ? w.tip : stLabel(w);
    if (w.k == StW::Cell && w.cell >= 0 && w.cell < (int)backCells.size())
    {
        auto &k = backCells[(size_t)w.cell];
        s = backWhat(k) + (backActive(k) ? ": " + juce::String(backShown(k)) + "   -   drag up / down for how much, right-click to unplug" : juce::String("   -   click to plug it in"));
    }
    else if (w.k == StW::Knob || w.k == StW::Fader || w.k == StW::Bar || w.k == StW::Choice)
    {
        s += ": " + stValText(w);
        if (!studioEnabled(w))
            s += "   (greyed: it does nothing right now)";
        else if (w.k != StW::Choice)
            s += "   -   drag up / down (SHIFT: fine), double-click: back to its default";
    }
    ed.status(s);
}

void MachineEditor::Impl::studioEditTo(StW &w, float v)
{
    auto *p = stParam(w);
    if (p == nullptr)
        return;
    v = juce::jlimit(p->meta.minVal, p->meta.maxVal, v);
    if (p->meta.type != PMD::FLOAT)
        v = std::round(v);
    if (v != p->value)
    {
        ed.setValue(*p, v);
        if (w.onEdit)
            w.onEdit();
        ed.status((w.tip.isNotEmpty() ? w.tip : w.label) + ": " + compact(valueText(*p, v)));
    }
}

void MachineEditor::Impl::studioMenu(int i)
{
    auto &w = stW[(size_t)i];
    juce::PopupMenu m;
    m.setLookAndFeel(&brass(*this));
    m.addSectionHeader(stLabel(w));
    auto sp = juce::Component::SafePointer<MachineEditor>(&ed);
    if (auto *c = stCtl(w))
    { // the machine's own dial: its own list
        const int n = selectorCount(*c), cur = selectorIndex(*c);
        for (int v = 0; v < n && v < 200; ++v)
            m.addItem(selectorText(*c, v), true, v == cur, [sp, i, v] {
                if (sp == nullptr || i >= (int)sp->impl->stW.size())
                    return;
                auto &I = *sp->impl;
                if (auto *k = I.stCtl(I.stW[(size_t)i]))
                    I.stepSelector(*k, v);
                I.ed.uiDirty = true;
            });
    }
    else if (auto *p = stParam(w))
    {
        const int lo = (int)std::lround(p->meta.minVal), hi = (int)std::lround(p->meta.maxVal);
        for (int v = lo; v <= hi && v - lo < 200; ++v)
        {
            const auto t = valueText(*p, (float)v);
            if (t.isEmpty())
                continue;
            m.addItem(t, true, (int)std::lround(p->value) == v, [sp, i, v] {
                if (sp == nullptr || i >= (int)sp->impl->stW.size())
                    return;
                auto &I = *sp->impl;
                if (auto *pp = I.stParam(I.stW[(size_t)i]))
                    I.ed.setOnce(*pp, (float)v);
                I.ed.uiDirty = true;
            });
        }
    }
    else
        return;
    const auto r = (w.r * (float)ed.viewScale()).toNearestInt();
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&ed).withTargetScreenArea(ed.localAreaToGlobal(r)).withStandardItemHeight(22));
}

bool MachineEditor::Impl::studioDown(const juce::MouseEvent &e, FPoint p)
{
    if (!studioOpen || backOpen)
        return false;
    if (ledger.mode != Ledger::Closed)
        return false; // a list is open over the studio: it answers (and a press outside closes it)
    const int note = studioKeyAt(p);
    if (note >= 0)
    {
        ed.grabKeyboardFocus();
        const auto kr = studioKeyRect(note);
        noteOn(note, juce::jlimit(0.3f, 1.0f, 0.3f + 0.7f * (p.y - kr.getY()) / juce::jmax(1.0f, kr.getHeight())));
        stKey = note;
        drag.move = [this](FPoint q, const juce::MouseEvent &) {
            const int n = studioKeyAt(q);
            if (n >= 0 && n != stKey)
            {
                if (stKey >= 0)
                    noteOff(stKey);
                noteOn(n, 0.8f);
                stKey = n;
            }
        };
        drag.up = [this](FPoint, const juce::MouseEvent &) {
            if (stKey >= 0)
                noteOff(stKey);
            stKey = -1;
        };
        drag.active = true;
        return true;
    }
    const int i = studioAt(p);
    if (i < 0)
        return true;
    auto &w = stW[(size_t)i];

    // ---- the wiring grid
    if (w.k == StW::Cell)
    {
        if (w.cell < 0 || w.cell >= (int)backCells.size())
            return true;
        const int ci = w.cell;
        auto &k = backCells[(size_t)ci];
        stSelCell = ci;
        if (k.kind == 0)
            selLink = posOf(k.from, k.to); // the link's own controls follow
        refresh();
        markAllDirty();
        if (e.mods.isPopupMenu())
        {
            if (backActive(k))
                backUnplug(k);
            return true;
        }
        if (!backActive(k))
            backPlug(k);
        auto *prm = backParam(k);
        const float lo = prm->meta.minVal, hi = prm->meta.maxVal, v0 = prm->value, y0 = p.y;
        ed.beginEdit(*prm);
        stHeld = i;
        drag.move = [this, ci, lo, hi, v0, y0](FPoint q, const juce::MouseEvent &ev) {
            if (ci >= (int)backCells.size())
                return;
            auto *pp = backParam(backCells[(size_t)ci]);
            const float per = ev.mods.isShiftDown() ? 600.0f : 150.0f;
            const float v = juce::jlimit(lo, hi, v0 + (y0 - q.y) / per * (hi - lo));
            if (v != pp->value)
            {
                ed.setValue(*pp, v);
                ed.status(backWhat(backCells[(size_t)ci]) + ": " + juce::String(backShown(backCells[(size_t)ci])));
            }
        };
        drag.up = [this, ci](FPoint, const juce::MouseEvent &) {
            stHeld = -1;
            if (ci < (int)backCells.size())
                ed.endEdit(*backParam(backCells[(size_t)ci]));
            ed.uiDirty = true;
        };
        drag.active = true;
        return true;
    }
    // ---- the blend pad
    if (w.k == StW::XY)
    {
        if (!ed.morph)
            return true;
        const auto pad = w.r;
        auto setp = [this, pad](FPoint q, int type) {
            ed.morphEdit(Morph::pX, juce::jlimit(0.f, 1.f, (q.x - pad.getX()) / pad.getWidth()), type);
            ed.morphEdit(Morph::pY, juce::jlimit(0.f, 1.f, (q.y - pad.getY()) / pad.getHeight()), type);
        };
        setp(p, 0);
        setp(p, 1);
        stHeld = i;
        drag.move = [setp](FPoint q, const juce::MouseEvent &) { setp(q, 1); };
        drag.up = [this, setp](FPoint q, const juce::MouseEvent &) {
            setp(q, 2);
            stHeld = -1;
            ed.markStateDirty();
            float wt[Morph::N]{};
            if (ed.morph->weights(wt))
            {
                static const char *L[Morph::N] = {"A", "B", "C", "D"};
                juce::String s = "Blend:";
                for (int k = 0; k < Morph::N; ++k)
                    if (ed.morph->kind[(size_t)k].load() != Morph::EMPTY)
                        s << "  " << L[k] << " " << juce::roundToInt(wt[k] * 100) << "%";
                ed.status(s);
            }
            ed.uiDirty = true;
        };
        drag.active = true;
        return true;
    }

    // ---- the machine's own controls, drawn flat here
    if (auto *c = stCtl(w))
    {
        if (!studioEnabled(w) && w.k != StW::Button)
        {
            ed.status(stLabel(w) + " is greyed: it does nothing right now.");
            return true;
        }
        if (w.k == StW::Choice)
        {
            studioMenu(i);
            return true;
        }
        if (e.mods.isPopupMenu())
        {
            if (c->type == CType::Wheel)
                typeIn(*c);
            return true;
        }
        if (w.k == StW::Knob)
            stHeld = i;
        pressCtl(*c, e, p);
        if (w.k == StW::Knob && drag.active)
        { // (the machine's drag, plus the studio's own "held" light)
            auto up = drag.up;
            drag.up = [this, up](FPoint q, const juce::MouseEvent &ev) {
                if (up)
                    up(q, ev);
                stHeld = -1;
            };
        }
        ed.uiDirty = true;
        return true;
    }

    // ---- the studio's own
    if (e.mods.isPopupMenu())
    {
        if (w.k == StW::Choice && studioEnabled(w))
            studioMenu(i);
        return true;
    }
    switch (w.k)
    {
    case StW::Button:
    case StW::Tab:
    case StW::Seg:
        if (w.act && studioEnabled(w))
            w.act();
        return true;
    case StW::Toggle:
        if (auto *pp = stParam(w); pp != nullptr && studioEnabled(w))
        {
            ed.setOnce(*pp, pp->value > 0.5f ? 0.f : 1.f);
            ed.status((w.tip.isNotEmpty() ? w.tip : w.label) + ": " + (pp->value > 0.5f ? "ON" : "OFF"));
            ed.uiDirty = true;
        }
        return true;
    case StW::Choice:
        if (studioEnabled(w))
            studioMenu(i);
        return true;
    default:
        break;
    }
    auto *prm = stParam(w);
    if (prm == nullptr || !studioEnabled(w))
    {
        if (prm != nullptr)
            ed.status((w.tip.isNotEmpty() ? w.tip : w.label) + " is greyed: it does nothing right now.");
        return true;
    }
    if (e.getNumberOfClicks() >= 2)
    {
        ed.setOnce(*prm, prm->meta.defaultVal);
        if (w.onEdit)
            w.onEdit();
        ed.status((w.tip.isNotEmpty() ? w.tip : w.label) + ": back to " + compact(valueText(*prm, prm->value)));
        ed.uiDirty = true;
        return true;
    }
    const float y0 = p.y, lo = prm->meta.minVal, hi = prm->meta.maxVal;
    ed.beginEdit(*prm);
    stHeld = i;
    const float span = w.k == StW::Knob ? 160.0f : juce::jmax(60.0f, w.r.getHeight() - 14.0f);
    if (w.k != StW::Knob)
    { // a bar or a fader jumps to where it was pressed
        const float top = w.r.getY() + 14, h = w.r.getBottom() - top;
        studioEditTo(w, lo + (hi - lo) * juce::jlimit(0.f, 1.f, (w.r.getBottom() - p.y) / juce::jmax(1.0f, h)));
    }
    const float vStart = prm->value;
    Param *held = prm;
    drag.move = [this, i, vStart, y0, lo, hi, span](FPoint q, const juce::MouseEvent &ev) {
        if (i >= (int)stW.size())
            return;
        const float per = ev.mods.isShiftDown() ? span * 4.0f : span;
        studioEditTo(stW[(size_t)i], vStart + (y0 - q.y) / per * (hi - lo));
    };
    drag.up = [this, held](FPoint, const juce::MouseEvent &) {
        stHeld = -1;
        ed.endEdit(*held);
        ed.uiDirty = true;
    };
    drag.active = true;
    return true;
}

bool MachineEditor::Impl::studioWheel(FPoint p, float dy)
{
    if (!studioOpen || backOpen || ledger.mode != Ledger::Closed)
        return false;
    const int i = studioAt(p);
    if (i < 0 || dy == 0.f)
        return true;
    auto &w = stW[(size_t)i];
    if (!studioEnabled(w) || w.k == StW::Toggle || w.k == StW::Button)
        return true;
    if (auto *c = stCtl(w))
    {
        if (w.k == StW::Choice)
            stepSelector(*c, juce::jlimit(0, selectorCount(*c) - 1, selectorIndex(*c) + (dy > 0 ? 1 : -1)));
        else if (w.k == StW::Knob)
        {
            const float v = juce::jlimit(0.f, 1.f, getCtl01(*c) + (dy > 0 ? 0.02f : -0.02f));
            setCtl01(*c, 0, 0, false);
            setCtl01(*c, v, 1, false);
            setCtl01(*c, 0, 2, false);
        }
        ed.uiDirty = true;
        return true;
    }
    if (w.k == StW::Cell && w.cell >= 0 && w.cell < (int)backCells.size())
    {
        auto &k = backCells[(size_t)w.cell];
        if (backActive(k))
        {
            auto *prm = backParam(k);
            ed.setOnce(*prm, juce::jlimit(prm->meta.minVal, prm->meta.maxVal, prm->value + (dy > 0 ? 0.02f : -0.02f) * (prm->meta.maxVal - prm->meta.minVal)));
            ed.uiDirty = true;
        }
        return true;
    }
    auto *prm = stParam(w);
    if (prm == nullptr)
        return true;
    const float range = prm->meta.maxVal - prm->meta.minVal;
    const float step = prm->meta.type == PMD::FLOAT ? range * 0.02f : 1.0f;
    float v = juce::jlimit(prm->meta.minVal, prm->meta.maxVal, prm->value + (dy > 0 ? step : -step));
    if (prm->meta.type != PMD::FLOAT)
        v = std::round(v);
    ed.setOnce(*prm, v);
    if (w.onEdit)
        w.onEdit();
    ed.status((w.tip.isNotEmpty() ? w.tip : w.label) + ": " + compact(valueText(*prm, v)));
    ed.uiDirty = true;
    return true;
}

// ============================================================================================== the way in, on the machine
bool MachineEditor::Impl::studioButtonAt(FPoint p) const
{
    if (studioOpen || backOpen || subOpen)
        return false;
    const auto r = studioDoor();
    return !r.isEmpty() && r.contains(p);
}

// the window / lantern / sun under the pointer glows warm (the way in is part of the picture, not a button)
void MachineEditor::Impl::drawStudioButton(const Ctx &c)
{
    if (studioOpen || backOpen || c.g == nullptr || !stButtonHover)
        return;
    const auto r = studioDoor();
    if (r.isEmpty() || !c.touches(r.expanded(16)))
        return;
    auto &g = *c.g;
    const auto ctr = r.getCentre();
    juce::ColourGradient cg(juce::Colour(0xc0ffd27a), ctr, juce::Colour(0x00ffa040), ctr + FPoint(r.getWidth() * 0.75f + 8, 0), true);
    cg.addColour(0.5, juce::Colour(0x60ffb050));
    g.setGradientFill(cg);
    g.fillEllipse(r.expanded(12));
}
} // namespace ghostblock::p2::machine
