// SPIRIT EQ: parameters, audio engine (matched zero-latency bands, dynamic + spectral dynamics, linear phase,
// character, auto gain) and state. The editor lives in FxEqPage.cpp.
#include "FxEqInternal.h"

namespace gpfx {
namespace eq {

const juce::StringArray& typeNames() { static juce::StringArray s { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch", "Band Pass", "Tilt Shelf", "Flat Tilt" }; return s; }
const juce::StringArray& slopeNames() { static juce::StringArray s { "6 dB", "12 dB", "18 dB", "24 dB", "36 dB", "48 dB", "72 dB", "96 dB" }; return s; }
const juce::StringArray& placementNames() { static juce::StringArray s { "Stereo", "Left", "Right", "Mid", "Side" }; return s; }
const juce::StringArray& modeNames() { static juce::StringArray s { "Zero Latency", "Linear Low", "Linear Medium", "Linear High" }; return s; }
juce::StringArray bandParamSuffixes()
{
    return { "on", "byp", "type", "slope", "freq", "gain", "q", "stereo", "dyn", "thr", "range", "ratio", "att", "rel", "auto", "detect", "spec" };
}
juce::StringArray allParamIds()
{
    juce::StringArray ids;
    for (int k = 0; k < kBands; ++k) for (auto& s : bandParamSuffixes()) ids.add (bid (k, s.toRawUTF8()));
    for (auto* g : { "fx_eq_out", "fx_eq_mode", "fx_eq_char", "fx_eq_scale", "fx_eq_autogain", "fx_eq_invert" }) ids.add (g);
    return ids;
}

// ============================================================================ parameters
void addParameters (Layout& l)
{
    const float freqs[kBands] { 40, 100, 250, 600, 1500, 3500, 8000, 14000, 60, 300, 2000, 10000 };
    for (int k = 0; k < kBands; ++k)
    {
        const juce::String n = "FX EQ " + juce::String (k + 1) + " ";
        addBool (l, bid (k, "on"), n + "On", false);
        addBool (l, bid (k, "byp"), n + "Bypass", false);
        addChoice (l, bid (k, "type"), n + "Type", typeNames(), 0);
        addChoice (l, bid (k, "slope"), n + "Slope", slopeNames(), 1);
        addFloat (l, bid (k, "freq"), n + "Freq", skewRange (10.0f, 30000.0f, 1000.0f), freqs[k], "Hz", 1);
        addFloat (l, bid (k, "gain"), n + "Gain", juce::NormalisableRange<float> (-30.0f, 30.0f), 0.0f, "dB", 1);
        addFloat (l, bid (k, "q"), n + "Q", skewRange (0.025f, 40.0f, 1.0f), 1.0f, "", 2);
        addChoice (l, bid (k, "stereo"), n + "Stereo", placementNames(), 0);
        addBool (l, bid (k, "dyn"), n + "Dynamic", false);
        addFloat (l, bid (k, "thr"), n + "Threshold", juce::NormalisableRange<float> (-80.0f, 0.0f), -24.0f, "dB", 1);
        addFloat (l, bid (k, "range"), n + "Range", juce::NormalisableRange<float> (-30.0f, 30.0f), -6.0f, "dB", 1);
        addFloat (l, bid (k, "ratio"), n + "Ratio", skewRange (1.0f, 20.0f, 3.0f), 2.0f, ":1", 1);
        addFloat (l, bid (k, "att"), n + "Attack", skewRange (0.1f, 200.0f, 10.0f), 5.0f, "ms", 1);
        addFloat (l, bid (k, "rel"), n + "Release", skewRange (5.0f, 2000.0f, 150.0f), 120.0f, "ms", 0);
        addBool (l, bid (k, "auto"), n + "Auto Threshold", false);
        addChoice (l, bid (k, "detect"), n + "Detect", { "Band", "Wide" }, 0);
        addBool (l, bid (k, "spec"), n + "Spectral", false);
    }
    addFloat (l, "fx_eq_out", "FX EQ Output", juce::NormalisableRange<float> (-24.0f, 24.0f), 0.0f, "dB", 1);
    addChoice (l, "fx_eq_mode", "FX EQ Mode", modeNames(), 0);
    addChoice (l, "fx_eq_char", "FX EQ Character", { "Clean", "Subtle", "Warm" }, 0);
    {
        auto attrs = juce::AudioParameterFloatAttributes().withLabel ("%")
            .withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; })
            .withValueFromStringFunction ([] (const juce::String& t) { return (float) t.getDoubleValue() / 100.0f; });
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "fx_eq_scale", 1 }, "FX EQ Gain Scale",
                                                            juce::NormalisableRange<float> (-2.0f, 2.0f), 1.0f, attrs));
    }
    addBool (l, "fx_eq_autogain", "FX EQ Auto Gain", false);
    addBool (l, "fx_eq_invert", "FX EQ Phase Invert", false);
}

// ============================================================================ spectral engine
void Spectral::prepare (double sampleRate)
{
    sr = sampleRate;
    order = sr > 100000.0 ? 13 : (sr > 50000.0 ? 12 : 11);
    N = 1 << order; hop = N / 4;
    fft = std::make_unique<juce::dsp::FFT> (order);
    win.assign ((size_t) N, 0.0f);
    double sum = 0;
    for (int i = 0; i < N; ++i) { win[(size_t) i] = (float) std::sqrt (0.5 - 0.5 * std::cos (2.0 * kPiD * i / N)); sum += win[(size_t) i]; }
    binNorm = (float) (2.0 / sum);
    invNorm = 0.5f;   // sqrt-Hann^2 at 75% overlap sums to 2 (JUCE's inverse FFT already divides by N)
    for (auto* v : { &inL, &inR }) v->assign ((size_t) N, 0.0f);
    for (auto* v : { &accL, &accR }) v->assign ((size_t) (2 * N), 0.0f);
    for (auto* v : { &wl, &wr }) v->assign ((size_t) (2 * N), 0.0f);
    lvl.assign ((size_t) (N / 2 + 1), -120.0f); gs.assign ((size_t) (N / 2 + 1), 0.0f);
    reset();
}
void Spectral::reset()
{
    for (auto* v : { &inL, &inR, &accL, &accR }) std::fill (v->begin(), v->end(), 0.0f);
    std::fill (lvl.begin(), lvl.end(), -120.0f); std::fill (gs.begin(), gs.end(), 0.0f);
    pos = 0; bandMaxDb.fill (0.0f); disp.fill (0.0f);
}
void Spectral::process (float* L, float* R, int n) noexcept
{
    const int N2 = 2 * N;
    for (int i = 0; i < n; ++i)
    {
        inL[(size_t) (pos % N)] = L[i]; inR[(size_t) (pos % N)] = R[i];
        if ((pos + 1) % hop == 0) frame();
        const size_t o = (size_t) (((pos - N) % N2 + N2) % N2);
        L[i] = accL[o]; R[i] = accR[o]; accL[o] = 0; accR[o] = 0;
        ++pos;
    }
}
void Spectral::frame() noexcept
{
    const juce::int64 start = pos - N + 1;
    for (int i = 0; i < N; ++i)
    {
        const size_t s = (size_t) (((start + i) % N + N) % N);
        wl[(size_t) i] = inL[s] * win[(size_t) i]; wr[(size_t) i] = inR[s] * win[(size_t) i];
    }
    std::fill (wl.begin() + N, wl.end(), 0.0f); std::fill (wr.begin() + N, wr.end(), 0.0f);
    fft->performRealOnlyForwardTransform (wl.data(), true);
    fft->performRealOnlyForwardTransform (wr.data(), true);
    const int nb = N / 2 + 1;
    bandMaxDb.fill (0.0f); disp.fill (0.0f);
    const double fmin = 20.0, fmax = 20000.0, lr = std::log (fmax / fmin);
    for (int k = 0; k < nb; ++k)
    {
        const float re1 = wl[(size_t) (2 * k)], im1 = wl[(size_t) (2 * k + 1)], re2 = wr[(size_t) (2 * k)], im2 = wr[(size_t) (2 * k + 1)];
        const float m = std::sqrt (std::max (re1 * re1 + im1 * im1, re2 * re2 + im2 * im2)) * binNorm;
        const float db = gainToDb (m);
        lvl[(size_t) k] = db;
        const double f = (double) k * sr / N;
        float g = 0;
        for (int b = 0; b < numBands; ++b)
        {
            const auto& bi = bands[(size_t) b];
            const float w = regionWeight (bi.type, f, bi.f0, bi.q);
            if (w < 0.01f) continue;
            const float over = db - bi.thr;
            if (over <= 0) continue;
            const float amount = std::min (std::abs (bi.range), over * (1.0f - 1.0f / std::max (1.0f, bi.ratio)));
            const float gb = (bi.range < 0 ? -amount : amount) * w;
            g += gb;
            if (std::abs (gb) > std::abs (bandMaxDb[(size_t) b])) bandMaxDb[(size_t) b] = gb;
        }
        float& sm = gs[(size_t) k];
        sm += (std::abs (g) > std::abs (sm) ? attC : relC) * (g - sm);
        const float lin = dbToGain (sm) * invNorm;
        wl[(size_t) (2 * k)] *= lin; wl[(size_t) (2 * k + 1)] *= lin;
        wr[(size_t) (2 * k)] *= lin; wr[(size_t) (2 * k + 1)] *= lin;
        if (f >= fmin && f <= fmax)
        {
            const int slot = juce::jlimit (0, kDisp - 1, (int) (std::log (f / fmin) / lr * kDisp));
            if (std::abs (sm) > std::abs (disp[(size_t) slot])) disp[(size_t) slot] = sm;
        }
    }
    fft->performRealOnlyInverseTransform (wl.data());
    fft->performRealOnlyInverseTransform (wr.data());
    const int N2 = 2 * N;
    for (int i = 0; i < N; ++i)
    {
        const size_t o = (size_t) (((start + i) % N2 + N2) % N2);
        accL[o] += wl[(size_t) i] * win[(size_t) i]; accR[o] += wr[(size_t) i] * win[(size_t) i];
    }
}

// ============================================================================ background designer
struct EqUnit::Worker : public juce::Thread
{
    EqUnit& u; std::vector<double> last; std::atomic<bool> force { true };
    double invScale = 0;
    explicit Worker (EqUnit& e) : juce::Thread ("Spirit EQ designer"), u (e) {}
    void run() override
    {
        while (! threadShouldExit())
        {
            wait (25);
            if (auto* t = u.irTrash.exchange (nullptr)) delete t;
            step();
        }
    }
    bool inFir (int k, int mode) const
    {
        const auto& p = u.bp[(size_t) k];
        return mode > 0 && u.bandActive (k) && p.stereo.i() <= plRight && ! (p.dyn.b() && ! isGainType (p.type.i()));
    }
    void step()
    {
        if (! u.attached) return;
        const double sr = u.sampleRate.load();
        const int mode = u.mode.i();
        std::vector<double> snap; snap.reserve (kBands * 10 + 4);
        for (int k = 0; k < kBands; ++k)
        {
            const auto& p = u.bp[(size_t) k];
            snap.insert (snap.end(), { (double) u.bandActive (k), (double) p.type(), (double) p.slope(), (double) p.freq(), (double) p.gain(),
                                       (double) p.q(), (double) p.stereo(), (double) p.dyn() });
        }
        snap.insert (snap.end(), { (double) u.scale(), (double) mode, sr });
        if (snap == last && ! force.exchange (false)) return;
        last = snap;

        // auto gain: pink-weighted average power of the static curve
        {
            double acc = 0; const int npts = 64;
            std::vector<Sections> secs;
            for (int k = 0; k < kBands; ++k)
                if (u.bandActive (k)) { Sections s; designBand (u.specOf (k, false), sr, s); secs.push_back (s); }
            for (int i = 0; i < npts; ++i)
            {
                const double f = 30.0 * std::pow (16000.0 / 30.0, (double) i / (npts - 1));
                double m = 1; for (auto& s : secs) m *= s.mag (f, sr);
                acc += m * m;
            }
            u.autoGainDb.store ((float) juce::jlimit (-24.0, 24.0, -10.0 * std::log10 (std::max (1e-12, acc / npts))));
        }

        if (mode <= 0) return;
        const int N = u.firLenFor (mode);
        int order = 0; while ((1 << order) < N) ++order;
        juce::dsp::FFT fft (order);
        std::vector<float> buf ((size_t) (2 * N), 0.0f);
        auto* ir = new juce::AudioBuffer<float> (2, N);
        std::vector<Sections> secL, secR;
        for (int k = 0; k < kBands; ++k)
        {
            if (! inFir (k, mode)) continue;
            Sections s; designBand (u.specOf (k, false), sr, s);
            const int pl = u.bp[(size_t) k].stereo.i();
            if (pl == plStereo || pl == plLeft) secL.push_back (s);
            if (pl == plStereo || pl == plRight) secR.push_back (s);
        }
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& secs = ch == 0 ? secL : secR;
            std::fill (buf.begin(), buf.end(), 0.0f);
            for (int k = 0; k <= N / 2; ++k)
            {
                const double f = (double) k * sr / N;
                double m = 1; for (auto& s : secs) m *= s.mag (std::max (0.5, f), sr);
                buf[(size_t) (2 * k)] = (float) m;
            }
            fft.performRealOnlyInverseTransform (buf.data());
            auto* d = ir->getWritePointer (ch);
            for (int n = 0; n < N; ++n)
            {
                const double w = 0.42 - 0.5 * std::cos (2.0 * kPiD * n / N) + 0.08 * std::cos (4.0 * kPiD * n / N);
                d[n] = (float) (buf[(size_t) ((n - N / 2 + N) % N)] * w);
            }
        }
        u.irPendingLen.store (N);
        if (auto* old = u.irSlot.exchange (ir)) delete old;
    }
};

// ============================================================================ unit
EqUnit::EqUnit()
{
    for (auto& a : dynDb) a.store (0.0f);
    for (auto& a : dynFrac) a.store (0.0f);
    for (auto& a : thrUsed) a.store (-24.0f);
    for (auto& a : specDisp) a.store (0.0f);
    worker = std::make_unique<Worker> (*this);
}
EqUnit::~EqUnit()
{
    if (worker) worker->stopThread (2000);
    delete irSlot.exchange (nullptr);
    delete irTrash.exchange (nullptr);
}

void EqUnit::attach (juce::AudioProcessorValueTreeState& s)
{
    for (int k = 0; k < kBands; ++k)
    {
        auto& b = bp[(size_t) k];
        b.on.bind (s, bid (k, "on")); b.byp.bind (s, bid (k, "byp")); b.type.bind (s, bid (k, "type")); b.slope.bind (s, bid (k, "slope"), 1);
        b.freq.bind (s, bid (k, "freq"), 1000); b.gain.bind (s, bid (k, "gain")); b.q.bind (s, bid (k, "q"), 1);
        b.stereo.bind (s, bid (k, "stereo")); b.dyn.bind (s, bid (k, "dyn")); b.thr.bind (s, bid (k, "thr"), -24);
        b.range.bind (s, bid (k, "range"), -6); b.ratio.bind (s, bid (k, "ratio"), 2); b.att.bind (s, bid (k, "att"), 5);
        b.rel.bind (s, bid (k, "rel"), 120); b.autoThr.bind (s, bid (k, "auto")); b.detect.bind (s, bid (k, "detect")); b.spec.bind (s, bid (k, "spec"));
    }
    mode.bind (s, "fx_eq_mode"); character.bind (s, "fx_eq_char"); scale.bind (s, "fx_eq_scale", 1);
    autoGain.bind (s, "fx_eq_autogain"); invert.bind (s, "fx_eq_invert"); out.bind (s, "fx_eq_out");
    attached = true;
    if (worker && ! worker->isThreadRunning()) worker->startThread (juce::Thread::Priority::low);
}

int EqUnit::firLenFor (int m) const
{
    const int base = m == mLinearLow ? 2048 : (m == mLinearMed ? 8192 : 16384);
    const int mul = sr > 100000.0 ? 4 : (sr > 50000.0 ? 2 : 1);
    return m > 0 ? base * mul : 0;
}

EqUnit::Config EqUnit::computeTarget() const
{
    Config c; c.mode = attached ? juce::jlimit (0, 3, mode.i()) : 0; c.firLen = firLenFor (c.mode);
    if (attached)
        for (int k = 0; k < kBands; ++k)
            if (bandActive (k) && bp[(size_t) k].dyn.b() && bp[(size_t) k].spec.b()) { c.spectral = true; break; }
    return c;
}

BandSpec EqUnit::specOf (int k, bool withDynamic) const
{
    const auto& p = bp[(size_t) k];
    BandSpec b; b.type = p.type.i(); b.slope = p.slope.i(); b.freq = p.freq(); b.q = p.q();
    b.gain = p.gain() * scale();
    if (withDynamic && p.dyn.b() && isGainType (b.type)) b.gain += dynDb[(size_t) k].load();
    return b;
}

void EqUnit::prepare (double s, int)
{
    sr = s; sampleRate.store (s);
    spec.prepare (s);
    conv.prepare ({ s, (juce::uint32) kSub, 2 });
    for (auto& r : dryRing) r.prepare (65536 + 2 * kSub);
    dryLat = 0; dryGain = 1; dryState = 0;
    outSm.setTime (s, 20); polSm.setTime (s, 8); soloSm.setTime (s, 15); subtleSm.setTime (s, 30); warmSm.setTime (s, 30);
    warmLoC = rbj (rbjBell, s, 90, 0.7, 0.8); warmHiC = rbj (rbjHighShelf, s, 11000, 0.7, -1.5);
    for (auto& d : dc) d.prepare (s);
    irLoadedLen = 0;
    cur = target = computeTarget();
    curLatency.store (latencyOf (cur));
    if (worker) worker->force.store (true);
    reset();
}

void EqUnit::reset()
{
    for (auto& s : bs)
    {
        s.amt = 0; s.first = true; s.type = -1; s.env = 0; s.dyn = 0; s.frac = 0; s.lastF = -1; s.detType = -2;
        for (auto& ch : s.st) for (auto& b : ch) b.reset();
        for (auto& d : s.det) d.reset();
    }
    conv.reset(); spec.reset();
    for (auto& d : dc) d.reset();
    for (auto& b : warmLo) b.reset();
    for (auto& b : warmHi) b.reset();
    for (auto& b : soloF) b.reset();
    outSm.snap (1); polSm.snap (attached && invert.b() ? -1.0f : 1.0f); soloSm.snap (0);
    subtleSm.snap (attached && character.i() == chSubtle ? 1.0f : 0.0f); warmSm.snap (attached && character.i() == chWarm ? 1.0f : 0.0f);
    if (latencyOf (cur) > 0) { ts = tsWarm; warm = latencyOf (cur); tGain = 0; }
    else { ts = tsRun; tGain = 1; }
}

void EqUnit::idle (const float* L, const float* R, int n)
{
    if (analyserWanted()) { preFifo.push (L, R, n); postFifo.push (L, R, n); }
}

void EqUnit::delayDry (float* L, float* R, int n)
{
    const bool processed = seq != drySeq; drySeq = seq;
    if (! processed)   // the unit is off: follow the parameters directly (nothing audible runs through the engines)
    {
        target = computeTarget();
        if (target != cur) { cur = target; curLatency.store (latencyOf (cur)); }
    }
    const int want = latencyOf (cur);
    for (int i = 0; i < n; ++i)
    {
        dryRing[0].push (L[i]); dryRing[1].push (R[i]);
        if (dryState == 0 && dryLat != want) dryState = 1;
        if (dryState == 1) { dryGain -= 1.0f / 64.0f; if (dryGain <= 0) { dryGain = 0; dryLat = want; dryState = 2; } }
        else if (dryState == 2) { dryGain += 1.0f / 64.0f; if (dryGain >= 1) { dryGain = 1; dryState = 0; } }
        L[i] = dryRing[0].at (dryLat) * dryGain; R[i] = dryRing[1].at (dryLat) * dryGain;
    }
}

// Decide what a band runs this sub-block (type switches fade out first), smooth its parameters, run its detector maths.
void EqUnit::updateBand (int k, BandState& s, int n) noexcept
{
    const auto& p = bp[(size_t) k];
    const bool want = bandActive (k);
    const int type = p.type.i(), slope = p.slope.i(), place = juce::jlimit (0, 4, p.stereo.i());
    const bool dynOn = p.dyn.b() && ! p.spec.b();
    const bool firStatic = cur.mode > 0 && want && place <= plRight && ! (p.dyn.b() && ! isGainType (type));
    const bool sw = type != s.type || (usesSlope (type) && slope != s.slope) || place != s.place || firStatic != s.firStatic;
    if (sw && s.amt <= 0.0f)
    {
        s.type = type; s.slope = slope; s.place = place; s.firStatic = firStatic; s.first = true; s.lastF = -1;
        for (auto& ch : s.st) for (auto& b : ch) b.reset();
    }
    const double tf = juce::jlimit (10.0, 30000.0, (double) p.freq()), tq = juce::jlimit (0.025, 40.0, (double) p.q());
    const double tg = (double) p.gain() * scale();
    if (s.first) { s.fLog = std::log (tf); s.q = tq; s.g = tg; s.first = false; }
    else
    {
        const double a = 1.0 - std::exp (-(double) n / (0.025 * sr));
        s.fLog += a * (std::log (tf) - s.fLog); s.q += a * (tq - s.q); s.g += a * (tg - s.g);
    }

    // dynamic gain computer (detector env was updated per sample during the previous sub-block)
    if (dynOn && want)
    {
        const float lvl = gainToDb (s.env);
        if (lvl > -90.0f) s.avgDb += (lvl - s.avgDb) * (float) std::min (1.0, (double) n / (2.0 * sr));
        const float thr = p.autoThr.b() ? s.avgDb + 2.0f : p.thr();
        const float over = lvl - thr, range = p.range();
        const float amount = over > 0 ? over * (1.0f - 1.0f / std::max (1.0f, p.ratio())) : 0.0f;
        s.dyn = (range < 0 ? -1.0f : 1.0f) * std::min (std::abs (range), amount);
        s.frac = std::abs (range) > 0.1f ? std::min (1.0f, amount / std::abs (range)) : (over > 0 ? 1.0f : 0.0f);
        thrUsed[(size_t) k].store (thr, std::memory_order_relaxed);
    }
    else { s.dyn = 0; s.frac = 0; s.env = 0; }
    dynDb[(size_t) k].store (s.dyn, std::memory_order_relaxed);
    dynFrac[(size_t) k].store (s.frac, std::memory_order_relaxed);

    const bool gainT = isGainType (s.type);
    const double geff = (s.firStatic ? 0.0 : s.g) + (gainT ? (double) s.dyn : 0.0);
    const double f = std::exp (s.fLog);
    if (std::abs (f - s.lastF) > 1e-4 * f || std::abs (geff - s.lastG) > 0.003 || std::abs (s.q - s.lastQ) > 1e-4 * s.q)
    {
        BandSpec b; b.type = s.type; b.slope = s.slope; b.freq = f; b.q = s.q; b.gain = geff;
        designBand (b, sr, s.sec);
        s.lastF = f; s.lastG = geff; s.lastQ = s.q;
    }
    // detector filter (band-limited sidechain)
    if (dynOn && want)
    {
        const int dt = p.detect.i() == dtWide ? -1 : s.type;
        if (dt != s.detType || std::abs (f - s.detF) > 0.01 * f || std::abs (s.q - s.detQ) > 0.01 * s.q)
        {
            s.detType = dt; s.detF = f; s.detQ = s.q;
            switch (dt)
            {
                case -1: case btTiltShelf: case btFlatTilt: s.detC = {}; break;
                case btLowShelf: case btHighCut: s.detC = matched::lowpass (2 * kPiD * f / sr, 0.707); break;
                case btHighShelf: case btLowCut: s.detC = matched::highpass (2 * kPiD * f / sr, 0.707); break;
                default: s.detC = matched::bandpass (2 * kPiD * f / sr, std::max (0.3, s.q)); break;
            }
        }
        const float at = p.att(), rt = p.rel();
        s.envAt = 1.0f - (float) std::exp (-1.0 / std::max (1.0, at * 0.001 * sr));
        s.envRt = 1.0f - (float) std::exp (-1.0 / std::max (1.0, rt * 0.001 * sr));
    }
}

void EqUnit::processBands (float* L, float* R, int n) noexcept
{
    constexpr int kChunk = 16;
    const float amtStep = 1.0f / (float) std::max (1.0, 0.008 * sr);
    for (int off = 0; off < n; off += kChunk)
    {
        const int m = std::min (kChunk, n - off);
        float* l = L + off; float* r = R + off;
        for (int k = 0; k < kBands; ++k)
        {
            auto& s = bs[(size_t) k];
            const auto& p = bp[(size_t) k];
            updateBand (k, s, m);
            const bool want = bandActive (k);
            const bool dynOn = p.dyn.b() && ! p.spec.b() && want;
            const bool gainT = isGainType (s.type);
            const bool sw = p.type.i() != s.type || (usesSlope (s.type) && p.slope.i() != s.slope) || juce::jlimit (0, 4, p.stereo.i()) != s.place
                            || (cur.mode > 0 && want && p.stereo.i() <= plRight && ! (p.dyn.b() && ! gainT)) != s.firStatic;
            float tgt = (want && ! sw) ? 1.0f : 0.0f;
            if (s.firStatic && ! (dynOn && gainT)) tgt = 0.0f;        // the FIR carries this band
            if (dynOn && ! gainT) tgt *= s.frac;                      // dynamic cut / notch: engages above threshold
            if (s.amt <= 0.0f && tgt <= 0.0f && ! dynOn) continue;
            const int ns = s.sec.n; const float sc = s.sec.scale;
            auto& st0 = s.st[0]; auto& st1 = s.st[1];
            const bool det = dynOn && s.detType != -2;
            const bool detFilt = det && s.detType >= 0 && s.detType != btTiltShelf && s.detType != btFlatTilt;
            for (int i = 0; i < m; ++i)
            {
                s.amt += clampf (tgt - s.amt, -amtStep, amtStep);
                const float a = s.amt;
                float x0, x1 = 0; int nch = 2;
                switch (s.place)
                {
                    case plLeft:  x0 = l[i]; nch = 1; break;
                    case plRight: x0 = r[i]; nch = 1; break;
                    case plMid:   x0 = 0.5f * (l[i] + r[i]); nch = 1; break;
                    case plSide:  x0 = 0.5f * (l[i] - r[i]); nch = 1; break;
                    default:      x0 = l[i]; x1 = r[i]; break;
                }
                if (det)
                {
                    float d0 = x0, d1 = x1;
                    if (detFilt) { d0 = s.det[0].process (d0, s.detC); if (nch == 2) d1 = s.det[1].process (d1, s.detC); }
                    const float e = std::max (std::abs (d0), nch == 2 ? std::abs (d1) : 0.0f);
                    s.env += (e > s.env ? s.envAt : s.envRt) * (e - s.env);
                }
                if (a <= 0.0f) continue;
                float y0 = x0, y1 = x1;
                for (int j = 0; j < ns; ++j) y0 = st0[(size_t) j].process (y0, s.sec.s[(size_t) j]);
                y0 = x0 + a * (y0 * sc - x0);
                if (nch == 2)
                {
                    for (int j = 0; j < ns; ++j) y1 = st1[(size_t) j].process (y1, s.sec.s[(size_t) j]);
                    y1 = x1 + a * (y1 * sc - x1);
                }
                switch (s.place)
                {
                    case plLeft:  l[i] = y0; break;
                    case plRight: r[i] = y0; break;
                    case plMid:   { const float sd = 0.5f * (l[i] - r[i]); l[i] = y0 + sd; r[i] = y0 - sd; break; }
                    case plSide:  { const float md = 0.5f * (l[i] + r[i]); l[i] = md + y0; r[i] = md - y0; break; }
                    default:      l[i] = y0; r[i] = y1; break;
                }
            }
        }
    }
}

void EqUnit::process (float* L, float* R, int n, const BlockCtx&)
{
    juce::ScopedNoDenormals noDenormals;
    if (! attached || n <= 0) return;
    ++seq;
    const bool ana = analyserWanted();
    if (ana) preFifo.push (L, R, n);

    target = computeTarget();
    if (ts == tsRun && target != cur) ts = tsFadeOut;

    // linear-phase IR hand-over (designed on the worker thread)
    if (irTrash.load() == nullptr)
        if (auto* b = irSlot.exchange (nullptr))
        {
            const int len = b->getNumSamples();
            conv.loadImpulseResponse (std::move (*b), sr, juce::dsp::Convolution::Stereo::yes, juce::dsp::Convolution::Trim::no,
                                      juce::dsp::Convolution::Normalise::no);
            irTrash.store (b);
            if (len == cur.firLen && irLoadedLen != len && ts == tsWarm) warm = std::max (warm, len + (int) (0.05 * sr));
            irLoadedLen = len;
        }

    if (cur.mode > 0)
    {
        float* ch[2] { L, R };
        juce::dsp::AudioBlock<float> blk (ch, 2, (size_t) n);
        conv.process (juce::dsp::ProcessContextReplacing<float> (blk));
    }

    processBands (L, R, n);

    if (cur.spectral)
    {
        spec.numBands = 0; float at = 0, rt = 0;
        for (int k = 0; k < kBands; ++k)
        {
            const auto& p = bp[(size_t) k];
            if (! (bandActive (k) && p.dyn.b() && p.spec.b())) continue;
            auto& bi = spec.bands[(size_t) spec.numBands++];
            bi = { p.type.i(), p.freq(), p.q(), p.thr(), p.range(), p.ratio(), k };
            at += p.att(); rt += p.rel();
        }
        if (spec.numBands > 0)
        {
            const double hopSec = (double) spec.hop / sr;
            spec.attC = (float) (1.0 - std::exp (-hopSec / std::max (0.001, at / spec.numBands * 0.001)));
            spec.relC = (float) (1.0 - std::exp (-hopSec / std::max (0.001, rt / spec.numBands * 0.001)));
        }
        spec.process (L, R, n);
        for (int b = 0; b < spec.numBands; ++b) dynDb[(size_t) spec.bands[(size_t) b].band].store (spec.bandMaxDb[(size_t) b], std::memory_order_relaxed);
        for (int i = 0; i < Spectral::kDisp; ++i) specDisp[(size_t) i].store (spec.disp[(size_t) i], std::memory_order_relaxed);
    }
    else for (auto& a : specDisp) a.store (0.0f, std::memory_order_relaxed);

    // solo: listen to the selected band's region only
    const int solo = soloBand.load (std::memory_order_relaxed);
    soloSm.target = (solo >= 0 && solo < kBands && bandActive (solo)) ? 1.0f : 0.0f;
    if (solo >= 0 && solo < kBands && solo != soloFor)
    {
        soloFor = solo;
        const auto b = specOf (solo, false);
        const double w0 = 2 * kPiD * juce::jlimit (10.0, 30000.0, b.freq) / sr;
        switch (b.type)
        {
            case btLowShelf: case btHighCut: soloC = matched::lowpass (w0, 0.707); break;
            case btHighShelf: case btLowCut: soloC = matched::highpass (w0, 0.707); break;
            case btTiltShelf: case btFlatTilt: soloC = {}; break;
            default: soloC = matched::bandpass (w0, std::max (0.3, b.q)); break;
        }
    }
    if (solo < 0 && soloSm.cur < 1e-4f) soloFor = -1;

    // character + output gain + polarity + config-switch gain
    const int chr = character.i();
    subtleSm.target = chr == chSubtle ? 1.0f : 0.0f; warmSm.target = chr == chWarm ? 1.0f : 0.0f;
    outSm.target = dbToGain (out() + (autoGain.b() ? autoGainDb.load (std::memory_order_relaxed) : 0.0f));
    polSm.target = invert.b() ? -1.0f : 1.0f;
    const float fadeStep = 1.0f / 128.0f;
    for (int i = 0; i < n; ++i)
    {
        float xl = L[i], xr = R[i];
        const float so = soloSm.next();
        if (so > 1e-4f) { xl += so * (soloF[0].process (xl, soloC) - xl); xr += so * (soloF[1].process (xr, soloC) - xr); }
        const float sb = subtleSm.next(), wm = warmSm.next();
        if (sb + wm > 1e-4f)
        {
            auto sub = [] (float x) { return x + 0.4f * (fastTanh (0.8f * x) / 0.8f - x) + 0.015f * x * x; };
            auto wrm = [] (float x) { return x + 0.7f * (fastTanh (1.1f * x + 0.12f * x * x) / 1.1f - x); };
            float yl = xl + sb * (sub (xl) - xl), yr = xr + sb * (sub (xr) - xr);
            if (wm > 1e-4f)
            {
                const float wl = warmHi[0].process (warmLo[0].process (wrm (xl), warmLoC), warmHiC);
                const float wr = warmHi[1].process (warmLo[1].process (wrm (xr), warmLoC), warmHiC);
                yl += wm * (wl - xl); yr += wm * (wr - xr);
            }
            xl = dc[0].process (yl); xr = dc[1].process (yr);
        }
        const float g = outSm.next() * polSm.next();
        // config switch state machine (latency changes): fade out, switch, wait for the new path to fill, fade in
        switch (ts)
        {
            case tsFadeOut:
                tGain -= fadeStep;
                if (tGain <= 0)
                {
                    tGain = 0;
                    const bool needConvReset = target.mode > 0 && (cur.mode == 0 || target.firLen != cur.firLen);
                    const bool needSpecReset = target.spectral && ! cur.spectral;
                    cur = target; curLatency.store (latencyOf (cur));
                    if (needConvReset) conv.reset();
                    if (needSpecReset) spec.reset();
                    warm = (needConvReset || needSpecReset) ? latencyOf (cur) + 64 : 64;
                    if (cur.mode > 0 && irLoadedLen != cur.firLen) warm = std::max (warm, cur.firLen + (int) (0.05 * sr));
                    ts = tsWarm;
                }
                break;
            case tsWarm:
                if (--warm <= 0 && (cur.mode == 0 || irLoadedLen == cur.firLen)) ts = tsFadeIn;
                break;
            case tsFadeIn:
                tGain += fadeStep; if (tGain >= 1) { tGain = 1; ts = tsRun; }
                break;
            default: break;
        }
        L[i] = xl * g * tGain; R[i] = xr * g * tGain;
    }
    if (ana) postFifo.push (L, R, n);
}

// ============================================================================ factory + state
std::unique_ptr<UnitDsp> createUnit() { return std::make_unique<EqUnit>(); }

juce::ValueTree saveState (const UnitDsp& unit)
{
    juce::ValueTree t ("EQ");
    if (auto* u = dynamic_cast<const EqUnit*> (&unit))
    {
        auto& e = const_cast<EqUnit&> (*u);
        const juce::ScopedLock sl (e.uiLock);
        t.setProperty ("ab", e.abActive, nullptr); t.setProperty ("ana", e.anaMode, nullptr);
        t.setProperty ("range", e.dispRange, nullptr); t.setProperty ("peak", e.peakHold, nullptr);
        t.appendChild (e.abSlot[0].createCopy(), nullptr); t.appendChild (e.abSlot[1].createCopy(), nullptr);
    }
    return t;
}
void loadState (UnitDsp& unit, const juce::ValueTree& t)
{
    auto* u = dynamic_cast<EqUnit*> (&unit);
    if (u == nullptr || ! t.hasType ("EQ")) return;
    const juce::ScopedLock sl (u->uiLock);
    u->abActive = juce::jlimit (0, 1, (int) t.getProperty ("ab", 0));
    u->anaMode = juce::jlimit (0, 3, (int) t.getProperty ("ana", 3));
    u->dispRange = juce::jlimit (0, 3, (int) t.getProperty ("range", 1));
    u->peakHold = (bool) t.getProperty ("peak", true);
    if (auto a = t.getChildWithName ("A"); a.isValid()) u->abSlot[0] = a.createCopy();
    if (auto b = t.getChildWithName ("B"); b.isValid()) u->abSlot[1] = b.createCopy();
}

} // namespace eq
} // namespace gpfx
