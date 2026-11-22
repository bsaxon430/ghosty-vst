// SPDX-License-Identifier: AGPL-3.0-or-later
// GHOST BREAK engine (real-time safe: no allocation, locks or files on the audio thread).
//   4 BREAK BAYS   each plays its break as 16th slices from a 32-step sequence (play / tie / rest, ratchet, reverse, pitch).
//                  NUDGE moves every slice start by whole grid units (1/4 ... 1/64), in time.
//   DRUM MACHINE   kick, snare, clap, hat, open hat, perc on a 32-step grid.
//   SPLIT SIDECHAIN each bay is split in three bands by LSP's crossover (Duck.h); by default the kick ducks only its lows, the
//                  snare + clap its mids, the hats + perc its highs (any drum can drive any band). The beat never pumps as a whole.
//   FX             every effect is a Phantom FX unit in a Phantom FX slot: per bay WRECKER (crunch) + PHANTOM FILTER
//                  (parallel filters); on the breaks: HAUNT EDIT (stutter) + its own PHANTOM FILTER + GHOST GATE;
//                  sends to CHORUS and HAUNTED HALLS; GHOST LIMITER on the output.
//   Everything is latency-aligned: bays, drums and the separate outputs line up to the sample.
#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include "BreakLib.h"
#include "MkDrumKit.h"
#include "effects/Catalog.h"
#include "rack/EffectSlot.h"
#include "Duck.h"
#include "SoftClip.h"
#include "Crush.h"
#include "NudgeLink.h"
#include <array>
#include <atomic>
#include <memory>

namespace gbk
{
constexpr int kBays = 4, kDrums = 6, kSteps = 32, kChunk = 256, kAux = 5;
// six free lanes: these are only their starting jobs. Any lane can hold any sound (six kicks, a second snare in PERC 1 ...) and take
// any name; the sidechain routing, sends and patterns go by the lane, not by what is in it.
enum DrumLane { dKick, dSnare, dHat, dOpenHat, dPerc1, dPerc2 };
inline const char* drumName (int d) { static const char* n[] { "KICK", "SNARE", "CLOSED HAT", "OPEN HAT", "PERC 1", "PERC 2" }; return n[juce::jlimit (0, kDrums - 1, d)]; }
// the sound category a lane starts on (and lists first in its sound menu)
inline const char* drumCategory (int d) { static const char* n[] { "Kick", "Snare", "Hat", "Open Hat", "Clap", "Perc" }; return n[juce::jlimit (0, kDrums - 1, d)]; }
inline int duckSource (int d) { return d == dKick ? 0 : (d == dSnare || d == dPerc1) ? 1 : 2; }   // the band a lane drives by default: 0 low, 1 mid, 2 high
inline int drumNote (int d) { static const int n[] { 36, 38, 42, 46, 39, 37 }; return n[juce::jlimit (0, kDrums - 1, d)]; }

// the Phantom FX units Ghost Break hosts
enum SlotId { sCrunch0 = 0, sFilter0 = 1, /* ... two per bay ... */ sStutter = kBays * 2, sStutFilter, sStutGate, sChorus, sReverb, sLimiter, sCrush0, /* ... one per bay ... */ kNumSlots = sCrush0 + kBays };
inline int crunchSlot (int bay) { return bay * 2; }
inline int filterSlot (int bay) { return bay * 2 + 1; }
inline int crushSlot (int bay) { return sCrush0 + bay; }
inline bool isCrush (int s) { return s >= sCrush0 && s < sCrush0 + kBays; }
inline bool isBaySlot (int s) { return s < kBays * 2 || isCrush (s); }   // a bay's own: its CRUSH, CLIP and FILTERS
inline int slotBay (int s) { return isCrush (s) ? s - sCrush0 : s / 2; }
inline int slotUnit (int s)
{
    if (s < kBays * 2) return (s & 1) ? phantom::uFilter : phantom::uWreck;
    if (isCrush (s)) return phantom::uWreck;
    switch (s) { case sStutter: return phantom::uHaunt; case sStutFilter: return phantom::uFilter; case sStutGate: return phantom::uGate;
                 case sChorus: return phantom::uChorus; case sReverb: return phantom::uHalls; default: return phantom::uLim; }
}
// each bay's CLIP is FL Studio's Fruity Soft Clipper (SoftClip.h); every other slot is a Phantom FX unit
inline std::unique_ptr<phantom::PhantomEffect> makeSlotEffect (int s)
{
    if (s < kBays * 2 && (s & 1) == 0) return std::make_unique<SoftClipFx>();
    if (isCrush (s)) return std::make_unique<CrushFx>();
    return phantom::makeEffect (slotUnit (s));
}
constexpr int kStutterNote = 53;   // F2: hold to stutter

// ---------------------------------------------------------------- the pattern (immutable once published)
enum StepKind : uint8_t { kPlay = 0, kTie = 1, kRest = 2 };   // tie: the slice before keeps playing through this step
struct Step { int16_t slice = 0; uint8_t kind = kPlay, rat = 1, vel = 110; int8_t pitch = 0; bool rev = false; };
struct Pattern
{
    std::array<std::array<Step, kSteps>, kBays> seq {};
    std::array<uint32_t, kBays> stepLock {};                    // bit s = step s keeps what it has when RANDOM COMBO runs
    std::array<std::array<uint8_t, kSteps>, kDrums> drum {};    // velocity 0..127
    std::array<mk::BreakPtr, kBays> brk;
    std::array<gb::SoundPtr, kDrums> snd;
    std::array<juce::String, kDrums> laneName;                  // the user's own lane names (empty = the starting name); message thread only
    void straight (int bay) { const int n = brk[(size_t) bay] ? juce::jmax (1, brk[(size_t) bay]->slices) : 32; for (int s = 0; s < kSteps; ++s) { Step st; st.slice = (int16_t) (s % n); seq[(size_t) bay][(size_t) s] = st; } }
};
using PatternPtr = std::shared_ptr<const Pattern>;

struct BayP { int nudge = 0, grid = 2; float pitch = 0, level = 0.8f, pan = 0, decay = 1, duck[3] {}, sendChorus = 0, sendReverb = 0; bool mute = false, ownOut = false; };
struct DrumP { float level = 0.8f, pan = 0, tune = 0; bool mute = false; };
struct Globals { float bpm = 172, swing = 0, breaks = 0.9f, drums = 0.9f, volume = 0.8f; bool play = false, drumsOwnOut = false, stutter = false, choke = true; int stutterPad = 1; DuckP duck; };

inline int gridUnit64 (int grid) { static const int u[] { 16, 8, 4, 2, 1 }; return u[juce::jlimit (0, 4, grid)]; }   // 64th notes in one NUDGE unit
// where in the break (in 64th notes from its start) a step's slice begins, with NUDGE applied
inline int startPos64 (const mk::Break& br, const Step& st, const BayP& P) { const int total = juce::jmax (1, br.slices) * 4; return (((int) st.slice * 4 + P.nudge * gridUnit64 (P.grid)) % total + total) % total; }

class Engine
{
public:
    Engine()
    {
        for (int s = 0; s < kNumSlots; ++s) { slots[(size_t) s] = std::make_unique<phantom::EffectSlot> (makeSlotEffect (s)); slots[(size_t) s]->setPolicy (phantom::OversamplingPolicy::X2); }
        for (int k = 0; k < 3; ++k) { dslots[(size_t) k] = std::make_unique<phantom::EffectSlot> (phantom::makeEffect (slotUnit (sStutter + k))); dslots[(size_t) k]->setPolicy (phantom::OversamplingPolicy::X2); }
        for (int b = 0; b < kBays; ++b) { dfilt[(size_t) b] = std::make_unique<phantom::EffectSlot> (makeSlotEffect (filterSlot (b))); dfilt[(size_t) b]->setPolicy (phantom::OversamplingPolicy::X2); }
    }
    const Pattern* patternNow() const noexcept { return pat.load(); }   // the pattern playing (tests)
    phantom::EffectSlot& slot (int s) noexcept { return *slots[(size_t) juce::jlimit (0, kNumSlots - 1, s)]; }
    phantom::EffectSlot& drumSlot (int k) noexcept { return *dslots[(size_t) juce::jlimit (0, 2, k)]; }
    phantom::EffectSlot& drumFilter (int b) noexcept { return *dfilt[(size_t) juce::jlimit (0, kBays - 1, b)]; }   // FILTER +DRUMS: a bay's filter, on the drums   // the drums' stutter, its filter, its gate
    std::atomic<int> stutTarget { 0 };   // 0 the breaks, 1 the drums, 2 both
    std::atomic<double> tempoMul { 1.0 };   // TEMPO 1/2x | 1x | 2x: the beat runs at this times the tempo, locked to the host's bars
    // the stutter's picture: a peak of the stuttered sound every few ms, and whether the stutter was on
    static constexpr int kScope = 192;
    std::array<std::atomic<float>, kScope> scopePeak {}; std::array<std::atomic<bool>, kScope> scopeOn {}; std::atomic<int> scopeAt { 0 };

    void prepare (double rate, int)
    {
        sr = rate;
        for (auto& s : slots) s->prepare (rate, kChunk, 2);
        for (auto& s : dslots) s->prepare (rate, kChunk, 2);
        for (auto& s : dfilt) s->prepare (rate, kChunk, 2);
        for (auto& b : bayBuf) { b.setSize (2, kChunk); b.clear(); }
        busBuf.setSize (2, kChunk); choBuf.setSize (2, kChunk); revBuf.setSize (2, kChunk); drumBuf.setSize (2, kChunk); mainBuf.setSize (2, kChunk); envBuf.setSize (3, kChunk);
        laneBuf.setSize (kDrums, kChunk); bandBuf.setSize (kBands, kChunk); duckEnv.prepare (rate);
        juce::dsp::ProcessSpec spec { rate, (juce::uint32) kChunk, 2 };
        for (int b = 0; b < kBays; ++b)
        {
            split[b][0].prepare (rate); split[b][1].prepare (rate);
            bayDelay[b].prepare (kMaxLat, 2); auxDelay[b].prepare (kMaxLat, 2);
        }
        auxDelay[kBays].prepare (kMaxLat, 2); drumDelay.prepare (kMaxLat, 2); envDelay.prepare (kMaxLat, 3);
        fade = juce::jmax (16, (int) (0.0015 * rate));
        reset(); refreshLatency();
    }
    void reset()
    {
        for (auto& s : slots) s->reset();
        for (auto& s : dslots) s->reset();
        for (auto& s : dfilt) s->reset();
        for (int b = 0; b < kBays; ++b) { for (auto& v : bay[b].v) v = Voice(); bay[b].cur = 0; split[b][0].reset(); split[b][1].reset(); bayDelay[b].reset(); auxDelay[b].reset(); gL[b] = gR[b] = 0; for (auto& d : dep[b]) d = 0; for (auto& m : uiDuck[b]) m.store (0); }
        duckEnv.reset(); for (auto& h : nHits) h = 0;
        auxDelay[kBays].reset(); drumDelay.reset(); envDelay.reset();
        for (auto& v : dv) v = DrumVoice();
        nPend = 0;
        clock = 0; absPos = 0; lastB0 = -1.0e9; first = true; stutNote = false; lastPat = nullptr;
        for (auto& u : uiPos) u.store (-1); uiStep.store (-1); meter.store (0);
    }
    // message thread, under the callback lock (prepare, or when a latency-changing choice moved): re-reads every slot, aligns the paths
    void refreshLatency()
    {
        for (auto& s : slots) s->applyPolicy();
        for (auto& s : dslots) s->applyPolicy();
        for (auto& s : dfilt) s->applyPolicy();
        int mx = 0; int lat[kBays];
        for (int b = 0; b < kBays; ++b) { lat[b] = slot (crushSlot (b)).getLatencySamples() + slot (crunchSlot (b)).getLatencySamples() + slot (filterSlot (b)).getLatencySamples(); mx = juce::jmax (mx, lat[b]); }
        const int bus = slot (sStutter).getLatencySamples() + slot (sStutFilter).getLatencySamples() + slot (sStutGate).getLatencySamples();
        const int lim = slot (sLimiter).getLatencySamples();
        const int L = lookSamples;   // LOOKAHEAD: the audio waits, the duck does not
        int dbus = dslots[0]->getLatencySamples() + dslots[1]->getLatencySamples() + dslots[2]->getLatencySamples();   // the drums' own stutter chain
        for (auto& f : dfilt) dbus += f->getLatencySamples();                                                        // and the four bay filters (+DRUMS)
        if (mx + bus < dbus) mx = dbus - bus;   // the drums' chain is the longer one: the bays wait for it (LOOKAHEAD still adds on top)
        for (int b = 0; b < kBays; ++b) { bayDelay[b].setDelay (mx - lat[b] + L); auxDelay[b].setDelay (bus + lim); }
        envDelay.setDelay (mx); drumDelay.setDelay (juce::jmax (0, mx + L + bus - dbus)); auxDelay[kBays].setDelay (lim);
        latency = juce::jmin (kMaxLat, mx + L + bus + lim);
    }
    int getLatencySamples() const noexcept { return latency; }
    void setLookaheadMs (float ms) noexcept { lookSamples = juce::jlimit (0, 2048, (int) std::lround (ms * 0.001 * sr)); }   // then refreshLatency()

    void setPattern (const Pattern* p) noexcept { pat.store (p); }
    void restart() noexcept { restartFlag.store (true); }
    // on-screen drum pads (message thread -> audio thread)
    void pushPad (int drum, float vel) { int s1, n1, s2, n2; padFifo.prepareToWrite (1, s1, n1, s2, n2); if (n1 > 0) { padEv[s1] = { drum, vel }; padFifo.finishedWrite (1); } }

    std::atomic<int> uiStep { -1 }, uiPos[kBays] { { -1 }, { -1 }, { -1 }, { -1 } };   // the step playing; each bay's last slice start (64ths)
    std::atomic<float> meter { 0 }; std::atomic<uint32_t> drumHits[kDrums] {}; std::atomic<uint64_t> blockCount { 0 };
    std::atomic<bool> uiStutter { false };
    std::atomic<float> uiDuck[kBays][kBands] {};   // each bay's gain reduction per band, dB (>= 0), the deepest of the last block

    // out: the main stereo pair; aux[0..3] = bay A..D, aux[4] = drums (each stereo, or nullptr when the host has that output off)
    void process (float* outL, float* outR, float* const* aux, int n, const std::array<BayP, kBays>& B, const std::array<DrumP, kDrums>& D, const Globals& G,
                  double hostBpm, double hostPpq, bool hostPlaying, const juce::MidiBuffer& midi)
    {
        const Pattern* P = pat.load(); chokeOn = G.choke; chokeStep = 1.0f / (float) juce::jmax (8.0, 0.004 * sr);
        const double bpm0 = juce::jlimit (20.0, 999.0, hostPlaying ? hostBpm : (double) G.bpm), spb0 = sr * 60.0 / bpm0;
        const double mul = juce::jlimit (0.25, 4.0, tempoMul.load (std::memory_order_relaxed));
        const double bpm = bpm0 * mul, spb = sr * 60.0 / bpm;   // the beat's own tempo and beats (the host's beats times TEMPO: bar 1 stays bar 1)
        if (restartFlag.exchange (false)) clock = 0;
        const bool run = hostPlaying || G.play;
        double c0 = hostPlaying ? hostPpq : clock;
        if (! std::isfinite (c0)) c0 = 0;
        if (std::abs (mul - lastMul) > 1.0e-9) { lastMul = mul; nPend = 0; lastB0 = -1.0e9; }   // TEMPO moved: start clean on the new grid
        const double b0 = c0 * mul;
        if (b0 < lastB0 - 0.01) nPend = 0;   // the transport jumped back: drop what was scheduled
        // NUDGE LINK: a Phantom FX in the project (HAUNT EDIT, FOLLOW GHOST BREAK) moves its own audio by each bay's NUDGE
        hostRun = hostPlaying; link.beat (hostPlaying, mul);
        for (int b = 0; b < kBays; ++b) link.setMaxShift (b, (breakTotal64 (P, b) - 1) / 16.0 / mul);
        // MIDI: drum notes fire on their sample; F2 holds the stutter
        for (const auto md : midi)
        {
            const auto m = md.getMessage(); const int nn = m.getNoteNumber();
            if (m.isNoteOn()) { if (nn == kStutterNote) stutNote = true; else for (int d = 0; d < kDrums; ++d) if (nn == drumNote (d) && P != nullptr) addDrum (P, D, d, m.getFloatVelocity(), absPos + juce::jlimit (0, n - 1, md.samplePosition)); }
            else if (m.isNoteOff() && nn == kStutterNote) stutNote = false;
            else if (m.isAllNotesOff() || m.isAllSoundOff()) stutNote = false;
        }
        { int s1, n1, s2, n2; padFifo.prepareToRead (padFifo.getNumReady(), s1, n1, s2, n2);
          auto fire = [&] (int st, int cnt) { for (int i = 0; i < cnt; ++i) if (P != nullptr) addDrum (P, D, padEv[st + i].drum, padEv[st + i].vel, absPos); };
          fire (s1, n1); fire (s2, n2); padFifo.finishedRead (n1 + n2); }
        float pk = 0;
        for (int o = 0; o < n; o += kChunk)
        {
            const int m = juce::jmin (kChunk, n - o);
            float* a[kAux * 2]; for (int k = 0; k < kAux; ++k) { a[k * 2] = aux != nullptr && aux[k * 2] != nullptr ? aux[k * 2] + o : nullptr; a[k * 2 + 1] = aux != nullptr && aux[k * 2 + 1] != nullptr ? aux[k * 2 + 1] + o : nullptr; }
            chunk (outL + o, outR + o, a, m, P, B, D, G, bpm, spb, b0 + o / spb, run, bpm0);
            for (int i = 0; i < m; ++i) pk = juce::jmax (pk, std::abs (outL[o + i]), std::abs (outR[o + i]));
        }
        if (run && ! hostPlaying) clock = c0 + n / spb0;
        lastB0 = b0 + n / spb;
        meter.store (juce::jmax (pk, meter.load() * 0.9f));
        blockCount.fetch_add (1);
    }

private:
    static constexpr int kMaxLat = 1 << 14;
    struct Voice { const mk::Break* brk = nullptr; double pos = 0, rate = 1; int left = 0, age = 0; float gain = 1; };
    struct Bay { Voice v[2]; int cur = 0; };
    struct DrumVoice { const gb::Sound* s = nullptr; double pos = 0, rate = 1; float gl = 0, gr = 0; int lane = 0; long long at = 0, chokeAt = std::numeric_limits<long long>::max(); float choke = 1; };
    struct Pend { long long at = 0; int bay = 0; const mk::Break* brk = nullptr; double start = 0, rate = 1; int len = 0; float gain = 1; };
    struct PadEv { int drum = 0; float vel = 0; };

    static float hermite (const float* d, int len, double pos) noexcept
    {
        const int i = (int) pos; const float t = (float) (pos - i);
        const float xm1 = d[(i - 1 + len) % len], x0 = d[i % len], x1 = d[(i + 1) % len], x2 = d[(i + 2) % len];
        const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2 * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }
    void addPend (const Pend& p) noexcept { if (nPend < (int) pend.size()) pend[(size_t) nPend++] = p; }
    void addDrum (const Pattern* P, const std::array<DrumP, kDrums>& D, int d, float vel, long long at) noexcept
    {
        const gb::Sound* s = P->snd[(size_t) d].get(); if (s == nullptr || s->data.empty()) return;
        if (! D[(size_t) d].mute && nHits[d] < kMaxHits) hitAt[d][nHits[d]++] = at;   // the sidechain hears it (a muted lane ducks nothing)
        DrumVoice* v = nullptr; for (auto& x : dv) if (x.s == nullptr) { v = &x; break; }
        if (v == nullptr) { v = &dv[0]; for (auto& x : dv) if (x.pos > v->pos) v = &x; }   // steal the one furthest along
        const auto& q = D[(size_t) d]; const float g = vel * vel, a = (q.pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
        // HAT CHOKE: a closed hat cuts the open hats that are ringing (over 4 ms, from its own sample on), as a real hi-hat does
        if (d == dHat && chokeOn) for (auto& x : dv) if (x.s != nullptr && x.lane == dOpenHat && x.at < at) x.chokeAt = juce::jmin (x.chokeAt, at);
        *v = DrumVoice { s, 0.0, s->rate / sr * std::pow (2.0, (double) q.tune / 12.0), g * std::cos (a) * 1.414f, g * std::sin (a) * 1.414f, d, at };
        drumHits[d].fetch_add (1);
    }

    // one step of the grid: drums, then every bay's slice (with ratchets and ties)
    void step (const Pattern* P, const std::array<BayP, kBays>& B, const std::array<DrumP, kDrums>& D, long s, long long at, double spb, double bpm) noexcept
    {
        const int st = (int) (((s % kSteps) + kSteps) % kSteps);
        uiStep.store (st);
        for (int d = 0; d < kDrums; ++d)
            if (const uint8_t v = P->drum[(size_t) d][(size_t) st]; v > 0 && ! D[(size_t) d].mute) addDrum (P, D, d, v / 127.0f, at);
        const double stepLen = spb * 0.25;
        for (int b = 0; b < kBays; ++b)
        {
            const mk::Break* br = P->brk[(size_t) b].get(); const auto& q = B[(size_t) b];
            if (br == nullptr || br->data.size() < 64 || q.mute) continue;
            const Step& e = P->seq[(size_t) b][(size_t) st];
            if (e.kind != kPlay) continue;
            int ties = 0; while (ties < kSteps - 1 && P->seq[(size_t) b][(size_t) ((st + 1 + ties) % kSteps)].kind == kTie) ++ties;
            const int rat = juce::jlimit (1, 4, (int) e.rat);
            const double pieceLen = stepLen / rat, sliceLen = (double) br->data.size() / juce::jmax (1, br->slices);
            const double fit = br->tempoFound ? bpm / juce::jmax (20.0, br->bpm) : 1.0;
            const double rate = br->rate / sr * fit * std::pow (2.0, (double) (q.pitch + (float) e.pitch) / 12.0);
            const int p64 = startPos64 (*br, e, q);
            uiPos[b].store (p64);
            for (int k = 0; k < rat; ++k)
            {
                Pend p; p.at = at + (long long) (k * pieceLen); p.bay = b; p.brk = br; p.gain = (e.vel / 127.0f) * (k == 0 ? 1.0f : 0.8f);
                const bool last = k == rat - 1;
                const double span = last ? pieceLen + ties * stepLen : pieceLen;
                p.len = juce::jmax (fade * 2, (int) (span * juce::jlimit (0.05f, 1.0f, q.decay)) + (q.decay >= 0.999f ? fade : 0));
                p.rate = e.rev ? -rate : rate;
                p.start = p64 * sliceLen * 0.25 + (e.rev ? sliceLen : 0.0);
                addPend (p);
            }
        }
    }

    // NUDGE LINK. A bay's break in 64th notes (an empty bay counts as the 32-slice default)
    static int breakTotal64 (const Pattern* P, int b) noexcept { const mk::Break* br = P != nullptr ? P->brk[(size_t) b].get() : nullptr; return (br != nullptr ? juce::jmax (1, br->slices) : 32) * 4; }
    // at step s (host quarter note ppq): how far each bay's NUDGE moves its break, as startPos64 applies it. A tied or resting step
    // keeps the move of the slice before it (Ghost Break keeps playing that slice), so a Phantom FX following the bay does the same
    void sendNudges (const Pattern* P, const std::array<BayP, kBays>& B, long s, double ppq) noexcept
    {
        const int st = (int) (((s % kSteps) + kSteps) % kSteps);
        for (int b = 0; b < kBays; ++b)
        {
            const auto& q = B[(size_t) b]; const int total = breakTotal64 (P, b);
            if (P == nullptr || P->seq[(size_t) b][(size_t) st].kind == kPlay || nudgeSent[b] < 0)
                nudgeSent[b] = ((q.nudge * gridUnit64 (q.grid)) % total + total) % total;
            link.step (b, (long long) s, ppq, nudgeSent[b] / 16.0 / lastMul);
        }
    }
    void chunk (float* outL, float* outR, float* const* aux, int n, const Pattern* P, const std::array<BayP, kBays>& B, const std::array<DrumP, kDrums>& D, const Globals& G,
                double bpm, double spb, double b0, bool run, double fitBpm) noexcept
    {
        // a new pattern: voices on sounds the new one no longer holds stop inside this chunk (the old pattern is kept alive meanwhile)
        if (P != lastPat)
        {
            for (int b = 0; b < kBays; ++b) for (auto& v : bay[b].v) if (v.brk != nullptr && (P == nullptr || P->brk[(size_t) b].get() != v.brk)) v.left = juce::jmin (v.left, fade);
            for (auto& v : dv) if (v.s != nullptr && (P == nullptr || P->snd[(size_t) v.lane].get() != v.s)) v.s = nullptr;
            int keep = 0; for (int i = 0; i < nPend; ++i) if (P != nullptr && P->brk[(size_t) pend[(size_t) i].bay].get() == pend[(size_t) i].brk) pend[(size_t) keep++] = pend[(size_t) i];
            nPend = keep; lastPat = P;
        }
        if (run && P != nullptr)
        {
            const double b1 = b0 + n / spb, sw = juce::jlimit (0.0f, 1.0f, G.swing) * 0.125;
            for (long s = (long) std::floor (b0 * 4.0) - 1; s <= (long) std::ceil (b1 * 4.0); ++s)
            {
                const double t = s * 0.25 + ((s & 1) ? sw : 0.0);
                if (t >= b0 - 1.0e-9 && t < b1 - 1.0e-9) { step (P, B, D, s, absPos + juce::jlimit (0, n - 1, (int) ((t - b0) * spb)), spb, fitBpm); if (hostRun) sendNudges (P, B, s, t / lastMul); }   // the breaks keep their pitch at 1/2x and 2x
            }
        }
        else if (! run) uiStep.store (-1);

        phantom::TempoInfo ti; ti.bpm = bpm; ti.ppqPosition = ti.ppqAtBlockStart = b0; ti.playing = run; ti.sampleRate = sr;

        // ---- the drums
        {
            drumBuf.clear (0, n); laneBuf.clear (0, n); float* L = drumBuf.getWritePointer (0); float* R = drumBuf.getWritePointer (1);
            for (auto& v : dv)
            {
                if (v.s == nullptr) continue;
                const auto& q = D[(size_t) v.lane]; const float lv = q.mute ? 0.0f : q.level * q.level * 1.5f;
                const float* d = v.s->data.data(); const int len = (int) v.s->data.size(); float* lane = laneBuf.getWritePointer (v.lane);
                int i = (int) juce::jlimit<long long> (0, n, v.at - absPos);
                for (; i < n; ++i)
                {
                    const int k = (int) v.pos; if (k >= len - 1) { v.s = nullptr; break; }
                    if (absPos + i >= v.chokeAt) { v.choke -= chokeStep; if (v.choke <= 0.0f) { v.s = nullptr; break; } }
                    const float x = (d[k] + (d[k + 1] - d[k]) * (float) (v.pos - k)) * lv * v.choke; L[i] += x * v.gl; R[i] += x * v.gr; lane[i] += x; v.pos += v.rate;
                }
            }
            juce::dsp::AudioBlock<float> blk (drumBuf); blk = blk.getSubBlock (0, (size_t) n);
            drumDelay.process (blk);
        }
        // ---- the split sidechain's envelopes, from this chunk's drum hits (PUNCH) or the drums' own sound (SMART), then delayed to
        //      where the bays are ducked (LOOKAHEAD earlier than the audio)
        {
            int laneHit[kDrums]; const float* lanes[kDrums]; float* env[kBands] { envBuf.getWritePointer (0), envBuf.getWritePointer (1), envBuf.getWritePointer (2) };
            for (int d = 0; d < kDrums; ++d)
            {
                laneHit[d] = -1; int keep = 0;
                for (int h = 0; h < nHits[d]; ++h)
                {
                    const long long t = hitAt[d][h];
                    if (t < absPos + n) { const int rel = (int) std::max<long long> (0, t - absPos); if (laneHit[d] < 0 || rel < laneHit[d]) laneHit[d] = rel; }
                    else hitAt[d][keep++] = t;
                }
                nHits[d] = keep; lanes[d] = laneBuf.getReadPointer (d);
            }
            duckEnv.process (G.duck, laneHit, lanes, env, n);
            juce::dsp::AudioBlock<float> eb (envBuf); envDelay.process (eb.getSubBlock (0, (size_t) n));
        }

        busBuf.clear (0, n); choBuf.clear (0, n); revBuf.clear (0, n); mainBuf.clear (0, n);
        // ---- the bays
        for (int b = 0; b < kBays; ++b)
        {
            auto& buf = bayBuf[b]; float* L = buf.getWritePointer (0); float* R = buf.getWritePointer (1);
            renderBay (b, L, n);
            juce::FloatVectorOperations::copy (R, L, n);
            juce::dsp::AudioBlock<float> blk (buf); blk = blk.getSubBlock (0, (size_t) n);
            slot (crushSlot (b)).process (blk, ti);    // CRUSH (TUBE + DOOM), then
            slot (crunchSlot (b)).process (blk, ti);   // CLIP (FL's soft clipper) catches its peaks, then
            slot (filterSlot (b)).process (blk, ti);
            bayDelay[b].process (blk);
            const auto& q = B[(size_t) b];
            // three bands (LSP's crossover); each goes down by its own envelope x this bay's depth for that band
            {
                const float* e[kBands] { envBuf.getReadPointer (0), envBuf.getReadPointer (1), envBuf.getReadPointer (2) };
                float target[kBands], from[kBands], deepest[kBands] {};
                for (int k = 0; k < kBands; ++k) { target[k] = G.duck.band[(size_t) k].on ? juce::jlimit (0.0f, 1.0f, q.duck[k] * G.duck.mix) : 0.0f; if (first) dep[b][k] = target[k]; from[k] = dep[b][k]; }
                const bool any = target[0] + target[1] + target[2] + from[0] + from[1] + from[2] > 0.0f;
                for (int ch = 0; ch < 2; ++ch)
                {
                    float* x = ch == 0 ? L : R; split[b][ch].set (G.duck.xLow, G.duck.xHigh, G.duck.slope);
                    float* bands[kBands] { bandBuf.getWritePointer (0), bandBuf.getWritePointer (1), bandBuf.getWritePointer (2) };
                    split[b][ch].process (x, bands, n);   // always split (so turning a duck on never moves the phase)
                    juce::FloatVectorOperations::clear (x, n);
                    for (int k = 0; k < kBands; ++k)
                    {
                        if (! any) { juce::FloatVectorOperations::add (x, bands[k], n); continue; }
                        const float step = (target[k] - from[k]) / (float) n;
                        for (int i = 0; i < n; ++i) { const float g = juce::jmax (0.0f, 1.0f - (from[k] + step * (float) i) * e[k][i]); x[i] += bands[k][i] * g; if (ch == 0) deepest[k] = juce::jmax (deepest[k], 1.0f - g); }
                    }
                }
                for (int k = 0; k < kBands; ++k)
                {
                    dep[b][k] = target[k];
                    const float gr = deepest[k] >= 0.999f ? 60.0f : -juce::Decibels::gainToDecibels (1.0f - deepest[k], -60.0f);
                    uiDuck[b][k].store (juce::jmax (gr, uiDuck[b][k].load (std::memory_order_relaxed) * 0.85f), std::memory_order_relaxed);
                }
            }
            // level + pan, glided over the chunk
            const float a = (q.pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi, lv = q.mute ? 0.0f : q.level * q.level * 1.5f;
            const float tl = lv * std::cos (a) * 1.414f, tr = lv * std::sin (a) * 1.414f;
            if (first) { gL[b] = tl; gR[b] = tr; }
            buf.applyGainRamp (0, 0, n, gL[b], tl); buf.applyGainRamp (1, 0, n, gR[b], tr); gL[b] = tl; gR[b] = tr;
            const bool own = q.ownOut && aux[b * 2] != nullptr && aux[b * 2 + 1] != nullptr;
            if (! own)
            {
                for (int ch = 0; ch < 2; ++ch)
                {
                    busBuf.addFrom (ch, 0, buf, ch, 0, n);
                    if (q.sendChorus > 0.0005f) choBuf.addFrom (ch, 0, buf, ch, 0, n, q.sendChorus);
                    if (q.sendReverb > 0.0005f) revBuf.addFrom (ch, 0, buf, ch, 0, n, q.sendReverb);
                }
            }
            if (aux[b * 2] != nullptr && aux[b * 2 + 1] != nullptr)
            {
                auxDelay[b].process (blk);
                juce::FloatVectorOperations::copy (aux[b * 2], L, n); juce::FloatVectorOperations::copy (aux[b * 2 + 1], R, n);
            }
        }
        // ---- the breaks: stutter -> its filter -> its gate
        {
            const bool st = G.stutter || stutNote; uiStutter.store (st); const int tgt = stutTarget.load (std::memory_order_relaxed);
            slot (sStutter).setValue (0, st && tgt != 1 ? (float) juce::jlimit (1, 16, G.stutterPad) : 0.0f);
            juce::dsp::AudioBlock<float> blk (busBuf); blk = blk.getSubBlock (0, (size_t) n);
            slot (sStutter).process (blk, ti); slot (sStutFilter).process (blk, ti); slot (sStutGate).process (blk, ti);
            juce::dsp::AudioBlock<float> cb (choBuf), rb (revBuf);
            slot (sChorus).process (cb.getSubBlock (0, (size_t) n), ti); slot (sReverb).process (rb.getSubBlock (0, (size_t) n), ti);
            for (int ch = 0; ch < 2; ++ch)
            {
                mainBuf.addFrom (ch, 0, busBuf, ch, 0, n, G.breaks); mainBuf.addFrom (ch, 0, choBuf, ch, 0, n, G.breaks); mainBuf.addFrom (ch, 0, revBuf, ch, 0, n, G.breaks);
            }
        }
        // ---- the drums into the mix (rendered and aligned above), through their own stutter chain
        {
            juce::dsp::AudioBlock<float> blk (drumBuf); blk = blk.getSubBlock (0, (size_t) n); float* L = drumBuf.getWritePointer (0); float* R = drumBuf.getWritePointer (1);
            {
                const bool st = G.stutter || stutNote; const int tgt = stutTarget.load (std::memory_order_relaxed);
                drumSlot (0).setValue (0, st && tgt != 0 ? (float) juce::jlimit (1, 16, G.stutterPad) : 0.0f);
                drumSlot (0).process (blk, ti); drumSlot (1).process (blk, ti); drumSlot (2).process (blk, ti);
                for (auto& f : dfilt) f->process (blk, ti);   // FILTER +DRUMS (each bay's filter that has it on; off ones pass the drums through)
                // the picture: the peak of what the stutter is working on (the breaks, the drums or both)
                float pk = 0.0f;
                if (tgt != 1) pk = juce::jmax (pk, busBuf.getMagnitude (0, 0, n));
                if (tgt != 0) pk = juce::jmax (pk, drumBuf.getMagnitude (0, 0, n));
                scopeAcc = juce::jmax (scopeAcc, pk); scopeN += n;
                if (scopeN >= (int) (sr * 0.012))
                {
                    const int at = (scopeAt.load (std::memory_order_relaxed) + 1) % kScope;
                    scopePeak[(size_t) at].store (scopeAcc, std::memory_order_relaxed); scopeOn[(size_t) at].store (st, std::memory_order_relaxed); scopeAt.store (at, std::memory_order_release);
                    scopeAcc = 0.0f; scopeN = 0;
                }
            }
            drumBuf.applyGain (0, n, G.drums);
            const bool own = G.drumsOwnOut && aux[kBays * 2] != nullptr && aux[kBays * 2 + 1] != nullptr;
            if (! own) for (int ch = 0; ch < 2; ++ch) mainBuf.addFrom (ch, 0, drumBuf, ch, 0, n);
            if (aux[kBays * 2] != nullptr && aux[kBays * 2 + 1] != nullptr)
            {
                auxDelay[kBays].process (blk);
                juce::FloatVectorOperations::copy (aux[kBays * 2], L, n); juce::FloatVectorOperations::copy (aux[kBays * 2 + 1], R, n);
            }
        }
        // ---- the output: volume, then the limiter
        {
            const float tv = G.volume * G.volume * 1.5f; if (first) gV = tv;
            mainBuf.applyGainRamp (0, n, gV, tv); gV = tv;
            juce::dsp::AudioBlock<float> blk (mainBuf); slot (sLimiter).process (blk.getSubBlock (0, (size_t) n), ti);
            juce::FloatVectorOperations::copy (outL, mainBuf.getReadPointer (0), n); juce::FloatVectorOperations::copy (outR, mainBuf.getReadPointer (1), n);
        }
        absPos += n; first = false;
    }

    // one bay's slices for this chunk (mono): scheduled triggers start on their sample; two voices crossfade
    void renderBay (int b, float* out, int n) noexcept
    {
        juce::FloatVectorOperations::clear (out, n);
        auto& by = bay[b];
        int from = 0;
        while (true)
        {
            // the next trigger of this bay inside the chunk
            int pi = -1; long long best = absPos + n;
            for (int i = 0; i < nPend; ++i) if (pend[(size_t) i].bay == b && pend[(size_t) i].at < best) { best = pend[(size_t) i].at; pi = i; }
            const int to = pi >= 0 ? (int) juce::jlimit<long long> (from, n, best - absPos) : n;
            for (auto& v : by.v)
            {
                if (v.brk == nullptr) continue;
                const float* d = v.brk->data.data(); const int len = (int) v.brk->data.size();
                for (int i = from; i < to; ++i)
                {
                    if (v.left <= 0) { v.brk = nullptr; break; }
                    float g = v.gain; if (v.age < fade) g *= (float) v.age / (float) fade; if (v.left < fade) g *= (float) v.left / (float) fade;
                    double p = std::fmod (v.pos, (double) len); if (p < 0) p += len;
                    out[i] += hermite (d, len, p) * g; v.pos += v.rate; ++v.age; --v.left;
                }
            }
            if (pi < 0) break;
            const Pend p = pend[(size_t) pi]; pend[(size_t) pi] = pend[(size_t) --nPend];
            by.v[by.cur].left = juce::jmin (by.v[by.cur].left, fade);   // the slice before fades out under the new one
            by.cur ^= 1; auto& v = by.v[by.cur];
            v.brk = p.brk; v.pos = p.start; v.rate = p.rate; v.left = p.len; v.age = 0; v.gain = p.gain;
            from = to;
        }
    }

    ghostlink::Sender link; int nudgeSent[kBays] { -1, -1, -1, -1 }; bool hostRun = false;   // NUDGE LINK
    double sr = 48000, clock = 0, lastB0 = -1.0e9, lastMul = 1.0; long long absPos = 0; int fade = 72, latency = 0; bool first = true, stutNote = false;
    std::atomic<const Pattern*> pat { nullptr }; const Pattern* lastPat = nullptr; std::atomic<bool> restartFlag { false };
    std::array<std::unique_ptr<phantom::EffectSlot>, kNumSlots> slots;
    std::array<std::unique_ptr<phantom::EffectSlot>, 3> dslots; std::array<std::unique_ptr<phantom::EffectSlot>, kBays> dfilt;
    float scopeAcc = 0.0f; int scopeN = 0;
    Bay bay[kBays]; DrumVoice dv[32]; std::array<Pend, 128> pend; int nPend = 0;
    juce::AudioBuffer<float> bayBuf[kBays], busBuf, choBuf, revBuf, drumBuf, mainBuf, envBuf;
    Split3 split[kBays][2]; DuckEnvelopes duckEnv; juce::AudioBuffer<float> laneBuf, bandBuf; float dep[kBays][kBands] {};
    static constexpr int kMaxHits = 16; long long hitAt[kDrums][kMaxHits] {}; int nHits[kDrums] {}; int lookSamples = 0;
    phantom::IntegerDelay bayDelay[kBays], auxDelay[kAux], drumDelay, envDelay;
    float gL[kBays] {}, gR[kBays] {}, gV = 0, chokeStep = 0.005f; bool chokeOn = true;
    juce::AbstractFifo padFifo { 64 }; PadEv padEv[64];
};
} // namespace gbk
