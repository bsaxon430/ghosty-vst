#include "BeatsXProcessor.h"
#include "HostNotify.h"
#include "BeatsXEditor.h"
#include <map>
#include <mutex>

static const juce::StringArray kParamIds { "swing", "human", "glitch", "filter", "res", "echo", "verb", "crush", "drive", "glue", "volume", "songmode", "pattern", "bpm", "repeat", "repeatdiv", "groove", "clip", "room", "chaos", "density", "mutation", "fracture", "gravity", "memory", "tension", "resolve", "ceiling", "limiter" };
static const char* kTrackParamIds[7] { "vol", "pan", "pitch", "decay", "tone", "duck", "char" };
static juce::File exportDir();
static juce::String trackParamId(int t, int k) { return "t" + juce::String(t + 1) + kTrackParamIds[k]; }

static juce::AudioProcessorValueTreeState::ParameterLayout layout() {
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout L;
    auto F = [&](const char* id, const char* nm, float lo, float hi, float d) { L.add(std::make_unique<AudioParameterFloat>(ParameterID { id, 1 }, nm, NormalisableRange<float>(lo, hi), d)); };
    F("swing", "Swing", 0, 1, 0); F("human", "Human", 0, 1, 0); F("glitch", "Glitch", 0, 1, 0);
    F("filter", "DJ Filter", 0, 1, 0.5f); F("res", "Filter Reso", 0, 1, 0.25f); F("echo", "Echo", 0, 1, 0); F("verb", "Reverb", 0, 1, 0);
    F("crush", "Crush", 0, 1, 0); F("drive", "Drive", 0, 1, 0); F("glue", "Glue", 0, 1, 0.25f); F("volume", "Volume", 0, 1, 0.8f);
    L.add(std::make_unique<AudioParameterBool>(ParameterID { "songmode", 1 }, "Song Mode", false));
    L.add(std::make_unique<AudioParameterInt>(ParameterID { "pattern", 1 }, "Pattern", 1, gb::kPatterns, 1));
    F("bpm", "Tempo (no host)", 60, 200, 140);
    L.add(std::make_unique<AudioParameterBool>(ParameterID { "repeat", 1 }, "Note Repeat", false));
    L.add(std::make_unique<AudioParameterChoice>(ParameterID { "repeatdiv", 1 }, "Repeat Rate", StringArray { "1/8", "1/16", "1/32", "1/16T" }, 1));
    L.add(std::make_unique<AudioParameterChoice>(ParameterID { "groove", 1 }, "Groove", gb::grooveNames(), 0));
    F("clip", "Bus Clipper", 0, 1, 0); F("room", "Bus Room", 0, 1, 0);
    F("chaos", "Chaos", 0, 1, 0); F("density", "Density", 0, 1, 0.5f); F("mutation", "Mutation", 0, 1, 0); F("fracture", "Fracture", 0, 1, 0);
    F("gravity", "Gravity", 0, 1, 0.5f); F("memory", "Memory", 0, 1, 0.5f); F("tension", "Tension", 0, 1, 0); F("resolve", "Resolve", 0, 1, 0);
    F("ceiling", "Output Ceiling (dB)", -6, 0, -1);
    L.add(std::make_unique<AudioParameterChoice>(ParameterID { "limiter", 1 }, "Limiter", StringArray { "Clean", "Soft (v1)", "Off" }, 0));
    // per-track automation: offsets on top of the track panel (all neutral by default)
    auto num3 = [](float v, int) { return juce::String(v, 3); };   // 3 decimals: the text round-trips (JUCE's default 7 does not, in float)
    for (int t = 0; t < gb::kTracks; ++t) {
        const juce::String n = "T" + juce::String(t + 1) + " ";
        L.add(std::make_unique<AudioParameterFloat>(ParameterID { trackParamId(t, 0), 1 }, n + "Volume", NormalisableRange<float>(-24.0f, 24.0f), 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction(num3).withLabel("dB")));
        L.add(std::make_unique<AudioParameterFloat>(ParameterID { trackParamId(t, 1), 1 }, n + "Pan", NormalisableRange<float>(-1.0f, 1.0f), 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction(num3)));
        L.add(std::make_unique<AudioParameterFloat>(ParameterID { trackParamId(t, 2), 1 }, n + "Pitch", NormalisableRange<float>(-24.0f, 24.0f), 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction(num3).withLabel("st")));
        L.add(std::make_unique<AudioParameterFloat>(ParameterID { trackParamId(t, 3), 1 }, n + "Decay", NormalisableRange<float>(0.05f, 2.0f), 1.0f, AudioParameterFloatAttributes().withStringFromValueFunction(num3).withLabel("x")));
        L.add(std::make_unique<AudioParameterFloat>(ParameterID { trackParamId(t, 4), 1 }, n + "Tone", NormalisableRange<float>(-0.5f, 0.5f), 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction(num3)));
        L.add(std::make_unique<AudioParameterFloat>(ParameterID { trackParamId(t, 5), 1 }, n + "Duck", NormalisableRange<float>(0.0f, 1.0f), 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction(num3)));
        L.add(std::make_unique<AudioParameterFloat>(ParameterID { trackParamId(t, 6), 1 }, n + "Character", NormalisableRange<float>(-1.0f, 1.0f), 0.0f, AudioParameterFloatAttributes().withStringFromValueFunction(num3)));
    }
    L.add(std::make_unique<AudioParameterBool>(ParameterID { "bypass", 1 }, "Bypass", false));   // LAST: existing automation keeps its indexes
    return L;
}
juce::AudioProcessor::BusesProperties BeatsXProcessor::makeBuses() {
    auto b = BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true);
    for (int t = 0; t < gb::kTracks; ++t) b = b.withOutput("Track " + juce::String(t + 1), juce::AudioChannelSet::stereo(), false);
    return b;
}

// Per-hit destruction renders (spectral freeze + 7 band reverses) happen here, never on the message or audio thread.
struct BeatsXProcessor::VariantWorker : juce::Thread {
    BeatsXProcessor& p; juce::CriticalSection lock; std::deque<gb::SoundPtr> queue; std::vector<std::pair<gb::SoundPtr, Variants>> done;
    explicit VariantWorker(BeatsXProcessor& o) : juce::Thread("Ghosty Beats variants"), p(o) {}
    ~VariantWorker() override { stopThread(4000); }
    void add(gb::SoundPtr s) { { const juce::ScopedLock sl(lock); for (auto& q : queue) if (q == s) return; queue.push_back(std::move(s)); } notify(); }
    void run() override {
        while (!threadShouldExit()) {
            gb::SoundPtr s; { const juce::ScopedLock sl(lock); if (!queue.empty()) { s = queue.front(); queue.pop_front(); } }
            if (!s) { wait(250); continue; }
            Variants v; v.src = s; v.v[0] = gb::destruct::freeze(*s, (uint32_t) s->name.hashCode());
            if (threadShouldExit()) return;
            const auto br = gb::destruct::bandReverseAll(*s); for (int m = 1; m <= 7; ++m) v.v[(size_t) m] = br[(size_t) (m - 1)];
            { const juce::ScopedLock sl(lock); done.push_back({ s, std::move(v) }); }
            p.triggerAsyncUpdate();
        }
    }
};
void BeatsXProcessor::handleAsyncUpdate() {
    std::vector<std::pair<gb::SoundPtr, Variants>> got; { const juce::ScopedLock sl(worker->lock); got.swap(worker->done); }
    if (got.empty()) return;
    for (auto& g : got) variantCache[g.first.get()] = std::move(g.second);
    publish();
}
bool BeatsXProcessor::variantsMissing() const {
    for (int t = 0; t < gb::kTracks; ++t) { const auto& tr = model.tracks[(size_t) t]; const gb::Sound* snd = tr.sample ? tr.sample.get() : lib->byName(tr.sound).get(); if (snd && variantCache.find(snd) == variantCache.end()) return true; }
    return false;
}
void BeatsXProcessor::requestVariants(bool all) {
    bool scripts = false; for (auto& sc : model.scripts) if (sc.on) scripts = true;
    for (int t = 0; t < gb::kTracks; ++t) {
        const auto& tr = model.tracks[(size_t) t]; gb::SoundPtr sp = tr.sample ? tr.sample : lib->byName(tr.sound); if (!sp || variantCache.count(sp.get()) || variantsRequested.count(sp.get())) continue;
        bool need = all || scripts;
        for (int pi = 0; pi < gb::kPatterns && !need; ++pi) for (auto& st : model.patterns[(size_t) pi].steps[(size_t) t]) if (st.destroy || st.freeze || st.frev) { need = true; break; }
        if (!need) continue;
        variantsRequested.insert(sp.get()); if (variantsRequested.size() > 512) variantsRequested.clear();
        if (!worker) { worker = std::make_unique<VariantWorker>(*this); worker->startThread(juce::Thread::Priority::low); }
        worker->add(sp);
    }
}

BeatsXProcessor::BeatsXProcessor()
    : AudioProcessor(makeBuses()), apvts(*this, nullptr, "GHOSTYBEATS2", layout()) {
    for (int i = 0; i < kParamIds.size(); ++i) p[i] = apvts.getRawParameterValue(kParamIds[i]);
    for (int t = 0; t < gb::kTracks; ++t) for (int k = 0; k < 7; ++k) tp[t][k] = apvts.getRawParameterValue(trackParamId(t, k));
    bypassParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("bypass")); jassert(bypassParam != nullptr);
    {   // one library shared by every Ghosty Beats instance in the host (played at any host rate; voices resample; cached on disk)
        static std::mutex m; static std::weak_ptr<gb::DrumLibrary> shared;
        const std::lock_guard<std::mutex> g(m); lib = shared.lock(); if (!lib) { lib = gb::loadBeatsLibrary(48000.0); shared = lib; }
    }
    juce::PropertiesFile::Options o; o.applicationName = "Ghosty Beats"; o.folderName = "Ghosty"; o.filenameSuffix = "settings"; o.osxLibrarySubFolder = "Application Support";
    settings = std::make_unique<juce::PropertiesFile>(o);
    favourites = juce::StringArray::fromTokens(settings->getValue("favourites"), "|", ""); favourites.removeEmptyStrings();
    normaliseSamples = settings->getBoolValue("normalise", true); midiChannel = settings->getIntValue("midich", 1) == 10 ? 10 : 1;
    theme = themeFromSettings(*settings); if (ghostFromSettings(*settings)) setLook(3); focusMode = settings->getBoolValue("focus", false);
    rescanShared();
    applyKit(0, true);
}
void BeatsXProcessor::saveSettings() {
    settings->setValue("favourites", favourites.joinIntoString("|")); settings->setValue("normalise", normaliseSamples); settings->setValue("midich", midiChannel);
    settings->setValue("theme_name", ghostLook ? juce::String("ghost") : ghosty::themes::toString(theme)); settings->setValue("dark", theme != ghosty::themes::Theme::Light);   // (the old key too, for older builds)
    settings->setValue("focus", focusMode); settings->saveIfNeeded();
}
int BeatsXProcessor::rescanShared() {
    int added = 0; auto files = sharedSoundsDir().findChildFiles(juce::File::findFiles, false, "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg"); files.sort();
    for (auto& f : files) {
        if (lib->indexOf(f.getFileNameWithoutExtension()) >= 0) continue;
        juce::String err; auto s = gb::DrumLibrary::fromFile(f, 48000.0, err, true); if (!s) continue;
        auto c = std::make_shared<gb::Sound>(*s); c->category = "Shared"; lib->append(c); ++added;
    }
    return added;
}

void BeatsXProcessor::prepareToPlay(double sr, int block) {
    engine.prepare(sr, juce::jmax(64, block)); demo.prepare(sr); renderRate = sr >= 22050 && sr <= 192000 ? sr : 48000;
    bypass.prepare(sr, 0.006, bypassParam && bypassParam->get());
    sync.prepare(sr); gate.prepare(sr); gateRate = sr; gate.releaseAll(); gate.state = playMode.load() == 1 ? NoteGate::Closed : NoteGate::Open; gate.gain = 1.0f; wasHostPlaying = false; gateFresh = true;
    const int lat = gb::BeatEngine::latencyFor(readParams(0), engine.lookahead()); wantLatency.store(lat); latencyPoll.reported = lat; setLatencySamples(lat);   // known before the first block
}

gb::EngineParams BeatsXProcessor::readParams(double bpm) const {
    gb::EngineParams e;
    e.swing = p[0]->load(); e.human = p[1]->load(); e.glitch = p[2]->load(); e.filter = p[3]->load(); e.res = p[4]->load(); e.echo = p[5]->load(); e.verb = p[6]->load();
    e.crush = p[7]->load(); e.drive = p[8]->load(); e.glue = p[9]->load(); e.volume = p[10]->load(); e.songMode = p[11]->load() > 0.5f; e.pattern = (int) p[12]->load() - 1;
    e.bpm = (float) (bpm > 0 ? bpm : p[13]->load()); e.padRepeat = p[14]->load() > 0.5f; e.repeatDiv = (int) p[15]->load();
    e.groove = (int) p[16]->load(); e.clip = p[17]->load(); e.room = p[18]->load();
    e.chaos = p[19]->load(); e.density = p[20]->load(); e.mutation = p[21]->load(); e.fracture = p[22]->load();
    e.gravity = p[23]->load(); e.memory = p[24]->load(); e.tension = p[25]->load(); e.resolve = p[26]->load(); e.ceiling = p[27]->load(); e.limiter = (int) p[28]->load();
    e.stutter = stutter.load(); e.tape = tape.load(); e.recording = recording.load();
    for (int t = 0; t < gb::kTracks; ++t) { e.tVol[t] = tp[t][0]->load(); e.tPan[t] = tp[t][1]->load(); e.tPitch[t] = tp[t][2]->load(); e.tDecay[t] = tp[t][3]->load(); e.tTone[t] = tp[t][4]->load(); e.tDuck[t] = tp[t][5]->load(); e.tChar[t] = tp[t][6]->load(); }
    return e;
}

void BeatsXProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals nd;
    double bpm = 0, ppq = 0; bool playing = false;
    juce::Optional<juce::AudioPlayHead::PositionInfo> pos; if (auto* ph = getPlayHead()) pos = ph->getPosition();
    // SYNC (Shared/SyncTransport.h): DAW = exactly the old path (host transport; no host = the PLAY button + the BPM parameter);
    // OWN = the plugin's own clock / tempo / PLAY, the DAW's transport and tempo ignored. Everything downstream (pattern / song position,
    // swing, LINEAR, MELODY, scripts, stutter / echo tempo, MIDI-in recording, the playhead display) reads these three values.
    const auto T = sync.resolve(pos, buffer.getNumSamples());
    const bool own = sync.getMode() == ghosty::SyncTransport::Mode::Own;
    if (own) { bpm = T.bpm; ppq = T.ppq; playing = T.playing; if (engine.internalPlay) engine.internalPlay = false; }
    else if (pos) { if (auto b = pos->getBpm()) bpm = *b; ppq = pos->getPpqPosition().orFallback(0.0); playing = pos->getIsPlaying(); }
    hostPresent.store(pos.hasValue(), std::memory_order_relaxed);
    if (!own && bpm > 0) hostBpm.store(bpm);   // the project tempo, even while the host is stopped (used for drag-out too)
    auto P = readParams(liveBpm()); if (!playing) bpm = P.bpm;
    if (buffer.getNumChannels() < 2) { buffer.clear(); return; }
    // BYPASS: engage edge stops everything; fully bypassed = nothing runs (clock keeps time); MIDI is ignored while bypassed
    bypass.set(bypassParam && bypassParam->get());
    if (bypass.takeEngageEdge()) { engine.panic(); gate.releaseAll(); if (gate.state != NoteGate::Open) { gate.state = NoteGate::Closed; gate.gain = 1.0f; } }   // (held notes forgotten: no stuck gate after release)
    if (bypass.silent()) { buffer.clear(); midi.clear(); engine.advanceSilently(buffer.getNumSamples(), P, bpm, ppq, playing); wasHostPlaying = playing; return; }
    if (bypass.isBypassed()) midi.clear();
    // MULTI-OUT: enabled track buses get their track (post chain, pre bus); the main bus always carries the full mix
    float* aux[gb::kTracks * 2] {}; bool anyAux = false;
    for (int b = 1; b < getBusCount(false) && b <= gb::kTracks; ++b) { auto* bus = getBus(false, b); if (!bus || !bus->isEnabled()) continue; auto bb = getBusBuffer(buffer, false, b); if (bb.getNumChannels() < 2) continue; aux[(b - 1) * 2] = bb.getWritePointer(0); aux[(b - 1) * 2 + 1] = bb.getWritePointer(1); anyAux = true; }
    auto main = getBusBuffer(buffer, false, 0);
    wantLatency.store(gb::BeatEngine::latencyFor(P, engine.lookahead()));
    if (playMode.load(std::memory_order_relaxed) == 1 || gate.state != NoteGate::Open) renderGated(buffer, midi, P, bpm, ppq, playing, anyAux ? aux : nullptr);   // PLAY: ON NOTES (or settling back to ALWAYS)
    else engine.process(main, midi, P, bpm, ppq, playing, anyAux ? aux : nullptr);   // ALWAYS: exactly as before
    wasHostPlaying = playing; gateFresh = false;
    if (const int bt = engine.tempoReq.exchange(0); bt > 0 && own) sync.setOwnBpm(bt);   // tracker F20..FF: Beats' own tempo (SYNC: DAW: the DAW owns the tempo, a marker only)
    demo.process(main, license.isUnlocked());
    bypass.applyMute(buffer);   // the engage / release fade on the main bus and every enabled Track bus at once (no-op while playing)
}

// PLAY: ON NOTES. The block is cut at every MIDI event that opens / closes the gate; each piece runs the engine with the host position of its first
// sample, so the beat starts exactly at the note-on sample on the host grid. Closing fades every bus over ~6 ms, then cuts all voices + tails.
void BeatsXProcessor::renderSegment(juce::AudioBuffer<float>& buffer, int from, int to, const gb::EngineParams& P, double bpm, double ppq, bool playing, float* const* aux) {
    const int len = to - from; if (len <= 0) return;
    const double spb = gateRate * 60.0 / juce::jmax(20.0, playing && bpm > 0 ? bpm : (double) P.bpm);
    float* mainCh[2] = { buffer.getWritePointer(0) + from, buffer.getWritePointer(1) + from };
    juce::AudioBuffer<float> sub(mainCh, 2, len);   // refers to the block (no allocation for 2 channels)
    float* subAux[gb::kTracks * 2] {}; if (aux) for (int c = 0; c < gb::kTracks * 2; ++c) subAux[c] = aux[c] ? aux[c] + from : nullptr;
    engine.patternGate = gate.state != NoteGate::Closed;
    engine.process(sub, noMidi, P, bpm, ppq + (playing ? from / spb : 0.0), playing, aux ? subAux : nullptr);
    if (gate.state == NoteGate::Closed || (gate.state == NoteGate::Open && gate.gain >= 1.0f)) return;
    // a fade (out while Fading, back in while Open with gain < 1), on the main bus and every enabled Track bus
    const int nch = buffer.getNumChannels();
    for (int i = 0; i < len; ++i) {
        gate.gain = gate.state == NoteGate::Fading ? gate.gain - gate.step : juce::jmin(1.0f, gate.gain + gate.step);
        if (gate.gain <= 0.0f) {   // faded out: silence from here, cut every voice and tail, the pattern stops
            for (int c = 0; c < nch; ++c) juce::FloatVectorOperations::clear(buffer.getWritePointer(c) + from + i, len - i);
            engine.panic(); gate.state = NoteGate::Closed; gate.gain = 1.0f; return;
        }
        for (int c = 0; c < nch; ++c) buffer.getWritePointer(c)[from + i] *= gate.gain;
    }
}
void BeatsXProcessor::renderGated(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, const gb::EngineParams& P, double bpm, double ppq, bool playing, float* const* aux) {
    const bool onNotes = playMode.load(std::memory_order_relaxed) == 1;
    auto setOpen = [&](bool open) {
        if (open) { if (gate.state == NoteGate::Closed) gate.gain = 1.0f; gate.state = NoteGate::Open; }   // from silence: no fade-in (the first hit lands whole)
        else if (gate.state == NoteGate::Open) gate.state = NoteGate::Fading;
    };
    if (!onNotes) { gate.releaseAll(); setOpen(true); }   // back to ALWAYS
    else if (wasHostPlaying && !playing) gate.releaseAll();   // the host stopped: forget held notes (a host that sends no note-offs can't leave it stuck)
    if (onNotes && gateFresh && gate.total == 0 && gate.state == NoteGate::Open) gate.state = NoteGate::Closed;   // ON NOTES from the first block: nothing to fade
    if (onNotes) setOpen(gate.total > 0);
    int pos = 0;
    if (onNotes) for (const auto md : midi) {
        if (!gate.feed(md.getMessage())) continue;
        const int at = juce::jlimit(pos, buffer.getNumSamples(), md.samplePosition);
        renderSegment(buffer, pos, at, P, bpm, ppq, playing, aux); pos = at; setOpen(gate.total > 0);
    }
    renderSegment(buffer, pos, buffer.getNumSamples(), P, bpm, ppq, playing, aux);
    engine.patternGate = true;
}

void BeatsXProcessor::publish() {
    auto s = std::make_unique<gb::Snapshot>();
    s->tracks = model.tracks; s->patterns = model.patterns; s->ver = model.ver;
    for (auto& tr : s->tracks) { const gb::Sound* snd = tr.sample ? tr.sample.get() : lib->byName(tr.sound).get(); if (snd) for (int v : tr.slices) if (v < 0 || v >= (int) snd->data.size()) { tr.slices.clear(); break; } }
    s->songCount = (int) std::min<size_t>(model.song.size(), s->song.size());
    for (int i = 0; i < s->songCount; ++i) s->song[(size_t) i] = model.song[(size_t) i];
    for (int t = 0; t < gb::kTracks; ++t) {
        const auto& tr = model.tracks[(size_t) t];
        gb::SoundPtr sp = tr.sample ? tr.sample : lib->byName(tr.sound);
        if (sp) keepSounds.push_back(sp);
        s->sound[(size_t) t] = sp.get(); if (tr.solo) s->anySolo = true;
        for (int l = 0; l < gb::kLayers; ++l) { const auto& L = tr.layers[(size_t) l]; gb::SoundPtr ls = L.sound.isNotEmpty() ? lib->byName(L.sound) : nullptr; if (ls) keepSounds.push_back(ls); s->layer[(size_t) t][(size_t) l] = ls.get(); }
    }
    s->scTrack = model.scTrack; s->role = roles(); s->arc = model.arc; s->converge = model.converge; s->locks = model.locks; s->motifs = model.motifs;
    s->linear = model.linear; s->serial = ++contentSerial;
    gb::mel::migrate(melody, model.tracks);   // (an instrument set up the old way, by track index, now owns that track's sound)
    { auto song = std::make_shared<gb::mel::Song>(melody); song->prepare(); s->melody = song; }
    for (int i = 0; i < gb::mel::kInstruments; ++i) {   // each instrument's own sound (a kit change never touches it) + its mip levels (made here, once per sound)
        const auto& in = melody.inst[(size_t) i]; gb::SoundPtr sp = in.sample ? in.sample : (in.sound.isNotEmpty() ? lib->byName(in.sound) : nullptr);
        if (!sp || !in.used()) continue; keepSounds.push_back(sp); s->melSound[(size_t) i] = sp.get();
        auto& m = melMips[sp.get()]; if (!m.mips) { m.src = sp; m.mips = gb::mel::Mips::make(*sp); } s->melMips[(size_t) i] = m.mips.get();
    }
    if (melMips.size() > 64) for (auto it = melMips.begin(); it != melMips.end();) { bool live = false; for (auto& in : melody.inst) if ((in.sample ? in.sample.get() : lib->byName(in.sound).get()) == it->first) live = true; if (!live) { keepMips.push_back(it->second.mips); it = melMips.erase(it); } else ++it; }
    // per-hit destruction sounds (spectral freeze + band reverse): rendered by the worker when a step / script / FRACTURE needs them
    if (variantCache.size() > 256) { variantCache.clear(); variantsRequested.clear(); }   // (snapshots in flight keep their pointers alive through keepSounds)
    for (int t = 0; t < gb::kTracks; ++t) if (const auto* snd = s->sound[(size_t) t]) {
        auto it = variantCache.find(snd); if (it == variantCache.end()) continue;
        for (int m = 0; m < 8; ++m) { s->variant[(size_t) t][(size_t) m] = it->second.v[(size_t) m].get(); if (it->second.v[(size_t) m]) keepSounds.push_back(it->second.v[(size_t) m]); }
    }
    requestVariants(p[22]->load() > 0.001f);
    // SCRIPTS: compiled once per text, run on the audio thread
    for (auto& sc : model.scripts) if (sc.on && s->nProgs < gb::kScripts) { auto prog = compiledScript(sc.code); if (prog && prog->ok() && !prog->code.empty()) s->progs[(size_t) s->nProgs++] = prog; }
    if (s->scTrack < 0) for (int t = 0; t < gb::kTracks; ++t) if (!model.tracks[(size_t) t].sample && categoryOf(model.tracks[(size_t) t].sound) == "Kick") { s->scTrack = t; break; }
    if (s->linear.on) { const auto P = readParams(liveBpm()); s->linPerf = gb::linear::build(*s, P, P.bpm, s->linear, evolveBars); keepPerf(s->linPerf); }
    if (!snaps.empty()) s->linPerfPrev = snaps.back()->linPerf;   // LINEAR: resolved here, never on the audio thread
    { const double now = juce::Time::getMillisecondCounterHiRes(); for (auto* q : s->sound) if (q) lastSeen[q] = now; for (auto& l : s->layer) for (auto* q : l) if (q) lastSeen[q] = now; for (auto* q : s->melSound) if (q) lastSeen[q] = now; for (auto* q : s->melMips) if (q) lastSeen[q] = now; }
    engine.setSnapshot(s.get());
    snaps.push_back(std::move(s));
    // retire snapshots the audio thread has moved past (it reads one per block; everything older than the one it picked up is free)
    if (snaps.size() > 8) { const auto* seen = engine.snapshotInUse(); size_t idx = 0; bool found = false;
        for (size_t i = 0; i < snaps.size(); ++i) if (snaps[i].get() == seen) { idx = i; found = true; }
        const size_t keepFrom = found ? idx : (snaps.size() > 2 ? snaps.size() - 2 : 0);
        if (keepFrom > 0) snaps.erase(snaps.begin(), snaps.begin() + (long) keepFrom); }
    if (keepSounds.size() > 4096) { std::sort(keepSounds.begin(), keepSounds.end()); keepSounds.erase(std::unique(keepSounds.begin(), keepSounds.end()), keepSounds.end()); }
}

void BeatsXProcessor::applyKit(int k, bool patternsToo) {
    currentKit = juce::jlimit(0, (int) gb::allKits().size() - 1, k);
    gb::applyKit(model, gb::allKits()[(size_t) currentKit], patternsToo, [this](const juce::String& n) { return categoryOf(n); });
    if (patternsToo) if (auto* b = apvts.getParameter("bpm")) b->setValueNotifyingHost(b->convertTo0to1((float) gb::allKits()[(size_t) currentKit].bpm));
    publish();
}

bool BeatsXProcessor::loadSampleToTrack(int t, const juce::File& f, juce::String& err) {
    auto s = gb::DrumLibrary::fromFile(f, 48000.0, err, normaliseSamples);   // trims silence; normalises unless switched off
    if (!s || !juce::isPositiveAndBelow(t, gb::kTracks)) { if (err.isEmpty()) err = "No sound in that file."; return false; }
    { auto& tr = model.tracks[(size_t) t]; tr.sample = s; tr.sound = s->name; tr.regStart = tr.loopStart = 0; tr.regEnd = tr.loopEnd = 1; tr.loopOn = false; } publish(); return true;   // (a new sound: the whole of it)
}
int BeatsXProcessor::loadSamples(int t, const juce::StringArray& files, juce::String& msg) {
    int n = 0; juce::String err, last;
    for (auto& f : files) { if (!juce::isPositiveAndBelow(t, gb::kTracks)) break; if (loadSampleToTrack(t, juce::File(f), err)) { ++n; last = juce::File(f).getFileNameWithoutExtension(); ++t; } }
    msg = n == 0 ? (err.isEmpty() ? juce::String("No sound in that file.") : err) : (n == 1 ? "Loaded \"" + last + "\"." : "Loaded " + juce::String(n) + " samples onto consecutive tracks.");
    return n;
}
int BeatsXProcessor::firstFreeTrack() const {
    for (int t = 0; t < gb::kTracks; ++t) if (model.tracks[(size_t) t].sound.isEmpty()) return t;
    for (int t = 0; t < gb::kTracks; ++t) { bool used = false; for (auto& p : model.patterns) for (auto& st : p.steps[(size_t) t]) if (st.vel) used = true; if (!used) return t; }
    return -1;
}
void BeatsXProcessor::setTrackSound(int t, const juce::String& name) {
    if (!juce::isPositiveAndBelow(t, gb::kTracks)) return;
    auto& tr = model.tracks[(size_t) t]; tr.sample = nullptr; tr.sound = name; tr.slices.clear(); tr.regStart = tr.loopStart = 0; tr.regEnd = tr.loopEnd = 1; tr.loopOn = false;
    if (categoryOf(name) == "Shared") { auto s = lib->byName(name); if (s) { auto c = std::make_shared<gb::Sound>(*s); c->category = "Sample"; tr.sample = c; } }   // kept in the project (the shared file may move)
    else if (categoryOf(name) == "Break") { chopTrack(t, 0.5f, true); return; }   // a break: slice it and lay its rhythm on the track
    if (tr.sample && tr.sample->data.size() > tr.sample->rate * 1.5) { chopTrack(t, 0.5f, true); return; }   // a long shared loop = a break
    publish();
}
std::array<uint8_t, gb::kTracks> BeatsXProcessor::roles() const {
    std::array<uint8_t, gb::kTracks> r {};
    for (int t = 0; t < gb::kTracks; ++t) { const auto& tr = model.tracks[(size_t) t]; r[(size_t) t] = !tr.slices.empty() ? gb::RoleBreak : gb::roleOf(tr.sample ? juce::String("Sample") : categoryOf(tr.sound), tr.sound); }
    return r;
}
// a part of a track's sound as a WAV (the region editor's WAV grip, a slice drag-out); the demo gate decides
juce::File BeatsXProcessor::exportSoundPart(int t, float a, float b, const juce::String& what) {
    if (!juce::isPositiveAndBelow(t, gb::kTracks) || !unlockedNow()) return {};   // back-stop (the UI asked exportGate())
    const auto& tr = model.tracks[(size_t) t]; const auto snd = tr.sample ? tr.sample : lib->byName(tr.sound); if (!snd || snd->data.empty()) return {};
    const int n = (int) snd->data.size(), i0 = juce::jlimit(0, n - 1, (int) (juce::jmin(a, b) * n)), i1 = juce::jlimit(i0 + 1, n, (int) std::ceil(juce::jmax(a, b) * n));
    const int ch = snd->stereo() ? 2 : 1; juce::AudioBuffer<float> buf(ch, i1 - i0);
    for (int c = 0; c < ch; ++c) buf.copyFrom(c, 0, (c == 0 ? snd->data : snd->right).data() + i0, i1 - i0);
    const int fade = juce::jmin((i1 - i0) / 4, (int) (snd->rate * 0.002)); for (int c = 0; c < ch; ++c) { buf.applyGainRamp(c, 0, fade, 0.0f, 1.0f); buf.applyGainRamp(c, i1 - i0 - fade, fade, 1.0f, 0.0f); }   // click-free ends
    auto f = exportDir().getNonexistentChildFile(juce::File::createLegalFileName(tr.sound + " " + what), ".wav", true);
    juce::WavAudioFormat wav; if (auto os = std::unique_ptr<juce::OutputStream>(f.createOutputStream())) if (auto* w = wav.createWriterFor(os.get(), snd->rate, (unsigned) ch, 24, {}, 0)) { os.release(); std::unique_ptr<juce::AudioFormatWriter> wr(w); wr->writeFromAudioSampleBuffer(buf, 0, buf.getNumSamples()); return f; }   // GATE: via-caller (beats.region-wav-drag / beats.slice-wav-drag)
    return {};
}
juce::String BeatsXProcessor::importTracker(const juce::File& f, bool& ok) {
    ok = false; juce::MemoryBlock mb; if (f.getSize() > (juce::int64) 256 << 20 || !f.loadFileAsData(mb) || mb.getSize() < 4) return "Can't read that file.";
    gb::mel::files::Report rep; gb::mel::Song s = melody; const bool it = std::memcmp(mb.getData(), "IMPM", 4) == 0;
    ok = it ? gb::mel::files::readIT(mb.getData(), mb.getSize(), s, rep) : gb::mel::files::readXM(mb.getData(), mb.getSize(), s, rep);
    if (!ok) return rep.error.isNotEmpty() ? rep.error : juce::String("That isn't an XM or IT file.");
    for (auto& in : s.inst) if (in.hasSound() && in.route < 0) in.route = 0;
    melody = s; publish();
    juce::String m = "Imported " + f.getFileName() + ": " + juce::String(rep.patterns) + " patterns, " + juce::String(rep.samples) + " samples, speed " + juce::String(rep.speed) + ", " + juce::String(rep.bpm) + " BPM (as F" + juce::String::toHexString(juce::jlimit(32, 255, rep.bpm)).toUpperCase() + " on the first row).";
    if (rep.droppedChannels) m << " Channels beyond 8 were left out."; if (rep.truncatedRows) m << " Patterns longer than 128 rows were cut."; if (rep.droppedFx) m << " " << rep.droppedFx << " unsupported effects were skipped.";
    if (rep.error.isNotEmpty()) m << " " << rep.error;
    return m;
}
bool BeatsXProcessor::exportTracker(const juce::File& dest, bool it, juce::String& msg) {
    if (!unlockedNow()) { msg = "Exporting is part of the full version (UNLOCK)."; return false; }   // back-stop (the UI asked exportGate())
    gb::mel::files::Report rep; auto soundOf = [this](const gb::mel::Instrument& in) -> gb::SoundPtr { return in.sample ? in.sample : lib->byName(in.sound); };
    const auto data = it ? gb::mel::files::writeIT(melody, soundOf, "Ghosty Beats melody", readParams(liveBpm()).bpm, rep) : gb::mel::files::writeXM(melody, soundOf, "Ghosty Beats melody", readParams(liveBpm()).bpm, rep);
    if (!dest.replaceWithData(data.getData(), data.getSize())) { msg = "Couldn't write " + dest.getFileName() + "."; return false; }   // GATE: via-caller (beats.export-xm / beats.export-it)
    msg = "Saved " + dest.getFileName() + ": " + juce::String(rep.patterns) + " patterns, " + juce::String(rep.samples) + " samples." + (rep.droppedFx ? " " + juce::String(rep.droppedFx) + " effects the format has no equivalent for were left out." : juce::String());
    return true;
}
juce::String BeatsXProcessor::applyRecreate(const gb::a2m::Loop& loop, const gb::a2m::Result& r, const gb::a2m::WriteOpts& w) {
    namespace A = gb::a2m;
    if (!r.ok) return "Couldn't analyse that file.";
    auto& P = model.patterns[(size_t) currentPattern()];
    const auto map = A::mapRoles(model.tracks, [this](const juce::String& n) { return categoryOf(n); });
    const juce::String head = "Found " + juce::String(r.bars) + (r.bars == 1 ? " bar, " : " bars, ") + juce::String(r.bpm, std::abs(r.bpm - std::round(r.bpm)) < 0.05 ? 0 : 1) + " BPM";
    if (!r.pitched) {
        const int n = A::writeDrums(P, model.tracks, r, map, w);
        juce::StringArray parts; for (int k = 0; k < A::kNumRoles; ++k) if (r.perRole[(size_t) k] > 0 && ((w.roles >> k) & 1u)) parts.add(juce::String(r.perRole[(size_t) k]) + " " + juce::String(A::roleName(k)).toLowerCase());
        if (w.useHits) { const auto snd = A::hitSounds(loop, r); for (int k = 0; k < A::kNumRoles; ++k) { const int t = map[(size_t) k]; if (t < 0 || !snd[(size_t) k] || model.tracks[(size_t) t].lock || !((w.roles >> k) & 1u)) continue;
                             bool other = false; for (int q = 0; q < A::kNumRoles; ++q) if (q != k && map[(size_t) q] == t && ((w.roles >> q) & 1u) && snd[(size_t) q] && r.perRole[(size_t) q] > r.perRole[(size_t) k]) other = true;
                             if (other) continue;   // (two drums on one track: the one with more hits gives the sound)
                             auto& tr = model.tracks[(size_t) t]; tr.sample = snd[(size_t) k]; tr.sound = snd[(size_t) k]->name; tr.slices.clear(); } }
        publish();
        if (n == 0) return r.onsets == 0 ? juce::String("No hits found in that file (silence?).") : head + ", but no hit fits this kit's tracks.";
        return head + ", " + juce::String(n) + " hits (" + parts.joinIntoString(", ") + ")" + (r.swing > 52 ? ", swing " + juce::String((int) std::lround(r.swing)) + "%" : juce::String()) + ". UNDO to go back.";
    }
    // PITCHED: onto the MELODY tracker of this pattern, channel 1, instrument 1
    auto& M = melody.pat(currentPattern()); const int n = A::writeNotes(M, r, 1);
    auto& in = melody.inst[0];
    int route = -1; for (int t = 0; t < gb::kTracks && route < 0; ++t) if (gb::roleOf(categoryOf(model.tracks[(size_t) t].sound), model.tracks[(size_t) t].sound) == gb::Role808) route = t;
    if (route < 0) route = juce::jmax(0, map[A::Kick]);
    auto rootOf = [](const gb::Sound& s) { float ap = 1; const int len = (int) juce::jmin(s.data.size(), (size_t) (s.rate * 0.3)); const float f0 = len > 64 ? A::yin(s.data.data() + juce::jmin(len / 4, (int) (s.rate * 0.03)), len - juce::jmin(len / 4, (int) (s.rate * 0.03)), s.rate, ap) : 0.0f; return f0 > 0 && ap < 0.35f ? juce::jlimit(0, 127, A::midiOf(f0)) : 36; };
    if (w.useHits && !r.notes.empty()) {   // the loop's own longest note becomes the instrument
        const auto* best = &r.notes.front(); for (auto& nt : r.notes) if (nt.len > best->len) best = &nt;
        const int a = (int) (best->sec * loop.sr), z = juce::jmin((int) loop.x.size(), (int) ((best->sec + best->len) * loop.sr));
        if (z - a > 64) { auto s = std::make_shared<gb::Sound>(); s->rate = loop.sr; s->category = "Sample"; s->name = loop.name + " note"; s->data.assign(loop.x.begin() + a, loop.x.begin() + z);
                          float mx = 1e-6f; for (auto v : s->data) mx = juce::jmax(mx, std::abs(v)); for (auto& v : s->data) v *= 0.89f / mx;
                          in.sample = s; in.sound = {}; in.slices.clear(); in.slice = 0; in.root = best->midi; in.route = route; }
    } else if (!in.hasSound()) {   // a bass sound from the library, its root measured
        gb::SoundPtr pick; for (auto& s : lib->sounds()) if (s->category == "808") { pick = s; break; }
        if (!pick) pick = lib->byName(model.tracks[(size_t) route].sound);
        if (pick) { in.sound = pick->name; in.sample = nullptr; in.root = rootOf(*pick); }
        in.route = route;
    } else if (in.route < 0) in.route = route;
    melody.on = true; melody.lpb = 4;
    const double needBeats = r.bars * 4.0; if (P.beats() < needBeats - 1e-9) P.length = juce::jlimit(1, gb::kMaxSteps, (int) std::lround(needBeats / gb::stepBeats(P.res)));
    publish();
    if (n == 0) return r.onsets == 0 ? juce::String("No notes found in that file (silence?).") : head + ", but no clear pitch was found.";
    return head + ", " + juce::String(n) + " notes onto the MELODY tracker (channel 1). UNDO to go back.";
}
bool BeatsXProcessor::chopTrack(int t, float sens, bool writeRhythm) {
    if (!juce::isPositiveAndBelow(t, gb::kTracks)) return false;
    auto& tr = model.tracks[(size_t) t]; const auto snd = tr.sample ? tr.sample : lib->byName(tr.sound); if (!snd) return false;
    tr.slices = gb::rhythm::chop(*snd, sens); tr.chopSens = sens;
    if (writeRhythm) {
        double beats = snd->beats;
        if (beats <= 0) { const double bpm = readParams(liveBpm()).bpm; const double sec = snd->data.size() / snd->rate; beats = juce::jlimit(4.0, 16.0, std::round(sec * bpm / 240.0) * 4.0); }
        auto& pat = model.patterns[(size_t) currentPattern()];   // laid on the pattern's own grid
        gb::rhythm::extractRhythm(pat, t, *snd, tr.slices, beats);
        tr.decay = 1; tr.choke = 0;
    }
    publish(); return true;
}
std::vector<uint8_t> BeatsXProcessor::sliceRolesOf(int t) const {
    const auto& tr = model.tracks[(size_t) t]; const auto snd = tr.sample ? tr.sample : lib->byName(tr.sound);
    return snd && !tr.slices.empty() ? gb::rhythm::sliceRoles(*snd, tr.slices) : std::vector<uint8_t>();
}
std::shared_ptr<const gb::script::Program> BeatsXProcessor::compiledScript(const juce::String& code) {
    auto it = scriptCache.find(code); if (it != scriptCache.end()) return it->second;
    if (scriptCache.size() > 64) scriptCache.clear();
    std::shared_ptr<const gb::script::Program> p = gb::script::Compiler::compile(code); scriptCache[code] = p; return p;
}
// steps a phrase lock or a motif protects inside pattern p (bar index inside the pattern)
bool BeatsXProcessor::protectedStep(const gb::Pattern& p, int t, int s) const {
    const double beat = s * gb::stepBeats(p.res); const long bar = (long) std::floor(beat / 4.0 + 1e-9);
    for (auto& L : model.locks) if (L.covers(bar, t)) return true;
    const double q = std::fmod(beat, 4.0) / 0.25; const int pos = (int) std::lround(q);
    if (std::abs(q - pos) < 1e-6) for (auto& m : model.motifs) if (m.active() && m.track == t && pos >= m.start && pos < m.start + m.len) return true;
    return false;
}
void BeatsXProcessor::markProtected(gb::Pattern& p) const { for (int t = 0; t < gb::kTracks; ++t) for (int s = 0; s < gb::kMaxSteps; ++s) { auto& st = p.steps[(size_t) t][(size_t) s]; if (!st.locked() && protectedStep(p, t, s)) st.flags |= (uint8_t) (2 | 8); } }
void BeatsXProcessor::unmarkProtected(gb::Pattern& p) const {
    for (auto& row : p.steps) for (auto& st : row) if (st.flags & 8) st.flags &= (uint8_t) ~(2 | 8);
    applyMotifs(p);
}
void BeatsXProcessor::applyMotifs(gb::Pattern& p) const {
    for (auto& m : model.motifs) if (m.active()) for (int s = 0; s < p.lenOf(m.track); ++s) {
        const double q = std::fmod(s * gb::stepBeats(p.res), 4.0) / 0.25; const int pos = (int) std::lround(q);
        if (std::abs(q - pos) < 1e-6 && pos >= m.start && pos < m.start + m.len) p.steps[(size_t) m.track][(size_t) s] = m.steps[(size_t) (pos - m.start)];
    }
}
bool BeatsXProcessor::captureMotif(int t, int start, int len) {
    const auto& p = model.patterns[(size_t) currentPattern()]; if (p.res != 1 || !juce::isPositiveAndBelow(t, gb::kTracks)) return false;
    start = juce::jlimit(0, 15, start); len = juce::jlimit(1, 16 - start, len);
    for (auto& m : model.motifs) if (!m.active() || m.track == t) { m.track = t; m.start = start; m.len = len; for (int k = 0; k < len; ++k) m.steps[(size_t) k] = p.steps[(size_t) t][(size_t) (start + k)]; publish(); return true; }
    return false;
}
template <class F> static void editProtected(BeatsXProcessor& pr, gb::Pattern& p, F&& fn) { pr.markProtected(p); fn(); pr.unmarkProtected(p); }

void BeatsXProcessor::rearrangeTrack(int t) { gb::rhythm::rearrange(model.patterns[(size_t) currentPattern()], t, sliceRolesOf(t), ++model.toolSeed); publish(); }
void BeatsXProcessor::generatePattern(int style) {
    auto& p = model.patterns[(size_t) currentPattern()];
    const int grit = style == gb::drill::kStyle ? model.drillGrit : 0;
    const auto r0 = roles(); auto kinds = gb::latin::kindsOf(model, [this](const juce::String& n) { return categoryOf(n); });
    for (int t = 0; t < gb::kTracks; ++t) if (r0[(size_t) t] == gb::RoleBreak) kinds[(size_t) t] = gb::latin::KNone;   // (a chopped break keeps its own rhythm)
    auto tkinds = gb::trapc::kindsOf(model, [this](const juce::String& n) { return categoryOf(n); });
    for (int t = 0; t < gb::kTracks; ++t) if (r0[(size_t) t] == gb::RoleBreak) tkinds[(size_t) t] = gb::trapc::KNone;
    editProtected(*this, p, [&] { gb::rhythm::generate(p, r0, lockedTracks(), style, ++model.toolSeed, grit, style == gb::latin::kStyle ? &kinds : (style == gb::trapc::kStyle ? &tkinds : nullptr)); });
    if (style == gb::latin::kStyle) if (auto* b = apvts.getParameter("bpm")) b->setValueNotifyingHost(b->convertTo0to1((float) gb::latin::tempoFor(model.toolSeed)));   // REGGAETON: 88-100 BPM
    if (style == gb::trapc::kStyle) {   // TRAP CLASSIC: 140-160 BPM (felt in half time) and the 808 ducked a little under the kick (as DRILL)
        if (auto* b = apvts.getParameter("bpm")) b->setValueNotifyingHost(b->convertTo0to1((float) gb::trapc::tempoFor(model.toolSeed)));
        const auto r = roles(); bool kick = false; for (auto v : r) kick = kick || v == gb::RoleKick;
        if (kick) for (int t = 0; t < gb::kTracks; ++t) { auto& tr = model.tracks[(size_t) t]; if (r[(size_t) t] == gb::Role808 && !tr.lock && tr.duck < 0.01f) tr.duck = 0.4f; }
    }
    if (style == gb::drill::kStyle) {   // DRILL GRIT 50+: the DRILL pack's 808 / snare become their DIRTY versions (and back below 50); other sounds are the user's
        static const std::pair<const char*, const char*> swaps[] { { "DRILL 808 SLIDE", "DRILL 808 DIRTY" }, { "DRILL 808 LONG", "DRILL 808 DIRTY" }, { "DRILL SNARE", "DRILL SNARE DIRTY" } };
        for (int t = 0; t < gb::kTracks; ++t) { auto& tr = model.tracks[(size_t) t]; if (tr.lock || tr.sample) continue;
            for (auto& sw : swaps) { if (grit >= 50 && tr.sound == sw.first) { tr.sound = sw.second; break; }
                                     if (grit < 50 && tr.sound == sw.second && juce::String(sw.first) != "DRILL 808 LONG") { tr.sound = sw.first; break; } } }
    }
    if (style == gb::drill::kStyle) {   // DRILL: its tempo lane (138-146, felt in half time) and the light sidechain-style duck of the 808 under the kick (3-6 dB)
        if (auto* b = apvts.getParameter("bpm")) b->setValueNotifyingHost(b->convertTo0to1((float) gb::drill::tempoFor(model.toolSeed)));
        const auto r = roles(); bool kick = false; for (auto v : r) kick = kick || v == gb::RoleKick;
        if (kick) for (int t = 0; t < gb::kTracks; ++t) { auto& tr = model.tracks[(size_t) t]; if (r[(size_t) t] == gb::Role808 && !tr.lock && tr.duck < 0.01f) tr.duck = 0.4f; }
    }
    publish();
}
void BeatsXProcessor::keepIdentity(float keep) { auto& p = model.patterns[(size_t) currentPattern()]; editProtected(*this, p, [&] { p = gb::rhythm::keepIdentity(p, roles(), lockedTracks(), keep, ++model.toolSeed); }); publish(); }
void BeatsXProcessor::blendPatterns(int a, int b, float w) {
    const auto A = model.patterns[(size_t) juce::jlimit(0, gb::kPatterns - 1, a)], B = model.patterns[(size_t) juce::jlimit(0, gb::kPatterns - 1, b)];
    auto& p = model.patterns[(size_t) currentPattern()];
    editProtected(*this, p, [&] { auto a2 = p; a2.steps = A.steps; a2.trackLen = A.trackLen; a2.length = A.length; a2.res = A.res;   // A's hits, the current pattern's locks
                                  for (int t = 0; t < gb::kTracks; ++t) for (int k = 0; k < gb::kMaxSteps; ++k) if (p.steps[(size_t) t][(size_t) k].flags & 8) a2.steps[(size_t) t][(size_t) k] = p.steps[(size_t) t][(size_t) k];
                                  p = gb::rhythm::blend(a2, B, roles(), lockedTracks(), w, ++model.toolSeed); });
    publish();
}
void BeatsXProcessor::breedNext(int count) {
    std::vector<gb::Pattern> starred; for (size_t i = 0; i < breedPool.size(); ++i) if (i < breedStar.size() && breedStar[i]) starred.push_back(breedPool[i]);
    if (breedPool.empty()) breedGeneration = 0; else ++breedGeneration;
    auto cur = model.patterns[(size_t) currentPattern()]; markProtected(cur); for (auto& sp : starred) markProtected(sp);
    breedPool = gb::rhythm::breed(cur, starred, roles(), lockedTracks(), count, breedGeneration, model.toolSeed);
    for (auto& c : breedPool) {   // phrase locks + motifs hold in every tile
        for (int t = 0; t < gb::kTracks; ++t) for (int k = 0; k < gb::kMaxSteps; ++k) if (cur.steps[(size_t) t][(size_t) k].flags & 8) c.steps[(size_t) t][(size_t) k] = cur.steps[(size_t) t][(size_t) k];
        unmarkProtected(c);
    }
    breedStar.assign(breedPool.size(), false);
}
void BeatsXProcessor::useBreed(int i) { if (!juce::isPositiveAndBelow(i, (int) breedPool.size())) return; pushUndo(); auto& p = model.patterns[(size_t) currentPattern()]; p = breedPool[(size_t) i]; unmarkProtected(p); publish(); }
int BeatsXProcessor::exportBars(bool song) const {
    if (song && !model.song.empty()) return songBars();
    const auto& p = model.patterns[(size_t) currentPattern()];
    bool evolving = p.evolve > 0.001f || p.fillEvery >= 2; for (int i = 19; i <= 26 && !evolving; ++i) evolving = std::abs(p_default(i) - this->p[i]->load()) > 0.001f;
    bool rules = false; for (auto& tr : model.tracks) for (auto& r : tr.rules) if (r.type) rules = true;
    double melBeats = melody.pat(currentPattern()).rows * melody.rowBeats(); if (melody.ownOrder) { gb::mel::Song c = melody; c.prepare(); melBeats = c.orderBeats(); }
    const int melBars = melody.on ? juce::jmax(1, (int) std::ceil(melBeats / 4.0 - 1e-9)) : 1;   // the melody's own length (its pattern, or one pass of its order)
    return juce::jmax(melBars, evolving || rules ? juce::jmax(p.loopBars(), evolveBars) : p.loopBars());
}
float BeatsXProcessor::p_default(int i) { return i == 20 || i == 23 || i == 24 ? 0.5f : 0.0f; }
int BeatsXProcessor::bakePerformance(int bars) {
    // render what EVOLVE / rules / macros would play bar by bar into real patterns (after the current one) + song blocks
    if (snaps.empty()) return 0;
    const int src = currentPattern(); const auto& S = *snaps.back(); const auto P = readParams(liveBpm());
    const auto& sp = model.patterns[(size_t) src]; const double sb = gb::stepBeats(sp.res);
    const int perPat = juce::jmax(1, (int) std::floor(gb::kMaxSteps * sb / 4.0 + 1e-9)), stepsPerBar = (int) std::lround(4.0 / sb);
    const int maxPats = gb::kPatterns - 1 - src; if (maxPats <= 0) return 0;
    bars = juce::jmin(bars, maxPats * perPat);
    auto Pm = P; Pm.songMode = false; Pm.pattern = src;
    gb::EventCtx C(S, Pm, sp, 0.0, P.bpm);
    model.song.clear(); int made = 0;
    for (int b0 = 0; b0 < bars; b0 += perPat) {
        const int dst = src + 1 + made++; auto& d = model.patterns[(size_t) dst]; d = gb::Pattern(); d.res = sp.res;
        const int nb = juce::jmin(perPat, bars - b0); d.length = nb * stepsPerBar; d.seed = sp.seed;
        for (int t = 0; t < gb::kTracks; ++t) for (int k = 0; k < d.length; ++k) {
            const long s = (long) b0 * stepsPerBar + k; auto st = C.evolved(t, s);   // probability + rules stay live (same seeds -> same result)
            st.flags |= 2;   // every baked step is locked, so EVOLVE / macros don't vary it a second time
            d.steps[(size_t) t][(size_t) k] = st;
        }
        model.song.push_back({ dst, b0, nb });
    }
    if (auto* sm = apvts.getParameter("songmode")) sm->setValueNotifyingHost(1.0f);
    publish(); return made;
}

void BeatsXProcessor::drainRecorded() {
    auto& f = engine.recFifo; const int k = f.getNumReady(); if (!k) return;
    int a, b, c, d; f.prepareToRead(k, a, b, c, d);
    pushUndo();
    auto put = [&](const gb::RecEvent& e) { auto& st = model.patterns[(size_t) e.pattern].steps[(size_t) e.track][(size_t) juce::jlimit(0, gb::kMaxSteps - 1, e.step)]; st = gb::Step(); st.vel = e.vel; };
    for (int i = 0; i < b; ++i) put(engine.recBuf[a + i]); for (int i = 0; i < d; ++i) put(engine.recBuf[c + i]);
    f.finishedRead(b + d); publish();
}

juce::File BeatsXProcessor::userDir() const { auto d = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Ghosty").getChildFile("Patterns"); d.createDirectory(); return d; }
bool BeatsXProcessor::savePattern(int i, const juce::String& name) {
    juce::ValueTree t("GHOSTYPATTERN"); const auto& pt = model.patterns[(size_t) i];
    t.setProperty("len", pt.length, nullptr); t.setProperty("res", pt.res, nullptr); t.setProperty("tlen", gb::trackLenToString(pt), nullptr); t.setProperty("steps", gb::patternToString(pt), nullptr);
    juce::StringArray snd; for (auto& tr : model.tracks) snd.add(tr.sound); t.setProperty("sounds", snd.joinIntoString("|"), nullptr);
    auto f = userDir().getChildFile(juce::File::createLegalFileName(name.isEmpty() ? "Pattern" : name) + ".ghostpat");
    if (auto xml = t.createXml()) return xml->writeTo(f);
    return false;
}
bool BeatsXProcessor::loadPattern(const juce::File& f, int i) {
    auto xml = juce::XmlDocument::parse(f); if (!xml) return false;
    auto t = juce::ValueTree::fromXml(*xml); if (!t.hasType("GHOSTYPATTERN")) return false;
    pushUndo();
    auto& pt = model.patterns[(size_t) i]; pt.clear(); pt.length = juce::jlimit(1, gb::kMaxSteps, (int) t.getProperty("len", 16)); pt.res = juce::jlimit(0, 5, (int) t.getProperty("res", 1));
    gb::trackLenFromString(pt, t.getProperty("tlen").toString()); gb::patternFromString(pt, t.getProperty("steps").toString());
    publish(); return true;
}

int BeatsXProcessor::songBars() const { int e = 0; for (auto& b : model.song) e = juce::jmax(e, b.startBar + b.bars); return juce::jmax(1, e); }

static juce::File exportDir() { auto d = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("GhostyBeats"); d.createDirectory(); return d; }

// Everything a render needs, copied on the message thread so a background render never touches live state.
BeatsXProcessor::RenderSpec BeatsXProcessor::makeSpec(AudioJob j) {
    RenderSpec r; r.job = j; r.song = j.song && !model.song.empty(); r.bars = exportBars(r.song); r.pattern = currentPattern();
    r.P = readParams(liveBpm()); r.P.songMode = r.song; r.P.stutter = 0; r.P.tape = false; r.P.recording = false; r.P.padRepeat = false;
    r.allowed = unlockedNow();   // back-stop: the drag itself asked exportGate() (the demo export gate)
    if (!snaps.empty()) r.snap = std::make_shared<gb::Snapshot>(*snaps.back());
    for (auto& tr : model.tracks) { r.names.add(tr.sound); if (tr.sample) r.keep.push_back(tr.sample);
        const gb::Sound* snd = tr.sample ? tr.sample.get() : lib->byName(tr.sound).get(); auto it = snd ? variantCache.find(snd) : variantCache.end(); if (it != variantCache.end()) for (auto& v : it->second.v) if (v) r.keep.push_back(v); }
    r.keep.insert(r.keep.end(), keepSounds.end() - juce::jmin((int) keepSounds.size(), 64), keepSounds.end());
    return r;
}
static bool usedTrack(const gb::Snapshot& base, bool song, int pat, int t) {
    if (base.melody && base.melody->on) {   // the melody plays through its instruments' route tracks
        const auto& m = *base.melody; auto melScan = [&](int pi) { const auto& pt = m.pat(pi); for (int c = 0; c < gb::mel::kChannels; ++c) for (int r = 0; r < pt.rows; ++r) { const auto& cl = pt.cells[(size_t) c][(size_t) r];
            if (cl.inst >= 1 && cl.inst <= gb::mel::kInstruments && cl.note >= 1 && cl.note <= 128 && m.inst[(size_t) cl.inst - 1].used() && m.inst[(size_t) cl.inst - 1].routeTrack() == t) return true; } return false; };
        if (m.ownOrder) { for (auto o : m.order) if (melScan(o)) return true; }
        else if (song && base.songCount) { for (int i = 0; i < base.songCount; ++i) if (melScan(base.song[(size_t) i].pattern)) return true; }
        else if (melScan(pat)) return true;
    }
    auto scan = [&](int pi) { const auto& p = base.patterns[(size_t) juce::jlimit(0, gb::kPatterns - 1, pi)]; for (int s = 0; s < p.lenOf(t); ++s) if (p.steps[(size_t) t][(size_t) s].vel) return true; return false; };
    if (song && base.songCount) { for (int i = 0; i < base.songCount; ++i) if (scan(base.song[(size_t) i].pattern)) return true; return false; }
    return scan(pat);
}
// STEMS and single-track drags are rendered pre-bus (track chain + sidechain + VOLUME): they sum exactly to the dry mix.
// The MIX drag is the processed master (GLUE, CLIPPER, ROOM, FX, limiter), so it is what you hear.
juce::StringArray BeatsXProcessor::renderSpec(const RenderSpec& r, std::atomic<float>* progress, juce::Thread* onThread) {
    juce::StringArray files;
    if (!r.allowed || !r.snap) return files;   // demo mode: nothing is rendered or written
    if (r.snap->linear.on) r.snap->linPerf = linearPerfFor(*r.snap, r.P);   // (stems / solo copies inherit it: they sum to the linear mix)
    const auto& base = *r.snap; const int bpm = juce::roundToInt(r.P.bpm);
    auto stop = [&] { return onThread && onThread->threadShouldExit(); };
    auto solo = [&](int t) { auto s = std::make_unique<gb::Snapshot>(base); for (int k = 0; k < gb::kTracks; ++k) { s->tracks[(size_t) k].solo = (k == t); s->tracks[(size_t) k].mute = false; } s->anySolo = true; return s; };
    auto name = [&](int t) { return juce::String(t + 1).paddedLeft('0', 2) + " " + juce::File::createLegalFileName(r.names[t]); };
    auto dryP = r.P; dryP.bypassMaster = true;
    if (r.job.track >= 0) {
        auto f = exportDir().getNonexistentChildFile(name(r.job.track) + " " + juce::String(bpm) + "bpm", ".wav", true);
        renderTo(solo(r.job.track).get(), dryP, r.bars, f, onThread); if (!stop()) files.add(f.getFullPathName());
    } else if (r.job.stems) {
        auto dir = exportDir().getNonexistentChildFile("Ghosty Stems " + juce::String(bpm) + "bpm " + juce::Time::getCurrentTime().formatted("%H%M%S"), "", true); dir.createDirectory();
        std::vector<int> tracks; for (int t = 0; t < gb::kTracks; ++t) if (usedTrack(base, r.song, r.pattern, t) && !base.tracks[(size_t) t].mute && (!base.anySolo || base.tracks[(size_t) t].solo)) tracks.push_back(t);
        for (size_t i = 0; i < tracks.size() && !stop(); ++i) { auto f = dir.getChildFile(name(tracks[i]) + ".wav"); renderTo(solo(tracks[i]).get(), dryP, r.bars, f, onThread); files.add(f.getFullPathName()); if (progress) progress->store((float) (i + 1) / (float) tracks.size()); }
        if (stop()) { dir.deleteRecursively(); files.clear(); }
    } else {
        auto f = exportDir().getNonexistentChildFile("Ghosty Beat " + juce::String(r.song ? "Song " : "Pattern ") + juce::String(bpm) + "bpm", ".wav", true);   // never overwrite an earlier drag-out
        renderTo(&base, r.P, r.bars, f, onThread); if (!stop()) files.add(f.getFullPathName());
    }
    return files;
}
juce::File BeatsXProcessor::bounce(bool song, int bars) { if (! unlockedNow()) return {}; auto r = makeSpec({ song, false, -1 }); r.bars = bars; auto f = renderSpec(r, nullptr); return f.isEmpty() ? juce::File() : juce::File(f[0]); }
juce::StringArray BeatsXProcessor::bounceStems(bool song, int bars) { if (! unlockedNow()) return {}; auto r = makeSpec({ song, true, -1 }); r.bars = bars; return renderSpec(r, &renderProgress); }
juce::File BeatsXProcessor::bounceTrack(bool song, int bars, int t) { if (! unlockedNow()) return {}; auto r = makeSpec({ song, false, t }); r.bars = bars; auto f = renderSpec(r, nullptr); return f.isEmpty() ? juce::File() : juce::File(f[0]); }

juce::File BeatsXProcessor::renderTo(const gb::Snapshot* snap, const gb::EngineParams& P, int bars, const juce::File& f, juce::Thread* onThread) {
    // a private engine renders a snapshot offline at the host rate (same sounds, same timing as playback), 24-bit
    const double sr = renderRate; const int block = 512;
    gb::BeatEngine off; off.prepare(sr, block); off.setSnapshot(snap); off.internalPlay = true;
    const double bpm = P.bpm; const int lat = gb::BeatEngine::latencyFor(P, off.lookahead());   // the limiter's look-ahead is trimmed so the file starts on the beat
    const int loopLen = (int) (bars * 4 * 60.0 / bpm * sr), total = loopLen + (int) (sr * 1.5), rendered = total + lat;
    juce::AudioBuffer<float> all(2, rendered), buf(2, block); juce::MidiBuffer none;
    for (int pos = 0; pos < rendered; pos += block) {
        if (onThread && onThread->threadShouldExit()) return f;   // the drag was superseded: stop now (no file is written)
        const int n = juce::jmin(block, rendered - pos); buf.setSize(2, n, false, false, true);
        off.internalPlay = pos < loopLen;
        off.process(buf, none, P, bpm, 0, false);
        for (int c = 0; c < 2; ++c) all.copyFrom(c, pos, buf, c, 0, n);
    }
    if (lat > 0) { juce::AudioBuffer<float> t(2, total); for (int c = 0; c < 2; ++c) t.copyFrom(c, 0, all, c, lat, total); all = std::move(t); }
    f.deleteFile();
    juce::WavAudioFormat wav;
    if (auto os = std::unique_ptr<juce::OutputStream>(f.createOutputStream()))
        if (auto* w = wav.createWriterFor(os.get(), sr, 2, 24, {}, 0)) { os.release(); std::unique_ptr<juce::AudioFormatWriter> wr(w); wr->writeFromAudioSampleBuffer(all, 0, total); }   // GATE: via-caller (renderSpec: beats.drag-audio / drag-stems / track-wav-drag)
    return f;
}

// ---- background audio render + cache (same state + same options -> the same files, no re-render)
struct BeatsXProcessor::RenderThread : juce::Thread {
    BeatsXProcessor& p; RenderSpec spec; juce::String key; juce::StringArray result; std::atomic<bool> finished { false }; std::atomic<float>* progress;
    RenderThread(BeatsXProcessor& o, RenderSpec r, juce::String k, std::atomic<float>* pr) : juce::Thread("Ghosty Beats render"), p(o), spec(std::move(r)), key(std::move(k)), progress(pr) {}
    ~RenderThread() override { signalThreadShouldExit(); stopThread(30000); }
    void run() override { if (progress) progress->store(0.0f); result = p.renderSpec(spec, progress, this); if (threadShouldExit()) result.clear(); if (progress) progress->store(1.0f); finished = true; }
};
// what a render depends on: the beat (steps, tracks, samples by identity, song, locks, motifs, scripts), the parameters, the job
juce::int64 BeatsXProcessor::modelHash() const {
    juce::uint64 h = 1469598103934665603ULL; auto mix = [&](const void* d, size_t n) { const auto* b = static_cast<const uint8_t*>(d); for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 1099511628211ULL; };
    auto mixS = [&](const juce::String& s) { mix(s.toRawUTF8(), (size_t) s.getNumBytesAsUTF8()); mix("|", 1); };
    for (auto& pt : model.patterns) { mix(pt.steps.data(), sizeof(pt.steps)); mix(pt.trackLen.data(), sizeof(pt.trackLen)); mix(&pt.length, sizeof(int)); mix(&pt.res, sizeof(int)); mix(&pt.evolve, sizeof(float)); mix(&pt.evolveStyle, sizeof(int)); mix(&pt.fillEvery, sizeof(int)); mix(&pt.seed, sizeof(int)); }
    for (auto& tr : model.tracks) {
        mixS(tr.sound); const float f[] { tr.pitch, tr.decay, tr.vol, tr.pan, tr.tone, tr.drive, tr.swing, tr.accent, tr.human, tr.tAttack, tr.tSustain, tr.sat, tr.comp, tr.lowCut, tr.tilt, tr.highCut, tr.duck, tr.glide, tr.attack, tr.pitchEnv, tr.pitchEnvTime, tr.velAmt, tr.chopSens, tr.charAmount, tr.charDrive };
        mix(f, sizeof(f)); const int ii[] { tr.reverse, tr.mute, tr.solo, tr.choke, tr.lock, tr.character }; mix(ii, sizeof(ii)); mix(tr.rules.data(), sizeof(tr.rules));
        for (auto& L : tr.layers) { mixS(L.sound); const float lf[] { L.level, L.pitch, L.offset }; mix(lf, sizeof(lf)); }
        if (!tr.slices.empty()) mix(tr.slices.data(), tr.slices.size() * sizeof(int));
        if (!tr.regionIsDefault()) { const float rf[] { tr.regStart, tr.regEnd, tr.loopStart, tr.loopEnd }; mix(rf, sizeof(rf)); mix(&tr.loopOn, 1); }
        const void* sp = tr.sample.get(); mix(&sp, sizeof(sp)); if (tr.sample) { const size_t n = tr.sample->data.size(); mix(&n, sizeof(n)); }
    }
    for (auto& b : model.song) mix(&b, sizeof(b));
    mix(model.arc.data(), model.arc.size()); mix(&model.converge, sizeof(int)); mix(&model.scTrack, sizeof(int)); mix(&model.ver, sizeof(int)); mix(model.locks.data(), sizeof(model.locks)); mix(model.motifs.data(), sizeof(model.motifs));
    for (auto& sc : model.scripts) { mixS(sc.code); mix(&sc.on, 1); }
    { const auto& L = model.linear; mix(&L.on, 1); mix(&L.rule, 1); mix(&L.strictness, sizeof(float)); mix(&L.move, 1); mix(&L.lock, sizeof(L.lock)); mix(&L.variation, sizeof(int)); mix(&L.seed, sizeof(int)); mix(L.order.data(), L.order.size()); }
    gb::mel::hashInto(melody, mix);   // (field by field: no padding bytes)
    for (int i = 0; i < kParamIds.size(); ++i) { const float v = p[i]->load(); mix(&v, sizeof(v)); }
    for (int t = 0; t < gb::kTracks; ++t) for (int k = 0; k < 7; ++k) { const float v = tp[t][k]->load(); mix(&v, sizeof(v)); }
    mix(&evolveBars, sizeof(int));
    return (juce::int64) h;
}
juce::String BeatsXProcessor::audioKey(const AudioJob& j) {
    return juce::String::toHexString(modelHash()) + "-" + juce::String((int) j.song) + juce::String((int) j.stems) + juce::String(j.track) + "-" + juce::String(readParams(liveBpm()).bpm, 2) + "-" + juce::String((int) renderRate) + (license.isUnlocked() ? "u" : "d");
}
juce::StringArray BeatsXProcessor::renderNow(AudioJob j) { renderProgress.store(0.0f); auto r = renderSpec(makeSpec(j), &renderProgress); renderProgress.store(1.0f); return r; }
void BeatsXProcessor::startAudioRender(AudioJob j) {
    if (! unlockedNow()) return;   // demo: nothing to prepare
    const auto key = audioKey(j);
    { const juce::ScopedLock sl(renderLock); if (key == cachedKey && !cachedFiles.isEmpty()) return; }
    if (renderThread && renderThread->key == key) return;   // already on it (or done, waiting to be picked up)
    // never block the message thread: an old render is told to stop and reaped once it is gone
    if (renderThread) { renderThread->signalThreadShouldExit(); retiring.push_back(std::move(renderThread)); }
    for (size_t i = 0; i < retiring.size();) { if (!retiring[i]->isThreadRunning()) retiring.erase(retiring.begin() + (long) i); else ++i; }
    renderThread = std::make_unique<RenderThread>(*this, makeSpec(j), key, &renderProgress); renderThread->startThread();
}
juce::StringArray BeatsXProcessor::waitAudioRender(AudioJob j) {
    if (! unlockedNow()) return {};   // back-stop: the UI asked exportGate() (demo: no file)
    const auto key = audioKey(j);
    { const juce::ScopedLock sl(renderLock); bool ok = key == cachedKey && !cachedFiles.isEmpty(); for (auto& f : cachedFiles) ok = ok && juce::File(f).existsAsFile(); if (ok) return cachedFiles; }
    juce::StringArray r;
    if (renderThread && renderThread->key == key) { renderThread->waitForThreadToExit(120000); r = renderThread->result; renderThread.reset(); }
    else { if (renderThread) { renderThread->signalThreadShouldExit(); retiring.push_back(std::move(renderThread)); } r = renderNow(j); }
    for (size_t i = 0; i < retiring.size();) { if (!retiring[i]->isThreadRunning()) retiring.erase(retiring.begin() + (long) i); else ++i; }
    const juce::ScopedLock sl(renderLock); cachedKey = key; cachedFiles = r; return r;
}

// ---- MIDI: GM-style drum notes picked from what each track holds
int BeatsXProcessor::midiNoteFor(int track) const {
    auto pick = [&](int t) {
        const auto& tr = model.tracks[(size_t) t]; const auto n = tr.sound.toLowerCase(); const auto c = tr.sample ? juce::String("Sample") : categoryOf(tr.sound);
        if (c == "808") return 24;   // pitched 808 lines: C0 + the step pitch
        if (n.contains("pedal")) return 44;
        if (c == "Open Hat" || n.contains("open hat")) return 46;
        if (c == "Hat" || n.contains("hat")) return 42;
        if (c == "Kick" || n.contains("kick")) return 36;
        if (n.contains("rim")) return 37;
        if (c == "Clap" || n.contains("clap") || n.contains("snap")) return 39;
        if (c == "Snare" || n.contains("snare")) return 38;
        if (c == "Tom" || n.contains("tom")) return 45;
        if (n.contains("ride")) return 51;
        if (n.contains("china")) return 52;
        if (n.contains("splash")) return 55;
        if (c == "Cymbal" || n.contains("crash")) return 49;
        if (n.contains("cowbell")) return 56;
        if (n.contains("tambourine")) return 54;
        if (n.contains("shaker")) return 70;
        if (n.contains("timbale")) return n.contains("rim") ? 76 : (n.endsWith(" lo") ? 66 : 65);
        if (n.contains("conga")) return n.contains("low") || n.contains("tumba") || n.endsWith(" lo") ? 64 : 63;
        if (n.contains("bongo")) return n.endsWith(" lo") ? 61 : 60;
        if (n.contains("clave")) return 75;
        if (n.contains("wood")) return 76;
        if (n.contains("triangle")) return 81;
        return -1;
    };
    // second kick -> 35, second snare -> 40, more toms -> 41/43/47/48/50, second crash -> 57, others -> free notes from 60 up
    static const std::map<int, std::vector<int>> alt { { 36, { 35 } }, { 38, { 40 } }, { 45, { 41, 43, 47, 48, 50 } }, { 49, { 57 } }, { 42, { 44 } }, { 39, { 40 } }, { 51, { 59 } }, { 63, { 62 } } };
    std::vector<int> used;
    for (int t = 0; t <= track; ++t) {
        int n = pick(t);
        if (n == 24) { if (t == track) return 24; continue; }
        if (n >= 0 && std::find(used.begin(), used.end(), n) != used.end()) {
            int m = -1; auto it = alt.find(n); if (it != alt.end()) for (int a : it->second) if (std::find(used.begin(), used.end(), a) == used.end()) { m = a; break; }
            n = m;
        }
        if (n < 0) { n = 60; while (std::find(used.begin(), used.end(), n) != used.end() || n == 63 || n == 64 || n == 70 || n == 75 || n == 76) ++n; }
        used.push_back(n);
        if (t == track) return n;
    }
    return 36 + track;
}

juce::File BeatsXProcessor::exportMidi(bool song, int track) {
    if (! unlockedNow() || snaps.empty()) return {};   // back-stop (the UI asked exportGate())
    song = song && !model.song.empty();
    const int tpq = 960; const auto& S = *snaps.back();
    auto P = readParams(liveBpm()); P.songMode = song;
    const auto& cp = model.patterns[(size_t) currentPattern()]; const int eb = exportBars(false);
    double beats = song ? songBars() * 4.0 : (eb > cp.loopBars() ? eb * 4.0 : cp.loopBeats());   // one loop (longest track), or the EVOLVE performance
    const auto perf = S.linear.on ? linearPerfFor(S, P) : nullptr;
    if (perf) beats = perf->exportBeats();   // LINEAR: exactly one period of what plays (the loop of all tracks, or the fixed performance / the song)
    int note[gb::kTracks]; for (int t = 0; t < gb::kTracks; ++t) note[t] = midiNoteFor(t);
    juce::MidiMessageSequence seq; const int ch = midiChannel == 10 ? 10 : 1;
    seq.addEvent(juce::MidiMessage::textMetaEvent(3, track >= 0 ? "Ghosty Beats " + model.tracks[(size_t) track].sound : juce::String("Ghosty Beats")), 0);
    seq.addEvent(juce::MidiMessage::timeSignatureMetaEvent(4, 4), 0);
    seq.addEvent(juce::MidiMessage::tempoMetaEvent((int) std::llround(60000000.0 / P.bpm)), 0);
    struct N { int t, nn, vel; double on, len; bool bass; };
    std::vector<N> notes;
    auto each = [&](int t, double beat, float vel, const gb::EvInfo& e) {   // DRAG MIDI == what plays (LINEAR included)
        const float pitch = e.pitch;
        if (track >= 0 && t != track) return;
        const auto& tr = S.tracks[(size_t) t]; if (track < 0 && (tr.mute || (S.anySolo && !tr.solo))) return;
        int pat = 0; double org = 0; gb::locate(S, P, juce::jmax(0.0, beat), pat, org);
        const double sb = gb::stepBeats(S.patterns[(size_t) pat].res);
        const int nn = juce::jlimit(0, 127, note[t] + (note[t] == 24 ? (int) std::lround(pitch + tr.pitch) : 0));
        notes.push_back({ t, nn, juce::jlimit(1, 127, (int) std::lround(vel * 127.0f)), juce::jmax(0.0, beat), e.gate > 0 ? juce::jmin(e.gate, 0.125) : juce::jmin(sb * 0.45, 0.125), note[t] == 24 });
    };
    if (perf) gb::linear::play(*perf, 0.0, beats, each); else gb::collectEvents(S, P, P.bpm, 0.0, beats, each);
    // 808 / bass tracks: a note holds until the next hit on its track (or 4 beats), so the drag is a playable bass line
    std::sort(notes.begin(), notes.end(), [](const N& a, const N& b) { return a.on < b.on || (a.on == b.on && a.t < b.t); });
    for (size_t i = 0; i < notes.size(); ++i) if (notes[i].bass) { double next = notes[i].on + 4.0; for (size_t k = i + 1; k < notes.size(); ++k) if (notes[k].t == notes[i].t && notes[k].on > notes[i].on + 1e-6) { next = notes[k].on; break; }
        notes[i].len = juce::jmax(0.05, juce::jmin(next - notes[i].on - 0.01, beats - notes[i].on)); }
    for (auto& nt : notes) { const double tick = nt.on * tpq; seq.addEvent(juce::MidiMessage::noteOn(ch, nt.nn, (juce::uint8) nt.vel), tick); seq.addEvent(juce::MidiMessage::noteOff(ch, nt.nn), tick + juce::jmax(10.0, nt.len * tpq)); }
    seq.sort(); seq.updateMatchedPairs();
    juce::MidiFile mf; mf.setTicksPerQuarterNote(tpq); mf.addTrack(seq);
    auto name = juce::String("Ghosty Beat ") + (track >= 0 ? juce::File::createLegalFileName(model.tracks[(size_t) track].sound) + " " : juce::String()) + juce::Time::getCurrentTime().formatted("%H%M%S");
    auto f = exportDir().getNonexistentChildFile(name, ".mid", true);
    f.deleteFile(); if (auto os = std::unique_ptr<juce::FileOutputStream>(f.createOutputStream())) mf.writeTo(*os);   // GATE: via-caller (beats.drag-midi / beats.track-midi-drag)
    return f;
}

// ---- undo / redo: whole-model snapshots + the melody song (samples are shared pointers, so this is cheap)
void BeatsXProcessor::pushUndo() { undoStack.push_back({ model, melody }); if (undoStack.size() > 64) undoStack.pop_front(); redoStack.clear(); }
bool BeatsXProcessor::undo() { if (undoStack.empty()) return false; redoStack.push_back({ model, melody }); model = undoStack.back().m; melody = undoStack.back().s; undoStack.pop_back(); publish(); return true; }
bool BeatsXProcessor::redo() { if (redoStack.empty()) return false; undoStack.push_back({ model, melody }); model = redoStack.back().m; melody = redoStack.back().s; redoStack.pop_back(); publish(); return true; }
// ---- DRAG MELODY MIDI + the LINEAR preview strip
juce::File BeatsXProcessor::exportMelodyMidi(bool song) {
    if (! unlockedNow() || snaps.empty()) return {};   // back-stop (the UI asked exportGate())
    song = song && !model.song.empty(); const auto& S = *snaps.back(); const auto P = readParams(liveBpm());
    auto mf = gb::mel::toMidi(melody, &S, song, currentPattern(), P.bpm, melodyExportBeats(song));
    auto f = exportDir().getNonexistentChildFile("Ghosty Melody " + juce::Time::getCurrentTime().formatted("%H%M%S"), ".mid", true);
    f.deleteFile(); if (auto os = std::unique_ptr<juce::FileOutputStream>(f.createOutputStream())) mf.writeTo(*os);   // GATE: via-caller (beats.drag-melody-midi)
    return f;
}
double BeatsXProcessor::melodyExportBeats(bool song) const {
    if (melody.ownOrder) { gb::mel::Song c = melody; c.prepare(); return juce::jmax(melody.rowBeats(), c.orderBeats()); }
    const auto& cp = model.patterns[(size_t) currentPattern()];
    return song && !model.song.empty() ? songBars() * 4.0 : juce::jmax(cp.loopBeats(), melody.pat(currentPattern()).rows * melody.rowBeats());
}
juce::File BeatsXProcessor::exportBeatAndMelodyMidi(bool song) {   // one file: the drum track + one track per melody channel
    if (! unlockedNow() || snaps.empty()) return {};   // back-stop (the UI asked exportGate())
    song = song && !model.song.empty();
    auto beat = exportMidi(song); if (!beat.existsAsFile()) return {};
    juce::MidiFile a; { juce::FileInputStream in(beat); a.readFrom(in); } beat.deleteFile();
    const auto& S = *snaps.back(); const auto P = readParams(liveBpm());
    auto mf = gb::mel::toMidi(melody, &S, song, currentPattern(), P.bpm, melodyExportBeats(song));
    juce::MidiFile out; out.setTicksPerQuarterNote(960); for (int i = 0; i < a.getNumTracks(); ++i) out.addTrack(*a.getTrack(i)); for (int i = 0; i < mf.getNumTracks(); ++i) out.addTrack(*mf.getTrack(i));
    auto f = exportDir().getNonexistentChildFile("Ghosty Beat + Melody " + juce::Time::getCurrentTime().formatted("%H%M%S"), ".mid", true);
    f.deleteFile(); if (auto os = std::unique_ptr<juce::FileOutputStream>(f.createOutputStream())) out.writeTo(*os);   // GATE: via-caller (beats.drag-beat-melody-midi)
    return f;
}
bool BeatsXProcessor::melodyRowAt(double beat, int& pat, int& row) const {
    if (snaps.empty() || !snaps.back()->melody) return false; const auto& S = *snaps.back(); const auto P = readParams(liveBpm());
    gb::mel::TickPos tp; const double rb = S.melody->rowBeats();
    if (!gb::mel::tickAt(*S.melody, &S, P.songMode, P.pattern, beat + rb * 0.5, tp)) return false;   // (+ half a row: the nearest row, as you played it)
    pat = tp.pat; row = tp.row; return true;
}
void BeatsXProcessor::previewMelodyNote(int inst, int note, float vel) { engine.melodyAudition(juce::jlimit(0, gb::mel::kInstruments - 1, inst), note, vel); }
int BeatsXProcessor::drainMelodyMidi(const std::function<void(int note, int vel, double beat, bool on)>& f) {
    int n = 0, a, b, c, d; const int k = engine.melFifo.getNumReady(); engine.melFifo.prepareToRead(k, a, b, c, d);
    auto take = [&](int i) { const auto& e = engine.melNotes[i]; f(e.note, e.vel, e.beat, e.on); ++n; };
    for (int i = 0; i < b; ++i) take(a + i); for (int i = 0; i < d; ++i) take(c + i); engine.melFifo.finishedRead(b + d);
    return n;
}

// ---- LINEAR: the performance the snapshot carries, the worker that refreshes it, the preview
gb::linear::PerfPtr BeatsXProcessor::linearPerfFor(const gb::Snapshot& S, const gb::EngineParams& P) const {
    if (gb::linear::fresh(S.linPerf.get(), S, P, P.bpm, S.linear, evolveBars)) return S.linPerf;
    return gb::linear::build(S, P, P.bpm, S.linear, evolveBars);
}
void BeatsXProcessor::keepPerf(const gb::linear::PerfPtr& p) {
    if (!p) return; const double now = juce::Time::getMillisecondCounterHiRes();
    keepPerfs.push_back({ p, now });
    while (keepPerfs.size() > 4 && now - keepPerfs.front().second > 30000.0) keepPerfs.pop_front();   // (the audio thread holds an old one for at most a bar)
}
struct BeatsXProcessor::LinearWorker : juce::Thread {
    std::mutex m; std::shared_ptr<const gb::Snapshot> job; gb::EngineParams P; gb::linear::Settings set; int horizon = 16; gb::linear::PerfPtr result; std::atomic<bool> busy { false };
    LinearWorker() : juce::Thread("Ghosty Beats LINEAR") { startThread(juce::Thread::Priority::low); }
    ~LinearWorker() override { signalThreadShouldExit(); notify(); stopThread(10000); }
    void submit(std::shared_ptr<const gb::Snapshot> s, const gb::EngineParams& p, const gb::linear::Settings& st, int h) { { std::lock_guard<std::mutex> l(m); job = std::move(s); P = p; set = st; horizon = h; } busy = true; notify(); }
    gb::linear::PerfPtr take() { std::lock_guard<std::mutex> l(m); auto r = result; result.reset(); return r; }
    void run() override {
        while (!threadShouldExit()) {
            std::shared_ptr<const gb::Snapshot> s; gb::EngineParams p; gb::linear::Settings st; int h = 16;
            { std::lock_guard<std::mutex> l(m); s = std::move(job); job.reset(); p = P; st = set; h = horizon; }
            if (!s) { busy = false; wait(200); continue; }
            auto r = gb::linear::build(*s, p, p.bpm, st, h);
            { std::lock_guard<std::mutex> l(m); result = r; }
        }
    }
};
void BeatsXProcessor::trimKeeps(bool force) {
    const double now = juce::Time::getMillisecondCounterHiRes(); if (!force && now - lastTrim < 5000.0) return; lastTrim = now;
    for (auto& sp : snaps) { auto see = [&](const void* q) { if (q) lastSeen[q] = now; };
        for (auto* q : sp->sound) see(q); for (auto& l : sp->layer) for (auto* q : l) see(q); for (auto& vv : sp->variant) for (auto* q : vv) see(q); for (auto* q : sp->melSound) see(q); for (auto* q : sp->melMips) see(q); }
    auto stale = [&](const void* q) { auto it = lastSeen.find(q); return it == lastSeen.end() ? true : now - it->second > keepMs; };
    std::sort(keepSounds.begin(), keepSounds.end()); keepSounds.erase(std::unique(keepSounds.begin(), keepSounds.end()), keepSounds.end());
    keepSounds.erase(std::remove_if(keepSounds.begin(), keepSounds.end(), [&](const gb::SoundPtr& p) { return p.use_count() == 1 && stale(p.get()) && (!lastSeen.count(p.get()) ? false : true); }), keepSounds.end());
    keepMips.erase(std::remove_if(keepMips.begin(), keepMips.end(), [&](const std::shared_ptr<const gb::mel::Mips>& m) { return stale(m.get()) && lastSeen.count(m.get()); }), keepMips.end());
    for (auto it = lastSeen.begin(); it != lastSeen.end();) if (now - it->second > 120000.0) it = lastSeen.erase(it); else ++it;
}
void BeatsXProcessor::linearPoll() {
    trimKeeps();
    if (snaps.empty() || !model.linear.on) return;
    if (!linWorker) linWorker = std::make_unique<LinearWorker>();
    const auto& cur = *snaps.back(); const auto P = readParams(liveBpm());
    if (auto r = linWorker->take()) {   // a rebuilt Perf: install it if it still matches what plays
        if (gb::linear::fresh(r.get(), cur, P, P.bpm, cur.linear, evolveBars)) {
            auto s = std::make_unique<gb::Snapshot>(cur); s->linPerfPrev = cur.linPerf; s->linPerf = r; keepPerf(r);
            engine.setSnapshot(s.get()); snaps.push_back(std::move(s));
            return;
        }
    }
    if (!gb::linear::fresh(cur.linPerf.get(), cur, P, P.bpm, cur.linear, evolveBars) && !linWorker->busy)
        linWorker->submit(std::make_shared<const gb::Snapshot>(cur), P, cur.linear, evolveBars);
}
BeatsXProcessor::LinearView BeatsXProcessor::linearView(double playBeat) {
    LinearView v; if (snaps.empty()) return v;
    const auto& S = *snaps.back(); auto P = readParams(liveBpm()); P.songMode = songModeOn() && S.songCount > 0;
    gb::linear::Settings set = model.linear; set.on = true;
    gb::linear::PerfPtr perf;
    if (S.linear.on && S.linPerf) perf = linearPerfFor(S, P);
    else { if (!gb::linear::fresh(previewPerf.get(), S, P, P.bpm, set, evolveBars)) previewPerf = gb::linear::build(S, P, P.bpm, set, evolveBars); perf = previewPerf; }   // (off: what it would do, cached)
    v.span = perf->span; v.periodic = perf->periodic; v.song = perf->song; v.st = perf->st;
    double x = juce::jmax(0.0, playBeat); if (!perf->song) x -= std::floor(x / perf->span) * perf->span;
    v.beats = juce::jmin(8.0, perf->span); v.from = std::floor(x / 8.0) * 8.0; if (v.from + v.beats > perf->span) v.from = juce::jmax(0.0, perf->span - v.beats);
    v.lin = gb::linear::render(*perf, v.from, v.from + v.beats, true);
    gb::linear::Settings off = set; off.on = false; v.orig = gb::linear::render(S, P, P.bpm, v.from, v.from + v.beats, off);
    return v;
}

void BeatsXProcessor::getStateInformation(juce::MemoryBlock& dest) {
    // a binary ValueTree stream (samples are FLAC bytes, not base64 text); v1 / v2 XML states are still read by restoreState
    auto state = apvts.copyState();
    auto m = gb::modelToTree(model); m.setProperty("evbars", evolveBars, nullptr);
    m.appendChild(gb::linear::toTree(model.linear), nullptr); m.appendChild(gb::mel::toTree(melody), nullptr);
    state.appendChild(m, nullptr);
    if (playMode.load() != 0) state.setProperty("playmode", playMode.load(), nullptr); else state.removeProperty("playmode", nullptr);   // PLAY: ON NOTES (per project)
    // SYNC mode + own tempo (per project; written only when not the defaults, so a DAW-synced project saves exactly as before)
    if (sync.getMode() != ghosty::SyncTransport::Mode::Daw) state.setProperty("syncmode", 1, nullptr); else state.removeProperty("syncmode", nullptr);
    if (sync.getOwnBpm() != 120.0) state.setProperty("ownbpm", sync.getOwnBpm(), nullptr); else state.removeProperty("ownbpm", nullptr);
    dest.reset(); juce::MemoryOutputStream mo(dest, false); state.writeToStream(mo);   // NOT-EXPORT: the host project state
}

void BeatsXProcessor::setStateInformation(const void* data, int size) {
    ghosty::StateLoadNotify notifyHost (*this);   // CLAP: tell the host the values changed (Shared/HostNotify.h)
    if (! license.isUnlocked()) return;   // demo mode: a reopened project starts at the default sound (unlocking brings the saved sound back)
    restoreState(data, size);
}
void BeatsXProcessor::restoreState(const void* data, int size, bool keepBypass) {
    juce::ValueTree t;
    if (auto xml = getXmlFromBinary(data, size)) t = juce::ValueTree::fromXml(*xml);   // v1 / v2 projects (XML)
    else t = juce::ValueTree::readFromData(data, (size_t) juce::jmax(0, size));
    if (!t.isValid()) return;
    auto m = t.getChildWithName("BEATS2");
    if (!m.isValid()) { model.linear = {}; melody = gb::mel::Song(); }
    if (m.isValid()) { gb::modelFromTree(model, m, 48000.0); evolveBars = juce::jlimit(8, 64, (int) m.getProperty("evbars", 16));
                       model.linear = gb::linear::fromTree(m.getChildWithName("LINEAR")); gb::linear::normaliseOrder(model.linear, roles()); melody = gb::mel::Song(); gb::mel::fromTree(melody, m.getChildWithName("MELODY")); gb::mel::migrate(melody, model.tracks);   // missing = off / empty (old projects unchanged)
                       t.removeChild(m, nullptr); }
    else if (t.hasProperty("steps")) {   // an old Ghosty Beats project: carry its 8x16 beat into pattern A
        model.linear = {}; melody = gb::mel::Song();   // (it has neither)
        applyKit(0, false);
        auto rows = juce::StringArray::fromTokens(t.getProperty("steps").toString(), "|", "");
        static const int map[8] = { 1, 2, 3, 4, 5, 6, 7, 14 };   // kick, snare, clap... onto the new layout
        for (auto& pt : model.patterns) pt.clear();
        for (int r = 0; r < juce::jmin(8, rows.size()); ++r)
            for (int s = 0; s < juce::jmin(16, rows[r].length()); ++s) { const int v = rows[r][s] - '0'; if (v > 0) model.patterns[0].steps[(size_t) map[r]][(size_t) s].vel = (uint8_t) (v > 1 ? 127 : 105); }
    }
    const bool savedLimiter = t.getChildWithProperty("id", "limiter").isValid();   // (checked before replaceState, which adds the missing parameters)
    // BYPASS is never part of a beat: LOAD BEAT keeps the current state; a host session restores what it saved; a session saved before
    // BYPASS existed loads with it off (replaceState alone would keep whatever this instance had)
    const bool bypassNow = bypassParam && bypassParam->get();
    const auto savedBypass = t.getChildWithProperty("id", "bypass");
    if (t.hasType(apvts.state.getType())) apvts.replaceState(t);
    if (!keepBypass) playMode.store(juce::jlimit(0, 1, (int) t.getProperty("playmode", 0)));
    if (!keepBypass) { sync.setMode((int) t.getProperty("syncmode", 0) == 1 ? ghosty::SyncTransport::Mode::Own : ghosty::SyncTransport::Mode::Daw); sync.setOwnBpm((double) t.getProperty("ownbpm", 120.0)); }   // old projects: DAW, 120   // PLAY mode: per project, never from a beat (old projects: ALWAYS)
    if (bypassParam) {
        const bool want = keepBypass ? bypassNow : (savedBypass.isValid() && (float) savedBypass.getProperty("value", 0.0f) > 0.5f);
        if (bypassParam->get() != want) bypassParam->setValueNotifyingHost(want ? 1.0f : 0.0f);
    }
    if (m.isValid() && model.ver < 4 && !savedLimiter)   // a v1 project: keep the v1 output stage unless it saved a choice
        if (auto* q = apvts.getParameter("limiter")) q->setValueNotifyingHost(q->convertTo0to1(1.0f));
    publish();
}

BeatsXProcessor::~BeatsXProcessor() { cancelPendingUpdate(); latencyPoll.stopTimer(); linearPollTimer.stopTimer(); linWorker.reset(); worker.reset(); if (renderThread) renderThread->signalThreadShouldExit(); for (auto& r : retiring) r->signalThreadShouldExit(); renderThread.reset(); retiring.clear(); engine.setSnapshot(nullptr); }
juce::AudioProcessorEditor* BeatsXProcessor::createEditor() { return new BeatsXEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BeatsXProcessor(); }
