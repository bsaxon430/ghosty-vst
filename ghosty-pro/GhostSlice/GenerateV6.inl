// GHOST SLICE build 6 generators (engine 6). Included inside Generate.cpp's private namespace: it shares Ctx and the helpers there.
// INSTRUMENT and PAD are made from the vocoder (Vocoder.h): the voice analysed once, then played back as clean harmonics + breath at
// any pitch. Older recipes (engine 5) never come here: they render with the code above, exactly as before.

using voc::Frame; using voc::Track;

inline double vfIdx (double wf) { return wf / (kRate * voc::kFrameS); }        // working frame -> vocoder frame

// one sung syllable of the sound (working frames): consonant [s0, v0), vowel [v0, v1), coda [v1, s1)
struct Syl { double s0 = 0, v0 = 0, v1 = 0, s1 = 0; double f0 = 0; };

// the syllables a take sings, in the order they were said (the user's parts when there are any). Breaths, clicks and silences are
// never syllables; at most maxN, a window that moves with the take's seed (each GENERATE sings different words of a long sound).
std::vector<Syl> syllablesFor (Ctx& c, int maxN)
{
    std::vector<Syl> out;
    const auto& A = *c.P.analysis; const double ssr = c.P.src->sampleRate;
    auto add = [&] (int64 s0, int64 v0, int64 v1, int64 s1)
    {
        if (v1 - v0 < (int64) (0.04 * ssr)) return;
        const auto st = statsOf (A.pitch, v0, v1);
        if (st.voiced < 0.45 || st.f0 <= 0) return;
        Syl y; y.s0 = c.wf (s0); y.v0 = c.wf (v0); y.v1 = c.wf (v1); y.s1 = c.wf (juce::jmin (s1, v1 + (int64) (0.15 * ssr))); y.f0 = st.f0;
        y.s0 = juce::jmax (y.s0, y.v0 - 0.25 * c.sr);   // (a long run of consonants: its last quarter second)
        out.push_back (y);
    };
    if (! c.manual.empty())
    {
        for (auto& m : c.manual)
        {   // a part: the syllables inside it, or the part itself as one syllable (its vowel = the voiced middle)
            const size_t before = out.size();
            for (auto& s : A.syllables) if (s.start >= m.s0 - (int64) (0.02 * ssr) && s.end <= m.s1 + (int64) (0.02 * ssr)) add (s.start, s.vowelStart, s.vowelEnd, s.end);
            if (out.size() == before) add (m.s0, m.v1 > m.v0 ? m.v0 : m.s0, m.v1 > m.v0 ? m.v1 : m.s1, m.s1);
        }
    }
    else for (auto& s : A.syllables) add (s.start, s.vowelStart, s.vowelEnd, s.end);
    if ((int) out.size() > maxN)
    {
        const int start = c.rs.pick ((int) out.size() - maxN + 1);
        out = std::vector<Syl> (out.begin() + start, out.begin() + start + maxN);
    }
    return out;
}

// the instrument's timbre: the log-mean envelope / aperiodicity over the vowel's stable core (the held, smooth "instrument" vowel)
Frame timbreOf (const voc::Frames& F, double c0, double c1)
{
    auto t = F.average ((int) std::floor (vfIdx (c0)), (int) std::ceil (vfIdx (c1)) + 1);
    return t;
}

struct SingNote { double t0 = 0, t1 = 0; double midi = 60; float vel = 0.8f; int syl = 0; double glideS = -1; };   // glideS >= 0: this note's own glide in (build 8 GLIDE)
struct SingSpec
{
    double voice = 0.0;             // 0 = the syllables as said .. 1 = the held instrument tone
    Frame inst;                     // the instrument timbre
    double instEvolveA = 0, instEvolveB = 0;   // vocoder frames of the core the instrument tone drifts through (slowly)
    double vibCents = 0, vibHz = 5.2, vibDelayS = 0.35;
    double glideS = 0.05;           // legato notes glide into each other
    double ownPitch = 0.6;          // how much of the voice's own pitch movement stays (scaled by 1 - voice)
    double formantRatio = 1.0;
    double breath = 0.35;           // 0 = clean harmonics .. 1 = the voice's own breath (and a little more air)
    bool hold = false;              // a key zone: the vowel at its own speed, then held (slowly moving) until the end
    double holdPeriodS = 2.0;       // the held part's slow movement (one cycle per loop)
    uint32_t seed = 1;
};

// sing notes with the sound's syllables (the core of VOICE <-> INSTRUMENT). Consonants stay as said (time 1:1), only the vowel is
// stretched to the note; every frame's timbre morphs toward the instrument by `voice`; unvoiced consonants are spliced from the
// original recording (level-matched, faded by `voice`), so words stay crisp. Returns mono at kRate.
juce::AudioBuffer<float> sing (const Ctx& c, const voc::Frames& F, const std::vector<Syl>& syl, const std::vector<SingNote>& notes, double seconds, const SingSpec& sp)
{
    const double h = voc::kFrameS; const int nF = juce::jmax (1, (int) std::ceil (seconds / h));
    const double v = juce::jlimit (0.0, 1.0, sp.voice);
    const double consW = std::pow (1.0 - v, 0.8);      // how much consonant / coda is there
    Track T ((size_t) nF); for (auto& f : T) { f = sp.inst; f.f0 = 0; f.gain = 0; }
    struct Splice { int outAt = 0; double srcA = 0, srcB = 0; double w = 0; };
    std::vector<Splice> splices;
    double prevHz = 0; int prevEnd = -100;
    for (size_t ni = 0; ni < notes.size(); ++ni)
    {
        const auto& N = notes[ni];
        if (syl.empty()) break;
        const Syl& S = syl[(size_t) (N.syl % (int) syl.size())];
        const int i0 = (int) std::lround (N.t0 / h), i1 = juce::jmax (i0 + 2, (int) std::lround (N.t1 / h));
        const int nextStart = ni + 1 < notes.size() ? (int) std::lround (notes[ni + 1].t0 / h) : nF;
        const int rel = 14, iEnd = juce::jmin (nF, juce::jmin (nextStart, i1 + rel));
        const double a0 = vfIdx (S.s0), av0 = vfIdx (S.v0), av1 = vfIdx (S.v1), a1 = vfIdx (S.s1);
        double cl = consW > 0.02 ? av0 - a0 : 0.0, cd = consW > 0.02 && ! sp.hold ? juce::jmin (a1 - av1, 24.0) : 0.0;
        const double vow = juce::jmax (2.0, av1 - av0);
        const int avail = i1 - i0;
        if (cl + cd > 0.6 * avail) { const double k = 0.6 * avail / (cl + cd); cl *= k; cd *= k; }
        const double hz = dsp::midiToHz (N.midi);
        const bool legato = i0 - prevEnd <= 3 && prevHz > 0;
        double ownSm = 0; bool ownInit = false;
        int unvA = -1, unvB = -1;   // the unvoiced part of the consonant (out frames) for the splice
        for (int j = i0; j < iEnd; ++j)
        {
            const double p = j - i0;
            double src; int part;   // 0 consonant, 1 vowel, 2 coda / release
            if (p < cl) { src = av0 - cl + p; part = 0; }
            else if (sp.hold)
            {
                const double q = p - cl, firstPart = 0.8 * vow;
                if (q < firstPart) src = av0 + q;
                else
                {   // held: slowly back and forth over the vowel's second half
                    const double ph = (q - firstPart) * h / juce::jmax (0.5, sp.holdPeriodS);
                    src = av0 + vow * (0.5 + 0.3 * (0.5 + 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * ph)));
                }
                part = 1;
            }
            else if (p < avail - cd) { const double u = (p - cl) / juce::jmax (1.0, avail - cl - cd); src = av0 + u * vow; part = 1; }
            else { src = av1 + (p - (avail - cd)); part = 2; }
            Frame fr = F.at (src);
            const bool srcVoiced = fr.f0 > 0;
            const double ownC = srcVoiced && S.f0 > 0 ? juce::jlimit (-150.0, 150.0, 1200.0 * std::log2 (fr.f0 / S.f0)) : 0.0;
            if (! ownInit) { ownSm = ownC; ownInit = true; } else ownSm += (ownC - ownSm) * 0.25;
            // the timbre: toward the instrument (the vowel fully by `voice`, the consonants half as much so they stay readable)
            Frame inst = sp.inst;
            if (sp.instEvolveB > sp.instEvolveA + 2)
            {   // the instrument tone breathes: it drifts slowly through the core's frames (weight 0.3)
                const double ph = (j * h) / juce::jmax (0.5, sp.holdPeriodS);
                const double at = sp.instEvolveA + (sp.instEvolveB - sp.instEvolveA) * (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * ph));
                voc::blend (inst, F.at (at), 0.3);
            }
            voc::blend (fr, inst, part == 1 ? v : 0.5 * v);
            // the pitch: the note (gliding in when legato), the voice's own movement (less as `voice` rises), the vibrato
            double cents = ownSm * sp.ownPitch * (1.0 - v);
            const double tn = p * h;
            if (sp.vibCents > 0) cents += sp.vibCents * juce::jlimit (0.0, 1.0, (tn - sp.vibDelayS) / 0.5) * std::sin (2.0 * juce::MathConstants<double>::pi * sp.vibHz * tn);
            double target = hz;
            if (legato && sp.glideS > 0) target = hz * std::pow (prevHz / hz, std::exp (-tn / sp.glideS));
            fr.f0 = (part != 1 && ! srcVoiced) ? 0.0 : target * std::exp2 (cents / 1200.0);
            // the level: the note's velocity, a quick attack (or none: legato), the release after the note, the consonants by consW
            double g = 0.35 + 0.65 * N.vel;
            if (! legato) g *= juce::jlimit (0.0, 1.0, (p + 1) / 2.0);
            if (j >= i1) g *= std::pow (1.0 - (double) (j - i1) / rel, 2.0);
            if (part != 1) g *= consW;
            fr.gain *= (float) g;
            T[(size_t) j] = fr;
            if (part == 0 && ! srcVoiced) { if (unvA < 0) unvA = j; unvB = j + 1; }
        }
        if (unvA >= 0 && consW > 0.02)
        {
            const double srcA = vfIdx (S.s0) + (av0 - vfIdx (S.s0) - cl) + (unvA - i0);
            splices.push_back ({ unvA, srcA, srcA + (unvB - unvA), consW });
        }
        prevHz = hz; prevEnd = i1;
    }
    voc::SynthSpec ss; ss.formantRatio = sp.formantRatio; ss.seed = sp.seed;
    ss.breathFloor = 0.01 + 0.05 * sp.breath; ss.breathCeil = 0.12 + 0.88 * sp.breath; ss.noise = 0.35 + 0.65 * sp.breath;
    ss.airHz = 2400.0 * (1.0 - 0.5 * sp.breath);     // (voiced: the breath lives up high, as air; low-band noise is grit)
    ss.topHz = 17000.0; ss.tiltDbPerOct = -0.4 * v;   // (the instrument end: a touch softer on top)
    auto y = voc::synth (T, kRate, ss);
    juce::AudioBuffer<float> out (1, (int) y.size()); out.copyFrom (0, 0, y.data(), (int) y.size());
    // the consonants as said: the original audio over the unvoiced part, level-matched to what the synth made there
    if (std::abs (sp.formantRatio - 1.0) < 1e-6)
        for (auto& sx : splices)
        {
            const int hop = (int) std::lround (kRate * h);
            const int o0 = sx.outAt * hop, len = (int) std::lround ((sx.srcB - sx.srcA) * hop);
            const double w0 = sx.srcA * hop;
            if (len < 64 || o0 + len > out.getNumSamples()) continue;
            juce::AudioBuffer<float> orig (1, len);
            for (int i = 0; i < len; ++i) { double s = 0; for (int ch = 0; ch < c.W().getNumChannels(); ++ch) s += readAt (c.W(), ch, w0 + i); orig.setSample (0, i, (float) (s / c.W().getNumChannels())); }
            const double ro = rmsOf (orig), rs = rmsOf (out, o0, o0 + len);
            const float g = (float) juce::jlimit (0.25, 4.0, ro > 1e-7 && rs > 1e-7 ? rs / ro : 1.0);
            const int ramp = juce::jmin (len / 3, (int) (0.006 * kRate));
            for (int i = 0; i < len; ++i)
            {
                double w = sx.w; if (i < ramp) w *= (double) i / ramp; else if (i >= len - ramp) w *= (double) (len - 1 - i) / ramp;
                float* d = out.getWritePointer (0) + o0 + i;
                *d = (float) ((1.0 - w) * *d + w * g * orig.getSample (0, i));
            }
        }
    return out;
}

// ---------------------------------------------------------------- VOICE <-> INSTRUMENT on the REAL recording (VoiceMorph.h)
// One synthesis, never two mixed: at VOICE 0 with no pitch change the syllable is the original samples (bit for bit, times the
// note's velocity); any other setting plays the syllable's phase-preserving harmonic model + its own residual, pitched
// shape-invariantly (formants kept), morphing toward the instrument as VOICE rises. Returns mono at kRate.
struct SylModel { std::shared_ptr<const vm::Model> m; int64 a = 0; };   // a = the segment's first working frame
// the syllables' models, made before any parallel rendering (each analysed once)
void buildModels (const Ctx& c, const std::vector<Syl>& syl, std::vector<SylModel>& models)
{
    const double q = c.r.deepOr (Type::Instrument, 5); const int periods = q < 1.0 / 3.0 ? 3 : q < 2.0 / 3.0 ? 4 : 6;   // QUALITY
    models.resize (syl.size());
    const auto& W = c.W(); const int wn = W.getNumSamples();
    forEachZone ((int) syl.size(), true, [&] (int si)
    {
        const Syl& S = syl[(size_t) si];
        const int64 a = (int64) std::floor (S.s0), b = juce::jmin ((int64) wn, (int64) std::ceil (S.s1));
        if (b - a < 64 || models[(size_t) si].m != nullptr) return;
        std::vector<float> x ((size_t) (b - a));
        for (int64 i = a; i < b; ++i) { double v = 0; for (int ch = 0; ch < W.getNumChannels(); ++ch) v += W.getSample (ch, (int) i); x[(size_t) (i - a)] = (float) (v / W.getNumChannels()); }
        models[(size_t) si].a = a;
        models[(size_t) si].m = std::make_shared<vm::Model> (vm::analyse (x.data(), (int) x.size(), c.sr, [&] (double sIdx) { return c.P.f0AtWork ((double) a + sIdx); }, periods));
    });
}
juce::AudioBuffer<float> singReal (const Ctx& c, const voc::Frames& F, const std::vector<Syl>& syl, const std::vector<SingNote>& notes, double seconds, const SingSpec& sp,
                                   const std::vector<SylModel>& models)
{
    const int nOutAll = (int) std::ceil (seconds * c.sr) + vm::kHop;
    juce::AudioBuffer<float> out (1, nOutAll); out.clear();
    if (syl.empty()) return out;
    const double v = juce::jlimit (0.0, 1.0, sp.voice), consW = std::pow (1.0 - v, 0.8);
    const auto& W = c.W(); const int wn = W.getNumSamples();
    auto mono = [&] (int64 i) { double s = 0; for (int ch = 0; ch < W.getNumChannels(); ++ch) s += W.getSample (ch, (int) i); return (float) (s / W.getNumChannels()); };
    // how much of the melody's pitch the syllables take: all of it when MUSICAL, less toward NATURAL (where they keep their own)
    const double shiftW = c.m() >= 0.5 ? 1.0 : 2.0 * c.m();
    double prevHz = 0; int prevEnd = -1000;
    for (size_t ni = 0; ni < notes.size(); ++ni)
    {
        const auto& N = notes[ni]; const int si = N.syl % (int) syl.size(); const Syl& S = syl[(size_t) si];
        const int64 a = (int64) std::floor (S.s0), b = juce::jmin ((int64) wn, (int64) std::ceil (S.s1));
        if (b - a < 64) continue;
        const double ownMidi = S.f0 > 0 ? dsp::hzToMidi (S.f0) : N.midi;
        const double semis = shiftW * (N.midi - ownMidi);
        const int o0 = (int) std::lround (N.t0 * c.sr), noteLen = juce::jmax (1, (int) std::lround ((N.t1 - N.t0) * c.sr));
        const int rel = (int) (0.07 * c.sr);
        const float g = (float) (sp.hold ? 1.0 : 0.35 + 0.65 * N.vel);
        // ---- the exact path: the recording itself
        // (build 11 (engine 9): every move up to three octaves, so there is no switch-over between two ways of re-pitching anywhere a
        //  melody goes; measured on the public-domain voice over 3 keys x 3 seeds: 13-19 semitones worst 0.757 (the model) -> 0.959, mean
        //  0.920 -> 0.990; 20-29 semitones mean 0.881 -> 0.986, 68 of 69 syllables >= 0.9, the one left (0.752) a note at the top of
        //  the roll three octaves above its spoken syllable)
        const double kPsolaMaxSemis = c.r.engine >= 9 ? 36.0 : c.r.engine >= 8 ? 12.0 : 7.0;   // (build 9: up to a fifth; build 10 (engine 8): up to an octave, see below)
        if (v < 0.005 && std::abs (semis) < 0.01 && std::abs (sp.formantRatio - 1.0) < 1e-9 && ! sp.hold)
        {
            const int len = juce::jmin ((int) (b - a), noteLen + rel);
            const int fade = len < (int) (b - a) ? juce::jmin (rel, len / 2) : juce::jmin (len / 4, (int) (0.01 * c.sr));   // (a cut syllable fades with the note; a whole one gets 10 ms)
            const int fin = juce::jmin (len / 4, (int) (0.002 * c.sr));                                                    // (2 ms in: a syllable cut out of running speech)
            float* d = out.getWritePointer (0);
            for (int i = 0; i < len && o0 + i < nOutAll; ++i)
            {
                float x = mono (a + i) * g;
                if (i < fin) x *= (float) i / (float) fin;
                if (fade > 0 && i >= len - fade) x *= (float) (len - i) / (float) fade;
                d[o0 + i] += x;
            }
            prevHz = dsp::midiToHz (ownMidi + semis); prevEnd = o0 + len;
            continue;
        }
        // ---- build 9 (engine 7): VOICE at (almost) 0 with a small re-pitch: TD-PSOLA on the syllable's OWN epochs, IN ORDER (each
        // output period takes the source period at the same time, so nothing is re-ordered: no buzz), 2-period Hann grains re-spaced at
        // the new pitch. The vowel's envelope (its formants) is the recording's, the consonants and breaths are the recording itself.
        // A formant guard (envelopeGuard) then lifts each frame's smoothed spectrum to the recording's at the same moment. Measured on
        // the public-domain voice (VOICE 0, MUSICAL, 6 syllables): worst syllable-envelope correlation 0.922 (build 8's model) -> 0.963,
        // and the periodicity (HNR) stays within 2 dB of the recording where the model lost up to 6 dB. Up to a fifth (7 semitones);
        // bigger moves and VOICE above 5 % go to the model below (one synthesis per note and setting: never two mixed).
        if (c.r.engine >= 7 && ! sp.hold && v <= 0.05 && std::abs (semis) >= 0.01 && std::abs (semis) <= kPsolaMaxSemis && std::abs (sp.formantRatio - 1.0) < 1e-9)
        {
            const int segN = (int) (b - a), len = juce::jmin (segN, noteLen + rel);
            std::vector<float> x ((size_t) segN); for (int i = 0; i < segN; ++i) x[(size_t) i] = mono (a + i);
            // the pitch marks: one per period along the analysis' f0 track, each one where the waveform repeats the last period best
            // (a +-15 % search by normalised correlation), the first on the period's highest peak: consistent marks, in order
            std::vector<psola::Epoch> ep;
            {
                auto f0At = [&] (double i) { return c.P.f0AtWork ((double) a + i); };
                int run = 0; double i = 0;
                while (i < segN)
                {
                    if (f0At (i) <= 40.0) { i += 48; continue; }
                    double P = c.sr / f0At (i);
                    if (i + 2 * P >= segN) break;
                    int pk = (int) i; for (int j = (int) i; j < (int) (i + P); ++j) if (x[(size_t) j] > x[(size_t) pk]) pk = j;
                    double tm = pk; ep.push_back ({ tm, run });
                    while (true)
                    {
                        const double f = f0At (tm); if (f <= 40.0) break;
                        P = c.sr / f; const int Pi = (int) P;
                        const double cand = tm + P; const int sr2 = (int) (0.15 * P);
                        if (cand + sr2 + Pi >= segN) break;
                        double best = -2; int bd = 0; const int t0 = (int) std::lround (tm);
                        for (int d = -sr2; d <= sr2; ++d)
                        {
                            const int t1 = (int) std::lround (cand) + d; double sxy = 0, sxx = 0, syy = 0;
                            for (int q = -Pi / 2; q < Pi / 2; ++q) { const int u0 = t0 + q, u1 = t1 + q; if (u0 < 0 || u1 < 0) continue; const double p0 = x[(size_t) u0], p1 = x[(size_t) u1]; sxy += p0 * p1; sxx += p0 * p0; syy += p1 * p1; }
                            const double cc = sxy / std::sqrt (sxx * syy + 1e-20);
                            if (cc > best) { best = cc; bd = d; }
                        }
                        if (best < 0.3) break;   // (it stopped repeating: the end of the voiced run)
                        tm = std::lround (cand) + bd; ep.push_back ({ tm, run });
                    }
                    ++run; i = tm + P;
                }
            }
            const bool legato = o0 - prevEnd <= 3 * vm::kHop && prevHz > 0;
            const double baseHz = dsp::midiToHz (ownMidi + semis), gS = N.glideS >= 0 ? N.glideS : sp.glideS;
            const double keep = (1.0 - v) * (1.0 - 0.75 * c.m()) * 2.0 * c.r.deepOr (Type::Instrument, 4);
            std::vector<double> y ((size_t) len + 4, 0.0); std::vector<float> uw ((size_t) len, 1.0f);   // uw: 1 = the recording as it is
            const int ramp = (int) (0.006 * c.sr);
            for (size_t r0 = 0; r0 < ep.size();)
            {
                size_t r1 = r0; while (r1 + 1 < ep.size() && ep[r1 + 1].run == ep[r0].run) ++r1;
                const int nE = (int) (r1 - r0 + 1);
                if (nE >= 3 && ep[r0].pos < len)
                {
                    auto E = [&] (int k) { return ep[r0 + (size_t) juce::jlimit (0, nE - 1, k)].pos; };
                    auto Pk = [&] (int k) { return k <= 0 ? E (1) - E (0) : k >= nE - 1 ? E (nE - 1) - E (nE - 2) : 0.5 * (E (k + 1) - E (k - 1)); };
                    std::vector<double> run ((size_t) len + 4, 0.0);
                    double tt = E (0); int k = 0;
                    while (tt <= E (nE - 1) && tt < len)
                    {
                        while (k + 1 < nE && std::abs (E (k + 1) - tt) <= std::abs (E (k) - tt)) ++k;
                        const double fTr = c.P.f0AtWork ((double) a + E (k));
                        const double P = fTr > 40.0 && std::abs (c.sr / fTr - Pk (k)) < 0.25 * Pk (k) ? c.sr / fTr : juce::jmax (24.0, Pk (k)), f0s = c.sr / P;
                        const double tn = tt / c.sr;
                        double target = baseHz * std::pow (juce::jlimit (0.5, 2.0, f0s / juce::jmax (40.0, S.f0)), keep);
                        if (legato && gS > 0) target *= std::pow (prevHz / baseHz, std::exp (-tn / gS));
                        const double ratio = c.r.engine >= 9 ? juce::jlimit (std::exp2 (-(kPsolaMaxSemis + 0.8) / 12.0), std::exp2 ((kPsolaMaxSemis + 0.8) / 12.0), target / f0s) : c.r.engine >= 8 ? juce::jlimit (0.48, 2.08, target / f0s) : juce::jlimit (0.6, 1.6, target / f0s);
                        const int Pi = (int) std::ceil (P); const double ek = E (k), frac = tt - std::floor (tt); const int at = (int) std::floor (tt);
                        for (int j = -Pi; j <= Pi; ++j)
                        {
                            const double sx = ek + j; const int si0 = (int) std::floor (sx); const double sf = sx - si0;
                            if (si0 < 0 || si0 + 1 >= segN) continue;
                            const double w = 0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * j / P);
                            if (std::abs (j) > P) continue;
                            const double val = w * ((1.0 - sf) * x[(size_t) si0] + sf * x[(size_t) si0 + 1]);
                            const int d0 = at + j;
                            if (d0 >= 0 && d0 + 1 < len) { run[(size_t) d0] += (1.0 - frac) * val; run[(size_t) d0 + 1] += frac * val; }
                        }
                        tt += P / ratio;
                    }
                    // the run's level = the recording's over the same span; the recording takes over (6 ms ramps) outside the run
                    const int s0r = juce::jmax (0, (int) (E (0) - Pk (0))), s1r = juce::jmin (len, (int) (E (nE - 1) + Pk (nE - 1)));
                    double ex = 0, ey = 0; for (int i = s0r; i < s1r; ++i) { ex += (double) x[(size_t) i] * x[(size_t) i]; ey += run[(size_t) i] * run[(size_t) i]; }
                    const double gn = ey > 1e-18 ? std::sqrt (ex / ey) : 0.0;
                    for (int i = s0r; i < s1r; ++i)
                    {
                        const double wv = juce::jlimit (0.0, 1.0, juce::jmin ((i - s0r) / (double) ramp, (s1r - 1 - i) / (double) ramp));
                        y[(size_t) i] += wv * gn * run[(size_t) i]; uw[(size_t) i] = (float) juce::jmin ((double) uw[(size_t) i], 1.0 - wv);
                    }
                }
                r0 = r1 + 1;
            }
            std::vector<double> z ((size_t) len); for (int i = 0; i < len; ++i) z[(size_t) i] = y[(size_t) i] + uw[(size_t) i] * x[(size_t) i];
            vm::envelopeGuard (x, z, len, c.sr, juce::jmax (S.f0, baseHz) * 1.25, c.r.engine >= 8);
            if (c.r.engine >= 8) vm::envelopeGuard (x, z, len, c.sr, juce::jmax (S.f0, baseHz) * 1.25, true);   // (build 10: a second pass, as the true envelope iterates: worst syllable of 12 takes 0.928 -> 0.955)
            const int fade = len < segN ? juce::jmin (rel, len / 2) : juce::jmin (len / 4, (int) (0.01 * c.sr));
            const int fin = juce::jmin (len / 4, (int) (0.002 * c.sr));
            float* d = out.getWritePointer (0);
            for (int i = 0; i < len && o0 + i < nOutAll; ++i)
            {
                double xv = z[(size_t) i];
                xv *= g;
                if (i < fin) xv *= (double) i / (double) fin;
                if (fade > 0 && i >= len - fade) xv *= (double) (len - i) / (double) fade;
                d[o0 + i] += (float) xv;
            }
            prevHz = baseHz; prevEnd = o0 + len;
            continue;
        }
        // ---- the model (buildModels made it)
        const auto& SM = models[(size_t) si];
        if (SM.m == nullptr) continue;
        const auto& M = *SM.m;
        const double hop = vm::kHop;
        const double cons = (S.v0 - (double) a) / hop, vowN = juce::jmax (2.0, (S.v1 - S.v0) / hop), coda = juce::jmax (0.0, ((double) b - S.v1) / hop);
        const double noteF = noteLen / hop;
        // the vowel: its own length at VOICE 0, stretched toward the note as VOICE rises (never squeezed: a short note cuts it)
        const double relF = rel / hop, codaE = sp.hold ? 0.0 : coda * (1.0 - v);
        const double want = juce::jmax (2.0, noteF + relF - cons - codaE), vowE = sp.hold ? 1e9 : (want > vowN ? vowN + v * (want - vowN) : vowN);
        const int nF = juce::jmax (3, (int) std::ceil (sp.hold ? noteF : juce::jmin (cons + vowE + codaE, noteF + relF)));
        vm::Plan P; P.formantRatio = sp.formantRatio; P.seed = sp.seed + (uint32_t) ni * 7919u; P.breathAirHz = 2400.0 * (1.0 - 0.5 * sp.breath); P.smoothResid = c.r.engine >= 9;
        const bool legato = o0 - prevEnd <= 3 * vm::kHop && prevHz > 0;
        const double baseHz = dsp::midiToHz (ownMidi + semis);
        const double keep = (1.0 - v) * (1.0 - 0.75 * c.m()) * 2.0 * c.r.deepOr (Type::Instrument, 4);   // (OWN PITCH; MUSICAL sings in tune, NATURAL keeps the voice's scoops)
        for (int j = 0; j < nF; ++j)
        {
            double src; int part;
            if (j < cons) { src = j; part = 0; }
            else if (sp.hold)
            {
                const double q = j - cons, first = 0.8 * vowN;
                if (q < first) src = cons + q;
                else { const double ph = (q - first) * hop / c.sr / juce::jmax (0.5, sp.holdPeriodS); src = cons + vowN * (0.5 + 0.3 * (0.5 + 0.5 * std::cos (juce::MathConstants<double>::twoPi * ph))); }
                part = 1;
            }
            else if (j < cons + vowE) { src = cons + (j - cons) * vowN / vowE; part = 1; }
            else { src = cons + vowN + (j - cons - vowE); part = 2; }   // (the coda as said, fading with VOICE)
            const double rate = part == 1 ? (sp.hold ? 0.0 : vowN / vowE) : 1.0;
            const int mj = juce::jlimit (0, (int) M.fr.size() - 1, (int) std::lround (src));
            const double f0s = M.fr[(size_t) mj].f0;
            // the pitch: the note (gliding in when legato), the voice's own movement by `keep`, the vibrato
            double f = 0;
            if (f0s > 0 || (part == 1 && v > 0.5))
            {
                const double dev = f0s > 0 && S.f0 > 0 ? juce::jlimit (-200.0, 200.0, 1200.0 * std::log2 (f0s / S.f0)) : 0.0;
                const double tn = j * hop / c.sr;
                double cents = dev * keep;
                if (sp.vibCents > 0) cents += sp.vibCents * v * juce::jlimit (0.0, 1.0, (tn - sp.vibDelayS) / 0.5) * std::sin (juce::MathConstants<double>::twoPi * sp.vibHz * tn);
                const double gS = N.glideS >= 0 ? N.glideS : sp.glideS; double target = baseHz; if (legato && gS > 0) target = baseHz * std::pow (prevHz / baseHz, std::exp (-tn / gS));
                f = target * std::exp2 (cents / 1200.0);
            }
            double gg = g;
            gg *= juce::jlimit (0.0, 1.0, j / (legato ? 1.0 : 2.0));                     // (from silence: 10 ms in, 5 ms when it joins the last note)
            if (! sp.hold && j >= nF - 3) gg *= juce::jmax (0.0, (nF - 1 - j) / 2.0);       // (and 10 ms out to silence, wherever it ends)
            if (! sp.hold && j * hop >= noteLen) gg *= std::pow (juce::jlimit (0.0, 1.0, 1.0 - (j * hop - noteLen) / (double) rel), 2.0);
            if (part != 1) gg *= consW;
            P.src.push_back (src); P.f0.push_back (f); P.gain.push_back ((float) gg);
            P.inst.push_back ((float) (part == 1 ? v : 0.5 * v)); P.phase.push_back ((float) v);
            P.resid.push_back ((float) (part == 1 ? (1.0 - v) * gg : gg)); P.air.push_back ((float) (part == 1 ? v * (0.2 + 0.8 * sp.breath) * gg * 0.6 : 0.0));
            P.copy.push_back (std::abs (rate - 1.0) < 0.02 ? 1 : 0);
            P.lock.push_back ((float) (std::abs (semis) < 0.01 && std::abs (rate - 1.0) < 0.02 && sp.formantRatio == 1.0 ? (1.0 - v) : 0.0));
        }
        // the instrument's timbre at each output frame (the vowel's core, slowly breathing through it, as the held tone)
        P.instFrame = [&, a] (int j)
        {
            voc::Frame inst = sp.inst;
            if (sp.instEvolveB > sp.instEvolveA + 2)
            {
                const double ph = (j * hop / c.sr) / juce::jmax (0.5, sp.holdPeriodS);
                voc::blend (inst, F.at (sp.instEvolveA + (sp.instEvolveB - sp.instEvolveA) * (0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * ph))), 0.3);
            }
            inst.gain = 1.0f;
            juce::ignoreUnused (a);
            return inst;
        };
        auto y = vm::render (M, P);
        {   // (the noise frames overlap the edges of the note: a 5 ms fade in and out keeps every edge silent)
            const int f = juce::jmin ((int) y.size() / 4, vm::kHop);
            for (int i = 0; i < f; ++i) { const float w = (float) i / (float) f; y[(size_t) i] *= w; if (! sp.hold) y[y.size() - 1 - (size_t) i] *= w; }
        }
        float* d = out.getWritePointer (0);
        for (int i = 0; i < (int) y.size() && o0 + i < nOutAll; ++i) d[o0 + i] += y[(size_t) i];
        prevHz = baseHz; prevEnd = o0 + noteLen;
    }
    return out;
}

// the take's knobs
double knobOf (const Ctx& c, Type t) { return c.r.knobOr (t); }
double deepOf (const Ctx& c, Type t, int i) { return c.r.deepOr (t, i); }

// the sound's own melody (the analysis' notes, in order): the VOICE style's seed
std::vector<SeedNote> seedNotes (const Ctx& c)
{
    std::vector<SeedNote> v; const double ssr = c.P.src->sampleRate;
    for (auto& n : c.P.analysis->notes)
    {
        if (n.pitch <= 0 || n.s1 - n.s0 < (int64) (0.06 * ssr)) continue;
        if (! c.manual.empty()) { bool in = false; for (auto& m : c.manual) in = in || (n.s0 >= m.s0 - (int64) (0.05 * ssr) && n.s1 <= m.s1 + (int64) (0.05 * ssr)); if (! in) continue; }
        v.push_back ({ n.pitch, (double) (n.s1 - n.s0) / ssr });
    }
    return v;
}
Style styleFor (const Ctx& c, Type t)
{
    if (c.r.style == Style::Auto && c.m() < 0.35 && seedNotes (c).size() >= 3 && t != Type::Texture) return Style::Voice;   // NATURAL: its own melody
    return resolveStyle (c.r.style, (int) t, c.r.seed);
}

// ---------------------------------------------------------------- INSTRUMENT (engine 6)
void makeInstrument6 (Ctx& c, Take& t)
{
    const Segment& s = mainPiece (c, &Segment::instrument, 3);
    double c0, c1; coreOf (c, s, c0, c1);
    auto Fp = c.P.vocoder();
    if (Fp == nullptr || Fp->frames() == 0) return;
    const auto& F = *Fp;
    const double voice = knobOf (c, Type::Instrument);
    const double formantSemis = (deepOf (c, Type::Instrument, 0) - 0.5) * 8.0 + (c.r.character == Character::Deep ? -4.0 : 0.0);
    const double breath = deepOf (c, Type::Instrument, 1), vib = deepOf (c, Type::Instrument, 2), glide = deepOf (c, Type::Instrument, 3);
    Frame inst = timbreOf (F, c0, c1);
    const double own = inst.f0 > 0 ? dsp::hzToMidi (inst.f0) : (s.midi > 0 ? s.midi : 60.0);
    const int root = c.musical() ? snapToScale ((int) std::lround (own), c.key) : (int) std::lround (own);
    t.rootNote = root;
    const bool cl = c.clear();
    c.report (0.5f);
    // ---- the held tone on every key (CLEAR: a zone per semitone; else every 2nd: formant-true, so a semitone of resampling is clean)
    const double secs = 3.6; const int ls = (int) (1.0 * c.sr), le = (int) (3.5 * c.sr);
    const int step = cl ? 1 : 2;
    std::vector<int> offs; for (int off = -18; off <= 18; off += step) offs.push_back (off);
    auto syl = syllablesFor (c, 8);
    // build 8: an edit on the roll re-renders only the preview: the zones and the syllables' models are the take's before the edit
    struct InstCache { std::vector<SylModel> models; };
    const auto rc = c.reuse != nullptr ? std::static_pointer_cast<const InstCache> (c.reuse->cache) : nullptr;
    std::vector<SylModel> models; if (rc != nullptr) models = rc->models; else buildModels (c, syl, models);
    { auto ic = std::make_shared<InstCache>(); ic->models = models; t.cache = ic; }
    const bool chopsOnKeys = voice < 0.98 && ! syl.empty();
    std::vector<int> sOffs; if (chopsOnKeys) for (int off = -16; off <= 16; off += 4) sOffs.push_back (off);   // (formant-true: two semitones of resampling either way stay clean)
    const int nSyl = chopsOnKeys ? juce::jmin (6, (int) syl.size()) : 0;
    const int total = (int) offs.size() + nSyl * (int) sOffs.size();
    std::vector<Zone> zs (offs.size()); std::vector<std::vector<Zone>> sz ((size_t) nSyl, std::vector<Zone> (sOffs.size()));
    std::atomic<int> done { 0 };
    const double vibC = cl ? vib * 10.0 : vib * 16.0;   // (CLEAR's default is 0: no wobble)
    auto fRatio = [&] (double semisFromVoice) { return std::exp2 ((c.r.character == Character::Chipmunk ? semisFromVoice : formantSemis) / 12.0); };
    if (rc == nullptr) forEachZone (total, true, [&] (int zi)
    {
        if (c.cancelled()) return;
        if (zi < (int) offs.size())
        {
            const int zn = root + offs[(size_t) zi];
            SingSpec sp; sp.voice = 1.0; sp.inst = inst; sp.instEvolveA = vfIdx (c0); sp.instEvolveB = vfIdx (c1); sp.holdPeriodS = (le - ls) / c.sr;
            sp.vibCents = vibC; sp.vibHz = std::round (5.2 * sp.holdPeriodS) / sp.holdPeriodS; sp.vibDelayS = 0.45; sp.formantRatio = fRatio (zn - own); sp.breath = breath; sp.seed = c.sub ((uint32_t) (zn + 40));
            sp.hold = true;
            Syl tone; tone.s0 = c.wf (s.v1 > s.v0 ? s.v0 : s.s0); tone.v0 = tone.s0; tone.v1 = c1; tone.s1 = c1; tone.f0 = inst.f0;
            Zone z; z.rootNote = zn;
            z.audio = sing (c, F, { tone }, { { 0.0, secs, (double) zn, 1.0f, 0 } }, secs, sp);
            const int per = (int) std::ceil (c.sr / juce::jmax (40.0, dsp::midiToHz (zn)));
            const int zle = phaseMatchedLoopEnd (z.audio, ls, le - per, per, juce::jmax (2 * per, (int) (0.02 * c.sr)));
            levelTo (z.audio, ls, zle, -18.0);
            bakeLoop (z.audio, ls, zle, (int) (0.12 * c.sr));
            z.loopStart = ls; z.loopEnd = zle;
            zs[(size_t) zi] = std::move (z);
        }
        else
        {   // a syllable on the keys: its consonant as said, its vowel, then held (looped) while the key is down
            const int k = zi - (int) offs.size(), si = k / (int) sOffs.size(), oi = k % (int) sOffs.size();
            const int zn = root + sOffs[(size_t) oi];
            const Syl& S = syl[(size_t) si];
            const double cons = (S.v0 - S.s0) / c.sr, vow = (S.v1 - S.v0) / c.sr;
            const double zsecs = juce::jmin (0.3, cons) + juce::jmin (0.6, vow) + 1.3;
            SingSpec sp; sp.voice = voice; sp.inst = inst; sp.vibCents = vibC; sp.vibDelayS = 0.4; sp.formantRatio = fRatio (zn - own); sp.breath = breath;
            sp.hold = true; sp.holdPeriodS = 0.9; sp.seed = c.sub ((uint32_t) (zn * 13 + si + 900));
            Zone z; z.rootNote = zn;
            z.audio = singReal (c, F, syl, { { 0.0, zsecs, (double) zn, 1.0f, si } }, zsecs, sp, models);
            const int lsz = juce::jlimit ((int) (0.1 * c.sr), z.audio.getNumSamples() - (int) (0.6 * c.sr), (int) ((juce::jmin (0.3, cons) + 0.8 * juce::jmin (0.6, vow) + 0.25) * c.sr));
            const int lez0 = z.audio.getNumSamples() - (int) (0.06 * c.sr);
            const int per = (int) std::ceil (c.sr / juce::jmax (40.0, dsp::midiToHz (zn)));
            const int lez = phaseMatchedLoopEnd (z.audio, lsz, lez0 - per, per, juce::jmax (2 * per, (int) (0.02 * c.sr)));
            levelTo (z.audio, lsz, lez, -18.0);
            bakeLoop (z.audio, lsz, lez, (int) (0.1 * c.sr));
            z.loopStart = lsz; z.loopEnd = lez;
            sz[(size_t) si][(size_t) oi] = std::move (z);
        }
        c.report (0.5f + 0.3f * (float) (++done) / (float) total);
    });
    if (c.cancelled()) return;
    if (rc != nullptr) { zs = c.reuse->zones; sz = c.reuse->sylZones; }
    t.zones = std::move (zs); t.sylZones = std::move (sz);
    t.attackS = 0.006; t.releaseS = 0.3 + 0.2 * (1.0 - c.m()); t.pairDetune = false; t.ensemble = cl ? 0.0f : 0.18f;
    // ---- the preview: the melody, sung with the sound's syllables in order (VOICE) .. on the held tone (INSTRUMENT)
    const int centre = centreFor (c, root);
    t.style = styleFor (c, Type::Instrument);
    auto song = compose (c.key, centre, t.style, c.r.bars, c.r.seed, c.r.detail, c.r.variation, 0.5, 0.5, seedNotes (c), t.bpm);
    // build 8 (PLAY IT): SIMPLE / RICH, your chord swaps, ARP, MASTER, then your notes on the roll (none: exactly as before)
    edit::editSong (song, c.key, centre, c.r.bars, c.r.seed, c.r.edit, true, true, &t.writtenChords);
    song.melody = edit::editLine (song.melody, c.r.edit, c.key, c.r.bars, true);
    t.notes = song.melody; t.chords = song.chords; t.beatsPerChord = song.beatsPerChord; t.progName = song.progName; t.form = song.form;
    const double spb = 60.0 / t.bpm;
    std::vector<SingNote> sn; int k = 0;
    {
        const auto play = edit::playLine (song.melody);   // (FATTEN: the note after the line louder)
        for (size_t i = 0; i < play.size(); ++i)
        {
            const auto& e = play[i];
            SingNote n { e.beat * spb, (e.beat + e.length) * spb, (double) e.note, e.velocity, k++ };
            if (i > 0 && song.melody[i - 1].link == 1 && ! sn.empty())
            {   // GLIDE: the note before holds until this one and slides into it; the slide starts where the line's dot is
                sn.back().t1 = juce::jmax (sn.back().t1, n.t0);
                n.glideS = juce::jlimit (0.02, 0.6, (1.0 - song.melody[i - 1].linkAmt) * (n.t0 - sn.back().t0) / 2.5);
            }
            sn.push_back (n);
        }
    }
    SingSpec sp; sp.voice = voice; sp.inst = inst; sp.instEvolveA = vfIdx (c0); sp.instEvolveB = vfIdx (c1); sp.holdPeriodS = 3.0;
    sp.vibCents = vibC; sp.glideS = 0.012 + 0.07 * glide; sp.formantRatio = fRatio (centre - own); sp.breath = breath; sp.seed = c.sub (77);
    sp.ownPitch = 0.6 * (1.0 - c.m());   // (the voice's own scoops and wobbles: only as NATURAL as you ask; MUSICAL sings in tune)
    std::vector<Syl> sylP = syl; if (sylP.empty()) { Syl tone; tone.s0 = tone.v0 = c.wf (s.v1 > s.v0 ? s.v0 : s.s0); tone.v1 = tone.s1 = c1; tone.f0 = inst.f0; sylP.push_back (tone); }
    const double total2 = song.bars * 4.0 * spb + t.releaseS + 0.6;
    std::vector<SylModel> modelsP = models; if (syl.empty()) buildModels (c, sylP, modelsP);
    t.preview = stereoOf (singReal (c, F, sylP, sn, total2, sp, modelsP));
    std::vector<NoteEvent> bedNotes;   // (build 10: the CHORD BED's notes go into the MIDI's CHORDS track)
    if (c.r.edit.bed > 0.0 && ! t.chords.empty() && ! t.zones.empty())
    {   // build 9: the CHORD BED (PLAY IT): the take's chords held softly on the same voice's held tone (its own zones, a slow pad-like
        // swell, darker), under the melody, so RICH chords and your swaps are heard. Off (0) by default: then nothing here runs.
        std::vector<NoteEvent> bev; const double len = c.r.bars * 4.0;
        int i = 0;
        for (double b = 0; b < len - 1e-9; b += t.beatsPerChord, ++i)
        {
            const auto& ch = t.chords[(size_t) i % t.chords.size()];
            std::vector<int> ns;
            for (size_t j = ch.notes.size() > 2 ? 1 : 0; j < ch.notes.size(); ++j)
            {
                int n = ch.notes[j]; while (n > t.rootNote + 9) n -= 12; while (n < t.rootNote - 12) n += 12;
                if (std::find (ns.begin(), ns.end(), n) == ns.end()) ns.push_back (n);
            }
            for (int n : ns) { NoteEvent e; e.note = n; e.beat = b; e.length = juce::jmin (t.beatsPerChord, len - b) + 0.1; e.velocity = 0.5f; bev.push_back (e); }
        }
        Take bt; bt.zones = std::move (t.zones); bt.rate = t.rate; bt.bpm = t.bpm; bt.rootNote = t.rootNote; bt.type = Type::Pad; bt.attackS = 0.45; bt.releaseS = 1.4; bt.ensemble = 0.0f;
        bedNotes = bev;
        auto bed = renderEvents (bt, bev, total2);
        t.zones = std::move (bt.zones);
        bed = lowpass (bed, c.sr, 2600.0);
        const double rm = rmsOf (t.preview), rb = rmsOf (bed);
        if (rb > 1e-9 && rm > 1e-9) addInto (t.preview, bed, 0, (float) (c.r.edit.bed * 0.75 * rm / rb));
        t.chain.clear();   // (set below; the bed shows as a step)
    }
    t.midi = toMidi (edit::midiLine (song.melody), t.bpm);
    // build 11: DRAG MIDI is the melody alone again (one clean track, FL-proof); the chords go out through DRAG CHORDS
    t.chordEvents = bedNotes;
    t.used.clear();
    if (voice < 0.98) for (auto& y : syl) t.used.push_back ({ y.s0 / c.sr, y.s1 / c.sr });
    if (t.used.empty()) t.used = { { (double) s.s0 / c.P.src->sampleRate, (double) s.s1 / c.P.src->sampleRate } };
    t.chain = { voice < 0.98 ? "VOICE PARTS" : "HELD VOWEL", "RESYNTH (WORLD)", "PITCH", "FORMANT", voice > 0.02 ? "MORPH" : "WORDS", "LOOP", "SPACE" };
    if (c.r.edit.bed > 0.0) t.chain.insert (t.chain.size() - 1, "CHORD BED");
    const juce::String noteName = juce::MidiMessage::getMidiNoteName (root, true, true, 5);
    t.info = juce::String (styleName (t.style)) + " melody (" + song.form + ") sung " + (voice < 0.15 ? "with the words of your sound" : voice < 0.85 ? "half voice, half instrument" : "on the held vowel")
             + ". Keys: " + (chopsOnKeys ? "each press sings the next syllable" : "the held tone") + " (its own note is " + noteName + ").";
    finishPreview (c, t);
}

// grains made from a buffer that is already at the note (the frozen sound): read 1:1, no re-pitch, so nothing can alias. Mono.
juce::AudioBuffer<float> grainsOf (const juce::AudioBuffer<float>& src, double sr, double seconds, double density, double grainS, double spray, int window, uint32_t seed)
{
    const int n = (int) (seconds * sr), sn = src.getNumSamples();
    juce::AudioBuffer<float> out (1, juce::jmax (1, n)); out.clear();
    if (sn < 256) return out;
    Rng r ((uint64) seed * 2654435761u + 5u);
    const double gap = 1.0 / juce::jmax (0.5, density), len = juce::jmax (256.0, grainS * sr);
    auto win = [&] (double x) -> float { if (window == 1) return (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * x)); if (window == 2) { const double u = (x - 0.5) / 0.18; return (float) (std::exp (-0.5 * u * u) * (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * x))); } const double e = 0.3; return (float) (x < e ? 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * x / e) : x > 1.0 - e ? 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * (1.0 - x) / e) : 1.0); };   // (Tukey, 60 % tapered)
    double wp = 0; for (int i = 0; i < 256; ++i) { const double w = win ((i + 0.5) / 256.0); wp += w * w; } wp /= 256.0;
    const float g = (float) (1.0 / std::sqrt (juce::jmax (1.0, density * grainS * wp)));
    const float* x = src.getReadPointer (0);
    for (double t = 0; t < seconds; t += gap * (0.5 + r.uni()))
    {
        const int o = (int) (t * sr); const double head = t / juce::jmax (0.1, seconds);
        int p = (int) ((head + spray * r.sym() * 0.5) * (sn - len)); p = juce::jlimit (0, juce::jmax (0, sn - (int) len - 1), p);
        for (int i = 0; i < (int) len && o + i < n; ++i) out.addSample (0, o + i, g * win ((double) i / len) * x[p + i]);
    }
    fadeEdges (out, (int) (0.05 * sr), (int) (0.05 * sr));
    return out;
}

// ---------------------------------------------------------------- PAD (engine 6)
void makePad6 (Ctx& c, Take& t)
{
    const Segment& s = mainPiece (c, &Segment::pad, 3);
    double c0, c1; coreOf (c, s, c0, c1);
    auto Fp = c.P.vocoder();
    if (Fp == nullptr || Fp->frames() == 0) return;
    const auto& F = *Fp;
    const double dream = knobOf (c, Type::Pad);
    const double shimmer = deepOf (c, Type::Pad, 0), freezeK = deepOf (c, Type::Pad, 1), grainsK = deepOf (c, Type::Pad, 2), colour = deepOf (c, Type::Pad, 3);
    const double gSize = deepOf (c, Type::Pad, 4), gSpray = deepOf (c, Type::Pad, 5), gWin = deepOf (c, Type::Pad, 6), blur = deepOf (c, Type::Pad, 7), route = deepOf (c, Type::Pad, 8);
    const int winKind = gWin < 1.0 / 3.0 ? 0 : gWin < 2.0 / 3.0 ? 1 : 2;
    Frame timbre = timbreOf (F, c0, c1);
    // a second vowel the pad drifts toward (slow movement), when the sound has one
    const auto sources = grainSources (c, 8, true);
    Frame timbre2 = timbre;
    for (auto& g : sources) if (std::abs (g.a - c.wf (s.v0)) > 0.05 * c.sr) { timbre2 = timbreOf (F, g.a + 0.2 * (g.b - g.a), g.b - 0.2 * (g.b - g.a)); break; }
    const double own = timbre.f0 > 0 ? dsp::hzToMidi (timbre.f0) : (s.midi > 0 ? s.midi : 57.0);
    const int root = c.musical() ? snapToScale ((int) std::lround (own), c.key) : (int) std::lround (own);
    t.rootNote = root;
    const bool cl = c.clear();
    c.report (0.5f);
    const double secs = 6.2; const int ls = (int) (1.8 * c.sr), le = (int) (5.9 * c.sr);
    const double loopS = (le - ls) / c.sr;
    const int step = cl ? 1 : 2;
    std::vector<int> offs; for (int off = -16; off <= 16; off += step) offs.push_back (off);
    std::vector<Zone> zs (offs.size()); std::atomic<int> done { 0 };
    const double fr0 = std::exp2 ((c.r.character == Character::Deep ? -4.0 : 0.0) / 12.0);
    const bool reuseZones = c.reuse != nullptr && c.reuse->zones.size() == offs.size();   // (build 8: an edit re-renders only the preview)
    if (! reuseZones) forEachZone ((int) offs.size(), true, [&] (int zi)
    {
        if (c.cancelled()) return;
        const int zn = root + offs[(size_t) zi];
        const double hz = dsp::midiToHz (zn);
        const double frat = c.r.character == Character::Chipmunk ? std::exp2 ((zn - own) / 12.0) : fr0;
        // the choir: 1 voice (CLEAR) or 3, gently detuned, each its own slow drift through the two vowels (loop-periodic)
        const int nv = cl ? 1 : 2;
        const int nFr = (int) std::ceil (secs / voc::kFrameS);
        juce::AudioBuffer<float> body (1, nFr * (int) std::lround (c.sr * voc::kFrameS)); body.clear();
        for (int vi = 0; vi < nv; ++vi)
        {
            const double det = nv == 1 ? 0.0 : (vi == 0 ? -1.0 : 1.0) * (2.5 + 5.0 * dream);
            Track T ((size_t) nFr);
            for (int j = 0; j < nFr; ++j)
            {
                const double tt = j * voc::kFrameS;
                Frame f = timbre;
                const double ph = (tt - ls / c.sr) / loopS;   // one cycle per loop: the loop joins on the same timbre
                voc::blend (f, timbre2, (0.15 + 0.35 * dream) * (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * (ph + vi / 3.0))));
                const double drift = cl ? 0.0 : dream * 3.0 * std::sin (2.0 * juce::MathConstants<double>::pi * (ph * 2.0 + vi * 0.37));
                f.f0 = hz * std::exp2 ((det + drift) / 1200.0);
                f.gain = 1.0f;
                T[(size_t) j] = f;
            }
            voc::SynthSpec ss; ss.formantRatio = frat; ss.seed = c.sub ((uint32_t) (zn * 7 + vi + 100)); ss.breathFloor = 0.06 + 0.12 * dream; ss.breathCeil = 0.6; ss.noise = 0.5 + 0.4 * dream; ss.tiltDbPerOct = -1.0 - 1.5 * dream; ss.airHz = 2000.0; ss.topHz = 12000.0;   // (a pad's top is soft: no harmonics above 12 kHz)
            auto y = voc::synth (T, c.sr, ss);
            body.addFrom (0, 0, y.data(), juce::jmin ((int) y.size(), body.getNumSamples()), 1.0f / (float) nv);
        }
        levelTo (body, 0, body.getNumSamples(), -20.0);
        // the shimmer: the same voice an octave up, softly (formants kept: a choir's upper octave, not a chipmunk)
        juce::AudioBuffer<float> shim (1, body.getNumSamples()); shim.clear();
        const double shimAmt = shimmer * (0.25 + 0.75 * dream);
        if (shimAmt > 0.02 && zn + 12 <= 108)
        {
            Track T ((size_t) nFr);
            for (int j = 0; j < nFr; ++j) { Frame f = timbre; f.f0 = hz * 2.0 * std::exp2 ((cl ? 0.0 : 3.0) / 1200.0); f.gain = (float) (0.5 + 0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * (j * voc::kFrameS - ls / c.sr) / loopS)); T[(size_t) j] = f; }
            voc::SynthSpec ss; ss.formantRatio = frat; ss.seed = c.sub ((uint32_t) (zn * 11 + 300)); ss.breathFloor = 0.1; ss.breathCeil = 0.5; ss.noise = 0.4; ss.tiltDbPerOct = -2.0; ss.airHz = 2500.0; ss.topHz = 12000.0;
            auto y = voc::synth (T, c.sr, ss);
            shim.copyFrom (0, 0, y.data(), juce::jmin ((int) y.size(), shim.getNumSamples()));
            highpassInPlace (shim, c.sr, 900.0);
            levelTo (shim, 0, shim.getNumSamples(), -20.0);
        }
        // ROUTE: left = GRAINS -> FREEZE (the grains of the voice go into the freeze with the choir), right = FREEZE -> GRAINS (the
        // grains are made from the frozen sound). Both rendered here, offline.
        const bool grainsFirst = route < 0.5;
        // the grains: the voice itself, long calm grains (fewer as it gets dreamier: the pad gets smoother); normalised by their overlap
        CloudSpec cs; cs.seconds = secs; cs.density = (8.0 + 8.0 * c.m()) * (1.2 - 0.6 * dream) * (0.3 + 1.4 * grainsK); cs.grainS = (0.26 + 0.2 * dream) * (0.5 + gSize); cs.grainJitter = 0.15;
        cs.taper = 0.6; cs.spray = juce::jlimit (0.02, 1.0, (0.1 + 0.2 * dream) * (0.4 + 1.6 * gSpray)); cs.headSpeed = 0.12; cs.notes = { (double) zn, zn + 12.0 }; cs.noteWeights = { 1.0, 0.2 }; cs.foldOctaves = false;
        cs.natural = 1.0 - c.m(); cs.scatterCents = 2; cs.spread = 0; cs.reverse = 0; cs.gainJitterDb = 1.5; cs.clean = true; cs.channels = 1; cs.seed = c.sub ((uint32_t) (zn + 700));
        cs.normalise = true; cs.window = winKind;
        juce::AudioBuffer<float> grains;
        if (grainsFirst) { grains = cloud (c.W(), c.P.bank, c.sr, sources, cs); levelTo (grains, 0, grains.getNumSamples(), -20.0); }
        // the glass: the identity phase-locked vocoder (PhaseLock.h): clear and glassy at low DREAM, the random-phase (Paulstretch) smear
        // only toward the top of DREAM (or with BLUR); FREEZE sets how slowly it reads back and forth
        pl::Spec ps; ps.fftOrder = 13; ps.transients = false; ps.seed = c.sub ((uint32_t) (zn + 500));
        ps.random = juce::jlimit (0.0, 1.0, juce::jmax (blur, std::pow (juce::jmax (0.0, (dream - 0.45) / 0.55), 1.5) * (0.4 + 0.6 * freezeK)));
        juce::AudioBuffer<float> frIn; frIn.makeCopyOf (body); if (grainsFirst) addInto (frIn, grains, 0, 0.5f);
        auto glass = monoOf (pl::freeze (frIn, c.sr, 0.25 * c.sr, (secs - 0.1) * c.sr, secs, 2.0 + 6.0 * freezeK * (0.5 + dream), ps));
        levelTo (glass, 0, glass.getNumSamples(), -20.0);
        if (! grainsFirst) { grains = grainsOf (glass, c.sr, secs, cs.density, cs.grainS, cs.spray, winKind, cs.seed); levelTo (grains, 0, grains.getNumSamples(), -20.0); }
        Zone z; z.rootNote = zn; z.audio.setSize (1, body.getNumSamples()); z.audio.clear();
        addInto (z.audio, body, 0, (float) (0.85 - 0.3 * dream));
        addInto (z.audio, glass, 0, (float) ((0.12 + 0.45 * dream) * (0.4 + 1.2 * freezeK)));
        addInto (z.audio, shim, 0, (float) (0.35 * shimAmt));
        addInto (z.audio, grains, 0, (float) (0.28 * (1.0 - 0.4 * dream)));
        swell (z.audio, (int) ((0.3 + 0.6 * dream) * c.sr));
        const int per = (int) std::ceil (c.sr / juce::jmax (40.0, hz));
        const int zle = phaseMatchedLoopEnd (z.audio, ls, le - per, per, juce::jmax (2 * per, (int) (0.03 * c.sr)));
        levelTo (z.audio, ls, zle, -19.0);
        bakeLoop (z.audio, ls, zle, (int) ((0.5 + 0.7 * dream) * c.sr));
        z.loopStart = ls; z.loopEnd = zle;
        zs[(size_t) zi] = std::move (z);
        c.report (0.5f + 0.3f * (float) (++done) / (float) offs.size());
    });
    if (c.cancelled()) return;
    if (reuseZones) zs = c.reuse->zones;
    t.zones = std::move (zs); t.cache = std::make_shared<int> (1);
    t.attackS = 0.4 + 0.6 * c.m() + 0.9 * dream; t.releaseS = 1.8 + 2.4 * dream; t.pairDetune = false; t.ensemble = cl ? 0.0f : (float) (0.3 + 0.4 * dream);
    t.dream = (float) dream;
    // the progression: the style's, open voicings (wider with DREAM), 9ths / 11ths with colour; the top voice moves halfway through
    // each chord when it is dreamy (slowly evolving voicings)
    const int centre = centreFor (c, root);
    t.style = styleFor (c, Type::Pad); if (t.style == Style::Arp || t.style == Style::Acid) t.style = Style::Dreamy;
    double bpc = 4.0; juce::String names;
    std::vector<Chord> chords;
    const int L = juce::jlimit (1, 16, c.r.bars);
    if ((int) t.style < kStyles)
    {   // our own styles: the progression, voiced open (wider with DREAM), 9ths / 11ths with COLOUR
        auto prog = harmony (c.key, centre, t.style, c.r.seed, c.r.detail, c.r.variation, juce::jlimit (0.0, 1.0, colour * 0.8 + 0.35 * dream), 0.35 + 0.6 * dream, bpc, names);
        bpc = L >= 8 ? 8.0 : 4.0;   // (a pad: two bars per chord when there is time)
        for (int i = 0; i * bpc < L * 4.0; ++i) chords.push_back (prog[(size_t) i % prog.size()]);
    }
    else
    {   // a brain style: its chords as it wrote them (the longer ones every two bars when there is time)
        const auto song = compose (c.key, centre, t.style, L, c.r.seed, c.r.detail, c.r.variation, 0.5, 0.5, {}, t.bpm);
        bpc = L >= 8 ? juce::jmax (8.0, song.beatsPerChord) : juce::jmax (4.0, song.beatsPerChord);
        for (double b = 0; b < L * 4.0 - 1e-9; b += bpc) chords.push_back (song.chords[(size_t) juce::jlimit (0, (int) song.chords.size() - 1, (int) (b / song.beatsPerChord))]);
        names = song.progName;
        if (c.r.variation > 0)
        {   // VARY: the same progression in new inversions (the lowest upper voices up an octave), one chord re-voiced wider
            Rng rv ((uint64) c.r.detail * 7919u + (uint64) c.r.variation * 104729u + 3u);
            for (auto& ch : chords)
            {
                if (ch.notes.size() < 3) continue;
                const int inv = 1 + rv.pick (2);
                for (int k = 0; k < inv && k + 1 < (int) ch.notes.size(); ++k) ch.notes[(size_t) k + 1] += 12;
                std::sort (ch.notes.begin() + 1, ch.notes.end());
                while (ch.notes.back() > centre + 16) { ch.notes.back() -= 12; std::sort (ch.notes.begin() + 1, ch.notes.end()); }
            }
            if (! chords.empty()) { auto& w = chords[(size_t) rv.pick ((int) chords.size())]; if (w.notes.size() >= 3) w.notes[1] -= 12; std::sort (w.notes.begin(), w.notes.end()); }
        }
    }
    // build 8 (PLAY IT): SIMPLE / RICH and your chord swaps (none: exactly as before)
    t.writtenChords = chords;
    if (c.r.edit.music())
    {
        chords = edit::editChords (chords, c.key, c.r.edit);
        if (! chords.empty()) { names.clear(); for (size_t i = 0; i < juce::jmin<size_t> (4, chords.size()); ++i) names << (i ? " - " : "") << chords[i].name; }
    }
    std::vector<NoteEvent> ev;
    for (size_t i = 0; i < chords.size(); ++i)
    {
        const auto& ch = chords[i]; const double b0 = (double) i * bpc;
        for (size_t j = 0; j < ch.notes.size(); ++j)
        {
            const bool top = j + 1 == ch.notes.size();
            NoteEvent e; e.note = ch.notes[j]; e.beat = b0; e.length = bpc; e.velocity = j == 0 ? 0.55f : 0.7f;
            if (top && dream >= 0.35 && ch.notes.size() >= 3)
            {   // the top voice steps to the next chord tone (or the 9th) halfway: the voicing evolves
                e.length = bpc * 0.5; ev.push_back (e);
                int nx = e.note + 1; while (! isChordTone (nx, ch) && ((nx % 12) + 12) % 12 != (ch.pcs.front() + 2) % 12 && nx < e.note + 6) ++nx;
                if (nx < e.note + 6) { e.note = nx; e.beat = b0 + bpc * 0.5; e.velocity = 0.6f; }
                else { e.beat = b0 + bpc * 0.5; }
            }
            ev.push_back (e);
        }
    }
    ev = edit::editLine (ev, c.r.edit, c.key, juce::jmax (1, (int) std::ceil (chords.size() * bpc / 4.0 - 1e-9)), false);   // (your notes on the roll)
    t.notes = ev; t.chords = chords; t.beatsPerChord = bpc; t.progName = names; t.form = juce::String ((int) chords.size()) + " chords, " + juce::String (bpc / 4.0, 0) + (bpc > 4.0 ? " bars each" : " bar each");
    t.midi = toMidi (ev, t.bpm);
    const double spb = 60.0 / t.bpm, total = chords.size() * bpc * spb + t.releaseS + 0.4;
    t.preview = renderEvents (t, ev, total);
    c.report (0.86f);
    t.used.clear(); t.used.push_back ({ (double) s.s0 / c.P.src->sampleRate, (double) s.s1 / c.P.src->sampleRate });
    for (auto& g : sources) t.used.push_back ({ g.a / c.sr, g.b / c.sr });
    t.chain = { "VOICE PARTS", "RESYNTH (WORLD)", "CHOIR", "SHIMMER", "FREEZE (PHASE-LOCKED)", "GRAINS", "SPACE" };
    t.info = names + ". A " + juce::String (styleName (t.style)) + " pad: your vowel as a choir" + (shimmer * (0.25 + 0.75 * dream) > 0.02 ? " with an octave shimmer" : "") + ", frozen and grained. Play chords on your keys.";
    finishPreview (c, t);
}

// ---------------------------------------------------------------- CHOPS (engine 6): the phrase from the melody style + STUTTER
std::vector<NoteEvent> phrase6 (Ctx& c, Take& t, int centre, int nChops, const std::vector<bool>& isLong, int bars)
{
    const double stut = c.r.knobOr (Type::Chops), rev = c.r.deepOr (Type::Chops, 0), human = c.r.deepOr (Type::Chops, 1), rep = c.r.deepOr (Type::Chops, 2), gate = c.r.deepOr (Type::Chops, 3);
    t.style = styleFor (c, Type::Chops);
    auto song = compose (c.key, centre, t.style, bars, c.r.seed, c.r.detail, c.r.variation, 0.4, 0.4, seedNotes (c), t.bpm);
    edit::editSong (song, c.key, centre, bars, c.r.seed, c.r.edit, true, true, &t.writtenChords);   // (build 8: SIMPLE / RICH, swaps, ARP, MASTER)
    t.chords = song.chords; t.beatsPerChord = song.beatsPerChord; t.progName = song.progName; t.form = song.form;
    Rng rr ((uint64) c.r.seed * 23u + 11u), rv ((uint64) c.r.detail * 41u + (uint64) c.r.variation * 7919u + 13u);
    std::vector<int> longs, shorts; for (int i = 0; i < nChops; ++i) ((size_t) i < isLong.size() && isLong[(size_t) i] ? longs : shorts).push_back (i);
    if (longs.empty()) longs.push_back (0); if (shorts.empty()) shorts = longs;
    // VARY: the chops come in another order (the same words, said in a new order)
    if (c.r.variation > 0) { for (size_t i = longs.size(); i > 1; --i) std::swap (longs[i - 1], longs[(size_t) rv.pick ((int) i)]); for (size_t i = shorts.size(); i > 1; --i) std::swap (shorts[i - 1], shorts[(size_t) rv.pick ((int) i)]); }
    std::vector<NoteEvent> ev; int lastChop = -1, li = 0, si = 0;
    for (auto e : song.melody)
    {
        const bool longNote = e.length >= 0.7;
        if (! song.chords.empty())
        {   // chops are words with their own wobble: on chord tones they sit in the harmony (passing notes would smear it)
            const auto& ch = song.chords[(size_t) (((int) std::floor (e.beat / song.beatsPerChord)) % (int) song.chords.size())];
            const auto& pent = pentatonicOf (c.key.minor);
            auto ok = [&] (int n, bool needChord) { const bool inP = std::find (pent.begin(), pent.end(), ((n - c.key.tonic) % 12 + 12) % 12) != pent.end(); return inP && (! needChord || isChordTone (n, ch)); };
            bool done = false;
            for (int pass = 0; pass < 2 && ! done; ++pass)   // (a chord tone of the pentatonic scale, else the nearest pentatonic note)
                for (int d = 0; d <= 6 && ! done; ++d) { if (ok (e.note - d, pass == 0)) { e.note -= d; done = true; } else if (ok (e.note + d, pass == 0)) { e.note += d; done = true; } }
        }
        int chop = longNote ? longs[(size_t) (li++ % (int) longs.size())] : shorts[(size_t) (si++ % (int) shorts.size())];
        if (lastChop >= 0 && rr.chance (0.1 + 0.45 * rep)) chop = lastChop;   // the same chop twice: the classic repeat
        e.chop = juce::jlimit (0, juce::jmax (0, nChops - 1), chop); lastChop = e.chop;
        e.reverse = longNote && rv.chance (0.35 * rev);
        if (e.length >= 0.45 && ! e.reverse && rv.chance (0.03 + 0.45 * stut * stut)) e.stutter = 2 + rv.pick (1 + (int) std::lround (3 * stut));
        e.length *= 0.45 + 0.55 * gate;
        if (e.beat > 0.0) e.beat = juce::jmax (0.0, e.beat + 0.03 * human * rv.sym());
        ev.push_back (e);
    }
    return ev;
}
