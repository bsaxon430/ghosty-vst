#pragma once
// Mr. Mickens engine (real-time safe).
//   4 BREAK BAYS: each break is cut into 16th slices; every 16th of the host grid triggers a slice, so breaks lock
//                 to any tempo. NUDGE shifts the start (snapped to a grid); RANDOM START picks a new start every
//                 bar; RANDOM CHOPS swaps slices at random. Each bay ducks from the kick / snare / hats on its own.
//   MICKENS 2:    BREAK DNA + REMIX decide what each bay plays per 16th (Remix.h); STEP LOCKS keep single 16ths;
//                 GENERATE FILL; INTERLOCK roles with bay-to-bay ducking around the main bay's hits; performance
//                 STUTTER (Stutter.h); FILTER ROUTING (FilterRoute.h). With all of that off, the classic engine runs
//                 exactly as before (sessions from before Mickens 2 are bit-identical: they keep linear reads, the
//                 classic filters / stutter and the classic output stage).
//   DRUMS:        kick, snare, hat, open hat on a 16-step grid (hard-hitting sounds from the Ghosty library).
//   BREAK FX:     crunch (2x oversampled in new sessions) -> filters -> chorus -> stutter -> reverb.
//   GAIN STAGING: every level / pan / volume glides over 10 ms (no per-block steps); the output soft clip is a safety
//                 above 0.98 only; peak + clip meters for the UI.
#include "BreakLib.h"
#include "MkDrumKit.h"
#include "Remix.h"
#include "Stutter.h"
#include "FilterRoute.h"

namespace mk {

constexpr int kBays = 4, kDrums = 4, kSteps = 16;
enum Interp { kLoFi = 0, kClean = 1, kHQ = 2 };   // linear / 4-point Hermite / 8-point windowed sinc

struct BayParams { int breakIndex = 0; int nudge = 0, grid = 1; float pitch = 0, level = 0.7f, decay = 1, pan = 0; bool rndStart = false, rndChops = false; float rndAmt = 0.3f, duck[3] {}; bool mute = false, fit = true; uint32_t seed = 0;
                   RemixParams rx; int role = kFree; };
struct FxParams {
    float crunch = 0, bits = 0;
    float dry = 1, lpCut = 0.4f, lpMix = 0, hpCut = 0.5f, hpMix = 0, bpCut = 0.5f, bpMix = 0, notchCut = 0.5f, notchMix = 0, combCut = 0.4f, combMix = 0, formant = 0.3f, formMix = 0, res = 0.5f;
    float chorus = 0, reverb = 0, size = 0.6f;
    bool stutter = false; int stutDiv = 2; float stutCut = 0.6f, stutEnv = 0.6f, stutDecay = 0.4f;
    float drumLvl[kDrums] { 1, 1, 0.8f, 0.7f }; bool drumMute[kDrums] {}; bool drumsMute = false; int keysBay = 0; float duckRelease = 0.18f, breaksLevel = 0.85f, drumsLevel = 0.7f, volume = 0.5f; float bpm = 172;
    // Mickens 2
    int fillStyle = kClassic, fillEvery = 0, fillLen16 = 8; float fillInt = 0.6f; uint32_t fillSeed = 0;
    int ilMode = kIlOff; float ilDuck = 0.5f;
    StutterParams st; bool oldStutter = false;
    FilterParams flt; bool oldFilters = false;
    int interp = kClean; bool legacyOut = false;   // legacyOut: the pre-Mickens-2 output stage (x * volume * 1.2 into tanh above 0.9), linear reads, no oversampling
};
struct Snapshot {
    std::array<const Break*, kBays> brk {};
    std::array<std::array<uint8_t, kSteps>, kDrums> drums {};   // velocity 0..127
    std::array<const gb::Sound*, kDrums> drumSound {};
    std::array<const BreakDNA*, kBays> dna {};      // the live break's DNA
    std::array<const BreakDNA*, kBays> groove {};   // FREEZE DNA: the frozen groove (nullptr = the live one)
    std::array<const StepLocks*, kBays> locks {};   // STEP LOCKS (nullptr = none)
    uint64_t gen = 0;                                // publish generation: the owner frees snapshots the audio thread has moved past
};

// ---- sample reads: linear (the classic engine, kept bit-identical), Hermite, 8-point windowed sinc
namespace interp_detail {
constexpr int kSincTaps = 16, kSincPhases = 512;
inline const std::vector<float>& sincTable() {
    static const std::vector<float> T = [] {
        std::vector<float> t((size_t) (kSincPhases + 1) * kSincTaps);
        for (int p = 0; p <= kSincPhases; ++p) { const double f = p / (double) kSincPhases; double sum = 0;
            for (int k = 0; k < kSincTaps; ++k) { const double x = (k - (kSincTaps / 2 - 1)) - f; const double s = x == 0 ? 1.0 : std::sin(juce::MathConstants<double>::pi * x) / (juce::MathConstants<double>::pi * x);
                const double u = juce::MathConstants<double>::pi * (x / (kSincTaps / 2) + 1), w = 0.35875 - 0.48829 * std::cos(u) + 0.14128 * std::cos(2 * u) - 0.01168 * std::cos(3 * u);   // Blackman-Harris
                t[(size_t) (p * kSincTaps + k)] = (float) (s * w); sum += s * w; }
            for (int k = 0; k < kSincTaps; ++k) t[(size_t) (p * kSincTaps + k)] /= (float) sum; }
        return t; }();
    return T;
}
}
inline float readSample(const float* d, int len, double pos, int mode) {
    const int idx = (int) pos; const float t = (float) (pos - idx);
    if (mode == kLoFi || len < 8) return d[idx] + (d[juce::jmin(idx + 1, len - 1)] - d[idx]) * t;
    if (mode == kClean) {
        const float xm1 = d[juce::jmax(0, idx - 1)], x0 = d[idx], x1 = d[juce::jmin(idx + 1, len - 1)], x2 = d[juce::jmin(idx + 2, len - 1)];
        const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2 * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * t + c2) * t + c1) * t + x0;
    }
    using namespace interp_detail; const auto& T = sincTable(); const float fp = t * kSincPhases; const int ph = juce::jmin(kSincPhases - 1, (int) fp); const float pt = fp - ph;
    const float* w0 = T.data() + ph * kSincTaps; const float* w1 = w0 + kSincTaps; float s = 0;
    for (int k = 0; k < kSincTaps; ++k) s += (w0[k] + (w1[k] - w0[k]) * pt) * d[juce::jlimit(0, len - 1, idx - (kSincTaps / 2 - 1) + k)];
    return s;
}

class Engine {
public:
    void prepare(double rate, int block) {
        sr = rate; maxBlock = juce::jmax(64, block);
        busBuf.setSize(2, maxBlock); drumBufs.setSize(kDrums * 2, maxBlock); scratch.setSize(2, maxBlock);
        juce::dsp::ProcessSpec spec { rate, (juce::uint32) maxBlock, 2 };
        chorus.prepare(spec); chorus.setRate(0.9f); chorus.setCentreDelay(7.0f); chorus.setFeedback(0.15f);
        reverb.prepare(spec);
        combBuf[0].assign((size_t) (rate / 25.0) + 4, 0.0f); combBuf[1] = combBuf[0]; combPos = 0;
        stutBuf[0].assign((size_t) (rate * 1.0), 0.0f); stutBuf[1] = stutBuf[0];
        for (auto& b : bays) b = Bay();
        for (auto& v : dv) v = DV();
        for (int e = 0; e < 3; ++e) { duckCurve[e].assign((size_t) maxBlock, 0.0f); duckHit[e].assign((size_t) maxBlock, 0); duckEnv[e] = 0; duckAtk[e] = 0; }
        bayDuckCurve.assign((size_t) maxBlock, 0.0f); bayDuck = BayDuck();
        for (auto& c : curves) c.assign((size_t) maxBlock, 0.0f);
        for (auto& s : sm) s = Sm();
        stut.prepare(rate); router.prepare(rate);
        os.reset(); os.initProcessing((size_t) maxBlock); osLat = (int) std::lround(os.getLatencyInSamples());
        (void) interp_detail::sincTable();
        clock = 0; absClock = 0; tlogN = 0; manStart = manEnd = -1; lastB0 = -1e9; firstBlock = true; fxFade = stFade = 0; lastOldF = lastOldSt = -1;
        fadeLen = juce::jmax(16, (int) (0.010 * rate)); audGain = 1; audBrk = nullptr;
        meterL.store(0); meterR.store(0); clipCount.store(0);
    }
    void setSnapshot(const Snapshot* s) { snap.store(s); }
    const Snapshot* snapshot() const { return snap.load(); }
    double clockBeats() const { return clock; }   // the internal clock (audio thread)
    void restart() { restartFlag.store(true); }   // message thread: start the internal clock from bar 1
    void restartAt(double beats) { restartBeats.store(beats); restartFlag.store(true); }   // offline renders: pre-roll from a negative bar
    std::atomic<bool> restartFlag { false }; std::atomic<double> restartBeats { 0 };
    std::atomic<bool> playing { false };
    std::atomic<int> uiStep { -1 }; std::atomic<int> uiSlice[kBays] { { -1 }, { -1 }, { -1 }, { -1 } };
    std::atomic<uint32_t> drumHits[kDrums] {};
    std::atomic<bool> uiFill { false }, uiStutter { false };
    std::atomic<float> meterL { 0 }, meterR { 0 }; std::atomic<int> clipCount { 0 };   // output peak per block, samples that hit the safety clipper
    std::atomic<uint64_t> usedGen { 0 };   // generation of the snapshot the last block used (set when the block is done)
    // pads from the UI (message thread -> audio thread): 0 drum hit, 1 bay hold on, 2 bay hold off, 3 slice of the KEYS bay, 4 GENERATE FILL now
    void pushPad(int kind, int idx, float vel) { int s1, n1, s2, n2; padFifo.prepareToWrite(1, s1, n1, s2, n2); if (n1 > 0) { padEv[s1] = { kind, idx, vel }; padFifo.finishedWrite(1); } }
    // for stems: which parts to output (bit 0 = breaks, bits 1..4 = drums)
    uint32_t outMask = 0xffffffffu;
    // BREAK AUDITION (the browser): a break plays once on its own, in time (FIT TEMPO at the current bpm), the mix ducks -9 dB meanwhile
    std::atomic<const Break*> audition { nullptr }; std::atomic<bool> auditionStart { false };
    // test hooks: every bay trigger (sample position since prepare)
    struct TrigLog { long long at; int bay, slice; bool fill, resolve; };
    TrigLog tlog[8192]; int tlogN = 0; long long absClock = 0;
    Stutter stut; FilterRouter router;
    static constexpr int kFillNote = 52, kStutterNote = 53;   // E2 = GENERATE FILL, F2 = hold to STUTTER
    // BYPASS engaged: every voice, pending hit, fill, stutter repeat and effect tail stops (audio thread, no allocation)
    void stopAll() {
        for (auto& b : bays) { b.v[0].brk = b.v[1].brk = b.pad.brk = nullptr; b.nPend = 0; }
        for (auto& v : dv) v.s = nullptr;
        for (auto& bt : bayTrigN) bt = 0; nHits = 0; manStart = manEnd = -1; audBrk = nullptr;
        for (int e = 0; e < 3; ++e) { duckEnv[e] = 0; duckAtk[e] = 0; std::fill(duckHit[e].begin(), duckHit[e].end(), (uint8_t) 0); } bayDuck = BayDuck(); nDuckHits = 0;
        stut.reset(); router.reset(); chorus.reset(); reverb.reset(); os.reset(); fxFade = stFade = 0;
        for (auto& c : combBuf) std::fill(c.begin(), c.end(), 0.0f); stutWas = false; lp = hp = bp = notch = stutF = TPT(); for (auto& f : form) f = TPT();
        for (int b = 0; b < kBays; ++b) uiSlice[b].store(-1); uiStutter.store(false); uiFill.store(false); meterL.store(0); meterR.store(0);
    }
    // BYPASS held / PLAY ON NOTES with no note: nothing renders, but the internal clock keeps counting so the restart lands in time;
    // on-screen pad presses made meanwhile are dropped (never fired late when the sound comes back)
    void idle(int n, double bpm) { const double b = juce::jlimit(20.0, 999.0, bpm); if (playing.load()) clock += n / (sr * 60.0 / juce::jmax(40.0, b)); absClock += n; uiStep.store(-1);
        int s1, n1, s2, n2; padFifo.prepareToRead(padFifo.getNumReady(), s1, n1, s2, n2); padFifo.finishedRead(n1 + n2); }

    void process(juce::AudioBuffer<float>& out, const std::array<BayParams, kBays>& B, const FxParams& F, double hostBpm, double hostPpq, bool hostPlaying, const juce::MidiBuffer& midi) {
        const int n = out.getNumSamples(); out.clear();
        const Snapshot* S = snap.load(); if (!S) { absClock += n; return; }
        const double bpm = juce::jlimit(20.0, 999.0, hostPlaying ? hostBpm : (double) F.bpm); curBpm = bpm; for (int d = 0; d < kDrums; ++d) muted[d] = F.drumsMute || F.drumMute[d]; const double spb = sr * 60.0 / juce::jmax(40.0, bpm);
        if (restartFlag.exchange(false)) { clock = restartBeats.exchange(0.0); for (auto& b : bays) b.lastBar = -1; manStart = manEnd = -1; }
        const bool run = hostPlaying || playing;
        const double b0 = hostPlaying ? hostPpq : clock, b1 = b0 + n / spb;
        if (!std::isfinite(b0)) { absClock += n; return; }
        // the transport jumped back (loop / relocate): forget per-bar state and pending fills
        if (b0 < lastB0 - 0.01) { manStart = manEnd = -1; for (auto& b : bays) b.lastBar = -1; }
        lastB0 = b1;
        busBuf.clear(0, n); drumBufs.clear(0, n);
        nHits = 0; for (auto& bt : bayTrigN) bt = 0;
        curB = &B; curF = &F; curS = S; curSpb = spb; curB0 = b0;
        buildCurves(B, F, n);
        int stutOn = -1, stutOff = -1;
        // pads pressed on screen
        { int s1, n1, s2, n2; padFifo.prepareToRead(padFifo.getNumReady(), s1, n1, s2, n2);
          auto run1 = [&](int st, int cnt) { for (int i = 0; i < cnt; ++i) { if (padEv[st + i].kind == 4) triggerFill(F, b0 + 0.0, run); else pad(S, B, F, padEv[st + i].kind, padEv[st + i].idx, padEv[st + i].vel, 0); } };
          run1(s1, n1); run1(s2, n2); padFifo.finishedRead(n1 + n2); }
        // live MIDI: C1 D1 F#1 A#1 = kick snare hat open hat; C2..D#2 = hold bay 1..4; E2 = fill; F2 = stutter; C3 and up = slices of the KEYS bay
        for (const auto md : midi) { const auto m = md.getMessage(); const int nn = m.getNoteNumber(), at = juce::jlimit(0, n - 1, md.samplePosition);
            if (m.isNoteOn()) { const int d = nn == 36 ? 0 : nn == 38 ? 1 : nn == 42 ? 2 : nn == 46 ? 3 : -1;
                if (d >= 0) pad(S, B, F, 0, d, m.getFloatVelocity(), at);
                else if (nn >= 48 && nn <= 51) pad(S, B, F, 1, nn - 48, m.getFloatVelocity(), at);
                else if (nn == kFillNote) triggerFill(F, b0 + at / spb, run);
                else if (nn == kStutterNote) stutOn = at;
                else if (nn >= 60 && nn < 60 + 64) pad(S, B, F, 3, nn - 60, m.getFloatVelocity(), at); }
            else if (m.isNoteOff() && nn >= 48 && nn <= 51) pad(S, B, F, 2, nn - 48, 0, at);
            else if (m.isNoteOff() && nn == kStutterNote) stutOff = at;
            else if (m.isAllNotesOff() || m.isAllSoundOff()) for (int b = 0; b < kBays; ++b) pad(S, B, F, 2, b, 0, at); }
        // pending ratchets / delayed hits from the last block
        for (int b = 0; b < kBays; ++b) { auto& bay = bays[b]; int keep = 0;
            for (int i = 0; i < bay.nPend; ++i) { auto t = bay.pend[i]; t.at -= lastN; if (t.at < n) addTrig(b, t); else bay.pend[keep++] = t; }
            bay.nPend = keep; }
        lastN = n;
        const int mainBay = mainBayIndex(B);
        if (run) {
            // every 16th that starts in this block
            for (long s = (long) std::ceil(b0 * 4.0 - 1e-9); s < b1 * 4.0; ++s) {
                const int at = juce::jlimit(0, n - 1, (int) ((s / 4.0 - b0) * spb));
                const int st = (int) (((s % kSteps) + kSteps) % kSteps);
                uiStep.store(st);
                for (int d = 0; d < kDrums; ++d) if (const uint8_t v = S->drums[(size_t) d][(size_t) st]) trigDrum(S, d, v / 127.0f, at);
                const StepPlan mainPlan = planFor(mainBay, s);
                for (int b = 0; b < kBays; ++b) stepBay(S, B[(size_t) b], b, s, at, b == mainBay ? mainPlan : planFor(b, s, &mainPlan, mainBay));
            }
            uiFill.store(inFill(F, (long) std::floor(b0 * 4.0)));
            // bay-to-bay ducking: the main bay's hits, looked ahead so the duck opens just before each transient
            if (F.ilMode != kIlOff && F.ilMode != kCollide && F.ilDuck > 0.001f) scheduleBayDucks(B, mainBay, b0, b1, spb, n);
            if (!hostPlaying) clock = b1;
        } else { uiStep.store(-1); uiFill.store(false); }

        // render bays (with their own ducking) into the break bus, drums into their own buffers
        const float rel = std::exp(-1.0f / (juce::jmax(0.02f, F.duckRelease) * (float) sr));
        buildDucks(n, rel);
        buildBayDuck(n, rel);
        for (int b = 0; b < kBays; ++b) renderBayBlock(B[(size_t) b], b, n, b != mainBay && F.ilMode != kIlOff && F.ilMode != kCollide && B[(size_t) b].role != kFree && B[(size_t) b].role != kMain ? F.ilDuck : 0.0f, F.interp);
        renderDrums(n, F);
        // the hits that drive the filters' HIT SWEEP: the main bay's triggers + the drum machine, in time order
        std::sort(hits, hits + nHits, [](const FilterHit& a, const FilterHit& c) { return a.at < c.at; });
        breakFx(F, n, spb, b0, run, stutOn, stutOff);
        uiStutter.store(stut.isActive() || (F.oldStutter && F.stutter));
        // mix
        float* L = out.getWritePointer(0); float* R = out.getWritePointer(1);
        const float* brkC = curves[kCBreaks].data(); const float* drmC = curves[kCDrums].data(); const float* volC = curves[kCVolume].data();
        if (outMask & 1u) for (int i = 0; i < n; ++i) { L[i] += busBuf.getSample(0, i) * brkC[i]; R[i] += busBuf.getSample(1, i) * brkC[i]; }
        for (int d = 0; d < kDrums; ++d) if (outMask & (2u << d)) for (int i = 0; i < n; ++i) { L[i] += drumBufs.getSample(d * 2, i) * drmC[i]; R[i] += drumBufs.getSample(d * 2 + 1, i) * drmC[i]; }
        renderAudition(L, R, n, F.interp);
        float pkL = 0, pkR = 0; int clips = 0;
        if (F.legacyOut) for (int i = 0; i < n; ++i) { L[i] = soft(L[i] * volC[i] * 1.2f); R[i] = soft(R[i] * volC[i] * 1.2f); pkL = juce::jmax(pkL, std::abs(L[i])); pkR = juce::jmax(pkR, std::abs(R[i])); }
        else for (int i = 0; i < n; ++i) { const float a = L[i] * volC[i], b = R[i] * volC[i]; if (std::abs(a) > 0.98f || std::abs(b) > 0.98f) ++clips; L[i] = safe(a); R[i] = safe(b); pkL = juce::jmax(pkL, std::abs(L[i])); pkR = juce::jmax(pkR, std::abs(R[i])); }
        meterL.store(pkL); meterR.store(pkR); if (clips) clipCount.fetch_add(clips);
        absClock += n; firstBlock = false; usedGen.store(S->gen);
    }

    // ---- pure helpers (also used by the tests and the UI)
    static int classicSlice(const Break* br, const BayParams& P, int b, long s) {
        static const int G[] = { 1, 2, 4, 8, 16 };
        const int grid = G[juce::jlimit(0, 4, P.grid)]; const long bar = s / 16;
        int barOffset = 0;
        if (P.rndStart) { const int count = juce::jmax(1, br->slices / grid); barOffset = juce::jmin(count - 1, (int) (hrand((uint32_t) bar * 977u + (uint32_t) b * 131u + P.seed) * (float) count)) * grid; }
        int slice = (int) (((s + (long) P.nudge * grid + barOffset) % br->slices + br->slices) % br->slices);
        if (P.rndChops && hrand((uint32_t) s * 1013u + (uint32_t) b * 71u + P.seed + 1u) < P.rndAmt) slice = (int) (hrand((uint32_t) s * 733u + (uint32_t) b * 29u + P.seed + 2u) * (float) br->slices) % br->slices;
        return slice;
    }
    static bool autoFill(const FxParams& F, long s) {
        if (F.fillEvery <= 0) return false; static const int E[] = { 0, 4, 8, 16 };
        const int every = E[juce::jlimit(0, 3, F.fillEvery)]; const long bar = s >= 0 ? s / 16 : (s - 15) / 16; const int st = (int) (((s % 16) + 16) % 16);
        return ((bar + 1) % every) == 0 && st >= 16 - juce::jlimit(1, 16, F.fillLen16);
    }
    static bool inFillAt(const FxParams& F, long s, long mS, long mE) { return autoFill(F, s) || (s >= mS && s < mE); }
    static long fillStartAt(const FxParams& F, long s, long mS, long mE) { if (s >= mS && s < mE) return mS; const long bar = s >= 0 ? s / 16 : (s - 15) / 16; return bar * 16 + 16 - juce::jlimit(1, 16, F.fillLen16); }
    bool inFill(const FxParams& F, long s) const { return inFillAt(F, s, manStart, manEnd); }
    long fillStartOf(const FxParams& F, long s) const { return fillStartAt(F, s, manStart, manEnd); }
    long manualFillStart() const { return manStart; }
    long manualFillEnd() const { return manEnd; }
    static int mainBayIndex(const std::array<BayParams, kBays>& B) { for (int b = 0; b < kBays; ++b) if (B[(size_t) b].role == kMain) return b; return 0; }

    // what bay b plays on step s (pure: same inputs, same answer). mS/mE: a manual fill window (-1 = none)
    static StepPlan planAt(const std::array<BayParams, kBays>& B, const FxParams& F, const Snapshot* S, int b, long s, long mS, long mE, const StepPlan* mainPlan, int mainBay) {
        const auto& P = B[(size_t) b];
        const Break* br = S->brk[(size_t) b]; StepPlan p; if (!br || br->data.empty() || P.mute) return p;
        const int pos = classicSlice(br, P, b, s);
        const BreakDNA* d = S->dna[(size_t) b]; const BreakDNA* g = S->groove[(size_t) b];
        if (d && d->steps != br->slices) d = nullptr;
        RemixParams rx = P.rx; if (!g) rx.freeze = false;
        p = d ? planStep(g ? g : d, d, rx, pos, s) : StepPlan();
        if (!d) p.slice = pos;
        const bool ilOn = F.ilMode != kIlOff;
        const bool fillsHere = !ilOn || P.role == kFree || P.role == kMain || P.role == kFillsRole;
        const bool fillNow = inFillAt(F, s, mS, mE);
        if (ilOn && !fillNow) {
            if (b == mainBay) p = mainUnderInterlock(F.ilMode, p, s);
            else if (mainPlan) { const Break* mb = S->brk[(size_t) mainBay]; const BreakDNA* md = S->dna[(size_t) mainBay];
                p = interlockStep(F.ilMode, P.role, p, *mainPlan, d, md && mb && md->steps == mb->slices ? md : nullptr, s, false); }
        }
        // STEP LOCKS win over remix + interlock
        if (const StepLocks* L = S->locks[(size_t) b]) { const auto& lk = L->s[(size_t) (((s % 32) + 32) % 32)]; if (lk.on && lk.slice >= 0 && lk.slice < br->slices) p = applyLock(lk, p); }
        if (fillNow && fillsHere && d) { const long f0 = fillStartAt(F, s, mS, mE);
            const long end = (s >= mS && s < mE) ? mE : ((s >= 0 ? s / 16 : (s - 15) / 16) + 1) * 16;
            p = fillStep(d, F.fillStyle, F.fillInt, (int) (s - f0), (int) (end - f0), s, F.fillSeed + (uint32_t) b * 977u, pos); }
        else if (fillNow && ilOn && !fillsHere) { if (mainPlan) { const Break* mb = S->brk[(size_t) mainBay]; const BreakDNA* md = S->dna[(size_t) mainBay]; p = interlockStep(F.ilMode, P.role, p, *mainPlan, d, md && mb && md->steps == mb->slices ? md : nullptr, s, true); } }
        else if (!fillNow && inFillAt(F, s - 1, mS, mE) && (((s % 16) + 16) % 16) == 0 && fillsHere) p = resolveStep(d, p);
        return p;
    }
    // the UI's SEQUENCE strip / MIDI export: what bay b would play on step s with the current settings (no manual fill)
    static StepPlan preview(const std::array<BayParams, kBays>& B, const FxParams& F, const Snapshot* S, int b, long s) {
        const int mainBay = mainBayIndex(B); const StepPlan mp = planAt(B, F, S, mainBay, s, -1, -1, nullptr, mainBay);
        return b == mainBay ? mp : planAt(B, F, S, b, s, -1, -1, &mp, mainBay);
    }

private:
    struct SV { const Break* brk = nullptr; double pos = 0, end = 0, rate = 1; float gain = 0, fadeIn = 0, fade = -1; bool loop = false, rev = false; int startAt = 0; };
    struct PadEv { int kind = 0, idx = 0; float vel = 0; };
    juce::AbstractFifo padFifo { 128 }; PadEv padEv[128];
    struct Trig { int at = 0; StepPlan plan; bool legacy = false, stop = false; };
    struct Bay { SV v[2]; SV pad; int cur = 0; int barOffset = 0; long lastBar = -1; float duck = 1; std::mt19937 rng { 99 }; Trig pend[16]; int nPend = 0; };
    struct DV { const gb::Sound* s = nullptr; double pos = 0, rate = 1; float gain = 1; int drum = 0, delay = 0; float fade = -1; };
    struct TPT { float s1[2] {}, s2[2] {}, a1 = 0, a2 = 0, a3 = 0, k = 1;
        void setup(float f, float q, float rate) { const float g = std::tan(juce::MathConstants<float>::pi * juce::jlimit(20.0f, rate * 0.45f, f) / rate); k = 2.0f - 1.9f * q; a1 = 1 / (1 + g * (g + k)); a2 = g * a1; a3 = g * a2; }
        // type: 0 low 1 band 2 high 3 notch
        float tick(int c, float x, int type) { const float v3 = x - s2[c], v1 = a1 * s1[c] + a2 * v3, v2 = s2[c] + a2 * s1[c] + a3 * v3; s1[c] = 2 * v1 - s1[c]; s2[c] = 2 * v2 - s2[c];
            switch (type) { case 0: return v2; case 1: return v1 * k; case 2: return x - k * v1 - v2; default: return x - k * v1; } } };
    // 10 ms linear glides for the gain-class parameters, rendered as per-sample curves once per block
    struct Sm { float cur = 0, target = 0; };
    enum Curve { kCLevel0 = 0, kCPan0 = kCLevel0 + kBays, kCDrumLvl0 = kCPan0 + kBays, kCBreaks = kCDrumLvl0 + kDrums, kCDrums, kCVolume, kCAud, kNumCurves };
    std::vector<float> curves[kNumCurves]; Sm sm[kNumCurves]; bool firstBlock = true; int fadeLen = 480;
    void buildCurves(const std::array<BayParams, kBays>& B, const FxParams& F, int n) {
        float tg[kNumCurves];
        for (int b = 0; b < kBays; ++b) { tg[kCLevel0 + b] = B[(size_t) b].level; tg[kCPan0 + b] = B[(size_t) b].pan; }
        for (int d = 0; d < kDrums; ++d) tg[kCDrumLvl0 + d] = F.drumLvl[d];
        tg[kCBreaks] = F.breaksLevel; tg[kCDrums] = F.drumsLevel; tg[kCVolume] = F.volume; tg[kCAud] = audBrk ? 0.35f : 1.0f;
        for (int c = 0; c < kNumCurves; ++c) {
            auto& s = sm[c]; s.target = tg[c]; if (firstBlock) s.cur = s.target;
            float* out = curves[c].data();
            if (s.cur == s.target) { for (int i = 0; i < n; ++i) out[i] = s.target; continue; }
            const float step = (s.target - s.cur) / (float) fadeLen; const float remaining = std::abs(s.target - s.cur);
            const int ramp = juce::jmin(n, (int) std::ceil(remaining / std::abs(step)));
            for (int i = 0; i < ramp; ++i) { s.cur += step; out[i] = s.cur; }
            if (ramp < n) { s.cur = s.target; for (int i = ramp; i < n; ++i) out[i] = s.target; }
            else if (std::abs(s.target - s.cur) < std::abs(step) * 0.5f) s.cur = s.target;
        }
    }

    bool muted[kDrums] {}; double sr = 48000, clock = 0, curBpm = 172, lastB0 = -1e9; int maxBlock = 512;
    std::atomic<const Snapshot*> snap { nullptr };
    Bay bays[kBays]; DV dv[48];
    float duckEnv[3] {};   // kick, snare, hats
    juce::AudioBuffer<float> busBuf, drumBufs, scratch;
    TPT lp, hp, bp, notch, form[3], stutF;
    std::vector<float> combBuf[2]; int combPos = 0;
    juce::dsp::Chorus<float> chorus; juce::dsp::Reverb reverb;
    juce::dsp::Oversampling<float> os { 2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, false }; int osLat = 0;
    std::vector<float> stutBuf[2]; int stutLen = 0, stutRec = 0, stutPos = 0, stutRep = 0; bool stutWas = false; float stutEnvNow = 0;
    int fxFade = 0, stFade = 0, lastOldF = -1, lastOldSt = -1;   // classic <-> new filter / stutter crossfades
    // Mickens 2 state
    const std::array<BayParams, kBays>* curB = nullptr; const FxParams* curF = nullptr; const Snapshot* curS = nullptr; double curSpb = 1, curB0 = 0;
    Trig bayTrig[kBays][48]; int bayTrigN[kBays] {}; int lastN = 0;
    FilterHit hits[256]; int nHits = 0;
    long manStart = -1, manEnd = -1;
    struct BayDuck { float env = 0; int atk = 0, hold = 0; float atkStep = 0, target = 0; } bayDuck;
    std::vector<float> bayDuckCurve; struct DuckHit { int at; float depth; } duckHits[128]; int nDuckHits = 0;
    const Break* audBrk = nullptr; double audPos = 0, audRate = 1; float audGain = 1;

    static float hrand(uint32_t x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return (float) (x & 0xffffffU) / 16777216.0f; }
    static float soft(float x) { return std::abs(x) < 0.9f ? x : std::tanh(x); }                                           // the classic output stage
    static float safe(float x) { const float a = std::abs(x); return a <= 0.98f ? x : (x < 0 ? -1.0f : 1.0f) * (0.98f + 0.02f * std::tanh((a - 0.98f) / 0.02f)); }   // safety only: a soft knee from 0.98 that never passes 1.0
    static float cutHz(float v) { return 30.0f * std::pow(600.0f, juce::jlimit(0.0f, 1.0f, v)); }

    StepPlan planFor(int b, long s, const StepPlan* mainPlan = nullptr, int mainBay = 0) const { return planAt(*curB, *curF, curS, b, s, manStart, manEnd, mainPlan, mainBay); }

    void addTrig(int b, const Trig& t) { if (bayTrigN[b] < 48) bayTrig[b][bayTrigN[b]++] = t; }
    void queueTrig(int b, const Trig& t, int n) {
        if (t.at < n) addTrig(b, t);
        else { auto& bay = bays[b]; if (bay.nPend < 16) bay.pend[bay.nPend++] = t; }
    }
    void stepBay(const Snapshot* S, const BayParams& P, int b, long s, int at, const StepPlan& plan) {
        const Break* br = S->brk[(size_t) b]; if (!br || br->data.empty() || P.mute) return;
        auto& bay = bays[b]; const long bar = s / 16;
        static const int G[] = { 1, 2, 4, 8, 16 };   // the start point HOLD uses (as the classic engine kept it)
        if (P.rndStart && bar != bay.lastBar) { const int grid = G[juce::jlimit(0, 4, P.grid)], count = juce::jmax(1, br->slices / grid); bay.barOffset = juce::jmin(count - 1, (int) (hrand((uint32_t) bar * 977u + (uint32_t) b * 131u + P.seed) * (float) count)) * grid; }
        if (!P.rndStart) bay.barOffset = 0;
        bay.lastBar = bar;
        const int n = lastN;
        if (plan.slice < 0) { Trig t; t.at = at; t.stop = true; t.plan = plan; queueTrig(b, t, n); return; }
        const double stepLen = curSpb / 4.0;
        const int r = juce::jlimit(1, 4, plan.ratchet); const double d0 = juce::jlimit(0.0f, 2.0f, plan.delay) * stepLen;
        for (int k = 0; k < r; ++k) {
            Trig t; t.plan = plan; t.at = at + (int) std::llround(d0 + k * stepLen / r);
            t.legacy = plan.identity && !plan.fill && !plan.resolve && k == 0;
            if (k > 0) t.plan.gain *= 0.92f;
            queueTrig(b, t, n);
        }
    }
    void startVoice(int b, const BayParams& P, const Trig& t, int at) {
        auto& bay = bays[b]; const Break* br = curS->brk[(size_t) b]; if (!br || br->data.empty()) return;
        const int slice = ((t.plan.slice % br->slices) + br->slices) % br->slices;
        uiSlice[b].store(slice);
        if (tlogN < 8192) tlog[tlogN++] = { absClock + at, b, slice, t.plan.fill, t.plan.resolve };
        // crossfade into the new slice
        auto& old = bay.v[bay.cur]; if (old.brk) old.fade = 1.0f;
        bay.cur ^= 1; auto& v = bay.v[bay.cur];
        const int L = br->sliceLen();
        const double base = slice * (double) L, lenS = L * juce::jlimit(0.05f, 1.0f, P.decay) * t.plan.len + (P.decay >= 0.99f ? L * 0.6 : 0);
        v.brk = br; v.loop = false; v.startAt = at;
        if (!t.plan.reverse) { v.pos = base + (t.plan.skip > 0 ? juce::jlimit(0.0f, 1.0f, t.plan.skip) * L : 0.0); v.end = base + lenS; v.rev = false; }
        else { v.pos = juce::jmin(base + lenS, (double) br->data.size() - 2); v.end = base; v.rev = true; }
        v.rate = rateFor(P, br) * (t.plan.semis != 0 ? std::exp2(juce::jlimit(-36.0f, 36.0f, t.plan.semis) / 12.0) : 1.0); v.gain = t.plan.gain; v.fadeIn = 0; v.fade = -1;
        // the main bay's hits drive the filter HIT SWEEP
        if (b == mainBayIndex(*curB) && nHits < 256) hits[nHits++] = { at, t.plan.cls != 0 ? t.plan.cls : (uint8_t) 8 };
    }

    double rateFor(const BayParams& P, const Break* br) const { return std::exp2(P.pitch / 12.0) * br->rate / sr * (P.fit ? curBpm / juce::jmax(40.0, br->bpm) : 1.0); }
    // a pad / key: drums hit; a bay's HOLD plays its break from its start point, in time, until released;
    // a slice key plays one slice of the KEYS bay
    void pad(const Snapshot* S, const std::array<BayParams, kBays>& B, const FxParams& F, int kind, int idx, float vel, int at) {
        if (kind == 0) { if (juce::isPositiveAndBelow(idx, kDrums)) trigDrum(S, idx, vel, at); return; }
        const int b = kind == 3 ? juce::jlimit(0, kBays - 1, F.keysBay) : idx; if (!juce::isPositiveAndBelow(b, kBays)) return;
        auto& bay = bays[b]; const Break* br = S->brk[(size_t) b]; const auto& P = B[(size_t) b];
        if (kind == 2) { if (bay.pad.brk && bay.pad.loop && bay.pad.fade < 0) bay.pad.fade = 1.0f; return; }
        if (!br || br->data.empty()) return;
        static const int G[] = { 1, 2, 4, 8, 16 };
        const int L = br->sliceLen(), grid = G[juce::jlimit(0, 4, P.grid)];
        auto& v = bay.pad; v.brk = br; v.rate = rateFor(P, br); v.gain = 0.45f + 0.55f * vel; v.fadeIn = 0; v.fade = -1; v.rev = false;
        if (kind == 1) { const int start = (((P.nudge * grid + bay.barOffset) % br->slices) + br->slices) % br->slices; v.pos = start * (double) L; v.end = 1e18; v.loop = true; }
        else { const int sl = idx % br->slices; v.pos = sl * (double) L; v.end = v.pos + L * juce::jlimit(0.05f, 1.0f, P.decay) + (P.decay >= 0.99f ? L * 0.6 : 0); v.loop = false; }
        v.startAt = at;
    }
    void triggerFill(const FxParams& F, double beat, bool running) {
        if (!running) return;
        const long sNext = (long) std::ceil(beat * 4.0 - 1e-9);
        const long bar = sNext >= 0 ? sNext / 16 : (sNext - 15) / 16; const long D = (bar + 1) * 16;
        manStart = juce::jmax(sNext, D - (long) juce::jlimit(1, 16, F.fillLen16)); manEnd = D;
    }

    // one bay for the whole block: triggers in time order, the voices rendered between them
    void renderBayBlock(const BayParams& P, int b, int n, float ilDuck, int interp) {
        auto& T = bayTrig[b]; const int nt = bayTrigN[b];
        std::sort(T, T + nt, [](const Trig& a, const Trig& c) { return a.at < c.at; });
        int cursor = 0, i0 = 0;
        // the classic engine fades the old slice from the top of the block and starts the new one at its 16th: keep that
        if (nt > 0 && T[0].legacy) { startVoice(b, P, T[0], T[0].at); i0 = 1; }
        for (int i = i0; i < nt; ++i) {
            const int at = juce::jlimit(0, n, T[i].at);
            if (at > cursor) { renderBay(P, b, cursor, at, ilDuck, interp); cursor = at; }
            if (T[i].stop) { auto& v = bays[b].v[bays[b].cur]; if (v.brk && v.fade < 0) v.fade = 1.0f; uiSlice[b].store(-1); }
            else startVoice(b, P, T[i], at);
        }
        renderBay(P, b, cursor, n, ilDuck, interp);
        auto& bay = bays[b]; bay.v[0].startAt = bay.v[1].startAt = bay.pad.startAt = 0;
    }

    void renderBay(const BayParams& P, int b, int from, int to, float ilDuck, int interp) {
        auto& bay = bays[b]; const float* lvlC = curves[kCLevel0 + b].data(); const float* panC = curves[kCPan0 + b].data();
        float pl = 0, pr = 0, lastPan = 1e9f;
        for (int k = 0; k < 3; ++k) {
            auto& v = k < 2 ? bay.v[k] : bay.pad; if (!v.brk) continue;
            const int len = (int) v.brk->data.size(); const float* d = v.brk->data.data();
            const int start = juce::jmax(from, v.startAt);
            if (k == 2 && start < to) uiSlice[b].store((int) (v.pos / juce::jmax(1, v.brk->sliceLen())) % juce::jmax(1, v.brk->slices));
            if (k < 2 && P.mute && v.fade < 0) v.fade = 1.0f;   // a muted bay's sequence fades out (2 ms); its pad still plays
            for (int i = start; i < to; ++i) {
                if (v.loop && v.pos >= len - 1) v.pos -= (len - 1);
                if (!v.rev) { if (v.pos >= v.end || v.pos >= len - 1) { v.brk = nullptr; break; } }
                else if (v.pos <= v.end || v.pos < 0) { v.brk = nullptr; break; }
                float x = readSample(d, len, v.pos, interp);
                float g = v.gain;
                if (v.fadeIn < 1) { v.fadeIn = juce::jmin(1.0f, v.fadeIn + 1.0f / 96.0f); g *= v.fadeIn; }
                if (v.fade >= 0) { v.fade -= 1.0f / 96.0f; if (v.fade <= 0) { v.brk = nullptr; break; } g *= v.fade; }
                if (!v.loop) { const double remain = v.rev ? v.pos - v.end : v.end - v.pos; if (remain < 240) g *= (float) (remain / 240.0); }   // tail fade at the slice end
                // per-element sidechain: only this bay ducks
                float duck = (1 - P.duck[0] * duckCurve[0][(size_t) i]) * (1 - P.duck[1] * duckCurve[1][(size_t) i]) * (1 - P.duck[2] * duckCurve[2][(size_t) i]);
                if (ilDuck > 0) duck *= 1 - ilDuck * bayDuckCurve[(size_t) i];
                x *= g * lvlC[i] * duck;
                if (panC[i] != lastPan) { lastPan = panC[i]; pl = std::cos((lastPan + 1) * juce::MathConstants<float>::pi * 0.25f) * 1.414f; pr = std::sin((lastPan + 1) * juce::MathConstants<float>::pi * 0.25f) * 1.414f; }
                busBuf.addSample(0, i, x * pl); busBuf.addSample(1, i, x * pr);
                v.pos += v.rev ? -v.rate : v.rate;
            }
        }
    }
    // per-element sidechain envelopes for this block: a fast 2 ms attack on each hit, then the release
    std::vector<float> duckCurve[3]; std::vector<uint8_t> duckHit[3]; int duckAtk[3] {};
    void buildDucks(int n, float rel) {
        const float atk = 1.0f - std::exp(-1.0f / (0.002f * (float) sr));
        for (int e = 0; e < 3; ++e) {
            float env = duckEnv[e];
            for (int i = 0; i < n; ++i) {
                if (duckHit[e][(size_t) i]) { duckAtk[e] = (int) (0.004 * sr); duckHit[e][(size_t) i] = 0; }
                if (duckAtk[e] > 0) { env += (1 - env) * atk; --duckAtk[e]; } else env *= rel;
                duckCurve[e][(size_t) i] = env;
            }
            duckEnv[e] = env;
        }
    }
    // INTERLOCK ducking: the other bays dip around the main bay's hits. The duck starts 2 ms before each hit
    // (the hits are known ahead: they're on the grid), holds 10 ms, then releases.
    int preRamp() const { return juce::jmax(16, (int) (0.002 * sr)); }
    void scheduleBayDucks(const std::array<BayParams, kBays>& B, int mainBay, double b0, double b1, double spb, int n) {
        nDuckHits = 0; const int pre = preRamp(); const double stepLen = spb / 4.0;
        if (B[(size_t) mainBay].mute) return;
        for (long s = (long) std::ceil(b0 * 4.0 - 1e-9) - 2; s < (b1 + pre / spb) * 4.0; ++s) {
            const StepPlan p = planFor(mainBay, s); if (p.slice < 0) continue;
            const BreakDNA* d = curS->dna[(size_t) mainBay]; const Break* br = curS->brk[(size_t) mainBay];
            float acc = 0.6f; if (d && br && d->steps == br->slices) acc = d->accent[(size_t) (((p.slice % d->steps) + d->steps) % d->steps)];
            const float depth = juce::jlimit(0.0f, 1.0f, (p.cls & (kK | kS)) ? 0.6f + 0.4f * acc : 0.5f * acc);
            if (depth < 0.05f) continue;
            const double t0 = (s / 4.0 - b0) * spb; const int r = juce::jlimit(1, 4, p.ratchet);
            for (int k = 0; k < r; ++k) {
                const double tt = std::floor(t0) + std::llround(juce::jlimit(0.0f, 2.0f, p.delay) * stepLen + k * stepLen / r);   // same rounding as the trigger itself
                const int at = (int) tt - pre;
                if (at >= 0 && at < n && nDuckHits < 128) duckHits[nDuckHits++] = { at, depth * (k ? 0.7f : 1.0f) };
            }
        }
    }
    void buildBayDuck(int n, float rel) {
        auto& D = bayDuck; int h = 0; const int pre = preRamp();
        std::sort(duckHits, duckHits + nDuckHits, [](const DuckHit& a, const DuckHit& c) { return a.at < c.at; });
        for (int i = 0; i < n; ++i) {
            while (h < nDuckHits && duckHits[h].at <= i) { if (duckHits[h].depth > D.env || D.atk <= 0) { D.target = juce::jmax(D.env, duckHits[h].depth); D.atk = pre; D.atkStep = (D.target - D.env) / pre; } ++h; }
            if (D.atk > 0) { D.env += D.atkStep; if (--D.atk == 0) D.hold = (int) (0.010 * sr); }
            else if (D.hold > 0) --D.hold; else D.env *= rel;
            bayDuckCurve[(size_t) i] = D.env;
        }
        nDuckHits = 0;
    }

    void trigDrum(const Snapshot* S, int d, float vel, int at) {
        if (muted[d]) return;   // a muted drum neither plays nor ducks
        const gb::Sound* s = S->drumSound[(size_t) d]; if (!s || s->data.empty()) return;
        drumHits[d].fetch_add(1);
        const int e = d == 0 ? 0 : (d == 1 ? 1 : 2);
        duckHit[e][(size_t) at] = 1;
        if (nHits < 256) hits[nHits++] = { at, (uint8_t) (d == 0 ? kK : d == 1 ? kS : kH) };
        if (d == 2) for (auto& v : dv) if (v.s && v.drum == 3 && v.fade < 0) v.fade = 1.0f;   // closed hat chokes the open hat
        DV* pick = &dv[0]; for (auto& v : dv) { if (!v.s) { pick = &v; break; } if (v.pos > pick->pos) pick = &v; }
        // the drums wait for the breaks' oversampler latency (new sessions) so both land together
        *pick = { s, 0, s->rate / sr, 0.3f + 0.7f * vel, d, at + (curF && !curF->legacyOut ? osLat : 0), -1 };
    }
    void renderDrums(int n, const FxParams& F) {
        for (auto& v : dv) {
            if (!v.s) continue; const int len = (int) v.s->data.size(); const float* lvlC = curves[kCDrumLvl0 + v.drum].data(); const float* d = v.s->data.data();
            for (int i = v.delay; i < n; ++i) {
                if (v.pos >= len - 1) { v.s = nullptr; break; }
                float g = v.gain * lvlC[i]; if (v.fade >= 0) { v.fade -= 1.0f / 200.0f; if (v.fade <= 0) { v.s = nullptr; break; } g *= v.fade; }
                const float x = readSample(d, len, v.pos, F.interp) * g;
                drumBufs.addSample(v.drum * 2, i, x); drumBufs.addSample(v.drum * 2 + 1, i, x); v.pos += v.rate;
            }
            v.delay = juce::jmax(0, v.delay - n);
        }
    }
    void renderAudition(float* L, float* R, int n, int interp) {
        if (auditionStart.exchange(false)) { audBrk = audition.load(); audPos = 0; if (audBrk) audRate = audBrk->rate / sr * (audBrk->tempoFound && audBrk->bpm > 40 ? curBpm / audBrk->bpm : 1.0); }
        const float* audC = curves[kCAud].data(); const float* d = audBrk ? audBrk->data.data() : nullptr; const int len = audBrk ? (int) audBrk->data.size() : 0;
        for (int i = 0; i < n; ++i) {
            L[i] *= audC[i]; R[i] *= audC[i];   // the mix ducks -9 dB while a break auditions
            if (!audBrk) continue;
            if (audPos >= len - 2) { audBrk = nullptr; continue; }
            const float x = readSample(d, len, audPos, interp) * 0.7f; L[i] += x; R[i] += x; audPos += audRate;
        }
    }

    // ---- the classic (pre-Mickens-2) filters and stutter, kept exactly as they were
    void classicFilters(const FxParams& F, float* L, float* R, int n) {
        const bool anyF = F.lpMix + F.hpMix + F.bpMix + F.notchMix + F.combMix + F.formMix > 0.01f;
        if (anyF) {
            lp.setup(cutHz(F.lpCut), F.res, (float) sr); hp.setup(cutHz(F.hpCut), F.res, (float) sr); bp.setup(cutHz(F.bpCut), F.res, (float) sr); notch.setup(cutHz(F.notchCut), F.res * 0.8f, (float) sr);
            static const float FORM[5][3] = { { 800, 1150, 2900 }, { 400, 1600, 2700 }, { 300, 2300, 3000 }, { 450, 800, 2830 }, { 325, 700, 2530 } };
            const float fp = juce::jlimit(0.0f, 1.0f, F.formant) * 3.999f; const int fa = (int) fp; const float ft = fp - fa;
            for (int j = 0; j < 3; ++j) form[j].setup(FORM[fa][j] + (FORM[fa + 1][j] - FORM[fa][j]) * ft, 0.85f, (float) sr);
            const int N = (int) combBuf[0].size(); const float combHz = 40.0f * std::pow(40.0f, F.combCut); const float cd = juce::jlimit(2.0f, (float) N - 2, (float) sr / combHz);
            for (int i = 0; i < n; ++i) {
                for (int c = 0; c < 2; ++c) {
                    float* X = c ? R : L; const float x = X[i];
                    float y = x * F.dry;
                    if (F.lpMix > 0.01f) y += lp.tick(c, x, 0) * F.lpMix;
                    if (F.hpMix > 0.01f) y += hp.tick(c, x, 2) * F.hpMix;
                    if (F.bpMix > 0.01f) y += bp.tick(c, x, 1) * F.bpMix;
                    if (F.notchMix > 0.01f) y += notch.tick(c, x, 3) * F.notchMix;
                    if (F.combMix > 0.01f) { auto& B = combBuf[c]; float rp = combPos - cd; if (rp < 0) rp += N; const int r0 = (int) rp; const float t = rp - r0;
                        const float dd = B[(size_t) r0] + (B[(size_t) ((r0 + 1) % N)] - B[(size_t) r0]) * t; const float cy = std::tanh(x + dd * (0.55f + F.res * 0.4f)); B[(size_t) combPos] = cy; y += cy * F.combMix * 0.7f; }
                    if (F.formMix > 0.01f) { float s = 0; for (int j = 0; j < 3; ++j) s += form[j].tick(c, x, 1) * (j == 0 ? 1.0f : (j == 1 ? 0.5f : 0.2f)); y += s * F.formMix * 1.5f; }
                    X[i] = y;
                }
                combPos = (combPos + 1) % N;
            }
        } else if (F.dry < 0.99f) for (int i = 0; i < n; ++i) { L[i] *= F.dry; R[i] *= F.dry; }
    }
    void classicStutter(const FxParams& F, float* L, float* R, int n, double spb) {
        if (F.stutter) {
            static const double D[] = { 0.5, 0.25, 0.125, 0.0625, 1.0 / 6.0 };   // 1/8, 1/16, 1/32, 1/64, 1/16T
            if (!stutWas) { stutLen = juce::jlimit(32, (int) stutBuf[0].size(), (int) (spb * D[juce::jlimit(0, 4, F.stutDiv)])); stutRec = 0; stutPos = 0; stutRep = 0; stutEnvNow = 1; stutWas = true; }
            const float decay = std::exp(-1.0f / (juce::jmax(0.01f, F.stutDecay * 0.3f) * (float) sr));
            for (int i = 0; i < n; ++i) {
                float a = L[i], b = R[i];
                if (stutRec < stutLen) { stutBuf[0][(size_t) stutRec] = a; stutBuf[1][(size_t) stutRec] = b; ++stutRec; }
                else { const float w = juce::jmin(1.0f, juce::jmin((float) stutPos, (float) (stutLen - stutPos)) / 48.0f); a = stutBuf[0][(size_t) stutPos] * w; b = stutBuf[1][(size_t) stutPos] * w;
                       if (++stutPos >= stutLen) { stutPos = 0; stutEnvNow = 1; } }
                stutEnvNow *= decay;
                if ((i & 15) == 0) stutF.setup(cutHz(juce::jlimit(0.0f, 1.0f, F.stutCut + F.stutEnv * stutEnvNow * 0.5f)), 0.6f, (float) sr);
                L[i] = stutF.tick(0, a, 0); R[i] = stutF.tick(1, b, 0);
            }
        } else stutWas = false;
    }
    // run `other` on a copy of the bus and crossfade it under `now` for the rest of a 10 ms window
    template <typename Now, typename Other> void crossfaded(int& fade, float* L, float* R, int n, Now now, Other other) {
        if (fade <= 0) { now(L, R); return; }
        float* sL = scratch.getWritePointer(0); float* sR = scratch.getWritePointer(1); std::copy(L, L + n, sL); std::copy(R, R + n, sR);
        now(L, R); other(sL, sR);
        for (int i = 0; i < n && fade > 0; ++i, --fade) { float t = fade / (float) fadeLen; t = t * t * (3 - 2 * t); L[i] = L[i] * (1 - t) + sL[i] * t; R[i] = R[i] * (1 - t) + sR[i] * t; }
    }

    void breakFx(const FxParams& F, int n, double spb, double b0, bool running, int stutOn, int stutOff) {
        float* L = busBuf.getWritePointer(0); float* R = busBuf.getWritePointer(1);
        // CRUNCH: drive + bit crush (2x oversampled in new sessions; the classic path is sample-exact)
        auto crunch = [&](float* X, float* Y, int m) { const float g = 1 + F.crunch * F.crunch * 18, c = 1 / (1 + F.crunch * 1.3f), q = std::exp2(14.0f - F.bits * 11.0f);
            for (int i = 0; i < m; ++i) { X[i] = std::tanh(X[i] * g) * c; Y[i] = std::tanh(Y[i] * g) * c; if (F.bits > 0.01f) { X[i] = std::round(X[i] * q) / q; Y[i] = std::round(Y[i] * q) / q; } } };
        if (F.legacyOut) { if (F.crunch > 0.01f || F.bits > 0.01f) crunch(L, R, n); }
        else {
            juce::dsp::AudioBlock<float> blk(busBuf.getArrayOfWritePointers(), 2, (size_t) n);
            auto up = os.processSamplesUp(blk);
            if (F.crunch > 0.01f || F.bits > 0.01f) crunch(up.getChannelPointer(0), up.getChannelPointer(1), (int) up.getNumSamples());
            os.processSamplesDown(blk);
            for (int i = 0; i < nHits; ++i) hits[i].at = juce::jmin(n - 1, hits[i].at + osLat);   // the bus is now osLat samples late: the sweeps follow it
        }
        // FILTERS: classic (legacy sessions) or the router; a switch between them crossfades over 10 ms
        const int oldF = F.oldFilters ? 1 : 0; if (lastOldF >= 0 && oldF != lastOldF) fxFade = fadeLen; lastOldF = oldF;
        crossfaded(fxFade, L, R, n,
            [&](float* X, float* Y) { if (F.oldFilters) classicFilters(F, X, Y, n); else router.process(X, Y, n, F.flt, hits, nHits, b0, spb, curBpm, running); },
            [&](float* X, float* Y) { if (F.oldFilters) router.process(X, Y, n, F.flt, hits, nHits, b0, spb, curBpm, running); else classicFilters(F, X, Y, n); });
        // CHORUS
        if (F.chorus > 0.01f) { chorus.setMix(F.chorus * 0.6f); chorus.setDepth(0.25f + F.chorus * 0.5f); juce::dsp::AudioBlock<float> blk(busBuf.getArrayOfWritePointers(), 2, (size_t) n); chorus.process(juce::dsp::ProcessContextReplacing<float>(blk)); }
        // STUTTER (same crossfade between the classic and the new one)
        const int oldSt = F.oldStutter ? 1 : 0; if (lastOldSt >= 0 && oldSt != lastOldSt) stFade = fadeLen; lastOldSt = oldSt;
        crossfaded(stFade, L, R, n,
            [&](float* X, float* Y) { if (F.oldStutter) classicStutter(F, X, Y, n, spb); else stut.process(X, Y, n, F.st, F.stutter, stutOn, stutOff, b0, spb, running); },
            [&](float* X, float* Y) { if (F.oldStutter) stut.process(X, Y, n, F.st, F.stutter, stutOn, stutOff, b0, spb, running); else classicStutter(F, X, Y, n, spb); });
        // REVERB
        if (F.reverb > 0.01f) { juce::dsp::Reverb::Parameters rp; rp.roomSize = 0.4f + F.size * 0.58f; rp.damping = 0.45f; rp.wetLevel = F.reverb * 0.5f; rp.dryLevel = 1; rp.width = 1;
            reverb.setParameters(rp); juce::dsp::AudioBlock<float> blk(busBuf.getArrayOfWritePointers(), 2, (size_t) n); reverb.process(juce::dsp::ProcessContextReplacing<float>(blk)); }
    }
};

} // namespace mk
