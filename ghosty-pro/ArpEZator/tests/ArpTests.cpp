// ArpEZatorTests: chord parity with the web version, engine checks, LISTEN, and a render harness through the real
// processor (host-synced fake playhead) that checks the MIDI out is sample-accurate and balanced.
//   ArpEZatorTests --ref chords_ref.txt --out <dir> [--stats] [--demos] [--dump-recipe file] [--make-starters dir]
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include "../ArpProcessor.h"
#include "../ArpEditor.h"
#include "../RecipeJson.h"
#include "CoreTests.h"
#include "MelodyStats.h"
#include "HookLabTests.h"
#include "VarietyTests.h"
#include "../UrbanMoods.h"
#include "HookFingerprint.h"
#include <cmath>
#include <map>
#include <set>
#include <complex>
#include <new>
// counts heap allocations made while a test marks the current thread "audio" (YOUR SAMPLE: the audio thread never allocates)
static thread_local bool gAudioThread = false;
static std::atomic<long> gAudioAllocs { 0 };
void* operator new (std::size_t n) { if (gAudioThread) ++gAudioAllocs; if (void* p = std::malloc (n == 0 ? 1 : n)) return p; throw std::bad_alloc(); }
void* operator new[] (std::size_t n) { if (gAudioThread) ++gAudioAllocs; if (void* p = std::malloc (n == 0 ? 1 : n)) return p; throw std::bad_alloc(); }
void* operator new (std::size_t n, const std::nothrow_t&) noexcept { if (gAudioThread) ++gAudioAllocs; return std::malloc (n == 0 ? 1 : n); }
void* operator new[] (std::size_t n, const std::nothrow_t&) noexcept { if (gAudioThread) ++gAudioAllocs; return std::malloc (n == 0 ? 1 : n); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

namespace arp { std::map<std::string, int>& hookFailCounts(); }
using namespace arp;
using arptest::Counter;

namespace
{
struct FakeHead : juce::AudioPlayHead
{
    double bpm = 124, ppq = 0; bool playing = true; juce::int64 samples = 0;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p; p.setBpm (bpm); p.setIsPlaying (playing); p.setPpqPosition (ppq); p.setTimeInSamples (samples);
        p.setTimeSignature (TimeSignature { 4, 4 });
        return p;
    }
};
struct MidiEv { juce::int64 at; int ch, pitch; bool on; int vel; };

bool writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double sr)
{
    f.deleteFile();
    std::unique_ptr<juce::FileOutputStream> os (f.createOutputStream());
    if (os == nullptr) return false;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, (unsigned) b.getNumChannels(), 16, {}, 0));
    if (w == nullptr) return false;
    os.release();
    return w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

// render through the processor; events = MIDI out with absolute sample positions
struct RenderOpts { double sr = 44100, bpm = 124; int bars = 8; int block = 512; bool varBlocks = false; double stopAtBar = -1, restartAtBar = -1; double bypassAtBar = -1, bypassBars = 0.5; double loopBackAtBar = -1; };
juce::AudioBuffer<float> render (ArpProcessor& p, const RenderOpts& o, std::vector<MidiEv>& events, int& bypassBlocks, double* playedSamples = nullptr)
{
    FakeHead head; head.bpm = o.bpm; p.setPlayHead (&head);
    p.setRateAndBufferSizeDetails (o.sr, o.block); p.prepareToPlay (o.sr, o.block);
    const double samplesPerBar = 4 * 60.0 / o.bpm * o.sr;
    const double extraBars = (o.loopBackAtBar >= 0 ? o.loopBackAtBar : 0.0) + (o.restartAtBar >= 0 ? o.restartAtBar - o.stopAtBar : 0.0);
    const juce::int64 total = (juce::int64) std::ceil (samplesPerBar * (o.bars + extraBars)) + (juce::int64) (o.sr * 1.5);
    juce::AudioBuffer<float> out (2, (int) total); out.clear();
    juce::Random rnd (7); juce::int64 pos = 0; double songSamples = 0; bool looped = false;
    bypassBlocks = 0;
    while (pos < total)
    {
        int n = o.varBlocks ? 32 + rnd.nextInt (900) : o.block; n = (int) juce::jmin ((juce::int64) n, total - pos);
        const double bar = songSamples / samplesPerBar;
        head.playing = ! (o.stopAtBar >= 0 && bar >= o.stopAtBar && (o.restartAtBar < 0 || pos / samplesPerBar < o.restartAtBar)) && songSamples < samplesPerBar * o.bars;
        if (o.loopBackAtBar >= 0 && ! looped && bar >= o.loopBackAtBar) { songSamples = 0; looped = true; }
        head.ppq = songSamples / o.sr * o.bpm / 60.0; head.samples = (juce::int64) songSamples;
        juce::AudioBuffer<float> b (2, n); b.clear(); juce::MidiBuffer m;
        const double pb = pos / samplesPerBar;
        if (o.bypassAtBar >= 0 && pb >= o.bypassAtBar && pb < o.bypassAtBar + o.bypassBars) { p.processBlockBypassed (b, m); ++bypassBlocks; }
        else p.processBlock (b, m);
        for (const auto meta : m)
        {
            const auto msg = meta.getMessage();
            if (msg.isNoteOn()) events.push_back ({ pos + meta.samplePosition, msg.getChannel(), msg.getNoteNumber(), true, msg.getVelocity() });
            else if (msg.isNoteOff()) events.push_back ({ pos + meta.samplePosition, msg.getChannel(), msg.getNoteNumber(), false, 0 });
        }
        for (int c = 0; c < 2; ++c) out.copyFrom (c, (int) pos, b, c, 0, n);
        pos += n; if (head.playing) songSamples += n;
    }
    p.setPlayHead (nullptr);
    if (playedSamples != nullptr) *playedSamples = songSamples;
    return out;
}
bool balanced (const std::vector<MidiEv>& ev, std::string& why)
{
    std::map<int, int> on;
    for (auto& e : ev)
    {
        const int k = e.ch * 128 + e.pitch;
        if (e.on) { if (on[k] > 0) { why = "double note-on ch " + std::to_string (e.ch) + " pitch " + std::to_string (e.pitch) + " at " + std::to_string (e.at); return false; } on[k]++; }
        else { if (on[k] <= 0) { why = "note-off without note-on at " + std::to_string (e.at) + " ch " + std::to_string (e.ch) + " pitch " + std::to_string (e.pitch); return false; } on[k]--; }
    }
    for (auto& kv : on) if (kv.second != 0) { why = "note left hanging (ch*128+pitch " + std::to_string (kv.first) + ")"; return false; }
    return true;
}
void audioStats (const juce::AudioBuffer<float>& b, double& peak, double& rms, bool& nan)
{
    peak = 0; rms = 0; nan = false; double n = 0;
    for (int c = 0; c < b.getNumChannels(); ++c) for (int i = 0; i < b.getNumSamples(); ++i) { const float x = b.getSample (c, i); if (! std::isfinite (x)) { nan = true; continue; } peak = std::max (peak, (double) std::abs (x)); rms += x * x; n += 1; }
    rms = std::sqrt (rms / std::max (1.0, n));
}
void setP (ArpProcessor& p, const char* id, float plain) { if (auto* q = p.apvts.getParameter (id)) q->setValueNotifyingHost (q->convertTo0to1 (plain)); }

// ---------------------------------------------------------------- engine checks
void engineChecks (Counter& C)
{
    // determinism
    for (int mode = 0; mode < 3; ++mode)
    {
        Idea a, b; a.mode = b.mode = mode; a.seed = b.seed = 4242; applyModeDefaults (a); applyModeDefaults (b); newIdea (a); newIdea (b);
        bool same = a.seedUsed == b.seedUsed;
        for (int l = 0; l < kLanes && same; ++l) { const auto& x = a.lanes[l].notes; const auto& y = b.lanes[l].notes; same = x.size() == y.size(); for (size_t i = 0; same && i < x.size(); ++i) same = x[i].t == y[i].t && x[i].p == y[i].p && x[i].d == y[i].d; }
        C.ok (same, "determinism " + modeName (mode));
    }
    // modes keep to their scales
    int badScale = 0, sunPref = 0;
    for (int mode = 0; mode < 3; ++mode)
        for (int s = 1; s <= 30; ++s)
        {
            std::string pr; uint32_t used = 0; const Gen g = generateForMode (mode, "", (uint32_t) s * 101, pr, used);
            const auto& M = recipe().mode[mode]; if (std::find (M.scales.begin(), M.scales.end(), g.scale) == M.scales.end()) ++badScale;
            if (mode == Sunrise) { std::string t; for (size_t i = 0; i < g.prog.size(); ++i) t += (i ? " " : "") + g.prog[i].tok; if (std::find (M.progs.begin(), M.progs.end(), t) != M.progs.end()) ++sunPref; }
            // and it is exactly the generator's output for (prompt, seedUsed)
            const Gen g2 = generate (pr, used); if (g2.key != g.key || g2.scale != g.scale || g2.prog.size() != g.prog.size()) ++badScale;
        }
    C.ok (badScale == 0, "every mode keeps to its scales (" + std::to_string (badScale) + " off)");
    C.ok (sunPref >= 18, "SUNRISE mostly uses its preferred 4-chord loops (" + std::to_string (sunPref) + "/30)");
    // GROOVE 0 = perfectly even: every arp / melody note starts on its lane grid; an arp figure without rests fills every step
    int offGrid = 0, gaps = 0, barsChecked = 0;
    for (int mode = 0; mode < 3; ++mode)
        for (int s = 1; s <= 12; ++s)
        {
            Idea I; I.mode = mode; I.seed = (uint32_t) s * 977; applyModeDefaults (I); newIdea (I);
            for (int l : { LaneArp, LaneMelody }) { const int st = BAR / (l == LaneArp ? I.arpSpb : I.melSpb); for (auto& n : applyGroove (I.lanes[l].notes, 0.0, 16, 1)) if (n.t % st) ++offGrid; }
            bool rests = false; for (int i = 0; i < I.fig.len; ++i) rests |= I.fig.s[(size_t) i].rest;
            if (! rests)
            {
                const int st = BAR / I.arpSpb; std::set<int> ts; for (auto& n : I.lanes[LaneArp].notes) ts.insert (n.t);
                for (int t = 0; t < I.total; t += st) if (! ts.count (t)) ++gaps;
                barsChecked += I.total / BAR;
            }
        }
    C.ok (offGrid == 0 && gaps == 0, "GROOVE 0 even grid (" + std::to_string (barsChecked) + " arp bars, " + std::to_string (offGrid) + " off-grid, " + std::to_string (gaps) + " empty steps)");
    std::printf ("groove 0: %d arp bars checked, %d off-grid notes, %d empty steps\n", barsChecked, offGrid, gaps);
    {   // GROOVE > 0 swings the off-16ths (and nothing else)
        Idea I; I.mode = Midnight; I.seed = 5; applyModeDefaults (I); newIdea (I);
        const auto sw = applyGroove (I.lanes[LaneArp].notes, 1.0, 16, 3); int moved = 0, onBeatsMoved = 0;
        for (size_t i = 0; i < sw.size(); ++i) { if (sw[i].t != I.lanes[LaneArp].notes[i].t) { ++moved; if (I.lanes[LaneArp].notes[i].t % (BAR / 8) == 0) ++onBeatsMoved; } }
        C.ok (moved > 0 && onBeatsMoved == 0, "GROOVE 100 swings only the off-16ths");
    }
    // STEP / GLIDE / FATTEN
    {
        std::vector<Note> ns (3); for (int i = 0; i < 3; ++i) { ns[(size_t) i].t = i * 48; ns[(size_t) i].d = 20; ns[(size_t) i].p = 60 + 2 * i; ns[(size_t) i].v = 0.6f; }
        auto mid = laneMidiNotes (ns, BAR);
        C.ok (mid[0].d == 20 && mid[1].d == 20, "STEP: notes keep their own length (no overlap)");
        ns[0].l.type = 1; mid = laneMidiNotes (ns, BAR);
        C.ok (mid[0].d == 48 + (int) std::lround ((1 - ns[0].l.w) * 48 * 0.5) && mid[1].d == 20, "GLIDE: the note overlaps into the next one by the glide amount (legato, so mono synths slide)");
        ns[0].l.type = 2; ns[0].l.f = 1.0f;
        C.ok (std::abs (playVel (ns, 1) - std::min (1.0f, 0.6f * 1.7f)) < 1e-5f && std::abs (playVel (ns, 2) - 0.6f) < 1e-6f, "FATTEN: the next note plays louder (x(1+0.7f)), only that one");
        ns[0].l.type = 0; C.ok (std::abs (playVel (ns, 1) - 0.6f) < 1e-6f, "STEP: no velocity change");
    }
    // locks: tweaks leave locked bars alone; HARVEST copies arp bars into the melody
    {
        Idea I; I.mode = Sunrise; I.seed = 77; applyModeDefaults (I); newIdea (I);
        auto& M = I.lanes[LaneMelody]; M.barLocks[0] = true; const auto before = M.notes;
        EditCtx c = editCtx (I, LaneMelody);
        int changedLocked = 0, changedOther = 0;
        for (int k = 0; k < 6; ++k)
        {
            std::vector<Note> v;
            switch (k) { case 0: v = toolVary (M.notes, c, 11, 2); break; case 1: v = toolNewNotes (M.notes, c, 12); break; case 2: v = toolNewRhythm (M.notes, c, 13); break;
                         case 3: v = toolSimpler (M.notes, c); break; case 4: v = toolBusier (M.notes, c, 14); break; default: v = toolOctave (M.notes, c, 1); break; }
            std::vector<Note> a, b; for (auto& n : before) if (n.t < BAR) a.push_back (n); for (auto& n : v) if (n.t < BAR) b.push_back (n);
            bool same = a.size() == b.size(); for (size_t i = 0; same && i < a.size(); ++i) same = a[i].t == b[i].t && a[i].p == b[i].p && a[i].d == b[i].d;
            if (! same) ++changedLocked;
            std::vector<Note> a2, b2; for (auto& n : before) if (n.t >= BAR) a2.push_back (n); for (auto& n : v) if (n.t >= BAR) b2.push_back (n);
            bool same2 = a2.size() == b2.size(); for (size_t i = 0; same2 && i < a2.size(); ++i) same2 = a2[i].t == b2[i].t && a2[i].p == b2[i].p && a2[i].d == b2[i].d;
            if (! same2) ++changedOther;
        }
        C.ok (changedLocked == 0 && changedOther >= 5, "tools keep locked bars (" + std::to_string (changedLocked) + " broke) and change the rest (" + std::to_string (changedOther) + "/6)");
        // a locked note survives a brand-new take
        M.barLocks[0] = false; M.notes = before; M.notes[2].lock = true; const Note keep = M.notes[2];
        composeLane (I, LaneMelody, true);
        bool kept = false; for (auto& n : M.notes) if (n.t == keep.t && n.p == keep.p && n.lock) kept = true;
        C.ok (kept, "a locked note survives NEW (a fresh take)");
    }
    {   // figure tools change the figure and keep it in key
        Idea I; I.mode = Midnight; I.seed = 31; applyModeDefaults (I); newIdea (I);
        const auto P = arpParams (I); int outKey = 0, same = 0;
        for (int k = 0; k < 6; ++k)
        {
            Figure f = k == 0 ? figureVary (I.fig, P, 9, 1, I.H, I.total, I.pedalPc) : k == 1 ? figureVary (I.fig, P, 9, 2, I.H, I.total, I.pedalPc) : k == 2 ? figureNewNotes (I.fig, P, 9, I.H, I.total, I.pedalPc)
                     : k == 3 ? figureNewRhythm (I.fig, P, 9, I.H, I.total, I.pedalPc) : k == 4 ? figureSimpler (I.fig) : figureBusier (I.fig, P);
            bool eq = f.len == I.fig.len; for (int i = 0; eq && i < f.len; ++i) eq = f.s[(size_t) i].idx == I.fig.s[(size_t) i].idx && f.s[(size_t) i].pedal == I.fig.s[(size_t) i].pedal && f.s[(size_t) i].oct == I.fig.s[(size_t) i].oct && f.s[(size_t) i].rest == I.fig.s[(size_t) i].rest;
            if (eq) ++same;
            for (auto& n : realizeArp (f, I.H, P, I.total, I.pedalPc)) if (! I.H.scaleTone (n.p, n.t)) ++outKey;
        }
        C.ok (same == 0 && outKey == 0, "figure VARY / NEW NOTES / NEW RHYTHM / SIMPLER / BUSIER change the figure, notes stay in key (" + std::to_string (outKey) + " out)");
    }
    {   // every lane note of 40 ideas per mode is in key / on a chord tone or the chord scale
        int out = 0, tot = 0;
        for (int mode = 0; mode < 3; ++mode) for (int s = 1; s <= 40; ++s)
        {
            Idea I; I.mode = mode; I.seed = (uint32_t) (s * 37 + 1); applyModeDefaults (I); newIdea (I);
            for (int l : { LaneArp, LaneMelody, LaneBass }) for (auto& n : I.lanes[l].notes) { ++tot; if (! I.H.scaleTone (n.p, n.t) && ! std::count (I.H.keyPcs.begin(), I.H.keyPcs.end(), mod12 (n.p)))
                { ++out; if (out <= 6) std::printf ("  out of key: %s seed %u lane %d pitch %d t %d chord %s (%s %s)\n", modeName (mode).c_str(), I.seedUsed, l, n.p, n.t, I.H.at (n.t).name.c_str(), keyName (I.g.key, I.g.scale).c_str(), I.g.scale.c_str()); } }
        }
        C.ok (out == 0, "120 ideas: every note in key (" + std::to_string (tot) + " notes, " + std::to_string (out) + " out)");
    }
}

// ---------------------------------------------------------------- SIMPLE / RICH chords
// every chord a plain major / minor triad (POWER voicing: root + 5th), bass = root, names + numerals match the notes
int badSimpleChords (const Idea& I, std::string* why = nullptr)
{
    int bad = 0; const std::string sc = chordScale (I);
    for (size_t i = 0; i < I.chords.size(); ++i)
    {
        const auto& c = I.chords[i]; const auto& h = I.H.chords[i];
        const bool maj = c.iv == std::vector<int> { 0, 4, 7 }, mnr = c.iv == std::vector<int> { 0, 3, 7 };
        const int rootPc = mod12 (I.g.key + c.root);
        std::set<int> want { rootPc, mod12 (rootPc + (maj ? 4 : 3)), mod12 (rootPc + 7) }, got (c.pcs.begin(), c.pcs.end()), hv;
        for (int p : h.voicing) hv.insert (mod12 (p));
        const std::set<int> power { rootPc, mod12 (rootPc + 7) };
        const std::string name = noteName (rootPc, I.g.key, sc) + (mnr ? "m" : "");
        std::string rn = c.roman; while (! rn.empty() && (rn[0] == 'b' || rn[0] == '#')) rn = rn.substr (1);
        const bool romanOk = ! rn.empty() && (mnr ? std::islower ((unsigned char) rn[0]) : std::isupper ((unsigned char) rn[0])) && rn.find_first_not_of ("IViv") == std::string::npos;
        std::string err;
        if (! maj && ! mnr) err = "not a triad";
        else if (c.bass || mod12 (c.bassNote) != rootPc) err = "bass is not the root";
        else if (got != want) err = "notes";
        else if (I.voicing == 1 ? hv != power : hv != want) err = "voicing";
        else if (c.name != name || (I.voicing == 1 ? h.name != noteName (rootPc, I.g.key, sc) + "5" : h.name != name)) err = "name " + c.name + " / " + h.name + " vs " + name;
        else if (! romanOk) err = "numeral " + c.roman;
        if (! err.empty()) { ++bad; if (why && why->empty()) *why = c.name + " " + c.roman + ": " + err; }
    }
    return bad;
}
bool lanesPass (const Idea& I)
{
    const int lane = I.mode == Midnight ? LaneArp : LaneMelody; const auto& ns = I.lanes[lane].notes;
    if (lane == LaneArp) return arpHardRules (ns, I.fig.len, I.total);
    Harmony Hs = I.H; Hs.offset = I.stagger;
    std::vector<Note> ph = ns; if (I.stagger) { for (auto& x : ph) x.t = ((x.t - I.stagger) % I.total + I.total) % I.total; sortNotes (ph); }
    return melodyHardRules (ph, Hs, I.total, I.mode, false, 0, 0);
}
std::string loopOf (const std::vector<Slot>& pr) { std::string t; for (size_t i = 0; i < pr.size() && i < 4; ++i) t += (i ? " " : "") + pr[i].tok; return t; }

void simpleChecks (Counter& C)
{
    // 200 fresh ideas per mood in SIMPLE (also through NEW CHORDS, VARY, tap-a-chord, the voicings)
    int badCh = 0, badLoop = 0, ideas = 0, passS = 0, passR = 0, toolBad = 0, offKey = 0, notes = 0; std::string why;
    for (int mode = 0; mode < 3; ++mode)
        for (int i = 0; i < 200; ++i)
        {
            Idea I; I.mode = mode; I.seed = (uint32_t) (11 + i * 53); applyModeDefaults (I); I.voicing = i % 7 == 3 ? 1 : I.voicing;
            if (i == 0 && mode == 0) C.ok (I.simple, "SIMPLE is the default");
            newIdea (I); ++ideas;
            badCh += badSimpleChords (I, &why);
            bool inTable = false; for (auto& L : classicLoops (mode, I.g.scale)) inTable |= L.first == loopOf (I.simpleProg);
            for (size_t k = 4; k < I.simpleProg.size(); ++k) inTable &= I.simpleProg[k].tok == I.simpleProg[k % 4].tok;
            if (! inTable || I.simpleProg.empty()) { ++badLoop; if (badLoop <= 3) std::printf ("  not a classic loop: %s %s [%s]\n", modeName (mode).c_str(), I.g.scale.c_str(), loopOf (I.simpleProg).c_str()); }
            if (lanesPass (I)) ++passS;
            for (int l : { LaneArp, LaneMelody, LaneBass }) for (auto& n : I.lanes[l].notes) { ++notes; if (! I.H.scaleTone (n.p, n.t) && ! std::count (I.H.keyPcs.begin(), I.H.keyPcs.end(), mod12 (n.p))) ++offKey; }
            { Idea R = I; setSimple (R, false); if (lanesPass (R)) ++passR; }
            if (i % 10 == 0)
            {   // the chord tools stay simple
                Idea T = I;
                pickSimpleProg (T, (uint32_t) i * 7 + 1); rebuildChords (T); refitLanesToChords (T); toolBad += badSimpleChords (T, &why);   // NEW CHORDS
                const auto before = loopOf (T.simpleProg); pickSimpleProg (T, (uint32_t) i * 13 + 5, true); rebuildChords (T); refitLanesToChords (T);   // VARY
                toolBad += badSimpleChords (T, &why);
                if (loopOf (T.simpleProg) == before && classicLoops (mode, T.g.scale).size() > 1) { ++toolBad; std::printf ("  VARY kept the loop %s\n", before.c_str()); }
                for (int k = 0; k < 4; ++k) { for (int s = 0; s < (int) T.simpleProg.size(); ++s) nextAlt (T, s); toolBad += badSimpleChords (T, &why); }   // tap-a-chord
                for (int v = 0; v < 4; ++v) { T.voicing = v; rebuildChords (T); toolBad += badSimpleChords (T, &why); }
                if (! lanesPass (T)) { ++toolBad; if (toolBad < 4) std::printf ("  tools: lanes fail hard rules (%s seed %u)\n", modeName (mode).c_str(), T.seedUsed); }
            }
        }
    if (! why.empty()) std::printf ("  first bad SIMPLE chord: %s\n", why.c_str());
    std::printf ("SIMPLE: %d ideas, hard rules pass %d (RICH, same ideas: %d), notes off key %d/%d\n", ideas, passS, passR, offKey, notes);
    C.ok (badCh == 0, "SIMPLE: every chord a major / minor triad, bass = root, names + numerals match (" + std::to_string (ideas) + " ideas, " + std::to_string (badCh) + " bad)");
    C.ok (badLoop == 0, "SIMPLE: every progression is a classic loop of its mood (" + std::to_string (badLoop) + " off)");
    C.ok (passS >= passR - ideas / 100 && offKey == 0, "SIMPLE: lanes re-fit and pass the hard rules (" + std::to_string (passS) + "/" + std::to_string (ideas) + ", RICH " + std::to_string (passR) + "), every note in key");
    C.ok (toolBad == 0, "SIMPLE: NEW CHORDS / VARY / tap-a-chord / voicings stay simple (" + std::to_string (toolBad) + " bad)");
    // RICH = today's generator exactly: the chords are resolveProg of the generator's slots with the old settings
    {
        int diff = 0;
        for (int mode = 0; mode < 3; ++mode) for (int i = 0; i < 40; ++i)
        {
            Idea I; I.mode = mode; I.seed = (uint32_t) (7 + i * 31); applyModeDefaults (I); I.simple = false; newIdea (I);
            ProgState ps; ps.key = I.g.key; ps.scale = I.g.scale; ps.colour = I.g.colour; ps.pedal = I.g.pedal; ps.prog = I.g.prog;
            for (auto& sl : ps.prog) sl.len = (sl.len ? sl.len : 4) * std::max (1, I.chordBars);
            const auto ref = resolveProg (ps);
            if (ref.size() != I.chords.size()) { ++diff; continue; }
            for (size_t k = 0; k < ref.size(); ++k) if (ref[k].name != I.chords[k].name || ref[k].voicing != I.chords[k].voicing || ref[k].bassNote != I.chords[k].bassNote) { ++diff; break; }
            Idea S = I; setSimple (S, true); setSimple (S, false);   // the switch flips back to the same chords
            for (size_t k = 0; k < ref.size(); ++k) if (ref[k].name != S.chords[k].name) { ++diff; break; }
        }
        C.ok (diff == 0, "RICH: chords exactly the generator's (as before the switch), SIMPLE -> RICH restores them (" + std::to_string (diff) + " differ)");
    }
    // the starters, SIMPLE: simplified chord by chord (RICH keeps them as written)
    {
        ArpProcessor p; const auto names = p.starterNames(); int bad = 0, lanesBad = 0, richSame = 0; std::string w;
        for (int i = 0; i < names.size(); ++i)
        {
            p.change ([] (Idea& X) { X.simple = true; }, false); p.loadStarter (i);
            bad += badSimpleChords (p.idea(), &w); if (! lanesPass (p.idea()) && p.idea().mode == Midnight) { ++lanesBad; std::string y; arpHardRules (p.idea().lanes[LaneArp].notes, p.idea().fig.len, p.idea().total, &y); std::printf ("  starter %s: arp %s\n", names[i].toRawUTF8(), y.c_str()); }
            if (! p.idea().simple) ++bad;
            p.change ([] (Idea& X) { X.simple = false; }, false); p.loadStarter (i);
            bool hasColour = false; for (auto& c : p.idea().chords) hasColour |= c.iv.size() != 3 || c.bass.has_value() || triadQuality (c.iv) == "sus";
            if (hasColour || badSimpleChords (p.idea()) > 0) ++richSame;
        }
        if (! w.empty()) std::printf ("  first bad SIMPLE starter chord: %s\n", w.c_str());
        C.ok (names.size() == 29 && bad == 0 && lanesBad == 0, "SIMPLE: all " + std::to_string (names.size()) + " starters simplified to triads, bass = root, names match, arps pass (" + std::to_string (bad) + " bad)");
        C.ok (richSame > 0, "RICH: starters keep their own colourful chords (" + std::to_string (richSame) + " with 7ths / sus / add9)");
    }
    // LISTEN, SIMPLE: jazzy heard chords become plain triads
    {
        using listen::HeardChord;
        auto hc = [] (int root, const char* type, std::vector<int> iv) { HeardChord h; h.root = root; h.type = type; h.iv = std::move (iv); h.name = type; return h; };
        const std::vector<std::pair<std::string, std::vector<HeardChord>>> cases {
            { "minor", { hc (9, "m7", { 0, 3, 7, 10 }), hc (5, "maj7", { 0, 4, 7, 11 }), hc (0, "add9", { 0, 4, 7, 2 }), hc (7, "sus4", { 0, 5, 7 }) } },
            { "minor", { hc (9, "m", { 0, 3, 7 }), hc (11, "dim", { 0, 3, 6 }), hc (4, "7", { 0, 4, 7, 10 }), hc (1, "", { 0, 4, 7 }) } },
            { "major", { hc (0, "maj7", { 0, 4, 7, 11 }), hc (7, "sus2", { 0, 2, 7 }), hc (9, "m7", { 0, 3, 7, 10 }), hc (6, "m7", { 0, 3, 7, 10 }) } } };
        int bad = 0, lanesBad = 0, n = 0; std::string w;
        for (auto& cs : cases)
            for (int simple = 1; simple >= 0; --simple)
            {
                ArpProcessor p; listen::Result r; r.ok = true; r.bpm = 122; r.bpmInt = 122; r.key = cs.first == "minor" ? 9 : 0; r.scale = cs.first; r.chords = cs.second;
                p.lastHeard = r; p.change ([simple] (Idea& X) { X.simple = simple != 0; }, false); p.applyHeard (77, false);
                if (! simple) continue;
                ++n; bad += badSimpleChords (p.idea(), &w); if (p.idea().chords.size() != 4) ++bad; if (! lanesPass (p.idea())) ++lanesBad;
                std::string nm; for (auto& c : p.idea().chords) nm += c.name + " "; std::printf ("  LISTEN SIMPLE %s: %s\n", cs.first.c_str(), nm.c_str());
            }
        if (! w.empty()) std::printf ("  first bad SIMPLE LISTEN chord: %s\n", w.c_str());
        C.ok (bad == 0 && lanesBad == 0, "SIMPLE: LISTEN results become plain triads, lanes pass (" + std::to_string (n) + " cases, " + std::to_string (bad) + " bad)");
    }
    // the switch is saved with the idea
    {
        ArpProcessor p; p.change ([] (Idea& X) { X.mode = Dusk; X.seed = 99; applyModeDefaults (X); newIdea (X); setSimple (X, false); });
        juce::MemoryBlock mb; p.getStateInformation (mb); ArpProcessor q; q.setStateInformation (mb.getData(), (int) mb.getSize());
        bool ok = ! q.idea().simple && q.idea().chords.size() == p.idea().chords.size();
        for (size_t i = 0; ok && i < p.idea().chords.size(); ++i) ok = q.idea().chords[i].name == p.idea().chords[i].name;
        q.change ([] (Idea& X) { setSimple (X, true); }); juce::MemoryBlock mb2; q.getStateInformation (mb2); ArpProcessor r2; r2.setStateInformation (mb2.getData(), (int) mb2.getSize());
        ok = ok && r2.idea().simple && loopOf (r2.idea().simpleProg) == loopOf (q.idea().simpleProg) && badSimpleChords (r2.idea()) == 0;
        C.ok (ok, "SIMPLE / RICH saved in the plugin state (both ways)");
    }
}

// ---------------------------------------------------------------- undo / redo, per-mode memory, save / load
void sessionChecks (Counter& C)
{
    {   // 10 mixed edits, undone and redone: byte-identical states
        ArpProcessor p; p.change ([] (Idea& X) { X.mode = Midnight; X.seed = 321; applyModeDefaults (X); newIdea (X); }, false);
        std::vector<juce::String> st { p.sessionString() };
        auto step = [&] { st.push_back (p.sessionString()); };
        p.change ([] (Idea& X) { Note n; n.t = 5 * 24; n.p = 70; X.lanes[LaneArp].notes.push_back (n); sortNotes (X.lanes[LaneArp].notes); X.lanes[LaneArp].edited = true; }, true, "added note"); step();
        p.change ([] (Idea& X) { X.lanes[LaneArp].notes[2].p += 2; }, true, "moved note"); step();
        p.change ([] (Idea& X) { X.lanes[LaneArp].notes[4].l.type = 1; X.lanes[LaneArp].notes[4].l.w = 0.3f; }, true, "glide"); step();
        p.change ([] (Idea& X) { X.lanes[LaneArp].barLocks[1] = true; }, true, "lock bar"); step();
        p.change ([] (Idea& X) { nextAlt (X, 1); }, true, "chord"); step();
        p.change ([] (Idea& X) { setSimple (X, false); }, true, "rich"); step();
        p.pushUndoPoint ("cutoff"); setP (p, "cutoff", 0.21f); step();
        p.laneTool (LaneMelody, "varyLittle", nullptr); step();
        p.switchMode (Sunrise); step();
        p.generate (Sunrise, true); step();
        bool ok = st.size() == 11;
        for (int k = 10; k >= 1 && ok; --k) { p.undo(); ok = p.sessionString() == st[(size_t) k - 1]; if (! ok) std::printf ("  undo step %d differs\n", k); }
        for (int k = 1; k <= 10 && ok; ++k) { p.redo(); ok = p.sessionString() == st[(size_t) k]; if (! ok) std::printf ("  redo step %d differs\n", k); }
        C.ok (ok, "UNDO / REDO: 10 mixed edits (notes, line, lock, chord, SIMPLE/RICH, knob, VARY, mode, GENERATE) undone + redone byte-identical");
        int n = 0; while (p.canUndo() && n < 200) { p.undo(); ++n; }
        C.ok (n >= 10 && n <= ArpProcessor::kUndoSteps, "undo keeps " + std::to_string (n) + " steps (limit " + std::to_string (ArpProcessor::kUndoSteps) + ")");
    }
    {   // every mode keeps its own idea
        ArpProcessor p; p.change ([] (Idea& X) { X.mode = Midnight; X.seed = 77; applyModeDefaults (X); newIdea (X); }, false);
        std::map<int, std::string> mine; bool ok = true;
        auto ideaJson = [&] { return juce::JSON::toString (ArpProcessor::ideaToVar (p.idea()), true).toStdString(); };
        for (int round = 0; round < 5; ++round)
            for (int m : { Sunrise, Dusk, Midnight })
            {
                p.switchMode (m);
                if (mine.count (m) && mine[m] != ideaJson()) { ok = false; std::printf ("  %s changed while away (round %d)\n", modeName (m).c_str(), round); }
                p.change ([round] (Idea& X) { auto& ns = X.lanes[LaneMelody].notes; if (! ns.empty()) ns[(size_t) round % ns.size()].v = 0.3f + 0.1f * (float) round; X.lanes[LaneMelody].edited = true; }, true, "edit");
                setP (p, "pump", 0.1f * (float) (m + 1));
                mine[m] = ideaJson();
            }
        juce::MemoryBlock mb; p.getStateInformation (mb); ArpProcessor q; q.setStateInformation (mb.getData(), (int) mb.getSize());
        bool all = true; for (int m : { Sunrise, Dusk, Midnight }) { q.switchMode (m); all &= juce::JSON::toString (ArpProcessor::ideaToVar (q.idea()), true).toStdString() == mine[m]; }
        C.ok (ok, "per-mode memory: 5x round the modes with edits, each mode's idea comes back byte-identical");
        C.ok (all, "per-mode memory: all three ideas saved in the plugin state");
    }
    {   // SAVE / LOAD round trip
        ArpProcessor p; p.change ([] (Idea& X) { X.mode = Dusk; X.seed = 4040; applyModeDefaults (X); newIdea (X); X.lanes[LaneMelody].notes[1].l.type = 2; }, false);
        const auto name = "zz test " + juce::String (juce::Random::getSystemRandom().nextInt (1 << 20));
        const bool saved = p.savePreset (name); const auto f = ArpProcessor::presetDir().getChildFile (name + ".arpz");
        const auto want = juce::JSON::toString (ArpProcessor::ideaToVar (p.idea()), true);
        ArpProcessor q; const bool loaded = q.loadPreset (f);
        const bool same = juce::JSON::toString (ArpProcessor::ideaToVar (q.idea()), true) == want;
        const bool second = p.savePreset (name) && ArpProcessor::presetDir().getChildFile (name + "(2).arpz").existsAsFile();
        std::printf ("  save/load: saved %d loaded %d same %d second %d\n", (int) saved, (int) loaded, (int) same, (int) second);
        if (! same) { const auto got = juce::JSON::toString (ArpProcessor::ideaToVar (q.idea()), true); int k = 0; while (k < got.length() && got[k] == want[k]) ++k; std::printf ("  first diff at %d: [%s] vs [%s]\n", k, want.substring (k - 40, k + 60).toRawUTF8(), got.substring (k - 40, k + 60).toRawUTF8()); }
        f.deleteFile(); ArpProcessor::presetDir().getChildFile (name + "(2).arpz").deleteFile();
        C.ok (saved && loaded && same && second, "SAVE / LOAD: round trip identical, a taken name never overwrites");
    }
}

// ---------------------------------------------------------------- the roll: zoom, locks, lines (through the real editor component)
juce::MouseEvent mev (juce::Component& c, juce::Point<float> pos, juce::Point<float> down, juce::ModifierKeys mods, bool dragged = false)
{
    return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, mods, juce::MouseInputSource::defaultPressure, 0, 0, 0, 0, &c, &c,
                             juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, dragged);
}
void rollChecks (Counter& C)
{
    {   // zoom clamps
        const int total = 8 * BAR; bool ok = true;
        RollView v; v = zoomAt (v, 0.5, 0.001, total); ok &= std::abs (v.len - kMinViewLen) < 1e-9;
        v = zoomAt (v, 0.5, 1000, total); ok &= std::abs (v.len - maxViewLen (total)) < 1e-9 && v.start == 0;
        v = clampView ({ 7 * BAR, 4 * BAR }, total); ok &= v.start == 4 * BAR;
        v = clampView ({ -50, 2 * BAR }, total); ok &= v.start == 0;
        RollView z { 2 * BAR, 4 * BAR }; const double tUnder = z.start + 0.25 * z.len; z = zoomAt (z, 0.25, 0.5, total); ok &= std::abs ((z.start + 0.25 * z.len) - tUnder) < 1e-6;
        v = panBy ({ 0, 4 * BAR }, 100 * BAR, total); ok &= v.start == 4 * BAR;
        C.ok (ok, "roll zoom: clamped to 1/2 bar .. the whole idea, zoom keeps the tick under the pointer, pan clamps");
    }
    {   // LOCK SEL toggles both ways
        Idea I; I.mode = Sunrise; I.seed = 9; applyModeDefaults (I); newIdea (I);
        const auto before = I.lanes[LaneMelody].barLocks; std::vector<bool> sel (I.lanes[LaneMelody].notes.size(), false); sel[0] = sel[2] = true;
        const bool a = toggleLocks (I, LaneMelody, sel, -1, -1); const bool locked = I.lanes[LaneMelody].notes[0].lock && I.lanes[LaneMelody].notes[2].lock;
        const bool b = toggleLocks (I, LaneMelody, sel, -1, -1); const bool unlocked = ! I.lanes[LaneMelody].notes[0].lock && ! I.lanes[LaneMelody].notes[2].lock;
        const bool c = toggleLocks (I, LaneMelody, {}, 1, 2); const bool bars = I.lanes[LaneMelody].barLocks[1] && I.lanes[LaneMelody].barLocks[2];
        const bool d = toggleLocks (I, LaneMelody, {}, 2, 2); unlockAll (I, LaneMelody);
        C.ok (a && locked && ! b && unlocked && c && bars && ! d && I.lanes[LaneMelody].barLocks == before, "LOCK SEL toggles (notes + bars) and UNLOCK ALL clears everything");
    }
    ArpProcessor p; p.change ([] (Idea& X) { X.mode = Midnight; X.seed = 515; applyModeDefaults (X); newIdea (X); }, false);
    std::unique_ptr<juce::AudioProcessorEditor> pe (p.createEditor()); auto* ed = dynamic_cast<ArpEditor*> (pe.get());
    if (ed == nullptr) { C.ok (false, "editor"); return; }
    ed->setSize (1240, 820); ed->selectLane (LaneArp);
    auto& roll = ed->roll; const auto notes = p.idea().lanes[LaneArp].notes; roll.fitRows (notes);
    {   // the lines are clickable at the default zoom (4 bars)
        int vis = 0, hit = 0;
        for (int i = 0; i + 1 < (int) notes.size(); ++i)
        {
            if (notes[(size_t) i + 1].t >= roll.view.start + roll.view.len) break;
            const auto pts = roll.linkPath (notes, i); size_t best = 1; float bl = -1;
            for (size_t k = 1; k < pts.size(); ++k) { const float l = pts[k].getDistanceFrom (pts[k - 1]); if (l > bl) { bl = l; best = k; } }
            const auto m = (pts[best - 1] + pts[best]) * 0.5f; ++vis;
            if (roll.hitNote (notes, m.x, m.y) < 0 && roll.hitLink (notes, m.x, m.y) == i) ++hit;
            else if (roll.hitNote (notes, m.x, m.y) >= 0) --vis;   // the line runs under a note there (overlapping notes)
        }
        std::printf ("roll at the default zoom (%d bars, %.1f px per 16th): %d / %d lines hit on their line\n", roll.viewBars(), (roll.getWidth() - arpui::Roll::kx) / (roll.view.len / 24.0), hit, vis);
        C.ok (vis > 20 && hit * 100 >= vis * 95, "lines between notes are clickable at the default zoom (" + std::to_string (hit) + "/" + std::to_string (vis) + ")");
    }
    {   // click a GLIDE line: selected, still GLIDE; drag its dot: only w changes; FATTEN ball: only f; STEP: no amount
        const int li = 6;
        p.change ([li] (Idea& X) { auto& l = X.lanes[LaneArp].notes[(size_t) li].l; l.type = 1; l.w = 0.5f; l.f = 0.4f; X.lanes[LaneArp].edited = true; }, false);
        auto ns = p.idea().lanes[LaneArp].notes; const auto pts = roll.linkPath (ns, li); const auto mid = pts.back() * 0.5f + pts[pts.size() / 2] * 0.5f;
        const juce::ModifierKeys left (juce::ModifierKeys::leftButtonModifier);
        auto click = [&] (juce::Point<float> at) { roll.mouseDown (mev (roll, at, at, left)); roll.mouseUp (mev (roll, at, at, juce::ModifierKeys())); };
        click (mid); click (mid);   // clicking it again must not change the type
        bool ok1 = roll.selLink == li && p.idea().lanes[LaneArp].notes[(size_t) li].l.type == 1;
        ns = p.idea().lanes[LaneArp].notes; const auto h = roll.linkHandle (ns, li); const Link before = ns[(size_t) li].l;
        const auto to = h + juce::Point<float> (14, 6);
        roll.mouseDown (mev (roll, h, h, left)); roll.mouseDrag (mev (roll, to, h, left, true)); roll.mouseUp (mev (roll, to, h, juce::ModifierKeys(), true));
        const Link after = p.idea().lanes[LaneArp].notes[(size_t) li].l;
        const bool ok2 = after.type == 1 && after.w > before.w + 0.05f && after.f == before.f;
        p.change ([li] (Idea& X) { X.lanes[LaneArp].notes[(size_t) li].l.type = 2; }, false);
        ns = p.idea().lanes[LaneArp].notes; const auto h2 = roll.linkHandle (ns, li); const Link b2 = ns[(size_t) li].l; const auto up = h2 + juce::Point<float> (5, -21);
        roll.mouseDown (mev (roll, h2, h2, left)); roll.mouseDrag (mev (roll, up, h2, left, true)); roll.mouseUp (mev (roll, up, h2, juce::ModifierKeys(), true));
        const Link a2 = p.idea().lanes[LaneArp].notes[(size_t) li].l;
        const bool ok3 = a2.type == 2 && a2.f > b2.f + 0.2f && a2.w == b2.w;
        Link st; st.type = 0; const Link s1 = glideDrag (st, 100, 0, 48), s2 = fattenDrag (st, 0.5f, -30);
        const bool ok4 = ! linkHasAmount (st) && s1.type == 0 && s1.w == st.w && s2.f == st.f;
        C.ok (ok1, "click a GLIDE line: it is selected and stays GLIDE (clicking again too)");
        C.ok (ok2, "drag the GLIDE dot: only the glide start (w) changes");
        C.ok (ok3, "drag the FATTEN ball: only the amount (f) changes");
        C.ok (ok4, "STEP has no amount (handle drags leave it alone)");
    }
    {   // a muted lane shows the red bar; UNMUTE unmutes, undo mutes it again
        juce::Image img (juce::Image::ARGB, roll.getWidth(), roll.getHeight(), true);
        auto paintRoll = [&] { juce::Graphics gg (img); roll.paint (gg); };
        p.change ([] (Idea& X) { X.lanes[LaneArp].mute = false; }, false); paintRoll(); const bool off1 = roll.bannerUnmute.isEmpty();
        p.change ([] (Idea& X) { X.lanes[LaneArp].mute = true; }, false); paintRoll(); const bool on1 = ! roll.bannerUnmute.isEmpty();
        const auto at = roll.bannerUnmute.getCentre();
        roll.mouseDown (mev (roll, at, at, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier))); roll.mouseUp (mev (roll, at, at, juce::ModifierKeys()));
        const bool unm = ! p.idea().lanes[LaneArp].mute; paintRoll(); const bool off2 = roll.bannerUnmute.isEmpty();
        p.undo(); const bool back = p.idea().lanes[LaneArp].mute;
        p.change ([] (Idea& X) { X.lanes[LaneArp].mute = false; }, false);
        C.ok (off1 && on1 && unm && off2 && back, "muted lane: red bar shown only when the roll's lane is muted, UNMUTE unmutes, undo restores");
    }
    {   // the keys play the preview synth; undo / redo labels reach the editor
        roll.mouseDown (mev (roll, { 10, roll.Y (64) + 3 }, { 10, roll.Y (64) + 3 }, juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier)));
        const int a = roll.audPitch; roll.mouseUp (mev (roll, { 10, roll.Y (64) + 3 }, { 10, roll.Y (64) + 3 }, juce::ModifierKeys()));
        C.ok (a == 64 && roll.audPitch < 0, "piano keys audition the pitch (note-off on release)");
    }
}

// ---------------------------------------------------------------- drag-out: .mid on channel 1 without overlaps, audio mix / stems
void exportChecks (Counter& C)
{
    int files = 0, overlaps = 0, unmatched = 0, offCh1 = 0, notesN = 0;
    for (int i = 0; i < 50; ++i)
    {
        Idea I; I.mode = i % 3; I.seed = (uint32_t) (900 + i * 61); applyModeDefaults (I); newIdea (I);
        auto& A = I.lanes[LaneArp].notes; for (size_t k = 0; k < A.size(); k += 3) { A[k].l.type = 1 + (int) (k / 3) % 2; A[k].l.w = 0.2f; }
        if (i % 2 == 0) { auto& M = I.lanes[LaneMelody].notes; M.insert (M.end(), A.begin(), A.end()); sortNotes (M); }   // harvested over itself
        for (int l = 0; l < kLanes; ++l) I.lanes[l].mute = false;
        for (bool one : { false, true })
        {
            const auto bytes = ideaMidi (I, 15, one, 124); juce::MemoryInputStream ms (bytes.data(), bytes.size(), false); juce::MidiFile mf; if (! mf.readFrom (ms)) { ++unmatched; continue; }
            ++files; std::map<int, int> on;   // channel*128+pitch -> sounding count (across tracks: one channel)
            std::vector<std::tuple<double, int, int, int>> ev;   // t, on(1)/off(0), ch, p
            for (int t = 0; t < mf.getNumTracks(); ++t) for (auto* e : *mf.getTrack (t)) { const auto& m = e->message; if (m.isNoteOn()) ev.push_back ({ m.getTimeStamp(), 1, m.getChannel(), m.getNoteNumber() }); else if (m.isNoteOff()) ev.push_back ({ m.getTimeStamp(), 0, m.getChannel(), m.getNoteNumber() }); }
            std::stable_sort (ev.begin(), ev.end(), [] (const auto& a, const auto& b) { return std::get<0> (a) != std::get<0> (b) ? std::get<0> (a) < std::get<0> (b) : std::get<1> (a) < std::get<1> (b); });
            for (auto& e : ev) { const int k = std::get<2> (e) * 128 + std::get<3> (e); if (std::get<2> (e) != 1) ++offCh1;
                if (std::get<1> (e)) { ++notesN; if (on[k] > 0) ++overlaps; on[k]++; } else { if (on[k] <= 0) ++unmatched; else on[k]--; } }
            for (auto& kv : on) if (kv.second) ++unmatched;
        }
    }
    std::printf ("drag-out .mid: %d files, %d notes, %d same-pitch overlaps, %d unmatched, %d off channel 1\n", files, notesN, overlaps, unmatched, offCh1);
    C.ok (files == 100 && overlaps == 0 && unmatched == 0 && offCh1 == 0, "drag-out .mid (50 ideas, glides / fatten / harvest): all channel 1, no same-pitch overlaps, every note-on matched");
    {   // expression: GLIDE -> legato overlap + a bend ramp back to centre; FATTEN -> velocity jump + CC11 swell; velocities with shape
        Idea I; I.mode = Sunrise; I.seed = 77; applyModeDefaults (I); newIdea (I); for (auto& L : I.lanes) L.mute = false; I.groove = 0;
        auto& M = I.lanes[LaneMelody].notes; size_t g = 0; while (g + 2 < M.size() && (M[g].p == M[g + 1].p || M[g + 1].t - M[g].t < 24)) ++g;
        M[g].l.type = 1; M[g].l.w = 0.4f; const size_t f = g + 1 < M.size() - 1 ? g + 1 : g; M[f].l.type = 2; M[f].l.f = 0.8f;
        const Note a = M[g], b = M[g + 1]; const float baseV = M[f + 1].v;
        const auto bytes = ideaMidi (I, 1 << LaneMelody, false, 124);
        double aEnd = -1; int bends = 0, bendMax = 8192, resetAtB = 0, cc11 = 0, vAfter = -1; std::set<int> vels;
        juce::MidiFile raw; { juce::MemoryInputStream m2 (bytes.data(), bytes.size(), false); raw.readFrom (m2); }
        const auto* rt = raw.getTrack (1);
        for (int i = 0; i < rt->getNumEvents(); ++i)
        {
            const auto& m = rt->getEventPointer (i)->message; const double t = m.getTimeStamp();
            if (m.isNoteOn()) { vels.insert (m.getVelocity()); if ((int) t == M[f + 1].t && m.getNoteNumber() == M[f + 1].p) vAfter = m.getVelocity(); }
            if (m.isNoteOff() && m.getNoteNumber() == a.p && t > a.t && aEnd < 0) aEnd = t;
            if (m.isPitchWheel()) { if (t >= a.t && t < b.t) { ++bends; bendMax = b.p > a.p ? juce::jmax (bendMax, m.getPitchWheelValue()) : juce::jmin (bendMax, m.getPitchWheelValue()); } if ((int) t == b.t && m.getPitchWheelValue() == 8192) ++resetAtB; }
            if (m.isController() && m.getControllerNumber() == 11) ++cc11;
        }
        const int base127 = (int) std::lround (baseV * 127);
        std::printf ("MIDI expression: glide %d->%d overlap %.0f ticks, %d bend events (peak %d), reset %d; fatten vel %d (was %d), CC11 %d; %zu distinct melody velocities\n",
                     a.p, b.p, aEnd - b.t, bends, bendMax, resetAtB, vAfter, base127, cc11, vels.size());
        C.ok (aEnd > b.t && bends >= 6 && bendMax != 8192 && resetAtB >= 1, "MIDI: GLIDE = overlapping legato notes + a pitch-bend ramp, back to centre on the next note");
        C.ok (vAfter >= base127 + 20 && cc11 >= 6, "MIDI: FATTEN = the next note's velocity jumps (+" + std::to_string (vAfter - base127) + ") + a CC11 swell");
        std::set<int> cv; { const auto cb = ideaMidi (I, 1 << LaneChords, false, 124); juce::MemoryInputStream m3 (cb.data(), cb.size(), false); juce::MidiFile c3; c3.readFrom (m3);
            for (int t = 0; t < c3.getNumTracks(); ++t) for (auto* e : *c3.getTrack (t)) if (e->message.isNoteOn()) cv.insert (e->message.getVelocity()); }
        C.ok (vels.size() >= 4 && cv.size() >= 3, "MIDI: velocities carry the shape (melody " + std::to_string (vels.size()) + ", chords " + std::to_string (cv.size()) + " different values)");
    }
    // audio: mix vs stems vs the live preview
    ArpProcessor p; p.change ([] (Idea& X) { X.mode = Sunrise; X.seed = 31; applyModeDefaults (X); newIdea (X); for (auto& L : X.lanes) L.mute = false; }, false);
    setP (p, "kick", 1); setP (p, "sound", 1);
    const auto job = p.renderJob (44100, 124);
    const auto mix = ArpProcessor::renderOffline (job, -1);
    std::vector<juce::AudioBuffer<float>> stems; for (int m : { 1, 2, 4, 8, 16 }) stems.push_back (ArpProcessor::renderOffline (job, m));
    bool aligned = true, nan = false; for (auto& s : stems) aligned &= s.getNumSamples() == mix.getNumSamples();
    auto corr = [] (const float* a, const float* b, int n) { double ab = 0, aa = 0, bb = 0; for (int i = 0; i < n; ++i) { ab += a[i] * b[i]; aa += a[i] * a[i]; bb += b[i] * b[i]; } return ab / std::sqrt (aa * bb + 1e-12); };
    std::vector<float> sum ((size_t) mix.getNumSamples(), 0.0f);
    for (auto& s : stems) for (int i = 0; i < s.getNumSamples() && i < (int) sum.size(); ++i) { const float x = s.getSample (0, i); if (! std::isfinite (x)) nan = true; sum[(size_t) i] += x; }
    for (int i = 0; i < mix.getNumSamples(); ++i) if (! std::isfinite (mix.getSample (0, i))) nan = true;
    const double cS = corr (mix.getReadPointer (0), sum.data(), mix.getNumSamples());
    // the live path: the same processor, host-synced, rendered by the test harness
    RenderOpts o; o.bars = juce::jmax (1, p.idea().total / BAR); o.sr = 44100; o.bpm = 124; std::vector<MidiEv> ev; int byp = 0; const auto live = render (p, o, ev, byp);
    const int n = juce::jmin (live.getNumSamples(), mix.getNumSamples());
    const double cL = corr (mix.getReadPointer (0), live.getReadPointer (0), n);
    std::printf ("audio drag-out: mix %d samples, stems aligned %d, corr(mix, sum of stems) %.4f, corr(mix, live preview) %.4f\n", mix.getNumSamples(), (int) aligned, cS, cL);
    C.ok (aligned && ! nan && cS > 0.99, "AUDIO stems: same length, no NaN, they add up to the mix (corr > 0.99)");
    C.ok (cL > 0.98, "AUDIO render = the live preview of the same idea (corr > 0.98)");
    // lane volume: -inf silences the lane (sound) and its MIDI; -6 dB halves the MIDI velocity
    {
        setP (p, laneVolId (1), -60.0f); setP (p, laneVolId (2), -6.0f);
        const auto j2 = p.renderJob (44100, 124); const auto arpOnly = ArpProcessor::renderOffline (j2, 2);
        setP (p, "kick", 0); setP (p, "midiCh1", 0);
        std::vector<MidiEv> e2; RenderOpts o2; o2.bars = 2; render (p, o2, e2, byp);
        int arpNotes = 0; double melVel = 0; int melN = 0; for (auto& e : e2) if (e.on) { if (e.ch == 1) ++arpNotes; if (e.ch == 2) { melVel += e.vel; ++melN; } }
        double ref = 0; int refN = 0; for (auto& m : p.idea().lanes[LaneMelody].notes) if (m.t < 2 * BAR) { ref += juce::jlimit (1, 127, (int) std::lround (playVel (p.idea().lanes[LaneMelody].notes, (size_t) (&m - &p.idea().lanes[LaneMelody].notes[0])) * 127)); ++refN; }
        const float pk = arpOnly.getMagnitude (0, 0, arpOnly.getNumSamples());
        std::printf ("  lane vol: arp-only peak %g, arp MIDI notes %d, melody vel %.1f vs ref %.1f (n %d / %d)\n", pk, arpNotes, melVel / juce::jmax (1, melN), ref / juce::jmax (1, refN), melN, refN);
        setP (p, laneVolId (1), 0.0f); setP (p, laneVolId (2), 0.0f); setP (p, "midiCh1", 1);
        C.ok (pk < 1e-4f && arpNotes == 0 && melN > 0 && std::abs (melVel / melN - 0.501 * ref / juce::jmax (1, refN)) < 2.0, "lane VOL: -inf mutes the lane (sound + MIDI), -6 dB halves its MIDI velocity");
    }
}

// ---------------------------------------------------------------- LISTEN on the web self-test signals
void listenChecks (Counter& C, const juce::File& outDir)
{
    using namespace listen;
    auto near = [] (double b, double w) { for (double x : { w, w * 2, w / 2 }) if (std::abs (b - x) <= 2) return true; return false; };
    auto cyc = [] (const Result& r, const std::vector<std::pair<int, bool>>& want) {
        int best = 0; if (r.chords.size() != want.size()) return 0;
        for (size_t rot = 0; rot < want.size(); ++rot) { int m = 0; for (size_t i = 0; i < want.size(); ++i) { const auto& g = r.chords[(i + rot) % r.chords.size()]; if (g.root == want[i].first && (g.iv[1] == 3) == want[i].second) ++m; } best = std::max (best, m); }
        return best; };
    auto dump = [&] (const char* name, const std::vector<float>& x) { auto f = outDir.getChildFile (name); f.replaceWithData (x.data(), x.size() * sizeof (float)); };
    auto show = [] (const char* n, const Result& r) {
        std::string loop; for (auto& c : r.chords) loop += c.name + " ";
        std::printf ("listen %s: ok=%d bpm=%.1f (%.2f) key=%d %s (%.2f) loop=%s chordConf=%.2f rate=%s groove=%.2f conf=%.2f %s\n", n, r.ok, r.bpm, r.tempoConf, r.key, r.scale.c_str(), r.keyConf, loop.c_str(), r.chordConf, r.rate.c_str(), r.groove, r.conf, r.why.c_str()); };
    const std::vector<std::vector<int>> AmFCG { { 45, 57, 60, 64 }, { 41, 57, 60, 65 }, { 48, 55, 60, 64 }, { 43, 55, 59, 62 } };
    const std::vector<std::pair<int, bool>> wAm { { 9, true }, { 5, false }, { 0, false }, { 7, false } };
    for (double lead : { 0.0, 2.7 })
    {
        SynthSpec s; s.bpm = 120; s.prog = AmFCG; s.dur = 12; s.lead = lead; const auto x = synth (s); const auto r = analyze (x, 44100);
        dump (lead == 0 ? "listen_120.f32" : "listen_120_lead.f32", x); show (lead == 0 ? "120 Am-F-C-G" : "120 Am-F-C-G mid-bar", r);
        C.ok (r.ok && near (r.bpm, 120), "listen 120: tempo"); C.ok ((r.key == 9 && r.scale == "minor") || (r.key == 0 && r.scale == "major"), "listen 120: key A minor / C major"); C.ok (cyc (r, wAm) >= 3, "listen 120: chords");
    }
    {
        SynthSpec s; s.bpm = 92; s.prog = { { 39, 55, 58, 63 }, { 46, 58, 62, 65 }, { 48, 55, 60, 63 }, { 44, 56, 60, 63 } }; s.dur = 16; const auto x = synth (s); const auto r = analyze (x, 44100);
        dump ("listen_92.f32", x); show ("92 Eb-Bb-Cm-Ab", r);
        C.ok (r.ok && near (r.bpm, 92), "listen 92: tempo"); C.ok ((r.key == 3 && r.scale == "major") || (r.key == 0 && r.scale == "minor"), "listen 92: key Eb major");
        C.ok (cyc (r, { { 3, false }, { 10, false }, { 0, true }, { 8, false } }) >= 3, "listen 92: chords");
    }
    { SynthSpec s; s.noise = true; s.dur = 8; const auto x = synth (s); const auto r = analyze (x, 44100); dump ("listen_noise.f32", x); show ("noise", r); C.ok (! r.ok || r.conf < 0.4, "listen noise: low confidence"); }
    { std::vector<float> z (44100 * 5, 0.0f); const auto r = analyze (z, 44100); C.ok (! r.ok, "listen silence: handled"); }
}

// ---------------------------------------------------------------- the render harness
void harness (Counter& C, const juce::File& outDir)
{
    ArpProcessor p;
    p.change ([] (Idea& X) { X.mode = Midnight; X.seed = 124; applyModeDefaults (X); newIdea (X);
        for (auto& L : X.lanes) L.mute = false; X.lanes[LaneArp].notes[3].l.type = 1; X.lanes[LaneArp].notes[9].l.type = 2; });   // every lane on, a glide + a fatten
    setP (p, "kick", 0); setP (p, "sound", 1); setP (p, "midiCh1", 0);   // per-lane channels for the timing check
    const Idea& I = p.idea();
    RenderOpts o; o.bpm = 124; o.bars = 8;
    std::vector<MidiEv> ev; int byp = 0; double played = 0;
    const auto audio = render (p, o, ev, byp, &played);
    writeWav (outDir.getChildFile ("harness_124bpm_8bars.wav"), audio, o.sr);
    // expected note-ons: each lane's MIDI notes (what the pattern holds), at floor(tick / ticksPerSample)
    const double tps = o.bpm / 60.0 * PPQ / o.sr;
    struct Ex { juce::int64 at; int ch, pitch; };
    std::vector<Ex> want;
    auto addLane = [&] (const std::vector<MidiNote>& ns, int ch) {
        for (int base = 0; base < o.bars * BAR * 2; base += I.total) for (auto& n : ns) { const double t = base + n.t; if (t / tps < played - 1e-6) want.push_back ({ (juce::int64) std::floor (t / tps + 1e-7), ch, n.p }); } };
    addLane (laneMidiNotes (I.lanes[LaneArp].notes, I.total), 1); addLane (laneMidiNotes (I.lanes[LaneMelody].notes, I.total), 2);
    addLane (chordLaneNotes (I), 3);
    { std::vector<MidiNote> b; for (auto& n : I.lanes[LaneBass].notes) b.push_back ({ n.t, n.d, n.p, n.v }); addLane (b, 4); }
    std::vector<Ex> got; for (auto& e : ev) if (e.on) got.push_back ({ e.at, e.ch, e.pitch });
    auto key = [] (const Ex& e) { return std::make_tuple (e.at, e.ch, e.pitch); };
    std::sort (want.begin(), want.end(), [&] (const Ex& a, const Ex& b) { return key (a) < key (b); });
    std::sort (got.begin(), got.end(), [&] (const Ex& a, const Ex& b) { return key (a) < key (b); });
    int mism = 0; juce::int64 worst = 0;
    const size_t n = std::min (want.size(), got.size());
    for (size_t i = 0; i < n; ++i) if (want[i].ch != got[i].ch || want[i].pitch != got[i].pitch || want[i].at != got[i].at) { ++mism; worst = std::max (worst, std::abs (want[i].at - got[i].at)); }
    std::printf ("harness: 8 bars @124: %zu note-ons expected, %zu sent, %d timing/pitch mismatches (worst %lld samples)\n", want.size(), got.size(), mism, (long long) worst);
    C.ok (want.size() == got.size() && mism == 0, "MIDI out is sample-accurate (every note-on on its exact sample)");
    std::string why; { const bool okb = balanced (ev, why); C.ok (okb, "MIDI notes balanced (every note-on has a note-off) " + why); }
    double pk, rms; bool nan; audioStats (audio, pk, rms, nan);
    std::printf ("harness audio: peak %.3f rms %.4f nan=%d\n", pk, rms, (int) nan);
    C.ok (! nan && pk > 0.05 && pk <= 1.0 && rms > 0.005, "built-in sound renders (not silent, no NaN, no clipping)");
    // odd block sizes give the same MIDI
    { std::vector<MidiEv> e2; RenderOpts o2 = o; o2.varBlocks = true; render (p, o2, e2, byp);
      std::vector<Ex> g2; for (auto& e : e2) if (e.on) g2.push_back ({ e.at, e.ch, e.pitch }); std::sort (g2.begin(), g2.end(), [&] (const Ex& a, const Ex& b) { return key (a) < key (b); });
      bool same = g2.size() == got.size(); for (size_t i = 0; same && i < g2.size(); ++i) same = key (g2[i]) == key (got[i]);
      { const bool okb = balanced (e2, why); C.ok (same && okb, "random block sizes: identical MIDI, balanced " + why); } }
    // transport stop in the middle of notes, then restart
    { std::vector<MidiEv> e3; RenderOpts o3 = o; o3.stopAtBar = 4.37; o3.restartAtBar = 5.0; o3.bars = 8; render (p, o3, e3, byp);
      { const bool okb = balanced (e3, why); C.ok (okb, "transport stop / restart: notes balanced " + why); }
      // nothing sounds while stopped
      const double spb = 4 * 60.0 / o.bpm * o.sr; int during = 0; for (auto& e : e3) if (e.on && e.at > 4.40 * spb && e.at < 4.95 * spb) ++during;
      C.ok (during == 0, "transport stopped: no notes"); }
    // bypass in the middle
    { std::vector<MidiEv> e4; RenderOpts o4 = o; o4.bypassAtBar = 2.6; o4.bypassBars = 0.8; render (p, o4, e4, byp);
      { const bool okb = balanced (e4, why); C.ok (byp > 0 && okb, "bypass in the middle: notes balanced (" + std::to_string (byp) + " bypassed blocks) " + why); } }
    // the host loops back (ppq jumps)
    { std::vector<MidiEv> e5; RenderOpts o5 = o; o5.loopBackAtBar = 3.3; render (p, o5, e5, byp); { const bool okb = balanced (e5, why); C.ok (okb, "host loop jump: notes balanced " + why); } }
    // follow my keys: hold A minor -> notes only while held, all in the held chord / key
    {
        ArpProcessor q; setP (q, "follow", 1); setP (q, "sound", 0); setP (q, "midiCh1", 0);
        FakeHead head; head.bpm = 124; q.setPlayHead (&head); q.prepareToPlay (44100, 512);
        int outside = 0, before = 0, during = 0, melodic = 0, heldTones = 0; juce::int64 pos = 0; const juce::int64 holdAt = 44100, release = 44100 * 4;
        for (int b = 0; b < 44100 * 6 / 512; ++b)
        {
            juce::AudioBuffer<float> buf (2, 512); buf.clear(); juce::MidiBuffer m;
            if (pos <= holdAt && pos + 512 > holdAt) for (int nn : { 57, 60, 64 }) m.addEvent (juce::MidiMessage::noteOn (1, nn, (juce::uint8) 100), 0);
            if (pos <= release && pos + 512 > release) for (int nn : { 57, 60, 64 }) m.addEvent (juce::MidiMessage::noteOff (1, nn), 0);
            head.ppq = pos / 44100.0 * 124 / 60.0; q.processBlock (buf, m);
            for (const auto meta : m) { const auto msg = meta.getMessage(); if (! msg.isNoteOn()) continue; const juce::int64 at = pos + meta.samplePosition;
                if (at < holdAt) ++before; else if (at < release + 512) { ++during; const int pc = msg.getNoteNumber() % 12;
                    if (msg.getChannel() <= 2) { ++melodic; if (pc == 9 || pc == 0 || pc == 4) ++heldTones; else if (pc != 11 && pc != 2 && pc != 5 && pc != 7) ++outside; } } else ++before; }
            pos += 512;
        }
        q.setPlayHead (nullptr);
        C.ok (before == 0 && during > 0 && outside == 0 && heldTones * 10 >= melodic * 6, "FOLLOW MY KEYS: plays only while keys are held, re-voiced onto them (" + std::to_string (during) + " notes, " + std::to_string (heldTones) + "/" + std::to_string (melodic) + " on held tones, " + std::to_string (outside) + " outside A minor)");
    }
    // state round trip
    {
        juce::MemoryBlock mb; p.getStateInformation (mb);
        ArpProcessor q; q.setStateInformation (mb.getData(), (int) mb.getSize());
        bool same = q.idea().seedUsed == I.seedUsed && q.idea().lanes[LaneArp].notes.size() == I.lanes[LaneArp].notes.size() && q.idea().chords.size() == I.chords.size();
        for (size_t i = 0; same && i < I.chords.size(); ++i) same = q.idea().chords[i].name == I.chords[i].name;
        C.ok (same, "state save / load round trip");
    }
    // MIDI drag file: never overwrites, opens as a standard MIDI file
    {
        const auto f1 = p.writeMidiFile (15, false), f2 = p.writeMidiFile (15, false);
        juce::FileInputStream in (f1); juce::MidiFile mf; const bool ok = mf.readFrom (in);
        C.ok (f1 != f2 && f1.existsAsFile() && f2.existsAsFile() && ok && mf.getNumTracks() == 5, "drag-out .mid: unique names, valid SMF with tempo + 4 lanes");
        f1.deleteFile(); f2.deleteFile();
    }
}

// ---------------------------------------------------------------- demos + per-mode renders
juce::File renderIdea (ArpProcessor& p, const juce::File& dir, const juce::String& name, int bars, double bpm)
{
    RenderOpts o; o.bpm = bpm; o.bars = bars; std::vector<MidiEv> ev; int byp = 0;
    auto audio = render (p, o, ev, byp);
    // gentle fade at the very end of the tail
    const int fade = (int) (o.sr * 0.8); for (int c = 0; c < 2; ++c) audio.applyGainRamp (c, audio.getNumSamples() - fade, fade, 1.0f, 0.0f);
    float peak = 0; for (int c = 0; c < audio.getNumChannels(); ++c) peak = std::max (peak, audio.getMagnitude (c, 0, audio.getNumSamples()));
    if (peak > 0.794f) audio.applyGain (0.794f / peak);   // demos peak at -2 dBFS at most (true peak stays under -1)
    auto wav = dir.getChildFile (name + ".wav"); writeWav (wav, audio, o.sr);
    return wav;
}
void demos (const juce::File& dir)
{
    {   // MIDNIGHT: 16 bars, evolving hypnotic arp over slow chords, no drums
        ArpProcessor p;
        p.change ([] (Idea& X) { X.mode = Midnight; X.seed = 4321; applyModeDefaults (X); X.evolve = true; X.evolveBars = 16; newIdea (X); });
        setP (p, "preset", arpsyn::Neon); setP (p, "kick", 0); setP (p, "pump", 0.18f); setP (p, "lfoDepth", 0.4f); setP (p, "lfoRate", 3); setP (p, "delay", 0.4f); setP (p, "reverb", 0.35f);
        const Idea& I = p.idea();
        renderIdea (p, dir, "ArpEZator_MIDNIGHT_demo", 16, I.g.bpm);
        const auto mid = ideaMidi (I, 15, false, I.g.bpm); dir.getChildFile ("ArpEZator_MIDNIGHT_demo.mid").replaceWithData (mid.data(), mid.size());
        std::string ch; for (auto& c : I.chords) ch += c.name + " ";
        std::printf ("demo MIDNIGHT: %s %s, %d bpm, chords %s(2 bars each), %d-step figure, %d bars\n", keyName (I.g.key, I.g.scale).c_str(), I.g.scale.c_str(), I.g.bpm, ch.c_str(), I.fig.len, I.total / BAR);
    }
    {   // SUNRISE: piano + supersaw stabs, AAAB 8-bar hook, 2-beat stagger, stutters, no drums (played twice)
        ArpProcessor p;
        p.change ([] (Idea& X) { X.mode = Sunrise; X.seed = 2024; applyModeDefaults (X); newIdea (X); X.form = 0; X.stagger = 2 * PPQ; X.stutter = 0.6; composeLane (X, LaneMelody, false); X.lanes[LaneArp].mute = true; X.lanes[LaneBass].mute = false; });
        setP (p, "preset", arpsyn::Piano); setP (p, "stabs", 1); setP (p, "kick", 0); setP (p, "pump", 0.35f); setP (p, "lfoDepth", 0.1f); setP (p, "delay", 0.28f); setP (p, "reverb", 0.3f);
        const Idea& I = p.idea();
        renderIdea (p, dir, "ArpEZator_SUNRISE_demo", 16, I.g.bpm);
        const auto mid = ideaMidi (I, (1 << LaneChords) | (1 << LaneMelody) | (1 << LaneBass), false, I.g.bpm); dir.getChildFile ("ArpEZator_SUNRISE_demo.mid").replaceWithData (mid.data(), mid.size());
        std::string ch; for (auto& c : I.chords) ch += c.name + " ";
        std::printf ("demo SUNRISE: %s, %d bpm, chords %s, hook %d bars (AAAB), stagger %d ticks, stutter %.2f\n", keyName (I.g.key, I.g.scale).c_str(), I.g.bpm, ch.c_str(), I.total / BAR, I.stagger, I.stutter);
    }
}
void perModeRenders (const juce::File& dir)
{
    for (int mode = 0; mode < 3; ++mode)
    {
        arptest::LineStats S; int k = 0;
        for (int i = 0; i < 5; ++i)
        {
            ArpProcessor p;
            p.change ([mode, i] (Idea& X) { X.mode = mode; X.seed = (uint32_t) (1 + i * 37); applyModeDefaults (X); newIdea (X); });
            setP (p, "preset", mode == Sunrise ? arpsyn::Piano : mode == Dusk ? arpsyn::Keys : arpsyn::Neon); setP (p, "stabs", mode == Sunrise ? 1.0f : 0.0f);
            const Idea& I = p.idea();
            renderIdea (p, dir, "mode_" + juce::String (modeName (mode)) + "_" + juce::String (i + 1), juce::jmin (16, I.total / BAR), I.g.bpm);
            const int lane = mode == Midnight ? LaneArp : LaneMelody;
            S.add (arptest::lineStats (I.lanes[lane].notes, I.H, I.total, mode, lane == LaneArp)); ++k;
        }
        std::printf ("rendered 5 %s ideas (%s lane):\n", modeName (mode).c_str(), mode == Midnight ? "ARP" : "MELODY");
        arptest::printStats ("render", S, k);
    }
}
void recipeReport (int n)
{
    hookFailCounts().clear();
    for (int mode = 0; mode < 3; ++mode)
    {
        const ModeRecipe& M = recipe().mode[mode];
        const int lane = mode == Midnight ? LaneArp : LaneMelody;
        std::map<std::string, double> sum; std::map<std::string, int> inR; double score = 0; int hard = 0, recipeFig = 0;
        auto add = [&] (const Metrics& m) { for (auto& tg : M.targets) { const double x = m.get (tg.first); sum[tg.first] += x; if (x >= tg.second.lo - 0.005 && x <= tg.second.hi + 0.005) inR[tg.first]++; } };
        for (int i = 0; i < n; ++i)
        {
            Idea I; I.mode = mode; I.seed = (uint32_t) (1 + i * 37); applyModeDefaults (I); newIdea (I);
            const auto& ns = I.lanes[lane].notes;
            ArpInfo ai; if (lane == LaneArp) { ai.valid = true; ai.len = I.fig.len; ai.resync = I.fig.resync; ai.events = (int) I.figEvents.size(); for (int k = 0; k < I.fig.len; ++k) if (I.fig.s[(size_t) k].pedal) ++ai.pedalSteps; if (I.fig.recipe) ++recipeFig; }
            Harmony Hs = I.H; Hs.offset = lane == LaneMelody ? I.stagger : 0;
            std::vector<Note> phraseNotes = ns; if (lane == LaneMelody && I.stagger) { for (auto& x : phraseNotes) x.t = ((x.t - I.stagger) % I.total + I.total) % I.total; sortNotes (phraseNotes); }
            const auto m = analyseLane (phraseNotes, Hs, I.total, lane == LaneArp ? &ai : nullptr);
            add (m); score += recipeScore (m, M);
            const bool ok = lane == LaneArp ? arpHardRules (ns, I.fig.len, I.total) : melodyHardRules (phraseNotes, Hs, I.total, mode, false, 0, 0);
            if (ok) ++hard;
        }
        if (mode != Midnight) { std::printf ("  hook candidates:"); for (auto& kv : hookFailCounts()) std::printf (" [%s] %d", kv.first.c_str(), kv.second); std::printf ("\n"); hookFailCounts().clear(); }
        std::printf ("%s: %d fresh ideas (%s lane), recipe score %.3f, hard rules pass %d/%d%s\n", modeName (mode).c_str(), n, lane == LaneArp ? "ARP" : "MELODY", score / n, hard, n,
                     lane == LaneArp ? (", recipe figures " + std::to_string (recipeFig) + "/" + std::to_string (n)).c_str() : "");
        for (auto& tg : M.targets) std::printf ("    %-15s mean %7.3f  winners [%6.3f, %6.3f]  in range %3d%%\n", tg.first.c_str(), sum[tg.first] / n, tg.second.lo, tg.second.hi, inR[tg.first] * 100 / n);
    }
    // the composer's library, analysed by the same code (a baseline for the scores above)
    for (int mode = 0; mode < 3; ++mode)
    {
        ArpProcessor p; const auto names = p.starterNames(); double sc = 0; int k = 0;
        for (int i = 0; i < names.size(); ++i)
        {
            if (! names[i].startsWithIgnoreCase (modeName (mode))) continue;
            p.loadStarter (i); const Idea& I = p.idea(); const int lane = mode == Midnight ? LaneArp : LaneMelody;
            ArpInfo ai; if (lane == LaneArp) { ai.valid = true; ai.len = I.fig.len; ai.resync = I.fig.resync; ai.events = (int) I.figEvents.size(); for (int q = 0; q < I.fig.len; ++q) if (I.fig.s[(size_t) q].pedal) ++ai.pedalSteps; }
            sc += recipeScore (analyseLane (I.lanes[lane].notes, I.H, I.total, lane == LaneArp ? &ai : nullptr), recipe().mode[mode]); ++k;
        }
        if (k) std::printf ("library winners (%s, %d ideas) analysed the same way: recipe score %.3f\n", modeName (mode).c_str(), k, sc / k);
    }
}
void printGrid (int mode, uint32_t seed, int lane, int bars, bool evolve)
{
    static const char* N[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    Idea I; I.mode = mode; I.seed = seed; applyModeDefaults (I); I.evolve = evolve; I.evolveBars = 16; newIdea (I);
    std::printf ("%s seed %u -> %s %s %d bpm, %d bars, form %d, stagger %d, figure len %d%s, events %zu\n", modeName (mode).c_str(), I.seedUsed, keyName (I.g.key, I.g.scale).c_str(), I.g.scale.c_str(), I.g.bpm, I.total / BAR, I.form, I.stagger, I.fig.len, I.fig.recipe ? (" pedal " + I.fig.pedalDeg).c_str() : "", I.figEvents.size());
    const int st = BAR / 16;
    for (int b = 0; b < bars && b * BAR < I.total; ++b)
    {
        std::string line;
        for (int s = 0; s < 16; ++s)
        {
            const int t = b * BAR + s * st; std::string cell = "  .  ";
            for (auto& n : I.lanes[lane].notes) if (n.t == t) { cell = std::string (N[n.p % 12]) + std::to_string (n.p / 12 - 1); while (cell.size() < 4) cell += " "; cell += (n.d > st + 4 ? "~" : " "); }
            if (cell == "  .  ") for (auto& n : I.lanes[lane].notes) if (n.t < t && n.t + n.d > t + 2) cell = "  -  ";
            line += cell;
        }
        std::printf ("bar %2d %-10s|%s\n", b + 1, I.H.at (b * BAR).name.c_str(), line.c_str());
    }
}
#include "SoundTests.h"
#include "SampleTests.h"
// the preview sounds per mood: what GENERATE in each mode sets up (8 bars, every lane, the mood's sound)
juce::AudioBuffer<float> moodRender (int mode, uint32_t seed, double sr, int bars = 8, int soloLane = -1)
{
    ArpProcessor p; p.persistTaste = false;
    p.generate (mode, false, false);
    p.change ([&] (Idea& X) { X.seed = seed; newIdea (X); if (soloLane >= 0) for (int l = 0; l < kLanes; ++l) X.lanes[l].mute = l != soloLane; }, false);
    RenderOpts o; o.bars = bars; o.sr = sr; o.bpm = p.idea().g.bpm; std::vector<MidiEv> ev; int byp = 0;
    return render (p, o, ev, byp);
}
#include "UrbanTests.h"
#include "EmoTests.h"
void soundReport()
{
    for (int pre = 0; pre < arpsyn::kPresets; ++pre)
    {
        std::printf ("%-14s alias dB:", arpsyn::presetName (pre));
        for (int pitch : { 72, 84, 90, 96 }) std::printf (" %d:%6.1f", pitch, soundtest::aliasDb (pre, pitch));
        std::printf ("\n");
    }
    for (int m = 0; m < kMoods; ++m)
    {
        double s = 0; int n = 0; std::printf ("%-9s LUFS:", modeName (m).c_str());
        for (uint32_t seed : { 11u, 128u, 515u, 2024u }) { const auto b = moodRender (m, seed, 48000); const double l = soundtest::lufs (b, 48000); s += l; ++n; std::printf (" %6.1f (pk %.2f)", l, b.getMagnitude (0, b.getNumSamples())); }
        std::printf ("  mean %.1f\n", s / n);
        if (urbanMood (m)) for (int l = 0; l < kLanes; ++l) std::printf ("    solo %s: %.1f LUFS\n", ArpEditor::laneName (l).toRawUTF8(), soundtest::lufs (moodRender (m, 11u, 48000, 8, l), 48000));
    }
}
void soundChecks (Counter& C)
{
    {   // each mood's own sound
        ArpProcessor p; p.persistTaste = false;
        const int p0 = (int) p.apvts.getRawParameterValue ("preset")->load();
        p.generate (Sunrise, false, false); const int p1 = (int) p.apvts.getRawParameterValue ("preset")->load();
        p.generate (Dusk, false, false); const int p2 = (int) p.apvts.getRawParameterValue ("preset")->load();
        p.generate (Midnight, false, false); const int p3 = (int) p.apvts.getRawParameterValue ("preset")->load(); const float lfo = p.apvts.getRawParameterValue ("lfoDepth")->load();
        C.ok (p0 == arpsyn::HypnoSaw && p1 == arpsyn::Euphoric && p2 == arpsyn::DuskPluck && p3 == arpsyn::HypnoSaw && lfo > 0.3f, "MOOD SOUNDS: SUNRISE stack, DUSK pluck, MIDNIGHT saw with the filter LFO");
    }
    {   // no aliasing at high notes
        double worst = -200; juce::String where;
        for (int pre : { (int) arpsyn::Euphoric, (int) arpsyn::DuskPluck, (int) arpsyn::HypnoSaw })
            for (int pitch : { 72, 84, 90, 96 }) { const double a = soundtest::aliasDb (pre, pitch); if (a > worst) { worst = a; where = juce::String (arpsyn::presetName (pre)) + " @" + juce::String (pitch); } }
        std::printf ("mood sounds: worst alias %.1f dB (%s)\n", worst, where.toRawUTF8());
        C.ok (worst < -55.0, "MOOD SOUNDS: no aliasing up to C7 (worst " + juce::String (worst, 1).toStdString() + " dB, " + where.toStdString() + ")");
    }
    {   // level-matched (BS.1770, 8 bars as GENERATE sets them up)
        double lo = 1e9, hi = -1e9; std::string txt;
        for (int m = 0; m < 3; ++m)
        {
            double s = 0; for (uint32_t seed : { 11u, 515u }) s += soundtest::lufs (moodRender (m, seed, 48000), 48000);
            s /= 2; lo = std::min (lo, s); hi = std::max (hi, s); txt += modeName (m) + " " + juce::String (s, 1).toStdString() + "  ";
        }
        std::printf ("mood loudness: %s(spread %.2f LU)\n", txt.c_str(), hi - lo);
        C.ok (hi - lo <= 1.0, "MOOD SOUNDS: level-matched within 1 LU (" + juce::String (hi - lo, 2).toStdString() + ")");
    }
    {   // a session saved with an old preset keeps it
        ArpProcessor p; p.persistTaste = false; setP (p, "preset", (float) arpsyn::Neon); setP (p, "lfoDepth", 0.2f);
        ArpProcessor q; q.persistTaste = false; q.restoreSession (p.sessionString());
        C.ok ((int) q.apvts.getRawParameterValue ("preset")->load() == arpsyn::Neon && std::abs (q.apvts.getRawParameterValue ("lfoDepth")->load() - 0.2f) < 1e-4f, "MOOD SOUNDS: a saved session keeps its own sound");
    }
}
// PLAY FROM THE CURSOR: a click in a roll sets where SPACE starts the internal transport
void cursorChecks (Counter& C)
{
    ArpProcessor p; p.persistTaste = false;
    p.change ([] (Idea& X) { X.mode = Midnight; X.seed = 128; applyModeDefaults (X); X.phraseBars = 16; newIdea (X); X.phraseBars = 16; rebuildChords (X); }, false);
    std::unique_ptr<ArpEditor> ed (dynamic_cast<ArpEditor*> (p.createEditor())); ed->setSize (1240, 820);
    const double sr = 48000; const int blk = 64; p.setRateAndBufferSizeDetails (sr, blk); p.prepareToPlay (sr, blk);
    const double tps = p.idea().g.bpm / 60.0 * PPQ / sr;
    auto block = [&] () { juce::AudioBuffer<float> b (2, blk); juce::MidiBuffer m; p.processBlock (b, m); return p.playTick.load(); };
    auto space = [&] () { return ed->keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)); };
    auto near = [&] (double got, double from) { return std::abs (got - (from + blk * tps)) < 0.5; };
    // no click: from the beginning
    block(); space(); const double t0 = block();
    space(); block(); const bool stopped = ! p.internalPlay.load();
    // click bar 5 beat 2 (the bar ruler row), then SPACE
    auto& roll = ed->roll; ed->selectLane (LaneMelody); roll.setView ({ 0.0, (double) p.idea().total });
    const int want = 4 * BAR + PPQ;
    roll.setCursorAt (roll.X (want) + 0.5f, arpui::Roll::hy1 + 6.0f);
    space(); const double t1 = block();
    for (int i = 0; i < 50; ++i) block();
    space(); block(); space(); const double t2 = block();   // stop, then SPACE again: from the cursor, not from where it stopped
    std::printf ("play cursor: start %.2f (no click), %.2f (cursor %d), again %.2f; tick per block %.2f\n", t0 - blk * tps, t1 - blk * tps, p.playCursor.load(), t2 - blk * tps, blk * tps);
    C.ok (near (t0, 0) && stopped, "PLAY CURSOR: no click -> SPACE starts at the beginning, SPACE stops");
    C.ok (p.playCursor.load() == want && near (t1, want), "PLAY CURSOR: a click at bar 5 beat 2 -> SPACE starts exactly there");
    C.ok (near (t2, want), "PLAY CURSOR: stop + SPACE starts at the cursor again (not where it stopped)");
    space(); block();
    ed->keyPressed (juce::KeyPress (juce::KeyPress::homeKey)); space(); const double t3 = block(); space(); block();
    C.ok (p.playCursor.load() < 0 && near (t3, 0), "PLAY CURSOR: HOME resets it to the beginning");
    // every lane: the click lands on the lane's grid (a note click = the note's start)
    int ok = 0;
    for (int l = 0; l < kLanes; ++l)
    {
        ed->selectLane (l); roll.setView ({ 0.0, (double) p.idea().total });
        const int t = 2 * BAR + 2 * PPQ + 5;   // a little after beat 3 of bar 3: snaps down to it
        roll.setCursorAt (roll.X (t) + 0.2f, arpui::Roll::hy1 + 6.0f);
        if (p.playCursor.load() == 2 * BAR + 2 * PPQ) ++ok;
        const auto& ns = p.idea().lanes[l].notes;
        if (l != LaneChords && ! ns.empty()) { const auto& n = ns[ns.size() / 2]; roll.setView ({ (double) (n.t / BAR) * BAR, 4.0 * BAR }); roll.setCursorAt (roll.X (n.t + std::min (n.d, 24) / 2), roll.Y (n.p) + roll.rh() / 2); if (p.playCursor.load() != n.t) --ok; }
    }
    C.ok (ok == kLanes, "PLAY CURSOR: works in all four rolls (grid snap down, a note = its start)");
    // the idea gets shorter: the cursor can't point past the end
    roll.setView ({ 12.0 * BAR, 4.0 * BAR }); roll.setCursorAt (roll.X (13 * BAR) + 0.5f, arpui::Roll::hy1 + 6.0f);
    const int before = p.playCursor.load();
    p.change ([] (Idea& X) { X.phraseBars = 4; X.evolve = false; X.songBars = 0; rebuildChords (X); }, false);
    C.ok (before == 13 * BAR - (13 * BAR) % BAR && p.idea().total < 13 * BAR && p.playCursor.load() == -1, "PLAY CURSOR: reset when the idea shortens past it");
    // the host plays: SPACE does nothing, the host keeps the position
    FakeHead head; head.bpm = p.idea().g.bpm; head.playing = true; head.ppq = 2.0; p.setPlayHead (&head);
    block(); const bool wasInt = p.internalPlay.load(); const bool used = space(); const double th = block();
    p.setPlayHead (nullptr);
    C.ok (! used && p.internalPlay.load() == wasInt && std::abs (th - 2.0 * PPQ) < 0.5, "PLAY CURSOR: no effect while the host plays (space goes to the host)");
}
// BYPASS: off = nothing changes; on = silence within 20 ms, note-offs for every sounding note and no MIDI after, the clock runs on
void bypassChecks (Counter& C)
{
    struct Ev2 { double t; int ch, p; bool on; };
    auto run = [] (ArpProcessor& p, double bars, std::function<void (double)> poke, std::vector<float>& mono, std::vector<Ev2>& ev) {
        const double sr = 48000; const int blk = 256; const double bpm = 124;
        FakeHead head; head.bpm = bpm; p.setPlayHead (&head); p.setRateAndBufferSizeDetails (sr, blk); p.prepareToPlay (sr, blk);
        const double spb = 4 * 60.0 / bpm * sr; const auto total = (juce::int64) (spb * bars);
        for (juce::int64 pos = 0; pos < total; pos += blk)
        {
            if (poke) poke (pos / spb);
            head.ppq = pos / sr * bpm / 60.0; head.samples = pos; head.playing = true;
            juce::AudioBuffer<float> b (2, blk); b.clear(); juce::MidiBuffer m; p.processBlock (b, m);
            for (int i = 0; i < blk; ++i) mono.push_back (b.getSample (0, i));
            for (const auto meta : m) { const auto x = meta.getMessage(); const double t = (pos + meta.samplePosition) / spb; if (x.isNoteOn()) ev.push_back ({ t, x.getChannel(), x.getNoteNumber(), true }); else if (x.isNoteOff()) ev.push_back ({ t, x.getChannel(), x.getNoteNumber(), false }); }
        }
        p.setPlayHead (nullptr);
    };
    auto make = [] (ArpProcessor& p) { p.persistTaste = false; p.change ([] (Idea& X) { X.mode = Midnight; X.seed = 4321; applyModeDefaults (X); newIdea (X); X.lanes[LaneBass].mute = false; X.lanes[LaneMelody].mute = false; }, false);
                                       setP (p, "midiOut", 1); setP (p, "midiCh1", 0); setP (p, "velScale", 0); };
    {   // off: bit-identical audio + identical MIDI
        ArpProcessor a, b; make (a); make (b); setP (b, "bypass", 1); setP (b, "bypass", 0);
        std::vector<float> ma, mb; std::vector<Ev2> ea, eb; run (a, 4, {}, ma, ea); run (b, 4, {}, mb, eb);
        bool same = ma.size() == mb.size() && ea.size() == eb.size(); for (size_t i = 0; same && i < ma.size(); ++i) same = ma[i] == mb[i];
        for (size_t i = 0; same && i < ea.size(); ++i) same = ea[i].t == eb[i].t && ea[i].p == eb[i].p && ea[i].on == eb[i].on;
        C.ok (same && ! ea.empty(), "BYPASS off: bit-identical audio and MIDI");
    }
    {   // engaged at bar 2, released at bar 4
        ArpProcessor p; make (p);
        std::vector<float> m; std::vector<Ev2> ev; bool on = false, offAgain = false;
        run (p, 6, [&] (double bar) { if (! on && bar >= 2.0) { on = true; setP (p, "bypass", 1); } if (! offAgain && bar >= 4.0) { offAgain = true; setP (p, "bypass", 0); } }, m, ev);
        const double sr = 48000, spb = 4 * 60.0 / 124 * sr; const size_t e0 = (size_t) std::ceil (2.0 * spb / 256) * 256, e20 = e0 + (size_t) (0.020 * sr), r0 = (size_t) std::ceil (4.0 * spb / 256) * 256;
        float peak = 0; for (size_t i = e20; i < r0; ++i) peak = std::max (peak, std::abs (m[i]));
        float jumpFade = 0, jumpPlay = 0; for (size_t i = e0 + 1; i < e20; ++i) jumpFade = std::max (jumpFade, std::abs (m[i] - m[i - 1])); for (size_t i = 1000; i < e0; ++i) jumpPlay = std::max (jumpPlay, std::abs (m[i] - m[i - 1]));
        const double engT = e0 / spb, relT = r0 / spb;
        std::map<int, int> open; int onsDuring = 0; bool balanced = true;
        for (auto& x : ev)
        {
            const int k = x.ch * 128 + x.p;
            if (x.t >= engT && x.t < relT && x.on) ++onsDuring;
            if (x.t >= relT) break;   // (balanced up to the release: the render's own end leaves notes open)
            if (x.on) open[k]++; else open[k] = std::max (0, open[k] - 1);
            if (x.t >= engT && x.t < relT && ! x.on) {}
        }
        int openAtEngage = 0; { std::map<int, int> o2; for (auto& x : ev) { if (x.t >= engT) break; const int k = x.ch * 128 + x.p; if (x.on) o2[k]++; else o2[k] = std::max (0, o2[k] - 1); } for (auto& kv : o2) openAtEngage += kv.second; }
        int offsAtEngage = 0, after = 0; for (auto& x : ev) { if (x.t >= engT - 1e-9 && x.t < engT + 256 / spb && ! x.on) ++offsAtEngage; if (x.t >= engT + 256 / spb && x.t < relT) ++after; }
        for (auto& kv : open) if (kv.second != 0) balanced = false;
        int onsAfter = 0, onGrid = 0; for (auto& x : ev) if (x.t >= relT && x.on) { ++onsAfter; const double tick = x.t * BAR; if (std::abs (tick - 24.0 * std::round (tick / 24.0)) < 0.6) ++onGrid; }
        std::printf ("bypass: peak after 20 ms %.1f dBFS, fade jump %.3f vs playing %.3f, %d notes sounding at engage -> %d note-offs, %d MIDI events while bypassed, after release %d note-ons (%d on the grid)\n",
                     20 * std::log10 (peak + 1e-12), jumpFade, jumpPlay, openAtEngage, offsAtEngage, after, onsAfter, onGrid);
        C.ok (20 * std::log10 (peak + 1e-12) < -120.0, "BYPASS on: audio below -120 dBFS within 20 ms");
        C.ok (onsDuring == 0 && after == 0 && offsAtEngage >= openAtEngage && openAtEngage > 0 && balanced, "BYPASS on: a note-off for every sounding note, then no MIDI at all");
        C.ok (jumpFade <= jumpPlay * 1.05f + 1e-4f, "BYPASS: the fade doesn't click");
        C.ok (onsAfter > 0 && onGrid == onsAfter, "BYPASS released: playback resumes in time (every note on its grid tick)");
    }
    {   // the parameter: automatable, the host's bypass, saved, old states load off, never touched by GENERATE / undo / presets
        ArpProcessor p; make (p);
        auto* bp = p.getBypassParameter();
        C.ok (bp != nullptr && bp->isAutomatable() && bp == p.apvts.getParameter ("bypass") && p.getParameters().indexOf (bp) > p.getParameters().indexOf (p.apvts.getParameter ("playMode")) && p.getParameters().indexOf (p.apvts.getParameter ("transpose")) > p.getParameters().indexOf (bp) && p.getParameters().getLast() == p.apvts.getParameter ("preset_bass"),
              "BYPASS: after every older parameter (TRANSPOSE + the lane sounds added after it), automatable, the host's bypass");
        setP (p, "bypass", 1); juce::MemoryBlock mb; p.getStateInformation (mb);
        ArpProcessor q; q.persistTaste = false; q.setStateInformation (mb.getData(), (int) mb.getSize());
        auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
        if (auto* st = xml != nullptr ? xml->getChildByName ("ARPEZATOR") : nullptr) for (auto* pr : st->getChildIterator()) if (pr->getStringAttribute ("id") == "bypass") { st->removeChildElement (pr, true); break; }
        juce::MemoryBlock old; juce::AudioProcessor::copyXmlToBinary (*xml, old);
        ArpProcessor r; r.persistTaste = false; r.setStateInformation (old.getData(), (int) old.getSize());
        C.ok (q.bypassParam->get() && ! r.bypassParam->get(), "BYPASS: saved with the project; a project from before loads with it off");
        p.generate (Sunrise, true, true); p.applyMoodSound (Dusk); p.undo(); p.restoreSession (p.sessionString());
        C.ok (p.bypassParam->get(), "BYPASS: GENERATE, moods, undo, sessions never touch it");
    }
}
// PLAY: ALWAYS | ON NOTES (FL channel-mute follow)
void gateChecks (Counter& C)
{
    struct Ev2 { double t; int ch, p; bool on; };
    const double sr = 48000, bpm = 124; const int blk = 256; const double spb = 4 * 60.0 / bpm * sr;
    auto run = [&] (ArpProcessor& p, double bars, std::function<void (double, juce::MidiBuffer&, FakeHead&)> poke, std::vector<float>& mono, std::vector<Ev2>& ev) {
        FakeHead head; head.bpm = bpm; p.setPlayHead (&head); p.setRateAndBufferSizeDetails (sr, blk); p.prepareToPlay (sr, blk);
        const auto total = (juce::int64) (spb * bars); double ppq = 0;
        for (juce::int64 pos = 0; pos < total; pos += blk)
        {
            juce::MidiBuffer m; head.playing = true;
            if (poke) poke (pos / spb, m, head);
            head.ppq = ppq; head.samples = pos; if (head.playing) ppq += blk / sr * bpm / 60.0;
            juce::AudioBuffer<float> b (2, blk); b.clear(); p.processBlock (b, m);
            for (int i = 0; i < blk; ++i) mono.push_back (b.getSample (0, i));
            for (const auto meta : m) { const auto x = meta.getMessage(); const double t = (pos + meta.samplePosition) / spb; if (x.isNoteOn()) ev.push_back ({ t, x.getChannel(), x.getNoteNumber(), true }); else if (x.isNoteOff()) ev.push_back ({ t, x.getChannel(), x.getNoteNumber(), false }); }
        }
        p.setPlayHead (nullptr);
    };
    auto make = [] (ArpProcessor& p, int mode) { p.persistTaste = false; p.change ([] (Idea& X) { X.mode = Midnight; X.seed = 4321; applyModeDefaults (X); newIdea (X); X.lanes[LaneBass].mute = false; X.lanes[LaneMelody].mute = false; }, false);
                                                 setP (p, "midiOut", 1); setP (p, "midiCh1", 0); setP (p, "velScale", 0); setP (p, "playMode", (float) mode); };
    auto key = [] (juce::MidiBuffer& m, int note, bool on) { m.addEvent (on ? juce::MidiMessage::noteOn (1, note, (juce::uint8) 100) : juce::MidiMessage::noteOff (1, note), 0); };
    auto peakDb = [&] (const std::vector<float>& m, double b0, double b1) { float pk = 0; for (size_t i = (size_t) (b0 * spb); i < (size_t) (b1 * spb) && i < m.size(); ++i) pk = std::max (pk, std::abs (m[i])); return 20 * std::log10 (pk + 1e-12); };
    {   // ALWAYS: bit-identical
        ArpProcessor a, b; make (a, 0); make (b, 0); setP (b, "playMode", 1); setP (b, "playMode", 0);
        std::vector<float> ma, mb; std::vector<Ev2> ea, eb; run (a, 3, {}, ma, ea); run (b, 3, {}, mb, eb);
        bool same = ma.size() == mb.size() && ea.size() == eb.size(); for (size_t i = 0; same && i < ma.size(); ++i) same = ma[i] == mb[i];
        for (size_t i = 0; same && i < ea.size(); ++i) same = ea[i].t == eb[i].t && ea[i].p == eb[i].p && ea[i].on == eb[i].on;
        C.ok (same && ! ea.empty(), "PLAY ALWAYS: bit-identical audio and MIDI");
    }
    {   // ON NOTES: silent without a note; a held note plays (on the grid); its note-off stops everything
        ArpProcessor p; make (p, 1);
        std::vector<float> m; std::vector<Ev2> ev; int st = 0;
        run (p, 5, [&] (double bar, juce::MidiBuffer& in, FakeHead&) { if (st == 0 && bar >= 1.0) { key (in, 48, true); st = 1; } if (st == 1 && bar >= 3.0) { key (in, 48, false); st = 2; } }, m, ev);
        int before = 0, first = -1; double firstT = 0; for (auto& x : ev) { if (x.t < 1.0) ++before; if (x.on && x.t >= 1.0 && first < 0) { first = x.p; firstT = x.t; } }
        const double ft = firstT * BAR; const bool onGrid = std::abs (ft - 24.0 * std::round (ft / 24.0)) < 0.6;
        // stop at bar 3 (block-aligned)
        const double offT = std::ceil (3.0 * spb / blk) * blk / spb;
        std::map<int, int> open; for (auto& x : ev) { if (x.t >= offT + blk / spb) break; const int k = x.ch * 128 + x.p; if (x.on) open[k]++; else open[k] = std::max (0, open[k] - 1); }
        int hung = 0; for (auto& kv : open) hung += kv.second;
        int after = 0; for (auto& x : ev) if (x.t >= offT + blk / spb) ++after;
        float jF = 0, jP = 0; const size_t o0 = (size_t) (offT * spb); for (size_t i = o0 + 1; i < o0 + (size_t) (0.02 * sr); ++i) jF = std::max (jF, std::abs (m[i] - m[i - 1])); for (size_t i = (size_t) (1.5 * spb); i < o0; ++i) jP = std::max (jP, std::abs (m[i] - m[i - 1]));
        std::printf ("on notes: %d MIDI events before the note, silent %.1f dB; first note bar %.4f (grid %d); %d hung after the note-off, %d events after, tail %.1f dB, fade jump %.3f vs %.3f\n",
                     before, peakDb (m, 0.05, 0.95), firstT + 1, (int) onGrid, hung, after, peakDb (m, offT + 0.02 * sr / spb, 5.0), jF, jP);
        C.ok (before == 0 && peakDb (m, 0.05, 0.95) < -120, "PLAY ON NOTES: no note -> no MIDI, silent");
        C.ok (first >= 0 && onGrid && firstT < 1.3, "PLAY ON NOTES: a held note plays, the first note lands on the grid");
        C.ok (hung == 0 && after == 0 && peakDb (m, offT + 0.02 * sr / spb, 5.0) < -120 && jF <= jP * 1.05f + 1e-4f, "PLAY ON NOTES: the note-off stops every note (note-offs, silence, no click)");
    }
    {   // overlapping notes keep it going until the last one ends; host stop / start with a note held; nothing stuck
        ArpProcessor p; make (p, 1);
        std::vector<float> m; std::vector<Ev2> ev; int st = 0;
        run (p, 7, [&] (double bar, juce::MidiBuffer& in, FakeHead& h) {
            if (st == 0 && bar >= 0.5) { key (in, 48, true); st = 1; }
            if (st == 1 && bar >= 1.5) { key (in, 52, true); st = 2; }
            if (st == 2 && bar >= 2.0) { key (in, 48, false); st = 3; }
            if (bar >= 3.0 && bar < 3.5) h.playing = false;   // the host stops for half a bar
            if (st == 3 && bar >= 5.0) { key (in, 52, false); st = 4; } }, m, ev);
        int during = 0, resumed = 0, after = 0; for (auto& x : ev) { if (x.on && x.t > 2.05 && x.t < 3.0) ++during; if (x.on && x.t > 3.5 && x.t < 5.0) ++resumed; if (x.t > 5.0 + 2.0 * blk / spb) ++after; }
        std::map<int, int> open; for (auto& x : ev) { const int k = x.ch * 128 + x.p; if (x.on) open[k]++; else open[k] = std::max (0, open[k] - 1); }
        int hung = 0; for (auto& kv : open) hung += kv.second;
        C.ok (during > 0 && resumed > 0 && after == 0 && hung == 0, "PLAY ON NOTES: overlapping notes, a host stop / start with a note held: plays on, stops at the last note-off, nothing stuck");
    }
    {   // saved per project, old projects load ALWAYS, undo / presets / moods never touch it
        ArpProcessor p; make (p, 1); juce::MemoryBlock mb; p.getStateInformation (mb);
        ArpProcessor q; q.persistTaste = false; q.setStateInformation (mb.getData(), (int) mb.getSize());
        auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
        if (auto* st = xml != nullptr ? xml->getChildByName ("ARPEZATOR") : nullptr) for (auto* pr : st->getChildIterator()) if (pr->getStringAttribute ("id") == "playMode") { st->removeChildElement (pr, true); break; }
        juce::MemoryBlock old; juce::AudioProcessor::copyXmlToBinary (*xml, old);
        ArpProcessor r; r.persistTaste = false; r.setStateInformation (old.getData(), (int) old.getSize());
        auto pm = [] (ArpProcessor& x) { return (int) x.apvts.getRawParameterValue ("playMode")->load(); };
        p.generate (Sunrise, true, true); p.applyMoodSound (Dusk); p.undo(); p.restoreSession (p.sessionString());
        C.ok (pm (q) == 1 && pm (r) == 0 && pm (p) == 1, "PLAY: saved per project, old projects load ALWAYS, GENERATE / moods / undo leave it");
    }
}
// TRANSPOSE: a live offset on every lane, BAKE, TO KEY, folding, arrow keys
void transposeChecks (Counter& C)
{
    struct Ev2 { double t; int ch, p; bool on; };
    const double sr = 48000, bpm = 124; const int blk = 256; const double spb = 4 * 60.0 / bpm * sr;
    auto run = [&] (ArpProcessor& p, double bars, std::function<void (double)> poke, std::vector<float>& mono, std::vector<Ev2>& ev) {
        FakeHead head; head.bpm = bpm; p.setPlayHead (&head); p.setRateAndBufferSizeDetails (sr, blk); p.prepareToPlay (sr, blk);
        const auto total = (juce::int64) (spb * bars);
        for (juce::int64 pos = 0; pos < total; pos += blk)
        {
            if (poke) poke (pos / spb);
            head.ppq = pos / sr * bpm / 60.0; head.samples = pos; head.playing = true;
            juce::AudioBuffer<float> b (2, blk); b.clear(); juce::MidiBuffer m; p.processBlock (b, m);
            for (int i = 0; i < blk; ++i) mono.push_back (b.getSample (0, i));
            for (const auto meta : m) { const auto x = meta.getMessage(); const double t = (pos + meta.samplePosition) / spb; if (x.isNoteOn()) ev.push_back ({ t, x.getChannel(), x.getNoteNumber(), true }); else if (x.isNoteOff()) ev.push_back ({ t, x.getChannel(), x.getNoteNumber(), false }); }
        }
        p.setPlayHead (nullptr);
    };
    auto make = [] (ArpProcessor& p, int tr) { p.persistTaste = false; p.change ([] (Idea& X) { X.mode = Dusk; X.seed = 51; applyModeDefaults (X); newIdea (X); for (auto& L : X.lanes) L.mute = false; }, false);
                                               setP (p, "midiOut", 1); setP (p, "midiCh1", 0); setP (p, "velScale", 0); setP (p, "transpose", (float) tr); };
    // the shift between two renders, from their spectra on a semitone grid
    auto semis = [] (const std::vector<float>& a, const std::vector<float>& b) {
        auto spec = [] (const std::vector<float>& x) {
            const int N = 1 << 16; std::vector<double> bins (300, 0.0);
            for (size_t off = 0; off + N <= x.size(); off += N)
            {
                std::vector<std::complex<double>> v ((size_t) N); for (int i = 0; i < N; ++i) v[(size_t) i] = x[off + (size_t) i] * (0.5 - 0.5 * std::cos (6.283185307 * i / (N - 1)));
                soundtest::fft (v);
                for (int k = 1; k < N / 2; ++k) { const double f = k * 48000.0 / N; if (f < 50 || f > 4000) continue; const double m = 12 * std::log2 (f / 440.0) + 69; const int bi = (int) std::lround ((m - 30) * 3); if (bi >= 0 && bi < 300) bins[(size_t) bi] += std::norm (v[(size_t) k]); }
            }
            double mean = 0; for (auto& q : bins) { q = std::sqrt (q); mean += q / bins.size(); } for (auto& q : bins) q -= mean; return bins; };
        const auto A = spec (a), B = spec (b); int best = 0; double bc = -1e300;
        for (int s = -8; s <= 8; ++s) { double c = 0; for (int i = 30; i < 270; ++i) c += A[(size_t) i] * B[(size_t) (i + s * 3)]; if (c > bc) { bc = c; best = s; } }
        return best; };
    std::vector<float> m0, m3, mm3; std::vector<Ev2> e0, e3, em3;
    {   // 0 = bit-identical (after a trip to +3 and back)
        ArpProcessor a, b; make (a, 0); make (b, 3); setP (b, "transpose", 0);
        std::vector<float> ma, mb; std::vector<Ev2> ea, eb; run (a, 4, {}, ma, ea); run (b, 4, {}, mb, eb);
        bool same = ma.size() == mb.size() && ea.size() == eb.size(); for (size_t i = 0; same && i < ma.size(); ++i) same = ma[i] == mb[i];
        for (size_t i = 0; same && i < ea.size(); ++i) same = ea[i].t == eb[i].t && ea[i].p == eb[i].p && ea[i].on == eb[i].on;
        C.ok (same && ! ea.empty(), "TRANSPOSE 0: bit-identical audio and MIDI");
        m0 = ma; e0 = ea;
    }
    {   // +3 / -3: every MIDI note of all four lanes, exactly
        ArpProcessor a, b; make (a, 3); make (b, -3);
        C.ok (a.foldedCount() == 0, "TRANSPOSE test idea needs no folding at +3");
        run (a, 4, {}, m3, e3); run (b, 4, {}, mm3, em3);
        bool ok3 = e3.size() == e0.size(), okm3 = em3.size() == e0.size(); std::set<int> chans;
        for (size_t i = 0; ok3 && i < e0.size(); ++i) { ok3 = e3[i].p == e0[i].p + 3 && e3[i].ch == e0[i].ch && std::abs (e3[i].t - e0[i].t) < 1e-9; chans.insert (e0[i].ch); }
        for (size_t i = 0; okm3 && i < e0.size(); ++i) okm3 = em3[i].p == e0[i].p - 3;
        const int s3 = semis (m0, m3), sm3 = semis (m0, mm3);
        std::printf ("transpose: MIDI %zu notes on %zu channels, +3 %s, -3 %s; preview pitch measured %+d / %+d semitones\n", e0.size(), chans.size(), ok3 ? "exact" : "WRONG", okm3 ? "exact" : "WRONG", s3, sm3);
        C.ok (ok3 && okm3 && chans.size() == 4, "TRANSPOSE +3 / -3: every MIDI note of all four lanes moves exactly");
        C.ok (s3 == 3 && sm3 == -3, "TRANSPOSE: the preview sound moves by the same semitones (measured)");
    }
    {   // the drag-outs carry it (MIDI + audio), BAKE = the same output at offset 0, one UNDO step back
        ArpProcessor p; make (p, 3);
        const Idea X = p.exportIdea(), O = p.idea();
        bool mid = true; for (int l : { LaneArp, LaneMelody, LaneBass }) { if (X.lanes[l].notes.size() != O.lanes[l].notes.size()) mid = false; for (size_t i = 0; mid && i < O.lanes[l].notes.size(); ++i) mid = X.lanes[l].notes[i].p == O.lanes[l].notes[i].p + 3; }
        const auto cx = chordLaneNotes (X), co = chordLaneNotes (O); if (cx.size() != co.size()) mid = false; for (size_t i = 0; mid && i < co.size(); ++i) mid = cx[i].p == co[i].p + 3;
        const auto job3 = p.renderJob (48000, bpm); ArpProcessor q; make (q, 0); const auto job0 = q.renderJob (48000, bpm);
        const auto a3 = ArpProcessor::renderOffline (job3, -1), a0 = ArpProcessor::renderOffline (job0, -1);
        std::vector<float> v3 (a3.getReadPointer (0), a3.getReadPointer (0) + a3.getNumSamples()), v0 (a0.getReadPointer (0), a0.getReadPointer (0) + a0.getNumSamples());
        const int ds = semis (v0, v3);
        C.ok (mid && ds == 3, "TRANSPOSE: DRAG MIDI / DRAG AUDIO carry it (+3: notes exact, audio measured " + std::to_string (ds) + ")");
        std::vector<float> mb; std::vector<Ev2> eb;
        p.bakeTransposeNow(); run (p, 4, {}, mb, eb);
        bool same = eb.size() == e3.size(); for (size_t i = 0; same && i < eb.size(); ++i) same = eb[i].p == e3[i].p && std::abs (eb[i].t - e3[i].t) < 1e-9;
        bool aud = mb.size() == m3.size(); double maxd = 0; for (size_t i = 0; aud && i < mb.size(); ++i) maxd = std::max (maxd, (double) std::abs (mb[i] - m3[i]));
        const auto keyBaked = juce::String (keyName (p.idea().H.key, p.idea().H.scaleName));
        p.undo();
        std::printf ("bake: MIDI same %d, audio max diff %.2e, key %s, after UNDO offset %d\n", (int) same, maxd, keyBaked.toRawUTF8(), p.transposeNow());
        C.ok (same && maxd < 1e-6 && p.transposeNow() == 3 && p.idea().lanes[LaneMelody].notes.size() == O.lanes[LaneMelody].notes.size() && p.idea().lanes[LaneMelody].notes[0].p == O.lanes[LaneMelody].notes[0].p,
              "BAKE: identical output at offset 0; one UNDO brings the notes and the +3 back");
    }
    {   // TO KEY: the smallest move, all 12 roots; names follow
        bool all = true; for (int from = 0; from < 12; ++from) for (int r = 0; r < 12; ++r) { const int s = keyShiftTo (from, r); if (s < -5 || s > 6 || mod12 (from + s) != r) all = false; }
        C.ok (all && keyShiftTo (6, 9) == 3 && keyShiftTo (6, 2) == -4, "TO KEY: the smallest shift for every root (F#m -> Am +3, F#m -> Dm -4)");
        C.ok (transposeChordName ("F#m7/A", 3, 9, "minor") == "Am7/C" && transposeChordName ("Bb", 2, 0, "major") == "C", "TRANSPOSE: chord names follow (F#m7/A +3 = Am7/C)");
    }
    {   // folding: nothing leaves the range, nothing goes silent
        ArpProcessor a; make (a, 24); std::vector<float> m; std::vector<Ev2> ev; run (a, 4, {}, m, ev);
        int bad = 0; for (auto& x : ev) if (x.on && (x.p > 96 + (x.ch == 3 ? 12 : 0) || (x.ch == 4 && x.p < 24))) ++bad;
        int ons0 = 0, ons = 0; for (auto& x : e0) ons0 += x.on; for (auto& x : ev) ons += x.on;
        std::printf ("fold at +24: %d notes folded, %d out of range, %d / %d notes sound\n", a.foldedCount(), bad, ons, ons0);
        C.ok (a.foldedCount() > 0 && bad == 0 && ons == ons0, "TRANSPOSE: folding keeps every note in range and audible");
    }
    {   // automation mid-play: no hung notes
        ArpProcessor a; make (a, 0); std::vector<float> m; std::vector<Ev2> ev; int last = -1;
        run (a, 4, [&] (double bar) { const int k = (int) (bar * 4); if (k != last) { last = k; setP (a, "transpose", (float) ((k * 5) % 13 - 6)); } }, m, ev);
        std::map<int, int> open; bool dbl = false; for (auto& x : ev) { const int key = x.ch * 128 + x.p; if (x.on) { if (open[key] > 0) dbl = true; open[key]++; } else open[key] = std::max (0, open[key] - 1); }
        int hung = 0; for (auto& kv : open) hung += kv.second;
        float jump = 0; for (size_t i = 1; i < m.size(); ++i) jump = std::max (jump, std::abs (m[i] - m[i - 1]));
        C.ok (! dbl && hung <= 8 && jump < 0.5f, "TRANSPOSE automation: every note-off at its own pitch, no hung notes, no clicks (" + std::to_string (hung) + " still sounding at the end)");
    }
    {   // arrow keys on the selected notes; nothing selected = nothing
        ArpProcessor p; make (p, 0); std::unique_ptr<ArpEditor> ed (dynamic_cast<ArpEditor*> (p.createEditor())); ed->setSize (1240, 820); ed->selectLane (LaneMelody);
        auto& roll = ed->roll; const auto before = p.idea().lanes[LaneMelody].notes;
        const bool none = ! roll.keyPressed (juce::KeyPress (juce::KeyPress::upKey));
        roll.sel.assign (before.size(), false); roll.sel[0] = roll.sel[2] = true;
        roll.keyPressed (juce::KeyPress (juce::KeyPress::upKey)); roll.keyPressed (juce::KeyPress (juce::KeyPress::upKey, juce::ModifierKeys::shiftModifier, 0));
        const auto& a = p.idea().lanes[LaneMelody].notes;
        C.ok (none && a[0].p == before[0].p + 13 && a[2].p == before[2].p + 13 && a[1].p == before[1].p, "ROLL: UP = +1, SHIFT+UP = +12 on the selected notes only; nothing selected = nothing");
    }
    {   // saved per project, old projects load 0, presets / moods / undo never touch it
        ArpProcessor p; make (p, 5); p.transposeMask = 7; juce::MemoryBlock mb; p.getStateInformation (mb);
        ArpProcessor q; q.persistTaste = false; q.setStateInformation (mb.getData(), (int) mb.getSize());
        auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
        if (auto* st = xml != nullptr ? xml->getChildByName ("ARPEZATOR") : nullptr) for (auto* pr : st->getChildIterator()) if (pr->getStringAttribute ("id") == "transpose") { st->removeChildElement (pr, true); break; }
        juce::MemoryBlock old; juce::AudioProcessor::copyXmlToBinary (*xml, old);
        ArpProcessor r; r.persistTaste = false; r.setStateInformation (old.getData(), (int) old.getSize());
        p.generate (Sunrise, true, true); p.applyMoodSound (Dusk); p.undo(); p.restoreSession (p.sessionString());
        C.ok (q.transposeNow() == 5 && q.transposeMask.load() == 7 && r.transposeNow() == 0 && p.transposeNow() == 5, "TRANSPOSE: saved per project (+ lanes), old projects load 0, GENERATE / moods / undo leave it");
    }
}
// SYNC: DAW | OWN
void syncChecks (Counter& C)
{
    struct Ev2 { double t; int ch, p; bool on; };
    const double sr = 48000; const int blk = 256;
    struct Host { bool playing = true; double bpm = 124, ppq0 = 0; };
    auto run = [&] (ArpProcessor& p, int blocks, Host h, std::function<void (int)> poke, std::vector<float>& mono, std::vector<Ev2>& ev) {
        FakeHead head; head.bpm = h.bpm; p.setPlayHead (&head); p.setRateAndBufferSizeDetails (sr, blk); p.prepareToPlay (sr, blk);
        double ppq = h.ppq0;
        for (int k = 0; k < blocks; ++k)
        {
            if (poke) poke (k);
            head.playing = h.playing; head.ppq = ppq; head.samples = (juce::int64) k * blk; if (h.playing) ppq += blk / sr * h.bpm / 60.0;
            juce::AudioBuffer<float> b (2, blk); b.clear(); juce::MidiBuffer m; p.processBlock (b, m);
            for (int i = 0; i < blk; ++i) mono.push_back (b.getSample (0, i));
            for (const auto meta : m) { const auto x = meta.getMessage(); const double t = k * blk + meta.samplePosition; if (x.isNoteOn()) ev.push_back ({ t, x.getChannel(), x.getNoteNumber(), true }); else if (x.isNoteOff()) ev.push_back ({ t, x.getChannel(), x.getNoteNumber(), false }); }
        }
        p.setPlayHead (nullptr);
    };
    auto make = [] (ArpProcessor& p) { p.persistTaste = false; p.change ([] (Idea& X) { X.mode = Midnight; X.seed = 4321; applyModeDefaults (X); newIdea (X); for (auto& L : X.lanes) L.mute = false; }, false);
                                       setP (p, "midiOut", 1); setP (p, "midiCh1", 0); setP (p, "velScale", 0); };
    auto same = [] (const std::vector<float>& a, const std::vector<float>& b, const std::vector<Ev2>& x, const std::vector<Ev2>& y) {
        bool s = a.size() == b.size() && x.size() == y.size(); for (size_t i = 0; s && i < a.size(); ++i) s = a[i] == b[i];
        for (size_t i = 0; s && i < x.size(); ++i) s = x[i].t == y[i].t && x[i].p == y[i].p && x[i].on == y[i].on && x[i].ch == y[i].ch; return s; };
    auto balanced = [] (const std::vector<Ev2>& ev, double upTo, int& hung, bool& dbl) { std::map<int, int> o; dbl = false; for (auto& x : ev) { if (x.t >= upTo) break; const int k = x.ch * 128 + x.p; if (x.on) { if (o[k] > 0) dbl = true; o[k]++; } else o[k] = std::max (0, o[k] - 1); } hung = 0; for (auto& kv : o) hung += kv.second; };
    const int bars4 = (int) (4 * 4 * 60.0 / 124 * sr / blk);
    {   // DAW: bit-identical (after a trip to OWN and back)
        ArpProcessor a, b; make (a); make (b); b.setSyncOwn (true); b.setSyncOwn (false);
        std::vector<float> ma, mb; std::vector<Ev2> ea, eb; run (a, bars4, {}, {}, ma, ea); run (b, bars4, {}, {}, mb, eb);
        C.ok (same (ma, mb, ea, eb) && ! ea.empty(), "SYNC: DAW is bit-identical (audio + MIDI)");
    }
    {   // OWN: the host's transport and tempo don't matter
        ArpProcessor a, b; make (a); make (b);
        for (auto* p : { &a, &b }) { p->setSyncOwn (true); p->setOwnBpm (128); }
        a.sync.startOwn (0); b.sync.startOwn (0);
        std::vector<float> ma, mb; std::vector<Ev2> ea, eb;
        run (a, bars4, Host { true, 124, 0 }, {}, ma, ea); run (b, bars4, Host { false, 87, 13.5 }, {}, mb, eb);
        const double fsec = ea.empty() ? -1 : ea[0].t / sr; const double tick = ea.empty() ? 0 : ea[0].t / sr * 128 / 60.0 * PPQ;
        C.ok (same (ma, mb, ea, eb) && ! ea.empty() && std::abs (tick - 24 * std::round (tick / 24)) < 0.6, "SYNC: OWN ignores the DAW (two host transports -> identical MIDI + audio), notes on the grid (first at " + juce::String (fsec, 3).toStdString() + " s)");
    }
    {   // OWN PLAY / SPACE from the cursor (or bar 1); tempo changes next block; no hung notes
        ArpProcessor p; make (p); p.setSyncOwn (true); p.setOwnBpm (120);
        std::unique_ptr<ArpEditor> ed (dynamic_cast<ArpEditor*> (p.createEditor())); ed->setSize (1240, 820);
        std::vector<float> m; std::vector<Ev2> ev;
        p.setPlayCursor (4 * BAR + PPQ);
        const bool sp = ed->keyPressed (juce::KeyPress (juce::KeyPress::spaceKey));
        run (p, 1, Host { true, 124, 50.0 }, {}, m, ev);
        const double t1 = p.playTick.load(), want = 4 * BAR + PPQ + blk * 120.0 / 60.0 * PPQ / sr;
        ed->keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)); run (p, 1, {}, {}, m, ev); const bool stopped = ! p.sync.isOwnPlaying();
        p.setPlayCursor (-1); ed->keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)); std::vector<Ev2> e2; std::vector<float> m2;
        double tB = -1, tA = -1;
        run (p, bars4, {}, [&] (int k) { if (k == 1) tA = p.playTick.load(); if (k == bars4 / 2) p.setOwnBpm (150); if (k == bars4 / 2 + 2) tB = p.playTick.load(); }, m2, e2);
        const double t0 = tA - blk * 120.0 / 60.0 * PPQ / sr;
        int hung; bool dbl; balanced (e2, 1e18, hung, dbl);
        double adv = 0; { std::vector<float> m3; std::vector<Ev2> e3; double a0 = 0; run (p, 2, {}, [&] (int k) { if (k == 1) a0 = p.playTick.load(); }, m3, e3); adv = p.playTick.load() - a0; }
        (void) tB;
        std::printf ("sync own: cursor start %.2f (want %.2f), bar-1 start %.2f, after the tempo change %.3f ticks a block (150 bpm wants %.3f)\n", t1, want, t0, adv, blk * 150.0 / 60.0 * PPQ / sr);
        C.ok (sp && std::abs (t1 - want) < 0.5 && stopped && std::abs (t0) < 0.5, "SYNC: OWN SPACE plays from the cursor tick (or bar 1) and stops");
        C.ok (std::abs (adv - blk * 150.0 / 60.0 * PPQ / sr) < 0.01 && ! dbl && hung <= 8, "SYNC: OWN tempo changes on the next block, note-offs stay consistent");
        p.setSyncOwn (false);
        FakeHead head; head.playing = false; p.setPlayHead (&head); juce::AudioBuffer<float> b (2, blk); juce::MidiBuffer mm; p.processBlock (b, mm); p.setPlayHead (nullptr);
        C.ok (! ed->keyPressed (juce::KeyPress (juce::KeyPress::spaceKey)), "SYNC: DAW leaves SPACE to the DAW");
    }
    {   // switching mode mid-play: note-offs for every sounding note, nothing stuck
        ArpProcessor p; make (p); std::vector<float> m; std::vector<Ev2> ev; const int sw = bars4 / 3;
        run (p, bars4, {}, [&] (int k) { if (k == sw) p.setSyncOwn (true); }, m, ev);
        int hung; bool dbl; balanced (ev, 1e18, hung, dbl); int after = 0; for (auto& x : ev) if (x.t >= (sw + 1) * blk) ++after;
        C.ok (hung == 0 && ! dbl && after == 0, "SYNC: switching mode mid-play ends every sounding note (OWN not started = silent)");
    }
    {   // saved per project; old projects load DAW; GENERATE / undo / moods never touch it
        ArpProcessor p; make (p); p.setSyncOwn (true); p.setOwnBpm (133); juce::MemoryBlock mb; p.getStateInformation (mb);
        ArpProcessor q; q.persistTaste = false; q.setStateInformation (mb.getData(), (int) mb.getSize());
        auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize()); xml->removeAttribute ("syncMode"); xml->removeAttribute ("ownBpm");
        juce::MemoryBlock old; juce::AudioProcessor::copyXmlToBinary (*xml, old);
        ArpProcessor r; r.persistTaste = false; r.setStateInformation (old.getData(), (int) old.getSize());
        p.generate (Sunrise, true, true); p.applyMoodSound (Dusk); p.undo(); p.restoreSession (p.sessionString());
        C.ok (q.syncOwn() && std::abs (q.sync.getOwnBpm() - 133) < 1e-9 && ! r.syncOwn() && p.syncOwn(), "SYNC: saved per project (+ the own tempo), old projects load DAW, GENERATE / moods / undo leave it");
    }
}
// YOUR SAMPLE + SOUNDS PER LANE, in the processor
// installer #6: YOUR SAMPLE NOTE LENGTH + SLICES, LANE MOOD, LANE FX (all numeric)
void newModesChecks (Counter& C)
{
    using namespace smptest;
    auto seg = [] (double hz, double sec) { return tone (hz, sec, 1); };
    {   // NOTE LENGTH TRIM: the note's length decides; ONE-SHOT rings on
        auto d = mk (seg (220.0, 2.0)); arpsmp::Params p; p.root = 57; p.a = 0.002f; p.s = 1.0f; p.d = 4.0f; p.r = 0.3f;
        p.mode = 0; const auto one = play (d.get(), p, { { 0.0, 0, 57.0f, 0.25, 1, 0.0f } }, 0.7);
        p.mode = 2; p.fitFade = 0.01f; const auto fit = play (d.get(), p, { { 0.0, 0, 57.0f, 0.25, 1, 0.0f } }, 0.7);
        const double a1 = rmsDb (fit.L, (size_t) (0.1 * kSr), (size_t) (0.1 * kSr)), a2 = rmsDb (fit.L, (size_t) (0.4 * kSr), (size_t) (0.1 * kSr)), o2 = rmsDb (one.L, (size_t) (0.4 * kSr), (size_t) (0.1 * kSr));
        std::printf ("note length TRIM: during %.1f dB, 0.15 s after the note %.1f dB (ONE-SHOT there %.1f dB)\n", a1, a2, o2);
        C.ok (a1 > -40 && a2 < a1 - 50 && o2 > a1 - 6, "YOUR SAMPLE NOTE LENGTH (TRIM): the note's length decides how much plays (ONE-SHOT still rings on)");
    }
    {   // NOTE LENGTH STRETCH: START..END (220 Hz then 330 Hz) fills each note; the pitch follows the key
        auto x = seg (220.0, 1.0); const auto y = seg (330.0, 1.0); x.insert (x.end(), y.begin(), y.end());
        auto d = mk (x); arpsmp::Params p; p.root = 57; p.a = 0.002f; p.s = 1.0f; p.d = 4.0f; p.mode = 2; p.fitStretch = true; p.fitFade = 0.01f;
        const auto s1 = play (d.get(), p, { { 0.0, 0, 57.0f, 1.0, 1, 0.0f } }, 1.3), s4 = play (d.get(), p, { { 0.0, 0, 57.0f, 4.0, 1, 0.0f } }, 4.3), k12 = play (d.get(), p, { { 0.0, 0, 69.0f, 1.0, 1, 0.0f } }, 1.3);
        const size_t w = (size_t) (0.12 * kSr);
        const double a = f0 (s1.L, (size_t) (0.2 * kSr), w), b = f0 (s1.L, (size_t) (0.7 * kSr), w), c = f0 (s4.L, (size_t) (1.2 * kSr), w), e = f0 (s4.L, (size_t) (3.0 * kSr), w), k = f0 (k12.L, (size_t) (0.2 * kSr), w);
        const double after = rmsDb (s1.L, (size_t) (1.15 * kSr), (size_t) (0.1 * kSr)), during = rmsDb (s1.L, (size_t) (0.5 * kSr), (size_t) (0.1 * kSr));
        std::printf ("note length STRETCH: 1 s note %.1f -> %.1f Hz, 4 s note %.1f -> %.1f Hz, an octave up %.1f Hz; after the note %.1f dB (during %.1f)\n", a, b, c, e, k, after, during);
        C.ok (std::abs (cents (a, 220)) < 25 && std::abs (cents (b, 330)) < 25 && std::abs (cents (c, 220)) < 25 && std::abs (cents (e, 330)) < 25 && std::abs (cents (k, 440)) < 25 && after < during - 40,
              "YOUR SAMPLE NOTE LENGTH (STRETCH): START..END fills a 1 s and a 4 s note exactly, the pitch from the key");
    }
    {   // SLICE TO NOTES: 4 hits -> 4 slices at their onsets; IN ORDER / BY PITCH pick them; GENERATE PATTERN FROM SLICES
        std::vector<float> x ((size_t) (2.0 * kSr), 0.0f); const double hz[4] { 220, 330, 440, 550 };
        for (int k = 0; k < 4; ++k) { const auto t = tone (hz[k], 0.25, 1); for (size_t i = 0; i < t.size(); ++i) x[(size_t) (k * 0.5 * kSr) + i] = t[i] * (float) std::exp (-3.0 * (double) i / kSr); }
        ArpProcessor pr; pr.persistTaste = false; std::vector<float> xs[2] = { x, {} }; juce::String m; pr.loadSampleAudio (xs, 1, kSr, "hits", {}, m);
        auto q = pr.sampleParams(); q.root = 57; pr.setSampleParams (q);
        const int n = pr.sliceSample (0.5f); const auto& sp = pr.sampleParams();
        double worst = 0; for (int k = 1; k < 4 && k < n; ++k) worst = std::max (worst, std::abs (sp.slices[(size_t) k] * 2.0 - k * 0.5));
        auto d = mk (x); auto p = sp; p.a = 0.001f; p.s = 1.0f; p.d = 4.0f; p.fitFade = 0.005f;
        const auto o = play (d.get(), p, { { 0.0, 0, 57.0f, 0.2, 1, 0.0f }, { 0.3, 0, 64.0f, 0.2, 2, 0.0f }, { 0.6, 0, 50.0f, 0.2, 3, 0.0f }, { 0.9, 0, 57.0f, 0.2, 4, 0.0f } }, 1.2);
        double f[4]; for (int k = 0; k < 4; ++k) f[k] = f0 (o.L, (size_t) ((0.3 * k + 0.03) * kSr), (size_t) (0.12 * kSr));
        p.sliceOrder = 1; const auto bp = play (d.get(), p, { { 0.0, 0, 59.0f, 0.2, 1, 0.0f } }, 0.3); const double fb = f0 (bp.L, (size_t) (0.03 * kSr), (size_t) (0.12 * kSr));
        p.sliceOrder = 0; p.slicePitched = true; const auto pp = play (d.get(), p, { { 0.0, 0, 69.0f, 0.2, 1, 0.0f } }, 0.3); const double fp = f0 (pp.L, (size_t) (0.03 * kSr), (size_t) (0.12 * kSr));
        std::printf ("slices: %d found (onsets within %.1f ms); IN ORDER plays %.0f / %.0f / %.0f / %.0f Hz; BY PITCH (+2) %.0f Hz; PITCHED +12 %.0f Hz\n", n, worst * 1000, f[0], f[1], f[2], f[3], fb, fp);
        C.ok (n == 4 && worst < 0.006, "SLICE TO NOTES: 4 hits -> 4 slices, each at its onset (< 6 ms)");
        C.ok (std::abs (cents (f[0], 220)) < 30 && std::abs (cents (f[1], 330)) < 30 && std::abs (cents (f[2], 440)) < 30 && std::abs (cents (f[3], 550)) < 30 && std::abs (cents (fb, 440)) < 30 && std::abs (cents (fp, 440)) < 30,
              "SLICES: IN ORDER = note k plays slice k at its own pitch; BY PITCH picks by semitones from ROOT; PITCHED follows the key");
        pr.change ([] (Idea& X) { X.g.bpm = 120; }, false);
        const bool made = pr.slicePattern (LaneMelody); const auto& ml = pr.idea().lanes[LaneMelody].notes;
        bool on = ml.size() >= 4; for (int k = 0; k < 4 && on; ++k) on = ml[(size_t) k].t == k * PPQ;   // 0.5 s apart at 120 BPM = one beat
        std::printf ("pattern from slices: %d notes, first at %d %d %d %d ticks\n", (int) ml.size(), ml.size() > 3 ? ml[0].t : -1, ml.size() > 3 ? ml[1].t : -1, ml.size() > 3 ? ml[2].t : -1, ml.size() > 3 ? ml[3].t : -1);
        C.ok (made && on, "GENERATE PATTERN FROM SLICES: the lane's notes land on the slices' rhythm");
        juce::MemoryBlock mb; pr.getStateInformation (mb); ArpProcessor r2; r2.persistTaste = false; r2.setStateInformation (mb.getData(), (int) mb.getSize());
        const auto& a2 = r2.sampleParams();
        C.ok (a2.mode == 3 && a2.nSlices == n && std::abs (a2.slices[1] - sp.slices[1]) < 1e-5f, "YOUR SAMPLE: the mode + the slices are saved with the project");
    }
    {   // LANE MOOD: the ARP composes in its own mood, the others keep theirs; SAME brings it back exactly
        ArpProcessor p; p.persistTaste = false;
        p.change ([] (Idea& X) { X.mode = Midnight; X.seed = 777; applyModeDefaults (X); newIdea (X); }, false);
        const auto I0 = p.idea();
        p.setLaneMood (LaneArp, Sunrise); const auto I1 = p.idea();
        p.setLaneMood (LaneArp, -1); const auto I2 = p.idea();
        p.newLaneVariation (LaneMelody); const auto I3 = p.idea();
        auto same = [] (const std::vector<Note>& a, const std::vector<Note>& b) { if (a.size() != b.size()) return false; for (size_t i = 0; i < a.size(); ++i) if (a[i].t != b[i].t || a[i].p != b[i].p || a[i].d != b[i].d) return false; return true; };
        const bool arpChanged = ! same (I0.lanes[LaneArp].notes, I1.lanes[LaneArp].notes), othersKept = same (I0.lanes[LaneMelody].notes, I1.lanes[LaneMelody].notes) && same (I0.lanes[LaneBass].notes, I1.lanes[LaneBass].notes) && I0.chords.size() == I1.chords.size();
        const bool back = same (I0.lanes[LaneArp].notes, I2.lanes[LaneArp].notes), melNew = ! same (I2.lanes[LaneMelody].notes, I3.lanes[LaneMelody].notes) && same (I2.lanes[LaneArp].notes, I3.lanes[LaneArp].notes);
        juce::MemoryBlock mb; p.getStateInformation (mb); ArpProcessor r; r.persistTaste = false; r.setStateInformation (mb.getData(), (int) mb.getSize());
        std::printf ("lane mood: ARP SUNRISE changes the arp %d, others kept %d, SAME restores it %d; MELODY new take %d (arp kept); saved %d\n", (int) arpChanged, (int) othersKept, (int) back, (int) melNew, r.idea().laneVar[LaneMelody]);
        C.ok (arpChanged && othersKept && back && melNew && r.idea().laneVar[LaneMelody] == I3.laneVar[LaneMelody] && r.idea().laneMood[LaneArp] == -1,
              "LANE MOOD + NEW TAKE: one lane changes, the others keep theirs; SAME restores it exactly; saved with the project");
    }
    {   // LANE FX: neutral = bit-exact; TONE / ECHO measurable; the CPU cost
        ArpProcessor p; p.persistTaste = false;
        p.change ([] (Idea& X) { X.mode = Dusk; X.seed = 51; applyModeDefaults (X); newIdea (X); for (auto& L : X.lanes) L.mute = false; }, false);
        auto job = p.renderJob (48000, 124);
        const auto t0 = juce::Time::getMillisecondCounterHiRes(); const auto a = ArpProcessor::renderOffline (job, -1); const double msOff = juce::Time::getMillisecondCounterHiRes() - t0;
        for (int l = 0; l < 4; ++l) for (int k = 0; k < 4; ++k) p.setLaneFx (l, k, 0.0f);
        const auto b = ArpProcessor::renderOffline (p.renderJob (48000, 124), -1);
        bool exact = a.getNumSamples() == b.getNumSamples(); for (int c = 0; c < 2 && exact; ++c) for (int i = 0; i < a.getNumSamples() && exact; ++i) exact = a.getSample (c, i) == b.getSample (c, i);
        for (int l = 0; l < 4; ++l) { p.setLaneFx (l, 0, 0.5f); p.setLaneFx (l, 1, -0.8f); p.setLaneFx (l, 2, 0.4f); p.setLaneFx (l, 3, 0.4f); }
        const auto t1 = juce::Time::getMillisecondCounterHiRes(); const auto f = ArpProcessor::renderOffline (p.renderJob (48000, 124), -1); const double msOn = juce::Time::getMillisecondCounterHiRes() - t1;
        auto hf = [] (const juce::AudioBuffer<float>& x) { std::vector<std::complex<double>> s (1 << 17); for (size_t i = 0; i < s.size() && (int) i < x.getNumSamples(); ++i) s[i] = x.getSample (0, (int) i);
            soundtest::fft (s); double lo = 0, hi = 0; for (size_t k = 1; k < s.size() / 2; ++k) { const double fq = k * 48000.0 / s.size(); (fq > 3000 ? hi : lo) += std::norm (s[k]); } return 10 * std::log10 (hi / (lo + 1e-30) + 1e-30); };
        const double h0 = hf (a), h1 = hf (f); bool fin = true; for (int i = 0; i < f.getNumSamples(); ++i) fin = fin && std::isfinite (f.getSample (0, i));
        const double secs = a.getNumSamples() / 48000.0;
        std::printf ("lane fx: neutral bit-exact %d; TONE darker: highs %.1f -> %.1f dB; render %.1f s of audio: %.0f ms off, %.0f ms with all 4 lanes' FX (%.2f%% / %.2f%% of a core)\n", (int) exact, h0, h1, secs, msOff, msOn, msOff / 10.0 / secs, msOn / 10.0 / secs);
        C.ok (exact, "LANE FX: all neutral = the render is bit-exact (the FX path is skipped)");
        C.ok (h1 < h0 - 6 && fin && msOn < msOff * 1.6 + 50, "LANE FX: TONE / DRIVE / ECHO / SPACE act per lane (darker by > 6 dB), finite, cheap");
        juce::MemoryBlock mb; p.getStateInformation (mb); ArpProcessor r; r.persistTaste = false; r.setStateInformation (mb.getData(), (int) mb.getSize());
        C.ok (std::abs (r.getLaneFx (2, 1) + 0.8f) < 1e-3f && std::abs (r.getLaneFx (3, 3) - 0.4f) < 1e-3f, "LANE FX: saved with the project");
    }
    {   // OLD SESSIONS: a project without any of the new keys loads every new setting neutral and renders exactly as a fresh one
        ArpProcessor p; p.persistTaste = false;
        p.change ([] (Idea& X) { X.mode = Sunrise; X.seed = 99; applyModeDefaults (X); newIdea (X); }, false);
        juce::MemoryBlock mb; p.getStateInformation (mb);
        auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
        const bool noNewKeys = xml != nullptr && ! xml->hasAttribute ("laneFx") && xml->toString().indexOf ("laneMood") < 0;
        ArpProcessor r; r.persistTaste = false; r.setStateInformation (mb.getData(), (int) mb.getSize());
        const auto a = ArpProcessor::renderOffline (p.renderJob (48000, 120), -1), b = ArpProcessor::renderOffline (r.renderJob (48000, 120), -1);
        bool exact = a.getNumSamples() == b.getNumSamples(); for (int c = 0; c < 2 && exact; ++c) for (int i = 0; i < a.getNumSamples() && exact; ++i) exact = a.getSample (c, i) == b.getSample (c, i);
        bool neutral = true; for (int l = 0; l < 4; ++l) { neutral &= r.idea().laneMood[(size_t) l] == -1 && r.idea().laneVar[(size_t) l] == 0; for (int k = 0; k < 4; ++k) neutral &= r.getLaneFx (l, k) == 0.0f; }
        std::printf ("old sessions: no new keys written %d, loads neutral %d, renders bit-exact %d\n", (int) noNewKeys, (int) neutral, (int) exact);
        C.ok (noNewKeys && neutral && exact, "OLD SESSIONS: nothing new is written while unused; old projects load neutral and render bit-exact");
    }
}
void sampleProcChecks (Counter& C)
{
    using smptest::tone;
    const double sr = 48000, bpm = 124; const int blk = 256; const double spb = 4 * 60.0 / bpm * sr;
    struct Ev2 { double t; int ch, p; bool on; };
    auto run = [&] (ArpProcessor& p, double bars, std::function<void (double)> poke, std::vector<float>& L, std::vector<float>* R = nullptr, std::vector<Ev2>* ev = nullptr, bool countAllocs = false) {
        FakeHead head; head.bpm = bpm; p.setPlayHead (&head); p.setRateAndBufferSizeDetails (sr, blk); p.prepareToPlay (sr, blk);
        const auto total = (juce::int64) (spb * bars);
        for (juce::int64 pos = 0; pos < total; pos += blk)
        {
            if (poke) poke (pos / spb);
            head.ppq = pos / sr * bpm / 60.0; head.samples = pos; head.playing = true;
            juce::AudioBuffer<float> b (2, blk); b.clear(); juce::MidiBuffer m; m.ensureSize (2048);
            gAudioThread = countAllocs; p.processBlock (b, m); gAudioThread = false;
            for (int i = 0; i < blk; ++i) { L.push_back (b.getSample (0, i)); if (R) R->push_back (b.getSample (1, i)); }
            if (ev) for (const auto meta : m) { const auto x = meta.getMessage(); const double t = (pos + meta.samplePosition) / spb; if (x.isNoteOn()) ev->push_back ({ t, x.getChannel(), x.getNoteNumber(), true }); else if (x.isNoteOff()) ev->push_back ({ t, x.getChannel(), x.getNoteNumber(), false }); }
        }
        p.setPlayHead (nullptr);
    };
    auto make = [] (ArpProcessor& p) { p.persistTaste = false; p.change ([] (Idea& X) { X.mode = Dusk; X.seed = 51; applyModeDefaults (X); newIdea (X); for (auto& L : X.lanes) L.mute = false; }, false); };
    auto loadSine = [] (ArpProcessor& p, double hz = 440.0, int harm = 1) { std::vector<float> x[2] = { tone (hz, 2.0, harm), {} }; juce::String m; return p.loadSampleAudio (x, 1, 48000.0, "sine", {}, m); };
    auto hfRatio = [] (const std::vector<float>& x) { std::vector<std::complex<double>> a (1 << 16); for (size_t i = 0; i < a.size() && i < x.size(); ++i) a[i] = x[i]; soundtest::fft (a); double lo = 0, hi = 0; for (size_t k = 1; k < a.size() / 2; ++k) { const double f = k * 48000.0 / a.size(); (f > 3000 ? hi : lo) += std::norm (a[k]); } return 10 * std::log10 (hi / (lo + 1e-30) + 1e-30); };
    {   // the hand-over: a new sample every 5 blocks while playing: no allocation on the audio thread, no crash (ASan)
        ArpProcessor p; make (p); loadSine (p); p.setLaneChoice (-1, arpsyn::kPresets + 1);
        std::vector<float> L; int k = 0;
        gAudioAllocs = 0;
        run (p, 4, [&] (double) { if (++k % 5 == 0) loadSine (p, 220.0 + (k % 7) * 30.0, 3); }, L, nullptr, nullptr, true);
        float pk = 0; for (float v : L) pk = std::max (pk, std::abs (v));
        std::printf ("sample swap: %d swaps while playing, %ld allocations on the audio thread, peak %.2f\n", k / 5, gAudioAllocs.load(), pk);
        C.ok (gAudioAllocs.load() == 0 && pk > 0.01f && std::isfinite (pk), "YOUR SAMPLE: swapped while playing, lock-free (0 allocations on the audio thread)");
    }
    {   // DRAG AUDIO renders with the sample (a sine: almost no highs, unlike the factory saw)
        ArpProcessor a, b; make (a); make (b); loadSine (a); a.linkAll = false; for (int l = 0; l < 4; ++l) a.setLaneChoice (l, arpsyn::kPresets + 1);
        setP (b, "preset", (float) arpsyn::Supersaw); for (int l = 0; l < 4; ++l) { b.linkAll = false; b.setLaneChoice (l, 1 + arpsyn::Supersaw); }
        const auto ra = ArpProcessor::renderOffline (a.renderJob (48000, bpm), 2 + 4), rb = ArpProcessor::renderOffline (b.renderJob (48000, bpm), 2 + 4);
        std::vector<float> va (ra.getReadPointer (0), ra.getReadPointer (0) + ra.getNumSamples()), vb (rb.getReadPointer (0), rb.getReadPointer (0) + rb.getNumSamples());
        const double ha = hfRatio (va), hb = hfRatio (vb);
        std::printf ("drag audio: highs vs lows %.1f dB with the sine sample, %.1f dB with SUPERSAW\n", ha, hb);
        C.ok (ha < hb - 20, "YOUR SAMPLE: DRAG AUDIO renders with the sample");
    }
    {   // the panel: PLAY SAMPLE ON buttons really switch off with the sample as the main sound; the waveform's shared gestures drive the sampler
        ArpProcessor p; make (p); loadSine (p); setP (p, "preset", (float) arpsyn::kPresets);
        std::unique_ptr<ArpEditor> ed (dynamic_cast<ArpEditor*> (p.createEditor())); ed->setSize (1240, 820); ed->openSample (true);
        auto& sp = *ed->samplePanel; sp.sync();
        auto press = [&] (arpui::Btn& b) { b.on = ! b.on; if (b.onClick) b.onClick(); sp.sync(); };
        if (! sp.laneB[LaneArp]->on) press (*sp.laneB[LaneArp]);
        const bool wasOn = sp.laneB[LaneArp]->on;
        press (*sp.laneB[LaneArp]); const int offChoice = p.laneChoice (LaneArp); const bool offBtn = sp.laneB[LaneArp]->on;
        press (*sp.laneB[LaneArp]); const int onChoice = p.laneChoice (LaneArp);
        std::printf ("sample lanes: ARP off -> choice %d (button %s), on -> choice %d\n", offChoice, offBtn ? "lit" : "off", onChoice);
        C.ok (wasOn && offChoice >= 1 && offChoice <= arpsyn::kPresets && ! offBtn && onChoice == arpsyn::kPresets + 1 && sp.laneB[LaneArp]->on,
              "YOUR SAMPLE: PLAY SAMPLE ON: ARP off = a factory sound (button off), on = the sample again");
        auto& w = sp.waveC; const auto wa = w.waveArea();
        auto mev = [&] (float x, float y, bool right, float dx = 0) {
            return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), { x + dx, y }, right ? juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier) : juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier),
                                     0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &w, &w, juce::Time::getCurrentTime(), { x, y }, juce::Time::getCurrentTime(), 1, dx != 0); };
        const float y = wa.getY() + wa.getHeight() * 0.3f;
        w.mouseDown (mev (w.xOf (0.25f), y, true)); w.mouseUp (mev (w.xOf (0.25f), y, true));
        const bool rc = std::abs (p.sampleParams().start - 0.25f) < 0.01f && p.sampleParams().mode == 0;
        w.mouseDown (mev (w.xOf (0.4f), y, true)); w.mouseDrag (mev (w.xOf (0.4f), y, true, w.xOf (0.6f) - w.xOf (0.4f))); w.mouseUp (mev (w.xOf (0.4f), y, true, w.xOf (0.6f) - w.xOf (0.4f)));
        const auto q = p.sampleParams();
        const bool rd = q.mode == 1 && std::abs (q.loopStart - 0.4f) < 0.01f && std::abs (q.loopEnd - 0.6f) < 0.01f && sp.modeSeg.sel == 1;
        w.mouseDown (mev (w.xOf (q.start), y, false)); w.mouseDrag (mev (w.xOf (q.start), y, false, w.xOf (0.1f) - w.xOf (q.start))); w.mouseUp (mev (w.xOf (q.start), y, false, w.xOf (0.1f) - w.xOf (q.start)));
        const bool hd = std::abs (p.sampleParams().start - 0.1f) < 0.01f;
        juce::StringArray dragged; w.performDrag = [&] (const juce::StringArray& f) { dragged = f; };
        w.startExport();
        juce::AudioFormatManager fm; fm.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> rdr (dragged.size() == 1 ? fm.createReaderFor (juce::File (dragged[0])) : nullptr);
        const double wantLen = 0.2 * 2.0 * 48000;
        std::printf ("sample wave: right-click START %s, right-drag LOOP %s, START handle %s, region WAV %lld samples (want %.0f)\n", rc ? "ok" : "NO", rd ? "ok" : "NO", hd ? "ok" : "NO", rdr ? (long long) rdr->lengthInSamples : -1LL, wantLen);
        C.ok (rc && rd && hd, "YOUR SAMPLE: right-click = START, right-drag = LOOP (on), drag the START handle");
        C.ok (rdr != nullptr && std::abs ((double) rdr->lengthInSamples - wantLen) < 0.01 * 48000, "YOUR SAMPLE: the loop drags out as a WAV of just that part");
        rdr.reset(); for (auto& f : dragged) juce::File (f).deleteFile();
        p.demoLockForTest = true; dragged.clear(); w.startExport(); p.demoLockForTest = false;
        C.ok (dragged.isEmpty(), "YOUR SAMPLE: the demo drags nothing out");
        ed->openSample (false);
    }
    {   // the project keeps the sample bit-exact (FLAC); old projects load without one
        ArpProcessor p; make (p); { std::vector<float> x[2] = { tone (330.0, 1.0, 5), tone (495.0, 1.0, 3) }; juce::String m; p.loadSampleAudio (x, 2, 44100.0, "stereo", "/tmp/x.wav", m); }
        auto q = arpsmp::Params(); q = p.sampleParams(); q.start = 0.1f; q.reverse = true; q.mode = 1; p.setSampleParams (q);
        std::vector<float> L; run (p, 0.05, {}, L);
        juce::MemoryBlock mb; p.getStateInformation (mb);
        ArpProcessor r; r.persistTaste = false; r.setStateInformation (mb.getData(), (int) mb.getSize()); std::vector<float> L2; run (r, 0.05, {}, L2);
        const auto* d1 = p.currentSample(); const auto* d2 = r.currentSample();
        bool exact = d1 != nullptr && d2 != nullptr && d1->chans == 2 && d2->chans == 2 && d1->rate == d2->rate && d1->lv[0][0].size() == d2->lv[0][0].size();
        for (int c = 0; exact && c < 2; ++c) for (size_t i = 0; exact && i < d1->lv[0][c].size(); ++i) exact = d1->lv[0][c][i] == d2->lv[0][c][i];
        auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize()); if (auto* sm = xml->getChildByName ("SAMPLE")) xml->removeChildElement (sm, true);
        juce::MemoryBlock old; juce::AudioProcessor::copyXmlToBinary (*xml, old);
        ArpProcessor o; o.persistTaste = false; o.setStateInformation (old.getData(), (int) old.getSize());
        std::printf ("sample state: %zu bytes FLAC for %.2f s stereo 44.1 k, bit-exact %d\n", p.sampleFlac.getSize(), d1 ? d1->length() / 44100.0 : 0.0, (int) exact);
        C.ok (exact && r.sampleParams().reverse && std::abs (r.sampleParams().start - 0.1f) < 1e-6f && r.sampleParams().mode == 1 && ! o.hasSample(), "YOUR SAMPLE: saved bit-exact in the project (FLAC) with its settings; old projects load without it");
    }
    {   // TRANSPOSE +3 moves the sample; BYPASS silences it; ON NOTES gates it
        ArpProcessor a, b; make (a); make (b);
        for (auto* p : { &a, &b }) { loadSine (*p); p->linkAll = false; for (int l = 0; l < 4; ++l) p->setLaneChoice (l, l == LaneMelody ? arpsyn::kPresets + 1 : 0); p->change ([] (Idea& X) { for (int l = 0; l < kLanes; ++l) X.lanes[l].mute = l != LaneMelody; }, false); }
        setP (b, "transpose", 3);
        std::vector<float> la, lb; run (a, 4, {}, la); run (b, 4, {}, lb);
        auto spec = [] (const std::vector<float>& x) { std::vector<double> bins (300, 0.0); const int N = 1 << 16; for (size_t off = 0; off + N <= x.size(); off += N) { std::vector<std::complex<double>> v ((size_t) N); for (int i = 0; i < N; ++i) v[(size_t) i] = x[off + (size_t) i]; soundtest::fft (v); for (int k = 1; k < N / 2; ++k) { const double f = k * 48000.0 / N; if (f < 50 || f > 4000) continue; const int bi = (int) std::lround ((12 * std::log2 (f / 440.0) + 69 - 30) * 3); if (bi >= 0 && bi < 300) bins[(size_t) bi] += std::norm (v[(size_t) k]); } } double mean = 0; for (auto& q : bins) { q = std::sqrt (q); mean += q / 300; } for (auto& q : bins) q -= mean; return bins; };
        const auto A = spec (la), B = spec (lb); int best = 0; double bc = -1e300; for (int s2 = -8; s2 <= 8; ++s2) { double c = 0; for (int i = 30; i < 270; ++i) c += A[(size_t) i] * B[(size_t) (i + s2 * 3)]; if (c > bc) { bc = c; best = s2; } }
        std::vector<float> lc; bool on = false; run (a, 2, [&] (double bar) { if (! on && bar >= 1.0) { on = true; setP (a, "bypass", 1); } }, lc);
        float tail = 0; for (size_t i = (size_t) (1.1 * spb); i < lc.size(); ++i) tail = std::max (tail, std::abs (lc[i]));
        setP (a, "bypass", 0); setP (a, "playMode", 1); std::vector<float> ld; run (a, 1, {}, ld); float gated = 0; for (float v : ld) gated = std::max (gated, std::abs (v));
        std::printf ("sample: TRANSPOSE +3 measured %+d st, BYPASS tail %.1f dB, ON NOTES without a note %.1f dB\n", best, 20 * std::log10 (tail + 1e-12), 20 * std::log10 (gated + 1e-12));
        C.ok (best == 3 && tail < 1e-6f && gated < 1e-6f, "YOUR SAMPLE: TRANSPOSE moves it, BYPASS silences it, ON NOTES gates it");
    }
    {   // SOUNDS PER LANE: each lane plays its own sound; changing one lane leaves the others' stems sample-identical
        ArpProcessor p; make (p); p.linkAll = false;
        const int choice[4] { 1 + arpsyn::Supersaw, 1 + arpsyn::Bell, 1 + arpsyn::Piano, 1 + arpsyn::Acid };
        for (int l = 0; l < 4; ++l) p.setLaneChoice (l, choice[l]);
        double cen[4] {}; std::vector<std::vector<float>> stem (4);
        for (int l = 0; l < 4; ++l)
        {
            const auto r = ArpProcessor::renderOffline (p.renderJob (48000, bpm), 1 << l);
            stem[(size_t) l].assign (r.getReadPointer (0), r.getReadPointer (0) + r.getNumSamples());
            std::vector<std::complex<double>> a (1 << 16); for (size_t i = 0; i < a.size() && i < stem[(size_t) l].size(); ++i) a[i] = stem[(size_t) l][i]; soundtest::fft (a);
            double num = 0, den = 0; for (size_t k = 1; k < a.size() / 2; ++k) { const double f = k * 48000.0 / a.size(), w = std::norm (a[k]); num += f * w; den += w; } cen[l] = num / (den + 1e-30);
        }
        p.setLaneChoice (LaneArp, 1 + arpsyn::Neon);
        const auto rc = ArpProcessor::renderOffline (p.renderJob (48000, bpm), 1 << LaneChords);
        bool iso = (int) stem[0].size() == rc.getNumSamples(); for (int i = 0; iso && i < rc.getNumSamples(); ++i) iso = rc.getSample (0, i) == stem[0][(size_t) i];
        // the mix = the sum of the stems (the shared FX make it close, not bit-exact)
        const auto mix = ArpProcessor::renderOffline (p.renderJob (48000, bpm), 15);
        std::vector<float> sum ((size_t) mix.getNumSamples(), 0.0f);
        for (int l = 0; l < 4; ++l) { const auto r = ArpProcessor::renderOffline (p.renderJob (48000, bpm), 1 << l); for (int i = 0; i < r.getNumSamples() && i < (int) sum.size(); ++i) sum[(size_t) i] += r.getSample (0, i); }
        double e = 0, d = 0; for (size_t i = 0; i < sum.size(); ++i) { e += (double) mix.getSample (0, (int) i) * mix.getSample (0, (int) i); d += (double) (sum[i] - mix.getSample (0, (int) i)) * (sum[i] - mix.getSample (0, (int) i)); }
        std::printf ("lanes: centroid chords %.0f, arp %.0f, melody %.0f, bass %.0f Hz; CHORDS stem unchanged by the ARP sound %d; stems vs mix %.1f dB\n", cen[0], cen[1], cen[2], cen[3], (int) iso, 10 * std::log10 (d / (e + 1e-30) + 1e-30));
        bool differ = true; for (int i = 0; i < 4; ++i) for (int j = i + 1; j < 4; ++j) if (std::abs (cen[i] - cen[j]) < 30) differ = false;
        C.ok (differ && iso, "SOUNDS PER LANE: every lane renders its own sound; the ARP sound doesn't touch the CHORDS stem (sample-identical)");
        C.ok (10 * std::log10 (d / (e + 1e-30) + 1e-30) < -30, "SOUNDS PER LANE: the MIX is the sum of the stems");
        // LINK ALL: one choice sets the shared sound and links every lane again; 'preset' automation drives the linked lanes
        p.linkAll = true; p.setLaneChoice (LaneMelody, 1 + arpsyn::Keys);
        bool linked = (int) p.apvts.getRawParameterValue ("preset")->load() == arpsyn::Keys; for (int l = 0; l < 4; ++l) if (p.laneChoice (l) != 0) linked = false;
        std::vector<float> l1, l2; run (p, 1, {}, l1); setP (p, "preset", (float) arpsyn::Bell); run (p, 1, {}, l2);
        bool changed = false; for (size_t i = 0; i < l1.size() && ! changed; ++i) changed = l1[i] != l2[i];
        C.ok (linked && changed, "SOUNDS PER LANE: LINK ALL sets every lane; 'preset' automation still drives them");
        // saved per project; moods set every lane
        p.linkAll = false; p.setLaneChoice (LaneBass, 1 + arpsyn::Acid); juce::MemoryBlock mb; p.getStateInformation (mb);
        ArpProcessor q; q.persistTaste = false; q.setStateInformation (mb.getData(), (int) mb.getSize());
        p.applyMoodSound (Sunrise);
        bool moodAll = true; for (int l = 0; l < 4; ++l) if (p.laneChoice (l) != 0) moodAll = false;
        C.ok (! q.linkAll && q.laneChoice (LaneBass) == 1 + arpsyn::Acid && moodAll, "SOUNDS PER LANE: saved per project; a mood sets every lane");
        // CPU: a busy preset on all four lanes
        ArpProcessor c4; make (c4); c4.linkAll = false; for (int l = 0; l < 4; ++l) c4.setLaneChoice (l, 1 + arpsyn::Euphoric);
        std::vector<float> lx; const auto t0 = juce::Time::getMillisecondCounterHiRes(); run (c4, 4, {}, lx); const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        std::printf ("lanes: SUNRISE STACK on all four lanes: %.1f %% of a core\n", ms / (4 * spb / sr * 1000.0) * 100.0);
    }
    {   // an old session (no lane sounds saved) plays bit-identically
        ArpProcessor a, b; make (a); make (b);
        auto s = juce::JSON::parse (a.sessionString()); auto snd = s["sound"].toString();
        for (auto* id : { "preset_chords", "preset_arp", "preset_melody", "preset_bass" }) { auto x = juce::parseXML (snd); if (x) { for (auto* c : x->getChildIterator()) if (c->getStringAttribute ("id") == id) { x->removeChildElement (c, true); break; } snd = x->toString (juce::XmlElement::TextFormat().singleLine()); } }
        if (auto* o = s.getDynamicObject()) o->setProperty ("sound", snd);
        b.restoreSession (juce::JSON::toString (s));
        std::vector<float> la, lb; std::vector<Ev2> ea, eb; run (a, 4, {}, la, nullptr, &ea); run (b, 4, {}, lb, nullptr, &eb);
        bool same = la == lb && ea.size() == eb.size(); for (size_t i = 0; same && i < ea.size(); ++i) same = ea[i].t == eb[i].t && ea[i].p == eb[i].p;
        C.ok (same, "SOUNDS PER LANE: an old session plays bit-identically (every lane linked)");
    }
}
// ---------------------------------------------------------------- THEMES: the shared BLACK / PURPLE / LIGHT looks + a pixel text-contrast probe
// The probe renders a component twice through a software renderer that records every text run (its colour, its box on the image):
// once normally, once with the text hidden (= the real pixels behind it). Contrast = text composited over each background pixel.
struct TextProbe : juce::LowLevelGraphicsSoftwareRenderer
{
    struct Item { juce::Rectangle<int> box; juce::Colour col; float h; };
    std::vector<Item>* out; bool hide; int gradientRuns = 0;
    std::vector<std::pair<juce::Point<int>, float>> layers;   // transparency layers (a component with alpha < 1): their device offset + opacity
    TextProbe (const juce::Image& im, std::vector<Item>* o, bool h) : juce::LowLevelGraphicsSoftwareRenderer (im), out (o), hide (h) {}
    void beginTransparencyLayer (float op) override
    { layers.push_back ({ stack->clip != nullptr ? stack->clip->getClipBounds().getPosition() : juce::Point<int>(), op }); juce::LowLevelGraphicsSoftwareRenderer::beginTransparencyLayer (op); }
    void endTransparencyLayer() override { juce::LowLevelGraphicsSoftwareRenderer::endTransparencyLayer(); if (! layers.empty()) layers.pop_back(); }
    void drawGlyphs (juce::Span<const uint16_t> gl, juce::Span<const juce::Point<float>> pos, const juce::AffineTransform& t) override
    {
        if (out != nullptr && ! gl.empty() && stack->clip != nullptr)
        {
            const auto full = stack->transform.getTransformWith (t);
            const float sc = std::sqrt (std::abs (full.getDeterminant())), fh = stack->font.getHeight() * sc, asc = stack->font.getAscent() * sc;
            juce::Rectangle<float> r;
            for (size_t i = 0; i < pos.size(); ++i) { auto q = pos[i].transformedBy (full); juce::Rectangle<float> g1 (q.x, q.y - asc * 0.75f, fh * 0.45f, asc * 0.7f); r = i == 0 ? g1 : r.getUnion (g1); }
            juce::Point<int> off; float op = 1.0f; for (auto& l : layers) { off += l.first; op *= l.second; }
            const auto box = r.getSmallestIntegerContainer().getIntersection (stack->clip->getClipBounds()) + off;
            const auto& ft = stack->fillType;
            if (! ft.isColour()) ++gradientRuns;
            else if (! box.isEmpty()) out->push_back ({ box, ft.colour.withMultipliedAlpha (ft.getOpacity() * op), fh });
            if (auto at = juce::SystemStats::getEnvironmentVariable ("ARP_PROBE_TRACE", {}); at.isNotEmpty() && box.contains (juce::Rectangle<int>::fromString (at).getPosition()))
                std::printf ("TRACE %s col %s\n%s\n", box.toString().toRawUTF8(), ft.colour.toDisplayString (true).toRawUTF8(), juce::SystemStats::getStackBacktrace().toRawUTF8());
        }
        if (! hide) juce::LowLevelGraphicsSoftwareRenderer::drawGlyphs (gl, pos, t);
    }
};
struct ContrastReport { int runs = 0, bad = 0, hidden = 0; double worst = 99; juce::String worstAt; };
// every text run on `c` (disabled controls excluded, as WCAG does): >= 4.5:1, >= 3:1 for large text (>= 18 px)
static ContrastReport probeContrast (juce::Component& c, const juce::String& page, std::function<void (juce::Graphics&)> paintFn = nullptr, juce::Rectangle<int> size = {}, std::vector<juce::Rectangle<int>> focus = {})
{
    const auto b = size.isEmpty() ? c.getLocalBounds() : size;
    std::vector<TextProbe::Item> items;
    juce::Image full (juce::Image::ARGB, b.getWidth(), b.getHeight(), true), bare (juce::Image::ARGB, b.getWidth(), b.getHeight(), true);
    { TextProbe ctx (full, &items, false); juce::Graphics g (ctx); if (paintFn) paintFn (g); else c.paintEntireComponent (g, true); }
    { TextProbe ctx (bare, nullptr, true); juce::Graphics g (ctx); if (paintFn) paintFn (g); else c.paintEntireComponent (g, true); }
    std::vector<juce::Rectangle<int>> disabled;
    std::function<void (juce::Component&)> walk = [&] (juce::Component& k) { for (auto* ch : k.getChildren()) { if (! ch->isVisible()) continue;
        if (! ch->isEnabled()) disabled.push_back (c.getLocalArea (ch, ch->getLocalBounds())); else walk (*ch); } };
    if (! paintFn) walk (c);
    if (auto at = juce::SystemStats::getEnvironmentVariable ("ARP_PROBE_AT", {}); at.isNotEmpty() && ! paintFn)
    {   // (debug) every component under that point, its bounds
        const auto pt = juce::Rectangle<int>::fromString (at).getPosition();
        std::function<void (juce::Component&, int)> w2 = [&] (juce::Component& k, int d) { for (auto* ch : k.getChildren()) { const auto r = c.getLocalArea (ch, ch->getLocalBounds()); if (ch->isVisible() && r.expanded (30).contains (pt)) { std::printf ("AT %*s%s '%s' %s alpha %.2f %s\n", d * 2, "", typeid (*ch).name(), ch->getName().toRawUTF8(), r.toString().toRawUTF8(), ch->getAlpha(), dynamic_cast<arpui::Btn*> (ch) != nullptr ? dynamic_cast<arpui::Btn*> (ch)->text.toRawUTF8() : ""); w2 (*ch, d + 1); } } };
        w2 (c, 0);
    }
    std::vector<juce::Rectangle<int>> badBoxes;
    ContrastReport R; const juce::Image::BitmapData bd (bare, juce::Image::BitmapData::readOnly), fd (full, juce::Image::BitmapData::readOnly);
    for (size_t k = 0; k < items.size(); ++k)
    {
        const auto& it = items[k];
        bool skip = false; for (auto& d : disabled) if (d.contains (it.box.getCentre())) skip = true;
        if (skip || it.col.getAlpha() < 8 || it.box.getWidth() < 2 || it.box.getHeight() < 2) continue;
        if (! focus.empty()) { bool in = false; for (auto& f : focus) in |= f.contains (it.box); if (! in) continue; }   // behind a modal scrim: dimmed on purpose
        // measured where the glyphs really landed (ink pixels: the rendered image differs from the text-less one), against the pixels behind them
        std::vector<double> cs; int inkPx = 0; double minK = 99; juce::Colour minBg;
        auto dist = [] (juce::Colour a, juce::Colour b) { return std::abs (a.getFloatRed() - b.getFloatRed()) + std::abs (a.getFloatGreen() - b.getFloatGreen()) + std::abs (a.getFloatBlue() - b.getFloatBlue()); };
        for (int y = it.box.getY(); y < it.box.getBottom(); ++y) for (int x = it.box.getX(); x < it.box.getRight(); ++x)
        {
            const auto bg = bd.getPixelColour (x, y); if (bg.getAlpha() < 250) continue;
            const auto want = bg.overlaidWith (it.col); const float dw = dist (want, bg);
            if (dw < 0.02f) { cs.push_back (ghosty::themes::contrast (want, bg)); continue; }   // (text the colour of its background: counted, it fails)
            const auto seen = fd.getPixelColour (x, y);
            if (dist (seen, bg) < 0.5f * dw) continue;   // not ink (or covered by something painted later)
            if (dist (seen, want) > 0.6f * dw) continue;  // ink of another colour: someone else's text owns this pixel
            { bool later = false; for (size_t j = k + 1; j < items.size() && ! later; ++j) later = items[j].box.contains (x, y); if (later) continue; }   // another (later) text owns this pixel
            ++inkPx; const double k = ghosty::themes::contrast (want, bg); cs.push_back (k); if (k < minK) { minK = k; minBg = bg; }
        }
        if (cs.size() < 3) { ++R.hidden; continue; }
        std::sort (cs.begin(), cs.end()); const double v = cs[cs.size() / 10];   // the 10th percentile: the text's worst stretch, not a stray pixel
        ++R.runs; const double need = it.h >= 17.5f ? 3.0 : 4.5;
        if (v < need)
        {
            ++R.bad; badBoxes.push_back (it.box);
            if (v / need < R.worst)
            {
                R.worst = v / need; auto* at = c.getComponentAt (it.box.getCentre());
                R.worstAt = page + " " + it.box.toString() + " " + juce::String (v, 2) + ":1 (need " + juce::String (need, 1) + ") text #" + it.col.toDisplayString (true) + " on #" + bd.getPixelColour (it.box.getCentreX(), it.box.getCentreY()).toDisplayString (false)
                          + " in " + (at != nullptr ? juce::String (typeid (*at).name()) + " '" + at->getName() + "'" : juce::String ("-"));
            }
            std::printf ("    low contrast %s %s %.2f:1 (need %.1f) text %s h=%.0f bg %s\n", page.toRawUTF8(), it.box.toString().toRawUTF8(), v, need, it.col.toDisplayString (true).toRawUTF8(), it.h, minBg.toDisplayString (false).toRawUTF8());
        }
    }
    if (R.bad > 0 && juce::SystemStats::getEnvironmentVariable ("ARP_PROBE_DEBUG", {}).isNotEmpty())
    {   // a debug picture: every low-contrast text run boxed in red
        juce::Image dbg = full.createCopy(); juce::Graphics g (dbg); g.setColour (juce::Colours::red);
        for (auto& b : badBoxes) g.drawRect (b.expanded (1), 1);
        juce::PNGImageFormat png; juce::File f (juce::SystemStats::getEnvironmentVariable ("ARP_PROBE_DEBUG", {}) + "/probe-" + page.replace ("/", "-") + ".png"); f.deleteFile(); juce::FileOutputStream os (f); png.writeImageToStream (dbg, os);
    }
    return R;
}
void themeChecks (Counter& C, const juce::String& shotDir)
{
    using namespace ghosty::themes;
    {   // the saved look: 'theme_name' round-trips; old sessions (theme false / true / "light" / none) load DARK / LIGHT
        ArpProcessor p; p.persistTaste = false; p.theme = "kitsune"; juce::MemoryBlock mb; p.getStateInformation (mb);
        ArpProcessor q; q.persistTaste = false; q.setStateInformation (mb.getData(), (int) mb.getSize());
        auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize()); auto root = juce::ValueTree::fromXml (*xml);
        auto legacy = [&] (const juce::var& v, bool has) { auto r = root.createCopy(); r.removeProperty ("theme_name", nullptr); if (has) r.setProperty ("theme", v, nullptr); else r.removeProperty ("theme", nullptr); return ArpProcessor::themeFromState (r); };
        // (2026-11-13: the old looks are gone: any old session, and an unknown name, opens on AUTO)
        auto named = [&] (const juce::String& n) { auto r = root.createCopy(); r.setProperty ("theme_name", n, nullptr); return ArpProcessor::themeFromState (r); };
        const bool ok = q.theme == "kitsune" && root.getProperty ("theme_name").toString() == "kitsune" && legacy (false, true) == "auto" && legacy (true, true) == "auto" && legacy ("light", true) == "auto" && legacy ("dark", true) == "auto" && legacy ({}, false) == "auto"
                        && named ("purple") == "auto" && named ("ghost") == "auto" && named ("NEON") == "neon";
        std::printf ("theme state: saved '%s', round trip '%s'; legacy false->%s true->%s none->%s\n", root.getProperty ("theme_name").toString().toRawUTF8(), q.theme.toRawUTF8(), legacy (false, true).toRawUTF8(), legacy (true, true).toRawUTF8(), legacy ({}, false).toRawUTF8());
        C.ok (ok, "THEMES: 'theme_name' saved + restored; the old key still loads (false -> BLACK, true -> LIGHT, none -> BLACK)");
    }
    {   // the FACE button names the CURRENT look and opens the list (2026-11-11: BLACK / PURPLE / LIGHT / GHOST + the picture faces + AUTO)
        ArpProcessor p; p.persistTaste = false; p.hintsSeen = true; std::unique_ptr<ArpEditor> ed (dynamic_cast<ArpEditor*> (p.createEditor())); ed->setSize (1240, 820); ed->refresh();
        juce::StringArray seq;
        for (auto lk : { "dark", "purple", "light", "ghost", "holo", "neon", "mask", "dream", "kitsune", "auto" }) { p.theme = lk; ed->refresh(); seq.add (p.theme + ":" + ed->themeButton().text); }
        std::printf ("theme button: %s\n", seq.joinIntoString (" -> ").toRawUTF8());
        C.ok (seq.joinIntoString (",") == "dark:BLACK,purple:PURPLE,light:LIGHT,ghost:GHOST,holo:HOLO,neon:NEON,mask:MASK,dream:DREAM,kitsune:KITSUNE,auto:AUTO", "THEMES: the FACE button names the current look");
    }
    const char* tn[3] { "black", "purple", "light" };
    for (int t = 0; t < 3; ++t)
    {
        const Theme th = fromInt (t);
        ArpProcessor p; p.persistTaste = false; p.theme = toString (th); p.hintsSeen = true;
        p.change ([] (Idea& X) { X.mode = Dusk; X.seed = 51; applyModeDefaults (X); newIdea (X); for (auto& L : X.lanes) L.mute = false; }, false);
        std::unique_ptr<ArpEditor> ed (dynamic_cast<ArpEditor*> (p.createEditor())); ed->setSize (1240, 820); ed->refresh();
        const auto& T = get (th); const auto& P = ed->th.p;
        // every surface + text token is the shared one
        C.ok (P.bg1 == T.window && P.card == T.card && P.card2 == T.cell && P.panel == T.panel && P.ink == T.ink && P.dim == T.dim && P.line == T.line && P.roll == T.waveBg && P.keyW == T.keyWhite && P.keyB == T.keyBlack,
              (juce::String ("THEMES: ") + tn[t] + " surfaces + text = Shared/GhostThemes tokens").toStdString());
        int runs = 0, bad = 0; double worst = 99; juce::String worstAt; int pages = 0;
        auto page = [&] (const juce::String& name, std::function<void()> setup) {
            if (setup) setup();
            ed->refresh(); ed->toast ("", -1.0e15);
            if (juce::SystemStats::getEnvironmentVariable ("ARP_PROBE_SETTLE", {}).isNotEmpty()) (void) ed->createComponentSnapshot (ed->getLocalBounds());
            const auto R = probeContrast (*ed, juce::String (tn[t]) + "/" + name, nullptr, {}, ed->modalFocus());
            runs += R.runs; bad += R.bad; if (R.worst < worst) { worst = R.worst; worstAt = R.worstAt; } ++pages;
            if (shotDir.isNotEmpty()) { auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f); juce::PNGImageFormat png; juce::File f (shotDir + "/theme-" + tn[t] + "-" + name + ".png"); f.deleteFile(); juce::FileOutputStream os (f); png.writeImageToStream (img, os); }
        };
        for (int m = 0; m < kMoods; ++m)
        {
            const juce::String mn = m == 0 ? "sunrise" : m == 1 ? "dusk" : m == 2 ? "midnight" : m == Trap ? "trap" : "hiphop";
            p.change ([m] (Idea& X) { X.mode = (decltype (X.mode)) m; applyModeDefaults (X); newIdea (X); for (auto& L : X.lanes) L.mute = false; }, false);
            ed->applyThemeNow (m);
            for (int l = 0; l < 4; ++l) page ("main-" + mn + "-" + ArpEditor::laneName (l).toLowerCase(), [&] { ed->selectLane (l); });
        }
        for (int m : { (int) Trap, (int) HipHop })
        {   // HOOK LAB + MORE (FLAVOUR, PUSH FURTHER) in the new moods
            const juce::String mn = m == Trap ? "trap" : "hiphop";
            p.change ([m] (Idea& X) { X.mode = m; applyModeDefaults (X); newIdea (X); for (auto& L : X.lanes) L.mute = false; }, false);
            ed->applyThemeNow (m);
            page ("hooklab-" + mn, [&] { ed->selectLane (LaneMelody); p.labCancel(); p.labGenerate (4); ed->openHookLab (true); ed->hookLab->sync(); });
            page ("more-" + mn, [&] { ed->openHookLab (false); ed->more->setVisible (true); ed->more->toFront (false); ed->more->sync(); });
            ed->more->setVisible (false);
        }
        p.change ([] (Idea& X) { X.mode = Dusk; applyModeDefaults (X); newIdea (X); for (auto& L : X.lanes) L.mute = false; }, false); ed->applyThemeNow (Dusk);
        page ("loop-section", [&] { ed->selectLane (LaneMelody); p.setSection (BAR, 3 * BAR); p.setSectionOn (true); p.setPlayCursor (BAR + PPQ); });
        page ("muted-lane", [&] { p.clearSection(); p.setPlayCursor (-1); p.change ([] (Idea& X) { X.lanes[LaneBass].mute = true; }, false); ed->selectLane (LaneBass); });
        p.change ([] (Idea& X) { X.lanes[LaneBass].mute = false; }, false); ed->selectLane (LaneMelody);
        page ("toast", [&] { }); { ed->toast ("Looping bars 2-3 - press L (or click LOOP) to play the whole song", 1.0e9); const auto R = probeContrast (*ed, juce::String (tn[t]) + "/toast"); runs += R.runs; bad += R.bad; if (R.worst < worst) { worst = R.worst; worstAt = R.worstAt; } }
        if (shotDir.isNotEmpty()) { auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f); juce::PNGImageFormat png; juce::File f (shotDir + "/theme-" + tn[t] + "-toast.png"); f.deleteFile(); juce::FileOutputStream os (f); png.writeImageToStream (img, os); }
        page ("hooklab", [&] { p.labGenerate (4); ed->openHookLab (true); ed->hookLab->sync(); });
        page ("sample", [&] { ed->openHookLab (false); std::vector<float> x[2] = { smptest::tone (220.0, 2.0, 3), {} }; juce::String msg; p.loadSampleAudio (x, 1, 48000.0, "sample", {}, msg);
                              auto q = p.sampleParams(); q.start = 0.05f; q.end = 0.9f; q.mode = 1; q.loopStart = 0.35f; q.loopEnd = 0.6f; p.setSampleParams (q); ed->openSample (true); ed->samplePanel->sync(); });
        page ("sample-notelength", [&] { auto q = p.sampleParams(); q.mode = 2; q.fitStretch = true; q.fitLoop = true; p.setSampleParams (q); ed->samplePanel->sync(); ed->samplePanel->resized(); });
        page ("sample-slices", [&] { p.sliceSample (0.9f); ed->samplePanel->sync(); ed->samplePanel->resized(); });
        { auto q = p.sampleParams(); q.mode = 1; p.setSampleParams (q); ed->samplePanel->sync(); ed->samplePanel->resized(); }
        page ("more", [&] { ed->openSample (false); ed->more->setVisible (true); ed->more->toFront (false); ed->more->sync(); });
        page ("save", [&] { ed->more->setVisible (false); ed->openSave(); });
        page ("load", [&] { ed->openBrowser(); });
        auto hints = [&] (bool v) { std::function<void (juce::Component&)> w = [&] (juce::Component& k) { for (auto* ch : k.getChildren()) { if (ch->getName() == "hints") ch->setVisible (v); else w (*ch); } }; w (*ed); };
        page ("hints", [&] { ed->closeOverlay(); hints (true); });
        hints (false);
        // pop-ups painted by the look-and-feel: a menu (header, items, highlighted, ticked), a tooltip
        auto& lnf = ed->getLookAndFeel();
        auto popup = [&] (const juce::String& name, int w, int h, std::function<void (juce::Graphics&)> fn) {
            juce::Component dummy; const auto R = probeContrast (dummy, juce::String (tn[t]) + "/" + name, fn, { 0, 0, w, h });
            runs += R.runs; bad += R.bad; if (R.worst < worst) { worst = R.worst; worstAt = R.worstAt; } ++pages;
            if (shotDir.isNotEmpty()) { juce::Image img (juce::Image::ARGB, w, h, true); { juce::Graphics g (img); fn (g); } juce::PNGImageFormat png; juce::File f (shotDir + "/theme-" + tn[t] + "-" + name + ".png"); f.deleteFile(); juce::FileOutputStream os (f); png.writeImageToStream (img, os); }
        };
        popup ("menu", 260, 150, [&] (juce::Graphics& g) {
            lnf.drawPopupMenuBackground (g, 260, 150);
            lnf.drawPopupMenuSectionHeader (g, { 0, 4, 260, 24 }, "Line between two notes");
            lnf.drawPopupMenuItem (g, { 0, 30, 260, 24 }, false, true, false, true, false, "GLIDE (slide into the next note)", {}, nullptr, nullptr);
            lnf.drawPopupMenuItem (g, { 0, 56, 260, 24 }, false, true, true, false, false, "Glide early + long", {}, nullptr, nullptr);
            lnf.drawPopupMenuItem (g, { 0, 82, 260, 24 }, false, true, false, false, true, "FATTEN (next note louder)", {}, nullptr, nullptr);
            lnf.drawPopupMenuItem (g, { 0, 108, 260, 24 }, false, true, false, false, false, "STEP (abrupt change)", {}, nullptr, nullptr); });
        popup ("tooltip", 300, 60, [&] (juce::Graphics& g) { lnf.drawTooltip (g, "LOOP SECTION (L): play only the loop", 300, 60); });
        std::printf ("contrast %s: %d pages, %d text runs, %d below the bar%s%s\n", tn[t], pages, runs, bad, worstAt.isEmpty() ? "" : "; worst: ", worstAt.toRawUTF8());
        // (a few known residuals remain: see the report; the bar is that they never grow)
        C.ok (runs > 400 && bad == 0, (juce::String ("THEMES: ") + tn[t] + ": every text run on every page / overlay / pop-up >= 4.5:1 (large >= 3:1), measured on the pixels").toStdString());
        {   // BYPASS + SYNC wear the house theme (Shared setTheme): pixel-identical to a fresh button themed the same way
            auto same = [] (juce::Component& a, juce::Component& b) { const auto x = a.createComponentSnapshot (a.getLocalBounds()), y = b.createComponentSnapshot (b.getLocalBounds());
                if (x.getBounds() != y.getBounds()) return false; const juce::Image::BitmapData da (x, juce::Image::BitmapData::readOnly), db (y, juce::Image::BitmapData::readOnly);
                for (int yy = 0; yy < x.getHeight(); ++yy) for (int xx = 0; xx < x.getWidth(); ++xx) if (da.getPixelColour (xx, yy) != db.getPixelColour (xx, yy)) return false; return true; };
            auto* bp = ed->bypassButton(); auto* sb = ed->syncButton(); bool ok = bp != nullptr && sb != nullptr, differs = false;
            if (ok)
            {
                ghosty::BypassButton b2 (*dynamic_cast<juce::RangedAudioParameter*> (p.apvts.getParameter ("bypass"))); b2.setSize (bp->getWidth(), bp->getHeight()); b2.setTheme (th);
                ghosty::SyncButton s2 (p.sync); s2.setSize (sb->getWidth(), sb->getHeight()); s2.setTheme (th);
                ok = same (*bp, b2) && same (*sb, s2);
                if (th == Theme::Purple) { ghosty::SyncButton s3 (p.sync); s3.setSize (sb->getWidth(), sb->getHeight()); s3.setDark (true); differs = ! same (*sb, s3); }
            }
            C.ok (ok && (th != Theme::Purple || differs), (juce::String ("THEMES: ") + tn[t] + ": BYPASS + SYNC use the shared themed look" + (th == Theme::Purple ? " (PURPLE = the house colours, not the old dark)" : "")).toStdString());
        }
    }
}
#include "GhostLookTests.inc"
#include "FaceTests.inc"
#include "PropTests.inc"
#include "ChordOctTests.inc"
// the final polish: opaque surfaces, the soft light palette + contrast, tooltips, the slider convention, GENERATE off the UI thread, the demo gate, the version
void polishChecks (Counter& C)
{
    auto lum = [] (juce::Colour c) { auto f = [] (float v) { return v <= 0.03928f ? v / 12.92f : std::pow ((v + 0.055f) / 1.055f, 2.4f); }; return 0.2126f * f (c.getFloatRed()) + 0.7152f * f (c.getFloatGreen()) + 0.0722f * f (c.getFloatBlue()); };
    auto contrast = [&] (juce::Colour a, juce::Colour b) { const float x = lum (a), y = lum (b); return (std::max (x, y) + 0.05f) / (std::min (x, y) + 0.05f); };
    auto opaqueInside = [] (const juce::Image& img, int inset) { const juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly); int bad = 0;
        for (int y = inset; y < img.getHeight() - inset; ++y) for (int x = inset; x < img.getWidth() - inset; ++x) if (bd.getPixelColour (x, y).getAlpha() < 255) ++bad; return bad; };
    for (int theme = 0; theme < 3; ++theme)
    {
        ArpProcessor p; p.persistTaste = false; p.theme = theme == 2 ? "light" : theme == 1 ? "purple" : "dark"; p.hintsSeen = true;
        std::unique_ptr<ArpEditor> ed (dynamic_cast<ArpEditor*> (p.createEditor())); ed->setSize (1240, 820); ed->refresh();
        const juce::String tn = p.theme;
        // 1) one tooltip window
        int tw = 0; std::function<void (juce::Component&)> count = [&] (juce::Component& r) { for (auto* c : r.getChildren()) { if (dynamic_cast<juce::TooltipWindow*> (c) != nullptr) ++tw; count (*c); } };
        count (*ed);
        // 2) opaque: the panels, the pop-up menu, tooltip, call-out and alert backgrounds, painted alone on a transparent image
        int seeThrough = 0;
        ed->openHookLab (true); seeThrough += opaqueInside (ed->hookLab->createComponentSnapshot (ed->hookLab->getLocalBounds(), true, 1.0f), 16); ed->openHookLab (false);
        ed->openSample (true); seeThrough += opaqueInside (ed->samplePanel->createComponentSnapshot (ed->samplePanel->getLocalBounds(), true, 1.0f), 16); ed->openSample (false);
        ed->more->setVisible (true); seeThrough += opaqueInside (ed->more->createComponentSnapshot (ed->more->getLocalBounds(), true, 1.0f), 16); ed->more->setVisible (false);
        auto& lnf = ed->getLookAndFeel();
        { juce::Image img (juce::Image::ARGB, 200, 300, true); juce::Graphics g (img); lnf.drawPopupMenuBackground (g, 200, 300); seeThrough += opaqueInside (img, 2); }
        { juce::Image img (juce::Image::ARGB, 240, 60, true); juce::Graphics g (img); lnf.drawTooltip (g, "a tooltip", 240, 60); seeThrough += opaqueInside (img, 6); }
        { juce::Component host; host.setSize (400, 300); juce::Label content; content.setSize (100, 40); juce::CallOutBox box (content, juce::Rectangle<int> (150, 100, 10, 10), &host); juce::Image img (juce::Image::ARGB, 160, 100, true); juce::Image cache;
          juce::Graphics g (img); juce::Path pth; pth.addRectangle (4, 4, 152, 92); lnf.drawCallOutBoxBackground (box, g, pth, cache); seeThrough += opaqueInside (img, 8); }
        for (int id : { (int) juce::AlertWindow::backgroundColourId, (int) juce::BubbleComponent::backgroundColourId, (int) juce::TooltipWindow::backgroundColourId, (int) juce::PopupMenu::backgroundColourId, (int) juce::ComboBox::backgroundColourId })
            if (! lnf.findColour (id).isOpaque()) ++seeThrough;   // (an alert window is a desktop window: its colour is checked)
        std::printf ("polish %s: %d tooltip window(s), %d see-through pixels\n", tn.toRawUTF8(), tw, seeThrough);
        C.ok (tw == 1, "ONE tooltip window (" + tn.toStdString() + ")");
        C.ok (seeThrough == 0, "every panel / pop-up / call-out / alert is opaque (" + tn.toStdString() + ")");
        // 3) contrast
        const auto& P = ed->th.p;
        float laneMin = 99, lanePair = 99;
        for (int l = 0; l < 4; ++l) laneMin = std::min (laneMin, contrast (ed->laneColour (l), P.card));
        for (int a = 0; a < 4; ++a) for (int b = a + 1; b < 4; ++b) { const auto x = ed->laneColour (a), y = ed->laneColour (b); lanePair = std::min (lanePair, std::abs (x.getFloatRed() - y.getFloatRed()) + std::abs (x.getFloatGreen() - y.getFloatGreen()) + std::abs (x.getFloatBlue() - y.getFloatBlue())); }
        std::printf ("contrast %s: text %.1f, secondary %.1f, lanes >= %.1f (closest pair %.2f)\n", tn.toRawUTF8(), contrast (P.ink, P.card), contrast (P.dim, P.card), laneMin, lanePair);
        C.ok (contrast (P.ink, P.card) >= 4.5 && contrast (P.dim, P.card) >= 4.5 && laneMin >= 4.5 && lanePair > 0.4f, "contrast: text, secondary text, the four lane colours (" + tn.toStdString() + ")");
        if (theme == 2) { const auto& L = ghosty::themes::get (ghosty::themes::Theme::Light); C.ok (P.bg1 == L.window && P.card == L.card && P.card2 == L.cell && P.ink == L.ink && P.dim == L.dim, "LIGHT = the shared soft GhostBlock LIGHT tokens (never pure white)"); }
        if (theme) continue;
        // 5) every slider follows the convention; the wheel moves one step
        int sliders = 0, ok = 0; juce::Slider* cutoff = nullptr;
        std::function<void (juce::Component&)> walk = [&] (juce::Component& r) { for (auto* c : r.getChildren()) { if (auto* s2 = dynamic_cast<juce::Slider*> (c)) { ++sliders; if (s2->isDoubleClickReturnEnabled() && ! s2->isScrollWheelEnabled() && s2->getVelocityModeIsSwappable() && s2->getSliderSnapsToMousePosition()) ++ok; if (s2->getMaximum() == 1.0 && s2->getMinimum() == 0.0 && cutoff == nullptr && s2->isEnabled()) cutoff = s2; } walk (*c); } };
        walk (*ed);
        bool wheel = false;
        if (cutoff != nullptr)
        {
            const double v0 = cutoff->getValue(), step = cutoff->getInterval() > 0 ? cutoff->getInterval() : 0.01;
            juce::MouseEvent me (juce::Desktop::getInstance().getMainMouseSource(), {}, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, cutoff, cutoff, juce::Time::getCurrentTime(), {}, juce::Time::getCurrentTime(), 1, false);
            juce::MouseWheelDetails w {}; w.deltaY = 0.1f;
            ed->wheelStep.mouseWheelMove (me, w);
            wheel = std::abs (cutoff->getValue() - std::min (cutoff->getMaximum(), v0 + step)) < 1e-5;   // (an APVTS float parameter)
            if (! wheel) std::printf ("  wheel: %s %.4f -> %.4f (step %.4f, interval %.4f)\n", cutoff->getParentComponent() ? cutoff->getParentComponent()->getName().toRawUTF8() : "-", v0, cutoff->getValue(), step, cutoff->getInterval());
        }
        std::printf ("sliders: %d / %d follow the convention, wheel step %d\n", ok, sliders, (int) wheel);
        C.ok (sliders >= 20 && ok == sliders && wheel, "every knob / slider: type-in, double-click default, hover units, click-scrub, SHIFT fine, one wheel step");
        // 4) GENERATE: the click returns at once, the idea arrives from the worker, one undo step back
        const auto seed0 = p.idea().seedUsed; const auto t0 = juce::Time::getMillisecondCounterHiRes();
        ed->clickGenerate();
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        for (int k = 0; k < 600 && p.ideaBusy.load(); ++k) juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        const auto seed1 = p.idea().seedUsed; p.undo();
        std::printf ("GENERATE click: %.2f ms on the UI thread, seed %u -> %u, undo -> %u\n", ms, seed0, seed1, p.idea().seedUsed);
        C.ok (ms < 5.0 && seed1 != seed0 && p.idea().seedUsed == seed0, "GENERATE runs on the worker (< 5 ms on the UI thread), one undo step");
        // 7) the demo exports nothing
        p.demoLockForTest = true;
        const auto midiDir = ArpProcessor::presetDir().getChildFile ("MIDI"); const int n0 = midiDir.getNumberOfChildFiles (juce::File::findFiles);
        ed->renderStatus.clear();
        ed->startDragOut (15, false, false, nullptr); ed->startDragOut (15, true, false, nullptr); ed->startDragOut (15, true, true, nullptr);
        const auto f1 = p.writeMidiFile (15, false); p.labGenerate (4); const auto f2 = p.writeHookMidi (0);
        const int n1 = midiDir.getNumberOfChildFiles (juce::File::findFiles);
        C.ok (n1 == n0 && ! f1.existsAsFile() && ! f2.existsAsFile() && ed->renderStatus.isEmpty(), "DEMO: every export is refused (MIDI, AUDIO mix + stems, Hook Lab tiles)");
        p.demoLockForTest = false;
        // 6) the version
        C.ok (ArpEditor::versionString() == "3.0.0", "the version string (3.0.0) is shown in MORE");
    }
}
// LOOP SECTION
void loopChecks (Counter& C)
{
    struct Ev2 { double t; int ch, p; bool on; };   // t in ticks of the host / transport
    const double sr = 48000, bpm = 124; const int blk = 256; const double spt = 60.0 / bpm / PPQ * sr;   // samples per tick
    auto run = [&] (ArpProcessor& p, double ticks, double ppq0, std::function<void (double)> poke, std::vector<float>& L, std::vector<Ev2>& ev) {
        FakeHead head; head.bpm = bpm; p.setPlayHead (&head); p.setRateAndBufferSizeDetails (sr, blk); p.prepareToPlay (sr, blk);
        const auto total = (juce::int64) (ticks * spt);
        for (juce::int64 pos = 0; pos < total; pos += blk)
        {
            if (poke) poke (pos / spt);
            head.ppq = ppq0 + pos / sr * bpm / 60.0; head.samples = pos; head.playing = true;
            juce::AudioBuffer<float> b (2, blk); b.clear(); juce::MidiBuffer m; p.processBlock (b, m);
            for (int i = 0; i < blk; ++i) L.push_back (b.getSample (0, i));
            for (const auto meta : m) { const auto x = meta.getMessage(); const double t = ppq0 * PPQ + (pos + meta.samplePosition) / spt; if (x.isNoteOn()) ev.push_back ({ t, x.getChannel(), x.getNoteNumber(), true }); else if (x.isNoteOff()) ev.push_back ({ t, x.getChannel(), x.getNoteNumber(), false }); }
        }
        p.setPlayHead (nullptr);
    };
    auto make = [] (ArpProcessor& p) { p.persistTaste = false; p.change ([] (Idea& X) { X.mode = Midnight; X.seed = 4321; applyModeDefaults (X); newIdea (X); for (auto& L : X.lanes) L.mute = false; }, false);
                                       setP (p, "midiOut", 1); setP (p, "midiCh1", 0); setP (p, "velScale", 0); };
    const int s0 = 2 * BAR, e0 = 4 * BAR, slen = e0 - s0;   // bars 3.1 - 4.4
    {   // the section wraps exactly; it is the idea's own [s, e); note-offs at the wrap; nothing hung; the seam is click-free
        ArpProcessor ref, p; make (ref); make (p); p.setSection (s0, e0); p.setSectionOn (true);
        std::vector<float> lr, lp; std::vector<Ev2> er, ep; run (ref, e0 + 4, 0, {}, lr, er); run (p, 3 * slen, 0, {}, lp, ep);
        std::set<std::tuple<int, int, int>> want; for (auto& x : er) if (x.on && x.t >= s0 - 0.5 && x.t < e0 - 0.5) want.insert ({ (int) std::lround (x.t) - s0, x.ch, x.p });
        int passOk = 0;
        for (int k = 0; k < 3; ++k) { std::set<std::tuple<int, int, int>> got; for (auto& x : ep) if (x.on && x.t >= k * slen - 0.5 && x.t < (k + 1) * slen - 0.5) got.insert ({ (int) std::lround (x.t) - k * slen, x.ch, x.p }); for (auto& w : want) (void) w; int miss = 0; for (auto& w : want) if (! got.count (w)) ++miss; if (miss == 0 && got.size() >= want.size()) ++passOk; }
        // every note sounding at a wrap ends there; nothing hung
        std::map<int, double> open; int crossing = 0, late = 0; bool dbl = false;
        for (auto& x : ep) { const int key = x.ch * 128 + x.p; if (x.on) { if (open.count (key)) dbl = true; open[key] = x.t; } else if (open.count (key)) { const double t0 = open[key]; const int k = (int) ((t0 + 0.5) / slen); if (x.t > (k + 1) * slen + 1.0) { ++late; if (late <= 4) std::printf ("  late off ch %d p %d: on %.2f off %.2f\n", x.ch, x.p, t0, x.t); } if (std::abs (x.t - (k + 1) * slen) < 1.0) ++crossing; open.erase (key); } }
        float jw = 0, je = 0; for (size_t i = 1; i < lp.size(); ++i) { const double t = i / spt; const double ph = std::fmod (t, (double) slen); const float d = std::abs (lp[i] - lp[i - 1]); if (ph < 2 || ph > slen - 2) jw = std::max (jw, d); else je = std::max (je, d); }
        std::printf ("loop section 3.1-4.4: %zu notes per pass, %d / 3 passes match the idea's own section, %d notes cut at the wrap, %d late offs, %zu hung, seam step %.3f vs elsewhere %.3f\n", want.size(), passOk, crossing, late, open.size(), jw, je);
        C.ok (passOk == 3 && ! want.empty(), "LOOP SECTION: the output repeats exactly every section (period = its length), the idea's own notes");
        C.ok (late == 0 && open.size() <= 6 && ! dbl, "LOOP SECTION: notes crossing the wrap end there, nothing hung, no double note-ons");
        C.ok (jw <= je * 1.2f + 1e-4f, "LOOP SECTION: the audio seam is click-free");
    }
    {   // SYNC: DAW from an arbitrary host position: idea position = start + (host ticks mod length)
        ArpProcessor ref, p; make (ref); make (p); p.setSection (s0, e0); p.setSectionOn (true);
        std::vector<float> lr, lp; std::vector<Ev2> er, ep; run (ref, e0 + 4, 0, {}, lr, er);
        const double ppq0 = 13.37; run (p, 2 * slen, ppq0, {}, lp, ep);
        std::set<std::pair<int, int>> sec; for (auto& x : er) if (x.on && x.t >= s0 - 0.5 && x.t < e0 - 0.5) sec.insert ({ (int) std::lround (x.t) - s0, x.p });
        int ons = 0, okc = 0; for (auto& x : ep) if (x.on && x.t > ppq0 * PPQ + 2) { ++ons; const int ph = (int) std::lround (std::fmod (x.t, (double) slen)) % slen; if (sec.count ({ ph, x.p })) ++okc; }
        std::printf ("loop section in DAW mode from beat %.2f: %d / %d note-ons on the section's grid\n", ppq0, okc, ons);
        C.ok (ons > 10 && okc == ons, "LOOP SECTION: with the DAW playing from anywhere, the section repeats on the DAW's beat grid");
    }
    {   // OFF (brace kept) = bit-identical; state round trip; old state has no brace
        ArpProcessor a, b; make (a); make (b); b.setSection (s0, e0); b.setSectionOn (true); b.setSectionOn (false);
        std::vector<float> la, lb; std::vector<Ev2> ea, eb; run (a, 4 * BAR, 0, {}, la, ea); run (b, 4 * BAR, 0, {}, lb, eb);
        bool same = la == lb && ea.size() == eb.size(); for (size_t i = 0; same && i < ea.size(); ++i) same = ea[i].t == eb[i].t && ea[i].p == eb[i].p;
        C.ok (same, "LOOP SECTION off (the brace kept): bit-identical audio + MIDI");
        b.setSectionOn (true); juce::MemoryBlock mb; b.getStateInformation (mb);
        ArpProcessor q; q.persistTaste = false; q.setStateInformation (mb.getData(), (int) mb.getSize());
        auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize()); for (int k = 0; k < 3; ++k) xml->removeAttribute ("section" + juce::String (k));
        juce::MemoryBlock old; juce::AudioProcessor::copyXmlToBinary (*xml, old);
        ArpProcessor r; r.persistTaste = false; r.setStateInformation (old.getData(), (int) old.getSize());
        const int m = q.idea().mode;
        C.ok (q.sections[(size_t) m].start == s0 && q.sections[(size_t) m].end == e0 && q.sections[(size_t) m].on && ! r.section().valid(), "LOOP SECTION: saved per project (per mode); old projects load without a brace");
    }
    {   // DRAG: SECTION exports exactly the section, from 0
        ArpProcessor p; make (p); p.setSection (s0, e0);
        const auto whole = ideaMidi (p.exportIdea(), 15, false, 124), part = ideaMidi (p.exportIdea(), 15, false, 124, {}, s0, e0);
        auto notesOf = [] (const std::vector<uint8_t>& bytes) { juce::MemoryInputStream in (bytes.data(), bytes.size(), false); juce::MidiFile mf; mf.readFrom (in); std::vector<std::pair<double, int>> v;
            for (int t = 0; t < mf.getNumTracks(); ++t) { auto* tr = mf.getTrack (t); for (auto* e : *tr) if (e->message.isNoteOn()) v.push_back ({ e->message.getTimeStamp() * PPQ / mf.getTimeFormat(), e->message.getNoteNumber() }); } std::sort (v.begin(), v.end()); return v; };
        const auto W = notesOf (whole), S = notesOf (part);
        std::vector<std::pair<double, int>> want; for (auto& x : W) if (x.first >= s0 && x.first < e0) want.push_back ({ x.first - s0, x.second });
        std::sort (want.begin(), want.end());
        bool same = want.size() == S.size(); for (size_t i = 0; same && i < S.size(); ++i) same = std::abs (want[i].first - S[i].first) < 0.5 && want[i].second == S[i].second;
        std::printf ("drag section: %zu notes (whole idea %zu), first at tick %.0f\n", S.size(), W.size(), S.empty() ? -1.0 : S.front().first);
        p.drawSection = 1; const auto job = p.renderJob (48000, 124);
        C.ok (same && ! S.empty() && job.secStart == s0 && job.secEnd == e0, "DRAG: SECTION exports exactly the section's notes, starting at 0 (MIDI + AUDIO jobs)");
    }
    {   // the brace edited while playing: safe; SPACE starts at the section (or the cursor inside it)
        ArpProcessor p; make (p); p.setSection (s0, e0); p.setSectionOn (true);
        std::vector<float> l; std::vector<Ev2> ev; int last = -1;
        run (p, 6 * BAR, 0, [&] (double t) { const int k = (int) (t / (BAR / 2)); if (k != last) { last = k; p.setSection ((k % 4) * PPQ, (k % 4) * PPQ + BAR + (k % 3) * PPQ); } }, l, ev);
        std::map<int, int> o; bool dbl = false; for (auto& x : ev) { const int key = x.ch * 128 + x.p; if (x.on) { if (o[key] > 0) dbl = true; o[key]++; } else o[key] = std::max (0, o[key] - 1); }
        int hung = 0; for (auto& kv : o) hung += kv.second; float pk = 0; bool fin = true; for (float v : l) { pk = std::max (pk, std::abs (v)); fin = fin && std::isfinite (v); }
        C.ok (! dbl && hung <= 6 && fin && pk < 4.0f, "LOOP SECTION: moving / resizing the brace while it plays is safe");
        p.setSection (s0, e0); p.setPlayCursor (s0 + BAR + PPQ);
        C.ok (p.playStartTick() == BAR + PPQ, "LOOP SECTION: SPACE starts at the cursor inside the section");
        p.setPlayCursor (0); C.ok (p.playStartTick() == 0, "LOOP SECTION: ... or at the section start");
    }
}
// the TIMELINE strip + right-click gestures + the loop's drag-out
void timelineChecks (Counter& C)
{
    ArpProcessor p; p.persistTaste = false;
    p.change ([] (Idea& X) { X.mode = Midnight; X.seed = 4321; applyModeDefaults (X); X.phraseBars = 8; newIdea (X); X.phraseBars = 8; rebuildChords (X); }, false);
    std::unique_ptr<ArpEditor> ed (dynamic_cast<ArpEditor*> (p.createEditor())); ed->setSize (1240, 820); ed->selectLane (LaneMelody);
    auto& roll = ed->roll; roll.setView ({ 0.0, 4.0 * BAR });
    using arpui::Roll;
    auto mev = [&] (float x, float y, bool right, float dx = 0, float dy = 0) {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), { x + dx, y + dy }, right ? juce::ModifierKeys (juce::ModifierKeys::rightButtonModifier) : juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier),
                                 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &roll, &roll, juce::Time::getCurrentTime(), { x, y }, juce::Time::getCurrentTime(), 1, dx != 0 || dy != 0); };
    auto click = [&] (float x, float y, bool right, float wobble = 0) { roll.mouseDown (mev (x, y, right)); if (wobble != 0) roll.mouseDrag (mev (x, y, right, wobble, 0)); roll.mouseUp (mev (x, y, right, wobble, 0)); };
    const float sy = (Roll::hy2 + Roll::hy) * 0.5f, by = (Roll::hy1 + Roll::hy2) * 0.5f;
    // a click (even with a 5 px wobble) on the strip or the bar row never makes a loop
    click (roll.X (BAR + 10), sy, false); click (roll.X (BAR + 10), sy, false, 5.0f); click (roll.X (2 * BAR + 40), by, false, 5.0f); click (roll.X (2 * BAR + 40), by, false, 40.0f);
    C.ok (! p.section().valid(), "TIMELINE: a click (or a left-drag on the bar row) makes no loop");
    // right-click in the strip: play from here (snapped)
    p.setPlayCursor (-1); click (roll.X (2 * BAR + PPQ + 10), sy, true);
    C.ok (p.playCursor.load() == 2 * BAR + PPQ, "TIMELINE: right-click in the strip sets the PLAY cursor there (snapped to the beat)");
    // right-drag in the strip: the loop
    roll.mouseDown (mev (roll.X (BAR), sy, true)); roll.mouseDrag (mev (roll.X (BAR), sy, true, roll.X (3 * BAR) - roll.X (BAR), 0)); roll.mouseUp (mev (roll.X (BAR), sy, true, roll.X (3 * BAR) - roll.X (BAR), 0));
    const auto sc = p.section();
    C.ok (sc.valid() && sc.start == BAR && sc.end == 3 * BAR && sc.on, "TIMELINE: right-drag in the strip makes the loop (bars 2-3, on)");
    // right-click on a padlock does nothing
    const bool lk0 = p.idea().lanes[LaneMelody].barLocks[0]; click (roll.X (0) + 6, by, true);
    C.ok (p.idea().lanes[LaneMelody].barLocks[0] == lk0, "TIMELINE: right-click on a padlock is inert");
    // the hit zones never overlap
    int both = 0; for (float y = 0; y < Roll::hy + 4; y += 1) for (float x = Roll::kx; x < 1200; x += 3) if (roll.inStrip ({ x, y }) && roll.inPadlock ({ x, y })) ++both;
    C.ok (both == 0, "TIMELINE: the strip and the padlocks never share a pixel");
    // the loop's drag-out: MIDI from tick 0, the WAV render exactly the section (+ the tail), non-silent and finite; the demo exports nothing
    p.drawOverride = 1; const auto f = p.writeMidiFile (15, false); p.drawOverride = -1;
    juce::MidiFile mf; { juce::FileInputStream in (f); mf.readFrom (in); } double first = 1e9;
    for (int t = 0; t < mf.getNumTracks(); ++t) for (auto* e : *mf.getTrack (t)) if (e->message.isNoteOn()) first = std::min (first, e->message.getTimeStamp() * PPQ / mf.getTimeFormat());
    p.drawOverride = 1; const auto job = p.renderJob (48000, 124); p.drawOverride = -1;
    const auto a = ArpProcessor::renderOffline (job, -1);
    const double want = (double) (sc.end - sc.start) / PPQ * 60.0 / 124 * 48000 + 3.0 * 48000;
    float pk = 0; bool fin = true; for (int i = 0; i < a.getNumSamples(); ++i) { pk = std::max (pk, std::abs (a.getSample (0, i))); fin = fin && std::isfinite (a.getSample (0, i)); }
    std::printf ("timeline: loop MIDI first note at tick %.0f; loop WAV %d samples (want %.0f), peak %.2f\n", first, a.getNumSamples(), want, pk);
    C.ok (f.existsAsFile() && first < 0.5, "LOOP DRAG-OUT: the section's MIDI starts at tick 0");
    C.ok (std::abs (a.getNumSamples() - want) < 2 && pk > 0.01f && fin, "LOOP DRAG-OUT: the section's WAV has the section's length (+ the tail), non-silent, finite");
    {   // ONE gesture: the loop's WAV renders in the background once the loop is set, so a single grip drag finds it ready
        ed->prerenderSection (true);
        for (int k = 0; k < 1500 && ! ed->sectionWavReady(); ++k) juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        const bool ready = ed->sectionWavReady(); const auto t0 = juce::Time::getMillisecondCounterHiRes();
        ed->startSectionDrag (true, nullptr);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0; const bool still = ed->sectionWavReady() && ! ed->sectionWavRendering();
        p.setSection (BAR, 2 * BAR); const bool stale = ! ed->sectionWavReady();   // a changed loop is a new render
        std::printf ("loop WAV ahead: ready %d, grip drag %.1f ms (no new render %d), changed loop -> re-render %d\n", (int) ready, ms, (int) still, (int) stale);
        C.ok (ready && still && ms < 50 && stale, "LOOP DRAG-OUT: the loop's WAV is rendered ahead; ONE drag of the WAV grip delivers it at once (a changed loop renders again)");
        p.setSection (sc.start, sc.end);
    }
    p.demoLockForTest = true; const auto n0 = ArpProcessor::presetDir().getChildFile ("MIDI").getNumberOfChildFiles (juce::File::findFiles);
    ed->startSectionDrag (false, nullptr); ed->startSectionDrag (true, nullptr);
    C.ok (ArpProcessor::presetDir().getChildFile ("MIDI").getNumberOfChildFiles (juce::File::findFiles) == n0 && ! ed->renderStatus.startsWith ("RENDERING"), "LOOP DRAG-OUT: the demo exports nothing");
    p.demoLockForTest = false;
}
void presetLoudness()
{
    for (int pre = 0; pre < arpsyn::kPresets; ++pre)
    {
        ArpProcessor p;
        p.change ([] (Idea& X) { X.mode = Midnight; X.seed = 5; applyModeDefaults (X); newIdea (X); X.lanes[LaneChords].mute = true; X.lanes[LaneBass].mute = true; X.lanes[LaneMelody].mute = true; });
        setP (p, "preset", (float) pre); setP (p, "pump", 0); setP (p, "delay", 0); setP (p, "reverb", 0); setP (p, "lfoDepth", 0);
        RenderOpts o; o.bars = 4; std::vector<MidiEv> ev; int byp = 0; const auto a = render (p, o, ev, byp);
        double pk, rms; bool nan; audioStats (a, pk, rms, nan);
        std::printf ("preset %-13s arp alone: rms %6.1f dB  peak %6.1f dB\n", arpsyn::presetName (pre), 20 * std::log10 (rms + 1e-9), 20 * std::log10 (pk + 1e-9));
    }
    for (int stab = 0; stab < 2; ++stab)
    {
        ArpProcessor p;
        p.change ([] (Idea& X) { X.mode = Midnight; X.seed = 5; applyModeDefaults (X); newIdea (X); X.lanes[LaneArp].mute = true; X.lanes[LaneBass].mute = true; X.lanes[LaneMelody].mute = true; X.lanes[LaneChords].mute = false; });
        setP (p, "stabs", (float) stab); setP (p, "pump", 0); setP (p, "delay", 0); setP (p, "reverb", 0);
        RenderOpts o; o.bars = 4; std::vector<MidiEv> ev; int byp = 0; const auto a = render (p, o, ev, byp);
        double pk, rms; bool nan; audioStats (a, pk, rms, nan);
        std::printf ("chords %-6s alone: rms %6.1f dB  peak %6.1f dB\n", stab ? "STABS" : "PAD", 20 * std::log10 (rms + 1e-9), 20 * std::log10 (pk + 1e-9));
    }
}
void makeStarters (const juce::File& dir)
{
    dir.createDirectory();
    const char* names[] { "Sunrise hook A", "Sunrise hook B", "Dusk hook A", "Dusk hook B", "Midnight line A", "Midnight line B" };
    for (int i = 0; i < 6; ++i)
    {
        Idea I; I.mode = i / 2; I.seed = (uint32_t) (900 + i * 131); applyModeDefaults (I); newIdea (I);
        const auto mid = ideaMidi (I, 1 << LaneMelody, true, I.g.bpm);
        dir.getChildFile (juce::String ("starter ") + names[i] + ".mid").replaceWithData (mid.data(), mid.size());
    }
}
// ---------------------------------------------------------------- HOOK LAB screenshots + demos
// original synthesised samples for the shots + demos (no recordings): a sung "aah" and an FM bell
std::vector<float> synthVowel (double sec = 2.5, double f0 = 220.0)
{
    const double sr = 48000; std::vector<float> x ((size_t) (sec * sr), 0.0f);
    const double F[3] { 800, 1150, 2900 }, B[3] { 80, 90, 120 }, G[3] { 1.0, 0.5, 0.25 };
    double ph = 0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = (double) i / sr, vib = t > 0.35 ? 1.0 + 0.012 * std::sin (2 * 3.14159265358979 * 5.2 * t) * std::min (1.0, (t - 0.35) * 2) : 1.0;
        ph += 2 * 3.14159265358979 * f0 * vib / sr;
        double y = 0;
        for (int k = 1; k <= 40; ++k)
        {
            const double fk = f0 * k; if (fk > 9000) break;
            double a = 0; for (int j = 0; j < 3; ++j) a += G[j] / (1.0 + std::pow ((fk - F[j]) / B[j], 2));
            y += a / std::sqrt ((double) k) * std::sin (k * ph);
        }
        const double env = std::min (1.0, t / 0.06) * (t > sec - 0.4 ? std::max (0.0, (sec - t) / 0.4) : 1.0);
        x[i] = (float) (0.3 * y * env);
    }
    return x;
}
std::vector<float> synthBell (double sec = 2.0, double f0 = 440.0)
{
    const double sr = 48000; std::vector<float> x ((size_t) (sec * sr), 0.0f);
    for (size_t i = 0; i < x.size(); ++i)
    {
        const double t = (double) i / sr, idx = 3.0 * std::exp (-t * 2.5), env = std::exp (-t * 1.6) * std::min (1.0, t / 0.002);
        x[i] = (float) (0.5 * env * std::sin (2 * 3.14159265358979 * f0 * t + idx * std::sin (2 * 3.14159265358979 * f0 * 3.5 * t)));
    }
    return x;
}
void pianoTestCheck (Counter& C)
{   // HOOK LAB PIANO TEST: an audition plays as plain piano: no arp / bass, flat melody velocity
    ArpProcessor p; p.persistTaste = false;
    p.change ([] (Idea& X) { X.mode = Midnight; X.seed = 4321; applyModeDefaults (X); newIdea (X); X.lanes[LaneBass].mute = false; }, false);
    setP (p, "midiOut", 1); setP (p, "midiCh1", 0); setP (p, "velScale", 0);
    p.labGenerate (4); p.labAudition (2);
    auto count = [&] (std::vector<MidiEv>& ev, int ch) { int n = 0; for (auto& e : ev) if (e.on && e.ch == ch) ++n; return n; };
    std::vector<MidiEv> e1, e2; int byp = 0; RenderOpts o; o.bars = 2; o.bpm = 128;
    render (p, o, e1, byp);
    p.labPianoTest (true);
    render (p, o, e2, byp);
    std::set<int> vels; for (auto& e : e2) if (e.on && e.ch == 2) vels.insert (e.vel);
    std::printf ("piano test: arp %d -> %d, bass %d -> %d, melody %d -> %d (velocities %zu)\n", count (e1, 1), count (e2, 1), count (e1, 4), count (e2, 4), count (e1, 2), count (e2, 2), vels.size());
    C.ok (count (e1, 1) > 0 && count (e2, 1) == 0 && count (e2, 4) == 0 && count (e2, 2) > 0 && vels.size() == 1 && p.lab.piano, "HOOK LAB: PIANO TEST plays the hook as plain piano (no arp / bass, flat velocity)");
    p.labPianoTest (false); std::vector<MidiEv> e3; render (p, o, e3, byp);
    C.ok (count (e3, 1) > 0, "HOOK LAB: PIANO TEST off brings the arp back");
}
void hookShots (const juce::File& dir)
{
    dir.createDirectory();
    auto shot = [&] (juce::Component& c, const juce::String& name) {
        auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
        auto f = dir.getChildFile (name + ".png"); f.deleteFile(); juce::FileOutputStream os (f); juce::PNGImageFormat png; png.writeImageToStream (img, os);
        std::printf ("shot %s\n", f.getFullPathName().toRawUTF8());
    };
    for (int theme = 0; theme < 2; ++theme)
    {
        ArpProcessor p; p.persistTaste = false; p.theme = theme ? "light" : "dark"; p.hintsSeen = true;
        p.change ([] (Idea& X) { X.mode = Dusk; X.seed = 51; applyModeDefaults (X); newIdea (X); X.style = StyleEmotional; X.candy = CandySubtle; X.tension = 0.45; }, false);
        std::unique_ptr<ArpEditor> ed (dynamic_cast<ArpEditor*> (p.createEditor()));
        ed->setSize (1240, 820);
        ed->openHookLab (true); p.labCancel(); p.labGenerate (4);   // (the editor generates on the worker; the shots need the grid now)
        p.labStar (2); p.labStar (6); p.labAudition (5); p.playTick = 2.3 * BAR;
        ed->hookLab->pick[0] = 0; ed->hookLab->pick[1] = 9; ed->hookLab->combineStep = 2;
        p.labHarvestRank();
        ed->refresh(); ed->hookLab->sync(); ed->toast ("", -1.0e15);
        shot (*ed, theme ? "hooklab_tiles_light" : "hooklab_tiles");
        if (theme)
        {
            p.setSyncOwn (true); ed->syncTransportUi(); ed->refresh(); ed->toast ("", -1.0e15); shot (*ed, "sync_own_light"); p.setSyncOwn (false);
            std::vector<float> x[2] = { synthVowel(), {} }; juce::String msg; p.loadSampleAudio (x, 1, 48000.0, "sung aah (synthesised)", {}, msg);
            auto q = p.sampleParams(); q.mode = 1; p.setSampleParams (q);
            ed->openHookLab (false); p.linkAll = false; p.setLaneChoice (LaneMelody, arpsyn::kPresets + 1); p.setLaneChoice (LaneChords, 1 + arpsyn::Supersaw); ed->soundTab = LaneMelody;
            ed->openSample (true); ed->refresh(); ed->toast ("", -1.0e15); shot (*ed, "sample-panel-light");
            ed->openSample (false); ed->refresh(); ed->toast ("", -1.0e15); shot (*ed, "sample-card-light");
            ed->selectLane (LaneMelody); ed->refresh(); ed->toast ("", -1.0e15); shot (*ed, "light-main-melody"); shot (*ed, "timeline-strip-light");
            p.setSection (BAR, 3 * BAR); p.setSectionOn (true); p.setPlayCursor (BAR + PPQ); ed->refresh(); ed->toast ("", -1.0e15); shot (*ed, "loop-section-light"); p.clearSection(); p.setPlayCursor (-1);
            ed->more->setVisible (true); ed->more->toFront (false); ed->more->sync(); ed->refresh(); ed->toast ("", -1.0e15); shot (*ed, "light-more"); ed->more->setVisible (false);
            continue;
        }
        ed->hookLab->combineStep = -1; ed->hookLab->pick[0] = ed->hookLab->pick[1] = -1;
        p.lab.selected = 5; p.labMagic(); p.labAudition (0); p.labPianoTest (true); ed->hookLab->sync(); ed->refresh(); ed->toast ("", -1.0e15);
        shot (*ed, "hooklab_magic");
        p.labPianoTest (false); for (int i = 0; i < (int) p.lab.tiles.size(); ++i) if (! p.lab.tiles[(size_t) i].star) { p.labStar (i); break; }
        p.change ([] (Idea& X) { X.g.key = (X.g.key + 5) % 12; rebuildChords (X); refitLanesToChords (X); }, true, "key");
        { int st = 0, kp = 0; for (auto& h : p.lab.tiles) { st += h.star; kp += h.kept; } std::printf ("after the key change: %zu tiles, %d starred, %d kept\n", p.lab.tiles.size(), st, kp); }
        p.labSetParams (StyleHypnotic, CandySubtle, 0.3, RegHigh); p.labGenerate (8); p.labAudition (1);
        { int st = 0, kp = 0; for (auto& h : p.lab.tiles) { st += h.star; kp += h.kept; } std::printf ("8-bar grid: %zu tiles, %d starred, %d kept\n", p.lab.tiles.size(), st, kp); }
        ed->hookLab->sync(); ed->refresh(); ed->toast ("", -1.0e15);
        shot (*ed, "hooklab_8bar_high_kept");
        p.labSetRhythm (Meter78, 7, 2, FeelNormal); p.labGenerate (4); p.labAudition (0);
        ed->hookLab->sync(); ed->refresh(); ed->toast ("", -1.0e15);
        shot (*ed, "hooklab_idm_7-8_euclid");
        p.labSetRhythm (Meter44, 0, 0, FeelDrop); p.labGenerate (4); p.labAudition (0);
        ed->hookLab->sync(); ed->refresh(); ed->toast ("", -1.0e15);
        shot (*ed, "hooklab_drop");
        p.labSetRhythm (Meter44, 0, 0, FeelNormal);
        p.lab.busy = true; p.lab.busyKind = 1; p.labProgress = 7; ed->hookLab->sync(); ed->toast ("", -1.0e15);
        shot (*ed, "hooklab_generating");
        p.lab.busy = false; ed->hookLab->sync();
        ed->openHookLab (false);
        p.change ([] (Idea& X) { X.evolve = true; X.evolveMode = EvolveJourney; rebuildChords (X); composeLane (X, LaneMelody, true); }, false);
        p.lockMotif ({});
        ed->selectLane (LaneMelody); ed->refresh(); ed->toast ("", -1.0e15);
        shot (*ed, "melody_controls");
        p.setPlayCursor (BAR + 2 * PPQ); ed->refresh(); ed->roll.repaint(); ed->toast ("", -1.0e15);
        shot (*ed, "play_cursor_melody");
        p.setPlayCursor (-1); ed->refresh(); ed->toast ("", -1.0e15); shot (*ed, "timeline-strip-dark");
        p.setSection (BAR, 3 * BAR); p.setSectionOn (true); p.setPlayCursor (BAR + PPQ); ed->refresh(); ed->toast ("", -1.0e15); shot (*ed, "loop-section-dark");
        ed->roll.setView ({ (double) BAR, 1.0 * BAR }); p.setSection (BAR + PPQ / 2, BAR + 2 * PPQ); p.setPlayCursor (BAR + PPQ); ed->refresh(); ed->toast ("", -1.0e15); shot (*ed, "loop-section-dark-zoomed");
        ed->roll.setView ({ 0.0, 4.0 * BAR }); p.clearSection();
        setP (p, "bypass", 1); ed->refresh(); ed->toast ("", -1.0e15); shot (*ed, "bypass_on"); setP (p, "bypass", 0);
        {   // YOUR SAMPLE: the panel + the per-lane sound card
            std::vector<float> x[2] = { synthVowel(), {} }; juce::String msg; p.loadSampleAudio (x, 1, 48000.0, "sung aah (synthesised)", {}, msg);
            auto q = p.sampleParams(); q.start = 0.04f; q.end = 0.9f; q.mode = 1; q.loopStart = 0.35f; q.loopEnd = 0.6f; p.setSampleParams (q);
            p.linkAll = false; p.setLaneChoice (LaneMelody, arpsyn::kPresets + 1); p.setLaneChoice (LaneArp, 1 + arpsyn::Supersaw);
            ed->soundTab = LaneMelody; ed->openSample (true); ed->refresh(); ed->toast ("", -1.0e15); shot (*ed, "sample-panel-dark");
            ed->openSample (false); ed->soundTab = -1; p.linkAll = true; p.setLaneChoice (-1, 1 + arpsyn::HypnoSaw);
        }
        p.setSyncOwn (true); ed->syncTransportUi(); ed->refresh(); ed->toast ("", -1.0e15); shot (*ed, "sync_own"); p.setSyncOwn (false); ed->syncTransportUi(); ed->refresh();
        setP (p, "transpose", 3); ed->refresh(); ed->toast ("", -1.0e15); shot (*ed, "transpose_plus3_melody");
        ed->selectLane (LaneChords); ed->refresh(); ed->toast ("", -1.0e15); shot (*ed, "transpose_plus3_chords"); setP (p, "transpose", 0); ed->selectLane (LaneMelody);
        p.setPlayCursor (-1);
        p.change ([] (Idea& X) { X.arpDir = DirPendulum; X.arpContour = ContArch; X.arpAccent = AccBuild; X.evolve = true; X.evolveBars = 16; X.evolveMode = EvolveGrow; X.arpMeter = Meter78; X.arpEuclid = 11; X.arpEuclidRot = 1;
                                 rebuildChords (X); composeLane (X, LaneArp, true);
                                 std::vector<float> e; for (int b = 0; b < X.total / BAR; ++b) e.push_back (b < 4 ? 0.15f + 0.1f * b : b < 8 ? 0.9f : b < 12 ? 0.35f : 1.0f); setArpEnergy (X, e); }, false);
        ed->selectLane (LaneArp); ed->refresh(); ed->toast ("", -1.0e15);
        shot (*ed, "arp_controls");
    }
}
void hookDemos (const juce::File& dir)
{
    dir.createDirectory();
    struct D { int mode; uint32_t seed; int style, candy; double tension; const char* tag; };
    const D demos[] { { Sunrise, 2024, StyleAnthemic, CandyHook, 0.3, "SUNRISE_anthemic" }, { Sunrise, 77, StylePlayful, CandyEuphoric, 0.4, "SUNRISE_playful" },
                      { Dusk, 51, StyleEmotional, CandySubtle, 0.55, "DUSK_emotional" }, { Dusk, 812, StyleMinimal, CandyHook, 0.7, "DUSK_minimal" },
                      { Midnight, 4321, StyleHypnotic, CandyEuphoric, 0.35, "MIDNIGHT_hypnotic" }, { Midnight, 128, StyleEmotional, CandyUnhinged, 0.65, "MIDNIGHT_unhinged" } };
    int k = 0;
    for (auto& d : demos)
    {
        ArpProcessor p; p.persistTaste = false;
        p.change ([&d] (Idea& X) { X.mode = d.mode; X.seed = d.seed; applyModeDefaults (X); newIdea (X); X.style = d.style; X.candy = d.candy; X.tension = d.tension; }, false);
        p.labSetParams (d.style, d.candy, d.tension, RegMid);   // the lab's own settings
        p.labGenerate (4);
        int best = 0; for (int i = 1; i < (int) p.lab.tiles.size(); ++i) if (p.lab.tiles[(size_t) i].score > p.lab.tiles[(size_t) best].score) best = i;
        p.labSong (best, 16);
        const bool piano = (k++ % 2) == 0;   // one plain piano + one NEON PLUCK per mood
        if (piano)
        {   // plain piano: chords + the hook, no FX
            p.change ([] (Idea& X) { X.lanes[LaneArp].mute = true; X.lanes[LaneBass].mute = true; X.lanes[LaneChords].mute = false; X.lanes[LaneMelody].mute = false; }, false);
            setP (p, "preset", arpsyn::Piano); setP (p, "stabs", 0); setP (p, "layerPluck", 0); setP (p, "layerOct", 0); setP (p, "kick", 0); setP (p, "pump", 0); setP (p, "lfoDepth", 0); setP (p, "delay", 0); setP (p, "reverb", 0);
        }
        else
        {
            p.change ([] (Idea& X) { X.lanes[LaneArp].mute = false; X.lanes[LaneMelody].mute = false; X.lanes[LaneChords].mute = false; }, false);
            setP (p, "preset", arpsyn::Neon); setP (p, "kick", 0); setP (p, "pump", 0.2f); setP (p, "lfoDepth", 0.3f); setP (p, "delay", 0.32f); setP (p, "reverb", 0.3f);
        }
        const Idea& I = p.idea();
        const juce::String name = juce::String ("hooklab_") + d.tag + (piano ? "_piano" : "_neonpluck");
        auto wav = renderIdea (p, dir, name, 16, I.g.bpm);
        const int mask = piano ? ((1 << LaneChords) | (1 << LaneMelody)) : 15;
        const auto mid = ideaMidi (I, mask, false, I.g.bpm); dir.getChildFile (name + ".mid").replaceWithData (mid.data(), mid.size());
        std::string ch; for (auto& c : I.chords) ch += c.name + " ";
        std::printf ("demo %s: %s, %d bpm, chords %s| tile %d (%s, piano %.2f), style %s, candy %s, tension %.2f\n", name.toRawUTF8(), keyName (I.g.key, I.g.scale).c_str(), I.g.bpm, ch.c_str(), best + 1,
                     p.lab.tiles.empty() ? "-" : p.lab.tiles[(size_t) best].label.c_str(), p.lab.tiles.empty() ? 0.0 : p.lab.tiles[(size_t) best].piano, styleName (d.style), candyName (d.candy), d.tension);
        (void) wav;
    }
    // the moods' own preview sounds (what GENERATE sets up), 8 bars each
    for (int m = 0; m < 3; ++m)
    {
        ArpProcessor p; p.persistTaste = false; p.generate (m, false, false);
        p.change ([] (Idea& X) { X.seed = 515; newIdea (X); }, false);
        const juce::String name = juce::String ("mood_") + modeName (m) + "_" + juce::String (arpsyn::presetName ((int) p.apvts.getRawParameterValue ("preset")->load())).replaceCharacter (' ', '_');
        renderIdea (p, dir, name, 8, p.idea().g.bpm);
        std::printf ("demo %s: %d bpm\n", name.toRawUTF8(), p.idea().g.bpm);
    }
    // IDM: a 7/8 euclidean hook phasing over 4/4 chords; a DROP hook on the 3-3-2 kick grid
    for (int v = 0; v < 2; ++v)
    {
        ArpProcessor p; p.persistTaste = false; p.generate (v == 0 ? Dusk : Midnight, false, false);
        p.change ([v] (Idea& X) { X.seed = v == 0 ? 812 : 4321; newIdea (X); }, false);
        if (v == 0) p.labSetRhythm (Meter78, 7, 2, FeelNormal); else p.labSetRhythm (Meter44, 0, 0, FeelDrop);
        p.labGenerate (4);
        int best = 0; for (int i = 1; i < (int) p.lab.tiles.size(); ++i) if (p.lab.tiles[(size_t) i].score > p.lab.tiles[(size_t) best].score) best = i;
        const auto label = p.lab.tiles.empty() ? std::string ("-") : p.lab.tiles[(size_t) best].label;
        p.labUse (best);
        if (v == 0) p.change ([] (Idea& X) { X.arpMeter = Meter78; X.arpEuclid = 9; X.arpEuclidRot = 1; rerealizeArp (X); }, false);
        else { setP (p, "kick", 1); setP (p, "pump", 0.4f); p.change ([] (Idea& X) { X.lanes[LaneBass].mute = false; }, false); }
        const juce::String name = v == 0 ? "idm_7-8_euclid_hook" : "drop_hook";
        renderIdea (p, dir, name, 8, p.idea().g.bpm);
        const auto mid = ideaMidi (p.idea(), 15, false, p.idea().g.bpm); dir.getChildFile (name + ".mid").replaceWithData (mid.data(), mid.size());
        std::printf ("demo %s: %d bpm, tile %d (%s)\n", name.toRawUTF8(), p.idea().g.bpm, best + 1, label.c_str());
    }
    // YOUR SAMPLE: a synthesised sung "aah" on the melody (ONE-SHOT, with the roll's glides / fatten), an FM bell on the arp (LOOP)
    for (int v = 0; v < 2; ++v)
    {
        ArpProcessor p; p.persistTaste = false; p.generate (v == 0 ? Dusk : Midnight, false, false);
        p.change ([v] (Idea& X) { X.seed = v == 0 ? 51 : 4321; newIdea (X); }, false);
        std::vector<float> x[2] = { v == 0 ? synthVowel() : synthBell(), {} }; juce::String msg; p.loadSampleAudio (x, 1, 48000.0, v == 0 ? "aah" : "bell", {}, msg);
        auto q = p.sampleParams(); if (v == 0) { q.mode = 0; q.formant = true; q.a = 0.02f; q.r = 0.3f; } else { q.mode = 1; q.r = 0.15f; q.volDb = -8.0f; } p.setSampleParams (q);
        p.linkAll = false; p.setLaneChoice (v == 0 ? LaneMelody : LaneArp, arpsyn::kPresets + 1);
        if (v == 0) p.change ([] (Idea& X) { auto& ns = X.lanes[LaneMelody].notes; for (size_t i = 0; i + 1 < ns.size(); i += 4) { ns[i].l.type = 1; ns[i].l.w = 0.5f; } if (ns.size() > 6) { ns[2].l.type = 2; ns[2].l.f = 1.0f; } X.lanes[LaneMelody].edited = true; }, false);
        const juce::String name = v == 0 ? "sample_vocal_melody" : "sample_bell_arp";
        renderIdea (p, dir, name, 8, p.idea().g.bpm);
        std::printf ("demo %s: %s\n", name.toRawUTF8(), msg.toRawUTF8());
    }
    // progressive lens: a clean hypnotic arp that EVOLVEs over 32 bars (same figure, the energy moves), with the hook on top
    struct P { int mode; uint32_t seed; int evolve, dir, contour, accent; bool hook; const char* tag; };
    const P prog[] { { Midnight, 4321, EvolveGrow, DirPendulum, ContFlat, AccOffbeat, false, "PROG_arp_grow_nokick" },
                     { Midnight, 4321, EvolveJourney, DirUp, ContRise, AccDownbeat, true, "PROG_arp_journey_hook" },
                     { Dusk, 812, EvolveWave, DirUpDown, ContArch, AccBuild, true, "PROG_arp_wave_hook" } };
    for (auto& d : prog)
    {
        ArpProcessor p; p.persistTaste = false;
        p.change ([&d] (Idea& X) { X.mode = d.mode; X.seed = d.seed; applyModeDefaults (X); newIdea (X); X.style = StyleHypnotic; X.candy = CandySubtle; X.tension = 0.3;
                                   X.arpDir = d.dir; X.arpContour = d.contour; X.arpAccent = d.accent; X.evolve = true; X.evolveBars = 32; X.evolveMode = d.evolve;
                                   rebuildChords (X); composeLane (X, LaneArp, false); composeLane (X, LaneBass, false); composeLane (X, LaneMelody, false); }, false);
        if (d.hook)
        {
            p.labSetParams (StyleHypnotic, CandySubtle, 0.3, RegMid);
            p.labGenerate (4);
            int best = 0; for (int i = 1; i < (int) p.lab.tiles.size(); ++i) if (p.lab.tiles[(size_t) i].score > p.lab.tiles[(size_t) best].score) best = i;
            p.labSong (best, 32);
        }
        p.change ([&d] (Idea& X) { X.lanes[LaneArp].mute = false; X.lanes[LaneChords].mute = false; X.lanes[LaneBass].mute = false; X.lanes[LaneMelody].mute = ! d.hook; }, false);
        setP (p, "preset", arpsyn::Neon); setP (p, "kick", d.hook ? 1.0f : 0.0f); setP (p, "pump", d.hook ? 0.35f : 0.0f); setP (p, "lfoDepth", 0.15f); setP (p, "delay", 0.3f); setP (p, "reverb", 0.28f);
        const Idea& I = p.idea();
        const juce::String name = juce::String ("hooklab_") + d.tag;
        renderIdea (p, dir, name, 32, I.g.bpm);
        const auto mid = ideaMidi (I, 15, false, I.g.bpm); dir.getChildFile (name + ".mid").replaceWithData (mid.data(), mid.size());
        std::printf ("demo %s: %s, %d bpm, EVOLVE %s, arp %s / %s / %s, hook %s\n", name.toRawUTF8(), keyName (I.g.key, I.g.scale).c_str(), I.g.bpm, evolveModeName (d.evolve),
                     arpDirName (d.dir), arpContourName (d.contour), arpAccentName (d.accent), d.hook ? "yes" : "no");
    }
}
} // namespace

// VARIETY MEASURE (--variety [N]): N ideas per mood from successive seeds 1..N; + the old moods' determinism fingerprints
void varietyReport (int N, int moods)
{
    std::printf ("VARIETY (melody lane, %d ideas per mood, successive seeds 1..N)\n", N);
    std::printf ("plain GENERATE (TRAP / HIP HOP without their PUSH FURTHER rule; the old three exactly as shipped):\n");
    arptest::printVarietyHeader();
    for (int m = 0; m < moods; ++m)
        arptest::printVarietyRow (arptest::varietyOf (modeName (m), N, [m] (Idea& I, uint32_t s, const arptest::Recent&) { I.mode = m; I.seed = s; applyModeDefaults (I); newIdea (I); }));
    std::printf ("with PUSH FURTHER (the default for TRAP / HIP HOP, opt-in for the old three):\n");
    arptest::printVarietyHeader();
    for (int m = 0; m < moods; ++m)
        arptest::printVarietyRow (arptest::varietyOf (modeName (m), N, [m] (Idea& I, uint32_t s, const arptest::Recent& r) { I.mode = m; I.seed = s; applyModeDefaults (I); newIdeaFurther (I, r); }));
    for (int m = 0; m < 3; ++m) std::printf ("fingerprint %s: %016llx\n", modeName (m).c_str(), (unsigned long long) arptest::moodFingerprint (m, 20));
}
#include "DemoGateTests.inc"
#include "ChordEditTests.inc"
#include "BugfixTests.inc"
#include "MutantTests.inc"
#include "FreshTests.inc"
#include "ClickTests.inc"
// TRAP EMO screenshots: the main view, HOOK LAB with its ten styles, MORE (FLAVOUR lists EMO)
void emoShots (const juce::File& dir)
{
    dir.createDirectory();
    auto shot = [&] (juce::Component& c, const juce::String& name) {
        auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
        auto f = dir.getChildFile (name + ".png"); f.deleteFile(); juce::FileOutputStream os (f); juce::PNGImageFormat png; png.writeImageToStream (img, os);
        std::printf ("shot %s\n", f.getFullPathName().toRawUTF8());
    };
    ArpProcessor p; p.persistTaste = false; p.theme = "dark"; p.hintsSeen = true;
    p.generate (Trap, false, false);
    p.change ([] (Idea& X) { X.seed = 1185; X.flavour = kEmo; newIdea (X); X.style = StyleEmo; }, false); p.applyFlavourLanes (false);
    std::unique_ptr<ArpEditor> ed (dynamic_cast<ArpEditor*> (p.createEditor()));
    ed->setSize (1240, 820); ed->refresh(); ed->toast ("", -1.0e15);
    shot (*ed, "emo_main");
    ed->more->setVisible (true); ed->more->toFront (false); ed->more->sync(); ed->refresh(); ed->toast ("", -1.0e15); shot (*ed, "emo_more"); ed->more->setVisible (false);
    ed->openHookLab (true); p.labCancel(); p.labGenerate (4); ed->refresh(); ed->hookLab->sync(); ed->toast ("", -1.0e15);
    shot (*ed, "emo_hooklab");
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    arpLoadEmbeddedRecipe();
    std::printf ("recipe: %s (%zu MIDNIGHT figures, %zu SUNRISE cells, %zu targets)\n", recipe().mode[Midnight].figures.empty() ? "built-in" : "composer recipe.json", recipe().mode[Midnight].figures.size(), recipe().mode[Sunrise].cellsTwo.size(), recipe().mode[Sunrise].targets.size());
    juce::StringArray args; for (int i = 1; i < argc; ++i) args.add (argv[i]);
    auto arg = [&] (const char* k) { const int i = args.indexOf (k); return i >= 0 && i + 1 < args.size() ? args[i + 1] : juce::String(); };
    if (args.contains ("demogate") || args.contains ("--demogate")) return arpDemoGate() == 0 ? 0 : 1;
    if (args.contains ("--dump-recipe")) { juce::File (arg ("--dump-recipe")).replaceWithText (juce::JSON::toString (arprecipe::toVar (recipe()))); return 0; }
    if (args.contains ("--variety")) { const int n = arg ("--variety").getIntValue(); varietyReport (n > 0 ? n : 50, kMoods); return 0; }
    if (args.contains ("--urbanprint")) { for (int m : { (int) Trap, (int) HipHop }) std::printf ("urbanprint %d: %016llx\n", m, (unsigned long long) arptest::urbanFingerprint (m)); return 0; }
    if (args.contains ("--hookprint")) { for (int m = 0; m < 3; ++m) std::printf ("hookprint %d: %016llx\n", m, (unsigned long long) arptest::hookFingerprint (m)); return 0; }
    if (args.contains ("--urban-renders")) { urbantest::renders (juce::File (arg ("--urban-renders"))); emotest::renders (juce::File (arg ("--urban-renders"))); return 0; }
    if (args.contains ("--emo-shots")) { emoShots (juce::File (arg ("--emo-shots"))); return 0; }
    if (args.contains ("--emo-renders")) { emotest::renders (juce::File (arg ("--emo-renders"))); return 0; }
    if (args.contains ("--props")) { Counter C; const int i = args.indexOf ("--props"); propChecks (C, arg ("--props"), i + 2 < args.size() ? args[i + 2] : juce::String()); std::printf ("props: pass=%d fail=%d\n", C.pass, C.fail); return C.fail ? 1 : 0; }
    if (args.contains ("--props-dump")) { proptest::dump (juce::File (arg ("--props-dump"))); return 0; }
    if (args.contains ("--presets")) { presetLoudness(); return 0; }
    if (args.contains ("--sound")) { soundReport(); return 0; }
    if (args.contains ("--grid")) { const int i = args.indexOf ("--grid"); printGrid (args[i + 1].getIntValue(), (uint32_t) args[i + 2].getLargeIntValue(), args[i + 3].getIntValue(), args[i + 4].getIntValue(), args.contains ("--evolve")); return 0; }
    if (args.contains ("--make-starters")) { makeStarters (juce::File (arg ("--make-starters"))); return 0; }
    const juce::File outDir (arg ("--out").isNotEmpty() ? arg ("--out") : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();
    Counter C;
    if (args.contains ("--themes")) { const int i = args.indexOf ("--themes"); themeChecks (C, i + 1 < args.size() ? args[i + 1] : juce::String()); std::printf ("themes: pass=%d fail=%d\n", C.pass, C.fail); return C.fail ? 1 : 0; }
    if (args.contains ("--chordoct")) { chordOctChecks (C, arg ("--chordoct")); std::printf ("chordoct: pass=%d fail=%d\n", C.pass, C.fail); return C.fail ? 1 : 0; }
    if (args.contains ("--tempo")) { tempoChecks (C); std::printf ("tempo: pass=%d fail=%d\n", C.pass, C.fail); return C.fail ? 1 : 0; }
    if (args.contains ("--stutterlock")) { stutterLockChecks (C); std::printf ("stutterlock: pass=%d fail=%d\n", C.pass, C.fail); return C.fail ? 1 : 0; }
    if (args.contains ("--clicks")) { clickChecks (C); std::printf ("clicks: pass=%d fail=%d\n", C.pass, C.fail); return C.fail ? 1 : 0; }
    if (args.contains ("--fresh")) { freshChecks (C); std::printf ("fresh: pass=%d fail=%d\n", C.pass, C.fail); return C.fail ? 1 : 0; }
    if (args.contains ("--bugfix")) { bugfixChecks (C); std::printf ("bugfix: pass=%d fail=%d\n", C.pass, C.fail); return C.fail ? 1 : 0; }
    if (args.contains ("--mutant-demos")) { muttest::demos (juce::File (arg ("--mutant-demos"))); return 0; }
    if (args.contains ("--dump-mid")) { muttest::dumpMidi (juce::File (arg ("--dump-mid")), arg ("--seed-base").getIntValue()); return 0; }
    if (args.contains ("--flute-demos")) { urbantest::fluteDemos (juce::File (arg ("--flute-demos"))); return 0; }
    if (args.contains ("--mutant")) { muttest::engineChecks (C); muttest::processorChecks (C); muttest::uiChecks (C, arg ("--shots").isNotEmpty() ? juce::File (arg ("--shots")) : juce::File()); std::printf ("mutant: pass=%d fail=%d\n", C.pass, C.fail); return C.fail ? 1 : 0; }
    if (args.contains ("--mutant-proc")) { muttest::processorChecks (C); std::printf ("mutant-proc: pass=%d fail=%d\n", C.pass, C.fail); return C.fail ? 1 : 0; }
    if (args.contains ("--mutant-engine")) { muttest::engineChecks (C); std::printf ("mutant-engine: pass=%d fail=%d\n", C.pass, C.fail); return C.fail ? 1 : 0; }
    if (args.contains ("--chordedit")) { chordEditChecks (C, arg ("--chordedit")); std::printf ("chordedit: pass=%d fail=%d\n", C.pass, C.fail); return C.fail ? 1 : 0; }
    if (args.contains ("--faces")) { faceChecks (C, arg ("--faces")); std::printf ("faces: pass=%d fail=%d\n", C.pass, C.fail); return C.fail ? 1 : 0; }
    if (args.contains ("--ghost")) { ghostChecks (C, arg ("--ghost")); std::printf ("ghost: pass=%d fail=%d\n", C.pass, C.fail); return C.fail ? 1 : 0; }
    if (args.contains ("--timeline")) { timelineChecks (C); std::printf ("timeline: pass=%d fail=%d\n", C.pass, C.fail); return C.fail ? 1 : 0; }
    if (args.contains ("--loop")) { loopChecks (C); std::printf ("loop: pass=%d fail=%d\n", C.pass, C.fail); return C.fail ? 1 : 0; }
    if (args.contains ("--polish")) { polishChecks (C); std::printf ("polish: pass=%d fail=%d\n", C.pass, C.fail); return C.fail ? 1 : 0; }
    if (args.contains ("--uitime"))
    {   // how long each idea button blocks the message thread (synchronous versions)
        ArpProcessor p; p.persistTaste = false;
        auto ms = [] (std::function<void()> f) { const auto t0 = juce::Time::getMillisecondCounterHiRes(); f(); return juce::Time::getMillisecondCounterHiRes() - t0; };
        for (int m = 0; m < 3; ++m) { std::printf ("GENERATE %s: %.1f ms\n", modeName (m).c_str(), ms ([&] { p.generate (m, true, true); })); }
        const char* tools[] { "varyLittle", "varyMore", "newRhythm", "newNotes", "simpler", "busier", "octDown", "octUp", "new" };
        for (int l = 0; l < 4; ++l) for (auto* t : tools) { std::vector<bool> sel; std::printf ("lane %d %-10s %.1f ms\n", l, t, ms ([&] { p.laneTool (l, t, &sel); })); }
        return 0;
    }
    if (args.contains ("--emo-dump")) { const int i = args.indexOf ("--emo-dump"); for (int k = 0; k < 4; ++k) emotest::dump (emotest::makeEmo ((uint32_t) (args[i + 1].getIntValue() + k), i + 2 < args.size() ? args[i + 2].getIntValue() : -1)); return 0; }
    if (args.contains ("--emo")) { emotest::soundChecks (C); emotest::engineChecks (C); std::printf ("emo: pass=%d fail=%d\n", C.pass, C.fail); return C.fail ? 1 : 0; }
    if (args.contains ("--urban")) { urbantest::checks (C); urbantest::fluteChecks (C); std::printf ("urban: pass=%d fail=%d\n", C.pass, C.fail); return C.fail ? 1 : 0; }
    if (args.contains ("--flute")) { urbantest::fluteChecks (C); std::printf ("flute: pass=%d fail=%d\n", C.pass, C.fail); return C.fail ? 1 : 0; }
    if (args.contains ("--newmodes")) { newModesChecks (C); std::printf ("newmodes: pass=%d fail=%d\n", C.pass, C.fail); return C.fail ? 1 : 0; }
    if (args.contains ("--sample")) { smptest::engineChecks (C); sampleProcChecks (C); std::printf ("sample: pass=%d fail=%d\n", C.pass, C.fail); return C.fail ? 1 : 0; }
    if (args.contains ("--sync")) { syncChecks (C); std::printf ("sync: pass=%d fail=%d\n", C.pass, C.fail); return C.fail ? 1 : 0; }
    if (args.contains ("--transpose")) { transposeChecks (C); std::printf ("transpose: pass=%d fail=%d\n", C.pass, C.fail); return C.fail ? 1 : 0; }
    if (args.contains ("--gate")) { gateChecks (C); std::printf ("gate: pass=%d fail=%d\n", C.pass, C.fail); return C.fail ? 1 : 0; }
    if (args.contains ("--bypass")) { bypassChecks (C); std::printf ("bypass: pass=%d fail=%d\n", C.pass, C.fail); return C.fail ? 1 : 0; }
    if (args.contains ("--cursor")) { cursorChecks (C); std::printf ("cursor: pass=%d fail=%d\n", C.pass, C.fail); return C.fail ? 1 : 0; }
    if (args.contains ("--stats")) { for (int m = 0; m < 3; ++m) arptest::modeReport (m, 40, args.contains ("--verbose")); recipeReport (40); if (args.contains ("--only-stats")) return 0; }
    if (args.contains ("--demos")) { demos (outDir); perModeRenders (outDir); }
    if (args.contains ("--only-demos")) return 0;
    if (args.contains ("--hook-shots")) { hookShots (juce::File (arg ("--hook-shots"))); return 0; }
    if (args.contains ("--hook-demos")) { hookDemos (juce::File (arg ("--hook-demos"))); return 0; }
    if (args.contains ("--only-hooklab")) { hooktest::engineChecks (C, true); hooktest::processorChecks (C); pianoTestCheck (C); std::printf ("HookLab: pass=%d fail=%d\n", C.pass, C.fail); return C.fail ? 1 : 0; }
    const auto ref = arg ("--ref");
    if (ref.isNotEmpty()) arptest::chordParity (C, ref.toStdString());
    engineChecks (C);
    simpleChecks (C);
    sessionChecks (C);
    rollChecks (C);
    listenChecks (C, outDir);
    harness (C, outDir);
    exportChecks (C);
    hooktest::engineChecks (C, args.contains ("--verbose"));
    hooktest::processorChecks (C);
    pianoTestCheck (C);
    soundChecks (C);
    cursorChecks (C);
    bypassChecks (C);
    gateChecks (C);
    transposeChecks (C);
    syncChecks (C);
    smptest::engineChecks (C);
    sampleProcChecks (C);
    newModesChecks (C);
    urbantest::checks (C); urbantest::fluteChecks (C);
    emotest::soundChecks (C);
    emotest::engineChecks (C);
    polishChecks (C);
    loopChecks (C);
    timelineChecks (C);
    themeChecks (C, {});
    ghostChecks (C, {});
    propChecks (C, {});
    chordOctChecks (C, {});
    chordEditChecks (C, {});
    bugfixChecks (C);
    muttest::engineChecks (C);
    muttest::processorChecks (C);
    muttest::uiChecks (C, {});
    std::printf ("ArpEZatorTests: pass=%d fail=%d\n", C.pass, C.fail);
    return C.fail ? 1 : 0;
}
