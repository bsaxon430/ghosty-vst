#include "SourceAudio.h"
#include "Analysis.h"
#if defined (__clang__)
 #pragma clang diagnostic push
 #pragma clang diagnostic ignored "-Weverything"
#elif defined (__GNUC__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wall"
 #pragma GCC diagnostic ignored "-Wextra"
 #pragma GCC diagnostic ignored "-Wconversion"
 #pragma GCC diagnostic ignored "-Wsign-conversion"
 #pragma GCC diagnostic ignored "-Wshadow"
#elif defined (_MSC_VER)
 #pragma warning (push, 0)
#endif
#include "CDSPResampler.h"
#if defined (__clang__)
 #pragma clang diagnostic pop
#elif defined (__GNUC__)
 #pragma GCC diagnostic pop
#elif defined (_MSC_VER)
 #pragma warning (pop)
#endif

namespace gslice
{
// ---------------------------------------------------------------- peak pyramid
void PeakPyramid::build (const juce::AudioBuffer<float>& audio)
{
    channels = audio.getNumChannels(); frames = audio.getNumSamples(); levels.clear();
    if (channels == 0 || frames == 0) return;
    // level 0 straight from the audio
    const int64 n0 = (frames + kBase - 1) / kBase;
    levels.emplace_back ((size_t) channels);
    for (int c = 0; c < channels; ++c)
    {
        auto& L = levels[0][(size_t) c]; L.resize ((size_t) n0);
        const float* x = audio.getReadPointer (c);
        for (int64 b = 0; b < n0; ++b)
        {
            const int64 a = b * kBase, e = std::min (frames, a + kBase);
            float mn = x[a], mx = x[a]; double ms = 0;
            for (int64 i = a; i < e; ++i) { mn = std::min (mn, x[i]); mx = std::max (mx, x[i]); ms += (double) x[i] * x[i]; }
            L[(size_t) b] = { mn, mx, (float) (ms / (double) (e - a)) };
        }
    }
    // coarser levels until one bin covers everything
    while (levels.back()[0].size() > 1)
    {
        const auto& prev = levels.back();
        std::vector<std::vector<Bin>> next ((size_t) channels);
        const int lv = (int) levels.size();
        const int64 fpbPrev = framesPerBin (lv - 1);
        for (int c = 0; c < channels; ++c)
        {
            const auto& P = prev[(size_t) c]; auto& N = next[(size_t) c];
            N.resize ((P.size() + kFactor - 1) / kFactor);
            for (size_t b = 0; b < N.size(); ++b)
            {
                Bin o = P[b * kFactor]; double ms = 0; int64 cnt = 0;
                for (size_t k = b * kFactor; k < std::min (P.size(), (b + 1) * kFactor); ++k)
                {
                    o.mn = std::min (o.mn, P[k].mn); o.mx = std::max (o.mx, P[k].mx);
                    const int64 binFrames = std::min (fpbPrev, frames - (int64) k * fpbPrev);
                    ms += (double) P[k].ms * (double) binFrames; cnt += binFrames;
                }
                o.ms = cnt > 0 ? (float) (ms / (double) cnt) : 0.0f;
                N[b] = o;
            }
        }
        levels.push_back (std::move (next));
    }
}

PeakPyramid::Bin PeakPyramid::query (const juce::AudioBuffer<float>& audio, int ch, int64 a, int64 b) const
{
    Bin out; a = std::max<int64> (0, a); b = std::min (frames, b);
    if (b <= a || channels == 0) return out;
    ch = juce::jlimit (0, channels - 1, ch);
    bool any = false; double ms = 0; int64 cnt = 0;
    auto take = [&] (const Bin& x, int64 n) { if (! any) { out.mn = x.mn; out.mx = x.mx; any = true; } else { out.mn = std::min (out.mn, x.mn); out.mx = std::max (out.mx, x.mx); } ms += (double) x.ms * (double) n; cnt += n; };
    // recursive cover: whole bins of the coarsest fitting level, the ragged edges from finer levels, raw samples at the bottom
    std::function<void (int64, int64, int)> cover = [&] (int64 lo, int64 hi, int lv)
    {
        if (hi <= lo) return;
        if (lv < 0)
        {
            const float* x = audio.getReadPointer (ch);
            for (int64 i = lo; i < hi; ++i) take ({ x[i], x[i], x[i] * x[i] }, 1);
            return;
        }
        const int64 f = framesPerBin (lv);
        const int64 b0 = (lo + f - 1) / f, b1 = hi / f;   // whole bins [b0, b1)  (the last bin of the file may be short)
        const int64 lastBinEnd = std::min (frames, b1 * f);
        if (b1 <= b0) { cover (lo, hi, lv - 1); return; }
        cover (lo, b0 * f, lv - 1);
        const auto& L = levels[(size_t) lv][(size_t) ch];
        for (int64 k = b0; k < b1 && k < (int64) L.size(); ++k) take (L[(size_t) k], std::min (f, frames - k * f));
        cover (lastBinEnd, hi, lv - 1);
    };
    int lv = numLevels() - 1;
    while (lv > 0 && framesPerBin (lv) > (b - a) / 2) --lv;
    cover (a, b, lv);
    out.ms = cnt > 0 ? (float) (ms / (double) cnt) : 0.0f;
    return out;
}

juce::String PeakPyramid::digest() const
{
    Hasher h; h.add (channels).add (frames).add (numLevels());
    for (auto& lv : levels) for (auto& ch : lv) for (auto& b : ch) h.add (b.mn).add (b.mx).add (b.ms);
    return h.hex();
}

juce::String SourceAudio::describe() const
{
    return juce::String (sampleRate / 1000.0, 1) + " kHz  |  " + juce::String (channels) + (channels == 1 ? " ch (mono)" : " ch")
         + "  |  " + juce::String (seconds(), 3) + " s  |  " + juce::String (frames) + " frames" + (formatName.isNotEmpty() ? "  |  " + formatName : juce::String());
}

namespace source
{
juce::AudioFormatManager& formats()
{
    static juce::AudioFormatManager* fm = [] { auto* m = new juce::AudioFormatManager(); m->registerBasicFormats(); return m; }();
    return *fm;
}
juce::String wildcard() { return "*.wav;*.wave;*.aif;*.aiff;*.flac;*.mp3"; }
bool canLoad (const juce::File& f)
{
    const auto e = f.getFileExtension().toLowerCase();
    return e == ".wav" || e == ".wave" || e == ".aif" || e == ".aiff" || e == ".flac" || e == ".mp3";
}
juce::String fingerprintFile (const juce::File& f)
{
    if (! f.existsAsFile()) return {};
    return juce::SHA256 (f).toHexString();
}

static void finish (SourceAudio& s, const juce::File& cacheDir = {})
{
    s.frames = s.audio.getNumSamples(); s.channels = s.audio.getNumChannels();
    s.peaks.build (s.audio);   // (fast: a 10 s vocal takes a few ms; no cache file)
    detectOnsets (s.audio, s.sampleRate, s.onsets, s.onsetStrength, s.fingerprint, cacheDir);
}

std::shared_ptr<const SourceAudio> load (const juce::File& f, juce::String& error, const juce::File& cacheDir)
{
    if (! f.existsAsFile()) { error = "File not found: " + f.getFileName(); return nullptr; }
    if (! canLoad (f)) { error = "Not an audio file Ghost Slice reads (WAV / AIFF / FLAC / MP3): " + f.getFileName(); return nullptr; }
    std::unique_ptr<juce::AudioFormatReader> r (formats().createReaderFor (f));
    if (r == nullptr) { error = "Couldn't decode " + f.getFileName(); return nullptr; }
    if (r->lengthInSamples <= 0 || r->numChannels == 0) { error = f.getFileName() + " has no audio"; return nullptr; }
    if (r->lengthInSamples > (juce::int64) r->sampleRate * 60 * 60) { error = f.getFileName() + " is longer than one hour"; return nullptr; }
    auto s = std::make_shared<SourceAudio>();
    s->file = f; s->sampleRate = r->sampleRate; s->formatName = r->getFormatName();
    const int ch = (int) juce::jmin<unsigned int> (2, r->numChannels);   // stereo editor: more channels fold to the first two
    s->audio.setSize (ch, (int) r->lengthInSamples);
    r->read (&s->audio, 0, (int) r->lengthInSamples, 0, true, ch > 1);
    for (int c = 0; c < ch; ++c)   // NaN / inf from a broken file never reach the engine
    {
        auto* x = s->audio.getWritePointer (c);
        for (int i = 0; i < s->audio.getNumSamples(); ++i) if (! std::isfinite (x[i])) x[i] = 0.0f;
    }
    s->fingerprint = fingerprintFile (f);
    finish (*s, cacheDir);
    readClipHint (f, s->seconds(), s->clip);
    return s;
}

bool readClipHint (const juce::File& audio, double clipSeconds, ClipHint& out, juce::String* why)
{
    out = {};
    juce::File side = audio.getParentDirectory().getChildFile (audio.getFileName() + ".ghostclip.json");
    if (! side.existsAsFile()) side = audio.getSiblingFile (audio.getFileNameWithoutExtension() + ".ghostclip.json");
    if (! side.existsAsFile()) return false;
    if (side.getSize() > 64 * 1024) { if (why) *why = "the .ghostclip.json is too big"; return false; }
    const auto v = juce::JSON::parse (side.loadFileAsString());
    if (! v.isObject() || v["format"].toString() != "ghostclip") { if (why) *why = "not a ghostclip sidecar"; return false; }
    if ((int) v.getProperty ("version", 1) > 1) { if (why) *why = "a newer ghostclip version"; return false; }
    auto num = [&] (const char* k, double def) { const auto x = v[k]; if (x.isVoid() || x.isUndefined()) return def; const double d = x.isString() ? x.toString().getDoubleValue() : (double) x; return std::isfinite (d) ? d : def; };
    ClipHint h; h.file = side;
    h.bpm = num ("bpm", 0.0); h.bars = (int) num ("bars", 0.0); h.beatsPerBar = juce::jlimit (1, 16, (int) num ("beatsPerBar", 4.0));
    h.offsetSeconds = juce::jlimit (0.0, std::max (0.0, clipSeconds), num ("offset", 0.0)); h.key = v["key"].toString().trim();
    if (h.bpm <= 0 && h.bars > 0 && clipSeconds > 0) h.bpm = h.bars * h.beatsPerBar * 60.0 / (clipSeconds - h.offsetSeconds);   // bars alone
    if (! h.valid()) { if (why) *why = "no usable bpm / bars"; return false; }
    if (h.bars < 0 || h.bars > 4096) h.bars = 0;
    out = h; return true;
}

std::shared_ptr<const SourceAudio> fromBuffer (juce::AudioBuffer<float> audio, double sampleRate, const juce::String& name)
{
    auto s = std::make_shared<SourceAudio>();
    s->audio = std::move (audio); s->sampleRate = sampleRate; s->formatName = "memory"; s->file = juce::File::getCurrentWorkingDirectory().getChildFile (name);
    juce::MemoryOutputStream m;
    for (int c = 0; c < s->audio.getNumChannels(); ++c) m.write (s->audio.getReadPointer (c), sizeof (float) * (size_t) s->audio.getNumSamples());
    m.writeDouble (sampleRate);
    s->fingerprint = juce::SHA256 (m.getData(), m.getDataSize()).toHexString();
    finish (*s);
    return s;
}

// r8brain-free-src (MIT, version-6.5, third_party/r8brain-free-src): CDSPResampler24 = 180 dB stop band, 2 % transition band,
// linear phase; oneshot() removes the filter latency, so output frame k sits at input time k / ratio exactly. Per channel, double.
void resampleChannels (const juce::AudioBuffer<float>& in, int frames, double fromRate, double toRate, juce::AudioBuffer<float>& out)
{
    const int n = out.getNumSamples();
    if (frames <= 0 || n <= 0) { out.clear(); return; }
    constexpr int kBlock = 1 << 14;
    std::vector<double> ip ((size_t) frames), op ((size_t) n);
    for (int c = 0; c < juce::jmin (in.getNumChannels(), out.getNumChannels()); ++c)
    {
        const float* x = in.getReadPointer (c);
        for (int i = 0; i < frames; ++i) ip[(size_t) i] = (double) x[i];
        r8b::CDSPResampler24 rs (fromRate, toRate, kBlock);
        rs.oneshot (ip.data(), frames, op.data(), n);
        float* y = out.getWritePointer (c);
        for (int i = 0; i < n; ++i) y[i] = (float) op[(size_t) i];
    }
}

std::shared_ptr<const WorkingAudio> makeWorking (std::shared_ptr<const SourceAudio> src, double hostRate)
{
    auto w = std::make_shared<WorkingAudio>();
    w->src = src; w->rate = hostRate;
    if (src == nullptr) return w;
    if (std::abs (hostRate - src->sampleRate) < 1.0e-6) { w->ratio = 1.0; return w; }
    w->ratio = hostRate / src->sampleRate;
    const int n = (int) std::llround ((double) src->frames * w->ratio);
    w->resampled.setSize (src->channels, n);
    resampleChannels (src->audio, (int) src->frames, src->sampleRate, hostRate, w->resampled);
    return w;
}

void detectOnsets (const juce::AudioBuffer<float>& audio, double sr, std::vector<int64>& pos, std::vector<float>& strength,
                   const juce::String& fingerprint, const juce::File& cacheDir)
{
    pos.clear(); strength.clear();
    if (audio.getNumSamples() == 0 || audio.getNumChannels() == 0) return;
    const auto x = analysis::mono (audio);
    const int64 n = (int64) x.size();
    const bool useCache = cacheDir != juce::File() && fingerprint.isNotEmpty();
    const auto dir = useCache ? analysis::cache::dirFor (cacheDir, fingerprint) : juce::File();
    analysis::OnsetSet o;
    if (! useCache || ! analysis::cache::readOnsets (dir, analysis::Material::Auto, n, o))
    {
        o = analysis::detectOnsets (x.data(), n, sr, analysis::Material::Auto);
        if (useCache) analysis::cache::writeOnsets (dir, o, n);
    }
    for (auto& e : o.events) { pos.push_back (e.pos); strength.push_back (e.strength); }
}

juce::File findByFingerprint (const juce::String& fingerprint, const juce::String& fileName, const juce::Array<juce::File>& near)
{
    if (fingerprint.isEmpty()) return {};
    juce::Array<juce::File> dirs;
    for (auto& d : near) { if (d.isDirectory()) dirs.addIfNotAlreadyThere (d); if (d.getParentDirectory().isDirectory()) dirs.addIfNotAlreadyThere (d.getParentDirectory()); }
    int checked = 0;
    // pass 1: the same file name; pass 2: any audio file (at most 400 hashed)
    for (int pass = 0; pass < 2; ++pass)
        for (auto& d : dirs)
            for (const auto& e : juce::RangedDirectoryIterator (d, true, pass == 0 && fileName.isNotEmpty() ? fileName : wildcard(), juce::File::findFiles))
            {
                const auto f = e.getFile();
                if (f.getRelativePathFrom (d).retainCharacters ("/\\").length() > 2) continue;   // 2 levels deep
                if (! canLoad (f) || ++checked > 400) continue;
                if (fingerprintFile (f) == fingerprint) return f;
            }
    return {};
}
} // namespace source
} // namespace gslice
