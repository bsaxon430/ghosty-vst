#include "SpecProcessor.h"
#include "SpecEditor.h"

static juce::AudioProcessorValueTreeState::ParameterLayout specLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout L;
    L.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { "bpm", 1 }, "Tempo (no host)", juce::NormalisableRange<float>(60, 200, 1), 120));
    L.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { "volume", 1 }, "Volume", juce::NormalisableRange<float>(0, 1), 0.8f));
    return L;
}

SpecProcessor::SpecProcessor() : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)), apvts(*this, nullptr, "GHOSTYSPECTRUM2", specLayout()) {
    drums = std::make_shared<gb::DrumLibrary>(); drums->build(48000.0);
    publish();
}
SpecProcessor::~SpecProcessor() { engine.setSnapshot(nullptr); }

void SpecProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals nd;
    double hbpm = 120, ppq = 0; bool playing = false;
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) { if (auto b = pos->getBpm()) hbpm = *b; ppq = pos->getPpqPosition().orFallback(0.0); playing = pos->getIsPlaying(); }
    midi.clear();
    if (buffer.getNumChannels() < 2) { buffer.clear(); return; }
    engine.process(buffer, hbpm, ppq, playing, bpm());
    buffer.applyGain(apvts.getRawParameterValue("volume")->load() * 1.25f);
}

int SpecProcessor::addLayer(int kind, const juce::String& name, const juce::String& words, uint32_t seed, std::shared_ptr<gs::SampleData> sample) {
    if ((int) model.layers.size() >= sp::kLayers) return -1;
    sp::Layer l; l.kind = kind; l.name = name; l.words = words; l.seed = seed; l.sample = sample;
    if (kind == 1) if (auto s = drums->byName(name)) l.centroidMidi = sp::centroidOf(*s);
    if (kind == 1) l.space = 0.08f;
    model.layers.push_back(l); publish(); return (int) model.layers.size() - 1;
}
int SpecProcessor::layerFor(int kind, const juce::String& name) {
    for (int i = 0; i < (int) model.layers.size(); ++i) if (model.layers[(size_t) i].kind == kind && model.layers[(size_t) i].name == name) return i;
    return -1;
}
void SpecProcessor::removeLayer(int i) {
    if (!juce::isPositiveAndBelow(i, (int) model.layers.size())) return;
    model.layers.erase(model.layers.begin() + i);
    auto& N = model.notes; N.erase(std::remove_if(N.begin(), N.end(), [i](auto& n) { return n.layer == i; }), N.end());
    for (auto& n : N) if (n.layer > i) --n.layer;
    publish();
}

void SpecProcessor::publish() {
    auto s = std::make_unique<sp::SpecSnapshot>();
    s->bars = model.bars; s->layerCount = (int) juce::jmin<size_t>(model.layers.size(), sp::kLayers);
    for (int i = 0; i < s->layerCount; ++i) {
        const auto& L = model.layers[(size_t) i]; auto& o = s->layers[(size_t) i];
        o.kind = L.kind; o.vol = L.vol; o.pan = L.pan; o.tone = L.tone; o.space = L.space; o.mute = L.mute; o.centroid = L.centroidMidi;
        bool anyGlide = false; for (auto& n : model.notes) if (n.layer == i && n.glide) anyGlide = true;
        if (L.kind == 0) { o.snap = gs::snapFromPatch(gs::Summoner::summon(L.words, L.seed)); }
        else if (L.kind == 2) { gs::Patch p; p["smp_level"] = 0.9f; p["smp_mode"] = 0; p["smp_track"] = 1; p[gs::opId(0, "level")] = 0.0f; p["master"] = -4.0f; o.snap = gs::snapFromPatch(p); }
        if (anyGlide && L.kind != 1) { o.snap.mono = true; o.snap.glide = juce::jmax(o.snap.glide, 0.09f); }
        if (L.kind == 1) { auto d = drums->byName(L.name); o.drum = d.get(); }
        if (L.kind == 2 && L.sample) { keep.push_back(L.sample); engine.engine(i).setSample(L.sample); }
        else if (L.kind == 0) engine.engine(i).setSample(nullptr);
    }
    s->noteCount = (int) juce::jmin<size_t>(model.notes.size(), sp::kMaxNotes);
    for (int i = 0; i < s->noteCount; ++i) s->notes[(size_t) i] = model.notes[(size_t) i];
    engine.setSnapshot(s.get()); snaps.push_back(std::move(s));
    if (snaps.size() > 40) snaps.erase(snaps.begin(), snaps.begin() + 10);
}

juce::ValueTree SpecProcessor::toTree() const {
    juce::ValueTree t("SPECTRUM2"); t.setProperty("bars", model.bars, nullptr); t.setProperty("root", model.root, nullptr); t.setProperty("scale", model.scale, nullptr); t.setProperty("snap", model.snap, nullptr);
    for (auto& L : model.layers) {
        juce::ValueTree x("LAYER"); x.setProperty("kind", L.kind, nullptr); x.setProperty("name", L.name, nullptr); x.setProperty("words", L.words, nullptr); x.setProperty("seed", (int) L.seed, nullptr);
        x.setProperty("vol", L.vol, nullptr); x.setProperty("pan", L.pan, nullptr); x.setProperty("tone", L.tone, nullptr); x.setProperty("space", L.space, nullptr); x.setProperty("mute", L.mute, nullptr); x.setProperty("cent", L.centroidMidi, nullptr);
        if (L.sample) { x.setProperty("srate", L.sample->rate, nullptr); x.setProperty("sample", L.sample->toBase64(), nullptr); }
        t.appendChild(x, nullptr);
    }
    juce::String notes; for (auto& n : model.notes) notes << n.layer << "," << n.start << "," << n.len << "," << n.pitch << "," << n.vel << "," << (int) n.glide << ";";
    t.setProperty("notes", notes, nullptr);
    return t;
}
void SpecProcessor::fromTree(const juce::ValueTree& t) {
    model = sp::Model(); model.bars = t.getProperty("bars", 2); model.root = t.getProperty("root", 0); model.scale = t.getProperty("scale", 1); model.snap = t.getProperty("snap", true);
    for (auto x : t) if (x.hasType("LAYER")) {
        sp::Layer L; L.kind = x.getProperty("kind"); L.name = x.getProperty("name").toString(); L.words = x.getProperty("words").toString(); L.seed = (uint32_t) (int) x.getProperty("seed", 1);
        L.vol = (float) (double) x.getProperty("vol", 0.8); L.pan = (float) (double) x.getProperty("pan", 0.0); L.tone = (float) (double) x.getProperty("tone", 0.5); L.space = (float) (double) x.getProperty("space", 0.2);
        L.mute = x.getProperty("mute", false); L.centroidMidi = (float) (double) x.getProperty("cent", 60.0);
        if (x.hasProperty("sample")) L.sample = gs::SampleData::fromBase64(x.getProperty("sample").toString(), (double) x.getProperty("srate", 44100.0), L.name);
        model.layers.push_back(L);
    }
    for (auto& part : juce::StringArray::fromTokens(t.getProperty("notes").toString(), ";", "")) {
        auto f = juce::StringArray::fromTokens(part, ",", ""); if (f.size() < 6) continue;
        model.notes.push_back({ f[0].getIntValue(), f[1].getDoubleValue(), f[2].getDoubleValue(), f[3].getFloatValue(), f[4].getFloatValue(), f[5].getIntValue() != 0 });
    }
    publish();
}

juce::File SpecProcessor::userDir() const { auto d = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Ghosty").getChildFile("Spectrum"); d.createDirectory(); return d; }
bool SpecProcessor::saveCanvas(const juce::String& name) { if (auto xml = toTree().createXml()) return xml->writeTo(userDir().getChildFile(juce::File::createLegalFileName(name) + ".ghostspec")); return false; }
bool SpecProcessor::loadCanvas(const juce::File& f) { auto xml = juce::XmlDocument::parse(f); if (!xml) return false; auto t = juce::ValueTree::fromXml(*xml); if (!t.hasType("SPECTRUM2")) return false; fromTree(t); return true; }

juce::File SpecProcessor::renderTo(const sp::SpecSnapshot* s, const juce::File& f) {
    const double sr = 48000; const int block = 512;
    auto off = std::make_unique<sp::SpecEngine>(); off->prepare(sr, block); off->setSnapshot(s); off->playing = true;
    for (int i = 0; i < s->layerCount; ++i) if (model.layers[(size_t) i].kind == 2) off->engine(i).setSample(model.layers[(size_t) i].sample);
    const double b = bpm(); const int loopN = (int) (s->bars * 4 * 60.0 / b * sr), total = loopN + (int) (sr * 2.0);
    juce::AudioBuffer<float> all(2, total), buf(2, block);
    for (int pos = 0; pos < total; pos += block) {
        const int n = juce::jmin(block, total - pos); buf.setSize(2, n, false, false, true);
        off->playing = pos < loopN; off->process(buf, b, 0, false, b);
        for (int c = 0; c < 2; ++c) all.copyFrom(c, pos, buf, c, 0, n);
    }
    all.applyGain(apvts.getRawParameterValue("volume")->load() * 1.25f);
    f.deleteFile(); juce::WavAudioFormat wav;
    if (auto os = std::unique_ptr<juce::OutputStream>(f.createOutputStream()))
        if (auto* w = wav.createWriterFor(os.get(), sr, 2, 24, {}, 0)) { os.release(); std::unique_ptr<juce::AudioFormatWriter> wr(w); wr->writeFromAudioSampleBuffer(all, 0, total); }
    return f;
}
juce::File SpecProcessor::bounce() {
    auto d = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("GhostySpectrum"); d.createDirectory();
    return renderTo(snaps.back().get(), d.getChildFile("Ghosty Spectrum loop " + juce::Time::getCurrentTime().formatted("%H%M%S") + ".wav"));
}
juce::StringArray SpecProcessor::bounceStems() {
    juce::StringArray out; const auto& base = *snaps.back();
    auto d = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("GhostySpectrum").getChildFile("Stems " + juce::Time::getCurrentTime().formatted("%H%M%S")); d.createDirectory();
    for (int l = 0; l < base.layerCount; ++l) {
        bool used = false; for (int i = 0; i < base.noteCount; ++i) if (base.notes[(size_t) i].layer == l) used = true;
        if (!used || base.layers[(size_t) l].mute) continue;
        auto s = std::make_unique<sp::SpecSnapshot>(base); for (int k = 0; k < s->layerCount; ++k) s->layers[(size_t) k].solo = (k == l); s->anySolo = true;
        auto f = d.getChildFile(juce::String(l + 1).paddedLeft('0', 2) + " " + juce::File::createLegalFileName(model.layers[(size_t) l].name) + ".wav");
        renderTo(s.get(), f); out.add(f.getFullPathName());
    }
    return out;
}

void SpecProcessor::getStateInformation(juce::MemoryBlock& dest) { auto st = apvts.copyState(); st.appendChild(toTree(), nullptr); if (auto xml = st.createXml()) copyXmlToBinary(*xml, dest); }
void SpecProcessor::setStateInformation(const void* data, int size) {
    auto xml = getXmlFromBinary(data, size); if (!xml) return; auto t = juce::ValueTree::fromXml(*xml);
    auto m = t.getChildWithName("SPECTRUM2"); if (m.isValid()) { fromTree(m); t.removeChild(m, nullptr); }
    if (t.hasType(apvts.state.getType())) apvts.replaceState(t);
}
juce::AudioProcessorEditor* SpecProcessor::createEditor() { return new SpecEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SpecProcessor(); }
