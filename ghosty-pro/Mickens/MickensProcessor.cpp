#include "MickensProcessor.h"
#include "HostNotify.h"
#include "ParamText.h"
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
    { "brk_lvl", "Breaks level", 0, 1, 0.85f }, { "drm_lvl", "Drums level", 0, 1, 0.7f },
    { "kick_lvl", "Kick level", 0, 1, 1 }, { "snr_lvl", "Snare level", 0, 1, 0.9f }, { "hat_lvl", "Hat level", 0, 1, 0.6f }, { "oh_lvl", "Open hat level", 0, 1, 0.55f },
    { "volume", "Volume", 0, 1, 0.5f },
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
        L.add(std::make_unique<AudioParameterFloat>(ParameterID { p + "level", 1 }, n + "level", 0, 1, b == 0 ? 0.7f : 0.5f));
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
        // Mickens 2: BREAK DNA + GROOVE LOCK + REMIX, INTERLOCK role
        L.add(std::make_unique<AudioParameterChoice>(ParameterID { p + "dna_mode", 2 }, n + "DNA mode", StringArray { "PRESERVE", "REWIRE", "MUTATE" }, 0));
        L.add(std::make_unique<AudioParameterFloat>(ParameterID { p + "remix", 2 }, n + "remix", NormalisableRange<float>(0, 100, 0.1f), 0));
        L.add(std::make_unique<AudioParameterFloat>(ParameterID { p + "lock", 2 }, n + "groove lock", NormalisableRange<float>(0, 100, 0.1f), 70));
        for (auto [id, nm] : { std::pair { "lk_ks", "lock kick/snare" }, { "lk_pos", "lock positions" }, { "lk_swing", "lock swing" }, { "lk_acc", "lock accents" }, { "lk_ghost", "lock ghosts" } })
            L.add(std::make_unique<AudioParameterBool>(ParameterID { p + id, 2 }, n + nm, true));
        L.add(std::make_unique<AudioParameterBool>(ParameterID { p + "freeze", 2 }, n + "freeze DNA", false));
        L.add(std::make_unique<AudioParameterInt>(ParameterID { p + "dna_seed", 2 }, n + "remix seed", 0, 9999, b * 17));
        L.add(std::make_unique<AudioParameterChoice>(ParameterID { p + "role", 2 }, n + "role", StringArray { "FREE", "MAIN", "GHOST", "TEXTURE", "FILLS" }, 0));
    }
    for (auto& f : kFx) L.add(std::make_unique<AudioParameterFloat>(ParameterID { f.id, 1 }, f.name, f.lo, f.hi, f.def));
    L.add(std::make_unique<AudioParameterBool>(ParameterID { "stutter", 1 }, "Stutter", false));
    L.add(std::make_unique<AudioParameterChoice>(ParameterID { "st_div", 1 }, "Stutter size", StringArray { "1/8", "1/16", "1/32", "1/64", "1/16T" }, 1));
    L.add(std::make_unique<AudioParameterChoice>(ParameterID { "keys_bay", 1 }, "Keys play bay", StringArray { "Bay 1", "Bay 2", "Bay 3", "Bay 4" }, 0));
    L.add(std::make_unique<AudioParameterBool>(ParameterID { "mute_all", 1 }, "Mute Mr. Mickens", false));
    L.add(std::make_unique<AudioParameterInt>(ParameterID { "rnd_seed", 1 }, "Random seed (DICE)", 0, 9999, 0));
    for (auto [id, nm] : { std::pair { "kick_mute", "Kick mute" }, { "snr_mute", "Snare mute" }, { "hat_mute", "Hat mute" }, { "oh_mute", "Open hat mute" }, { "drums_mute", "Mute all drums" } })
        L.add(std::make_unique<AudioParameterBool>(ParameterID { id, 1 }, nm, false));
    L.add(std::make_unique<AudioParameterFloat>(ParameterID { "bpm", 1 }, "Tempo (when the host is stopped)", NormalisableRange<float>(70, 200, 0.1f), 172));
    // ---- Mickens 2
    L.add(std::make_unique<AudioParameterChoice>(ParameterID { "fill_style", 2 }, "Fill style", StringArray { "CLASSIC", "JUNGLE", "ROLLING", "GLITCH", "CHAOS" }, 0));
    L.add(std::make_unique<AudioParameterChoice>(ParameterID { "fill_every", 2 }, "Fill every", StringArray { "OFF", "4 BARS", "8 BARS", "16 BARS" }, 0));
    L.add(std::make_unique<AudioParameterChoice>(ParameterID { "fill_len", 2 }, "Fill length", StringArray { "1 BEAT", "2 BEATS", "1 BAR" }, 1));
    L.add(std::make_unique<AudioParameterFloat>(ParameterID { "fill_int", 2 }, "Fill intensity", 0, 1, 0.6f));
    L.add(std::make_unique<AudioParameterChoice>(ParameterID { "il_mode", 2 }, "Interlock", StringArray { "OFF", "COMPLEMENT", "CALL & RESPONSE", "UNISON", "CROSS-RHYTHM", "COLLISION" }, 0));
    L.add(std::make_unique<AudioParameterFloat>(ParameterID { "il_duck", 2 }, "Interlock ducking", 0, 1, 0.6f));
    L.add(std::make_unique<AudioParameterChoice>(ParameterID { "st_mode", 2 }, "Stutter mode", StringArray { "ROLL", "RAMP", "GLITCH", "BREAKDOWN" }, 0));
    L.add(std::make_unique<AudioParameterFloat>(ParameterID { "st_gate", 2 }, "Stutter gate", 0.05f, 1, 1));
    L.add(std::make_unique<AudioParameterFloat>(ParameterID { "st_vdec", 2 }, "Stutter decay", 0, 1, 0));
    L.add(std::make_unique<AudioParameterFloat>(ParameterID { "st_pitch", 2 }, "Stutter pitch", NormalisableRange<float>(-12, 12, 0.01f), 0));
    L.add(std::make_unique<AudioParameterFloat>(ParameterID { "st_move", 2 }, "Stutter movement", 0, 1, 0));
    L.add(std::make_unique<AudioParameterChoice>(ParameterID { "flt_route", 2 }, "Filter routing", StringArray { "PARALLEL", "SERIAL", "MORPH" }, 0));
    L.add(std::make_unique<AudioParameterFloat>(ParameterID { "flt_morph", 2 }, "Filter morph", 0, 1, 0));
    L.add(std::make_unique<AudioParameterBool>(ParameterID { "flt_auto", 2 }, "Filter auto gain", true));
    L.add(std::make_unique<AudioParameterFloat>(ParameterID { "cb_fb", 2 }, "Comb feedback", NormalisableRange<float>(-0.95f, 0.95f, 0.001f), 0.75f));
    L.add(std::make_unique<AudioParameterChoice>(ParameterID { "cb_tune", 2 }, "Comb tuning", StringArray { "FREE", "KEY", "TEMPO" }, 0));
    L.add(std::make_unique<AudioParameterInt>(ParameterID { "cb_note", 2 }, "Comb note", 24, 96, 45, AudioParameterIntAttributes().withStringFromValueFunction([](int v, int) { return MidiMessage::getMidiNoteName(v, true, true, 3); })
                                                                                                                                           .withValueFromStringFunction([](const String& t) { const int n = ghosty::ptext::noteFromName(t, 3); return n >= 0 ? n : t.getIntValue(); })));
    L.add(std::make_unique<AudioParameterChoice>(ParameterID { "ftr_src", 2 }, "Hit sweep trigger", StringArray { "OFF", "KICK", "SNARE", "ANY HIT" }, 0));
    L.add(std::make_unique<AudioParameterFloat>(ParameterID { "ftr_amt", 2 }, "Hit sweep amount (oct)", NormalisableRange<float>(-4, 4, 0.01f), 2));
    L.add(std::make_unique<AudioParameterFloat>(ParameterID { "ftr_dec", 2 }, "Hit sweep decay (s)", NormalisableRange<float>(0.01f, 1.0f, 0.001f, 0.5f), 0.12f));
    L.add(std::make_unique<AudioParameterFloat>(ParameterID { "ftr_res", 2 }, "Hit sweep resonance", 0, 1, 0.3f));
    L.add(std::make_unique<AudioParameterChoice>(ParameterID { "phr_len", 2 }, "Phrase sweep", StringArray { "OFF", "1 BAR", "2 BARS", "4 BARS", "8 BARS" }, 0));
    L.add(std::make_unique<AudioParameterFloat>(ParameterID { "phr_amt", 2 }, "Phrase sweep amount (oct)", NormalisableRange<float>(-5, 5, 0.01f), 3));
    L.add(std::make_unique<AudioParameterChoice>(ParameterID { "phr_shape", 2 }, "Phrase sweep shape", StringArray { "RISE", "FALL", "ARCH" }, 0));
    L.add(std::make_unique<AudioParameterBool>(ParameterID { "morph_on", 2 }, "Snapshot morph on", false));
    L.add(std::make_unique<AudioParameterFloat>(ParameterID { "morph", 2 }, "Snapshot morph", 0, 1, 0));
    L.add(std::make_unique<AudioParameterInt>(ParameterID { "morph_a", 2 }, "Morph from slot", 1, 8, 1));
    L.add(std::make_unique<AudioParameterInt>(ParameterID { "morph_b", 2 }, "Morph to slot", 1, 8, 2));
    L.add(std::make_unique<AudioParameterChoice>(ParameterID { "interp", 2 }, "Sample quality", StringArray { "LO-FI", "CLEAN", "HQ" }, 1));
    L.add(std::make_unique<AudioParameterBool>(ParameterID { "bypass", 1 }, "Bypass", false));   // LAST: existing automation keeps its index
    L.add(std::make_unique<AudioParameterChoice>(ParameterID { "play_mode", 1 }, "Play", StringArray { "ALWAYS", "ON NOTES" }, 0));   // after bypass, same rule
    return L;
}
// the order readParams() reads parameters in (indices into the processor's parameter list)
const char* const kBayIds[] = { "nudge", "grid", "pitch", "level", "decay", "pan", "rnd_start", "rnd_chops", "rnd_amt", "duck_k", "duck_s", "duck_h", "mute", "fit",
                                "dna_mode", "remix", "lock", "lk_ks", "lk_pos", "lk_swing", "lk_acc", "lk_ghost", "freeze", "dna_seed", "role" };
juce::StringArray readOrder() {
    juce::StringArray o;
    for (int b = 0; b < mk::kBays; ++b) for (auto* id : kBayIds) o.add("b" + juce::String(b + 1) + "_" + id);
    o.add("rnd_seed");
    for (auto& f : kFx) o.add(f.id);
    for (auto* id : { "stutter", "st_div", "bpm", "kick_mute", "snr_mute", "hat_mute", "oh_mute", "drums_mute", "keys_bay",
                      "fill_style", "fill_every", "fill_len", "fill_int", "il_mode", "il_duck", "st_mode", "st_gate", "st_vdec", "st_pitch", "st_move",
                      "flt_route", "flt_morph", "flt_auto", "cb_fb", "cb_tune", "cb_note", "ftr_src", "ftr_amt", "ftr_dec", "ftr_res", "phr_len", "phr_amt", "phr_shape", "interp" }) o.add(id);
    return o;
}
juce::String locksToString(const mk::StepLocks& L) {
    juce::String s; for (int i = 0; i < 32; ++i) { const auto& k = L.s[(size_t) i]; if (!k.on) continue; s << i << ":" << k.slice << "," << k.ratchet << "," << (k.reverse ? 1 : 0) << "," << juce::String(k.semis, 2) << "," << juce::String(k.gain, 3) << "," << juce::String(k.len, 3) << "," << (int) k.cls << ";"; }
    return s;
}
std::shared_ptr<const mk::StepLocks> locksFromString(const juce::String& s) {
    if (s.isEmpty()) return nullptr; auto L = std::make_shared<mk::StepLocks>();
    for (auto& item : juce::StringArray::fromTokens(s, ";", "")) { if (item.isEmpty()) continue; const int step = item.upToFirstOccurrenceOf(":", false, false).getIntValue(); auto f = juce::StringArray::fromTokens(item.fromFirstOccurrenceOf(":", false, false), ",", ""); if (f.size() < 7 || !juce::isPositiveAndBelow(step, 32)) continue;
        auto& k = L->s[(size_t) step]; k.on = true; k.slice = juce::jlimit(0, 127, f[0].getIntValue()); k.ratchet = juce::jlimit(1, 4, f[1].getIntValue()); k.reverse = f[2].getIntValue() != 0; k.semis = juce::jlimit(-36.0f, 36.0f, f[3].getFloatValue()); k.gain = juce::jlimit(0.0f, 2.0f, f[4].getFloatValue()); k.len = juce::jlimit(0.05f, 4.0f, f[5].getFloatValue()); k.cls = (uint8_t) (f[6].getIntValue() & 31); ++L->count; }
    return L->count ? L : nullptr;
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
    fx[kNumFx + 8] = apvts.getRawParameterValue("keys_bay");
    morphOn = apvts.getRawParameterValue("morph_on"); morphAmt = apvts.getRawParameterValue("morph"); morphA = apvts.getRawParameterValue("morph_a"); morphB = apvts.getRawParameterValue("morph_b");
    for (auto* ap : getParameters()) if (auto* rp = dynamic_cast<juce::RangedAudioParameter*>(ap)) {
        plist.push_back(rp); praw.push_back(apvts.getRawParameterValue(rp->getParameterID()));
        const auto id = rp->getParameterID();
        const bool perf = id.startsWith("morph") || id == "mute_all" || id == "stutter" || id == "bypass" || id == "play_mode";
        pkind.push_back(perf ? 2 : (dynamic_cast<juce::AudioParameterFloat*>(rp) ? 0 : 1));
    }
    bypassParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("bypass")); jassert(bypassParam); playMode = apvts.getRawParameterValue("play_mode"); sync.setOwnBpm(apvts.getRawParameterValue("bpm")->load());
    valsBuf.assign(plist.size(), 0.0f);
    for (auto& id : readOrder()) { pi.push_back(pIndex(id)); jassert(pi.back() >= 0); }
    drums.build(48000.0);
    rescanFolder();
    startTimer(60);
    defaultPattern(0);
    publish();
}
MickensProcessor::~MickensProcessor() { stopTimer(); engine.setSnapshot(nullptr); }

namespace {
// one library per app: the first instance starts building it (audio + DNA) on a background thread, later ones reuse it
std::shared_future<std::shared_ptr<const mk::BreakLibrary>> sharedLibrary() {
    static std::shared_future<std::shared_ptr<const mk::BreakLibrary>> f = std::async(std::launch::async, [] {
        auto l = std::make_shared<mk::BreakLibrary>(); l->build(48000.0);
        for (auto& b : l->breaks()) std::const_pointer_cast<mk::Break>(b)->dna = std::make_shared<const mk::BreakDNA>(mk::analyzeBreak(*b));
        return std::shared_ptr<const mk::BreakLibrary>(l); }).share();
    return f;
}
// the Breaks folder: one scan per app per folder state (path + newest mtime), shared by every instance, DNA analysed on the
// scan thread, each file capped at kMaxBreakSeconds and the whole folder at ~300 MB of audio
std::vector<mk::BreakPtr> scanFolder(juce::File dir) {
    std::vector<mk::BreakPtr> out; size_t bytes = 0; const size_t capBytes = 300u * 1024u * 1024u;
    auto files = dir.findChildFiles(juce::File::findFiles, true, "*.wav;*.aif;*.aiff;*.mp3;*.flac;*.ogg");
    files.sort();
    for (auto& f : files) {
        if (out.size() >= 400 || bytes >= capBytes) break;
        juce::String err; auto br = mk::BreakLibrary::fromFile(f, 0, err); if (!br) continue;
        auto b = std::make_shared<mk::Break>(*br); b->user = false;
        const auto sub = f.getParentDirectory();
        b->family = sub == dir ? juce::String("Your Breaks folder") : "Breaks folder: " + sub.getFileName();
        b->dna = std::make_shared<const mk::BreakDNA>(mk::analyzeBreak(*b));
        bytes += b->data.size() * sizeof(float);
        out.push_back(b);
    }
    std::stable_sort(out.begin(), out.end(), [](const mk::BreakPtr& a, const mk::BreakPtr& b) { return a->family < b->family; });
    return out;
}
juce::String folderStamp(const juce::File& dir) {
    juce::int64 newest = 0; int count = 0;
    for (auto& f : dir.findChildFiles(juce::File::findFiles, true, "*.wav;*.aif;*.aiff;*.mp3;*.flac;*.ogg")) { newest = juce::jmax(newest, f.getLastModificationTime().toMilliseconds()); ++count; }
    return dir.getFullPathName() + "|" + juce::String(newest) + "|" + juce::String(count);
}
std::shared_future<std::vector<mk::BreakPtr>> sharedFolder(const juce::File& dir) {
    static std::map<juce::String, std::shared_future<std::vector<mk::BreakPtr>>> scans; static juce::CriticalSection lock;
    const auto key = folderStamp(dir);
    const juce::ScopedLock sl(lock);
    auto it = scans.find(key); if (it != scans.end()) return it->second;
    if (scans.size() > 8) scans.clear();   // older folder states are not needed once the folder changed
    auto f = std::async(std::launch::async, scanFolder, dir).share(); scans[key] = f; return f;
}
} // namespace

void MickensProcessor::rescanFolder() { folderScan = sharedFolder(breaksFolder()); folderPending = true; }
void MickensProcessor::timerCallback() {
    bool changed = false;
    if (!lib) { auto f = sharedLibrary(); if (f.wait_for(std::chrono::seconds(0)) == std::future_status::ready) { lib = f.get(); changed = true; } }
    if (folderPending && folderScan.valid() && folderScan.wait_for(std::chrono::seconds(0)) == std::future_status::ready) { folderBreaks = folderScan.get(); folderPending = false; changed = true; }
    if (changed) { resolveBays(); if (morphing()) refreshMorph(); }
    syncFreeze();
    const bool on = morphOn->load() > 0.5f; const int a = (int) morphA->load() - 1, b = (int) morphB->load() - 1;
    if (on != lastMorphOn || a != lastMorphA || b != lastMorphB) refreshMorph();
    morphSideUi.store(morphSide.load());
    gcSnapshots();
}

juce::StringArray MickensProcessor::drumChoices(int d) {
    static const char* const K[] = { "Hard Kick", "Punch Kick", "Techno Kick", "Boom Bap Kick", "Sub Kick", "Boom Sub Punch", "Boom Sub Dirty", "Boom Sub Short" };
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

void MickensProcessor::setParam(const juce::String& id, float value) {
    auto* p = apvts.getParameter(id); if (!p) return;
    const float norm = p->convertTo0to1(value);
    if (std::abs(p->getValue() - norm) < 1e-6f) return;
    p->beginChangeGesture(); p->setValueNotifyingHost(norm); p->endChangeGesture();
}

void MickensProcessor::diceStarts() {
    juce::Random r;
    setParam("rnd_seed", (float) r.nextInt(10000));   // new random-chop / random-start combos too
    for (int b = 0; b < mk::kBays; ++b) setParam("b" + juce::String(b + 1) + "_nudge", (float) (r.nextInt(33) - 16));
}

void MickensProcessor::publish() {
    auto s = std::make_unique<mk::Snapshot>(); s->gen = ++genCounter;
    for (int b = 0; b < mk::kBays; ++b) { auto br = breakAt(bayBreak[(size_t) b]); if (br) { keepBreaks.push_back(br); s->brk[(size_t) b] = br.get();
        if (auto d = dnaOf(br)) { keepDNA.push_back(d); s->dna[(size_t) b] = d.get(); } if (frozen[(size_t) b]) { keepDNA.push_back(frozen[(size_t) b]); s->groove[(size_t) b] = frozen[(size_t) b].get(); } }
        if (locks[(size_t) b]) { keepLocks.push_back(locks[(size_t) b]); s->locks[(size_t) b] = locks[(size_t) b].get(); } }
    for (int d = 0; d < mk::kDrums; ++d) { auto ds = drumSound(d); if (ds) { keepSounds.push_back(ds); s->drumSound[(size_t) d] = ds.get(); } }
    s->drums = pattern;
    liveSnap.store(s.get());
    if (!morphing()) engine.setSnapshot(s.get());
    snaps.push_back(std::move(s));
    // shared_ptr copies only grow by one per publish; drop exact duplicates so the lists stay short
    auto dedupe = [](auto& v) { std::sort(v.begin(), v.end()); v.erase(std::unique(v.begin(), v.end()), v.end()); };
    dedupe(keepBreaks); dedupe(keepSounds); dedupe(keepDNA); dedupe(keepLocks);
}
// snapshots / morph sets are published by pointer; the audio thread tells us (usedGen / audioMsDone) what it has moved past
void MickensProcessor::gcSnapshots() {
    if (renderBusy.load() > 0) return;
    const MorphSet* cur = morphSet.load(); const MorphSet* seen = audioMsDone.load();
    keepMorph.erase(std::remove_if(keepMorph.begin(), keepMorph.end(), [&](const std::unique_ptr<MorphSet>& m) { return m.get() != cur && m.get() != seen; }), keepMorph.end());
    const uint64_t used = engine.usedGen.load(); const mk::Snapshot* live = liveSnap.load();
    auto held = [&](const mk::Snapshot* s) { if (s == live) return true; for (auto& m : keepMorph) if (m->snapA == s || m->snapB == s) return true; return false; };
    snaps.erase(std::remove_if(snaps.begin(), snaps.end(), [&](const std::unique_ptr<mk::Snapshot>& s) { return s->gen < used && !held(s.get()); }), snaps.end());
}

// ---------------------------------------------------------------- BREAK DNA
std::shared_ptr<const mk::BreakDNA> MickensProcessor::dnaOf(const mk::BreakPtr& br) {
    if (!br) return nullptr;
    if (br->dna) return br->dna;
    auto it = dnaCache.find(br); if (it != dnaCache.end()) return it->second;
    auto d = std::make_shared<const mk::BreakDNA>(mk::analyzeBreak(*br));
    dnaCache[br] = d; return d;
}
void MickensProcessor::syncFreeze() {
    bool changed = false;
    for (int b = 0; b < mk::kBays; ++b) {
        const bool on = apvts.getRawParameterValue("b" + juce::String(b + 1) + "_freeze")->load() > 0.5f;
        if (on && !frozen[(size_t) b]) { if (auto d = bayDNA(b)) { frozen[(size_t) b] = d; changed = true; } }
        else if (!on && frozen[(size_t) b]) { frozen[(size_t) b] = nullptr; changed = true; }
    }
    if (changed) publish();
}
juce::StringArray MickensProcessor::dnaParamIds(int bay) {
    juce::StringArray a; const juce::String p = "b" + juce::String(bay + 1) + "_";
    for (auto* id : { "dna_mode", "remix", "lock", "lk_ks", "lk_pos", "lk_swing", "lk_acc", "lk_ghost", "freeze", "dna_seed" }) a.add(p + id);
    return a;
}
void MickensProcessor::dnaPush(int bay) {
    std::vector<float> v; for (auto& id : dnaParamIds(bay)) v.push_back(apvts.getRawParameterValue(id)->load());
    auto& h = dnaHistory[(size_t) bay]; if (!h.empty() && h.back() == v) return;
    h.push_back(v); if (h.size() > 64) h.erase(h.begin());
}
bool MickensProcessor::dnaUndo(int bay) {
    auto& h = dnaHistory[(size_t) bay]; if (h.empty()) return false;
    auto v = h.back(); h.pop_back(); auto ids = dnaParamIds(bay);
    for (int i = 0; i < ids.size(); ++i) setParam(ids[i], v[(size_t) i]);
    syncFreeze(); return true;
}
void MickensProcessor::dnaDice(int bay) {
    dnaPush(bay);
    setParam("b" + juce::String(bay + 1) + "_dna_seed", (float) juce::Random::getSystemRandom().nextInt(10000));
}

// ---------------------------------------------------------------- STEP LOCKS / SEQUENCE
mk::StepPlan MickensProcessor::previewPlan(int bay, long step) {
    const mk::Snapshot* S = currentSnapshot(); if (!S || !juce::isPositiveAndBelow(bay, mk::kBays)) return {};
    std::vector<float> v; liveValues(v); std::array<mk::BayParams, mk::kBays> B; mk::FxParams F; readParams(B, F, v.data());
    return mk::Engine::preview(B, F, S, bay, step);
}
void MickensProcessor::previewRow(int bay, mk::StepPlan* out, int steps, long from) {
    const mk::Snapshot* S = currentSnapshot(); if (!S || !juce::isPositiveAndBelow(bay, mk::kBays)) { for (int i = 0; i < steps; ++i) out[i] = {}; return; }
    std::vector<float> v; liveValues(v); std::array<mk::BayParams, mk::kBays> B; mk::FxParams F; readParams(B, F, v.data());
    for (int i = 0; i < steps; ++i) out[i] = mk::Engine::preview(B, F, S, bay, from + i);
}
void MickensProcessor::lockStep(int bay, int step) {
    if (!juce::isPositiveAndBelow(bay, mk::kBays) || !juce::isPositiveAndBelow(step, 32)) return;
    auto L = std::make_shared<mk::StepLocks>(); if (locks[(size_t) bay]) *L = *locks[(size_t) bay];
    auto p = previewPlan(bay, step); if (p.slice < 0) return;
    if (!L->s[(size_t) step].on) ++L->count; L->s[(size_t) step] = mk::lockFrom(p);
    locks[(size_t) bay] = L; publish();
}
void MickensProcessor::unlockStep(int bay, int step) {
    if (!juce::isPositiveAndBelow(bay, mk::kBays) || !juce::isPositiveAndBelow(step, 32) || !locks[(size_t) bay] || !locks[(size_t) bay]->s[(size_t) step].on) return;
    auto L = std::make_shared<mk::StepLocks>(*locks[(size_t) bay]); L->s[(size_t) step] = {}; --L->count;
    locks[(size_t) bay] = L->count ? L : nullptr; publish();
}
void MickensProcessor::clearLocks(int bay) { if (juce::isPositiveAndBelow(bay, mk::kBays) && locks[(size_t) bay]) { locks[(size_t) bay] = nullptr; publish(); } }
void MickensProcessor::printLocks(int bay) {
    if (!juce::isPositiveAndBelow(bay, mk::kBays)) return;
    auto L = std::make_shared<mk::StepLocks>();
    for (int s = 0; s < 32; ++s) { auto p = previewPlan(bay, s); if (p.slice >= 0) { L->s[(size_t) s] = mk::lockFrom(p); ++L->count; } }
    locks[(size_t) bay] = L->count ? L : nullptr; publish();
}
// one bay's 16ths from step s0 to s1 as notes (C3 + slice, ratchets as repeats), starting at tick 0
void MickensProcessor::bayMidi(int bay, long s0, long s1, juce::MidiMessageSequence& seq, int channel) {
    const int tps = 240;   // ticks per 16th (960 per quarter)
    auto br = breakAt(bayBreak[(size_t) juce::jlimit(0, mk::kBays - 1, bay)]); const int slices = br ? br->slices : 32;
    for (long s = s0; s < s1; ++s) { auto p = previewPlan(bay, s); if (p.slice < 0) continue;
        const int r = juce::jlimit(1, 4, p.ratchet); const int note = juce::jlimit(0, 127, 60 + (p.slice % juce::jmax(1, slices)));
        const float vel = juce::jlimit(0.1f, 1.0f, p.gain * 0.8f);
        for (int k = 0; k < r; ++k) { const double t0 = (s - s0) * tps + k * tps / (double) r, len = tps / (double) r * 0.9;
            seq.addEvent(juce::MidiMessage::noteOn(channel, note, vel), t0); seq.addEvent(juce::MidiMessage::noteOff(channel, note), t0 + len); } }
    seq.updateMatchedPairs();
}
juce::String MickensProcessor::rangeName(int b0, int b1) const { return "Mr Mickens " + juce::String(juce::roundToInt(lastBpm.load())) + "bpm bars " + juce::String(b0 + 1) + "-" + juce::String(b1); }
juce::File MickensProcessor::bounceRange(int b0, int b1) {
    if (!exportOk() || b1 <= b0 || b0 < 0) return {};   // worker thread: silent (the grip asked the gate on the message thread)
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("MrMickens"); dir.createDirectory();
    return renderTo(0xffffffffu, b1 - b0, dir.getNonexistentChildFile(rangeName(b0, b1), ".wav", true), b0);
}
juce::File MickensProcessor::exportMidiRange(int b0, int b1) {
    if (!exportOk() || b1 <= b0 || b0 < 0) return {};   // silent back-stop (the grip asked the gate)
    juce::MidiFile mf; mf.setTicksPerQuarterNote(960);
    for (int b = 0; b < mk::kBays; ++b) if (bp[b].mute->load() < 0.5f) { juce::MidiMessageSequence seq; seq.addEvent(juce::MidiMessage::textMetaEvent(3, "Bay " + juce::String(b + 1)), 0); bayMidi(b, b0 * 16L, b1 * 16L, seq, 1); mf.addTrack(seq); }
    {   // the drum machine on its own track (the notes Mickens itself plays them from: C1 D1 F#1 A#1)
        juce::MidiMessageSequence seq; seq.addEvent(juce::MidiMessage::textMetaEvent(3, "Drums"), 0); static const int dn[] = { 36, 38, 42, 46 };
        const bool allMuted = fx[kNumFx + 7]->load() > 0.5f;
        for (int d = 0; d < mk::kDrums; ++d) if (!allMuted && fx[kNumFx + 3 + d]->load() < 0.5f) for (long s = b0 * 16L; s < b1 * 16L; ++s) if (const int v = pattern[(size_t) d][(size_t) (s % mk::kSteps)]) {
            const double t0 = (s - b0 * 16L) * 240.0; seq.addEvent(juce::MidiMessage::noteOn(10, dn[d], (juce::uint8) v), t0); seq.addEvent(juce::MidiMessage::noteOff(10, dn[d]), t0 + 120); }
        seq.updateMatchedPairs(); if (seq.getNumEvents() > 1) mf.addTrack(seq);
    }
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("MrMickens"); dir.createDirectory();
    auto f = dir.getNonexistentChildFile(rangeName(b0, b1), ".mid", true);
    if (auto os = std::unique_ptr<juce::FileOutputStream>(f.createOutputStream())) mf.writeTo(*os);   // GATE: mick.section.midi
    return f;
}
juce::File MickensProcessor::exportMidi(int bay, int bars) {
    if (!exportOk()) return {};   // demo: export is part of the full version (silent back-stop; the drag button asked the gate)
    juce::MidiFile mf; mf.setTicksPerQuarterNote(960); juce::MidiMessageSequence seq;
    bayMidi(bay, 0, (long) bars * 16, seq, 1); mf.addTrack(seq);
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("MrMickens"); dir.createDirectory();
    auto f = dir.getNonexistentChildFile("Mr Mickens bay " + juce::String(bay + 1) + " " + juce::String(juce::roundToInt(lastBpm.load())) + "bpm", ".mid", true);
    if (auto os = std::unique_ptr<juce::FileOutputStream>(f.createOutputStream())) mf.writeTo(*os);   // GATE: mick.bay.midi
    return f;
}

// ---------------------------------------------------------------- BREAK SEARCH
mk::SearchIndex& MickensProcessor::searchIndex() {
    if (!search || searchVersion != listVersion.load()) {
        search = std::make_unique<mk::SearchIndex>(); searchVersion = listVersion.load();
        std::vector<float> brights;
        for (int i = 0; i < numBreaks(); ++i) { auto br = breakAt(i); if (!br) continue;
            mk::BreakEntry e; e.index = i; e.name = br->name; e.family = br->family; e.dna = dnaOf(br); e.bpm = br->bpm; brights.push_back(e.dna ? e.dna->bright : 0.5f);
            e.tags.add(br->user ? "dropped" : (i < (int) folderBreaks.size() ? "folder" : "built-in"));
            search->items.push_back(std::move(e)); }
        const float med = mk::pct(brights, 0.5f);   // BRIGHT / DARK relative to the whole list, not to 0.5
        for (auto& e : search->items) if (e.dna) { auto t = mk::feelTags(*e.dna, med); for (auto& x : t) e.tags.add(x); }
    }
    return *search;
}
void MickensProcessor::audition(int index) { auto br = breakAt(index); if (br) keepBreaks.push_back(br); engine.audition.store(br.get()); engine.auditionStart.store(true); }

// ---------------------------------------------------------------- SNAPSHOT DECK
juce::ValueTree MickensProcessor::modelTree() const {
    juce::ValueTree m("MODEL");
    for (int b = 0; b < mk::kBays; ++b) m.setProperty("bay" + juce::String(b), bayKey[(size_t) b], nullptr);
    juce::String pat; for (auto& r : pattern) for (auto v : r) pat << (int) v << ","; m.setProperty("pattern", pat, nullptr);
    for (int d = 0; d < mk::kDrums; ++d) m.setProperty("drum" + juce::String(d), drumName[(size_t) d], nullptr);
    for (int b = 0; b < mk::kBays; ++b) if (frozen[(size_t) b]) m.setProperty("frozen" + juce::String(b), frozen[(size_t) b]->toString(), nullptr);
    for (int b = 0; b < mk::kBays; ++b) if (locks[(size_t) b]) m.setProperty("locks" + juce::String(b), locksToString(*locks[(size_t) b]), nullptr);
    return m;
}
void MickensProcessor::applyModel(const juce::ValueTree& m) {
    for (int b = 0; b < mk::kBays; ++b) { auto k = m["bay" + juce::String(b)].toString(); if (k.isNotEmpty()) bayKey[(size_t) b] = k; }
    auto pat = juce::StringArray::fromTokens(m["pattern"].toString(), ",", "");
    if (pat.size() >= mk::kDrums * mk::kSteps) for (int d = 0; d < mk::kDrums; ++d) for (int s = 0; s < mk::kSteps; ++s) pattern[(size_t) d][(size_t) s] = (uint8_t) juce::jlimit(0, 127, pat[d * mk::kSteps + s].getIntValue());
    for (int d = 0; d < mk::kDrums; ++d) { auto nm = mk::renamedDrum(m["drum" + juce::String(d)].toString()); if (nm.isNotEmpty() && nm != drumName[(size_t) d]) { drumName[(size_t) d] = nm; if (userDrum[(size_t) d] && userDrum[(size_t) d]->name != nm) userDrum[(size_t) d] = nullptr; } }
    for (int b = 0; b < mk::kBays; ++b) { mk::BreakDNA d; auto str = m["frozen" + juce::String(b)].toString();
        frozen[(size_t) b] = str.isNotEmpty() && mk::BreakDNA::fromString(str, d) ? std::make_shared<const mk::BreakDNA>(d) : nullptr;
        locks[(size_t) b] = locksFromString(m["locks" + juce::String(b)].toString()); }
}
static juce::String fmtVal(float v) { juce::String s(juce::String::formatted("%.6g", (double) v)); return s == "-0" ? juce::String("0") : s; }
void MickensProcessor::storeSlot(int i) {
    if (!juce::isPositiveAndBelow(i, kSlots)) return;
    juce::ValueTree t("SLOT"); t.setProperty("idx", i, nullptr);
    juce::ValueTree pv("PARAMS");
    for (size_t k = 0; k < plist.size(); ++k) if (pkind[k] != 2) pv.setProperty(plist[k]->getParameterID(), fmtVal(praw[k]->load()), nullptr);
    t.appendChild(pv, nullptr); t.appendChild(modelTree(), nullptr);
    slots[(size_t) i] = { true, t }; refreshMorph();
}
void MickensProcessor::recallSlot(int i) {
    if (!juce::isPositiveAndBelow(i, kSlots) || !slots[(size_t) i].used) return;
    const auto& t = slots[(size_t) i].data; auto pv = t.getChildWithName("PARAMS");
    for (size_t k = 0; k < plist.size(); ++k) if (pkind[k] != 2 && pv.hasProperty(plist[k]->getParameterID()))
        setParam(plist[k]->getParameterID(), (float) pv[plist[k]->getParameterID()].toString().getDoubleValue());
    applyModel(t.getChildWithName("MODEL"));
    resolveBays();
}
juce::ValueTree MickensProcessor::slotTree(int i) const { return juce::isPositiveAndBelow(i, kSlots) && slots[(size_t) i].used ? slots[(size_t) i].data : juce::ValueTree(); }
mk::Snapshot* MickensProcessor::buildSnapshot(const juce::ValueTree& m) {
    auto s = std::make_unique<mk::Snapshot>(); s->gen = ++genCounter;
    for (int b = 0; b < mk::kBays; ++b) {
        const auto key = upgradeKey(m["bay" + juce::String(b)].toString()); mk::BreakPtr br = breakAt(keyIndex(key));
        if (!br) br = breakAt(bayBreak[(size_t) b]);
        if (br) { keepBreaks.push_back(br); s->brk[(size_t) b] = br.get(); if (auto d = dnaOf(br)) { keepDNA.push_back(d); s->dna[(size_t) b] = d.get(); } }
        mk::BreakDNA fd; auto str = m["frozen" + juce::String(b)].toString();
        if (str.isNotEmpty() && mk::BreakDNA::fromString(str, fd)) { auto sp = std::make_shared<const mk::BreakDNA>(fd); keepDNA.push_back(sp); s->groove[(size_t) b] = sp.get(); }
        if (auto L = locksFromString(m["locks" + juce::String(b)].toString())) { keepLocks.push_back(L); s->locks[(size_t) b] = L.get(); }
    }
    auto pat = juce::StringArray::fromTokens(m["pattern"].toString(), ",", "");
    s->drums = pattern;
    if (pat.size() >= mk::kDrums * mk::kSteps) for (int d = 0; d < mk::kDrums; ++d) for (int k = 0; k < mk::kSteps; ++k) s->drums[(size_t) d][(size_t) k] = (uint8_t) juce::jlimit(0, 127, pat[d * mk::kSteps + k].getIntValue());
    for (int d = 0; d < mk::kDrums; ++d) { const auto nm = mk::renamedDrum(m["drum" + juce::String(d)].toString());
        gb::SoundPtr ds = userDrum[(size_t) d] && userDrum[(size_t) d]->name == nm ? userDrum[(size_t) d] : drums.byName(nm);
        if (!ds) ds = drumSound(d); if (ds) { keepSounds.push_back(ds); s->drumSound[(size_t) d] = ds.get(); } }
    auto* raw = s.get(); snaps.push_back(std::move(s)); return raw;
}
void MickensProcessor::refreshMorph() {
    const bool on = morphOn->load() > 0.5f; const int a = (int) morphA->load() - 1, b = (int) morphB->load() - 1;
    lastMorphOn = on; lastMorphA = a; lastMorphB = b;
    if (!on || !juce::isPositiveAndBelow(a, kSlots) || !juce::isPositiveAndBelow(b, kSlots) || !slots[(size_t) a].used || !slots[(size_t) b].used) { morphSet.store(nullptr); return; }
    auto ms = std::make_unique<MorphSet>(); ms->a.resize(plist.size()); ms->b.resize(plist.size());
    auto pa = slots[(size_t) a].data.getChildWithName("PARAMS"), pb = slots[(size_t) b].data.getChildWithName("PARAMS");
    for (size_t k = 0; k < plist.size(); ++k) { const auto id = plist[k]->getParameterID(); const float live = praw[k]->load();
        ms->a[k] = pa.hasProperty(id) ? (float) pa[id].toString().getDoubleValue() : live; ms->b[k] = pb.hasProperty(id) ? (float) pb[id].toString().getDoubleValue() : live; }
    ms->snapA = buildSnapshot(slots[(size_t) a].data.getChildWithName("MODEL")); ms->snapB = buildSnapshot(slots[(size_t) b].data.getChildWithName("MODEL"));
    morphSet.store(ms.get()); keepMorph.push_back(std::move(ms));
}

// ---------------------------------------------------------------- PRESETS
void MickensProcessor::loadFactoryPreset(int i) {
    const auto& P = mk::factoryPresets(); if (!juce::isPositiveAndBelow(i, (int) P.size())) return;
    const auto& pr = P[(size_t) i];
    for (size_t k = 0; k < plist.size(); ++k) if (pkind[k] != 2) setParam(plist[k]->getParameterID(), plist[k]->convertFrom0to1(plist[k]->getDefaultValue()));
    setParam("morph_on", 0); setParam("stutter", 0);
    frozen = {}; locks = {}; for (auto& h : dnaHistory) h.clear();
    userDrum = {}; drumName = { "Hard Kick", "Big Snare", "Tight Hat", "Short Open Hat" };
    defaultPattern(pr.pattern);
    auto keys = juce::StringArray::fromTokens(pr.bays, ";", "");
    for (int b = 0; b < mk::kBays && b < keys.size(); ++b) if (keys[b] != "-" && keyIndex(keys[b]) >= 0) bayKey[(size_t) b] = keys[b];
    for (auto& kv : juce::StringArray::fromTokens(pr.params, " ", "")) { if (!kv.contains("=")) continue; setParam(kv.upToFirstOccurrenceOf("=", false, false), kv.fromFirstOccurrenceOf("=", false, false).getFloatValue()); }
    legacy.store(false); currentPreset = i; presetName = pr.name;
    resolveBays(); refreshMorph();
}

bool MickensProcessor::addUserBreak(const juce::File& f, int bay, juce::String& err) {
    auto br = mk::BreakLibrary::fromFile(f, lastBpm.load(), err); if (!br) return false;
    userBreaks.push_back(br);
    ++listVersion;
    if (juce::isPositiveAndBelow(bay, mk::kBays)) { pickBreak(bay, numBreaks() - 1); if (!br->tempoFound) setParam("b" + juce::String(bay + 1) + "_fit", 0.0f); } else publish();
    return true;
}
bool MickensProcessor::setUserDrum(int d, const juce::File& f, juce::String& err) {
    auto s = gb::DrumLibrary::fromFile(f, 48000.0, err); if (!s) return false;
    userDrum[(size_t) d] = s; publish(); return true;
}

void MickensProcessor::prepareToPlay(double sr, int block) {
    curSr = sr > 1000 ? sr : 48000.0;
    bypass.prepare(curSr, 0.006, bypassParam->get()); bypassStopPending = false; sync.prepare(curSr);
    gate.prepare(curSr, 0.006, playMode->load() > 0.5f); gateStopPending = false; heldNotes.reset(); heldCount = 0; lastHostPlay = false;
    demo.prepare(sr);
    engine.prepare(sr, block);
    chunk.setSize(2, juce::jmax(64, block)); subMidi.ensureSize(8192);
}

int MickensProcessor::pIndex(const juce::String& id) const { for (size_t i = 0; i < plist.size(); ++i) if (plist[i]->getParameterID() == id) return (int) i; return -1; }

void MickensProcessor::liveValues(std::vector<float>& v, int side) const {
    v.resize(plist.size());
    const MorphSet* ms = morphSet.load(); const float m = morphAmt->load();
    const int sd = side >= 0 ? side : morphSide.load();
    for (size_t i = 0; i < plist.size(); ++i) {
        const float live = praw[i]->load();
        if (!ms || pkind[i] == 2) v[i] = live;
        else if (pkind[i] == 0) v[i] = ms->a[i] + (ms->b[i] - ms->a[i]) * m;   // continuous: interpolate
        else v[i] = sd ? ms->b[i] : ms->a[i];                                      // stepped: one side or the other (switches on a bar line)
    }
}

void MickensProcessor::readParams(std::array<mk::BayParams, mk::kBays>& B, mk::FxParams& F, const float* vals) const {
    size_t c = 0; auto v = [&]() { return vals[pi[c++]]; };
    for (int b = 0; b < mk::kBays; ++b) {
        auto& o = B[(size_t) b];
        o.nudge = (int) std::lround(v()); o.grid = (int) v(); o.pitch = v(); o.level = v(); o.decay = v(); o.pan = v();
        o.rndStart = v() > 0.5f; o.rndChops = v() > 0.5f; o.rndAmt = v();
        o.duck[0] = v(); o.duck[1] = v(); o.duck[2] = v(); o.mute = v() > 0.5f; o.fit = v() > 0.5f;
        o.rx.mode = (int) v(); o.rx.remix = v(); o.rx.lock = v() / 100.0f;
        uint8_t mask = 0; for (uint8_t bit : { mk::kLockKS, mk::kLockPos, mk::kLockSwing, mk::kLockAcc, mk::kLockGhost }) if (v() > 0.5f) mask |= bit;
        o.rx.lockMask = mask; o.rx.freeze = v() > 0.5f; o.rx.seed = (uint32_t) v() * 7919u + (uint32_t) b * 104729u; o.role = (int) v();
    }
    const float seedV = v();
    for (auto& o : B) o.seed = (uint32_t) seedV * 2654435761u;
    F.crunch = v(); F.bits = v(); F.dry = v();
    F.lpCut = v(); F.lpMix = v(); F.hpCut = v(); F.hpMix = v(); F.bpCut = v(); F.bpMix = v(); F.notchCut = v(); F.notchMix = v();
    F.combCut = v(); F.combMix = v(); F.formant = v(); F.formMix = v(); F.res = v();
    F.chorus = v(); F.reverb = v(); F.size = v();
    F.stutCut = v(); F.stutEnv = v(); F.stutDecay = v();
    F.duckRelease = v(); F.breaksLevel = v(); F.drumsLevel = v();
    for (int d = 0; d < mk::kDrums; ++d) F.drumLvl[d] = v();
    F.volume = v();
    F.stutter = v() > 0.5f; F.stutDiv = (int) v(); F.bpm = v();
    for (int d = 0; d < mk::kDrums; ++d) F.drumMute[d] = v() > 0.5f;
    F.drumsMute = v() > 0.5f; F.keysBay = (int) v();
    // Mickens 2
    F.fillStyle = (int) v(); F.fillEvery = (int) v(); { static const int FL[] = { 4, 8, 16 }; F.fillLen16 = FL[juce::jlimit(0, 2, (int) v())]; } F.fillInt = v();
    F.fillSeed = (uint32_t) seedV * 31337u + 7u;
    F.ilMode = (int) v(); F.ilDuck = v();
    F.st.mode = (int) v(); F.st.gate = v(); F.st.decay = v(); F.st.pitch = v(); F.st.move = v();
    F.st.div = F.stutDiv; F.st.cut = F.stutCut; F.st.env = F.stutEnv; F.st.envDecay = F.stutDecay;
    auto& f = F.flt;
    f.route = (int) v(); f.morph = v(); f.autoGain = v() > 0.5f; f.combFb = v(); f.combTune = (int) v(); f.combNote = (int) std::lround(v());
    f.trigSrc = (int) v(); f.trigAmt = v(); f.trigDec = v(); f.trigRes = v();
    { static const int PB[] = { 0, 1, 2, 4, 8 }; f.phrBars = PB[juce::jlimit(0, 4, (int) v())]; } f.phrAmt = v(); f.phrShape = (int) v();
    F.interp = juce::jlimit(0, 2, (int) v());
    jassert(c == pi.size());
    f.dry = F.dry; f.cut[0] = F.lpCut; f.cut[1] = F.hpCut; f.cut[2] = F.bpCut; f.cut[3] = F.notchCut;
    f.mix[0] = F.lpMix; f.mix[1] = F.hpMix; f.mix[2] = F.bpMix; f.mix[3] = F.notchMix; f.mix[4] = F.combMix; f.mix[5] = F.formMix; f.res = F.res; f.combCut = F.combCut; f.formant = F.formant;
    // a session from before Mickens 2 keeps the classic engine paths until something new is switched on
    const bool leg = legacy.load(); F.legacyOut = leg;
    F.oldFilters = leg && f.route == mk::kParallel && !f.autoGain && f.trigSrc == 0 && f.phrBars == 0 && f.combTune == 0 && std::abs(f.combFb - 0.75f) < 1e-4f && f.morph < 1e-4f;
    F.oldStutter = leg && F.st.mode == mk::kRoll && F.st.gate >= 0.999f && F.st.decay <= 0.0001f && std::abs(F.st.pitch) < 0.001f && F.st.move <= 0.0001f;
}

void MickensProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals nd;
    const int n = buffer.getNumSamples(), maxN = chunk.getNumSamples();
    if (maxN <= 0 || n <= 0) { buffer.clear(); midi.clear(); return; }   // never prepared: nothing to do (and no spin)
    // BYPASS: on the engage edge everything stops; fully bypassed nothing renders (the clock keeps counting); MIDI is ignored meanwhile
    bypass.set(bypassParam->get());
    if (bypass.takeEngageEdge()) bypassStopPending = true;   // the 6 ms fade runs on the live sound; everything stops once it is silent
    if (bypass.isBypassed()) midi.clear();
    const MorphSet* ms = morphSet.load();
    const int wantSide = morphAmt->load() >= 0.5f ? 1 : 0;
    if (ms != audioMs) { audioMs = ms; if (ms) { morphSide.store(wantSide); engine.setSnapshot(wantSide ? ms->snapB : ms->snapA); } else engine.setSnapshot(liveSnap.load()); }
    std::array<mk::BayParams, mk::kBays> B; mk::FxParams F;
    liveValues(valsBuf); readParams(B, F, valsBuf.data());
    // SYNC (Shared/SyncTransport.h): DAW = the host's play / tempo / position drive Mr. Mickens (as always); OWN (or no host at all, e.g. the
    // standalone app) = its own clock, own tempo and own PLAY / SPACE. Everything below (FIT TEMPO, fills, INTERLOCK, stutter, the silent
    // idle clock, loop jumps, drag-outs, PLAY ON NOTES) runs on this one resolved transport.
    const auto hostPos = getPlayHead() != nullptr && wrapperType != wrapperType_Standalone ? getPlayHead()->getPosition() : juce::Optional<juce::AudioPlayHead::PositionInfo> {};   // the standalone app always runs on the own clock
    const auto tr = sync.resolve(hostPos, n);
    double bpm = F.bpm, ppq = 0; bool hostPlay = false;
    if (tr.fromDaw) {
        // DAW: the classic reading, unchanged (its tempo guard / missing-position rules keep old sessions bit-identical)
        if (auto t = hostPos->getBpm()) if (*t > 20.0 && *t < 999.0 && std::isfinite(*t)) { bpm = *t; hostHasTempo = true; }   // hosts report 0 without a tempo track
        hostPlay = hostPos->getIsPlaying();
        if (auto q = hostPos->getPpqPosition()) { if (std::isfinite(*q)) ppq = *q; else hostPlay = false; } else hostPlay = false;
        if (hostHasTempo) F.bpm = (float) bpm;   // follow the project tempo even while the host is stopped (internal play + drag-out)
        if (ownClockWas) { ownClockWas = false; sync.stopOwn(); }   // OWN -> DAW: the own clock stops (nothing keeps running unseen)
    } else {
        bpm = tr.bpm; ppq = tr.ppq; hostPlay = tr.playing; F.bpm = (float) bpm;   // the own clock drives the engine like a host transport would
        if (!ownClockWas) { ownClockWas = true; engine.playing.store(false); }       // DAW -> OWN: the classic preview clock stops; own PLAY starts it
    }
    const bool keepTempo = hostHasTempo || !tr.fromDaw;
    hostRunning.store(hostPlay); lastBpm.store(bpm); ownClockUi.store(!tr.fromDaw);
    const bool onNotes = playMode->load() > 0.5f;
    if (bypass.silent()) { if (bypassStopPending) { engine.stopAll(); bypassStopPending = false; } engine.idle(n, bpm); buffer.clear(); midi.clear();
        // BYPASS wins over PLAY ON NOTES: the note-offs it swallows can't leave the gate stuck open; after un-bypass the next note opens it
        heldNotes.reset(); heldCount = 0; gate.prepare(curSr, 0.006, onNotes); gateStopPending = false; lastHostPlay = hostPlay; gateOpenUi.store(!onNotes);
        audioMsDone.store(ms); return; }
    // PLAY: ON NOTES. Held notes gate the beat; the gate opens / closes at the note's own sample (sample-accurate splits below).
    // A transport stop forgets the held notes (no stuck gate). In ALWAYS the gate is open and a bit-exact no-op.
    // Notes are counted per key (a retrigger of a held key doesn't double-count); all-notes-off / all-sound-off close it too.
    int gateEvents[64]; bool gateClosed[64]; int nGate = 0;
    auto pushGate = [&](int at, bool closed) { if (nGate < 64) ++nGate; gateEvents[nGate - 1] = at; gateClosed[nGate - 1] = closed; };   // full: the newest replaces the last (the final state is always right)
    if (onNotes) {
        if (lastHostPlay && !hostPlay) { if (heldCount > 0) pushGate(0, true); heldNotes.reset(); heldCount = 0; }
        if (!gate.isBypassed() && heldCount == 0) pushGate(0, true);   // just switched ALWAYS -> ON NOTES with no note held
        for (const auto md : midi) { const auto m = md.getMessage(); const int at = juce::jlimit(0, n - 1, md.samplePosition);
            if (m.isNoteOn()) { const int nn = m.getNoteNumber(); if (!heldNotes[(size_t) nn]) { heldNotes.set((size_t) nn); if (++heldCount == 1) pushGate(at, false); } }
            else if (m.isNoteOff()) { const int nn = m.getNoteNumber(); if (heldNotes[(size_t) nn]) { heldNotes.reset((size_t) nn); if (--heldCount == 0) pushGate(at, true); } }
            else if (m.isAllNotesOff() || m.isAllSoundOff()) { if (heldCount > 0) pushGate(at, true); heldNotes.reset(); heldCount = 0; } }
    } else { heldNotes.reset(); heldCount = 0; if (gate.isBypassed()) pushGate(0, false); }
    lastHostPlay = hostPlay;
    // opening from full silence snaps (the engine restarts from nothing, so the first hit keeps its full attack, on the host's 16th grid);
    // opening during a fade-out (a quick retrigger) fades back in; closing always fades (~6 ms) and stops everything once silent
    auto gateApply = [&](bool closed) {
        if (!closed && gate.silent()) { if (gateStopPending) { engine.stopAll(); gateStopPending = false; } gate.prepare(curSr, 0.006, false); return; }
        gate.set(closed); if (gate.takeEngageEdge()) gateStopPending = true; };
    int gi = 0; while (gi < nGate && gateEvents[gi] <= 0) { gateApply(gateClosed[gi]); ++gi; }
    const double spbS = curSr * 60.0 / bpm;
    // SNAPSHOT MORPH: the stepped settings (breaks, patterns, modes...) change sides on the next bar line, sample-exact
    int switchAt = -1;
    if (ms && wantSide != morphSide.load()) {
        const bool running = hostPlay || engine.playing.load();
        const double beat = hostPlay ? ppq : engine.clockBeats();
        if (!running) switchAt = 0;
        else { const double toBar = std::fmod(4.0 - std::fmod(beat, 4.0), 4.0); const int at = (int) std::floor(toBar * spbS); if (at < n) switchAt = juce::jmax(0, at); }   // floor: the bar-line 16th itself already plays the new side
    }
    // the engine works in chunks no bigger than prepareToPlay's block size (and splits at the morph's bar line)
    const bool loopOn = sectionLooping(); const double LA = secA.load() * 4.0, LB = secB.load() * 4.0, LL = juce::jmax(4.0, LB - LA);
    auto posmod = [](double x, double m) { const double r = std::fmod(x, m); return r < 0 ? r + m : r; };
    for (int pos = 0; pos < n;) {
        if (pos == switchAt) { morphSide.store(wantSide); engine.setSnapshot(wantSide ? ms->snapB : ms->snapA); liveValues(valsBuf); const float keepBpm = F.bpm; readParams(B, F, valsBuf.data()); if (keepTempo) F.bpm = keepBpm; }
        while (gi < nGate && gateEvents[gi] <= pos) { gateApply(gateClosed[gi]); ++gi; }
        int len = juce::jmin(maxN, n - pos); if (switchAt > pos && switchAt < pos + len) len = switchAt - pos;
        if (gi < nGate && gateEvents[gi] > pos && gateEvents[gi] < pos + len) len = gateEvents[gi] - pos;
        // LOOP SECTION: the song position (DAW / own clock) is mapped into the section (its timeline starts at 0 = the section start) and
        // the chunk splits at the wrap sample; the DAW-stopped preview clock jumps back to the section start at its end
        double chunkPpq = ppq + pos / spbS;
        if (loopOn) {
            if (hostPlay) { chunkPpq = LA + posmod(chunkPpq, LL); const int toWrap = (int) std::ceil((LB - chunkPpq) * spbS - 1e-6); if (toWrap > 0 && toWrap < len) len = toWrap; }
            else if (engine.playing.load()) { double b = engine.restartFlag.load() ? engine.restartBeats.load() : engine.clockBeats();   // a pending PLAY restart counts
                if (b >= LB - 1e-9 || b < LA - 1e-9) { b = b < LA ? LA + posmod(b, LL) : LA + posmod(b - LB, LL); engine.restartAt(b); }
                const int toWrap = (int) std::ceil((LB - b) * spbS - 1e-6); if (toWrap > 0 && toWrap < len) len = toWrap; }
        }
        juce::AudioBuffer<float> view(chunk.getArrayOfWritePointers(), 2, len);
        if (gate.silent()) { if (gateStopPending) { engine.stopAll(); gateStopPending = false; } engine.idle(len, bpm); view.clear(); }   // no note held: nothing runs, the clock counts
        else {
            subMidi.clear(); for (const auto m : midi) if (m.samplePosition >= pos && m.samplePosition < pos + len) subMidi.addEvent(m.getMessage(), m.samplePosition - pos);
            engine.process(view, B, F, bpm, chunkPpq, hostPlay, subMidi);
            gate.applyMute(view);
        }
        for (int c = 0; c < juce::jmin(2, buffer.getNumChannels()); ++c) buffer.copyFrom(c, pos, view, c, 0, len);
        pos += len;
    }
    gateOpenUi.store(!gate.isBypassed());
    midi.clear();
    // MUTE: keep following the host (so it's in time when un-muted) but output nothing, with a short fade
    const bool mute = apvts.getRawParameterValue("mute_all")->load() > 0.5f;
    if (mute || muteGain < 1.0f) {
        const float target = mute ? 0.0f : 1.0f, step = 1.0f / (float) juce::jmax(1.0, curSr * 0.01);
        for (int i = 0; i < n; ++i) { muteGain += juce::jlimit(-step, step, target - muteGain); for (int c = 0; c < buffer.getNumChannels(); ++c) buffer.getWritePointer(c)[i] *= muteGain; }
    }
    demo.process(buffer, license.isUnlocked());
    bypass.applyMute(buffer);   // the 6 ms fade on engage / release (a bit-exact no-op while playing)
    audioMsDone.store(ms);
}

juce::File MickensProcessor::renderTo(uint32_t mask, int bars, const juce::File& f, int startBar) {
    // a private engine renders the current setup offline at 48 kHz, from bar 1. It captures what you hear: the current
    // settings (mid-morph too) at the host tempo. New sessions pre-roll one bar so filters / followers / tails are warm
    // (like live playing) and read with the HQ sinc; legacy sessions render exactly as the old build did.
    if (!exportOk()) return {};   // demo: no render reaches the disk (the callers asked the gate; this is the back-stop)
    struct Busy { std::atomic<int>& c; explicit Busy(std::atomic<int>& x) : c(x) { ++c; } ~Busy() { --c; } } busy(renderBusy);
    const double sr = 48000; const int block = 512;
    auto off = std::make_unique<mk::Engine>(); off->prepare(sr, block);
    const mk::Snapshot* S = currentSnapshot(); if (!S) return {};
    off->setSnapshot(S); off->outMask = mask; off->playing.store(true);
    std::vector<float> vals; liveValues(vals, morphAmt->load() >= 0.5f ? 1 : 0);
    std::array<mk::BayParams, mk::kBays> B; mk::FxParams F; readParams(B, F, vals.data()); F.stutter = false;
    if (!F.legacyOut && F.interp != mk::kLoFi) F.interp = mk::kHQ;
    const double bpm = juce::jlimit(20.0, 999.0, lastBpm.load()); F.bpm = (float) bpm;
    const int loop = (int) (bars * 4 * 60.0 / bpm * sr), pre = F.legacyOut ? 0 : (int) (4 * 60.0 / bpm * sr), total = loop + (int) (sr * 2.0);
    if (startBar > 0) off->restartAt(startBar * 4.0 - (pre > 0 ? 4.0 : 0.0));   // a section: from its own bar (fills / phrases land as in the song)
    else if (pre > 0) off->restartAt(-4.0);
    juce::AudioBuffer<float> all(2, total), buf(2, block); juce::MidiBuffer none; all.clear();
    for (int pos = -pre; pos < total; pos += block) {
        const int n = juce::jmin(block, total - pos); buf.setSize(2, n, false, false, true);
        off->playing.store(pos < loop);
        off->process(buf, B, F, bpm, 0, false, none);
        if (pos >= 0) for (int c = 0; c < 2; ++c) all.copyFrom(c, pos, buf, c, 0, n);
        else if (pos + n > 0) for (int c = 0; c < 2; ++c) all.copyFrom(c, 0, buf, c, -pos, pos + n);
    }
    f.deleteFile();
    juce::WavAudioFormat wav;
    if (auto os = std::unique_ptr<juce::OutputStream>(f.createOutputStream()))
        if (auto* w = wav.createWriterFor(os.get(), sr, 2, 24, {}, 0)) { os.release();   // GATE: via-caller (mick.mix.wav, mick.stems.wav, mick.section.wav)
            std::unique_ptr<juce::AudioFormatWriter> wr(w); wr->writeFromAudioSampleBuffer(all, 0, total); }
    return f;
}
juce::String MickensProcessor::describeMix() const {
    juce::StringArray bays, drumsOn;
    for (int b = 0; b < mk::kBays; ++b) if (bp[b].mute->load() < 0.5f) bays.add(juce::String(b + 1));
    static const char* dn[] = { "Kick", "Snare", "Hat", "OpenHat" };
    const bool allMuted = fx[kNumFx + 7]->load() > 0.5f;
    for (int d = 0; d < mk::kDrums; ++d) { bool any = false; for (auto v : pattern[(size_t) d]) any |= v > 0; if (any && ! allMuted && fx[kNumFx + 3 + d]->load() < 0.5f) drumsOn.add(dn[d]); }
    juce::String s = bays.isEmpty() ? juce::String("no bays") : "Bay " + bays.joinIntoString("+");
    if (! drumsOn.isEmpty()) s << " " << drumsOn.joinIntoString(" ");
    return juce::File::createLegalFileName(s);
}

juce::File MickensProcessor::bounce(int bars) {
    if (! exportOk()) return {};   // worker thread: silent (the drag button asked the gate on the message thread)
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("MrMickens"); dir.createDirectory();
    // every drag-out is its own new file named after what's playing ("Mr Mickens 140bpm Bay 1+2 Kick Hat.wav", then " (2)"...)
    // so a later drag never overwrites a clip that's already in your DAW
    return renderTo(0xffffffffu, bars, dir.getNonexistentChildFile("Mr Mickens " + juce::String(juce::roundToInt(lastBpm.load())) + "bpm " + describeMix(), ".wav", true));
}
juce::StringArray MickensProcessor::bounceStems(int bars) {
    if (! exportOk()) return {};   // worker thread: silent (the drag button asked the gate on the message thread)
    auto dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("MrMickens").getNonexistentChildFile("Stems " + juce::String(juce::roundToInt(lastBpm.load())) + "bpm " + describeMix(), "", true);
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
    for (int i = st.getNumChildren() - 1; i >= 0; --i) if (st.getChild(i)["id"].toString() == "bypass") st.removeChild(i, nullptr);   // BYPASS is never saved
    auto m = modelTree();
    m.setProperty("dark", theme == 1, nullptr); m.setProperty("theme", theme, nullptr); m.setProperty("theme_name", juce::String(theme == 1 ? "dark" : theme == 2 ? "light" : theme == 3 ? "ghost" : "purple"), nullptr);   // "dark" as before (older builds read it), "theme" adds LIGHT
    m.setProperty("mk2", 1, nullptr); m.setProperty("legacy", legacy.load(), nullptr);
    m.setProperty("sync", (int) sync.getMode(), nullptr); m.setProperty("own_bpm", sync.getOwnBpm(), nullptr);   // per project (file loads ignore them)
    if (secA >= 0 && secB > secA) { m.setProperty("sec_a", secA.load(), nullptr); m.setProperty("sec_b", secB.load(), nullptr); if (secLoop.load()) m.setProperty("sec_loop", true, nullptr); }
    if (presetName.isNotEmpty()) m.setProperty("preset", presetName, nullptr);
    { juce::ValueTree sl("SLOTS"); for (int i = 0; i < kSlots; ++i) if (slots[(size_t) i].used) sl.appendChild(slots[(size_t) i].data.createCopy(), nullptr); if (sl.getNumChildren() > 0) m.appendChild(sl, nullptr); }
    auto enc = [](const std::vector<float>& x) { juce::MemoryBlock b(x.size() * 2); auto* p = (int16_t*) b.getData(); for (size_t i = 0; i < x.size(); ++i) p[i] = (int16_t) juce::jlimit(-32767.0f, 32767.0f, x[i] * 32767.0f); return b.toBase64Encoding(); };
    for (auto& ub : userBreaks) { if (ub->b64.isEmpty()) ub->b64 = enc(ub->data);   // encoded once; the audio never changes
        juce::ValueTree u("USERBREAK"); u.setProperty("name", ub->name, nullptr); u.setProperty("rate", ub->rate, nullptr); u.setProperty("bpm", ub->bpm, nullptr); u.setProperty("slices", ub->slices, nullptr); u.setProperty("tempo", ub->tempoFound, nullptr); u.setProperty("data", ub->b64, nullptr); m.appendChild(u, nullptr); }
    for (int d = 0; d < mk::kDrums; ++d) if (auto& s = userDrum[(size_t) d]) { juce::ValueTree u("USERDRUM"); u.setProperty("lane", d, nullptr); u.setProperty("name", s->name, nullptr); u.setProperty("rate", s->rate, nullptr); u.setProperty("data", enc(s->data), nullptr); m.appendChild(u, nullptr); }
    st.appendChild(m, nullptr);
    if (auto xml = st.createXml()) copyXmlToBinary(*xml, mb);
}
void MickensProcessor::setStateInformation(const void* data, int size) {
    ghosty::StateLoadNotify notifyHost (*this);   // CLAP: tell the host the values changed (Shared/HostNotify.h)
    if (! license.isUnlocked()) return;   // demo mode: a reopened project starts at the default sound (unlocking brings the saved sound back)
    restoreState(data, size, true);
}
void MickensProcessor::restoreState(const void* data, int size, bool fromHost) {
    auto xml = getXmlFromBinary(data, size); if (!xml) return;
    auto st = juce::ValueTree::fromXml(*xml); if (!st.isValid()) return;
    auto m = st.getChildWithName("MODEL"); st.removeChild(m, nullptr);
    // sessions from before Mickens 2: the new switches start where the classic sound is unchanged
    const bool leg = !m.isValid() || !m.hasProperty("mk2") || (bool) m.getProperty("legacy", false);
    legacy.store(leg);
    const bool hadAuto = st.getChildWithProperty("id", "flt_auto").isValid(), hadInterp = st.getChildWithProperty("id", "interp").isValid();
    const bool wasBypassed = bypassParam->get(), hadPlay = st.getChildWithProperty("id", "play_mode").isValid();
    auto* pm = apvts.getParameter("play_mode"); const float pmWas = pm->getValue();
    apvts.replaceState(st);
    if (bypassParam->get() != wasBypassed) bypassParam->setValueNotifyingHost(wasBypassed ? 1.0f : 0.0f);   // loading a session never touches BYPASS
    // PLAY mode belongs to the project: the host's session restore sets it (sessions from before it existed -> ALWAYS); preset / .mickens file loads keep the current one
    const float pmNow = fromHost ? (hadPlay ? pm->getValue() : 0.0f) : pmWas;
    if (std::abs(pm->getValue() - pmNow) > 1.0e-6f) pm->setValueNotifyingHost(pmNow);
    // SYNC mode + own tempo: per project like PLAY mode. Missing (older sessions) = SYNC TO DAW, own tempo = the session's TEMPO knob
    if (fromHost) { const bool has = m.isValid() && m.hasProperty("sync");
        sync.setMode(has && (int) m["sync"] == 1 ? ghosty::SyncTransport::Mode::Own : ghosty::SyncTransport::Mode::Daw);
        sync.setOwnBpm(m.isValid() && m.hasProperty("own_bpm") ? (double) m["own_bpm"] : (double) apvts.getRawParameterValue("bpm")->load()); }
    if (leg && !hadAuto) if (auto* p = apvts.getParameter("flt_auto")) p->setValueNotifyingHost(0.0f);
    if (leg && !hadInterp) if (auto* p = apvts.getParameter("interp")) p->setValueNotifyingHost(0.0f);   // the classic linear reads
    for (auto& h : dnaHistory) h.clear();
    for (auto& sl : slots) sl = {};
    secA = -1; secB = -1; secLoop = false;   // the LOOP SECTION: only what this session saved (older sessions: none)
    frozen = {}; locks = {}; currentPreset = -1; presetName = m.isValid() ? m["preset"].toString() : juce::String();
    if (!m.isValid()) { publish(); refreshMorph(); return; }
    for (auto sv : m.getChildWithName("SLOTS")) { const int i = sv["idx"]; if (juce::isPositiveAndBelow(i, kSlots)) slots[(size_t) i] = { true, sv.createCopy() }; }
    auto dec = [](const juce::String& s) { juce::MemoryBlock b; b.fromBase64Encoding(s); std::vector<float> x(b.getSize() / 2); auto* p = (const int16_t*) b.getData(); for (size_t i = 0; i < x.size(); ++i) x[i] = p[i] / 32767.0f; return x; };
    userBreaks.clear(); userDrum = {};
    for (auto u : m) {   // session data is clamped before it reaches the audio thread
        if (u.hasType("USERBREAK")) { auto b = std::make_shared<mk::Break>(); b->name = u["name"].toString(); b->family = "Your breaks"; b->user = true; b->rate = juce::jlimit(8000.0, 384000.0, (double) u["rate"]); b->bpm = juce::jlimit(20.0, 999.0, (double) u["bpm"]); b->slices = juce::jlimit(1, 128, (int) u["slices"]); b->tempoFound = (bool) u.getProperty("tempo", true); b->data = dec(u["data"].toString()); if (b->data.size() > 64) { b->b64 = u["data"].toString(); userBreaks.push_back(b); } }
        if (u.hasType("USERDRUM")) { const int d = u["lane"]; if (!juce::isPositiveAndBelow(d, mk::kDrums)) continue; auto s = std::make_shared<gb::Sound>(); s->name = u["name"].toString(); s->category = "Sample"; s->rate = juce::jlimit(8000.0, 384000.0, (double) u["rate"]); s->data = dec(u["data"].toString()); if (s->data.size() > 64) userDrum[(size_t) d] = s; }
    }
    theme = m.hasProperty("theme") ? ((int) m["theme"] == 3 ? 3 : juce::jlimit(0, 2, (int) m["theme"])) : ((bool) m.getProperty("dark", false) ? 1 : 0);   // 3 = GHOST; anything else unknown as before
    if (m.hasProperty("sec_a") && m.hasProperty("sec_b")) { const int a = juce::jlimit(0, 63, (int) m["sec_a"]), b = juce::jlimit(1, 64, (int) m["sec_b"]); if (b > a) { secA = a; secB = b; secLoop = (bool) m.getProperty("sec_loop", false); } }
    applyModel(m);
    resolveBays();
    refreshMorph();
}

juce::AudioProcessorEditor* MickensProcessor::createEditor() { return new MickensEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MickensProcessor(); }
