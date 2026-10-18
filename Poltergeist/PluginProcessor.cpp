#include "PluginProcessor.h"
#include "PluginEditor.h"

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
    engine.setSample(s);
    return true;
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
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, dest);
}

void PoltergeistProcessor::setStateInformation(const void* data, int size) {
    if (auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName(apvts.state.getType())) {
            auto t = juce::ValueTree::fromXml(*xml);
            patchName = t.getProperty("patchName", "Init").toString();
            auto st = t.getChildWithName("SAMPLE");
            if (st.isValid()) {
                engine.setSample(gs::SampleData::fromBase64(st.getProperty("data").toString(), (double) st.getProperty("rate", 44100.0), st.getProperty("name").toString()));
                t.removeChild(st, nullptr);
            }
            apvts.replaceState(t);
        }
}

juce::AudioProcessorEditor* PoltergeistProcessor::createEditor() { return new PoltergeistEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PoltergeistProcessor(); }
