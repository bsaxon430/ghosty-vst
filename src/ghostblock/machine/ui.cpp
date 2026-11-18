/*
 * Poltergeist 2 (GhostBlock) - the ghost-machine editor: the controls.
 *
 * Every entry of ui-layout.json becomes a part of the machine and is bound to what the first Poltergeist 2 editor
 * bound it to: a parameter of the engine's own patch (edited through beginEdit / setValue / endEdit), one of MORPH's
 * own settings (morphEdit / the corner atomics), or an action of the editor. The behaviour is the mock-up's
 * (src/rt_ui.js); the values and their words are the parameters' own.
 */
#include "ghostblock/machine/machine-impl.h"
#include "synth/macro_usage.h"
#include "synth/matrix_index.h"

namespace ghostblock::p2::machine
{
using MD = sst::basic_blocks::params::ParamMetaData;

static float norm(const Param &p)
{
    const float lo = p.meta.minVal, hi = p.meta.maxVal;
    return hi > lo ? juce::jlimit(0.0f, 1.0f, (p.value - lo) / (hi - lo)) : 0.0f;
}
static bool stepped(const Param &p) { return p.meta.type == MD::INT || p.meta.type == MD::BOOL; }
static juce::String toStr(const std::optional<std::string> &s) { return s ? juce::String(juce::CharPointer_UTF8(s->c_str())) : juce::String(); }

int MachineEditor::Impl::posOf(int f, int t) { return (int)ss::MatrixIndex::positionForSourceTarget((size_t)f, (size_t)t); }
bool MachineEditor::Impl::linked(int f, int t) const { return f < t && P.matrixNodes[(size_t)posOf(f, t)].active.value > 0.5f; }
bool MachineEditor::Impl::awake(int o) const { return P.sourceNodes[(size_t)o].active.value > 0.5f; }
void MachineEditor::Impl::wake(int o, bool mixer, bool self)
{ // touching OUT / FEEDBACK wakes what is needed so you hear it at once (as the first editor)
    auto &s = P.sourceNodes[(size_t)o];
    if (s.active.value < 0.5f)
        ed.setOnce(s.active, 1.f);
    if (mixer && P.mixerNodes[(size_t)o].active.value < 0.5f)
        ed.setOnce(P.mixerNodes[(size_t)o].active, 1.f);
    if (self && P.selfNodes[(size_t)o].active.value < 0.5f)
        ed.setOnce(P.selfNodes[(size_t)o].active, 1.f);
    ed.uiDirty = true;
}

Ctl *MachineEditor::Impl::find(const juce::String &id)
{
    auto it = ctlIx.find(id);
    return it == ctlIx.end() ? nullptr : &ctls[(size_t)it->second];
}
const Ctl *MachineEditor::Impl::find(const juce::String &id) const
{
    auto it = ctlIx.find(id);
    return it == ctlIx.end() ? nullptr : &ctls[(size_t)it->second];
}

bool MachineEditor::Impl::visible(const Ctl &c) const
{
    if (c.page == kSubPage)
        return subOpen;
    if (c.page >= 0 && c.page != page)
        return false;
    if (c.face != 0 && c.face != face())
        return false;
    return c.role != R_Ledger;
}

// ============================================================================================== building
void MachineEditor::Impl::buildControls()
{
    // keep what the pointer is doing across a rebuild (the screen's scale changed)
    ctls.clear();
    ctlIx.clear();
    const auto &L = as->layout;
    auto &o = P.output;
    auto &ft = P.fineTuneMod;
    const std::map<juce::String, Param *> fixed = {
        {"dr-rate", &ft.lfoRate},        {"dr-amount", &ft.lfoCoarseDepth}, {"dr-shape", &ft.lfoShape},      {"dr-sync", &ft.tempoSync},
        {"pd-amount", &ft.envCoarseDepth}, {"pd-decay", &ft.decay},          {"m-tune", &o.fineTune},         {"m-bendup", &o.bendUp},
        {"m-benddn", &o.bendDown},       {"tone-sat", &o.saturationType},   {"tone-drive", &o.saturationDrive}, {"tone-lp", &o.lowpass},
        {"tone-hp", &o.highpass},        {"m-bitrate", &o.bitRateAdjust},   {"m-bits", &o.bitDepthAdjust},   {"m-vel", &o.velSensitivity},
        {"v-master", &o.level},          {"m-gain", &o.outputGain},         {"m-pan", &o.pan},               {"v-mono", &o.playMode},
        {"m-voices", &o.polyLimit},      {"v-glide", &o.portaTime},         {"m-glidefrom", &o.portaContMode}, {"v-octave", &o.octTranspose},
        {"v-unison", &o.unisonCount},    {"m-spread", &o.unisonSpread},     {"m-stereo", &o.unisonPan}};
    const std::map<juce::String, int> roles = {
        {"bench-title", R_BenchTitle}, {"bench-lamp", R_BenchLamp}, {"g-wave", R_GWave},     {"g-awake", R_GAwake},   {"g-ratio", R_GRatio},
        {"g-octave", R_GOctave},       {"g-out", R_GOut},           {"g-pan", R_GPan},       {"g-fb", R_GFb},         {"g-envamt", R_GEnvAmt},
        {"g-env", R_GEnv},             {"g-envon", R_GEnvOn},       {"g-scope", R_GScope},   {"l-mode", R_LMode},     {"l-linked", R_LLinked},
        {"l-depth", R_LDepth},         {"l-lfo", R_LLfo},           {"l-envamt", R_LEnvAmt}, {"l-view", R_LView},     {"l-env", R_LEnv},
        {"l-break", R_LBreak},         {"l-envon", R_LEnvOn},       {"l-drive", R_LDrive},   {"m-env", R_MEnv},       {"morph-puck", R_Puck},
        {"morph-on", R_MorphOn},       {"o-size", R_OSize},         {"morph-orbit", R_Orbit}, {"morph-rate", R_Rate}, {"o-spectrum", R_Spectrum},
        {"pick-name", R_PickName},     {"pick-prev", R_Prev},       {"pick-next", R_Next},   {"pick-folder", R_Folder}, {"save", R_Save},
        {"load", R_Load},              {"init", R_Init},            {"title", R_Title},      {"out-gauge", R_OutGauge}, {"voices", R_Voices},
        {"status", R_Status},          {"kbd", R_Keys},             {"theme", R_Theme},      {"roll-chain", R_Calm},  {"ledger", R_Ledger},
        {"spyglass", R_Spyglass},      {"size-grip", R_Grip}};
    juce::StringArray pgs("common");
    pgs.addArray(as->pageKeys);
    for (int pi = 0; pi <= pgs.size(); ++pi) // (the last round: the SUB panel's parts, from common/sub/sub.json)
    {
        auto *arr = pi < pgs.size() ? L["pages"][juce::Identifier(pgs[pi])]["controls"].getArray() : ed.sub ? as->subJson["controls"].getArray() : nullptr;
        if (arr == nullptr)
            continue;
        for (auto &v : *arr)
        {
            // The brass roll is a NEW feature the plugin does not have (no sequencer): its grid and its two buttons are left out.
            if (v["state"].toString() == "roll")
                continue;
            Ctl c;
            c.id = v["id"].toString();
            c.page = pi < pgs.size() ? pi - 1 : kSubPage;
            c.name = v["name"].toString();
            c.rect = rectOf(v["rect"]);
            c.hit = v.hasProperty("hit") ? rectOf(v["hit"]) : c.rect;
            c.hasCentre = v.hasProperty("centre");
            c.centre = c.hasCentre ? pointOf(v["centre"]) : c.rect.getCentre();
            c.pivot = v.hasProperty("pivot") ? pointOf(v["pivot"]) : c.centre;
            const auto fc = v["face"].toString();
            c.face = fc == "ghost" ? 1 : fc == "link" ? 2 : 0;
            c.op = v.hasProperty("op") ? (int)v["op"] : -1;
            c.corner = v.hasProperty("corner") ? (int)v["corner"] : -1;
            c.index = v.hasProperty("index") ? (int)v["index"] : -1;
            c.spriteName = v["sprite"].toString();
            c.cls = v["text"].toString();
            c.group = v["group"].toString();
            c.artR = numOf(v["art_r"], 0);
            if (v.hasProperty("label"))
            {
                c.label = rectOf(v["label"]);
                c.hasLabel = true;
            }
            if (v.hasProperty("lamp"))
            {
                c.lamp = rectOf(v["lamp"]);
                c.hasLamp = true;
            }
            const auto type = v["type"].toString();
            const float r = c.rect.getWidth() * 0.5f;
            if (type == "wheel")
            {
                c.type = CType::Wheel;
                c.interactive = true;
                const float ar = c.artR > 0 ? c.artR : r;
                if (c.spriteName.isEmpty())
                    c.spriteName = "tap-a";
                c.sprScale = c.spriteName == "valve-s" ? 1.0f : c.spriteName.startsWith("macro") ? ar / 23.0f : ar / 21.5f;
                c.glowSize = ar * 3.1f;
                c.two = c.hasLabel && c.label.getHeight() >= 20;
            }
            else if (type == "selector" && c.hasCentre)
            {
                c.type = CType::Selector;
                c.interactive = true;
                c.spriteName = "sel-knob";
                c.sprScale = r / 17.0f;
                c.glowSize = r * 3.1f;
                c.two = true;
            }
            else if (type == "selector")
            {
                c.type = CType::Drum;
                c.interactive = true;
                c.label = {c.rect.getX(), c.rect.getY() + 3, c.rect.getWidth() - 8, c.rect.getHeight() - 6};
                c.hasLabel = true;
                c.lcls = lcDrum;
            }
            else if (type == "switch")
            {
                c.type = CType::Switch;
                c.interactive = true;
                c.sprScale = c.id == "morph-on" ? 1.2f : 1.0f;
                c.glowSize = 9.0f * c.sprScale * 3.1f;
            }
            else if (type == "plunger")
            {
                c.type = CType::Plunger;
                c.interactive = true;
                if (c.hasLabel)
                    c.glowSize = 10.0f * 3.1f;
                else
                {
                    c.label = {c.rect.getX() + 3, c.rect.getY() + 3, c.rect.getWidth() - 6, c.rect.getHeight() - 9};
                    c.hasLabel = true;
                    c.spriteName.clear(); // a painted plate: the text is the button
                }
                c.lcls = lcBtn;
            }
            else if (type == "lever")
            {
                c.type = CType::Lever;
                c.interactive = true;
                c.sprScale = c.id == "roll-chain" ? 0.8f : 1.0f;
            }
            else if (type == "sign")
            {
                c.type = CType::Sign;
                c.interactive = true;
                c.label = {c.rect.getX(), c.rect.getY() + 1, c.rect.getWidth(), c.rect.getHeight() - 2};
                c.hasLabel = true;
                c.lcls = lcSign;
            }
            else if (type == "gauge")
                c.type = CType::Gauge;
            else if (type == "plate")
            {
                c.type = CType::Plate;
                c.interactive = v.hasProperty("action");
                if (c.cls.isNotEmpty())
                {
                    c.label = c.rect;
                    c.hasLabel = true;
                    c.lcls = c.id == "pick-name" ? lcName : c.id == "status" ? lcStatus : c.id == "bench-title" ? lcBTitle : c.cls == "num" ? lcNum : c.cls == "state" ? lcState : c.cls == "sound" ? lcSnd : c.cls == "fixed" ? lcTag : lcBase;
                }
            }
            else if (type == "chamber")
            {
                c.type = CType::Chamber;
                c.interactive = true;
            }
            else if (type == "socket")
            {
                c.type = CType::Socket;
                c.interactive = true;
                c.glowSize = 9.0f * 3.1f;
            }
            else if (type == "lantern")
            {
                c.type = CType::Lantern;
                c.interactive = true;
                c.sprScale = c.rect.getWidth() * 0.5f / 18.0f;
                c.glowSize = 20.0f * 3.1f;
                c.lcls = lcWt;
            }
            else if (type == "lens")
            {
                c.type = CType::Lens;
                c.interactive = true;
            }
            else if (type == "pin")
            {
                c.type = CType::Pin;
                c.interactive = true;
            }
            else if (type == "key")
            {
                c.type = CType::Keys;
                c.interactive = true;
            }
            else if (type == "spyglass")
            {
                c.type = CType::Spyglass;
                c.interactive = true;
                c.centre = v.hasProperty("lens") ? pointOf(v["lens"]) : c.rect.getCentre(); // the glass: where its light sits
                c.glowSize = 30.0f;
            }
            else if (type == "grip")
            {
                c.type = CType::Grip;
                c.interactive = true;
            }
            // what it is for
            if (auto it = roles.find(c.id); it != roles.end())
                c.role = it->second;
            else if (auto f = fixed.find(c.id); f != fixed.end())
            {
                c.role = R_Fixed;
                c.fixed = f->second;
            }
            else if (c.id.startsWith("macro"))
            {
                c.role = R_Macro;
                c.index = c.id.getTrailingIntValue() - 1;
                c.fixed = &P.macroNodes[(size_t)juce::jlimit(0, (int)ss::numMacros - 1, c.index)].level;
            }
            else if (c.id.startsWith("ch") && c.op >= 0)
            {
                const auto tail = c.id.fromFirstOccurrenceOf("-", false, false);
                c.role = tail.isEmpty() ? R_Chamber : tail == "mix" ? R_ChMix : tail == "out" ? R_SockOut : tail == "in" ? R_SockIn : tail == "num" ? R_ChNum : tail == "vu" ? R_ChVu : R_ChTag;
                if (c.role == R_ChMix)
                    c.fixed = &P.mixerNodes[(size_t)c.op].level;
            }
            else if (c.id.startsWith("g-tab-"))
                c.role = R_GTab;
            else if (c.id.startsWith("asm-"))
                c.role = R_Asm;
            else if (c.id.startsWith("page-"))
            {
                c.role = R_Page;
                c.index = as->pageKeys.indexOf(c.id.fromFirstOccurrenceOf("page-", false, false)); // the page this sign shows
                if (c.index < 0)
                    c.interactive = false; // a sign for a page whose art is not in the pack yet
            }
            else if (c.id.startsWith("morph-") && c.type == CType::Lantern)
                c.role = R_Lantern;
            else if (c.id.startsWith("o-") && c.corner >= 0)
            {
                c.role = c.id.endsWith("-sound") ? R_CornerSound : c.id.endsWith("-vol") ? R_CornerVol : c.id.endsWith("-oct") ? R_CornerOct : R_CornerPan;
                if (c.role == R_CornerOct)
                    c.two = true; // OCT over the number
            }
            if (c.id == "sub-sign")
            {
                c.role = R_Sub;
                c.interactive = ed.sub != nullptr;
            }
            if (c.role == R_Calm)
                c.name = "CALM"; // the pull-chain: the roll it was drawn for is not built, so it holds the "calm" setting
            if (c.role == R_Ledger || c.role == R_Title)
                c.interactive = false;
            ctlIx[c.id] = (int)ctls.size();
            ctls.push_back(std::move(c));
        }
    }
    fxBuildControls();
    subBuildControls();
    resolveOverlaps();
    for (auto &c : ctls)
    {
        c.area = ctlArea(c);
        c.cells = cellsOf(c.area);
    }
    // the parts of a page that are not controls
    auto cellsFor = [&](const char *id, float grow) {
        auto *c = find(id);
        return c ? cellsOf(c->rect.expanded(grow)) : std::vector<uint16_t>();
    };
    envCells[0] = cellsFor("g-env", 10);
    envCells[1] = envCells[0];
    envCells[2] = cellsFor("m-env", 10);
    scopeCells = cellsFor("g-scope", 3);
    specCells = cellsFor("o-spectrum", 3);
    linkViewCells = cellsFor("l-view", 3);
    statusCells = cellsFor("status", 4);
    {
        const auto gl = rectOf(L["pages"]["morph"]["regions"]["glass"]);
        morphCells = cellsOf(gl.expanded(30));
    }
}

// Where a small part sits on a big one (a mix valve on its chamber's pipe, a socket beside the next chamber) the big
// one gives way, so no two parts ever claim the same point.
void MachineEditor::Impl::resolveOverlaps()
{
    auto together = [](const Ctl &a, const Ctl &b) {
        if (a.page >= 0 && b.page >= 0 && a.page != b.page)
            return false;
        return a.face == 0 || b.face == 0 || a.face == b.face;
    };
    for (int pass = 0; pass < 6; ++pass)
        for (size_t i = 0; i < ctls.size(); ++i)
            for (size_t j = i + 1; j < ctls.size(); ++j)
            {
                auto &a = ctls[i], &b = ctls[j];
                if (!a.interactive || !b.interactive || !together(a, b) || a.type == CType::Lens || b.type == CType::Lens)
                    continue;
                const auto ov = a.hit.getIntersection(b.hit);
                if (ov.getWidth() <= 0.01f || ov.getHeight() <= 0.01f)
                    continue;
                auto &big = a.hit.getWidth() * a.hit.getHeight() >= b.hit.getWidth() * b.hit.getHeight() ? a : b;
                auto &h = big.hit;
                // cut the strip that costs the least and keeps it 34 px
                struct Cut
                {
                    float cost;
                    FRect r;
                };
                std::vector<Cut> cuts;
                if (ov.getX() <= h.getX() + 0.01f)
                    cuts.push_back({ov.getWidth() * h.getHeight(), h.withLeft(ov.getRight())});
                if (ov.getRight() >= h.getRight() - 0.01f)
                    cuts.push_back({ov.getWidth() * h.getHeight(), h.withRight(ov.getX())});
                if (ov.getY() <= h.getY() + 0.01f)
                    cuts.push_back({ov.getHeight() * h.getWidth(), h.withTop(ov.getBottom())});
                if (ov.getBottom() >= h.getBottom() - 0.01f)
                    cuts.push_back({ov.getHeight() * h.getWidth(), h.withBottom(ov.getY())});
                std::sort(cuts.begin(), cuts.end(), [](const Cut &x, const Cut &y) { return x.cost < y.cost; });
                bool cut = false;
                for (auto &ct : cuts)
                    if (ct.r.getWidth() >= 34.0f && ct.r.getHeight() >= 34.0f)
                    {
                        h = ct.r;
                        cut = true;
                        break;
                    }
                if (!cut)
                { // two parts that are both at the 34 px floor (hose sockets a pixel or two into each other): they keep
                  // their size and step apart by the overlap, half each
                    if (ov.getWidth() < ov.getHeight())
                    {
                        const float d = ov.getWidth() * 0.5f * (a.hit.getCentreX() < b.hit.getCentreX() ? -1.0f : 1.0f);
                        a.hit.translate(d, 0);
                        b.hit.translate(-d, 0);
                    }
                    else
                    {
                        const float d = ov.getHeight() * 0.5f * (a.hit.getCentreY() < b.hit.getCentreY() ? -1.0f : 1.0f);
                        a.hit.translate(0, d);
                        b.hit.translate(0, -d);
                    }
                }
            }
}

FRect MachineEditor::Impl::ctlArea(const Ctl &k) const
{
    FRect a = k.rect.expanded(3);
    if (k.hasLabel)
        a = a.getUnion(k.label.expanded(7, 6));
    if (k.hasLamp)
        a = a.getUnion(k.lamp.expanded(10));
    auto round = [&](FPoint c, float half) { a = a.getUnion(FRect(c.x - half, c.y - half, half * 2, half * 2)); };
    switch (k.type)
    {
    case CType::Wheel:
    case CType::Selector:
        round(k.centre, juce::jmax(k.glowSize * 0.5f, k.rect.getWidth() * 0.5f * 1.45f + 6.0f));
        break;
    case CType::Switch:
        round({k.pivot.x, k.pivot.y - 14 * k.sprScale}, 44 * k.sprScale);
        break;
    case CType::Plunger:
        if (k.glowSize > 0)
            round(k.rect.getCentre(), k.glowSize * 0.5f + 2);
        break;
    case CType::Lever:
        if (k.role == R_Calm)
            a = a.getUnion(FRect(k.pivot.x - 16, k.pivot.y - 8, 32, 112));
        else
            round({k.pivot.x, k.pivot.y + 16}, 34);
        break;
    case CType::Sign:
        a = a.getUnion(FRect(k.rect.getX() - 8, k.rect.getY() - 20, k.rect.getWidth() + 16, 60));
        break;
    case CType::Gauge:
        round(k.pivot, k.rect.getWidth() * 0.5f + 4);
        break;
    case CType::Socket:
        round(k.centre, 30); // (a legal socket's light swells while a hose is dragged)
        break;
    case CType::Lantern:
        round(k.centre, juce::jmax(k.glowSize * 0.5f, 30.0f));
        break;
    case CType::Lens:
        a = rectOf(as->layout["pages"]["morph"]["regions"]["glass"]).expanded(30);
        break;
    case CType::Pin:
        a = k.rect.expanded(12);
        break;
    case CType::Spyglass:
        round(k.centre, k.glowSize * 0.5f + 2);
        break;
    default:
        break;
    }
    return a.getIntersection({0, 0, (float)CW, (float)CH});
}

// ============================================================================================== what a control edits
Param *MachineEditor::Impl::paramOf(const Ctl &c)
{
    if (c.fixed != nullptr)
        return c.fixed;
    const size_t o = (size_t)juce::jlimit(0, (int)ss::numOps - 1, sel);
    auto &s = P.sourceNodes[o];
    auto &mx = P.mixerNodes[o];
    auto &sf = P.selfNodes[o];
    auto &m = P.matrixNodes[(size_t)juce::jlimit(0, (int)ss::matrixSize - 1, selLink)];
    switch (c.role)
    {
    case R_GWave:
        return &s.waveForm;
    case R_GAwake:
        return &s.active;
    case R_GRatio:
        return &s.ratio;
    case R_GOctave:
        return &s.octTranspose;
    case R_GOut:
        return &mx.level;
    case R_GPan:
        return &mx.pan;
    case R_GFb:
        return &sf.fbLevel;
    case R_GEnvAmt:
        return envTab == 0 ? &mx.envToLevel : envTab == 1 ? &sf.envToFB : &s.envToRatio;
    case R_GEnvOn:
        return envPower(0);
    case R_LMode:
        return &m.modulationMode;
    case R_LLinked:
        return &m.active;
    case R_LDepth:
        return &m.level;
    case R_LLfo:
        return &m.lfoToDepth;
    case R_LEnvAmt:
        return &m.envToLevel;
    case R_LEnvOn:
        return envPower(1);
    case R_LDrive:
        return &m.overdrive;
    default:
        return nullptr;
    }
}

Param *MachineEditor::Impl::envParam(int which, int k)
{
    auto pick = [k](auto &n) -> Param * {
        Param *e[6] = {&n.delay, &n.attack, &n.hold, &n.decay, &n.sustain, &n.release};
        return e[juce::jlimit(0, 5, k)];
    };
    const size_t o = (size_t)juce::jlimit(0, (int)ss::numOps - 1, sel);
    if (which == 2)
        return pick(P.output);
    if (which == 1)
        return pick(P.matrixNodes[(size_t)juce::jlimit(0, (int)ss::matrixSize - 1, selLink)]);
    return envTab == 0 ? pick(P.mixerNodes[o]) : envTab == 1 ? pick(P.selfNodes[o]) : pick(P.sourceNodes[o]);
}
Param *MachineEditor::Impl::envPower(int which)
{
    const size_t o = (size_t)juce::jlimit(0, (int)ss::numOps - 1, sel);
    if (which == 2)
        return &P.output.envPower;
    if (which == 1)
        return &P.matrixNodes[(size_t)juce::jlimit(0, (int)ss::matrixSize - 1, selLink)].envPower;
    return envTab == 0 ? &P.mixerNodes[o].envPower : envTab == 1 ? &P.selfNodes[o].envPower : &P.sourceNodes[o].envPower;
}

// the parameter's own words, tidied for a small plate: "100.00 %" -> "100 %", "1.0000 x" -> "1 x", "1/2.0000 x" -> "1/2 x"
juce::String MachineEditor::Impl::compact(juce::String s) const
{
    juce::String out;
    auto p = s.getCharPointer();
    while (!p.isEmpty())
    {
        if (juce::CharacterFunctions::isDigit(*p))
        {
            juce::String num;
            int dot = -1;
            while (juce::CharacterFunctions::isDigit(*p) || (*p == '.' && dot < 0 && juce::CharacterFunctions::isDigit(*(p + 1))))
            {
                if (*p == '.')
                    dot = num.length();
                num << juce::String::charToString(p.getAndAdvance());
            }
            if (dot >= 0)
            {
                const double v = num.getDoubleValue();
                const int have = num.length() - dot - 1;
                num = juce::String(v, (have >= 4 && v < 10.0) ? 3 : juce::jmin(2, have));
                while (num.contains(".") && (num.endsWithChar('0') || num.endsWithChar('.')))
                    num = num.dropLastCharacters(1);
            }
            out << num;
        }
        else
            out << juce::String::charToString(p.getAndAdvance());
    }
    return out.trim();
}
juce::String MachineEditor::Impl::valueText(Param &p, float v) const
{
    auto s = toStr(p.meta.valueToString(v));
    return s.isEmpty() ? juce::String(v, 2) : compact(s);
}

static float selAngle(int n, int i)
{
    const float span = juce::jmin(130.0f, 26.0f * (float)(n - 1));
    return n > 1 ? -span + 2.0f * span * (float)i / (float)(n - 1) : 0.0f;
}
static juce::String panWords(float v) { return std::abs(v) < 0.01f ? juce::String("C") : (v < 0 ? "L" : "R") + juce::String(juce::roundToInt(std::abs(v) * 100)); }

// ============================================================================================== reading the patch
void MachineEditor::Impl::touch(Ctl &c)
{
    if (visible(c))
        sceneDirty.mark(c.cells);
}

void MachineEditor::Impl::refreshCtl(Ctl &c)
{
    if (c.fx != 0 && fxRefresh(c))
        return;
    float v01 = c.v01, ang = c.angle;
    int step = c.step, steps = c.steps;
    bool on = c.on, off = c.labOff, lit = c.lit;
    juce::String t1 = c.t1, t2 = c.t2;
    juce::Colour col = c.colour;
    auto *p = paramOf(c);
    switch (c.type)
    {
    case CType::Wheel:
        t1 = c.name;
        if (p != nullptr)
        {
            v01 = norm(*p);
            t2 = valueText(*p, p->value);
        }
        if (c.role == R_Macro)
        {
            juce::String nm(P.macroNames[(size_t)c.index].data());
            t1 = nm.trim().isEmpty() ? "MACRO " + juce::String(c.index + 1) : nm.trim().toUpperCase();
        }
        else if (c.role == R_GEnvAmt)
            t1 = envTab == 0 ? "ENV > OUT" : envTab == 1 ? "ENV > FB" : "ENV > PITCH";
        else if (c.role == R_OSize && ed.morph)
        {
            v01 = ed.morph->orbitSize.load();
            t2 = juce::String(juce::roundToInt(v01 * 100)) + "%";
        }
        else if (c.role == R_CornerVol && ed.morph)
        {
            const float v = ed.morph->volDb[(size_t)c.corner].load();
            v01 = juce::jlimit(0.f, 1.f, (v + 24.0f) / 30.0f);
            t2 = juce::String(v, 1) + " dB";
        }
        else if (c.role == R_CornerPan && ed.morph)
        {
            const float v = ed.morph->pan[(size_t)c.corner].load();
            v01 = juce::jlimit(0.f, 1.f, (v + 1.0f) * 0.5f);
            t2 = panWords(v);
        }
        ang = -135.0f + 270.0f * v01;
        break;
    case CType::Selector:
    case CType::Drum:
        t1 = c.name;
        if (p != nullptr)
        {
            steps = (int)std::lround(p->meta.maxVal - p->meta.minVal) + 1;
            step = juce::jlimit(0, steps - 1, (int)std::lround(p->value - p->meta.minVal));
            t2 = valueText(*p, p->value);
        }
        else if (c.role == R_Theme)
        {
            static const char *nm[3] = {"STORM", "DUSK", "CLEAR"};
            steps = 3;
            step = juce::jlimit(0, 2, env.theme);
            t1 = "SKY";
            t2 = nm[step];
        }
        else if (c.role == R_Orbit && ed.morph)
        {
            steps = Morph::O_COUNT;
            step = juce::jlimit(0, steps - 1, ed.morph->orbit.load());
            t2 = Morph::orbitNames[step];
        }
        else if (c.role == R_Rate && ed.morph)
        {
            steps = Morph::kRates;
            step = juce::jlimit(0, steps - 1, ed.morph->orbitRate.load());
            t2 = Morph::rateNames[step];
        }
        else if (c.role == R_Folder)
            t2 = currentFolder();
        if (c.type == CType::Drum)
        {
            t1 = t2;
            t2.clear();
        }
        else
            ang = selAngle(steps, step);
        break;
    case CType::Switch:
        t1 = c.name;
        on = p != nullptr ? p->value > 0.5f : (c.role == R_MorphOn && ed.morph ? ed.morph->on.load() : false);
        ang = on ? 0.0f : 52.0f;
        break;
    case CType::Plunger:
        t1 = c.name;
        on = c.role == R_GTab && envTab == c.index;
        break;
    case CType::Lever:
        t1 = c.name;
        on = c.role == R_Calm && env.calm;
        break;
    case CType::Sign:
        t1 = c.name;
        on = c.role == R_Sub ? (ed.sub != nullptr && ed.sub->on()) : c.index == page;
        break;
    case CType::Gauge:
        t1 = c.name;
        break;
    case CType::Plate:
        switch (c.role)
        {
        case R_PickName:
            t1 = juce::String(juce::CharPointer_UTF8(P.name)) + (P.dirty ? " *" : "");
            break;
        case R_Voices:
            t1 = "VOICES " + juce::String(ed.voiceCount);
            break;
        case R_Status:
            t1 = statusText;
            break;
        case R_BenchTitle:
        {
            const int li = selLink;
            if (li >= 0)
            {
                const int f = (int)ss::MatrixIndex::sourceIndexAt((size_t)li), t = (int)ss::MatrixIndex::targetIndexAt((size_t)li);
                t1 = "GHOST " + juce::String(f + 1) + " > GHOST " + juce::String(t + 1);
                col = ghostColour(f);
            }
            else
            {
                t1 = "GHOST " + juce::String(sel + 1);
                col = ghostColour(sel);
            }
            panelTitle = t1;
            break;
        }
        case R_ChNum:
            t1 = juce::String(c.op + 1);
            col = ghostColour(c.op);
            break;
        case R_ChTag:
        {
            const bool aw = awake(c.op);
            auto &mx = P.mixerNodes[(size_t)c.op];
            const bool you = aw && mx.active.value > 0.5f && mx.level.value > 0.01f;
            bool haunts = false;
            for (int t = c.op + 1; t < (int)ss::numOps; ++t)
                haunts = haunts || (linked(c.op, t) && P.matrixNodes[(size_t)posOf(c.op, t)].level.value > 0.001f);
            t1 = !aw ? "asleep" : you ? "you hear it" : haunts ? "haunts others" : "silent";
            on = you;
            off = !aw;
            break;
        }
        case R_Asm:
            t1 = c.name;
            break;
        case R_CornerOct:
            if (ed.morph)
            {
                const int o = ed.morph->octave[(size_t)c.corner].load();
                t1 = "OCT";
                t2 = o > 0 ? "+" + juce::String(o) : juce::String(o);
                off = ed.morph->kind[(size_t)c.corner].load() == Morph::EMPTY; // greyed: nothing to move yet
                lit = !off && o != 0;
            }
            break;
        case R_CornerSound:
            if (ed.morph)
            {
                const int k = ed.morph->kind[(size_t)c.corner].load();
                t1 = k == Morph::EMPTY ? juce::String("+ pick a sound") : k == Morph::THIS_SOUND ? juce::String("THIS SOUND") : juce::String(juce::CharPointer_UTF8(ed.morph->names[(size_t)c.corner].c_str()));
                off = k == Morph::EMPTY;
                lit = k != Morph::EMPTY;
            }
            break;
        default:
            break;
        }
        break;
    default:
        break;
    }
    if (v01 != c.v01 || std::abs(ang - c.angle) > 0.05f || step != c.step || steps != c.steps || on != c.on || off != c.labOff || lit != c.lit || t1 != c.t1 || t2 != c.t2 || col != c.colour)
    {
        c.v01 = v01;
        c.angle = ang;
        c.step = step;
        c.steps = steps;
        c.on = on;
        c.labOff = off;
        c.lit = lit;
        c.t1 = t1;
        c.t2 = t2;
        c.colour = col;
        touch(c);
        if (c.role == R_Status)
            sceneDirty.mark(statusCells);
    }
}

void MachineEditor::Impl::refreshMacros()
{
    const auto usage = ss::computeMacroUsage(P);
    for (int m = 0; m < (int)ss::numMacros; ++m)
    {
        const auto &u = usage[(size_t)m];
        juce::String h;
        if (u.empty())
            h = "not wired in this sound";
        else
        {
            h = juce::String(juce::CharPointer_UTF8(u[0].nodeLabel.c_str())) + ": " + juce::String(juce::CharPointer_UTF8(u[0].targetName.c_str()));
            if (u.size() > 1)
                h << " (+" << (int)u.size() - 1 << " more)";
        }
        macroHint[m] = h;
    }
}

// the weather follows the patch: deep links, feedback and drive bring the storm (JUCE-NOTES.md, section 6)
void MachineEditor::Impl::refreshWeather()
{
    float load = 0;
    for (int f = 0; f < (int)ss::numOps; ++f)
        for (int t = f + 1; t < (int)ss::numOps; ++t)
            if (linked(f, t) && awake(f))
                load += juce::jlimit(0.f, 1.f, P.matrixNodes[(size_t)posOf(f, t)].level.value);
    for (int o = 0; o < (int)ss::numOps; ++o)
        if (P.selfNodes[(size_t)o].active.value > 0.5f)
            load += 1.5f * std::abs(P.selfNodes[(size_t)o].fbLevel.value);
    const float drive = juce::jmax(0.0f, (P.output.saturationDrive.value - 1.0f) * 0.5f);
    load += 0.8f * drive;
    // the SKY dial leans on it: CLEAR weather takes heavier FM to break, DUSK a little more than STORM
    const float lean = env.theme == 2 ? 0.55f : env.theme == 1 ? 0.85f : 1.0f;
    const float st = juce::jlimit(0.f, 1.f, (load - 2.2f) / 3.2f) * lean;
    if (st - env.stormBase > 0.22f && !env.calm)
        strike(); // a jump: one strike at once
    env.stormBase = st;
    env.drive = drive;
    env.driftRate = juce::jlimit(0.f, 1.f, (P.fineTuneMod.lfoRate.value + 7.0f) / 18.0f);
    env.driftAmt = P.fineTuneMod.lfoCoarseDepth.value;
    env.pdAmt = P.fineTuneMod.envCoarseDepth.value;
    env.pdDecay = 0.05f + 2.5f * P.fineTuneMod.decay.value;
    env.master = P.output.level.value;
    for (int o = 0; o < (int)ss::numOps; ++o)
    {
        env.ops[o].awake = awake(o);
        auto &mx = P.mixerNodes[(size_t)o];
        env.ops[o].out = awake(o) && mx.active.value > 0.5f ? mx.level.value : 0.f;
    }
    env.sel = sel;
    env.selLink = selLink;
}

void MachineEditor::Impl::refreshEnv()
{
    static const char *ids[3] = {"g-env", "l-env", "m-env"};
    for (int w = 0; w < 3; ++w)
    {
        auto *c = find(ids[w]);
        if (c == nullptr || !visible(*c))
            continue;
        float v[6];
        for (int k = 0; k < 6; ++k)
            v[k] = juce::jlimit(0.f, 1.f, envParam(w, k)->value);
        const auto rc = c->rect;
        float wd[6] = {v[0] > 0.001f ? v[0] + 0.04f : 0.f, v[1] + 0.04f, v[2] > 0.001f ? v[2] + 0.04f : 0.f, v[3] + 0.04f, 0.5f, v[5] + 0.04f};
        const float x0 = rc.getX() + 10, span = rc.getWidth() - 20, yb = rc.getBottom() - 14, ht = rc.getHeight() - 28;
        const float mn[6] = {22, 12, 22, 12, 12, 12};
        for (int pass = 0; pass < 3; ++pass)
        {
            float tot = 0;
            for (float q : wd)
                tot += q;
            const float sc = span / tot;
            for (int k = 0; k < 6; ++k)
                if (wd[k] > 0 && wd[k] * sc < mn[k])
                    wd[k] = mn[k] / sc;
        }
        float tot = 0;
        for (float q : wd)
            tot += q;
        const float sc = span / tot;
        EnvGeom g;
        g.xs[0] = x0;
        for (int k = 0; k < 6; ++k)
            g.xs[k + 1] = g.xs[k] + wd[k] * sc;
        const float lv[7] = {0, 0, 1, 1, v[4], v[4], 0};
        for (int k = 0; k < 7; ++k)
            g.ys[k] = yb - lv[k] * ht;
        g.yb = yb;
        g.ht = ht;
        // five pins: delay, attack, hold, decay + sustain, release
        g.pin[0] = {g.xs[1], g.ys[1]};
        g.pin[1] = {g.xs[2], g.ys[2]};
        g.pin[2] = {g.xs[3], g.ys[3]};
        g.pin[3] = {g.xs[4], g.ys[4]};
        g.pin[4] = {g.xs[6], g.ys[6]};
        if (v[2] <= 0.001f)
            g.pin[2] = {g.pin[1].x + 16, g.pin[1].y};
        if (v[0] <= 0.001f)
            g.pin[0] = {g.xs[0], g.ys[0]};
        g.on = envPower(w)->value > 0.5f;
        auto &old = envGeom[w];
        bool same = g.on == old.on;
        for (int k = 0; k < 7 && same; ++k)
            same = std::abs(g.xs[k] - old.xs[k]) < 0.01f && std::abs(g.ys[k] - old.ys[k]) < 0.01f;
        if (!same)
        {
            old = g;
            sceneDirty.mark(envCells[w]);
        }
    }
}

void MachineEditor::Impl::refreshMorph()
{
    if (!ed.morph)
        return;
    auto &m = *ed.morph;
    float w[Morph::N]{};
    cornerAny = m.weights(w);
    const bool on = m.on.load();
    env.morphOn = on;
    env.orbit = on && m.orbit.load() != Morph::O_OFF;
    for (int c = 0; c < Morph::N; ++c)
    {
        const int k = m.kind[(size_t)c].load();
        const float wc = cornerAny && k != Morph::EMPTY ? w[c] : 0.f;
        if (std::abs(wc - cornerW[c]) > 0.002f)
        {
            cornerW[c] = wc;
            if (page == 2)
                sceneDirty.mark(morphCells);
        }
        env.corner[c] = wc;
        static const char *L = "ABCD";
        if (auto *lt = find(juce::String("morph-") + juce::String::charToString((juce::juce_wchar)L[c])))
        {
            const auto t1 = juce::String::charToString((juce::juce_wchar)L[c]) + (k != Morph::EMPTY && cornerAny ? " " + juce::String(juce::roundToInt(wc * 100)) + "%" : juce::String());
            const float lit = k != Morph::EMPTY ? (on && cornerAny ? 0.35f + 0.65f * juce::jmin(1.0f, wc * 1.6f) : 0.3f) : 0.f;
            if (t1 != lt->t1 || std::abs(lit - lt->v01) > 0.02f || lt->colour != cornerColour(c))
            {
                lt->t1 = t1;
                lt->v01 = lit;
                lt->colour = cornerColour(c);
                touch(*lt);
            }
        }
    }
    const FPoint puck(m.effX.load(), m.effY.load());
    if (puck.getDistanceFrom(puckDrawn) > 0.0015f)
    {
        puckDrawn = puck;
        if (page == 2)
            sceneDirty.mark(morphCells);
    }
}

void MachineEditor::Impl::refresh()
{
    refreshWeather();
    for (auto &c : ctls)
        refreshCtl(c);
    refreshEnv();
    refreshMorph();
    refreshMacros();
    rebuildHoses();
}

// ============================================================================================== the status slit
void MachineEditor::status(const juce::String &s)
{
    if (!impl)
        return;
    impl->statusText = s;
    impl->statusAt = impl->clock;
    if (impl->backOpen)
        impl->sceneDirty.mark(impl->backHintCells);
    else if (impl->studioOpen)
        impl->sceneDirty.mark(impl->stStatusR.expanded(2));
    if (auto *c = impl->find("status"))
    {
        c->flashAt = impl->clock;
        impl->refreshCtl(*c);
        impl->sceneDirty.mark(impl->statusCells);
        if (impl->as)
        {
            TextStyle st;
            st.size = 8.8f;
            st.semi = false;
            st.track = 0.01f;
            impl->statusOver = juce::jmax(0.0f, impl->as->textWidth(s, st) - (c->rect.getWidth() - 14.0f));
        }
    }
}

// ============================================================================================== presets
int MachineEditor::Impl::factoryIndex() const
{
    const auto &v = pm->factoryPatchVector;
    const auto nm = std::string(P.name);
    for (int i = 0; i < (int)v.size(); ++i)
        if (ss::presets::PresetManager::stripPresetExtension(v[(size_t)i].second) == nm)
            return i;
    return -1;
}
juce::String MachineEditor::Impl::currentFolder() const
{
    const int i = factoryIndex();
    if (i >= 0)
        return juce::String(pm->factoryPatchVector[(size_t)i].first);
    return std::string(P.name) == "Init" ? "INIT" : "YOUR SOUNDS";
}
void MachineEditor::Impl::loadFactory(int i)
{
    const auto &v = pm->factoryPatchVector;
    if (v.empty())
        return;
    i = (i % (int)v.size() + (int)v.size()) % (int)v.size();
    pm->loadFactoryPreset(P, ed.mainToAudio, v[(size_t)i].first, v[(size_t)i].second);
}
void MachineEditor::Impl::stepPreset(int d)
{
    const int i = factoryIndex();
    loadFactory(i < 0 ? (d > 0 ? 0 : -1) : i + d);
}
// the folder drum: the 13 sound folders you browse by ear, one click = the next folder's first sound
void MachineEditor::Impl::nextFolder(int d)
{
    std::vector<std::string> folders;
    for (auto &[cat, names] : pm->factoryPatchNames)
        if (!names.empty() && cat != "MPE" && cat != "MTS-ESP-MPE" && cat != "Templates")
            folders.push_back(cat);
    if (folders.empty())
        return;
    const auto &v = pm->factoryPatchVector;
    const int cur = factoryIndex();
    int fi = -1;
    if (cur >= 0)
        for (int k = 0; k < (int)folders.size(); ++k)
            if (folders[(size_t)k] == v[(size_t)cur].first)
                fi = k;
    const int n = (int)folders.size();
    fi = fi < 0 ? (d > 0 ? 0 : n - 1) : ((fi + d) % n + n) % n;
    for (int i = 0; i < (int)v.size(); ++i)
        if (v[(size_t)i].first == folders[(size_t)fi])
        {
            loadFactory(i);
            return;
        }
}

void MachineEditor::Impl::setCornerSound(int c, const LedgerItem &it)
{
    if (!ed.morph)
        return;
    auto &m = *ed.morph;
    switch (it.kind)
    {
    case LedgerItem::This:
        m.setThisSound(c);
        break;
    case LedgerItem::Copy:
        m.setOwn(c, P.toState(), std::string(P.name) + " (copy)");
        break;
    case LedgerItem::Factory:
    case LedgerItem::User:
    {
        auto *e = m.engineFor(c);
        if (it.kind == LedgerItem::Factory)
            cornerPm->loadFactoryPreset(e->patchMain, e->mainToAudio, it.cat, it.file);
        else
            cornerPm->loadUserPresetDirect(e->patchMain, e->mainToAudio, cornerPm->userPatchesPath / it.user);
        e->reconcileWavetables();
        m.names[(size_t)c] = std::string(e->patchMain.name);
        m.kind[(size_t)c] = Morph::OWN;
        m.changed++;
        break;
    }
    default:
        m.clear(c);
        break;
    }
    afterCorner(c);
}
void MachineEditor::Impl::afterCorner(int c)
{
    static const char *L[Morph::N] = {"A", "B", "C", "D"};
    ed.markStateDirty();
    const auto nm = juce::String(juce::CharPointer_UTF8(ed.morph->names[(size_t)c].c_str()));
    ed.status(ed.morph->kind[(size_t)c].load() == Morph::EMPTY ? "Corner " + juce::String(L[c]) + " is empty."
                                                              : "Corner " + juce::String(L[c]) + ": " + nm + (ed.morph->on.load() ? "" : "  (turn MORPH ON to hear the blend)"));
    ed.uiDirty = true;
    flash(juce::String("morph-") + L[c]);
}

// ============================================================================================== hoses: the haunt links
// A hose runs from a ghost's OUT socket to the IN socket of a ghost below it (a higher number): the engine's matrix
// is lower triangular. A loop from a ghost's OUT to its own IN is its feedback.
namespace
{
struct Chamber
{
    FPoint centre, out, inn;
};
} // namespace
static Chamber chamberOf(const Assets &as, int i)
{
    const auto &c = as.layout["pages"]["ghosts"]["chambers"][i];
    return {pointOf(c["centre"]), pointOf(c["out"]), pointOf(c["inn"])};
}
static FPoint unit(FPoint from, FPoint to)
{
    const auto d = to - from;
    const float l = juce::jmax(1e-4f, d.getDistanceFromOrigin());
    return d / l;
}
static void hoseGeom(FPoint g[4], FPoint a0, FPoint dA, FPoint b0, FPoint dB, float k = -1, float sag = -1)
{
    const float dist = a0.getDistanceFrom(b0);
    if (k < 0)
        k = juce::jmin(46.0f, dist * 0.42f + 8.0f); // how far it runs straight out of the socket
    if (sag < 0)
        sag = 10.0f + dist * 0.16f + juce::jmax(0.0f, 96.0f - dist) * 0.5f; // short hoses hang in a loop, long ones belly
    g[0] = a0;
    g[1] = a0 + dA * k + FPoint(0, sag);
    g[2] = b0 + dB * k + FPoint(0, sag);
    g[3] = b0;
}
static FPoint bez(const FPoint g[4], float t)
{
    const float m = 1 - t;
    return g[0] * (m * m * m) + g[1] * (3 * m * m * t) + g[2] * (3 * m * t * t) + g[3] * (t * t * t);
}

float MachineEditor::Impl::hoseDepth(const Hose &h) const
{
    return h.fb ? std::abs(P.selfNodes[(size_t)h.src].fbLevel.value) : P.matrixNodes[(size_t)posOf(h.src, h.dst)].level.value;
}
Param *MachineEditor::Impl::hoseParam(const Hose &h) { return h.fb ? &P.selfNodes[(size_t)h.src].fbLevel : &P.matrixNodes[(size_t)posOf(h.src, h.dst)].level; }

void MachineEditor::Impl::hoseCells(Hose &h)
{
    h.body = Sprite(); // its shape changed: the cached painting of it is stale
    CellSet s;
    for (int i = 0; i <= 14; ++i)
    {
        const auto q = bez(h.cur, (float)i / 14.0f);
        s.mark(FRect(q.x - 13, q.y - 11, 26, 26));
    }
    s.mark(FRect(h.cur[0].x - 20, h.cur[0].y - 20, 40, 40));
    s.mark(FRect(h.cur[3].x - 20, h.cur[3].y - 20, 40, 40));
    if (!h.dragging)
        s.mark(FRect(h.vq.x - 17, h.vq.y - 17, 34, 44));
    h.cells.clear();
    for (int i = 0; i < NCELLS; ++i)
        if (s.test(i))
            h.cells.push_back((uint16_t)i);
}

void MachineEditor::Impl::rebuildHoses()
{
    if (!as)
        return;
    struct Want
    {
        bool fb;
        int s, d;
    };
    std::vector<Want> want;
    for (int f = 0; f < (int)ss::numOps; ++f)
        for (int t = f + 1; t < (int)ss::numOps; ++t)
            if (linked(f, t))
                want.push_back({false, f, t});
    for (int o = 0; o < (int)ss::numOps; ++o)
        if (P.selfNodes[(size_t)o].active.value > 0.5f)
            want.push_back({true, o, o});
    bool changed = false;
    // drop the ones that are gone (not the one in the hand)
    for (size_t i = hoses.size(); i-- > 0;)
    {
        auto &h = hoses[i];
        if (h.dragging)
            continue;
        const bool keep = std::any_of(want.begin(), want.end(), [&](const Want &w) { return w.fb == h.fb && w.s == h.src && w.d == h.dst; });
        if (!keep)
        {
            if (page == 0)
                sceneDirty.mark(h.cells);
            if (dragHose > (int)i)
                --dragHose;
            hoses.erase(hoses.begin() + (long)i);
            changed = true;
        }
    }
    for (auto &w : want)
        if (!std::any_of(hoses.begin(), hoses.end(), [&](const Hose &h) { return !h.dragging && h.fb == w.fb && h.src == w.s && h.dst == w.d; }))
        {
            Hose h;
            h.fb = w.fb;
            h.src = w.s;
            h.dst = w.d;
            h.ph = rng.nextFloat() * 6.28f;
            hoses.push_back(h);
            changed = true;
        }
    if (changed)
        layoutHoses();
    // the valves follow their depth
    for (auto &h : hoses)
    {
        const float d = hoseDepth(h);
        if (std::abs(d - h.depthDrawn) > 0.0005f)
        {
            h.depthDrawn = d;
            if (page == 0)
                sceneDirty.mark(h.cells);
        }
    }
}

void MachineEditor::Impl::layoutHoses()
{
    // hoses that share a socket fan out: each leaves about 30 degrees from its neighbour
    std::vector<int> atOut[ss::numOps], atIn[ss::numOps];
    for (int i = 0; i < (int)hoses.size(); ++i)
    {
        auto &h = hoses[(size_t)i];
        if (h.dragging || h.fb)
            continue;
        atOut[h.src].push_back(i);
        atIn[h.dst].push_back(i);
    }
    auto rot = [](FPoint d, float a) { return FPoint(d.x * std::cos(a) - d.y * std::sin(a), d.x * std::sin(a) + d.y * std::cos(a)); };
    std::vector<FPoint> placed;
    Chamber ch[ss::numOps];
    for (int i = 0; i < (int)ss::numOps; ++i)
        ch[i] = chamberOf(*as, i);
    for (int i = 0; i < (int)hoses.size(); ++i)
    {
        auto &h = hoses[(size_t)i];
        if (h.dragging)
            continue;
        if (page == 0)
            sceneDirty.mark(h.cells);
        const auto a0 = ch[h.src].out, b0 = ch[h.dst].inn;
        const auto dA = unit(ch[h.src].centre, ch[h.src].out), dB = unit(ch[h.dst].centre, ch[h.dst].inn);
        if (h.fb)
            hoseGeom(h.g, a0, dA, b0, dB, 34.0f, 0.0f);
        else
        {
            auto &lo = atOut[h.src], &li = atIn[h.dst];
            const int io = (int)(std::find(lo.begin(), lo.end(), i) - lo.begin()), ii = (int)(std::find(li.begin(), li.end(), i) - li.begin());
            hoseGeom(h.g, a0, rot(dA, ((float)io - (float)(lo.size() - 1) * 0.5f) * 0.55f), b0, rot(dB, ((float)ii - (float)(li.size() - 1) * 0.5f) * 0.55f));
        }
        // the valve rides at 55 % of the hose, or the first place clear of the glasses, the sockets and the other valves
        float best = 0.55f;
        for (float t : {0.55f, 0.45f, 0.65f, 0.36f, 0.74f, 0.28f})
        {
            const auto q = bez(h.g, t);
            bool clear = true;
            for (auto &c : ch)
                clear = clear && q.getDistanceFrom(c.centre) > 46 && q.getDistanceFrom(c.out) > 26 && q.getDistanceFrom(c.inn) > 26;
            for (auto &pq : placed)
                clear = clear && q.getDistanceFrom(pq) > 30;
            if (clear)
            {
                best = t;
                break;
            }
        }
        h.vt = best;
        h.vq = bez(h.g, best);
        placed.push_back(h.vq);
        for (int k = 0; k < 4; ++k)
            h.cur[k] = h.g[k];
        hoseCells(h);
        h.drawnSway = -99;
        if (page == 0)
            sceneDirty.mark(h.cells);
    }
}

// they sway a little and carry the signal: a lit pulse runs along the hose with the source ghost's level
void MachineEditor::Impl::tickHoses(double dt)
{
    if (page != 0)
        return;
    const float A = env.calm ? 0.f : 0.8f + env.act * 2.2f + env.storm * 2.0f;
    const float t = (float)env.t;
    for (auto &h : hoses)
    {
        if (h.dragging)
            continue;
        const float lv = env.ops[h.src].level, dp = hoseDepth(h);
        h.pulse *= (float)std::exp(-dt / 0.6);
        const float al = juce::jlimit(0.f, 1.f, dp * (0.16f + 1.5f * lv) + h.pulse);
        h.dash -= (float)dt * (26.0f + 60.0f * lv + 3.5f * std::pow(2.0f, P.sourceNodes[(size_t)h.src].ratio.value));
        while (h.dash < 0)
            h.dash += 24.0f;
        FPoint c1 = h.g[1] + FPoint(A * std::sin(t * 1.3f + h.ph), 0.6f * A * std::cos(t * 1.1f + h.ph));
        FPoint c2 = h.g[2] + FPoint(A * std::sin(t * 1.5f + h.ph + 1), 0.6f * A * std::cos(t * 0.9f + h.ph + 2));
        const bool swayed = c1.getDistanceFrom(h.cur[1]) > 0.45f || c2.getDistanceFrom(h.cur[2]) > 0.45f;
        const bool ran = al > 0.03f && std::abs(h.dash - h.drawnDash) > (lv > 0.02f || h.pulse > 0.05f ? 1.2f : 3.0f);
        if (swayed || ran || std::abs(al - h.drawnAlpha) > 0.04f)
        {
            sceneDirty.mark(h.cells);
            if (swayed)
            {
                h.cur[1] = c1;
                h.cur[2] = c2;
                h.vq = bez(h.cur, h.vt);
                h.body = Sprite(); // it moved: paint it again
                hoseCells(h);
                sceneDirty.mark(h.cells);
            }
            h.drawnDash = h.dash;
            h.drawnAlpha = al;
        }
    }
    // a socket glows with the signal it is fed
    for (auto &c : ctls)
        if (c.role == R_SockIn)
        {
            float fed = 0;
            for (auto &h : hoses)
                if (!h.dragging && h.dst == c.op)
                    fed = juce::jmax(fed, hoseDepth(h) * env.ops[h.src].level * 1.6f);
            c.v01 = juce::jlimit(0.f, 1.f, fed);
        }
}

void MachineEditor::Impl::startHose(int src, FPoint at, float keepDepth)
{
    Hose h;
    h.src = h.dst = src;
    h.dragging = true;
    h.ph = 0;
    hoses.push_back(h);
    dragHose = (int)hoses.size() - 1;
    dragKeep = keepDepth;
    litSrc = src;
    for (auto &c : ctls)
        if (c.role == R_SockIn)
            touch(c);
    env.holdTarget = 1;
    moveHose(at);
}

void MachineEditor::Impl::moveHose(FPoint at)
{
    if (dragHose < 0 || dragHose >= (int)hoses.size())
        return;
    auto &h = hoses[(size_t)dragHose];
    sceneDirty.mark(h.cells);
    const auto cs = chamberOf(*as, h.src);
    const auto dA = unit(cs.centre, cs.out);
    const float d = juce::jmax(1.0f, at.getDistanceFrom(cs.out));
    const FPoint dB((cs.out.x - at.x) / d, (cs.out.y - at.y) / d);
    hoseGeom(h.g, cs.out, dA, at, dB, juce::jmin(40.0f, d * 0.35f + 4.0f));
    for (int k = 0; k < 4; ++k)
        h.cur[k] = h.g[k];
    int tgt = -1;
    for (int i = 0; i < (int)ss::numOps; ++i)
    {
        const auto c = chamberOf(*as, i);
        if (at.getDistanceFrom(c.inn) < 24 || (i != h.src && at.getDistanceFrom(c.centre) < 40))
            tgt = i;
    }
    h.target = tgt;
    h.bad = tgt >= 0 && !(tgt > h.src || tgt == h.src);
    dragPt = at;
    hoseCells(h);
    sceneDirty.mark(h.cells);
}

void MachineEditor::Impl::endHose(FPoint at)
{
    if (dragHose < 0 || dragHose >= (int)hoses.size())
        return;
    const auto h = hoses[(size_t)dragHose];
    sceneDirty.mark(h.cells);
    hoses.erase(hoses.begin() + dragHose);
    dragHose = -1;
    litSrc = -1;
    for (auto &c : ctls)
        if (c.role == R_SockIn)
            touch(c);
    env.holdTarget = 0;
    const int src = h.src, tgt = h.target;
    const float keep = dragKeep;
    dragKeep = -1;
    if (tgt == src && tgt >= 0 && at.getDistanceFrom(chamberOf(*as, src).inn) < 24)
    { // onto its own IN socket: the feedback loop
        auto &sf = P.selfNodes[(size_t)src];
        if (sf.active.value < 0.5f)
            ed.setOnce(sf.active, 1.f);
        if (std::abs(sf.fbLevel.value) < 0.01f)
            ed.setOnce(sf.fbLevel, keep >= 0 ? keep : 0.3f);
        if (!awake(src))
            ed.setOnce(P.sourceNodes[(size_t)src].active, 1.f);
        ed.status("GHOST " + juce::String(src + 1) + " feeds back into itself. Turn the valve on the loop for how much.");
        selectGhost(src, true);
    }
    else if (tgt >= 0 && tgt > src)
    { // exactly what dragging a ghost onto a ghost did in the first editor
        auto &m = P.matrixNodes[(size_t)posOf(src, tgt)];
        if (m.active.value < 0.5f)
            ed.setOnce(m.active, 1.f);
        if (keep >= 0)
            ed.setOnce(m.level, keep);
        else if (m.level.value < 0.01f)
            ed.setOnce(m.level, 0.5f);
        for (int g2 : {src, tgt})
            if (!awake(g2))
                ed.setOnce(P.sourceNodes[(size_t)g2].active, 1.f);
        selectLink(src, tgt, true);
        ed.status("GHOST " + juce::String(src + 1) + " haunts GHOST " + juce::String(tgt + 1) + ". Turn the valve on the hose for how hard.");
        env.bump(0.4f);
        const auto q = chamberOf(*as, tgt).inn;
        if (stageBuilt[0])
            stage[0].puff(q.x, q.y, pkSpark, 0.8f, 1, -1, env, rng);
    }
    else if (tgt >= 0 && tgt != src)
        ed.status("GHOST " + juce::String(src + 1) + " can only haunt the ghosts below it (a higher number). Try dragging GHOST " + juce::String(tgt + 1) + " onto GHOST " + juce::String(src + 1) + ".");
    else if (keep >= 0)
        ed.status("Link broken.");
    ed.uiDirty = true;
    refresh();
}

// ============================================================================================== selection, pages
void MachineEditor::Impl::selectGhost(int op, bool quiet)
{
    sel = juce::jlimit(0, (int)ss::numOps - 1, op);
    selLink = -1;
    if (page != 0)
        setPage(0, false);
    if (stageBuilt[0])
        stage[0].setFace(1, sceneDirty);
    sceneDirty.mark(benchCells);
    ed.uiDirty = true;
    refresh();
    if (!quiet)
        ed.status("GHOST " + juce::String(sel + 1) + ": OUT = how loud you hear it  -  RATIO = its pitch  -  drag a hose from its OUT socket to a ghost below to haunt it");
    flash("bench-title");
}

void MachineEditor::Impl::selectLink(int from, int to, bool quiet)
{
    if (from >= to)
        return;
    sel = from;
    selLink = posOf(from, to);
    if (page != 0)
        setPage(0, false);
    if (stageBuilt[0])
        stage[0].setFace(2, sceneDirty);
    sceneDirty.mark(benchCells);
    ed.uiDirty = true;
    refresh();
    if (!quiet)
        ed.status("How hard GHOST " + juce::String(from + 1) + " haunts GHOST " + juce::String(to + 1) + ", and how that moves over each note");
    flash("bench-title");
}

void MachineEditor::Impl::flash(const juce::String &id)
{
    if (auto *c = find(id))
    {
        c->flashAt = clock;
        touch(*c);
    }
}

// "find it": when a page opens every control on it flashes once, in a wave that starts at the face
void MachineEditor::Impl::ripple()
{
    for (auto &c : ctls)
    {
        if (!visible(c) || c.role == R_Status || c.role == R_Title || c.type == CType::Keys || c.type == CType::Chamber)
            continue;
        const auto ctr = c.rect.getCentre();
        c.flashAt = clock + 0.15 + 0.0016 * (double)ctr.getDistanceFrom({542.0f, 110.0f});
    }
}
} // namespace ghostblock::p2::machine
