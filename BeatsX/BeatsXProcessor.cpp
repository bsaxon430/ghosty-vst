#include "BeatsXProcessor.h"
#include "BeatsXEditor.h"

static const juce::StringArray kParamIds { "swing", "human", "glitch", "filter", "res", "echo", "verb", "crush", "drive", "glue", "volume", "songmode", "pattern", "bpm", "repeat", "repeatdiv" };

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
    return L;
}

BeatsXProcessor::BeatsXProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)), apvts(*this, nullptr, "GHOSTYBEATS2", layout()) {
    for (int i = 0; i < kParamIds.size(); ++i) p[i] = apvts.getRawParameterValue(kParamIds[i]);
    lib = std::make_shared<gb::DrumLibrary>(); lib->build(48000.0);   // played at any host rate (voices resample)
    applyKit(0, true);
}
BeatsXProcessor::~BeatsXProcessor() { engine.setSnapshot(nullptr); }

void BeatsXProcessor::prepareToPlay(double sr, int block) { engine.prepare(sr, juce::jmax(64, block)); }

gb::EngineParams BeatsXProcessor::readParams(double bpm) const {
    gb::EngineParams e;
    e.swing = p[0]->load(); e.human = p[1]->load(); e.glitch = p[2]->load(); e.filter = p[3]->load(); e.res = p[4]->load(); e.echo = p[5]->load(); e.verb = p[6]->load();
    e.crush = p[7]->load(); e.drive = p[8]->load(); e.glue = p[9]->load(); e.volume = p[10]->load(); e.songMode = p[11]->load() > 0.5f; e.pattern = (int) p[12]->load() - 1;
    e.bpm = (float) (bpm > 0 ? bpm : p[13]->load()); e.padRepeat = p[14]->load() > 0.5f; e.repeatDiv = (int) p[15]->load();
    e.stutter = stutter.load(); e.tape = tape.load(); e.recording = recording.load();
    return e;
}

void BeatsXProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals nd;
    double bpm = 0, ppq = 0; bool playing = false;
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) { if (auto b = pos->getBpm()) bpm = *b; ppq = pos->getPpqPosition().orFallback(0.0); playing = pos->getIsPlaying(); }
    auto P = readParams(0); if (!playing) bpm = P.bpm;
    if (buffer.getNumChannels() < 2) { buffer.clear(); return; }
    engine.process(buffer, midi, P, bpm, ppq, playing);
}

void BeatsXProcessor::publish() {
    auto s = std::make_unique<gb::Snapshot>();
    s->tracks = model.tracks; s->patterns = model.patterns;
    s->songCount = (int) std::min<size_t>(model.song.size(), s->song.size());
    for (int i = 0; i < s->songCount; ++i) s->song[(size_t) i] = model.song[(size_t) i];
    for (int t = 0; t < gb::kTracks; ++t) {
        const auto& tr = model.tracks[(size_t) t];
        gb::SoundPtr sp = tr.sample ? tr.sample : lib->byName(tr.sound);
        if (sp) keepSounds.push_back(sp);
        s->sound[(size_t) t] = sp.get(); if (tr.solo) s->anySolo = true;
    }
    engine.setSnapshot(s.get());
    snaps.push_back(std::move(s));
    if (snaps.size() > 48) snaps.erase(snaps.begin(), snaps.begin() + 16);   // the audio thread only ever holds the newest
    if (keepSounds.size() > 4096) { std::sort(keepSounds.begin(), keepSounds.end()); keepSounds.erase(std::unique(keepSounds.begin(), keepSounds.end()), keepSounds.end()); }
}

void BeatsXProcessor::applyKit(int k, bool patternsToo) {
    currentKit = juce::jlimit(0, (int) gb::kits().size() - 1, k);
    gb::applyKit(model, gb::kits()[(size_t) currentKit], patternsToo);
    if (patternsToo) if (auto* b = apvts.getParameter("bpm")) b->setValueNotifyingHost(b->convertTo0to1((float) gb::kits()[(size_t) currentKit].bpm));
    publish();
}

bool BeatsXProcessor::loadSampleToTrack(int t, const juce::File& f, juce::String& err) {
    auto s = gb::DrumLibrary::fromFile(f, 48000.0, err);
    if (!s || !juce::isPositiveAndBelow(t, gb::kTracks)) { if (err.isEmpty()) err = "No sound in that file."; return false; }
    model.tracks[(size_t) t].sample = s; model.tracks[(size_t) t].sound = s->name; publish(); return true;
}
void BeatsXProcessor::setTrackSound(int t, const juce::String& name) { if (!juce::isPositiveAndBelow(t, gb::kTracks)) return; model.tracks[(size_t) t].sample = nullptr; model.tracks[(size_t) t].sound = name; publish(); }

void BeatsXProcessor::drainRecorded() {
    auto& f = engine.recFifo; const int k = f.getNumReady(); if (!k) return;
    int a, b, c, d; f.prepareToRead(k, a, b, c, d);
    auto put = [&](const gb::RecEvent& e) { auto& st = model.patterns[(size_t) e.pattern].steps[(size_t) e.track][(size_t) e.step]; st.vel = e.vel; st.prob = 100; st.roll = 1; st.nudge = 0; };
    for (int i = 0; i < b; ++i) put(engine.recBuf[a + i]); for (int i = 0; i < d; ++i) put(engine.recBuf[c + i]);
    f.finishedRead(b + d); publish();
}

juce::File BeatsXProcessor::userDir() const { auto d = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Ghosty").getChildFile("Patterns"); d.createDirectory(); return d; }
bool BeatsXProcessor::savePattern(int i, const juce::String& name) {
    juce::ValueTree t("GHOSTYPATTERN"); const auto& pt = model.patterns[(size_t) i];
    t.setProperty("len", pt.length, nullptr); t.setProperty("steps", gb::patternToString(pt), nullptr);
    juce::StringArray snd; for (auto& tr : model.tracks) snd.add(tr.sound); t.setProperty("sounds", snd.joinIntoString("|"), nullptr);
    auto f = userDir().getChildFile(juce::File::createLegalFileName(name.isEmpty() ? "Pattern" : name) + ".ghostpat");
    if (auto xml = t.createXml()) return xml->writeTo(f);
    return false;
}
bool BeatsXProcessor::loadPattern(const juce::File& f, int i) {
    auto xml = juce::XmlDocument::parse(f); if (!xml) return false;
    auto t = juce::ValueTree::fromXml(*xml); if (!t.hasType("GHOSTYPATTERN")) return false;
    auto& pt = model.patterns[(size_t) i]; pt.length = juce::jlimit(4, gb::kMaxSteps, (int) t.getProperty("len", 16)); gb::patternFromString(pt, t.getProperty("steps").toString());
    publish(); return true;
}

int BeatsXProcessor::songBars() const { int e = 0; for (auto& b : model.song) e = juce::jmax(e, b.startBar + b.bars); return juce::jmax(1, e); }

juce::File BeatsXProcessor::bounce(bool song, int bars) {
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("GhostyBeats"); dir.createDirectory();
    auto f = dir.getChildFile("Ghosty Beat " + juce::String(song ? "Song " : "Pattern ") + juce::Time::getCurrentTime().formatted("%H%M%S") + ".wav");
    return renderTo(snaps.empty() ? nullptr : snaps.back().get(), song, bars, f);
}

juce::StringArray BeatsXProcessor::bounceStems(bool song, int bars) {
    juce::StringArray files; if (snaps.empty()) return files;
    const auto& base = *snaps.back();
    // which tracks actually play in what we're bouncing
    bool used[gb::kTracks] {};
    auto scan = [&](int pi) { for (int t = 0; t < gb::kTracks; ++t) for (auto& st : base.patterns[(size_t) pi].steps[(size_t) t]) if (st.vel) used[t] = true; };
    if (song && base.songCount) for (int i = 0; i < base.songCount; ++i) scan(base.song[(size_t) i].pattern); else scan((int) p[12]->load() - 1);
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("GhostyBeats").getChildFile("Stems " + juce::Time::getCurrentTime().formatted("%H%M%S"));
    dir.createDirectory();
    for (int t = 0; t < gb::kTracks; ++t) {
        if (!used[t] || base.tracks[(size_t) t].mute || (base.anySolo && !base.tracks[(size_t) t].solo)) continue;
        auto solo = std::make_unique<gb::Snapshot>(base);
        for (int k = 0; k < gb::kTracks; ++k) { solo->tracks[(size_t) k].solo = (k == t); solo->tracks[(size_t) k].mute = false; }
        solo->anySolo = true;
        auto f = dir.getChildFile(juce::String(t + 1).paddedLeft('0', 2) + " " + juce::File::createLegalFileName(model.tracks[(size_t) t].sound) + ".wav");
        renderTo(solo.get(), song, bars, f);
        files.add(f.getFullPathName());
    }
    return files;
}

juce::File BeatsXProcessor::renderTo(const gb::Snapshot* snap, bool song, int bars, const juce::File& f) {
    // a private engine renders a snapshot offline at 48 kHz (same sounds, same timing as playback)
    const double sr = 48000; const int block = 512;
    gb::BeatEngine off; off.prepare(sr, block); off.setSnapshot(snap); off.internalPlay = true;
    auto P = readParams(0); P.songMode = song; P.stutter = 0; P.tape = false; P.recording = false; P.padRepeat = false;
    const double bpm = P.bpm; const int total = (int) (bars * 4 * 60.0 / bpm * sr) + (int) (sr * 1.5);
    juce::AudioBuffer<float> all(2, total), buf(2, block); juce::MidiBuffer none;
    for (int pos = 0; pos < total; pos += block) {
        const int n = juce::jmin(block, total - pos); buf.setSize(2, n, false, false, true);
        const bool inLoop = pos < (int) (bars * 4 * 60.0 / bpm * sr);
        off.internalPlay = inLoop;
        off.process(buf, none, P, bpm, 0, false);
        for (int c = 0; c < 2; ++c) all.copyFrom(c, pos, buf, c, 0, n);
    }
    f.deleteFile();
    juce::WavAudioFormat wav;
    if (auto os = std::unique_ptr<juce::OutputStream>(f.createOutputStream()))
        if (auto* w = wav.createWriterFor(os.get(), sr, 2, 24, {}, 0)) { os.release(); std::unique_ptr<juce::AudioFormatWriter> wr(w); wr->writeFromAudioSampleBuffer(all, 0, total); }
    return f;
}

juce::File BeatsXProcessor::exportMidi(bool song) {
    juce::MidiMessageSequence seq; const int tpq = 960; const int pat = (int) p[12]->load() - 1;
    auto addPattern = [&](int pi, int startStep, int steps) {
        const auto& pt = model.patterns[(size_t) pi];
        for (int s = 0; s < steps; ++s) for (int t = 0; t < gb::kTracks; ++t) {
            const auto& st = pt.steps[(size_t) t][(size_t) (s % pt.length)]; if (!st.vel) continue;
            const int roll = juce::jmax(1, (int) st.roll);
            for (int r = 0; r < roll; ++r) {
                const double tick = ((startStep + s) + st.nudge / 100.0 + (double) r / roll) * tpq / 4.0;
                seq.addEvent(juce::MidiMessage::noteOn(10, 36 + t, (juce::uint8) st.vel), tick);
                seq.addEvent(juce::MidiMessage::noteOff(10, 36 + t), tick + tpq / 8.0 / roll);
            }
        }
    };
    if (song && !model.song.empty()) for (auto& b : model.song) addPattern(b.pattern, b.startBar * 16, b.bars * 16);
    else addPattern(pat, 0, model.patterns[(size_t) pat].length);
    seq.updateMatchedPairs();
    juce::MidiFile mf; mf.setTicksPerQuarterNote(tpq); mf.addTrack(seq);
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("GhostyBeats"); dir.createDirectory();
    auto f = dir.getChildFile("Ghosty Beat " + juce::Time::getCurrentTime().formatted("%H%M%S") + ".mid");
    f.deleteFile(); if (auto os = std::unique_ptr<juce::FileOutputStream>(f.createOutputStream())) mf.writeTo(*os);
    return f;
}

void BeatsXProcessor::getStateInformation(juce::MemoryBlock& dest) {
    auto state = apvts.copyState();
    state.appendChild(gb::modelToTree(model), nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary(*xml, dest);
}

void BeatsXProcessor::setStateInformation(const void* data, int size) {
    auto xml = getXmlFromBinary(data, size); if (!xml) return;
    auto t = juce::ValueTree::fromXml(*xml);
    auto m = t.getChildWithName("BEATS2");
    if (m.isValid()) { gb::modelFromTree(model, m, 48000.0); t.removeChild(m, nullptr); }
    else if (t.hasProperty("steps")) {   // an old Ghosty Beats project: carry its 8x16 beat into pattern A
        applyKit(0, false);
        auto rows = juce::StringArray::fromTokens(t.getProperty("steps").toString(), "|", "");
        static const int map[8] = { 1, 2, 3, 4, 5, 6, 7, 14 };   // kick, snare, clap... onto the new layout
        for (auto& pt : model.patterns) pt.clear();
        for (int r = 0; r < juce::jmin(8, rows.size()); ++r)
            for (int s = 0; s < juce::jmin(16, rows[r].length()); ++s) { const int v = rows[r][s] - '0'; if (v > 0) model.patterns[0].steps[(size_t) map[r]][(size_t) s].vel = (uint8_t) (v > 1 ? 127 : 105); }
    }
    if (t.hasType(apvts.state.getType())) apvts.replaceState(t);
    publish();
}

juce::AudioProcessorEditor* BeatsXProcessor::createEditor() { return new BeatsXEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BeatsXProcessor(); }
