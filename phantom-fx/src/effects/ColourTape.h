// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX 9 RETRO: vintage colour around ChowTapeModel's real tape DSP (GPL-3.0, compiled unchanged from third_party/chowtape):
// its hysteresis solver (RK4) for MAGNETIC and tape bias, its wow and flutter generators driving a modulated delay as its
// WowFlutterProcessor does, its degrade noise + filter. Around it: the shared saturation stage / Airwindows for DISTORT,
// a decimator for DIGITAL, a small diffuser for SPACE, and the noise beds (our own generators: no upstream exists for them).
#pragma once
#ifndef HYST_STEPS
 #define HYST_STEPS 2
#endif
#include "ColourAw.h"
#include "HysteresisProcessing.h"
#include "WowProcess.h"
#include "FlutterProcess.h"
#include "DegradeNoise.h"
#include "DegradeFilter.h"
#include <random>

namespace phantom
{
class RetroFx final : public CatalogEffect
{
public:
    RetroFx() : CatalogEffect (uRetro) {}
    bool isNonlinear() const override { return true; }
    int oversamplingFactor() const override { return 16; }   // ChowTapeModel's hysteresis solver is run oversampled (its own range is 2-16x)
    int getLatencySamples() const override { return Shaper::latencySamples + AwStage::latency (true); }
    void neutralise (std::vector<float>& v) const override
    {
        for (auto* k : { "noise_on", "wobble_on", "dist_on", "digi_on", "space_on", "mag_on" }) v[(size_t) id (k)] = 0.0f;
        v[(size_t) id ("degrade")] = 0.0f; v[(size_t) id ("tone")] = 0.0f;
    }
    void aliasIsolate (std::vector<float>& v) const override { v[(size_t) id ("wobble_on")] = 0.0f; }
    void zipperIsolate (const std::string& c, std::vector<float>& v) const override { if (c == "wobble" || c == "flutter" || c == "magic") for (auto* k : { "dist_on", "mag_on", "noise_on" }) v[(size_t) id (k)] = 0.0f; }
    void prepareEffect (double sampleRate, int maxBlock, int ch) override
    {
        sr = sampleRate;
        for (auto& h : hyst) { h.setSampleRate (sampleRate * kHystSteps); h.reset(); } hystPrev[0] = hystPrev[1] = 0.0;
        // Chow's wow / flutter run at a control rate near 48 kHz (as in Chow's own plugin, where they sit outside the oversampling, so their
        // depth in time is Chow's); the tape delay between control points is interpolated at the processing rate
        ctl = juce::jmax (1, (int) std::lround (sampleRate / 48000.0)); while (kSub % ctl != 0) --ctl;
        wow.prepare (sampleRate / ctl, maxBlock / ctl + 1, 2); flutter.prepare (sampleRate / ctl, maxBlock / ctl + 1, 2);
        delay = std::make_unique<juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd>> ((int) (sampleRate * 0.1));
        delay->prepare ({ sampleRate, (juce::uint32) maxBlock, 2 }); delay->setDelay (0.0f);
        for (auto& f : degF) f.reset ((float) sampleRate, 20); for (auto& nz : degN) nz.prepare();
        distSh.prepare (ch); tube.prepare (aw::makeTube2(), sampleRate, maxBlock, true, 0.97f); console.prepare (aw::makeConsole7Channel(), sampleRate, maxBlock);
        for (auto* f : { &bumpF, &lossF, &toneLo, &toneHi, &nTone, &hissHp, &radioBp, &spaceLp, &rumbleLp, &crackleBp }) f->prepare (sampleRate);
        for (auto& d : diff) d.assign ((size_t) (sampleRate * 0.03) + 8, 0.0f); spaceBuf.assign ((size_t) (sampleRate * 0.08) + 8, 0.0f);
        { const float top = (float) juce::jmin (20000.0, sampleRate * 0.45); for (auto& b : magBand) b.prepare (sampleRate); magBand[0].set (sst::filters::CytomicSVF::Mode::Lowpass, top, 0.2346f); magBand[1].set (sst::filters::CytomicSVF::Mode::Lowpass, top, 0.6173f); }
        dryBuf.setSize (2, maxBlock); agTape.prepare (sampleRate); envF.prepare (sampleRate); envF.setTimes (5.0f, 120.0f);
        for (auto* k : { "magic", "noise_on", "noise", "ntype", "ntone", "nfollow", "wobble_on", "wobble", "flutter", "dist_on", "dist", "dtype", "digi_on", "digi", "space_on", "space", "mag_on", "mag", "bias", "speed", "degrade", "tone", "out" })
            pid[k] = id (k);
        for (auto& [k, v] : pid) juce::ignoreUnused (k, v);
        q_bias = id ("bias"); q_degrade = id ("degrade"); q_digi = id ("digi"); q_digi_on = id ("digi_on"); q_dist = id ("dist"); q_dist_on = id ("dist_on"); q_dtype = id ("dtype"); q_flutter = id ("flutter"); q_mag = id ("mag"); q_mag_on = id ("mag_on"); q_magic = id ("magic"); q_nfollow = id ("nfollow"); q_noise = id ("noise"); q_noise_on = id ("noise_on"); q_ntone = id ("ntone"); q_ntype = id ("ntype"); q_out = id ("out"); q_space = id ("space"); q_space_on = id ("space_on"); q_speed = id ("speed"); q_tone = id ("tone"); q_wobble = id ("wobble"); q_wobble_on = id ("wobble_on");
        rng.seed (12345u);
    }
    void resetEffect() override
    {
        for (auto& h : hyst) h.reset(); hystPrev[0] = hystPrev[1] = 0.0; delay->reset(); distSh.reset(); tube.reset(); console.reset();
        for (auto* f : { &bumpF, &lossF, &toneLo, &toneHi, &nTone, &hissHp, &radioBp, &spaceLp, &rumbleLp, &crackleBp }) f->reset();
        for (auto& d : diff) std::fill (d.begin(), d.end(), 0.0f); std::fill (spaceBuf.begin(), spaceBuf.end(), 0.0f); agTape.reset(); envF.reset(); for (auto& b : magBand) b.reset();
        pinkB.fill (0.0f); humPhase = radioPhase = 0.0; decL = decR = 0; decPhase = 0; padA[0] = padA[1] = 0.0f; act = 0.0f;
    }
    void process (juce::dsp::AudioBlock<float> block, const ModulationContext& ctx) noexcept override
    {
        beginBlock (ctx);
        const int n = (int) block.getNumSamples(), ch = (int) block.getNumChannels();
        auto* L = block.getChannelPointer (0); auto* R = ch > 1 ? block.getChannelPointer (1) : L;
        using M = sst::filters::CytomicSVF::Mode;
        const bool noiseOn = on (q_noise_on), wobOn = on (q_wobble_on), distOn = on (q_dist_on), digiOn = on (q_digi_on), spaceOn = on (q_space_on), magOn = on (q_mag_on), follow = on (q_nfollow);
        const int ntype = choice (q_ntype), dtype = choice (q_dtype), speed = choice (q_speed);
        const float speedK = speed == 0 ? 0.5f : speed == 1 ? 1.0f : 2.0f;   // 7.5 / 15 / 30 ips
        for (int i0 = 0; i0 < n; i0 += kSub)
        {
            const int len = juce::jmin (kSub, n - i0);
            const float magic = p (q_magic).skip (len) * 2.0f;   // MAGIC scales every section: 50 % = as set, 0 = nothing, 100 % = twice
            auto amt = [&] (int k, bool sectionOn) { const float v = p (k).skip (len); return sectionOn ? juce::jlimit (0.0f, 1.0f, v * magic) : 0.0f; };
            const float nAmt = amt (q_noise, noiseOn), wAmt = amt (q_wobble, wobOn), fAmt = amt (q_flutter, wobOn), dAmt = amt (q_dist, distOn), gAmt = amt (q_digi, digiOn), sAmt = amt (q_space, spaceOn), mAmt = amt (q_mag, magOn);
            const float bias = p (q_bias).skip (len), degr = juce::jlimit (0.0f, 1.0f, p (q_degrade).skip (len) * magic), tone = p (q_tone).skip (len), ntone = p (q_ntone).skip (len), out = p (q_out).skipGain (len);
            for (int i = i0; i < i0 + len; ++i) { dryBuf.setSample (0, i, L[i]); dryBuf.setSample (1, i, R[i]); }

            // ---- DISTORT: TAPE / TRANSISTOR / FUZZ on the shared anti-aliased stage, TUBE / CONSOLE through Airwindows (each guarded)
            {
                const int k = dtype; const bool aw = k == 1 || k == 2;
                distSh.setKind (k == 0 ? Shaper::Kind::Tape : k == 3 ? Shaper::Kind::Transistor : Shaper::Kind::Hard);
                for (int i = i0; i < i0 + len; ++i)
                {
                    float l = L[i], r = R[i];
                    if (! aw && dAmt > 0.0005f) { const float g = 1.0f + dAmt * (k == 4 ? 20.0f : 6.0f); distSh.process (l, r, g, k == 4 ? 0.1f * dAmt : 0.0f); l /= g; r /= g; const float a = fadeIn (dAmt, 0.1f); const float dl = padA[0], dr = padA[1]; l = dl + (l - dl) * a; r = dr + (r - dr) * a; }
                    else distSh.pass (l, r);
                    padA[0] = L[i]; padA[1] = R[i]; L[i] = l; R[i] = r;
                }
                tube.fx->setParameter (0, 1.0f); tube.fx->setParameter (1, 1.0f); console.fx->setParameter (0, 0.772f);
                const float ta = k == 1 ? fadeIn (dAmt, kFade) : 0.0f, ca = k == 2 ? fadeIn (dAmt, kFade) : 0.0f;
                tube.run (L, R, i0, len, ta, ta);   // (always run: the guard's one-sample pad keeps every DISTORT type at the same latency)
                console.run (L, R, i0, len, ca, ca);   // (its input is already guarded by the tube stage's pad path: no second guard)
            }

            // ---- MAGNETIC: ChowTapeModel's hysteresis (RK4). Drive = MAGNETIC, Chow's "width" = TAPE BIAS, saturation = half;
            // Chow's own make-up gain; then the head bump and high-frequency loss of the chosen tape speed
            if (mAmt > 0.0005f || p (q_mag).isSmoothing())
            {
                const double drive = 0.1 + kMagDrive * mAmt, width = 0.9 * bias, sat = 0.5;   // (Chow width 1 = c below zero: its solver then rings)
                for (auto& h : hyst) h.cook (drive, width, sat, false);
                const float makeup = (float) ((1.0 + 0.6 * width) / (0.5 + 1.5 * (1.0 - sat)));
                bumpF.set (M::Bell, 60.0f * std::sqrt (speedK) * 1.4f, 0.6f, dbToGain (3.0f / std::sqrt (speedK)));
                lossF.set (M::Lowpass, juce::jmin (0.45f * (float) sr, 9000.0f * speedK * 1.2f), 0.2f);
                const float a = fadeIn (mAmt, 0.1f);
                for (int i = i0; i < i0 + len; ++i)
                {
                    float bl = L[i], br = R[i]; magBand[0].process (bl, br); magBand[1].process (bl, br);   // held to the audio band first (see AwStage)
                    // the solver runs kHystSteps times per (already oversampled) sample, on the line between the last input and this
                    // one, and its steps are averaged: at a wide TAPE BIAS the loop's corners make harmonics past the 100th, which
                    // one step per sample folded back into the audio band (-74 dBFS)
                    double o alignas (16) [2] { 0.0, 0.0 };
                    for (int st = 1; st <= kHystSteps; ++st)
                    {
                        const double f = (double) st / kHystSteps; double in alignas (16) [2] { hystPrev[0] + ((double) bl - hystPrev[0]) * f, hystPrev[1] + ((double) br - hystPrev[1]) * f };
                        const auto y = hyst[0].process<RK4> (xsimd::batch<double>::load_aligned (in));
                        double os alignas (16) [2]; y.store_aligned (os); o[0] += os[0] / kHystSteps; o[1] += os[1] / kHystSteps;
                    }
                    hystPrev[0] = (double) bl; hystPrev[1] = (double) br;
                    float l = (float) o[0] * makeup, r = (float) o[1] * makeup;
                    if (! std::isfinite (l) || ! std::isfinite (r)) { l = r = 0.0f; hyst[0].reset(); }
                    bumpF.process (l, r); lossF.process (l, r);
                    L[i] += (l - L[i]) * a; R[i] += (r - R[i]) * a;
                }
            }

            // ---- WOW + FLUTTER: Chow's generators into a modulated delay, exactly as its WowFlutterProcessor wires them
            if (wowIdle && wAmt <= 0.0005f && fAmt <= 0.0005f)
            {   // idle: the tape delay rests; it is cleared when wobble comes back, so it never replays stale tape
                delayStale = true;
            }
            else
            {
                if (delayStale) { delay->reset(); delayStale = false; lastLen = 0.0f; }
                const int nc = (len + ctl - 1) / ctl;
                wow.prepareBlock (std::pow (wAmt, 3.0f), (std::pow (4.5f, 0.25f) - 1.0f) * speedK, 0.2f, 0.1f, nc, 2);
                flutter.prepareBlock (std::pow (std::pow (fAmt, 3.0f) * 81.0f / 625.0f, 0.5f), 0.1f * std::pow (1000.0f, 0.3f) * speedK, nc, 2);
                // one tape, one transport: the left channel's wow / flutter moves both channels, so a mono source stays mono
                const float fsCtl = (float) sr / (float) ctl;
                for (int c = 0; c < nc; ++c)
                {
                    auto [wl, wo] = wow.getLFO (c, 0); auto [fl, fo] = flutter.getLFO (c, 0);
                    const float target = juce::jlimit (0.0f, (float) sr * 0.09f, (wl + fl + fo + wo) * fsCtl / 1000.0f * (float) ctl);
                    const int k0 = c * ctl, k1 = juce::jmin (len, k0 + ctl);
                    for (int k = k0; k < k1; ++k)
                    {
                        const float lenS = lastLen + (target - lastLen) * (float) (k - k0 + 1) / (float) (k1 - k0);
                        delay->setDelay (lenS);
                        delay->pushSample (0, L[i0 + k]); L[i0 + k] = delay->popSample (0);
                        delay->pushSample (1, R[i0 + k]); R[i0 + k] = delay->popSample (1);
                    }
                    lastLen = target;
                }
                wow.boundPhase (0); flutter.boundPhase (0);
                wowIdle = wAmt <= 0.0005f && fAmt <= 0.0005f && wow.shouldTurnOff() && flutter.shouldTurnOff();
            }

            // ---- DEGRADE: Chow's noise + sweeping low-pass + level loss (depth = amount = DEGRADE)
            if (degr > 0.0005f || p (q_degrade).isSmoothing())
            {
                const float freqHz = 200.0f * std::pow (20000.0f / 200.0f, 1.0f - degr), gain = dbToGain (-24.0f * degr * 0.5f);
                for (int c = 0; c < 2; ++c) { degN[(size_t) c].setGain (0.5f * degr * degr * 0.03f); degF[(size_t) c].setFreq (juce::jmin (freqHz, 0.45f * (float) sr)); }
                for (int c = 0; c < 2; ++c)
                {
                    float* x = c == 0 ? L + i0 : R + i0; float tmp[kSub]; std::copy (x, x + len, tmp);
                    degN[(size_t) c].processBlock (tmp, len); degF[(size_t) c].process (tmp, len);
                    const float a = fadeIn (degr, 0.05f);
                    for (int k = 0; k < len; ++k) x[k] += (tmp[k] * gain - x[k]) * a;
                }
            }

            // ---- DIGITAL: bit depth and sample-rate reduction
            if (gAmt > 0.0005f)
                for (int i = i0; i < i0 + len; ++i)
                {
                    const float steps = std::pow (2.0f, 16.0f - gAmt * 12.0f); float l = std::round (L[i] * steps) / steps, r = std::round (R[i] * steps) / steps;
                    const float ratio = 1.0f + gAmt * 15.0f * 4.0f; decPhase += 1.0f; if (decPhase >= ratio) { decPhase -= ratio; decL = l; decR = r; }
                    const float a = fadeIn (gAmt, 0.05f); L[i] += (decL - L[i]) * a; R[i] += (decR - R[i]) * a;
                }

            // ---- SPACE: a small room (three diffusing all-passes into a short damped echo)
            if (sAmt > 0.0005f || spaceTail > 0)
            {
                spaceLp.set (M::Lowpass, 5000.0f, 0.2f);
                for (int i = i0; i < i0 + len; ++i)
                {
                    float m = 0.5f * (L[i] + R[i]);
                    static constexpr float times[3] { 0.0071f, 0.0113f, 0.0173f };
                    for (int d = 0; d < 3; ++d) { auto& b = diff[(size_t) d]; const int dl = (int) (times[d] * sr); const size_t rd = (diffW + b.size() - (size_t) dl) % b.size(); const float z = b[rd]; const float v = m + 0.6f * z; b[diffW % b.size()] = v; m = z - 0.6f * v; }
                    ++diffW;
                    const int el = (int) (0.043 * sr); const size_t rd = (spaceW + spaceBuf.size() - (size_t) el) % spaceBuf.size(); float e = spaceBuf[rd], er = e; spaceLp.process (e, er);
                    spaceBuf[spaceW % spaceBuf.size()] = m + 0.45f * e; ++spaceW;
                    const float w = sAmt * 0.5f; L[i] += (m + e) * w; R[i] += (m - e * 0.5f) * w;
                }
                spaceTail = sAmt > 0.0005f ? (int) (sr * 0.5) : juce::jmax (0, spaceTail - len);
            }

            // ---- level: a slow RMS match of the coloured signal against the input (faded in as far as anything colours it), before the beds
            {
                const float activity = fadeIn (juce::jmax (juce::jmax (dAmt, mAmt, degr), juce::jmax (gAmt, sAmt)), 0.1f);
                if (activity > 0.0f || act > 1.0e-6f) for (int i = i0; i < i0 + len; ++i)
                {
                    agTape.feedIn (dryBuf.getSample (0, i), dryBuf.getSample (1, i)); act += 0.0005f * (activity - act);
                    const float g = 1.0f + (agTape.gain (L[i], R[i]) - 1.0f) * act; L[i] *= g; R[i] *= g;
                }
            }
            // ---- NOISE: the beds, optionally following the input's level
            if (nAmt > 0.0005f)
            {
                nTone.set (ntone >= 0.0f ? M::Highpass : M::Lowpass, ntone >= 0.0f ? 20.0f + ntone * ntone * 4000.0f : 20000.0f * std::pow (0.05f, -ntone), 0.2f);
                hissHp.set (M::Highpass, 2500.0f, 0.2f); radioBp.set (M::Bandpass, 1400.0f, 0.5f); rumbleLp.set (M::Lowpass, 90.0f, 0.3f); crackleBp.set (M::Bandpass, 3000.0f, 0.3f);
                for (int i = i0; i < i0 + len; ++i)
                {
                    const float e = envF.next (juce::jmax (std::abs (dryBuf.getSample (0, i)), std::abs (dryBuf.getSample (1, i))));
                    float nl = 0, nr = 0; noiseSample (ntype, nl, nr); nr = nl; nTone.process (nl, nr);   // the beds sit in the centre (mono-safe)
                    const float lvl = nAmt * 0.06f * (follow ? juce::jlimit (0.0f, 2.0f, e * 4.0f) : 1.0f);
                    L[i] += nl * lvl; R[i] += nr * lvl;
                }
            }

            // ---- TONE (tilt) and the output
            if (std::abs (tone) > 1.0e-4f || p (q_tone).isSmoothing())
            {
                toneLo.set (M::LowShelf, 400.0f, 0.3f, dbToGain (-tone * 6.0f)); toneHi.set (M::HighShelf, 2500.0f, 0.3f, dbToGain (tone * 6.0f));
                for (int i = i0; i < i0 + len; ++i) { float l = L[i], r = R[i]; toneLo.process (l, r); toneHi.process (l, r); L[i] = l; R[i] = r; }
            }
            for (int i = i0; i < i0 + len; ++i) { L[i] *= out; R[i] *= out; }
        }
    }
private:
    static constexpr int kSub = 16;
    static constexpr double kMagDrive = 0.4;   // Chow's drive 0.1 .. 0.5: past that its output corners fold back as aliasing even at 16x
    int P (const char* k) const { auto it = pid.find (k); return it == pid.end() ? 0 : it->second; }
    float uni() noexcept { return (float) (rng() >> 8) / 8388608.0f - 1.0f; }
    // the noise beds (own generators): VINYL, DUST, TAPE HISS, WHITE, PINK, HUM 50, HUM 60, RADIO
    void noiseSample (int type, float& l, float& r) noexcept
    {
        const float w1 = uni(), w2 = uni();
        switch (type)
        {
            case 0: { float rl = w1, rr = w2; rumbleLp.process (rl, rr); float c = 0, cr = 0; if ((rng() & 0xffff) < (unsigned) (12.0 * 192000.0 / sr)) { c = uni() * 3.0f; cr = c * 0.8f; } crackleBp.process (c, cr); l = rl * 2.0f + c; r = rr * 2.0f + cr; break; }
            case 1: { float c = (rng() & 0xfffff) < (unsigned) (900.0 * 192000.0 / sr) ? uni() * 4.0f : 0.0f; l = c; r = (rng() & 1) ? c : -c * 0.7f; break; }
            case 2: { l = w1; r = w2; hissHp.process (l, r); break; }
            case 3: l = w1 * 0.6f; r = w2 * 0.6f; break;
            case 4: { auto& b = pinkB; b[0] = 0.99886f * b[0] + w1 * 0.0555179f; b[1] = 0.99332f * b[1] + w1 * 0.0750759f; b[2] = 0.969f * b[2] + w1 * 0.153852f; b[3] = 0.8665f * b[3] + w1 * 0.3104856f; b[4] = 0.55f * b[4] + w1 * 0.5329522f; b[5] = -0.7616f * b[5] - w1 * 0.016898f;
                      l = (b[0] + b[1] + b[2] + b[3] + b[4] + b[5] + w1 * 0.5362f) * 0.2f; r = l * 0.9f + w2 * 0.05f; break; }
            case 5: case 6: { const double f = type == 5 ? 50.0 : 60.0; humPhase += f / sr; if (humPhase >= 1.0) humPhase -= 1.0; const double ph = juce::MathConstants<double>::twoPi * humPhase;
                              l = r = (float) (0.6 * std::sin (ph) + 0.25 * std::sin (2 * ph) + 0.15 * std::sin (3 * ph)); break; }
            default: { radioPhase += 0.3 / sr; if (radioPhase >= 1.0) radioPhase -= 1.0; l = w1; r = w2; radioBp.process (l, r); const float am = 0.6f + 0.4f * (float) std::sin (juce::MathConstants<double>::twoPi * radioPhase); l *= am * 2.0f; r *= am * 2.0f; break; }
        }
    }
    double sr = 48000.0;
    std::array<HysteresisProcessing, 1> hyst; WowProcess wow; FlutterProcess flutter;
    std::unique_ptr<juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd>> delay; bool wowIdle = true, delayStale = true; int ctl = 1; float lastLen = 0.0f;
    std::array<DegradeNoise, 2> degN; std::array<DegradeFilter, 2> degF;
    Shaper distSh; AwStage tube, console; float padA[2] { 0, 0 };
    Svf bumpF, lossF, toneLo, toneHi, nTone, hissHp, radioBp, spaceLp, rumbleLp, crackleBp; std::array<Svf, 2> magBand;
    std::array<std::vector<float>, 3> diff; std::vector<float> spaceBuf; size_t diffW = 0, spaceW = 0; int spaceTail = 0;
    juce::AudioBuffer<float> dryBuf; AutoGain agTape; EnvelopeFollower envF;
    std::array<float, 6> pinkB {}; double humPhase = 0, radioPhase = 0; float decL = 0, decR = 0, decPhase = 0, act = 0;
    std::minstd_rand rng; std::map<std::string, int> pid;
    static constexpr int kHystSteps = HYST_STEPS; double hystPrev[2] { 0.0, 0.0 };
    int q_bias = 0, q_degrade = 0, q_digi = 0, q_digi_on = 0, q_dist = 0, q_dist_on = 0, q_dtype = 0, q_flutter = 0, q_mag = 0, q_mag_on = 0, q_magic = 0, q_nfollow = 0, q_noise = 0, q_noise_on = 0, q_ntone = 0, q_ntype = 0, q_out = 0, q_space = 0, q_space_on = 0, q_speed = 0, q_tone = 0, q_wobble = 0, q_wobble_on = 0;
};
} // namespace phantom
