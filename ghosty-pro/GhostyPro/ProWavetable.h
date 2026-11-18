#pragma once
// WAVETABLE EDITOR engine (round 3 item 2b, part 1, 2026-10-24). Original code. Message thread only (allocates freely).
//
//   Doc        a user wavetable: up to kMaxFrames (256) single-cycle frames of TBL (2048) points, a name, the cycle length it was cut at
//              (0 = not from a sample) and the key frames a morph was built from.
//   analysis   per-frame harmonic magnitudes + phases (FFT) and the inverse (band-limited resynthesis happens in the existing mip
//              builder: whatever the editor makes, the oscillator reads a table limited to 0.45 fs per octave).
//   ops        normalise, DC remove, invert, reverse, smooth, morph / interpolate between key frames (time-domain cross-fade or
//              spectral: magnitudes and unwrapped phases interpolated), formula / generate (sine, saw, square, triangle, seeded noise +
//              harmonic recipes), resample an imported sample at a chosen cycle length (auto pitch detect through sliceSample).
//   files      our own JSON + float32 PCM (".ghostywt") in the user bank: name, points, cycle length, keys, base64 of the frames
//              (little-endian float32: a save -> load round trip is bit-exact). Also a plain float32 WAV export (2048-point frames
//              concatenated: the common wavetable WAV layout) and an import that takes such WAVs directly or slices any other audio.
#include "ProTables.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

namespace gpro::wt
{
constexpr int kBins = TBL / 2;        // harmonics 1 .. 1023 live in bins 1 .. kBins - 1
constexpr int kMaxFrames = 256;       // the editor's limit (a 256-frame table is 11 mips x 2048 x 256 floats = 23 MB built, 2 MB in a saved state)

struct Doc
{
    juce::String name { "User" };
    std::vector<HiFrame> frames;      // each TBL points
    int cycleLen = 0;                 // the cycle length (in source samples) a sample was cut at, 0 = synthetic
    std::vector<int> keys;            // key frames (indices) the in-between frames were morphed from
    bool made = false;                // the PLUGIN made these frames (seeded from a built-in table, generated, drawn, harmonics edited);
                                      // false = imported from the user's own sample (and unknown / older files). The demo keeps a made
                                      // table out of every save (ExportGate.h keepRenderedAudio): it is plugin-made audio in base64.
    bool valid() const noexcept { if (frames.empty() || frames.size() > (size_t) kMaxFrames) return false; for (auto& f : frames) if (f.size() != (size_t) TBL) return false; return true; }
};

struct Spectrum { std::vector<float> mag, phase; };    // index = harmonic number (0 unused), size kBins

inline juce::dsp::FFT& fft2048() { static juce::dsp::FFT f (11); return f; }

inline Spectrum analyze (const HiFrame& frame)
{
    using C = std::complex<float>;
    std::vector<C> in ((size_t) TBL), out ((size_t) TBL);
    for (int i = 0; i < TBL; ++i) in[(size_t) i] = C (frame[(size_t) (i % (int) frame.size())], 0.0f);
    fft2048().perform (in.data(), out.data(), false);
    Spectrum s; s.mag.assign ((size_t) kBins, 0.0f); s.phase.assign ((size_t) kBins, 0.0f);
    for (int k = 1; k < kBins; ++k) { s.mag[(size_t) k] = std::abs (out[(size_t) k]) * (2.0f / TBL); s.phase[(size_t) k] = std::arg (out[(size_t) k]); }
    return s;
}
inline HiFrame synth (const Spectrum& s)
{
    using C = std::complex<float>;
    std::vector<C> in ((size_t) TBL), out ((size_t) TBL);
    for (int k = 1; k < kBins && k < (int) s.mag.size(); ++k)
    {
        const C v = std::polar (s.mag[(size_t) k] * (TBL / 2.0f), s.phase[(size_t) k]);
        in[(size_t) k] = v; in[(size_t) (TBL - k)] = std::conj (v);
    }
    fft2048().perform (in.data(), out.data(), true);
    HiFrame f ((size_t) TBL);
    for (int i = 0; i < TBL; ++i) f[(size_t) i] = out[(size_t) i].real();
    return f;
}

// ---------------------------------------------------------------- frame ops (each returns a new frame)
inline HiFrame normalise (HiFrame f) { float pk = 0; for (auto v : f) pk = std::max (pk, std::abs (v)); if (pk > 1.0e-9f) for (auto& v : f) v /= pk; return f; }
inline HiFrame removeDC (HiFrame f) { double m = 0; for (auto v : f) m += v; m /= (double) f.size(); for (auto& v : f) v = (float) (v - m); return f; }
inline HiFrame invert (HiFrame f) { for (auto& v : f) v = -v; return f; }
inline HiFrame reverse (HiFrame f) { std::reverse (f.begin(), f.end()); return f; }
inline HiFrame smooth (const HiFrame& f, int passes = 1)
{   // a 17-point box (1 / 120 of the cycle) over the cycle (wrapping), repeated: rounds corners without touching the low harmonics
    HiFrame a = f, b (f.size());
    const int n = (int) f.size(), R = 8;
    for (int p = 0; p < passes; ++p)
    {
        for (int i = 0; i < n; ++i) { float s = 0; for (int k = -R; k <= R; ++k) s += a[(size_t) ((i + k + n) % n)]; b[(size_t) i] = s / (float) (2 * R + 1); }
        std::swap (a, b);
    }
    return a;
}
// rotate a cycle (whole samples, so the data stays exact) so its fundamental is a sine starting at 0 and rising: frames cut from
// different places of a sample then line up with each other and with the generated shapes
inline HiFrame alignFundamental (const HiFrame& f)
{
    const int n = (int) f.size(); double re = 0, im = 0;
    for (int i = 0; i < n; ++i) { const double w = juce::MathConstants<double>::twoPi * i / n; re += f[(size_t) i] * std::cos (w); im -= f[(size_t) i] * std::sin (w); }
    if (re * re + im * im < 1.0e-12) return f;
    const double phase = std::atan2 (im, re);                                   // the fundamental's phase as cos (w + phase)
    const int shift = ((int) std::lround ((-juce::MathConstants<double>::halfPi - phase) / juce::MathConstants<double>::twoPi * n) % n + n) % n;
    HiFrame out ((size_t) n);
    for (int i = 0; i < n; ++i) out[(size_t) i] = f[(size_t) ((i + shift) % n)];
    return out;
}

// ---------------------------------------------------------------- morph: fill the frames between the key frames
enum MorphMode { morphCrossfade = 0, morphSpectral };
// doc.keys sorted and unique, each < frames.size(); frames between two keys are replaced by the interpolation of the key frames
inline void morph (Doc& d, int mode)
{
    std::sort (d.keys.begin(), d.keys.end()); d.keys.erase (std::unique (d.keys.begin(), d.keys.end()), d.keys.end());
    if (d.keys.size() < 2) return;
    for (size_t k = 0; k + 1 < d.keys.size(); ++k)
    {
        const int a = d.keys[k], b = d.keys[k + 1];
        if (a < 0 || b >= (int) d.frames.size() || b - a < 2) continue;
        const auto& fa = d.frames[(size_t) a]; const auto& fb = d.frames[(size_t) b];
        Spectrum sa, sb;
        if (mode == morphSpectral) { sa = analyze (fa); sb = analyze (fb); }
        for (int i = a + 1; i < b; ++i)
        {
            const float t = (float) (i - a) / (float) (b - a);
            if (mode == morphCrossfade)
            {
                HiFrame f ((size_t) TBL);
                for (int n = 0; n < TBL; ++n) f[(size_t) n] = fa[(size_t) n] * (1.0f - t) + fb[(size_t) n] * t;
                d.frames[(size_t) i] = f;
            }
            else
            {   // magnitudes linear, phases along the shorter way round
                Spectrum s; s.mag.assign ((size_t) kBins, 0.0f); s.phase.assign ((size_t) kBins, 0.0f);
                for (int h = 1; h < kBins; ++h)
                {
                    s.mag[(size_t) h] = sa.mag[(size_t) h] * (1.0f - t) + sb.mag[(size_t) h] * t;
                    float dp = sb.phase[(size_t) h] - sa.phase[(size_t) h];
                    while (dp > juce::MathConstants<float>::pi) dp -= juce::MathConstants<float>::twoPi;
                    while (dp < -juce::MathConstants<float>::pi) dp += juce::MathConstants<float>::twoPi;
                    s.phase[(size_t) h] = sa.phase[(size_t) h] + dp * t;
                }
                d.frames[(size_t) i] = synth (s);
            }
        }
    }
}

// ---------------------------------------------------------------- formula / generate
enum Recipe { genSine = 0, genTriangle, genSaw, genSquare, genNoise, genOdd, genEven, genOneOverN, genOneOverN2, genBright, numRecipes };
inline const juce::StringArray& recipeNames() { static juce::StringArray s { "Sine", "Triangle", "Saw", "Square", "Noise", "Odd harmonics", "Even + 1", "1 / n", "1 / n squared", "Bright" }; return s; }
// harmonics: how many partials the recipe uses (1 .. kBins - 1); seed: for the noise; the result peaks at 1
inline HiFrame generate (int recipe, int harmonics, juce::uint32 seed = 1)
{
    harmonics = juce::jlimit (1, kBins - 1, harmonics);
    Spectrum s; s.mag.assign ((size_t) kBins, 0.0f); s.phase.assign ((size_t) kBins, 0.0f);
    auto hash01 = [] (juce::uint32 x) { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return (float) (x & 0xffffff) / 16777215.0f; };
    for (int h = 1; h <= harmonics; ++h)
    {
        float m = 0, p = -juce::MathConstants<float>::halfPi;       // (sine partials start at 0: a saw / square built this way starts at its zero crossing)
        switch (recipe)
        {
            case genSine:      m = h == 1 ? 1.0f : 0.0f; break;
            case genTriangle:  m = (h & 1) ? 1.0f / (float) (h * h) : 0.0f; p = (h % 4 == 3) ? juce::MathConstants<float>::halfPi : -juce::MathConstants<float>::halfPi; break;
            case genSaw:       m = 1.0f / (float) h; p = (h & 1) ? -juce::MathConstants<float>::halfPi : juce::MathConstants<float>::halfPi; break;
            case genSquare:    m = (h & 1) ? 1.0f / (float) h : 0.0f; break;
            case genNoise:     m = 1.0f / std::sqrt ((float) h); p = hash01 (seed * 2654435761U + (juce::uint32) h * 97u) * juce::MathConstants<float>::twoPi; break;
            case genOdd:       m = (h & 1) ? 1.0f / std::sqrt ((float) h) : 0.0f; break;
            case genEven:      m = (h == 1 || (h & 1) == 0) ? 1.0f / (float) h : 0.0f; break;
            case genOneOverN:  m = 1.0f / (float) h; break;
            case genOneOverN2: m = 1.0f / (float) (h * h); break;
            case genBright:    m = h <= 8 ? 1.0f : 1.0f / std::sqrt ((float) (h - 7)); break;
            default: break;
        }
        s.mag[(size_t) h] = m; s.phase[(size_t) h] = p;
    }
    return normalise (synth (s));
}
// a table that morphs from one recipe to another over `frames` frames (spectral morph)
inline Doc generateTable (int recipeA, int recipeB, int frames, int harmonics = 64, juce::uint32 seed = 1)
{
    Doc d; d.name = recipeNames()[juce::jlimit (0, numRecipes - 1, recipeA)] + " > " + recipeNames()[juce::jlimit (0, numRecipes - 1, recipeB)];
    frames = juce::jlimit (1, kMaxFrames, frames);
    d.frames.assign ((size_t) frames, HiFrame ((size_t) TBL, 0.0f));
    d.frames.front() = generate (recipeA, harmonics, seed);
    if (frames > 1) { d.frames.back() = generate (recipeB, harmonics, seed + 1); d.keys = { 0, frames - 1 }; morph (d, morphSpectral); }
    d.made = true;
    return d;
}

// ---------------------------------------------------------------- resample a sample into frames
// cycleLen <= 0: auto pitch detect (sliceSample's autocorrelation); otherwise every frame is one cycle of that many source samples,
// the frames spread evenly over the sample. detected (out) = the cycle length used.
inline Doc fromSample (const std::vector<float>& mono, double sr, int cycleLen, int frames, int* detected = nullptr)
{
    Doc d; d.name = "Sample";
    frames = juce::jlimit (1, kMaxFrames, frames);
    const int n = (int) mono.size();
    if (n < 64) return d;
    if (cycleLen <= 0)
    {
        auto sl = sliceSample (mono, sr, frames);
        if (sl.empty()) return d;
        for (auto& h : sl) h = alignFundamental (h);
        d.frames = sl; d.cycleLen = 0;
        double bestR = 0; const int period = detectPeriod (mono, sr, bestR);
        d.cycleLen = (period > 0 && bestR > 0.6) ? period : 0;
        if (detected) *detected = d.cycleLen;
        return d;
    }
    cycleLen = juce::jlimit (4, n, cycleLen);
    d.cycleLen = cycleLen; if (detected) *detected = cycleLen;
    const int count = juce::jlimit (1, frames, n / cycleLen);
    for (int f = 0; f < count; ++f)
    {
        const int start = count == 1 ? 0 : (int) ((double) f / (count - 1) * (n - cycleLen - 1));
        HiFrame h ((size_t) TBL);
        for (int i = 0; i < TBL; ++i)
        {   // one cycle resampled to TBL points, 4-point cubic between source samples
            const double pos = start + (double) i / TBL * cycleLen; const int a = (int) pos; const float t = (float) (pos - a);
            auto at = [&] (int k) { return mono[(size_t) juce::jlimit (0, n - 1, k)]; };
            const float ym1 = at (a - 1), y0 = at (a), y1 = at (a + 1), y2 = at (a + 2);
            const float c1 = 0.5f * (y1 - ym1), c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2, c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
            h[(size_t) i] = ((c3 * t + c2) * t + c1) * t + y0;
        }
        d.frames.push_back (alignFundamental (removeDC (h)));
    }
    return d;
}

// ---------------------------------------------------------------- files
// frames <-> base64 of little-endian float32 (bit-exact both ways; also used by the processor's USERTABLE state child)
inline juce::String framesToBase64 (const std::vector<HiFrame>& frames)
{
    juce::MemoryOutputStream mo;
    for (auto& f : frames) for (int i = 0; i < TBL; ++i) mo.writeFloat (f[(size_t) i % f.size()]);
    return mo.getMemoryBlock().toBase64Encoding();
}
inline std::vector<HiFrame> framesFromBase64 (const juce::String& s, int count)
{
    std::vector<HiFrame> out; juce::MemoryBlock mb;
    if (count <= 0 || count > kMaxFrames || ! mb.fromBase64Encoding (s) || mb.getSize() != (size_t) count * TBL * sizeof (float)) return out;
    juce::MemoryInputStream mi (mb, false);
    for (int k = 0; k < count; ++k)
    {
        HiFrame f ((size_t) TBL);
        for (int i = 0; i < TBL; ++i) { const float x = mi.readFloat(); f[(size_t) i] = std::isfinite (x) ? juce::jlimit (-4.0f, 4.0f, x) : 0.0f; }
        out.push_back (std::move (f));
    }
    return out;
}
inline juce::File userBankDir()
{
    auto d = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Ghosty").getChildFile ("Ghosty Pro").getChildFile ("Wavetables");
    d.createDirectory();
    return d;
}
inline juce::String fileExtension() { return ".ghostywt"; }

// A sound / board / snapshot tree for disk while the demo is locked: drops every plugin-made USERTABLE (made="1"), at any depth
// (a board holds whole sounds). The user's own imported tables stay. Returns how many it dropped.
inline int stripMadeTables (juce::ValueTree t)
{
    int n = 0;
    for (int i = t.getNumChildren() - 1; i >= 0; --i)
    {
        auto c = t.getChild (i);
        if (c.hasType ("USERTABLE") && (bool) c.getProperty ("made", false)) { t.removeChild (i, nullptr); ++n; }
        else n += stripMadeTables (c);
    }
    return n;
}

inline bool save (const Doc& d, const juce::File& file)
{
    if (! d.valid()) return false;
    auto* o = new juce::DynamicObject();
    o->setProperty ("ghostyWavetable", 1); o->setProperty ("name", d.name); o->setProperty ("frames", (int) d.frames.size()); o->setProperty ("points", TBL);
    o->setProperty ("cycleLen", d.cycleLen); o->setProperty ("format", "float32le");
    juce::Array<juce::var> keys; for (int k : d.keys) keys.add (k); o->setProperty ("keys", keys);
    o->setProperty ("pcm", framesToBase64 (d.frames));
    if (d.made) o->setProperty ("made", true);   // provenance (absent = imported)
    return file.replaceWithText (juce::JSON::toString (juce::var (o)));
}
inline bool load (const juce::File& file, Doc& d)
{
    auto v = juce::JSON::parse (file);
    auto* o = v.getDynamicObject();
    if (o == nullptr || (int) o->getProperty ("ghostyWavetable") != 1) return false;
    const int frames = (int) o->getProperty ("frames"), points = (int) o->getProperty ("points");
    if (frames < 1 || frames > kMaxFrames || points != TBL || o->getProperty ("format").toString() != "float32le") return false;
    Doc out; out.name = o->getProperty ("name").toString(); out.cycleLen = (int) o->getProperty ("cycleLen");
    if (auto* ks = o->getProperty ("keys").getArray()) for (auto& k : *ks) out.keys.push_back ((int) k);
    out.frames = framesFromBase64 (o->getProperty ("pcm").toString(), frames);
    if ((int) out.frames.size() != frames) return false;
    if (out.name.isEmpty()) out.name = file.getFileNameWithoutExtension();
    out.made = (bool) o->getProperty ("made");
    d = std::move (out);
    return true;
}
inline juce::Array<juce::File> listUserTables()
{
    auto files = userBankDir().findChildFiles (juce::File::findFiles, false, "*" + fileExtension());
    struct ByName { static int compareElements (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()); } } cmp;
    files.sort (cmp);
    return files;
}
// a float32 mono WAV of the frames concatenated (the common 2048-point wavetable WAV layout)
inline bool exportWav (const Doc& d, const juce::File& file, double sr = 48000.0)
{
    if (! d.valid()) return false;
    juce::WavAudioFormat wav; file.deleteFile();
    std::unique_ptr<juce::FileOutputStream> os (file.createOutputStream()); if (os == nullptr) return false;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), sr, 1, 32, {}, 0)); if (w == nullptr) return false;   // GATE: via-caller (pro.wt.export-wav in the WAVETABLE EDITOR)
    os.release();
    juce::AudioBuffer<float> b (1, (int) d.frames.size() * TBL);
    for (size_t f = 0; f < d.frames.size(); ++f) b.copyFrom (0, (int) f * TBL, d.frames[f].data(), TBL);
    return w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}
// any audio file: a whole number of 2048-point frames (<= kMaxFrames) is taken as a wavetable as it is; anything else is sliced
// (auto pitch, or cycleLen if given)
inline bool importAudio (const juce::File& file, Doc& d, int cycleLen = 0, int frames = SAMPLE_FRAMES, juce::String* message = nullptr)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (file));
    if (rd == nullptr) { if (message) *message = "Couldn't read that file. Use a WAV, AIFF or FLAC."; return false; }
    const int len = (int) std::min<juce::int64> (rd->lengthInSamples, (juce::int64) (rd->sampleRate * 20.0));
    if (len < 64) { if (message) *message = "That file is too short."; return false; }
    juce::AudioBuffer<float> buf ((int) rd->numChannels, len);
    rd->read (&buf, 0, len, 0, true, true);
    std::vector<float> mono ((size_t) len, 0.0f);
    for (int ch = 0; ch < buf.getNumChannels(); ++ch) for (int i = 0; i < len; ++i) mono[(size_t) i] += buf.getSample (ch, i) / (float) buf.getNumChannels();
    if (cycleLen <= 0 && len % TBL == 0 && len / TBL <= kMaxFrames && len >= TBL)
    {   // a wavetable WAV: frames as they are
        Doc out; out.name = file.getFileNameWithoutExtension(); out.cycleLen = TBL;
        for (int f = 0; f < len / TBL; ++f) out.frames.emplace_back (mono.begin() + (long) f * TBL, mono.begin() + (long) (f + 1) * TBL);
        d = std::move (out);
        if (message) *message = juce::String ((int) d.frames.size()) + " frames (2048-point wavetable file)";
        return true;
    }
    int detected = 0;
    d = fromSample (mono, rd->sampleRate, cycleLen, frames, &detected);
    d.name = file.getFileNameWithoutExtension();
    if (d.frames.empty()) { if (message) *message = "That sample is silent."; return false; }
    if (message) *message = juce::String ((int) d.frames.size()) + " frames, cycle " + juce::String (detected) + " samples" + (cycleLen <= 0 ? " (auto)" : "");
    return true;
}
} // namespace gpro::wt
