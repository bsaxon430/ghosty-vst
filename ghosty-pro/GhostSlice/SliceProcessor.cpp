#include "SliceProcessor.h"
#include "SliceEditor.h"

using namespace gslice;

static juce::File looseCacheDir() { return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("GhostBlock").getChildFile ("Ghost Slice").getChildFile ("cache"); }

SliceProcessor::SliceProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    auto flag = alive;
    studio.onRendered = [this, flag] { if (flag->load()) triggerAsyncUpdate(); };
    analyzer.onDone = [this, flag] { if (flag->load()) { analysisArrived.store (true); triggerAsyncUpdate(); } };
    history.reset (Project());
    rebuild (true);
}

SliceProcessor::~SliceProcessor()
{
    alive->store (false);
    analyzer.onDone = nullptr; analyzer.stop();
    if (bankCancel) bankCancel->store (true);
    stopTimer(); cancelPendingUpdate();
    studio.onRendered = nullptr;
}

bool SliceProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() && l.getMainInputChannelSet().isDisabled();
}

void SliceProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    maxBlock = juce::jmax (16, samplesPerBlock);
    hostSr.store (sampleRate);
    player.prepare (sampleRate, maxBlock);
    playEngine.prepare (sampleRate, maxBlock);
    ceiling.prepare (sampleRate);
    setLatencySamples (ceiling.latency());   // the only latency: the true-peak ceiling (LIVE stretch is primed ahead, it adds none)
    demo.prepare (sampleRate);
    sync.prepare (sampleRate);
    if (preparedSr.exchange (sampleRate) != sampleRate) triggerAsyncUpdate();   // a new rate: the working copy + the plan follow (message thread)
}

void SliceProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (buffer.getNumChannels() < 2 || n == 0) return;
    float* L = buffer.getWritePointer (0); float* R = buffer.getWritePointer (1);
    const auto pos = getPlayHead() != nullptr ? getPlayHead()->getPosition() : juce::Optional<juce::AudioPlayHead::PositionInfo>();
    const auto t = sync.resolve (pos, n);
    const double sr = getSampleRate() > 0 ? getSampleRate() : hostSr.load();
    if (stopReq.exchange (false)) ownPlaying.store (false);
    if (const auto s = seekReq.exchange (-1); s >= 0) playHf.store (s);
    if (t.fromDaw && t.playing)
    {   // the DAW plays: its timeline is ours
        hostPlaying.store (true); ownPlaying.store (false);
        int64 hp = 0;
        if (pos.hasValue() && pos->getTimeInSamples().hasValue()) hp = *pos->getTimeInSamples();
        else hp = (int64) std::llround (t.ppq * 60.0 / juce::jmax (1.0, t.bpm) * sr);
        if (isNonRealtime()) offlineWait (hp, n);
        player.render (L, R, n, hp, true);
        displayHf.store (hp + n);
    }
    else
    {
        hostPlaying.store (false);
        const bool playing = ownPlaying.load();
        int64 p = playHf.load();
        if (playing)
        {
            if (isNonRealtime()) offlineWait (p, n);
            const int64 A = loopA.load(), B = loopB.load(); const bool lp = loopOn.load() && B > A;
            int done = 0;
            while (done < n)
            {
                int m = n - done;
                if (lp && p < B && p + m > B) m = (int) (B - p);
                if (lp && B - p <= (int64) (gslice::Player::kPrimeAheadS * sr)) player.setJumpHint (A);   // the wrap: prime the loop start
                player.render (L + done, R + done, m, p, true);
                p += m; done += m;
                if (lp && p >= B && p - m < B) p = A;
            }
            if (! lp && p > planLenHf.load() + (int64) (0.3 * sr)) { ownPlaying.store (false); p = cursorHf.load(); }   // the end: back to the cursor
            playHf.store (p);
        }
        else
        {   // stopped: voices fade out; the stretchers for where play will start are primed in the background
            const bool dawPos = t.fromDaw && pos.hasValue() && pos->getTimeInSamples().hasValue();
            player.setJumpHint (dawPos ? *pos->getTimeInSamples() : p);
            player.render (L, R, n, p, false);
        }
        displayHf.store (p);
    }
    // PLAY: MIDI notes play the slices on top (zero added latency: the bank is pre-rendered)
    if (t.fromDaw)
    {
        hostBpm.store (t.bpm);
        if (std::abs (t.bpm - compiledBpmA.load()) > 0.01 && hasRepeats.load() && sync.getMode() == ghosty::SyncTransport::Mode::Daw) triggerAsyncUpdate();
    }
    playEngine.process (L, R, n, midi, t.fromDaw ? t.bpm : projectBpm.load());
    ceiling.process (L, R, n);
    demo.process (buffer, license.isUnlocked());
}

// OFFLINE BOUNCE: wait for the STUDIO renders this block plays (non-realtime renders only: sleeping and the cache's mutex are fine
// there; the realtime path never comes here). A table whose wait timed out is not waited for again.
void SliceProcessor::offlineWait (int64 T, int n)
{
    const auto gen = player.currentGen();
    if (gen == 0 || gen == offlineGaveUpGen) return;
    player.fillStudio (studio);
    if (player.missingStudio (T, n) == 0) return;
    offlineWaits.fetch_add (1);
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    while (true)
    {
        std::this_thread::sleep_for (std::chrono::milliseconds (2));
        player.fillStudio (studio);
        if (player.missingStudio (T, n) == 0) return;
        if (player.currentGen() != gen) { player.fillStudio (studio); if (player.missingStudio (T, n) == 0) return; }
        if (juce::Time::getMillisecondCounterHiRes() - t0 > offlineWaitMs) { offlineGaveUpGen = player.currentGen(); offlineTimeouts.fetch_add (1); return; }
    }
}

// ---------------------------------------------------------------- edits
bool SliceProcessor::apply (const Project& p, const juce::String& label)
{
    if (p == history.current()) return false;
    history.push (p, label);
    rebuild (true);
    return true;
}
bool SliceProcessor::undo() { if (! history.undo()) return false; rebuild (true); return true; }
bool SliceProcessor::redo() { if (! history.redo()) return false; rebuild (true); return true; }

void SliceProcessor::rebuild (bool recompile)
{
    if (source != nullptr && source->fingerprint + "/" + project().material != analysedKey) reanalyse();   // a new source or material
    if (recompile)
    {
        CompileOptions o; o.hostRate = hostSr.load(); o.bpm = compiledBpm = repeatBpm();
        compiledBpmA.store (compiledBpm); hasRepeats.store (! project().repeats.empty()); projectBpm.store (project().bpm);
        auto a = analysis();
        if (a != nullptr) { o.formantBaseHz = a->medianF0; o.pitchTrack = &a->pitch; }   // FORMANT 0 = the voice's own formants at any PITCH; note edits read the f0 track
        plan = compile (project(), source.get(), o);
        planOriginal = compileOriginal (project(), source.get(), o);
        planLenHf.store (plan.length);
    }
    publishTable();
    publishPlay();
    startTimer (150);   // STUDIO jobs 150 ms after the last edit (a drag doesn't start a render storm)
    if (onChanged) onChanged();
}

void SliceProcessor::publishTable()
{
    auto t = std::make_shared<PlayTable>();
    t->plan = abOriginal ? planOriginal : plan;
    t->work = work; t->useStudio = useStudio;
    t->studio.resize (t->plan.pieces.size());
    for (size_t i = 0; i < t->plan.pieces.size(); ++i) if (t->plan.pieces[i].stretch) t->studio[i] = studio.get (t->plan.pieces[i].studioKey);
    player.publish (t);
}

void SliceProcessor::requestStudio()
{
    std::vector<StudioCache::Job> jobs;
    if (useStudio && work != nullptr)
    {
        const int64 ph = displayHf.load();
        for (auto& pc : plan.pieces)
            if (pc.stretch && ! (pc.hold != nullptr && pc.hold->liveIsExact() && ! pc.hold->grains.empty()))   // (a SUSTAIN hold plays its grains exactly)
                jobs.push_back ({ pc, plan.pitchSemis, plan.formantSemis, ph >= pc.dst0 && ph < pc.dst1 ? 0 : std::min (std::abs (pc.dst0 - ph), std::abs (pc.dst1 - ph)) });
    }
    studio.request (work, std::move (jobs));
}

void SliceProcessor::timerCallback() { stopTimer(); requestStudio(); requestBank(); }

// ---------------------------------------------------------------- PLAY
double SliceProcessor::repeatBpm() const
{
    const double hb = hostBpm.load();
    if (isHosted() && sync.getMode() == ghosty::SyncTransport::Mode::Daw && hb >= 20.0) return hb;
    return project().bpm;
}

void SliceProcessor::publishPlay()
{
    auto t = std::make_shared<play::Table>();
    if (bank != nullptr && std::abs (bank->rate - hostSr.load()) < 1e-6) t->bank = bank;
    t->map = project().play;
    playEngine.publish (t);
}

std::vector<play::Slice> SliceProcessor::currentSlices() const
{
    auto a = analysis();
    if (bank != nullptr && bank->key == play::bankKey (project(), plan, a.get())) return bank->slices;
    return play::slices (project(), plan, a.get());
}

void SliceProcessor::requestBank()
{
    if (work == nullptr) return;
    auto an = analysis();
    const auto key = play::bankKey (project(), plan, an.get());
    if ((bank != nullptr && bank->key == key) || bankWant == key) return;
    if (bankCancel) bankCancel->store (true);
    bankWant = key;
    auto cancel = std::make_shared<std::atomic<bool>> (false); bankCancel = cancel;
    // (the worker owns copies of everything it reads: the processor may be gone before it finishes, so no cache pointer)
    auto flag = alive; const Project p = project(); const Plan pl = plan; auto src = source; auto w = work;
    juce::Thread::launch ([this, flag, cancel, p, pl, src, w, key, an]
    {
        auto b = play::buildBank (p, pl, src.get(), *w, nullptr, [cancel] { return cancel->load(); }, an.get());
        if (b == nullptr) return;
        juce::MessageManager::callAsync ([this, flag, b, key]
        {
            if (! flag->load() || key != bankWant) return;
            bank = b; publishPlay();
            if (onChanged) onChanged();
        });
    });
}

bool SliceProcessor::buildBankNow()
{
    if (work == nullptr) return false;
    auto an = analysis();
    const auto key = play::bankKey (project(), plan, an.get());
    if (bankCancel) bankCancel->store (true);
    bankWant = key;
    bank = play::buildBank (project(), plan, source.get(), *work, &studio, {}, an.get());
    publishPlay();
    return bank != nullptr;
}

bool SliceProcessor::exportPlayMidi (const juce::File& f, juce::String& error)
{
    if (! ghosty::exportUnlocked (license)) { error = "Export is part of the full version"; return false; }
    if (project().play.keys.empty()) { error = "Map the slices first (TURN INTO INSTRUMENT)"; return false; }
    return exporter::writeMidi (play::mappingMidi (project().play, currentSlices(), project().bpm, plan.hostRate), f, error);
}

bool SliceProcessor::writeSliceWav (int i, const juce::File& f, juce::String& error)
{
    if (! ghosty::exportUnlocked (license)) { error = "Export is part of the full version"; return false; }
    if (bank == nullptr || i < 0 || i >= (int) bank->audio.size()) { error = "That slice is still rendering"; return false; }
    return exporter::writeWav (bank->audio[(size_t) i], bank->rate, f, 24, error);
}

void SliceProcessor::handleAsyncUpdate()
{
    const bool arrived = analysisArrived.exchange (false);
    if (arrived)
    {
        if (auto a = analysis()) lastMessage = "Analysed (" + juce::String (gslice::analysis::materialName (a->onsets.used)) + "): " + juce::String ((int) a->onsets.events.size()) + " onsets"
                                              + (a->tempo.bpm > 0 ? ", " + juce::String (a->tempo.bpm, 1) + " BPM" : juce::String()) + (a->key.valid() ? ", " + a->key.readout() : juce::String());
    }
    if (source != nullptr && (work == nullptr || std::abs (work->rate - hostSr.load()) > 1e-6))
    {
        work = source::makeWorking (source, hostSr.load());
        studio.clear();
        rebuild (true);
        return;
    }
    if (arrived || std::abs (plan.hostRate - hostSr.load()) > 1e-6) { rebuild (true); return; }
    if (! project().repeats.empty() && std::abs (repeatBpm() - compiledBpm) > 0.01) { rebuild (true); return; }   // the DAW's tempo moved: REPEAT follows
    publishTable();
    player.collectGarbage();
    playEngine.collectGarbage();
    if (onChanged) onChanged();
}

// ---------------------------------------------------------------- analysis
void SliceProcessor::reanalyse()
{
    if (source == nullptr) { analyzer.stop(); analysedKey = {}; return; }
    analysedKey = source->fingerprint + "/" + project().material;
    analyzer.start (source, material(), projectLoc != juce::File() ? projectio::cacheDirOf (projectLoc) : looseCacheDir());
}

std::shared_ptr<const analysis::Result> SliceProcessor::analysis() const
{
    auto r = analyzer.result();
    if (r == nullptr || source == nullptr || r->fingerprint != source->fingerprint) return nullptr;
    return r;
}

std::vector<analysis::Event> SliceProcessor::alignEvents() const
{
    if (auto a = analysis(); a != nullptr && a->material == material()) return a->onsets.events;
    std::vector<analysis::Event> ev;
    if (source != nullptr) for (size_t i = 0; i < source->onsets.size(); ++i) { analysis::Event e; e.pos = source->onsets[i]; e.strength = source->onsetStrength[i]; ev.push_back (e); }
    return ev;
}

void SliceProcessor::setStudioMode (bool on) { if (useStudio == on) return; useStudio = on; publishTable(); requestStudio(); if (onChanged) onChanged(); }
void SliceProcessor::setCompareOriginal (bool b) { if (abOriginal == b) return; abOriginal = b; publishTable(); if (onChanged) onChanged(); }

// ---------------------------------------------------------------- audio in
static SourceRef refOf (const SourceAudio& s)
{
    SourceRef r; r.fingerprint = s.fingerprint; r.path = s.file.getFullPathName(); r.fileName = s.file.getFileName();
    r.sampleRate = s.sampleRate; r.frames = s.frames; r.channels = s.channels; return r;
}

void SliceProcessor::installSource (std::shared_ptr<const SourceAudio> s, bool keepProject)
{
    source = s; relinkPending = false;
    work = source::makeWorking (s, hostSr.load());
    studio.clear();
    if (! keepProject)
    {
        Project p = makeProject (refOf (*s));
        if (s->clip.valid())
        {   // GHOST CAPTURE: the clip says its tempo, downbeat and key
            p.bpm = s->clip.bpm; p.beatOffset = s->clip.offsetSeconds * p.sampleRate;
            bool minor = false; if (s->clip.key.isNotEmpty() && analysis::parseKey (s->clip.key, minor) >= 0) p.key = s->clip.key.toUpperCase();
        }
        history.reset (p); projectLoc = juce::File(); cursorHf.store (0); playHf.store (0);
    }
    else
    {   // the same recording (fingerprint checked by the caller): refresh where it lives now
        Project p = history.current(); p.source.path = s->file.getFullPathName(); p.source.fileName = s->file.getFileName();
        history.reset (p);
    }
    rebuild (true);
}

bool SliceProcessor::loadAudio (const juce::File& f, juce::String& error, bool keepProject)
{
    auto s = source::load (f, error, looseCacheDir());
    if (s == nullptr) return false;
    installSource (s, keepProject);
    lastMessage = "Loaded " + f.getFileName();
    return true;
}

bool SliceProcessor::loadSourceForTests (std::shared_ptr<const SourceAudio> s) { if (s == nullptr) return false; installSource (s, false); return true; }

void SliceProcessor::loadAudioAsync (const juce::File& f)
{
    loading.store (true);
    auto flag = alive;
    juce::Thread::launch ([this, f, flag]
    {
        juce::String err; auto s = source::load (f, err, looseCacheDir());
        juce::MessageManager::callAsync ([this, f, flag, s, err]
        {
            if (! flag->load()) return;
            loading.store (false);
            if (s != nullptr) { installSource (s, false); lastMessage = "Loaded " + f.getFileName(); if (onToast) onToast ("Loaded " + f.getFileName()); }
            else { lastMessage = err; if (onToast) onToast (err); }
            if (onChanged) onChanged();
        });
    });
}

// ---------------------------------------------------------------- projects
bool SliceProcessor::saveProject (const juce::File& loc, juce::String& error)
{
    if (! projectio::save (project(), loc, audioFile(), source.get(), error)) return false;
    projectLoc = loc;
    lastMessage = "Saved " + loc.getFileName();
    return true;
}

bool SliceProcessor::openProject (const juce::File& loc, juce::String& error)
{
    Project p;
    if (! projectio::load (loc, p, error)) return false;
    projectLoc = loc;
    const auto audio = projectio::resolveAudio (p, loc);
    history.reset (p);
    if (audio == juce::File())
    {
        source = nullptr; work = nullptr; relinkPending = true; rebuild (true);
        error = "relink"; lastMessage = "The audio (" + p.source.fileName + ") was not found: RELINK it"; return false;
    }
    auto s = source::load (audio, error, projectio::cacheDirOf (loc));
    if (s == nullptr) { relinkPending = true; rebuild (true); return false; }
    installSource (s, true);
    lastMessage = "Opened " + loc.getFileName() + (audio.getFullPathName() != p.source.path ? " (audio found at " + audio.getFullPathName() + ")" : juce::String());
    return true;
}

bool SliceProcessor::relinkAudio (const juce::File& f, juce::String& error)
{
    if (! projectio::acceptRelink (project(), f, error)) return false;
    auto s = source::load (f, error, projectLoc != juce::File() ? projectio::cacheDirOf (projectLoc) : looseCacheDir());
    if (s == nullptr) return false;
    installSource (s, true);
    lastMessage = "Relinked " + f.getFileName();
    return true;
}

// ---------------------------------------------------------------- export (re-checked: the editor asked the gate first)
juce::AudioBuffer<float> SliceProcessor::renderArrangement()
{
    if (work == nullptr) return {};
    return render::plan (plan, *work, &studio, true);
}
bool SliceProcessor::exportWav (const juce::File& f, juce::String& error)
{
    if (! ghosty::exportUnlocked (license)) { error = "Export is part of the full version"; return false; }
    if (work == nullptr || plan.pieces.empty()) { error = "Nothing to export"; return false; }
    const auto audio = renderArrangement();
    return exporter::writeWav (audio, work->rate, f, 24, error);
}
bool SliceProcessor::exportMidi (const juce::File& f, juce::String& error)
{
    if (! ghosty::exportUnlocked (license)) { error = "Export is part of the full version"; return false; }
    if (plan.pieces.empty()) { error = "Nothing to export"; return false; }
    return exporter::writeMidi (exporter::slicesMidi (plan, project().bpm), f, error);
}

// ---------------------------------------------------------------- transport
void SliceProcessor::play (bool on)
{
    if (on) { stopReq.store (false); if (! ownPlaying.load()) { seekReq.store (playHf.load() >= 0 ? playHf.load() : cursorHf.load()); ownPlaying.store (true); } }
    else { ownPlaying.store (false); stopReq.store (true); seekReq.store (cursorHf.load()); }
    if (onChanged) onChanged();
}

// ---------------------------------------------------------------- state (the DAW session): the model as JSON + where the audio is
void SliceProcessor::getStateInformation (juce::MemoryBlock& mb)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("ghostslice", 1);
    Project p = history.current(); if (source != nullptr) p.source.path = source->file.getFullPathName();
    o->setProperty ("project", json::toVar (p));
    o->setProperty ("projectLoc", projectLoc.getFullPathName());
    o->setProperty ("theme", uiTheme.load()); o->setProperty ("studio", useStudio);
    o->setProperty ("cursor", (juce::int64) cursorHf.load()); o->setProperty ("sync", (int) sync.getMode());
    const auto s = juce::JSON::toString (juce::var (o), true);
    mb.replaceAll (s.toRawUTF8(), s.getNumBytesAsUTF8());
}

void SliceProcessor::setStateInformation (const void* data, int size)
{
    if (! license.isUnlocked()) { pendingState.replaceAll (data, (size_t) size); return; }   // demo: a reopened project starts empty (UNLOCK brings it back)
    const auto v = juce::JSON::parse (juce::String::fromUTF8 ((const char*) data, size));
    if (! v.isObject() || ! v.hasProperty ("ghostslice")) return;
    uiTheme.store ((int) v["theme"]); useStudio = v.hasProperty ("studio") ? (bool) v["studio"] : true;
    sync.setMode ((ghosty::SyncTransport::Mode) (int) v["sync"]);
    Project p; juce::String err;
    if (! json::fromVar (v["project"], p, err)) return;
    projectLoc = juce::File (v["projectLoc"].toString().isNotEmpty() && juce::File::isAbsolutePath (v["projectLoc"].toString()) ? v["projectLoc"].toString() : juce::String());
    history.reset (p);
    cursorHf.store ((int64) (juce::int64) v["cursor"]); playHf.store (cursorHf.load());
    if (! p.hasSource()) { source = nullptr; work = nullptr; rebuild (true); return; }
    const auto audio = projectio::resolveAudio (p, projectLoc);
    if (audio == juce::File()) { source = nullptr; work = nullptr; relinkPending = true; lastMessage = "The audio (" + p.source.fileName + ") was not found: RELINK it"; rebuild (true); return; }
    loading.store (true);
    auto flag = alive;
    const auto cache = projectLoc != juce::File() ? projectio::cacheDirOf (projectLoc) : looseCacheDir();
    juce::Thread::launch ([this, flag, audio, cache, p]
    {
        juce::String e; auto s = source::load (audio, e, cache);
        juce::MessageManager::callAsync ([this, flag, s, p, e]
        {
            if (! flag->load()) return;
            loading.store (false);
            if (s == nullptr || s->fingerprint != p.source.fingerprint) { relinkPending = true; lastMessage = s == nullptr ? e : "The audio changed: RELINK it"; rebuild (true); return; }
            installSource (s, true);
        });
    });
}

void SliceProcessor::unlockedNow()
{
    if (pendingState.getSize() == 0) return;
    const auto mb = pendingState; pendingState.reset();
    setStateInformation (mb.getData(), (int) mb.getSize());
}

juce::AudioProcessorEditor* SliceProcessor::createEditor() { return new SliceEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SliceProcessor(); }
