// Ghost Voice - AGPL-3.0-only (see LICENSE, NOTICE.md).
#include "Processor.h"
#include "Editor.h"
#include "TerrainSerial.h"

namespace gv
{
juce::String slotName (int s) { return s == slotA ? "VOICE A" : s == slotB ? "VOICE B" : "PERFORMANCE"; }

namespace
{
juce::ValueTree settingsTree (const EngineSettings& s)
{
    juce::ValueTree t ("ENGINE");
    t.setProperty ("steps", s.steps, nullptr); t.setProperty ("follow", s.follow, nullptr); t.setProperty ("semis", s.semitones, nullptr);
    t.setProperty ("detail", s.detail, nullptr); t.setProperty ("autoPitch", s.autoPitch, nullptr); t.setProperty ("cpu", s.cpuOnly, nullptr);
    t.setProperty ("seed", s.seed, nullptr); t.setProperty ("novelty", s.novelty, nullptr); t.setProperty ("variation", s.variation, nullptr);
    if (s.lengthAdjust != EngineSettings().lengthAdjust) t.setProperty ("lengthAdjust", s.lengthAdjust, nullptr);   // UNDER THE HOOD: only when changed
    if (! s.f0Condition) t.setProperty ("f0", false, nullptr);
    if (s.fp16 >= 0) t.setProperty ("fp16", s.fp16, nullptr);
    if (s.checkpoint.isNotEmpty()) t.setProperty ("ckpt", s.checkpoint, nullptr);
    if (s.config.isNotEmpty()) t.setProperty ("cfg", s.config, nullptr);
    return t;
}
EngineSettings settingsFrom (const juce::ValueTree& t)
{
    EngineSettings d, s;
    s.steps = t.getProperty ("steps", d.steps); s.follow = t.getProperty ("follow", d.follow); s.semitones = t.getProperty ("semis", d.semitones);
    s.detail = t.getProperty ("detail", d.detail); s.autoPitch = t.getProperty ("autoPitch", d.autoPitch); s.cpuOnly = t.getProperty ("cpu", d.cpuOnly);
    s.seed = t.getProperty ("seed", d.seed); s.novelty = t.getProperty ("novelty", d.novelty); s.variation = t.getProperty ("variation", d.variation);
    s.lengthAdjust = t.getProperty ("lengthAdjust", d.lengthAdjust); s.f0Condition = t.getProperty ("f0", d.f0Condition); s.fp16 = t.getProperty ("fp16", d.fp16);
    s.checkpoint = t.getProperty ("ckpt", d.checkpoint).toString(); s.config = t.getProperty ("cfg", d.config).toString();
    return s;
}
juce::ValueTree dspTree (const DspSettings& d)
{
    juce::ValueTree t ("DSP");
    t.setProperty ("tone", d.tone, nullptr); t.setProperty ("timbre", d.timbre, nullptr); t.setProperty ("texture", d.texture, nullptr);
    t.setProperty ("breath", d.breath, nullptr); t.setProperty ("resonance", d.resonance, nullptr); t.setProperty ("warmth", d.warmth, nullptr);
    t.setProperty ("cons", d.keepConsonants, nullptr);
    return t;
}
DspSettings dspFrom (const juce::ValueTree& t)
{
    DspSettings d, s;
    s.tone = t.getProperty ("tone", d.tone); s.timbre = t.getProperty ("timbre", d.timbre); s.texture = t.getProperty ("texture", d.texture);
    s.breath = t.getProperty ("breath", d.breath); s.resonance = t.getProperty ("resonance", d.resonance); s.warmth = t.getProperty ("warmth", d.warmth);
    s.keepConsonants = t.getProperty ("cons", d.keepConsonants);
    return s;
}
juce::String mixWord (float m) { return juce::String (juce::roundToInt (m * 100)) + " % B"; }
float roundMix (float m) { return std::round (juce::jlimit (0.0f, 1.0f, m) * 1000.0f) / 1000.0f; }
juce::ValueTree identityTree (const Identity& i)
{
    juce::ValueTree t ("IDENTITY");
    t.setProperty ("valid", i.valid, nullptr); t.setProperty ("a", i.cosA, nullptr); t.setProperty ("b", i.cosB, nullptr);
    t.setProperty ("perf", i.cosPerf, nullptr); t.setProperty ("ab", i.cosAB, nullptr);
    return t;
}
Identity identityFrom (const juce::ValueTree& t)
{
    Identity i; i.valid = t.getProperty ("valid", false); i.cosA = t.getProperty ("a", 0.0f); i.cosB = t.getProperty ("b", 0.0f);
    i.cosPerf = t.getProperty ("perf", 0.0f); i.cosAB = t.getProperty ("ab", 0.0f);
    return i;
}
juce::String safeName (juce::String s)
{
    s = juce::File::createLegalFileName (s.trim()).substring (0, 40);
    return s.isEmpty() ? "Voice C" : s;
}
}

bool Recipe::same (const Recipe& o) const
{
    if (engine != o.engine || std::abs (balance - o.balance) >= 0.005f) return false;
    if (usesSeed() && ! (seed == o.seed)) return false;
    if (usesDsp() && (! (dsp == o.dsp) || seed.semitones != o.seed.semitones)) return false;
    return true;
}

bool SavedVoice::load (const juce::File& dir, SavedVoice& v)
{
    const auto x = juce::parseXML (dir.getChildFile ("voice.xml"));
    if (x == nullptr) return false;
    const auto t = juce::ValueTree::fromXml (*x);
    if (! t.hasType ("GHOSTVOICE_C")) return false;
    v.dir = dir; v.name = t.getProperty ("name").toString();
    v.created = juce::Time ((juce::int64) t.getProperty ("created", 0));
    v.balance = t.getProperty ("balance", 0.5f);
    v.engine = engineFromInt (t.getProperty ("engine", (int) Engine::seedvc));
    for (auto c : t)
    {
        if (c.hasType ("DSP")) (c.getProperty ("rendered", false) ? v.dspRendered : v.dsp) = dspFrom (c);
        if (c.hasType ("ENGINE")) (c.getProperty ("rendered", false) ? v.rendered : v.settings) = settingsFrom (c);
        if (c.hasType ("IDENTITY")) v.identity = identityFrom (c);
        if (c.hasType ("TAKE")) (c.getProperty ("voice").toString() == "A" ? v.a : v.b).add (dir.getChildFile (c.getProperty ("file").toString()).getFullPathName());
    }
    v.preview = dir.getChildFile ("preview.wav");
    if (v.rendered == EngineSettings()) v.rendered = v.settings;
    if (v.dspRendered == DspSettings()) v.dspRendered = v.dsp;
    return v.name.isNotEmpty() && ! v.a.isEmpty() && ! v.b.isEmpty();
}

Processor::Processor()
    : juce::AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)   // REC only (an instrument: never passed through)
                                             .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    board.ghosts.clear();
    board.add (0.18f, 0.5f); board.add (0.82f, 0.5f);           // A on the left, B on the right
    board.ghosts[0].label = "A"; board.ghosts[1].label = "B";
    board.ghosts[0].colour = juce::Colour (0xff3fc8ff); board.ghosts[1].colour = juce::Colour (0xffa46bff);   // the cards' colours
    board.weights.mode = ghosty::spirit::WeightMode::InverseDistance; board.weights.power = 1.0f;   // on the A..B line: B's share = how far along
    setBalance (0.5f);
    board.orbit.rate = 1.0f / 16.0f; board.orbit.depth = 0.2f; board.orbit.sync = true;   // one trip every 4 bars
    terrain.setWithoutUndo (ghost::terrain::Terrain());          // flat: Seed-VC's own defaults hold until the land is shaped
    terrain.onChange = [this] { changed(); };
    runner.onFinished = [this] { finished(); };
    dspRunner.onFinished = [this] { dspFinished(); };
    reloadLibrary();
    startTimerHz (10);
}

Processor::~Processor() { stopTimer(); runner.onFinished = nullptr; dspRunner.onFinished = nullptr; runner.cancel(); dspRunner.cancel(); terrain.onChange = nullptr; }

// ---------------------------------------------------------------- blend
float Processor::blendB() const
{
    std::vector<float> w; board.weightsAt (board.puckX, board.puckY, w);
    return w.size() >= 2 ? juce::jlimit (0.0f, 1.0f, w[1] / juce::jmax (1.0e-6f, w[0] + w[1])) : 0.5f;
}

void Processor::setBalance (float b)
{
    b = juce::jlimit (0.0f, 1.0f, b);
    const auto& A = board.ghosts[0]; const auto& B = board.ghosts[1];
    board.puckX = A.x + (B.x - A.x) * b; board.puckY = A.y + (B.y - A.y) * b;
}

VoiceLand Processor::land() const { return mapVoiceLand (terrain.terrain(), 0.0f, terrainAmount, engine); }

float Processor::blendAt (double seconds) const
{
    const auto l = land();
    auto o = board.orbit;
    o.depth = juce::jlimit (0.0f, 0.5f, o.depth + l.motionDepth);
    const double beats = seconds * bpm() / 60.0;
    const auto p = motionOn() ? ghosty::spirit::orbitPuck (o, board.activePath(), { board.puckX, board.puckY }, o.sync ? beats : seconds)
                              : juce::Point<float> { board.puckX, board.puckY };
    std::vector<float> w; board.weightsAt (p.x, p.y, w);
    const float b = w.size() >= 2 ? w[1] / juce::jmax (1.0e-6f, w[0] + w[1]) : 0.5f;
    return juce::jlimit (0.0f, 1.0f, b + l.lean);
}

EngineSettings Processor::effectiveSettings() const { return applyLand (settings, land()); }
DspSettings Processor::effectiveDsp() const { return applyLand (dsp, land()); }

Recipe Processor::currentRecipe() const
{
    Recipe r; r.engine = engine; r.balance = roundMix (blendB() + land().lean); r.seed = effectiveSettings(); r.dsp = effectiveDsp();
    return r;
}

Recipe Processor::previewRecipe() const
{
    Recipe r; r.engine = previewEngine; r.balance = previewBalance; r.seed = previewSettings; r.dsp = previewDsp;
    return r;
}

bool Processor::previewIsCurrent() const
{
    return preview.existsAsFile() && ! previewQuick && previewBalance >= 0 && previewRecipe().same (currentRecipe());
}

juce::String Processor::noveltyWhy() const
{
    if (noveltyWorks()) return {};
    return engineName (engine) + " has no voice space to turn in (it blends the two singers' vowel fingerprints). NOVELTY needs NEURAL or BEST OF ALL.";
}

void Processor::cancelAll()
{
    runner.cancel(); dspRunner.cancel();
    pend = Pending(); currentTask = Task::none;
    changed();
}

juce::String Processor::progressText() const
{
    juce::String t;
    if (dspRunner.busy()) t = dspRunner.message();
    if (runner.busy()) t = (t.isNotEmpty() ? t + "  -  " : juce::String()) + (currentTask == Task::measure ? "" : "NEURAL: ") + uiWords (runner.message());
    if (t.isEmpty() && currentTask == Task::preview) t = "putting voice C together";
    return t;
}

float Processor::progressValue() const
{
    if (runner.busy() && dspRunner.busy()) return 0.5f * (runner.progress() + dspRunner.progress());
    if (runner.busy()) return runner.progress();
    if (dspRunner.busy()) return dspRunner.progress();
    return 1.0f;
}

// ---------------------------------------------------------------- jobs
Job Processor::makePreviewJob() const
{
    Job j;
    j.a = takes[slotA]; j.b = takes[slotB];
    j.performance = takes[slotPerf].isEmpty() ? takes[slotA][0] : takes[slotPerf][0];
    j.settings = effectiveSettings(); j.rights = rights;
    j.mixes = { std::round (juce::jlimit (0.0f, 1.0f, blendB() + land().lean) * 1000.0f) / 1000.0f };
    j.outDir = workDir().getChildFile ("renders").getChildFile ("voiceC_" + juce::Time::getCurrentTime().formatted ("%Y-%m-%d_%H%M%S"));
    return j;
}

Job Processor::makeSongJob() const
{
    Job j;
    j.a = takes[slotA]; j.b = takes[slotB]; j.performance = takes[slotPerf][0];
    j.settings = effectiveSettings(); j.rights = rights;
    const double len = juce::jmax (0.5, takeSeconds (slotPerf));
    float lo = 1, hi = 0;
    for (int k = 0; k <= 400; ++k) { const float m = blendAt (len * k / 400.0); lo = juce::jmin (lo, m); hi = juce::jmax (hi, m); }
    j.mixes = ladderFor (lo, hi);
    j.outDir = workDir().getChildFile ("renders").getChildFile ("song_" + juce::Time::getCurrentTime().formatted ("%Y-%m-%d_%H%M%S"));
    return j;
}

std::vector<float> Processor::seedMixesFor (const std::vector<float>& ladder) const
{
    if (engine == Engine::seedvc) return ladder;
    if (engine != Engine::best) return {};
    if (ladder.size() == 1) return seedWeight (ladder[0]) > 0.0f ? ladder : std::vector<float>();
    // the steps around the SEED-VC zones the blend curve reaches (below 25 % B, above 75 % B)
    std::vector<float> v;
    const bool low = ladder.front() < 0.25f, high = ladder.back() > 0.75f;
    for (float m : ladder) if ((low && m <= 0.25f) || (high && m >= 0.75f)) v.push_back (m);
    return v;
}

juce::String Processor::whyNotPreview() const
{
    if (busy()) return "Busy...";
    const auto r = currentRecipe();
    if (r.usesSeed() && ! engineReady())
        return engine == Engine::best ? "This close to A or B, BEST OF ALL uses NEURAL: install the voice engine (SETTINGS), or move the balance towards the middle."
                                      : "NEURAL needs the voice engine: install it (SETTINGS), or pick NATURAL / CLASSIC (built in).";
    return whyNot (makePreviewJob());
}

juce::String Processor::whyNotSong() const
{
    if (busy()) return "Busy...";
    if (takes[slotPerf].isEmpty()) return "Add the performance to sing (record your melody or drop an audio guide).";
    const auto j = makeSongJob();
    if (! seedMixesFor (j.mixes).empty() && ! engineReady())
        return engine == Engine::best ? "BEST OF ALL sings near A or B with NEURAL: install the voice engine (SETTINGS), or keep the blend in the middle."
                                      : "NEURAL needs the voice engine: install it (SETTINGS), or pick NATURAL / CLASSIC (built in).";
    return whyNot (j);
}

bool Processor::createVoiceC()
{
    if (busy() || whyNotPreview().isNotEmpty()) return false;
    lastJob = makePreviewJob();
    if (whyNot (lastJob).isNotEmpty()) return false;
    // the preview sings the first seconds of the guide
    std::vector<float> x; int fs = 0; juce::String err;
    if (readMono (juce::File (lastJob.performance), x, fs, err) && x.size() > (size_t) (kPreviewSeconds * fs))
    {
        x.resize ((size_t) (kPreviewSeconds * fs));
        const auto cut = lastJob.outDir.getChildFile ("guide.wav");
        if (writeWav (cut, x, fs)) lastJob.performance = cut.getFullPathName();
    }
    lastJob.fakeDelay = fakeEngineDelay;
    pend = Pending(); pend.recipe = currentRecipe(); pend.outDir = lastJob.outDir; pend.t0 = juce::Time::getMillisecondCounterHiRes();
    const float b = pend.recipe.balance;
    pend.needSeed = pend.recipe.usesSeed();
    pend.needDsp = pend.recipe.usesDsp() || (pend.needSeed && instantPreview);
    if (pend.needSeed) pend.seedMixes = { b };
    if (pend.needDsp) pend.dspMixes = { b };
    if (pend.needSeed && ! runner.startRender (lastJob)) return false;
    if (pend.needDsp)
    {
        DspJob d; d.a = lastJob.a; d.b = lastJob.b; d.performance = lastJob.performance; d.mixes = pend.dspMixes; d.engine = dspEngineFor (engine);
        d.settings = pend.recipe.dsp; d.semitones = pend.recipe.seed.semitones; d.rights = rights; d.outDir = lastJob.outDir;
        if (! dspRunner.start (d)) { runner.cancel(); return false; }
    }
    currentTask = Task::preview; changed();
    return true;
}

bool Processor::renderSong()
{
    if (busy() || takes[slotPerf].isEmpty() || whyNotSong().isNotEmpty()) return false;
    lastJob = makeSongJob(); lastJob.fakeDelay = fakeEngineDelay;
    if (whyNot (lastJob).isNotEmpty()) return false;
    const auto ladder = lastJob.mixes;
    pend = Pending(); pend.song = true; pend.recipe = currentRecipe(); pend.outDir = lastJob.outDir; pend.t0 = juce::Time::getMillisecondCounterHiRes();
    pend.perfName = juce::File (lastJob.performance).getFileNameWithoutExtension();
    pend.seedMixes = seedMixesFor (ladder);
    pend.needSeed = ! pend.seedMixes.empty();
    pend.needDsp = isDsp (engine) || engine == Engine::best || (pend.needSeed && instantPreview);
    if (pend.needDsp) pend.dspMixes = ladder;
    if (pend.needSeed) { Job sj = lastJob; sj.mixes = pend.seedMixes; if (! runner.startRender (sj)) return false; }
    if (pend.needDsp)
    {
        DspJob d; d.a = lastJob.a; d.b = lastJob.b; d.performance = lastJob.performance; d.mixes = pend.dspMixes; d.engine = dspEngineFor (engine);
        d.settings = pend.recipe.dsp; d.semitones = pend.recipe.seed.semitones; d.rights = rights; d.outDir = lastJob.outDir;
        if (! dspRunner.start (d)) { runner.cancel(); return false; }
    }
    currentTask = Task::song; changed();
    return true;
}

bool Processor::analyzeVoices()
{
    if (busy() || takes[slotA].isEmpty() || takes[slotB].isEmpty() || ! engineReady()) return false;
    Job j = makePreviewJob(); j.outDir = workDir().getChildFile ("renders").getChildFile ("analyze");
    if (! runner.startAnalyze (j)) return false;
    currentTask = Task::analyze; changed();
    return true;
}

bool Processor::installEngine()
{
    if (busy() || ! runner.startInstall()) return false;
    currentTask = Task::install; changed();
    return true;
}

namespace
{
// the takes of a ladder, read back (same rate), and their MOTION take
bool motionTake (const std::map<float, juce::File>& res, const std::function<float (double)>& mixAt, std::vector<float>& y, int& fs, std::vector<float>& mixes)
{
    std::vector<std::vector<float>> audio; fs = 0; mixes.clear();
    for (auto& [m, f] : res)
    {
        std::vector<float> x; int r = 0; juce::String err;
        if (! readMono (f, x, r, err)) continue;
        if (fs == 0) fs = r;
        if (r != fs) continue;
        mixes.push_back (m); audio.push_back (std::move (x));
    }
    if (audio.empty()) return false;
    y = renderMotion (mixes, audio, fs, mixAt);
    return ! y.empty();
}
}

void Processor::finished()
{
    const auto t = currentTask;
    const bool ok = runner.state() == Runner::State::done;
    if (t == Task::analyze) { currentTask = Task::none; if (ok) analysisAB = runner.analysis(); changed(); return; }
    if (t == Task::install) { currentTask = Task::none; changed(); return; }
    if (t == Task::measure)
    {
        currentTask = Task::none;
        if (ok) { const auto ids = runner.identities(); if (! ids.empty()) previewId = ids.begin()->second; }
        changed(); return;
    }
    if (t != Task::preview && t != Task::song) { changed(); return; }
    pend.seedDone = true; pend.seedOk = ok;
    if (ok) { pend.seedRes = runner.results(); pend.seedIds = runner.identities(); }
    else pend.error = runner.message();
    if (! pend.needDsp || pend.dspDone) finalise();
    else changed();
}

void Processor::dspFinished()
{
    const auto t = currentTask;
    if (t != Task::preview && t != Task::song) { changed(); return; }
    pend.dspDone = true; pend.dspOk = dspRunner.state() == DspRunner::State::done;
    if (pend.dspOk) pend.dspRes = dspRunner.results();
    else if (pend.error.isEmpty()) pend.error = dspRunner.message();
    if (pend.needSeed && ! pend.seedDone)
    {   // SEED-VC is still singing: NATURAL's render is the instant preview / quick take meanwhile
        if (pend.dspOk && ! pend.dspRes.empty())
        {
            if (! pend.song)
            {
                preview = pend.dspRes.begin()->second; previewBalance = pend.recipe.balance; previewQuick = true;
                previewEngine = pend.recipe.engine; previewSettings = pend.recipe.seed; previewDsp = pend.recipe.dsp; previewId = Identity();
            }
            else
            {
                std::vector<float> y, mixes; int fs = 0;
                if (motionTake (pend.dspRes, [this] (double s) { return blendAt (s); }, y, fs, mixes))
                {
                    const auto out = pend.outDir.getChildFile ("quick take (NATURAL).wav");
                    if (writeWav (out, y, fs, 32)) { result = out; resultInfo = "NATURAL quick take - NEURAL is still singing the real one"; resultId = Identity(); }
                }
            }
        }
        changed();
        return;
    }
    finalise();
}

void Processor::finalise()
{
    currentTask = Task::none;
    const auto r = pend.recipe;
    const Engine e = r.engine;
    // which parts this engine needs (the instant NATURAL part of a SEED-VC task is only a stand-in)
    const bool wantSeed = pend.needSeed, wantDsp = isDsp (e) || (e == Engine::best && (pend.song || r.usesDsp()));
    if ((wantSeed && (! pend.seedOk || pend.seedRes.empty())) || (wantDsp && (! pend.dspOk || pend.dspRes.empty())))
    {
        if (previewQuick) { preview = juce::File(); previewQuick = false; }
        if (pend.error.isEmpty()) pend.error = "the engine wrote no audio";
        lastError = pend.error;
        changed(); return;
    }
    lastError = {};
    const double took = (juce::Time::getMillisecondCounterHiRes() - pend.t0) / 1000.0;
    if (! pend.song)
    {
        const float b = r.balance;
        juce::File f; Identity id;
        if (e == Engine::seedvc || (e == Engine::best && seedWeight (b) >= 1.0f)) { f = pend.seedRes.begin()->second; id = pend.seedIds.empty() ? Identity() : pend.seedIds.begin()->second; }
        else if (isDsp (e) || seedWeight (b) <= 0.0f) f = pend.dspRes.begin()->second;
        else
        {   // BEST OF ALL in the crossfade zone: both, at this balance's weight
            std::vector<float> s, n; int fs = 0, nfs = 0; juce::String err;
            if (readMono (pend.seedRes.begin()->second, s, fs, err) && readMono (pend.dspRes.begin()->second, n, nfs, err))
            {
                const auto y = bestOfAll (s, n, fs, nfs, [b] (double) { return b; });
                f = pend.outDir.getChildFile ("best_of_all_" + juce::String (juce::roundToInt (b * 1000)).paddedLeft ('0', 4) + ".wav");
                if (! writeWav (f, y, fs, 32)) f = juce::File();
            }
        }
        if (f.existsAsFile())
        {
            preview = f; previewBalance = b; previewQuick = false; previewEngine = e; previewSettings = r.seed; previewDsp = r.dsp; previewId = id;
            previewSeconds = took;
            if (! id.valid && engineReady()) startMeasure (f, b);
        }
    }
    else
    {
        auto mixAt = [this] (double s) { return blendAt (s); };
        std::vector<float> seedY, natY, seedMixes, natMixes; int seedFs = 0, natFs = 0;
        if (wantSeed) motionTake (pend.seedRes, mixAt, seedY, seedFs, seedMixes);
        if (wantDsp) motionTake (pend.dspRes, mixAt, natY, natFs, natMixes);
        std::vector<float> y; int fs = 0;
        if (e == Engine::seedvc) { y = seedY; fs = seedFs; }
        else if (isDsp (e)) { y = natY; fs = natFs; }
        else if (seedY.empty()) { y = natY; fs = natFs; }
        else { y = bestOfAll (seedY, natY, seedFs, natFs, mixAt); fs = seedFs; }
        const auto& ladder = pend.dspMixes.empty() ? pend.seedMixes : pend.dspMixes;
        if (! y.empty() && fs > 0)
        {
            const auto out = pend.outDir.getChildFile (safeName (voiceName) + " - " + pend.perfName + " (" + engineName (e) + ").wav");
            if (writeWav (out, y, fs, 32))   // 32-bit float: Seed-VC's output can peak past 0 dBFS; nothing of ours touches its level
            {
                result = out;
                resultInfo = engineName (e) + "  -  " + (ladder.size() == 1 ? mixWord (ladder[0]) : "MOTION " + juce::String (juce::roundToInt (ladder.front() * 100)) + "-" + mixWord (ladder.back()))
                           + "  -  " + juce::String ((double) y.size() / fs, 1) + " s  -  sung in " + juce::String (took, took < 10 ? 1 : 0) + " s";
                resultId = e == Engine::seedvc && ladder.size() == 1 && ! pend.seedIds.empty() ? pend.seedIds.begin()->second : Identity();
            }
        }
    }
    changed();
}

void Processor::startMeasure (const juce::File& f, float mix)
{
    if (runner.busy()) return;
    Job j = lastJob; j.mixes = { mix }; j.measure = { { mix, f.getFullPathName() } };
    if (runner.startMeasure (j)) currentTask = Task::measure;
}

void Processor::timerCallback()
{
    if (recArmed.load() && recPos.load() >= (int) recBuf.size()) stopRecording();
}

// ---------------------------------------------------------------- library
void Processor::reloadLibrary()
{
    library.clear();
    for (auto& d : workDir().getChildFile ("library").findChildFiles (juce::File::findDirectories, false))
    {
        SavedVoice v;
        if (SavedVoice::load (d, v)) library.push_back (v);
    }
    std::sort (library.begin(), library.end(), [] (const SavedVoice& x, const SavedVoice& y) { return x.created < y.created; });
}

bool Processor::saveVoiceC (const juce::String& name0, juce::String& why)
{
    if (! previewIsCurrent()) { why = "Create voice C first (its preview must match the current settings)."; return false; }
    const auto name = safeName (name0);
    auto dir = workDir().getChildFile ("library").getChildFile (name);
    if (dir.exists()) dir = dir.getNonexistentSibling();
    if (! dir.createDirectory()) { why = "Can't write to " + dir.getFullPathName(); return false; }
    juce::ValueTree t ("GHOSTVOICE_C");
    t.setProperty ("name", dir.getFileName(), nullptr);
    t.setProperty ("created", juce::Time::getCurrentTime().toMilliseconds(), nullptr);
    t.setProperty ("balance", previewBalance, nullptr);
    t.setProperty ("engine", (int) previewEngine, nullptr);
    t.appendChild (dspTree (dsp), nullptr);
    { auto pd = dspTree (previewDsp); pd.setProperty ("rendered", true, nullptr); t.appendChild (pd, nullptr); }
    t.appendChild (settingsTree (settings), nullptr);                                   // the controls as set
    { auto ps = settingsTree (previewSettings); ps.setProperty ("rendered", true, nullptr); t.appendChild (ps, nullptr); }   // as rendered (TERRAIN's moves in)
    t.appendChild (identityTree (previewId), nullptr);
    for (int s : { (int) slotA, (int) slotB })
        for (int i = 0; i < takes[s].size(); ++i)
        {
            const juce::File src (takes[s][i]);
            const auto dst = dir.getChildFile ((s == slotA ? "A" : "B") + juce::String (i + 1) + "_" + src.getFileName());
            if (! src.copyFileTo (dst)) { why = "Can't copy " + src.getFileName(); dir.deleteRecursively(); return false; }
            juce::ValueTree c ("TAKE"); c.setProperty ("voice", s == slotA ? "A" : "B", nullptr); c.setProperty ("file", dst.getFileName(), nullptr);
            t.appendChild (c, nullptr);
        }
    preview.copyFileTo (dir.getChildFile ("preview.wav"));
    preview = dir.getChildFile ("preview.wav");      // the kept copy (the render folder may be cleaned)
    if (auto x = t.createXml(); x == nullptr || ! x->writeTo (dir.getChildFile ("voice.xml"))) { why = "Can't write the voice file"; return false; }
    voiceName = dir.getFileName();
    reloadLibrary();
    changed();
    return true;
}

void Processor::useVoice (int i)
{
    if (! juce::isPositiveAndBelow (i, (int) library.size())) return;
    const auto& v = library[(size_t) i];
    takes[slotA] = v.a; takes[slotB] = v.b;
    settings = v.settings; dsp = v.dsp; engine = v.engine; setBalance (v.balance);
    preview = v.preview; previewBalance = v.balance; previewSettings = v.rendered; previewDsp = v.dspRendered; previewEngine = v.engine; previewQuick = false; previewId = v.identity;
    voiceName = v.name;
    changed();
}

void Processor::deleteVoice (int i)
{
    if (! juce::isPositiveAndBelow (i, (int) library.size())) return;
    library[(size_t) i].dir.deleteRecursively();
    reloadLibrary();
    changed();
}

// ---------------------------------------------------------------- slots
void Processor::addTakes (int slot, const juce::StringArray& files)
{
    if (! juce::isPositiveAndBelow (slot, (int) kSlots) || files.isEmpty()) return;
    if (slot == slotPerf) { takes[slot].clear(); takes[slot].add (files[files.size() - 1]); }
    else for (auto& f : files) if (! takes[slot].contains (f)) takes[slot].add (f);
    if (slot != slotPerf) analysisAB = juce::var();
    changed();
}
void Processor::clearSlot (int slot) { if (juce::isPositiveAndBelow (slot, (int) kSlots)) takes[slot].clear(); if (slot != slotPerf) analysisAB = juce::var(); changed(); }

double Processor::takeSeconds (int slot) const
{   // the files' lengths, remembered per file + time stamp (the window asks on every paint)
    double s = 0;
    for (auto& f : takes[slot])
    {
        const juce::File F (f);
        const auto k = F.getFullPathName() + "|" + juce::String (F.getLastModificationTime().toMilliseconds());
        if (auto it = lengthCache.find (k); it != lengthCache.end()) { s += it->second; continue; }
        double len = 0;
        juce::AudioFormatManager fm; fm.registerBasicFormats();
        if (std::unique_ptr<juce::AudioFormatReader> r { fm.createReaderFor (F) }; r != nullptr && r->sampleRate > 0) len = (double) r->lengthInSamples / r->sampleRate;
        if (lengthCache.size() > 64) lengthCache.clear();
        lengthCache[k] = len; s += len;
    }
    return s;
}

// ---------------------------------------------------------------- recorder
void Processor::startRecording (int slot)
{
    if (recArmed.load()) stopRecording();
    recBuf.assign ((size_t) (kMaxRecordSeconds * sampleRate), 0.0f);
    recPos = 0; recSlot = slot; recArmed = true;
    changed();
}

void Processor::stopRecording()
{
    if (! recArmed.exchange (false)) return;
    const int slot = recSlot.exchange (-1), n = recPos.load();
    if (n > (int) (0.5 * sampleRate))
    {
        std::vector<float> x (recBuf.begin(), recBuf.begin() + n);
        const auto f = workDir().getChildFile ("takes").getChildFile (slotName (slot).replace (" ", "_") + "_" + juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S") + ".wav");
        if (writeWav (f, x, (int) sampleRate)) addTakes (slot, { f.getFullPathName() });
    }
    recBuf.clear(); recBuf.shrink_to_fit();
    changed();
}

double Processor::recordedSeconds() const { return recArmed.load() ? recPos.load() / sampleRate : 0.0; }

// ---------------------------------------------------------------- player
void Processor::play (const juce::File& f)
{
    stopPlaying();
    std::vector<float> x; int fs = 0; juce::String err;
    if (! readMono (f, x, fs, err)) return;
    suspendProcessing (true);
    playBuf = std::move (x); playFs = fs; playPos = 0; playingFile = f;
    suspendProcessing (false);
    playing = true;
    changed();
}
void Processor::stopPlaying() { playing = false; changed(); }
float Processor::playPosition() const { return playBuf.empty() ? 0.0f : (float) (playPos.load() / (double) playBuf.size()); }

// ---------------------------------------------------------------- audio
void Processor::prepareToPlay (double sr, int block) { sampleRate = sr > 0 ? sr : 48000.0; chopper->prepare (sampleRate, block); }

bool Processor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto o = l.getMainOutputChannelSet();
    const auto i = l.getMainInputChannelSet();   // an instrument: the input may be off (FL Studio's channel rack) or match the output
    return (o == juce::AudioChannelSet::mono() || o == juce::AudioChannelSet::stereo()) && (i.isDisabled() || i == o);
}

void Processor::processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    const int n = buf.getNumSamples(), ch = buf.getNumChannels();
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition()) if (auto b = pos->getBpm()) if (*b > 20.0 && *b < 400.0) hostBpm = *b;
    if (recArmed.load() && ch > 0)
    {
        const int p = recPos.load();
        const int room = juce::jmax (0, juce::jmin (n, (int) recBuf.size() - p));
        for (int i = 0; i < room; ++i) { float s = 0; for (int c = 0; c < ch; ++c) s += buf.getSample (c, i); recBuf[(size_t) (p + i)] = s / (float) ch; }
        recPos = p + room;
    }
    // an instrument: the input is never heard. The CHOP page plays (INTERSECT, from the MIDI and the on-screen keys)
    for (int c = 0; c < ch; ++c) buf.clear (c, 0, n);
    chopper->process (buf, midi, getPlayHead());
    midi.clear();
    if (playing.load() && ! playBuf.empty())
    {
        const double step = (double) playFs / sampleRate;   // the file at its own rate, linear interpolation; the input is muted meanwhile
        double pos = playPos.load();
        for (int i = 0; i < n; ++i)
        {
            const auto k = (size_t) pos; float s = 0;
            if (k + 1 < playBuf.size()) { const float t = (float) (pos - (double) k); s = playBuf[k] * (1 - t) + playBuf[k + 1] * t; }
            for (int c = 0; c < ch; ++c) buf.addSample (c, i, s);
            pos += step;
        }
        playPos = pos;
        if (pos >= (double) playBuf.size()) playing = false;
    }
}

// ---------------------------------------------------------------- state
juce::ValueTree Processor::toTree() const
{
    juce::ValueTree t ("GHOSTVOICE");
    t.setProperty ("v", 1, nullptr);
    for (int s = 0; s < kSlots; ++s) t.setProperty ("slot" + juce::String (s), takes[s].joinIntoString ("\n"), nullptr);
    t.setProperty ("rights", rights, nullptr);
    t.setProperty ("look", theme == ghosty::themes::Theme::Light ? "light" : "dark", nullptr);
    t.setProperty ("name", voiceName, nullptr); t.setProperty ("bubbles", bubbles, nullptr);
    t.setProperty ("preview", preview.getFullPathName(), nullptr); t.setProperty ("previewBalance", previewBalance, nullptr);
    t.setProperty ("result", result.getFullPathName(), nullptr); t.setProperty ("resultInfo", resultInfo, nullptr);
    t.setProperty ("terrainAmt", terrainAmount, nullptr);
    t.setProperty ("engine", (int) engine, nullptr); t.setProperty ("instant", instantPreview, nullptr);
    t.setProperty ("previewEngine", (int) previewEngine, nullptr);
    t.appendChild (dspTree (dsp), nullptr);
    { auto pd = dspTree (previewDsp); pd.setProperty ("preview", true, nullptr); t.appendChild (pd, nullptr); }
    t.setProperty ("terrain", juce::JSON::toString (terrain.stateToVar(), true), nullptr);
    t.appendChild (settingsTree (settings), nullptr);
    auto ps = settingsTree (previewSettings); ps.setProperty ("preview", true, nullptr); t.appendChild (ps, nullptr);
    t.appendChild (identityTree (previewId), nullptr);
    t.appendChild (board.toTree(), nullptr);
    t.appendChild (chopper->toTree(), nullptr);
    return t;
}

void Processor::fromTree (const juce::ValueTree& t)
{
    if (! t.hasType ("GHOSTVOICE")) return;
    for (int s = 0; s < kSlots; ++s) { takes[s].clear(); takes[s].addTokens (t.getProperty ("slot" + juce::String (s)).toString(), "\n", {}); takes[s].removeEmptyStrings(); }
    rights = t.getProperty ("rights", false);
    theme = t.getProperty ("look", "dark").toString() == "light" ? ghosty::themes::Theme::Light : ghosty::themes::Theme::Dark;   // build 13: DARK unless the project saved LIGHT
    voiceName = t.getProperty ("name", "Voice C").toString(); bubbles = t.getProperty ("bubbles", true);
    preview = juce::File (t.getProperty ("preview").toString()); previewBalance = t.getProperty ("previewBalance", -1.0f);
    result = juce::File (t.getProperty ("result").toString()); resultInfo = t.getProperty ("resultInfo").toString();
    terrainAmount = t.getProperty ("terrainAmt", 1.0f);
    engine = engineFromInt (t.getProperty ("engine", (int) Engine::seedvc));     // sessions from build 1 were SEED-VC
    instantPreview = t.getProperty ("instant", true);
    previewEngine = engineFromInt (t.getProperty ("previewEngine", (int) Engine::seedvc)); previewQuick = false;
    const auto tv = juce::JSON::parse (t.getProperty ("terrain").toString());
    if (tv.isObject()) terrain.stateFromVar (tv);
    for (auto c : t)
    {
        if (c.hasType ("ENGINE")) (c.getProperty ("preview", false) ? previewSettings : settings) = settingsFrom (c);
        if (c.hasType ("DSP")) (c.getProperty ("preview", false) ? previewDsp : dsp) = dspFrom (c);
        if (c.hasType ("IDENTITY")) previewId = identityFrom (c);
        if (c.hasType ("SPIRITBOARD")) { auto b = ghosty::spirit::Board::fromTree (c); if (b.ghosts.size() >= 2) board = b; }
        if (c.hasType ("CHOP")) chopper->fromTree (c);
    }
    changed();
}

void Processor::getStateInformation (juce::MemoryBlock& mb) { if (auto x = toTree().createXml()) copyXmlToBinary (*x, mb); }
void Processor::setStateInformation (const void* d, int n) { if (auto x = getXmlFromBinary (d, n)) fromTree (juce::ValueTree::fromXml (*x)); }

juce::AudioProcessorEditor* Processor::createEditor() { return new Editor (*this); }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new gv::Processor(); }
