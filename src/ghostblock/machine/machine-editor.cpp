/*
 * Poltergeist 2 (GhostBlock) - the ghost-machine editor: the clock, the engine's queues, composing and presenting.
 *
 * The engine side is the first editor's, word for word: the same queue messages (BEGIN_EDIT / SET_PARAM / END_EDIT),
 * the same idle (drain the audio thread's echoes, reconcile the wavetables), the same MORPH calls. Nothing here
 * touches the sound path.
 *
 * The picture: one opaque frame at the screen's pixel size, kept up to date cell by cell (16 x 16 design px).
 * A tick moves what moves, marks the cells that changed, composes only those (world -> page -> controls -> text)
 * and hands the same cells to the screen as tiles. A governor watches what that costs and slows the ambient motion
 * (clouds, grass, sway, smoke) before it slows anything you touch.
 */
#include "ghostblock/machine/machine-impl.h"
#include <cstdlib>
#include "ghostblock/machine/spyglass.h"
#include "ghostblock/machine/sizing.h"
#include "synth/wavetable_handoff.h"

#include <clap/ext/params.h>
#include <clap/ext/state.h>

namespace ghostblock::p2::machine
{
static double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }

static juce::String versionText()
{
#ifdef POLTERGEIST2_VERSION
    return "v1 beta";
#else
    return "v-dev";
#endif
}

// ============================================================================================== the inside
MachineEditor::Impl::Impl(MachineEditor &e) : ed(e), P(e.patch)
{
    juce::PropertiesFile::Options o;
    o.applicationName = "Poltergeist 2";
    o.folderName = "GhostBlock";
    o.filenameSuffix = ".settings";
    o.osxLibrarySubFolder = "Application Support";
    // the checks (p2-tests) keep their own settings file, so the size / spyglass / theme Brandon set in his DAW never
    // reach them (a saved 108 % opened their editor at 1122 x 803) and they never change his
    if (const char *t = std::getenv("POLTERGEIST2_SETTINGS_FILE"); t != nullptr && *t != 0)
        prefs = std::make_unique<juce::PropertiesFile>(juce::File(juce::CharPointer_UTF8(t)), o);
    else
        prefs = std::make_unique<juce::PropertiesFile>(o);
    env.theme = juce::jlimit(0, 2, prefs->getIntValue("theme", 0)); // the same key as before: 0 BLACK = STORM, 1 PURPLE = DUSK, 2 LIGHT = CLEAR
    env.calm = prefs->getBoolValue("calm", false);
    pm = std::make_unique<ss::presets::PresetManager>(ed.clapHost);
    pm->onPresetLoaded = [this](const ss::presets::PresetManager::LoadedPreset &lp) {
        ed.dawState.main.recordPreset(lp.sessionKind(), lp.category, lp.sessionPath());
        ed.uiDirty = true;
        ed.status("Loaded \"" + juce::String(juce::CharPointer_UTF8(P.name)) + "\"");
        env.bump(0.35f);
    };
    cornerPm = std::make_unique<ss::presets::PresetManager>(Morph::quietHost());
    versionLine = "POLTERGEIST 2  " + versionText() + "  -  FM SPIRIT SYNTH  -  GHOSTBLOCK";
    for (int i = 0; i < 256; ++i)
        dimLut[i] = gradeR[i] = gradeG[i] = gradeB[i] = (uint8_t)i;
    lastTick = nowMs();
}

MachineEditor::Impl::~Impl()
{
    allNotesOff();
    if (dialog != nullptr)
    { // a brass dialog still open: it must not outlive its look
        dialog->setLookAndFeel(nullptr);
        if (dialog->isCurrentlyModal())
            dialog->exitModalState(0);
    }
}

void MachineEditor::Impl::build(int scale)
{
    S = juce::jlimit(1, 2, scale);
    as = std::make_unique<Assets>(S);
    scene.alloc(CW * S, CH * S);
    worldBuf.alloc(CW * S, CH * S);
    snap = Canvas();
    front = juce::Image();
    frontStale = true;
    tileCols = (CW * S + kTile - 1) / kTile;
    tileRows = (CH * S + kTile - 1) / kTile;
    tiles.clear();
    for (int ty = 0; ty < tileRows; ++ty)
        for (int tx = 0; tx < tileCols; ++tx)
            tiles.emplace_back(juce::Image::ARGB, juce::jmin(kTile, CW * S - tx * kTile), juce::jmin(kTile, CH * S - ty * kTile), true);
    world.build(*as, "common", as->common["world"]);
    pageWorld.clear();
    pageWorld.resize((size_t)as->pageCount());
    stage.clear();
    stage.resize((size_t)as->pageCount());
    stageBuilt.assign(stage.size(), 0);
    page = juce::jlimit(0, juce::jmax(0, pageCount() - 1), page);
    bank = as->bitmap("common/world/bank.png", 0.5 * S);          // one pixel per design pixel (it is a soft cloud)
    rainSpr = Sprite::fromBitmap(as->bitmap("common/world/rain.png", 1.0)); // a 520 px tile at the screen's scale
    hoses.clear();
    dragHose = -1;
    litSrc = -1;
    drag = Drag();
    hoverCtl = hoverHoseValve = hoverPin = -1;
    buildControls();
    backBuild();
    studioBuild();
    if (!stInit)
    { // the view used last time
        stInit = true;
        studioOpen = prefs != nullptr && prefs->getValue("view") == "studio";
    }
    spyglassChanged();
    ensurePage(page);
    setTheme(env.theme, false);
    worldAll = true;
    markAllDirty();
    refresh();
    ed.status(statusText);
}

void MachineEditor::Impl::ensurePage(int pg)
{
    pg = juce::jlimit(0, juce::jmax(0, pageCount() - 1), pg);
    if (!stageBuilt[(size_t)pg] && as)
    {
        stage[(size_t)pg].build(*as, pg);
        stageBuilt[(size_t)pg] = true;
        if (as->pages[(size_t)pg]["world"].isArray())
        {
            pageWorld[(size_t)pg] = std::make_unique<World>();
            pageWorld[(size_t)pg]->build(*as, as->pageKeys[pg], as->pages[(size_t)pg]["world"]);
        }
        if (pg == 0)
        {
            CellSet tmp;
            stage[0].setFace(face(), tmp);
        }
    }
}

void MachineEditor::Impl::markAllDirty()
{
    sceneDirty.fill();
    frontDirty.fill();
    worldAll = true;
}

void MachineEditor::Impl::setTheme(int t, bool save)
{
    env.theme = juce::jlimit(0, 2, t);
    // the three looks are three lights on the same painting (JUCE-NOTES.md, section 10); the text is not graded
    auto screen = [](float v, float s) { return 1.0f - (1.0f - v) * (1.0f - s); };
    for (int i = 0; i < 256; ++i)
    {
        const float v = (float)i / 255.0f;
        float r = v, g = v, b = v;
        if (env.theme == 1)
        { // DUSK: multiply #ffc2cf, then screen #3a1466 at 22 %
            r = screen(v * 1.0f, 0x3a / 255.0f * 0.22f);
            g = screen(v * (0xc2 / 255.0f), 0x14 / 255.0f * 0.22f);
            b = screen(v * (0xcf / 255.0f), 0x66 / 255.0f * 0.22f);
        }
        else if (env.theme == 2)
        { // CLEAR: screen #fff2c8 at 14 % (and the decks at 55 %)
            r = screen(v, 1.0f * 0.14f);
            g = screen(v, 0xf2 / 255.0f * 0.14f);
            b = screen(v, 0xc8 / 255.0f * 0.14f);
        }
        gradeR[i] = (uint8_t)juce::jlimit(0, 255, (int)std::lround(r * 255.0f));
        gradeG[i] = (uint8_t)juce::jlimit(0, 255, (int)std::lround(g * 255.0f));
        gradeB[i] = (uint8_t)juce::jlimit(0, 255, (int)std::lround(b * 255.0f));
    }
    gradeOn = env.theme != 0;
    if (save && prefs)
    {
        prefs->setValue("theme", env.theme);
        prefs->saveIfNeeded();
    }
    markAllDirty();
    ed.uiDirty = true;
}

void MachineEditor::Impl::setCalm(bool on, bool save)
{
    env.calm = on;
    if (save && prefs)
    {
        prefs->setValue("calm", on);
        prefs->saveIfNeeded();
    }
    if (on)
        for (auto &st : stage)
        {
            for (auto &p : st.parts)
                if (!p.drawn.isEmpty())
                    sceneDirty.mark(p.drawn.expanded(1));
            st.parts.clear();
        }
    ed.uiDirty = true;
}

void MachineEditor::Impl::strike()
{
    if (env.calm || !as)
        return;
    curWorld().strike(clock, rng);
}

// the camera swings to another part of the machine: a cloud crosses the picture and the view changes behind it
void MachineEditor::Impl::startTurn(int)
{
    if (S <= 0 || env.calm)
        return;
    if (snap.s.data == nullptr)
        snap.alloc(CW * S, CH * S);
    for (int y = 0; y < scene.s.h; ++y)
        std::memcpy(snap.s.row(y), scene.s.row(y), (size_t)scene.s.w * 4);
    turning = true;
    turnAt = clock;
}

void MachineEditor::Impl::setPage(int pg, bool instant)
{
    pg = juce::jlimit(0, juce::jmax(0, pageCount() - 1), pg);
    if (backOpen || studioOpen)
    { // a page asked for from the back room / the studio (or by the host): out to the machine first
        backOpen = false;
        studioOpen = false;
        backHover = stHover = -1;
        markAllDirty();
    }
    if (pg == page && !instant)
        return;
    closeLedger();
    if (drag.active && drag.up)
    {
        auto up = std::move(drag.up);
        drag = Drag();
        juce::ignoreUnused(up);
    }
    pageFrom = page;
    if (!instant && pg != page)
        startTurn(pg);
    if (hoverCtl >= 0 && hoverCtl < (int)ctls.size())
        ctls[(size_t)hoverCtl].hover = false;
    hoverCtl = hoverHoseValve = hoverPin = -1;
    page = pg;
    env.page = pg;
    ensurePage(pg);
    markAllDirty();
    ed.uiDirty = true;
    refresh();
    static const char *hint[3] = {"GHOSTS: touch a chamber to shape that ghost; drag a hose from its OUT socket to a ghost below it.",
                                  "MAIN: volume envelope, glide, bend, unison, tone and output. The windmill turns at the DRIFT rate.",
                                  "MORPH: a sound in each corner lantern; drag the puck to blend the real sounds."};
    if (as->pageKeys[pg] == "fx")
        ed.status(ed.spectral ? "FX: ghost = 4 effect slots, town = WarpCore, the hose's valve = which runs first.  " + fxLatencyText() : juce::String("FX: this build has no SPECTRAL FX."));
    else
        ed.status(pg < 3 ? juce::String(hint[pg]) : as->layout["page_names"][juce::Identifier(as->pageKeys[pg])].toString());
    rippleAt = clock + (instant ? 0.5 : 0.62);
    rippleDone = false;
    env.bump(0.3f);
}

// ============================================================================================== the clock
void MachineEditor::Impl::tick(double dt)
{
    if (!as)
        return;
    const double t0 = nowMs();
    clock += dt;
    env.t = clock;
    const float fdt = (float)dt;
    // ---- the two numbers that drive the life (JUCE-NOTES.md, section 6) and what the engine tells us
    env.kick *= (float)std::exp(-dt / 0.9);
    env.stormKick *= (float)std::exp(-dt / 3.0);
    float out = 0;
    for (int i = 0; i < (int)ss::numOps; ++i)
    {
        auto &o = env.ops[i];
        o.level += (juce::jmin(1.0f, ed.vu[(size_t)i]) - o.level) * juce::jmin(1.0f, fdt * 9.0f);
        o.heard = o.level * o.out;
        out = juce::jmax(out, o.heard);
    }
    out = juce::jmax(out * 1.3f * env.master, juce::jmin(1.0f, ed.vuOut * 1.3f));
    env.out += (juce::jmin(1.0f, out) - env.out) * juce::jmin(1.0f, fdt * 8.0f);
    bool keyHeld = false;
    for (int n : keysDown)
        keyHeld = keyHeld || n > 0;
    if (ed.voiceCount > env.voices)
        env.noteOn(); // a new voice: a note came (from the host or from our keys)
    env.voices = ed.voiceCount;
    env.noteTarget = (ed.voiceCount > 0 || keyHeld) ? 1.0f : 0.0f;
    env.note += (env.noteTarget - env.note) * juce::jmin(1.0f, fdt * (env.noteTarget > env.note ? 14.0f : 4.0f));
    env.pdEnv *= (float)std::exp(-dt / juce::jmax(0.08f, env.pdDecay));
    env.act = juce::jlimit(0.0f, 1.0f, Env::IDLE + env.kick + 0.5f * env.out);
    const float storm = env.stormHold >= 0 ? env.stormHold : juce::jlimit(0.0f, 1.0f, env.stormBase + env.stormKick);
    env.storm = env.calm && env.stormHold < 0 ? juce::jmin(storm, 0.18f) : storm; // calm: it may darken a little, it never rains
    env.hold += (env.holdTarget - env.hold) * juce::jmin(1.0f, fdt * 4.0f);
    for (auto &b : env.boost)
        b *= (float)std::exp(-dt / 0.8);
    // ---- lightning: visual only, capped, never more than one sequence in 1.5 s
    if (!env.calm && env.storm > 0.12f && !backOpen && !studioOpen)
    {
        nextStrike -= dt * (env.storm - 0.12) * 0.55;
        if (nextStrike <= 0)
        {
            strike();
            nextStrike = 0.6 + rng.nextDouble() * 0.9;
        }
    }
    // ---- what the page shows of the patch that moves by itself
    if (ed.morph && (env.orbit || ed.morph->changed.load() != lastMorphChange))
        refreshMorph();
    refreshGuard(); // BLEND GUARD: says when sounds cancel
    if (!rippleDone && clock >= rippleAt)
    {
        rippleDone = true;
        ripple();
    }
    // ---- the ambient motion (sky, meadow, sway, smoke): 30 a second when something is going on, less at rest
    const bool busy = env.act > 0.24f || env.hold > 0.05f || turning || curWorld().strikeLevel(clock) > 0 || env.storm > 0.2f;
    const double every = 1.0 / (double)(busy ? juce::jmin(30, ambientHz * 2) : ambientHz);
    ambAcc += dt;
    if (ambAcc >= every - 1e-4 && (backOpen || studioOpen))
        ambAcc = 0; // in the back room the sky and the machine outside stand still (nothing of them is seen)
    if (ambAcc >= every - 1e-4)
    {
        const float adt = (float)juce::jmin(0.25, ambAcc);
        ambAcc = 0;
        CellSet wd;
        double q0 = nowMs();
        curWorld().tick(env, adt, wd);
        prof[0] += nowMs() - q0;
        if (worldAll)
        {
            wd.fill();
            worldAll = false;
        }
        const float dim = 1.0f - 0.34f * curWorld().bakedStorm;
        if (std::abs(dim - dimDrawn) > 0.001f)
        { // the painted (not the lit) layers darken with the storm
            dimDrawn = dim;
            dimOn = dim < 0.999f;
            for (int i = 0; i < 256; ++i)
                dimLut[i] = (uint8_t)juce::jlimit(0, 255, (int)std::lround((float)i * dim));
        }
        wd.remove(stage[page].covered);
        q0 = nowMs();
        if (wd.any())
        {
            curWorld().compose(worldBuf.s, wd);
            sceneDirty.add(wd);
            worldCells += wd.count();
        }
        prof[1] += nowMs() - q0;
        q0 = nowMs();
        int n0 = sceneDirty.count();
        stage[page].tick(env, adt, sceneDirty, rng, curWorld().strikeLevel(clock), !env.calm);
        prof[2] += nowMs() - q0;
        cellsBy[0] += sceneDirty.count() - n0;
        n0 = sceneDirty.count();
        q0 = nowMs();
        tickHoses(adt);
        prof[3] += nowMs() - q0;
        cellsBy[1] += sceneDirty.count() - n0;
    }
    const double q1 = nowMs();
    const int n1 = sceneDirty.count();
    fxTick(dt);
    subTick(dt);
    backTick(dt);
    studioTick(dt);
    tickLights(dt);
    updateScope();
    updateSpectrum();
    cellsBy[2] += sceneDirty.count() - n1;
    prof[4] += nowMs() - q1;
    // ---- the ledger unrolls from its roller (320 ms)
    if (ledger.mode != Ledger::Closed)
    {
        const float u = (float)juce::jlimit(0.0, 1.0, (clock - ledger.openedAt) / 0.32);
        ledger.reveal = env.calm ? 1.0f : 1.0f - (1.0f - u) * (1.0f - u);
        if (std::abs(ledger.reveal - ledger.revealDrawn) > 0.004f)
        {
            ledger.revealDrawn = ledger.reveal;
            touchLedger();
        }
    }
    // ---- rain, when the storm is up
    rainAlpha = env.calm || backOpen || studioOpen ? 0.0f : 0.85f * juce::jlimit(0.0f, 1.0f, (env.storm - 0.22f) / 0.6f);
    rainAcc += fdt;
    if (rainAlpha > 0.004f && rainAcc >= 1.0f / 15.0f - 1e-4f)
    {
        const float rdt = juce::jmin(0.2f, rainAcc);
        rainX = std::fmod(rainX - rdt * 170.0f * (1.0f + env.storm), 520.0f);
        rainY = std::fmod(rainY + rdt * 560.0f * (1.0f + env.storm), 520.0f);
        rainMoved = true;
    }
    if (rainAcc >= 1.0f / 15.0f - 1e-4f)
        rainAcc = 0;
    if (std::abs(rainAlpha - rainDrawn) > 0.004f)
        rainMoved = true; // it starts, thickens or stops
    if (turning && clock - turnAt >= 0.9)
    {
        turning = false;
        frontDirty.fill();
    }
    compose(false);
    // ---- the governor: what a second of this costs; the ambient motion gives way first
    const double cost = nowMs() - t0;
    costAcc += cost;
    costTime += dt;
    lastTickMs = cost;
    if (costTime >= 1.0)
    {
        const double perSec = costAcc / costTime; // ms of work per second of clock
        costAvg = perSec;
        costAcc = costTime = 0;
        if (!testClock)
        { // smooth clouds (15 a second) unless a second of this really is too dear twice running; back as soon as it is not
            costHigh = perSec > 130.0 ? costHigh + 1 : 0;
            if (costHigh >= 2 && ambientHz > 8)
            {
                ambientHz = ambientHz > 10 ? 10 : 8;
                costHigh = 0;
            }
            else if (perSec < 90.0 && ambientHz < 15)
                ambientHz = ambientHz < 10 ? 10 : 15;
        }
    }
}

// ============================================================================================== composing
static void copyToTile(const Surface &tile, int tx, int ty, const Surface &src, IRect r)
{
    r = r.getIntersection({tx, ty, tile.w, tile.h}).getIntersection(src.bounds());
    for (int y = r.getY(); y < r.getBottom(); ++y)
        std::memcpy(tile.row(y - ty) + (r.getX() - tx), src.row(y) + r.getX(), (size_t)r.getWidth() * 4);
}
static void lerpToTile(const Surface &tile, int tx, int ty, const Surface &a, const Surface &b, IRect r, uint32_t t)
{
    r = r.getIntersection({tx, ty, tile.w, tile.h}).getIntersection(a.bounds());
    const uint32_t it = 256u - t;
    for (int y = r.getY(); y < r.getBottom(); ++y)
    {
        Px *d = tile.row(y - ty) + (r.getX() - tx);
        const Px *pa = a.row(y) + r.getX(), *pb = b.row(y) + r.getX();
        for (int i = 0, n = r.getWidth(); i < n; ++i)
        {
            const Px p = pa[i], q = pb[i];
            d[i] = 0xff000000u | ((((p & 0x00ff00ffu) * it + (q & 0x00ff00ffu) * t) >> 8) & 0x00ff00ffu) | ((((p >> 8) & 0x000000ffu) * it + ((q >> 8) & 0x000000ffu) * t) & 0x0000ff00u);
        }
    }
}

void MachineEditor::Impl::compose(bool fromPaint)
{
    if (!as)
        return;
    // ---- the scene: the cells that changed, back to front
    sceneDirty.rects(rectTmp, S);
    if (!rectTmp.empty())
    {
        IRect bounds = rectTmp.front();
        juce::RectangleList<int> rl;
        for (auto &r : rectTmp)
        {
            copyRect(scene.s, worldBuf.s, r);
            bounds = bounds.getUnion(r);
            rl.addWithoutMerging(r);
        }
        juce::LowLevelGraphicsSoftwareRenderer llr(scene.img, juce::Point<int>(), rl);
        juce::Graphics g(llr);
        g.addTransform(juce::AffineTransform::scale((float)S));
        Ctx c{scene.s, S, &rectTmp, bounds, &g, dimOn ? dimLut : nullptr};
        double q0 = nowMs(), q1;
        if (backOpen || studioOpen)
        { // the back room / the studio fill the window: nothing of the front is drawn under them
            if (backOpen)
                backDraw(c);
            else
            {
                studioDraw(c);
                drawLedger(c); // the sound list opens over the studio too
            }
            frontDirty.add(sceneDirty);
            cellsComposed += sceneDirty.count();
            sceneDirty.clear();
        }
        else
        {
            stage[page].drawBack(c);
            prof[5] += (q1 = nowMs()) - q0;
            drawControlsArt(c);
            prof[6] += (q0 = nowMs()) - q1;
            stage[page].drawFront(c);
            if (gradeOn)
                for (auto &r : rectTmp)
                    applyLut(scene.s, r, gradeR, gradeG, gradeB);
            prof[7] += (q1 = nowMs()) - q0;
            drawControlsText(c);
            drawDoors(c);
        drawStudioButton(c);
            drawLedger(c);
            subDraw(c);
            prof[8] += nowMs() - q1;
            frontDirty.add(sceneDirty);
            cellsComposed += sceneDirty.count();
            sceneDirty.clear();
        }
    }
    // ---- the front: the page change's cloud and cross-fade, the rain; then the cells go to the screen's tiles
    const double tt = clock - turnAt;
    const bool rains = rainAlpha > 0.004f || rainDrawn > 0.004f;
    if (turning || (rains && rainMoved))
        frontDirty.fill();
    rainMoved = false;
    if (!frontDirty.any())
        return;
    rainDrawn = rainAlpha;
    const double qt = nowMs();
    frontDirty.rects(rectTmp, S);
    const float u = (float)juce::jlimit(0.0, 1.0, tt / 0.9);
    const float eased = u < 0.5f ? 2.0f * u * u : 1.0f - 2.0f * (1.0f - u) * (1.0f - u); // ease-in-out
    const float bankA = !turning ? 0.0f : u < 0.25f ? u / 0.25f : u < 0.7f ? 1.0f : 1.0f - (u - 0.7f) / 0.3f;
    const int dirn = page >= pageFrom ? 1 : -1;
    const float bw = (float)bank.w; // design px
    const float bankX = dirn > 0 ? -bw + (CW + bw) * eased : (float)CW - (CW + bw) * eased;
    const uint32_t mix = !turning ? 256u : tt < 0.38 ? 0u : (uint32_t)juce::jlimit(0, 256, (int)std::lround((tt - 0.38) / 0.42 * 256.0));
    for (int ty = 0; ty < tileRows; ++ty)
        for (int tx = 0; tx < tileCols; ++tx)
        {
            const IRect tr(tx * kTile, ty * kTile, juce::jmin(kTile, CW * S - tx * kTile), juce::jmin(kTile, CH * S - ty * kTile));
            bool any = false;
            for (auto &r : rectTmp)
                any = any || r.intersects(tr);
            if (!any)
                continue;
            auto &img = tiles[(size_t)(ty * tileCols + tx)];
            juce::Image::BitmapData bd(img, juce::Image::BitmapData::readWrite);
            const Surface ts{(Px *)(void *)bd.data, tr.getWidth(), tr.getHeight(), bd.lineStride / 4};
            for (auto &r : rectTmp)
            {
                const auto q = r.getIntersection(tr);
                if (q.isEmpty())
                    continue;
                if (turning && mix < 256u && snap.s.data != nullptr)
                {
                    if (mix == 0u)
                        copyToTile(ts, tr.getX(), tr.getY(), snap.s, q);
                    else
                        lerpToTile(ts, tr.getX(), tr.getY(), snap.s, scene.s, q, mix);
                }
                else
                    copyToTile(ts, tr.getX(), tr.getY(), scene.s, q);
                const IRect local = q.translated(-tr.getX(), -tr.getY());
                if (rainAlpha > 0.004f && !rainSpr.empty())
                { // the rain tile, scrolled down the slant over the whole picture
                    const int rw = rainSpr.w, rh = rainSpr.h;
                    const int ox = (int)std::lround(rainX * S), oy = (int)std::lround(rainY * S);
                    for (int yy = ((q.getY() - oy) / rh - 1) * rh + oy; yy < q.getBottom(); yy += rh)
                        for (int xx = ((q.getX() - ox) / rw - 1) * rw + ox; xx < q.getRight(); xx += rw)
                            blit(ts, local, rainSpr, xx - tr.getX(), yy - tr.getY(), (int)std::lround(rainAlpha * 256.0f));
                }
                if (bankA > 0.004f)
                    blitScaled(ts, local, bank, bankX * S - (float)tr.getX(), -(float)tr.getY(), (float)S, (int)std::lround(bankA * 256.0f), Blend::Over, dirn < 0);
            }
        }
    frontStale = true;
    prof[9] += nowMs() - qt;
    // the screen is told about every rect composed, also when the compose came from a paint: that paint only puts its
    // own (often small) area on the screen, and the rest would wait for something else to repaint it (owner,
    // 2026-11-15: a studio page "gets stuck ... move your cursor around and it slowly comes through")
    juce::ignoreUnused(fromPaint);
    {
        const double k = ed.viewScale() / (double)S; // device px of the scene -> component px
        for (auto &r : rectTmp)
            ed.repaint(juce::Rectangle<int>::leftTopRightBottom((int)std::floor(r.getX() * k), (int)std::floor(r.getY() * k), (int)std::ceil(r.getRight() * k), (int)std::ceil(r.getBottom() * k)));
    }
    frontDirty.clear();
}

void MachineEditor::Impl::present(juce::Graphics &g, float phys)
{
    const auto clip = g.getClipBounds();
    const float vs = (float)ed.viewScale(); // the window's size (the grip): the same picture, scaled, its shape kept
    const bool exact = std::abs(phys * vs - (float)S) < 0.01f;
    g.setImageResamplingQuality(exact ? juce::Graphics::lowResamplingQuality : juce::Graphics::mediumResamplingQuality);
    if (!exact || std::abs(vs - 1.0f) > 0.001f)
    { // an odd scale: one image, so no seams between tiles
        // the page change's cloud and cross-fade and the rain live only on the tiles (the front): while one of them
        // shows, the tiles go into one picture and that is drawn; otherwise the scene is the same picture
        const bool overlay = turning || rainDrawn > 0.004f;
        if (overlay && !tiles.empty())
        {
            if (!front.isValid() || front.getWidth() != CW * S || front.getHeight() != CH * S)
            {
                front = juce::Image(juce::Image::ARGB, CW * S, CH * S, false);
                frontStale = true;
            }
            if (frontStale)
            {
                juce::Image::BitmapData fd(front, juce::Image::BitmapData::writeOnly);
                for (int ty = 0; ty < tileRows; ++ty)
                    for (int tx = 0; tx < tileCols; ++tx)
                    {
                        auto &img = tiles[(size_t)(ty * tileCols + tx)];
                        juce::Image::BitmapData bd(img, juce::Image::BitmapData::readOnly);
                        const int w = img.getWidth(), h = img.getHeight();
                        for (int y = 0; y < h; ++y)
                            std::memcpy(fd.getPixelPointer(tx * kTile, ty * kTile + y), bd.getLinePointer(y), (size_t)w * 4);
                    }
                frontStale = false;
            }
        }
        g.drawImageTransformed(overlay && front.isValid() ? front : scene.img, juce::AffineTransform::scale(vs / (float)S));
        const auto pic = juce::Rectangle<float>(0, 0, CW * vs, CH * vs);
        if (pic.getRight() < (float)ed.getWidth() - 0.5f || pic.getBottom() < (float)ed.getHeight() - 0.5f)
        { // a host gave a window of another shape: the rest is the night
            g.setColour(juce::Colours::black);
            g.fillRect(juce::Rectangle<float>(pic.getRight(), 0, (float)ed.getWidth() - pic.getRight(), (float)ed.getHeight()));
            g.fillRect(juce::Rectangle<float>(0, pic.getBottom(), pic.getRight(), (float)ed.getHeight() - pic.getBottom()));
        }
        return;
    }
    for (int ty = 0; ty < tileRows; ++ty)
        for (int tx = 0; tx < tileCols; ++tx)
        {
            const juce::Rectangle<int> r(tx * kTile / S, ty * kTile / S, (kTile + S - 1) / S, (kTile + S - 1) / S);
            if (!r.intersects(clip))
                continue;
            g.drawImageTransformed(tiles[(size_t)(ty * tileCols + tx)], juce::AffineTransform::scale(1.0f / (float)S).translated((float)(tx * kTile) / (float)S, (float)(ty * kTile) / (float)S));
        }
}

// ============================================================================================== the editor
MachineEditor::MachineEditor(ss::Patch &pm, ss::WavetableHandoff &h, ss::Synth::audioToMainQueue_t &atou, ss::Synth::mainToAudioQueue_T &utoa,
                             ss::Synth::audioOutputQueue_t &aor, std::atomic<bool> &ea, std::atomic<uint32_t> &fr, ss::Synth::DawStateMain &ds,
                             const clap_host_t *host, UiNoteQueue &notes, Morph *mo, spectral::SpectralFx *sp, sub::SubBass *sb)
    : patch(pm), morph(mo), spectral(sp), sub(sb), handoff(h), audioToMain(atou), mainToAudio(utoa), audioOutputRing(aor), editorActive(ea), uiForceRebuild(fr),
      dawState(ds), clapHost(host), noteQueue(notes)
{
    lastForceRebuild = uiForceRebuild.load();
    impl = std::make_unique<Impl>(*this);
    setOpaque(true);
    setWantsKeyboardFocus(true);
    { // the size it had last time (never over what fits the main screen, unless that is under 100 %)
        double s0 = juce::jlimit(sizing::kMin, sizing::kMax, impl->prefs->getDoubleValue("uiScale", 1.0));
        if (s0 > 1.0)
            s0 = juce::jmin(s0, sizing::displayCap(nullptr));
        const int w0 = sizing::widthFor(s0);
        setSize(w0, sizing::heightForWidth(w0));
    }
    editorActive = true;
    audioOutputRing.subscribe();
    if (sub)
        sub->tapOpen(true);
    mainToAudio.push({ss::Synth::MainToAudioMsg::REQUEST_NON_PATCH_STATE});
    requestFlush();
    impl->statusText = "Touch a chamber to shape that ghost. Drag a hose from its OUT socket to a ghost below it to make it haunt it.";
    // the picture is built for the screen it will most likely open on (a paint at another scale builds it again)
    float sc = 1.0f;
    if (auto *d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        sc = (float)d->scale;
    impl->build(sc >= 1.5f ? 2 : 1);
    impl->rippleAt = impl->clock + 0.5;
    impl->rippleDone = false;
    spyReopen = impl->prefs->getBoolValue("spyglassOpen", false);
    uiDirty = true;
    startTimerHz(30);
}

MachineEditor::~MachineEditor()
{
    stopTimer();
    closeSpyglass(false); // (remembered as open: it comes back with the editor)
    audioOutputRing.unsubscribe();
    if (sub)
        sub->tapOpen(false);
    editorActive = false;
    if (clapHost)
        clapHost->request_callback(clapHost);
    impl.reset();
}

void MachineEditor::paint(juce::Graphics &g)
{
    const double t0 = nowMs();
    const float phys = g.getInternalContext().getPhysicalPixelScaleFactor();
    const int s = phys * (float)viewScale() >= 1.2f ? 2 : 1; // the 2x art from 120 % of the screen's own pixel up
    if (s != impl->S)
        impl->build(s);
    impl->compose(true);
    impl->present(g, phys);
    impl->lastPaintMs = nowMs() - t0;
    impl->costAcc += impl->lastPaintMs;
}

double MachineEditor::viewScale() const { return sizing::scaleOf(juce::jmax(1, getWidth()), juce::jmax(1, getHeight())); }

void MachineEditor::resized()
{
    // the host (or the grip) gave the window a new size: the whole picture follows (present() scales it); a size the
    // host chose is remembered like one picked here
    if (getParentComponent() != nullptr && impl && impl->prefs && std::abs(viewScale() - impl->prefs->getDoubleValue("uiScale", 1.0)) > 0.004)
        impl->prefs->setValue("uiScale", viewScale());
    repaint();
}

void MachineEditor::setUiScale(double s, bool save)
{
    s = juce::jlimit(sizing::kMin, isShowing() ? sizing::displayCap(this) : sizing::kMax, s);
    const int w = sizing::widthFor(s), h = sizing::heightForWidth(w);
    if (w != getWidth() || h != getHeight())
    {
        if (onRequestSize && !onRequestSize(w, h))
            return; // the host would not: keep the size it has
        if (auto *holder = getParentComponent())
        { // inside the plugin's holder (clap_juce_shim): it and the top-level part grow with us, before the host's set_size arrives
            holder->setSize(w, h);
            if (auto *top = holder->getParentComponent())
                top->setSize(holder->getBoundsInParent().getWidth(), holder->getBoundsInParent().getHeight());
            if (getWidth() != w || getHeight() != h)
                setSize(w, h);
        }
        else
            setSize(w, h);
    }
    if (save && impl->prefs)
    {
        impl->prefs->setValue("uiScale", viewScale());
        impl->prefs->saveIfNeeded();
    }
}

double MachineEditor::testSavedScale() const { return impl->prefs ? impl->prefs->getDoubleValue("uiScale", 1.0) : 1.0; }

// ============================================================================================== the SPYGLASS
void MachineEditor::toggleSpyglass()
{
    if (spy)
        closeSpyglass(true);
    else
        openSpyglass();
}

void MachineEditor::openSpyglass()
{
    spyReopen = false;
    if (spy)
    {
        spy->toFront(false);
        return;
    }
    if (!spyRing)
        spyRing = std::make_unique<ss::Synth::audioOutputQueue_t>();
    spy = std::make_unique<SpyglassWindow>(*spyRing, hostSR, analyzerDefaults);
    spy->onCloseRequested = [this]() { closeSpyglass(true); };
    spy->onBoundsChanged = [this](juce::Rectangle<int> r) {
        if (impl->prefs)
            impl->prefs->setValue("spyglassBounds", r.toString());
    };
    const auto was = juce::Rectangle<int>::fromString(impl->prefs ? impl->prefs->getValue("spyglassBounds") : juce::String());
    spy->open(was, this);
    if (impl->prefs)
    {
        impl->prefs->setValue("spyglassOpen", true);
        impl->prefs->saveIfNeeded();
    }
    impl->spyglassChanged();
}

// a press or a release on the machine: the host may have just brought the plugin's window forward (FL Studio does on
// every click), so the SPYGLASS is put back in front of it once that has happened (deferred: after the host's own reorder)
void MachineEditor::raiseSpyglass()
{
    if (!spy)
        return;
    juce::MessageManager::callAsync([sp = juce::Component::SafePointer<SpyglassWindow>(spy.get())]() {
        if (sp != nullptr && sp->isOnDesktop() && sp->isVisible())
            sp->toFront(false);
    });
}

void MachineEditor::closeSpyglass(bool rememberClosed)
{
    if (rememberClosed && impl && impl->prefs)
    {
        impl->prefs->setValue("spyglassOpen", false);
        impl->prefs->saveIfNeeded();
    }
    if (!spy)
        return;
    if (impl && impl->prefs)
        impl->prefs->saveIfNeeded();
    spy.reset();     // its analysis thread stops and lets go of the ring
    spyRing.reset(); // then the ring
    if (impl)
        impl->spyglassChanged();
}

void MachineEditor::requestFlush()
{
    if (!clapHost)
        return;
    if (auto *ext = static_cast<const clap_host_params_t *>(clapHost->get_extension(clapHost, CLAP_EXT_PARAMS)))
        ext->request_flush(clapHost);
}
void MachineEditor::beginEdit(ss::Param &p) { mainToAudio.push({ss::Synth::MainToAudioMsg::BEGIN_EDIT, p.meta.id}); }
void MachineEditor::setValue(ss::Param &p, float v)
{
    p.value = v;
    patch.dirty = true;
    mainToAudio.push({ss::Synth::MainToAudioMsg::SET_PARAM, p.meta.id, v});
    requestFlush();
    uiDirty = true;
}
void MachineEditor::endEdit(ss::Param &p)
{
    mainToAudio.push({ss::Synth::MainToAudioMsg::END_EDIT, p.meta.id});
    requestFlush();
}
void MachineEditor::setOnce(ss::Param &p, float v)
{
    beginEdit(p);
    setValue(p, v);
    endEdit(p);
}
void MachineEditor::selectGhost(int op) { impl->selectGhost(op, false); }
void MachineEditor::selectLink(int from, int to) { impl->selectLink(from, to, false); }
void MachineEditor::showPage(int pg) { impl->setPage(pg, impl->testClock); }
void MachineEditor::refreshAll()
{
    impl->refresh();
    uiDirty = false;
}
void MachineEditor::morphEdit(uint32_t id, double v, int type)
{
    if (!morph)
        return;
    morph->uiEdit(id, v, type);
    requestFlush();
    uiDirty = true;
}
void MachineEditor::spectralEdit(uint32_t id, double v, int type)
{
    if (!spectral)
        return;
    spectral->uiEdit(id, v, type);
    spectralService();
}
void MachineEditor::subEdit(uint32_t id, double v, int type)
{
    if (!sub)
        return;
    sub->uiEdit(id, v, type);
    requestFlush();
    uiDirty = true;
}
void MachineEditor::subPreset(int preset)
{
    if (!sub)
        return;
    sub->uiPreset(preset);
    requestFlush();
    uiDirty = true;
}
void MachineEditor::spectralService()
{
    if (!spectral)
        return;
    requestFlush();
    // a new effect / FFT size is built on the main thread: by the plugin when there is a host, here when there is none
    if (clapHost)
        clapHost->request_callback(clapHost);
    else
        spectral->serviceMainThread();
    uiDirty = true;
}
void MachineEditor::markStateDirty()
{
    if (!clapHost)
        return;
    if (auto *st = static_cast<const clap_host_state_t *>(clapHost->get_extension(clapHost, CLAP_EXT_STATE)))
        st->mark_dirty(clapHost);
}
float MachineEditor::histSample(int back) const
{
    back = juce::jlimit(0, kHist - 1, back);
    return scope[(size_t)(((scopePos - 1 - back) % kHist + kHist) % kHist)];
}

void MachineEditor::drainAudio()
{
    auto aum = audioToMain.pop();
    while (aum.has_value())
    {
        using A = ss::Synth::AudioToMainMsg;
        if (aum->action == A::UPDATE_PARAM)
        {
            auto it = patch.paramMap.find(aum->paramId);
            if (it != patch.paramMap.end())
                it->second->value = aum->value;
            uiDirty = true;
        }
        else if (aum->action == A::UPDATE_VU)
        {
            if (aum->paramId >= 1 && aum->paramId <= ss::numOps)
                vu[aum->paramId - 1] = juce::jmax(aum->value, aum->value2);
            else if (aum->paramId == 0)
                vuOut = juce::jmax(aum->value, aum->value2); // the main output: the gauge, the eyes, the lighthouse
        }
        else if (aum->action == A::UPDATE_VOICE_COUNT)
        {
            if ((int)aum->paramId != voiceCount)
                uiDirty = true;
            voiceCount = (int)aum->paramId;
        }
        else if (aum->action == A::SEND_SAMPLE_RATE)
        { // the analyzer's frequency axis (upstream's editor does the same with it)
            hostSR = aum->value;
            if (spy)
                spy->setHostSampleRate(hostSR);
        }
        aum = audioToMain.pop();
    }
    // the scope: the newest samples of the left channel. This is the only reader of the synth's output ring; while
    // the SPYGLASS is open, everything read is also handed on, in order, to the analyzer's own ring (its only writer)
    auto [L, R] = audioOutputRing.popall();
    if (sub)
    { // SUB joins after the engine's tap: what the scope, the gauge and the SPYGLASS see is the engine plus the sub
        static constexpr size_t kMax = 16384;
        float tmp[2048];
        size_t at = L.size() > kMax ? L.size() - kMax : 0;
        while (at < L.size())
        {
            const uint32_t got = sub->tapRead(tmp, (uint32_t)juce::jmin(L.size() - at, (size_t)2048));
            if (got == 0)
                break;
            for (uint32_t i = 0; i < got; ++i)
            {
                L[at + i] += tmp[i];
                R[at + i] += tmp[i];
            }
            at += got;
        }
        if (sub->on())
            vuOut = juce::jmax(vuOut, sub->level());
    }
    if (spyRing && spyRing->subscribed() && !L.empty())
        spyRing->push(L, R);
    const size_t n = L.size(), start = n > (size_t)kHist ? n - (size_t)kHist : 0;
    for (size_t i = start; i < n; ++i)
    {
        scope[(size_t)scopePos] = L[i];
        scopePos = (scopePos + 1) % kHist;
    }
    // the corner engines: their echoes and tables (the main engine's are above / below)
    if (morph)
        for (auto &o : morph->owned)
            if (o)
            {
                o->drainAudioToMainInto(o->patchMain);
                o->reconcileWavetables();
            }
}

void MachineEditor::timerCallback()
{
    drainAudio();
    const auto fr = uiForceRebuild.load();
    if (fr != lastForceRebuild)
    {
        lastForceRebuild = fr;
        uiDirty = true;
    }
    // wavetable patches: build and publish the tables patchMain references (the upstream editor's idle does the same)
    ss::reconcileWavetables<ss::Synth::mainToAudioQueue_T, ss::Synth::MainToAudioMsg>(patch, handoff, mainToAudio, wavetableError);
    if (morph && morph->changed.load() != impl->lastMorphChange)
        uiDirty = true;
    if (uiDirty)
    {
        uiDirty = false;
        impl->refresh();
        if (morph)
            impl->lastMorphChange = morph->changed.load();
    }
    for (auto &v : vu)
        v *= 0.85f; // fall back between meter messages
    vuOut *= 0.85f;
    const double now = nowMs();
    double dt = juce::jlimit(0.0, 0.1, (now - impl->lastTick) / 1000.0);
    impl->lastTick = now;
    if (impl->testClock)
        dt = 1.0 / 30.0;
    // Nothing is composed while nobody can see it (closed, hidden, minimised): the clock stops, only the engine's
    // queues are served. Seen but not attended (the pointer is elsewhere and nothing here was touched for 8 s) it
    // steps 8 times a second; when the host is not the front application, 4 times.
    auto *peer = getPeer();
    const bool onScreen = peer != nullptr && isShowing() && !peer->isMinimised();
    if (spyReopen && onScreen)
    {
        spyReopen = false;
        openSpyglass();
    }
    if (!onScreen && !impl->testClock && !impl->testRun)
        return;
    if (!impl->testClock && !impl->testRun)
    {
        const bool front = juce::Process::isForegroundProcess();
        const bool attended = front && (isMouseOver(true) || isMouseButtonDown(true) || now - impl->lastTouchMs < 8000.0 || impl->turning || impl->ledger.mode != Ledger::Closed);
        if (!attended)
        {
            impl->bgAcc += dt;
            if (impl->bgAcc < (front ? 0.124 : 0.249))
                return;
            dt = juce::jmin(0.3, impl->bgAcc);
        }
        impl->bgAcc = 0;
    }
    impl->tick(dt);
}

// ============================================================================================== test hooks
int MachineEditor::testFactoryCount() const { return (int)impl->pm->factoryPatchVector.size(); }
juce::String MachineEditor::testLoadFactory(int i)
{
    impl->loadFactory(i);
    impl->testClock = true;
    timerCallback();
    return juce::String(juce::CharPointer_UTF8(patch.name));
}
void MachineEditor::testTheme(int t)
{
    impl->setTheme(t, false);
    refreshAll();
}
void MachineEditor::testAdvance(double seconds)
{
    impl->testClock = true;
    for (int i = 0, n = juce::jmax(1, (int)std::lround(seconds * 30.0)); i < n; ++i)
        timerCallback();
}
void MachineEditor::testEnvTab(int m)
{
    impl->envTab = juce::jlimit(0, 2, m);
    refreshAll();
}
juce::String MachineEditor::testPanelTitle() const { return impl->panelTitle; }
juce::String MachineEditor::testStatus() const { return impl->statusText; }
juce::Point<float> MachineEditor::testSocket(int g, bool out) const
{
    if (auto *c = impl->find("ch" + juce::String(juce::jlimit(0, (int)ss::numOps - 1, g) + 1) + (out ? "-out" : "-in")))
        return c->centre;
    return {};
}
void MachineEditor::testMorphSet(int index)
{
    impl->applyMorphSet(index);
    refreshAll();
}
void MachineEditor::testMorphCorner(int c, int fi)
{
    if (!morph)
        return;
    LedgerItem it;
    if (fi < 0)
        it.kind = LedgerItem::Copy;
    else
    {
        const auto &v = impl->cornerPm->factoryPatchVector;
        it.kind = LedgerItem::Factory;
        it.cat = v[(size_t)fi].first;
        it.file = v[(size_t)fi].second;
    }
    impl->setCornerSound(c, it);
    refreshAll();
}
void MachineEditor::testPuck(float x, float y)
{
    if (!morph)
        return;
    morphSet(Morph::pX, x);
    morphSet(Morph::pY, y);
    refreshAll();
}
juce::String MachineEditor::testLayoutJson() const { return impl->as ? juce::JSON::toString(impl->as->layout, true) : juce::String(); }
juce::StringArray MachineEditor::testControlIds() const
{
    juce::StringArray a;
    for (auto &c : impl->ctls)
        a.add(c.id);
    return a;
}
juce::Rectangle<float> MachineEditor::testHitRect(const juce::String &id) const
{
    auto *c = impl->find(id);
    return c ? c->hit : juce::Rectangle<float>();
}
juce::String MachineEditor::testControlState(const juce::String &id) const
{
    auto *c = impl->find(id);
    if (c == nullptr)
        return {};
    return juce::String(c->page) + "/" + juce::String(c->face) + "/" + (c->interactive ? "1" : "0");
}
juce::String MachineEditor::testControlText(const juce::String &id) const
{
    auto *c = impl->find(id);
    return c ? c->t1 + "|" + c->t2 : juce::String();
}
std::vector<uint32_t> MachineEditor::testReachableParams()
{
    std::vector<uint32_t> ids;
    auto add = [&](Param *p) {
        if (p)
            ids.push_back(p->meta.id);
    };
    const int sel0 = impl->sel, link0 = impl->selLink, tab0 = impl->envTab;
    // what every control can edit, in every state of the bench
    for (auto &c : impl->ctls)
        add(c.fixed);
    for (int o = 0; o < (int)ss::numOps; ++o)
    {
        impl->sel = o;
        impl->selLink = -1;
        for (int tab = 0; tab < 3; ++tab)
        {
            impl->envTab = tab;
            for (auto &c : impl->ctls)
                if (c.face == 1)
                    add(impl->paramOf(c));
            for (int k = 0; k < 6; ++k)
                add(impl->envParam(0, k));
            add(impl->envPower(0));
        }
        // what the hoses and the wake-up rules edit
        add(&patch.sourceNodes[(size_t)o].active);
        add(&patch.mixerNodes[(size_t)o].active);
        add(&patch.selfNodes[(size_t)o].active);
        add(&patch.selfNodes[(size_t)o].fbLevel);
    }
    for (int li = 0; li < (int)ss::matrixSize; ++li)
    {
        impl->selLink = li;
        for (auto &c : impl->ctls)
            if (c.face == 2)
                add(impl->paramOf(c));
        for (int k = 0; k < 6; ++k)
            add(impl->envParam(1, k));
        add(impl->envPower(1));
        add(&patch.matrixNodes[(size_t)li].active);
        add(&patch.matrixNodes[(size_t)li].level);
    }
    for (int k = 0; k < 6; ++k)
        add(impl->envParam(2, k));
    // the pitch drop's helper (as before: AMOUNT sets these when the envelope would only shift the pitch)
    add(&patch.fineTuneMod.sustain);
    add(&patch.fineTuneMod.envPower);
    impl->sel = sel0;
    impl->selLink = link0;
    impl->envTab = tab0;
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}
void MachineEditor::testOpenLedger(int mode, int corner)
{
    impl->openLedger(mode == 1 ? Ledger::Yours : mode == 2 ? Ledger::CornerPick : mode == 3 ? Ledger::FxPick : Ledger::Sounds, corner);
    impl->ledger.openedAt = impl->clock - 1.0;
}
void MachineEditor::testCloseLedger()
{
    impl->closeLedger();
    impl->subToggle(false);
}
void MachineEditor::testSubPanel(bool open) { impl->subToggle(open); }
bool MachineEditor::testSubPanelOpen() const { return impl->subOpen; }
void MachineEditor::testBackRoom(bool open) { impl->backToggle(open); }
bool MachineEditor::testBackRoomOpen() const { return impl->backOpen; }
juce::Point<float> MachineEditor::testBackCell(int from, int to) const
{
    for (auto &c : impl->backCells)
        if (c.from == from && (to < 0 ? c.kind == 2 : to == from ? c.kind == 1 : (c.kind == 0 && c.to == to)))
            return c.c;
    return {-1, -1};
}
juce::Rectangle<float> MachineEditor::testBackDoor(int i) const { return i >= 0 && i < impl->backDoorCount ? impl->backDoors[i] : juce::Rectangle<float>(); }
juce::Rectangle<float> MachineEditor::testBackExit() const { return impl->backExit; }
void MachineEditor::testStudio(bool open) { impl->studioToggle(open); }
bool MachineEditor::testStudioOpen() const { return impl->studioOpen; }
int MachineEditor::testStudioTab() const { return impl->stTab; }
juce::Rectangle<float> MachineEditor::testStudioWidget(const juce::String &label, int op, int tab) const
{
    for (auto &w : impl->stW)
        if (w.label == label && w.op == op && w.tab == tab)
            return w.r;
    return {};
}
juce::Rectangle<float> MachineEditor::testStudioButton() const { return impl->studioDoor(); }
juce::Rectangle<float> MachineEditor::testStudioProxy(const juce::String &ctlId) const
{
    for (auto &w : impl->stW)
        if (auto *c = impl->stCtl(w); c != nullptr && c->id == ctlId)
            return w.r;
    return {};
}
juce::Rectangle<float> MachineEditor::testStudioCell(int from, int to) const
{
    for (auto &w : impl->stW)
        if (w.k == Impl::StW::Cell && w.cell >= 0 && w.cell < (int)impl->backCells.size())
        {
            auto &k = impl->backCells[(size_t)w.cell];
            if (k.from == from && (to < 0 ? k.kind == 2 : to == from ? k.kind == 1 : (k.kind == 0 && k.to == to)))
                return w.r;
        }
    return {};
}
juce::Rectangle<float> MachineEditor::testStudioPad() const { return impl->stPad; }
double MachineEditor::testLastTickMs() const { return impl->lastTickMs; }
void MachineEditor::testStorm(float v, bool strikeNow)
{
    impl->env.stormHold = v;
    if (strikeNow)
    {
        impl->curWorld().lastStrike = -99;
        impl->strike();
    }
}
void MachineEditor::testCalm(bool on) { impl->setCalm(on, false); }
int MachineEditor::testPage() const { return impl->page; }

void MachineEditor::testScale(int s) { impl->build(s); }
void MachineEditor::testRun(bool on) { impl->testRun = on; }
juce::String MachineEditor::testLive() const
{
    return juce::String(impl->costAvg, 1) + " ms of composing and painting a second, ambient motion at " + juce::String(impl->ambientHz) + " Hz, at " + juce::String(impl->S) + "x";
}

std::vector<uint32_t> MachineEditor::testFxReachable()
{
    std::vector<uint32_t> ids;
    if (!spectral)
        return ids;
    const int slot0 = impl->benchSlot, page0 = impl->benchPage;
    for (int bs = -1; bs < spectral::kSlots; ++bs)
    {
        impl->benchSlot = bs;
        impl->benchPage = 0;
        const int was = bs >= 0 ? spectral->slotType(bs) : 0;
        if (bs >= 0) // an effect with eight parameters, so that all eight host parameters of the slot show
            for (auto &t : spectral::types())
                if (t.params.size() >= 8)
                {
                    spectral->uiSet(spectral::SpectralFx::pSlotType(bs), t.index);
                    spectral->serviceMainThread();
                    break;
                }
        for (auto &c : impl->ctls)
        {
            FxV v;
            if (c.fx != 0 && c.interactive && impl->fxValue(c, v) && v.id != 0)
                ids.push_back(v.id);
            if (c.fx == FX_Name) // the name plate opens the effect ledger: the slot's effect
                ids.push_back(spectral::SpectralFx::pSlotType(c.slot));
        }
        if (bs >= 0)
        {
            spectral->uiSet(spectral::SpectralFx::pSlotType(bs), was);
            spectral->serviceMainThread();
        }
    }
    impl->benchSlot = slot0;
    impl->benchPage = page0;
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

juce::String MachineEditor::testFxEffectCheck()
{
    if (!spectral)
        return "no rack";
    int effects = 0, params = 0;
    const int slot0 = impl->benchSlot, page0 = impl->benchPage, was = spectral->slotType(0);
    juce::String bad;
    for (auto &t : spectral::types())
    {
        if (t.index == 0)
            continue;
        impl->fxSetSlotType(0, t.index); // (what a pick in the effect ledger does)
        if (spectral->slotType(0) != t.index)
            bad << " [" << t.id << " did not load]";
        ++effects;
        std::vector<bool> seen(t.params.size(), false);
        for (int pg = 0; pg < impl->fxBenchPages(); ++pg)
        {
            impl->benchPage = pg;
            for (auto &c : impl->ctls)
            {
                FxV v;
                if (c.fx == FX_P && impl->fxValue(c, v) && v.p >= 0 && v.p < (int)seen.size())
                {
                    seen[(size_t)v.p] = true;
                    // set it to its far end and back through the control's own path
                    const float keep = v.val, far = keep == v.hi ? v.lo : v.hi;
                    impl->fxSet(c, far, 0);
                    impl->fxSet(c, far, 1);
                    impl->fxSet(c, far, 2);
                    if (std::abs(spectral->slotValue(0, v.p) - far) > 1e-3f * juce::jmax(1.0f, std::abs(far)))
                        bad << " [" << t.id << "." << v.d->id << " did not take a value]";
                    impl->fxSet(c, keep, 0);
                    impl->fxSet(c, keep, 1);
                    impl->fxSet(c, keep, 2);
                }
            }
        }
        for (size_t i = 0; i < seen.size(); ++i)
        {
            ++params;
            if (!seen[i])
                bad << " [" << t.id << "." << t.params[i].id << " not on the bench]";
        }
    }
    impl->fxSetSlotType(0, was);
    impl->benchSlot = slot0;
    impl->benchPage = page0;
    refreshAll();
    if (bad.isNotEmpty())
        return "MISSING:" + bad;
    return "ok: " + juce::String(effects) + " effects, " + juce::String(params) + " parameters";
}

juce::String MachineEditor::testPerf(int frames)
{
    impl->testClock = true;
    double total = 0, worst = 0;
    const long long cells0 = impl->cellsComposed, wc0 = impl->worldCells;
    for (auto &p : impl->prof)
        p = 0;
    for (auto &p : impl->cellsBy)
        p = 0;
    for (int i = 0; i < frames; ++i)
    {
        const double a = nowMs();
        timerCallback();
        const double d = nowMs() - a;
        total += d;
        worst = juce::jmax(worst, d);
    }
    const double cells = (double)(impl->cellsComposed - cells0) / juce::jmax(1, frames);
    juce::String parts;
    static const char *nm[10] = {"sky", "world", "stage", "hoses", "lights", "back", "controls", "front", "text", "tiles"};
    for (int i = 0; i < 10; ++i)
        parts << " " << nm[i] << " " << juce::String(impl->prof[i] / juce::jmax(1, frames), 2);
    parts << " cells: stage " << juce::String((double)impl->cellsBy[0] / frames, 0) << " hoses " << juce::String((double)impl->cellsBy[1] / frames, 0) << " lights " << juce::String((double)impl->cellsBy[2] / frames, 0);
    parts << " worldcells " << juce::String((double)(impl->worldCells - wc0) / juce::jmax(1, frames), 0);
    if (std::getenv("P2_PROF") != nullptr)
        std::printf("  parts (ms a frame):%s\n", parts.toRawUTF8());
    return juce::String(total / juce::jmax(1, frames), 2) + " ms a frame (worst " + juce::String(worst, 2) + "), " + juce::String(cells, 0) + " of " + juce::String(NCELLS) + " cells a frame (" +
           juce::String(100.0 * cells / NCELLS, 1) + " % of the picture), at " + juce::String(impl->S) + "x";
}
} // namespace ghostblock::p2::machine
