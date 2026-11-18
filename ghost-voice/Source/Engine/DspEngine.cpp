// Ghost Voice - AGPL-3.0-only (see LICENSE, NOTICE.md).
#include "DspEngine.h"
#include "../Fusion/VoiceFusion.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <memory>
#include <iostream>

namespace gv
{
juce::String engineName (Engine e)
{
    switch (e) { case Engine::seedvc: return "NEURAL"; case Engine::natural: return "NATURAL"; case Engine::classic: return "CLASSIC"; case Engine::best: return "BEST OF ALL"; }
    return {};
}

juce::String engineBlurb (Engine e)
{
    switch (e)
    {
        case Engine::seedvc:  return "AI re-sing: a new singer sings your performance (the neural engine, on your Mac).";
        case Engine::natural: return "Keeps the performer's own recording, reshaped. Subtle blends. Instant.";
        case Engine::classic: return "Re-synthesises the voice from scratch (vocoder morph). Instant.";
        case Engine::best:    return "NEURAL near voice A and voice B, NATURAL in the middle, crossfaded.";
    }
    return {};
}

juce::String whyNotDsp (const DspJob& j)
{
    if (! j.rights) return "Tick \"I have the rights to these voices\" first.";
    if (j.a.isEmpty()) return "Add voice A (one or more recordings).";
    if (j.b.isEmpty()) return "Add voice B (one or more recordings).";
    if (j.performance.isEmpty()) return "Add the performance to sing.";
    for (auto& f : j.a) if (! juce::File (f).existsAsFile()) return "Voice A's file is missing: " + juce::File (f).getFileName();
    for (auto& f : j.b) if (! juce::File (f).existsAsFile()) return "Voice B's file is missing: " + juce::File (f).getFileName();
    if (! juce::File (j.performance).existsAsFile()) return "The performance file is missing: " + juce::File (j.performance).getFileName();
    if (j.mixes.empty()) return "Nothing to sing.";
    return {};
}

// ---------------------------------------------------------------- the render (Spectrum's GhostVoiceABC, step for step)
namespace
{
constexpr double kMaxSeconds = 180.0;   // fusion::FusionHost::kMaxSeconds

// fusion::FusionHost::decodeFile (ghosty-pro/Shared/VoiceFusionHost.cpp), line for line
bool decodeFile (const juce::File& f, std::vector<float>& mono, int& fs, juce::String& err)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr) { err = "Can't read " + f.getFileName(); return false; }
    if (r->sampleRate < 1000 || r->sampleRate > 768000 || r->lengthInSamples <= 0 || r->numChannels < 1) { err = f.getFileName() + " is empty or has an unusual format"; return false; }
    const int n = (int) std::min<juce::int64> (r->lengthInSamples, (juce::int64) (kMaxSeconds * r->sampleRate)); const int inCh = (int) r->numChannels;
    mono.assign ((size_t) n, 0.0f); constexpr int kBlock = 65536; juce::AudioBuffer<float> b (inCh, kBlock);
    for (int pos = 0; pos < n; pos += kBlock) { const int len = std::min (kBlock, n - pos); r->read (&b, 0, len, pos, true, true);
        for (int c = 0; c < inCh; ++c) { const float* p = b.getReadPointer (c); for (int i = 0; i < len; ++i) mono[(size_t) (pos + i)] += p[i] / (float) inCh; } }
    fs = (int) std::lround (r->sampleRate); err = {}; return true;
}

// fusion::FusionHost::writeWav: 24-bit, mono or stereo as the engine rendered it
bool writeRendered (const juce::File& f, const voice::Rendered& x)
{
    if (x.L.empty() || x.fs <= 0) return false;
    f.getParentDirectory().createDirectory(); f.deleteFile(); std::unique_ptr<juce::FileOutputStream> os (f.createOutputStream()); if (os == nullptr) return false;
    juce::WavAudioFormat wav; const int nch = x.stereo() ? 2 : 1;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), x.fs, (unsigned) nch, 24, {}, 0)); if (w == nullptr) return false;
    os.release(); const float* ch[2] = { x.L.data(), x.stereo() ? x.R.data() : nullptr }; return w->writeFromFloatArrays (ch, nch, (int) x.L.size());
}

voice::AudioRef refOf (const std::vector<float>& x, int fs) { voice::AudioRef r; r.ch.push_back (x.data()); r.n = (int) x.size(); r.fs = fs; return r; }

// one analysed sound: the performance (audio + analysis) or a voice (its takes joined + analysis + identity)
struct Analysed { std::vector<float> x; int fs = 0; voice::Analysis an; fusion::Identity id; };
using AnalysedPtr = std::shared_ptr<const Analysed>;

// the analysis cache: a MOTION ladder or a second preview re-uses the analyses (key: the files, their sizes + times, the rate)
std::mutex cacheLock;
std::vector<std::pair<juce::String, AnalysedPtr>> cache;
juce::String keyOf (const juce::StringArray& files, int fs, bool identity)
{
    juce::String k (fs); k << (identity ? "|id" : "|perf");
    for (auto& f : files) { const juce::File F (f); k << "|" << F.getFullPathName() << ":" << F.getSize() << ":" << F.getLastModificationTime().toMilliseconds(); }
    return k;
}
AnalysedPtr cached (const juce::String& k) { std::lock_guard<std::mutex> g (cacheLock); for (auto& [kk, v] : cache) if (kk == k) return v; return nullptr; }
void keep (const juce::String& k, AnalysedPtr v) { std::lock_guard<std::mutex> g (cacheLock); cache.emplace_back (k, std::move (v)); while (cache.size() > 6) cache.erase (cache.begin()); }

// the performance: decoded at its own working rate
AnalysedPtr performance (const juce::String& file, juce::String& err)
{
    const auto k = keyOf ({ file }, 0, false);
    if (auto c = cached (k)) return c;
    auto a = std::make_shared<Analysed>();
    if (! loadForFusion (juce::File (file), a->x, a->fs, 0, err)) return nullptr;
    voice::AnalysisOptions o;
    if (! voice::analyse (a->x.data(), (int) a->x.size(), a->fs, o, a->an)) { err = "the performance could not be analysed"; return nullptr; }
    keep (k, a); return a;
}

// a voice: every take decoded at the performance's rate, joined with 0.3 s of silence, analysed, its identity extracted
AnalysedPtr voiceOf (const juce::StringArray& files, int fs, juce::String& err)
{
    const auto k = keyOf (files, fs, true);
    if (auto c = cached (k)) return c;
    auto a = std::make_shared<Analysed>(); a->fs = fs;
    for (auto& f : files)
    {
        std::vector<float> x; int r = 0;
        if (! loadForFusion (juce::File (f), x, r, fs, err)) return nullptr;
        if (! a->x.empty()) a->x.insert (a->x.end(), (size_t) (0.3 * fs), 0.0f);
        a->x.insert (a->x.end(), x.begin(), x.end());
    }
    voice::AnalysisOptions o;
    if (! voice::analyse (a->x.data(), (int) a->x.size(), fs, o, a->an)) { err = "a voice could not be analysed"; return nullptr; }
    a->id = fusion::extractIdentity (a->an);
    keep (k, a); return a;
}

fusion::Params paramsFor (const DspJob& j, float mix)
{
    fusion::Params p;                      // the engine's defaults; only what Ghost Voice shows is set
    p.engine = j.engine == Engine::classic ? 0 : 1;
    p.wB = 1.0f - mix; p.wC = mix;         // identity B = voice A, identity C = voice B: the performance's own weight = 0
    p.brightness = j.settings.tone; p.formant = j.settings.timbre; p.grit = j.settings.texture; p.breath = j.settings.breath;
    p.resonance = j.settings.resonance; p.warmth = j.settings.warmth; p.keepConsonants = j.settings.keepConsonants;
    p.semis = (float) j.semitones;
    return p;
}

bool renderCore (const DspJob& j, const std::function<juce::String (float)>& nameFor, std::map<float, juce::File>& out, std::map<float, double>& ms,
                 juce::String& err, std::atomic<float>* progress, std::atomic<bool>* cancel)
{
    auto prog = [&] (float v) { if (progress) progress->store (v); };
    prog (0.02f);
    const auto perf = performance (j.performance, err); if (perf == nullptr) return false;
    prog (0.15f);
    const auto va = voiceOf (j.a, perf->fs, err); if (va == nullptr) return false;
    const auto vb = voiceOf (j.b, perf->fs, err); if (vb == nullptr) return false;
    prog (0.3f);
    for (size_t i = 0; i < j.mixes.size(); ++i)
    {
        if (cancel && cancel->load()) { err = "stopped"; return false; }
        const float mix = j.mixes[i];
        std::atomic<int> pct { 0 }; voice::Progress vp; vp.pct = &pct; vp.cancel = cancel;
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        voice::Rendered y;
        if (! fusion::render (perf->an, refOf (perf->x, perf->fs), &va->id, &vb->id, paramsFor (j, mix), y, vp)) { err = cancel && cancel->load() ? "stopped" : "the engine could not render " + juce::String (juce::roundToInt (mix * 100)) + " % B"; return false; }
        ms[mix] = juce::Time::getMillisecondCounterHiRes() - t0;
        const auto f = j.outDir.getChildFile (nameFor (mix));
        if (! writeRendered (f, y)) { err = "can't write " + f.getFullPathName(); return false; }
        out[mix] = f;
        prog (0.3f + 0.7f * (float) (i + 1) / (float) j.mixes.size());
    }
    return true;
}
} // namespace

bool loadForFusion (const juce::File& f, std::vector<float>& x, int& fs, int wantFs, juce::String& err)
{   // GhostVoiceABC's load(): decode, the working rate (or the rate asked for), the same input level for every file
    if (! decodeFile (f, x, fs, err)) return false;
    const int wrate = wantFs > 0 ? wantFs : voice::workingRate (fs); if (wrate != fs) { x = voice::resample (x, fs, wrate); fs = wrate; }
    float pk = 0; for (float v : x) pk = std::max (pk, std::abs (v)); if (pk > 1e-6f) for (auto& v : x) v *= 0.7f / pk;
    return true;
}

bool renderDsp (const DspJob& j, std::map<float, juce::File>& out, std::map<float, double>& ms, juce::String& err, std::atomic<float>* progress, std::atomic<bool>* cancel)
{
    if (const auto w = whyNotDsp (j); w.isNotEmpty()) { err = w; return false; }
    if (! isDsp (j.engine)) { err = "not a DSP engine"; return false; }
    j.outDir.createDirectory();
    const auto en = engineName (j.engine).toLowerCase();
    return renderCore (j, [en] (float m) { return en + "_" + juce::String (juce::roundToInt (m * 1000)).paddedLeft ('0', 4) + ".wav"; }, out, ms, err, progress, cancel);
}

int renderAbcLayout (const juce::File& dir, const juce::File& source, const juce::StringArray& a, const juce::StringArray& b)
{
    dir.createDirectory();
    juce::DynamicObject::Ptr timings = new juce::DynamicObject();
    {   // 00_source.wav as GhostVoiceABC writes it (decoded, working rate, 0.7 peak)
        voice::Rendered r; juce::String err;
        if (! loadForFusion (source, r.L, r.fs, 0, err)) { std::cout << err << std::endl; return 1; }
        writeRendered (dir.getChildFile ("00_source.wav"), r);
    }
    const std::pair<const char*, float> jobs[] { { "anchor_a", 0.0f }, { "anchor_b", 1.0f }, { "fusion_025", 0.25f }, { "fusion_050", 0.5f }, { "fusion_075", 0.75f } };
    for (Engine e : { Engine::classic, Engine::natural })
    {
        DspJob j; j.a = a; j.b = b; j.performance = source.getFullPathName(); j.engine = e; j.rights = true; j.outDir = dir;
        for (auto& [id, m] : jobs) j.mixes.push_back (m);
        const auto en = engineName (e).toLowerCase();
        std::map<float, juce::File> out; std::map<float, double> ms; juce::String err;
        const bool ok = renderCore (j, [&] (float m) { for (auto& [id, mm] : jobs) if (mm == m) return en + "_" + id + ".wav"; return en + "_x.wav"; }, out, ms, err, nullptr, nullptr);
        if (! ok) { std::cout << "render failed: " << err << std::endl; return 1; }
        for (auto& [id, m] : jobs) { timings->setProperty (en + "_" + id + "_ms", ms[m]); std::cout << "  " << en << "_" << id << ".wav  " << (int) ms[m] << " ms" << std::endl; }
    }
    dir.getChildFile ("timings.json").replaceWithText (juce::JSON::toString (juce::var (timings.get())));
    std::cout << "done" << std::endl;
    return 0;
}

// ---------------------------------------------------------------- runner
DspRunner::DspRunner() : juce::Thread ("Ghost Voice DSP") {}
DspRunner::~DspRunner() { cancel(); }

bool DspRunner::start (const DspJob& job)
{
    if (busy() || whyNotDsp (job).isNotEmpty() || ! isDsp (job.engine)) return false;
    { std::lock_guard<std::mutex> g (m); j = job; res.clear(); msg = engineName (job.engine) + " is singing"; }
    stop = false; prog = 0; secs = 0; st = State::running;
    startThread();
    return true;
}

void DspRunner::cancel() { stop = true; stopThread (10000); }
juce::String DspRunner::message() const { std::lock_guard<std::mutex> g (m); return msg; }
std::map<float, juce::File> DspRunner::results() const { std::lock_guard<std::mutex> g (m); return res; }
DspJob DspRunner::job() const { std::lock_guard<std::mutex> g (m); return j; }

void DspRunner::run()
{
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    DspJob job; { std::lock_guard<std::mutex> g (m); job = j; }
    std::map<float, juce::File> out; std::map<float, double> ms; juce::String err;
    bool ok = false;
    try { ok = renderDsp (job, out, ms, err, &prog, &stop); }
    catch (const std::exception& e) { err = e.what(); }
    secs = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
    { std::lock_guard<std::mutex> g (m); res = out; msg = ok ? engineName (job.engine) + " sang it in " + juce::String (secs.load(), 1) + " s" : err; }
    prog = 1; st = ok ? State::done : State::failed;
    if (onFinished) juce::MessageManager::callAsync ([f = onFinished] { f(); });
}

// ---------------------------------------------------------------- BEST OF ALL
float seedWeight (float b)
{
    const float d = juce::jlimit (0.0f, 0.5f, std::min (b, 1.0f - b));   // distance to the nearer of A / B
    if (d <= 0.10f) return 1.0f;
    if (d >= 0.25f) return 0.0f;
    const float t = (0.25f - d) / 0.15f;                                  // 1 at 0.10, 0 at 0.25
    return t * t * (3.0f - 2.0f * t);
}

std::vector<float> resampleTo (const std::vector<float>& x, int from, int to) { return from == to || from <= 0 || to <= 0 ? x : voice::resample (x, from, to); }

std::vector<float> bestOfAll (const std::vector<float>& seed, const std::vector<float>& nat0, int fs, int natFs, const std::function<float (double)>& mixAt, float* gainUsed)
{
    if (seed.empty()) { if (gainUsed) *gainUsed = 1.0f; return resampleTo (nat0, natFs, fs); }
    const auto nat = resampleTo (nat0, natFs, fs);
    const size_t n = seed.size();
    // one gain: the NATURAL take's RMS brought to the SEED-VC take's (over the length both have)
    double es = 0, en = 0; const size_t both = std::min (n, nat.size());
    for (size_t i = 0; i < both; ++i) { es += (double) seed[i] * seed[i]; en += (double) nat[i] * nat[i]; }
    const float g = en > 1e-12 && es > 1e-12 ? (float) std::sqrt (es / en) : 1.0f;
    if (gainUsed) *gainUsed = g;
    std::vector<float> y (n, 0.0f);
    const int hop = juce::jmax (1, fs / 1000);
    for (size_t i0 = 0; i0 < n; i0 += (size_t) hop)
    {
        const float w0 = seedWeight (mixAt ((double) i0 / fs)), w1 = seedWeight (mixAt ((double) std::min (n, i0 + (size_t) hop) / fs));
        for (size_t i = i0; i < std::min (n, i0 + (size_t) hop); ++i)
        {
            const float w = w0 + (w1 - w0) * (float) (i - i0) / (float) hop;
            const float s = seed[i], v = i < nat.size() ? nat[i] * g : 0.0f;
            y[i] = w >= 1.0f ? s : w <= 0.0f ? v : w * s + (1.0f - w) * v;
        }
    }
    return y;
}
}
