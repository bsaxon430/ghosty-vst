#include "SpectrumEditor.h"
using namespace ghosty;

namespace
{
const float FMIN = 20.0f, FMAX = 20000.0f;
const float LR = std::log (FMAX / FMIN);
juce::Colour timbreColour (int t)
{
    static const juce::uint32 c[16] { 0xff45E6FF, 0xff78FFC8, 0xffFF4FD8, 0xffFFB547, 0xffFF785A, 0xffB6FF3C, 0xffA0DCFF, 0xffFF5A96,
                                      0xff3CFF8F, 0xffC882FF, 0xffFFD0A0, 0xff9EB8FF, 0xffFF9EF0, 0xff7DF9FF, 0xffE0C8FF, 0xffFFF07A };
    return juce::Colour (c[juce::jlimit (0, 15, t)]);
}
const juce::Colour drumColour { 0xffA06BFF };
juce::String hzText (float f) { return f >= 1000.0f ? juce::String (f / 1000.0f, f >= 10000.0f ? 1 : 2) + " kHz" : juce::String (f, f < 100.0f ? 2 : 1) + " Hz"; }
}

// ------------------------------------------------------------------ field
SpectrumField::SpectrumField (SpectrumProcessor& p) : proc (p)
{
    spec = juce::Image (juce::Image::ARGB, 420, 260, true);
    for (int t = 0; t < SpectrumProcessor::NUM_TIMBRES; ++t)
    {
        const auto f = makeShape (t);
        float mx = 1e-9f;
        for (int k = 1; k <= 32; ++k)
        {
            double re = 0, im = 0;
            for (int n = 0; n < FRAME; ++n) { const double ph = juce::MathConstants<double>::twoPi * k * n / FRAME; re += f[(size_t) n] * std::cos (ph); im += f[(size_t) n] * std::sin (ph); }
            amps[(size_t) t][(size_t) k - 1] = (float) std::hypot (re, im);
            mx = juce::jmax (mx, amps[(size_t) t][(size_t) k - 1]);
        }
        for (auto& a : amps[(size_t) t]) a /= mx;
    }
    liveLook = lookFor (7);
    startTimerHz (30);
}

void SpectrumField::resized() {}
float SpectrumField::f2y (float f) const { return TOP + ((float) getHeight() - TOP - BOT) * (1.0f - std::log (juce::jlimit (FMIN, FMAX, f) / FMIN) / LR); }
float SpectrumField::y2f (float y) const { return FMIN * std::exp (LR * (1.0f - (y - TOP) / ((float) getHeight() - TOP - BOT))); }
float SpectrumField::pan2x (float p) const { return LEFT + (p + 1.0f) * 0.5f * ((float) getWidth() - LEFT - RIGHT); }
float SpectrumField::x2pan (float x) const { return juce::jlimit (-1.0f, 1.0f, (x - LEFT) / ((float) getWidth() - LEFT - RIGHT) * 2.0f - 1.0f); }

int SpectrumField::hitTest (juce::Point<float> p) const
{
    int best = -1; float bd = 30.0f;
    for (int i = 0; i < SpectrumProcessor::MAX_EL; ++i)
    {
        auto& e = proc.els[(size_t) i];
        if (! e.active.load()) continue;
        const float d = p.getDistanceFrom ({ pan2x (e.pan.load()), f2y (e.freq.load()) });
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

void SpectrumField::updateSpectrogram()
{
    const int N = 4096;
    const int end = proc.ringPos.load();
    for (int i = 0; i < N; ++i)
    {
        const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) (N - 1));
        fftData[(size_t) i] = proc.ring[(size_t) ((end - N + i + SpectrumProcessor::RING) & (SpectrumProcessor::RING - 1))] * w;
    }
    std::fill (fftData.begin() + N, fftData.end(), 0.0f);
    fft.performFrequencyOnlyForwardTransform (fftData.data());
    const int w = spec.getWidth(), h = spec.getHeight();
    spec.moveImageSection (0, 0, 1, 0, w - 1, h);
    const double sr = proc.getSampleRate() > 0 ? proc.getSampleRate() : 44100.0;
    for (int r = 0; r < h; ++r)
    {
        const float f = FMIN * std::exp (LR * (1.0f - (float) r / (float) (h - 1)));
        const int bin = juce::jlimit (1, N / 2 - 1, (int) (f / (sr * 0.5) * (N / 2)));
        const float mag = fftData[(size_t) bin] / (float) (N / 4);
        const float db = 20.0f * std::log10 (mag + 1e-9f);
        const float v = juce::jlimit (0.0f, 1.0f, (db + 95.0f) / 75.0f);
        juce::Colour c;
        if (v < 0.35f) c = juce::Colour (0xff07080b).interpolatedWith (juce::Colour (0xff3c146e), v / 0.35f);
        else if (v < 0.6f) c = juce::Colour (0xff3c146e).interpolatedWith (col::ecto, (v - 0.35f) / 0.25f);
        else if (v < 0.8f) c = col::ecto.interpolatedWith (col::slime, (v - 0.6f) / 0.2f);
        else c = col::slime.interpolatedWith (col::white, (v - 0.8f) / 0.2f);
        spec.setPixelAt (w - 1, r, c.withAlpha (juce::jmin (1.0f, 0.15f + v)));
    }
}

void SpectrumField::paint (juce::Graphics& g)
{
    const float W = (float) getWidth(), H = (float) getHeight();
    g.setColour (juce::Colour (0xff06070a)); g.fillRoundedRectangle (getLocalBounds().toFloat(), 12);
    g.setOpacity (0.8f); g.drawImage (spec, juce::Rectangle<float> (LEFT, TOP, W - LEFT - RIGHT, H - TOP - BOT)); g.setOpacity (1.0f);

    const char* bandNames[7] { "SUB", "BASS", "LOW MID", "MID", "HIGH MID", "PRESENCE", "AIR" };
    const float bands[8] { 20, 60, 250, 500, 2000, 4000, 6000, 20000 };
    for (int i = 0; i < 7; ++i)
    {
        const float y1 = f2y (bands[i + 1]), y2 = f2y (bands[i]);
        g.setColour ((i % 2 ? col::ecto : col::slime).withAlpha (0.03f)); g.fillRect (LEFT, y1, W - LEFT, y2 - y1);
        g.setColour (col::white.withAlpha (0.32f)); g.setFont (font (9.0f));
        g.drawText (bandNames[i], juce::Rectangle<float> (W - 90, y2 - 15, 80, 12), juce::Justification::centredRight);
    }
    const float ref = proc.ref();
    for (int m = 21; m <= 127; m += 12)          // A notes: m = 21 is A0
    {
        const float f = ref * std::pow (2.0f, (float) (m - 69) / 12.0f);
        if (f < FMIN || f > FMAX) continue;
        const float y = f2y (f); const bool isRef = m == 69;
        g.setColour (isRef ? col::slime.withAlpha (0.6f) : col::white.withAlpha (0.12f));
        if (isRef) g.drawLine (LEFT, y, W - RIGHT, y, 1.2f);
        else { const float dl[] { 2, 4 }; g.drawDashedLine ({ LEFT, y, W - RIGHT, y }, dl, 2, 1.0f); }
        g.setColour (isRef ? col::slime : col::white.withAlpha (0.55f)); g.setFont (font (9.0f));
        g.drawText ("A" + juce::String (m / 12 - 1) + " " + (f >= 1000 ? juce::String (f / 1000.0f, 2) + "k" : juce::String (juce::roundToInt (f))),
                    juce::Rectangle<float> (2, y - 6, LEFT - 4, 12), juce::Justification::centredLeft);
    }

    // filter lines
    const float lp = proc.apvts.getRawParameterValue ("lp")->load(), hp = proc.apvts.getRawParameterValue ("hp")->load();
    auto filterLine = [&] (const juce::String& nm, float f, bool isLP)
    {
        const float y = f2y (f);
        if ((isLP && f < 19900) || (! isLP && f > 21)) { g.setColour (juce::Colours::black.withAlpha (0.45f)); g.fillRect (LEFT, isLP ? 0.0f : y, W - LEFT, isLP ? y : H - y); }
        const float dl[] { 6, 4 }; g.setColour (col::ice); g.drawDashedLine ({ LEFT, y, W, y }, dl, 2, 1.5f);
        auto pill = juce::Rectangle<float> (W - 70, y - 10, 64, 20);
        g.fillRoundedRectangle (pill, 10); g.setColour (col::bg); g.setFont (font (10.0f));
        g.drawText (nm + " " + (f >= 1000 ? juce::String (f / 1000.0f, 1) + "k" : juce::String (juce::roundToInt (f))), pill, juce::Justification::centred);
    };
    filterLine ("LP", lp, true); filterLine ("HP", hp, false);

    // overtones + drum bands
    const float lvl = proc.level.load();
    auto drawHarmonics = [&] (float x, float f, int tb, float energy, juce::Colour c)
    {
        const auto& a = amps[(size_t) juce::jlimit (0, 15, tb)];
        for (int k = 1; k <= 32; ++k)
        {
            const float fk = f * (float) k; if (fk > FMAX) break;
            const float amp = a[(size_t) k - 1]; if (amp < 0.015f) continue;
            const float y = f2y (fk), w = (18.0f + amp * 90.0f) * (0.4f + energy * 0.9f) * (1.0f + lvl * 0.6f);
            const float al = juce::jmin (1.0f, (0.15f + amp * 0.85f) * (0.25f + energy * 0.9f));
            juce::ColourGradient gr (c.withAlpha (0.0f), x - w, y, c.withAlpha (0.0f), x + w, y, false); gr.addColour (0.5, c.withAlpha (al));
            g.setGradientFill (gr); g.fillRect (x - w, y - (k == 1 ? 1.5f : 0.7f), w * 2, k == 1 ? 3.0f : 1.4f + amp * 1.5f);
            if (k > 1 && amp > 0.12f && energy > 0.3f) { g.setColour (c.withAlpha (al * 0.7f)); g.setFont (font (7.0f, false)); g.drawText (juce::String (k) + "x", juce::Rectangle<float> (x + w * 0.55f, y - 10, 20, 9), juce::Justification::centredLeft); }
        }
        g.setColour (c.withAlpha (0.12f + energy * 0.2f)); g.drawLine (x, f2y (f), x, f2y (juce::jmin (FMAX, f * 32.0f)), 1.0f);
    };
    for (int i = 0; i < SpectrumProcessor::MAX_EL; ++i)
    {
        auto& e = proc.els[(size_t) i];
        if (! e.active.load()) continue;
        const float x = pan2x (e.pan.load()), f = e.freq.load();
        if (e.kind.load() == 0)
        {
            const float en = juce::jmax (e.drone.load() ? 1.0f : e.level.load(), i == selected ? 0.25f : 0.12f);
            drawHarmonics (x, f, e.timbre.load(), en, timbreColour (e.timbre.load()));
        }
        else if (flash[i] > 0.03f)
        {
            const auto band = SpectrumProcessor::drumBand (e.timbre.load());
            const float ratio = f / SpectrumProcessor::drumCenter (e.timbre.load());
            const float y1 = f2y (juce::jmin (FMAX, band.y * ratio)), y2 = f2y (band.x * ratio), w = 40.0f + (1.0f - flash[i]) * W * 0.4f;
            juce::ColourGradient gr (drumColour.withAlpha (0.0f), x - w, 0, drumColour.withAlpha (0.0f), x + w, 0, false); gr.addColour (0.5, drumColour.withAlpha (flash[i] * 0.35f));
            g.setGradientFill (gr); g.fillRect (x - w, y1, w * 2, y2 - y1);
            g.setColour (juce::Colours::white.withAlpha (flash[i] * 0.8f)); g.drawLine (x - w * 0.4f, f2y (f), x + w * 0.4f, f2y (f), 2.0f);
        }
    }
    if (drag == DragLive)
    {
        const int tb = (int) proc.apvts.getRawParameterValue ("playTimbre")->load();
        drawHarmonics (liveAt.x, y2f (liveAt.y), tb, 1.0f, timbreColour (tb));
    }
    for (auto& r : ripples) { g.setColour (col::slime.withAlpha (r.a)); g.drawEllipse (r.x - r.r * 2, r.y - r.r * 0.5f, r.r * 4, r.r, 2.0f); }
    rain.paint (g);

    // ghosts + labels
    for (int i = 0; i < SpectrumProcessor::MAX_EL; ++i)
    {
        auto& e = proc.els[(size_t) i];
        if (! e.active.load()) continue;
        const float x = pan2x (e.pan.load()), y = f2y (e.freq.load());
        const bool drum = e.kind.load() == 1;
        const float en = drum ? flash[i] : e.level.load();
        const float sz = (drum ? 24.0f : 22.0f) + en * 8.0f + (i == selected ? 6.0f : 0.0f);
        if (i == selected) { g.setColour (col::slime.withAlpha (0.7f)); const float dl[] { 3, 3 }; juce::Path c; c.addEllipse (x - sz * 1.15f, y - sz * 1.15f, sz * 2.3f, sz * 2.3f); juce::PathStrokeType (1.5f).createDashedStroke (c, c, dl, 2); g.fillPath (c); }
        drawGhost (g, lookFor (e.look), x, y - (drum ? flash[i] * 6.0f : 0.0f), sz, juce::jmax (glitch[i], drum ? flash[i] * 0.6f : 0.0f), drag == DragEl && i == selected, 1.0f, e.pan.load() >= 0);
        const float m = proc.midiOf (e.freq.load()); const int mi = juce::roundToInt (m);
        const juce::String name = drum ? DrumBank::names()[e.timbre.load()].toUpperCase() : keyNames()[(mi % 12 + 12) % 12] + juce::String (mi / 12 - 1);
        const juce::String lab = name + "  " + hzText (e.freq.load());
        g.setFont (font (9.0f));
        const float tw = textWidth (font (9.0f), lab) + 10.0f;
        g.setColour (juce::Colour (0xc007080b)); g.fillRect (x - tw / 2, y + sz * 0.95f + 2, tw, 13.0f);
        g.setColour (drum ? drumColour : timbreColour (e.timbre.load())); g.drawText (lab, juce::Rectangle<float> (x - tw / 2, y + sz * 0.95f + 2, tw, 13), juce::Justification::centred);
    }
    if (drag == DragLive)
    {
        drawGhost (g, liveLook, liveAt.x, liveAt.y, 24.0f, 0.5f, juce::Random::getSystemRandom().nextBool());
        const float f = y2f (liveAt.y); const int mi = juce::roundToInt (proc.midiOf (f));
        g.setColour (col::white); g.setFont (font (10.0f));
        g.drawText (keyNames()[(mi % 12 + 12) % 12] + juce::String (mi / 12 - 1) + "  " + hzText (f), juce::Rectangle<float> (liveAt.x - 80, liveAt.y - 44, 160, 14), juce::Justification::centred);
    }
    const int ps = proc.playStep.load();
    if (ps >= 0) for (int i = 0; i < 16; ++i) { g.setColour (i == ps ? col::slime : col::white.withAlpha (i % 4 == 0 ? 0.35f : 0.15f)); const float r = i == ps ? 3.0f : 2.0f; g.fillEllipse (LEFT + 8 + i * 9 - r, H - 8 - r, r * 2, r * 2); }
}

void SpectrumField::mouseDown (const juce::MouseEvent& e)
{
    const auto p = e.position;
    const float lp = proc.apvts.getRawParameterValue ("lp")->load(), hp = proc.apvts.getRawParameterValue ("hp")->load();
    if (p.x > (float) getWidth() - 74)
    {
        if (std::abs (p.y - f2y (lp)) < 16) { drag = DragLP; proc.apvts.getParameter ("lp")->beginChangeGesture(); return; }
        if (std::abs (p.y - f2y (hp)) < 16) { drag = DragHP; proc.apvts.getParameter ("hp")->beginChangeGesture(); return; }
    }
    const int hit = hitTest (p);
    if (hit >= 0)
    {
        selected = hit; drag = DragEl; glitch[hit] = 1.0f;
        auto& el = proc.els[(size_t) hit];
        grab = juce::Point<float> (pan2x (el.pan.load()), f2y (el.freq.load())) - p;
        if (onSelectionChanged) onSelectionChanged();
        return;
    }
    if (p.x < LEFT) return;
    const float raw = y2f (p.y), pan = x2pan (p.x);
    if (mode == PlayMode)
    {
        liveAt = { p.x, f2y (proc.snap (raw)) };
        proc.liveOn (proc.snap (raw), pan); drag = DragLive;
        ripples.push_back ({ p.x, liveAt.y, 6.0f, 1.0f });
        liveLook = lookFor (juce::Random::getSystemRandom().nextInt (35));
        return;
    }
    int idx = -1;
    if (mode == ToneMode) idx = proc.addTone (proc.snap (raw), pan, (int) proc.apvts.getRawParameterValue ("playTimbre")->load(), true);
    else
    {
        idx = proc.addDrum (drumToAdd, proc.isTonalDrum (drumToAdd) ? proc.snap (raw) : raw, pan);
        if (! proc.hostPlaying.load()) proc.internalPlay.store (true);
    }
    if (idx >= 0) { selected = idx; drag = DragEl; grab = {}; glitch[idx] = 1.0f; if (onSelectionChanged) onSelectionChanged(); }
}

void SpectrumField::mouseDrag (const juce::MouseEvent& e)
{
    const auto p = e.position;
    if (drag == DragLP || drag == DragHP)
    {
        auto* prm = proc.apvts.getParameter (drag == DragLP ? "lp" : "hp");
        const float other = proc.apvts.getRawParameterValue (drag == DragLP ? "hp" : "lp")->load();
        float f = juce::jlimit (FMIN, FMAX, y2f (p.y));
        f = drag == DragLP ? juce::jmax (f, other * 1.5f) : juce::jmin (f, other / 1.5f);
        prm->setValueNotifyingHost (prm->convertTo0to1 (f));
        return;
    }
    if (drag == DragLive)
    {
        const float f = proc.snap (y2f (p.y));
        if (std::abs (proc.midiOf (f) - proc.midiOf (y2f (liveAt.y))) > 0.01f) { ripples.push_back ({ p.x, f2y (f), 6.0f, 1.0f }); rain.spawn ({ p.x - 60, 0, 120, 1 }, 2); }
        liveAt = { p.x, f2y (f) };
        proc.liveMove (f, x2pan (p.x));
        return;
    }
    if (drag == DragEl && selected >= 0)
    {
        auto& el = proc.els[(size_t) selected];
        const auto q = p + grab;
        const float raw = y2f (q.y), old = el.freq.load();
        const bool snappy = el.kind.load() == 0 || proc.isTonalDrum (el.timbre.load());
        el.freq.store (snappy ? proc.snap (raw) : juce::jlimit (FMIN, FMAX, raw));
        el.pan.store (x2pan (q.x));
        if (std::abs (proc.midiOf (old) - proc.midiOf (el.freq.load())) > 0.01f) { glitch[selected] = 1.0f; rain.spawn ({ q.x - 60, 0, 120, 1 }, 3); }
        glitch[selected] = juce::jmax (glitch[selected], 0.5f);
        if (onSelectionChanged) onSelectionChanged();
    }
}

void SpectrumField::mouseUp (const juce::MouseEvent&)
{
    if (drag == DragLive) proc.liveOff();
    if (drag == DragLP) proc.apvts.getParameter ("lp")->endChangeGesture();
    if (drag == DragHP) proc.apvts.getParameter ("hp")->endChangeGesture();
    drag = None;
}

void SpectrumField::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int hit = hitTest (e.position);
    if (hit >= 0) { proc.removeElement (hit); selected = -1; drag = None; if (onSelectionChanged) onSelectionChanged(); }
}

void SpectrumField::timerCallback()
{
    updateSpectrogram();
    for (int i = 0; i < SpectrumProcessor::MAX_EL; ++i)
    {
        auto& e = proc.els[(size_t) i];
        const int h = e.hits.load();
        if (h != lastHits[i]) { lastHits[i] = h; flash[i] = 1.0f; glitch[i] = juce::jmax (glitch[i], 0.4f);
            if (juce::Random::getSystemRandom().nextFloat() < 0.4f) rain.spawn ({ pan2x (e.pan.load()) - 20, 0, 40, 1 }, 1); }
        flash[i] *= 0.88f; glitch[i] *= 0.9f;
    }
    for (auto& r : ripples) { r.r += 4.0f; r.a *= 0.92f; }
    ripples.erase (std::remove_if (ripples.begin(), ripples.end(), [] (const Ripple& r) { return r.a < 0.03f; }), ripples.end());
    rain.step ((float) getHeight());
    repaint();
}

// ------------------------------------------------------------------ editor
SpectrumEditor::SpectrumEditor (SpectrumProcessor& p) : AudioProcessorEditor (&p), proc (p), field (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (field);
    for (auto* c : std::initializer_list<juce::Component*> { &sceneBox, &keyBox, &scaleBox, &refBox, &playSoundBox, &elSoundBox, &drumPick,
                                                             &playBtn, &modePlay, &modeTone, &modeDrum, &snapBtn, &droneBtn, &delBtn, &vol, &selLabel })
        addAndMakeVisible (c);

    refreshScenes();
    sceneBox.onChange = [this] { const int i = sceneBox.getSelectedItemIndex(); if (i >= 0) { proc.setCurrentProgram (i); field.selected = -1; refreshSelection(); wordGlitch = 1; } };
    keyBox.addItemList (keyNames(), 1); scaleBox.addItemList (scaleNames(), 1);
    refBox.addItemList ({ "A4 = 440 Hz", "A4 = 432 Hz", "A4 = 442 Hz" }, 1);
    playSoundBox.addItemList (SpectrumProcessor::timbreNames(), 1);
    using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    keyAtt = std::make_unique<CA> (proc.apvts, "key", keyBox); scaleAtt = std::make_unique<CA> (proc.apvts, "scale", scaleBox);
    refAtt = std::make_unique<CA> (proc.apvts, "ref", refBox); playSoundAtt = std::make_unique<CA> (proc.apvts, "playTimbre", playSoundBox);
    snapBtn.setClickingTogglesState (true);
    snapAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, "snap", snapBtn);

    drumPick.addItemList (DrumBank::names(), 1); drumPick.setSelectedItemIndex (0, juce::dontSendNotification);
    drumPick.onChange = [this] { field.drumToAdd = juce::jmax (0, drumPick.getSelectedItemIndex()); };

    playBtn.onClick = [this] { proc.internalPlay.store (! proc.internalPlay.load()); };
    modePlay.onClick = [this] { setMode (SpectrumField::PlayMode); };
    modeTone.onClick = [this] { setMode (SpectrumField::ToneMode); };
    modeDrum.onClick = [this] { setMode (SpectrumField::DrumMode); };
    setMode (SpectrumField::PlayMode);

    for (int i = 0; i < 16; ++i)
    {
        auto* b = stepBtns.add (new juce::TextButton (""));
        b->setClickingTogglesState (true);
        b->onClick = [this, i, b]
        {
            if (field.selected < 0) return;
            auto& e = proc.els[(size_t) field.selected];
            int m = e.steps.load(); m = b->getToggleState() ? (m | (1 << i)) : (m & ~(1 << i)); e.steps.store (m);
            if (b->getToggleState() && ! proc.hostPlaying.load()) proc.internalPlay.store (true);
        };
        addAndMakeVisible (b);
    }
    droneBtn.setClickingTogglesState (true);
    droneBtn.onClick = [this] { if (field.selected >= 0) { proc.els[(size_t) field.selected].drone.store (droneBtn.getToggleState()); if (! droneBtn.getToggleState() && ! proc.hostPlaying.load()) proc.internalPlay.store (true); refreshSelection(); } };
    delBtn.onClick = [this] { if (field.selected >= 0) { proc.removeElement (field.selected); field.selected = -1; refreshSelection(); } };
    elSoundBox.onChange = [this]
    {
        if (field.selected < 0) return;
        auto& e = proc.els[(size_t) field.selected];
        const int i = elSoundBox.getSelectedItemIndex(); if (i < 0) return;
        e.timbre.store (i);
        if (e.kind.load() == 1) e.freq.store (SpectrumProcessor::drumCenter (i));
    };
    vol.setRange (0.0, 1.4, 0.01);
    vol.onValueChange = [this] { if (field.selected >= 0) proc.els[(size_t) field.selected].vol.store ((float) vol.getValue()); };
    selLabel.setFont (font (14.0f));

    field.onSelectionChanged = [this] { refreshSelection(); };

    const std::pair<const char*, const char*> ks[] { { "atmos", "ATMOSPHERE" }, { "drive", "DRIVE" }, { "gain", "VOLUME" }, { "bpm", "TEMPO*" } };
    for (auto& k : ks) addAndMakeVisible (knobs.add (new GhostKnob (proc.apvts, k.first, k.second)));

    refreshSelection();
    setSize (1120, 790);
    startTimerHz (20);
}

SpectrumEditor::~SpectrumEditor() { setLookAndFeel (nullptr); }

void SpectrumEditor::refreshScenes()
{
    sceneBox.clear (juce::dontSendNotification);
    for (int i = 0; i < proc.getNumPrograms(); ++i) sceneBox.addItem (proc.getProgramName (i), i + 1);
    sceneBox.setSelectedItemIndex (proc.getCurrentProgram(), juce::dontSendNotification);
}

void SpectrumEditor::setMode (SpectrumField::Mode m)
{
    field.mode = m;
    modePlay.setToggleState (m == SpectrumField::PlayMode, juce::dontSendNotification);
    modeTone.setToggleState (m == SpectrumField::ToneMode, juce::dontSendNotification);
    modeDrum.setToggleState (m == SpectrumField::DrumMode, juce::dontSendNotification);
    drumPick.setVisible (m == SpectrumField::DrumMode);
    repaint();
}

void SpectrumEditor::refreshSelection()
{
    const int s = field.selected;
    const bool has = s >= 0 && proc.els[(size_t) s].active.load();
    for (auto* c : std::initializer_list<juce::Component*> { &elSoundBox, &droneBtn, &delBtn, &vol }) c->setEnabled (has);
    for (auto* b : stepBtns) b->setEnabled (has);
    if (! has) { selLabel.setText ("Click a ghost to edit it", juce::dontSendNotification); selLabel.setColour (juce::Label::textColourId, col::dim); for (auto* b : stepBtns) b->setToggleState (false, juce::dontSendNotification); return; }
    auto& e = proc.els[(size_t) s];
    const bool drum = e.kind.load() == 1;
    elSoundBox.clear (juce::dontSendNotification);
    elSoundBox.addItemList (drum ? DrumBank::names() : SpectrumProcessor::timbreNames(), 1);
    elSoundBox.setSelectedItemIndex (e.timbre.load(), juce::dontSendNotification);
    droneBtn.setEnabled (! drum); droneBtn.setToggleState (! drum && e.drone.load(), juce::dontSendNotification);
    const int m = e.steps.load();
    for (int i = 0; i < 16; ++i) { stepBtns[i]->setToggleState ((m >> i) & 1, juce::dontSendNotification); stepBtns[i]->setEnabled (drum || ! e.drone.load()); }
    vol.setValue (e.vol.load(), juce::dontSendNotification);
    const float f = e.freq.load(); const int mi = juce::roundToInt (proc.midiOf (f));
    const juce::String name = drum ? DrumBank::names()[e.timbre.load()] : keyNames()[(mi % 12 + 12) % 12] + juce::String (mi / 12 - 1) + "  " + SpectrumProcessor::timbreNames()[e.timbre.load()];
    selLabel.setText (name + "   " + hzText (f) + (e.pan.load() < -0.05f ? "   L" + juce::String (juce::roundToInt (-e.pan.load() * 100)) : e.pan.load() > 0.05f ? "   R" + juce::String (juce::roundToInt (e.pan.load() * 100)) : "   CENTER"), juce::dontSendNotification);
    selLabel.setColour (juce::Label::textColourId, drum ? drumColour : timbreColour (e.timbre.load()));
}

void SpectrumEditor::paint (juce::Graphics& g)
{
    g.fillAll (col::bg);
    auto b = getLocalBounds().toFloat();
    juce::ColourGradient haze (col::ecto.withAlpha (0.18f), 0, 0, col::bg.withAlpha (0.0f), b.getWidth() * 0.5f, b.getHeight() * 0.4f, true);
    g.setGradientFill (haze); g.fillRect (b);
    drawWordmark (g, { 20, 10, 300, 40 }, "SPECTRUM", wordGlitch);
    g.setColour (col::dim); g.setFont (font (10.0f));
    g.drawText ("PLAY SOUND", juce::Rectangle<float> (480, 58, 80, 24), juce::Justification::centredLeft);
    const juce::String hint = field.mode == SpectrumField::PlayMode ? "Click and slide on the field to play notes at their true frequency"
                            : field.mode == SpectrumField::ToneMode ? "Click the field to place a glowing tone at that exact frequency"
                                                                    : "Click the field to drop a drum where its energy lives";
    g.drawText (hint, juce::Rectangle<float> (712, 58, 390, 24), juce::Justification::centredRight);
    auto panel = juce::Rectangle<float> (14, 626, 1092, 156);
    g.setColour (col::card.withAlpha (0.9f)); g.fillRoundedRectangle (panel, 14);
    g.setColour (col::slime.withAlpha (0.25f)); g.drawRoundedRectangle (panel, 14, 1.0f);
    g.setColour (col::dim); g.drawText ("STEPS", juce::Rectangle<float> (28, 698, 50, 28), juce::Justification::centredLeft);
    g.drawText ("VOLUME", juce::Rectangle<float> (28, 736, 60, 24), juce::Justification::centredLeft);
    g.drawText ("Double-click a ghost to remove it. Drag the cyan LP / HP lines to filter. *TEMPO only outside a DAW.", juce::Rectangle<float> (700, 760, 400, 16), juce::Justification::centredRight);
}

void SpectrumEditor::resized()
{
    sceneBox.setBounds (330, 14, 200, 32); playBtn.setBounds (540, 14, 72, 32);
    modePlay.setBounds (630, 14, 110, 32); modeTone.setBounds (746, 14, 90, 32); modeDrum.setBounds (842, 14, 90, 32); drumPick.setBounds (938, 14, 162, 32);
    keyBox.setBounds (20, 56, 64, 28); scaleBox.setBounds (90, 56, 150, 28); refBox.setBounds (246, 56, 140, 28); snapBtn.setBounds (392, 56, 70, 28);
    playSoundBox.setBounds (560, 56, 140, 28);
    field.setBounds (20, 92, 1080, 526);
    selLabel.setBounds (28, 632, 520, 24);
    elSoundBox.setBounds (28, 662, 180, 28); droneBtn.setBounds (214, 662, 80, 28); delBtn.setBounds (300, 662, 90, 28);
    for (int i = 0; i < 16; ++i) stepBtns[i]->setBounds (80 + i * 36, 698, 33, 28);
    vol.setBounds (90, 736, 300, 24);
    for (int i = 0; i < knobs.size(); ++i) knobs[i]->setBounds (700 + i * 100, 632, 96, 120);
}

void SpectrumEditor::timerCallback()
{
    wordGlitch *= 0.85f;
    playBtn.setToggleState (proc.internalPlay.load() || proc.hostPlaying.load(), juce::dontSendNotification);
    playBtn.setButtonText (proc.internalPlay.load() ? "STOP" : "PLAY");
    const int sv = proc.sceneVersion.load();
    if (sv != lastScene) { lastScene = sv; field.selected = -1; refreshSelection(); sceneBox.setSelectedItemIndex (proc.getCurrentProgram(), juce::dontSendNotification); }
    const int ps = proc.playStep.load();
    for (int i = 0; i < 16; ++i)
    {
        const bool now = i == ps;
        stepBtns[i]->setColour (juce::TextButton::buttonColourId, now ? col::white.withAlpha (0.4f) : (i % 4 == 0 ? col::card2.brighter (0.15f) : col::card2));
    }
    repaint (0, 0, getWidth(), 92);
}
