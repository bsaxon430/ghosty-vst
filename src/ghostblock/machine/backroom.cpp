/*
 * Poltergeist 2 (GhostBlock) - the ghost-machine editor: the BACK ROOM, the wiring behind the
 * machine.
 *
 * The ghost's eyes and mouth (GHOSTS, MAIN, MORPH) are doors: a click goes through the cloud into
 * the back room, a full-window room with every FM link laid out as plumbing (common/backroom/, art
 * cut from the machine's own by scripts/ghostblock/make_backroom_art.py). Each row is a ghost; its
 * pipe runs to a window under every ghost it may haunt (a higher number), to the window on the
 * diagonal (into itself: feedback) and to YOU HEAR (how loud it is in the mix). A window shows the
 * amount; click an empty one to plug it in, drag up / down for how much, right-click to unplug. The
 * plate at the top right (or Esc) goes back out to the front panel, which is not changed in any
 * way.
 *
 * It edits exactly the parameters the front panel's hoses, loops and mix valves edit (matrix level
 * / active, self feedback, mixer level / active), with the same BEGIN_EDIT / SET_PARAM / END_EDIT
 * messages. No sound code here.
 */
#include "ghostblock/machine/machine-impl.h"

namespace ghostblock::p2::machine
{
static constexpr int kBackExit = -2, kBackTitle = -3;

// ==============================================================================================
// building
void MachineEditor::Impl::backBuild()
{
    backCells.clear();
    backJson = juce::JSON::parse(Assets::textFile("common/backroom/backroom.json"));
    backSpr = Sprite(); // loaded the first time the room opens (it is the size of the window)
    if (auto *arr = backJson["cells"].getArray())
        for (auto &v : *arr)
        {
            BackCell c;
            c.from = (int)v["from"];
            c.to = (int)v["to"];
            const auto k = v["kind"].toString();
            c.kind = k == "fb" ? 1 : k == "out" ? 2 : 0;
            c.c = pointOf(v["centre"]);
            c.cells = cellsOf(FRect(c.c.x - 30, c.c.y - 30, 60, 60));
            backCells.push_back(c);
        }
    const auto &pl = backJson["plates"];
    backExit = rectOf(pl["exit"]);
    backTitle = rectOf(pl["title"]);
    backHint = rectOf(pl["hint"]);
    backOutHead = rectOf(pl["out-head"]);
    backLegend = rectOf(pl["legend"]);
    backHintCells = cellsOf(backHint.expanded(2));
    const auto ch = pointOf(backJson["chamber"]);
    const float lx = numOf(backJson["left_x"]), hy = numOf(backJson["head_y"]);
    for (int o = 0; o < (int)ss::numOps; ++o)
    {
        const float cx = (float)(double)backJson["cols"][o],
                    ry = (float)(double)backJson["rows"][o];
        backHead[0][o] = FRect(lx - ch.x * 0.5f + 6, ry - ch.y * 0.5f + 6, ch.x - 12, ch.y - 12);
        backHead[1][o] = FRect(cx - ch.x * 0.5f + 6, hy - ch.y * 0.5f + 6, ch.x - 12, ch.y - 12);
    }
    backDoorCount = 0;
    if (auto *d = backJson["doors"].getArray())
        for (auto &v : *d)
            if (backDoorCount < 3)
                backDoors[backDoorCount++] = rectOf(v);
    doorCells.clear();
    for (int i = 0; i < backDoorCount; ++i)
        for (auto c : cellsOf(backDoors[i].expanded(14)))
            doorCells.push_back(c);
    backHover = -1;
    doorHover = -1;
}

// the pages that have the ghost's face (the FX page has a ghost of its own, without one)
bool MachineEditor::Impl::doorPage() const
{
    if (!as || page < 0 || page >= as->pageKeys.size())
        return false;
    const auto &k = as->pageKeys[page];
    return k == "ghosts" || k == "main" || k == "morph";
}

int MachineEditor::Impl::doorAt(FPoint p) const
{
    if (backOpen || !doorPage() || subOpen)
        return -1;
    for (int i = 0; i < backDoorCount; ++i)
    {
        const auto &r =
            backDoors[i]; // an ellipse in its rect: the eye sockets and the mouth are round
        const float dx = (p.x - r.getCentreX()) / (r.getWidth() * 0.5f),
                    dy = (p.y - r.getCentreY()) / (r.getHeight() * 0.5f);
        if (dx * dx + dy * dy <= 1.0f)
            return i;
    }
    return -1;
}

void MachineEditor::Impl::setDoorHover(int d)
{
    if (d == doorHover)
        return;
    doorHover = d;
    sceneDirty.mark(doorCells);
    if (d >= 0)
        ed.status("THE BACK ROOM: click the ghost's eyes or mouth to go behind the machine and see "
                  "all the wiring.");
}

// the door's light: the eyes and the mouth glow warm while one of them is under the pointer
void MachineEditor::Impl::drawDoors(const Ctx &c)
{
    if (backOpen || !doorPage() || doorHover < 0 || c.g == nullptr)
        return;
    for (int i = 0; i < backDoorCount; ++i)
    {
        const auto r = backDoors[i];
        if (!c.touches(r.expanded(14)))
            continue;
        auto &g = *c.g; // all three light together (they are one face); the one under the pointer
                        // gets a ring
        const auto ctr = r.getCentre();
        juce::ColourGradient cg(juce::Colour(0xb0ffc25a), ctr, juce::Colour(0x00ff9a2a),
                                ctr + FPoint(r.getWidth() * 0.62f, 0), true);
        cg.addColour(0.45, juce::Colour(0x60ffa640));
        g.setGradientFill(cg);
        g.fillEllipse(r.expanded(6));
        if (i == doorHover)
        {
            g.setColour(juce::Colour(0xd0ffd98a));
            g.drawEllipse(r.reduced(3), 1.6f);
        }
    }
}

// ==============================================================================================
// what a window edits
Param *MachineEditor::Impl::backParam(const BackCell &c)
{
    switch (c.kind)
    {
    case 1:
        return &P.selfNodes[(size_t)c.from].fbLevel;
    case 2:
        return &P.mixerNodes[(size_t)c.from].level;
    default:
        return &P.matrixNodes[(size_t)posOf(c.from, c.to)].level;
    }
}
bool MachineEditor::Impl::backActive(const BackCell &c) const
{
    switch (c.kind)
    {
    case 1:
        return P.selfNodes[(size_t)c.from].active.value > 0.5f;
    case 2:
        return P.mixerNodes[(size_t)c.from].active.value > 0.5f;
    default:
        return linked(c.from, c.to);
    }
}
static juce::String g1(int o) { return "GHOST " + juce::String(o + 1); }
juce::String MachineEditor::Impl::backWhat(const BackCell &c) const
{
    switch (c.kind)
    {
    case 1:
        return g1(c.from) + " into itself (feedback)";
    case 2:
        return "how loud you hear " + g1(c.from);
    default:
        return g1(c.from) + " haunts " + g1(c.to);
    }
}
int MachineEditor::Impl::backShown(const BackCell &c)
{
    return (int)std::lround(backParam(c)->value * 100.0f);
}

void MachineEditor::Impl::backPlug(BackCell &c)
{
    switch (c.kind)
    {
    case 1:
    {
        auto &sf = P.selfNodes[(size_t)c.from];
        ed.setOnce(sf.active, 1.f);
        if (std::abs(sf.fbLevel.value) < 0.01f)
            ed.setOnce(sf.fbLevel, 0.3f);
        if (!awake(c.from))
            ed.setOnce(P.sourceNodes[(size_t)c.from].active, 1.f);
        break;
    }
    case 2:
    {
        auto &mx = P.mixerNodes[(size_t)c.from];
        ed.setOnce(mx.active, 1.f);
        if (mx.level.value < 0.01f)
            ed.setOnce(mx.level, 0.7f);
        if (!awake(c.from))
            ed.setOnce(P.sourceNodes[(size_t)c.from].active, 1.f);
        break;
    }
    default:
    { // exactly what dropping a hose on a lower ghost does on the front panel
        auto &m = P.matrixNodes[(size_t)posOf(c.from, c.to)];
        ed.setOnce(m.active, 1.f);
        if (m.level.value < 0.01f)
            ed.setOnce(m.level, 0.5f);
        for (int g2 : {c.from, c.to})
            if (!awake(g2))
                ed.setOnce(P.sourceNodes[(size_t)g2].active, 1.f);
        break;
    }
    }
    env.bump(0.4f);
    ed.status("Plugged in: " + backWhat(c) + " at " + juce::String(backShown(c)) +
              ". Drag the window up or down for how much.");
    ed.uiDirty = true;
}

void MachineEditor::Impl::backUnplug(BackCell &c)
{
    switch (c.kind)
    {
    case 1:
        ed.setOnce(P.selfNodes[(size_t)c.from].active, 0.f);
        break;
    case 2:
        ed.setOnce(P.mixerNodes[(size_t)c.from].active, 0.f);
        break;
    default:
        ed.setOnce(P.matrixNodes[(size_t)posOf(c.from, c.to)].active, 0.f);
        break;
    }
    ed.status("Unplugged: " + backWhat(c) + ".");
    ed.uiDirty = true;
}

// ==============================================================================================
// open / close
void MachineEditor::Impl::backToggle(bool open)
{
    if (open == backOpen || !as)
        return;
    closeLedger();
    if (drag.active)
        drag = Drag();
    if (hoverCtl >= 0 && hoverCtl < (int)ctls.size())
        ctls[(size_t)hoverCtl].hover = false;
    hoverCtl = hoverHoseValve = hoverPin = -1;
    doorHover = -1;
    backHover = -1;
    pageFrom = open ? page : page + 1; // the cloud crosses one way in, the other way out
    startTurn(page);
    backOpen = open;
    if (open && backSpr.empty())
        backSpr = Sprite::fromBitmap(as->bitmap("common/backroom/" + backJson["room"].toString(),
                                                numOf(backJson["png_scale"], 2.0f)));
    for (auto &c : backCells)
        c.drawn = -9999;
    markAllDirty();
    ed.uiDirty = true;
    if (open)
        ed.status("THE BACK ROOM: each row is a ghost. Click a window to plug it in, drag it up / "
                  "down for how much, right-click to unplug.");
    else
    {
        refresh();
        ed.status("Back on the front panel.");
    }
    env.bump(0.3f);
}

// ==============================================================================================
// the clock
void MachineEditor::Impl::backTick(double dt)
{
    if (!backOpen)
        return;
    backAcc += dt;
    const bool lights = backAcc >= 1.0 / 15.0;
    if (lights)
        backAcc = 0;
    bool wiring = false;
    for (auto &c : backCells)
    {
        const bool on = backActive(c);
        const int v = backShown(c);
        const float lv = on ? juce::jlimit(0.f, 1.f, env.ops[c.from].level * 1.4f) : 0.f;
        if (on != c.on)
            wiring = true;
        if (on != c.on || v != c.drawn || (lights && std::abs(lv - c.lv) > 0.05f))
        {
            c.on = on;
            c.drawn = v;
            c.lv = lv;
            sceneDirty.mark(c.cells);
        }
    }
    // whether each ghost is awake (its chamber lights) and what flows in the pipes
    uint32_t sig = 0;
    for (int o = 0; o < (int)ss::numOps; ++o)
        sig |= (awake(o) ? 1u : 0u) << o;
    if (wiring || sig != backAwake)
    {
        backAwake = sig;
        sceneDirty.fill();
    }
}

// ==============================================================================================
// drawing
void MachineEditor::Impl::backText(const Ctx &c, FRect r, const juce::String &t, float size,
                                   bool semi, juce::Colour col, juce::Colour glow, float track,
                                   float alpha)
{
    TextStyle st;
    st.size = size;
    st.semi = semi;
    st.track = track;
    st.col = col;
    st.glow = glow;
    st.shadow = true;
    const float mx = r.getWidth() - 10.0f;
    const float w = as->textWidth(t, st), q = w > mx && w > 0 ? mx / w : 1.0f;
    const auto &ts = as->text(t, st, q);
    const float base = r.getY() + (r.getHeight() - size) * 0.5f + 0.85f * size;
    c.sprite(ts.spr, r.getCentreX() - ts.w * 0.5f - ts.pad, base - 1.05f * size - ts.pad, alpha);
}

void MachineEditor::Impl::backDraw(const Ctx &c)
{
    if (!backOpen || backSpr.empty() || c.g == nullptr)
        return;
    c.sprite(backSpr, 0, 0);
    auto &g = *c.g;
    const float hy = numOf(backJson["head_y"]), lx = numOf(backJson["left_x"]),
                ox = numOf(backJson["out_x"]);
    auto colX = [&](int j) { return (float)(double)backJson["cols"][j]; };
    auto rowY = [&](int i) { return (float)(double)backJson["rows"][i]; };

    // ---- what flows: each plugged-in window colours its ghost's pipe up to it and the pipe up to
    // the ghost it feeds
    auto flow = [&](FPoint a, FPoint b, juce::Colour col)
    {
        const FRect box = FRect(a, b).expanded(8);
        if (!c.touches(box))
            return;
        g.setColour(col.withAlpha(0.22f));
        g.drawLine({a, b}, 9.0f);
        g.setColour(col.withAlpha(0.9f));
        g.drawLine({a, b}, 2.6f);
        g.setColour(juce::Colours::white.withAlpha(0.35f));
        g.drawLine({a + FPoint(0, -0.6f), b + FPoint(0, -0.6f)}, 0.8f);
    };
    for (int i = 0; i < (int)ss::numOps; ++i)
    { // the row: from the chamber to the farthest window that is plugged in
        float far = -1;
        for (auto &k : backCells)
            if (k.from == i && k.on)
                far = juce::jmax(far, k.c.x);
        if (far > 0)
            flow({lx + 40, rowY(i)}, {far, rowY(i)}, ghostColour(i));
    }
    for (auto &k : backCells)
    {
        if (!k.on)
            continue;
        const float off =
            ((float)k.from - 2.5f) * 1.5f; // several ghosts in one column: side by side threads
        const float x = (k.kind == 2 ? ox : colX(k.to)) + off;
        flow({x, k.c.y - 24}, {x, hy + (k.kind == 2 ? 26.0f : 32.0f)}, ghostColour(k.from));
    }

    // ---- the chambers: lit when the ghost is awake, its number in its colour
    for (int side = 0; side < 2; ++side)
        for (int o = 0; o < (int)ss::numOps; ++o)
        {
            const auto r = backHead[side][o];
            if (!c.touches(r.expanded(4)))
                continue;
            const bool aw = awake(o);
            const auto col = ghostColour(o);
            if (aw)
            {
                juce::ColourGradient cg(col.withAlpha(0.42f), r.getCentre(), col.withAlpha(0.0f),
                                        r.getCentre() + FPoint(r.getWidth() * 0.5f, 0), true);
                g.setGradientFill(cg);
                g.fillEllipse(r.reduced(4));
            }
            const bool hov = backHover == -10 - (side * 10 + o);
            backText(c, r, juce::String(o + 1), 22.0f, true,
                     aw ? col.brighter(0.25f) : juce::Colour(0xff8a7b66),
                     aw ? col.withAlpha(0.8f) : juce::Colour(0x00000000), 0.0f);
            if (hov)
            {
                g.setColour(juce::Colour(0xd0ffd98a));
                g.drawEllipse(r.reduced(2), 1.6f);
            }
        }

    // ---- the windows
    for (auto &k : backCells)
    {
        const FRect r(k.c.x - 22, k.c.y - 22, 44, 44);
        if (!c.touches(r.expanded(6)))
            continue;
        const auto col = ghostColour(k.from);
        const bool hov = backHover == (int)(&k - backCells.data());
        if (k.on)
        {
            const float a = 0.38f + 0.45f * k.lv;
            juce::ColourGradient cg(col.withAlpha(a), k.c, col.withAlpha(0.0f), k.c + FPoint(21, 0),
                                    true);
            g.setGradientFill(cg);
            g.fillEllipse(r.reduced(1));
            backText(c, r, juce::String(k.drawn), k.drawn <= -100 || k.drawn >= 100 ? 13.0f : 15.0f,
                     true, juce::Colour(0xfffff3d8), col, 0.0f);
            if (k.kind == 1)
                backText(c, FRect(r.getX(), r.getBottom() - 15, r.getWidth(), 10), "LOOP", 5.4f,
                         true, col.brighter(0.4f), juce::Colour(0x00000000), 0.18f);
        }
        else
            backText(c, r, "+", 17.0f, false, juce::Colour(0xffb8975e), juce::Colour(0x00000000),
                     0.0f, hov ? 1.0f : 0.55f);
        if (hov)
        {
            g.setColour(k.on ? col.brighter(0.5f).withAlpha(0.95f) : juce::Colour(0xd0ffd98a));
            g.drawEllipse(r.reduced(0.5f), backHeld == backHover ? 2.6f : 1.8f);
        }
    }

    // ---- the plates
    if (c.touches(backTitle))
        backText(c, backTitle, "THE BACK ROOM  -  WIRING", 13.0f, true, juce::Colour(0xffffd98a),
                 juce::Colour(0x60ff9a2a), 0.22f);
    if (c.touches(backExit))
    {
        const bool hov = backHover == kBackExit;
        backText(c, backExit, "<  FRONT PANEL", 10.5f, true,
                 hov ? juce::Colour(0xfffff3d8) : juce::Colour(0xffffd98a),
                 hov ? juce::Colour(0xc0ffb23e) : juce::Colour(0x00000000), 0.16f);
    }
    if (c.touches(backOutHead))
        backText(c, backOutHead, "YOU HEAR", 9.5f, true, juce::Colour(0xffffd98a),
                 juce::Colour(0x00000000), 0.2f);
    if (c.touches(backLegend))
    {
        backText(c, backLegend.withHeight(backLegend.getHeight() * 0.5f).translated(0, 3),
                 "ROW: THIS GHOST", 7.2f, true, juce::Colour(0xffffd98a), juce::Colour(0x00000000),
                 0.1f);
        backText(c, backLegend.withTrimmedTop(backLegend.getHeight() * 0.5f).translated(0, -3),
                 "HAUNTS: THE ONE ON TOP", 7.2f, true, juce::Colour(0xffffd98a),
                 juce::Colour(0x00000000), 0.1f);
    }
    if (c.touches(backHint))
        backText(c, backHint, statusText, 8.8f, false, juce::Colour(0xffe6c98a),
                 juce::Colour(0x00000000), 0.01f);
}

// ==============================================================================================
// the pointer
int MachineEditor::Impl::backAt(FPoint p) const
{
    if (backExit.contains(p))
        return kBackExit;
    for (int i = 0; i < (int)backCells.size(); ++i)
        if (p.getDistanceFrom(backCells[(size_t)i].c) <= 24.0f)
            return i;
    for (int side = 0; side < 2; ++side)
        for (int o = 0; o < (int)ss::numOps; ++o)
            if (backHead[side][o].contains(p))
                return -10 - (side * 10 + o);
    return -1;
}

void MachineEditor::Impl::backHoverAt(FPoint p)
{
    const int h = backAt(p);
    if (h == backHover)
        return;
    auto markOf = [&](int ix)
    {
        if (ix >= 0 && ix < (int)backCells.size())
            sceneDirty.mark(backCells[(size_t)ix].cells);
        else if (ix == kBackExit)
            sceneDirty.mark(backExit.expanded(4));
        else if (ix <= -10)
        {
            const int s = (-10 - ix) / 10, o = (-10 - ix) % 10;
            sceneDirty.mark(backHead[s][o].expanded(4));
        }
    };
    markOf(backHover);
    backHover = h;
    markOf(h);
    if (h >= 0)
    {
        auto &k = backCells[(size_t)h];
        if (k.on)
            ed.status(backWhat(k) + ": " + juce::String(backShown(k)) +
                      "   -   drag up / down for how much (SHIFT: fine), right-click to unplug");
        else if (k.kind == 2)
            ed.status("YOU HEAR: " + g1(k.from) +
                      " is not in the mix (it only haunts others). Click to hear it.");
        else
            ed.status("Click to plug in: " + backWhat(k) + ".");
    }
    else if (h == kBackExit)
        ed.status("Back out to the front panel (or press Esc).");
    else if (h <= -10)
    {
        const int o = (-10 - h) % 10;
        ed.status(g1(o) + (awake(o) ? " (awake)" : " (asleep)") +
                  "   -   click: shape it on the front panel");
    }
    sceneDirty.mark(backHintCells);
}

bool MachineEditor::Impl::backDown(const juce::MouseEvent &e, FPoint p)
{
    if (!backOpen)
        return false;
    const int h = backAt(p);
    if (h == kBackExit)
    {
        backToggle(false);
        return true;
    }
    if (h <= -10)
    {
        const int o = (-10 - h) % 10;
        backToggle(false);
        selectGhost(o, false);
        return true;
    }
    if (h < 0)
        return true;
    auto &k = backCells[(size_t)h];
    if (e.mods.isPopupMenu())
    {
        juce::PopupMenu m;
        m.setLookAndFeel(&brass(*this));
        m.addSectionHeader(backWhat(k).toUpperCase());
        auto sp = juce::Component::SafePointer<MachineEditor>(&ed);
        if (k.on)
        {
            m.addItem("Unplug",
                      [sp, h]
                      {
                          if (sp != nullptr && h < (int)sp->impl->backCells.size())
                              sp->impl->backUnplug(sp->impl->backCells[(size_t)h]);
                      });
            for (int v : {25, 50, 75, 100})
                m.addItem("Set to " + juce::String(v),
                          [sp, h, v]
                          {
                              if (sp == nullptr || h >= (int)sp->impl->backCells.size())
                                  return;
                              auto &I = *sp->impl;
                              I.ed.setOnce(*I.backParam(I.backCells[(size_t)h]), (float)v / 100.0f);
                              I.ed.uiDirty = true;
                          });
        }
        else
            m.addItem("Plug in",
                      [sp, h]
                      {
                          if (sp != nullptr && h < (int)sp->impl->backCells.size())
                              sp->impl->backPlug(sp->impl->backCells[(size_t)h]);
                      });
        m.addSeparator();
        m.addItem("Show it on the front panel",
                  [sp, h]
                  {
                      if (sp == nullptr || h >= (int)sp->impl->backCells.size())
                          return;
                      auto &I = *sp->impl;
                      const auto k2 = I.backCells[(size_t)h];
                      I.backToggle(false);
                      if (k2.kind == 0 && I.linked(k2.from, k2.to))
                          I.selectLink(k2.from, k2.to, false);
                      else
                          I.selectGhost(k2.from, false);
                  });
        const auto r =
            (FRect(k.c.x - 22, k.c.y - 22, 44, 44) * (float)ed.viewScale()).toNearestInt();
        m.showMenuAsync(juce::PopupMenu::Options()
                            .withTargetComponent(&ed)
                            .withTargetScreenArea(ed.localAreaToGlobal(r))
                            .withStandardItemHeight(24));
        return true;
    }
    if (!k.on)
        backPlug(k); // and the same press goes on to set how much
    auto *prm = backParam(k);
    const float lo = prm->meta.minVal, hi = prm->meta.maxVal;
    const float v0 = prm->value;
    const float y0 = p.y;
    ed.beginEdit(*prm);
    backHeld = h;
    env.holdTarget = 1;
    sceneDirty.mark(k.cells);
    drag.move = [this, h, lo, hi, v0, y0](FPoint q, const juce::MouseEvent &ev)
    {
        if (h >= (int)backCells.size())
            return;
        auto &kk = backCells[(size_t)h];
        auto *pp = backParam(kk);
        const float per = ev.mods.isShiftDown() ? 600.0f : 150.0f; // design px for the whole range
        const float v = juce::jlimit(lo, hi, v0 + (y0 - q.y) / per * (hi - lo));
        if (v != pp->value)
        {
            ed.setValue(*pp, v);
            sceneDirty.mark(kk.cells);
            ed.status(backWhat(kk) + ": " + juce::String(backShown(kk)));
            env.bump(0.08f);
        }
    };
    drag.up = [this, h](FPoint, const juce::MouseEvent &)
    {
        env.holdTarget = 0;
        backHeld = -1;
        if (h < (int)backCells.size())
        {
            ed.endEdit(*backParam(backCells[(size_t)h]));
            sceneDirty.mark(backCells[(size_t)h].cells);
        }
        ed.uiDirty = true;
    };
    drag.active = true;
    return true;
}

bool MachineEditor::Impl::backWheel(FPoint p, float dy)
{
    if (!backOpen)
        return false;
    const int h = backAt(p);
    if (h < 0 || dy == 0.f)
        return true;
    auto &k = backCells[(size_t)h];
    if (!k.on)
        return true;
    auto *prm = backParam(k);
    const float v = juce::jlimit(prm->meta.minVal, prm->meta.maxVal,
                                 prm->value + (dy > 0 ? 0.02f : -0.02f) *
                                                  (prm->meta.maxVal - prm->meta.minVal));
    ed.setOnce(*prm, v);
    sceneDirty.mark(k.cells);
    ed.status(backWhat(k) + ": " + juce::String(backShown(k)));
    ed.uiDirty = true;
    return true;
}
} // namespace ghostblock::p2::machine
