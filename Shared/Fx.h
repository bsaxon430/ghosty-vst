#pragma once
#include <juce_dsp/juce_dsp.h>

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
        reset();
    }
    void reset()
    {
        lp.reset(); hp.reset(); chorus.reset(); phaser.reset(); reverb.reset();
        echoL.reset(); echoR.reset(); hauntL.reset(); hauntR.reset();
        fbL = fbR = hL = hR = 0; lfo = 0;
    }

    void process (juce::AudioBuffer<float>& buf, const FxParams& p)
    {
        const int n = buf.getNumSamples();
        auto* L = buf.getWritePointer (0);
        auto* R = buf.getNumChannels() > 1 ? buf.getWritePointer (1) : nullptr;

        // drive + crush
        if (p.drive > 0.01f || p.crush > 0.01f)
        {
            const float k = p.drive * 40.0f, comp = 1.0f / (1.0f + p.drive * 1.3f);
            const float steps = std::pow (2.0f, 8.0f - p.crush * 6.3f);
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
        if (p.echo > 0.01f || p.haunt > 0.01f)
        {
            for (int i = 0; i < n; ++i)
            {
                const float inL = L[i], inR = R ? R[i] : inL;
                float outL = 0, outR = 0;
                if (p.echo > 0.01f)
                {
                    echoL.setDelay (delaySamps); echoR.setDelay (delaySamps);
                    const float dL = echoL.popSample (0), dR = echoR.popSample (0);
                    fbL += 0.35f * (dL - fbL); fbR += 0.35f * (dR - fbR);
                    echoL.pushSample (0, (inL + inR) * 0.5f + fbR * 0.45f);
                    echoR.pushSample (0, fbL * 0.45f);
                    outL += dL * p.echo * 0.6f; outR += dR * p.echo * 0.6f;
                }
                if (p.haunt > 0.01f)
                {
                    lfo += 0.13 / sr; if (lfo > 1.0) lfo -= 1.0;
                    const float wob = (float) (std::sin (lfo * juce::MathConstants<double>::twoPi) * sr * 0.006);
                    hauntL.setDelay (hauntSamps + wob); hauntR.setDelay (hauntSamps * 1.13f - wob);
                    const float a = hauntL.popSample (0), b = hauntR.popSample (0);
                    hL += 0.6f * (a - hL); hR += 0.6f * (b - hR);
                    const float hpA = a - hL * 0.85f, hpB = b - hR * 0.85f;   // keep it airy
                    hauntL.pushSample (0, inL + hpA * 0.62f); hauntR.pushSample (0, inR + hpB * 0.62f);
                    outL += hpA * p.haunt * 0.7f; outR += hpB * p.haunt * 0.7f;
                }
                L[i] = inL + outL; if (R) R[i] = inR + outR;
            }
        }

        if (p.verb > 0.01f)
        {
            juce::dsp::Reverb::Parameters rp;
            rp.roomSize = 0.55f + p.verb * 0.4f; rp.damping = 0.4f; rp.width = 1.0f;
            rp.wetLevel = p.verb * 0.45f; rp.dryLevel = 1.0f - p.verb * 0.25f;
            reverb.setParameters (rp); reverb.process (ctx);
        }
    }

private:
    double sr = 44100.0, lfo = 0;
    juce::dsp::StateVariableTPTFilter<float> lp, hp;
    juce::dsp::Chorus<float> chorus;
    juce::dsp::Phaser<float> phaser;
    juce::dsp::Reverb reverb;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> echoL, echoR, hauntL, hauntR;
    float fbL = 0, fbR = 0, hL = 0, hR = 0;
};
} // namespace ghosty
