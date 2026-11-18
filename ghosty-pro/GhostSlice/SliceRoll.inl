// GHOST SLICE build 8 - the editable roll and PLAY IT (UNDER THE HOOD). Included by SliceEditor.cpp after its drawing helpers.
// Declarations: SliceEditor.h (PlaySeg, NoteRoll, PlayPanel). The music side: PlayEdit.h.

// ---------------------------------------------------------------- PlaySeg
juce::Rectangle<float> PlaySeg::cell (int i) const
{
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float w = r.getWidth() / (float) juce::jmax (1, items.size());
    return { r.getX() + w * (float) i, r.getY(), w, r.getHeight() };
}
void PlaySeg::paint (juce::Graphics& g)
{
    if (S == nullptr) return;
    const auto& K = *S;
    const auto r = getLocalBounds().toFloat().reduced (1.0f);
    const float rad = juce::jmin (9.0f, r.getHeight() * 0.5f);
    const float alpha = isEnabled() ? 1.0f : 0.38f;
    g.setColour (K.well.withMultipliedAlpha (alpha)); g.fillRoundedRectangle (r, rad);
    g.setColour (K.panelLine.withMultipliedAlpha (alpha)); g.drawRoundedRectangle (r.reduced (0.5f), rad, 1.0f);
    const auto f = capFont (cap, Weight::SemiBold, 0.08f);
    for (int i = 0; i < items.size(); ++i)
    {
        const auto c = cell (i).reduced (2.0f, 2.0f);
        const bool on = momentary ? pressed == i : sel == i;
        if (on)
        {
            glow (g, c, rad - 2.0f, K.a, 6.0f, 3, 0.08f * alpha);
            g.setGradientFill (juce::ColourGradient (K.a.withAlpha (0.55f * alpha), c.getX(), c.getY(), K.b.withAlpha (0.35f * alpha), c.getRight(), c.getBottom(), false));
            g.fillRoundedRectangle (c, rad - 2.0f);
            g.setColour (K.a2.withAlpha (0.9f * alpha)); g.drawRoundedRectangle (c.reduced (0.5f), rad - 2.0f, 1.0f);
        }
        else if (i == hover && isEnabled()) { g.setColour (K.a.withAlpha (0.12f)); g.fillRoundedRectangle (c, rad - 2.0f); }
        g.setColour ((on ? K.ink : K.dim).withMultipliedAlpha (alpha));
        g.setFont (f); g.drawFittedText (items[i], c.toNearestInt(), juce::Justification::centred, 1, 0.6f);
    }
}
void PlaySeg::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled() || items.isEmpty()) return;
    const int i = juce::jlimit (0, items.size() - 1, (int) (e.position.x / juce::jmax (1.0f, (float) getWidth()) * (float) items.size()));
    if (momentary) { pressed = i; repaint(); if (onChange) onChange (i); return; }
    if (i != sel) { sel = i; repaint(); if (onChange) onChange (i); }
}
void PlaySeg::mouseMove (const juce::MouseEvent& e)
{
    const int i = items.isEmpty() ? -1 : juce::jlimit (0, items.size() - 1, (int) (e.position.x / juce::jmax (1.0f, (float) getWidth()) * (float) items.size()));
    if (i != hover) { hover = i; repaint(); }
}

// ---------------------------------------------------------------- NoteRoll
gen::Key NoteRoll::key() const { auto t = proc.currentTake(); return t != nullptr ? gen::Key { t->keyTonic, t->keyMinor } : proc.detectedKey(); }
bool NoteRoll::canEdit() const { return proc.editable(); }
bool NoteRoll::mono() const { auto t = proc.currentTake(); return t != nullptr && (t->type == gen::Type::Instrument || t->type == gen::Type::Chops); }

int NoteRoll::stepInKey (int note, int steps, const gen::Key& k)
{
    int n = gen::edit::snapPitch (note, k);
    for (; steps > 0; --steps) { do ++n; while (n < 127 && ! gen::inScale (n, k)); }
    for (; steps < 0; ++steps) { do --n; while (n > 0 && ! gen::inScale (n, k)); }
    return juce::jlimit (0, 127, n);
}

void NoteRoll::layoutGeo()
{
    auto t = proc.currentTake();
    geo.chordH = t != nullptr && ! t->chords.empty() ? 22.0f : 0.0f;
    geo.roll = getLocalBounds().toFloat().reduced (10.0f, 8.0f).withTrimmedBottom (geo.chordH);
    double total = 4.0; int lo = 127, hi = 0;
    if (t != nullptr && t->recipe.engine >= 6 && (t->type == gen::Type::Instrument || t->type == gen::Type::Chops)) total = t->recipe.bars * 4.0;
    for (auto& e : notes) { total = std::max (total, e.beat + e.length); lo = std::min (lo, e.note); hi = std::max (hi, e.note); }
    if (t != nullptr && t->type == gen::Type::Pad && ! t->chords.empty()) total = std::max (total, t->chords.size() * t->beatsPerChord);
    geo.total = std::ceil (total / 4.0 - 1e-9) * 4.0;
    if (hi < lo) { lo = 55; hi = 72; }
    lo -= 3; hi += 3; if (hi - lo < 16) { const int m = (lo + hi) / 2; lo = m - 8; hi = m + 8; }
    geo.lo = juce::jmax (0, lo); geo.hi = juce::jmin (127, hi);
}

void NoteRoll::sync()
{
    if (dragging) return;
    auto t = proc.currentTake();
    if (waitingFor != nullptr)
    {
        if (t.get() == waitingFor && proc.busy()) return;   // (the edit is still being rendered: keep showing it)
        waitingFor = nullptr; shownTake = nullptr;
    }
    if (t.get() == shownTake) return;
    shownTake = t.get();
    notes = t != nullptr ? gen::notesOf (*t) : std::vector<gen::NoteEvent> {};   // (build 9: an older take's notes come from its MIDI)
    sel.assign (notes.size(), false);
    for (size_t i = 0; i < notes.size(); ++i)
        for (auto& k : keepSel) if (std::abs (notes[i].beat - k.first) < 1e-4 && notes[i].note == k.second) sel[i] = true;
    keepSel.clear();
    layoutGeo(); repaint();
    if (onSelectionChanged) onSelectionChanged();
}

int NoteRoll::hitNote (juce::Point<float> p) const
{
    for (int i = (int) notes.size() - 1; i >= 0; --i)
    {
        const auto& e = notes[(size_t) i];
        const auto r = R (geo.x (e.beat), geo.y (e.note), juce::jmax (6.0f, geo.x (e.beat + e.length) - geo.x (e.beat)), juce::jmax (5.0f, geo.rowH())).expanded (1.0f, 1.5f);
        if (r.contains (p)) return i;
    }
    return -1;
}
bool NoteRoll::onRightEdge (int i, juce::Point<float> p) const
{
    if (i < 0 || i >= (int) notes.size()) return false;
    const auto& e = notes[(size_t) i]; const float x1 = geo.x (e.beat + e.length), x0 = geo.x (e.beat);
    return x1 - x0 >= 10.0f && p.x >= x1 - 5.0f && p.x <= x1 + 2.0f;
}
int NoteRoll::chordSlotAt (juce::Point<float> p) const
{
    auto t = proc.currentTake();
    if (t == nullptr || t->chords.empty() || ! geo.chordLane().expanded (0.0f, 2.0f).contains (p)) return -1;
    const int i = (int) std::floor (geo.beat (p.x) / juce::jmax (0.25, t->beatsPerChord));
    return i < 0 ? -1 : i % (int) t->chords.size();
}

bool NoteRoll::commit (const std::vector<Op>& ops, const juce::String& what)
{
    auto t = proc.currentTake();
    if (! canEdit() || t == nullptr || ops.empty()) return false;
    const auto k = key(); const bool m = mono();
    auto e = proc.takeEdit();
    std::vector<gen::NoteEvent> fresh;   // (the notes you placed with this edit: selected afterwards)
    for (auto& [i, nv] : ops)
    {
        if (i >= 0 && i < (int) notes.size())
        {
            const auto& o = notes[(size_t) i];
            if (o.mine)
            {   // one of yours: its old version goes
                for (size_t j = 0; j < e.locked.size(); ++j)
                    if (std::abs (e.locked[j].beat - o.beat) < 1e-4 && (m || gen::edit::snapPitch (e.locked[j].note, k) == o.note)) { e.locked.erase (e.locked.begin() + (long) j); break; }
            }
            else
            {
                e.removed.push_back ({ o.beat, o.note });   // the take's own: remembered as removed
                if (! nv.has_value() && m) e.rests.push_back ({ o.beat, o.length });   // (deleted from a line: that spot stays empty, also after VARY)
            }
        }
        if (nv.has_value())
        {
            auto n = *nv; n.mine = true; n.note = gen::edit::snapPitch (n.note, k);
            n.beat = juce::jlimit (0.0, juce::jmax (0.0, geo.total - 1.0 / 32.0), n.beat); n.length = juce::jlimit (1.0 / 32.0, geo.total - n.beat, n.length);
            e.locked.push_back (n); fresh.push_back (n);
        }
    }
    // what the roll shows until the new take arrives: the same line step the take makes
    std::vector<gen::NoteEvent> genOnly; for (auto& x : notes) if (! x.mine) genOnly.push_back (x);
    auto shown = gen::edit::editLine (genOnly, e, k, juce::jmax (1, (int) std::lround (geo.total / 4.0)), m);
    if (! proc.setEdit (e, what)) return false;
    notes = shown; keepSel.clear();
    sel.assign (notes.size(), false);
    for (auto& f : fresh) { keepSel.push_back ({ f.beat, f.note }); for (size_t i = 0; i < notes.size(); ++i) if (std::abs (notes[i].beat - f.beat) < 1e-4 && notes[i].note == f.note) sel[i] = true; }
    waitingFor = t.get();
    repaint();
    if (onSelectionChanged) onSelectionChanged();
    return true;
}

bool NoteRoll::deleteSelected()
{
    std::vector<Op> ops; for (size_t i = 0; i < notes.size(); ++i) if (i < sel.size() && sel[i]) ops.push_back ({ (int) i, std::nullopt });
    if (ops.empty()) return false;
    return commit (ops, ops.size() == 1 ? "Deleted a note." : "Deleted " + juce::String ((int) ops.size()) + " notes.");
}
bool NoteRoll::addAt (double beat, int note)
{
    if (! canEdit()) return false;
    gen::NoteEvent n; const int g = grid();
    n.beat = gen::edit::snapTime (beat - 0.5 * gen::edit::gridBeats (g), g); n.note = gen::edit::snapPitch (note, key());
    n.length = juce::jmax (gen::edit::gridBeats (g), 0.5); n.velocity = 0.75f;
    for (auto& x : notes) if (x.beat <= n.beat && x.chop >= 0) { n.chop = x.chop; }   // (CHOPS: the chop before it)
    if (n.chop < 0) for (auto& x : notes) if (x.chop >= 0) { n.chop = x.chop; break; }
    return commit ({ { -1, n } }, "Added " + juce::MidiMessage::getMidiNoteName (n.note, true, true, 5) + ".");
}
bool NoteRoll::nudge (double dBeat, int steps)
{
    std::vector<Op> ops; const auto k = key();
    for (size_t i = 0; i < notes.size(); ++i)
        if (i < sel.size() && sel[i])
        {
            auto n = notes[i]; n.beat = juce::jlimit (0.0, geo.total - 1.0 / 32.0, n.beat + dBeat); n.note = stepInKey (n.note, steps, k);
            ops.push_back ({ (int) i, n });
        }
    if (ops.empty()) return false;
    return commit (ops, steps != 0 ? (steps > 0 ? "Up" : "Down") + juce::String (" (in the key).") : juce::String ("Moved."));
}
bool NoteRoll::setLink (int type, float amount)
{
    std::vector<Op> ops;
    for (size_t i = 0; i + 1 < notes.size(); ++i)
        if (i < sel.size() && sel[i]) { auto n = notes[i]; n.link = type; if (amount >= 0) n.linkAmt = amount; if (n.link != notes[i].link || n.linkAmt != notes[i].linkAmt) ops.push_back ({ (int) i, n }); }
    if (ops.empty()) return false;
    static const char* nm[] { "STEP: a plain change into the next note.", "GLIDE: it slides into the next note.", "FATTEN: the next note comes in louder." };
    return commit (ops, nm[juce::jlimit (0, 2, type)]);
}
bool NoteRoll::resetNotes()
{
    auto e = proc.takeEdit();
    if (! e.notesEdited()) return false;
    e.locked.clear(); e.removed.clear(); e.rests.clear(); e.onlyMine = false;
    auto t = proc.currentTake();
    if (! proc.setEdit (e, "Your notes are gone: the take's own melody is back.")) return false;
    waitingFor = t.get(); keepSel.clear();
    return true;
}

bool NoteRoll::swapChord (int slot, int code)
{
    auto t = proc.currentTake();
    if (! canEdit() || t == nullptr) return false;
    const auto& written = t->writtenChords.empty() ? t->chords : t->writtenChords;
    if (slot < 0 || slot >= (int) written.size()) return false;
    const auto k = key(); auto e = proc.takeEdit();
    if ((int) e.chordAlt.size() < (int) written.size()) e.chordAlt.resize (written.size(), -1);
    e.chordAlt[(size_t) slot] = code;
    // your notes over that chord re-fit to it (the take's own notes re-fit when it renders)
    auto chords = t->chords; if ((int) chords.size() < (int) written.size()) chords = written;
    chords[(size_t) slot] = code >= 0 ? gen::edit::chordFor (k, written[(size_t) slot], code, e.chords) : gen::edit::editChords ({ written[(size_t) slot] }, k, [&] { auto x = e; x.chordAlt.clear(); return x; }()).front();
    std::vector<bool> changed (chords.size(), false); changed[(size_t) slot] = true;
    gen::edit::refit (e.locked, chords, t->beatsPerChord, k, changed);
    const bool ok = proc.setEdit (e, "CHORD " + juce::String (slot + 1) + ": " + chords[(size_t) slot].name + ". The melody follows it.");
    if (ok) { waitingFor = t.get(); keepSel.clear(); }
    return ok;
}

void NoteRoll::chordMenu (int slot)
{
    auto t = proc.currentTake();
    if (t == nullptr) return;
    const auto& written = t->writtenChords.empty() ? t->chords : t->writtenChords;
    if (slot < 0 || slot >= (int) written.size()) return;
    const auto k = key(); const auto e = proc.takeEdit();
    const int cur = slot < (int) e.chordAlt.size() ? e.chordAlt[(size_t) slot] : -1;
    const auto opts = gen::edit::chordOptions (k, written[(size_t) slot], e.chords);
    juce::PopupMenu m;
    m.addSectionHeader ("CHORD " + juce::String (slot + 1) + ": " + (slot < (int) t->chords.size() ? t->chords[(size_t) slot].name : written[(size_t) slot].name) + "  (all in " + t->keyName() + ")");
    m.addItem (1, "THE STYLE'S CHORD  (" + written[(size_t) slot].name + ")", true, cur < 0);
    m.addSeparator();
    for (size_t j = 0; j < opts.size(); ++j) m.addItem (100 + (int) j, opts[j].label, true, opts[j].code == cur);
    if (written.size() > 1)
    {
        m.addSeparator();
        for (size_t j = 0; j < written.size(); ++j)
            if ((int) j != slot) m.addItem (300 + (int) j, "SAME AS CHORD " + juce::String ((int) j + 1) + "  (" + (j < t->chords.size() ? t->chords[j].name : written[j].name) + ")");
    }
    juce::Component::SafePointer<NoteRoll> self (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [self, slot, opts, written, e] (int r)
    {
        if (self == nullptr || r <= 0) return;
        if (r == 1) self->swapChord (slot, -1);
        else if (r >= 300) { const size_t j = (size_t) (r - 300); const int code = j < e.chordAlt.size() && e.chordAlt[j] >= 0 ? e.chordAlt[j] : juce::jlimit (0, 6, written[j].degree) * 8; self->swapChord (slot, code); }
        else if (r >= 100 && r - 100 < (int) opts.size()) self->swapChord (slot, opts[(size_t) (r - 100)].code);
    });
}

juce::String NoteRoll::getTooltip()
{
    const auto m = getMouseXYRelative().toFloat();
    if (! canEdit()) return "Press GENERATE: the notes of the take show here.";
    if (chordSlotAt (m) >= 0) return "Click a chord to swap it for another chord in the key (ArpEZator's choices: sus, 7th, lush, over the 3rd, swap down / up, or any chord of the key). The melody follows it.";
    const int i = hitNote (m);
    if (i >= 0) return juce::MidiMessage::getMidiNoteName (notes[(size_t) i].note, true, true, 5) + (notes[(size_t) i].mine ? " (yours)" : "") + ": drag to move (it stays in the key), drag its right end to resize, double-click or DELETE to delete. Right-click: GLIDE / FATTEN / STEP into the next note.";
    return "Double-click to add a note (in the key, on the grid). Drag on empty space to select several. Arrows move the selection, Cmd+Z undoes.";
}

void NoteRoll::paint (juce::Graphics& g)
{
    if (S == nullptr) return;
    const auto& K = *S;
    const auto a = getLocalBounds().toFloat();
    g.setColour (K.well); g.fillRoundedRectangle (a, 10.0f); g.setColour (hasKeyboardFocus (false) ? K.a2.withAlpha (0.45f) : K.panelLine); g.drawRoundedRectangle (a.reduced (0.5f), 10.0f, 1.0f);
    auto t = proc.currentTake();
    if (t == nullptr) return;
    const auto k = key(); const auto& G = geo; const float rowH = G.rowH();
    // rows: notes outside the key are shaded (nothing can land there), the tonic rows marked
    for (int n = G.lo; n <= G.hi; ++n)
    {
        const auto row = R (G.roll.getX(), G.y (n), G.roll.getWidth(), rowH);
        if (! gen::inScale (n, k)) { g.setColour (juce::Colours::black.withAlpha (K.light ? 0.06f : 0.22f)); g.fillRect (row); }
        else if (((n - k.tonic) % 12 + 12) % 12 == 0) { g.setColour (K.a.withAlpha (0.07f)); g.fillRect (row); }
    }
    // the grid: bars, beats, the grid step (when there is room)
    const double gb = gen::edit::gridBeats (grid());
    const bool fine = G.x (gb) - G.x (0) >= 7.0f;
    for (double b = 0; b <= G.total + 1e-9; b += fine ? gb : 1.0)
    {
        const bool bar = std::fmod (b + 1e-9, 4.0) < 1e-6, beat = std::fmod (b + 1e-9, 1.0) < 1e-6;
        g.setColour (K.dim.withAlpha (bar ? 0.30f : beat ? 0.11f : 0.045f)); g.drawVerticalLine ((int) G.x (b), G.roll.getY(), G.roll.getBottom() + (bar ? G.chordH : 0.0f));
    }
    // the lines between notes (ArpEZator's GLIDE / FATTEN)
    for (size_t i = 0; i + 1 < notes.size(); ++i)
    {
        const auto& e = notes[i]; const auto& nx = notes[i + 1];
        if (e.link == 0) continue;
        const float x0 = G.x (e.beat + e.length * (e.link == 1 ? e.linkAmt : 1.0)), y0 = G.y (e.note) + rowH * 0.5f, x1 = G.x (nx.beat) + 2.0f, y1 = G.y (nx.note) + rowH * 0.5f;
        if (e.link == 1)
        {
            juce::Path p; p.startNewSubPath (x0, y0); p.cubicTo (x0 + (x1 - x0) * 0.5f, y0, x0 + (x1 - x0) * 0.5f, y1, x1, y1);
            g.setColour (K.a2.withAlpha (0.9f)); g.strokePath (p, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.fillEllipse (R (7.0f, 7.0f).withCentre ({ x0, y0 }));
        }
        else
        {
            g.setColour (K.star.withAlpha (0.8f)); g.drawLine (x0, y0, x1, y1, 1.4f);
            const float sz = 6.0f + 6.0f * e.linkAmt; g.fillEllipse (R (sz, sz).withCentre ({ (x0 + x1) * 0.5f, (y0 + y1) * 0.5f }));
        }
    }
    // the notes (yours have a light rim; the selected ones glow)
    for (size_t i = 0; i < notes.size(); ++i)
    {
        const auto& e = notes[i];
        if (e.note < G.lo || e.note > G.hi) continue;
        const auto r = R (G.x (e.beat), G.y (e.note), juce::jmax (3.0f, G.x (e.beat + e.length) - G.x (e.beat) - 1.0f), juce::jmax (3.0f, rowH - 1.0f));
        const bool s = i < sel.size() && sel[i];
        const auto c = K.at ((float) juce::jlimit (0.0, 1.0, (e.note - G.lo) / (double) juce::jmax (1, G.hi - G.lo)));
        if (s) glow (g, r, 3.0f, K.b, 5.0f, 3, 0.12f);
        g.setColour (c.withAlpha (s ? 1.0f : 0.40f + 0.55f * e.velocity)); g.fillRoundedRectangle (r, juce::jmin (3.0f, rowH * 0.4f));
        if (s) { g.setColour (juce::Colours::white.withAlpha (0.95f)); g.drawRoundedRectangle (r.expanded (0.8f), 3.0f, 1.4f); }
        else if (e.mine) { g.setColour (juce::Colours::white.withAlpha (0.45f)); g.drawRoundedRectangle (r, 3.0f, 0.9f); }
        if ((int) i == hover && hoverEdge) { g.setColour (K.b); g.fillRect (R (r.getRight() - 2.5f, r.getY(), 2.5f, r.getHeight())); }
        if (e.chop >= 0 && r.getWidth() > 12.0f && rowH >= 7.0f) { g.setColour (K.light ? juce::Colours::white : juce::Colour (0xff0b0920)); g.setFont (capFont (6.4f, Weight::SemiBold)); g.drawText (juce::String (e.chop + 1), r, juce::Justification::centred, false); }
    }
    // the chords along the bottom: click one to swap it
    if (! t->chords.empty())
    {
        const auto lane = G.chordLane(); const auto f = capFont (7.6f, Weight::SemiBold, 0.04f);
        const auto mouse = getMouseXYRelative().toFloat(); const int hs = isMouseOver() ? chordSlotAt (mouse) : -1;
        const auto e = proc.takeEdit();
        int i = 0;
        for (double b = 0; b < G.total - 1e-9; b += t->beatsPerChord, ++i)
        {
            const int slot = i % (int) t->chords.size();
            const auto& c = t->chords[(size_t) slot];
            const auto r = R (G.x (b) + 1.0f, lane.getY(), G.x (juce::jmin (G.total, b + t->beatsPerChord)) - G.x (b) - 2.0f, lane.getHeight());
            const bool swapped = slot < (int) e.chordAlt.size() && e.chordAlt[(size_t) slot] >= 0;
            g.setColour (K.a.withAlpha (slot == hs && canEdit() ? 0.34f : swapped ? 0.26f : 0.16f)); g.fillRoundedRectangle (r, 4.0f);
            if (slot == hs && canEdit()) { g.setColour (K.a2.withAlpha (0.8f)); g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f); }
            g.setColour (K.ink.withAlpha (0.9f)); g.setFont (f); g.drawFittedText (c.name + (swapped ? " *" : ""), r.toNearestInt(), juce::Justification::centred, 1, 0.6f);
        }
    }
    // the band selection
    if (dr.kind == 3 && dragging) { g.setColour (K.b.withAlpha (0.12f)); g.fillRect (dr.band); g.setColour (K.b.withAlpha (0.7f)); g.drawRect (dr.band, 1.0f); }
    // the play head
    if (proc.previewPlaying())
    {
        const double spb = 60.0 / juce::jmax (1.0, t->bpm), secs = t->preview.getNumSamples() / juce::jmax (1.0, t->rate);
        const double beat = proc.previewPosition() * secs / spb;
        if (beat <= G.total)
        {
            const float px = G.x (beat);
            g.setColour (K.b.withAlpha (0.25f)); g.fillRect (R (px - 2.0f, G.roll.getY(), 5.0f, G.roll.getHeight() + G.chordH));
            g.setColour (K.b); g.drawVerticalLine ((int) px, G.roll.getY(), G.roll.getBottom() + G.chordH);
        }
    }
}

void NoteRoll::mouseMove (const juce::MouseEvent& e)
{
    const int h = canEdit() ? hitNote (e.position) : -1; const bool edge = onRightEdge (h, e.position);
    setMouseCursor (! canEdit() ? juce::MouseCursor::NormalCursor : chordSlotAt (e.position) >= 0 ? juce::MouseCursor::PointingHandCursor : edge ? juce::MouseCursor::LeftRightResizeCursor : h >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
    if (h != hover || edge != hoverEdge) { hover = h; hoverEdge = edge; }
    repaint();
}

void NoteRoll::mouseDown (const juce::MouseEvent& e)
{
    grabKeyboardFocus();
    dr = {}; dragging = false;
    if (! canEdit()) return;
    const int slot = chordSlotAt (e.position);
    if (slot >= 0) { chordMenu (slot); return; }
    const int i = hitNote (e.position);
    if (sel.size() != notes.size()) sel.assign (notes.size(), false);
    if (e.mods.isPopupMenu())
    {
        if (i < 0) return;
        if (! sel[(size_t) i]) { std::fill (sel.begin(), sel.end(), false); sel[(size_t) i] = true; }
        juce::PopupMenu m; const int lk = notes[(size_t) i].link;
        m.addSectionHeader (juce::MidiMessage::getMidiNoteName (notes[(size_t) i].note, true, true, 5) + " - INTO THE NEXT NOTE");
        m.addItem (1, "STEP (a plain change)", (size_t) i + 1 < notes.size(), lk == 0); m.addItem (2, "GLIDE (slide into it)", (size_t) i + 1 < notes.size(), lk == 1); m.addItem (3, "FATTEN (next note louder)", (size_t) i + 1 < notes.size(), lk == 2);
        m.addSeparator(); m.addItem (4, "UP AN OCTAVE"); m.addItem (5, "DOWN AN OCTAVE"); m.addItem (6, "DELETE");
        juce::Component::SafePointer<NoteRoll> self (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(), [self] (int r)
        {
            if (self == nullptr || r <= 0) return;
            if (r <= 3) self->setLink (r - 1); else if (r == 4) self->nudge (0, 7); else if (r == 5) self->nudge (0, -7); else self->deleteSelected();
        });
        repaint(); if (onSelectionChanged) onSelectionChanged();
        return;
    }
    dragging = true; dr.p0 = e.position; base = notes;
    if (i >= 0)
    {
        if (e.mods.isShiftDown()) sel[(size_t) i] = ! sel[(size_t) i];
        else if (! sel[(size_t) i]) { std::fill (sel.begin(), sel.end(), false); sel[(size_t) i] = true; }
        dr.kind = onRightEdge (i, e.position) ? 2 : 1; dr.anchor = i;
        // hear it (the on-screen keys play the take)
        const int nn = notes[(size_t) i].note; proc.keyboard.noteOn (1, nn, 0.8f);
        juce::Component::SafePointer<NoteRoll> self (this);
        juce::Timer::callAfterDelay (260, [self, nn] { if (self != nullptr) self->proc.keyboard.noteOff (1, nn, 0.0f); });
    }
    else
    {
        if (! e.mods.isShiftDown()) std::fill (sel.begin(), sel.end(), false);
        dr.kind = 3; dr.band = R (e.position.x, e.position.y, 0, 0);
    }
    repaint(); if (onSelectionChanged) onSelectionChanged();
}

void NoteRoll::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging || ! canEdit()) return;
    if (e.position.getDistanceFrom (dr.p0) > 3.0f) dr.moved = true;
    const int gs = grid(); const auto k = key();
    if (dr.kind == 1 && dr.moved && dr.anchor >= 0 && dr.anchor < (int) base.size())
    {
        const auto& an = base[(size_t) dr.anchor];
        const double nb = gen::edit::snapTime (an.beat + geo.beat (e.position.x) - geo.beat (dr.p0.x), gs), dB = nb - an.beat;
        const int target = gen::edit::snapPitch (juce::jlimit (geo.lo, geo.hi, geo.note (e.position.y)), k);
        auto idx = [&] (int n) { int c = 0; for (int x = 0; x < n; ++x) c += gen::inScale (x, k) ? 1 : 0; return c; };
        const int steps = idx (target) - idx (gen::edit::snapPitch (an.note, k));
        for (size_t j = 0; j < notes.size(); ++j)
            if (j < sel.size() && sel[j])
            {
                notes[j].beat = juce::jlimit (0.0, geo.total - gen::edit::gridBeats (gs), base[j].beat + dB);
                notes[j].note = juce::jlimit (geo.lo, geo.hi, stepInKey (base[j].note, steps, k));
            }
    }
    else if (dr.kind == 2 && dr.anchor >= 0 && dr.anchor < (int) base.size())
    {
        const auto& an = base[(size_t) dr.anchor]; const double gb = gen::edit::gridBeats (gs);
        const double end = gen::edit::snapTime (geo.beat (e.position.x), gs), dL = juce::jmax (gb, end - an.beat) - an.length;
        for (size_t j = 0; j < notes.size(); ++j) if (j < sel.size() && sel[j]) notes[j].length = juce::jlimit (gb, geo.total - base[j].beat, base[j].length + dL);
    }
    else if (dr.kind == 3)
    {
        dr.band = R (dr.p0, e.position);
        for (size_t j = 0; j < notes.size(); ++j)
        {
            const auto& n = notes[j]; const auto r = R (geo.x (n.beat), geo.y (n.note), juce::jmax (3.0f, geo.x (n.beat + n.length) - geo.x (n.beat)), geo.rowH());
            if (dr.band.intersects (r)) sel[j] = true; else if (! e.mods.isShiftDown()) sel[j] = false;
        }
    }
    repaint();
}

void NoteRoll::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    if ((dr.kind == 1 || dr.kind == 2) && dr.moved)
    {
        std::vector<Op> ops;
        for (size_t j = 0; j < notes.size() && j < base.size(); ++j)
            if (notes[j].beat != base[j].beat || notes[j].note != base[j].note || notes[j].length != base[j].length) ops.push_back ({ (int) j, notes[j] });
        notes = base;   // (the edit starts from the notes as they were)
        if (! ops.empty()) commit (ops, dr.kind == 2 ? juce::String ("Note length changed.") : ops.size() == 1 ? juce::String ("Moved: ") + juce::MidiMessage::getMidiNoteName (ops[0].second->note, true, true, 5) + " (in the key)." : "Moved " + juce::String ((int) ops.size()) + " notes (in the key).");
    }
    dr = {};
    repaint(); if (onSelectionChanged) onSelectionChanged();
}

void NoteRoll::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! canEdit() || chordSlotAt (e.position) >= 0) return;
    const int i = hitNote (e.position);
    if (i >= 0) { sel.assign (notes.size(), false); sel[(size_t) i] = true; deleteSelected(); return; }
    if (geo.roll.contains (e.position)) addAt (geo.beat (e.position.x), geo.note (e.position.y));
}

bool NoteRoll::keyPressed (const juce::KeyPress& kp)
{
    const auto m = kp.getModifiers();
    if (kp.getKeyCode() == 'Z' || kp.getKeyCode() == 'z')
        if (m.isCommandDown()) { if (m.isShiftDown()) proc.redoEdit(); else proc.undoEdit(); return true; }
    if (! canEdit()) return false;
    if (m.isCommandDown() && (kp.getKeyCode() == 'A' || kp.getKeyCode() == 'a')) { sel.assign (notes.size(), true); repaint(); if (onSelectionChanged) onSelectionChanged(); return true; }
    if (kp.isKeyCode (juce::KeyPress::deleteKey) || kp.isKeyCode (juce::KeyPress::backspaceKey)) return deleteSelected() || true;
    if (kp.isKeyCode (juce::KeyPress::escapeKey)) { sel.assign (notes.size(), false); repaint(); if (onSelectionChanged) onSelectionChanged(); return true; }
    const double gb = gen::edit::gridBeats (grid());
    if (kp.isKeyCode (juce::KeyPress::upKey)) return nudge (0, m.isShiftDown() ? 7 : 1) || true;
    if (kp.isKeyCode (juce::KeyPress::downKey)) return nudge (0, m.isShiftDown() ? -7 : -1) || true;
    if (kp.isKeyCode (juce::KeyPress::leftKey)) return nudge (-gb, 0) || true;
    if (kp.isKeyCode (juce::KeyPress::rightKey)) return nudge (gb, 0) || true;
    return false;
}

// ---------------------------------------------------------------- PlayPanel (PLAY IT)
PlayPanel::PlayPanel (SliceProcessor& p, NoteRoll& r) : proc (p), roll (r)
{
    for (auto* s : { &master, &lineAmt })
    {
        addAndMakeVisible (*s); s->setRange (0.0, 1.0, 0.01); s->setDoubleClickReturnValue (true, 0.5);
    }
    master.setTooltip ("MASTER (ArpEZator's): SIMPLE thins the line out to its strong notes, WILD fills it in with busier runs. Your own notes stay. Double-click: the middle (as composed).");
    auto commitMaster = [this] { if (refreshing) return; const double v = master.getValue(); proc.setPlay ([v] (gen::NoteEdit& e) { e.master = v; }, v < 0.45 ? "MASTER: simpler." : v > 0.55 ? "MASTER: wilder." : "MASTER: as composed."); };
    master.onDragEnd = commitMaster;
    master.onValueChange = [this, commitMaster] { if (! refreshing && ! master.isMouseButtonDown()) commitMaster(); repaint(); };
    addAndMakeVisible (bed); bed.setRange (0.0, 1.0, 0.01); bed.setDoubleClickReturnValue (true, 0.0);
    bed.setTooltip ("CHORD BED (INSTRUMENT): the take's chords held softly under the melody, on the same voice, so RICH chords and your chord swaps are heard. All the way left = off. Double-click: off.");
    auto commitBed = [this] { if (refreshing) return; const double v = bed.getValue(); proc.setPlay ([v] (gen::NoteEdit& e) { e.bed = v; }, v <= 0.0 ? "CHORD BED off." : "CHORD BED " + juce::String (juce::roundToInt (v * 100.0)) + "%: the chords under the melody."); };
    bed.onDragEnd = commitBed;
    bed.onValueChange = [this, commitBed] { if (! refreshing && ! bed.isMouseButtonDown()) commitBed(); repaint(); };
    lineAmt.setTooltip ("How much: GLIDE = where the slide starts in the note (left = early, a long slide), FATTEN = how much louder the next note is.");
    auto commitLine = [this] { if (refreshing) return; roll.setLink (line.sel, (float) lineAmt.getValue()); };
    lineAmt.onDragEnd = commitLine;
    lineAmt.onValueChange = [this, commitLine] { if (! refreshing && ! lineAmt.isMouseButtonDown()) commitLine(); repaint(); };
    for (auto* s : { &chords, &rate, &octaves, &line }) addAndMakeVisible (*s);
    chords.setTooltip ("CHORDS: AS WRITTEN (the style's own), SIMPLE (plain major / minor triads) or RICH (7ths, 9ths, 11ths). Click a chord under the roll to swap just that one.");
    chords.onChange = [this] (int i) { proc.setPlay ([i] (gen::NoteEdit& e) { e.chords = i - 1; }, i == 1 ? "SIMPLE chords: plain major / minor triads." : i == 2 ? "RICH chords: 7ths, 9ths and 11ths." : "The chords as the style wrote them."); };
    rate.setTooltip ("RATE: the arp in 1/8 or 1/16 notes."); rate.onChange = [this] (int i) { proc.setPlay ([i] (gen::NoteEdit& e) { e.arpRate = i; }, juce::String ("ARP RATE: ") + (i == 0 ? "1/8" : "1/16")); };
    octaves.setTooltip ("OCTAVES: how far the arp climbs."); octaves.onChange = [this] (int i) { proc.setPlay ([i] (gen::NoteEdit& e) { e.arpOct = i + 1; }, "ARP: " + juce::String (i + 1) + (i == 0 ? " octave." : " octaves.")); };
    line.setTooltip ("The line from each selected note into the next one: STEP (plain), GLIDE (slides into it), FATTEN (the next note louder). Select notes on the roll first.");
    line.onChange = [this] (int i) { if (! roll.setLink (i, (float) lineAmt.getValue())) { refresh(); if (proc.onToast) proc.onToast ("Select a note on the roll first (click it)."); } };
    addAndMakeVisible (arpBtn); arpBtn.label = "ARP"; arpBtn.menu = true;
    arpBtn.setTooltip ("ARP (ArpEZator's engine): OFF = the melody, or the arp over the take's chords: FIGURE (its own figure), UP, DOWN, UP-DOWN, RANDOM, PENDULUM, CONVERGE.");
    arpBtn.onClick = [this]
    {
        juce::PopupMenu m; const int cur = proc.takeEdit().arp;
        for (int a = 0; a < gen::edit::kArps; ++a) m.addItem (a + 1, gen::edit::arpName (a), true, a == cur);
        juce::Component::SafePointer<PlayPanel> self (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&arpBtn), [self] (int r)
        {
            if (self == nullptr || r <= 0) return;
            const int a = r - 1;
            self->proc.setPlay ([a] (gen::NoteEdit& e) { e.arp = a; }, a == 0 ? juce::String ("ARP off: the melody again.") : juce::String ("ARP: ") + gen::edit::arpName (a) + " over the chords.");
        });
    };
}
void PlayPanel::setSkin (const Skin* s) { S = s; chords.S = s; rate.S = s; octaves.S = s; line.S = s; arpBtn.S = s; }
void PlayPanel::refresh()
{
    refreshing = true;
    const auto e = proc.takeEdit(); auto t = proc.currentTake();
    const auto ty = t != nullptr ? t->type : proc.knobType();
    const bool lineType = ty == gen::Type::Instrument || ty == gen::Type::Chops, hasChords = ty != gen::Type::Texture;
    master.setValue (e.master, juce::dontSendNotification); bed.setValue (e.bed, juce::dontSendNotification); bed.setEnabled (ty == gen::Type::Instrument);
    chords.sel = e.chords + 1; rate.sel = e.arpRate; octaves.sel = e.arpOct - 1;
    arpBtn.value = gen::edit::arpName (e.arp);
    int lk = 0; float amt = 0.55f; bool any = false;
    for (size_t i = 0; i < roll.notes.size(); ++i) if (i < roll.sel.size() && roll.sel[i]) { lk = roll.notes[i].link; amt = roll.notes[i].linkAmt; any = true; break; }
    line.sel = lk; lineAmt.setValue (amt, juce::dontSendNotification);
    master.setEnabled (lineType); arpBtn.setEnabled (lineType); rate.setEnabled (lineType && e.arp > 0); octaves.setEnabled (lineType && e.arp > 0);
    chords.setEnabled (hasChords); line.setEnabled (lineType && any); lineAmt.setEnabled (lineType && any && lk != 0);
    refreshing = false;
    for (auto* c : getChildren()) c->repaint();
    repaint();
}
juce::String PlayPanel::getTooltip() { return "PLAY IT: ArpEZator's controls for this take. Every change re-renders the take (one undo step) and the next takes get the same settings."; }
void PlayPanel::resized()
{
    const float w = (float) getWidth();
    master.setBounds (R (0.0f, 14.0f, w * 0.58f, 24.0f).toNearestInt());
    bed.setBounds (R (w * 0.64f, 14.0f, w * 0.36f, 24.0f).toNearestInt());
    chords.setBounds (R (0.0f, 58.0f, w, 22.0f).toNearestInt());
    arpBtn.setBounds (R (-7.0f, 93.0f, 170.0f, 36.0f).toNearestInt());
    rate.setBounds (R (170.0f, 100.0f, 84.0f, 22.0f).toNearestInt());
    octaves.setBounds (R (262.0f, 100.0f, w - 262.0f, 22.0f).toNearestInt());
    line.setBounds (R (0.0f, 146.0f, 186.0f, 22.0f).toNearestInt());
    lineAmt.setBounds (R (196.0f, 145.0f, w - 196.0f, 24.0f).toNearestInt());
}
void PlayPanel::paint (juce::Graphics& g)
{
    if (S == nullptr) return;
    const auto& K = *S; const float w = (float) getWidth();
    const auto cap = capFont (7.2f, Weight::Medium, 0.08f), val = capFont (7.2f, Weight::SemiBold, 0.06f);
    g.setColour (K.dim); textAt (g, "MASTER", cap, 0.0f, 6.0f);
    g.setColour (K.dim.withAlpha (0.8f)); textAt (g, "SIMPLE - WILD", val, 54.0f, 6.0f);
    g.setColour (K.light ? K.a : K.a2); textAt (g, juce::String (juce::roundToInt (master.getValue() * 100.0)) + "%", val, w * 0.58f, 6.0f, juce::Justification::centredRight, 50.0f);
    g.setColour (K.dim.withMultipliedAlpha (bed.isEnabled() ? 1.0f : 0.5f)); textAt (g, "CHORD BED", cap, w * 0.64f, 6.0f);
    g.setColour ((K.light ? K.a : K.a2).withMultipliedAlpha (bed.isEnabled() ? 1.0f : 0.5f)); textAt (g, bed.getValue() <= 0.0 ? juce::String ("OFF") : juce::String (juce::roundToInt (bed.getValue() * 100.0)) + "%", val, w, 6.0f, juce::Justification::centredRight, 50.0f);
    g.setColour (K.dim); textAt (g, "CHORDS", cap, 0.0f, 50.0f);
    auto t = proc.currentTake();
    if (t != nullptr && ! t->chords.empty()) { g.setColour (K.light ? K.a : K.a2); textAt (g, "CLICK A CHORD UNDER THE ROLL TO SWAP IT", capFont (6.6f, Weight::Medium, 0.06f), w, 50.0f, juce::Justification::centredRight, w - 60.0f); }
    g.setColour (K.dim); textAt (g, "THE ARP", cap, 0.0f, 90.0f); textAt (g, "RATE", cap, 170.0f, 90.0f); textAt (g, "OCTAVES", cap, 262.0f, 90.0f);
    g.setColour (K.dim); textAt (g, "LINE INTO THE NEXT NOTE", cap, 0.0f, 138.0f);
    const int lk = line.sel;
    g.setColour (K.light ? K.a : K.a2);
    textAt (g, ! line.isEnabled() ? juce::String ("SELECT A NOTE") : lk == 1 ? "GLIDE STARTS AT " + juce::String (juce::roundToInt (lineAmt.getValue() * 100.0)) + "%" : lk == 2 ? "FATTEN " + juce::String (juce::roundToInt (lineAmt.getValue() * 100.0)) + "%" : juce::String ("STEP: NO AMOUNT"),
            val, w, 138.0f, juce::Justification::centredRight, 170.0f);
}
