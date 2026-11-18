/*
 * Poltergeist 2 (GhostBlock) - the Poltergeist look on the Six Sines engine.
 *
 * The page layout, colours, ghost field, ghost panel, envelope view, bottom cards, six macro knobs and the
 * keyboard follow Poltergeist's editor (ghosty-pro/Poltergeist/PluginEditor.cpp). Every control is bound to
 * a parameter of the upstream Six Sines patch and edits it through the same queue messages the upstream
 * editor sends (BEGIN_EDIT / SET_PARAM / END_EDIT). Nothing here touches the sound path.
 */
#include "ghostblock/p2-editor.h"
#include "ghostblock/spectral/spectral-panel.h" // SPECTRAL FX: the fourth page
#include "ghostblock/GhostThemes.h"

#include "presets/preset-manager.h"
#include "synth/macro_usage.h"
#include "synth/matrix_index.h"
#include "synth/wavetable_handoff.h"

#include <clap/ext/params.h>
#include <cmath>
#include <functional>
#include <map>

namespace ghostblock::p2
{
using Param = ss::Param;
namespace th = ghosty::themes;

// =========================================================================================================
// colours: the current GhostBlock theme (BLACK / PURPLE / LIGHT), read at paint time
namespace col
{
inline juce::Colour BG, CARD, CARD2, INK, GREY, LINE, PURPLE, VIOLET, GREEN, RED, AMBER, PINK, CYAN, BLUE,
    KEYWHITE, KEYBLACK, ON_ACCENT;
inline juce::Colour GHOST[ss::numOps];
inline th::Theme theme = th::Theme::Dark;
inline juce::Colour FG(float a)
{
    const auto &T = th::get(theme);
    return T.overlay.withAlpha(T.isLight ? juce::jmin(1.0f, a * 1.8f) : a);
}
inline void apply(th::Theme t)
{
    theme = t;
    const auto &T = th::get(t);
    BG = T.window; CARD = T.panel; CARD2 = T.cell; INK = T.ink; GREY = T.dim; LINE = T.line;
    PURPLE = T.violet; VIOLET = T.isLight ? T.violet.brighter(0.15f) : T.violet.darker(0.25f);
    GREEN = T.accent3; RED = T.bad; AMBER = T.warn; PINK = T.accent1; CYAN = T.accent2;
    for (auto *a : {&PURPLE, &GREEN, &RED, &AMBER, &PINK, &CYAN})
        *a = th::readableOn(*a, T.panel, 3.2);
    BLUE = th::readableOn(juce::Colour(0xff60a5fa), T.panel, 3.2);
    KEYWHITE = T.keyWhite; KEYBLACK = T.keyBlack; ON_ACCENT = T.onAccent;
    // Poltergeist's ghost colours (ghost 1 = the body text colour)
    const juce::uint32 g[ss::numOps] = {0, 0xff5fb24a, 0xff9b74d2, 0xff5bd7f5, 0xfffbbf24, 0xfff472b6};
    GHOST[0] = T.ink;
    for (int i = 1; i < (int)ss::numOps; ++i)
        GHOST[i] = th::readableOn(juce::Colour(g[i]), T.panel, 3.2);
}
} // namespace col
using namespace col;

static juce::String versionText()
{
#ifdef POLTERGEIST2_VERSION
    return juce::String("v") + POLTERGEIST2_VERSION;
#else
    return "v-dev";
#endif
}
static juce::String toStr(const std::optional<std::string> &s) { return s ? juce::String(*s) : juce::String(); }

// =========================================================================================================
// look and feel (Poltergeist's)
struct LNF : juce::LookAndFeel_V4
{
    LNF() { applyColours(); }
    void applyColours()
    {
        setColourScheme(getDarkColourScheme());
        setColour(juce::Label::textColourId, INK);
        setColour(juce::TextEditor::backgroundColourId, CARD2);
        setColour(juce::TextEditor::textColourId, INK);
        setColour(juce::TextEditor::outlineColourId, PURPLE);
        setColour(juce::TextEditor::focusedOutlineColourId, PURPLE);
        setColour(juce::CaretComponent::caretColourId, GREEN);
        setColour(juce::ComboBox::backgroundColourId, CARD2);
        setColour(juce::ComboBox::textColourId, INK);
        setColour(juce::ComboBox::arrowColourId, PURPLE);
        setColour(juce::PopupMenu::backgroundColourId, CARD2);
        setColour(juce::PopupMenu::textColourId, INK);
        setColour(juce::PopupMenu::headerTextColourId, GREY);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, PURPLE);
        setColour(juce::PopupMenu::highlightedTextColourId, ON_ACCENT);
        setColour(juce::TextButton::textColourOffId, INK);
        setColour(juce::TextButton::textColourOnId, ON_ACCENT);
        setColour(juce::Slider::textBoxTextColourId, INK);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::TooltipWindow::backgroundColourId, CARD2);
        setColour(juce::TooltipWindow::textColourId, INK);
        setColour(juce::TooltipWindow::outlineColourId, PURPLE);
        setColour(juce::BubbleComponent::backgroundColourId, CARD2);
        setColour(juce::BubbleComponent::outlineColourId, PURPLE);
        setColour(juce::AlertWindow::backgroundColourId, CARD);
        setColour(juce::AlertWindow::textColourId, INK);
        setColour(juce::AlertWindow::outlineColourId, PURPLE);
        setColour(juce::ResizableWindow::backgroundColourId, BG);
        setColour(juce::ScrollBar::thumbColourId, PURPLE);
        setColour(juce::Slider::trackColourId, PURPLE);
        setColour(juce::Slider::thumbColourId, GREEN);
        setColour(juce::Slider::backgroundColourId, LINE);
    }
    void drawPopupMenuBackground(juce::Graphics &g, int w, int h) override
    {
        g.fillAll(CARD2);
        g.setColour(PURPLE);
        g.drawRect(0, 0, w, h, 1);
    }
    juce::Font getComboBoxFont(juce::ComboBox &) override { return juce::FontOptions(13.0f, juce::Font::bold); }
    juce::Font getTextButtonFont(juce::TextButton &, int h) override
    {
        return juce::FontOptions(juce::jmin(13.5f, h * 0.48f), juce::Font::bold);
    }
    juce::Font getPopupMenuFont() override { return juce::FontOptions(14.0f); }
    void drawButtonText(juce::Graphics &g, juce::TextButton &b, bool, bool) override
    {
        const bool on = b.getToggleState() || (bool)b.getProperties()["accent"];
        g.setFont(getTextButtonFont(b, b.getHeight()));
        g.setColour(!b.isEnabled() ? GREY : on ? ON_ACCENT : INK);
        g.drawFittedText(b.getButtonText(), b.getLocalBounds().reduced(4, 2), juce::Justification::centred, 2, 0.8f);
    }
    void drawButtonBackground(juce::Graphics &g, juce::Button &b, const juce::Colour &, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced(1);
        const bool on = b.getToggleState() || (bool)b.getProperties()["accent"];
        const auto hover = CARD2.interpolatedWith(PURPLE, 0.18f);
        g.setColour(on ? PURPLE : (over ? hover : CARD2));
        g.fillRoundedRectangle(r, r.getHeight() * 0.35f);
        g.setColour(on ? VIOLET : (over ? PURPLE : LINE));
        g.drawRoundedRectangle(r, r.getHeight() * 0.35f, down ? 2.0f : 1.2f);
    }
    void drawToggleButton(juce::Graphics &g, juce::ToggleButton &b, bool over, bool) override
    {
        auto r = b.getLocalBounds().toFloat().reduced(2);
        const bool on = b.getToggleState();
        g.setColour(on ? PURPLE : CARD2);
        g.fillRoundedRectangle(r, 8);
        g.setColour(on ? VIOLET : (over ? PURPLE : LINE));
        g.drawRoundedRectangle(r, 8, 1.2f);
        g.setColour(on ? ON_ACCENT : INK);
        g.setFont(juce::FontOptions(12.5f, juce::Font::bold));
        g.drawFittedText(b.getButtonText(), r.toNearestInt().reduced(3, 0), juce::Justification::centred, 1, 0.75f);
    }
    void drawComboBox(juce::Graphics &g, int w, int h, bool, int, int, int, int, juce::ComboBox &) override
    {
        auto r = juce::Rectangle<float>(0, 0, (float)w, (float)h).reduced(1);
        g.setColour(CARD2);
        g.fillRoundedRectangle(r, 8);
        g.setColour(LINE);
        g.drawRoundedRectangle(r, 8, 1.2f);
        juce::Path arrow;
        const float ax = (float)w - 14, ay = (float)h * 0.5f;
        arrow.addTriangle(ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
        g.setColour(PURPLE);
        g.fillPath(arrow);
    }
    void positionComboBoxText(juce::ComboBox &b, juce::Label &l) override
    {
        l.setBounds(6, 1, b.getWidth() - 24, b.getHeight() - 2);
        l.setFont(getComboBoxFont(b));
    }
    void drawRotarySlider(juce::Graphics &g, int x, int y, int w, int h, float pos, float a0, float a1,
                          juce::Slider &s) override
    {
        auto r = juce::Rectangle<float>((float)x, (float)y, (float)w, (float)h).reduced(4);
        const float size = juce::jmin(r.getWidth(), r.getHeight());
        auto c = r.withSizeKeepingCentre(size, size);
        const bool big = s.getProperties()["big"];
        const bool bip = s.getProperties()["bipolar"];
        const float thick = big ? 7.0f : 4.0f;
        const float ang = a0 + pos * (a1 - a0);
        const float mid = a0 + 0.5f * (a1 - a0);
        const float amt = bip ? std::abs(pos - 0.5f) * 2.0f : pos;
        if (amt > 0.01f)
        {
            g.setColour(PURPLE.withAlpha((0.10f + 0.18f * amt) * 0.6f));
            g.fillEllipse(c.expanded(big ? 6.0f : 3.0f));
        }
        g.setColour(CARD2);
        g.fillEllipse(c.reduced(thick + 2));
        juce::Path track;
        track.addCentredArc(c.getCentreX(), c.getCentreY(), size / 2 - thick / 2, size / 2 - thick / 2, 0, a0, a1, true);
        g.setColour(LINE);
        g.strokePath(track, juce::PathStrokeType(thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        juce::Path arc;
        arc.addCentredArc(c.getCentreX(), c.getCentreY(), size / 2 - thick / 2, size / 2 - thick / 2, 0,
                          bip ? juce::jmin(mid, ang) : a0, bip ? juce::jmax(mid, ang) : ang, true);
        g.setGradientFill(juce::ColourGradient(PURPLE, c.getX(), c.getBottom(), GREEN, c.getRight(), c.getY(), false));
        g.strokePath(arc, juce::PathStrokeType(thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const auto ctr = c.getCentre();
        g.setColour(s.isEnabled() ? INK : GREY);
        g.drawLine({ctr.getPointOnCircumference(size * 0.12f, ang), ctr.getPointOnCircumference(size / 2 - thick - 5, ang)},
                   big ? 3.0f : 2.0f);
        if (big)
        { // little ghost eyes in the middle of the macro knobs
            g.fillRoundedRectangle(ctr.x - 9, ctr.y + 4, 6, 8, 1.5f);
            g.fillRoundedRectangle(ctr.x + 3, ctr.y + 4, 6, 8, 1.5f);
        }
    }
};

static void card(juce::Graphics &g, juce::Rectangle<int> r, const juce::String &title)
{
    g.setColour(CARD);
    g.fillRoundedRectangle(r.toFloat(), 16);
    g.setColour(LINE);
    g.drawRoundedRectangle(r.toFloat().reduced(0.5f), 16, 1);
    if (title.isNotEmpty())
    {
        g.setColour(GREY);
        g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
        g.drawText(title, r.reduced(14, 6).removeFromTop(14), juce::Justification::centredLeft);
    }
}

// =========================================================================================================
// controls bound to one engine parameter
struct Bound
{
    virtual ~Bound() = default;
    virtual void refresh() = 0;
};

// the slider: popup text from the parameter's own formatting, right-click to type a value, optional snap
struct GSlider : juce::Slider
{
    Param *p{nullptr};
    std::function<double(double)> snap;
    std::function<void()> onTypeIn;
    GSlider() : juce::Slider(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox) {}
    juce::String getTextFromValue(double v) override
    {
        if (!p)
            return juce::String(v, 2);
        auto s = toStr(p->meta.valueToString((float)v));
        return s.isNotEmpty() ? s : juce::String(v, 2);
    }
    double snapValue(double v, DragMode m) override
    {
        if (snap && m != notDragging && !juce::ModifierKeys::currentModifiers.isShiftDown())
            return snap(v);
        return v;
    }
    void mouseDown(const juce::MouseEvent &e) override
    {
        if (e.mods.isPopupMenu() && onTypeIn)
        {
            onTypeIn();
            return;
        }
        juce::Slider::mouseDown(e);
    }
};

// A knob with its name under it (Poltergeist's Knob), bound to a Six Sines parameter.
struct PKnob : juce::Component, Bound
{
    Poltergeist2Editor &ed;
    GSlider s;
    juce::Label label;
    bool big, dragging{false}, syncing{false};
    std::function<void()> onBegin; // e.g. wake the ghost when OUT is touched
    PKnob(Poltergeist2Editor &e, const juce::String &name, bool b = false) : ed(e), big(b)
    {
        s.getProperties().set("big", big);
        s.setPopupDisplayEnabled(true, true, nullptr);
        s.setMouseDragSensitivity(big ? 260 : 200);
        s.setVelocityModeParameters(1.0, 1, 0.0, true, juce::ModifierKeys::ctrlModifier);
        s.onDragStart = [this] {
            if (!s.p) return;
            dragging = true;
            if (onBegin) onBegin();
            ed.beginEdit(*s.p);
        };
        s.onDragEnd = [this] {
            if (!s.p) return;
            dragging = false;
            ed.endEdit(*s.p);
        };
        s.onValueChange = [this] {
            if (syncing || !s.p) return;
            if (dragging) ed.setValue(*s.p, (float)s.getValue());
            else { if (onBegin) onBegin(); ed.setOnce(*s.p, (float)s.getValue()); }
        };
        s.onTypeIn = [this] { typeIn(); };
        addAndMakeVisible(s);
        setName(name);
        label.setText(name, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.setFont(juce::FontOptions(big ? 15.0f : 11.0f, juce::Font::bold));
        label.setInterceptsMouseClicks(false, false);
        label.setMinimumHorizontalScale(0.7f);
        addAndMakeVisible(label);
    }
    void setLabel(const juce::String &t) { label.setText(t, juce::dontSendNotification); }
    void bind(Param *p)
    {
        s.p = p;
        if (!p) { setEnabled(false); return; }
        setEnabled(true);
        syncing = true;
        const auto &m = p->meta;
        const bool isInt = m.type == sst::basic_blocks::params::ParamMetaData::INT;
        s.setRange(m.minVal, m.maxVal, isInt ? 1.0 : 0.0);
        s.setDoubleClickReturnValue(true, m.defaultVal);
        s.getProperties().set("bipolar", m.minVal < 0 && std::abs(m.minVal + m.maxVal) < 1e-4f);
        s.setValue(p->value, juce::dontSendNotification);
        s.setTooltip(juce::String(m.name));
        syncing = false;
        repaint();
    }
    void refresh() override
    {
        if (!s.p || dragging) return;
        if (std::abs(s.getValue() - s.p->value) > 1e-7)
        {
            syncing = true;
            s.setValue(s.p->value, juce::dontSendNotification);
            syncing = false;
        }
        label.setColour(juce::Label::textColourId, big ? INK : GREY);
    }
    void typeIn()
    {
        if (!s.p) return;
        auto *w = new juce::AlertWindow("Type a value", juce::String(s.p->meta.name), juce::MessageBoxIconType::NoIcon);
        w->addTextEditor("v", toStr(s.p->meta.valueToString(s.p->value)));
        w->addButton("OK", 1, juce::KeyPress(juce::KeyPress::returnKey));
        w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        w->enterModalState(true, juce::ModalCallbackFunction::create([sp = juce::Component::SafePointer<PKnob>(this), w](int r) {
            if (r != 1 || !sp || !sp->s.p) return;
            std::string err;
            const auto txt = w->getTextEditorContents("v").toStdString();
            if (auto v = sp->s.p->meta.valueFromString(txt, err))
            {
                const float c = juce::jlimit(sp->s.p->meta.minVal, sp->s.p->meta.maxVal, *v);
                if (sp->onBegin) sp->onBegin();
                sp->ed.setOnce(*sp->s.p, c);
                sp->refresh();
            }
            else sp->ed.status("Couldn't read \"" + juce::String(txt) + "\"" + (err.empty() ? juce::String() : ": " + juce::String(err)));
        }), true);
    }
    void resized() override
    {
        auto r = getLocalBounds();
        label.setBounds(r.removeFromBottom(big ? 20 : 15));
        s.setBounds(r);
    }
};

// a drop-down for a stepped parameter: its items are the parameter's own names for each step
struct PCombo : juce::ComboBox, Bound
{
    Poltergeist2Editor &ed;
    Param *p{nullptr};
    bool syncing{false};
    std::function<void()> onPicked;
    explicit PCombo(Poltergeist2Editor &e) : ed(e)
    {
        onChange = [this] {
            if (syncing || !p || getSelectedId() == 0) return;
            ed.setOnce(*p, p->meta.minVal + (float)(getSelectedId() - 1));
            if (onPicked) onPicked();
        };
    }
    void bind(Param *pp)
    {
        p = pp;
        syncing = true;
        clear(juce::dontSendNotification);
        if (p)
            for (int v = (int)std::round(p->meta.minVal); v <= (int)std::round(p->meta.maxVal); ++v)
            {
                auto t = toStr(p->meta.valueToString((float)v));
                if (t.isEmpty()) t = juce::String(v);
                addItem(t, v - (int)std::round(p->meta.minVal) + 1);
            }
        syncing = false;
        refresh();
        setTooltip(p ? juce::String(p->meta.name) : juce::String());
    }
    void refresh() override
    {
        if (!p) return;
        syncing = true;
        setSelectedId((int)std::round(p->value - p->meta.minVal) + 1, juce::dontSendNotification);
        syncing = false;
    }
};

// an on / off button for a bool (or a 0 / 1 choice) parameter
struct PToggle : juce::ToggleButton, Bound
{
    Poltergeist2Editor &ed;
    Param *p{nullptr};
    std::function<void(bool)> onFlip;
    PToggle(Poltergeist2Editor &e, const juce::String &text) : juce::ToggleButton(text), ed(e)
    {
        setClickingTogglesState(false);
        onClick = [this] {
            if (!p) return;
            const bool now = !(p->value > 0.5f);
            ed.setOnce(*p, now ? 1.f : 0.f);
            setToggleState(now, juce::dontSendNotification);
            if (onFlip) onFlip(now);
        };
    }
    void bind(Param *pp) { p = pp; refresh(); setTooltip(p ? juce::String(p->meta.name) : juce::String()); }
    void refresh() override { if (p) setToggleState(p->value > 0.5f, juce::dontSendNotification); }
};

// =========================================================================================================
// the DAHDSR envelope view (Poltergeist's EnvView, drawn from the six engine parameters)
struct EnvView : juce::Component
{
    Param *e[6]{}; // delay attack hold decay sustain release
    juce::Colour colour;
    juce::String tag;
    bool on{true};
    void paint(juce::Graphics &g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour(CARD2);
        g.fillRoundedRectangle(r, 10);
        if (!e[0]) return;
        auto v = [&](int k) { return juce::jlimit(0.f, 1.f, e[k]->value); };
        const float S = v(4);
        float wd[6] = {v(0) > 0.001f ? v(0) + 0.04f : 0.f, v(1) + 0.04f, v(2) > 0.001f ? v(2) + 0.04f : 0.f, v(3) + 0.04f, 0.5f, v(5) + 0.04f};
        auto in = r.reduced(8, 7);
        in.removeFromBottom(10);
        for (int pass = 0; pass < 3; ++pass)
        {
            float total = 0;
            for (float w : wd) total += w;
            const float sc = in.getWidth() / total;
            const float minW[6] = {26, 12, 30, 12, 12, 12};
            for (int k = 0; k < 6; ++k)
                if (wd[k] > 0 && wd[k] * sc < minW[k]) wd[k] = minW[k] / sc;
        }
        float total = 0;
        for (float w : wd) total += w;
        const float sc = in.getWidth() / total;
        float xs[7];
        xs[0] = in.getX();
        for (int k = 0; k < 6; ++k) xs[k + 1] = xs[k] + wd[k] * sc;
        const float lv[7] = {0, 0, 1, 1, S, S, 0};
        auto Y = [&](float l) { return in.getBottom() - l * in.getHeight(); };
        juce::Path p;
        p.startNewSubPath(xs[0], Y(0));
        for (int k = 1; k < 7; ++k) p.lineTo(xs[k], Y(lv[k]));
        juce::Path fill = p;
        fill.lineTo(xs[6], Y(0));
        fill.closeSubPath();
        const auto c = on ? colour : GREY;
        g.setColour(c.withAlpha(0.15f));
        g.fillPath(fill);
        g.setColour(c);
        g.strokePath(p, juce::PathStrokeType(1.8f));
        g.setColour(GREY);
        g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        const char *nm[6] = {"DLY", "A", "HOLD", "D", "S", "R"};
        for (int k = 0; k < 6; ++k)
            if (wd[k] > 0) g.drawText(nm[k], juce::Rectangle<float>(xs[k], in.getBottom() + 1, xs[k + 1] - xs[k], 10), juce::Justification::centred);
        g.drawText(on ? tag : tag + " (OFF)", juce::Rectangle<float>(r.getRight() - 120, r.getY() + 3, 114, 10), juce::Justification::centredRight);
    }
};

// the output scope
struct Scope : juce::Component
{
    Poltergeist2Editor &ed;
    explicit Scope(Poltergeist2Editor &e) : ed(e) {}
    void paint(juce::Graphics &g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour(CARD2);
        g.fillRoundedRectangle(r, 12);
        juce::Path p;
        const int N = Poltergeist2Editor::kScope;
        for (int i = 0; i < N; ++i)
        {
            const float x = r.getX() + r.getWidth() * (float)i / (float)(N - 1);
            const float y = r.getCentreY() - juce::jlimit(-1.0f, 1.0f, ed.scopeSample(i)) * r.getHeight() * 0.45f;
            if (i) p.lineTo(x, y); else p.startNewSubPath(x, y);
        }
        g.setColour(GREEN.withAlpha(0.25f));
        g.strokePath(p, juce::PathStrokeType(4));
        g.setColour(GREEN);
        g.strokePath(p, juce::PathStrokeType(1.4f));
    }
};

// =========================================================================================================
// The ghost field: the six operators as ghosts. Drag a ghost onto a ghost BELOW it (a higher number) to make it
// HAUNT it (frequency modulation: the engine's matrix runs top to bottom). Drag a link's dot up / down for how
// hard; click a dot to open that link; double-click it to break it. The ring over a ghost = its feedback.
struct GhostField : juce::Component
{
    Poltergeist2Editor &ed;
    juce::Path body;
    juce::Point<float> pos[ss::numOps], dragPt;
    int selected{0}, selLink{-1}; // selLink = matrix position (or -1: a ghost is selected)
    int dragFrom{-1}, dragLink{-1}, hoverLink{-1};
    bool dragging{false};
    float dragStartVal{0};
    double phase{0};

    explicit GhostField(Poltergeist2Editor &e) : ed(e)
    {
        body = juce::Drawable::parseSVGPath("M16 31 C16 13 48 13 48 31 V48 Q45 54 42 49 Q39 54 36 49 Q33 54 30 49 Q27 54 24 49 Q21 54 18 49 Q16 52 16 48 Z");
    }
    ss::Patch &P() { return ed.patch; }
    static int posOf(int f, int t) { return (int)ss::MatrixIndex::positionForSourceTarget((size_t)f, (size_t)t); }
    bool linked(int f, int t) { return f < t && P().matrixNodes[(size_t)posOf(f, t)].active.value > 0.5f; }
    float depth(int f, int t) { return linked(f, t) ? P().matrixNodes[(size_t)posOf(f, t)].level.value : 0.f; }
    bool awake(int o) { return P().sourceNodes[(size_t)o].active.value > 0.5f; }
    float heard(int o)
    {
        auto &m = P().mixerNodes[(size_t)o];
        return m.active.value > 0.5f ? m.level.value : 0.f;
    }
    float fb(int o)
    {
        auto &s = P().selfNodes[(size_t)o];
        return s.active.value > 0.5f ? std::abs(s.fbLevel.value) : 0.f;
    }
    bool hauntsAny(int o)
    {
        for (int t = o + 1; t < (int)ss::numOps; ++t)
            if (linked(o, t) && depth(o, t) > 0.001f) return true;
        return false;
    }

    void resized() override
    {
        // top to bottom, the way the haunting flows: 1 on top, 2 / 3, 4 / 5, 6 at the bottom
        const float w = (float)getWidth(), h = (float)getHeight();
        static const float Pp[ss::numOps][2] = {{270, 70}, {104, 124}, {436, 124}, {104, 214}, {436, 214}, {270, 244}};
        for (int o = 0; o < (int)ss::numOps; ++o) pos[o] = {Pp[o][0] / 540.0f * w, Pp[o][1] / 310.0f * h};
    }
    juce::Point<float> handleFor(int f, int t) const
    {
        auto a = pos[f], b = pos[t];
        auto mid = (a + b) * 0.5f;
        auto dir = b - a;
        const float len = juce::jmax(1.0f, dir.getDistanceFromOrigin());
        juce::Point<float> perp(-dir.y / len, dir.x / len);
        return mid + perp * 22.0f;
    }
    void paint(juce::Graphics &g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour(CARD);
        g.fillRoundedRectangle(bounds, 18);
        g.setColour(PURPLE.withAlpha(0.25f));
        g.drawRoundedRectangle(bounds.reduced(0.5f), 18, 1);
        g.setColour(GREY);
        g.setFont(juce::FontOptions(11.0f));
        g.drawFittedText("Drag a ghost onto one BELOW it to make it HAUNT it (FM). Drag a link's dot up/down for how hard. Double-click a dot to break it.",
                         bounds.reduced(16, 8).removeFromTop(16).toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
        for (int f = 0; f < (int)ss::numOps; ++f)
            for (int t = f + 1; t < (int)ss::numOps; ++t)
            {
                if (!linked(f, t)) continue;
                const float a = juce::jlimit(0.f, 1.f, depth(f, t));
                auto h = handleFor(f, t);
                juce::Path p;
                p.startNewSubPath(pos[f]);
                p.quadraticTo(h * 2.0f - (pos[f] + pos[t]) * 0.5f, pos[t]);
                const float pulse = 0.6f + 0.4f * std::sin((float)phase * 3.0f + (float)(f * 4 + t));
                const auto fc = GHOST[f];
                g.setColour(fc.withAlpha(0.18f * pulse));
                g.strokePath(p, juce::PathStrokeType(4.0f + a * 16.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                g.setColour(fc.withAlpha(a > 0.001f ? 0.85f : 0.35f));
                g.strokePath(p, juce::PathStrokeType(1.5f + a * 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                // the arrow head: which way the haunting goes
                {
                    const auto dir = pos[t] - h;
                    const float len = juce::jmax(1.0f, dir.getDistanceFromOrigin());
                    const auto u = dir / len;
                    const auto tip = pos[t] - u * 34.0f;
                    juce::Path ah;
                    ah.addTriangle(tip, tip - u * 9.0f + juce::Point<float>(-u.y, u.x) * 5.0f, tip - u * 9.0f - juce::Point<float>(-u.y, u.x) * 5.0f);
                    g.setColour(fc.withAlpha(0.85f));
                    g.fillPath(ah);
                }
                const int li = posOf(f, t);
                g.setColour(BG);
                g.fillEllipse(juce::Rectangle<float>(20, 20).withCentre(h));
                g.setColour(selLink == li ? GREEN : hoverLink == li ? GREEN.withAlpha(0.7f) : fc);
                g.drawEllipse(juce::Rectangle<float>(20, 20).withCentre(h), selLink == li ? 3.0f : 2.0f);
                g.setColour(INK);
                g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
                g.drawText(juce::String(juce::roundToInt(a * 100)), juce::Rectangle<float>(26, 12).withCentre(h), juce::Justification::centred);
            }
        if (dragFrom >= 0 && dragging)
        {
            juce::Path p;
            p.startNewSubPath(pos[dragFrom]);
            p.lineTo(dragPt);
            const int to = ghostAt(dragPt);
            g.setColour((to >= 0 && to <= dragFrom ? RED : GREEN).withAlpha(0.5f));
            g.strokePath(p, juce::PathStrokeType(3, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        for (int o = 0; o < (int)ss::numOps; ++o) drawGhost(g, o);
    }
    void drawGhost(juce::Graphics &g, int o)
    {
        const bool on = awake(o);
        const float lvl = juce::jlimit(0.f, 1.f, ed.opLevel(o) * 1.6f);
        const float out = heard(o), f = fb(o);
        const float size = 56.0f + lvl * 7.0f;
        const float bob = on ? std::sin((float)phase * 2.0f + (float)o * 1.7f) * 4.0f : 0.f;
        auto c = pos[o].translated(0, bob);
        auto area = juce::Rectangle<float>(size, size).withCentre(c);
        const auto colr = on ? GHOST[o] : GHOST[o].withAlpha(0.3f);
        g.setColour(colr.withAlpha((0.10f + lvl * 0.35f) * (on ? 1.f : 0.4f)));
        g.fillEllipse(area.expanded(10 + lvl * 14));
        if (o == selected && selLink < 0)
        {
            g.setColour(GREEN.withAlpha(0.8f));
            g.drawEllipse(area.expanded(6), 2.0f);
        }
        juce::Path p = body;
        p.applyTransform(juce::AffineTransform::fromTargetPoints(
            16.0f, 13.0f, area.getX() + area.getWidth() * 0.18f, area.getY() + area.getHeight() * 0.12f, 48.0f, 13.0f,
            area.getRight() - area.getWidth() * 0.18f, area.getY() + area.getHeight() * 0.12f, 16.0f, 54.0f,
            area.getX() + area.getWidth() * 0.18f, area.getBottom() - area.getHeight() * 0.05f));
        g.setColour(colr);
        g.fillPath(p);
        g.setColour(BG);
        auto eye = [&](float ex) {
            if (on) g.fillRoundedRectangle(juce::Rectangle<float>(size * 0.09f, size * 0.15f).withCentre({ex, c.y - size * 0.04f}), 2);
            else g.fillRect(juce::Rectangle<float>(size * 0.12f, 2.5f).withCentre({ex, c.y - size * 0.02f})); // asleep: closed eyes
        };
        eye(c.x - size * 0.11f);
        eye(c.x + size * 0.11f);
        if (lvl > 0.6f) g.fillEllipse(juce::Rectangle<float>(size * 0.1f, size * 0.08f * lvl).withCentre({c.x, c.y + size * 0.12f}));
        g.setColour(INK);
        g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
        g.drawText("GHOST " + juce::String(o + 1), juce::Rectangle<float>(110, 14).withCentre({c.x, area.getBottom() + 8}), juce::Justification::centred);
        g.setFont(juce::FontOptions(10.0f));
        const bool you = on && out > 0.01f;
        g.setColour(you ? GREEN : GREY);
        g.drawText(!on ? "asleep" : you ? "you hear it" : hauntsAny(o) ? "haunts others" : "silent",
                   juce::Rectangle<float>(110, 12).withCentre({c.x, area.getBottom() + 20}), juce::Justification::centred);
        if (f > 0.01f && on)
        {
            g.setColour(GHOST[o].withAlpha(0.7f));
            g.drawEllipse(juce::Rectangle<float>(26, 12).withCentre({c.x, area.getY() - 2}), 1.5f + f * 2);
        }
    }
    int ghostAt(juce::Point<float> p) const
    {
        for (int o = 0; o < (int)ss::numOps; ++o)
            if (p.getDistanceFrom(pos[o]) < 34) return o;
        return -1;
    }
    int linkAt(juce::Point<float> p)
    {
        for (int f = 0; f < (int)ss::numOps; ++f)
            for (int t = f + 1; t < (int)ss::numOps; ++t)
                if (linked(f, t) && p.getDistanceFrom(handleFor(f, t)) < 13) return posOf(f, t);
        return -1;
    }
    ss::Param &linkLevel(int li) { return P().matrixNodes[(size_t)li].level; }
    void mouseMove(const juce::MouseEvent &e) override
    {
        const int l = linkAt(e.position);
        if (l != hoverLink) { hoverLink = l; repaint(); }
    }
    void mouseDown(const juce::MouseEvent &e) override
    {
        dragging = false;
        dragLink = linkAt(e.position);
        if (dragLink >= 0)
        {
            ed.beginEdit(linkLevel(dragLink));
            dragStartVal = linkLevel(dragLink).value;
            return;
        }
        dragFrom = ghostAt(e.position);
        dragPt = e.position;
    }
    void mouseDrag(const juce::MouseEvent &e) override
    {
        if (dragLink >= 0)
        {
            if (e.getDistanceFromDragStart() > 2) dragging = true;
            ed.setValue(linkLevel(dragLink), juce::jlimit(0.0f, 1.0f, dragStartVal - (float)e.getDistanceFromDragStartY() / 160.0f));
            repaint();
            return;
        }
        if (dragFrom >= 0)
        {
            dragging = e.getDistanceFromDragStart() > 6;
            dragPt = e.position;
            repaint();
        }
    }
    void mouseUp(const juce::MouseEvent &e) override
    {
        if (dragLink >= 0)
        {
            ed.endEdit(linkLevel(dragLink));
            if (!dragging) ed.selectLink((int)ss::MatrixIndex::sourceIndexAt((size_t)dragLink), (int)ss::MatrixIndex::targetIndexAt((size_t)dragLink));
            dragLink = -1;
            dragging = false;
            return;
        }
        if (dragFrom >= 0)
        {
            const int to = ghostAt(e.position);
            if (dragging && to >= 0 && to != dragFrom)
            {
                if (to < dragFrom)
                    ed.status("GHOST " + juce::String(dragFrom + 1) + " can only haunt the ghosts below it (a higher number). Try dragging GHOST " +
                              juce::String(to + 1) + " onto GHOST " + juce::String(dragFrom + 1) + ".");
                else
                {
                    auto &m = P().matrixNodes[(size_t)posOf(dragFrom, to)];
                    if (m.active.value < 0.5f) ed.setOnce(m.active, 1.f);
                    if (m.level.value < 0.01f) ed.setOnce(m.level, 0.5f);
                    for (int g2 : {dragFrom, to})
                        if (!awake(g2)) ed.setOnce(P().sourceNodes[(size_t)g2].active, 1.f);
                    ed.selectLink(dragFrom, to);
                    ed.status("GHOST " + juce::String(dragFrom + 1) + " haunts GHOST " + juce::String(to + 1) + ". Drag the dot up / down for how hard.");
                }
            }
            else if (!dragging)
                ed.selectGhost(dragFrom);
        }
        dragFrom = -1;
        dragging = false;
        repaint();
    }
    void mouseDoubleClick(const juce::MouseEvent &e) override
    {
        const int l = linkAt(e.position);
        if (l < 0) return;
        auto &m = P().matrixNodes[(size_t)l];
        ed.setOnce(m.active, 0.f);
        ed.status("Link broken.");
        if (selLink == l) ed.selectGhost((int)ss::MatrixIndex::sourceIndexAt((size_t)l));
        repaint();
    }
    void tick() { phase += 1.0 / 30.0; repaint(); }
};

// the keyboard: plays the engine through the editor's note queue (the state is a base so it exists before the component)
struct KeyStateHolder
{
    juce::MidiKeyboardState keyState;
};
struct NoteForwarder : juce::MidiKeyboardState::Listener
{
    UiNoteQueue &q;
    explicit NoteForwarder(UiNoteQueue &qq) : q(qq) {}
    void handleNoteOn(juce::MidiKeyboardState *, int, int note, float vel) override { q.push(UiNote{true, (int16_t)note, vel}); }
    void handleNoteOff(juce::MidiKeyboardState *, int, int note, float) override { q.push(UiNote{false, (int16_t)note, 0.f}); }
};
struct Keys : KeyStateHolder, juce::MidiKeyboardComponent
{
    NoteForwarder fwd;
    explicit Keys(UiNoteQueue &qq) : juce::MidiKeyboardComponent(keyState, horizontalKeyboard), fwd(qq) { keyState.addListener(&fwd); }
    ~Keys() override { keyState.allNotesOff(0); keyState.removeListener(&fwd); }
};


// =========================================================================================================
// MORPH: the blend board (Ghost Bass's idea): a sound on each corner, drag the puck to mix the real sounds
static juce::Colour cornerCol(int c)
{
    const juce::Colour C[Morph::N] = {GREEN, PINK, CYAN, AMBER};
    return C[(size_t)juce::jlimit(0, Morph::N - 1, c)];
}
static const char *cornerLetter(int c) { static const char *L[Morph::N] = {"A", "B", "C", "D"}; return L[juce::jlimit(0, Morph::N - 1, c)]; }

struct MorphBoard : juce::Component
{
    Poltergeist2Editor &ed;
    juce::Path body;
    bool dragging{false};
    explicit MorphBoard(Poltergeist2Editor &e) : ed(e)
    {
        body = juce::Drawable::parseSVGPath("M16 31 C16 13 48 13 48 31 V48 Q45 54 42 49 Q39 54 36 49 Q33 54 30 49 Q27 54 24 49 Q21 54 18 49 Q16 52 16 48 Z");
    }
    juce::Rectangle<float> pad() const { return getLocalBounds().toFloat().reduced(52, 56); }
    juce::Point<float> cornerPos(int c) const
    {
        auto r = pad();
        return {c % 2 ? r.getRight() : r.getX(), c / 2 ? r.getBottom() : r.getY()};
    }
    juce::Point<float> toBoard(float px, float py) const
    {
        auto r = pad();
        return {r.getX() + px * r.getWidth(), r.getY() + py * r.getHeight()};
    }
    juce::Point<float> puckPos() const { return toBoard(ed.morph->effX.load(), ed.morph->effY.load()); }
    juce::Point<float> centrePos() const { return toBoard(ed.morph->x.load(), ed.morph->y.load()); }
    void ghost(juce::Graphics &g, juce::Point<float> c, float size, juce::Colour col, bool eyesOpen)
    {
        auto area = juce::Rectangle<float>(size, size).withCentre(c);
        juce::Path p = body;
        p.applyTransform(juce::AffineTransform::fromTargetPoints(
            16.0f, 13.0f, area.getX() + area.getWidth() * 0.18f, area.getY() + area.getHeight() * 0.12f, 48.0f, 13.0f,
            area.getRight() - area.getWidth() * 0.18f, area.getY() + area.getHeight() * 0.12f, 16.0f, 54.0f,
            area.getX() + area.getWidth() * 0.18f, area.getBottom() - area.getHeight() * 0.05f));
        g.setColour(col);
        g.fillPath(p);
        g.setColour(BG);
        for (float ex : {c.x - size * 0.11f, c.x + size * 0.11f})
        {
            if (eyesOpen) g.fillRoundedRectangle(juce::Rectangle<float>(size * 0.09f, size * 0.15f).withCentre({ex, c.y - size * 0.04f}), 2);
            else g.fillRect(juce::Rectangle<float>(size * 0.12f, 2.5f).withCentre({ex, c.y - size * 0.02f}));
        }
    }
    void paint(juce::Graphics &g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour(CARD);
        g.fillRoundedRectangle(b, 18);
        g.setColour(PURPLE.withAlpha(0.25f));
        g.drawRoundedRectangle(b.reduced(0.5f), 18, 1);
        if (!ed.morph) return;
        auto &m = *ed.morph;
        const bool on = m.on.load();
        auto r = pad();
        g.setColour(CARD2);
        g.fillRoundedRectangle(r.expanded(14), 14);
        for (int k = 1; k < 4; ++k)
        {
            g.setColour(FG(k == 2 ? 0.10f : 0.05f));
            g.drawVerticalLine((int)(r.getX() + r.getWidth() * k / 4), r.getY(), r.getBottom());
            g.drawHorizontalLine((int)(r.getY() + r.getHeight() * k / 4), r.getX(), r.getRight());
        }
        float w[Morph::N]{};
        const bool any = m.weights(w);
        const auto pk = puckPos();
        for (int c = 0; c < Morph::N; ++c)
        {
            const int kd = m.kind[(size_t)c].load();
            const auto cp = cornerPos(c);
            const auto col = cornerCol(c);
            if (kd != Morph::EMPTY && any && w[c] > 0.001f)
            { // a beam from each sounding corner to the puck, as thick as its share
                g.setColour(col.withAlpha((on ? 0.55f : 0.2f) * (0.3f + 0.7f * w[c])));
                g.drawLine({cp, pk}, 1.5f + 9.0f * w[c]);
            }
            if (kd == Morph::EMPTY)
            {
                juce::Path d;
                d.addEllipse(juce::Rectangle<float>(44, 44).withCentre(cp));
                juce::Path dd;
                const float dash[]{4, 4};
                juce::PathStrokeType(1.6f).createDashedStroke(dd, d, dash, 2);
                g.setColour(col.withAlpha(0.6f));
                g.fillPath(dd);
                g.setFont(juce::FontOptions(18.0f, juce::Font::bold));
                g.drawText("+", juce::Rectangle<float>(44, 44).withCentre(cp), juce::Justification::centred);
            }
            else
            {
                g.setColour(col.withAlpha(0.15f + 0.35f * w[c]));
                g.fillEllipse(juce::Rectangle<float>(64, 64).withCentre(cp));
                ghost(g, cp, 46.0f, col, on);
            }
            g.setColour(INK);
            g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
            const float ty = c / 2 ? cp.y + 26 : cp.y - 40;
            g.drawText(juce::String(cornerLetter(c)) + (kd == Morph::EMPTY ? juce::String() : "  " + juce::String(juce::roundToInt(w[c] * 100)) + "%"),
                       juce::Rectangle<float>(90, 14).withCentre({cp.x, ty + 7}), juce::Justification::centred);
        }
        // ORBIT: its path (faint) and the centre you set (a ring); the puck rides the path while MORPH is on
        if (const int sh = m.orbit.load(); sh != Morph::O_OFF)
        {
            juce::Path op;
            for (int i = 0; i <= 160; ++i)
            {
                float ox, oy;
                Morph::orbitAt(sh, m.orbitSize.load(), (float)i / 160.f, m.x.load(), m.y.load(), ox, oy);
                const auto q = toBoard(ox, oy);
                if (i == 0) op.startNewSubPath(q); else op.lineTo(q);
            }
            const float dash[]{5, 5};
            juce::Path dd;
            juce::PathStrokeType(1.4f).createDashedStroke(dd, op, dash, 2);
            g.setColour(INK.withAlpha(0.35f));
            g.fillPath(dd);
            const auto cpos = centrePos();
            g.setColour(GREEN.withAlpha(0.8f));
            g.drawEllipse(juce::Rectangle<float>(18, 18).withCentre(cpos), 2.0f);
            g.drawLine(cpos.x - 5, cpos.y, cpos.x + 5, cpos.y, 1.5f);
            g.drawLine(cpos.x, cpos.y - 5, cpos.x, cpos.y + 5, 1.5f);
        }
        // the puck: a little ghost of the blend's colours
        juce::Colour mix = INK;
        if (any)
        {
            float rr = 0, gg = 0, bb = 0;
            for (int c = 0; c < Morph::N; ++c)
            {
                auto col = cornerCol(c);
                rr += w[c] * col.getFloatRed(); gg += w[c] * col.getFloatGreen(); bb += w[c] * col.getFloatBlue();
            }
            mix = juce::Colour::fromFloatRGBA(rr, gg, bb, 1.0f);
        }
        g.setColour(mix.withAlpha(0.25f));
        g.fillEllipse(juce::Rectangle<float>(52, 52).withCentre(pk));
        ghost(g, pk, 34.0f, on ? mix : GREY, on);
        g.setColour(dragging ? GREEN : INK);
        g.drawEllipse(juce::Rectangle<float>(40, 40).withCentre(pk), dragging ? 2.5f : 1.5f);
        if (!on)
        {
            g.setColour(GREY);
            g.setFont(juce::FontOptions(11.5f, juce::Font::bold));
            g.drawText("MORPH IS OFF: you hear THIS SOUND only. Turn it on to hear the blend.", b.reduced(16, 10).removeFromBottom(16), juce::Justification::centred);
        }
    }
    void setFrom(juce::Point<float> p, int type)
    { // the puck (or, while it orbits, the centre it orbits around)
        auto r = pad();
        ed.morphEdit(Morph::pX, juce::jlimit(0.f, 1.f, (p.x - r.getX()) / r.getWidth()), type);
        ed.morphEdit(Morph::pY, juce::jlimit(0.f, 1.f, (p.y - r.getY()) / r.getHeight()), type);
        repaint();
    }
    std::function<void(int)> onCornerClick;
    void mouseDown(const juce::MouseEvent &e) override
    {
        if (!ed.morph) return;
        for (int c = 0; c < Morph::N; ++c)
            if (e.position.getDistanceFrom(cornerPos(c)) < 26 && e.position.getDistanceFrom(puckPos()) > 20)
            {
                if (ed.morph->kind[(size_t)c].load() == Morph::EMPTY && onCornerClick) { onCornerClick(c); return; }
            }
        dragging = true;
        setFrom(e.position, 0);
        setFrom(e.position, 1);
    }
    void mouseDrag(const juce::MouseEvent &e) override { if (dragging) setFrom(e.position, 1); }
    void mouseUp(const juce::MouseEvent &e) override
    {
        if (!dragging) return;
        dragging = false;
        setFrom(e.position, 2);
        ed.markStateDirty();
        repaint();
    }
    void mouseDoubleClick(const juce::MouseEvent &e) override
    { // double-click a corner: jump the puck onto it (exactly that sound)
        for (int c = 0; c < Morph::N; ++c)
            if (e.position.getDistanceFrom(cornerPos(c)) < 30)
            {
                ed.morphSet(Morph::pX, (float)(c % 2));
                ed.morphSet(Morph::pY, (float)(c / 2));
                ed.markStateDirty();
                repaint();
                return;
            }
    }
};

// what the blend is doing: the output's spectrum (owner, 2026-11-07: "would like to see the frequencies")
struct Spectrum : juce::Component
{
    Poltergeist2Editor &ed;
    static constexpr int kOrder = 11, kN = 1 << kOrder;
    juce::dsp::FFT fft{kOrder};
    std::vector<float> buf = std::vector<float>((size_t)kN * 2, 0.f), smooth = std::vector<float>(256, -90.f);
    explicit Spectrum(Poltergeist2Editor &e) : ed(e) {}
    void update()
    {
        for (int i = 0; i < kN; ++i)
        {
            const float wv = 0.5f - 0.5f * std::cos(juce::MathConstants<float>::twoPi * (float)i / (float)(kN - 1));
            buf[(size_t)i] = ed.histSample(kN - 1 - i) * wv;
        }
        std::fill(buf.begin() + kN, buf.end(), 0.f);
        fft.performFrequencyOnlyForwardTransform(buf.data());
        const double sr = 48000.0;
        for (int x = 0; x < (int)smooth.size(); ++x)
        {
            const double f = 30.0 * std::pow(16000.0 / 30.0, (double)x / (double)(smooth.size() - 1));
            const int bin = juce::jlimit(1, kN / 2 - 1, (int)std::round(f / sr * kN));
            const float db = juce::Decibels::gainToDecibels(buf[(size_t)bin] / (float)kN * 4.0f, -90.0f);
            smooth[(size_t)x] = db > smooth[(size_t)x] ? db : smooth[(size_t)x] * 0.85f + db * 0.15f;
        }
        repaint();
    }
    void paint(juce::Graphics &g) override
    {
        auto r = getLocalBounds().toFloat();
        g.setColour(CARD2);
        g.fillRoundedRectangle(r, 10);
        auto in = r.reduced(8, 6);
        juce::Path p;
        for (int x = 0; x < (int)smooth.size(); ++x)
        {
            const float px = in.getX() + in.getWidth() * (float)x / (float)(smooth.size() - 1);
            const float py = in.getBottom() - juce::jlimit(0.f, 1.f, (smooth[(size_t)x] + 90.f) / 90.f) * in.getHeight();
            if (x == 0) p.startNewSubPath(px, py); else p.lineTo(px, py);
        }
        juce::Path f = p;
        f.lineTo(in.getRight(), in.getBottom());
        f.lineTo(in.getX(), in.getBottom());
        f.closeSubPath();
        g.setGradientFill(juce::ColourGradient(GREEN.withAlpha(0.35f), 0, in.getY(), PURPLE.withAlpha(0.08f), 0, in.getBottom(), false));
        g.fillPath(f);
        g.setColour(GREEN);
        g.strokePath(p, juce::PathStrokeType(1.4f));
        g.setColour(GREY);
        g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        for (double hz : {100.0, 1000.0, 10000.0})
        {
            const float px = in.getX() + in.getWidth() * (float)(std::log(hz / 30.0) / std::log(16000.0 / 30.0));
            g.setColour(FG(0.08f));
            g.drawVerticalLine((int)px, in.getY(), in.getBottom());
            g.setColour(GREY);
            g.drawText(hz >= 1000 ? juce::String((int)(hz / 1000)) + "k" : juce::String((int)hz), juce::Rectangle<float>(px + 2, in.getY(), 30, 10), juce::Justification::left);
        }
        g.drawText("WHAT YOU HEAR", r.reduced(10, 4), juce::Justification::topRight);
    }
};

// a knob on one of MORPH's own settings (not an engine parameter: it lives in the plugin's state)
struct AKnob : juce::Component
{
    juce::Slider s{juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox};
    juce::Label label;
    std::atomic<float> *v{nullptr};
    std::function<void()> onDone;
    std::function<juce::String(double)> fmt;
    AKnob(const juce::String &name, double lo, double hi, double def)
    {
        s.setRange(lo, hi, 0.0);
        s.setDoubleClickReturnValue(true, def);
        s.setPopupDisplayEnabled(true, true, nullptr);
        s.getProperties().set("bipolar", lo < 0 && std::abs(lo + hi) < 1e-6);
        s.textFromValueFunction = [this](double x) { return fmt ? fmt(x) : juce::String(x, 2); };
        s.onValueChange = [this] { if (v) v->store((float)s.getValue()); };
        s.onDragEnd = [this] { if (onDone) onDone(); };
        addAndMakeVisible(s);
        label.setText(name, juce::dontSendNotification);
        label.setJustificationType(juce::Justification::centred);
        label.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        label.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(label);
    }
    void bind(std::atomic<float> *a) { v = a; refresh(); }
    void refresh()
    {
        if (v && !s.isMouseButtonDown()) s.setValue(v->load(), juce::dontSendNotification);
        label.setColour(juce::Label::textColourId, GREY);
    }
    void resized() override
    {
        auto r = getLocalBounds();
        label.setBounds(r.removeFromBottom(15));
        s.setBounds(r);
    }
};

// =========================================================================================================
struct Poltergeist2Editor::Impl : juce::Component
{
    Poltergeist2Editor &ed;
    ss::Patch &P;
    LNF lnf;
    std::unique_ptr<ss::presets::PresetManager> pm;
    std::unique_ptr<juce::PropertiesFile> prefs;
    std::vector<Bound *> bound;
    juce::OwnedArray<juce::Component> owned;

    // header
    juce::TextButton prevBtn{"<"}, nextBtn{">"}, presetBtn, saveBtn{"SAVE"}, loadBtn{"LOAD"}, initBtn{"INIT"}, themeBtn;
    juce::OwnedArray<juce::TextButton> chips;
    juce::Label statusL, infoL;
    static constexpr int kPages = 4; // SPECTRAL FX: + the SPECTRAL page
    std::unique_ptr<juce::Component> spectralPanel;
    juce::TextButton pageBtns[kPages];
    int page{0};
    std::vector<juce::Component *> pageComps[kPages];
    std::vector<std::pair<juce::Rectangle<int>, juce::String>> cards[kPages];

    // ghosts page
    GhostField field{ed};
    Scope scope{ed};
    juce::Label panelTitle, panelRole;
    PCombo waveBox{ed}, modeBox{ed};
    PToggle awakeBtn{ed, "AWAKE"}, linkOnBtn{ed, "LINKED"}, driveBtn{ed, "DRIVE"}, envOnBtn{ed, "ENV ON"};
    juce::TextButton envTabs[3];
    int envMode{0}; // 0 HEARD (mixer), 1 FEEDBACK (self), 2 PITCH (source)
    EnvView envView;
    PKnob *opK[12]{};
    juce::Rectangle<int> panelArea, toneArea, pitchArea, driftArea, voiceArea, macroArea;
    PKnob *toneDrive{}, *pdAmt{}, *pdDec{}, *drRate{}, *drAmt{}, *vGlide{}, *vOct{}, *vUni{}, *vMaster{};
    PCombo *toneSat{}, *toneLp{}, *toneHp{}, *drShape{};
    juce::Label *toneLpL{}, *toneHpL{};
    PToggle *drSync{}, *vMono{};
    PKnob *macroK[ss::numMacros]{};
    juce::Label macroHint[ss::numMacros];
    Keys keys;

    // MAIN page
    EnvView mainEnv;

    // MORPH page
    MorphBoard board{ed};
    Spectrum spectrum{ed};
    juce::ToggleButton morphOn{"MORPH ON"};
    juce::ComboBox orbitBox, orbitRateBox;
    juce::Slider orbitSize{juce::Slider::LinearBar, juce::Slider::TextBoxLeft};
    juce::Label orbitL;
    juce::Label morphHelp;
    juce::TextButton cornerBtn[Morph::N];
    juce::Label cornerName[Morph::N];
    std::unique_ptr<AKnob> cornerVol[Morph::N], cornerPan[Morph::N];
    juce::Rectangle<int> cornerArea[Morph::N];
    std::unique_ptr<ss::presets::PresetManager> cornerPm; // loads into the corner engines (no host to notify)
    uint32_t lastMorphChange{0xffffffff};
    bool orbitSync{false};

    Impl(Poltergeist2Editor &e, UiNoteQueue &q) : ed(e), P(e.patch), keys(q)
    {
        setLookAndFeel(&lnf);
        juce::PropertiesFile::Options o;
        o.applicationName = "Poltergeist 2";
        o.folderName = "GhostBlock";
        o.filenameSuffix = ".settings";
        o.osxLibrarySubFolder = "Application Support";
        prefs = std::make_unique<juce::PropertiesFile>(o);
        applyTheme(th::fromInt(prefs->getIntValue("theme", 0)));

        pm = std::make_unique<ss::presets::PresetManager>(ed.clapHost);
        pm->onPresetLoaded = [this](const ss::presets::PresetManager::LoadedPreset &lp) {
            ed.dawState.main.recordPreset(lp.sessionKind(), lp.category, lp.sessionPath());
            ed.uiDirty = true;
            ed.status("Loaded \"" + juce::String(P.name) + "\"");
        };
        buildHeader();
        buildGhostsPage();
        buildMainPage();
        buildMorphPage();
        if (ed.spectral)
        { // SPECTRAL FX: the whole page is one component (src/ghostblock/spectral/spectral-panel.cpp)
            spectralPanel = spectral::makeSpectralPanel(*ed.spectral, {[this](uint32_t id, double v, int t) { ed.spectralEdit(id, v, t); },
                                                                      [this] { ed.markStateDirty(); }, [this](const juce::String &s) { ed.status(s); }});
            addAndMakeVisible(*spectralPanel);
            pageComps[3].push_back(spectralPanel.get());
        }
        setPage(0);
    }
    ~Impl() override
    {
        for (auto *c : getChildren()) c->setLookAndFeel(nullptr);
        setLookAndFeel(nullptr);
    }

    template <typename T> T *keep(T *c, int pg = -1)
    {
        owned.add(c);
        addAndMakeVisible(c);
        if (pg >= 0) pageComps[pg].push_back(c);
        if (auto *b = dynamic_cast<Bound *>(c)) bound.push_back(b);
        return c;
    }
    PKnob *knob(int pg, const juce::String &name, Param *p, bool big = false)
    {
        auto *k = keep(new PKnob(ed, name, big), pg);
        k->bind(p);
        return k;
    }
    PCombo *combo(int pg, Param *p)
    {
        auto *c = keep(new PCombo(ed), pg);
        c->bind(p);
        return c;
    }
    PToggle *toggle(int pg, const juce::String &t, Param *p)
    {
        auto *b = keep(new PToggle(ed, t), pg);
        b->bind(p);
        return b;
    }
    juce::Label *label(int pg, const juce::String &t, float size = 10.5f)
    {
        auto *l = keep(new juce::Label({}, t), pg);
        l->setFont(juce::FontOptions(size, juce::Font::bold));
        l->setColour(juce::Label::textColourId, GREY);
        l->setInterceptsMouseClicks(false, false);
        return l;
    }

    // ------------------------------------------------------------------------------------------- header
    void buildHeader()
    {
        for (auto *b : {&prevBtn, &nextBtn, &presetBtn, &saveBtn, &loadBtn, &initBtn, &themeBtn}) addAndMakeVisible(*b);
        prevBtn.setTooltip("Previous preset");
        nextBtn.setTooltip("Next preset");
        presetBtn.setTooltip("All presets by folder, and your saved sounds");
        prevBtn.onClick = [this] { stepPreset(-1); };
        nextBtn.onClick = [this] { stepPreset(1); };
        presetBtn.onClick = [this] { showPresetMenu(); };
        saveBtn.onClick = [this] { saveSound(); };
        loadBtn.onClick = [this] { loadMenu(); };
        initBtn.setTooltip("Start from the plain init sound");
        initBtn.onClick = [this] { pm->loadInit(P, ed.mainToAudio); };
        themeBtn.onClick = [this] { applyTheme(th::next(col::theme)); prefs->setValue("theme", th::toInt(col::theme)); prefs->saveIfNeeded(); };
        // the sound folders as chips: a click plays the next sound of that folder
        for (auto &[cat, names] : pm->factoryPatchNames)
        {
            // the sound folders you browse by ear (the MPE / tuning / template folders stay in the preset menu)
            if (names.empty() || cat == "MPE" || cat == "MTS-ESP-MPE" || cat == "Templates") continue;
            auto *b = chips.add(new juce::TextButton(juce::String(cat)));
            const auto c = cat;
            b->onClick = [this, c] { nextInCategory(c); };
            b->setTooltip("Next " + juce::String(c) + " sound");
            addAndMakeVisible(b);
        }
        statusL.setFont(juce::FontOptions(12.0f));
        statusL.setColour(juce::Label::textColourId, GREEN);
        statusL.setMinimumHorizontalScale(0.7f);
        addAndMakeVisible(statusL);
        infoL.setFont(juce::FontOptions(10.5f, juce::Font::bold));
        infoL.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(infoL);
        const char *names[kPages] = {"GHOSTS", "MAIN", "MORPH", "SPECTRAL"};
        const char *roles[kPages] = {"GHOSTS: the six FM ghosts, how they haunt each other, the tone, pitch drop, drift and voice",
                                     "MAIN: the main volume envelope, play mode, glide, bend, unison and the output stage",
                                     "MORPH: put a sound on each corner, drag the puck to blend the real sounds",
                                     "SPECTRAL: spectral effects after the synth (Spectral Corruptor's effects in four slots + WarpCore)"};
        for (int i = 0; i < kPages; ++i)
        {
            pageBtns[i].setButtonText(names[i]);
            pageBtns[i].setTooltip(roles[i]);
            pageBtns[i].onClick = [this, i] { setPage(i); };
            addAndMakeVisible(pageBtns[i]);
        }
    }
    void applyTheme(th::Theme t)
    {
        col::apply(t);
        lnf.applyColours();
        themeBtn.setButtonText(th::displayName(th::next(t)));
        themeBtn.setTooltip("Switch the look: BLACK -> PURPLE -> LIGHT (the button names the next look)");
        keys.setColour(juce::MidiKeyboardComponent::whiteNoteColourId, KEYWHITE);
        keys.setColour(juce::MidiKeyboardComponent::blackNoteColourId, KEYBLACK);
        keys.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, PURPLE.withAlpha(0.7f));
        keys.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, GREEN.withAlpha(0.3f));
        keys.setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId, LINE);
        keys.setColour(juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);
        statusL.setColour(juce::Label::textColourId, GREEN);
        infoL.setColour(juce::Label::textColourId, GREY);
        panelRole.setColour(juce::Label::textColourId, GREY);
        for (auto &h : macroHint) h.setColour(juce::Label::textColourId, GREY);
        for (auto *c : owned)
            if (auto *l = dynamic_cast<juce::Label *>(c)) l->setColour(juce::Label::textColourId, GREY);
        if (auto *k = macroK[0]) juce::ignoreUnused(k);
        refreshTitle();
        sendLookAndFeelChange();
        repaint();
    }

    // presets
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
        pm->loadFactoryPreset(P, ed.mainToAudio, v[(size_t)i].first, v[(size_t)i].second);
    }
    void stepPreset(int d)
    {
        const int i = factoryIndex();
        loadFactory(i < 0 ? (d > 0 ? 0 : -1) : i + d);
    }
    void nextInCategory(const std::string &cat)
    {
        const auto &v = pm->factoryPatchVector;
        const int cur = factoryIndex();
        int first = -1;
        for (int i = 0; i < (int)v.size(); ++i)
        {
            if (v[(size_t)i].first != cat) continue;
            if (first < 0) first = i;
            if (cur >= 0 && v[(size_t)cur].first == cat && i > cur) { loadFactory(i); return; }
        }
        if (first >= 0) loadFactory(first);
    }
    void showPresetMenu()
    {
        juce::PopupMenu m;
        m.addSectionHeader("SOUNDS");
        for (auto &[cat, names] : pm->factoryPatchNames)
        {
            juce::PopupMenu sub;
            for (auto &n : names)
            {
                const auto c = cat, f = n;
                const auto disp = juce::String(ss::presets::PresetManager::stripPresetExtension(n));
                sub.addItem(disp, true, disp == juce::String(P.name), [this, c, f] { pm->loadFactoryPreset(P, ed.mainToAudio, c, f); });
            }
            m.addSubMenu(juce::String(cat), sub);
        }
        m.addSeparator();
        m.addSubMenu("YOUR SOUNDS", userMenu());
        m.addItem("INIT (plain)", [this] { pm->loadInit(P, ed.mainToAudio); });
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presetBtn).withStandardItemHeight(22));
    }
    juce::PopupMenu userMenu()
    {
        juce::PopupMenu sub;
        pm->rescanUserPresets();
        for (auto &up : pm->userPatches)
        {
            const auto rel = up;
            sub.addItem(juce::String(ss::presets::PresetManager::stripPresetExtension(up.filename().u8string())),
                        [this, rel] { pm->loadUserPresetDirect(P, ed.mainToAudio, pm->userPatchesPath / rel); });
        }
        if (pm->userPatches.empty()) sub.addItem("No saved sounds yet (use SAVE)", false, false, [] {});
        return sub;
    }
    void loadMenu() { userMenu().showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&loadBtn)); }
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
#if USE_WCHAR_PRESET
            pm->saveUserPresetDirect(P, path.c_str());
#else
            pm->saveUserPresetDirect(P, path);
#endif
            P.dirty = false;
            const auto lp = pm->identifyUserPath(path);
            ed.dawState.main.recordPreset(lp.sessionKind(), lp.category, lp.sessionPath());
            ed.status("Saved \"" + n + "\". LOAD brings it back in any project.");
            refreshTitle();
        }), true);
    }

    // ------------------------------------------------------------------------------------------- GHOSTS page
    void buildGhostsPage()
    {
        addAndMakeVisible(field);
        addAndMakeVisible(scope);
        addAndMakeVisible(envView);
        addAndMakeVisible(panelTitle);
        addAndMakeVisible(panelRole);
        panelTitle.setFont(juce::FontOptions(20.0f, juce::Font::bold));
        panelRole.setFont(juce::FontOptions(11.0f));
        panelRole.setMinimumHorizontalScale(0.8f);
        for (auto *c : std::initializer_list<juce::Component *>{&field, &scope, &envView, &panelTitle, &panelRole})
            pageComps[0].push_back(c);
        for (auto *c : std::initializer_list<juce::Component *>{&waveBox, &modeBox, &awakeBtn, &linkOnBtn, &driveBtn, &envOnBtn})
        {
            addAndMakeVisible(c);
            pageComps[0].push_back(c);
        }
        for (auto *b : std::initializer_list<Bound *>{&waveBox, &modeBox, &awakeBtn, &linkOnBtn, &driveBtn, &envOnBtn}) bound.push_back(b);
        waveBox.setTooltip("The ghost's wave");
        modeBox.setTooltip("How this link haunts: phase (FM), ring or amplitude modulation");
        awakeBtn.onFlip = [this](bool) { field.repaint(); refreshTitle(); };
        linkOnBtn.onFlip = [this](bool) { field.repaint(); };
        envOnBtn.onFlip = [this](bool) { envView.repaint(); };
        const char *tabNames[3] = {"HEARD", "FEEDBACK", "PITCH"};
        const char *tabTips[3] = {"The envelope of how loud you hear this ghost", "The envelope of this ghost's feedback ring",
                                  "The envelope that bends this ghost's pitch (ENV AMT sets how far)"};
        for (int i = 0; i < 3; ++i)
        {
            envTabs[i].setButtonText(tabNames[i]);
            envTabs[i].setTooltip(tabTips[i]);
            envTabs[i].onClick = [this, i] { envMode = i; rebindPanel(); };
            addAndMakeVisible(envTabs[i]);
            pageComps[0].push_back(&envTabs[i]);
        }
        const char *names[12] = {"RATIO", "OCTAVE", "OUT", "PAN", "FEEDBACK", "ENV AMT", "DELAY", "ATTACK", "HOLD", "DECAY", "SUSTAIN", "RELEASE"};
        for (int i = 0; i < 12; ++i) opK[i] = knob(0, names[i], nullptr);
        // RATIO snaps to musical ratios (hold SHIFT for any ratio); the engine stores it as a power of two
        opK[0]->s.snap = [](double v) {
            if (field_isLinkMode) return v;
            static const double R[] = {1.0 / 32, 1.0 / 16, 1.0 / 8, 0.25, 0.5, 0.75, 1, 1.4142, 1.5, 2, 2.5, 3, 3.5, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 20, 24, 32};
            double best = 0, bd = 1e9;
            for (double r : R) { const double l = std::log2(r); if (std::abs(l - v) < bd) { bd = std::abs(l - v); best = l; } }
            return best;
        };
        // touching OUT / FEEDBACK wakes what is needed so you hear it at once
        opK[2]->onBegin = [this] {
            if (isLink()) return;
            wake(sel(), true, false);
        };
        opK[4]->onBegin = [this] {
            if (isLink()) return;
            wake(sel(), false, true);
        };

        // bottom row: TONE / PITCH DROP / DRIFT / VOICE (the engine's output stage, main tune envelope + LFO, voice)
        auto &out = P.output;
        auto &ft = P.fineTuneMod;
        toneSat = combo(0, &out.saturationType);
        toneDrive = knob(0, "DRIVE", &out.saturationDrive);
        toneLpL = label(0, "LOW PASS");
        toneLp = combo(0, &out.lowpass);
        toneHpL = label(0, "HIGH PASS");
        toneHp = combo(0, &out.highpass);
        toneSat->setTooltip("Saturation on the whole sound");
        pdAmt = knob(0, "AMOUNT", &ft.envCoarseDepth);
        pdDec = knob(0, "DECAY", &ft.decay);
        pdAmt->onBegin = [this] {
            // a pitch DROP falls back: with the envelope held at full sustain it would only shift the pitch
            auto &f2 = P.fineTuneMod;
            if (f2.sustain.value > 0.99f && std::abs(f2.envCoarseDepth.value) < 1e-4f)
            {
                ed.setOnce(f2.sustain, 0.f);
                if (f2.decay.value < 0.05f) ed.setOnce(f2.decay, 0.3f);
                if (f2.envPower.value < 0.5f) ed.setOnce(f2.envPower, 1.f);
                ed.uiDirty = true;
            }
        };
        drRate = knob(0, "RATE", &ft.lfoRate);
        drAmt = knob(0, "AMOUNT", &ft.lfoCoarseDepth);
        drSync = toggle(0, "SYNC", &ft.tempoSync);
        drShape = combo(0, &ft.lfoShape);
        drShape->setTooltip("The drift's shape");
        vGlide = knob(0, "GLIDE", &out.portaTime);
        vOct = knob(0, "OCTAVE", &out.octTranspose);
        vUni = knob(0, "UNISON", &out.unisonCount);
        vMaster = knob(0, "MASTER", &out.level);
        vMono = toggle(0, "MONO", &out.playMode);

        // the six macros (always on screen, as in Poltergeist)
        for (int m = 0; m < (int)ss::numMacros; ++m)
        {
            macroK[m] = knob(-1, "MACRO " + juce::String(m + 1), &P.macroNodes[(size_t)m].level, true);
            macroHint[m].setFont(juce::FontOptions(10.5f));
            macroHint[m].setJustificationType(juce::Justification::centredLeft);
            macroHint[m].setMinimumHorizontalScale(0.8f);
            addAndMakeVisible(macroHint[m]);
        }
        keys.setAvailableRange(24, 108);
        keys.setLowestVisibleKey(36);
        keys.setScrollButtonsVisible(false);
        addAndMakeVisible(keys);
        rebindPanel();
    }
    static inline bool field_isLinkMode = false;
    bool isLink() const { return field.selLink >= 0; }
    int sel() const { return field.selected; }
    void wake(int o, bool mixer, bool self)
    {
        auto &s = P.sourceNodes[(size_t)o];
        if (s.active.value < 0.5f) ed.setOnce(s.active, 1.f);
        if (mixer && P.mixerNodes[(size_t)o].active.value < 0.5f) ed.setOnce(P.mixerNodes[(size_t)o].active, 1.f);
        if (self && P.selfNodes[(size_t)o].active.value < 0.5f) ed.setOnce(P.selfNodes[(size_t)o].active, 1.f);
        ed.uiDirty = true;
    }
    template <typename N> void bindEnv(N &n, const juce::String &tag, juce::Colour c, Param *amt, const juce::String &amtName)
    {
        Param *e[6] = {&n.delay, &n.attack, &n.hold, &n.decay, &n.sustain, &n.release};
        for (int i = 0; i < 6; ++i) { opK[6 + i]->bind(e[i]); envView.e[i] = e[i]; }
        envView.tag = tag;
        envView.colour = c;
        envOnBtn.bind(&n.envPower);
        envView.on = n.envPower.value > 0.5f;
        opK[5]->bind(amt);
        opK[5]->setLabel(amtName);
    }
    void rebindPanel()
    {
        const bool link = isLink();
        field_isLinkMode = link;
        for (int i = 0; i < 3; ++i)
        {
            envTabs[i].setToggleState(!link && envMode == i, juce::dontSendNotification);
            envTabs[i].setVisible(page == 0 && !link);
        }
        waveBox.setVisible(page == 0 && !link);
        awakeBtn.setVisible(page == 0 && !link);
        modeBox.setVisible(page == 0 && link);
        linkOnBtn.setVisible(page == 0 && link);
        driveBtn.setVisible(page == 0 && link);
        for (int i = 3; i < 5; ++i) opK[i]->setVisible(page == 0);
        if (link)
        {
            const int li = field.selLink;
            auto &m = P.matrixNodes[(size_t)li];
            const int f = (int)ss::MatrixIndex::sourceIndexAt((size_t)li), t = (int)ss::MatrixIndex::targetIndexAt((size_t)li);
            modeBox.bind(&m.modulationMode);
            linkOnBtn.bind(&m.active);
            driveBtn.bind(&m.overdrive);
            opK[0]->bind(&m.level); opK[0]->setLabel("DEPTH");
            opK[1]->bind(&m.lfoToDepth); opK[1]->setLabel("LFO AMT");
            opK[3]->setVisible(false); opK[4]->setVisible(false);
            bindEnv(m, "HAUNT", GHOST[f], &m.envToLevel, "ENV AMT");
            // ENV AMT sits next to DEPTH / LFO AMT on a link (the knob slot after them)
            opK[2]->bind(&m.envToLevel); opK[2]->setLabel("ENV AMT"); opK[2]->setVisible(page == 0);
            opK[5]->bind(nullptr); opK[5]->setVisible(false);
            panelTitle.setText("GHOST " + juce::String(f + 1) + " > GHOST " + juce::String(t + 1), juce::dontSendNotification);
            panelRole.setText("How hard GHOST " + juce::String(f + 1) + " haunts GHOST " + juce::String(t + 1) + ", and how that moves over each note", juce::dontSendNotification);
        }
        else
        {
            const int o = sel();
            auto &s = P.sourceNodes[(size_t)o];
            auto &mx = P.mixerNodes[(size_t)o];
            auto &sf = P.selfNodes[(size_t)o];
            opK[2]->setVisible(page == 0);
            opK[5]->setVisible(page == 0);
            waveBox.bind(&s.waveForm);
            awakeBtn.bind(&s.active);
            opK[0]->bind(&s.ratio); opK[0]->setLabel("RATIO");
            opK[1]->bind(&s.octTranspose); opK[1]->setLabel("OCTAVE");
            opK[2]->bind(&mx.level); opK[2]->setLabel("OUT");
            opK[3]->bind(&mx.pan); opK[3]->setLabel("PAN");
            opK[4]->bind(&sf.fbLevel); opK[4]->setLabel("FEEDBACK");
            if (envMode == 0) bindEnv(mx, "HEARD", GHOST[o], &mx.envToLevel, "ENV > OUT");
            else if (envMode == 1) bindEnv(sf, "FEEDBACK", GHOST[o], &sf.envToFB, "ENV > FB");
            else bindEnv(s, "PITCH", CYAN, &s.envToRatio, "ENV > PITCH");
            panelTitle.setText("GHOST " + juce::String(o + 1), juce::dontSendNotification);
            panelRole.setText("OUT = how loud you hear it  -  RATIO = its pitch  -  drag it onto a ghost below to haunt it", juce::dontSendNotification);
        }
        refreshTitle();
        field.repaint();
        envView.repaint();
        resized();
    }
    void refreshTitle()
    {
        const int o = isLink() ? (int)ss::MatrixIndex::sourceIndexAt((size_t)field.selLink) : sel();
        panelTitle.setColour(juce::Label::textColourId, th::readableOn(GHOST[o], CARD, 4.5));
        presetBtn.setButtonText(juce::String(P.name) + (P.dirty ? " *" : ""));
    }

    // ------------------------------------------------------------------------------------------- MAIN page
    struct Cell { juce::Component *c; juce::Label *l; bool isKnob; };
    std::vector<Cell> mainCells[3];
    void addCell(int group, const juce::String &name, Param *p)
    {
        using MD = sst::basic_blocks::params::ParamMetaData;
        const auto &m = p->meta;
        const int steps = (int)std::round(m.maxVal - m.minVal);
        if (m.type == MD::BOOL)
        {
            mainCells[group].push_back({toggle(1, name, p), nullptr, false});
            return;
        }
        if (m.type == MD::INT && steps <= 12 && toStr(m.valueToString(m.minVal)).containsNonWhitespaceChars() &&
            !toStr(m.valueToString(m.minVal)).containsOnly("0123456789-. "))
        {
            mainCells[group].push_back({combo(1, p), label(1, name), false});
            return;
        }
        mainCells[group].push_back({knob(1, name, p), nullptr, true});
    }
    void buildMainPage()
    {
        addAndMakeVisible(mainEnv);
        pageComps[1].push_back(&mainEnv);
        auto &o = P.output;
        // AMP ENVELOPE
        addCell(0, "DELAY", &o.delay); addCell(0, "ATTACK", &o.attack); addCell(0, "HOLD", &o.hold);
        addCell(0, "DECAY", &o.decay); addCell(0, "SUSTAIN", &o.sustain); addCell(0, "RELEASE", &o.release);
        addCell(0, "VELOCITY", &o.velSensitivity);
        Param *e[6] = {&o.delay, &o.attack, &o.hold, &o.decay, &o.sustain, &o.release};
        for (int i = 0; i < 6; ++i) mainEnv.e[i] = e[i];
        mainEnv.tag = "VOLUME";
        // PLAY
        addCell(1, "PLAY MODE", &o.playMode); addCell(1, "VOICES", &o.polyLimit); addCell(1, "GLIDE", &o.portaTime);
        addCell(1, "GLIDE FROM", &o.portaContMode); addCell(1, "BEND UP", &o.bendUp); addCell(1, "BEND DOWN", &o.bendDown);
        addCell(1, "OCTAVE", &o.octTranspose); addCell(1, "TUNE", &o.fineTune);
        // OUTPUT
        addCell(2, "UNISON", &o.unisonCount); addCell(2, "SPREAD", &o.unisonSpread); addCell(2, "STEREO", &o.unisonPan);
        addCell(2, "SATURATION", &o.saturationType); addCell(2, "DRIVE", &o.saturationDrive); addCell(2, "LOW PASS", &o.lowpass);
        addCell(2, "HIGH PASS", &o.highpass); addCell(2, "BIT RATE", &o.bitRateAdjust); addCell(2, "BITS", &o.bitDepthAdjust);
        addCell(2, "PAN", &o.pan); addCell(2, "GAIN", &o.outputGain);
    }
    void layoutCells(int group, juce::Rectangle<int> area, int cellW, int knobH)
    {
        int x = area.getX(), y = area.getY();
        for (auto &c : mainCells[group])
        {
            if (x + cellW > area.getRight()) { x = area.getX(); y += knobH + 8; }
            if (c.isKnob) c.c->setBounds(x, y, cellW - 4, knobH);
            else if (c.l) { c.l->setBounds(x, y + 10, cellW - 4, 16); c.c->setBounds(x, y + 28, cellW - 6, 28); c.l->setJustificationType(juce::Justification::centred); }
            else c.c->setBounds(x + 2, y + 26, cellW - 8, 30);
            x += cellW;
        }
    }

    // ------------------------------------------------------------------------------------------- MORPH page
    void buildMorphPage()
    {
        cornerPm = std::make_unique<ss::presets::PresetManager>(Morph::quietHost());
        addAndMakeVisible(board);
        addAndMakeVisible(spectrum);
        pageComps[2].push_back(&board);
        pageComps[2].push_back(&spectrum);
        board.onCornerClick = [this](int c) { cornerMenu(c); };
        morphOn.setClickingTogglesState(false);
        morphOn.setTooltip("MORPH ON: you hear the blend of the corners. OFF: THIS SOUND alone (the corners rest)");
        morphOn.onClick = [this] {
            if (!ed.morph) return;
            ed.morphSet(Morph::pOn, ed.morph->on.load() ? 0.0 : 1.0);
            ed.markStateDirty();
            ed.status(ed.morph->on.load() ? "MORPH is on: drag the puck to blend the corners (double-click a corner = exactly that sound)."
                                          : "MORPH is off: you hear THIS SOUND only.");
            refreshMorph();
        };
        addAndMakeVisible(morphOn);
        pageComps[2].push_back(&morphOn);
        // ORBIT: the puck moves by itself around where you put it, locked to the song's bars
        orbitL.setText("ORBIT", juce::dontSendNotification);
        orbitL.setFont(juce::FontOptions(10.5f, juce::Font::bold));
        orbitL.setJustificationType(juce::Justification::centredRight);
        for (int i = 0; i < Morph::O_COUNT; ++i) orbitBox.addItem(Morph::orbitNames[i], i + 1);
        for (int i = 0; i < Morph::kRates; ++i) orbitRateBox.addItem(juce::String("EVERY ") + Morph::rateNames[i], i + 1);
        orbitBox.setTooltip("ORBIT: the puck moves by itself around where you put it (drag the puck = move the centre). Your DAW can automate it.");
        orbitRateBox.setTooltip("How long one trip round the orbit takes, locked to the song's bars");
        orbitBox.onChange = [this] {
            if (!ed.morph || orbitSync) return;
            ed.morphSet(Morph::pOrbit, orbitBox.getSelectedId() - 1);
            ed.markStateDirty();
            ed.status(orbitBox.getSelectedId() > 1 ? "ORBIT: the puck moves by itself while MORPH is on. Drag it to move the centre; SIZE = how far it goes."
                                                   : "ORBIT off: the puck stays where you put it.");
            if (orbitBox.getSelectedId() > 1 && !ed.morph->on.load()) ed.morphSet(Morph::pOn, 1.0);
            refreshMorph();
        };
        orbitRateBox.onChange = [this] {
            if (!ed.morph || orbitSync) return;
            ed.morphSet(Morph::pRate, orbitRateBox.getSelectedId() - 1);
            ed.markStateDirty();
        };
        orbitSize.setRange(0.0, 1.0, 0.0);
        orbitSize.setTextBoxStyle(juce::Slider::TextBoxLeft, true, 0, 0);
        orbitSize.textFromValueFunction = [](double v) { return "SIZE " + juce::String(juce::roundToInt(v * 100)) + "%"; };
        orbitSize.setTooltip("SIZE: how far the orbit takes the puck from its centre");
        orbitSize.onDragStart = [this] { if (ed.morph) ed.morphEdit(Morph::pSize, orbitSize.getValue(), 0); };
        orbitSize.onValueChange = [this] { if (ed.morph && !orbitSync) ed.morphEdit(Morph::pSize, orbitSize.getValue(), 1); };
        orbitSize.onDragEnd = [this] { if (ed.morph) { ed.morphEdit(Morph::pSize, orbitSize.getValue(), 2); ed.markStateDirty(); } };
        for (juce::Component *x : std::initializer_list<juce::Component *>{&orbitL, &orbitBox, &orbitRateBox, &orbitSize})
        {
            addAndMakeVisible(x);
            pageComps[2].push_back(x);
        }
        morphHelp.setText("Click a corner's sound to pick it. THIS SOUND = the one you shape on GHOSTS. Each corner plays its real sound; the puck mixes them.", juce::dontSendNotification);
        morphHelp.setFont(juce::FontOptions(11.0f));
        morphHelp.setJustificationType(juce::Justification::centredLeft);
        morphHelp.setMinimumHorizontalScale(0.85f);
        addAndMakeVisible(morphHelp);
        pageComps[2].push_back(&morphHelp);
        for (int c = 0; c < Morph::N; ++c)
        {
            cornerName[c].setFont(juce::FontOptions(11.0f, juce::Font::bold));
            cornerName[c].setText("CORNER " + juce::String(cornerLetter(c)), juce::dontSendNotification);
            cornerBtn[c].setTooltip("Pick this corner's sound. THIS SOUND = the one you shape on GHOSTS; each corner plays its real sound and the puck mixes them.");
            cornerBtn[c].onClick = [this, c] { cornerMenu(c); };
            cornerVol[c] = std::make_unique<AKnob>("VOLUME", -24.0, 6.0, 0.0);
            cornerVol[c]->fmt = [](double v) { return juce::String(v, 1) + " dB"; };
            cornerPan[c] = std::make_unique<AKnob>("PAN", -1.0, 1.0, 0.0);
            cornerPan[c]->fmt = [](double v) { return std::abs(v) < 0.01 ? juce::String("C") : (v < 0 ? "L" : "R") + juce::String(juce::roundToInt(std::abs(v) * 100)); };
            for (auto *k : {cornerVol[c].get(), cornerPan[c].get()}) k->onDone = [this] { ed.markStateDirty(); };
            if (ed.morph)
            {
                cornerVol[c]->bind(&ed.morph->volDb[(size_t)c]);
                cornerPan[c]->bind(&ed.morph->pan[(size_t)c]);
            }
            for (juce::Component *x : std::initializer_list<juce::Component *>{&cornerName[c], &cornerBtn[c], cornerVol[c].get(), cornerPan[c].get()})
            {
                addAndMakeVisible(x);
                pageComps[2].push_back(x);
            }
        }
    }
    void cornerMenu(int c)
    {
        if (!ed.morph) return;
        auto &m = *ed.morph;
        juce::PopupMenu menu;
        menu.addSectionHeader("CORNER " + juce::String(cornerLetter(c)));
        menu.addItem("THIS SOUND (the one you shape on GHOSTS)", [this, c] { ed.morph->setThisSound(c); afterCorner(c); });
        menu.addItem("A COPY OF THIS SOUND (keeps it as it is now)", [this, c] {
            ed.morph->setOwn(c, P.toState(), juce::String(P.name).toStdString() + " (copy)");
            afterCorner(c);
        });
        menu.addSeparator();
        for (auto &[cat, names] : cornerPm->factoryPatchNames)
        {
            juce::PopupMenu sub;
            for (auto &n : names)
            {
                const auto ct = cat, f = n;
                sub.addItem(juce::String(ss::presets::PresetManager::stripPresetExtension(n)), [this, c, ct, f] {
                    auto *e = ed.morph->engineFor(c);
                    cornerPm->loadFactoryPreset(e->patchMain, e->mainToAudio, ct, f);
                    e->reconcileWavetables();
                    ed.morph->names[(size_t)c] = std::string(e->patchMain.name);
                    ed.morph->kind[(size_t)c] = Morph::OWN;
                    ed.morph->changed++;
                    afterCorner(c);
                });
            }
            menu.addSubMenu(juce::String(cat), sub);
        }
        juce::PopupMenu user;
        cornerPm->rescanUserPresets();
        for (auto &up : cornerPm->userPatches)
        {
            const auto rel = up;
            user.addItem(juce::String(ss::presets::PresetManager::stripPresetExtension(up.filename().u8string())), [this, c, rel] {
                auto *e = ed.morph->engineFor(c);
                cornerPm->loadUserPresetDirect(e->patchMain, e->mainToAudio, cornerPm->userPatchesPath / rel);
                e->reconcileWavetables();
                ed.morph->names[(size_t)c] = std::string(e->patchMain.name);
                ed.morph->kind[(size_t)c] = Morph::OWN;
                ed.morph->changed++;
                afterCorner(c);
            });
        }
        if (cornerPm->userPatches.empty()) user.addItem("No saved sounds yet (SAVE one first)", false, false, [] {});
        menu.addSubMenu("YOUR SOUNDS", user);
        menu.addSeparator();
        menu.addItem("EMPTY", m.kind[(size_t)c].load() != Morph::EMPTY, false, [this, c] { ed.morph->clear(c); afterCorner(c); });
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&cornerBtn[c]).withStandardItemHeight(22));
    }
    void afterCorner(int c)
    {
        ed.markStateDirty();
        const auto nm = juce::String(ed.morph->names[(size_t)c]);
        ed.status(ed.morph->kind[(size_t)c].load() == Morph::EMPTY ? "Corner " + juce::String(cornerLetter(c)) + " is empty."
                  : "Corner " + juce::String(cornerLetter(c)) + ": " + nm + (ed.morph->on.load() ? "" : "  (turn MORPH ON to hear the blend)"));
        refreshMorph();
    }
    void refreshMorph()
    {
        if (!ed.morph) return;
        auto &m = *ed.morph;
        morphOn.setToggleState(m.on.load(), juce::dontSendNotification);
        orbitSync = true;
        orbitBox.setSelectedId(m.orbit.load() + 1, juce::dontSendNotification);
        orbitRateBox.setSelectedId(m.orbitRate.load() + 1, juce::dontSendNotification);
        if (!orbitSize.isMouseButtonDown()) orbitSize.setValue(m.orbitSize.load(), juce::dontSendNotification);
        orbitSync = false;
        orbitL.setColour(juce::Label::textColourId, GREY);
        orbitSize.setColour(juce::Slider::trackColourId, PURPLE.withAlpha(0.6f));
        orbitSize.setColour(juce::Slider::backgroundColourId, CARD2);
        orbitSize.setColour(juce::Slider::textBoxTextColourId, INK);
        morphHelp.setColour(juce::Label::textColourId, GREY);
        for (int c = 0; c < Morph::N; ++c)
        {
            const int k = m.kind[(size_t)c].load();
            cornerBtn[c].setButtonText(k == Morph::EMPTY ? juce::String("+ pick a sound") : k == Morph::THIS_SOUND ? juce::String("THIS SOUND") : juce::String(m.names[(size_t)c]));
            cornerName[c].setText("CORNER " + juce::String(cornerLetter(c)) + (k == Morph::THIS_SOUND ? "  -  live: " + juce::String(P.name) : juce::String()), juce::dontSendNotification);
            cornerName[c].setColour(juce::Label::textColourId, th::readableOn(cornerCol(c), CARD, 4.5));
            cornerVol[c]->refresh();
            cornerPan[c]->refresh();
        }
        board.repaint();
        repaint();
    }

    // ------------------------------------------------------------------------------------------- pages / layout
    void setPage(int pg)
    {
        page = pg;
        for (int i = 0; i < kPages; ++i)
        {
            pageBtns[i].setToggleState(i == pg, juce::dontSendNotification);
            for (auto *c : pageComps[i]) c->setVisible(i == pg);
        }
        rebindPanel();
        repaint();
    }
    void paint(juce::Graphics &g) override
    {
        const auto &T = th::get(col::theme);
        g.setGradientFill(juce::ColourGradient(T.windowTop, 0, 0, T.windowBottom, 0, (float)H, false));
        g.fillRect(getLocalBounds());
        auto logo = juce::Rectangle<float>(18, 12, 46, 46);
        g.setColour(GREEN);
        g.drawEllipse(logo, 2.5f);
        g.setColour(INK);
        g.fillRoundedRectangle(logo.reduced(12, 10).withTrimmedBottom(4), 6);
        g.setColour(BG);
        g.fillRect(logo.getCentreX() - 6, logo.getCentreY() - 3, 4.0f, 6.0f);
        g.fillRect(logo.getCentreX() + 2, logo.getCentreY() - 3, 4.0f, 6.0f);
        g.setColour(INK);
        g.setFont(juce::FontOptions(28.0f, juce::Font::bold));
        g.drawText("POLTERGEIST", 74, 8, 186, 32, juce::Justification::centredLeft);
        g.setColour(GREEN);
        g.drawText("2", 250, 8, 30, 32, juce::Justification::centredLeft);
        g.setColour(PURPLE);
        g.setFont(juce::FontOptions(10.5f, juce::Font::bold));
        g.drawText("FM SPIRIT SYNTH - GHOSTBLOCK", 76, 38, 180, 14, juce::Justification::centredLeft);
        g.setColour(GREY);
        g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
        g.drawText(versionText(), 246, 38, 52, 14, juce::Justification::centredLeft);
        if (page == 0)
        {
            card(g, panelArea, {});
            card(g, toneArea, "TONE");
            card(g, pitchArea, "PITCH DROP");
            card(g, driftArea, "DRIFT (LFO)");
            card(g, voiceArea, "VOICE");
        }
        for (auto &[r, t] : cards[page]) card(g, r, t);
        if (page == 2 && ed.morph)
        {
            float w[Morph::N]{};
            const bool any = ed.morph->weights(w);
            for (int c = 0; c < Morph::N; ++c)
            {
                const auto r = cornerArea[c];
                g.setColour(cornerCol(c));
                g.fillEllipse(juce::Rectangle<float>(28, 28).withCentre({(float)r.getX() + 22, (float)r.getCentreY()}));
                g.setColour(ON_ACCENT);
                g.setFont(juce::FontOptions(15.0f, juce::Font::bold));
                g.drawText(cornerLetter(c), juce::Rectangle<float>(28, 28).withCentre({(float)r.getX() + 22, (float)r.getCentreY()}), juce::Justification::centred);
                // how much of it you hear now
                auto bar = juce::Rectangle<float>((float)r.getRight() - 52, (float)r.getY() + 14, 36, (float)r.getHeight() - 28);
                g.setColour(CARD2);
                g.fillRoundedRectangle(bar, 6);
                const float v = any && ed.morph->kind[(size_t)c].load() != Morph::EMPTY ? w[c] : 0.f;
                g.setColour(cornerCol(c).withAlpha(ed.morph->on.load() ? 0.9f : 0.35f));
                g.fillRoundedRectangle(bar.withTrimmedTop(bar.getHeight() * (1.f - v)), 6);
                g.setColour(INK);
                g.setFont(juce::FontOptions(10.0f, juce::Font::bold));
                g.drawText(juce::String(juce::roundToInt(v * 100)) + "%", bar.translated(0, bar.getHeight() * 0.5f - 6).withHeight(12), juce::Justification::centred);
            }
        }
        card(g, macroArea, {});
    }
    void resized() override
    {
        prevBtn.setBounds(300, 16, 28, 34);
        presetBtn.setBounds(330, 16, 230, 34);
        nextBtn.setBounds(562, 16, 28, 34);
        saveBtn.setBounds(596, 16, 52, 34);
        loadBtn.setBounds(652, 16, 52, 34);
        initBtn.setBounds(708, 16, 48, 34);
        infoL.setBounds(764, 22, 258, 22);
        {
            int x = 18;
            for (auto *b : chips)
            {
                const int w = (int)juce::GlyphArrangement::getStringWidth(juce::FontOptions(12.0f, juce::Font::bold), b->getButtonText()) + 16;
                b->setBounds(x, 64, w, 26);
                x += w + 3;
            }
        }
        pageBtns[0].setBounds(18, 98, 70, 28);
        pageBtns[1].setBounds(92, 98, 56, 28);
        pageBtns[2].setBounds(152, 98, 66, 28);
        pageBtns[3].setBounds(222, 98, 86, 28); // SPECTRAL FX
        pageBtns[3].setVisible(spectralPanel != nullptr);
        if (spectralPanel) spectralPanel->setBounds(18, 134, 1004, 444);
        themeBtn.setBounds(1022 - 62, 99, 62, 26);
        statusL.setBounds(316, 102, 1022 - 62 - 8 - 316, 20);

        // ---- GHOSTS: the field, the ghost / link panel, the bottom cards
        field.setBounds(18, 134, 540, 310);
        panelArea = {570, 134, 452, 310};
        const int px = panelArea.getX(), py = panelArea.getY();
        panelTitle.setBounds(px + 16, py + 10, 176, 26);
        panelTitle.setMinimumHorizontalScale(0.6f);
        panelRole.setBounds(px + 16, py + 252, 424, 16);
        waveBox.setBounds(px + 16, py + 38, 96, 24);
        awakeBtn.setBounds(px + 116, py + 38, 74, 24);
        modeBox.setBounds(px + 16, py + 38, 120, 24);
        linkOnBtn.setBounds(px + 16, py + 274, 80, 26);
        driveBtn.setBounds(px + 100, py + 274, 70, 26);
        envView.setBounds(px + 196, py + 8, 244, 58);
        for (int i = 0; i < 12; ++i) opK[i]->setBounds(px + 8 + (i % 6) * 73, py + 68 + (i / 6) * 92, 72, 88);
        envTabs[0].setBounds(px + 14, py + 274, 64, 26);
        envTabs[1].setBounds(px + 80, py + 274, 82, 26);
        envTabs[2].setBounds(px + 164, py + 274, 58, 26);
        envOnBtn.setBounds(px + 226, py + 274, 66, 26);
        scope.setBounds(px + 296, py + 270, 144, 32);
        const int sy = 452, sh = 104;
        toneArea = {18, sy, 330, sh};
        pitchArea = {356, sy, 150, sh};
        driftArea = {514, sy, 238, sh};
        voiceArea = {760, sy, 262, sh};
        toneSat->setBounds(toneArea.getRight() - 92, sy + 4, 80, 22);
        toneDrive->setBounds(toneArea.getX() + 10, sy + 26, 72, 74);
        toneLpL->setBounds(toneArea.getX() + 92, sy + 34, 110, 14);
        toneLp->setBounds(toneArea.getX() + 92, sy + 50, 110, 26);
        toneHpL->setBounds(toneArea.getX() + 208, sy + 34, 110, 14);
        toneHp->setBounds(toneArea.getX() + 208, sy + 50, 110, 26);
        pdAmt->setBounds(pitchArea.getX() + 4, sy + 26, 72, 74);
        pdDec->setBounds(pitchArea.getX() + 74, sy + 26, 72, 74);
        drRate->setBounds(driftArea.getX() + 6, sy + 26, 72, 74);
        drAmt->setBounds(driftArea.getX() + 78, sy + 26, 72, 74);
        drSync->setBounds(driftArea.getX() + 156, sy + 30, 72, 26);
        drShape->setBounds(driftArea.getX() + 156, sy + 64, 72, 26);
        vMono->setBounds(voiceArea.getRight() - 72, sy + 4, 62, 22);
        PKnob *vk[4] = {vGlide, vOct, vUni, vMaster};
        for (int i = 0; i < 4; ++i) vk[i]->setBounds(voiceArea.getX() + 6 + i * 64, sy + 26, 62, 74);

        // ---- MAIN
        cards[1].clear();
        cards[1].push_back({{18, 134, 640, 300}, "VOLUME ENVELOPE (EVERY NOTE)"});
        cards[1].push_back({{668, 134, 354, 300}, "PLAY"});
        cards[1].push_back({{18, 442, 1004, 136}, "UNISON + OUTPUT"});
        mainEnv.setBounds(32, 158, 612, 120);
        layoutCells(0, {32, 290, 612, 140}, 87, 94);
        layoutCells(1, {682, 158, 330, 270}, 82, 94);
        layoutCells(2, {28, 460, 990, 112}, 90, 100);

        // ---- MORPH: the board (left), a card per corner (right), the spectrum under the board
        board.setBounds(18, 134, 470, 360);
        spectrum.setBounds(18, 500, 470, 78);
        cards[2].clear();
        morphOn.setBounds(500, 140, 110, 30);
        orbitL.setBounds(612, 142, 46, 26);
        orbitBox.setBounds(662, 142, 106, 26);
        orbitRateBox.setBounds(772, 142, 120, 26);
        orbitSize.setBounds(896, 142, 126, 26);
        morphHelp.setBounds(0, 0, 0, 0); // (its words are the corners' tooltips now)
        for (int c = 0; c < Morph::N; ++c)
        {
            cornerArea[c] = {500, 182 + c * 99, 522, 92};
            cards[2].push_back({cornerArea[c], {}});
            const auto r = cornerArea[c];
            cornerName[c].setBounds(r.getX() + 44, r.getY() + 10, 250, 20);
            cornerBtn[c].setBounds(r.getX() + 44, r.getY() + 36, 250, 32);
            if (cornerVol[c]) cornerVol[c]->setBounds(r.getX() + 310, r.getY() + 6, 72, 82);
            if (cornerPan[c]) cornerPan[c]->setBounds(r.getX() + 386, r.getY() + 6, 72, 82);
        }

        // ---- macros + keyboard (every page)
        macroArea = {18, 586, 1004, 92};
        for (int m = 0; m < (int)ss::numMacros; ++m)
        {
            const int mx = macroArea.getX() + 8 + m * 166;
            macroK[m]->setBounds(mx + 4, macroArea.getY() + 4, 70, 84);
            macroHint[m].setBounds(mx + 76, macroArea.getY() + 14, 86, 64);
        }
        keys.setBounds(18, 684, 1004, 54);
        keys.setKeyWidth(1004.0f / 36.0f);
    }

    void refreshMacros()
    {
        const auto usage = ss::computeMacroUsage(P);
        for (int m = 0; m < (int)ss::numMacros; ++m)
        {
            juce::String nm(P.macroNames[(size_t)m].data());
            macroK[m]->setLabel(nm.trim().isEmpty() ? "MACRO " + juce::String(m + 1) : nm.toUpperCase());
            const auto &u = usage[(size_t)m];
            juce::String h;
            if (u.empty()) h = "not wired in this sound";
            else
            {
                h = juce::String(u[0].nodeLabel) + ": " + juce::String(u[0].targetName);
                if (u.size() > 1) h << " (+" << (int)u.size() - 1 << " more)";
            }
            macroHint[m].setText(h, juce::dontSendNotification);
            macroHint[m].setColour(juce::Label::textColourId, GREY);
        }
    }
    void refreshAll()
    {
        for (auto *b : bound) b->refresh();
        envView.on = envOnBtn.p && envOnBtn.p->value > 0.5f;
        refreshMacros();
        refreshTitle();
        field.repaint();
        envView.repaint();
        mainEnv.colour = GREEN;
        mainEnv.on = P.output.envPower.value > 0.5f;
        mainEnv.repaint();
        refreshMorph();
    }
};

// =========================================================================================================
Poltergeist2Editor::Poltergeist2Editor(ss::Patch &pm, ss::WavetableHandoff &h, ss::Synth::audioToMainQueue_t &atou,
                                       ss::Synth::mainToAudioQueue_T &utoa, ss::Synth::audioOutputQueue_t &aor,
                                       std::atomic<bool> &ea, std::atomic<uint32_t> &fr, ss::Synth::DawStateMain &ds,
                                       const clap_host_t *host, UiNoteQueue &notes, Morph *mo, spectral::SpectralFx *sp)
    : patch(pm), morph(mo), spectral(sp), handoff(h), audioToMain(atou), mainToAudio(utoa), audioOutputRing(aor), editorActive(ea),
      uiForceRebuild(fr), dawState(ds), clapHost(host), noteQueue(notes)
{
    lastForceRebuild = uiForceRebuild.load();
    impl = std::make_unique<Impl>(*this, notes);
    addAndMakeVisible(*impl);
    impl->setBounds(0, 0, W, H);
    setSize(W, H);
    editorActive = true;
    audioOutputRing.subscribe();
    mainToAudio.push({ss::Synth::MainToAudioMsg::REQUEST_NON_PATCH_STATE});
    requestFlush();
    selectGhost(0);
    impl->refreshAll();
    status("Click a ghost to shape it. Drag one onto a ghost below it to make it haunt it.");
    startTimerHz(30);
}

Poltergeist2Editor::~Poltergeist2Editor()
{
    stopTimer();
    audioOutputRing.unsubscribe();
    editorActive = false;
    if (clapHost) clapHost->request_callback(clapHost);
    impl.reset();
}

void Poltergeist2Editor::paint(juce::Graphics &g) { g.fillAll(col::BG); }
void Poltergeist2Editor::resized()
{
    if (impl) impl->setTransform(juce::AffineTransform::scale((float)getWidth() / (float)W, (float)getHeight() / (float)H));
}

void Poltergeist2Editor::requestFlush()
{
    if (!clapHost) return;
    if (auto *ext = static_cast<const clap_host_params_t *>(clapHost->get_extension(clapHost, CLAP_EXT_PARAMS)))
        ext->request_flush(clapHost);
}
void Poltergeist2Editor::beginEdit(ss::Param &p) { mainToAudio.push({ss::Synth::MainToAudioMsg::BEGIN_EDIT, p.meta.id}); }
void Poltergeist2Editor::setValue(ss::Param &p, float v)
{
    p.value = v;
    patch.dirty = true;
    mainToAudio.push({ss::Synth::MainToAudioMsg::SET_PARAM, p.meta.id, v});
    requestFlush();
    uiDirty = true;
}
void Poltergeist2Editor::endEdit(ss::Param &p)
{
    mainToAudio.push({ss::Synth::MainToAudioMsg::END_EDIT, p.meta.id});
    requestFlush();
}
void Poltergeist2Editor::setOnce(ss::Param &p, float v)
{
    beginEdit(p);
    setValue(p, v);
    endEdit(p);
}
void Poltergeist2Editor::status(const juce::String &s)
{
    if (impl) impl->statusL.setText(s, juce::dontSendNotification);
}
void Poltergeist2Editor::selectGhost(int op)
{
    impl->field.selected = juce::jlimit(0, (int)ss::numOps - 1, op);
    impl->field.selLink = -1;
    impl->rebindPanel();
}
void Poltergeist2Editor::selectLink(int from, int to)
{
    if (from >= to) return;
    impl->field.selected = from;
    impl->field.selLink = (int)ss::MatrixIndex::positionForSourceTarget((size_t)from, (size_t)to);
    impl->rebindPanel();
}
void Poltergeist2Editor::showPage(int pg) { impl->setPage(juce::jlimit(0, Impl::kPages - 1, pg)); }
void Poltergeist2Editor::morphEdit(uint32_t id, double v, int type)
{
    if (!morph) return;
    morph->uiEdit(id, v, type);
    requestFlush();
}
void Poltergeist2Editor::spectralEdit(uint32_t id, double v, int type) // SPECTRAL FX
{
    if (!spectral) return;
    spectral->uiEdit(id, v, type);
    requestFlush();
    // a new effect / FFT size is built on the main thread: by the plugin when there is a host, here when there is none
    if (clapHost) clapHost->request_callback(clapHost);
    else spectral->serviceMainThread();
}
void Poltergeist2Editor::markStateDirty()
{
    if (!clapHost) return;
    if (auto *st = static_cast<const clap_host_state_t *>(clapHost->get_extension(clapHost, CLAP_EXT_STATE)))
        st->mark_dirty(clapHost);
}
float Poltergeist2Editor::histSample(int back) const
{
    back = juce::jlimit(0, kHist - 1, back);
    return scope[(size_t)(((scopePos - 1 - back) % kHist + kHist) % kHist)];
}
void Poltergeist2Editor::testMorphCorner(int c, int fi)
{
    if (!morph) return;
    if (fi < 0) morph->setOwn(c, patch.toState(), juce::String(patch.name).toStdString() + " (copy)");
    else
    {
        const auto &v = impl->cornerPm->factoryPatchVector;
        auto *e = morph->engineFor(c);
        impl->cornerPm->loadFactoryPreset(e->patchMain, e->mainToAudio, v[(size_t)fi].first, v[(size_t)fi].second);
        morph->names[(size_t)c] = std::string(e->patchMain.name);
        morph->kind[(size_t)c] = Morph::OWN;
        morph->changed++;
    }
    impl->refreshMorph();
}
void Poltergeist2Editor::testPuck(float x, float y)
{
    if (!morph) return;
    morphSet(Morph::pX, x);
    morphSet(Morph::pY, y);
    impl->refreshMorph();
}
void Poltergeist2Editor::refreshAll() { impl->refreshAll(); uiDirty = false; }
float Poltergeist2Editor::scopeSample(int i) const { return histSample(kScope - 1 - i); }

int Poltergeist2Editor::testFactoryCount() const { return (int)impl->pm->factoryPatchVector.size(); }
juce::String Poltergeist2Editor::testLoadFactory(int i)
{
    impl->loadFactory(i);
    timerCallback();
    return juce::String(patch.name);
}
void Poltergeist2Editor::testTheme(int t) { impl->applyTheme(th::fromInt(t)); refreshAll(); }
void Poltergeist2Editor::testEnvTab(int m) { impl->envMode = juce::jlimit(0, 2, m); impl->rebindPanel(); refreshAll(); }
juce::String Poltergeist2Editor::testPanelTitle() const { return impl->panelTitle.getText(); }
juce::String Poltergeist2Editor::testStatus() const { return impl->statusL.getText(); }
juce::Component *Poltergeist2Editor::testField() const { return &impl->field; }
// every engine parameter this editor binds to a control or sets itself (the lists in buildPanel / rebindPanel /
// buildMainPage / the ghost field, written out once so the ghost-machine editor can be checked against it)
std::vector<uint32_t> Poltergeist2Editor::testReachableParams() const
{
    std::vector<uint32_t> ids;
    auto add = [&](const ss::Param &p) { ids.push_back(p.meta.id); };
    auto env = [&](const auto &n) {
        add(n.delay); add(n.attack); add(n.hold); add(n.decay); add(n.sustain); add(n.release); add(n.envPower);
    };
    auto &P = patch;
    for (size_t o = 0; o < ss::numOps; ++o)
    {
        auto &s = P.sourceNodes[o];
        auto &mx = P.mixerNodes[o];
        auto &sf = P.selfNodes[o];
        add(s.waveForm); add(s.active); add(s.ratio); add(s.octTranspose); add(s.envToRatio); env(s);
        add(mx.level); add(mx.pan); add(mx.active); add(mx.envToLevel); env(mx);
        add(sf.fbLevel); add(sf.active); add(sf.envToFB); env(sf);
    }
    for (size_t li = 0; li < ss::matrixSize; ++li)
    {
        auto &m = P.matrixNodes[li];
        add(m.modulationMode); add(m.active); add(m.overdrive); add(m.level); add(m.lfoToDepth); add(m.envToLevel); env(m);
    }
    auto &o = P.output;
    auto &ft = P.fineTuneMod;
    add(o.saturationType); add(o.saturationDrive); add(o.lowpass); add(o.highpass);
    add(ft.envCoarseDepth); add(ft.decay); add(ft.sustain); add(ft.envPower);
    add(ft.lfoRate); add(ft.lfoCoarseDepth); add(ft.tempoSync); add(ft.lfoShape);
    add(o.portaTime); add(o.octTranspose); add(o.unisonCount); add(o.level); add(o.playMode);
    add(o.delay); add(o.attack); add(o.hold); add(o.decay); add(o.sustain); add(o.release); add(o.velSensitivity);
    add(o.polyLimit); add(o.portaContMode); add(o.bendUp); add(o.bendDown); add(o.fineTune);
    add(o.unisonSpread); add(o.unisonPan); add(o.bitRateAdjust); add(o.bitDepthAdjust); add(o.pan); add(o.outputGain);
    for (size_t m = 0; m < ss::numMacros; ++m) add(P.macroNodes[m].level);
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}
juce::Point<float> Poltergeist2Editor::testGhostPos(int g) const { return impl->field.pos[juce::jlimit(0, (int)ss::numOps - 1, g)]; }

void Poltergeist2Editor::drainAudio()
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
            if (aum->paramId >= 1 && aum->paramId <= ss::numOps)
                vu[aum->paramId - 1] = juce::jmax(aum->value, aum->value2);
        }
        else if (aum->action == A::UPDATE_VOICE_COUNT)
            impl->infoL.setText("VOICES " + juce::String((int)aum->paramId), juce::dontSendNotification);
        aum = audioToMain.pop();
    }
    // the scope: the newest samples of the left channel
    auto [L, R] = audioOutputRing.popall();
    juce::ignoreUnused(R);
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

void Poltergeist2Editor::timerCallback()
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
    if (uiDirty) refreshAll();
    for (auto &v : vu) v *= 0.85f; // fall back between meter messages
    impl->field.tick();
    impl->scope.repaint();
    if (morph && morph->changed.load() != impl->lastMorphChange)
    {
        impl->lastMorphChange = morph->changed.load();
        impl->refreshMorph();
    }
    if (impl->page == 2)
    {
        impl->spectrum.update();
        impl->board.repaint();
        impl->repaint(impl->cornerArea[0].getUnion(impl->cornerArea[Morph::N - 1]));
    }
}
} // namespace ghostblock::p2
