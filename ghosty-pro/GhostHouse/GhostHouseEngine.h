#pragma once
// Ghost House sound engine (Block Step's engine plus swing): the pattern the page sends (JSON) played sample-accurately,
// synced to the host's ppq or to its own clock when the host is stopped. One-shot sample voices (Brandon's GhostBlock kit +
// the synthesised house pack, mp3, decoded in the background), built-in synth drums rendered once at load, and the tracker FX of the web version:
// CUT, RETRIG, VSLIDE (swell), PSLIDE, VIB, ARP, NOTEOFF, DELAY, plus a per-channel choke.
// Real-time rules: no allocation, locks or decoding on the audio thread.
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <array>
#include <memory>
#include <vector>
#include <deque>
#include <cmath>
#include "GhostSynth.h"
#include "UserBanks.h"
#include "SampleBass.h"
#include "BassRoots.h"

namespace ghouse {

constexpr int kInst = 12, kMaxCh = 16, kMaxVoices = 192, kCats = 10;
constexpr int kPianoInst = 10, kCatPiano = 9, kSynthInst = 11;   // + the SYNTH page's instrument (GhostSynth, no samples)   // the page's PIANO (INST 10) and its sample category
enum Fx : uint8_t { FX_NONE, FX_CUT, FX_RETRIG, FX_VSLIDE, FX_PSLIDE, FX_VIB, FX_ARP, FX_NOTEOFF, FX_DELAY, FX_ECHO, FX_PING, FX_TAPE, FX_STUT };
// instrument -> sample category (index into kCatNames) and built-in synth sound; same order as INST in the page
static const char* const kCatNames[kCats] = { "kick", "snare", "clap", "hat", "ohat", "perc", "bass", "fx", "crash", "piano" };
static constexpr int kInstCat[kInst] = { 0, 1, 2, 3, 4, 5, 5, 8, 6, 7, 9, -1 };
enum Synth { SY_KICK, SY_SNARE, SY_CLAP, SY_HAT, SY_OHAT, SY_TOM, SY_RIM, SY_CRASH, SY_SUB, SY_FX, SY_PIANO, SY_COUNT };
static constexpr int kInstSynth[kInst] = { SY_KICK, SY_SNARE, SY_CLAP, SY_HAT, SY_OHAT, SY_TOM, SY_RIM, SY_CRASH, SY_SUB, SY_FX, SY_PIANO, SY_PIANO };

struct Cell { int8_t i = -1; int16_t s = -1; int8_t ab = -1; /* ACID BASS SAMPLES: which bass an audition plays (-1 = the setting) */ int8_t p = 0; uint8_t v = 80; uint8_t fx = FX_NONE; uint8_t pr = 4; uint8_t q = 8; int8_t pn = 0;   // pn: pan, -100 left .. 100 right   // q: FADE of ECHO / PING / TAPE / STUT (0 long tail .. 16 quick)
             
              float l = 0; bool m = false; float o = 0; bool sl = false, ac = false; int8_t ax = 0, pg = 0, vr = 0, ct = 0, ft = 0; uint8_t fl = 0, st = 0, pc = 100; bool rv = false; /* GLITCH: accel, pitch ramp, vel ramp, cents, filter, flam ms, start %, chance %, reverse */   // sl: slide into the next note, ac: accent (SYNTH)   // o: a note between the grid lines, as a fraction of its row (-0.5..0.5)   // l: length in steps (0 = rings out), m: muted (the page's FL-style tools)
              bool on() const { return i >= 0; } };

inline uint8_t fxFromString(const juce::String& f) {
    static const char* const names[] = { "", "CUT", "RETRIG", "VSLIDE", "PSLIDE", "VIB", "ARP", "NOTEOFF", "DELAY", "ECHO", "PING", "TAPE", "STUT" };
    for (uint8_t k = 1; k < 13; ++k) if (f == names[k]) return k;
    return FX_NONE;
}
inline Cell cellFromVar(const juce::var& c) {
    Cell x; if (! c.isObject()) return x;
    x.i = (int8_t) juce::jlimit(0, kInst - 1, (int) c["i"]);
    x.s = c.hasProperty("s") && ! c["s"].isVoid() ? (int16_t) juce::jlimit(-2, 32767, (int) c["s"]) : (int16_t) -1;
    x.p = (int8_t) juce::jlimit(-48, 48, (int) c["p"]);   // 88 keys
    x.v = (uint8_t) juce::jlimit(1, 100, c.hasProperty("v") ? (int) c["v"] : 80);
    x.fx = fxFromString(c["fx"].toString());
    x.pr = (uint8_t) juce::jlimit(0, 16, c.hasProperty("pr") ? (int) c["pr"] : 4);
    x.pn = (int8_t) juce::jlimit(-100, 100, c.hasProperty("pn") && ! c["pn"].isVoid() ? (int) c["pn"] : 0);
    x.q = (uint8_t) juce::jlimit(0, 16, c.hasProperty("q") && ! c["q"].isVoid() ? (int) c["q"] : 8);
    x.l = c.hasProperty("l") ? (float) juce::jlimit(0.0, 1024.0, (double) c["l"]) : 0.0f;
    x.m = c.hasProperty("m") && (bool) c["m"];
    x.o = c.hasProperty("o") ? (float) juce::jlimit(-0.5, 0.5, (double) c["o"]) : 0.0f;
    x.sl = c.hasProperty("sl") && (bool) c["sl"]; x.ac = c.hasProperty("ac") && (bool) c["ac"];
    auto gi = [&](const char* k, int lo, int hi, int d) { return c.hasProperty(k) && ! c[k].isVoid() ? juce::jlimit(lo, hi, (int) c[k]) : d; };   // GLITCH
    x.ax = (int8_t) gi("ax", -100, 100, 0); x.pg = (int8_t) gi("pg", -24, 24, 0); x.vr = (int8_t) gi("vr", -100, 100, 0); x.ct = (int8_t) gi("ct", -100, 100, 0); x.ft = (int8_t) gi("ft", -100, 100, 0);
    x.fl = (uint8_t) gi("fl", 0, 60, 0); x.st = (uint8_t) gi("st", 0, 90, 0); x.pc = (uint8_t) gi("pc", 0, 100, 100); x.rv = c.hasProperty("rv") && (bool) c["rv"];
    x.ab = (int8_t) (c.hasProperty("ab") && ! c["ab"].isVoid() ? juce::jlimit(-1, ghosty::bassroots::kCount - 1, (int) c["ab"]) : -1);
    return x;
}

// What the audio thread plays. Built on the message thread from the page's state, then swapped in.
struct Pattern {
    int rows = 0, ch = 0; std::vector<Cell> cells;
    std::vector<double> rowT { 0.0 }; std::vector<double> rowD, rowE;   // the page's GRID: row start (16th steps, rows + 1 entries), row length, earliest note (<= 0)
    bool loopOn = false; int la = 0, lb = 0;
    bool useKit = true; std::array<int, kInst> defSnd {}; double bpm = 124;
    double mul = 1;   // TEMPO ½x | 1x | 2x (owner 2026-11-07): the beat runs at half / the same / double the tempo, locked to the DAW's bars
    static double mulOf(const juce::var& v) { const double m = v.isVoid() ? 1.0 : (double) v; return m == 0.5 || m == 2.0 ? m : 1.0; }
    double swing = 56;   // %: every second 16th lands (swing - 50) / 50 of a step late, same as the page
    double swingDelay(int step, double sd) const { return (step & 1) ? (swing - 50.0) / 50.0 * sd : 0.0; }
    std::array<float, kInst> drumVol {}; std::array<float, kMaxCh> chVol {}; std::array<float, kMaxCh> chPan {}; std::array<bool, kMaxCh> chOn {};   // the page's MIX + channel panel
    bool keyOn = false, keyRootMode = false; int keyRoot = 9; std::array<bool, 12> keyPcs {};   // the page's ♪ KEY
    bool acidBass = false; int acidB = 0;   // ACID SOUND: BASS SAMPLES (one of the 17 Block Step basses) instead of the synth
    std::array<gsyn::Knobs, gsyn::kSounds> knobs {}; float synSpace = 0.2f, synEcho = 0.1f;   // the SYNTH page's knobs (per sound) + bus FX
    std::vector<std::shared_ptr<const Pattern>> along; bool selfMuted = false;   // LAYERS: the page on screen muted   // LAYERS: the other pages playing along with this one
    Pattern() { drumVol.fill(1.0f); chVol.fill(1.0f); chOn.fill(true); for (int k = 0; k < gsyn::kSounds; ++k) knobs[(size_t) k] = gsyn::defaultKnobs(k); }
    int synthSound(const Cell& x) const { return juce::jlimit(0, gsyn::kSounds - 1, x.s >= 0 ? (int) x.s : juce::jmax(0, defSnd[(size_t) (kInst - 1)])); }
    // steps from row r to the next note of this synth sound on any channel (wrapping through the loop / pattern), 0 = none
    double nextSynthSteps(int r, int snd) const {
        const int a = loopOn ? la : 0, b = loopOn ? lb : rows - 1; if (rows <= 0 || r < a || r > b) return 0; const double C = rowT[(size_t) b + 1] - rowT[(size_t) a];
        for (int k = 1; k <= b - a + 1; ++k) { int q = r + k; double add = 0; if (q > b) { q -= b - a + 1; add = C; }
            for (int c = 0; c < ch; ++c) { const Cell& x = at(q, c); if (x.on() && ! x.m && chOn[(size_t) c] && x.i == kInst - 1 && synthSound(x) == snd) return rowT[(size_t) q] + add - rowT[(size_t) r]; } }
        return 0;
    }
    // steps from row r to the next note on channel c (wrapping through the loop / pattern), 0 = none
    double nextNoteSteps(int r, int c) const {
        const int a = loopOn ? la : 0, b = loopOn ? lb : rows - 1; if (rows <= 0 || r < a || r > b) return 0; const double C = rowT[(size_t) b + 1] - rowT[(size_t) a];
        for (int k = 1; k <= b - a + 1; ++k) { int q = r + k; double add = 0; if (q > b) { q -= b - a + 1; add = C; }
            if (at(q, c).on() && ! at(q, c).m) return rowT[(size_t) q] + add - rowT[(size_t) r]; }
        return 0;
    }
    void buildTimes(const juce::var& pt) {   // rows per beat from "rpb" ({beat: k}); anything that doesn't add up plays as plain 16ths
        rowT.assign((size_t) rows + 1, 0.0); rowD.assign((size_t) rows, 1.0); rowE.assign((size_t) rows, 0.0);
        bool ok = false; const int beats = (int) std::lround((double) pt["bars"] * 4.0);
        if (auto* o = pt["rpb"].getDynamicObject(); o != nullptr && beats > 0 && beats <= 512 * 4) {
            std::vector<int> k((size_t) beats, 4); for (auto& pr : o->getProperties()) { const int b = pr.name.toString().getIntValue(); if (b >= 0 && b < beats) k[(size_t) b] = juce::jlimit(1, 64, (int) pr.value); }
            int tot = 0; for (int x : k) tot += x;
            if (tot == rows) { int r = 0; for (int b = 0; b < beats; ++b) for (int i = 0; i < k[(size_t) b]; ++i, ++r) { rowT[(size_t) r] = b * 4.0 + i * 4.0 / k[(size_t) b]; rowD[(size_t) r] = 4.0 / k[(size_t) b]; } ok = true; }
        }
        if (! ok) for (int r = 0; r < rows; ++r) rowT[(size_t) r] = r;
        rowT[(size_t) rows] = rows > 0 ? rowT[(size_t) rows - 1] + rowD[(size_t) rows - 1] : 0.0;
        for (int r = 0; r < rows; ++r) for (int c = 0; c < ch; ++c) rowE[(size_t) r] = juce::jmin(rowE[(size_t) r], (double) at(r, c).o * rowD[(size_t) r] - at(r, c).fl / 1000.0 * bpm / 15.0 * 1.6);   // (a flam: its grace hit is early)
    }
    int rowAtTime(double t) const { const int r = (int) (std::upper_bound(rowT.begin(), rowT.begin() + rows, t + 1e-9) - rowT.begin()) - 1; return juce::jlimit(0, juce::jmax(0, rows - 1), r); }
    const Cell& at(int r, int c) const { return cells[(size_t) r * (size_t) ch + (size_t) c]; }

    static std::unique_ptr<Pattern> fromState(const juce::var& st) {
        auto P = build(st, st["pats"], st["cur"]);
        // LAYERS: every other page switched on in "lay" plays along (its own patterns, kept in "pg")
        if (st.isObject()) { const juce::String page = st["page"].toString().isNotEmpty() ? st["page"].toString() : ((bool) st["pmode"] ? juce::String("piano") : juce::String("drums"));
            if ((int) st["layv"] == 2 && st["lay"].isObject() && ! (bool) st["lay"].getProperty(juce::Identifier(page), true)) P->selfMuted = true;
            for (const char* nm : { "drums", "piano", "synth" }) { if (page == nm || ! (bool) st["lay"][nm]) continue; auto o = st["pg"][nm]; if (! o.isObject()) continue;
                auto A = build(st, o["pats"], o["cur"]); if (A->rows > 0) P->along.push_back(std::shared_ptr<const Pattern>(A.release())); } }
        return P;
    }
    static std::unique_ptr<Pattern> build(const juce::var& st, const juce::var& patsV, const juce::var& curV) {
        auto P = std::make_unique<Pattern>(); P->defSnd.fill(-1);
        if (! st.isObject()) return P;
        P->bpm = juce::jlimit(60.0, 300.0, st.hasProperty("bpm") ? (double) st["bpm"] : 124.0);
        P->mul = mulOf(st["tmul"]);
        P->swing = juce::jlimit(50.0, 75.0, st.hasProperty("swing") ? (double) st["swing"] : 56.0);
        const juce::String bank = st["bank"].toString().isNotEmpty() ? st["bank"].toString() : juce::String("block");
        P->useKit = bank != "synth";
        if (auto k = st["key"]; k.isObject() && (bool) k["on"]) {
            static const std::pair<const char*, std::vector<int>> scales[] = { {"minor", {0,2,3,5,7,8,10}}, {"major", {0,2,4,5,7,9,11}}, {"dorian", {0,2,3,5,7,9,10}},
                {"phrygian", {0,1,3,5,7,8,10}}, {"harmonic", {0,2,3,5,7,8,11}}, {"minor pent", {0,3,5,7,10}}, {"major pent", {0,2,4,7,9}} };
            P->keyOn = true; P->keyRoot = juce::jlimit(0, 11, (int) k["root"]); P->keyRootMode = k["mode"].toString() == "root";
            const juce::String sc = k["scale"].toString(); const std::vector<int>* iv = &scales[0].second;
            for (auto& x : scales) if (sc == x.first) iv = &x.second;
            for (int pc : *iv) P->keyPcs[(size_t) pc] = true;
        }
        if (auto* m = st["mix"].getArray()) for (int i = 0; i < kInst && i < m->size(); ++i) if (! m->getReference(i).isVoid()) P->drumVol[(size_t) i] = (float) juce::jlimit(0.0, 1.5, (double) m->getReference(i));
        auto def = st["sndB"][juce::Identifier(bank == "synth" ? "block" : bank)];
        if (auto* a = def.getArray()) for (int i = 0; i < kInst && i < a->size(); ++i) P->defSnd[(size_t) i] = (int) a->getReference(i);
        if (auto sy = st["syn"]; sy.isObject()) for (int k = 0; k < gsyn::kSounds; ++k) if (auto o = sy[juce::Identifier(juce::String(k))]; o.isObject()) {
            auto& K = P->knobs[(size_t) k]; auto g = [&](const char* n, float& v) { if (o.hasProperty(n)) v = (float) juce::jlimit(0.0, 1.0, (double) o[n]); };
            g("cut", K.cut); g("res", K.res); g("env", K.env); g("dec", K.dec); g("acc", K.acc); g("drv", K.drv); }
        if (auto a = st["acid"]; a.isObject()) { P->acidBass = a["snd"].toString() == "bass"; P->acidB = juce::jlimit(0, ghosty::bassroots::kCount - 1, (int) a["b"]); }   // (none: the 303, as older projects had)
        if (auto fx = st["synfx"]; fx.isObject()) { if (fx.hasProperty("space")) P->synSpace = (float) juce::jlimit(0.0, 1.0, (double) fx["space"]); if (fx.hasProperty("echo")) P->synEcho = (float) juce::jlimit(0.0, 1.0, (double) fx["echo"]); }
        auto* pats = patsV.getArray(); if (pats == nullptr || pats->isEmpty()) return P;
        const int cur = juce::jlimit(0, pats->size() - 1, (int) curV);
        const auto& pt = pats->getReference(cur);
        auto* rows = pt["rows"].getArray(); if (rows == nullptr) return P;
        P->rows = juce::jmin(rows->size(), 512 * 16);
        int ch = (int) pt["ch"]; if (ch <= 0 && P->rows > 0) if (auto* r0 = rows->getReference(0).getArray()) ch = r0->size();
        P->ch = juce::jlimit(1, kMaxCh, ch);
        P->cells.assign((size_t) P->rows * (size_t) P->ch, Cell());
        for (int r = 0; r < P->rows; ++r) if (auto* row = rows->getReference(r).getArray())
            for (int c = 0; c < P->ch && c < row->size(); ++c) P->cells[(size_t) r * (size_t) P->ch + (size_t) c] = cellFromVar(row->getReference(c));
        P->buildTimes(pt);
        auto L = pt["loop"];
        if (L.isObject() && (bool) L["on"]) { P->loopOn = true; P->la = juce::jlimit(0, juce::jmax(0, P->rows - 1), (int) L["a"]); P->lb = juce::jlimit(P->la, juce::jmax(0, P->rows - 1), (int) L["b"]); }
        if (auto* cp = pt["cp"].getArray()) for (int c = 0; c < P->ch && c < cp->size(); ++c) if (! cp->getReference(c).isVoid()) P->chPan[(size_t) c] = (float) juce::jlimit(-1.0, 1.0, (double) cp->getReference(c) / 100.0);
        auto* cv = pt["cv"].getArray(); auto* cm = pt["cm"].getArray(); auto* cs = pt["cs"].getArray(); bool solo = false;
        if (cs != nullptr) for (auto& v : *cs) solo = solo || (! v.isVoid() && (bool) v);
        for (int c = 0; c < P->ch; ++c) {
            if (cv != nullptr && c < cv->size() && ! cv->getReference(c).isVoid()) P->chVol[(size_t) c] = (float) juce::jlimit(0.0, 1.5, (double) cv->getReference(c));
            const bool muted = cm != nullptr && c < cm->size() && ! cm->getReference(c).isVoid() && (bool) cm->getReference(c);
            const bool soloed = cs != nullptr && c < cs->size() && ! cs->getReference(c).isVoid() && (bool) cs->getReference(c);
            P->chOn[(size_t) c] = ! muted && (! solo || soloed);
        }
        return P;
    }
};

// ------------------------------------------------------------------------------------------------ samples
struct Sample { std::atomic<int> state { 0 }; juce::AudioBuffer<float> buf; double sr = 44100; juce::String name, resource; juce::File file; float rt = -1; int grp = 0; };   // file: a MY BANKS sound (Shared/UserBanks.h)   // grp: the piano set ("g" in the manifest)   // rt: the note it plays (MIDI), -1 = noise   // 0 idle, 1 queued, 2 ready, 3 failed

// The kit index (same order as the page's KITMAN) plus a background decoder. Shared by every Ghost House instance.
class SampleStore : private juce::Thread {
public:
    using Lookup = std::function<const void* (const juce::String& originalFilename, int& size)>;
    SampleStore() : juce::Thread("GhostHouse samples") { fm.registerBasicFormats(); }
    ~SampleStore() override { stopThread(4000); }

    // manifest: {"kick":[{"n":"kick 1","f":"kit/kick-01.mp3","b":"block"},...],...}
    void init(const juce::String& manifestJson, Lookup lookup) {
        if (ready.load()) return;
        find = std::move(lookup);
        auto m = juce::JSON::parse(manifestJson);
        for (int c = 0; c < kCats; ++c) {
            if (auto* a = m[kCatNames[c]].getArray())
                for (auto& it : *a) { auto s = std::make_unique<Sample>(); s->name = it["n"].toString(); s->resource = it["f"].toString().fromLastOccurrenceOf("/", false, false); s->rt = it.hasProperty("rt") ? (float) (double) it["rt"] : -1.0f; s->grp = it.hasProperty("g") ? (int) it["g"] : 0; cats[(size_t) c].push_back(std::move(s)); }
        }
        ready.store(true); startThread();
    }
    float rootOf(int cat, int idx) const { const Sample* x = slot(cat, idx); return x != nullptr ? x->rt : -1.0f; }
    int groupOf(int cat, int idx) const { const Sample* x = slot(cat, idx); return x != nullptr ? x->grp : -1; }
    bool hasGroup(int cat, int g) const { for (int k = 0; k < count(cat); ++k) if (groupOf(cat, k) == g) return true; return false; }
    // middle-out (nearest middle C first): a note that plays before its own root is in borrows the nearest decoded root, so the
    // middle of the keyboard, where most notes are, is covered within a few ms instead of waiting for the low octaves
    void requestGroup(int cat, int g) { std::vector<int> ks; for (int k = 0; k < count(cat); ++k) if (groupOf(cat, k) == g) ks.push_back(k);
        std::stable_sort(ks.begin(), ks.end(), [&](int a, int b) { return std::abs(rootOf(cat, a) - 60.0f) < std::abs(rootOf(cat, b) - 60.0f); }); for (int k : ks) request(cat, k); }
    int stateOf(int cat, int idx) const { const Sample* x = slot(cat, idx); return x != nullptr ? x->state.load() : 3; }
    int count(int cat) const { return cat >= 0 && cat < kCats ? (int) cats[(size_t) cat].size() : 0; }
    // audio-thread safe: nullptr unless decoded
    const Sample* get(int cat, int idx) const {
        const Sample* s = slot(cat, idx); if (s == nullptr) return nullptr;
        return s->state.load(std::memory_order_acquire) == 2 ? s : nullptr;
    }
    // message thread: queue a decode
    void request(int cat, int idx) {
        Sample* s = slot(cat, idx); if (s == nullptr) return; int expected = 0;
        if (! s->state.compare_exchange_strong(expected, 1)) return;
        { const juce::ScopedLock sl(qLock); queue.push_back(s); }
        notify();
    }
    // message thread (drag-out): this sound now, even if a lot of piano is still queued in front of it
    bool decodeUrgent(int cat, int idx, double maxMs = 8000) {
        Sample* s = slot(cat, idx); if (s == nullptr) return false;
        int e = 0; if (s->state.compare_exchange_strong(e, 4)) { decode(*s); return s->state.load() == 2; }   // not asked for yet: do it here
        e = 1; if (s->state.compare_exchange_strong(e, 4)) { decode(*s); return s->state.load() == 2; }       // queued: take it out of line
        const double t0 = juce::Time::getMillisecondCounterHiRes();                                            // the decoder has it right now
        while (s->state.load() == 4 && juce::Time::getMillisecondCounterHiRes() - t0 < maxMs) juce::Thread::sleep(2);
        return s->state.load() == 2;
    }
    juce::String nameOf(int cat, int idx) const { const Sample* x = slot(cat, idx); return x != nullptr ? x->name : juce::String(); }
    bool decodeNow(int cat, int idx) {   // tests: blocking decode
        Sample* s = slot(cat, idx); if (s == nullptr) return false;
        if (s->state.load() == 2) return true;
        decode(*s);
        return s->state.load() == 2;
    }
    // drag one sound out as a WAV (message thread): a MY BANKS sound = the user's own file, whole, at its own rate and channels;
    // a built-in sound = what plays (decoded, the MP3 delay cut), at its own rate
    bool exportSound(int cat, int idx, juce::AudioBuffer<float>& out, double& sr) {
        Sample* s = slot(cat, idx); if (s == nullptr) return false;
        if (s->file != juce::File()) {
            std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(s->file)); if (r == nullptr || r->lengthInSamples <= 0) return false;
            const int n = (int) juce::jmin<juce::int64>(r->lengthInSamples, (juce::int64) (r->sampleRate * 600.0)), ch = juce::jlimit(1, 2, (int) r->numChannels);
            out.setSize(ch, n); out.clear(); r->read(&out, 0, n, 0, true, ch > 1); sr = r->sampleRate; return true; }
        if (! decodeUrgent(cat, idx)) return false;
        out.makeCopyOf(s->buf); sr = s->sr; return out.getNumSamples() > 0;
    }
    // MY BANKS (message thread): the file behind user sound 'uid'. A new path gets a new slot; the old one stays alive (the audio
    // thread may still be playing it) until the plugin closes.
    void setUser(int uid, const juce::File& f, const juce::String& name) {
        if (uid < 0 || uid >= ghosty::userbanks::kMax) return;
        if (const Sample* o = user[(size_t) uid].load(std::memory_order_acquire); o != nullptr && o->file == f) return;
        auto s = std::make_unique<Sample>(); s->file = f; s->name = name; s->resource = f.getFileName();
        user[(size_t) uid].store(s.get(), std::memory_order_release); userOwned.push_back(std::move(s));
    }
    // every sound in the shared library (called at load and whenever library.json changes)
    void loadUserLibrary() {
        const auto lib = ghosty::userbanks::load(); const auto when = ghosty::userbanks::stamp(); userStamp = when;
        if (auto* S = lib["snd"].getDynamicObject()) for (auto& kv : S->getProperties()) {
            const int uid = kv.name.toString().getIntValue(); const auto f = kv.value["f"].toString();
            if (f.isNotEmpty()) setUser(uid, ghosty::userbanks::dir().getChildFile(f), kv.value["n"].toString()); }
    }
    bool userLibraryChanged() const { return ghosty::userbanks::libFile().existsAsFile() && ghosty::userbanks::stamp() != userStamp; }
private:
    Sample* slot(int cat, int idx) const {
        if (idx >= ghosty::userbanks::kBase) { const int u = idx - ghosty::userbanks::kBase; return u < ghosty::userbanks::kMax ? user[(size_t) u].load(std::memory_order_acquire) : nullptr; }
        if (cat < 0 || cat >= kCats || idx < 0 || idx >= (int) cats[(size_t) cat].size()) return nullptr;
        return cats[(size_t) cat][(size_t) idx].get();
    }
    void run() override {
        while (! threadShouldExit()) {
            Sample* s = nullptr;
            { const juce::ScopedLock sl(qLock); if (! queue.empty()) { s = queue.front(); queue.pop_front(); } }
            if (s == nullptr) { wait(250); continue; }
            int e = 1; if (! s->state.compare_exchange_strong(e, 4)) continue;   // a drag-out already took it
            decode(*s);
        }
    }
    void decode(Sample& s) {
        std::unique_ptr<juce::AudioFormatReader> r;
        if (s.file != juce::File()) r.reset(fm.createReaderFor(s.file));   // MY BANKS: the user's own file
        else {
            int size = 0; const void* data = find ? find(s.resource, size) : nullptr;
            if (data == nullptr || size <= 0) { s.state.store(3); return; }
            // a built-in .mp3: always JUCE's own MP3 decoder, the one the 1105-sample cut below was measured against. (On a Mac
            // the basic formats would pick Apple's decoder first, which already drops that delay: the cut then took the first
            // ~25 ms of every hit away; the Mac engine tests caught it 2026-11-07: kick 7's attack at sample 0, the WAV's at 249.)
            auto in = std::make_unique<juce::MemoryInputStream>(data, (size_t) size, false);
            if (s.resource.endsWithIgnoreCase(".mp3")) r.reset(juce::MP3AudioFormat().createReaderFor(in.release(), true));
            else r.reset(fm.createReaderFor(std::move(in)));
        }
        if (r == nullptr || r->lengthInSamples <= 0) { s.state.store(3); return; }
        const int n = (int) juce::jmin<juce::int64>(r->lengthInSamples, (juce::int64) (r->sampleRate * 20.0));
        juce::AudioBuffer<float> b(2, n); b.clear();
        r->read(&b, 0, n, 0, true, r->numChannels > 1);
        if (r->numChannels == 1) b.copyFrom(1, 0, b, 0, 0, n);
        // JUCE's MP3 decoder keeps the encoder + decoder delay (LAME 576 + 529 = 1105 samples at 44.1 kHz; measured against
        // the original WAVs: every hit came out exactly 1105 samples late). Cut exactly that, so each sound keeps its own attack.
        const int lead = s.resource.endsWithIgnoreCase(".mp3") ? (int) std::lround(1105.0 * r->sampleRate / 44100.0) : 0;
        if (lead > 0 && lead < n - 64) { juce::AudioBuffer<float> t(2, n - lead); t.copyFrom(0, 0, b, 0, lead, n - lead); t.copyFrom(1, 0, b, 1, lead, n - lead); b = std::move(t); }
        if (s.file != juce::File()) {   // MY BANKS: start on the hit (up to 250 ms of silence before it is cut)
            const int nn = b.getNumSamples(), lim = juce::jmin(nn, (int) (r->sampleRate * 0.25)); int idx = 0;
            for (; idx < lim; ++idx) if (std::abs(b.getSample(0, idx)) > 0.003f || std::abs(b.getSample(1, idx)) > 0.003f) break;
            if (idx >= 8 && idx < lim) { juce::AudioBuffer<float> t(2, nn - idx); t.copyFrom(0, 0, b, 0, idx, nn - idx); t.copyFrom(1, 0, b, 1, idx, nn - idx); b = std::move(t); }
        }
        if (s.grp > 0) {   // piano bank: the web trimLead, so the plugin starts each note on the same sample the page does
            const int nn = b.getNumSamples(), lim = juce::jmin(nn, (int) (r->sampleRate * 0.08)); int idx = 0;
            for (; idx < lim; ++idx) if (std::abs(b.getSample(0, idx)) > 0.004f || std::abs(b.getSample(1, idx)) > 0.004f) break;
            if (idx >= 8 && idx < lim) { juce::AudioBuffer<float> t(2, nn - idx); t.copyFrom(0, 0, b, 0, idx, nn - idx); t.copyFrom(1, 0, b, 1, idx, nn - idx); b = std::move(t); }
        }
        {   // the end: drop the decoder's silent tail, then a 3 ms fade (some files stop mid-wave; long ones are cut at 20 s) -> no click
            int nn = b.getNumSamples(); while (nn > 64 && std::abs(b.getSample(0, nn - 1)) < 1.0e-5f && std::abs(b.getSample(1, nn - 1)) < 1.0e-5f) --nn;
            if (nn < b.getNumSamples()) b.setSize(2, nn, true, false, true);
            const int fl = juce::jmin(nn / 4, (int) (r->sampleRate * 0.003));
            for (int i = 0; i < fl; ++i) { const float g = (float) i / (float) fl; for (int c = 0; c < 2; ++c) b.setSample(c, nn - 1 - i, b.getSample(c, nn - 1 - i) * g); }
        }
        s.buf = std::move(b); s.sr = r->sampleRate; s.state.store(2, std::memory_order_release);
    }
    juce::AudioFormatManager fm; Lookup find; std::atomic<bool> ready { false };
    std::array<std::vector<std::unique_ptr<Sample>>, kCats> cats;
    std::array<std::atomic<Sample*>, ghosty::userbanks::kMax> user {}; std::vector<std::unique_ptr<Sample>> userOwned; juce::int64 userStamp = 0;
    juce::CriticalSection qLock; std::deque<Sample*> queue;
};

// ------------------------------------------------------------------------------------------------ built-in synth drums
// Rendered once at 44.1 kHz into buffers (ports of the page's Web Audio voices), then played like samples.
struct SynthKit {
    std::array<juce::AudioBuffer<float>, SY_COUNT> buf; const double sr = 44100;
    SynthKit() { for (int k = 0; k < SY_COUNT; ++k) render(k); }
private:
    static float rnd(uint32_t& x) { x ^= x << 13; x ^= x >> 17; x ^= x << 5; return (float) ((double) x / 4294967295.0 * 2.0 - 1.0); }
    void render(int k) {
        const double len = k == SY_PIANO ? 3.0 : k == SY_SUB ? 1.4 : k == SY_CRASH || k == SY_FX ? 1.4 : k == SY_OHAT ? 0.38 : k == SY_KICK ? 0.5 : 0.35;
        const int n = (int) (len * sr); auto& b = buf[(size_t) k]; b.setSize(2, n); b.clear();
        float* o = b.getWritePointer(0); uint32_t seed = 0x9E3779B9u + (uint32_t) k * 7919u;
        double ph = 0, hp = 0, hpx = 0, lp = 0, bp1 = 0, bp2 = 0;
        for (int i = 0; i < n; ++i) {
            const double t = i / sr; double v = 0; const double nz = rnd(seed);
            auto hpf = [&](double x, double a) { hp = a * (hp + x - hpx); hpx = x; return hp; };
            switch (k) {
                case SY_KICK: case SY_TOM: case SY_SUB: {
                    const bool sub = k == SY_SUB, kick = k == SY_KICK;
                    const double f0 = sub ? 60 : kick ? 160 : 220, f1 = sub ? 52 : kick ? 44 : 120, tc = kick ? 0.11 : 0.15;
                    const double f = f1 + (f0 - f1) * std::exp(-t / (tc / 4.0)); ph += 2 * juce::MathConstants<double>::pi * f / sr;
                    const double dec = sub ? 1.2 : kick ? 0.42 : 0.28;
                    v = std::sin(ph) * std::exp(-t * 6.9 / dec) * (kick ? 1.0 : 0.8);
                    if (kick && t < 0.02) v += hpf(nz, 0.9) * 0.35 * std::exp(-t * 300);
                    break; }
                case SY_SNARE: {
                    const double f = 140 + 60 * std::exp(-t / 0.02); ph += 2 * juce::MathConstants<double>::pi * f / sr;
                    const double tri = 2.0 / juce::MathConstants<double>::pi * std::asin(std::sin(ph));
                    v = tri * 0.6 * std::exp(-t * 69) + hpf(nz, 0.85) * 0.75 * std::exp(-t * 34); break; }
                case SY_CLAP: {
                    lp += 0.35 * (nz - lp); bp1 = hpf(lp, 0.8);
                    double e = 0; for (double d : { 0.0, 0.011, 0.023 }) if (t >= d) e = juce::jmax(e, 0.9 * std::exp(-(t - d) * 230));
                    if (t > 0.03) e = juce::jmax(e, 0.7 * std::exp(-(t - 0.03) * 31));
                    v = bp1 * e * 1.4; break; }
                case SY_RIM: {
                    ph += 2 * juce::MathConstants<double>::pi * 1650 / sr; v = (std::sin(ph) > 0 ? 1 : -1) * 0.45 * std::exp(-t * 200);
                    bp2 += 0.3 * (v - bp2); v = v - bp2; break; }
                case SY_PIANO: {   // built-in piano (synth bank, or before the real piano decodes): middle C, a few decaying partials
                    const double f = 261.63, w = 2 * juce::MathConstants<double>::pi * f * t;
                    const double tri = 2.0 / juce::MathConstants<double>::pi * std::asin(std::sin(w));
                    v = tri * 0.55 * std::exp(-t / 2.6) + std::sin(2 * w) * 0.22 * std::exp(-t / 1.6) + std::sin(3 * w) * 0.1 * std::exp(-t / 1.0) + std::sin(4.02 * w) * 0.05 * std::exp(-t / 0.8);
                    v *= juce::jmin(1.0, t / 0.003); break; }
                default: {   // hats / crash / fx
                    const double dec = k == SY_HAT ? 0.045 : k == SY_OHAT ? 0.32 : 1.3;
                    const double a = k == SY_HAT || k == SY_OHAT ? 0.97 : 0.93;
                    v = hpf(nz, a) * (k == SY_CRASH || k == SY_FX ? 0.55 : 0.5) * std::exp(-t * 6.9 / dec);
                    if (k == SY_FX) v *= std::sin(juce::MathConstants<double>::pi * juce::jmin(1.0, t / 0.25));
                }
            }
            o[i] = (float) v;
        }
        // fade the tail and normalise to roughly the page's level
        float pk = b.getMagnitude(0, 0, n); if (pk > 0) b.applyGain(0, 0, n, 0.9f / pk);
        const int tail = juce::jmin(n, 600); b.applyGainRamp(0, n - tail, tail, 1.0f, 0.0f);
        b.copyFrom(1, 0, b, 0, 0, n);
    }
};

// the note each built-in synth drum plays (page: SYN_RT): kick 44 Hz, perc (tom) 120 Hz, rim 1650 Hz, sub 52 Hz
inline double synthRoot(int inst) { auto m = [](double hz) { return 69.0 + 12.0 * std::log2(hz / 440.0); };
    return inst == 0 ? m(44) : inst == 5 ? m(120) : inst == 6 ? m(1650) : inst == 8 ? m(52) : inst == kPianoInst ? 60.0 : -1.0; }
// semitones to add so a hit (its own note rt + its pitch p) lands on the nearest note of the key; ROOT mode: kick + sub on root / 5th
// which piano sound a note plays: its own s, else the page's default piano sound
inline int pianoSetOf(const Cell& c, const Pattern* P) { return c.s >= 0 ? c.s : (P != nullptr ? juce::jmax(0, P->defSnd[(size_t) kPianoInst]) : 0); }
inline double keyShift(const Pattern* P, int inst, double rt, int p) {
    if (P == nullptr || ! P->keyOn || rt < 0) return 0;
    const double want = rt + p; const bool kickish = P->keyRootMode && (inst == 0 || inst == 8);
    double best = 0; bool found = false;
    for (int n = (int) std::floor(want) - 7; n <= (int) std::ceil(want) + 7; ++n) {
        const int pc = ((n - P->keyRoot) % 12 + 12) % 12;
        if (! (kickish ? (pc == 0 || pc == 7) : P->keyPcs[(size_t) pc])) continue;
        if (! found || std::abs(n - want) < std::abs(best - want) - 1e-9) { best = n; found = true; }
    }
    return found ? best - want : 0;
}

// GLITCH rolls: where hit k of a RETRIG roll lands (u 0..1 of the step; ACCEL > 0 speeds up, < 0 slows down) and how loud it is
inline double rollPos(double u, int ax) { if (ax == 0) return u; const double e = 1.0 + std::abs(ax) / 100.0 * 2.0; return ax > 0 ? 1.0 - std::pow(1.0 - u, e) : std::pow(u, e); }
inline double rollVol(int k, int n, int vr) { if (vr == 0) return k == 0 ? 1.0 : juce::jmax(0.2, 0.8 - k * 0.06); const double f = (double) k / juce::jmax(1, n), a = std::abs(vr) / 100.0; return vr > 0 ? (1.0 - a * 0.8) + a * 0.8 * f : 1.0 - a * 0.85 * f; }
// ------------------------------------------------------------------------------------------------ master stage
// The page's level and its soft curve, then a stereo peak limiter and a soft ceiling: stacked piano chords / drums never go
// over 0 dBFS, so FL never hard-clips them into crackle. Below -1.6 dB nothing changes.
// The limiter's gain is SMOOTHED (two 1.5 ms boxcar averages of a fast-attack / 80 ms-release control): the old 0.4 ms attack
// stepped the gain down 20-35 % within a millisecond on every loud drum hit, and that step was printed onto whatever piano note
// was ringing as a click. Same threshold, same curve, same ceiling, same release: only the gain's edges are rounded off, so
// steady loudness is unchanged and a drum transient still meets the ceiling the way it did.
struct Master {
    static constexpr int kMaxBox = 512;
    void prepare(double rate) { sr = rate; aA = 1.0 - std::exp(-1.0 / (0.0004 * sr)); aR = 1.0 - std::exp(-1.0 / (0.08 * sr)); box = juce::jlimit(1, kMaxBox, (int) std::lround(sr * 0.0015)); reset(); }
    void reset() { limG = 1; h1.fill(1.0); h2.fill(1.0); s1 = s2 = box; ix = 0; }
    static float curve(float x) { x *= 0.85f; return x / (1.0f + std::abs(x) * 0.25f) * 1.08f; }
    static float ceil1(float y) { const float a = std::abs(y); if (a <= 0.86f) return y; const float z = 0.86f + 0.12f * std::tanh((a - 0.86f) / 0.12f); return y < 0 ? -z : z; }
    // in place; gainOut (tests) receives the gain applied to each sample
    void process(float* L0, float* R0, int n, float* gainOut = nullptr) {
        const float thr = 0.83f;
        for (int i = 0; i < n; ++i) { const float l = curve(L0[i]), r = R0 ? curve(R0[i]) : l; const float pk = juce::jmax(std::abs(l), std::abs(r));
            const double want = pk > thr ? thr / pk : 1.0; limG += (want - limG) * (want < limG ? aA : aR);
            s1 += limG - h1[(size_t) ix]; h1[(size_t) ix] = limG; const double g1 = s1 / box;
            s2 += g1 - h2[(size_t) ix]; h2[(size_t) ix] = g1; const double g = s2 / box; if (++ix >= box) ix = 0;
            if (gainOut) gainOut[i] = (float) g;
            L0[i] = ceil1((float) (l * g)); if (R0) R0[i] = ceil1((float) (r * g)); }
    }
    double sr = 44100, aA = 0.05, aR = 0.0003, limG = 1, s1 = 1, s2 = 1; int box = 1, ix = 0; std::array<double, kMaxBox> h1 {}, h2 {};
};

// ------------------------------------------------------------------------------------------------ the engine
class Engine {
public:
    explicit Engine(SampleStore& s, const SynthKit& k) : store(s), synth(k) { voices.fill({}); chanLast.fill(-1); }

    void prepare(double rate) { sr = rate; now = 0; for (auto& v : voices) v.on = false; for (auto& v : tails) v.on = false; master.prepare(rate); chanLast.fill(-1); bus.prepare(rate, 4096); monoR.assign(4096, 0.0f); bassV.prepare(rate); }

    // message thread: new pattern (the old one is kept alive briefly, then freed)
    void setPattern(std::unique_ptr<Pattern> p) {
        if (p == nullptr) return;
        requestSamples(*p);
        ensureBass(); if (bassReady.load()) { auto want = [&](const Pattern& q) { if (q.acidBass) store.request(kCatBass, bassIx[(size_t) q.acidB]); }; want(*p); for (auto& a : p->along) if (a) want(*a); }
        { bool n3 = false; auto uses = [&](const Pattern& q) { for (auto& c : q.cells) if (c.i == kSynthInst && c.s <= gsyn::S_ACIDSQ) return true; return false; };
          n3 = uses(*p); for (auto& a : p->along) if (a && uses(*a)) n3 = true; if (n3) bus.need303(); }
        Pattern* old = current.exchange(p.release(), std::memory_order_acq_rel);
        if (old != nullptr) retired.push_back({ juce::Time::getMillisecondCounter(), std::unique_ptr<Pattern>(old) });
        freeRetired();
    }
    void freeRetired() {   // message thread (timer): anything swapped out over a second ago is surely not in use
        const auto t = juce::Time::getMillisecondCounter();
        while (! retired.empty() && t - retired.front().first > 1500) retired.pop_front();
    }
    // message thread: make sure every sound the pattern (and its defaults) uses gets decoded
    void requestSamples(const Pattern& p) {
        for (auto& a : p.along) if (a) requestSamples(*a);
        if (! p.useKit) return;
        for (int i = 0; i < kInst; ++i) if (kInstCat[i] >= 0 && i != kPianoInst) store.request(kInstCat[i], p.defSnd[(size_t) i]);
        { const int g = juce::jmax(0, p.defSnd[(size_t) kPianoInst]); store.requestGroup(kCatPiano, store.hasGroup(kCatPiano, g) ? g : 0); }   // the piano sound picked on the page
        std::array<bool, 64> sets {}; for (auto& c : p.cells) if (c.on()) { if (c.i == kPianoInst) sets[(size_t) juce::jlimit(0, 63, pianoSetOf(c, &p))] = true; else if (c.i != kSynthInst) store.request(kInstCat[c.i], c.s >= 0 ? c.s : p.defSnd[(size_t) c.i]); }
        for (int g = 0; g < 64; ++g) if (sets[(size_t) g]) store.requestGroup(kCatPiano, store.hasGroup(kCatPiano, g) ? g : 0);   // only the piano sounds this pattern uses
    }
    void audition(const Cell& c) {   // message thread -> audio thread
        if (! c.on()) return;
        if (c.i == kSynthInst) { ensureBass(); const Pattern* p = current.load(); const int b = c.ab >= 0 ? c.ab : (p != nullptr && p->acidBass ? p->acidB : -1);
            if (b >= 0 && bassReady.load()) store.decodeUrgent(kCatBass, bassIx[(size_t) b], 1500); }   // the sample bass: decoded now, so it plays
        if (c.i == kSynthInst && c.s <= gsyn::S_ACIDSQ) bus.need303();
        if (c.i == kPianoInst) {   // the sound you clicked: its nearest real note now, the rest behind it
            const Pattern* p = current.load(); int g = pianoSetOf(c, p); if (! store.hasGroup(kCatPiano, g)) g = 0;
            const double want = 60.0 + c.p + keyShift(p, c.i, 60.0, c.p); int best = -1; double bd = 1e9;
            for (int k = 0; k < store.count(kCatPiano); ++k) if (store.groupOf(kCatPiano, k) == g) { const double d = std::abs(store.rootOf(kCatPiano, k) - want); if (d < bd) { bd = d; best = k; } }
            if (best >= 0) store.decodeUrgent(kCatPiano, best, 1500); store.requestGroup(kCatPiano, g);
        }
        if (const Pattern* p = current.load(); p != nullptr && p->useKit && c.i != kSynthInst && c.i != kPianoInst) store.request(kInstCat[c.i], c.s >= 0 ? c.s : p->defSnd[(size_t) c.i]);
        int s1, n1, s2, n2; fifo.prepareToWrite(1, s1, n1, s2, n2);
        if (n1 > 0) pending[(size_t) s1] = c; else if (n2 > 0) pending[(size_t) s2] = c;
        fifo.finishedWrite(n1 + n2);
    }
    // audio thread (MIDI in): play a hit right now, offset samples into this block
    void playNow(const Cell& c, int offset) { const Pattern* P = current.load(std::memory_order_acquire); trigger(c, P, now + offset, stepSamples(effBpm(P)), -1); }
    void setInternalPlay(bool on) { wantPlay.store(on ? 1 : 0); }
    void seek(int step) { seekTo.store(step); }

    struct Host { bool playing = false, ppqValid = false, bpmValid = false; double ppq = 0, bpm = 120; };   // bpmValid: the DAW told its tempo (even stopped)
    struct Pos { std::atomic<bool> playing { false }, host { false }; std::atomic<int> step { -1 }; std::atomic<float> frac { 0 }, bpm { 150 }; } pos;

    void process(juce::AudioBuffer<float>& out, int numSamples, const Host& h) {
        const Pattern* P = current.load(std::memory_order_acquire);
        // auditions
        { int s1, n1, s2, n2; fifo.prepareToRead(fifo.getNumReady(), s1, n1, s2, n2);
          for (int k = 0; k < n1; ++k) trigger(pending[(size_t) (s1 + k)], P, now, stepSamples(effBpm(P)), -1);
          for (int k = 0; k < n2; ++k) trigger(pending[(size_t) (s2 + k)], P, now, stepSamples(effBpm(P)), -1);
          fifo.finishedRead(n1 + n2); }
        if (h.bpmValid && h.bpm > 0) dawBpm = juce::jlimit(20.0, 999.0, h.bpm);   // the DAW's tempo: the beat's base tempo, even with the DAW stopped
        const int wp = wantPlay.exchange(-1);
        if (wp == 1 && ! internal) { internal = true; ipos = 0; }
        if (wp == 0) { internal = false; }
        if (const int sk = seekTo.exchange(-1); sk >= 0 && P != nullptr && P->rows > 0 && internal) { const int r = juce::jmin(sk, P->rows - 1); ipos = P->rowT[(size_t) r] - (P->loopOn ? P->rowT[(size_t) P->la] : 0.0); }

        if (P != nullptr && P->rows > 0 && h.playing && h.ppqValid) {
            // TEMPO ½x | 1x | 2x: the steps run at the DAW's tempo x mul, counted from the DAW's bar 1 (2x = 16 steps in half a bar,
            // ½x = 16 steps over two bars), so the beat stays locked to the DAW's bars
            const double bpm = juce::jlimit(20.0, 999.0, h.bpm) * P->mul, sps = bpm / 60.0 * 4.0 / sr; double s0 = h.ppq * 4.0 * P->mul;
            // hosts don't all report the position sample-exactly block to block (FL's ppq can lag or lead by a tick): a window that
            // started a hair before the last one ended would fire the rows in the overlap twice (a doubled piano note = a click), a
            // hair after would skip them (Logic has been seen a whole buffer ahead for one block). So the windows stay contiguous unless the
            // host really moved (a seek / loop: a 16th step or more).
            double s1 = s0 + numSamples * sps;
            if (wasHost && std::abs(s0 - hostS1) < 1.0) { const double lim = 0.002 * numSamples * sps; s1 = hostS1 + numSamples * sps + juce::jlimit(-lim, lim, s1 - (hostS1 + numSamples * sps)); s0 = hostS1; }   // (and creep towards the host's clock, 0.2 % a block at most, so a slow drift never becomes a jump)
            fireAll(*P, s0, s1, sps, stepSamples(bpm));
            report(*P, s1, true, bpm / P->mul, true);
            wasHost = true; hostS1 = s1;
        } else if (P != nullptr && P->rows > 0 && internal) {
            if (wasHost) { wasHost = false; }
            const double bpm = effBpm(P), sps = bpm / 60.0 * 4.0 / sr, s0 = ipos, s1 = s0 + numSamples * sps;   // (the DAW stopped: its tempo x mul, the same as when it plays)
            fireAll(*P, s0, s1, sps, stepSamples(bpm));
            ipos = s1; report(*P, s1, true, bpm / P->mul, false);
        } else {
            if (wasHost && ! h.playing) wasHost = false;
            pos.playing.store(false); pos.host.store(false); pos.step.store(-1);
            if (P != nullptr) pos.bpm.store((float) baseBpm(P));
        }
        if (P != nullptr) { const Pattern* S = P; for (auto& a : P->along) if (a) for (auto& x : a->cells) if (x.i == kSynthInst) { S = a.get(); break; }
            bus.setFx(S->synSpace, S->synEcho, h.playing && h.ppqValid ? h.bpm * P->mul : effBpm(P)); }
        render(out, numSamples);
        now += numSamples;
    }

    // Offline render for the plugin's ⇣ WAV: the whole beat (or its loop), or one channel (onlyCh). It plays twice and keeps the
    // second pass, so anything still ringing at the end wraps into the start and the WAV loops seamlessly in the DAW.
    // bpm: the DAW's tempo (the base); the pattern's TEMPO multiple (mul) is applied, so the WAV is exactly what was heard
    static juce::AudioBuffer<float> bounce(SampleStore& store, const SynthKit& synth, const Pattern& src, double sr, double bpm, int onlyCh, juce::uint32 chMask = 0) {
        if (src.rows <= 0 || sr <= 0) return {};
        auto P = std::make_unique<Pattern>(src); P->bpm = juce::jlimit(20.0, 999.0, bpm);
        if (onlyCh >= 0) for (int c = 0; c < kMaxCh; ++c) P->chOn[(size_t) c] = c == onlyCh;
        if (onlyCh == -3) for (int c = 0; c < kMaxCh; ++c) P->chOn[(size_t) c] = c < 32 && ((chMask >> c) & 1u);   // several channels (like one channel: what they hold, muted or not)
        if (onlyCh != -1) { P->along.clear(); P->selfMuted = false; }   // CHn / PAGE (-2): this page only; ALL (-1): every layer playing
        if (P->acidBass) { const int ix = findBass(store, P->acidB); if (ix >= 0) store.decodeUrgent(kCatBass, ix); }   // ACID BASS SAMPLES: in the WAV too
        if (P->useKit) {   // every sound it uses, decoded first (the background decoder does it; wait up to 5 s)
            std::vector<std::pair<int, int>> need; std::vector<const Pattern*> all { P.get() }; for (auto& a : P->along) if (a) all.push_back(a.get());
            for (const Pattern* Q : all) for (int r = 0; r < Q->rows; ++r) for (int c = 0; c < Q->ch; ++c) { const Cell& x = Q->at(r, c); if (! x.on() || ! Q->chOn[(size_t) c] || x.i == kSynthInst) continue;
                if (x.i == kPianoInst) { int g = pianoSetOf(x, Q); if (! store.hasGroup(kCatPiano, g)) g = 0; for (int k = 0; k < store.count(kCatPiano); ++k) if (store.groupOf(kCatPiano, k) == g) { need.emplace_back(kCatPiano, k); } continue; }
                const int idx = x.s >= 0 ? x.s : x.s == -2 ? -1 : Q->defSnd[(size_t) x.i]; if (idx >= 0) need.emplace_back(kInstCat[x.i], idx); }
            std::sort(need.begin(), need.end()); need.erase(std::unique(need.begin(), need.end()), need.end());
            for (auto& n : need) store.decodeUrgent(n.first, n.second);
        }
        const double C = P->loopOn ? P->rowT[(size_t) P->lb + 1] - P->rowT[(size_t) P->la] : P->rowT[(size_t) P->rows];
        const int len = (int) std::llround(C * 60.0 / (P->bpm * P->mul) / 4.0 * sr); if (len <= 0 || len > (int) (sr * 60 * 30)) return {};   // (TEMPO: as heard)
        auto eng = std::make_unique<Engine>(store, synth); eng->prepare(sr); eng->setPattern(std::move(P)); eng->setInternalPlay(true);
        juce::AudioBuffer<float> blk(2, 512), out(2, len); out.clear(); Host h;
        for (int done = 0; done < 2 * len; ) { const int n = juce::jmin(512, 2 * len - done); blk.clear(); eng->process(blk, n, h);
            for (int i = 0; i < n; ++i) { const int at = done + i - len; if (at >= 0) for (int c = 0; c < 2; ++c) out.setSample(c, at, blk.getSample(c, i)); }
            done += n; }
        return out;
    }

    // tests
    int activeVoices() const { int n = 0; for (auto& v : voices) n += v.on ? 1 : 0; return n; }
    int synthVoices() const { return bus.activeVoices(); }
    bool bassActive() const { return bassV.active(); }
    // the 17 basses in the sound data: "bass 1" .. "bass 17" of the bass category (BANK 1 SUB; Ghost House: BLOCK STEP BANK 1 SUB)
    static constexpr int kCatBass = 6;
    static int findBass(const SampleStore& st, int b) { const juce::String nm = "bass " + juce::String(b + 1);
        if (st.nameOf(kCatBass, 8 + b) == nm) return 8 + b; for (int k = 0; k < st.count(kCatBass); ++k) if (st.nameOf(kCatBass, k) == nm) return k; return -1; }
    void ensureBass() {   // message thread
        if (bassReady.load() || store.count(kCatBass) == 0) return; bool all = true;
        for (int b = 0; b < ghosty::bassroots::kCount; ++b) { const int k = findBass(store, b); bassIx[(size_t) b] = juce::jmax(0, k); all = all && k >= 0; }
        if (all) bassReady.store(true, std::memory_order_release); }

private:
    struct Voice {
        bool on = false; const float* l = nullptr; const float* r = nullptr; int len = 0;
        double pos = 0, rate = 1; float gain = 1;
        juce::int64 t0 = 0, tStop = std::numeric_limits<juce::int64>::max(); int fadeLen = 264, inLen = 0;   // inLen: a short fade-in (a piano note that starts on a non-zero sample)
        double psSemis = 0; juce::int64 psLen = 0; double vibSemis = 0, vibHz = 0; juce::int64 swLen = 0; uint32_t serial = 0;
        float panL = 1, panR = 1; double lpA = 0, lpL = 0, lpR = 0; bool rev = false, fHP = false; double fA = 0, f1L = 0, f1R = 0, f2L = 0, f2R = 0;   // GLITCH: reverse, per-hit filter   // ECHO repeats: PING pans, TAPE darkens
    };
    struct Echo { double pan = 0, lp = 0, cents = 0, cut = 0; };   // per-repeat settings for the echo / stutter FX
    double stepSamples(double bpm) const { return 60.0 / juce::jmax(20.0, bpm) / 4.0 * sr; }
    double dawBpm = 0;   // audio thread: the DAW's last tempo (0 = no DAW tempo yet: the page's own)
    double baseBpm(const Pattern* P) const { return dawBpm > 0 ? dawBpm : (P ? P->bpm : 150.0); }
    double effBpm(const Pattern* P) const { return baseBpm(P) * (P ? P->mul : 1.0); }
    static int mapStep(const Pattern& P, juce::int64 k) {
        if (P.loopOn) { const juce::int64 n = P.lb - P.la + 1; return P.la + (int) (((k % n) + n) % n); }
        const juce::int64 n = P.rows; return (int) (((k % n) + n) % n);
    }
    void report(const Pattern& P, double s1, bool playing, double bpm, bool host) {
        const double base = P.loopOn ? P.rowT[(size_t) P.la] : 0.0, C = P.loopOn ? P.rowT[(size_t) P.lb + 1] - base : P.rowT[(size_t) P.rows], at = s1 - 1e-6;
        const double pt = C > 0 ? base + (at - std::floor(at / C) * C) : 0.0; const int r = P.rowAtTime(pt);
        pos.step.store(r); pos.frac.store((float) juce::jlimit(0.0, 1.0, (pt - P.rowT[(size_t) r]) / P.rowD[(size_t) r])); pos.playing.store(playing); pos.host.store(host); pos.bpm.store((float) bpm);
    }
    // every row whose time (minus any early notes) falls in [s0, s1), positions in 16th steps; the loop / pattern repeats every C steps
    void fireWindow(const Pattern& P, double s0, double s1, double sps, double sd) {
        const double base = P.loopOn ? P.rowT[(size_t) P.la] : 0.0, C = P.loopOn ? P.rowT[(size_t) P.lb + 1] - base : P.rowT[(size_t) P.rows]; if (C <= 0) return;
        const int r0 = P.loopOn ? P.la : 0, r1 = P.loopOn ? P.lb + 1 : P.rows;
        for (double m = std::floor((s0 - 2.0) / C); m * C - base <= s1 + 2.0; m += 1.0) {
            const double off = m * C - base, lo = s0 - off - 1e-9, hi = s1 - off;
            if (hi + 2.0 < P.rowT[(size_t) r0] || lo - 2.0 > P.rowT[(size_t) r1]) continue;
            for (int r = (int) (std::lower_bound(P.rowT.begin() + r0, P.rowT.begin() + r1, lo) - P.rowT.begin()); r < r1 && P.rowT[(size_t) r] < hi + 2.0; ++r) {
                const double ft = P.rowT[(size_t) r] + P.rowE[(size_t) r]; if (ft < lo || ft >= hi) continue;
                fireRow(P, r, now + (juce::int64) ((P.rowT[(size_t) r] + off - s0) / sps), sps, sd);
            }
        }
    }
    void fireRow(const Pattern& P, int r, juce::int64 t, double sps, double sd) {
        const double d = P.rowD[(size_t) r], rsd = sd * d; curStepSd = sd;
        const double sw = d == 1.0 && (std::llround(P.rowT[(size_t) r]) & 1) ? P.swingDelay(1, sd) : 0.0;   // swing: only on 16th rows, every second one leans late
        for (int c = 0; c < P.ch; ++c) { const Cell& x = P.at(r, c); if (! x.on() || x.m || ! P.chOn[(size_t) c]) continue; if (x.pc < 100 && (int) (nextRand() % 100u) >= x.pc) continue; /* CHANCE */ const juce::int64 tt = t + (juce::int64) sw + (juce::int64) (x.o * d / sps);
            if (x.i == kSynthInst) synthNote(P, r, c, x, tt, sd, rsd); else trigger(x, &P, tt, rsd, c); }
        curStepSd = 0;
    }
    void fireStep(const Pattern& P, int step, juce::int64 t, double sd) {
        if (step < 0 || step >= P.rows) return;
        const juce::int64 ts = t + (juce::int64) P.swingDelay(step, sd);
        for (int c = 0; c < P.ch; ++c) { const Cell& x = P.at(step, c); if (x.on() && ! x.m && P.chOn[(size_t) c]) trigger(x, &P, ts, sd, c); }
    }
    // every page playing: this one, then the layers along with it (their own channels for the choke)
    void fireAll(const Pattern& P, double s0, double s1, double sps, double sd) {
        if (! P.selfMuted) fireWindow(P, s0, s1, sps, sd);
        int k = 0; for (auto& a : P.along) { if (! a || a->rows <= 0) continue; chBase = kMaxCh * juce::jmin(2, ++k); fireWindow(*a, s0, s1, sps, sd); chBase = 0; }
    }
    // a SYNTH note: its gate is its length (a 303 note: half a step), a slide holds it to the next note so the synth glides there
    void synthNote(const Pattern& P, int r, int c, const Cell& x, juce::int64 t, double sd, double rsd) {
        const int snd = juce::jlimit(0, gsyn::kSounds - 1, x.s >= 0 ? (int) x.s : juce::jmax(0, P.defSnd[(size_t) kSynthInst]));
        const int base = gsyn::baseNote(snd), midi = base + x.p + (int) std::lround(keyShift(&P, kSynthInst, base, x.p));
        juce::int64 off = t + (juce::int64) ((x.l > 0 ? x.l : (gsyn::isMono(snd) ? 0.55 : 0.9)) * rsd);
        if (x.sl) { const double dt = P.nextSynthSteps(r, snd); if (dt > 0) off = t + (juce::int64) (dt * sd) + (juce::int64) (sr * 0.004); }
        const int lane = gsyn::isMono(snd) ? gsyn::Bus::kLanes - 4 + snd : c;   // mono sounds: one voice per sound, whatever channel the note is on
        if (x.fx == FX_CUT) off = juce::jmin(off, t + (juce::int64) (rsd * juce::jmax(1, (int) x.pr) / 4.0));
        const double vol = std::pow(x.v / 100.0, 1.4); const float mixG = (float) (P.drumVol[(size_t) kSynthInst] * P.chVol[(size_t) c]);   // synthvol: MIX is a gain, not velocity
        const float pan = (float) juce::jlimit(-1.0, 1.0, x.pn / 100.0 + P.chPan[(size_t) c]);
        if (P.acidBass) {   // BASS SAMPLES: the line on the chosen bass (two octaves down for the SYNTH page's high sounds)
            const Sample* smp = bassReady.load() ? store.get(kCatBass, bassIx[(size_t) P.acidB]) : nullptr; if (smp == nullptr) return;
            auto nt = [&](juce::int64 a, juce::int64 b, double v, bool acc) { ghosty::samplebass::Note n; n.t = a; n.off = b; n.midi = (float) (midi + (base >= 48 ? -24 : 0)); n.vel = (float) juce::jmin(1.0, v);
                n.gain = mixG; n.pan = pan; n.accent = acc; n.buf = &smp->buf; n.srcRate = smp->sr; n.root = ghosty::bassroots::kRoot[(size_t) P.acidB]; bassV.note(n); };
            if (x.fx == FX_RETRIG && x.pr > 0) { const int n = juce::jmin((int) x.pr, 8); const double sp = rsd / (n + 1); for (int k = 0; k <= n; ++k) nt(t + (juce::int64) (k * sp), t + (juce::int64) (k * sp + sp * 0.8), vol * (k ? 0.75 : 1.0), x.ac && k == 0); }
            else nt(t, off, vol, x.ac);
            return; }
        if (x.fx == FX_RETRIG && x.pr > 0) { const int n = juce::jmin((int) x.pr, 8); const double sp = rsd / (n + 1);
            for (int k = 0; k <= n; ++k) bus.note(t + (juce::int64) (k * sp), t + (juce::int64) (k * sp + sp * 0.8), lane, snd, midi, (float) juce::jmin(1.0, vol * (k ? 0.75 : 1.0)), x.ac && k == 0, false, pan, P.knobs[(size_t) snd], mixG);
            return; }
        bus.note(t, off, lane, snd, midi, (float) juce::jmin(1.0, vol), x.ac, true, pan, P.knobs[(size_t) snd], mixG);
    }
    void trigger(const Cell& c, const Pattern* P, juce::int64 t, double sd, int ch) {
        if (c.i == kSynthInst) {   // an audition / MIDI key: play it for its length (two steps if it has none)
            if (const int b = c.ab >= 0 ? c.ab : (P != nullptr && P->acidBass ? P->acidB : -1); b >= 0) {   // BASS SAMPLES
                const Sample* smp = bassReady.load() ? store.get(kCatBass, bassIx[(size_t) b]) : nullptr; if (smp == nullptr) return;
                const int snd = juce::jlimit(0, gsyn::kSounds - 1, c.s >= 0 ? (int) c.s : (P ? juce::jmax(0, P->defSnd[(size_t) kSynthInst]) : 0)), base = gsyn::baseNote(snd);
                ghosty::samplebass::Note n; n.t = t; n.off = t + (juce::int64) (sd * (c.l > 0 ? c.l : 2.0)); n.midi = (float) (base + c.p + (base >= 48 ? -24 : 0)); n.vel = (float) std::pow(c.v / 100.0, 1.4);
                n.accent = c.ac; n.buf = &smp->buf; n.srcRate = smp->sr; n.root = ghosty::bassroots::kRoot[(size_t) b]; bassV.note(n); return; }
            const int snd = juce::jlimit(0, gsyn::kSounds - 1, c.s >= 0 ? (int) c.s : (P ? juce::jmax(0, P->defSnd[(size_t) kSynthInst]) : 0));
            const gsyn::Knobs k = P ? P->knobs[(size_t) snd] : gsyn::defaultKnobs(snd);
            bus.note(t, t + (juce::int64) (sd * (c.l > 0 ? c.l : 2.0)), ch >= 0 && ch < gsyn::Bus::kLanes ? ch : gsyn::Bus::kLanes - 1, snd, gsyn::baseNote(snd) + c.p, (float) std::pow(c.v / 100.0, 1.4), c.ac, false, 0.0f, k);
            return;
        }
        const double vol = std::pow(c.v / 100.0, 1.4) * (P != nullptr ? P->drumVol[(size_t) c.i] * (ch >= 0 && ch < kMaxCh ? P->chVol[(size_t) ch] : 1.0f) : 1.0f); const int pr = c.pr;
        const double base = juce::jlimit(-1.0, 1.0, c.pn / 100.0 + (P != nullptr && ch >= 0 && ch < kMaxCh ? P->chPan[(size_t) ch] : 0.0f));   // note pan + channel pan
        auto go = [&](double dt, double vm, int po, bool choke, double semis = 0.0) { Echo eb; eb.pan = base; const int ip = (int) std::floor(semis); eb.cents = (semis - ip) * 100.0; start(c, P, t + (juce::int64) dt, sd, vol * vm, c.p + po + ip, choke ? ch : -1, eb); };
        if (c.fl > 0) { Echo ef; ef.pan = base; start(c, P, t - (juce::int64) (c.fl / 1000.0 * sr), sd, vol * 0.5, c.p, -1, ef); }   // FLAM: the grace hit
        const bool roll = c.fx == FX_RETRIG && pr > 0; const int rn = roll ? juce::jmin(pr, 8) : 0;
        go(0, roll ? rollVol(0, rn, c.vr) : 1.0, 0, true);
        if (roll) { for (int k = 1; k <= rn; ++k) go(rollPos((double) k / (rn + 1), c.ax) * sd, rollVol(k, rn, c.vr), 0, true, c.pg * (double) k / rn); }   // ACCEL / VEL RAMP / PITCH RAMP
        else if (c.fx == FX_ARP && pr > 0) { go(sd / 3, 0.7, pr, false); go(sd * 2 / 3, 0.6, -pr, false); }
        else if (c.fx == FX_ECHO || c.fx == FX_PING || c.fx == FX_TAPE) {
            static constexpr double T[9] = { 8, 6, 4, 3, 2, 1.5, 1, 0.75, 0.5 };   // 1/2 .. 1/32 (same as the page)
            const double ssd = curStepSd > 0 ? curStepSd : sd, dt = T[juce::jmin(pr, 8)] * ssd, fb = 0.9 - juce::jmin((int) c.q, 16) * 0.045; double g = 1;
            for (int k = 1; k <= 32; ++k) { g *= fb; if (g < 0.03) break; Echo ex; ex.pan = base;
                if (c.fx == FX_PING) ex.pan = juce::jlimit(-1.0, 1.0, base + (k % 2 ? 0.75 : -0.75));
                if (c.fx == FX_TAPE) { ex.lp = juce::jmax(450.0, 7000.0 * std::pow(0.7, k)); ex.cents = -6.0 * k + ((k * 37) % 7 - 3); }
                start(c, P, t + (juce::int64) (k * dt), sd, vol * g, c.p, -1, ex); }
        }
        else if (c.fx == FX_STUT) {
            static constexpr double R[9] = { 1, 1.5, 2, 3, 4, 6, 8, 12, 16 };   // hits per 16th: 1/16 .. 1/256
            const double ssd = curStepSd > 0 ? curStepSd : sd, sp = ssd / R[juce::jmin(pr, 8)], gm = 0.97 - juce::jmin((int) c.q, 16) * 0.03; double g = 1;
            for (int k = 1; k <= 128 && k * sp < ssd * 32; ++k) { g *= gm; if (g < 0.04) break; Echo ex; ex.cut = sp * 0.92; ex.pan = base; start(c, P, t + (juce::int64) (k * sp), sd, vol * g, c.p, ch, ex); }
        }
        else if (c.fx == FX_DELAY && pr > 0) { const int n = juce::jmin((pr + 3) / 4, 4); for (int k = 1; k <= n; ++k) go(sd * 0.75 * k, std::pow(0.5, k), 0, false); }
    }
    void start(const Cell& c, const Pattern* P, juce::int64 t, double sd, double vol, int pitch, int ch) { start(c, P, t, sd, vol, pitch, ch, Echo()); }
    void start(const Cell& c, const Pattern* P, juce::int64 t, double sd, double vol, int pitch, int ch, Echo ex) {
        const float* L = nullptr; const float* R = nullptr; int len = 0; double bsr = 44100, rt = -1, semis = -1e9;
        const bool piano = c.i == kPianoInst;
        if (piano) {   // every key from the nearest of the 22 real piano notes (like the page)
            const double want = 60.0 + pitch + keyShift(P, c.i, 60.0, c.p); const Sample* best = nullptr;
            int set = pianoSetOf(c, P); if (! store.hasGroup(kCatPiano, set)) set = 0;   // a sound no longer in the bank: GRAND
            if (P == nullptr || P->useKit) for (int k = 0; k < store.count(kCatPiano); ++k) if (const Sample* s = store.get(kCatPiano, k)) if (s->grp == set) if (best == nullptr || std::abs(s->rt - want) < std::abs(best->rt - want)) best = s;
            if (best != nullptr) { L = best->buf.getReadPointer(0); R = best->buf.getReadPointer(1); len = best->buf.getNumSamples(); bsr = best->sr; semis = want - best->rt; }
            else semis = want - 60.0;   // the built-in piano is middle C
        } else if (P == nullptr || P->useKit) {
            const int idx = c.s >= 0 ? c.s : c.s == -2 ? -1 : (P ? P->defSnd[(size_t) c.i] : -1);   // s -2: this note plays the built-in synth
            if (const Sample* s = store.get(kInstCat[c.i], idx)) { L = s->buf.getReadPointer(0); R = s->buf.getReadPointer(1); len = s->buf.getNumSamples(); bsr = s->sr; rt = s->rt; }
        }
        if (L == nullptr) { const auto& b = synth.buf[(size_t) kInstSynth[c.i]]; L = b.getReadPointer(0); R = b.getReadPointer(1); len = b.getNumSamples(); bsr = synth.sr; rt = synthRoot(c.i); }
        // choke: the channel's previous sound fades out when this one starts
        if (ch >= 0 && ch < kMaxCh) { const size_t cx = (size_t) (ch + chBase); const int pv = chanLast[cx]; if (pv >= 0 && voices[(size_t) pv].on && voices[(size_t) pv].serial == chanSerial[cx]) { fadeOut(voices[(size_t) pv], t, juce::jmax(16, (int) (sr * (piano ? 0.05 : 0.006)))); } }
        int vi = -1; juce::int64 oldest = std::numeric_limits<juce::int64>::max(); int oi = 0;
        for (int i = 0; i < kMaxVoices; ++i) { if (! voices[(size_t) i].on) { vi = i; break; } if (voices[(size_t) i].t0 < oldest) { oldest = voices[(size_t) i].t0; oi = i; } }
        if (vi < 0) { vi = oi;   // every voice busy: the oldest gives up its slot, but fades out (12 ms, like a CUT) in a tail slot instead of stopping dead
            Voice& tl = tails[(size_t) (tailNext++ % kTails)]; tl = voices[(size_t) vi]; fadeOut(tl, t, juce::jmax(16, (int) (sr * 0.012))); }
        Voice& v = voices[(size_t) vi]; v = Voice();
        v.on = true; v.l = L; v.r = R; v.len = len; v.pos = 0; v.rate = bsr / sr * std::exp2((semis > -1e8 ? semis : pitch + keyShift(P, c.i, rt, c.p)) / 12.0 + (ex.cents + c.ct) / 1200.0); v.gain = (float) vol; v.t0 = t;
        v.fadeLen = juce::jmax(16, (int) (sr * 0.012)); v.serial = ++serialCounter;
        if (c.st > 0) v.pos = len * (c.st / 100.0); v.rev = c.rv;   // START % / REVERSE
        if (piano) {   // the piano: a note starting on a non-zero sample (START %, or a trimmed sample that begins a hair off zero) eases in over 1 ms (3 ms mid-wave);
                       // the hammer itself peaks 20-200 ms in, untouched. Drums keep their very first sample as it is.
            const int p0 = juce::jlimit(0, juce::jmax(0, len - 1), (int) (v.rev ? (double) (len - 2) - v.pos : v.pos));
            if (std::abs(L[p0]) > 1.0e-4f || std::abs(R[p0]) > 1.0e-4f) v.inLen = juce::jmax(16, (int) (sr * (c.st > 0 ? 0.003 : 0.001))); }
        if (c.ft != 0) { const double fc = c.ft < 0 ? 18000.0 * std::pow(150.0 / 18000.0, -c.ft / 100.0) : 30.0 * std::pow(5000.0 / 30.0, c.ft / 100.0); v.fA = 1.0 - std::exp(-2.0 * juce::MathConstants<double>::pi * juce::jmin(fc, sr * 0.45) / sr); v.fHP = c.ft > 0; }
        if (ex.pan != 0) { v.panL = (float) (ex.pan > 0 ? 1.0 - ex.pan : 1.0); v.panR = (float) (ex.pan < 0 ? 1.0 + ex.pan : 1.0); }
        if (ex.lp > 0) v.lpA = 1.0 - std::exp(-2.0 * juce::MathConstants<double>::pi * ex.lp / sr);
        if (ex.cut > 0) { v.tStop = t + (juce::int64) ex.cut; v.fadeLen = juce::jmax(16, (int) (sr * 0.003)); }
        switch (c.fx) {
            case FX_CUT:     v.tStop = t + (juce::int64) (sd * juce::jmax(1, (int) c.pr) / 4.0); break;
            case FX_NOTEOFF: v.tStop = t + (juce::int64) (sd * 0.5); v.fadeLen = juce::jmax(16, (int) (sr * (piano ? 0.08 : 0.004))); break;   // (the page: 80 ms for the piano, 4 ms for drums)
            case FX_PSLIDE:  v.psSemis = -(c.pr > 0 ? c.pr : 4); v.psLen = (juce::int64) (sd * 2); break;
            case FX_VIB:     { const int d = c.pr > 0 ? c.pr : 4; v.vibSemis = d * 0.3; v.vibHz = 6 + d; break; }
            case FX_VSLIDE:  v.swLen = (juce::int64) (sd * juce::jmax(0.25, c.pr / 4.0)); break;
            default: break;
        }
        // a long note (FL-style length) stops at its end, like the page; CUT / NOTE OFF keep their own stop
        if (c.l > 0 && c.fx != FX_CUT && c.fx != FX_NOTEOFF) { v.tStop = juce::jmin(v.tStop, t + (juce::int64) (sd * c.l)); v.fadeLen = juce::jmax(16, (int) (sr * (piano ? 0.08 : 0.006))); }
        if (ch >= 0 && ch < kMaxCh) { chanLast[(size_t) (ch + chBase)] = vi; chanSerial[(size_t) (ch + chBase)] = v.serial; }
    }
    // fade a sounding voice out over len samples from time at, carrying on from its current level if it is already fading
    static void fadeOut(Voice& o, juce::int64 at, int len) {
        if (at < o.tStop) { o.tStop = at; o.fadeLen = len; return; }
        const double k0 = (double) (at - o.tStop) / juce::jmax(1, o.fadeLen); if (k0 >= 1.0 || (1.0 - k0) * o.fadeLen <= len) return;   // gone by then, or already fading faster
        o.fadeLen = juce::jmax(1, (int) std::lround(len / (1.0 - k0))); o.tStop = at - (juce::int64) std::llround(k0 * o.fadeLen);
    }
    void render(juce::AudioBuffer<float>& out, int n) {
        const int nc = out.getNumChannels(); if (nc <= 0) return;
        float* oL = out.getWritePointer(0); float* oR = nc > 1 ? out.getWritePointer(1) : nullptr;
        const double tau = 2.0 * juce::MathConstants<double>::pi;
        for (int vx = 0; vx < kMaxVoices + kTails; ++vx) { Voice& v = vx < kMaxVoices ? voices[(size_t) vx] : tails[(size_t) (vx - kMaxVoices)];
            if (! v.on) continue;
            const bool mod = v.psLen > 0 || v.vibSemis > 0;
            for (int i = 0; i < n; ++i) {
                const juce::int64 t = now + i; if (t < v.t0) continue;
                const juce::int64 age = t - v.t0;
                double g = v.gain;
                if (v.swLen > 0 && age < v.swLen) g *= 0.04 + 0.96 * (double) age / (double) v.swLen;
                if (age < v.inLen) g *= (double) age / (double) v.inLen;
                if (t >= v.tStop) { const double k = (double) (t - v.tStop) / v.fadeLen; if (k >= 1.0) { v.on = false; break; } g *= 1.0 - k; }
                const double sp = v.rev ? (double) (v.len - 2) - v.pos : v.pos; if (sp < 0.0) { v.on = false; break; }
                const int idx = (int) sp; if (idx + 1 >= v.len) { v.on = false; break; }
                const float fr = (float) (sp - idx);
                float a = v.l[idx] + (v.l[idx + 1] - v.l[idx]) * fr, b = v.r[idx] + (v.r[idx + 1] - v.r[idx]) * fr;
                if (v.lpA > 0) { v.lpL += v.lpA * (a - v.lpL); v.lpR += v.lpA * (b - v.lpR); a = (float) v.lpL; b = (float) v.lpR; }
                if (v.fA > 0) { v.f1L += v.fA * (a - v.f1L); v.f2L += v.fA * (v.f1L - v.f2L); v.f1R += v.fA * (b - v.f1R); v.f2R += v.fA * (v.f1R - v.f2R);
                                if (v.fHP) { a -= (float) v.f2L; b -= (float) v.f2R; } else { a = (float) v.f2L; b = (float) v.f2R; } }   // FILTER (12 dB)
                oL[i] += (float) (a * g * v.panL); if (oR) oR[i] += (float) (b * g * v.panR);
                double r = v.rate;
                if (mod) { double semi = 0; if (v.psLen > 0) semi += v.psSemis * juce::jmin(1.0, (double) age / (double) v.psLen);
                           if (v.vibSemis > 0) semi += v.vibSemis * std::sin(tau * v.vibHz * (double) age / sr);
                           r *= std::exp2(semi / 12.0); }
                v.pos += r;
            }
        }
        // the SYNTH page's voices + its SPACE / ECHO
        bassV.render(oL, oR, n, now);   // ACID BASS SAMPLES (dry: the SYNTH page's SPACE / ECHO stay on the synths)
        if (oR) bus.render(oL, oR, n, now); else { for (int a = 0; a < n; a += 4096) { const int m = juce::jmin(4096, n - a); std::fill(monoR.begin(), monoR.begin() + m, 0.0f); bus.render(oL + a, monoR.data(), m, now + a); } }
        master.process(out.getWritePointer(0), nc > 1 ? out.getWritePointer(1) : nullptr, n);
    }

    uint32_t rng = 0x2545F491u; uint32_t nextRand() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
    SampleStore& store; const SynthKit& synth;
    gsyn::Bus bus; std::vector<float> monoR; Master master;
    ghosty::samplebass::Voice bassV; std::array<int, ghosty::bassroots::kCount> bassIx {}; std::atomic<bool> bassReady { false };   // ACID BASS SAMPLES
    double curStepSd = 0;   // a real 16th while a row fires (echo times don't stretch with the row's grid)
    double sr = 44100; juce::int64 now = 0;
    std::atomic<Pattern*> current { nullptr }; std::deque<std::pair<juce::uint32, std::unique_ptr<Pattern>>> retired;
    static constexpr int kTails = 32; std::array<Voice, kTails> tails; int tailNext = 0;   // voices taken for a new note, fading out
    std::array<Voice, kMaxVoices> voices; std::array<int, kMaxCh * 3> chanLast; std::array<uint32_t, kMaxCh * 3> chanSerial {}; int chBase = 0; uint32_t serialCounter = 0;
    juce::AbstractFifo fifo { 64 }; std::array<Cell, 64> pending {};
    std::atomic<int> wantPlay { -1 }, seekTo { -1 }; bool internal = false, wasHost = false; double ipos = 0, hostS1 = 0;
public:
    ~Engine() { delete current.exchange(nullptr); }
};

// ---------------------------------------------------------------- ⇣ MIDI
// The ⇣ MIDI clip (owner 2026-11-07: "everything needs to get it"): what plays, as GM drum notes (+ the piano on channel 1,
// the synth on 2), at the DAW's tempo dawBpm. TEMPO ½x | 1x | 2x scales every note time, so the clip played at the DAW's
// tempo is exactly what was heard: 2x = the 16 steps in half a bar, ½x = over two bars. Swing is baked in.
// onlyCh: -1 ALL (every layer playing), -2 this page, -3 the channels in mask, else that one channel.
inline juce::MidiFile midiClip(const Pattern& PP, int onlyCh, juce::uint32 dragMask, double dawBpm) {
    juce::MidiFile mf; if (PP.rows <= 0) return mf;
    const Pattern* P = &PP;
    static constexpr int kNote[kInst] = { 36, 38, 39, 42, 46, 45, 37, 49, 35, 56, 60, 48 };   // GM drums: kick snare clap hat ohat tom rim crash, sub = 35, fx = 56
    const int ppq = 96; const double tps = 24.0 / P->mul;   // a step: a 16th = 24 ticks at 1x, a 32nd at 2x, an 8th at ½x
    const double base = dawBpm > 0 ? dawBpm : P->bpm, tpsec = ppq * base / 60.0;   // ticks per second at the DAW's tempo (FLAM: in ms)
    juce::MidiMessageSequence seq;
    seq.addEvent(juce::MidiMessage::tempoMetaEvent((int) std::lround(60000000.0 / base)), 0);
    seq.addEvent(juce::MidiMessage::timeSignatureMetaEvent(4, 4), 0);
    int mch = 10;   // drums on channel 10, the piano on channel 1
    auto note = [&](int n, int vel, double t, double len) {
        seq.addEvent(juce::MidiMessage::noteOn(mch, n, (juce::uint8) juce::jlimit(1, 127, vel)), t);
        seq.addEvent(juce::MidiMessage::noteOff(mch, n), t + len);
    };
    std::vector<const Pattern*> pages; if (onlyCh != -1 || ! P->selfMuted) pages.push_back(P); if (onlyCh == -1) for (auto& a : P->along) if (a) pages.push_back(a.get());   // ALL: every layer playing
    for (const Pattern* Q : pages) for (int r = 0; r < Q->rows; ++r) for (int c = 0; c < Q->ch; ++c) {
        const auto& x = Q->at(r, c); if (! x.on() || x.m || (onlyCh >= 0 ? (Q != P || c != onlyCh) : onlyCh == -3 ? (Q != P || c >= 32 || ! ((dragMask >> c) & 1u)) : ! Q->chOn[(size_t) c])) continue;
        const int n = juce::jlimit(0, 127, kNote[x.i] + (x.i == 8 ? x.p : x.i == kPianoInst ? x.p + (int) std::lround(keyShift(Q, x.i, 60.0, x.p)) : 0)), vel = (int) std::lround(x.v * 1.27 * Q->drumVol[(size_t) x.i] * Q->chVol[(size_t) c]);
        const double rd = Q->rowD[(size_t) r], tq = tps * rd, t = Q->rowT[(size_t) r] * tps + x.o * tq + (rd == 1.0 && (std::llround(Q->rowT[(size_t) r]) & 1) ? Q->swingDelay(1, tps) : 0.0);   // swing baked into the MIDI, same feel as the plugin
        mch = x.i == kPianoInst ? 1 : x.i == kSynthInst ? 2 : 10;
        if (x.i == kSynthInst) { const int snd = juce::jlimit(0, gsyn::kSounds - 1, x.s >= 0 ? (int) x.s : 0), base = gsyn::baseNote(snd);
            const int sn = juce::jlimit(0, 127, base + x.p + (int) std::lround(keyShift(Q, x.i, base, x.p)));
            double ln = x.l > 0 ? x.l * tq : tq * (gsyn::isMono(snd) ? 0.55 : 0.9); if (x.sl) { const double dt = Q->nextSynthSteps(r, snd); if (dt > 0) ln = dt * tps + 2; }
            note(sn, x.ac ? 127 : juce::jmin(110, vel), t, ln); continue; }
        if (x.fl > 0) note(n, juce::jmax(1, vel / 2), juce::jmax(0.0, t - x.fl / 1000.0 * tpsec), juce::jmax(2.0, x.fl / 1000.0 * tpsec * 0.8));   // FLAM
        const double ln0 = x.l > 0 ? x.l * tq : tq * 0.9;
        { const double pn = juce::jlimit(-1.0, 1.0, x.pn / 100.0 + Q->chPan[(size_t) c]);   // pan: CC10 just before the note
          seq.addEvent(juce::MidiMessage::controllerEvent(mch, 10, juce::jlimit(0, 127, (int) std::lround(64 + pn * 63.5))), juce::jmax(0.0, t - 0.5)); }
        if (x.fx == FX_ECHO || x.fx == FX_PING || x.fx == FX_TAPE) { static constexpr double T[9] = { 8, 6, 4, 3, 2, 1.5, 1, 0.75, 0.5 }; const double dt = T[juce::jmin((int) x.pr, 8)] * tps, fb = 0.9 - juce::jmin((int) x.q, 16) * 0.045; double g = 1;
            note(n, vel, t, ln0); for (int k = 1; k <= 32; ++k) { g *= fb; if (g < 0.03) break; note(n, (int) std::lround(vel * g), t + k * dt, juce::jmin(ln0, dt * 0.95)); } }
        else if (x.fx == FX_STUT) { static constexpr double R[9] = { 1, 1.5, 2, 3, 4, 6, 8, 12, 16 }; const double sp = tps / R[juce::jmin((int) x.pr, 8)], gm = 0.97 - juce::jmin((int) x.q, 16) * 0.03; double g = 1;
            note(n, vel, t, sp * 0.9); for (int k = 1; k <= 128 && k * sp < tps * 32; ++k) { g *= gm; if (g < 0.04) break; note(n, (int) std::lround(vel * g), t + k * sp, sp * 0.9); } }
        else if (x.fx == FX_RETRIG && x.pr > 0) { const int k = juce::jmin((int) x.pr, 8); const double sp = tq / (k + 1); note(n, (int) std::lround(vel * rollVol(0, k, x.vr)), t, sp * 0.8);
            for (int j = 1; j <= k; ++j) note(n, (int) std::lround(vel * rollVol(j, k, x.vr)), t + rollPos((double) j / (k + 1), x.ax) * tq, sp * 0.8); }   // ACCEL / VEL RAMP
        else if (x.fx == FX_CUT || x.fx == FX_NOTEOFF) note(n, vel, t, tq * 0.5);
        else note(n, vel, t, x.l > 0 ? x.l * tq : tq * 0.9);   // long notes keep their length in the clip
    }
    seq.updateMatchedPairs(); seq.sort();
    mf.setTicksPerQuarterNote(ppq); mf.addTrack(seq);
    return mf;
}

} // namespace ghouse
