// ================================================================================================
// build 11 MAIN PAGE - included by GhostEasyView.cpp inside namespace ghost::ui (after the layer pages).
//   Header     : logo, the sound picker (drop-down on the patch name, < > step in its folder), SAVE, ENGINE, About
//   ModeTabs   : BLEND  TERRAIN  MORPH  SUB  LOCK  SEQUENCER
//   BoardPage  : the BLEND BOARD - 2 to 4 sounds in the corners, a puck that blends them (every blendable Surge control)
// ================================================================================================

struct Logo : public juce::Component
{
    void paint(juce::Graphics &g) override
    {
        {
            // Brandon's painted ghost, with a red glow behind it
            const R lr(0.f, 0.f, 96.f, 86.f);
            g.setGradientFill(juce::ColourGradient(col::red.withAlpha(col::light ? 0.f : 0.35f), lr.getCentreX(), lr.getCentreY(), juce::Colours::transparentBlack,
                                                   lr.getCentreX() + 52.f, lr.getCentreY(), true));
            g.fillEllipse(lr.expanded(6.f));
            auto lg = art(col::light ? Art::dawnLogo : Art::logo);
            if (lg.isValid())
                g.drawImage(lg, lr.reduced(2.f), juce::RectanglePlacement::centred);
            else
                drawGhost(g, R(2, 2, 92, 78));
        }
        drawWordmark(g, 100.f, 0.f);
    }
};

// the sound picker's face: the patch name and its folder; a click opens the drop-down
struct PresetPill : public juce::Component
{
    EasyView::Impl &I;
    std::function<void()> onClick;
    explicit PresetPill(EasyView::Impl &i) : I(i) { setMouseCursor(juce::MouseCursor::PointingHandCursor); }
    void paint(juce::Graphics &g) override
    {
        R pill = getLocalBounds().toFloat().reduced(1.f);
        const float rad = pill.getHeight() * 0.5f;
        g.setColour(col::soot.withAlpha(0.85f));
        fillChamfer(g, pill.translated(0.f, 2.f), rad);
        g.setGradientFill(juce::ColourGradient(col::navy.brighter(0.1f), pill.getX(), pill.getY(), col::soot, pill.getX(), pill.getBottom(), false));
        fillChamfer(g, pill, rad);
        glowBorder(g, pill, rad, isMouseOver() ? 1.2f : 0.9f, 1.4f);
        auto &patch = I.synth->storage.getPatch();
        g.setColour(col::ink);
        g.setFont(F(13.f, 2));
        dt(g, juce::String(patch.name).toUpperCase(), pill.reduced(30, 0).withTrimmedBottom(22).toNearestInt(), juce::Justification::centredBottom);
        // the Ghost Bass category (from <ghostbass>) wins over Surge's folder / "Drag & Drop"
        juce::String sub = juce::String(I.eng->category.empty() ? patch.category : I.eng->category).toUpperCase();
        if (!I.eng->tags.empty())
            sub += " / " + juce::String(I.eng->tags).toUpperCase();
        g.setColour(col::amber);
        g.setFont(Mono(11.f));
        dt(g, sub, pill.reduced(30, 0).withTrimmedTop(32).toNearestInt(), juce::Justification::centredTop);
        tech(g, "PATCH", R(pill.getX() + 14.f, pill.getY() + 6.f, 60.f, 11.f), juce::Justification::centredLeft, col::red.withAlpha(0.85f), 9.f);
        // the drop-down arrow
        const float ax = pill.getRight() - 24.f, ay = pill.getCentreY();
        juce::Path ar;
        ar.startNewSubPath(ax - 6.f, ay - 3.f);
        ar.lineTo(ax, ay + 4.f);
        ar.lineTo(ax + 6.f, ay - 3.f);
        g.setColour(col::cyan);
        g.strokePath(ar, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    void mouseUp(const juce::MouseEvent &e) override
    {
        if (e.mouseWasClicked() && onClick)
            onClick();
    }
    void mouseEnter(const juce::MouseEvent &) override { repaint(); }
    void mouseExit(const juce::MouseEvent &) override { repaint(); }
};

struct Header : public juce::Component
{
    EasyView::Impl &I;
    Logo logo;
    PresetPill pill;
    GButton prevB{"<"}, nextB{">"}, saveB{"SAVE"}, engB{"ENGINE"}, aboutB, themeB;
    explicit Header(EasyView::Impl &i) : I(i), pill(i)
    {
        aboutB.text = juce::String("(i)  v") + GHOST_BASS_VERSION_STR;
        for (juce::Component *c : std::initializer_list<juce::Component *>{&logo, &pill, &prevB, &nextB, &saveB, &engB, &aboutB, &themeB})
            addAndMakeVisible(*c);
        themeB.big = true;
        themeB.text = col::light ? "DARK" : "LIGHT";
        themeB.setTooltip("Switch between the dark (NIGHT) and light (DAWN) look");
        themeB.onClick = [this] {
            if (I.toggleTheme)
                I.toggleTheme();
            themeB.text = col::light ? "DARK" : "LIGHT";
        };
        engB.tint = col::cyan;
        for (auto *b : {&prevB, &nextB, &saveB, &engB, &aboutB})
            b->big = true;
        prevB.setTooltip("the previous sound in this folder");
        nextB.setTooltip("the next sound in this folder");
        aboutB.setTooltip("About / Credits");
        // the one About / Credits item (GPLv3 section 5(d) legal notices)
        aboutB.onClick = [] {
            juce::AlertWindow::showMessageBoxAsync(
                juce::MessageBoxIconType::InfoIcon, "About / Credits",
                juce::String("Ghost Bass ") + GHOST_BASS_VERSION_STR +
                    " - GPL-3.0. Built from open-source projects; the full notices are in the LICENSE and CREDITS files that come with it.\n\n"
                    "Engine: Surge XT - Surge Synth Team - GPL-3.0 - github.com/surge-synthesizer/surge\n"
                    "SUB layer: Bombaz - Matthijs Hollemans - GPL-3.0 - github.com/hollance/bombaz\n"
                    "Plug-in framework: JUCE - GPL-3.0 - github.com/juce-framework/JUCE\n\n"
                    "The source code of this build is available from the same place you got the plug-in.");
        };
    }
    void resized() override
    {
        const int w = getWidth();
        logo.setBounds(0, 0, 340, 80);
        engB.setBounds(w - 12 - 112, 20, 112, 40);
        saveB.setBounds(engB.getX() - 8 - 92, 20, 92, 40);
        aboutB.setBounds(saveB.getX() - 8 - 120, 20, 120, 40);
        themeB.setBounds(aboutB.getX() - 8 - 76, 20, 76, 40);
        nextB.setBounds(themeB.getX() - 10 - 40, 20, 40, 40);
        prevB.setBounds(352, 20, 40, 40);
        pill.setBounds(prevB.getRight() + 6, 14, nextB.getX() - 6 - (prevB.getRight() + 6), 52);
    }
    void paint(juce::Graphics &g) override
    {
        const float w = (float)getWidth(), h = (float)getHeight();
        if (col::light)
        {
            // DAWN: the painting's sky shows through a soft paper haze; a fine indigo rule with a rose start
            g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.55f), 0, 0, juce::Colours::white.withAlpha(0.2f), 0, h, false));
            g.fillRect(getLocalBounds());
            g.setColour(col::line);
            g.fillRect(R(0.f, h - 3.f, w, 1.f));
            g.setColour(col::red);
            g.fillRect(R(0.f, h - 3.f, w * 0.30f, 2.f));
            return;
        }
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xff0d1830), 0, 0, col::bg, 0, h, false));
        g.fillRect(getLocalBounds());
        {
            auto bl = art(Art::bloom);
            if (bl.isValid())
            {
                g.setOpacity(0.7f);
                g.drawImage(bl, 0, 0, getWidth(), getHeight(), 0, 0, bl.getWidth(), bl.getHeight() * getHeight() / 760);
                g.setOpacity(1.f);
            }
        }
        // the city behind the controls, and the red glow of its streets
        g.setGradientFill(juce::ColourGradient(col::red.withAlpha(0.16f), w * 0.55f, h, juce::Colours::transparentBlack, w * 0.55f, h - 70.f, false));
        g.fillRect(getLocalBounds());
        drawSkyline(g, R(330.f, 18.f, w - 330.f, h - 21.f), 0x9e3779b9u, 0.85f);
        drawSkyline(g, R(0.f, 40.f, w, h - 43.f), 0x7f4a7c15u, 0.55f);
        g.setGradientFill(juce::ColourGradient(col::bg.withAlpha(0.95f), 0.f, 0.f, col::bg.withAlpha(0.0f), 330.f, 0.f, false));
        g.fillRect(R(0.f, 0.f, 360.f, h));
        // the deck edge: a ruled line with ticks, red from the logo, and a hazard block at the far right
        g.setColour(col::line);
        g.fillRect(R(0.f, h - 3.f, w, 1.f));
        g.setColour(col::red);
        g.fillRect(R(0.f, h - 3.f, w * 0.30f, 2.f));
        g.setColour(col::steel.withAlpha(0.6f));
        for (float x = w * 0.30f; x < w - 90.f; x += 8.f)
            g.fillRect(R(x, h - (std::fmod(x, 64.f) < 8.f ? 8.f : 5.f), 1.f, 3.f));
        hazard(g, R(w - 84.f, h - 6.f, 72.f, 5.f), col::amber.withAlpha(0.9f), col::soot, 4.f);
    }
};

// ------------------------------------------------------------------------------------------------
struct ModeTabs : public juce::Component
{
    EasyView::Impl &I;
    std::function<void(int)> onMode;
    ModeTabs(EasyView::Impl &i) : I(i) {}
    static constexpr int kTabs = EasyView::n_modes;
    R tab(int k) const { return R(k * (getWidth() / (float)kTabs), 0, getWidth() / (float)kTabs, (float)getHeight()).reduced(3, 1); }
    void paint(juce::Graphics &g) override
    {
        static const char *n[kTabs] = {"BLEND", "TERRAIN", "MORPH", "SUB", "LOCK", "SEQUENCER"};
        for (int k = 0; k < kTabs; ++k)
        {
            auto r = tab(k).reduced(1.5f);
            const float rad = 10.f;
            bool on = I.mode == k;
            g.setColour(col::soot.withAlpha(0.8f));
            fillChamfer(g, r.translated(0.f, 2.f), rad);
            if (on)
            {
                // the active tab: an illuminated Kaneda-red plate
                brushSwash(g, r.expanded(6.f, 3.f), col::red, k % 2);
            }
            else
            {
                g.setGradientFill(juce::ColourGradient(col::cell, r.getX(), r.getY(), col::panel.darker(0.3f), r.getX(), r.getBottom(), false));
                fillChamfer(g, r, rad);
                g.setColour(col::line.withAlpha(0.9f));
                drawChamfer(g, r.reduced(0.5f), rad, 1.1f);
            }
            if (!on)
                tech(g, "0" + juce::String(k + 1), R(r.getX() + 8.f, r.getY() + 2.f, 20.f, 11.f), juce::Justification::centredLeft, col::dim.withAlpha(0.6f),
                     8.5f);
            g.setColour(on ? col::onLit : col::ink.withAlpha(0.92f));
            g.setFont(on ? Brush(17.f) : F(11.f, 2));
            dt(g, n[k], r.reduced(10, 0), juce::Justification::centred);
        }
    }
    void mouseUp(const juce::MouseEvent &e) override
    {
        for (int k = 0; k < kTabs; ++k)
            if (tab(k).contains(e.position) && onMode)
                onMode(k);
    }
};

// ------------------------------------------------------------------------------------------------
// TEMPO: half | normal | double. The sequencer, the arp, TERRAIN and Surge's own synced LFOs and delays all follow the tempo they are given;
// this changes only that tempo (it is shown as the effective BPM next to the switch).
struct TempoBox : public juce::Component, public juce::SettableTooltipClient
{
    EasyView::Impl &I;
    explicit TempoBox(EasyView::Impl &i) : I(i)
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        setTooltip("TEMPO: play at the DAW's tempo (1x), half of it or double it. The sequencer, the arp, TERRAIN and the sound's synced LFOs and delays all follow it. "
                   "Saved with the project.");
    }
    static constexpr float kCaption = 46.f;
    R seg(int k) const
    {
        R r = getLocalBounds().toFloat().reduced(1.f);
        r.removeFromLeft(kCaption);
        const float w = r.getWidth() / 3.f;
        return R(r.getX() + w * (float)k, r.getY(), w, r.getHeight());
    }
    static juce::String bpmText(double bpm)
    {
        const double r = std::round(bpm * 10.0) / 10.0;
        return std::abs(r - std::round(r)) < 1e-6 ? juce::String((int)std::lround(r)) : juce::String(r, 1);
    }
    void paint(juce::Graphics &g) override
    {
        const int cur = I.synth->ghost->tempoMul.load();
        const double f[3] = {0.5, 1.0, 2.0};
        const double base = I.synth->time_data.tempo / f[juce::jlimit(0, 2, cur)]; // the DAW's tempo
        const juce::String names[3] = {juce::String(juce::CharPointer_UTF8("\xc2\xbd")) + "x", "1x", "2x"};
        R r = getLocalBounds().toFloat().reduced(1.f);
        const float rad = juce::jmin(10.f, r.getHeight() * 0.5f);
        g.setColour(col::soot);
        fillChamfer(g, r, rad);
        g.setColour(col::line.withAlpha(0.9f));
        drawChamfer(g, r, rad, 1.2f);
        tech(g, "TEMPO", R(r.getX(), r.getY() + 4.f, kCaption, 12.f), juce::Justification::centred, col::dim, 9.5f);
        tech(g, "BPM", R(r.getX(), r.getBottom() - 16.f, kCaption, 12.f), juce::Justification::centred, col::amber.withAlpha(0.7f), 9.5f);
        for (int k = 0; k < 3; ++k)
        {
            R s = seg(k);
            const bool on = cur == k;
            if (on)
            {
                brushSwash(g, s.expanded(4.f, 2.f), col::amber, 2);
                g.setColour(juce::Colours::transparentBlack);
                fillChamfer(g, s.reduced(2.f), juce::jmax(2.f, rad - 2.f));
            }
            const juce::Colour c = on ? col::onLit : col::ink;
            g.setColour(c.withAlpha(on ? 0.85f : 0.65f));
            g.setFont(F(9.f, 2));
            dt(g, names[k], s.withTrimmedBottom(s.getHeight() * 0.5f).withTrimmedTop(2.f).toNearestInt(), juce::Justification::centred);
            g.setColour(c);
            g.setFont(F(14.f, 2));
            dt(g, bpmText(base * f[k]), s.withTrimmedTop(s.getHeight() * 0.45f).withTrimmedBottom(2.f).toNearestInt(), juce::Justification::centred);
        }
    }
    void mouseUp(const juce::MouseEvent &e) override
    {
        for (int k = 0; k < 3; ++k)
            if (seg(k).contains(e.position) && I.synth->ghost->tempoMul.load() != k)
            {
                I.synth->ghost->tempoMul.store(k);
                I.proc.ghostLayers->touch();
                repaint();
            }
    }
};

// ------------------------------------------------------------------------------------------------
// BLEND BOARD
static juce::Colour slotColour(int k)
{
    const juce::Colour c[4] = {col::violet, col::cyan, col::pink, col::amber};
    return c[juce::jlimit(0, 3, k)];
}

// one corner's slot: its letter, the sound's name and its live weight; click or right-click opens the picker
struct SlotChip : public juce::Component
{
    int k{0};
    juce::String name;
    float weight{0.f};
    bool filled{false};
    std::function<void(int)> onPick;
    explicit SlotChip(int kk) : k(kk) { setMouseCursor(juce::MouseCursor::PointingHandCursor); }
    void paint(juce::Graphics &g) override
    {
        R r = getLocalBounds().toFloat().reduced(1.5f);
        const float rad = 14.f;
        const auto c = slotColour(k);
        g.setGradientFill(juce::ColourGradient(col::panel.brighter(isMouseOver() ? 0.45f : 0.25f), r.getX(), r.getY(), col::bg, r.getX(), r.getBottom(), false));
        fillChamfer(g, r, rad);
        g.setColour(c.withAlpha(filled ? 0.9f : 0.5f));
        drawChamfer(g, r, rad, filled ? 1.8f : 1.2f);
        // the letter
        R dot = R(r.getHeight() - 14.f, r.getHeight() - 14.f).withCentre({r.getX() + r.getHeight() * 0.5f, r.getCentreY()});
        // the letter: a brush-painted capital in the slot's colour (a dab of paint behind it once filled)
        if (filled)
            brushSwash(g, dot.expanded(4.f, 0.f), c, 2);
        g.setColour(filled ? col::onLit : c.withAlpha(0.9f));
        g.setFont(Brush(dot.getHeight() * 0.78f));
        dt(g, juce::String::charToString((juce::juce_wchar)('A' + k)), dot.toNearestInt(), juce::Justification::centred);
        auto tb = r.withTrimmedLeft(r.getHeight() + 2.f).reduced(4, 2);
        g.setColour(filled ? col::ink : col::dim);
        g.setFont(F(9.5f, filled ? 2 : 1));
        dt(g, filled ? name.toUpperCase() : juce::String("PICK A SOUND"), tb.withTrimmedBottom(filled ? 18.f : 0.f).toNearestInt(), juce::Justification::centredLeft);
        if (filled)
        {
            g.setColour(c);
            g.setFont(Fr(13.f, 2));
            dt(g, juce::String(juce::roundToInt(weight * 100.f)) + "%", tb.withTrimmedTop(tb.getHeight() - 18.f).toNearestInt(), juce::Justification::centredLeft);
        }
    }
    void mouseUp(const juce::MouseEvent &e) override
    {
        if (e.mouseWasClicked() && onPick)
            onPick(k);
    }
    void mouseEnter(const juce::MouseEvent &) override { repaint(); }
    void mouseExit(const juce::MouseEvent &) override { repaint(); }
};

struct BoardPage;

// the board itself: the slots in its corners, the puck anywhere between them
struct BoardPad : public juce::Component
{
    BoardPage &page;
    SlotChip chip[4]{SlotChip(0), SlotChip(1), SlotChip(2), SlotChip(3)};
    explicit BoardPad(BoardPage &p);
    R inner() const { return getLocalBounds().toFloat(); }
    void resized() override
    {
        const int cw = 196, ch = 52, m = 8, w = getWidth(), h = getHeight();
        chip[0].setBounds(m, m, cw, ch);
        chip[1].setBounds(w - m - cw, m, cw, ch);
        chip[2].setBounds(m, h - m - ch, cw, ch);
        chip[3].setBounds(w - m - cw, h - m - ch, cw, ch);
    }
    juce::Point<float> puckPos() const;
    void paint(juce::Graphics &g) override;
    void set(const juce::MouseEvent &e);
    void mouseDown(const juce::MouseEvent &e) override { set(e); }
    void mouseDrag(const juce::MouseEvent &e) override { set(e); }
    void mouseDoubleClick(const juce::MouseEvent &) override;
};

struct BoardPage : public juce::Component, private juce::Timer
{
    EasyView::Impl &I;
    struct Slot
    {
        bool filled{false};
        std::string path, name;
    } slot[4];
    float px{0.5f}, py{0.5f};
    juce::String status;
    BoardPad pad;
    GButton centreB{"CENTRE"}, clearB{"CLEAR ALL"}, cleanB{"CLEAN BLEND"}, setsB{"BOARD SETS"};
    std::unique_ptr<LSlider> volS[4], panS[4];
    ghost::layers::Mixer &mx() { return I.proc.ghostLayers->mixer; }
    // the frequency view: what each sound (and the SUB) plays now, 120 bars from 30 Hz to 12 kHz, 0..1 high
    static constexpr int kBins = 120, kFftOrder = 11, kN = 1 << kFftOrder;
    float spec[5][kBins]{};
    juce::dsp::FFT fft{kFftOrder};
    juce::dsp::WindowingFunction<float> window{(size_t)kN, juce::dsp::WindowingFunction<float>::hann};
    std::array<float, 2 * kN> fftBuf{};
    static float binFreq(float t) { return 30.f * std::pow(12000.f / 30.f, t); } // t 0..1 along the bars
    void updateSpectra()
    {
        const float sr = std::max(8000.f, I.synth->storage.samplerate);
        const bool subOn = I.proc.ghostLayers->sub.on.load();
        for (int src = 0; src < 5; ++src)
        {
            const bool live = src < 4 ? mx().filled(src) : subOn;
            if (!live)
            {
                for (auto &v : spec[src])
                    v *= 0.7f;
                continue;
            }
            mx().snapshot(src, fftBuf.data());
            std::fill(fftBuf.begin() + kN, fftBuf.end(), 0.f);
            window.multiplyWithWindowingTable(fftBuf.data(), (size_t)kN);
            fft.performFrequencyOnlyForwardTransform(fftBuf.data());
            for (int b = 0; b < kBins; ++b)
            {
                const float f0 = binFreq((float)b / (float)kBins), f1 = binFreq((float)(b + 1) / (float)kBins);
                const int i0 = std::max(1, (int)(f0 / sr * (float)kN)), i1 = std::max(i0, std::min(kN / 2 - 1, (int)(f1 / sr * (float)kN)));
                float m = 0.f;
                for (int i = i0; i <= i1; ++i)
                    m = std::max(m, fftBuf[(size_t)i]);
                const float amp = m / ((float)kN * 0.25f);
                const float db = 20.f * std::log10(amp + 1e-9f);
                const float y = std::clamp((db + 84.f) / 66.f, 0.f, 1.f);
                spec[src][b] = std::max(y, spec[src][b] * 0.8f);
            }
        }
    }

    explicit BoardPage(EasyView::Impl &i) : I(i), pad(*this)
    {
        addAndMakeVisible(pad);
        addAndMakeVisible(centreB);
        addAndMakeVisible(clearB);
        addAndMakeVisible(cleanB);
        addAndMakeVisible(setsB);
        setsB.dropdown = true;
        setsB.setTooltip("ready-made boards: four sounds that blend well, levels matched");
        setsB.onClick = [this] { setsMenu(); };
        cleanB.setTooltip("when sounds fight in the same frequency band, the quieter one is ducked a little there (and, with the SUB on, they leave the lows to it)");
        cleanB.onClick = [this] {
            mx().clean = !mx().clean.load();
            refreshChips();
        };
        centreB.setTooltip("put the puck in the middle");
        clearB.setTooltip("empty every slot");
        centreB.onClick = [this] { setPuck(0.5f, 0.5f); };
        clearB.onClick = [this] { clearAll(); };
        for (int k = 0; k < 4; ++k)
        {
            pad.chip[k].onPick = [this](int s) { pickSlot(s); };
            volS[k] = std::make_unique<LSlider>("VOLUME", 0.f, 1.5f, 0.01f, [this, k] { return mx().vol[k].load(); },
                                                [this, k](float v) { mx().vol[k] = v; },
                                                [](float v) { return juce::String(juce::roundToInt(v * 100.f)) + "%"; });
            panS[k] = std::make_unique<LSlider>("PAN", -1.f, 1.f, 0.02f, [this, k] { return mx().pan[k].load(); },
                                                [this, k](float v) { mx().pan[k] = std::fabs(v) < 0.04f ? 0.f : v; },
                                                [](float v) {
                                                    if (std::fabs(v) < 0.005f)
                                                        return juce::String("C");
                                                    return juce::String(v < 0.f ? "L" : "R") + juce::String(juce::roundToInt(std::fabs(v) * 100.f));
                                                });
            addChildComponent(*volS[k]);
            addChildComponent(*panS[k]);
        }
        status = "Click (or right-click) a corner to pick its sound, then drag the puck between them.";
        adopt();
        startTimerHz(15);
    }
    int filledCount() const
    {
        int n = 0;
        for (auto &s : slot)
            n += s.filled ? 1 : 0;
        return n;
    }
    void weights(float *w) const
    {
        bool f[4];
        for (int k = 0; k < 4; ++k)
            f[k] = slot[k].filled;
        ghost::Morph::boardWeights(f, px, py, w);
    }
    // the slots, the puck and the levels come from the mixer, which holds what the host handed back for this instance (nothing else)
    void adopt()
    {
        for (int k = 0; k < 4; ++k)
            slot[k] = mx().filled(k) ? Slot{true, mx().slotPath(k), mx().slotName(k)} : Slot{};
        px = mx().puckX.load();
        py = mx().puckY.load();
        refreshChips();
    }
    void refreshChips()
    {
        float w[4];
        weights(w);
        for (int k = 0; k < 4; ++k)
        {
            pad.chip[k].filled = slot[k].filled;
            pad.chip[k].name = juce::String(slot[k].name);
            pad.chip[k].weight = w[k];
            pad.chip[k].repaint();
            volS[k]->setVisible(slot[k].filled);
            panS[k]->setVisible(slot[k].filled);
        }
        cleanB.toggled = mx().clean.load();
        cleanB.repaint();
        pad.repaint();
        repaint();
    }
    void timerCallback() override
    {
        // the project's state can arrive after the page was made: follow the mixer
        bool differs = false;
        for (int k = 0; k < 4; ++k)
            differs = differs || slot[k].filled != mx().filled(k) || (slot[k].filled && slot[k].path != mx().slotPath(k));
        if (differs)
            adopt();
        guardSays();
        if (isShowing())
        {
            updateSpectra();
            for (int k = 0; k < 4; ++k)
            {
                volS[k]->repaint();
                panS[k]->repaint();
            }
            refreshChips();
        }
    }
    // ---- BLEND GUARD: when two sounds cancel each other, say which and what was done (at most once a second) ----
    juce::String guardMsg;
    double guardAt{0};
    void guardSays()
    {
        auto &g = mx().guard;
        const float loss = g.lossDb.load();
        if (!mx().on.load() || !g.active.load() || loss > -3.f)
            return;
        const int a = g.pairA.load(), b = g.pairB.load();
        const int db = juce::roundToInt(-loss / 3.f) * 3;
        const juce::String who = a >= 0 && b >= 0 && a < 4 && b < 4
                                     ? juce::String::charToString((juce::juce_wchar)('A' + a)) + " + " + juce::String::charToString((juce::juce_wchar)('A' + b))
                                     : juce::String("Two sounds");
        const juce::String msg = "BLEND GUARD: " + who + " cancel each other here (-" + juce::String(db) + " dB). " +
                                 (g.flips.load() != 0 ? "Flipped one's phase, blend kept full." : "Holding the blend at full level.");
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        if (msg == guardMsg || now - guardAt < 1.0)
            return;
        guardMsg = msg;
        guardAt = now;
        status = msg;
        repaint();
    }
    // ---- the puck: sets how much of each sound plays ----
    void pushWeights()
    {
        float w[4];
        weights(w);
        for (int k = 0; k < 4; ++k)
            mx().weight[k] = slot[k].filled ? w[k] : 0.f;
        mx().puckX = px;
        mx().puckY = py;
        mx().on = filledCount() >= 2;
        I.proc.ghostLayers->touch();
    }
    void setPuck(float x, float y)
    {
        px = std::clamp(x, 0.f, 1.f);
        py = std::clamp(y, 0.f, 1.f);
        pushWeights();
        refreshChips();
    }
    // ---- the slots ----
    void pickSlot(int k)
    {
        EasyView::Impl::MenuOpts o;
        o.current = true;
        o.empty = slot[k].filled;
        o.title = std::string("SLOT ") + (char)('A' + k);
        I.openMenu(I.canvasRect(pad.chip[k]), o, [this, k](const EasyView::Impl::PickResult &r) { setSlot(k, r); });
    }
    // "CURRENT SOUND" becomes a file in the user folder, so the slot's own engine can play it
    bool materializeCurrent(std::string &path, std::string &name)
    {
        ghost::Blob b = I.current();
        name = I.synth->storage.getPatch().name;
        auto dir = I.synth->storage.userDataPath / "Board Sounds";
        std::error_code ec;
        fs::create_directories(dir, ec);
        auto f = dir / ((name.empty() ? std::string("sound") : name) + ".fxp");
        path = path_to_string(f);
        return ghost::dna::writeFxp(path, b, name);
    }
    void setSlot(int k, const EasyView::Impl::PickResult &r)
    {
        using PR = EasyView::Impl::PickResult;
        if (r.kind == PR::EMPTY)
        {
            mx().clearSlot(k);
            status = "Slot " + juce::String::charToString((juce::juce_wchar)('A' + k)) + " emptied.";
        }
        else
        {
            std::string path = r.path, name = r.name;
            if (r.kind == PR::CURRENT && !materializeCurrent(path, name))
            {
                status = "Could not keep the current sound.";
                repaint();
                return;
            }
            if (!mx().setSlot(k, path, name))
            {
                status = "Could not read " + juce::String(name) + ".";
                repaint();
                return;
            }
            status = filledCount() >= 1 ? "Drag the puck: it sets how much of each sound plays." : "";
        }
        adopt();
        pushWeights();
        if (filledCount() < 2)
            status = filledCount() == 1 ? "Pick a second sound to blend." : "Click (or right-click) a corner to pick its sound.";
        repaint();
    }
    // ---- BOARD SETS: four factory sounds at once, each corner's level set so all four play equally loud ----
    void setsMenu()
    {
        juce::PopupMenu m;
        m.addSectionHeader("BOARD SETS: four sounds that blend well");
        const auto &sets = ghost::boardSets();
        for (int i = 0; i < (int)sets.size(); ++i)
            m.addItem(1 + i, juce::String(sets[(size_t)i].name) + "   -   " + sets[(size_t)i].hint);
        juce::Component::SafePointer<BoardPage> safe(this);
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&setsB), [safe](int id) {
            if (safe != nullptr && id > 0)
                safe->applySet(id - 1);
        });
    }
    void applySet(int i)
    {
        const auto &sets = ghost::boardSets();
        if (i < 0 || i >= (int)sets.size())
            return;
        const auto &S = sets[(size_t)i];
        const auto root = I.synth->storage.datapath / "patches_factory";
        int ok = 0;
        for (int k = 0; k < 4; ++k)
        {
            const auto &c = S.corners[(size_t)k];
            const auto f = root / c.cat / (std::string(c.name) + ".fxp");
            mx().vol[k] = std::clamp(c.vol, 0.f, 1.5f);
            mx().pan[k] = 0.f;
            if (mx().setSlot(k, path_to_string(f), c.name))
                ++ok;
            else
                mx().clearSlot(k);
        }
        mx().clean = true;
        adopt();
        setPuck(0.5f, 0.5f);
        status = ok == 4 ? juce::String("BOARD SET ") + S.name + ": four sounds, levels matched. Drag the puck."
                         : juce::String("BOARD SET ") + S.name + ": only " + juce::String(ok) + " of 4 sounds were found.";
        repaint();
    }
    void clearAll()
    {
        for (int k = 0; k < 4; ++k)
        {
            mx().clearSlot(k);
            mx().vol[k] = 1.f;
            mx().pan[k] = 0.f;
        }
        mx().on = false;
        status = "Click (or right-click) a corner to pick its sound.";
        adopt();
        pushWeights();
    }
    // ---- layout ----
    static constexpr int kPadX = 16, kPadY = 56, kPadW = 556, kPadH = 366;
    static constexpr int kRowH = 62;
    void resized() override
    {
        pad.setBounds(kPadX, kPadY, kPadW, kPadH);
        const int x = kPadX + kPadW + 18, w = getWidth() - x - 18;
        const int by = kPadY + kPadH - 42;
        cleanB.setBounds(x, by - 46, w, 42);
        setsB.setBounds(x, 6, w, 40);
        clearB.setBounds(x + w / 2 + 4, by, w / 2 - 4, 42);
        centreB.setBounds(x, by, w / 2 - 4, 42);
        for (int k = 0; k < 4; ++k)
        {
            const int y = kPadY + 26 + k * kRowH + 24;
            volS[k]->setBounds(x, y, w / 2 - 6, 38);
            panS[k]->setBounds(x + w / 2 + 6, y, w / 2 - 6, 38);
        }
    }
    void paint(juce::Graphics &g) override
    {
        panelBg(g, getLocalBounds().toFloat(), 18.f);
        g.setColour(col::ink);
        brushTitle(g, "BLEND BOARD", 30.f, 8.f, 30.f);
        tech(g, "SECT-A // 4CH", R(30.f, 40.f, 160.f, 11.f), juce::Justification::centredLeft, col::red.withAlpha(0.85f), 9.f);
        g.setColour(col::dim);
        g.setFont(Fr(13.f, 1));
        dt(g, status.isNotEmpty() ? status : juce::String("2 to 4 sounds, each playing as it is: the puck sets how much of each you hear"),
           R(290, 14, (float)(kPadX + kPadW + 18) - 300.f, 30), juce::Justification::centredLeft);
        const int x = kPadX + kPadW + 18, w = getWidth() - x - 18;
        g.setColour(col::ink);
        g.setFont(Brush(17.f));
        dt(g, "MIX NOW", R((float)x, (float)kPadY - 2.f, (float)w, 24.f), juce::Justification::centredLeft);
        float wt[4];
        weights(wt);
        for (int k = 0; k < 4; ++k)
        {
            R row((float)x, (float)kPadY + 26.f + (float)k * (float)kRowH, (float)w, 24.f);
            const auto c = slotColour(k);
            g.setColour(slot[k].filled ? c : c.withAlpha(0.3f));
            g.setFont(Brush(18.f));
            dt(g, juce::String::charToString((juce::juce_wchar)('A' + k)), row.withWidth(22.f).toNearestInt(), juce::Justification::centred);
            g.setColour(slot[k].filled ? col::ink : col::dim);
            g.setFont(Fr(13.f, 1));
            dt(g, slot[k].filled ? juce::String(slot[k].name) : juce::String("empty"), row.withTrimmedLeft(26.f).withTrimmedRight(52.f).toNearestInt(),
               juce::Justification::centredLeft);
            g.setFont(Fr(13.f, 2));
            g.setColour(c);
            dt(g, juce::String(juce::roundToInt(wt[k] * 100.f)) + "%", row.removeFromRight(50.f).toNearestInt(), juce::Justification::centredRight);
        }
    }
};

BoardPad::BoardPad(BoardPage &p) : page(p)
{
    for (auto &c : chip)
        addAndMakeVisible(c);
}
juce::Point<float> BoardPad::puckPos() const { return {page.px * (float)getWidth(), page.py * (float)getHeight()}; }
void BoardPad::set(const juce::MouseEvent &e)
{
    page.setPuck(e.position.x / std::max(1.f, (float)getWidth()), e.position.y / std::max(1.f, (float)getHeight()));
}
void BoardPad::mouseDoubleClick(const juce::MouseEvent &) { page.setPuck(0.5f, 0.5f); }
void BoardPad::paint(juce::Graphics &g)
{
    auto r = inner();
    // the board: dark, with faint rings round the middle and a grid, like a spirit board
    if (col::light)
        g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.75f), r.getCentreX(), r.getCentreY(), col::cell.withAlpha(0.8f), r.getX(),
                                               r.getY(), true));
    else
        g.setGradientFill(juce::ColourGradient(col::card.darker(0.35f), r.getCentreX(), r.getCentreY(), col::soot, r.getX(), r.getY(), true));
    fillChamfer(g, r, 18.f);
    // a targeting scope: fine grid, range rings, a crosshair with ticks
    g.setColour(col::line.withAlpha(0.10f));
    for (float x = r.getX() + 16.f; x < r.getRight(); x += 16.f)
        g.drawVerticalLine((int)x, r.getY() + 2.f, r.getBottom() - 2.f);
    for (float y = r.getY() + 16.f; y < r.getBottom(); y += 16.f)
        g.drawHorizontalLine((int)y, r.getX() + 2.f, r.getRight() - 2.f);
    g.setColour(col::steel.withAlpha(0.28f));
    for (int k = 1; k <= 4; ++k)
        g.drawEllipse(R(r.getWidth() * 0.2f * (float)k, r.getHeight() * 0.2f * (float)k).withCentre(r.getCentre()), k == 2 ? 1.4f : 1.f);
    g.drawHorizontalLine((int)r.getCentreY(), r.getX() + 10.f, r.getRight() - 10.f);
    g.drawVerticalLine((int)r.getCentreX(), r.getY() + 10.f, r.getBottom() - 10.f);
    g.setColour(col::steel.withAlpha(0.55f));
    for (float d = -r.getWidth() * 0.5f + 20.f; d < r.getWidth() * 0.5f - 10.f; d += 20.f)
        g.drawVerticalLine((int)(r.getCentreX() + d), r.getCentreY() - 3.f, r.getCentreY() + 3.f);
    for (float d = -r.getHeight() * 0.5f + 20.f; d < r.getHeight() * 0.5f - 10.f; d += 20.f)
        g.drawHorizontalLine((int)(r.getCentreY() + d), r.getCentreX() - 3.f, r.getCentreX() + 3.f);
    tech(g, "X " + juce::String(page.px, 2) + "  Y " + juce::String(page.py, 2), R(r.getCentreX() - 60.f, r.getY() + 6.f, 120.f, 12.f),
         juce::Justification::centred, col::cyan.withAlpha(0.7f), 9.f);
    glowBorder(g, r, 18.f, 0.9f, 1.4f);
    // the frequency view: every sound's spectrum in its own colour (the SUB in white), so a clash is something you can see
    {
        const float base = r.getBottom() - 26.f, hgt = r.getHeight() * 0.42f;
        g.saveState();
        g.reduceClipRegion(r.toNearestInt());
        for (int src = 4; src >= 0; --src)
        {
            bool any = false;
            for (float v : page.spec[src])
                any = any || v > 0.02f;
            if (!any)
                continue;
            const auto c = src < 4 ? slotColour(src) : col::ink;
            juce::Path p;
            p.startNewSubPath(r.getX(), base);
            for (int b = 0; b < BoardPage::kBins; ++b)
                p.lineTo(r.getX() + r.getWidth() * ((float)b + 0.5f) / (float)BoardPage::kBins, base - hgt * page.spec[src][b]);
            p.lineTo(r.getRight(), base);
            juce::Path fill(p);
            fill.closeSubPath();
            g.setColour(c.withAlpha(0.16f));
            g.fillPath(fill);
            g.setColour(c.withAlpha(0.75f));
            g.strokePath(p, juce::PathStrokeType(1.6f));
        }
        // the Hz marks, and the line where the SUB hands over to the ENGINE (GHOST LOCK's split) while the SUB is on
        g.setFont(Fr(10.f, 1));
        for (float f : {50.f, 100.f, 200.f, 500.f, 1000.f, 2000.f, 5000.f, 10000.f})
        {
            const float t = std::log(f / 30.f) / std::log(12000.f / 30.f);
            const float x = r.getX() + r.getWidth() * t;
            g.setColour(col::line.withAlpha(0.22f));
            g.drawVerticalLine((int)x, base - hgt, base);
            g.setColour(col::dim.withAlpha(0.75f));
            dt(g, f >= 1000.f ? juce::String((int)(f / 1000.f)) + "k" : juce::String((int)f), R(x - 18.f, base + 2.f, 36.f, 14.f).toNearestInt(),
               juce::Justification::centred);
        }
        auto &lay = *page.I.proc.ghostLayers;
        if (lay.sub.on.load() && lay.blend.stack.load())
        {
            const float t = std::log(std::max(31.f, lay.blend.split.load()) / 30.f) / std::log(12000.f / 30.f);
            const float x = r.getX() + r.getWidth() * t;
            g.setColour(col::cyan.withAlpha(0.8f));
            g.drawVerticalLine((int)x, base - hgt - 4.f, base);
            g.setFont(Fr(10.f, 2));
            dt(g, "SUB | ENGINE", R(x - 50.f, base - hgt - 18.f, 100.f, 14.f).toNearestInt(), juce::Justification::centred);
        }
        g.restoreState();
    }
    float wt[4];
    page.weights(wt);
    const auto pp = puckPos();
    // the corners' nodes, and a line from the puck to each sound (thicker = more of it)
    for (int k = 0; k < 4; ++k)
    {
        float cx, cy;
        ghost::Morph::cornerPos(k, cx, cy);
        const juce::Point<float> np(cx * r.getWidth(), cy * r.getHeight());
        const auto c = slotColour(k);
        const bool f = page.slot[k].filled;
        if (f)
        {
            g.setColour(c.withAlpha(0.15f + 0.6f * wt[k]));
            g.drawLine(juce::Line<float>(pp, np), 1.f + 7.f * wt[k]);
        }
        g.setColour(f ? c : c.withAlpha(0.3f));
        g.fillEllipse(R(f ? 16.f : 10.f, f ? 16.f : 10.f).withCentre(np));
        if (f)
        {
            g.setColour(c.withAlpha(0.25f));
            g.fillEllipse(R(30.f, 30.f).withCentre(np));
        }
    }
    // the puck: a machined disc with a red core, a cyan lock ring and crosshair lines out to the edges
    g.setColour(col::cyan.withAlpha(0.22f));
    g.drawHorizontalLine((int)pp.y, r.getX() + 4.f, pp.x - 30.f);
    g.drawHorizontalLine((int)pp.y, pp.x + 30.f, r.getRight() - 4.f);
    g.drawVerticalLine((int)pp.x, r.getY() + 4.f, pp.y - 30.f);
    g.drawVerticalLine((int)pp.x, pp.y + 30.f, r.getBottom() - 4.f);
    g.setColour(col::red.withAlpha(0.14f));
    g.fillEllipse(R(64.f, 64.f).withCentre(pp));
    g.setColour(col::soot.withAlpha(0.8f));
    g.fillEllipse(R(48.f, 48.f).withCentre(pp.translated(0.f, 2.f)));
    g.setGradientFill(juce::ColourGradient(juce::Colour(col::light ? 0xffffffff : 0xff3a4659), pp.x - 18.f, pp.y - 22.f,
                                           juce::Colour(col::light ? 0xffcdc5e3 : 0xff070b14), pp.x + 18.f, pp.y + 22.f, false));
    g.fillEllipse(R(46.f, 46.f).withCentre(pp));
    {
        juce::Path ring;
        for (int q = 0; q < 4; ++q)
            ring.addCentredArc(pp.x, pp.y, 21.f, 21.f, 0.f, (float)q * juce::MathConstants<float>::halfPi + 0.25f,
                               (float)(q + 1) * juce::MathConstants<float>::halfPi - 0.25f, true);
        g.setColour(col::cyan);
        g.strokePath(ring, juce::PathStrokeType(2.f));
    }
    g.setGradientFill(juce::ColourGradient(col::red.brighter(0.3f), pp.x - 6.f, pp.y - 6.f, col::redDeep, pp.x + 6.f, pp.y + 7.f, false));
    g.fillEllipse(R(17.f, 17.f).withCentre(pp));
    g.setColour(juce::Colours::white.withAlpha(0.5f));
    g.fillEllipse(R(4.f, 4.f).withCentre(pp.translated(-3.f, -3.f)));
    if (page.filledCount() < 2)
    {
        g.setColour(col::dim);
        g.setFont(F(10.f, 1));
        dt(g, "pick sounds for at least two corners", R(r.getX() + 210.f, r.getCentreY() + 34.f, r.getWidth() - 420.f, 28.f).toNearestInt(), juce::Justification::centred);
    }
}
