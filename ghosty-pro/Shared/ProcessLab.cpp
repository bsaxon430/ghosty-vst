// PROCESS LAB engine (see ProcessLab.h for the integration note).
#include "ProcessLab.h"
#include "../GhostyPro/fx/SpectralCore.h"            // helpers (wrapPi)
#include "signalsmith-stretch/signalsmith-stretch.h"  // Signalsmith Stretch (MIT): stretch / shift / stretch-to
#include <juce_dsp/juce_dsp.h>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <thread>
#include <complex>

namespace plab {

static constexpr float kPi = juce::MathConstants<float>::pi;
using Cpx = std::complex<float>;

// ================================================================ audio
Audio Audio::fromBuffer (const juce::AudioBuffer<float>& b, double r)
{
    Audio a; a.rate = r; a.resize (b.getNumChannels(), b.getNumSamples());
    for (int c = 0; c < b.getNumChannels(); ++c) std::copy (b.getReadPointer (c), b.getReadPointer (c) + b.getNumSamples(), a.ch[(size_t) c].begin());
    return a;
}
juce::AudioBuffer<float> Audio::toBuffer() const
{
    juce::AudioBuffer<float> b (channels(), length());
    for (int c = 0; c < channels(); ++c) if (length() > 0) std::copy (ch[(size_t) c].begin(), ch[(size_t) c].end(), b.getWritePointer (c));
    return b;
}
Audio Audio::slice (int start, int len) const
{
    const Region r = Region { start, len }.clipped (*this);
    Audio s; s.rate = rate; s.resize (channels(), r.length);
    for (int c = 0; c < channels(); ++c) std::copy (ch[(size_t) c].begin() + r.start, ch[(size_t) c].begin() + r.end(), s.ch[(size_t) c].begin());
    return s;
}
void Audio::splice (int start, int len, const Audio& with)
{
    const Region r = Region { start, len }.clipped (*this);
    static const std::vector<float> none;
    for (int c = 0; c < channels(); ++c)
    {
        auto& v = ch[(size_t) c];
        const std::vector<float>& w = with.channels() > 0 ? with.ch[(size_t) juce::jmin (c, with.channels() - 1)] : none;
        v.erase (v.begin() + r.start, v.begin() + r.end());
        v.insert (v.begin() + r.start, w.begin(), w.end());
    }
}
float Audio::peak() const { float p = 0; for (auto& c : ch) for (float v : c) p = juce::jmax (p, std::abs (v)); return p; }
float Audio::rms() const
{
    double s = 0; size_t n = 0; for (auto& c : ch) { for (float v : c) s += (double) v * v; n += c.size(); }
    return n ? (float) std::sqrt (s / (double) n) : 0.0f;
}
Region Region::clipped (const Audio& a) const
{
    Region r; r.start = juce::jlimit (0, a.length(), start); r.length = juce::jlimit (0, a.length() - r.start, length); return r;
}
uint32_t mixSeed (uint32_t seed, uint32_t index)
{
    uint32_t h = seed * 0x9e3779b9u ^ (index + 1) * 0x85ebca6bu; h ^= h >> 15; h *= 0xc2b2ae35u; h ^= h >> 13; return h ? h : 1u;
}
Audio monoMix (const Audio& a)
{
    Audio m; m.rate = a.rate; m.resize (1, a.length());
    if (a.channels() == 0) return m;
    const float g = 1.0f / (float) a.channels();
    for (auto& c : a.ch) for (size_t i = 0; i < c.size(); ++i) m.ch[0][i] += c[i] * g;
    return m;
}
float maxAbsDiff (const std::vector<float>& x, int order)
{
    std::vector<float> d = x;
    for (int o = 0; o < order; ++o) { for (size_t i = 0; i + 1 < d.size(); ++i) d[i] = d[i + 1] - d[i]; if (! d.empty()) d.pop_back(); }
    float m = 0; for (float v : d) m = juce::jmax (m, std::abs (v)); return m;
}
void normaliseTo (Audio& a, float peakDb)
{
    const float p = a.peak(); if (p < 1.0e-9f) return;
    const float g = juce::Decibels::decibelsToGain (peakDb) / p;
    for (auto& c : a.ch) for (float& v : c) v *= g;
}
int64_t maxOutSamples (double rate) { return juce::jmin ((int64_t) 1 << 26, (int64_t) (kMaxOutSeconds * juce::jmax (1000.0, rate))); }
int contextSamples (double rate) { return juce::jmax (32768, (int) (0.5 * juce::jmax (1000.0, rate))); }
int fftOrderFor (int order48k, double rate)
{
    const int add = (int) std::lround (std::log2 (juce::jmax (1000.0, rate) / 48000.0));
    return juce::jlimit (8, 15, order48k + add);
}

// ================================================================ op table
#define CH(list) Choice, list
static const OpInfo kInfo[(int) OpType::Count] = {
    { "cycShuffle", "CYCLE SHUFFLE", "WAVECYCLES", "shuffle the order of the wavecycles (zero-crossing cycles)", 3,
      { { "group", "cyc", 1, 64, 1, true }, { "amount", "", 0, 1, 1, false }, { "min len", "ms", 0.05f, 20, 0.5f, false } }, true, false, false },
    { "cycRepeat", "CYCLE REPEAT", "WAVECYCLES", "play every wavecycle several times (lower, stuttered)", 4,
      { { "group", "cyc", 1, 64, 1, true }, { "count", "x", 2, 16, 2, true }, { "chance", "", 0, 1, 1, false }, { "min len", "ms", 0.05f, 20, 0.5f, false } }, true, true, false },
    { "cycOmit", "CYCLE OMIT", "WAVECYCLES", "keep K cycles, drop D (shorter, higher, broken)", 5,
      { { "group", "cyc", 1, 64, 1, true }, { "keep", "cyc", 1, 16, 1, true }, { "drop", "cyc", 1, 16, 1, true }, { "random", "", 0, 1, 0, true, Toggle }, { "min len", "ms", 0.05f, 20, 0.5f, false } }, true, true, false },
    { "cycReverse", "CYCLE REVERSE", "WAVECYCLES", "reverse inside each cycle, the order of cycles, or both", 3,
      { { "group", "cyc", 1, 64, 1, true }, { "mode", "", 0, 2, 0, true, CH ("inside|order|both") }, { "min len", "ms", 0.05f, 20, 0.5f, false } }, false, false, false },
    { "cycInterleave", "CYCLE INTERLEAVE", "WAVECYCLES", "cut the region into streams and interleave their cycles", 3,
      { { "group", "cyc", 1, 64, 1, true }, { "streams", "", 2, 4, 2, true }, { "min len", "ms", 0.05f, 20, 0.5f, false } }, false, false, false },
    { "brassage", "BRASSAGE", "GRANULAR", "granular reassembly: grains around the time pointer, time + pitch scatter, stereo spread", 9,
      { { "grain", "ms", 2, 500, 40, false }, { "density", "x", 0.25f, 8, 2, false }, { "scatter", "ms", 0, 500, 20, false }, { "pitch rnd", "st", 0, 24, 0, false },
        { "stretch", "x", 0.25f, 4, 1, false }, { "transpose", "st", -24, 24, 0, false }, { "reverse", "%", 0, 100, 0, false }, { "spread", "", 0, 1, 0, false },
        { "quantise", "", 0, 4, 0, true, CH ("off|octaves|fifths|major|minor pent") } }, true, true, false },
    { "iterate", "ITERATE", "GRANULAR", "repeat the region with a random pitch walk (+/-24 st), gaps / overlaps and decay", 5,
      { { "repeats", "", 2, 32, 4, true }, { "pitch", "st", 0, 12, 1, false }, { "gap", "%", -75, 200, 0, false }, { "decay", "dB", 0, 24, 3, false }, { "jitter", "ms", 0, 200, 0, false } }, true, true, false },
    { "specBlur", "SPECTRAL BLUR", "SPECTRAL", "average the magnitudes over a time window (smeared, washed)", 3,
      { { "fft", "2^n", 9, 13, 11, true }, { "width", "ms", 10, 2000, 85, false }, { "phase rnd", "", 0, 1, 0, true, Toggle } }, true, false, false },
    { "specTrace", "SPECTRAL TRACE", "SPECTRAL", "keep only the N loudest bins of every frame", 2,
      { { "fft", "2^n", 9, 13, 11, true }, { "keep", "bins", 1, 512, 16, true } }, false, false, false },
    { "specFreeze", "SPECTRAL FREEZE", "SPECTRAL", "hold the spectrum at a position (crossfaded in, optional hold length past the region, release)", 7,
      { { "fft", "2^n", 9, 13, 11, true }, { "position", "%", 0, 100, 25, false }, { "wobble", "", 0, 1, 0, false }, { "hold", "s", 0, 60, 0, false },
        { "xfade", "ms", 0, 1000, 40, false }, { "release", "ms", 0, 5000, 60, false }, { "lock", "", 0, 1, 1, true, Toggle } }, true, true, false },
    { "specStretch", "STRETCH", "SPECTRAL", "time-stretch (pitch kept), optional transpose", 4,
      { { "factor", "x", 0.25f, 8, 2, false }, { "pitch", "st", -24, 24, 0, false }, { "formants", "", 0, 1, 0, true, Toggle }, { "cents", "ct", -100, 100, 0, false } }, true, true, false },
    { "specShift", "PITCH SHIFT", "SPECTRAL", "transpose (length kept), optional formant keep", 4,
      { { "pitch", "st", -36, 36, 12, false }, { "formants", "", 0, 1, 0, true, Toggle }, { "tonality", "Hz", 0, 16000, 8000, false }, { "cents", "ct", -100, 100, 0, false } }, true, false, false },
    { "envelope", "ENVELOPE", "SHAPE", "impose a drawn breakpoint envelope", 1,
      { { "depth", "", 0, 1, 1, false } }, false, false, true },
    { "extend", "EXTEND", "SHAPE", "loop the region (crossfaded) to a multiple of its length", 4,
      { { "factor", "x", 1, 16, 2, false }, { "xfade", "ms", 0, 500, 20, false }, { "pingpong", "", 0, 1, 0, true, Toggle }, { "curve", "", 0, 2, 0, true, CH ("auto|linear|equal power") } }, false, true, false },
    { "normalise", "NORMALISE", "SHAPE", "peak or RMS to a level", 2, { { "level", "dB", -24, 0, -1, false }, { "mode", "", 0, 1, 0, true, CH ("peak|rms") } }, false, false, false },
    { "reverse", "REVERSE", "SHAPE", "play backwards", 0, {}, false, false, false },
    { "fadeIn", "FADE IN", "SHAPE", "fade in (curve: - exp, 0 linear, + log; or S-curve / equal power)", 3,
      { { "length", "ms", 1, 10000, 100, false }, { "shape", "", -1, 1, 0, false }, { "type", "", 0, 2, 0, true, CH ("curve|s-curve|equal power") } }, false, false, false },
    { "fadeOut", "FADE OUT", "SHAPE", "fade out (curve: - exp, 0 linear, + log; or S-curve / equal power)", 3,
      { { "length", "ms", 1, 10000, 100, false }, { "shape", "", -1, 1, 0, false }, { "type", "", 0, 2, 0, true, CH ("curve|s-curve|equal power") } }, false, false, false },
    // ---- added
    { "gain", "GAIN", "BASICS", "change the level", 1, { { "gain", "dB", -60, 24, -6, false } }, false, false, false },
    { "invert", "INVERT", "BASICS", "flip the polarity", 0, {}, false, false, false },
    { "dcRemove", "DC REMOVE", "BASICS", "remove DC offset / sub-sonic drift (zero-phase high-pass)", 1, { { "cutoff", "Hz", 2, 40, 10, false } }, false, false, false },
    { "stereo", "STEREO", "BASICS", "mono / swap / width (M/S) / one side to both", 2,
      { { "mode", "", 0, 4, 2, true, CH ("mono|swap|width|left to both|right to both") }, { "width", "", 0, 2, 1.5f, false } }, false, false, false },
    { "varispeed", "VARISPEED", "PITCH", "tape-style resample: pitch and length change together", 2,
      { { "pitch", "st", -48, 48, -12, false }, { "cents", "ct", -100, 100, 0, false } }, false, true, false },
    { "drive", "DRIVE", "COLOUR", "4x oversampled saturation: soft / hard clip / fold / asymmetric", 4,
      { { "mode", "", 0, 3, 0, true, CH ("soft|hard|fold|asym") }, { "drive", "dB", 0, 48, 12, false }, { "mix", "", 0, 1, 1, false }, { "output", "dB", -24, 12, -6, false } }, false, false, false },
    { "bitcrush", "BITCRUSH", "COLOUR", "bit depth + sample-and-hold downsampling", 3,
      { { "bits", "", 1, 24, 8, true }, { "downsample", "x", 1, 64, 4, true }, { "mix", "", 0, 1, 1, false } }, false, false, false },
    { "filterSweep", "FILTER SWEEP", "COLOUR", "destructive resonant filter sweeping across the region", 5,
      { { "type", "", 0, 3, 0, true, CH ("low-pass|high-pass|band-pass|notch") }, { "start", "Hz", 20, 20000, 200, false }, { "end", "Hz", 20, 20000, 8000, false },
        { "reso", "Q", 0.5f, 20, 2, false }, { "curve", "", 0, 1, 0, true, CH ("log|linear") } }, false, false, false },
    { "freqShift", "FREQ SHIFT", "COLOUR", "frequency shifter (inharmonic) or ring modulator", 3,
      { { "shift", "Hz", -2000, 2000, 100, false }, { "mode", "", 0, 1, 0, true, CH ("shift|ring") }, { "mix", "", 0, 1, 1, false } }, false, false, false },
    { "specGate", "SPECTRAL GATE", "SPECTRAL", "keep only bins above (or below) a level", 4,
      { { "fft", "2^n", 9, 13, 11, true }, { "threshold", "dB", -100, 0, -40, false }, { "mode", "", 0, 1, 0, true, CH ("keep above|keep below") }, { "knee", "dB", 0, 24, 6, false } }, false, false, false },
    { "specBinShift", "BIN SHIFT", "SPECTRAL", "shift (Hz) and stretch the partials: inharmonic, metallic", 3,
      { { "fft", "2^n", 9, 13, 11, true }, { "shift", "Hz", -2000, 2000, 150, false }, { "stretch", "x", 0.5f, 2, 1, false } }, false, false, false },
    { "crossSynth", "CROSS SYNTH", "SPECTRAL", "imprint sound B (clipboard, else the audio after the region) onto the region", 3,
      { { "fft", "2^n", 9, 13, 11, true }, { "amount", "", 0, 1, 1, false }, { "mode", "", 0, 2, 0, true, CH ("vocode|morph|B phase") } }, false, false, false, true },
    { "specExtreme", "EXTREME STRETCH", "SPECTRAL", "very long smooth stretch (random-phase spectral frames)", 2,
      { { "factor", "x", 2, 200, 8, false }, { "window", "ms", 20, 2000, 250, false } }, true, true, false },
    { "stretchTo", "STRETCH TO BARS", "PITCH", "stretch the region to N beats at a tempo (0 = host tempo)", 3,
      { { "beats", "", 0.25f, 64, 4, false }, { "bpm", "", 0, 300, 0, false }, { "formants", "", 0, 1, 0, true, Toggle } }, true, true, false, false, true },
    { "cycAverage", "CYCLE AVERAGE", "WAVECYCLES", "replace runs of N cycles by their average shape (smoother, buzzy)", 3,
      { { "group", "cyc", 1, 64, 1, true }, { "average", "cyc", 2, 16, 4, true }, { "min len", "ms", 0.05f, 20, 0.5f, false } }, false, false, false },
    { "cycTranspose", "CYCLE TRANSPOSE", "WAVECYCLES", "squeeze each cycle and repeat it (octaves up, length kept)", 3,
      { { "group", "cyc", 1, 64, 1, true }, { "ratio", "x", 2, 8, 2, true }, { "min len", "ms", 0.05f, 20, 0.5f, false } }, false, false, false },
    { "cycDistort", "CYCLE DISTORT", "WAVECYCLES", "reshape every cycle: normalise / square / power", 4,
      { { "group", "cyc", 1, 64, 1, true }, { "amount", "", 0, 1, 1, false }, { "shape", "", 0, 2, 0, true, CH ("normalise|square|power") }, { "min len", "ms", 0.05f, 20, 0.5f, false } }, false, false, false },
};
#undef CH
const OpInfo& info (OpType t) { return kInfo[juce::jlimit (0, (int) OpType::Count - 1, (int) t)]; }
const OpInfo* infoById (const juce::String& id) { for (auto& i : kInfo) if (id == i.id) return &i; return nullptr; }
juce::StringArray choiceNames (const ParamInfo& p) { return p.choices ? juce::StringArray::fromTokens (p.choices, "|", "") : juce::StringArray(); }

static constexpr int kStateVersion = 2;   // v2: blur width in ms (v1: frames of 512 samples at 48 kHz)

void Op::setDefaults()
{
    const auto& I = info (type);
    p.fill (0.0f); for (int i = 0; i < I.numParams; ++i) p[(size_t) i] = I.params[i].def;
    env.clear();
    if (I.hasEnvelope) env = { { 0.0f, 0.0f, 0.0f }, { 0.08f, 1.0f, 0.0f }, { 0.6f, 0.55f, 0.0f }, { 1.0f, 0.0f, 0.0f } };
}
void Op::sanitise()
{
    const auto& I = info (type);
    for (int i = 0; i < kMaxParams; ++i)
    {
        float& v = p[(size_t) i];
        if (! std::isfinite (v)) v = i < I.numParams ? I.params[i].def : 0.0f;
        if (i < I.numParams) { v = juce::jlimit (I.params[i].min, I.params[i].max, v); if (I.params[i].integer) v = std::round (v); }
        else v = 0.0f;
    }
    if ((int) env.size() > kMaxEnvPoints) env.resize ((size_t) kMaxEnvPoints);
    for (auto& pt : env)
    {
        pt.x = std::isfinite (pt.x) ? juce::jlimit (0.0f, 1.0f, pt.x) : 0.0f; pt.y = std::isfinite (pt.y) ? juce::jlimit (0.0f, 1.0f, pt.y) : 0.0f;
        pt.curve = std::isfinite (pt.curve) ? juce::jlimit (-1.0f, 1.0f, pt.curve) : 0.0f;
    }
    std::stable_sort (env.begin(), env.end(), [] (auto& a, auto& b) { return a.x < b.x; });
}
juce::ValueTree Op::toValueTree() const
{
    juce::ValueTree v ("OP"); v.setProperty ("id", info (type).id, nullptr); v.setProperty ("on", enabled, nullptr); v.setProperty ("v", kStateVersion, nullptr);
    for (int i = 0; i < info (type).numParams; ++i) v.setProperty ("p" + juce::String (i), (double) p[(size_t) i], nullptr);
    if (! env.empty())
    {
        juce::ValueTree e ("ENV");
        for (auto& pt : env)
        {
            juce::ValueTree q ("PT"); q.setProperty ("x", (double) pt.x, nullptr); q.setProperty ("y", (double) pt.y, nullptr);
            if (pt.curve != 0.0f) q.setProperty ("c", (double) pt.curve, nullptr);
            e.appendChild (q, nullptr);
        }
        v.appendChild (e, nullptr);
    }
    return v;
}
static double finiteProp (const juce::ValueTree& v, const juce::Identifier& id, double def)
{
    const auto& var = v.getProperty (id); if (var.isVoid()) return def;
    const double d = (double) var; return std::isfinite (d) ? d : def;
}
bool Op::fromValueTree (const juce::ValueTree& v, Op& out)
{
    if (! v.hasType ("OP")) return false;
    const OpInfo* I = infoById (v.getProperty ("id").toString()); if (I == nullptr) return false;
    out = Op ((OpType) (int) (I - kInfo));
    out.enabled = (bool) v.getProperty ("on", true);
    const int ver = (int) juce::jlimit (0.0, 100.0, finiteProp (v, "v", 1.0));
    for (int i = 0; i < kMaxParams; ++i)
    {
        const juce::Identifier id ("p" + juce::String (i));
        const double d = finiteProp (v, id, (double) out.p[(size_t) i]);
        out.p[(size_t) i] = (float) juce::jlimit (-1.0e9, 1.0e9, d);
    }
    if (ver < 2 && out.type == OpType::SpecBlur && v.hasProperty ("p1")) out.p[1] *= 512.0f / 48.0f;   // frames (hop 512 @ 48 kHz) -> ms
    auto e = v.getChildWithName ("ENV");
    if (e.isValid())
    {
        out.env.clear();
        for (auto q : e)
        {
            if ((int) out.env.size() >= kMaxEnvPoints) break;
            out.env.push_back ({ (float) finiteProp (q, "x", 0.0), (float) finiteProp (q, "y", 0.0), (float) finiteProp (q, "c", 0.0) });
        }
    }
    out.sanitise();
    return true;
}
juce::ValueTree Chain::toValueTree() const
{
    juce::ValueTree v ("PLCHAIN"); v.setProperty ("seed", (juce::int64) seed, nullptr); v.setProperty ("edgeMs", (double) edgeMs, nullptr);
    for (auto& o : ops) v.appendChild (o.toValueTree(), nullptr);
    return v;
}
Chain Chain::fromValueTree (const juce::ValueTree& v)
{
    Chain c; if (! v.hasType ("PLCHAIN")) return c;
    const double s = finiteProp (v, "seed", 1.0);
    c.seed = (uint32_t) (juce::int64) juce::jlimit (1.0, 4294967295.0, std::abs (s)); if (c.seed == 0) c.seed = 1;
    c.edgeMs = (float) juce::jlimit (0.0, 50.0, finiteProp (v, "edgeMs", 3.0));
    for (auto ch : v) { if ((int) c.ops.size() >= kMaxChainOps) break; Op o; if (Op::fromValueTree (ch, o)) c.ops.push_back (o); }
    return c;
}
juce::String Chain::toXml() const { return toValueTree().toXmlString(); }
Chain Chain::fromXml (const juce::String& xml) { return fromValueTree (juce::ValueTree::fromXml (xml)); }
bool Chain::changesLength() const { for (auto& o : ops) if (o.enabled && info (o.type).changesLength) return true; return false; }

// ================================================================ small helpers
namespace {
constexpr int kTickBlock = 65536;
struct Prog {   // a progress window [a, b] inside the whole job
    Progress* p; float a, b;
    bool tick (float f) { return p == nullptr || p->tick (a + juce::jlimit (0.0f, 1.0f, f) * (b - a)); }
    bool alive() const { return p == nullptr || p->alive(); }
    Prog sub (float f0, float f1) const { return { p, a + f0 * (b - a), a + f1 * (b - a) }; }
};
// sample loop helper: tick every 64k samples
struct Ticker {
    Prog& pr; int64_t total, count = 0, nextTick = kTickBlock;
    bool step (int64_t n = 1) { count += n; if (count < nextTick) return true; nextTick = count + kTickBlock; return pr.tick ((float) ((double) count / (double) juce::jmax ((int64_t) 1, total))); }
};
struct Ctx {
    const Audio& s; const Audio* pre = nullptr; const Audio* post = nullptr; juce::String* note = nullptr;
    void addNote (const juce::String& n) const { if (note != nullptr && ! note->contains (n)) *note << (note->isEmpty() ? "" : "; ") << n; }
};
// 4-point Hermite with zeros outside
inline float hermite (float xm, float x0, float x1, float x2, float t)
{
    const float c = 0.5f * (x1 - xm), v = x0 - x1, w = c + v, a = w + v + 0.5f * (x2 - x0), b = w + a;
    return ((a * t - b) * t + c) * t + x0;
}
inline float readAt (const std::vector<float>& x, double pos)
{
    const int n = (int) x.size(); if (n == 0) return 0.0f;
    const int i = (int) std::floor (pos); const float t = (float) (pos - i);
    auto at = [&] (int k) { return (k >= 0 && k < n) ? x[(size_t) k] : 0.0f; };
    return hermite (at (i - 1), at (i), at (i + 1), at (i + 2), t);
}
inline int reflectIdx (int k, int n)
{
    if (n <= 1) return 0;
    const int period = 2 * (n - 1); k %= period; if (k < 0) k += period;
    return k < n ? k : period - k;
}
inline float readReflect (const std::vector<float>& x, double pos)
{
    const int n = (int) x.size(); if (n == 0) return 0.0f;
    const int i = (int) std::floor (pos); const float t = (float) (pos - i);
    auto at = [&] (int k) { return x[(size_t) reflectIdx (k, n)]; };
    return hermite (at (i - 1), at (i), at (i + 1), at (i + 2), t);
}
inline float fadeShape (float t, float shape, int type = 0)
{
    t = juce::jlimit (0.0f, 1.0f, t);
    if (type == 1) return t * t * (3.0f - 2.0f * t);                 // S-curve
    if (type == 2) return std::sin (t * kPi * 0.5f);                 // equal power
    if (std::abs (shape) < 1.0e-6f) return t;
    return shape > 0 ? std::pow (t, 1.0f / (1.0f + 3.0f * shape)) : std::pow (t, 1.0f - 3.0f * shape);
}
Audio emptyLike (const Audio& s, int len) { Audio o; o.rate = s.rate; o.resize (s.channels(), juce::jmax (0, len)); return o; }
int capLen (int64_t want, double rate, const Ctx& cx)
{
    const int64_t cap = maxOutSamples (rate);
    if (want > cap) { cx.addNote ("output capped at " + juce::String ((double) cap / rate, 0) + " s"); return (int) cap; }
    return (int) juce::jmax ((int64_t) 0, want);
}
// region + handles: [pre tail (<= H)] + s + [post head (<= H)]
Audio withHandles (const Ctx& cx, int H, int& preLen, int& postLen)
{
    const Audio& s = cx.s; preLen = 0; postLen = 0;
    if (cx.pre != nullptr && cx.pre->channels() == s.channels()) preLen = juce::jmin (H, cx.pre->length());
    if (cx.post != nullptr && cx.post->channels() == s.channels()) postLen = juce::jmin (H, cx.post->length());
    Audio o = emptyLike (s, preLen + s.length() + postLen);
    for (int c = 0; c < s.channels(); ++c)
    {
        auto& y = o.ch[(size_t) c];
        if (preLen > 0) std::copy (cx.pre->ch[(size_t) c].end() - preLen, cx.pre->ch[(size_t) c].end(), y.begin());
        std::copy (s.ch[(size_t) c].begin(), s.ch[(size_t) c].end(), y.begin() + preLen);
        if (postLen > 0) std::copy (cx.post->ch[(size_t) c].begin(), cx.post->ch[(size_t) c].begin() + postLen, y.begin() + preLen + s.length());
    }
    return o;
}
Audio trimmed (const Audio& a, int start, int len)
{
    Audio o = emptyLike (a, len);
    for (int c = 0; c < a.channels(); ++c)
        for (int i = 0; i < len; ++i) { const int k = start + i; o.ch[(size_t) c][(size_t) i] = (k >= 0 && k < a.length()) ? a.ch[(size_t) c][(size_t) k] : 0.0f; }
    return o;
}
// zero-phase one-pole filters (forward + backward)
void onePoleLP (std::vector<float>& x, float a)   // y += a (x - y)
{
    float y = x.empty() ? 0.0f : x.front(); for (float& v : x) { y += a * (v - y); v = y; }
    y = x.empty() ? 0.0f : x.back(); for (auto it = x.rbegin(); it != x.rend(); ++it) { y += a * (*it - y); *it = y; }
}
void zeroPhaseHP (std::vector<float>& x, double hz, double rate)
{
    if (x.empty()) return;
    const float a = (float) (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * hz / rate));
    std::vector<float> lp = x; onePoleLP (lp, a);
    for (size_t i = 0; i < x.size(); ++i) x[i] -= lp[i];
}
struct BiquadC { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };
BiquadC lowpass (double fc, double rate, double q = 0.7071)
{
    const double w = 2.0 * juce::MathConstants<double>::pi * juce::jlimit (1.0, rate * 0.49, fc) / rate, al = std::sin (w) / (2 * q), cw = std::cos (w), a0 = 1 + al;
    BiquadC c; c.b0 = (1 - cw) * 0.5 / a0; c.b1 = (1 - cw) / a0; c.b2 = c.b0; c.a1 = -2 * cw / a0; c.a2 = (1 - al) / a0; return c;
}
void biquadZeroPhase (std::vector<float>& x, const BiquadC& c)
{
    auto pass = [&] (auto begin, auto end)
    {
        double z1 = 0, z2 = 0; bool first = true;
        for (auto it = begin; it != end; ++it)
        {
            const double in = *it;
            if (first) { z1 = in * (1 - c.b0); z2 = in * (c.b2 - c.a2); first = false; }   // start in steady state for the first value
            const double y = c.b0 * in + z1; z1 = c.b1 * in - c.a1 * y + z2; z2 = c.b2 * in - c.a2 * y; *it = (float) y;
        }
    };
    pass (x.begin(), x.end()); pass (x.rbegin(), x.rend());
}
float corrOf (const float* a, const float* b, int n)
{
    double ab = 0, aa = 0, bb = 0; for (int i = 0; i < n; ++i) { ab += (double) a[i] * b[i]; aa += (double) a[i] * a[i]; bb += (double) b[i] * b[i]; }
    return aa > 1e-20 && bb > 1e-20 ? (float) (ab / std::sqrt (aa * bb)) : 1.0f;
}
} // namespace

// ================================================================ wavecycles
std::vector<int> cycleUnits (const Audio& slice, int group) { return cycleUnits (slice, group, 0.5f); }
std::vector<int> cycleUnits (const Audio& slice, int group, float minLenMs)
{
    std::vector<int> b;
    const Audio m = monoMix (slice); if (m.channels() == 0 || m.length() < 4) return b;
    std::vector<float> y = m.ch[0];
    zeroPhaseHP (y, 10.0, slice.rate);                                                        // DC / drift
    const float a = (float) (1.0 - std::exp (-2.0 * juce::MathConstants<double>::pi * 2000.0 / slice.rate));
    onePoleLP (y, a); onePoleLP (y, a);                                                       // 4-pole zero-phase low-pass at ~2 kHz
    double s2 = 0; for (float v : y) s2 += (double) v * v;
    const float rms = (float) std::sqrt (s2 / (double) y.size());
    if (rms < 1.0e-7f) return b;
    const float h = 0.1f * rms;                                                               // hysteresis
    const int minLen = juce::jmax (1, (int) std::round (minLenMs * 0.001 * slice.rate));
    bool armed = false; int last = -minLen;
    for (int i = 1; i < (int) y.size(); ++i)
    {
        if (y[(size_t) i] < -h) armed = true;
        if (armed && y[(size_t) i - 1] < 0.0f && y[(size_t) i] >= 0.0f && i - last >= minLen) { b.push_back (i); last = i; armed = false; }
    }
    group = juce::jmax (1, group);
    if (group > 1) { std::vector<int> g; for (size_t i = 0; i < b.size(); i += (size_t) group) g.push_back (b[i]); b.swap (g); }
    return b;
}
namespace {
struct Piece { int a, e; bool rev; };
// write pieces in order; at every join crossfade (raised cosine, <= 1/8 of the shorter piece, <= 2 ms) from the continuation of the
// previous piece into the new one, per channel. Length = sum of pieces.
Audio assemblePieces (const Audio& s, const std::vector<Piece>& pieces, Prog& pr)
{
    int64_t total = 0; for (auto& p : pieces) total += p.e - p.a;
    Audio o = emptyLike (s, (int) total);
    const int n = s.length(); const int maxX = juce::jmax (0, (int) (0.005 * s.rate));
    Ticker tk { pr, total * s.channels() };
    for (int c = 0; c < s.channels(); ++c)
    {
        const auto& x = s.ch[(size_t) c]; auto& y = o.ch[(size_t) c]; int w = 0;
        for (size_t k = 0; k < pieces.size(); ++k)
        {
            const auto& p = pieces[k]; const int L = p.e - p.a;
            for (int i = 0; i < L; ++i) y[(size_t) (w + i)] = p.rev ? x[(size_t) (p.e - 1 - i)] : x[(size_t) (p.a + i)];
            if (k > 0)
            {
                const auto& q = pieces[k - 1];
                const bool natural = ! q.rev && ! p.rev && q.e == p.a;
                const int X = juce::jmin (maxX, juce::jmin (L, q.e - q.a) / 2);
                if (! natural && X >= 2)
                    for (int i = 0; i < X; ++i)
                    {
                        const int ci = q.rev ? q.a - 1 - i : q.e + i;
                        const float cont = x[(size_t) reflectIdx (ci, n)];
                        const float wt = 0.5f - 0.5f * std::cos (kPi * (float) (i + 1) / (float) (X + 1));
                        y[(size_t) (w + i)] = cont * (1.0f - wt) + y[(size_t) (w + i)] * wt;
                    }
            }
            w += L;
            if (! tk.step (L)) return o;
        }
    }
    return o;
}
// resample piece [a, e) of x to `len` samples (Hermite, positions spanning the piece)
inline float pieceAt (const std::vector<float>& x, int a, int e, double t01)
{
    const double pos = a + t01 * (double) (e - a);
    return readReflect (x, juce::jmin (pos, (double) e - 1.0e-6));
}
bool cycleOp (const Ctx& cx, const Op& op, uint32_t seed, Audio& out, Prog& pr)
{
    const Audio& s = cx.s;
    const float minMs = op.p[(size_t) (info (op.type).numParams - 1)];
    const auto b = cycleUnits (s, (int) op.p[0], minMs);
    const int K = (int) b.size() - 1;
    if (K < 2) { if (s.length() > 0) cx.addNote ("no wavecycles found (silence or noise?)"); out = s; return pr.tick (1.0f); }
    Rng rng (seed); std::vector<Piece> pieces;
    pieces.push_back ({ 0, b.front(), false });
    auto unit = [&] (int i, bool rev) { pieces.push_back ({ b[(size_t) i], b[(size_t) i + 1], rev }); };
    switch (op.type)
    {
        case OpType::CycleShuffle:
        {
            std::vector<int> idx ((size_t) K); std::iota (idx.begin(), idx.end(), 0);
            if (op.p[1] >= 0.999f) { for (int i = K - 1; i > 0; --i) std::swap (idx[(size_t) i], idx[(size_t) rng.below (i + 1)]); }
            else { const int swaps = (int) std::round (op.p[1] * (float) K); for (int sIdx = 0; sIdx < swaps; ++sIdx) std::swap (idx[(size_t) rng.below (K)], idx[(size_t) rng.below (K)]); }
            for (int i : idx) unit (i, false);
            break;
        }
        case OpType::CycleRepeat:
        {
            const int count = juce::jmax (1, (int) op.p[1]); int64_t len = b.front() + (s.length() - b.back());
            const int64_t cap = maxOutSamples (s.rate);
            for (int i = 0; i < K; ++i)
            {
                const int times = rng.uni() < op.p[2] ? count : 1;
                for (int t = 0; t < times; ++t) { const int L = b[(size_t) i + 1] - b[(size_t) i]; if (len + L > cap) { cx.addNote ("output capped"); break; } len += L; unit (i, false); }
            }
            break;
        }
        case OpType::CycleOmit:
        {
            const int keep = juce::jmax (1, (int) op.p[1]), drop = juce::jmax (1, (int) op.p[2]);
            size_t before = pieces.size();
            if (op.p[3] >= 0.5f)
            {
                const int nDrop = (int) std::round ((double) K * drop / (keep + drop));
                std::vector<int> idx ((size_t) K); std::iota (idx.begin(), idx.end(), 0);
                for (int i = K - 1; i > 0; --i) std::swap (idx[(size_t) i], idx[(size_t) rng.below (i + 1)]);
                std::vector<char> dropped ((size_t) K, 0); for (int i = 0; i < nDrop && i < K; ++i) dropped[(size_t) idx[(size_t) i]] = 1;
                for (int i = 0; i < K; ++i) if (! dropped[(size_t) i]) unit (i, false);
            }
            else for (int i = 0; i < K; ++i) if (i % (keep + drop) < keep) unit (i, false);
            if (pieces.size() == before) unit (0, false);
            break;
        }
        case OpType::CycleReverse:
        {
            const int mode = (int) op.p[1];
            for (int i = 0; i < K; ++i) { const int j = mode >= 1 ? K - 1 - i : i; unit (j, mode == 0 || mode == 2); }
            break;
        }
        case OpType::CycleInterleave:
        {
            const int S = juce::jlimit (2, 4, (int) op.p[1]);
            const int per = (K + S - 1) / S;
            for (int i = 0; i < per; ++i) for (int st = 0; st < S; ++st) { const int u = st * per + i; if (u < K) unit (u, false); }
            break;
        }
        case OpType::CycleAverage: case OpType::CycleTranspose: case OpType::CycleDistort:
        {
            // in-place reshaping (length kept); joins between reshaped cycles meet at zero crossings
            out = s;
            const float peakAll = s.peak();
            const int avgN = juce::jmax (2, (int) op.p[1]); const int ratio = juce::jmax (2, (int) op.p[1]);
            const float amount = op.p[1]; const int shape = (int) op.p[2];
            Ticker tk { pr, (int64_t) s.length() * s.channels() };
            for (int c = 0; c < s.channels(); ++c)
            {
                const auto& x = s.ch[(size_t) c]; auto& y = out.ch[(size_t) c];
                if (op.type == OpType::CycleAverage)
                    for (int r0 = 0; r0 < K; r0 += avgN)
                    {
                        const int r1 = juce::jmin (K, r0 + avgN);
                        for (int u = r0; u < r1; ++u)
                        {
                            const int a = b[(size_t) u], e = b[(size_t) u + 1], L = e - a;
                            for (int i = 0; i < L; ++i)
                            {
                                const double t = (double) i / (double) L; float acc = 0;
                                for (int v = r0; v < r1; ++v) acc += pieceAt (x, b[(size_t) v], b[(size_t) v + 1], t);
                                y[(size_t) (a + i)] = acc / (float) (r1 - r0);
                            }
                            if (! tk.step (L)) return false;
                        }
                    }
                else
                    for (int u = 0; u < K; ++u)
                    {
                        const int a = b[(size_t) u], e = b[(size_t) u + 1], L = e - a;
                        float pk = 0; for (int i = a; i < e; ++i) pk = juce::jmax (pk, std::abs (x[(size_t) i]));
                        for (int i = 0; i < L; ++i)
                        {
                            float v;
                            if (op.type == OpType::CycleTranspose) v = pieceAt (x, a, e, std::fmod ((double) i * ratio / (double) L, 1.0));
                            else
                            {
                                const float xi = x[(size_t) (a + i)];
                                const float g = pk > 1.0e-9f ? peakAll / pk : 0.0f;
                                float sh = xi;
                                if (shape == 0) sh = xi * g;
                                else if (shape == 1) sh = (xi >= 0 ? 1.0f : -1.0f) * pk * (pk > 1e-9f ? juce::jmin (1.0f, std::abs (xi) / pk * 8.0f) : 0.0f);
                                else sh = (xi >= 0 ? 1.0f : -1.0f) * pk * (pk > 1e-9f ? std::pow (std::abs (xi) / pk, 0.3f) : 0.0f);
                                v = xi + amount * (sh - xi);
                            }
                            y[(size_t) (a + i)] = v;
                        }
                        if (! tk.step (L)) return false;
                    }
            }
            return pr.tick (1.0f);
        }
        default: break;
    }
    pieces.push_back ({ b.back(), s.length(), false });
    if (! pr.tick (0.3f)) return false;
    Prog sub = pr.sub (0.3f, 1.0f);
    out = assemblePieces (s, pieces, sub);
    return pr.tick (1.0f);
}

} // namespace

// ================================================================ brassage / iterate
bool brassage (const Ctx& cx, const Op& op, uint32_t seed, Audio& out, Prog& pr)
{
    const Audio& s = cx.s;
    const int n = s.length(); const double sr = s.rate;
    const int grain = juce::jmax (4, (int) std::round (op.p[0] * 0.001 * sr));
    const float density = juce::jmax (0.05f, op.p[1]);
    const double scatter = op.p[2] * 0.001 * sr, pitchRnd = op.p[3], stretch = juce::jmax (0.01f, op.p[4]), transpose = op.p[5];
    const float revProb = op.p[6] * 0.01f, spread = op.p[7]; const int quant = (int) op.p[8];
    const int outLen = capLen ((int64_t) std::llround ((double) n * stretch), sr, cx);
    out = emptyLike (s, outLen);
    if (n == 0 || outLen == 0) return true;
    Rng rng (seed);
    const double hop = juce::jmax (1.0, grain / (double) density);
    std::vector<float> win ((size_t) grain); for (int i = 0; i < grain; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * kPi * ((float) i + 0.5f) / (float) grain);
    static const int scales[5][8] = { { 0 }, { 0, 12 }, { 0, 7, 12 }, { 0, 2, 4, 5, 7, 9, 11, 12 }, { 0, 3, 5, 7, 10, 12 } };
    static const int scaleN[5] = { 1, 2, 3, 8, 6 };
    auto quantise = [&] (double st)
    {
        if (quant <= 0) return st;
        const double oct = std::floor (st / 12.0), rem = st - oct * 12.0; double best = 0, bd = 1e9;
        for (int i = 0; i < scaleN[quant]; ++i) { const double d = std::abs (rem - scales[quant][i]); if (d < bd) { bd = d; best = scales[quant][i]; } }
        return oct * 12.0 + best;
    };
    const int nCh = s.channels(); int g = 0;
    for (double t = -grain * 0.5; t < outLen; t += hop, ++g)
    {
        if ((g & 31) == 0 && ! pr.tick ((float) ((t + grain) / (outLen + grain)))) return false;
        const double centre = t + grain * 0.5 + rng.bi() * hop * 0.25;                         // where the grain lands
        double srcCentre = centre / stretch + rng.bi() * scatter;                              // where it reads
        const double st = quantise (transpose + rng.bi() * pitchRnd);
        const double ratio = std::exp2 (st / 12.0);
        const bool rev = rng.uni() < revProb;
        const float pan = spread * rng.bi();
        const double span = grain * 0.5 * ratio;                                               // keep the whole grain inside the slice
        if (n - 1 > 2 * span) srcCentre = juce::jlimit (span, (double) (n - 1) - span, srcCentre); else srcCentre = (n - 1) * 0.5;
        const int w0 = (int) std::floor (centre - grain * 0.5);
        for (int c = 0; c < nCh; ++c)
        {
            float pg = 1.0f;
            if (nCh >= 2 && c < 2 && spread > 0) { const float th = (pan + 1.0f) * kPi * 0.25f; pg = juce::MathConstants<float>::sqrt2 * (c == 0 ? std::cos (th) : std::sin (th)); }
            const auto& x = s.ch[(size_t) c]; auto& y = out.ch[(size_t) c];
            for (int i = 0; i < grain; ++i)
            {
                const int o = w0 + i; if (o < 0 || o >= outLen) continue;
                const double off = (i - grain * 0.5) * ratio;
                y[(size_t) o] += pg * win[(size_t) i] * readReflect (x, srcCentre + (rev ? -off : off));
            }
        }
    }
    const float g0 = 0.5f * density;   // Hann overlap gain
    for (auto& c : out.ch) for (float& v : c) v /= g0;
    const float ri = s.rms(), ro = out.rms();   // keep the source's level
    if (ro > 1.0e-9f && ri > 1.0e-9f) { const float k = juce::jmin (4.0f, ri / ro); for (auto& c : out.ch) for (float& v : c) v *= k; }
    return pr.tick (1.0f);
}
bool iterate (const Ctx& cx, const Op& op, uint32_t seed, Audio& out, Prog& pr)
{
    const Audio& s = cx.s;
    const int n = s.length(); const double sr = s.rate;
    const int reps = juce::jmax (1, (int) op.p[0]);
    const double pitchSt = op.p[1], gap = op.p[2] * 0.01, decayDb = op.p[3], jitter = op.p[4] * 0.001 * sr;
    const int64_t cap = maxOutSamples (sr);
    Rng rng (seed);
    struct Rep { double start, ratio; float gain; int len; };
    std::vector<Rep> list; double pitch = 0.0, pos = 0.0; int64_t total = 0;
    for (int k = 0; k < reps; ++k)
    {
        if (k > 0)
        {
            pitch += rng.bi() * pitchSt;
            if (pitch > 24.0) pitch = 48.0 - pitch;   // reflect at +/-24 st
            if (pitch < -24.0) pitch = -48.0 - pitch;
        }
        const double ratio = std::exp2 (pitch / 12.0); const int len = juce::jmax (1, (int) std::round (n / ratio));
        const double start = k == 0 ? 0.0 : juce::jmax (0.0, pos + rng.bi() * jitter);
        if ((int64_t) std::ceil (start) + len > cap) { cx.addNote ("output capped at " + juce::String (kMaxOutSeconds, 0) + " s"); break; }
        list.push_back ({ start, ratio, (float) juce::Decibels::decibelsToGain (-decayDb * (double) k), len });
        total = juce::jmax (total, (int64_t) std::ceil (start) + len);
        pos = start + len * (1.0 + gap);
    }
    out = emptyLike (s, (int) juce::jmax ((int64_t) 1, total));
    if (n == 0) return true;
    const int F = juce::jmax (1, juce::jmin ((int) (0.003 * sr), n / 4));   // per-repeat fades
    int64_t work = 0; for (auto& r : list) work += r.len; work *= s.channels();
    Ticker tk { pr, work };
    for (size_t k = 0; k < list.size(); ++k)
    {
        const auto& r = list[k]; const int st = (int) std::round (r.start);
        for (int c = 0; c < s.channels(); ++c)
        {
            const auto& x = s.ch[(size_t) c]; auto& y = out.ch[(size_t) c];
            for (int i = 0; i < r.len && st + i < (int) y.size(); ++i)
            {
                float g = r.gain;
                if (k > 0 && i < F) g *= (float) i / (float) F;
                if (k + 1 < list.size() && i >= r.len - F) g *= (float) (r.len - 1 - i) / (float) F;
                y[(size_t) (st + i)] += g * (k == 0 ? x[(size_t) i] : readAt (x, i * r.ratio));
                if ((i & 4095) == 0 && ! tk.step (4096)) return false;
            }
        }
    }
    return pr.tick (1.0f);
}

// ================================================================ offline multichannel STFT (Hann / Hann, overlap 4)
namespace {
struct Ostft {
    int N, hop, bins; std::unique_ptr<juce::dsp::FFT> fft; std::vector<float> win, buf; float norm = 1.0f;
    explicit Ostft (int order)
    {
        fft = std::make_unique<juce::dsp::FFT> (order); N = 1 << order; hop = N / 4; bins = N / 2 + 1;
        win.resize ((size_t) N); for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * kPi * (float) i / (float) N);
        buf.assign ((size_t) N * 2, 0.0f);
        std::vector<float> t ((size_t) N * 2, 0.0f); t[3] = 1.0f;
        fft->performRealOnlyForwardTransform (t.data(), true); fft->performRealOnlyInverseTransform (t.data());
        const float g = std::abs (t[3]) > 1.0e-9f ? t[3] : 1.0f;
        norm = 1.0f / (g * 1.5f);   // periodic Hann^2 at 75 % overlap sums to 1.5
    }
    int frames (int len) const { return (len + N - hop + hop - 1) / hop; }            // frame k starts at k*hop - (N - hop)
    int startOf (int k) const { return k * hop - (N - hop); }
    void analyse (const std::vector<float>& x, int k, Cpx* spec)
    {
        const int s0 = startOf (k), L = (int) x.size();
        for (int i = 0; i < N; ++i) { const int j = s0 + i; buf[(size_t) i] = (j >= 0 && j < L) ? x[(size_t) j] * win[(size_t) i] : 0.0f; }
        std::fill (buf.begin() + N, buf.end(), 0.0f);
        fft->performRealOnlyForwardTransform (buf.data(), true);
        std::copy ((Cpx*) buf.data(), (Cpx*) buf.data() + bins, spec);
    }
    void synth (const Cpx* spec, std::vector<float>& y, int k)
    {
        std::copy (spec, spec + bins, (Cpx*) buf.data());
        for (int i = bins; i < N; ++i) { buf[(size_t) (2 * i)] = 0.0f; buf[(size_t) (2 * i + 1)] = 0.0f; }
        fft->performRealOnlyInverseTransform (buf.data());
        const int s0 = startOf (k), L = (int) y.size();
        for (int i = 0; i < N; ++i) { const int j = s0 + i; if (j >= 0 && j < L) y[(size_t) j] += buf[(size_t) i] * win[(size_t) i] * norm; }
    }
};
// process every frame of all channels: fn (frame, specs[ch], bins); mono runs once. in/out same length.
template <class Fn>
bool stftAll (const Audio& in, int order, Audio& out, Prog& pr, Fn&& fn)
{
    out = emptyLike (in, in.length());
    const int nCh = in.channels(), L = in.length(); if (nCh == 0 || L == 0) return true;
    Ostft st (order); const int F = st.frames (L);
    std::vector<std::vector<Cpx>> spec ((size_t) nCh, std::vector<Cpx> ((size_t) st.bins));
    std::vector<Cpx*> ptr ((size_t) nCh);
    for (int k = 0; k < F; ++k)
    {
        for (int c = 0; c < nCh; ++c) { st.analyse (in.ch[(size_t) c], k, spec[(size_t) c].data()); ptr[(size_t) c] = spec[(size_t) c].data(); }
        fn (k, ptr, st.bins);
        for (int c = 0; c < nCh; ++c) st.synth (spec[(size_t) c].data(), out.ch[(size_t) c], k);
        if ((k & 7) == 0 && ! pr.tick ((float) k / (float) F)) return false;
    }
    return true;
}
// run a spectral body over region + handles, then trim back to the region
template <class Body>
bool spectralWithHandles (const Ctx& cx, int order, Audio& out, Prog& pr, Body&& body)
{
    int preL = 0, postL = 0; const Audio x = withHandles (cx, 1 << order, preL, postL);
    Audio y; if (! body (x, y, pr)) return false;
    out = trimmed (y, preL, cx.s.length()); return true;
}
} // namespace

bool specBlur (const Ctx& cx, const Op& op, uint32_t seed, Audio& out, Prog& pr)
{
    const int order = fftOrderFor ((int) op.p[0], cx.s.rate); const bool randPhase = op.p[2] >= 0.5f;
    const double hopMs = 1000.0 * (double) ((1 << order) / 4) / cx.s.rate;
    const int w = juce::jmax (0, (int) std::round (op.p[1] / hopMs * 0.5));   // +/- frames
    return spectralWithHandles (cx, order, out, pr, [&] (const Audio& x, Audio& y, Prog& p) -> bool
    {
        y = emptyLike (x, x.length());
        const int nCh = x.channels(), L = x.length(); if (nCh == 0 || L == 0) return true;
        Ostft st (order); const int F = st.frames (L), B = st.bins;
        // ring of spectra for frames [f - w, f + w]
        const int R = 2 * w + 1;
        std::vector<std::vector<std::vector<Cpx>>> ring ((size_t) R, std::vector<std::vector<Cpx>> ((size_t) nCh, std::vector<Cpx> ((size_t) B)));
        std::vector<float> sum ((size_t) nCh * (size_t) B, 0.0f);
        std::vector<Cpx> o ((size_t) B); Rng rng (seed);
        auto slot = [&] (int k) -> std::vector<std::vector<Cpx>>& { return ring[(size_t) (k % R)]; };
        for (int g = 0; g < F + w; ++g)
        {
            if (g < F)
            {
                if (g - R >= 0) for (int c = 0; c < nCh; ++c) for (int k = 0; k < B; ++k) sum[(size_t) c * B + k] -= std::abs (slot (g)[(size_t) c][(size_t) k]);   // leaving the window
                for (int c = 0; c < nCh; ++c) { st.analyse (x.ch[(size_t) c], g, slot (g)[(size_t) c].data()); for (int k = 0; k < B; ++k) sum[(size_t) c * B + k] += std::abs (slot (g)[(size_t) c][(size_t) k]); }
            }
            else if (g - R >= 0)
                for (int c = 0; c < nCh; ++c) for (int k = 0; k < B; ++k) sum[(size_t) c * B + k] -= std::abs (slot (g)[(size_t) c][(size_t) k]), slot (g)[(size_t) c][(size_t) k] = 0.0f;
            const int f = g - w; if (f < 0) continue;
            const int f0 = juce::jmax (0, f - w), f1 = juce::jmin (F - 1, f + w); const float cnt = (float) (f1 - f0 + 1);
            for (int c = 0; c < nCh; ++c)
            {
                const auto& sp = slot (f)[(size_t) c];
                for (int k = 0; k < B; ++k)
                {
                    const float m = juce::jmax (0.0f, sum[(size_t) c * B + k]) / cnt;
                    const float a = std::abs (sp[(size_t) k]);
                    if (randPhase) o[(size_t) k] = std::polar (m, rng.uni() * 2.0f * kPi);
                    else o[(size_t) k] = a > 1.0e-12f ? sp[(size_t) k] * (m / a) : Cpx (m, 0.0f);
                }
                st.synth (o.data(), y.ch[(size_t) c], f);
            }
            if ((f & 7) == 0 && ! p.tick ((float) f / (float) F)) return false;
        }
        return true;
    });
}
bool specTrace (const Ctx& cx, const Op& op, Audio& out, Prog& pr)
{
    const int order = fftOrderFor ((int) op.p[0], cx.s.rate), keep = juce::jmax (1, (int) op.p[1]);
    return spectralWithHandles (cx, order, out, pr, [&] (const Audio& x, Audio& y, Prog& p) -> bool
    {
        std::vector<float> m, sorted;
        return stftAll (x, order, y, p, [&] (int, std::vector<Cpx*>& sp, int bins)
        {
            if (keep >= bins) return;
            m.assign ((size_t) bins, 0.0f);
            for (auto* s : sp) for (int k = 0; k < bins; ++k) m[(size_t) k] = juce::jmax (m[(size_t) k], std::abs (s[k]));
            sorted = m; std::nth_element (sorted.begin(), sorted.begin() + (keep - 1), sorted.end(), std::greater<float>());
            const float thr = sorted[(size_t) (keep - 1)]; int kept = 0;
            for (int k = 0; k < bins; ++k) { const bool on = m[(size_t) k] >= thr && kept < keep && thr > 0.0f; if (on) ++kept; else for (auto* s : sp) s[k] = 0.0f; }
        });
    });
}
bool specGate (const Ctx& cx, const Op& op, Audio& out, Prog& pr)
{
    const int order = fftOrderFor ((int) op.p[0], cx.s.rate); const float thr = op.p[1], knee = juce::jmax (0.01f, op.p[3]); const bool below = op.p[2] >= 0.5f;
    return spectralWithHandles (cx, order, out, pr, [&] (const Audio& x, Audio& y, Prog& p) -> bool
    {
        const float scale = 2.0f / (float) ((1 << order) / 2);   // full-scale sine -> 0 dB
        return stftAll (x, order, y, p, [&] (int, std::vector<Cpx*>& sp, int bins)
        {
            for (int k = 0; k < bins; ++k)
            {
                float m = 0; for (auto* s : sp) m = juce::jmax (m, std::abs (s[k]));
                const float db = juce::Decibels::gainToDecibels (m * scale, -200.0f);
                float g = juce::jlimit (0.0f, 1.0f, (db - (thr - knee)) / knee);
                if (below) g = 1.0f - juce::jlimit (0.0f, 1.0f, (db - thr) / knee);
                for (auto* s : sp) s[k] *= g;
            }
        });
    });
}
bool specBinShift (const Ctx& cx, const Op& op, Audio& out, Prog& pr)
{
    const int order = fftOrderFor ((int) op.p[0], cx.s.rate); const double shiftHz = op.p[1], stretch = op.p[2];
    return spectralWithHandles (cx, order, out, pr, [&] (const Audio& x, Audio& y, Prog& p) -> bool
    {
        const int N = 1 << order, hop = N / 4; const double binHz = x.rate / N;
        const int nCh = x.channels();
        std::vector<std::vector<float>> prevPh ((size_t) nCh), outPh ((size_t) nCh);
        std::vector<float> om, of;
        return stftAll (x, order, y, p, [&] (int, std::vector<Cpx*>& sp, int bins)
        {
            for (int c = 0; c < nCh; ++c)
            {
                auto& pp = prevPh[(size_t) c]; auto& op2 = outPh[(size_t) c];
                if ((int) pp.size() != bins) { pp.assign ((size_t) bins, 0.0f); op2.assign ((size_t) bins, 0.0f); }
                om.assign ((size_t) bins, 0.0f); of.assign ((size_t) bins, 0.0f);
                Cpx* s = sp[(size_t) c];
                for (int k = 0; k < bins; ++k)
                {
                    const float mag = std::abs (s[k]), ph = std::arg (s[k]);
                    const float expected = 2.0f * kPi * (float) k * (float) hop / (float) N;
                    const float d = spectral::wrapPi (ph - pp[(size_t) k] - expected); pp[(size_t) k] = ph;
                    const double trueBin = k + d * N / (2.0 * juce::MathConstants<double>::pi * hop);
                    const double nb = trueBin * stretch + shiftHz / binHz;
                    const int j = (int) std::lround (nb);
                    if (j < 0 || j >= bins) continue;
                    if (mag > om[(size_t) j]) of[(size_t) j] = (float) nb;
                    om[(size_t) j] += mag;
                }
                for (int j = 0; j < bins; ++j)
                {
                    op2[(size_t) j] = spectral::wrapPi (op2[(size_t) j] + 2.0f * kPi * of[(size_t) j] * (float) hop / (float) N);
                    s[j] = std::polar (om[(size_t) j], op2[(size_t) j]);
                }
            }
        });
    });
}
bool crossSynth (const Ctx& cx, const Op& op, Audio& out, Prog& pr)
{
    const int order = fftOrderFor ((int) op.p[0], cx.s.rate); const float amount = op.p[1]; const int mode = (int) op.p[2];
    std::shared_ptr<const Audio> B = op.side;
    if ((B == nullptr || B->empty()) && cx.post != nullptr && cx.post->length() > 0) B = std::make_shared<Audio> (*cx.post);
    if (B == nullptr || B->empty()) { cx.addNote ("cross synth: no B sound (copy one, or leave audio after the region)"); out = cx.s; return pr.tick (1.0f); }
    const Audio bm = monoMix (*B);
    return spectralWithHandles (cx, order, out, pr, [&] (const Audio& x, Audio& y, Prog& p) -> bool
    {
        Ostft sb (order); const int FB = juce::jmax (1, sb.frames (bm.length()));
        std::vector<Cpx> bs ((size_t) sb.bins); std::vector<float> envA, envB, mA, mB, pre;
        auto smooth = [&] (const std::vector<float>& m, std::vector<float>& e)
        {
            const int n = (int) m.size(); pre.assign ((size_t) n + 1, 0.0f); for (int i = 0; i < n; ++i) pre[(size_t) i + 1] = pre[(size_t) i] + m[(size_t) i];
            e.resize ((size_t) n); for (int i = 0; i < n; ++i) { const int r = juce::jmax (1, i / 12); const int a = juce::jmax (0, i - r), b = juce::jmin (n, i + r + 1); e[(size_t) i] = (pre[(size_t) b] - pre[(size_t) a]) / (float) (b - a) + 1.0e-9f; }
        };
        return stftAll (x, order, y, p, [&] (int f, std::vector<Cpx*>& sp, int bins)
        {
            sb.analyse (bm.ch[0], f % FB, bs.data());
            mB.resize ((size_t) bins); for (int k = 0; k < bins; ++k) mB[(size_t) k] = std::abs (bs[(size_t) k]);
            if (mode == 0) smooth (mB, envB);
            for (auto* s : sp)
            {
                mA.resize ((size_t) bins); for (int k = 0; k < bins; ++k) mA[(size_t) k] = std::abs (s[k]);
                if (mode == 0) smooth (mA, envA);
                for (int k = 0; k < bins; ++k)
                {
                    const float a = mA[(size_t) k];
                    if (mode == 0) { const float g = std::pow (envB[(size_t) k] / envA[(size_t) k], amount); s[k] *= juce::jmin (g, 1000.0f); }
                    else if (mode == 1) { const float m = std::pow (a + 1e-12f, 1.0f - amount) * std::pow (mB[(size_t) k] + 1e-12f, amount); s[k] = a > 1e-12f ? s[k] * (m / a) : Cpx (m, 0); }
                    else { const float ph = std::arg (bs[(size_t) k]), pa = std::arg (s[k]); s[k] = std::polar (a, pa + amount * spectral::wrapPi (ph - pa)); }
                }
            }
        });
    });
}

// freeze: analysis at the position (2 frames -> per-bin phase advance), crossfade into an identity-phase-locked resynthesis,
// optional hold beyond the region end, release.
bool specFreeze (const Ctx& cx, const Op& op, uint32_t seed, Audio& out, Prog& pr)
{
    const Audio& s = cx.s; const int n = s.length(); const double sr = s.rate;
    const int order = fftOrderFor ((int) op.p[0], sr); const float wobble = op.p[2]; const bool lock = op.p[6] >= 0.5f;
    const int N = 1 << order, hop = N / 4;
    int preL = 0, postL = 0; const Audio x = withHandles (cx, N, preL, postL);
    const int P = (int) std::round (op.p[1] * 0.01 * n);                                           // freeze point (slice coords)
    const int xf = (int) std::round (op.p[4] * 0.001 * sr), rel = (int) std::round (op.p[5] * 0.001 * sr);
    const int outLen = op.p[3] > 0 ? capLen ((int64_t) P + (int64_t) std::llround (op.p[3] * sr), sr, cx) : n;
    out = emptyLike (s, outLen);
    if (n == 0 || outLen == 0) return true;
    Ostft st (order); const int B = st.bins; Rng rng (seed);
    const int z0 = juce::jmax (0, P - xf);                                                        // frozen sound starts here
    // frame k of the synth covers [z0 - N + k*hop, ...): use Ostft framing on a buffer starting at z0 - N
    const int synthLen = outLen - z0 + 2 * N;
    std::vector<Cpx> a0 ((size_t) B), a1 ((size_t) B), o ((size_t) B);
    std::vector<float> mag ((size_t) B), ph0 ((size_t) B), inc ((size_t) B), ph ((size_t) B); std::vector<int> owner ((size_t) B);
    for (int c = 0; c < s.channels(); ++c)
    {
        // analyse at P (centre) on the handle buffer
        const int centre = preL + juce::jlimit (0, n, P);
        std::vector<float> seg ((size_t) N * 2, 0.0f);
        for (int i = 0; i < N + hop; ++i) { const int j = centre - N / 2 + i; seg[(size_t) i] = (j >= 0 && j < x.length()) ? x.ch[(size_t) c][(size_t) j] : 0.0f; }
        // Ostft frame k starts at k*hop - (N - hop): frame 3 starts at 2*hop - N + hop... simpler: analyse explicit windows
        std::vector<float> w0 (seg.begin(), seg.begin() + N), w1 (seg.begin() + hop, seg.begin() + hop + N);
        // analyse via a frame index whose start is 0: k = (N - hop) / hop = 3
        st.analyse (w0, 3, a0.data()); st.analyse (w1, 3, a1.data());
        for (int k = 0; k < B; ++k)
        {
            mag[(size_t) k] = std::abs (a1[(size_t) k]); ph0[(size_t) k] = std::arg (a1[(size_t) k]);
            const float expected = 2.0f * kPi * (float) k * (float) hop / (float) N;
            inc[(size_t) k] = expected + spectral::wrapPi (ph0[(size_t) k] - std::arg (a0[(size_t) k]) - expected);
        }
        // identity phase locking: every bin follows its nearest spectral peak
        std::vector<int> peaks; for (int k = 1; k + 1 < B; ++k) if (mag[(size_t) k] > mag[(size_t) k - 1] && mag[(size_t) k] >= mag[(size_t) k + 1]) peaks.push_back (k);
        if (peaks.empty()) peaks.push_back (0);
        for (int k = 0, pi = 0; k < B; ++k)
        {
            while (pi + 1 < (int) peaks.size() && std::abs (peaks[(size_t) pi + 1] - k) < std::abs (peaks[(size_t) pi] - k)) ++pi;
            owner[(size_t) k] = lock ? peaks[(size_t) pi] : k;
        }
        ph = ph0;
        std::vector<float> z ((size_t) synthLen, 0.0f);
        const int F = st.frames (synthLen);
        for (int f = 0; f < F; ++f)
        {
            for (int k = 0; k < B; ++k) if (owner[(size_t) k] == k) ph[(size_t) k] = spectral::wrapPi (ph[(size_t) k] + inc[(size_t) k] + wobble * rng.bi() * 0.6f);
            for (int k = 0; k < B; ++k)
            {
                const int pk = owner[(size_t) k];
                const float phase = pk == k ? ph[(size_t) k] : ph[(size_t) pk] + (ph0[(size_t) k] - ph0[(size_t) pk]);
                o[(size_t) k] = std::polar (mag[(size_t) k], phase);
            }
            st.synth (o.data(), z, f);
            if ((f & 7) == 0 && ! pr.tick (((float) c + (float) f / (float) F) / (float) s.channels())) return false;
        }
        // z[j] is output sample (z0 - N + j); the frame grid is aligned so the synth's phase at frame start matches the analysis at P
        auto& y = out.ch[(size_t) c]; const auto& src = s.ch[(size_t) c];
        for (int i = 0; i < outLen; ++i)
        {
            const float fz = (i >= z0) ? z[(size_t) (i - z0 + N)] : 0.0f;
            float wz = i < z0 ? 0.0f : (xf > 0 ? juce::jmin (1.0f, (float) (i - z0) / (float) xf) : 1.0f);
            const float sv = i < n ? src[(size_t) i] : 0.0f;
            const float a = std::sin (wz * kPi * 0.5f), b = std::cos (wz * kPi * 0.5f);
            float v = a * fz + b * sv;
            if (rel > 0 && i >= outLen - rel) v *= 0.5f + 0.5f * std::cos (kPi * (float) (i - (outLen - rel)) / (float) rel);
            y[(size_t) i] = v;
        }
    }
    return pr.tick (1.0f);
}
bool specExtreme (const Ctx& cx, const Op& op, uint32_t seed, Audio& out, Prog& pr)
{
    const Audio& s = cx.s; const int n = s.length(); const double sr = s.rate; const double factor = op.p[0];
    const int order = juce::jlimit (8, 16, (int) std::ceil (std::log2 (juce::jmax (256.0, op.p[1] * 0.001 * sr))));
    const int outLen = capLen ((int64_t) std::llround (n * factor), sr, cx);
    out = emptyLike (s, outLen); if (n == 0 || outLen == 0) return true;
    int preL = 0, postL = 0; const Audio x = withHandles (cx, 1 << order, preL, postL);
    Ostft st (order); const int N = st.N, hop = st.hop, B = st.bins; Rng rng (seed);
    const int F = st.frames (outLen);
    std::vector<Cpx> sp ((size_t) B); std::vector<float> phs ((size_t) B), seg ((size_t) N);
    for (int f = 0; f < F; ++f)
    {
        for (int k = 0; k < B; ++k) phs[(size_t) k] = rng.uni() * 2.0f * kPi;          // same random phases on every channel (stereo image kept)
        const double centreOut = st.startOf (f) + N * 0.5;
        const int centreIn = preL + (int) std::llround (centreOut / factor);
        for (int c = 0; c < s.channels(); ++c)
        {
            for (int i = 0; i < N; ++i) { const int j = centreIn - N / 2 + i; seg[(size_t) i] = (j >= 0 && j < x.length()) ? x.ch[(size_t) c][(size_t) j] : 0.0f; }
            st.analyse (seg, 3, sp.data());
            for (int k = 0; k < B; ++k) sp[(size_t) k] = std::polar (std::abs (sp[(size_t) k]), phs[(size_t) k]);
            st.synth (sp.data(), out.ch[(size_t) c], f);
        }
        if ((f & 7) == 0 && ! pr.tick ((float) f / (float) F)) return false;
    }
    const float ri = s.rms(), ro = out.rms();
    if (ri > 1e-9f && ro > 1e-9f) for (auto& c : out.ch) for (float& v : c) v *= ri / ro;
    return pr.tick (1.0f);
}

// ================================================================ stretch / shift (Signalsmith Stretch, deterministic engine, handles)
bool stretchRender (const Ctx& cx, double factor, float semitones, bool formants, float tonalityHz, uint32_t seed, Audio& out, Prog& pr)
{
    const Audio& s = cx.s; const int n = s.length(), nCh = s.channels();
    const int outLen = juce::jmax (1, capLen ((int64_t) std::llround (n * factor), s.rate, cx));
    factor = n > 0 ? (double) outLen / (double) n : factor;
    out = emptyLike (s, outLen);
    if (n == 0 || nCh == 0) return true;
    signalsmith::stretch::SignalsmithStretch<float, Rng> st ((long) seed);
    st.presetDefault (nCh, (float) s.rate);
    st.setTransposeSemitones (semitones, tonalityHz > 0 ? (float) (tonalityHz / s.rate) : 0.0f);
    st.setFormantFactor (1.0f, formants);
    int preL = 0, postL = 0; const int H = (int) (0.25 * s.rate);
    const Audio x = withHandles (cx, H, preL, postL);
    const int nx = x.length();
    const int outX = juce::jmax (1, (int) std::llround (nx * factor));
    const float playbackRate = (float) nx / (float) outX;
    const int seekLength = st.outputSeekLength (playbackRate);
    const int nPad = juce::jmax (nx, seekLength * 2 + 16);
    const int outPad = juce::jmax (outX, (int) std::round (nPad / (double) playbackRate));
    std::vector<std::vector<float>> X ((size_t) nCh), Y ((size_t) nCh);
    std::vector<float*> xp ((size_t) nCh), yp ((size_t) nCh);
    for (int c = 0; c < nCh; ++c) { X[(size_t) c] = x.ch[(size_t) c]; X[(size_t) c].resize ((size_t) nPad, 0.0f); Y[(size_t) c].assign ((size_t) outPad, 0.0f); xp[(size_t) c] = X[(size_t) c].data(); yp[(size_t) c] = Y[(size_t) c].data(); }
    st.outputSeek (xp.data(), seekLength);
    const int outputIndex = juce::jlimit (0, outPad, (int) (outPad - seekLength / playbackRate));
    const int inAvail = nPad - seekLength;
    int inDone = 0; const int chunk = 2048;
    std::vector<float*> xi ((size_t) nCh), yi ((size_t) nCh);
    for (int outDone = 0; outDone < outputIndex; )
    {
        const int oc = juce::jmin (chunk, outputIndex - outDone);
        const int inTarget = outDone + oc >= outputIndex ? inAvail : juce::jlimit (inDone, inAvail, (int) std::llround ((double) (outDone + oc) * inAvail / outputIndex));
        for (int c = 0; c < nCh; ++c) { xi[(size_t) c] = xp[(size_t) c] + seekLength + inDone; yi[(size_t) c] = yp[(size_t) c] + outDone; }
        st.process (xi.data(), inTarget - inDone, yi.data(), oc);
        inDone = inTarget; outDone += oc;
        if (! pr.tick ((float) outDone / (float) outPad)) return false;
    }
    for (int c = 0; c < nCh; ++c) yi[(size_t) c] = yp[(size_t) c] + outputIndex;
    st.flush (yi.data(), outPad - outputIndex, playbackRate);
    const int off = (int) std::llround (preL * factor);
    for (int c = 0; c < nCh; ++c)
        for (int i = 0; i < outLen; ++i) { const int j = off + i; out.ch[(size_t) c][(size_t) i] = j < outPad ? Y[(size_t) c][(size_t) j] : 0.0f; }
    return pr.tick (1.0f);
}

// ================================================================ time-domain ops
namespace {
bool envelopeOp (const Ctx& cx, const Op& op, Audio& out, Prog& pr)
{
    const Audio& s = cx.s; out = s; const int n = s.length(); if (n == 0 || op.env.empty()) return true;
    std::vector<EnvPoint> e = op.env; if ((int) e.size() > kMaxEnvPoints) e.resize ((size_t) kMaxEnvPoints);
    std::stable_sort (e.begin(), e.end(), [] (auto& a, auto& b) { return a.x < b.x; });
    const float depth = juce::jlimit (0.0f, 1.0f, op.p[0]);
    size_t seg = 1; Ticker tk { pr, (int64_t) n };
    for (int i = 0; i < n; ++i)
    {
        const float x = (float) i / (float) juce::jmax (1, n - 1); float v;
        if (x <= e.front().x) v = e.front().y;
        else if (x >= e.back().x) v = e.back().y;
        else
        {
            while (seg < e.size() && e[seg].x < x) ++seg;          // incremental: x only grows
            const auto& a = e[seg - 1]; const auto& b = e[juce::jmin (seg, e.size() - 1)]; const float dx = b.x - a.x;
            float t = dx > 1.0e-9f ? (x - a.x) / dx : 1.0f;
            if (a.curve != 0.0f) t = std::pow (t, std::pow (4.0f, -a.curve));
            v = a.y + (b.y - a.y) * t;
        }
        const float g = 1.0f + depth * (v - 1.0f);
        for (auto& c : out.ch) c[(size_t) i] *= g;
        if (! tk.step()) return false;
    }
    return true;
}
bool extendOp (const Ctx& cx, const Op& op, Audio& out, Prog& pr)
{
    const Audio& s = cx.s; const int n = s.length();
    const int total = juce::jmax (n, capLen ((int64_t) std::llround ((double) n * juce::jmax (1.0f, op.p[0])), s.rate, cx));
    out = emptyLike (s, total); if (n == 0) return true;
    const int xf = juce::jlimit (0, n / 2, (int) std::round (op.p[1] * 0.001 * s.rate)); const bool pingpong = op.p[2] >= 0.5f;
    int curve = (int) op.p[3];
    if (curve == 0 && xf > 0)   // auto: correlation of the loop end with the loop start
    {
        const float r = corrOf (s.ch[0].data() + (n - xf), s.ch[0].data(), xf);
        curve = r > 0.5f ? 1 : 2;
    }
    auto wIn = [&] (float t) { return curve == 2 ? std::sin (t * kPi * 0.5f) : t; };
    const int step = juce::jmax (1, n - xf);
    const int endFade = juce::jmax (xf, juce::jmin (n / 4, (int) (0.003 * s.rate)));
    Ticker tk { pr, (int64_t) total * s.channels() };
    for (int k = 0, pos = 0; pos < total; ++k, pos += step)
    {
        const bool rev = pingpong && (k & 1);
        const bool lastSeg = pos + n >= total;
        for (int c = 0; c < s.channels(); ++c)
        {
            const auto& x = s.ch[(size_t) c]; auto& y = out.ch[(size_t) c];
            for (int i = 0; i < n && pos + i < total; ++i)
            {
                float w = 1.0f;
                if (k > 0 && i < xf) w *= wIn ((float) (i + 1) / (float) (xf + 1));
                if (! lastSeg && i >= n - xf) w *= wIn ((float) (n - i) / (float) (xf + 1));
                y[(size_t) (pos + i)] += w * x[(size_t) (rev ? n - 1 - i : i)];
            }
            if (! tk.step (n)) return false;
        }
    }
    if (total > n)   // the last loop is cut wherever the length ends: fade it out
        for (auto& c : out.ch) for (int i = 0; i < endFade && i < total; ++i) c[(size_t) (total - 1 - i)] *= fadeShape ((float) i / (float) endFade, 0, 1);
    return true;
}
bool fadeOp (const Ctx& cx, const Op& op, bool in, Audio& out, Prog& pr)
{
    const Audio& s = cx.s; out = s; const int n = s.length(); if (n == 0) return true;
    const int L = juce::jlimit (1, n, (int) std::round (op.p[0] * 0.001 * s.rate)); const int type = (int) op.p[2];
    Ticker tk { pr, (int64_t) L };
    for (int i = 0; i < L; ++i)
    {
        const float g = fadeShape ((float) i / (float) L, op.p[1], type);
        for (auto& c : out.ch) c[(size_t) (in ? i : n - 1 - i)] *= g;
        if (! tk.step()) return false;
    }
    return true;
}
bool normaliseOp (const Ctx& cx, const Op& op, Audio& out)
{
    out = cx.s;
    if (op.p[1] < 0.5f) { normaliseTo (out, op.p[0]); return true; }
    const float r = out.rms(), pk = out.peak(); if (r < 1.0e-9f) return true;
    float g = juce::Decibels::decibelsToGain (op.p[0]) / r;
    if (pk * g > 1.0f) { g = 1.0f / pk; cx.addNote ("RMS normalise limited by the peak (0 dBFS)"); }
    for (auto& c : out.ch) for (float& v : c) v *= g;
    return true;
}
bool simpleMap (const Ctx& cx, Audio& out, Prog& pr, const std::function<float (int, int, float)>& f)   // f (channel, index, x)
{
    out = cx.s; Ticker tk { pr, (int64_t) out.length() * out.channels() };
    for (int c = 0; c < out.channels(); ++c) for (int i = 0; i < out.length(); ++i) { auto& v = out.ch[(size_t) c][(size_t) i]; v = f (c, i, v); if ((i & 4095) == 0 && ! tk.step (4096)) return false; }
    return true;
}
bool dcRemoveOp (const Ctx& cx, const Op& op, Audio& out, Prog& pr)
{
    int preL = 0, postL = 0; Audio x = withHandles (cx, (int) (cx.s.rate / juce::jmax (1.0f, op.p[0]) * 2), preL, postL);
    for (auto& c : x.ch) { zeroPhaseHP (c, op.p[0], x.rate); if (! pr.tick (0.5f)) return false; }
    out = trimmed (x, preL, cx.s.length()); return true;
}
bool stereoOp (const Ctx& cx, const Op& op, Audio& out)
{
    out = cx.s; if (out.channels() < 2) return true;
    const int mode = (int) op.p[0]; const float w = op.p[1];
    auto& L = out.ch[0]; auto& R = out.ch[1];
    for (size_t i = 0; i < L.size(); ++i)
    {
        const float l = L[i], r = R[i], m = 0.5f * (l + r), sd = 0.5f * (l - r);
        switch (mode)
        {
            case 0: L[i] = R[i] = m; break;
            case 1: L[i] = r; R[i] = l; break;
            case 2: L[i] = m + w * sd; R[i] = m - w * sd; break;
            case 3: R[i] = l; break;
            default: L[i] = r; break;
        }
    }
    return true;
}
bool varispeedOp (const Ctx& cx, const Op& op, Audio& out, Prog& pr)
{
    const Audio& s = cx.s; const int n = s.length();
    const double ratio = std::exp2 ((op.p[0] + op.p[1] * 0.01) / 12.0);
    const int outLen = capLen ((int64_t) std::llround (n / ratio), s.rate, cx);
    out = emptyLike (s, outLen); if (n == 0) return true;
    int preL = 0, postL = 0; Audio x = withHandles (cx, 4096, preL, postL);
    if (ratio > 1.0001) { const auto bq = lowpass (0.45 * s.rate / ratio, s.rate); for (auto& c : x.ch) { biquadZeroPhase (c, bq); biquadZeroPhase (c, bq); } }
    Ticker tk { pr, (int64_t) outLen * s.channels() };
    for (int c = 0; c < s.channels(); ++c)
        for (int i = 0; i < outLen; ++i)
        {
            out.ch[(size_t) c][(size_t) i] = readReflect (x.ch[(size_t) c], preL + i * ratio);
            if ((i & 4095) == 0 && ! tk.step (4096)) return false;
        }
    return true;
}
bool driveOp (const Ctx& cx, const Op& op, Audio& out, Prog& pr)
{
    if (cx.s.length() == 0 || cx.s.channels() == 0) { out = cx.s; return true; }
    int preL = 0, postL = 0; const Audio s = withHandles (cx, 2048, preL, postL);   // real audio around the region: no filter edge transients
    out = s; const int n = s.length(), nCh = s.channels();
    const int mode = (int) op.p[0]; const float drive = juce::Decibels::decibelsToGain (op.p[1]), mix = op.p[2], outG = juce::Decibels::decibelsToGain (op.p[3]);
    juce::dsp::Oversampling<float> os ((size_t) nCh, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, true);
    const int block = 1024; os.initProcessing ((size_t) block);
    const int lat = (int) std::round (os.getLatencyInSamples());
    juce::AudioBuffer<float> buf (nCh, block);
    auto shape = [&] (float v)
    {
        v *= drive;
        switch (mode)
        {
            case 0: return std::tanh (v);
            case 1: return juce::jlimit (-1.0f, 1.0f, v);
            case 2: { float t = v; for (int k = 0; k < 8 && std::abs (t) > 1.0f; ++k) t = t > 1.0f ? 2.0f - t : -2.0f - t; return juce::jlimit (-1.0f, 1.0f, t); }
            default: return v >= 0 ? std::tanh (v) : std::tanh (0.5f * v) * 1.6f;
        }
    };
    const int total = n + lat;
    for (int pos = 0; pos < total; pos += block)
    {
        const int m = juce::jmin (block, total - pos);
        buf.clear();
        for (int c = 0; c < nCh; ++c) for (int i = 0; i < m; ++i) { const int j = pos + i; buf.setSample (c, i, j < n ? s.ch[(size_t) c][(size_t) j] : 0.0f); }
        juce::dsp::AudioBlock<float> blk (buf.getArrayOfWritePointers(), (size_t) nCh, (size_t) m);
        auto up = os.processSamplesUp (blk);
        for (size_t c = 0; c < up.getNumChannels(); ++c) { auto* d = up.getChannelPointer (c); for (size_t i = 0; i < up.getNumSamples(); ++i) d[i] = shape (d[i]); }
        os.processSamplesDown (blk);
        for (int c = 0; c < nCh; ++c) for (int i = 0; i < m; ++i)
        {
            const int j = pos + i - lat; if (j < 0 || j >= n) continue;
            const float dry = s.ch[(size_t) c][(size_t) j];
            out.ch[(size_t) c][(size_t) j] = outG * (dry + mix * (buf.getSample (c, i) - dry));
        }
        if (! pr.tick ((float) pos / (float) total)) return false;
    }
    out = trimmed (out, preL, cx.s.length());
    return true;
}
bool bitcrushOp (const Ctx& cx, const Op& op, Audio& out, Prog& pr)
{
    const float q = std::exp2 ((float) op.p[0] - 1.0f); const int ds = juce::jmax (1, (int) op.p[1]); const float mix = op.p[2];
    out = cx.s; Ticker tk { pr, (int64_t) out.length() * out.channels() };
    for (auto& c : out.ch)
    {
        float held = 0;
        for (size_t i = 0; i < c.size(); ++i)
        {
            if (i % (size_t) ds == 0) held = std::round (c[i] * q) / q;
            c[i] += mix * (held - c[i]);
            if ((i & 4095) == 0 && ! tk.step (4096)) return false;
        }
    }
    return true;
}
bool filterSweepOp (const Ctx& cx, const Op& op, Audio& out, Prog& pr)
{
    const Audio& s = cx.s; out = s; const int n = s.length(); if (n == 0) return true;
    const int type = (int) op.p[0]; const double f0 = op.p[1], f1 = op.p[2], Q = op.p[3]; const bool lin = op.p[4] >= 0.5f;
    const double sr = s.rate; const int pre = cx.pre ? juce::jmin (cx.pre->length(), (int) (0.05 * sr)) : 0;
    Ticker tk { pr, (int64_t) n * s.channels() };
    for (int c = 0; c < s.channels(); ++c)
    {
        double ic1 = 0, ic2 = 0;
        auto stepF = [&] (double fc, double x, double& lp, double& bp, double& hp)
        {
            const double g = std::tan (juce::MathConstants<double>::pi * juce::jlimit (10.0, sr * 0.45, fc) / sr), k = 1.0 / Q;
            const double a1 = 1.0 / (1.0 + g * (g + k)), a2 = g * a1, a3 = g * a2;
            const double v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2; lp = v2; bp = v1; hp = x - k * v1 - v2;
        };
        double lp, bp, hp;
        if (pre > 0 && cx.pre->channels() > c) for (int i = cx.pre->length() - pre; i < cx.pre->length(); ++i) stepF (f0, cx.pre->ch[(size_t) c][(size_t) i], lp, bp, hp);   // warm up
        for (int i = 0; i < n; ++i)
        {
            const double t = (double) i / juce::jmax (1, n - 1);
            const double fc = lin ? f0 + (f1 - f0) * t : f0 * std::pow (f1 / f0, t);
            stepF (fc, s.ch[(size_t) c][(size_t) i], lp, bp, hp);
            const double y = type == 0 ? lp : type == 1 ? hp : type == 2 ? bp * (1.0 / Q) : lp + hp;
            out.ch[(size_t) c][(size_t) i] = (float) y;
            if ((i & 4095) == 0 && ! tk.step (4096)) return false;
        }
    }
    return true;
}
bool freqShiftOp (const Ctx& cx, const Op& op, Audio& out, Prog& pr)
{
    const Audio& s = cx.s; const double sr = s.rate, hz = op.p[0]; const bool ring = op.p[1] >= 0.5f; const float mix = op.p[2];
    out = s; const int n = s.length(); if (n == 0) return true;
    constexpr int T = 255, D = T / 2;
    std::vector<float> h ((size_t) T, 0.0f);
    for (int i = 0; i < T; ++i) { const int k = i - D; if (k % 2 != 0) h[(size_t) i] = (float) (2.0 / (juce::MathConstants<double>::pi * k)) * (0.54f - 0.46f * std::cos (2.0f * kPi * (float) i / (float) (T - 1))); }
    int preL = 0, postL = 0; const Audio x = withHandles (cx, T, preL, postL);
    Ticker tk { pr, (int64_t) n * s.channels() };
    for (int c = 0; c < s.channels(); ++c)
    {
        const auto& xc = x.ch[(size_t) c];
        for (int i = 0; i < n; ++i)
        {
            const int j = preL + i;
            const double ph = 2.0 * juce::MathConstants<double>::pi * hz * (double) i / sr;
            float wet;
            if (ring) wet = xc[(size_t) j] * (float) std::cos (ph);
            else
            {
                double im = 0; for (int k = 0; k < T; k += 1) { if (h[(size_t) k] == 0.0f) continue; const int q = j + D - k; if (q >= 0 && q < (int) xc.size()) im += h[(size_t) k] * xc[(size_t) q]; }
                wet = (float) (xc[(size_t) j] * std::cos (ph) - im * std::sin (ph));
            }
            out.ch[(size_t) c][(size_t) i] = xc[(size_t) j] + mix * (wet - xc[(size_t) j]);
            if ((i & 4095) == 0 && ! tk.step (4096)) return false;
        }
    }
    return true;
}
} // namespace

// ================================================================ dispatch
static bool renderOpCtx (const Ctx& cx, const Op& op, uint32_t seed, Audio& out, Prog pr)
{
    Op o = op; o.sanitise();
    const Audio& slice = cx.s;
    if (slice.channels() == 0) { out = slice; return true; }
    bool ok = true;
    switch (o.type)
    {
        case OpType::CycleShuffle: case OpType::CycleRepeat: case OpType::CycleOmit: case OpType::CycleReverse: case OpType::CycleInterleave:
        case OpType::CycleAverage: case OpType::CycleTranspose: case OpType::CycleDistort:
                                   ok = cycleOp (cx, o, seed, out, pr); break;
        case OpType::Brassage:     ok = brassage (cx, o, seed, out, pr); break;
        case OpType::Iterate:      ok = iterate (cx, o, seed, out, pr); break;
        case OpType::SpecBlur:     ok = specBlur (cx, o, seed, out, pr); break;
        case OpType::SpecTrace:    ok = specTrace (cx, o, out, pr); break;
        case OpType::SpecFreeze:   ok = specFreeze (cx, o, seed, out, pr); break;
        case OpType::SpecStretch:  ok = stretchRender (cx, o.p[0], o.p[1] + o.p[3] * 0.01f, o.p[2] >= 0.5f, 8000.0f, seed, out, pr); break;
        case OpType::SpecShift:    ok = stretchRender (cx, 1.0, o.p[0] + o.p[3] * 0.01f, o.p[1] >= 0.5f, o.p[2], seed, out, pr); break;
        case OpType::StretchTo:
        {
            const double bpm = o.p[1] > 0 ? o.p[1] : 120.0;
            const double want = o.p[0] * 60.0 / bpm * slice.rate;
            double f = slice.length() > 0 ? want / slice.length() : 1.0;
            if (f > 8.0 || f < 0.125) { f = juce::jlimit (0.125, 8.0, f); cx.addNote ("stretch limited to 0.125x..8x (EXTREME STRETCH goes further)"); }
            ok = stretchRender (cx, f, 0.0f, o.p[2] >= 0.5f, 8000.0f, seed, out, pr); break;
        }
        case OpType::SpecGate:     ok = specGate (cx, o, out, pr); break;
        case OpType::SpecBinShift: ok = specBinShift (cx, o, out, pr); break;
        case OpType::CrossSynth:   ok = crossSynth (cx, o, out, pr); break;
        case OpType::SpecExtreme:  ok = specExtreme (cx, o, seed, out, pr); break;
        case OpType::Envelope:     ok = envelopeOp (cx, o, out, pr); break;
        case OpType::Extend:       ok = extendOp (cx, o, out, pr); break;
        case OpType::Normalise:    ok = normaliseOp (cx, o, out); break;
        case OpType::Reverse:      out = slice; for (auto& c : out.ch) std::reverse (c.begin(), c.end()); break;
        case OpType::FadeIn:       ok = fadeOp (cx, o, true, out, pr); break;
        case OpType::FadeOut:      ok = fadeOp (cx, o, false, out, pr); break;
        case OpType::Gain:         { const float g = juce::Decibels::decibelsToGain (o.p[0], -60.0f); ok = simpleMap (cx, out, pr, [g] (int, int, float v) { return v * g; }); break; }
        case OpType::Invert:       ok = simpleMap (cx, out, pr, [] (int, int, float v) { return -v; }); break;
        case OpType::DcRemove:     ok = dcRemoveOp (cx, o, out, pr); break;
        case OpType::Stereo:       ok = stereoOp (cx, o, out); break;
        case OpType::Varispeed:    ok = varispeedOp (cx, o, out, pr); break;
        case OpType::Drive:        ok = driveOp (cx, o, out, pr); break;
        case OpType::Bitcrush:     ok = bitcrushOp (cx, o, out, pr); break;
        case OpType::FilterSweep:  ok = filterSweepOp (cx, o, out, pr); break;
        case OpType::FreqShift:    ok = freqShiftOp (cx, o, out, pr); break;
        case OpType::Count:        out = slice; break;
    }
    if (! ok || ! pr.alive()) return false;
    for (auto& c : out.ch) for (float& v : c) if (! std::isfinite (v)) v = 0.0f;   // never let a NaN through
    return pr.tick (1.0f);
}
bool renderOp (const Audio& slice, const Op& op, uint32_t seed, Audio& out, Progress* prog)
{
    Ctx cx { slice }; juce::String note; cx.note = &note;
    const bool ok = renderOpCtx (cx, op, seed, out, Prog { prog, 0.0f, 1.0f });
    if (prog != nullptr && note.isNotEmpty()) prog->note = note;
    return ok;
}

// adaptive edge blend: only where the joint would click (step > 2x the local slope)
static void blendEdges (Audio& res, const Audio& src, Region r, float edgeMs)
{
    const int L = res.length(), nCh = juce::jmin (res.channels(), src.channels());
    if (edgeMs <= 0 || L < 4 || nCh == 0) return;
    const int X = juce::jmin ((int) std::round (edgeMs * 0.001 * src.rate), L / 2, juce::jmax (2, r.length / 2));
    if (X < 2) return;
    auto slope = [] (const std::vector<float>& v, int a, int b) { float m = 0; a = juce::jmax (1, a); b = juce::jmin ((int) v.size(), b); for (int i = a; i < b; ++i) m = juce::jmax (m, std::abs (v[(size_t) i] - v[(size_t) i - 1])); return m; };
    auto blend = [&] (bool atStart)
    {
        bool clicks = false;
        for (int c = 0; c < nCh && ! clicks; ++c)
        {
            const auto& s = src.ch[(size_t) c]; const auto& y = res.ch[(size_t) c];
            const float jump = atStart ? std::abs (y[0] - s[(size_t) r.start - 1]) : std::abs (s[(size_t) r.end()] - y[(size_t) L - 1]);
            const float nat = atStart ? juce::jmax (slope (s, r.start - 32, r.start), slope (y, 0, 32)) : juce::jmax (slope (y, L - 32, L), slope (s, r.end(), r.end() + 32));
            clicks = jump > 2.0f * nat + 1.0e-4f;
        }
        if (! clicks) return;
        const int Xo = juce::jmin (X, r.length);
        for (int c = 0; c < nCh; ++c)
        {
            const auto& s = src.ch[(size_t) c]; auto& y = res.ch[(size_t) c];
            const float* orig = atStart ? s.data() + r.start : s.data() + r.end() - Xo;
            float* dst = atStart ? y.data() : y.data() + L - Xo;
            const bool lin = corrOf (orig, dst, Xo) > 0.5f;
            for (int i = 0; i < Xo; ++i)
            {
                float t = (float) (i + 1) / (float) (Xo + 1); if (! atStart) t = 1.0f - t;   // t = weight of the result
                const float wr = lin ? t : std::sin (t * kPi * 0.5f), wo = lin ? 1.0f - t : std::cos (t * kPi * 0.5f);
                dst[i] = wr * dst[i] + wo * orig[i];
            }
        }
    };
    if (r.start > 0) blend (true);
    if (r.end() < src.length()) blend (false);
}
struct SeededOp { const Op* op; uint32_t seed; };
static bool renderSeq (const Audio& src, Region r, const std::vector<SeededOp>& ops, float edgeMs, Audio& out, Progress* prog)
{
    r = r.clipped (src);
    const int H = contextSamples (src.rate);
    const Audio pre = src.slice (r.start - juce::jmin (H, r.start), juce::jmin (H, r.start));
    const Audio post = src.slice (r.end(), juce::jmin (H, src.length() - r.end()));
    Audio cur = src.slice (r.start, r.length);
    juce::String note;
    int k = 0; const int active = juce::jmax (1, (int) ops.size());
    for (auto& so : ops)
    {
        Audio next; Prog pr { prog, (float) k / (float) active, (float) (k + 1) / (float) active }; ++k;
        Ctx cx { cur, &pre, &post, &note };
        if (! renderOpCtx (cx, *so.op, so.seed, next, pr)) { if (prog) prog->note = note; return false; }
        cur = std::move (next);
    }
    blendEdges (cur, src, r, edgeMs);
    if (prog) { prog->note = note; prog->tick (1.0f); }
    out = std::move (cur); return true;
}
bool applyOp (Audio& a, Region& r, const Op& op, uint32_t seed, Progress* prog)
{
    r = r.clipped (a); Audio out;
    if (! renderSeq (a, r, { { &op, seed } }, 3.0f, out, prog)) return false;
    a.splice (r.start, r.length, out); r.length = out.length(); return true;
}
bool renderChain (const Audio& src, Region r, const Chain& c, Audio& out, Progress* prog)
{
    std::vector<SeededOp> ops;
    for (size_t i = 0; i < c.ops.size(); ++i) if (c.ops[i].enabled) ops.push_back ({ &c.ops[i], mixSeed (c.seed, (uint32_t) i) });
    return renderSeq (src, r, ops, c.edgeMs, out, prog);
}
bool applyChain (Audio& a, Region& r, const Chain& c, Progress* prog)
{
    r = r.clipped (a); Audio out;
    if (! renderChain (a, r, c, out, prog)) return false;
    a.splice (r.start, r.length, out); r.length = out.length(); return true;
}

// ================================================================ undo
size_t UndoStack::Entry::bytes() const { size_t b = 64; for (auto& c : samples) b += c.size() * sizeof (float); return b; }
UndoStack::Capture UndoStack::capture (const Audio& a, Region before, const juce::String& name)
{
    Capture c; before = before.clipped (a); c.start = before.start; c.name = name; c.samples = a.slice (before.start, before.length).ch; return c;
}
void UndoStack::push (Capture&& c, Region after)
{
    Entry e; e.name = c.name; e.start = c.start; e.otherLength = juce::jmax (0, after.length); e.samples = std::move (c.samples);
    future.clear(); past.push_back (std::move (e)); trim();
}
void UndoStack::swapIn (Audio& a, Entry& e)
{
    Audio stored; stored.rate = a.rate; stored.ch = std::move (e.samples);
    const int storedLen = stored.length();
    Audio current = a.slice (e.start, e.otherLength);
    a.splice (e.start, e.otherLength, stored);
    e.samples = std::move (current.ch); e.otherLength = storedLen;
}
bool UndoStack::undo (Audio& a, Region& restored)
{
    if (past.empty()) return false;
    Entry e = std::move (past.back()); past.pop_back();
    swapIn (a, e); restored = { e.start, e.otherLength };
    future.push_back (std::move (e)); trim(); return true;
}
bool UndoStack::redo (Audio& a, Region& restored)
{
    if (future.empty()) return false;
    Entry e = std::move (future.back()); future.pop_back();
    swapIn (a, e); restored = { e.start, e.otherLength };
    past.push_back (std::move (e)); trim(); return true;
}
size_t UndoStack::bytes() const { size_t b = 0; for (auto& e : past) b += e.bytes(); for (auto& e : future) b += e.bytes(); return b; }
void UndoStack::trim()
{
    while (bytes() > budget && past.size() + future.size() > 1)
    {
        if (! future.empty()) future.pop_front();        // the most distant redo goes first
        else if (past.size() > 1) past.pop_front();       // then the oldest undo; the newest step is always kept
        else break;
    }
}

// ================================================================ Lab
Lab::Lab() : juce::Thread ("ProcessLab") { startThread (juce::Thread::Priority::normal); }
Lab::~Lab()
{
    *alive = false;
    signalThreadShouldExit();
    { const juce::ScopedLock sl (jobLock); pending.reset(); if (workerProg) workerProg->cancel.store (true); }
    if (curProg) curProg->cancel.store (true);
    notify();
    stopThread (-1);   // the job stops within one block; never force-killed
}
void Lab::setAudio (Audio a)
{
    cancel(); audio = std::move (a); undo.clear(); previewAudio.reset(); region = Region::all (audio); previewFor = {}; ++chainGen;
    if (onChanged) onChanged();
}
void Lab::setRegion (Region r)
{
    r = r.clipped (audio);
    if (r != region) { region = r; ++chainGen; if (onChanged) onChanged(); }
}
void Lab::chainChanged()
{
    ++chainGen; previewDirty = true;
    if (running.load() && curProg && curKind == Kind::Preview) { curProg->cancel.store (true); running.store (false); ++jobGen; }   // a preview of the old chain is moot
    if (onChanged) onChanged();
}
void Lab::preview() { start (Kind::Preview); }
void Lab::apply() { start (Kind::Apply); }
void Lab::bounce() { start (Kind::Bounce); }
void Lab::cancel()
{
    if (curProg) curProg->cancel.store (true);
    ++jobGen; running.store (false);
    if (onChanged) onChanged();
}
std::shared_ptr<const Audio> Lab::regionAudio() const { return std::make_shared<Audio> (audio.slice (region.start, region.length)); }
std::shared_ptr<const Audio> Lab::sideFor (Region r) const
{
    if (clipboard != nullptr && ! clipboard->empty()) return clipboard;
    return std::make_shared<Audio> (audio.slice (r.end(), juce::jmax (r.length, (int) audio.rate)));
}
void Lab::start (Kind k)
{
    if (audio.empty()) { if (onMessage) onMessage ("no sound loaded"); return; }
    const Region r = region.clipped (audio);
    if (r.empty() && k != Kind::PerSlice) { if (onMessage) onMessage ("select a region first"); return; }
    if ((k == Kind::Apply || k == Kind::Bounce) && previewValid())   // reuse the preview: same chain, same region, same seed
    {
        if (curProg) curProg->cancel.store (true);
        ++jobGen; running.store (false);
        auto res = previewAudio; commit (*res, r, previewChain, k, chainGen);
        return;
    }
    auto j = std::make_unique<Job>();
    j->kind = k; j->c = chain; j->cgen = chainGen;
    for (auto& o : j->c.ops)
    {
        if (info (o.type).usesTempo && o.p[1] <= 0) o.p[1] = (float) (tempoBpm > 0 ? tempoBpm : 120.0);
        if (info (o.type).usesSide && o.side == nullptr) o.side = sideFor (r);
    }
    const int H = contextSamples (audio.rate);
    const Region span = r;
    const int off = juce::jmax (0, span.start - H), end = juce::jmin (audio.length(), span.end() + H);
    j->offset = off; j->src = audio.slice (off, end - off); j->r = { span.start - off, span.length };
    j->prog = std::make_shared<Progress>(); jobBytes = j->src.bytes();
    if (curProg) curProg->cancel.store (true);
    curProg = j->prog; j->gen = ++jobGen; curKind = k;
    { const juce::ScopedLock sl (jobLock); pending = std::move (j); }
    running.store (true);
    notify();
    if (onChanged) onChanged();
}
void Lab::applyPerSlice (std::vector<Region> slices)
{
    if (audio.empty()) return;
    std::vector<Region> ok; for (auto s : slices) { s = s.clipped (audio); if (! s.empty()) ok.push_back (s); }
    if (ok.empty()) { if (onMessage) onMessage ("no slices"); return; }
    std::sort (ok.begin(), ok.end(), [] (Region a, Region b) { return a.start < b.start; });
    for (size_t i = 1; i < ok.size(); ++i) if (ok[i].start < ok[i - 1].end()) ok[i].length = juce::jmax (0, ok[i].end() - ok[i - 1].end()), ok[i].start = ok[i - 1].end();
    auto j = std::make_unique<Job>();
    j->kind = Kind::PerSlice; j->c = chain; j->cgen = chainGen;
    for (auto& o : j->c.ops) if (info (o.type).usesTempo && o.p[1] <= 0) o.p[1] = (float) (tempoBpm > 0 ? tempoBpm : 120.0);
    const Region span { ok.front().start, ok.back().end() - ok.front().start };
    const int H = contextSamples (audio.rate);
    const int off = juce::jmax (0, span.start - H), end = juce::jmin (audio.length(), span.end() + H);
    j->offset = off; j->src = audio.slice (off, end - off); j->r = { span.start - off, span.length };
    for (auto s : ok) j->slices.push_back ({ s.start - off, s.length });
    j->prog = std::make_shared<Progress>();
    if (curProg) curProg->cancel.store (true);
    curProg = j->prog; j->gen = ++jobGen; curKind = Kind::PerSlice;
    { const juce::ScopedLock sl (jobLock); pending = std::move (j); }
    running.store (true); notify();
    if (onChanged) onChanged();
}
void Lab::repeatLast()
{
    if (lastChain.ops.empty()) { if (onMessage) onMessage ("nothing to repeat yet"); return; }
    const Chain keep = chain; chain = lastChain; ++chainGen;
    start (Kind::Apply);
    chain = keep; ++chainGen; previewDirty = true;
    // the running job keeps its own copy; its cgen differs from chainGen now, which only matters for previews / bounce clears
}
void Lab::run()
{
    while (! threadShouldExit())
    {
        std::unique_ptr<Job> j;
        { const juce::ScopedLock sl (jobLock); j = std::move (pending); if (j) workerProg = j->prog; }
        if (! j) { wait (200); continue; }
        auto result = std::make_shared<Audio>(); bool ok = false; juce::String err;
        try
        {
            if (j->kind == Kind::PerSlice)
            {
                Audio a = j->src; const int nS = (int) j->slices.size();
                ok = true;
                for (int i = nS - 1; i >= 0 && ok; --i)   // back to front: earlier positions stay valid
                {
                    Region rr = j->slices[(size_t) i];
                    ok = applyChain (a, rr, j->c, j->prog.get()) && j->prog->alive();
                    j->prog->fraction.store ((float) (nS - i) / (float) nS);
                }
                *result = std::move (a);
            }
            else ok = renderChain (j->src, j->r, j->c, *result, j->prog.get()) && j->prog->alive();
        }
        catch (const std::bad_alloc&) { ok = false; err = "out of memory: the result is too large"; result = std::make_shared<Audio>(); }
        catch (...) { ok = false; err = "processing failed"; result = std::make_shared<Audio>(); }
        { const juce::ScopedLock sl (jobLock); workerProg.reset(); }
        if (err.isEmpty()) err = j->prog->note;
        auto token = alive;
        auto* raw = j.release();
        juce::MessageManager::callAsync ([this, token, raw, ok, result, err]
        {
            std::unique_ptr<Job> job (raw);
            if (! token->load()) return;
            finished (std::move (job), ok, result, err);
        });
    }
}
void Lab::finished (std::unique_ptr<Job> j, bool ok, std::shared_ptr<Audio> result, juce::String note)
{
    if (j->gen != jobGen) return;                 // superseded / cancelled
    running.store (false);
    lastNote = note;
    if (! ok) { if (onMessage) onMessage (note.isNotEmpty() && ! j->prog->cancel.load() ? note : juce::String ("cancelled")); if (onChanged) onChanged(); return; }
    const Region fileRegion { j->offset + j->r.start, j->r.length };
    if (j->kind == Kind::Preview)
    {
        if (j->cgen != chainGen || fileRegion != region) { previewDirty = true; if (onChanged) onChanged(); return; }   // stale: the chain or region changed meanwhile
        previewAudio = result; previewRegion = { fileRegion.start, result->length() }; previewFor = fileRegion; previewDirty = false;
        previewGen = chainGen; previewChain = j->c;
        if (onMessage) onMessage ("preview ready: " + juce::String (result->length() / audio.rate, 2) + " s" + (note.isNotEmpty() ? "  (" + note + ")" : juce::String()));
    }
    else if (j->kind == Kind::PerSlice)
    {
        const Region before { j->offset, j->src.length() };
        edit (before, *result, "PER SLICE " + juce::String ((int) j->slices.size()), { j->offset + j->r.start, result->length() - (j->src.length() - j->r.end()) - j->r.start });
        lastChain = j->c;
        if (onMessage) onMessage ("processed " + juce::String ((int) j->slices.size()) + " slices" + (note.isNotEmpty() ? "  (" + note + ")" : juce::String()));
    }
    else commit (*result, fileRegion, j->c, j->kind, j->cgen);
    if (onChanged) onChanged();
}
void Lab::commit (const Audio& result, Region src, const Chain& used, Kind k, uint32_t cgen)
{
    int nOps = 0; for (auto& o : used.ops) if (o.enabled) ++nOps;
    auto cap = UndoStack::capture (audio, src, (k == Kind::Bounce ? "BOUNCE " : "APPLY ") + juce::String (nOps) + (nOps == 1 ? " op" : " ops"));
    audio.splice (src.start, src.length, result);
    const Region after { src.start, result.length() };
    undo.push (std::move (cap), after);
    const bool chainUnchanged = cgen == chainGen;
    region = after; previewAudio.reset(); previewFor = {}; ++chainGen;
    lastChain = used; for (auto& o : lastChain.ops) o.side.reset();
    if (k == Kind::Bounce && chainUnchanged) chain.ops.clear();   // only if nobody edited the chain meanwhile
    if (onCommitted) onCommitted (audio);
    if (onMessage) onMessage ((k == Kind::Bounce ? "bounced: " : "applied: ") + juce::String (nOps) + " op(s), undo " + juce::String ((double) undo.bytes() / 1048576.0, 1) + " MB"
                              + (lastNote.isNotEmpty() ? "  (" + lastNote + ")" : juce::String()));
    if (onChanged) onChanged();
}
bool Lab::edit (Region before, const Audio& with, const juce::String& name, Region select)
{
    if (running.load()) cancel();
    before = before.clipped (audio);
    auto cap = UndoStack::capture (audio, before, name);
    audio.splice (before.start, before.length, with);
    undo.push (std::move (cap), { before.start, with.length() });
    region = select.clipped (audio); previewAudio.reset(); previewFor = {}; ++chainGen;
    if (onCommitted) onCommitted (audio);
    if (onChanged) onChanged();
    return true;
}
bool Lab::undoStep()
{
    cancel(); Region r; if (! undo.undo (audio, r)) return false;
    region = r; previewAudio.reset(); previewFor = {}; ++chainGen;
    if (onCommitted) onCommitted (audio); if (onChanged) onChanged(); return true;
}
bool Lab::redoStep()
{
    cancel(); Region r; if (! undo.redo (audio, r)) return false;
    region = r; previewAudio.reset(); previewFor = {}; ++chainGen;
    if (onCommitted) onCommitted (audio); if (onChanged) onChanged(); return true;
}
static Audio conformTo (const Audio& clip, int channels, double rate)
{
    Audio a; a.rate = rate; if (clip.empty() || channels <= 0) { a.resize (channels, 0); return a; }
    const double ratio = clip.rate / rate; const int len = juce::jmax (1, (int) std::llround (clip.length() / ratio));
    a.resize (channels, len);
    for (int c = 0; c < channels; ++c)
    {
        std::vector<float> x = clip.ch[(size_t) juce::jmin (c, clip.channels() - 1)];
        if (ratio > 1.0001) { const auto bq = lowpass (0.45 * rate, clip.rate); biquadZeroPhase (x, bq); }
        for (int i = 0; i < len; ++i) a.ch[(size_t) c][(size_t) i] = std::abs (ratio - 1.0) < 1e-9 ? x[(size_t) juce::jmin (i, (int) x.size() - 1)] : readReflect (x, i * ratio);
    }
    return a;
}
bool Lab::crop() { if (region.empty()) return false; const Region r = region; return edit (Region::all (audio), audio.slice (r.start, r.length), "CROP", { 0, r.length }); }
bool Lab::deleteRegion() { if (region.empty()) return false; Audio e; e.rate = audio.rate; e.resize (audio.channels(), 0); return edit (region, e, "DELETE", { region.start, 0 }); }
bool Lab::silenceRegion() { if (region.empty()) return false; Audio z; z.rate = audio.rate; z.resize (audio.channels(), region.length); return edit (region, z, "SILENCE", region); }
bool Lab::insertSilence (double seconds)
{
    const int n = (int) juce::jlimit ((int64_t) 0, maxOutSamples (audio.rate), (int64_t) std::llround (seconds * audio.rate)); if (n <= 0) return false;
    Audio z; z.rate = audio.rate; z.resize (juce::jmax (1, audio.channels()), n);
    return edit ({ region.start, 0 }, z, "INSERT SILENCE", { region.start, n });
}
bool Lab::copyRegion() { if (region.empty()) return false; clipboard = regionAudio(); if (onMessage) onMessage ("copied " + juce::String (region.length / audio.rate, 3) + " s"); return true; }
bool Lab::cutRegion() { return copyRegion() && deleteRegion(); }
bool Lab::paste()
{
    if (clipboard == nullptr || clipboard->empty()) return false;
    const Audio c = conformTo (*clipboard, juce::jmax (1, audio.channels()), audio.rate);
    return edit ({ region.start, 0 }, c, "PASTE", { region.start, c.length() });
}
bool Lab::mixPaste (float gain)
{
    if (clipboard == nullptr || clipboard->empty()) return false;
    const Audio c = conformTo (*clipboard, juce::jmax (1, audio.channels()), audio.rate);
    const int span = juce::jmin (c.length(), audio.length() - region.start);
    Audio mixd = audio.slice (region.start, span);
    if (mixd.channels() == 0) mixd.resize (c.channels(), 0);
    for (int ch = 0; ch < mixd.channels(); ++ch)
    {
        auto& v = mixd.ch[(size_t) ch]; v.resize ((size_t) c.length(), 0.0f);
        for (int i = 0; i < c.length(); ++i) v[(size_t) i] += gain * c.ch[(size_t) ch][(size_t) i];
    }
    return edit ({ region.start, span }, mixd, "MIX PASTE", { region.start, c.length() });
}
bool Lab::findLoop()
{
    if (region.length < 64) return false;
    const Audio m = monoMix (audio); const auto& x = m.ch[0]; const int n = (int) x.size();
    const int s = region.start, e0 = region.end(); const int W = juce::jlimit (16, 4096, region.length / 8);
    const int lo = juce::jmax (s + region.length / 2, e0 - region.length / 20), hi = juce::jmin (n - 1, e0 + region.length / 20);
    // score: what follows the candidate end vs what follows the start (the loop jumps from e back to s)
    double best = -2; int bestE = e0;
    const float xs = x[(size_t) s], ds = s + 1 < n ? x[(size_t) s + 1] - xs : 0.0f;
    for (int e = lo; e <= hi; ++e)
    {
        // candidates: where the signal crosses the start's level with the start's slope direction (plus the current end)
        const bool cross = (x[(size_t) e - 1] - xs) * (x[(size_t) e] - xs) <= 0.0f && (x[(size_t) e] - x[(size_t) e - 1]) * ds >= 0.0f;
        if (! cross && e != e0) continue;
        const int w = juce::jmin (W, n - e, n - s); if (w < 8) continue;
        double sc = corrOf (x.data() + e, x.data() + s, w) - 0.5 * std::abs ((double) x[(size_t) e] - x[(size_t) s]);
        if (sc > best) { best = sc; bestE = e; }
    }
    setRegion ({ s, bestE - s });
    if (onMessage) onMessage ("loop end moved to " + juce::String (bestE / audio.rate, 4) + " s (match " + juce::String (best, 2) + ")");
    return true;
}

// ================================================================ audition player
void AuditionPlayer::play (std::shared_ptr<const Audio> clip, bool loop)
{
    if (clip == nullptr || clip->empty() || clip->channels() == 0) { stop(); return; }
    const double host = rate.load();
    if (std::abs (clip->rate - host) > 1.0e-6) clip = std::make_shared<Audio> (conformTo (*clip, clip->channels(), host));   // message thread: resample + anti-alias here
    const int a = active.load(); const int s = a == 0 ? 1 : 0;
    writing.store (true);
    while (inUse.load() == s) std::this_thread::yield();   // never touch a slot the audio thread reads (it holds it for one block at most)
    {
        const int len = clip->length(); const int F = (int) juce::jmax (1.0, juce::jmin (0.01 * clip->rate, len / 4.0));
        float r = 1.0f; if (len >= 8) { r = 0; for (auto& c : clip->ch) r += corrOf (c.data() + (len - F), c.data(), F); r /= (float) clip->channels(); }
        slotLinear[(size_t) s] = r > 0.5f;
    }
    slots[s] = std::move (clip);
    writing.store (false);
    loopFlag.store (loop);
    playGen.fetch_add (1);
    active.store (s);
    state.store (Playing);
}
void AuditionPlayer::stop() { if (state.load() == Playing) state.store (Stopping); }
void AuditionPlayer::release()
{
    if (state.load() != Idle) return;
    active.store (-1);
    writing.store (true);
    for (int s = 0; s < 2; ++s) { while (inUse.load() == s) std::this_thread::yield(); slots[s].reset(); }
    writing.store (false);
}
void AuditionPlayer::render (juce::AudioBuffer<float>& out, int startSample, int numSamples, float gain)
{
    const int a = active.load(); if (a < 0 || state.load() == Idle) return;
    inUse.store (a);
    if (writing.load() || active.load() != a) { inUse.store (-1); return; }   // a slot is being replaced right now: skip this block
    const Audio* clip = slots[a].get();
    if (clip == nullptr || clip->empty()) { inUse.store (-1); state.store (Idle); return; }
    const uint32_t gen = playGen.load();
    if (gen != startedGen) { startedGen = gen; readPos = 0.0; fade = 0.0f; tail = last; tailGain = 1.0f; }   // old clip's last value ramps out
    const double hostRate = rate.load(); const double ratio = clip->rate / hostRate;
    const int len = clip->length(), nCh = clip->channels(), outCh = out.getNumChannels();
    const bool loop = loopFlag.load() && len >= 8;
    const double fadeLen = juce::jmax (1.0, juce::jmin (0.005 * hostRate, len / 4.0));
    const float step = (float) (1.0 / fadeLen);
    const double F = juce::jmax (1.0, juce::jmin (0.01 * clip->rate, len / 4.0));   // loop crossfade (clip samples)
    const double body = len - F;
    auto rd = [&] (const std::vector<float>& x, double pos)
    {
        const int i = (int) std::floor (pos); const float t = (float) (pos - i);
        auto at = [&] (int k) { return x[(size_t) juce::jlimit (0, len - 1, k)]; };
        return hermite (at (i - 1), at (i), at (i + 1), at (i + 2), t);
    };
    for (int i = 0; i < numSamples; ++i)
    {
        const int st = state.load();
        if (st == Stopping) { fade -= step; if (fade <= 0.0f) { fade = 0.0f; last.fill (0.0f); state.store (Idle); break; } }
        else fade = juce::jmin (1.0f, fade + step);
        float edge = 1.0f; float wa = 1.0f, wb = 0.0f;
        if (loop && readPos >= body)
        {
            const float t = (float) ((readPos - body) / F);
            if (slotLinear[(size_t) a]) { wa = 1.0f - t; wb = t; } else { wa = std::cos (t * kPi * 0.5f); wb = std::sin (t * kPi * 0.5f); }
        }
        if (! loop) edge = (float) juce::jlimit (0.0, 1.0, (len - 1 - readPos) / (fadeLen * ratio));
        for (int c = 0; c < outCh; ++c)
        {
            const auto& x = clip->ch[(size_t) juce::jmin (c, nCh - 1)];
            float v = rd (x, readPos) * wa;
            if (wb > 0.0f) v += wb * rd (x, readPos - body);
            float y = v * fade * edge * gain;
            if (c < 8) { if (tailGain > 0.0f) y += tail[(size_t) c] * tailGain; last[(size_t) c] = y; }
            out.addSample (c, startSample + i, y);
        }
        tailGain = juce::jmax (0.0f, tailGain - step);
        readPos += ratio;
        if (loop) { if (readPos >= len) readPos -= body; }
        else if (readPos >= len - 1) { last.fill (0.0f); state.store (Idle); break; }
    }
    pos01.store (len > 1 ? (float) (juce::jmin (readPos, (double) len - 1) / (len - 1)) : 0.0f);
    inUse.store (-1);
}

} // namespace plab
