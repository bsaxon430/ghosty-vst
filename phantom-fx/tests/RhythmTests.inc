// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX Phase 5 (Rhythm) checks, included by PhantomTests.cpp: the brief's tempo lock (sample-accurate host sync over 10 minutes with
// tempo changes and loop jumps) for GHOST GATE, GHOST GRID and HAUNT EDIT, GATE's audio trigger with lookahead, WARP's pitch, an empty
// GRID nulls, drawn data survives save / load, and pluginval's state-restoration procedure run directly on the processor.
#include "../src/rhythm/RhythmGate.h"
#include "../src/rhythm/RhythmGrid.h"
#include "../src/rhythm/RhythmHaunt.h"
#include "../src/rhythm/RhythmWarp.h"

namespace rhythmtest
{
// a host song: 10 minutes, a new tempo every 30 s (70..174 BPM), and from minute 5 a loop (bars 1.25 .. 3.25 of each 4-bar phrase,
// so the jumps land mid-pattern) -- the clock every rhythm check plays against
struct Song
{
    double sr = 48000.0, ppq = 0.0, bpm = 120.0; int block = 256; long long abs = 0; bool loopOn = false; int jumps = 0;
    double sampleSeconds() const { return abs / sr; }
    // before each block: tempo and loop jumps (a host jumps at a block start)
    void beginBlock (TempoInfo& t)
    {
        const int seg = (int) (sampleSeconds() / 30.0); bpm = 70.0 + (seg * 37) % 105;
        loopOn = sampleSeconds() >= 300.0;
        if (loopOn) { const double phrase = 16.0, start = 5.0, end = 13.0, inPhrase = std::fmod (ppq, phrase); if (inPhrase >= end) { ppq += start - inPhrase; ++jumps; } }
        t.bpm = bpm; t.ppqPosition = ppq; t.playing = true; t.looping = loopOn; t.sampleRate = sr;
    }
    void endBlock (int len) { ppq += len / (sr * 60.0 / bpm); abs += len; }
};

static void gateTempoLock()
{
    Bench b (std::make_unique<GhostGateFx>()); b.setDefaults();
    auto& fx = dynamic_cast<GhostGateFx&> (b.slot.fx());
    gatedata::Pattern sq; sq.name = "SQUARE"; sq.pts = { { 0.0f, 1.0f, 0, 0 }, { 0.5f, 0.0f, 0, 0 } };   // GATE-12 HOLD segments: open for half a bar
    fx.setPattern (0, sq);
    auto& ps = b.slot.params(); for (size_t i = 0; i < ps.size(); ++i) if (ps[i].id == "smooth") b.slot.setValue ((int) i, 0.0f);
    Song song; juce::AudioBuffer<float> buf (2, song.block);
    std::vector<long long> expected, measured; float prevOut = 1.0f; int prevIdeal = -1;
    const long long total = (long long) (600.0 * song.sr);
    while (song.abs < total)
    {
        song.beginBlock (b.tempo); const double ppq0 = song.ppq, spb = b.tempo.samplesPerBeat();
        for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::fill (buf.getWritePointer (c), 1.0f, song.block);
        juce::dsp::AudioBlock<float> blk (buf); b.slot.process (blk, b.tempo);
        for (int i = 0; i < song.block; ++i)
        {
            const double p = ppq0 + i / spb; const int ideal = rhythm::positiveMod (p, 4.0) < 2.0 ? 1 : 0;   // 1 bar of 4/4, open first half
            if (prevIdeal >= 0 && ideal != prevIdeal) expected.push_back ((song.abs + i) * 2 + ideal);
            prevIdeal = ideal;
            const float y = buf.getSample (0, i);
            if ((prevOut < 0.5f) != (y < 0.5f)) measured.push_back ((song.abs + i) * 2 + (y >= 0.5f ? 1 : 0));
            prevOut = y;
        }
        song.endBlock (song.block);
    }
    // pair each expected edge with the first measured edge of the same direction at or after it; the slew delay must be the same every time
    long long lo = LLONG_MAX, hi = LLONG_MIN; size_t m = 0; int paired = 0;
    for (auto e : expected)
    {
        while (m < measured.size() && (measured[m] / 2 < e / 2 || (measured[m] & 1) != (e & 1))) ++m;
        if (m >= measured.size()) break;
        const long long d = measured[m] / 2 - e / 2; lo = std::min (lo, d); hi = std::max (hi, d); ++paired; ++m;
    }
    report (paired > 300 && paired == (int) expected.size() && hi - lo <= 1, "GHOST GATE tempo lock",
            juce::String (paired) + " drawn edges over 10 min at 70-174 BPM with " + juce::String (song.jumps) + " loop jumps; each lands " + juce::String (lo) + ".." + juce::String (hi)
            + " samples after its exact host-clock sample (the constant is the 0.3 ms anti-click slew; spread limit 1)");
}

static void gridTempoLock()
{
    Bench b (std::make_unique<GhostGridFx>()); b.setDefaults();
    auto& fx = dynamic_cast<GhostGridFx&> (b.slot.fx());
    griddata::Pattern p; p.name = "TEST"; p.rows[griddata::rReverse] = (1u << 0) | (1u << 4) | (1u << 9) | (1u << 13); p.rows[griddata::rGate] = (1u << 2) | (1u << 3) | (1u << 11);
    fx.setPattern (0, p);
    Song song; juce::AudioBuffer<float> buf (2, song.block); auto nz = noise (song.block, 0.3f, 4);
    int checked = 0, wrong = 0; long long worst = 0; std::array<int, griddata::kRows> seen {};
    long long lastUnit = LLONG_MIN; bool contiguous = true;
    const long long total = (long long) (600.0 * song.sr);
    while (song.abs < total)
    {
        song.beginBlock (b.tempo); const double ppq0 = song.ppq, spb = b.tempo.samplesPerBeat(), stepBeats = 0.25;   // 16 steps in 4/4
        for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, nz, c, 0, song.block);
        // what should enter in this block, sample by sample, from the host clock alone
        std::vector<std::pair<int, int>> want;   // (row, sample)
        for (int i = 0; i < song.block; ++i)
        {
            const long long unit = (long long) std::floor ((ppq0 + i / spb) / stepBeats + 1.0e-9);
            if (unit == lastUnit) continue;
            contiguous = unit == lastUnit + 1; lastUnit = unit; const int step = (int) rhythm::positiveMod ((double) unit, 16.0);
            for (int r : { (int) griddata::rReverse, (int) griddata::rGate })
            {
                const uint32_t mk = p.rows[(size_t) r]; const bool on = (mk >> step) & 1u, prevOn = step > 0 && ((mk >> (step - 1)) & 1u);
                if (on && (step == 0 || ! prevOn || ! contiguous)) want.push_back ({ r, i });
            }
        }
        std::array<int, griddata::kRows> before = fx.debugEnters;
        juce::dsp::AudioBlock<float> blk (buf); b.slot.process (blk, b.tempo);
        for (auto [r, i] : want)
        {
            ++checked;
            if (fx.debugEnters[(size_t) r] != before[(size_t) r] + 1) { ++wrong; continue; }
            const long long d = fx.debugEnterAt[(size_t) r] - (song.abs + i); worst = std::max (worst, std::abs (d)); if (d != 0) ++wrong;
        }
        for (int r : { (int) griddata::rReverse, (int) griddata::rGate }) seen[(size_t) r] += fx.debugEnters[(size_t) r] - before[(size_t) r];
        song.endBlock (song.block);
    }
    const int extra = seen[griddata::rReverse] + seen[griddata::rGate] - checked;
    report (checked > 1000 && wrong == 0 && extra == 0 && finite (buf), "GHOST GRID tempo lock (sample-accurate block boundaries)",
            juce::String (checked) + " painted blocks over 10 min at 70-174 BPM with " + juce::String (song.jumps) + " loop jumps; every block entered on its exact sample: "
            + (wrong == 0 ? juce::String ("yes") : "NO (" + juce::String (wrong) + " off, worst " + juce::String (worst) + " samples)") + "; unexpected blocks " + juce::String (extra));
}

static void hauntTempoLock()
{
    Bench b (std::make_unique<HauntEditFx>()); b.setDefaults();
    auto& fx = dynamic_cast<HauntEditFx&> (b.slot.fx());
    Song song; juce::AudioBuffer<float> buf (2, song.block); auto nz = noise (song.block, 0.3f, 6);
    std::mt19937 rng (5); std::uniform_int_distribution<int> off (0, song.block - 1);
    int presses = 0, bad = 0, note = 0; long long worstOrigin = 0; bool held = false; long long nextAt = 24000;
    const long long total = (long long) (600.0 * song.sr); NoteEvent ev[1];
    while (song.abs < total)
    {
        song.beginBlock (b.tempo); const double ppq0 = song.ppq, spb = b.tempo.samplesPerBeat();
        for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, nz, c, 0, song.block);
        b.tempo.notes = nullptr; b.tempo.numNotes = 0; int at = -1;
        if (song.abs + song.block > nextAt)
        {
            at = off (rng); if (! held) note = presses % 4; ev[0] = { at, hauntdata::kNoteBase + note, held ? 0.0f : 0.9f };   // pads 1-4: stutters 1/4 .. 1/32
            b.tempo.notes = ev; b.tempo.numNotes = 1;
        }
        juce::dsp::AudioBlock<float> blk (buf); b.slot.process (blk, b.tempo);
        if (at >= 0)
        {
            if (! held)
            {
                const long long press = song.abs + at; const double p = ppq0 + at / spb;
                static const double grid[] { 1.0, 0.5, 0.25, 0.125 };   // pads 1-4 are 1/4, 1/8, 1/16, 1/32 stutters: each aligns to its own grid
                const long long origin = press - (long long) std::llround (rhythm::positiveMod (p, grid[note]) * spb);
                if (fx.debugPress != press) ++bad;
                const long long d = fx.debugOrigin - origin; worstOrigin = std::max (worstOrigin, std::abs (d)); if (std::abs (d) > 1) ++bad;
                ++presses;
            }
            held = ! held; nextAt = song.abs + song.block + (held ? 48000 : 72000);
        }
        b.tempo.notes = nullptr; b.tempo.numNotes = 0;
        song.endBlock (song.block);
    }
    report (presses > 100 && bad == 0 && finite (buf), "HAUNT EDIT tempo lock (MIDI pads)",
            juce::String (presses) + " MIDI pad presses over 10 min at 70-174 BPM with " + juce::String (song.jumps) + " loop jumps: each gesture starts on the note's own sample (latency-free), its slices on the host's grid within "
            + juce::String (worstOrigin) + " sample (limit 1); misses " + juce::String (bad));
}

static void gateAudioTrigger()
{
    Bench b (std::make_unique<GhostGateFx>()); b.setDefaults();
    auto& fx = dynamic_cast<GhostGateFx&> (b.slot.fx());
    gatedata::Pattern sq; sq.pts = { { 0.0f, 1.0f, 0, 0 }, { 0.5f, 0.0f, 0, 0 } }; fx.setPattern (0, sq);
    auto& ps = b.slot.params();
    for (size_t i = 0; i < ps.size(); ++i) { if (ps[i].id == "trig") b.slot.setValue ((int) i, 1.0f); if (ps[i].id == "plook") b.slot.setValue ((int) i, 1.0f); if (ps[i].id == "smooth") b.slot.setValue ((int) i, 0.0f); }
    b.slot.applyPolicy();
    const int lat = b.slot.getLatencySamples(); const int n = 48000 * 8; juce::AudioBuffer<float> buf (2, n); buf.clear();
    std::vector<int> hits; for (int t = 30000; t + 2000 < n; t += 96000) { hits.push_back (t); for (int k = 0; k < 2400; ++k) for (int c = 0; c < 2; ++c) buf.setSample (c, t + k, 0.8f * std::exp (-k / 600.0f) * (k % 2 ? 1.0f : -1.0f)); }
    auto in = buf; b.run (buf);
    int worst = 0; bool kept = true;
    for (int t : hits)
    {   // the first sample of the hit must come out (the gate opened before it, thanks to the lookahead) and the level after it is the hit's own
        const float a = std::abs (buf.getSample (0, t + lat)), want = std::abs (in.getSample (0, t));
        if (a < 0.5f * want) kept = false;
        int open = -1; for (int k = -lat; k < 600; ++k) if (std::abs (buf.getSample (0, t + lat + k)) > 1.0e-4f) { open = k; break; }
        worst = std::max (worst, std::abs (open));
    }
    report (lat == 240 && kept, "GHOST GATE audio trigger with lookahead",
            juce::String ((int) hits.size()) + " hits; latency " + juce::String (lat) + " samples (5 ms, reported); the first sample of every hit passes: " + (kept ? "yes" : "NO") + "; gate opening within " + juce::String (worst) + " samples of the hit");
}

static void warpPitch()
{
    for (int mode : { 0, 1 })
    {
        Bench b (std::make_unique<WarpFx>()); b.setDefaults();
        auto& fx = dynamic_cast<WarpFx&> (b.slot.fx());
        bendlane::Shape s; s.add (0.0f, 7.0f / 12.0f); s.sanitise(); fx.setLane (s);   // a flat +7 semitone lane (range +-12)
        auto& ps = b.slot.params(); for (size_t i = 0; i < ps.size(); ++i) if (ps[i].id == "mode") b.slot.setValue ((int) i, (float) mode);
        b.slot.applyPolicy();
        const int n = 48000 * 2; auto buf = sine (48000.0, n, 440.0f, 0.5f); b.run (buf);
        // frequency from zero crossings over the last second (TAPE: a constant bend reads its lane cycle at the new speed, then the head returns)
        int zc = 0; double first = -1, last = -1; for (int i = 48000 + 1; i < 48000 + 9600; ++i) if (buf.getSample (0, i - 1) < 0 && buf.getSample (0, i) >= 0) { const double f = i - 1 + buf.getSample (0, i - 1) / (buf.getSample (0, i - 1) - buf.getSample (0, i)); if (first < 0) first = f; last = f; ++zc; }
        const double hz = zc > 1 ? (zc - 1) * 48000.0 / (last - first) : 0.0, want = 440.0 * std::pow (2.0, 7.0 / 12.0);
        report (std::abs (hz / want - 1.0) < 0.005, juce::String ("WARP pitch (") + (mode == 0 ? "PITCH" : "TAPE") + ")", "a +7 semitone lane plays 440 Hz at " + juce::String (hz, 2) + " Hz (want " + juce::String (want, 2) + ")");
    }
}

static void gridEmptyNull()
{
    Bench b (std::make_unique<GhostGridFx>()); b.setDefaults();
    auto& fx = dynamic_cast<GhostGridFx&> (b.slot.fx()); griddata::Pattern e; fx.setPattern (0, e);
    auto in = bandNoise (48000.0, 96000, 0.5f); auto out = in; b.run (out);
    float worst = 0; for (int c = 0; c < 2; ++c) for (int i = 0; i < out.getNumSamples(); ++i) worst = juce::jmax (worst, std::abs (out.getSample (c, i) - in.getSample (c, i)));
    report (db (worst) < -100.0f, "GHOST GRID null test (empty pattern)", "residual " + juce::String (db (worst), 1) + " dBFS (limit -100)");
}

// scenes, A/B, unit A/B, unit presets and presets carry the drawn patterns (0.1.10); a scene stored by 0.1.9 (no drawn data in it)
// still loads and leaves the patterns alone; with a morph time the patterns change at the switch, they do not glide
static void drawnInScenes()
{
    auto txt = [] (PhantomProcessor& p, int u) { return juce::JSON::toString (p.getUnitData (u), true); };
    auto gateLevel = [] (PhantomProcessor& p, float lv) { auto& g = dynamic_cast<GhostGateFx&> (p.rack.slot (uGate).fx()); auto pt = g.pattern (3); pt.pts[1].level = lv; g.setPattern (3, pt); p.setUnitData (uGate, g.getData()); };
    auto gridRow = [] (PhantomProcessor& p, unsigned bits) { auto& g = dynamic_cast<GhostGridFx&> (p.rack.slot (uGrid).fx()); auto pt = g.pattern (2); pt.rows[griddata::rFreeze] = bits; g.setPattern (2, pt); p.setUnitData (uGrid, g.getData()); };
    PhantomProcessor a; a.prepareToPlay (48000.0, 256); a.setParamNormalised ("scene_quant", 0.0f); a.setParamNormalised ("scene_time", 0.0f);
    gateLevel (a, 0.25f); gridRow (a, 0x8001u); const auto gA = txt (a, uGate), rA = txt (a, uGrid); a.storeScene (0);
    gateLevel (a, 0.75f); gridRow (a, 0x00f0u); const auto gB = txt (a, uGate), rB = txt (a, uGrid); a.storeScene (1);
    a.recallScene (0); const bool s0 = txt (a, uGate) == gA && txt (a, uGrid) == rA;
    a.recallScene (1); const bool s1 = txt (a, uGate) == gB && txt (a, uGrid) == rB;
    // a morph time (the longest): the pattern is the new scene's at once
    a.setParamNormalised ("scene_time", 1.0f); a.recallScene (0); const bool snap = txt (a, uGate) == gA; a.setParamNormalised ("scene_time", 0.0f); a.recallScene (0);
    // save / load: the scenes still hold their patterns
    juce::MemoryBlock mb; a.getStateInformation (mb); PhantomProcessor b; b.prepareToPlay (48000.0, 256); b.setStateInformation (mb.getData(), (int) mb.getSize());
    b.setParamNormalised ("scene_quant", 0.0f); b.setParamNormalised ("scene_time", 0.0f);
    const bool loaded0 = txt (b, uGate) == gA; b.recallScene (1); const bool loaded1 = txt (b, uGate) == gB && txt (b, uGrid) == rB;
    // an old state: the same bytes with the drawn data taken out of every scene (what 0.1.9 wrote)
    bool oldOk = false, oldKeeps = false;
    if (auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize()))
    {
        std::function<void (juce::XmlElement&)> strip = [&] (juce::XmlElement& e) { if (e.hasTagName ("PARAMS")) { e.removeAttribute ("drawn"); e.removeAttribute ("drawnVersion"); } for (auto* c : e.getChildIterator()) strip (*c); };
        strip (*xml); juce::MemoryBlock old; juce::AudioProcessor::copyXmlToBinary (*xml, old);
        PhantomProcessor c; c.prepareToPlay (48000.0, 256); c.setStateInformation (old.getData(), (int) old.getSize()); c.setParamNormalised ("scene_quant", 0.0f); c.setParamNormalised ("scene_time", 0.0f);
        oldOk = c.sceneFilled (0) && c.sceneFilled (1) && txt (c, uGate) == gA; c.recallScene (1); oldKeeps = txt (c, uGate) == gA;   // (recalled: parameters move, the patterns stay)
    }
    // A/B of the whole rack, A/B of one unit, a unit preset and a preset
    PhantomProcessor d; d.prepareToPlay (48000.0, 256); gateLevel (d, 0.2f); const auto dA = txt (d, uGate); d.abToggle(); gateLevel (d, 0.9f); const auto dB = txt (d, uGate);
    d.abToggle(); const bool abA = txt (d, uGate) == dA; d.abToggle(); const bool abB = txt (d, uGate) == dB;
    d.unitAbToggle (uGate); gateLevel (d, 0.5f); const auto uB = txt (d, uGate); d.unitAbToggle (uGate); const bool uab = txt (d, uGate) == dB; d.unitAbToggle (uGate); const bool uab2 = txt (d, uGate) == uB;
    const auto unitJson = d.unitToJson (uGate); const auto full = d.toJson ("t", {});
    PhantomProcessor f; f.prepareToPlay (48000.0, 256); const bool up = f.unitFromJson (uGate, unitJson) && txt (f, uGate) == uB;
    PhantomProcessor g2; g2.prepareToPlay (48000.0, 256); const bool pr = g2.fromJson (juce::JSON::parse (juce::JSON::toString (full))) && txt (g2, uGate) == uB;
    const bool all = s0 && s1 && snap && loaded0 && loaded1 && oldOk && oldKeeps && abA && abB && uab && uab2 && up && pr;
    auto yn = [] (bool v) { return juce::String (v ? "yes" : "NO"); };
    report (all, "Rhythm drawn data in scenes, A/B and presets", "two scenes recall their own gate curve and grid: " + yn (s0 && s1) + "; with a 4-bar morph the pattern changes at the switch: " + yn (snap) + "; after save / load: " + yn (loaded0 && loaded1)
            + "; a 0.1.9 state (scenes without patterns) loads, and recalling its scenes leaves the patterns alone: " + yn (oldOk && oldKeeps) + "; rack A/B: " + yn (abA && abB) + "; unit A/B: " + yn (uab && uab2) + "; unit preset: " + yn (up) + "; preset: " + yn (pr));
}

static void dataRoundTrip()
{
    PhantomProcessor a; a.prepareToPlay (48000.0, 256);
    { auto& g = dynamic_cast<GhostGateFx&> (a.rack.slot (uGate).fx()); auto p = g.pattern (3); p.pts[1].level = 0.33f; p.steps[5] = rhythm::tmReverse; g.setPattern (3, p); a.setUnitData (uGate, g.getData()); }
    { auto& g = dynamic_cast<GhostGridFx&> (a.rack.slot (uGrid).fx()); auto p = g.pattern (2); p.rows[griddata::rFreeze] = 0x8001u; g.setPattern (2, p); a.setUnitData (uGrid, g.getData()); }
    { auto& h = dynamic_cast<HauntEditFx&> (a.rack.slot (uHaunt).fx()); h.setPad (7, { hauntdata::htCrush, 10, 0.9f }); a.setUnitData (uHaunt, h.getData()); }
    { auto& w = dynamic_cast<WarpFx&> (a.rack.slot (uWarp).fx()); w.setLane (bendlane::factory()[3].shape); a.setUnitData (uWarp, w.getData()); }
    juce::MemoryBlock mb; a.getStateInformation (mb);
    PhantomProcessor b; b.prepareToPlay (48000.0, 256); b.setStateInformation (mb.getData(), (int) mb.getSize());
    bool same = true; for (int u : { (int) uGate, (int) uGrid, (int) uHaunt, (int) uWarp }) if (juce::JSON::toString (a.getUnitData (u), true) != juce::JSON::toString (b.getUnitData (u), true)) same = false;
    juce::MemoryBlock mb2; b.getStateInformation (mb2);
    PhantomProcessor c; bool fresh = juce::JSON::toString (c.getUnitData (uGate), true) == juce::JSON::toString (GhostGateFx().getData(), true) && juce::JSON::toString (c.getUnitData (uHaunt), true) == juce::JSON::toString (HauntEditFx().getData(), true);
    report (same && mb == mb2 && fresh, "Rhythm drawn data save / load", juce::String ("patterns, painted grid, pads and bend lane identical after reload: ") + (same ? "yes" : "NO") + "; state bytes identical: " + (mb == mb2 ? "yes" : "NO")
            + "; a new instance starts with the factory data: " + (fresh ? "yes" : "NO"));
}

// pluginval's "Plugin state restoration" (strictness 6+), done to the processor itself, with the message loop running between the steps
// (timers and async updates fire, as they do in a host)
static void pluginvalRestoration()
{
    PhantomProcessor p; p.prepareToPlay (48000.0, 256);
    auto pump = [] { juce::MessageManager::getInstance()->runDispatchLoopUntil (20); };
    juce::MemoryBlock orig; p.getStateInformation (orig); p.setStateInformation (orig.getData(), (int) orig.getSize()); pump();
    juce::Random r (0x595d4c5); int bad = 0, n = 0; juce::String names;
    for (auto* q : p.getParameters())
    {
        if (! q->isAutomatable() || q == p.getBypassParameter()) continue;
        const float o = q->getValue(); const int steps = q->getNumSteps();
        float v = r.nextFloat(); if (q->isDiscrete() && steps > 1) v = (float) r.nextInt (steps) / (float) (steps - 1);   // what pluginval picks for a stepped parameter
        q->setValue (v); q->sendValueChangedMessageToListeners (v); pump();   // what JUCE's VST3 wrapper does with a host's value
        p.setStateInformation (orig.getData(), (int) orig.getSize()); pump();
        ++n; if (std::abs (q->getValue() - o) > 0.1f) { ++bad; if (names.length() < 300) names += q->getName (40) + " "; }
    }
    juce::MemoryBlock again; p.getStateInformation (again);
    report (bad == 0 && again == orig, "state restoration (pluginval procedure)",
            juce::String (n) + " parameters each randomised then restored: " + (bad == 0 ? juce::String ("all back") : juce::String (bad) + " NOT restored: " + names) + "; state read back identical: " + (again == orig ? "yes" : "NO"));
}

static void run()
{
    gateTempoLock(); gridTempoLock(); hauntTempoLock(); gateAudioTrigger(); warpPitch(); gridEmptyNull(); dataRoundTrip(); drawnInScenes(); pluginvalRestoration();
}
} // namespace rhythmtest
