#include "PluginProcessor.h"
#include "PluginEditor.h"

PoltergeistProcessor::~PoltergeistProcessor() { aliveFlag->store(false); if (pitchCancel) pitchCancel->store(true); }

PoltergeistProcessor::PoltergeistProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "POLTERGEIST", gs::createLayout()) {
    reader.attach(apvts);
    summonSeed = (uint32_t) juce::Random::getSystemRandom().nextInt();
    loadPreset(0);
}

void PoltergeistProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    engine.prepare(sampleRate, juce::jmax(16, samplesPerBlock));
    setLatencySamples(engine.latency());
    keyboard.reset();
    midiFx.reset();
}

void PoltergeistProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    keyboard.processNextMidiBuffer(midi, 0, buffer.getNumSamples(), true);
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition()) {
            if (auto b = pos->getBpm()) { bpm = *b; engine.setBpm(*b); }
            ppq = pos->getPpqPosition().orFallback(0.0); playing = pos->getIsPlaying();
            engine.setPpq(ppq, playing);
        }
    reader.read(snap);
    if (buffer.getNumChannels() < 2) { buffer.clear(); return; }
    midiFx.process(midi, buffer.getNumSamples(), snap, getSampleRate(), bpm, ppq, playing);
    engine.process(buffer, midi, snap);
    // Sample Lab preview: the bounced timeline looped under everything, locked to the host beat while playing
    if (previewOn.load())
        if (auto* pv = previewPtr.load()) {
            const int N = pv->size();
            if (N > 0) {
                const double inc = pv->rate / getSampleRate();
                if (playing) { const double spb = 60.0 / bpm * pv->rate; previewPos = std::fmod(ppq * spb, (double) N); }
                for (int i = 0; i < buffer.getNumSamples(); ++i) {
                    const float x = pv->at(previewPos) * 0.8f;
                    buffer.addSample(0, i, x); buffer.addSample(1, i, x);
                    previewPos += inc; if (previewPos >= N) previewPos -= N;
                }
            }
        }
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
    summonSeed = summonSeed * 1664525u + 1013904223u;
    juce::String cat;
    applyPatch(gs::Summoner::summon(words, summonSeed, &cat));
    patchName = words.trim().isEmpty() ? "Summoned " + cat : words.trim();
    return cat;
}

void PoltergeistProcessor::loadPreset(int i) {
    const auto& v = gs::factoryPresets();
    if (!juce::isPositiveAndBelow(i, (int) v.size())) return;
    currentPreset = i;
    applyPatch(gs::Summoner::summon(v[(size_t) i].words, v[(size_t) i].seed));
    patchName = v[(size_t) i].name;
}

bool PoltergeistProcessor::loadSampleFile(const juce::File& f, juce::String& err) {
    auto s = gs::SampleData::fromFile(f, err);
    if (!s || s->size() < 64) { if (err.isEmpty()) err = "That file has no sound in it."; return false; }
    setSampleGhost(s);
    lab.addSource(s);   // every dropped file also lands in the Sample Lab
    return true;
}

void PoltergeistProcessor::setSampleGhost(std::shared_ptr<gs::SampleData> s) {
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
    return lab.addSource(s);
}

void PoltergeistProcessor::publishKeys() {
    auto km = std::make_unique<gs::KeyMap>();
    for (int i = 0; i < 128; ++i) km->keys[i] = keySamples[i].get();
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

juce::File PoltergeistProcessor::writeTempWav(const gs::SampleData& s, const juce::String& name) {
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("Poltergeist");
    dir.createDirectory();
    auto f = dir.getChildFile(juce::File::createLegalFileName(name) + ".wav");
    f.deleteFile();
    juce::AudioBuffer<float> b(1, s.size()); b.copyFrom(0, 0, s.data.data(), s.size());
    juce::WavAudioFormat wav;
    if (auto os = std::unique_ptr<juce::OutputStream>(f.createOutputStream()))
        if (auto* w = wav.createWriterFor(os.get(), s.rate, 1, 24, {}, 0)) { os.release(); std::unique_ptr<juce::AudioFormatWriter> wr(w); wr->writeFromAudioSampleBuffer(b, 0, s.size()); }
    return f;
}

bool PoltergeistProcessor::resampleRecent(double seconds) {
    auto v = engine.recent(seconds);
    float peak = 0; for (float x : v) peak = juce::jmax(peak, std::abs(x));
    if (v.size() < 256 || peak < 0.003f) return false;
    juce::AudioBuffer<float> b(1, (int) v.size());
    b.copyFrom(0, 0, v.data(), (int) v.size());
    engine.setSample(gs::SampleData::fromBuffer(b, engine.sampleRate(), "Resampled " + patchName));
    return true;
}

void PoltergeistProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto state = apvts.copyState();
    state.setProperty("patchName", patchName, nullptr);
    if (auto smp = engine.getSample()) {   // the Sample Ghost travels inside the project (16-bit, up to 30 s)
        juce::ValueTree st("SAMPLE");
        st.setProperty("name", smp->name, nullptr); st.setProperty("rate", smp->rate, nullptr); st.setProperty("data", smp->toBase64(), nullptr);
        state.appendChild(st, nullptr);
    }
    state.appendChild(lab.toTree(), nullptr);   // the Sample Lab (sources + clips)
    juce::ValueTree keys("KEYS");
    for (int i = 0; i < 128; ++i) if (keySamples[i]) {
        juce::ValueTree k("KEY"); k.setProperty("note", i, nullptr); k.setProperty("name", keySamples[i]->name, nullptr);
        k.setProperty("rate", keySamples[i]->rate, nullptr); k.setProperty("data", keySamples[i]->toBase64(), nullptr); keys.appendChild(k, nullptr);
    }
    state.appendChild(keys, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, dest);
}

void PoltergeistProcessor::setStateInformation(const void* data, int size) {
    if (auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName(apvts.state.getType())) {
            auto t = juce::ValueTree::fromXml(*xml);
            patchName = t.getProperty("patchName", "Init").toString();
            auto st = t.getChildWithName("SAMPLE");
            if (st.isValid()) {
                setSampleGhost(gs::SampleData::fromBase64(st.getProperty("data").toString(), (double) st.getProperty("rate", 44100.0), st.getProperty("name").toString()));
                t.removeChild(st, nullptr);
            }
            auto lt = t.getChildWithName("LAB");
            if (lt.isValid()) { lab.fromTree(lt); t.removeChild(lt, nullptr); }
            auto kt = t.getChildWithName("KEYS");
            if (kt.isValid()) {
                for (int i = 0; i < 128; ++i) { keySamples[i] = nullptr; keyNames[i] = {}; }
                for (auto k : kt) { const int n = k.getProperty("note"); if (auto s = gs::SampleData::fromBase64(k.getProperty("data").toString(), (double) k.getProperty("rate", 44100.0), k.getProperty("name").toString()); s && juce::isPositiveAndBelow(n, 128)) { keySamples[n] = s; keyNames[n] = s->name; keepAlive.push_back(s); } }
                publishKeys();
                t.removeChild(kt, nullptr);
            }
            apvts.replaceState(t);
        }
}

juce::AudioProcessorEditor* PoltergeistProcessor::createEditor() { return new PoltergeistEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PoltergeistProcessor(); }
