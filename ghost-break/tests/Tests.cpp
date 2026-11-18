// SPDX-License-Identifier: AGPL-3.0-or-later
// GHOST BREAK checks (console). `GhostBreakTests` runs them all; `GhostBreakTests shot <file.png> [fxSlot]` paints the editor to a file.
#include "Processor.h"
#include "Editor.h"
#include "engines/CurveEngine.h"
#include "effects/ColourSst.h"
#include "effects/ControlUse.h"
#include <set>
#include "engines/Engines.h"
#include <cstdio>

using namespace gbk;
namespace
{
int passed = 0, failed = 0;
void check (bool ok, const juce::String& what) { std::printf ("%s %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8()); std::fflush (stdout); ok ? ++passed : ++failed; }
void set (Processor& p, const juce::String& id, float v) { if (auto* q = p.apvts.getParameter (id)) q->setValueNotifyingHost (q->convertTo0to1 (v)); else { std::printf ("FAIL no parameter %s\n", id.toRawUTF8()); ++failed; } }

struct Head : juce::AudioPlayHead
{
    double ppq = 0, bpm = 172; bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override { PositionInfo i; i.setBpm (bpm); i.setPpqPosition (ppq); i.setIsPlaying (playing); return i; }
};
// renders `seconds` in blocks of `block`; with a head the host plays from ppq 0
juce::AudioBuffer<float> render (Processor& p, double seconds, Head* head = nullptr, int block = 512, int channels = 2, double sr = 48000.0)
{
    const int total = (int) (seconds * sr); juce::AudioBuffer<float> out (channels, total), buf (channels, block); juce::MidiBuffer midi;
    if (head != nullptr) { head->ppq = 0; p.setPlayHead (head); }
    for (int o = 0; o < total; o += block)
    {
        const int n = juce::jmin (block, total - o); juce::AudioBuffer<float> b (buf.getArrayOfWritePointers(), channels, n); b.clear();
        p.processBlock (b, midi);
        for (int c = 0; c < channels; ++c) out.copyFrom (c, o, b, c, 0, n);
        if (head != nullptr) head->ppq += n / sr * head->bpm / 60.0;
    }
    return out;
}
float peak (const juce::AudioBuffer<float>& b, int ch0 = 0, int nch = 2) { float pk = 0; for (int c = ch0; c < ch0 + nch; ++c) pk = juce::jmax (pk, b.getMagnitude (c, 0, b.getNumSamples())); return pk; }
bool finite (const juce::AudioBuffer<float>& b) { for (int c = 0; c < b.getNumChannels(); ++c) for (int i = 0; i < b.getNumSamples(); ++i) if (! std::isfinite (b.getSample (c, i))) return false; return true; }
int onset (const juce::AudioBuffer<float>& b, float thr) { for (int i = 0; i < b.getNumSamples(); ++i) if (std::abs (b.getSample (0, i)) > thr) return i; return -1; }
// energy below ~120 Hz and above ~6 kHz (4th-order Butterworth splits, so neither band leaks into the other)
void bands (const juce::AudioBuffer<float>& b, double& low, double& high, int from, int to)
{
    using C = juce::IIRCoefficients; juce::IIRFilter l1, l2, h1, h2;
    l1.setCoefficients (C::makeLowPass (48000.0, 120.0)); l2.setCoefficients (C::makeLowPass (48000.0, 120.0));
    h1.setCoefficients (C::makeHighPass (48000.0, 6000.0)); h2.setCoefficients (C::makeHighPass (48000.0, 6000.0));
    low = high = 0;
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const float x = b.getSample (0, i), lo = l2.processSingleSampleRaw (l1.processSingleSampleRaw (x)), hi = h2.processSingleSampleRaw (h1.processSingleSampleRaw (x));
        if (i >= from && i < to) { low += lo * lo; high += hi * hi; }
    }
}
bool sameSeq (const Pattern& a, const Pattern& b, int bay) { for (int s = 0; s < kSteps; ++s) { const auto& x = a.seq[(size_t) bay][(size_t) s]; const auto& y = b.seq[(size_t) bay][(size_t) s]; if (x.slice != y.slice || x.kind != y.kind || x.rat != y.rat || x.rev != y.rev || x.vel != y.vel) return false; } const auto& x = a.brk[(size_t) bay]; const auto& y = b.brk[(size_t) bay];
    return x == y || (x && y && x->user && y->user && x->family == y->family); }   // a file break is decoded again by each copy: the same file counts
void soloBay (Processor& p, bool drumsOn) { for (int d = 0; d < kDrums; ++d) set (p, juce::String (drumPrefix (d)) + "level", drumsOn ? 0.75f : 0.0f); }

void runAll()
{
    Head head;
    { // 1. it plays: sound, no NaN, never over the limiter's ceiling
        Processor p; p.prepareToPlay (48000.0, 512); set (p, "play", 1);
        auto a = render (p, 4.0);
        check (finite (a) && peak (a) > 0.05f, "plays from its own clock (peak " + juce::String (peak (a), 3) + ")");
        check (peak (a) <= 1.0f, "output stays under full scale");
        check (p.getLatencySamples() == p.engine.getLatencySamples() && p.getLatencySamples() < 4096, "reports its latency (" + juce::String (p.getLatencySamples()) + " samples)");
        auto t0 = juce::Time::getMillisecondCounterHiRes(); render (p, 10.0); const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        std::printf ("INFO default patch: 10 s rendered in %.0f ms (%.1f %% of one core)\n", ms, ms / 100.0);
        for (int s = 0; s < kNumSlots; ++s) if (slotHasOn (s)) set (p, slotPrefix (s) + "on", 1);
        for (int b = 0; b < kBays; ++b) { set (p, juce::String (bayPrefix (b)) + "mute", 0); set (p, juce::String (bayPrefix (b)) + "send_rev", 0.4f); set (p, juce::String (bayPrefix (b)) + "send_cho", 0.4f); }
        set (p, "stutter", 1); p.prepareToPlay (48000.0, 512);
        t0 = juce::Time::getMillisecondCounterHiRes(); auto all = render (p, 10.0); const double ms2 = juce::Time::getMillisecondCounterHiRes() - t0;
        std::printf ("INFO everything on: 10 s rendered in %.0f ms (%.1f %% of one core)\n", ms2, ms2 / 100.0);
        check (finite (all) && peak (all) <= 1.0f && peak (all) > 0.02f, "every effect on, 4 bays, stutter held: clean output");
        for (int blk : { 16, 97, 2048, 4096 }) { auto x = render (p, 1.0, nullptr, blk); check (finite (x) && peak (x) <= 1.0f, "block size " + juce::String (blk)); }
        for (double sr : { 44100.0, 96000.0 }) { p.prepareToPlay (sr, 512); auto x = render (p, 1.0, nullptr, 512, 2, sr); check (finite (x) && peak (x) > 0.02f, "sample rate " + juce::String (sr)); }
    }
    { // 2. RANDOM COMBO obeys its rules and the locks
        Processor p; const Pattern base = p.pattern(); auto B = p.bayParams(); for (auto& b : B) b.mute = false;
        Rules r; Locks l; const auto all = p.allBreaks();
        const Pattern a = randomCombo (base, B, r, l, all, 1234u), a2 = randomCombo (base, B, r, l, all, 1234u), c = randomCombo (base, B, r, l, all, 99u);
        bool same = true, differs = false; for (int b = 0; b < kBays; ++b) { same = same && sameSeq (a, a2, b); differs = differs || ! sameSeq (a, c, b); }
        check (same, "RANDOM COMBO: same seed, same arrangement"); check (differs, "RANDOM COMBO: another seed, another arrangement");
        bool drumsKept = true; for (int d = 0; d < kDrums; ++d) drumsKept = drumsKept && a.drum[(size_t) d] == base.drum[(size_t) d];
        check (drumsKept, "RANDOM COMBO: locked drum lanes stay");
        Locks l2; l2.bay[1] = true; Pattern lockedIn = base; lockedIn.stepLock[0] = (1u << 4) | (1u << 12); lockedIn.seq[0][4].slice = 17; lockedIn.seq[0][12].slice = 21;
        const Pattern k = randomCombo (lockedIn, B, r, l2, all, 77u);
        check (sameSeq (k, lockedIn, 1), "RANDOM COMBO: a locked bay stays");
        check (k.seq[0][4].slice == 17 && k.seq[0][12].slice == 21 && ! sameSeq (k, lockedIn, 0), "RANDOM COMBO: locked steps stay while the rest of the bay changes");
        Rules plain; plain.density = 1; plain.chaos = 0; plain.ratchet = 0; plain.reverse = 0; plain.trade = 0; plain.sliceLen = 2;
        const Pattern s = randomCombo (base, B, plain, l, all, 5u);
        bool straight = true; for (int b = 0; b < kBays; ++b) for (int i = 0; i < kSteps; ++i) straight = straight && s.seq[(size_t) b][(size_t) i].kind == kPlay && s.seq[(size_t) b][(size_t) i].slice == i % s.brk[(size_t) b]->slices;
        check (straight, "RANDOM COMBO: density 100 %, chaos 0 % gives the break as it is");
        Rules q = r; q.chaos = 1; B[0].grid = 0; const Pattern g = randomCombo (base, B, q, l, all, 31u);
        bool onGrid = true; for (int i = 0; i < kSteps; ++i) if (g.seq[0][(size_t) i].kind == kPlay) onGrid = onGrid && g.seq[0][(size_t) i].slice % 4 == 0;
        check (onGrid, "RANDOM COMBO: slice starts stay on the bay's grid (1/4)");
        Rules nb = r; nb.newBreaks = true; const Pattern n2 = randomCombo (base, B, nb, l2, all, 8u);
        check (n2.brk[1] == base.brk[1], "RANDOM COMBO: NEW BREAKS leaves a locked bay's break alone");
        Locks dl; for (auto& x : dl.drum) x = false; const Pattern dr = randomCombo (base, B, r, dl, all, 3u);
        int kicks = 0, snares = 0; for (int i = 0; i < kSteps; ++i) { kicks += dr.drum[dKick][(size_t) i] > 0; snares += dr.drum[dSnare][(size_t) i] > 0; }
        check (kicks >= 3 && snares >= 4 && dr.drum[dKick][0] > 0, "RANDOM COMBO: unlocked drum lanes get a drum & bass pattern");
        // RANDOM START
        l2 = Locks(); l2.bay[2] = true; const auto st = randomStart (base, B, l2, 42u);
        bool ok = st[2] == -1; for (int b = 0; b < kBays; ++b) if (b != 2) ok = ok && st[(size_t) b] >= 0 && st[(size_t) b] <= 31 && st[(size_t) b] != B[(size_t) b].nudge;
        check (ok, "RANDOM START: new start for every unlocked bay, on its grid, inside the knob's range");
    }
    { // 3. timing: drums and breaks line up (latency-aligned), and follow the host
        Processor p; p.prepareToPlay (48000.0, 512);
        soloBay (p, true); set (p, "a_mute", 1);
        auto d = render (p, 0.5, &head); const int od = onset (d, 0.02f);
        Processor q; q.prepareToPlay (48000.0, 512); soloBay (q, false); set (q, "a_duck_low", 0); set (q, "a_duck_mid", 0);
        auto b = render (q, 0.5, &head); const int ob = onset (b, 0.02f);
        check (od >= p.getLatencySamples() && od < p.getLatencySamples() + 96, "the kick lands on the bar line, after the reported latency (sample " + juce::String (od) + ", latency " + juce::String (p.getLatencySamples()) + ")");
        check (ob >= 0 && std::abs (ob - od) < 192, "the break's first hit lines up with the drums (" + juce::String (ob) + " vs " + juce::String (od) + ")");
        check (p.engine.uiStep.load() >= 0 && p.hostPlayingNow.load(), "follows the host transport");
        set (q, "a_crunch_on", 1); set (q, "a_filter_on", 1); set (q, "a_filter_cutoff", 18000); q.prepareToPlay (48000.0, 512);
        auto b2 = render (q, 0.5, &head); const int ob2 = onset (b2, 0.02f);
        check (ob2 >= 0 && std::abs (ob2 - od) < 192, "still lined up with the bay's crunch and filters on (" + juce::String (ob2) + ")");
    }
    { // 4. selective sidechain: the kick takes the break's lows down and leaves its highs alone
        double low[2], high[2];
        for (int k = 0; k < 2; ++k)
        {
            Processor p; p.prepareToPlay (48000.0, 512); soloBay (p, false); set (p, "a_duck_mid", 0); set (p, "a_duck_low", k == 0 ? 0.0f : 1.0f); set (p, "dl_rel", 400);
            auto x = render (p, 0.2, &head); bands (x, low[k], high[k], p.getLatencySamples() + 300, p.getLatencySamples() + 4000);
        }
        const double dl = 10.0 * std::log10 (low[1] / juce::jmax (1.0e-12, low[0])), dh = 10.0 * std::log10 (high[1] / juce::jmax (1.0e-12, high[0]));
        check (dl < -6.0 && std::abs (dh) < 1.0, "KICK > LOWS: lows " + juce::String (dl, 1) + " dB, highs " + juce::String (dh, 1) + " dB");
        Processor p; p.prepareToPlay (48000.0, 512); soloBay (p, false); set (p, "k_mute", 1); set (p, "a_duck_low", 1);
        Processor q; q.prepareToPlay (48000.0, 512); soloBay (q, false); set (q, "a_duck_low", 0); set (q, "a_duck_mid", 0); set (p, "a_duck_mid", 0);
        auto x = render (p, 0.2, &head), y = render (q, 0.2, &head); double l1, h1, l2, h2; bands (x, l1, h1, 600, 4000); bands (y, l2, h2, 600, 4000);
        check (std::abs (10.0 * std::log10 (l1 / juce::jmax (1.0e-12, l2))) < 0.5, "a muted drum does not duck");
    }
    { // 4b. the split sidechain in depth (LSP crossover, routing, SMART, lookahead)
        // the three bands add back up to the input's exact magnitude (an impulse through the split, summed)
        { Split3 x; x.prepare (48000.0); x.set (160, 5000, 1); const int N = 8192; std::vector<float> in ((size_t) N, 0.0f), sum ((size_t) N, 0.0f); in[0] = 1;
          std::array<std::array<float, kDuckChunk>, 3> bb {}; float* outs[3] { bb[0].data(), bb[1].data(), bb[2].data() };
          for (int o = 0; o < N; o += kDuckChunk) { x.process (in.data() + o, outs, kDuckChunk); for (int i = 0; i < kDuckChunk; ++i) sum[(size_t) (o + i)] = bb[0][(size_t) i] + bb[1][(size_t) i] + bb[2][(size_t) i]; }
          juce::dsp::FFT fft (13); std::vector<float> f ((size_t) N * 2, 0.0f); std::copy (sum.begin(), sum.end(), f.begin()); fft.performFrequencyOnlyForwardTransform (f.data());
          float worst = 0; for (int k = 4; k < N / 2 * 20 / 24; ++k) worst = juce::jmax (worst, std::abs (juce::Decibels::gainToDecibels (f[(size_t) k])));
          check (worst < 0.1f, "the LSP split sums back flat (worst " + juce::String (worst, 3) + " dB, 23 Hz - 20 kHz)"); }
        auto bandsOf = [&] (std::function<void (Processor&)> setup, double& lo, double& hi) {
            Processor p; auto lay = p.getBusesLayout(); lay.outputBuses.getReference (1) = juce::AudioChannelSet::stereo(); p.setBusesLayout (lay);
            p.prepareToPlay (48000.0, 512); set (p, "a_duck_low", 0); set (p, "a_duck_mid", 0); set (p, "a_out", 1); setup (p);
            Head h; auto x = render (p, 0.2, &h, 512, 4); juce::AudioBuffer<float> a (1, x.getNumSamples()); a.copyFrom (0, 0, x, 2, 0, x.getNumSamples());
            bands (a, lo, hi, p.getLatencySamples() + 300, p.getLatencySamples() + 4000); return p.getLatencySamples(); };
        double l0, h0, l1, h1, l2, h2, l3, h3;
        bandsOf ([] (Processor&) {}, l0, h0);
        bandsOf ([] (Processor& p) { set (p, "a_duck_low", 1); set (p, "dl_from_k", 0); set (p, "dl_from_s", 1); set (p, "dl_rel", 400); }, l1, h1);
        const double gl = 10.0 * std::log10 (l1 / l0);
        check (gl > -0.5, "routing: with the kick taken off the low band, the first beat's lows stay (" + juce::String (gl, 1) + " dB)");
        bandsOf ([] (Processor& p) { set (p, "a_duck_low", 1); set (p, "dl_rel", 400); set (p, "duck_mode", 1); set (p, "duck_thr", -50); }, l2, h2);
        const double sl = 10.0 * std::log10 (l2 / l0), sh = 10.0 * std::log10 (h2 / h0);
        check (sl < -3.0 && std::abs (sh) < 1.0, "SMART: the kick's own sound takes the lows down (" + juce::String (sl, 1) + " dB), highs stay (" + juce::String (sh, 1) + " dB)");
        const int lat0 = bandsOf ([] (Processor&) {}, l3, h3);
        const int lat1 = bandsOf ([] (Processor& p) { set (p, "duck_look", 4); p.prepareToPlay (48000.0, 512); }, l3, h3);
        check (lat1 - lat0 == 480, "LOOKAHEAD 10 ms adds exactly 480 samples of latency (" + juce::String (lat1 - lat0) + ")");
        { Processor p; p.prepareToPlay (48000.0, 512); set (p, "duck_look", 4); p.prepareToPlay (48000.0, 512); soloBay (p, true); set (p, "a_mute", 1);
          Head h; auto d = render (p, 0.5, &h); const int od = onset (d, 0.02f);
          check (od >= p.getLatencySamples() && od < p.getLatencySamples() + 96, "with LOOKAHEAD the drums still land on the reported latency (" + juce::String (od) + " / " + juce::String (p.getLatencySamples()) + ")"); }
        { Processor p; p.prepareToPlay (48000.0, 512); for (int b = 0; b < kBays; ++b) { set (p, juce::String (bayPrefix (b)) + "mute", 0); for (auto bnd : { "duck_low", "duck_mid", "duck_high" }) set (p, juce::String (bayPrefix (b)) + bnd, 1); }
          set (p, "duck_mode", 1); Head h; auto x = render (p, 2.0, &h); set (p, "duck_mode", 0); auto y = render (p, 2.0, &h);
          check (finite (x) && finite (y) && peak (x) <= 1.0f, "every band of every bay ducking, both modes: clean"); }
    }
    { // 5. NUDGE moves the start in time; the stutter changes the sound; state comes back
        Processor p; p.prepareToPlay (48000.0, 512); soloBay (p, false);
        auto a = render (p, 0.5, &head); set (p, "a_grid", 1); set (p, "a_nudge", 3); auto b = render (p, 0.5, &head);
        double diff = 0; for (int i = 0; i < a.getNumSamples(); ++i) diff += std::abs (a.getSample (0, i) - b.getSample (0, i));
        Step first = p.pattern().seq[0][0]; auto B = p.bayParams();
        check (diff > 1.0 && startPos64 (*p.pattern().brk[0], first, B[0]) == 24, "NUDGE 3 on a 1/8 grid starts three 8ths into the break");
        set (p, "a_nudge", 0); auto c = render (p, 1.0, &head); set (p, "stutter", 1); auto e = render (p, 1.0, &head); set (p, "stutter", 0);
        diff = 0; for (int i = 24000; i < c.getNumSamples(); ++i) diff += std::abs (c.getSample (0, i) - e.getSample (0, i));
        check (diff > 1.0 && finite (e), "holding STUTTER repeats the break");
        p.randomCombo(); p.randomStart(); set (p, "b_filter_cutoff", 777.0f); set (p, "rule_chaos", 0.9f);
        juce::MemoryBlock mb; p.getStateInformation (mb);
        Processor q; q.setStateInformation (mb.getData(), (int) mb.getSize());
        bool same = true; for (int bay = 0; bay < kBays; ++bay) same = same && sameSeq (p.pattern(), q.pattern(), bay) && p.pattern().stepLock[(size_t) bay] == q.pattern().stepLock[(size_t) bay];
        for (int d = 0; d < kDrums; ++d) same = same && p.pattern().drum[(size_t) d] == q.pattern().drum[(size_t) d] && p.pattern().snd[(size_t) d] == q.pattern().snd[(size_t) d];
        if (! same)   // say what differs
            for (int bay = 0; bay < kBays; ++bay)
            {
                const auto& x = p.pattern(); const auto& y = q.pattern(); int s0 = -1;
                for (int s = 0; s < kSteps && s0 < 0; ++s) { const auto& u = x.seq[(size_t) bay][(size_t) s]; const auto& v = y.seq[(size_t) bay][(size_t) s]; if (u.slice != v.slice || u.kind != v.kind || u.rat != v.rat || u.rev != v.rev || u.vel != v.vel) s0 = s; }
                std::printf ("  bay %d: %s / %s same %d, first different step %d, locks %u/%u\n", bay, (x.brk[(size_t) bay] ? x.brk[(size_t) bay]->name : juce::String ("-")).toRawUTF8(),
                             (y.brk[(size_t) bay] ? y.brk[(size_t) bay]->name : juce::String ("-")).toRawUTF8(), (int) (x.brk[(size_t) bay] == y.brk[(size_t) bay]), s0, (unsigned) x.stepLock[(size_t) bay], (unsigned) y.stepLock[(size_t) bay]);
            }
        if (! same)
            for (int d = 0; d < kDrums; ++d)
                std::printf ("  drum %d: %s / %s steps same %d\n", d, (p.pattern().snd[(size_t) d] ? p.pattern().snd[(size_t) d]->name : juce::String ("-")).toRawUTF8(),
                             (q.pattern().snd[(size_t) d] ? q.pattern().snd[(size_t) d]->name : juce::String ("-")).toRawUTF8(), (int) (p.pattern().drum[(size_t) d] == q.pattern().drum[(size_t) d]));
        check (same, "the arrangement, breaks and drum sounds come back with the project");
        check (std::abs (q.apvts.getRawParameterValue ("b_filter_cutoff")->load() - 777.0f) < 0.5f && std::abs (q.apvts.getRawParameterValue ("a_nudge")->load() - p.apvts.getRawParameterValue ("a_nudge")->load()) < 0.01f, "every knob comes back with the project");
        { // the newest Phantom FX engines are there, and a drawn WRECKER curve comes back with the project
            auto has = [&] (int slot, const char* label) { for (auto& sp : p.engine.slot (slot).params()) if (sp.type == phantom::ParamSpec::Type::Choice) for (auto& c : sp.choices) if (juce::String (c).startsWith (label)) return true; return false; };
            check (has (sChorus, "ORBIT") && has (sReverb, "VINTAGE") && has (sReverb, "ROOMS") && has (sReverb, "PLATES") && has (sReverb, "CLASSIC") && has (sLimiter, "APEX") && has (sLimiter, "GRANITE"),
                   "the newest Phantom FX engines: CHORUS ORBIT, HALLS VINTAGE / ROOMS / PLATES / CLASSIC, LIMITER APEX + GRANITE");
            bool types = false; for (auto& sp : p.engine.slot (filterSlot (0)).params()) if (sp.id == "type") types = sp.choices.size() == 44 && sp.choices[31] == "SAMPLER LP" && sp.choices[43] == "RESONATOR NOTCH";
            check (types, "PHANTOM FILTER's 44 types (the 31 Surge ones, then SAMPLER / ACID / WERNER / RESONATOR) in every bay's FILTERS");
            // CLIP: FL Studio's Fruity Soft Clipper (the curve and the post law as Lemon Studio measured them against FL)
            gbk::FlSoftClip c; c.set (100, 128);
            const double x = 0.9, t = 0.78125, k = 0.21875; const float want = (float) ((1.0 - std::exp ((t - x) / k)) * k + t);
            bool under = true; for (float v : { 0.0f, 0.5f, -0.78125f, 0.78125f, 1e-30f }) under = under && c.sample (v) == v;
            bool ceil = true; for (float v = 0.0f; v < 8.0f; v += 0.01f) ceil = ceil && std::abs (c.sample (v)) <= 1.0f && std::abs (c.sample (-v)) <= 1.0f;
            const bool post = gbk::FlSoftClip::postGain (128) == 1.0f && gbk::FlSoftClip::postGain (0) == 0.0f && std::abs (gbk::FlSoftClip::postGain (160) - 1.9033f) < 1e-4f;
            check (under && ceil && std::abs (c.sample (0.9f) - want) < 1e-7f && std::abs (c.sample (-0.9f) + want) < 1e-7f && post && p.engine.slot (crunchSlot (0)).params().size() == 2,
                   "each bay's CLIP is FL's Fruity Soft Clipper: untouched under the threshold, FL's curve over it, never past full scale, FL's post gain");
        }
        {   // RANDOM DRUMS: new hits on the lanes, the breaks untouched; RANDOM COMBO leaves the drums alone
            const Pattern d0 = p.pattern(); p.randomDrums(); bool bays = true, changed = false;
            for (int b = 0; b < kBays; ++b) bays = bays && sameSeq (d0, p.pattern(), b);
            for (int t = 0; t < 4 && ! changed; ++t) { for (int d = 0; d < kDrums; ++d) changed = changed || d0.drum[(size_t) d] != p.pattern().drum[(size_t) d]; if (! changed) p.randomDrums(); }
            check (bays && changed, "RANDOM DRUMS makes a new drum beat and leaves the breaks alone");
            const Pattern d1 = p.pattern(); p.randomCombo(); bool kept = true; for (int d = 0; d < kDrums; ++d) kept = kept && d1.drum[(size_t) d] == p.pattern().drum[(size_t) d];
            check (kept, "RANDOM COMBO leaves the drum beat alone");
        }
        const Pattern before = p.pattern(); p.randomCombo(); p.undo(); check (sameSeq (before, p.pattern(), 0) && sameSeq (before, p.pattern(), 1), "UNDO brings the last arrangement back");
    }
    { // 6a. RESET: a bay (or all of them) empty, and empty stays empty with the project; the break list's order; the window's size
        Processor p; p.prepareToPlay (48000.0, 512);
        p.resetBay (1);
        check (p.pattern().brk[1] == nullptr && p.pattern().brk[0] != nullptr && p.pattern().stepLock[1] == 0, "RESET empties one bay and leaves the others");
        juce::MemoryBlock mb; p.getStateInformation (mb); Processor q; q.setStateInformation (mb.getData(), (int) mb.getSize());
        check (q.pattern().brk[1] == nullptr && q.pattern().brk[0] != nullptr, "an empty bay stays empty with the project");
        p.undo(); check (p.pattern().brk[1] != nullptr, "UNDO brings the reset bay's break back");
        p.resetBays(); bool all = true; for (int b = 0; b < kBays; ++b) all = all && p.pattern().brk[(size_t) b] == nullptr;
        Head hd; auto x = render (p, 0.5, &hd); check (all && finite (x), "RESET ALL empties every bay, and an empty Ghost Break still plays (the drums)");
        const auto& cat = p.breakCatalog(); bool order = true; int last = -1, yours = 0;
        for (auto& e : cat) { order = order && e.kind >= last; last = e.kind; if (e.kind == Processor::yourBreak) ++yours; }
        check (order && yours == 0, "the break list: CLASSIC BREAKS first, then BUILT-IN, then YOUR BREAKS (empty until you add one)");
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        const int w0 = ed->getWidth(), h0 = ed->getHeight(); ed->setSize (1770, 1140);
        check (w0 >= 1180 && w0 <= 1534 && std::abs ((float) w0 / (float) h0 - 1180.0f / 760.0f) < 0.01f && ed->isResizable() && std::abs (p.uiScale() - 1.5f) < 0.01f, "the window opens big (" + juce::String (w0) + " wide), drags bigger (1.5x here) and keeps that size with the project");
        ed.reset(); ed.reset (p.createEditor()); check (ed->getWidth() == 1770, "the window reopens at the size it was left at");
        if (auto* ge = dynamic_cast<Editor*> (ed.get()))
        {   // the WATERCOLOR look: the window is rebuilt in it, paints, and comes back to neon
            p.apvts.state.setProperty ("look", "water", nullptr); ge->rebuild(); auto im1 = ge->createComponentSnapshot (ge->getLocalBounds());
            p.apvts.state.setProperty ("look", "neon", nullptr); ge->rebuild(); auto im2 = ge->createComponentSnapshot (ge->getLocalBounds());
            check (im1.isValid() && im2.isValid() && im1.getPixelAt (600, 400) != im2.getPixelAt (600, 400), "SETTINGS switches the look to WATERCOLOR and back");
        }
        if (auto* ge = dynamic_cast<Editor*> (ed.get()))
        {   // the PAGES layout: each page lays out and paints; back to classic
            bool ok = true;
            for (int pg = 0; pg < 3; ++pg) { p.apvts.state.setProperty ("ui_layout", "pages", nullptr); p.apvts.state.setProperty ("ui_page", pg, nullptr); ge->relayout(); ok = ok && ge->createComponentSnapshot (ge->getLocalBounds()).isValid(); }
            p.apvts.state.setProperty ("ui_layout", "classic", nullptr); ge->relayout();
            check (ok, "the PAGES layout shows BREAKS, DRUMS and RULES + STUTTER + SPACE, and goes back to classic");
        }
        ed.reset(); p.setUiScale (1.0f);
    }
    { // 6a2. with the real break pack there: no home-made breaks in the list, the bays open on real breaks, NEW BREAKS draws only real ones
        const auto dir = Processor().breaksFolder(); const bool had = dir.exists();
        if (! had)
        {
            juce::WavAudioFormat wav; juce::Random rr (11);
            for (int i = 0; i < 6; ++i)
            {
                const auto f = dir.getChildFile (i < 3 ? "Pack One" : "Pack Two").getChildFile ("Real Break " + juce::String (i) + " 174.wav"); f.getParentDirectory().createDirectory();
                juce::AudioBuffer<float> buf (1, (int) (44100.0 * 60.0 / 174.0 * 8.0)); for (int n = 0; n < buf.getNumSamples(); ++n) buf.setSample (0, n, (n % 7600 < 900 ? 0.6f : 0.04f) * (rr.nextFloat() * 2.0f - 1.0f));
                if (auto os = std::unique_ptr<juce::OutputStream> (f.createOutputStream()))
                    if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (os.get(), 44100.0, 1, 16, {}, 0))) { os.release(); w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples()); }
            }
            {
                Processor q; q.prepareToPlay (48000.0, 512); bool noHome = true, real = true, drawn = true;
                for (auto& e : q.breakCatalog()) noHome = noHome && e.kind != Processor::builtInBreak;
                for (int b = 0; b < kBays; ++b) real = real && q.pattern().brk[(size_t) b] != nullptr && q.pattern().brk[(size_t) b]->user;
                set (q, "rule_newbreaks", 1.0f);
                for (int k = 0; k < 5; ++k) { q.randomCombo(); for (int b = 0; b < kBays; ++b) drawn = drawn && q.pattern().brk[(size_t) b] != nullptr && q.pattern().brk[(size_t) b]->family.startsWith (dir.getFullPathName()); }
                check (noHome, "with the break pack there, the home-made breaks are not in the list");
                check (real, "the four bays open on real breaks from the pack");
                check (drawn, "RANDOM COMBO's NEW BREAKS only ever picks real breaks from the pack");
                Head hd; auto x = render (q, 1.0, &hd); check (finite (x) && peak (x) > 0.02f, "the real breaks play");
            }
            dir.deleteRecursively();
        }
    }
    { // 6a4. with the break pack there, the bays open on AMEN, APACHE, COLD SWEAT and FLOW
        Processor p; bool pack = false; for (auto& e : p.breakCatalog()) pack = pack || e.kind == Processor::classicBreak;
        if (pack)
        {
            static const char* const want[] { "KAB1_137_AMENBREAK", "KAPB1_115_APACHEBREAK", "KCSB1_112.5_COLDSWEAT", "KFB1_159_FLOWBREAK" }; bool ok = true;
            for (int b = 0; b < kBays; ++b) { const auto n = p.bayBreakName (b); std::printf ("INFO bay %c opens on %s\n", 'A' + b, n.toRawUTF8()); ok = ok && n.startsWithIgnoreCase (want[b]); }
            check (ok, "the bays open on AMEN, APACHE, COLD SWEAT and FLOW");
        }
    }
    { // 6a5. BACK TO MY BEAT: after RANDOM BREAKS, RANDOM DRUMS and RANDOM START, back to the beat from before
        Processor p; p.prepareToPlay (48000.0, 512);
        const Pattern mine = p.pattern();
        p.randomCombo(); p.randomDrums(); p.randomStart(); p.randomCombo();
        const bool could = p.canGoHome(); p.backHome();
        bool same = true; for (int b = 0; b < kBays; ++b) same = same && sameSeq (mine, p.pattern(), b);
        for (int d = 0; d < kDrums; ++d) same = same && mine.drum[(size_t) d] == p.pattern().drum[(size_t) d];
        check (could && same && ! p.canGoHome(), "BACK TO MY BEAT brings back the beat from before the RANDOM buttons");
        {   // ...and BACK TO THE BREAK flips to the random beat you just left, back and forth; a new RANDOM starts over
            Processor q; q.prepareToPlay (48000.0, 512); const Pattern m2 = q.pattern(); q.randomCombo(); const Pattern r2 = q.pattern();
            auto eq = [] (const Pattern& a, const Pattern& b) { bool e = true; for (int k = 0; k < kBays; ++k) e = e && sameSeq (a, b, k); for (int d = 0; d < kDrums; ++d) e = e && a.drum[(size_t) d] == b.drum[(size_t) d]; return e; };
            q.backHome(); const bool atHome = eq (m2, q.pattern()) && q.canGoAway() && ! q.canGoHome();
            q.backAway(); const bool atBreak = eq (r2, q.pattern()) && q.canGoHome() && ! q.canGoAway();
            q.backHome(); q.backAway(); const bool again = eq (r2, q.pattern());
            q.backHome(); q.randomCombo(); const bool fresh = ! q.canGoAway() && q.canGoHome();
            check (atHome && atBreak && again && fresh, "BACK TO THE BREAK and BACK TO MY BEAT flip back and forth; a new RANDOM starts over");
        }
        // the stutter on the drums, and on both
        set (p, "stutter", 1.0f); bool ok = true;
        for (float t : { 1.0f, 2.0f, 0.0f }) { set (p, "stut_target", t); Head hd; auto x = render (p, 0.5, &hd); ok = ok && finite (x); }
        check (ok, "HOLD stutters the breaks, the drums or both");
    }
    { // 6a7. CRUSH: WRECKER's TUBE and CRACKLE's CURVES DOOMCOMPPARALLEL, blended
        gbk::CrushFx fx; check (fx.doomEngineEntry() > 0, "CRUSH finds CRACKLE's CURVES DOOMCOMPPARALLEL");
        Processor p; p.prepareToPlay (48000.0, 512); set (p, "a_mute", 0.0f);
        p.reset(); Head hd; auto clean = render (p, 1.5, &hd); std::printf ("INFO crush clean peak %.3f\n", peak (clean));
        set (p, "a_crush_on", 1.0f); set (p, "a_crush_drive", 0.8f);
        double e[2] {}; bool ok = true;
        for (int k = 0; k < 2; ++k)
        {
            set (p, "a_crush_blend", (float) k); p.reset(); auto x = render (p, 1.5, &hd); ok = ok && finite (x);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < x.getNumSamples(); ++i) { const double d = x.getSample (c, i) - clean.getSample (c, i); e[k] += d * d; }
            std::printf ("INFO crush blend %d: difference from clean %.3f\n", k, std::sqrt (e[k]));
        }
        check (ok && e[0] > 1e-3 && e[1] > 1e-3 && p.engine.slot (crushSlot (0)).getLatencySamples() >= 0, "CRUSH changes the bay's sound at TUBE and at DOOM, nothing breaks");
    }
    { // 6a8. FILTER +DRUMS: the drums go through the bay's filter too
        auto drumsOnly = [] (bool through) {
            Processor p; p.prepareToPlay (48000.0, 512); for (auto b : { "a_", "b_", "c_", "d_" }) set (p, juce::String (b) + "mute", 1.0f);
            set (p, "a_filter_on", 1.0f); set (p, "a_filter_cutoff", 0.0f); set (p, "a_filter_drums", through ? 1.0f : 0.0f);
            Head hd; auto x = render (p, 1.5, &hd); return std::make_pair (finite (x), x.getRMSLevel (0, 0, x.getNumSamples())); };
        const auto off = drumsOnly (false), on = drumsOnly (true);
        std::printf ("INFO drums rms: filter not on drums %.4f, +DRUMS %.4f\n", off.second, on.second);
        check (off.first && on.first && off.second > 0.001f && on.second < off.second * 0.7f, "FILTER +DRUMS filters the drums too (and they stay in time)");
    }
    { // 6a6. TEMPO 1/2x | 1x | 2x and DRAG WAV
        Processor p; p.prepareToPlay (48000.0, 512); set (p, "bpm", 120.0f); set (p, "a_mute", 0.0f);
        bool ok = true; double len[3] {};
        for (int t = 0; t < 3; ++t)
        {
            set (p, "tempo_mul", (float) t);
            const auto f = p.renderWav (-1); juce::AudioFormatManager fm; fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
            ok = ok && r != nullptr && r->numChannels == 2 && f.getFileName().contains (t == 0 ? "60bpm" : t == 1 ? "120bpm" : "240bpm");
            if (r != nullptr) { len[t] = (double) r->lengthInSamples / r->sampleRate; float lo[2] {}, hi[2] {}; r->readMaxLevels (0, r->lengthInSamples, lo[0], hi[0], lo[1], hi[1]); ok = ok && hi[0] > 0.01f && std::isfinite (hi[0]); }
            std::printf ("INFO tempo %s: %s, %.3f s\n", t == 0 ? "1/2x" : t == 1 ? "1x" : "2x", f.getFileName().toRawUTF8(), len[t]);
            f.deleteFile();
        }
        // 32 steps = 8 beats: 4 s at 120, 8 s at 1/2x (60), 2 s at 2x (240)
        check (ok && std::abs (len[1] - 4.0) < 0.01 && std::abs (len[0] - 8.0) < 0.01 && std::abs (len[2] - 2.0) < 0.01, "TEMPO 1/2x | 1x | 2x: the beat at 60 / 120 / 240 when the tempo is 120, and DRAG WAV renders one loop of it with the BPM in its name");
        const auto fb = p.renderWav (0); check (fb.existsAsFile() && fb.getFileName().startsWith ("Ghost Break Bay A"), "DRAG WAV on a bay renders that bay alone"); fb.deleteFile();
        set (p, "tempo_mul", 2.0f); auto x = render (p, 1.0); check (finite (x), "the beat plays at 2x");
        set (p, "tempo_mul", 0.0f); x = render (p, 1.0); check (finite (x), "the beat plays at 1/2x");
    }
    { // 6a3. the reverb starts on Brandon's setting: ROOMS · Deep Space, size 76 %
        Processor p; auto* e = dynamic_cast<juce::AudioParameterChoice*> (p.apvts.getParameter ("reverb_engine"));
        auto* sz = p.apvts.getRawParameterValue ("reverb_size");
        check (e != nullptr && e->getCurrentChoiceName().endsWith ("Deep Space") && sz != nullptr && std::abs (sz->load() - 0.76f) < 0.001f, "the reverb opens on ROOMS - Deep Space, size 76 %");
    }
    { // 6b. free lanes: any sound in any lane, own names, the closed hat chokes the open hat
        Processor p; const auto all = p.soundsFor (dPerc2); int kickIdx = -1, ohIdx = -1, total = (int) sharedLibrary()->drums.sounds().size();
        for (int i = 0; i < (int) all.size(); ++i) { if (kickIdx < 0 && all[(size_t) i]->category == "Kick") kickIdx = i; if (ohIdx < 0 && all[(size_t) i]->category == "Open Hat") ohIdx = i; }
        check ((int) all.size() >= total && kickIdx >= 0, "every lane offers every sound (" + juce::String ((int) all.size()) + ")");
        for (int d = 0; d < kDrums; ++d) { const auto v = p.soundsFor (d); for (int i = 0; i < (int) v.size(); ++i) if (v[(size_t) i]->category == "Kick") { p.setDrumSound (d, i); break; } }
        bool kicks = true; for (int d = 0; d < kDrums; ++d) kicks = kicks && p.pattern().snd[(size_t) d] && p.pattern().snd[(size_t) d]->category == "Kick";
        p.setLaneName (dPerc1, "second kick"); p.setLaneName (dKick, "KICK");
        check (kicks && p.laneName (dPerc1) == "SECOND KICK" && p.laneName (dKick) == "KICK" && p.pattern().laneName[0].isEmpty(), "six kicks in six lanes, and a lane takes a typed name");
        juce::MemoryBlock mb; p.getStateInformation (mb); Processor q; q.setStateInformation (mb.getData(), (int) mb.getSize());
        check (q.laneName (dPerc1) == "SECOND KICK" && q.pattern().snd[dPerc1] == p.pattern().snd[dPerc1], "lane names and sounds come back with the project");
        // choke: an open hat on step 0, a closed hat on step 2. With CHOKE on the open hat stops right after the closed hat
        auto tail = [&] (bool choke) {
            Processor h; h.prepareToPlay (48000.0, 512); set (h, "a_mute", 1);
            set (h, "k_level", 0); set (h, "s_level", 0); set (h, "p1_level", 0); set (h, "p2_level", 0); set (h, "h_level", 0.75f); set (h, "o_level", 0.75f); set (h, "choke", choke ? 1.0f : 0.0f);
            {   // the longest open hat there is (whatever kit the lanes start on, the choke needs a hat that rings)
                const auto v = h.soundsFor (dOpenHat); int best = -1; size_t len = 0;
                for (int i = 0; i < (int) v.size(); ++i) if (v[(size_t) i]->category == "Open Hat" && v[(size_t) i]->data.size() > len) { len = v[(size_t) i]->data.size(); best = i; }
                if (best >= 0) h.setDrumSound (dOpenHat, best);
            }
            h.edit ([] (Pattern& pt) { for (auto& l : pt.drum) l.fill (0); pt.drum[dOpenHat][0] = 120; pt.drum[dHat][2] = 30; });
            set (h, "h_level", 0.0f);   // the closed hat silent: what is left after it is the open hat alone
            Head hd; auto x = render (h, 0.4, &hd); const int from = h.getLatencySamples() + (int) (48000.0 * 60.0 / 172.0 * 0.5) + 400;
            return x.getMagnitude (0, from, 4000); };
        const float on = tail (true), off = tail (false);
        check (off > 0.01f && on < off * 0.05f, "CLOSED HAT chokes the OPEN HAT (" + juce::String (on, 4) + " vs " + juce::String (off, 4) + " without the choke)");
    }
    { // 6. separate outputs
        Processor p; auto lay = p.getBusesLayout(); for (int i = 1; i < lay.outputBuses.size(); ++i) lay.outputBuses.getReference (i) = juce::AudioChannelSet::stereo();
        const bool ok = p.setBusesLayout (lay); p.prepareToPlay (48000.0, 512);
        soloBay (p, true); set (p, "a_out", 1); set (p, "drums_out", 1);
        auto x = render (p, 1.0, &head, 512, 12);
        check (ok && peak (x, 2) > 0.02f && peak (x, 10) > 0.02f && peak (x, 0) < 1.0e-3f && peak (x, 4) < 1.0e-6f, "separate outputs: bay A and the drums leave the main output and play on their own");
    }
}
void realBreaks (const char* dir)
{   // 7. the owner's break pack: every file loads, finds its tempo from its name, and plays in time
    juce::File root (dir); if (! root.isDirectory()) return;
    auto files = root.findChildFiles (juce::File::findFiles, true, "*.wav"); int ok = 0, tempo = 0, bad = 0; juce::String worst;
    Processor p; p.prepareToPlay (48000.0, 512);
    for (auto& f : files)
    {
        juce::String err; if (! p.addUserBreak (f, 0, err)) { ++bad; worst = f.getFileName() + ": " + err; continue; }
        ++ok; const auto& br = *p.pattern().brk[0];
        if (br.tempoFound && br.slices >= 8 && br.slices % 4 == 0) ++tempo; else if (worst.isEmpty()) worst = f.getFileName() + " -> " + juce::String (br.bpm, 1) + " bpm, " + juce::String (br.slices) + " slices";
    }
    std::printf ("INFO break pack: %d files, %d loaded, %d with a clean tempo/bar grid; first odd one: %s\n", files.size(), ok, tempo, worst.toRawUTF8());
    check (bad == 0 && ok == files.size(), "every file in the break pack loads");
    check (tempo >= ok * 9 / 10, "nearly every break finds its tempo and a whole number of beats");
    Head h; auto x = render (p, 2.0, &h); check (finite (x) && peak (x) > 0.05f && peak (x) <= 1.0f, "a real break plays");
}
// 9. the Drums folder: every file shows up in its lane's sound menu and plays
int userDrums()
{
    Processor p; p.prepareToPlay (48000.0, 512); int counts[kDrums] {}, user = 0;
    for (auto& snd : p.soundsFor (0)) if (! snd->data.empty() && snd->rate > 0 && ! sharedLibrary()->drums.byName (snd->name)) { for (int d = 0; d < kDrums; ++d) if (snd->category == drumCategory (d)) ++counts[d]; ++user; }
    std::printf ("INFO drums folder: %d kicks, %d snares, %d closed hats, %d open hats, %d claps, %d percs\n", counts[0], counts[1], counts[2], counts[3], counts[4], counts[5]);
    { bool firstOwn = true; for (int d = 0; d < kDrums; ++d) { auto v = p.soundsFor (d); if (! v.empty() && counts[d] > 0) firstOwn = firstOwn && v.front()->category == drumCategory (d) && ! sharedLibrary()->drums.byName (v.front()->name); }
      check (firstOwn, "each lane lists its own kind of your sounds first"); }
    check (user > 0, "the Drums folder's sounds are in the menus");
    for (int d = 0; d < kDrums; ++d) p.setDrumSound (d, 0);
    for (int b = 0; b < kBays; ++b) set (p, juce::String (bayPrefix (b)) + "level", 0.0f);
    Head h; auto x = render (p, 2.0, &h); check (finite (x) && peak (x) > 0.05f && peak (x) <= 1.0f, "the user drums play");
    juce::MemoryBlock mb; p.getStateInformation (mb); Processor q; q.setStateInformation (mb.getData(), (int) mb.getSize());
    bool same = true; for (int d = 0; d < kDrums; ++d) same = same && p.pattern().snd[(size_t) d] && q.pattern().snd[(size_t) d] && p.pattern().snd[(size_t) d]->name == q.pattern().snd[(size_t) d]->name;
    check (same, "a user drum comes back with the project");
    std::printf ("%d passed, %d failed\n", passed, failed); return failed == 0 ? 0 : 1;
}
// 10. PHANTOM FILTER's extra models (Airwindows Z series, chowdsp Werner + ARP): each one filters the way its name says, stays finite
//     and bounded at full resonance + drive under fast modulation, sits near the LOW PASS model's level, and its CPU is reported.
int filterModels (bool calibrate)
{
    using namespace phantom; const double sr = 48000.0; const int bs = 512;
    auto make = [&] (int type, std::function<void (std::vector<ParamSpec>&, std::vector<float>&)> tweak) {
        auto slot = std::make_unique<EffectSlot> (makeEffect (uFilter)); slot->setPolicy (OversamplingPolicy::X2);
        auto specs = slot->params(); std::vector<float> v; for (auto& q : specs) v.push_back (q.def);
        auto at = [&] (const char* id) { for (size_t i = 0; i < specs.size(); ++i) if (specs[i].id == id) return (int) i; return -1; };
        v[(size_t) at ("type")] = (float) type; v[(size_t) at ("drive")] = 0.0f; v[(size_t) at ("cutoff")] = 1000.0f; v[(size_t) at ("reso")] = 0.3f;
        v[(size_t) at ("lfoamt")] = 0.0f; v[(size_t) at ("envamt")] = 0.0f;
        tweak (specs, v);
        for (size_t i = 0; i < v.size(); ++i) slot->setValue ((int) i, v[i]);
        slot->prepare (sr, bs, 2); slot->setEnabled (true); slot->setMix (1.0f); slot->applyPolicy(); return slot; };
    auto run = [&] (EffectSlot& s, std::function<float (int)> src, int total, juce::AudioBuffer<float>& out) {
        out.setSize (2, total); TempoInfo ti; ti.bpm = 172; ti.playing = true; ti.sampleRate = sr; juce::AudioBuffer<float> b (2, bs);
        for (int o = 0; o < total; o += bs) { for (int i = 0; i < bs; ++i) { const float x = o + i < total ? src (o + i) : 0.0f; b.setSample (0, i, x); b.setSample (1, i, x); }
            ti.ppqPosition = ti.ppqAtBlockStart = o / sr * 172.0 / 60.0; s.process (juce::dsp::AudioBlock<float> (b), ti); for (int c = 0; c < 2; ++c) out.copyFrom (c, o, b, c, 0, juce::jmin (bs, total - o)); } };
    auto rmsDb = [] (const juce::AudioBuffer<float>& b, int from) { double e = 0; int n = 0; for (int i = from; i < b.getNumSamples(); ++i) { e += b.getSample (0, i) * b.getSample (0, i); ++n; } return 10.0 * std::log10 (e / juce::jmax (1, n) + 1.0e-20); };
    auto sineGain = [&] (int type, float hz) { auto s = make (type, [] (auto&, auto&) {}); juce::AudioBuffer<float> o; const float w = juce::MathConstants<float>::twoPi * hz / (float) sr;
        run (*s, [w] (int i) { return 0.25f * std::sin (w * (float) i); }, (int) sr / 2, o); return rmsDb (o, (int) sr / 4) - 10.0 * std::log10 (0.25 * 0.25 * 0.5); };
    juce::Random rnd (11); float pinkB[7] {};
    auto pink = [&] (int) { const float w = rnd.nextFloat() * 2 - 1; pinkB[0] = 0.99886f * pinkB[0] + w * 0.0555179f; pinkB[1] = 0.99332f * pinkB[1] + w * 0.0750759f; pinkB[2] = 0.96900f * pinkB[2] + w * 0.1538520f;
        pinkB[3] = 0.86650f * pinkB[3] + w * 0.3104856f; pinkB[4] = 0.55000f * pinkB[4] + w * 0.5329522f; pinkB[5] = -0.7616f * pinkB[5] - w * 0.0168980f;
        const float y = pinkB[0] + pinkB[1] + pinkB[2] + pinkB[3] + pinkB[4] + pinkB[5] + pinkB[6] + w * 0.5362f; pinkB[6] = w * 0.115926f; return y * 0.06f; };
    juce::AudioBuffer<float> o; { auto s = make (0, [] (auto&, auto&) {}); run (*s, pink, (int) sr * 2, o); } const double refDb = rmsDb (o, (int) sr);
    for (int m = 0; m < FilterModels::kNumModels; ++m)
    {
        const int type = PhantomFilterFx::kSurgeTypes + m; const juce::String nm = FilterModels::name (m);
        const double g100 = sineGain (type, 100), g1k = sineGain (type, 1000), g10k = sineGain (type, 10000);
        if (calibrate && m < 4) for (float cut : { 300.0f, 1000.0f, 4000.0f })
        {   // the passband (two octaves away from the cutoff) at three cutoffs
            const float probe = m == 1 ? juce::jmin (16000.0f, cut * 4.0f) : m == 2 ? cut : cut / 4.0f;
            auto s2 = make (type, [cut] (std::vector<ParamSpec>& sp, std::vector<float>& v) { for (size_t i = 0; i < sp.size(); ++i) if (sp[i].id == "cutoff") v[i] = cut; });
            juce::AudioBuffer<float> o2; const float w = juce::MathConstants<float>::twoPi * probe / (float) sr;
            run (*s2, [w] (int i) { return 0.25f * std::sin (w * (float) i); }, (int) sr / 2, o2);
            std::printf ("PASSBAND %s cut %.0f probe %.0f: %.2f dB\n", nm.toRawUTF8(), cut, probe, rmsDb (o2, (int) sr / 4) - 10.0 * std::log10 (0.25 * 0.25 * 0.5));
        }
        bool shape = false;
        if (nm == "ACID LP") shape = g100 > g1k + 2.0 && g1k > g10k && g100 > -3.0;   // its own gentle, squelchy slope
        else if (nm.endsWith ("LP") || nm.contains ("LP ")) shape = g100 > g10k + 18.0 && g100 > -4.0;
        else if (nm.endsWith ("HP")) shape = g10k > g100 + 18.0 && g10k > -4.0;
        else if (nm.endsWith ("BP")) shape = g1k > g100 + 3.0 && g1k > g10k + 3.0;
        else shape = g1k < g100 - 8.0 && g1k < g10k - 8.0;
        check (shape, nm + " filters as it says (100 Hz " + juce::String (g100, 1) + " dB, 1 kHz " + juce::String (g1k, 1) + " dB, 10 kHz " + juce::String (g10k, 1) + " dB)");
        { rnd.setSeed (11); for (auto& x : pinkB) x = 0; auto s = make (type, [] (auto&, auto&) {}); run (*s, pink, (int) sr * 2, o);
          const double d = rmsDb (o, (int) sr) - refDb;
          if (calibrate) std::printf ("CAL %s %.4f\n", nm.toRawUTF8(), std::pow (10.0, -d / 20.0) * FilterModels::makeup (m, 1000.0f));
          juce::ignoreUnused (d); }
        {   // the passband level stays put while the cutoff moves (two octaves from the cutoff; at the cutoff for a band-pass)
            double lo = 1e9, hi = -1e9;
            for (float cut : { 300.0f, 1000.0f, 4000.0f })
            {
                const float probe = nm.endsWith ("HP") ? juce::jmin (16000.0f, cut * 4.0f) : nm.endsWith ("BP") ? cut : cut / 4.0f;
                auto s2 = make (type, [cut] (std::vector<ParamSpec>& sp, std::vector<float>& v) { for (size_t i = 0; i < sp.size(); ++i) { if (sp[i].id == "cutoff") v[i] = cut; if (sp[i].id == "reso") v[i] = 0.0f; } });
                juce::AudioBuffer<float> o2; const float w = juce::MathConstants<float>::twoPi * probe / (float) sr;
                run (*s2, [w] (int i) { return 0.25f * std::sin (w * (float) i); }, (int) sr / 2, o2);
                const double g = rmsDb (o2, (int) sr / 4) - 10.0 * std::log10 (0.25 * 0.25 * 0.5); lo = juce::jmin (lo, g); hi = juce::jmax (hi, g);
            }
            check (lo > -2.0 && hi < 1.0, nm + " passband level at 300 Hz / 1 kHz / 4 kHz cutoffs, resonance 0: " + juce::String (lo, 1) + " .. " + juce::String (hi, 1) + " dB");
        }
        { auto s = make (type, [] (std::vector<ParamSpec>& sp, std::vector<float>& v) { for (size_t i = 0; i < sp.size(); ++i) { if (sp[i].id == "reso") v[i] = 1.0f; if (sp[i].id == "drive") v[i] = 1.0f; if (sp[i].id == "safe") v[i] = 0.0f;
              if (sp[i].id == "lfoamt") v[i] = 1.0f; if (sp[i].id == "lfosync") v[i] = 0.0f; if (sp[i].id == "lforate") v[i] = 20.0f; if (sp[i].id == "envamt") v[i] = 1.0f; if (sp[i].id == "routing") v[i] = 2.0f; if (sp[i].id == "poles") v[i] = 1.0f; } });
          rnd.setSeed (5); const auto t0 = juce::Time::getMillisecondCounterHiRes(); run (*s, [&] (int i) { return (i / 4800) % 2 ? 0.9f * (rnd.nextFloat() * 2 - 1) : 0.0f; }, (int) sr * 3, o); const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
          bool fin = true; for (int c = 0; c < 2; ++c) for (int i = 0; i < o.getNumSamples(); ++i) fin = fin && std::isfinite (o.getSample (c, i));
          const float pk = o.getMagnitude (0, o.getNumSamples());
          check (fin && pk <= 6.0f, nm + " at full resonance + drive, fast LFO, envelope, parallel, steep: finite and bounded (peak " + juce::String (pk, 2) + ", " + juce::String (ms / 30.0, 1) + " % of a core)"); }
        {   // silence in, silence out (the Phantom FX latency check: nothing above 1e-7 before an impulse, oversampling off)
            auto s = make (type, [] (std::vector<ParamSpec>& sp, std::vector<float>& v) { for (size_t i = 0; i < sp.size(); ++i) { if (sp[i].id == "cutoff") v[i] = 20000.0f; if (sp[i].id == "reso") v[i] = 0.0f; } });
            s->setPolicy (OversamplingPolicy::Off); s->applyPolicy();
            run (*s, [] (int i) { return i == 4096 ? 0.5f : 0.0f; }, 8192, o); int first = -1; for (int i = 0; i < o.getNumSamples(); ++i) if (std::abs (o.getSample (0, i)) > 1.0e-7f) { first = i - 4096; break; }
            check (first == 0, nm + " answers an impulse on its own sample, nothing before it (" + juce::String (first) + ")");
        }
        {   // with SELF-OSCILLATION SAFE on (the default): full resonance and drive stay tame
            auto s = make (type, [] (std::vector<ParamSpec>& sp, std::vector<float>& v) { for (size_t i = 0; i < sp.size(); ++i) { if (sp[i].id == "reso") v[i] = 1.0f; if (sp[i].id == "drive") v[i] = 1.0f; if (sp[i].id == "safe") v[i] = 1.0f; } });
            rnd.setSeed (6); run (*s, [&] (int i) { return (i / 4800) % 2 ? 0.9f * (rnd.nextFloat() * 2 - 1) : 0.0f; }, (int) sr * 2, o);
            const float pk = o.getMagnitude (0, o.getNumSamples()); check (pk <= 3.0f, nm + " with SAFE on at full resonance + drive: peak " + juce::String (pk, 2));
        }
    }
    std::printf ("%d passed, %d failed\n", passed, failed); return failed == 0 ? 0 : 1;
}
// 8. what pluginval's "Audio processing" test does, from a background thread, while the message thread runs the editor-less
//    plugin's timer (as in a host): prepare / process / release at several rates and block sizes, noise in every channel, MIDI notes
int hostTorture()
{
    Processor p; std::atomic<bool> done { false }; std::atomic<int> blocks { 0 };
    juce::Thread::launch ([&] {
        juce::Random r (7);
        for (int round = 0; round < 3; ++round)
            for (double sr : { 44100.0, 48000.0, 96000.0, 192000.0, 22050.0 })
                for (int bs : { 64, 128, 256, 512, 1024, 1, 4096 })
                {
                    p.releaseResources(); p.prepareToPlay (sr, bs);
                    juce::AudioBuffer<float> b (2, bs); juce::MidiBuffer m;
                    for (int k = 0; k < 40; ++k)
                    {
                        for (int c = 0; c < 2; ++c) for (int i = 0; i < bs; ++i) b.setSample (c, i, r.nextFloat() * 2 - 1);
                        m.clear(); if (k % 3 == 0) m.addEvent (juce::MidiMessage::noteOn (1, 36 + r.nextInt (30), (juce::uint8) 100), r.nextInt (bs));
                        if (k % 5 == 0) m.addEvent (juce::MidiMessage::allNotesOff (1), 0);
                        const int n = k % 7 == 0 ? juce::jmax (1, bs / 3) : bs; juce::AudioBuffer<float> sub (b.getArrayOfWritePointers(), 2, n);
                        p.processBlock (sub, m); blocks.fetch_add (1);
                        for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) if (! std::isfinite (sub.getSample (c, i))) { std::printf ("FAIL non-finite at sr %g bs %d\n", sr, bs); std::fflush (stdout); }
                        if (k == 20) { p.randomCombo(); }
                    }
                }
        done = true; });
    juce::Random r2 (3); std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor()); int tick = 0;
    while (! done.load())
    {
        if (auto* e = dynamic_cast<Editor*> (ed.get())) { if (++tick % 7 == 0) e->showFx (r2.nextInt (kNumSlots + 2) - 1); if (tick % 23 == 0) { p.randomCombo(); p.randomStart(); } if (tick % 61 == 0) p.undo(); }
        if (tick % 97 == 0) ed.reset (p.createEditor());   // close and reopen the window meanwhile
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
        // the user (or host automation) moving latency-changing choices meanwhile
        if (auto* q = p.apvts.getParameter ("limit_lookahead")) q->setValueNotifyingHost (r2.nextFloat());
        if (auto* q = p.apvts.getParameter ("limit_quality")) q->setValueNotifyingHost (r2.nextFloat());
        if (auto* q = p.apvts.getParameter ("a_filter_type")) q->setValueNotifyingHost (r2.nextFloat());
        if (auto* q = p.apvts.getParameter ("stut_type")) q->setValueNotifyingHost (r2.nextFloat());
    }
    ed.reset(); std::printf ("PASS host torture: %d blocks, editor open, closed and reopened\n", blocks.load()); return 0;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc >= 3 && juce::String (argv[1]) == "shot")
    {
        Processor p; p.prepareToPlay (48000.0, 512);
        if (auto* q = p.apvts.getParameter ("play")) q->setValueNotifyingHost (1.0f);
        { juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m; for (int i = 0; i < 300; ++i) { b.clear(); p.processBlock (b, m); } }
        p.randomCombo();
        for (int a = 5; a < argc; ++a) if (juce::String (argv[a]) == "classic") for (auto& e : p.drumCatalog()) if (e.source == "CLASSIC BREAKS" && e.tab == 1) { p.setDrumSound (0, e.s); break; }
        for (int a = 5; a < argc; ++a) { const juce::String kv (argv[a]); if (auto* q = p.apvts.getParameter (kv.upToFirstOccurrenceOf ("=", false, false))) q->setValueNotifyingHost (q->convertTo0to1 (kv.fromFirstOccurrenceOf ("=", false, false).getFloatValue())); }
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        for (int a = 5; a < argc; ++a) { const juce::String kv (argv[a]); if (kv.startsWith ("width=")) { const int w = kv.fromFirstOccurrenceOf ("=", false, false).getIntValue(); ed->setSize (w, w * 760 / 1180); } }
        for (int a = 5; a < argc; ++a) if (juce::String (argv[a]) == "empty") p.resetBay (3);
        for (int a = 5; a < argc; ++a) if (juce::String (argv[a]).startsWith ("paper=")) p.apvts.state.setProperty ("paper", juce::String (argv[a]).fromFirstOccurrenceOf ("=", false, false).getIntValue(), nullptr);
        for (int a = 5; a < argc; ++a) if (juce::String (argv[a]) == "water") { p.apvts.state.setProperty ("look", "water", nullptr); if (auto* ge = dynamic_cast<Editor*> (ed.get())) ge->rebuild(); }
        for (int a = 5; a < argc; ++a) if (juce::String (argv[a]).startsWith ("pages")) { p.apvts.state.setProperty ("ui_layout", "pages", nullptr); p.apvts.state.setProperty ("ui_page", juce::String (argv[a]).getTrailingIntValue(), nullptr); if (auto* ge = dynamic_cast<Editor*> (ed.get())) ge->relayout(); }
        for (int a = 5; a < argc; ++a) if (juce::String (argv[a]) == "bigbay") { p.apvts.state.setProperty ("bay_h", "176,80,80,80", nullptr); if (auto* ge = dynamic_cast<Editor*> (ed.get())) ge->relayout(); }
        for (int a = 5; a < argc; ++a) if (juce::String (argv[a]) == "steps") { p.apvts.state.setProperty ("steps_open", 15, nullptr); if (auto* ge = dynamic_cast<Editor*> (ed.get())) ge->relayout(); }
        if (argc >= 4) if (auto* e = dynamic_cast<Editor*> (ed.get())) e->showFx (juce::String (argv[3]).getIntValue());
        const float scale = argc >= 5 ? juce::String (argv[4]).getFloatValue() : 1.0f;
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, scale);
        juce::File f (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2])); f.deleteFile();
        juce::FileOutputStream os (f); juce::PNGImageFormat png; const bool ok = os.openedOk() && png.writeImageToStream (img, os);
        std::printf ("shot %s %dx%d %s\n", f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight(), ok ? "ok" : "FAILED"); return ok ? 0 : 1;
    }
    if (argc >= 3 && juce::String (argv[1]) == "breaks") { realBreaks (argv[2]); std::printf ("%d passed, %d failed\n", passed, failed); return failed == 0 ? 0 : 1; }
    if (argc >= 2 && juce::String (argv[1]) == "torture") return hostTorture();
    if (argc >= 2 && juce::String (argv[1]) == "drums") return userDrums();
    if (argc >= 2 && juce::String (argv[1]) == "idle")
    {   // the grey-out rules (phantom-fx effects/ControlUse.h)
        using namespace phantom;
        for (int u = 0; u < kNumUnits; ++u)
        {
            std::set<std::string> have; for (auto& sp : unitParams (u)) have.insert (sp.id);
            std::string missing; for (auto& id : controlRuleIds (u)) if (! have.count (id)) missing += id + " ";
            check (missing.empty(), juce::String ("every control the grey-out rules name exists: ") + unitDef (u).key + (missing.empty() ? "" : " (missing: " + missing + ")"));
        }
        auto at = [] (int u, std::map<std::string, float> set) {
            std::map<std::string, float> v; for (auto& sp : unitParams (u)) v[sp.id] = sp.def; for (auto& kv : set) v[kv.first] = kv.second;
            return [v] (const std::string& id) { auto it = v.find (id); return it == v.end() ? 0.0f : it->second; }; };
        auto engineIndex = [] (int u, const std::string& name) { const auto l = engines::engineLabels (u); for (size_t i = 0; i < l.size(); ++i) if (l[i].rfind (name, 0) == 0) return (float) i; return -1.0f; };
        auto idle = [] (int u, const char* id, const ControlValue& v) { return ! controlIdleReason (u, id, v).empty(); };
        // at the defaults nothing a default sound uses is greyed (each unit's defaults, PHANTOM engine)
        for (int u = 0; u < kNumUnits; ++u) { const auto v = at (u, {}); for (auto& sp : unitParams (u)) if (sp.id == "cutoff" || sp.id == "drive" || sp.id == "decay" || sp.id == "ceiling") check (! idle (u, sp.id.c_str(), v), juce::String ("not greyed at the defaults: ") + unitDef (u).key + " " + sp.id); }
        check (idle (uFilter, "reso", at (uFilter, { { "type", 31 } })) && idle (uFilter, "reso", at (uFilter, { { "type", 34 } })) && ! idle (uFilter, "reso", at (uFilter, { { "type", 35 } })) && ! idle (uFilter, "reso", at (uFilter, { { "type", 0 } })),
               "PHANTOM FILTER: RESONANCE greys out on the four SAMPLER types only");
        check (idle (uFilter, "spread", at (uFilter, { { "routing", 0 } })) && ! idle (uFilter, "spread", at (uFilter, { { "routing", 2 } })), "PHANTOM FILTER: SPREAD greys out in SINGLE routing");
        check (idle (uFilter, "lforate", at (uFilter, { { "lfosync", 1 } })) && idle (uFilter, "lfodiv", at (uFilter, { { "lfosync", 0 } })), "PHANTOM FILTER: LFO RATE / DIVISION follow LFO SYNC");
        check (idle (uFilter, "poles", at (uFilter, { { "type", 40 } })) && ! idle (uFilter, "poles", at (uFilter, { { "type", 0 } })), "PHANTOM FILTER: SLOPE greys out on one-slope types");
        check (idle (uHalls, "shimmer", at (uHalls, { { "engine", engineIndex (uHalls, "VINTAGE") } })) && ! idle (uHalls, "decay", at (uHalls, { { "engine", engineIndex (uHalls, "VINTAGE") } })) && ! idle (uHalls, "shimmer", at (uHalls, {})),
               "HAUNTED HALLS: SHIMMER greys out on VINTAGE, DECAY does not; both live on PHANTOM");
        check (idle (uLim, "adaptive", at (uLim, { { "engine", engineIndex (uLim, "APEX") } })) && idle (uLim, "release", at (uLim, { { "engine", engineIndex (uLim, "CLASSIC") } })) && ! idle (uLim, "quality", at (uLim, { { "engine", engineIndex (uLim, "GRANITE") } })),
               "GHOST LIMITER: each engine greys what it does not read");
        check (idle (uWreck, "style", at (uWreck, { { "engine", engineIndex (uWreck, "CURVES") } })) && ! idle (uWreck, "drive", at (uWreck, { { "engine", engineIndex (uWreck, "DRAW") } })), "WRECKER: STYLE greys out on CURVES / DRAW, DRIVE stays");
        check (idle (uShimmer, "size", at (uShimmer, { { "engine", engineIndex (uShimmer, "HALO") } })), "SHIMMER: SIZE greys out on HALO (its DECAY is the size)");
        check (idle (uEcho, "time", at (uEcho, { { "sync", 1 } })) && ! idle (uEcho, "time", at (uEcho, { { "sync", 0 } })) && idle (uEcho, "spread", at (uEcho, { { "nodes", 1 } })), "ECTO ECHO: TIME follows SYNC, SPREAD needs more than one node");
        check (idle (uRetro, "ntype", at (uRetro, { { "noise_on", 0 } })) && ! idle (uRetro, "ntype", at (uRetro, { { "noise_on", 1 } })), "RETRO: a section's knobs follow its switch");
        check (idle (uEq, "1_gain", at (uEq, { { "1_on", 1 }, { "1_type", 3 } })) && ! idle (uEq, "1_gain", at (uEq, { { "1_on", 1 }, { "1_type", 0 } })) && idle (uEq, "1_freq", at (uEq, { { "1_on", 0 } })), "SPIRIT EQ: GAIN greys out on a cut, a band's controls when it is off");
        check (idle (uGate, "pthr", at (uGate, { { "pmode", 1 } })) && ! idle (uGate, "pthr", at (uGate, { { "pmode", 5 } })), "GHOST GATE: SIDECHAIN THRESHOLD only in SIDECHAIN");
        check (! idle (uFilter, "engine", at (uFilter, {})) && ! idle (uHalls, "mix", at (uHalls, {})), "ENGINE and MIX never grey out");
        for (int u = 0; u < kNumUnits; ++u)
        {   // what is greyed at each unit's defaults, and on each ENGINE (informational: the audit's view)
            auto show = [&] (const char* label, const ControlValue& v) { std::string g; for (auto& sp : unitParams (u)) if (! controlIdleReason (u, sp.id, v).empty() && sp.id.find ('_') == std::string::npos) g += sp.id + " "; if (! g.empty()) std::printf ("GREY %s %s: %s\n", unitDef (u).key, label, g.c_str()); };
            show ("defaults", at (u, {}));
            juce::StringArray seen; const auto l = engines::engineLabels (u);
            for (size_t i = 1; i < l.size(); ++i) { const auto e = juce::String (l[i]).upToFirstOccurrenceOf (" \xc2\xb7", false, false); if (seen.contains (e)) continue; seen.add (e); show (e.toRawUTF8(), at (u, { { "engine", (float) i } })); }
        }
        std::printf ("%d passed, %d failed\n", passed, failed); return failed == 0 ? 0 : 1;
    }
    if (argc >= 2 && juce::String (argv[1]) == "catalog")
    {   // every Phantom FX unit's controls and ENGINE entries (one line each), for audits
        for (int u = 0; u < phantom::kNumUnits; ++u)
        {
            std::printf ("UNIT %d %s:", u, phantom::unitDef (u).key);
            for (auto& sp : phantom::unitParams (u)) { std::printf (" %s", sp.id.c_str()); if (sp.type == phantom::ParamSpec::Type::Choice && sp.id != "engine") { std::printf ("["); for (auto& c : sp.choices) std::printf ("%s|", c.c_str()); std::printf ("]"); } }
            juce::StringArray eng; for (auto& l : phantom::engines::engineLabels (u)) { const auto e = juce::String (l).upToFirstOccurrenceOf (" \xc2\xb7", false, false); if (! eng.contains (e)) eng.add (e); }
            std::printf ("\n  ENGINES: %s\n", eng.joinIntoString (", ").toRawUTF8());
        }
        return 0;
    }
    if (argc >= 2 && juce::String (argv[1]) == "filters") return filterModels (argc >= 3 && juce::String (argv[2]) == "calibrate");
    runAll();
    std::printf ("%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
