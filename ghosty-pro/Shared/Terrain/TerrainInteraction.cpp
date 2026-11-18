#include "TerrainInteraction.h"
#include "TerrainSerial.h"
#include <cmath>

namespace ghost::terrain
{
TerrainEditor::TerrainEditor()
{
    slots[0] = defaultTerrain();
    slots[1] = slots[0];
}

void TerrainEditor::notify()
{
    if (onChange) onChange();
    for (auto& w : watchers) if (w.second) w.second();
}

int TerrainEditor::watch (std::function<void()> f) { watchers.push_back ({ ++nextWatch, std::move (f) }); return nextWatch; }
void TerrainEditor::unwatch (int id) { watchers.erase (std::remove_if (watchers.begin(), watchers.end(), [id] (auto& w) { return w.first == id; }), watchers.end()); }

void TerrainEditor::pushUndo (const Terrain& prior)
{
    undoStack.push_back ({ active, prior });
    while ((int) undoStack.size() > kMaxUndo) undoStack.pop_front();
    redoStack.clear();
}

void TerrainEditor::set (const Terrain& t)
{
    if (gesture) endGesture();
    if (t == terrain()) return;
    pushUndo (terrain());
    slots[(size_t) active] = t;
    slots[(size_t) active].clampAll();
    if (selected >= terrain().size()) selected = -1;
    notify();
    if (onCommit) onCommit();
}

void TerrainEditor::apply (const std::function<void (Terrain&)>& edit)
{
    Terrain t = terrain();
    edit (t);
    t.clampAll();
    set (t);
}

void TerrainEditor::setWithoutUndo (const Terrain& t)
{
    slots[(size_t) active] = t;
    slots[(size_t) active].clampAll();
    if (selected >= terrain().size()) selected = -1;
    notify();
}

void TerrainEditor::beginGesture()
{
    if (gesture) return;
    gesture = true;
    before = terrain();
}

Terrain& TerrainEditor::live() { return slots[(size_t) active]; }

void TerrainEditor::changed()
{
    slots[(size_t) active].clampAll();
    trackMotion();
    notify();
}

void TerrainEditor::trackMotion()
{
    const auto& t = terrain();
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (lastSeenMs > 0.0 && t.size() == lastSeen.size())
    {
        float d = 0.0f;
        for (int i = 0; i < t.size(); ++i)
        {
            const auto& a = t[i]; const auto& b = lastSeen[i];
            d += std::abs (a.x - b.x) + std::abs (a.z - b.z) + 0.5f * std::abs (a.height - b.height) + 2.0f * std::abs (a.width - b.width);
        }
        const double dt = juce::jlimit (4.0, 200.0, now - lastSeenMs) / 1000.0;
        const float speed = (float) (d / dt);   // land units a second: ~2 is a brisk drag
        motionLevel = juce::jmax (motion(), juce::jlimit (0.0f, 1.0f, speed / 2.0f));
        motionMs = now;
    }
    lastSeen = t; lastSeenMs = now;
}

float TerrainEditor::motion() const
{
    if (motionMs <= 0.0) return 0.0f;
    const double age = juce::Time::getMillisecondCounterHiRes() - motionMs;
    return motionLevel * (float) std::exp (-age / 250.0);
}

void TerrainEditor::endGesture()
{
    if (! gesture) return;
    gesture = false;
    if (before != terrain())
    {
        undoStack.push_back ({ active, before });
        while ((int) undoStack.size() > kMaxUndo) undoStack.pop_front();
        redoStack.clear();
        if (onCommit) onCommit();
    }
}

bool TerrainEditor::undo()
{
    if (gesture) endGesture();
    if (undoStack.empty()) return false;
    auto [slot, t] = undoStack.back();
    undoStack.pop_back();
    redoStack.push_back ({ slot, slots[(size_t) slot] });
    active = slot;
    slots[(size_t) slot] = t;
    if (selected >= terrain().size()) selected = -1;
    notify();
    if (onCommit) onCommit();
    return true;
}

bool TerrainEditor::redo()
{
    if (gesture) endGesture();
    if (redoStack.empty()) return false;
    auto [slot, t] = redoStack.back();
    redoStack.pop_back();
    undoStack.push_back ({ slot, slots[(size_t) slot] });
    active = slot;
    slots[(size_t) slot] = t;
    if (selected >= terrain().size()) selected = -1;
    notify();
    if (onCommit) onCommit();
    return true;
}

void TerrainEditor::toggleAB()
{
    if (gesture) endGesture();
    active = 1 - active;
    selected = -1;
    notify();
    if (onCommit) onCommit();
}

void TerrainEditor::copyActiveToOther()
{
    slots[(size_t) (1 - active)] = terrain();
}

juce::var TerrainEditor::stateToVar() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("a", toVar (slots[0]));
    o->setProperty ("b", toVar (slots[1]));
    o->setProperty ("active", active);
    return juce::var (o);
}

bool TerrainEditor::stateFromVar (const juce::var& v)
{
    Terrain a, b;
    if (! fromVar (v.getProperty ("a", {}), a)) return false;
    if (! fromVar (v.getProperty ("b", {}), b)) b = a;
    slots[0] = a; slots[1] = b;
    active = juce::jlimit (0, 1, (int) v.getProperty ("active", 0));
    undoStack.clear(); redoStack.clear();
    selected = -1;
    notify();
    return true;
}

// ------------------------------------------------------------------------------------------------ gestures
static juce::String two (float v) { return juce::String (v, 2); }

TerrainInteraction::Hit TerrainInteraction::legacyHit (juce::Point<float> p, juce::ModifierKeys) const
{
    Hit h;
    if (const int i = handleAt ? handleAt (p) : -1; i >= 0) { h.peak = i; h.kind = Kind::height; return h; }
    if (const int i = widthTagAt ? widthTagAt (p) : -1; i >= 0) { h.peak = i; h.kind = Kind::size; h.byNumber = true; return h; }
    if (const int i = lineAt ? lineAt (p) : -1; i >= 0) { h.peak = i; h.kind = Kind::move; return h; }
    return h;
}

void TerrainInteraction::mouseDown (juce::Point<float> p, juce::ModifierKeys mods)
{
    start = p; moved = false; liveText = {}; clicked = Kind::none;
    mode = Mode::none; index = -1;
    const Hit h = hit ? hit (p) : legacyHit (p, mods);
    if (h.peak >= 0 && h.peak < ed.terrain().size())
    {
        index = h.peak; lift = h.lift; byNumber = h.byNumber;
        wasSelected = ed.selected == index;
        ed.selected = index;
        startPeak = ed.terrain()[index];
        ed.beginGesture();
        switch (h.kind)
        {
            case Kind::height: mode = mods.isShiftDown() ? Mode::grit : Mode::height; liveText = two (startPeak.height); break;
            case Kind::size:
            {
                mode = Mode::width;
                liveText = two (startPeak.r());
                float x = 0, z = 0;
                if (! byNumber && groundAt && groundAt (p, lift, x, z))
                    grabRatio = startPeak.r() / juce::jmax (1e-4f, std::hypot ((x - startPeak.x) * 2.0f, (z - startPeak.z) * 2.0f));
                else byNumber = true;
                break;
            }
            case Kind::move:
            {
                mode = mods.isShiftDown() ? Mode::grit : Mode::move;
                float x = 0, z = 0;
                if (groundAt && groundAt (p, lift, x, z)) { grabX = x - startPeak.x; grabZ = z - startPeak.z; }
                else if (pick && pick (p, x, z)) { grabX = x - startPeak.x; grabZ = z - startPeak.z; }
                else { grabX = grabZ = 0; }
                startX = x; startZ = z;
                break;
            }
            case Kind::none: break;
        }
        ed.changed();
        return;
    }
    // empty land: a tap adds (or lets go), a pull up raises a new one
    float x = 0, z = 0;
    if (pick && pick (p, x, z)) { startX = x; startZ = z; mode = Mode::pending; }
    else if (ed.selected >= 0) { ed.select (-1); }
}

void TerrainInteraction::mouseDrag (juce::Point<float> p, juce::ModifierKeys)
{
    if (mode == Mode::none) return;
    const auto d = p - start;
    if (! moved && std::abs (d.x) + std::abs (d.y) <= dragThreshold) return;
    if (mode == Mode::pending)
    {
        if (std::abs (d.y) <= dragThreshold || ed.terrain().size() >= Terrain::kMaxPeaks) return;   // a pull up raises land, down carves a valley
        ed.beginGesture();
        auto& t = ed.live();
        Peak np; np.x = startX; np.z = startZ; np.height = 0.30f; np.width = np.depth = 0.05f;
        t.add (np);
        index = t.size() - 1; startPeak = np; ed.selected = index;
        mode = Mode::create;
    }
    moved = true;
    if (index < 0 || index >= ed.terrain().size()) return;
    auto& pk = ed.live()[index];
    const float dh = -d.y / juce::jmax (20.0f, pixelsPerUnitHeight);
    switch (mode)
    {
        case Mode::create:
        case Mode::height:
            pk.height = juce::jlimit (kMinHeight, kMaxHeight, startPeak.height + dh);
            liveText = two (pk.height);
            break;
        case Mode::grit:
            pk.rough = juce::jlimit (0.0f, 1.0f, startPeak.rough + dh);
            liveText = "GRIT " + two (pk.rough);
            break;
        case Mode::width:
        {
            float r = startPeak.r();
            float x = 0, z = 0;
            if (! byNumber && groundAt && groundAt (p, lift, x, z)) r = std::hypot ((x - pk.x) * 2.0f, (z - pk.z) * 2.0f) * grabRatio;
            else r = startPeak.r() + d.x / juce::jmax (10.0f, pixelsPerUnitSize);
            r = juce::jlimit (4.0f * kMinWidth, 4.0f * kMaxWidth, r);
            const float k = r / juce::jmax (1e-4f, startPeak.r());
            pk.width = r * 0.25f;
            pk.depth = juce::jlimit (kMinDepth, kMaxDepth, startPeak.depth * k);   // (the time extent keeps its proportion)
            liveText = two (r);
            break;
        }
        case Mode::move:
        {
            float x = 0, z = 0;
            const bool ok = groundAt ? groundAt (p, lift, x, z) : (pick && pick (p, x, z));
            juce::ignoreUnused (ok);
            float nx = x - grabX, nz = z - grabZ;
            if (! groundAt && pick) { nx = startPeak.x + (x - startX); nz = startPeak.z + (z - startZ); }
            // held on the island (the reference keeps every mountain's centre inside the shore)
            float u = 2.0f * nx - 1.0f, v = 2.0f * nz - 1.0f;
            const float rr = std::hypot (u, v);
            if (rr > 1.0f) { u /= rr; v /= rr; }
            pk.x = juce::jlimit (0.0f, 1.0f, 0.5f + 0.5f * u); pk.z = juce::jlimit (0.0f, 1.0f, 0.5f + 0.5f * v);
            liveText = {};
            break;
        }
        case Mode::none: case Mode::pending: break;
    }
    ed.changed();
}

void TerrainInteraction::mouseUp (juce::Point<float>, juce::ModifierKeys)
{
    const bool valid = index >= 0 && index < ed.terrain().size();
    if (mode == Mode::pending && ! moved)
    {
        // a tap on empty land: let go of the chosen one, or (none chosen) a new mountain right there
        if (ed.selected >= 0) ed.select (-1);
        else if (ed.terrain().size() < Terrain::kMaxPeaks)
        {
            ed.beginGesture();
            auto& t = ed.live();
            Peak np; np.x = startX; np.z = startZ; np.height = 0.30f; np.width = np.depth = 0.05f;
            t.add (np);
            ed.selected = t.size() - 1;
            ed.changed();
            ed.endGesture();
        }
        mode = Mode::none;
        return;
    }
    if (mode == Mode::create && valid && std::abs (ed.terrain()[index].height) < 0.03f)
    {
        ed.live().remove (index);   // a pull that ended flat makes nothing
        ed.selected = -1;
        ed.changed();
    }
    if (! moved && valid && (mode == Mode::height || mode == Mode::width)) clicked = mode == Mode::height ? Kind::height : Kind::size;
    if (mode != Mode::none && mode != Mode::pending) ed.endGesture();
    mode = Mode::none;
    liveText = {};
}

void TerrainInteraction::doubleClick (juce::Point<float> p)
{
    int i = hit ? hit (p).peak : (handleAt ? handleAt (p) : -1);
    if (i < 0 && bodyAt) i = bodyAt (p);
    if (i < 0) return;
    ed.apply ([i] (Terrain& t) { t.remove (i); });
    ed.selected = -1;
}

bool TerrainInteraction::removeSelected()
{
    const int i = ed.selected;
    if (i < 0 || i >= ed.terrain().size()) return false;
    ed.apply ([i] (Terrain& t) { t.remove (i); });
    ed.selected = -1;
    return true;
}

void TerrainInteraction::wheel (juce::Point<float> p, float deltaY)
{
    int i = hit ? hit (p).peak : (handleAt ? handleAt (p) : -1);
    if (i < 0 && bodyAt) i = bodyAt (p);
    if (i < 0 || std::abs (deltaY) < 1e-4f) return;
    ed.selected = i;
    ed.apply ([i, deltaY] (Terrain& t) { const float k = std::exp (-deltaY * 0.6f); t[i].width *= k; t[i].depth *= k; });
}

void TerrainInteraction::magnify (juce::Point<float> p, float scale)
{
    int i = bodyAt ? bodyAt (p) : -1;
    if (i < 0) i = ed.selected;
    if (i < 0 || i >= ed.terrain().size() || std::abs (scale - 1.0f) < 1e-3f) return;
    ed.selected = i;
    ed.apply ([i, scale] (Terrain& t) { t[i].width *= scale; t[i].depth *= scale; });
}
}
