// Mr. Mickens 2 tests + demo renders. Offline, deterministic, no ears needed: everything is checked by numbers.
//   MickensTests                 run every test (exit code = number of failures)
//   MickensTests demos <dir>     render the demo WAVs into <dir>
//   MICKENS_OLD_REF=<dir>        sessions + bounces made by the pre-Mickens-2 build (MickensOldRef): the back-compat test
#include "MickensProcessor.h"
#include "MickensEditor.h"
#include "BreakDNA.h"
#include <iostream>
#include <cmath>
#include <set>

static int fails = 0, passes = 0;
static void check(bool ok, const juce::String& what) { std::cout << (ok ? "PASS  " : "FAIL  ") << what << std::endl; if (ok) ++passes; else ++fails; }
static void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }
static void waitReady(MickensProcessor& p) { for (int i = 0; i < 1200 && !(p.libReady() && p.bayBreak[0] >= 0); ++i) pump(50); }
static void setP(MickensProcessor& p, const juce::String& id, float v) { auto* pr = p.apvts.getParameter(id); if (!pr) { std::cerr << "no parameter " << id << "\n"; std::exit(3); } pr->setValueNotifyingHost(pr->convertTo0to1(v)); }
static float getP(MickensProcessor& p, const juce::String& id) { return p.apvts.getRawParameterValue(id)->load(); }
static int keyIndex(MickensProcessor& p, const juce::String& key) { for (int i = 0; i < p.numBreaks(); ++i) if (MickensProcessor::keyOf(*p.breakAt(i)) == key) return i; return -1; }
struct Peek : MickensProcessor { using juce::AudioProcessor::getXmlFromBinary; using juce::AudioProcessor::copyXmlToBinary; };
static mk::BreakPtr byName(const mk::BreakLibrary& lib, const juce::String& name) { for (auto& b : lib.breaks()) if (b->name == name) return b; return nullptr; }

struct Head : juce::AudioPlayHead { double bpm = 174, ppq = 0; bool playing = true; double sr = 48000;
    juce::Optional<PositionInfo> getPosition() const override { PositionInfo i; i.setBpm(bpm); i.setPpqPosition(ppq); i.setIsPlaying(playing); i.setTimeInSamples((juce::int64) (ppq * 60.0 / bpm * sr)); return i; } };

struct Stats { float peak = 0, rms = 0; bool nan = false; };
static Stats stats(const juce::AudioBuffer<float>& b, int from = 0, int to = -1) {
    Stats s; if (to < 0) to = b.getNumSamples(); double acc = 0; long n = 0;
    for (int c = 0; c < b.getNumChannels(); ++c) for (int i = from; i < to; ++i) { const float x = b.getSample(c, i); if (!std::isfinite(x)) s.nan = true; s.peak = juce::jmax(s.peak, std::abs(x)); acc += x * x; ++n; }
    s.rms = n ? (float) std::sqrt(acc / n) : 0; return s;
}
static void writeWav(const juce::AudioBuffer<float>& b, double sr, const juce::File& f) {
    f.deleteFile(); juce::WavAudioFormat wav;
    if (auto os = std::unique_ptr<juce::OutputStream>(f.createOutputStream()))
        if (auto* w = wav.createWriterFor(os.get(), sr, (unsigned) b.getNumChannels(), 24, {}, 0)) { os.release(); std::unique_ptr<juce::AudioFormatWriter> wr(w); wr->writeFromAudioSampleBuffer(b, 0, b.getNumSamples()); }
}
static juce::AudioBuffer<float> readWav(const juce::File& f, double& sr) {
    juce::AudioFormatManager fm; fm.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(f)); juce::AudioBuffer<float> b;
    if (!r) return b; sr = r->sampleRate; b.setSize((int) r->numChannels, (int) r->lengthInSamples); r->read(&b, 0, (int) r->lengthInSamples, 0, true, true); return b;
}

// render through the processor like a host: bars at the head's tempo; perBlock(sampleStart, midi) may set parameters / add notes
static juce::AudioBuffer<float> hostRender(MickensProcessor& p, Head& head, int bars, int block, int prepBlock, const std::function<void(long, juce::MidiBuffer&)>& perBlock = {}) {
    const double spb = head.sr * 60.0 / head.bpm; const int total = (int) std::ceil(bars * 4 * spb);
    p.setRateAndBufferSizeDetails(head.sr, prepBlock); p.prepareToPlay(head.sr, prepBlock); p.setPlayHead(&head); head.ppq = 0;
    juce::AudioBuffer<float> out(2, total); out.clear(); juce::AudioBuffer<float> buf(2, block); juce::MidiBuffer midi;
    for (int pos = 0; pos < total; pos += block) {
        const int n = juce::jmin(block, total - pos); buf.setSize(2, n, false, false, true); buf.clear(); midi.clear();
        if (perBlock) perBlock(pos, midi);
        head.ppq = pos / spb;
        p.processBlock(buf, midi);
        for (int c = 0; c < 2; ++c) out.copyFrom(c, pos, buf, c, 0, n);
    }
    return out;
}
// the bare engine: internal clock, one snapshot
struct Rig { mk::Engine e; mk::Snapshot snap; std::array<mk::BayParams, mk::kBays> B; mk::FxParams F; double sr = 48000; int block = 512;
    std::vector<std::shared_ptr<const mk::BreakDNA>> keep; std::vector<mk::BreakPtr> keepB;   // the snapshot holds raw pointers: keep the breaks alive
    Rig() { for (auto& b : B) b.mute = true; F.reverb = 0; F.crunch = 0; }
    void bay(int b, const mk::BreakPtr& br) { keepB.push_back(br); snap.brk[(size_t) b] = br.get(); auto d = std::make_shared<const mk::BreakDNA>(mk::analyzeBreak(*br)); keep.push_back(d); snap.dna[(size_t) b] = d.get(); B[(size_t) b].mute = false; }
    juce::AudioBuffer<float> run(int bars, const std::function<void(long, juce::MidiBuffer&)>& perBlock = {}) {
        e.prepare(sr, block); e.setSnapshot(&snap); e.playing.store(true); e.restart();
        const double spb = sr * 60.0 / F.bpm; const int total = (int) std::ceil(bars * 4 * spb);
        juce::AudioBuffer<float> out(2, total); out.clear(); juce::AudioBuffer<float> buf(2, block); juce::MidiBuffer midi;
        for (int pos = 0; pos < total; pos += block) { const int n = juce::jmin(block, total - pos); buf.setSize(2, n, false, false, true); midi.clear(); if (perBlock) perBlock(pos, midi);
            e.process(buf, B, F, F.bpm, 0, false, midi); for (int c = 0; c < 2; ++c) out.copyFrom(c, pos, buf, c, 0, n); }
        return out;
    }
    double stepLen() const { return sr * 60.0 / F.bpm / 4.0; }
};

// ------------------------------------------------------------------------------------------------ tests
static void testDNA(const mk::BreakLibrary& lib) {
    const char* nm[] = { "kick", "snare", "ghost", "hat", "cymbal" }; const double evMin[] = { 0.95, 0.9, 0.5, 0.65, 0.28 }, stMin[] = { 0.99, 0.98, 0.9, 0.78, 0.84 };   // the blind classifier (files); built-ins are exact
    for (int c = 0; c < 5; ++c) { int tp = 0, fp = 0, fn = 0, agree = 0, tot = 0;
        for (auto& b : lib.breaks()) { mk::Break blind = *b; blind.truth.clear(); auto d = mk::analyzeBreak(blind); for (int s = 0; s < 32; ++s) { const bool pr = d.cls[(size_t) s] & (1 << c), t = b->truth[(size_t) s] & (1 << c); tp += pr && t; fp += pr && !t; fn += t && !pr; agree += pr == t; ++tot; } }
        const double ev = tp / (double) juce::jmax(1, tp + fp + fn), st = agree / (double) tot;
        check(ev >= evMin[c] && st >= stMin[c], juce::String("DNA classifier (blind, for files) ") + nm[c] + ": event agreement " + juce::String(ev, 3) + " (min " + juce::String(evMin[c], 2) + "), step agreement " + juce::String(st, 3) + " (min " + juce::String(stMin[c], 2) + ")"); }
    // the built-in breaks use what was really played (Break::truth): exact classes
    { int exact = 0; for (auto& b : lib.breaks()) { auto d = mk::analyzeBreak(*b); bool ok = d.steps == 32; for (int s = 0; ok && s < 32; ++s) ok = d.cls[(size_t) s] == (b->truth[(size_t) s] & 31); exact += ok; }
      check(exact == (int) lib.breaks().size(), "DNA: built-in breaks carry their exact drum events (" + juce::String(exact) + "/" + juce::String((int) lib.breaks().size()) + ")"); }
    // every built-in break: a kick on the downbeat, swing within a 16th, descriptors in range
    int downbeat = 0, inRange = 0; for (auto& b : lib.breaks()) { auto d = mk::analyzeBreak(*b); downbeat += (d.cls[0] & mk::kK) != 0;
        inRange += std::abs(d.swing) < 0.5f && d.density >= 0 && d.density <= 1 && d.bright >= 0 && d.bright <= 1 && d.ghostDensity <= 1 && d.kickSparse >= 0 && d.kickSparse <= 1; }
    check(downbeat == (int) lib.breaks().size(), "DNA: every built-in break has a kick on step 1 (" + juce::String(downbeat) + "/" + juce::String((int) lib.breaks().size()) + ")");
    check(inRange == (int) lib.breaks().size(), "DNA: feel descriptors in range for every break");
    // string round trip (FREEZE DNA in the session)
    bool rt = true; for (int i = 0; i < 20; ++i) { auto d = mk::analyzeBreak(*lib.breaks()[(size_t) i]); mk::BreakDNA e; if (!mk::BreakDNA::fromString(d.toString(), e)) { rt = false; break; }
        if (e.steps != d.steps) rt = false; for (int s = 0; s < d.steps; ++s) { if (e.cls[(size_t) s] != d.cls[(size_t) s] || std::abs(e.accent[(size_t) s] - d.accent[(size_t) s]) > 0.001f || std::abs(e.offset[(size_t) s] - d.offset[(size_t) s]) > 0.001f) rt = false; }
        if (std::abs(e.swing - d.swing) > 0.001f) rt = false; for (int g = 0; g < 6; ++g) if (e.nCls[g] != d.nCls[g]) rt = false; }
    check(rt, "DNA: toString / fromString round trip (classes, accents, offsets, swing, class index)");
}

static void testRemix(const mk::BreakLibrary& lib) {
    auto A = byName(lib, "Haunt - Original"), Bk = byName(lib, "Static - Original");
    const auto dA = mk::analyzeBreak(*A), dB = mk::analyzeBreak(*Bk);
    // remix 0 = exactly the classic engine
    { bool id = true; mk::RemixParams P; for (long s = 0; s < 128; ++s) { auto p = mk::planStep(&dA, &dA, P, (int) (s % 32), s); if (!p.identity || p.slice != (int) (s % 32) || p.ratchet != 1 || p.reverse || std::abs(p.gain - 1) > 1e-6f) id = false; }
      check(id, "REMIX 0: every step is the classic slice, untouched (identity)"); }
    // deterministic, and the seed matters
    { mk::RemixParams P; P.mode = mk::kMutate; P.remix = 100; P.lock = 0; P.lockMask = 0; P.seed = 77; int same = 0, diff = 0;
      for (long s = 0; s < 64; ++s) { auto a = mk::planStep(&dA, &dA, P, (int) (s % 32), s), b = mk::planStep(&dA, &dA, P, (int) (s % 32), s); same += a.slice == b.slice && a.ratchet == b.ratchet && a.gain == b.gain && a.semis == b.semis;
          mk::RemixParams Q = P; Q.seed = 78; auto c = mk::planStep(&dA, &dA, Q, (int) (s % 32), s); diff += c.slice != a.slice || c.ratchet != a.ratchet || c.gain != a.gain; }
      check(same == 64, "REMIX: deterministic (same seed, same plan)"); check(diff > 16, "REMIX: a new seed changes the plan (" + juce::String(diff) + "/64 steps differ)"); }
    // GROOVE LOCK: K/S kept exactly at LOCK 100 in REWIRE and MUTATE; without the lock MUTATE moves kicks/snares
    for (int mode : { mk::kRewire, mk::kMutate }) {
        mk::RemixParams P; P.mode = mode; P.remix = 100; P.lock = 1; P.lockMask = mk::kLockKS; P.seed = 5; int kept = 0, moved = 0, movedUnlocked = 0;
        for (long s = 0; s < 128; ++s) { const int pos = (int) (s % 32); auto p = mk::planStep(&dA, &dA, P, pos, s);
            const bool ok = p.slice >= 0 && (dA.cls[(size_t) p.slice] & (mk::kK | mk::kS)) == (dA.cls[(size_t) pos] & (mk::kK | mk::kS)) && (p.cls & (mk::kK | mk::kS)) == (dA.cls[(size_t) pos] & (mk::kK | mk::kS)); kept += ok; moved += p.slice != pos;
            mk::RemixParams U = P; U.lock = 0; U.lockMask = 0; auto q = mk::planStep(&dA, &dA, U, pos, s); movedUnlocked += q.slice >= 0 && (dA.cls[(size_t) q.slice] & (mk::kK | mk::kS)) != (dA.cls[(size_t) pos] & (mk::kK | mk::kS)); }
        check(kept == 128, juce::String(mode == mk::kRewire ? "REWIRE" : "MUTATE") + " 100 + LOCK K/S 100: kicks and snares land exactly where the break has them (" + juce::String(kept) + "/128), " + juce::String(moved) + " slices swapped");
        check(moved > 20, juce::String(mode == mk::kRewire ? "REWIRE" : "MUTATE") + " 100 still swaps plenty of slices under the lock (" + juce::String(moved) + ")");
        if (mode == mk::kMutate) check(movedUnlocked > 10, "MUTATE 100 without the lock does move kicks/snares (" + juce::String(movedUnlocked) + "/128): the lock is doing something");
    }
    // POSITIONS lock: no new hits, no new rests
    { mk::RemixParams P; P.mode = mk::kRewire; P.remix = 100; P.lock = 1; P.lockMask = mk::kLockKS | mk::kLockPos; P.seed = 9; int ok = 0;
      for (long s = 0; s < 128; ++s) { const int pos = (int) (s % 32); auto p = mk::planStep(&dA, &dA, P, pos, s); ok += p.slice >= 0 && ((dA.cls[(size_t) p.slice] == 0) == (dA.cls[(size_t) pos] == 0)); }
      check(ok == 128, "LOCK POSITIONS: a hit stays a hit and a rest stays a rest at every step (" + juce::String(ok) + "/128)"); }
    // the layers come in with the knob: ratchets from 40, reverses / pitch from 60
    { auto count = [&](float r, int& rat, int& rev, int& pit) { mk::RemixParams P; P.mode = mk::kPreserve; P.remix = r; P.lock = 0; P.lockMask = 0; P.seed = 3; rat = rev = pit = 0;
          for (long s = 0; s < 128; ++s) { auto p = mk::planStep(&dA, &dA, P, (int) (s % 32), s); rat += p.ratchet > 1; rev += p.reverse; pit += std::abs(p.semis) > 0.01f; } };
      int r30, v30, p30, r55, v55, p55, r90, v90, p90; count(30, r30, v30, p30); count(55, r55, v55, p55); count(90, r90, v90, p90);
      check(r30 == 0 && v30 == 0 && p30 == 0, "REMIX 30 (PRESERVE): accents only, no ratchets / reverses / pitch yet");
      check(r55 > 0 && v55 == 0, "REMIX 55: ratchets in (" + juce::String(r55) + "), no reverses yet");
      check(r90 > 0 && (v90 > 0 || p90 > 0), "REMIX 90: ratchets " + juce::String(r90) + ", reverses " + juce::String(v90) + ", pitch moves " + juce::String(p90)); }
    // FREEZE DNA: Haunt's groove played with Static's sounds -> kicks / snares follow the frozen groove
    { mk::RemixParams P; P.freeze = true; int ok = 0, n = 0;
      for (long s = 0; s < 64; ++s) { const int pos = (int) (s % 32); auto p = mk::planStep(&dA, &dB, P, pos, s); if (p.slice < 0) continue; ++n;
          ok += (dB.cls[(size_t) p.slice] & (mk::kK | mk::kS)) == (dA.cls[(size_t) (s % dA.steps)] & (mk::kK | mk::kS)); }
      check(n == 64 && ok == 64, "FREEZE DNA: the live break's slices follow the frozen groove's kick/snare pattern (" + juce::String(ok) + "/" + juce::String(n) + ")"); }
    // RESOLVE: the downbeat after a fill carries a kick
    { mk::StepPlan n; n.slice = 5; n.cls = dA.cls[5]; auto r = mk::resolveStep(&dA, n); check(r.resolve && (dA.cls[(size_t) r.slice] & mk::kK) && r.ratchet == 1 && !r.reverse, "RESOLVE: downbeat after a fill lands on a kick slice"); }
    // fills: 5 styles, every step inside the fill is a real slice; intensity scales the ratchets
    { int bad = 0, ratLow = 0, ratHigh = 0; for (int st = 0; st < 5; ++st) for (int k = 0; k < 16; ++k) { auto lo = mk::fillStep(&dA, st, 0.1f, k, 16, 48 + k, 11, k), hi = mk::fillStep(&dA, st, 1.0f, k, 16, 48 + k, 11, k);
          if (lo.slice < 0 || lo.slice >= 32 || hi.slice < 0 || hi.slice >= 32 || !lo.fill || !hi.fill) ++bad; ratLow += lo.ratchet; ratHigh += hi.ratchet; }
      check(bad == 0 && ratHigh > ratLow, "GENERATE FILL: 5 styles produce valid slices; intensity 1 ratchets more than 0.1 (" + juce::String(ratHigh) + " vs " + juce::String(ratLow) + ")"); }
}

static void testEngineFills(const mk::BreakLibrary& lib) {
    for (int style = 0; style < 5; ++style) {
        Rig r; r.bay(0, byName(lib, "Haunt - Original")); r.F.bpm = 174; r.F.fillEvery = 1; r.F.fillLen16 = 16; r.F.fillStyle = style; r.F.fillInt = 0.8f;
        auto out = r.run(8); const double sl = r.stepLen();
        int fillOut = 0, fillIn = 0, resolveOk = 0, resolveN = 0; const auto d = mk::analyzeBreak(*byName(lib, "Haunt - Original"));
        for (int i = 0; i < r.e.tlogN; ++i) { const auto& t = r.e.tlog[i]; const long step = (long) std::floor(t.at / sl + 0.1); const long bar = step / 16;
            if (t.fill) { if ((bar % 4) == 3) ++fillIn; else ++fillOut; }
            if (step % 64 == 0 && step > 0 && std::abs(t.at - step * sl) < 3) { ++resolveN; resolveOk += t.resolve && (d.cls[(size_t) t.slice] & mk::kK) != 0; } }
        const auto s = stats(out);
        check(fillIn > 8 && fillOut == 0 && !s.nan, "FILL style " + juce::String(style) + " every 4 bars: " + juce::String(fillIn) + " fill triggers, all inside bars 4 and 8 (" + juce::String(fillOut) + " outside), no NaN");
        check(resolveN >= 1 && resolveOk == resolveN, "FILL style " + juce::String(style) + ": the downbeat after each fill RESOLVES on a kick (" + juce::String(resolveOk) + "/" + juce::String(resolveN) + ")");
    }
    // manual fill (MIDI E2) at bar 1 beat 2 -> fills from the next 16th until bar 2, resolves on bar 2
    { Rig r; r.bay(0, byName(lib, "Two Step - Original")); r.F.bpm = 174; r.F.fillLen16 = 16; const double sl = r.stepLen(); const long noteAt = (long) std::llround(sl * (16 + 4) + 37);
      auto out = r.run(4, [&](long pos, juce::MidiBuffer& m) { if (noteAt >= pos && noteAt < pos + r.block) m.addEvent(juce::MidiMessage::noteOn(1, mk::Engine::kFillNote, 1.0f), (int) (noteAt - pos)); });
      long first = -1, last = -1; int outside = 0; bool resolved = false;
      for (int i = 0; i < r.e.tlogN; ++i) { const auto& t = r.e.tlog[i]; if (t.fill) { if (first < 0) first = t.at; last = t.at; const long step = (long) std::floor(t.at / sl + 0.1); if (step < 21 || step >= 32) ++outside; } if (t.resolve && std::abs(t.at - 32 * sl) < 3) resolved = true; }
      check(first >= 0 && std::abs(first - 21 * sl) < 3 && outside == 0 && last < 32 * sl && resolved, "FILL NOW (E2) mid-bar: starts on the next 16th (step 21), stops at the bar line, resolves on bar 2"); (void) out; }
}

static void testInterlock(const mk::BreakLibrary& lib) {
    const auto dMain = mk::analyzeBreak(*byName(lib, "Haunt - Original"));
    auto mainKS = [&](Rig& r) { std::set<long> s; const double sl = r.stepLen(); for (int i = 0; i < r.e.tlogN; ++i) { const auto& t = r.e.tlog[i]; if (t.bay == 0 && (dMain.cls[(size_t) t.slice] & (mk::kK | mk::kS))) s.insert((long) std::floor(t.at / sl + 0.1)); } return s; };
    { Rig r; r.bay(0, byName(lib, "Haunt - Original")); r.bay(1, byName(lib, "Static - Original")); r.B[0].role = mk::kMain; r.B[1].role = mk::kGhostRole; r.F.ilMode = mk::kComplement; r.F.bpm = 174; r.F.ilDuck = 0;
      r.run(4); auto ks = mainKS(r); const double sl = r.stepLen(); int clash = 0, ghostTrig = 0;
      for (int i = 0; i < r.e.tlogN; ++i) { const auto& t = r.e.tlog[i]; if (t.bay == 1) { ++ghostTrig; if (ks.count((long) std::floor(t.at / sl + 0.1))) ++clash; } }
      check(ghostTrig > 10 && clash == 0, "INTERLOCK COMPLEMENT: the GHOST bay never plays on the main bay's kick/snare steps (" + juce::String(ghostTrig) + " triggers, " + juce::String(clash) + " clashes)"); }
    { Rig r; r.bay(0, byName(lib, "Haunt - Original")); r.bay(1, byName(lib, "Static - Original")); r.B[0].role = mk::kMain; r.B[1].role = mk::kGhostRole; r.F.ilMode = mk::kUnison; r.F.bpm = 174; r.F.ilDuck = 0;
      r.run(4); std::set<long long> a0, a1; for (int i = 0; i < r.e.tlogN; ++i) { const auto& t = r.e.tlog[i]; (t.bay == 0 ? a0 : a1).insert(t.at); }
      int shared = 0; for (auto x : a1) shared += a0.count(x) > 0;
      check(a1.size() > 20 && shared == (int) a1.size(), "INTERLOCK UNISON: every GHOST-bay trigger is sample-aligned with a main-bay trigger (" + juce::String(shared) + "/" + juce::String((int) a1.size()) + ")"); }
    // FILLS role: silent outside fills
    { Rig r; r.bay(0, byName(lib, "Haunt - Original")); r.bay(1, byName(lib, "Jungle Roller - Original")); r.B[0].role = mk::kMain; r.B[1].role = mk::kFillsRole; r.F.ilMode = mk::kComplement; r.F.bpm = 174; r.F.fillEvery = 1; r.F.fillLen16 = 8;
      r.run(8); int inFill = 0, outFill = 0; for (int i = 0; i < r.e.tlogN; ++i) { const auto& t = r.e.tlog[i]; if (t.bay == 1) (t.fill || t.resolve ? inFill : outFill)++; }
      check(inFill > 4 && outFill == 0, "INTERLOCK role FILLS: that bay only plays inside fills (" + juce::String(inFill) + " in, " + juce::String(outFill) + " out)"); }
    // bay ducking: the ghost bay dips around the main bay's hits (main bay silent so we only hear the ghost)
    { auto energy = [&](float duck) { Rig r; r.bay(0, byName(lib, "Haunt - Original")); r.bay(1, byName(lib, "Static - Original")); r.B[0].role = mk::kMain; r.B[1].role = mk::kGhostRole; r.F.ilMode = mk::kUnison; r.F.bpm = 174; r.F.ilDuck = duck; r.B[0].level = 0; r.B[1].level = 1; r.F.breaksLevel = 1; r.F.volume = 0.5f;
          auto out = r.run(4); auto ks = mainKS(r); const double sl = r.stepLen(); double e = 0; int w = 0;
          for (long st : ks) { const int t0 = (int) (st * sl), t1 = juce::jmin(out.getNumSamples(), t0 + (int) (0.012 * r.sr)); if (t0 < 2 * r.block) continue; for (int i = t0; i < t1; ++i) e += out.getSample(0, i) * out.getSample(0, i); ++w; } return w ? e / w : 0.0; };
      const double e0 = energy(0), e1 = energy(1);
      check(e0 > 1e-6 && e1 < e0 * 0.35, "INTERLOCK bay ducking: energy in the 12 ms after each main hit drops to " + juce::String(e1 / juce::jmax(1e-12, e0) * 100, 1) + "% with DUCK 1 (needs < 35%)"); }
}

static void testStutter(const mk::BreakLibrary& lib) {
    // MIDI F2 engages at the note's exact sample; ROLL repeats sit on exact multiples of the 1/16
    { Rig r; r.bay(0, byName(lib, "Haunt - Original")); r.F.bpm = 174; r.F.st.mode = mk::kRoll; r.F.stutDiv = 1; r.F.st.div = 1; const long onAt = 20 * r.block + 100, offAt = onAt + (long) (r.sr * 1.1);
      auto out = r.run(6, [&](long pos, juce::MidiBuffer& m) { if (onAt >= pos && onAt < pos + r.block) m.addEvent(juce::MidiMessage::noteOn(1, mk::Engine::kStutterNote, 1.0f), (int) (onAt - pos)); if (offAt >= pos && offAt < pos + r.block) m.addEvent(juce::MidiMessage::noteOff(1, mk::Engine::kStutterNote), (int) (offAt - pos)); });
      const auto& L = r.e.stut.log; const double len0 = r.sr * 60.0 / 174 * 0.25; int exact = 0;
      for (int k = 1; k < L.n; ++k) exact += L.at[k] - L.at[0] == (long long) std::llround(len0 * k);
      check(L.n > 4 && L.at[0] == onAt, "STUTTER via MIDI F2: engages on the note's exact sample (" + juce::String((juce::int64) L.at[0]) + " = " + juce::String((juce::int64) onAt) + ")");
      check(L.n > 4 && exact == L.n - 1, "STUTTER ROLL: every repeat starts on an exact multiple of the 1/16 (" + juce::String(exact) + "/" + juce::String(L.n - 1) + ")");
      const auto tail = stats(out, (int) (offAt + r.sr * 0.3), out.getNumSamples()); check(!r.e.stut.isActive() && tail.rms > 0.01f && !tail.nan, "STUTTER: releases on note-off and the breaks keep playing (tail RMS " + juce::String(tail.rms, 3) + ")"); }
    // the switch engages on the next 16th line
    { Rig r; r.bay(0, byName(lib, "Haunt - Original")); r.F.bpm = 174; r.F.stutDiv = 1; r.F.st.div = 1; const long swAt = 13 * r.block;
      r.run(4, [&](long pos, juce::MidiBuffer&) { r.F.stutter = pos >= swAt && pos < swAt + (long) r.sr; });
      const auto& L = r.e.stut.log; const double sl = r.stepLen(); const double frac = L.n ? std::fmod(L.at[0] / sl, 1.0) : 1;
      check(L.n > 0 && L.at[0] >= swAt && (frac < 0.002 || frac > 0.998), "STUTTER switch: engages on the next 1/16 line (sample " + juce::String((juce::int64) (L.n ? L.at[0] : -1)) + ", grid phase " + juce::String(frac, 4) + ")"); }
    // every mode, extreme settings: finite output, and RAMP repeats get shorter, BREAKDOWN longer
    for (int mode = 0; mode < 4; ++mode) {
        Rig r; r.bay(0, byName(lib, "Haunt - Original")); r.F.bpm = 174; r.F.stutDiv = 1; r.F.st.div = 1; r.F.st.mode = mode; r.F.st.gate = 0.3f; r.F.st.decay = 0.5f; r.F.st.pitch = mode == mk::kBreakdown ? -12.0f : 12.0f; r.F.st.move = 1; r.F.st.env = 1;
        auto out = r.run(4, [&](long pos, juce::MidiBuffer&) { r.F.stutter = pos >= 4 * r.block && pos < 4 * r.block + (long) (r.sr * 1.5); });
        const auto s = stats(out); const auto& L = r.e.stut.log; bool shape = true;
        if (mode == mk::kRamp) for (int k = 2; k < L.n && k < 6; ++k) shape &= (L.at[k] - L.at[k - 1]) <= (L.at[k - 1] - L.at[k - 2]);
        if (mode == mk::kBreakdown) for (int k = 2; k < L.n && k < 6; ++k) shape &= (L.at[k] - L.at[k - 1]) >= (L.at[k - 1] - L.at[k - 2]);
        static const char* mn[] = { "ROLL", "RAMP", "GLITCH", "BREAKDOWN" };
        check(!s.nan && s.peak <= 1.0f && L.n > 3 && shape, juce::String("STUTTER ") + mn[mode] + " extreme settings: finite, peak " + juce::String(s.peak, 3) + ", " + juce::String(L.n) + " repeats" + (mode == 1 ? ", shrinking" : mode == 3 ? ", growing" : ""));
    }
}

static void testFilterRouter() {
    const double sr = 48000; const int n = (int) sr * 2; juce::Random rnd(7);
    std::vector<float> inL((size_t) n), inR((size_t) n); for (int i = 0; i < n; ++i) { inL[(size_t) i] = rnd.nextFloat() * 0.6f - 0.3f; inR[(size_t) i] = inL[(size_t) i]; }
    auto rms = [](const std::vector<float>& v, int from) { double a = 0; for (size_t i = (size_t) from; i < v.size(); ++i) a += v[i] * v[i]; return std::sqrt(a / (double) (v.size() - (size_t) from)); };
    auto run = [&](const mk::FilterParams& P, const std::vector<mk::FilterHit>& hits, mk::FilterRouter& fr, std::vector<float>& L, std::vector<float>& R) {
        fr.prepare(sr); L = inL; R = inR; const int block = 480; const double spb = sr * 60.0 / 174;
        for (int pos = 0; pos < n; pos += block) { std::vector<mk::FilterHit> h; for (auto& x : hits) if (x.at >= pos && x.at < pos + block) h.push_back({ x.at - pos, x.cls }); fr.process(L.data() + pos, R.data() + pos, block, P, h.data(), (int) h.size(), pos / spb, spb, 174, true); } };
    const double inR_ = rms(inL, (int) sr / 2);
    for (int route = 0; route < 3; ++route) {
        mk::FilterParams P; P.route = route; for (auto& m : P.mix) m = 1; P.dry = 0.5f; P.combFb = 0.9f; P.res = 0.9f; P.trigSrc = 3; P.phrBars = 2; P.morph = 0.37f;
        std::vector<mk::FilterHit> hits; for (int i = 0; i < 40; ++i) hits.push_back({ i * 2400 + 17, (uint8_t) (i % 3 == 0 ? mk::kK : i % 3 == 1 ? mk::kS : mk::kH) });
        mk::FilterRouter fr; std::vector<float> L, R; run(P, hits, fr, L, R); bool fin = true; float pk = 0; for (int i = 0; i < n; ++i) { if (!std::isfinite(L[(size_t) i]) || !std::isfinite(R[(size_t) i])) fin = false; pk = juce::jmax(pk, std::abs(L[(size_t) i])); }
        static const char* rn[] = { "PARALLEL", "SERIAL", "MORPH" };
        check(fin && pk < 10.0f && rms(L, (int) sr / 2) > 0.01, juce::String("FILTER ROUTING ") + rn[route] + ", everything on + hit sweeps + phrase sweep: finite, peak " + juce::String(pk, 2) + ", RMS " + juce::String(rms(L, (int) sr / 2), 3));
        check(fr.hitLog.n == 40 && fr.hitLog.at[5] == hits[5].at && fr.hitLog.at[39] == hits[39].at, juce::String("HIT SWEEP (") + rn[route] + "): every hit fires on its own sample (" + juce::String(fr.hitLog.n) + "/40)");
    }
    // AUTO GAIN holds the level
    { mk::FilterParams P; P.route = mk::kParallel; P.dry = 0; P.mix[0] = 1; P.cut[0] = 0.7f; P.autoGain = true; mk::FilterRouter fr; std::vector<float> L, R; run(P, {}, fr, L, R); const double a = rms(L, (int) sr);
      P.autoGain = false; run(P, {}, fr, L, R); const double b = rms(L, (int) sr);
      check(a > inR_ * 0.7 && a < inR_ * 1.4 && b < inR_ * 0.7, "AUTO GAIN: low-pass at 70% keeps the level (" + juce::String(20 * std::log10(a / inR_), 1) + " dB vs " + juce::String(20 * std::log10(b / inR_), 1) + " dB without)"); }
    // HIT SWEEP only fires on the chosen source
    { mk::FilterParams P; P.mix[0] = 1; P.trigSrc = 1; std::vector<mk::FilterHit> hits { { 1000, mk::kK }, { 3000, mk::kS }, { 5000, mk::kH }, { 9000, (uint8_t) (mk::kK | mk::kH) } }; mk::FilterRouter fr; std::vector<float> L, R; run(P, hits, fr, L, R);
      check(fr.hitLog.n == 2 && fr.hitLog.at[0] == 1000 && fr.hitLog.at[1] == 9000, "HIT SWEEP source KICK: only kick hits fire (" + juce::String(fr.hitLog.n) + " of 4)"); }
    // comb tuning
    { mk::FilterRouter fr; fr.prepare(sr); mk::FilterParams P; P.combTune = 1; P.combNote = 69; const double fk = fr.combHz(P, 174); P.combTune = 2; P.combCut = 0.5f; const double ft = fr.combHz(P, 174), ratio = ft / (174.0 / 60.0), l2 = std::log2(ratio);
      check(std::abs(fk - 440) < 0.01 && std::abs(l2 - std::round(l2)) < 1e-6, "COMB tuning: KEY A3 = 440 Hz, TEMPO = beat x 2^" + juce::String((int) std::round(l2)) + " (" + juce::String(ft, 1) + " Hz)"); }
    // MORPH at 0 = the first switched-on filter, at 1 = the last
    { mk::FilterParams M; M.route = mk::kMorphRoute; M.mix[0] = 1; M.mix[1] = 1; M.autoGain = false; M.morph = 0; mk::FilterRouter a, b; std::vector<float> L1, R1, L2, R2; run(M, {}, a, L1, R1);
      mk::FilterParams Pp; Pp.route = mk::kParallel; Pp.dry = 0; Pp.mix[0] = 1; Pp.autoGain = false; run(Pp, {}, b, L2, R2); double d = 0; for (int i = 0; i < n; ++i) d = juce::jmax(d, (double) std::abs(L1[(size_t) i] - L2[(size_t) i]));
      M.morph = 1; run(M, {}, a, L1, R1); Pp.mix[0] = 0; Pp.mix[1] = 1; run(Pp, {}, b, L2, R2); double d2 = 0; for (int i = 0; i < n; ++i) d2 = juce::jmax(d2, (double) std::abs(L1[(size_t) i] - L2[(size_t) i]));
      check(d < 1e-5 && d2 < 1e-5, "FILTER MORPH: 0 = pure low-pass, 1 = pure high-pass (max diff " + juce::String(juce::jmax(d, d2), 8) + ")"); }
}

static void testProcessor() {
    // snapshot deck + morph + state
    MickensProcessor p; waitReady(p); check(p.libReady() && p.numBreaks() >= 100, "processor: library ready, " + juce::String(p.numBreaks()) + " breaks");
    check(!p.legacy.load(), "a fresh session is not a legacy session");
    p.storeSlot(0);
    setP(p, "b1_remix", 50); setP(p, "crunch", 0.9f); setP(p, "b2_mute", 0); setP(p, "fill_style", 3); p.pattern[0][3] = 100; p.pickBreak(1, keyIndex(p, "Stepper|Stepper - Dirty")); setP(p, "b1_freeze", 1); p.syncFreeze(); p.publish();
    p.storeSlot(1);
    p.recallSlot(0);
    check(std::abs(getP(p, "b1_remix")) < 1e-4f && std::abs(getP(p, "crunch") - 0.25f) < 1e-4f && getP(p, "b2_mute") > 0.5f && (int) getP(p, "fill_style") == 0 && p.pattern[0][3] == 0 && p.bayKey[1] == "Static|Static - Original" && !p.frozen[0], "SNAPSHOT DECK: recall slot 1 brings back knobs, drum pattern, breaks and (no) frozen DNA");
    p.recallSlot(1);
    check(std::abs(getP(p, "b1_remix") - 50) < 1e-3f && getP(p, "b2_mute") < 0.5f && p.pattern[0][3] == 100 && p.bayKey[1] == "Stepper|Stepper - Dirty" && p.frozen[0] != nullptr, "SNAPSHOT DECK: recall slot 2 brings back the other setup, frozen DNA included");
    // the whole state survives a save / load, slots byte-identical
    juce::MemoryBlock mb; p.getStateInformation(mb);
    MickensProcessor q; waitReady(q); q.restoreState(mb.getData(), (int) mb.getSize()); waitReady(q);
    bool same = true; for (size_t i = 0; i < p.plist.size(); ++i) if (std::abs(p.praw[i]->load() - q.praw[i]->load()) > 1e-6f) { same = false; std::cout << "   differs: " << p.plist[i]->getParameterID() << "\n"; }
    check(same, "save / load: every parameter identical (" + juce::String((int) p.plist.size()) + " parameters)");
    check(q.bayKey == p.bayKey && q.pattern == p.pattern && q.frozen[0] && q.frozen[0]->toString() == p.frozen[0]->toString() && !q.legacy.load(), "save / load: bays, drum pattern, frozen DNA, not legacy");
    check(q.slotTree(0).toXmlString() == p.slotTree(0).toXmlString() && q.slotTree(1).toXmlString() == p.slotTree(1).toXmlString() && !q.slotTree(2).isValid(), "save / load: snapshot slots byte-identical");
    // morph: continuous values interpolate, stepped ones switch on the bar line
    setP(p, "morph_on", 1); setP(p, "morph_a", 1); setP(p, "morph_b", 2); setP(p, "morph", 0.5f); p.refreshMorph();
    { std::vector<float> v; p.liveValues(v, 0); check(p.morphing(), "MORPH armed (slots 1 -> 2)");
      int ir = -1; for (size_t i = 0; i < p.plist.size(); ++i) if (p.plist[i]->getParameterID() == "b1_remix") ir = (int) i; check(ir >= 0 && std::abs(v[(size_t) ir] - 25.0f) < 1e-3f, "MORPH 0.5: continuous knobs sit half way (remix 0 -> 50 reads " + juce::String(v[(size_t) ir], 2) + ")"); }
    { Head head; head.bpm = 174; setP(p, "morph", 0); const double spb = head.sr * 60.0 / head.bpm; const long flip = (long) (spb * 4 * 1.5);
      auto out = hostRender(p, head, 3, 512, 512, [&](long pos, juce::MidiBuffer&) { if (pos >= flip && getP(p, "morph") < 0.5f) setP(p, "morph", 1.0f); });
      long long firstB2 = -1; for (int i = 0; i < p.engine.tlogN; ++i) if (p.engine.tlog[i].bay == 1) { firstB2 = p.engine.tlog[i].at; break; }
      const double bar2 = spb * 8; check(firstB2 >= 0 && std::abs(firstB2 - bar2) <= 2, "MORPH stepped switch: bay 2 (muted in A, playing in B) starts exactly on the bar line after the knob crossed half way (sample " + juce::String((juce::int64) firstB2) + ", bar line " + juce::String((juce::int64) bar2) + ")");
      const auto s = stats(out); check(!s.nan && s.peak <= 1.0f, "MORPH render: finite, soft-clipped (peak " + juce::String(s.peak, 3) + ")"); }
    setP(p, "morph_on", 0); p.refreshMorph();
    // DNA undo
    { const float before = getP(p, "b1_remix"); p.dnaPush(0); setP(p, "b1_remix", 99); p.dnaDice(0); check(getP(p, "b1_remix") > 98, "DNA: moved"); p.dnaUndo(0); p.dnaUndo(0); check(std::abs(getP(p, "b1_remix") - before) < 1e-3f, "DNA UNDO takes the remix back to " + juce::String(before, 1)); }
    // search + match
    { auto& ix = p.searchIndex(); std::array<float, mk::kNumAxes> want; want.fill(-1.0f); want[mk::kAxTempo] = 1.0f; auto r = ix.search(want, "");
      check((int) r.size() == p.numBreaks() && ix.items[(size_t) r[0]].bpm >= 160 && ix.items[(size_t) r.back()].bpm <= 100, "BREAK SEARCH by feel: TEMPO fast puts the " + juce::String(ix.items[(size_t) r[0]].bpm, 0) + " bpm break first, " + juce::String(ix.items[(size_t) r.back()].bpm, 0) + " bpm last");
      auto j = ix.search(want, "jungle"); bool allFast = !j.empty(); for (int i : j) allFast &= ix.items[(size_t) i].bpm >= 160; check(allFast, "BREAK SEARCH words: 'jungle' only returns 160+ bpm breaks (" + juce::String((int) j.size()) + ")");
      want.fill(-1.0f); want[mk::kAxGhost] = 1.0f; auto gh = ix.search(want, ""); check(ix.items[(size_t) gh[0]].dna->ghostDensity >= ix.items[(size_t) gh.back()].dna->ghostDensity && ix.items[(size_t) gh[0]].tags.contains("ghosty"), "BREAK SEARCH: GHOSTS high puts a 'ghosty' break first (" + ix.items[(size_t) gh[0]].name + ")");
      const int hi = keyIndex(p, "Haunt|Haunt - Original"); auto ref = p.dnaOf(p.breakAt(hi)); auto sim = ix.match(*ref, 0); int fam = 0; for (int k = 0; k < 5; ++k) fam += ix.items[(size_t) sim[(size_t) k].first].family == "Haunt";
      check(ix.items[(size_t) sim[0].first].index == hi && fam >= 3, "BREAK MATCH similar: the break itself first, " + juce::String(fam) + "/5 of the top 5 from the same family");
      auto comp = ix.match(*ref, 1); check(comp[0].second > 0.3f && ix.items[(size_t) comp[0].first].family != "Haunt" && comp[0].second > comp.back().second, "BREAK MATCH complement: best " + ix.items[(size_t) comp[0].first].name + " (" + juce::String(comp[0].second, 2) + "), worst " + juce::String(comp.back().second, 2)); }
    // audition plays once, on its own
    { Head head; head.playing = false; p.setPlaying(false); p.audition(keyIndex(p, "Static|Static - Original")); auto br = p.breakAt(keyIndex(p, "Static|Static - Original")); const int len = (int) (br->data.size() * 48000.0 / br->rate);
      auto out = hostRender(p, head, 8, 512, 512); const auto a = stats(out, 0, 24000), b = stats(out, len + 4800, out.getNumSamples());
      check(a.rms > 0.02f && b.rms < 1e-5f, "AUDITION: the break plays once (RMS " + juce::String(a.rms, 3) + ") then silence (" + juce::String(b.rms, 6) + ")"); }
    // everything on, host blocks bigger than the prepared size, 44.1 kHz
    { MickensProcessor z; waitReady(z); for (int b = 1; b <= 4; ++b) { setP(z, "b" + juce::String(b) + "_mute", 0); setP(z, "b" + juce::String(b) + "_remix", 25.0f * b); setP(z, "b" + juce::String(b) + "_dna_mode", b % 3); setP(z, "b" + juce::String(b) + "_role", b); }
      setP(z, "il_mode", 1); setP(z, "fill_every", 1); setP(z, "fill_style", 4); setP(z, "flt_route", 1); setP(z, "lp_mix", 1); setP(z, "cb_mix", 0.6f); setP(z, "cb_tune", 2); setP(z, "fm_mix", 0.5f); setP(z, "ftr_src", 3); setP(z, "phr_len", 2); setP(z, "st_mode", 2); setP(z, "crunch", 0.8f); setP(z, "reverb", 0.4f); setP(z, "chorus", 0.5f);
      Head head; head.sr = 44100; head.bpm = 160; const long on = 44100 * 2;
      auto out = hostRender(z, head, 8, 1024, 256, [&](long pos, juce::MidiBuffer& m) { if (on >= pos && on < pos + 1024) m.addEvent(juce::MidiMessage::noteOn(1, mk::Engine::kStutterNote, 1.0f), (int) (on - pos)); if (on + 30000 >= pos && on + 30000 < pos + 1024) m.addEvent(juce::MidiMessage::noteOff(1, mk::Engine::kStutterNote), (int) (on + 30000 - pos)); });
      const auto s = stats(out); check(!s.nan && s.peak <= 1.0f && s.rms > 0.02f, "everything on at 44.1 kHz, 1024-sample host blocks over a 256 prepare: finite, peak " + juce::String(s.peak, 3) + ", RMS " + juce::String(s.rms, 3)); }
    // a session from before Mickens 2 is flagged legacy and keeps the classic sound path
    { juce::MemoryBlock mb2; p.getStateInformation(mb2); auto xml = Peek::getXmlFromBinary(mb2.getData(), (int) mb2.getSize()); auto st = juce::ValueTree::fromXml(*xml); auto m = st.getChildWithName("MODEL"); m.removeProperty("mk2", nullptr); m.removeProperty("legacy", nullptr);
      for (int i = st.getNumChildren() - 1; i >= 0; --i) { auto c = st.getChild(i); if (c.hasType("PARAM") && (c["id"].toString().startsWith("flt_") || c["id"].toString() == "st_mode" || c["id"].toString() == "interp")) st.removeChild(i, nullptr); }
      juce::MemoryBlock old; Peek::copyXmlToBinary(*st.createXml(), old); MickensProcessor o; waitReady(o); o.restoreState(old.getData(), (int) old.getSize());
      std::vector<float> v; o.liveValues(v); std::array<mk::BayParams, mk::kBays> B; mk::FxParams F; o.readParams(B, F, v.data());
      check(o.legacy.load() && F.oldFilters && F.oldStutter && F.legacyOut && F.interp == mk::kLoFi && getP(o, "flt_auto") < 0.5f, "legacy session: flagged, classic filters + stutter + output stage + linear reads engaged, auto gain off"); }
}

static void testBackCompat() {
    const juce::File dir(juce::String(std::getenv("MICKENS_OLD_REF") ? std::getenv("MICKENS_OLD_REF") : ""));
    if (!dir.isDirectory()) { std::cout << "SKIP  back-compat (set MICKENS_OLD_REF to the MickensOldRef output dir)\n"; return; }
    for (int c = 0; c < 3; ++c) {
        auto sess = dir.getChildFile("case" + juce::String(c) + ".mickens"), wav = dir.getChildFile("case" + juce::String(c) + ".wav"); if (!sess.existsAsFile() || !wav.existsAsFile()) { check(false, "back-compat case " + juce::String(c) + ": reference files missing"); continue; }
        MickensProcessor p; waitReady(p); check(p.loadSession(sess), "back-compat case " + juce::String(c) + ": old session loads"); waitReady(p);
        auto f = p.bounce(4); double sr = 0; auto a = readWav(f, sr), b = readWav(wav, sr);
        double md = 0; long nd = 0; const int n = juce::jmin(a.getNumSamples(), b.getNumSamples());
        for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < n; ++i) { const double d = std::abs(a.getSample(ch, i) - b.getSample(ch, i)); md = juce::jmax(md, d); nd += d > 1e-4; }
        check(p.legacy.load() && a.getNumSamples() == b.getNumSamples() && n > 0 && md < 1e-4, "back-compat case " + juce::String(c) + ": the 4-bar bounce is identical to the pre-Mickens-2 build (max diff " + juce::String(md, 7) + ", " + juce::String((juce::int64) nd) + " samples over 1e-4, len " + juce::String(a.getNumSamples()) + " vs " + juce::String(b.getNumSamples()) + ")");
        f.deleteFile();
    }
}

// ------------------------------------------------------------------------------------------------ demos
static void demos(const juce::File& dir) {
    dir.createDirectory(); const double sr = 48000;
    auto render = [&](const juce::String& name, double bpm, int bars, const std::function<void(MickensProcessor&)>& setup, const std::function<void(MickensProcessor&, long, int, juce::MidiBuffer&)>& perBlock) {
        MickensProcessor p; waitReady(p); setup(p); pump(80); Head head; head.bpm = bpm; head.sr = sr; const double spb = sr * 60.0 / bpm;
        auto out = hostRender(p, head, bars, 512, 512, [&](long pos, juce::MidiBuffer& m) { perBlock(p, pos, (int) (pos / (spb * 4)), m); });
        // 1.5 s tail
        juce::AudioBuffer<float> all(2, out.getNumSamples() + (int) (sr * 1.5)); all.clear(); for (int c = 0; c < 2; ++c) all.copyFrom(c, 0, out, c, 0, out.getNumSamples());
        { juce::AudioBuffer<float> buf(2, 512); juce::MidiBuffer none; head.playing = false; for (int pos = out.getNumSamples(); pos < all.getNumSamples(); pos += 512) { const int n = juce::jmin(512, all.getNumSamples() - pos); buf.setSize(2, n, false, false, true); buf.clear(); p.processBlock(buf, none); for (int c = 0; c < 2; ++c) all.copyFrom(c, pos, buf, c, 0, n); } }
        const auto s = stats(all); writeWav(all, sr, dir.getChildFile(name + ".wav")); std::cout << "demo " << name << "  peak " << s.peak << "  rms " << s.rms << (s.nan ? "  NAN!" : "") << "\n"; };
    render("01_break_dna_remix_preserve_rewire_mutate", 174, 24, [&](MickensProcessor& p) { setP(p, "b1_lock", 100); setP(p, "reverb", 0.1f); },
        [&](MickensProcessor& p, long pos, int bar, juce::MidiBuffer&) { (void) pos; setP(p, "b1_dna_mode", (float) juce::jmin(2, bar / 8)); setP(p, "b1_remix", (bar % 8) / 7.0f * 100.0f); });
    render("02_groove_lock_freeze_dna", 174, 16, [&](MickensProcessor& p) { setP(p, "b1_remix", 70); setP(p, "b1_dna_mode", 1); },
        [&](MickensProcessor& p, long, int bar, juce::MidiBuffer&) {
            // bars 0-7: Haunt remixed with every lock on; bars 8-11: locks off; bars 12-15: Haunt's groove frozen onto Static
            for (auto* id : { "b1_lk_ks", "b1_lk_pos", "b1_lk_swing", "b1_lk_acc", "b1_lk_ghost" }) setP(p, id, bar < 8 || bar >= 12 ? 1.0f : 0.0f);
            if (bar >= 12 && getP(p, "b1_freeze") < 0.5f) { setP(p, "b1_freeze", 1); p.syncFreeze(); p.pickBreak(0, keyIndex(p, "Static|Static - Original")); setP(p, "b1_remix", 0); } });
    render("03_generate_fill_5_styles", 174, 20, [&](MickensProcessor& p) { setP(p, "fill_every", 1); setP(p, "fill_len", 2); setP(p, "fill_int", 0.75f); p.pickBreak(0, keyIndex(p, "Two Step|Two Step - Original")); },
        [&](MickensProcessor& p, long, int bar, juce::MidiBuffer&) { setP(p, "fill_style", (float) juce::jmin(4, bar / 4)); });
    render("04_interlock_bays_and_ducking", 172, 24, [&](MickensProcessor& p) {
            p.pickBreak(1, keyIndex(p, "Stepper|Stepper - Ghosts")); p.pickBreak(2, keyIndex(p, "Cymbal Run|Cymbal Run - Original")); p.pickBreak(3, keyIndex(p, "Jungle Roller|Jungle Roller - Original"));
            for (int b = 1; b <= 4; ++b) { setP(p, "b" + juce::String(b) + "_mute", 0); setP(p, "b" + juce::String(b) + "_level", b == 1 ? 0.85f : 0.6f); setP(p, "b" + juce::String(b) + "_role", (float) b); }
            setP(p, "il_duck", 0.7f); setP(p, "fill_every", 1); setP(p, "fill_len", 1); },
        [&](MickensProcessor& p, long, int bar, juce::MidiBuffer&) { setP(p, "il_mode", (float) juce::jmin(5, bar / 4)); });
    render("05_performance_stutter_roll_ramp_glitch_breakdown", 174, 16, [&](MickensProcessor& p) { setP(p, "st_gate", 0.85f); setP(p, "st_vdec", 0.25f); setP(p, "st_move", 0.6f); setP(p, "st_div", 1); },
        [&](MickensProcessor& p, long pos, int bar, juce::MidiBuffer& m) { const double spb = sr * 60.0 / 174; const int mode = juce::jmin(3, bar / 4); setP(p, "st_mode", (float) mode); setP(p, "st_pitch", mode == 1 ? 2.0f : mode == 3 ? -3.0f : 0.0f);
            const long on = (long) ((bar * 4 + 2) * spb), off = (long) ((bar * 4 + 3.75) * spb); if (on >= pos && on < pos + 512) m.addEvent(juce::MidiMessage::noteOn(1, mk::Engine::kStutterNote, 1.0f), (int) (on - pos)); if (off >= pos && off < pos + 512) m.addEvent(juce::MidiMessage::noteOff(1, mk::Engine::kStutterNote), (int) (off - pos)); });
    render("06_filter_routing_parallel_serial_morph", 174, 24, [&](MickensProcessor& p) { setP(p, "lp_mix", 1); setP(p, "lp_cut", 0.45f); setP(p, "hp_mix", 0.6f); setP(p, "hp_cut", 0.6f); setP(p, "cb_mix", 0.6f); setP(p, "cb_tune", 2); setP(p, "cb_fb", 0.85f); setP(p, "fm_mix", 0.5f); setP(p, "res", 0.7f); setP(p, "dry", 0.4f); },
        [&](MickensProcessor& p, long pos, int bar, juce::MidiBuffer&) { const int sec = juce::jmin(2, bar / 8); setP(p, "flt_route", (float) sec);
            setP(p, "ftr_src", sec == 0 ? 1.0f : 0.0f); setP(p, "ftr_amt", 2.5f); setP(p, "ftr_dec", 0.15f); setP(p, "ftr_res", 0.4f);
            setP(p, "phr_len", sec == 1 ? 3.0f : 0.0f); setP(p, "phr_shape", 2); setP(p, "phr_amt", 3.5f);
            if (sec == 2) setP(p, "flt_morph", (float) juce::jlimit(0.0, 1.0, (pos / (sr * 60.0 / 174 * 4) - 16.0) / 8.0)); });
    render("07_snapshot_deck_morph", 174, 16, [&](MickensProcessor& p) { setP(p, "lp_mix", 1); setP(p, "lp_cut", 0.35f); setP(p, "dry", 0); p.storeSlot(0);
            setP(p, "b2_mute", 0); setP(p, "b1_remix", 65); setP(p, "b1_dna_mode", 1); setP(p, "crunch", 0.6f); setP(p, "lp_cut", 0.9f); setP(p, "hp_mix", 0.5f); setP(p, "reverb", 0.3f); p.pickBreak(1, keyIndex(p, "Rolling|Rolling - Ghosts")); setP(p, "b2_level", 0.6f); p.defaultPattern(2); p.publish(); p.storeSlot(1);
            setP(p, "morph_on", 1); setP(p, "morph_a", 1); setP(p, "morph_b", 2); setP(p, "morph", 0); p.refreshMorph(); },
        [&](MickensProcessor& p, long pos, int, juce::MidiBuffer&) { setP(p, "morph", (float) juce::jlimit(0.0, 1.0, (pos / (sr * 60.0 / 174 * 4) - 4.0) / 8.0)); });
    render("08_break_match_complement", 174, 16, [&](MickensProcessor& p) { auto& ix = p.searchIndex(); const int hi = keyIndex(p, "Haunt|Haunt - Original"); auto best = ix.match(*p.dnaOf(p.breakAt(hi)), 1)[0];
            std::cout << "   BREAK MATCH complement for Haunt - Original: " << ix.items[(size_t) best.first].name << " (" << best.second << ")\n"; p.pickBreak(1, ix.items[(size_t) best.first].index); setP(p, "b2_level", 0.65f); setP(p, "b2_pan", 0.3f); setP(p, "b1_pan", -0.2f); },
        [&](MickensProcessor& p, long, int bar, juce::MidiBuffer&) { setP(p, "b2_mute", bar < 8 ? 1.0f : 0.0f); });
}


// ------------------------------------------------------------------------------------------------ FINAL GATE punch list
static mk::BreakPtr toneBreak(double hz, double sr = 48000, double bpm = 174, int slices = 32) {
    auto b = std::make_shared<mk::Break>(); b->name = "tone"; b->family = "test"; b->rate = sr; b->bpm = bpm; b->slices = slices;
    const int n = (int) std::round(60.0 / bpm / 4 * sr * slices); b->data.resize((size_t) n);
    for (int i = 0; i < n; ++i) b->data[(size_t) i] = 0.5f * (float) std::sin(2 * juce::MathConstants<double>::pi * hz * i / sr);
    return b;
}
static double dB(double x) { return 20 * std::log10(std::max(1e-12, x)); }
static void testGate(const mk::BreakLibrary& lib) {
    const double sr = 48000; const double spb = sr * 60 / 174;
    // 1. AUTO GAIN: wet only, DRY untouched, bypass with no filter, no bloom on a sub-only LP
    { const int n = (int) sr * 2; juce::Random rnd(3); std::vector<float> L((size_t) n), R((size_t) n); for (int i = 0; i < n; ++i) L[(size_t) i] = R[(size_t) i] = rnd.nextFloat() * 0.6f - 0.3f;
      double ir = 0; for (auto x : L) ir += x * x; ir = std::sqrt(ir / n);
      std::vector<float> l = L, r = R; mk::FilterParams P; P.route = mk::kParallel; P.dry = 0.25f; P.autoGain = true; mk::FilterRouter fr; fr.prepare(sr);
      for (int pos = 0; pos < n; pos += 480) fr.process(l.data() + pos, r.data() + pos, 480, P, nullptr, 0, pos / spb, spb, 174, true);
      double o = 0; for (int i = n / 2; i < n; ++i) o += l[(size_t) i] * l[(size_t) i]; o = std::sqrt(o / (n / 2));
      check(std::abs(dB(o / ir) + 12.04) < 0.3, "AUTO GAIN on, DRY 0.25, no filter: output " + juce::String(dB(o / ir), 2) + " dB (DRY is never corrected)"); }
    { auto br = byName(lib, "Haunt - Original"); const int n = (int) br->data.size(); const double spbH = sr * 60 / 136; float pk[2]; float gmax[2];
      for (int ag = 0; ag < 2; ++ag) { std::vector<float> L(br->data), R(br->data); mk::FilterParams P; P.route = mk::kParallel; P.dry = 0; P.mix[0] = 1; P.cut[0] = 0.15f; P.autoGain = ag; mk::FilterRouter fr; fr.prepare(sr);
          for (int pos = 0; pos + 480 <= n; pos += 480) fr.process(L.data() + pos, R.data() + pos, 480, P, nullptr, 0, pos / spbH, spbH, 136, true);
          pk[ag] = 0; for (int i = n / 4; i < n; ++i) pk[ag] = std::max(pk[ag], std::abs(L[(size_t) i]));
          gmax[ag] = 0; for (int w = n / 4; w + 2400 <= n; w += 2400) { double a = 0, b = 0; for (int i = w; i < w + 2400; ++i) { a += L[(size_t) i] * L[(size_t) i]; b += br->data[(size_t) i] * br->data[(size_t) i]; } if (b > 1e-6) gmax[ag] = std::max(gmax[ag], (float) std::sqrt(a / b)); } }
      check(pk[1] <= 1.3f && dB(gmax[1]) <= 6.5, "AUTO GAIN on a sub-only LP (cut 0.15): peak " + juce::String(pk[1], 2) + " (off: " + juce::String(pk[0], 2) + "), biggest 50 ms out/in " + juce::String(dB(gmax[1]), 1) + " dB (cap +6)"); }
    // 2. switches crossfade: ROUTE, MORPH active set, AUTO GAIN -> no click on a steady tone
    auto tone = [&](std::vector<float>& L, std::vector<float>& R, int n) { L.assign((size_t) n, 0); R.assign((size_t) n, 0); for (int i = 0; i < n; ++i) L[(size_t) i] = R[(size_t) i] = 0.5f * (float) std::sin(2 * juce::MathConstants<double>::pi * 220 * i / sr); };
    auto clickRatio = [&](const std::vector<float>& L, int sw) { auto d2 = [&](int i) { return std::abs(L[(size_t) i + 1] - 2 * L[(size_t) i] + L[(size_t) i - 1]); }; float atSw = 0, els = 0; for (int i = 1; i < (int) L.size() - 1; ++i) { if (std::abs(i - sw) <= 600) atSw = std::max(atSw, d2(i)); else if (i > 4800) els = std::max(els, d2(i)); } return atSw / std::max(1e-9f, els); };
    { const int n = (int) sr, sw = 24000; std::vector<float> L, R; tone(L, R, n); mk::FilterParams P; P.route = mk::kParallel; P.dry = 0.5f; P.mix[0] = 1; P.cut[0] = 0.3f; P.autoGain = false; mk::FilterRouter fr; fr.prepare(sr);
      for (int p = 0; p < n; p += 480) { if (p == sw) P.route = mk::kSerial; fr.process(L.data() + p, R.data() + p, 480, P, nullptr, 0, p / spb, spb, 174, true); }
      const float ratio = clickRatio(L, sw); check(ratio < 1.5f, "ROUTE switch PARALLEL -> SERIAL on a 220 Hz tone: 2nd-difference at the switch " + juce::String(ratio, 2) + "x the steady state (was 17x)"); }
    { const int n = (int) sr, sw = 24000; std::vector<float> L, R; tone(L, R, n); mk::FilterParams P; P.route = mk::kMorphRoute; P.mix[0] = 1; P.mix[1] = 1; P.morph = 0.5f; P.autoGain = false; mk::FilterRouter fr; fr.prepare(sr);
      for (int p = 0; p < n; p += 480) { if (p == sw) P.mix[2] = 0.02f; fr.process(L.data() + p, R.data() + p, 480, P, nullptr, 0, p / spb, spb, 174, true); }
      const float ratio = clickRatio(L, sw); check(ratio < 1.5f, "MORPH route, a MIX nudged 0 -> 0.02: " + juce::String(ratio, 2) + "x (the active set crossfades)"); }
    { const int n = (int) sr, sw = 24000; std::vector<float> L, R; tone(L, R, n); mk::FilterParams P; P.route = mk::kParallel; P.dry = 0.5f; P.mix[0] = 1; P.cut[0] = 0.3f; P.autoGain = false; mk::FilterRouter fr; fr.prepare(sr);
      for (int p = 0; p < n; p += 480) { if (p == sw) P.autoGain = true; fr.process(L.data() + p, R.data() + p, 480, P, nullptr, 0, p / spb, spb, 174, true); }
      const float ratio = clickRatio(L, sw); check(ratio < 1.5f, "AUTO GAIN off -> on: " + juce::String(ratio, 2) + "x (fades in over 10 ms)"); }
    // 3. gain staging: a default session sits around -14 dBFS RMS with the safety clipper idle
    { MickensProcessor p; waitReady(p); Head head; head.bpm = 174; auto out = hostRender(p, head, 5, 512, 512); const int from = (int) (4 * spb);
      long n = 0, clip = 0; double e = 0; float pk = 0; for (int i = from; i < out.getNumSamples(); ++i) { const float y = std::abs(out.getSample(0, i)); ++n; e += y * y; pk = std::max(pk, y); clip += y > 0.98f; }
      const double rms = dB(std::sqrt(e / n)); check(rms > -17 && rms < -10 && clip * 100.0 / n < 0.5 && p.engine.clipCount.load() * 100.0 / (out.getNumSamples() * 2) < 0.5,
          "default session: RMS " + juce::String(rms, 1) + " dBFS, peak " + juce::String(pk, 3) + ", " + juce::String(clip * 100.0 / n, 2) + " % of samples at the clipper (needs < 0.5 %)"); }
    // 4. interpolation: Hermite and sinc reads vs the linear one
    { const double rate = std::exp2(7.0 / 12.0), f0 = 5000 * rate; const int N = 65536; std::vector<float> d((size_t) (N * rate) + 16); for (size_t i = 0; i < d.size(); ++i) d[i] = 0.5f * (float) std::sin(2 * juce::MathConstants<double>::pi * 5000 * i / sr);
      double res[3];
      for (int mode = 0; mode < 3; ++mode) { std::vector<double> y((size_t) N); double pos = 8; for (int i = 0; i < N; ++i) { y[(size_t) i] = mk::readSample(d.data(), (int) d.size(), pos, mode); pos += rate; }
          double c = 0, s = 0, tot = 0; for (int i = 0; i < N; ++i) { const double ph = 2 * juce::MathConstants<double>::pi * f0 * i / sr; c += y[(size_t) i] * std::cos(ph); s += y[(size_t) i] * std::sin(ph); tot += y[(size_t) i] * y[(size_t) i]; }
          double r = 0; for (int i = 0; i < N; ++i) { const double ph = 2 * juce::MathConstants<double>::pi * f0 * i / sr; const double e = y[(size_t) i] - (c / (N / 2.0)) * std::cos(ph) - (s / (N / 2.0)) * std::sin(ph); r += e * e; } res[mode] = dB(std::sqrt(r / tot)); }
      check(res[1] < -45 && res[2] < -60 && res[1] < res[0] - 10, "sample reads, 5 kHz pitched +7 st, residual below the tone: LO-FI " + juce::String(res[0], 1) + " dB, CLEAN " + juce::String(res[1], 1) + " dB, HQ " + juce::String(res[2], 1) + " dB"); }
    // 5. no per-block gain steps: a LEVEL ramp on a 55 Hz tone
    { Rig r; r.bay(0, toneBreak(55)); r.F.bpm = 174; r.F.legacyOut = false; const int total = (int) (8 * spb);
      auto o = r.run(2, [&](long pos, juce::MidiBuffer&) { r.B[0].level = 1.0f - 0.8f * (float) pos / total; });
      float atB = 0, inB = 0; for (int i = 2 * r.block; i < o.getNumSamples() - 1; ++i) { const float dd = std::abs(o.getSample(0, i) - o.getSample(0, i - 1)); if (i % r.block == 0) atB = std::max(atB, dd); else inB = std::max(inB, dd); }
      check(atB <= inB * 1.05f, "LEVEL ramp on a 55 Hz tone: jump at block boundaries " + juce::String(atB, 5) + " vs inside " + juce::String(inB, 5) + " (gains glide per sample)"); }
    // 6. bay MUTE fades, stutter releases without a step
    { Rig r; r.bay(0, toneBreak(300)); r.F.bpm = 174; const long muteAt = 10 * r.block; auto o = r.run(2, [&](long pos, juce::MidiBuffer&) { r.B[0].mute = pos >= muteAt; });
      float mx = 0; for (int i = (int) muteAt - 1; i < (int) muteAt + 100; ++i) mx = std::max(mx, std::abs(o.getSample(0, i + 1) - o.getSample(0, i))); float typ = 0; for (int i = 1000; i < 4000; ++i) typ = std::max(typ, std::abs(o.getSample(0, i + 1) - o.getSample(0, i)));
      check(mx <= typ * 1.1f && std::abs(o.getSample(0, (int) muteAt + 200)) < 1e-4f, "bay MUTE mid-slice: biggest step around the mute " + juce::String(mx, 4) + " vs the tone's own " + juce::String(typ, 4) + ", silent 200 samples later"); }
    { Rig r; r.bay(0, byName(lib, "Haunt - Original")); r.F.bpm = 174; r.F.st.div = 1; r.F.stutDiv = 1; const long onAt = 20 * r.block + 100, offAt = onAt + 7000;
      auto out = r.run(4, [&](long pos, juce::MidiBuffer& m) { if (onAt >= pos && onAt < pos + r.block) m.addEvent(juce::MidiMessage::noteOn(1, mk::Engine::kStutterNote, 1.0f), (int) (onAt - pos)); if (offAt >= pos && offAt < pos + r.block) m.addEvent(juce::MidiMessage::noteOff(1, mk::Engine::kStutterNote), (int) (offAt - pos)); });
      float j = 0; for (int i = (int) offAt - 2; i < (int) offAt + 300; ++i) j = std::max(j, std::abs(out.getSample(0, i + 1) - out.getSample(0, i))); float typ = 0; for (int i = (int) offAt + 2000; i < (int) offAt + 20000; ++i) typ = std::max(typ, std::abs(out.getSample(0, i + 1) - out.getSample(0, i)));
      check(j <= typ, "STUTTER release mid-repeat: biggest step in the 300 samples after note-off " + juce::String(j, 3) + " vs the break's own " + juce::String(typ, 3)); }
    // BREAKDOWN ends by itself and never grows past 2 beats
    { Rig r; r.bay(0, byName(lib, "Haunt - Original")); r.F.bpm = 174; r.F.st.mode = mk::kBreakdown; r.F.st.div = 1; r.F.stutDiv = 1; r.F.st.decay = 0.5f;
      r.run(16, [&](long pos, juce::MidiBuffer&) { r.F.stutter = pos >= 4 * r.block; }); const auto& L = r.e.stut.log; long long maxGap = 0; for (int k = 1; k < L.n; ++k) maxGap = std::max(maxGap, L.at[k] - L.at[k - 1]);
      check(!r.e.stut.isActive() && maxGap <= (long long) (2 * spb) + 2, "STUTTER BREAKDOWN held for 16 bars: stopped by itself after " + juce::String(L.n) + " repeats, longest repeat " + juce::String((double) maxGap / spb, 2) + " beats (cap 2)"); }
    // 7. transport jumps back: pending fills and per-bar state reset
    { MickensProcessor q; waitReady(q); Head z; z.bpm = 174; q.setRateAndBufferSizeDetails(48000, 512); q.prepareToPlay(48000, 512); q.setPlayHead(&z); juce::AudioBuffer<float> buf(2, 512); juce::MidiBuffer midi;
      const int loopLen = (int) (8 * spb); int fired = 0;
      for (int it = 0; it < 4; ++it) for (int pos = 0; pos < loopLen; pos += 512) { z.ppq = pos / spb; midi.clear(); if (it == 1 && pos >= loopLen - 2048 && pos < loopLen - 1536) midi.addEvent(juce::MidiMessage::noteOn(1, mk::Engine::kFillNote, 1.0f), 0); buf.clear(); q.processBlock(buf, midi); }
      for (int i = 0; i < q.engine.tlogN; ++i) fired += q.engine.tlog[i].fill;
      check(q.engine.manualFillStart() < 0, "transport loop: a fill armed before the loop point is forgotten at the jump (window " + juce::String((int) q.engine.manualFillStart()) + ", " + juce::String(fired) + " fill triggers)"); }
    // 8. a host that reports bpm 0 / NaN
    { MickensProcessor q; waitReady(q); Head z; z.bpm = 0; q.setRateAndBufferSizeDetails(48000, 512); q.prepareToPlay(48000, 512); q.setPlayHead(&z); juce::AudioBuffer<float> buf(2, 512); juce::MidiBuffer midi; bool nan = false;
      for (int k = 0; k < 20; ++k) { buf.clear(); z.ppq = k * 0.1; q.processBlock(buf, midi); for (int i = 0; i < 512; ++i) if (!std::isfinite(buf.getSample(0, i))) nan = true; }
      auto f = q.bounce(1); check(!nan && q.currentBpm() > 20 && f.existsAsFile() && f.getSize() > 1000, "host bpm 0: tempo stays " + juce::String(q.currentBpm(), 0) + ", no NaN, bounce renders"); f.deleteFile(); }
    // 9. odd user files
    { auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("MickensTests"); dir.createDirectory();
      auto write = [&](const juce::String& nm, double secs, double fsr) { juce::AudioBuffer<float> b(1, (int) (secs * fsr)); for (int i = 0; i < b.getNumSamples(); ++i) b.setSample(0, i, 0.3f * (float) std::sin(i * 0.05)); auto f = dir.getChildFile(nm); writeWav(b, fsr, f); return f; };
      juce::String err; auto a = mk::BreakLibrary::fromFile(write("long.wav", 40.0, 48000), 174, err); auto b = mk::BreakLibrary::fromFile(write("tiny.wav", 0.25, 48000), 174, err); auto c = mk::BreakLibrary::fromFile(write("odd.wav", 3.0, 96000), 174, err);
      check(a && a->data.size() <= 48000 * 20 + 1 && (a->tempoFound ? (a->bpm >= 60 && a->bpm <= 200) : true), "user file 40 s: capped to 20 s, tempo " + juce::String(a ? a->bpm : 0, 1) + (a && !a->tempoFound ? " (none found: FIT TEMPO off)" : ""));
      check(!b && err.isNotEmpty(), "user file 0.25 s: rejected (" + err + ")");
      check(c && c->bpm >= 60 && c->bpm <= 200, "user file 3 s @ 96 kHz: " + juce::String(c ? c->bpm : 0, 1) + " bpm, " + juce::String(c ? c->slices : 0) + " slices"); }
    // 10. STEP LOCKS, PRINT, MIDI export, presets, snapshot GC, editor pass
    { MickensProcessor p; waitReady(p); setP(p, "b1_remix", 100); setP(p, "b1_dna_mode", 2); setP(p, "b1_lock", 0); for (auto* id : { "b1_lk_ks", "b1_lk_pos", "b1_lk_swing", "b1_lk_acc", "b1_lk_ghost" }) setP(p, id, 0);
      auto before = p.previewPlan(0, 5); p.lockStep(0, 5); setP(p, "b1_dna_seed", 4242); auto after = p.previewPlan(0, 5); auto other = p.previewPlan(0, 6);
      check(p.stepLocked(0, 5) && after.slice == before.slice && after.ratchet == before.ratchet && after.reverse == before.reverse, "STEP LOCK: step 6 keeps its plan after a new seed (slice " + juce::String(after.slice) + ")"); (void) other;
      juce::MemoryBlock mb; p.getStateInformation(mb); MickensProcessor q; waitReady(q); q.restoreState(mb.getData(), (int) mb.getSize()); waitReady(q);
      check(q.stepLocked(0, 5) && !q.stepLocked(0, 4) && q.previewPlan(0, 5).slice == after.slice, "STEP LOCK survives save / load");
      p.printLocks(0); int cnt = 0; for (int s = 0; s < 32; ++s) cnt += p.stepLocked(0, s); check(cnt >= 28, "PRINT bakes the 2-bar plan into " + juce::String(cnt) + " locks");
      p.unlockStep(0, 5); check(!p.stepLocked(0, 5), "a step unlocks"); p.clearLocks(0); cnt = 0; for (int s = 0; s < 32; ++s) cnt += p.stepLocked(0, s); check(cnt == 0, "CLEAR frees every lock");
      auto mid = p.exportMidi(0, 4); juce::MidiFile mf; juce::FileInputStream is(mid); bool ok = is.openedOk() && mf.readFrom(is); int notes = 0; if (ok && mf.getNumTracks() > 0) for (int i = 0; i < mf.getTrack(0)->getNumEvents(); ++i) notes += mf.getTrack(0)->getEventPointer(i)->message.isNoteOn();
      check(ok && notes >= 48, "DRAG MIDI OUT: 4 bars of bay 1 -> " + juce::String(notes) + " notes in " + mid.getFileName()); mid.deleteFile();
      // presets
      const auto& P = mk::factoryPresets(); int bad = 0; juce::StringArray badNames;
      for (int i = 0; i < (int) P.size(); ++i) { p.loadFactoryPreset(i); bool good = p.currentPreset == i;
          for (auto& k : juce::StringArray::fromTokens(P[(size_t) i].bays, ";", "")) if (k != "-" && p.keyIndex(k) < 0) good = false;
          for (auto& kv : juce::StringArray::fromTokens(P[(size_t) i].params, " ", "")) if (!p.apvts.getParameter(kv.upToFirstOccurrenceOf("=", false, false))) good = false;
          if (!good) { ++bad; badNames.add(P[(size_t) i].name); } }
      check(bad == 0 && P.size() >= 40, juce::String((int) P.size()) + " factory presets: every break key and parameter resolves" + (bad ? " (bad: " + badNames.joinIntoString(", ") + ")" : ""));
      { p.loadFactoryPreset(15); Head head; head.bpm = 150; auto out = hostRender(p, head, 4, 512, 512); const auto s = stats(out); check(!s.nan && s.peak <= 1.0f && s.rms > 0.02f, "preset '" + juce::String(P[15].name) + "' renders: peak " + juce::String(s.peak, 3) + ", RMS " + juce::String(s.rms, 3)); }
      // snapshot garbage collection: hundreds of publishes while the audio thread runs stay bounded
      { Head head; head.bpm = 174; p.setRateAndBufferSizeDetails(48000, 512); p.prepareToPlay(48000, 512); p.setPlayHead(&head); juce::AudioBuffer<float> buf(2, 512); juce::MidiBuffer midi;
        for (int k = 0; k < 400; ++k) { p.pattern[0][(size_t) (k % 16)] = (uint8_t) (k & 1 ? 100 : 0); p.publish(); head.ppq = k * 512 / spb; p.processBlock(buf, midi); if (k % 10 == 0) pump(1); }
        pump(80); check(p.snapshotCount() < 12, "snapshot GC: after 400 publishes under playback " + juce::String((int) p.snapshotCount()) + " snapshots are alive"); }
      // the editor (so ASan sees it): open, let it tick, destroy
      { std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor()); ed->setVisible(true); pump(300); p.lockStep(1, 3); p.storeSlot(0); pump(150); ed.reset(); check(true, "editor: created, ticked, destroyed"); }
    }
}

static void testBypass(const mk::BreakLibrary&) {
    MickensProcessor p; waitReady(p); const double sr = 48000, spb = sr * 60 / 174;
    check(p.getBypassParameter() != nullptr && p.getBypassParameter()->isAutomatable() && p.plist[170]->getParameterID() == "bypass" && p.plist.size() >= 171, "BYPASS: 171st parameter (after every sound parameter; only PLAY mode follows), automatable, getBypassParameter() set");
    setP(p, "reverb", 0.5f); setP(p, "b2_mute", 0); setP(p, "stutter", 0);
    // engage mid-play: silent within 20 ms, no click in the fade; release while stopped: no leaking tails; un-bypass in time
    Head head; head.bpm = 174; const long on = (long) (spb * 6), off = (long) (spb * 14);
    auto out = hostRender(p, head, 6, 512, 512, [&](long pos, juce::MidiBuffer& m) { if (pos >= on && pos < on + 512) setP(p, "bypass", 1); if (pos >= off && pos < off + 512) setP(p, "bypass", 0); if (pos >= on && pos < off && pos % 4096 == 0) m.addEvent(juce::MidiMessage::noteOn(1, 36, 1.0f), 0); });
    const auto before = stats(out, (int) (on - 4800), (int) on); const auto after = stats(out, (int) (on + 512 + sr * 0.02), (int) off); const auto back = stats(out, (int) (off + 512 + sr * 0.3), out.getNumSamples());
    float jump = 0; for (int i = (int) on; i < (int) on + 512 + 400; ++i) jump = std::max(jump, std::abs(out.getSample(0, i + 1) - out.getSample(0, i))); float typ = 0; for (int i = (int) on - 20000; i < (int) on; ++i) typ = std::max(typ, std::abs(out.getSample(0, i + 1) - out.getSample(0, i)));
    check(before.rms > 0.02f && after.peak == 0.0f, "BYPASS engaged mid-play: silence (-inf dBFS, exact 0) from 20 ms after the switch (RMS before " + juce::String(before.rms, 3) + "), MIDI ignored meanwhile");
    check(jump <= typ, "BYPASS fade: biggest step during the 6 ms fade " + juce::String(jump, 4) + " vs the music's own " + juce::String(typ, 4) + " (no click)");
    check(back.rms > 0.02f, "BYPASS released: the plugin plays again (RMS " + juce::String(back.rms, 3) + ")");
    long long firstAfter = -1; for (int i = 0; i < p.engine.tlogN; ++i) if (p.engine.tlog[i].at >= off) { firstAfter = p.engine.tlog[i].at; break; }
    const double stepLen = spb / 4; const double phase = firstAfter >= 0 ? std::fmod(firstAfter / stepLen, 1.0) : 1; check(firstAfter >= 0 && (phase < 0.01 || phase > 0.99), "BYPASS released: the first trigger sits on the 16th grid again (phase " + juce::String(phase, 4) + ")");
    { Head h2; h2.playing = false; p.setPlaying(true); setP(p, "bypass", 0); auto o = hostRender(p, h2, 2, 512, 512, [&](long pos, juce::MidiBuffer&) { if (pos == 20 * 512) setP(p, "bypass", 1); if (pos == 20 * 512 + 48000) { p.setPlaying(false); } if (pos == 20 * 512 + 48000 + 4096) setP(p, "bypass", 0); });
      const auto tail = stats(o, 20 * 512 + 48000 + 4096 + 512, 20 * 512 + 48000 + 4096 + 512 + 9600); p.setPlaying(false);
      check(tail.peak < 1e-6f, "BYPASS released after a stop: no reverb / voice tail leaks back (peak " + juce::String(tail.peak, 7) + ")"); }
    // never saved, loading never touches it; snapshots / presets / dice / undo leave it alone
    setP(p, "bypass", 1); juce::MemoryBlock mb; p.getStateInformation(mb);
    { MickensProcessor q; waitReady(q); q.restoreState(mb.getData(), (int) mb.getSize()); check(q.bypassParam->get() == false, "a saved session does not carry BYPASS: a fresh instance loads it off"); }
    p.restoreState(mb.getData(), (int) mb.getSize()); check(p.bypassParam->get() == true, "loading a session never changes BYPASS (stayed on)");
    p.storeSlot(2); setP(p, "bypass", 0); p.recallSlot(2); const bool a = p.bypassParam->get(); p.loadFactoryPreset(3); const bool b = p.bypassParam->get(); p.diceStarts(); p.dnaDice(0); p.dnaUndo(0); const bool c = p.bypassParam->get();
    check(!a && !b && !c, "snapshot recall, preset, DICE and undo leave BYPASS alone");
    { setP(p, "morph_on", 0); std::vector<float> v; p.liveValues(v); check(p.pkind.back() == 2, "BYPASS is outside the morph / snapshot parameter set"); }
}
static void testPlayMode(const mk::BreakLibrary&) {
    MickensProcessor p; waitReady(p); const double sr = 48000, spb = sr * 60 / 174, stepLen = spb / 4;
    check(p.plist.back()->getParameterID() == "play_mode" && p.pkind.back() == 2 && p.plist.size() == 172, "PLAY mode: last parameter (172nd), outside presets / slots / morph");
    setP(p, "reverb", 0.4f); setP(p, "b2_mute", 0);
    // ALWAYS = today's sound, bit for bit: gate notes change nothing in ALWAYS, and ON NOTES with a note held from the first sample equals ALWAYS
    { auto gateNote = [&](long pos, juce::MidiBuffer& m) { if (pos == 0) m.addEvent(juce::MidiMessage::noteOn(1, 24, 1.0f), 0); };
      Head h0; setP(p, "play_mode", 0); hostRender(p, h0, 4, 512, 512);   // warm-up: the renders below all start from the same engine state
      Head h1; h1.bpm = 174; auto ref = hostRender(p, h1, 4, 512, 512);
      Head h2; auto withNotes = hostRender(p, h2, 4, 512, 512, gateNote);
      Head h3; setP(p, "play_mode", 1); auto held = hostRender(p, h3, 4, 512, 512, gateNote);
      float d1 = 0, d2 = 0; for (int c = 0; c < 2; ++c) for (int i = 0; i < ref.getNumSamples(); ++i) { d1 = std::max(d1, std::abs(ref.getSample(c, i) - withNotes.getSample(c, i))); d2 = std::max(d2, std::abs(ref.getSample(c, i) - held.getSample(c, i))); }
      check(stats(ref).rms > 0.02f && d1 == 0.0f && d2 == 0.0f, "PLAY: ALWAYS is bit-identical with or without gate notes (max diff " + juce::String(d1, 7) + "); ON NOTES with a note held from the start is bit-identical to ALWAYS (max diff " + juce::String(d2, 7) + ")"); }
    { Head head; head.bpm = 174; auto out = hostRender(p, head, 2, 512, 512); check(stats(out).peak == 0.0f, "ON NOTES, no note: exact silence over 2 bars"); }
    // one low note (C0 = 24: no other job) held from an off-grid sample at bar 1 beat 2 to bar 3 beat 1
    { Head head; head.bpm = 174; const long on = (long) (spb * 5) + 137, off = (long) (spb * 12) + 301;
      auto out = hostRender(p, head, 5, 512, 512, [&](long pos, juce::MidiBuffer& m) { if (on >= pos && on < pos + 512) m.addEvent(juce::MidiMessage::noteOn(1, 24, 1.0f), (int) (on - pos)); if (off >= pos && off < pos + 512) m.addEvent(juce::MidiMessage::noteOff(1, 24), (int) (off - pos)); });
      const auto pre = stats(out, 0, (int) on), play = stats(out, (int) on + 2400, (int) off), post = stats(out, (int) (off + sr * 0.02), out.getNumSamples());
      long long first = -1; for (int i = 0; i < p.engine.tlogN; ++i) if (p.engine.tlog[i].at >= on) { first = p.engine.tlog[i].at; break; }
      const double phase = first >= 0 ? std::fmod(first / stepLen, 1.0) : 0.5;
      check(pre.peak == 0.0f && play.rms > 0.02f, "ON NOTES, C0 held: silent before the note (peak " + juce::String(pre.peak, 6) + "), playing while held (RMS " + juce::String(play.rms, 3) + ")");
      check(first >= 0 && (phase < 0.01 || phase > 0.99) && first >= on, "ON NOTES: the first hit lands on the 16th grid, not at the note's phase (grid phase " + juce::String(phase, 4) + ", " + juce::String((int) (first - on)) + " samples after the note)");
      float jump = 0; for (int i = (int) off; i < (int) off + 500; ++i) jump = std::max(jump, std::abs(out.getSample(0, i + 1) - out.getSample(0, i))); float typ = 0; for (int i = (int) on + 4800; i < (int) off - 100; ++i) typ = std::max(typ, std::abs(out.getSample(0, i + 1) - out.getSample(0, i)));
      check(post.peak == 0.0f && jump <= typ, "ON NOTES, note-off: 6 ms fade (biggest step " + juce::String(jump, 4) + " vs the music's " + juce::String(typ, 4) + ") then exact silence"); }
    // overlapping notes keep it running; the last note-off stops it
    { Head head; head.bpm = 174; const long a0 = 4000, b0 = 20000, a1 = 40000, b1 = 70000;
      auto out = hostRender(p, head, 3, 512, 512, [&](long pos, juce::MidiBuffer& m) { auto ev = [&](long at, juce::MidiMessage msg) { if (at >= pos && at < pos + 512) m.addEvent(msg, (int) (at - pos)); };
          ev(a0, juce::MidiMessage::noteOn(1, 24, 1.0f)); ev(b0, juce::MidiMessage::noteOn(1, 26, 1.0f)); ev(a1, juce::MidiMessage::noteOff(1, 24)); ev(b1, juce::MidiMessage::noteOff(1, 26)); });
      const auto mid = stats(out, (int) a1 + 2400, (int) b1), after = stats(out, (int) (b1 + sr * 0.02), out.getNumSamples());
      check(mid.rms > 0.02f && after.peak == 0.0f, "ON NOTES, overlapping notes: still playing after the first note-off (RMS " + juce::String(mid.rms, 3) + "), silent after the last"); }
    // transport stop with a note held: the held note is forgotten (no stuck gate); the next note after restart plays again
    { Head head; head.bpm = 174; p.setRateAndBufferSizeDetails(48000, 512); p.prepareToPlay(48000, 512); p.setPlayHead(&head); juce::AudioBuffer<float> buf(2, 512); juce::MidiBuffer midi; float rmsStopped = 0, rmsRestart = 0, rmsNew = 0;
      auto run = [&](int blocks, bool playing, int noteOnAt, float& acc) { head.playing = playing; double e = 0; long cnt = 0; for (int k = 0; k < blocks; ++k) { midi.clear(); if (k == noteOnAt) midi.addEvent(juce::MidiMessage::noteOn(1, 24, 1.0f), 0); head.ppq += 512 / spb; buf.clear(); p.processBlock(buf, midi); if (k > 10) for (int i = 0; i < 512; ++i) { e += buf.getSample(0, i) * buf.getSample(0, i); ++cnt; } } acc = (float) std::sqrt(e / juce::jmax(1L, cnt)); };
      float dummy = 0; run(40, true, 0, dummy); run(20, false, -1, rmsStopped); run(40, true, -1, rmsRestart); run(40, true, 0, rmsNew);
      check(rmsStopped < 1e-6f && rmsRestart < 1e-6f && rmsNew > 0.02f, "ON NOTES, host stop with a note held: silent while stopped, still silent after restart (note forgotten), plays again on a new note (RMS " + juce::String(rmsNew, 3) + ")"); }
    // FL channel mute = the notes stop arriving: bars 1-2 one note per bar, bars 3-4 no notes (muted), bars 5-6 notes again
    { Head head; head.bpm = 174; const long bar = (long) (spb * 4);
      auto out = hostRender(p, head, 6, 512, 512, [&](long pos, juce::MidiBuffer& m) { for (int b : { 0, 1, 4, 5 }) { const long a = b * bar, e = a + bar - 1; if (a >= pos && a < pos + 512) m.addEvent(juce::MidiMessage::noteOn(1, 24, 1.0f), (int) (a - pos)); if (e >= pos && e < pos + 512) m.addEvent(juce::MidiMessage::noteOff(1, 24), (int) (e - pos)); } });
      const auto on1 = stats(out, 0, (int) (2 * bar)), muted = stats(out, (int) (2 * bar + sr * 0.02), (int) (4 * bar)), on2 = stats(out, (int) (4 * bar), out.getNumSamples());
      long long first = -1; for (int i = 0; i < p.engine.tlogN; ++i) if (p.engine.tlog[i].at >= 4 * bar) { first = p.engine.tlog[i].at; break; }
      check(on1.rms > 0.02f && muted.peak == 0.0f && on2.rms > 0.02f && std::abs((double) first - 16 * spb) <= 2.0, "ON NOTES, channel mute simulated (notes removed for 2 bars): plays (RMS " + juce::String(on1.rms, 3) + "), exact silence while muted, plays again from the next note with its first hit on the bar line (" + juce::String((double) first - 16 * spb, 1) + " samples off)"); }
    // the same key retriggered while held, a note-off for a key never held, all-notes-off: never stuck
    { Head head; head.bpm = 174; const long a = 2000, r = 30000, x = 40000, off = 60000, b = 90000, ano = 120000;
      auto out = hostRender(p, head, 3, 512, 512, [&](long pos, juce::MidiBuffer& m) { auto ev = [&](long at, juce::MidiMessage msg) { if (at >= pos && at < pos + 512) m.addEvent(msg, (int) (at - pos)); };
          ev(a, juce::MidiMessage::noteOn(1, 24, 1.0f)); ev(r, juce::MidiMessage::noteOn(1, 24, 1.0f)); ev(x, juce::MidiMessage::noteOff(1, 30)); ev(off, juce::MidiMessage::noteOff(1, 24));
          ev(b, juce::MidiMessage::noteOn(1, 25, 1.0f)); ev(b + 10, juce::MidiMessage::noteOn(1, 27, 1.0f)); ev(ano, juce::MidiMessage::allNotesOff(1)); });
      const auto held = stats(out, (int) r, (int) off), gap = stats(out, (int) (off + sr * 0.02), (int) b), held2 = stats(out, (int) b + 2400, (int) ano), end = stats(out, (int) (ano + sr * 0.02), out.getNumSamples());
      check(held.rms > 0.02f && gap.peak == 0.0f && held2.rms > 0.02f && end.peak == 0.0f, "ON NOTES, retrigger / stray note-off / all-notes-off: plays through the retrigger, stops at the note-off, two keys then ALL NOTES OFF -> exact silence (no stuck gate)"); }
    // BYPASS wins: a note held when BYPASS goes on, its note-off swallowed while bypassed -> silent after un-bypass, the next note plays
    { Head head; head.bpm = 174; const long n0 = 1000, by = 30000, nf = 50000, ub = 70000, n1 = 110000;
      auto out = hostRender(p, head, 4, 512, 512, [&](long pos, juce::MidiBuffer& m) { auto in = [&](long at) { return at >= pos && at < pos + 512; };
          if (in(n0)) m.addEvent(juce::MidiMessage::noteOn(1, 24, 1.0f), (int) (n0 - pos)); if (in(by)) setP(p, "bypass", 1); if (in(nf)) m.addEvent(juce::MidiMessage::noteOff(1, 24), (int) (nf - pos));
          if (in(ub)) setP(p, "bypass", 0); if (in(n1)) m.addEvent(juce::MidiMessage::noteOn(1, 24, 1.0f), (int) (n1 - pos)); });
      const auto before = stats(out, (int) n0 + 2400, (int) by), byp = stats(out, (int) (by + 512 + sr * 0.02), (int) ub), after = stats(out, (int) ub, (int) n1), again = stats(out, (int) n1 + 2400, out.getNumSamples());
      setP(p, "bypass", 0);
      check(before.rms > 0.02f && byp.peak == 0.0f && after.peak == 0.0f && again.rms > 0.02f, "ON NOTES + BYPASS: bypass silences a held note, its swallowed note-off can't leave the gate stuck (exact silence after un-bypass), the next note plays (RMS " + juce::String(again.rms, 3) + ")"); }
    // on-screen pads pressed while no note is held are dropped, never fired late when the next note opens the gate
    { Head head; head.bpm = 174; setP(p, "kick_lvl", 1.0f); const long n1 = 96000;
      auto out = hostRender(p, head, 3, 512, 512, [&](long pos, juce::MidiBuffer& m) { if (pos == 20 * 512) for (int k = 0; k < 4; ++k) p.engine.pushPad(0, 0, 1.0f); if (n1 >= pos && n1 < pos + 512) m.addEvent(juce::MidiMessage::noteOn(1, 24, 1.0f), (int) (n1 - pos)); });
      long long first = -1; for (int i = 0; i < p.engine.tlogN; ++i) if (p.engine.tlog[i].at >= n1) { first = p.engine.tlog[i].at; break; }
      const double ph = first >= 0 ? std::fmod(first / stepLen, 1.0) : 0.5;
      const int grid = (int) (std::ceil(n1 / stepLen) * stepLen);
      check(stats(out, 0, grid).peak == 0.0f && first >= n1 && (ph < 0.01 || ph > 0.99), "ON NOTES: pads pressed with no note held stay silent and are not fired late (silent up to the first 16th after the note, first event phase " + juce::String(ph, 4) + ")"); }
    // state: saved per session, never in presets / slots; old sessions load ALWAYS
    { juce::MemoryBlock mb; p.getStateInformation(mb); MickensProcessor q; waitReady(q); q.restoreState(mb.getData(), (int) mb.getSize()); check(getP(q, "play_mode") > 0.5f, "PLAY mode survives a save / load (ON NOTES)");
      auto xml = Peek::getXmlFromBinary(mb.getData(), (int) mb.getSize()); auto st = juce::ValueTree::fromXml(*xml); for (int i = st.getNumChildren() - 1; i >= 0; --i) if (st.getChild(i)["id"].toString() == "play_mode") st.removeChild(i, nullptr);
      juce::MemoryBlock old; Peek::copyXmlToBinary(*st.createXml(), old); MickensProcessor o; waitReady(o); setP(o, "play_mode", 1); o.restoreState(old.getData(), (int) old.getSize()); check(getP(o, "play_mode") < 0.5f, "a session from before PLAY mode loads ALWAYS (even into an instance set to ON NOTES)");
      { const auto f = juce::File::createTempFile(".mickens"); f.replaceWithData(mb.getData(), mb.getSize()); MickensProcessor r; waitReady(r); const bool ok = r.loadSession(f); const bool keptAlways = getP(r, "play_mode") < 0.5f;
        setP(r, "play_mode", 1); juce::MemoryBlock mb0; o.getStateInformation(mb0); f.replaceWithData(mb0.getData(), mb0.getSize()); r.loadSession(f); const bool keptOn = getP(r, "play_mode") > 0.5f; f.deleteFile();
        check(ok && keptAlways && keptOn, "preset / .mickens file loads keep the current PLAY mode (ALWAYS stays ALWAYS, ON NOTES stays ON NOTES)"); }
      { setP(p, "play_mode", 1); p.diceStarts(); p.dnaDice(0); p.dnaUndo(0); std::vector<float> v; p.liveValues(v); check(getP(p, "play_mode") > 0.5f, "DICE, DNA dice and undo leave PLAY mode alone"); }
      p.storeSlot(1); p.loadFactoryPreset(2); const bool a = getP(p, "play_mode") > 0.5f; p.recallSlot(1); const bool b = getP(p, "play_mode") > 0.5f; setP(p, "play_mode", 0); p.recallSlot(1); const bool c = getP(p, "play_mode") < 0.5f;
      check(a && b && c, "presets and snapshot slots never change PLAY mode"); }
}

// SYNC TO DAW | OWN (Shared/SyncTransport.h)
static juce::AudioBuffer<float> syncRender(MickensProcessor& p, Head& head, int blocks, const std::function<void(int, juce::MidiBuffer&)>& f = {}) {
    p.setRateAndBufferSizeDetails(48000, 512); p.prepareToPlay(48000, 512); p.setPlayHead(&head);
    juce::AudioBuffer<float> out(2, blocks * 512), buf(2, 512); juce::MidiBuffer midi;
    for (int k = 0; k < blocks; ++k) { buf.clear(); midi.clear(); if (f) f(k, midi); p.processBlock(buf, midi); if (head.playing) head.ppq += 512 / (48000 * 60.0 / head.bpm); for (int c = 0; c < 2; ++c) out.copyFrom(c, k * 512, buf, c, 0, 512); }
    return out;
}
static float maxDiff(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int from = 0, int to = -1) { if (to < 0) to = juce::jmin(a.getNumSamples(), b.getNumSamples()); float d = 0; for (int c = 0; c < 2; ++c) for (int i = from; i < to; ++i) d = std::max(d, std::abs(a.getSample(c, i) - b.getSample(c, i))); return d; }
static float maxStep(const juce::AudioBuffer<float>& a, int from, int to) { float d = 0; for (int i = juce::jmax(0, from); i < juce::jmin(a.getNumSamples() - 1, to); ++i) d = std::max(d, std::abs(a.getSample(0, i + 1) - a.getSample(0, i))); return d; }
static void testSync(const mk::BreakLibrary&) {
    using Mode = ghosty::SyncTransport::Mode;
    MickensProcessor p; waitReady(p); setP(p, "reverb", 0.3f); setP(p, "b2_mute", 0);
    check(p.sync.getMode() == Mode::Daw && std::abs(p.sync.getOwnBpm() - 172.0) < 1e-9, "SYNC: a new instance is SYNC TO DAW, own tempo 172 (the TEMPO default)");
    // DAW: the classic host path. Matched OWN (own 174 bpm, own PLAY at the host's start) gives the same music
    { Head w; syncRender(p, w, 200);   // warm-up
      Head h1; h1.bpm = 174; auto daw = syncRender(p, h1, 700);
      p.sync.setMode(Mode::Own); p.sync.setOwnBpm(174); Head h2; h2.bpm = 174; auto own = syncRender(p, h2, 700, [&](int k, juce::MidiBuffer&) { if (k == 0) p.sync.startOwn(0.0); });
      const float d = maxDiff(daw, own);
      check(stats(daw).rms > 0.02f && d < 1e-4f, "SYNC: OWN at 174 bpm started at bar 1 sounds like SYNC TO DAW at 174 bpm (max diff " + juce::String(d, 7) + ")"); }
    // OWN: only the own tempo + own PLAY matter: host playing at 174 vs host stopped at 90 vs host playing at 133 from bar 9 -> identical
    { p.sync.setMode(Mode::Own); p.sync.setOwnBpm(160); auto start = [&](int k, juce::MidiBuffer&) { if (k == 3) p.sync.startOwn(0.0); };
      Head w; syncRender(p, w, 50, [&](int k, juce::MidiBuffer&) { if (k == 0) p.sync.stopOwn(); });
      Head a; a.bpm = 174; auto oa = syncRender(p, a, 600, start); p.sync.stopOwn();
      Head b; b.bpm = 90; b.playing = false; auto ob = syncRender(p, b, 600, start); p.sync.stopOwn();
      Head c; c.bpm = 133; c.ppq = 32; auto oc = syncRender(p, c, 600, start); p.sync.stopOwn();
      const float d1 = maxDiff(oa, ob), d2 = maxDiff(oa, oc);
      check(stats(oa).rms > 0.02f && stats(oa, 0, 3 * 512).peak == 0.0f && d1 == 0.0f && d2 == 0.0f, "SYNC OWN: output depends only on the own tempo + own PLAY (host 174 playing vs 90 stopped vs 133 from bar 9: max diff " + juce::String(d1, 7) + " / " + juce::String(d2, 7) + "; silent before own PLAY)");
      long long first = -1; for (int i = 0; i < p.engine.tlogN; ++i) if (p.engine.tlog[i].at >= 3 * 512) { first = p.engine.tlog[i].at; break; }
      check(first == 3 * 512, "SYNC OWN: own PLAY starts at ppq 0, the first hit on the very first sample of the 16th grid (" + juce::String(first - 3 * 512) + " samples off)");
      // the step grid follows the own tempo: hits every 1/16 at 160 bpm
      const double step160 = 48000 * 60.0 / 160 / 4; int off = 0, cnt = 0; for (int i = 0; i < p.engine.tlogN; ++i) { const double ph = std::fmod((p.engine.tlog[i].at - 3 * 512) / step160, 1.0); off += (ph > 0.01 && ph < 0.99); ++cnt; }
      check(cnt > 20 && off == 0, "SYNC OWN: every hit sits on the own 160 bpm 16th grid (" + juce::String(cnt) + " hits, " + juce::String(off) + " off)"); }
    // tempo change while playing in OWN: heard from the next block, no click
    { Head h; h.bpm = 120; const int chg = 300; auto o = syncRender(p, h, 700, [&](int k, juce::MidiBuffer&) { if (k == 0) p.sync.startOwn(0.0); if (k == chg) p.sync.setOwnBpm(140); });
      p.sync.stopOwn(); const float jump = maxStep(o, chg * 512 - 64, chg * 512 + 512), typ = maxStep(o, 2000, chg * 512 - 64);
      // hits after the change follow the 140 bpm grid counted from the change point
      long long a0 = -1, a1 = -1; for (int i = 0; i < p.engine.tlogN; ++i) { if (p.engine.tlog[i].at >= chg * 512 + 512 && a0 < 0) a0 = p.engine.tlog[i].at; else if (a0 >= 0 && p.engine.tlog[i].at > a0) { a1 = p.engine.tlog[i].at; break; } }
      const double gap = (double) (a1 - a0), want = 48000 * 60.0 / 140 / 4;
      check(jump <= typ && a1 > a0 && (std::fmod(gap / want, 1.0) < 0.01 || std::fmod(gap / want, 1.0) > 0.99), "SYNC OWN: tempo 160 -> 140 while playing: next hits " + juce::String(gap, 0) + " samples apart (a multiple of the 140 bpm 16th " + juce::String(want, 1) + "), biggest step at the change " + juce::String(jump, 4) + " vs the music's " + juce::String(typ, 4) + " (no click)"); }
    // mode switch mid-play: DAW playing -> OWN (stopped): stops, no stuck clock; -> DAW again: back on the host grid; OWN playing -> DAW -> OWN: own clock stopped
    { setP(p, "reverb", 0.0f); p.sync.setMode(Mode::Daw); Head h; h.bpm = 174; const int toOwn = 200, toDaw = 400;
      auto o = syncRender(p, h, 600, [&](int k, juce::MidiBuffer&) { if (k == toOwn) p.sync.setMode(Mode::Own); if (k == toDaw) p.sync.setMode(Mode::Daw); });
      const auto before = stats(o, 50 * 512, toOwn * 512), quiet = stats(o, (toOwn + 120) * 512, toDaw * 512), back = stats(o, toDaw * 512, 600 * 512);
      long long first = -1; for (int i = 0; i < p.engine.tlogN; ++i) if (p.engine.tlog[i].at >= toDaw * 512) { first = p.engine.tlog[i].at; break; }
      const double stepLen = 48000 * 60.0 / 174 / 4, ph = first >= 0 ? std::fmod(first / stepLen, 1.0) : 0.5;
      check(before.rms > 0.02f && quiet.peak < 1e-4f && back.rms > 0.02f && (ph < 0.01 || ph > 0.99), "SYNC: DAW playing -> OWN stops (peak " + juce::String(quiet.peak, 6) + " after the tails), -> DAW plays again on the host's 16th grid (phase " + juce::String(ph, 4) + ")");
      p.sync.setMode(Mode::Own); Head h2; h2.bpm = 174; syncRender(p, h2, 20, [&](int k, juce::MidiBuffer&) { if (k == 0) p.sync.startOwn(0.0); if (k == 10) p.sync.setMode(Mode::Daw); });
      p.sync.setMode(Mode::Own); Head h3; h3.playing = false; auto o3 = syncRender(p, h3, 200);
      check(!p.sync.isOwnPlaying() && stats(o3, 100 * 512, 200 * 512).peak < 1e-4f, "SYNC: OWN playing -> DAW -> OWN: the own clock was stopped by the switch (no hidden running clock, silent)"); setP(p, "reverb", 0.3f); }
    // PLAY: ON NOTES gates on top of OWN; BYPASS wins
    { p.sync.setMode(Mode::Own); setP(p, "play_mode", 1); Head h; h.playing = false; const int note = 200;
      auto o = syncRender(p, h, 400, [&](int k, juce::MidiBuffer& m) { if (k == 0) p.sync.startOwn(0.0); if (k == note) m.addEvent(juce::MidiMessage::noteOn(1, 24, 1.0f), 0); if (k == 330) setP(p, "bypass", 1); });
      setP(p, "bypass", 0); setP(p, "play_mode", 0); p.sync.stopOwn();
      check(stats(o, 0, note * 512).peak == 0.0f && stats(o, note * 512 + 2400, 330 * 512).rms > 0.02f && stats(o, 345 * 512, 400 * 512).peak == 0.0f, "SYNC OWN + PLAY ON NOTES + BYPASS: own clock running but silent until a note, plays while held, BYPASS silences it"); }
    // SPACE: OWN = toggles own play and is consumed; DAW (host present) = not consumed
    { std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor()); ed->setVisible(true); pump(100);
      Head h; h.playing = false; p.sync.setMode(Mode::Own); syncRender(p, h, 2); const bool was = p.sync.isOwnPlaying();
      const bool used = ed->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)); juce::AudioBuffer<float> buf(2, 512); juce::MidiBuffer m; p.processBlock(buf, m); const bool now = p.sync.isOwnPlaying();
      const bool used2 = ed->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)); p.processBlock(buf, m); const bool now2 = p.sync.isOwnPlaying();
      p.sync.setMode(Mode::Daw); p.processBlock(buf, m); const bool used3 = ed->keyPressed(juce::KeyPress(juce::KeyPress::spaceKey)); p.processBlock(buf, m);
      check(!was && used && now && used2 && !now2 && !used3 && !p.sync.isOwnPlaying(), "SPACE: in SYNC OWN it starts / stops Mr. Mickens and is consumed; in SYNC TO DAW it is not consumed (left to the DAW)");
      // header screenshots (MICKENS_SHOTS=dir): SYNC TO DAW (with a host) and OWN, PURPLE and BLACK
      if (auto* dir = std::getenv("MICKENS_SHOTS")) for (int dark = 0; dark < 2; ++dark) for (int own = 0; own < 2; ++own) {
          p.theme = dark == 1 ? 1 : 0; p.sync.setMode(own ? Mode::Own : Mode::Daw); Head hh; hh.bpm = 174; hh.playing = !own; p.processBlock(buf, m); p.setPlayHead(&hh); p.processBlock(buf, m); if (own) p.sync.startOwn(0.0); p.processBlock(buf, m);
          pump(400); auto img = ed->createComponentSnapshot(ed->getLocalBounds()); auto crop = img.getClippedImage({ 0, 0, img.getWidth(), 120 });
          const auto f = juce::File(dir).getChildFile(juce::String("mickens2-sync-") + (dark ? "black-" : "purple-") + (own ? "own" : "daw") + ".png"); f.deleteFile();
          juce::PNGImageFormat png; if (auto os = f.createOutputStream()) png.writeImageToStream(crop, *os); p.sync.stopOwn(); p.processBlock(buf, m); p.setPlayHead(nullptr); }
      p.theme = 0; pump(100); ed.reset(); }
    // state: per project; file / preset loads keep the current SYNC; old sessions load DAW; presets / slots / DICE / undo leave it alone
    { p.sync.setMode(Mode::Own); p.sync.setOwnBpm(133.5); juce::MemoryBlock mb; p.getStateInformation(mb);
      MickensProcessor q; waitReady(q); q.restoreState(mb.getData(), (int) mb.getSize());
      check(q.sync.getMode() == Mode::Own && std::abs(q.sync.getOwnBpm() - 133.5) < 1e-9, "SYNC: mode + own tempo survive a session save / load (OWN, 133.5 bpm)");
      auto xml = Peek::getXmlFromBinary(mb.getData(), (int) mb.getSize()); auto st = juce::ValueTree::fromXml(*xml); auto mm = st.getChildWithName("MODEL"); mm.removeProperty("sync", nullptr); mm.removeProperty("own_bpm", nullptr);
      juce::MemoryBlock old; Peek::copyXmlToBinary(*st.createXml(), old); q.restoreState(old.getData(), (int) old.getSize());
      check(q.sync.getMode() == Mode::Daw && std::abs(q.sync.getOwnBpm() - getP(q, "bpm")) < 1e-3, "SYNC: a session from before SYNC loads SYNC TO DAW (own tempo = its TEMPO knob, " + juce::String(q.sync.getOwnBpm(), 1) + ")");
      const auto f = juce::File::createTempFile(".mickens"); f.replaceWithData(old.getData(), old.getSize()); q.sync.setMode(Mode::Own); q.sync.setOwnBpm(99); q.loadSession(f); f.deleteFile();
      const bool keep = q.sync.getMode() == Mode::Own && std::abs(q.sync.getOwnBpm() - 99) < 1e-9;
      q.storeSlot(0); q.sync.setMode(Mode::Daw); q.recallSlot(0); q.loadFactoryPreset(1); q.diceStarts(); q.dnaDice(0); q.dnaUndo(0);
      check(keep && q.sync.getMode() == Mode::Daw, "SYNC: preset / .mickens loads, snapshot recall, factory presets, DICE and undo never change SYNC or the own tempo"); p.sync.setMode(Mode::Daw); }
}

// LIGHT theme (third look next to PURPLE / BLACK): contrast, nothing see-through, screenshots
static juce::Component* findId(juce::Component* c, const juce::String& id) { if (c->getComponentID() == id) return c; for (auto* k : c->getChildren()) if (auto* f = findId(k, id)) return f; return nullptr; }
static int holes(const juce::Image& img, juce::Rectangle<int> area) { int n = 0; for (int y = area.getY(); y < area.getBottom(); ++y) for (int x = area.getX(); x < area.getRight(); ++x) n += img.getPixelAt(x, y).getAlpha() < 255; return n; }
static void testTheme() {
    { const auto L = MickensEditor::contrastOf(2);
      check(L.body >= 7.0 && L.secondary >= 4.5 && L.accent >= 3.0, "LIGHT contrast: body text " + juce::String(L.body, 2) + ":1 (>= 7), secondary " + juce::String(L.secondary, 2) + ":1 (>= 4.5), accents incl. ghost + the whole rainbow " + juce::String(L.accent, 2) + ":1 (>= 3) on window / panel / cell");
      const auto P = MickensEditor::contrastOf(0), B = MickensEditor::contrastOf(1);
      std::cout << "      (unchanged looks, for reference: PURPLE body " << P.body << " secondary " << P.secondary << " | BLACK body " << B.body << " secondary " << B.secondary << ")\n"; }
    MickensProcessor p; waitReady(p); p.theme = 0;
    std::unique_ptr<juce::AudioProcessorEditor> edp(p.createEditor()); auto* ed = dynamic_cast<MickensEditor*>(edp.get()); ed->setVisible(true); pump(300);
    // play a bit so the lanes, sequence and meters show something
    { Head h; h.bpm = 174; p.setRateAndBufferSizeDetails(48000, 512); p.prepareToPlay(48000, 512); p.setPlayHead(&h); juce::AudioBuffer<float> buf(2, 512); juce::MidiBuffer m; for (int k = 0; k < 200; ++k) { p.processBlock(buf, m); h.ppq += 512 / (48000 * 60.0 / 174); } p.setPlayHead(nullptr); }
    const char* tn[] = { "PURPLE", "BLACK", "LIGHT" }; int bad = 0; juce::String worst;
    const auto shots = std::getenv("MICKENS_SHOTS") ? juce::File(std::getenv("MICKENS_SHOTS")) : juce::File();
    auto save = [&](const juce::Image& img, const juce::String& name) { if (shots == juce::File()) return; auto f = shots.getChildFile(name + ".png"); f.deleteFile(); juce::PNGImageFormat png; if (auto os = f.createOutputStream()) png.writeImageToStream(img, *os); };
    for (int t = 0; t < 3; ++t) {
        p.theme = t; pump(250);
        // the whole editor, painted alone onto a fully transparent image: no pixel may stay see-through
        { juce::Image img(juce::Image::ARGB, ed->getWidth(), ed->getHeight(), true); { juce::Graphics g(img); ed->paintEntireComponent(g, true); }
          const int h = holes(img, img.getBounds()); if (h) { ++bad; worst << tn[t] << " editor " << h << " px; "; } save(img, t == 2 ? juce::String("mickens2-light-main") : juce::String("mickens2-theme-check-") + tn[t]); }
        // each bay panel and the search overlay, alone: their whole surface (inside the rounded corners) is opaque
        for (int b = 0; b < 4; ++b) if (auto* c = findId(ed, "bayPanel" + juce::String(b))) { juce::Image img(juce::Image::ARGB, c->getWidth(), c->getHeight(), true); { juce::Graphics g(img); c->paintEntireComponent(g, true); }
            const int h = holes(img, img.getBounds().reduced(10)); if (h) { ++bad; worst << tn[t] << " bay " << b << " " << h << " px; "; } }
        for (int md = 0; md < 3; ++md) { ed->openSearch(md == 0 ? 0 : 1, md); pump(150);
            if (auto* c = findId(ed, "searchPanel")) { juce::Image img(juce::Image::ARGB, c->getWidth(), c->getHeight(), true); { juce::Graphics g(img); c->paintEntireComponent(g, true); }
                const int h = holes(img, img.getBounds().reduced(12)); if (h) { ++bad; worst << tn[t] << " search " << md << " " << h << " px; "; } }
            if (t == 2) { static const char* sn[] = { "search-feel", "match-similar", "match-complement" }; save(ed->createComponentSnapshot(ed->getLocalBounds()), juce::String("mickens2-light-") + sn[md]); } }
        ed->closeSearch(); pump(60);
        // BYPASS / SYNC use the house tokens of this look (Shared/GhostThemes.h): their idle pill is the theme's cell colour
        for (auto* id : { "bypassButton", "syncButton" }) if (auto* c = findId(ed, id)) { auto img = c->createComponentSnapshot(c->getLocalBounds()); const auto px = img.getPixelAt(7, c->getHeight() / 2);
            const auto want = ghosty::themes::get(ghosty::themes::fromMickensIndex(t)).cell; const int d = std::abs(px.getRed() - want.getRed()) + std::abs(px.getGreen() - want.getGreen()) + std::abs(px.getBlue() - want.getBlue());
            if (d > 12) { ++bad; worst << tn[t] << " " << id << " idle " << px.toDisplayString(false) << " vs cell " << want.toDisplayString(false) << "; "; } }
    }
    check(bad == 0, "nothing see-through in PURPLE / BLACK / LIGHT: the editor, every bay panel and the BREAK SEARCH / MATCH overlay paint opaque on a transparent image; BYPASS / SYNC wear each look's house tokens" + (bad ? " (" + worst + ")" : juce::String()));
    // the theme choice is saved as before ("dark") plus "theme" for LIGHT; an old session ("dark" only) loads its look
    { p.theme = 2; juce::MemoryBlock mb; p.getStateInformation(mb); MickensProcessor q; waitReady(q); q.restoreState(mb.getData(), (int) mb.getSize()); const bool lt = q.theme == 2;
      auto xml = Peek::getXmlFromBinary(mb.getData(), (int) mb.getSize()); auto st = juce::ValueTree::fromXml(*xml); auto mm = st.getChildWithName("MODEL"); mm.removeProperty("theme", nullptr); mm.setProperty("dark", true, nullptr);
      juce::MemoryBlock old; Peek::copyXmlToBinary(*st.createXml(), old); q.restoreState(old.getData(), (int) old.getSize()); const bool oldBlack = q.theme == 1;
      check(lt && oldBlack, "theme saved with the session: LIGHT round-trips; an older session saved as BLACK (\"dark\" only) loads BLACK"); }
    p.theme = 0; pump(100); edp.reset();
}

// final polish: slider convention on the feel BARS, version string, demo gate on every export
static juce::MouseEvent mouseAt(juce::Component& c, float x, juce::ModifierKeys mods = {}, int clicks = 1, juce::Point<float> down = { -1, -1 }) {
    const auto pt = juce::Point<float>(x, c.getHeight() * 0.5f);
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pt, mods, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, juce::Time::getCurrentTime(), down.x < 0 ? pt : down, juce::Time::getCurrentTime(), clicks, false);
}
static void testPolish() {
    check(MickensEditor::versionString() == JucePlugin_VersionString, "version: the header shows v" + MickensEditor::versionString() + " (JucePlugin_VersionString, " JucePlugin_VersionString ")");
    MickensProcessor p; waitReady(p);
    std::unique_ptr<juce::AudioProcessorEditor> edp(p.createEditor()); auto* ed = dynamic_cast<MickensEditor*>(edp.get()); ed->setVisible(true); pump(200);
    ed->openSearch(0, 0); pump(100);
    std::vector<float> before; for (auto* pr : p.getParameters()) before.push_back(pr->getValue());
    auto* bar = dynamic_cast<juce::Slider*>(findId(ed, "feel1")); auto* any = dynamic_cast<juce::Button*>(findId(ed, "any1"));
    if (!bar || !any) { check(false, "feel bars found"); return; }
    const float x0 = (float) bar->getPositionOfValue(0.0), x1 = (float) bar->getPositionOfValue(1.0);
    double got[3]; int k = 0;
    for (double f : { 0.25, 0.5, 0.75 }) { const float x = x0 + (float) f * (x1 - x0); bar->mouseDown(mouseAt(*bar, x)); bar->mouseUp(mouseAt(*bar, x)); got[k++] = bar->getValue(); }
    check(std::abs(got[0] - 0.25) < 0.01 && std::abs(got[1] - 0.5) < 0.01 && std::abs(got[2] - 0.75) < 0.01 && !any->getToggleState(),
          "feel BAR: a click at 25 / 50 / 75 % of the bar sets " + juce::String(got[0], 3) + " / " + juce::String(got[1], 3) + " / " + juce::String(got[2], 3) + " (absolute, ANY switches off)");
    // drag scrubs; Shift = fine (1/10)
    { const float xs = x0 + 0.2f * (x1 - x0); bar->mouseDown(mouseAt(*bar, xs)); bar->mouseDrag(mouseAt(*bar, x0 + 0.6f * (x1 - x0))); const double scrub = bar->getValue(); bar->mouseUp(mouseAt(*bar, x0 + 0.6f * (x1 - x0)));
      const juce::ModifierKeys sh(juce::ModifierKeys::shiftModifier); bar->setValue(0.5); bar->mouseDown(mouseAt(*bar, xs, sh)); bar->mouseDrag(mouseAt(*bar, xs + 0.4f * (x1 - x0), sh)); const double fine = bar->getValue(); bar->mouseUp(mouseAt(*bar, xs, sh));
      check(std::abs(scrub - 0.6) < 0.01 && std::abs(fine - 0.54) < 0.005, "feel BAR: drag scrubs to " + juce::String(scrub, 3) + " (0.6); Shift-drag over 40 % of the bar moves only " + juce::String(fine - 0.5, 3) + " (fine, 1/10)"); }
    // wheel = one step
    { bar->setValue(0.5); juce::MouseWheelDetails w {}; w.deltaY = 0.2f; bar->mouseWheelMove(mouseAt(*bar, x0), w); const double up = bar->getValue(); w.deltaY = -0.5f; bar->mouseWheelMove(mouseAt(*bar, x0), w); bar->mouseWheelMove(mouseAt(*bar, x0), w);
      check(std::abs(up - 0.51) < 1e-6 && std::abs(bar->getValue() - 0.49) < 1e-6, "feel BAR: mouse wheel = one 1 % step per notch (0.50 -> " + juce::String(up, 2) + " -> " + juce::String(bar->getValue(), 2) + ")"); }
    // type-in (what TypeIn applies) and the hover readout, in real units; tempo bar in bpm
    { auto* tempo = dynamic_cast<juce::Slider*>(findId(ed, "feel0"));
      check(std::abs(bar->getValueFromText("37 %") - 0.37) < 1e-9 && bar->getTextFromValue(0.62) == "62 %" && tempo && std::abs(tempo->getValueFromText("140") - 0.6) < 1e-9 && tempo->getTextFromValue(0.6) == "140 bpm",
            "feel BAR: right-click type-in reads real units (\"37 %\" -> 0.37, tempo \"140\" -> 140 bpm) and hover shows them (\"62 %\", \"140 bpm\")"); }
    // double-click = default (the middle, back to ANY)
    { bar->setValue(0.8); bar->mouseDoubleClick(mouseAt(*bar, x0 + 0.8f * (x1 - x0), {}, 2));
      check(std::abs(bar->getValue() - 0.5) < 1e-9 && any->getToggleState(), "feel BAR: double-click resets to the default (0.5, ANY on)"); }
    { int changed = 0; int i = 0; for (auto* pr : p.getParameters()) changed += std::abs(pr->getValue() - before[(size_t) i++]) > 1e-7f;
      check(changed == 0, "feel BARS are not host parameters: clicking, dragging, wheel and resets leave every automatable parameter untouched (" + juce::String(changed) + " changed)"); }
    ed->closeSearch(); pump(50); edp.reset();
    // demo gate: with export refused, every export path returns nothing and writes nothing; SAVE setup / preset (.mickens) still work
    { int asked = 0; p.exportGate = [&] { ++asked; return false; };
      const auto tmp = juce::File::createTempFile(".mickens"); tmp.deleteFile();
      const bool wav = p.bounce(1) == juce::File(), stems = p.bounceStems(1).isEmpty(), midi = p.exportMidi(0, 1) == juce::File();
      const bool setup = p.writeSetup(tmp) && tmp.existsAsFile(), sess = p.saveSession("demo gate test"), pre = p.saveUserPreset("demo gate test");
      const bool saved = p.saveDir().getChildFile("demo gate test.mickens").existsAsFile() && p.presetDir().getChildFile("demo gate test.mickens").existsAsFile();
      tmp.deleteFile(); p.saveDir().getChildFile("demo gate test.mickens").deleteFile(); p.presetDir().getChildFile("demo gate test.mickens").deleteFile();
      p.exportGate = nullptr; const auto midiOk = p.exportMidi(0, 1); const bool works = midiOk.existsAsFile(); midiOk.deleteFile();
      check(wav && stems && midi && asked == 3 && setup && sess && pre && saved && works, "DEMO GATE: drag-out WAV, stems and MIDI each ask the license and are refused when locked (" + juce::String(asked) + " of 3 asked, nothing written); SAVE setup / session / preset (.mickens, not an export) work in demo; unlocked, MIDI export works again"); }
}

// LOOP SECTION on the BAR ruler: right-click plays from a bar, right-drag picks bars, the grips export only those bars
static void testSection() {
    MickensProcessor p; waitReady(p);
    { Head h; h.bpm = 174; p.setRateAndBufferSizeDetails(48000, 512); p.prepareToPlay(48000, 512); p.setPlayHead(&h); juce::AudioBuffer<float> buf(2, 512); juce::MidiBuffer m; p.processBlock(buf, m); p.setPlayHead(nullptr); }
    std::unique_ptr<juce::AudioProcessorEditor> edp(p.createEditor()); auto* ed = dynamic_cast<MickensEditor*>(edp.get()); ed->setVisible(true); pump(200);
    auto* ruler = findId(ed, "barRuler"); if (!ruler) { check(false, "bar ruler found"); return; }
    auto barMid = [&](int b) { return 84.0f + (ruler->getWidth() - 88.0f) * (b + 0.5f) / 16.0f; };
    auto ev = [&](float x, float x0, bool right, int clicks = 1) { return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), { x, 12.0f }, right ? juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier) : juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier),
        1.0f, 0, 0, 0, 0, ruler, ruler, juce::Time::getCurrentTime(), { x0, 12.0f }, juce::Time::getCurrentTime(), clicks, std::abs(x - x0) > 3); };
    // plain clicks never make a section; a right-click plays from that bar (own clock here)
    p.sync.setMode(ghosty::SyncTransport::Mode::Own);
    ruler->mouseDown(ev(barMid(4), barMid(4), false)); ruler->mouseUp(ev(barMid(4), barMid(4), false));
    ruler->mouseDown(ev(barMid(4), barMid(4), true)); ruler->mouseDrag(ev(barMid(4) + 4, barMid(4), true)); ruler->mouseUp(ev(barMid(4) + 4, barMid(4), true));
    { juce::AudioBuffer<float> buf(2, 512); juce::MidiBuffer m; p.processBlock(buf, m); }
    const bool noSec = p.secA < 0 && p.secB < 0, playing = p.sync.isOwnPlaying(), fromBar5 = std::abs(p.sync.getOwnPpq() - (16.0 + 512 * p.sync.getOwnBpm() / 60.0 / 48000)) < 1e-6;
    check(noSec && playing && fromBar5, "BAR ruler: a plain click or a right-click (or a 4 px wobble) makes no section; the right-click plays from that bar (own clock at bar 5, ppq " + juce::String(p.sync.getOwnPpq(), 3) + ")");
    p.sync.stopOwn(); p.sync.setMode(ghosty::SyncTransport::Mode::Daw);
    // right-drag from bar 3 to bar 6 = section bars 3-6 with grips
    ruler->mouseDown(ev(barMid(2), barMid(2), true)); ruler->mouseDrag(ev(barMid(5), barMid(2), true)); ruler->mouseUp(ev(barMid(5), barMid(2), true)); pump(60);
    int grips = 0; for (auto* c : ruler->getChildren()) grips += c->isVisible();
    check(p.secA == 2 && p.secB == 6 && grips == 3, "BAR ruler: right-drag bar 3 -> 6 = LOOP SECTION bars 3-6 with its MIDI + WAV grips (+ the LOOP button)");
    // ADDITIVE: the ruler lives in its own strip under the original 1500 x 1020 layout; everything above is pixel-identical with or without it
    { ed->setSize(1500, 1056); pump(60); auto* tl = findId(ed, "barRuler"); auto withR = ed->createComponentSnapshot(ed->getLocalBounds()); tl->setVisible(false); auto without = ed->createComponentSnapshot(ed->getLocalBounds()); tl->setVisible(true);
      int diff = 0; for (int y = 0; y < 1020; y += 1) for (int x = 0; x < 1500; x += 1) diff += withR.getPixelAt(x, y) != without.getPixelAt(x, y);
      check(diff == 0 && tl->getY() >= 1020 && ed->getHeight() == 1056, "BAR ruler: its own strip below the original 1500 x 1020 layout (y " + juce::String(tl->getY()) + "); the unicorn strip and every control above are pixel-identical with or without it (" + juce::String(diff) + " px differ)"); }
    if (auto* dir = std::getenv("MICKENS_SHOTS")) for (int t : { 0, 1, 2 }) { p.theme = t; pump(200); auto img = ed->createComponentSnapshot(ed->getLocalBounds());
        static const char* nm[] = { "purple", "black", "light" }; auto f = juce::File(dir).getChildFile(juce::String("mickens2-section-") + nm[t] + ".png"); f.deleteFile(); juce::PNGImageFormat png; if (auto os = f.createOutputStream()) png.writeImageToStream(img, *os); }
    p.theme = 0; pump(100);
    // the grips' exports: only those bars, from their own bar, tempo in the name
    const double bpmNow = p.currentBpm(); const juce::String tag = juce::String(juce::roundToInt(bpmNow)) + "bpm bars 3-6";
    { const auto f = p.bounceRange(p.secA, p.secB); juce::WavAudioFormat wav; std::unique_ptr<juce::AudioFormatReader> rd(wav.createReaderFor(new juce::FileInputStream(f), true));
      const juce::int64 want = (juce::int64) (4 * 4 * 60.0 / bpmNow * 48000) + 96000; juce::AudioBuffer<float> a(2, rd ? (int) rd->lengthInSamples : 1); if (rd) rd->read(&a, 0, a.getNumSamples(), 0, true, true);
      const auto st = stats(a, 0, juce::jmin(a.getNumSamples(), (int) (want - 96000)));
      check(rd && rd->lengthInSamples == want && st.rms > 0.02f && f.getFileName().contains(tag), "SECTION WAV: " + f.getFileName() + ", " + juce::String(rd ? rd->lengthInSamples : 0) + " samples = 4 bars at " + juce::String(bpmNow, 1) + " bpm + 2 s tail (" + juce::String(want) + "), RMS " + juce::String(st.rms, 3));
      f.deleteFile(); }
    { const auto f = p.exportMidiRange(p.secA, p.secB); juce::MidiFile mf; juce::FileInputStream is(f); const bool ok = is.openedOk() && mf.readFrom(is); int notes = 0; double last = 0;
      if (ok) for (int t = 0; t < mf.getNumTracks(); ++t) for (int i = 0; i < mf.getTrack(t)->getNumEvents(); ++i) { const auto& mm = mf.getTrack(t)->getEventPointer(i)->message; if (mm.isNoteOn()) { ++notes; last = juce::jmax(last, mm.getTimeStamp()); } }
      check(ok && mf.getNumTracks() >= 2 && notes > 20 && last < 4 * 3840 && f.getFileName().contains(tag), "SECTION MIDI: " + f.getFileName() + ", " + juce::String(mf.getNumTracks()) + " tracks, " + juce::String(notes) + " notes, all inside the 4 bars");
      f.deleteFile(); }
    // a section is cut from the song at its own bar: bars 3-6 are not just bars 1-4 again when fills fall there
    // demo: the grips refuse
    { p.exportGate = [] { return false; }; const bool refused = p.bounceRange(2, 6) == juce::File() && p.exportMidiRange(2, 6) == juce::File(); p.exportGate = nullptr;
      check(refused, "SECTION drag-out in demo mode: WAV and MIDI refused (the license gate)"); }
    // saved with the session; an older session has none; double-click clears
    { juce::MemoryBlock mb; p.getStateInformation(mb); MickensProcessor q; waitReady(q); q.restoreState(mb.getData(), (int) mb.getSize()); const bool kept = q.secA == 2 && q.secB == 6;
      auto xml = Peek::getXmlFromBinary(mb.getData(), (int) mb.getSize()); auto st = juce::ValueTree::fromXml(*xml); auto mm = st.getChildWithName("MODEL"); mm.removeProperty("sec_a", nullptr); mm.removeProperty("sec_b", nullptr);
      juce::MemoryBlock old; Peek::copyXmlToBinary(*st.createXml(), old); q.restoreState(old.getData(), (int) old.getSize());
      ruler->mouseDoubleClick(ev(barMid(3), barMid(3), false, 2));
      check(kept && q.secA < 0 && q.secB < 0 && p.secA < 0, "SECTION: saved with the session; an older session loads with no section; double-click on the ruler clears it"); }
    pump(50); edp.reset();
}

// LOOP SECTION playback: the own clock, the DAW-stopped preview and the DAW's playback loop only the braced bars
static void testSectionLoop() {
    using Mode = ghosty::SyncTransport::Mode;
    MickensProcessor p; waitReady(p); setP(p, "reverb", 0.3f); setP(p, "b2_mute", 0);
    const double spb = 48000 * 60.0 / 174; const long Wrap = (long) std::ceil(8 * spb - 1e-6);   // bars 2-3 = 8 beats
    auto render = [&](Head& h, int blocks, const std::function<void(int)>& f) { p.setRateAndBufferSizeDetails(48000, 512); p.prepareToPlay(48000, 512); p.setPlayHead(&h);
        juce::AudioBuffer<float> out(2, blocks * 512), buf(2, 512); juce::MidiBuffer m;
        for (int k = 0; k < blocks; ++k) { buf.clear(); if (f) f(k); p.processBlock(buf, m); if (h.playing) h.ppq += 512 / spb; for (int c = 0; c < 2; ++c) out.copyFrom(c, k * 512, buf, c, 0, 512); }
        p.setPlayHead(nullptr); return out; };
    auto same = [](const std::vector<std::pair<long long, int>>& a, const std::vector<std::pair<long long, int>>& b) { if (a.size() != b.size() || a.empty()) { std::cout << "      sizes " << a.size() << " / " << b.size() << "\n"; return false; }
        for (size_t i = 0; i < a.size(); ++i) if (a[i].second != b[i].second || std::abs(a[i].first - b[i].first) > 2) { std::cout << "      #" << i << ": " << a[i].first << "/" << a[i].second << " vs " << b[i].first << "/" << b[i].second << "\n"; return false; } return true; };   // same slices, same 16ths (a pass starts on a fractional sample)
    auto slices = [&](long from, long to) { std::vector<std::pair<long long, int>> v; for (int i = 0; i < p.engine.tlogN; ++i) if (p.engine.tlog[i].at >= from - 2 && p.engine.tlog[i].at < to - 2) v.push_back({ p.engine.tlog[i].at - from, p.engine.tlog[i].bay * 1000 + p.engine.tlog[i].slice }); return v; };   // windows start 2 samples early (a pass starts on a fractional sample)
    // off = unchanged: a brace with LOOP off plays exactly like no brace
    { Head w; render(w, 100, {}); Head h1; h1.bpm = 174; auto a = render(h1, 600, {}); p.secA = 1; p.secB = 3; p.secLoop = false; Head h2; h2.bpm = 174; auto b = render(h2, 600, {});
      check(stats(a).rms > 0.02f && maxDiff(a, b) == 0.0f, "LOOP SECTION off: a brace changes nothing (max diff " + juce::String(maxDiff(a, b), 7) + ")"); }
    // OWN clock, LOOP ON bars 2-3: wraps at 8 beats, every loop pass plays the same slices, no click at the wrap
    p.secLoop = true; p.sync.setMode(Mode::Own); p.sync.setOwnBpm(174);
    { Head h; h.playing = false; auto o = render(h, 900, [&](int k) { if (k == 0) p.sync.startOwn(p.ownStartFor(0)); });
      const auto l1 = slices(0, Wrap), l2 = slices(Wrap, 2 * Wrap), l3 = slices(2 * Wrap, 3 * Wrap);
      long long firstAfter = -1; for (int i = 0; i < p.engine.tlogN; ++i) if (p.engine.tlog[i].at >= Wrap - 2) { firstAfter = p.engine.tlog[i].at; break; }
      const float jump = maxStep(o, (int) Wrap - 32, (int) Wrap + 300), typ = maxStep(o, 3000, (int) Wrap - 64);
      check(same(l1, l2) && same(l2, l3) && std::abs(firstAfter - Wrap) <= 1, "LOOP ON bars 2-3 (own clock): wraps at sample " + juce::String(firstAfter) + " (8 beats at 174 = " + juce::String(Wrap) + "), every pass plays the same " + juce::String((int) l1.size()) + " slices");
      check(jump <= typ, "LOOP wrap: no click (biggest step at the wrap " + juce::String(jump, 4) + " vs the music's " + juce::String(typ, 4) + ")");
      p.sync.stopOwn(); render(h, 4, {});
      // the DAW's playback maps into the loop the same way (DAW bar 1 = the section start): host 174 from 0 = own 174 from 0
      p.sync.setMode(Mode::Daw); Head hd; hd.bpm = 174; auto d = render(hd, 900, {});
      check(maxDiff(o, d) < 1e-4f, "LOOP ON in SYNC TO DAW: the DAW's bars map into bars 2-3 exactly like the own clock (max diff " + juce::String(maxDiff(o, d), 7) + ")"); }
    // the DAW-stopped preview (PLAY) loops too
    { Head h; h.playing = false; auto o = render(h, 900, [&](int k) { if (k == 0) p.setPlaying(true); }); p.setPlaying(false);
      const auto l1 = slices(0, Wrap), l2 = slices(Wrap, 2 * Wrap);
      check(stats(o).rms > 0.02f && same(l1, l2), "LOOP ON, DAW stopped: the PLAY preview starts at the section and loops it (" + juce::String((int) l1.size()) + " slices per pass, identical)"); }
    // saved with the session; key L toggles it (with a toast)
    { juce::MemoryBlock mb; p.getStateInformation(mb); MickensProcessor q; waitReady(q); q.restoreState(mb.getData(), (int) mb.getSize());
      std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor()); ed->setVisible(true); pump(150);
      const bool used = ed->keyPressed(juce::KeyPress('l', juce::ModifierKeys(), 'l')); pump(80); const bool off = !p.secLoop.load();
      auto* toast = findId(ed.get(), "toast"); ed->keyPressed(juce::KeyPress('l', juce::ModifierKeys(), 'l')); pump(80); const bool on = p.secLoop.load(); const bool toastShown = toast && toast->isVisible();
      check(q.sectionLooping() && q.secA == 1 && q.secB == 3 && used && off && on && toastShown, "LOOP SECTION: saved with the session (bars 2-3, ON); key L toggles it off / on, with a toast");
      ed.reset(); }
    p.secLoop = false; p.secA = -1; p.secB = -1;
}
#include "DemoGateTests.inc"
#include "GhostLookTests.inc"
// deterministic stills of every look (the animation frozen): the main page + the BREAK SEARCH / MATCH overlays, for pixel comparisons between builds
static void lookShots(const juce::File& dir, std::initializer_list<int> looks) {
    dir.createDirectory(); MickensEditor::freezeAnimation = true;
    MickensProcessor p; waitReady(p); p.theme = 0;
    std::unique_ptr<juce::AudioProcessorEditor> edp(p.createEditor()); auto* ed = dynamic_cast<MickensEditor*>(edp.get()); ed->setVisible(true); pump(300);
    auto save = [&](const juce::String& name) { auto img = ed->createComponentSnapshot(ed->getLocalBounds()); auto f = dir.getChildFile(name + ".png"); f.deleteFile(); juce::PNGImageFormat png; if (auto os = f.createOutputStream()) png.writeImageToStream(img, *os); };
    static const char* tn[] = { "purple", "black", "light", "ghost" };
    for (int t : looks) {
        p.theme = t; pump(300); save(juce::String("mick-") + tn[t] + "-main");
        static const char* sn[] = { "search-feel", "match-similar", "match-complement" };
        for (int md = 0; md < 3; ++md) { ed->openSearch(md == 0 ? 0 : 1, md); pump(200); save(juce::String("mick-") + tn[t] + "-" + sn[md]); }
        ed->closeSearch(); pump(100);
    }
    p.theme = 0; pump(100); edp.reset(); MickensEditor::freezeAnimation = false;
}
int main(int argc, char** argv) {
    juce::ScopedJuceInitialiser_GUI init;
    if (argc >= 2 && juce::String(argv[1]) == "ghostui") { testGhostLook(argc >= 3 ? juce::File(juce::String(argv[2])) : juce::File()); std::cout << "\n" << passes << " passed, " << fails << " failed\n"; return fails; }
    if (argc >= 3 && juce::String(argv[1]) == "lookshots") { lookShots(juce::File(juce::String(argv[2])), { 0, 1, 2, 3 }); return 0; }
    if (argc >= 2 && juce::String(argv[1]) == "demogate") return demogateTest();
    if (argc >= 3 && juce::String(argv[1]) == "demos") { demos(juce::File(juce::String(argv[2]))); return 0; }
    if (argc >= 2 && juce::String(argv[1]) == "dnatune") {   // sweep the blind classifier's hat / cymbal thresholds against the rendered truth
        mk::BreakLibrary lib; lib.build(48000); double best = -1;
        for (float hatT : { 0.002f, 0.005f, 0.01f }) for (float adapt : { 0.0f, 0.03f, 0.06f, 0.1f, 0.2f }) for (float tail : { 0.15f, 0.2f, 0.3f, 0.45f }) for (float tl : { 0.05f, 0.1f, 0.2f, 9.0f }) {
            mk::kHatT = hatT; mk::kHatAdapt = adapt; mk::kCymTail = tail; mk::kCymTailLong = tl; double ev[5], st[5];
            for (int c = 0; c < 5; ++c) { int tp = 0, fp = 0, fn = 0, agree = 0, tot = 0; for (auto& b : lib.breaks()) { mk::Break blind = *b; blind.truth.clear(); auto d = mk::analyzeBreak(blind);
                for (int s = 0; s < 32; ++s) { const bool pr = d.cls[(size_t) s] & (1 << c), t = b->truth[(size_t) s] & (1 << c); tp += pr && t; fp += pr && !t; fn += t && !pr; agree += pr == t; ++tot; } } ev[c] = tp / (double) juce::jmax(1, tp + fp + fn); st[c] = agree / (double) tot; }
            const double score = ev[3] + ev[4] + 0.5 * (st[3] + st[4]); if (score > best) { best = score; std::cout << "hatT " << hatT << " adapt " << adapt << " tail " << tail << " tailLong " << tl << " -> hat ev " << ev[3] << " st " << st[3] << " | cym ev " << ev[4] << " st " << st[4] << " | ghost ev " << ev[2] << " (score " << score << ")\n"; } }
        return 0; }
    mk::BreakLibrary lib; lib.build(48000);
    testDNA(lib); testRemix(lib); testEngineFills(lib); testInterlock(lib); testStutter(lib); testFilterRouter(); testProcessor(); testGate(lib); testBypass(lib); testPlayMode(lib); testSync(lib); testTheme(); testPolish(); testSection(); testSectionLoop(); testBackCompat(); testGhostLook(juce::File());
    std::cout << "\n" << passes << " passed, " << fails << " failed\n";
    return fails;
}
