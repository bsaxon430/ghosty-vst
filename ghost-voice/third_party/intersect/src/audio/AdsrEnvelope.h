#pragma once
#include <cmath>

class AdsrEnvelope
{
public:
    enum State { Attack, Decay, Sustain, Release, Done };

    // sampleRate must be passed so coefficients are pre-computed; avoids per-sample exp() calls.
    void noteOn (float attackSec, float decaySec, float sustain, float releaseSec, double sr)
    {
        sampleRate   = sr;
        attackCoeff  = makeCoeff (attackSec);
        decayCoeff   = makeCoeff (decaySec);
        sustainLvl   = sustain < 0.0f ? 0.0f : (sustain > 1.0f ? 1.0f : sustain);
        releaseCoeff = makeCoeff (releaseSec);
        state        = Attack;
        level        = 0.0f;
    }

    void noteOff()
    {
        if (state != Done)
            state = Release;
    }

    void forceRelease (float timeSec, double sr)
    {
        sampleRate   = sr;
        releaseCoeff = makeCoeff (timeSec);
        state        = Release;
    }

    float processSample()
    {
        switch (state)
        {
            case Attack:
                // One-pole filter targeting slightly above 1.0 so the curve reaches 1.0
                // within the specified attack time rather than asymptotically approaching it.
                level += attackCoeff * (1.01f - level);
                if (level >= 1.0f)
                {
                    level = 1.0f;
                    state = Decay;
                }
                break;

            case Decay:
                level += decayCoeff * (sustainLvl - level);
                if (level <= sustainLvl + 0.001f)
                {
                    level = sustainLvl;
                    state = Sustain;
                }
                break;

            case Sustain:
                level = sustainLvl;
                break;

            case Release:
                // Multiply by the per-sample decay factor = exp(-5 / (releaseTime * sr))
                level *= (1.0f - releaseCoeff);
                if (level < 0.00015f)  // ≈ -76 dB, treated as inaudible; avoids infinite exponential decay
                {
                    level = 0.0f;
                    state = Done;
                }
                break;

            case Done:
                level = 0.0f;
                break;
        }

        return level;
    }

    bool  isDone()    const { return state == Done; }
    State getState()  const { return state; }
    float getLevel()  const { return level; }

private:
    // One-pole coefficient: after timeSec seconds the envelope is ~99.3% toward its target.
    float makeCoeff (float timeSec) const
    {
        if (timeSec < 0.0001f || sampleRate < 1.0)
            return 1.0f;
        return 1.0f - std::exp (-5.0f / (timeSec * (float) sampleRate));
    }

    State  state        = Done;
    float  level        = 0.0f;
    float  attackCoeff  = 1.0f;
    float  decayCoeff   = 1.0f;
    float  sustainLvl   = 1.0f;
    float  releaseCoeff = 1.0f;
    double sampleRate   = 44100.0;
};
