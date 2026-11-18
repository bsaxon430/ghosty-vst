#include "BlockStepProcessor.h"
#include "BlockStepEditor.h"
#include "BlockStepBin.h"
#include <unordered_map>

// ---------------------------------------------------------------- kit (shared)
const void* BlockStepKit::resource(const juce::String& originalFilename, int& size) {
    static const std::unordered_map<std::string, int> index = [] {
        std::unordered_map<std::string, int> m;
        for (int i = 0; i < BlockStepBin::namedResourceListSize; ++i) m[BlockStepBin::originalFilenames[i]] = i;
        return m;
    }();
    size = 0;
    auto it = index.find(originalFilename.toStdString());
    if (it == index.end()) return nullptr;
    return BlockStepBin::getNamedResource(BlockStepBin::namedResourceList[it->second], size);
}

BlockStepKit::BlockStepKit() {
    int n = 0; const char* m = (const char*) resource("allman.json", n);
    store.init(m != nullptr ? juce::String::fromUTF8(m, n) : juce::String("{}"), [](const juce::String& f, int& sz) { return resource(f, sz); });
    store.loadUserLibrary();   // MY BANKS (Shared/UserBanks.h)
}

// ---------------------------------------------------------------- processor
BlockStepProcessor::BlockStepProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      engine(kit->store, kit->synth) {
    addParameter(bypassParam = new juce::AudioParameterBool(juce::ParameterID { "bypass", 1 }, "Bypass", false));
    startTimer(1000);
}
BlockStepProcessor::~BlockStepProcessor() { stopTimer(); }

bool BlockStepProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void BlockStepProcessor::prepareToPlay(double sampleRate, int) { engine.prepare(sampleRate); demo.prepare(sampleRate); bypass.prepare(sampleRate, 0.006, bypassParam->get()); }

void BlockStepProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    bypass.set(bypassParam->get()); bypass.takeEngageEdge();
    if (bypass.isBypassed()) midi.clear();   // BYPASS: finger drumming is ignored while bypassed
    bstep::Engine::Host h;
    if (auto* ph = getPlayHead()) if (auto p = ph->getPosition()) {
        h.playing = p->getIsPlaying();
        if (auto q = p->getPpqPosition()) { h.ppq = *q; h.ppqValid = true; }
        if (auto b = p->getBpm()) { h.bpm = *b; dawBpm.store(*b); }
    }
    // finger drumming: MIDI notes C1 (36) .. A1 (45) play KICK .. FX
    for (const auto meta : midi) {
        const auto m = meta.getMessage();
        if (m.isNoteOn() && m.getNoteNumber() >= 36 && m.getNoteNumber() < 36 + bstep::kSynthInst) {
            bstep::Cell c; c.i = (int8_t) (m.getNoteNumber() - 36); c.v = (uint8_t) juce::jlimit(1, 100, (int) std::lround(m.getVelocity() * 100 / 127.0));
            engine.playNow(c, meta.samplePosition);
        }
    }
    engine.process(buffer, buffer.getNumSamples(), h);
    demo.process(buffer, license.isUnlocked());
    // BYPASS: the engine keeps running (the beat stays in time with the DAW) but nothing is heard; ~6 ms fade on and off
    if (bypass.silent()) buffer.clear(); else bypass.applyMute(buffer);
}

void BlockStepProcessor::setUiState(const juce::String& json) {
    auto v = juce::JSON::parse(json);
    if (! v.isObject()) return;
    pianoPage = (bool) v.getProperty("pmode", false);
    { const juce::String pg = v.getProperty("page", juce::var()).toString(); pageKind = pg == "synth" ? 2 : (pg == "piano" || pianoPage.load()) ? 1 : 0; }
    auto forEngine = bstep::Pattern::fromState(v);
    std::shared_ptr<const bstep::Pattern> forUi = bstep::Pattern::fromState(v);
    { const juce::ScopedLock sl(stateLock); uiState = json; uiPattern = forUi; }
    engine.setPattern(std::move(forEngine));
}

void BlockStepProcessor::getStateInformation(juce::MemoryBlock& dest) {
    juce::MemoryOutputStream o(dest, false);
    o.writeString("BSTP1"); o.writeString(getUiState());
    o.writeInt(uiW); o.writeInt(uiH);   // the window size (older projects simply lack it)
}

void BlockStepProcessor::setStateInformation(const void* data, int size) {
    juce::MemoryInputStream in(data, (size_t) size, false);
    if (in.readString() != "BSTP1") return;
    const juce::String json = in.readString();
    if (in.getNumBytesRemaining() >= 8) { const int w = in.readInt(), h = in.readInt(); if (w >= 960 && h >= 620) { uiW = w; uiH = h; } }
    // demo mode: a reopened project starts at the default beat (same rule as every GhostBlock plugin)
    if (! license.isUnlocked()) return;
    setUiState(json);
    if (auto* e = dynamic_cast<BlockStepEditor*>(getActiveEditor())) e->sendRestore();
}

juce::AudioProcessorEditor* BlockStepProcessor::createEditor() { return new BlockStepEditor(*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BlockStepProcessor(); }
