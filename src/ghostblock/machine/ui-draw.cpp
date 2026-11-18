/*
 * Poltergeist 2 (GhostBlock) - the ghost-machine editor: drawing the controls.
 *
 * Two passes over the cells that changed: the art (backlights, glows, the moving parts, hoses, rails) goes down before
 * the SKY dial's colour grade, the live text after it, so the labels keep their colour in every sky. The light is the
 * design's one rule (JUCE-NOTES.md, 8.1): a dim amber backlight at rest, brighter under the pointer, full while held,
 * a flash that fades over about 600 ms when touched, and it stays lit when on.
 */
#include "ghostblock/machine/machine-impl.h"
#include "synth/matrix_index.h"

namespace ghostblock::p2::machine
{
namespace
{
struct LStyle
{
    float size;
    bool semi;
    float track;
    juce::uint32 col;
    bool left;
    float pad;
    bool backlight, shadow;
};
const LStyle &styleOf(LCls c)
{
    static const LStyle st[] = {
        {7.8f, true, 0.05f, 0xffe6c98a, false, 0, true, true},    // base
        {15.0f, false, 0.03f, 0xfffff0cc, true, 14, true, true},  // name
        {8.8f, false, 0.01f, 0xffb6f0bc, true, 7, true, true},    // status
        {13.0f, true, 0.10f, 0xffefe4c4, true, 9, true, true},    // bench title
        {10.5f, true, 0.05f, 0xffe6c98a, false, 0, true, true},   // num
        {6.8f, false, 0.04f, 0xffe6c98a, false, 0, true, true},   // state
        {10.0f, true, 0.14f, 0xffd9c79a, false, 0, false, true},  // sign
        {10.0f, false, 0.02f, 0xfffff0cc, true, 8, false, true},  // drum
        {10.5f, false, 0.02f, 0xfffff0cc, true, 8, false, true},  // sound plate
        {7.6f, true, 0.06f, 0xffe6c98a, false, 0, true, true},    // button
        {7.0f, true, 0.12f, 0xffffd98a, false, 0, true, true},    // tag
        {8.0f, true, 0.05f, 0xffe6c98a, false, 0, true, true},    // weight
        {5.8f, false, 0.05f, 0xffa89468, false, 0, false, true},  // tiny
        {6.6f, true, 0.05f, 0xfffff0cc, false, 0, false, true},   // valve tag
        {11.0f, true, 0.05f, 0xff0e0b06, false, 0, false, false}, // lamp letter
        {8.6f, true, 0.10f, 0xffe6c98a, false, 0, false, true},   // link view
    };
    return st[juce::jlimit(0, (int)lcLv, (int)c)];
}
} // namespace
const juce::Colour kAmber(0xffffb23e);

juce::Path bezierPath(const FPoint g[4])
{
    juce::Path p;
    p.startNewSubPath(g[0]);
    p.cubicTo(g[1], g[2], g[3]);
    return p;
}
FPoint bezAt(const FPoint g[4], float t)
{
    const float m = 1 - t;
    return g[0] * (m * m * m) + g[1] * (3 * m * m * t) + g[2] * (3 * m * t * t) + g[3] * (t * t * t);
}
// dashes that travel: `on` px lit every `period` px, starting `offset` px along the curve
juce::Path dashes(const FPoint g[4], float on, float period, float offset)
{
    constexpr int N = 28;
    FPoint pts[N + 1];
    float len[N + 1];
    len[0] = 0;
    pts[0] = g[0];
    for (int i = 1; i <= N; ++i)
    {
        pts[i] = bezAt(g, (float)i / (float)N);
        len[i] = len[i - 1] + pts[i].getDistanceFrom(pts[i - 1]);
    }
    auto at = [&](float s) {
        s = juce::jlimit(0.0f, len[N], s);
        int i = 1;
        while (i < N && len[i] < s)
            ++i;
        const float seg = juce::jmax(1e-4f, len[i] - len[i - 1]);
        return pts[i - 1] + (pts[i] - pts[i - 1]) * ((s - len[i - 1]) / seg);
    };
    juce::Path p;
    for (float s = std::fmod(offset, period) - period; s < len[N]; s += period)
    {
        const float a = juce::jmax(0.0f, s), b = juce::jmin(len[N], s + on);
        if (b - a < 0.3f)
            continue;
        p.startNewSubPath(at(a));
        for (float q = a + 2.0f; q < b; q += 2.0f)
            p.lineTo(at(q));
        p.lineTo(at(b));
    }
    return p;
}
juce::Colour shade(juce::Colour c, float k)
{ // the mock-up's shade(): toward white for k > 0, darker for k < 0
    auto ch = [k](juce::uint8 v) { return (juce::uint8)juce::jlimit(0, 255, (int)std::lround(k > 0 ? (float)v + (255.0f - (float)v) * k : (float)v * (1.0f + k * 0.75f))); };
    return juce::Colour(ch(c.getRed()), ch(c.getGreen()), ch(c.getBlue()));
}


// ============================================================================================== small made parts
static const Sprite &lampSprite(Assets &as, juce::Colour col, float size, bool halo)
{
    const juce::String key = "lamp" + col.toString() + juce::String(size) + (halo ? "h" : "");
    auto it = as.made.find(key);
    if (it != as.made.end())
        return it->second;
    const float pad = 12.0f, full = size + pad * 2;
    auto spr = as.render(full, full, [&](juce::Graphics &g) {
        const float c = full * 0.5f;
        if (halo)
        { // box-shadow: 0 0 9px 1px
            juce::ColourGradient gl(col.withAlpha(0.75f), c, c, col.withAlpha(0.0f), c, c + size * 0.5f + 10.0f, true);
            gl.addColour(juce::jlimit(0.01, 0.99, (double)(size * 0.5f) / (double)(size * 0.5f + 10.0f)), col.withAlpha(0.62f));
            g.setGradientFill(gl);
            g.fillEllipse(c - size * 0.5f - 10, c - size * 0.5f - 10, size + 20, size + 20);
        }
        // radial-gradient(circle at 40% 35%, #fff, colour 46%, rgba(0,0,0,.55) 100%)
        const float gx = pad + size * 0.4f, gy = pad + size * 0.35f, rad = size * 0.885f;
        juce::ColourGradient gr(juce::Colours::white, gx, gy, juce::Colour(0x8c000000), gx, gy + rad, true);
        gr.addColour(0.46, col);
        g.setGradientFill(gr);
        g.fillEllipse(pad, pad, size, size);
    });
    return as.made.emplace(key, std::move(spr)).first->second;
}

// one bead of the light that runs along a hose: a soft glow in the ghost's colour with a pale core
const Sprite &pulseSprite(Assets &as, juce::Colour col)
{
    const juce::String key = "pulse" + col.toString();
    auto it = as.made.find(key);
    if (it != as.made.end())
        return it->second;
    auto spr = as.render(14.0f, 14.0f, [&](juce::Graphics &g) {
        juce::ColourGradient gl(col.withAlpha(0.55f), 7, 7, col.withAlpha(0.0f), 7, 13.5f, true);
        gl.addColour(0.45, col.withAlpha(0.28f));
        g.setGradientFill(gl);
        g.fillEllipse(0.5f, 0.5f, 13, 13);
        g.setColour(juce::Colour(0xfffff6e0).withAlpha(0.55f));
        g.fillEllipse(6.0f, 6.0f, 2.0f, 2.0f);
    });
    return as.made.emplace(key, std::move(spr)).first->second;
}

static const Sprite &seatSprite(Assets &as)
{
    auto it = as.made.find("seat");
    if (it != as.made.end())
        return it->second;
    auto spr = as.render(21, 21, [](juce::Graphics &g) {
        juce::ColourGradient gr(juce::Colour(0xff6a5a34), 1 + 19 * 0.4f, 1 + 19 * 0.35f, juce::Colour(0xff2e2716), 1 + 19 * 0.4f, 1 + 19 * 0.35f + 19 * 0.62f, true);
        g.setGradientFill(gr);
        g.fillEllipse(1, 1, 19, 19);
        g.setColour(juce::Colour(0xff17110e));
        g.drawEllipse(1.5f, 1.5f, 18, 18, 1.0f);
    });
    return as.made.emplace("seat", std::move(spr)).first->second;
}

static const Bitmap &haloOf(Assets &as, Kit &k)
{
    if (k.halo.empty() && !k.bmp.empty())
    {
        k.haloPad = 5 * as.S;
        k.halo = blurredAlpha(k.bmp, 1.6f * (float)as.S, juce::Colour(255, 193, 93), 1.35f, k.haloPad);
    }
    return k.halo;
}

void MachineEditor::Impl::drawGlow(const Ctx &c, const juce::String &kit, FPoint at, float size, float level)
{
    if (level <= 0.01f || size <= 0)
        return;
    const auto &k = as->kit(kit);
    c.affine(k.bmp, juce::AffineTransform::scale(size / k.w, size / k.h).translated(at.x - size * 0.5f, at.y - size * 0.5f), juce::jmin(1.0f, level), Blend::Screen, juce::jmax(1.0f, level));
}

// the amber light behind a label plate: radial-gradient(ellipse at 50% 60%, rgba(255,178,62,.55), transparent 72%)
void MachineEditor::Impl::drawBacklight(const Ctx &c, FRect r, float level, bool green)
{
    if (level <= 0.01f || r.isEmpty())
        return;
    const juce::String key = "bl" + juce::String(juce::roundToInt(r.getWidth() * 2)) + "x" + juce::String(juce::roundToInt(r.getHeight() * 2)) + (green ? "g" : "a");
    auto it = as->made.find(key);
    if (it == as->made.end())
    {
        const float w = r.getWidth(), h = r.getHeight();
        const auto col = green ? juce::Colour(120, 230, 140) : juce::Colour(255, 178, 62);
        const float a0 = green ? 0.35f : 0.55f;
        auto spr = as->render(w, h, [&](juce::Graphics &g) {
            g.reduceClipRegion(juce::Rectangle<int>(0, 0, (int)std::ceil(w), (int)std::ceil(h)));
            g.addTransform(juce::AffineTransform::scale(w * 0.7071f * 0.72f, h * 0.6f * 1.4142f * 0.72f).translated(w * 0.5f, h * 0.6f));
            g.setGradientFill(juce::ColourGradient(col.withAlpha(a0), 0, 0, col.withAlpha(0.0f), 1, 0, true));
            g.fillRect(-4.0f, -4.0f, 8.0f, 8.0f);
        });
        it = as->made.emplace(key, std::move(spr)).first;
    }
    c.sprite(it->second, r.getX(), r.getY(), juce::jmin(1.0f, level));
}

float MachineEditor::Impl::glowLevel(const Ctl &k, float &fl) const
{
    const double dt = clock - k.flashAt;
    fl = (dt >= 0 && dt < 0.6) ? (float)((1.0 - dt / 0.6) * (1.0 - dt / 0.6)) : 0.0f;
    const bool socket = k.type == CType::Switch || k.type == CType::Socket;
    float base = socket ? 0.0f : 0.2f;
    if (k.hover)
        base = socket ? 0.5f : 0.8f;
    if (k.on || (k.type == CType::Lantern && k.v01 > 0.5f && false))
        base = 0.9f;
    if (k.held)
        base = 1.5f;
    return base;
}

// ============================================================================================== labels
void MachineEditor::Impl::drawLabel(const Ctx &c, FRect r, LCls cls, bool two, const juce::String &a, const juce::String &b, float /*light*/, bool hov, bool on, bool off, float fl, juce::Colour over)
{
    if (!c.touches(r.expanded(7, 6)))
        return;
    const auto &L = styleOf(cls);
    TextStyle st;
    st.size = L.size;
    st.semi = L.semi;
    st.track = L.track;
    st.shadow = L.shadow;
    juce::Colour col(L.col);
    if (!over.isTransparent())
        col = over;
    else if (cls == lcState)
        col = on ? juce::Colour(0xffb6f0bc) : off ? juce::Colour(0xff8f7f5c) : col;
    else if (cls == lcSign)
        col = on ? juce::Colour(0xffffd27a) : col;
    else if (cls == lcSnd)
        col = off ? juce::Colour(0xffa8946a) : col;
    else if (cls == lcVt)
        col = on ? kAmber : col;
    else if (L.backlight && cls != lcStatus && cls != lcName && cls != lcTag)
        col = on ? juce::Colour(0xffffc24e) : hov ? juce::Colour(0xfffff0c4) : col;
    st.col = col;
    if (cls == lcSign && on)
        st.glow = juce::Colour(0xe6ffb23e);
    const float fs = L.size;
    float base1;
    TextStyle st2 = st;
    if (two)
    {
        st2.size = 8.8f;
        st2.semi = false;
        st2.track = 0.02f;
        st2.col = on ? juce::Colour(0xffffe6a8) : juce::Colour(0xffffd98a);
        st2.glow = juce::Colour(0x00000000);
        const float total = fs + 1.5f + st2.size, top = r.getY() + (r.getHeight() - total) * 0.5f;
        base1 = top + 0.85f * fs;
        const float base2 = top + fs + 1.5f + 0.85f * st2.size;
        const float mx = r.getWidth() - 5.0f;
        float w2 = as->textWidth(b, st2), q2 = w2 > mx && w2 > 0 ? mx / w2 : 1.0f;
        const auto &t2 = as->text(b, st2, q2);
        c.sprite(t2.spr, r.getCentreX() - t2.w * 0.5f - t2.pad, base2 - 1.05f * st2.size - t2.pad);
    }
    else if (cls == lcLv)
        base1 = r.getBottom() - 0.15f * fs - 2.0f;
    else
        base1 = r.getY() + (r.getHeight() - fs) * 0.5f + 0.85f * fs;
    const float mx = L.left ? r.getWidth() - L.pad - 4.0f : r.getWidth() - 5.0f;
    const float w1 = as->textWidth(a, st), q1 = w1 > mx && w1 > 0 ? mx / w1 : 1.0f;
    const auto &t1 = as->text(a, st, q1);
    const float x = L.left ? r.getX() + L.pad : r.getCentreX() - t1.w * 0.5f;
    c.sprite(t1.spr, x - t1.pad, base1 - 1.05f * fs - t1.pad);
    if (fl > 0.02f && L.backlight)
    { // touched: the letters burn for a moment
        TextStyle hot = st;
        hot.col = juce::Colour(0xfffff6dc);
        hot.glow = juce::Colour(0xffffb23e);
        const auto &th = as->text(a, hot, q1);
        c.sprite(th.spr, x - th.pad, base1 - 1.05f * fs - th.pad, fl);
    }
}

// ============================================================================================== one control
void MachineEditor::Impl::drawCtlArt(const Ctx &c, Ctl &k)
{
    if (!c.touches(k.area))
        return;
    float fl = k.flashDrawn;
    const float base = k.glowDrawn;
    const bool socketKind = k.type == CType::Switch || k.type == CType::Socket;
    const float glow = juce::jmax(base, socketKind ? 1.6f * fl : 0.2f + 1.4f * fl);
    const float gain = k.held ? 1.32f : k.hover ? 1.2f : 1.0f;
    const float haloA = k.held ? 1.0f : k.hover ? 0.8f : 0.0f;
    auto part = [&](const juce::String &name, const juce::AffineTransform &t, float alpha = 1.0f) {
        auto &kit = const_cast<Kit &>(as->kit(name));
        if (haloA > 0)
        {
            const auto &h = haloOf(*as, kit);
            const float pd = (float)kit.haloPad / (float)S;
            c.affine(h, juce::AffineTransform::translation(-pd, -pd).followedBy(t), haloA * alpha);
        }
        c.affine(kit.bmp, t, alpha, Blend::Over, gain);
    };
    auto pivoted = [&](const juce::String &name, FPoint at, float sc, float deg) {
        const auto &kit = as->kit(name);
        return juce::AffineTransform::translation(-kit.pivot.x, -kit.pivot.y).scaled(sc).rotated(juce::degreesToRadians(deg)).translated(at.x, at.y);
    };
    // the light behind its label plate
    if (k.hasLabel && styleOf(k.lcls).backlight && k.role != R_Status)
    {
        const float lb = k.held ? 0.95f : k.on ? 0.85f : k.hover ? 0.6f : 0.22f;
        drawBacklight(c, k.label, juce::jmax(lb, 0.22f + 0.78f * fl), false);
    }
    switch (k.type)
    {
    case CType::Wheel:
    case CType::Selector:
        drawGlow(c, "glow-round", k.centre, k.glowSize, glow);
        part(k.spriteName, pivoted(k.spriteName, k.centre, k.sprScale, k.angle));
        break;
    case CType::Switch:
    {
        const FPoint lampAt(k.pivot.x, k.pivot.y - 12.0f * k.sprScale);
        drawGlow(c, "glow-socket", lampAt, k.glowSize, glow);
        part("switch-arm", pivoted("switch-arm", k.pivot, k.sprScale, k.angle));
        const double since = clock - k.pressAt;
        if (since >= 0 && since < 0.11)
        { // the contact spark
            const auto &sp = as->kit("spark");
            c.affine(sp.bmp, juce::AffineTransform::translation(k.pivot.x - sp.w * 0.5f, k.pivot.y - 14.0f - sp.h * 0.5f), 1.0f, Blend::Screen);
        }
        break;
    }
    case CType::Plunger:
        if (k.spriteName.isNotEmpty())
        {
            const auto ctr = k.rect.getCentre();
            drawGlow(c, "glow-round", ctr, k.glowSize, glow);
            const float a = k.animDrawn > 0 ? k.animDrawn : 0.f;
            part("rod-knob", pivoted("rod-knob", {ctr.x, ctr.y + 2.0f * a}, 1.0f - 0.08f * a, 0));
        }
        break;
    case CType::Lever:
        if (k.role == R_Calm)
        {
            const float a = (k.animDrawn > 0 ? k.animDrawn : 0.f);
            part("chain-pull", pivoted("chain-pull", {k.pivot.x, k.pivot.y + 12.0f * a + (k.on ? 5.0f : 0.0f)}, k.sprScale, 0));
        }
        else
            part("pull-lever", pivoted("pull-lever", k.pivot, 1.0f, (k.role == R_Prev ? 24.0f : -24.0f) * (k.animDrawn > 0 ? k.animDrawn : 0.f)));
        break;
    case CType::Sign:
    {
        const auto name = k.spriteName.isNotEmpty() ? k.spriteName : "sign-" + k.id.fromFirstOccurrenceOf("page-", false, false);
        // it hangs from the top of its sprite, which is taller than its plate by the chains (7 px; the FX sign's are 17)
        const FPoint top(k.rect.getCentreX(), k.rect.getY() - juce::jmax(0.0f, as->kit(name).h - k.rect.getHeight()));
        const float deg = k.animDrawn > -90 ? k.animDrawn : 0.f;
        part(name, pivoted(name, top, 1.0f, deg));
        if (k.extraDrawn > 0.01f)
            c.affine(as->kit(name + "-lit").bmp, pivoted(name + "-lit", top, 1.0f, deg), k.extraDrawn);
        break;
    }
    case CType::Gauge:
        part(k.role == R_OutGauge ? "needle-big" : "needle-small", pivoted(k.role == R_OutGauge ? "needle-big" : "needle-small", k.pivot, 1.0f, k.angle));
        break;
    case CType::Socket:
    {
        float lv = glow, size = k.glowSize;
        if (litSrc >= 0 && k.role == R_SockIn)
        { // a hose is in the hand: the sockets it may go to burn, the others go dark
            const bool legal = k.op > litSrc || k.op == litSrc;
            lv = legal ? 1.0f : 0.0f;
            size *= legal ? k.extraDrawn : 1.0f;
        }
        else if (k.role == R_SockIn)
            lv = juce::jmax(lv, k.v01);
        drawGlow(c, "glow-socket", k.centre, size, lv);
        break;
    }
    case CType::Lantern:
    {
        drawGlow(c, "glow-round", k.centre, k.glowSize, glow);
        static const char *L = "ABCD";
        const auto name = juce::String("lantern-") + juce::String::charToString((juce::juce_wchar)L[juce::jlimit(0, 3, k.corner)]);
        part(name, pivoted(name, k.centre, k.sprScale, 0));
        if (k.v01 > 0.01f)
            c.affine(as->kit(name + "-lit").bmp, pivoted(name + "-lit", k.centre, k.sprScale, 0), k.v01, Blend::Over, gain);
        break;
    }
    case CType::Plate:
        if (k.role == R_CornerOct && c.g != nullptr)
        { // MORPH: a little glass window between VOLUME and PAN, lit in the corner's colour when moved
            auto &g = *c.g;
            const auto r = k.rect.reduced(1.0f);
            g.setColour(juce::Colour(0xf0140d08));
            g.fillRoundedRectangle(r, 5.0f);
            if (k.lit)
            {
                juce::ColourGradient cg(cornerColour(k.corner).withAlpha(0.45f), r.getCentre(), cornerColour(k.corner).withAlpha(0.0f), r.getCentre() + FPoint(r.getWidth() * 0.6f, 0), true);
                g.setGradientFill(cg);
                g.fillRoundedRectangle(r, 5.0f);
            }
            g.setColour(k.hover ? juce::Colour(0xffffd98a) : juce::Colour(0xffa07a44));
            g.drawRoundedRectangle(r, 5.0f, k.hover ? 1.8f : 1.3f);
            g.setColour(juce::Colour(0x50ffd98a)); // the line between up (top) and down (bottom)
            g.drawHorizontalLine((int)std::round(r.getCentreY()), r.getX() + 6, r.getRight() - 6);
        }
        if (k.hasLamp)
        {
            const float sz = k.lamp.getWidth() - 6.0f;
            const auto &sp = lampSprite(*as, cornerColour(k.corner), sz, k.lit);
            c.sprite(sp, k.lamp.getX() + 3 - 12, k.lamp.getY() + 3 - 12, k.lit ? 1.0f : 0.3f);
        }
        if (k.role == R_BenchLamp)
        {
            const float cy = k.rect.getCentreY();
            if (selLink >= 0)
            {
                const int f = (int)ss::MatrixIndex::sourceIndexAt((size_t)selLink), t = (int)ss::MatrixIndex::targetIndexAt((size_t)selLink);
                c.sprite(lampSprite(*as, ghostColour(f), 14, true), k.rect.getX() + k.rect.getWidth() * 0.26f - 7 - 12, cy - 7 - 12);
                c.sprite(lampSprite(*as, ghostColour(t), 14, true), k.rect.getX() + k.rect.getWidth() * 0.76f - 7 - 12, cy - 7 - 12);
            }
            else
                c.sprite(lampSprite(*as, ghostColour(sel), 14, true), k.rect.getX() + k.rect.getWidth() * 0.5f - 7 - 12, cy - 7 - 12);
        }
        else if (k.role == R_GScope)
            drawScope(c);
        else if (k.role == R_LView)
            drawLinkView(c);
        else if (k.role == R_Status)
            drawBacklight(c, k.rect, juce::jmax(0.22f, 0.22f + 0.78f * fl), true);
        break;
    case CType::Pin:
        drawEnv(c, k.role == R_GEnv ? 0 : k.role == R_LEnv ? 1 : 2, k);
        break;
    case CType::Spyglass:
    case CType::Grip:
    { // idle: a glint; hover: bright; pressed: a flash; the SPYGLASS stays lit while its window is open (k.on)
        const bool spy = k.type == CType::Spyglass;
        const auto name = spy ? juce::String("spyglass") : juce::String("size-grip");
        const auto at = juce::AffineTransform::translation(k.rect.getX(), k.rect.getY());
        const float lit = juce::jlimit(0.0f, 1.0f, glow * 0.9f - 0.1f);
        if (spy)
            drawGlow(c, "glow-socket", k.centre, k.glowSize * (0.8f + 0.4f * lit), glow * 0.55f);
        part(name, at);
        if (lit > 0.01f)
            c.affine(as->kit(name + "-lit").bmp, at, lit, Blend::Over, gain);
        break;
    }
    case CType::Keys:
    {
        const auto *keys = as->layout["keys"].getArray();
        if (keys == nullptr)
            break;
        auto keySprite = [&](const juce::String &name, float w, float h) -> const Sprite & {
            const juce::String key = "key" + name + juce::String(juce::roundToInt(w * 4)) + "x" + juce::String(juce::roundToInt(h * 4));
            auto it = as->made.find(key);
            if (it != as->made.end())
                return it->second;
            const auto &kt = as->kit(name);
            // a white key is its sprite cut to the key's own length; a black key is the sprite fitted to its rect
            Bitmap b = name.startsWith("key-black") ? kt.bmp.resampled(juce::roundToInt(w * S), juce::roundToInt(h * S))
                                                    : kt.bmp.resampled(juce::roundToInt((w + 0.4f) * S), kt.bmp.h).cropped({0, 0, juce::roundToInt((w + 0.4f) * S), juce::roundToInt(h * S)});
            return as->made.emplace(key, Sprite::fromBitmap(b)).first->second;
        };
        int wi = 0;
        for (auto &kv : *keys)
        {
            if ((bool)kv["black"])
                continue;
            const auto r = rectOf(kv["rect"]);
            const int note = (int)kv["note"];
            const bool dn = keysDown[juce::jlimit(0, 127, note)] > 0;
            const juce::String name = dn ? juce::String("key-white-down") : "key-white-" + juce::String(wi % 3);
            ++wi;
            if (c.touches(r))
                c.sprite(keySprite(name, r.getWidth(), r.getHeight()), r.getX(), r.getY());
        }
        for (auto &kv : *keys)
        {
            if (!(bool)kv["black"])
                continue;
            const auto r = rectOf(kv["rect"]);
            const bool dn = keysDown[juce::jlimit(0, 127, (int)kv["note"])] > 0;
            if (c.touches(r))
                c.sprite(keySprite(dn ? "key-black-down" : "key-black", r.getWidth(), r.getHeight()), r.getX(), r.getY());
        }
        break;
    }
    default:
        break;
    }
}

void MachineEditor::Impl::drawCtlText(const Ctx &c, Ctl &k)
{
    if (!k.hasLabel || !c.touches(k.area))
        return;
    const float fl = k.flashDrawn;
    if (k.role == R_Status)
    { // one line; a longer message runs through the slit
        TextStyle st;
        st.size = 8.8f;
        st.semi = false;
        st.track = 0.01f;
        st.col = juce::Colour(0xffb6f0bc);
        const auto &t = as->text(k.t1, st, 1.0f);
        const auto r = k.rect;
        const float x = r.getX() + 7.0f + statusShiftDrawn, base = r.getY() + (r.getHeight() - st.size) * 0.5f + 0.85f * st.size;
        // (clipped to the slit)
        std::vector<IRect> cl;
        const IRect slit((int)std::floor((r.getX() + 3) * S), (int)std::floor((r.getY() - 4) * S), (int)std::ceil((r.getWidth() - 6) * S), (int)std::ceil((r.getHeight() + 8) * S));
        for (auto &q : *c.clips)
        {
            const auto i = q.getIntersection(slit);
            if (!i.isEmpty())
                cl.push_back(i);
        }
        Ctx c2 = c;
        c2.clips = &cl;
        c2.sprite(t.spr, x - t.pad, base - 1.05f * st.size - t.pad);
        if (fl > 0.02f)
        {
            TextStyle hot = st;
            hot.col = juce::Colour(0xfff0fff2);
            hot.glow = juce::Colour(0xff78e68c);
            const auto &th = as->text(k.t1, hot, 1.0f);
            c2.sprite(th.spr, x - th.pad, base - 1.05f * st.size - th.pad, fl);
        }
        return;
    }
    FRect r = k.label;
    if (k.type == CType::Plunger && k.spriteName.isEmpty() && k.animDrawn > 0)
        r = r.translated(0, 2.0f * k.animDrawn);
    if (k.type == CType::Sign && std::abs(k.animDrawn) > 0.05f && k.animDrawn > -90)
    { // the sign swings: its words swing with it
        const FPoint top(k.rect.getCentreX(), k.rect.getY() - juce::jmax(0.0f, as->kit("sign-" + k.id.fromFirstOccurrenceOf("page-", false, false)).h - k.rect.getHeight()));
        const float rad = juce::degreesToRadians(k.animDrawn);
        const auto ctr = r.getCentre();
        const FPoint d = ctr - top;
        const FPoint moved(top.x + d.x * std::cos(rad) - d.y * std::sin(rad), top.y + d.x * std::sin(rad) + d.y * std::cos(rad));
        r = r.translated(moved.x - ctr.x, moved.y - ctr.y);
    }
    drawLabel(c, r, k.lcls, k.two, k.t1, k.t2, 0, k.hover, k.on, k.labOff, fl, k.colour);
    if (k.hasLamp)
    {
        static const char *L = "ABCD";
        drawLabel(c, k.lamp, lcLt, false, juce::String::charToString((juce::juce_wchar)L[juce::jlimit(0, 3, k.corner)]), {}, 0, false, false, false, 0, {});
    }
}

// ============================================================================================== the envelope rail
void MachineEditor::Impl::drawEnv(const Ctx &c, int w, const Ctl &k)
{
    if (c.g == nullptr || !c.touches(k.rect.expanded(12)))
        return;
    const auto &g = envGeom[w];
    juce::Path p;
    p.startNewSubPath(g.xs[0], g.ys[0]);
    for (int i = 1; i < 7; ++i)
        p.lineTo(g.xs[i], g.ys[i]);
    juce::Path fill = p;
    fill.lineTo(g.xs[6], g.yb);
    fill.lineTo(g.xs[0], g.yb);
    fill.closeSubPath();
    const float op = g.on ? 1.0f : 0.4f;
    auto &G = *c.g;
    G.setColour(kAmber.withAlpha(0.10f * op));
    G.fillPath(fill);
    const juce::PathStrokeType::JointStyle js = juce::PathStrokeType::curved;
    for (float wd : {9.0f, 6.0f}) // the rail's glow
    {
        G.setColour(kAmber.withAlpha(0.09f * op));
        G.strokePath(p, juce::PathStrokeType(wd, js, juce::PathStrokeType::rounded));
    }
    G.setColour(juce::Colour(0xff17110e).withAlpha(op));
    G.strokePath(p, juce::PathStrokeType(4.2f, js));
    G.setColour(juce::Colour(0xffc9b06a).withAlpha(op));
    G.strokePath(p, juce::PathStrokeType(2.4f, js));
    G.setColour(juce::Colour(0xfffff0c0).withAlpha(0.6f * op));
    G.strokePath(p, juce::PathStrokeType(0.7f, js), juce::AffineTransform::translation(-0.4f, -0.7f));
    const int hot = (hoverPin >= 0 && hoverPin / 8 == w) ? hoverPin % 8 : -1;
    for (int i = 0; i < 5; ++i)
    {
        const auto &kt = as->kit(i == hot ? "pin-hot" : "pin");
        c.affine(kt.bmp, juce::AffineTransform::translation(-kt.pivot.x, -kt.pivot.y).scaled(1.15f).translated(g.pin[i].x, g.pin[i].y));
    }
}

// ============================================================================================== the scope behind its glass slit
void MachineEditor::Impl::updateScope()
{
    auto *k = find("g-scope");
    if (k == nullptr || page != 0)
        return;
    const auto r = k->rect;
    constexpr int N = 113;
    float peak = 0;
    juce::Path p;
    for (int i = 0; i < N; ++i)
    {
        const float s = juce::jlimit(-1.0f, 1.0f, ed.scopeSample(i * (MachineEditor::kScope - 1) / (N - 1)));
        peak = juce::jmax(peak, std::abs(s));
        const float x = r.getX() + 4 + (r.getWidth() - 8) * (float)i / (float)(N - 1), y = r.getCentreY() - s * r.getHeight() * 0.42f;
        if (i)
            p.lineTo(x, y);
        else
            p.startNewSubPath(x, y);
    }
    const bool live = peak > 0.002f;
    if (live || scopeLive)
    {
        scopePath = p;
        sceneDirty.mark(scopeCells);
    }
    scopeLive = live;
}

void MachineEditor::Impl::drawScope(const Ctx &c)
{
    auto *k = find("g-scope");
    if (c.g == nullptr || k == nullptr)
        return;
    if (scopePath.isEmpty())
    {
        scopePath.startNewSubPath(k->rect.getX() + 4, k->rect.getCentreY());
        scopePath.lineTo(k->rect.getRight() - 4, k->rect.getCentreY());
    }
    auto &G = *c.g;
    G.saveState();
    G.reduceClipRegion(k->rect.toNearestInt());
    G.setColour(juce::Colour(0xff9fe6a8).withAlpha(0.14f));
    G.strokePath(scopePath, juce::PathStrokeType(5.2f, juce::PathStrokeType::curved));
    G.setColour(juce::Colour(0xff9fe6a8).withAlpha(0.20f));
    G.strokePath(scopePath, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved));
    G.setColour(juce::Colour(0xffc4f6ca));
    G.strokePath(scopePath, juce::PathStrokeType(1.3f, juce::PathStrokeType::curved));
    G.restoreState();
}

// which ghost haunts which: two lamps in the ghosts' colours (the pipe between them is painted)
void MachineEditor::Impl::drawLinkView(const Ctx &c)
{
    auto *k = find("l-view");
    if (k == nullptr || selLink < 0)
        return;
    const auto v = k->rect;
    const int f = (int)ss::MatrixIndex::sourceIndexAt((size_t)selLink), t = (int)ss::MatrixIndex::targetIndexAt((size_t)selLink);
    c.sprite(lampSprite(*as, ghostColour(f), 24, true), v.getX() + 18 - 12, v.getCentreY() - 12 - 12);
    c.sprite(lampSprite(*as, ghostColour(t), 24, true), v.getRight() - 42 - 12, v.getCentreY() - 12 - 12);
}

// ============================================================================================== MORPH: the furnace window
void MachineEditor::Impl::updateSpectrum()
{
    if (page != 2)
        return;
    constexpr int kOrder = 11, kN = 1 << kOrder;
    if (!fft)
    {
        fft = std::make_unique<juce::dsp::FFT>(kOrder);
        fftBuf.assign((size_t)kN * 2, 0.f);
    }
    float peak = 0;
    for (int i = 0; i < kN; ++i)
    {
        const float wv = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * (float)i / (float)(kN - 1));
        const float s = ed.histSample(kN - 1 - i);
        peak = juce::jmax(peak, std::abs(s));
        fftBuf[(size_t)i] = s * wv;
    }
    std::fill(fftBuf.begin() + kN, fftBuf.end(), 0.f);
    bool moved = false;
    if (peak > 1e-5f)
        fft->performFrequencyOnlyForwardTransform(fftBuf.data());
    const double sr = 48000.0;
    for (int x = 0; x < (int)specSmooth.size(); ++x)
    {
        const double f = 30.0 * std::pow(16000.0 / 30.0, (double)x / (double)(specSmooth.size() - 1));
        const int bin = juce::jlimit(1, kN / 2 - 1, (int)std::round(f / sr * kN));
        const float db = peak > 1e-5f ? juce::Decibels::gainToDecibels(fftBuf[(size_t)bin] / (float)kN * 4.0f, -90.0f) : -90.0f;
        const float nv = db > specSmooth[(size_t)x] ? db : specSmooth[(size_t)x] * 0.85f + db * 0.15f;
        moved = moved || std::abs(nv - specSmooth[(size_t)x]) > 0.4f;
        specSmooth[(size_t)x] = nv;
    }
    if (moved)
    {
        sceneDirty.mark(specCells);
        sceneDirty.mark(morphCells);
    }
}

void MachineEditor::Impl::drawMorph(const Ctx &c, bool text)
{
    if (!ed.morph)
        return;
    auto &m = *ed.morph;
    const auto &R = as->layout["pages"]["morph"]["regions"];
    const auto gl = rectOf(R["glass"]), pad = rectOf(R["pad"]);
    const bool on = m.on.load();
    const FPoint puck(pad.getX() + puckDrawn.x * pad.getWidth(), pad.getY() + puckDrawn.y * pad.getHeight());
    auto *spec = find("o-spectrum");
    if (text)
    {
        if (spec != nullptr)
            drawLabel(c, {spec->rect.getX() + 6, spec->rect.getY() + 2, 90, 8}, lcTiny, false, "WHAT YOU HEAR", {}, 0, false, false, false, 0, {});
        return;
    }
    if (c.g != nullptr && c.touches(gl.expanded(30)))
    {
        auto &G = *c.g;
        G.saveState();
        juce::Path clip;
        clip.addRoundedRectangle(gl, 12.0f);
        G.reduceClipRegion(clip);
        // what you hear: the output's spectrum along the bottom of the furnace window
        {
            const float x0 = gl.getX() + 8, x1 = gl.getRight() - 8, yb = gl.getBottom() - 8, ht = 54.0f * (on ? 1.0f : 0.5f);
            juce::Path p;
            for (int x = 0; x < (int)specSmooth.size(); ++x)
            {
                const float px = x0 + (x1 - x0) * (float)x / (float)(specSmooth.size() - 1);
                const float py = yb - juce::jlimit(0.f, 1.f, (specSmooth[(size_t)x] + 90.f) / 90.f) * ht;
                if (x == 0)
                    p.startNewSubPath(px, py);
                else
                    p.lineTo(px, py);
            }
            G.setColour(juce::Colour(0xfffff0cc).withAlpha(0.10f));
            G.strokePath(p, juce::PathStrokeType(5.0f, juce::PathStrokeType::curved));
            G.setColour(juce::Colour(0xfffff0cc).withAlpha(0.16f));
            G.strokePath(p, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved));
            G.setColour(juce::Colour(0xfffff3d6).withAlpha(on ? 1.0f : 0.5f));
            G.strokePath(p, juce::PathStrokeType(1.3f, juce::PathStrokeType::curved));
        }
        // ORBIT: its path (dashed), where the puck goes by itself
        const int sh = m.orbit.load();
        if (sh != Morph::O_OFF)
        {
            juce::Path op;
            for (int i = 0; i <= 96; ++i)
            {
                float ox, oy;
                Morph::orbitAt(sh, m.orbitSize.load(), (float)i / 96.f, m.x.load(), m.y.load(), ox, oy);
                const FPoint q(pad.getX() + ox * pad.getWidth(), pad.getY() + oy * pad.getHeight());
                if (i == 0)
                    op.startNewSubPath(q);
                else
                    op.lineTo(q);
            }
            const float dash[] = {3, 4};
            juce::Path dd;
            juce::PathStrokeType(1.0f).createDashedStroke(dd, op, dash, 2);
            G.setColour(juce::Colour(0xffe9dcb8).withAlpha(0.45f));
            G.fillPath(dd);
        }
        // a cable from each corner that holds a sound to the puck, as thick as its share
        for (int i = 0; i < Morph::N; ++i)
        {
            const int kd = m.kind[(size_t)i].load();
            if (kd == Morph::EMPTY || !cornerAny)
                continue;
            static const char *L = "ABCD";
            auto *lt = find(juce::String("morph-") + juce::String::charToString((juce::juce_wchar)L[i]));
            if (lt == nullptr)
                continue;
            const float w = cornerW[i], o = on ? 0.25f + 0.75f * w : 0.15f;
            const juce::Line<float> ln(lt->centre, puck);
            G.setColour(cornerColour(i).withAlpha(o * 0.15f));
            G.drawLine(ln, 3.0f + 9.0f * w);
            G.setColour(juce::Colour(0xff17110e).withAlpha(0.7f));
            G.drawLine(ln, 2.4f);
            G.setColour(cornerColour(i).withAlpha(o));
            G.drawLine(ln, 0.8f + 3.4f * w);
        }
        G.restoreState();
        // the sill slit: the same spectrum, small
        if (spec != nullptr && c.touches(spec->rect))
        {
            const auto os = spec->rect;
            juce::Path p;
            for (int x = 0; x < (int)specSmooth.size(); ++x)
            {
                const float px = os.getX() + 4 + (os.getWidth() - 8) * (float)x / (float)(specSmooth.size() - 1);
                const float py = os.getBottom() - 4 - juce::jlimit(0.f, 1.f, (specSmooth[(size_t)x] + 90.f) / 90.f) * (os.getHeight() - 12);
                if (x == 0)
                    p.startNewSubPath(px, py);
                else
                    p.lineTo(px, py);
            }
            G.setColour(juce::Colour(0xfffff0cc).withAlpha(0.22f));
            G.strokePath(p, juce::PathStrokeType(3.4f, juce::PathStrokeType::curved));
            G.setColour(juce::Colour(0xfffff3d6));
            G.strokePath(p, juce::PathStrokeType(1.2f, juce::PathStrokeType::curved));
        }
    }
    // the gantry bar the puck rides on, the clockwork arm of ORBIT, the puck
    std::vector<IRect> cl;
    const IRect glass((int)std::floor((gl.getX() - 14) * S), (int)std::floor((gl.getY() - 14) * S), (int)std::ceil((gl.getWidth() + 28) * S), (int)std::ceil((gl.getHeight() + 28) * S));
    for (auto &q : *c.clips)
    {
        const auto i = q.getIntersection(glass);
        if (!i.isEmpty())
            cl.push_back(i);
    }
    if (cl.empty())
        return;
    Ctx c2 = c;
    c2.clips = &cl;
    const auto &rail = as->kit("rail");
    c2.affine(rail.bmp, juce::AffineTransform::translation(gl.getCentreX() - rail.pivot.x, puck.y - rail.pivot.y));
    if (m.orbit.load() != Morph::O_OFF)
    {
        const FPoint ctr(pad.getX() + m.x.load() * pad.getWidth(), pad.getY() + m.y.load() * pad.getHeight());
        const float ang = std::atan2(puck.y - ctr.y, puck.x - ctr.x), ln = ctr.getDistanceFrom(puck);
        const auto &arm = as->kit("orbit-arm");
        c2.affine(arm.bmp, juce::AffineTransform::translation(-arm.pivot.x, -arm.pivot.y).scaled(juce::jmax(0.2f, ln / 50.0f), 1.0f).rotated(ang).translated(ctr.x, ctr.y));
        if (c2.g != nullptr)
        {
            c2.g->setColour(juce::Colour(0xff84e092).withAlpha(0.8f));
            c2.g->drawEllipse(ctr.x - 7, ctr.y - 7, 14, 14, 2.0f);
        }
    }
    const auto &lens = as->kit("lens");
    auto *pk = find("morph-puck");
    const float gain = pk != nullptr && pk->held ? 1.32f : pk != nullptr && pk->hover ? 1.2f : on ? 1.0f : 0.7f;
    c2.affine(lens.bmp, juce::AffineTransform::translation(-lens.pivot.x, -lens.pivot.y).scaled(1.1f).translated(puck.x, puck.y), 1.0f, Blend::Over, gain);
}

// ============================================================================================== hoses
void MachineEditor::Impl::drawHose(const Ctx &c, Hose &h, bool text)
{
    float x0 = h.cur[0].x, x1 = x0, y0 = h.cur[0].y, y1 = y0; // (a bezier stays inside its four points)
    for (int i = 1; i < 4; ++i)
    {
        x0 = juce::jmin(x0, h.cur[i].x);
        x1 = juce::jmax(x1, h.cur[i].x);
        y0 = juce::jmin(y0, h.cur[i].y);
        y1 = juce::jmax(y1, h.cur[i].y);
    }
    const FRect box(x0, y0, x1 - x0, y1 - y0);
    if (!c.touches(box.expanded(40)))
        return;
    const float depth = h.dragging ? 0.f : hoseDepth(h);
    if (text)
    {
        if (!h.dragging)
        {
            const bool selq = !h.fb && selLink == posOf(h.src, h.dst);
            drawLabel(c, {h.vq.x - 13, h.vq.y + 12, 26, 10}, lcVt, false, juce::String(juce::roundToInt(depth * 100)), {}, 0, false, selq, false, 0, {});
        }
        return;
    }
    const auto cloth = clothColour(h.src), gc = ghostColour(h.src);
    const bool selq = !h.dragging && !h.fb && selLink == posOf(h.src, h.dst);
    const float op = h.bad ? 0.5f : 1.0f;
    // the hose itself: painted once for the shape it has now (JUCE's path renderer), then a sprite until it moves
    const int key = h.src * 4 + (selq ? 1 : 0) + (h.bad ? 2 : 0);
    if (h.body.empty() || h.bodyKey != key)
    {
        const auto bb = box.expanded(10.0f).getSmallestIntegerContainer().toFloat();
        h.bodyAt = bb.getPosition();
        h.bodyKey = key;
        h.body = as->render(bb.getWidth(), bb.getHeight(), [&](juce::Graphics &G) {
            G.addTransform(juce::AffineTransform::translation(-bb.getX(), -bb.getY()));
            const auto path = bezierPath(h.cur);
            const auto round = juce::PathStrokeType::rounded;
            G.setColour(juce::Colour(0xff04050a).withAlpha(0.4f * op));
            G.strokePath(path, juce::PathStrokeType(9.0f, juce::PathStrokeType::curved, round), juce::AffineTransform::translation(1.6f, 3.8f));
            G.setColour((selq ? kAmber : juce::Colour(0xff17110e)).withAlpha(op));
            G.strokePath(path, juce::PathStrokeType(7.4f, juce::PathStrokeType::curved, round));
            G.setColour(shade(cloth, -0.2f).withAlpha(op));
            G.strokePath(path, juce::PathStrokeType(5.6f, juce::PathStrokeType::curved, round));
            juce::Path weave;
            const float dash[] = {2.4f, 2.8f};
            juce::PathStrokeType(4.2f, juce::PathStrokeType::curved, juce::PathStrokeType::butt).createDashedStroke(weave, path, dash, 2);
            G.setColour(shade(cloth, 0.35f).withAlpha(0.45f * op));
            G.fillPath(weave);
            G.setColour(shade(cloth, 0.7f).withAlpha(0.6f * op));
            G.strokePath(path, juce::PathStrokeType(1.0f, juce::PathStrokeType::curved, round), juce::AffineTransform::translation(-0.9f, -1.3f));
        });
    }
    c.sprite(h.body, h.bodyAt.x, h.bodyAt.y);
    if (!h.dragging && h.drawnAlpha > 0.02f)
    { // the signal: lit beads in the source ghost's colour run along it (light, so they are added)
        const auto &bead = pulseSprite(*as, gc);
        constexpr int N = 28;
        FPoint pts[N + 1];
        float len[N + 1];
        len[0] = 0;
        pts[0] = h.cur[0];
        for (int i = 1; i <= N; ++i)
        {
            pts[i] = bezAt(h.cur, (float)i / (float)N);
            len[i] = len[i - 1] + pts[i].getDistanceFrom(pts[i - 1]);
        }
        int seg = 1;
        const float half = 0.5f * (float)bead.w / (float)c.S;
        for (float d0 = std::fmod(h.drawnDash, 24.0f) - 24.0f; d0 < len[N]; d0 += 24.0f)
            for (float u = 0; u <= 8.0f; u += 2.0f)
            {
                const float sAt = d0 + u;
                if (sAt < 3.0f || sAt > len[N] - 3.0f)
                    continue;
                while (seg < N && len[seg] < sAt)
                    ++seg;
                while (seg > 1 && len[seg - 1] > sAt)
                    --seg;
                const float f = (sAt - len[seg - 1]) / juce::jmax(1e-4f, len[seg] - len[seg - 1]);
                const auto q = pts[seg - 1] + (pts[seg] - pts[seg - 1]) * f;
                c.sprite(bead, q.x - half, q.y - half, 0.5f * h.drawnAlpha, Blend::Screen);
            }
    }
    // the brass plugs at both ends, turned to the hose
    const auto &plug = as->kit("plug");
    const float aA = std::atan2(h.cur[1].y - h.cur[0].y, h.cur[1].x - h.cur[0].x), aB = std::atan2(h.cur[2].y - h.cur[3].y, h.cur[2].x - h.cur[3].x);
    for (int e = 0; e < 2; ++e)
    {
        const auto at = e == 0 ? h.cur[0] : h.cur[3];
        c.affine(plug.bmp, juce::AffineTransform::translation(-plug.pivot.x, -plug.pivot.y).scaled(1.2f).rotated(e == 0 ? aA : aB).translated(at.x, at.y), op);
    }
    if (h.dragging)
        return;
    // its valve: the link's depth (the loop's valve: the feedback, both ways from the middle)
    c.sprite(seatSprite(*as), h.vq.x - 10.5f, h.vq.y - 10.5f);
    const bool hov = hoverHoseValve >= 0 && hoverHoseValve < (int)hoses.size() && &hoses[(size_t)hoverHoseValve] == &h;
    drawGlow(c, "glow-round", h.vq, 28.0f, selq ? 0.9f : hov ? 0.8f : 0.2f + juce::jmin(1.0f, h.pulse) * 1.2f);
    const float v = h.fb ? (P.selfNodes[(size_t)h.src].fbLevel.value + 1.0f) * 0.5f : depth;
    const auto &valve = as->kit("valve-s");
    c.affine(valve.bmp, juce::AffineTransform::translation(-valve.pivot.x, -valve.pivot.y).rotated(juce::degreesToRadians(-135.0f + 270.0f * juce::jlimit(0.f, 1.f, v))).translated(h.vq.x, h.vq.y), 1.0f, Blend::Over, hov ? 1.2f : 1.0f);
}

// ============================================================================================== all of them
void MachineEditor::Impl::drawControlsArt(const Ctx &c)
{
    if (page == 0)
        for (auto &h : hoses)
            drawHose(c, h, false);
    for (auto &k : ctls)
        if (k.page == page && visible(k))
            drawCtlArt(c, k);
    if (page == 2)
        drawMorph(c, false);
    if (page == as->pageKeys.indexOf("fx"))
        fxDraw(c);
    for (auto &k : ctls)
        if (k.page < 0 && visible(k))
            drawCtlArt(c, k);
}

void MachineEditor::Impl::drawControlsText(const Ctx &c)
{
    for (auto &k : ctls)
        if (k.page != kSubPage && visible(k)) // (the SUB panel's parts are drawn with their board, over everything: subDraw)
            drawCtlText(c, k);
    if (page == 0)
    {
        for (auto &h : hoses)
            drawHose(c, h, true);
        // the envelope's little tags under the rail
        const int w = selLink >= 0 ? 1 : 0;
        if (auto *k = find(w ? "l-env" : "g-env"))
            if (c.touches(k->rect.expanded(12)))
            {
                static const char *nm[6] = {"DLY", "A", "HOLD", "D", "S", "R"};
                const auto &g = envGeom[w];
                for (int i = 0; i < 6; ++i)
                    if (g.xs[i + 1] - g.xs[i] > 1.0f)
                        drawLabel(c, {(g.xs[i] + g.xs[i + 1]) * 0.5f - 15, g.yb + 3, 30, 9}, lcTiny, false, nm[i], {}, 0, false, false, false, 0, {});
            }
        if (selLink >= 0)
            if (auto *k = find("l-view"))
            {
                const int f = (int)ss::MatrixIndex::sourceIndexAt((size_t)selLink), t = (int)ss::MatrixIndex::targetIndexAt((size_t)selLink);
                drawLabel(c, {k->rect.getX(), k->rect.getY() + 4, k->rect.getWidth(), k->rect.getHeight() - 8}, lcLv, false, juce::String(f + 1) + "   haunts   " + juce::String(t + 1), {}, 0, false, false, false, 0, {});
            }
    }
    else if (page == 1)
    {
        if (auto *k = find("m-env"))
            if (c.touches(k->rect.expanded(12)))
            {
                static const char *nm[6] = {"DLY", "A", "HOLD", "D", "S", "R"};
                const auto &g = envGeom[2];
                for (int i = 0; i < 6; ++i)
                    if (g.xs[i + 1] - g.xs[i] > 1.0f)
                        drawLabel(c, {(g.xs[i] + g.xs[i + 1]) * 0.5f - 15, g.yb + 3, 30, 9}, lcTiny, false, nm[i], {}, 0, false, false, false, 0, {});
            }
    }
    else if (page == 2)
        drawMorph(c, true);
    // the version line (the mock-up had no place for it): small, in the lee corner over the meadow
    if (c.touches({CW - 260.0f, CH - 14.0f, 260.0f, 14.0f}))
    {
        TextStyle st;
        st.size = 5.8f;
        st.semi = false;
        st.col = juce::Colour(0xffd9cfae);
        const auto &t = as->text(versionLine, st, 1.0f);
        c.sprite(t.spr, CW - 8.0f - t.w - t.pad, CH - 6.0f - 0.85f * st.size - 0.2f * st.size - t.pad);
    }
}

// ============================================================================================== lights, needles, the ticker
void MachineEditor::Impl::tickLights(double)
{
    for (auto &k : ctls)
    {
        if (!visible(k))
            continue;
        float fl = 0;
        float lv = glowLevel(k, fl);
        float anim = k.animDrawn, extra = k.extraDrawn, ang = k.angle;
        const double since = clock - k.pressAt;
        switch (k.type)
        {
        case CType::Plunger:
            anim = since >= 0 && since < 0.22 ? (float)(since < 0.066 ? since / 0.066 : 1.0 - (since - 0.066) / 0.154) : 0.f;
            break;
        case CType::Lever:
        {
            const double peak = k.role == R_Calm ? 0.12 : 0.105;
            anim = since >= 0 && since < 0.3 ? (float)(since < peak ? since / peak : 1.0 - (since - peak) / (0.3 - peak)) : 0.f;
            break;
        }
        case CType::Sign:
        { // touched: it swings 5, -3, 0 degrees over 700 ms; the page's own sign is lit
            float deg = 0;
            if (since >= 0 && since < 0.7)
            {
                const float u = (float)(since / 0.7);
                deg = u < 0.25f ? 5.0f * u / 0.25f : u < 0.6f ? 5.0f - 8.0f * (u - 0.25f) / 0.35f : -3.0f + 3.0f * (u - 0.6f) / 0.4f;
            }
            anim = deg;
            extra = k.on ? juce::jmin(1.0f, (extra < 0 ? 1.0f : extra) + 0.12f) : juce::jmax(0.0f, (extra < 0 ? 0.0f : extra) - 0.12f);
            break;
        }
        case CType::Switch:
            anim = since >= 0 && since < 0.11 ? 1.0f : 0.0f;
            break;
        case CType::Gauge:
            if (k.role == R_OutGauge)
                ang = -120.0f + 240.0f * juce::jlimit(0.f, 1.f, env.out * 1.05f);
            else if (k.op >= 0)
                ang = -110.0f + 220.0f * juce::jlimit(0.f, 1.f, env.ops[k.op].level * 1.4f);
            else if (k.fx != 0)
                ang = fxGaugeAngle(k);
            break;
        case CType::Socket:
            if (litSrc >= 0 && k.role == R_SockIn)
            { // legal sockets swell and shrink while a hose is in the hand
                const float tri = (float)std::abs(std::fmod(clock, 1.0) * 2.0 - 1.0);
                extra = 1.3f + 0.6f * tri;
            }
            else
                extra = k.v01; // the signal it is fed
            break;
        default:
            break;
        }
        if (std::abs(lv - k.glowDrawn) > 0.02f || std::abs(fl - k.flashDrawn) > 0.03f || (fl == 0.f && k.flashDrawn != 0.f) || std::abs(anim - k.animDrawn) > 0.02f ||
            std::abs(extra - k.extraDrawn) > 0.03f || std::abs(ang - k.angle) > 0.4f)
        {
            k.glowDrawn = lv;
            k.flashDrawn = fl;
            k.animDrawn = anim;
            k.extraDrawn = extra;
            k.angle = ang;
            touch(k);
        }
    }
    // the status slit: a message longer than the slit runs through it (pause, slide, pause)
    float shift = 0;
    if (statusOver > 0.5f && !env.calm)
    {
        const double dur = 2.6 + statusOver * 0.028, p = (clock - statusAt) / dur;
        const double fr = p - std::floor(p);
        const double q = p >= 2.0 ? 0.0 : ((long long)std::floor(p) % 2 == 0) ? fr : 1.0 - fr; // there and back once, then it rests
        shift = q < 0.18 ? 0.f : q > 0.82 ? -statusOver : -statusOver * (float)((q - 0.18) / 0.64);
    }
    if (std::abs(shift - statusShiftDrawn) > 0.4f)
    {
        statusShiftDrawn = shift;
        sceneDirty.mark(statusCells);
    }
}
} // namespace ghostblock::p2::machine
