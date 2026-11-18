// ================================================================================================
// build 11 LAYERS - included by GhostEasyView.cpp inside namespace ghost::ui (it uses that file's look helpers).
//   LayersStrip : ENGINE / SUB - on/off, level, octave, pan, OPEN
//   SubPage     : every control of the SUB (original names); the XY pad stays an XY pad
//   BlendPage   : GHOST LOCK - STACK MODE / SPLIT, MONO BASS, AUTO GAIN, per-layer MONO / PHASE / cuts, the clash meter
// ================================================================================================

// a knob bound to one parameter of a layer engine (the engine's own parameter, its own name and range)
struct LKnob : public juce::Component
{
    juce::RangedAudioParameter *p;
    juce::String name;
    juce::ParameterAttachment att;
    float startNorm{0.f};
    LKnob(juce::RangedAudioParameter &prm, const juce::String &label)
        : p(&prm), name(label), att(prm, [this](float) { repaint(); })
    {
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    }
    void paint(juce::Graphics &g) override
    {
        auto b = getLocalBounds().toFloat();
        g.setColour(col::dim);
        g.setFont(Fr(12.5f, 1));
        dt(g, name, b.removeFromTop(16.f).toNearestInt(), juce::Justification::centred);
        auto val = b.removeFromBottom(17.f);
        g.setColour(col::ink);
        g.setFont(Fr(12.5f, 2));
        dt(g, p->getText(p->getValue(), 20), val.toNearestInt(), juce::Justification::centred);
        const float d = std::min(b.getWidth(), b.getHeight()) - 6.f;
        const auto c = b.getCentre();
        const float r = d * 0.5f, a0 = -2.3f, a1 = 2.3f, v = a0 + (a1 - a0) * p->getValue();
        juce::Path track, arc;
        track.addCentredArc(c.x, c.y, r, r, 0.f, a0, a1, true);
        arc.addCentredArc(c.x, c.y, r, r, 0.f, a0, v, true);
        g.setColour(col::line.withAlpha(0.35f));
        g.strokePath(track, juce::PathStrokeType(4.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(col::violet.withAlpha(0.22f));
        g.strokePath(arc, juce::PathStrokeType(8.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setGradientFill(juce::ColourGradient(col::violet, c.x - r, c.y, col::cyan, c.x + r, c.y, false));
        g.strokePath(arc, juce::PathStrokeType(4.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour(col::panel.brighter(0.15f));
        g.fillEllipse(c.x - r * 0.62f, c.y - r * 0.62f, r * 1.24f, r * 1.24f);
        const float tx = c.x + std::sin(v) * r * 0.5f, ty = c.y - std::cos(v) * r * 0.5f;
        g.setColour(col::ink);
        g.drawLine(c.x + std::sin(v) * r * 0.2f, c.y - std::cos(v) * r * 0.2f, tx, ty, 2.4f);
    }
    void mouseDown(const juce::MouseEvent &) override
    {
        startNorm = p->getValue();
        att.beginGesture();
    }
    void mouseDrag(const juce::MouseEvent &e) override
    {
        const float sens = e.mods.isShiftDown() ? 600.f : 160.f;
        const float n = juce::jlimit(0.f, 1.f, startNorm - (float)e.getDistanceFromDragStartY() / sens);
        att.setValueAsPartOfGesture(p->convertFrom0to1(n));
        repaint();
    }
    void mouseUp(const juce::MouseEvent &) override { att.endGesture(); }
    void mouseDoubleClick(const juce::MouseEvent &) override
    {
        att.setValueAsCompleteGesture(p->convertFrom0to1(p->getDefaultValue()));
        repaint();
    }
    void mouseWheelMove(const juce::MouseEvent &, const juce::MouseWheelDetails &w) override
    {
        att.setValueAsCompleteGesture(p->convertFrom0to1(juce::jlimit(0.f, 1.f, p->getValue() + w.deltaY * 0.1f)));
        repaint();
    }
};

// a choice (or an on/off) parameter as a row of pills: Octave -2/-1/0, Voices 1-4, Res. Mode, Noise Type, Link, Velocity...
struct LPills : public juce::Component
{
    juce::RangedAudioParameter *p;
    juce::String name;
    juce::StringArray items;
    juce::ParameterAttachment att;
    bool showName{true};
    LPills(juce::RangedAudioParameter &prm, const juce::String &label)
        : p(&prm), name(label), att(prm, [this](float) { repaint(); })
    {
        if (auto *c = dynamic_cast<juce::AudioParameterChoice *>(p))
            items = c->choices;
        else
            items = juce::StringArray{"OFF", "ON"};
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }
    int count() const { return items.size(); }
    int cur() const { return juce::jlimit(0, count() - 1, (int)std::lround(p->convertFrom0to1(p->getValue()))); }
    juce::Rectangle<float> pill(int k) const
    {
        auto b = getLocalBounds().toFloat();
        if (showName)
            b.removeFromTop(16.f);
        const float w = b.getWidth() / (float)count();
        return juce::Rectangle<float>(b.getX() + w * (float)k, b.getY(), w, b.getHeight()).reduced(2.f, 1.f);
    }
    void paint(juce::Graphics &g) override
    {
        if (showName)
        {
            g.setColour(col::dim);
            g.setFont(Fr(12.5f, 1));
            dt(g, name, getLocalBounds().removeFromTop(16), juce::Justification::centred);
        }
        for (int k = 0; k < count(); ++k)
        {
            auto r = pill(k);
            const bool on = k == cur();
            const float rad = r.getHeight() * 0.5f;
            if (on)
            {
                brushSwash(g, r.expanded(4.f, 2.f), col::amber, 2);
                g.setColour(juce::Colours::transparentBlack);
            }
            else
                g.setGradientFill(juce::ColourGradient(col::cell.brighter(0.1f), r.getX(), r.getY(), col::panel.darker(0.3f), r.getX(), r.getBottom(), false));
            fillChamfer(g, r, rad);
            g.setColour(on ? juce::Colours::transparentBlack : col::line.withAlpha(0.9f));
            drawChamfer(g, r.reduced(0.5f), rad, 1.1f);
            g.setColour(on ? col::onLit : col::ink);
            g.setFont(Fr(13.5f, 2));
            dt(g, items[k], r.toNearestInt(), juce::Justification::centred);
        }
    }
    void mouseDown(const juce::MouseEvent &e) override
    {
        for (int k = 0; k < count(); ++k)
            if (pill(k).contains(e.position))
            {
                att.setValueAsCompleteGesture(p->convertFrom0to1(count() > 1 ? (float)k / (float)(count() - 1) : 0.f));
                repaint();
                return;
            }
    }
};

// Bombaz's XY pad: X = Width (pulse width), Y = Drive
struct LXY : public juce::Component
{
    juce::RangedAudioParameter *px, *py;
    juce::ParameterAttachment ax, ay;
    LXY(juce::RangedAudioParameter &x, juce::RangedAudioParameter &y)
        : px(&x), py(&y), ax(x, [this](float) { repaint(); }), ay(y, [this](float) { repaint(); })
    {
        setMouseCursor(juce::MouseCursor::CrosshairCursor);
    }
    juce::Rectangle<float> pad() const { return getLocalBounds().toFloat().reduced(22.f, 8.f).withTrimmedBottom(14.f).withTrimmedLeft(14.f); }
    void paint(juce::Graphics &g) override
    {
        auto r = pad();
        g.setGradientFill(juce::ColourGradient(col::card.darker(0.4f), r.getX(), r.getY(), col::bg, r.getRight(), r.getBottom(), false));
        fillChamfer(g, r, 14.f);
        g.setColour(col::line.withAlpha(0.22f));
        for (int k = 1; k < 8; ++k)
        {
            g.drawVerticalLine((int)(r.getX() + r.getWidth() * (float)k / 8.f), r.getY() + 6, r.getBottom() - 6);
            g.drawHorizontalLine((int)(r.getY() + r.getHeight() * (float)k / 8.f), r.getX() + 6, r.getRight() - 6);
        }
        glowBorder(g, r, 14.f, 0.9f, 1.4f);
        const float x = r.getX() + px->getValue() * r.getWidth(), y = r.getBottom() - py->getValue() * r.getHeight();
        g.setColour(col::pink.withAlpha(0.18f));
        g.fillEllipse(x - 18, y - 18, 36, 36);
        g.setColour(col::pink.withAlpha(0.35f));
        g.fillEllipse(x - 11, y - 11, 22, 22);
        g.setColour(col::ink);
        g.fillEllipse(x - 6, y - 6, 12, 12);
        g.setColour(col::dim);
        g.setFont(Fr(12.5f, 2));
        dt(g, "Width  " + px->getText(px->getValue(), 20), getLocalBounds().removeFromBottom(18).withTrimmedLeft(14), juce::Justification::centred);
        juce::Graphics::ScopedSaveState ss(g);
        g.addTransform(juce::AffineTransform::rotation(-juce::MathConstants<float>::halfPi, 10.f, getHeight() * 0.5f));
        dt(g, "Drive  " + py->getText(py->getValue(), 20), juce::Rectangle<int>(10 - (getHeight() / 2), (int)(getHeight() * 0.5f) - 10, getHeight(), 20), juce::Justification::centred);
    }
    void set(const juce::MouseEvent &e, bool begin)
    {
        auto r = pad();
        if (begin)
        {
            ax.beginGesture();
            ay.beginGesture();
        }
        ax.setValueAsPartOfGesture(px->convertFrom0to1(juce::jlimit(0.f, 1.f, (e.position.x - r.getX()) / r.getWidth())));
        ay.setValueAsPartOfGesture(py->convertFrom0to1(juce::jlimit(0.f, 1.f, (r.getBottom() - e.position.y) / r.getHeight())));
        repaint();
    }
    void mouseDown(const juce::MouseEvent &e) override { set(e, true); }
    void mouseDrag(const juce::MouseEvent &e) override { set(e, false); }
    void mouseUp(const juce::MouseEvent &) override
    {
        ax.endGesture();
        ay.endGesture();
    }
    void mouseDoubleClick(const juce::MouseEvent &) override
    {
        ax.setValueAsCompleteGesture(px->convertFrom0to1(px->getDefaultValue()));
        ay.setValueAsCompleteGesture(py->convertFrom0to1(py->getDefaultValue()));
        repaint();
    }
};

// a bound on/off pill for an engine's boolean parameter (Link, Velocity Sensitivity, Use MTS)
struct LToggle : public GButton
{
    juce::RangedAudioParameter *p;
    juce::ParameterAttachment att;
    LToggle(juce::RangedAudioParameter &prm, const juce::String &label) : GButton(label), p(&prm), att(prm, [this](float v) { toggled = v > 0.5f; repaint(); })
    {
        tint = col::cyan;
        att.sendInitialUpdate();
        onClick = [this]() { att.setValueAsCompleteGesture(p->getValue() > 0.5f ? 0.f : 1.f); toggled = p->getValue() > 0.5f; repaint(); };
    }
};

// a slim slider for our own mix controls (level, pan, octave)
struct LSlider : public juce::Component
{
    juce::String label;
    float lo, hi, step;
    std::function<float()> get;
    std::function<void(float)> set;
    std::function<juce::String(float)> fmt;
    LSlider(juce::String l, float a, float b, float s, std::function<float()> g, std::function<void(float)> st, std::function<juce::String(float)> f)
        : label(std::move(l)), lo(a), hi(b), step(s), get(std::move(g)), set(std::move(st)), fmt(std::move(f))
    {
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
    }
    static constexpr int kValW = 62; // the value's room (the label gets the rest, so "OCTAVE" never gets cut)
    juce::Rectangle<float> track() const { return juce::Rectangle<float>(4.f, (float)getHeight() - 16.f, (float)getWidth() - 8.f, 7.f); }
    void paint(juce::Graphics &g) override
    {
        g.setColour(col::dim);
        g.setFont(Fr(13.f, 1));
        dt(g, label, getLocalBounds().removeFromTop(20).withTrimmedRight(kValW), juce::Justification::centredLeft);
        g.setColour(col::ink);
        g.setFont(Fr(13.f, 2));
        dt(g, fmt(get()), getLocalBounds().removeFromTop(20).removeFromRight(kValW), juce::Justification::centredRight);
        auto t = track();
        const float n = (get() - lo) / (hi - lo);
        g.setColour(col::line.withAlpha(0.35f));
        fillChamfer(g, t, 3.f);
        g.setGradientFill(juce::ColourGradient(col::violet, t.getX(), 0, col::cyan, t.getRight(), 0, false));
        fillChamfer(g, t.withWidth(std::max(6.f, t.getWidth() * n)), 3.f);
        g.setColour(col::ink);
        g.fillEllipse(t.getX() + t.getWidth() * n - 7.f, t.getCentreY() - 7.f, 14.f, 14.f);
    }
    void apply(const juce::MouseEvent &e)
    {
        auto t = track();
        float v = lo + (hi - lo) * juce::jlimit(0.f, 1.f, (e.position.x - t.getX()) / t.getWidth());
        if (step > 0.f)
            v = lo + std::round((v - lo) / step) * step;
        set(v);
        repaint();
    }
    void mouseDown(const juce::MouseEvent &e) override { apply(e); }
    void mouseDrag(const juce::MouseEvent &e) override { apply(e); }
};

// ---- the strip: ENGINE and SUB, large ----
struct LayersStrip : public juce::Component
{
    EasyView::Impl &I;
    ghost::layers::Host &host;
    std::function<void(int)> onOpen; // 0 ENGINE (opens DEEP mode), 1 SUB
    GButton onB[2], openB[2];
    std::unique_ptr<LSlider> level[2], pan;
    std::unique_ptr<LPills> subOct;
    std::unique_ptr<LSlider> surgeOct;
    static const char *nm(int k) { return k == 0 ? "ENGINE" : "SUB"; }
    static const char *key(int k) { return k == 0 ? "surge" : "sub"; }
    // a mix control moves through its host parameter (so the host sees the drag as a gesture and can record it)
    void setMix(const std::string &id, float plain)
    {
        if (auto *p = host.mixParam(id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1(plain));
            p->endChangeGesture();
        }
    }
    ghost::layers::Host::Mix &mix(int k) { return k == 0 ? host.surge : host.sub; }
    LayersStrip(EasyView::Impl &i, ghost::layers::Host &h) : I(i), host(h)
    {
        for (int k = 0; k < 2; ++k)
        {
            addAndMakeVisible(onB[k]);
            addAndMakeVisible(openB[k]);
            onB[k].text = nm(k);
            onB[k].big = true;
            onB[k].tint = k == 0 ? col::violet : col::cyan;
            onB[k].onClick = [this, k]() {
                setMix(std::string(key(k)) + "_on", mix(k).on.load() ? 0.f : 1.f);
                repaint();
            };
            openB[k].text = "OPEN";
            openB[k].tint = onB[k].tint;
            openB[k].onClick = [this, k]() {
                if (onOpen)
                    onOpen(k);
            };
            level[k] = std::make_unique<LSlider>("LEVEL", 0.f, 1.5f, 0.f, [this, k]() { return mix(k).level.load(); },
                                                 [this, k](float v) { setMix(std::string(key(k)) + "_level", v); },
                                                 [](float v) { return juce::String((int)std::lround(v * 100.f)) + "%"; });
            addAndMakeVisible(*level[k]);
        }
        pan = std::make_unique<LSlider>("PAN", -1.f, 1.f, 0.f, [this]() { return mix(1).pan.load(); },
                                        [this](float v) { setMix("sub_pan", v); },
                                        [](float v) { return std::fabs(v) < 0.005f ? juce::String("C") : juce::String((int)std::lround(std::fabs(v) * 100.f)) + (v < 0 ? "L" : "R"); });
        addAndMakeVisible(*pan);
        if (auto *o = host.uiParam("octave"))
        {
            subOct = std::make_unique<LPills>(*o, "OCTAVE");
            addAndMakeVisible(*subOct);
        }
        // ENGINE's octave is Surge's own scene Octave control (both scenes), set the way Surge sets it
        surgeOct = std::make_unique<LSlider>(
            "OCTAVE", -3.f, 3.f, 1.f, [this]() { return (float)I.synth->storage.getPatch().scene[0].octave.val.i; },
            [this](float v) {
                for (int sc = 0; sc < 2; ++sc)
                    I.setParamInt(&I.synth->storage.getPatch().scene[sc].octave, (int)std::lround(v));
            },
            [](float v) { return (v > 0 ? juce::String("+") : juce::String()) + juce::String((int)std::lround(v)); });
        addAndMakeVisible(*surgeOct);
    }
    juce::Rectangle<int> card(int k) const
    {
        const int w = (getWidth() - 8) / 2;
        return juce::Rectangle<int>(k * (w + 8), 0, w, getHeight());
    }
    void resized() override
    {
        for (int k = 0; k < 2; ++k)
        {
            auto c = card(k).reduced(12, 9);
            onB[k].setBounds(c.removeFromLeft(100));
            c.removeFromLeft(10);
            openB[k].setBounds(c.removeFromRight(88));
            c.removeFromRight(10);
            if (k == 0)
            {
                const int w2 = c.getWidth() / 2;
                level[0]->setBounds(c.removeFromLeft(w2).reduced(6, 0));
                surgeOct->setBounds(c.reduced(6, 0));
            }
            else
            {
                const int w3 = c.getWidth() / 3;
                level[1]->setBounds(c.removeFromLeft(w3).reduced(6, 0));
                if (subOct)
                    subOct->setBounds(c.removeFromLeft(w3).reduced(6, 0));
                pan->setBounds(c.reduced(6, 0));
            }
        }
    }
    void paint(juce::Graphics &g) override
    {
        for (int k = 0; k < 2; ++k)
        {
            panelBg(g, card(k).toFloat(), 16.f);
            onB[k].toggled = mix(k).on.load();
        }
    }
};

// ---- SUB page: Bombaz ----
struct SubPage : public juce::Component
{
    EasyView::Impl &I;
    ghost::layers::Host &host;
    std::unique_ptr<LXY> xy;
    std::unique_ptr<LKnob> tune, glide;
    std::unique_ptr<LPills> octave;
    GButton resetB{"DEFAULTS"};
    SubPage(EasyView::Impl &i, ghost::layers::Host &h) : I(i), host(h)
    {
        xy = std::make_unique<LXY>(*host.uiParam("width"), *host.uiParam("drive"));
        tune = std::make_unique<LKnob>(*host.uiParam("tuning"), "Tune");
        glide = std::make_unique<LKnob>(*host.uiParam("glideTime"), "Glide");
        octave = std::make_unique<LPills>(*host.uiParam("octave"), "Octave");
        for (juce::Component *c : std::initializer_list<juce::Component *>{xy.get(), tune.get(), glide.get(), octave.get(), &resetB})
            addAndMakeVisible(*c);
        resetB.onClick = [this]() {
            host.subEngine.setToDefaults();
            repaint();
        };
    }
    void resized() override
    {
        auto b = getLocalBounds().reduced(18, 14);
        b.removeFromTop(56);
        xy->setBounds(b.removeFromLeft(380));
        b.removeFromLeft(20);
        octave->setBounds(b.removeFromTop(62));
        b.removeFromTop(12);
        auto kr = b.removeFromTop(150);
        tune->setBounds(kr.removeFromLeft(150));
        glide->setBounds(kr.removeFromLeft(150));
        resetB.setBounds(b.removeFromBottom(40).removeFromLeft(150));
    }
    void paint(juce::Graphics &g) override
    {
        panelBg(g, getLocalBounds().toFloat(), 18.f);
        brushTitle(g, "SUB", 26.f, 12.f, 32.f, col::cyan);
        g.setColour(col::cyan);
        g.setFont(Fr(13.f, 1));
        dt(g, "Window-function sub bass. Mono, last note wins.", getLocalBounds().reduced(22, 12).withTrimmedTop(32).removeFromTop(22), juce::Justification::centredLeft);
        g.setColour(col::dim);
        g.setFont(Fr(13.f, 1));
        auto t = getLocalBounds().reduced(22, 14).withTrimmedLeft(420).withTrimmedTop(56 + 62 + 12 + 150 + 10).withTrimmedBottom(50);
        dt(g, "Pitch bend: your pitch wheel moves SUB by up to 7 semitones (the SUB's own range). Glide runs between notes that overlap. "
              "The XY pad: left-right = Width of the pulse, up-down = Drive. The on/off, level and pan of this layer are in the layer strip below.",
           t, juce::Justification::topLeft);
    }
};

// ---- LOCK page (GHOST LOCK): STACK MODE / SPLIT, MONO BASS, AUTO GAIN, per-layer MONO / PHASE / cuts, the clash meter ----
struct BlendPage : public juce::Component, private juce::Timer
{
    EasyView::Impl &I;
    ghost::layers::Host &host;
    struct Card
    {
        GButton mono{"MONO"}, phase{"PHASE"};
        std::unique_ptr<LSlider> lowCut, highCut;
    } card[2];
    GButton stackB{"STACK MODE"}, monoBassB{"MONO BASS"}, autoB{"AUTO GAIN"}, presetB[3];
    std::unique_ptr<LSlider> split, below;
    static const char *key(int k) { return k == 0 ? "surge" : "sub"; }
    static const char *nm(int k) { return k == 0 ? "ENGINE" : "SUB"; }
    void setMix(const std::string &id, float plain)
    {
        if (auto *p = host.mixParam(id))
        {
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1(plain));
            p->endChangeGesture();
        }
    }
    float getMix(const std::string &id)
    {
        auto *p = host.mixParam(id);
        return p ? p->convertFrom0to1(p->getValue()) : 0.f;
    }
    // cut sliders: the left end is "Off", then a log scale from lo to hi Hz
    static float hzOf(float n, float lo, float hi) { return n < 0.03f ? 0.f : lo * std::pow(hi / lo, (n - 0.03f) / 0.97f); }
    static float nOf(float hz, float lo, float hi) { return hz < 1.f ? 0.f : 0.03f + 0.97f * std::log(std::max(hz, lo) / lo) / std::log(hi / lo); }
    static juce::String hzTxt(float hz) { return hz < 1.f ? juce::String("Off") : juce::String((int)std::lround(hz)) + " Hz"; }
    void toggle(GButton &b, const std::string &id)
    {
        b.onClick = [this, id, &b]() {
            setMix(id, getMix(id) > 0.5f ? 0.f : 1.f);
            b.toggled = getMix(id) > 0.5f;
            repaint();
        };
    }
    BlendPage(EasyView::Impl &i, ghost::layers::Host &h) : I(i), host(h)
    {
        for (int k = 0; k < 2; ++k)
        {
            const std::string kk = key(k);
            card[k].mono.tint = card[k].phase.tint = k == 0 ? col::violet : col::cyan;
            toggle(card[k].mono, kk + "_mono");
            toggle(card[k].phase, kk + "_phase");
            card[k].lowCut = std::make_unique<LSlider>("LOW CUT", 0.f, 1.f, 0.f, [this, kk]() { return nOf(getMix(kk + "_lowcut"), 20.f, 1000.f); },
                                                       [this, kk](float v) { setMix(kk + "_lowcut", hzOf(v, 20.f, 1000.f)); }, [](float v) { return hzTxt(hzOf(v, 20.f, 1000.f)); });
            card[k].highCut = std::make_unique<LSlider>("HIGH CUT", 0.f, 1.f, 0.f, [this, kk]() { return nOf(getMix(kk + "_highcut"), 60.f, 20000.f); },
                                                        [this, kk](float v) { setMix(kk + "_highcut", hzOf(v, 60.f, 20000.f)); }, [](float v) { return hzTxt(hzOf(v, 60.f, 20000.f)); });
            for (juce::Component *c : std::initializer_list<juce::Component *>{&card[k].mono, &card[k].phase, card[k].lowCut.get(), card[k].highCut.get()})
                addAndMakeVisible(*c);
            card[k].mono.setTooltip(k == 0 ? "Sum ENGINE to mono" : "Centre this layer (ignore its pan)");
            card[k].phase.setTooltip("Flip the polarity of this layer, if it cancels the other one");
        }
        stackB.tint = monoBassB.tint = autoB.tint = col::cyan;
        toggle(stackB, "blend_stack");
        toggle(monoBassB, "blend_monobass");
        toggle(autoB, "blend_autogain");
        stackB.setTooltip("SUB owns everything below SPLIT, ENGINE owns everything above (crossovers that sum flat). Acts while SUB is on");
        monoBassB.setTooltip("Everything below MONO BELOW is summed to mono. Acts while SUB is on");
        autoB.setTooltip("SUB on = each layer a little lower, so turning SUB on does not jump in level");
        split = std::make_unique<LSlider>("SPLIT", 40.f, 300.f, 1.f, [this]() { return getMix("blend_split"); }, [this](float v) { setMix("blend_split", v); },
                                          [](float v) { return hzTxt(v); });
        below = std::make_unique<LSlider>("MONO BELOW", 40.f, 300.f, 1.f, [this]() { return getMix("blend_monobelow"); }, [this](float v) { setMix("blend_monobelow", v); },
                                          [](float v) { return hzTxt(v); });
        for (juce::Component *c : std::initializer_list<juce::Component *>{&stackB, &monoBassB, &autoB, split.get(), below.get()})
            addAndMakeVisible(*c);
        const auto &pre = ghost::layers::Host::stackPresets();
        for (int p = 0; p < 3; ++p)
        {
            if (p >= (int)pre.size())
                break;
            presetB[p].text = pre[(size_t)p].name;
            presetB[p].tint = col::violet;
            addAndMakeVisible(presetB[p]);
            presetB[p].onClick = [this, p]() { applyPreset(p); };
        }
    }
    void applyPreset(int p)
    {
        // neutral first (through the host parameters, so the host sees it), then the preset's own settings
        for (int k = 0; k < 2; ++k)
            for (const char *w : {"_mono", "_phase", "_lowcut", "_highcut"})
                setMix(std::string(key(k)) + w, 0.f);
        setMix("blend_stack", 0.f);
        setMix("blend_split", 110.f);
        setMix("blend_monobass", 0.f);
        setMix("blend_monobelow", 120.f);
        setMix("blend_autogain", 0.f);
        for (auto &kv : ghost::layers::Host::stackPresets()[(size_t)p].set)
            setMix(kv.first, kv.second);
        sync();
        repaint();
    }
    void sync()
    {
        for (int k = 0; k < 2; ++k)
        {
            card[k].mono.toggled = getMix(std::string(key(k)) + "_mono") > 0.5f;
            card[k].phase.toggled = getMix(std::string(key(k)) + "_phase") > 0.5f;
        }
        stackB.toggled = getMix("blend_stack") > 0.5f;
        monoBassB.toggled = getMix("blend_monobass") > 0.5f;
        autoB.toggled = getMix("blend_autogain") > 0.5f;
    }
    void visibilityChanged() override
    {
        host.blend.meterOn = isVisible();
        if (isVisible())
        {
            sync();
            startTimerHz(15);
        }
        else
            stopTimer();
    }
    void timerCallback() override
    {
        sync();
        repaint();
    }
    ~BlendPage() override { host.blend.meterOn = false; }
    static constexpr int kCardTop = 164, kCardH = 150;
    void resized() override
    {
        auto b = getLocalBounds().reduced(18, 14);
        b.removeFromTop(40);
        auto r1 = b.removeFromTop(46);
        stackB.setBounds(r1.removeFromLeft(140));
        split->setBounds(r1.removeFromLeft(220).reduced(8, 0));
        r1.removeFromLeft(14);
        monoBassB.setBounds(r1.removeFromLeft(140));
        below->setBounds(r1.removeFromLeft(220).reduced(8, 0));
        b.removeFromTop(6);
        auto r2 = b.removeFromTop(40);
        autoB.setBounds(r2.removeFromLeft(140));
        r2.removeFromLeft(14);
        for (int p = 0; p < 3; ++p)
        {
            presetB[p].setBounds(r2.removeFromLeft(220));
            r2.removeFromLeft(8);
        }
        b.removeFromTop(10);
        auto cards = b.removeFromTop(kCardH);
        const int cw = cards.getWidth() / 2;
        for (int k = 0; k < 2; ++k)
        {
            auto c = cards.removeFromLeft(cw).reduced(6, 0);
            c.removeFromTop(32);
            auto t = c.removeFromTop(34);
            card[k].mono.setBounds(t.removeFromLeft(t.getWidth() / 2).reduced(4, 0));
            card[k].phase.setBounds(t.reduced(4, 0));
            card[k].lowCut->setBounds(c.removeFromTop(40).reduced(8, 0));
            card[k].highCut->setBounds(c.removeFromTop(40).reduced(8, 0));
        }
        meterArea = b.withTrimmedTop(10);
    }
    juce::Rectangle<int> meterArea;
    void paint(juce::Graphics &g) override
    {
        sync();
        panelBg(g, getLocalBounds().toFloat(), 18.f);
        brushTitle(g, "GHOST LOCK", 26.f, 12.f, 32.f);
        g.setColour(col::dim);
        g.setFont(F(11.f, 1));
        dt(g, "ENGINE and SUB that sit together", R(300.f, 12.f, 360.f, 32.f), juce::Justification::centredLeft);
        // the per-layer cards
        const int cw = (getWidth() - 36) / 2;
        for (int k = 0; k < 2; ++k)
        {
            auto r = juce::Rectangle<int>(18 + k * cw + 6, card[k].mono.getY() - 32, cw - 12, kCardH).toFloat();
            panelBg(g, r, 12.f);
            g.setColour(k == 0 ? col::violet : col::cyan);
            g.setFont(Fr(15.f, 2));
            dt(g, nm(k), r.toNearestInt().reduced(14, 4).removeFromTop(26), juce::Justification::centredLeft);
        }
        // the clash meter: each layer's level below 200 Hz and how well the lows line up
        auto m = meterArea.reduced(2, 0);
        panelBg(g, m.toFloat(), 12.f);
        g.setColour(col::ink);
        g.setFont(Fr(15.f, 2));
        dt(g, "CLASH METER  (below 200 Hz)", m.reduced(14, 4).removeFromTop(24), juce::Justification::centredLeft);
        auto body = m.reduced(14, 0).withTrimmedTop(32).withTrimmedBottom(30);
        auto bars = body.removeFromLeft(body.getWidth() * 55 / 100);
        for (int k = 0; k < 2; ++k)
        {
            auto row = bars.removeFromTop(20);
            g.setColour(col::dim);
            g.setFont(Fr(13.f, 1));
            dt(g, nm(k), row.removeFromLeft(70), juce::Justification::centredLeft);
            const float db = host.blend.meterDb[k].load();
            const float n = juce::jlimit(0.f, 1.f, (db + 60.f) / 60.f);
            auto track = row.toFloat().reduced(0, 5);
            g.setColour(col::line.withAlpha(0.35f));
            fillChamfer(g, track, 3.f);
            g.setColour(k == 0 ? col::violet : col::cyan);
            fillChamfer(g, track.withWidth(std::max(2.f, track.getWidth() * n)), 3.f);
            bars.removeFromTop(2);
        }
        // correlation: -1 (cancel) .. +1 (together)
        auto cor = body.reduced(14, 0);
        {
            auto row = cor.removeFromTop(20);
            g.setColour(col::dim);
            g.setFont(Fr(13.f, 1));
            dt(g, "ENGINE / SUB", row.removeFromLeft(110), juce::Justification::centredLeft);
            auto track = row.toFloat().reduced(0, 5);
            g.setColour(col::line.withAlpha(0.35f));
            fillChamfer(g, track, 3.f);
            const float c = host.blend.meterCorr.load();
            g.setColour(c < -0.3f ? juce::Colour(0xffff4d6d) : (c < 0.f ? col::amber : col::cyan));
            const float cx = track.getCentreX();
            fillChamfer(g, juce::Rectangle<float>(std::min(cx, cx + c * track.getWidth() * 0.5f), track.getY(), std::max(2.f, std::fabs(c) * track.getWidth() * 0.5f), track.getHeight()), 2.f);
            g.setColour(col::ink.withAlpha(0.5f));
            g.fillRect(cx - 0.5f, track.getY() - 1.f, 1.f, track.getHeight() + 2.f);
        }
        int level = 0;
        const std::string hint = host.clashHint(level);
        g.setColour(level == 2 ? juce::Colour(0xffff4d6d) : (level == 1 ? col::amber : col::cyan));
        g.setFont(Fr(13.f, 2));
        dt(g, hint.empty() ? juce::String(host.sub.on.load() ? "The lows look clean." : "SUB is off: GHOST LOCK starts working when you turn SUB on.") : juce::String(hint),
           m.reduced(14, 4).removeFromBottom(24), juce::Justification::centredLeft);
    }
};
