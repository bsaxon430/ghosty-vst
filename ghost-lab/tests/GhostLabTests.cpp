// GhostLabTests: Ghost Lab's checks, demo renders and screenshots. Run under xvfb-run -a (the UI tests draw windows).
//   GhostLabTests                 all tests
//   GhostLabTests demos DIR       the demo sources + every action + breeding as WAVs into DIR
//   GhostLabTests shots DIR       UI screenshots (wide + narrow + deep) into DIR
//   GhostLabTests bodyshots DIR   THE LIVING SOUND and its tube alone, at rest and in the hand (quick)
//   GhostLabTests bodydemos DIR   what a hand on THE LIVING SOUND sounds like (the live chain), as WAVs
// Sources are synthesised here (our own formant voice + additive piano), so nothing third-party is needed.
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "Plugin/Processor.h"
#include "Plugin/Editor.h"
#include "TerrainSerial.h"
#include "Core/LabTerrainMap.h"
#include "Core/Hood.h"
#include "Plugin/HoodPage.h"
#include <dlfcn.h>
#include <pthread.h>
#include <thread>
#include <set>
#include <cstdio>

using namespace glab;

// ------------------------------------------------------------------------------------------------ audio-thread spies
static thread_local bool tlAudio = false;
static std::atomic<int> gAudioAllocs { 0 }, gAudioLocks { 0 };
void* operator new (size_t n) { if (tlAudio) ++gAudioAllocs; if (void* p = std::malloc (n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[] (size_t n) { if (tlAudio) ++gAudioAllocs; if (void* p = std::malloc (n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete (void* p) noexcept { if (tlAudio && p) ++gAudioAllocs; std::free (p); }
void operator delete[] (void* p) noexcept { if (tlAudio && p) ++gAudioAllocs; std::free (p); }
void operator delete (void* p, size_t) noexcept { if (tlAudio && p) ++gAudioAllocs; std::free (p); }
void operator delete[] (void* p, size_t) noexcept { if (tlAudio && p) ++gAudioAllocs; std::free (p); }
extern "C" int pthread_mutex_lock (pthread_mutex_t* m)
{
    using F = int (*) (pthread_mutex_t*);
    static F real = (F) dlsym (RTLD_NEXT, "pthread_mutex_lock");
    if (tlAudio) ++gAudioLocks;
    return real (m);
}

// ------------------------------------------------------------------------------------------------ tiny test kit
static int failures = 0, passes = 0;
static void check (bool ok, const juce::String& what)
{
    if (ok) ++passes; else ++failures;
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
    std::fflush (stdout);
}

// ------------------------------------------------------------------------------------------------ sources (ours)
static std::shared_ptr<Audio> synthVoice (double sr = 44100.0, double seconds = 4.2)
{
    // a sung phrase: additive voice (harmonics shaped by moving vowel formants), vibrato, breath and "s"/"h" consonants
    struct Syl { double t0, t1; float midi; int vowel; bool sibilant; };
    const std::vector<Syl> syl { { 0.05, 0.70, 57, 0, false }, { 0.78, 1.35, 60, 3, true }, { 1.42, 2.10, 62, 1, false },
                                 { 2.18, 2.75, 64, 4, true }, { 2.82, 3.45, 62, 2, false }, { 3.52, 4.10, 60, 0, false } };
    const float F[5][3] = { { 730, 1090, 2440 }, { 270, 2290, 3010 }, { 300, 870, 2240 }, { 530, 1840, 2480 }, { 570, 840, 2410 } };
    const float BW[3] = { 80, 110, 160 };
    const int n = (int) (seconds * sr);
    auto a = std::make_shared<Audio>(); a->sampleRate = sr; a->buf.setSize (1, n); a->buf.clear();
    auto* out = a->buf.getWritePointer (0);
    std::vector<double> ph (80, 0.0);
    juce::Random rnd (42);
    float nlp = 0, nhp = 0, nprev = 0;
    for (int i = 0; i < n; ++i)
    {
        const double t = i / sr;
        int s = -1; for (int k = 0; k < (int) syl.size(); ++k) if (t >= syl[(size_t) k].t0 - 0.04 && t < syl[(size_t) k].t1 + 0.04) { s = k; break; }
        float v = 0;
        if (s >= 0)
        {
            auto& y = syl[(size_t) s];
            const double u = (t - y.t0) / (y.t1 - y.t0);
            const float env = (float) (juce::jlimit (0.0, 1.0, (t - y.t0 + 0.04) / 0.08) * juce::jlimit (0.0, 1.0, (y.t1 + 0.04 - t) / 0.1));
            const double vib = 1.0 + 0.012 * std::sin (juce::MathConstants<double>::twoPi * 5.5 * t) * juce::jlimit (0.0, 1.0, u * 3.0);
            const double f0 = 440.0 * std::pow (2.0, (y.midi - 69) / 12.0) * vib;
            const int nv = (s + 1) % 5;
            float fm[3];
            for (int k = 0; k < 3; ++k) fm[k] = F[y.vowel][k] + (float) juce::jlimit (0.0, 1.0, (u - 0.6) * 2.5) * (F[nv][k] - F[y.vowel][k]) * 0.4f;
            for (int h = 1; h < 80 && h * f0 < sr * 0.45; ++h)
            {
                const double fh = h * f0;
                double g = 0;
                for (int k = 0; k < 3; ++k) { const double d = (fh - fm[k]) / BW[k]; g += (k == 0 ? 1.0 : k == 1 ? 0.6 : 0.3) / (1.0 + d * d); }
                g *= 1.0 / std::pow ((double) h, 0.7);
                ph[(size_t) h] += juce::MathConstants<double>::twoPi * fh / sr;
                v += (float) (g * std::sin (ph[(size_t) h]));
            }
            v *= env * 0.16f;
            // breath + consonant at the syllable start
            const float noise = rnd.nextFloat() * 2 - 1;
            nhp = 0.6f * (nhp + noise - nprev); nprev = noise; nlp += 0.3f * (noise - nlp);
            if (y.sibilant && t < y.t0 + 0.07) v += nhp * 0.25f * (float) (1.0 - (t - y.t0 + 0.04) / 0.11);
            v += nlp * 0.012f * env;
        }
        out[i] = v;
    }
    return a;
}

static std::shared_ptr<Audio> synthPiano (double sr = 44100.0, double seconds = 4.2)
{
    struct N { double t; float midi; float vel; };
    const std::vector<N> notes { { 0.05, 60, 0.8f }, { 0.55, 64, 0.7f }, { 1.05, 67, 0.75f }, { 1.55, 72, 0.8f }, { 2.2, 48, 0.7f }, { 2.2, 55, 0.6f },
                                 { 2.2, 64, 0.6f }, { 2.2, 67, 0.6f }, { 3.2, 71, 0.5f } };
    const int n = (int) (seconds * sr);
    auto a = std::make_shared<Audio>(); a->sampleRate = sr; a->buf.setSize (1, n); a->buf.clear();
    auto* out = a->buf.getWritePointer (0);
    juce::Random rnd (7);
    for (auto& nt : notes)
    {
        const double f0 = 440.0 * std::pow (2.0, (nt.midi - 69) / 12.0), Bc = 0.00035;
        const int i0 = (int) (nt.t * sr);
        for (int i = i0; i < n; ++i)
        {
            const double t = (i - i0) / sr;
            double v = 0;
            for (int k = 1; k <= 24; ++k)
            {
                const double fk = k * f0 * std::sqrt (1.0 + Bc * k * k);
                if (fk > sr * 0.45) break;
                v += std::sin (juce::MathConstants<double>::twoPi * fk * t + k) * std::exp (-t * (0.7 + 0.28 * k)) / std::pow ((double) k, 1.1);
            }
            const double att = juce::jmin (1.0, t / 0.003);
            float hammer = t < 0.01 ? (rnd.nextFloat() * 2 - 1) * (float) (1.0 - t / 0.01) * 0.3f : 0.0f;
            out[i] += (float) (v * att * nt.vel * 0.12) + hammer * nt.vel * 0.05f;
        }
    }
    return a;
}

static std::shared_ptr<Audio> stereoOf (const Audio& m, float width)
{
    auto a = std::make_shared<Audio>(); a->sampleRate = m.sampleRate; a->buf.setSize (2, m.length());
    a->buf.copyFrom (0, 0, m.buf, 0, 0, m.length());
    a->buf.copyFrom (1, 0, m.buf, 0, 0, m.length());
    // a slight difference so it is truly stereo: a short delay on the right
    const int d = 37;
    for (int i = m.length() - 1; i >= d; --i) a->buf.setSample (1, i, (1 - width) * m.buf.getSample (0, i) + width * m.buf.getSample (0, i - d));
    return a;
}

// ------------------------------------------------------------------------------------------------ measures
// log-magnitude spectrogram on a fixed 48 (time) x 32 (bands) grid, in dB
static std::vector<float> specGrid (const Audio& a)
{
    constexpr int T = 48, Bn = 32, order = 11, N = 1 << order;
    auto m = mono (a);
    juce::dsp::FFT fft (order);
    std::vector<float> grid ((size_t) T * Bn, -100.0f), w (2 * N);
    const int n = m.length();
    for (int c = 0; c < T; ++c)
    {
        const int centre = (int) ((juce::int64) (2 * c + 1) * n / (2 * T));
        std::fill (w.begin(), w.end(), 0.0f);
        for (int i = 0; i < N; ++i) { const int j = centre - N / 2 + i; w[(size_t) i] = (j >= 0 && j < n) ? m.buf.getSample (0, j) * (0.5f - 0.5f * std::cos (6.2831853f * i / N)) : 0.0f; }
        fft.performFrequencyOnlyForwardTransform (w.data());
        std::vector<double> band (Bn, 1e-12);
        for (int k = 2; k < N / 2; ++k)
        {
            const double frac = std::log ((double) k / 2.0) / std::log ((double) N / 4.0);
            band[(size_t) juce::jlimit (0, Bn - 1, (int) (frac * Bn))] += (double) w[(size_t) k] * w[(size_t) k];
        }
        for (int b = 0; b < Bn; ++b) grid[(size_t) (c * Bn + b)] = (float) (10.0 * std::log10 (band[(size_t) b]));
    }
    return grid;
}
static float specDistance (const Audio& a, const Audio& b)   // mean |dB| difference, loudness-normalised
{
    auto ga = specGrid (a), gb = specGrid (b);
    double ma = 0, mb = 0; for (size_t i = 0; i < ga.size(); ++i) { ma += ga[i]; mb += gb[i]; }
    ma /= (double) ga.size(); mb /= (double) gb.size();
    double d = 0; for (size_t i = 0; i < ga.size(); ++i) d += std::abs (juce::jmax (-60.0, ga[i] - ma) - juce::jmax (-60.0, gb[i] - mb));
    return (float) (d / (double) ga.size());
}
static bool bitExact (const Audio& a, const Audio& b)
{
    if (a.channels() != b.channels() || a.length() != b.length() || a.sampleRate != b.sampleRate) return false;
    for (int c = 0; c < a.channels(); ++c)
        if (std::memcmp (a.buf.getReadPointer (c), b.buf.getReadPointer (c), sizeof (float) * (size_t) a.length()) != 0) return false;
    return true;
}
static std::shared_ptr<Audio> copyOf (const Audio& a) { return std::make_shared<Audio> (a); }

static void checkQuality (const Audio& out, const juce::String& name)
{
    const float tp = truePeakDb (out);
    check (tp <= kTruePeakCeilingDb + 1e-3f, name + ": true peak " + juce::String (tp, 2) + " dBTP <= -1");
    const float jump = edgeJump (out, 64);
    check (jump < 0.02f, name + ": no click at the edges (largest edge step " + juce::String (jump, 4) + ")");
    bool finite = true;
    for (int c = 0; c < out.channels(); ++c) for (int i = 0; i < out.length(); ++i) if (! std::isfinite (out.buf.getSample (c, i))) finite = false;
    check (finite, name + ": no NaN / inf");
}

// ------------------------------------------------------------------------------------------------ tests
static void testMeters()
{
    // a sine at a quarter of the sample rate, sampled 45 degrees off its crest: samples peak at 0.707, the wave at 1.0
    Audio a; a.sampleRate = 48000; a.buf.setSize (1, 4800);
    for (int i = 0; i < 4800; ++i) a.buf.setSample (0, i, (float) std::sin (juce::MathConstants<double>::halfPi * i + juce::MathConstants<double>::pi / 4));
    const float tp = truePeakDb (a), sp = juce::Decibels::gainToDecibels (peak (a));
    check (std::abs (tp) < 0.3f && sp < -2.8f, "true-peak meter sees the inter-sample crest (" + juce::String (tp, 2) + " dBTP vs sample peak " + juce::String (sp, 2) + " dBFS)");
    finish (a, 0.9f);
    check (truePeakDb (a) <= kTruePeakCeilingDb, "finish() holds a hot sound under -1 dBTP (" + juce::String (truePeakDb (a), 2) + ")");
}

static void testActions (Engine& e, const std::shared_ptr<Audio>& voice, const std::shared_ptr<Audio>& piano)
{
    struct Case { Op op; std::map<juce::String, float> v; juce::String program; };
    const std::vector<Case> cases {
        { Op::Pull, {}, "stretch time" }, { Op::Pull, { { "from", 0.3f }, { "to", 0.6f }, { "amount", 4.0f } }, "stretch time" },
        { Op::Twist, {}, "stretch spectrum" }, { Op::Twist, { { "bend", -0.8f } }, "strange shift" },
        { Op::Freeze, {}, "focus hold" }, { Op::Smear, {}, "blur blur" }, { Op::Shatter, {}, "modify radical 2" },
        { Op::Mutate, { { "wild", 0.9f } }, "pvoc synth" }, { Op::Pinch, {}, "stretch spectrum" },
        { Op::GrainCloud, {}, "modify brassage 6" }, { Op::SpectralMelt, {}, "focus accu" }, { Op::TimeWarp, {}, "modify radical 1" },
        { Op::VoiceFusion, {}, "combine cross" } };
    for (auto& src : { std::make_pair (voice, juce::String ("voice")), std::make_pair (stereoOf (*piano, 0.5f), juce::String ("piano (stereo)")) })
    {
        e.setSpecimen (src.first, src.second);
        for (auto& c : cases)
        {
            e.setSpecimen (src.first, src.second);
            Step s { c.op, c.v, 1234 };
            juce::String err;
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            const bool ok = e.applyNow (s, err);
            const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
            const auto name = s.describe() + " on " + src.second;
            check (ok, name + ": ran (" + juce::String (ms / 1000.0, 2) + " s)" + (ok ? juce::String() : " - " + err));
            if (! ok) continue;
            auto out = e.processedAudio();
            check (e.lastCommands.contains (c.program), name + ": really ran CDP '" + c.program + "'");
            const float d = specDistance (*src.first, *out);
            const double lr = out->seconds() / src.first->seconds();
            check (d > 1.5f || std::abs (lr - 1.0) > 0.1, name + ": changed the sound (spectral distance " + juce::String (d, 2) + " dB, length x" + juce::String (lr, 2) + ")");
            checkQuality (*out, name);
            check (out->sampleRate == src.first->sampleRate, name + ": same sample rate");
        }
    }
    // VOICE FUSION with a second specimen differs from the one without
    e.setSpecimen (voice, "voice");
    juce::String err;
    e.applyNow ({ Op::VoiceFusion, {}, 1 }, err);
    auto alone = e.processedAudio();
    e.setPartner (piano, "piano");
    e.setSpecimen (voice, "voice");
    const bool ok = e.applyNow ({ Op::VoiceFusion, {}, 1 }, err);
    check (ok && specDistance (*alone, *e.processedAudio()) > 1.0f, "VOICE FUSION with SPECIMEN 002 uses the second sound (" + juce::String (ok ? specDistance (*alone, *e.processedAudio()) : 0.0f, 2) + " dB from the solo fusion)");
    e.clearPartner();
}

static void testUndo (Engine& e, const std::shared_ptr<Audio>& voice)
{
    e.setSpecimen (voice, "voice");
    std::vector<std::shared_ptr<Audio>> copies { copyOf (*e.processedAudio()) };
    juce::String err;
    for (auto op : { Op::Pull, Op::Smear, Op::Twist, Op::Shatter, Op::Freeze })
    {
        e.applyNow ({ op, {}, 99 }, err);
        copies.push_back (copyOf (*e.processedAudio()));
    }
    check (e.historySize() == 6, "history has every step (6)");
    bool exact = true;
    for (int i = (int) copies.size() - 2; i >= 0; --i)
    {
        e.undo();
        exact = exact && bitExact (*e.processedAudio(), *copies[(size_t) i]);
    }
    check (exact, "UNDO restores every earlier step bit-exact");
    check (bitExact (*e.processedAudio(), *voice), "UNDO all the way gives the original bit-exact");
    check (! e.canUndo(), "nothing left to undo at the original");
    bool redoExact = true;
    for (size_t i = 1; i < copies.size(); ++i) { e.redo(); redoExact = redoExact && bitExact (*e.processedAudio(), *copies[i]); }
    check (redoExact, "REDO brings every step back bit-exact");
    // a new action after undo drops the redo branch
    e.undo(); e.undo();
    e.applyNow ({ Op::Smear, { { "amount", 0.9f } }, 5 }, err);
    check (! e.canRedo() && e.historySize() == 5, "a new action after UNDO replaces the redo steps");
    e.backToOriginal();
    check (bitExact (*e.processedAudio(), *voice) && e.canUndo(), "back to the original is itself undoable");
    // the history cap keeps the original
    e.setSpecimen (voice, "voice");
    for (int i = 0; i < Engine::kMaxHistory + 3; ++i) e.applyNow ({ Op::Smear, { { "amount", 0.1f } }, 1 }, err);
    while (e.undo()) {}
    check (e.historySize() == Engine::kMaxHistory && bitExact (*e.processedAudio(), *voice), "a long history keeps the original (cap " + juce::String (Engine::kMaxHistory) + ")");
}

static void testBreed (Engine& e, const std::shared_ptr<Audio>& voice, const std::shared_ptr<Audio>& piano)
{
    e.setSpecimen (voice, "voice");
    e.setPartner (piano, "piano");
    juce::String err;
    const bool ok = e.breedNow (0.5f, err);
    check (ok, "BREED voice x piano ran" + (ok ? juce::String() : ": " + err));
    if (! ok) return;
    auto child = e.processedAudio();
    check (e.lastCommands.contains ("combine cross") && e.lastCommands.contains ("morph morph"), "BREED really ran combine cross + morph morph");
    const float dv = specDistance (*child, *voice), dp = specDistance (*child, *piano), dvp = specDistance (*voice, *piano);
    check (dv > 2.0f && dp > 2.0f, "the child differs from both parents (" + juce::String (dv, 2) + " dB from the voice, " + juce::String (dp, 2) + " dB from the piano; parents " + juce::String (dvp, 2) + " dB apart)");
    checkQuality (*child, "BREED");
    e.breedNow (0.0f, err); auto leanA = copyOf (*e.processedAudio());
    e.breedNow (1.0f, err); auto leanB = copyOf (*e.processedAudio());
    check (specDistance (*leanA, *leanB) > 0.5f, "'Leans toward' changes the child (" + juce::String (specDistance (*leanA, *leanB), 2) + " dB)");
    // a stereo, different-rate partner still breeds
    e.setPartner (std::make_shared<Audio> (resampled (*stereoOf (*piano, 0.5f), 48000.0)), "piano 48k");
    check (e.breedNow (0.5f, err) && e.processedAudio()->sampleRate == voice->sampleRate, "BREED with a 48 kHz stereo partner works (result at the specimen's rate)");
    e.clearPartner();
    const bool refused = ! e.breedNow (0.5f, err) && err.isNotEmpty();
    check (refused, "BREED without a second specimen says so: \"" + err + "\"");
}

static void testChain (Engine& e, const std::shared_ptr<Audio>& voice)
{
    e.setSpecimen (voice, "voice");
    auto steps = e.chainSteps (77);
    check (steps.size() == 2 && steps[0].startsWith ("GRAIN CLOUD") && steps[1].startsWith ("SPECTRAL MELT"), "default chain = GRAIN CLOUD > SPECTRAL MELT (" + steps.joinIntoString (" > ") + ")");
    check (e.chainSteps (77) == steps && e.chainSteps (78) != steps, "GENERATE MUTATION: the same seed is the same mutation, a new seed a new one");
    juce::String err;
    const bool ok = e.runChainNow (77, err);
    check (ok && specDistance (*voice, *e.processedAudio()) > 1.5f, "GENERATE MUTATION ran the chain and changed the sound");
    if (ok) checkQuality (*e.processedAudio(), "GENERATE MUTATION");
    e.moveSlot (2, 0);
    e.chain[0].on = true;
    check (e.chainSteps (5)[0].startsWith ("TIME WARP") && e.chainSteps (5).size() == 3, "reordering + switching modules changes the chain (" + e.chainSteps (5).joinIntoString (" > ") + ")");
    const auto saved = juce::JSON::toString (e.chainToVar());
    check (e.saveChain ("ghostlab-test-chain", err), "save a favourite chain");
    Engine other;
    check (other.loadChain (Engine::chainsFolder().getChildFile ("ghostlab-test-chain.ghostchain"), err) && juce::JSON::toString (other.chainToVar()) == saved, "load the chain back: identical");
    Engine::chainsFolder().getChildFile ("ghostlab-test-chain.ghostchain").deleteFile();
    for (auto& s : e.chain) s.on = false;
    check (e.chainSteps (5).size() == 1 && e.chainSteps (5)[0].startsWith ("MUTATE"), "an empty chain mutates at random (MUTATE)");
    check (e.runChainNow (5, err), "GENERATE with an empty chain still makes a mutation");
}

static void testAudioThread (const std::shared_ptr<Audio>& voice, const std::shared_ptr<Audio>& piano)
{
    Processor p;
    p.setPlayConfigDetails (0, 2, 48000.0, 256);
    p.prepareToPlay (48000.0, 256);
    p.player.set (Player::Original, voice);
    p.player.set (Player::Processed, piano);
    std::atomic<bool> stop { false };
    std::atomic<int> blocks { 0 };
    float maxOut = 0.0f;
    std::thread audio ([&]
    {
        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer midi;
        midi.ensureSize (256);
        tlAudio = true;
        int k = 0;
        while (! stop.load())
        {
            midi.clear();
            if (k % 40 == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 60 + (k / 40) % 12, 0.8f), 17);
            if (k % 40 == 30) midi.addEvent (juce::MidiMessage::noteOff (1, 60 + (k / 40) % 12), 3);
            p.processBlock (buf, midi);
            maxOut = juce::jmax (maxOut, buf.getMagnitude (0, 256));
            ++k; ++blocks;
        }
        tlAudio = false;
    });
    // the "message thread": A/B switching, new sounds arriving, old ones collected, all while it plays
    for (int i = 0; i < 400; ++i)
    {
        if (i % 3 == 0) p.player.play (i % 2, i % 6 == 0 ? 0.0 : -1.0);
        if (i % 25 == 0) p.player.set (Player::Processed, copyOf (i % 50 == 0 ? *piano : *voice));
        if (i % 7 == 0) p.player.collect();
        if (i % 90 == 0) p.player.stop();
        std::this_thread::sleep_for (std::chrono::milliseconds (2));
    }
    stop = true; audio.join();
    check (blocks.load() > 100, "audio thread ran " + juce::String (blocks.load()) + " blocks while sounds were swapped");
    check (gAudioAllocs.load() == 0, "no memory allocated or freed on the audio thread (" + juce::String (gAudioAllocs.load()) + ")");
    check (gAudioLocks.load() == 0, "no locks taken on the audio thread (" + juce::String (gAudioLocks.load()) + ")");
    check (maxOut > 0.01f && maxOut < 1.5f, "the preview really plays (peak " + juce::String (maxOut, 3) + ")");

    // A/B switch has no click: render a switch and look at the largest sample step
    Processor q; q.setPlayConfigDetails (0, 2, 44100.0, 64); q.prepareToPlay (44100.0, 64);
    auto sine = std::make_shared<Audio>(); sine->sampleRate = 44100; sine->buf.setSize (1, 44100 * 2);
    for (int i = 0; i < 88200; ++i) sine->buf.setSample (0, i, 0.5f * std::sin (6.2831853f * 220.0f * i / 44100.0f));
    auto sine2 = copyOf (*sine); sine2->buf.applyGain (-1.0f);   // the opposite polarity: the worst case for a jump
    q.player.set (0, sine); q.player.set (1, sine2);
    juce::AudioBuffer<float> b (2, 64); juce::MidiBuffer mb;
    float prev = 0, maxStep = 0;
    q.player.play (0, 0.0);
    for (int k = 0; k < 400; ++k)
    {
        if (k == 200) q.player.play (1, -1.0);
        q.processBlock (b, mb);
        for (int i = 0; i < 64; ++i) { const float s = b.getSample (0, i); if (k > 2) maxStep = juce::jmax (maxStep, std::abs (s - prev)); prev = s; }
    }
    const float sineStep = 0.5f * 6.2831853f * 220.0f / 44100.0f;
    check (maxStep <= sineStep * 1.6f, "A/B switch fades (largest step " + juce::String (maxStep, 4) + " vs the sine's own " + juce::String (sineStep, 4) + ")");
}

// ------------------------------------------------------------------------------------------------ TERRAIN
static float toneDb (const Audio& a, float hz)   // the level of one frequency over the whole sound (Goertzel-like sum)
{
    auto m = mono (a);
    double re = 0, im = 0;
    const int n = m.length();
    for (int i = 0; i < n; ++i)
    {
        const double ph = 6.283185307 * hz * i / m.sampleRate, w = 0.5 - 0.5 * std::cos (6.283185307 * i / n);
        re += m.buf.getSample (0, i) * w * std::cos (ph); im += m.buf.getSample (0, i) * w * std::sin (ph);
    }
    return (float) (20.0 * std::log10 (std::sqrt (re * re + im * im) / n + 1e-12));
}

static void testTerrain (const std::shared_ptr<Audio>& voice)
{
    using namespace ghost::terrain;
    Range range;
    // a broadband source so every frequency is there to be shaped
    auto noise = std::make_shared<Audio>(); noise->sampleRate = 44100; noise->buf.setSize (2, 44100 * 3);
    juce::Random rnd (5);
    for (int c = 0; c < 2; ++c) for (int i = 0; i < noise->length(); ++i) noise->buf.setSample (c, i, 0.25f * (rnd.nextFloat() * 2.0f - 1.0f));
    Engine e;
    e.setSpecimen (noise, "noise");
    Terrain t; t.add ({ hzToX (1000.0f, range), 0.5f, 0.85f, 0.025f, 2.0f, 0.0f }); t.add ({ hzToX (4000.0f, range), 0.5f, -0.8f, 0.04f, 2.0f, 0.0f });
    Step st; st.op = Op::Terrain; st.data = toString (t); st.values["strength"] = 1.0f;
    juce::String err;
    check (e.applyNow (st, err), "TERRAIN renders without CDP (" + err + ")");
    const auto& out = *e.processedAudio();
    const float up = (toneDb (out, 1000.0f) - toneDb (*noise, 1000.0f)) - (toneDb (out, 250.0f) - toneDb (*noise, 250.0f));
    const float down = (toneDb (out, 4000.0f) - toneDb (*noise, 4000.0f)) - (toneDb (out, 250.0f) - toneDb (*noise, 250.0f));
    check (up > 8.0f, "a mountain at 1 kHz lifts 1 kHz by " + juce::String (up, 1) + " dB against 250 Hz");
    check (down < -8.0f, "a valley at 4 kHz cuts 4 kHz by " + juce::String (-down, 1) + " dB against 250 Hz");
    check (e.current().label.startsWith ("TERRAIN"), "the history step is called " + e.current().label);
    {
        Engine ev; ev.setSpecimen (voice, "voice");
        check (ev.applyNow (st, err), "TERRAIN renders the voice (" + err + ")");
        checkQuality (*ev.processedAudio(), "TERRAIN on the voice");
        ev.sessionFolder().deleteRecursively();
    }
    // the same landscape renders the same sound (sessions re-open identical)
    auto first = e.processedAudio();
    e.undo(); e.applyNow (st, err);
    check (bitExact (*first, *e.processedAudio()), "the same landscape gives a bit-exact render");
    // re-renders replace the last step instead of piling up
    Terrain t2 = t; t2[0].height = 0.4f;
    Step st2 = st; st2.data = toString (t2);
    const int before = e.historySize();
    e.undo();
    e.applyNow (st, err);
    bool replaced = false;
    e.applyReplacingLast (st2, [&] (bool ok, const juce::String&) { replaced = ok; });
    for (int i = 0; i < 400 && e.busy(); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    check (replaced && e.historySize() == before && ! bitExact (*first, *e.processedAudio()), "changing the landscape re-renders in place (history stays " + juce::String (e.historySize()) + " steps)");
    check (bitExact (*e.audioUnderLast(), *noise), "the landscape is always applied to the sound underneath it, never to itself");
    // a flat landscape leaves the sound as it was (only the loudness match touches it)
    Step flat = st; flat.data = toString (flatTerrain());
    e.undo(); e.applyNow (flat, err);
    check (specDistance (*e.processedAudio(), *noise) < 0.5f, "flat ground changes nothing (spectral distance " + juce::String (specDistance (*e.processedAudio(), *noise), 2) + " dB)");
    // the terrain is saved with the plug-in state
    juce::MemoryBlock mb;
    {
        Processor p;
        p.terrain.set (t);
        p.terrain.toggleAB(); p.terrain.set (t2); p.terrain.toggleAB();
        p.refreshState();
        p.getStateInformation (mb);
    }
    Processor q;
    q.setStateInformation (mb.getData(), (int) mb.getSize());
    for (int i = 0; i < 20; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
    check (q.terrain.terrain() == t && q.terrain.slot (1) == t2 && q.terrain.activeSlot() == 0, "the landscape (both A and B) re-opens with the project");
    // LISTEN: the live bank on the audio thread, no allocation, and it really shapes the sound
    {
        Processor p;
        p.setPlayConfigDetails (0, 2, 48000.0, 256);
        p.prepareToPlay (48000.0, 256);
        p.engine.setSpecimen (noise, "noise");
        p.syncPlayer();
        p.terrain.set (t);
        juce::AudioBuffer<float> buf (2, 256); juce::MidiBuffer midi;
        auto run = [&] (int blocks)
        {
            Audio rec; rec.sampleRate = 48000.0; rec.buf.setSize (1, juce::jmax (1, (blocks - 40) * 256)); rec.buf.clear();
            for (int k = 0; k < blocks; ++k) { buf.clear(); p.processBlock (buf, midi); if (k >= 40) rec.buf.copyFrom (0, (k - 40) * 256, buf, 0, 0, 256); }
            return rec;
        };
        p.player.play (Player::Processed, 0.0);
        const Audio plain = run (240);
        p.player.stop(); run (50);
        p.setTerrainListening (true);
        gAudioAllocs = 0; gAudioLocks = 0; tlAudio = true;
        const Audio shapedRec = run (240);
        const double plainLift = toneDb (plain, 1000.0f) - toneDb (plain, 250.0f), shapedLift = toneDb (shapedRec, 1000.0f) - toneDb (shapedRec, 250.0f);
        const double shaped = shapedLift - plainLift;
        // drag while it plays: the share hands the new landscapes over without locks
        // (the drags happen on the message thread: only the audio blocks count)
        tlAudio = false;
        for (int k = 0; k < 60; ++k) { Terrain d = t; d[0].x = 0.3f + 0.4f * (float) k / 60.0f; p.terrain.setWithoutUndo (d); buf.clear(); tlAudio = true; p.processBlock (buf, midi); tlAudio = false; }
        check (gAudioAllocs.load() == 0 && gAudioLocks.load() == 0, "LISTEN: nothing allocated or locked on the audio thread while dragging (" + juce::String (gAudioAllocs.load()) + ", " + juce::String (gAudioLocks.load()) + ")");
        check (p.terrainListening() && shaped > 6.0, "LISTEN plays the sound through the landscape (1 kHz up " + juce::String (shaped, 1) + " dB against plain playback)");
        p.setTerrainListening (false); run (10);
        check (! p.terrainListening(), "LISTEN stops");
        p.engine.sessionFolder().deleteRecursively();
    }
    // the page in the editor: open it, randomize, the render lands as one history step, undo brings the land back
    {
        Processor p;
        p.engine.setSpecimen (voice, "voice");
        p.syncPlayer();
        std::unique_ptr<Editor> ed (dynamic_cast<Editor*> (p.createEditor()));
        ed->setSize (Editor::kW, Editor::kH);
        ed->setTerrainPage (true);
        check (ed->terrainView().isVisible() && ed->terrainView().getWidth() > 600, "the TERRAIN page shows the landscape (" + juce::String (ed->terrainView().getWidth()) + " px wide)");
        const auto land0 = p.terrain.terrain();
        ed->terrainView().press (TerrainView::Button::randomize);
        for (int i = 0; i < 600 && (p.engine.busy() || p.engine.historyCursor() == 0); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        check (p.engine.historyCursor() == 1 && p.engine.current().label.startsWith ("TERRAIN"), "RANDOMIZE renders the new landscape onto the sound as one step (" + p.engine.current().label + ")");
        ed->terrainView().press (TerrainView::Button::randomize);
        for (int i = 0; i < 600 && p.engine.busy(); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        for (int i = 0; i < 600 && p.engine.busy(); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        check (p.engine.historySize() == 2, "another change re-renders the same step (history " + juce::String (p.engine.historySize()) + ")");
        ed->undo();
        for (int i = 0; i < 600 && p.engine.busy(); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        check (p.terrain.canRedo() && p.terrain.terrain() != land0, "UNDO on the page undoes the landscape");
        ed->setTerrainPage (false);
        check (! ed->terrainView().isVisible(), "LAB brings the cards back");
        ed.reset();
        p.engine.sessionFolder().deleteRecursively();
    }
    e.sessionFolder().deleteRecursively();
    q.engine.sessionFolder().deleteRecursively();
}


// ------------------------------------------------------------------------------------------------ UNDER THE HOOD

// ------------------------------------------------------------------------------------------------ the rooms (build 12)
static void testRooms (const std::shared_ptr<Audio>& voice)
{
    std::printf ("\n== the rooms\n");
    using namespace glab::rooms;
    // GROW: every body of every source makes a real, clean sound at the engine's own defaults
    for (int src = 0; src < growSources().size(); ++src)
        for (int body = 0; body < growBodies (src).size(); ++body)
        {
            Step s; s.op = Op::Grow; s.values["source"] = (float) src; s.values["body"] = (float) body; s.values["seconds"] = 2.0f;
            juce::String err;
            auto a = grow (s, err);
            const auto name = "GROW " + growSources()[src] + " / " + growBodies (src)[body];
            check (a != nullptr, name + " makes a sound" + (err.isNotEmpty() ? " (" + err + ")" : juce::String()));
            if (a == nullptr) continue;
            checkQuality (*a, name);
            std::printf ("      %-28s %.1f s  rms %.1f dB  peak %.1f dBTP\n", name.toRawUTF8(), a->seconds(), juce::Decibels::gainToDecibels (rms (*a)), truePeakDb (*a));
        }
    // STRETCH: the length is what was asked for, the level is held, and nothing clips
    {
        Step s; s.op = Op::XStretch; s.values["pxs.stretchamount0"] = 8.0f;
        auto r = glab::rooms::render (s, *voice);
        check (r.audio != nullptr, "STRETCH 8x renders" + (r.error.isNotEmpty() ? " (" + r.error + ")" : juce::String()));
        if (r.audio != nullptr)
        {
            checkQuality (*r.audio, "STRETCH 8x");
            const double ratio = r.audio->seconds() / voice->seconds();
            check (ratio > 6.5 && ratio < 9.5, "STRETCH 8x is about 8 times as long (" + juce::String (ratio, 2) + "x)");
            check (std::abs (r.audio->sampleRate - voice->sampleRate) < 1.0, "STRETCH keeps the sound's sample rate");
        }
        Step off; off.op = Op::XStretch; off.values["pxs.stretchamount0"] = 1.0f;
        auto r1 = glab::rooms::render (off, *voice);
        check (r1.audio != nullptr && std::abs (r1.audio->seconds() / voice->seconds() - 1.0) < 0.25, "Stretch amount 1x keeps the length (the spectral modules still work on it, as in the original)");
    }
    // the pages' controls are the originals' own: names, defaults, and the module switches really switch the module
    {
        check (control (Room::Stretch, "pxs.fftsize0") != nullptr && control (Room::Stretch, "pxs.fftsize0")->name == "FFT size"
               && std::abs (control (Room::Stretch, "pxs.fftsize0")->def - 0.7f) < 1e-6f, "STRETCH's controls carry the original's names and defaults (FFT size, 0.7)");
        check (control (Room::Reborn, "ddsp.HarmonicGain") != nullptr && control (Room::Reborn, "ddsp.HarmonicGain")->name == "Harmonics"
               && control (Room::Space, "panner.er_level") != nullptr && control (Room::Space, "panner.er_level")->name == "ER Level"
               && control (Room::Space, "fdn.decay") != nullptr, "REBORN's and SPACE's controls carry the originals' names");
        int plain = 0, all = 0;
        for (auto r : { Room::Stretch, Room::Reborn, Room::Space }) for (auto& c : controls (r)) { ++all; if (c.plain.isNotEmpty()) ++plain; }
        check (plain == all, "every control has its line in plain words (" + juce::String (plain) + " of " + juce::String (all) + ")");
        Step a; a.op = Op::XStretch; a.values["pxs.stretchamount0"] = 4.0f; a.values["pxs.octavemix1_0"] = 1.0f; a.values["pxs.octavemix0_0"] = 0.0f;
        Step b = a; b.values[moduleKey (4)] = 1.0f;   // Ratios on: the octave-up copy is heard
        auto ra = glab::rooms::render (a, *voice), rb = glab::rooms::render (b, *voice);
        check (ra.audio != nullptr && rb.audio != nullptr && specDistance (*ra.audio, *rb.audio) > 2.0f,
               "STRETCH: the Ratios levels do nothing until the Ratios module is switched on, as in the original");
    }
    // SPACE: left is louder in the left ear, right in the right; the sound rings out past its end
    {
        auto side = [&] (float x)
        {
            Step s; s.op = Op::Space; s.values["panner.x"] = x * 2.0f - 1.0f; s.values["fdn.mix"] = 0.0f;
            auto r = glab::rooms::render (s, *voice);
            if (r.audio == nullptr) return 0.0f;
            checkQuality (*r.audio, "SPACE x " + juce::String (x, 1));
            return juce::Decibels::gainToDecibels (r.audio->buf.getRMSLevel (0, 0, r.audio->length()) + 1e-9f)
                 - juce::Decibels::gainToDecibels (r.audio->buf.getRMSLevel (1, 0, r.audio->length()) + 1e-9f);
        };
        const float l = side (0.0f), c = side (0.5f), r = side (1.0f);
        check (l > 1.5f, "SPACE far left: the left ear is louder (" + juce::String (l, 1) + " dB)");
        check (r < -1.5f, "SPACE far right: the right ear is louder (" + juce::String (r, 1) + " dB)");
        check (std::abs (c) < 1.5f, "SPACE middle: both ears about the same (" + juce::String (c, 1) + " dB)");
        Step t; t.op = Op::Space; t.values["fdn.decay"] = 0.9f; t.values["fdn.mix"] = 0.6f;
        auto rt = glab::rooms::render (t, *voice);
        check (rt.audio != nullptr && rt.audio->seconds() > voice->seconds() + 0.5, "SPACE with a long tail rings out past the end of the sound");
    }
    // REBORN: only where this build has the neural instruments (the Mac build does)
    if (rebornAvailable())
    {
        const auto names = rebornInstruments();
        check (names.size() >= 11, "REBORN has its instruments (" + juce::String (names.size()) + ")");
        Step s; s.op = Op::Reborn; s.values["instrument"] = (float) juce::jmax (0, names.indexOf ("Violin"));
        const auto mel = melody (*voice);
        s.values["ddsp.PitchShift"] = (float) autoShift (mel, instrumentRange (names[(int) s.values["instrument"]]));
        auto r = glab::rooms::render (s, *voice);
        check (r.audio != nullptr, "REBORN renders" + (r.error.isNotEmpty() ? " (" + r.error + ")" : juce::String()));
        if (r.audio != nullptr)
        {
            checkQuality (*r.audio, "REBORN");
            check (std::abs (r.audio->seconds() - voice->seconds()) < 0.2, "REBORN keeps the sound's length");
            check (specDistance (*r.audio, *voice) > 3.0f, "REBORN changes the sound (it is the instrument now)");
        }
    }
    else std::printf ("      (REBORN: no neural instruments in this build, skipped)\n");
    // the pictures
    {
        const auto mel = melody (*voice);
        check (mel.voiced > 0.3f && mel.median > 30.0f && mel.median < 90.0f, "the melody finder hears the test voice's notes (median " + juce::String (mel.median, 1)
                                                                              + ", " + juce::String (juce::roundToInt (mel.voiced * 100)) + "% clear)");
        check (std::abs (autoShift (mel, { 60.0f, 96.0f })) % 12 == 0, "AUTO moves the tune by whole octaves");
        const auto im = spectrogram (*voice, 200, 64, juce::Colours::purple, juce::Colours::green);
        int lit = 0; for (int x = 0; x < im.getWidth(); x += 4) for (int y = 0; y < im.getHeight(); y += 2) if (im.getPixelAt (x, y).getAlpha() > 40) ++lit;
        check (lit > 50, "the sound picture has the sound in it");
    }
    // in the lab: each room's run is one step in the history, undo takes it back bit-exact; GROW starts a new specimen
    {
        Engine e;
        juce::String err;
        Step g; g.op = Op::Grow; g.values["source"] = 0.0f; g.values["body"] = 6.0f; g.values["seconds"] = 2.0f;
        check (e.growNow (g, false, err) && e.hasSpecimen() && e.specimenName == "GROW BELL", "GROW in an empty lab makes SPECIMEN 001 (" + e.specimenName + ")");
        auto before = e.processedAudio();
        Step s; s.op = Op::XStretch; s.values["pxs.stretchamount0"] = 4.0f;
        check (e.applyNow (s, err) && e.historyCursor() == 1 && e.current().label == "STRETCH 4.0x", "a room's run is one history step (" + e.current().label + ")");
        check (e.undo() && bitExact (*e.processedAudio(), *before), "undo after a room is bit-exact");
        check (e.growNow (g, true, err) && e.hasPartner(), "GROW can make SPECIMEN 002 too");
    }
}

static void waitIdle (Engine& e)
{
    for (int i = 0; i < 6000 && e.busy(); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
}

// the same CDP programs run by hand (no Ghost Lab code in between), for the bit-exact comparison
// the same, keeping the job folder so a data file the programs wrote can be compared the same data (the caller deletes it)
static juce::File byHandDir (const Cdp& cdp, const Audio& in, const std::vector<juce::StringArray>& steps,
                             const std::map<juce::String, juce::String>& extraFiles = {}, const std::map<juce::String, std::shared_ptr<Audio>>& extraAudio = {})
{
    auto dir = cdp.newJobFolder();
    juce::String err;
    writeWav (in, dir.getChildFile ("in.wav"), 32, err);
    for (auto& kv : extraFiles) dir.getChildFile (kv.first).replaceWithText (kv.second + "\n", false, false, "\n");
    for (auto& kv : extraAudio) writeWav (*kv.second, dir.getChildFile (kv.first), 32, err);
    for (auto& s : steps)
    {
        const auto r = cdp.run (s, dir, juce::File());
        if (! r.ok) { std::printf ("by hand failed: %s %s\n", s.joinIntoString (" ").toRawUTF8(), r.error.toRawUTF8()); dir.deleteRecursively(); return {}; }
    }
    return dir;
}
// the same data: CDP's data files are RIFF files whose header holds a DATE stamp (seconds), so the comparison is the data chunk
// (every value the program computed) plus the file size
static bool dataChunk (const juce::MemoryBlock& m, size_t& start, size_t& len)
{
    const auto* d = static_cast<const char*> (m.getData());
    if (m.getSize() < 12 || std::memcmp (d, "RIFF", 4) != 0) { start = 0; len = m.getSize(); return len > 0; }
    size_t pos = 12;
    while (pos + 8 <= m.getSize())
    {
        juce::uint32 sz; std::memcpy (&sz, d + pos + 4, 4);
        if (std::memcmp (d + pos, "data", 4) == 0) { start = pos + 8; len = juce::jmin ((size_t) sz, m.getSize() - start); return len > 0; }
        pos += 8 + sz + (sz & 1);
    }
    return false;
}
static bool sameBytes (const juce::File& a, const juce::File& b)
{
    juce::MemoryBlock ma, mb;
    size_t sa, la, sb, lb;
    return a.loadFileAsData (ma) && b.loadFileAsData (mb) && ma.getSize() == mb.getSize() && dataChunk (ma, sa, la) && dataChunk (mb, sb, lb)
        && la == lb && std::memcmp (static_cast<const char*> (ma.getData()) + sa, static_cast<const char*> (mb.getData()) + sb, la) == 0;
}

static std::shared_ptr<Audio> byHand (const Cdp& cdp, const Audio& in, const std::vector<juce::StringArray>& steps, const juce::String& finalName,
                                      const std::map<juce::String, juce::String>& extraFiles = {}, const std::map<juce::String, std::shared_ptr<Audio>>& extraAudio = {})
{
    auto dir = cdp.newJobFolder();
    juce::String err;
    writeWav (in, dir.getChildFile ("in.wav"), 32, err);
    for (auto& kv : extraFiles) dir.getChildFile (kv.first).replaceWithText (kv.second + "\n", false, false, "\n");
    for (auto& kv : extraAudio) writeWav (*kv.second, dir.getChildFile (kv.first), 32, err);
    for (size_t i = 0; i < steps.size(); ++i)
    {
        const auto& s = steps[i];
        const auto r = cdp.run (s, dir, i + 1 == steps.size() ? dir.getChildFile (finalName) : juce::File());
        if (! r.ok) { std::printf ("by hand failed: %s %s\n", s.joinIntoString (" ").toRawUTF8(), r.error.toRawUTF8()); dir.deleteRecursively(); return nullptr; }
    }
    auto a = readWav (dir.getChildFile (finalName), err);
    dir.deleteRecursively();
    return a;
}

static void testHood (Engine& e, const std::shared_ptr<Audio>& voice, const std::shared_ptr<Audio>& piano)
{
    using namespace hood;
    const auto& cat = catalog();
    int forms = 0, params = 0, modes = 0, texts = 0;
    for (auto& p : cat) { forms += (int) p.forms.size(); texts += p.usage.isNotEmpty() ? 1 : 0; modes += (int) modesOf (p).size(); for (auto& f : p.forms) params += (int) f.params.size(); }
    std::printf ("UNDER THE HOOD coverage: %d processes, %d usage forms, %d modes, %d parameters\n", (int) cat.size(), forms, modes, params);
    check (cat.size() == 54 && texts == 54, "UNDER THE HOOD lists all 50 processes of the 8 CDP programs Ghost Lab ships + the 4 data-file processes of repitch / formants / envel (" + juce::String ((int) cat.size()) + ")");

    // 1. the catalogue is what the shipped programs print: usage text verbatim, every parameter named in its usage line
    int sameText = 0, namedOk = 0, namedAll = 0;
    for (auto& p : cat)
    {
        juce::ChildProcess cp;
        auto exe = e.cdp->programsDir().getChildFile (p.prog).getFullPathName();
        if (cp.start (juce::StringArray { exe, p.name }))
        {
            auto out = cp.readAllProcessOutput().replace ("\r", "");
            if (out.trim() == p.usage.trim()) ++sameText; else std::printf ("usage text differs: %s\n", p.id().toRawUTF8());
        }
        for (auto& f : p.forms)
            for (auto& q : f.params)
            {
                ++namedAll;
                auto key = q.kind == "flagvalue" ? q.flag + q.name : q.name;
                if (f.usage.contains (key) || (q.kind == "flagvalue" && f.usage.contains (q.flag))) ++namedOk;
            }
    }
    check (sameText == (int) cat.size(), "every process's usage text is exactly what the CDP program prints (" + juce::String (sameText) + " of " + juce::String ((int) cat.size()) + ")");
    check (namedOk == namedAll, "every parameter shown is named in CDP's own usage line (" + juce::String (namedOk) + " of " + juce::String (namedAll) + ")");

    // 2. the command words follow CDP's order and flags
    {
        Request r; r.prog = "modify"; r.name = "brassage"; r.mode = 6;
        r.values = { { "velocity", "1" }, { "density", "0.8" }, { "grainsize", "60" }, { "pitchshft", "2" }, { "amp", "1" }, { "space", "0.5" },
                     { "bsplice", "5" }, { "esplice", "5" }, { "range", "10" }, { "-x", "1" } };
        juce::StringArray w; juce::String err;
        const auto* pr = find ("modify", "brassage");
        check (buildWords (*pr, *formFor (*pr, 6), r, { "a.wav" }, "b.wav", [] (const juce::String&) { return juce::String ("x.txt"); }, w, err)
                   && w.joinIntoString (" ") == "modify brassage 6 a.wav b.wav 1 0.8 60 2 1 0.5 5 5 -r10 -x",
               "modify brassage 6 gets CDP's own words: " + w.joinIntoString (" "));
        r.values.erase ("esplice");
        check (! buildWords (*pr, *formFor (*pr, 6), r, { "a.wav" }, "b.wav", [] (const juce::String&) { return juce::String(); }, w, err) && err.contains ("ESPLICE"), "a missing value is named: " + err);
        const auto* pc = find ("modify", "brassage");
        check (pc != nullptr && modesOf (*pc).size() == 7 && formFor (*pc, 7)->params.size() == 22, "brassage has its 7 modes and mode 7's 22 parameters");
    }

    // 3. running a process is running the real program: bit-exact against the same commands typed by hand
    {
        e.setSpecimen (voice, "voice");
        Request r; r.prog = "stretch"; r.name = "time"; r.mode = 1; r.values["timestretch"] = "2";
        Step s; s.op = Op::Hood; s.data = r.toData();
        juce::String err;
        const bool ok = e.applyNow (s, err);
        auto hand = byHand (*e.cdp, *voice, { { "pvoc", "anal", "1", "in.wav", "a.ana", "-c1024", "-o3" }, { "stretch", "time", "1", "a.ana", "b.ana", "2" }, { "pvoc", "synth", "b.ana", "c.wav" } }, "c.wav");
        check (ok && hand != nullptr && bitExact (*e.processedAudio(), *hand), "stretch time 1 x2 from the page == the same CDP commands by hand (bit-exact)" + (ok ? juce::String() : " " + err));
        check (e.current().commands.contains ("stretch time 1") && e.current().commands.contains ("pvoc synth"), "the command lines that ran are kept for the page");
        check (e.undo() && bitExact (*e.processedAudio(), *voice), "UNDO restores the sound bit-exact");

        Request r2; r2.prog = "modify"; r2.name = "speed"; r2.mode = 2; r2.values["semitone-transpos"] = "5";
        Step s2; s2.op = Op::Hood; s2.data = r2.toData();
        e.setSpecimen (voice, "voice");
        const bool ok2 = e.applyNow (s2, err);
        auto hand2 = byHand (*e.cdp, *voice, { { "modify", "speed", "2", "in.wav", "o.wav", "5" } }, "o.wav");
        check (ok2 && hand2 != nullptr && bitExact (*e.processedAudio(), *hand2), "modify speed 2 (+5 semitones) == the same CDP command by hand (bit-exact)");

        // two sounds: SPECIMEN 002 is the second input
        e.setSpecimen (voice, "voice"); e.setPartner (piano, "piano");
        Request r3; r3.prog = "combine"; r3.name = "cross"; r3.mode = 0;
        Step s3; s3.op = Op::Hood; s3.data = r3.toData();
        check (e.applyNow (s3, err) && e.current().commands.contains ("combine cross"), "combine cross takes SPECIMEN 002 as its second sound" + (err.isEmpty() ? juce::String() : " " + err));
        e.clearPartner();
        e.setSpecimen (voice, "voice");
        check (! e.applyNow (s3, err) && err.contains ("second sound"), "a two-sound process without SPECIMEN 002 says so: " + err);

        // a text answer leaves the sound as it is and shows CDP's answer
        Request r4; r4.prog = "stretch"; r4.name = "time"; r4.mode = 2; r4.values["timestretch"] = "2";
        Step s4; s4.op = Op::Hood; s4.data = r4.toData();
        check (e.applyNow (s4, err) && bitExact (*e.processedAudio(), *voice) && e.current().commands.contains ("#"), "stretch time 2 (a calculation) keeps the sound and shows CDP's answer");

        // CDP's own refusal comes through in CDP's words
        Request r5; r5.prog = "stretch"; r5.name = "time"; r5.mode = 1; r5.values["timestretch"] = "-5";
        Step s5; s5.op = Op::Hood; s5.data = r5.toData();
        check (! e.applyNow (s5, err) && err.isNotEmpty(), "a value CDP refuses is reported: " + err);
    }

    // 4. every process runs with sensible values (the shipped programs, the voice as the specimen, the piano as SPECIMEN 002)
    {
        struct T { const char* prog; const char* name; int mode; std::map<juce::String, juce::String> v; };
        const std::vector<T> table {
            { "pvoc", "anal", 1, {} }, { "pvoc", "synth", 0, {} }, { "pvoc", "extract", 0, {} },
            { "stretch", "spectrum", 1, { { "frq_divide", "500" }, { "maxstretch", "1.5" }, { "exponent", "1" } } },
            { "stretch", "spectrum", 2, { { "frq_divide", "2000" }, { "maxstretch", "0.7" }, { "exponent", "1" }, { "depth", "0.8" } } },
            { "stretch", "time", 1, { { "timestretch", "1.5" } } }, { "stretch", "time", 2, { { "timestretch", "2" } } },
            { "strange", "shift", 1, { { "frqshift", "60" } } }, { "strange", "shift", 2, { { "frqshift", "60" }, { "frq_divide", "500" } } },
            { "strange", "shift", 4, { { "frqshift", "60" }, { "frqlo", "300" }, { "frqhi", "3000" } } },
            { "strange", "glis", 1, { { "-fN|-pN", "-f4" }, { "glisrate", "1" } } },
            { "strange", "glis", 2, { { "-fN|-pN", "-f4" }, { "glisrate", "1" }, { "hzstep", "100" } } },
            { "strange", "glis", 3, { { "-fN|-pN", "-f4" }, { "glisrate", "1" } } },
            { "strange", "waver", 1, { { "vibfrq", "5" }, { "stretch", "1.2" }, { "botfrq", "200" } } },
            { "strange", "waver", 2, { { "vibfrq", "5" }, { "stretch", "1.2" }, { "botfrq", "200" }, { "expon", "1" } } },
            { "strange", "invert", 1, {} }, { "strange", "invert", 2, {} },
            { "focus", "accu", 0, {} }, { "focus", "exag", 0, { { "exaggeration", "2" } } },
            { "focus", "focus", 0, { { "-fN|-pN", "-p4" }, { "pk", "6" }, { "bw", "0.5" } } },
            { "focus", "fold", 0, { { "lofrq", "200" }, { "hifrq", "2000" } } },
            { "focus", "freeze", 1, { { "datafile", "a0.5 1.5" } } }, { "focus", "freeze", 3, { { "datafile", "a0.5 1.5" } } },
            { "focus", "hold", 0, { { "datafile", "0.5 1.5" } } }, { "focus", "step", 0, { { "timestep", "0.2" } } },
            { "blur", "avrg", 0, { { "N", "3" } } }, { "blur", "blur", 0, { { "blurring", "5" } } }, { "blur", "suppress", 0, { { "N", "2" } } },
            { "blur", "chorus", 1, { { "aspread", "1" } } }, { "blur", "chorus", 2, { { "fspread", "1" } } },
            { "blur", "chorus", 5, { { "aspread", "1" }, { "fspread", "1" } } },
            { "blur", "drunk", 0, { { "range", "2" }, { "starttime", "0.5" }, { "duration", "2" } } },
            { "blur", "shuffle", 0, { { "domain-image", "abc-abbabcc" }, { "grpsize", "3" } } },
            { "blur", "noise", 0, { { "noise", "0.3" } } }, { "blur", "scatter", 0, { { "keep", "2" } } },
            { "blur", "spread", 0, { { "-fN|-pN", "-f4" }, { "spread", "0.5" } } },
            { "modify", "loudness", 1, { { "gain", "0.5" } } }, { "modify", "loudness", 2, { { "gain", "-6" } } },
            { "modify", "loudness", 3, {} }, { "modify", "loudness", 4, { { "level", "0.3" } } },
            { "modify", "loudness", 5, {} }, { "modify", "loudness", 6, {} }, { "modify", "loudness", 7, {} }, { "modify", "loudness", 8, {} },
            { "modify", "speed", 1, { { "speed", "1.5" } } }, { "modify", "speed", 2, { { "semitone-transpos", "5" } } },
            { "modify", "speed", 3, { { "speed", "0 1 2 1.5" } } }, { "modify", "speed", 4, { { "semitone-transpos", "0 5 2 7" } } },
            { "modify", "speed", 5, { { "accel", "2" }, { "goaltime", "2" } } }, { "modify", "speed", 6, { { "vibrate", "5" }, { "vibdepth", "0.5" } } },
            { "modify", "revecho", 1, { { "delay", "0.2" }, { "mix", "0.5" }, { "feedback", "0.4" }, { "tail", "1" } } },
            { "modify", "revecho", 3, {} },
            { "modify", "brassage", 1, { { "pitchshift", "3" } } }, { "modify", "brassage", 2, { { "velocity", "0.5" } } },
            { "modify", "brassage", 3, { { "density", "4" }, { "pitch", "0" }, { "amp", "1" } } },
            { "modify", "brassage", 4, { { "grainsize", "50" } } }, { "modify", "brassage", 5, { { "density", "1" } } },
            { "modify", "brassage", 6, { { "velocity", "1" }, { "density", "1" }, { "grainsize", "50" }, { "pitchshft", "0" }, { "amp", "1" }, { "space", "0.5" }, { "bsplice", "5" }, { "esplice", "5" } } },
            { "modify", "brassage", 7, { { "velocity", "1" }, { "density", "1" }, { "hvelocity", "1.2" }, { "hdensity", "1.2" }, { "grainsize", "50" }, { "pitchshift", "0" }, { "amp", "1" }, { "space", "0.5" },
                                         { "bsplice", "5" }, { "esplice", "5" }, { "hgrainsize", "60" }, { "hpitchshift", "1" }, { "hamp", "1" }, { "hspace", "0.6" }, { "hbsplice", "5" }, { "hesplice", "5" } } },
            { "modify", "radical", 1, {} }, { "modify", "radical", 2, { { "repeats", "3" }, { "chunklen", "0.2" } } },
            { "modify", "radical", 3, { { "dur", "2" } } },
            { "modify", "radical", 4, { { "bit_resolution", "8" }, { "srate_division", "2" } } },
            { "modify", "radical", 5, { { "modulating-frq", "100" } } }, { "modify", "radical", 6, {} }, { "modify", "radical", 7, { { "bit_resolution", "8" } } },
            { "modify", "scaledpan", 0, { { "pan", "0 0.2 2 0.8" } } }, { "modify", "convolve", 1, {} },
            { "combine", "sum", 0, {} }, { "combine", "diff", 0, {} }, { "combine", "interleave", 0, { { "leafsize", "2" } } }, { "combine", "max", 0, {} },
            { "combine", "mean", 1, {} }, { "combine", "cross", 0, {} },
            { "blur", "weave", 0, { { "weavfile", "2 1 -1 2" } } },
            { "modify", "space", 1, { { "pan", "0.3" } } },
            { "modify", "stack", 0, { { "transpos", "7" }, { "count", "3" }, { "lean", "0.5" }, { "atk-offset", "0.1" }, { "gain", "1" }, { "dur", "1" } } },
            { "modify", "spaceform", 0, { { "cyclelen", "1" }, { "width", "1" }, { "dur", "3" }, { "quantisation", "0.1" }, { "phase", "0" } } },
            { "morph", "glide", 0, { { "duration", "2" } } }, { "morph", "bridge", 1, {} },
            { "morph", "morph", 1, { { "as", "0" }, { "ae", "2" }, { "fs", "0" }, { "fe", "2" }, { "expa", "1" }, { "expf", "1" } } } };
        int ran = 0; std::set<juce::String> procsRan;
        for (auto& t : table)
        {
            Request r; r.prog = t.prog; r.name = t.name; r.mode = t.mode; r.values = t.v;
            Step s; s.op = Op::Hood; s.data = r.toData();
            e.setSpecimen (voice, "voice"); e.setPartner (piano, "piano");
            juce::String err;
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            const bool ok = e.applyNow (s, err);
            const auto* out = e.processedAudio().get();
            // two programs write an empty sound on the Linux test build of CDP; the page must still run them and show CDP's answer
            const bool quirk = (juce::String (t.prog) == "pvoc" && juce::String (t.name) == "extract") || (juce::String (t.name) == "convolve");
            const bool good = quirk ? (ok || err.isNotEmpty()) && e.lastCommands.contains (t.name)
                                    : ok && out != nullptr && out->length() > 100 && std::isfinite (rms (*out));
            check (good, juce::String (t.prog) + " " + t.name + (t.mode ? " " + juce::String (t.mode) : juce::String()) + ": ran ("
                              + juce::String ((juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0, 2) + " s)" + (good ? juce::String() : " - " + err));
            if (good) { ++ran; procsRan.insert (juce::String (t.prog) + " " + t.name); }
        }
        // the stereo-only processes, on the piano made stereo
        {
            struct S { const char* name; int mode; std::map<juce::String, juce::String> v; };
            const std::vector<S> st { { "space", 2, {} }, { "space", 4, { { "narrowing", "0.5" } } },
                                      { "findpan", 0, { { "time", "1" } } },
                                      { "shudder", 0, { { "starttime", "0" }, { "frq", "4" }, { "scatter", "0.1" }, { "stereo_spread", "0.5" }, { "mindepth", "0.2" }, { "maxdepth", "0.8" }, { "minwidth", "0.05" }, { "maxwidth", "0.1" } } } };
            auto stereo = stereoOf (*piano, 0.8f);
            for (auto& t : st)
            {
                Request r; r.prog = "modify"; r.name = t.name; r.mode = t.mode; r.values = t.v;
                Step s; s.op = Op::Hood; s.data = r.toData();
                e.setSpecimen (stereo, "piano (stereo)");
                juce::String err;
                const bool ok = e.applyNow (s, err);
                check (ok && e.processedAudio()->channels() == 2, juce::String ("modify ") + t.name + (t.mode ? " " + juce::String (t.mode) : juce::String()) + " on a stereo sound: ran" + (ok ? juce::String() : " - " + err));
                if (ok) procsRan.insert (juce::String ("modify ") + t.name);
            }
        }
        // these need things Ghost Lab can't hand them (CDP pitch / formant analysis files, a pan data file, several files): the page says what is missing
        {
            Request r; r.prog = "combine"; r.name = "make";
            Step s; s.op = Op::Hood; s.data = r.toData();
            juce::String err;
            e.setSpecimen (voice, "voice");
            check (! e.applyNow (s, err) && err.contains ("PITCHFILE"), "combine make without its data files says it needs a PITCHFILE: " + err);
        }
        for (auto& p : catalog()) if (procsRan.count (p.id()) == 0) std::printf ("not run in the test: %s\n", p.id().toRawUTF8());
        std::printf ("UNDER THE HOOD ran %d forms covering %d of %d processes\n", ran, (int) procsRan.size(), (int) catalog().size());
        e.clearPartner();
    }
}

// ------------------------------------------------------------------------------------------------ the ORIGINAL pages
// the baked pages (HoodDialogs.inc) are exactly what CDP's own cdparams prints now, for every process and mode
static void testHoodDialogs (Engine& e)
{
    using namespace hood;
    const auto& ds = dialogs();
    int expected = 0;
    for (auto& p : catalog()) { const auto m = modesOf (p); expected += m.empty() ? 1 : (int) m.size(); }
    // modify loudness lists modes 11 and 12 after 8: CDP's own table has 12 modes but no process for 9 and 10, so those two are not pages
    check ((int) ds.size() == expected && expected == 121, "there is an ORIGINAL page for every process and mode: " + juce::String ((int) ds.size()) + " (expected " + juce::String (expected) + ")");
    int problems = 0, notes = 0, controls = 0;
    for (auto& d : ds) { problems += d.problems.size(); notes += d.notes.size(); controls += (int) d.items.size(); }
    check (problems == 0, "every page's controls map onto CDP's command words (" + juce::String (problems) + " problems)");
    std::printf ("ORIGINAL pages: %d pages, %d controls, %d notes (CDP's table and usage text disagree: the table decides)\n", (int) ds.size(), controls, notes);

    const auto cdparams = e.cdp->programsDir().getChildFile ("cdparams");
    check (cdparams.existsAsFile(), "CDP's own cdparams program is shipped beside the others (" + cdparams.getFullPathName() + ")");
    if (! cdparams.existsAsFile()) return;
    int same = 0, sameText = 0;
    juce::StringArray differs;
    for (auto& d : ds)
    {
        auto dir = e.cdp->newJobFolder();
        const auto r = e.cdp->run (cdparamsArgs (d, referenceSound()), dir, {}, nullptr, 20000);
        dir.deleteRecursively();
        Dialog live;
        bool ok = r.ok && parseDialogText (r.log, live) && live.items.size() == d.items.size() && live.brklen == d.brklen;
        for (size_t i = 0; ok && i < d.items.size(); ++i)
        {
            const auto& a = d.items[i]; const auto& b = live.items[i];
            auto near = [] (double x, double y) { return std::abs (x - y) <= 1e-6 * juce::jmax (1.0, std::abs (x)); };
            ok = a.kind == b.kind && a.name == b.name && a.name2 == b.name2 && a.name3 == b.name3 && a.type == b.type
                 && near (a.lo, b.lo) && near (a.hi, b.hi) && near (a.dflt, b.dflt) && near (a.sublo, b.sublo) && near (a.subhi, b.subhi)
                 && near (a.lo2, b.lo2) && near (a.hi2, b.hi2) && near (a.dflt2, b.dflt2) && a.ranges == b.ranges;
        }
        if (ok) ++same; else differs.add (d.id());
        if (r.ok && r.log.trim() == d.raw.trim()) ++sameText;
    }
    check (same == (int) ds.size(), "every page's parameter list (count, names, kinds, types, ranges, defaults, sub-ranges) matches what CDP's cdparams prints (" + juce::String (same) + " of " + juce::String ((int) ds.size()) + ")" + (differs.isEmpty() ? juce::String() : ": " + differs.joinIntoString (", ")));
    check (sameText == (int) ds.size(), "every page's text is exactly what cdparams prints, the same data (" + juce::String (sameText) + " of " + juce::String ((int) ds.size()) + ")");

    // every numeric control has a sensible range and a default inside it; every control drives a command word
    int badRange = 0, noWord = 0, varying = 0, checks = 0, files = 0;
    for (auto& d : ds)
        for (auto& it : d.items)
        {
            if (it.numeric() && ! (it.lo <= it.dflt + 1e-9 && it.dflt <= it.hi + 1e-9 && it.hi > it.lo)) { ++badRange; std::printf ("  odd range: %s %s %g..%g default %g\n", d.id().toRawUTF8(), it.name.toRawUTF8(), it.lo, it.hi, it.dflt); }
            if (it.param.isEmpty()) ++noWord;
            if (it.varying()) ++varying;
            if (it.kind == "CHECKBUTTON") ++checks;
            if (it.textual()) ++files;
        }
    check (badRange == 0, "every numeric control's default lies inside its range (" + juce::String (badRange) + " odd)");
    check (noWord == 0, "every control drives a word of the command (" + juce::String (noWord) + " without)");
    std::printf ("ORIGINAL pages: %d time-varying (breakpoint file) controls, %d check buttons, %d data-file / text controls\n", varying, checks, files);

    // the page for a particular sound: ranges that depend on it follow it (a 4 s sound: the start-time ceiling is near 4 s)
    {
        const auto* bakedDrunk = dialogFor ("blur", "drunk", 0);
        SoundInfo s; s.sampleRate = 48000; s.samples = 48000 * 4; s.channels = 1;
        auto liveD = bakedDrunk != nullptr ? liveDialog (*e.cdp, *bakedDrunk, s) : nullptr;
        double ceiling = 0;
        if (liveD != nullptr) for (auto& it : liveD->items) if (it.name == "STARTTIME_IN_FILE") ceiling = it.hi;
        check (liveD != nullptr && ceiling > 3.9 && ceiling < 4.0, "asked for a 4 s sound, blur drunk's STARTTIME_IN_FILE ceiling follows it (" + juce::String (ceiling, 3) + ")");
        const auto* bakedBlur = dialogFor ("blur", "blur", 0);
        s.points = 256; s.overlap = 2;
        auto liveB = bakedBlur != nullptr ? liveDialog (*e.cdp, *bakedBlur, s) : nullptr;
        double hi = 0;
        if (liveB != nullptr) for (auto& it : liveB->items) if (it.name == "BLURRING_FACTOR") hi = it.hi;
        check (liveB != nullptr && std::abs (hi - 1500) <= 1, "with 256 points / overlap 2 the BLURRING_FACTOR ceiling is the window count (" + juce::String (hi) + " of 1500)");
    }

    // a real run from the ORIGINAL page with non-default values == the same CDP commands typed by hand (bit-exact)
    {
        Processor pr;
        pr.engine.setSpecimen (synthVoice(), "voice");
        pr.syncPlayer();
        std::unique_ptr<Editor> ed (dynamic_cast<Editor*> (pr.createEditor()));
        ed->setSize (Editor::kW, Editor::kH); ed->refresh();
        ed->setHoodPage (true);
        auto& h = ed->hood();
        auto voice = pr.engine.processedAudio();
        auto runPage = [&] (const char* prog, const char* name, int mode, std::map<juce::String, juce::String> v, std::vector<juce::StringArray> hand, const juce::String& last, const juce::String& what,
                            std::map<juce::String, juce::String> extra = {})
        {
            h.selectProcess (prog, name); if (mode > 0) h.setMode (mode);
            for (auto& kv : v) h.setValue (kv.first, kv.second);
            h.run(); waitIdle (pr.engine);
            auto out = pr.engine.processedAudio();
            auto hd = byHand (*pr.engine.cdp, *voice, hand, last, extra);
            const bool ok = pr.engine.historyCursor() == 1 && out != nullptr && hd != nullptr && bitExact (*out, *hd);
            check (ok, what + (ok ? juce::String() : "  (" + pr.engine.lastCommands.replace ("\n", " | ") + ")"));
            ed->undo();
        };
        runPage ("stretch", "time", 1, { { "timestretch", "1.7" } },
                 { { "pvoc", "anal", "1", "in.wav", "a.ana", "-c1024", "-o3" }, { "stretch", "time", "1", "a.ana", "b.ana", "1.7" }, { "pvoc", "synth", "b.ana", "c.wav" } }, "c.wav",
                 "ORIGINAL page stretch time 1 at 1.7 == CDP by hand (bit-exact)");
        runPage ("modify", "brassage", 6, { { "velocity", "1" }, { "density", "0.9" }, { "grainsize", "70" }, { "pitchshft", "3" }, { "amp", "1" }, { "space", "0.5" }, { "bsplice", "5" }, { "esplice", "5" }, { "range", "30" }, { "-x", "1" }, { "-d", "1" } },
                 { { "modify", "brassage", "6", "in.wav", "o.wav", "1", "0.9", "70", "3", "1", "0.5", "5", "5", "-r30", "-x", "-d" } }, "o.wav",
                 "ORIGINAL page modify brassage 6 with -r30 -x and the table-only -d == CDP by hand (bit-exact)");
        h.setAnalysis ("2048", "4");
        runPage ("blur", "blur", 0, { { "blurring", "9" } },
                 { { "pvoc", "anal", "1", "in.wav", "a.ana", "-c2048", "-o4" }, { "blur", "blur", "a.ana", "b.ana", "9" }, { "pvoc", "synth", "b.ana", "c.wav" } }, "c.wav",
                 "ORIGINAL page blur blur 9 with 2048 points / overlap 4 == CDP by hand (bit-exact)");
        h.setAnalysis ("1024", "3");
        runPage ("focus", "focus", 0, { { "-fN|-pN", "-f6" }, { "-i", "1" }, { "pk", "5" }, { "bw", "0.3" } },
                 { { "pvoc", "anal", "1", "in.wav", "a.ana", "-c1024", "-o3" }, { "focus", "focus", "a.ana", "b.ana", "-f6", "-i", "5", "0.3" }, { "pvoc", "synth", "b.ana", "c.wav" } }, "c.wav",
                 "ORIGINAL page focus focus with FORMANT_BANDS FRQWISE 6 + quick search == CDP by hand (bit-exact)");
        runPage ("modify", "speed", 2, { { "semitone-transpos", "0 -3 2 4" } },
                 { { "modify", "speed", "2", "in.wav", "o.wav", "brk.txt" } }, "o.wav",
                 "ORIGINAL page modify speed 2 with a time-varying (BRK) transposition == CDP by hand with that breakpoint file (bit-exact)", { { "brk.txt", "0 -3 2 4" } });
        // the page's defaults are CDP's own defaults
        h.selectProcess ("stretch", "time"); h.setMode (1);
        check (h.valueOf ("timestretch") == "1", "stretch time opens at CDP's own default (TIMESTRETCH_MULTIPLIER 1): " + h.valueOf ("timestretch"));
        h.selectProcess ("modify", "brassage"); h.setMode (6);
        check (h.valueOf ("grainsize") == "50" && h.valueOf ("bsplice") == "5" && h.valueOf ("density") == "2" && h.valueOf ("range").isEmpty() && h.valueOf ("-d") == "0",
               "brassage 6 opens at CDP's own defaults (grainsize 50, splices 5, density 2; options not sent, flags off)");
        h.setValue ("grainsize", "123"); h.setDefaults();
        check (h.valueOf ("grainsize") == "50", "SET DEFAULTS puts CDP's own default back");
        ed.reset();
        pr.engine.sessionFolder().deleteRecursively();
    }
}

// the ORIGINAL pages at the opening size: every control inside the page, nothing overlapping, for a spread of processes
static bool noOverlaps (juce::Component& parent, juce::String& why, const std::vector<juce::Component*>& skip = {});
static void testHoodLayout()
{
    Processor p;
    p.engine.setSpecimen (synthVoice(), "voice");
    std::unique_ptr<Editor> ed (dynamic_cast<Editor*> (p.createEditor()));
    ed->setSize (Editor::kW, Editor::kH); ed->refresh();
    ed->setHoodPage (true);
    auto& h = ed->hood();
    struct P { const char* prog; const char* name; int mode; };
    const std::vector<P> pages { { "stretch", "time", 1 }, { "modify", "brassage", 7 }, { "modify", "sausage", 0 }, { "strange", "glis", 2 }, { "focus", "focus", 0 },
                                 { "focus", "freeze", 1 }, { "modify", "stack", 0 }, { "pvoc", "anal", 1 }, { "blur", "shuffle", 0 }, { "combine", "make", 0 },
                                 { "modify", "loudness", 3 }, { "morph", "bridge", 2 }, { "modify", "radical", 3 }, { "blur", "drunk", 0 }, { "combine", "mean", 4 },
                                 { "repitch", "getpitch", 2 }, { "formants", "get", 0 }, { "repitch", "analenv", 0 }, { "envel", "extract", 2 }, { "combine", "make2", 0 } };
    int bad = 0, controls = 0;
    for (auto& pg : pages)
    {
        h.selectProcess (pg.prog, pg.name); if (pg.mode > 0) h.setMode (pg.mode);
        if (juce::String (pg.name) == "sausage") h.setExtraFiles ({ "/tmp/one.wav", "/tmp/two.wav" });   // (names only: the list's layout)
        // and with the time-varying view open on the first control that has it
        for (int pass = 0; pass < 2; ++pass)
        {
            if (pass == 1)
            {
                bool any = false;
                if (auto* d = h.currentDialog()) for (auto& it : d->items) if (it.varying() && ! any) { h.setValue (it.param, "0 1 1 2"); any = true; }
                if (! any) break;
            }
            ed->refresh();
            juce::String why;
            auto& form = h.formComponent();
            bool ok = noOverlaps (form, why);
            for (auto* c : form.getChildren())
            {
                if (! ok) break;
                if (! c->isVisible() || c->getBounds().isEmpty()) continue;
                juce::String w2;
                if (! noOverlaps (*c, w2)) { ok = false; why = juce::String (pg.prog) + " " + pg.name + " control: " + w2; }
                ++controls;
            }
            if (! ok) { ++bad; check (false, juce::String (pg.prog) + " " + pg.name + " " + juce::String (pg.mode) + (pass ? " (BRK open)" : "") + ": " + why); }
        }
    }
    check (bad == 0, "nothing overlaps on " + juce::String ((int) pages.size()) + " ORIGINAL pages at 1200x760 (" + juce::String (controls) + " controls checked, inside their rows, rows inside the page)");
    ed.reset();
    p.engine.sessionFolder().deleteRecursively();
}

// ------------------------------------------------------------------------------------------------ the data files make / make2 / sausage need
static void testHoodDataFiles()
{
    using namespace hood;
    Processor pr;
    auto voice = synthVoice(), piano = synthPiano();
    pr.engine.setSpecimen (voice, "voice"); pr.engine.setPartner (piano, "piano");
    pr.syncPlayer();
    std::unique_ptr<Editor> ed (dynamic_cast<Editor*> (pr.createEditor()));
    ed->setSize (Editor::kW, Editor::kH); ed->refresh();
    ed->setHoodPage (true);
    auto& h = ed->hood();
    const Cdp& cdp = *pr.engine.cdp;
    for (auto* prog : { "repitch", "formants", "envel" })
        check (cdp.programsDir().getChildFile (prog).existsAsFile(), juce::String ("the engine's ") + prog + " program is shipped beside the others");

    // 1. MAKE FROM SOUND == the engine's own extractor by hand, the same data (pitch, formant, envelope data from the same analysis)
    auto hand = byHandDir (cdp, *voice, { { "pvoc", "anal", "1", "in.wav", "a.ana", "-c1024", "-o3" },
                                          { "repitch", "getpitch", "1", "a.ana", "tone.ana", "p.frq" },
                                          { "formants", "get", "a.ana", "f.for", "-p4" },
                                          { "repitch", "analenv", "a.ana", "e.evl" } });
    check (hand != juce::File(), "pitch, formant and envelope data made by hand with the engine's programs");
    h.selectProcess ("combine", "make2");
    h.makeDataNow ("pitchfile", voice); h.makeDataNow ("formantfile", voice); h.makeDataNow ("envfile", voice);
    const juce::File pitchF (h.valueOf ("pitchfile")), formantF (h.valueOf ("formantfile")), envF (h.valueOf ("envfile"));
    check (pitchF.existsAsFile() && pitchF.hasFileExtension ("frq") && sameBytes (pitchF, hand.getChildFile ("p.frq")), "MAKE FROM SOUND pitch data (repitch getpitch 1) == by hand, the same data: " + pitchF.getFileName());
    check (formantF.existsAsFile() && formantF.hasFileExtension ("for") && sameBytes (formantF, hand.getChildFile ("f.for")), "MAKE FROM SOUND formant data (formants get -p4) == by hand, the same data: " + formantF.getFileName());
    check (envF.existsAsFile() && envF.hasFileExtension ("evl") && sameBytes (envF, hand.getChildFile ("e.evl")), "MAKE FROM SOUND envelope data (repitch analenv) == by hand, the same data: " + envF.getFileName() + " " + juce::String (envF.getSize()) + " vs " + juce::String (hand.getChildFile ("e.evl").getSize()));
    check (h.commandText().contains ("MADE envelope data") && h.commandText().contains ("repitch analenv"), "the page shows what was made and the commands that made it");

    // 2. combine make2 from the page (the three files filled in) == CDP by hand (bit-exact), then combine make
    auto hand2 = byHand (cdp, *voice, { { "pvoc", "anal", "1", "in.wav", "a.ana", "-c1024", "-o3" },
                                        { "repitch", "getpitch", "1", "a.ana", "tone.ana", "p.frq" }, { "formants", "get", "a.ana", "f.for", "-p4" }, { "repitch", "analenv", "a.ana", "e.evl" },
                                        { "combine", "make2", "p.frq", "f.for", "e.evl", "m.ana" }, { "pvoc", "synth", "m.ana", "m.wav" } }, "m.wav");
    h.run(); waitIdle (pr.engine);
    auto out = pr.engine.processedAudio();
    check (pr.engine.historyCursor() == 1 && hand2 != nullptr && out != nullptr && bitExact (*out, *hand2), "combine make2 from the page (pitch + formant + envelope data made from the sound) == CDP by hand (bit-exact)"
           + (pr.engine.historyCursor() == 1 ? juce::String() : "  (" + pr.engine.lastCommands.replace ("\n", " | ") + ")"));
    ed->undo();
    h.selectProcess ("combine", "make");
    h.setValue ("pitchfile", pitchF.getFullPathName()); h.setValue ("formantfile", formantF.getFullPathName());
    auto hand3 = byHand (cdp, *voice, { { "pvoc", "anal", "1", "in.wav", "a.ana", "-c1024", "-o3" },
                                        { "repitch", "getpitch", "1", "a.ana", "tone.ana", "p.frq" }, { "formants", "get", "a.ana", "f.for", "-p4" },
                                        { "combine", "make", "p.frq", "f.for", "m.ana" }, { "pvoc", "synth", "m.ana", "m.wav" } }, "m.wav");
    h.run(); waitIdle (pr.engine);
    out = pr.engine.processedAudio();
    check (pr.engine.historyCursor() == 1 && hand3 != nullptr && out != nullptr && bitExact (*out, *hand3), "combine make from the page (files picked) == CDP by hand (bit-exact)"
           + (pr.engine.historyCursor() == 1 ? juce::String() : "  (" + pr.engine.lastCommands.replace ("\n", " | ") + ")"));
    ed->undo();
    // formant data made from SPECIMEN 002 gives a different (cross) result, still CDP's
    h.makeDataNow ("formantfile", piano);
    const juce::File formantP (h.valueOf ("formantfile"));
    check (formantP.existsAsFile() && formantP != formantF, "MAKE FROM SOUND can take SPECIMEN 002 (" + formantP.getFileName() + ")");
    h.run(); waitIdle (pr.engine);
    check (pr.engine.historyCursor() == 1 && ! bitExact (*pr.engine.processedAudio(), *hand3), "combine make with the voice's pitch and the piano's formants runs (a different sound)");
    ed->undo();

    // 3. the data-file processes run from their own ORIGINAL pages: the sound stays, the file is kept and listed
    h.selectProcess ("formants", "get"); h.setValue ("-fN|-pN", "-f8");
    h.run(); waitIdle (pr.engine);
    check (pr.engine.historyCursor() == 1 && bitExact (*pr.engine.processedAudio(), *voice) && pr.engine.lastCommands.contains ("# DATA for:"), "formants get -f8 from its page keeps the sound and writes a .for data file: " + pr.engine.lastCommands.replace ("\n", " | "));
    {
        auto f = juce::File (pr.engine.lastCommands.fromFirstOccurrenceOf ("# DATA for: ", false, false).upToFirstOccurrenceOf ("\n", false, false).trim());
        auto hd = byHandDir (cdp, *voice, { { "pvoc", "anal", "1", "in.wav", "a.ana", "-c1024", "-o3" }, { "formants", "get", "a.ana", "f.for", "-f8" } });
        check (f.existsAsFile() && hd != juce::File() && sameBytes (f, hd.getChildFile ("f.for")), "that .for file == formants get -f8 by hand, the same data");
        hd.deleteRecursively();
    }
    ed->undo();
    h.selectProcess ("repitch", "getpitch"); h.setMode (1); h.setValue ("R", "2"); h.setValue ("-z", "1");
    h.run(); waitIdle (pr.engine);
    {
        auto f = juce::File (pr.engine.lastCommands.fromFirstOccurrenceOf ("# DATA frq: ", false, false).upToFirstOccurrenceOf ("\n", false, false).trim());
        auto hd = byHandDir (cdp, *voice, { { "pvoc", "anal", "1", "in.wav", "a.ana", "-c1024", "-o3" }, { "repitch", "getpitch", "1", "a.ana", "t.ana", "p.frq", "-t2", "-z" }, { "pvoc", "synth", "t.ana", "t.wav" } });
        juce::String err;
        auto tone = hd != juce::File() ? readWav (hd.getChildFile ("t.wav"), err) : nullptr;
        // (the pitch DATA is checked exactly below; the tone getpitch writes beside it is not bit-stable between two runs of CDP's own
        // program, so the tone is checked for length and level)
        const auto page = pr.engine.processedAudio();
        const bool toneOk = pr.engine.historyCursor() == 1 && tone != nullptr && page->length() == tone->length() && std::abs (rms (*page) - rms (*tone)) < 0.02f * juce::jmax (0.001f, rms (*tone));
        check (toneOk, "repitch getpitch 1 -t2 -z from its page: the detected-pitch tone is heard (same length and level as CDP by hand)"
               + (toneOk ? juce::String() : " [cursor " + juce::String (pr.engine.historyCursor()) + " | " + pr.engine.lastCommands.replace ("\n", " | ").substring (0, 300) + "]"));
        check (f.existsAsFile() && hd != juce::File() && sameBytes (f, hd.getChildFile ("p.frq")), "and its pitch data file == by hand, the same data");
        hd.deleteRecursively();
    }
    ed->undo();
    h.selectProcess ("envel", "extract"); h.setMode (1); h.setValue ("wsize", "30");
    h.run(); waitIdle (pr.engine);
    {
        auto f = juce::File (pr.engine.lastCommands.fromFirstOccurrenceOf ("# DATA evl: ", false, false).upToFirstOccurrenceOf ("\n", false, false).trim());
        auto hd = byHandDir (cdp, *voice, { { "envel", "extract", "1", "in.wav", "e.evl", "30" } });
        check (pr.engine.historyCursor() == 1 && f.existsAsFile() && hd != juce::File() && sameBytes (f, hd.getChildFile ("e.evl")), "envel extract 1 (window 30) from its page == by hand, the same data");
        hd.deleteRecursively();
    }
    ed->undo();

    // 4. modify sausage with several sounds from the page == CDP by hand (bit-exact)
    h.selectProcess ("modify", "sausage");
    check (h.request().extraFiles.isEmpty(), "sausage starts with no extra sounds");
    auto soundA = writeSoundFile (*piano, h.dataFolder(), "piano");
    h.setExtraFiles ({ soundA.getFullPathName() });
    auto earlier = pr.engine.snapshot (0).audio;   // (the original: an "earlier result" the page can add)
    auto soundB = writeSoundFile (*earlier, h.dataFolder(), "result-0");
    h.setExtraFiles ({ soundA.getFullPathName(), soundB.getFullPathName() });
    for (auto kv : std::map<juce::String, juce::String> { { "velocity", "1" }, { "density", "1.5" }, { "hvelocity", "1" }, { "hdensity", "1.5" }, { "grainsize", "60" }, { "pitchshift", "2" },
                                                          { "amp", "1" }, { "space", "0.5" }, { "bsplice", "5" }, { "esplice", "5" }, { "hgrainsize", "80" }, { "hpitchshift", "3" }, { "hamp", "1" },
                                                          { "hspace", "0.5" }, { "hbsplice", "5" }, { "hesplice", "5" } })
        h.setValue (kv.first, kv.second);
    check (h.commandText().contains ("modify sausage infile infile2 infile3 outfile 1 1.5 1 1.5 60 2 1 0.5 5 5 80 3 1 0.5 5 5"), "the command preview lists the three sounds: " + h.commandText().replace ("\n", " | "));
    h.run(); waitIdle (pr.engine);
    auto pianoM = std::make_shared<Audio> (resampled (mono (*piano), voice->sampleRate));
    auto hand4 = byHand (cdp, *voice, { { "modify", "sausage", "in.wav", "in2.wav", "in3.wav", "o.wav", "1", "1.5", "1", "1.5", "60", "2", "1", "0.5", "5", "5", "80", "3", "1", "0.5", "5", "5" } }, "o.wav",
                         {}, { { "in2.wav", pianoM }, { "in3.wav", voice } });
    out = pr.engine.processedAudio();
    // (sausage takes no seed: the engine seeds its grains from the clock, so the two runs are bit-exact only when they land in the
    //  same second; otherwise the same command gives the same length and channels with different grain picks)
    const bool sameShape = hand4 != nullptr && out != nullptr && out->length() == hand4->length() && out->channels() == hand4->channels();
    check (pr.engine.historyCursor() == 1 && sameShape && (bitExact (*out, *hand4) || out->length() > 0), "modify sausage with the piano and an earlier result added == CDP by hand with those three files (same command, same length and channels)"
           + (pr.engine.historyCursor() == 1 ? juce::String() : "  (" + pr.engine.lastCommands.replace ("\n", " | ") + ")"));
    ed->undo();
    // the saved step remembers the extra sounds (session / state)
    {
        hood::Request r = h.request(), back;
        check (hood::Request::fromData (r.toData(), back) && back.extraFiles == r.extraFiles && back.extraFiles.size() == 2 && back.dataDir == r.dataDir, "the extra sounds and the data folder are saved with the step");
    }
    hand.deleteRecursively();
    ed.reset();
    pr.engine.sessionFolder().deleteRecursively();
}

static void testHoodPage (const std::shared_ptr<Audio>& voice)
{
    using namespace hood;
    Processor p;
    p.engine.setSpecimen (voice, "voice");
    p.syncPlayer();
    std::unique_ptr<Editor> ed (dynamic_cast<Editor*> (p.createEditor()));
    ed->setSize (Editor::kW, Editor::kH); ed->refresh();
    ed->setHoodPage (true);
    auto& h = ed->hood();
    check (ed->hoodPageOn() && h.isVisible(), "the UNDER THE HOOD page opens");
    check (h.visibleProcessCount() == 54, "the list shows all 54 processes");
    h.setSearch ("brassage");
    check (h.listShows ("modify", "brassage") && h.visibleProcessCount() <= 2, "search finds brassage (and only its grain cousin, Multi-Sound Brassage) (" + juce::String (h.visibleProcessCount()) + ")");
    h.setSearch ("reverse");
    check (h.listShows ("modify", "radical"), "search finds a process by our plain name too (reverse -> modify radical 1)");
    h.setSearch ("grpsize");
    check (h.visibleProcessCount() == 1, "search also finds a parameter name (grpsize -> blur shuffle)");
    h.setSearch ({});
    h.selectProcess ("modify", "brassage"); h.setMode (7);
    check (h.paramFieldCount() == 23, "modify brassage 7 shows its 23 controls: CDP's own 16 parameters, 4 options and 3 flags (-d is in CDP's table, not its usage text) (" + juce::String (h.paramFieldCount()) + ")");
    h.setMode (6);
    for (auto kv : std::map<juce::String, juce::String> { { "velocity", "1" }, { "density", "0.9" }, { "grainsize", "70" }, { "pitchshft", "3" }, { "amp", "1" }, { "space", "0.5" }, { "bsplice", "5" }, { "esplice", "5" } })
        h.setValue (kv.first, kv.second);
    check (h.commandText().contains ("modify brassage 6 infile outfile 1 0.9 70 3 1 0.5 5 5"), "the command preview is CDP's: " + h.commandText().replace ("\n", " | "));
    h.run();
    waitIdle (p.engine);
    check (p.engine.historyCursor() == 1 && ! bitExact (*p.engine.processedAudio(), *voice), "RUN made the processed sound");
    check (h.commandText().contains ("RAN") && h.commandText().contains ("modify brassage 6"), "the page shows the command that ran");
    ed->undo();
    check (p.engine.historyCursor() == 0 && bitExact (*p.engine.processedAudio(), *voice), "UNDO on the page restores the original sound");
    // a spectral process runs pvoc around it
    h.selectProcess ("blur", "blur"); h.setValue ("blurring", "9"); h.setAnalysis ("2048", "3");
    h.run(); waitIdle (p.engine);
    check (p.engine.current().commands.contains ("pvoc anal 1") && p.engine.current().commands.contains ("-c2048") && p.engine.current().commands.contains ("blur blur"), "a spectral process runs pvoc anal (-c2048) before it");
    // saved and restored with the session
    ed.reset();
    p.engine.sessionFolder().deleteRecursively();
}

static void testLong (Engine& e)
{
    // three minutes of stereo
    auto v = synthVoice (44100.0, 4.2);
    auto longA = std::make_shared<Audio>(); longA->sampleRate = 44100; longA->buf.setSize (2, 44100 * 180);
    for (int c = 0; c < 2; ++c) for (int i = 0; i < longA->length(); ++i) longA->buf.setSample (c, i, v->buf.getSample (0, (i + c * 13) % v->length()) * (c == 0 ? 1.0f : 0.8f));
    e.setSpecimen (longA, "long");
    juce::String err;
    auto t0 = juce::Time::getMillisecondCounterHiRes();
    const bool ok = e.applyNow ({ Op::Pull, { { "amount", 2.0f } }, 1 }, err);
    const double secs = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
    check (ok && std::abs (e.processedAudio()->seconds() - 360.0) < 2.0, "a 3-minute stereo sound: PULL 2x gives 6 minutes (" + juce::String (ok ? e.processedAudio()->seconds() : 0.0, 1) + " s, took " + juce::String (secs, 1) + " s)");
    if (ok) checkQuality (*e.processedAudio(), "3-minute PULL");
    t0 = juce::Time::getMillisecondCounterHiRes();
    const bool ok2 = e.applyNow ({ Op::Smear, {}, 1 }, err);
    check (ok2, "a 6-minute stereo sound: SMEAR ran (" + juce::String ((juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0, 1) + " s)");
    // over the limits: the input is trimmed to 10 minutes; a result never grows past 12
    auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ghostlab-11min.wav");
    Audio big; big.sampleRate = 8000; big.buf.setSize (1, 8000 * 660);
    for (int i = 0; i < big.length(); ++i) big.buf.setSample (0, i, 0.2f * std::sin (6.2831853f * 200.0f * i / 8000.0f));
    writeWav (big, f, 16, err);
    bool trimmed = false;
    auto loaded = loadAudio (f, err, &trimmed);
    check (loaded != nullptr && trimmed && std::abs (loaded->seconds() - kMaxInputSeconds) < 0.01, "an 11-minute file is trimmed to 10 minutes");
    f.deleteFile();
}

static void testErrors (const std::shared_ptr<Audio>& voice)
{
    // a fake CDP whose pvoc fails the way CDP does: prints ERROR and exits 255
    auto fake = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ghostlab-fakecdp");
    fake.deleteRecursively(); fake.createDirectory();
    auto pv = fake.getChildFile ("pvoc");
    pv.replaceWithText ("#!/bin/sh\necho 'ERROR: INVALID DATA'\nexit 255\n", false, false, "\n");
    pv.setExecutePermission (true);
    Engine e;
    e.cdp = std::make_unique<Cdp> (fake, Cdp::defaultWorkRoot());
    e.setSpecimen (voice, "voice");
    juce::String err;
    const bool ok = e.applyNow ({ Op::Pull, {}, 1 }, err);
    check (! ok && err.contains ("didn't work") && err.contains ("INVALID DATA"), "a CDP error becomes a friendly message: \"" + err + "\"");
    check (e.historySize() == 1 && bitExact (*e.processedAudio(), *voice), "a failed action leaves the sound and the history alone");
    // a hung program is stopped
    pv.replaceWithText ("#!/bin/sh\nsleep 30\n", false, false, "\n");
    pv.setExecutePermission (true);
    juce::File job = e.cdp->newJobFolder();
    auto t0 = juce::Time::getMillisecondCounterHiRes();
    auto r = e.cdp->run ({ "pvoc" }, job, {}, nullptr, 800);
    check (! r.ok && juce::Time::getMillisecondCounterHiRes() - t0 < 5000 && r.error.contains ("too long"), "a stuck program is stopped after its time-out: \"" + r.error + "\"");
    std::atomic<bool> cancel { true };
    r = e.cdp->run ({ "pvoc" }, job, {}, &cancel, 60000);
    check (! r.ok && r.error == "Stopped.", "cancel stops a running program");
    job.deleteRecursively();
    // no engine at all
    e.cdp = std::make_unique<Cdp> (juce::File(), Cdp::defaultWorkRoot());
    bool okE = ! e.applyNow ({ Op::Pull, {}, 1 }, err) && err.contains ("missing");
    check (okE, "no engine: \"" + err + "\"");
    fake.deleteRecursively();
    // bad files
    auto txt = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ghostlab-not-audio.wav");
    txt.replaceWithText ("not audio");
    okE = loadAudio (txt, err) == nullptr && err.isNotEmpty();
    check (okE, "a broken file: \"" + err + "\"");
    txt.deleteFile();
    // a tiny sound
    Engine e2;
    auto tiny = std::make_shared<Audio>(); tiny->sampleRate = 44100; tiny->buf.setSize (1, 3000); tiny->buf.clear();
    for (int i = 0; i < 3000; ++i) tiny->buf.setSample (0, i, 0.3f * std::sin (i * 0.05f));
    e2.setSpecimen (tiny, "tiny");
    int okCount = 0, friendly = 0;
    for (auto op : { Op::Pull, Op::Twist, Op::Freeze, Op::Smear, Op::Shatter, Op::Mutate, Op::GrainCloud, Op::SpectralMelt, Op::TimeWarp, Op::VoiceFusion })
    {
        e2.setSpecimen (tiny, "tiny");
        if (e2.applyNow ({ op, {}, 3 }, err)) ++okCount; else if (err.isNotEmpty()) ++friendly;
    }
    check (okCount + friendly == 10, "a 68 ms sound: every action either works or explains itself (" + juce::String (okCount) + " worked, " + juce::String (friendly) + " explained)");
}

static void testGestures()
{
    SoundBody b; b.setSize (800, 200);
    auto stroke = [] (std::vector<juce::Point<float>> pts, double msEach)
    { SoundBody::Stroke s; double t = 0; for (auto& p : pts) { s.pts.push_back (p); s.ms.push_back (t); t += msEach; } return s; };
    Step s;
    bool ok = b.classify (stroke ({ { 200, 100 }, { 300, 102 }, { 520, 98 } }, 30), s) && s.op == Op::Pull && s.get ("from") > 0.2f && s.get ("to") < 0.7f;
    check (ok, "drag sideways = PULL of that region (" + s.describe() + ")");
    check (b.classify (stroke ({ { 400, 40 }, { 402, 100 }, { 401, 170 } }, 30), s) && s.op == Op::Pinch && s.get ("amount") > 0, "drag down = PINCH (squeeze)");
    check (b.classify (stroke ({ { 400, 170 }, { 402, 100 }, { 401, 30 } }, 30), s) && s.op == Op::Pinch && s.get ("amount") < 0, "drag up = PINCH (widen)");
    std::vector<juce::Point<float>> circle;
    for (int i = 0; i <= 40; ++i) circle.push_back ({ 400 + 150 * std::cos (i * 0.15f), 100 + 70 * std::sin (i * 0.15f) });
    ok = b.classify (stroke (circle, 15), s) && s.op == Op::Twist && s.get ("bend") > 0;
    check (ok, "circling = TWIST (" + s.describe() + ")");
    std::vector<juce::Point<float>> shake;
    for (int i = 0; i < 12; ++i) shake.push_back ({ 400.0f + (i % 2 ? 40.0f : -40.0f), 100.0f });
    ok = b.classify (stroke (shake, 40), s) && s.op == Op::Mutate;
    check (ok, "shaking = MUTATE (" + s.describe() + ")");
    check (! b.classify (stroke ({ { 400, 100 }, { 402, 101 } }, 30), s), "a tap is not a gesture (it plays the sound)");
    // build 11: twisting is easy - a small circle anywhere on the body (not only round its very centre), either way round,
    // and a curve of half a turn is enough
    std::vector<juce::Point<float>> small, half, back;
    for (int i = 0; i <= 30; ++i) small.push_back ({ 180 + 34 * std::cos (i * 0.21f), 70 + 34 * std::sin (i * 0.21f) });
    ok = b.classify (stroke (small, 15), s) && s.op == Op::Twist && s.get ("bend") > 0;
    check (ok, "a small circle off to one side = TWIST too (" + s.describe() + ")");
    for (int i = 0; i <= 16; ++i) half.push_back ({ 600 + 60 * std::cos (i * 0.2f), 110 + 60 * std::sin (i * 0.2f) });
    ok = b.classify (stroke (half, 15), s) && s.op == Op::Twist && s.get ("bend") > 0.5f && s.get ("bend") < 1.0f;
    check (ok, "half a turn is enough to TWIST (" + s.describe() + ")");
    for (int i = 0; i <= 30; ++i) back.push_back ({ 400 + 90 * std::cos (-i * 0.2f), 100 + 60 * std::sin (-i * 0.2f) });
    ok = b.classify (stroke (back, 15), s) && s.op == Op::Twist && s.get ("bend") < 0;
    check (ok, "anticlockwise twists the other way (" + s.describe() + ")");
    // going round twice quickly is still a twist, not a shake (it crosses left-right four times, but it goes round)
    std::vector<juce::Point<float>> twice;
    for (int i = 0; i <= 64; ++i) twice.push_back ({ 400 + 80 * std::cos (i * 0.2f), 100 + 55 * std::sin (i * 0.2f) });
    ok = b.classify (stroke (twice, 12), s) && s.op == Op::Twist;
    check (ok, "twice round quickly is a TWIST, not a shake (" + s.describe() + ")");
    // and the others are not mistaken for one: a hooked sideways drag still pulls, a wobbly shake still mutates
    ok = b.classify (stroke ({ { 200, 100 }, { 320, 96 }, { 470, 100 }, { 520, 118 }, { 528, 140 } }, 30), s) && s.op == Op::Pull;
    check (ok, "a sideways drag with a hook at its end is still a PULL (" + s.describe() + ")");
    std::vector<juce::Point<float>> wobbly;
    for (int i = 0; i < 14; ++i) wobbly.push_back ({ 400.0f + (i % 2 ? 60.0f : -60.0f), 100.0f + (i % 4 < 2 ? 5.0f : -5.0f) });
    ok = b.classify (stroke (wobbly, 40), s) && s.op == Op::Mutate;
    check (ok, "a shake that wanders up and down a little is still MUTATE (" + s.describe() + ")");
}

// ------------------------------------------------------------------------------------------------ build 11: the body in the hand
// THE LIVING SOUND goes with the hand at once, springs back like jelly, winds up as you turn, and tells the editor how
// it is held (so the sound can change while the hand moves). A tap still only plays / stops the sound.
static void testBodyFeel (const std::shared_ptr<Audio>& voice)
{
    SoundBody b; b.setSize (800, 200);
    b.setAnimating (false);                       // (the test moves time itself)
    b.setProfile (makeProfile (*voice));
    int clicks = 0, gestures = 0, feels = 0, ends = 0;
    Step last; SoundBody::Feel feel; juce::String liveNow, hintNow; bool commit = false;
    b.onClick = [&] { ++clicks; };
    b.onGesture = [&] (const Step& st) { ++gestures; last = st; };
    b.onFeel = [&] (const SoundBody::Feel& f) { if (f.active) { ++feels; feel = f; } else { ++ends; commit = f.commit; } };
    b.onLive = [&] (const juce::String& g, const juce::String& h) { liveNow = g; hintNow = h; };
    // a tap
    b.press ({ 400, 100 }); b.advance (3); b.release ({ 401, 100 });
    check (clicks == 1 && gestures == 0 && feels == 0 && ends == 0, "a tap only plays / stops the sound (no live sound, no gesture)");
    // grab it and move down: the grabbed part is under the hand within a few frames, before any gesture is recognised
    b.press ({ 300, 100 }); b.dragTo ({ 300, 112 });
    b.advance (3);
    const float want = 12.0f / (b.bodyRect().getHeight() * 0.5f);
    check (b.liveBend() > 0.6f * want && liveNow.isEmpty() && hintNow.isNotEmpty(),
           "the body goes with the hand at once: 3 frames after a 12 px move the grabbed part has gone " + juce::String (juce::roundToInt (100.0f * b.liveBend() / want)) + "% of the way (no gesture needed)");
    check (feels >= 1 && feel.active && feel.bend > 0.0f && std::abs (feel.where - 0.36f) < 0.05f, "it tells the editor it is held, how, and where it was grabbed (" + juce::String (feel.where, 2) + " along the sound)");
    b.dragTo ({ 300, 150 }); b.advance (10);
    check (std::abs (b.liveBend() - 50.0f / (b.bodyRect().getHeight() * 0.5f) * 0.4f) < 0.03f && liveNow.startsWith ("PINCH"), "it keeps following (" + liveNow + ": " + hintNow + ")");
    // let go: it springs back past rest and settles (jelly), and the pinch runs
    b.release ({ 300, 150 });
    float lowest = 1.0f; int settled = -1;
    for (int i = 0; i < 150; ++i) { b.advance (1); lowest = juce::jmin (lowest, b.liveBend()); if (settled < 0 && std::abs (b.liveBend()) < 0.002f && std::abs (b.livePinch()) < 0.002f && i > 20) settled = i; }
    check (lowest < -0.01f && settled > 0 && settled < 140, "let go, it wobbles back like jelly (past rest by " + juce::String (-lowest, 3) + ", still after " + juce::String (settled) + " frames)");
    check (gestures == 1 && last.op == Op::Pinch && ends == 1 && commit && liveNow.isEmpty() && hintNow.isEmpty(), "letting go runs the PINCH and says the hand is off");
    // sideways: the body's material slides with the hand, and it reads as a PULL
    b.press ({ 250, 100 }); b.dragTo ({ 290, 101 }); b.advance (4); b.dragTo ({ 400, 100 }); b.advance (8);
    check (b.liveDrag() > 0.15f && b.livePull() > 0.1f && liveNow.startsWith ("PULL") && feel.pull > 0.1f, "dragged sideways, it slides with the hand and stretches (" + liveNow + ")");
    b.release ({ 400, 100 });
    check (gestures == 2 && last.op == Op::Pull, "letting go runs the PULL");
    b.advance (160);
    // going round: it winds up while the hand turns (before the twist is complete), then the TWIST runs
    b.press ({ 560, 100 });
    float woundAtQuarter = 0, woundAtHalf = 0;
    for (int i = 1; i <= 22; ++i)
    {
        b.dragTo ({ 520 + 40 * std::cos (i * 0.2f), 100 + 40 * std::sin (i * 0.2f) }); b.advance (1);
        if (i == 9) woundAtQuarter = b.liveTwist();
        if (i == 16) woundAtHalf = b.liveTwist();
    }
    check (woundAtQuarter > 0.02f && woundAtHalf > woundAtQuarter + 0.1f && feel.twist > 0.3f,
           "turning winds it up as you go (" + juce::String (woundAtQuarter, 2) + " at a quarter turn, " + juce::String (woundAtHalf, 2) + " at half, " + juce::String (b.liveTwist(), 2) + " at " + liveNow + ")");
    b.release ({ 520 + 40 * std::cos (4.4f), 100 + 40 * std::sin (4.4f) });
    check (gestures == 3 && last.op == Op::Twist && last.get ("bend") > 0.5f, "letting go runs the TWIST (" + last.describe() + ")");
    // a wiggle that is no gesture: the live sound ends, nothing runs, and it is not a click either
    b.advance (160);
    const int clicksBefore = clicks, endsBefore = ends;
    b.press ({ 400, 100 }); b.dragTo ({ 409, 104 }); b.advance (2); b.release ({ 409, 104 });
    check (gestures == 3 && clicks == clicksBefore && ends == endsBefore + 1 && ! commit, "a small wiggle runs nothing: the hand is off, no process follows");
    // an empty tube does nothing in the hand
    SoundBody e; e.setSize (800, 200); e.setAnimating (false);
    int eg = 0, ef = 0; e.onGesture = [&] (const Step&) { ++eg; }; e.onFeel = [&] (const SoundBody::Feel&) { ++ef; };
    e.press ({ 200, 100 }); e.dragTo ({ 500, 100 }); e.release ({ 500, 100 });
    check (eg == 0 && ef == 0 && ! e.touching(), "with no sound in the tube, dragging does nothing");
}

// the live sound under the hand: the processor plays the processed sound round and round through the body's live chain
// (no allocation, no locks), a still hand changes nothing, every gesture changes it, and the real result takes over
static void testBodyLive (const std::shared_ptr<Audio>& voice)
{
    const juce::String who = Processor::isFx ? "FX" : "instrument";
    const int bs = 256, ins = Processor::isFx ? 2 : 0;
    Processor p;
    p.setSpectreLive (false);   // (FX: the track dry, so only the body's live chain is heard)
    p.setPlayConfigDetails (ins, 2, 48000.0, bs);
    p.prepareToPlay (48000.0, bs);
    p.engine.setSpecimen (voice, "voice");
    p.syncPlayer();
    juce::AudioBuffer<float> buf (2, bs); juce::MidiBuffer midi;
    int at = 0;
    // one block: the track's own audio goes in (FX), what comes out beyond it is our preview
    auto block = [&] (float* keep)
    {
        for (int i = 0; i < bs; ++i, ++at) { const float in = Processor::isFx ? 0.2f * (float) std::sin (0.031 * at) : 0.0f; buf.setSample (0, i, in); buf.setSample (1, i, in); }
        const juce::AudioBuffer<float> in (buf);
        p.processBlock (buf, midi);
        float peak = 0;
        for (int i = 0; i < bs; ++i) { const float v = buf.getSample (0, i) - in.getSample (0, i); peak = juce::jmax (peak, std::abs (v)); if (keep != nullptr) keep[i] = v; }
        return peak;
    };
    auto record = [&] (int blocks) { Audio a; a.sampleRate = 48000.0; a.buf.setSize (1, blocks * bs); for (int k = 0; k < blocks; ++k) block (a.buf.getWritePointer (0) + k * bs); return a; };
    auto rmsDiff = [] (const Audio& a, const Audio& b, int from, int n)
    {
        double d = 0, e = 0;
        for (int i = from; i < from + n; ++i) { d += juce::square ((double) a.buf.getSample (0, i) - b.buf.getSample (0, i)); e += juce::square ((double) a.buf.getSample (0, i)); }
        return std::sqrt (d / juce::jmax (1e-12, e));
    };
    // the plain processed sound, as a reference
    p.player.play (Player::Processed, 0.0);
    const Audio plain = record (360);
    p.player.stop(); record (20);
    check (p.player.playingSlot() < 0, who + ": (stopped)");
    // a hand lands and holds still
    p.beginBodyTouch (0.0f);
    const Audio still = record (120);
    check (p.bodyTouching() && p.player.playingSlot() == Player::Touch, who + ": a hand on the living sound starts the sound playing round");
    check (rmsDiff (plain, still, 0, 120 * bs) < 1e-6, who + ": a still hand changes nothing (the same sound, sample for sample)");
    // it twists: the sound changes at once, and nothing is allocated or locked on the audio thread
    Processor::BodyTouch tw; tw.twist = 0.9f;
    p.setBodyTouch (tw);
    gAudioAllocs = 0; gAudioLocks = 0; tlAudio = true;
    const Audio twisted = [&] { Audio a; a.sampleRate = 48000.0; a.buf.setSize (1, 120 * bs); for (int k = 0; k < 120; ++k) block (a.buf.getWritePointer (0) + k * bs); return a; }();
    tlAudio = false;
    {
        Audio ref; ref.sampleRate = 48000.0; ref.buf.setSize (1, 120 * bs); ref.buf.copyFrom (0, 0, plain.buf, 0, 120 * bs, 120 * bs);
        const double early = rmsDiff (ref, twisted, 0, 12 * bs), all = rmsDiff (ref, twisted, 0, 120 * bs);
        check (early > 0.05 && all > 0.25, who + ": twisting it changes the sound live (" + juce::String (juce::roundToInt (100.0 * early)) + "% different within 64 ms, " + juce::String (juce::roundToInt (100.0 * all)) + "% over the next 0.6 s)");
    }
    // (the allocations of the test's own recording buffer happen before tlAudio; only the blocks count)
    check (gAudioAllocs.load() == 0 && gAudioLocks.load() == 0, who + ": the live chain allocates and locks nothing on the audio thread (" + juce::String (gAudioAllocs.load()) + ", " + juce::String (gAudioLocks.load()) + ")");
    // each way of holding it sounds different
    const auto lp = [] (float pull, float pinch, float twist, float shake, float bend) { Processor::BodyTouch b; b.pull = pull; b.pinch = pinch; b.twist = twist; b.shake = shake; b.bend = bend; return Processor::bodyLive (b); };
    check (lp (0, 0, 0, 0, 0).isZero(), who + ": held without moving, the live chain does nothing");
    check (lp (0.8f, 0, 0, 0, 0).smear > 0.4f && lp (0.8f, 0, 0, 0, 0).freeze > 0.3f, who + ": PULL smears and holds the sound");
    check (lp (0, 0, 0.8f, 0, 0).shimmer > 0.4f && lp (0, 0, 0.8f, 0, 0).tilt > 0.3f && lp (0, 0, -0.8f, 0, 0).tilt < -0.3f && lp (0, 0, -0.8f, 0, 0).drive > 0.3f && lp (0, 0, -0.8f, 0, 0).gainDb < -1.0f,
           who + ": TWIST one way is brighter with a shimmer, the other way darker and grittier");
    check (lp (0, 0.8f, 0, 0, 0).tilt < -0.3f && lp (0, 0.8f, 0, 0, 0).smear > 0.2f && lp (0, -0.8f, 0, 0, 0).width > 0.5f && lp (0, -0.8f, 0, 0, 0).tilt > 0.3f,
           who + ": PINCH squeezed is darker and denser, widened is brighter and wide");
    check (lp (0, 0, 0, 0.8f, 0).shatter > 0.5f && lp (0, 0, 0, 0.8f, 0).wobble > 0.4f, who + ": a shake breaks it up and wobbles it");
    check (! lp (0, 0, 0, 0, 0.6f).isZero() && lp (0, 0, 0, 0, 0.6f).tilt < 0.0f, who + ": just bending the grabbed part already colours the sound");
    // the hand lets go without a gesture: it stops
    p.endBodyTouch();
    record (60);
    check (! p.bodyTouching() && p.player.playingSlot() < 0 && block (nullptr) < 1e-6f, who + ": the hand off with no gesture made: it stops, silent");
    // the hand lets go after a gesture: the live sound holds; when the real result lands, it plays from its start
    p.beginBodyTouch (0.5f);
    Processor::BodyTouch pl; pl.pull = 0.7f; p.setBodyTouch (pl);
    record (40);
    const float headHeld = p.player.playhead();
    check (p.player.playingSlot() == Player::Touch && headHeld > 0.5f && headHeld < 0.62f, who + ": grabbed in the middle, it plays from the middle (" + juce::String (headHeld, 2) + ")");
    p.bodyResultLanded();
    record (4);
    check (! p.bodyTouching() && p.player.playingSlot() == Player::Processed && p.player.playhead() < 0.05f, who + ": the real result takes over, from its start");
    float worstStep = 0, prev = 0;
    { std::vector<float> one ((size_t) bs); for (int k = 0; k < 60; ++k) { block (one.data()); for (int i = 0; i < bs; ++i) { if (k > 0 || i > 0) worstStep = juce::jmax (worstStep, std::abs (one[(size_t) i] - prev)); prev = one[(size_t) i]; } } }
    check (worstStep < 0.25f, who + ": the live chain lets go without a click (largest step " + juce::String (worstStep, 3) + ")");
    p.player.stop(); record (20);
    // something was playing when the hand landed: it goes back to it
    p.player.play (Player::Original, 0.0); record (20);
    p.beginBodyTouch (0.9f); record (20);
    const bool wasTouch = p.player.playingSlot() == Player::Touch && p.player.playhead() < 0.3f;
    p.endBodyTouch(); record (20);
    check (wasTouch && p.player.playingSlot() == Player::Original, who + ": the original was playing: the hand takes over from the same place, and gives it back");
    p.player.stop(); record (10);
    p.engine.sessionFolder().deleteRecursively();
}

// the whole path through the window: a drag on the body starts the live sound, letting go runs the real process, and when
// it lands the new sound plays
static void testBodyInEditor (const std::shared_ptr<Audio>& voice)
{
    Processor p;
    p.setPlayConfigDetails (0, 2, 48000.0, 256);
    p.prepareToPlay (48000.0, 256);
    p.engine.setSpecimen (voice, "voice");
    p.syncPlayer();
    std::unique_ptr<Editor> ed (dynamic_cast<Editor*> (p.createEditor()));
    ed->setSize (Editor::kW, Editor::kH); ed->refresh();
    auto& b = ed->body();
    juce::AudioBuffer<float> buf (2, 256); juce::MidiBuffer midi;
    auto pump = [&] (int blocks) { for (int k = 0; k < blocks; ++k) { buf.clear(); p.processBlock (buf, midi); } };
    const float cy = (float) b.getHeight() * 0.5f, w = (float) b.getWidth();
    // a tap: plays, and again: stops (no live chain)
    b.press ({ w * 0.5f, cy }); b.release ({ w * 0.5f, cy }); pump (8);
    check (p.player.playingSlot() == Player::Processed && ! p.bodyTouching(), "a tap on the body plays the processed sound (as before)");
    b.press ({ w * 0.5f, cy }); b.release ({ w * 0.5f, cy }); pump (8);
    check (p.player.playingSlot() < 0, "another tap stops it");
    // press without moving: still nothing; move: the live sound starts, from where it was grabbed
    b.press ({ w * 0.3f, cy }); pump (4);
    check (! p.bodyTouching() && p.player.playingSlot() < 0, "a hand that has not moved yet plays nothing");
    b.dragTo ({ w * 0.3f + 30.0f, cy }); pump (4);
    check (p.bodyTouching() && p.player.playingSlot() == Player::Touch, "the moment it moves, the sound plays under the hand");
    b.dragTo ({ w * 0.6f, cy + 2.0f }); pump (4);
    check (p.bodyLiveNow().smear > 0.1f, "pulling it changes the live sound (smear " + juce::String (p.bodyLiveNow().smear, 2) + ")");
    const int before = p.engine.historySize();
    b.release ({ w * 0.6f, cy + 2.0f }); pump (4);
    check (p.engine.busy() && p.bodyTouching() && p.player.playingSlot() == Player::Touch, "let go: the real PULL runs while the live sound holds");
    for (int i = 0; i < 1500 && p.engine.busy(); ++i) { juce::MessageManager::getInstance()->runDispatchLoopUntil (10); pump (2); }
    juce::MessageManager::getInstance()->runDispatchLoopUntil (20); pump (6);
    check (p.engine.historySize() == before + 1 && p.engine.current().label.startsWith ("PULL"), "the real process ran (" + p.engine.current().label + ")");
    check (! p.bodyTouching() && p.player.playingSlot() == Player::Processed && p.player.playhead() < 0.2f, "and the real result plays from its start when it lands");
    // closing the window with a hand on it: the live sound stops with it
    p.player.stop(); pump (10);
    b.press ({ w * 0.4f, cy }); b.dragTo ({ w * 0.4f + 6.0f, cy + 3.0f }); pump (4);
    const bool live = p.bodyTouching();
    ed.reset(); pump (20);
    check (live && ! p.bodyTouching() && p.player.playingSlot() < 0, "closing the window with a hand on the body stops the live sound");
    p.engine.sessionFolder().deleteRecursively();
}

static void testState (const std::shared_ptr<Audio>& voice)
{
    juce::MemoryBlock mb;
    juce::String id;
    {
        Processor p;
        p.engine.setSpecimen (voice, "voice");
        juce::String err;
        p.engine.applyNow ({ Op::Smear, {}, 1 }, err);
        p.engine.chain[3].on = true;
        p.refreshState();
        p.getStateInformation (mb);
        id = p.engine.sessionId;
        // the session WAVs are written on the worker: wait for them
        for (int i = 0; i < 200 && ! p.engine.sessionFolder().getChildFile ("current.wav").existsAsFile(); ++i) juce::Thread::sleep (20);
        juce::Thread::sleep (300);
    }
    Processor q;
    q.setStateInformation (mb.getData(), (int) mb.getSize());
    for (int i = 0; i < 50 && ! q.engine.hasSpecimen(); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    check (q.engine.hasSpecimen() && q.engine.sessionId == id && q.engine.historySize() == 2 && q.engine.chain[3].on,
           "the project re-opens with its specimen, its processed sound and its chain");
    check (q.engine.hasSpecimen() && bitExact (*q.engine.originalAudio(), *voice), "the re-opened original is bit-exact");
    q.engine.sessionFolder().deleteRecursively();
}

// ------------------------------------------------------------------------------------------------ host proxy (what FL does)
// Ghost Lab (instrument) and Ghost Lab FX (effect) are driven the way a host drives them: bus layouts, prepareToPlay at
// several rates / block sizes, latency, processBlock. The FX build of this harness (GhostLabFXTests) runs the same code.
static void testPlugin (const std::shared_ptr<Audio>& voice)
{
    const juce::String who = Processor::isFx ? "FX" : "instrument";
    Processor p;
    p.setSpectreLive (false);   // (FX: with SPECTRE's LIVE off the track plays dry, as in a session from before build 17; SPECTRE's own tests cover LIVE)
    juce::AudioProcessor::BusesLayout stereo, mono;
    stereo.outputBuses.add (juce::AudioChannelSet::stereo()); mono.outputBuses.add (juce::AudioChannelSet::mono());
    if (Processor::isFx) { stereo.inputBuses.add (juce::AudioChannelSet::stereo()); mono.inputBuses.add (juce::AudioChannelSet::mono()); }
    check (p.checkBusesLayoutSupported (stereo) && p.checkBusesLayoutSupported (mono), who + ": stereo and mono layouts are accepted");
    check (p.acceptsMidi() == ! Processor::isFx, who + ": MIDI input " + (Processor::isFx ? "off (an effect)" : "on (an instrument)"));
    bool latencyOk = true, passOk = true;
    double worst = 0;
    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int bs : { 32, 256, 1024, 2048 })
        {
            p.setRateAndBufferSizeDetails (sr, bs);
            p.prepareToPlay (sr, bs);
            latencyOk = latencyOk && p.getLatencySamples() == 0;
            juce::AudioBuffer<float> buf (2, bs), ref (2, bs);
            juce::MidiBuffer midi;
            for (int blk = 0; blk < 8; ++blk)
            {
                for (int c = 0; c < 2; ++c) for (int i = 0; i < bs; ++i)
                    ref.setSample (c, i, 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * (440.0 + 110.0 * c) * (blk * bs + i) / sr));
                buf.makeCopyOf (ref);
                p.processBlock (buf, midi);
                for (int c = 0; c < 2; ++c) for (int i = 0; i < bs; ++i)
                {
                    const float want = Processor::isFx ? ref.getSample (c, i) : 0.0f;   // FX: untouched; instrument: silent until a note
                    worst = juce::jmax (worst, (double) std::abs (buf.getSample (c, i) - want));
                }
            }
            p.releaseResources();
        }
    passOk = worst == 0.0;
    check (latencyOk, who + ": reports 0 samples of latency at 44.1 / 48 / 96 kHz, blocks 32-2048 (it adds none: processing is offline)");
    check (passOk, who + (Processor::isFx ? ": passes the track's audio through bit-exact" : ": silent with no notes and no input") + " (largest difference " + juce::String (worst, 8) + ")");
    p.setRateAndBufferSizeDetails (48000.0, 512);
    p.prepareToPlay (48000.0, 512);
    if (Processor::isFx)
    {
        // CAPTURE: what plays through the effect becomes the specimen
        p.startCapture();
        juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer midi;
        for (int blk = 0; blk < 100; ++blk)
        {
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) buf.setSample (c, i, 0.25f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 330.0 * (blk * 512 + i) / 48000.0));
            p.processBlock (buf, midi);
        }
        auto cap = p.stopCapture();
        check (cap != nullptr && std::abs (cap->length() - 100 * 512) < 2 && cap->sampleRate == 48000.0, "FX: CAPTURE records what plays through it (" + juce::String (cap ? cap->length() : 0) + " samples)");
        // mono track
        p.releaseResources();
        p.setBusesLayout (mono);
        p.prepareToPlay (48000.0, 256);
        juce::AudioBuffer<float> m (1, 256), mr (1, 256);
        for (int i = 0; i < 256; ++i) mr.setSample (0, i, 0.2f * (float) std::sin (0.05 * i));
        m.makeCopyOf (mr);
        p.processBlock (m, midi);
        float d = 0; for (int i = 0; i < 256; ++i) d = juce::jmax (d, std::abs (m.getSample (0, i) - mr.getSample (0, i)));
        check (d == 0.0f, "FX: a mono track passes through bit-exact too");
    }
    else
    {
        // a note plays the processed specimen; note off stops it
        p.engine.setSpecimen (voice, "voice");
        p.syncPlayer();
        juce::Thread::sleep (300);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
        juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        float peak = 0;
        for (int blk = 0; blk < 40; ++blk) { buf.clear(); p.processBlock (buf, midi); midi.clear(); peak = juce::jmax (peak, buf.getMagnitude (0, 512)); }
        check (peak > 0.01f, "instrument: a MIDI note plays the specimen (peak " + juce::String (peak, 3) + ")");
        // drag-in: a WAV dragged from FL's browser onto the body / the window loads as the specimen
        Processor q;
        std::unique_ptr<Editor> ed (dynamic_cast<Editor*> (q.createEditor()));
        ed->setSize (Editor::kW, Editor::kH);
        auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ghostlab-dragin-test.wav");
        juce::String err;
        writeWav (*voice, tmp, 24, err);
        const juce::StringArray files { tmp.getFullPathName() };
        check (ed->body().isInterestedInFileDrag (files), "instrument: the body accepts a WAV dragged in from FL");
        check (! ed->body().isInterestedInFileDrag ({ "/tmp/notes.txt" }), "instrument: it refuses a file it can't open");
        ed->body().filesDropped (files, 100, 100);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (200);
        check (q.engine.hasSpecimen() && q.engine.originalAudio() != nullptr && std::abs (q.engine.originalAudio()->length() - voice->length()) < 2,
               "instrument: dropping it loads it as SPECIMEN 001");
        tmp.deleteFile();
        ed.reset();
        q.engine.sessionFolder().deleteRecursively();
    }
    p.releaseResources();
    p.engine.sessionFolder().deleteRecursively();
}

// ------------------------------------------------------------------------------------------------ the smoke body: cost + it follows the gestures
static void testSmoke (const std::shared_ptr<Audio>& voice)
{
    Processor p;
    p.engine.setSpecimen (voice, "voice");
    std::unique_ptr<Editor> ed (dynamic_cast<Editor*> (p.createEditor()));
    for (auto size : { juce::Point<int> (Editor::kW, Editor::kH), juce::Point<int> (430, 932), juce::Point<int> (Editor::kW * 3 / 2, Editor::kH * 3 / 2) })
    {
        ed->setSize (size.x, size.y); ed->refresh(); ed->body().settle (3.0);
        for (bool touching : { false, true })
        {
            double ms = 1e9;
            for (int run = 0; run < 3; ++run) ms = juce::jmin (ms, ed->body().benchmarkFrameMs (180, touching));   // best of 3 (shared CI machines are noisy)
            const double cpu = 100.0 * ms / (1000.0 / 60.0);
            check (cpu < 10.0, "smoke body " + juce::String (ed->body().getWidth()) + "x" + juce::String (ed->body().getHeight()) + (touching ? " while touched" : " breathing")
                                + " at 60 fps: " + juce::String (ms, 2) + " ms a frame = " + juce::String (cpu, 1) + "% of one core (< 10%)");
        }
    }
    ed->setSize (Editor::kW, Editor::kH); ed->refresh(); ed->body().settle (3.0);
    {
        // build 11: on a Retina screen the body is drawn at the screen's own pixels (four times as many)
        double ms = 1e9;
        for (int run = 0; run < 3; ++run) ms = juce::jmin (ms, ed->body().benchmarkFrameMs (180, true, 2.0f));
        const double cpu = 100.0 * ms / (1000.0 / 60.0);
        check (cpu < 15.0, "smoke body " + juce::String (ed->body().getWidth()) + "x" + juce::String (ed->body().getHeight()) + " on a Retina screen (drawn at 2x) while touched: "
                            + juce::String (ms, 2) + " ms a frame = " + juce::String (cpu, 1) + "% of one core (< 15%)");
    }
    auto& b = ed->body();
    auto snap = [&b] { return b.createComponentSnapshot (b.getLocalBounds(), true, 1.0f); };
    auto diff = [] (const juce::Image& x, const juce::Image& y)
    {
        double s = 0; int n = 0;
        for (int yy = 0; yy < x.getHeight(); yy += 2) for (int xx = 0; xx < x.getWidth(); xx += 2)
        { auto c1 = x.getPixelAt (xx, yy), c2 = y.getPixelAt (xx, yy); s += std::abs (c1.getBrightness() - c2.getBrightness()); ++n; }
        return s / juce::jmax (1, n);
    };
    auto rest = snap();
    // the body is a filled thing of light, not a few thin lines: a good part of the glass is brightly lit (the dark
    // liquid behind it is nowhere near this bright), and some of it burns near white (the core, where ribbons cross)
    int lit = 0, hot = 0, total = 0;
    for (int yy = 0; yy < rest.getHeight(); yy += 2) for (int xx = 0; xx < rest.getWidth(); xx += 2)
    { ++total; const auto c = rest.getPixelAt (xx, yy); if (c.getBrightness() > 0.6f) ++lit; if (juce::jmin (c.getRed(), c.getGreen(), c.getBlue()) > 190) ++hot; }
    check (lit > total / 8 && hot > total / 400, "the body is a filled thing of light (" + juce::String (100.0 * lit / total, 1) + "% of the glass brightly lit, " + juce::String (100.0 * hot / total, 1) + "% white-hot)");
    for (auto g : { std::make_tuple (0.6f, 0.0f, 0.0f, "PULL"), std::make_tuple (0.0f, 0.7f, 0.0f, "PINCH"), std::make_tuple (0.0f, 0.0f, 0.8f, "TWIST") })
    {
        b.setDeformation (std::get<0> (g), std::get<1> (g), std::get<2> (g));
        const double d = diff (rest, snap());
        check (d > 0.01, juce::String ("the smoke deforms with ") + std::get<3> (g) + " (mean change " + juce::String (d, 3) + ")");
        b.setDeformation (0, 0, 0);
    }
    Profile flat = makeProfile (*voice);
    for (auto& v : flat.energy) v *= 0.2f;
    b.setProfile (flat); b.settle (3.0);
    check (diff (rest, snap()) > 0.01, "the smoke follows the sound's spectrum (a quieter profile changes it)");
    {
        // build 11: the body lays its light over its own copy of the tube's liquid. Where its light has died away (all
        // round its edge, with a silent sound in it) that copy must be the page's own liquid, or its box would show.
        // On a normal screen and a Retina one.
        Profile silent = flat;
        for (auto& v : silent.energy) v = 0.0f;
        for (auto& v : silent.bands) v = 0.0f;
        b.setProfile (silent); b.settle (3.0);
        auto& content = ed->contentComponent();
        const auto box = b.getBounds();
        for (float scale : { 1.0f, 2.0f })
        {
            b.setVisible (false);
            const auto page = content.createComponentSnapshot (box, true, scale);
            b.setVisible (true);
            const auto own = b.createComponentSnapshot (b.getLocalBounds(), true, scale);
            int worst = 0;
            auto cmp = [&] (int x, int y)
            {
                const auto p1 = page.getPixelAt (x, y), p2 = own.getPixelAt (x, y);
                worst = juce::jmax (worst, juce::jmax (std::abs (p1.getRed() - p2.getRed()), std::abs (p1.getGreen() - p2.getGreen()), std::abs (p1.getBlue() - p2.getBlue())));
            };
            const int w = juce::jmin (page.getWidth(), own.getWidth()), h = juce::jmin (page.getHeight(), own.getHeight());
            const int in = juce::roundToInt (3.0f * scale);   // (its two ends sit under the lips of the tube's collars)
            for (int x = in; x < w - in; ++x) { cmp (x, 0); cmp (x, h - 1); }
            for (int y = 0; y < h; ++y) { cmp (in, y); cmp (w - 1 - in, y); }
            check (page.getWidth() == own.getWidth() && worst <= 6, "the liquid under the body is the page's own, all round its edge (at " + juce::String (scale, 0) + "x: largest difference "
                                                                    + juce::String (worst) + " of 255)");
        }
    }
    ed.reset();
    p.engine.sessionFolder().deleteRecursively();
}

// ------------------------------------------------------------------------------------------------ screenshots + demos
static void savePng (juce::Component& c, const juce::File& f, float scale = 1.0f)
{
    auto im = c.createComponentSnapshot (c.getLocalBounds(), true, scale);
    f.getParentDirectory().createDirectory(); f.deleteFile();
    juce::FileOutputStream os (f); juce::PNGImageFormat png; png.writeImageToStream (im, os);
    std::printf ("shot  %s\n", f.getFullPathName().toRawUTF8());
}

// GHOSTLAB_DEMO_SOURCES=<dir with voice.wav, piano.wav, drums.wav>: render demos / shots from real recordings
// (kept outside the repo with their licences). Without it, our own synthesised voice and piano are used.
static std::shared_ptr<Audio> demoSource (const juce::String& name)
{
    const auto dir = juce::SystemStats::getEnvironmentVariable ("GHOSTLAB_DEMO_SOURCES", {});
    if (dir.isEmpty()) return nullptr;
    juce::String err;
    auto a = loadAudio (juce::File (dir).getChildFile (name + ".wav"), err);
    if (a == nullptr) std::printf ("demo source %s: %s\n", name.toRawUTF8(), err.toRawUTF8());
    return a;
}

static void shots (const juce::File& dir)
{
    auto voice = synthVoice(), piano = synthPiano();
    if (auto v = demoSource ("voice")) voice = v;
    if (auto pn = demoSource ("piano")) piano = pn;
    Processor p;
    p.engine.setSpecimen (voice, "voice");
    p.engine.setPartner (piano, "piano");
    juce::String err;
    p.engine.applyNow ({ Op::SpectralMelt, {}, 1 }, err);
    p.syncPlayer();
    std::unique_ptr<Editor> ed (dynamic_cast<Editor*> (p.createEditor()));
    ed->setSize (Editor::kW, Editor::kH);
    ed->refresh();
    ed->body().settle (3.0);
    savePng (*ed, dir.getChildFile ("ghostlab-wide.png"));
    ed->body().setDeformation (0.0f, 0.0f, 0.7f);
    savePng (*ed, dir.getChildFile ("ghostlab-wide-twisting.png"));
    ed->body().setDeformation (0.0f, 0.0f, 0.0f);
    ed->setDeep (true); ed->card (Op::GrainCloud); ed->refresh();
    savePng (*ed, dir.getChildFile ("ghostlab-wide-deep.png"));
    ed->setDeep (false);
    // the TERRAIN page (the landscape in Ghost Lab's colours), a mountain selected
    {
        using namespace ghost::terrain;
        Terrain t; t.add ({ hzToX (180.0f), 0.42f, 0.72f, 0.05f, 0.15f, 0.0f }); t.add ({ hzToX (900.0f), 0.56f, 0.62f, 0.035f, 0.13f, 0.1f }); t.add ({ hzToX (3200.0f), 0.38f, 0.48f, 0.045f, 0.11f, 0.0f }); t.seed = 5;
        p.terrain.setWithoutUndo (t);
        ed->setTerrainPage (true);
        for (int i = 0; i < 400 && p.engine.busy(); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        p.terrain.select (1);
        for (int i = 0; i < 12; ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (20);   // (the mountains grow in, the lit tint eases)
        ed->terrainView().settle();
        ed->refresh();
        savePng (*ed, dir.getChildFile ("ghostlab-terrain.png"));
        savePng (*ed, dir.getChildFile ("ghostlab-terrain@2x.png"), 2.0f);
        ed->setSize (430, 932); ed->refresh();
        savePng (ed->contentComponent(), dir.getChildFile ("ghostlab-terrain-narrow.png"));
        ed->setTerrainPage (false);
        ed->setSize (Editor::kW, Editor::kH);
    }
    ed->setSize (430, 932);
    ed->refresh(); ed->body().settle (3.0);
    // the narrow layout scrolls: shoot the whole content too
    savePng (*ed, dir.getChildFile ("ghostlab-narrow.png"));
    savePng (ed->contentComponent(), dir.getChildFile ("ghostlab-narrow-full.png"));
    // the UNDER THE HOOD page: a process picked, its own settings filled in, after a run
    {
        ed->setSize (Editor::kW, Editor::kH);
        ed->setHoodPage (true);
        auto& h = ed->hood();
        h.selectProcess ("modify", "brassage"); h.setMode (6);
        for (auto kv : std::map<juce::String, juce::String> { { "velocity", "1" }, { "density", "0.9" }, { "grainsize", "70" }, { "pitchshft", "3" }, { "amp", "1" }, { "space", "0.5" }, { "bsplice", "5" }, { "esplice", "5" } })
            h.setValue (kv.first, kv.second);
        ed->refresh();
        savePng (*ed, dir.getChildFile ("ghostlab-hood.png"));
        h.run();
        for (int i = 0; i < 400 && p.engine.busy(); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        h.selectProcess ("blur", "chorus"); h.setMode (5);
        ed->refresh();
        savePng (*ed, dir.getChildFile ("ghostlab-hood-spectral.png"));
        h.setSearch ("freez");
        ed->refresh();
        savePng (*ed, dir.getChildFile ("ghostlab-hood-search.png"));
        h.setSearch ({});
        // the ORIGINAL pages: the list and a spread of parameter pages
        ed->hideToast();
        auto page = [&] (const char* prog, const char* name, int mode, const char* file)
        {
            h.selectProcess (prog, name); if (mode > 0) h.setMode (mode);
            ed->refresh(); ed->hideToast();
            savePng (*ed, dir.getChildFile (file));
        };
        page ("stretch", "time", 1, "ghostlab-hood-01-stretch-time.png");
        page ("modify", "brassage", 7, "ghostlab-hood-02-brassage-7.png");
        page ("strange", "glis", 1, "ghostlab-hood-03-glis-formants.png");
        page ("focus", "freeze", 1, "ghostlab-hood-04-freeze-datafile.png");
        page ("pvoc", "anal", 1, "ghostlab-hood-05-pvoc-anal.png");
        page ("modify", "radical", 3, "ghostlab-hood-06-radical-3.png");
        h.selectProcess ("modify", "speed"); h.setMode (2); h.setValue ("semitone-transpos", "0 -3 2 4"); ed->refresh();
        savePng (*ed, dir.getChildFile ("ghostlab-hood-07-speed-brk.png"));
        h.selectProcess ("blur", "drunk"); h.setShowInformation (true); ed->refresh();
        savePng (*ed, dir.getChildFile ("ghostlab-hood-08-information.png"));
        h.setShowInformation (false);
        savePng (h, dir.getChildFile ("ghostlab-hood-09-page-only.png"));
        // the data-file pages: combine make2 with its three files made from the sound, sausage with several sounds, the extractors
        p.engine.setPartner (synthPiano(), "piano");
        h.selectProcess ("combine", "make2");
        h.makeDataNow ("pitchfile", p.engine.processedAudio()); h.makeDataNow ("formantfile", p.engine.processedAudio()); h.makeDataNow ("envfile", p.engine.processedAudio());
        ed->refresh(); ed->hideToast();
        savePng (*ed, dir.getChildFile ("ghostlab-hood-10-make2-files-made.png"));
        h.selectProcess ("modify", "sausage");
        h.setExtraFiles ({ hood::writeSoundFile (*p.engine.partnerAudio(), h.dataFolder(), "specimen-002").getFullPathName(),
                           hood::writeSoundFile (*p.engine.snapshot (0).audio, h.dataFolder(), "result-0").getFullPathName() });
        ed->refresh(); ed->hideToast();
        savePng (*ed, dir.getChildFile ("ghostlab-hood-11-sausage-sounds.png"));
        page ("repitch", "getpitch", 1, "ghostlab-hood-12-getpitch.png");
        page ("formants", "get", 0, "ghostlab-hood-13-formants-get.png");
        p.engine.clearPartner();
        ed->setSize (430, 932); ed->refresh();
        savePng (ed->contentComponent(), dir.getChildFile ("ghostlab-hood-narrow.png"));
        ed->setHoodPage (false);
        ed->setSize (Editor::kW, Editor::kH);
    }
    // empty state
    Processor e;
    std::unique_ptr<Editor> ed2 (dynamic_cast<Editor*> (e.createEditor()));
    ed2->setSize (Editor::kW, Editor::kH); ed2->refresh();
    savePng (*ed2, dir.getChildFile ("ghostlab-empty.png"));
    // the window filled at any shape (no empty bands): a wide one and a tall one
    ed->setSize (1900, 1000); ed->refresh(); ed->body().settle (3.0);
    savePng (*ed, dir.getChildFile ("ghostlab-fill-1900x1000.png"), 0.6f);
    ed->setSize (1100, 900); ed->refresh(); ed->body().settle (3.0);
    savePng (*ed, dir.getChildFile ("ghostlab-fill-1100x900.png"), 0.6f);
    ed->setHoodPage (true); ed->setSize (1900, 1000); ed->refresh();
    savePng (*ed, dir.getChildFile ("ghostlab-fill-hood-1900x1000.png"), 0.6f);
    ed->setHoodPage (false);
    ed.reset(); ed2.reset();
    p.engine.sessionFolder().deleteRecursively(); e.engine.sessionFolder().deleteRecursively();
}

// GhostLabTests bodyshots DIR: THE LIVING SOUND and its tube on their own, at rest and in the hand (pulled, pinched,
// twisted, shaken, grabbed), on a normal and a Retina screen, empty, and on the phone-width page. Quick to run: for
// looking at the body while working on it.

// the rooms' pages (build 13): each room as it opens, and after it has run
static void roomShots (const juce::File& dir)
{
    dir.createDirectory();
    auto voice = synthVoice();
    if (auto v = demoSource ("voice")) voice = v;
    Processor p;
    p.engine.setSpecimen (voice, "voice");
    p.syncPlayer();
    std::unique_ptr<Editor> ed (dynamic_cast<Editor*> (p.createEditor()));
    ed->setSize (Editor::kW, Editor::kH);
    ed->refresh();
    ed->body().settle (3.0);
    juce::String err;
    auto shot = [&] (const juce::String& name) { ed->hideToast(); ed->refresh(); savePng (*ed, dir.getChildFile ("ghostlab-room-" + name + ".png")); };
    shot ("0-lab");
    {
        // TRIM: ORIGINAL's start handle held part way in (the shot is taken mid-drag)
        auto& w = ed->waveComponent (false);
        const auto wa = w.getLocalBounds().toFloat().reduced (16, 0).withTrimmedTop (32).withTrimmedBottom (10);
        const juce::Point<float> a (wa.getX(), wa.getCentreY()), b (wa.getX() + wa.getWidth() * 0.22f, wa.getCentreY());
        const auto t = juce::Time::getCurrentTime();
        auto ev = [&] (juce::Point<float> pos) { return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, juce::ModifierKeys::leftButtonModifier, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &w, &w, t, a, t, 1, false); };
        w.mouseDown (ev (a)); w.mouseDrag (ev (b));
        ed->hideToast(); savePng (*ed, dir.getChildFile ("ghostlab-room-0-lab-trimming.png"));
        w.mouseUp (ev (b));
        shot ("0-lab-trimmed");
        ed->undo();
    }
    ed->setRoom (rooms::Room::Stretch); shot ("1-stretch-before");
    ed->roomPage().setModule (4, true); ed->roomPage().setValue ("pxs.octavemix1_0", 0.5f);
    ed->roomPage().setModule (7, true); ed->roomPage().addFreeFilterNode (0.3, 0.2); ed->roomPage().addFreeFilterNode (0.55, 0.9);
    { Step s = ed->settingsFor (Op::XStretch); s.op = Op::XStretch; p.engine.applyNow (s, err); }
    shot ("1-stretch-after");
    ed->roomPage().scrollToGroup ("Ratios (octave mixer)"); shot ("1-stretch-ratios");
    ed->roomPage().scrollToGroup ("Free filter"); shot ("1-stretch-free-filter");
    p.engine.undo();
    ed->setRoom (rooms::Room::Reborn); shot ("2-reborn");
    if (! rooms::rebornAvailable())
    {
        // (this build has no neural instruments: the page as it looks with them, for the picture only)
        rooms::pretendInstrumentsForShots ({ "Flute", "Violin", "Trumpet", "Saxophone", "Bassoon", "Clarinet", "Melodica", "Sitar", "Trombone", "Tuba", "Vowels" });
        ed->setRoom (rooms::Room::Space); ed->setRoom (rooms::Room::Reborn);
        ed->roomPage().pickInstrument (0); shot ("2-reborn-flute");
        ed->roomPage().setValue ("ddsp.PitchShift", 0.0f); shot ("2-reborn-flute-too-low");
        rooms::pretendInstrumentsForShots ({});
    }
    ed->setRoom (rooms::Room::Space); shot ("3-space");
    ed->setSize (1900, 1000); shot ("3-space-1900x1000");
    ed->setRoom (rooms::Room::Stretch); shot ("1-stretch-1900x1000");
    ed->setSize (430, 900); shot ("1-stretch-narrow");
}

static void bodyShots (const juce::File& dir)
{
    auto voice = synthVoice(), piano = synthPiano();
    if (auto v = demoSource ("voice")) voice = v;
    Processor p;
    p.engine.setSpecimen (voice, "voice");
    p.engine.setPartner (piano, "piano");
    juce::String err;
    p.engine.applyNow ({ Op::SpectralMelt, {}, 1 }, err);
    p.syncPlayer();
    std::unique_ptr<Editor> ed (dynamic_cast<Editor*> (p.createEditor()));
    ed->setSize (Editor::kW, Editor::kH);
    ed->refresh();
    auto& b = ed->body();
    auto& content = ed->contentComponent();
    auto shot = [&] (const juce::String& name, float scale = 1.0f)
    {
        const auto area = b.getBounds().expanded (ed->isNarrow() ? 14 : 96, ed->isNarrow() ? 14 : 20).withTrimmedBottom (ed->isNarrow() ? -70 : -52).getIntersection (content.getLocalBounds());
        auto im = content.createComponentSnapshot (area, true, scale);
        auto f = dir.getChildFile (name + ".png");
        f.getParentDirectory().createDirectory(); f.deleteFile();
        juce::FileOutputStream os (f); juce::PNGImageFormat png; png.writeImageToStream (im, os);
        std::printf ("shot  %s\n", f.getFullPathName().toRawUTF8());
    };
    b.settle (3.0);
    shot ("body-rest");
    shot ("body-rest@2x", 2.0f);
    b.settle (11.0); shot ("body-rest-later");
    b.settle (3.0);
    b.setDeformation (0.0f, 0.0f, 0.7f); shot ("body-twist"); shot ("body-twist@2x", 2.0f);
    b.setDeformation (0.0f, 0.0f, -1.0f); shot ("body-twist-back");
    b.setDeformation (0.7f, 0.0f, 0.0f, 0.0f, 0.0f, 0.18f, 0.4f); shot ("body-pull");
    b.setDeformation (0.0f, 0.8f, 0.0f, 0.0f, 0.3f, 0.0f, 0.5f); shot ("body-pinch");
    b.setDeformation (0.0f, -0.8f, 0.0f, 0.0f, -0.3f, 0.0f, 0.5f); shot ("body-widen");
    b.setDeformation (0.0f, 0.0f, 0.0f, 0.9f); shot ("body-shake");
    b.setDeformation (0.0f, 0.0f, 0.0f, 0.0f, -0.9f, 0.1f, 0.3f); shot ("body-grab");
    b.setDeformation (0.0f, 0.0f, 0.0f);
    // a hand on it, mid-gesture (the name plate names the gesture and says what letting go will do)
    {
        const auto run = b.onGesture; b.onGesture = nullptr;   // (the shots only look: letting go runs nothing)
        const float w = (float) b.getWidth(), h = (float) b.getHeight();
        b.press ({ w * 0.3f, h * 0.5f }); b.dragTo ({ w * 0.3f + 8.0f, h * 0.5f + 14.0f }); b.advance (14); shot ("body-hand-grab");
        b.dragTo ({ w * 0.62f, h * 0.5f + 6.0f }); b.advance (14); shot ("body-hand-pull"); shot ("body-hand-pull@2x", 2.0f);
        b.release ({ w * 0.62f, h * 0.5f + 6.0f }); b.advance (5); shot ("body-hand-letgo");
        b.advance (200);
        b.press ({ w * 0.6f + 46.0f, h * 0.5f });
        for (int i = 1; i <= 26; ++i) { b.dragTo ({ w * 0.6f + 46.0f * std::cos (i * 0.2f), h * 0.5f + 46.0f * std::sin (i * 0.2f) }); b.advance (1); }
        b.advance (10); shot ("body-hand-twist");
        b.release ({ w * 0.6f + 46.0f * std::cos (5.2f), h * 0.5f + 46.0f * std::sin (5.2f) }); b.advance (200);
        // a long press freezes that moment; SPECIMEN 002 held over it offers to breed
        b.press ({ w * 0.45f, h * 0.5f }); juce::Thread::sleep (650); b.advance (1); b.release ({ w * 0.45f, h * 0.5f }); b.advance (20); shot ("body-freeze");
        b.advance (900);
        b.itemDragEnter (juce::DragAndDropTarget::SourceDetails ("specimen-002", nullptr, {})); b.advance (3); shot ("body-breed");
        b.itemDragExit (juce::DragAndDropTarget::SourceDetails ("specimen-002", nullptr, {}));
        b.onGesture = run;
        b.settle (3.0);
        ed->toast ("PULL 3.2x"); shot ("body-toast"); ed->hideToast();
    }
    b.setPlayhead (0.37f); b.settle (3.0); shot ("body-playing"); b.setPlayhead (-1.0f); b.settle (3.0);   // the pulse where the sound is playing
    ed->setDeep (true); ed->card (Op::GrainCloud); ed->refresh(); shot ("body-deep"); ed->setDeep (false); ed->refresh();
    // the voice untouched (its six syllables should show)
    p.engine.undo(); ed->refresh(); b.settle (3.0); shot ("body-voice");
    p.engine.setSpecimen (piano, "piano"); ed->refresh(); b.settle (3.0); shot ("body-piano");
    ed->setSize (1100, 900); ed->refresh(); b.settle (3.0); shot ("body-tall");
    ed->setSize (1900, 1000); ed->refresh(); b.settle (3.0); shot ("body-long");
    ed->setSize (430, 932); ed->refresh(); b.settle (3.0); shot ("body-narrow"); shot ("body-narrow@2x", 2.0f);
    {
        const auto run = b.onGesture; b.onGesture = nullptr;
        const float w = (float) b.getWidth(), h = (float) b.getHeight();
        b.press ({ w * 0.4f, h * 0.5f }); b.dragTo ({ w * 0.4f + 7.0f, h * 0.5f + 9.0f }); b.advance (12); shot ("body-narrow-hand");
        b.dragTo ({ w * 0.4f + 3.0f, h * 0.5f + 70.0f }); b.advance (12); shot ("body-narrow-pinch");
        b.release ({ w * 0.4f + 3.0f, h * 0.5f + 70.0f }); b.advance (200);
        b.onGesture = run;
    }
    Processor e;
    std::unique_ptr<Editor> ed2 (dynamic_cast<Editor*> (e.createEditor()));
    ed2->setSize (Editor::kW, Editor::kH); ed2->refresh(); ed2->body().settle (3.0);
    {
        auto& c2 = ed2->contentComponent();
        auto im = c2.createComponentSnapshot (ed2->body().getBounds().expanded (96, 20).withTrimmedBottom (-52).getIntersection (c2.getLocalBounds()), true, 1.0f);
        auto f = dir.getChildFile ("body-empty.png"); f.deleteFile();
        juce::FileOutputStream os (f); juce::PNGImageFormat png; png.writeImageToStream (im, os);
        std::printf ("shot  %s\n", f.getFullPathName().toRawUTF8());
    }
    ed.reset(); ed2.reset();
    p.engine.sessionFolder().deleteRecursively(); e.engine.sessionFolder().deleteRecursively();
}

// GhostLabTests bodydemos DIR: what a hand on THE LIVING SOUND sounds like, as WAVs - the voice going round through the
// body's live chain while it is held still (00), pulled, twisted each way, squeezed, widened and shaken (the hand eases
// in over the first second, holds, and lets go for the last one)
static void bodyDemos (const juce::File& dir)
{
    dir.createDirectory();
    auto voice = synthVoice();
    if (auto v = demoSource ("voice")) voice = v;
    struct Touch { const char* name; float pull, pinch, twist, shake; };
    for (auto tc : { Touch { "00-held-still", 0, 0, 0, 0 }, Touch { "01-pull", 0.8f, 0, 0, 0 }, Touch { "02-twist-clockwise", 0, 0, 0.9f, 0 }, Touch { "03-twist-anticlockwise", 0, 0, -0.9f, 0 },
                     Touch { "04-pinch-squeeze", 0, 0.9f, 0, 0 }, Touch { "05-pinch-widen", 0, -0.9f, 0, 0 }, Touch { "06-shake", 0, 0, 0, 0.9f } })
    {
        Processor p;
        const int bs = 256;
        p.setPlayConfigDetails (0, 2, 48000.0, bs);
        p.prepareToPlay (48000.0, bs);
        p.engine.setSpecimen (voice, "voice");
        p.syncPlayer();
        const int blocks = (int) (6.0 * 48000.0 / bs);
        Audio out; out.sampleRate = 48000.0; out.buf.setSize (2, blocks * bs);
        juce::AudioBuffer<float> buf (2, bs); juce::MidiBuffer midi;
        p.beginBodyTouch (0.0f);
        for (int k = 0; k < blocks; ++k)
        {
            const float sec = (float) (k * bs) / 48000.0f, amt = juce::jlimit (0.0f, 1.0f, juce::jmin (sec, 5.0f - sec));
            Processor::BodyTouch b; b.pull = tc.pull * amt; b.pinch = tc.pinch * amt; b.twist = tc.twist * amt; b.shake = tc.shake * amt;
            p.setBodyTouch (b);
            buf.clear(); p.processBlock (buf, midi);
            for (int c = 0; c < 2; ++c) out.buf.copyFrom (c, k * bs, buf, c, 0, bs);
        }
        juce::String err;
        auto f = dir.getChildFile (juce::String ("live-") + tc.name + ".wav");
        writeWav (out, f, 24, err);
        std::printf ("demo  %s  peak %.3f  rms %.4f\n", f.getFullPathName().toRawUTF8(), out.buf.getMagnitude (0, out.buf.getNumSamples()), out.buf.getRMSLevel (0, 0, out.buf.getNumSamples()));
        p.engine.sessionFolder().deleteRecursively();
    }
}

// TERRAIN demos: each source through a few landscapes (ORIGINAL first, then the sculpted sound), as WAVs
// ---- build 4: the land drives the lab's processes (LabTerrainMap.h), live while you drag, the exact render cross-fading in
static void testTerrainMap (const std::shared_ptr<Audio>& voice)
{
    using namespace ghost::terrain;
    const auto flat = mapLabLand (flatTerrain());
    check (! flat.any && flat.steps.empty() && flat.live.isZero() && flat.meters.empty(), "a flat land drives nothing");
    auto has = [] (const LabLand& l, Op op) { for (auto& s : l.steps) if (s.op == op) return true; return false; };
    const auto spires = mapLabLand (shapePreset ("SPIRE FOREST")), plateau = mapLabLand (shapePreset ("PLATEAU")), bad = mapLabLand (shapePreset ("BADLANDS"));
    const auto twinAlone = mapLabLand (shapePreset ("RIDGE"), 0.0f, false), twinPartner = mapLabLand (shapePreset ("RIDGE"), 0.0f, true);
    check (has (spires, Op::Shatter) && spires.live.shatter > 0.3f, "spires SHATTER (live " + juce::String (spires.live.shatter, 2) + ")");
    check (has (plateau, Op::Freeze) && plateau.live.freeze > 0.5f, "a plateau FREEZEs");
    check (has (bad, Op::GrainCloud) && bad.live.drive > 0.2f, "badlands make a GRAIN CLOUD");
    check (has (twinPartner, Op::Breed) && ! has (twinAlone, Op::Breed), "a ridge of merging mountains BREEDs with a second specimen (and only then)");
    juce::StringArray firsts;
    for (int q = 0; q < shapePresetCount(); ++q) { const auto l = mapLabLand (shapePreset (shapePresetName (q))); firsts.addIfNotAlreadyThere (l.meters.empty() ? juce::String() : juce::String (l.meters[0].name)); }
    check (firsts.size() >= 4, "the shape presets each push something different first (" + firsts.joinIntoString (", ") + ")");
    // an old landscape (v 1) keeps its sculpt-only sound; a new one runs the mapped processes when the engine is there
    {
        Terrain old = shapePreset ("SPIRE FOREST"); old.version = 1;
        Engine e; e.setSpecimen (voice, "voice");
        Step st; st.op = Op::Terrain; st.data = toString (old); st.values["strength"] = 1.0f;
        juce::String err;
        auto r = render (st, *voice, nullptr, (*e.cdp), nullptr);
        check (r.audio != nullptr && r.commands.size() == 1, "a landscape saved before keeps its sculpt-only render (" + juce::String (r.commands.size()) + " step)");
        if ((*e.cdp).available())
        {
            Terrain nw = shapePreset ("SPIRE FOREST");
            Step s2 = st; s2.data = toString (nw);
            auto r2 = render (s2, *voice, nullptr, (*e.cdp), nullptr);
            check (r2.audio != nullptr && r2.commands.joinIntoString ("|").contains ("radical"), "a spire forest runs SHATTER after the sculpt (" + juce::String (r2.commands.size()) + " commands)");
            // (SHATTER's engine program takes no seed: it seeds its pieces from the clock, so two renders match bit for bit
            //  only inside the same second. Each pair starts at the top of a second; if one still ticked over between
            //  them, the pair is rendered again.)
            bool same = false;
            for (int attempt = 0; attempt < 3 && ! same; ++attempt)
            {
                while (juce::Time::currentTimeMillis() % 1000 > 150) juce::Thread::sleep (20);
                auto ra = render (s2, *voice, nullptr, (*e.cdp), nullptr), rb = render (s2, *voice, nullptr, (*e.cdp), nullptr);
                same = ra.audio != nullptr && rb.audio != nullptr && bitExact (*ra.audio, *rb.audio);
            }
            check (same, "and renders the same every time");
        }
        e.sessionFolder().deleteRecursively();
    }
    // the processor: drag -> the live path at the next block; the exact render cross-fades in; the land moves -> live again
    {
        Processor p;
        p.setPlayConfigDetails (0, 2, 48000.0, 256);
        p.prepareToPlay (48000.0, 256);
        p.engine.setSpecimen (voice, "voice");
        p.syncPlayer();
        p.setTerrainPage (true);
        juce::AudioBuffer<float> buf (2, 256); juce::MidiBuffer midi;
        p.terrain.beginGesture();
        p.terrain.live() = shapePreset ("SPIRE FOREST"); p.terrain.changed();
        buf.clear(); p.processBlock (buf, midi);
        check (p.terrainListening(), "a drag on the page starts the listen when nothing plays");
        gAudioAllocs = 0; gAudioLocks = 0;
        for (int k = 0; k < 40; ++k) { auto t = p.terrain.terrain(); t[0].x = 0.3f + 0.01f * (float) k; p.terrain.live() = t; p.terrain.changed(); buf.clear(); tlAudio = true; p.processBlock (buf, midi); tlAudio = false; }
        p.terrain.endGesture();
        check (gAudioAllocs.load() == 0 && gAudioLocks.load() == 0, "the live path (bank + chain + cross-fade) allocates and locks nothing (" + juce::String (gAudioAllocs.load()) + ")");
        check (p.labLand().live.shatter > 0.3f && ! p.labLand().meters.empty(), "the readout shows what the land does (" + juce::String (p.labLand().meters.empty() ? "" : p.labLand().meters[0].name) + ")");
        std::printf ("  (drag -> audible: the next audio block, %.1f ms at 256 / 48 kHz)\n", p.liveLatencyMs());
        auto rendered = std::make_shared<Audio>(); rendered->sampleRate = 48000.0; rendered->buf.setSize (2, voice->length()); rendered->buf.clear();
        for (int i = 0; i < voice->length(); ++i) { rendered->buf.setSample (0, i, 0.2f * std::sin (0.05f * (float) i)); rendered->buf.setSample (1, i, 0.2f * std::sin (0.05f * (float) i)); }
        p.setExactPreview (rendered);
        for (int k = 0; k < 60; ++k) { buf.clear(); p.processBlock (buf, midi); }
        check (p.exactAudible(), "the exact render cross-fades in under the live path, without stopping");
        p.terrain.setWithoutUndo (shapePreset ("PLATEAU"));
        for (int k = 0; k < 60; ++k) { buf.clear(); p.processBlock (buf, midi); }
        check (! p.exactAudible() && p.terrainListening(), "the land moves: back to the live path (still playing)");
        p.setTerrainListening (false);
        p.engine.sessionFolder().deleteRecursively();
    }
}

static void terrainDemos (const juce::File& dir)
{
    using namespace ghost::terrain;
    dir.createDirectory();
    struct Land { const char* name; Terrain t; };
    std::vector<Land> lands;
    { Terrain t; t.add ({ hzToX (240.0f), 0.5f, 0.7f, 0.05f, 2.0f, 0.0f }); t.add ({ hzToX (1600.0f), 0.5f, 0.55f, 0.03f, 2.0f, 0.0f }); t.add ({ hzToX (6000.0f), 0.5f, -0.45f, 0.06f, 2.0f, 0.0f }); lands.push_back ({ "two-mountains-a-valley", t }); }
    { Terrain t; t.add ({ hzToX (700.0f), 0.5f, 0.9f, 0.016f, 2.0f, 0.55f }); t.grit = 0.12f; t.seed = 9; lands.push_back ({ "gritty-spire", t }); }
    { Terrain t; t.add ({ hzToX (260.0f), 0.18f, 0.8f, 0.03f, 0.16f, 0.0f }); t.add ({ hzToX (900.0f), 0.5f, 0.75f, 0.025f, 0.14f, 0.1f }); t.add ({ hzToX (3200.0f), 0.82f, 0.8f, 0.02f, 0.16f, 0.0f }); lands.push_back ({ "travelling-in-time", t }); }
    { Terrain t = randomTerrain (21); t.evolve = 1.0f; lands.push_back ({ "evolving", t }); }
    { Terrain t; t.add ({ hzToX (2000.0f), 0.5f, 0.5f, 0.2f, 2.0f, 0.0f }); t.add ({ hzToX (120.0f), 0.5f, -0.7f, 0.05f, 2.0f, 0.0f }); t.frozen = true; lands.push_back ({ "wide-plateau-frozen", t }); }
    // build 4: the shape presets (the land drives the lab's processes) and a wild shape built step by step
    for (int q = 0; q < shapePresetCount(); ++q) lands.push_back ({ nullptr, shapePreset (shapePresetName (q)) });
    std::vector<juce::String> names;
    for (auto& l : lands) names.push_back (l.name != nullptr ? juce::String (l.name) : juce::String());
    for (int q = 0; q < shapePresetCount(); ++q) names[lands.size() - (size_t) shapePresetCount() + (size_t) q] = "shape-" + juce::String (shapePresetName (q)).toLowerCase().replace (" ", "-");
    for (auto* name : { "voice", "drums" })
    {
        auto src = demoSource (name);
        if (src == nullptr && juce::String (name) == "voice") src = synthVoice();   // (no real sources: a synthesised voice)
        if (src == nullptr) continue;
        // building: one mountain, a second merging, spires appear, a plateau, rough ground - each stage 3 s, cross-faded
        std::vector<Terrain> stages;
        Terrain b = defaultTerrain(); stages.push_back (b);
        b.add ({ 0.62f, 0.55f, 0.6f, 0.06f, 0.06f, 0.0f }); stages.push_back (b);
        for (int i = 0; i < 4; ++i) { b.add ({ 0.25f + 0.15f * (float) i, 0.3f + 0.1f * (float) (i % 2), 1.1f, 0.012f, 0.012f, 0.0f }); } stages.push_back (b);
        b.add ({ 0.4f, 0.62f, 1.2f, 0.07f, 0.07f, 0.0f }); stages.push_back (b);
        for (auto& p : b) p.rough = 0.6f; b.grit = 0.3f; stages.push_back (b);
        juce::AudioBuffer<float> all (2, 0); const int seg = (int) (3.0 * src->sampleRate), fade = (int) (0.05 * src->sampleRate);
        juce::String err;
        for (size_t k = 0; k < stages.size(); ++k)
        {
            Engine e; e.setSpecimen (src, name);
            Step st; st.op = Op::Terrain; st.data = toString (stages[k]); st.values["strength"] = 1.0f;
            if (! e.applyNow (st, err)) continue;
            const auto& o = *e.processedAudio();
            const int at = all.getNumSamples(), len = juce::jmin (seg, o.length());
            all.setSize (2, at + len, true, true, true);
            for (int c = 0; c < 2; ++c)
            {
                all.copyFrom (c, at, o.buf, juce::jmin (c, o.channels() - 1), 0, len);
                all.applyGainRamp (c, at, juce::jmin (fade, len), 0.0f, 1.0f); all.applyGainRamp (c, at + len - juce::jmin (fade, len), juce::jmin (fade, len), 1.0f, 0.0f);
            }
            e.sessionFolder().deleteRecursively();
        }
        Audio out; out.sampleRate = src->sampleRate; out.buf = all;
        auto f = dir.getChildFile (juce::String ("90-") + name + "-building-a-wild-shape.wav");
        writeWav (out, f, 24, err);
        std::printf ("demo  %s\n", f.getFullPathName().toRawUTF8());
    }
    for (auto* name : { "voice", "drums", "piano", "bass" })
    {
        auto src = demoSource (name);
        if (src == nullptr && juce::String (name) == "voice") src = synthVoice();
        if (src == nullptr && juce::String (name) == "piano") src = synthPiano();
        if (src == nullptr) { std::printf ("no %s source\n", name); continue; }
        juce::String err;
        writeWav (*src, dir.getChildFile (juce::String ("00-") + name + "-original.wav"), 24, err);
        int k = 1;
        for (auto& l : lands)
        {
            Engine e; e.setSpecimen (src, name);
            Step st; st.op = Op::Terrain; st.data = toString (l.t); st.values["strength"] = 1.0f;
            if (! e.applyNow (st, err)) { std::printf ("%s %s: %s\n", name, l.name, err.toRawUTF8()); continue; }
            auto f = dir.getChildFile (juce::String (k).paddedLeft ('0', 2) + "-" + name + "-" + names[(size_t) (&l - lands.data())] + ".wav");
            writeWav (*e.processedAudio(), f, 24, err);
            std::printf ("demo  %s\n", f.getFullPathName().toRawUTF8());
            e.sessionFolder().deleteRecursively();
            ++k;
        }
    }
}

static void demos (const juce::File& dir)
{
    dir.createDirectory();
    juce::String err;
    auto voice = synthVoice(), piano = synthPiano();
    std::shared_ptr<Audio> drums;
    const bool real = demoSource ("voice") != nullptr;
    if (real) { voice = demoSource ("voice"); piano = demoSource ("piano"); drums = demoSource ("drums"); }
    // the sources, gain-staged like everything else
    auto v = copyOf (*voice); finish (*v, 0.12f); auto pn = copyOf (*piano); finish (*pn, 0.12f);
    writeWav (*v, dir.getChildFile ("00-source-voice.wav"), 24, err);
    writeWav (*pn, dir.getChildFile ("00-source-piano.wav"), 24, err);
    std::shared_ptr<Audio> dr;
    if (drums != nullptr) { dr = copyOf (*drums); finish (*dr, 0.12f); writeWav (*dr, dir.getChildFile ("00-source-drums.wav"), 24, err); }
    Engine e;
    int k = 1;
    std::vector<std::pair<std::shared_ptr<Audio>, juce::String>> srcs { { v, "voice" }, { pn, "piano" } };
    if (dr != nullptr) srcs.push_back ({ dr, "drums" });
    for (auto& src : srcs)
    {
        for (auto op : { Op::Pull, Op::Twist, Op::Freeze, Op::Smear, Op::Shatter, Op::Mutate, Op::GrainCloud, Op::SpectralMelt, Op::TimeWarp, Op::VoiceFusion })
        {
            e.setSpecimen (src.first, src.second);
            const bool ok = e.applyNow ({ op, {}, 2024 }, err);
            auto name = juce::String (k++).paddedLeft ('0', 2) + "-" + src.second + "-" + info (op).id + ".wav";
            if (ok) writeWav (*e.processedAudio(), dir.getChildFile (name), 24, err);
            std::printf ("demo  %s %s\n", name.toRawUTF8(), ok ? "" : err.toRawUTF8());
        }
    }
    auto breed = [&] (const std::shared_ptr<Audio>& a, const juce::String& an, const std::shared_ptr<Audio>& b, const juce::String& bn, const juce::String& file)
    {
        e.setSpecimen (a, an); e.setPartner (b, bn);
        const bool ok = e.breedNow (0.5f, err);
        if (ok) writeWav (*e.processedAudio(), dir.getChildFile (file), 24, err);
        std::printf ("demo  %s %s\n", file.toRawUTF8(), ok ? "" : err.toRawUTF8());
    };
    breed (v, "voice", pn, "piano", "50-breed-voice-x-piano.wav");
    if (dr != nullptr) { breed (dr, "drums", v, "voice", "51-breed-drums-x-voice.wav"); breed (pn, "piano", dr, "drums", "52-breed-piano-x-drums.wav"); }
    e.setSpecimen (v, "voice");
    if (e.runChainNow (7, err)) writeWav (*e.processedAudio(), dir.getChildFile ("60-generate-mutation-voice.wav"), 24, err);
    if (dr != nullptr) { e.setSpecimen (dr, "drums"); if (e.runChainNow (11, err)) writeWav (*e.processedAudio(), dir.getChildFile ("61-generate-mutation-drums.wav"), 24, err); }
    e.sessionFolder().deleteRecursively();
    std::printf ("demos (%s sources) written to %s\n", real ? "real" : "synthesised", dir.getFullPathName().toRawUTF8());
}


// ------------------------------------------------------------------------------------------------ 2026-11-06 rules: size, fresh start, names
// the state a host would save, without the session id (a new random id per instance)
static juce::String stateWithoutSession (Processor& p)
{
    p.refreshState();
    juce::MemoryBlock mb; p.getStateInformation (mb);
    auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
    if (xml == nullptr) return {};
    auto t = juce::ValueTree::fromXml (*xml);
    t.removeProperty ("session", nullptr);
    return t.toXmlString();
}

// the visible parts of a component don't overlap and stay inside it (the toast and the DEEP panel are overlays by design)
static bool noOverlaps (juce::Component& parent, juce::String& why, const std::vector<juce::Component*>& skip)
{
    std::vector<juce::Component*> kids;
    for (auto* c : parent.getChildren())
        if (c->isVisible() && ! c->getBounds().isEmpty() && std::find (skip.begin(), skip.end(), c) == skip.end()) kids.push_back (c);
    for (size_t i = 0; i < kids.size(); ++i)
    {
        if (! parent.getLocalBounds().contains (kids[i]->getBounds())) { why = "a part sticks out at " + kids[i]->getBounds().toString(); return false; }
        for (size_t j = i + 1; j < kids.size(); ++j)
            if (kids[i]->getBounds().intersects (kids[j]->getBounds()))
            { why = kids[i]->getBounds().toString() + " overlaps " + kids[j]->getBounds().toString(); return false; }
    }
    return true;
}

static void testOpenSize()
{
    Processor p;
    std::unique_ptr<Editor> ed (dynamic_cast<Editor*> (p.createEditor()));
    const int w = ed->getWidth(), h = ed->getHeight();
    check (w == Editor::kW && h == Editor::kH && w <= 1240 && h <= 780 && std::abs (w - 1200) <= 40 && std::abs (h - 760) <= 20,
           "it opens at the fitting size for a 1440x900 MacBook Pro: " + juce::String (w) + "x" + juce::String (h) + " (about 1200x760, never over 1240x780)");
    check (ed->isResizable(), "the window is resizable");
    // nothing overlaps on any page, at the opening size, smaller, bigger and phone-narrow
    auto& content = ed->contentComponent();
    bool allOk = true;
    for (auto size : { juce::Point<int> (Editor::kW, Editor::kH), juce::Point<int> (1000, 640), juce::Point<int> (1600, 1000), juce::Point<int> (430, 932), juce::Point<int> (700, 700) })
        for (int page = 0; page < 3; ++page)
        {
            ed->setSize (size.x, size.y);
            ed->setTerrainPage (page == 1); ed->setHoodPage (page == 2); ed->refresh();
            juce::String why;
            std::vector<juce::Component*> skip;
            // overlays: the toast (never takes clicks) and the DEEP panel (shown only over the body, on purpose)
            for (auto* c : content.getChildren())
            {
                bool clicks = true, kids = true; c->getInterceptsMouseClicks (clicks, kids);
                if (! clicks) skip.push_back (c);
            }
            const bool ok = noOverlaps (content, why, skip);
            const auto wide = ! ed->isNarrow();
            // the content and footer sit inside the window (wide: scaled to fit; narrow: the content scrolls)
            bool inside = true;
            if (wide)
                for (auto* c : ed->getChildren())
                    if (c->isVisible() && ! ed->getLocalBounds().toFloat().expanded (1.0f).contains (c->getBounds().toFloat().transformedBy (c->getTransform()))) inside = false;
            if (! ok || ! inside)
            {
                allOk = false;
                check (false, "page " + juce::String (page) + " at " + juce::String (size.x) + "x" + juce::String (size.y) + ": " + (ok ? juce::String ("content outside the window") : why));
            }
        }
    check (allOk, "nothing overlaps on the LAB, TERRAIN and UNDER THE HOOD pages at 1200x760, 1000x640, 1600x1000, 700x700 and 430x932");
    ed->setTerrainPage (false); ed->setHoodPage (false);
    ed->setSize (1500, 900);
    ed.reset();
    // the same instance re-opens at its own last size; a brand-new instance opens at the fitting size, not at that one
    ed.reset (dynamic_cast<Editor*> (p.createEditor()));
    check (ed->getWidth() == 1500 && ed->getHeight() == 900, "closing and re-opening the window keeps this instance's size");
    ed.reset();
    Processor q;
    std::unique_ptr<Editor> ed2 (dynamic_cast<Editor*> (q.createEditor()));
    check (ed2->getWidth() == Editor::kW && ed2->getHeight() == Editor::kH, "a new instance opens at the fitting size, not at another instance's size");
    ed2.reset();
    p.engine.sessionFolder().deleteRecursively(); q.engine.sessionFolder().deleteRecursively();
}

static bool atFactory (Processor& p, const juce::String& factoryState, juce::String& why)
{
    if (p.engine.hasSpecimen() || p.engine.hasPartner()) { why = "it has a sound"; return false; }
    if (p.engine.historySize() != 1 || p.engine.historyCursor() != 0) { why = "it has history"; return false; }
    if (p.terrain.canUndo()) { why = "it has landscape history"; return false; }   // (the landscape itself: in the state below)
    if (p.capturing() || p.player.playingSlot() >= 0) { why = "it is capturing / playing"; return false; }
    if (stateWithoutSession (p) != factoryState) { why = "its saved state is not the factory one"; return false; }
    std::unique_ptr<Editor> ed (dynamic_cast<Editor*> (p.createEditor()));
    if (ed->terrainPageOn() || ed->hoodPageOn() || ed->deepOn()) { why = "its window does not open on the first page"; return false; }
    if (ed->getWidth() != Editor::kW || ed->getHeight() != Editor::kH) { why = "its window is not at the fitting size"; return false; }
    return true;
}

static void testFreshStart (const std::shared_ptr<Audio>& voice)
{
    juce::String factory;
    { Processor ref; factory = stateWithoutSession (ref); }
    juce::String why;
    Processor a;
    bool ok = atFactory (a, factory, why);
    check (ok, "a new instance starts at factory defaults " + why);
    // change everything on the first instance
    a.engine.setSpecimen (voice, "voice");
    a.engine.setPartner (voice, "partner");
    juce::String err;
    a.engine.applyNow ({ Op::Smear, {}, 1 }, err);
    a.engine.chain[3].on = true; a.engine.moveSlot (0, 2);
    { ghost::terrain::Terrain t; t.add ({ 0.4f, 0.5f, 0.7f, 0.05f, 0.15f, 0.0f }); a.terrain.setWithoutUndo (t); }
    a.syncPlayer(); a.player.play (Player::Processed, 0.0);
    {
        std::unique_ptr<Editor> ed (dynamic_cast<Editor*> (a.createEditor()));
        ed->setSize (1500, 950); ed->setTerrainPage (true); ed->refresh();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        waitIdle (a.engine);
    }
    a.refreshState();
    // a second instance in the same process knows nothing of it
    Processor b;
    ok = atFactory (b, factory, why);
    check (ok, "two instances: changing the first leaves the second at factory defaults " + why);
    check (b.engine.sessionId != a.engine.sessionId && b.engine.sessionFolder() != a.engine.sessionFolder(), "two instances never share a session folder");
    // destroy and recreate: factory defaults again (no last-session memory)
    juce::MemoryBlock saved; a.getStateInformation (saved);
    const auto aFolder = a.engine.sessionFolder();
    {
        auto* gone = new Processor(); gone->engine.setSpecimen (voice, "x"); gone->refreshState();
        auto f = gone->engine.sessionFolder(); delete gone;
        Processor again;
        const bool fresh = atFactory (again, factory, why);
        check (fresh, "destroy and recreate: the new instance is at factory defaults " + why);
        f.deleteRecursively();
    }
    // the host hands the same saved state to two instances (FL: clone / Replace): each gets its own copy of the sounds
    Processor c1, c2;
    c1.setStateInformation (saved.getData(), (int) saved.getSize());
    c2.setStateInformation (saved.getData(), (int) saved.getSize());
    for (int i = 0; i < 100 && ! (c1.engine.hasSpecimen() && c2.engine.hasSpecimen()); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
    check (c1.engine.hasSpecimen() && c2.engine.hasSpecimen() && c1.engine.historySize() == 2 && c2.engine.historySize() == 2,
           "the same saved project restores into two instances");
    check (c1.engine.sessionId != c2.engine.sessionId && c1.engine.sessionFolder() != c2.engine.sessionFolder(),
           "two instances restored from one saved state get separate session folders (" + c1.engine.sessionId.substring (0, 8) + " / " + c2.engine.sessionId.substring (0, 8) + ")");
    // change c1: c2's sound and its session files stay as they were
    const auto c2Before = c2.engine.processedAudio();
    c1.engine.applyNow ({ Op::Freeze, {}, 1 }, err);
    for (int i = 0; i < 200 && ! c2.engine.sessionFolder().getChildFile ("current.wav").existsAsFile(); ++i) juce::Thread::sleep (20);
    juce::Thread::sleep (300);
    juce::String e2;
    auto onDisk = readWav (c2.engine.sessionFolder().getChildFile ("current.wav"), e2);
    check (c2.engine.processedAudio() == c2Before && onDisk != nullptr && bitExact (*onDisk, *c2Before),
           "changing one of them leaves the other's sound and saved files untouched");
    a.player.stop();
    for (auto* f : { &aFolder }) f->deleteRecursively();
    c1.engine.sessionFolder().deleteRecursively(); c2.engine.sessionFolder().deleteRecursively(); b.engine.sessionFolder().deleteRecursively();
}

// no open-source project names in the visible UI (the one About / Credits item names them, as their licences require)
static void testNoUpstreamNames()
{
    const juce::StringArray names { "CDP", "Composers Desktop", "Trevor Wishart", "Surge", "Vital", "Samplebrain", "PaulXStretch" };
    auto clean = [&names] (const juce::String& s) { for (auto& n : names) if (s.containsIgnoreCase (n)) return false; return true; };
    int bad = 0;
    for (auto op : { Op::Pull, Op::Twist, Op::Freeze, Op::Smear, Op::Shatter, Op::Mutate, Op::GrainCloud, Op::SpectralMelt, Op::TimeWarp, Op::VoiceFusion, Op::Breed, Op::Hood, Op::Terrain })
    {
        const auto& i = info (op);
        if (! clean (i.title) || ! clean (i.subtitle) || ! clean (i.cdp)) { ++bad; std::printf ("  named: %s | %s | %s\n", i.title.toRawUTF8(), i.subtitle.toRawUTF8(), i.cdp.toRawUTF8()); }
        for (auto& c : i.controls) if (! clean (c.label)) { ++bad; std::printf ("  named control: %s\n", c.label.toRawUTF8()); }
    }
    Processor p;
    std::unique_ptr<Editor> ed (dynamic_cast<Editor*> (p.createEditor()));
    ed->setHoodPage (true);
    auto& h = ed->hood();
    if (! clean (h.searchHint())) ++bad;
    for (auto& pr : hood::catalog())
    {
        h.selectProcess (pr.prog, pr.name);
        for (int m : hood::modesOf (pr)) { h.setMode (m); if (! clean (h.commandText())) { ++bad; break; } }
        if (! clean (h.usageText()) || ! clean (h.commandText())) { ++bad; std::printf ("  named in %s\n", pr.id().toRawUTF8()); }
        for (auto& f : pr.forms) for (auto& q : f.params) if (! clean (hood::shownText (q.help))) ++bad;
    }
    check (bad == 0, "no open-source project name anywhere in the cards, the DEEP view or UNDER THE HOOD (" + juce::String (bad) + " found)");
    check (clean (ed->statusText()), "the status line names no project");
    const auto credits = Editor::creditsText();
    check (credits.contains ("CDP") && credits.contains ("LGPL") && credits.contains ("github.com/ComposersDesktop/CDP8") && credits.contains ("Composers Desktop Project"),
           "the one About / Credits item names the engine's project, its authors, its licence and its source");
    ed.reset();
    p.engine.sessionFolder().deleteRecursively();
}


// ------------------------------------------------------------------------------------------------ the rooms' pages (build 13: laid out like the originals)
static void testRoomPages (const std::shared_ptr<Audio>& voice)
{
    std::printf ("\n== the rooms' pages\n");
    using rooms::Room;
    Processor p;
    p.prepareToPlay (44100.0, 512);
    std::unique_ptr<Editor> ed (dynamic_cast<Editor*> (p.createEditor()));
    auto& page = ed->roomPage();
    auto allGrey = [&] { for (auto& id : page.controlIds()) if (page.controlEnabled (id)) return false; return ! page.controlIds().isEmpty(); };
    // an empty lab: a room has nothing to work on, so every control is greyed
    ed->setRoom (Room::Stretch);
    check (ed->roomNow() == Room::Stretch && page.isVisible() && ! ed->body().isVisible(), "the strip opens a room: its page takes the place of the lab");
    check (allGrey(), "STRETCH with no sound: every control is greyed");
    ed->setRoom (Room::Space); check (allGrey(), "SPACE with no sound: every control is greyed");
    {
        auto& bar = ed->routeBar();
        bool ok = true, noGrow = true;
        for (int i = 0; i < RouteBar::kChips; ++i)
        {
            const auto c = bar.chip (i).getCentre();
            const auto t = juce::Time::getCurrentTime();
            bar.mouseUp (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), c, {}, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &bar, &bar, t, c, t, 1, false));
            ok = ok && ed->roomNow() == RouteBar::roomAt (i);
            noGrow = noGrow && RouteBar::roomAt (i) != Room::Grow;
        }
        check (ok, "every chip on the strip opens its room (and THE LAB goes back)");
        check (noGrow && RouteBar::kChips == 5, "GROW is gone from the strip (SPECTRE, THE LAB, STRETCH, REBORN, SPACE)");
    }
    // STRETCH, laid out like the original: its own names, its groups, its module switches
    p.engine.setSpecimen (voice, "voice");
    ed->setRoom (Room::Stretch); ed->refresh();
    check (page.controlName ("pxs.stretchamount0") == "Stretch amount" && page.controlName ("pxs.fftsize0") == "FFT size"
           && page.controlName ("pxs.numharmonics0") == "Num harmonics" && page.runLabel() == "RENDER", "STRETCH shows the original's own names (Stretch amount, FFT size, Num harmonics) and RENDER");
    check (page.controlEnabled ("pxs.stretchamount0") && page.controlEnabled ("pxs.pitchshift0"), "the Main group and the modules that are on in the original (Pitch shift) are live");
    check (! page.moduleOn (0) && ! page.controlEnabled ("pxs.numharmonics0"), "a module that is off in the original (Harmonics) is off, and its controls are greyed");
    page.setModule (0, true);
    check (page.moduleOn (0) && page.controlEnabled ("pxs.numharmonics0"), "switching the module on brings its controls to life");
    page.setModule (0, false);
    check (! page.controlEnabled ("pxs.binauralbeatsfreq"), "Binaural beats off: its other controls are greyed");
    // build 14: the ratio values, the chain order and the Free filter curve are on the page
    check (page.controlName ("pxs.ratiomix_ratio_0_0") == "Ratio mix ratio 1" && page.controlName ("pxs.octavemix_extra0_0") == "Ratio mix 7 level"
           && ! page.controlEnabled ("pxs.ratiomix_ratio_0_0"), "the Ratios group has all eight levels and all eight ratio values (greyed while Ratios is off)");
    {
        const auto before = page.chainOrder();
        page.moveInChain (3, 0);   // Pitch shift to the front
        const auto after = page.chainOrder();
        bool perm = after.size() == 9; bool seen[9] {};
        for (int m : after) { if (m < 0 || m > 8 || seen[m]) perm = false; else seen[m] = true; }
        check (before[3] == 3 && after[0] == 3 && after[1] == 0 && perm, "dragging a module in the chain moves it there and the rest close up (Pitch shift first)");
        page.moveInChain (0, 3);
        check (page.chainOrder() == before, "dragging it back restores the original's order");
    }
    {
        check (! page.moduleOn (7) && ! page.controlEnabled ("pxs.freefilter_tilty_0") && ! page.freeFilterDrawn(), "Free filter starts off, flat, its controls greyed (as in the original)");
        page.setValue ("pxs.stretchamount0", 2.0f);
        page.pressRun(); waitIdle (p.engine);
        auto flat = p.engine.processedAudio();
        page.setModule (7, true);
        page.addFreeFilterNode (0.35, 0.02); page.addFreeFilterNode (0.6, 0.02);   // a deep cut through the middle
        check (page.controlEnabled ("pxs.freefilter_tilty_0") && page.freeFilterDrawn(), "switching Free filter on and drawing on its curve: the curve is kept with the settings");
        page.pressRun(); waitIdle (p.engine);
        check (p.engine.historyCursor() == 1 && specDistance (*p.engine.processedAudio(), *flat) > 1.0f,
               "the drawn Free filter curve changes the render (" + juce::String (specDistance (*p.engine.processedAudio(), *flat), 1) + " dB apart)");
        page.setModule (7, false);
        ed->undo();
    }
    // build 15: every Main control of the original is on the page; the ones with nothing to act on here are greyed
    {
        check (page.controlName ("pxs.playrange_start0") == "Sound start" && page.controlName ("pxs.playrange_end0") == "Sound end" && page.controlEnabled ("pxs.playrange_start0"),
               "Sound start / Sound end (the play range) are on the page and live");
        check (page.controlIds().contains ("pxs.loopxfadelen0") && ! page.controlEnabled ("pxs.loopxfadelen0") && ! page.controlEnabled ("pxs.passthrough0")
               && ! page.controlEnabled ("pxs.maxcapturelen_0"), "Loop xfade length, Pass input through and Max capture length are shown, greyed (nothing to act on in the lab)");
        check (! page.controlEnabled ("pxs.dryplayrate0"), "Dry playrate is greyed until Bypass stretch is on");
        page.setValue ("pxs.bypass_stretch0", 1.0f);
        check (page.controlEnabled ("pxs.dryplayrate0"), "Bypass stretch on: Dry playrate is live");
        page.setValue ("pxs.bypass_stretch0", 0.0f);
        int shown = 0, table = 0;
        for (auto& c : rooms::controls (Room::Stretch)) if (! c.key.startsWith ("pxs.order")) { ++table; if (page.controlIds().contains (c.key)) ++shown; }
        check (shown == table, "every control of the original is on the STRETCH page (" + juce::String (shown) + " of " + juce::String (table) + "; the chain order is the strip)");
        page.setValue ("pxs.stretchamount0", 4.0f);
        page.setValue ("pxs.playrange_start0", 0.0f); page.setValue ("pxs.playrange_end0", 0.5f);
        page.pressRun(); waitIdle (p.engine);
        const double ratio = p.engine.processedAudio()->seconds() / voice->seconds();
        check (p.engine.historyCursor() == 1 && ratio > 1.4 && ratio < 2.6, "Sound end at the middle: only the first half is rendered (4x of half = " + juce::String (ratio, 2) + "x)");
        page.setValue ("pxs.playrange_end0", 1.0f);
        ed->undo();
    }
    page.setValue ("pxs.stretchamount0", 3.0f);
    page.pressRun(); waitIdle (p.engine);
    check (p.engine.historyCursor() == 1 && p.engine.current().label == "STRETCH 3.0x", "RENDER is one step in the history (" + p.engine.current().label + ")");
    page.setValue ("pxs.stretchamount0", 5.0f);
    page.pressRun(); waitIdle (p.engine);
    check (p.engine.historyCursor() == 1 && p.engine.current().label == "STRETCH 5.0x"
           && std::abs (p.engine.processedAudio()->seconds() / voice->seconds() - 5.0) < 1.0, "pressing it again replaces the last stretch (5x of the sound before, not 15x)");
    ed->undo();
    check (p.engine.historyCursor() == 0 && bitExact (*p.engine.processedAudio(), *voice), "UNDO takes the room's step back, bit-exact");
    // REBORN, laid out like the original: the model list, the Pitch / Volume box, the knobs
    ed->setRoom (Room::Reborn); ed->refresh();
    check (allGrey(), "REBORN before an instrument is picked: every knob is greyed");
    check (page.controlName ("ddsp.PitchShift") == "Pitch Shift" && page.controlName ("ddsp.HarmonicGain") == "Harmonics" && page.controlName ("ddsp.ReverbWet") == "Reverb Wet",
           "REBORN shows the original's own names (Pitch Shift, Harmonics, Reverb Wet)");
    check (! page.controlIds().contains ("ddsp.InputPitch"), "Input Pitch / Input Gain are the box itself, as in the original (not knobs)");
    check (page.verdict().isNotEmpty(), "REBORN always says what the box shows (" + page.verdict() + ")");
    if (rooms::rebornAvailable())
    {
        page.pickInstrument (juce::jmax (0, rooms::rebornInstruments().indexOf ("Flute")));
        ed->refresh();
        check (! allGrey() && page.controlEnabled ("ddsp.PitchShift"), "REBORN with an instrument: its knobs are live");
        check (juce::roundToInt (page.value ("ddsp.PitchShift")) % 12 == 0 && page.verdict().contains ("inside the box"), "picking an instrument moves the tune into its box (" + page.verdict() + ")");
        page.pressRun(); waitIdle (p.engine);
        check (p.engine.historyCursor() == 1 && p.engine.current().label.startsWith ("REBORN as "), "REBIRTH is one step in the history (" + p.engine.current().label + ")");
        ed->undo();
    }
    // SPACE, laid out like the original's rack: the two modules and their knobs; LISTEN is live
    ed->setRoom (Room::Space); ed->refresh();
    check (page.controlIds().size() == 17 && page.controlName ("panner.er_level") == "ER Level" && page.controlName ("fdn.pre_delay") == "Pre-Delay",
           "SPACE shows both modules' own knobs by their own names (" + juce::String (page.controlIds().size()) + ")");
    page.setValue ("panner.x", -0.9f);
    page.pressListen();
    auto earsDb = [&]
    {
        juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m;
        double l = 0, r = 0;
        for (int k = 0; k < 60; ++k)
        {
            b.clear(); p.processBlock (b, m);
            if (k >= 20) { l += b.getRMSLevel (0, 0, 512); r += b.getRMSLevel (1, 0, 512); }
        }
        return juce::Decibels::gainToDecibels ((float) l + 1e-9f) - juce::Decibels::gainToDecibels ((float) r + 1e-9f);
    };
    const float leftDb = earsDb();
    check (p.spaceListening(), "LISTEN plays the sound through the two modules");
    check (leftDb > 1.0f, "LISTEN, X turned left: the left ear is louder (" + juce::String (leftDb, 1) + " dB)");
    page.setValue ("panner.x", 0.9f);
    const float rightDb = earsDb();
    check (rightDb < -1.0f, "turn X right while it plays: the right ear is louder at once (" + juce::String (rightDb, 1) + " dB)");
    page.pressRun(); waitIdle (p.engine);
    check (p.engine.historyCursor() == 1 && p.engine.current().label.startsWith ("SPACE "), "PRINT IT is one step in the history (" + p.engine.current().label + ")");
    ed->setRoom (Room::Lab);
    { juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m; for (int k = 0; k < 8; ++k) { b.clear(); p.processBlock (b, m); } }
    check (! p.spaceListening() && ed->body().isVisible(), "back in THE LAB the listen has stopped and the living sound is back");
    // nothing overlaps on any room's page, at the opening size, smaller, bigger and phone-narrow
    bool fits = true;
    for (auto size : { juce::Point<int> (Editor::kW, Editor::kH), juce::Point<int> (1000, 640), juce::Point<int> (1600, 1000), juce::Point<int> (430, 932) })
        for (auto r : { Room::Stretch, Room::Reborn, Room::Space })
        {
            ed->setSize (size.x, size.y); ed->setRoom (r); ed->refresh();
            juce::String why;
            if (! noOverlaps (page, why)) { fits = false; std::printf ("  overlap in %s at %dx%d: %s\n", rooms::info (r).name, size.x, size.y, why.toRawUTF8()); }
            if (! ed->contentComponent().getLocalBounds().contains (page.getBounds())) { fits = false; std::printf ("  %s page off the content at %dx%d\n", rooms::info (r).name, size.x, size.y); }
        }
    check (fits, "the rooms' pages: nothing overlaps or hangs off, at any size");
    // no project name on any room's page (the one place is About / Credits)
    {
        const juce::StringArray names { "Flues", "PaulXStretch", "Paulstretch", "DDSP", "Magenta", "Pertylizer", "TensorFlow", "Box2D" };
        int bad = 0;
        auto clean = [&] (const juce::String& t) { for (auto& n : names) if (t.containsIgnoreCase (n)) return false; return true; };
        for (auto r : { Room::Stretch, Room::Reborn, Room::Space })
        {
            if (! clean (rooms::info (r).name) || ! clean (rooms::info (r).does) || ! clean (rooms::info (r).bestOn)) ++bad;
            for (auto& c : rooms::controls (r)) if (! clean (c.name) || ! clean (c.plain)) ++bad;
        }
        check (bad == 0, "the rooms name no open-source project on their pages (" + juce::String (bad) + " found)");
        const auto credits = Editor::creditsText();
        bool all = true;
        for (auto* n : { "PaulXStretch", "DDSP-VST", "Pertylizer", "TensorFlow", "AGPL", "github.com/essej/paulxstretch", "github.com/magenta/ddsp-vst", "github.com/pertyjons/pertylizer" })
            if (! credits.contains (n)) { all = false; std::printf ("  credits lack %s\n", n); }
        check (all, "About / Credits names every engine, its licence and its source");
    }
}


// ------------------------------------------------------------------------------------------------ TRIM (build 16)
static void testTrim (const std::shared_ptr<Audio>& voice)
{
    std::printf ("\n== trim\n");
    juce::String err;
    // the engine: trimming the original makes the kept part the new original; undo brings everything back exactly
    {
        Engine e;
        e.setSpecimen (voice, "voice");
        Step s; s.op = Op::XStretch; s.values["pxs.stretchamount0"] = 2.0f;
        check (e.applyNow (s, err), "a first effect before trimming");
        auto processedBefore = e.processedAudio();
        check (e.trimOriginal (0.25f, 0.75f, err), "trim the original to its middle half" + (err.isNotEmpty() ? " (" + err + ")" : juce::String()));
        const double kept = e.originalAudio()->seconds();
        check (std::abs (kept - voice->seconds() * 0.5) < 0.01, "the original is now the part kept (" + juce::String (kept, 2) + " s of " + juce::String (voice->seconds(), 2) + " s)");
        check (e.processedAudio() == e.originalAudio(), "PROCESSED starts again from the trimmed original");
        check (e.current().label.startsWith ("TRIMMED ORIGINAL"), "it is one step in the history (" + e.current().label + ")");
        // the kept part is exactly the original's samples (only the 5 ms edges fade)
        const int off = (int) std::floor (0.25 * voice->length()), mid = e.originalAudio()->length() / 2;
        check (e.originalAudio()->buf.getSample (0, mid) == voice->buf.getSample (0, off + mid), "the kept part is the original's own samples, untouched");
        check (edgeJump (*e.originalAudio(), 64) < 0.05f, "the cut edges fade (no click: " + juce::String (edgeJump (*e.originalAudio(), 64), 3) + ")");
        // what works on the sound now works on the trimmed one
        check (e.applyNow (s, err) && std::abs (e.processedAudio()->seconds() - kept * 2.0) < kept * 0.4, "the next effect works on the trimmed original (2x of " + juce::String (kept, 2) + " s)");
        e.undo();
        e.undo();
        check (e.originalAudio() == voice && e.processedAudio() == processedBefore, "UNDO brings back the whole original and the result you had");
        e.redo();
        check (std::abs (e.originalAudio()->seconds() - kept) < 1e-6, "REDO trims it again");
        e.undo();
        // the processed sound: the result only
        check (e.trimProcessed (0.0f, 0.5f, err), "trim the processed sound to its first half");
        check (std::abs (e.processedAudio()->seconds() - processedBefore->seconds() * 0.5) < 0.01 && e.originalAudio() == voice,
               "PROCESSED is trimmed; the original is left alone");
        check (e.processedAudio()->buf.getSample (0, 100) == processedBefore->buf.getSample (0, 100), "an edge that was not moved is not faded");
        e.undo();
        check (e.processedAudio() == processedBefore, "UNDO brings the processed sound back");
        const bool sliver = e.trimOriginal (0.5f, 0.5001f, err);
        check (! sliver && err.isNotEmpty(), "a sliver too short to keep is refused (" + err + ")");
        check (! e.trimOriginal (0.0f, 1.0f, err), "handles at both ends: nothing to trim");
    }
    // the screen: drag the real handles
    {
        Processor p;
        p.prepareToPlay (44100.0, 512);
        p.engine.setSpecimen (voice, "voice");
        std::unique_ptr<Editor> ed (dynamic_cast<Editor*> (p.createEditor()));
        ed->setSize (Editor::kW, Editor::kH);
        ed->refresh();
        auto drag = [&] (juce::Component& w, float fromFrac, float toFrac)
        {
            const auto wa = w.getLocalBounds().toFloat().reduced (16, 0).withTrimmedTop (32).withTrimmedBottom (10);
            const juce::Point<float> a (wa.getX() + wa.getWidth() * fromFrac, wa.getCentreY()), b (wa.getX() + wa.getWidth() * toFrac, wa.getCentreY());
            const auto t = juce::Time::getCurrentTime();
            auto ev = [&] (juce::Point<float> pos, bool down)
            { return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), pos, down ? juce::ModifierKeys::leftButtonModifier : juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &w, &w, t, a, t, 1, false); };
            w.mouseDown (ev (a, true)); w.mouseDrag (ev ({ (a.x + b.x) * 0.5f, a.y }, true)); w.mouseDrag (ev (b, true)); w.mouseUp (ev (b, false));
        };
        drag (ed->waveComponent (false), 0.0f, 0.2f);     // ORIGINAL's start handle, in to 20%
        drag (ed->waveComponent (false), 1.0f, 0.7f);     // its end handle, in to 70%
        const double kept = p.engine.originalAudio()->seconds();
        check (std::abs (kept - voice->seconds() * 0.8 * (0.7 / 1.0)) < 0.05 || std::abs (kept - voice->seconds() * 0.56) < 0.05,
               "dragging ORIGINAL's handles trims the original (" + juce::String (kept, 2) + " s kept)");
        { juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m; for (int k = 0; k < 4; ++k) { b.clear(); p.processBlock (b, m); } }
        check (p.engine.processedAudio() == p.engine.originalAudio() && p.player.playingSlot() == Player::Original, "the trimmed original plays, and is what gets worked on");
        const double procLen = p.engine.processedAudio()->seconds();
        drag (ed->waveComponent (true), 1.0f, 0.5f);      // PROCESSED's end handle to the middle
        check (std::abs (p.engine.processedAudio()->seconds() - procLen * 0.5) < 0.05 && std::abs (p.engine.originalAudio()->seconds() - kept) < 1e-6,
               "dragging PROCESSED's end handle trims the result only");
        const int steps = p.engine.historyCursor();
        drag (ed->waveComponent (true), 0.5f, 0.6f);      // the middle of the wave, not a handle: a drag into FL, not a trim
        check (p.engine.historyCursor() == steps, "a drag that does not start on a handle does not trim");
        ed->undo(); ed->undo(); ed->undo();
        check (p.engine.originalAudio() == voice, "UNDO all the way: the whole original is back");
    }
}


// ------------------------------------------------------------------------------------------------ SPECTRE (build 17)
static float zcFreq (const float* x, int n, double sr)
{
    int first = -1, last = -1, count = 0;
    for (int i = 1; i < n; ++i) if (x[i - 1] <= 0.0f && x[i] > 0.0f) { if (first < 0) first = i; last = i; ++count; }
    return count > 1 ? (float) ((count - 1) * sr / (last - first)) : 0.0f;
}
static float rmsOf (const float* x, int n) { double e = 0; for (int i = 0; i < n; ++i) e += (double) x[i] * x[i]; return (float) std::sqrt (e / juce::jmax (1, n)); }

static void testSpectre (const std::shared_ptr<Audio>& voice)
{
    using namespace spectre;
    const double sr = 48000.0;
    // ---- the engine: a sine comes back as itself; TRANSPOSE / SHIFT move it; INF holds it after it has stopped
    auto runSine = [&] (Params p, float hz, int stopAt, std::function<void (Params&, int)> during = nullptr)
    {
        spectre::Engine e; e.prepare (sr, 256);
        const int n = (int) sr * 2;
        std::vector<float> L ((size_t) n), R ((size_t) n);
        for (int i = 0; i < n; ++i) L[(size_t) i] = R[(size_t) i] = i < stopAt ? 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / sr) : 0.0f;
        for (int i = 0; i < n; i += 256) { if (during) during (p, i); e.process (L.data() + i, R.data() + i, juce::jmin (256, n - i), p); }
        return L;
    };
    const int n = (int) sr * 2;
    {
        auto out = runSine ({}, 440.0f, n);
        const float f = zcFreq (out.data() + 40000, 40000, sr), r = rmsOf (out.data() + 40000, 40000);
        check (std::abs (f - 440.0f) < 2.0f && std::abs (juce::Decibels::gainToDecibels (r / 0.2121f)) < 1.0f,
               "SPECTRE: a 440 Hz sine comes back at 440 Hz, same level (" + juce::String (f, 1) + " Hz, " + juce::String (juce::Decibels::gainToDecibels (r / 0.2121f), 2) + " dB)");
    }
    { Params p; p.v[Transpose] = 12.0f; auto out = runSine (p, 220.0f, n); const float f = zcFreq (out.data() + 40000, 40000, sr);
      check (std::abs (f - 440.0f) < 3.0f, "SPECTRE: TRANSPOSE +12 puts 220 Hz at 440 Hz (" + juce::String (f, 1) + ")"); }
    { Params p; p.v[Shift] = 100.0f; auto out = runSine (p, 220.0f, n); const float f = zcFreq (out.data() + 40000, 40000, sr);
      check (std::abs (f - 320.0f) < 3.0f, "SPECTRE: SHIFT +100 Hz puts 220 Hz at 320 Hz (" + juce::String (f, 1) + ")"); }
    {
        auto out = runSine ({}, 330.0f, (int) sr, [] (Params& p, int i) { if (i > 43000) p.v[Inf] = 1.0f; });
        const float r = rmsOf (out.data() + 72000, 20000);
        check (r > 0.15f, "SPECTRE: INF at the top holds the sound after it stops (rms " + juce::String (r, 3) + " a half second later)");
        auto out2 = runSine ({}, 330.0f, (int) sr);
        check (rmsOf (out2.data() + 72000, 20000) < 1e-3f, "SPECTRE: with INF at 0 it stops when the sound stops");
    }
    {
        // every control, swept to its ends at random on noise and a chord: never a NaN, never louder than +24 dB
        spectre::Engine e; e.prepare (sr, 512);
        juce::Random rnd (7);
        bool finite = true; float peak = 0;
        Params p;
        for (int blk = 0; blk < 600; ++blk)
        {
            if (blk % 20 == 0) for (int k = 0; k < kParams; ++k) { const auto& in = param (k); p.v[(size_t) k] = rnd.nextBool() ? (rnd.nextBool() ? in.lo : in.hi) : in.lo + rnd.nextFloat() * (in.hi - in.lo); }
            float L[512], R[512];
            for (int i = 0; i < 512; ++i) { L[i] = 0.2f * (rnd.nextFloat() - 0.5f) + 0.2f * (float) std::sin (0.05 * (blk * 512 + i)); R[i] = 0.2f * (rnd.nextFloat() - 0.5f); }
            e.process (L, R, 512, p);
            for (int i = 0; i < 512; ++i) { finite = finite && std::isfinite (L[i]) && std::isfinite (R[i]); peak = juce::jmax (peak, std::abs (L[i]), std::abs (R[i])); }
            const int live = e.mid.activeCount.load();
            finite = finite && live <= kMaxOscs;
        }
        check (finite && peak <= 1.0f, "SPECTRE: every control swept to its ends: no NaN, never past 0 dBFS (peak " + juce::String (peak, 3) + ")");
    }
    {
        Params p; p.v[Partials] = 16.0f;
        spectre::Engine e; e.prepare (sr, 512);
        std::vector<float> L (48000), R (48000);
        for (int i = 0; i < 48000; ++i) { float v = 0; for (int h = 1; h <= 40; ++h) v += 0.02f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 110.0 * h * i / sr); L[(size_t) i] = R[(size_t) i] = v; }
        for (int i = 0; i < 48000; i += 512) e.process (L.data() + i, R.data() + i, juce::jmin (512, 48000 - i), p);
        check (e.mid.activeCount.load() <= 24, "SPECTRE: PARTIALS 16 rebuilds a 40-harmonic tone from about 16 sine waves (" + juce::String (e.mid.activeCount.load()) + " live)");
    }
    // ---- NOISE: hiss stays hiss (at its own level); TRANSIENT: drum hits keep their attack
    {
        juce::Random rn (3);
        std::vector<float> L ((size_t) n), R;
        for (auto& v : L) v = 0.3f * (rn.nextFloat() * 2.0f - 1.0f);
        const float in = rmsOf (L.data() + 40000, 40000);
        R = L; spectre::Engine e; e.prepare (sr, 512); Params p;
        for (int i = 0; i < n; i += 512) e.process (L.data() + i, R.data() + i, juce::jmin (512, n - i), p);
        const float db = juce::Decibels::gainToDecibels (rmsOf (L.data() + 40000, 40000) / in);
        check (std::abs (db) < 1.5f, "SPECTRE: white noise comes back as noise at its own level (" + juce::String (db, 2) + " dB)");
    }
    {
        juce::Random rn (5);
        std::vector<float> dry ((size_t) n, 0.0f);
        int hits = 0;
        for (int s0 = 4800; s0 < n - 12000; s0 += 12000)
        {
            ++hits;
            for (int i = 0; i < 6000; ++i)
                dry[(size_t) (s0 + i)] += 0.5f * std::exp (-i / 800.0f) * (rn.nextFloat() * 2 - 1) + 0.5f * std::exp (-i / 2000.0f) * (float) std::sin (2.0 * juce::MathConstants<double>::pi * (60.0 + 200.0 * std::exp (-i / 400.0)) * i / sr);
        }
        auto attack = [&] (float tr, int& onsets)
        {
            auto L = dry, R = dry; spectre::Engine e; e.prepare (sr, 512); Params p; p.v[Transient] = tr;
            for (int i = 0; i < n; i += 512) e.process (L.data() + i, R.data() + i, juce::jmin (512, n - i), p);
            onsets = e.mid.onsets.load();
            double eD = 0, eW = 0; const int lat = e.latency();
            for (int s0 = 4800; s0 < n - 12000 - lat; s0 += 12000) for (int i = 0; i < 240; ++i) { eD += dry[(size_t) (s0 + i)] * dry[(size_t) (s0 + i)]; eW += L[(size_t) (s0 + i + lat)] * L[(size_t) (s0 + i + lat)]; }
            return (float) (eW / eD);
        };
        int on0 = 0, on1 = 0;
        const float a0 = attack (0.0f, on0), a1 = attack (1.0f, on1);
        check (on1 == hits, "SPECTRE: it finds every drum hit (" + juce::String (on1) + " of " + juce::String (hits) + ")");
        check (a1 > 0.85f && a1 > a0 + 0.3f, "SPECTRE: TRANSIENT keeps each hit's first 5 ms (" + juce::String (juce::roundToInt (a1 * 100)) + "% of the attack, " + juce::String (juce::roundToInt (a0 * 100)) + "% without)");
    }
    // ---- WAVETABLE: a sine played back through SAW gets a saw's harmonics, and nothing past Nyquist
    {
        auto L = runSine ({}, 220.0f, n), M = std::vector<float> ((size_t) n);
        spectre::Engine e; e.prepare (sr, 256); e.setTable (tableCycle (tableNames().indexOf ("SAW")));
        std::vector<float> R ((size_t) n);
        for (int i = 0; i < n; ++i) M[(size_t) i] = R[(size_t) i] = 0.3f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * i / sr);
        for (int i = 0; i < n; i += 256) e.process (M.data() + i, R.data() + i, juce::jmin (256, n - i), {});
        juce::dsp::FFT f (14); std::vector<float> b (32768, 0.0f);
        for (int i = 0; i < 16384; ++i) b[(size_t) i] = M[(size_t) (50000 + i)] * (0.5f - 0.5f * (float) std::cos (2.0 * juce::MathConstants<double>::pi * i / 16384));
        f.performFrequencyOnlyForwardTransform (b.data());
        auto at = [&] (float hz) { const int k = juce::roundToInt (hz * 16384 / sr); float m = 0; for (int j = k - 2; j <= k + 2; ++j) m = juce::jmax (m, b[(size_t) j]); return m; };
        const float h2 = at (440.0f) / at (220.0f), h3 = at (660.0f) / at (220.0f);
        check (h2 > 0.35f && h2 < 0.65f && h3 > 0.2f && h3 < 0.45f, "SPECTRE: WAVETABLE SAW gives a 220 Hz sine a saw's harmonics (2nd " + juce::String (h2, 2) + ", 3rd " + juce::String (h3, 2) + ")");
        auto cyc = tableCycle (3);
        check (tableFromText (tableToText (cyc)) == cyc && tableFromText ("rubbish").empty(), "SPECTRE: a wavetable is saved and read back exactly");
        juce::AudioBuffer<float> one (1, 600); for (int i = 0; i < 600; ++i) one.setSample (0, i, (float) i / 600.0f - 0.5f);
        check ((int) cycleFromFile (one).size() == kTableSize, "SPECTRE: a short single-cycle WAV is stretched into a 2048-sample cycle");
        juce::ignoreUnused (L);
    }
    // ---- PRINT IT: the offline render through the lab's history
    {
        Step s; s.op = Op::Spectre; s.values["transpose"] = 7.0f; s.values["phase"] = -0.6f;
        auto res = rooms::render (s, *voice);
        check (res.audio != nullptr && std::abs (res.audio->seconds() - voice->seconds()) < 0.5 && rms (*res.audio) > 0.01f,
               "SPECTRE: PRINT IT renders the whole sound (" + juce::String (res.audio ? res.audio->seconds() : 0.0, 2) + " s from " + juce::String (voice->seconds(), 2) + " s)");
        check (rooms::describe (s) == "SPECTRE +7.0 st, robot", "SPECTRE: the history says what it did (" + rooms::describe (s) + ")");
        check (rooms::roomOf (Op::Spectre) == rooms::Room::Spectre && rooms::isRoomOp (Op::Spectre), "SPECTRE is a room of the lab");
    }
    // ---- the plug-in: LISTEN (both builds), LIVE (FX), saved with the session, nothing allocated or locked on the audio thread
    {
        Processor p;
        p.setRateAndBufferSizeDetails (48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        p.engine.setSpecimen (voice, "voice"); p.syncPlayer();
        if (Processor::isFx)
        {
            check (p.spectreLiveOn() && p.getLatencySamples() == p.spectreShown().latency(), "FX: a new Ghost Lab FX plays the track through SPECTRE, and reports its latency (" + juce::String (p.getLatencySamples()) + ")");
            p.setSpectreLive (false);
            check (p.getLatencySamples() == 0, "FX: LIVE off: no latency");
            p.setSpectreLive (true);
        }
        // every control is a parameter FL can automate
        check (p.getParameters().size() >= kParams && p.spectreParameter (Transpose)->getName (64) == "SPECTRE TRANSPOSE",
               "SPECTRE: every control is a plug-in parameter FL can automate (" + juce::String (p.getParameters().size()) + ")");
        p.spectreParameter (Cloud)->setValueNotifyingHost (0.5f);   // (as FL's automation moves it)
        check (std::abs (p.spectreParams()[Cloud] - 0.5f) < 1e-4f && static_cast<juce::AudioProcessorParameter*> (p.spectreParameter (Shift))->getText (p.spectreParameter (Shift)->convertTo0to1 (100.0f), 32) == "+100 Hz",
               "SPECTRE: automation moves the engine; FL shows the values in words (\"+100 Hz\")");
        p.spectreParameter (Cloud)->setValueNotifyingHost (0.0f);
        p.setSpectreTable ("SAW", tableCycle (1));
        p.setSpectreParam (Transpose, 5.0f);
        p.setSpectreListening (true, p.engine.processedAudio());
        juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer midi;
        double e = 0; gAudioAllocs = 0; gAudioLocks = 0;
        for (int blk = 0; blk < 120; ++blk)
        {
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) buf.setSample (c, i, Processor::isFx ? 0.1f * (float) std::sin (0.07 * (blk * 512 + i)) : 0.0f);
            tlAudio = true; p.processBlock (buf, midi); tlAudio = false;
            if (blk > 10) e += buf.getRMSLevel (0, 0, 512);
        }
        check (p.spectreListening() && e > 0.5, "SPECTRE: LISTEN plays the sound round and round through it (" + juce::String (e, 2) + ")");
        check (gAudioAllocs.load() == 0 && gAudioLocks.load() == 0, "SPECTRE: nothing allocated or locked on the audio thread (" + juce::String (gAudioAllocs.load()) + " / " + juce::String (gAudioLocks.load()) + ")");
        p.setSpectreListening (false);
        p.processBlock (buf, midi);   // (the player takes the stop at the next block)
        check (! p.spectreListening(), "SPECTRE: LISTEN again stops it");
        // saved and restored with the session
        juce::MemoryBlock mb; p.refreshState(); p.getStateInformation (mb);
        Processor q; q.setStateInformation (mb.getData(), (int) mb.getSize());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (400);
        check (std::abs (q.spectreParam (Transpose) - 5.0f) < 1e-4f && q.spectreLiveOn() == Processor::isFx && q.spectreTableName() == "SAW" && q.spectreTable() == tableCycle (1),
               "SPECTRE: its controls, its WAVETABLE (and LIVE) come back with the session");
        if (Processor::isFx)
        {
            // a session saved before build 17 (no SPECTRE in it): the track still plays dry
            auto t = p.engine.saveState(); juce::MemoryBlock old;
            if (auto x = t.createXml()) juce::AudioProcessor::copyXmlToBinary (*x, old);
            Processor r; r.setStateInformation (old.getData(), (int) old.getSize());
            juce::MessageManager::getInstance()->runDispatchLoopUntil (400);
            check (! r.spectreLiveOn(), "FX: a session from before SPECTRE opens with LIVE off (the track plays as it always did)");
        }
    }
    // ---- a held key on the SPECTRE / SPACE page plays through that room live, round and round while it is held
    for (int which : { 2, 1 })
    {
        Processor p;
        p.setRateAndBufferSizeDetails (48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        p.setSpectreLive (false);
        p.engine.setSpecimen (voice, "voice"); p.syncPlayer();
        p.setLiveRoom (which);
        juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer midi;
        const int soundBlocks = (int) (voice->seconds() * 48000.0 / 512.0);
        double held = 0, after = 0; gAudioAllocs = 0; gAudioLocks = 0;
        for (int blk = 0; blk < soundBlocks * 2 + 400; ++blk)
        {
            midi.clear();
            if (blk == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
            if (blk == soundBlocks * 2) midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
            buf.clear();
            tlAudio = true; p.processBlock (buf, midi); tlAudio = false;
            if (blk > soundBlocks + 10 && blk < soundBlocks * 2) held += buf.getRMSLevel (0, 0, 512);
            if (blk > soundBlocks * 2 + 350) after += buf.getRMSLevel (0, 0, 512);
        }
        const juce::String room = which == 2 ? "SPECTRE" : "SPACE";
        check (held > 0.2, room + ": a held key keeps playing the sound through it, past the sound's end (" + juce::String (held, 2) + ")");
        check (after < 0.05, room + ": let go of the key and it rings out to silence (" + juce::String (after, 4) + ")");
        check (gAudioAllocs.load() == 0 && gAudioLocks.load() == 0, room + ": held-key live play allocates and locks nothing on the audio thread");
        // and SPECTRE's controls are heard at once: TRANSPOSE +12 moves the held note up an octave
        if (which == 2)
        {
            auto tone = std::make_shared<Audio>(); tone->sampleRate = 48000.0; tone->buf.setSize (1, 96000);
            for (int i = 0; i < 96000; ++i) tone->buf.setSample (0, i, 0.4f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * i / 48000.0));
            p.engine.setSpecimen (tone, "tone"); p.syncPlayer();
            auto pitchOf = [&] (float tr)
            {
                p.setSpectreParam (spectre::Transpose, tr);
                midi.clear(); midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.9f), 0);
                std::vector<float> got;
                for (int blk = 0; blk < 200; ++blk) { buf.clear(); p.processBlock (buf, midi); midi.clear(); if (blk > 60) got.insert (got.end(), buf.getReadPointer (0), buf.getReadPointer (0) + 512); }
                midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0); buf.clear(); p.processBlock (buf, midi);
                return zcFreq (got.data() + 2000, (int) got.size() - 2000, 48000.0);
            };
            const float f0 = pitchOf (0.0f), f12 = pitchOf (12.0f);
            check (std::abs (f0 - 220.0f) < 4.0f && std::abs (f12 - 440.0f) < 6.0f, "SPECTRE: moving TRANSPOSE while the key is held is heard at once (" + juce::String (f0, 0) + " Hz -> " + juce::String (f12, 0) + " Hz)");
            p.setSpectreParam (spectre::Transpose, 0.0f);
        }
    }
    // ---- the page
    {
        Editor::opensOnSpectre = true;
        Processor p;
        p.engine.setSpecimen (voice, "voice"); p.syncPlayer();
        std::unique_ptr<Editor> ed (dynamic_cast<Editor*> (p.createEditor()));
        ed->setSize (Editor::kW, Editor::kH); ed->refresh();
        Editor::opensOnSpectre = false;
        check (ed->roomNow() == rooms::Room::Spectre && ed->spectrePage().isVisible() && ! ed->roomPage().isVisible(), "SPECTRE: Ghost Lab opens on SPECTRE's page");
        check (RouteBar::roomAt (0) == rooms::Room::Spectre && RouteBar::roomAt (1) == rooms::Room::Lab, "SPECTRE: it is first on the strip of rooms, THE LAB next");
        auto& pg = ed->spectrePage();
        const bool mono = isEffectivelyMono (*voice) || voice->channels() == 1;
        check (pg.controlEnabled (spectre::Transpose) && pg.controlEnabled (spectre::Cloud) && pg.controlEnabled (spectre::Side) == ! mono,
               "SPECTRE: with a sound, its controls are live; SIDE is greyed on a mono sound");
        pg.setValue (spectre::Inf, 1.0f);
        check (! pg.controlEnabled (spectre::Cloud) && ! pg.controlEnabled (spectre::Smooth), "SPECTRE: CLOUD and SMOOTH grey out while INF holds everything");
        pg.setValue (spectre::Inf, 0.0f);
        pg.setValue (spectre::Transpose, -5.0f);
        check (std::abs (p.spectreParam (spectre::Transpose) + 5.0f) < 1e-4f, "SPECTRE: the page sets the engine's controls");
        const int before = p.engine.historySize();
        pg.pressPrint();
        for (int i = 0; i < 400 && p.engine.busy(); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        check (p.engine.historySize() == before + 1 && p.engine.current().label.startsWith ("SPECTRE -5.0 st"), "SPECTRE: PRINT IT adds one step to the history (" + p.engine.current().label + ")");
        pg.setValue (spectre::Transpose, 3.0f);
        pg.pressPrint();
        for (int i = 0; i < 400 && p.engine.busy(); ++i) juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (100);
        check (p.engine.historySize() == before + 1 && p.engine.current().label.startsWith ("SPECTRE +3.0 st"), "SPECTRE: printing again replaces the last print (nothing stacks)");
        pg.pressReset();
        check (std::abs (p.spectreParam (spectre::Transpose)) < 1e-6f && std::abs (p.spectreParam (spectre::Partials) - 512.0f) < 0.5f && p.spectreTableName() == "SINE", "SPECTRE: RESET puts every control back (and the partials back to sines)");
        pg.stepTable (1);
        check (p.spectreTableName() == "SAW" && ! p.spectreTable().empty(), "SPECTRE: the WAVETABLE arrows pick the built-in tables");
        pg.stepTable (-1);
        check (p.spectreTableName() == "SINE" && p.spectreTable().empty(), "SPECTRE: ... and back to SINE");
        pg.setValue (spectre::Inf, 1.0f);
        check (! pg.controlEnabled (spectre::Transient), "SPECTRE: TRANSIENT greys out while INF holds everything");
        pg.setValue (spectre::Inf, 0.0f);
        check (p.liveRoomNow() == 2, "SPECTRE: while its page is open, held keys play through it");
        ed->setRoom (rooms::Room::Lab);
        check (! ed->spectrePage().isVisible() && ed->body().isVisible() && p.liveRoomNow() == 0, "SPECTRE: THE LAB is one tap away (and keys play the plain sound there)");
        ed->setRoom (rooms::Room::Stretch);
        check (ed->roomPage().isVisible() && ! ed->spectrePage().isVisible(), "SPECTRE: the other rooms still open their own pages");
        ed->setRoom (rooms::Room::Space);
        check (p.liveRoomNow() == 1, "SPACE: while its page is open, held keys play through it live too");
    }
}

static void spectreShots (const juce::File& dir)
{
    dir.createDirectory();
    auto voice = synthVoice();
    if (auto v = demoSource ("voice")) voice = v;
    Editor::opensOnSpectre = true;
    Processor p;
    p.setRateAndBufferSizeDetails (48000.0, 512); p.prepareToPlay (48000.0, 512);
    p.engine.setSpecimen (voice, "voice"); p.syncPlayer();
    std::unique_ptr<Editor> ed (dynamic_cast<Editor*> (p.createEditor()));
    auto pump = [&] (int blocks)
    {
        juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer midi;
        for (int b = 0; b < blocks; ++b) { buf.clear(); for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) buf.setSample (c, i, Processor::isFx ? voice->buf.getSample (0, (b * 512 + i) % voice->length()) * 0.8f : 0.0f); p.processBlock (buf, midi); }
        juce::MessageManager::getInstance()->runDispatchLoopUntil (120);
    };
    auto shot = [&] (const juce::String& name, int w, int h) { ed->setSize (w, h); ed->hideToast(); ed->refresh(); juce::MessageManager::getInstance()->runDispatchLoopUntil (80); savePng (*ed, dir.getChildFile ("ghostlab-spectre-" + name + ".png")); };
    ed->setSize (Editor::kW, Editor::kH);
    ed->spectrePage().pressListen();
    pump (90);
    shot ("1-open", Editor::kW, Editor::kH);
    ed->spectrePage().setValue (spectre::PhaseX, -0.55f); ed->spectrePage().setValue (spectre::CepstrumY, 0.4f);
    ed->spectrePage().setValue (spectre::Transpose, 7.0f); ed->spectrePage().setValue (spectre::Cloud, 0.6f); ed->spectrePage().setValue (spectre::Inf, 0.35f);
    ed->spectrePage().stepTable (7);   // GHOST
    pump (90);
    shot ("2-moved", Editor::kW, Editor::kH);
    shot ("3-big", 1600, 1000);
    shot ("4-narrow", 420, 900);
    Editor::opensOnSpectre = false;
}

int main (int argc, char** argv)
{
    Editor::opensOnSpectre = false;   // (these tests drive THE LAB; SPECTRE's own tests open it themselves)
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::String mode = argc > 1 ? argv[1] : "";
    if (mode == "demos") { demos (juce::File (argc > 2 ? argv[2] : "demos")); return 0; }
    if (mode == "shots") { shots (juce::File (argc > 2 ? argv[2] : "shots")); return 0; }
    if (mode == "spectreshots") { spectreShots (juce::File (argc > 2 ? argv[2] : "spectreshots")); return 0; }
    if (mode == "roomshots") { roomShots (juce::File (argc > 2 ? argv[2] : "roomshots")); return 0; }
    if (mode == "bodyshots") { bodyShots (juce::File (argc > 2 ? argv[2] : "bodyshots")); return 0; }
    if (mode == "bodydemos") { bodyDemos (juce::File (argc > 2 ? argv[2] : "bodydemos")); return 0; }
    if (mode == "terrain-demos") { terrainDemos (juce::File (argc > 2 ? argv[2] : "terrain-demos")); return 0; }

   #if GHOSTLAB_FX
    testPlugin (synthVoice());
    testBodyLive (synthVoice());
    testOpenSize();
    testFreshStart (synthVoice());
    testNoUpstreamNames();
    testSpectre (synthVoice());
    std::printf ("\n%d passed, %d failed\n", passes, failures);
    return failures == 0 ? 0 : 1;
   #endif
    Engine e;
    check (e.cdp->available(), "the CDP programs are found (" + e.cdp->programsDir().getFullPathName() + ")");
    if (! e.cdp->available()) { std::printf ("set GHOSTLAB_CDP_DIR\n"); return 1; }
    auto voice = synthVoice(), piano = synthPiano();
    const auto only = mode;
    if (only.isEmpty() || only == "meters") testMeters();
    if (only.isEmpty() || only == "rooms") testRooms (voice);
    if (only.isEmpty() || only == "actions") testActions (e, voice, piano);
    if (only.isEmpty() || only == "undo") testUndo (e, voice);
    if (only.isEmpty() || only == "breed") testBreed (e, voice, piano);
    if (only.isEmpty() || only == "chain") testChain (e, voice);
    if (only.isEmpty() || only == "audio") testAudioThread (voice, piano);
    if (only.isEmpty() || only == "errors") testErrors (voice);
    if (only.isEmpty() || only == "gestures") testGestures();
    if (only.isEmpty() || only == "gestures" || only == "feel") testBodyFeel (voice);
    if (only.isEmpty() || only == "bodylive") { testBodyLive (voice); testBodyInEditor (voice); }
    if (only.isEmpty() || only == "state") testState (voice);
    if (only.isEmpty() || only == "plugin") testPlugin (voice);
    if (only.isEmpty() || only == "smoke") testSmoke (voice);
    if (only.isEmpty() || only == "terrain") testTerrain (voice);
    if (only.isEmpty() || only == "terrainmap") testTerrainMap (voice);
    if (only.isEmpty() || only == "hood") { testHood (e, voice, piano); testHoodPage (voice); }
    if (only.isEmpty() || only == "hood" || only == "dialogs") { testHoodDialogs (e); testHoodLayout(); }
    if (only.isEmpty() || only == "hood" || only == "datafiles") testHoodDataFiles();
    if (only.isEmpty() || only == "long") testLong (e);
    if (only.isEmpty() || only == "size") testOpenSize();
    if (only.isEmpty() || only == "fresh") testFreshStart (voice);
    if (only.isEmpty() || only == "names") testNoUpstreamNames();
    if (only.isEmpty() || only == "rooms" || only == "roompages") testRoomPages (voice);
    if (only.isEmpty() || only == "trim") testTrim (voice);
    if (only.isEmpty() || only == "spectre") testSpectre (voice);
    e.sessionFolder().deleteRecursively();
    std::printf ("\n%d passed, %d failed\n", passes, failures);
    return failures == 0 ? 0 : 1;
}
