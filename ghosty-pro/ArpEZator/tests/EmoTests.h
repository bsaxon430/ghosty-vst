#pragma once
// TRAP EMO checks. Sounds: CLEAN GUITAR PLUCK (Karplus-Strong string) + SAD PAD: peak at full velocity, aliasing, tuning, the
// string's decay, no clicks at the start / end, mono-safe, the strum, CPU. Included by ArpTests.cpp after UrbanTests.h.

namespace emotest
{
using namespace arp;

struct Held { std::vector<float> L, R; };
inline std::vector<std::string> split (const std::string& s, char c) { std::vector<std::string> o; std::string cur; for (char ch : s) { if (ch == c) { if (! cur.empty()) o.push_back (cur); cur.clear(); } else cur += ch; } if (! cur.empty()) o.push_back (cur); return o; }
inline bool has (const std::vector<int>& v, int x) { return std::find (v.begin(), v.end(), x) != v.end(); }
inline Held playNote (int preset, float pitch, float vel, int lenS, int totalS, bool noNoise = true, double sr = 48000, float delay = 0, float reverb = 0)
{
    arpsyn::testNoNoise() = noNoise;
    arpsyn::Engine e; e.prepare (sr, 512); arpsyn::Params P; P.preset = preset; P.delay = delay; P.reverb = reverb; P.pump = 0; P.lfoDepth = 0; P.volume = 0.8f;
    Held h; h.L.assign ((size_t) totalS, 0.0f); h.R.assign ((size_t) totalS, 0.0f);
    e.leadStart (2, 1, pitch, vel, lenS, P);
    for (int i = 0; i < totalS; i += 512) e.render (h.L.data() + i, h.R.data() + i, std::min (512, totalS - i), P, 120, 0, 0.0, false);
    arpsyn::testNoNoise() = false;
    return h;
}
inline double rmsOf (const std::vector<float>& x, int a, int b) { double s = 0; for (int i = a; i < b; ++i) s += (double) x[(size_t) i] * x[(size_t) i]; return std::sqrt (s / std::max (1, b - a)); }
inline double db (double x) { return 20 * std::log10 (x + 1e-12); }
// the period by normalised autocorrelation around the expected one (parabolic peak), -> Hz
inline double pitchHz (const std::vector<float>& x, int from, int len, double expectHz, double sr = 48000)
{
    const double P0 = sr / expectHz; int lo = (int) (P0 * 0.94), hi = (int) (P0 * 1.06) + 1;
    std::vector<double> r ((size_t) hi + 2, 0.0);
    for (int lag = lo - 1; lag <= hi + 1; ++lag)
    {
        double s = 0, e0 = 0, e1 = 0;
        for (int i = from; i < from + len; ++i) { const double a = x[(size_t) i], b = x[(size_t) (i + lag)]; s += a * b; e0 += a * a; e1 += b * b; }
        r[(size_t) lag] = s / std::sqrt (e0 * e1 + 1e-30);
    }
    int best = lo; for (int lag = lo; lag <= hi; ++lag) if (r[(size_t) lag] > r[(size_t) best]) best = lag;
    const double a = r[(size_t) best - 1], b = r[(size_t) best], c = r[(size_t) best + 1], d = a - 2 * b + c;
    const double off = std::abs (d) > 1e-12 ? 0.5 * (a - c) / d : 0.0;
    return sr / (best + off);
}

inline void soundChecks (Counter& C)
{
    const double sr = 48000;
    {   // full velocity: peak under -1 dBFS, no NaN (no FX, as the other new sounds); aliasing (FFT, C5..C7)
        double worstPk = 0, worstAl = -200; std::string at, atA; bool nan = false;
        for (int pre : { (int) arpsyn::GuitarPluck, (int) arpsyn::SadPad })
        {
            for (int pitch : { 40, 45, 52, 57, 64, 69, 76, 84 })
            {
                const auto h = playNote (pre, (float) pitch, 1.0f, 36000, 48000);
                for (int i = 0; i < 48000; ++i) { if (! std::isfinite (h.L[(size_t) i]) || ! std::isfinite (h.R[(size_t) i])) nan = true; const double a = std::max (std::abs (h.L[(size_t) i]), std::abs (h.R[(size_t) i])); if (a > worstPk) { worstPk = a; at = std::string (arpsyn::presetName (pre)) + " @" + std::to_string (pitch); } }
            }
            arpsyn::testNoNoise() = true;
            for (int pitch : { 72, 84, 90, 96 }) { const double a = soundtest::aliasDb (pre, pitch); if (a > worstAl) { worstAl = a; atA = std::string (arpsyn::presetName (pre)) + " @" + std::to_string (pitch); } }
            arpsyn::testNoNoise() = false;
        }
        // with the delay + reverb as TRAP sets them (the sounds' own sends on top)
        double fxPk = 0; for (int pitch : { 52, 64, 76 }) for (int pre : { (int) arpsyn::GuitarPluck, (int) arpsyn::SadPad }) { const auto h = playNote (pre, (float) pitch, 1.0f, 36000, 72000, false, sr, 0.3f, 0.42f); for (size_t i = 0; i < h.L.size(); ++i) fxPk = std::max (fxPk, (double) std::max (std::abs (h.L[i]), std::abs (h.R[i]))); }
        std::printf ("EMO sounds: worst peak %.3f (%s), with the TRAP delay + reverb %.3f, worst alias %.1f dB (%s)\n", worstPk, at.c_str(), fxPk, worstAl, atA.c_str());
        C.ok (! nan && worstPk < 0.891 && fxPk < 0.891, "EMO SOUNDS: CLEAN GUITAR PLUCK + SAD PAD clean at full velocity (peak under -1 dBFS, also with the FX; worst " + juce::String (std::max (worstPk, fxPk), 3).toStdString() + ")");
        C.ok (worstAl < -55.0, "EMO SOUNDS: no aliasing (worst " + juce::String (worstAl, 1).toStdString() + " dB, " + atA + ")");
    }
    {   // the string: in tune (fractional delay), decays by itself (low strings longer), no click at the start or at the release
        double worstCents = 0; std::string atP;
        for (int pitch : { 40, 45, 52, 57, 64, 69, 76, 81 })
        {
            const auto h = playNote (arpsyn::GuitarPluck, (float) pitch, 0.8f, 40000, 30000);
            const double f0 = 440.0 * std::pow (2.0, (pitch - 69) / 12.0), f = pitchHz (h.L, 9600, 4096, f0);
            const double c = 1200.0 * std::log2 (f / f0); if (std::abs (c) > std::abs (worstCents)) { worstCents = c; atP = std::to_string (pitch); }
        }
        const auto lo = playNote (arpsyn::GuitarPluck, 40.0f, 0.8f, 200000, 144000), hi = playNote (arpsyn::GuitarPluck, 76.0f, 0.8f, 200000, 144000);
        const double dLo = db (rmsOf (lo.L, 96000, 100800) / rmsOf (lo.L, 2400, 7200)), dHi = db (rmsOf (hi.L, 96000, 100800) / rmsOf (hi.L, 2400, 7200));
        // the release: a note cut at 0.3 s while it rings; the biggest sample step after the cut vs while it rang; the first samples
        const auto cut = playNote (arpsyn::GuitarPluck, 57.0f, 1.0f, 14400, 48000);
        double stepRing = 0, stepRel = 0, first = 0;
        for (int i = 9600; i < 14400; ++i) stepRing = std::max (stepRing, (double) std::abs (cut.L[(size_t) i] - cut.L[(size_t) i - 1]));
        for (int i = 14400; i < 48000; ++i) stepRel = std::max (stepRel, (double) std::abs (cut.L[(size_t) i] - cut.L[(size_t) i - 1]));
        for (int i = 0; i < 8; ++i) first = std::max (first, (double) std::abs (cut.L[(size_t) i]));
        const double firstS = std::abs (cut.L[0]);
        const double tail = rmsOf (cut.L, 14400 + 9600, 14400 + 14400) / rmsOf (cut.L, 12000, 14400);
        std::printf ("GUITAR string: worst tuning %.2f cents (@%s), after 2 s E2 %.1f dB / E5 %.1f dB, release step %.4f vs ringing %.4f, first samples %.4f, 200 ms after the cut %.1f dB\n",
                     worstCents, atP.c_str(), dLo, dHi, stepRel, stepRing, first, db (tail));
        C.ok (std::abs (worstCents) < 3.0, "GUITAR PLUCK: in tune E2..A5 (fractional-delay string, worst " + juce::String (worstCents, 2).toStdString() + " cents)");
        C.ok (dLo > -50 && dLo < -20 && dHi < dLo - 15, "GUITAR PLUCK: the string decays by itself, low strings ring longer (2 s: E2 " + juce::String (dLo, 1).toStdString() + " dB, E5 " + juce::String (dHi, 1).toStdString() + " dB)");
        C.ok (stepRel <= stepRing * 1.05 && firstS < 0.005 && first < 0.06 && db (tail) < -30, "GUITAR PLUCK: no clicks (soft start, a release that fades: no step bigger than while ringing, -30 dB within 200 ms)");
    }
    {   // mono-safe: the chorus taps cost < 1.5 dB in mono; SAD PAD: centred lows, its swell + long release
        double worstMono = 0;
        for (int pre : { (int) arpsyn::GuitarPluck, (int) arpsyn::SadPad })
            for (int pitch : { 45, 57, 69 })
            {
                const auto h = playNote (pre, (float) pitch, 0.8f, 48000, 48000);
                std::vector<float> m (h.L.size()); for (size_t i = 0; i < m.size(); ++i) m[i] = 0.5f * (h.L[i] + h.R[i]);
                const double st = std::sqrt (0.5 * (std::pow (rmsOf (h.L, 0, 48000), 2) + std::pow (rmsOf (h.R, 0, 48000), 2)));
                worstMono = std::min (worstMono, db (rmsOf (m, 0, 48000) / st));
            }
        const auto pad = playNote (arpsyn::SadPad, 57.0f, 0.8f, 96000, 288000);
        const double a50 = rmsOf (pad.L, 0, 2400), a600 = rmsOf (pad.L, 26400, 31200), held = rmsOf (pad.L, 84000, 96000), rel1 = rmsOf (pad.L, 96000 + 45600, 96000 + 50400), end = rmsOf (pad.L, 280000, 288000);
        std::printf ("EMO mono: worst mono-sum loss %.2f dB; SAD PAD: first 50 ms %.1f dB, at 0.6 s %.1f dB re held, 1 s after release %.1f dB, 4 s after %.1f dB\n",
                     worstMono, db (a50 / held), db (a600 / held), db (rel1 / held), db (end / held));
        C.ok (worstMono > -1.5, "EMO SOUNDS: mono-safe (mono sum within 1.5 dB, worst " + juce::String (worstMono, 2).toStdString() + ")");
        C.ok (db (a50 / held) < -12 && db (a600 / held) > -6 && db (rel1 / held) > -32 && db (rel1 / held) < -3 && db (end / held) < -60, "SAD PAD: slow ~450 ms swell, long release (still there 1 s after the key is up, gone after 4 s)");
    }
    {   // the strum: three notes that start together on a lane come in ~18 ms apart (low -> high as they arrive)
        arpsyn::testNoNoise() = true;
        arpsyn::Engine e; e.prepare (sr, 512); arpsyn::Params P; P.preset = arpsyn::Neon; P.delay = 0; P.reverb = 0; P.pump = 0; P.lfoDepth = 0; P.lanePreset[0] = arpsyn::GuitarPluck;
        const int ps[3] { 52, 59, 64 }; e.chord (ps, 3, 0.8f, 40000, false, &P);
        std::vector<float> L (9600, 0.0f), R (9600, 0.0f);
        for (int i = 0; i < 9600; i += 512) e.render (L.data() + i, R.data() + i, std::min (512, 9600 - i), P, 120, 0, 0.0, false);
        arpsyn::testNoNoise() = false;
        const double pre1 = rmsOf (L, 0, 860), s1 = rmsOf (L, 100, 800), s2 = rmsOf (L, 1000, 1600), s3 = rmsOf (L, 1900, 2600);
        int firstSound = -1; for (int i = 0; i < 9600 && firstSound < 0; ++i) if (std::abs (L[(size_t) i]) > 1e-4f) firstSound = i;
        std::printf ("GUITAR strum: string 1 alone %.1f dB, +string 2 %.1f dB, +string 3 %.1f dB (first sound at %d samples)\n", db (s1), db (s2), db (s3), firstSound);
        C.ok (firstSound >= 0 && firstSound < 48 && s2 > s1 * 1.15 && s3 > s2 * 1.08 && pre1 > 0, "GUITAR PLUCK: a chord strums (strings enter ~18 ms apart)");
    }
    {   // CPU: 6 strings + 4 pad voices, 10 s
        arpsyn::Engine e; e.prepare (sr, 512); arpsyn::Params P; P.preset = arpsyn::GuitarPluck; P.delay = 0.3f; P.reverb = 0.4f; P.pump = 0; P.lfoDepth = 0;
        P.lanePreset[0] = arpsyn::SadPad;
        const int N = 480000; std::vector<float> L (512), R (512);
        for (int s = 0; s < 6; ++s) e.leadStart (1, 100 + s, (float) (52 + s * 5), 0.8f, N, P);
        { const int ps[4] { 45, 52, 59, 64 }; e.chord (ps, 4, 0.7f, N, false, &P); }
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int i = 0; i < N; i += 512)
        {
            std::fill (L.begin(), L.end(), 0.0f); std::fill (R.begin(), R.end(), 0.0f);
            e.render (L.data(), R.data(), 512, P, 160, 0, 0.0, false);
        }
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        std::printf ("EMO CPU: 6 strings + 4 pad voices for 10 s rendered in %.0f ms (%.1f%% of real time)\n", ms, ms / 100.0);
        C.ok (ms < 300.0, "EMO SOUNDS: cheap (6 strings + a 4-note pad: " + juce::String (ms / 100.0, 1).toStdString() + "% of one core)");
    }
}

inline Idea makeEmo (uint32_t seed, int darkness = -1)
{
    Idea I; I.mode = Trap; I.seed = seed; I.flavour = kEmo; I.darkness = darkness; applyModeDefaults (I); newIdea (I); return I;
}
inline std::string pn (int p) { static const char* N[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; return std::string (N[mod12 (p)]) + std::to_string (p / 12 - 1); }
inline void dump (const Idea& I)
{
    std::string ch; for (auto& c : I.chords) ch += c.name + "(" + std::to_string (c.len) + ") ";
    std::printf ("EMO seed %u: %s %d bpm groove %.3f prog %d [%s] chords %s| bass '%s' melody '%s' guitar '%s' lead %d\n", I.seed, keyName (I.g.key, I.g.scale).c_str(), I.g.bpm, I.groove, I.urbanProg, emoProgName (I.urbanProg), ch.c_str(),
                 urbanBassArchName (Trap, I.urbanBassArch, kEmo), urbanMelArchName (Trap, I.urbanMelArch, kEmo), emoArpArchName (emoArchFor (I)), emoLead (I));
    for (int l : { (int) LaneChords, (int) LaneMelody, (int) LaneArp, (int) LaneBass })
    {
        std::string s;
        if (l == LaneChords) { for (auto& c : I.H.chords) { s += "["; for (int v : c.voicing) s += pn (v) + " "; s += "] "; } }
        else for (auto& n : I.lanes[l].notes) { if (n.t >= 4 * kEmoFeltBar) break; char b[64]; std::snprintf (b, sizeof b, "%d:%s/%d%s ", n.t / 48, pn (n.p).c_str(), n.d / 48, n.l.type == 1 ? "~" : ""); s += b; }
        std::printf ("  lane %d: %s\n", l, s.c_str());
    }
}

// ---------------------------------------------------------------- the EMO engine
constexpr uint64_t kUrbanPrint[2] { 0x1e56ab5c7b9741d9ull, 0x75b707a3b451311eull };   // urbanFingerprint (TRAP, HIP HOP) on origin/main before EMO
struct PhraseStats { int phrases = 0, spanOver = 0, peakOff = 0, endOnRoot = 0, endMissing = 0, withPickup = 0; double spanSum = 0; int steps = 0, moves = 0; };
inline void melodyPhrases (const Idea& I, PhraseStats& S, std::string* why = nullptr)
{
    const auto& mel = I.lanes[LaneMelody].notes; const int PT = 4 * kEmoFeltBar;
    for (int t0 = 0; t0 + PT <= I.total; t0 += PT)
    {
        std::vector<Note> ph; for (auto& n : mel) if (n.t >= t0 && n.t < t0 + PT) ph.push_back (n);
        if (ph.empty()) continue;
        ++S.phrases;
        int mn = 999, mx = -1, mxT = 0; for (auto& n : ph) { mn = std::min (mn, n.p); if (n.p > mx) { mx = n.p; mxT = n.t - t0; } }
        S.spanSum += mx - mn; if (mx - mn > 17) { ++S.spanOver; if (why) *why = "span " + std::to_string (mx - mn); }
        if (mxT < kEmoFeltBar || mxT >= 3 * kEmoFeltBar) { ++S.peakOff; if (why) *why = "peak at " + std::to_string (mxT / 48); }
        bool pick = false; for (auto& n : ph) if (n.t - t0 >= 3 * kEmoFeltBar + 10 * 48 && n.d <= 2 * 48) pick = true;
        S.withPickup += pick;
        const Note* end = nullptr; for (auto& n : ph) if (n.t - t0 >= 3 * kEmoFeltBar && n.d >= 3 * 48) end = &n;
        if (end == nullptr) ++S.endMissing;
        else { const auto& ch = I.H.at (end->t); if (mod12 (end->p) == ch.root || ! I.H.chordTone (end->p, end->t)) { ++S.endOnRoot; if (why) *why = "end " + pn (end->p) + " over " + ch.name + " seed " + std::to_string (I.seed) + " dk " + std::to_string (I.darkness) + " at " + std::to_string (end->t / 48); } }
        for (size_t i = 1; i < ph.size(); ++i) { ++S.moves; S.steps += std::abs (ph[i].p - ph[i - 1].p) <= 2; }
    }
}
inline void engineChecks (Counter& C)
{
    {   // the shipped TRAP flavours + HIP HOP: unchanged (fingerprints measured before EMO)
        const uint64_t a = arptest::urbanFingerprint (Trap), b = arptest::urbanFingerprint (HipHop);
        std::printf ("TRAP / HIP HOP fingerprints: %016llx %016llx (shipped %016llx %016llx)\n", (unsigned long long) a, (unsigned long long) b, (unsigned long long) kUrbanPrint[0], (unsigned long long) kUrbanPrint[1]);
        C.ok (a == kUrbanPrint[0] && b == kUrbanPrint[1], "EMO: every shipped TRAP flavour (fixed, every DARKNESS, PUSH FURTHER, HOOK LAB, borrowed lanes) and HIP HOP regenerate bit-identically");
        C.ok (flavourCount (Trap) == 6 && flavourCount (HipHop) == 5 && std::string (flavourName (Trap, kEmo)) == "EMO" && std::string (flavourName (Trap, 4)) == "RATCHET", "EMO: TRAP's flavours are the shipped five + EMO (appended)");
    }
    {   // keys, progressions, tempo, groove, melody / guitar / 808 / pad rules over DARKNESS x seeds
        int ideas = 0, badKey = 0, badProg = 0, badStart = 0, vToI = 0, badTempo = 0, badGroove = 0, badMel = 0, badBass = 0, slideOver = 0, slides = 0, badArp = 0, badVoice = 0, arpNotes = 0, feltBars = 0, emoGot = 0;
        PhraseStats S; std::string why, whyM, whyP, whyA; std::set<int> progs, arps, basses, mels;
        for (int dk : { -1, 0, 30, 49, 50, 70, 100 })
            for (uint32_t sd = 1; sd <= 20; ++sd)
            {
                const Idea I = makeEmo (sd * 37 + (uint32_t) dk * 3, dk); ++ideas;
                emoGot += I.flavourUsed == kEmo;
                const bool minorRep = darknessOf (I, Trap) >= 50;
                const int K = minorRep ? mod12 (I.g.key + 3) : I.g.key;   // the major home key
                const bool keyOk = (K == 4 || K == 9 || K == 2 || K == 7) && (minorRep ? (I.g.scale == "minor" || (I.g.scale == "harmonic" && I.urbanProg == 7)) : I.g.scale == "major");
                if (! keyOk) { ++badKey; why = keyName (I.g.key, I.g.scale) + " dk " + std::to_string (dk); }
                progs.insert (I.urbanProg); arps.insert (emoArchFor (I)); basses.insert (I.urbanBassArch); mels.insert (I.urbanMelArch);
                // the chords are the progression's, written in the major key
                const auto toks = split (emoProgText (I.urbanProg), ' ');
                bool pOk = toks.size() == I.H.chords.size();
                static const std::map<std::string, int> D { { "1", 0 }, { "2", 2 }, { "3", 4 }, { "4", 5 }, { "5", 7 }, { "6", 9 }, { "7", 11 } };
                for (size_t i = 0; pOk && i < toks.size(); ++i) pOk = I.H.chords[i].root == mod12 (K + D.at (toks[i].substr (0, 1)));
                if (! pOk) { ++badProg; whyP = std::to_string (I.urbanProg); }
                const int first = mod12 (I.H.chords.front().root - K);
                if (! (first == 9 || first == 5 || (first == 0 && ! minorRep))) ++badStart;   // vi / IV (I only on the bright side)
                for (size_t i = 0; i < I.H.chords.size(); ++i)
                {   // no V -> I (wrap included)
                    const auto& a = I.H.chords[i]; const auto& b = I.H.chords[(i + 1) % I.H.chords.size()];
                    if (mod12 (a.root - K) == 7 && has (a.pcs, mod12 (a.root + 4)) && b.root == K) ++vToI;
                }
                if (I.g.bpm < 140 || I.g.bpm > 170) ++badTempo;
                if (I.groove < 0.05 - 1e-9 || I.groove > 0.10 + 1e-9) ++badGroove;
                badMel += urbantest::badMelodyNotes (I, I.lanes[LaneMelody].notes, &whyM);
                melodyPhrases (I, S, &whyM);
                const auto& bs = I.lanes[LaneBass].notes;
                std::map<int, int> perWin;
                for (size_t i = 0; i < bs.size(); ++i)
                {
                    if (bs[i].p < 24 || bs[i].p > 47) ++badBass;
                    if (bs[i].l.type == 1) { ++slides; ++perWin[bs[i].t / (4 * kEmoFeltBar)]; if (i + 1 < bs.size() && bs[i + 1].p == bs[i].p) ++badBass; }
                    else if (i + 1 < bs.size() && bs[i].t + bs[i].d > bs[i + 1].t) ++badBass;
                }
                for (auto& w : perWin) if (w.second > 1) ++slideOver;
                {   // 4-bar windows at the session tempo too
                    std::map<int, int> w4; for (auto& n : bs) if (n.l.type == 1) ++w4[n.t / (4 * BAR)];
                    for (auto& w : w4) if (w.second > 1) ++slideOver;
                }
                for (auto& n : I.lanes[LaneArp].notes) { ++arpNotes; if (n.p < 45 || n.p > 79 || (! I.H.chordTone (n.p, n.t) && ! has (I.H.keyPcs, mod12 (n.p)))) { ++badArp; whyA = pn (n.p) + " over " + I.H.at (n.t).name + " (" + emoArpArchName (emoArchFor (I)) + ")"; } }
                feltBars += I.total / kEmoFeltBar;
                for (auto& c : I.H.chords)
                {
                    int thirds = 0; for (int v : c.voicing) for (int x : { 3, 4 }) if (has (c.pcs, mod12 (c.root + x)) && mod12 (v - c.root) == x) ++thirds;
                    if (c.voicing.size() < 2 || c.voicing.front() < 43 || c.voicing.back() > 76 || thirds > 1) ++badVoice;
                }
            }
        std::printf ("EMO: %d ideas (EMO %d), keys off %d%s, progressions off %d%s, starts off %d, V->I %d, tempo off %d, groove off %d, melody notes off %d%s\n", ideas, emoGot, badKey, badKey ? (" (" + why + ")").c_str() : "", badProg, badProg ? (" (" + whyP + ")").c_str() : "", badStart, vToI, badTempo, badGroove, badMel, badMel ? (" (" + whyM + ")").c_str() : "");
        if (S.endOnRoot || S.spanOver || S.peakOff) std::printf ("  melody: %s\n", whyM.c_str());
        { std::string miss; for (int i = 0; i < emoProgCount(); ++i) if (! progs.count (i)) miss += std::to_string (i) + " "; if (! miss.empty()) std::printf ("  loops not used: %s\n", miss.c_str()); }
        std::printf ("EMO melody: %d phrases, span over 17: %d, mean span %.1f, peak outside bars 2-3: %d, ends on the root / off the chord: %d, no held end %d, with pickups %.0f%%, steps %.0f%%\n",
                     S.phrases, S.spanOver, S.spanSum / std::max (1, S.phrases), S.peakOff, S.endOnRoot, S.endMissing, 100.0 * S.withPickup / std::max (1, S.phrases), 100.0 * S.steps / std::max (1, S.moves));
        std::printf ("EMO 808: slides %d (windows over 1 per 4 bars: %d), off %d; guitar: %.1f notes per felt bar, off %d; voicings off %d%s; used: %zu / %d loops, %zu / %d guitar figures, %zu / %d 808 patterns, %zu / %d melody cells\n",
                     slides, slideOver, badBass, (double) arpNotes / std::max (1, feltBars), badArp, badVoice, badArp ? (" (" + whyA + ")").c_str() : "", progs.size(), emoProgCount(), arps.size(), emoArpArchCount(), basses.size(), emoBassArchCount(), mels.size(), emoMelArchCount());
        C.ok (emoGot == ideas && badKey == 0, "EMO: guitar-friendly keys (E / A / D / G major or C#m / F#m / Bm / Em), major below DARKNESS 50, the relative minor above");
        C.ok (badProg == 0 && badStart == 0 && vToI == 0, "EMO: chords from P1..P11 (loops start on vi / IV, on I only on the bright side; no V -> I anywhere)");
        C.ok (badTempo == 0 && badGroove == 0, "EMO: 140..170 bpm half-time (felt 70..85), GROOVE 5..10 % swing");
        C.ok (badMel == 0, "EMO: melody notes in the key / chord");
        C.ok (S.spanOver == 0 && S.peakOff == 0 && S.endOnRoot == 0 && S.endMissing == 0 && S.withPickup * 10 >= S.phrases * 6 && S.steps * 100 >= S.moves * 65,
              "EMO melody: range <= an 11th, peak in bar 2-3, ends held on the chord's 3rd / 5th (unresolved), pickups start most phrases, mostly steps");
        C.ok (slideOver == 0 && slides > 0 && badBass == 0, "EMO 808: follows the roots, mono, at most 1 slide per 4 bars (and some slides)");
        C.ok (badArp == 0 && (double) arpNotes / std::max (1, feltBars) >= 3.0 && badVoice == 0, "EMO: the guitar bed in E3..G5 on chord / key notes; SAD PAD open voicings (3rd never doubled)");
        C.ok ((int) progs.size() == emoProgCount() && (int) arps.size() == emoArpArchCount() && (int) basses.size() == emoBassArchCount() && (int) mels.size() == emoMelArchCount(),
              "EMO: every loop (P1..P11), guitar figure (GA), 808 pattern (EB1..EB10) and melody cell shows up");
    }
    {   // DARKNESS: 0 = brighter, major-leaning (I / IV starts allowed, a higher melody), 100 = darker minor (minor colours, lower)
        double p0 = 0, p1 = 0; int n0 = 0, n1 = 0, major0 = 0, minor1 = 0, ideasN = 0, minorSide0 = 0, minorSide1 = 0;
        for (uint32_t sd = 1; sd <= 30; ++sd)
        {
            const Idea a = makeEmo (sd * 11, 0), b = makeEmo (sd * 11, 100); ++ideasN;
            major0 += a.g.scale == "major"; minor1 += b.g.scale == "minor" || b.g.scale == "harmonic";
            for (int pi : { 1, 3, 5, 7 }) { minorSide0 += a.urbanProg == pi; minorSide1 += b.urbanProg == pi; }
            for (auto& n : a.lanes[LaneMelody].notes) { p0 += n.p; ++n0; }
            for (auto& n : b.lanes[LaneMelody].notes) { p1 += n.p; ++n1; }
        }
        std::printf ("EMO DARKNESS 0 -> 100: major %d -> minor %d of %d, minor-colour loops %d -> %d, mean melody pitch %.1f -> %.1f\n", major0, minor1, ideasN, minorSide0, minorSide1, p0 / std::max (1, n0), p1 / std::max (1, n1));
        C.ok (major0 == ideasN && minor1 == ideasN && minorSide1 > minorSide0 && p0 / std::max (1, n0) > p1 / std::max (1, n1) + 2, "EMO DARKNESS: low = brighter major-leaning, high = darker minor (minor-colour loops, a lower melody)");
    }
    {   // TRAP AUTO: GENERATE rotates DARK / DRILL / EMO: never the same style twice in a row, >= 4 of the 8 settings different
        std::vector<RecentIdea> hist; int same = 0, near4 = 0, near3 = 0, n = 0; std::map<int, int> got; double minD = 9, sumD = 0; int tries = 0;
        for (uint32_t s = 1; s <= 60; ++s)
        {
            std::vector<RecentIdea> rec (hist.size() > 3 ? hist.end() - 3 : hist.begin(), hist.end());
            Idea X; X.mode = Trap; X.seed = s * 7717u; applyModeDefaults (X); tries += newIdeaFurther (X, rec);
            const RecentIdea me = recentOf (X); ++got[me.flavour];
            if (! hist.empty())
            {
                ++n; same += me.flavour == hist.back().flavour; const int d = rotationDistance (me, hist.back()); near4 += d < 4; minD = std::min (minD, (double) d); sumD += d;
                if (hist.size() >= 2) near3 += rotationDistance (me, hist[hist.size() - 2]) < 3;
            }
            hist.push_back (me);
        }
        std::printf ("TRAP AUTO rotation (60 GENERATEs): DARK %d DRILL %d EMO %d others %d; same style twice %d, < 4 settings apart %d, < 3 from the one before %d; settings apart min %.0f mean %.2f; tries %.1f per idea\n",
                     got[0], got[3], got[kEmo], 60 - got[0] - got[3] - got[kEmo], same, near4, near3, minD, sumD / std::max (1, n), tries / 60.0);
        C.ok (same == 0 && near4 == 0 && near3 == 0 && got[0] > 5 && got[3] > 5 && got[kEmo] > 5 && got[0] + got[3] + got[kEmo] == 60,
              "TRAP AUTO: GENERATE rotates DARK / DRILL / EMO (never the same twice in a row, >= 4 of the 8 rotation settings apart, >= 3 from the one before)");
        int autoOk = 0; for (uint32_t s = 1; s <= 40; ++s) { Idea I; I.mode = Trap; I.seed = s; applyModeDefaults (I); newIdea (I); autoOk += I.flavourUsed == 0 || I.flavourUsed == 3 || I.flavourUsed == kEmo; }
        Idea x; x.mode = Trap; x.seed = 4242; applyModeDefaults (x); { std::vector<RecentIdea> rec { hist[hist.size() - 2], hist.back() }; newIdeaFurther (x, rec); }
        Idea y; y.mode = Trap; y.seed = x.seed; applyModeDefaults (y); newIdea (y);
        C.ok (autoOk == 40 && arptest::ideaFingerprint (x) == arptest::ideaFingerprint (y), "TRAP AUTO: the style comes from the seed (a rotated idea comes back exactly from its seed)");
        // EMO fixed: two EMO ideas in a row move the loop / first chord / guitar figure
        std::vector<RecentIdea> rec; int sameStart = 0, k = 0;
        for (uint32_t s = 1; s <= 20; ++s) { Idea X; X.mode = Trap; X.flavour = kEmo; X.seed = s * 313u; applyModeDefaults (X); newIdeaFurther (X, rec); const auto me = recentOf (X); if (! rec.empty()) { ++k; sameStart += me.firstChord == rec.back().firstChord || me.prog == rec.back().prog; } rec.push_back (me); if (rec.size() > 3) rec.erase (rec.begin()); }
        std::printf ("EMO after EMO (20 GENERATEs): same loop or first chord %d / %d\n", sameStart, k);
        C.ok (sameStart <= 2, "EMO: consecutive EMO ideas change the loop and never start on the same chord (PUSH FURTHER)");
    }
    {   // HOOK LAB: EMO appended for TRAP (old indices unchanged), writes the singable melody
        const bool idx = styleCountFor (Trap) == 10 && styleAt (Trap, 9) == StyleEmo && styleButton (Trap, StyleEmo) == 9 && styleAt (Trap, 8) == StyleHypnoLoop && styleCountFor (HipHop) == 9
                         && styleButton (HipHop, StyleEmo) == 0 && styleAt (HipHop, 8) == StyleLaidBack && std::string (styleName (StyleEmo)) == "EMO" && StyleLaidBack == 11 && baseStyle (StyleEmo) == StyleEmotional;
        int bad = 0, n = 0; double d = 0; int k = 0;
        for (uint32_t s : { 31u, 77u })
        {
            const Idea I = urbantest::make (Trap, s, 0);   // a DARK idea: the EMO style still writes EMO hooks
            auto hp = hookParams (I, 8, 4321 + s); hp.count = 6; hp.style = StyleEmo;
            const auto hooks = generateHooks (I, hp); n += (int) hooks.size();
            for (auto& h : hooks) bad += urbantest::badMelodyNotes (I, h.notes);
            for (size_t i = 0; i < hooks.size(); ++i) for (size_t j = i + 1; j < hooks.size(); ++j) { d += melodyDistance (hooks[i].notes, I.H.key, hooks[j].notes, I.H.key); ++k; }
        }
        const Idea E = makeEmo (515); auto hp = hookParams (E, 8, 99); hp.count = 6; const auto he = generateHooks (E, hp);
        int badE = 0; for (auto& h : he) badE += urbantest::badMelodyNotes (E, h.notes);
        std::printf ("HOOK LAB EMO: %d hooks (style EMO on DARK ideas), notes off %d, distance %.3f; an EMO idea's AUTO hooks %zu, off %d\n", n, bad, k ? d / k : 0.0, he.size(), badE);
        C.ok (idx, "HOOK LAB: EMO appended for TRAP after the classic six + DARK / TRIUMPHANT / HYPNO LOOP (HIP HOP's three unchanged)");
        C.ok (n >= 10 && bad == 0 && k && d / k > 0.5 && he.size() >= 5 && badE == 0, "HOOK LAB EMO: singable hooks, in the key, varied");
    }
    {   // the processor: EMO sounds, MORE > FLAVOUR, state round trip
        ArpProcessor p; p.persistTaste = false; p.generate (Trap, true, false);
        p.change ([] (Idea& X) { X.flavour = kEmo; X.seed = 2024; newIdea (X); }, false); p.applyFlavourLanes (false);
        auto ch = [&] (ArpProcessor& q, const char* id) { return (int) std::lround (q.apvts.getRawParameterValue (id)->load()); };
        const bool snd = ch (p, "preset_arp") == 1 + arpsyn::GuitarPluck && ch (p, "preset_chords") == 1 + arpsyn::SadPad && ch (p, "preset_bass") == 1 + arpsyn::Bass808
                         && ch (p, "preset_melody") == (emoLead (p.idea()) ? 1 + arpsyn::GuitarPluck : 0);
        juce::MemoryBlock mb; p.getStateInformation (mb);
        ArpProcessor q; q.persistTaste = false; q.setStateInformation (mb.getData(), (int) mb.getSize());
        const auto& a = p.idea(); const auto& b = q.idea();
        bool same = a.flavour == kEmo && b.flavour == kEmo && b.flavourUsed == kEmo && a.g.bpm == b.g.bpm && std::abs (a.groove - b.groove) < 1e-9 && a.g.key == b.g.key && a.urbanProg == b.urbanProg;
        for (int l = 0; l < kLanes && same; ++l) { same = a.lanes[l].notes.size() == b.lanes[l].notes.size(); for (size_t i = 0; same && i < a.lanes[l].notes.size(); ++i) same = a.lanes[l].notes[i].t == b.lanes[l].notes[i].t && a.lanes[l].notes[i].p == b.lanes[l].notes[i].p && a.lanes[l].notes[i].d == b.lanes[l].notes[i].d && a.lanes[l].notes[i].l.type == b.lanes[l].notes[i].l.type; }
        for (size_t i = 0; same && i < a.H.chords.size(); ++i) same = i < b.H.chords.size() && a.H.chords[i].voicing == b.H.chords[i].voicing;
        same = same && ch (q, "preset_arp") == 1 + arpsyn::GuitarPluck && ch (q, "preset_chords") == 1 + arpsyn::SadPad;
        // back to DARK: the lanes still on the mood's sounds follow (bells, the classic chords, the flute)
        p.change ([] (Idea& X) { X.flavour = 0; newIdea (X); }, false); p.applyFlavourLanes (false);
        const bool back = ch (p, "preset_arp") == 1 + arpsyn::Bell && ch (p, "preset_chords") == 0 && ch (p, "preset_melody") == 0;
        C.ok (snd && back, "EMO SOUNDS: CLEAN GUITAR PLUCK guitar bed, SAD PAD chords, the 808, a soft lead or the guitar on the melody (and back for the other flavours)");
        C.ok (same, "STATE: an EMO idea (flavour, tempo, groove, notes, voicings, sounds) round-trips exactly");
    }
    {   // level: EMO as GENERATE sets it up, against the old moods (BS.1770)
        double old = 0; for (int m = 0; m < 3; ++m) for (uint32_t s : { 11u, 515u }) old += soundtest::lufs (moodRender (m, s, 48000), 48000);
        old /= 6; double l = 0; int n = 0;
        for (uint32_t s : { 11u, 515u, 2024u, 128u })
        {
            ArpProcessor p; p.persistTaste = false; p.generate (Trap, false, false);
            p.change ([s] (Idea& X) { X.flavour = kEmo; X.seed = s; newIdea (X); }, false); p.applyFlavourLanes (false);
            RenderOpts o; o.bars = 8; o.sr = 48000; o.bpm = p.idea().g.bpm; std::vector<MidiEv> ev; int byp = 0;
            l += soundtest::lufs (render (p, o, ev, byp), 48000); ++n;
        }
        l /= n;
        if (std::getenv ("EMO_SOLO"))
            for (int lane = 0; lane < kLanes; ++lane)
            {
                ArpProcessor p; p.persistTaste = false; p.generate (Trap, false, false);
                p.change ([lane] (Idea& X) { X.flavour = kEmo; X.seed = 11; newIdea (X); for (int k = 0; k < kLanes; ++k) X.lanes[k].mute = k != lane; }, false); p.applyFlavourLanes (false);
                RenderOpts o; o.bars = 8; o.sr = 48000; o.bpm = p.idea().g.bpm; std::vector<MidiEv> ev; int byp = 0;
                std::printf ("  EMO solo lane %d: %.1f LUFS\n", lane, soundtest::lufs (render (p, o, ev, byp), 48000));
            }
        std::printf ("loudness TRAP EMO %.1f LUFS vs the old moods %.1f\n", l, old);
        C.ok (std::abs (l - old) <= 1.5, "EMO: level-matched to the old moods within 1.5 LU");
    }
}
// TRAP_EMO.wav (8 bars) + TRAP_EMO.mid + TRAP_EMO_hook.mid (a HOOK LAB hook in the EMO style, the melody lane)
inline void renders (const juce::File& dir)
{
    dir.createDirectory();
    ArpProcessor p; p.persistTaste = false;
    p.generate (Trap, false, false);
    p.change ([] (Idea& X) { X.seed = 1000 + (uint32_t) kEmo * 37; X.flavour = kEmo; newIdea (X); }, false);
    p.applyFlavourLanes (true);
    const Idea& I = p.idea();
    const auto wav = renderIdea (p, dir, "TRAP_EMO", 8, I.g.bpm);
    const auto mid = ideaMidi (I, 15, false, I.g.bpm); dir.getChildFile ("TRAP_EMO.mid").replaceWithData (mid.data(), mid.size());
    auto hp = hookParams (I, 8, 777); hp.count = 4; hp.style = StyleEmo;
    const auto hooks = generateHooks (I, hp);
    Idea H = I; if (! hooks.empty()) H.lanes[LaneMelody].notes = hooks.front().notes;
    const auto hm = ideaMidi (H, 1 << LaneMelody, false, I.g.bpm); dir.getChildFile ("TRAP_EMO_hook.mid").replaceWithData (hm.data(), hm.size());
    std::string ch; for (auto& c : I.chords) ch += c.name + " ";
    std::printf ("render TRAP_EMO: %s %d bpm (felt %d) groove %.2f, loop %s, chords %s| 808 '%s', guitar '%s', melody '%s' (lead %s), hook '%s' -> %s\n", keyName (I.g.key, I.g.scale).c_str(), I.g.bpm, I.g.bpm / 2, I.groove,
                 emoProgName (I.urbanProg), ch.c_str(), urbanBassArchName (Trap, I.urbanBassArch, kEmo), emoArpArchName (emoArchFor (I)), urbanMelArchName (Trap, I.urbanMelArch, kEmo), emoLead (I) ? "CLEAN GUITAR PLUCK" : "TRAP FLUTE",
                 hooks.empty() ? "-" : hooks.front().label.c_str(), wav.getFullPathName().toRawUTF8());
}
} // namespace emotest
