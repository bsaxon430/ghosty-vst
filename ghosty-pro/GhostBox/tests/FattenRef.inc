// FATTEN as it was before the modes were published (copied from commit 85ce04d, test-only): the reference that keeps
// CLASSIC bit-identical to the old build. Never change this file.
// ================================================================ FATTEN
// MODE (CLASSIC = the original unit, bit-identical): what the FAT knob adds on top of the low-shelf + glue + drive chain
class FattenUnitRef : public UnitDsp
{
public:
    Telemetry* tm = nullptr;
    void attach (juce::AudioProcessorValueTreeState& s) override
    {
        fat.bind (s, pid (uFatten, "fat"), 0.4f); color.bind (s, pid (uFatten, "color"), 0); crunch.bind (s, pid (uFatten, "crunch"), 0.2f); out.bind (s, pid (uFatten, "out"), 0);
    }
    void prepare (double sr_, int) override
    {
        sr = sr_; fatSm.setTime (sr, 60.0f); crSm.setTime (sr, 60.0f); outSm.setTime (sr, 30.0f);
        det.set (sr, 8.0f, 140.0f); ag.prepare (sr, 350.0f);
        for (auto& t : tilt) t.prepare (sr, 1000.0);
        for (auto& d : dc) d.prepare (sr);
        subLp1.setLP (sr, 120.0); subLp2.setLP (sr, 120.0); subOutLp1.setLP (sr, 90.0); subOutLp2.setLP (sr, 90.0); subEnv.set (sr, 3.0f, 90.0f);
        bodyC = rbj (rbjBell, sr, 140.0, 1.1, 9.0); bodyShC = rbj (rbjLowShelf, sr, 90.0, 0.7, 4.0);
        smashEnv.set (sr, 0.5f, 60.0f); trFast.set (sr, 0.3f, 25.0f); trSlow.set (sr, 15.0f, 250.0f);
        for (auto& r : stackRing) r.prepare ((int) (sr * 0.03) + 16);
        modeSm.setTime (sr, 40.0f);
        reset();
    }
    void reset() override
    {
        fatSm.snap (fat()); crSm.snap (crunch()); outSm.snap (dbToGain (out())); det.reset(); ag.reset();
        subLp1.reset(); subLp2.reset(); subOutLp1.reset(); subOutLp2.reset(); subEnv.reset(); subFlip = 1.0f; subArmed = true;
        for (auto& b : bodyB) b.reset();
        for (auto& b : bodySh) b.reset();
        smashEnv.reset(); trFast.reset(); trSlow.reset(); for (auto& r : stackRing) r.clear(); stackPh = 0; modeSm.snap (0.0f);
        for (auto& b : lowSh) b.reset();
        for (auto& b : midB) b.reset();
        for (auto& t : tilt) t.reset();
        for (auto& d : dc) d.reset();
        lastF = -1;
    }
    void process (float* L, float* R, int n, const BlockCtx&) override
    {
        fatSm.target = clampf (fat(), 0.0f, 1.0f); crSm.target = clampf (crunch(), 0.0f, 1.0f); outSm.target = dbToGain (out());
        if (std::abs (fatSm.cur - lastF) > 0.002f)
        {
            lastF = fatSm.cur;
            lowC = rbj (rbjLowShelf, sr, 110.0, 0.7, lastF * 7.0);
            midC = rbj (rbjBell, sr, 320.0, 0.8, lastF * 3.5);
        }
        for (auto& t : tilt) t.set (color());
        float pk = 0, grMax = 0;
        const int md = 0;
        modeSm.target = md == 0 ? 0.0f : 1.0f;   // a mode fades in / out (no click when switching to / from CLASSIC)
        const bool modes = md != 0 || modeSm.cur > 1.0e-5f;
        const bool mSub = md == 1 || md == 7, mBody = md == 2 || md == 7, mSmash = md == 3 || md == 7,
                   mSat = md == 4 || md == 7, mStack = md == 5, mTr = md == 6;
        for (int i = 0; i < n; ++i)
        {
            const float f = fatSm.next(), cr = crSm.next(), og = outSm.next();
            float l = L[i], r = R[i];
            float pre[2] { l, r }, wm = 0;
            if (modes)
            {
                wm = modeSm.next() * f;   // the mode's amount: FAT x the mode fade
                const float mono = 0.5f * (l + r);
                if (mBody)
                {   // BODY: a resonant 140 Hz thump + low shelf, scaled by FAT
                    for (int c = 0; c < 2; ++c) { float& x = c == 0 ? l : r; const float y = bodySh[c].process (bodyB[c].process (x, bodyC), bodyShC); x += wm * (y - x); }
                }
                if (mSub || mBody)
                {   // SUB / BODY / SAUSAGE weight: the low band (below ~120 Hz, mono) driven into a soft clip and added back: more low
                    // energy at the same peak (the bottom stays mono; the octave-down layer below adds the sub itself)
                    const float lo = subLp2.lp (subLp1.lp (mono));
                    const float drive = 1.0f + 10.0f * wm, heavy = fastTanh (lo * drive) * (mSub ? 0.9f : 0.6f) * wm;
                    l += heavy; r += heavy;
                    if (mSub)
                    {   // the octave below the low band (flip-flop on its zero crossings), shaped by its level, low-passed
                        const float e = subEnv.process (lo);
                        if (subArmed && lo > 0.1f * e) { subFlip = -subFlip; subArmed = false; } else if (lo < -0.1f * e) subArmed = true;
                        const float sub = subOutLp2.lp (subOutLp1.lp (subFlip * e)) * 1.6f * wm;
                        l += sub; r += sub;
                    }
                }
                if (mTr)
                {   // TRANSIENT: attack down, sustain up = a fatter tail
                    const float a = std::abs (mono) + 1.0e-9f, fe = trFast.process (a), se = trSlow.process (a);
                    const float gTr = juce::jlimit (0.25f, 4.0f, std::pow ((se + 1.0e-6f) / (fe + 1.0e-6f), 1.5f * wm));
                    l *= gTr; r *= gTr;
                }
                if (mStack)
                {   // STACK: two micro-delayed, slowly moving copies (thick, chorus-like)
                    stackRing[0].push (l); stackRing[1].push (r);
                    stackPh += 0.3 / sr; if (stackPh >= 1.0) stackPh -= 1.0;
                    const double m1 = (0.007 + 0.0015 * std::sin (6.2831853 * stackPh)) * sr, m2 = (0.013 + 0.002 * std::sin (6.2831853 * (stackPh + 0.37))) * sr;
                    const float a1 = stackRing[1].readLinear (m1), a2 = stackRing[0].readLinear (m2);
                    l += 0.55f * wm * (a1 + 0.6f * a2); r += 0.55f * wm * (a2 + 0.6f * a1);
                }
                pre[0] = l; pre[1] = r;
            }
            ag.feedIn (L[i], R[i]);
            // low / low-mid thickening
            l = midB[0].process (lowSh[0].process (l, lowC), midC); r = midB[1].process (lowSh[1].process (r, lowC), midC);
            // glue compression (soft knee)
            const float e = det.process (0.5f * (std::abs (l) + std::abs (r)) + 1.0e-9f);
            const float lv = gainToDb (e), thr = -10.0f - 16.0f * f, ratio = 1.0f + 5.0f * f, knee = 6.0f;
            float over = lv - thr, gr = 0;
            if (over > knee * 0.5f) gr = over * (1.0f / ratio - 1.0f);
            else if (over > -knee * 0.5f) { const float x = over + knee * 0.5f; gr = (1.0f / ratio - 1.0f) * x * x / (2.0f * knee); }
            grMax = std::min (grMax, gr);
            const float cg = dbToGain (gr);
            l *= cg; r *= cg;
            // drive + saturation (smooth -> crunchy asymmetric edges)
            const float g = dbToGain (f * 18.0f + cr * 14.0f);
            for (int c = 0; c < 2; ++c)
            {
                float& x = c == 0 ? l : r;
                const float d = x * g;
                const float smooth = fastTanh (d);
                const float hard = d > 0 ? clampf (d * 1.25f, 0.0f, 0.92f) : -clampf (-d * 0.85f, 0.0f, 0.7f) + 0.06f * fastTanh (d * d);
                x = dc[c].process (smooth + cr * (hard - smooth));
                x = fastTanh (x * 1.1f) * 0.95f;   // final soft clip
                x = tilt[c].process (x);
            }
            if (modes && wm > 0.0f)
            {
                if (mSmash)
                {   // SMASH: parallel heavy compression (fast attack, 8:1 from -30 dB, make-up) blended in by FAT
                    const float e2 = smashEnv.process (0.5f * (std::abs (pre[0]) + std::abs (pre[1])) + 1.0e-9f);
                    const float lv2 = gainToDb (e2), ov = lv2 + 30.0f, g2 = dbToGain ((ov > 0 ? ov * (1.0f / 8.0f - 1.0f) : 0.0f) + 14.0f);
                    l += wm * 0.8f * fastTanh (pre[0] * g2); r += wm * 0.8f * fastTanh (pre[1] * g2);
                }
                if (mSat)
                {   // SATURATE: a parallel tape / tube drive (even + odd), level compensated
                    const float gd = dbToGain (6.0f + 18.0f * wm);
                    for (int c = 0; c < 2; ++c) { float& x = c == 0 ? l : r; const float d = pre[c] * gd; x += wm * 0.7f * (fastTanh (d + 0.15f * d * d) - 0.15f * fastTanh (0.15f * d * d)) / std::sqrt (gd) * 2.0f; }
                }
                l = fastTanh (l * 0.95f); r = fastTanh (r * 0.95f);   // the sum stays under full scale
            }
            const float a = ag.next (l, r) * og;
            L[i] = l * a; R[i] = r * a;
            pk = std::max (pk, std::abs (L[i]));
        }
        if (tm) { tm->fatAmount.store (fatSm.cur); if (grMax < tm->fatGr.load()) tm->fatGr.store (grMax); tm->fatLevel.store (pk); }   // GR held until the editor reads it
    }
private:
    P fat, color, crunch, out, mode; double sr = 48000; ExpSmooth fatSm, crSm, outSm, modeSm; EnvFollow det; AutoGain ag;
    Biquad lowSh[2], midB[2]; BiquadCoefs lowC, midC; Tilt tilt[2]; DcBlock dc[2]; float lastF = -1;
    OnePole subLp1, subLp2, subOutLp1, subOutLp2; EnvFollow subEnv, smashEnv, trFast, trSlow; float subFlip = 1.0f; bool subArmed = true;
    Biquad bodyB[2], bodySh[2]; BiquadCoefs bodyC, bodyShC; std::array<Ring, 2> stackRing; double stackPh = 0;
};

