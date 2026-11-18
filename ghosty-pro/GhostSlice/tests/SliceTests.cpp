// GhostSliceTests: Ghost Slice GENERATE (drop a sound, press GENERATE). Run under xvfb-run -a.
//   GhostSliceTests                 every group
//   GhostSliceTests <group>         one group: dsp material types determinism midi rtsafety record state oldstate loudness speed clear parts ui
//   GhostSliceTests clearab [DIR]   CLEAR off / on A/B WAVs of every type (default /home/claude/work/gs/ab)
//   GhostSliceTests demogate        the demo export gate paths (DRAG WAV / DRAG MIDI / SAVE TAKE) + the drop-out
//   GhostSliceTests examples [DIR]  the example renders (WAV) + UI screenshots (default /home/claude/work/ghostslice-gen)
// Every test sound is synthesised here (tests/TestVoices.h): no recordings, no datasets.
#include "../SliceEditor.h"
#include "tests/DemoGateHarness.h"
#include "TestVoices.h"
#include "../Hold.h"
#include <juce_dsp/juce_dsp.h>
#include <cstdio>
#include <atomic>
#include <thread>
#include <new>

using namespace gslice;
using namespace gslice::gen;

// ---------------------------------------------------------------- allocation counter (the audio thread must never allocate)
static thread_local bool tlAudio = false;
static std::atomic<long> gAudioAllocs { 0 };
void* operator new (std::size_t n) { if (tlAudio) gAudioAllocs.fetch_add (1); if (void* p = std::malloc (n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[] (std::size_t n) { if (tlAudio) gAudioAllocs.fetch_add (1); if (void* p = std::malloc (n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

static int gPass = 0, gFail = 0;
static void check (bool ok, const juce::String& what) { (ok ? gPass : gFail)++; std::printf ("  [%s] %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8()); std::fflush (stdout); }
static void section (const char* s) { std::printf ("\n== %s\n", s); std::fflush (stdout); }
static void pump (int ms) { ghosty::demogate::pump (ms); }
static juce::String f2 (double v, int d = 2) { return juce::String (v, d); }

static juce::File tmpDir()
{
    static juce::File d = [] { auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("gslice-tests-" + juce::String (juce::Time::currentTimeMillis())); f.createDirectory(); return f; }();
    return d;
}

// ---------------------------------------------------------------- the test sounds
static std::shared_ptr<const SourceAudio> spoken() { static auto s = source::fromBuffer (slicetest::spokenSentence(), 48000, "spoken.wav"); return s; }
static std::shared_ptr<const SourceAudio> sung() { static auto s = source::fromBuffer (slicetest::sungPhrase(), 48000, "sung.wav"); return s; }
static std::shared_ptr<const Prepared> prepOf (std::shared_ptr<const SourceAudio> s)
{
    static std::map<const SourceAudio*, std::shared_ptr<const Prepared>> m;
    auto& p = m[s.get()]; if (! p) p = prepare (s, nullptr); return p;
}
// about 10 s of voice: sentence + phrase + sentence (a lower speaker)
static std::shared_ptr<const SourceAudio> tenSeconds()
{
    static auto s = []
    {
        auto a = slicetest::spokenSentence (48000, 7), b = slicetest::sungPhrase (48000, 11), c = slicetest::spokenSentence (48000, 23, 104.0);
        juce::AudioBuffer<float> o (1, a.getNumSamples() + b.getNumSamples() + c.getNumSamples());
        o.copyFrom (0, 0, a, 0, 0, a.getNumSamples()); o.copyFrom (0, a.getNumSamples(), b, 0, 0, b.getNumSamples()); o.copyFrom (0, a.getNumSamples() + b.getNumSamples(), c, 0, 0, c.getNumSamples());
        return source::fromBuffer (o, 48000, "ten.wav");
    }();
    return s;
}

// ---------------------------------------------------------------- measures
static juce::AudioBuffer<float> monoOf (const juce::AudioBuffer<float>& b) { juce::AudioBuffer<float> m (1, b.getNumSamples()); m.clear(); for (int c = 0; c < b.getNumChannels(); ++c) m.addFrom (0, 0, b, c, 0, b.getNumSamples(), 1.0f / (float) b.getNumChannels()); return m; }
// the share of the tonal energy (80 Hz .. 4 kHz, spectral peaks) on the key's scale pitch classes
static double chromaInKey (const juce::AudioBuffer<float>& b, const Key& k)
{
    auto m = monoOf (b); const int N = 8192; juce::dsp::FFT f (13); std::vector<float> buf ((size_t) N * 2);
    std::array<double, 12> pc {};
    for (int s = 0; s + N < m.getNumSamples(); s += N / 2)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < N; ++i) buf[(size_t) i] = m.getSample (0, s + i) * (float) (0.5 - 0.5 * std::cos (6.283185307 * i / N));
        f.performFrequencyOnlyForwardTransform (buf.data());
        for (int i = 2; i < N / 2 - 1; ++i)
        {
            const double hz = i * 48000.0 / N; if (hz < 80 || hz > 4000) continue;
            if (buf[(size_t) i] < buf[(size_t) i - 1] || buf[(size_t) i] < buf[(size_t) i + 1]) continue;   // peaks only
            const double midi = dsp::hzToMidi (hz); const int n = (int) std::lround (midi);
            pc[(size_t) ((n % 12) + 12) % 12] += (double) buf[(size_t) i] * buf[(size_t) i];
        }
    }
    double in = 0, all = 0; for (int i = 0; i < 12; ++i) { all += pc[(size_t) i]; if (inScale (i, k)) in += pc[(size_t) i]; }
    return all > 0 ? in / all : 0.0;
}
// the share of clearly pitched frames whose pitch sits within tol cents of a scale note
static double pitchInKey (const juce::AudioBuffer<float>& b, const Key& k, double tol, int* framesOut = nullptr)
{
    auto m = monoOf (b);
    auto t = analysis::trackPitch (m.getReadPointer (0), m.getNumSamples(), 48000.0);
    int in = 0, n = 0;
    for (int i = 0; i < t.frames(); ++i)
    {
        if (t.f0[(size_t) i] <= 0 || t.voicedProb[(size_t) i] < 0.6) continue;
        const double midi = dsp::hzToMidi (t.f0[(size_t) i]); const int nn = (int) std::lround (midi);
        ++n; if (std::abs (midi - nn) * 100.0 <= tol && inScale (nn, k)) ++in;
    }
    if (framesOut) *framesOut = n;
    return n > 0 ? (double) in / n : 0.0;
}
static double highShare (const juce::AudioBuffer<float>& b, double hz)
{
    auto m = monoOf (b); juce::dsp::FFT f (12); std::vector<float> buf (8192); double hi = 0, all = 0;
    for (int s = 0; s + 4096 < m.getNumSamples(); s += 2048)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < 4096; ++i) buf[(size_t) i] = m.getSample (0, s + i) * (float) (0.5 - 0.5 * std::cos (6.2831853 * i / 4096));
        f.performFrequencyOnlyForwardTransform (buf.data());
        for (int i = 1; i < 2048; ++i) { const double p = (double) buf[(size_t) i] * buf[(size_t) i]; all += p; if (i * 48000.0 / 4096 >= hz) hi += p; }
    }
    return all > 0 ? hi / all : 0;
}
// a zone as the keys play it: the start, then the loop twice (the stored buffer past the loop end is never played)
static juce::AudioBuffer<float> asPlayed (const Zone& z)
{
    if (z.loopEnd <= z.loopStart) return z.audio;
    const int head = z.loopEnd, loop = z.loopEnd - z.loopStart;
    juce::AudioBuffer<float> o (2, head + 2 * loop);
    for (int c = 0; c < 2; ++c) { const int sc = juce::jmin (c, z.audio.getNumChannels() - 1); o.copyFrom (c, 0, z.audio, sc, 0, head); o.copyFrom (c, head, z.audio, sc, z.loopStart, loop); o.copyFrom (c, head + loop, z.audio, sc, z.loopStart, loop); }   // (a mono zone: both sides)
    return o;
}
static double yinOf (const juce::AudioBuffer<float>& b, int from, int len)
{
    auto m = monoOf (b); double ap = 1;
    return hold::yin (m.getReadPointer (0) + from, len, 48000.0, 50.0, 1500.0, &ap);
}

// ---------------------------------------------------------------- dsp
static void testDsp()
{
    section ("dsp: loudness, true peak, band limits, PSOLA pitch, clicks");
    juce::AudioBuffer<float> s (2, 48000 * 4);
    for (int c = 0; c < 2; ++c) for (int i = 0; i < s.getNumSamples(); ++i) s.setSample (c, i, 0.1f * (float) std::sin (6.283185307 * 1000.0 * i / 48000.0));
    check (std::abs (dsp::lufs (s, 48000) - -20.0) < 0.15, "BS.1770: a 1 kHz sine at -20 dBFS on both sides reads -20 LUFS (" + f2 (dsp::lufs (s, 48000)) + ")");
    juce::AudioBuffer<float> q (2, 48000);
    for (int c = 0; c < 2; ++c) for (int i = 0; i < q.getNumSamples(); ++i) q.setSample (c, i, (float) std::sin (juce::MathConstants<double>::halfPi * i + juce::MathConstants<double>::pi / 4.0));
    check (dsp::peakOf (q) < 0.72f && std::abs (dsp::truePeakDb (q)) < 0.4, "true peak: an fs/4 sine sampled at 45 degrees peaks at -3 dBFS but reads about 0 dBTP (" + f2 (dsp::truePeakDb (q)) + ")");
    juce::AudioBuffer<float> loud; loud.makeCopyOf (q); loud.applyGain (3.0f);
    const double L = dsp::finish (loud, 48000, -14.0, -1.2);
    check (dsp::truePeakDb (loud) < -1.0 && std::abs (L - -14.0) < 1.5, "finish(): an overloaded sine ends below -1 dBTP (" + f2 (dsp::truePeakDb (loud)) + ") near -14 LUFS (" + f2 (L) + ")");
    // the band-limited copies: a 15 kHz tone read 2x faster would alias to 18 kHz; the copy for 2x keeps nothing above 11 kHz
    juce::AudioBuffer<float> hf (1, 48000); for (int i = 0; i < 48000; ++i) hf.setSample (0, i, 0.5f * (float) std::sin (6.283185307 * 15000.0 * i / 48000.0));
    dsp::BandBank bank; bank.build (hf, 48000);
    check (dsp::rmsOf (bank.forRatio (2.0), 4800, 43200) < 0.003 && dsp::rmsOf (bank.forRatio (1.0), 4800, 43200) > 0.3, "band-limited copies: 15 kHz is gone in the 2x copy (no aliasing on the way up), kept at 1x");
    // PSOLA: a synthetic vowel moved +5 and -7 semitones lands on its target pitch, formants kept
    auto src = sung(); auto P = prepOf (src);
    const auto& seg = *std::max_element (P->material.begin(), P->material.end(), [] (auto& a, auto& b) { return a.instrument < b.instrument; });
    const double c0 = P->wf (seg.v0) + 0.05 * 48000, c1 = P->wf (seg.v1) - 0.05 * 48000;
    auto v = dsp::analyseVoice (P->work->buffer(), 48000, P->wf (seg.v0), c0, c1, P->wf (seg.v1), [&] (double w) { return P->f0AtWork (w); });
    check (v.ok() && v.E.size() > 40, "the voice analysis finds the epochs of the best vowel (" + juce::String ((int) v.E.size()) + ", f0 " + f2 (v.f0, 1) + " Hz)");
    for (double semis : { 5.0, -7.0 })
    {
        dsp::ToneSpec ts; ts.seconds = 2.0; ts.semis = semis; ts.width = 0; ts.driftCents = 0; ts.natural = 0; ts.onset = false; ts.seed = 3;
        auto tone = dsp::psolaTone (v, ts);
        const double f = yinOf (tone, 24000, 24000), want = v.f0 * std::exp2 (semis / 12.0);
        check (std::abs (1200.0 * std::log2 (f / want)) < 20.0, "PSOLA tone " + f2 (semis, 0) + " st: " + f2 (f, 1) + " Hz (wanted " + f2 (want, 1) + ")");
        check (dsp::clicksIn (tone) == 0 && dsp::finiteAndClean (tone), "PSOLA tone " + f2 (semis, 0) + " st: no clicks, all finite");
    }
    juce::AudioBuffer<float> click (1, 4800); for (int i = 0; i < 4800; ++i) click.setSample (0, i, 0.3f * (float) std::sin (6.283185307 * 220.0 * i / 48000.0)); click.setSample (0, 2400, 0.9f);
    check (dsp::clicksIn (click) >= 1 && dsp::clicksIn (s) == 0, "the click finder: finds a spike, not a clean 1 kHz tone");
}

// ---------------------------------------------------------------- material
static void testMaterial()
{
    section ("material: pieces, ranking, breaths, key");
    auto P = prepOf (spoken());
    int voiced = 0, breaths = 0; for (auto& s : P->material) { if (s.instrument > 0.3) ++voiced; if (s.breath) ++breaths; }
    check ((int) P->material.size() >= 12, "the spoken sentence gives many pieces (" + juce::String ((int) P->material.size()) + ")");
    check (voiced >= 3, "several clean singable vowels rank for INSTRUMENT (" + juce::String (voiced) + ")");
    bool breathPitched = false; for (auto& s : P->material) if (s.breath && (s.instrument > 0 || s.pad > 0)) breathPitched = true;
    check (breaths >= 1 && ! breathPitched, "breaths are found (" + juce::String (breaths) + ") and never become pitched material");
    auto S = prepOf (sung());
    check (S->detected.tonic == 9 && S->detected.minor, "the sung A minor phrase is heard in A MIN (" + analysis::pitchClassName (S->detected.tonic) + (S->detected.minor ? " MIN" : " MAJ") + ")");
    const auto ch = progression ({ 9, true }, 64, true, 5, 9);
    bool smooth = true; for (size_t i = 1; i < ch.size(); ++i) { int mv = 0; for (size_t j = 0; j < std::min (ch[i].notes.size(), ch[i - 1].notes.size()); ++j) mv = std::max (mv, std::abs (ch[i].notes[j] - ch[i - 1].notes[j])); if (mv > 7) smooth = false; }
    bool allIn = true; for (auto& c : ch) for (int n : c.notes) if (! inScale (n, { 9, true })) allIn = false;
    check (ch.size() == 4 && smooth && allIn, "progressions: 4 chords, every note in the key, no voice leaps more than a fifth");
    auto ph = phrase ({ 9, true }, 69, 6, { true, false, true, false, false, true }, 4, 3, 4);
    bool rev = false, stut = false; for (auto& e : ph) { rev |= e.reverse; stut |= e.stutter > 0; }
    check (ph.size() >= 10 && ph.back().beat >= 12.0 && stut, "a CHOPS phrase: call / answer over 4 bars with a stutter (" + juce::String ((int) ph.size()) + " hits)");
}

// ---------------------------------------------------------------- every TYPE
static void testTypes()
{
    section ("types: each renders, clean, in key when MUSICAL");
    for (int v = 0; v < 2; ++v)
    {
        auto P = prepOf (v == 0 ? spoken() : sung());
        const char* vn = v == 0 ? "spoken" : "sung";
        for (int ty = 1; ty <= 4; ++ty)
        {
            Recipe r; r.type = (Type) ty; r.seed = 101 + (uint32_t) ty; r.detail = 7; r.musical = 1.0;
            if (ty == (int) Type::Instrument) r.knob = 1.0;   // (build 6: the instrument end; VOICE's word end is the recording itself, SliceTests6.h)
            // (build 6: a fixed melody style here - every style's own notes are checked in key by SliceTests6.h's brain test; this chroma
            //  measure also counts a bright timbre's upper harmonics, so a sparse style with three long notes reads low however in key)
            if (ty == (int) Type::Instrument || ty == (int) Type::Chops) r.style = Style::Hook;
            auto t = generate (*P, r);
            if (t == nullptr) { check (false, juce::String (vn) + " " + typeName ((Type) ty) + ": rendered"); continue; }
            const auto name = juce::String (vn) + " " + typeName (t->type) + (t->recipe.engine >= 6 ? juce::String (" (") + styleName (t->style) + ")" : juce::String());
            const Key k { t->keyTonic, t->keyMinor };
            check (dsp::rmsOf (t->preview) > 0.01, name + ": not silent");
            check (dsp::finiteAndClean (t->preview), name + ": no NaN / Inf / denormals");
            bool zonesClean = true; for (auto& z : t->zones) zonesClean = zonesClean && dsp::finiteAndClean (z.audio) && dsp::clicksIn (asPlayed (z)) == 0;
            check (dsp::clicksIn (t->preview) == 0 && zonesClean, name + ": no clicks (preview + every zone)");
            const double ck = chromaInKey (t->preview, k);
            check (ck >= 0.78, name + ": in key (" + f2 (ck * 100, 0) + " % of the tonal energy on the scale)");
            if (t->type == Type::Instrument || t->type == Type::Chops)
            {
                int fr = 0; const double pk = pitchInKey (t->preview, k, 35, &fr);
                check (fr > 50 && pk >= (t->type == Type::Chops ? 0.75 : 0.9), name + ": its notes are scale notes (" + f2 (pk * 100, 0) + " % of " + juce::String (fr) + " pitched frames)");
            }
            check (highShare (t->preview, 7000) < 0.03, name + ": no harsh top (" + f2 (highShare (t->preview, 7000) * 100, 2) + " % of the energy above 7 kHz)");
            check (t->midi.getNumTracks() == 1 && t->midi.getTrack (0)->getNumEvents() > 4, name + ": has its MIDI, one track (melody / chords / phrase; build 11: the chords go out through DRAG CHORDS)");
            if (t->type == Type::Chops) check (t->chops.size() >= 5, name + ": " + juce::String ((int) t->chops.size()) + " chops on the keys");
            else check (t->zones.size() >= 5, name + ": " + juce::String ((int) t->zones.size()) + " zones across the keyboard");
        }
        // NATURAL keeps the voice's own pitches: less snapping than MUSICAL
        Recipe r; r.type = Type::Instrument; r.seed = 5; r.detail = 5;
        r.musical = 1.0; auto m = generate (*P, r); r.musical = 0.0; auto n = generate (*P, r);
        check (m->digest != n->digest, juce::String (vn) + ": MUSICAL and NATURAL takes differ");
        for (auto c : { Character::Natural, Character::Chipmunk, Character::Deep })
        {
            Recipe rc; rc.type = Type::Instrument; rc.seed = 5; rc.detail = 5; rc.character = c;
            auto t = generate (*P, rc);
            check (t != nullptr && dsp::rmsOf (t->preview) > 0.01 && dsp::clicksIn (t->preview) == 0, juce::String (vn) + " INSTRUMENT " + characterName (c) + ": renders clean");
        }
    }
}

// ---------------------------------------------------------------- determinism
static void testDeterminism()
{
    section ("determinism: the same seed = the same take; new seeds = new takes");
    auto P = prepOf (spoken());
    for (int ty = 1; ty <= 4; ++ty)
    {
        Recipe r; r.type = (Type) ty; r.seed = 42; r.detail = 9;
        auto a = generate (*P, r), b = generate (*P, r);
        Recipe r2 = r; r2.seed = 43; r2.detail = 10; auto c = generate (*P, r2);
        Recipe r3 = r; r3.detail = 11; auto d = generate (*P, r3);
        check (a->digest == b->digest, juce::String (typeName ((Type) ty)) + ": seed 42 twice = bit for bit the same");
        check (a->digest != c->digest, juce::String (typeName ((Type) ty)) + ": another seed = another take");
        check (a->digest != d->digest, juce::String (typeName ((Type) ty)) + ": VARY (new details, same structure) = another take");
    }
    Recipe a; a.rotation = 0; Recipe b = a; b.rotation = 1; Recipe c = a; c.rotation = 2; Recipe d = a; d.rotation = 3;
    check (resolveType (a) != resolveType (b) && resolveType (b) != resolveType (c) && resolveType (c) != resolveType (d), "AUTO rotates: four presses, four different types");
}

// ---------------------------------------------------------------- MIDI playback
static juce::AudioBuffer<float> play (Engine& e, const std::vector<std::tuple<double, int, bool>>& ev, double seconds)
{
    const int n = (int) (seconds * 48000), blk = 256; juce::AudioBuffer<float> out (2, n + blk); out.clear(); size_t k = 0;
    for (int pos = 0; pos < n; pos += blk)
    {
        juce::MidiBuffer mb;
        while (k < ev.size() && std::get<0> (ev[k]) * 48000 < pos + blk) { const int at = juce::jmax (0, (int) (std::get<0> (ev[k]) * 48000) - pos); mb.addEvent (std::get<2> (ev[k]) ? juce::MidiMessage::noteOn (1, std::get<1> (ev[k]), 0.8f) : juce::MidiMessage::noteOff (1, std::get<1> (ev[k])), at); ++k; }
        e.process (out.getWritePointer (0) + pos, out.getWritePointer (1) + pos, blk, mb);
    }
    out.setSize (2, n, true, true, true); return out;
}
static void testMidi()
{
    section ("MIDI playback: INSTRUMENT / PAD / CHOPS on the keys");
    auto P = prepOf (sung());
    Recipe r; r.type = Type::Instrument; r.seed = 8; r.detail = 2;
    std::shared_ptr<const Take> t = generate (*P, r);
    Engine e; e.prepare (48000, 256); e.publish (t);
    auto held = play (e, { { 0.0, 64, true }, { 6.0, 64, false } }, 7.5);
    const double f = yinOf (held, 48000 * 3, 8192), want = dsp::midiToHz (64);
    check (std::abs (1200.0 * std::log2 (f / want)) < 30.0, "INSTRUMENT: E4 held plays E4 (" + f2 (f, 1) + " Hz)");
    check (dsp::rmsOf (held, 48000 * 4, 48000 * 6) > 0.5 * dsp::rmsOf (held, 48000, 48000 * 2), "INSTRUMENT: a held key keeps sounding (looped sustain, 6 s)");
    check (dsp::clicksIn (held) == 0, "INSTRUMENT: no clicks over 6 s of loop + the release");
    check (dsp::rmsOf (held, (int) (48000 * 7.2), 48000 * 7 + 14000) < 0.002, "INSTRUMENT: the release ends in silence");
    auto low = play (e, { { 0.0, 43, true }, { 2.0, 43, false } }, 2.5), high = play (e, { { 0.0, 84, true }, { 2.0, 84, false } }, 2.5);
    check (std::abs (1200 * std::log2 (yinOf (low, 48000, 8192) / dsp::midiToHz (43))) < 35 && std::abs (1200 * std::log2 (yinOf (high, 48000, 4096) / dsp::midiToHz (84))) < 35, "INSTRUMENT: one sound across the keyboard (G2 and C6 in tune)");
    r.type = Type::Pad; std::shared_ptr<const Take> pad = generate (*P, r); e.publish (pad);
    auto ch = play (e, { { 0.0, 57, true }, { 0.0, 60, true }, { 0.0, 64, true }, { 4.0, 57, false }, { 4.0, 60, false }, { 4.0, 64, false } }, 7.0);
    check (dsp::rmsOf (ch, 48000 * 2, 48000 * 4) > 0.02 && dsp::clicksIn (ch) == 0, "PAD: an A minor chord plays (slow swell, no clicks)");
    check (e.activeVoices() == 0, "PAD: every voice released");
    r.type = Type::Chops; std::shared_ptr<const Take> cp = generate (*P, r); e.publish (cp);
    auto c1 = play (e, { { 0.0, 36, true }, { 0.4, 36, false } }, 0.8), ph = play (e, { { 0.0, 60, true }, { 0.1, 60, false } }, 4.0);
    check (dsp::rmsOf (c1) > 0.005 && dsp::rmsOf (ph, 0, 48000 * 3) > 0.01, "CHOPS: MIDI 36 (FL C3) plays the first chop, 60 (FL C5) the whole phrase");
    // a new take while a key is held: the old take keeps playing until released, then it is freed
    e.publish (t); auto a = play (e, { { 0.0, 60, true } }, 0.5); e.publish (pad); auto b = play (e, { { 0.0, 60, false } }, 1.5);
    e.collectGarbage();
    check (dsp::clicksIn (a) == 0 && dsp::clicksIn (b) == 0, "a take switch under a held key: no click");
    check (t.use_count() <= 3, "the old take is let go once nothing plays it");
}

// ---------------------------------------------------------------- the audio thread
static void testRtSafety()
{
    section ("real-time safety: no allocation on the audio thread");
    SliceProcessor p; p.setPlayConfigDetails (0, 2, 48000, 512); p.prepareToPlay (48000, 512);
    p.loadSourceForTests (sung()); pump (20); p.type = Type::Pad; p.generate(); p.waitIdle (60000);
    check (p.currentTake() != nullptr, "a take is ready");
    juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer mb;
    gAudioAllocs.store (0);
    for (int i = 0; i < 400; ++i)
    {
        mb.clear(); if (i == 3) mb.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 10); if (i == 200) mb.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
        if (i == 50) p.keyboard.noteOn (1, 64, 0.8f); if (i == 120) p.keyboard.noteOff (1, 64, 0.0f);
        if (i == 250) p.playPreview (true);
        tlAudio = true; p.processBlock (buf, mb); tlAudio = false;
    }
    check (gAudioAllocs.load() == 0, "400 blocks with MIDI, the on-screen keys and the preview: " + juce::String (gAudioAllocs.load()) + " allocations");
    p.startRecording(); juce::AudioBuffer<float> in (2, 512);
    gAudioAllocs.store (0);
    for (int i = 0; i < 50; ++i) { tlAudio = true; p.processBlock (in, mb); tlAudio = false; }
    p.stopRecording();
    check (gAudioAllocs.load() == 0, "recording blocks: no allocation");
}

// ---------------------------------------------------------------- RECORD
static void testRecord()
{
    section ("RECORD: the track input becomes the sound");
    ghosty::License::testForce().store (0);   // (recording is not an export: it works in the demo)
    SliceProcessor p; p.enableAllBuses(); p.setPlayConfigDetails (2, 2, 48000, 512); p.prepareToPlay (48000, 512);
    p.input = SliceProcessor::Input::Track;
    check (p.getTotalNumInputChannels() == 2, "the RECORD input bus is there when the host turns it on");
    check (p.startRecording() && p.recording(), "RECORD starts");
    const auto& v = sung()->audio; juce::MidiBuffer mb;
    for (int at = 0; at + 512 <= v.getNumSamples(); at += 512)
    {
        juce::AudioBuffer<float> b (2, 512); for (int c = 0; c < 2; ++c) b.copyFrom (c, 0, v, 0, at, 512);
        p.processBlock (b, mb);
    }
    check (std::abs (p.recordedSeconds() - sung()->seconds()) < 0.05, "it records what comes in (" + f2 (p.recordedSeconds()) + " s)");
    p.stopRecording(); pump (300); p.waitIdle (60000); pump (50);
    const auto s = p.getSource();
    check (s != nullptr && s->file.getParentDirectory() == SliceProcessor::recordingsFolder() && std::abs (s->seconds() - 4.4) < 0.6, "STOP: the recording is kept in Documents/Ghosty/Ghost Slice/Recordings and becomes the sound (" + (s != nullptr ? f2 (s->seconds()) : juce::String ("none")) + " s)");
    p.generate(); p.waitIdle (60000);
    check (p.currentTake() != nullptr, "GENERATE works on the recording");
    ghosty::License::testForce().store (-1);
}

// ---------------------------------------------------------------- state
static juce::File wavOf (std::shared_ptr<const SourceAudio> s, const juce::String& name)
{
    auto f = tmpDir().getChildFile (name); juce::String e; exporter::writeWav (s->audio, s->sampleRate, f, 24, e); return f;
}
// a reopened session: the sound loads on its own thread, then the current take renders: wait for both (up to 2 minutes)
static void waitReopened (SliceProcessor& p)
{
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    while (juce::Time::getMillisecondCounterHiRes() - t0 < 120000.0)
    {
        pump (20); p.waitIdle (1000);
        if (! p.isLoading() && ! p.busy() && p.getSource() != nullptr && (p.takes().empty() || p.currentTake() != nullptr)) break;
    }
    pump (20);
}
static void testState()
{
    section ("state: the session keeps the sound and every take");
    ghosty::License::testForce().store (1);
    auto file = wavOf (sung(), "sung-state.wav");
    juce::MemoryBlock mb; juce::String d1, d2, n1;
    {
        SliceProcessor a; a.prepareToPlay (48000, 512);
        juce::String err; check (a.loadAudio (file, err), "load a WAV");
        a.type = Type::Instrument; a.generate(); a.waitIdle (60000);
        a.type = Type::Chops; a.generate(); a.waitIdle (60000);
        a.vary(); a.waitIdle (60000);
        a.toggleFavourite (1); a.setTone (-0.4f); a.setSpace (0.7f); a.uiTheme.store (3);
        check (a.takes().size() == 3 && a.takes()[2].recipe.variation == 1, "3 takes (the third a VARY of the second)");
        d1 = a.currentTake()->digest; n1 = a.currentTake()->recipe.name;
        if (juce::SystemStats::getEnvironmentVariable ("GS_DEBUG", "").isNotEmpty())
        {
            std::printf ("    (recipe: %s / style %s)\n", juce::JSON::toString (a.takes()[2].recipe.toVar(), true).toRawUTF8(), styleName (a.currentTake()->style));
            auto P = a.getPrepared(); auto again = P != nullptr ? generate (*P, a.takes()[2].recipe) : nullptr;
            std::printf ("    (rendered again in the same session: %s)\n", again != nullptr && again->digest == d1 ? "same" : "DIFFERENT");
        }
        a.getStateInformation (mb);
    }
    {
        SliceProcessor b; b.prepareToPlay (48000, 512);
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        waitReopened (b);
        check (b.getSource() != nullptr && b.takes().size() == 3, "reopened: the sound and the 3 takes are back");
        if (b.currentTake() != nullptr && b.currentTake()->digest != d1 && juce::SystemStats::getEnvironmentVariable ("GS_DEBUG", "").isNotEmpty())
            std::printf ("    (reopened recipe: %s / style %s)\n", juce::JSON::toString (b.takes()[2].recipe.toVar(), true).toRawUTF8(), styleName (b.currentTake()->style));
        check (b.currentTake() != nullptr && b.currentTake()->digest == d1 && b.currentTake()->recipe.name == n1, "the current take re-renders bit for bit (" + n1 + ")");
        check (b.takes()[1].recipe.favourite && std::abs (b.tone() + 0.4f) < 1e-4f && std::abs (b.space() - 0.7f) < 1e-4f && b.uiTheme.load() == 3, "the star, TONE, SPACE and the look come back");
        b.selectTake (0); b.waitIdle (60000);
        check (b.currentTake() != nullptr && b.currentTake()->type == Type::Instrument, "an older take is remade when picked");
    }
    {   // a session saved before CLEAR existed: it reopens with CLEAR off, every take too (they sound as they did)
        auto v = juce::JSON::parse (mb.toString());
        v.getDynamicObject()->removeProperty ("clear");
        if (auto* arr = v["takes"].getArray()) for (auto& x : *arr) x.getDynamicObject()->removeProperty ("clear");
        const auto js = juce::JSON::toString (v);
        SliceProcessor o; o.prepareToPlay (48000, 512);
        o.setStateInformation (js.toRawUTF8(), (int) js.getNumBytesAsUTF8()); waitReopened (o);
        bool allOff = ! o.clear; for (auto& sl : o.takes()) allOff = allOff && ! sl.recipe.clear;
        check (allOff && o.currentTake() != nullptr && ! o.currentTake()->clear(), "an older session (no CLEAR in it) reopens with CLEAR off, its takes too ("
               + juce::String (allOff ? "all off" : "some on") + ", " + (o.currentTake() == nullptr ? "no take" : o.currentTake()->clear() ? "take CLEAR" : "take not CLEAR") + ")");
    }
    {   // locked (demo): a reopened session starts empty; UNLOCK brings it back
        ghosty::License::testForce().store (0);
        SliceProcessor c; c.setStateInformation (mb.getData(), (int) mb.getSize()); pump (50);
        check (c.getSource() == nullptr && c.takes().empty(), "demo: a reopened session starts empty");
        ghosty::License::testForce().store (1); c.unlockedNow(); pump (100); c.waitIdle (60000);
        check (c.takes().size() == 3, "after UNLOCK it comes back");
    }
    ghosty::License::testForce().store (-1);
}

static void testOldState()
{
    section ("old sessions: an editor-era Ghost Slice session loads its audio into the new screen");
    ghosty::License::testForce().store (1);
    auto file = wavOf (spoken(), "old-session.wav");
    auto* src = new juce::DynamicObject(); src->setProperty ("path", file.getFullPathName()); src->setProperty ("sha256", "whatever"); src->setProperty ("fileName", file.getFileName());
    auto* proj = new juce::DynamicObject(); proj->setProperty ("format", "ghostslice"); proj->setProperty ("source", juce::var (src)); proj->setProperty ("regions", juce::Array<juce::var>());
    auto* o = new juce::DynamicObject(); o->setProperty ("ghostslice", 1); o->setProperty ("project", juce::var (proj)); o->setProperty ("theme", 2);
    const auto s = juce::JSON::toString (juce::var (o));
    SliceProcessor p; p.setStateInformation (s.toRawUTF8(), (int) s.getNumBytesAsUTF8());
    waitReopened (p);
    check (p.getSource() != nullptr && p.takes().empty() && p.uiTheme.load() == 2, "the old session's sound is loaded (no takes yet), its look kept");
    check (! p.clear, "an editor-era session opens with CLEAR off");
    p.generate(); p.waitIdle (60000);
    check (p.currentTake() != nullptr, "and GENERATE works on it");
    ghosty::License::testForce().store (-1);
}

// ---------------------------------------------------------------- loudness
static void testLoudness()
{
    section ("loudness: exports at -14 LUFS, true peak below -1 dBTP");
    for (auto s : { spoken(), sung() })
    {
        auto P = prepOf (s);
        for (int ty = 1; ty <= 4; ++ty)
            for (float space : { 0.0f, 0.4f, 1.0f })
            {
                Recipe r; r.type = (Type) ty; r.seed = 77; r.detail = 3;
                auto t = generate (*P, r);
                auto w = renderForExport (*t, space > 0.5f ? 0.8f : -0.3f, space, 0.55f, 0.35f);
                const double L = dsp::lufs (w, 48000), tp = dsp::truePeakDb (w);
                check (L > -14.6 && L < -13.4 && tp < -1.0 && dsp::finiteAndClean (w), juce::String (s == spoken() ? "spoken " : "sung ") + typeName (t->type) + (t->recipe.engine >= 6 ? juce::String (" (") + styleName (t->style) + ")" : juce::String()) + " SPACE " + f2 (space, 1) + ": " + f2 (L) + " LUFS, " + f2 (tp) + " dBTP");
            }
    }
}

// ---------------------------------------------------------------- speed
static void testSpeed()
{
    section ("speed: a take from a 10 s vocal in a few seconds");
    auto src = tenSeconds();
    auto t0 = juce::Time::getMillisecondCounterHiRes();
    auto P = prepare (src, nullptr);
    const double prepMs = juce::Time::getMillisecondCounterHiRes() - t0;
    check (P != nullptr && prepMs < 6000, "listening to " + f2 (src->seconds(), 1) + " s: " + f2 (prepMs, 0) + " ms (once per sound)");
    for (int ty = 1; ty <= 4; ++ty)
    {
        Recipe r; r.type = (Type) ty; r.seed = 9; r.detail = 9;
        t0 = juce::Time::getMillisecondCounterHiRes(); auto t = generate (*P, r); const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        // (the budget: 8 s on a 4-core machine; build 6's renders run on every core, so a 2-core test box gets twice that)
        const double budget = 8000.0 * juce::jmax (1.0, 4.0 / juce::jmax (1, (int) std::thread::hardware_concurrency()));
        check (t != nullptr && ms < budget && t->bytes() < 64e6, juce::String (typeName ((Type) ty)) + ": " + f2 (ms, 0) + " ms (budget " + f2 (budget, 0) + "), " + f2 (t->bytes() / 1e6, 1) + " MB");
    }
}

// ---------------------------------------------------------------- UI
static juce::Image shot (juce::Component& c) { return c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f); }
static void savePng (const juce::Image& im, const juce::File& f) { f.getParentDirectory().createDirectory(); f.deleteFile(); juce::FileOutputStream os (f); juce::PNGImageFormat png; png.writeImageToStream (im, os); }
static void testUi (const juce::File& dir)
{
    section ("UI: the one-screen design (the reference picture) in the four looks, every control real");
    ghosty::License::testForce().store (1);
    SliceProcessor p; p.prepareToPlay (48000, 512);
    std::unique_ptr<SliceEditor> ed (static_cast<SliceEditor*> (p.createEditor())); ed->setVisible (true); pump (50);
    const char* names[] { "black", "purple", "light", "ghost" };
    check (p.uiTheme.load() == 1 && ed->look() == SliceEditor::kLookPurple, "a new Ghost Slice opens in the PURPLE look");
    check (std::abs ((double) ed->getWidth() / ed->getHeight() - 1477.0 / 1065.0) < 0.01 && ed->getConstrainer()->getFixedAspectRatio() > 1.38,
           "the window keeps the picture's proportions (" + juce::String (ed->getWidth()) + " x " + juce::String (ed->getHeight()) + ")");
    ed->setSize (1477, 1065); pump (30);
    check (std::abs (ed->viewScale() - 1.0f) < 1e-3f, "at 1477 x 1065 the design is drawn 1 : 1");
    for (int l = 0; l < 4; ++l) { ed->setLook (l); pump (20); savePng (shot (*ed), dir.getChildFile (juce::String ("ui-empty-") + names[l] + ".png")); }
    ed->setLook (1); pump (20);
    check (ed->stage.isInterestedInFileDrag ({ "/x/voice.wav" }) && ! ed->stage.isInterestedInFileDrag ({ "/x/notes.txt" }), "the drop zone takes audio files, not other files");
    // the look chips switch the look
    { juce::MouseEvent* none = nullptr; juce::ignoreUnused (none);
      ed->looks.onChange (3); check (ed->look() == 3 && p.uiTheme.load() == 3, "the GHOST chip switches to GHOST"); ed->looks.onChange (1); pump (10); }
    // the picture's sound: "sung.wav", 16.2 s (three sung phrases and a sentence), the takes in A MIN, MUSICAL at 60 %
    {
        auto a = slicetest::sungPhrase (48000, 11), b = slicetest::spokenSentence (48000, 7), c = slicetest::sungPhrase (48000, 29), d = slicetest::sungPhrase (48000, 5);
        const int n = (int) (16.2 * 48000.0);
        juce::AudioBuffer<float> o (1, n); o.clear(); int at = 0;
        for (auto* x : { &a, &b, &c, &d }) { const int m = juce::jmin (x->getNumSamples(), n - at); if (m > 0) o.copyFrom (0, at, *x, 0, 0, m); at += m; }
        p.loadSourceForTests (source::fromBuffer (o, 48000, "sung.wav")); pump (30); p.waitIdle (60000);
        p.keyTonic = 9; p.keyMinor = true; p.musical = 0.6;
    }
    // four takes like the picture: PAD, INSTRUMENT, CHOPS, TEXTURE (1 and 3 starred), take 1 picked
    for (auto t : { Type::Pad, Type::Instrument, Type::Chops, Type::Texture }) { p.type = t; p.generate(); p.waitIdle (60000); }
    p.toggleFavourite (0); p.toggleFavourite (2);
    p.type = Type::Auto; p.selectTake (0); p.waitIdle (60000); p.playPreview (false); pump (60);
    ed->strip.showCurrent(); ed->refresh(); pump (20);
    { juce::AudioBuffer<float> hb (2, 512); juce::MidiBuffer hm; hm.addEvent (juce::MidiMessage::noteOn (1, 57, 0.85f), 0); p.processBlock (hb, hm); pump (60); }   // (a held note: VELOCITY)
    savePng (shot (*ed), dir.getChildFile ("ui-loaded-purple.png"));
    { juce::AudioBuffer<float> hb (2, 512); juce::MidiBuffer hm; hm.addEvent (juce::MidiMessage::noteOff (1, 57), 0); p.processBlock (hb, hm); }
    for (int l = 0; l < 4; ++l) { ed->setLook (l); pump (20); savePng (shot (*ed), dir.getChildFile (juce::String ("ui-takes-") + names[l] + ".png")); }
    ed->setLook (1); pump (20);
    check (p.takes().size() == 4 && ed->strip.hits.size() == 4, "the TAKES strip shows the four takes");
    // every control has a tooltip; nothing says "brain"
    int missing = 0; juce::StringArray bad;
    std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
    {
        for (auto* ch : c.getChildren())
        {
            if (auto* t = dynamic_cast<juce::TooltipClient*> (ch)) { const auto tip = t->getTooltip(); if (tip.isEmpty() && ch->isVisible() && dynamic_cast<juce::MidiKeyboardComponent*> (ch) == nullptr) { ++missing; bad.add (ch->getName()); } if (tip.containsIgnoreCase ("brain")) bad.add ("brain"); }
            walk (*ch);
        }
    };
    walk (*ed);
    check (missing == 0 && ! bad.contains ("brain"), "every visible control has a tooltip (" + juce::String (missing) + " missing" + (bad.isEmpty() ? juce::String() : ": " + bad.joinIntoString (", ")) + ") and plain words");
    // text contrast in every look (the captions on the panels)
    for (int l = 0; l < 4; ++l)
    {
        ed->setLook (l);
        const auto& P = ed->P;
        check (ghosty::themes::contrast (P.ink, P.panel) >= 4.5 && ghosty::themes::contrast (P.dim, P.panel) >= 4.0, juce::String (names[l]) + ": text reads on the panels (" + f2 (ghosty::themes::contrast (P.dim, P.panel), 1) + ":1 for captions)");
    }
    ed->setLook (1); pump (10);
    // the waveform tools: zoom in / out / fit
    ed->stage.zoomInBtn.triggerClick(); pump (10);
    const double w1 = ed->stage.v1 - ed->stage.v0;
    ed->stage.zoomOutBtn.triggerClick(); ed->stage.zoomOutBtn.triggerClick(); pump (10);
    const double w2 = ed->stage.v1 - ed->stage.v0;
    ed->stage.zoomInBtn.triggerClick(); ed->stage.zoomInBtn.triggerClick(); pump (10); ed->stage.fitBtn.triggerClick(); pump (10);
    check (std::abs (w1 - 0.5) < 1e-6 && std::abs (w2 - 1.0) < 1e-6 && ed->stage.v0 == 0.0 && ed->stage.v1 == 1.0, "zoom in halves the view, zoom out widens it, fit shows the whole sound");
    // the type / character tiles, the musical slider, the knobs set the processor
    ed->typeTiles.onChange (3); ed->charTiles.onChange (2); ed->musicalSl.setValue (0.25); ed->spaceKnob.setValue (0.8); ed->toneKnob.setValue (-0.5); pump (30);
    check (p.type == Type::Chops && p.character == Character::Deep && std::abs (p.musical - 0.25) < 1e-6 && std::abs (p.space() - 0.8f) < 1e-6f && std::abs (p.tone() + 0.5f) < 1e-6f,
           "TAKE TYPE, CHARACTER, NATURAL <-> MUSICAL, TONE and SPACE reach the processor");
    p.type = Type::Auto; p.character = Character::Natural; p.musical = 0.7; p.setSpace (0.4f); p.setTone (0.0f); ed->refresh();
    // CLEAR: on for a new Ghost Slice; the pill toggles it (a toast says to press GENERATE); saved with the session and each take
    {
        check (p.clear && ed->clearPill.on && ed->clearPill.getTooltip().startsWith ("CLEAR: keeps your sound as clean and true as possible"), "CLEAR is on for a new Ghost Slice (the pill is lit, its tooltip says what it does)");
        check (p.currentTake() != nullptr && p.currentTake()->clear(), "the takes made with it on are CLEAR takes");
        const auto before = p.currentTake();
        ed->clearPill.triggerClick(); pump (10);
        check (! p.clear && ! ed->clearPill.on && ed->toastText == "CLEAR off: press GENERATE for a dreamier take" && p.currentTake() == before, "the pill turns CLEAR off (toast), the take stays as it is (no re-render)");
        ed->clearPill.triggerClick(); pump (10);
        check (p.clear && ed->clearPill.on && ed->toastText == "CLEAR on: press GENERATE for a clearer take", "and back on: \"CLEAR on: press GENERATE for a clearer take\"");
        savePng (shot (*ed), dir.getChildFile ("ui-clear-toast-purple.png"));
        juce::MemoryBlock mb; p.getStateInformation (mb); const auto v = juce::JSON::parse (mb.toString());
        check ((bool) v["clear"] && (bool) v["takes"][0]["clear"], "CLEAR is saved with the session and with each take");
        ed->toastUntil = 1.0; pump (60);
    }
    // SCALE: < > cycle the key, the lock keeps it when a new sound loads
    { const auto k = p.detectedKey(); ed->scaleBox.nextBtn.triggerClick(); pump (10);
      check (p.keyTonic == (k.tonic + 1) % 12 && p.keyMinor == k.minor, "SCALE > steps the key up a semitone (" + ed->keyText() + ")");
      ed->scaleBox.prevBtn.triggerClick(); pump (10); check (p.keyTonic == k.tonic, "SCALE < steps it back"); }
    // TRIM: the part of the sound GENERATE uses (stored with the session); the takes use only that part
    {
        const double secs = p.getSource()->seconds();
        p.setTrim (0.5, 1.0); p.waitIdle (60000);
        p.type = Type::Instrument; p.generate(); p.waitIdle (60000); p.playPreview (false);
        auto t = p.currentTake(); bool inside = t != nullptr && ! t->used.empty();
        if (t != nullptr) for (auto& u : t->used) inside = inside && u.first >= 0.5 * secs - 1e-3 && u.second <= secs + 1e-3;
        check (inside, "TRIM 50-100 %: the new take used only that part (" + (t != nullptr && ! t->used.empty() ? f2 (t->used.front().first, 2) + " s .." : juce::String ("none")) + ")");
        juce::MemoryBlock mb; p.getStateInformation (mb);
        const auto v = juce::JSON::parse (mb.toString());
        check (std::abs ((double) v["trim0"] - 0.5) < 1e-9 && (double) v["takes"][4]["trim0"] == 0.5, "TRIM is saved with the session (and with the take)");
        ed->refresh(); pump (20);
        ed->stage.trimBtn.triggerClick(); pump (10);
        savePng (shot (*ed), dir.getChildFile ("ui-trim-purple.png"));
        ed->stage.trimBtn.triggerClick();
        p.setTrim (0.0, 1.0); p.waitIdle (60000); p.deleteTake (4); p.selectTake (0); p.waitIdle (60000); p.playPreview (false); p.type = Type::Auto; ed->refresh(); pump (20);
        check (! p.isTrimmed(), "the whole sound again");
    }
    // the sound's own play button: plays (dry) and stops
    {
        ed->stage.playSrcBtn.triggerClick(); pump (30); juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer mb; float mx = 0;
        for (int i = 0; i < 60; ++i) { p.processBlock (b, mb); mx = std::max (mx, b.getMagnitude (0, 512)); }
        const bool on = p.sourcePlaying() && mx > 1e-3f && p.sourcePosition() > 0.5;
        ed->stage.playSrcBtn.triggerClick(); pump (30);
        check (on && ! p.sourcePlaying(), "the play button under the waveform plays the sound itself, and stops it");
    }
    // the keys: the on-screen keyboard reaches the engine; VELOCITY follows the note
    p.keyboard.noteOn (1, 60, 0.8f); juce::AudioBuffer<float> b (2, 4096); juce::MidiBuffer mb; for (int i = 0; i < 8; ++i) p.processBlock (b, mb);
    check (p.activeVoices() > 0, "the on-screen keys play the take");
    pump (80);
    check (std::abs (p.lastVelocity() - 0.8f) < 0.01f && ed->velocity.value > 0.3f, "VELOCITY shows the note's velocity (" + f2 (ed->velocity.value, 2) + ")");
    p.keyboard.noteOff (1, 60, 0.0f);
    check (ed->keys.lit.count() > 0, "the keyboard lights the take's notes (" + juce::String ((int) ed->keys.lit.count()) + " keys)");
    // GENERATING (the progress fills GENERATE) and RECORDING
    p.generate(); pump (120);
    savePng (shot (*ed), dir.getChildFile ("ui-generating-purple.png"));
    check (p.busy() || p.takes().size() == 5, "GENERATE shows its progress while it renders");
    p.waitIdle (60000); p.deleteTake ((int) p.takes().size() - 1); p.selectTake (0); p.waitIdle (60000); p.playPreview (false); ed->refresh(); pump (20);
    p.input = SliceProcessor::Input::Track;
    p.startRecording(); ed->refresh(); pump (200);
    savePng (shot (*ed), dir.getChildFile ("ui-recording-purple.png"));
    check (p.recording() && ed->stage.recordBtn.getButtonText() == "STOP", "RECORD becomes STOP while recording");
    p.stopRecording(); pump (100); p.waitIdle (60000); p.input = SliceProcessor::Input::Auto;
    // a smaller window draws the same picture, scaled
    ed->setSize (1108, 799); pump (30);
    savePng (shot (*ed), dir.getChildFile ("ui-small-purple.png"));
    check (std::abs (ed->viewScale() - 0.75f) < 0.01f, "the default size draws it at 75 %");
    ed.reset(); pump (20);
    ghosty::License::testForce().store (-1);
}

// ---------------------------------------------------------------- CLEAR: the same seed and sound, CLEAR off vs on, measured
namespace clearm
{
using Buf = juce::AudioBuffer<float>;
// a key held through the engine and the FX the processor gives this take (SPACE 0: no reverb in the measures)
static Buf held (std::shared_ptr<const Take> t, int note, double secs, double offAt = -1)
{
    Engine e; e.prepare (48000, 256); e.publish (t);
    if (offAt < 0) offAt = secs;
    auto o = play (e, { { 0.0, note, true }, { offAt, note, false } }, secs);
    Fx fx; fx.space.store (0.0f); fx.tone.store (0.0f); fx.width.store (0.55f); fx.warmth.store (0.35f); fx.ensemble.store (t->ensemble); fx.clear.store (t->clear());
    fx.prepare (48000, 512);
    for (int i = 0; i < o.getNumSamples(); i += 512) fx.process (o.getWritePointer (0) + i, o.getWritePointer (1) + i, juce::jmin (512, o.getNumSamples() - i));
    return o;
}
// the pitch every 10 ms (YIN, 2048), Hz (0 = none)
static std::vector<double> pitchTrack (const Buf& b, int from, int to)
{
    auto m = monoOf (b); std::vector<double> out;
    for (int s = from; s + 2048 <= to; s += 480) { double ap = 1; const double f = hold::yin (m.getReadPointer (0) + s, 2048, 48000.0, 60.0, 1500.0, &ap); out.push_back (ap < 0.35 ? f : 0.0); }
    return out;
}
static double median (std::vector<double> v) { if (v.empty()) return 0; std::sort (v.begin(), v.end()); return v[v.size() / 2]; }
// pitch steadiness: the RMS of the cents away from the median pitch
static double centsSpread (const std::vector<double>& tr)
{
    std::vector<double> v; for (double f : tr) if (f > 0) v.push_back (f);
    const double med = median (v); if (med <= 0) return 999;
    double s = 0; for (double f : v) { double c = 1200.0 * std::log2 (f / med); c -= 1200.0 * std::round (c / 1200.0); s += c * c; }   // (octave slips of the tracker folded)
    return std::sqrt (s / (double) v.size());
}
// harmonic-to-noise ratio (dB): the normalised autocorrelation peak at the period, 2048-sample frames, the median
static double hnr (const Buf& b, int from, int to)
{
    auto m = monoOf (b); const float* x = m.getReadPointer (0); std::vector<double> r;
    const int W = 2048;
    for (int s = from; s + W + 1000 <= to; s += 1024)
    {
        double best = 0;
        for (int lag = 40; lag <= 800; ++lag)
        {
            double xy = 0, xx = 0, yy = 0;
            for (int i = 0; i < W; ++i) { xy += (double) x[s + i] * x[s + i + lag]; xx += (double) x[s + i] * x[s + i]; yy += (double) x[s + i + lag] * x[s + i + lag]; }
            if (xx > 0 && yy > 0) best = std::max (best, xy / std::sqrt (xx * yy));
        }
        best = juce::jlimit (1e-4, 0.99999, best);
        r.push_back (10.0 * std::log10 (best / (1.0 - best)));
    }
    return median (r);
}
// L / R correlation (Pearson)
static double lrCorr (const Buf& b, int from, int to)
{
    double xy = 0, xx = 0, yy = 0;
    for (int i = from; i < to; ++i) { const double l = b.getSample (0, i), r = b.getSample (1, i); xy += l * r; xx += l * l; yy += r * r; }
    return xx > 0 && yy > 0 ? xy / std::sqrt (xx * yy) : 1.0;
}
// the spectral envelope (dB) on a log-frequency grid 150 Hz .. 6 kHz (200 points): averaged power spectra, cepstrally smoothed
static std::vector<double> envelope (const Buf& b, int from, int to)
{
    auto m = monoOf (b); const int N = 4096; juce::dsp::FFT f (12); std::vector<float> buf ((size_t) N * 2); std::vector<double> P ((size_t) N / 2 + 1, 0.0); int frames = 0;
    for (int s = from; s + N <= to; s += N / 4, ++frames)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < N; ++i) buf[(size_t) i] = m.getSample (0, s + i) * (float) (0.5 - 0.5 * std::cos (6.283185307 * i / N));
        f.performFrequencyOnlyForwardTransform (buf.data());
        for (int k = 0; k <= N / 2; ++k) P[(size_t) k] += (double) buf[(size_t) k] * buf[(size_t) k];
    }
    // log spectrum -> real cepstrum (a plain DFT of the even log spectrum) -> keep 30 coefficients -> back
    const int K = N / 2 + 1; std::vector<double> L ((size_t) K); for (int k = 0; k < K; ++k) L[(size_t) k] = std::log (P[(size_t) k] / juce::jmax (1, frames) + 1e-12);
    const int Q = 30; std::vector<double> c ((size_t) Q, 0.0);
    for (int q = 0; q < Q; ++q) { double s = 0; for (int k = 0; k < K; ++k) s += L[(size_t) k] * std::cos (juce::MathConstants<double>::pi * q * k / (K - 1)) * (k == 0 || k == K - 1 ? 0.5 : 1.0); c[(size_t) q] = s / (K - 1); }
    std::vector<double> env (200);
    for (int j = 0; j < 200; ++j)
    {
        const double hz = 150.0 * std::pow (6000.0 / 150.0, j / 199.0), k = hz * N / 48000.0;
        double s = c[0]; for (int q = 1; q < Q; ++q) s += 2.0 * c[(size_t) q] * std::cos (juce::MathConstants<double>::pi * q * k / (K - 1));
        env[(size_t) j] = 10.0 / std::log (10.0) * s;
    }
    return env;
}
static double pearson (const std::vector<double>& a, const std::vector<double>& b, int shift = 0)
{
    double ma = 0, mb = 0; int n = 0;
    for (int i = 0; i < (int) a.size(); ++i) { const int j = i + shift; if (j < 0 || j >= (int) b.size()) continue; ma += a[(size_t) i]; mb += b[(size_t) j]; ++n; }
    if (n < 10) return 0; ma /= n; mb /= n; double xy = 0, xx = 0, yy = 0;
    for (int i = 0; i < (int) a.size(); ++i) { const int j = i + shift; if (j < 0 || j >= (int) b.size()) continue; xy += (a[(size_t) i] - ma) * (b[(size_t) j] - mb); xx += (a[(size_t) i] - ma) * (a[(size_t) i] - ma); yy += (b[(size_t) j] - mb) * (b[(size_t) j] - mb); }
    return xx > 0 && yy > 0 ? xy / std::sqrt (xx * yy) : 0;
}
// how far (cents) the formants of `b` sit from those of `ref`: the log-frequency shift that best lines the envelopes up (+-6 semitones)
static double formantShiftCents (const std::vector<double>& ref, const std::vector<double>& b)
{
    const double centsPerPoint = 1200.0 * std::log2 (6000.0 / 150.0) / 199.0;
    int best = 0; double bc = -2;
    for (int s = -(int) (600 / centsPerPoint); s <= (int) (600 / centsPerPoint); ++s) { const double c = pearson (ref, b, s); if (c > bc) { bc = c; best = s; } }
    return best * centsPerPoint;
}
// spectral flux of a held sound: the mean frame-to-frame change of the (normalised) magnitude spectrum, 100 Hz .. 8 kHz (smearing /
// churning grains move it; a clean sustain barely moves)
static double flux (const Buf& b, int from, int to)
{
    auto m = monoOf (b); const int N = 2048; juce::dsp::FFT f (11); std::vector<float> buf ((size_t) N * 2); std::vector<double> prev; double sum = 0; int cnt = 0;
    const int k0 = 100 * N / 48000, k1 = 8000 * N / 48000;
    for (int s = from; s + N <= to; s += 512)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < N; ++i) buf[(size_t) i] = m.getSample (0, s + i) * (float) (0.5 - 0.5 * std::cos (6.283185307 * i / N));
        f.performFrequencyOnlyForwardTransform (buf.data());
        std::vector<double> mag; double tot = 0; for (int k = k0; k < k1; ++k) { mag.push_back (buf[(size_t) k]); tot += buf[(size_t) k]; }
        for (auto& v : mag) v /= juce::jmax (1e-12, tot);
        if (! prev.empty()) { double d = 0; for (size_t k = 0; k < mag.size(); ++k) d += std::abs (mag[k] - prev[k]); sum += d; ++cnt; }
        prev = mag;
    }
    return cnt > 0 ? sum / cnt : 0;
}
// the spectral centroid (Hz, 100 Hz .. 10 kHz) of a span: with the formants in place it barely moves when the pitch moves
static double centroid (const Buf& b, int from, int to)
{
    auto m = monoOf (b); const int N = 4096; juce::dsp::FFT f (12); std::vector<float> buf ((size_t) N * 2); double num = 0, den = 0;
    for (int s = from; s + N <= to; s += N / 4)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < N; ++i) buf[(size_t) i] = m.getSample (0, s + i) * (float) (0.5 - 0.5 * std::cos (6.283185307 * i / N));
        f.performFrequencyOnlyForwardTransform (buf.data());
        for (int k = 100 * N / 48000; k < 10000 * N / 48000; ++k) { const double p = (double) buf[(size_t) k] * buf[(size_t) k]; num += p * k * 48000.0 / N; den += p; }
    }
    return den > 0 ? num / den : 1.0;
}
static double crestDb (const Buf& b) { return dsp::gainToDb (dsp::peakOf (b)) - dsp::gainToDb (dsp::rmsOf (b)); }
static bool clipFree (const Buf& b) { return dsp::peakOf (b) < 0.999f && dsp::finiteAndClean (b); }
// an RMS envelope (2 ms frames) of a buffer
static std::vector<double> rmsEnv (const Buf& b, int frames)
{
    auto m = monoOf (b); const int n = m.getNumSamples(); std::vector<double> e ((size_t) frames, 0.0);
    for (int k = 0; k < frames; ++k) { const int a = (int) ((int64) n * k / frames), z = juce::jmax (a + 1, (int) ((int64) n * (k + 1) / frames)); double s = 0; for (int i = a; i < z && i < n; ++i) s += (double) m.getSample (0, i) * m.getSample (0, i); e[(size_t) k] = std::sqrt (s / (z - a)); }
    return e;
}
}

static void testClear()
{
    using namespace clearm;
    section ("CLEAR: the same seed and the same sung source, CLEAR off vs on, measured");
    auto src = sung(); auto P = prepOf (src);
    // (build 4's CLEAR contract, on the engine it was made for: build 5's, which older projects still use. Build 6's own measures
    //  are in SliceTests6.h)
    auto make = [&] (Type ty, bool cl, double musical = 0.7) { Recipe r; r.type = ty; r.seed = 2024; r.detail = 5; r.musical = musical; r.clear = cl; r.engine = 5;
                                                               const auto t0 = juce::Time::getMillisecondCounterHiRes(); auto t = generate (*P, r);
                                                               std::printf ("    (%s CLEAR %s: %.0f ms, %.1f MB, %d zones)\n", typeName (ty), cl ? "on " : "off", juce::Time::getMillisecondCounterHiRes() - t0, t != nullptr ? t->bytes() / 1e6 : 0.0, t != nullptr ? (int) t->zones.size() : 0);
                                                               return std::shared_ptr<const Take> (t); };
    // ---- INSTRUMENT
    auto i0 = make (Type::Instrument, false), i1 = make (Type::Instrument, true);
    check (i0 != nullptr && i1 != nullptr && i0->rootNote == i1->rootNote, "INSTRUMENT: both takes made from the same vowel, same root (" + juce::MidiMessage::getMidiNoteName (i1->rootNote, true, true, 5) + ")");
    bool every = true; for (int n = i1->rootNote - 18; n <= i1->rootNote + 18; ++n) every = every && i1->zones[(size_t) Engine::zoneFor (*i1, n)].rootNote == n;
    check (every && i0->zones.size() == 13 && i1->zones.size() == 37, "INSTRUMENT: CLEAR has a rendered zone on every semitone of its range (37 vs 13: every 3rd)");
    const int between = i1->rootNote + 1;   // a key between the old zones (root, root + 3)
    auto h0 = held (i0, between, 3.2), h1 = held (i1, between, 3.2);
    const double want = dsp::midiToHz (between);
    const double p0 = median (pitchTrack (h0, 48000, 48000 * 3)), p1 = median (pitchTrack (h1, 48000, 48000 * 3));
    const double e0 = 1200 * std::log2 (p0 / want), e1 = 1200 * std::log2 (p1 / want);
    const double s0 = centsSpread (pitchTrack (h0, (int) (0.7 * 48000), 48000 * 3)), s1 = centsSpread (pitchTrack (h1, (int) (0.7 * 48000), 48000 * 3));
    std::printf ("    INSTRUMENT key %s: pitch error off %.1f / on %.1f cents; steadiness (RMS cents) off %.2f / on %.2f\n", juce::MidiMessage::getMidiNoteName (between, true, true, 5).toRawUTF8(), e0, e1, s0, s1);
    check (std::abs (e1) < 10.0 && s1 < s0, "INSTRUMENT: steadier pitch with CLEAR (" + f2 (s0) + " -> " + f2 (s1) + " cents RMS over the sustain; the key " + f2 (e1, 1) + " cents from its note)");
    const double hn0 = hnr (h0, 48000, 48000 * 3), hn1 = hnr (h1, 48000, 48000 * 3);
    check (hn1 > hn0, "INSTRUMENT: more harmonic, less noise with CLEAR (HNR " + f2 (hn0, 1) + " -> " + f2 (hn1, 1) + " dB)");
    const double c0 = lrCorr (h0, 48000, 48000 * 3), c1 = lrCorr (h1, 48000, 48000 * 3);
    check (c1 > c0 && c1 > 0.99, "INSTRUMENT: no fake width with CLEAR (L/R correlation " + f2 (c0, 3) + " -> " + f2 (c1, 3) + ")");
    // the formants of the key between the old zones, against the source vowel's own
    const auto& u = i1->used.front();
    const auto refEnv = envelope (src->audio, (int) ((u.first + 0.25 * (u.second - u.first)) * 48000), (int) ((u.second - 0.25 * (u.second - u.first)) * 48000));
    auto r0 = held (i0, i0->rootNote, 3.2), r1 = held (i1, i1->rootNote, 3.2);   // each take at the voice's own note: its formants are the vowel's
    const double fs0 = formantShiftCents (envelope (r0, 48000, 48000 * 3), envelope (h0, 48000, 48000 * 3)), fs1 = formantShiftCents (envelope (r1, 48000, 48000 * 3), envelope (h1, 48000, 48000 * 3));
    const double ct0 = 1200 * std::log2 (centroid (h0, 48000, 48000 * 3) / centroid (r0, 48000, 48000 * 3)), ct1 = 1200 * std::log2 (centroid (h1, 48000, 48000 * 3) / centroid (r1, 48000, 48000 * 3));
    std::printf ("    spectral centroid moved off %.0f / on %.0f cents (the pitch moved 100)\n", ct0, ct1);
    const int z0 = Engine::zoneFor (*i0, between), z1 = Engine::zoneFor (*i1, between);
    const double sp0 = std::exp2 ((between - i0->zones[(size_t) z0].rootNote) / 12.0), sp1 = std::exp2 ((between - i1->zones[(size_t) z1].rootNote) / 12.0);
    std::printf ("    INSTRUMENT key between old zones: formants moved off %.0f / on %.0f cents; played at %.3fx / %.3fx speed\n", fs0, fs1, sp0, sp1);
    check (std::abs (fs1) < std::abs (fs0) && std::abs (fs1) <= 40.0 && sp1 == 1.0 && sp0 > 1.05,
           "INSTRUMENT: a key between the old zones plays its own zone at 1.000x (right length; was " + f2 (sp0, 3) + "x) with the formants in place (" + f2 (fs0, 0) + " -> " + f2 (fs1, 0) + " cents)");
    // at the voice's own note: the take's spectrum against the source vowel's
    const double ec0 = pearson (refEnv, envelope (r0, 48000, 48000 * 3)), ec1 = pearson (refEnv, envelope (r1, 48000, 48000 * 3));
    const double fl0 = flux (r0, 48000, 48000 * 3), fl1 = flux (r1, 48000, 48000 * 3);
    check (ec1 >= ec0 - 0.002 && fl1 < fl0, "INSTRUMENT: truer to the source (spectral envelope correlation " + f2 (ec0, 3) + " -> " + f2 (ec1, 3) + "), less churn (spectral flux " + f2 (fl0, 3) + " -> " + f2 (fl1, 3) + ")");
    // ---- PAD (one key held) and TEXTURE (the root held)
    for (auto ty : { Type::Pad, Type::Texture })
    {
        auto a = make (ty, false), b = make (ty, true);
        const int key = a->rootNote + 1;
        const double secs = ty == Type::Pad ? 6.0 : 9.0; const int f0 = (int) ((ty == Type::Pad ? 2.5 : 4.0) * 48000), f1 = (int) ((secs - 0.3) * 48000);
        auto ha = held (a, key, secs), hb = held (b, key, secs);
        const juce::String nm = typeName (ty);
        bool every2 = true; for (int n = b->rootNote - 12; n <= b->rootNote + 12; ++n) every2 = every2 && b->zones[(size_t) Engine::zoneFor (*b, n)].rootNote == n;
        check (every2 && b->zones.size() > a->zones.size(), nm + ": CLEAR has a zone on every semitone (" + juce::String ((int) b->zones.size()) + " vs " + juce::String ((int) a->zones.size()) + ")");
        const double ha1 = hnr (ha, f0, f1), hb1 = hnr (hb, f0, f1);
        check (hb1 > ha1, nm + ": more harmonic, less noise with CLEAR (HNR " + f2 (ha1, 1) + " -> " + f2 (hb1, 1) + " dB)");
        const double ca = lrCorr (ha, f0, f1), cb = lrCorr (hb, f0, f1);
        check (cb > ca, nm + ": no fake width with CLEAR (L/R correlation " + f2 (ca, 3) + " -> " + f2 (cb, 3) + ")");
        const double fa = flux (ha, f0, f1), fb = flux (hb, f0, f1);
        check (fb < fa, nm + ": less smearing / churn with CLEAR (spectral flux " + f2 (fa, 3) + " -> " + f2 (fb, 3) + ")");
        const double sa = centsSpread (pitchTrack (ha, f0, f1)), sb = centsSpread (pitchTrack (hb, f0, f1));
        check (sb < sa, nm + ": steadier pitch with CLEAR (" + f2 (sa) + " -> " + f2 (sb) + " cents RMS)");
        check (highShare (b->preview, 7000) < 0.03, nm + " CLEAR: still no harsh top (" + f2 (highShare (b->preview, 7000) * 100, 2) + " % above 7 kHz)");
    }
    // ---- CHOPS: the chops on the keys, against the parts of the sound they came from
    {
        auto a = make (Type::Chops, false), b = make (Type::Chops, true);
        auto chopFidelity = [&] (const Take& t, double& envCorr, double& snr, int& untouched)
        {
            envCorr = 0; snr = 0; untouched = 0; int n = 0;
            for (size_t i = 0; i < t.chops.size() && i < t.used.size(); ++i)
            {
                const auto& ch = t.chops[i];
                auto s = dsp::slice (P->work->buffer(), t.used[i].first * 48000 - 0.004 * 48000, t.used[i].second * 48000);
                auto e1 = rmsEnv (ch, 120), e2 = rmsEnv (s, 120);
                envCorr += pearson (e1, e2);
                // gain-matched sample difference over the middle (the fades excluded)
                const int L = juce::jmin (ch.getNumSamples(), s.getNumSamples()), a0 = (int) (0.02 * 48000), a1 = L - (int) (0.03 * 48000);
                double xy = 0, yy = 0; for (int k = a0; k < a1; ++k) { xy += (double) ch.getSample (0, k) * s.getSample (0, k); yy += (double) s.getSample (0, k) * s.getSample (0, k); }
                const double g = yy > 0 ? xy / yy : 0; double err = 0, sig = 0;
                for (int k = a0; k < a1; ++k) { const double d = ch.getSample (0, k) - g * s.getSample (0, k); err += d * d; sig += (double) ch.getSample (0, k) * ch.getSample (0, k); }
                const double db = juce::jlimit (-10.0, 80.0, 10.0 * std::log10 ((sig + 1e-12) / (err + 1e-12)));
                snr += db; if (db > 60.0) ++untouched; ++n;
            }
            if (n > 0) { envCorr /= n; snr /= n; }
        };
        double ea, sa, eb, sb; int ua, ub;
        chopFidelity (*a, ea, sa, ua); chopFidelity (*b, eb, sb, ub);
        std::printf ("    CHOPS: envelope corr off %.3f / on %.3f, mean SNR off %.1f / on %.1f dB, untouched chops off %d / on %d of %d\n", ea, eb, sa, sb, ua, ub, (int) b->chops.size());
        check (eb >= ea && sb > sa && ub >= ua, "CHOPS: the chops stay truer to the sound with CLEAR (envelope " + f2 (ea, 3) + " -> " + f2 (eb, 3) + ", sample match " + f2 (sa, 1) + " -> " + f2 (sb, 1) + " dB, untouched " + juce::String (ua) + " -> " + juce::String (ub) + ")");
        check (a->digest != b->digest && b->ensemble == 0.0f, "CHOPS: CLEAR makes its own take, no chorus on it");
    }
    // ---- clipping / headroom: every type, the preview and the export
    for (int ty = 1; ty <= 4; ++ty)
    {
        auto a = make ((Type) ty, false), b = make ((Type) ty, true);
        auto viaFx = [] (const Take& t)
        {   // the preview through the take's FX (SPACE 0) = what the finish gets
            Fx fx; fx.space.store (0.0f); fx.tone.store (0.0f); fx.width.store (0.55f); fx.warmth.store (0.35f); fx.ensemble.store (t.ensemble); fx.clear.store (t.clear()); fx.prepare (48000, 512);
            Buf o; o.makeCopyOf (t.preview); for (int i = 0; i < o.getNumSamples(); i += 512) fx.process (o.getWritePointer (0) + i, o.getWritePointer (1) + i, juce::jmin (512, o.getNumSamples() - i));
            return o;
        };
        const auto wa = renderForExport (*a, 0.0f, 0.0f, 0.55f, 0.35f), wb = renderForExport (*b, 0.0f, 0.0f, 0.55f, 0.35f);
        const double squashA = crestDb (viaFx (*a)) - crestDb (wa), squashB = crestDb (viaFx (*b)) - crestDb (wb);
        bool zonesOk = true; for (auto& z : b->zones) { const bool ok = clipFree (z.audio) && dsp::clicksIn (asPlayed (z)) == 0; if (! ok) std::printf ("    zone %d: peak %.3f clicks %d\n", z.rootNote, dsp::peakOf (z.audio), dsp::clicksIn (asPlayed (z))); zonesOk = zonesOk && ok; }
        for (auto& c : b->chops) zonesOk = zonesOk && clipFree (c);
        check (clipFree (b->preview) && clipFree (wb) && dsp::truePeakDb (wb) < -1.0 && zonesOk && dsp::clicksIn (b->preview) == 0 && squashB <= squashA + 0.01 && std::abs (squashB) < 0.15,
               juce::String (typeName ((Type) ty)) + " CLEAR: no clipping, no clicks, no limiting (export " + f2 (dsp::lufs (wb, 48000), 1) + " LUFS, " + f2 (dsp::truePeakDb (wb)) + " dBTP; peaks squashed " + f2 (squashA, 2) + " -> " + f2 (squashB, 2) + " dB)");
    }
    // ---- determinism: CLEAR renders its zones on every core; the same seed is still the same take, bit for bit
    for (int ty = 1; ty <= 4; ++ty)
    {
        Recipe r; r.type = (Type) ty; r.seed = 42; r.detail = 9; r.clear = true;
        auto a = generate (*P, r), b = generate (*P, r);
        check (a != nullptr && b != nullptr && a->digest == b->digest, juce::String (typeName ((Type) ty)) + " CLEAR: seed 42 twice = bit for bit the same");
    }
    // ---- the recipe: saved with the take, old recipes load with it off
    {
        Recipe r; r.clear = true; auto v = r.toVar();
        check (Recipe::fromVar (v).clear, "CLEAR is saved with the take's recipe");
        v.getDynamicObject()->removeProperty ("clear");
        check (! Recipe::fromVar (v).clear, "a take saved before CLEAR loads with it off (it sounds as it did)");
    }
}

// ---------------------------------------------------------------- CLEAR A/B renders for the owner (24-bit 48 kHz WAVs)
static void clearAB (const juce::File& dir)
{
    section ("CLEAR A/B renders");
    dir.createDirectory();
    auto src = sung(); auto P = prepOf (src);
    juce::String e;
    exporter::writeWav (src->audio, 48000, dir.getChildFile ("source-sung-phrase.wav"), 24, e);
    juce::AudioBuffer<float> all (2, 0);
    auto append = [&all] (const juce::AudioBuffer<float>& b, double gapS)
    {
        const int at = all.getNumSamples(), gap = (int) (gapS * 48000);
        all.setSize (2, at + b.getNumSamples() + gap, true, true, true);
        for (int c = 0; c < 2; ++c) all.copyFrom (c, at, b, juce::jmin (c, b.getNumChannels() - 1), 0, b.getNumSamples());
    };
    for (int ty = 1; ty <= 4; ++ty)
        for (int cl = 0; cl < 2; ++cl)
        {
            Recipe r; r.type = (Type) ty; r.seed = 2024; r.detail = 5; r.musical = 0.7; r.clear = cl == 1;
            auto t = generate (*P, r);
            auto w = renderForExport (*t, 0.0f, 0.35f, 0.55f, 0.35f);
            const auto name = juce::String (typeName ((Type) ty)).toLowerCase() + (cl ? "-clear-on" : "-clear-off");
            const bool ok = exporter::writeWav (w, 48000, dir.getChildFile (name + ".wav"), 24, e);
            check (ok, name + ".wav (" + f2 (w.getNumSamples() / 48000.0, 1) + " s, " + f2 (dsp::lufs (w, 48000), 1) + " LUFS, " + f2 (dsp::truePeakDb (w)) + " dBTP)");
            append (w, cl ? 1.5 : 0.8);
        }
    check (exporter::writeWav (all, 48000, dir.getChildFile ("clear-ab-all.wav"), 24, e), "clear-ab-all.wav: INSTRUMENT, PAD, CHOPS, TEXTURE, each CLEAR off then on (" + f2 (all.getNumSamples() / 48000.0, 1) + " s)");
}


// ---------------------------------------------------------------- the parts (the boxes on the waveform) are editable
// a chop against the sound at [a, b) seconds: the sample match (dB, gain-matched, the 3 / 12 ms fades left out) and the length check
static double chopMatchDb (const juce::AudioBuffer<float>& chop, const SourceAudio& src, double a, double b)
{
    const int s0 = (int) std::llround (a * src.sampleRate), n = (int) std::llround (b * src.sampleRate) - s0;
    if (chop.getNumSamples() != n) return -99.0;
    const int e0 = (int) (0.004 * 48000), e1 = n - (int) (0.013 * 48000);
    double xy = 0, yy = 0; for (int k = e0; k < e1; ++k) { xy += (double) chop.getSample (0, k) * src.audio.getSample (0, s0 + k); yy += (double) src.audio.getSample (0, s0 + k) * src.audio.getSample (0, s0 + k); }
    const double g = yy > 0 ? xy / yy : 0; double err = 0, sig = 0;
    for (int k = e0; k < e1; ++k) { const double d = chop.getSample (0, k) - g * src.audio.getSample (0, s0 + k); err += d * d; sig += (double) chop.getSample (0, k) * chop.getSample (0, k); }
    return 10.0 * std::log10 ((sig + 1e-15) / (err + 1e-15));
}
static juce::MouseEvent mouseAt (juce::Component& c, juce::Point<float> p, juce::Point<float> down, juce::ModifierKeys mods, int clicks = 1, bool dragged = false)
{
    const auto now = juce::Time::getCurrentTime();
    return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p, mods, juce::MouseInputSource::defaultPressure, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, down, now, clicks, dragged);
}

static void testParts (const juce::File& dir)
{
    section ("parts: the highlighted boxes move / resize / add / delete, and the take rebuilds from them");
    ghosty::License::testForce().store (1);
    auto file = wavOf (sung(), "sung-parts.wav");
    SliceProcessor p; p.prepareToPlay (48000, 512);
    juce::String err; p.loadAudio (file, err); p.waitIdle (60000);
    const auto& src = *p.getSource();
    p.type = Type::Chops; p.musical = 0.0; p.clear = true;   // (NATURAL + CLEAR: a chop in its own pitch is the sound itself)
    p.generate(); p.waitIdle (60000); p.playPreview (false);
    check (p.currentTake() != nullptr && ! p.partsEdited() && ! p.takeParts().empty(), "a CHOPS take: its parts are the generator's (" + juce::String ((int) p.takeParts().size()) + " boxes)");
    const auto origDigest = p.currentTake()->digest;
    // 1. the user's boxes: three parts, each one chop, sample for sample
    SliceProcessor::Parts mine { { 0.30, 0.55 }, { 1.20, 1.50 }, { 2.40, 2.70 } };
    const auto before = p.currentTake();
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    check (p.editParts (mine), "an edit is accepted");
    const double callMs = juce::Time::getMillisecondCounterHiRes() - t0;
    check (callMs < 50.0 && p.currentTake() == before, "the edit returns at once (" + f2 (callMs, 1) + " ms) and the old take keeps playing while the new one renders");
    p.waitIdle (60000);
    check (! p.lastRenderOnMessageThread.load() && p.currentTake() != before, "the take re-rendered on the worker thread, off the audio and message threads");
    auto t = p.currentTake();
    bool exact = t->chops.size() == 3; double worst = 999;
    for (size_t i = 0; i < 3 && exact; ++i) { const double db = chopMatchDb (t->chops[i], src, mine[i].first, mine[i].second); worst = std::min (worst, db); exact = db > 60.0; }
    check (exact, "each box is one chop, in order, sample-accurate to its part of the sound (worst match " + f2 (worst, 1) + " dB)");
    check (t->used.size() == 3 && std::abs (t->used[1].first - 1.20) < 1e-3 && p.takeParts() == mine, "the waveform shows exactly the user's boxes");
    // 2. move the second box: its chop becomes the new part
    const auto oldChop1 = t->chops[1];
    auto moved = mine; moved[1] = { 1.60, 1.90 };
    p.editParts (moved); p.waitIdle (60000); t = p.currentTake();
    check (chopMatchDb (t->chops[1], src, 1.60, 1.90) > 60.0 && chopMatchDb (t->chops[1], src, 1.20, 1.50) < 20.0, "moving a box: that chop is now the new part of the sound, sample-accurate (" + f2 (chopMatchDb (t->chops[1], src, 1.60, 1.90), 1) + " dB)");
    check (chopMatchDb (t->chops[0], src, 0.30, 0.55) > 60.0 && chopMatchDb (t->chops[2], src, 2.40, 2.70) > 60.0, "the other chops stay as they were, on the same keys");
    // 3. resize the first box
    auto resized = moved; resized[0] = { 0.30, 0.70 };
    p.editParts (resized); p.waitIdle (60000); t = p.currentTake();
    check (t->chops[0].getNumSamples() == (int) std::llround (0.40 * 48000) && chopMatchDb (t->chops[0], src, 0.30, 0.70) > 60.0, "resizing a box changes its chop's length (" + f2 (t->chops[0].getNumSamples() / 48000.0, 3) + " s)");
    // 4. delete the last
    auto fewer = resized; fewer.pop_back();
    p.editParts (fewer); p.waitIdle (60000); t = p.currentTake();
    check (t->chops.size() == 2, "deleting a box removes its chop (" + juce::String ((int) t->chops.size()) + " chops)");
    // 5. undo, step by step, back to the generator's own take
    check (p.undoEdit() && p.waitIdle (60000) && p.currentTake()->chops.size() == 3 && p.takeParts() == resized, "Cmd / Ctrl + Z: the deleted chop is back");
    p.undoEdit(); p.waitIdle (60000); p.undoEdit(); p.waitIdle (60000); p.undoEdit(); p.waitIdle (60000);
    check (! p.partsEdited() && p.currentTake()->digest == origDigest, "undoing every edit restores the original take, bit for bit");
    p.editParts (resized); p.waitIdle (60000);
    // 6. the session keeps the edited parts
    juce::MemoryBlock mb; p.getStateInformation (mb);
    {
        SliceProcessor q; q.prepareToPlay (48000, 512);
        q.setStateInformation (mb.getData(), (int) mb.getSize()); waitReopened (q);
        const bool same = q.takeParts() == resized && q.currentTake() != nullptr && q.currentTake()->chops.size() == 3 && q.currentTake()->digest == p.currentTake()->digest;
        check (same, "a reopened session has the edited parts and re-renders the same take");
    }
    // 7. limits: a type's number of parts; VARY keeps the boxes, GENERATE starts fresh
    check (p.maxParts() == 16, "CHOPS takes up to 16 parts");
    p.vary(); p.waitIdle (60000);
    check (p.takeParts() == resized && p.currentTake()->chops.size() == 3, "VARY keeps the user's boxes");
    p.generate(); p.waitIdle (60000);
    check (! p.partsEdited(), "GENERATE starts fresh (the generator picks)");
    p.type = Type::Instrument; p.generate(); p.waitIdle (60000);
    {   // build 6: INSTRUMENT sings the syllables of up to 8 parts (the first is also the held tone); nine are refused
        SliceProcessor::Parts nine; for (int i = 0; i < 9; ++i) nine.push_back ({ 0.1 + 0.25 * i, 0.3 + 0.25 * i });
        check (p.maxParts() == 8 && ! p.editParts (nine) && p.editParts ({ { 1.2, 1.6 } }), "INSTRUMENT takes up to 8 parts (nine are refused, one is used)");
    }
    p.waitIdle (60000);
    check (! p.currentTake()->used.empty() && p.currentTake()->used[0].first >= 1.2 - 1e-3 && p.currentTake()->used.back().second <= 1.6 + 1e-3, "INSTRUMENT: the take holds the user's part");
    p.type = Type::Pad; p.generate(); p.waitIdle (60000);
    { SliceProcessor::Parts pp { { 0.35, 0.8 }, { 1.3, 1.8 }, { 3.0, 3.6 } }; p.editParts (pp); p.waitIdle (60000);
      check (p.currentTake() != nullptr && p.takeParts() == pp && dsp::rmsOf (p.currentTake()->preview) > 0.01, "PAD: rebuilt from the user's three parts"); }
    // 8. the screen: hit-test, a mouse drag, double-click add, a click hears the part
    p.type = Type::Chops; p.generate(); p.waitIdle (60000); p.editParts (mine); p.waitIdle (60000); p.playPreview (false);
    std::unique_ptr<SliceEditor> ed (static_cast<SliceEditor*> (p.createEditor())); ed->setVisible (true); ed->setSize (1477, 1065); pump (50);
    auto& st = ed->stage; const double secs = src.seconds(); const float cy = st.waveArea().getCentreY();
    const float bx0 = st.fracToX (1.20 / secs), bx1 = st.fracToX (1.50 / secs);
    const auto hl = st.hitBox ({ bx0 + 1.0f, cy }), hr = st.hitBox ({ bx1 - 1.0f, cy }), hm = st.hitBox ({ (bx0 + bx1) * 0.5f, cy }), he = st.hitBox ({ st.fracToX (2.0 / secs), cy });
    check (hl.box == 1 && hl.what == 1 && hr.box == 1 && hr.what == 2 && hm.box == 1 && hm.what == 3 && he.box == -1, "hit-test: a box's left edge, right edge and middle; empty waveform is nothing");
    { const auto down = juce::Point<float> ((bx0 + bx1) * 0.5f, cy); st.mouseMove (mouseAt (st, down, down, {})); check (st.getTooltip() == juce::String (gslice::ui::Stage::kBoxTip), "over a box, the tooltip explains the boxes"); }
    // drag the middle 60 px right (snapped), take the picture mid-drag, let go
    {
        const auto down = juce::Point<float> ((bx0 + bx1) * 0.5f, cy), to = down.translated (60.0f, 0.0f);
        st.mouseDown (mouseAt (st, down, down, {}));
        st.mouseDrag (mouseAt (st, down.translated (30.0f, 0.0f), down, {}, 1, true));
        st.mouseDrag (mouseAt (st, to, down, {}, 1, true)); pump (30);
        savePng (shot (*ed), dir.getChildFile ("ui-box-drag-purple.png"));
        st.mouseUp (mouseAt (st, to, down, {}, 1, true));
        p.waitIdle (60000); pump (30);
        const auto ps = p.takeParts(); const double want = 60.0 / st.waveArea().getWidth() * secs * (st.v1 - st.v0);
        const double movedBy = ps.size() == 3 ? ps[1].first - 1.20 : 0.0;
        check (ps.size() == 3 && std::abs (movedBy - want) < 0.02 && std::abs ((ps[1].second - ps[1].first) - 0.30) < 1e-9 && p.currentTake()->chops[1].getNumSamples() == (int) std::llround ((ps[1].second - ps[1].first) * 48000),
               "a mouse drag moves the box (" + f2 (movedBy, 3) + " s for 60 px; the length kept) and the chop follows");
        // the start sits on a zero crossing (snapped) unless Shift is held
        auto zc = [&] (double sec) { const int k = (int) std::llround (sec * 48000); const float a0 = src.audio.getSample (0, k - 1), a1 = src.audio.getSample (0, k); return (a0 <= 0 && a1 > 0) || (a0 >= 0 && a1 < 0) || a1 == 0; };
        check (zc (ps[1].first) || zc (ps[1].second), "the moved box snapped (an edge on a zero crossing: no click)");
    }
    // the right edge: resize; it cannot run into the next box
    {
        const auto ps = p.takeParts(); const float ex = st.fracToX (ps[1].second / secs);
        const auto down = juce::Point<float> (ex, cy), to = juce::Point<float> (st.fracToX (2.6 / secs), cy);
        st.mouseDown (mouseAt (st, down, down, juce::ModifierKeys::shiftModifier)); st.mouseDrag (mouseAt (st, to, down, juce::ModifierKeys::shiftModifier, 1, true)); st.mouseUp (mouseAt (st, to, down, juce::ModifierKeys::shiftModifier, 1, true));
        p.waitIdle (60000);
        const auto q = p.takeParts();
        check (q.size() == 3 && std::abs (q[1].second - 2.40) < 1e-6 && q[1].second <= q[2].first, "dragging an edge resizes; it stops at the next box (no overlap)");
    }
    // a click (no drag) hears the part and flashes its CHOPS key
    {
        const auto ps = p.takeParts(); const auto at = juce::Point<float> (st.fracToX ((ps[0].first + ps[0].second) * 0.5 / secs), cy);
        st.mouseDown (mouseAt (st, at, at, {})); st.mouseUp (mouseAt (st, at, at, {}));
        juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer mm; float mx = 0; for (int i = 0; i < 20; ++i) { p.processBlock (b, mm); mx = std::max (mx, b.getMagnitude (0, 512)); }
        check (p.sourcePlaying() || mx > 1e-3f, "a click on a box plays that part of the sound");
        check (ed->keys.flash == Take::kChopNote, "and flashes its key (FL C3 for the first chop)");
        p.playSource (false);
    }
    // double-click empty waveform: a new part there (a new chop)
    {
        const auto at = juce::Point<float> (st.fracToX (3.6 / secs), cy);
        st.mouseDoubleClick (mouseAt (st, at, at, {}, 2)); p.waitIdle (60000);
        check (p.takeParts().size() == 4 && p.currentTake()->chops.size() == 4, "double-click on empty waveform adds a part: a new chop");
    }
    // Cmd / Ctrl + Z in the window
    { ed->keyPressed (juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)); p.waitIdle (60000);
      check (p.takeParts().size() == 3, "Cmd / Ctrl + Z in the window undoes the add"); }
    ed.reset(); pump (20);
    ghosty::License::testForce().store (-1);
}

// ---------------------------------------------------------------- the demo export gate
static constexpr int kSliceInventory = 3;
static int demogateTest()
{
    ghosty::demogate::Sandbox box;
    ghosty::License::testForce().store (1);
    SliceProcessor p; p.prepareToPlay (48000, 512);
    p.loadSourceForTests (sung()); pump (30); p.waitIdle (60000); p.generate(); p.waitIdle (60000);
    std::unique_ptr<SliceEditor> ed (static_cast<SliceEditor*> (p.createEditor())); ed->setVisible (true); pump (50);
    std::vector<ghosty::demogate::Path> paths {
        { "slice.drag.wav", ghosty::demogate::Drag, [&] { ed->dragWav.startDrag(); pump (30); } },
        { "slice.drag.midi", ghosty::demogate::Drag, [&] { ed->dragMidi.startDrag(); pump (30); } },
        { "slice.take.save", ghosty::demogate::File, [&] { ed->saveBtn.triggerClick(); pump (60); } },
    };
    int fails = ghosty::demogate::run ("Ghost Slice", paths, kSliceInventory, box);
    ed.reset(); pump (30);
    fails += ghosty::demogate::checkDropout ("Ghost Slice", []
    {
        auto q = std::make_unique<SliceProcessor>(); q->setPlayConfigDetails (0, 2, 48000, 480); q->prepareToPlay (48000, 480);
        q->loadSourceForTests (sung()); q->waitIdle (60000); q->type = Type::Instrument; q->generate(); q->waitIdle (60000); q->playPreview (false); pump (30);
        return std::unique_ptr<juce::AudioProcessor> (std::move (q));
    }, [] (juce::AudioBuffer<float>&, juce::MidiBuffer& m, juce::int64 pos) { if (pos == 0) m.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0); });
    return fails;
}

// ---------------------------------------------------------------- the example renders
static void examples (const juce::File& dir)
{
    section ("examples");
    dir.createDirectory();
    juce::String e;
    exporter::writeWav (spoken()->audio, 48000, dir.getChildFile ("source-spoken-sentence.wav"), 24, e);
    exporter::writeWav (sung()->audio, 48000, dir.getChildFile ("source-sung-phrase.wav"), 24, e);
    auto write = [&] (const Take& t, const juce::String& name)
    {
        auto w = renderForExport (t, 0.0f, 0.45f, 0.55f, 0.35f);
        exporter::writeWav (w, t.rate, dir.getChildFile (name + ".wav"), 24, e);
        exporter::writeMidi (t.midi, dir.getChildFile (name + ".mid"), e);
        std::printf ("  %-40s %5.1f s  %6.2f LUFS  %6.2f dBTP  %s\n", name.toRawUTF8(), w.getNumSamples() / 48000.0, dsp::lufs (w, 48000), dsp::truePeakDb (w), t.info.toRawUTF8());
    };
    for (int v = 0; v < 2; ++v)
    {
        auto P = prepOf (v == 0 ? spoken() : sung());
        const juce::String vn = v == 0 ? "spoken" : "sung";
        for (int ty = 1; ty <= 4; ++ty)
            for (int k = 0; k < 2; ++k)
            {
                Recipe r; r.type = (Type) ty; r.seed = 1000 + 17 * (uint32_t) k + (uint32_t) ty; r.detail = 31 + (uint32_t) k; r.musical = 0.75;
                auto t = generate (*P, r); write (*t, vn + "-" + juce::String (typeName ((Type) ty)).toLowerCase() + "-take" + juce::String (k + 1));
            }
        for (double m : { 1.0, 0.0 })
            for (int ty : { 1, 2 })
            {
                Recipe r; r.type = (Type) ty; r.seed = 2024; r.detail = 5; r.musical = m;
                auto t = generate (*P, r); write (*t, vn + "-" + juce::String (typeName ((Type) ty)).toLowerCase() + (m > 0.5 ? "-MUSICAL" : "-NATURAL"));
            }
        for (auto c : { Character::Natural, Character::Chipmunk, Character::Deep })
        {
            Recipe r; r.type = Type::Instrument; r.seed = 2024; r.detail = 5; r.character = c;
            auto t = generate (*P, r); write (*t, vn + "-instrument-character-" + juce::String (characterName (c)));
        }
    }
    testUi (dir);
}

#include "SliceTests6.h"
#include "SliceTests7.h"
#include "SliceTests8.h"
#include "SliceTests9.h"
#include "SliceTests10.h"
#include "SliceTests11.h"
#include "SliceTests12.h"
#include "SliceTests13.h"
#include "SliceDigests.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    auto home = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("gslice_home"); home.deleteRecursively(); home.createDirectory();
    setenv ("HOME", home.getFullPathName().toRawUTF8(), 1);
    const juce::String mode = argc > 1 ? argv[1] : "all";
    if (mode == "demogate") { const int f = demogateTest(); std::printf ("\nGhostSliceTests demogate: %s\n", f == 0 ? "PASS" : "FAIL"); return f == 0 ? 0 : 1; }
    if (mode == "clearab") { clearAB (juce::File (argc > 2 ? juce::String (argv[2]) : juce::String ("/home/claude/work/gs/ab"))); std::printf ("\nclearab: %d passed, %d failed\n", gPass, gFail); return gFail == 0 ? 0 : 1; }
    if (mode == "examples") { examples (juce::File (argc > 2 ? juce::String (argv[2]) : juce::String ("/home/claude/work/ghostslice-gen"))); std::printf ("\nexamples: %d passed, %d failed\n", gPass, gFail); return gFail == 0 ? 0 : 1; }
    if (mode == "exp2") { six::exp2(); return 0; }
    if (mode == "vdump") { six::vdump (juce::File (argc > 2 ? juce::String (argv[2]) : juce::String ("/home/claude/work/gs/vdump"))); return 0; }
    if (mode == "clk") { six::clkDebug(); return 0; }
    if (mode == "ab2") { six::abDemos (juce::File (argc > 2 ? juce::String (argv[2]) : juce::String ("/home/claude/work/gs/ab2"))); std::printf ("\nab2: %d passed, %d failed\n", gPass, gFail); return gFail == 0 ? 0 : 1; }
    if (mode == "ab7") { seven::abSeven (juce::File (argc > 2 ? juce::String (argv[2]) : juce::String ("/home/claude/work/gs/ab2"))); std::printf ("\nab7: %d passed, %d failed\n", gPass, gFail); return gFail == 0 ? 0 : 1; }
    if (mode == "ab8") { eight::abEight (juce::File (argc > 2 ? juce::String (argv[2]) : juce::String ("/home/claude/work/gs/ab2"))); std::printf ("\nab8: %d passed, %d failed\n", gPass, gFail); return gFail == 0 ? 0 : 1; }
    if (mode == "ab9") { nine::abNine (juce::File (argc > 2 ? juce::String (argv[2]) : juce::String ("/home/claude/work/gs/ab2"))); std::printf ("\nab9: %d passed, %d failed\n", gPass, gFail); return gFail == 0 ? 0 : 1; }
    if (mode == "ab10") { ten::abTen (juce::File (argc > 2 ? juce::String (argv[2]) : juce::String ("/home/claude/work/gs/ab2"))); std::printf ("\nab10: %d passed, %d failed\n", gPass, gFail); return gFail == 0 ? 0 : 1; }
    if (mode == "digests") { digests::dump (argc > 2 ? juce::String (argv[2]).getIntValue() : 8); return 0; }
    if (mode == "ab11") { eleven::abEleven (juce::File (argc > 2 ? juce::String (argv[2]) : juce::String ("/home/claude/work/gs/ab2"))); std::printf ("\nab11: %d passed, %d failed\n", gPass, gFail); return gFail == 0 ? 0 : 1; }
    if (mode == "clickdiag") { eleven::clickDiag(); return 0; }
    if (mode == "ab13") { thirteen::abThirteen (juce::File (argc > 2 ? juce::String (argv[2]) : juce::String ("/home/claude/work/gs/ab2"))); std::printf ("\nab13: %d passed, %d failed\n", gPass, gFail); return gFail == 0 ? 0 : 1; }
    if (mode == "ab12") { twelve::abTwelve (juce::File (argc > 2 ? juce::String (argv[2]) : juce::String ("/home/claude/work/gs/ab2"))); std::printf ("\nab12: %d passed, %d failed\n", gPass, gFail); return gFail == 0 ? 0 : 1; }
    if (mode == "diag") { six::diag(); std::printf ("\ndiag: %d passed, %d failed\n", gPass, gFail); return 0; }
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    auto want = [&] (const char* g) { return mode == "all" || mode == g; };
    if (want ("dsp")) testDsp();
    if (want ("material")) testMaterial();
    if (want ("types")) testTypes();
    if (want ("determinism")) testDeterminism();
    if (want ("midi")) testMidi();
    if (want ("rtsafety")) testRtSafety();
    if (want ("record")) testRecord();
    if (want ("state")) testState();
    if (want ("oldstate")) testOldState();
    if (want ("loudness")) testLoudness();
    if (want ("speed")) testSpeed();
    if (want ("clear")) testClear();
    if (want ("parts")) testParts (juce::File (GSLICE_SHOTS_DIR));
    if (want ("ui")) testUi (juce::File (GSLICE_SHOTS_DIR));
    // build 6
    if (want ("morph") || mode == "six") six::morphSix();
    if (want ("voice") || mode == "six") six::voiceSix();
    if (want ("pl") || mode == "six") six::plSix();
    if (want ("brain") || mode == "six") six::brainSix();
    if (want ("vary") || mode == "six") six::varySix();
    if (want ("rt6") || mode == "six") six::rtSix();
    if (want ("ui6") || mode == "six") six::uiSix (juce::File (GSLICE_SHOTS_DIR));
    // build 7
    if (want ("parlour") || mode == "seven") seven::parlourSeven();
    // build 8
    if (want ("play") || mode == "eight") eight::coreEight();
    if (want ("playtypes") || mode == "eight") eight::typesEight();
    if (want ("roll") || mode == "eight") eight::rollEight (juce::File (GSLICE_SHOTS_DIR));
    // build 9
    if (want ("psola") || mode == "nine") nine::psolaNine();
    if (want ("edits9") || mode == "nine") nine::editsNine (juce::File (GSLICE_SHOTS_DIR));
    // build 10
    if (want ("big") || mode == "ten") ten::bigMovesTen();
    // (build 10's two-track MIDI test is replaced by build 11's: DRAG MIDI and DRAG CHORDS are one track each)
    if (want ("old10") || mode == "ten") ten::oldChopsTen (juce::File (GSLICE_SHOTS_DIR));
    // build 11
    if (want ("far") || mode == "eleven") eleven::farMovesEleven();
    if (want ("midi11") || mode == "eleven") eleven::midiEleven();
    if (want ("quality") || mode == "eleven") eleven::qualityEleven();
    if (want ("ui11") || mode == "eleven") eleven::uiEleven (juce::File (GSLICE_SHOTS_DIR));
    // build 12: TERRAIN
    if (want ("terrain") || mode == "twelve") twelve::coreTwelve();
    if (want ("terrainproc") || mode == "twelve") twelve::processorTwelve();
    if (want ("ui12") || mode == "twelve") twelve::uiTwelve (juce::File (GSLICE_SHOTS_DIR));
    if (want ("land13") || mode == "thirteen") thirteen::coreThirteen();
    std::printf ("\nGhostSliceTests %s: %d passed, %d failed (%.1f s)\n", mode.toRawUTF8(), gPass, gFail, (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0);
    return gFail == 0 ? 0 : 1;
}
