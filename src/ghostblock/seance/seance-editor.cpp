/*
 * SEANCE (GhostBlock) - the editor. See seance-editor.h.
 * Everything here is drawn by code (no picture files yet): the table, the spirits, the threads.
 */
#include "ghostblock/seance/seance-editor.h"
#include "synth/matrix_index.h"
#include "presets/preset-manager.h"
#include "filesystem/import.h"
#include "ghostblock/spectral/spectral-fx.h"
#include "ghostblock/spectral/spectral-panel.h"
#include "ghostblock/sub/sub-bass.h"
#include "ghostblock/morph.h"
#include "ghostblock/seance/summon.h"
#include "ghostblock/seance/crossroads.h"
#include "ghostblock/seance/steps.h"
#include "ghostblock/seance/out-guard.h"
#include "ghostblock/seance/blend-guard.h"
#include "ghostblock/seance/step-patterns.h"
#include "RecipeJson.h" // ArpEZator: reads its recipe (the tables every one of its composers uses)
#include "ghostblock/beast/beast.h"
#include "ghostblock/shade/shade.h"
#include <cmrc/cmrc.hpp>
#include <cmath>
#include <random>

CMRC_DECLARE(sixsines_fonts);
CMRC_DECLARE(seance);

namespace ghostblock::seance
{
using Param = ss::Param;
namespace
{
constexpr float PI = 3.14159265358979f;
juce::String toStr(const std::optional<std::string> &s) { return s ? juce::String(*s) : juce::String(); }

namespace col
{
const juce::Colour SOOT{0xff0a0807}, PANEL{0xff14100e}, PANEL2{0xff1e1714}, EDGE{0xff3b2c24}, BONE{0xffe8dcc0}, DIM{0xff8f8272},
    BLOOD{0xffc2182b}, CANDLE{0xffffb45a}, WOOD0{0xff2c1810}, WOOD1{0xff120a07};
}
juce::Colour seatCol(int i)
{
    static const juce::uint32 c[12] = {0xff9be564, 0xff5fe0a0, 0xff46d6c8, 0xff5ab4f0, 0xff8c8cff, 0xffb878f5,
                                       0xffe46ad8, 0xffff5f8f, 0xffff5a4a, 0xffff8a3c, 0xfff5c542, 0xffd6e84a};
    return juce::Colour(c[(size_t)juce::jlimit(0, 11, i)]);
}

struct Fonts
{
    juce::Typeface::Ptr goth, bold, reg;
    Fonts()
    {
        auto load = [](auto fs, const std::string &path) -> juce::Typeface::Ptr {
            try
            {
                auto f = fs.open(path);
                return juce::Typeface::createSystemTypefaceFor(f.begin(), f.size());
            }
            catch (const std::exception &)
            {
                return nullptr;
            }
        };
        goth = load(cmrc::seance::get_filesystem(), "fonts/PirataOne-Regular.ttf");
        bold = load(cmrc::sixsines_fonts::get_filesystem(), "resources/fonts/Manrope/static/Manrope-Bold.ttf");
        reg = load(cmrc::sixsines_fonts::get_filesystem(), "resources/fonts/Manrope/static/Manrope-Medium.ttf");
    }
    static juce::Font mk(const juce::Typeface::Ptr &t, float h, bool b)
    {
        if (t) return juce::Font(juce::FontOptions(t).withHeight(h));
        return juce::Font(juce::FontOptions(h, b ? juce::Font::bold : juce::Font::plain));
    }
    juce::Font g(float h) const { return mk(goth, h, true); }
    juce::Font b(float h) const { return mk(bold, h, true); }
    juce::Font r(float h) const { return mk(reg, h, false); }
};
using FontsPtr = juce::SharedResourcePointer<Fonts>;

struct Bound
{
    virtual ~Bound() = default;
    virtual void refresh() = 0;
};

// ---------------------------------------------------------------------------------------------- a knob
struct Knob : juce::Component, Bound, juce::SettableTooltipClient
{
    SeanceEditor &ed;
    FontsPtr F;
    Param *p{nullptr};
    juce::String label;
    juce::Colour colour{col::BONE};
    bool dragging{false}, over{false};
    float downV{0};
    std::function<void()> onTouch; // e.g. turning VOLUME makes the spirit heard
    Knob(SeanceEditor &e, const juce::String &n) : ed(e), label(n) { setName(n); }
    void bind(Param *pp, const juce::String &n)
    {
        p = pp;
        label = n;
        setName(n);
        repaint();
    }
    void refresh() override { repaint(); }
    float norm() const { return p ? (p->value - p->meta.minVal) / juce::jmax(1e-9f, p->meta.maxVal - p->meta.minVal) : 0.f; }
    bool isInt() const { return p && p->meta.type != sst::basic_blocks::params::ParamMetaData::FLOAT; }
    juce::String valueText() const
    {
        if (!p) return {};
        auto s = toStr(p->meta.valueToString(p->value));
        return s.isNotEmpty() ? s : juce::String(p->value, 2);
    }
    void paint(juce::Graphics &g) override
    {
        const float al = isEnabled() && p ? 1.f : 0.25f;
        auto b = getLocalBounds().toFloat();
        auto lab = b.removeFromBottom(16);
        const float rad = juce::jmin(b.getWidth(), b.getHeight()) * 0.5f - 5.f;
        const auto c = b.getCentre();
        const float a0 = -0.75f * PI, a1 = 0.75f * PI, n = juce::jlimit(0.f, 1.f, norm()), a = a0 + n * (a1 - a0);
        const bool bip = p && p->meta.minVal < 0 && std::abs(p->meta.minVal + p->meta.maxVal) < 1e-4f;
        juce::Path tr;
        tr.addCentredArc(c.x, c.y, rad, rad, 0, a0, a1, true);
        g.setColour(col::EDGE.withMultipliedAlpha(al));
        g.strokePath(tr, juce::PathStrokeType(3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        if (p)
        {
            juce::Path va;
            const float from = bip ? 0.f : a0;
            va.addCentredArc(c.x, c.y, rad, rad, 0, juce::jmin(from, a), juce::jmax(from, a), true);
            g.setColour(colour.withMultipliedAlpha(al * 0.25f));
            g.strokePath(va, juce::PathStrokeType(8.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(colour.withMultipliedAlpha(al));
            g.strokePath(va, juce::PathStrokeType(3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        const float ir = rad - 7.f;
        g.setGradientFill(juce::ColourGradient(col::PANEL2.brighter(0.25f).withMultipliedAlpha(al), c.x, c.y - ir, col::SOOT.withMultipliedAlpha(al), c.x, c.y + ir, false));
        g.fillEllipse(c.x - ir, c.y - ir, 2 * ir, 2 * ir);
        g.setColour(col::EDGE.withMultipliedAlpha(al));
        g.drawEllipse(c.x - ir, c.y - ir, 2 * ir, 2 * ir, 1.f);
        g.setColour((over || dragging ? colour : col::BONE).withMultipliedAlpha(al));
        g.drawLine(c.x + std::sin(a) * ir * 0.35f, c.y - std::cos(a) * ir * 0.35f, c.x + std::sin(a) * ir * 0.9f, c.y - std::cos(a) * ir * 0.9f, 2.5f);
        g.setFont(F->b(10.5f));
        g.setColour(((over || dragging) && isEnabled() ? col::BONE : col::DIM).withMultipliedAlpha(isEnabled() ? 1.f : 0.4f));
        g.drawFittedText((over || dragging) && isEnabled() && p ? valueText() : label, lab.toNearestInt(), juce::Justification::centred, 1, 0.7f);
    }
    void mouseEnter(const juce::MouseEvent &) override { over = true; repaint(); }
    void mouseExit(const juce::MouseEvent &) override { over = false; repaint(); }
    void mouseDown(const juce::MouseEvent &e) override
    {
        if (!p) return;
        if (e.mods.isPopupMenu()) { typeIn(); return; }
        dragging = true;
        downV = p->value;
        if (onTouch) onTouch();
        ed.beginEdit(*p);
    }
    void mouseDrag(const juce::MouseEvent &e) override
    {
        if (!p || !dragging) return;
        const float range = p->meta.maxVal - p->meta.minVal;
        float v = downV + (float)(-e.getDistanceFromDragStartY() + e.getDistanceFromDragStartX() * 0.5f) / (e.mods.isShiftDown() ? 900.f : 190.f) * range;
        v = juce::jlimit(p->meta.minVal, p->meta.maxVal, v);
        if (isInt()) v = std::round(v);
        if (v != p->value) ed.setValue(*p, v);
        repaint();
    }
    void mouseUp(const juce::MouseEvent &) override
    {
        if (!p || !dragging) return;
        dragging = false;
        ed.endEdit(*p);
        repaint();
    }
    void mouseDoubleClick(const juce::MouseEvent &) override
    {
        if (!p) return;
        if (onTouch) onTouch();
        ed.setOnce(*p, p->meta.defaultVal);
    }
    void mouseWheelMove(const juce::MouseEvent &e, const juce::MouseWheelDetails &w) override
    {
        if (!p || !isEnabled()) return;
        const float range = p->meta.maxVal - p->meta.minVal;
        float v = p->value + (isInt() ? (w.deltaY > 0 ? 1.f : -1.f) : w.deltaY * range * (e.mods.isShiftDown() ? 0.02f : 0.12f));
        v = juce::jlimit(p->meta.minVal, p->meta.maxVal, v);
        if (onTouch) onTouch();
        ed.setOnce(*p, v);
    }
    void typeIn()
    {
        auto *w = new juce::AlertWindow("Type a value", juce::String(p->meta.name), juce::MessageBoxIconType::NoIcon);
        w->addTextEditor("v", valueText());
        w->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
        w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        w->enterModalState(true, juce::ModalCallbackFunction::create([sp = juce::Component::SafePointer<Knob>(this), w](int r) {
            if (r != 1 || !sp || !sp->p) return;
            std::string err;
            if (auto v = sp->p->meta.valueFromString(w->getTextEditorContents("v").toStdString(), err))
            {
                if (sp->onTouch) sp->onTouch();
                sp->ed.setOnce(*sp->p, juce::jlimit(sp->p->meta.minVal, sp->p->meta.maxVal, *v));
            }
            else sp->ed.status("Couldn't read that value.");
        }), true);
    }
};

// ---------------------------------------------------------------------------------------------- a plaque: button / toggle / choice
struct Plaque : juce::Component, Bound
{
    SeanceEditor &ed;
    FontsPtr F;
    enum Kind { BUTTON, TOGGLE, CHOICE } kind;
    juce::String text;
    Param *p{nullptr};
    bool lit{false}, over{false};
    juce::Colour colour{col::CANDLE};
    std::function<void()> onClick;
    std::function<void(bool)> onFlip;
    std::function<void(int)> onChoice; // CHOICE: after a pick, with the new value
    std::function<juce::String(int)> nameFor; // CHOICE: its own names for the values ("" = not offered)
    Plaque(SeanceEditor &e, Kind k, const juce::String &t) : ed(e), kind(k), text(t) { setName(t); }
    void bind(Param *pp, const juce::String &t)
    {
        p = pp;
        text = t;
        setName(t);
        repaint();
    }
    void refresh() override
    {
        if (kind == TOGGLE && p) lit = p->value > 0.5f;
        repaint();
    }
    void paint(juce::Graphics &g) override
    {
        const float al = isEnabled() ? 1.f : 0.3f;
        auto r = getLocalBounds().toFloat().reduced(1.f);
        const bool on = lit && isEnabled();
        g.setColour((on ? colour.withAlpha(0.22f) : col::PANEL2).withMultipliedAlpha(al));
        g.fillRoundedRectangle(r, 4.f);
        g.setColour((on ? colour : (over ? col::DIM : col::EDGE)).withMultipliedAlpha(al));
        g.drawRoundedRectangle(r, 4.f, on ? 1.6f : 1.f);
        g.setFont(F->b(11.5f));
        g.setColour((on ? colour.brighter(0.3f) : col::BONE).withMultipliedAlpha(al));
        juce::String t = text;
        if (kind == CHOICE && p)
        {
            t = nameFor ? nameFor((int)std::round(p->value)) : toStr(p->meta.valueToString(p->value));
            if (t.isEmpty()) t = "-";
            g.setColour(col::DIM.withMultipliedAlpha(al));
            g.drawText(text, r.reduced(8, 0).toNearestInt(), juce::Justification::centredLeft, false);
            g.setColour(col::BONE.withMultipliedAlpha(al));
            g.drawFittedText(t + "  v", r.reduced(8, 0).withTrimmedLeft(juce::GlyphArrangement::getStringWidth(F->b(11.5f), text) + 8).toNearestInt(), juce::Justification::centredRight, 1, 0.7f);
            return;
        }
        g.drawFittedText(t, r.reduced(4, 0).toNearestInt(), juce::Justification::centred, 1, 0.7f);
    }
    void mouseEnter(const juce::MouseEvent &) override { over = true; repaint(); }
    void mouseExit(const juce::MouseEvent &) override { over = false; repaint(); }
    void mouseUp(const juce::MouseEvent &e) override
    {
        if (!isEnabled() || !getLocalBounds().contains(e.getPosition())) return;
        click();
    }
    void click()
    {
        if (kind == TOGGLE && p)
        {
            const bool now = !(p->value > 0.5f);
            ed.setOnce(*p, now ? 1.f : 0.f);
            lit = now;
            if (onFlip) onFlip(now);
        }
        else if (kind == CHOICE && p)
        {
            juce::PopupMenu m;
            const int lo = (int)std::round(p->meta.minVal), hi = (int)std::round(p->meta.maxVal), cur = (int)std::round(p->value);
            for (int v = lo; v <= hi; ++v)
            {
                auto t = nameFor ? nameFor(v) : toStr(p->meta.valueToString((float)v));
                if (t.isEmpty() && nameFor) continue;
                if (t.isEmpty()) t = juce::String(v);
                m.addItem(v - lo + 1, t, true, v == cur);
            }
            m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [sp = juce::Component::SafePointer<Plaque>(this), lo](int r) {
                if (r > 0 && sp && sp->p)
                {
                    sp->ed.setOnce(*sp->p, (float)(lo + r - 1));
                    if (sp->onChoice) sp->onChoice(lo + r - 1);
                }
            });
        }
        if (onClick) onClick();
        repaint();
    }
};

// ---------------------------------------------------------------------------------------------- the envelope picture
struct EnvView : juce::Component
{
    ss::Patch::DAHDSRMixin *env{nullptr};
    juce::Colour colour{col::BONE};
    bool on{true};
    FontsPtr F;
    static float n(const Param &p) { return juce::jlimit(0.f, 1.f, (p.value - p.meta.minVal) / juce::jmax(1e-9f, p.meta.maxVal - p.meta.minVal)); }
    void paint(juce::Graphics &g) override
    {
        auto r = getLocalBounds().toFloat().reduced(1.f);
        g.setColour(col::SOOT);
        g.fillRoundedRectangle(r, 5.f);
        g.setColour(col::EDGE);
        g.drawRoundedRectangle(r, 5.f, 1.f);
        if (!env) return;
        auto a = r.reduced(10, 10);
        const float seg[6] = {0.02f + n(env->delay) * 0.6f, 0.05f + n(env->attack), 0.02f + n(env->hold) * 0.6f, 0.05f + n(env->decay), 0.6f, 0.05f + n(env->release)};
        float tot = 0;
        for (auto s : seg) tot += s;
        const float sus = n(env->sustain), x0 = a.getX(), w = a.getWidth(), yb = a.getBottom(), yt = a.getY();
        float x = x0;
        juce::Path p;
        p.startNewSubPath(x, yb);
        x += seg[0] / tot * w; p.lineTo(x, yb);
        x += seg[1] / tot * w; p.lineTo(x, yt);
        x += seg[2] / tot * w; p.lineTo(x, yt);
        x += seg[3] / tot * w; p.lineTo(x, yb - sus * (yb - yt));
        x += seg[4] / tot * w; p.lineTo(x, yb - sus * (yb - yt));
        p.lineTo(a.getRight(), yb);
        const float al = on ? 1.f : 0.25f;
        juce::Path fill(p);
        fill.closeSubPath();
        g.setColour(colour.withAlpha(0.13f * al));
        g.fillPath(fill);
        g.setColour(colour.withAlpha(al));
        g.strokePath(p, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        if (!on)
        {
            g.setColour(col::DIM);
            g.setFont(F->b(11.f));
            g.drawText("ENVELOPE OFF", getLocalBounds(), juce::Justification::centred);
        }
    }
};

struct KeyStateHolder
{
    juce::MidiKeyboardState keyState;
};
struct NoteForwarder : juce::MidiKeyboardState::Listener
{
    UiNoteQueue &q;
    explicit NoteForwarder(UiNoteQueue &qq) : q(qq) {}
    void handleNoteOn(juce::MidiKeyboardState *, int, int note, float vel) override { q.push(ghostblock::p2::UiNote{true, (int16_t)note, vel}); }
    void handleNoteOff(juce::MidiKeyboardState *, int, int note, float) override { q.push(ghostblock::p2::UiNote{false, (int16_t)note, 0.f}); }
};
struct Keys : KeyStateHolder, juce::MidiKeyboardComponent
{
    NoteForwarder fwd;
    explicit Keys(UiNoteQueue &qq) : juce::MidiKeyboardComponent(keyState, horizontalKeyboard), fwd(qq)
    {
        keyState.addListener(&fwd);
        setColour(whiteNoteColourId, col::BONE.darker(0.15f));
        setColour(blackNoteColourId, col::SOOT);
        setColour(keySeparatorLineColourId, col::EDGE);
        setColour(mouseOverKeyOverlayColourId, col::CANDLE.withAlpha(0.35f));
        setColour(keyDownOverlayColourId, col::BLOOD.withAlpha(0.8f));
        setColour(shadowColourId, juce::Colours::transparentBlack);
        setColour(upDownButtonBackgroundColourId, col::PANEL2);
        setColour(upDownButtonArrowColourId, col::BONE);
    }
    ~Keys() override { keyState.allNotesOff(0); keyState.removeListener(&fwd); }
};

// ---------------------------------------------------------------------------------------------- THE TABLE
struct Table : juce::Component
{
    SeanceEditor &ed;
    FontsPtr F;
    static constexpr int S = 700;                         // the table's square, in editor pixels
    static constexpr float CX = 350, CY = 350, RSEAT = 262, RTABLE = 322, RSPIRIT = 34, RCENTRE = 62;
    double t{0};
    int sel{0}, selFrom{-1}, selTo{-1};                   // the chosen spirit, or the chosen thread
    bool selCircle{false};
    int dragFrom{-1}, hover{-1};
    bool dragging{false};
    juce::Point<float> mouse;
    int depthDrag{-1};                                    // matrix index while a thread is dragged for depth
    float depthDown{0};
    bool depthMoved{false};
    juce::Image bg;
    struct Thread { int pos, from, to; std::vector<juce::Point<float>> pts; };
    std::vector<Thread> threads;

    explicit Table(SeanceEditor &e) : ed(e) { setOpaque(true); }
    static juce::Point<float> seat(int i)
    {
        const float a = (-75.f + 30.f * (float)i) * PI / 180.f; // seat 1 just right of the top; 1-6 down the right, 7-12 up the left
        return {CX + RSEAT * std::cos(a), CY + RSEAT * std::sin(a)};
    }
    void tick() { t += 1.0 / 30.0; buildThreads(); repaint(); }

    // ---- the picture that never changes (wood, chalk, rim), drawn once at 2x
    void makeBg()
    {
        bg = juce::Image(juce::Image::RGB, S * 2, S * 2, true);
        juce::Graphics g(bg);
        g.addTransform(juce::AffineTransform::scale(2.f));
        g.fillAll(col::SOOT);
        std::mt19937 rng(1313);
        std::uniform_real_distribution<float> u(0.f, 1.f);
        // candle light spilling over the edge
        g.setGradientFill(juce::ColourGradient(col::CANDLE.withAlpha(0.10f), CX, CY, col::SOOT.withAlpha(0.f), CX, CY + S * 0.72f, true));
        g.fillRect(0, 0, S, S);
        // the table
        g.setGradientFill(juce::ColourGradient(col::WOOD0, CX, CY, col::WOOD1, CX, CY + RTABLE, true));
        g.fillEllipse(CX - RTABLE, CY - RTABLE, 2 * RTABLE, 2 * RTABLE);
        for (int i = 0; i < 90; ++i)
        { // grain: uneven rings
            const float r = RTABLE * (0.08f + 0.92f * u(rng)), a0 = u(rng) * 2 * PI, len = (0.3f + u(rng) * 1.4f);
            juce::Path p;
            p.addCentredArc(CX + (u(rng) - 0.5f) * 10, CY + (u(rng) - 0.5f) * 10, r, r * (0.97f + 0.06f * u(rng)), 0, a0, a0 + len, true);
            g.setColour((u(rng) > 0.5f ? juce::Colours::black : col::CANDLE).withAlpha(0.03f + 0.05f * u(rng)));
            g.strokePath(p, juce::PathStrokeType(0.6f + u(rng) * 1.4f));
        }
        for (int i = 0; i < 70; ++i)
        { // scratches
            const float a = u(rng) * 2 * PI, r = RTABLE * std::sqrt(u(rng)) * 0.95f, x = CX + r * std::cos(a), y = CY + r * std::sin(a), b = u(rng) * 2 * PI, l = 6 + u(rng) * 34;
            g.setColour(col::BONE.withAlpha(0.02f + 0.05f * u(rng)));
            g.drawLine(x, y, x + l * std::cos(b), y + l * std::sin(b), 0.6f);
        }
        // rim
        g.setColour(juce::Colours::black.withAlpha(0.55f));
        g.drawEllipse(CX - RTABLE, CY - RTABLE, 2 * RTABLE, 2 * RTABLE, 10.f);
        g.setColour(juce::Colour(0xff6b4a2a));
        g.drawEllipse(CX - RTABLE, CY - RTABLE, 2 * RTABLE, 2 * RTABLE, 2.f);
        g.setColour(juce::Colour(0xff6b4a2a).withAlpha(0.5f));
        g.drawEllipse(CX - RTABLE + 7, CY - RTABLE + 7, 2 * RTABLE - 14, 2 * RTABLE - 14, 1.f);
        // chalk: the circle, the twelve-pointed star, the ticks
        const auto chalk = col::BONE;
        auto rough = [&](juce::Point<float> a, juce::Point<float> b, float al) {
            for (int k = 0; k < 3; ++k)
            {
                g.setColour(chalk.withAlpha(al * (0.5f + 0.5f * u(rng))));
                g.drawLine(a.x + (u(rng) - 0.5f) * 2.2f, a.y + (u(rng) - 0.5f) * 2.2f, b.x + (u(rng) - 0.5f) * 2.2f, b.y + (u(rng) - 0.5f) * 2.2f, 0.9f);
            }
        };
        for (int i = 0; i < 12; ++i) rough(seat(i), seat((i + 5) % 12), 0.07f);
        for (int k = 0; k < 3; ++k)
        {
            g.setColour(chalk.withAlpha(0.07f));
            const float r1 = RSEAT + (u(rng) - 0.5f) * 3, r2 = 150 + (u(rng) - 0.5f) * 3;
            g.drawEllipse(CX - r1, CY - r1, 2 * r1, 2 * r1, 1.f);
            g.drawEllipse(CX - r2, CY - r2, 2 * r2, 2 * r2, 0.8f);
        }
        for (int i = 0; i < 72; ++i)
        {
            const float a = (float)i * 5.f * PI / 180.f, r0 = 150, r1 = (i % 6 == 0) ? 168.f : 158.f;
            g.setColour(chalk.withAlpha(0.10f));
            g.drawLine(CX + r0 * std::cos(a), CY + r0 * std::sin(a), CX + r1 * std::cos(a), CY + r1 * std::sin(a), 0.8f);
        }
        // seat hollows and numbers
        for (int i = 0; i < 12; ++i)
        {
            auto s = seat(i);
            g.setGradientFill(juce::ColourGradient(juce::Colours::black.withAlpha(0.55f), s.x, s.y, juce::Colours::black.withAlpha(0.f), s.x, s.y + 58, true));
            g.fillEllipse(s.x - 58, s.y - 58, 116, 116);
            const float a = (-75.f + 30.f * (float)i) * PI / 180.f;
            const juce::Point<float> np{CX + (RTABLE - 16) * std::cos(a), CY + (RTABLE - 16) * std::sin(a)};
            g.setColour(col::SOOT.withAlpha(0.85f));
            g.fillEllipse(np.x - 11, np.y - 11, 22, 22);
            g.setColour(seatCol(i).withAlpha(0.8f));
            g.drawEllipse(np.x - 11, np.y - 11, 22, 22, 1.2f);
            g.setColour(col::BONE);
            g.setFont(F->b(11.5f));
            g.drawText(juce::String(i + 1), juce::Rectangle<float>(np.x - 11, np.y - 11, 22, 22), juce::Justification::centred);
        }
        // grime
        for (int i = 0; i < 9000; ++i)
        {
            g.setColour((u(rng) > 0.6f ? col::BONE : juce::Colours::black).withAlpha(0.03f + 0.05f * u(rng)));
            g.fillRect(u(rng) * S, u(rng) * S, 1.f, 1.f);
        }
    }

    // ---- threads
    static juce::Point<float> bez(juce::Point<float> a, juce::Point<float> c, juce::Point<float> b, float u)
    {
        const float v = 1 - u;
        return a * (v * v) + c * (2 * v * u) + b * (u * u);
    }
    static void curve(int from, int to, juce::Point<float> &a, juce::Point<float> &c, juce::Point<float> &b)
    {
        const auto A = seat(from), B = seat(to), mid = (A + B) * 0.5f;
        const juce::Point<float> ctr{CX, CY};
        const int lo = juce::jmin(from, to), hi = juce::jmax(from, to);
        auto d = seat(hi) - seat(lo); // the same normal for both directions of a pair, so the two threads lie side by side
        const float len = juce::jmax(1.f, d.getDistanceFromOrigin());
        const juce::Point<float> nrm{-d.y / len, d.x / len};
        c = ctr + (mid - ctr) * 0.42f + nrm * (14.f * (float)(((lo * 7 + hi * 3) % 5) - 2) + (from > to ? 20.f : -20.f));
        auto da = c - A, db = c - B;
        a = A + da * (RSPIRIT * 0.9f / juce::jmax(1.f, da.getDistanceFromOrigin()));
        b = B + db * (RSPIRIT * 1.05f / juce::jmax(1.f, db.getDistanceFromOrigin()));
    }
    void buildThreads()
    {
        threads.clear();
        auto &P = ed.patch;
        for (int i = 0; i < (int)ss::matrixSize; ++i)
        {
            auto &m = P.matrixNodes[(size_t)i];
            if (m.active.value < 0.5f) continue;
            const int f = (int)ss::MatrixIndex::sourceIndexAt((size_t)i), to = (int)ss::MatrixIndex::targetIndexAt((size_t)i);
            Thread th{i, f, to, {}};
            juce::Point<float> a, c, b;
            curve(f, to, a, c, b);
            const float depth = std::abs(m.level.value), amp = 1.5f + 9.f * juce::jmin(1.f, ed.opLevel(f) * 2.5f) * (0.3f + depth);
            const float k = 5.f + (float)(f % 4) * 2.f;
            const int N = 44;
            for (int j = 0; j <= N; ++j)
            {
                const float u = (float)j / N;
                auto p = bez(a, c, b, u);
                auto p2 = bez(a, c, b, juce::jmin(1.f, u + 0.01f)), p1 = bez(a, c, b, juce::jmax(0.f, u - 0.01f));
                auto d = p2 - p1;
                const float l = juce::jmax(1e-4f, d.getDistanceFromOrigin());
                const float w = amp * std::sin(PI * u) * std::sin(2 * PI * (u * k - (float)t * 2.2f) + (float)f);
                th.pts.push_back(p + juce::Point<float>(-d.y / l, d.x / l) * w);
            }
            threads.push_back(std::move(th));
        }
    }
    static juce::Path polyline(const std::vector<juce::Point<float>> &pts)
    {
        juce::Path p;
        for (size_t i = 0; i < pts.size(); ++i)
            if (i == 0) p.startNewSubPath(pts[i]);
            else p.lineTo(pts[i]);
        return p;
    }
    // a wraith: a hooded head, a ragged hem that never keeps still
    static juce::Path ghost(float cx, float cy, float r, float t, float seed)
    {
        juce::Path p;
        const float w = r * 0.84f, hem = cy + r * 0.62f;
        p.startNewSubPath(cx - w, cy + r * 0.1f);
        p.cubicTo(cx - w * 1.05f, cy - r * 1.36f, cx + w * 1.05f, cy - r * 1.36f, cx + w, cy + r * 0.1f);
        p.lineTo(cx + w, hem);
        const int n = 6;
        const float sw = 2 * w / n;
        for (int i = 0; i < n; ++i)
        {
            const float x0 = cx + w - (float)i * sw, x1 = x0 - sw;
            const float len = r * (0.30f + 0.38f * (0.5f + 0.5f * std::sin(seed * 3.7f + (float)i * 2.3f))) * (1.f + 0.35f * std::sin(t * 2.7f + (float)i * 1.9f + seed));
            const float sway = r * 0.10f * std::sin(t * 1.9f + (float)i + seed);
            p.quadraticTo(x0 - sw * 0.15f, hem + len * 0.5f, (x0 + x1) * 0.5f + sway, hem + len);
            p.quadraticTo(x1 + sw * 0.15f, hem + len * 0.5f, x1, hem - (i == n - 1 ? 0.f : r * 0.04f));
        }
        p.closeSubPath();
        return p;
    }

    void paint(juce::Graphics &g) override
    {
        if (bg.isNull()) makeBg();
        g.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
        g.drawImage(bg, getLocalBounds().toFloat());
        auto &P = ed.patch;
        const float tt = (float)t;
        const juce::Point<float> ctr{CX, CY};

        // what you hear: a line of light from every heard spirit to the middle
        for (int i = 0; i < 12; ++i)
        {
            auto &mx = P.mixerNodes[(size_t)i];
            if (mx.active.value < 0.5f || P.sourceNodes[(size_t)i].active.value < 0.5f || mx.level.value <= 0.001f) continue;
            auto s = seat(i);
            auto d = ctr - s;
            const float l = d.getDistanceFromOrigin();
            auto a = s + d * ((RSPIRIT + 6) / l), b = s + d * ((l - RCENTRE - 6) / l);
            const float al = 0.10f + 0.5f * juce::jlimit(0.f, 1.f, mx.level.value) + 0.3f * juce::jmin(1.f, ed.opLevel(i) * 2);
            g.setGradientFill(juce::ColourGradient(seatCol(i).withAlpha(juce::jmin(1.f, al)), a.x, a.y, col::BONE.withAlpha(al * 0.25f), b.x, b.y, false));
            g.drawLine(a.x, a.y, b.x, b.y, 1.2f + 4.f * mx.level.value);
            const float u = std::fmod(tt * 0.6f + (float)i * 0.13f, 1.f);
            auto dot = a + (b - a) * u;
            g.setColour(col::BONE.withAlpha(0.7f * (1 - u)));
            g.fillEllipse(dot.x - 2, dot.y - 2, 4, 4);
        }

        // the threads (who possesses who)
        for (auto &th : threads)
        {
            auto &m = P.matrixNodes[(size_t)th.pos];
            const bool chosen = (selFrom == th.from && selTo == th.to);
            const bool live = P.sourceNodes[(size_t)th.from].active.value > 0.5f && P.sourceNodes[(size_t)th.to].active.value > 0.5f;
            const float depth = std::abs(m.level.value), w = 1.4f + 5.f * depth, al = live ? 1.f : 0.3f;
            const auto c = seatCol(th.from);
            auto p = polyline(th.pts);
            g.setColour(c.withAlpha(0.10f * al));
            g.strokePath(p, juce::PathStrokeType(w * 4.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour(c.withAlpha(0.28f * al));
            g.strokePath(p, juce::PathStrokeType(w * 2.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour((chosen ? col::BONE : c.brighter(0.2f)).withAlpha(0.95f * al));
            g.strokePath(p, juce::PathStrokeType(w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            // beads run from the possessor to the possessed
            for (int k = 0; k < 3; ++k)
            {
                const float u = std::fmod(tt * 0.45f + (float)k / 3.f + (float)th.pos * 0.07f, 1.f);
                auto q = th.pts[(size_t)juce::jlimit(0, (int)th.pts.size() - 1, (int)(u * (float)(th.pts.size() - 1)))];
                const float r = 2.2f + 2.2f * depth;
                g.setColour(col::BONE.withAlpha(0.9f * al * std::sin(PI * u)));
                g.fillEllipse(q.x - r, q.y - r, 2 * r, 2 * r);
            }
            // the hook in the possessed spirit
            const auto e = th.pts.back(), e0 = th.pts[th.pts.size() - 4];
            auto d = e - e0;
            const float l = juce::jmax(1e-3f, d.getDistanceFromOrigin());
            d = d / l;
            const juce::Point<float> n{-d.y, d.x};
            juce::Path hook;
            const float hs = 6.f + 5.f * depth;
            hook.addTriangle(e + d * 2.f, e - d * hs + n * hs * 0.6f, e - d * hs - n * hs * 0.6f);
            g.setColour((chosen ? col::BONE : c).withAlpha(al));
            g.fillPath(hook);
        }

        // a thread being pulled
        if (dragging && dragFrom >= 0)
        {
            auto a = seat(dragFrom);
            const bool ok = hover >= 0;
            g.setColour(ok ? seatCol(dragFrom) : col::BONE.withAlpha(0.6f));
            juce::Path p;
            p.startNewSubPath(a);
            p.quadraticTo(ctr + ((a + mouse) * 0.5f - ctr) * 0.6f, mouse);
            const float dl[2] = {7.f, 6.f};
            juce::Path dp;
            juce::PathStrokeType(2.2f).createDashedStroke(dp, p, dl, 2);
            g.fillPath(dp);
        }

        // the spirits
        for (int i = 0; i < 12; ++i)
        {
            auto s0 = seat(i);
            auto &src = P.sourceNodes[(size_t)i];
            auto &mx = P.mixerNodes[(size_t)i];
            auto &sf = P.selfNodes[(size_t)i];
            const bool awake = src.active.value > 0.5f, heard = awake && mx.active.value > 0.5f && mx.level.value > 0.001f;
            const float lv = juce::jmin(1.f, ed.opLevel(i) * 2.5f), seed = (float)i * 2.1f;
            const float cx = s0.x + (awake ? std::sin(tt * 1.3f + seed) * 2.0f : 0.f), cy = s0.y + (awake ? std::cos(tt * 1.1f + seed * 2) * 2.5f : 3.f);
            const float r = RSPIRIT * (awake ? 1.f + 0.14f * lv : 0.88f);
            const auto c = seatCol(i);
            const bool target = dragging && dragFrom >= 0 && i != dragFrom;
            if (awake)
            {
                const float gr = r * (1.9f + 0.7f * lv);
                g.setGradientFill(juce::ColourGradient(c.withAlpha(0.20f + 0.25f * lv), cx, cy, c.withAlpha(0.f), cx, cy + gr, true));
                g.fillEllipse(cx - gr, cy - gr, 2 * gr, 2 * gr);
            }
            // feedback: the spirit's own tail curls round it
            if (awake && sf.active.value > 0.5f && std::abs(sf.fbLevel.value) > 0.001f)
            {
                juce::Path fb;
                const float rr = r * 1.38f, a0 = tt * 1.6f + seed, sweep = PI * (0.5f + 1.3f * juce::jmin(1.f, std::abs(sf.fbLevel.value)));
                fb.addCentredArc(cx, cy, rr, rr, 0, a0, a0 + sweep, true);
                g.setColour(c.withAlpha(0.85f));
                g.strokePath(fb, juce::PathStrokeType(2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                const float ea = a0 + sweep;
                g.fillEllipse(cx + rr * std::sin(ea) - 3.5f, cy - rr * std::cos(ea) - 3.5f, 7, 7);
            }
            auto body = ghost(cx, cy, r, tt, seed);
            if (awake)
            {
                if ((int)std::round(src.species.value) == ss::Patch::SourceNode::shadeSpecies)
                { // a shade (the wavetable oscillator) wears a crown of waves
                    juce::Path crown;
                    const int NP = 40;
                    for (int q = 0; q <= NP; ++q)
                    {
                        const float u = (float)q / NP, a = PI * (-0.42f + 0.84f * u);
                        const float rr = r * (1.28f + 0.16f * std::sin(u * PI * 10.f + tt * 4.f + seed));
                        const juce::Point<float> pt{cx + rr * std::sin(a), cy - r * 0.15f - rr * std::cos(a)};
                        if (q == 0) crown.startNewSubPath(pt);
                        else crown.lineTo(pt);
                    }
                    g.setColour(c.withAlpha(0.35f));
                    g.strokePath(crown, juce::PathStrokeType(5.f));
                    g.setColour(c.brighter(0.6f));
                    g.strokePath(crown, juce::PathStrokeType(1.8f));
                }
                else if (src.species.value > 0.5f)
                { // a beast (one of the Surge oscillators) has horns
                    juce::Path h;
                    h.addTriangle(cx - r * 0.62f, cy - r * 0.72f, cx - r * 0.30f, cy - r * 0.92f, cx - r * 0.78f, cy - r * 1.42f);
                    h.addTriangle(cx + r * 0.62f, cy - r * 0.72f, cx + r * 0.30f, cy - r * 0.92f, cx + r * 0.78f, cy - r * 1.42f);
                    g.setColour(c.darker(0.75f));
                    g.fillPath(h);
                    g.setColour(c.withAlpha(0.9f));
                    g.strokePath(h, juce::PathStrokeType(1.2f));
                }
                g.setGradientFill(juce::ColourGradient(c.darker(0.35f).withAlpha(0.96f), cx, cy - r * 1.1f, juce::Colour(0xff0c0908).withAlpha(0.93f), cx, cy + r * 0.9f, false));
                g.fillPath(body);
                g.setColour(c.withAlpha(0.30f));
                g.strokePath(body, juce::PathStrokeType(4.f));
                g.setColour(c.brighter(0.5f));
                g.strokePath(body, juce::PathStrokeType(1.5f));
            }
            else
            {
                g.setColour(juce::Colour(0xff15110f).withAlpha(0.9f));
                g.fillPath(body);
                g.setColour(c.withAlpha(0.28f));
                g.strokePath(body, juce::PathStrokeType(1.2f));
            }
            // face: slanted burning eyes, a jagged mouth that opens with its voice
            const float ex = r * 0.34f, ey = cy - r * 0.30f;
            if (awake)
            {
                const auto fire = heard ? col::BONE.interpolatedWith(c, 0.25f) : c.brighter(0.3f);
                const float eh = r * (0.20f + 0.14f * lv), ew = r * 0.22f;
                juce::Path eyes;
                for (float sd : {-1.f, 1.f})
                {
                    const float x = cx + sd * ex;
                    const juce::Point<float> in{x - sd * ew, ey + eh * 0.55f}, outp{x + sd * ew, ey - eh * 0.75f};
                    eyes.startNewSubPath(in);
                    eyes.quadraticTo(x - sd * ew * 0.1f, ey - eh * 0.9f, outp.x, outp.y);
                    eyes.quadraticTo(x + sd * ew * 0.5f, ey + eh * 1.0f, in.x, in.y);
                    eyes.closeSubPath();
                }
                g.setColour(c.withAlpha(0.35f));
                g.strokePath(eyes, juce::PathStrokeType(5.f));
                g.setColour(fire);
                g.fillPath(eyes);
                const float mw = r * 0.46f, y0 = cy + r * 0.12f, open = r * (0.10f + 0.42f * lv);
                juce::Path mouth;
                const int T = 6;
                for (int k = 0; k <= T; ++k)
                {
                    const float x = cx - mw + (float)k * (2 * mw / T), y = y0 + ((k % 2) ? open * 0.5f : 0.f);
                    if (k == 0) mouth.startNewSubPath(x, y);
                    else mouth.lineTo(x, y);
                }
                for (int k = T; k >= 0; --k)
                    mouth.lineTo(cx - mw + (float)k * (2 * mw / T), y0 + open + ((k % 2) ? 0.f : -open * 0.45f) + 1.5f);
                mouth.closeSubPath();
                g.setColour(c.withAlpha(0.30f));
                g.strokePath(mouth, juce::PathStrokeType(4.f));
                g.setColour(fire.withAlpha(0.95f));
                g.fillPath(mouth);
            }
            else
            {
                g.setColour(c.withAlpha(0.40f));
                g.drawLine(cx - ex - 5, ey - 2, cx - ex + 5, ey + 2, 1.6f);
                g.drawLine(cx + ex - 5, ey + 2, cx + ex + 5, ey - 2, 1.6f);
            }
            if (awake && P.throatNodes[(size_t)i].kind.value > 0.5f)
            { // a throat (a Surge filter on it): an iron collar under the jaw
                juce::Path collar;
                collar.addCentredArc(cx, cy + r * 0.20f, r * 0.84f, r * 0.50f, 0, PI * 0.60f, PI * 1.40f, true);
                g.setColour(col::SOOT.withAlpha(0.9f));
                g.strokePath(collar, juce::PathStrokeType(5.f));
                g.setColour(col::BONE.withAlpha(0.95f));
                g.strokePath(collar, juce::PathStrokeType(2.2f));
            }
            if (i == sel && !selCircle && selFrom < 0)
            {
                g.setColour(col::BONE);
                const float rr = r * 1.55f;
                const float dl[2] = {5.f, 5.f};
                juce::Path ring, dp;
                ring.addCentredArc(cx, cy, rr, rr, tt * 0.5f, 0, 2 * PI, true);
                juce::PathStrokeType(1.6f).createDashedStroke(dp, ring, dl, 2);
                g.fillPath(dp);
            }
            if (target)
            {
                g.setColour(seatCol(dragFrom).withAlpha(0.5f + 0.4f * std::sin(tt * 6)));
                g.drawEllipse(cx - r * 1.45f, cy - r * 1.45f, r * 2.9f, r * 2.9f, i == hover ? 3.f : 1.5f);
            }
        }

        // the middle: what you hear, drawn round a circle
        {
            const float r0 = RCENTRE;
            g.setGradientFill(juce::ColourGradient(juce::Colour(0xff1a1210), CX, CY - r0, col::SOOT, CX, CY + r0, false));
            g.fillEllipse(CX - r0, CY - r0, 2 * r0, 2 * r0);
            g.setColour(selCircle ? col::BONE : juce::Colour(0xff6b4a2a));
            g.drawEllipse(CX - r0, CY - r0, 2 * r0, 2 * r0, selCircle ? 2.2f : 1.5f);
            juce::Path p;
            const int N = 420;
            float pk = 0;
            for (int i = 0; i < N; ++i)
            {
                const float v = ed.histSample((N - i) * 5), win = std::sin(PI * (float)i / (N - 1));
                pk = juce::jmax(pk, std::abs(v));
                const float rr = 34.f + juce::jlimit(-22.f, 22.f, v * 60.f) * win, a = 2 * PI * (float)i / (N - 1);
                const juce::Point<float> q{CX + rr * std::sin(a), CY - rr * std::cos(a)};
                if (i == 0) p.startNewSubPath(q);
                else p.lineTo(q);
            }
            p.closeSubPath();
            g.setColour(col::BLOOD.withAlpha(0.18f + 0.4f * juce::jmin(1.f, pk * 2)));
            g.fillPath(p);
            g.setColour(col::BONE.withAlpha(0.9f));
            g.strokePath(p, juce::PathStrokeType(1.5f));
            g.setFont(F->b(9.5f));
            g.setColour(col::DIM);
            g.drawText("YOU HEAR", juce::Rectangle<float>(CX - 50, CY + r0 + 4, 100, 14), juce::Justification::centred);
        }
    }

    // ---- the mouse
    int seatAt(juce::Point<float> p) const
    {
        for (int i = 0; i < 12; ++i)
            if (seat(i).getDistanceFrom(p) < RSPIRIT * 1.25f) return i;
        return -1;
    }
    int threadAt(juce::Point<float> p) const
    {
        int best = -1;
        float bd = 10.f;
        for (size_t k = 0; k < threads.size(); ++k)
            for (auto &q : threads[k].pts)
                if (q.getDistanceFrom(p) < bd) { bd = q.getDistanceFrom(p); best = (int)k; }
        return best;
    }
    void link(int a, int b)
    {
        auto &P = ed.patch;
        auto &m = P.matrixNodes[ss::MatrixIndex::positionForSourceTarget((size_t)a, (size_t)b)];
        if (m.active.value < 0.5f) ed.setOnce(m.active, 1.f);
        if (std::abs(m.level.value) < 0.01f) ed.setOnce(m.level, 0.5f);
        for (int o : {a, b})
            if (P.sourceNodes[(size_t)o].active.value < 0.5f) ed.setOnce(P.sourceNodes[(size_t)o].active, 1.f);
        ed.selectThread(a, b);
        ed.status("Spirit " + juce::String(a + 1) + " now possesses spirit " + juce::String(b + 1) + ". Drag the thread up or down for how much.");
    }
    void mouseDown(const juce::MouseEvent &e) override
    {
        mouse = e.position;
        dragging = false;
        depthDrag = -1;
        const int s = seatAt(mouse);
        if (s >= 0)
        {
            if (e.mods.isPopupMenu()) { spiritMenu(s); return; }
            dragFrom = s;
            ed.selectSpirit(s);
            return;
        }
        dragFrom = -1;
        if (mouse.getDistanceFrom({CX, CY}) < RCENTRE) { ed.selectCircle(); return; }
        const int k = threadAt(mouse);
        if (k >= 0)
        {
            auto &th = threads[(size_t)k];
            auto &m = ed.patch.matrixNodes[(size_t)th.pos];
            if (e.mods.isPopupMenu())
            {
                ed.setOnce(m.active, 0.f);
                ed.status("Thread " + juce::String(th.from + 1) + " > " + juce::String(th.to + 1) + " cut.");
                ed.selectSpirit(th.from);
                return;
            }
            ed.selectThread(th.from, th.to);
            depthDrag = th.pos;
            depthDown = m.level.value;
            depthMoved = false;
        }
    }
    void mouseDrag(const juce::MouseEvent &e) override
    {
        mouse = e.position;
        if (depthDrag >= 0)
        {
            auto &m = ed.patch.matrixNodes[(size_t)depthDrag];
            if (!depthMoved && std::abs(e.getDistanceFromDragStartY()) < 3) return;
            if (!depthMoved) { depthMoved = true; ed.beginEdit(m.level); }
            const float v = juce::jlimit(m.level.meta.minVal, m.level.meta.maxVal, depthDown - (float)e.getDistanceFromDragStartY() / 180.f * (m.level.meta.maxVal - m.level.meta.minVal));
            ed.setValue(m.level, v);
            ed.status("Depth " + toStr(m.level.meta.valueToString(v)));
            return;
        }
        if (dragFrom < 0) return;
        if (!dragging && e.getDistanceFromDragStart() > 8) dragging = true;
        hover = seatAt(mouse);
        if (hover == dragFrom) hover = -1;
    }
    void mouseUp(const juce::MouseEvent &e) override
    {
        mouse = e.position;
        if (depthDrag >= 0)
        {
            if (depthMoved) ed.endEdit(ed.patch.matrixNodes[(size_t)depthDrag].level);
            depthDrag = -1;
            return;
        }
        if (dragging && dragFrom >= 0)
        {
            const int to = seatAt(mouse);
            if (to >= 0 && to != dragFrom) link(dragFrom, to);
        }
        dragging = false;
        dragFrom = -1;
        hover = -1;
    }
    void mouseDoubleClick(const juce::MouseEvent &e) override
    {
        const int s = seatAt(e.position);
        if (s < 0) return;
        toggleHeard(s);
    }
    void mouseWheelMove(const juce::MouseEvent &e, const juce::MouseWheelDetails &w) override
    {
        const int s = seatAt(e.position);
        if (s < 0) return;
        auto &P = ed.patch;
        auto &mx = P.mixerNodes[(size_t)s];
        if (P.sourceNodes[(size_t)s].active.value < 0.5f) ed.setOnce(P.sourceNodes[(size_t)s].active, 1.f);
        if (mx.active.value < 0.5f) ed.setOnce(mx.active, 1.f);
        ed.setOnce(mx.level, juce::jlimit(mx.level.meta.minVal, mx.level.meta.maxVal, mx.level.value + w.deltaY * 0.25f));
        ed.selectSpirit(s);
    }
    void toggleHeard(int s)
    {
        auto &P = ed.patch;
        auto &mx = P.mixerNodes[(size_t)s];
        const bool heard = mx.active.value > 0.5f && mx.level.value > 0.001f && P.sourceNodes[(size_t)s].active.value > 0.5f;
        if (heard) ed.setOnce(mx.active, 0.f);
        else
        {
            if (P.sourceNodes[(size_t)s].active.value < 0.5f) ed.setOnce(P.sourceNodes[(size_t)s].active, 1.f);
            ed.setOnce(mx.active, 1.f);
            if (mx.level.value < 0.05f) ed.setOnce(mx.level, 0.7f);
        }
        ed.status("Spirit " + juce::String(s + 1) + (heard ? " is no longer heard (it can still possess others)." : " is heard."));
    }
    void spiritMenu(int s)
    {
        auto &P = ed.patch;
        juce::PopupMenu m;
        const bool awake = P.sourceNodes[(size_t)s].active.value > 0.5f;
        m.addItem(1, awake ? "Put to sleep" : "Wake up");
        m.addItem(2, "Heard / not heard");
        m.addItem(3, "Cut all its threads");
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this).withTargetScreenArea(juce::Rectangle<int>(1, 1).withPosition(localPointToGlobal(seat(s).toInt()))),
                        [sp = juce::Component::SafePointer<Table>(this), s, awake](int r) {
                            if (!sp) return;
                            auto &PP = sp->ed.patch;
                            if (r == 1) sp->ed.setOnce(PP.sourceNodes[(size_t)s].active, awake ? 0.f : 1.f);
                            if (r == 2) sp->toggleHeard(s);
                            if (r == 3)
                            {
                                for (int i = 0; i < (int)ss::matrixSize; ++i)
                                    if (((int)ss::MatrixIndex::sourceIndexAt((size_t)i) == s || (int)ss::MatrixIndex::targetIndexAt((size_t)i) == s) && PP.matrixNodes[(size_t)i].active.value > 0.5f)
                                        sp->ed.setOnce(PP.matrixNodes[(size_t)i].active, 0.f);
                                sp->ed.status("Spirit " + juce::String(s + 1) + " is cut loose.");
                            }
                        });
        ed.selectSpirit(s);
    }
};

// ---------------------------------------------------------------------------------------------- a knob for a value that is not an engine parameter
struct GKnob : juce::Component
{
    FontsPtr F;
    juce::String label;
    juce::Colour colour{col::BLOOD};
    float lo{0}, hi{1}, def{0};
    bool stepped{false}, dragging{false}, over{false};
    float downV{0};
    std::function<double()> get;
    std::function<void(double, int)> edit; // 0 begin, 1 value, 2 end
    std::function<juce::String(double)> text;
    float value() const { return get ? (float)get() : 0.f; }
    void paint(juce::Graphics &g) override
    {
        const float al = isEnabled() ? 1.f : 0.25f;
        auto b = getLocalBounds().toFloat();
        auto lab = b.removeFromBottom(16);
        const float rad = juce::jmin(b.getWidth(), b.getHeight()) * 0.5f - 5.f;
        const auto c = b.getCentre();
        const float a0 = -0.75f * PI, a1 = 0.75f * PI, n = juce::jlimit(0.f, 1.f, (value() - lo) / juce::jmax(1e-9f, hi - lo)), a = a0 + n * (a1 - a0);
        const bool bip = lo < 0 && std::abs(lo + hi) < 1e-4f;
        juce::Path tr, va;
        tr.addCentredArc(c.x, c.y, rad, rad, 0, a0, a1, true);
        g.setColour(col::EDGE.withMultipliedAlpha(al));
        g.strokePath(tr, juce::PathStrokeType(3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const float from = bip ? 0.f : a0;
        va.addCentredArc(c.x, c.y, rad, rad, 0, juce::jmin(from, a), juce::jmax(from, a), true);
        g.setColour(colour.withMultipliedAlpha(al * 0.25f));
        g.strokePath(va, juce::PathStrokeType(8.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(colour.withMultipliedAlpha(al));
        g.strokePath(va, juce::PathStrokeType(3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const float ir = rad - 7.f;
        g.setGradientFill(juce::ColourGradient(col::PANEL2.brighter(0.25f).withMultipliedAlpha(al), c.x, c.y - ir, col::SOOT.withMultipliedAlpha(al), c.x, c.y + ir, false));
        g.fillEllipse(c.x - ir, c.y - ir, 2 * ir, 2 * ir);
        g.setColour(col::EDGE.withMultipliedAlpha(al));
        g.drawEllipse(c.x - ir, c.y - ir, 2 * ir, 2 * ir, 1.f);
        g.setColour((over || dragging ? colour : col::BONE).withMultipliedAlpha(al));
        g.drawLine(c.x + std::sin(a) * ir * 0.35f, c.y - std::cos(a) * ir * 0.35f, c.x + std::sin(a) * ir * 0.9f, c.y - std::cos(a) * ir * 0.9f, 2.5f);
        g.setFont(F->b(10.5f));
        g.setColour(((over || dragging) && isEnabled() ? col::BONE : col::DIM).withMultipliedAlpha(isEnabled() ? 1.f : 0.4f));
        g.drawFittedText((over || dragging || stepped) && isEnabled() && text ? (stepped && !(over || dragging) ? label + ": " + text(value()) : text(value())) : label, lab.toNearestInt(),
                         juce::Justification::centred, 1, 0.7f);
    }
    void set(float v, int type) { if (edit) edit(juce::jlimit(lo, hi, stepped ? std::round(v) : v), type); repaint(); }
    void mouseEnter(const juce::MouseEvent &) override { over = true; repaint(); }
    void mouseExit(const juce::MouseEvent &) override { over = false; repaint(); }
    void mouseDown(const juce::MouseEvent &) override { dragging = true; downV = value(); set(downV, 0); }
    void mouseDrag(const juce::MouseEvent &e) override
    {
        if (dragging) set(downV + (float)(-e.getDistanceFromDragStartY() + e.getDistanceFromDragStartX() * 0.5f) / (e.mods.isShiftDown() ? 900.f : 190.f) * (hi - lo), 1);
    }
    void mouseUp(const juce::MouseEvent &) override { if (dragging) { dragging = false; set(value(), 2); } }
    void mouseDoubleClick(const juce::MouseEvent &) override { set(def, 0); set(def, 1); set(def, 2); }
    void mouseWheelMove(const juce::MouseEvent &, const juce::MouseWheelDetails &w) override
    {
        if (!isEnabled()) return;
        const float v = value() + (stepped ? (w.deltaY > 0 ? 1.f : -1.f) : w.deltaY * (hi - lo) * 0.12f);
        set(v, 0); set(v, 1); set(v, 2);
    }
};

// ---------------------------------------------------------------------------------------------- a page laid over the table (FX, SUB)
struct PageBase : juce::Component
{
    FontsPtr F;
    juce::String title, blurb;
    PageBase() { setOpaque(true); }
    void paint(juce::Graphics &g) override
    {
        g.fillAll(col::SOOT);
        auto r = getLocalBounds().toFloat().reduced(8.f, 4.f);
        g.setColour(col::PANEL);
        g.fillRoundedRectangle(r, 8.f);
        g.setColour(col::EDGE);
        g.drawRoundedRectangle(r, 8.f, 1.f);
        g.setFont(F->g(30.f));
        g.setColour(col::BLOOD.brighter(0.2f));
        g.drawText(title, 28, 10, 400, 34, juce::Justification::centredLeft);
        g.setFont(F->r(11.5f));
        g.setColour(col::DIM);
        g.drawText(blurb, 28, 42, getWidth() - 56, 16, juce::Justification::centredLeft);
    }
};
struct SubPage : PageBase
{
    SeanceEditor &ed;
    ghostblock::p2::sub::SubBass &sb;
    using SB = ghostblock::p2::sub::SubBass;
    Plaque onBtn{ed, Plaque::BUTTON, "SUB ON"};
    std::vector<std::unique_ptr<Plaque>> presets;
    std::vector<std::unique_ptr<GKnob>> knobs;
    uint32_t lastChange{0xffffffffu};
    SubPage(SeanceEditor &e, SB &s) : ed(e), sb(s)
    {
        title = "The deep";
        blurb = "A sub-bass under the whole sound: it follows the lowest note you play, after the spirits and the FX. Off = the sound exactly as it was.";
        onBtn.colour = col::BLOOD.brighter(0.3f);
        onBtn.onClick = [this] { ed.subEdit(SB::pOn, sb.on() ? 0.0 : 1.0, 0); ed.subEdit(SB::pOn, sb.on() ? 0.0 : 1.0, 1); ed.subEdit(SB::pOn, sb.on() ? 1.0 : 0.0, 2); refresh(); };
        addAndMakeVisible(onBtn);
        onBtn.setBounds(28, 76, 140, 34);
        for (int i = 0; i < ghostblock::p2::sub::kPresets; ++i)
        {
            auto b = std::make_unique<Plaque>(ed, Plaque::BUTTON, ghostblock::p2::sub::presetName(i));
            b->colour = col::CANDLE;
            b->onClick = [this, i] { sb.uiPreset(i); ed.markStateDirty(); refresh(); };
            b->setBounds(184 + i * 120, 76, 114, 34);
            addAndMakeVisible(*b);
            presets.push_back(std::move(b));
        }
        for (uint32_t i = 1; i < SB::kParams; ++i)
        {
            const auto id = SB::paramIdAt(i);
            const auto &d = SB::desc(id);
            auto k = std::make_unique<GKnob>();
            k->label = d.label;
            k->setName(d.label);
            k->lo = d.min; k->hi = d.max; k->def = d.def; k->stepped = d.stepped;
            k->get = [this, id] { return sb.getParam(id); };
            k->edit = [this, id](double v, int t) { ed.subEdit(id, v, t); refresh(); };
            k->text = [this, id](double v) { char buf[64]{}; sb.valueToText(id, v, buf, sizeof(buf)); return juce::String(buf); };
            k->setBounds(28 + (int)(i - 1) * 102, 150, 98, 112);
            addAndMakeVisible(*k);
            knobs.push_back(std::move(k));
        }
        refresh();
    }
    GKnob *knob(uint32_t id) { return knobs[(size_t)(id - SB::pOn - 1)].get(); }
    void refresh()
    {
        const bool on = sb.on();
        onBtn.lit = on;
        onBtn.text = on ? "SUB ON" : "SUB OFF";
        const int cur = sb.currentPreset();
        for (int i = 0; i < (int)presets.size(); ++i) { presets[(size_t)i]->lit = on && cur == i; presets[(size_t)i]->repaint(); }
        const bool dirty = sb.getParam(SB::pBlend) > 0.5; // DRIVE / HARMONICS / MODE only shape the DIRTY side
        for (auto &k : knobs) k->setEnabled(on);
        for (auto id : {SB::pDrive, SB::pHarmonics, SB::pMode}) knob(id)->setEnabled(on && dirty);
        repaint();
    }
    void tick()
    {
        if (!isVisible()) return;
        const auto c = sb.changeCount();
        if (c != lastChange) { lastChange = c; refresh(); }
        repaint(28, 290, 400, 60);
    }
    void paint(juce::Graphics &g) override
    {
        PageBase::paint(g);
        g.setFont(F->b(10.5f));
        g.setColour(col::DIM);
        g.drawText("SOUNDS", 184, 58, 100, 14, juce::Justification::centredLeft);
        const float lv = juce::jlimit(0.f, 1.f, sb.level()), hz = sb.pitchHz();
        g.setColour(col::EDGE);
        g.fillRoundedRectangle(28, 300, 300, 10, 4.f);
        g.setColour(col::BLOOD.brighter(0.3f));
        g.fillRoundedRectangle(28, 300, 300 * lv, 10, 4.f);
        g.setFont(F->r(11.5f));
        g.setColour(col::DIM);
        g.drawText(sb.on() ? (hz > 0 ? "playing " + juce::String(hz, 1) + " Hz" : "waiting for a note") : "off", 28, 316, 300, 16, juce::Justification::centredLeft);
        if (sb.on() && sb.getParam(SB::pBlend) <= 0.5)
            g.drawText("DRIVE, HARMONICS and MODE shape the dirty side: turn BLEND up to use them.", 28, 270, 700, 16, juce::Justification::centredLeft);
    }
};
struct FxPage : PageBase
{
    std::unique_ptr<juce::Component> panel;
    FxPage()
    {
        title = "The veil";
        blurb = "Spectral effects after the spirits: four slots of spectral corruption, then the inversion. Off = the sound exactly as it was.";
    }
};
// ---------------------------------------------------------------------------------------------- CROSS (the crossroads): four sounds at four corners
struct BlendPage : PageBase
{
    using M = ghostblock::p2::Morph;
    SeanceEditor &ed;
    M &m;
    std::unique_ptr<ss::presets::PresetManager> pm; // its own, so a corner's sound never replaces the one on the table
    Plaque onBtn{ed, Plaque::BUTTON, "CROSSROADS OFF"}, orbitBtn{ed, Plaque::BUTTON, "ORBIT"}, rateBtn{ed, Plaque::BUTTON, "EVERY"}, presetsBtn{ed, Plaque::BUTTON, "CROSSROADS  v"};
    juce::String loadedName; // the ready-made or saved crossroads last loaded ("" = put together by hand)
    Plaque fixBtn{ed, Plaque::BUTTON, "FIX CANCELLING ON"};
    float shownCancel{0.f}, shownAfter{0.f}; // the readout, eased so it can be read
    Plaque cornerBtn[4] = {{ed, Plaque::BUTTON, ""}, {ed, Plaque::BUTTON, ""}, {ed, Plaque::BUTTON, ""}, {ed, Plaque::BUTTON, ""}};
    Plaque octBtn[4] = {{ed, Plaque::BUTTON, ""}, {ed, Plaque::BUTTON, ""}, {ed, Plaque::BUTTON, ""}, {ed, Plaque::BUTTON, ""}};
    GKnob vol[4], pan[4], sizeK;
    juce::Rectangle<int> board{390, 78, 400, 400};
    uint32_t lastChange{0xffffffffu};
    bool dragging{false};
    static juce::Colour cornerCol(int c)
    {
        static const juce::Colour k[4] = {juce::Colour(0xffc2182b), juce::Colour(0xffffb45a), juce::Colour(0xff7fd1c8), juce::Colour(0xffb48cff)};
        return k[c & 3];
    }
    static juce::Rectangle<int> panel(int c) { return {c % 2 ? 810 : 28, c / 2 ? 318 : 110, 342, 160}; }
    BlendPage(SeanceEditor &e, M &mm, const clap_host_t *host) : ed(e), m(mm)
    {
        title = "The crossroads";
        blurb = "Four sounds at four corners. Drag the flame: you hear a blend of the real sounds, each played by its own engine. Off = only the sound on the table. Kept with your project, not inside a sound.";
        pm = std::make_unique<ss::presets::PresetManager>(host ? host : M::quietHost()); // (never null: a load is only sent to the engine with a host)
        onBtn.colour = col::BLOOD.brighter(0.3f);
        onBtn.onClick = [this] { const double v = m.on.load() ? 0.0 : 1.0; ed.morphEdit(M::pOn, v, 0); ed.morphEdit(M::pOn, v, 1); ed.morphEdit(M::pOn, v, 2); ed.markStateDirty(); refresh(); };
        onBtn.setBounds(28, 72, 160, 30);
        addAndMakeVisible(onBtn);
        presetsBtn.setName("CROSSROADS");
        presetsBtn.colour = col::CANDLE;
        presetsBtn.onClick = [this] { presetsMenu(); };
        presetsBtn.setBounds(196, 72, 174, 30);
        addAndMakeVisible(presetsBtn);
        fixBtn.setName("FIX CANCELLING");
        fixBtn.colour = col::CANDLE;
        fixBtn.onClick = [this] {
            if (!ed.blendGuard) return;
            ed.blendGuard->on = !ed.blendGuard->on.load();
            ed.markStateDirty();
            ed.status(ed.blendGuard->on.load() ? "FIX CANCELLING on: a corner that fights the others is turned over, and the blend keeps its level."
                                               : "FIX CANCELLING off: the plain mix. The readout still shows how much the corners cancel.");
            refresh();
        };
        fixBtn.setBounds(28, 496, 200, 30);
        addAndMakeVisible(fixBtn);
        orbitBtn.setName("ORBIT");
        orbitBtn.onClick = [this] { pick(orbitBtn, M::pOrbit, M::O_COUNT, [](int i) { return juce::String(M::orbitNames[i]); }); };
        orbitBtn.setBounds(390, 496, 170, 30);
        addAndMakeVisible(orbitBtn);
        rateBtn.setName("EVERY");
        rateBtn.onClick = [this] { pick(rateBtn, M::pRate, M::kRates, [](int i) { return juce::String(M::rateNames[i]); }); };
        rateBtn.setBounds(566, 496, 136, 30);
        addAndMakeVisible(rateBtn);
        sizeK.label = "SIZE";
        sizeK.setName("SIZE");
        sizeK.lo = 0; sizeK.hi = 1; sizeK.def = 0.6f;
        sizeK.colour = col::CANDLE;
        sizeK.get = [this] { return (double)m.orbitSize.load(); };
        sizeK.edit = [this](double v, int t) { ed.morphEdit(M::pSize, v, t); if (t == 2) ed.markStateDirty(); };
        sizeK.text = [](double v) { return juce::String(juce::roundToInt(v * 100)) + " %"; };
        sizeK.setBounds(712, 486, 78, 90);
        addAndMakeVisible(sizeK);
        for (int c = 0; c < 4; ++c)
        {
            const auto r = panel(c);
            const juce::String L = juce::String::charToString((juce::juce_wchar)('A' + c));
            cornerBtn[c].setName("CORNER " + L);
            cornerBtn[c].colour = cornerCol(c);
            cornerBtn[c].onClick = [this, c] { cornerMenu(c); };
            cornerBtn[c].setBounds(r.getX() + 8, r.getY() + 8, r.getWidth() - 16, 32);
            addAndMakeVisible(cornerBtn[c]);
            vol[c].label = "VOLUME"; vol[c].setName("VOLUME " + L);
            vol[c].lo = -24; vol[c].hi = 6; vol[c].def = 0; vol[c].colour = cornerCol(c);
            vol[c].get = [this, c] { return (double)m.volDb[(size_t)c].load(); };
            vol[c].edit = [this, c](double v, int t) { m.volDb[(size_t)c] = (float)v; if (t == 2) ed.markStateDirty(); };
            vol[c].text = [](double v) { return juce::String(v, 1) + " dB"; };
            vol[c].setBounds(r.getX() + 16, r.getY() + 52, 84, 98);
            addAndMakeVisible(vol[c]);
            pan[c].label = "PAN"; pan[c].setName("PAN " + L);
            pan[c].lo = -1; pan[c].hi = 1; pan[c].def = 0; pan[c].colour = cornerCol(c);
            pan[c].get = [this, c] { return (double)m.pan[(size_t)c].load(); };
            pan[c].edit = [this, c](double v, int t) { m.pan[(size_t)c] = (float)v; if (t == 2) ed.markStateDirty(); };
            pan[c].text = [](double v) { return std::abs(v) < 0.01 ? juce::String("C") : (v < 0 ? "L" : "R") + juce::String(juce::roundToInt(std::abs(v) * 100)); };
            pan[c].setBounds(r.getX() + 108, r.getY() + 52, 84, 98);
            addAndMakeVisible(pan[c]);
            octBtn[c].setName("OCTAVE " + L);
            octBtn[c].onClick = [this, c] {
                juce::PopupMenu menu;
                const int cur = m.octave[(size_t)c].load();
                for (int o = M::kOctMax; o >= M::kOctMin; --o) menu.addItem(o - M::kOctMin + 1, (o > 0 ? "+" : "") + juce::String(o) + (o == 0 ? "  (as played)" : ""), true, o == cur);
                menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&octBtn[c]), [sp = juce::Component::SafePointer<BlendPage>(this), c](int r) {
                    if (r > 0 && sp) { sp->m.octave[(size_t)c] = r - 1 + M::kOctMin; sp->ed.markStateDirty(); sp->refresh(); }
                });
            };
            octBtn[c].setBounds(r.getX() + 204, r.getY() + 84, 126, 30);
            addAndMakeVisible(octBtn[c]);
        }
        refresh();
    }
    void pick(Plaque &b, uint32_t id, int n, std::function<juce::String(int)> name)
    {
        juce::PopupMenu menu;
        const int cur = (int)std::lround(m.getParam(id));
        for (int i = 0; i < n; ++i) menu.addItem(i + 1, name(i), true, i == cur);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&b), [sp = juce::Component::SafePointer<BlendPage>(this), id](int r) {
            if (r > 0 && sp) { sp->ed.morphEdit(id, r - 1, 0); sp->ed.morphEdit(id, r - 1, 1); sp->ed.morphEdit(id, r - 1, 2); sp->ed.markStateDirty(); sp->refresh(); }
        });
    }
    // ---- ready-made and saved crossroads
    juce::File savedFolder() const { return juce::File(juce::String(pm->userPatchesPath.parent_path().u8string())).getChildFile("Crossroads"); }
    int factoryIndexOf(const std::string &cat, const std::string &file) const
    {
        const auto &v = pm->factoryPatchVector;
        for (size_t i = 0; i < v.size(); ++i)
            if (v[i].first == cat && v[i].second == file) return (int)i;
        return -1;
    }
    // returns how many corners got their sound (a missing factory sound leaves that corner empty)
    int loadRecipe(const CrossRecipe &r)
    {
        int got = 0;
        for (int c = 0; c < 4; ++c)
        {
            const auto &k = r.c[c];
            const std::string file = k.file ? std::string(k.file) + ss::presets::PresetManager::presetExtension : std::string();
            if (!k.folder || factoryIndexOf(k.folder, file) < 0) { m.clear(c); continue; }
            auto *e = m.engineFor(c);
            pm->loadFactoryPreset(e->patchMain, e->mainToAudio, k.folder, file);
            e->reconcileWavetables();
            m.names[(size_t)c] = k.file;
            m.kind[(size_t)c] = M::OWN;
            m.volDb[(size_t)c] = juce::jlimit(-24.f, 6.f, k.volDb);
            m.pan[(size_t)c] = juce::jlimit(-1.f, 1.f, k.pan);
            m.octave[(size_t)c] = juce::jlimit(M::kOctMin, M::kOctMax, k.octave);
            ++got;
        }
        auto set = [this](uint32_t id, double v) { ed.morphEdit(id, v, 0); ed.morphEdit(id, v, 1); ed.morphEdit(id, v, 2); };
        set(M::pX, r.x);
        set(M::pY, r.y);
        set(M::pOrbit, r.orbit);
        set(M::pRate, r.rate);
        set(M::pSize, r.size);
        set(M::pOn, 1.0);
        m.changed++;
        loadedName = r.name;
        ed.tellHost();
        ed.status(juce::String(r.name) + ": " + r.what + ".");
        refresh();
        return got;
    }
    bool loadSaved(const juce::File &f)
    {
        juce::MemoryBlock mb;
        if (f.getSize() > (64 << 20) || !f.loadFileAsData(mb) || !m.fromState(std::string((const char *)mb.getData(), mb.getSize())))
        {
            ed.status("Couldn't read " + f.getFileName() + ".");
            return false;
        }
        for (int c = 0; c < 4; ++c)
            if (m.kind[(size_t)c].load() == M::OWN && m.owned[(size_t)c]) m.owned[(size_t)c]->reconcileWavetables();
        loadedName = f.getFileNameWithoutExtension();
        ed.tellHost();
        ed.status("Crossroads \"" + loadedName + "\" loaded.");
        refresh();
        return true;
    }
    bool saveAs(const juce::String &name)
    {
        const auto clean = juce::File::createLegalFileName(name.trim());
        if (clean.isEmpty()) return false;
        auto dir = savedFolder();
        dir.createDirectory();
        const auto f = dir.getChildFile(clean + ".crossroads");
        const auto st = m.toState(); // (byte for byte: its sections are measured in bytes, so no line-ending conversion)
        if (!f.replaceWithData(st.data(), st.size())) { ed.status("Couldn't save the crossroads."); return false; }
        loadedName = clean;
        ed.status("Saved the crossroads as \"" + clean + "\" (CROSSROADS > YOURS).");
        refresh();
        return true;
    }
    void askSave()
    {
        auto *w = new juce::AlertWindow("Save this crossroads", "Its four sounds, volumes, pans, octaves, the flame and its orbit. A name:", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor("n", loadedName);
        w->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
        w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        w->enterModalState(true, juce::ModalCallbackFunction::create([sp = juce::Component::SafePointer<BlendPage>(this), w](int r) {
            if (r == 1 && sp) sp->saveAs(w->getTextEditorContents("n"));
        }), true);
    }
    void presetsMenu()
    {
        juce::PopupMenu menu, mine;
        menu.addSectionHeader("Ready-made: four sounds that blend");
        for (const auto &r : crossRecipes())
        {
            const auto *rp = &r;
            menu.addItem(juce::String(r.name), true, loadedName == juce::String(r.name), [this, rp] { loadRecipe(*rp); });
        }
        juce::Array<juce::File> files;
        if (savedFolder().isDirectory()) savedFolder().findChildFiles(files, juce::File::findFiles, false, "*.crossroads");
        files.sort();
        for (auto &f : files) mine.addItem(f.getFileNameWithoutExtension(), [this, f] { loadSaved(f); });
        if (files.isEmpty()) mine.addItem("None saved yet", false, false, [] {});
        menu.addSeparator();
        menu.addSubMenu("YOURS", mine);
        bool any = false;
        for (int c = 0; c < 4; ++c) any = any || m.kind[(size_t)c].load() != M::EMPTY;
        menu.addItem("Save this crossroads...", any, false, [this] { askSave(); });
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presetsBtn));
    }
    void setCornerCopy(int c)
    {
        m.setOwn(c, ed.patch.toState(), juce::String(ed.patch.name).toStdString() + " (copy)");
        if (auto *e = m.owned[(size_t)c].get()) e->reconcileWavetables();
        after(c);
    }
    void setCornerFactory(int c, const std::string &cat, const std::string &file)
    {
        auto *e = m.engineFor(c);
        pm->loadFactoryPreset(e->patchMain, e->mainToAudio, cat, file);
        loaded(c, e);
    }
    void setCornerUser(int c, const fs::path &path)
    {
        auto *e = m.engineFor(c);
        pm->loadUserPresetDirect(e->patchMain, e->mainToAudio, path);
        loaded(c, e);
    }
    void loaded(int c, ss::Synth *e)
    {
        e->reconcileWavetables();
        m.names[(size_t)c] = std::string(e->patchMain.name);
        m.kind[(size_t)c] = M::OWN;
        m.changed++;
        after(c);
    }
    void after(int c)
    {
        loadedName.clear();
        ed.markStateDirty();
        const juce::String L = juce::String::charToString((juce::juce_wchar)('A' + c));
        ed.status(m.kind[(size_t)c].load() == M::EMPTY ? "Corner " + L + " is empty." : "Corner " + L + ": " + juce::String(m.names[(size_t)c]) + (m.on.load() ? "" : "  (turn the CROSSROADS on to hear it)"));
        refresh();
    }
    void cornerMenu(int c)
    {
        juce::PopupMenu menu;
        menu.addItem("THE SOUND ON THE TABLE (follows what you change)", [this, c] { m.setThisSound(c); after(c); });
        menu.addItem("A COPY OF IT (stays as it is now)", [this, c] { setCornerCopy(c); });
        menu.addSeparator();
        for (auto &[cat, names] : pm->factoryPatchNames)
        {
            juce::PopupMenu sub;
            for (auto &n : names)
            {
                const auto ct = cat, f = n;
                sub.addItem(juce::String(ss::presets::PresetManager::stripPresetExtension(n)), [this, c, ct, f] { setCornerFactory(c, ct, f); });
            }
            menu.addSubMenu(juce::String(cat), sub);
        }
        juce::PopupMenu user;
        pm->rescanUserPresets();
        for (auto &up : pm->userPatches)
        {
            const auto rel = up;
            user.addItem(juce::String(ss::presets::PresetManager::stripPresetExtension(up.filename().u8string())), [this, c, rel] { setCornerUser(c, pm->userPatchesPath / rel); });
        }
        if (pm->userPatches.empty()) user.addItem("No saved sounds yet (use SAVE)", false, false, [] {});
        menu.addSubMenu("YOUR SOUNDS", user);
        menu.addSeparator();
        menu.addItem("EMPTY", m.kind[(size_t)c].load() != M::EMPTY, false, [this, c] { m.clear(c); after(c); });
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&cornerBtn[c]).withStandardItemHeight(22));
    }
    void refresh()
    {
        const bool on = m.on.load();
        onBtn.lit = on;
        onBtn.text = on ? "CROSSROADS ON" : "CROSSROADS OFF";
        const int orb = m.orbit.load();
        orbitBtn.text = "ORBIT  " + juce::String(M::orbitNames[juce::jlimit(0, M::O_COUNT - 1, orb)]) + "  v";
        rateBtn.text = "EVERY  " + juce::String(M::rateNames[juce::jlimit(0, M::kRates - 1, m.orbitRate.load())]) + "  v";
        orbitBtn.lit = on && orb != M::O_OFF;
        presetsBtn.text = loadedName.isEmpty() ? juce::String("CROSSROADS  v") : loadedName + "  v";
        const bool fixOn = ed.blendGuard && ed.blendGuard->on.load();
        fixBtn.text = fixOn ? "FIX CANCELLING ON" : "FIX CANCELLING OFF";
        fixBtn.lit = on && fixOn;
        fixBtn.setEnabled(on && ed.blendGuard != nullptr); // (nothing to fix with the crossroads off)
        orbitBtn.setEnabled(on);
        rateBtn.setEnabled(on && orb != M::O_OFF); // nothing to time or size while the flame stands still
        sizeK.setEnabled(on && orb != M::O_OFF);
        for (int c = 0; c < 4; ++c)
        {
            const int k = m.kind[(size_t)c].load();
            const juce::String L = juce::String::charToString((juce::juce_wchar)('A' + c));
            cornerBtn[c].text = L + ":  " + (k == M::EMPTY ? juce::String("empty  (pick a sound)") : k == M::THIS_SOUND ? juce::String("THE SOUND ON THE TABLE") : juce::String(m.names[(size_t)c])) + "  v";
            cornerBtn[c].lit = k != M::EMPTY;
            const int o = m.octave[(size_t)c].load();
            octBtn[c].text = "OCTAVE  " + juce::String(o > 0 ? "+" : "") + juce::String(o) + "  v";
            for (juce::Component *x : {(juce::Component *)&vol[c], (juce::Component *)&pan[c], (juce::Component *)&octBtn[c]}) x->setEnabled(on && k != M::EMPTY);
        }
        repaint();
    }
    void tick()
    {
        if (!isVisible()) return;
        const auto c = m.changed.load();
        if (c != lastChange) { lastChange = c; refresh(); }
        if (ed.blendGuard)
        {
            const bool act = ed.blendGuard->active.load();
            shownCancel += ((act ? ed.blendGuard->cancelDb.load() : 0.f) - shownCancel) * 0.25f;
            shownAfter += ((act ? ed.blendGuard->afterDb.load() : 0.f) - shownAfter) * 0.25f;
            repaint(28, 530, 342, 60);
        }
        repaint(board.expanded(16));
    }
    void toBoard(juce::Point<int> p, int type)
    {
        ed.morphEdit(M::pX, juce::jlimit(0.f, 1.f, (float)(p.x - board.getX()) / (float)board.getWidth()), type);
        ed.morphEdit(M::pY, juce::jlimit(0.f, 1.f, (float)(p.y - board.getY()) / (float)board.getHeight()), type);
    }
    void mouseDown(const juce::MouseEvent &e) override
    {
        if (!m.on.load() || !board.expanded(10).contains(e.getPosition())) return;
        dragging = true;
        toBoard(e.getPosition(), 0);
        toBoard(e.getPosition(), 1);
    }
    void mouseDrag(const juce::MouseEvent &e) override { if (dragging) toBoard(e.getPosition(), 1); }
    void mouseUp(const juce::MouseEvent &e) override
    {
        if (!dragging) return;
        dragging = false;
        toBoard(e.getPosition(), 2);
        ed.markStateDirty();
    }
    void paint(juce::Graphics &g) override
    {
        PageBase::paint(g);
        const bool on = m.on.load();
        const float al = on ? 1.f : 0.3f;
        for (int c = 0; c < 4; ++c)
        {
            const auto r = panel(c).toFloat();
            g.setColour(col::PANEL2.withAlpha(0.6f));
            g.fillRoundedRectangle(r, 6.f);
            g.setColour(m.kind[(size_t)c].load() != M::EMPTY ? cornerCol(c).withAlpha(0.5f) : col::EDGE);
            g.drawRoundedRectangle(r, 6.f, 1.f);
        }
        const auto b = board.toFloat();
        g.setGradientFill(juce::ColourGradient(col::WOOD0.withMultipliedAlpha(al), b.getCentreX(), b.getCentreY(), col::SOOT, b.getX(), b.getY(), true));
        g.fillRoundedRectangle(b, 8.f);
        g.setColour(col::EDGE.withMultipliedAlpha(al));
        g.drawRoundedRectangle(b, 8.f, 1.2f);
        g.drawLine(b.getX(), b.getY(), b.getRight(), b.getBottom(), 0.8f); // the two roads
        g.drawLine(b.getRight(), b.getY(), b.getX(), b.getBottom(), 0.8f);
        float w[4]{0, 0, 0, 0};
        const bool any = m.weights(w);
        const float cx[4] = {b.getX(), b.getRight(), b.getX(), b.getRight()}, cy[4] = {b.getY(), b.getY(), b.getBottom(), b.getBottom()};
        const float px = b.getX() + m.effX.load() * b.getWidth(), py = b.getY() + m.effY.load() * b.getHeight();
        for (int c = 0; c < 4; ++c)
        {
            const bool filled = m.kind[(size_t)c].load() != M::EMPTY;
            const auto cc = cornerCol(c);
            const float share = (any && filled) ? w[c] : 0.f;
            if (on && share > 0.002f)
            { // how much of this corner you hear now: a glow and a thread to the flame
                const float rad = 14.f + 70.f * std::sqrt(share);
                g.setGradientFill(juce::ColourGradient(cc.withAlpha(0.45f * share + 0.1f), cx[c], cy[c], cc.withAlpha(0.f), cx[c] + rad, cy[c], true));
                g.saveState();
                g.reduceClipRegion(board);
                g.fillEllipse(cx[c] - rad, cy[c] - rad, 2 * rad, 2 * rad);
                g.restoreState();
                g.setColour(cc.withAlpha(0.15f + 0.7f * share));
                g.drawLine(cx[c], cy[c], px, py, 1.f + 2.5f * share);
            }
            const float ix = cx[c] + (c % 2 ? -22.f : 22.f), iy = cy[c] + (c / 2 ? -22.f : 22.f);
            g.setColour((filled ? cc : col::EDGE).withMultipliedAlpha(al));
            g.drawEllipse(ix - 13, iy - 13, 26, 26, filled ? 1.8f : 1.f);
            g.setFont(F->g(20.f));
            g.drawText(juce::String::charToString((juce::juce_wchar)('A' + c)), (int)ix - 13, (int)iy - 13, 26, 26, juce::Justification::centred);
            if (on && filled)
            {
                g.setFont(F->b(10.5f));
                g.setColour(col::BONE.withAlpha(0.85f));
                g.drawText(juce::String(juce::roundToInt(share * 100)) + " %", (int)ix - 30, (int)(iy + (c / 2 ? -34.f : 16.f)), 60, 14, juce::Justification::centred);
            }
        }
        if (on && m.orbit.load() != M::O_OFF)
        { // where you put it (the flame circles around this)
            const float hx = b.getX() + m.x.load() * b.getWidth(), hy = b.getY() + m.y.load() * b.getHeight();
            g.setColour(col::DIM.withAlpha(0.7f));
            g.drawEllipse(hx - 5, hy - 5, 10, 10, 1.f);
        }
        // the flame
        g.setGradientFill(juce::ColourGradient(col::CANDLE.withAlpha(0.5f * al), px, py, col::CANDLE.withAlpha(0.f), px + 26, py, true));
        g.fillEllipse(px - 26, py - 26, 52, 52);
        juce::Path fl;
        fl.startNewSubPath(px, py - 13);
        fl.cubicTo(px + 9, py - 3, px + 7, py + 8, px, py + 9);
        fl.cubicTo(px - 7, py + 8, px - 9, py - 3, px, py - 13);
        g.setColour(col::CANDLE.withMultipliedAlpha(al));
        g.fillPath(fl);
        g.setColour(col::BONE.withMultipliedAlpha(al));
        g.fillEllipse(px - 2.5f, py - 1, 5, 6);
        if (!on)
        {
            g.setFont(F->r(12.f));
            g.setColour(col::DIM);
            g.drawFittedText("The crossroads is off: you hear only the sound on the table. Pick sounds for the corners, then turn it on.", board.reduced(60, 150), juce::Justification::centred, 3);
        }
        // the phase readout: how much the corners cancel each other, and what the guard did about it
        if (ed.blendGuard)
        {
            const auto rr = juce::Rectangle<int>(28, 532, 342, 56);
            const bool act = on && ed.blendGuard->active.load();
            const bool fixOn = ed.blendGuard->on.load();
            const int fl = ed.blendGuard->flipped.load();
            juce::String line1, line2;
            juce::Colour c1 = col::DIM;
            if (!act)
                line1 = on ? "BLEND: one sound playing, nothing to cancel" : "";
            else if (shownCancel > -1.5f)
            {
                line1 = "BLEND: CLEAN";
                line2 = "The corners add up without cancelling each other.";
                c1 = col::BONE;
            }
            else
            {
                line1 = "CANCELLING " + juce::String(-shownCancel, 1) + " dB";
                c1 = col::BLOOD.brighter(0.4f);
                if (fixOn)
                {
                    juce::String who;
                    for (int k = 0; k < 4; ++k)
                        if (fl & (1 << k)) who += (who.isEmpty() ? "" : ", ") + juce::String::charToString((juce::juce_wchar)('A' + k));
                    if (fl & 16) who += (who.isEmpty() ? "" : ", ") + juce::String("the table's sound");
                    line2 = (who.isEmpty() ? juce::String("Holding the level up") : "Fixed: " + who + " turned over") +
                            (shownAfter > -1.5f ? juce::String(", now adding up.") : "  (still " + juce::String(-shownAfter, 1) + " dB down: try moving the flame)");
                    if (shownAfter > -1.5f) c1 = col::CANDLE;
                }
                else
                    line2 = "The corners are fighting. Turn FIX CANCELLING on, or move the flame.";
            }
            g.setFont(F->b(12.f));
            g.setColour(c1);
            g.drawText(line1, rr.withHeight(18), juce::Justification::centredLeft);
            g.setFont(F->r(11.f));
            g.setColour(col::DIM);
            g.drawFittedText(line2, rr.withTrimmedTop(20), juce::Justification::topLeft, 2);
        }
    }
};

// ---------------------------------------------------------------------------------------------- STEPS (the rite): the note sequencer
// ArpEZator's composers use the recipe ArpEZator ships; without it they fall back to built-in placeholders
static void loadArpezRecipeOnce()
{
    static bool done = false;
    if (done) return;
    done = true;
    try
    {
        auto fs = cmrc::seance::get_filesystem();
        if (!fs.exists("arpez/recipe.json")) return;
        auto f = fs.open("arpez/recipe.json");
        arp::Recipe R;
        if (arprecipe::fromVar(juce::JSON::parse(juce::String::fromUTF8(f.begin(), (int)f.size())), R)) arp::setRecipe(R);
    }
    catch (...)
    {
    }
}
// a tile you drag out of the plugin: it makes a file when the drag starts and hands it to the host / the desktop
struct DragTile : juce::Component
{
    FontsPtr F;
    juce::String text;
    std::function<juce::File()> makeFile;
    std::function<void()> onClick;
    bool started{false}, over{false};
    void paint(juce::Graphics &g) override
    {
        const float al = isEnabled() ? 1.f : 0.3f;
        auto r = getLocalBounds().toFloat().reduced(1.f);
        g.setColour(col::PANEL2.withMultipliedAlpha(al));
        g.fillRoundedRectangle(r, 4.f);
        g.setColour((over ? col::CANDLE : col::EDGE).withMultipliedAlpha(al));
        const float dash[2] = {4.f, 3.f};
        juce::Path pth;
        pth.addRoundedRectangle(r, 4.f);
        juce::Path dashed;
        juce::PathStrokeType(1.2f).createDashedStroke(dashed, pth, dash, 2);
        g.fillPath(dashed);
        g.setFont(F->b(11.f));
        g.setColour(col::BONE.withMultipliedAlpha(al));
        g.drawFittedText(text, getLocalBounds().reduced(4, 0), juce::Justification::centred, 1, 0.8f);
    }
    void mouseEnter(const juce::MouseEvent &) override { over = true; repaint(); }
    void mouseExit(const juce::MouseEvent &) override { over = false; repaint(); }
    void mouseDrag(const juce::MouseEvent &e) override
    {
        if (started || !isEnabled() || e.getDistanceFromDragStart() < 6 || !makeFile) return;
        const auto f = makeFile();
        if (!f.existsAsFile()) return;
        started = true;
        juce::DragAndDropContainer::performExternalDragDropOfFiles({f.getFullPathName()}, false, this, [sp = juce::Component::SafePointer<DragTile>(this)] { if (sp) sp->started = false; });
    }
    void mouseUp(const juce::MouseEvent &e) override
    {
        if (!started && isEnabled() && e.getDistanceFromDragStart() < 6 && onClick) onClick();
        started = false;
    }
};
struct StepsPage : PageBase
{
    SeanceEditor &ed;
    StepSeq &q;
    std::unique_ptr<StepPattern> pat = std::make_unique<StepPattern>(); // the page's copy (what the roll shows and edits)
    Plaque onBtn{ed, Plaque::BUTTON, "STEPS OFF"}, patBtn{ed, Plaque::BUTTON, ""}, newBtn{ed, Plaque::BUTTON, "ANOTHER"}, speedBtn{ed, Plaque::BUTTON, ""},
        octBtn{ed, Plaque::BUTTON, ""}, latchBtn{ed, Plaque::BUTTON, "LATCH"}, drawBtn{ed, Plaque::BUTTON, ""}, loopBtn{ed, Plaque::BUTTON, ""}, followBtn{ed, Plaque::BUTTON, ""};
    DragTile midiTile;
    int drawLane{1};   // the part a new note goes to
    int dragMode{0};   // 1 move, 2 change the length
    int grabTick{0};
    Plaque laneBtn[4] = {{ed, Plaque::BUTTON, "CHORDS"}, {ed, Plaque::BUTTON, "ARP"}, {ed, Plaque::BUTTON, "MELODY"}, {ed, Plaque::BUTTON, "BASS"}};
    GKnob gateK, swingK;
    juce::Rectangle<int> roll{28, 112, 1124, 380};
    static constexpr int gutter = 44;
    int rowLo{-12}, rowHi{24}; // the rows the roll shows (semitones, or held-note numbers)
    uint32_t lastChange{0xffffffffu};
    int dragNote{-1};
    bool dragMoved{false};
    juce::Random rnd;
    static juce::Colour laneCol(int l)
    {
        static const juce::Colour k[4] = {juce::Colour(0xff8f7cff), juce::Colour(0xffffb45a), juce::Colour(0xffff5a6a), juce::Colour(0xff5fd0c0)};
        return k[l & 3];
    }
    StepsPage(SeanceEditor &e, StepSeq &seq) : ed(e), q(seq)
    {
        loadArpezRecipeOnce();
        title = "The rite";
        blurb = "A sequencer in front of the sound. While it is on, the keys you play tell it what to play: one key for a whole idea or a line, or a chord for an arp.";
        onBtn.setName("STEPS ON");
        onBtn.colour = col::BLOOD.brighter(0.3f);
        onBtn.onClick = [this] { q.on = !q.on.load(); ed.tellHost(); refresh(); };
        onBtn.setBounds(28, 70, 120, 30);
        addAndMakeVisible(onBtn);
        patBtn.setName("PATTERN");
        patBtn.colour = col::CANDLE;
        patBtn.onClick = [this] { patternMenu(); };
        patBtn.setBounds(156, 70, 404, 30);
        addAndMakeVisible(patBtn);
        followBtn.setName("CHORDS FROM");
        followBtn.onClick = [this] { if (steppat::isIdea(pat->kind)) make(pat->kind, pat->seed, pat->ladder == 0); };
        followBtn.setBounds(566, 70, 150, 30);
        addAndMakeVisible(followBtn);
        newBtn.setName("ANOTHER");
        newBtn.onClick = [this] { another(); };
        newBtn.setBounds(724, 70, 110, 30);
        addAndMakeVisible(newBtn);
        for (int l = 0; l < 4; ++l)
        {
            laneBtn[l].setName(juce::String("LANE ") + laneBtn[l].text);
            laneBtn[l].colour = laneCol(l);
            laneBtn[l].onClick = [this, l] { q.laneMask = q.laneMask.load() ^ (1 << l); ed.markStateDirty(); refresh(); };
            laneBtn[l].setBounds(846 + l * 77, 70, 74, 30);
            addAndMakeVisible(laneBtn[l]);
        }
        speedBtn.setName("SPEED");
        speedBtn.onClick = [this] {
            juce::PopupMenu m;
            static const char *n[3] = {"Half speed", "As written", "Double speed"};
            for (int i = 0; i < 3; ++i) m.addItem(i + 1, n[i], true, q.speed.load() == i);
            m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&speedBtn), [sp = juce::Component::SafePointer<StepsPage>(this)](int r) {
                if (r > 0 && sp) { sp->q.speed = r - 1; sp->ed.tellHost(); sp->refresh(); }
            });
        };
        speedBtn.setBounds(28, 520, 170, 30);
        addAndMakeVisible(speedBtn);
        octBtn.setName("STEPS OCTAVE");
        octBtn.onClick = [this] {
            juce::PopupMenu m;
            for (int o = 2; o >= -2; --o) m.addItem(o + 3, (o > 0 ? "+" : "") + juce::String(o) + (o == 0 ? "  (where you play)" : ""), true, q.octave.load() == o);
            m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&octBtn), [sp = juce::Component::SafePointer<StepsPage>(this)](int r) {
                if (r > 0 && sp) { sp->q.octave = r - 3; sp->ed.tellHost(); sp->refresh(); }
            });
        };
        octBtn.setBounds(206, 520, 150, 30);
        addAndMakeVisible(octBtn);
        latchBtn.setName("LATCH");
        latchBtn.colour = col::CANDLE;
        latchBtn.onClick = [this] { q.latch = !q.latch.load(); ed.tellHost(); refresh(); };
        latchBtn.setBounds(364, 520, 110, 30);
        addAndMakeVisible(latchBtn);
        gateK.label = "LENGTH"; gateK.setName("LENGTH");
        gateK.lo = 0.1f; gateK.hi = 1.5f; gateK.def = 1.f; gateK.colour = col::CANDLE;
        gateK.get = [this] { return (double)q.gate.load(); };
        gateK.edit = [this](double v, int t) { q.gate = (float)v; if (t == 2) ed.tellHost(); };
        gateK.text = [](double v) { return juce::String(juce::roundToInt(v * 100)) + " %"; };
        gateK.setBounds(500, 500, 84, 96);
        addAndMakeVisible(gateK);
        swingK.label = "SWING"; swingK.setName("SWING");
        swingK.lo = 0.f; swingK.hi = 0.75f; swingK.def = 0.f; swingK.colour = col::CANDLE;
        swingK.get = [this] { return (double)q.swing.load(); };
        swingK.edit = [this](double v, int t) { q.swing = (float)v; if (t == 2) ed.tellHost(); };
        swingK.text = [](double v) { return juce::String(juce::roundToInt(v / 0.75 * 100)) + " %"; };
        swingK.setBounds(592, 500, 84, 96);
        addAndMakeVisible(swingK);
        drawBtn.setName("DRAW ON");
        drawBtn.onClick = [this] {
            juce::PopupMenu m;
            for (int l = 0; l < 4; ++l) m.addItem(l + 1, laneBtn[l].text, true, drawLane == l);
            m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&drawBtn), [sp = juce::Component::SafePointer<StepsPage>(this)](int r) {
                if (r > 0 && sp) { sp->drawLane = r - 1; sp->refresh(); }
            });
        };
        drawBtn.setBounds(28, 562, 170, 28);
        addAndMakeVisible(drawBtn);
        loopBtn.setName("LOOP");
        loopBtn.onClick = [this] {
            juce::PopupMenu m;
            const int bars = pat->loopTicks / StepSeq::BAR;
            for (int b : {1, 2, 4, 8, 16}) m.addItem(b, juce::String(b) + (b == 1 ? " bar" : " bars"), true, bars == b);
            m.addSeparator();
            m.addItem(100, "Twice as long, with what is there repeated", pat->loopTicks <= StepSeq::BAR * 8 && pat->n * 2 <= StepPattern::kMax);
            m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&loopBtn), [sp = juce::Component::SafePointer<StepsPage>(this)](int r) {
                if (r <= 0 || !sp) return;
                if (r == 100) sp->repeatLoop(); else sp->setLoopBars(r);
            });
        };
        loopBtn.setBounds(206, 562, 150, 28);
        addAndMakeVisible(loopBtn);
        midiTile.setName("DRAG MIDI");
        midiTile.text = "DRAG MIDI OUT";
        midiTile.makeFile = [this] { return writeMidiFile(); };
        midiTile.onClick = [this] { ed.status("Drag that tile into the piano roll or the playlist: the pattern as MIDI notes" + juce::String(pat->ladder ? " (over a C minor chord)." : " (in C).")); };
        midiTile.setBounds(364, 562, 110, 28);
        addAndMakeVisible(midiTile);
        pull();
    }
    uint32_t newSeed() { return (uint32_t)rnd.nextInt(0x7ffffffe) + 1u; }
    // the sequencer's pattern -> this page (after a project or a sound loaded)
    void pull()
    {
        q.getPattern(*pat);
        fitRows();
        refresh();
    }
    void fitRows()
    {
        if (pat->ladder)
        {
            int lo = 0, hi = 5;
            for (int i = 0; i < pat->n; ++i) { lo = std::min(lo, rowOf(pat->notes[i])); hi = std::max(hi, rowOf(pat->notes[i])); }
            rowLo = lo < 0 ? lo - 1 : 0;
            rowHi = hi + 2;
            return;
        }
        int lo = 0, hi = 12;
        for (int i = 0; i < pat->n; ++i) { lo = std::min(lo, (int)pat->notes[i].off); hi = std::max(hi, (int)pat->notes[i].off); }
        rowLo = lo - 3;
        rowHi = hi + 3;
        if (rowHi - rowLo < 24) rowHi = rowLo + 24;
    }
    void push(bool refit)
    {
        q.setPattern(*pat);
        if (refit) fitRows();
        ed.markStateDirty();
        refresh();
    }
    void use(const StepPattern &p, int laneMask = -1)
    {
        *pat = p;
        if (laneMask >= 0) q.laneMask = laneMask & 0xF;
        else if (!steppat::isIdea(p.kind)) q.laneMask = 0xF;
        push(true);
        ed.status(juce::String(pat->name) + (q.on.load() ? (pat->ladder ? ".  Hold a chord." : ".  Hold one key.") : ".  Turn STEPS ON, then " + juce::String(pat->ladder ? "hold a chord." : "hold one key.")));
    }
    // follow: an idea re-written over the chords you hold (-1 = as the pattern on the page is now)
    bool make(int kind, uint32_t seed, int follow = -1)
    {
        const bool fol = follow < 0 ? (steppat::isIdea(pat->kind) && pat->ladder == 1) : follow != 0;
        auto p = std::make_unique<StepPattern>();
        int mask = -1;
        bool ok = false;
        using namespace steppat;
        if (isIdea(kind)) ok = arpezIdea(*p, moodOfKind(kind), seed, &mask, fol);
        else if (isFigure(kind)) ok = arpezFigure(*p, moodOfKind(kind), seed);
        else if (kind >= HELD_ARP && kind - HELD_ARP < (int)heldArps().size()) ok = fromBuiltin(*p, heldArps()[(size_t)(kind - HELD_ARP)], true, kind - HELD_ARP);
        else if (kind >= LINE && kind - LINE < (int)lines().size()) ok = fromBuiltin(*p, lines()[(size_t)(kind - LINE)], false, kind - LINE);
        else if (kind == EMPTY_KEY || kind == EMPTY_HELD)
        {
            p->loopTicks = StepSeq::BAR * 2;
            p->ladder = kind == EMPTY_HELD;
            p->kind = kind;
            p->setName(kind == EMPTY_HELD ? "Your own arp  (over the notes you hold)" : "Your own line  (one key)");
            ok = true;
        }
        if (ok) use(*p, mask);
        else ed.status("That pattern came out empty. Try ANOTHER.");
        return ok;
    }
    void another()
    {
        if (steppat::rerollable(pat->kind)) make(pat->kind, newSeed());
    }
    void patternMenu()
    {
        using namespace steppat;
        juce::PopupMenu m, mid, dusk, sun, trap, hip, ln, ha;
        mid.addItem(IDEA_MIDNIGHT + 1, "A new MIDNIGHT idea: chords, arp, melody, bass (hold one key)");
        mid.addItem(FIG_MIDNIGHT + 1, "A new MIDNIGHT arp figure over the chord you hold");
        dusk.addItem(IDEA_DUSK + 1, "A new DUSK idea: chords, arp, melody, bass (hold one key)");
        dusk.addItem(FIG_DUSK + 1, "A new DUSK arp figure over the chord you hold");
        sun.addItem(IDEA_SUNRISE + 1, "A new SUNRISE idea: chords, arp, melody, bass (hold one key)");
        sun.addItem(FIG_SUNRISE + 1, "A new SUNRISE arp figure over the chord you hold");
        trap.addItem(IDEA_TRAP + 1, "A new TRAP idea: chords, arp, melody, 808 line (hold one key)");
        hip.addItem(IDEA_HIPHOP + 1, "A new HIP HOP idea: chords, arp, melody, bass (hold one key)");
        for (size_t i = 0; i < lines().size(); ++i) ln.addItem(LINE + (int)i + 1, lines()[i].name, true, pat->kind == LINE + (int)i);
        for (size_t i = 0; i < heldArps().size(); ++i) ha.addItem(HELD_ARP + (int)i + 1, heldArps()[i].name, true, pat->kind == HELD_ARP + (int)i);
        m.addSubMenu("MIDNIGHT", mid);
        m.addSubMenu("DUSK", dusk);
        m.addSubMenu("SUNRISE", sun);
        m.addSubMenu("TRAP", trap);
        m.addSubMenu("HIP HOP", hip);
        m.addSubMenu("LINES (hold one key)", ln);
        m.addSubMenu("ARPS (hold a chord)", ha);
        m.addSeparator();
        m.addItem(EMPTY_KEY + 1, "Empty: draw your own line (one key)");
        m.addItem(EMPTY_HELD + 1, "Empty: draw your own arp (held chord)");
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&patBtn), [sp = juce::Component::SafePointer<StepsPage>(this)](int r) {
            if (r > 0 && sp) sp->make(r - 1, sp->newSeed());
        });
    }
    void refresh()
    {
        const bool on = q.on.load();
        onBtn.lit = on;
        onBtn.text = on ? "STEPS ON" : "STEPS OFF";
        patBtn.text = juce::String(pat->n == 0 && pat->name[0] == 0 ? "PICK A PATTERN" : pat->name) + "  v";
        patBtn.lit = pat->n > 0;
        newBtn.setEnabled(steppat::rerollable(pat->kind)); // only the composed kinds have another to give
        int used = 0;
        for (int i = 0; i < pat->n; ++i) used |= 1 << (pat->notes[i].lane & 3);
        const bool lanes = steppat::isIdea(pat->kind) || (used & (used - 1)) != 0; // parts to switch: an idea, or notes on more than one part
        drawBtn.text = "DRAW ON  " + laneBtn[drawLane].text + "  v";
        drawBtn.colour = laneCol(drawLane);
        const int bars = pat->loopTicks / StepSeq::BAR;
        loopBtn.text = "LOOP  " + juce::String(bars) + (bars == 1 ? " BAR" : " BARS") + "  v";
        midiTile.setEnabled(pat->n > 0); // nothing to drag out of an empty pattern
        const bool idea = steppat::isIdea(pat->kind);
        followBtn.setEnabled(idea); // only a composed idea has chords of its own to swap for yours
        followBtn.text = idea && pat->ladder ? "CHORDS: YOURS" : "CHORDS: ITS OWN";
        followBtn.lit = idea && pat->ladder;
        const int mask = q.laneMask.load();
        for (int l = 0; l < 4; ++l)
        {
            laneBtn[l].lit = lanes && (mask & (1 << l));
            laneBtn[l].setEnabled(lanes);
        }
        static const char *sn[3] = {"HALF SPEED", "AS WRITTEN", "DOUBLE SPEED"};
        speedBtn.text = juce::String("SPEED  ") + sn[juce::jlimit(0, 2, q.speed.load())] + "  v";
        const int o = q.octave.load();
        octBtn.text = "OCTAVE  " + juce::String(o > 0 ? "+" : "") + juce::String(o) + "  v";
        latchBtn.lit = on && q.latch.load();
        for (juce::Component *x : {(juce::Component *)&speedBtn, (juce::Component *)&octBtn, (juce::Component *)&latchBtn, (juce::Component *)&gateK, (juce::Component *)&swingK}) x->setEnabled(on);
        repaint();
    }
    void tick()
    {
        if (!isVisible()) return;
        const auto c = q.changed.load();
        if (c != lastChange) { lastChange = c; pull(); }
        if (q.running.load() || wasRunning) repaint(roll.expanded(4));
        wasRunning = q.running.load();
    }
    bool wasRunning{false};
    // ---- the loop
    void setLoopBars(int bars)
    {
        const int loop = std::clamp(bars, 1, 16) * StepSeq::BAR;
        int k = 0;
        for (int i = 0; i < pat->n; ++i)
            if (pat->notes[i].t < loop) pat->notes[k++] = pat->notes[i]; // (shorter: what lies past the end goes)
        pat->n = k;
        pat->loopTicks = loop;
        push(false);
    }
    void repeatLoop()
    {
        const int loop = pat->loopTicks, n0 = pat->n;
        if (loop > StepSeq::BAR * 8 || n0 * 2 > StepPattern::kMax) return;
        for (int i = 0; i < n0; ++i) { auto x = pat->notes[i]; x.t += loop; pat->notes[pat->n++] = x; }
        pat->loopTicks = loop * 2;
        push(false);
    }
    void copyBar(int bar, bool toAll)
    {
        const int B = StepSeq::BAR, bars = pat->loopTicks / B;
        std::vector<StepNote> src;
        for (int i = 0; i < pat->n; ++i)
            if (pat->notes[i].t / B == bar) src.push_back(pat->notes[i]);
        for (int b = 0; b < bars; ++b)
        {
            if (b == bar || (!toAll && b != bar + 1)) continue;
            int k = 0;
            for (int i = 0; i < pat->n; ++i)
                if (pat->notes[i].t / B != b) pat->notes[k++] = pat->notes[i];
            pat->n = k;
            for (auto x : src) { x.t += (b - bar) * B; if (pat->n < StepPattern::kMax) pat->notes[pat->n++] = x; }
        }
        pat->sortNotes();
        push(false);
    }
    void clearBar(int bar)
    {
        int k = 0;
        for (int i = 0; i < pat->n; ++i)
            if (bar < 0 ? false : pat->notes[i].t / StepSeq::BAR != bar) pat->notes[k++] = pat->notes[i];
        pat->n = k;
        push(false);
    }
    // ---- MIDI out: the pattern as it plays (parts that are on, LENGTH, SWING, SPEED, OCTAVE), from C4 / over C minor
    std::vector<uint8_t> midiBytes() const
    {
        arp::MidiTrack tr;
        tr.name = "Seance";
        tr.ch = 0;
        const int mask = q.laneMask.load(), oc = 12 * q.octave.load();
        const double gate = q.gate.load(), sw = q.swing.load(), stretch = q.speed.load() == 0 ? 2.0 : q.speed.load() == 2 ? 0.5 : 1.0;
        static const int triad[3] = {60, 63, 67};
        for (int i = 0; i < pat->n; ++i)
        {
            const auto &n = pat->notes[i];
            if (!(mask & (1 << (n.lane & 3)))) continue;
            int key;
            if (pat->ladder) { const int idx = n.off & 63; key = triad[idx % 3] + 12 * (idx / 3) + 12 * ((n.off - idx) / 64) + n.nudge() + oc; }
            else key = 60 + n.off + oc;
            if (key < 0 || key > 127) continue;
            double t = n.t;
            if (sw > 0 && n.t % 24 == 0 && ((n.t / 24) & 1)) t += sw * 12.0;
            tr.notes.push_back({(int)std::lround(t * stretch), std::max(3, (int)std::lround(std::max(6.0, n.d * gate) * stretch)), key, n.vel / 127.0});
        }
        if (tr.notes.empty()) return {};
        return arp::writeMidi(120.0, {tr});
    }
    juce::File writeMidiFile() const
    {
        const auto bytes = midiBytes();
        if (bytes.empty()) return {};
        auto nm = juce::String(pat->name).upToFirstOccurrenceOf("  ", false, false).retainCharacters("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 #-").trim();
        if (nm.isEmpty()) nm = "pattern";
        const auto f = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("Seance - " + nm + ".mid");
        f.deleteFile();
        return f.replaceWithData(bytes.data(), bytes.size()) ? f : juce::File();
    }
    void noteMenu(int i)
    {
        juce::PopupMenu m, part;
        const auto &n = pat->notes[i];
        m.addItem(1, "Loud", true, n.vel >= 112);
        m.addItem(2, "Medium", true, n.vel >= 80 && n.vel < 112);
        m.addItem(3, "Soft", true, n.vel >= 52 && n.vel < 80);
        m.addItem(4, "Ghost (very soft)", true, n.vel < 52);
        m.addSeparator();
        m.addItem(5, "Up an octave");
        m.addItem(6, "Down an octave");
        for (int l = 0; l < 4; ++l) part.addItem(10 + l, laneBtn[l].text, true, (n.lane & 3) == l);
        m.addSubMenu("Belongs to", part);
        m.addItem(8, "Set its loudness and length exactly...");
        m.addSeparator();
        m.addItem(9, "Remove");
        m.showMenuAsync(juce::PopupMenu::Options(), [sp = juce::Component::SafePointer<StepsPage>(this), i](int r) {
            if (r <= 0 || !sp || i >= sp->pat->n) return;
            if (r == 8) { sp->askExact(i); return; }
            auto &x = sp->pat->notes[i];
            static const int vel[4] = {120, 96, 64, 40};
            if (r >= 1 && r <= 4) x.vel = (uint8_t)vel[r - 1];
            else if (r == 5) x.off = (int16_t)(sp->pat->ladder ? x.off + 64 : std::min(120, x.off + 12));
            else if (r == 6) x.off = (int16_t)(sp->pat->ladder ? x.off - 64 : std::max(-120, x.off - 12));
            else if (r >= 10 && r <= 13) x.lane = (uint8_t)((x.lane & 0xF0) | (r - 10));
            else if (r == 9) { for (int k = i; k + 1 < sp->pat->n; ++k) sp->pat->notes[k] = sp->pat->notes[k + 1]; --sp->pat->n; }
            sp->push(r == 5 || r == 6);
        });
    }
    // exact values for one note: loudness in percent, length in sixteenths (0.5 = a thirty-second, 4 = a quarter)
    bool setExact(int i, double loudPercent, double sixteenths)
    {
        if (i < 0 || i >= pat->n || !std::isfinite(loudPercent) || !std::isfinite(sixteenths)) return false;
        auto &n = pat->notes[i];
        n.vel = (uint8_t)juce::jlimit(1, 127, juce::roundToInt(loudPercent * 1.27));
        n.d = juce::jlimit(3, std::max(3, (int)pat->loopTicks - n.t), juce::roundToInt(sixteenths * 24.0));
        push(false);
        return true;
    }
    void askExact(int i)
    {
        const auto &n = pat->notes[i];
        auto *w = new juce::AlertWindow("This note", "Loudness: 1 to 100.  Length: in sixteenths (0.5 = half a step, 1 = one step, 4 = a quarter note).", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor("loud", juce::String(juce::roundToInt(n.vel / 1.27)), "Loudness %");
        w->addTextEditor("len", juce::String(n.d / 24.0, 2), "Length (sixteenths)");
        w->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
        w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        w->enterModalState(true, juce::ModalCallbackFunction::create([sp = juce::Component::SafePointer<StepsPage>(this), w, i](int r) {
            if (r == 1 && sp) sp->setExact(i, w->getTextEditorContents("loud").getDoubleValue(), w->getTextEditorContents("len").getDoubleValue());
        }), true);
    }
    void barMenu(int bar)
    {
        juce::PopupMenu m;
        const int bars = pat->loopTicks / StepSeq::BAR;
        const juce::String b = "bar " + juce::String(bar + 1);
        m.addItem(1, "Copy " + b + " onto the next bar", bar + 1 < bars);
        m.addItem(2, "Copy " + b + " onto every bar", bars > 1);
        m.addItem(3, "Clear " + b);
        m.addSeparator();
        m.addItem(4, "Clear everything", pat->n > 0);
        m.showMenuAsync(juce::PopupMenu::Options(), [sp = juce::Component::SafePointer<StepsPage>(this), bar](int r) {
            if (r <= 0 || !sp) return;
            if (r == 1) sp->copyBar(bar, false);
            else if (r == 2) sp->copyBar(bar, true);
            else if (r == 3) sp->clearBar(bar);
            else { sp->pat->n = 0; sp->push(false); }
        });
    }
    double tickAt(float x) const { return (double)(x - grid().getX()) / grid().getWidth() * pat->loopTicks; }
    // ---- the roll
    int rows() const { return rowHi - rowLo + 1; }
    juce::Rectangle<float> grid() const { return roll.toFloat().withTrimmedLeft((float)gutter); }
    float xOf(double tick) const { const auto g = grid(); return g.getX() + (float)(tick / (double)std::max(24, (int)pat->loopTicks)) * g.getWidth(); }
    float yOfRow(int row) const { const auto g = grid(); return g.getBottom() - (float)(row - rowLo + 1) * g.getHeight() / (float)rows(); }
    int rowAt(float y) const { const auto g = grid(); return juce::jlimit(rowLo, rowHi, rowLo + (int)std::floor((g.getBottom() - y) / (g.getHeight() / (float)rows()))); }
    // a ladder note's row: which held note, with an octave drawn as four rows (so lifted and dropped notes sit apart)
    static int ladderRow(int off) { const int idx = off & 63; return idx + 4 * ((off - idx) / 64); }
    static int ladderOffOfRow(int row) { return row >= 0 ? row : ((row % 4) + 4) % 4 + 64 * (int)std::floor(row / 4.0); }
    int rowOf(const StepNote &n) const { return pat->ladder ? ladderRow(n.off) : (int)n.off; }
    int noteAt(juce::Point<float> p) const
    {
        const float rh = grid().getHeight() / (float)rows();
        for (int i = pat->n - 1; i >= 0; --i)
        {
            const auto &n = pat->notes[i];
            const float x0 = xOf(n.t), x1 = std::max(x0 + 4.f, xOf(n.t + n.d)), y = yOfRow(rowOf(n));
            if (p.x >= x0 - 1 && p.x <= x1 + 1 && p.y >= y && p.y <= y + rh) return i;
        }
        return -1;
    }
    void mouseDown(const juce::MouseEvent &e) override
    {
        dragNote = -1;
        dragMoved = false;
        dragMode = 0;
        const auto p = e.position;
        if (!grid().contains(p)) return;
        const int hit = noteAt(p);
        if (e.mods.isPopupMenu())
        { // right-click: the note's own menu, or the bar's
            if (hit >= 0) noteMenu(hit);
            else barMenu(juce::jlimit(0, std::max(0, pat->loopTicks / StepSeq::BAR - 1), (int)(tickAt(p.x) / StepSeq::BAR)));
            return;
        }
        if (hit >= 0)
        {
            dragNote = hit;
            const auto &n = pat->notes[hit];
            const float x1 = std::max(xOf(n.t) + 4.f, xOf(n.t + n.d));
            dragMode = (p.x >= x1 - 5.f && x1 - xOf(n.t) >= 9.f) ? 2 : 1; // its right end: the length; anywhere else: move it
            grabTick = (int)tickAt(p.x) - n.t;
            return;
        }
        // an empty square: a new sixteenth there, on the part DRAW ON names
        const int t = juce::jlimit(0, std::max(0, (int)pat->loopTicks - 24), (int)std::floor(tickAt(p.x) / 24.0) * 24);
        if (pat->add(t, 20, pat->ladder ? ladderOffOfRow(rowAt(p.y)) : rowAt(p.y), 100, drawLane))
        {
            dragNote = pat->n - 1;
            dragMode = 2; // keep dragging to the right: make it longer
            dragMoved = true; // (a note just made is not removed by this same click)
            push(false);
        }
    }
    void mouseDrag(const juce::MouseEvent &e) override
    {
        if (dragNote < 0 || dragNote >= pat->n || dragMode == 0) return;
        auto &n = pat->notes[dragNote];
        if (dragMode == 2)
        {
            const int end = (int)std::ceil(tickAt(e.position.x) / 24.0) * 24;
            const int d = juce::jlimit(20, std::max(20, (int)pat->loopTicks - n.t), end - n.t - 4);
            if (d == n.d) return;
            n.d = d;
            dragMoved = true;
            push(false);
            return;
        }
        const int row = rowAt(e.position.y);
        const int t = juce::jlimit(0, std::max(0, (int)pat->loopTicks - 24), (int)std::floor((tickAt(e.position.x) - grabTick + 12) / 24.0) * 24);
        if (row == rowOf(n) && (t == n.t || e.getDistanceFromDragStart() < 4)) return;
        if (pat->ladder)
        { // up or down by held notes; its octave stays
            const int idx = n.off & 63;
            n.off = (int16_t)(n.off - idx + juce::jlimit(0, 63, idx + row - rowOf(n)));
        }
        else
            n.off = (int16_t)row;
        if (e.getDistanceFromDragStart() >= 4) n.t = t;
        dragMoved = true;
        push(false);
    }
    void mouseUp(const juce::MouseEvent &e) override
    {
        if (e.mods.isPopupMenu()) { dragNote = -1; return; }
        if (dragNote >= 0 && dragNote < pat->n && !dragMoved)
        { // a plain click on a note removes it
            for (int i = dragNote; i + 1 < pat->n; ++i) pat->notes[i] = pat->notes[i + 1];
            --pat->n;
            push(false);
        }
        else if (dragNote >= 0)
        {
            pat->sortNotes();
            push(false);
        }
        dragNote = -1;
        dragMode = 0;
    }
    void mouseWheelMove(const juce::MouseEvent &e, const juce::MouseWheelDetails &w) override
    { // the wheel over a note: its loudness
        const int hit = grid().contains(e.position) ? noteAt(e.position) : -1;
        if (hit < 0 || std::abs(w.deltaY) < 1e-4f) return;
        auto &n = pat->notes[hit];
        n.vel = (uint8_t)juce::jlimit(8, 127, (int)n.vel + (w.deltaY > 0 ? 8 : -8));
        push(false);
        ed.status("Loudness " + juce::String(juce::roundToInt(n.vel / 1.27f)) + " %");
    }
    void paint(juce::Graphics &g) override
    {
        PageBase::paint(g);
        const bool on = q.on.load();
        const auto r = roll.toFloat(), gr = grid();
        g.setColour(col::SOOT);
        g.fillRoundedRectangle(r, 6.f);
        g.setColour(col::EDGE);
        g.drawRoundedRectangle(r, 6.f, 1.f);
        const int loop = std::max(24, (int)pat->loopTicks), nr = rows();
        const float rh = gr.getHeight() / (float)nr;
        // rows: the held key (or the lowest held note) is the bright one
        g.setFont(F->b(9.5f));
        for (int row = rowLo; row <= rowHi; ++row)
        {
            const float y = yOfRow(row);
            const bool home = row == 0, oct = !pat->ladder && row % 12 == 0;
            if (home || oct)
            {
                g.setColour((home ? col::CANDLE : col::EDGE).withAlpha(home ? 0.16f : 0.25f));
                g.fillRect(gr.getX(), y, gr.getWidth(), rh);
            }
            const bool label = pat->ladder ? true : (home || oct || rh >= 11.f);
            if (label && rh >= 6.5f)
            {
                g.setColour(home ? col::CANDLE : col::DIM);
                const juce::String t = pat->ladder ? (row == 0 ? juce::String("low") : row > 0 ? juce::String(row + 1) : juce::String())
                                                   : (row == 0 ? juce::String("KEY") : (row > 0 ? "+" : "") + juce::String(row));
                g.drawText(t, (int)r.getX() + 2, (int)y, gutter - 6, (int)std::max(8.f, rh), juce::Justification::centredRight);
            }
            if (rh >= 5.f)
            {
                g.setColour(col::EDGE.withAlpha(0.35f));
                g.drawHorizontalLine((int)y, gr.getX(), gr.getRight());
            }
        }
        // time: bars, beats, sixteenths
        const float six = gr.getWidth() * 24.f / (float)loop;
        for (int t = 0; t <= loop; t += 24)
        {
            const bool bar = t % StepSeq::BAR == 0, beat = t % StepSeq::PPQ == 0;
            if (!bar && !beat && six < 5.f) continue;
            const float x = xOf(t);
            g.setColour(col::EDGE.withAlpha(bar ? 1.f : beat ? 0.6f : 0.25f));
            g.drawVerticalLine((int)x, gr.getY(), gr.getBottom());
            if (bar && t < loop)
            {
                g.setColour(col::DIM);
                g.drawText(juce::String(t / StepSeq::BAR + 1), (int)x + 3, (int)gr.getY() + 1, 24, 11, juce::Justification::centredLeft);
            }
        }
        // notes
        const int mask = q.laneMask.load();
        for (int i = 0; i < pat->n; ++i)
        {
            const auto &n = pat->notes[i];
            const int row = rowOf(n);
            if (row < rowLo || row > rowHi) continue;
            const float x0 = xOf(n.t), x1 = std::min(gr.getRight(), std::max(x0 + 3.f, xOf(n.t + n.d) - 1.f)), y = yOfRow(row);
            const bool plays = mask & (1 << (n.lane & 3));
            auto c = laneCol(n.lane).withMultipliedAlpha(plays ? (0.45f + 0.55f * (float)n.vel / 127.f) : 0.18f);
            if (!on) c = c.withMultipliedSaturation(0.5f);
            g.setColour(c);
            g.fillRoundedRectangle(x0, y + 0.5f, x1 - x0, std::max(2.f, rh - 1.f), 1.5f);
            if (pat->ladder && n.off >= 64)
            { // lifted an octave
                g.setColour(col::BONE.withAlpha(plays ? 0.9f : 0.3f));
                g.fillRect(x0, y + 0.5f, x1 - x0, 1.5f);
            }
        }
        if (pat->n == 0)
        {
            g.setFont(F->r(13.f));
            g.setColour(col::DIM);
            g.drawFittedText("Nothing here yet. Pick a pattern above, or click the squares to write your own.", gr.reduced(120, 120).toNearestInt(), juce::Justification::centred, 2);
        }
        if (q.running.load())
        { // where it is in the loop
            const float x = xOf(std::fmod((double)q.posTicks.load(), (double)loop));
            g.setColour(col::BONE.withAlpha(0.9f));
            g.fillRect(x - 0.75f, gr.getY(), 1.5f, gr.getHeight());
        }
        // how to use it
        g.setFont(F->r(11.5f));
        g.setColour(col::DIM);
        const juce::String how = pat->ladder ? (steppat::isIdea(pat->kind) ? "This idea is written over the chord you hold: \"low\" is your lowest note (its root), 2 the next one up; four rows = an octave. Change chord and it follows."
                                                                           : "This pattern plays the notes you hold: \"low\" is your lowest note, 2 the next one up, and past the top it carries on an octave higher.")
                                             : "This pattern plays from one key: KEY is the note you hold, the numbers are semitones above or below it. Play another key and it moves there.";
        g.drawFittedText(how + "\nClick an empty square: a note (keep dragging right for a longer one). Click a note: gone. Drag it: move it. Drag its right end: its length. "
                               "Wheel over it: loudness. Right-click a note or a bar for more. With the song playing it stays on the song's beat.",
                         juce::Rectangle<int>(700, 502, 452, 96), juce::Justification::topLeft, 7, 0.9f);
    }
};

// ---------------------------------------------------------------------------------------------- the output meter (click: limiter on / off)
struct OutMeter : juce::Component
{
    SeanceEditor &ed;
    float shownL{0}, shownR{0}, red{0}; // what the bars show now (falling back slowly); how recently the limiter acted
    explicit OutMeter(SeanceEditor &e) : ed(e) { setName("LIMITER"); }
    static float pos(float peak) { return peak <= 1e-4f ? 0.f : juce::jlimit(0.f, 1.f, (20.f * std::log10(peak) + 48.f) / 48.f); } // -48 dB .. 0 dB
    void tick()
    {
        if (!ed.guard) return;
        const float l = ed.guard->peakL.exchange(0.f), r = ed.guard->peakR.exchange(0.f), g = ed.guard->reduction.exchange(1.f);
        shownL = std::max(l, shownL * 0.86f);
        shownR = std::max(r, shownR * 0.86f);
        red = g < 0.999f ? 1.f : red * 0.9f;
        repaint();
    }
    void toggle()
    {
        if (!ed.guard) return;
        ed.guard->on = !ed.guard->on.load();
        ed.tellHost();
        ed.status(ed.guard->on.load() ? "Limiter ON: if the sound would clip, the plugin turns itself down for that moment."
                                      : "Limiter OFF: the plugin's output is untouched and can go over full scale.");
        repaint();
    }
    void mouseDown(const juce::MouseEvent &) override { toggle(); }
    void paint(juce::Graphics &g) override
    {
        const bool on = ed.guard && ed.guard->on.load();
        const auto b = getLocalBounds().toFloat();
        const float h = (b.getHeight() - 2.f) * 0.5f;
        for (int c = 0; c < 2; ++c)
        {
            const float v = c ? shownR : shownL;
            const auto r = juce::Rectangle<float>(b.getX(), b.getY() + (float)c * (h + 2.f), b.getWidth(), h);
            g.setColour(col::PANEL2);
            g.fillRect(r);
            const bool over = v > 1.0f;
            g.setColour(over && !on ? col::BLOOD.brighter(0.4f) : (red > 0.3f ? col::CANDLE.interpolatedWith(col::BLOOD.brighter(0.4f), red) : col::CANDLE.withAlpha(0.85f)));
            g.fillRect(r.withWidth(r.getWidth() * pos(v)));
        }
        g.setColour(on ? col::EDGE : col::BLOOD);
        g.drawRect(b.expanded(1.f), 1.f); // (a red frame: the limiter is off)
    }
};

// ---------------------------------------------------------------------------------------------- the look of the controls JUCE draws
// (the FX rack is a ready-made panel of JUCE sliders, tick boxes and menus: drawn here like the rest of Seance)
struct SeanceLook : juce::LookAndFeel_V4
{
    FontsPtr F;
    void drawRotarySlider(juce::Graphics &g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider &sl) override
    {
        const float al = sl.isEnabled() ? 1.f : 0.25f;
        const auto b = juce::Rectangle<float>((float)x, (float)y, (float)w, (float)h);
        const float rad = juce::jmin(b.getWidth(), b.getHeight()) * 0.5f - 4.f;
        const auto c = b.getCentre();
        const float a = a0 + pos * (a1 - a0);
        const auto colour = col::BLOOD.brighter(0.35f);
        juce::Path tr, va;
        tr.addCentredArc(c.x, c.y, rad, rad, 0, a0, a1, true);
        g.setColour(col::EDGE.withMultipliedAlpha(al));
        g.strokePath(tr, juce::PathStrokeType(3.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        va.addCentredArc(c.x, c.y, rad, rad, 0, a0, a, true);
        g.setColour(colour.withMultipliedAlpha(al * 0.25f));
        g.strokePath(va, juce::PathStrokeType(7.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(colour.withMultipliedAlpha(al));
        g.strokePath(va, juce::PathStrokeType(3.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const float ir = juce::jmax(2.f, rad - 6.f);
        g.setGradientFill(juce::ColourGradient(col::PANEL2.brighter(0.25f).withMultipliedAlpha(al), c.x, c.y - ir, col::SOOT.withMultipliedAlpha(al), c.x, c.y + ir, false));
        g.fillEllipse(c.x - ir, c.y - ir, 2 * ir, 2 * ir);
        g.setColour(col::EDGE.withMultipliedAlpha(al));
        g.drawEllipse(c.x - ir, c.y - ir, 2 * ir, 2 * ir, 1.f);
        g.setColour((sl.isMouseOverOrDragging() ? colour : col::BONE).withMultipliedAlpha(al));
        g.drawLine(c.x + std::sin(a) * ir * 0.35f, c.y - std::cos(a) * ir * 0.35f, c.x + std::sin(a) * ir * 0.9f, c.y - std::cos(a) * ir * 0.9f, 2.2f);
    }
    void drawToggleButton(juce::Graphics &g, juce::ToggleButton &b, bool over, bool) override
    {
        const float al = b.isEnabled() ? 1.f : 0.3f;
        auto r = b.getLocalBounds().toFloat().reduced(1.f);
        const bool on = b.getToggleState();
        const auto colour = col::CANDLE;
        g.setColour((on ? colour.withAlpha(0.22f) : col::PANEL2).withMultipliedAlpha(al));
        g.fillRoundedRectangle(r, 4.f);
        g.setColour((on ? colour : (over ? col::DIM : col::EDGE)).withMultipliedAlpha(al));
        g.drawRoundedRectangle(r, 4.f, on ? 1.6f : 1.f);
        g.setFont(F->b(11.f));
        g.setColour((on ? colour.brighter(0.3f) : col::BONE).withMultipliedAlpha(al));
        g.drawFittedText(b.getButtonText(), r.reduced(4, 0).toNearestInt(), juce::Justification::centred, 1, 0.7f);
    }
    void drawButtonBackground(juce::Graphics &g, juce::Button &b, const juce::Colour &, bool over, bool down) override
    {
        const float al = b.isEnabled() ? 1.f : 0.3f;
        auto r = b.getLocalBounds().toFloat().reduced(1.f);
        g.setColour((down ? col::BLOOD.withAlpha(0.4f) : col::PANEL2).withMultipliedAlpha(al));
        g.fillRoundedRectangle(r, 4.f);
        g.setColour((over ? col::DIM : col::EDGE).withMultipliedAlpha(al));
        g.drawRoundedRectangle(r, 4.f, 1.f);
    }
    juce::Font getTextButtonFont(juce::TextButton &, int) override { return F->b(11.f); }
    juce::Font getComboBoxFont(juce::ComboBox &) override { return F->b(11.5f); }
    juce::Font getLabelFont(juce::Label &l) override { return F->b(juce::jmin(13.f, l.getFont().getHeight())); }
    juce::Font getPopupMenuFont() override { return F->r(14.f); }
    void drawComboBox(juce::Graphics &g, int w, int h, bool, int, int, int, int, juce::ComboBox &box) override
    {
        const float al = box.isEnabled() ? 1.f : 0.3f;
        juce::Rectangle<float> r(1.f, 1.f, (float)w - 2.f, (float)h - 2.f);
        g.setColour(col::PANEL2.withMultipliedAlpha(al));
        g.fillRoundedRectangle(r, 4.f);
        g.setColour((box.isMouseOver(true) ? col::DIM : col::EDGE).withMultipliedAlpha(al));
        g.drawRoundedRectangle(r, 4.f, 1.f);
        juce::Path p;
        const float cx = (float)w - 13.f, cy = (float)h * 0.5f;
        p.addTriangle(cx - 4.f, cy - 2.f, cx + 4.f, cy - 2.f, cx, cy + 3.f);
        g.setColour(col::DIM.withMultipliedAlpha(al));
        g.fillPath(p);
    }
};
} // namespace

// =========================================================================================================
struct SeanceEditor::Impl : juce::Component
{
    SeanceEditor &ed;
    ss::Patch &P;
    FontsPtr F;
    std::unique_ptr<ss::presets::PresetManager> pm;
    Table table;
    Keys keys;
    juce::TooltipWindow tips{this, 700};
    SeanceLook lnf; // the colours and the drawing the FX rack, the menus and the dialogs take
    std::unique_ptr<juce::FileChooser> chooser; // (kept alive while its window is open)
    int page{0};
    Plaque pageBtn[5] = {{ed, Plaque::BUTTON, "TABLE"}, {ed, Plaque::BUTTON, "FX"}, {ed, Plaque::BUTTON, "SUB"}, {ed, Plaque::BUTTON, "CROSS"}, {ed, Plaque::BUTTON, "STEPS"}};
    std::unique_ptr<StepsPage> stepsPage;
    std::unique_ptr<BlendPage> blendPage;
    FxPage fxPage;
    std::unique_ptr<SubPage> subPage;

    enum Mode { SPIRIT, THREAD, CIRCLE } mode{SPIRIT};
    int envMode{0}; // SPIRIT: 0 heard, 1 feedback, 2 pitch, 3 body (a beast's own knobs), 4 throat (its filter), 5 move, 6 hands (FOLLOWS)
    juce::String title, subtitle, statusText;

    // the panel (x 716 .. 1164)
    static constexpr int PX = 716, PW = 448;
    Plaque tgA{ed, Plaque::TOGGLE, "AWAKE"}, tgB{ed, Plaque::TOGGLE, "HEARD"}, choice{ed, Plaque::CHOICE, "WAVE"};
    Plaque species{ed, Plaque::CHOICE, "SPECIES"};
    BeastWorld *uiWorld{nullptr}; // the editor's own copy of the Surge oscillator setups: names, defaults, value texts
    bool hasBeast{false};
    std::array<std::unique_ptr<GKnob>, 7> BK; // a beast's own seven knobs (the BODY tab)
    std::array<std::unique_ptr<Knob>, 5> K;
    std::array<std::unique_ptr<Plaque>, 7> envTab;
    // FOLLOWS: something you do while playing (velocity, the mod wheel, a macro ...) pushes one thing.
    // 0 a spirit's volume, 1 its feedback, 2 its pitch (the HANDS tab); 3 a thread's depth. Each is the node's first modulation slot.
    // 0-3 use the node's first modulation slot, 4-7 the same four again on its second slot ("and also")
    Plaque followBtn[8] = {{ed, Plaque::BUTTON, ""}, {ed, Plaque::BUTTON, ""}, {ed, Plaque::BUTTON, ""}, {ed, Plaque::BUTTON, ""},
                           {ed, Plaque::BUTTON, ""}, {ed, Plaque::BUTTON, ""}, {ed, Plaque::BUTTON, ""}, {ed, Plaque::BUTTON, ""}};
    std::array<std::unique_ptr<Knob>, 8> FK;
    Param *fSrc[8]{}, *fTgt[8]{}, *fDep[8]{};
    int fDirect[8]{10, 10, 11, 10, 10, 10, 11, 10};
    Plaque macroBtn{ed, Plaque::BUTTON, "MACROS"};
    bool macroView{false};
    std::array<std::unique_ptr<Knob>, 6> MACK;
    Plaque macroNameBtn[6] = {{ed, Plaque::BUTTON, "NAME IT"}, {ed, Plaque::BUTTON, "NAME IT"}, {ed, Plaque::BUTTON, "NAME IT"},
                              {ed, Plaque::BUTTON, "NAME IT"}, {ed, Plaque::BUTTON, "NAME IT"}, {ed, Plaque::BUTTON, "NAME IT"}};
    // MOVE: the spirit's own LFO and its PITCH envelope sweep its throat's cutoff and, on a shade, one BODY knob
    std::array<std::unique_ptr<Knob>, 6> MK; // LFO RATE, LFO SHAPE, LFO > CUTOFF, ENV > CUTOFF, LFO > body, ENV > body
    Plaque moveTarget{ed, Plaque::CHOICE, "MOVES"};
    Plaque throatBtn{ed, Plaque::BUTTON, "THROAT"}; // which Surge filter this spirit sings through
    Plaque tableBtn{ed, Plaque::BUTTON, "TABLE FILE"}; // a shade's own wavetable file (takes WAVE's place on a shade)
    std::array<std::unique_ptr<Knob>, 3> TK;         // CUTOFF, RESONANCE, KEYTRACK
    Plaque envOn{ed, Plaque::TOGGLE, "ENVELOPE ON"};
    EnvView envView;
    std::array<std::unique_ptr<Knob>, 7> EK;
    std::array<std::unique_ptr<Knob>, 5> XK; // the circle's second row
    Plaque cutBtn{ed, Plaque::BUTTON, "CUT THIS THREAD"};
    // the sound bar
    Plaque prevBtn{ed, Plaque::BUTTON, "<"}, nextBtn{ed, Plaque::BUTTON, ">"}, presetBtn{ed, Plaque::BUTTON, "INIT"}, saveBtn{ed, Plaque::BUTTON, "SAVE"},
        initBtn{ed, Plaque::BUTTON, "NEW"}, summonBtn{ed, Plaque::BUTTON, "SUMMON"};
    Summoner summoner;
    juce::String voices{"VOICES 0"};
    OutMeter meter{ed};
    std::vector<Bound *> bound;
    struct Chip { juce::Rectangle<int> r; int from, to; };
    std::vector<Chip> chips;

    Impl(SeanceEditor &e, UiNoteQueue &notes) : ed(e), P(e.patch), table(e), keys(notes)
    {
        setOpaque(true);
        pm = std::make_unique<ss::presets::PresetManager>(ed.clapHost);
        pm->onPresetLoaded = [this](const ss::presets::PresetManager::LoadedPreset &lp) {
            ed.dawState.main.recordPreset(lp.sessionKind(), lp.category, lp.sessionPath());
            applyRack();
            ed.uiDirty = true;
            ed.status("Loaded \"" + juce::String(P.name) + "\"");
        };
        // one dark look for everything JUCE draws itself (menus, dialogs, the FX rack reads its colours from here)
        lnf.setColour(juce::AlertWindow::backgroundColourId, col::PANEL);
        lnf.setColour(juce::AlertWindow::textColourId, col::BONE);
        lnf.setColour(juce::AlertWindow::outlineColourId, col::EDGE);
        lnf.setColour(juce::ComboBox::backgroundColourId, col::PANEL2);
        lnf.setColour(juce::ComboBox::textColourId, col::BONE);
        lnf.setColour(juce::ComboBox::outlineColourId, col::EDGE);
        lnf.setColour(juce::ComboBox::arrowColourId, col::DIM);
        lnf.setColour(juce::Slider::backgroundColourId, col::EDGE);
        lnf.setColour(juce::Slider::trackColourId, col::BLOOD.brighter(0.25f));
        lnf.setColour(juce::Slider::thumbColourId, col::CANDLE);
        lnf.setColour(juce::Slider::rotarySliderFillColourId, col::BLOOD.brighter(0.25f));
        lnf.setColour(juce::Slider::rotarySliderOutlineColourId, col::EDGE);
        lnf.setColour(juce::Label::textColourId, col::BONE);
        lnf.setColour(juce::PopupMenu::backgroundColourId, col::PANEL);
        lnf.setColour(juce::PopupMenu::textColourId, col::BONE);
        lnf.setColour(juce::PopupMenu::headerTextColourId, col::DIM);
        lnf.setColour(juce::PopupMenu::highlightedBackgroundColourId, col::BLOOD);
        lnf.setColour(juce::PopupMenu::highlightedTextColourId, col::BONE);
        lnf.setColour(juce::TextButton::buttonColourId, col::PANEL2);
        lnf.setColour(juce::TextButton::buttonOnColourId, col::BLOOD);
        lnf.setColour(juce::TextButton::textColourOffId, col::BONE);
        lnf.setColour(juce::TextButton::textColourOnId, col::BONE);
        lnf.setColour(juce::ToggleButton::textColourId, col::BONE);
        lnf.setColour(juce::ToggleButton::tickColourId, col::CANDLE);
        lnf.setColour(juce::ToggleButton::tickDisabledColourId, col::DIM);
        lnf.setColour(juce::TextEditor::backgroundColourId, col::SOOT);
        lnf.setColour(juce::TextEditor::textColourId, col::BONE);
        lnf.setColour(juce::TextEditor::outlineColourId, col::EDGE);
        lnf.setColour(juce::TooltipWindow::backgroundColourId, col::PANEL2);
        lnf.setColour(juce::TooltipWindow::textColourId, col::BONE);
        setLookAndFeel(&lnf);

        addAndMakeVisible(table);
        table.setBounds(8, 52, Table::S, Table::S);
        uiWorld = beast_world_create();
        hasBeast = uiWorld != nullptr && beast_type_count() >= ss::Patch::SourceNode::numBeastTypes;
        addAndMakeVisible(species);
        bound.push_back(&species);
        species.onChoice = [this](int v) { pickSpecies(table.sel, v, true); };
        for (int k = 0; k < 7; ++k)
        {
            BK[(size_t)k] = std::make_unique<GKnob>();
            auto &b = *BK[(size_t)k];
            b.lo = 0; b.hi = 1;
            b.get = [this, k] {
                auto &sn = P.sourceNodes[(size_t)table.sel];
                if (sn.beastSet.value < 0.5f) return (double)bodyDefault(table.sel, k); // untouched = the type's default
                return (double)sn.beastP[(size_t)k].value;
            };
            b.edit = [this, k](double v, int t) {
                auto &sn = P.sourceNodes[(size_t)table.sel];
                auto &pr = sn.beastP[(size_t)k];
                if (t == 0 && sn.beastSet.value < 0.5f)
                { // first touch: write the defaults the knobs were showing, then they are the patch's
                    for (int j = 0; j < 7; ++j) ed.setOnce(sn.beastP[(size_t)j], bodyDefault(table.sel, j));
                    ed.setOnce(sn.beastSet, 1.f);
                }
                if (isShade(table.sel) && shade_param_steps(k) > 1)
                { // a shade's picker knobs (TABLE, the types, UNISON) click between their positions
                    const float n = (float)(shade_param_steps(k) - 1);
                    v = std::round(v * n) / n;
                }
                if (t == 0) ed.beginEdit(pr);
                if (t == 1) ed.setValue(pr, (float)v);
                if (t == 2) ed.endEdit(pr);
            };
            b.text = [this, k](double v) {
                char buf[64]{};
                if (isShade(table.sel)) shade_param_display(k, (float)v, buf, sizeof(buf));
                else if (uiWorld) beast_slot_param_display(uiWorld, table.sel, k, (float)v, buf, sizeof(buf));
                return juce::String(buf);
            };
            addChildComponent(b);
            b.setBounds(PX + (k % 4) * 112 + (k >= 4 ? 56 : 0), 300 + (k / 4) * 106, 108, 100);
        }
        for (auto *c : std::initializer_list<juce::Component *>{&tgA, &tgB, &choice, &envOn, &envView, &cutBtn, &prevBtn, &nextBtn, &presetBtn, &saveBtn, &initBtn, &keys})
            addAndMakeVisible(*c);
        for (auto *b : std::initializer_list<Bound *>{&tgA, &tgB, &choice, &envOn}) bound.push_back(b);
        for (auto &k : K) { k = std::make_unique<Knob>(ed, ""); addAndMakeVisible(*k); bound.push_back(k.get()); }
        for (auto &k : EK) { k = std::make_unique<Knob>(ed, ""); addAndMakeVisible(*k); bound.push_back(k.get()); }
        for (auto &k : XK) { k = std::make_unique<Knob>(ed, ""); addAndMakeVisible(*k); bound.push_back(k.get()); }
        addChildComponent(throatBtn);
        throatBtn.setName("THROAT");
        throatBtn.onClick = [this] { throatMenu(); };
        static const char *tkn[3] = {"CUTOFF", "RESONANCE", "KEYTRACK"};
        for (int i = 0; i < 3; ++i) { TK[(size_t)i] = std::make_unique<Knob>(ed, tkn[i]); addChildComponent(*TK[(size_t)i]); bound.push_back(TK[(size_t)i].get()); }
        static const char *mkn[6] = {"LFO RATE", "LFO SHAPE", "LFO > CUTOFF", "ENV > CUTOFF", "LFO > FRAME", "ENV > FRAME"};
        for (int i = 0; i < 6; ++i) { MK[(size_t)i] = std::make_unique<Knob>(ed, mkn[i]); addChildComponent(*MK[(size_t)i]); bound.push_back(MK[(size_t)i].get()); }
        addChildComponent(moveTarget);
        bound.push_back(&moveTarget);
        moveTarget.onClick = [this] { ed.uiDirty = true; };
        moveTarget.onChoice = [this](int) { rebind(); ed.uiDirty = true; };
        moveTarget.nameFor = [this](int v) { return moveName(table.sel, v, true); };
        static const char *fkn[8] = {"VOLUME AMT", "FEEDBACK AMT", "PITCH AMT", "FOLLOW AMT", "VOLUME AMT 2", "FEEDBACK AMT 2", "PITCH AMT 2", "FOLLOW AMT 2"};
        static const char *fbn[8] = {"VOLUME FOLLOWS", "FEEDBACK FOLLOWS", "PITCH FOLLOWS", "DEPTH FOLLOWS", "VOLUME ALSO FOLLOWS", "FEEDBACK ALSO FOLLOWS", "PITCH ALSO FOLLOWS", "DEPTH ALSO FOLLOWS"};
        for (int i = 0; i < 8; ++i)
        {
            FK[(size_t)i] = std::make_unique<Knob>(ed, fkn[i]);
            addChildComponent(*FK[(size_t)i]);
            bound.push_back(FK[(size_t)i].get());
            followBtn[i].setName(fbn[i]);
            followBtn[i].onClick = [this, i] { followMenu(i); };
            addChildComponent(followBtn[i]);
        }
        for (int i = 0; i < 3; ++i)
        {
            // each row: what it follows + how much, then "and also" + how much
            followBtn[i].setBounds(PX, 318 + i * 76, 168, 28);
            FK[(size_t)i]->setBounds(PX + 170, 296 + i * 76, 62, 72);
            followBtn[i + 4].setBounds(PX + 238, 318 + i * 76, 146, 28);
            FK[(size_t)i + 4]->setBounds(PX + 386, 296 + i * 76, 62, 72);
        }
        followBtn[3].setBounds(PX + 196, 158, 184, 28);
        FK[3]->setBounds(PX + 384, 146, 64, 54);
        followBtn[7].setBounds(PX + 196, 214, 184, 28);
        FK[7]->setBounds(PX + 384, 202, 64, 54);
        for (int i = 0; i < 6; ++i)
        {
            MACK[(size_t)i] = std::make_unique<Knob>(ed, "MACRO " + juce::String(i + 1));
            addChildComponent(*MACK[(size_t)i]);
            bound.push_back(MACK[(size_t)i].get());
            MACK[(size_t)i]->setBounds(PX + 20 + (i % 3) * 144, 296 + (i / 3) * 128, 112, 98);
            macroNameBtn[i].setName("NAME MACRO " + juce::String(i + 1));
            macroNameBtn[i].setBounds(PX + 20 + (i % 3) * 144, 396 + (i / 3) * 128, 112, 24);
            macroNameBtn[i].onClick = [this, i] { askMacroName(i); };
            addChildComponent(macroNameBtn[i]);
        }
        macroBtn.setBounds(PX + 330, 62, 118, 28);
        macroBtn.onClick = [this] { macroView = !macroView; rebind(); };
        addChildComponent(macroBtn);
        static const char *tabs[7] = {"HEARD", "FEEDBACK", "PITCH", "BODY", "THROAT", "MOVE", "HANDS"};
        for (int i = 0; i < 7; ++i)
        {
            envTab[(size_t)i] = std::make_unique<Plaque>(ed, Plaque::BUTTON, tabs[i]);
            addAndMakeVisible(*envTab[(size_t)i]);
            envTab[(size_t)i]->onClick = [this, i] { envMode = i; rebind(); };
        }
        tgA.onFlip = [this](bool) { ed.uiDirty = true; };
        tgB.onFlip = [this](bool on) {
            if (mode == SPIRIT && on && P.mixerNodes[(size_t)table.sel].level.value < 0.05f) ed.setOnce(P.mixerNodes[(size_t)table.sel].level, 0.7f);
            ed.uiDirty = true;
        };
        envOn.onFlip = [this](bool) { ed.uiDirty = true; };
        cutBtn.colour = col::BLOOD;
        cutBtn.onClick = [this] {
            if (mode != THREAD) return;
            auto &m = P.matrixNodes[ss::MatrixIndex::positionForSourceTarget((size_t)table.selFrom, (size_t)table.selTo)];
            ed.setOnce(m.active, 0.f);
            ed.status("Thread " + juce::String(table.selFrom + 1) + " > " + juce::String(table.selTo + 1) + " cut.");
            ed.selectSpirit(table.selFrom);
        };
        prevBtn.onClick = [this] { stepPreset(-1); };
        nextBtn.onClick = [this] { stepPreset(1); };
        presetBtn.onClick = [this] { presetMenu(); };
        saveBtn.onClick = [this] { saveSound(); };
        initBtn.onClick = [this] { pm->loadInit(P, ed.mainToAudio); applyRack(); ed.status("A bare table: one spirit, nothing connected."); };

        // layout
        presetBtn.setBounds(300, 10, 250, 30);
        summonBtn.setBounds(732, 10, 90, 30);
        summonBtn.colour = col::CANDLE;
        meter.setBounds(W - 70, 30, 56, 12);
        meter.setVisible(ed.guard != nullptr);
        addChildComponent(meter);
        if (ed.guard) meter.setVisible(true);
        summonBtn.onClick = [this] { summonMenu(); };
        summoner.pickTable = [this](std::string &nm, std::string &js) { return pickVitalTable(nm, js); };
        addAndMakeVisible(summonBtn);
        prevBtn.setBounds(264, 10, 32, 30);
        nextBtn.setBounds(554, 10, 32, 30);
        saveBtn.setBounds(596, 10, 64, 30);
        initBtn.setBounds(664, 10, 64, 30);
        tgA.setBounds(PX, 112, 80, 30);
        tgB.setBounds(PX + 84, 112, 80, 30);
        species.setBounds(PX + 168, 112, 136, 30);
        choice.setBounds(PX + 308, 112, PW - 308, 30);
        addChildComponent(tableBtn);
        tableBtn.setName("TABLE FILE");
        tableBtn.setBounds(PX + 308, 112, PW - 308, 30);
        tableBtn.colour = col::CANDLE;
        tableBtn.onClick = [this] { tableMenu(); };
        for (int i = 0; i < 5; ++i) K[(size_t)i]->setBounds(PX + i * 90, 152, 88, 98);
        for (int i = 0; i < 7; ++i) envTab[(size_t)i]->setBounds(PX + i * 50, 262, 48, 26);
        for (int i = 0; i < 4; ++i) MK[(size_t)i]->setBounds(PX + i * 112, 296, 108, 100);
        moveTarget.setBounds(PX, 430, 200, 30);
        MK[4]->setBounds(PX + 224, 402, 108, 100);
        MK[5]->setBounds(PX + 336, 402, 108, 100);
        throatBtn.setBounds(PX, 300, PW, 34);
        for (int i = 0; i < 3; ++i) TK[(size_t)i]->setBounds(PX + i * 112, 350, 104, 112);
        envOn.setBounds(PX + PW - 96, 262, 96, 26);
        envView.setBounds(PX, 296, PW, 120);
        for (int i = 0; i < 7; ++i) EK[(size_t)i]->setBounds(PX + i * 64, 424, 64, 86);
        for (int i = 0; i < 5; ++i) XK[(size_t)i]->setBounds(PX + i * 90, 522, 88, 98);
        cutBtn.setBounds(PX, 540, 180, 30);
        keys.setBounds(PX, 690, PW, 62);
        keys.setKeyWidth((float)PW / 21.f);
        keys.setLowestVisibleKey(36);

        // the pages over the table
        for (int i = 0; i < 5; ++i)
        {
            addAndMakeVisible(pageBtn[i]);
            pageBtn[i].setBounds(830 + i * 56, 10, 53, 30);
            pageBtn[i].colour = col::BLOOD.brighter(0.3f);
            pageBtn[i].onClick = [this, i] { setPage(i); };
        }
        if (ed.spectral)
        {
            fxPage.panel = ghostblock::p2::spectral::makeSpectralPanel(*ed.spectral, {[this](uint32_t id, double v, int t) { ed.spectralEdit(id, v, t); }, [this] { ed.markStateDirty(); },
                                                                                 [this](const juce::String &st) { ed.status(st); }});
            fxPage.addAndMakeVisible(*fxPage.panel);
            fxPage.panel->setBounds(88, 72, 1004, 444);
            addChildComponent(fxPage);
            fxPage.setBounds(0, 48, W, 638);
        }
        if (ed.sub)
        {
            subPage = std::make_unique<SubPage>(ed, *ed.sub);
            addChildComponent(*subPage);
            subPage->setBounds(0, 48, W, 638);
        }
        if (ed.morph)
        {
            blendPage = std::make_unique<BlendPage>(ed, *ed.morph, ed.clapHost);
            addChildComponent(*blendPage);
            blendPage->setBounds(0, 48, W, 638);
        }
        if (ed.steps)
        {
            stepsPage = std::make_unique<StepsPage>(ed, *ed.steps);
            addChildComponent(*stepsPage);
            stepsPage->setBounds(0, 48, W, 638);
        }
        pageBtn[4].setEnabled(ed.steps != nullptr);
        pageBtn[3].setEnabled(ed.morph != nullptr);
        pageBtn[1].setEnabled(ed.spectral != nullptr);
        pageBtn[2].setEnabled(ed.sub != nullptr);
        rebind();
        setPage(0);
    }
    ~Impl() override
    {
        setLookAndFeel(nullptr);
        if (uiWorld) beast_world_destroy(uiWorld);
    }
    // ---- a shade's own table: the Vital wavetable files on this computer (Music/Vital), read where they are
    static juce::File vitalFolder() { return juce::File::getSpecialLocation(juce::File::userMusicDirectory).getChildFile("Vital"); }
    bool loadShadeTable(int seat, const juce::String &name, const std::string &text)
    {
        P.setShadeTable((size_t)seat, name.toStdString(), text);
        ss::reconcileShadeTables(P, ed.handoff.shadeWorld);
        const bool ok = text.empty() || shade_slot_user_frames(ed.handoff.shadeWorld, seat) > 0;
        if (!ok)
        {
            P.setShadeTable((size_t)seat, "", "");
            ss::reconcileShadeTables(P, ed.handoff.shadeWorld);
        }
        ed.markStateDirty();
        ed.uiDirty = true;
        return ok;
    }
    bool loadWavBytes(int seat, const juce::String &name, const void *data, size_t size)
    {
        int frames = 0;
        char *text = shade_wav_to_vitaltable(data, size, name.toRawUTF8(), &frames);
        const bool ok = text != nullptr && loadShadeTable(seat, name, std::string(text));
        shade_text_free(text);
        if (ok)
            ed.status("Spirit " + juce::String(seat + 1) + " now plays \"" + name + "\" (" + juce::String(frames) + (frames == 1 ? " cycle" : " cycles") + "). FRAME moves through it.");
        else
            ed.status("\"" + name + "\" is not a wav this can read as a wavetable (it needs to be a plain PCM or float .wav).");
        return ok;
    }
    void loadWavTable(int seat, const juce::File &f)
    {
        juce::MemoryBlock mb;
        if (f.getSize() > (64 << 20) || !f.loadFileAsData(mb)) { ed.status("Couldn't read " + f.getFileName() + "."); return; }
        loadWavBytes(seat, f.getFileNameWithoutExtension(), mb.getData(), mb.getSize());
    }
    void tableMenu()
    {
        const int o = table.sel;
        juce::PopupMenu m;
        const bool own = !P.shadeTables[(size_t)o].json.empty();
        m.addItem("Built-in tables (the TABLE knob)", true, !own, [this, o] {
            loadShadeTable(o, "", "");
            ed.status("Spirit " + juce::String(o + 1) + " is back on the built-in tables: pick one with the TABLE knob.");
        });
        m.addItem("Load a .wav wavetable...", [this, o] {
            chooser = std::make_unique<juce::FileChooser>("Pick a .wav wavetable (one cycle after another, 2048 samples each)", juce::File::getSpecialLocation(juce::File::userMusicDirectory), "*.wav");
            chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, o](const juce::FileChooser &fc) {
                const auto f = fc.getResult();
                if (f.existsAsFile()) loadWavTable(o, f);
            });
        });
        const auto root = vitalFolder();
        juce::Array<juce::File> files;
        if (root.isDirectory()) root.findChildFiles(files, juce::File::findFiles, true, "*.vitaltable");
        if (files.isEmpty())
            m.addItem("No Vital wavetables found in Music/Vital", false, false, [] {});
        else
        {
            m.addSeparator();
            struct Entry { juce::String pack, label; juce::File f; };
            std::vector<Entry> es;
            for (auto &f : files)
            {
                auto rel = f.getRelativePathFrom(root).replaceCharacter('\\', '/');
                auto pack = rel.upToFirstOccurrenceOf("/", false, false);
                if (!rel.containsChar('/')) pack = "Loose files";
                auto parent = f.getParentDirectory().getFileName();
                es.push_back({pack, (parent == "Wavetables" || parent == pack ? juce::String() : parent + " / ") + f.getFileNameWithoutExtension(), f});
            }
            std::sort(es.begin(), es.end(), [](const Entry &a, const Entry &b) {
                const int c = a.pack.compareIgnoreCase(b.pack);
                return c != 0 ? c < 0 : a.label.compareIgnoreCase(b.label) < 0;
            });
            // Factory first, then the packs by name
            std::stable_sort(es.begin(), es.end(), [](const Entry &a, const Entry &b) { return (a.pack == "Factory") > (b.pack == "Factory"); });
            juce::PopupMenu sub;
            juce::String cur;
            int count = 0;
            auto flush = [&] { if (cur.isNotEmpty()) m.addSubMenu(cur + "  (" + juce::String(count) + ")", sub); sub = juce::PopupMenu(); count = 0; };
            for (auto &e : es)
            {
                if (e.pack != cur) { flush(); cur = e.pack; }
                const auto f = e.f;
                const auto nm = f.getFileNameWithoutExtension();
                sub.addItem(e.label, true, own && juce::String(P.shadeTables[(size_t)o].name) == nm, [this, o, f, nm] {
                    const auto text = f.loadFileAsString().toStdString();
                    if (loadShadeTable(o, nm, text))
                        ed.status("Spirit " + juce::String(o + 1) + " now plays \"" + nm + "\" (" + juce::String(shade_slot_user_frames(ed.handoff.shadeWorld, o)) + " frames). FRAME moves through it.");
                    else
                        ed.status("\"" + nm + "\" could not be read as a wavetable; spirit " + juce::String(o + 1) + " keeps its built-in table.");
                });
                ++count;
            }
            flush();
        }
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&tableBtn));
    }

    // every Surge filter, grouped by its model
    void throatMenu()
    {
        const auto &cat = throatCatalog();
        const int o = table.sel, cur = (int)std::lround(P.throatNodes[(size_t)o].kind.value);
        juce::PopupMenu m;
        m.addItem(1, "Off", true, cur == 0);
        juce::PopupMenu sub;
        std::string grp;
        bool has = false;
        for (int k = 1; k < (int)cat.size(); ++k)
        {
            if (cat[(size_t)k].group != grp)
            {
                if (!grp.empty()) m.addSubMenu(juce::String(grp), sub, true, nullptr, has);
                sub = juce::PopupMenu();
                grp = cat[(size_t)k].group;
                has = false;
            }
            sub.addItem(k + 1, cat[(size_t)k].name.empty() ? juce::String(grp) : juce::String(cat[(size_t)k].name), true, k == cur);
            has |= (k == cur);
        }
        if (!grp.empty()) m.addSubMenu(juce::String(grp), sub, true, nullptr, has);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&throatBtn), [this, o](int r) {
            if (r <= 0) return;
            ed.setOnce(P.throatNodes[(size_t)o].kind, (float)(r - 1));
            ed.status(r == 1 ? "Spirit " + juce::String(o + 1) + " sings with an open throat again." : "Spirit " + juce::String(o + 1) + " now sings through " + juce::String(throatCatalog()[(size_t)(r - 1)].group) + ".");
        });
    }

    // a seat takes a species: 0 = the sine operator, 1.. = a beast. A newly picked beast starts from its own defaults.
    bool isShade(int seat) const { return (int)std::round(P.sourceNodes[(size_t)seat].species.value) == ss::Patch::SourceNode::shadeSpecies; }
    float bodyDefault(int seat, int k) const
    {
        if (isShade(seat)) return shade_param_default01(k);
        return uiWorld ? beast_slot_param_default01(uiWorld, seat, k) : 0.f;
    }
    void pickSpecies(int seat, int v, bool setDefaults)
    {
        auto &s = P.sourceNodes[(size_t)seat];
        if (std::abs(s.species.value - (float)v) > 0.01f) ed.setOnce(s.species, (float)v);
        if (v == ss::Patch::SourceNode::shadeSpecies)
        {
            if (setDefaults)
            {
                for (int k = 0; k < 7; ++k) ed.setOnce(s.beastP[(size_t)k], shade_param_default01(k));
                ed.setOnce(s.beastSet, 1.f);
            }
            envMode = 3;
            ed.status("Spirit " + juce::String(seat + 1) + " is now a SHADE: a wavetable voice. Its table, morphs, warps and unison are under BODY.");
        }
        else if (v > 0 && uiWorld)
        {
            beast_slot_set_type(uiWorld, seat, v - 1);
            if (setDefaults)
            {
                for (int k = 0; k < 7; ++k) ed.setOnce(s.beastP[(size_t)k], beast_slot_param_default01(uiWorld, seat, k));
                ed.setOnce(s.beastSet, 1.f);
            }
            envMode = 3;
            ed.status("Spirit " + juce::String(seat + 1) + " is now a " + juce::String(ss::Patch::SourceNode::speciesName(v)).toUpperCase() + " beast. Its own knobs are under BODY.");
        }
        else if (envMode == 3) envMode = 0;
        rebind();
    }
    void setPage(int pg)
    {
        if ((pg == 1 && !ed.spectral) || (pg == 2 && !subPage) || (pg == 3 && !blendPage) || (pg == 4 && !stepsPage) || pg < 0 || pg > 4) pg = 0;
        page = pg;
        for (int i = 0; i < 5; ++i) { pageBtn[i].lit = (i == pg); pageBtn[i].repaint(); }
        if (stepsPage) { stepsPage->setVisible(pg == 4); if (pg == 4) stepsPage->pull(); }
        if (blendPage) { blendPage->setVisible(pg == 3); if (pg == 3) blendPage->refresh(); }
        fxPage.setVisible(pg == 1);
        if (subPage) { subPage->setVisible(pg == 2); if (pg == 2) subPage->refresh(); }
        table.setVisible(pg == 0); // (not drawn under a page)
    }

    // ---- presets
    int factoryIndex()
    {
        const auto &v = pm->factoryPatchVector;
        const auto nm = std::string(P.name);
        for (int i = 0; i < (int)v.size(); ++i)
            if (ss::presets::PresetManager::stripPresetExtension(v[(size_t)i].second) == nm) return i;
        return -1;
    }
    void loadFactory(int i)
    {
        const auto &v = pm->factoryPatchVector;
        if (v.empty()) return;
        i = (i % (int)v.size() + (int)v.size()) % (int)v.size();
        currentKey = "F\t" + v[(size_t)i].first + "\t" + v[(size_t)i].second;
        pm->loadFactoryPreset(P, ed.mainToAudio, v[(size_t)i].first, v[(size_t)i].second);
    }
    void stepPreset(int d)
    {
        const int i = factoryIndex();
        loadFactory(i < 0 ? (d > 0 ? 0 : -1) : i + d);
    }
    // ---- the sound list: every sound there is, favourites, search
    struct SoundEntry
    {
        bool factory{true};
        std::string cat, file; // factory: its folder and file
        fs::path rel;          // yours: its path under YOUR SOUNDS
        juce::String name;
        std::string key() const { return factory ? "F\t" + cat + "\t" + file : "U\t" + rel.generic_u8string(); }
    };
    std::string currentKey; // the sound on the table, if it came from the list ("" = made here and not saved)
    std::vector<SoundEntry> allSounds()
    {
        std::vector<SoundEntry> v;
        for (auto &[cat, names] : pm->factoryPatchNames)
            for (auto &n : names) v.push_back({true, cat, n, {}, juce::String(ss::presets::PresetManager::stripPresetExtension(n))});
        pm->rescanUserPresets();
        for (auto &up : pm->userPatches) v.push_back({false, "YOUR SOUNDS", "", up, juce::String(ss::presets::PresetManager::stripPresetExtension(up.filename().u8string()))});
        return v;
    }
    void loadEntry(const SoundEntry &e)
    {
        currentKey = e.key();
        if (e.factory) pm->loadFactoryPreset(P, ed.mainToAudio, e.cat, e.file);
        else pm->loadUserPresetDirect(P, ed.mainToAudio, pm->userPatchesPath / e.rel);
    }
    juce::File favFile() const { return juce::File(juce::String(pm->userPatchesPath.parent_path().u8string())).getChildFile("favourites.txt"); }
    juce::StringArray favKeys() const
    {
        juce::StringArray a;
        a.addLines(favFile().loadFileAsString());
        a.removeEmptyStrings();
        return a;
    }
    bool isFavourite(const std::string &key) const { return !key.empty() && favKeys().contains(juce::String(key)); }
    bool toggleFavourite(const std::string &key)
    {
        if (key.empty()) return false;
        auto a = favKeys();
        const bool was = a.contains(juce::String(key));
        if (was) a.removeString(juce::String(key)); else a.add(juce::String(key));
        favFile().getParentDirectory().createDirectory();
        favFile().replaceWithText(a.joinIntoString("\n") + "\n");
        ed.status(was ? "Taken out of your favourites." : "Added to your favourites (top of the sound list).");
        return !was;
    }
    std::vector<SoundEntry> searchSounds(const juce::String &text)
    {
        std::vector<SoundEntry> hits;
        const auto t = text.trim();
        if (t.isEmpty()) return hits;
        for (auto &e : allSounds())
            if (e.name.containsIgnoreCase(t) || juce::String(e.cat).containsIgnoreCase(t)) hits.push_back(e);
        return hits;
    }
    void showSearch(const juce::String &text)
    {
        const auto hits = searchSounds(text);
        if (hits.empty()) { ed.status("No sound with \"" + text.trim() + "\" in its name or folder."); return; }
        juce::PopupMenu m;
        m.addSectionHeader(juce::String((int)hits.size()) + " found for \"" + text.trim() + "\"");
        for (auto &e : hits) m.addItem(e.name + "    (" + juce::String(e.cat) + ")", [this, e] { loadEntry(e); });
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presetBtn).withMaximumNumColumns(2));
    }
    void askSearch()
    {
        auto *w = new juce::AlertWindow("Find a sound", "Part of a name or of a folder (bass, bell, choir, twelve ...)", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor("q", "");
        w->addButton("Find", 1, juce::KeyPress(juce::KeyPress::returnKey));
        w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        w->enterModalState(true, juce::ModalCallbackFunction::create([sp = juce::Component::SafePointer<Impl>(this), w](int r) {
            if (r == 1 && sp) sp->showSearch(w->getTextEditorContents("q"));
        }), true);
    }
    void presetMenu()
    {
        juce::PopupMenu m;
        const auto all = allSounds();
        const auto favs = favKeys();
        m.addItem("FIND A SOUND...", [this] { askSearch(); });
        juce::PopupMenu fav;
        for (auto &e : all)
            if (favs.contains(juce::String(e.key()))) fav.addItem(e.name + "    (" + juce::String(e.cat) + ")", true, e.key() == currentKey, [this, e] { loadEntry(e); });
        if (fav.getNumItems() == 0) fav.addItem("None yet: load a sound, then tick the line below", false, false, [] {});
        m.addSubMenu("FAVOURITES", fav);
        const bool listed = !currentKey.empty() && !P.dirty;
        m.addItem(isFavourite(currentKey) ? "Take \"" + juce::String(P.name) + "\" out of favourites" : "Add \"" + juce::String(P.name) + "\" to favourites" + (listed || !currentKey.empty() ? "" : "  (SAVE it first)"),
                  !currentKey.empty(), isFavourite(currentKey), [this] { toggleFavourite(currentKey); });
        m.addSeparator();
        std::string lastCat;
        juce::PopupMenu sub;
        auto flush = [&] { if (!lastCat.empty()) m.addSubMenu(juce::String(lastCat), sub); sub = juce::PopupMenu(); };
        for (auto &e : all)
        {
            if (!e.factory) continue;
            if (e.cat != lastCat) { flush(); lastCat = e.cat; }
            sub.addItem(e.name, true, e.key() == currentKey, [this, e] { loadEntry(e); });
        }
        flush();
        m.addSeparator();
        juce::PopupMenu user;
        for (auto &e : all)
            if (!e.factory) user.addItem(e.name, true, e.key() == currentKey, [this, e] { loadEntry(e); });
        if (user.getNumItems() == 0) user.addItem("No saved sounds yet (use SAVE)", false, false, [] {});
        m.addSubMenu("YOUR SOUNDS", user);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presetBtn));
    }
    // A sound opens the way it was saved: its FX and SUB pages too (a sound saved without them: both off)
    // what MOVE's second pair of knobs pushes on this seat. offered: "" when that choice does nothing here
    juce::String moveName(int o, int v, bool offered)
    {
        static const char *tgn[3] = {"FRAME", "MORPH", "WARP"};
        const int sp = (int)std::lround(P.sourceNodes[(size_t)o].species.value);
        if (sp == ss::Patch::SourceNode::shadeSpecies) return (offered && v > 2) ? juce::String() : juce::String(tgn[juce::jlimit(0, 2, v)]);
        if (sp > 0 && uiWorld)
        {
            v = juce::jlimit(0, 6, v);
            if (beast_slot_type(uiWorld, o) != sp - 1) beast_slot_set_type(uiWorld, o, sp - 1);
            const juce::String nm = juce::String(beast_slot_param_name(uiWorld, o, v)).toUpperCase();
            if (nm.isNotEmpty() && beast_slot_param_movable(uiWorld, o, v)) return nm;
            return offered ? juce::String() : juce::String("KNOB ") + juce::String(v + 1);
        }
        return (offered && v > 2) ? juce::String() : juce::String(tgn[juce::jlimit(0, 2, v)]);
    }
    // SUMMON: a new sound made by rule in the chosen character, played and level-trimmed before you hear it
    void summonMenu()
    {
        juce::PopupMenu m;
        m.addSectionHeader("Summon a new sound");
        for (int i = 0; i < Summoner::kStyles; ++i) m.addItem(i + 1, Summoner::styleName(i));
        m.addSectionHeader("Or change the sound on the table");
        m.addItem(101, "Mutate it a little");
        m.addItem(102, "Mutate it a lot");
        juce::PopupMenu only;
        only.addItem(111, "The thread depths");
        only.addItem(112, "The tuning (ratios)");
        only.addItem(113, "The wavetables");
        only.addItem(114, "The filters");
        only.addItem(115, "Envelopes, pans and feedback");
        m.addSubMenu("Change only...", only);
        m.addItem(104, "New voices (same wiring, tuning and envelopes)");
        juce::PopupMenu around;
        static const char *kn[3] = {"Keep the wiring (threads, feedback, tuning)", "Keep the voices (wavetables, oscillators, filters)", "Keep the envelopes and pans"};
        for (int k = 0; k < 3; ++k)
        {
            juce::PopupMenu st;
            for (int i = 0; i < Summoner::kStyles; ++i) st.addItem(200 + k * 10 + i, Summoner::styleName(i));
            around.addSubMenu(kn[k], st);
        }
        m.addSubMenu("Summon around this sound...", around);
        m.addSeparator();
        m.addItem(103, "Bring back the sound before that", !undoState.empty());
        m.addSeparator();
        scanVital();
        m.addItem(120, vitalFiles.isEmpty() ? juce::String("Use my Vital wavetables (none found in Music/Vital)") : "Use my Vital wavetables (" + juce::String(vitalFiles.size()) + " found)", !vitalFiles.isEmpty(),
                  useVital && !vitalFiles.isEmpty());
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&summonBtn), [sp = juce::Component::SafePointer<Impl>(this)](int r) {
            if (!sp || r <= 0) return;
            if (r == 101) sp->mutate(0.35f);
            else if (r == 102) sp->mutate(1.f);
            else if (r == 103) sp->bringBack();
            else if (r == 104) sp->mutate(1.f, -1);
            else if (r >= 200 && r < 230) sp->summonAround((r - 200) % 10, 1 << ((r - 200) / 10));
            else if (r >= 111 && r <= 115) sp->mutate(0.8f, 1 << (r - 111));
            else if (r == 120) { sp->useVital = !sp->useVital; sp->ed.status(sp->useVital ? "New sounds may use your Vital wavetables." : "New sounds use the built-in tables only."); }
            else sp->summon(r - 1);
        });
    }
    std::string undoState; // the sound as it was before the last SUMMON / MUTATE (with its FX and SUB pages)
    std::string snapshot()
    {
        P.rackSpectral = ed.spectral ? ed.spectral->toState() : std::string();
        P.rackSub = ed.sub ? ed.sub->toState() : std::string();
        P.rackSteps = ed.steps ? ed.steps->toState() : std::string();
        return P.toState();
    }
    void sendWhole() { ss::Synth::sendEntirePatchToAudio(P, ed.mainToAudio, ed.clapHost ? ed.clapHost : ghostblock::p2::Morph::quietHost()); }
    // the owner's Vital table files, for SUMMON / MUTATE to hand to wavetable spirits (read from disk, never bundled)
    juce::Array<juce::File> vitalFiles;
    bool vitalScanned{false}, useVital{true};
    void scanVital()
    {
        if (vitalScanned) return;
        vitalScanned = true;
        const auto root = vitalFolder();
        if (root.isDirectory()) root.findChildFiles(vitalFiles, juce::File::findFiles, true, "*.vitaltable");
    }
    bool pickVitalTable(std::string &name, std::string &text)
    {
        scanVital();
        if (!useVital || vitalFiles.isEmpty()) return false;
        const auto f = vitalFiles[juce::Random::getSystemRandom().nextInt(vitalFiles.size())];
        if (!f.existsAsFile() || f.getSize() > (8 << 20)) return false;
        text = f.loadFileAsString().toStdString();
        name = f.getFileNameWithoutExtension().toStdString();
        return !text.empty();
    }
    // what: Summoner::What bits, or -1 = new voices
    bool mutate(float amount, int what = Summoner::ALL)
    {
        const auto before = snapshot();
        const bool ok = what < 0 ? summoner.revoice(P) : summoner.mutate(P, amount, what);
        if (ok) undoState = before;
        else P.fromState(before);
        sendWhole();
        P.dirty = true;
        rebind();
        ed.uiDirty = true;
        ss::reconcileShadeTables(P, ed.handoff.shadeWorld); // (a table file may have come or gone)
        static const char *wn[5] = {"The thread depths changed.", "The tuning changed.", "The wavetables changed.", "The filters changed.", "Envelopes, pans and feedback changed."};
        juce::String did = what < 0 ? "New voices." : what == Summoner::ALL ? (amount > 0.7f ? "Mutated a lot." : "Mutated a little.") : "";
        for (int b = 0; b < 5 && did.isEmpty(); ++b) if (what == (1 << b)) did = wn[b];
        ed.status(ok ? did + " SUMMON > Bring back... undoes it. Not kept until you SAVE."
                     : juce::String("That mutation fell silent, so the sound was left as it was. Try again."));
        return ok;
    }
    bool bringBack()
    {
        if (undoState.empty()) return false;
        const auto now = snapshot();
        P.fromState(undoState);
        undoState = now; // (so the same item swaps back again)
        sendWhole();
        ss::reconcileShadeTables(P, ed.handoff.shadeWorld);
        applyRack();
        P.dirty = true;
        rebind();
        ed.uiDirty = true;
        ed.status("Back to \"" + juce::String(P.name) + "\" as it was. (The same item swaps again.)");
        return true;
    }
    juce::String summonAround(int style, int keep)
    {
        const auto before = snapshot();
        const auto nm = summoner.summonKeeping(P, style, keep);
        if (!nm.empty()) { undoState = before; currentKey.clear(); }
        sendWhole();
        ss::reconcileShadeTables(P, ed.handoff.shadeWorld);
        P.dirty = true;
        rebind();
        ed.uiDirty = true;
        ed.status(nm.empty() ? juce::String("Nothing that plays came out of that; the sound was left as it was. Try again.")
                             : "Summoned \"" + juce::String(nm) + "\" around the last sound. SUMMON > Bring back... undoes it. Not kept until you SAVE.");
        return juce::String(nm);
    }
    juce::String summon(int style)
    {
        currentKey.clear();
        undoState = snapshot();
        const auto nm = summoner.summon(P, style);
        sendWhole();
        ss::reconcileShadeTables(P, ed.handoff.shadeWorld);
        applyRack();
        P.dirty = !nm.empty();
        table.sel = 0;
        for (int o = 0; o < 12; ++o)
            if (P.mixerNodes[(size_t)o].active.value > 0.5f) { table.sel = o; break; }
        ed.selectCircle();
        ed.uiDirty = true;
        ed.status(nm.empty() ? juce::String("Nothing answered. Try again.") : "Summoned \"" + juce::String(nm) + "\". It is not kept until you SAVE it.");
        return juce::String(nm);
    }
    // what a FOLLOWS control can follow: the engine's own modulation sources, the ones a player can reach
    static const std::vector<std::pair<int, const char *>> &followSources()
    {
        static const std::vector<std::pair<int, const char *>> v = {{0, "NOTHING"}, {5000, "VELOCITY (how hard you play)"}, {201, "MOD WHEEL"}, {100, "AFTERTOUCH"}, {101, "PITCH BEND"},
                                                                    {5061, "KEY (higher notes, more)"}, {5201, "RANDOM (new for every note)"}, {400, "MACRO 1"}, {401, "MACRO 2"},
                                                                    {402, "MACRO 3"}, {403, "MACRO 4"}, {404, "MACRO 5"}, {405, "MACRO 6"}};
        return v;
    }
    // a macro's own name (the patch keeps one per macro; "Macro 3" until the owner names it)
    juce::String macroLabel(int i) const
    {
        const juce::String n = juce::String(juce::CharPointer_UTF8(P.macroNames[(size_t)i].data())).trim();
        return n.isEmpty() ? "MACRO " + juce::String(i + 1) : n.toUpperCase();
    }
    void setMacroName(int i, juce::String n)
    {
        n = n.trim().substring(0, 24);
        if (n.isEmpty()) n = "Macro " + juce::String(i + 1);
        auto &dst = P.macroNames[(size_t)i];
        std::memset(dst.data(), 0, dst.size());
        n.copyToUTF8(dst.data(), dst.size() - 1);
        P.dirty = true;
        ed.markStateDirty();
        rebind();
        ed.uiDirty = true;
    }
    void askMacroName(int i)
    {
        auto *w = new juce::AlertWindow("Name macro " + juce::String(i + 1), "What does this knob do in this sound? (Empty = back to \"Macro " + juce::String(i + 1) + "\")", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor("n", juce::String(juce::CharPointer_UTF8(P.macroNames[(size_t)i].data())));
        w->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
        w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        w->enterModalState(true, juce::ModalCallbackFunction::create([sp = juce::Component::SafePointer<Impl>(this), w, i](int r) {
            if (r == 1 && sp) sp->setMacroName(i, w->getTextEditorContents("n"));
        }), true);
    }
    juce::String followName(int id) const
    {
        if (id >= 400 && id <= 405) return macroLabel(id - 400);
        for (auto &[i, n] : followSources())
            if (i == id) return juce::String(n).upToFirstOccurrenceOf(" (", false, false);
        return "SOMETHING ELSE"; // (a sound made elsewhere can use a source this menu does not list)
    }
    void setFollow(int i, int id)
    {
        if (!fSrc[i]) return;
        ed.setOnce(*fSrc[i], (float)id);
        ed.setOnce(*fTgt[i], id ? (float)fDirect[i] : 0.f);
        if (id && std::abs(fDep[i]->value) < 1e-4f) ed.setOnce(*fDep[i], 0.5f); // so the pick does something at once
        rebind();
    }
    void followMenu(int i)
    {
        if (!fSrc[i]) return;
        juce::PopupMenu m;
        const int cur = (int)std::lround(fSrc[i]->value);
        int n = 0;
        for (auto &[id, name] : followSources())
        {
            if (id == 400) m.addSeparator();
            m.addItem(++n, id >= 400 && id <= 405 && macroLabel(id - 400) != juce::String(name) ? juce::String(name) + "  -  " + macroLabel(id - 400) : juce::String(name), true, id == cur);
        }
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&followBtn[i]), [sp = juce::Component::SafePointer<Impl>(this), i](int r) {
            if (r > 0 && sp) sp->setFollow(i, followSources()[(size_t)(r - 1)].first);
        });
    }
    void applyRack()
    {
        if (ed.spectral)
        {
            if (P.rackSpectral.empty() || !ed.spectral->fromState(P.rackSpectral)) ed.spectral->resetToDefaults();
            if (ed.clapHost) ed.clapHost->request_callback(ed.clapHost); // the plugin builds what changed and tells the host
            else ed.spectral->serviceMainThread();
        }
        if (ed.sub)
        {
            if (P.rackSub.empty() || !ed.sub->fromState(P.rackSub)) ed.sub->resetToDefaults();
            if (subPage) subPage->refresh();
        }
        // STEPS: a sound saved with its sequencer brings it back (on or off, as saved); a sound with none leaves yours running
        if (ed.steps && !P.rackSteps.empty() && ed.steps->fromState(P.rackSteps) && stepsPage) stepsPage->pull();
        ed.requestFlush();
        ed.markStateDirty();
    }
    void writeSound(const fs::path &path)
    {
        P.rackSpectral = ed.spectral ? ed.spectral->toState() : std::string();
        P.rackSub = ed.sub ? ed.sub->toState() : std::string();
        P.rackSteps = ed.steps ? ed.steps->toState() : std::string();
#if USE_WCHAR_PRESET
        pm->saveUserPresetDirect(P, path.c_str());
#else
        pm->saveUserPresetDirect(P, path);
#endif
    }
    void saveSound()
    {
        auto *w = new juce::AlertWindow("Save this sound", "Name it:", juce::MessageBoxIconType::NoIcon);
        w->addTextEditor("name", juce::String(P.name));
        w->addButton("SAVE", 1, juce::KeyPress(juce::KeyPress::returnKey));
        w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        w->enterModalState(true, juce::ModalCallbackFunction::create([this, w](int r) {
            if (r != 1) return;
            auto n = juce::File::createLegalFileName(w->getTextEditorContents("name").trim());
            if (n.isEmpty()) return;
            std::error_code ec;
            fs::create_directories(pm->userPatchesPath, ec);
            const auto path = pm->userPatchesPath / (n.toStdString() + ss::presets::PresetManager::presetExtension);
            strncpy(P.name, n.toRawUTF8(), sizeof(P.name) - 1);
            P.name[sizeof(P.name) - 1] = 0;
            writeSound(path);
            currentKey = "U\t" + fs::path(n.toStdString() + ss::presets::PresetManager::presetExtension).generic_u8string();
            P.dirty = false;
            const auto lp = pm->identifyUserPath(path);
            ed.dawState.main.recordPreset(lp.sessionKind(), lp.category, lp.sessionPath());
            ed.status("Saved \"" + n + "\". Find it under YOUR SOUNDS.");
            ed.uiDirty = true;
        }), true);
    }

    // ---- the panel
    ss::Patch::DAHDSRMixin *curEnv{nullptr};
    void bindEnv(ss::Patch::DAHDSRMixin &e, juce::Colour c, Param *amt, const juce::String &amtName)
    {
        curEnv = &e;
        envView.env = &e;
        envView.colour = c;
        envOn.bind(&e.envPower, "ENVELOPE ON");
        Param *ps[6] = {&e.delay, &e.attack, &e.hold, &e.decay, &e.sustain, &e.release};
        static const char *nm[6] = {"DELAY", "ATTACK", "HOLD", "DECAY", "SUSTAIN", "RELEASE"};
        for (int i = 0; i < 6; ++i) { EK[(size_t)i]->bind(ps[i], nm[i]); EK[(size_t)i]->colour = c; }
        EK[6]->bind(amt, amtName);
        EK[6]->colour = c;
        EK[6]->setVisible(amt != nullptr);
    }
    void rebind()
    {
        for (auto &k : K) { k->onTouch = nullptr; k->setVisible(true); }
        for (auto &k : XK) k->setVisible(mode == CIRCLE);
        for (auto &t : envTab) t->setVisible(mode == SPIRIT);
        for (auto &k : MK) k->setVisible(mode == SPIRIT && envMode == 5);
        moveTarget.setVisible(mode == SPIRIT && envMode == 5);
        for (int i : {0, 1, 2, 4, 5, 6}) { followBtn[i].setVisible(mode == SPIRIT && envMode == 6); FK[(size_t)i]->setVisible(mode == SPIRIT && envMode == 6); }
        for (int i : {3, 7}) { followBtn[i].setVisible(mode == THREAD); FK[(size_t)i]->setVisible(mode == THREAD); }
        macroBtn.setVisible(mode == CIRCLE);
        macroBtn.lit = macroView;
        for (auto &k : MACK) k->setVisible(mode == CIRCLE && macroView);
        for (auto &b : macroNameBtn) b.setVisible(mode == CIRCLE && macroView);
        throatBtn.setVisible(mode == SPIRIT && envMode == 4);
        for (auto &k : TK) k->setVisible(mode == SPIRIT && envMode == 4);
        species.setVisible(mode == SPIRIT);
        tableBtn.setVisible(mode == SPIRIT && isShade(table.sel));
        if (mode == SPIRIT) choice.setVisible(!isShade(table.sel)); // WAVE belongs to the sine operator; a shade has TABLE FILE there
        for (auto &b : BK) b->setVisible(false);
        envOn.setVisible(true);
        envView.setVisible(true);
        for (auto &k : EK) k->setVisible(true);
        tgA.setVisible(mode != CIRCLE);
        tgB.setVisible(mode != CIRCLE);
        cutBtn.setVisible(mode == THREAD);
        if (mode == SPIRIT)
        {
            const int o = table.sel;
            const auto c = seatCol(o);
            auto &s = P.sourceNodes[(size_t)o];
            auto &mx = P.mixerNodes[(size_t)o];
            auto &sf = P.selfNodes[(size_t)o];
            tgA.bind(&s.active, "AWAKE");
            tgB.bind(&mx.active, "HEARD");
            tgA.colour = tgB.colour = c;
            choice.bind(&s.waveForm, "WAVE");
            species.bind(&s.species, "SPECIES");
            const bool shadeSeat = isShade(o);
            const int sp = shadeSeat ? ss::Patch::SourceNode::shadeSpecies : (hasBeast ? (int)std::round(s.species.value) : 0);
            if (sp == 0 && envMode == 3) envMode = 0;
            if (shadeSeat)
            {
                for (int k = 0; k < 7; ++k)
                {
                    BK[(size_t)k]->label = shade_param_name(k);
                    BK[(size_t)k]->setName(shade_param_name(k));
                    BK[(size_t)k]->colour = c;
                    BK[(size_t)k]->def = shade_param_default01(k);
                    BK[(size_t)k]->setVisible(envMode == 3);
                }
            }
            else if (sp > 0 && uiWorld)
            {
                if (beast_slot_type(uiWorld, o) != sp - 1) beast_slot_set_type(uiWorld, o, sp - 1);
                for (int k = 0; k < 7; ++k)
                {
                    const juce::String nm(beast_slot_param_name(uiWorld, o, k));
                    BK[(size_t)k]->label = nm.toUpperCase();
                    BK[(size_t)k]->setName(nm.toUpperCase());
                    BK[(size_t)k]->colour = c;
                    BK[(size_t)k]->def = beast_slot_param_default01(uiWorld, o, k);
                    BK[(size_t)k]->setVisible(envMode == 3 && nm.isNotEmpty());
                }
            }
            K[0]->bind(&s.ratio, "RATIO");
            K[1]->bind(&s.octTranspose, "OCTAVE");
            K[2]->bind(&sf.fbLevel, "FEEDBACK");
            K[3]->bind(&mx.level, "VOLUME");
            K[4]->bind(&mx.pan, "PAN");
            for (auto &k : K) k->colour = c;
            K[2]->onTouch = [this, o] { if (P.selfNodes[(size_t)o].active.value < 0.5f) ed.setOnce(P.selfNodes[(size_t)o].active, 1.f); };
            K[3]->onTouch = [this, o] { if (P.mixerNodes[(size_t)o].active.value < 0.5f) ed.setOnce(P.mixerNodes[(size_t)o].active, 1.f); };
            if (envMode == 0) bindEnv(mx, c, &mx.envToLevel, "ENV AMT");
            else if (envMode == 1) bindEnv(sf, c, &sf.envToFB, "ENV AMT");
            else bindEnv(s, c, &s.envToRatio, "ENV AMT");
            const bool body = envMode >= 3; // the beast's own knobs (3) or the throat (4) take the envelope's place
            envOn.setVisible(!body);
            envView.setVisible(!body);
            if (body)
                for (auto &k : EK) k->setVisible(false);
            auto &tn = P.throatNodes[(size_t)o];
            auto &sm = P.sourceNodes[(size_t)o];
            MK[0]->bind(&sm.lfoRate, "LFO RATE");
            MK[1]->bind(&sm.lfoShape, "LFO SHAPE");
            MK[2]->bind(&tn.lfoToCutoff, "LFO > CUTOFF");
            MK[3]->bind(&tn.envToCutoff, "ENV > CUTOFF");
            const juce::String tg = moveName(o, (int)std::lround(tn.bodyTarget.value), false);
            MK[4]->bind(&tn.lfoToBody, "LFO > " + tg);
            MK[5]->bind(&tn.envToBody, "ENV > " + tg);
            moveTarget.bind(&tn.bodyTarget, "MOVES");
            for (auto &k : MK) k->colour = c;
            TK[0]->bind(&tn.cutoff, "CUTOFF");
            TK[1]->bind(&tn.resonance, "RESONANCE");
            TK[2]->bind(&tn.keytrack, "KEYTRACK");
            for (auto &k : TK) k->colour = c;
            throatBtn.colour = c;
            fSrc[0] = &mx.modsource[0]; fTgt[0] = &mx.modtarget[0]; fDep[0] = &mx.moddepth[0];
            fSrc[1] = &sf.modsource[0]; fTgt[1] = &sf.modtarget[0]; fDep[1] = &sf.moddepth[0];
            fSrc[2] = &s.modsource[0]; fTgt[2] = &s.modtarget[0]; fDep[2] = &s.moddepth[0];
            fSrc[4] = &mx.modsource[1]; fTgt[4] = &mx.modtarget[1]; fDep[4] = &mx.moddepth[1];
            fSrc[5] = &sf.modsource[1]; fTgt[5] = &sf.modtarget[1]; fDep[5] = &sf.moddepth[1];
            fSrc[6] = &s.modsource[1]; fTgt[6] = &s.modtarget[1]; fDep[6] = &s.moddepth[1];
            FK[0]->bind(fDep[0], "VOLUME AMT");
            FK[1]->bind(fDep[1], "FEEDBACK AMT");
            FK[2]->bind(fDep[2], "PITCH AMT");
            FK[4]->bind(fDep[4], "VOLUME AMT 2");
            FK[5]->bind(fDep[5], "FEEDBACK AMT 2");
            FK[6]->bind(fDep[6], "PITCH AMT 2");
            for (int i : {0, 1, 2, 4, 5, 6}) { FK[(size_t)i]->colour = c; followBtn[i].colour = c; }
            title = "Spirit " + juce::String(o + 1);
            subtitle = envMode == 6 ? juce::String("HANDS: let how you play push this spirit. Each row: what it follows (<) and how much, and a second one beside it.")
                                    : juce::String("Drag it onto any other spirit to possess it. Double-click: heard or not. Scroll on it: volume.");
        }
        else if (mode == THREAD)
        {
            const int f = table.selFrom, t = table.selTo;
            const auto c = seatCol(f);
            auto &m = P.matrixNodes[ss::MatrixIndex::positionForSourceTarget((size_t)f, (size_t)t)];
            tgA.bind(&m.active, "CONNECTED");
            tgB.bind(&m.overdrive, "OVERDRIVE");
            tgA.colour = tgB.colour = c;
            choice.bind(&m.modulationMode, "HOW");
            K[0]->bind(&m.level, "DEPTH");
            K[1]->bind(&m.envToLevel, "ENV AMT");
            K[0]->colour = K[1]->colour = c;
            for (int i = 2; i < 5; ++i) K[(size_t)i]->setVisible(false);
            fSrc[3] = &m.modsource[0]; fTgt[3] = &m.modtarget[0]; fDep[3] = &m.moddepth[0];
            fSrc[7] = &m.modsource[1]; fTgt[7] = &m.modtarget[1]; fDep[7] = &m.moddepth[1];
            FK[3]->bind(fDep[3], "FOLLOW AMT");
            FK[7]->bind(fDep[7], "FOLLOW AMT 2");
            for (int i : {3, 7}) { FK[(size_t)i]->colour = c; followBtn[i].colour = c; }
            bindEnv(m, c, nullptr, "");
            title = juce::String(f + 1) + " possesses " + juce::String(t + 1);
            subtitle = "How hard spirit " + juce::String(f + 1) + " bends spirit " + juce::String(t + 1) + ", and how that moves over each note. Right-click a thread to cut it.";
        }
        else
        {
            auto &o = P.output;
            choice.bind(&o.playMode, "PLAY");
            K[0]->bind(&o.level, "VOLUME");
            K[1]->bind(&o.velSensitivity, "VELOCITY");
            K[2]->bind(&o.octTranspose, "OCTAVE");
            K[3]->bind(&o.unisonCount, "UNISON");
            K[4]->bind(&o.unisonSpread, "SPREAD");
            XK[0]->bind(&o.portaTime, "GLIDE");
            XK[1]->bind(&o.pan, "PAN");
            XK[2]->bind(&o.fineTune, "FINE TUNE");
            XK[3]->bind(&o.polyLimit, "MAX VOICES");
            XK[4]->bind(&o.bendUp, "BEND RANGE");
            for (auto &k : K) k->colour = col::CANDLE;
            for (auto &k : XK) k->colour = col::CANDLE;
            bindEnv(o, col::CANDLE, nullptr, "");
            for (int i = 0; i < 6; ++i) { MACK[(size_t)i]->bind(&P.macroNodes[(size_t)i].level, macroLabel(i)); MACK[(size_t)i]->colour = col::CANDLE; }
            if (macroView)
            { // the six macro knobs take the envelope's place
                envOn.setVisible(false);
                envView.setVisible(false);
                for (auto &k : EK) k->setVisible(false);
                for (auto &k : XK) k->setVisible(false);
            }
            title = "The circle";
            subtitle = macroView ? juce::String("MACROS: six knobs of your own. Give one a job on a spirit's HANDS tab or on a thread (FOLLOWS), then turn it.")
                                 : juce::String("The whole sound: everything you hear passes through the middle of the table.");
        }
        refreshAll();
    }
    void refreshAll()
    {
        for (auto *b : bound) b->refresh();
        const bool envLive = curEnv && curEnv->envPower.value > 0.5f;
        bool live = true;
        if (mode == SPIRIT)
        {
            const int o = table.sel;
            live = P.sourceNodes[(size_t)o].active.value > 0.5f;
            const bool heard = P.mixerNodes[(size_t)o].active.value > 0.5f;
            const int sp = isShade(o) ? ss::Patch::SourceNode::shadeSpecies : (hasBeast ? (int)std::round(P.sourceNodes[(size_t)o].species.value) : 0);
            tgB.setEnabled(live);
            species.setEnabled(live);
            choice.setEnabled(live && sp == 0); // WAVE shapes the sine operator only: a beast has its own BODY
            for (int i = 0; i < 4; ++i) K[(size_t)i]->setEnabled(live);
            K[4]->setEnabled(live && heard);
            {
                const bool hasThroat = P.throatNodes[(size_t)o].kind.value > 0.5f, shadeSeat = isShade(o);
                MK[0]->setEnabled(live);
                MK[1]->setEnabled(live);
                MK[2]->setEnabled(live && hasThroat); // nothing to sweep without a filter
                MK[3]->setEnabled(live && hasThroat);
                // a wavetable spirit: FRAME / MORPH / WARP; a Surge spirit: one of its own knobs; a sine has none
                const int tgv = (int)std::lround(P.throatNodes[(size_t)o].bodyTarget.value);
                const bool beastSeat = !shadeSeat && (int)std::lround(P.sourceNodes[(size_t)o].species.value) > 0;
                const bool movable = shadeSeat || (beastSeat && moveName(o, tgv, true).isNotEmpty());
                MK[4]->setEnabled(live && movable);
                MK[5]->setEnabled(live && movable);
                moveTarget.setEnabled(live && (shadeSeat || beastSeat));
                const juce::String tg = moveName(o, tgv, false);
                MK[4]->label = "LFO > " + tg;
                MK[5]->label = "ENV > " + tg;
                MK[4]->setName(MK[4]->label);
                MK[5]->setName(MK[5]->label);
            }
            {
                const bool gate[3] = {heard, P.selfNodes[(size_t)o].active.value > 0.5f, true}; // nothing to push on a spirit that is not heard / has no feedback
                static const char *what[3] = {"VOLUME", "FEEDBACK", "PITCH"};
                for (int i : {0, 1, 2, 4, 5, 6})
                {
                    const int src = fSrc[i] ? (int)std::lround(fSrc[i]->value) : 0;
                    followBtn[i].text = (i < 4 ? juce::String(what[i]) + " < " : juce::String("AND < ")) + followName(src) + " v";
                    followBtn[i].lit = src != 0;
                    followBtn[i].setEnabled(live && gate[i % 4]);
                    FK[(size_t)i]->setEnabled(live && gate[i % 4] && src != 0);
                    followBtn[i].repaint();
                }
            }
            for (int i = 0; i < 7; ++i)
            {
                envTab[(size_t)i]->lit = (envMode == i);
                envTab[(size_t)i]->colour = seatCol(o);
                envTab[(size_t)i]->setEnabled(live && (i != 3 || sp > 0));
                envTab[(size_t)i]->repaint();
            }
            {
                const auto &cat = throatCatalog();
                const int tk = juce::jlimit(0, (int)cat.size() - 1, (int)std::lround(P.throatNodes[(size_t)o].kind.value));
                throatBtn.text = tk == 0 ? juce::String("THROAT: OFF  (pick a filter)") : "THROAT: " + juce::String(cat[(size_t)tk].group) + " - " + juce::String(cat[(size_t)tk].name);
                throatBtn.lit = tk > 0;
                throatBtn.setEnabled(live);
                throatBtn.repaint();
                for (auto &k : TK) k->setEnabled(live && tk > 0); // no filter picked = these would do nothing
            }
            if (isShade(o))
            { // MORPH does nothing without a morph type, WARP nothing without a warp type
                float v7[7];
                auto &sn = P.sourceNodes[(size_t)o];
                for (int k = 0; k < 7; ++k) v7[k] = sn.beastSet.value > 0.5f ? sn.beastP[(size_t)k].value : shade_param_default01(k);
                const bool ownTable = !P.shadeTables[(size_t)o].json.empty();
                tableBtn.text = ownTable ? "TABLE  " + juce::String(P.shadeTables[(size_t)o].name) + "  v" : juce::String("TABLE  built-in  v");
                tableBtn.lit = ownTable;
                tableBtn.setEnabled(live);
                tableBtn.repaint();
                for (int k = 0; k < 7; ++k)
                {
                    if (shade_param_steps(k) > 1)
                    { // the picker knobs always say what they are on
                        char buf[64]{};
                        shade_param_display(k, v7[k], buf, sizeof(buf));
                        BK[(size_t)k]->label = juce::String(shade_param_name(k)) + ": " + juce::String(buf);
                    }
                    if (k == 0 && ownTable) BK[0]->label = "TABLE: (file)";
                    // the TABLE knob picks among the built-in tables: it does nothing while a file is loaded
                    BK[(size_t)k]->setEnabled(live && shade_param_live(k, v7) != 0 && !(k == 0 && ownTable));
                    BK[(size_t)k]->repaint();
                }
            }
            else
                for (auto &b : BK) { b->setEnabled(live); b->repaint(); }
        }
        else if (mode == THREAD)
        {
            auto &m = P.matrixNodes[ss::MatrixIndex::positionForSourceTarget((size_t)table.selFrom, (size_t)table.selTo)];
            live = m.active.value > 0.5f;
            tgB.setEnabled(live);
            choice.setEnabled(live);
            K[0]->setEnabled(live);
            K[1]->setEnabled(live && envLive);
            cutBtn.setEnabled(live);
            for (int i : {3, 7})
            {
                const int src = fSrc[i] ? (int)std::lround(fSrc[i]->value) : 0;
                followBtn[i].text = juce::String(i == 3 ? "DEPTH < " : "AND < ") + followName(src) + " v";
                followBtn[i].lit = src != 0;
                followBtn[i].setEnabled(live);
                FK[(size_t)i]->setEnabled(live && src != 0);
                followBtn[i].repaint();
            }
        }
        else
        {
            K[4]->setEnabled(P.output.unisonCount.value > 1.5f); // SPREAD does nothing with one voice per note
        }
        envOn.setEnabled(live);
        envView.on = live && envLive;
        envView.repaint();
        for (int i = 0; i < 7; ++i) EK[(size_t)i]->setEnabled(live && envLive);
        presetBtn.text = juce::String(P.name) + (P.dirty ? " *" : "");
        presetBtn.repaint();
        repaint(PX - 8, 48, PW + 16, 640);
    }

    void paint(juce::Graphics &g) override
    {
        g.fillAll(col::SOOT);
        // the sound bar
        g.setFont(F->g(40.f));
        g.setColour(col::BLOOD);
        g.drawText(juce::String::fromUTF8("S\xc3\x89" "ANCE"), 19, 3, 240, 46, juce::Justification::centredLeft);
        g.setColour(col::BONE);
        g.drawText(juce::String::fromUTF8("S\xc3\x89" "ANCE"), 18, 2, 240, 46, juce::Justification::centredLeft);
        g.setFont(F->b(10.5f));
        g.setColour(col::DIM);
        g.drawText("TWELVE SPIRITS", 150, 12, 110, 12, juce::Justification::centredLeft);
        g.drawText("GHOSTBLOCK", 150, 26, 110, 12, juce::Justification::centredLeft);
        g.drawText(voices, W - 110, 4, 94, 24, juce::Justification::centredRight);
        // the panel
        juce::Rectangle<float> pr((float)PX - 8, 52.f, (float)PW + 16, 630.f);
        g.setColour(col::PANEL);
        g.fillRoundedRectangle(pr, 8.f);
        g.setColour(col::EDGE);
        g.drawRoundedRectangle(pr, 8.f, 1.f);
        const auto tc = mode == SPIRIT ? seatCol(table.sel) : mode == THREAD ? seatCol(table.selFrom) : col::CANDLE;
        g.setFont(F->g(30.f));
        g.setColour(tc);
        g.drawText(title, PX, 56, PW, 32, juce::Justification::centredLeft);
        g.setFont(F->r(11.f));
        g.setColour(col::DIM);
        g.drawFittedText(subtitle, PX, 86, PW, 24, juce::Justification::topLeft, 2, 0.9f);
        // who it is tied to (SPIRIT)
        chips.clear();
        if (mode == SPIRIT)
        {
            const int o = table.sel;
            int y = 528;
            for (int pass = 0; pass < 2; ++pass)
            {
                g.setFont(F->b(10.5f));
                g.setColour(col::DIM);
                g.drawText(pass == 0 ? "POSSESSED BY" : "POSSESSES", PX, y, 96, 26, juce::Justification::centredLeft);
                int x = PX + 100, n = 0;
                for (int k = 0; k < 12; ++k)
                {
                    const int f = pass == 0 ? k : o, t = pass == 0 ? o : k;
                    if (f == t) continue;
                    auto &m = P.matrixNodes[ss::MatrixIndex::positionForSourceTarget((size_t)f, (size_t)t)];
                    if (m.active.value < 0.5f) continue;
                    juce::Rectangle<int> r(x, y + 1, 26, 24);
                    g.setColour(seatCol(k).withAlpha(0.2f));
                    g.fillRoundedRectangle(r.toFloat(), 4.f);
                    g.setColour(seatCol(k));
                    g.drawRoundedRectangle(r.toFloat(), 4.f, 1.2f);
                    g.setColour(col::BONE);
                    g.drawText(juce::String(k + 1), r, juce::Justification::centred);
                    chips.push_back({r, f, t});
                    x += 29;
                    ++n;
                }
                if (n == 0)
                {
                    g.setFont(F->r(11.f));
                    g.setColour(col::DIM.withAlpha(0.6f));
                    g.drawText(pass == 0 ? "nobody" : "nobody yet", PX + 100, y, 200, 26, juce::Justification::centredLeft);
                }
                y += 32;
            }
        }
        g.setFont(F->r(12.f));
        g.setColour(col::BONE.withAlpha(0.85f));
        g.drawFittedText(statusText, PX, 636, PW, 40, juce::Justification::centredLeft, 2, 0.85f);
    }
    void mouseDown(const juce::MouseEvent &e) override
    {
        for (auto &c : chips)
            if (c.r.contains(e.getPosition())) { ed.selectThread(c.from, c.to); return; }
    }
};

// =========================================================================================================
SeanceEditor::SeanceEditor(ss::Patch &pm, ss::WavetableHandoff &h, ss::Synth::audioToMainQueue_t &atou, ss::Synth::mainToAudioQueue_T &utoa,
                           ss::Synth::audioOutputQueue_t &aor, std::atomic<bool> &ea, std::atomic<uint32_t> &fr, ss::Synth::DawStateMain &ds,
                           const clap_host_t *host, UiNoteQueue &notes, ghostblock::p2::Morph *mo, ghostblock::p2::spectral::SpectralFx *sp,
                           ghostblock::p2::sub::SubBass *sb, StepSeq *sq, OutGuard *og, std::atomic<bool> *hd, BlendGuard *bg)
    : patch(pm), spectral(sp), sub(sb), morph(mo), steps(sq), guard(og), blendGuard(bg), seanceHostDirty(hd), handoff(h), audioToMain(atou), mainToAudio(utoa), audioOutputRing(aor), editorActive(ea), uiForceRebuild(fr), dawState(ds),
      clapHost(host), noteQueue(notes)
{
    ss::MatrixIndex::initialize();
    lastForceRebuild = uiForceRebuild.load();
    impl = std::make_unique<Impl>(*this, notes);
    addAndMakeVisible(*impl);
    impl->setBounds(0, 0, W, H);
    setSize(W, H);
    editorActive = true;
    audioOutputRing.subscribe();
    mainToAudio.push({ss::Synth::MainToAudioMsg::REQUEST_NON_PATCH_STATE});
    requestFlush();
    status("Drag one spirit onto another to make it possess it. Click a spirit, a thread or the middle to shape it.");
    startTimerHz(30);
}

SeanceEditor::~SeanceEditor()
{
    stopTimer();
    audioOutputRing.unsubscribe();
    editorActive = false;
    if (clapHost) clapHost->request_callback(clapHost);
    impl.reset();
}

void SeanceEditor::paint(juce::Graphics &g) { g.fillAll(col::SOOT); }
void SeanceEditor::resized()
{
    if (impl) impl->setTransform(juce::AffineTransform::scale((float)getWidth() / (float)W, (float)getHeight() / (float)H));
}
void SeanceEditor::requestFlush()
{
    if (!clapHost) return;
    if (auto *ext = static_cast<const clap_host_params_t *>(clapHost->get_extension(clapHost, CLAP_EXT_PARAMS))) ext->request_flush(clapHost);
}
void SeanceEditor::beginEdit(ss::Param &p) { mainToAudio.push({ss::Synth::MainToAudioMsg::BEGIN_EDIT, p.meta.id}); }
void SeanceEditor::setValue(ss::Param &p, float v)
{
    p.value = v;
    patch.dirty = true;
    mainToAudio.push({ss::Synth::MainToAudioMsg::SET_PARAM, p.meta.id, v});
    requestFlush();
    uiDirty = true;
}
void SeanceEditor::endEdit(ss::Param &p)
{
    mainToAudio.push({ss::Synth::MainToAudioMsg::END_EDIT, p.meta.id});
    requestFlush();
}
void SeanceEditor::setOnce(ss::Param &p, float v)
{
    beginEdit(p);
    setValue(p, v);
    endEdit(p);
}
void SeanceEditor::status(const juce::String &s)
{
    if (!impl) return;
    impl->statusText = s;
    impl->repaint();
}
void SeanceEditor::selectSpirit(int op)
{
    impl->table.sel = juce::jlimit(0, (int)ss::numOps - 1, op);
    impl->table.selFrom = impl->table.selTo = -1;
    impl->table.selCircle = false;
    impl->mode = Impl::SPIRIT;
    impl->rebind();
}
void SeanceEditor::selectThread(int from, int to)
{
    if (from < 0 || to < 0 || to == from || from >= (int)ss::numOps || to >= (int)ss::numOps) return;
    impl->table.selFrom = from;
    impl->table.selTo = to;
    impl->table.selCircle = false;
    impl->mode = Impl::THREAD;
    impl->rebind();
}
void SeanceEditor::selectCircle()
{
    impl->table.selFrom = impl->table.selTo = -1;
    impl->table.selCircle = true;
    impl->mode = Impl::CIRCLE;
    impl->rebind();
}
void SeanceEditor::refreshAll() { impl->refreshAll(); uiDirty = false; }
void SeanceEditor::showPage(int pg) { impl->setPage(pg); }
void SeanceEditor::spectralEdit(uint32_t id, double v, int type)
{
    if (!spectral) return;
    spectral->uiEdit(id, v, type);
    requestFlush();
    // a new effect / FFT size is built on the main thread: by the plugin when there is a host, here when there is none
    if (clapHost) clapHost->request_callback(clapHost);
    else spectral->serviceMainThread();
}
void SeanceEditor::tellHost()
{
    if (seanceHostDirty) seanceHostDirty->store(true);
    if (clapHost) clapHost->request_callback(clapHost);
    markStateDirty();
}
bool SeanceEditor::testLimiterOn() const { return guard && guard->on.load(); }
void SeanceEditor::testClickMeter() { impl->meter.toggle(); }
void SeanceEditor::morphEdit(uint32_t id, double v, int type)
{
    if (!morph) return;
    morph->uiEdit(id, v, type);
    requestFlush();
}
int SeanceEditor::testPhaseFix(int what)
{
    if (!blendGuard) return -1;
    if (what == 1 && impl->blendPage) impl->blendPage->fixBtn.onClick();
    if (what == 2) return juce::roundToInt(blendGuard->cancelDb.load() * 10.f);
    return blendGuard->on.load() ? 1 : 0;
}
int SeanceEditor::testCrossroads(int which)
{ // which >= 0: load that ready-made crossroads (returns how many corners got a sound); -1: how many there are
    if (!impl->blendPage) return -1;
    if (which < 0) return (int)crossRecipes().size();
    return impl->blendPage->loadRecipe(crossRecipes()[(size_t)which]);
}
bool SeanceEditor::testCrossroadsSaveLoad(const juce::String &name)
{
    if (!impl->blendPage) return false;
    auto &bp = *impl->blendPage;
    if (!bp.saveAs(name)) return false;
    const auto f = bp.savedFolder().getChildFile(juce::File::createLegalFileName(name) + ".crossroads");
    const bool ok = bp.loadSaved(f);
    f.deleteFile();
    return ok;
}
void SeanceEditor::testBlendCorner(int c, int fi)
{
    if (!impl->blendPage) return;
    if (fi < 0) impl->blendPage->setCornerCopy(c);
    else
    {
        const auto &v = impl->blendPage->pm->factoryPatchVector;
        impl->blendPage->setCornerFactory(c, v[(size_t)fi].first, v[(size_t)fi].second);
    }
}
int SeanceEditor::testBlendEnabled(const juce::String &name) const
{
    if (!impl->blendPage) return -1;
    for (auto *c : impl->blendPage->getChildren())
        if (c->getName() == name) return c->isEnabled() ? 1 : 0;
    return -1;
}
void SeanceEditor::subEdit(uint32_t id, double v, int type)
{
    if (!sub) return;
    sub->uiEdit(id, v, type);
    requestFlush();
}
void SeanceEditor::markStateDirty()
{
    if (!clapHost) return;
    if (auto *st = static_cast<const clap_host_state_t *>(clapHost->get_extension(clapHost, CLAP_EXT_STATE))) st->mark_dirty(clapHost);
}
int SeanceEditor::testPage() const { return impl->page; }
bool SeanceEditor::testHasBeast() const { return impl->hasBeast; }
void SeanceEditor::testEnvTab(int tab) { impl->envMode = tab; impl->rebind(); }
void SeanceEditor::testFollow(int which, int sourceId) { impl->setFollow(which, sourceId); }
void SeanceEditor::testMacroView(bool on) { impl->macroView = on; impl->rebind(); }
bool SeanceEditor::testLoadShadeTable(int seat, const juce::String &name, const std::string &json) { return impl->loadShadeTable(seat, name, json); }
bool SeanceEditor::testStepsMake(int kind, uint32_t seed, int follow) { return impl->stepsPage && impl->stepsPage->make(kind, seed, follow); }
bool SeanceEditor::testStepsExact(int note, double loud, double sixteenths) { return impl->stepsPage && impl->stepsPage->setExact(note, loud, sixteenths); }
int SeanceEditor::testStepsEnabled(const juce::String &name) const
{
    if (!impl->stepsPage) return -1;
    for (auto *c : impl->stepsPage->getChildren())
        if (c->getName() == name) return c->isEnabled() ? 1 : 0;
    return -1;
}
std::vector<uint8_t> SeanceEditor::testStepsMidi() const { return impl->stepsPage ? impl->stepsPage->midiBytes() : std::vector<uint8_t>(); }
void SeanceEditor::testStepsTool(int what, int arg)
{
    if (!impl->stepsPage) return;
    auto &pg = *impl->stepsPage;
    if (what == 0) pg.setLoopBars(arg);
    else if (what == 1) pg.repeatLoop();
    else if (what == 2) pg.copyBar(arg, false);
    else if (what == 3) pg.copyBar(arg, true);
    else if (what == 4) pg.clearBar(arg);
    else if (what == 5) { pg.drawLane = arg; pg.refresh(); }
}
void SeanceEditor::testStepsDrag(float tick, int row, float toTick, int toRow)
{
    if (!impl->stepsPage) return;
    auto &pg = *impl->stepsPage;
    auto ev = [&](juce::Point<float> p, juce::Point<float> down) {
        return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p, juce::ModifierKeys::leftButtonModifier, 0.f, 0.f, 0.f, 0.f, 0.f, &pg, &pg, juce::Time::getCurrentTime(), down,
                                juce::Time::getCurrentTime(), 1, true);
    };
    const juce::Point<float> a(pg.xOf(tick), pg.yOfRow(row) + 2.f), b(pg.xOf(toTick), pg.yOfRow(toRow) + 2.f);
    pg.mouseDown(ev(a, a));
    pg.mouseDrag(ev(b, a));
    pg.mouseUp(ev(b, a));
}
void SeanceEditor::testStepsClick(float tick, int row)
{
    if (!impl->stepsPage) return;
    auto &pg = *impl->stepsPage;
    const juce::Point<float> p(pg.xOf(tick) + 2.f, pg.yOfRow(row) + 2.f);
    const auto ev = juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), p, juce::ModifierKeys::leftButtonModifier, 0.f, 0.f, 0.f, 0.f, 0.f, &pg, &pg,
                                     juce::Time::getCurrentTime(), p, juce::Time::getCurrentTime(), 1, false);
    pg.mouseDown(ev);
    pg.mouseUp(ev);
}
juce::String SeanceEditor::testSummon(int style) { return impl->summon(style); }
juce::String SeanceEditor::testSummonAround(int style, int keep) { return impl->summonAround(style, keep); }
int SeanceEditor::testSearch(const juce::String &text) { return (int)impl->searchSounds(text).size(); }
int SeanceEditor::testFavourite(int what)
{ // 0: how many; 1: toggle the sound on the table (returns 1 if it is now a favourite)
    if (what == 1) return impl->toggleFavourite(impl->currentKey) ? 1 : 0;
    int n = 0;
    const auto favs = impl->favKeys();
    for (auto &e : impl->allSounds()) n += favs.contains(juce::String(e.key())) ? 1 : 0;
    return n;
}
bool SeanceEditor::testMutate(float amount, int what) { return impl->mutate(amount, what); }
void SeanceEditor::testSetMacroName(int i, const juce::String &n) { impl->setMacroName(i, n); }
bool SeanceEditor::testBringBack() { return impl->bringBack(); }
void SeanceEditor::testSaveSoundTo(const std::string &path) { impl->writeSound(fs::path(path)); }
void SeanceEditor::testLoadSoundFrom(const std::string &path) { impl->pm->loadUserPresetDirect(impl->P, mainToAudio, fs::path(path)); }
bool SeanceEditor::testLoadWav(int seat, const juce::String &name, const void *data, size_t size) { return impl->loadWavBytes(seat, name, data, size); }
void SeanceEditor::testPickSpecies(int seat, int v)
{
    selectSpirit(seat);
    impl->pickSpecies(seat, v, true);
}
juce::Component *SeanceEditor::testPageComponent(int pg) const { return pg == 4 ? (juce::Component *)impl->stepsPage.get() : pg == 3 ? (juce::Component *)impl->blendPage.get() : pg == 1 ? (juce::Component *)&impl->fxPage : pg == 2 ? (juce::Component *)impl->subPage.get() : (juce::Component *)&impl->table; }
bool SeanceEditor::testSubKnobEnabled(const juce::String &label) const
{
    if (!impl->subPage) return false;
    for (auto &k : impl->subPage->knobs)
        if (k->label == label) return k->isEnabled();
    return false;
}
float SeanceEditor::histSample(int back) const
{
    return scope[(size_t)(((scopePos - 1 - back) % kHist + kHist) % kHist)];
}

juce::Component *SeanceEditor::testTable() const { return &impl->table; }
juce::Point<float> SeanceEditor::testSeatPos(int s) const { return Table::seat(juce::jlimit(0, 11, s)); }
juce::Point<float> SeanceEditor::testThreadPos(int from, int to) const
{
    for (auto &t : impl->table.threads)
        if (t.from == from && t.to == to) return t.pts[t.pts.size() / 2];
    return {-1, -1};
}
juce::Point<float> SeanceEditor::testCentre() const { return {Table::CX, Table::CY}; }
juce::String SeanceEditor::testPanelTitle() const { return impl->title; }
juce::String SeanceEditor::testStatus() const { return impl->statusText; }
int SeanceEditor::testFactoryCount() const { return (int)impl->pm->factoryPatchVector.size(); }
juce::String SeanceEditor::testLoadFactory(int i)
{
    impl->loadFactory(i);
    return juce::String(patch.name);
}
std::vector<SeanceEditor::CtlInfo> SeanceEditor::testControls() const
{
    std::vector<CtlInfo> v;
    auto add = [&](juce::Component &c) { v.push_back({c.getName(), c.getBounds(), c.isVisible(), c.isEnabled()}); };
    add(impl->tgA); add(impl->tgB); add(impl->choice); add(impl->envOn); add(impl->cutBtn); add(impl->species);
    for (auto &k : impl->BK) add(*k);
    for (auto &k : impl->K) add(*k);
    for (auto &k : impl->EK) add(*k);
    for (auto &k : impl->XK) add(*k);
    for (auto &t : impl->envTab) add(*t);
    add(impl->throatBtn);
    for (auto &k : impl->MK) add(*k);
    for (auto &k : impl->FK) add(*k);
    for (auto &b : impl->followBtn) add(b);
    for (auto &k : impl->MACK) add(*k);
    for (auto &b : impl->macroNameBtn) add(b);
    add(impl->macroBtn); add(impl->summonBtn);
    add(impl->moveTarget);
    add(impl->tableBtn);
    for (auto &k : impl->TK) add(*k);
    add(impl->prevBtn); add(impl->nextBtn); add(impl->presetBtn); add(impl->saveBtn); add(impl->initBtn);
    return v;
}

void SeanceEditor::drainAudio()
{
    auto aum = audioToMain.pop();
    while (aum.has_value())
    {
        using A = ss::Synth::AudioToMainMsg;
        if (aum->action == A::UPDATE_PARAM)
        {
            auto it = patch.paramMap.find(aum->paramId);
            if (it != patch.paramMap.end()) it->second->value = aum->value;
            uiDirty = true;
        }
        else if (aum->action == A::UPDATE_VU)
        {
            if (aum->paramId >= 1 && aum->paramId <= ss::numOps) vu[aum->paramId - 1] = juce::jmax(aum->value, aum->value2);
        }
        else if (aum->action == A::UPDATE_VOICE_COUNT)
        {
            const auto t = "VOICES " + juce::String((int)aum->paramId);
            if (t != impl->voices) { impl->voices = t; impl->repaint(W - 112, 0, 112, 48); }
        }
        aum = audioToMain.pop();
    }
    auto [L, R] = audioOutputRing.popall();
    juce::ignoreUnused(R);
    const size_t n = L.size(), start = n > (size_t)kHist ? n - (size_t)kHist : 0;
    for (size_t i = start; i < n; ++i)
    {
        scope[(size_t)scopePos] = L[i];
        scopePos = (scopePos + 1) % kHist;
    }
}

void SeanceEditor::timerCallback()
{
    drainAudio();
    const auto fr = uiForceRebuild.load();
    if (fr != lastForceRebuild)
    {
        lastForceRebuild = fr;
        uiDirty = true;
    }
    ss::reconcileWavetables<ss::Synth::mainToAudioQueue_T, ss::Synth::MainToAudioMsg>(patch, handoff, mainToAudio, wavetableError);
    if (uiDirty) refreshAll();
    for (auto &v : vu) v *= 0.85f;
    if (impl->page == 0) impl->table.tick();
    if (impl->subPage) impl->subPage->tick();
    if (impl->blendPage) impl->blendPage->tick();
    if (impl->stepsPage) impl->stepsPage->tick();
    impl->meter.tick();
}
} // namespace ghostblock::seance
