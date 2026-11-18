// Ghost Voice - AGPL-3.0-only (see LICENSE, NOTICE.md).
#include "VoiceEngine.h"
#include <algorithm>
#include <cmath>

namespace gv
{
juce::File workDir()
{
    const auto e = juce::SystemStats::getEnvironmentVariable ("GHOSTVOICE_HOME", {});
    if (e.isNotEmpty()) return juce::File (e);
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
#if JUCE_MAC
        .getChildFile ("Application Support")
#endif
        .getChildFile ("GhostBlock").getChildFile ("Ghost Voice");
}

juce::String uiWords (juce::String t)
{
    static const char* const swaps[][2] {
        { "Seed-VC's", "the neural engine's" }, { "SEED-VC's", "NEURAL's" }, { "Seed-VC", "the neural engine" }, { "SEED-VC", "NEURAL" }, { "seed-vc", "neural engine" },
        { "SeedVC", "neural engine" }, { "CAMPPlus", "the voice-print meter" }, { "campplus", "voice-print meter" }, { "BigVGAN", "the vocoder" }, { "bigvgan", "vocoder" },
        { "WhisperX", "the word finder" }, { "whisperx", "word finder" }, { "slicer2", "phrase finder" }, { "OpenVPI", "" }, { "Whisper", "the speech model" }, { "whisper", "speech model" }, { "RMVPE", "the pitch tracker" }, { "rmvpe", "pitch tracker" }, { "Plachtaa/", "" }, { "Plachtaa", "" } };
    for (auto& s : swaps) t = t.replace (s[0], s[1]);
    return t;
}

juce::File engineDir()
{
    const auto e = juce::SystemStats::getEnvironmentVariable ("GHOSTVOICE_ENGINE", {});
    return e.isNotEmpty() ? juce::File (e) : workDir().getChildFile ("engine");
}

juce::File helperDir()
{
    const auto e = juce::SystemStats::getEnvironmentVariable ("GHOSTVOICE_HELPER_DIR", {});
    if (e.isNotEmpty()) return juce::File (e);
    // inside a bundle: <x>.app|vst3|component/Contents/MacOS/<binary> -> Contents/Resources/helper
    auto f = juce::File::getSpecialLocation (juce::File::currentExecutableFile);
    for (int i = 0; i < 4 && f.exists(); ++i)
    {
        f = f.getParentDirectory();
        const auto h = f.getChildFile ("Resources").getChildFile ("helper");
        if (h.getChildFile ("ghostvoice_engine.py").existsAsFile()) return h;
    }
#ifdef GHOSTVOICE_DEV_HELPER_DIR
    return juce::File (GHOSTVOICE_DEV_HELPER_DIR);
#else
    return {};
#endif
}

juce::File enginePython() { return engineDir().getChildFile ("venv").getChildFile ("bin").getChildFile ("python"); }

bool engineInstalled()
{
    const auto inf = engineDir().getChildFile ("seed-vc").getChildFile ("inference.py");
    return enginePython().existsAsFile() && inf.existsAsFile() && inf.loadFileAsString().contains ("GHOSTVOICE_SEEDVC_FUSION_PATCH")
        && engineDir().getChildFile ("seed-vc").getChildFile ("checkpoints").isDirectory();
}

juce::String engineSummary()
{
    if (engineInstalled()) return "VOICE ENGINE READY";
    if (! helperDir().getChildFile ("install-engine.sh").existsAsFile()) return "VOICE ENGINE MISSING";
    return "VOICE ENGINE NOT INSTALLED";
}

// ---------------------------------------------------------------- job
juce::String jobToJson (const Job& j)
{
    auto* o = new juce::DynamicObject();
    juce::var keep (o);
    auto arr = [] (const juce::StringArray& s) { juce::Array<juce::var> v; for (auto& x : s) v.add (x); return juce::var (v); };
    o->setProperty ("source", j.performance);
    o->setProperty ("a", arr (j.a));
    o->setProperty ("b", arr (j.b));
    juce::Array<juce::var> mx; for (float m : j.mixes) mx.add ((double) m);
    o->setProperty ("mixes", mx);
    o->setProperty ("rights", j.rights);
    o->setProperty ("out_dir", j.outDir.getFullPathName());
    o->setProperty ("steps", j.settings.steps);
    o->setProperty ("cfg", (double) j.settings.follow);
    o->setProperty ("semitones", j.settings.semitones);
    o->setProperty ("auto_f0", j.settings.autoPitch);
    o->setProperty ("prompt", j.settings.detail ? "nearest" : "none");
    o->setProperty ("seed", j.settings.seed);
    o->setProperty ("device", j.settings.cpuOnly ? "cpu" : "auto");
    o->setProperty ("novelty", (double) j.settings.novelty);
    o->setProperty ("variation", j.settings.variation);
    {   // UNDER THE HOOD: Seed-VC's other options; a key exists only when it is not build 3's value, so an untouched job is unchanged
        const EngineSettings d;
        if (j.settings.lengthAdjust != d.lengthAdjust) o->setProperty ("length_adjust", std::round ((double) j.settings.lengthAdjust * 1000.0) / 1000.0);
        if (j.settings.f0Condition != d.f0Condition) o->setProperty ("f0_condition", j.settings.f0Condition);
        if (j.settings.fp16 >= 0) o->setProperty ("fp16", j.settings.fp16 == 1);
        if (j.settings.checkpoint.isNotEmpty()) o->setProperty ("checkpoint", j.settings.checkpoint);
        if (j.settings.config.isNotEmpty()) o->setProperty ("config", j.settings.config);
    }
    if (! j.measure.empty())
    {
        juce::Array<juce::var> ms;
        for (auto& [mx, f] : j.measure) { juce::Array<juce::var> pair; pair.add ((double) mx); pair.add (f); ms.add (juce::var (pair)); }
        o->setProperty ("measure", ms);
    }
    if (j.fakeDelay > 0) o->setProperty ("fake_delay", j.fakeDelay);
    return juce::JSON::toString (keep, false);
}

juce::String whyNot (const Job& j)
{
    if (! j.rights) return "Tick \"I have the rights to these voices\" first.";
    if (j.a.isEmpty()) return "Add voice A.";
    if (j.b.isEmpty()) return "Add voice B.";
    if (j.performance.isEmpty()) return "Add a performance to re-sing.";
    for (auto& f : j.a) if (! juce::File (f).existsAsFile()) return "A voice A take is missing: " + juce::File (f).getFileName();
    for (auto& f : j.b) if (! juce::File (f).existsAsFile()) return "A voice B take is missing: " + juce::File (f).getFileName();
    if (! juce::File (j.performance).existsAsFile()) return "The performance file is missing.";
    if (j.mixes.empty()) return "Nothing to sing.";
    return {};
}

// ---------------------------------------------------------------- lines
Line parseLine (const juce::String& s0)
{
    const auto s = s0.trim();
    Line l; l.text = s;
    auto rest = [&] (const char* tag) { return s.fromFirstOccurrenceOf (tag, false, false).trim(); };
    if (s.startsWith ("GV_PROGRESS ")) { auto r = rest ("GV_PROGRESS"); l.kind = Line::progress; l.value = juce::jlimit (0.0f, 1.0f, r.upToFirstOccurrenceOf (" ", false, false).getFloatValue()); l.text = r.fromFirstOccurrenceOf (" ", false, false); }
    else if (s.startsWith ("GV_RESULT ")) { auto r = rest ("GV_RESULT"); l.kind = Line::result; l.value = r.upToFirstOccurrenceOf (" ", false, false).getFloatValue(); l.text = r.fromFirstOccurrenceOf (" ", false, false); }
    else if (s.startsWith ("GV_TIME ")) { auto r = rest ("GV_TIME"); l.kind = Line::time; l.value = r.upToFirstOccurrenceOf (" ", false, false).getFloatValue(); l.seconds = r.fromFirstOccurrenceOf (" ", false, false).getDoubleValue(); }
    else if (s.startsWith ("GV_IDENTITY ")) { auto r = rest ("GV_IDENTITY"); l.kind = Line::identity; l.value = r.upToFirstOccurrenceOf (" ", false, false).getFloatValue(); l.text = r.fromFirstOccurrenceOf (" ", false, false); }
    else if (s.startsWith ("GV_ANALYSIS ")) { l.kind = Line::analysis; l.text = rest ("GV_ANALYSIS"); }
    else if (s.startsWith ("GV_ERROR")) { l.kind = Line::error; l.text = rest ("GV_ERROR"); }
    else if (s == "GV_DONE") l.kind = Line::done;
    else if (s.startsWith ("GV_STEP ")) { auto r = rest ("GV_STEP"); l.kind = Line::step; const auto frac = r.upToFirstOccurrenceOf (" ", false, false);
        const int a = frac.upToFirstOccurrenceOf ("/", false, false).getIntValue(), b = juce::jmax (1, frac.fromFirstOccurrenceOf ("/", false, false).getIntValue());
        l.value = juce::jlimit (0.0f, 1.0f, (float) (a - 1) / (float) b); l.text = r.fromFirstOccurrenceOf (" ", false, false); }
    else if (s == "GV_INSTALLED") l.kind = Line::installed;
    else if (s.startsWith ("GV_FAILED")) { l.kind = Line::failed; l.text = rest ("GV_FAILED"); }
    return l;
}

Identity Identity::fromJson (const juce::String& s)
{
    Identity i; const auto v = juce::JSON::parse (s);
    if (! v.isObject() || ! v.hasProperty ("cos_a")) return i;
    i.cosA = (float) (double) v["cos_a"]; i.cosB = (float) (double) v["cos_b"]; i.cosPerf = (float) (double) v["cos_perf"]; i.cosAB = (float) (double) v["cos_a_b"];
    i.valid = std::isfinite (i.cosA) && std::isfinite (i.cosB);
    return i;
}

// ---------------------------------------------------------------- runner
Runner::Runner() : juce::Thread ("Ghost Voice engine")
{
    renderCommand = [] (const juce::File& job) { return juce::StringArray { enginePython().getFullPathName(), helperDir().getChildFile ("ghostvoice_engine.py").getFullPathName(), "render", job.getFullPathName() }; };
    installCommand = [] { return juce::StringArray { "/bin/bash", helperDir().getChildFile ("install-engine.sh").getFullPathName(), helperDir().getChildFile ("lab").getFullPathName() }; };
    analyzeCommand = [] (const juce::File& job) { return juce::StringArray { enginePython().getFullPathName(), helperDir().getChildFile ("ghostvoice_engine.py").getFullPathName(), "analyze", job.getFullPathName() }; };
    measureCommand = [] (const juce::File& job) { return juce::StringArray { enginePython().getFullPathName(), helperDir().getChildFile ("ghostvoice_engine.py").getFullPathName(), "measure", job.getFullPathName() }; };
}
Runner::~Runner() { cancel(); }

bool Runner::startRender (const Job& j)
{
    if (busy() || whyNot (j).isNotEmpty()) return false;
    j.outDir.createDirectory();
    const auto jf = j.outDir.getChildFile ("job.json");
    if (! jf.replaceWithText (jobToJson (j))) return false;
    { std::lock_guard<std::mutex> g (m); res.clear(); secs.clear(); ids.clear(); msg = "starting the voice engine"; fullLog.clear(); }
    cmd = renderCommand (jf); kind = 0; prog = 0; st = State::running;
    startThread();
    return true;
}

bool Runner::startAnalyze (const Job& j)
{
    if (busy() || j.a.isEmpty() || j.b.isEmpty()) return false;
    j.outDir.createDirectory();
    const auto jf = j.outDir.getChildFile ("analyze.json");
    if (! jf.replaceWithText (jobToJson (j))) return false;
    { std::lock_guard<std::mutex> g (m); ana = juce::var(); msg = "listening to the voices"; fullLog.clear(); }
    cmd = analyzeCommand (jf); kind = 2; prog = 0; st = State::running;
    startThread();
    return true;
}

bool Runner::startMeasure (const Job& j)
{
    if (busy() || j.a.isEmpty() || j.b.isEmpty() || j.measure.empty()) return false;
    j.outDir.createDirectory();
    const auto jf = j.outDir.getChildFile ("measure.json");
    if (! jf.replaceWithText (jobToJson (j))) return false;
    { std::lock_guard<std::mutex> g (m); ids.clear(); msg = "measuring the voice prints"; fullLog.clear(); }
    cmd = measureCommand (jf); kind = 3; prog = 0; st = State::running;
    startThread();
    return true;
}

bool Runner::startInstall()
{
    if (busy()) return false;
    { std::lock_guard<std::mutex> g (m); msg = "installing the voice engine"; fullLog.clear(); }
    cmd = installCommand(); kind = 1; prog = 0; st = State::running;
    startThread();
    return true;
}

void Runner::cancel() { signalThreadShouldExit(); stopThread (4000); }

juce::String Runner::message() const { std::lock_guard<std::mutex> g (m); return msg; }
std::map<float, juce::File> Runner::results() const { std::lock_guard<std::mutex> g (m); return res; }
std::map<float, double> Runner::seconds() const { std::lock_guard<std::mutex> g (m); return secs; }
std::map<float, Identity> Runner::identities() const { std::lock_guard<std::mutex> g (m); return ids; }
juce::var Runner::analysis() const { std::lock_guard<std::mutex> g (m); return ana; }
juce::String Runner::log() const { std::lock_guard<std::mutex> g (m); return fullLog; }
double Runner::elapsed() const { return (t1 > t0 ? t1 : juce::Time::getMillisecondCounterHiRes() * 0.001) - t0; }

void Runner::run()
{
    t0 = juce::Time::getMillisecondCounterHiRes() * 0.001; t1 = 0;
    juce::ChildProcess p;
    bool ok = false, finished = false;
    if (! p.start (cmd, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
    {
        std::lock_guard<std::mutex> g (m); msg = "could not start " + cmd[0];
    }
    else
    {
        juce::String pending;
        char buf[4096];
        auto handle = [&] (const juce::String& raw)
        {
            const auto l = parseLine (raw);
            std::lock_guard<std::mutex> g (m);
            fullLog << raw << "\n";
            switch (l.kind)
            {
                case Line::progress: case Line::step: prog = l.value; msg = l.text; break;
                case Line::result: res[l.value] = juce::File (l.text); break;
                case Line::time: secs[l.value] = l.seconds; break;
                case Line::identity: ids[l.value] = Identity::fromJson (l.text); break;
                case Line::analysis: ana = juce::JSON::parse (l.text); break;
                case Line::error: case Line::failed: msg = l.text; finished = true; break;
                case Line::done: case Line::installed: ok = true; finished = true; prog = 1; break;
                default: break;
            }
        };
        while (! threadShouldExit())
        {
            const int n = p.readProcessOutput (buf, sizeof (buf));
            if (n > 0)
            {
                pending += juce::String::fromUTF8 (buf, n);
                while (pending.containsChar ('\n'))
                {
                    handle (pending.upToFirstOccurrenceOf ("\n", false, false));
                    pending = pending.fromFirstOccurrenceOf ("\n", false, false);
                }
                continue;
            }
            if (! p.isRunning()) break;
            wait (20);
        }
        if (pending.isNotEmpty()) handle (pending);
        if (threadShouldExit() && p.isRunning()) { p.kill(); std::lock_guard<std::mutex> g (m); msg = "stopped"; ok = false; }
        if (! finished && ! ok) { std::lock_guard<std::mutex> g (m); if (msg.isEmpty() || msg.startsWith ("starting")) msg = "the engine stopped (code " + juce::String ((int) p.getExitCode()) + ")"; }
    }
    t1 = juce::Time::getMillisecondCounterHiRes() * 0.001;
    if (ok && kind == 0) { std::lock_guard<std::mutex> g (m); ok = ! res.empty(); if (! ok) msg = "the engine wrote no audio"; }
    st = ok ? State::done : State::failed;
    if (onFinished) juce::MessageManager::callAsync ([f = onFinished] { f(); });
}

// ---------------------------------------------------------------- MOTION
std::vector<float> ladderFor (float lo, float hi)
{
    lo = juce::jlimit (0.0f, 1.0f, lo); hi = juce::jlimit (0.0f, 1.0f, hi);
    if (hi < lo) std::swap (lo, hi);
    if (hi - lo < 1.0e-4f) return { std::round (lo * 1000.0f) / 1000.0f };
    std::vector<float> v;
    const int a = (int) std::floor (lo * 4.0f + 1.0e-4f), b = (int) std::ceil (hi * 4.0f - 1.0e-4f);
    for (int k = a; k <= b; ++k) v.push_back ((float) k / 4.0f);
    return v;
}

std::vector<float> renderMotion (const std::vector<float>& mixes, const std::vector<std::vector<float>>& renders, int fs,
                                 const std::function<float (double)>& mixAt)
{
    if (mixes.empty() || renders.size() != mixes.size() || fs <= 0) return {};
    size_t n = renders[0].size();
    for (auto& r : renders) n = std::min (n, r.size());
    std::vector<float> y (n, 0.0f);
    if (mixes.size() == 1) { std::copy (renders[0].begin(), renders[0].begin() + (long) n, y.begin()); return y; }
    const int hop = juce::jmax (1, fs / 1000);   // the curve is read every millisecond and interpolated between
    for (size_t i0 = 0; i0 < n; i0 += (size_t) hop)
    {
        const float m0 = juce::jlimit (mixes.front(), mixes.back(), mixAt ((double) i0 / fs));
        const float m1 = juce::jlimit (mixes.front(), mixes.back(), mixAt ((double) std::min (n, i0 + (size_t) hop) / fs));
        for (size_t i = i0; i < std::min (n, i0 + (size_t) hop); ++i)
        {
            const float mm = m0 + (m1 - m0) * (float) (i - i0) / (float) hop;
            size_t k = 0;
            while (k + 2 < mixes.size() && mm > mixes[k + 1]) ++k;
            const float span = mixes[k + 1] - mixes[k];
            const float t = span > 0 ? juce::jlimit (0.0f, 1.0f, (mm - mixes[k]) / span) : 0.0f;
            y[i] = renders[k][i] * (1.0f - t) + renders[k + 1][i] * t;
        }
    }
    return y;
}

// ---------------------------------------------------------------- audio files
bool readMono (const juce::File& f, std::vector<float>& out, int& fs, juce::String& err)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr) { err = "can't read " + f.getFileName(); return false; }
    const auto len = (int) juce::jmin<juce::int64> (r->lengthInSamples, (juce::int64) r->sampleRate * 60 * 20);
    juce::AudioBuffer<float> b ((int) juce::jmax (1u, r->numChannels), len);
    r->read (&b, 0, len, 0, true, true);
    out.assign ((size_t) len, 0.0f);
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < len; ++i) out[(size_t) i] += b.getSample (c, i) / (float) b.getNumChannels();
    fs = (int) r->sampleRate;
    return len > 0;
}

bool writeWav (const juce::File& f, const std::vector<float>& x, int fs, int bits)
{
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    std::unique_ptr<juce::FileOutputStream> os (f.createOutputStream());
    if (os == nullptr) return false;
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), fs, 1, bits, {}, 0));
    if (w == nullptr) return false;
    os.release();
    const float* ch[] = { x.data() };
    return w->writeFromFloatArrays (ch, 1, (int) x.size());
}
}
