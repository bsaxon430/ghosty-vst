#pragma once
#include <juce_dsp/juce_dsp.h>
#include "ProVerb.h"

// Master effects shared by both plugins: drive, crush, DJ filter, chorus, phaser, ping echo, reverb, haunt.
namespace ghosty
{
struct FxParams
{
    float drive = 0, crush = 0;
    float filter = 0.5f, res = 0.2f;      // filter < .5 = low-pass, > .5 = high-pass
    float chorus = 0, phaser = 0;
    float echo = 0, verb = 0, haunt = 0;
    double bpm = 120.0;
    bool smooth = false;     // Ghosty Pro (2026-10-24): DRIVE / CRUSH / ECHO / HAUNT glide per sample, REVERB on / off and the CLEAN reverb dry
                             // crossfade instead of stepping (a knob at rest sounds exactly as without it; off = the old per-block steps)
    bool hq = false;         // Ghosty Pro (sound audit 2026-10-26, QUALITY High / Ultra): the DRIVE waveshaper runs 4x oversampled (smooth path only)
    bool clean = false;      // Ghosty Pro CLEAN SOUND: drive / crush level-matched, echo / haunt / reverb wet high-passed and wide (default off)
    // GHOST VERB (round 2): when set, the REVERB knob drives this engine instead of the Freeverb-style one (older sounds keep the old one)
    const ghosty::GhostVerb::Params* ghostVerb = nullptr;
};

class FxChain
{
public:
    void prepare (double sampleRate, int blockSize)
    {
        sr = sampleRate;
        juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) blockSize, 2 };
        lp.prepare (spec); hp.prepare (spec);
        lp.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        hp.setType (juce::dsp::StateVariableTPTFilterType::highpass);
        chorus.prepare (spec); phaser.prepare (spec); reverb.prepare (spec);
        chorus.setRate (0.8f); chorus.setDepth (0.35f); chorus.setCentreDelay (12.0f); chorus.setFeedback (0.1f);
        phaser.setRate (0.3f); phaser.setDepth (0.8f); phaser.setCentreFrequency (900.0f); phaser.setFeedback (0.5f);
        const int maxDelay = (int) (sampleRate * 2.5);
        for (auto* d : { &echoL, &echoR, &hauntL, &hauntR })
        {
            d->setMaximumDelayInSamples (maxDelay);
            d->prepare ({ sampleRate, (juce::uint32) blockSize, 1 });
        }
        wetBuf.setSize (2, juce::jmax (1, blockSize)); dryBuf.setSize (2, juce::jmax (1, blockSize));
        driveOs.initProcessing ((size_t) juce::jmax (1, blockSize)); driveOs.reset();
        kArr.assign ((size_t) juce::jmax (1, blockSize), 0.0f); cArr.assign ((size_t) juce::jmax (1, blockSize), 1.0f);
        smA = 1.0f - (float) std::exp (-1.0 / (0.010 * sampleRate));        // the knob glides: 10 ms (~16 Hz)
        onStep = (float) (1.0 / (0.010 * sampleRate)); verbStep = (float) (1.0 / (0.030 * sampleRate));
        gverb.prepare (sampleRate);
        wetHpA = (float) std::exp (-juce::MathConstants<double>::twoPi * 120.0 / sampleRate);
        matchA = 1.0f - (float) std::exp (-1.0 / (0.2 * sampleRate)); matchGa = 1.0f - (float) std::exp (-1.0 / (0.025 * sampleRate));
        reset();
    }
    void reset()
    {
        driveOs.reset();
        lp.reset(); hp.reset(); chorus.reset(); phaser.reset(); reverb.reset(); gverb.reset(); gverbHold = 0;
        echoL.reset(); echoR.reset(); hauntL.reset(); hauntR.reset();
        fbL = fbR = hL = hR = 0; lfo = 0;
        mIn = mOut = 0; mG = 1; for (auto& v : wetHp) v = 0;
        smFirst = true;
    }

    void process (juce::AudioBuffer<float>& buf, const FxParams& p)
    {
        const int n = buf.getNumSamples();
        auto* L = buf.getWritePointer (0);
        auto* R = buf.getNumChannels() > 1 ? buf.getWritePointer (1) : nullptr;

        const float eT = p.echo > 0.01f ? p.echo : 0.0f, hT = p.haunt > 0.01f ? p.haunt : 0.0f;
        if (p.smooth && smFirst)
        {   // the first block after a reset: everything starts where the knobs are
            smFirst = false;
            dSm = p.drive > 0.01f ? p.drive : 0.01f; cSm = p.crush > 0.01f ? p.crush : 0.01f;
            dW = p.drive > 0.01f ? 1.0f : 0.0f; cW = p.crush > 0.01f ? 1.0f : 0.0f;
            eSm = eT; hSm = hT; vW = p.verb > 0.01f ? 1.0f : 0.0f; vLast = juce::jmax (0.0101f, p.verb); dryWas = -1.0f;
            stepsAt = -1.0f;
        }

        // drive + crush
        if (p.smooth) driveSmooth (L, R, n, p);
        else if (p.drive > 0.01f || p.crush > 0.01f)
        {
            const float k = p.drive * 40.0f, comp = 1.0f / (1.0f + p.drive * 1.3f);
            const float steps = std::pow (2.0f, 8.0f - p.crush * 6.3f);
            if (p.clean && R != nullptr)
            {   // CLEAN: the same drive / crush, then the output follows the input's loudness (slow RMS: tasteful at 100 %, never louder)
                for (int i = 0; i < n; ++i)
                {
                    float x[2] { L[i], R[i] };
                    const float pin = x[0] * x[0] + x[1] * x[1];
                    for (auto& v : x)
                    {
                        if (p.drive > 0.01f) v = (1.0f + k) * v / (1.0f + k * std::abs (v)) * comp;
                        if (p.crush > 0.01f) v = std::round (v * steps) / steps;
                    }
                    const float pout = x[0] * x[0] + x[1] * x[1];
                    if (mIn <= 0.0f && mOut <= 0.0f) { mIn = pin; mOut = pout; }
                    mIn += matchA * (pin - mIn) + 1.0e-24f; mOut += matchA * (pout - mOut) + 1.0e-24f;
                    const float t = juce::jlimit (0.06f, 1.4f, mOut > 1.0e-12f ? std::sqrt (mIn / mOut) : 1.0f);
                    mG += matchGa * (t - mG);
                    L[i] = x[0] * mG; R[i] = x[1] * mG;
                }
            }
            else
            for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            {
                auto* d = buf.getWritePointer (ch);
                for (int i = 0; i < n; ++i)
                {
                    float x = d[i];
                    if (p.drive > 0.01f) x = (1.0f + k) * x / (1.0f + k * std::abs (x)) * comp;
                    if (p.crush > 0.01f) x = std::round (x * steps) / steps;
                    d[i] = x;
                }
            }
        }

        juce::dsp::AudioBlock<float> block (buf);
        juce::dsp::ProcessContextReplacing<float> ctx (block);

        // DJ filter
        const float q = 0.7f + p.res * 8.0f;
        if (p.filter < 0.47f)
        {
            lp.setCutoffFrequency (juce::jmax (110.0f, 20000.0f * std::pow (p.filter / 0.47f, 2.6f)));
            lp.setResonance (q); lp.process (ctx);
        }
        else if (p.filter > 0.53f)
        {
            hp.setCutoffFrequency (20.0f * std::pow (300.0f, (p.filter - 0.53f) / 0.47f));
            hp.setResonance (q); hp.process (ctx);
        }

        if (p.phaser > 0.01f) { phaser.setMix (p.phaser * 0.8f); phaser.process (ctx); }
        if (p.chorus > 0.01f) { chorus.setMix (p.chorus * 0.6f); chorus.setDepth (0.2f + p.chorus * 0.5f); chorus.process (ctx); }

        // echo (dotted eighth, ping-pong) + haunt (dark wobbly long echo)
        const float delaySamps = (float) juce::jlimit (100.0, sr * 2.0, 60.0 / p.bpm * 0.75 * sr);
        const float hauntSamps = (float) (sr * 0.37);
        // smooth: the ECHO / HAUNT lines always run (their knobs are output levels that glide), so turning one on never plays a line
        // that stopped and restarted (that seam was a click); at rest the output is exactly as before
        if (p.smooth || p.echo > 0.01f || p.haunt > 0.01f)
        {
            for (int i = 0; i < n; ++i)
            {
                const float inL = L[i], inR = R ? R[i] : inL;
                float outL = 0, outR = 0;
                float eg = p.echo, hg = p.haunt;
                bool eOn = p.echo > 0.01f, hOn = p.haunt > 0.01f;
                if (p.smooth)
                {   // the levels glide (10 ms); a knob at rest = exactly its value; turned off, the level glides down to 0 first
                    glide (eSm, eT); glide (hSm, hT);
                    eg = eSm; hg = hSm; eOn = hOn = true;
                }
                if (eOn)
                {
                    echoL.setDelay (delaySamps); echoR.setDelay (delaySamps);
                    const float dL = echoL.popSample (0), dR = echoR.popSample (0);
                    fbL += 0.35f * (dL - fbL); fbR += 0.35f * (dR - fbR);
                    echoL.pushSample (0, (inL + inR) * 0.5f + fbR * 0.45f);
                    echoR.pushSample (0, fbL * 0.45f);
                    outL += dL * eg * 0.6f; outR += dR * eg * 0.6f;
                }
                if (hOn)
                {
                    lfo += 0.13 / sr; if (lfo > 1.0) lfo -= 1.0;
                    const float wob = (float) (std::sin (lfo * juce::MathConstants<double>::twoPi) * sr * 0.006);
                    hauntL.setDelay (hauntSamps + wob); hauntR.setDelay (hauntSamps * 1.13f - wob);
                    const float a = hauntL.popSample (0), b = hauntR.popSample (0);
                    hL += 0.6f * (a - hL); hR += 0.6f * (b - hR);
                    const float hpA = a - hL * 0.85f, hpB = b - hR * 0.85f;   // keep it airy
                    hauntL.pushSample (0, inL + hpA * 0.62f); hauntR.pushSample (0, inR + hpB * 0.62f);
                    outL += hpA * hg * 0.7f; outR += hpB * hg * 0.7f;
                }
                if (p.clean && (! p.smooth || eg > 0.0f || hg > 0.0f || wetHp[0] != 0.0f || wetHp[1] != 0.0f || wetHp[2] != 0.0f || wetHp[3] != 0.0f)) wideWet (outL, outR, 0);
                if (p.smooth && outL == 0.0f && outR == 0.0f) continue;
                L[i] = inL + outL; if (R) R[i] = inR + outR;
            }
        }

        if (p.ghostVerb != nullptr)
        {   // GHOST VERB: its own pre-delay, wet lo / hi cut, width, equal-power mix and 30 ms glides (the CLEAN wet shaping is built in);
            // it keeps running while the knob glides down to 0 and while its tail rings out (decay x1.5 + 0.5 s), so a tail never stops or
            // restarts at a seam when the amount is modulated through 0 (ProTests zipper MODAMT -> verb, 2026-10-25)
            if (p.verb > 0.001f) gverbHold = (int) (sr * (std::max (0.1f, p.ghostVerb->decay) * 1.5 + 0.5));
            if (p.verb > 0.001f || gverbHold > 0) { gverb.process (L, R != nullptr ? R : L, n, *p.ghostVerb); if (p.verb <= 0.001f) gverbHold -= n; }
        }
        else if (p.smooth) verbSmooth (L, R, n, p, ctx);
        else if (p.verb > 0.01f)
        {
            juce::dsp::Reverb::Parameters rp;
            rp.roomSize = 0.55f + p.verb * 0.4f; rp.damping = 0.4f; rp.width = 1.0f;
            rp.wetLevel = p.verb * 0.45f; rp.dryLevel = 1.0f - p.verb * 0.25f;
            if (p.clean && R != nullptr && wetBuf.getNumSamples() >= n)
            {   // CLEAN: the reverb's wet alone, high-passed and wide, on top of the centred dry
                const float dry = rp.dryLevel * 2.0f; rp.dryLevel = 0.0f;      // (juce::Reverb scales its dry level by 2)
                reverb.setParameters (rp);
                wetBuf.copyFrom (0, 0, L, n); wetBuf.copyFrom (1, 0, R, n);
                float* ch[2] { wetBuf.getWritePointer (0), wetBuf.getWritePointer (1) };
                juce::dsp::AudioBlock<float> wb (wetBuf); auto wbs = wb.getSubBlock (0, (size_t) n);
                reverb.process (juce::dsp::ProcessContextReplacing<float> (wbs));
                for (int i = 0; i < n; ++i) { float a = ch[0][i], b = ch[1][i]; wideWet (a, b, 1); L[i] = L[i] * dry + a; R[i] = R[i] * dry + b; }
            }
            else { reverb.setParameters (rp); reverb.process (ctx); }
        }
    }

private:
    static inline void glideTo (float& k, float t, float a) noexcept { if (k != t) { k += (t - k) * a; if (std::abs (t - k) < 1.0e-5f) k = t; } }
    inline void glide (float& k, float t) const noexcept { glideTo (k, t, smA); }
    static inline void rampTo (float& w, float t, float st) noexcept { if (w != t) w = t > w ? juce::jmin (t, w + st) : juce::jmax (t, w - st); }

    // Ghosty Pro (smooth): DRIVE / CRUSH per sample. The knob glides (10 ms); crossing 1 % (where the old law switched the stage in or
    // out: +2.8 dB at once for DRIVE) is a 10 ms crossfade with the dry signal, the law held at 1 % meanwhile. A knob at rest computes
    // exactly what the per-block code computes.
    void driveSmooth (float* L, float* R, int n, const FxParams& p) noexcept
    {
        const bool dOn = p.drive > 0.01f, cOn = p.crush > 0.01f;
        if (! dOn && ! cOn && dW <= 0.0f && cW <= 0.0f) return;              // exact bypass
        const float dT = dOn ? p.drive : 0.01f, cT = cOn ? p.crush : 0.01f;
        const int chans = R != nullptr ? 2 : 1;
        if (p.hq && R != nullptr && n <= (int) kArr.size()) { driveSmoothHq (L, R, n, p, dOn, cOn, dT, cT); return; }
        float k = dSm * 40.0f, comp = 1.0f / (1.0f + dSm * 1.3f);
        if (cSm != stepsAt) { stepsAt = cSm; steps = std::pow (2.0f, 8.0f - cSm * 6.3f); }
        for (int i = 0; i < n; ++i)
        {
            if (dSm != dT) { glide (dSm, dT); k = dSm * 40.0f; comp = 1.0f / (1.0f + dSm * 1.3f); }
            if (cSm != cT) { glide (cSm, cT); stepsAt = cSm; steps = std::pow (2.0f, 8.0f - cSm * 6.3f); }
            rampTo (dW, dOn ? 1.0f : 0.0f, onStep); rampTo (cW, cOn ? 1.0f : 0.0f, onStep);
            float x[2] { L[i], R != nullptr ? R[i] : 0.0f };
            const float pin = x[0] * x[0] + x[1] * x[1];
            for (int c = 0; c < chans; ++c)
            {
                float v = x[c];
                if (dW > 0.0f) { const float sh = (1.0f + k) * v / (1.0f + k * std::abs (v)) * comp; v = dW >= 1.0f ? sh : v + dW * (sh - v); }
                if (cW > 0.0f) { const float q = std::round (v * steps) / steps; v = cW >= 1.0f ? q : v + cW * (q - v); }
                x[c] = v;
            }
            if (p.clean && R != nullptr)
            {   // CLEAN: as the per-block code (the output follows the input's loudness)
                const float pout = x[0] * x[0] + x[1] * x[1];
                if (mIn <= 0.0f && mOut <= 0.0f) { mIn = pin; mOut = pout; }
                mIn += matchA * (pin - mIn) + 1.0e-24f; mOut += matchA * (pout - mOut) + 1.0e-24f;
                const float t = juce::jlimit (0.06f, 1.4f, mOut > 1.0e-12f ? std::sqrt (mIn / mOut) : 1.0f);
                mG += matchGa * (t - mG);
                x[0] *= mG; x[1] *= mG;
            }
            L[i] = x[0]; if (R != nullptr) R[i] = x[1];
        }
    }

    // the same DRIVE / CRUSH with the drive's waveshaper 4x oversampled (juce::dsp::Oversampling, two half-band polyphase IIR stages,
    // integer latency) with first-order anti-derivative anti-aliasing: at 1x its 1 / (1 + k |x|) knee folded the harmonics of a C6 back
    // at -41 dB (30 %) .. -30 dB (100 %). The knob glide, the 1 % crossfade, CRUSH (a bit-crusher: aliasing is its sound, kept at 1x) and the
    // CLEAN match are as above. (During the 10 ms on / off crossfade the dry is not delayed by the oversampler's ~2 samples.)
    void driveSmoothHq (float* L, float* R, int n, const FxParams& p, bool dOn, bool cOn, float dT, float cT) noexcept
    {
        for (int i = 0; i < n; ++i)
        {   // the glided drive law per base sample
            if (dSm != dT) glide (dSm, dT);
            kArr[(size_t) i] = dSm * 40.0f; cArr[(size_t) i] = 1.0f / (1.0f + dSm * 1.3f);
        }
        const bool shape = dOn || dW > 0.0f;
        if (shape)
        {
            if (dryBuf.getNumSamples() >= n) { dryBuf.copyFrom (0, 0, L, n); dryBuf.copyFrom (1, 0, R, n); }
            float* ch[2] { L, R };
            juce::dsp::AudioBlock<float> blk (ch, 2, (size_t) n);
            auto up = driveOs.processSamplesUp (blk);
            const int f = (int) up.getNumSamples() / std::max (1, n);
            for (int c = 0; c < 2; ++c)
            {   // first-order anti-derivative anti-aliasing (ADAA) on top of the 4x: y = (F (x) - F (x1)) / (x - x1), F the shaper's integral
                float* u = up.getChannelPointer ((size_t) c);
                double x1 = adaaX[c];
                for (int j = 0; j < (int) up.getNumSamples(); ++j)
                {
                    const int i = std::min (n - 1, j / std::max (1, f)); const double k = std::max (1.0e-4, (double) kArr[(size_t) i]);
                    const double x = u[j];
                    auto F = [k] (double v) { const double a = std::abs (v); return (1.0 + k) / k * (a - std::log1p (k * a) / k); };
                    double y;
                    if (std::abs (x - x1) > 1.0e-5) y = (F (x) - F (x1)) / (x - x1);
                    else { const double m = 0.5 * (x + x1); y = (1.0 + k) * m / (1.0 + k * std::abs (m)); }
                    x1 = x;
                    u[j] = (float) (y * cArr[(size_t) i]);
                }
                adaaX[c] = x1;
            }
            driveOs.processSamplesDown (blk);
        }
        else { driveOs.reset(); adaaX[0] = adaaX[1] = 0.0; }
        if (cSm != stepsAt) { stepsAt = cSm; steps = std::pow (2.0f, 8.0f - cSm * 6.3f); }
        const float* dl = dryBuf.getReadPointer (0); const float* dr = dryBuf.getReadPointer (1);
        for (int i = 0; i < n; ++i)
        {
            if (cSm != cT) { glide (cSm, cT); stepsAt = cSm; steps = std::pow (2.0f, 8.0f - cSm * 6.3f); }
            rampTo (dW, dOn ? 1.0f : 0.0f, onStep); rampTo (cW, cOn ? 1.0f : 0.0f, onStep);
            const float x0[2] { shape ? dl[i] : L[i], shape ? dr[i] : R[i] };
            const float pin = x0[0] * x0[0] + x0[1] * x0[1];
            float x[2] { L[i], R[i] };
            for (int c = 0; c < 2; ++c)
            {
                float v = x[c];
                if (shape && dW < 1.0f) v = x0[c] + dW * (v - x0[c]);
                if (cW > 0.0f) { const float q = std::round (v * steps) / steps; v = cW >= 1.0f ? q : v + cW * (q - v); }
                x[c] = v;
            }
            if (p.clean)
            {
                const float pout = x[0] * x[0] + x[1] * x[1];
                if (mIn <= 0.0f && mOut <= 0.0f) { mIn = pin; mOut = pout; }
                mIn += matchA * (pin - mIn) + 1.0e-24f; mOut += matchA * (pout - mOut) + 1.0e-24f;
                const float t = juce::jlimit (0.06f, 1.4f, mOut > 1.0e-12f ? std::sqrt (mIn / mOut) : 1.0f);
                mG += matchGa * (t - mG);
                x[0] *= mG; x[1] *= mG;
            }
            L[i] = x[0]; R[i] = x[1];
        }
    }

    // Ghosty Pro (smooth): the reverb always runs (so it never restarts from a stale tail), and REVERB switching on / off (the knob
    // crossing 1 %) crossfades with the dry sound over 30 ms: juce::Reverb doubles the dry level, so the switch was a 6 dB step. While on,
    // its gains glide inside juce::Reverb (10 ms); the CLEAN dry gain ramps across the block. A knob at rest = exactly the old output.
    void verbSmooth (float* L, float* R, int n, const FxParams& p, juce::dsp::ProcessContextReplacing<float>& ctx)
    {
        const bool on = p.verb > 0.01f;
        if (on) vLast = p.verb;
        const float v = vLast;
        const bool fade = ! (on && vW >= 1.0f) && dryBuf.getNumSamples() >= n;
        if (fade) { dryBuf.copyFrom (0, 0, L, n); if (R != nullptr) dryBuf.copyFrom (1, 0, R, n); }
        juce::dsp::Reverb::Parameters rp;
        rp.roomSize = 0.55f + v * 0.4f; rp.damping = 0.4f; rp.width = 1.0f;
        rp.wetLevel = v * 0.45f; rp.dryLevel = 1.0f - v * 0.25f;
        if (p.clean && R != nullptr && wetBuf.getNumSamples() >= n)
        {
            const float dry = rp.dryLevel * 2.0f; rp.dryLevel = 0.0f;
            reverb.setParameters (rp);
            wetBuf.copyFrom (0, 0, L, n); wetBuf.copyFrom (1, 0, R, n);
            float* ch[2] { wetBuf.getWritePointer (0), wetBuf.getWritePointer (1) };
            juce::dsp::AudioBlock<float> wb (wetBuf); auto wbs = wb.getSubBlock (0, (size_t) n);
            reverb.process (juce::dsp::ProcessContextReplacing<float> (wbs));
            if (dryWas >= 0.0f && dryWas != dry)
            {
                const float st = (dry - dryWas) / (float) n; float dg = dryWas;
                for (int i = 0; i < n; ++i) { dg += st; if (i == n - 1) dg = dry; float a = ch[0][i], b = ch[1][i]; wideWet (a, b, 1); L[i] = L[i] * dg + a; R[i] = R[i] * dg + b; }
            }
            else
                for (int i = 0; i < n; ++i) { float a = ch[0][i], b = ch[1][i]; wideWet (a, b, 1); L[i] = L[i] * dry + a; R[i] = R[i] * dry + b; }
            dryWas = dry;
        }
        else { reverb.setParameters (rp); reverb.process (ctx); dryWas = -1.0f; }
        if (fade)
        {
            const float tgt = on ? 1.0f : 0.0f;
            const float* dl = dryBuf.getReadPointer (0); const float* dr = R != nullptr ? dryBuf.getReadPointer (1) : nullptr;
            for (int i = 0; i < n; ++i)
            {
                rampTo (vW, tgt, verbStep);
                if (vW <= 0.0f) { L[i] = dl[i]; if (R != nullptr) R[i] = dr[i]; }
                else if (vW < 1.0f) { L[i] = dl[i] + vW * (L[i] - dl[i]); if (R != nullptr) R[i] = dr[i] + vW * (R[i] - dr[i]); }
            }
        }
        else vW = on ? 1.0f : 0.0f;
    }

    // CLEAN: a wet signal loses its lows (2 x one-pole high-pass ~120 Hz) and its mid gives way to its side
    inline void wideWet (float& l, float& r, int slot) noexcept
    {
        float* z = wetHp + slot * 4;
        z[0] = wetHpA * z[0] + (1.0f - wetHpA) * l; l -= z[0]; z[1] = wetHpA * z[1] + (1.0f - wetHpA) * l; l -= z[1];
        z[2] = wetHpA * z[2] + (1.0f - wetHpA) * r; r -= z[2]; z[3] = wetHpA * z[3] + (1.0f - wetHpA) * r; r -= z[3];
        const float m = 0.5f * (l + r) * 0.9f, s = 0.5f * (l - r) * 1.25f;
        l = m + s; r = m - s;
    }
    juce::AudioBuffer<float> wetBuf; float wetHp[8] {}, wetHpA = 0.98f, matchA = 0.0001f, matchGa = 0.001f, mIn = 0, mOut = 0, mG = 1;
    double sr = 44100.0, lfo = 0;
    juce::dsp::StateVariableTPTFilter<float> lp, hp;
    juce::dsp::Chorus<float> chorus;
    juce::dsp::Oversampling<float> driveOs { 2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true };
    std::vector<float> kArr, cArr; double adaaX[2] {};
    juce::dsp::Phaser<float> phaser;
    juce::dsp::Reverb reverb;
    ghosty::GhostVerb gverb; int gverbHold = 0;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> echoL, echoR, hauntL, hauntR;
    float fbL = 0, fbR = 0, hL = 0, hR = 0;
    // smooth (Ghosty Pro)
    juce::AudioBuffer<float> dryBuf;
    bool smFirst = true; float smA = 0.002f, onStep = 0.002f, verbStep = 0.0007f;
    float dSm = 0.01f, cSm = 0.01f, dW = 0, cW = 0, eSm = 0, hSm = 0, vW = 0, vLast = 0.0101f, dryWas = -1.0f, steps = 256.0f, stepsAt = -1.0f;
};
} // namespace ghosty
