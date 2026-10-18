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
}

void PoltergeistProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    keyboard.processNextMidiBuffer(midi, 0, buffer.getNumSamples(), true);
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition()) {
            if (auto b = pos->getBpm()) engine.setBpm(*b);
            engine.setPpq(pos->getPpqPosition().orFallback(0.0), pos->getIsPlaying());
        }
    reader.read(snap);
    if (buffer.getNumChannels() < 2) { buffer.clear(); return; }
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

void PoltergeistProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto state = apvts.copyState();
    state.setProperty("patchName", patchName, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, dest);
}

void PoltergeistProcessor::setStateInformation(const void* data, int size) {
    if (auto xml = getXmlFromBinary(data, size))
        if (xml->hasTagName(apvts.state.getType())) {
            auto t = juce::ValueTree::fromXml(*xml);
            patchName = t.getProperty("patchName", "Init").toString();
            apvts.replaceState(t);
        }
}

juce::AudioProcessorEditor* PoltergeistProcessor::createEditor() { return new PoltergeistEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PoltergeistProcessor(); }
