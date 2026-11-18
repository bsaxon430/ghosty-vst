// Ghost House engine tests (console; local harness, not part of CI). Same as Block Step's, retargeted at the Ghost House kit + swing.
// Decodes real kit mp3s from data/sounds, plays patterns on the internal clock and a fake host playhead, and checks
// timing, levels, every FX, the channel choke and speed.
#include <juce_core/juce_core.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "../GhostHouseEngine.h"
#include <cstdio>

using namespace ghouse;
static int fails = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("  ok   %s\n", juce::String(msg).toRawUTF8()); else { std::printf("  FAIL %s\n", juce::String(msg).toRawUTF8()); ++fails; } } while (0)

static juce::File dataDir;
static std::vector<juce::MemoryBlock> keep;
static const void* lookup(const juce::String& f, int& size) {
    juce::File file = f == "allman.json" ? dataDir.getChildFile(f) : dataDir.getChildFile("sounds").getChildFile(f);
    juce::MemoryBlock mb; if (! file.loadFileAsData(mb)) { size = 0; return nullptr; }
    keep.push_back(std::move(mb)); size = (int) keep.back().getSize(); return keep.back().getData();
}
static juce::String cellJ(int i, int v, const char* fx = "", int pr = 4, int p = 0, int s = -1) {
    return "{\"i\":" + juce::String(i) + ",\"v\":" + juce::String(v) + ",\"fx\":\"" + fx + "\",\"pr\":" + juce::String(pr) + ",\"p\":" + juce::String(p) + (s >= 0 ? ",\"s\":" + juce::String(s) : juce::String()) + "}";
}
// rows: vector of rows, each row a vector of 5 cell json strings ("" = empty)
static juce::String stateJ(const std::vector<std::vector<juce::String>>& rows, double bpm, const char* bank = "block", const char* loop = "null", int swing = 50) {
    juce::String r = "[";
    for (size_t i = 0; i < rows.size(); ++i) { r += (i ? "," : ""); r += "["; for (size_t c = 0; c < rows[i].size(); ++c) { r += (c ? "," : ""); r += rows[i][c].isEmpty() ? "null" : rows[i][c]; } r += "]"; }
    r += "]";
    const juce::String pat = "{\"bars\":" + juce::String((int) rows.size() / 16) + ",\"ch\":5,\"rows\":" + r + ",\"loop\":" + loop + "}";
    const juce::String empty = "{\"bars\":1,\"ch\":5,\"rows\":[],\"loop\":null}";
    return "{\"pats\":[" + pat + "," + empty + "," + empty + "," + empty + "],\"cur\":0,\"bpm\":" + juce::String(bpm) + ",\"swing\":" + juce::String(swing) +
           ",\"bank\":\"" + bank + "\",\"sndB\":{\"block\":[6,3,0,1,0,0,7,-1,0,0],\"lib\":[30,30,5,17,6,42,49,0,25,12],\"all\":[6,3,0,1,0,0,7,0,0,0]}}";
}
static std::vector<std::vector<juce::String>> blank(int rows) { return std::vector<std::vector<juce::String>>((size_t) rows, std::vector<juce::String>(5)); }

struct Run { std::vector<float> L; };
static Run render(Engine& e, double sr, double seconds, bool hostPlaying = false, double ppq0 = 0, double hostBpm = 120, bool bpmKnown = false) {
    Run out; const int bs = 512; juce::AudioBuffer<float> b(2, bs); double ppq = ppq0;
    for (int done = 0; done < (int) (sr * seconds); done += bs) {
        b.clear(); Engine::Host h; h.playing = hostPlaying; h.ppqValid = hostPlaying; h.ppq = ppq; h.bpm = hostBpm; h.bpmValid = bpmKnown;
        e.process(b, bs, h); for (int i = 0; i < bs; ++i) out.L.push_back(b.getSample(0, i));
        ppq += bs / sr * hostBpm / 60.0;
    }
    return out;
}
static int firstAbove(const std::vector<float>& x, float th, int from = 0) { for (int i = from; i < (int) x.size(); ++i) if (std::abs(x[(size_t) i]) > th) return i; return -1; }
static double rms(const std::vector<float>& x, int a, int b) { double s = 0; b = juce::jmin(b, (int) x.size()); for (int i = a; i < b; ++i) s += x[(size_t) i] * x[(size_t) i]; return std::sqrt(s / juce::jmax(1, b - a)); }

int main(int argc, char** argv) {
    dataDir = juce::File(argc > 1 ? argv[1] : "../data");
    SampleStore store; SynthKit synth;
    int msz = 0; const char* man = (const char*) lookup("allman.json", msz);
    store.init(juce::String::fromUTF8(man, msz), lookup);
    std::printf("kit: kick %d snare %d hat %d perc %d bass %d crash %d\n", store.count(0), store.count(1), store.count(3), store.count(5), store.count(6), store.count(8));
    CHECK(store.count(0) >= 101 && store.count(1) >= 49 && store.count(5) >= 54 && store.count(7) >= 98, "manifest matches the page (its own 101 kicks, 49 snares, 54 percs, 98 fx first, then Block Step banks)");
    const juce::int64 t0 = juce::Time::getMillisecondCounterHiRes();
    bool dec = store.decodeNow(0, 6) && store.decodeNow(1, 3) && store.decodeNow(3, 1) && store.decodeNow(5, 0) && store.decodeNow(5, 7) && store.decodeNow(8, 0) && store.decodeNow(7, 0) && store.decodeNow(0, 77);   // 0/77 = first house-pack kick
    std::printf("  decode 8 mp3s: %.0f ms\n", juce::Time::getMillisecondCounterHiRes() - t0);
    CHECK(dec, "GhostBlock kit + house pack mp3s decode");
    const Sample* kick = store.get(0, 6); CHECK(kick && kick->buf.getNumSamples() > 1000, "kick 7 has audio");
    // the original GhostBlock Kick 7.wav crosses 0.1 at sample 31 (ffmpeg). Its attack starts on sample 0, so mp3 pre-echo pulls the
    // crossing ~16 samples (0.4 ms) earlier; a wrong delay trim would be off by hundreds (576 / 1105), so +-24 still proves the trim.
    if (kick) { int at = firstAbove(std::vector<float>(kick->buf.getReadPointer(0), kick->buf.getReadPointer(0) + juce::jmin(4000, kick->buf.getNumSamples())), 0.1f); std::printf("  kick 7 attack at %d (wav: 31)\n", at); CHECK(std::abs(at - 31) <= 24, "mp3 delay removed: kick 7 attack lines up with the original WAV"); }

    const double sr = 48000, bpm = 150, sd = 60.0 / bpm / 4.0 * sr;
    // 1. starter-like beat on the internal clock
    {
        auto rows = blank(32);
        for (int s : {0, 8, 16, 24}) rows[(size_t) s][0] = cellJ(0, 100);
        for (int s : {4, 12, 20, 28}) rows[(size_t) s][1] = cellJ(1, 90);
        Engine e(store, synth); e.prepare(sr); e.setPattern(Pattern::fromState(juce::JSON::parse(stateJ(rows, bpm)))); e.setInternalPlay(true);
        auto r = render(e, sr, 1.0);
        const int on0 = firstAbove(r.L, 0.02f);
        std::printf("  kick crosses 0.02 at %d samples (wav: 0)\n", on0);
        CHECK(on0 >= 0 && on0 <= 12, "PLAY: kick lands on step 0, sample-accurate");
        const int snareAt = (int) std::lround(4 * sd);
        CHECK(rms(r.L, snareAt, snareAt + 2000) > 3 * rms(r.L, snareAt - 2200, snareAt - 200), "snare lands on step 4 (backbeat), sample-accurate");
        CHECK(e.pos.playing.load() && ! e.pos.host.load(), "position reports internal play");
        double pk = 0; for (float x : r.L) pk = juce::jmax(pk, (double) std::abs(x)); std::printf("  peak %.2f\n", pk);
        CHECK(pk > 0.2 && pk < 1.5, "level sane, no runaway clipping");
    }
    // 2. host sync: DAW playing from beat 2 (ppq 1.0) at 174 bpm -> step 4 fires at sample 0, position follows the host
    {
        auto rows = blank(16); rows[4][1] = cellJ(1, 100); rows[0][0] = cellJ(0, 100);
        Engine e(store, synth); e.prepare(sr); e.setPattern(Pattern::fromState(juce::JSON::parse(stateJ(rows, 150))));
        auto r = render(e, sr, 0.05, true, 1.0, 174);
        // reference: the same snare played at sample 0 with no transport; the synced render must start at the same sample
        Engine ref(store, synth); ref.prepare(sr); ref.setPattern(Pattern::fromState(juce::JSON::parse(stateJ(rows, 150))));
        Cell sn; sn.i = 1; sn.v = 100; ref.playNow(sn, 0); auto rr = render(ref, sr, 0.05);
        const int on = firstAbove(r.L, 0.02f), onRef = firstAbove(rr.L, 0.02f); std::printf("  synced snare at %d, reference at %d\n", on, onRef);
        CHECK(on >= 0 && std::abs(on - onRef) <= 1, "DAW sync: snare on step 4 plays exactly at ppq 1.0");
        CHECK(e.pos.host.load() && std::abs(e.pos.bpm.load() - 174.0f) < 0.01f, "follows the DAW's tempo (174)");
        CHECK(e.pos.step.load() == 4 || e.pos.step.load() == 5, "reported step matches the host position");
    }
    // 3. loop: steps 8-11 looped
    {
        auto rows = blank(16); rows[0][0] = cellJ(0, 100); rows[8][1] = cellJ(1, 100);
        Engine e(store, synth); e.prepare(sr); e.setPattern(Pattern::fromState(juce::JSON::parse(stateJ(rows, bpm, "block", "{\"on\":true,\"a\":8,\"b\":11}")))); e.setInternalPlay(true);
        auto r = render(e, sr, 4 * 60.0 / bpm / 4.0 * 3);
        const int L = (int) std::lround(4 * sd);
        CHECK(rms(r.L, 0, 1500) > 0.01 && rms(r.L, L, L + 1500) > 0.01, "LOOP: playback starts at the loop (snare) and repeats every 4 steps");
    }
    // 4. FX
    auto oneHit = [&](const juce::String& cell, double secs, const char* bank = "block") {
        auto rows = blank(16); rows[0][0] = cell; Engine e(store, synth); e.prepare(sr); e.setPattern(Pattern::fromState(juce::JSON::parse(stateJ(rows, bpm, bank)))); e.setInternalPlay(true);
        return render(e, sr, secs).L; };
    {
        auto plain = oneHit(cellJ(4, 100), 0.5), cut = oneHit(cellJ(4, 100, "CUT", 1), 0.5);   // open hat
        const int qs = (int) (sd / 4) + (int) (sr * 0.02);
        CHECK(rms(plain, qs, qs + 3000) > 0.005 && rms(cut, qs, qs + 3000) < 0.0005, "CUT 1: open hat stops after a quarter step");
        auto off = oneHit(cellJ(4, 100, "NOTEOFF"), 0.5); const int hs = (int) (sd / 2) + (int) (sr * 0.01);
        CHECK(rms(off, hs, hs + 3000) < 0.0005, "NOTE OFF: stops at half a step");
        auto rt = oneHit(cellJ(3, 100, "RETRIG", 3, 0, 18), 0.12), hat = oneHit(cellJ(3, 100, "", 4, 0, 18), 0.12);   // short house-pack closed hat + 3 repeats inside one step
        int hits = 0; const double sp = sd / 4;
        for (int k = 1; k <= 3; ++k) { const int a = (int) (k * sp) + 300; std::printf("    repeat %d: %.4f vs plain %.4f\n", k, rms(rt, a, a + 400), rms(hat, a, a + 400)); if (rms(rt, a, a + 400) > 1.8 * rms(hat, a, a + 400) + 0.001) ++hits; }
        std::printf("  retrig repeats heard: %d of 3\n", hits);
        CHECK(hits == 3, "RETRIG 3: hat repeats 3 times inside the step");
        auto sw = oneHit(cellJ(4, 100, "VSLIDE", 1, 0, 3), 0.3);   // house-pack open hat, quarter-step swell CHECK(rms(sw, 0, 400) < 0.4 * rms(sw, 2000, 2600) + 0.002, "VOL SLIDE: swells in");
        auto ps = oneHit(cellJ(8, 100, "PSLIDE", 7), 0.6), pl = oneHit(cellJ(8, 100), 0.6);
        double d = 0; for (size_t i = 8000; i < 20000 && i < ps.size(); ++i) d += std::abs(ps[i] - pl[i]); CHECK(d > 50, "PITCH SLIDE: changes the sub's pitch over time");
        auto dl = oneHit(cellJ(6, 100, "DELAY", 8), 0.5); const int e1 = (int) (sd * 0.75); CHECK(rms(dl, e1, e1 + 600) > 0.003, "DELAY: echo at 3/4 step");
        auto ar = oneHit(cellJ(5, 100, "ARP", 7), 0.3); const int a1 = (int) (sd / 3); CHECK(rms(ar, a1, a1 + 500) > 0.003, "ARP: extra hits at 1/3 and 2/3 step");
        auto vb = oneHit(cellJ(8, 100, "VIB", 8), 0.6); double dv = 0; for (size_t i = 6000; i < 20000 && i < vb.size(); ++i) dv += std::abs(vb[i] - pl[i]); CHECK(dv > 50, "VIBRATO: wobbles the pitch");
        auto syn = oneHit(cellJ(0, 100), 0.3, "synth"); CHECK(rms(syn, 0, 4000) > 0.02, "SYNTH bank: built-in kick sounds");
        auto cr = oneHit(cellJ(7, 100), 0.3, "block"); CHECK(rms(cr, 0, 4000) > 0.01, "RIDE with no default sound falls back to the synth cymbal");
        auto lib = oneHit(cellJ(7, 100), 0.3, "all"); CHECK(rms(lib, 0, 4000) > 0.01, "RIDE in ALL plays a ride sample");
        auto hp = oneHit(cellJ(0, 100, "", 4, 0, 77), 0.3, "lib"); CHECK(rms(hp, 0, 4000) > 0.02, "HOUSE PACK: a synthesised house kick plays");
    }
    // 4b. swing: a snare on step 1 lands (swing - 50) / 50 of a step late; even steps stay put
    {
        auto rows = blank(16); rows[1][1] = cellJ(1, 100); rows[2][1] = cellJ(1, 100);
        auto onset = [&](int swing, int from) { Engine e(store, synth); e.prepare(sr); e.setPattern(Pattern::fromState(juce::JSON::parse(stateJ(rows, bpm, "block", "null", swing)))); e.setInternalPlay(true);
                                               auto r = render(e, sr, 0.5); return firstAbove(r.L, 0.02f, from); };
        const int straight1 = onset(50, 0), swung1 = onset(66, 0), want = (int) std::lround(0.32 * sd);
        std::printf("  step 1: straight %d, swing 66 %d (want +%d)\n", straight1, swung1, want);
        CHECK(std::abs((swung1 - straight1) - want) <= 2, "SWING 66: the odd 16th lands 32% of a step late");
        const int from2 = (int) (2 * sd) - 50; const int straight2 = onset(50, from2), swung2 = onset(66, from2);
        CHECK(std::abs(swung2 - straight2) <= 1, "SWING: even 16ths are not moved");
        CHECK(Pattern::fromState(juce::JSON::parse("{\"pats\":[{\"rows\":[]}],\"bpm\":124}"))->swing == 56.0, "no swing in the state = the page default (56)");
    }
    // 4c. FL-style note length and mute (from the page's draw / mute tools)
    {
        auto plain = oneHit(cellJ(4, 100), 0.5);
        auto lr = blank(16); lr[0][0] = "{\"i\":4,\"v\":100,\"fx\":\"\",\"pr\":4,\"p\":0,\"l\":0.25}";
        Engine e(store, synth); e.prepare(sr); e.setPattern(Pattern::fromState(juce::JSON::parse(stateJ(lr, bpm)))); e.setInternalPlay(true);
        auto r = render(e, sr, 0.5).L; const int after = (int) (sd / 4) + (int) (sr * 0.02);
        CHECK(rms(plain, after, after + 3000) > 0.005 && rms(r, after, after + 3000) < 0.0005, "LENGTH: a quarter-step note stops at its end");
        auto mr = blank(16); mr[0][0] = "{\"i\":0,\"v\":100,\"fx\":\"\",\"pr\":4,\"p\":0,\"m\":true}";
        Engine em(store, synth); em.prepare(sr); em.setPattern(Pattern::fromState(juce::JSON::parse(stateJ(mr, bpm)))); em.setInternalPlay(true);
        CHECK(rms(render(em, sr, 0.3).L, 0, 10000) < 1e-6, "MUTE: a muted hit is silent");
        CHECK(Pattern::fromState(juce::JSON::parse(stateJ(lr, bpm)))->at(0, 0).l == 0.25f, "length parses from the page state");
        auto sy = oneHit(cellJ(0, 100, "", 4, 0, 6), 0.3); auto sy2 = blank(16); sy2[0][0] = "{\"i\":0,\"v\":100,\"fx\":\"\",\"pr\":4,\"p\":0,\"s\":-2}";
        Engine es(store, synth); es.prepare(sr); es.setPattern(Pattern::fromState(juce::JSON::parse(stateJ(sy2, bpm)))); es.setInternalPlay(true);
        auto syr = render(es, sr, 0.3).L; double dd = 0; for (int k = 0; k < 6000; ++k) dd += std::abs(syr[(size_t) k] - sy[(size_t) k]);
        CHECK(rms(syr, 0, 4000) > 0.02 && dd > 20, "SYNTH per note (s = -2): the note plays the built-in kick, not the sample");
    }
    // 4d. volume: drum volume (MIX) and channel volume / mute / solo (the page's channel panel)
    {
        auto rows = blank(16); rows[0][0] = cellJ(0, 100); rows[0][1] = cellJ(0, 100);
        auto run = [&](const juce::String& st) { Engine e(store, synth); e.prepare(sr); e.setPattern(Pattern::fromState(juce::JSON::parse(st))); e.setInternalPlay(true); return rms(render(e, sr, 0.2).L, 0, 6000); };
        const juce::String base = stateJ(rows, bpm);
        const double full = run(base);
        const double half = run(base.replaceFirstOccurrenceOf("{\"pats\"", "{\"mix\":[0.5,1,1,1,1,1,1,1,1,1],\"pats\""));
        const double cm = run(base.replaceFirstOccurrenceOf(",\"loop\":null}", ",\"loop\":null,\"cm\":[true,true]}"));
        const double one = run(base.replaceFirstOccurrenceOf(",\"loop\":null}", ",\"loop\":null,\"cs\":[false,true]}"));
        const double oneHalf = run(base.replaceFirstOccurrenceOf(",\"loop\":null}", ",\"loop\":null,\"cs\":[null,true],\"cv\":[1,0.5]}"));
        std::printf("  rms: full %.4f, kick 50%% %.4f, muted %.6f, solo one %.4f, solo one at 50%% %.4f\n", full, half, cm, one, oneHalf);
        CHECK(half < full * 0.75 && half > full * 0.25, "MIX: every kick at 50% plays quieter");
        CHECK(cm < 1e-6, "channel MUTE: muted channels are silent");
        CHECK(one > 0.01 && one < full * 0.8, "SOLO: only the solo channel plays");
        CHECK(oneHalf < one * 0.75 && oneHalf > one * 0.25, "channel VOLUME 50% on the solo channel");
    }
    // 4e. KEY: pitched hits land on the key's notes (same sum as the page)
    {
        int kick = -1; for (int i = 0; i < store.count(0) && kick < 0; ++i) { store.decodeNow(0, i); if (const Sample* s = store.get(0, i); s && s->rt > 0 && std::abs(s->rt - std::round(s->rt)) > .2) kick = i; }
        CHECK(kick >= 0, "the kit's kicks carry their measured note (rt)");
        if (kick >= 0) {
            const Sample* ks = store.get(0, kick); auto rows = blank(16); rows[0][0] = cellJ(0, 100, "", 4, 0, kick);
            const juce::String off = stateJ(rows, bpm), on = off.replaceFirstOccurrenceOf("{\"pats\"", "{\"key\":{\"on\":true,\"root\":9,\"scale\":\"minor\",\"mode\":\"scale\"},\"pats\"");
            auto P = Pattern::fromState(juce::JSON::parse(on)); const double sh = keyShift(P.get(), 0, ks->rt, 0), fin = ks->rt + sh;
            const int pc = (((int) std::lround(fin) - 9) % 12 + 12) % 12; const int sc[] = {0, 2, 3, 5, 7, 8, 10}; bool inKey = false; for (int x : sc) inKey = inKey || x == pc;
            std::printf("  kick %d: plays %.2f, key A minor moves it %+.2f to %.2f\n", kick, ks->rt, sh, fin);
            CHECK(inKey && std::abs(fin - std::round(fin)) < 1e-6 && std::abs(sh) <= 1.0, "KEY A minor: the kick lands exactly on an A minor note");
            auto render1 = [&](const juce::String& st) { Engine e(store, synth); e.prepare(sr); e.setPattern(Pattern::fromState(juce::JSON::parse(st))); e.setInternalPlay(true); return render(e, sr, 0.3).L; };
            auto a = render1(off), b = render1(on); double d = 0; for (size_t i = 0; i < 12000 && i < a.size(); ++i) d += std::abs(a[i] - b[i]);
            CHECK(d > 5, "KEY on: the kick really plays retuned");
            auto P2 = Pattern::fromState(juce::JSON::parse(on.replace("\"mode\":\"scale\"", "\"mode\":\"root\""))); const double f2 = ks->rt + keyShift(P2.get(), 0, ks->rt, 0);
            const int pc2 = (((int) std::lround(f2) - 9) % 12 + 12) % 12; CHECK(pc2 == 0 || pc2 == 7, "ROOT mode: the kick lands on A or E");
            CHECK(keyShift(Pattern::fromState(juce::JSON::parse(off)).get(), 0, ks->rt, 0) == 0.0, "KEY off: no change");
        }
    }
    // 4f. GRID: rows per beat from the page ("rpb"): beat 2 in 32nds, beat 4 in 8th triplets; a note between the lines ("o")
    {
        auto zstate = [&](std::vector<std::vector<juce::String>> rows, const char* loop = "null") { return stateJ(rows, bpm, "block", loop).replace("\"loop\":", "\"rpb\":{\"1\":8,\"3\":3},\"loop\":", false); };
        auto P0 = Pattern::fromState(juce::JSON::parse(zstate(blank(19))));
        CHECK(P0->rows == 19 && std::abs(P0->rowT[5] - 4.5) < 1e-9 && std::abs(P0->rowT[17] - (12 + 4.0 / 3)) < 1e-9 && std::abs(P0->rowT[19] - 16) < 1e-9, "GRID: row times follow the page's grid (32nds, triplets)");
        CHECK(Pattern::fromState(juce::JSON::parse(zstate(blank(20))))->rowT[5] == 5.0, "GRID: rows that don't add up play as plain 16ths");
        auto hit = [&](int row, const char* o, bool host = false) { auto rows = blank(19); rows[(size_t) row][0] = cellJ(0, 100).replace("}", juce::String(o).isEmpty() ? "}" : ",\"o\":" + juce::String(o) + "}");
            Engine e(store, synth); e.prepare(sr); e.setPattern(Pattern::fromState(juce::JSON::parse(zstate(rows)))); e.setInternalPlay(! host); return firstAbove(render(e, sr, 2.0, host, 0, bpm).L, 0.02f); };
        const int base = hit(0, "");
        struct W { int row; const char* o; double t; const char* what; } ws[] = { {4, "", 4, "beat 2 starts on time"}, {5, "", 4.5, "a 32nd lands half a step in"}, {11, "", 7.5, "the last 32nd"},
            {12, "", 8, "back to 16ths after the zone"}, {17, "", 12 + 4.0 / 3, "an 8th triplet"}, {18, "-0.25", 12 + 8.0 / 3 - 1.0 / 3, "a note between the triplet lines (o = -0.25)"}, {2, "0.5", 2.5, "a 16th pushed half a row late"} };
        for (auto& w : ws) { const int got = hit(w.row, w.o), want = base + (int) std::lround(w.t * sd); std::printf("  row %d: %d, want %d\n", w.row, got, want); const std::string msg = std::string("GRID: ") + w.what; CHECK(std::abs(got - want) <= 2, msg.c_str()); }
        const int hb = hit(17, "", true); std::printf("  host: row 17 at %d\n", hb); CHECK(std::abs(hb - (base + (int) std::lround((12 + 4.0 / 3) * sd))) <= 3, "GRID: same timing following the DAW's transport");
        { auto rows = blank(19); rows[4][0] = cellJ(0, 100); Engine e(store, synth); e.prepare(sr); e.setPattern(Pattern::fromState(juce::JSON::parse(zstate(rows, "{\"on\":true,\"a\":4,\"b\":11}")))); e.setInternalPlay(true);
          auto r = render(e, sr, 1.0); const int a = firstAbove(r.L, 0.02f), b = firstAbove(r.L, 0.02f, a + (int) (3.5 * sd));
          std::printf("  loop the 32nd beat: hits at %d and %d (want %d apart)\n", a, b, (int) std::lround(4 * sd));
          CHECK(a >= 0 && std::abs((b - a) - (int) std::lround(4 * sd)) <= 3, "GRID: looping the 32nd beat repeats every beat (8 rows = 4 steps)"); }
    }
    // 4g. ⇣ WAV: bounce the beat (or one channel) offline; swing 0-100% on the page = 50-75 here
    {
        auto rows = blank(16); rows[0][0] = cellJ(0, 100); rows[8][1] = cellJ(1, 100);
        auto P = Pattern::fromState(juce::JSON::parse(stateJ(rows, bpm)));
        auto all = Engine::bounce(store, synth, *P, sr, bpm, -1), ch1 = Engine::bounce(store, synth, *P, sr, bpm, 1), at140 = Engine::bounce(store, synth, *P, sr, 140, -1);
        const int len = (int) std::lround(16 * sd);
        std::printf("  bounce: %d samples (want %d), at 140 BPM %d\n", all.getNumSamples(), len, at140.getNumSamples());
        CHECK(all.getNumSamples() == len && at140.getNumSamples() == (int) std::lround(16 * 60.0 / 140 / 4 * sr), "WAV: exactly one bar long, at the DAW's tempo");
        std::vector<float> a(all.getReadPointer(0), all.getReadPointer(0) + all.getNumSamples()), c1(ch1.getReadPointer(0), ch1.getReadPointer(0) + ch1.getNumSamples());
        const int k0 = firstAbove(a, 0.02f), s0 = firstAbove(c1, 0.02f);
        std::printf("  kick at %d, snare-only channel first sound at %d (want ~%d)\n", k0, s0, (int) (8 * sd));
        CHECK(k0 >= 0 && k0 < 400 && rms(a, 0, (int) sd) > 0.02, "WAV: the kick is on the one");
        CHECK(s0 >= (int) (8 * sd) - 5 && s0 < (int) (8 * sd) + 400 && rms(c1, 0, (int) (6 * sd)) < 1e-4, "WAV per channel: CH2 has only its snare, nothing from the kick channel");
        bool same = true; for (int i = 0; i < len; ++i) if (std::abs(all.getSample(0, i) - (ch1.getSample(0, i) + Engine::bounce(store, synth, *P, sr, bpm, 0).getSample(0, i))) > 1e-3) { same = false; break; }
        CHECK(same, "WAV: the channels add up to the whole beat");
        auto sw = Pattern::fromState(juce::JSON::parse(stateJ(rows, bpm, "block", "null", 66)));
        CHECK(sw->swing == 66.0 && std::abs(sw->swingDelay(1, sd) - 0.32 * sd) < 1e-6, "SWING 64% on the page = 66 here: odd 16ths 32% of a step late");
    }
    // 4i. PIANO (22 real notes, nearest one) + note FX: ECHO / PING-PONG / TAPE ECHO / STUTTER with FADE
    {
        CHECK(store.count(9) >= 22, "PIANO: the grand's 22 real notes in the manifest");
        bool dp = true; for (int k = 0; k < 22; ++k) dp = store.decodeNow(9, k) && dp; CHECK(dp, "PIANO: all 22 notes decode");
        auto renderLR = [&](const juce::String& st, double secs, std::vector<float>& L, std::vector<float>& R) { Engine e(store, synth); e.prepare(sr); e.setPattern(Pattern::fromState(juce::JSON::parse(st))); e.setInternalPlay(true);
            juce::AudioBuffer<float> b(2, 512); Engine::Host h; for (int done = 0; done < (int) (sr * secs); done += 512) { b.clear(); e.process(b, 512, h); for (int i = 0; i < 512; ++i) { L.push_back(b.getSample(0, i)); R.push_back(b.getSample(1, i)); } } };
        auto one = [&](const juce::String& cell, const char* bank = "block") { auto rows = blank(16); rows[0][0] = cell; return stateJ(rows, bpm, bank); };
        auto hz = [&](const std::vector<float>& x, double want) { const int a = (int) (0.12 * sr), n = 4096; double best = -1; int bl = 1;   // autocorrelation near the wanted pitch
            for (int lag = (int) (sr / (want * 1.35)); lag <= (int) (sr / (want * 0.74)); ++lag) { double s = 0; for (int i = a; i < a + n; ++i) s += x[(size_t) i] * x[(size_t) (i + lag)]; if (s > best) { best = s; bl = lag; } } return sr / bl; };
        struct Pn { int p; double f; const char* what; } pns[] = { {0, 261.63, "middle C"}, {4, 329.63, "E4"}, {7, 392.0, "G4 (from the F#4 note, a semitone up)"}, {-12, 130.81, "C3"}, {19, 783.99, "G5"} };
        for (auto& x : pns) { std::vector<float> L, R; renderLR(one(cellJ(10, 90, "", 4, x.p)), 0.5, L, R); const double f = hz(L, x.f);
            std::printf("  piano p %d: %.1f Hz (want %.1f)\n", x.p, f, x.f); const std::string m = std::string("PIANO plays ") + x.what; CHECK(std::abs(f / x.f - 1) < 0.03 && rms(L, 0, 6000) > 0.01, m.c_str()); }
        { std::vector<float> L, R; renderLR(one(cellJ(10, 90), "synth"), 0.5, L, R); const double f = hz(L, 261.63); CHECK(std::abs(f / 261.63 - 1) < 0.03, "SYNTH bank: the built-in piano plays middle C"); }
        // echoes on a short hat: an 8th apart (2 steps), each quieter, FADE sets how many
        auto peaks = [&](const std::vector<float>& x, double every, int n) { std::vector<double> o; for (int k = 0; k < n; ++k) { const int a = (int) (k * every); double m = 0; for (int i = a; i < a + (int) (every * 0.4) && i < (int) x.size(); ++i) m = juce::jmax(m, (double) std::abs(x[(size_t) i])); o.push_back(m); } return o; };
        auto fxs = [&](const char* fx, int pr, int q) { return one(cellJ(3, 100, fx, pr).replace("}", ",\"q\":" + juce::String(q) + "}")); };
        { std::vector<float> L, R; renderLR(fxs("ECHO", 4, 8), 1.6, L, R); auto pk = peaks(L, 2 * sd, 8);
          std::printf("  ECHO 1/8 fade 8 peaks:"); for (double v : pk) std::printf(" %.3f", v); std::printf("\n");
          CHECK(pk[1] > 0.01 && pk[1] < pk[0] && pk[2] < pk[1] && pk[5] > 0.002 && pk[7] < pk[5] * 0.7, "ECHO: repeats an 8th apart, each quieter"); }
        { std::vector<float> L, R, L2, R2; renderLR(fxs("ECHO", 4, 0), 1.6, L, R); renderLR(fxs("ECHO", 4, 16), 1.6, L2, R2); auto lo = peaks(L, 2 * sd, 8), hi = peaks(L2, 2 * sd, 8);
          CHECK(lo[4] > lo[0] * 0.3 && hi[1] > hi[0] * 0.1 && hi[3] < hi[0] * 0.01, "FADE: left = a long tail, right = gone after a couple of repeats"); }
        { std::vector<float> L, R; renderLR(fxs("PING", 4, 8), 1.0, L, R); const double l1 = rms(L, (int) (2 * sd), (int) (2.4 * sd)), r1 = rms(R, (int) (2 * sd), (int) (2.4 * sd)), l2 = rms(L, (int) (4 * sd), (int) (4.4 * sd)), r2 = rms(R, (int) (4 * sd), (int) (4.4 * sd));
          std::printf("  PING: 1st repeat L %.4f R %.4f, 2nd L %.4f R %.4f\n", l1, r1, l2, r2); CHECK(r1 > l1 * 2.5 && l2 > r2 * 2.5, "PING-PONG: repeats bounce right, left"); }
        { std::vector<float> L, R; renderLR(fxs("STUT", 4, 8), 1.0, L, R); auto pk = peaks(L, sd / 4, 14);
          std::printf("  STUT 1/64 peaks:"); for (double v : pk) std::printf(" %.3f", v); std::printf("\n");
          CHECK(pk[1] > 0.01 && pk[4] > 0.005 && pk[4] < pk[1] && pk[13] < pk[1] * 0.5, "STUTTER: retriggers every 64th, fading out"); }
        CHECK(Pattern::fromState(juce::JSON::parse(fxs("TAPE", 3, 5)))->at(0, 0).q == 5 && cellFromVar(juce::JSON::parse("{\"i\":3,\"fx\":\"STUT\"}")).fx == FX_STUT, "the page's new FX and FADE parse");
    }
    // 4j. ⇣ WAV on the piano page: the whole piano, or one piano channel
    {
        auto rows = blank(16); rows[0][0] = cellJ(10, 90, "", 4, -12); rows[0][1] = cellJ(10, 90, "", 4, 4); rows[8][4] = cellJ(10, 90, "", 4, 12);
        auto P = Pattern::fromState(juce::JSON::parse(stateJ(rows, bpm)));
        auto all = Engine::bounce(store, synth, *P, sr, bpm, -1), mel = Engine::bounce(store, synth, *P, sr, bpm, 4), bass = Engine::bounce(store, synth, *P, sr, bpm, 0);
        std::vector<float> a(all.getReadPointer(0), all.getReadPointer(0) + all.getNumSamples()), m(mel.getReadPointer(0), mel.getReadPointer(0) + mel.getNumSamples()), b(bass.getReadPointer(0), bass.getReadPointer(0) + bass.getNumSamples());
        CHECK(rms(a, 0, (int) sd) > 0.01 && rms(m, (int) (8 * sd), (int) (9 * sd)) > 2 * rms(m, (int) (6 * sd), (int) (7 * sd)) && rms(m, (int) (8 * sd), (int) (9 * sd)) > 0.005 && rms(b, 0, (int) sd) > 0.005, "WAV on the piano page: whole piano, or just the melody channel, or just the bass");
    }
    // 4k. the piano sound bank: s = the sound (0 GRAND .. 14 HARPSICHORD); each plays its own real notes, in tune; only sounds in use are decoded
    {
        CHECK(store.count(9) == 22 + 229, "PIANO BANK: 13 sounds in the manifest (22 grand notes + 229)");
        int firstOf[16]; for (int g = 0; g < 16; ++g) { firstOf[g] = -1; for (int k = 0; k < store.count(9); ++k) if (store.groupOf(9, k) == g) { firstOf[g] = k; break; } }
        { auto rows = blank(16); rows[0][0] = cellJ(10, 90, "", 4, 0, 5); Engine e(store, synth); e.prepare(sr); e.setPattern(Pattern::fromState(juce::JSON::parse(stateJ(rows, bpm))));
          for (int w = 0; w < 400 && (store.stateOf(9, firstOf[5]) == 1 || store.stateOf(9, firstOf[5]) == 4); ++w) juce::Thread::sleep(10);
          CHECK(store.stateOf(9, firstOf[5]) == 2 && store.stateOf(9, firstOf[13]) == 0, "a pattern using HAUNTED decodes HAUNTED, not the other sounds"); }
        for (int g : { 1, 2, 3, 10, 11, 12, 13, 14 }) for (int k = 0; k < store.count(9); ++k) if (store.groupOf(9, k) == g) store.decodeNow(9, k);
        auto renderL = [&](const juce::String& st) { Engine e(store, synth); e.prepare(sr); e.setPattern(Pattern::fromState(juce::JSON::parse(st))); e.setInternalPlay(true); return render(e, sr, 0.6).L; };
        auto hz = [&](const std::vector<float>& x, double want) { const int a = (int) (0.15 * sr), n = 4096; double best = -1; int bl = 1;
            for (int lag = (int) (sr / (want * 1.35)); lag <= (int) (sr / (want * 0.74)); ++lag) { double s = 0; for (int i = a; i < a + n; ++i) s += x[(size_t) i] * x[(size_t) (i + lag)]; if (s > best) { best = s; bl = lag; } } return sr / bl; };
        std::vector<float> grand; { auto rows = blank(16); rows[0][0] = cellJ(10, 90, "", 4, 4, 0); grand = renderL(stateJ(rows, bpm)); }
        for (int g : { 1, 2, 10, 11, 12, 13 }) { auto rows = blank(16); rows[0][0] = cellJ(10, 90, "", 4, 4, g); auto L = renderL(stateJ(rows, bpm)); const double f = hz(L, 329.63);
            double diff = 0; for (int i = 0; i < 20000; ++i) diff += std::abs(L[(size_t) i] - grand[(size_t) i]);
            std::printf("  sound %d E4: %.1f Hz, differs from grand %.1f\n", g, f, diff);
            const std::string m = "PIANO BANK sound " + std::to_string(g) + ": plays E4 in tune, its own recording"; CHECK(std::abs(f / 329.63 - 1) < 0.03 && diff > 50 && rms(L, 0, 6000) > 0.005, m.c_str()); }
        { auto rows = blank(16); rows[0][0] = cellJ(10, 90, "", 4, 4); auto st = stateJ(rows, bpm).replace("\"block\":[6,3,0,1,0,0,7,-1,0,0]", "\"block\":[6,3,0,1,0,0,7,-1,0,0,10]");
          auto L = renderL(st); std::vector<float> wur;
          { auto r3 = blank(16); r3[0][0] = cellJ(10, 90, "", 4, 4, 3); auto gone = renderL(stateJ(r3, bpm)); double dg = 0; for (int i = 0; i < 20000; ++i) dg += std::abs(gone[(size_t) i] - grand[(size_t) i]); CHECK(dg < 1.0, "a note set to a removed sound (SCARY) plays GRAND"); } { auto r2 = blank(16); r2[0][0] = cellJ(10, 90, "", 4, 4, 10); wur = renderL(stateJ(r2, bpm)); }
          double diff = 0; for (int i = 0; i < 20000; ++i) diff += std::abs(L[(size_t) i] - wur[(size_t) i]); CHECK(diff < 1.0, "a note with no sound of its own plays the page's chosen piano sound (WURLITZER)"); }
    }
    // 4l. PAN: per note (pn) and per channel (cp), added together
    {
        auto lr = [&](const juce::String& cell, const char* cp) { auto rows = blank(16); rows[0][0] = cell; auto st = stateJ(rows, bpm).replace("\"loop\":null}", juce::String("\"loop\":null,\"cp\":") + cp + "}", false);
            Engine e(store, synth); e.prepare(sr); e.setPattern(Pattern::fromState(juce::JSON::parse(st))); e.setInternalPlay(true);
            juce::AudioBuffer<float> b(2, 512); Engine::Host h; double l = 0, r = 0; for (int k = 0; k < 20; ++k) { b.clear(); e.process(b, 512, h); l += b.getRMSLevel(0, 0, 512); r += b.getRMSLevel(1, 0, 512); } return std::make_pair(l, r); };
        auto c0 = lr(cellJ(0, 100), "[]"), right = lr(cellJ(0, 100).replace("}", ",\"pn\":100}"), "[]"), left = lr(cellJ(0, 100).replace("}", ",\"pn\":-60}"), "[]"), chL = lr(cellJ(0, 100), "[-100]"), both = lr(cellJ(0, 100).replace("}", ",\"pn\":100}"), "[-100]");
        std::printf("  pan L/R: center %.3f/%.3f, note R100 %.3f/%.3f, note L60 %.3f/%.3f, channel L100 %.3f/%.3f, note R100 + channel L100 %.3f/%.3f\n", c0.first, c0.second, right.first, right.second, left.first, left.second, chL.first, chL.second, both.first, both.second);
        CHECK(right.first < 0.02 * c0.first && left.second / c0.second < 0.5 * left.first / c0.first && chL.second < 0.02 * c0.second && std::abs(both.first - c0.first) < 0.05 * c0.first && std::abs(both.second - c0.second) < 0.05 * c0.second,   // per side: the kick itself isn't centred
              "PAN: a note hard right, a note 60 left, a channel hard left, and note + channel cancel out to the middle");
    }
    // 4p. PIANO PREVIEW: clicking a piano sound before any note uses it plays THAT sound (not the built-in piano)
    {
        const double psr = 44100.0; Engine e(store, synth); e.prepare(psr); e.setPattern(Pattern::fromState(juce::JSON::parse(stateJ(blank(16), bpm)))); e.setInternalPlay(false);
        int g = -1; for (int k = 0; k < store.count(9); ++k) { const int gg = store.groupOf(9, k); if (gg > 0 && store.stateOf(9, k) == 0) { g = gg; break; } }
        if (g < 0) g = 12;
        int best = -1; double bd = 1e9; for (int k = 0; k < store.count(9); ++k) if (store.groupOf(9, k) == g) { const double d = std::abs(store.rootOf(9, k) - 60.0); if (d < bd) { bd = d; best = k; } }
        const int pp = (int) std::lround(store.rootOf(9, best)) - 60;   // its own recorded note: no resampling
        e.audition(cellFromVar(juce::JSON::parse(cellJ(10, 100, "", 4, pp, g))));
        auto r = render(e, psr, 0.4);
        const Sample* sm = store.get(9, best); double c = -1; const int at = firstAbove(r.L, 1e-6);
        if (sm && at >= 0) for (int lag = 0; lag < 400; ++lag) { const float* x = sm->buf.getReadPointer(0); double xy = 0, xx = 0, yy = 0; const int m = juce::jmin(8000, sm->buf.getNumSamples() - lag, (int) r.L.size() - at);
            for (int i = 0; i < m; ++i) { xy += x[i + lag] * r.L[(size_t) (at + i)]; xx += x[i + lag] * x[i + lag]; yy += r.L[(size_t) (at + i)] * r.L[(size_t) (at + i)]; } c = juce::jmax(c, xy / std::sqrt(xx * yy + 1e-12)); } std::printf("  piano preview: sound %d, matches its own recording %.3f\n", g, c);
        CHECK(sm != nullptr && c > 0.9, "PIANO PREVIEW: the clicked piano sound plays right away, not the built-in piano");
    }
    // 4m. every piano sound plays its own recording at every sampled note (engine output vs the decoded sample)
    {
        int bad = 0, n = 0;
        for (int k = 0; k < store.count(9); ++k) { const int g = store.groupOf(9, k); if (g == 0) continue; store.decodeNow(9, k); }
        for (int k = 0; k < store.count(9); ++k) { const int g = store.groupOf(9, k); if (g == 0) continue; const Sample* s = store.get(9, k); if (! s) { ++bad; continue; }
            const int p = (int) std::lround(s->rt) - 60; if (p < -24 || p > 24) continue;
            auto rows = blank(16); rows[0][0] = cellJ(10, 100, "", 4, p, g); Engine e(store, synth); e.prepare(s->sr); e.setPattern(Pattern::fromState(juce::JSON::parse(stateJ(rows, bpm)))); e.setInternalPlay(true);
            auto L = render(e, s->sr, 0.5).L; const float* x = s->buf.getReadPointer(0); const int m = juce::jmin(16000, s->buf.getNumSamples(), (int) L.size());
            double xy = 0, xx = 0, yy = 0; for (int i = 0; i < m; ++i) { xy += x[i] * L[(size_t) i]; xx += x[i] * x[i]; yy += L[(size_t) i] * L[(size_t) i]; }
            const double c = xy / std::sqrt(xx * yy + 1e-12); ++n; if (c < 0.97) { ++bad; std::printf("  set %d note %.0f (%s): corr %.3f\n", g, s->rt, s->name.toRawUTF8(), c); } }
        std::printf("  checked %d bank notes, %d off\n", n, bad);
        CHECK(bad == 0, "PIANO BANK: every sound plays its own recorded notes, untouched");
    }
    // 4n. DRAG-OUT AFTER THE PIANO: with every piano sound still queued to load, a drum WAV still comes out whole, right away
    {
        int k0 = -1, s0 = -1; for (int k = 60; k < store.count(0); ++k) if (store.stateOf(0, k) == 0) { k0 = k; break; }
        for (int k = 30; k < store.count(1); ++k) if (store.stateOf(1, k) == 0) { s0 = k; break; }
        for (int g = 1; g < 16; ++g) store.requestGroup(9, g);   // a long line of piano in front
        auto rows = blank(16); rows[0][0] = cellJ(0, 100, "", 4, 0, k0); rows[4][1] = cellJ(1, 100, "", 4, 0, s0); rows[8][0] = cellJ(0, 100, "", 4, 0, k0); rows[12][1] = cellJ(1, 100, "", 4, 0, s0);
        auto P = Pattern::fromState(juce::JSON::parse(stateJ(rows, bpm)));
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        auto all = Engine::bounce(store, synth, *P, sr, bpm, -1); const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        const int st = (int) (sr * 60.0 / bpm);   // one beat = 4 steps
        const double kick = all.getRMSLevel(0, 0, st / 2), snare = all.getRMSLevel(0, st, st / 2);
        std::printf("  drum WAV behind a piano queue: kick %d snare %d, %.0f ms, kick rms %.3f, snare rms %.3f\n", k0, s0, ms, kick, snare);
        CHECK(k0 >= 0 && s0 >= 0 && kick > 0.01 && snare > 0.01 && ms < 3000, "DRAG-OUT: drums come out whole even while the piano sounds are still loading");
        for (int w = 0; w < 3000; ++w) { bool busy = false; for (int k = 0; k < store.count(9); ++k) { const int q = store.stateOf(9, k); if (q == 1 || q == 4) { busy = true; break; } } if (! busy) break; juce::Thread::sleep(10); }
    }
    // 4o. SYNTH page + LAYERS: synth notes play (accent, slide), the drum page plays along when its layer is on, WAV ALL vs PAGE
    {
        const int S = 5;
        auto sc = [&](int p, bool ac, bool sl) { return juce::String("{\"i\":11,\"s\":") + juce::String(S) + ",\"v\":90,\"p\":" + juce::String(p) + (ac ? ",\"ac\":true" : "") + (sl ? ",\"sl\":true" : "") + "}"; };
        auto rows = blank(16); rows[0][0] = sc(0, true, false); rows[4][0] = sc(12, false, true); rows[8][0] = sc(7, false, false); rows[12][0] = sc(3, true, false);
        const juce::String syn = stateJ(rows, bpm);
        Engine e(store, synth); e.prepare(sr); e.setPattern(Pattern::fromState(juce::JSON::parse(syn))); e.setInternalPlay(true);
        auto r = render(e, sr, 1.2); const double lv = rms(r.L, 0, (int) r.L.size()); int nan = 0; for (float x : r.L) nan += ! std::isfinite(x);
        std::printf("  synth sound %d: rms %.3f, nan %d\n", S, lv, nan);
        CHECK(lv > 0.01 && nan == 0, "SYNTH: synth notes play through the synth bus (no NaN)");
        // the slid note (step 4) holds its gate into step 8: sound all the way through steps 5-7
        const int st = (int) (sr * 60.0 / bpm / 4.0);
        CHECK(rms(r.L, st * 6, st * 8) > 0.002, "SLIDE: a slid note keeps sounding into the next note");
        { auto rr = blank(16); rr[0][0] = juce::String("{\"i\":11,\"s\":0,\"v\":90,\"p\":0,\"sl\":true}"); rr[4][1] = juce::String("{\"i\":11,\"s\":0,\"v\":90,\"p\":12}");
          Engine es(store, synth); es.prepare(sr); es.setPattern(Pattern::fromState(juce::JSON::parse(stateJ(rr, bpm)))); es.setInternalPlay(true);
          auto x = render(es, sr, 0.6); std::printf("  slide across channels: steps 1-3 rms %.4f\n", rms(x.L, st * 1, st * 4));
          CHECK(rms(x.L, st * 1, st * 4) > 0.003, "SLIDE ACROSS CHANNELS: a 303 note on CH1 slides into the next 303 note on CH2 (one voice per sound)"); }
        // LAYERS: the same synth page, with the drum page playing along
        auto drows = blank(16); drows[0][1] = cellJ(0, 100); drows[8][1] = cellJ(0, 100);
        const juce::String ds = stateJ(drows, bpm); const int a = ds.indexOf("\"pats\":") + 7, b = ds.indexOf(",\"cur\":");
        auto withLay = [&](bool on) { return syn.dropLastCharacters(1) + ",\"page\":\"synth\",\"lay\":{\"drums\":" + (on ? "true" : "false") + "},\"pg\":{\"drums\":{\"cur\":0,\"pats\":" + ds.substring(a, b) + "}}}"; };
        Engine e1(store, synth); e1.prepare(sr); e1.setPattern(Pattern::fromState(juce::JSON::parse(withLay(true)))); e1.setInternalPlay(true);
        Engine e0(store, synth); e0.prepare(sr); e0.setPattern(Pattern::fromState(juce::JSON::parse(withLay(false)))); e0.setInternalPlay(true);
        store.decodeNow(0, 6);
        auto r1 = render(e1, sr, 1.2), r0 = render(e0, sr, 1.2);
        const double k1 = rms(r1.L, st * 8, st * 8 + 2000), k0 = rms(r0.L, st * 8, st * 8 + 2000);
        std::printf("  layers: step 8 with drums along %.3f, without %.3f\n", k1, k0);
        CHECK(k1 > k0 * 1.3 + 0.01, "LAYERS: the drum page plays along with the synth page when its layer is on");
        // LAYERS: the synth page on screen muted, drums along -> only the drums play
        { juce::String m = withLay(true).replace("\"layv\":2", "").replace(",\"lay\":{\"drums\":true}", ",\"layv\":2,\"lay\":{\"drums\":true,\"synth\":false}");
          Engine em(store, synth); em.prepare(sr); em.setPattern(Pattern::fromState(juce::JSON::parse(m))); em.setInternalPlay(true);
          auto rm = render(em, sr, 1.2); const double s0 = rms(rm.L, st * 4, st * 7), k8 = rms(rm.L, st * 8, st * 8 + 2000);
          std::printf("  synth page muted: synth-only steps %.4f, drum step %.3f\n", s0, k8);
          CHECK(s0 < 0.002 && k8 > 0.05, "LAYERS: muting the page on screen silences it, the drums along still play"); }
        auto P1 = Pattern::fromState(juce::JSON::parse(withLay(true)));
        auto all = Engine::bounce(store, synth, *P1, sr, bpm, -1), page = Engine::bounce(store, synth, *P1, sr, bpm, -2);
        const double ra = all.getRMSLevel(0, 0, all.getNumSamples()), rp = page.getRMSLevel(0, 0, page.getNumSamples());
        std::printf("  WAV ALL %.3f vs PAGE %.3f\n", ra, rp);
        CHECK(rp > 0.01 && ra > rp * 1.05, "DRAG-OUT: ALL has every layer playing, PAGE just this page");
    }
    // 5. choke: open hat then closed hat on the same channel one step later -> the open hat is cut
    {
        auto rows = blank(16); rows[0][0] = cellJ(4, 100); rows[1][0] = cellJ(4, 1);   // nearly silent second hit
        Engine e(store, synth); e.prepare(sr); e.setPattern(Pattern::fromState(juce::JSON::parse(stateJ(rows, bpm)))); e.setInternalPlay(true);
        auto r = render(e, sr, 0.5); auto open = oneHit(cellJ(4, 100), 0.5);
        const int s1 = (int) sd + (int) (sr * 0.02);
        CHECK(rms(r.L, s1, s1 + 3000) < 0.25 * rms(open, s1, s1 + 3000), "CHOKE: a new hit on the channel cuts the previous one");
    }
    // 6. speed: 16 channels, every step full, retrigs everywhere, 10 s
    {
        juce::String r = "[";
        for (int s = 0; s < 64; ++s) { r += (s ? "," : ""); r += "["; for (int c = 0; c < 16; ++c) { r += (c ? "," : ""); r += cellJ(c % 10, 80, (s + c) % 3 == 0 ? "RETRIG" : "", 8, (s % 7) - 3); } r += "]"; }
        r += "]";
        const juce::String st = "{\"pats\":[{\"bars\":4,\"ch\":16,\"rows\":" + r + ",\"loop\":null},{\"rows\":[]},{\"rows\":[]},{\"rows\":[]}],\"cur\":0,\"bpm\":190,\"bank\":\"all\",\"sndB\":{\"all\":[6,3,0,1,0,0,7,0,0,0]}}";
        Engine e(store, synth); e.prepare(sr); auto P = Pattern::fromState(juce::JSON::parse(st)); CHECK(P->ch == 16 && P->rows == 64, "16-channel pattern parses");
        e.setPattern(std::move(P)); e.setInternalPlay(true);
        const double a = juce::Time::getMillisecondCounterHiRes(); auto out = render(e, sr, 10.0); const double ms = juce::Time::getMillisecondCounterHiRes() - a;
        std::printf("  worst case: 10 s of audio in %.0f ms (%.0fx real time), voices %d\n", ms, 10000.0 / ms, e.activeVoices());
        CHECK(ms < 2500, "worst-case pattern renders well faster than real time");
        bool finite = true; for (float x : out.L) if (! std::isfinite(x)) { finite = false; break; } CHECK(finite, "no NaN / inf");
    }
    // 4q. CLEAN SOUND (owner 2026-10-29: pops and clicks): the output never goes over 0 dBFS, a choked note keeps fading from where it is,
    // a held synth note changing accent glides, the FM bass starts without a step
    {
        auto stepAt = [](const std::vector<float>& x, int a, int b) { double m = 0; for (int i = juce::jmax(1, a); i < juce::jmin(b, (int) x.size()); ++i) m = juce::jmax(m, (double) std::abs(x[(size_t) i] - x[(size_t) i - 1])); return m; };
        for (int k = 0; k < 22; ++k) store.decodeNow(9, k);
        auto rows = blank(32); for (int r = 0; r < 32; r += 2) for (int c = 0; c < 5; ++c) rows[(size_t) r][(size_t) c] = cellJ(10, 127, "", 4, -12 + c * 4 + (r % 8));
        auto P = Pattern::fromState(juce::JSON::parse(stateJ(rows, bpm))); auto b = Engine::bounce(store, synth, *P, sr, bpm, -1);
        const float pk = b.getMagnitude(0, b.getNumSamples()); std::printf("  5-note piano chords at full velocity: peak %.3f, rms %.3f\n", pk, b.getRMSLevel(0, 0, b.getNumSamples()));
        CHECK(pk < 0.99f && pk > 0.5f, "CLEAN: stacked piano chords stay under 0 dBFS (no clipping in FL)");
        // a long piano note fading out, choked half way through its fade by the next note on its channel: no instant drop
        { auto rr = blank(16); rr[0][0] = cellJ(10, 110, "", 4, 0).replace("}", ",\"l\":2}"); rr[2][0] = cellJ(10, 1, "", 4, 24).replace("}", ",\"o\":0.4}");
          Engine e(store, synth); e.prepare(sr); e.setPattern(Pattern::fromState(juce::JSON::parse(stateJ(rr, bpm)))); e.setInternalPlay(true);
          auto x = render(e, sr, 1.0); const int st = (int) (sr * 60.0 / bpm / 4.0), at = (int) (2.4 * st);
          const double jump = stepAt(x.L, at - 8, at + 8), before = stepAt(x.L, at - 400, at - 40); CHECK(rms(x.L, at - 400, at - 40) > 0.005, "(the fading note is still sounding when it is choked)");
          std::printf("  choke during a fade: step at the choke %.4f, biggest step just before %.4f\n", jump, before);
          CHECK(jump < before * 1.6 + 0.004, "CLEAN: a note choked while it fades keeps fading from where it is (no click)"); }
        // 101: a held (slid) accented note into a plain one: the level glides
        { auto rr = blank(16); rr[0][0] = "{\"i\":11,\"s\":2,\"v\":100,\"p\":-11,\"ac\":true,\"sl\":true}"; rr[1][0] = "{\"i\":11,\"s\":2,\"v\":100,\"p\":-11}";
          Engine e(store, synth); e.prepare(sr); e.setPattern(Pattern::fromState(juce::JSON::parse(stateJ(rr, bpm)))); e.setInternalPlay(true);
          auto x = render(e, sr, 0.5); const int st = (int) (sr * 60.0 / bpm / 4.0);
          const double jump = stepAt(x.L, st - 6, st + 6), before = stepAt(x.L, st - 600, st - 30);
          std::printf("  101 accent -> plain on a held note: step %.4f, before %.4f\n", jump, before);
          CHECK(jump < before * 1.6 + 0.004, "CLEAN: a held synth note changing accent glides (no click)"); }
        // FM BASS: the first samples of a note rise smoothly
        { auto rr = blank(16); rr[0][0] = "{\"i\":11,\"s\":3,\"v\":100,\"p\":0}";
          Engine e(store, synth); e.prepare(sr); e.setPattern(Pattern::fromState(juce::JSON::parse(stateJ(rr, bpm)))); e.setInternalPlay(true);
          auto x = render(e, sr, 0.2); const double first = stepAt(x.L, 0, (int) (sr * 0.0012)), body = stepAt(x.L, (int) (sr * 0.01), (int) (sr * 0.05));
          std::printf("  FM BASS onset: biggest step in the first 1.2 ms %.4f, in the body %.4f\n", first, body);
          CHECK(first < body * 0.5 + 0.002, "CLEAN: a synth note starts without a step (no click)"); }
    }

    // TEMPO ½x | 1x | 2x (owner 2026-11-07): playback locked to the DAW's bars at the multiple, the MIDI clip and the WAV as heard
    {
        std::printf("\nTEMPO\n");
        auto withMul = [](const juce::String& st, double m) { return st.replace("\"cur\":0,", "\"cur\":0,\"tmul\":" + juce::String(m) + ","); };
        const double hb = 130;   // the DAW's tempo
        auto sn = blank(16); sn[8][1] = cellJ(1, 100);   // a snare on step 8 only
        Engine ref(store, synth); ref.prepare(sr); ref.setPattern(Pattern::fromState(juce::JSON::parse(stateJ(sn, 150))));
        { Cell c; c.i = 1; c.v = 100; ref.playNow(c, 0); } const int onRef = firstAbove(render(ref, sr, 0.05).L, 0.02f);
        CHECK(Pattern::fromState(juce::JSON::parse(stateJ(sn, 150)))->mul == 1.0, "a new beat (no TEMPO saved) plays at 1x");
        CHECK(Pattern::fromState(juce::JSON::parse(withMul(stateJ(sn, 150), 3)))->mul == 1.0, "anything but ½ / 1 / 2 plays at 1x");
        for (double m : { 0.5, 1.0, 2.0 }) {
            const juce::String nm = m == 0.5 ? "1/2x" : m == 1.0 ? "1x" : "2x";
            const double stepS = 60.0 / (hb * m) / 4.0 * sr;
            auto mk = [&] { auto e = std::make_unique<Engine>(store, synth); e->prepare(sr); e->setPattern(Pattern::fromState(juce::JSON::parse(withMul(stateJ(sn, 150), m)))); return e; };
            // sync from the DAW's bar 1: step 8 lands 8 steps of (DAW tempo x m) in
            { auto e = mk(); auto r = render(*e, sr, 2.2, true, 0.0, hb, true); const int on = firstAbove(r.L, 0.02f), want = (int) std::lround(8 * stepS) + onRef;
              std::printf("  %s: DAW at %.0f, snare (step 8) at sample %d, expected %d (%.0f BPM)\n", nm.toRawUTF8(), hb, on, want, hb * m);
              CHECK(on >= 0 && std::abs(on - want) <= 2, nm + ": playing with the DAW, step 8 lands exactly at " + juce::String(hb * m, 0) + " BPM"); }
            // locked to the DAW's bars: the DAW starts at bar 2 (ppq 4). ½x: bar 2 is the 2nd half of the 16 steps (step 8 at once);
            // 1x / 2x: bar 2 starts the pattern again (step 8 after 8 steps)
            { auto e = mk(); auto r = render(*e, sr, 2.2, true, 4.0, hb, true); const int on = firstAbove(r.L, 0.02f), want = (m == 0.5 ? 0 : (int) std::lround(8 * stepS)) + onRef;
              std::printf("  %s: DAW from bar 2, snare at %d, expected %d\n", nm.toRawUTF8(), on, want);
              CHECK(on >= 0 && std::abs(on - want) <= 2, nm + ": locked to the DAW's bars (starting from bar 2)"); }
            // the DAW stopped (its tempo known): the preview plays at the DAW's tempo x m too, not the page's own
            { auto e = mk(); e->setInternalPlay(true); auto r = render(*e, sr, 2.2, false, 0.0, hb, true); const int on = firstAbove(r.L, 0.02f), want = (int) std::lround(8 * stepS) + onRef;
              std::printf("  %s: DAW stopped, preview snare at %d, expected %d\n", nm.toRawUTF8(), on, want);
              CHECK(on >= 0 && std::abs(on - want) <= 2, nm + ": the preview with the DAW stopped is the DAW's tempo x " + nm + " (what you hear = what you drag)"); }
            // the ⇣ MIDI clip at the DAW's tempo: step k at k x 24 / m ticks (96 per quarter): 2x = the 16 steps in half a bar
            { auto hats = blank(16); for (int k = 0; k < 16; ++k) hats[(size_t) k][2] = cellJ(3, 80); hats[8][1] = cellJ(1, 100);
              auto P = Pattern::fromState(juce::JSON::parse(withMul(stateJ(hats, 150), m))); auto mf = midiClip(*P, -1, 0, hb);
              const auto* tr = mf.getTrack(0); double snareT = -1, lastOn = -1, lastEnd = 0, tempo = 0;
              for (int i = 0; i < tr->getNumEvents(); ++i) { const auto& msg = tr->getEventPointer(i)->message;
                  if (msg.isTempoMetaEvent()) tempo = 60.0 / msg.getTempoSecondsPerQuarterNote();
                  if (msg.isNoteOn()) { if (msg.getNoteNumber() == 38) snareT = msg.getTimeStamp(); lastOn = juce::jmax(lastOn, msg.getTimeStamp()); }
                  if (msg.isNoteOff()) lastEnd = juce::jmax(lastEnd, msg.getTimeStamp()); }
              std::printf("  %s: MIDI tempo %.1f, snare at tick %.0f (expected %.0f), last hat at %.0f, clip ends %.0f (a bar = 384)\n", nm.toRawUTF8(), tempo, snareT, 8 * 24 / m, lastOn, lastEnd);
              CHECK(std::abs(tempo - hb) < 0.05, nm + ": the MIDI clip is at the DAW's tempo");
              CHECK(std::abs(snareT - 8 * 24 / m) < 0.01 && std::abs(lastOn - 15 * 24 / m) < 0.01, nm + ": MIDI note times = as heard (step 8 at tick " + juce::String(8 * 24 / m, 0) + ")");
              CHECK(lastEnd <= 16 * 24 / m + 0.01, nm + ": the 16 steps fill " + (m == 2.0 ? juce::String("half a bar") : m == 1.0 ? juce::String("one bar") : juce::String("two bars")) + " in the DAW"); }
            // the ⇣ WAV: as heard (length = 16 steps at DAW tempo x m), the snare on step 8
            { auto P = Pattern::fromState(juce::JSON::parse(withMul(stateJ(sn, 150), m))); auto b = Engine::bounce(store, synth, *P, sr, hb, -1);
              const int wantLen = (int) std::llround(16 * stepS); std::vector<float> L(b.getReadPointer(0), b.getReadPointer(0) + b.getNumSamples());
              const int on = firstAbove(L, 0.02f), want = (int) std::lround(8 * stepS) + onRef;
              std::printf("  %s: WAV %d samples (expected %d = %.3f s), snare at %d (expected %d)\n", nm.toRawUTF8(), b.getNumSamples(), wantLen, wantLen / sr, on, want);
              CHECK(std::abs(b.getNumSamples() - wantLen) <= 1, nm + ": the WAV is as long as heard (" + juce::String(wantLen / sr, 3) + " s)");
              CHECK(on >= 0 && std::abs(on - want) <= 2, nm + ": the WAV's snare is where it was heard"); }
        }
    }
    std::printf(fails ? "\n%d FAILED\n" : "\nALL PASSED\n", fails);
    return fails ? 1 : 0;
}
