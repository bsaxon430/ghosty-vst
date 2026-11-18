#include "PluginProcessor.h"
#include "HostNotify.h"
#include "PluginEditor.h"

PoltergeistProcessor::~PoltergeistProcessor() {
    aliveFlag->store(false); if (pitchCancel) pitchCancel->store(true);
#if GHOSTY_PROCESS_LAB
    labAudition.release();
#endif
}

#if GHOSTY_PROCESS_LAB
std::shared_ptr<plab::Audio> PoltergeistProcessor::originalFromFile(const juce::File& f) {
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(f)); if (!r) return nullptr;
    const int n = (int) std::min<juce::int64>(r->lengthInSamples, (juce::int64) (r->sampleRate * 60.0));
    const int nch = (int) juce::jlimit(1u, 2u, r->numChannels);
    juce::AudioBuffer<float> b(nch, n); r->read(&b, 0, n, 0, true, nch > 1);
    // the same trim + level as SampleData::fromBuffer (so a region here is the same spot on the timeline copy)
    std::vector<float> mono((size_t) n, 0.0f);
    for (int c = 0; c < nch; ++c) { const float* x = b.getReadPointer(c); for (int i = 0; i < n; ++i) mono[(size_t) i] += std::isfinite(x[i]) ? x[i] / (float) nch : 0.0f; }
    int a = 0, z = n - 1;
    while (a < z && std::abs(mono[(size_t) a]) < 0.0015f) ++a;
    while (z > a && std::abs(mono[(size_t) z]) < 0.0015f) --z;
    if (z - a <= 32) { a = 0; z = n - 1; }
    float peak = 1e-6f; for (int i = a; i <= z; ++i) peak = juce::jmax(peak, std::abs(mono[(size_t) i]));
    auto o = std::make_shared<plab::Audio>(); o->rate = r->sampleRate; o->resize(nch, z - a + 1);
    for (int c = 0; c < nch; ++c) { const float* x = b.getReadPointer(c); for (int i = a; i <= z; ++i) o->ch[(size_t) c][(size_t) (i - a)] = std::isfinite(x[i]) ? x[i] * 0.9f / peak : 0.0f; }
    return o;
}

plab::Audio PoltergeistProcessor::labProcessAudio(int id) const {
    if (auto it = labOriginals.find(id); it != labOriginals.end() && it->second && lab.sources.count(id)) return *it->second;
    plab::Audio a; a.rate = gs::Lab::kRate;
    if (auto it = lab.sources.find(id); it != lab.sources.end() && it->second) { a.rate = it->second->rate; a.ch.push_back(it->second->data); }
    return a;
}

std::vector<plab::Region> PoltergeistProcessor::labSlices(int id) const {
    std::vector<plab::Region> out;
    auto it = lab.sources.find(id);
    if (!chop.audio || chop.count() < 1 || it == lab.sources.end() || it->second->name != chop.audio->name) return out;
    const auto a = labProcessAudio(id); if (a.empty()) return out;
    const double secs = (double) chop.size() / chop.rate(), mine = (double) a.length() / a.rate;
    if (std::abs(secs - mine) > 0.01 * juce::jmax(secs, 0.1)) return out;   // not the same audio any more (processed, trimmed...)
    for (int i = 0; i < chop.count(); ++i) {
        const int s0 = (int) std::lround(chop.slices[(size_t) i].start / chop.rate() * a.rate), s1 = juce::jmin(a.length(), (int) std::lround(chop.endOf(i) / chop.rate() * a.rate));
        if (s1 > s0) out.push_back({ s0, s1 - s0 });
    }
    return out;
}

int PoltergeistProcessor::addLabSourceFrom(const plab::Audio& a, const juce::String& suffix, int fromId) {
    if (a.empty()) return 0;
    auto s = std::make_shared<gs::SampleData>(); s->rate = gs::Lab::kRate; s->data.assign(1, 0.0f);
    auto it = lab.sources.find(fromId); s->name = (it != lab.sources.end() ? it->second->name : juce::String("Processed")) + suffix;
    const int id = lab.addSource(s);   // (a placeholder: replaceLabSource makes the real timeline copy + keeps the full result)
    replaceLabSource(id, a);
    lab.current = id;
    return id;
}

bool PoltergeistProcessor::replaceLabSource(int id, const plab::Audio& a) {
    auto it = lab.sources.find(id); if (it == lab.sources.end() || a.empty()) return false;
    labOriginals[id] = std::make_shared<plab::Audio>(a);   // the full result (its own rate, every channel)
    // the timeline copy: mono at the Lab's rate
    auto s = std::make_shared<gs::SampleData>(); s->name = it->second->name; s->rate = gs::Lab::kRate; s->made = true;   // a PROCESS LAB result
    const auto mono = plab::monoMix(a); const auto& m = mono.ch.empty() ? std::vector<float>() : mono.ch[0];
    const double ratio = a.rate / gs::Lab::kRate; const int n = (int) std::floor((double) m.size() / ratio);
    s->data.resize((size_t) juce::jmax(0, n));
    for (int i = 0; i < n; ++i) { const double pos = i * ratio; const int i0 = (int) pos; const float t = (float) (pos - i0);
        const float x0 = m[(size_t) juce::jmin(i0, (int) m.size() - 1)], x1 = m[(size_t) juce::jmin(i0 + 1, (int) m.size() - 1)]; s->data[(size_t) i] = x0 + (x1 - x0) * t; }
    for (auto& x : s->data) if (!std::isfinite(x)) x = 0.0f;
    s->buildFrames();
    keepAlive.push_back(it->second); it->second = s;
    labRenderPreview();
    return true;
}
#endif

PoltergeistProcessor::PoltergeistProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "POLTERGEIST", gs::createLayout()) {
    reader.attach(apvts);
    bypassParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("bypass"));
    summonSeed = (uint32_t) juce::Random::getSystemRandom().nextInt();
    loadPreset(0);
    // a brand-new instance runs the current engine revision (an old project restores without eng_ver -> 0; choosing an
    // original preset from the list sets 0 again, so the original presets always sound as they did)
    if (auto* ev = apvts.getParameter("eng_ver")) ev->setValueNotifyingHost(ev->convertTo0to1((float) gs::kEngVer));
    // a new instance bounces at 8x (old projects keep "same as live": their RENDER QUALITY is missing, so it stays at the default)
    if (auto* rq = apvts.getParameter("render_q")) rq->setValueNotifyingHost(rq->convertTo0to1(2.0f));
    morph.build(apvts, *this);
    morphOnRaw = apvts.getRawParameterValue("morph_on"); morphXRaw = apvts.getRawParameterValue("morph_x"); morphYRaw = apvts.getRawParameterValue("morph_y");
    startTimerHz(4);
}

int PoltergeistProcessor::wantedOversampling(bool offline) const {
    const int live = 2 << juce::jlimit(0, 2, (int) apvts.getRawParameterValue("quality")->load());
    const int rq = (int) apvts.getRawParameterValue("render_q")->load();
    const bool current = apvts.getRawParameterValue("eng_ver")->load() >= (float) gs::kEngVer;   // the original presets bounce exactly as they play
    return offline && rq > 0 && current ? juce::jmax(live, rq == 1 ? 4 : 8) : live;
}

void PoltergeistProcessor::applyQuality() {
    if (preparedRate <= 0 || isNonRealtime()) return;   // a bounce keeps what prepareToPlay chose
    const int want = gs::Engine::effectiveFactor(preparedRate, wantedOversampling(false));
    if (want == engine.oversampling()) return;
    {
        const juce::ScopedLock sl(getCallbackLock());   // the audio thread waits for this short re-prepare
        engine.prepare(preparedRate, preparedBlock, want);
    }
    setLatencySamples(engine.latency());
}

void PoltergeistProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    preparedRate = sampleRate; preparedBlock = juce::jmax(16, samplesPerBlock);
    bypass.prepare(sampleRate, 0.006, bypassParam->get()); bypassWasSilent = bypassParam->get();
#if GHOSTY_PROCESS_LAB
    labAudition.prepare(sampleRate);
#endif
    engine.prepare(sampleRate, preparedBlock, wantedOversampling(isNonRealtime()));
    demo.prepare(sampleRate);
    setLatencySamples(engine.latency());
    keyboard.reset();
    midiFx.reset();
    merged.ensureSize(16384);   // the audio thread merges MIDI into this without allocating
    chopLastNote = -1; chopLastStep = -1; chopClock = 0;
}

void PoltergeistProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    // BYPASS: on engage every voice is released (the 6 ms fade covers it); once faded out nothing runs and MIDI is ignored
    bypass.set(bypassParam->get());
    if (bypass.takeEngageEdge()) { midi.clear(); midi.addEvent(juce::MidiMessage::allNotesOff(1), 0); midiFx.reset(); keyboard.allNotesOff(0); chopLastNote = -1; }
    else if (bypass.isBypassed()) midi.clear();
    if (bypass.silent()) {
        if (!bypassWasSilent) { engine.hardReset(); bypassWasSilent = true; }   // nothing left to fade: start clean when it comes back
        buffer.clear(); midi.clear(); return;
    }
    bypassWasSilent = false;
    if (!bypass.isBypassed()) keyboard.processNextMidiBuffer(midi, 0, buffer.getNumSamples(), true);
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition()) {
            if (auto b = pos->getBpm()) { bpm = *b; engine.setBpm(*b); }
            ppq = pos->getPpqPosition().orFallback(0.0); playing = pos->getIsPlaying();
            engine.setPpq(ppq, playing);
        }
    if (!(*morphOnRaw > 0.5f && morph.process(snap, buffer.getNumSamples(), getSampleRate(), ppq, playing, bpm, morphXRaw->load(), morphYRaw->load())))
        reader.read(snap);
    if (buffer.getNumChannels() < 2) { buffer.clear(); return; }
    midiFx.process(midi, buffer.getNumSamples(), snap, getSampleRate(), bpm, ppq, playing);
    merged.clear();
    for (const auto md : midi) merged.addEvent(md.getMessage(), md.samplePosition);
    chopSequence(merged, buffer.getNumSamples());   // CHOP MELODY (after the play helpers: its notes are the slices' keys)
    engine.process(buffer, merged, snap);
    // Sample Lab preview: the bounced timeline looped under everything, locked to the host beat while playing
    int rA = previewRangeA.load(), rB = previewRangeB.load(), lA = previewLoopA.load(), lB = previewLoopB.load();
    if (!(rA >= 0 && rB > rA) && previewOriginal.load() && originalEnd.load() > originalStart.load() && originalStart.load() >= 0) {   // ORIGINAL: the strip's START..END + LOOP
        rA = originalStart.load(); rB = originalEnd.load(); lA = originalLoopA.load(); lB = originalLoopB.load(); }
    const bool section = rA >= 0 && rB > rA;   // PLAY SECTION / a START..END window
    if (previewRestart.exchange(false)) previewPos = section ? (double) rA : 0.0;
    if (previewOn.load())
        if (auto* pv = previewOriginal.load() ? originalPtr.load() : previewPtr.load()) {
            const int N = section ? juce::jmin(rB, pv->size()) : pv->size(), start = section ? juce::jmin(rA, N) : 0;
            const bool loop = previewLoop.load();
            const bool region = loop && lA >= start && lB <= N && lB - lA >= 64;   // LOOP region: play into it, then repeat just it
            if (N > start) {
                const double inc = pv->rate / getSampleRate();
                if (playing && loop && !section && !previewOriginal.load()) { const double spb = 60.0 / bpm * pv->rate; previewPos = std::fmod(ppq * spb, (double) N); }   // (LOOP: locked to the host beat, as PREVIEW was)
                for (int i = 0; i < buffer.getNumSamples(); ++i) {
                    if (region && previewPos >= lB) previewPos -= (lB - lA);
                    if (previewPos >= N) { if (!loop) { previewOn.store(false); previewPos = start; break; } previewPos -= (N - start); }   // played once: stop
                    const float x = pv->at(previewPos) * 0.8f;
                    buffer.addSample(0, i, x); buffer.addSample(1, i, x);
                    previewPos += inc;
                }
            }
        }
#if GHOSTY_PROCESS_LAB
    labAudition.render(buffer, 0, buffer.getNumSamples(), 0.9f);   // PROCESS LAB audition (silent unless the panel plays)
#endif
    demo.process(buffer, license.isUnlocked());
    bypass.applyMute(buffer);   // the fade on engage / release (a bit-exact no-op while playing)
}

void PoltergeistProcessor::applyPatch(const gs::Patch& p) {
    for (auto& [id, value] : p)
        if (auto* prm = apvts.getParameter(id)) {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost(prm->convertTo0to1(value));
            prm->endChangeGesture();
        }
}

juce::String PoltergeistProcessor::summon(const juce::String& words) {
    snapshotsLeft();
    summonSeed = summonSeed * 1664525u + 1013904223u;
    juce::String cat;
    auto bass = gs::BassSummon::summon(words, summonSeed, &cat);   // bass words (growl, riddim, reese, 808, ...) use the BASS presets
    applyPatch(bass.empty() ? gs::Summoner::summon(words, summonSeed, &cat) : bass);
    patchName = words.trim().isEmpty() ? "Summoned " + cat : words.trim();
    return cat;
}

ghosty::SnapshotBank& PoltergeistProcessor::snapshots() {
    if (!snaps) {
        snaps = std::make_unique<ghosty::SnapshotBank>(ghosty::SnapshotBank::Options { "Poltergeist" });
        snaps->capture = [this] { return captureSound(); };
        snaps->restore = [this](const juce::ValueTree& t) { restoreSound(t); };
        snaps->onRestored = [this](const ghosty::SnapshotInfo& in) { patchName = in.name; };
    }
    return *snaps;
}
juce::ValueTree PoltergeistProcessor::captureSound() {
    juce::MemoryBlock mb; getStateInformation(mb); tellLeftOut("save:polt.snapshot");
    if (auto xml = getXmlFromBinary(mb.getData(), (int) mb.getSize())) { auto t = juce::ValueTree::fromXml(*xml); t.removeChild(t.getChildWithName("MORPH"), nullptr); return t; }   // (a sound: not the board)
    return {};
}
void PoltergeistProcessor::restoreSound(const juce::ValueTree& t) {
    auto xml = t.createXml(); if (!xml) return;
    const int theme = uiTheme; const float byp = bypassParam ? bypassParam->get() : 0.0f;
    const float mo = morphOnRaw->load(), mx = morphXRaw->load(), my = morphYRaw->load(); const auto keepBoard = morphTree();
    juce::MemoryBlock mb; copyXmlToBinary(*xml, mb);
    restoreState(mb.getData(), (int) mb.getSize());   // (the LOAD path: no licence gate, as user sounds)
    uiTheme = theme; if (bypassParam) bypassParam->setValueNotifyingHost(byp > 0.5f ? 1.0f : 0.0f);
    morphFromTree(keepBoard);   // the board is not part of a sound
    for (auto [id, v] : { std::pair<const char*, float> { "morph_on", mo }, { "morph_x", mx }, { "morph_y", my } }) if (auto* p = apvts.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(v));
}

void PoltergeistProcessor::loadPreset(int i) {
    const auto& v = gs::factoryPresets();
    if (!juce::isPositiveAndBelow(i, getNumPrograms())) return;
    snapshotsLeft();
    currentPreset = i;
    if (i >= (int) v.size()) { const int b = i - (int) v.size(); applyPatch(gs::bassPatch(b)); patchName = gs::bassPresets()[(size_t) b].name; return; }
    applyPatch(gs::Summoner::summon(v[(size_t) i].words, v[(size_t) i].seed));
    patchName = v[(size_t) i].name;
}

bool PoltergeistProcessor::loadSampleFile(const juce::File& f, juce::String& err) {
    auto s = gs::SampleData::fromFile(f, err);
    if (!s || s->size() < 64) { if (err.isEmpty()) err = "That file has no sound in it."; return false; }
    setSampleGhost(s);
#if GHOSTY_PROCESS_LAB
    if (const int id = lab.addSource(s)) if (auto o = originalFromFile(f)) labOriginals[id] = o;   // every dropped file also lands in the Sample Lab
#else
    lab.addSource(s);   // every dropped file also lands in the Sample Lab
#endif
    chopLoad(s);        // ... and is AUTO CHOPPED, ready in CHOP mode
    return true;
}

// ---- ADD SAMPLE (the FM page's slot) --------------------------------------------------------------------------------
int PoltergeistProcessor::detectRoot(const gs::SampleData& s) {
    if (s.size() < 2048) return -1;
    const int n = juce::jmin(s.size(), (int) (s.rate * 8.0));   // (the first 8 s are plenty)
    const float m = gs::yinMidi(s.data.data(), n, s.rate);
    return m < 0 ? -1 : juce::jlimit(0, 127, (int) std::lround(m));
}
int PoltergeistProcessor::sampleRoot() const { return juce::jlimit(0, 127, 60 - (int) std::lround(apvts.getRawParameterValue("smp_pitch")->load())); }
void PoltergeistProcessor::setSampleRoot(int note) {
    if (auto* p = apvts.getParameter("smp_pitch")) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1((float) juce::jlimit(-24, 24, 60 - note))); p->endChangeGesture(); }
}
bool PoltergeistProcessor::addSampleFile(const juce::File& f, juce::String& err) {
    auto s = gs::SampleData::fromFile(f, err);
    if (!s || s->size() < 64) { if (err.isEmpty()) err = "That file has no sound in it."; return false; }
    return addSample(s);
}
bool PoltergeistProcessor::addSample(std::shared_ptr<gs::SampleData> s) {
    if (!s || s->size() < 64) return false;
    if (s->frames.empty()) s->buildFrames();
    detectedRoot = detectRoot(*s);
    auto set = [this](const char* id, float v) { if (auto* p = apvts.getParameter(id)) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(v)); p->endChangeGesture(); } };
    // classic playback of the whole file, looped, at its own pitch on its root note
    set("smp_mode", 0.0f); set("smp_start", 0.0f); set("smp_len", 1.0f); set("smp_lst", 0.0f); set("smp_lend", 1.0f);
    set("smp_rev", 0.0f); set("smp_keep", 0.0f); set("smp_track", 1.0f); set("smp_loop", 1.0f); set("mute_sample", 0.0f);
    set("smp_pitch", (float) juce::jlimit(-24, 24, 60 - (detectedRoot >= 0 ? detectedRoot : 60)));
    // silent so far (LEVEL and HAUNT at 0): make it heard and let it haunt its ghost, so the drop does something right away
    if (apvts.getRawParameterValue("smp_level")->load() < 0.01f && apvts.getRawParameterValue("smp_fm")->load() < 0.01f) { set("smp_level", 0.5f); set("smp_fm", 0.3f); }
    setSampleGhost(s);
    snapshotsLeft();
    return true;
}

std::shared_ptr<gs::SampleData> PoltergeistProcessor::withCharacter(std::shared_ptr<gs::SampleData> s) const {
    const int mode = (int) apvts.getRawParameterValue("smp_vc")->load();
    const float amt = apvts.getRawParameterValue("smp_vc_amt")->load(), drv = apvts.getRawParameterValue("smp_vc_drive")->load();
    if (!s || mode <= 0 || amt <= 0.0f) return s;
    // the character is "sampled in" at the sample's own rate, like recording into an old sampler: its aliasing and hold steps
    // then move with the pitch you play it at
    auto c = std::make_shared<gs::SampleData>(*s); c->pitched.clear();
    vc::Character ch; ch.prepare(c->rate, c->size()); ch.setMode(mode); ch.setAmount(amt); ch.setDrive(drv);
    float* p[1] = { c->data.data() }; ch.process(p, 1, c->size());
    c->buildFrames();
    return c;
}

void PoltergeistProcessor::applyCharacter() {
    const float now[3] = { apvts.getRawParameterValue("smp_vc")->load(), apvts.getRawParameterValue("smp_vc_amt")->load(), apvts.getRawParameterValue("smp_vc_drive")->load() };
    if (now[0] == vcApplied[0] && now[1] == vcApplied[1] && now[2] == vcApplied[2]) return;
    if (rawSample) setSampleGhost(rawSample);
    else for (int k = 0; k < 3; ++k) vcApplied[k] = now[k];
    if (rawChopKit) publishChopKit(rawChopKit);
    publishKeys();
}

void PoltergeistProcessor::setSampleGhost(std::shared_ptr<gs::SampleData> raw) {
    rawSample = raw;
    vcApplied[0] = apvts.getRawParameterValue("smp_vc")->load(); vcApplied[1] = apvts.getRawParameterValue("smp_vc_amt")->load(); vcApplied[2] = apvts.getRawParameterValue("smp_vc_drive")->load();
    auto s = withCharacter(raw);
    if (s && s != raw) keepAlive.push_back(s);   // freed on the message thread, never on the audio thread
    engine.setSample(s);
    if (!s) return;
    // keep-tempo copies (-24..+24 semitones, same length) are made in the background, then swapped in
    if (pitchCancel) pitchCancel->store(true);
    auto cancel = std::make_shared<std::atomic<bool>>(false); pitchCancel = cancel;
    const int gen = ++pitchGen;
    auto alive = aliveFlag;
    juce::Thread::launch([this, s, cancel, gen, alive] {
        auto set = gs::pitchedSet(*s, *cancel);
        if (cancel->load() || set.size() != 49) return;
        auto copy = std::make_shared<gs::SampleData>(*s); copy->pitched = std::move(set);
        juce::MessageManager::callAsync([this, copy, gen, alive] { if (!alive->load()) return; if (gen == pitchGen.load() && engine.getSample() && engine.getSample()->name == copy->name) { keepAlive.push_back(copy); engine.setSample(copy); } });
    });
}

int PoltergeistProcessor::labAddFile(const juce::File& f, juce::String& err) {
    auto s = gs::SampleData::fromFile(f, err);
    if (!s || s->size() < 64) { if (err.isEmpty()) err = "That file has no sound in it."; return 0; }
    const int id = lab.addSource(s);
#if GHOSTY_PROCESS_LAB
    if (auto o = originalFromFile(f)) labOriginals[id] = o;   // PROCESS LAB: native rate + channels
#endif
    return id;
}

void PoltergeistProcessor::publishKeys() {
    auto km = std::make_unique<gs::KeyMap>();
    for (int i = 0; i < 128; ++i) {
        keyPlay[i] = keySamples[i] ? withCharacter(keySamples[i]) : nullptr;
        if (keyPlay[i] && keyPlay[i] != keySamples[i]) keepAlive.push_back(keyPlay[i]);
        km->keys[i] = keyPlay[i].get();
    }
    engine.setKeyMap(km.get());
    keymaps.push_back(std::move(km));
}
void PoltergeistProcessor::labMapKey(int note, std::shared_ptr<gs::SampleData> s) {
    if (!juce::isPositiveAndBelow(note, 128) || !s) return;
    keepAlive.push_back(s); keySamples[note] = s; keyNames[note] = s->name; publishKeys();
}
void PoltergeistProcessor::labClearKey(int note) { if (juce::isPositiveAndBelow(note, 128)) { keySamples[note] = nullptr; keyNames[note] = {}; publishKeys(); } }
void PoltergeistProcessor::labClearKeys() { for (int i = 0; i < 128; ++i) { keySamples[i] = nullptr; keyNames[i] = {}; } publishKeys(); }

void PoltergeistProcessor::labRenderPreview() {
    lab.bpm = bpm;
    const int gen = ++renderGen;
    auto copy = std::make_shared<gs::Lab>(lab);   // render a snapshot so editing can carry on
    auto alive = aliveFlag;
    juce::Thread::launch([this, copy, gen, alive] {
        auto r = copy->render("Sample Lab " + juce::String(copy->bars) + " bars");
        juce::MessageManager::callAsync([this, r, gen, alive] {
            if (!alive->load() || gen != renderGen.load()) return;
            keepAlive.push_back(r); labRendered = r; previewPtr.store(r.get());
            if (onLabRendered) onLabRendered();
        });
    });
}

bool PoltergeistProcessor::resampleRecent(double seconds) {
    auto v = engine.recent(seconds);
    float peak = 0; for (float x : v) peak = juce::jmax(peak, std::abs(x));
    if (v.size() < 256 || peak < 0.003f) return false;
    juce::AudioBuffer<float> b(1, (int) v.size());
    b.copyFrom(0, 0, v.data(), (int) v.size());
    auto s = gs::SampleData::fromBuffer(b, engine.sampleRate(), "Resampled " + patchName); s->made = true;   // Poltergeist's own output
    setSampleGhost(s);
    return true;
}

void PoltergeistProcessor::getStateInformation(juce::MemoryBlock& dest) {
    const bool keepMade = savesKeepMade(); leftOut = 0;   // demo: audio the plugin made stays out of every save (Shared/ExportGate.h)
    auto state = apvts.copyState();
    state.setProperty("patchName", patchName, nullptr);
    state.setProperty("theme_name", uiTheme == 1 ? "purple" : uiTheme == 2 ? "light" : "dark", nullptr);
    state.setProperty("uiTheme", uiTheme == 2 ? "light" : "dark", nullptr);   // (the old key, for older builds: PURPLE reads as dark there)
    if (auto smp = rawSample ? std::shared_ptr<const gs::SampleData>(rawSample) : engine.getSample()) {   // the Sample Ghost travels inside the project (as loaded: its character is re-made from the settings)
        if (smp->made && !keepMade) ++leftOut;
        else {
            juce::ValueTree st("SAMPLE");
            st.setProperty("name", smp->name, nullptr); st.setProperty("rate", smp->rate, nullptr); st.setProperty("data", smp->toBase64(), nullptr);
            if (smp->made) st.setProperty("made", true, nullptr);
            state.appendChild(st, nullptr);
        }
    }
    state.appendChild(lab.toTree(keepMade, &leftOut), nullptr);   // the Sample Lab (sources + clips; demo: without the recordings it made)
    {   // the strip's START / END / LOOP per recording (only when set)
        juce::ValueTree mk("LABMARKS");
        for (auto& [id, m] : stripMarks) if (!m.isDefault() && lab.sources.count(id) && (keepMade || !lab.sources.at(id)->made)) { juce::ValueTree c("M"); c.setProperty("id", id, nullptr); c.setProperty("start", m.start, nullptr); c.setProperty("end", m.end, nullptr); c.setProperty("la", m.loopA, nullptr); c.setProperty("lb", m.loopB, nullptr); mk.appendChild(c, nullptr); }
        if (mk.getNumChildren() > 0) state.appendChild(mk, nullptr);
    }
#if GHOSTY_PROCESS_LAB
    if (auto pl = plabState(); pl.isValid()) state.appendChild(pl, nullptr);   // PROCESS LAB chain (the processed audio is the Lab source itself)
    for (auto& [id, o] : labOriginals) if (o && !o->empty() && lab.sources.count(id) && (keepMade || !lab.sources.at(id)->made)) {   // the sources at their own rate / channels (16-bit, max 30 s; demo: not the ones the plugin made)
        juce::ValueTree t("PLABSRC"); t.setProperty("id", id, nullptr); t.setProperty("rate", o->rate, nullptr); t.setProperty("nch", o->channels(), nullptr);
        const int n = juce::jmin(o->length(), (int) (o->rate * 30.0));
        const float sc = juce::jmax(1.0f, o->peak());   // a channel may peak above 1 (the level is set on the mono mix): scaled into 16 bit
        t.setProperty("scale", sc, nullptr);
        for (int c = 0; c < o->channels(); ++c) {
            juce::MemoryBlock mb((size_t) n * 2); auto* p = static_cast<int16_t*>(mb.getData());
            for (int i = 0; i < n; ++i) p[i] = (int16_t) std::lround(juce::jlimit(-32767.0f, 32767.0f, o->ch[(size_t) c][(size_t) i] / sc * 32767.0f));
            t.setProperty("d" + juce::String(c), mb.toBase64Encoding(), nullptr);
        }
        state.appendChild(t, nullptr);
    }
#else
    for (auto& v : plabKept) state.appendChild(v.createCopy(), nullptr);   // a build without PROCESS LAB keeps a project's lab data untouched
#endif
    if (chop.audio) {   // VOCAL CHOP: audio, lines, slice settings, melody (demo: the lines without audio the plugin made)
        if (chop.audio->made && !keepMade) { auto ct = chop.toTree(false); ct.removeProperty("audio", nullptr); state.appendChild(ct, nullptr); ++leftOut; }
        else state.appendChild(chop.toTree(true), nullptr);
    }
    if (!board.ghosts.empty()) state.appendChild(morphTree(true), nullptr);   // the MORPH BOARD (ghosts + their sounds)
    juce::ValueTree keys("KEYS");
    for (int i = 0; i < 128; ++i) if (keySamples[i]) {
        if (keySamples[i]->made && !keepMade) { ++leftOut; continue; }
        juce::ValueTree k("KEY"); k.setProperty("note", i, nullptr); k.setProperty("name", keySamples[i]->name, nullptr);
        k.setProperty("rate", keySamples[i]->rate, nullptr); k.setProperty("data", keySamples[i]->toBase64(), nullptr);
        if (keySamples[i]->made) k.setProperty("made", true, nullptr);
        keys.appendChild(k, nullptr);
    }
    state.appendChild(keys, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, dest);
}

void PoltergeistProcessor::setStateInformation(const void* data, int size) {
    ghosty::StateLoadNotify notifyHost (*this);   // CLAP: tell the host the values changed (Shared/HostNotify.h)
    if (! license.isUnlocked()) return;   // demo mode: a reopened project starts at the default sound (unlocking brings the saved sound back)
    restoreState(data, size);
}
void PoltergeistProcessor::restoreState(const void* data, int size) {
    if (auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName(apvts.state.getType())) {
            auto t = juce::ValueTree::fromXml(*xml);
            patchName = t.getProperty("patchName", "Init").toString();
            {   // the look: theme_name (dark | purple | light); else the old key (light -> LIGHT, anything else -> BLACK); no key -> BLACK
                const auto tn = t.getProperty("theme_name").toString().trim().toLowerCase();
                if (tn == "purple") uiTheme = 1; else if (tn == "light") uiTheme = 2; else if (tn == "dark") uiTheme = 0;
                else uiTheme = t.getProperty("uiTheme", "dark").toString() == "light" ? 2 : 0;
            }
            auto st = t.getChildWithName("SAMPLE");
            if (st.isValid()) {
                auto smp = gs::SampleData::fromBase64(st.getProperty("data").toString(), (double) st.getProperty("rate", 44100.0), st.getProperty("name").toString());
                gs::SampleData::readMade(smp.get(), st);
                setSampleGhost(smp);
                t.removeChild(st, nullptr);
            }
#if GHOSTY_PROCESS_LAB
            labOriginals.clear();
            for (int k = t.getNumChildren(); --k >= 0;) {
                auto c = t.getChild(k); if (!c.hasType("PLABSRC")) continue;
                auto o = std::make_shared<plab::Audio>(); o->rate = (double) c.getProperty("rate", 44100.0); const float sc = (float) c.getProperty("scale", 1.0);
                for (int ch = 0; ch < juce::jlimit(1, 8, (int) c.getProperty("nch", 1)); ++ch) {
                    juce::MemoryBlock mb; if (!mb.fromBase64Encoding(c.getProperty("d" + juce::String(ch)).toString())) break;
                    const int n = (int) (mb.getSize() / 2); auto* p = static_cast<const int16_t*>(mb.getData());
                    std::vector<float> v((size_t) n); for (int i = 0; i < n; ++i) v[(size_t) i] = p[i] / 32767.0f * sc; o->ch.push_back(std::move(v));
                }
                if (!o->empty()) labOriginals[(int) c.getProperty("id")] = o;
                t.removeChild(k, nullptr);
            }
            auto pt = t.getChildWithName("PLAB");
            if (pt.isValid()) { setPlabState(pt); t.removeChild(pt, nullptr); }
#else
            plabKept.clear();
            for (int k = t.getNumChildren(); --k >= 0;) { auto c = t.getChild(k); if (c.hasType("PLAB") || c.hasType("PLABSRC")) { plabKept.insert(plabKept.begin(), c.createCopy()); t.removeChild(k, nullptr); } }
#endif
            stripMarks.clear();
            if (auto mk = t.getChildWithName("LABMARKS"); mk.isValid()) {
                for (auto c : mk) { StripMarks m; m.start = c.getProperty("start", 0.0); m.end = c.getProperty("end", -1.0); m.loopA = c.getProperty("la", 0.0); m.loopB = c.getProperty("lb", 0.0); stripMarks[(int) c.getProperty("id")] = m; }
                t.removeChild(mk, nullptr);
            }
            auto lt = t.getChildWithName("LAB");
            if (lt.isValid()) { lab.fromTree(lt); t.removeChild(lt, nullptr); }
            auto ct = t.getChildWithName("CHOP");
            if (ct.isValid()) { chop.fromTree(ct); chopUndoStack.clear(); chopRedoStack.clear(); chopCache.clear(); chopChanged(); t.removeChild(ct, nullptr); }
            { auto mt = t.getChildWithName("MORPH"); morphFromTree(mt); if (mt.isValid()) t.removeChild(mt, nullptr); }   // (no board saved: an empty board)
            auto kt = t.getChildWithName("KEYS");
            if (kt.isValid()) {
                for (int i = 0; i < 128; ++i) { keySamples[i] = nullptr; keyNames[i] = {}; }
                for (auto k : kt) { const int n = k.getProperty("note"); if (auto s = gs::SampleData::fromBase64(k.getProperty("data").toString(), (double) k.getProperty("rate", 44100.0), k.getProperty("name").toString()); s && juce::isPositiveAndBelow(n, 128)) { gs::SampleData::readMade(s.get(), k); keySamples[n] = s; keyNames[n] = s->name; keepAlive.push_back(s); } }
                publishKeys();
                t.removeChild(kt, nullptr);
            }
            // a project from before the BASS update has no values for the new parameters: give them their (off / neutral)
            // defaults, otherwise they would keep whatever the current sound had and the old project would sound different
            for (auto* prm : getParameters())
                if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(prm))
                    if (!t.getChildWithProperty("id", rp->getParameterID()).isValid()) {
                        juce::ValueTree c("PARAM"); c.setProperty("id", rp->getParameterID(), nullptr);
                        c.setProperty("value", rp->convertFrom0to1(rp->getDefaultValue()), nullptr); t.appendChild(c, nullptr);
                    }
            apvts.replaceState(t);
            applyCharacter();   // the sample was restored before its character settings
        }
}

// ---- VOCAL CHOP ------------------------------------------------------------------------------------------------
void PoltergeistProcessor::chopLoad(std::shared_ptr<gs::SampleData> s, bool undoable) {
    if (!s || s->size() < 64) return;
    if (undoable) chopBegin();
    chop.audio = s; chop.autoChop();
    chopChanged();
}
void PoltergeistProcessor::chopBegin() {
    chopUndoStack.push_back(chop); chopRedoStack.clear();
    if (chopUndoStack.size() > 64) chopUndoStack.erase(chopUndoStack.begin());
}
void PoltergeistProcessor::chopPushUndo(const gs::ChopState& before) {
    chopUndoStack.push_back(before); chopRedoStack.clear();
    if (chopUndoStack.size() > 64) chopUndoStack.erase(chopUndoStack.begin());
}
static bool sameClips(const std::vector<gs::LabClip>& a, const std::vector<gs::LabClip>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const auto &x = a[i], &y = b[i];
        if (x.id != y.id || x.source != y.source || x.srcStart != y.srcStart || x.srcEnd != y.srcEnd || x.start != y.start || x.len != y.len || x.pitch != y.pitch
            || x.gainDb != y.gainDb || x.reverse != y.reverse || x.headPitch != y.headPitch || x.formant != y.formant || x.marks.size() != y.marks.size()) return false;
        for (size_t k = 0; k < x.marks.size(); ++k) if (x.marks[k].src != y.marks[k].src || x.marks[k].out != y.marks[k].out || x.marks[k].pitch != y.marks[k].pitch) return false;
    }
    return true;
}
void PoltergeistProcessor::labPushUndo(const std::vector<gs::LabClip>& before) {
    if (sameClips(before, lab.clips)) return;   // nothing changed
    labUndoStack.push_back(before); labRedoStack.clear();
    if (labUndoStack.size() > 64) labUndoStack.erase(labUndoStack.begin());
}
void PoltergeistProcessor::labPlaySection(double beatA, double beatB) {
    labRenderNow();
    const int n = labRendered->size(), a = juce::jlimit(0, n, (int) std::lround(beatA * lab.beatSec() * gs::Lab::kRate)), b = juce::jlimit(a, n, (int) std::lround(beatB * lab.beatSec() * gs::Lab::kRate));
    if (b - a < 64) return;
    previewOriginal.store(false); previewLoopA.store(-1); previewLoopB.store(-1); previewIsSection.store(true);
    previewRangeA.store(a); previewRangeB.store(b); previewRestart.store(true); previewOn.store(true);
}
void PoltergeistProcessor::labPlayOriginalRange(double aSec, double bSec) {
    auto it = lab.sources.find(lab.current); if (it == lab.sources.end()) return;
    const auto& s = *it->second; const int n = s.size();
    const int a = juce::jlimit(0, n, (int) std::lround(aSec * s.rate)), b = juce::jlimit(a, n, (int) std::lround(bSec * s.rate)); if (b - a < 64) return;
    keepAlive.push_back(it->second); originalPtr.store(it->second.get());
    const auto m = marksOf(lab.current); const int la = (int) std::lround(m.loopA * s.rate), lb = (int) std::lround(m.loopB * s.rate);
    const bool inLoop = m.loopB > m.loopA && la >= a && lb <= b && lb - la >= 64;
    previewOriginal.store(true); previewLoopA.store(inLoop ? la : -1); previewLoopB.store(inLoop ? lb : -1); previewIsSection.store(true);
    previewRangeA.store(a); previewRangeB.store(b); previewRestart.store(true); previewOn.store(true);
}
bool PoltergeistProcessor::stripToKeys(double aSec, double bSec, const juce::String& name) {
    auto it = lab.sources.find(lab.current); if (it == lab.sources.end()) return false;
    const auto m = marksOf(lab.current); const double len = it->second->size() / it->second->rate;
    if (!(bSec > aSec)) { aSec = juce::jlimit(0.0, len, m.start); bSec = m.end < 0 ? len : juce::jlimit(aSec, len, m.end); }   // no highlight: START..END
    auto s = lab.section(lab.current, aSec, bSec, (name.isNotEmpty() ? name : it->second->name) + " cut"); if (!s) return false;
    setSampleGhost(s);
    auto set = [this](const char* id, float v) { if (auto* p = apvts.getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(v)); };
    if (apvts.getRawParameterValue("smp_level")->load() < 0.01f) set("smp_level", 0.9f);
    set("smp_mode", 0);   // (as HIGHLIGHT ON KEYS always did; START / LENGTH stay as they are)
    if (m.loopB > m.loopA && m.loopA >= aSec - 1e-6 && m.loopB <= bSec + 1e-6) {   // the strip's LOOP inside it -> the sampler's LOOP (only when one is set)
        set("smp_loop", 1); set("smp_lst", (float) ((m.loopA - aSec) / (bSec - aSec))); set("smp_lend", (float) ((m.loopB - aSec) / (bSec - aSec)));
    }
    return true;
}
void PoltergeistProcessor::labPlay(bool on) {
    previewRangeA.store(-1); previewRangeB.store(-1); previewLoopA.store(-1); previewLoopB.store(-1); previewIsSection.store(false);
    if (on) { if (auto it = lab.sources.find(lab.current); it != lab.sources.end()) { keepAlive.push_back(it->second); originalPtr.store(it->second.get());
                  const auto m = marksOf(lab.current);
                  if (!m.isDefault()) {   // ORIGINAL plays the strip's START..END, with its LOOP region (when LOOP is on)
                      const auto& s = *it->second; const int n = s.size(), a = juce::jlimit(0, n, (int) std::lround(m.start * s.rate)), b = m.end < 0 ? n : juce::jlimit(a, n, (int) std::lround(m.end * s.rate));
                      const int la = (int) std::lround(m.loopA * s.rate), lb = (int) std::lround(m.loopB * s.rate);
                      originalRange[0] = a; originalRange[1] = b; originalRange[2] = m.loopB > m.loopA && la >= a && lb <= b && lb - la >= 64 ? la : -1; originalRange[3] = originalRange[2] >= 0 ? lb : -1;
                  } else { originalRange[0] = originalRange[1] = originalRange[2] = originalRange[3] = -1; }
                  originalStart.store(originalRange[0]); originalEnd.store(originalRange[1]); originalLoopA.store(originalRange[2]); originalLoopB.store(originalRange[3]); }
              if (!labRendered) labRenderNow(); previewRestart.store(true); }
    previewOn.store(on);
}
void PoltergeistProcessor::labRenderNow() {
    lab.bpm = bpm; ++renderGen;   // (any render still running is dropped)
    auto r = lab.render("Sample Lab " + juce::String(lab.bars) + " bars");
    keepAlive.push_back(r); labRendered = r; previewPtr.store(r.get());
    if (onLabRendered) onLabRendered();
}
bool PoltergeistProcessor::labUseOnKeys() {
    if (lab.clips.empty()) return false;
    labRenderNow();
    auto s = std::make_shared<gs::SampleData>(*labRendered); s->name = "My Lab edit";
    return useSoundOnKeys(s);
}
int PoltergeistProcessor::labEditToRecording() {
    if (lab.clips.empty()) return 0;
    labRenderNow();
    auto s = std::make_shared<gs::SampleData>(*labRendered); s->name = "My Lab edit " + juce::Time::getCurrentTime().formatted("%H:%M");
    return lab.addSource(s);   // (becomes the current recording)
}
bool PoltergeistProcessor::useSoundOnKeys(std::shared_ptr<gs::SampleData> s) {
    if (!s || s->size() < 64) return false;
    setSampleGhost(s);
    auto set = [this](const char* id, float v) { if (auto* p = apvts.getParameter(id)) { p->beginChangeGesture(); p->setValueNotifyingHost(p->convertTo0to1(v)); p->endChangeGesture(); } };
    set("smp_mode", 0); set("smp_start", 0); set("smp_len", 1); set("smp_lst", 0); set("smp_lend", 1); set("smp_loop", previewLoop.load() ? 1.0f : 0.0f);
    if (apvts.getRawParameterValue("smp_level")->load() < 0.01f) set("smp_level", 0.9f);
    return true;
}
bool PoltergeistProcessor::labUndo() { if (labUndoStack.empty()) return false; labRedoStack.push_back(lab.clips); lab.clips = labUndoStack.back(); labUndoStack.pop_back(); return true; }
bool PoltergeistProcessor::labRedo() { if (labRedoStack.empty()) return false; labUndoStack.push_back(lab.clips); lab.clips = labRedoStack.back(); labRedoStack.pop_back(); return true; }
bool PoltergeistProcessor::chopUndo() {
    if (chopUndoStack.empty()) return false;
    chopRedoStack.push_back(chop); chop = chopUndoStack.back(); chopUndoStack.pop_back(); chopChanged(); return true;
}
bool PoltergeistProcessor::chopRedo() {
    if (chopRedoStack.empty()) return false;
    chopUndoStack.push_back(chop); chop = chopRedoStack.back(); chopRedoStack.pop_back(); chopChanged(); return true;
}
std::shared_ptr<gs::ChopKit> PoltergeistProcessor::withCharacter(std::shared_ptr<gs::ChopKit> k) const {
    const int mode = (int) apvts.getRawParameterValue("smp_vc")->load();
    const float amt = apvts.getRawParameterValue("smp_vc_amt")->load(), drv = apvts.getRawParameterValue("smp_vc_drive")->load();
    if (!k || mode <= 0 || amt <= 0.0f) return k;
    auto c = std::make_shared<gs::ChopKit>(*k);   // same keys / flags; each slice re-printed with the character
    vc::Character ch; ch.prepare(k->rate, 512); ch.setMode(mode); ch.setAmount(amt); ch.setDrive(drv);
    for (int i = 0; i < c->n && i < gs::ChopKit::kMax; ++i) {
        auto& sl = c->slot[i]; if (!sl.d || sl.len < 2) continue;
        auto v = std::make_shared<std::vector<float>>(sl.d, sl.d + sl.len);
        ch.reset(); float* p[1] = { v->data() }; ch.process(p, 1, sl.len);
        sl.d = v->data(); c->hold.push_back(v);
    }
    return c;
}
void PoltergeistProcessor::publishChopKit(std::shared_ptr<gs::ChopKit> raw) {
    if (!raw) return;
    rawChopKit = raw;
    auto k = withCharacter(raw);
    chopKits.push_back(k); chopKitUi = k; engine.setChopKit(k.get());
    chopPublishPattern();
    if (onChopRendered) onChopRendered();
}
void PoltergeistProcessor::chopChanged() {
    chopPublishPattern();
    const int gen = ++chopGen;
    auto copy = std::make_shared<gs::ChopState>(chop); auto cache = std::make_shared<gs::ChopState::Cache>(chopCache);
    auto alive = aliveFlag;
    juce::Thread::launch([this, copy, cache, gen, alive] {
        if (!alive->load() || gen != chopGen.load()) return;
        auto kit = copy->buildKit(*cache);
        juce::MessageManager::callAsync([this, kit, cache, gen, alive] {
            if (!alive->load() || gen != chopGen.load()) return;
            chopCache = *cache; publishChopKit(kit);
        });
    });
}
void PoltergeistProcessor::chopRenderNow() { ++chopGen; publishChopKit(chop.buildKit(chopCache)); }
void PoltergeistProcessor::chopBounce() {
    if (!chop.audio || chop.slices.empty()) return;
    chopBegin(); chop.bounce(chopCache); chopChanged();
}
void PoltergeistProcessor::chopPublishPattern() {
    chopNumSteps.store(chop.steps == 8 ? 8 : 16);
    for (int i = 0; i < 16; ++i) {
        const auto& st = chop.pattern[i]; uint32_t code = 0;
        if (st.on && st.slice < chop.count()) { const int note = chop.noteFor(st.slice); if (juce::isPositiveAndBelow(note, 128)) code = 1u | ((uint32_t) juce::jlimit(1, 127, (int) (st.vel * 127)) << 1) | ((uint32_t) note << 8); }
        chopSteps[i].store(code);
    }
}
// audio thread: CHOP MELODY steps on the 1/16 grid (host-locked while the host plays)
void PoltergeistProcessor::chopSequence(juce::MidiBuffer& m, int n) {
    if (!chopPlay.load()) {
        if (chopWasPlaying) { if (chopLastNote >= 0) m.addEvent(juce::MidiMessage::noteOff(1, chopLastNote), 0); chopLastNote = -1; chopLastStep = -1; chopStepNow.store(-1); chopWasPlaying = false; }
        return;
    }
    if (!chopWasPlaying) { chopWasPlaying = true; chopClock = 0; chopLastStep = -1; }
    const double sps = juce::jmax(1.0, 60.0 / juce::jlimit(30.0, 400.0, bpm) / 4.0 * engine.sampleRate());
    const int steps = juce::jmax(1, chopNumSteps.load());
    const double s0 = playing ? ppq * 4.0 : chopClock / sps;
    for (int i = 0; i < n; ++i) {
        const long st = (long) std::floor(s0 + i / sps);
        if (st == chopLastStep) continue;
        chopLastStep = st;
        const int idx = (int) (((st % steps) + steps) % steps);
        const uint32_t code = chopSteps[idx].load();
        if (chopLastNote >= 0) { m.addEvent(juce::MidiMessage::noteOff(1, chopLastNote), i); chopLastNote = -1; }
        if (code & 1u) { const int note = (int) ((code >> 8) & 127), vel = (int) ((code >> 1) & 127); m.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8) vel), i); chopLastNote = note; }
        chopStepNow.store(idx);
    }
    if (!playing) chopClock += n;
}
// the melody bounced offline straight from the rendered slices (same choke / gate / velocity rules as the engine)
juce::AudioProcessorEditor* PoltergeistProcessor::createEditor() { return new PoltergeistEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PoltergeistProcessor(); }

// ---- MORPH BOARD ------------------------------------------------------------------------------------------------
PoltergeistProcessor::GhostSound PoltergeistProcessor::soundFrom(const juce::String& source) {
    GhostSound g; g.source = source;
    if (source.startsWith("snap:")) {
        auto& b = snapshots(); const int i = b.indexOfFile(b.folder().getChildFile(source.fromFirstOccurrenceOf("snap:", false, false)));
        const auto st = b.stateOf(i); if (!st.isValid()) return {};
        g.values = morph.valuesFromState(st);
        if (auto sm = st.getChildWithName("SAMPLE"); sm.isValid()) { g.sample = gs::SampleData::fromBase64(sm.getProperty("data").toString(), (double) sm.getProperty("rate", 44100.0), sm.getProperty("name").toString()); gs::SampleData::readMade(g.sample.get(), sm); }
        return g;
    }
    if (source.startsWith("preset:")) {
        const int i = source.fromFirstOccurrenceOf("preset:", false, false).getIntValue(); if (!juce::isPositiveAndBelow(i, getNumPrograms())) return {};
        const auto& v = gs::factoryPresets(); const auto patch = i < (int) v.size() ? gs::Summoner::summon(v[(size_t) i].words, v[(size_t) i].seed) : gs::bassPatch(i - (int) v.size());
        juce::ValueTree t("S"); for (auto& [id, val] : patch) { juce::ValueTree c("PARAM"); c.setProperty("id", juce::String(id), nullptr); c.setProperty("value", val, nullptr); t.appendChild(c, nullptr); }
        if (i < (int) v.size()) { juce::ValueTree c("PARAM"); c.setProperty("id", "eng_ver", nullptr); c.setProperty("value", 0, nullptr); t.appendChild(c, nullptr); }
        else { juce::ValueTree c("PARAM"); c.setProperty("id", "eng_ver", nullptr); c.setProperty("value", (int) gs::kEngVer, nullptr); t.appendChild(c, nullptr); }
        g.values = morph.valuesFromState(t);
        return g;
    }
    g.source = "current"; g.values = morph.currentValues(); g.sample = rawSample;
    return g;
}
int PoltergeistProcessor::morphAddGhost(float x, float y, const juce::String& source) {
    auto gsd = soundFrom(source); if (gsd.values.empty()) return -1;
    const int i = board.add(x, y); if (i < 0) return -1;
    ghostSounds[board.ghosts[(size_t) i].id] = gsd; board.ghosts[(size_t) i].userData = gsd.source;
    morphPublish(); return i;
}
bool PoltergeistProcessor::morphSetGhost(int i, const juce::String& source) {
    if (!juce::isPositiveAndBelow(i, (int) board.ghosts.size())) return false;
    auto gsd = soundFrom(source); if (gsd.values.empty()) return false;
    ghostSounds[board.ghosts[(size_t) i].id] = gsd; board.ghosts[(size_t) i].userData = gsd.source; morphLeader = -1;
    morphPublish(); return true;
}
void PoltergeistProcessor::morphRemoveGhost(int i) {
    if (!juce::isPositiveAndBelow(i, (int) board.ghosts.size())) return;
    ghostSounds.erase(board.ghosts[(size_t) i].id); board.remove(i); morphLeader = -1; morphPublish();
}
void PoltergeistProcessor::morphPublish() {
    std::vector<std::vector<float>> t;
    for (auto& g : board.ghosts) { auto it = ghostSounds.find(g.id); t.push_back(it != ghostSounds.end() ? it->second.values : morph.currentValues()); }
    morph.setTable(t);   // (table first, then the positions: the audio thread never sees more ghosts than rows)
    morph.shared.publish(board);
}
void PoltergeistProcessor::morphApplyBlend() {
    if (board.ghosts.empty()) return;
    std::vector<float> w; board.weightsAt(apvts.getRawParameterValue("morph_x")->load(), apvts.getRawParameterValue("morph_y")->load(), w);
    int best = 0; for (int k = 1; k < (int) w.size(); ++k) if (w[(size_t) k] > w[(size_t) best]) best = k;
    for (int i = 0; i < morph.n; ++i) {
        auto& p = morph.params[(size_t) i]; if (p.keep) continue;
        float v = 0;
        if (p.discrete) { auto it = ghostSounds.find(board.ghosts[(size_t) best].id); if (it == ghostSounds.end()) continue; v = it->second.values[(size_t) i]; }
        else for (int k = 0; k < (int) w.size(); ++k) { auto it = ghostSounds.find(board.ghosts[(size_t) k].id); if (it != ghostSounds.end()) v += w[(size_t) k] * it->second.values[(size_t) i]; }
        p.prm->beginChangeGesture(); p.prm->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, v)); p.prm->endChangeGesture();
    }
    if (auto it = ghostSounds.find(board.ghosts[(size_t) best].id); it != ghostSounds.end() && it->second.sample) setSampleGhost(it->second.sample);
    if (auto* p = apvts.getParameter("morph_on")) p->setValueNotifyingHost(0.0f);
}
void PoltergeistProcessor::morphTimer() {   // the leading ghost's sample goes on the Sample Ghost (the normal sample path)
    if (!morphActive()) { morphLeader = -1; return; }
    const int l = morph.leaderOut.load();
    if (l == morphLeader || !juce::isPositiveAndBelow(l, (int) board.ghosts.size())) return;
    morphLeader = l;
    if (auto it = ghostSounds.find(board.ghosts[(size_t) l].id); it != ghostSounds.end() && it->second.sample && it->second.sample != rawSample) setSampleGhost(it->second.sample);
}
juce::ValueTree PoltergeistProcessor::morphTree(bool forSave) const {   // forSave: a save (demo: no audio the plugin made); else an in-memory copy
    juce::ValueTree t("MORPH"); t.appendChild(board.toTree(), nullptr);
    for (auto& g : board.ghosts) {
        auto it = ghostSounds.find(g.id); if (it == ghostSounds.end()) continue;
        juce::ValueTree c("GSOUND"); c.setProperty("id", g.id, nullptr); c.setProperty("source", it->second.source, nullptr);
        juce::StringArray vs; for (auto v : it->second.values) vs.add(juce::String(v, 7));
        c.setProperty("ids", [&] { juce::StringArray ids; for (auto& p : morph.params) ids.add(p.prm->getParameterID()); return ids.joinIntoString(" "); }(), nullptr);
        c.setProperty("values", vs.joinIntoString(" "), nullptr);   // (readable) ...
        c.setProperty("bits", juce::MemoryBlock(it->second.values.data(), it->second.values.size() * sizeof(float)).toBase64Encoding(), nullptr);   // ... and bit-exact
        if (auto s = it->second.sample) {
            if (forSave && s->made && !savesKeepMade()) ++leftOut;   // demo: a ghost keeps its settings, not audio the plugin made
            else { c.setProperty("sname", s->name, nullptr); c.setProperty("srate", s->rate, nullptr); c.setProperty("sdata", s->toBase64(), nullptr); if (s->made) c.setProperty("smade", true, nullptr); }
        }
        t.appendChild(c, nullptr);
    }
    return t;
}
void PoltergeistProcessor::morphFromTree(const juce::ValueTree& t) {
    board = ghosty::spirit::Board::fromTree(t.getChildWithName("SPIRITBOARD")); ghostSounds.clear(); morphLeader = -1;
    for (auto c : t) {
        if (!c.hasType("GSOUND")) continue;
        GhostSound g; g.source = c.getProperty("source").toString(); g.values.resize((size_t) morph.n);
        for (int i = 0; i < morph.n; ++i) g.values[(size_t) i] = morph.params[(size_t) i].prm->getDefaultValue();
        const auto ids = juce::StringArray::fromTokens(c.getProperty("ids").toString(), " ", ""), vs = juce::StringArray::fromTokens(c.getProperty("values").toString(), " ", "");
        for (int k = 0; k < juce::jmin(ids.size(), vs.size()); ++k) if (auto it = morph.index.find(ids[k]); it != morph.index.end()) g.values[(size_t) it->second] = juce::jlimit(0.0f, 1.0f, vs[k].getFloatValue());   // (by id: survives new parameters)
        { juce::MemoryBlock mb; if (mb.fromBase64Encoding(c.getProperty("bits").toString()) && (int) (mb.getSize() / sizeof(float)) == ids.size()) {
              const auto* f = static_cast<const float*>(mb.getData());
              for (int k = 0; k < ids.size(); ++k) if (auto it = morph.index.find(ids[k]); it != morph.index.end() && std::isfinite(f[k])) g.values[(size_t) it->second] = juce::jlimit(0.0f, 1.0f, f[k]); } }
        if (c.hasProperty("sdata")) { g.sample = gs::SampleData::fromBase64(c.getProperty("sdata").toString(), (double) c.getProperty("srate", 44100.0), c.getProperty("sname").toString()); gs::SampleData::readMade(g.sample.get(), c, "smade"); }
        ghostSounds[c.getProperty("id").toString()] = g;
    }
    morphPublish();
}
bool PoltergeistProcessor::saveBoard(const juce::String& name) {
    leftOut = 0; auto xml = morphTree(true).createXml(); if (!xml || name.trim().isEmpty()) return false;
    tellLeftOut("save:polt.board");
    return xml->writeTo(boardsDir().getChildFile(juce::File::createLegalFileName(name.trim()) + ".ghostboard"));
}
bool PoltergeistProcessor::loadBoard(const juce::File& f) {
    auto xml = juce::XmlDocument::parse(f); if (!xml || !xml->hasTagName("MORPH")) return false;
    morphFromTree(juce::ValueTree::fromXml(*xml)); return true;
}
