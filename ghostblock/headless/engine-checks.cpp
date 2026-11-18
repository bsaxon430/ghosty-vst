// SEANCE (GhostBlock) - offline checks of the twelve-operator engine. No editor, no plugin host.
//   engine-checks            all checks
// Every check prints "ok:" or "FAIL"; exit code 0 only when all pass.
#include <cmath>
#include <cstdio>
#include <set>
#include <vector>
#include <chrono>
#include <memory>
#include <fstream>
#include <sstream>
#include <cstdlib>
#include <utility>
#include "synth/synth.h"
#include "synth/matrix_index.h"
#include "ghostblock/seance/summon.h"
#include "ghostblock/seance/steps.h"
#include "ghostblock/seance/out-guard.h"
#include "ghostblock/seance/host-params.h"
#include <limits>

using namespace baconpaul::six_sines;
static int fails{0};
#define CHECK(c, ...) do { if (c) { printf("ok: "); } else { printf("FAIL: "); fails++; } printf(__VA_ARGS__); printf("\n"); } while (0)

struct Rig
{
    std::unique_ptr<Synth> s;
    Rig() : s(std::make_unique<Synth>(false)) { s->setSampleRate(48000); quiet(); }
    void quiet()
    {
        auto &p = s->patch;
        for (auto &m : p.mixerNodes) { m.active = 0; m.level = 0; }
        for (auto &m : p.matrixNodes) { m.active = 0; m.level = 0; }
        for (auto &m : p.selfNodes) { m.active = 0; m.fbLevel = 0; }
        for (auto &m : p.sourceNodes) { m.active = 1; }
    }
    // play one note for n host samples, return the left channel
    std::vector<float> render(int key, int n, bool release = false)
    {
        std::vector<float> o; o.reserve(n);
        s->voiceManager->processNoteOnEvent(0, 0, key, -1, 0.9f, 0.f);
        size_t bp{0};
        for (int i = 0; i < n; ++i)
        {
            if (bp == 0) s->process(nullptr);
            o.push_back(s->output[0][bp]);
            if (++bp == s->blockSize) bp = 0;
        }
        s->voiceManager->processNoteOffEvent(0, 0, key, -1, 0.9f);
        for (int i = 0; i < 48000; ++i) { if (bp == 0) s->process(nullptr); if (++bp == s->blockSize) bp = 0; }
        return o;
    }
};
static double rms(const std::vector<float> &v, size_t from = 4800)
{
    double a{0}; size_t n{0};
    for (size_t i = from; i < v.size(); ++i) { a += (double)v[i] * v[i]; n++; }
    return std::sqrt(a / std::max<size_t>(n, 1));
}
static bool finite(const std::vector<float> &v) { for (auto f : v) if (!std::isfinite(f)) return false; return true; }
// rising zero crossings per second (the pitch of a clean tone)
static double zc(const std::vector<float> &v)
{
    int c{0}; size_t from{9600};
    for (size_t i = from + 1; i < v.size(); ++i) if (v[i - 1] <= 0 && v[i] > 0) c++;
    return c * 48000.0 / (v.size() - from);
}
static double diff(const std::vector<float> &a, const std::vector<float> &b)
{
    double d{0}; for (size_t i = 4800; i < a.size(); ++i) d += std::fabs(a[i] - b[i]); return d / a.size();
}

int main()
{
    MatrixIndex::initialize();
    CHECK(numOps == 12 && matrixSize == 132, "twelve operators, %d links", (int)matrixSize);

    {   // ids: unique everywhere; the six-operator links keep upstream's ids
        Rig r; std::set<uint32_t> ids; bool uniq{true};
        for (auto *p : r.s->patch.params) uniq &= ids.insert(p->meta.id).second;
        CHECK(uniq, "%d parameters, every id unique", (int)ids.size());
        bool legacy{true};
        for (int i = 0; i < 15; ++i) legacy &= (r.s->patch.matrixNodes[i].level.meta.id == 30000u + 200u * i + 0u ||
                                               r.s->patch.matrixNodes[i].level.meta.id / 200 == (30000u + 200u * i) / 200);
        CHECK(legacy, "links 1-15 keep the Six Sines ids (old patches load)");
    }

    // each operator alone: sounds, finite, in tune (A3 = 220 Hz)
    for (int op = 0; op < (int)numOps; ++op)
    {
        Rig r; auto &p = r.s->patch;
        p.mixerNodes[op].active = 1; p.mixerNodes[op].level = 1;
        auto v = r.render(57, 48000);
        auto f = zc(v);
        CHECK(finite(v) && rms(v) > 0.01 && std::fabs(f - 220.0) < 2.0, "operator %2d alone: rms %.3f, %.1f Hz", op + 1, rms(v), f);
    }

    // every link, both ways, changes the sound of its target (and nothing blows up)
    {
        int bad{0}; double least{1e9};
        for (int t = 0; t < (int)numOps; ++t)
        {
            Rig base; base.s->patch.mixerNodes[t].active = 1; base.s->patch.mixerNodes[t].level = 1;
            auto dry = base.render(57, 24000);
            for (int s = 0; s < (int)numOps; ++s)
            {
                if (s == t) continue;
                Rig r; auto &p = r.s->patch;
                p.mixerNodes[t].active = 1; p.mixerNodes[t].level = 1;
                auto pos = MatrixIndex::positionForSourceTarget(s, t);
                p.matrixNodes[pos].active = 1; p.matrixNodes[pos].level = 0.5;
                auto wet = r.render(57, 24000);
                auto d = diff(dry, wet); least = std::min(least, d);
                if (!finite(wet) || d < 1e-3) { bad++; printf("   link %d -> %d: diff %.5f\n", s + 1, t + 1, d); }
            }
        }
        CHECK(bad == 0, "all 132 links modulate their target (smallest change %.4f)", least);
    }
    // a loop: 1 > 2 and 2 > 1 at once, and a ring of all twelve
    {
        Rig r; auto &p = r.s->patch;
        p.mixerNodes[0].active = 1; p.mixerNodes[0].level = 1;
        for (auto [a, b] : {std::pair{0, 1}, std::pair{1, 0}}) { auto &m = p.matrixNodes[MatrixIndex::positionForSourceTarget(a, b)]; m.active = 1; m.level = 0.6; }
        auto v = r.render(57, 48000);
        Rig q; auto &pq = q.s->patch;
        pq.mixerNodes[0].active = 1; pq.mixerNodes[0].level = 1;
        for (int i = 0; i < 12; ++i) { auto &m = pq.matrixNodes[MatrixIndex::positionForSourceTarget((i + 1) % 12, i)]; m.active = 1; m.level = 0.5; }
        auto w = q.render(57, 48000);
        CHECK(finite(v) && rms(v) > 0.01 && finite(w) && rms(w) > 0.01, "loops (1 <> 2, and a ring of all twelve) stay finite and sound");
    }

    // THROAT: every Surge filter in the catalog, on a bright operator (one with feedback)
    {
        const auto &cat = ghostblock::seance::throatCatalog();
        auto bright = [](Rig &r, int op) {
            auto &p = r.s->patch;
            p.mixerNodes[op].active = 1; p.mixerNodes[op].level = 1;
            p.selfNodes[op].active = 1; p.selfNodes[op].fbLevel = 0.6;
        };
        Rig dryRig; bright(dryRig, 0);
        auto dry = dryRig.render(45, 24000);
        int bad{0}, same{0};
        for (int k = 1; k < (int)cat.size(); ++k)
        {
            Rig r; bright(r, 0);
            r.s->patch.throatNodes[0].kind = (float)k;
            r.s->patch.throatNodes[0].cutoff = 10; r.s->patch.throatNodes[0].resonance = 0.5;
            auto wet = r.render(45, 24000);
            double pk{0}; for (auto f : wet) pk = std::max(pk, (double)std::fabs(f));
            if (!finite(wet) || pk > 8.0) { bad++; printf("   throat %d %s / %s: peak %.2f\n", k, cat[k].group.c_str(), cat[k].name.c_str(), pk); }
            if (diff(dry, wet) < 1e-4) { same++; printf("   throat %d %s / %s changes nothing\n", k, cat[k].group.c_str(), cat[k].name.c_str()); }
        }
        CHECK(cat.size() > 40 && bad == 0 && same == 0, "THROAT: all %d Surge filters run, stay finite and change the sound", (int)cat.size() - 1);
        // a 24 dB lowpass, closed: much quieter top end; the same seat at Off: exactly the dry sound
        int lp{-1};
        for (int k = 1; k < (int)cat.size(); ++k)
            if (cat[k].model == ghostblock::seance::sfpp::FilterModel::VemberClassic && cat[k].name == "Lowpass 24 dB Standard") { lp = k; break; }
        Rig a; bright(a, 0); a.s->patch.throatNodes[0].kind = (float)lp; a.s->patch.throatNodes[0].cutoff = -30; a.s->patch.throatNodes[0].keytrack = 0; a.s->patch.throatNodes[0].resonance = 0.1;
        auto closed = a.render(45, 24000);
        auto hf = [](const std::vector<float> &v) { double e{0}; for (size_t i = 4801; i < v.size(); ++i) e += std::fabs(v[i] - v[i - 1]); return e / v.size(); };
        Rig b; bright(b, 0); b.s->patch.throatNodes[0].kind = 0; b.s->patch.throatNodes[0].cutoff = -30;
        auto off = b.render(45, 24000);
        printf("   lp kind %d (%s / %s): rms dry %.4f closed %.4f, hf dry %.5f closed %.5f, off-vs-dry %.6f\n", lp, cat[lp].group.c_str(), cat[lp].name.c_str(), rms(dry), rms(closed), hf(dry), hf(closed), diff(off, dry));
        CHECK(lp > 0 && hf(closed) < 0.25 * hf(dry) && diff(off, dry) == 0.0, "THROAT: a closed 24 dB lowpass dulls the operator (%.4f -> %.4f); Off leaves it bit-identical", hf(dry), hf(closed));
    }

    // SHADE: Vital's wavetable oscillator in a seat
    {
        using SN = Patch::SourceNode;
        auto shadeRig = [](Rig &r, int op, std::initializer_list<float> knobs = {}) {
            auto &p = r.s->patch;
            p.mixerNodes[op].active = 1; p.mixerNodes[op].level = 1;
            p.sourceNodes[op].species = (float)SN::shadeSpecies;
            if (knobs.size())
            {
                int k = 0;
                for (auto v : knobs) p.sourceNodes[op].beastP[k++] = v;
                p.sourceNodes[op].beastSet = 1;
            }
        };
        Rig a; shadeRig(a, 3);
        auto v = a.render(57, 48000);
        CHECK(finite(v) && rms(v) > 0.05 && std::fabs(zc(v) - 220.0) < 3.0, "SHADE: a wavetable spirit plays in tune (rms %.3f, %.1f Hz)", rms(v), zc(v));
        // every table, every spectral morph, every warp, unison: finite, audible, and different from the plain table
        int bad{0}, same{0}, n{0};
        auto tryKnobs = [&](std::initializer_list<float> k, const std::vector<float> *ref) {
            Rig r; shadeRig(r, 3, k);
            auto w = r.render(57, 24000);
            ++n;
            double pk{0}; for (auto f : w) pk = std::max(pk, (double)std::fabs(f));
            if (!finite(w) || pk > 4.0) { bad++; printf("   shade setting %d: peak %.2f rms %.5f\n", n, pk, rms(w)); }
            if (ref && diff(*ref, w) < 1e-4) { same++; printf("   shade setting %d changes nothing\n", n); }
            return w;
        };
        auto plain = tryKnobs({0.f, 0.f, 0.f, 0.5f, 0.f, 0.5f, 0.f}, nullptr);
        for (int t = 1; t < shade_table_count(); ++t) tryKnobs({(float)t / (shade_table_count() - 1), 0.3f, 0.f, 0.5f, 0.f, 0.5f, 0.f}, &plain);
        tryKnobs({0.f, 1.f, 0.f, 0.5f, 0.f, 0.5f, 0.f}, &plain); // the other end of the table
        for (int m = 1; m < shade_param_steps(2); ++m) for (float am : {0.15f, 0.35f, 0.9f}) tryKnobs({0.f, 0.f, (float)m / (shade_param_steps(2) - 1), am, 0.f, 0.5f, 0.f}, nullptr);
        for (int w = 1; w < shade_param_steps(4); ++w) for (float am : {0.6f, 0.9f}) tryKnobs({0.f, 0.f, 0.f, 0.5f, (float)w / (shade_param_steps(4) - 1), am, 0.f}, &plain);
        tryKnobs({0.f, 0.f, 0.f, 0.5f, 0.f, 0.5f, 0.4f}, &plain); // unison 7
        CHECK(bad == 0 && same == 0, "SHADE: %d settings (8 tables, %d spectral morphs and %d warps at several amounts, unison) stay in range; tables, warps and unison change the sound", n, shade_param_steps(2) - 1, shade_param_steps(4) - 1);
        // threads: a sine spirit possesses the shade, the shade possesses a sine spirit, the shade's own feedback
        Rig b; shadeRig(b, 3);
        { auto &m = b.s->patch.matrixNodes[MatrixIndex::positionForSourceTarget(0, 3)]; m.active = 1; m.level = 0.5; }
        auto pmd = b.render(57, 24000);
        Rig c; c.s->patch.mixerNodes[5].active = 1; c.s->patch.mixerNodes[5].level = 1;
        auto sineDry = c.render(57, 24000);
        Rig d; d.s->patch.mixerNodes[5].active = 1; d.s->patch.mixerNodes[5].level = 1; d.s->patch.sourceNodes[3].species = (float)SN::shadeSpecies;
        { auto &m = d.s->patch.matrixNodes[MatrixIndex::positionForSourceTarget(3, 5)]; m.active = 1; m.level = 0.5; }
        auto sinePossessed = d.render(57, 24000);
        Rig e; shadeRig(e, 3); e.s->patch.selfNodes[3].active = 1; e.s->patch.selfNodes[3].fbLevel = 0.5;
        auto fb = e.render(57, 24000);
        auto ref = a.render(57, 24000);
        CHECK(finite(pmd) && diff(ref, pmd) > 1e-3 && finite(sinePossessed) && diff(sineDry, sinePossessed) > 1e-3 && finite(fb) && diff(ref, fb) > 1e-3,
              "SHADE: a sine spirit bends it, it bends a sine spirit, and its own feedback works");
        // cost
        Rig h; auto &hp = h.s->patch;
        for (int i = 0; i < 12; ++i) { hp.mixerNodes[i].active = 1; hp.mixerNodes[i].level = 0.2; hp.sourceNodes[i].species = (float)SN::shadeSpecies; }
        for (int k = 0; k < 8; ++k) h.s->voiceManager->processNoteOnEvent(0, 0, 40 + 3 * k, -1, 0.9f, 0.f);
        auto t0 = std::chrono::steady_clock::now();
        size_t bp{0};
        for (int i = 0; i < 48000 * 2; ++i) { if (bp == 0) h.s->process(nullptr); if (++bp == h.s->blockSize) bp = 0; }
        printf("cpu: twelve shades x 8 voices use %.0f %% of one core here\n", 100.0 * std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / 2.0);
    }

    // SHADE tables from Vital wavetable files (built by Vital's own WavetableCreator), kept in the patch
    {
        using SN = Patch::SourceNode;
        // a table of our own in Vital's file format: a ramp whose corner moves across the cycle
        const std::string mine = R"({"groups":[{"components":[{"keyframes":[{"num_points":3,"points":[0.0,1.0,0.25,-1.0,1.0,1.0],"position":0,"powers":[0.0,0.0,0.0]},{"num_points":3,"points":[0.0,1.0,0.85,-1.0,1.0,1.0],"position":256,"powers":[0.0,0.0,0.0]}],"num_points":3,"type":1}]}]})";
        auto play = [&](const std::string &js, std::vector<float> &out, int &frames) {
            Rig r; auto &p = r.s->patch;
            p.mixerNodes[2].active = 1; p.mixerNodes[2].level = 1; p.sourceNodes[2].species = (float)SN::shadeSpecies;
            r.s->patchMain.setShadeTable(2, "t", js);
            r.s->reconcileWavetables();
            frames = shade_slot_user_frames(r.s->monoValues.shadeWorld, 2);
            out = r.render(57, 36000);
        };
        std::vector<float> builtIn, own; int f0{0}, f1{0};
        play("", builtIn, f0);
        play(mine, own, f1);
        CHECK(f0 == 0 && f1 == 257 && finite(own) && rms(own) > 0.01 && std::fabs(zc(own) - 220.0) < 3.0 && diff(builtIn, own) > 1e-3,
              "SHADE: a table in Vital's file format loads (%d frames), plays in tune (%.1f Hz) and differs from the built-in one", f1, zc(own));
        std::vector<float> junk; int fj{-1};
        play("{ this is not a table", junk, fj);
        CHECK(fj == 0 && finite(junk) && diff(builtIn, junk) < 1e-6, "SHADE: text that is not a table is refused and the seat stays on its built-in table");
        // the table travels in the patch: save, load into a fresh engine, same sound
        {
            Rig a; a.s->patch.mixerNodes[2].active = 1; a.s->patch.mixerNodes[2].level = 1; a.s->patch.sourceNodes[2].species = (float)SN::shadeSpecies;
            a.s->patchMain.copyValuesFrom(a.s->patch);
            a.s->patchMain.setShadeTable(2, "My Ramp", mine);
            const auto xml = a.s->patchMain.toState();
            Rig b;
            const bool ok = b.s->patchMain.fromState(xml);
            b.s->patch.copyValuesFrom(b.s->patchMain);
            b.s->reconcileWavetables();
            auto again = b.render(57, 36000);
            CHECK(ok && b.s->patchMain.shadeTables[2].name == "My Ramp" && shade_slot_user_frames(b.s->monoValues.shadeWorld, 2) == 257 && diff(own, again) < 1e-6,
                  "SHADE: the table is saved inside the sound and comes back identical in a fresh engine");
        }
        // a .wav wavetable (made here: 8 cycles of 2048, 16-bit, a sine growing a third harmonic) loads the same way
        {
            const int cyc = 2048, nf = 8;
            std::vector<unsigned char> wav(44 + (size_t)cyc * nf * 2, 0);
            auto w32 = [&](size_t o, uint32_t v) { for (int i = 0; i < 4; ++i) wav[o + i] = (unsigned char)(v >> (8 * i)); };
            auto w16 = [&](size_t o, uint16_t v) { wav[o] = (unsigned char)v; wav[o + 1] = (unsigned char)(v >> 8); };
            memcpy(&wav[0], "RIFF", 4); w32(4, (uint32_t)wav.size() - 8); memcpy(&wav[8], "WAVEfmt ", 8); w32(16, 16); w16(20, 1); w16(22, 1);
            w32(24, 44100); w32(28, 88200); w16(32, 2); w16(34, 16); memcpy(&wav[36], "data", 4); w32(40, (uint32_t)cyc * nf * 2);
            for (int f = 0; f < nf; ++f)
                for (int i = 0; i < cyc; ++i)
                {
                    const double ph = 2 * 3.14159265358979 * i / cyc, v = 0.6 * std::sin(ph) + 0.35 * (double)f / (nf - 1) * std::sin(3 * ph);
                    w16(44 + ((size_t)f * cyc + i) * 2, (uint16_t)(int16_t)std::lround(v * 32767));
                }
            int frames{0};
            char *txt = shade_wav_to_vitaltable(wav.data(), wav.size(), "Test Wav", &frames);
            std::vector<float> o; int fr{0};
            if (txt) play(txt, o, fr);
            char *bad = shade_wav_to_vitaltable("not a wav file at all, just some text to be long enough", 56, "x", nullptr);
            CHECK(txt && frames == 8 && fr == 257 && finite(o) && rms(o) > 0.01 && std::fabs(zc(o) - 220.0) < 3.0 && !bad,
                  "SHADE: a .wav wavetable (%d cycles) becomes a table, loads and plays in tune (%.1f Hz); junk is refused", frames, o.empty() ? 0.0 : zc(o));
            shade_text_free(txt);
        }
        // every .vitaltable in a folder given by SEANCE_VITAL_TABLES (the owner's own Vital tables, when testing with them)
        if (const char *dir = getenv("SEANCE_VITAL_TABLES"))
        {
            int n{0}, bad{0};
            for (auto &e : fs::recursive_directory_iterator(dir))
            {
                if (e.path().extension() != ".vitaltable") continue;
                std::ifstream f(e.path()); std::stringstream ss; ss << f.rdbuf();
                std::vector<float> o; int fr{0};
                play(ss.str(), o, fr);
                double pk{0}; for (auto v : o) pk = std::max(pk, (double)std::fabs(v));
                ++n;
                if (fr <= 0 || !finite(o) || pk > 4.0 || rms(o) < 1e-4) { ++bad; }
                printf("   %-28s frames %3d  rms %.3f  peak %.2f\n", e.path().stem().string().c_str(), fr, rms(o), pk);
            }
            CHECK(n > 0 && bad == 0, "SHADE: all %d Vital wavetable files in that folder load and play", n);
        }
    }

    // MOVE: an operator's own envelope and LFO move its throat's cutoff and a shade's FRAME / MORPH / WARP, per note
    {
        using SN = Patch::SourceNode;
        auto base = [](Rig &r, bool shade) {
            auto &p = r.s->patch;
            p.mixerNodes[0].active = 1; p.mixerNodes[0].level = 1;
            if (shade) { p.sourceNodes[0].species = (float)SN::shadeSpecies; p.sourceNodes[0].beastSet = 1; for (int k = 0; k < 7; ++k) p.sourceNodes[0].beastP[k] = shade_param_default01(k); p.sourceNodes[0].beastP[2] = 6.f / 9.f; p.sourceNodes[0].beastP[4] = 1.f / 6.f; }
            else { p.selfNodes[0].active = 1; p.selfNodes[0].fbLevel = 0.6; p.throatNodes[0].kind = 75; p.throatNodes[0].cutoff = 0; p.throatNodes[0].resonance = 0.4; }
            p.sourceNodes[0].lfoRate = 3.0; p.sourceNodes[0].envPower = 1; p.sourceNodes[0].attack = 0.5; p.sourceNodes[0].sustain = 1.0;
        };
        Rig a; base(a, false); auto still = a.render(45, 48000);
        Rig b; base(b, false); b.s->patch.throatNodes[0].lfoToCutoff = 30; auto lfo = b.render(45, 48000);
        Rig c; base(c, false); c.s->patch.throatNodes[0].envToCutoff = 40; auto envd = c.render(45, 48000);
        CHECK(finite(lfo) && finite(envd) && diff(still, lfo) > 1e-3 && diff(still, envd) > 1e-3, "MOVE: the LFO and the envelope each move a throat's cutoff (off = unchanged)");
        int bad{0};
        Rig d; base(d, true); auto sStill = d.render(57, 48000);
        for (int tg = 0; tg < 3; ++tg)
        {
            Rig e; base(e, true); e.s->patch.throatNodes[0].bodyTarget = (float)tg; e.s->patch.throatNodes[0].lfoToBody = 0.5; auto l = e.render(57, 48000);
            Rig f; base(f, true); f.s->patch.throatNodes[0].bodyTarget = (float)tg; f.s->patch.throatNodes[0].envToBody = 0.6; auto en = f.render(57, 48000);
            if (!finite(l) || !finite(en) || diff(sStill, l) < 1e-3 || diff(sStill, en) < 1e-3) { ++bad; printf("   body target %d: lfo %.5f env %.5f\n", tg, diff(sStill, l), diff(sStill, en)); }
        }
        CHECK(bad == 0, "MOVE: the LFO and the envelope each move a shade's FRAME, MORPH and WARP");
        // a Surge spirit: the same two sweeps push one of its own knobs (Classic's first knob here)
        auto beastBase = [](Rig &r) {
            auto &p = r.s->patch;
            p.mixerNodes[0].active = 1; p.mixerNodes[0].level = 1;
            p.sourceNodes[0].species = 1; p.sourceNodes[0].lfoRate = 3.0; p.sourceNodes[0].envPower = 1; p.sourceNodes[0].attack = 0.5; p.sourceNodes[0].sustain = 1.0;
        };
        Rig g; beastBase(g); auto bStill = g.render(57, 48000);
        Rig h; beastBase(h); h.s->patch.throatNodes[0].bodyTarget = 0; h.s->patch.throatNodes[0].lfoToBody = 0.5; auto bl = h.render(57, 48000);
        Rig k; beastBase(k); k.s->patch.throatNodes[0].bodyTarget = 0; k.s->patch.throatNodes[0].envToBody = 0.6; auto be = k.render(57, 48000);
        CHECK(finite(bl) && finite(be) && rms(bStill) > 0.01 && diff(bStill, bl) > 1e-3 && diff(bStill, be) > 1e-3, "MOVE: the LFO and the envelope each push a Surge spirit's knob (lfo %.4f env %.4f)", diff(bStill, bl), diff(bStill, be));
    }

    if (getenv("SEANCE_BENCH"))
    { // how much of a shade's cost is per call (the engine asks for 8 samples at a time)?
        auto *w = shade_world_create();
        shade_world_set_rate(w, 48000);
        float p7[7]; for (int k = 0; k < 7; ++k) p7[k] = shade_param_default01(k);
        for (int uni : {1, 5})
          for (int morph : {0, 1})
          {
            p7[6] = (float)(uni - 1) / 15.f; p7[2] = morph ? 6.f / 9.f : 0.f;
            shade_slot_set_params01(w, 0, p7);
            for (int n : {8, 16, 32, 64})
            {
                auto *o = shade_osc_create(w, 0);
                shade_osc_start(o, 57.f);
                float out[256]; float mv[3]{0, 0, 0};
                const auto t0 = std::chrono::steady_clock::now();
                for (int i = 0; i < 48000 * 10 / n; ++i) shade_osc_render(o, 57.f, nullptr, mv, out, n);
                const double t = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
                std::printf("   bench unison %d morph %d block %2d: %.2f %% of a core per voice\n", uni, morph, n, t / 10.0 * 100.0);
                shade_osc_destroy(o);
            }
          }
        shade_world_destroy(w);
    }

    // THROAT: the comb filters, added after every other filter so saved sounds keep their filter
    {
        const auto &cat = ghostblock::seance::throatCatalog();
        int firstComb = -1, combs = 0, early = 0;
        for (int k = 1; k < (int)cat.size(); ++k)
        {
            const bool d = ghostblock::seance::sfpp::Filter::requiredDelayLinesSizes(cat[(size_t)k].model, cat[(size_t)k].cfg) != 0;
            if (d) { ++combs; if (firstComb < 0) firstComb = k; }
            else if (firstComb >= 0) ++early;
        }
        auto base = [](Rig &r) {
            auto &p = r.s->patch;
            p.mixerNodes[0].active = 1; p.mixerNodes[0].level = 0.6; p.mixerNodes[0].release = 0.2;
            p.selfNodes[0].active = 1; p.selfNodes[0].fbLevel = 0.3; // (some harmonics for the comb to work on)
        };
        {
            ghostblock::seance::Throat t;
            t.configure(firstComb, 48000, 8);
            float buf[8] = {1, 0, 0, 0, 0, 0, 0, 0};
            if (t.ok) t.process(buf, 8, 0.f, 0.6f);
            std::printf("   comb debug: line size %zu, has line %d, ok %d, first out %.4f, name %s / %s\n", ghostblock::seance::throatLineSize(), (int)(t.line != nullptr), (int)t.ok, buf[0],
                        cat[(size_t)firstComb].group.c_str(), cat[(size_t)firstComb].name.c_str());
        }
        Rig a; base(a); auto dry = a.render(57, 48000);
        bool allOk = combs > 0;
        double d1 = 0;
        for (int k = firstComb; k > 0 && k < (int)cat.size(); ++k)
        {
            Rig b; base(b);
            b.s->patch.throatNodes[0].kind = (float)k; b.s->patch.throatNodes[0].cutoff = 0; b.s->patch.throatNodes[0].resonance = 0.6;
            auto y = b.render(57, 48000);
            float pk = 0; for (auto v : y) pk = std::max(pk, std::fabs(v));
            const double d = diff(dry, y);
            if (k == firstComb) d1 = d;
            allOk = allOk && finite(y) && d > 1e-3 && pk < 8.f;
        }
        CHECK(allOk && firstComb == 215 && early == 0, "THROAT: %d comb filters work (each changes the tone, stays finite), and they start at %d, after all 214 older filters (diff %.3f)", combs, firstComb, d1);
    }
    // the output guard and Seance's own host parameters
    {
        using namespace ghostblock::seance;
        OutGuard g; g.setRate(48000);
        std::vector<float> L(4800), R(4800), L0;
        for (size_t i = 0; i < L.size(); ++i) { L[i] = 0.5f * (float)std::sin(i * 0.05); R[i] = 0.4f * (float)std::cos(i * 0.05); }
        L0 = L;
        g.process(L.data(), R.data(), (uint32_t)L.size());
        const bool untouched = L == L0 && g.reduction.load() == 1.f && std::fabs(g.peakL.load() - 0.5f) < 0.01f;
        for (size_t i = 0; i < L.size(); ++i) { L[i] = 2.5f * (float)std::sin(i * 0.05); R[i] = 1.5f * (float)std::cos(i * 0.05); }
        L[100] = std::numeric_limits<float>::quiet_NaN();
        g.process(L.data(), R.data(), (uint32_t)L.size());
        float pk = 0; bool fin = true;
        for (size_t i = 0; i < L.size(); ++i) { fin = fin && std::isfinite(L[i]) && std::isfinite(R[i]); pk = std::max({pk, std::fabs(L[i]), std::fabs(R[i])}); }
        const bool held = fin && pk <= OutGuard::kCeiling + 1e-4f && g.reduction.load() < 0.5f && g.peakL.load() > 2.f;
        g.on = false;
        for (size_t i = 0; i < L.size(); ++i) L[i] = 2.f;
        g.process(L.data(), nullptr, 16);
        StepSeq q;
        HostParams::set(HostParams::pStepsOn, 1, q, g); HostParams::set(HostParams::pStepsSwing, 0.3, q, g); HostParams::set(HostParams::pStepsOctave, -2, q, g); HostParams::set(HostParams::pLimiter, 1, q, g);
        char txt[32]; clap_param_info inf;
        const bool hp = q.on.load() && std::fabs(q.swing.load() - 0.3f) < 1e-6f && q.octave.load() == -2 && g.on.load() && HostParams::get(HostParams::pStepsOctave, q, g) == -2 &&
                        HostParams::toText(HostParams::pStepsSwing, 0.375, txt, 32) && std::string(txt) == "50 %" && HostParams::info(6, &inf) && inf.id == HostParams::pLimiter && !HostParams::info(7, &inf);
        CHECK(untouched && held && L[0] == 2.f && hp, "the output limiter leaves quiet sound bit-for-bit, holds loud sound under the ceiling (peak %.3f), passes everything when off; STEPS and the limiter read and write as host parameters", pk);
    }
    {   // a ladder note below the held chord, and a note a semitone off its chord tone (the follow-my-chords notes)
        using namespace ghostblock::seance;
        auto pat = std::make_unique<StepPattern>();
        pat->loopTicks = 96; pat->ladder = 1;
        pat->add(0, 20, 0 - 64, 100, StepNote::packLane(3, 0));  // the lowest held note, an octave down, on the bass part
        pat->add(24, 20, 1, 100, StepNote::packLane(2, -1));     // the second held note, a semitone under, on the melody part
        pat->add(48, 20, 2 + 64, 100, StepNote::packLane(1, 2)); // the third, an octave up and a tone over
        StepSeq q; q.on = true; q.setPattern(*pat);
        StepSeq::KeyEv kv[3] = {{0, 60, 0.9f, true}, {0, 64, 0.9f, true}, {0, 67, 0.9f, true}};
        StepSeq::Out o[64]; std::vector<int> ks;
        int n = q.block(kv, 3, 256, 48000, 120, false, 0, o, 64);
        for (int i = 0; i < n; ++i) if (o[i].down) ks.push_back(o[i].key);
        for (int b = 0; b < 80; ++b) { n = q.block(nullptr, 0, 256, 48000, 120, false, 0, o, 64); for (int i = 0; i < n; ++i) if (o[i].down) ks.push_back(o[i].key); }
        StepSeq r; r.fromState(q.toState());
        StepPattern back; r.getPattern(back);
        CHECK(ks.size() >= 3 && ks[0] == 48 && ks[1] == 63 && ks[2] == 81 && back.n == 3 && back.notes[1].nudge() == -1 && back.notes[0].laneOf() == 3 && back.notes[0].off == -64,
              "STEPS: ladder notes an octave under the chord and a semitone off a chord tone play and save (%d %d %d)", ks.size() > 0 ? ks[0] : -1, ks.size() > 1 ? ks[1] : -1, ks.size() > 2 ? ks[2] : -1);
    }

    // STEPS: the note sequencer in front of the engine
    {
        using namespace ghostblock::seance;
        auto pat = std::make_unique<StepPattern>();
        pat->loopTicks = 384; // one bar: four quarter notes, offsets 0 3 7 12
        pat->add(0, 48, 0, 100, 1); pat->add(96, 48, 3, 100, 1); pat->add(192, 48, 7, 100, 1); pat->add(288, 48, 12, 100, 1);
        struct Ev { long t; int key; bool down; };
        // run `seconds` of 256-sample buffers at 120 bpm / 48 kHz; keys = (sample, key, down)
        auto runSeq = [](StepSeq &q, const std::vector<Ev> &keys, double seconds, bool hostPlaying, double beat0, std::vector<Ev> &got) {
            const double sr = 48000, bpm = 120;
            long t = 0;
            size_t ki = 0;
            while (t < (long)(seconds * sr))
            {
                StepSeq::KeyEv kv[16]; int nk = 0;
                while (ki < keys.size() && keys[ki].t < t + 256 && nk < 16) { kv[nk++] = {(uint32_t)(keys[ki].t - t), (int16_t)keys[ki].key, 0.9f, keys[ki].down}; ++ki; }
                StepSeq::Out o[256];
                const int n = q.block(kv, nk, 256, sr, bpm, hostPlaying, beat0 + (double)t / sr * bpm / 60.0, o, 256);
                for (int i = 0; i < n; ++i) got.push_back({t + (long)o[i].time, o[i].key, o[i].down});
                t += 256;
            }
        };
        {
            StepSeq q; q.on = true; q.setPattern(*pat);
            std::vector<Ev> got;
            runSeq(q, {{100, 57, true}, {100 + 110000, 57, false}}, 3.0, false, 0, got); // held for a little over 4.5 beats
            std::vector<Ev> ons, offs;
            for (auto &e : got) (e.down ? ons : offs).push_back(e);
            bool ok = ons.size() == 5 && offs.size() == 5;
            const int want[5] = {57, 60, 64, 69, 57};
            for (size_t i = 0; ok && i < 5; ++i) ok = ons[i].key == want[i] && std::labs(ons[i].t - (100 + (long)i * 24000)) <= 2;
            for (size_t i = 0; ok && i < 4; ++i) ok = std::labs(offs[i].t - (100 + (long)i * 24000 + 12000)) <= 2; // an eighth long
            ok = ok && std::labs(offs[4].t - (100 + 108000)) <= 2 && !q.running.load();
            CHECK(ok, "STEPS: one held key plays the pattern in that key, a quarter apart at the host's tempo, loops, and stops with the key (%zu on, %zu off)", ons.size(), offs.size());
        }
        {   // the song is playing: the pattern sits on the song's grid wherever you press
            StepSeq q; q.on = true; q.setPattern(*pat);
            std::vector<Ev> got;
            runSeq(q, {{30000, 60, true}}, 2.0, true, 0.0, got); // pressed a quarter of a beat into beat 2 (beat = 24000 samples)
            std::vector<Ev> ons;
            for (auto &e : got) if (e.down) ons.push_back(e);
            const bool ok = ons.size() >= 2 && ons[0].key == 67 && std::labs(ons[0].t - 48000) <= 3 && ons[1].key == 72 && std::labs(ons[1].t - 72000) <= 3;
            CHECK(ok, "STEPS: with the host playing, a key pressed mid-beat joins on the song's grid (first note at %ld, wanted 48000)", ons.empty() ? -1L : ons[0].t);
        }
        {   // LATCH, a second key moves it, off ends everything
            StepSeq q; q.on = true; q.latch = true; q.setPattern(*pat);
            std::vector<Ev> got;
            runSeq(q, {{0, 57, true}, {1000, 57, false}, {60000, 62, true}, {61000, 62, false}}, 2.0, false, 0, got);
            int after = 0; bool moved = false;
            for (auto &e : got) if (e.down && e.t > 61000) { ++after; moved = moved || e.key == 62 + 12 || e.key == 62 || e.key == 65 || e.key == 69; }
            q.on = false;
            std::vector<Ev> tail;
            runSeq(q, {}, 0.05, false, 0, tail);
            int downs = 0, ups = 0;
            for (auto &e : got) (e.down ? downs : ups)++;
            for (auto &e : tail) (e.down ? downs : ups)++;
            CHECK(after >= 1 && moved && downs == ups && !q.busy(), "STEPS: LATCH keeps playing after the key is let go, a new key moves the pattern, switching off ends every note (%d on / %d off)", downs, ups);
        }
        {   // a ladder pattern plays the notes you hold; lanes can be muted; the state round-trips
            auto lad = std::make_unique<StepPattern>();
            lad->loopTicks = 96; lad->ladder = 1;
            for (int i = 0; i < 4; ++i) lad->add(i * 24, 20, i, 110, 1);
            lad->add(0, 96, 0, 90, 3);
            lad->setName("Up over what you hold");
            StepSeq q; q.on = true; q.setPattern(*lad); q.laneMask = 0x2; // arp only
            std::vector<Ev> got;
            runSeq(q, {{0, 60, true}, {0, 64, true}, {0, 67, true}, {30000, 60, false}, {30000, 64, false}, {30000, 67, false}}, 1.0, false, 0, got);
            std::vector<int> ks;
            for (auto &e : got) if (e.down) ks.push_back(e.key);
            const bool up = ks.size() >= 5 && ks[0] == 60 && ks[1] == 64 && ks[2] == 67 && ks[3] == 72 && ks[4] == 60;
            StepSeq r;
            const bool back = r.fromState(q.toState());
            StepPattern a, b;
            q.getPattern(a); r.getPattern(b);
            const bool same = back && a.n == b.n && a.loopTicks == b.loopTicks && a.ladder == b.ladder && std::string(a.name) == b.name && r.laneMask.load() == 0x2 && r.on.load() &&
                              std::memcmp(a.notes, b.notes, sizeof(StepNote) * (size_t)a.n) == 0;
            StepSeq z;
            CHECK(up && same && !z.fromState("garbage"), "STEPS: a ladder pattern climbs the held notes then up an octave, a muted lane stays silent, the state round-trips, junk is refused");
        }
    }

    // FOLLOWS: a macro (the engine's own modulation slots) pushes a spirit's volume, feedback, pitch and a thread's depth
    {
        auto base = [](Rig &r) {
            auto &p = r.s->patch;
            p.mixerNodes[0].active = 1; p.mixerNodes[0].level = 0.5;
            p.selfNodes[0].active = 1; p.selfNodes[0].fbLevel = 0.2;
            p.matrixNodes[MatrixIndex::positionForSourceTarget(1, 0)].active = 1; p.matrixNodes[MatrixIndex::positionForSourceTarget(1, 0)].level = 0.2;
            p.macroNodes[0].level = 1.0;
        };
        Rig a; base(a); auto still = a.render(57, 24000);
        Rig b; base(b); { auto &n = b.s->patch.mixerNodes[0]; n.modsource[0] = 400; n.modtarget[0] = 10; n.moddepth[0] = 0.5; } auto vol = b.render(57, 24000);
        Rig c; base(c); { auto &n = c.s->patch.selfNodes[0]; n.modsource[0] = 400; n.modtarget[0] = 10; n.moddepth[0] = 0.5; } auto fb = c.render(57, 24000);
        Rig d; base(d); { auto &n = d.s->patch.sourceNodes[0]; n.modsource[0] = 400; n.modtarget[0] = 11; n.moddepth[0] = 0.5; } auto pit = d.render(57, 24000);
        Rig e; base(e); { auto &n = e.s->patch.matrixNodes[MatrixIndex::positionForSourceTarget(1, 0)]; n.modsource[0] = 400; n.modtarget[0] = 10; n.moddepth[0] = 0.5; } auto dep = e.render(57, 24000);
        Rig f; base(f); f.s->patch.macroNodes[0].level = 0.0; { auto &n = f.s->patch.mixerNodes[0]; n.modsource[0] = 400; n.modtarget[0] = 10; n.moddepth[0] = 0.5; } auto zero = f.render(57, 24000);
        CHECK(finite(vol) && finite(fb) && finite(pit) && finite(dep) && rms(vol) > rms(still) * 1.1 && diff(still, fb) > 1e-3 && diff(still, pit) > 1e-3 && diff(still, dep) > 1e-3 && diff(still, zero) < 1e-5,
              "FOLLOWS: a macro pushes volume (%.3f > %.3f), feedback, pitch and a thread's depth; at zero it changes nothing", rms(vol), rms(still));
    }

    // SUMMON: sounds made by rule, each played and trimmed before it is handed over
    {
        ghostblock::seance::Summoner sm;
        sm.rng.seed(20261115u);
        Patch P;
        int made = 0, lost = 0, sang = 0;
        float lo = 9, hi = 0;
        const auto t0 = std::chrono::steady_clock::now();
        for (int style = 0; style < ghostblock::seance::Summoner::kStyles; ++style)
            for (int k = 0; k < 8; ++k)
            {
                const auto nm = sm.summon(P, style);
                if (nm.empty()) { ++lost; continue; }
                ++made;
                lo = std::min(lo, sm.lastPeak); hi = std::max(hi, sm.lastPeak);
                int heard = 0, on = 0;
                for (size_t o = 0; o < numOps; ++o) { heard += P.mixerNodes[o].active.value > 0.5f; on += P.sourceNodes[o].active.value > 0.5f; }
                if (heard >= 1 && on >= 2 && std::string(P.name) == nm) ++sang;
            }
        std::printf("   SUMMON: %.2f s per sound on this machine\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / 48.0);
        {   // MUTATE: the last summoned sound, nudged ten times, keeps playing
            int ok = 0;
            for (int k = 0; k < 10; ++k) ok += sm.mutate(P, k % 2 ? 1.f : 0.35f) ? 1 : 0;
            CHECK(ok >= 8, "MUTATE: %d of 10 nudges of a summoned sound still play, in level (last peak %.2f)", ok, sm.lastPeak);
        }
        {   // change one thing only; new voices; summoning with table files on offer
            using SM = ghostblock::seance::Summoner;
            sm.summon(P, SM::PAD);
            auto snap = [&] { std::vector<float> v; for (auto *pp : P.params) v.push_back(pp->value); return v; };
            const auto before = snap();
            const bool ok1 = sm.mutate(P, 0.8f, SM::DEPTHS);
            const auto after = snap();
            int depthMoved = 0, otherMoved = 0;
            for (size_t i = 0; i < P.params.size(); ++i)
                if (std::fabs(after[i] - before[i]) > 1e-6f)
                {
                    bool isDepth = false, isLevel = false;
                    for (auto &m : P.matrixNodes) isDepth = isDepth || P.params[i] == &m.level;
                    for (auto &m : P.mixerNodes) isLevel = isLevel || P.params[i] == &m.level;
                    isLevel = isLevel || P.params[i] == &P.output.level; // (the re-levelling afterwards)
                    if (isDepth) ++depthMoved; else if (!isLevel) ++otherMoved;
                }
            int ratioSame = 1;
            const bool ok2 = sm.revoice(P);
            const auto voiced = snap();
            for (size_t o = 0; o < numOps; ++o)
                for (size_t i = 0; i < P.params.size(); ++i)
                    if (P.params[i] == &P.sourceNodes[o].ratio && std::fabs(voiced[i] - after[i]) > 1e-6f) ratioSame = 0;
            int handed = 0;
            sm.pickTable = [&](std::string &nm, std::string &js) { ++handed; nm = "t"; js = ""; return false; }; // (asked, has none to give)
            for (int k = 0; k < 6; ++k) sm.summon(P, SM::PAD);
            sm.pickTable = nullptr;
            CHECK(ok1 && depthMoved > 0 && otherMoved == 0 && ok2 && ratioSame && handed > 0,
                  "MUTATE one thing: only thread depths moved (%d, others %d); NEW VOICES keeps the tuning; SUMMON asks for table files when they are on offer (%d asks)", depthMoved, otherMoved, handed);
        }
        CHECK(made >= 46 && sang == made && lo > 0.12f && hi < 0.95f, "SUMMON: %d of 48 sounds made across the six characters (%d given up), every one plays, peaks %.2f - %.2f", made, lost, lo, hi);
    }

    // feedback on every operator
    {
        int bad{0};
        for (int op = 0; op < (int)numOps; ++op)
        {
            Rig a; a.s->patch.mixerNodes[op].active = 1; a.s->patch.mixerNodes[op].level = 1;
            auto dry = a.render(57, 24000);
            Rig r; r.s->patch.mixerNodes[op].active = 1; r.s->patch.mixerNodes[op].level = 1;
            r.s->patch.selfNodes[op].active = 1; r.s->patch.selfNodes[op].fbLevel = 0.5;
            auto wet = r.render(57, 24000);
            if (!finite(wet) || diff(dry, wet) < 1e-3) bad++;
        }
        CHECK(bad == 0, "feedback works on all twelve operators");
    }

    // BEAST: a seat whose tone is one of Surge XT's oscillators
    if (beast_type_count() >= Patch::SourceNode::numBeastTypes)
    {
        for (int sp = 1; sp <= Patch::SourceNode::numBeastTypes; ++sp)
        {
            auto mk = [&](int link, float fb, int ebs) {
                auto r = std::make_unique<Rig>(); auto &p = r->s->patch;
                p.output.engineBlockSize = (float)ebs;
                p.sourceNodes[2].species = (float)sp;                 // seat 3 is the beast
                p.mixerNodes[2].active = 1; p.mixerNodes[2].level = 1;
                if (link == 1) { auto &m = p.matrixNodes[MatrixIndex::positionForSourceTarget(0, 2)]; m.active = 1; m.level = 0.5; } // a sine onto the beast
                if (link == 2) { auto &m = p.matrixNodes[MatrixIndex::positionForSourceTarget(7, 2)]; m.active = 1; m.level = 0.5; } // a later seat onto it
                if (fb > 0) { p.selfNodes[2].active = 1; p.selfNodes[2].fbLevel = fb; }
                r->s->reapplyControlSettings(); // the engine block size takes effect (between notes)
                for (int k = 0; k < 64; ++k) r->s->process(nullptr);
                return r;
            };
            auto dry = mk(0, 0, 0)->render(57, 48000);
            auto fm = mk(1, 0, 0)->render(57, 48000);
            auto fm2 = mk(2, 0, 0)->render(57, 48000);
            auto fb = mk(0, 0.5f, 0)->render(57, 48000);
            auto b16 = mk(1, 0, 1)->render(57, 48000);
            auto b32 = mk(1, 0, 2)->render(57, 48000);
            const bool ok = finite(dry) && finite(fm) && finite(fm2) && finite(fb) && finite(b16) && finite(b32) && rms(dry) > 0.004 && rms(b16) > 0.001 && rms(b32) > 0.001 && // (String is struck with noise: its level differs run to run)
                           
                            diff(dry, fm) > 1e-3 && diff(dry, fm2) > 1e-3 && diff(dry, fb) > 1e-3;
            CHECK(ok, "beast %-8s rms %.3f (peak-safe), %.0f Hz; a link onto it moves it %.3f, a later seat %.3f, feedback %.3f; engine blocks 16 / 32 rms %.3f / %.3f",
                  Patch::SourceNode::speciesName(sp), rms(dry), zc(dry), diff(dry, fm), diff(dry, fm2), diff(dry, fb), rms(b16), rms(b32));
            // and the beast possessing a sine
            Rig a; a.s->patch.mixerNodes[5].active = 1; a.s->patch.mixerNodes[5].level = 1; a.s->patch.sourceNodes[5].active = 1;
            auto sdry = a.render(57, 24000);
            Rig b; b.s->patch.mixerNodes[5].active = 1; b.s->patch.mixerNodes[5].level = 1; b.s->patch.sourceNodes[5].active = 1;
            b.s->patch.sourceNodes[2].species = (float)sp;
            { auto &m = b.s->patch.matrixNodes[MatrixIndex::positionForSourceTarget(2, 5)]; m.active = 1; m.level = 0.5; }
            auto swet = b.render(57, 24000);
            CHECK(finite(swet) && diff(sdry, swet) > 1e-3, "beast %-8s possessing a sine operator moves it %.3f", Patch::SourceNode::speciesName(sp), diff(sdry, swet));
        }
        // twelve beasts at once, all linked in a ring, 8 voices
        Rig r; auto &p = r.s->patch;
        for (int i = 0; i < 12; ++i)
        {
            p.sourceNodes[i].species = (float)(1 + i % Patch::SourceNode::numBeastTypes);
            p.mixerNodes[i].active = 1; p.mixerNodes[i].level = 0.2;
            auto &m = p.matrixNodes[MatrixIndex::positionForSourceTarget(i, (i + 1) % 12)]; m.active = 1; m.level = 0.3;
        }
        for (int k = 0; k < 8; ++k) r.s->voiceManager->processNoteOnEvent(0, 0, 40 + 3 * k, -1, 0.9f, 0.f);
        auto t0 = std::chrono::steady_clock::now();
        bool fin{true}; double pk{0}; size_t bp{0}; int n = 48000 * 2;
        for (int i = 0; i < n; ++i)
        {
            if (bp == 0) r.s->process(nullptr);
            auto f = r.s->output[0][bp]; fin &= std::isfinite(f); pk = std::max(pk, (double)std::fabs(f));
            if (++bp == r.s->blockSize) bp = 0;
        }
        auto dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        CHECK(fin && pk > 0.01, "twelve beasts in a ring, 8 voices: finite, peak %.2f", pk);
        printf("cpu: twelve beasts x 8 voices use %.0f %% of one core here\n", 100.0 * dt / 2.0);
    }
    else
        printf("note: BEAST is not in this build (no Surge object): beast checks skipped\n");

    // the worst case: everything on, everything connected, 8 voices
    {
        Rig r; auto &p = r.s->patch;
        for (auto &m : p.mixerNodes) { m.active = 1; m.level = 0.3; }
        for (auto &m : p.matrixNodes) { m.active = 1; m.level = 0.2; }
        for (auto &m : p.selfNodes) { m.active = 1; m.fbLevel = 0.2; }
        for (int k = 0; k < 8; ++k) r.s->voiceManager->processNoteOnEvent(0, 0, 40 + 3 * k, -1, 0.9f, 0.f);
        auto t0 = std::chrono::steady_clock::now();
        bool fin{true}; double pk{0}; size_t bp{0}; int n = 48000 * 4;
        for (int i = 0; i < n; ++i)
        {
            if (bp == 0) r.s->process(nullptr);
            auto f = r.s->output[0][bp]; fin &= std::isfinite(f); pk = std::max(pk, (double)std::fabs(f));
            if (++bp == r.s->blockSize) bp = 0;
        }
        auto dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        CHECK(fin, "12 operators + 132 links + 12 feedbacks, 8 voices: finite, peak %.2f", pk);
        printf("cpu: that worst case uses %.0f %% of one core here (4 s of sound in %.2f s)\n", 100.0 * dt / 4.0, dt);
    }

    printf(fails ? "FAILURES: %d\n" : "ALL PASS\n", fails);
    return fails ? 1 : 0;
}
