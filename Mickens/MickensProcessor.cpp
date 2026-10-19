#include "MickensProcessor.h"
#include "MickensEditor.h"

namespace {
struct FxDef { const char* id; const char* name; float lo, hi, def; };
// order matters: readParams() reads fx[i] by index
const FxDef kFx[] = {
    { "crunch", "Crunch", 0, 1, 0.25f }, { "bits", "Bits", 0, 1, 0 },
    { "dry", "Dry", 0, 1, 1 },
    { "lp_cut", "Low-pass cutoff", 0, 1, 0.45f }, { "lp_mix", "Low-pass mix", 0, 1, 0 },
    { "hp_cut", "High-pass cutoff", 0, 1, 0.55f }, { "hp_mix", "High-pass mix", 0, 1, 0 },
    { "bp_cut", "Band-pass cutoff", 0, 1, 0.5f }, { "bp_mix", "Band-pass mix", 0, 1, 0 },
    { "nt_cut", "Notch cutoff", 0, 1, 0.5f }, { "nt_mix", "Notch mix", 0, 1, 0 },
    { "cb_cut", "Comb pitch", 0, 1, 0.4f }, { "cb_mix", "Comb mix", 0, 1, 0 },
    { "fm_vow", "Formant vowel", 0, 1, 0.3f }, { "fm_mix", "Formant mix", 0, 1, 0 },
    { "res", "Filter resonance", 0, 1, 0.5f },
    { "chorus", "Chorus", 0, 1, 0 }, { "reverb", "Reverb", 0, 1, 0.12f }, { "size", "Reverb size", 0, 1, 0.6f },
    { "st_cut", "Stutter filter", 0, 1, 0.75f }, { "st_env", "Stutter envelope", 0, 1, 0.5f }, { "st_dec", "Stutter env decay", 0, 1, 0.35f },
    { "duck_rel", "Duck release", 0.03f, 0.6f, 0.16f },
    { "brk_lvl", "Breaks level", 0, 1, 0.85f }, { "drm_lvl", "Drums level", 0, 1, 0.9f },
    { "kick_lvl", "Kick level", 0, 1, 1 }, { "snr_lvl", "Snare level", 0, 1, 0.9f }, { "hat_lvl", "Hat level", 0, 1, 0.6f }, { "oh_lvl", "Open hat level", 0, 1, 0.55f },
    { "volume", "Volume", 0, 1, 0.8f },
};
constexpr int kNumFx = (int) (sizeof(kFx) / sizeof(kFx[0]));

juce::AudioProcessorValueTreeState::ParameterLayout layout() {
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout L;
    for (int b = 0; b < mk::kBays; ++b) {
        const String p = "b" + String(b + 1) + "_", n = "Bay " + String(b + 1) + " ";
        L.add(std::make_unique<AudioParameterInt>(ParameterID { p + "nudge", 1 }, n + "nudge", -16, 16, 0));
        L.add(std::make_unique<AudioParameterChoice>(ParameterID { p + "grid", 1 }, n + "grid", StringArray { "1/16", "1/8", "1/4", "1/2", "1 bar" }, 0));
        L.add(std::make_unique<AudioParameterFloat>(ParameterID { p + "pitch", 1 }, n + "pitch", NormalisableRange<float>(-12, 12, 0.01f), 0));
        L.add(std::make_unique<AudioParameterFloat>(ParameterID { p + "level", 1 }, n + "level", 0, 1, b == 0 ? 0.85f : 0.6f));
        L.add(std::make_unique<AudioParameterFloat>(ParameterID { p + "decay", 1 }, n + "decay", 0.05f, 1, 1));
        L.add(std::make_unique<AudioParameterFloat>(ParameterID { p + "pan", 1 }, n + "pan", -1, 1, 0));
        L.add(std::make_unique<AudioParameterBool>(ParameterID { p + "rnd_start", 1 }, n + "random start", false));
        L.add(std::make_unique<AudioParameterBool>(ParameterID { p + "rnd_chops", 1 }, n + "random chops", false));
        L.add(std::make_unique<AudioParameterFloat>(ParameterID { p + "rnd_amt", 1 }, n + "chop amount", 0, 1, 0.3f));
        L.add(std::make_unique<AudioParameterFloat>(ParameterID { p + "duck_k", 1 }, n + "duck by kick", 0, 1, b == 0 ? 0.35f : 0));
        L.add(std::make_unique<AudioParameterFloat>(ParameterID { p + "duck_s", 1 }, n + "duck by snare", 0, 1, 0));
        L.add(std::make_unique<AudioParameterFloat>(ParameterID { p + "duck_h", 1 }, n + "duck by hats", 0, 1, 0));
        L.add(std::make_unique<AudioParameterBool>(ParameterID { p + "mute", 1 }, n + "mute", b >= 1));
        L.add(std::make_unique<AudioParameterBool>(ParameterID { p + "fit", 1 }, n + "fit to tempo", true));
    }
    for (auto& f : kFx) L.add(std::make_unique<AudioParameterFloat>(ParameterID { f.id, 1 }, f.name, f.lo, f.hi, f.def));
    L.add(std::make_unique<AudioParameterBool>(ParameterID { "stutter", 1 }, "Stutter", false));
    L.add(std::make_unique<AudioParameterChoice>(ParameterID { "st_div", 1 }, "Stutter size", StringArray { "1/8", "1/16", "1/32", "1/64", "1/16T" }, 1));
    for (auto [id, nm] : { std::pair { "kick_mute", "Kick mute" }, { "snr_mute", "Snare mute" }, { "hat_mute", "Hat mute" }, { "oh_mute", "Open hat mute" }, { "drums_mute", "Mute all drums" } })
        L.add(std::make_unique<AudioParameterBool>(ParameterID { id, 1 }, nm, false));
    L.add(std::make_unique<AudioParameterFloat>(ParameterID { "bpm", 1 }, "Tempo (when the host is stopped)", NormalisableRange<float>(70, 200, 0.1f), 172));
    return L;
}
} // namespace

MickensProcessor::MickensProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "MICKENS", layout()) {
    for (int b = 0; b < mk::kBays; ++b) {
        const juce::String p = "b" + juce::String(b + 1) + "_";
        auto g = [&](const char* s) { return apvts.getRawParameterValue(p + s); };
        bp[b] = { g("nudge"), g("grid"), g("pitch"), g("level"), g("decay"), g("pan"), g("rnd_start"), g("rnd_chops"), g("rnd_amt"), g("duck_k"), g("duck_s"), g("duck_h"), g("mute"), g("fit") };
    }
    for (int i = 0; i < kNumFx; ++i) fx[i] = apvts.getRawParameterValue(kFx[i].id);
    fx[kNumFx] = apvts.getRawParameterValue("stutter"); fx[kNumFx + 1] = apvts.getRawParameterValue("st_div"); fx[kNumFx + 2] = apvts.getRawParameterValue("bpm");
    const char* mids[] = { "kick_mute", "snr_mute", "hat_mute", "oh_mute", "drums_mute" }; for (int i = 0; i < 5; ++i) fx[kNumFx + 3 + i] = apvts.getRawParameterValue(mids[i]);
    drums.build(48000.0);
    rescanFolder();
    startTimer(60);
    defaultPattern(0);
    publish();
}
MickensProcessor::~MickensProcessor() { stopTimer(); engine.setSnapshot(nullptr); if (folderScan.valid()) folderScan.wait(); }

namespace {
// one library per app: the first instance starts building it on a background thread, later ones reuse it
std::shared_future<std::shared_ptr<const mk::BreakLibrary>> sharedLibrary() {
    static std::shared_future<std::shared_ptr<const mk::BreakLibrary>> f = std::async(std::launch::async, [] {
        auto l = std::make_shared<mk::BreakLibrary>(); l->build(48000.0); return std::shared_ptr<const mk::BreakLibrary>(l); }).share();
    return f;
}
std::vector<mk::BreakPtr> scanFolder(juce::File dir) {
    std::vector<mk::BreakPtr> out;
    auto files = dir.findChildFiles(juce::File::findFiles, true, "*.wav;*.aif;*.aiff;*.mp3;*.flac;*.ogg");
    files.sort();
    for (auto& f : files) {
        if (out.size() >= 400) break;
        juce::String err; auto br = mk::BreakLibrary::fromFile(f, 0, err); if (!br) continue;
        auto b = std::make_shared<mk::Break>(*br); b->user = false;
        const auto sub = f.getParentDirectory();
        b->family = sub == dir ? juce::String("Your Breaks folder") : "Breaks folder: " + sub.getFileName();
        out.push_back(b);
    }
    std::stable_sort(out.begin(), out.end(), [](const mk::BreakPtr& a, const mk::BreakPtr& b) { return a->family < b->family; });
    return out;
}
} // namespace

void MickensProcessor::rescanFolder() {
    if (folderScan.valid()) return;   // a scan is already running
    folderScan = std::async(std::launch::async, scanFolder, breaksFolder());
}
void MickensProcessor::timerCallback() {
    bool changed = false;
    if (!lib) { auto f = sharedLibrary(); if (f.wait_for(std::chrono::seconds(0)) == std::future_status::ready) { lib = f.get(); changed = true; } }
    if (folderScan.valid() && folderScan.wait_for(std::chrono::seconds(0)) == std::future_status::ready) { folderBreaks = folderScan.get(); changed = true; }
    if (changed) resolveBays();
}

juce::StringArray MickensProcessor::drumChoices(int d) {
    static const char* const K[] = { "Hard Kick", "Punch Kick", "Techno Kick", "Boom Bap Kick", "Sub Kick", "808 Punchy", "808 Dirty", "808 Short" };
    static const char* const S[] = { "Big Snare", "Tight Snare", "Boom Bap Snare", "Drill Snare", "Trap Snare", "Lofi Snare", "Big Clap", "Clap", "Rim Snare" };
    static const char* const H[] = { "Tight Hat", "Hat", "Trap Hat", "Lofi Hat", "Shaker Hat", "Pedal Hat", "Shaker", "Ride" };
    static const char* const O[] = { "Short Open Hat", "Open Hat", "Long Open Hat", "Crash", "Ride", "Tambourine" };
    juce::StringArray a;
    if (d == 0) for (auto s : K) a.add(s); else if (d == 1) for (auto s : S) a.add(s); else if (d == 2) for (auto s : H) a.add(s); else for (auto s : O) a.add(s);
    return a;
}

void MickensProcessor::defaultPattern(int style) {
    for (auto& r : pattern) r.fill(0);
    auto put = [&](int d, const char* s) { for (int i = 0; i < mk::kSteps && s[i]; ++i) pattern[(size_t) d][(size_t) i] = s[i] == 'X' ? 127 : s[i] == 'x' ? 96 : s[i] == 'o' ? 60 : 0; };
    if (style == 0) { put(0, "X.........X....."); put(1, "....X.......X..."); put(2, "x.x.x.x.x.x.x.xo"); put(3, "..............x."); }        // two-step
    else if (style == 1) { put(0, "X......X..X....."); put(1, "........X......."); put(2, "x.x.x.x.x.x.x.x."); }                            // halftime
    else if (style == 2) { put(0, "X.....X...X....."); put(1, "....X.......X..o"); put(2, "xxxxxxxxxxxxxxxx"); put(3, "......x.......x."); }  // roller
}

void MickensProcessor::diceStarts() {
    juce::Random r;
    for (int b = 0; b < mk::kBays; ++b) {
        auto* p = apvts.getParameter("b" + juce::String(b + 1) + "_nudge");
        const int v = r.nextInt(33) - 16;
        p->setValueNotifyingHost(p->convertTo0to1((float) v));
    }
}

void MickensProcessor::publish() {
    auto s = std::make_unique<mk::Snapshot>();
    for (int b = 0; b < mk::kBays; ++b) { auto br = breakAt(bayBreak[(size_t) b]); if (br) { keepBreaks.push_back(br); s->brk[(size_t) b] = br.get(); } }
    for (int d = 0; d < mk::kDrums; ++d) { auto ds = drumSound(d); if (ds) { keepSounds.push_back(ds); s->drumSound[(size_t) d] = ds.get(); } }
    s->drums = pattern;
    engine.setSnapshot(s.get());
    snaps.push_back(std::move(s));
    // shared_ptr copies only grow by one per publish; drop exact duplicates so the list stays short
    auto dedupe = [](auto& v) { std::sort(v.begin(), v.end()); v.erase(std::unique(v.begin(), v.end()), v.end()); };
    dedupe(keepBreaks); dedupe(keepSounds);
}

bool MickensProcessor::addUserBreak(const juce::File& f, int bay, juce::String& err) {
    auto br = mk::BreakLibrary::fromFile(f, lastBpm.load(), err); if (!br) return false;
    userBreaks.push_back(br);
    ++listVersion;
    if (juce::isPositiveAndBelow(bay, mk::kBays)) pickBreak(bay, numBreaks() - 1); else publish();
    return true;
}
bool MickensProcessor::setUserDrum(int d, const juce::File& f, juce::String& err) {
    auto s = gb::DrumLibrary::fromFile(f, 48000.0, err); if (!s) return false;
    userDrum[(size_t) d] = s; publish(); return true;
}

void MickensProcessor::prepareToPlay(double sr, int block) {
    engine.prepare(sr, block);
    chunk.setSize(2, juce::jmax(64, block)); subMidi.ensureSize(8192);
}

void MickensProcessor::readParams(std::array<mk::BayParams, mk::kBays>& B, mk::FxParams& F) const {
    for (int b = 0; b < mk::kBays; ++b) {
        auto& p = bp[b]; auto& o = B[(size_t) b];
        o.nudge = (int) std::lround(p.nudge->load()); o.grid = (int) p.grid->load(); o.pitch = p.pitch->load(); o.level = p.level->load(); o.decay = p.decay->load(); o.pan = p.pan->load();
        o.rndStart = p.rs->load() > 0.5f; o.rndChops = p.rc->load() > 0.5f; o.rndAmt = p.ra->load();
        o.duck[0] = p.dk->load(); o.duck[1] = p.ds->load(); o.duck[2] = p.dh->load(); o.mute = p.mute->load() > 0.5f; o.fit = p.fit->load() > 0.5f;
    }
    auto v = [&](int i) { return fx[i]->load(); };
    int i = 0;
    F.crunch = v(i++); F.bits = v(i++); F.dry = v(i++);
    F.lpCut = v(i++); F.lpMix = v(i++); F.hpCut = v(i++); F.hpMix = v(i++); F.bpCut = v(i++); F.bpMix = v(i++); F.notchCut = v(i++); F.notchMix = v(i++);
    F.combCut = v(i++); F.combMix = v(i++); F.formant = v(i++); F.formMix = v(i++); F.res = v(i++);
    F.chorus = v(i++); F.reverb = v(i++); F.size = v(i++);
    F.stutCut = v(i++); F.stutEnv = v(i++); F.stutDecay = v(i++);
    F.duckRelease = v(i++); F.breaksLevel = v(i++); F.drumsLevel = v(i++);
    for (int d = 0; d < mk::kDrums; ++d) F.drumLvl[d] = v(i++);
    F.volume = v(i++);
    jassert(i == kNumFx);
    F.stutter = v(kNumFx) > 0.5f; F.stutDiv = (int) v(kNumFx + 1); F.bpm = v(kNumFx + 2);
    for (int d = 0; d < mk::kDrums; ++d) F.drumMute[d] = v(kNumFx + 3 + d) > 0.5f; F.drumsMute = v(kNumFx + 7) > 0.5f;
}

void MickensProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals nd;
    std::array<mk::BayParams, mk::kBays> B; mk::FxParams F; readParams(B, F);
    double bpm = F.bpm, ppq = 0; bool hostPlay = false;
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) {
        if (auto t = pos->getBpm()) bpm = *t;
        hostPlay = pos->getIsPlaying();
        if (auto q = pos->getPpqPosition()) ppq = *q; else hostPlay = false;
    }
    hostRunning.store(hostPlay); lastBpm.store(hostPlay ? bpm : F.bpm);
    const int n = buffer.getNumSamples(), maxN = chunk.getNumSamples();
    // the engine works in chunks no bigger than prepareToPlay's block size
    for (int pos = 0; pos < n; pos += maxN) {
        const int len = juce::jmin(maxN, n - pos);
        juce::AudioBuffer<float> view(chunk.getArrayOfWritePointers(), 2, len);
        subMidi.clear(); for (const auto m : midi) if (m.samplePosition >= pos && m.samplePosition < pos + len) subMidi.addEvent(m.getMessage(), m.samplePosition - pos);
        engine.process(view, B, F, bpm, ppq + pos / (getSampleRate() * 60.0 / bpm), hostPlay, subMidi);
        for (int c = 0; c < juce::jmin(2, buffer.getNumChannels()); ++c) buffer.copyFrom(c, pos, view, c, 0, len);
    }
    midi.clear();
}

juce::File MickensProcessor::renderTo(uint32_t mask, int bars, const juce::File& f) {
    // a private engine renders the current setup offline at 48 kHz, from bar 1
    const double sr = 48000; const int block = 512;
    mk::Engine off; off.prepare(sr, block); off.setSnapshot(snaps.empty() ? nullptr : snaps.back().get()); off.outMask = mask; off.playing.store(true);
    std::array<mk::BayParams, mk::kBays> B; mk::FxParams F; readParams(B, F); F.stutter = false;
    const double bpm = lastBpm.load(); F.bpm = (float) bpm;
    const int loop = (int) (bars * 4 * 60.0 / bpm * sr), total = loop + (int) (sr * 2.0);
    juce::AudioBuffer<float> all(2, total), buf(2, block); juce::MidiBuffer none;
    for (int pos = 0; pos < total; pos += block) {
        const int n = juce::jmin(block, total - pos); buf.setSize(2, n, false, false, true);
        off.playing.store(pos < loop);
        off.process(buf, B, F, bpm, 0, false, none);
        for (int c = 0; c < 2; ++c) all.copyFrom(c, pos, buf, c, 0, n);
    }
    f.deleteFile();
    juce::WavAudioFormat wav;
    if (auto os = std::unique_ptr<juce::OutputStream>(f.createOutputStream()))
        if (auto* w = wav.createWriterFor(os.get(), sr, 2, 24, {}, 0)) { os.release(); std::unique_ptr<juce::AudioFormatWriter> wr(w); wr->writeFromAudioSampleBuffer(all, 0, total); }
    return f;
}
juce::File MickensProcessor::bounce(int bars) {
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("MrMickens"); dir.createDirectory();
    return renderTo(0xffffffffu, bars, dir.getChildFile("Mr Mickens " + juce::String(juce::roundToInt(lastBpm.load())) + "bpm.wav"));
}
juce::StringArray MickensProcessor::bounceStems(int bars) {
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("MrMickens").getChildFile("Stems " + juce::Time::getCurrentTime().formatted("%H%M%S"));
    dir.createDirectory();
    juce::StringArray out;
    const struct { uint32_t mask; const char* name; } parts[] = { { 1u, "Breaks" }, { 2u, "Kick" }, { 4u, "Snare" }, { 8u | 16u, "Hats" } };
    for (auto& p : parts) {
        if (p.mask != 1u) { bool any = false; for (int d = 0; d < mk::kDrums; ++d) if (p.mask & (2u << d)) for (auto v : pattern[(size_t) d]) any |= v > 0; if (!any) continue; }
        out.add(renderTo(p.mask, bars, dir.getChildFile(juce::String(p.name) + ".wav")).getFullPathName());
    }
    return out;
}

void MickensProcessor::getStateInformation(juce::MemoryBlock& mb) {
    auto st = apvts.copyState();
    juce::ValueTree m("MODEL");
    for (int b = 0; b < mk::kBays; ++b) m.setProperty("bay" + juce::String(b), bayKey[(size_t) b], nullptr);
    juce::String pat; for (auto& r : pattern) for (auto v : r) pat << (int) v << ","; m.setProperty("pattern", pat, nullptr);
    for (int d = 0; d < mk::kDrums; ++d) m.setProperty("drum" + juce::String(d), drumName[(size_t) d], nullptr);
    m.setProperty("dark", darkTheme, nullptr);
    auto enc = [](const std::vector<float>& x) { juce::MemoryBlock b(x.size() * 2); auto* p = (int16_t*) b.getData(); for (size_t i = 0; i < x.size(); ++i) p[i] = (int16_t) juce::jlimit(-32767.0f, 32767.0f, x[i] * 32767.0f); return b.toBase64Encoding(); };
    for (auto& ub : userBreaks) { juce::ValueTree u("USERBREAK"); u.setProperty("name", ub->name, nullptr); u.setProperty("rate", ub->rate, nullptr); u.setProperty("bpm", ub->bpm, nullptr); u.setProperty("slices", ub->slices, nullptr); u.setProperty("data", enc(ub->data), nullptr); m.appendChild(u, nullptr); }
    for (int d = 0; d < mk::kDrums; ++d) if (auto& s = userDrum[(size_t) d]) { juce::ValueTree u("USERDRUM"); u.setProperty("lane", d, nullptr); u.setProperty("name", s->name, nullptr); u.setProperty("rate", s->rate, nullptr); u.setProperty("data", enc(s->data), nullptr); m.appendChild(u, nullptr); }
    st.appendChild(m, nullptr);
    if (auto xml = st.createXml()) copyXmlToBinary(*xml, mb);
}
void MickensProcessor::setStateInformation(const void* data, int size) {
    auto xml = getXmlFromBinary(data, size); if (!xml) return;
    auto st = juce::ValueTree::fromXml(*xml); if (!st.isValid()) return;
    auto m = st.getChildWithName("MODEL"); st.removeChild(m, nullptr);
    apvts.replaceState(st);
    if (!m.isValid()) { publish(); return; }
    auto dec = [](const juce::String& s) { juce::MemoryBlock b; b.fromBase64Encoding(s); std::vector<float> x(b.getSize() / 2); auto* p = (const int16_t*) b.getData(); for (size_t i = 0; i < x.size(); ++i) x[i] = p[i] / 32767.0f; return x; };
    userBreaks.clear(); userDrum = {};
    for (auto u : m) {
        if (u.hasType("USERBREAK")) { auto b = std::make_shared<mk::Break>(); b->name = u["name"].toString(); b->family = "Your breaks"; b->user = true; b->rate = u["rate"]; b->bpm = u["bpm"]; b->slices = juce::jmax(1, (int) u["slices"]); b->data = dec(u["data"].toString()); if (!b->data.empty()) userBreaks.push_back(b); }
        if (u.hasType("USERDRUM")) { const int d = u["lane"]; if (!juce::isPositiveAndBelow(d, mk::kDrums)) continue; auto s = std::make_shared<gb::Sound>(); s->name = u["name"].toString(); s->category = "Sample"; s->rate = u["rate"]; s->data = dec(u["data"].toString()); if (!s->data.empty()) userDrum[(size_t) d] = s; }
    }
    darkTheme = (bool) m.getProperty("dark", false);
    for (int b = 0; b < mk::kBays; ++b) { auto k = m["bay" + juce::String(b)].toString(); if (k.isNotEmpty()) bayKey[(size_t) b] = k; }
    auto pat = juce::StringArray::fromTokens(m["pattern"].toString(), ",", "");
    if (pat.size() >= mk::kDrums * mk::kSteps) for (int d = 0; d < mk::kDrums; ++d) for (int s = 0; s < mk::kSteps; ++s) pattern[(size_t) d][(size_t) s] = (uint8_t) juce::jlimit(0, 127, pat[d * mk::kSteps + s].getIntValue());
    for (int d = 0; d < mk::kDrums; ++d) { auto nm = m["drum" + juce::String(d)].toString(); if (nm.isNotEmpty()) drumName[(size_t) d] = nm; }
    resolveBays();
}

juce::AudioProcessorEditor* MickensProcessor::createEditor() { return new MickensEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MickensProcessor(); }
