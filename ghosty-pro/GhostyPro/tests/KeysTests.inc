// KEYS (round 3): the PLAY / FLOW keyboard strip - readable note labels, HOLD (latch), the ghost cursor.
//   ProTests keys [dir]   -> label contrast (formula over every key / flash / level, and measured on the rendered strip in
//                            BLACK / PURPLE / LIGHT), HOLD button placement, ghost cursor; screenshots into dir
//   ProTests hold         -> the latch on the processor: held after release, HOLD off lets go, ARP / MONO, MIDI all-notes-off
#include "ProKeysExtras.h"

namespace keystest
{
static void savePng (const juce::Image& img, const juce::File& f)
{
    f.deleteFile();
    if (auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream())) juce::PNGImageFormat().writeImageToStream (img, *os);
}
static juce::Component* findNamed (juce::Component& root, const juce::String& name)
{
    for (auto* k : root.getChildren()) { if (k->getName() == name) return k; if (auto* r = findNamed (*k, name)) return r; }
    return nullptr;
}
template <typename T> static T* findType (juce::Component& root)
{
    for (auto* k : root.getChildren()) { if (auto* t = dynamic_cast<T*> (k)) return t; if (auto* r = findType<T> (*k)) return r; }
    return nullptr;
}
// the strip's notes, as the strip builds them: three octaves of the scale from the key at C3
static std::vector<int> stripNotes (ProProcessor& p)
{
    const int key = (int) p.keyP->load(), mask = ghosty::scaleMask ((int) p.scaleP->load());
    std::vector<int> v; for (int n = 48 + key; n <= 84 + key && v.size() < 64; ++n) if (ghosty::inScale (n, key, mask)) v.push_back (n);
    return v;
}

static int labelTests (const juce::File& dir)
{
    section ("KEYS 1: FLOW strip note labels read on every key (>= 4.5:1) in BLACK / PURPLE / LIGHT; the key colours stay");
    {   // every key colour, every flash level, every output level the strip can paint
        double lo = 99, oldLo = 99; juce::String worst;
        for (int d = 0; d < 12; ++d)
            for (int fi = 0; fi <= 20; ++fi)
                for (int li = 0; li <= 15; ++li)
                {
                    const float fl = fi / 20.0f, lv = li / 10.0f;
                    const auto c = keysx::keyColour (d);
                    const auto behind = keysx::keyBehind (c, fl, lv, keysx::labelYFrac (90.0f));
                    const double r = keysx::contrast (keysx::labelInk (behind), behind);
                    if (r < lo) { lo = r; worst = ghosty::keyNames()[d] + " flash " + juce::String (fl, 2) + " level " + juce::String (lv, 1); }
                    oldLo = std::min (oldLo, keysx::contrast (behind.overlaidWith (c.withAlpha (0.5f + fl * 0.5f)), behind));   // the old label: the key colour itself
                }
        CHECK (lo >= 4.5, "label ink vs the key behind it, over 12 key colours x flash 0..1 x level 0..1.5: lowest %.2f:1 (%s); the old key-coloured labels went down to %.2f:1",
               lo, worst.toRawUTF8(), oldLo);
    }
    static const char* themeName[] { "black", "purple", "light" };
    auto p = makeProc (progOf ("Trap Starter Stack"));
    render (*p, { { 0.0, 48, 0.9f, true }, { 0.0, 60, 0.9f, true }, { 0.0, 67, 0.9f, true } }, 0.4);   // a few keys lit
    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
    auto* pe = dynamic_cast<ProEditor*> (ed.get());
    pe->setSize (ProEditor::W, ProEditor::H); pe->showTab (0); pump (100);
    auto* strip = findNamed (*pe, "FLOW");
    bool visible = strip != nullptr; for (auto* c = strip; c != nullptr && c != pe; c = c->getParentComponent()) visible &= c->isVisible();
    CHECK (visible, "the FLOW strip is on the SYNTH page");
    if (strip == nullptr) return 1;
    const auto notes = stripNotes (*p);
    for (int t = 0; t < 3; ++t)
    {
        pe->setTheme (t); pump (150);
        auto img = strip->createComponentSnapshot (strip->getLocalBounds(), true, 1.0f);
        savePng (img, dir.getChildFile (juce::String ("labels-after-") + themeName[t] + ".png"));
        // measured on the pixels: for every key, the key colour just left of the label vs the label's strongest glyph pixel
        const float cw = (float) img.getWidth() / (float) notes.size();
        const int h = img.getHeight(), yc = h - 9;
        double lo = 99; int worstKey = -1, measured = 0;
        for (int i = 0; i < (int) notes.size(); ++i)
        {
            const int x0 = (int) std::ceil (i * cw) + 2;
            const auto bg = img.getPixelAt (x0, yc);
            double best = 1.0;
            for (int y = h - 15; y < h - 3; ++y)
                for (int x = (int) (i * cw + cw * 0.25f); x < (int) (i * cw + cw * 0.75f); ++x)
                    best = std::max (best, keysx::contrast (img.getPixelAt (x, y), bg));
            ++measured;
            if (best < lo) { lo = best; worstKey = i; }
        }
        CHECK (lo >= 4.5 && measured == (int) notes.size(), "%s: rendered labels on %d keys, lowest %.2f:1 (key %s)", themeName[t], measured, lo,
               worstKey >= 0 ? ghosty::keyNames()[notes[(size_t) worstKey] % 12].toRawUTF8() : "-");
    }
    pe->setTheme (0); pump (50);
    return 0;
}

// run the processor for a while (UI notes arrive through the keyboard state, MIDI through the block); rms of the last 0.2 s in dB
static double run (ProProcessor& p, double secs, juce::MidiMessage first = {}, bool sendFirst = false)
{
    const int total = (int) (secs * SR); juce::AudioBuffer<float> blk (2, BS); double acc = 0; int cnt = 0;
    for (int pos = 0; pos < total; pos += BS)
    {
        juce::MidiBuffer mb; if (sendFirst && pos == 0) mb.addEvent (first, 0);
        blk.setSize (2, BS, false, false, true); p.processBlock (blk, mb);
        if (pos >= total - (int) (0.2 * SR)) for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < BS; ++i) { acc += blk.getSample (ch, i) * blk.getSample (ch, i); ++cnt; }
    }
    return cnt ? juce::Decibels::gainToDecibels (std::sqrt (acc / cnt), -150.0) : -150.0;
}

static int holdTests()
{
    section ("KEYS 2: HOLD (latch) on the FLOW strip: held after release, a held note played again lets go, HOLD off lets all go, ARP / MONO, MIDI all-notes-off");
    auto p = makeProc (progOf ("Trap Starter Stack"));
    p->setParam ("sustain", 1.0f);
    keysx::NoteLatch latch (p->keyboardState);
    {   // HOLD off: the strip as before
        const bool s = latch.press (60, 0.8f); run (*p, 0.2); latch.release (60, s); run (*p, 0.6);
        CHECK (s && ! p->isNoteSounding (60) && ! p->keyboardState.isNoteOn (1, 60), "HOLD off: note-on, release -> released, as before");
    }
    latch.setHold (true);
    {
        const bool s = latch.press (60, 0.8f); run (*p, 0.2); latch.release (60, s);
        const double tail = run (*p, 1.0);
        CHECK (s && p->isNoteSounding (60) && latch.isHeld (60) && tail > -60, "HOLD on: note-on, release -> still sounding 1 s later (%.1f dB)", tail);
        const bool s2 = latch.press (60, 0.8f); run (*p, 0.2); latch.release (60, s2); run (*p, 0.2);
        CHECK (! s2 && ! p->isNoteSounding (60) && ! latch.isHeld (60), "HOLD on: playing the held note again lets it go");
    }
    {
        for (int n : { 60, 64, 67 }) { const bool s = latch.press (n, 0.8f); run (*p, 0.05); latch.release (n, s); }
        run (*p, 0.5);
        const bool chordHeld = p->isNoteSounding (60) && p->isNoteSounding (64) && p->isNoteSounding (67) && latch.numHeld() == 3;
        latch.setHold (false); run (*p, 0.2);
        CHECK (chordHeld && latch.numHeld() == 0 && ! p->isNoteSounding (60) && ! p->isNoteSounding (64) && ! p->isNoteSounding (67) && ! p->keyboardState.isNoteOn (1, 64),
               "HOLD off releases all (3 held -> 0, nothing sounding)");
    }
    {   // ARP: the held notes feed it
        p->setParam ("arp", 1.0f); latch.setHold (true);
        for (int n : { 60, 67 }) { const bool s = latch.press (n, 0.8f); run (*p, 0.05); latch.release (n, s); }
        int ons = 0; double lvl = -150;
        for (int k = 0; k < 20; ++k) { lvl = std::max (lvl, run (*p, 0.1)); ons += p->isNoteSounding (60) || p->isNoteSounding (67) || p->isNoteSounding (72) || p->isNoteSounding (79); }
        latch.setHold (false); run (*p, 0.3);
        bool after = false; for (int n = 0; n < 128; ++n) after |= p->isNoteSounding (n);
        CHECK (ons > 0 && lvl > -60 && ! after, "ARP + HOLD: the arp keeps playing the held notes after release (%d of 20 looks sounding, %.1f dB); HOLD off stops it", ons, lvl);
        p->setParam ("arp", 0.0f); run (*p, 0.2);
    }
    {   // MONO / LEGATO
        p->setParam ("mono", 1.0f); p->setParam ("legato", 1.0f); latch.setHold (true);
        const bool s = latch.press (62, 0.8f); run (*p, 0.1); latch.release (62, s);
        const double tail = run (*p, 0.8);
        CHECK (p->isNoteSounding (62) && tail > -60, "MONO + LEGATO + HOLD: held after release (%.1f dB)", tail);
        latch.setHold (false); run (*p, 0.2);
        CHECK (! p->isNoteSounding (62), "MONO + LEGATO: HOLD off lets go");
        p->setParam ("mono", 0.0f); p->setParam ("legato", 0.0f); run (*p, 0.2);
    }
    {   // MIDI panic: all-notes-off still silences latched notes
        latch.setHold (true);
        for (int n : { 60, 64 }) { const bool s = latch.press (n, 0.8f); run (*p, 0.05); latch.release (n, s); }
        const double held = run (*p, 0.4);
        run (*p, 0.1, juce::MidiMessage::allNotesOff (1), true);
        const bool gone = ! p->isNoteSounding (60) && ! p->isNoteSounding (64);
        const double after = run (*p, 3.0);
        CHECK (gone && held > -60 && after < held - 40, "MIDI all-notes-off with HOLD on: notes stop (%.1f dB -> %.1f dB after 3 s)", held, after);
        run (*p, 0.1, juce::MidiMessage::allSoundOff (1), true);
        latch.setHold (false); run (*p, 0.2);
        bool any = false; for (int n = 0; n < 128; ++n) any |= p->isNoteSounding (n);
        CHECK (! any && latch.numHeld() == 0, "after the panic, HOLD off leaves nothing hanging");
    }
    {   // a latched note never outlives the strip (closing the editor)
        auto l2 = std::make_unique<keysx::NoteLatch> (p->keyboardState); l2->setHold (true);
        const bool s = l2->press (65, 0.8f); run (*p, 0.05); l2->release (65, s); run (*p, 0.2);
        const bool was = p->isNoteSounding (65);
        l2.reset(); run (*p, 0.2);
        CHECK (was && ! p->isNoteSounding (65), "closing the editor lets latched notes go");
    }
    return 0;
}

static int holdUi (const juce::File& dir)
{
    section ("KEYS 2: HOLD + OCT - / + in the PLAY row: free space at its right end, no overlap, lit when on, session only");
    auto p = makeProc (progOf ("Trap Starter Stack"));
    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
    auto* pe = dynamic_cast<ProEditor*> (ed.get());
    pe->setSize (ProEditor::W, ProEditor::H); pe->showTab (0); pump (100);
    auto* hb = findType<keysx::HoldButton> (*pe); auto* oc = findType<keysx::OctaveControl> (*pe); auto* strip = findNamed (*pe, "FLOW");
    CHECK (hb != nullptr && oc != nullptr && strip != nullptr, "HOLD button and OCT control exist");
    if (hb == nullptr || oc == nullptr || strip == nullptr) return 1;
    juce::StringArray hits;
    for (auto* c : hb->getParentComponent()->getChildren())
        for (auto* mine : { (juce::Component*) hb, (juce::Component*) oc })
            if (c != hb && c != oc && c->isVisible() && c->getName() != "MOD OVERLAY" && c->getBounds().intersects (mine->getBounds()))
                hits.add (c->getName().isEmpty() ? juce::String (typeid (*c).name()) : c->getName());
    CHECK (hits.isEmpty() && hb->getTooltip().isNotEmpty(), "HOLD at %d,%d %dx%d, OCT at %d,%d %dx%d; overlaps: %s; tooltip set",
           hb->getX(), hb->getY(), hb->getWidth(), hb->getHeight(), oc->getX(), oc->getY(), oc->getWidth(), oc->getHeight(), hits.isEmpty() ? "none" : hits.joinIntoString (", ").toRawUTF8());
    juce::Rectangle<int> area;
    auto shot = [&] (const char* name) { pump (120); savePng (pe->createComponentSnapshot (area, true, 1.0f), dir.getChildFile (name)); };
    // the editor scales its content as one piece: map the PLAY card area into editor pixels
    area = pe->getLocalArea (hb->getParentComponent(), hb->getBounds().getUnion (oc->getBounds()).getUnion (strip->getBounds()).expanded (8, 6));
    for (int t : { 0, 1, 2 })
    {
        static const char* tn[] { "black", "purple", "light" };
        pe->setTheme (t);
        shot ((juce::String ("hold-off-") + tn[t] + ".png").toRawUTF8());
        hb->triggerClick(); pump (30);
        for (int n : { 60, 64, 67 }) { auto& l = hb->getLatch(); l.release (n, l.press (n, 0.8f)); }   // a chord latched from the strip
        shot ((juce::String ("hold-on-") + tn[t] + ".png").toRawUTF8());
        hb->triggerClick(); pump (30);
    }
    pe->setTheme (0);
    const auto stateBefore = [&] { juce::MemoryBlock mb; p->getStateInformation (mb); return mb; }();
    hb->triggerClick(); pump (30);
    const bool on = hb->getToggleState();
    const auto stateOn = [&] { juce::MemoryBlock mb; p->getStateInformation (mb); return mb; }();
    hb->triggerClick(); pump (30);
    CHECK (on && ! hb->getToggleState() && stateBefore == stateOn, "HOLD toggles (lit when on) and is not in the saved state");
    oc->shift (1); pump (30);
    CHECK (oc->up.isEnabled() && oc->down.isEnabled(), "OCT + steps up (both buttons live at +1)");
    oc->shift (5); CHECK (! oc->up.isEnabled(), "OCT stops at +2"); oc->shift (-9); CHECK (! oc->down.isEnabled(), "OCT stops at -2"); oc->shift (2);
    return 0;
}

static int cursorTests (const juce::File& dir)
{
    section ("KEYS 3: the ghost cursor over the FLOW strip: follows with lag, speed lines, glow, glitch, press squash + rings + burst, fades out, never takes the mouse");
    auto p = makeProc (progOf ("Trap Starter Stack"));
    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
    auto* pe = dynamic_cast<ProEditor*> (ed.get());
    pe->setSize (ProEditor::W, ProEditor::H); pe->showTab (0); pump (100);
    auto* gc = findType<keysx::GhostCursor> (*pe); auto* strip = findNamed (*pe, "FLOW");
    CHECK (gc != nullptr && strip != nullptr && gc->getParentComponent() == strip && gc->getBounds() == strip->getLocalBounds(), "the cursor overlay covers the strip");
    if (gc == nullptr || strip == nullptr) return 1;
    bool selfClicks = true, kidClicks = true; gc->getInterceptsMouseClicks (selfClicks, kidClicks);
    const auto mid = strip->getLocalBounds().getCentre();
    CHECK (! selfClicks && ! kidClicks && strip->getComponentAt (mid) == strip, "it never takes a mouse event (the strip still gets the clicks under him)");
    CHECK (! gc->isShowing() && ! gc->isAnimating(), "hidden and no timer while the mouse is elsewhere");
    gc->hoverForTest ({ 100.0f, 50.0f });
    for (int k = 0; k < 20; ++k) gc->stepForTest();
    const auto settled = gc->ghostPos();
    CHECK (gc->isShowing() && gc->isAnimating() && settled.getDistanceFrom ({ 111.0f, 38.0f }) < 3.0f, "hovering: shown, timer on, he settles up-right of the pointer (%.0f, %.0f)", settled.x, settled.y);
    gc->moveForTest ({ 400.0f, 50.0f }); gc->stepForTest();
    const float lag = gc->ghostPos().getDistanceFrom ({ 411.0f, 38.0f });
    gc->stepForTest(); gc->stepForTest();
    const float sp = gc->speedNow();
    CHECK (lag > 100.0f && sp > 2.5f, "a fast move: he lags behind (%.0f px after one frame) and moves fast enough for speed lines (%.1f px/frame)", lag, sp);
    auto zoom = [&] (const char* name, int scale)
    {
        auto pt = pe->getLocalPoint (strip, gc->ghostPos().toInt());
        auto area = juce::Rectangle<int> (pt.x - 70, pt.y - 40, 140, 80).getIntersection (pe->getLocalArea (strip, strip->getLocalBounds()));
        savePng (pe->createComponentSnapshot (area, true, (float) scale), dir.getChildFile (name));
    };
    zoom ("ghost-speedlines.png", 4);
    for (int k = 0; k < 25; ++k) gc->stepForTest();
    for (int t : { 0, 1, 2 })
    {
        static const char* tn[] { "black", "purple", "light" };
        pe->setTheme (t); pump (30);
        zoom ((juce::String ("ghost-cursor-") + tn[t] + ".png").toRawUTF8(), 4);
    }
    pe->setTheme (0); pump (30);
    gc->forceGlitch (2);
    CHECK (gc->isGlitching(), "a forced glitch shows");
    zoom ("ghost-glitch.png", 4);
    savePng (pe->createComponentSnapshot (pe->getLocalArea (strip, strip->getLocalBounds()), true, 1.0f), dir.getChildFile ("ghost-glitch-strip.png"));
    gc->stepForTest(); gc->stepForTest(); gc->stepForTest();
    CHECK (! gc->isGlitching(), "the glitch lasts 1-2 frames");
    {   // he glitches on his own every 1.5 .. 3 s
        int glitches = 0; bool was = false;
        for (int k = 0; k < 30 * 30; ++k) { gc->moveForTest ({ 400.0f, 50.0f }); gc->stepForTest(); const bool g = gc->isGlitching(); glitches += g && ! was; was = g; }
        CHECK (glitches >= 9 && glitches <= 21, "in 30 s he glitched %d times (one every 1.5 .. 3 s)", glitches);
    }
    gc->setShadesForTest (true); for (int k = 0; k < 3; ++k) gc->stepForTest();
    zoom ("ghost-sunglasses.png", 4);
    gc->setShadesForTest (false);
    gc->pressForTest(); for (int k = 0; k < 4; ++k) gc->stepForTest();
    zoom ("ghost-press-rings.png", 3);
    gc->leaveForTest();
    for (int k = 0; k < 40; ++k) gc->stepForTest();
    CHECK (! gc->isShowing() && ! gc->isAnimating(), "mouse gone: he fades out and the timer stops");
    return 0;
}
} // namespace keystest
