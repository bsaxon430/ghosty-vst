#pragma once
// GHOST SPACE page (a tab of the rack panel, "G-SPACE"; also a shortcut button in the strip's free tile cell and an entry in the scene bar's
// "..." menu): the field (listener in the middle, facing up; drag the source dot, or DRAW a looping path), the PATH (STILL / ORBIT / FALL /
// GHOST / BLACK HOLE / DRAWN), its RATE, and the space (DEPTH, ROOM, AIR, REVERB, GHOST PITCH, TRAIL). The audio side is
// GhostyPro/fx/FxGhostSpace.h (SpaceStage, just before the limiter). Themed in all 4 looks.
#include "BoxProcessor.h"
#include "FxUi.h"
#include "FxRackImpl.h"
#include "GhostGlass.h"

class BoxSpacePage : public juce::Component, private juce::Timer
{
public:
    static constexpr juce::uint32 kAccent = 0xff9C8BFF;
    explicit BoxSpacePage (BoxProcessor& p) : proc (p), rack (*p.rack.impl), field (*this),
        onT (p.apvts, "fx_gspace_on", "SPACE ON", acc()), path (p.apvts, "fx_gspace_path", acc()), rate (p.apvts, "fx_gspace_rate", acc()), trail (p.apvts, "fx_gspace_trail", acc()),
        depth (p.apvts, "fx_gspace_depth", "DEPTH", acc()), room (p.apvts, "fx_gspace_room", "ROOM", acc()), air (p.apvts, "fx_gspace_air", "AIR", acc()),
        verb (p.apvts, "fx_gspace_verb", "REVERB", acc()), ghostK (p.apvts, "fx_gspace_ghost", "GHOST PITCH", acc())
    {
        onT.setTooltip ("GHOST SPACE on / off (20 ms crossfade). Off, the rack sounds exactly as without it");
        rate.setTooltip ("How long one ORBIT turn, one FALL or one BLACK HOLE cycle takes (in time with the song)");
        trail.setTooltip ("GHOST: how far behind the source its copy follows (and how late it sounds)");
        depth.slider.setTooltip ("ORBIT / GHOST: the circle's size; FALL: how far it falls; BLACK HOLE: how deep it is pulled in");
        room.slider.setTooltip ("The size of the space: the reflections' pattern and the reverb's size and length");
        air.slider.setTooltip ("How much the distance darkens the sound (air absorbs the highs)");
        verb.slider.setTooltip ("How much of the room you hear (reflections + reverb). The farther the source, the more of it");
        ghostK.slider.setTooltip ("GHOST: the copy's pitch in semitones (-12 = an octave below)");
        path.columns = 3;
        path.onChange = [this] (int) { repaint(); field.repaint(); };
        drawB.setButtonText ("DRAW"); drawB.setClickingTogglesState (true); drawB.setColour (juce::TextButton::buttonOnColourId, acc());
        drawB.setTooltip ("DRAW: click-drag in the field to draw a looping path (the timing is how you draw it); it plays as PATH DRAWN, in time with the song");
        clearB.setButtonText ("CLEAR PATH"); clearB.setColour (juce::TextButton::buttonOnColourId, acc());
        clearB.setTooltip ("Remove the drawn path");
        clearB.onClick = [this] { rack.pushUndo(); rack.setSpacePath ({}); refresh(); };
        barsBox.addItemList ({ "1 BAR", "2 BARS", "4 BARS", "8 BARS" }, 1); barsBox.setSelectedItemIndex (1, juce::dontSendNotification);
        barsBox.setTooltip ("The drawn path's loop length (it repeats in time with the song)");
        barsBox.setColour (juce::ComboBox::outlineColourId, acc());
        barsBox.onChange = [this] { auto pth = rack.space.drawnPath; if (! pth.empty()) { rack.pushUndo(); pth.bars = drawBars(); pth.normalise(); rack.setSpacePath (pth); } refresh(); };
        for (auto* c : std::initializer_list<juce::Component*> { &field, &onT, &path, &rate, &trail, &depth, &room, &air, &verb, &ghostK, &drawB, &clearB, &barsBox }) addAndMakeVisible (c);
        refresh();
        startTimerHz (30);
    }
    ~BoxSpacePage() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        using namespace gpfx::ui;
        const auto ink2 = ghost() ? ghosty::glass::ink2() : col::dim.get();
        drawCaption (g, pathCap.toFloat(), "PATH", ink2, 10.0f);
        drawCaption (g, rateCap.toFloat(), "RATE", ink2, 10.0f);
        drawCaption (g, trailCap.toFloat(), "TRAIL", ink2, 10.0f);
        drawCaption (g, infoR.toFloat(), infoText(), ghost() ? ghosty::glass::violetDeep() : acc().brighter (0.2f), 10.5f);
    }
    void resized() override
    {
        auto r = getLocalBounds().reduced (6, 2);
        infoR = r.removeFromBottom (18); r.removeFromBottom (4);
        const int fs = juce::jmin (r.getHeight(), r.getWidth() / 2);
        field.setBounds (r.removeFromLeft (fs + 120).withSizeKeepingCentre (fs + 120, fs));
        r.removeFromLeft (14);
        auto row = [&r] (int h) { auto x = r.removeFromTop (h); r.removeFromTop (6); return x; };
        auto r1 = row (28); onT.setBounds (r1.removeFromLeft (110).reduced (0, 1)); r1.removeFromLeft (10);
        drawB.setBounds (r1.removeFromLeft (70).reduced (0, 1)); r1.removeFromLeft (6); barsBox.setBounds (r1.removeFromLeft (90).reduced (0, 2)); r1.removeFromLeft (6);
        clearB.setBounds (r1.removeFromLeft (100).reduced (0, 1));
        pathCap = row (12); path.setBounds (row (60).withWidth (juce::jmin (420, r.getWidth())));
        auto r3 = row (26); rateCap = r3.removeFromLeft (36); rate.setBounds (r3.removeFromLeft (110).reduced (0, 1)); r3.removeFromLeft (16);
        trailCap = r3.removeFromLeft (40); trail.setBounds (r3.removeFromLeft (90).reduced (0, 1));
        auto r4 = row (84);
        for (auto* k : std::initializer_list<juce::Component*> { &depth, &room, &air, &verb, &ghostK }) { k->setBounds (r4.removeFromLeft (84)); r4.removeFromLeft (4); }
    }
    // tests
    struct Text { juce::Rectangle<int> r; juce::Colour c; juce::String what; };
    std::vector<Text> texts() const
    {
        using namespace gpfx::ui; std::vector<Text> v;
        const auto ink2 = ghost() ? ghosty::glass::ink2() : col::dim.get();
        v.push_back ({ pathCap.withWidth (40).withSizeKeepingCentre (40, 9), ink2, "PATH" });
        v.push_back ({ rateCap.withSizeKeepingCentre (rateCap.getWidth(), 9), ink2, "RATE" });
        v.push_back ({ field.behindRect().translated (field.getX(), field.getY()), field.labelColour(), "BEHIND (on the field)" });
        return v;
    }
    juce::Component& fieldView() { return field; }
    void drawStroke (const std::vector<juce::Point<float>>& pts, const std::vector<double>& secs) { field.finishStroke (pts, secs); }   // (normalised field points)
    void setDrawMode (bool on) { drawB.setToggleState (on, juce::dontSendNotification); }

private:
    static juce::Colour acc() { return fxtheme::T (juce::Colour (kAccent)); }
    int drawBars() const { static const int b[] { 1, 2, 4, 8 }; return b[juce::jlimit (0, 3, barsBox.getSelectedItemIndex())]; }
    float param (const char* id) const { auto* v = proc.apvts.getRawParameterValue (id); return v != nullptr ? v->load() : 0.0f; }
    juce::String infoText() const
    {
        const bool on = param ("fx_gspace_on") > 0.5f;
        const float x = rack.space.running.load() ? rack.space.liveX.load() : param ("fx_gspace_x"), y = rack.space.running.load() ? rack.space.liveY.load() : param ("fx_gspace_y");
        const int deg = juce::roundToInt (gpfx::spaceAngle (x, y) * 180.0f / juce::MathConstants<float>::pi);
        juce::String s = on ? "" : "SPACE is OFF (the sound is unchanged).  ";
        s << "Distance " << juce::roundToInt (gpfx::spaceDistance (x, y) * 100.0f) << " %, " << (deg == 0 ? juce::String ("straight ahead") : juce::String (std::abs (deg)) + (deg > 0 ? " deg right" : " deg left"))
          << (std::abs (deg) > 90 ? " (behind)" : "") << ".  ";
        if (juce::roundToInt (param ("fx_gspace_path")) == gpfx::spDrawn && rack.space.drawnPath.empty()) s << "DRAWN has no path yet: switch DRAW on and draw one in the field.";
        else s << "Drag the dot, or pick a PATH. The reverb, reflections, pitch and freeze are the rack's own.";
        return s;
    }
    void refresh() { clearB.setEnabled (! rack.space.drawnPath.empty()); if (! rack.space.drawnPath.empty()) { const int b = rack.space.drawnPath.bars; barsBox.setSelectedItemIndex (b >= 8 ? 3 : b >= 4 ? 2 : b >= 2 ? 1 : 0, juce::dontSendNotification); } repaint(); field.repaint(); }
    void timerCallback() override { field.repaint(); if (++slow % 10 == 0) { refresh(); } }

    // ---------------------------------------------------------------- the field
    struct Field : juce::Component, juce::SettableTooltipClient
    {
        explicit Field (BoxSpacePage& o) : page (o) { setTooltip ("The field: you are in the middle, facing up. Drag the dot to place the sound (farther = quieter, darker, more room); with DRAW on, draw a looping path"); }
        BoxSpacePage& page;
        std::vector<juce::Point<float>> stroke; std::vector<double> strokeT; double t0 = 0; bool dragging = false;
        juce::Rectangle<float> sq() const { const float s = (float) juce::jmin (getWidth(), getHeight()); return juce::Rectangle<float> (s, s).withCentre (getLocalBounds().toFloat().getCentre()).reduced (4.0f); }
        juce::Point<float> toScreen (float x, float y) const { const auto r = sq(); return { r.getX() + x * r.getWidth(), r.getY() + y * r.getHeight() }; }
        juce::Point<float> toField (juce::Point<float> p) const { const auto r = sq(); return { juce::jlimit (0.0f, 1.0f, (p.x - r.getX()) / r.getWidth()), juce::jlimit (0.0f, 1.0f, (p.y - r.getY()) / r.getHeight()) }; }
        juce::Rectangle<int> behindRect() const { const auto r = sq(); return juce::Rectangle<float> (r.getCentreX() - 30, r.getBottom() - 16, 60, 12).toNearestInt().withSizeKeepingCentre (36, 9); }
        juce::Colour labelColour() const { return gpfx::ui::ghost() ? ghosty::glass::ink() : gpfx::ui::col::white.get(); }
        void paint (juce::Graphics& g) override
        {
            using namespace gpfx::ui;
            const bool gl = ghost();
            const auto r = sq(); const auto c = r.getCentre(); const auto a = acc();
            drawScreen (g, r, a, 12.0f);
            const auto line = gl ? ghosty::glass::ink2().withAlpha (0.35f) : col::dim.get().withAlpha (0.35f);
            g.setColour (line);
            for (float k : { 0.25f, 0.5f, 0.75f, 1.0f }) { const float rr = k * r.getWidth() * 0.5f; g.drawEllipse (c.x - rr, c.y - rr, rr * 2, rr * 2, k == 1.0f ? 1.2f : 0.7f); }
            g.drawLine (r.getX() + 6, c.y, r.getRight() - 6, c.y, 0.6f); g.drawLine (c.x, r.getY() + 6, c.x, r.getBottom() - 6, 0.6f);
            g.setColour (labelColour()); g.setFont (font (10.0f));
            g.drawText ("FRONT", juce::Rectangle<float> (c.x - 30, r.getY() + 4, 60, 12), juce::Justification::centred, false);
            g.drawText ("BEHIND", juce::Rectangle<float> (c.x - 30, r.getBottom() - 16, 60, 12), juce::Justification::centred, false);
            g.drawText ("L", juce::Rectangle<float> (r.getX() + 4, c.y - 14, 12, 12), juce::Justification::centred, false);
            g.drawText ("R", juce::Rectangle<float> (r.getRight() - 16, c.y - 14, 12, 12), juce::Justification::centred, false);
            auto& sp = page.rack.space;
            const int pm = juce::roundToInt (page.param ("fx_gspace_path"));
            // the path preview (one cycle of the pure movement the audio plays)
            gpfx::SpaceMove mv; mv.path = pm; mv.x = page.param ("fx_gspace_x"); mv.y = page.param ("fx_gspace_y"); mv.depth = page.param ("fx_gspace_depth");
            mv.periodBeats = 4.0; mv.trailBeats = gpfx::spaceTrailBeats (juce::roundToInt (page.param ("fx_gspace_trail")));
            std::array<float, 2 * gpfx::SpacePathTables::kN> tab {};
            if (pm == gpfx::spDrawn && ! sp.drawnPath.empty()) { for (int k = 0; k < gpfx::SpacePathTables::kN; ++k) { const auto q = sp.drawnPath.at ((double) k / gpfx::SpacePathTables::kN); tab[(size_t) (2 * k)] = q.x; tab[(size_t) (2 * k + 1)] = q.y; } mv.drawn = tab.data(); mv.drawnN = gpfx::SpacePathTables::kN; mv.drawnBeats = 4.0; }
            if (pm != gpfx::spStill && ! (pm == gpfx::spDrawn && sp.drawnPath.empty()))
            {
                juce::Path pth;
                for (int k = 0; k <= 160; ++k) { const auto q = gpfx::spacePositionAt (mv, 4.0 * k / 160.0); const auto s = toScreen (q.x, q.y); if (k == 0) pth.startNewSubPath (s); else pth.lineTo (s); }
                glowStroke (g, pth, a.withAlpha (0.7f), 1.4f, gl ? 0.0f : 0.6f);
            }
            if (! stroke.empty())
            {
                juce::Path pth; for (size_t k = 0; k < stroke.size(); ++k) { const auto s = toScreen (stroke[k].x, stroke[k].y); if (k == 0) pth.startNewSubPath (s); else pth.lineTo (s); }
                glowStroke (g, pth, a, 2.0f, gl ? 0.0f : 1.0f);
            }
            // the black hole at the centre (while swallowing)
            const float sw = sp.running.load() ? sp.liveSwallow.load() : 0.0f;
            if (sw > 0.01f) { const float rr = 6.0f + 26.0f * sw; g.setColour ((gl ? ghosty::glass::ink() : juce::Colours::black).withAlpha (0.55f * sw)); g.fillEllipse (c.x - rr, c.y - rr, rr * 2, rr * 2); }
            // the listener (a head facing up)
            {
                const auto hc = gl ? ghosty::glass::ink() : col::white.get();
                g.setColour (hc.withAlpha (0.85f)); g.fillEllipse (c.x - 9, c.y - 9, 18, 18);
                juce::Path nose; nose.addTriangle (c.x - 5, c.y - 7, c.x + 5, c.y - 7, c.x, c.y - 15); g.fillPath (nose);
            }
            // the set place (ring), the live place (dot), the ghost copy
            const auto set = toScreen (mv.x, mv.y);
            g.setColour (a.withAlpha (0.9f)); g.drawEllipse (set.x - 9, set.y - 9, 18, 18, 1.6f);
            if (sp.running.load())
            {
                const auto live = toScreen (sp.liveX.load(), sp.liveY.load());
                glowDot (g, live, 6.5f * (1.0f - 0.6f * sw), a);
                if (sp.liveGX.load() >= 0.0f) { const auto gh = toScreen (sp.liveGX.load(), sp.liveGY.load()); glowDot (g, gh, 4.5f, a.withAlpha (0.45f)); }
            }
            else glowDot (g, set, 5.0f, a.withAlpha (0.6f));
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            dragging = true;
            if (page.drawB.getToggleState()) { stroke.clear(); strokeT.clear(); t0 = juce::Time::getMillisecondCounterHiRes(); addPoint (e.position); return; }
            for (auto* id : { "fx_gspace_x", "fx_gspace_y" }) if (auto* q = page.proc.apvts.getParameter (id)) q->beginChangeGesture();
            moveDot (e.position);
        }
        void mouseDrag (const juce::MouseEvent& e) override { if (! dragging) return; if (page.drawB.getToggleState()) addPoint (e.position); else moveDot (e.position); }
        void mouseUp (const juce::MouseEvent&) override
        {
            if (! dragging) return; dragging = false;
            if (page.drawB.getToggleState()) { auto p = stroke; auto t = strokeT; finishStroke (p, t); return; }
            for (auto* id : { "fx_gspace_x", "fx_gspace_y" }) if (auto* q = page.proc.apvts.getParameter (id)) q->endChangeGesture();
        }
        void addPoint (juce::Point<float> p) { stroke.push_back (toField (p)); strokeT.push_back ((juce::Time::getMillisecondCounterHiRes() - t0) * 0.001); repaint(); }
        void moveDot (juce::Point<float> p)
        {
            const auto f = toField (p);
            if (auto* q = page.proc.apvts.getParameter ("fx_gspace_x")) q->setValueNotifyingHost (q->convertTo0to1 (f.x));
            if (auto* q = page.proc.apvts.getParameter ("fx_gspace_y")) q->setValueNotifyingHost (q->convertTo0to1 (f.y));
            repaint();
        }
        void finishStroke (const std::vector<juce::Point<float>>& pts, const std::vector<double>& secs)
        {
            stroke.clear(); strokeT.clear();
            auto pth = ghosty::spirit::OrbitPath::fromStroke (pts, secs, page.drawBars());
            if (pth.empty()) { repaint(); return; }
            page.rack.pushUndo(); page.rack.setSpacePath (pth);
            if (auto* q = page.proc.apvts.getParameter ("fx_gspace_path")) q->setValueNotifyingHost (q->convertTo0to1 ((float) gpfx::spDrawn));
            if (auto* q = page.proc.apvts.getParameter ("fx_gspace_on")) if (q->getValue() < 0.5f) q->setValueNotifyingHost (1.0f);
            page.refresh();
        }
    };

    BoxProcessor& proc; gpfx::Rack::Impl& rack; Field field;
    gpfx::ui::Toggle onT; gpfx::ui::Segmented path; gpfx::ui::Choice rate, trail; gpfx::ui::Knob depth, room, air, verb, ghostK;
    juce::TextButton drawB, clearB; juce::ComboBox barsBox;
    juce::Rectangle<int> pathCap, rateCap, trailCap, infoR; int slow = 0;
};
