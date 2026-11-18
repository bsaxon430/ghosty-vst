#include <time.h>
// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX test harness: the quality-bar check table, run offline against every effect the rack holds (Phase 1: the pass-through),
// plus the rack checks (state round trip, sample-rate / buffer changes, fresh start, window size, CPU budget, tempo lock).
// Prints one PASS / FAIL line per check with the measured numbers. Exit code = number of failures.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "../src/plugin/PluginProcessor.h"
#include "../src/plugin/PluginEditor.h"
#include "../src/effects/PassThrough.h"
#include "../src/dsp/Modulators.h"
#include "../src/effects/Catalog.h"
#include "../src/dsp/SstWrap.h"
#include <chrono>
#include <cstdio>
#include <random>
#include <set>

using namespace phantom;
static int failures = 0, passes = 0;
static void report (bool ok, const juce::String& name, const juce::String& numbers)
{
    std::printf ("%s %s: %s\n", ok ? "PASS" : "FAIL", name.toRawUTF8(), numbers.toRawUTF8()); std::fflush (stdout);
    (ok ? passes : failures)++;
}
static float db (float v) { return 20.0f * std::log10 (juce::jmax (1.0e-12f, v)); }
static float peakOf (const juce::AudioBuffer<float>& b) { float p = 0; for (int c = 0; c < b.getNumChannels(); ++c) p = juce::jmax (p, b.getMagnitude (c, 0, b.getNumSamples())); return p; }
static float rmsOf (const juce::AudioBuffer<float>& b) { double s = 0; for (int c = 0; c < b.getNumChannels(); ++c) { const float r = b.getRMSLevel (c, 0, b.getNumSamples()); s += r * r; } return std::sqrt ((float) (s / b.getNumChannels())); }
static bool finite (const juce::AudioBuffer<float>& b) { for (int c = 0; c < b.getNumChannels(); ++c) for (int i = 0; i < b.getNumSamples(); ++i) if (! std::isfinite (b.getSample (c, i))) return false; return true; }

// ---------------------------------------------------------------- a slot under test: one effect, host rate, no global stage
struct Bench
{
    double sr; int block; EffectSlot slot; TempoInfo tempo;
    Bench (std::unique_ptr<PhantomEffect> fx, double sampleRate = 48000.0, int blockSize = 256, OversamplingPolicy pol = OversamplingPolicy::X4)
        : sr (sampleRate), block (blockSize), slot (std::move (fx))
    { slot.setPolicy (pol); slot.prepare (sr, block, 2); tempo.sampleRate = sr; tempo.playing = true; }
    void setDefaults() { auto& ps = slot.params(); for (size_t i = 0; i < ps.size(); ++i) slot.setValue ((int) i, ps[i].def); slot.setMix (1.0f); slot.setTrimDb (0.0f); slot.setEnabled (true);
        if (auto* e = std::getenv ("PHX_SET")) for (auto& kv : juce::StringArray::fromTokens (e, ",", "")) for (size_t i = 0; i < ps.size(); ++i) if (ps[i].id == kv.upToFirstOccurrenceOf ("=", false, false).toStdString()) slot.setValue ((int) i, kv.fromFirstOccurrenceOf ("=", false, false).getFloatValue()); }   // debug only
    void run (juce::AudioBuffer<float>& buf, std::function<void (int blockIndex)> perBlock = {})
    {
        juce::ScopedNoDenormals noDenormals;   // as the plugin's processBlock has
        const int n = buf.getNumSamples(); int bi = 0;
        for (int i = 0; i < n; i += block)
        {
            const int len = juce::jmin (block, n - i);
            if (perBlock) perBlock (bi++);
            juce::dsp::AudioBlock<float> b (buf.getArrayOfWritePointers(), 2, (size_t) i, (size_t) len);
            slot.process (b, tempo);
            tempo.ppqPosition += len / tempo.samplesPerBeat();
        }
    }
};
static juce::AudioBuffer<float> sine (double sr, int n, float hz, float amp, bool stereoDifferent = false)
{
    juce::AudioBuffer<float> b (2, n);
    for (int i = 0; i < n; ++i) { const float v = (float) (amp * std::sin (juce::MathConstants<double>::twoPi * hz * i / sr)); b.setSample (0, i, v); b.setSample (1, i, stereoDifferent ? (float) (amp * std::sin (juce::MathConstants<double>::twoPi * hz * 1.5 * i / sr)) : v); }
    return b;
}
static juce::AudioBuffer<float> noise (int n, float amp, unsigned seed = 7)
{
    juce::AudioBuffer<float> b (2, n); std::mt19937 rng (seed); std::uniform_real_distribution<float> d (-1.0f, 1.0f);
    for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) b.setSample (c, i, amp * d (rng));
    return b;
}
// white noise band-limited to the audio band (0 .. 20 kHz at 48 kHz; brick-wall in the FFT domain, circular): what an oversampled effect
// can pass untouched. Above ~21 kHz the oversampler's own anti-alias filters roll off (as in every oversampled plugin), so full-band
// white noise cannot null to -100 dB through any oversampled path; real audio is band-limited by its own converters.
// pink noise (Paul Kellet's filter on white), the same on both channels unless asked, peak-normalised to amp
static juce::AudioBuffer<float> pinkNoise (int n, float amp, unsigned seed = 7)
{
    juce::AudioBuffer<float> b (2, n); std::mt19937 rng (seed); std::uniform_real_distribution<float> d (-1.0f, 1.0f);
    for (int c = 0; c < 2; ++c)
    {
        float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0, pk = 1.0e-9f;
        for (int i = 0; i < n; ++i)
        {
            const float w = d (rng);
            b0 = 0.99886f * b0 + w * 0.0555179f; b1 = 0.99332f * b1 + w * 0.0750759f; b2 = 0.96900f * b2 + w * 0.1538520f; b3 = 0.86650f * b3 + w * 0.3104856f;
            b4 = 0.55000f * b4 + w * 0.5329522f; b5 = -0.7616f * b5 - w * 0.0168980f; const float y = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f; b6 = w * 0.115926f;
            b.setSample (c, i, y); pk = juce::jmax (pk, std::abs (y));
        }
        b.applyGain (c, 0, n, amp / pk);
    }
    return b;
}
static juce::AudioBuffer<float> bandNoise (double sr, int n, float amp, unsigned seed = 7, double topHz = 20000.0)
{
    int order = 1; while ((1 << order) < n) ++order; const int N = 1 << order;
    juce::dsp::FFT fft (order); juce::AudioBuffer<float> b (2, n); std::mt19937 rng (seed); std::normal_distribution<float> d (0.0f, 1.0f);
    for (int c = 0; c < 2; ++c)
    {
        std::vector<juce::dsp::Complex<float>> x ((size_t) N), y ((size_t) N);
        for (auto& v : x) v = { d (rng), 0.0f };
        fft.perform (x.data(), y.data(), false);
        const int top = (int) (topHz * N / sr); for (int k = top; k <= N - top; ++k) y[(size_t) k] = {};
        y[0] = {};
        fft.perform (y.data(), x.data(), true);
        double s = 0; for (int i = 0; i < n; ++i) s += (double) x[(size_t) i].real() * x[(size_t) i].real();
        const float g = amp * 0.577f / (float) std::sqrt (s / n);   // same RMS as uniform noise of peak `amp`
        for (int i = 0; i < n; ++i) b.setSample (c, i, x[(size_t) i].real() * g);
    }
    return b;
}
// magnitude spectrum (dBFS per bin, Hann, sine-normalised) of channel 0 over [start, start+N)
static std::vector<float> spectrumDb (const juce::AudioBuffer<float>& b, int start, int order)
{
    const int N = 1 << order; juce::dsp::FFT fft (order); std::vector<float> buf ((size_t) 2 * N, 0.0f);
    for (int i = 0; i < N; ++i) { const float w = 0.5f - 0.5f * std::cos ((float) (juce::MathConstants<double>::twoPi * i / N)); buf[(size_t) i] = b.getSample (0, start + i) * w; }
    fft.performFrequencyOnlyForwardTransform (buf.data());
    std::vector<float> out ((size_t) N / 2); for (int k = 0; k < N / 2; ++k) out[(size_t) k] = db (buf[(size_t) k] * 4.0f / N);   // sine at full scale -> 0 dB
    return out;
}

// zipper: one continuous control swept fast (64 steps over 200 ms, up and down, repeatedly) on a 1 kHz tone; the loudest component above
// 6 kHz that is not a harmonic of the tone (harmonics are the effect's distortion, not zipper)
static bool zipVerbose = false; static float zipLo = 0.0f, zipHi = 1.0f;
static float zipperRest = -200.0f;   // the last zipperOf call's reading with the control held still (the engine's own floor)
static float zipperRender (std::function<std::unique_ptr<PhantomEffect>()> make, int pi, std::vector<std::pair<std::string, float>> overrides, bool sweep);
static float zipperOf (std::function<std::unique_ptr<PhantomEffect>()> make, int pi, std::vector<std::pair<std::string, float>> overrides)
{
    const float hi = zipperRender (make, pi, overrides, true);
    // held still at its lowest, middle and highest swept value: the engine's own floor anywhere in the range the sweep covers
    zipperRest = -200.0f;
    if (hi >= -90.0f) { const float lo = zipLo, hi2 = zipHi; for (float t : { lo, 0.5f * (lo + hi2), hi2 }) { zipLo = zipHi = t; zipperRest = juce::jmax (zipperRest, zipperRender (make, pi, overrides, false)); } zipLo = lo; zipHi = hi2; }
    return hi;
}
static float zipperRender (std::function<std::unique_ptr<PhantomEffect>()> make, int pi, std::vector<std::pair<std::string, float>> overrides, bool sweep)
{
    const double sr = 48000.0; Bench b (make()); b.setDefaults(); const auto specs = b.slot.params();
    { std::vector<float> v (specs.size()); for (size_t i = 0; i < specs.size(); ++i) v[i] = specs[i].def; b.slot.fx().zipperIsolate (specs[(size_t) pi].id, v); for (size_t i = 0; i < v.size(); ++i) b.slot.setValue ((int) i, v[i]); }
    for (auto& [id, v] : overrides) for (size_t i = 0; i < specs.size(); ++i) if (specs[i].id == id) b.slot.setValue ((int) i, v);
    if (auto* e = std::getenv ("PHX_SET")) for (auto& kv : juce::StringArray::fromTokens (e, ",", "")) for (size_t i = 0; i < specs.size(); ++i) if (specs[i].id == kv.upToFirstOccurrenceOf ("=", false, false).toStdString()) b.slot.setValue ((int) i, kv.fromFirstOccurrenceOf ("=", false, false).getFloatValue());   // debug only
    b.slot.applyPolicy();   // the message-thread update a host's parameter change gets before playback
    auto buf = sine (sr, (int) sr * 2, 1000.0f, 0.5f);
    const int stepBlocks = juce::jmax (1, (int) (0.2 * sr / 64 / b.block));
    b.run (buf, [&] (int bi) { const int step = (bi / stepBlocks) % 128; const float t = sweep ? zipLo + (zipHi - zipLo) * (step < 64 ? step / 63.0f : (127 - step) / 63.0f) : 0.5f * (zipLo + zipHi); b.slot.setValue (pi, specs[(size_t) pi].min + (specs[(size_t) pi].max - specs[(size_t) pi].min) * t); });
    // for a nonlinear module, a control that moves the level into the drive stage (a cut, a crossover) makes the harmonics swell and shrink:
    // that shows as sidebands within +-150 Hz of each harmonic (the sweep is a 2.5 Hz triangle), so those bands are left out for it; a
    // stepped control would spray energy across the gaps between harmonics, which is what this measures
    const double guard = juce::jmax (b.slot.fx().isNonlinear() ? 150.0 : 10.5 * sr / (1 << 13), b.slot.fx().ownSpreadHz());
    float hi = -200.0f; const int order = 13, N = 1 << order;
    for (int start = (int) sr / 2; start + N < buf.getNumSamples(); start += N / 2) { auto sp = spectrumDb (buf, start, order); for (int k = (int) (6000.0 * N / sr); k < N / 2; ++k) { const double f = k * sr / N, h = f / 1000.0; if (std::abs (h - std::round (h)) * 1000.0 < guard) continue; if (zipVerbose && sp[(size_t) k] > hi) std::printf ("  %.0f Hz %.1f dB @%.2fs\n", f, sp[(size_t) k], start / sr); hi = juce::jmax (hi, sp[(size_t) k]); } }
    return hi;
}

// worst aliasing in the audio band (to 20 kHz) for sines at 2 / 5.1 / 7.3 / 11 kHz, 0.9 peak, every drive-like control at max, the module at
// its own oversampling factor (the policy's Render setting lets it have up to 16x). Aliases that fold into 20-24 kHz come from the
// half-band filters' own transition band and are left out. A non-harmonic component that is identical (within 1 dB) when the same
// render is made at half the oversampling factor is the engine's own modulation (a compressor's gain movement, an envelope), not
// aliasing: aliases move to other frequencies when the factor changes. Those are reported separately as `modulation`.
static void renderTone (std::function<std::unique_ptr<PhantomEffect>()> make, OversamplingPolicy pol, const std::vector<std::pair<std::string, float>>& overrides, float fq, std::vector<float>& sp)
{
    const double sr = 48000.0; Bench bb (make(), sr, 256, pol); bb.setDefaults(); auto& specs = bb.slot.params();
    { std::vector<float> v (specs.size()); for (size_t i = 0; i < specs.size(); ++i) v[i] = specs[i].def; bb.slot.fx().aliasIsolate (v); for (size_t i = 0; i < v.size(); ++i) bb.slot.setValue ((int) i, v[i]); }
    for (size_t i = 0; i < specs.size(); ++i) if (specs[i].isContinuous() && (specs[i].id.find ("drive") != std::string::npos || specs[i].id.find ("crush") != std::string::npos || specs[i].id.find ("fat") != std::string::npos || specs[i].id == "dist" || specs[i].id == "mag")) bb.slot.setValue ((int) i, specs[i].max);
    for (auto& [id, v] : overrides) for (size_t i = 0; i < specs.size(); ++i) if (specs[i].id == id) bb.slot.setValue ((int) i, v);
    bb.slot.applyPolicy();
    const int order = 15, N = 1 << order; auto buf = sine (sr, N * 2, fq, 0.9f); bb.run (buf); sp = spectrumDb (buf, N, order);
}
static OversamplingPolicy policyFor (int factor) { return factor >= 16 ? OversamplingPolicy::Render : factor >= 8 ? OversamplingPolicy::X8 : factor >= 4 ? OversamplingPolicy::X4 : factor >= 2 ? OversamplingPolicy::X2 : OversamplingPolicy::Off; }
static float worstAlias (std::function<std::unique_ptr<PhantomEffect>()> make, OversamplingPolicy pol, std::vector<std::pair<std::string, float>> overrides, juce::String& at, float* modulation = nullptr)
{
    const double sr = 48000.0; float worst = -200.0f, worstMod = -200.0f; const int order = 15, N = 1 << order;
    const int own = make()->oversamplingFactor(); const int f = juce::jmin (own, policyFactor (pol)); const auto refPol = policyFor (f / 2);
    for (float f0 : { 2000.0f, 5100.0f, 7300.0f, 11000.0f })
    {
        const float fq = (float) (std::round (f0 * N / sr) * sr / N);   // bin-centred so leakage is nil
        std::vector<float> sp, ref; renderTone (make, pol, overrides, fq, sp); if (f > 1) renderTone (make, refPol, overrides, fq, ref);
        // a harmonic (+-25 Hz: slow pitch / level modulation, such as tape wow, sits right beside the harmonics and is not aliasing)
        for (int k = 2; k < (int) (20000.0 * N / sr); ++k) { const double fr = k * sr / N; const double h = fr / fq; if (std::abs (h - std::round (h)) * fq < 25.0) continue;
            const bool mod = f > 1 && std::abs (sp[(size_t) k] - ref[(size_t) k]) < 1.0f && sp[(size_t) k] > -140.0f;
            if (mod) { worstMod = juce::jmax (worstMod, sp[(size_t) k]); continue; }
            if (sp[(size_t) k] > worst) { worst = sp[(size_t) k]; at = juce::String (fq, 0) + " Hz -> " + juce::String (fr, 0) + " Hz"; } }
    }
    if (modulation != nullptr) *modulation = worstMod;
    return worst;
}

// the factory bank: every preset loads into a brand-new instance with every name in it known, gives the same state whatever was
// loaded before it, plays a -18 dBFS noise without clipping or going silent or blowing up, and costs well under the rack's CPU
// budget; between them the presets use all 24 units; names are unique plain words
static void factoryChecks()
{
    const int n = PhantomProcessor::numFactoryPresets(); juce::StringArray bad, names; std::array<bool, kNumUnits> used {}; double worstCpu = 0, loudLo = 99, loudHi = -99; juce::String worstName;
    static const char* banned[] { "vital", "surge", "chow", "airwindows", "dragonfly", "cloudseed", "valhalla", "mango", "steam", "zl ", "lsp", "rubber", "signalsmith", "ott", "serum", "fabfilter", "soundtoys", "brain" };
    auto snapshot = [] (PhantomProcessor& p) { juce::String t; for (auto* q : p.getParameters()) t << juce::String (q->getValue(), 6) << ","; const auto tp = p.getTopology(); for (int v : tp.order) t << v << "."; for (int u = 0; u < kNumUnits; ++u) t << juce::JSON::toString (p.getUnitData (u), true); return t; };
    for (int i = 0; i < n; ++i)
    {
        const auto name = PhantomProcessor::factoryPresetName (i); const auto tags = PhantomProcessor::factoryPresetTags (i);
        if (names.contains (name)) bad.add (name + ": name used twice"); names.add (name);
        if (name.length() < 3 || name.length() > 20 || tags.isEmpty()) bad.add (name + ": name length or no tag");
        for (auto* w : banned) if ((name.toLowerCase() + " ").contains (w)) bad.add (name + ": a name that is not ours");
        PhantomProcessor p; p.prepareToPlay (48000.0, 256); juce::String problem;
        if (! p.loadFactoryPreset (i, &problem)) { bad.add (name + ": unknown " + problem); continue; }
        int on = 0; for (int u = 0; u < kNumUnits; ++u) if (p.getParamNormalised (PhantomProcessor::pid (u, "on")) >= 0.5f) { used[(size_t) u] = true; if (u != uLim) ++on; }
        if (on == 0) bad.add (name + ": switches nothing on");
        // the same whatever came before
        { PhantomProcessor q; q.prepareToPlay (48000.0, 256); q.loadFactoryPreset ((i + 7) % n); q.loadFactoryPreset (i); if (snapshot (q) != snapshot (p)) bad.add (name + ": depends on the preset before it"); }
        // sound: 3 s of noise at -18 dBFS RMS
        auto buf = noise (48000 * 3, 0.218f, 31); const float inRms = db (buf.getRMSLevel (0, 96000, 48000)); juce::MidiBuffer m;
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        for (int k = 0; k + 256 <= buf.getNumSamples(); k += 256) { juce::AudioBuffer<float> part (buf.getArrayOfWritePointers(), 2, k, 256); p.processBlock (part, m); }
        const double cpu = (juce::Time::getMillisecondCounterHiRes() - t0) / 3000.0 * 100.0; if (cpu > worstCpu) { worstCpu = cpu; worstName = name; }
        const float peak = db (buf.getMagnitude (0, buf.getNumSamples())), outRms = db (juce::jmax (buf.getRMSLevel (0, 96000, 48000), buf.getRMSLevel (1, 96000, 48000)));
        loudLo = juce::jmin (loudLo, (double) (outRms - inRms)); loudHi = juce::jmax (loudHi, (double) (outRms - inRms));
        if (! finite (buf)) bad.add (name + ": not a number in the output");
        if (peak > -0.2f) bad.add (name + ": peak " + juce::String (peak, 2) + " dBFS (over the limiter's -0.3 ceiling)");
        if (outRms - inRms > 9.0f || outRms - inRms < -12.0f) bad.add (name + ": level changes by " + juce::String (outRms - inRms, 1) + " dB");
        if (cpu > 40.0) bad.add (name + ": " + juce::String (cpu, 1) + " % of one core");
    }
    juce::StringArray unusedUnits; for (int u = 0; u < kNumUnits; ++u) if (! used[(size_t) u]) unusedUnits.add (unitDef (u).shortName);
    report (bad.isEmpty() && unusedUnits.isEmpty() && n >= 24, "Factory presets", juce::String (n) + " presets; every one loads into a new instance with every control name known, and is the same whatever was loaded before; units never used: " + (unusedUnits.isEmpty() ? juce::String ("none (all 24 are)") : unusedUnits.joinIntoString (", "))
            + "; -18 dBFS noise comes out between " + juce::String (loudLo, 1) + " and " + juce::String (loudHi, 1) + " dB of its level, never over the ceiling; the heaviest (" + worstName + ") takes " + juce::String (worstCpu, 1) + " % of one core (limit 40)"
            + (bad.isEmpty() ? juce::String() : "; wrong: " + bad.joinIntoString ("; ").substring (0, 900)));
}

// RETRO with TAPE BIAS turned up: the hysteresis model at Chow's widest setting is the hardest case for aliasing
static void retroBiasAlias()
{
    juce::String line; float worstAll = -200.0f;
    for (float bias : { 0.0f, 0.5f, 1.0f })
    {
        juce::String at; float mod = -200.0f; const float w = worstAlias ([] { return makeEffect (uRetro); }, OversamplingPolicy::Render, { { "mag_on", 1.0f }, { "bias", bias } }, at, &mod);
        worstAll = juce::jmax (worstAll, w); line += "bias " + juce::String (juce::roundToInt (bias * 100.0f)) + " %: " + juce::String (w, 1) + " dBFS (" + at + ")  ";
    }
    report (worstAll < -80.0f, "RETRO aliasing with tape bias", "MAGNETIC at full, worst alias below 20 kHz: " + line.trim() + " (limit -80)");
}

// ---------------------------------------------------------------- the check table against one effect
// ---- Reverb tail (18-20; DOORWAY informational): a 0.5 s noise burst, then 40 s of silence at the defaults. The tail's level (200 ms RMS
// windows, 50 ms apart) must never rise more than 1 dB above the lowest level it has already reached, and must reach -120 dBFS. Late-tail
// ringing: from 40 dB below the tail's start, a 1 s Welch spectrum (4096 Hann, 75 % overlap) in 1/12-octave bands (at least 100 Hz wide), 100 Hz to 16 kHz; no band
// (within 50 dB of the loudest) more than 6 dB above the median of the bands within +-1/3 octave of it
static void tailCheck (const char* name, std::function<std::unique_ptr<PhantomEffect>()> make, bool informational)
{
    const double sr = 48000.0; Bench b (make()); b.setDefaults();
    const int burst = (int) (sr * 0.5), total = (int) (sr * 40.5);
    juce::AudioBuffer<float> buf (2, total); buf.clear(); { auto nz = noise (burst, 0.5f, 21); for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, nz, c, 0, burst); }
    b.run (buf);
    const int win = (int) (sr * 0.2), hop = (int) (sr * 0.05);
    std::vector<float> lv; for (int st = burst; st + win <= total; st += hop) { double e = 0; for (int c = 0; c < 2; ++c) for (int i = 0; i < win; ++i) { const double x = buf.getSample (c, st + i); e += x * x; } lv.push_back (10.0f * (float) std::log10 (e / (2.0 * win) + 1e-30)); }
    float runMin = lv.empty() ? 0.0f : lv[0], worstRise = -100.0f; double reach = -1.0; const float start = lv.empty() ? -200.0f : lv[0];
    float riseAt = 0, riseLv = 0; for (size_t k = 1; k < lv.size(); ++k) { if (reach < 0 && lv[k] - runMin > worstRise) { worstRise = lv[k] - runMin; riseAt = k * 0.05f; riseLv = lv[k]; } runMin = juce::jmin (runMin, lv[k]); if (reach < 0 && lv[k] < -120.0f) reach = (double) k * 0.05; }
    // late-tail ringing
    int segStart = -1; for (size_t k = 0; k < lv.size(); ++k) if (lv[k] < start - 40.0f) { segStart = burst + (int) k * hop; break; }
    float worstPeak = -100.0f, atHz = 0.0f;
    if (segStart > 0 && segStart + (int) sr < total)
    {
        const int N = 4096, H = 1024; juce::dsp::FFT fft (12); std::vector<double> P ((size_t) N / 2, 0.0); std::vector<float> w ((size_t) N), tmp ((size_t) N * 2);
        for (int i = 0; i < N; ++i) w[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * i / N);
        int frames = 0;
        for (int st = segStart; st + N <= segStart + (int) sr; st += H, ++frames) for (int c = 0; c < 2; ++c)
        { std::fill (tmp.begin(), tmp.end(), 0.0f); for (int i = 0; i < N; ++i) tmp[(size_t) i] = buf.getSample (c, st + i) * w[(size_t) i]; fft.performFrequencyOnlyForwardTransform (tmp.data()); for (int k = 0; k < N / 2; ++k) P[(size_t) k] += (double) tmp[(size_t) k] * tmp[(size_t) k]; }
        std::vector<float> dB ((size_t) N / 2); for (int k = 0; k < N / 2; ++k) dB[(size_t) k] = 10.0f * (float) std::log10 (P[(size_t) k] + 1e-30);
        if (std::getenv ("PHX_DUMP")) { FILE* f = std::fopen (std::getenv ("PHX_DUMP"), "w"); for (int k = 0; k < N / 2; ++k) std::fprintf (f, "%.1f %.2f\n", k * sr / N, dB[(size_t) k]); std::fclose (f); }
        // ringing: power in 1/12-octave bands (a single bin of a reverb tail is a random mode: even a perfectly diffuse tail shows ~7 dB
        // single-bin peaks, Schroeder's statistics; a metallic ring is a narrow peak that stands out of its band's neighbours), each band
        // against the median of the bands within +-1/3 octave (not counting itself and its two neighbours), bands within 50 dB of the loudest
        std::vector<float> band; std::vector<float> bandHz;
        for (double f0 = 100.0, f1; (f1 = juce::jmax (f0 * std::pow (2.0, 1.0 / 12.0), f0 + 100.0)) <= 16000.0; f0 = f1)   // at least 100 Hz (8 bins) wide
        {
            const int k0 = (int) std::ceil (f0 * N / sr), k1 = (int) std::floor (f1 * N / sr) - 1; double e = 0; int cnt = 0;
            for (int k = k0; k <= k1; ++k) { e += P[(size_t) k]; ++cnt; }
            band.push_back (10.0f * (float) std::log10 (e / juce::jmax (1, cnt) + 1e-30)); bandHz.push_back ((float) (0.5 * (f0 + f1)));
        }
        const float top = *std::max_element (band.begin(), band.end());
        for (int i = 0; i < (int) band.size(); ++i)
        {
            if (band[(size_t) i] < top - 50.0f) continue;
            std::vector<float> nb; for (int j = juce::jmax (0, i - 4); j <= juce::jmin ((int) band.size() - 1, i + 4); ++j) if (std::abs (j - i) > 1) nb.push_back (band[(size_t) j]);
            if (nb.size() < 4) continue; std::nth_element (nb.begin(), nb.begin() + (long) nb.size() / 2, nb.end());
            const float ex = band[(size_t) i] - nb[nb.size() / 2]; if (ex > worstPeak) { worstPeak = ex; atHz = bandHz[(size_t) i]; }
        }
    }
    const bool ok = worstRise <= 1.0f && reach > 0.0 && worstPeak <= 6.0f;
    report (informational || ok, juce::String (name) + " reverb tail", juce::String (informational ? "(informational) " : "") + "tail starts " + juce::String (start, 1) + " dBFS, reaches -120 dBFS after " + (reach > 0 ? juce::String (reach, 2) + " s" : juce::String ("NEVER (40 s)"))
            + "; worst rise " + juce::String (worstRise, 2) + " dB (limit 1) at " + juce::String (riseAt, 2) + " s / " + juce::String (riseLv, 1) + " dBFS; late-tail peak " + juce::String (worstPeak, 2) + " dB above its neighbours at " + juce::String (atHz, 0) + " Hz (limit 6)");
}
// ---- Spectral artifacts (21): an impulse at the defaults: nothing before its own arrival (reported latency) above -60 dB of the output's
// peak (pre-echo); and at neutral the overlap-add reconstructs the input (identity, the null test's residual is reported again here)
static void spectralCheck (const char* name, std::function<std::unique_ptr<PhantomEffect>()> make)
{
    const double sr = 48000.0; Bench b (make()); b.setDefaults();
    juce::AudioBuffer<float> buf (2, (int) sr * 2); buf.clear(); const int at = (int) sr; buf.setSample (0, at, 0.5f); buf.setSample (1, at, 0.5f);
    b.run (buf); const int lat = b.slot.getLatencySamples(); float pk = 0, pre = 0;
    for (int i = 0; i < buf.getNumSamples(); ++i) { const float a = juce::jmax (std::abs (buf.getSample (0, i)), std::abs (buf.getSample (1, i))); pk = juce::jmax (pk, a); if (i < at + lat) pre = juce::jmax (pre, a); }
    Bench n (make()); n.setDefaults(); { auto& ps = n.slot.params(); std::vector<float> v (ps.size()); for (size_t i = 0; i < ps.size(); ++i) v[i] = ps[i].def; n.slot.fx().neutralise (v); for (size_t i = 0; i < v.size(); ++i) n.slot.setValue ((int) i, v[i]); }
    auto in = bandNoise (sr, (int) sr * 2, 0.5f, 9, 23500.0); auto out = in; n.run (out); float worst = 0; const int l2 = n.slot.getLatencySamples();
    for (int c = 0; c < 2; ++c) for (int i = 9600; i + l2 < out.getNumSamples(); ++i) worst = juce::jmax (worst, std::abs (out.getSample (c, i + l2) - in.getSample (c, i)));
    const float preDb = db (pre) - db (pk);
    report (preDb < -60.0f && db (worst) < -100.0f, juce::String (name) + " spectral artifacts", "pre-echo " + juce::String (preDb, 1) + " dB below the peak (limit -60); identity at neutral " + juce::String (db (worst), 1) + " dBFS (limit -100); latency " + juce::String (lat) + " samples");
}

// ---- Tempo lock (17 ECTO ECHO): synced to 1/8, an impulse every 2 s for 10 minutes while the host tempo steps between 70 and 174 BPM
// every 30 s; each echo must land within 1 sample of the division's exact length at the tempo of the moment
static bool gTempoInformational = false;   // an extra echo engine whose first voice moves its repeats by design (tape wow, a frequency shifter): reported (DECISIONS.md 86)
static void echoTempoLock (const char* name, std::function<std::unique_ptr<PhantomEffect>()> make, int tempoMode)
{
    const double sr = 48000.0; Bench b (make()); b.setDefaults(); auto& ps = b.slot.params();
    auto set = [&] (const char* idp, float v) { for (size_t i = 0; i < ps.size(); ++i) if (ps[i].id == idp) b.slot.setValue ((int) i, v); };
    set ("sync", 1); set ("div", 6); set ("fb", 0); set ("nodes", 1); set ("mode", (float) tempoMode); set ("char", 0); set ("ping", 0); set ("lowcut", 20); set ("highcut", 20000); set ("duck", 0); set ("sat", 0);
    const int blk = b.block, period = (int) sr * 2, seconds = 600; juce::AudioBuffer<float> buf (2, blk);
    std::vector<float> rec ((size_t) period); double worst = 0; int echoes = 0; long long t = 0;
    for (int k = 0; k < seconds / 2; ++k)
    {
        b.tempo.bpm = 70.0 + ((k / 15) * 37 % 105);   // a new tempo every 30 s
        const double expect = 0.5 * 60.0 / b.tempo.bpm * sr;
        for (int o = 0; o < period; o += blk) { buf.clear(); if (o == 0) { buf.setSample (0, 0, 1.0f); buf.setSample (1, 0, 1.0f); } b.run (buf); for (int i = 0; i < blk; ++i) rec[(size_t) (o + i)] = buf.getSample (0, i); }
        t += period;
        if (k % 15 == 0) continue;   // the first impulse after a tempo change is not timed (the earlier tempo's echo may still be in flight)
        int best = 0; float bv = 0; for (int i = 1; i < period - 1; ++i) if (std::abs (rec[(size_t) i]) > bv) { bv = std::abs (rec[(size_t) i]); best = i; }
        // sub-sample peak (parabolic) so a fractional delay is measured as such
        const float a = std::abs (rec[(size_t) best - 1]), c0 = std::abs (rec[(size_t) best]), d = std::abs (rec[(size_t) best + 1]);
        const double frac = (a - 2 * c0 + d) != 0.0f ? 0.5 * (a - d) / (a - 2 * c0 + d) : 0.0;
        if (std::getenv ("PHX_TDBG") && k % 15 == 1) std::printf ("bpm %.0f err %+.3f\n", b.tempo.bpm, best + frac - expect);
        worst = juce::jmax (worst, std::abs (best + frac - expect)); ++echoes;
    }
    report (worst <= 1.0 || gTempoInformational, juce::String (name) + " tempo lock (" + (gTempoInformational ? "informational, " : "") + (tempoMode == 0 ? "TAPE" : "DIGITAL") + ")", juce::String (echoes) + " echoes over 10 min at 70-174 BPM, worst " + juce::String (worst, 3) + " samples from the exact 1/8 (limit 1)");
}

static bool gTailInformational = false;   // an extra echo engine whose first voice moves its repeats by design (tape wow, a frequency shifter): reported (DECISIONS.md 86)   // the extra engines' tails are reported, not judged (DECISIONS.md 85)
static void checkTable (const char* name, std::function<std::unique_ptr<PhantomEffect>()> make)
{
    const double sr = 48000.0;
    // ---- Null test: neutral settings null against dry below -100 dBFS (delay-compensated by the reported latency)
    { Bench b (make()); b.setDefaults(); { auto& ps = b.slot.params(); std::vector<float> v (ps.size()); for (size_t i = 0; i < ps.size(); ++i) v[i] = ps[i].def; b.slot.fx().neutralise (v); for (size_t i = 0; i < v.size(); ++i) b.slot.setValue ((int) i, v[i]); if (b.slot.fx().neutralAtMixZero()) b.slot.setMix (0.0f); b.slot.applyPolicy(); }
      auto in = bandNoise (sr, (int) sr * 2, 0.5f * b.slot.fx().nullTestGain()); auto out = in; b.run (out);   // audio-band noise (see bandNoise)
      const int lat = b.slot.getLatencySamples(); float worst = 0; for (int c = 0; c < 2; ++c) for (int i = 4800; i + lat < out.getNumSamples(); ++i) worst = juce::jmax (worst, std::abs (out.getSample (c, i + lat) - in.getSample (c, i)));
      const bool gen = b.slot.fx().isGenerator(), none = ! b.slot.fx().hasNullSetting(); report (gen || none || db (worst) < -100.0f, juce::String (name) + " null test", gen ? "n/a (generator)" : b.slot.fx().wetOnly() ? "(wet-only effect at MIX 0 %) residual " + juce::String (db (worst), 1) + " dBFS (limit -100)" : none ? "n/a (always shapes the sound; measured residual at its mildest setting " + juce::String (db (worst), 1) + " dBFS)" : "residual " + juce::String (db (worst), 1) + " dBFS (limit -100)"); }
    // ---- Latency honesty: reported == measured impulse delay to the sample, for every latency-changing choice
    { Bench spec0 (make()); const auto specs0 = spec0.slot.params(); std::vector<int> choiceIdx; for (size_t i = 0; i < specs0.size(); ++i) if (specs0[i].type == ParamSpec::Type::Choice) choiceIdx.push_back ((int) i);
      bool ok = true; juce::String nums;
      auto neutral = [] (Bench& b) { auto& ps = b.slot.params(); std::vector<float> v (ps.size()); for (size_t i = 0; i < ps.size(); ++i) v[i] = ps[i].def; b.slot.fx().neutralise (v); for (size_t i = 0; i < v.size(); ++i) b.slot.setValue ((int) i, v[i]); if (b.slot.fx().neutralAtMixZero()) b.slot.setMix (0.0f); };
      // white noise through the module at its neutral setting; the delay is the lag of the input / output cross-correlation peak
      // (right for linear-phase oversampling, saturators at drive 0 and filters at their mildest setting alike)
      auto measure = [&] (Bench& b) { b.slot.applyPolicy(); auto in = noise (24576, 0.5f, 77); auto out = in; b.run (out);
          int best = 0; double bestC = -1e30; for (int lag = 0; lag < 7000; ++lag) { double c = 0; for (int i = 3000; i < 14000; ++i) c += (double) out.getSample (0, i + lag) * in.getSample (0, i); if (c > bestC) { bestC = c; best = lag; } }
          const int measured = best, reported = b.slot.getLatencySamples(); nums += juce::String (reported) + "/" + juce::String (measured) + " "; if (measured != reported) ok = false; };
      // a filter / EQ always shapes, so its cross-correlation peak moves with its own group delay (that is its sound, not latency).
      // For those the check is causality to the sample: with oversampling off, an impulse produces nothing before the reported latency and
      // starts within 4 samples of it (a ladder's first stage answers one sample later by design). The oversampler's own latency is
      // checked to the sample by every module that has a neutral setting.
      auto onset = [&] (Bench& b) { b.slot.setPolicy (OversamplingPolicy::Off); b.slot.applyPolicy(); juce::AudioBuffer<float> imp (2, 8192); imp.clear(); imp.setSample (0, 4096, 0.5f); imp.setSample (1, 4096, 0.5f); b.run (imp);
          int first = -1; for (int i = 0; i < imp.getNumSamples(); ++i) if (std::abs (imp.getSample (0, i)) > 1.0e-7f) { first = i - 4096; break; }
          const int reported = b.slot.getLatencySamples(); nums += juce::String (reported) + "/" + juce::String (first) + " "; if (first < reported || first > reported + 4) ok = false; };
      const bool shapes = ! spec0.slot.fx().hasNullSetting();
      // a choice that puts a filter in the path (CRACKLE's speaker boxes, for example) is checked like a filter: correlation first, onset if the
      // filter's group delay moves the correlation peak (marked "f")
      auto either = [&] (std::function<void (Bench&)> setup) { { Bench b (make()); setup (b); const bool okBefore = ok; const auto before = nums; ok = true; measure (b); if (ok) { ok = okBefore; return; } ok = okBefore; nums = before + "f"; }
          Bench b2 (make()); setup (b2); onset (b2); };
      if (shapes) { Bench b (make()); b.setDefaults(); neutral (b); onset (b); } else either ([&] (Bench& b) { b.setDefaults(); neutral (b); });
      for (int ci : choiceIdx) for (size_t v = 0; v < specs0[(size_t) ci].choices.size(); ++v)
      { auto setup = [&, ci, v] (Bench& b) { b.setDefaults(); neutral (b); b.slot.setValue (ci, (float) v); };
        if (shapes) { Bench b (make()); setup (b); onset (b); } else either (setup); }
      if (shapes) nums = "(filter: impulse onset with oversampling off, reported/first) " + nums;
      report (ok && ! choiceIdx.empty() == (std::count_if (specs0.begin(), specs0.end(), [] (const ParamSpec& q) { return q.type == ParamSpec::Type::Choice; }) > 0), juce::String (name) + " latency honesty", "reported/measured samples: " + nums.trim()); }
    // ---- Gain staging: defaults change RMS by <= 1 dB (except dynamics / limiter)
    // (pink noise, 3 s, measured over the last 2 s: a music-like spectrum, and echoes / reverbs have arrived; DECISIONS.md 24)
    { Bench b (make()); b.setDefaults(); auto in = b.slot.fx().wetOnly() ? pinkNoise ((int) sr * 3, 0.25f, 3) : noise ((int) sr * 3, 0.25f, 3); auto out = in; b.run (out);
      auto part = [&] (const juce::AudioBuffer<float>& x) { juce::AudioBuffer<float> t (2, (int) sr * 2); for (int c = 0; c < 2; ++c) t.copyFrom (c, 0, x, c, (int) sr, (int) sr * 2); return rmsOf (t); };
      const float d = db (part (out)) - db (part (in));
      const bool ex = b.slot.fx().changesLevelOnPurpose(); report (ex || std::abs (d) <= 1.0f, juce::String (name) + " gain staging", (ex ? "n/a (changing level is its job; informational) " : "") + juce::String ("RMS change ") + juce::String (d, 2) + " dB (limit 1)"); }
    // ---- Stereo integrity: mono-compatible in stays mono-compatible (L == R to -100 dB at defaults), and a stereo signal's mono sum is not cancelled
    { Bench b (make()); b.setDefaults(); auto in = noise (sr, 0.5f, 5); for (int i = 0; i < in.getNumSamples(); ++i) in.setSample (1, i, in.getSample (0, i)); auto out = in; b.run (out);
      float worst = 0; for (int i = 4800; i < out.getNumSamples(); ++i) worst = juce::jmax (worst, std::abs (out.getSample (0, i) - out.getSample (1, i)));
      Bench b2 (make()); b2.setDefaults(); auto st = sine (sr, (int) sr, 440.0f, 0.5f, true); const float sumIn = [&] { double s = 0; for (int i = 0; i < st.getNumSamples(); ++i) { const float m = 0.5f * (st.getSample (0, i) + st.getSample (1, i)); s += m * m; } return std::sqrt ((float) (s / st.getNumSamples())); }();
      auto st2 = st; b2.run (st2); const float sumOut = [&] { double s = 0; for (int i = 4800; i < st2.getNumSamples(); ++i) { const float m = 0.5f * (st2.getSample (0, i) + st2.getSample (1, i)); s += m * m; } return std::sqrt ((float) (s / (st2.getNumSamples() - 4800))); }();
      // a level effect (compressor, limiter) changes the level on purpose: the mono-sum change is measured against its own overall change
      const float levelDb = b2.slot.fx().changesLevelOnPurpose() ? [&] { auto r = st2; return db (rmsOf (r)) - db (rmsOf (st)); }() : 0.0f;
      const float sumDb = db (sumOut) - db (sumIn) - levelDb;
      if (b.slot.fx().widensOnPurpose())
      {   // a reverb / chorus makes stereo from mono by design: at WIDTH (or SPREAD) 0 a mono source must come out exactly mono; at the
          // defaults the mono sum of a mono source must never cancel (mid energy at least the side energy, i.e. the sum loses <= 3 dB)
          Bench b3 (make()); b3.setDefaults(); auto& ps = b3.slot.params(); bool hasW = false;
          for (size_t i = 0; i < ps.size(); ++i) if (ps[i].id == "width" || ps[i].id == "spread") { b3.slot.setValue ((int) i, 0.0f); hasW = true; }
          auto in3 = in; b3.run (in3); float w0 = 0; for (int i = 4800; i < in3.getNumSamples(); ++i) w0 = juce::jmax (w0, std::abs (in3.getSample (0, i) - in3.getSample (1, i)));
          double mid = 0, side = 0; for (int i = 4800; i < out.getNumSamples(); ++i) { const double m = 0.5 * (out.getSample (0, i) + out.getSample (1, i)), sd = 0.5 * (out.getSample (0, i) - out.getSample (1, i)); mid += m * m; side += sd * sd; }
          const float monoLoss = (float) (10.0 * std::log10 ((mid + 1e-20) / (0.5 * (mid + side) + 1e-20)));
          // on a stereo source: the mono sum's share of the output against its share of the input (cancellation, not level)
          auto share = [] (const juce::AudioBuffer<float>& x, int from) { double m = 0, a = 0; for (int i = from; i < x.getNumSamples(); ++i) { const double l = x.getSample (0, i), r = x.getSample (1, i); m += 0.25 * (l + r) * (l + r); a += 0.5 * (l * l + r * r); } return 10.0 * std::log10 ((m + 1e-20) / (a + 1e-20)); };
          const float shareDb = (float) (share (st2, 4800) - share (st, 4800));
          report ((! hasW || db (w0) < -100.0f) && monoLoss > -3.0f && shareDb > -3.0f, juce::String (name) + " stereo integrity",
                  (hasW ? "L-R on mono in at width 0: " + juce::String (db (w0), 1) + " dBFS; " : juce::String ("(no width control) ")) + "mono sum of a mono source at defaults " + juce::String (monoLoss, 2) + " dB vs L/R (limit -3); mono-sum share on a stereo source " + juce::String (shareDb, 2) + " dB vs the input's (limit -3)");
      }
      else
      report (db (worst) < -100.0f && sumDb > -3.0f, juce::String (name) + " stereo integrity", "L-R on mono in " + juce::String (db (worst), 1) + " dBFS; mono-sum change on stereo in " + juce::String (sumDb, 2) + " dB"); }
    // ---- Bypass click: toggling bypass mid-signal makes no transient above the signal's own peak
    { Bench b (make()); b.setDefaults(); auto in = sine (sr, (int) sr * 2, 220.0f, 0.7f); auto out = in; const float pin = peakOf (in);
      Bench b0 (make()); b0.setDefaults(); auto on = in; b0.run (on); const float pon = peakOf (on);   // the effect left on: its own peak
      b.run (out, [&] (int bi) { if (bi % 40 == 20) b.slot.setEnabled (false); if (bi % 40 == 0) b.slot.setEnabled (true); });
      const float pout = peakOf (out), allowed = juce::jmax (pin, pon) * 1.001f; report (pout <= allowed, juce::String (name) + " bypass click", "in peak " + juce::String (db (pin), 2) + ", effect-on peak " + juce::String (db (pon), 2) + ", toggled peak " + juce::String (db (pout), 2) + " dBFS"); }
    // ---- Zipper noise: every continuous control swept fast (64 steps over 200 ms, repeatedly); energy far from the 1 kHz test tone (> 6 kHz) stays below -90 dBFS
    { Bench specB (make()); const auto specs = specB.slot.params(); juce::String worstName, floorNotes; float worstDb = -200.0f; int tested = 0;
      for (size_t pi = 0; pi < specs.size(); ++pi)
      {
          if (! specs[pi].isContinuous()) continue;
          const float hi = zipperOf (make, (int) pi, {});
          // an engine whose own output already has non-harmonic content (a granular engine, a pitch shifter, a reverb's modulation images)
          // shows it with the control held still too: the sweep fails only if it adds more than 3 dB to that floor
          const bool event = specB.slot.fx().zipperEventControl (specs[pi].id);
          const float excess = hi < -90.0f ? hi : (hi <= zipperRest + 3.0f || event ? -200.0f : hi);
          if (event && hi >= -90.0f) floorNotes += "[grain-plan control] ";
          if (hi >= -90.0f) floorNotes += juce::String (specs[pi].name) + " " + juce::String (hi, 1) + " (still " + juce::String (zipperRest, 1) + ") ";
          if (excess > worstDb) { worstDb = excess; worstName = specs[pi].name; } ++tested;
      }
      report (tested > 0 && worstDb < -90.0f, juce::String (name) + " zipper noise", juce::String (tested) + " controls swept; worst " + worstName + ": " + juce::String (worstDb, 1) + " dBFS above 6 kHz (limit -90)" + (floorNotes.isEmpty() ? juce::String() : "; at the engine's own floor (sweep vs held still): " + floorNotes.trim())); }
    // ---- Aliasing (nonlinear effects): sine at full drive with oversampling on (the policy's top normal setting, 8x, so a module gets the
    // factor it asks for); every component in the audio band that is not a harmonic stays below -80 dBFS
    { Bench b (make()); const bool nl = b.slot.fx().isNonlinear(); juce::String at; float mod = -200.0f; const float worst = worstAlias (make, OversamplingPolicy::Render, {}, at, &mod);
      report (! nl || worst < -80.0f, juce::String (name) + " aliasing", (nl ? "" : "(linear, informational) ") + juce::String ("worst alias ") + juce::String (worst, 1) + " dBFS at " + at + " (oversampling " + juce::String (b.slot.fx().oversamplingFactor()) + "x)"
             + (mod > -140.0f ? "; engine's own modulation products up to " + juce::String (mod, 1) + " dBFS (the same at half the factor, so not aliasing)" : juce::String())); }
    // ---- Denormals / NaN: 10 minutes of silence then signal; no NaN / Inf, no CPU spike (silent blocks no slower than 3x the signal blocks)
    { Bench b (make()); b.setDefaults(); const int blocks = (int) (600.0 * sr / b.block); juce::AudioBuffer<float> buf (2, b.block);
      auto timeBlocks = [&] (int count, bool silent, unsigned seed) { std::mt19937 rng (seed); std::uniform_real_distribution<float> d (-0.5f, 0.5f); double worst = 0, total = 0; bool fin = true;
          for (int k = 0; k < count; ++k) { if (silent) buf.clear(); else for (int c = 0; c < 2; ++c) for (int i = 0; i < b.block; ++i) buf.setSample (c, i, d (rng));
              const auto t0 = std::chrono::steady_clock::now(); b.run (buf); const double us = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count(); total += us; if (k > 10) worst = juce::jmax (worst, us); if (! finite (buf)) fin = false; }
          return std::make_tuple (total / count, worst, fin); };
      // a short signal first, then the long silence (the decayed-state case), then signal again
      auto [sigMean0, sigWorst0, fin0] = timeBlocks (2000, false, 11); auto [silMean, silWorst, fin1] = timeBlocks (blocks, true, 12); auto [sigMean, sigWorst, fin2] = timeBlocks (2000, false, 13);
      juce::ignoreUnused (sigWorst0, sigWorst);
      report (fin0 && fin1 && fin2 && silMean <= 3.0 * juce::jmax (sigMean, 0.5), juce::String (name) + " denormals / NaN", "silence " + juce::String (silMean, 2) + " us/block mean (worst " + juce::String (silWorst, 1) + "), signal " + juce::String (sigMean, 2) + " us/block; finite " + juce::String ((fin0 && fin1 && fin2) ? "yes" : "NO")); }
    // ---- Tails / spectral / true-peak checks apply to reverbs, STFT effects and the limiter (Phases 4-6)
    { Bench b (make()); const juce::String k (b.slot.fx().key());
      if (k == "space" || k == "shimmer" || k == "halls" || k == "portal") tailCheck (name, make, k == "portal" || gTailInformational);
      else if (k == "spectra") spectralCheck (name, make);
      else if (k != "lim") report (true, juce::String (name) + " reverb tail / spectral / true-peak", "n/a for this effect");
      if (k == "echo") { echoTempoLock (name, make, 0); echoTempoLock (name, make, 3); } }
}

#include "RhythmTests.inc"
#include "MasterTests.inc"
#include "SpatialTests.inc"
#include "LooksTests.inc"
#include "EngineTests.inc"

// ---------------------------------------------------------------- rack checks
static void cpuCheck()
{
    // every one of the 24 units switched on at its defaults (the brief's case), plus the all-off rack for reference
    double offPct = 0.0;
    { PhantomProcessor p; p.prepareToPlay (48000.0, 256); auto buf = noise (256, 0.5f); juce::MidiBuffer m; const auto t0 = std::chrono::steady_clock::now(); for (int i = 0; i < 1000; ++i) p.processBlock (buf, m);
      offPct = 100.0 * std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count() / (1000 * 256.0 / 48000.0); }
    { PhantomProcessor p; for (auto* q : p.getParameters()) if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (q)) for (int u = 0; u < kNumUnits; ++u) if (rp->getParameterID() == juce::String ("fx_") + unitDef (u).key + "_on") q->setValueNotifyingHost (1.0f);
      p.prepareToPlay (48000.0, 256); auto buf = noise (256, 0.5f); juce::MidiBuffer m; const int blocks = 4000;
      const auto t0 = std::chrono::steady_clock::now(); for (int i = 0; i < blocks; ++i) p.processBlock (buf, m);
      const double s = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count(), audio = blocks * 256.0 / 48000.0, pct = 100.0 * s / audio;
      int onCount = 0; for (int u = 0; u < p.rack.numSlots(); ++u) if (p.rack.slot (u).isEnabled()) ++onCount;
      report (pct < 40.0, "rack CPU budget", juce::String (pct, 2) + " % of one core with " + juce::String (onCount) + " of " + juce::String (p.rack.numSlots()) + " units on (limit 40, Brandon 2026-11-09, DECISIONS.md 28); all off: " + juce::String (offPct, 2) + " %"); }
}

// ---------------------------------------------------------------- SPIRIT EQ: its bands measured (the check table sees an EQ with every band off)
// SPIRIT EQ's linear-phase modes: the latency is 1.5 x the tap count and reported; a flat EQ is a pure delay; the impulse response
// is symmetric (that is what linear phase means); the curve matches Zero Latency; a Side band leaves a mono signal alone
static void eqLinearChecks()
{
    const double sr = 48000.0; using Set = std::vector<std::pair<std::string, float>>;
    auto impulse = [&] (Set set, int len, int& latency, bool side = false) {
        Bench b (makeEffect (uEq)); b.setDefaults(); auto& ps = b.slot.params();
        for (auto& [k, v] : set) for (size_t i = 0; i < ps.size(); ++i) if (ps[i].id == k) b.slot.setValue ((int) i, v);
        b.slot.applyPolicy(); latency = b.slot.getLatencySamples();
        { juce::AudioBuffer<float> quiet (2, 9600); quiet.clear(); b.run (quiet); }   // (a band fades in over 20 ms when switched on: let that pass first)
        juce::AudioBuffer<float> buf (2, len); buf.clear(); buf.setSample (0, 0, 1.0f); buf.setSample (1, 0, side ? -1.0f : 1.0f); b.run (buf); return buf; };
    auto magDb = [&] (const juce::AudioBuffer<float>& ir, double hz) { double re = 0, im = 0; const float* d = ir.getReadPointer (0); for (int i = 0; i < ir.getNumSamples(); ++i) { const double ph = juce::MathConstants<double>::twoPi * hz * i / sr; re += d[i] * std::cos (ph); im -= d[i] * std::sin (ph); } return 20.0 * std::log10 (std::sqrt (re * re + im * im) + 1.0e-12); };
    const Set bands { { "1_on", 1.0f }, { "1_type", 0.0f }, { "1_freq", 1000.0f }, { "1_gain", 12.0f }, { "1_q", 1.0f }, { "2_on", 1.0f }, { "2_type", 1.0f }, { "2_freq", 200.0f }, { "2_gain", -6.0f }, { "3_on", 1.0f }, { "3_type", 4.0f }, { "3_freq", 9000.0f }, { "3_slope", 3.0f } };
    int zlat = 0; const auto zl = impulse (bands, 32768, zlat);
    juce::StringArray lines; bool ok = zlat == 0;
    for (int mode = 1; mode <= 3; ++mode)
    {
        const int taps = 512 << mode, want = taps * 3 / 2; int lat = 0;
        // flat: a pure delay of exactly the reported latency
        const auto flat = impulse ({ { "mode", (float) mode } }, 16384, lat); float peak = flat.getSample (0, lat), rest = 0; for (int i = 0; i < flat.getNumSamples(); ++i) if (i != lat) rest = juce::jmax (rest, std::abs (flat.getSample (0, i)));
        // three bands: symmetric about the latency, and the same curve as Zero Latency
        Set sb = bands; sb.push_back ({ "mode", (float) mode }); int lat2 = 0; const auto ir = impulse (sb, 32768, lat2);
        float asym = 0; for (int i = 1; i < taps / 2; ++i) asym = juce::jmax (asym, std::abs (ir.getSample (0, lat2 + i) - ir.getSample (0, lat2 - i)));
        double worst = 0; for (double hz : { 400.0, 1000.0, 2500.0, 6000.0 }) worst = juce::jmax (worst, std::abs (magDb (ir, hz) - magDb (zl, hz)));
        double worstLow = 0; for (double hz : { 60.0, 120.0, 200.0 }) worstLow = juce::jmax (worstLow, std::abs (magDb (ir, hz) - magDb (zl, hz)));
        if (std::getenv ("PHX_EQDBG")) for (double hz : { 60.0, 120.0, 200.0, 400.0, 1000.0, 2500.0, 6000.0, 9000.0, 12000.0 }) std::printf ("  mode %d  %6.0f Hz  linear %7.2f  zero-latency %7.2f\n", mode, hz, magDb (ir, hz), magDb (zl, hz));
        const double lowLimit = mode == 3 ? 0.5 : mode == 2 ? 1.5 : 3.5;   // (fewer taps resolve the low end less finely: that is what Low / Medium / High mean)
        const bool good = lat == want && lat2 == want && std::abs (peak - 1.0f) < 1.0e-4f && rest < 1.0e-4f && asym < 1.0e-5f && worst < 0.3 && worstLow < lowLimit; ok = ok && good;
        lines.add (juce::String (taps) + " taps: latency " + juce::String (lat) + " (reported, " + juce::String (want) + " wanted), flat = a delay (peak " + juce::String (peak, 5) + ", everything else under " + juce::String (rest, 6) + "), symmetric to " + juce::String (asym, 7) + ", curve within " + juce::String (worst, 2) + " dB of Zero Latency from 400 Hz up and " + juce::String (worstLow, 2) + " dB at 60-200 Hz");
    }
    // a Side band: a mono signal passes untouched (delayed), a side-only signal gets the band's 12 dB at its centre
    int lat = 0; const Set sideBand { { "mode", 2.0f }, { "1_on", 1.0f }, { "1_freq", 1000.0f }, { "1_gain", 12.0f }, { "1_stereo", 4.0f } };
    const auto mono = impulse (sideBand, 16384, lat); float monoErr = std::abs (mono.getSample (0, lat) - 1.0f); for (int i = 0; i < mono.getNumSamples(); ++i) if (i != lat) monoErr = juce::jmax (monoErr, std::abs (mono.getSample (0, i)));
    const auto sd = impulse (sideBand, 16384, lat, true); const double sideDb = magDb (sd, 1000.0);
    ok = ok && monoErr < 1.0e-4f && std::abs (sideDb - 12.0) < 0.3;
    report (ok, "SPIRIT EQ linear phase", lines.joinIntoString ("; ") + "; a Side band leaves mono alone (error " + juce::String (monoErr, 6) + ") and lifts the side by " + juce::String (sideDb, 2) + " dB (12 wanted)");
    // the spectrum tap: nothing is copied until an editor asks; then exactly the samples played
    { Bench b (makeEffect (uEq)); b.setDefaults(); auto* tap = b.slot.fx().analyserTap(); auto buf = noise (4096, 0.3f, 3); auto ref = buf; b.run (buf); const int before = tap != nullptr ? tap->fifo.getNumReady() : -1;
      if (tap != nullptr) tap->wanted = true; auto buf2 = noise (4096, 0.3f, 3); b.run (buf2); std::vector<float> got (8192); const int n = tap != nullptr ? tap->pull (got.data(), 8192) : 0;
      float err = 0; for (int i = 0; i < juce::jmin (n, 4096); ++i) err = juce::jmax (err, std::abs (got[(size_t) i] - 0.5f * (buf2.getSample (0, i) + buf2.getSample (1, i))));
      report (tap != nullptr && before == 0 && n == 4096 && err < 1.0e-6f, "SPIRIT EQ spectrum tap", "idle until an editor asks (" + juce::String (before) + " samples queued), then " + juce::String (n) + " of 4096 samples, equal to the output to " + juce::String (err, 7)); }
}

static void eqChecks()
{
    eqLinearChecks();
    const double sr = 48000.0;
    auto gainAt = [&] (std::vector<std::pair<std::string, float>> set, float hz, float amp = 0.25f) {
        Bench b (makeEffect (uEq)); b.setDefaults(); auto& ps = b.slot.params();
        for (auto& [k, v] : set) for (size_t i = 0; i < ps.size(); ++i) if (ps[i].id == k) b.slot.setValue ((int) i, v);
        b.slot.applyPolicy(); auto buf = sine (sr, (int) sr, hz, amp); auto in = buf; b.run (buf);
        double si = 0, so = 0; for (int i = 24000; i < 48000; ++i) { si += in.getSample (0, i) * in.getSample (0, i); so += buf.getSample (0, i) * buf.getSample (0, i); }
        return (float) (10.0 * std::log10 (so / si)); };
    const float bell = gainAt ({ { "1_on", 1 }, { "1_type", 0 }, { "1_freq", 1000 }, { "1_gain", 6 }, { "1_q", 1 } }, 1000), bellOff = gainAt ({ { "1_on", 1 }, { "1_type", 0 }, { "1_freq", 1000 }, { "1_gain", 6 }, { "1_q", 1 } }, 100);
    report (std::abs (bell - 6.0f) < 0.2f && std::abs (bellOff) < 0.3f, "SPIRIT EQ bell", "+6 dB bell at 1 kHz measures " + juce::String (bell, 2) + " dB there, " + juce::String (bellOff, 2) + " dB at 100 Hz");
    const float cut = gainAt ({ { "1_on", 1 }, { "1_type", 3 }, { "1_slope", 3 }, { "1_freq", 1000 } }, 500), cutPass = gainAt ({ { "1_on", 1 }, { "1_type", 3 }, { "1_slope", 3 }, { "1_freq", 1000 } }, 8000);
    const float cut96 = gainAt ({ { "1_on", 1 }, { "1_type", 4 }, { "1_slope", 7 }, { "1_freq", 1000 } }, 2000, 0.5f);
    report (std::abs (cut + 24.1f) < 1.0f && std::abs (cutPass) < 0.1f && cut96 < -90.0f, "SPIRIT EQ cuts", "24 dB/oct low cut at 1 kHz: " + juce::String (cut, 1) + " dB an octave below (theory -24.1), " + juce::String (cutPass, 2) + " dB at 8 kHz; 96 dB/oct high cut an octave above: " + juce::String (cut96, 1) + " dB");
    const float shelf = gainAt ({ { "1_on", 1 }, { "1_type", 2 }, { "1_freq", 3000 }, { "1_gain", 6 } }, 15000), shelfLo = gainAt ({ { "1_on", 1 }, { "1_type", 2 }, { "1_freq", 3000 }, { "1_gain", 6 } }, 200);
    report (std::abs (shelf - 6.0f) < 0.5f && std::abs (shelfLo) < 0.3f, "SPIRIT EQ shelf", "+6 dB high shelf at 3 kHz: " + juce::String (shelf, 2) + " dB at 15 kHz, " + juce::String (shelfLo, 2) + " dB at 200 Hz");
    const float dynLoud = gainAt ({ { "1_on", 1 }, { "1_type", 0 }, { "1_freq", 1000 }, { "1_gain", 0 }, { "1_dyn", 1 }, { "1_thr", -40 }, { "1_range", -6 }, { "1_ratio", 4 } }, 1000, 0.5f);
    const float dynQuiet = gainAt ({ { "1_on", 1 }, { "1_type", 0 }, { "1_freq", 1000 }, { "1_gain", 0 }, { "1_dyn", 1 }, { "1_thr", -40 }, { "1_range", -6 }, { "1_ratio", 4 } }, 1000, 0.002f);
    report (std::abs (dynLoud + 6.0f) < 0.7f && std::abs (dynQuiet) < 0.3f, "SPIRIT EQ dynamic band", "loud 1 kHz tone cut by " + juce::String (dynLoud, 2) + " dB (range -6), quiet one by " + juce::String (dynQuiet, 2) + " dB");
    { Bench b (makeEffect (uEq)); b.setDefaults(); auto& ps = b.slot.params(); for (size_t i = 0; i < ps.size(); ++i) { if (ps[i].id == "1_on") b.slot.setValue ((int) i, 1); if (ps[i].id == "1_stereo") b.slot.setValue ((int) i, 4); if (ps[i].id == "1_gain") b.slot.setValue ((int) i, -12); }
      b.slot.applyPolicy(); auto in = sine (sr, (int) sr, 1000.0f, 0.3f); auto out = in; b.run (out); float w = 0; for (int i = 4800; i < 48000; ++i) w = juce::jmax (w, std::abs (out.getSample (0, i) - in.getSample (0, i)));
      report (db (w) < -100.0f, "SPIRIT EQ side band", "a -12 dB side-only bell on a mono signal changes it by " + juce::String (db (w), 1) + " dBFS"); }
    float zw = -200.0f; for (auto* k : { "1_freq", "1_gain", "1_q" }) { int pi = -1; auto specs = unitParams (uEq); for (size_t i = 0; i < specs.size(); ++i) if (specs[i].id == k) pi = (int) i;
        zw = juce::jmax (zw, zipperOf ([] { return makeEffect (uEq); }, pi, { { "1_on", 1.0f }, { "1_gain", 9.0f }, { "1_freq", 1000.0f } })); }
    report (zw < -90.0f, "SPIRIT EQ zipper (band on)", "band 1 freq / gain / Q swept on a live +9 dB bell: worst " + juce::String (zw, 1) + " dBFS above 6 kHz");
}

static void rackChecks()
{
    // state round trip is bit-exact
    // (0.1.13: the ENGINE menus draw from their own random stream, so every other control keeps the values this check has always used)
    { PhantomProcessor a; a.prepareToPlay (48000.0, 256); std::mt19937 rng (99), rngEngine (199); std::uniform_real_distribution<float> d (0.0f, 1.0f);
      for (auto* p : a.getParameters()) if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
      { auto& g = rp->getParameterID().endsWith ("_engine") ? rngEngine : rng; rp->setValueNotifyingHost (p->isDiscrete() || p->isBoolean() ? std::round (d (g)) : d (g)); }
      juce::MemoryBlock mb; a.getStateInformation (mb);
      PhantomProcessor b; b.prepareToPlay (48000.0, 256); b.setStateInformation (mb.getData(), (int) mb.getSize());
      bool same = true; int n = 0; juce::String diff; for (int i = 0; i < a.getParameters().size(); ++i) { const float x = a.getParameters()[i]->getValue(), y = b.getParameters()[i]->getValue(); if (std::abs (x - y) > 2.0e-7f) { /* a skewed range may land one float ulp off after the text round trip; the saved bytes below must still be identical */ same = false; diff += a.getParameters()[i]->getName (32) + " " + juce::String (x, 9) + "/" + juce::String (y, 9) + "; "; } ++n; }
      juce::MemoryBlock mb2; b.getStateInformation (mb2); const bool bytesSame = mb == mb2;
      report (same && bytesSame, "rack state round trip", juce::String (n) + " parameters compared bit-exact: " + (same ? "yes" : "NO " + diff) + "; saved state bytes identical after reload: " + (bytesSame ? "yes" : "NO")); }
    // sample-rate / buffer-size changes: no crash, finite output, latency reported
    { PhantomProcessor p; bool ok = true; juce::String nums;
      for (double sr : { 44100.0, 48000.0, 96000.0 }) for (int bs : { 64, 256, 2048 }) { p.prepareToPlay (sr, bs); auto buf = noise (bs * 8, 0.5f); juce::MidiBuffer m;
          for (int i = 0; i < 8; ++i) { juce::AudioBuffer<float> part (buf.getArrayOfWritePointers(), 2, i * bs, bs); p.processBlock (part, m); } if (! finite (buf)) ok = false; nums += juce::String (sr, 0) + "/" + juce::String (bs) + " "; }
      report (ok, "rack sample-rate / buffer changes", "ran " + nums.trim() + "; output finite " + (ok ? "yes" : "NO")); }
    // fresh start: two instances in one process; changing one leaves the other at factory defaults; destroy + recreate = defaults
    { auto defaultsOf = [] (PhantomProcessor& p) { std::vector<float> v; for (auto* q : p.getParameters()) v.push_back (q->getValue()); return v; };
      auto isDefault = [] (PhantomProcessor& p) { for (auto* q : p.getParameters()) if (std::abs (q->getValue() - q->getDefaultValue()) > 1.0e-6f) return false; return true; };
      bool ok = true; { PhantomProcessor a, b; for (auto* q : a.getParameters()) q->setValueNotifyingHost (0.123f); if (! isDefault (b)) ok = false; if (isDefault (a)) ok = false; }
      { PhantomProcessor c; if (! isDefault (c)) ok = false; juce::ignoreUnused (defaultsOf); }
      report (ok, "rack fresh start", ok ? "second instance and a recreated instance are at factory defaults" : "an instance carried state over"); }
    // window size: opens at 1200 x 760 (never wider than 1240 or taller than 780), resizable
    { const bool sizeOk = PhantomProcessor::kDefaultWidth <= 1240 && PhantomProcessor::kDefaultHeight <= 780 && PhantomProcessor::kDefaultWidth >= 1100;
      juce::String nums = juce::String (PhantomProcessor::kDefaultWidth) + " x " + juce::String (PhantomProcessor::kDefaultHeight);
      bool editorOk = true;
      if (! juce::Desktop::getInstance().getDisplays().displays.isEmpty())
      { PhantomProcessor p; std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditor()); editorOk = e->getWidth() == PhantomProcessor::kDefaultWidth && e->getHeight() == PhantomProcessor::kDefaultHeight && e->isResizable(); nums += ", editor " + juce::String (e->getWidth()) + " x " + juce::String (e->getHeight()); }
      else nums += " (no display: editor not opened)";
      report (sizeOk && editorOk, "rack window size", nums); }
    // CPU budget (informational until the 24 effects are in): the whole rack at 48 kHz / 256 with 4x oversampling policy
    cpuCheck();

    // Phase 2: parallel lanes stay phase-aligned when one lane carries latency (PASS-style test latency on a placeholder is not available,
    // so the rack is checked with the limiter lane delay math: two lanes with different latency get compensating delays)
    // (the limiter, on by default and always last, is switched off here: this checks the lanes' alignment, not limiting a 3x sum)
    { PhantomProcessor p; p.setParamNormalised ("fx_lim_on", 0.0f); p.prepareToPlay (48000.0, 256); Topology t; t.lane[(size_t) uChorus] = 1; t.lane[(size_t) uEcho] = 2; p.setTopology (t, false);
      juce::MidiBuffer m; { juce::AudioBuffer<float> z (2, 256); for (int i = 0; i < 20; ++i) { z.clear(); p.processBlock (z, m); juce::MessageManager::getInstance()->runDispatchLoopUntil (2); } }   // the audio thread fades, the timer completes the swap
      auto in = noise (48000, 0.3f, 21); auto out = in; for (int i = 0; i < 48000 / 256; ++i) { juce::AudioBuffer<float> part (out.getArrayOfWritePointers(), 2, i * 256, 256); p.processBlock (part, m); }
      // with every placeholder a pass-through, main + lane A + lane B all carry the dry signal: the sum is exactly 3x the input (0 dB lanes), phase-aligned
      const int lat = p.totalLatency(); float worst = 0; for (int i = 4800; i + lat < 48000 - 256; ++i) worst = juce::jmax (worst, std::abs (out.getSample (0, i + lat) - 3.0f * in.getSample (0, i)));
      report (db (worst) < -100.0f, "rack lanes latency-aligned sum", "3 lanes of the same signal sum to exactly 3x: residual " + juce::String (db (worst), 1) + " dBFS; latency " + juce::String (lat)); }
    // topology change while playing: no dropout (the signal dips to the dry copy for ~20 ms, never to silence) and no click above the input peak
    { PhantomProcessor p; p.setParamNormalised ("fx_lim_on", 0.0f); p.prepareToPlay (48000.0, 256); auto in = sine (48000.0, 96000, 220.0f, 0.7f); auto out = in; juce::MidiBuffer m; float minRms = 1.0f;
      for (int i = 0; i < 96000 / 256; ++i) { if (i == 100) p.moveUnit (uEcho, 0); if (i == 200) p.moveUnit (uWreck, 3); juce::AudioBuffer<float> part (out.getArrayOfWritePointers(), 2, i * 256, 256); p.processBlock (part, m); juce::MessageManager::getInstance()->runDispatchLoopUntil (1); if (i * 256 > p.totalLatency() + 512) minRms = juce::jmin (minRms, part.getRMSLevel (0, 0, 256)); }   // after the rack's whole latency has passed (the Master units added ~700 samples to it)
      const bool topoApplied = p.getTopology().order[0] == uEcho && p.getTopology().order[3] == uWreck;
      report (peakOf (out) <= peakOf (in) * 1.001f && minRms > 0.3f && topoApplied, "rack topology change", "peak in " + juce::String (db (peakOf (in)), 2) + " / out " + juce::String (db (peakOf (out)), 2) + " dBFS; lowest block RMS during the change " + juce::String (db (minRms), 1) + " dBFS; new order applied: " + (topoApplied ? "yes" : "NO")); }
    // scenes: store two, recall with a morph, the parameters glide and land exactly; the limiter is still last
    { PhantomProcessor p; p.prepareToPlay (48000.0, 256); p.setParamNormalised ("fx_wreck_drive", 0.1f); p.storeScene (0); p.setParamNormalised ("fx_wreck_drive", 0.9f); p.storeScene (1);
      p.setParamNormalised ("scene_time", 0.0f); p.setParamNormalised ("scene_quant", 0.0f); p.recallScene (0); juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
      const float a = p.getParamNormalised ("fx_wreck_drive"); p.recallScene (1); juce::MessageManager::getInstance()->runDispatchLoopUntil (50); const float b = p.getParamNormalised ("fx_wreck_drive");
      const bool limLast = p.getTopology().order[kNumUnits - 1] == uLim;
      report (std::abs (a - 0.1f) < 1e-4f && std::abs (b - 0.9f) < 1e-4f && p.sceneFilled (0) && p.sceneFilled (1) && ! p.sceneFilled (2) && limLast, "rack scenes", "recall 1 -> " + juce::String (a, 3) + ", recall 2 -> " + juce::String (b, 3) + "; limiter last: " + (limLast ? "yes" : "NO")); }
    // presets: JSON round trip restores every parameter, the order, the lanes and the routes; unknown keys are ignored (backwards compatible)
    { PhantomProcessor a; a.prepareToPlay (48000.0, 256); std::mt19937 rng (5); std::uniform_real_distribution<float> d (0.0f, 1.0f);
      for (auto* q : a.getParameters()) if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (q)) if (rp->paramID.startsWith ("fx_") || rp->paramID.startsWith ("macro")) rp->setValueNotifyingHost (q->isDiscrete() || q->isBoolean() ? std::round (d (rng)) : d (rng));
      Topology t; std::swap (t.order[0], t.order[5]); t.lane[(size_t) uTube] = 2; a.setTopology (t, false); a.addRoute ({ 0, uWreck, 1, 0.3f, true }); a.setStepValue (0, 3, 0.75f);
      auto js = a.toJson ("Test", { "a", "b" }); auto* o = js.getDynamicObject(); o->setProperty ("some_future_key", 42);
      if (auto* ps = o->getProperty ("params").getDynamicObject()) ps->setProperty ("fx_future_unit_knob", 0.5);
      PhantomProcessor b; b.prepareToPlay (48000.0, 256); const bool ok = b.fromJson (js);
      int diff = 0; for (int i = 0; i < a.getParameters().size(); ++i) { auto* pa = dynamic_cast<juce::RangedAudioParameter*> (a.getParameters()[i]); if (pa->paramID == "in" || pa->paramID == "out" || pa->paramID == "bypass" || pa->paramID == "scene") continue; if (std::abs (pa->getValue() - b.getParameters()[i]->getValue()) > 2e-4f) ++diff; }
      const bool topoOk = b.getTopology().order[0] == t.order[0] && b.getTopology().lane[(size_t) uTube] == 2, routeOk = b.getRoutes().size() == 1 && b.getRoutes()[0].unit == uWreck, stepOk = std::abs (b.getStepValue (0, 3) - 0.75f) < 1e-6f;
      report (ok && diff == 0 && topoOk && routeOk && stepOk, "rack preset JSON round trip", juce::String (diff) + " parameter differences; order/lanes " + (topoOk ? "ok" : "NO") + "; routes " + (routeOk ? "ok" : "NO") + "; steps " + (stepOk ? "ok" : "NO") + "; unknown keys ignored: " + (ok ? "yes" : "NO")); }
    // modulation: an LFO routed to a knob moves the slot value while the host value stays put
    { PhantomProcessor p; p.prepareToPlay (48000.0, 256); p.addRoute ({ 0, uWreck, 1, 1.0f, true }); juce::MidiBuffer m; auto buf = noise (256, 0.1f);
      float lo = 1e9f, hi = -1e9f; for (int i = 0; i < 400; ++i) { p.processBlock (buf, m); const float v = p.rack.sourceValue[0]; lo = juce::jmin (lo, v); hi = juce::jmax (hi, v); }
      report (hi - lo > 1.0f, "rack modulation routing", "LFO 1 swept " + juce::String (lo, 2) + " .. " + juce::String (hi, 2) + " while routed to WRECKER drive"); }
    // every placeholder has its controls: the parameter count and that every id is unique
    { PhantomProcessor p; std::set<juce::String> ids; int dup = 0; for (auto* q : p.getParameters()) if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (q)) { if (! ids.insert (rp->paramID).second) ++dup; }
      report (dup == 0 && (int) ids.size() > 600, "rack parameter tree", juce::String ((int) ids.size()) + " host parameters, " + juce::String (dup) + " duplicate ids, " + juce::String (kNumUnits) + " units"); }
    // tempo lock: the shared host-locked LFO / step sequencer stay sample-locked over 10 minutes with tempo changes
    { Lfo lfo; lfo.prepare (48000.0); lfo.setShape (LfoShape::SawUp); lfo.setSync (true, 9); TempoInfo t; t.sampleRate = 48000.0; t.playing = true; const int block = 256; double worst = 0;
      for (int b = 0; b < (int) (600.0 * 48000.0 / block); ++b)
      { t.bpm = 90.0 + 60.0 * (0.5 + 0.5 * std::sin (b * 0.001)); lfo.beginBlock (t);
        const float v = lfo.next(); const double expectPhase = hostPhase (t.ppqPosition, 1.0), got = (v + 1.0) * 0.5; double err = std::abs (got - expectPhase); err = juce::jmin (err, 1.0 - err);
        worst = juce::jmax (worst, err * t.samplesPerBeat()); for (int i = 1; i < block; ++i) lfo.next(); t.ppqPosition += block / t.samplesPerBeat(); }
      report (worst <= 1.0, "rack tempo lock (shared LFO)", "worst phase error " + juce::String (worst, 3) + " samples over 10 min with a sweeping tempo (limit 1)"); }
}

// "shots <folder>": render the editor's pages to PNG (needs a display)
static int shots (const juce::File& dir)
{
    if (juce::Desktop::getInstance().getDisplays().displays.isEmpty()) { std::printf ("no display: no screenshots\n"); return 0; }
    dir.createDirectory();
    PhantomProcessor p; p.prepareToPlay (48000.0, 256);
    for (int u : { uFatten, uCrackle, uEq, uHalls }) p.setParamNormalised (PhantomProcessor::pid (u, "on"), 1.0f);
    p.storeScene (0); p.storeScene (3); p.addRoute ({ 0, uWreck, 1, 0.5f, true });
    std::unique_ptr<juce::AudioProcessorEditor> e (p.createEditor());
    e->setBounds (0, 0, PhantomProcessor::kDefaultWidth, PhantomProcessor::kDefaultHeight);   // rendered off-screen: no window manager needed
    auto snap = [&] (const juce::String& name) { juce::MessageManager::getInstance()->runDispatchLoopUntil (250); auto img = e->createComponentSnapshot (e->getLocalBounds(), false, 1.0f); juce::PNGImageFormat png; juce::FileOutputStream os (dir.getChildFile (name + ".png")); if (os.openedOk()) { os.setPosition (0); os.truncate(); png.writeImageToStream (img, os); std::printf ("shot %s\n", name.toRawUTF8()); } };
    snap ("01-rack-fatten");
    if (auto* strip = dynamic_cast<juce::Component*> (e.get())) juce::ignoreUnused (strip);
    for (int i = 0; i < 4; ++i)
    {   // the tab buttons are the first four TextButtons named RACK / LANES / MODS / PERFORM inside the panel
        std::function<juce::Button* (juce::Component*, const juce::String&)> find = [&] (juce::Component* c, const juce::String& n) -> juce::Button* { if (auto* b = dynamic_cast<juce::TextButton*> (c)) if (b->getButtonText() == n) return b; for (auto* k : c->getChildren()) if (auto* r = find (k, n)) return r; return nullptr; };
        const char* names[] { "RACK", "LANES", "MODS", "PERFORM" };
        if (auto* b = find (e.get(), names[i])) { b->triggerClick(); snap (juce::String ("0") + juce::String (i + 2) + "-" + juce::String (names[i]).toLowerCase()); }
    }
    // the Rhythm units' drawn editors (Phase 5)
    if (auto* pe = dynamic_cast<PhantomEditor*> (e.get()))
        for (int u : { uGate, uGrid, uHaunt, uWarp, uOtt, uLim }) { p.setParamNormalised (PhantomProcessor::pid (u, "on"), 1.0f); pe->showUnit (u); snap (juce::String ("1") + juce::String (u) + "-" + juce::String (unitDef (u).key)); }
        if (auto* pe = dynamic_cast<PhantomEditor*> (e.get()))
        {   // 0.1.13: an ENGINE menu set to DRAW brings its curve editor (WRECKER)
            const auto labels = engines::engineLabels (uWreck); int e = 0; for (size_t i = 0; i < labels.size(); ++i) if (labels[i] == "DRAW") e = (int) i;
            p.setParamNormalised (PhantomProcessor::pid (uWreck, "on"), 1.0f); p.setParamNormalised (PhantomProcessor::pid (uWreck, "engine"), (float) e / (float) (labels.size() - 1));
            pe->showUnit (uWreck); snap ("15-wreck-draw");
            {   // 0.1.18: a CURVES set with SHOW CURVE open (DRIVE 60 %)
                int c = 0; for (size_t i = 0; i < labels.size(); ++i) if (labels[i] == "CURVES · Acid") c = (int) i;
                p.setParamNormalised (PhantomProcessor::pid (uWreck, "engine"), (float) c / (float) (labels.size() - 1)); p.setParamNormalised (PhantomProcessor::pid (uWreck, "drive"), 0.6f);
                pe->showUnit (uWreck);
                std::function<juce::TextButton* (juce::Component&)> find = [&] (juce::Component& k) -> juce::TextButton* { for (auto* ch : k.getChildren()) { if (auto* b = dynamic_cast<juce::TextButton*> (ch); b != nullptr && b->getButtonText() == "SHOW CURVE") return b; if (auto* r = find (*ch)) return r; } return nullptr; };
                if (auto* b = find (*pe)) { b->setToggleState (true, juce::dontSendNotification); if (b->onClick) b->onClick(); }
                std::function<juce::Component* (juce::Component&)> findP = [&] (juce::Component& k) -> juce::Component* { for (auto* ch : k.getChildren()) { if (ch->getName() == "SHOW CURVE" && dynamic_cast<juce::TextButton*> (ch) == nullptr) return ch; if (auto* r = findP (*ch)) return r; } return nullptr; };
                if (auto* pn = findP (*pe); pn == nullptr || ! pn->isVisible()) std::printf ("FAIL SHOW CURVE: the panel did not open\n");
                snap ("17-wreck-show-curve");
                if (auto* b = find (*pe)) { b->setToggleState (false, juce::dontSendNotification); if (b->onClick) b->onClick(); }
            }
            const auto hl = engines::engineLabels (uHalls); int v = 0; for (size_t i = 0; i < hl.size(); ++i) if (hl[i].rfind ("VINTAGE", 0) == 0) { v = (int) i; break; }
            p.setParamNormalised (PhantomProcessor::pid (uHalls, "engine"), (float) v / (float) (hl.size() - 1)); pe->showUnit (uHalls); snap ("16-halls-vintage");
        }
    // SPATIAL (Phase 6b): Brandon's own example - reverb below, delay above, distortion on the right, crystal on the left - with recorded paths
    if (auto* pe = dynamic_cast<PhantomEditor*> (e.get()))
    {
        for (int u : { uGate, uGrid, uHaunt, uWarp, uOtt }) p.setParamNormalised (PhantomProcessor::pid (u, "on"), 0.0f);
        for (int u : { uEcho, uCrystal, uWreck, uChorus }) p.setParamNormalised (PhantomProcessor::pid (u, "on"), 1.0f);
        spatialtest::setP (p, "fx_spatial_on", 1.0f);
        spatialtest::place (p, uHalls, 0, -70, 6); spatialtest::place (p, uEcho, 20, 70, 8); spatialtest::place (p, uWreck, 90, 0, 3); spatialtest::place (p, uCrystal, -90, 10, 4); spatialtest::place (p, uChorus, 150, 20, 12);
        std::vector<SpPoint> path; for (int i = 0; i <= 160; ++i) { const float t = i / 160.0f; path.push_back ({ i * 0.1, -90.0f + 180.0f * t, 10.0f + 30.0f * std::sin (t * 6.28f), 4.0f + 3.0f * std::sin (t * 3.14f), i == 0 }); }
        p.spatialAuto.setPath (uCrystal, path); p.spatialAuto.armed[uCrystal] = true; p.spatialAuto.mode = spTouch;
        std::vector<SpPoint> p2; for (int i = 0; i <= 120; ++i) { const float t = i / 120.0f; p2.push_back ({ 20.0 + i * 0.1, 150.0f - 120.0f * t, 20.0f - 40.0f * t, 12.0f - 6.0f * t, i == 0 }); }
        p.spatialAuto.setPath (uChorus, p2);
        juce::AudioBuffer<float> buf (2, 256); juce::MidiBuffer m; for (int i = 0; i < 8; ++i) { buf.clear(); p.processBlock (buf, m); }
        pe->showSpatial (uWreck); snap ("20-spatial");
        p.setParamNormalised ("fx_spatial_out", 1.0f); pe->showSpatial (uCrystal); for (int i = 0; i < 4; ++i) { buf.clear(); p.processBlock (buf, m); } snap ("21-spatial-crystal-path");
    }
    return 0;
}

static void hauntSlideCheck()
{   // debug / check: HAUNT EDIT, a 220 Hz sine at -12 dBFS, a mouse held on pad 1 then slid across pads 2, 6, 7, 11 (150 ms each):
    // the biggest sample-to-sample step within 2 ms after each change, against the biggest anywhere else in the gestures
    Bench b (makeEffect (uHaunt)); b.setDefaults(); b.slot.applyPolicy(); auto* f = dynamic_cast<HauntEditFx*> (&b.slot.fx());
    const int sr = 48000, blk = 64, n = sr * 2; juce::AudioBuffer<float> buf (2, n);
    for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) buf.setSample (c, i, 0.25f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 220.0 * i / sr));
    const int seq[] = { 0, 1, 5, 6, 10 }; std::vector<int> at; int k = 0;
    for (int s0 = 0; s0 < n; s0 += blk)
    {
        const int want = s0 < sr / 4 ? -1 : (s0 - sr / 4) / (sr * 15 / 100) < 5 ? seq[(s0 - sr / 4) / (sr * 15 / 100)] : -1;
        if (f != nullptr && f->uiPad.load() != want) { f->uiPad.store (want); if (want >= 0 && k++ > 0) at.push_back (s0); }
        juce::AudioBuffer<float> part (buf.getArrayOfWritePointers(), 2, s0, blk); b.run (part);
    }
    float atSwitch = 0, elsewhere = 0;
    for (int i = sr / 4 + 1; i < sr / 4 + sr * 75 / 100; ++i)
    {
        const float d = std::abs (buf.getSample (0, i) - buf.getSample (0, i - 1)); bool nearS = false;
        for (int s : at) if (i >= s && i < s + sr / 500) nearS = true;
        (nearS ? atSwitch : elsewhere) = std::max (nearS ? atSwitch : elsewhere, d);
    }
    report (atSwitch <= std::max (0.02f, 1.5f * elsewhere), "HAUNT EDIT slide across pads", "largest step right after a pad change " + juce::String (atSwitch, 4) + ", elsewhere in the gestures " + juce::String (elsewhere, 4) + " (limit: 1.5x that, at least 0.02)");
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    if (argc >= 3 && juce::String (argv[1]) == "shots") return shots (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
    if (argc >= 2 && juce::String (argv[1]) == "osnull")
    {   // debug: the oversampler alone (up, nothing, down) against band-limited noise
        for (int f : { 2, 4, 8 }) for (bool render : { false, true })
        {
            Oversampler os; os.prepare (48000.0, 256, 2); os.set (f, render); auto in = bandNoise (48000.0, 48000, 0.5f, 7, argc >= 3 ? juce::String (argv[2]).getDoubleValue() : 20000.0); auto out = in;
            for (int i = 0; i + 256 <= 48000; i += 256) { juce::dsp::AudioBlock<float> b (out.getArrayOfWritePointers(), 2, (size_t) i, 256); auto up = os.up (b); juce::ignoreUnused (up); os.down (b); }
            for (int d = -2; d <= 2; ++d) { const int lat = (int) std::lround (os.latencySamples()) + d; float worst = 0; for (int i = 4800; i + lat < 47872; ++i) worst = juce::jmax (worst, std::abs (out.getSample (0, i + lat) - in.getSample (0, i)));
            std::printf ("factor %d render %d latency %.3f%+d residual %.1f dBFS\n", f, (int) render, os.latencySamples(), d, db (worst)); }
        }
        return 0;
    }
    if (argc >= 3 && juce::String (argv[1]) == "null")
    {   // debug: residual of one unit at its neutral setting, per 0.25 s
        const int u = juce::String (argv[2]).getIntValue(); Bench b (makeEffect (u), 48000.0, 256, (OversamplingPolicy) (argc >= 4 ? juce::String (argv[3]).getIntValue() : 2)); b.setDefaults();
        { auto& ps = b.slot.params(); std::vector<float> v (ps.size()); for (size_t i = 0; i < ps.size(); ++i) v[i] = ps[i].def; b.slot.fx().neutralise (v); for (size_t i = 0; i < v.size(); ++i) b.slot.setValue ((int) i, v[i]); b.slot.applyPolicy(); }
        auto in = bandNoise (48000.0, 96000, 0.5f); auto out = in; b.run (out); const int lat = b.slot.getLatencySamples();
        for (int i = 50000; i < 50008; ++i) std::printf ("in %.4f out %.4f\n", in.getSample (0, i), out.getSample (0, i + lat));
        for (int seg = 0; seg < 8; ++seg) { float worst = 0; for (int i = seg * 12000; i < (seg + 1) * 12000 && i + lat < 96000; ++i) worst = juce::jmax (worst, std::abs (out.getSample (0, i + lat) - in.getSample (0, i))); std::printf ("%.2fs residual %.1f dBFS\n", seg * 0.25, db (worst)); }
        return 0;
    }
    if (argc >= 3 && juce::String (argv[1]) == "shaper")
    {   // debug: shaper <kind index> -- a slow ramp through the stage
        Shaper sh; sh.prepare (2); sh.setKind ((Shaper::Kind) juce::String (argv[2]).getIntValue());
        for (int i = 0; i <= 40; ++i) { float l = -2.0f + i * 0.1f, r = l; sh.process (l, r, 1.0f, 0.0f); std::printf ("%.2f->%.4f ", -2.0f + i * 0.1f, l); } std::printf ("\n"); return 0;
    }
    if (argc >= 4 && juce::String (argv[1]) == "zip")
    {   // debug: zip <unit> <param id> [id=value ...]
        const int u = juce::String (argv[2]).getIntValue(); const auto specs = unitParams (u); int pi = -1; for (size_t i = 0; i < specs.size(); ++i) if (specs[i].id == argv[3]) pi = (int) i;
        zipVerbose = true; std::vector<std::pair<std::string, float>> ov; for (int a = 4; a < argc; ++a) { juce::String t (argv[a]); if (t.startsWith ("range=")) { zipLo = t.fromFirstOccurrenceOf ("=", false, false).upToFirstOccurrenceOf (",", false, false).getFloatValue(); zipHi = t.fromFirstOccurrenceOf (",", false, false).getFloatValue(); continue; } ov.push_back ({ t.upToFirstOccurrenceOf ("=", false, false).toStdString(), t.fromFirstOccurrenceOf ("=", false, false).getFloatValue() }); }
        std::printf ("zipper %.1f dBFS\n", zipperOf ([u] { return makeEffect (u); }, pi, ov)); return 0;
    }
    if (argc >= 3 && juce::String (argv[1]) == "imp")
    {   // debug: imp <unit> [id=value ...] -- impulse onset with oversampling off, neutral settings
        const int u = juce::String (argv[2]).getIntValue(); Bench b (makeEffect (u), 48000.0, 256, OversamplingPolicy::Off); b.setDefaults();
        { auto& ps = b.slot.params(); std::vector<float> v (ps.size()); for (size_t i = 0; i < ps.size(); ++i) v[i] = ps[i].def; b.slot.fx().neutralise (v); for (size_t i = 0; i < v.size(); ++i) b.slot.setValue ((int) i, v[i]);
          for (int a = 3; a < argc; ++a) { juce::String t (argv[a]); for (size_t i = 0; i < ps.size(); ++i) if (ps[i].id == t.upToFirstOccurrenceOf ("=", false, false).toStdString()) b.slot.setValue ((int) i, t.fromFirstOccurrenceOf ("=", false, false).getFloatValue()); } }
        b.slot.applyPolicy(); juce::AudioBuffer<float> imp (2, 8192); imp.clear(); imp.setSample (0, 4096, 0.5f); imp.setSample (1, 4096, 0.5f); b.run (imp);
        for (int i : { 0, 1, 2, 10, 100, 1000, 3000, 4090, 4095, 4096, 4097, 4098, 4099, 4100 }) std::printf ("%d:%.2e ", i - 4096, imp.getSample (0, i)); std::printf ("\nreported %d\n", b.slot.getLatencySamples()); return 0;
    }
    if (argc >= 3 && juce::String (argv[1]) == "gain")
    {   // debug: gain <unit> [id=value ...] -- RMS change at the given settings, white and audio-band noise
        const int u = juce::String (argv[2]).getIntValue();
        for (int kind = 0; kind < 2; ++kind)
        {
            Bench b (makeEffect (u)); b.setDefaults(); auto& ps = b.slot.params();
            for (int a = 3; a < argc; ++a) { juce::String t (argv[a]); for (size_t i = 0; i < ps.size(); ++i) if (ps[i].id == t.upToFirstOccurrenceOf ("=", false, false).toStdString()) b.slot.setValue ((int) i, t.fromFirstOccurrenceOf ("=", false, false).getFloatValue()); }
            b.slot.applyPolicy(); auto in = kind == 0 ? noise (96000, 0.25f, 3) : bandNoise (48000.0, 96000, 0.25f, 3); auto out = in; b.run (out);
            std::printf ("%s noise: RMS change %.2f dB\n", kind == 0 ? "white" : "band", db (rmsOf (out)) - db (rmsOf (in)));
        }
        return 0;
    }
    if (argc >= 4 && juce::String (argv[1]) == "enginetempo") { const int u = uEcho, e = juce::String (argv[2]).getIntValue(); echoTempoLock ("ECHO entry", [e] { return makeEffectOn (uEcho, e); }, juce::String (argv[3]).getIntValue()); juce::ignoreUnused (u); return failures; }
    if (argc >= 3 && juce::String (argv[1]) == "tempo") { const int m = juce::String (argv[2]).getIntValue(); echoTempoLock ("ECHO", [] { return makeEffect (uEcho); }, m); return failures; }
    if (argc >= 3 && juce::String (argv[1]) == "tailcheck") { const int u = juce::String (argv[2]).getIntValue(); tailCheck (unitDef (u).title, [u] { return makeEffect (u); }, false); return failures; }
    if (argc >= 3 && juce::String (argv[1]) == "tail")
    {   // debug: tail <unit> [id=value ...] -- level every 100 ms after a 0.5 s noise burst
        const int u = juce::String (argv[2]).getIntValue(); Bench b (makeEffect (u)); b.setDefaults(); auto& ps = b.slot.params();
        for (int a = 3; a < argc; ++a) { auto kv = juce::StringArray::fromTokens (argv[a], "=", ""); for (size_t i = 0; i < ps.size(); ++i) if (ps[i].id == kv[0].toStdString()) b.slot.setValue ((int) i, kv[1].getFloatValue()); }
        const int burst = 24000, total = 48000 * 20; juce::AudioBuffer<float> buf (2, total); buf.clear(); auto nz = noise (burst, 0.5f, 21); for (int c = 0; c < 2; ++c) buf.copyFrom (c, 0, nz, c, 0, burst);
        b.run (buf); for (int st = 0; st + 4800 <= total; st += 4800) { double e = 0; for (int c = 0; c < 2; ++c) for (int i = 0; i < 4800; ++i) e += buf.getSample (c, st + i) * (double) buf.getSample (c, st + i); std::printf ("%.1f:%.1f ", st / 48000.0, 10 * std::log10 (e / 9600 + 1e-30)); }
        std::printf ("\n"); return 0;
    }
    if (argc >= 2 && juce::String (argv[1]) == "cpurack") { cpuCheck(); return failures; }
    if (argc >= 2 && juce::String (argv[1]) == "engines") { enginetest::run (true); std::printf ("%d passed, %d failed\n", passes, failures); return failures; }
    if (argc >= 2 && juce::String (argv[1]) == "calibrate-engines") return enginetest::calibrate();
    if (argc >= 4 && juce::String (argv[1]) == "engineunit")
    {   // debug: engineunit <unit> <entry> -- the check table on one ENGINE menu entry
        const int u = juce::String (argv[2]).getIntValue(), e = juce::String (argv[3]).getIntValue(); gTailInformational = true;
        checkTable ((juce::String (unitDef (u).title) + " [" + juce::String (engines::engineLabels (u)[(size_t) e]) + "]").toRawUTF8(), [u, e] { return makeEffectOn (u, e); });
        std::printf ("%d passed, %d failed\n", passes, failures); return failures;
    }
    if (argc >= 5 && juce::String (argv[1]) == "enginezip")
    {   // debug: enginezip <unit> <entry> <control id> -- the zipper reading of one control on one ENGINE menu entry (swept and held still)
        const int u = juce::String (argv[2]).getIntValue(), e = juce::String (argv[3]).getIntValue(); const auto specs = unitParams (u); int pi = -1;
        for (size_t i = 0; i < specs.size(); ++i) if (specs[i].id == argv[4]) pi = (int) i;
        const float z = zipperOf ([u, e] { return makeEffectOn (u, e); }, pi, {}); std::printf ("swept %.1f still %.1f dBFS\n", z, zipperRest); return 0;
    }
    if (argc >= 3 && juce::String (argv[1]) == "enginetails")
    {   // debug: enginetails <unit> -- the reverb-tail check on every ENGINE menu entry
        const int u = juce::String (argv[2]).getIntValue(); const auto l = engines::engineLabels (u);
        for (int e = 0; e < (int) l.size(); ++e) tailCheck ((juce::String (unitDef (u).title) + " [" + juce::String (l[(size_t) e]) + "]").toRawUTF8(), [u, e] { return makeEffectOn (u, e); }, false);
        std::printf ("%d passed, %d failed\n", passes, failures); return failures;
    }
    if (argc >= 3 && juce::String (argv[1]) == "pvpages") return lookstest::pvpages (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
    if (argc >= 3 && juce::String (argv[1]) == "freerects") return lookstest::freerects (argv[2]);
    if (argc >= 2 && juce::String (argv[1]) == "pvparams") return lookstest::pvparams();
    if (argc >= 8 && juce::String (argv[1]) == "pvstill") { juce::StringArray l; for (int i = 2; i < argc; ++i) l.add (argv[i]); return lookstest::pvstill (l); }
    if (argc >= 2 && juce::String (argv[1]) == "lookprobe") return lookstest::lookprobe();
    if (argc >= 2 && juce::String (argv[1]) == "lookscost") { lookstest::costChecks(); std::printf ("%d passed, %d failed\n", passes, failures); return failures; }
    if (argc >= 2 && juce::String (argv[1]) == "looks") { lookstest::looksChecks(); std::printf ("%d passed, %d failed\n", passes, failures); return failures; }
    if (argc >= 3 && juce::String (argv[1]) == "visualshots") return lookstest::visualshots (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
    if (argc >= 3 && juce::String (argv[1]) == "lookshots") return lookstest::lookshots (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]), argc >= 4 && juce::String (argv[3]) == "jpg");
    if (argc >= 2 && juce::String (argv[1]) == "spatial") { spatialtest::run (true); std::printf ("%d passed, %d failed\n", passes, failures); return failures; }
    if (argc >= 2 && juce::String (argv[1]) == "spatialdbg") { spatialtest::debug (argc >= 3 ? juce::String (argv[2]).getIntValue() : -1); return 0; }
    if (argc >= 3 && juce::String (argv[1]) == "spatialdemo") return spatialtest::demo (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
    if (argc >= 2 && juce::String (argv[1]) == "spatialcpu") { spatialtest::cpuChecks(); return failures; }
    if (argc >= 2 && juce::String (argv[1]) == "rhythm") { rhythmtest::run(); hauntSlideCheck(); std::printf ("%d passed, %d failed\n", passes, failures); return failures; }
    if (argc >= 3 && juce::String (argv[1]) == "clips") return mastertest::renderClips (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
    if (argc >= 3 && juce::String (argv[1]) == "statedump")
    {   // debug: statedump <dir> -- the rack round trip's two saved states as XML (a.xml, b.xml)
        PhantomProcessor a; a.prepareToPlay (48000.0, 256); std::mt19937 rng (99); std::uniform_real_distribution<float> d (0.0f, 1.0f);
        for (auto* p : a.getParameters()) if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p)) rp->setValueNotifyingHost (p->isDiscrete() || p->isBoolean() ? std::round (d (rng)) : d (rng));
        juce::MemoryBlock mb; a.getStateInformation (mb); PhantomProcessor b; b.prepareToPlay (48000.0, 256); b.setStateInformation (mb.getData(), (int) mb.getSize());
        juce::MemoryBlock mb2; b.getStateInformation (mb2); const juce::File dir (argv[2]);
        if (auto x = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize())) x->writeTo (dir.getChildFile ("a.xml"));
        if (auto x = juce::AudioProcessor::getXmlFromBinary (mb2.getData(), (int) mb2.getSize())) x->writeTo (dir.getChildFile ("b.xml"));
        return 0;
    }
    if (argc >= 2 && juce::String (argv[1]) == "enginelabels")
    {   // debug: engineLabels and the ENGINE menu's choices side by side
        const int u = argc >= 3 ? juce::String (argv[2]).getIntValue() : uWreck; const auto l = engines::engineLabels (u); std::vector<std::string> ch; for (auto& q : unitParams (u)) if (q.id == "engine") ch = q.choices;
        for (size_t i = 0; i < std::max (l.size(), ch.size()) && i < 6; ++i) std::printf ("%zu  label=%-10s choice=%s\n", i, i < l.size() ? l[i].c_str() : "-", i < ch.size() ? ch[i].c_str() : "-");
        std::printf ("labels %zu, choices %zu\n", l.size(), ch.size()); return 0;
    }
    if (argc >= 2 && juce::String (argv[1]) == "hauntslide") { hauntSlideCheck(); std::printf ("%d passed, %d failed\n", passes, failures); return failures; }   // debug
    if (argc >= 2 && juce::String (argv[1]) == "curvediff")
    {   // debug: WRECKER on each CURVES set, a 110 Hz sine at -12 dBFS, AUTO GAIN on: harmonic distortion (% of the output) at DRIVE 30 / 60 / 90 %
        const auto l = engines::engineLabels (uWreck); int first = -1; for (int e = 0; e < (int) l.size(); ++e) if (l[(size_t) e].rfind ("CURVES", 0) == 0) { first = e; break; }
        const auto& lib = engines::curveLibrary();
        for (int v = 0; v < (int) lib.size(); ++v)
        {
            { const auto sp = unitParams (uWreck); std::string lab; for (auto& q : sp) if (q.id == "engine" && first + v < (int) q.choices.size()) lab = q.choices[(size_t) (first + v)];
              std::printf ("%-28s [menu: %-34s] ", lib[(size_t) v].name.substr (0, 27).c_str(), lab.c_str()); }
            for (float d : { 0.3f, 0.6f, 0.9f })
            {
                Bench b (makeEffectOn (uWreck, first + v)); b.setDefaults(); mastertest::setParam (b, "drive", d); b.slot.applyPolicy();
                for (int k = 0; k < 40 && engines::builtCurves (v) == nullptr; ++k) juce::Thread::sleep (25);
                juce::AudioBuffer<float> buf (2, 48000); for (int c = 0; c < 2; ++c) for (int i = 0; i < 48000; ++i) buf.setSample (c, i, 0.251f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 110.0 * i / 48000.0));
                b.run (buf);
                // one exact period count (110 Hz x 0.5 s = 55 periods) from 0.5 s on: the fundamental by correlation, the rest is distortion
                double tot = 0, cs = 0, sn = 0; const int s0 = 24000, n = 24000;
                for (int i = 0; i < n; ++i) { const double y = buf.getSample (0, s0 + i), ph = 2.0 * juce::MathConstants<double>::pi * 110.0 * (s0 + i) / 48000.0; tot += y * y; cs += y * std::cos (ph); sn += y * std::sin (ph); }
                const double fund = 2.0 * (cs * cs + sn * sn) / n; const double rest = juce::jmax (0.0, tot - fund);
                std::printf ("  %6.2f%%", 100.0 * std::sqrt (rest / juce::jmax (1e-12, tot)));
            }
            std::printf ("\n");
        }
        return 0;
    }
    if (argc >= 2 && juce::String (argv[1]) == "engineswitch") { for (int u : enginetest::units()) enginetest::switchClick (u); std::printf ("%d passed, %d failed\n", passes, failures); return failures; }   // debug
    if (argc >= 5 && juce::String (argv[1]) == "enginetp1")
    {   // debug: enginetp1 <entry> <quality> <which: 0 beat, 1 melody, 2 noise>
        const int e = juce::String (argv[2]).getIntValue(), q = juce::String (argv[3]).getIntValue(), which = juce::String (argv[4]).getIntValue();
        Bench b (makeEffectOn (uLim, e)); b.setDefaults(); mastertest::setParam (b, "gain", 12.0f); mastertest::setParam (b, "quality", (float) q); mastertest::setParam (b, "ceiling", -0.3f); b.slot.applyPolicy();
        auto buf = which < 2 ? mastertest::material (which, 48000.0, 48000 * 4) : noise (48000 * 4, 0.9f, 99); b.run (buf);
        const int lat = b.slot.getLatencySamples(); float spk = 0; for (int c = 0; c < 2; ++c) for (int i = 4800 + lat; i < buf.getNumSamples(); ++i) spk = std::max (spk, std::abs (buf.getSample (c, i)));
        std::printf ("lat %d  true-peak over %.3f dB  sample peak %.3f dBFS\n", lat, mastertest::truePeakDb (buf, 4800 + lat) + 0.3f, 20 * std::log10 (spk)); return 0;
    }
    if (argc >= 2 && juce::String (argv[1]) == "enginetp") { enginetest::limiterEngineTruePeak(); std::printf ("%d passed, %d failed\n", passes, failures); return failures; }   // debug
    if (argc >= 2 && juce::String (argv[1]) == "enginemem")
    {   // debug: resident memory each unit's effect takes once prepared (48 kHz, 256), in MB
        auto rss = [] { long kb = 0; if (FILE* f = std::fopen ("/proc/self/status", "r")) { char line[256]; while (std::fgets (line, sizeof line, f)) if (std::strncmp (line, "VmRSS:", 6) == 0) kb = std::atol (line + 6); std::fclose (f); } return kb / 1024.0; };
        for (int u = 0; u < kNumUnits; ++u)
        {
            const double r0 = rss(); auto fx = makeEffect (u); fx->prepare (48000.0, 256, 2); const double r1 = rss();
            std::printf ("%-8s %8.1f MB\n", unitDef (u).key, r1 - r0); std::fflush (stdout);
            static std::vector<std::unique_ptr<PhantomEffect>> keep; keep.push_back (std::move (fx));
        }
        return 0;
    }
    if (argc >= 2 && juce::String (argv[1]) == "unitcpu")
    {   // debug: each unit alone at its defaults, thread CPU time (best of 5 x 2000 blocks), % of one core
        auto tcpu = [] { timespec ts; clock_gettime (CLOCK_THREAD_CPUTIME_ID, &ts); return ts.tv_sec + ts.tv_nsec * 1e-9; };
        for (int u = 0; u < kNumUnits; ++u)
        {
            PhantomProcessor p; for (auto* q : p.getParameters()) if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (q)) if (rp->getParameterID() == juce::String ("fx_") + unitDef (u).key + "_on") q->setValueNotifyingHost (1.0f);
            p.prepareToPlay (48000.0, 256); auto buf = noise (256, 0.5f); juce::MidiBuffer m; for (int i = 0; i < 400; ++i) p.processBlock (buf, m);
            double best = 1e9; for (int r = 0; r < 5; ++r) { const double t0 = tcpu(); for (int i = 0; i < 2000; ++i) p.processBlock (buf, m); best = std::min (best, tcpu() - t0); }
            std::printf ("%-8s %6.2f\n", unitDef (u).key, 100.0 * best / (2000 * 256.0 / 48000.0));
        }
        return 0;
    }
    if (argc >= 2 && juce::String (argv[1]) == "rack") { rackChecks(); std::printf ("%d passed, %d failed\n", passes, failures); return failures; }
    if (argc >= 2 && juce::String (argv[1]) == "master") { mastertest::limiterChecks(); mastertest::warpChecks(); std::printf ("%d passed, %d failed\n", passes, failures); return failures; }
    if (argc >= 2 && juce::String (argv[1]) == "factory") { factoryChecks(); return failures; }
    if (argc >= 2 && juce::String (argv[1]) == "retrobias") { retroBiasAlias(); return failures; }
    if (argc >= 2 && juce::String (argv[1]) == "eq") { eqChecks(); std::printf ("%d passed, %d failed\n", passes, failures); return failures; }
    if (argc >= 3 && juce::String (argv[1]) == "unit")
    {   // the check table for some units only: unit <u> [u ...]
        for (int a = 2; a < argc; ++a) { const int u = juce::String (argv[a]).getIntValue(); checkTable (unitDef (u).title, [u] { return makeEffect (u); }); }
        std::printf ("%d passed, %d failed\n", passes, failures); return failures;
    }
    if (argc >= 3 && juce::String (argv[1]) == "cpu")
    {   // debug: cpu <unit> <policy> [id=value ...] -- us per 256-sample block
        const int u = juce::String (argv[2]).getIntValue(); Bench b (makeEffect (u), 48000.0, 256, (OversamplingPolicy) juce::String (argv[3]).getIntValue()); b.setDefaults(); auto& ps = b.slot.params();
        for (int a = 4; a < argc; ++a) { juce::String t (argv[a]); for (size_t i = 0; i < ps.size(); ++i) if (ps[i].id == t.upToFirstOccurrenceOf ("=", false, false).toStdString()) b.slot.setValue ((int) i, t.fromFirstOccurrenceOf ("=", false, false).getFloatValue()); }
        b.slot.applyPolicy(); auto buf = noise (256 * 400, 0.3f); const auto t0 = std::chrono::steady_clock::now(); b.run (buf);
        std::printf ("%.1f us/block\n", std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count() / 400.0); return 0;
    }
    if (argc >= 3 && juce::String (argv[1]) == "alias")
    {   // debug: alias <unit> <policy 0..4> [id=value ...]
        const int u = juce::String (argv[2]).getIntValue(); const auto pol = (OversamplingPolicy) (argc >= 4 ? juce::String (argv[3]).getIntValue() : 3);
        zipVerbose = true; std::vector<std::pair<std::string, float>> ov; for (int a = 4; a < argc; ++a) { juce::String t (argv[a]); if (t.startsWith ("range=")) { zipLo = t.fromFirstOccurrenceOf ("=", false, false).upToFirstOccurrenceOf (",", false, false).getFloatValue(); zipHi = t.fromFirstOccurrenceOf (",", false, false).getFloatValue(); continue; } ov.push_back ({ t.upToFirstOccurrenceOf ("=", false, false).toStdString(), t.fromFirstOccurrenceOf ("=", false, false).getFloatValue() }); }
        juce::String at; const float w = worstAlias ([u] { return makeEffect (u); }, pol, ov, at); std::printf ("worst %.1f dBFS at %s\n", w, at.toRawUTF8()); return 0;
    }
    if (argc >= 3 && juce::String (argv[1]) == "spectrum")
    {   // debug: top bins of one unit at full drive on a 2 kHz tone (unit index, optional policy 0..4)
        const int u = juce::String (argv[2]).getIntValue(); const auto pol = (OversamplingPolicy) (argc >= 4 ? juce::String (argv[3]).getIntValue() : 2);
        Bench b (makeEffect (u), 48000.0, 256, pol); b.setDefaults(); auto& specs = b.slot.params();
        for (size_t i = 0; i < specs.size(); ++i) if (specs[i].isContinuous() && specs[i].id.find ("drive") != std::string::npos) b.slot.setValue ((int) i, specs[i].max);
        b.slot.applyPolicy();
        const int order = 15, N = 1 << order; const float fq = (float) (std::round (2000.0 * N / 48000.0) * 48000.0 / N);
        auto buf = sine (48000.0, N * 2, fq, 0.9f); b.run (buf); auto sp = spectrumDb (buf, N, order);
        std::vector<std::pair<float, int>> top; for (int k = 1; k < N / 2; ++k) top.push_back ({ sp[(size_t) k], k }); std::sort (top.rbegin(), top.rend());
        for (int i = 0; i < 14; ++i) std::printf ("%8.1f Hz  %6.1f dB\n", top[(size_t) i].second * 48000.0 / N, top[(size_t) i].first);
        std::printf ("peak %.3f rms %.3f latency %d\n", peakOf (buf), rmsOf (buf), b.slot.getLatencySamples());
        for (int i = 48000; i < 48000 + 30; ++i) std::printf ("%.3f ", buf.getSample (0, i)); std::printf ("\n");
        return 0;
    }
    std::printf ("Phantom FX tests, version %s\n", JucePlugin_VersionString);
    checkTable ("PASS (pass-through)", [] { return std::make_unique<PassThrough>(); });
    for (int u = 0; u < kNumUnits; ++u) checkTable (unitDef (u).title, [u] { return makeEffect (u); });
    enginetest::run (true);
    eqChecks(); retroBiasAlias(); factoryChecks();
    rhythmtest::run(); hauntSlideCheck();
    mastertest::limiterChecks();
    mastertest::warpChecks();
    rackChecks();
    spatialtest::run (false);
    lookstest::looksChecks();
    if (juce::SystemStats::getEnvironmentVariable ("PHANTOM_SKIP_COST", "").isEmpty()) lookstest::costChecks();   // (the Mac CI runs it on its own, under a time limit)
    std::printf ("%d passed, %d failed\n", passes, failures);
    return failures;
}
