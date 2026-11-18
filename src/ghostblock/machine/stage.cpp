/*
 * Poltergeist 2 (GhostBlock) - the ghost-machine editor: one page of the machine.
 *
 * Built from assets/<page>/layers.json: the painted layers in z order, what turns, swings, pumps, flickers and
 * lights (JUCE-NOTES.md, section 5; the numbers are the mock-up's, src/rt_engine.js), the emitters and their puffs.
 * A part is drawn again only when it has visibly moved: it marks the cells it covered and the cells it covers now.
 */
#include "ghostblock/machine/machine-impl.h"

namespace ghostblock::p2::machine
{

static FRect boundsOf(const juce::AffineTransform &t, FRect r)
{
    float xs[4] = {r.getX(), r.getRight(), r.getX(), r.getRight()}, ys[4] = {r.getY(), r.getY(), r.getBottom(), r.getBottom()};
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int i = 0; i < 4; ++i)
    {
        t.transformPoint(xs[i], ys[i]);
        x0 = juce::jmin(x0, xs[i]);
        x1 = juce::jmax(x1, xs[i]);
        y0 = juce::jmin(y0, ys[i]);
        y1 = juce::jmax(y1, ys[i]);
    }
    return FRect(x0, y0, x1 - x0, y1 - y0).expanded(1.0f);
}
static float moved(const juce::AffineTransform &a, const juce::AffineTransform &b, FRect r)
{
    float m = 0;
    for (auto p : {r.getTopLeft(), r.getTopRight(), r.getBottomLeft(), r.getBottomRight()})
    {
        auto q = p;
        a.transformPoint(p.x, p.y);
        b.transformPoint(q.x, q.y);
        m = juce::jmax(m, std::abs(p.x - q.x), std::abs(p.y - q.y));
    }
    return m;
}

void Stage::build(Assets &assets, int pg)
{
    as = &assets;
    page = pg;
    S = assets.S;
    nodes.clear();
    emitters.clear();
    parts.clear();
    covered.clear();
    const auto &J = assets.pages[(size_t)pg];
    const juce::String dir = assets.pageKeys[pg];
    uiAt = -1;
    hookAt = -1;
    if (auto *arr = J["layers"].getArray())
        for (auto &it : *arr)
        {
            const auto id = it["id"].toString();
            if (id == "ui")
            {
                uiAt = (int)nodes.size();
                continue;
            }
            if (!it.hasProperty("file") || id == "roll") // (the brass roll is not built: the plugin has no sequencer)
                continue;
            const auto path = Assets::resolvePath(dir, it["file"].toString());
            const float ps = numOf(it["png_scale"], 2);
            if (id == "lip")
            {
                lip = Sprite::fromBitmap(assets.bitmap(path, ps));
                lipX = numOf(it["x"]);
                lipY = numOf(it["y"]);
                continue;
            }
            if (id == "machine")
                hookAt = (int)nodes.size();
            LayerNode n;
            n.id = id;
            n.x = numOf(it["x"]);
            n.y = numOf(it["y"]);
            n.w = numOf(it["w"]);
            n.h = numOf(it["h"]);
            n.hidden = (bool)it["hidden"];
            const auto &a = it["anim"];
            const auto type = a["type"].toString();
            n.light = a["blend"].toString() == "add";
            n.period = numOf(a["period"], 1);
            n.dir = numOf(a["dir"], 1);
            n.gain = numOf(a["gain"], 1);
            n.op = a.hasProperty("op") ? (int)a["op"] : -1;
            n.corner = a.hasProperty("corner") ? (int)a["corner"] : -1;
            n.minA = numOf(a["min"], 0);
            n.maxA = numOf(a["max"], 1);
            const auto grp = a["group"].toString();
            n.group = grp == "jaw" ? gJaw : grp == "flank" ? gFlank : grp == "yard" ? gYard : grp == "orbit" ? gOrbit : grp == "wind" ? gWind : grp == "windmill" ? gWindmill : grp == "fxwarp" ? gFxWarp : grp == "fxscale" ? gFxScale : grp == "spectral" ? gSpectral : gNone;
            const auto src = a["source"].toString();
            if (type == "static")
                n.kind = LayerNode::Static;
            else if (type == "rotate")
            {
                n.kind = LayerNode::Rotate;
                n.cx = numOf(a["cx"]);
                n.cy = numOf(a["cy"]);
                n.crank = (bool)a["crank"];
                n.crankR = numOf(a["crank_r"]);
            }
            else if (type == "sway" || type == "pendulum")
            {
                n.kind = type == "sway" ? LayerNode::Sway : LayerNode::Pendulum;
                n.cx = numOf(a["px"]);
                n.cy = numOf(a["py"]);
                n.amp = numOf(a["amp"]);
                n.bias = numOf(a["bias"]);
                n.ph = numOf(a["phase"]) * juce::MathConstants<double>::twoPi;
            }
            else if (type == "frames")
            {
                n.kind = LayerNode::Frames;
                n.nframes = juce::jmax(1, (int)a["frames"]);
            }
            else if (type == "pump")
            {
                n.kind = LayerNode::Pump;
                n.source = src == "fxin" ? 7 : 0;
                n.travel = numOf(a["travel"]);
                n.ph = numOf(a["phase"]) * juce::MathConstants<double>::twoPi;
            }
            else if (type == "flap")
            {
                n.kind = LayerNode::Flap;
                n.hingeY = numOf(a["hinge_y"]);
                n.open0 = numOf(a["open"][0], 1);
                n.open1 = numOf(a["open"][1], 0.3f);
                n.pinX = numOf(a["pin"][0]);
                n.pinY = numOf(a["pin"][1]);
            }
            else if (type == "link")
            {
                n.kind = LayerNode::Link;
                n.ax = numOf(a["a"][0]);
                n.ay = numOf(a["a"][1]);
                n.bx = numOf(a["b"][0]);
                n.by = numOf(a["b"][1]);
            }
            else if (type == "flicker")
            {
                n.kind = LayerNode::Flicker;
                const auto st = a["style"].toString();
                n.style = st == "fire" ? 1 : st == "breathe" ? 2 : st == "lid" ? 3 : st == "candle" ? 4 : 0;
            }
            else if (type == "flare")
            {
                n.kind = LayerNode::Flare;
                n.source = src == "note" ? 1 : src == "morph" ? 2 : src == "title" ? 3 : src == "spectral" ? 4 : src == "fxwet" ? 5 : src == "fxfhigh" ? 6 : 0;
            }
            else if (type == "drift")
            { // mist: a wide soft veil; it is kept still (moving it would repaint a third of the picture for a haze)
                n.kind = LayerNode::Flare;
                n.source = 8;
                n.gain = numOf(a["alpha"], 1);
            }
            else if (type == "fxpole")
            { // a stack lamp of the town: lit while WarpCore runs and uses this pole
                n.kind = LayerNode::Flare;
                n.source = 9;
                n.op = (int)a["index"];
            }
            else if (type == "level")
            {
                n.kind = LayerNode::Level;
                n.source = src == "awake" ? 1 : src == "heard" ? 2 : 0;
            }
            else if (type == "select")
                n.kind = LayerNode::Select;
            else if (type == "corner")
                n.kind = LayerNode::Corner;
            else if (type == "beam")
            {
                n.kind = LayerNode::Beam;
                n.source = src == "fxfhigh" ? 6 : 0;
                n.cx = numOf(a["cx"]);
                n.cy = numOf(a["cy"]);
            }
            else if (type == "drop")
            {
                n.kind = LayerNode::Drop;
                n.travel = numOf(a["travel"]);
            }
            else if (type == "bellows")
            {
                n.kind = LayerNode::Bellows;
                n.anchorY = numOf(a["anchor_y"]);
                n.sq0 = numOf(a["squeeze"][0], 1);
                n.sq1 = numOf(a["squeeze"][1], 0.72f);
            }
            else if (type == "strike")
                n.kind = LayerNode::Strike;
            else
                n.kind = LayerNode::Static;

            switch (n.kind)
            {
            case LayerNode::Strike:
                n.cls = path; // loaded when the first lightning comes (it is a whole canvas of rim light)
                n.alpha = 0;
                break;
            case LayerNode::Static:
            case LayerNode::Flicker:
            case LayerNode::Flare:
            case LayerNode::Level:
            case LayerNode::Select:
            case LayerNode::Corner:
            case LayerNode::Pump:
            case LayerNode::Drop:
                n.spr = Sprite::fromBitmap(assets.bitmap(path, ps));
                n.home = cellsOf(n.spr, n.x, n.y, S);
                n.alpha = n.kind == LayerNode::Static || n.kind == LayerNode::Pump || n.kind == LayerNode::Drop ? 1.0f : 0.0f;
                break;
            case LayerNode::Frames:
            {
                const auto strip = assets.bitmap(path, ps);
                const int fw = juce::jmax(1, strip.w / n.nframes);
                for (int f = 0; f < n.nframes; ++f)
                    n.frames.push_back(Sprite::fromBitmap(strip.cropped({f * fw, 0, fw, strip.h})));
                n.home = cellsOf(FRect(n.x, n.y, n.w, n.h));
                n.ph = (double)(nodes.size() % 7) / 7.0;
                break;
            }
            default:
                n.bmp = assets.bitmap(path, ps);
                {
                    const auto cb = n.bmp.contentBounds();
                    n.content = FRect((float)cb.getX() / S, (float)cb.getY() / S, (float)cb.getWidth() / S, (float)cb.getHeight() / S);
                }
                n.xf = n.drawnXf = juce::AffineTransform::translation(n.x, n.y);
                n.cells = cellsOf(boundsOf(n.xf, n.content));
                n.slow = n.w * n.h > 150.0f * 150.0f;
                break;
            }
            if (n.kind == LayerNode::Flicker)
                n.ph = (double)((nodes.size() * 37) % 100) / 100.0 * 6.28;
            nodes.push_back(std::move(n));
        }
    if (uiAt < 0)
        uiAt = (int)nodes.size();

    // ---- the cells the painted hull hides completely (no sky to keep up to date there)
    {
        const int w = CW * S, h = CH * S;
        std::vector<uint8_t> cov((size_t)w * (size_t)h, 0);
        for (auto &n : nodes)
        {
            if (n.kind != LayerNode::Static || n.id.startsWith("face-") || n.spr.empty())
                continue;
            const int ox = (int)std::lround(n.x * S), oy = (int)std::lround(n.y * S);
            for (int r = 0; r < n.spr.box.getHeight(); ++r)
            {
                const int y = oy + n.spr.box.getY() + r;
                if (y < 0 || y >= h)
                    continue;
                for (uint32_t k = n.spr.rowStart[(size_t)r]; k < n.spr.rowStart[(size_t)r + 1]; ++k)
                {
                    const auto &q = n.spr.spans[k];
                    if (!q.solid)
                        continue;
                    const int a = juce::jmax(0, ox + q.x), b = juce::jmin(w, ox + q.x + q.n);
                    if (b > a)
                        std::memset(cov.data() + (size_t)y * (size_t)w + (size_t)a, 1, (size_t)(b - a));
                }
            }
        }
        const int cs = CELL * S;
        for (int cy = 0; cy < ROWS; ++cy)
            for (int cx = 0; cx < COLS; ++cx)
            {
                bool all = true;
                for (int y = cy * cs; y < juce::jmin(h, (cy + 1) * cs) && all; ++y)
                {
                    const uint8_t *row = cov.data() + (size_t)y * (size_t)w;
                    for (int x = cx * cs; x < juce::jmin(w, (cx + 1) * cs); ++x)
                        if (!row[x])
                        {
                            all = false;
                            break;
                        }
                }
                if (all)
                    covered.set(cy * COLS + cx);
            }
    }

    if (auto *arr = J["emitters"].getArray())
        for (auto &it : *arr)
        {
            Emitter e;
            e.x = numOf(it["x"]);
            e.y = numOf(it["y"]);
            e.size = numOf(it["size"], 1);
            e.dx = numOf(it["dx"]);
            e.dy = numOf(it["dy"]);
            e.rate = numOf(it["rate"], 1);
            const auto k = it["kind"].toString();
            e.kind = k == "black" ? pkBlack : k == "steam" ? pkSteam : k == "breath" ? pkBreath : k == "stream" ? pkStream : k == "spark" ? pkSpark : k == "drip" ? pkDrip : pkSmoke;
            e.op = it.hasProperty("op") ? (int)it["op"] : -1;
            e.voice = it.hasProperty("voice") ? (int)it["voice"] : -1;
            e.pole = it.hasProperty("pole") ? (int)it["pole"] : -1;
            e.fxin = it["source"].toString() == "fxin";
            e.acc = (double)((emitters.size() * 53) % 100) / 100.0;
            emitters.push_back(e);
        }

    // puffs: 0..2 steam, 3..5 smoke, 6..8 black smoke, 9 spark, 10 drip
    puffs.clear();
    for (int i = 0; i < 6; ++i)
        puffs.push_back(assets.bitmap("common/sprites/puff" + juce::String(i) + ".png", 2.0));
    for (int i = 3; i < 6; ++i)
    {
        auto b = puffs[(size_t)i];
        b.brighten(0.28f);
        puffs.push_back(std::move(b));
    }
    puffs.push_back(assets.renderBitmap(16, 16, [](juce::Graphics &g) {
        juce::ColourGradient gr(juce::Colours::white, 8, 8, juce::Colour(0x00ffa028), 8, 13.6f, true);
        gr.addColour(0.57, juce::Colour(0xffffd27a));
        g.setGradientFill(gr);
        g.fillEllipse(0, 0, 16, 16);
    }));
    puffs.push_back(assets.renderBitmap(4, 6, [](juce::Graphics &g) {
        g.setColour(juce::Colour(0xffa6e0cf));
        g.fillEllipse(0.9f, 1.2f, 2.2f, 3.6f);
    }));
    face = 1;
    phi = belPh = penPh = beamPh = 0;
}

LayerNode *Stage::find(const juce::String &id)
{
    for (auto &n : nodes)
        if (n.id == id)
            return &n;
    return nullptr;
}

void Stage::setFace(int f, CellSet &dirty)
{
    if (page != 0)
        return;
    face = f;
    for (auto &n : nodes)
        if (n.id.startsWith("face-"))
        {
            const bool hide = n.id == "face-ghost" ? f != 1 : f != 2;
            if (hide != n.hidden)
            {
                n.hidden = hide;
                dirty.mark(n.home);
            }
        }
}

// ============================================================================================== the clock
void Stage::tick(Env &G, float dt, CellSet &dirty, juce::Random &rng, float strike, bool particlesOn)
{
    const float a = G.act, s = G.storm, I = Env::IDLE;
    const double tw = juce::MathConstants<double>::twoPi;
    phi += dt * tw / 3.2 * (1.0 + (a - I) * 5.0 + G.note * 2.0);
    belPh += dt * (1.2 + a * 5.0 + G.note * 6.0);
    penPh += dt * tw * (0.25 + 1.6 * G.driftRate);
    beamPh += dt * tw / 7.0 * (1.0 + s);
    const float sw = 1.0f + a * 1.2f + s * 0.8f;
    float hatchSy = 1.0f;
    const LayerNode *crankNode = nullptr, *hatchNode = nullptr;
    for (auto &n : nodes)
    {
        if (n.kind == LayerNode::Rotate && n.crank)
            crankNode = &n;
        if (n.kind == LayerNode::Flap)
        {
            hatchNode = &n;
            const float open = juce::jmin(1.0f, juce::jmax(G.note, a * (0.15f + 0.85f * (0.5f - 0.5f * (float)std::cos(phi))) * 0.8f));
            hatchSy = n.open0 + (n.open1 - n.open0) * open;
        }
    }
    for (auto &n : nodes)
    {
        if (n.hidden)
            continue;
        const auto base = juce::AffineTransform::translation(n.x, n.y);
        bool affine = false;
        float al = n.alpha;
        switch (n.kind)
        {
        case LayerNode::Static:
        case LayerNode::Skip:
            continue;
        case LayerNode::Rotate:
        {
            double ang;
            if (n.crank)
                ang = phi;
            else if (n.group == gJaw)
                ang = -phi * 10.0 / 6.0;
            else
            {
                float m = 1.0f + (a - I) * 6.0f + G.boost[n.group] * 6.0f;
                if (n.group == gOrbit)
                    m = G.orbit ? 2.2f : G.boost[gOrbit] * 3.0f;
                else if (n.group == gWind)
                    m = 1.0f + s * 4.0f + (a - I) * 2.0f;
                else if (n.group == gWindmill)
                    m = (0.35f + 2.6f * G.driftRate) * (1.0f + s * 1.5f) + G.boost[gWindmill] * 2.0f;
                else if (n.group == gFxWarp)
                    m = G.fx.warpOn ? 0.5f + 5.0f * G.fx.warp01 : 0.25f;
                else if (n.group == gFxScale)
                    m = G.fx.warpOn ? 0.3f + 3.2f * G.fx.scale01 : 0.15f;
                else if (n.group == gSpectral)
                    m = G.fx.on ? 0.5f + 4.0f * G.fx.spectral : 0.12f;
                n.ph += dt * tw / n.period * n.dir * m;
                ang = n.ph;
            }
            n.xf = base.rotated((float)ang, n.cx, n.cy);
            affine = true;
            break;
        }
        case LayerNode::Sway:
            n.ph += dt * tw / n.period * (1.0 + (a - I) * 1.2 + s * 0.6);
            n.xf = base.rotated(juce::degreesToRadians(n.amp * sw * (float)std::sin(n.ph) + n.bias * (1.0f + s * 2.5f)), n.cx, n.cy);
            affine = true;
            break;
        case LayerNode::Pendulum:
            n.xf = base.rotated(juce::degreesToRadians(n.amp * (0.12f + 0.88f * std::abs(G.driftAmt)) * (float)std::sin(penPh)), n.cx, n.cy);
            affine = true;
            break;
        case LayerNode::Frames:
            n.ph += dt / n.period * (1.0 + s * 1.2);
            n.ph -= std::floor(n.ph);
            n.frame = juce::jlimit(0, n.nframes - 1, (int)std::floor(n.ph * n.nframes));
            if (n.frame != n.drawnFrame)
            {
                n.drawnFrame = n.frame;
                dirty.mark(n.home);
            }
            continue;
        case LayerNode::Pump:
        {
            const float lv = n.source == 7 ? juce::jmin(1.0f, G.fx.in * 1.6f) : juce::jmin(1.0f, juce::jmax(a * 0.8f, n.op >= 0 ? G.ops[n.op].heard * 1.6f : 0.f));
            n.ph += dt * tw / n.period * (0.7 + (a - I) * 3.0 + lv);
            n.ty = -n.travel * lv * (0.5f - 0.5f * (float)std::cos(n.ph));
            break;
        }
        case LayerNode::Drop:
            n.ty = n.travel * juce::jmin(1.0f, std::abs(G.pdAmt)) * G.pdEnv;
            break;
        case LayerNode::Flap:
            n.xf = base.followedBy(juce::AffineTransform::scale(1.0f, hatchSy, n.x + n.w * 0.5f, n.hingeY));
            affine = true;
            break;
        case LayerNode::Link:
        {
            if (crankNode == nullptr || hatchNode == nullptr)
                continue;
            const float ax = crankNode->cx + crankNode->crankR * (float)std::cos(phi), ay = crankNode->cy + crankNode->crankR * (float)std::sin(phi);
            const float bx = hatchNode->pinX, by = hatchNode->hingeY - (hatchNode->hingeY - hatchNode->pinY) * hatchSy;
            const float a0 = std::atan2(n.by - n.ay, n.bx - n.ax), l0 = std::hypot(n.bx - n.ax, n.by - n.ay);
            const float an = std::atan2(by - ay, bx - ax), ln = std::hypot(bx - ax, by - ay);
            n.xf = base.rotated(-a0, n.ax, n.ay).followedBy(juce::AffineTransform::scale(ln / juce::jmax(0.01f, l0), 1.0f, n.ax, n.ay)).rotated(an, n.ax, n.ay).translated(ax - n.ax, ay - n.ay);
            affine = true;
            break;
        }
        case LayerNode::Bellows:
            n.xf = base.followedBy(juce::AffineTransform::scale(1.0f, n.sq0 + (n.sq1 - n.sq0) * (0.5f - 0.5f * (float)std::cos(belPh)) * juce::jmin(1.0f, 0.25f + a + G.note + G.drive * 0.4f), n.x + n.w * 0.5f, n.anchorY));
            affine = true;
            break;
        case LayerNode::Beam:
        {
            const float c = (float)std::cos(beamPh);
            n.xf = base.followedBy(juce::AffineTransform::scale(std::abs(c) < 0.02f ? 0.02f : c, 1.0f, n.cx, n.cy));
            al = std::abs(c) * (n.source == 6 ? (G.fx.warpOn ? 0.25f + 0.75f * G.fx.fhigh : 0.06f) : juce::jmin(1.0f, 0.16f + 0.9f * juce::jmax(G.out, (a - I) * 0.7f)));
            affine = true;
            break;
        }
        case LayerNode::Flare:
        {
            if (n.source >= 4)
            { // SPECTRAL FX: the ghost's eyes, his mouth, the lighthouse lamp, the mist, the stack lamps
                const float fv = n.source == 4 ? (G.fx.on ? 0.25f + 0.75f * G.fx.spectral : 0.0f) : n.source == 5 ? (G.fx.on ? G.fx.wet : 0.0f) : n.source == 6 ? (G.fx.warpOn ? 0.3f + 0.7f * G.fx.fhigh : 0.0f) : n.source == 8 ? 1.0f : (G.fx.warpOn && n.op < G.fx.poles ? 1.0f : 0.0f);
                al = juce::jlimit(0.0f, 1.0f, fv * n.gain);
                break;
            }
            const float v = n.source == 1 ? juce::jmax(G.note, (a - I) * 0.8f) : n.source == 2 ? (G.morphOn ? 1.0f : 0.0f) : n.source == 3 ? 0.35f + 0.65f * juce::jmax(G.out, a) : juce::jmax(G.out, (a - I) * 0.9f);
            al = juce::jlimit(0.0f, 1.0f, v * n.gain);
            break;
        }
        case LayerNode::Level:
        {
            const auto &o = G.ops[juce::jlimit(0, (int)ss::numOps - 1, n.op)];
            float v;
            if (n.source == 1)
                v = o.awake ? 0.62f + 0.16f * (float)(std::sin(G.t * 7.3 + n.op * 2.1) * std::sin(G.t * 3.1 + n.op)) : 0.f;
            else if (n.source == 2)
                v = juce::jmin(1.0f, o.heard * 1.5f);
            else
                v = juce::jmin(1.0f, o.level * 1.5f);
            al = v * n.gain;
            break;
        }
        case LayerNode::Corner:
            al = juce::jmin(1.0f, (G.morphOn ? 1.0f : 0.0f) * (0.12f + 1.5f * G.corner[juce::jlimit(0, Morph::N - 1, n.corner)]) * (0.8f + 0.2f * (float)std::sin(G.t * 5.0 + n.corner)));
            break;
        case LayerNode::Select:
            al = (G.sel == n.op && G.selLink < 0) ? 0.75f + 0.25f * (float)std::sin(G.t * 4.0) : 0.f;
            break;
        case LayerNode::Flicker:
        {
            n.ph += dt;
            if (n.style == 3)
            {
                const double c = std::fmod(n.ph, (double)n.period) / n.period;
                al = c > 0.985 ? 1.0f : 0.0f;
                break;
            }
            const double p = n.ph * tw / n.period;
            float v = n.style == 1 ? 0.5f + 0.5f * (float)(0.6 * std::sin(p * 3.1) + 0.4 * std::sin(p * 7.7 + 1.1)) : n.style == 2 ? 0.5f + 0.5f * (float)std::sin(p) : 0.5f + 0.5f * (float)(0.7 * std::sin(p) + 0.3 * std::sin(p * 5.3 + 0.7));
            if (n.style == 4 && std::sin(p * 0.37 + n.ph) > 0.97)
                v *= 0.4f;
            al = n.minA + (n.maxA - n.minA) * v;
            break;
        }
        case LayerNode::Strike:
            al = strike * 0.7f * n.gain; // capped
            if (al > 0.003f && n.spr.empty() && n.cls.isNotEmpty())
            {
                n.spr = Sprite::fromBitmap(as->bitmap(n.cls, 2.0));
                n.home = cellsOf(n.spr, n.x, n.y, S);
                n.cls.clear();
            }
            break;
        }
        n.alpha = al;
        if (affine)
        {
            n.since += dt;
            const bool alphaMoved = n.kind == LayerNode::Beam && std::abs(n.alpha - n.drawnAlpha) > 0.03f;
            if ((moved(n.xf, n.drawnXf, n.content) > 0.3f || alphaMoved) && (!n.slow || n.since >= 1.0 / 15.0))
            {
                n.since = 0;
                dirty.mark(n.cells);
                n.drawnXf = n.xf;
                n.drawnAlpha = n.alpha;
                n.cells = cellsOf(boundsOf(n.xf, n.content));
                dirty.mark(n.cells);
            }
        }
        else if (n.kind == LayerNode::Pump || n.kind == LayerNode::Drop)
        {
            const float q = std::round(n.ty * (float)S) / (float)S;
            if (q != n.drawnTy)
            {
                dirty.mark(n.cells.empty() ? n.home : n.cells);
                n.drawnTy = q;
                n.cells = cellsOf(n.spr, n.x, n.y + q, S);
                dirty.mark(n.cells);
            }
        }
        else if (std::abs(n.alpha - n.drawnAlpha) > 0.025f || ((n.alpha <= 0.003f) != (n.drawnAlpha <= 0.003f)))
        {
            n.drawnAlpha = n.alpha;
            dirty.mark(n.home);
        }
    }

    // ---- emitters
    if (particlesOn)
        for (auto &e : emitters)
        {
            double rate = e.rate * (1.0 + (a - I) * 1.8) * (e.kind == pkSteam ? 0.7 : e.kind == pkStream ? (0.9 + s * 1.5) * 0.45 : 0.5);
            if (e.op >= 0)
                rate = e.rate * (0.12 + 1.6 * G.ops[e.op].heard + 0.5 * (a - I)); // the organ rank smokes for the ghosts you hear
            if (e.voice >= 0)
                rate = e.rate * (G.voices > e.voice ? 1.5 + 0.5 * a : 0.06); // the town stacks smoke for the voices that sound
            if (e.pole >= 0)
                rate = e.rate * (G.fx.warpOn && e.pole < G.fx.poles ? 0.9 : 0.03); // FX: a stack smokes while WarpCore uses its pole
            if (e.fxin)
                rate = e.rate * (0.1 + 1.6 * juce::jmin(1.0f, G.fx.in));
            if (e.kind == pkBreath)
                rate = 0.25 + G.note * 1.4;
            if (e.kind == pkSpark)
                rate = (std::sin(G.t * 0.8 + e.x) > 0.3 ? 7.0 : 0.0) * e.rate;
            if (e.kind == pkDrip)
                rate = e.rate * (0.4 + 0.6 * a);
            e.acc += dt * rate;
            if (e.acc >= 1.0)
            {
                e.acc = 0;
                puff(e.x, e.y, e.kind, e.size, e.dx, e.dy, G, rng);
            }
        }
    // ---- the puffs drift, grow and fade
    for (auto &p : parts)
    {
        p.age += dt;
        FRect r;
        const float u = p.age / p.dur;
        if (u < 1.0f)
        {
            if (p.kind == pkSpark)
            {
                if (p.img < 0)
                { // the flash
                    p.op = 0.85f * (u < 0.3f ? u / 0.3f : 1.0f - (u - 0.3f) / 0.7f);
                    r = FRect(p.x - 8, p.y - 8, 16, 16);
                }
                else
                {
                    const float t = p.age;
                    p.op = u < 0.75f ? 1.0f : 1.0f - (u - 0.75f) / 0.25f;
                    r = FRect(p.x + p.vx * t - p.base * 0.5f, p.y + p.vy * t + 80.0f * t * t - p.base * 0.5f, p.base, p.base);
                }
            }
            else if (p.kind == pkDrip)
            {
                const float e = u * u; // ease-in
                const float dy = e < 0.35f ? 0.f : e < 0.6f ? 5.0f * p.sx * (e - 0.35f) / 0.25f : (5.0f + 29.0f * (e - 0.6f) / 0.4f) * p.sx;
                p.op = e < 0.35f ? e / 0.35f : e < 0.6f ? 1.0f : 1.0f - (e - 0.6f) / 0.4f;
                r = FRect(p.x - 1.1f, p.y + dy, 2.2f, 3.6f * (e < 0.35f ? 0.4f + 0.6f * e / 0.35f : 1.0f));
            }
            else
            {
                float px, py, sc;
                if (u < 0.25f)
                {
                    const float k = u / 0.25f;
                    px = p.tx * 0.35f * k;
                    py = p.ty * 0.55f * k;
                    sc = 0.35f + 0.65f * k;
                    p.op = p.peak * k;
                }
                else
                {
                    const float k = (u - 0.25f) / 0.75f;
                    px = p.tx * (0.35f + 0.65f * k);
                    py = p.ty * (0.55f + 0.45f * k);
                    sc = 1.0f + (p.grow - 1.0f) * k;
                    p.op = p.peak * (1.0f - k);
                }
                const float w = p.base * sc * p.sx, h = p.base * sc;
                r = FRect(p.x + px - w * 0.5f, p.y + py - h * 0.5f, w, h);
            }
        }
        if (!p.drawn.isEmpty())
            dirty.mark(p.drawn.expanded(1));
        p.drawn = r;
        if (!r.isEmpty())
            dirty.mark(r.expanded(1));
    }
    parts.erase(std::remove_if(parts.begin(), parts.end(), [](const Particle &p) { return p.age >= p.dur; }), parts.end());
}

void Stage::puff(float x, float y, int kind, float size, float dx, float dy, const Env &G, juce::Random &rng)
{
    if (parts.size() > 90)
        return;
    Particle p;
    p.kind = kind;
    p.x = x;
    p.y = y;
    if (kind == pkSpark)
    {
        p.img = -1;
        p.dur = 0.15f;
        parts.push_back(p);
        const int n = 3 + rng.nextInt(4);
        for (int k = 0; k < n; ++k)
        {
            Particle q;
            q.kind = pkSpark;
            q.img = 9;
            q.x = x;
            q.y = y;
            q.base = 1.5f + rng.nextFloat() * 1.7f;
            q.vx = (rng.nextFloat() - 0.25f) * 40.0f * dx * size;
            q.vy = -(6.0f + rng.nextFloat() * 24.0f) * size;
            q.dur = 0.4f + rng.nextFloat() * 0.45f;
            parts.push_back(q);
        }
        return;
    }
    if (kind == pkDrip)
    {
        p.img = 10;
        p.sx = size;
        p.dur = 1.5f;
        parts.push_back(p);
        return;
    }
    const bool smoke = kind == pkSmoke || kind == pkBlack, stream = kind == pkStream;
    p.base = (stream ? 26.0f : smoke ? 16.0f : kind == pkBreath ? 13.0f : 10.0f) * size * (kind == pkBlack ? 1.5f : 1.0f) * (0.8f + G.act * 0.9f);
    p.img = (kind == pkBlack ? 6 : smoke ? 3 : 0) + rng.nextInt(3);
    const float wind = 1.0f + G.storm * 1.6f + (G.act - Env::IDLE);
    p.tx = (stream ? 150.0f : smoke ? 70.0f : 26.0f) * size * dx * wind + (stream ? 0.0f : 14.0f * wind);
    p.ty = (stream ? 20.0f : smoke ? 64.0f : 34.0f) * size * dy;
    p.dur = (stream ? 2.2f : smoke ? 5.2f : kind == pkBreath ? 2.6f : 3.0f) / (0.8f + G.storm * 0.8f);
    p.peak = stream ? 0.34f : smoke ? 0.7f : 0.75f;
    p.sx = stream ? 4.2f : 1.0f;
    p.grow = smoke ? 2.4f : 1.7f;
    parts.push_back(p);
}

// ============================================================================================== drawing
static void drawNode(const Ctx &c, const LayerNode &n)
{
    if (n.hidden)
        return;
    switch (n.kind)
    {
    case LayerNode::Static:
        c.sprite(n.spr, n.x, n.y, 1.0f, Blend::Over, c.dim);
        break;
    case LayerNode::Flicker:
    case LayerNode::Flare:
    case LayerNode::Level:
    case LayerNode::Select:
    case LayerNode::Corner:
    case LayerNode::Strike:
        if (n.drawnAlpha > 0.003f)
            c.sprite(n.spr, n.x, n.y, n.drawnAlpha, n.light ? Blend::Screen : Blend::Over);
        break;
    case LayerNode::Frames:
        if (!n.frames.empty())
            c.sprite(n.frames[(size_t)juce::jlimit(0, (int)n.frames.size() - 1, n.drawnFrame)], n.x, n.y);
        break;
    case LayerNode::Pump:
    case LayerNode::Drop:
        c.sprite(n.spr, n.x, n.y + (n.drawnTy > -1e8f ? n.drawnTy : 0.f));
        break;
    case LayerNode::Skip:
        break;
    default:
        c.affine(n.bmp, n.drawnXf, n.kind == LayerNode::Beam ? juce::jmax(0.f, n.drawnAlpha) : 1.0f, n.light ? Blend::Screen : Blend::Over);
        break;
    }
}

void Stage::drawBack(const Ctx &c) const
{
    for (int i = 0; i < uiAt; ++i)
    {
        if (i == hookAt && hook)
            hook(c); // (FX: the hose between the two craft lies behind the bridge and the workbench)
        drawNode(c, nodes[(size_t)i]);
    }
}

void Stage::drawFront(const Ctx &c) const
{
    c.sprite(lip, lipX, lipY, 1.0f, Blend::Over, c.dim);
    for (size_t i = (size_t)uiAt; i < nodes.size(); ++i)
        drawNode(c, nodes[i]);
    for (auto &p : parts)
    {
        if (p.drawn.isEmpty() || p.op <= 0.004f || !c.touches(p.drawn))
            continue;
        if (p.kind == pkSpark)
        {
            const auto &b = puffs[9];
            c.affine(b, juce::AffineTransform::scale(p.drawn.getWidth() / 16.0f, p.drawn.getHeight() / 16.0f).translated(p.drawn.getX(), p.drawn.getY()), p.op, Blend::Screen);
        }
        else if (p.kind == pkDrip)
            c.affine(puffs[10], juce::AffineTransform::scale(1.0f, p.drawn.getHeight() / 3.6f).translated(p.drawn.getX() - 0.9f, p.drawn.getY() - 1.2f), p.op);
        else
        {
            const auto &b = puffs[(size_t)juce::jlimit(0, 8, p.img)];
            c.affine(b, juce::AffineTransform::scale(p.drawn.getWidth() / 64.0f, p.drawn.getHeight() / 64.0f).translated(p.drawn.getX(), p.drawn.getY()), p.op);
        }
    }
}
} // namespace ghostblock::p2::machine
