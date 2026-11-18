/*
 * Poltergeist 2 (GhostBlock) - the minimal SPECTRAL page (see spectral-panel.h). Plain JUCE controls in the editor's
 * look: master row, four slot rows (effect, on, mix, the effect's real parameters), the WarpCore row.
 */
#include "ghostblock/spectral/spectral-panel.h"

namespace ghostblock::p2::spectral
{
namespace
{
// the editor's colours, through the LookAndFeel it gave us (p2-editor.cpp LNF::applyColours)
juce::Colour cCard(juce::Component &c) { return c.findColour(juce::AlertWindow::backgroundColourId); }
juce::Colour cCell(juce::Component &c) { return c.findColour(juce::ComboBox::backgroundColourId); }
juce::Colour cLine(juce::Component &c) { return c.findColour(juce::Slider::backgroundColourId); }
juce::Colour cInk(juce::Component &c) { return c.findColour(juce::Label::textColourId); }
juce::Colour cGrey(juce::Component &c) { return c.findColour(juce::PopupMenu::headerTextColourId); }
juce::Colour cAccent(juce::Component &c) { return c.findColour(juce::Slider::trackColourId); }
juce::Colour cGreen(juce::Component &c) { return c.findColour(juce::Slider::thumbColourId); }

// a knob with its name above and its value below; works in 0..1 and asks `text` what that means
struct Knob : juce::Component
{
    juce::Slider s{juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox};
    juce::String name;
    std::function<juce::String(double)> text;
    std::function<void(double, int)> onEdit; // (value 0..1, 0 begin / 1 value / 2 end)
    double def{0};
    bool quiet{false};
    Knob()
    {
        s.setRange(0.0, 1.0);
        s.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        s.onDragStart = [this] { if (onEdit) onEdit(s.getValue(), 0); };
        s.onValueChange = [this] { if (onEdit && !quiet) onEdit(s.getValue(), 1); repaint(); };
        s.onDragEnd = [this] { if (onEdit) onEdit(s.getValue(), 2); };
        addAndMakeVisible(s);
    }
    void show(const juce::String &n, double value, double defaultValue, int steps)
    {
        name = n;
        def = defaultValue;
        s.setDoubleClickReturnValue(true, defaultValue);
        s.setRange(0.0, 1.0, steps > 1 ? 1.0 / (steps - 1) : 0.0);
        quiet = true;
        s.setValue(value, juce::dontSendNotification);
        quiet = false;
        setVisible(true);
        repaint();
    }
    void resized() override { s.setBounds(getLocalBounds().withTrimmedTop(12).withTrimmedBottom(13)); }
    void paint(juce::Graphics &g) override
    {
        g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
        g.setColour(cGrey(*this));
        g.drawFittedText(name.toUpperCase(), getLocalBounds().removeFromTop(12), juce::Justification::centred, 1, 0.6f);
        g.setColour(cInk(*this));
        g.setFont(juce::FontOptions(10.0f));
        g.drawFittedText(text ? text(s.getValue()) : juce::String(), getLocalBounds().removeFromBottom(13), juce::Justification::centred, 1, 0.6f);
    }
};

struct Panel : juce::Component, private juce::Timer
{
    SpectralFx &fx;
    PanelHost host;
    uint32_t seen{0xffffffffu};
    static constexpr int kRowH = 76, kTop = 44;

    juce::ToggleButton onBtn{"SPECTRAL ON"};
    juce::TextButton orderBtn;
    juce::ComboBox fftBox, windowBox, limitBox;
    Knob mixK, chainMixK, overlapK;
    juce::Label info;
    struct Row
    {
        juce::ToggleButton on{"ON"};
        juce::ComboBox type;
        Knob mix;
        Knob k[kHostParamsPerSlot];
        juce::TextButton page{"1/1"};
        int pageIndex{0};
    } rows[kSlots];
    juce::ToggleButton warpOn{"WARPCORE"};
    Knob warpK[kWarpParams];

    Panel(SpectralFx &f, PanelHost h) : fx(f), host(std::move(h))
    {
        auto edit = [this](uint32_t id, double v, int t) { if (host.edit) host.edit(id, v, t); };
        auto once = [edit](uint32_t id, double v) { edit(id, v, 0); edit(id, v, 1); edit(id, v, 2); };
        // ---- master row
        onBtn.setTooltip("SPECTRAL FX on / off. Off = the synth exactly as it is without it, no latency.");
        onBtn.onClick = [this, once] {
            once(SpectralFx::pOn, onBtn.getToggleState() ? 1 : 0);
            say();
        };
        addAndMakeVisible(onBtn);
        mixK.text = [](double v) { return juce::String(juce::roundToInt(v * 100)) + " %"; };
        mixK.onEdit = [edit](double v, int t) { edit(SpectralFx::pMix, v * 100.0, t); };
        chainMixK.text = mixK.text;
        chainMixK.onEdit = [edit](double v, int t) { edit(SpectralFx::pChainMix, v * 100.0, t); };
        overlapK.text = [](double v) { return juce::String(2.0 + v * 14.0, 2) + "x"; };
        overlapK.onEdit = [edit](double v, int t) { edit(SpectralFx::pOverlap, 2.0 + v * 14.0, t); };
        for (auto *k : {&mixK, &chainMixK, &overlapK}) addAndMakeVisible(*k);
        orderBtn.setTooltip("Which section comes first: the CHAIN (slots 1-4) or WARPCORE");
        orderBtn.onClick = [this, once] { once(SpectralFx::pOrder, fx.order() ? 0 : 1); };
        addAndMakeVisible(orderBtn);
        for (int i = 0; i < kFftSizes; ++i) fftBox.addItem("FFT " + juce::String(fftSizeAt(i)), i + 1);
        fftBox.setTooltip("FFT size of the chain's spectral effects: bigger = finer in pitch, slower in time, more latency (size - 1 samples)");
        fftBox.onChange = [this, once] { once(SpectralFx::pFft, fftBox.getSelectedId() - 1); say(); };
        for (int i = 0; i < kWindows; ++i) windowBox.addItem(windowName(i), i + 1);
        windowBox.setTooltip("FFT window");
        windowBox.onChange = [this, once] { once(SpectralFx::pWindow, windowBox.getSelectedId() - 1); };
        for (int i = 0; i < kLimits; ++i) limitBox.addItem("< " + juce::String((int)limitHz(i)) + " Hz", i + 1);
        limitBox.setTooltip("Spectral effects leave everything above this frequency alone");
        limitBox.onChange = [this, once] { once(SpectralFx::pLimit, limitBox.getSelectedId() - 1); };
        for (auto *b : {&fftBox, &windowBox, &limitBox}) addAndMakeVisible(*b);
        info.setFont(juce::FontOptions(10.5f, juce::Font::bold));
        info.setJustificationType(juce::Justification::centredRight);
        addAndMakeVisible(info);
        // ---- the slots
        for (int s = 0; s < kSlots; ++s)
        {
            auto &r = rows[s];
            r.on.setTooltip("This slot on / bypassed");
            r.on.onClick = [this, s, once] { once(SpectralFx::pSlotOn(s), rows[s].on.getToggleState() ? 1 : 0); };
            r.type.addItem("(empty)", 1);
            for (auto &cat : categories())
            {
                r.type.addSectionHeading(cat);
                for (auto &t : types())
                    if (t.category == cat) r.type.addItem(t.name, t.index + 1);
            }
            r.type.onChange = [this, s, once] {
                once(SpectralFx::pSlotType(s), rows[s].type.getSelectedId() - 1);
                rows[s].pageIndex = 0;
                refresh();
                say();
            };
            r.mix.text = [](double v) { return juce::String(juce::roundToInt(v * 100)) + " %"; };
            r.mix.onEdit = [s, edit](double v, int t) { edit(SpectralFx::pSlotMix(s), v * 100.0, t); };
            r.page.setTooltip("More parameters of this effect");
            r.page.onClick = [this, s] {
                const int pages = ((int)types()[(size_t)fx.slotType(s)].params.size() + kHostParamsPerSlot - 1) / kHostParamsPerSlot;
                rows[s].pageIndex = (rows[s].pageIndex + 1) % juce::jmax(1, pages);
                refresh();
            };
            addAndMakeVisible(r.on);
            addAndMakeVisible(r.type);
            addAndMakeVisible(r.mix);
            addAndMakeVisible(r.page);
            for (int k = 0; k < kHostParamsPerSlot; ++k)
            {
                r.k[k].text = [this, s, k](double v) {
                    const auto &d = types()[(size_t)fx.slotType(s)];
                    const int p = rows[s].pageIndex * kHostParamsPerSlot + k;
                    return p < (int)d.params.size() ? juce::String(d.params[(size_t)p].text(d.params[(size_t)p].fromNorm((float)v))) : juce::String();
                };
                r.k[k].onEdit = [this, s, k](double v, int t) {
                    const auto &d = types()[(size_t)fx.slotType(s)];
                    const int p = rows[s].pageIndex * kHostParamsPerSlot + k;
                    if (p >= (int)d.params.size()) return;
                    if (p < kHostParamsPerSlot) { if (host.edit) host.edit(SpectralFx::pSlotParam(s, p), v, t); }
                    else
                    { // an editor-only parameter (saved with the project, not automated)
                        fx.uiSlotValue(s, p, d.params[(size_t)p].fromNorm((float)v), t);
                        if (t == 2 && host.stateDirty) host.stateDirty();
                    }
                };
                addAndMakeVisible(r.k[k]);
            }
        }
        // ---- WarpCore
        warpOn.setTooltip(juce::String("WarpCore spectral inversion on / off (zero latency; running on ") + warpSimdName() + ")");
        warpOn.onClick = [this, once] { once(SpectralFx::pWarpOn, warpOn.getToggleState() ? 1 : 0); };
        warpOn.setEnabled(warpAvailable());
        addAndMakeVisible(warpOn);
        for (int k = 0; k < kWarpParams; ++k)
        {
            warpK[k].text = [k](double v) {
                const auto &d = warpParams()[(size_t)k];
                const float real = d.fromNorm((float)v);
                return d.id == "f_high" && real >= 20000.f ? juce::String("Full") : juce::String(d.text(real));
            };
            warpK[k].onEdit = [k, edit](double v, int t) { edit(SpectralFx::pWarpParam(k), warpParams()[(size_t)k].fromNorm((float)v), t); };
            addAndMakeVisible(warpK[k]);
        }
        setSize(1004, 444);
        refresh();
        startTimerHz(15);
    }
    void say()
    {
        if (!host.status) return;
        const auto want = fx.latencyWanted();
        host.status(!fx.on() ? juce::String("SPECTRAL is off: the synth plays exactly as without it.")
                    : want == 0 ? juce::String("SPECTRAL on. No latency (only time effects / WarpCore).")
                                : "SPECTRAL on. Spectral effects need " + juce::String(want) + " samples of latency; the DAW lines it up after it restarts the plugin.");
    }
    juce::Rectangle<int> rowArea(int i) const { return {0, kTop + i * (kRowH + 4), getWidth(), kRowH}; }
    void resized() override
    {
        onBtn.setBounds(8, 6, 118, 30);
        mixK.setBounds(132, 0, 60, 44);
        orderBtn.setBounds(198, 8, 150, 26);
        fftBox.setBounds(356, 8, 96, 26);
        windowBox.setBounds(456, 8, 126, 26);
        overlapK.setBounds(586, 0, 60, 44);
        limitBox.setBounds(650, 8, 110, 26);
        chainMixK.setBounds(764, 0, 60, 44);
        info.setBounds(828, 8, 170, 26);
        for (int s = 0; s < kSlots; ++s)
        {
            auto &r = rows[s];
            const auto a = rowArea(s);
            r.on.setBounds(34, a.getY() + 8, 46, 24);
            r.type.setBounds(84, a.getY() + 8, 168, 24);
            r.page.setBounds(34, a.getY() + 40, 46, 22);
            r.mix.setBounds(256, a.getY() + 4, 62, kRowH - 8);
            for (int k = 0; k < kHostParamsPerSlot; ++k) r.k[k].setBounds(326 + k * 84, a.getY() + 4, 80, kRowH - 8);
        }
        const auto w = rowArea(kSlots);
        warpOn.setBounds(8, w.getY() + 24, 104, 28);
        for (int k = 0; k < kWarpParams; ++k) warpK[k].setBounds(122 + k * 97, w.getY() + 4, 90, kRowH - 8);
    }
    void paint(juce::Graphics &g) override
    {
        const auto order = fx.runOrder();
        for (int i = 0; i <= kSlots; ++i)
        {
            const auto a = rowArea(i).toFloat();
            g.setColour(cCard(*this));
            g.fillRoundedRectangle(a, 14);
            g.setColour(cLine(*this));
            g.drawRoundedRectangle(a.reduced(0.5f), 14, 1);
            if (i == kSlots) continue;
            // the slot's number, and when it runs
            int at = 0;
            for (int k = 0; k < kSlots; ++k)
                if (order[(size_t)k] == i) at = k;
            const bool filled = fx.slotType(i) != 0;
            g.setColour(filled ? cAccent(*this) : cCell(*this));
            g.fillEllipse(a.getX() + 8, a.getY() + 9, 22, 22);
            g.setColour(cInk(*this));
            g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
            g.drawText(juce::String(i + 1), juce::Rectangle<float>(a.getX() + 8, a.getY() + 9, 22, 22), juce::Justification::centred);
            g.setColour(cGrey(*this));
            g.setFont(juce::FontOptions(9.5f, juce::Font::bold));
            const auto &d = types()[(size_t)fx.slotType(i)];
            juce::String note = !filled ? juce::String("pick an effect") : juce::String(d.category) + (d.domain == Domain::Time ? "  -  time effect: runs after the spectral ones" : "") + "  -  runs " + juce::String(at + 1) + (at == 0 ? "st" : at == 1 ? "nd" : at == 2 ? "rd" : "th");
            g.drawFittedText(note, juce::Rectangle<int>((int)a.getX() + 84, (int)a.getY() + 40, 168, 24), juce::Justification::centredLeft, 2, 0.7f);
            // level after this slot
            const float lv = juce::jlimit(0.f, 1.f, fx.slotLevel(i) * 1.5f);
            g.setColour(cCell(*this));
            g.fillRoundedRectangle(a.getX() + 12, a.getY() + 40, 14, 26, 3);
            g.setColour(cGreen(*this));
            g.fillRoundedRectangle(a.getX() + 12, a.getY() + 40 + 26 * (1 - lv), 14, 26 * lv, 3);
        }
    }
    void refresh()
    {
        auto set = [](juce::ComboBox &b, int id) { b.setSelectedId(id, juce::dontSendNotification); };
        onBtn.setToggleState(fx.on(), juce::dontSendNotification);
        mixK.show("Mix", fx.mix() / 100.0, 1.0, 0);
        chainMixK.show("Chain Mix", fx.chainMix() / 100.0, 1.0, 0);
        overlapK.show("Overlap", (fx.overlap() - 2.0) / 14.0, 2.0 / 14.0, 0);
        orderBtn.setButtonText(fx.order() ? "WARPCORE > CHAIN" : "CHAIN > WARPCORE");
        set(fftBox, fx.fftIndex() + 1);
        set(windowBox, fx.windowIndex() + 1);
        set(limitBox, fx.limitIndex() + 1);
        for (int s = 0; s < kSlots; ++s)
        {
            auto &r = rows[s];
            const auto &d = types()[(size_t)fx.slotType(s)];
            r.on.setToggleState(fx.slotOn(s), juce::dontSendNotification);
            set(r.type, fx.slotType(s) + 1);
            r.mix.show("Slot Mix", fx.slotMix(s) / 100.0, 1.0, 0);
            const int pages = juce::jmax(1, ((int)d.params.size() + kHostParamsPerSlot - 1) / kHostParamsPerSlot);
            r.pageIndex = juce::jmin(r.pageIndex, pages - 1);
            r.page.setVisible(pages > 1);
            r.page.setButtonText(juce::String(r.pageIndex + 1) + "/" + juce::String(pages));
            for (int k = 0; k < kHostParamsPerSlot; ++k)
            {
                const int p = r.pageIndex * kHostParamsPerSlot + k;
                if (p >= (int)d.params.size()) { r.k[k].setVisible(false); continue; }
                const auto &q = d.params[(size_t)p];
                const int steps = q.kind == Kind::Float ? 0 : (int)(q.max - q.min) + 1;
                r.k[k].show(q.name, q.toNorm(fx.slotValue(s, p)), q.toNorm(q.def), steps);
            }
        }
        warpOn.setToggleState(fx.warpOn(), juce::dontSendNotification);
        for (int k = 0; k < kWarpParams; ++k)
        {
            const auto &q = warpParams()[(size_t)k];
            warpK[k].show(q.name, q.toNorm(fx.warpValue(k)), q.toNorm(q.def), q.kind == Kind::Float ? 0 : (int)(q.max - q.min) + 1);
            warpK[k].setEnabled(warpAvailable());
        }
        showLatency();
        repaint();
    }
    void showLatency()
    {
        const auto told = fx.latency(), want = fx.latencyWanted();
        info.setText(want == told ? "LATENCY " + juce::String(told) + " smp" : "LATENCY " + juce::String(told) + " > " + juce::String(want) + " (restart)", juce::dontSendNotification);
        info.setColour(juce::Label::textColourId, cGrey(*this));
    }
    void visibilityChanged() override
    {
        if (isVisible()) { seen = fx.changeCount(); refresh(); }
    }
    void timerCallback() override
    {
        const auto c = fx.changeCount();
        if (c != seen)
        {
            seen = c;
            if (!juce::Component::isMouseButtonDownAnywhere()) refresh(); // (not under the user's hand)
            else seen = c - 1;
        }
        showLatency();
        if (isShowing()) repaint(0, kTop, 34, getHeight() - kTop);
    }
};
} // namespace

std::unique_ptr<juce::Component> makeSpectralPanel(SpectralFx &fx, PanelHost host) { return std::make_unique<Panel>(fx, std::move(host)); }
} // namespace ghostblock::p2::spectral
