#pragma once
// RHYTHM LAB page: GENERATE, KEEP x% IDENTITY, BLEND, EVOLVE + PHRASE arc + CONVERGENCE, BREED tiles, BAKE.
#include "LinearPanel.h"

namespace bx {

// 16-bar PHRASE arc: draw the intensity EVOLVE follows (establish / vary / build / resolve)
struct PhraseArc : juce::Component {
    Ed& ed; explicit PhraseArc(Ed& e) : ed(e) {}
    void paint(juce::Graphics& g) override {
        auto R = getLocalBounds().toFloat(); g.setColour(CARD2); g.fillRoundedRectangle(R, 8);
        const auto& a = ed.proc.model.arc; const float w = R.getWidth() / 16.0f; juce::Path p;
        for (int i = 0; i < 16; ++i) { const float y = R.getBottom() - 6 - a[(size_t) i] / 100.0f * (R.getHeight() - 20); const float x = i * w + w * 0.5f; if (i == 0) p.startNewSubPath(x, y); else p.lineTo(x, y);
            g.setColour(PURPLE.withAlpha(0.35f)); g.fillRect(i * w + 3, y, w - 6, R.getBottom() - 6 - y); }
        g.setColour(GREEN); g.strokePath(p, juce::PathStrokeType(2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        const int bar = ed.proc.engine.uiBar.load(); if (ed.proc.engine.uiBeat.load() >= 0) { g.setColour(INK.withAlpha(0.6f)); g.drawVerticalLine((int) ((bar % 16) * w + w * 0.5f), 2, R.getBottom() - 2); }
        g.setColour(GREY); g.setFont(font(9.5f, false));
        const char* lab[] { "ESTABLISH", "VARY", "BUILD", "RESOLVE" }; for (int i = 0; i < 4; ++i) g.drawText(lab[i], (int) (i * 4 * w), 2, (int) (4 * w), 12, juce::Justification::centred);
    }
    void put(juce::Point<float> p) { const int i = juce::jlimit(0, 15, (int) (p.x / (getWidth() / 16.0f))); const int v = juce::jlimit(0, 100, (int) std::lround((getHeight() - 6 - p.y) / (getHeight() - 20) * 100));
        if (ed.proc.model.arc[(size_t) i] != v) { ed.proc.model.arc[(size_t) i] = (uint8_t) v; ed.live(); } }
    void mouseDown(const juce::MouseEvent& e) override { ed.proc.pushUndo(); put(e.position); }
    void mouseDrag(const juce::MouseEvent& e) override { put(e.position); }
};

// a tiny drawing of a pattern (tiles, DNA)
inline void drawMini(juce::Graphics& g, const gb::Pattern& p, juce::Rectangle<float> r, int maxTracks = 8) {
    const int L = juce::jmin(32, p.length); const float cw = r.getWidth() / L, rh = r.getHeight() / maxTracks;
    for (int t = 0; t < maxTracks; ++t) for (int s = 0; s < L; ++s) { const auto& st = p.steps[(size_t) t][(size_t) (s % p.lenOf(t))]; if (!st.vel) continue;
        g.setColour(trackColour(t).withAlpha(0.35f + 0.65f * juce::jmin(1.0f, st.vel / 127.0f))); g.fillRect(r.getX() + s * cw + 0.5f, r.getY() + t * rh + 0.5f, juce::jmax(1.0f, cw - 1), juce::jmax(1.0f, rh - 1));
        if (st.roll > 1) { g.setColour(INK); g.fillRect(r.getX() + s * cw + cw * 0.5f - 0.5f, r.getY() + t * rh, 1.0f, rh); } }
}

struct BreedTiles : juce::Component {
    Ed& ed; int hover = -1, playing = -1; explicit BreedTiles(Ed& e) : ed(e) {}
    juce::Rectangle<float> tile(int i) const { const float w = getWidth() / 4.0f, h = getHeight() / 4.0f; return { (i % 4) * w + 3, (i / 4) * h + 3, w - 6, h - 6 }; }
    juce::Rectangle<float> star(int i) const { auto t = tile(i); return { t.getRight() - 22, t.getY() + 2, 20, 18 }; }
    void paint(juce::Graphics& g) override {
        auto& pool = ed.proc.breedPool;
        if (pool.empty()) { g.setColour(GREY); g.setFont(font(13.0f, false)); g.drawFittedText("BREED makes 16 variations of the current pattern. Click a tile to hear it, star the ones you like, then NEXT GENERATION leans toward what the stars share.", getLocalBounds().reduced(30), juce::Justification::centred, 4); return; }
        for (int i = 0; i < (int) pool.size() && i < 16; ++i) {
            auto r = tile(i); g.setColour(i == playing ? PURPLE.withAlpha(0.35f) : (i == hover ? CARD2.brighter(0.1f) : CARD2)); g.fillRoundedRectangle(r, 6);
            g.setColour(i == playing ? GREEN : PURPLE.withAlpha(0.4f)); g.drawRoundedRectangle(r, 6, i == playing ? 1.8f : 1.0f);
            drawMini(g, pool[(size_t) i], r.reduced(6, 20).translated(0, 6));
            g.setColour(GREY); g.setFont(font(10.0f)); g.drawText(juce::String(i + 1), r.reduced(6, 3), juce::Justification::topLeft);
            const bool st = i < (int) ed.proc.breedStar.size() && ed.proc.breedStar[(size_t) i]; g.setColour(st ? AMBER : DIM); g.setFont(font(16.0f)); g.drawText(st ? "*" : "+", star(i), juce::Justification::centred);
        }
    }
    int at(juce::Point<float> p) const { for (int i = 0; i < (int) ed.proc.breedPool.size() && i < 16; ++i) if (tile(i).contains(p)) return i; return -1; }
    void mouseMove(const juce::MouseEvent& e) override { const int h = at(e.position); if (h != hover) { hover = h; repaint(); } }
    void mouseDown(const juce::MouseEvent& e) override {
        const int i = at(e.position); if (i < 0) return;
        if (star(i).contains(e.position)) { ed.proc.breedStar[(size_t) i] = !ed.proc.breedStar[(size_t) i]; repaint(); return; }
        ed.proc.useBreed(i); playing = i; ed.proc.engine.internalPlay = true; if (ed.refresh) ed.refresh();
        if (ed.say) ed.say("Tile " + juce::String(i + 1) + " is in pattern " + patName(ed.patIdx()) + " (UNDO to go back).");
    }
};

struct RhythmLab : juce::Component {
    Ed& ed;
    PhraseArc arc { ed }; BreedTiles tiles { ed };
    // LINEAR DRUMMING (one voice at a time; see Linear.h): its own tab over the left column
    LinearPanel linear { ed }; juce::TextButton tabTools { "RHYTHM TOOLS" }, tabLinear { "LINEAR DRUMMING" }; bool linearTab = false;
    std::vector<juce::Component*> toolComps;
    juce::TextButton genI { "GENERATE INTRICATE" }, genM { "GENERATE MACHINE GUN" }, genD { "GENERATE DRILL" }, genR { "GENERATE REGGAETON" }, genT { "GENERATE TRAP CLASSIC" }, keepBtn { "KEEP + NEW HITS" }, blendBtn { "BLEND" }, breedNew { "NEW BREED" }, breedNext { "NEXT GENERATION" },
                     styleI { "INTRICATE" }, styleM { "MACHINE GUN" }, dice { "NEW SEED" }, bake { "BAKE TO SONG" };
    Bar evolve, keep, blendW, grit;
    juce::ComboBox fill, conv, blendA, blendB, perfBars;
    juce::Label lEvolve, lFill, lConv, lKeep, lBlend, lPerf, lSeed, help, lLocks, lMotif, lGrit;
    juce::OwnedArray<juce::ComboBox> lockStart, lockLen; juce::OwnedArray<juce::TextButton> lockMask;
    juce::ComboBox motifFrom, motifLen; juce::TextButton motifBtn { "LOCK MOTIF" }, motifClear { "CLEAR MOTIFS" };
    explicit RhythmLab(Ed& e) : ed(e) {
        addAndMakeVisible(arc); addAndMakeVisible(tiles);
        for (auto* s : { &evolve, &keep, &blendW, &grit }) { s->setColour(juce::Slider::trackColourId, PURPLE.withAlpha(0.5f)); s->setColour(juce::Slider::backgroundColourId, CARD2); addAndMakeVisible(s); }
        evolve.setRange(0, 100, 1); evolve.setTextValueSuffix(" %"); keep.setRange(10, 100, 1); keep.setValue(80); keep.setTextValueSuffix(" % kept"); blendW.setRange(0, 100, 1); blendW.setValue(50); blendW.setTextValueSuffix(" % B");
        grit.setRange(0, 100, 1); grit.setTextValueSuffix(" % grit"); grit.setDoubleClickReturnValue(true, 70);
        grit.setTooltip("DRILL GRIT for the next GENERATE DRILL: 0 = the clean drill, up = tough and dirty (darker, heavier sliding 808 carrying the melody, a late dirty snare + clap, sparser kicks, softer hats). Saved with the beat.");
        grit.onDragStart = [this] { ed.proc.pushUndo(); }; grit.onValueChange = [this] { ed.proc.model.drillGrit = (int) grit.getValue(); };
        evolve.setDoubleClickReturnValue(true, 0); keep.setDoubleClickReturnValue(true, 80); blendW.setDoubleClickReturnValue(true, 50);
        evolve.onDragStart = [this] { ed.proc.pushUndo(); }; evolve.onValueChange = [this] { ed.pat().evolve = (float) evolve.getValue() / 100.0f; ed.live(); };
        fill.addItemList({ "No fills", "Fill every 2 bars", "Fill every 4 bars", "Fill every 8 bars" }, 1); fill.onChange = [this] { static const int F[] { 0, 2, 4, 8 }; ed.edit([&] { ed.pat().fillEvery = F[juce::jmax(0, fill.getSelectedItemIndex())]; }); };
        conv.addItemList({ "Never realign", "Realign every bar", "every 2 bars", "every 4 bars", "every 8 bars", "every 16 bars" }, 1);
        conv.onChange = [this] { static const int C[] { 0, 1, 2, 4, 8, 16 }; ed.edit([&] { ed.proc.model.converge = C[juce::jmax(0, conv.getSelectedItemIndex())]; }); };
        perfBars.addItemList({ "8 bars", "16 bars", "32 bars", "64 bars" }, 1); perfBars.onChange = [this] { static const int B[] { 8, 16, 32, 64 }; ed.proc.evolveBars = B[juce::jmax(0, perfBars.getSelectedItemIndex())]; };
        for (int i = 0; i < gb::kPatterns; ++i) { blendA.addItem("A = pattern " + patName(i), i + 1); blendB.addItem("B = pattern " + patName(i), i + 1); }
        blendA.setSelectedItemIndex(0, juce::dontSendNotification); blendB.setSelectedItemIndex(1, juce::dontSendNotification);
        for (auto* c : { &fill, &conv, &blendA, &blendB, &perfBars }) addAndMakeVisible(c);
        styleI.setClickingTogglesState(false); styleM.setClickingTogglesState(false);
        styleI.onClick = [this] { ed.edit([&] { ed.pat().evolveStyle = 0; }); }; styleM.onClick = [this] { ed.edit([&] { ed.pat().evolveStyle = 1; }); };
        dice.onClick = [this] { ed.edit([&] { ed.pat().seed = (int) (juce::Random::getSystemRandom().nextInt(99999) + 1); }); };
        genI.onClick = [this] { ed.edit([&] { ed.proc.generatePattern(0); }); say("Generated an INTRICATE pattern (seed " + juce::String(ed.proc.model.toolSeed) + ")."); };
        genM.onClick = [this] { ed.edit([&] { ed.proc.generatePattern(1); }); say("Generated a MACHINE GUN pattern (seed " + juce::String(ed.proc.model.toolSeed) + ")."); };
        genD.onClick = [this] { ed.edit([&] { ed.proc.generatePattern(gb::drill::kStyle); });
                                say("Generated a DRILL pattern (seed " + juce::String(ed.proc.model.toolSeed) + ", " + juce::String(gb::drill::tempoFor(ed.proc.model.toolSeed)) + " BPM, GRIT " + juce::String(ed.proc.model.drillGrit) + "): 2 bars, displaced bar-2 snare, sliding 808."); };
        genD.setTooltip("DRILL: 138-146 BPM in half time, snare on beat 3 then displaced in bar 2, 3-3-2 hats, a sliding 808 (each press rotates the 808 pattern, snare spot and hat cell)");
        genR.onClick = [this] { ed.edit([&] { ed.proc.generatePattern(gb::latin::kStyle); }); const int sd = ed.proc.model.toolSeed;
                                say("Generated a REGGAETON pattern (seed " + juce::String(sd) + ", " + juce::String(gb::latin::tempoFor(sd)) + " BPM, " + juce::String(gb::latin::families()[(size_t) gb::latin::family(sd)].name).fromFirstOccurrenceOf(" ", false, false)
                                    + "): the dembow, " + juce::String(gb::latin::families()[(size_t) gb::latin::family(sd)].fillBars) + " bars with a timbale fill."); };
        genR.setTooltip("REGGAETON: 88-100 BPM, kick on 1 and 3, the dembow snare / rim figure, straight hats, shaker 16ths, clave, congas / bongos, cowbell, timbale fills every 4 or 8 bars (each press rotates the pattern family)");
        genT.onClick = [this] { ed.edit([&] { ed.proc.generatePattern(gb::trapc::kStyle); }); const int sd = ed.proc.model.toolSeed;
                                say("Generated a TRAP CLASSIC pattern (seed " + juce::String(sd) + ", " + juce::String(gb::trapc::tempoFor(sd)) + " BPM, " + juce::String(gb::trapc::families()[(size_t) gb::trapc::family(sd)].name).fromFirstOccurrenceOf(" ", false, false)
                                    + "): 4 bars, rolling hats, the 808 on the kicks, snare + clap on 3, a snare roll into the drop."); };
        genT.setTooltip("TRAP CLASSIC: early-2010s orchestral trap at 140-160 BPM in half time: rolling hats (1/32, triplet, accelerating, pitch-rising, 1/64 rolls with velocity ramps), open-hat off-beats, a long 808 on the kicks with slides, snare + clap on 3, crash + brass on the phrase start, a snare roll into the drop (each press rotates the pattern family and the roll order)");
        keepBtn.onClick = [this] { ed.edit([&] { ed.proc.keepIdentity((float) keep.getValue() / 100.0f); }); say("Kept " + juce::String((int) keep.getValue()) + "% of the pattern's identity, new hits elsewhere."); };
        blendBtn.onClick = [this] { ed.edit([&] { ed.proc.blendPatterns(blendA.getSelectedItemIndex(), blendB.getSelectedItemIndex(), (float) blendW.getValue() / 100.0f); }); say("Blended the structure of the two patterns into " + patName(ed.patIdx()) + "."); };
        breedNew.onClick = [this] { ed.proc.breedPool.clear(); ed.proc.breedNext(16); tiles.playing = -1; tiles.repaint(); say("16 variations. Click to hear, star your favourites, then NEXT GENERATION."); };
        breedNext.onClick = [this] { int n = 0; for (bool b : ed.proc.breedStar) n += b; if (ed.proc.breedPool.empty() || n == 0) { say("Star at least one tile first."); return; }
                                     ed.proc.breedNext(16); tiles.playing = -1; tiles.repaint(); say("Generation " + juce::String(ed.proc.breedGeneration) + ": leaning toward your " + juce::String(n) + " favourites."); };
        bake.onClick = [this] { int made = 0; ed.edit([&] { made = ed.proc.bakePerformance(ed.proc.evolveBars); }); say(made ? "Baked " + juce::String(ed.proc.evolveBars) + " evolving bars into patterns " + patName(ed.patIdx() + 1) + "-" + patName(ed.patIdx() + made) + " + the song (SONG MODE on). Open any of them in the TRACKER." : "No room after this pattern to bake into."); };
        for (auto* b : { &genI, &genM, &genD, &genR, &genT, &keepBtn, &blendBtn, &breedNew, &breedNext, &styleI, &styleM, &dice, &bake }) addAndMakeVisible(b);
        for (auto* b : { &genI, &genM, &genD, &genR, &genT, &breedNew }) b->getProperties().set("accent", true);
        auto lab = [this](juce::Label& l, const juce::String& t) { l.setText(t, juce::dontSendNotification); l.setFont(font(11.0f)); l.setColour(juce::Label::textColourId, GREY); addAndMakeVisible(l); };
        lab(lEvolve, "EVOLVE (bar-by-bar variation, kick 1 + backbeat snares stay)"); lab(lFill, "FILLS"); lab(lConv, "CONVERGENCE (polymeter tracks realign)"); lab(lKeep, "RHYTHM DNA: KEEP x% IDENTITY, NEW HITS");
        lab(lBlend, "BLEND two patterns' structure (beat by beat, not a crossfade)"); lab(lPerf, "PERFORMANCE: drag-out length / bake"); lab(lSeed, ""); lab(lGrit, "DRILL GRIT");
        lab(help, "PHRASE: draw the 16-bar arc EVOLVE follows.  Locks: double-click a step > lock, or LOCK a whole track; generators never touch locked hits. Every action here is one UNDO step and seeded.");
        help.setJustificationType(juce::Justification::topLeft); help.setFont(font(10.5f, false));
        lab(lLocks, "PHRASE LOCKS: these bars play exactly as written (EVOLVE, BREED, KEEP, BLEND, GENERATE keep them)");
        for (int k = 0; k < gb::kLocks; ++k) {
            auto* st = lockStart.add(new juce::ComboBox()); for (int b = 1; b <= 16; ++b) st->addItem("bar " + juce::String(b), b);
            auto* ln = lockLen.add(new juce::ComboBox()); ln->addItem("off", 100); for (int b : { 1, 2, 4, 8 }) ln->addItem(juce::String(b) + (b == 1 ? " bar" : " bars"), b);
            auto* mk = lockMask.add(new juce::TextButton("ALL"));
            auto upd = [this, k] { ed.edit([&] { auto& L = ed.proc.model.locks[(size_t) k]; L.start = juce::jmax(0, lockStart[k]->getSelectedId() - 1); const int id = lockLen[k]->getSelectedId(); L.bars = id == 100 || id <= 0 ? 0 : id; }); };
            st->onChange = upd; ln->onChange = upd;
            mk->onClick = [this, k] { ed.edit([&] { auto& L = ed.proc.model.locks[(size_t) k]; L.mask = L.mask == 0xffff ? (uint16_t) (1u << ed.track) : (uint16_t) 0xffff; }); };
            for (juce::Component* c : std::initializer_list<juce::Component*> { st, ln, mk }) addAndMakeVisible(c);
        }
        lab(lMotif, "MOTIF");
        for (int b = 1; b <= 16; ++b) { motifFrom.addItem("from " + juce::String(b), b); motifLen.addItem(juce::String(b) + " steps", b); }
        motifFrom.setSelectedId(1, juce::dontSendNotification); motifLen.setSelectedId(4, juce::dontSendNotification);
        motifBtn.onClick = [this] { bool ok = false; ed.edit([&] { ok = ed.proc.captureMotif(ed.track, motifFrom.getSelectedId() - 1, motifLen.getSelectedId()); });
                                    say(ok ? "Track " + juce::String(ed.track + 1) + "'s figure is locked as a motif: every bar keeps it." : "Motifs work on a 1/16 grid with up to 4 motifs (CLEAR MOTIFS frees them)."); };
        motifClear.onClick = [this] { ed.edit([&] { ed.proc.model.motifs = {}; }); say("Motifs cleared."); };
        for (juce::Component* c : std::initializer_list<juce::Component*> { &motifFrom, &motifLen, &motifBtn, &motifClear }) addAndMakeVisible(c);
        // the left column's two tabs: the rhythm tools, or LINEAR DRUMMING (a roomy panel of its own)
        for (auto* c : getChildren()) if (c != &arc && c != &tiles && c != &breedNew && c != &breedNext) toolComps.push_back(c);
        toolComps.push_back(&arc);
        for (auto* b : { &tabTools, &tabLinear }) { b->setClickingTogglesState(false); b->setRadioGroupId(7312); addAndMakeVisible(b); }
        tabTools.onClick = [this] { setLinearTab(false); }; tabLinear.onClick = [this] { setLinearTab(true); };
        addChildComponent(linear);
        setLinearTab(false);
        sync();
    }
    void setLinearTab(bool on) {
        linearTab = on; tabTools.setToggleState(!on, juce::dontSendNotification); tabLinear.setToggleState(on, juce::dontSendNotification);
        for (auto* c : toolComps) c->setVisible(!on); linear.setVisible(on); if (on) linear.sync(); repaint();
    }
    void tickStrip() { if (linearTab) linear.tick(); }   // from the editor timer
    void say(const juce::String& s) { if (ed.say) ed.say(s); }
    void sync() {
        auto& p = ed.pat(); evolve.setValue(p.evolve * 100.0, juce::dontSendNotification);
        styleI.setToggleState(p.evolveStyle == 0, juce::dontSendNotification); styleM.setToggleState(p.evolveStyle == 1, juce::dontSendNotification);
        fill.setSelectedItemIndex(p.fillEvery >= 8 ? 3 : (p.fillEvery >= 4 ? 2 : (p.fillEvery >= 2 ? 1 : 0)), juce::dontSendNotification);
        const int c = ed.proc.model.converge; conv.setSelectedItemIndex(c >= 16 ? 5 : c >= 8 ? 4 : c >= 4 ? 3 : c >= 2 ? 2 : c >= 1 ? 1 : 0, juce::dontSendNotification);
        const int b = ed.proc.evolveBars; perfBars.setSelectedItemIndex(b >= 64 ? 3 : b >= 32 ? 2 : b >= 16 ? 1 : 0, juce::dontSendNotification);
        lSeed.setText("seed " + juce::String(p.seed), juce::dontSendNotification); grit.setValue(ed.proc.model.drillGrit, juce::dontSendNotification);
        for (int k = 0; k < gb::kLocks; ++k) { const auto& L = ed.proc.model.locks[(size_t) k]; lockStart[k]->setSelectedId(L.start + 1, juce::dontSendNotification); lockLen[k]->setSelectedId(L.bars > 0 ? L.bars : 100, juce::dontSendNotification);
            int one = -1; for (int t = 0; t < gb::kTracks; ++t) if (L.mask == (1u << t)) one = t; lockMask[k]->setButtonText(one >= 0 ? "TRK " + juce::String(one + 1) : juce::String("ALL")); lockMask[k]->setToggleState(L.active(), juce::dontSendNotification); }
        tabLinear.setButtonText(ed.proc.model.linear.on ? "LINEAR DRUMMING (ON)" : "LINEAR DRUMMING");
        if (linearTab) linear.sync();
        int nm = 0; juce::String ms; for (auto& m : ed.proc.model.motifs) if (m.active()) { ms << (nm ? ", " : "") << "track " << m.track + 1 << " steps " << m.start + 1 << "-" << m.start + m.len; ++nm; }
        lMotif.setText(nm ? "MOTIFS: " + ms : juce::String("MOTIF"), juce::dontSendNotification);
        tiles.repaint(); arc.repaint();
    }
    void paint(juce::Graphics& g) override {
        g.setColour(CARD2.withAlpha(0.5f)); g.fillRoundedRectangle(getLocalBounds().withWidth(560).toFloat(), 10);
        auto r = dnaArea;
        // the current pattern + its DNA (hits per bar position)
        g.setColour(CARD2); g.fillRoundedRectangle(r, 8);
        drawMini(g, ed.pat(), r.reduced(8).withWidth(280), 16);
        const auto dna = gb::rhythm::extract(ed.pat(), ed.proc.roles()); float mx = 1e-6f; for (auto v : dna.posDensity) mx = juce::jmax(mx, v);
        auto d = r.reduced(8).withTrimmedLeft(296); const float w = d.getWidth() / 16.0f;
        for (int i = 0; i < 16; ++i) { const float h = dna.posDensity[(size_t) i] / mx * (d.getHeight() - 24); g.setColour((i % 4 == 0 ? GREEN : PURPLE).withAlpha(0.7f)); g.fillRect(d.getX() + i * w + 2, d.getBottom() - 12 - h, w - 4, h);
            if (dna.accent[(size_t) i] > 0) { g.setColour(AMBER); g.fillRect(d.getX() + i * w + 2, d.getBottom() - 12 - h - 3, w - 4, 2.0f); } }
        g.setColour(GREY); g.setFont(font(9.5f, false));
        g.drawText("DNA  sync " + juce::String(dna.syncopation, 2) + "  vel " + juce::String(dna.velMean, 2) + "+-" + juce::String(dna.velStd, 2) + "  retrig " + juce::String(dna.retrig, 2) + "  silence " + juce::String(dna.silence, 2), d.withHeight(12), juce::Justification::left);
        g.drawText("hits per bar position (amber = accents)", d.withTrimmedTop(d.getHeight() - 11), juce::Justification::left);
    }
    void resized() override {
        int y = 6; const int x = 12, w = 536;
        tabTools.setBounds(x, y, 200, 24); tabLinear.setBounds(x + 206, y, 200, 24); y += 30;
        linear.setBounds(x - 4, y, w + 8, getHeight() - y - 4);
        lEvolve.setBounds(x, y, w, 16); y += 18; evolve.setBounds(x, y, 240, 26); styleI.setBounds(x + 248, y, 96, 26); styleM.setBounds(x + 348, y, 110, 26); dice.setBounds(x + 462, y, 74, 26); y += 30;
        lFill.setBounds(x, y, 50, 24); fill.setBounds(x + 50, y, 160, 26); lSeed.setBounds(x + 220, y, 120, 26); y += 30;
        arc.setBounds(x, y, w, 72); y += 76;
        lConv.setBounds(x, y, 260, 24); conv.setBounds(x + 264, y, 180, 26); y += 30;
        genI.setBounds(x, y, 176, 30); genM.setBounds(x + 180, y, 196, 30); genD.setBounds(x + 380, y, 156, 30); y += 34;
        genR.setBounds(x, y - 2, 168, 28); genT.setBounds(x + 172, y - 2, 192, 28); lGrit.setBounds(x + 368, y, 70, 24); grit.setBounds(x + 438, y, 98, 24); y += 30;
        lKeep.setBounds(x, y, w, 16); y += 17; keep.setBounds(x, y, 360, 24); keepBtn.setBounds(x + 370, y, 166, 24); y += 28;
        lBlend.setBounds(x, y, w, 16); y += 17; blendA.setBounds(x, y, 130, 24); blendB.setBounds(x + 136, y, 130, 24); blendW.setBounds(x + 272, y, 150, 24); blendBtn.setBounds(x + 428, y, 108, 24); y += 28;
        lPerf.setBounds(x, y, 260, 24); perfBars.setBounds(x + 264, y, 110, 24); bake.setBounds(x + 380, y, 156, 24); y += 28;
        lLocks.setBounds(x, y, w, 16); y += 18;
        for (int k = 0; k < gb::kLocks; ++k) { const int cx = x + (k % 2) * 272, cy = y + (k / 2) * 28; lockStart[k]->setBounds(cx, cy, 82, 24); lockLen[k]->setBounds(cx + 86, cy, 86, 24); lockMask[k]->setBounds(cx + 176, cy, 84, 24); }
        y += 58;
        lMotif.setBounds(x, y, 170, 24); motifFrom.setBounds(x + 172, y, 86, 24); motifLen.setBounds(x + 262, y, 90, 24); motifBtn.setBounds(x + 356, y, 90, 24); motifClear.setBounds(x + 450, y, 86, 24); y += 28;
        help.setBounds(x, y, w, juce::jmax(16, getHeight() - y - 4));
        const int tw = juce::jmax(400, getWidth() - 586), th = juce::jmax(250, getHeight() - 176);
        tiles.setBounds(574, 4, tw, th); breedNew.setBounds(580, th + 10, 180, 30); breedNext.setBounds(766, th + 10, 200, 30);
        dnaArea = juce::Rectangle<float>(578.0f, (float) th + 46, (float) tw - 6, (float) juce::jmax(80, getHeight() - th - 50));
    }
    juce::Rectangle<float> dnaArea { 578, 424, 570, 112 };
};

} // namespace bx
