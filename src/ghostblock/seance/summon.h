/*
 * SEANCE (GhostBlock) - SUMMON: a new sound from nothing, in a chosen character.
 *
 * It fills a patch by rule (which seats sing, which possess which, ratios, envelopes, species, filters, movement), then
 * plays it through a scratch copy of the engine and trims what is heard so three notes peak near 0.55. A sound that
 * comes out silent, non-finite or still ringing long after release is thrown away and made again. Main thread only.
 * No sound is copied from anywhere: every value comes from the random numbers and the rules in this file.
 */
#pragma once
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <vector>
#include "synth/synth.h"
#include "synth/matrix_index.h"

namespace ghostblock::seance
{
struct Summoner
{
    using Patch = baconpaul::six_sines::Patch;
    using Synth = baconpaul::six_sines::Synth;
    using SN = Patch::SourceNode;
    enum Style : int { BASS = 0, BELL, PAD, LEAD, KEYS, UNHOLY, kStyles };
    static const char *styleName(int s)
    {
        static const char *n[kStyles] = {"A bass", "A bell", "A pad", "A lead", "Keys", "Something unholy (anything goes)"};
        return n[std::clamp(s, 0, (int)kStyles - 1)];
    }

    std::unique_ptr<Synth> s; // the scratch engine (made the first time)
    std::mt19937 rng{std::random_device{}()};
    std::vector<int> heardOps;
    // optional: hands out a wavetable file (name + the text of a Vital table) for a wavetable spirit; false = none.
    // The editor fills this from the tables on the owner's disk. Unset: built-in tables only.
    std::function<bool(std::string &name, std::string &text)> pickTable;
    enum What : int { DEPTHS = 1, TUNING = 2, TABLES = 4, FILTERS = 8, SHAPES = 16, ALL = 31 }; // what MUTATE may touch

    float u() { return std::uniform_real_distribution<float>(0.f, 1.f)(rng); }
    float in(float a, float b) { return a + (b - a) * u(); }
    int pickN(int a, int b) { return std::uniform_int_distribution<int>(a, b)(rng); }
    bool chance(float p) { return u() < p; }
    template <size_t N> float of(const float (&v)[N]) { return v[(size_t)pickN(0, (int)N - 1)]; }

    static void env(Patch::DAHDSRMixin &e, float a, float d, float sus, float r)
    {
        e.envPower = 1; e.delay = 0; e.hold = 0; e.attack = a; e.decay = d; e.sustain = sus; e.release = r;
    }
    static int throatIndex(const char *group, const char *name)
    {
        const auto &cat = throatCatalog();
        for (int k = 1; k < (int)cat.size(); ++k)
            if (cat[(size_t)k].group.find(group) != std::string::npos && cat[(size_t)k].name.find(name) != std::string::npos) return k;
        return 0;
    }

    // three notes for a second, then the release; peak while held, tail = level 2 s after letting go
    float play(const Patch &P, float &tail, bool &finite)
    {
        if (!s)
        {
            s = std::make_unique<Synth>(false);
            s->setSampleRate(48000);
        }
        static clap_output_events sink{nullptr, [](const clap_output_events *, const clap_event_header_t *) { return true; }};
        s->patch.copyValuesFrom(P);
        // measured with every attack instant, so a slow swell is judged at its full level, not at its first second
        for (auto &m : s->patch.mixerNodes) m.attack = 0;
        for (auto &m : s->patch.matrixNodes) m.attack = 0;
        s->postLoad();
        s->reconcileWavetables();
        for (int k : {48, 55, 60}) s->voiceManager->processNoteOnEvent(0, 0, k, -1, 0.85f, 0.f);
        float pk = 0;
        finite = true;
        size_t bp = 0;
        auto run = [&](int n, bool measure, float &m) {
            for (int i = 0; i < n; ++i)
            {
                if (bp == 0) s->process(&sink);
                if (measure)
                    for (int c = 0; c < 2; ++c)
                    {
                        const float y = s->output[c][bp];
                        finite = finite && std::isfinite(y);
                        m = std::max(m, std::fabs(y));
                    }
                if (++bp == s->blockSize) bp = 0;
            }
        };
        run(48000, true, pk);
        for (int k : {48, 55, 60}) s->voiceManager->processNoteOffEvent(0, 0, k, -1, 0.5f);
        float skip = 0;
        run(48000 * 2, false, skip);
        tail = 0;
        run(4800, true, tail);
        s->voiceManager->allSoundsOff();
        run(2400, false, skip);
        return pk;
    }

    void clear(Patch &P)
    {
        P.resetToInit();
        for (auto &m : P.mixerNodes) { m.active = 0; m.level = 0; }
        for (auto &m : P.matrixNodes) { m.active = 0; m.level = 0; }
        for (auto &m : P.selfNodes) { m.active = 0; m.fbLevel = 0; }
        for (auto &m : P.sourceNodes) m.active = 0;
        heardOps.clear();
    }

    void giveTable(Patch &P, int o, float odds)
    {
        if (!pickTable || !chance(odds)) return;
        std::string nm, js;
        if (pickTable(nm, js) && !js.empty()) P.setShadeTable((size_t)o, nm, js);
    }
    // a new voice for one seat: sine, wavetable or one of the first four Surge oscillators (its place in the wiring stays)
    void voiceFor(Patch &P, int o, float shadeOdds, float beastOdds)
    {
        auto &n = P.sourceNodes[(size_t)o];
        const float r = u();
        const int species = r < shadeOdds ? (int)SN::shadeSpecies : r < shadeOdds + beastOdds ? pickN(1, 4) : 0;
        n.species = (float)species;
        P.setShadeTable((size_t)o, "", "");
        if (species == SN::shadeSpecies)
        {
            const int uni = chance(0.4f) ? pickN(2, 5) : 1;
            const int morph = chance(0.3f) ? pickN(1, 9) : 0, warp = chance(0.3f) ? pickN(1, 6) : 0;
            const float v[7] = {(float)pickN(0, 7) / 7.f, u(), (float)morph / 9.f, in(0.3f, 0.7f), (float)warp / 6.f, in(0.25f, 0.6f), (float)(uni - 1) / 15.f};
            for (int k = 0; k < 7; ++k) n.beastP[(size_t)k] = v[k];
            n.beastSet = 1;
            giveTable(P, o, 0.6f);
        }
        else
            n.beastSet = 0; // (a Surge oscillator starts from its own defaults)
    }
    // NEW VOICES: every heard spirit gets a new voice; wiring, tuning, envelopes and filters stay. false = it did not play.
    bool revoice(Patch &P)
    {
        int any = 0;
        for (int o = 0; o < 12; ++o)
            if (P.sourceNodes[(size_t)o].active.value > 0.5f && P.mixerNodes[(size_t)o].active.value > 0.5f) { voiceFor(P, o, 0.5f, 0.25f); ++any; }
        return any > 0 && trim(P);
    }
    std::string name()
    {
        static const char *a[] = {"Hollow", "Ashen", "Black", "Pale", "Buried", "Iron", "Velvet", "Salt", "Feral", "Drowned", "Waxen", "Bitter", "Gilded", "Restless", "Cold",
                                  "Burnt", "Silent", "Crooked", "Sunken", "Moth", "Thorn", "Copper", "Wicked", "Last", "Veiled", "Howling", "Grave", "Ember", "Glass", "Bone"};
        static const char *b[] = {"Covenant", "Choir", "Lantern", "Widow", "Chapel", "Tongue", "Omen", "Vigil", "Marrow", "Reliquary", "Hymn", "Procession", "Altar", "Shroud", "Familiar",
                                  "Rite", "Wake", "Seance", "Cellar", "Mirror", "Psalm", "Furnace", "Orchard", "Parlour", "Throat", "Bell", "Circle", "Hour", "Ghost", "Garden"};
        std::string n = std::string(a[pickN(0, 29)]) + " " + b[pickN(0, 29)];
        return n;
    }

    // one attempt: fills P; false when the rules made nothing heard
    void build(Patch &P, int style)
    {
        using MI = baconpaul::six_sines::MatrixIndex;
        clear(P);
        const bool unholy = style == UNHOLY;
        const int st = unholy ? pickN(0, 4) : style; // the envelope family an unholy sound borrows
        const int nCar = style == BASS ? pickN(1, 2) : style == BELL ? pickN(3, 6) : style == PAD ? pickN(3, 4) : style == LEAD ? pickN(1, 2) : style == KEYS ? pickN(2, 3) : pickN(1, 6);
        const int total = unholy ? 12 : std::clamp(nCar + pickN(2, 9), nCar + 1, 12);
        // seats in a shuffled order, so the picture on the table differs every time
        std::vector<int> seats(12);
        for (int i = 0; i < 12; ++i) seats[(size_t)i] = i;
        std::shuffle(seats.begin(), seats.end(), rng);
        seats.resize((size_t)total);

        // amplitude envelope of the family
        float A, D, S, R;
        switch (st)
        {
        case BASS: A = in(0.f, 0.04f); D = in(0.42f, 0.55f); S = in(0.5f, 1.f); R = in(0.2f, 0.3f); break;
        case BELL: A = 0.f; D = in(0.58f, 0.7f); S = 0.f; R = in(0.52f, 0.64f); break;
        case PAD: A = in(0.5f, 0.64f); D = 0.6f; S = in(0.75f, 0.95f); R = in(0.58f, 0.66f); break;
        case LEAD: A = in(0.02f, 0.08f); D = 0.5f; S = in(0.75f, 0.95f); R = in(0.28f, 0.36f); break;
        default: A = 0.f; D = in(0.5f, 0.64f); S = in(0.f, 0.25f); R = in(0.4f, 0.5f); break;
        }
        static const float carBass[] = {1, 1, 1, 0.5f}, carBell[] = {1, 2.76f, 5.4f, 8.93f, 0.5f, 13.34f, 2, 3.01f}, carPad[] = {1, 1, 2, 1, 0.5f}, carLead[] = {1, 1, 2}, carKeys[] = {1, 1, 2, 3};
        static const float modBass[] = {0.5f, 1, 1, 2, 3}, modBell[] = {1.41f, 2.76f, 3.5f, 5.4f, 7.1f, 11.f}, modPad[] = {1, 2, 3, 0.5f}, modLead[] = {1, 2, 3, 4, 1.5f}, modKeys[] = {1, 2, 3, 7, 14};
        const float depth = unholy ? in(0.25f, 0.6f) : st == BASS ? in(0.25f, 0.45f) : st == BELL ? in(0.2f, 0.35f) : st == PAD ? in(0.06f, 0.16f) : st == LEAD ? in(0.18f, 0.36f) : in(0.2f, 0.32f);
        const float shadeOdds = unholy ? 0.4f : st == PAD ? 0.6f : st == LEAD ? 0.4f : st == BELL ? 0.15f : 0.3f;
        const float beastOdds = unholy ? 0.3f : st == BASS ? 0.25f : st == LEAD ? 0.2f : st == PAD ? 0.15f : 0.f;

        std::vector<int> fanIn(12, 0);
        for (int i = 0; i < total; ++i)
        {
            const int o = seats[(size_t)i];
            auto &n = P.sourceNodes[(size_t)o];
            n.active = 1;
            const bool car = i < nCar;
            float ratio;
            if (car)
            {
                ratio = st == BASS ? of(carBass) : st == BELL ? carBell[(size_t)(i % 8)] : st == PAD ? of(carPad) : st == LEAD ? of(carLead) : of(carKeys);
                if (i == 0 && st != BELL) ratio = 1.f; // the first singer holds the note
                if (i > 0 && st != BELL) ratio *= 1.f + in(-0.004f, 0.004f); // the others sit a hair off it
            }
            else
                ratio = st == BASS ? of(modBass) : st == BELL ? of(modBell) : st == PAD ? of(modPad) : st == LEAD ? of(modLead) : of(modKeys);
            n.ratio = std::log2(ratio);
            // species: a singer may be a wavetable or a Surge oscillator; possessors stay sines unless anything goes
            int species = 0;
            if (car || (unholy && chance(0.25f)))
            {
                const float r = u();
                if (r < shadeOdds) species = SN::shadeSpecies;
                else if (r < shadeOdds + beastOdds) species = st == BASS ? pickN(1, 2) : pickN(1, 4);
            }
            n.species = (float)species;
            if (species == SN::shadeSpecies)
            {
                const int uni = st == PAD ? pickN(3, 5) : chance(0.3f) ? 3 : 1;
                const int morph = chance(unholy ? 0.5f : 0.2f) ? pickN(1, 9) : 0, warp = chance(unholy ? 0.5f : 0.25f) ? pickN(1, 6) : 0;
                const float v[7] = {(float)pickN(0, 7) / 7.f, u(), (float)morph / 9.f, in(0.3f, 0.7f), (float)warp / 6.f, in(0.25f, 0.6f), (float)(uni - 1) / 15.f};
                for (int k = 0; k < 7; ++k) n.beastP[(size_t)k] = v[k];
                n.beastSet = 1;
                giveTable(P, o, 0.6f);
            }
            if (car)
            {
                auto &m = P.mixerNodes[(size_t)o];
                m.active = 1;
                m.level = (st == BELL ? 0.7f / (1.f + 0.5f * (float)i) : 0.7f) / std::sqrt((float)nCar);
                m.pan = nCar == 1 ? 0.f : in(-0.6f, 0.6f);
                env(m, A, D - (st == BELL ? 0.03f * (float)i : 0.f), S, R - (st == BELL ? 0.03f * (float)i : 0.f));
                heardOps.push_back(o);
                // a filter on a bright singer, opened by its envelope or swayed by its LFO
                if (species != 0 && st != BELL && chance(0.65f))
                {
                    static const char *fg[] = {"Vember Classic", "OB-Xd", "K35", "Diode Ladder", "Vintage Ladder"}, *fn[] = {"Lowpass 24 dB Standard", "24", "Lowpass", "24", ""};
                    const int f = pickN(0, 4), k = throatIndex(fg[f], fn[f]);
                    if (k > 0)
                    {
                        auto &t = P.throatNodes[(size_t)o];
                        t.kind = (float)k;
                        t.resonance = in(0.1f, 0.5f);
                        t.keytrack = 1.f;
                        if (st == BASS || st == KEYS)
                        {
                            t.cutoff = in(4.f, 18.f);
                            env(n, 0.f, in(0.38f, 0.55f), in(0.f, 0.3f), 0.3f);
                            t.envToCutoff = in(18.f, 34.f);
                        }
                        else
                        {
                            t.cutoff = in(26.f, 40.f);
                            n.lfoRate = in(-2.6f, st == LEAD ? 1.5f : -0.5f);
                            t.lfoToCutoff = in(4.f, 12.f);
                        }
                    }
                }
                if (species == SN::shadeSpecies && chance(st == PAD ? 0.8f : 0.4f))
                { // the wavetable drifts
                    auto &t = P.throatNodes[(size_t)o];
                    if (t.lfoToCutoff.value == 0.f) n.lfoRate = in(-2.6f, -0.8f);
                    t.bodyTarget = 0.f;
                    t.lfoToBody = in(0.12f, 0.35f) * (chance(0.5f) ? 1.f : -1.f);
                }
            }
        }
        // every possessor takes hold of a singer, or of a possessor made before it (a stack, at most three deep)
        std::vector<int> depthOf(12, 0);
        for (int i = nCar; i < total; ++i)
        {
            const int o = seats[(size_t)i];
            int target = seats[(size_t)pickN(0, nCar - 1)];
            if (i > nCar && chance(0.4f))
            {
                const int cand = seats[(size_t)pickN(nCar, i - 1)];
                if (depthOf[(size_t)cand] < 2) target = cand;
            }
            depthOf[(size_t)o] = depthOf[(size_t)target] + 1;
            fanIn[(size_t)target]++;
            auto &m = P.matrixNodes[MI::positionForSourceTarget((size_t)o, (size_t)target)];
            m.active = 1;
            m.level = depth; // (shared out below)
            if (st == PAD) env(m, in(0.5f, 0.62f), 0.6f, in(0.5f, 0.8f), 0.6f);
            else if (st == LEAD) env(m, in(0.f, 0.2f), in(0.4f, 0.55f), in(0.3f, 0.7f), 0.3f);
            else env(m, 0.f, in(0.3f, 0.5f), st == BASS ? in(0.1f, 0.4f) : 0.f, in(0.3f, 0.4f));
            if (st != PAD && chance(0.5f))
            { // play harder, bend harder: the thread's depth follows velocity (the engine's own modulation slot)
                m.modsource[0] = 5000;
                m.modtarget[0] = 10;
                m.moddepth[0] = in(0.15f, 0.5f);
            }
            if (chance(unholy ? 0.35f : 0.12f))
            {
                P.selfNodes[(size_t)o].active = 1;
                P.selfNodes[(size_t)o].fbLevel = in(0.08f, unholy ? 0.4f : 0.22f);
            }
        }
        // many possessors on one seat share the depth, so a crowd does not turn it to noise
        for (int i = nCar; i < total; ++i)
        {
            const int o = seats[(size_t)i];
            for (int t = 0; t < 12; ++t)
            {
                if (t == o) continue;
                auto &m = P.matrixNodes[MI::positionForSourceTarget((size_t)o, (size_t)t)];
                if (m.active.value > 0.5f) m.level = m.level.value / std::pow((float)std::max(1, fanIn[(size_t)t]), 0.7f);
            }
        }
        if (unholy)
        { // the circle closes: two singers reach back into the crowd
            for (int k = 0; k < 2 && total > nCar; ++k)
            {
                const int from = seats[(size_t)pickN(0, nCar - 1)], to = seats[(size_t)pickN(nCar, total - 1)];
                auto &m = P.matrixNodes[MI::positionForSourceTarget((size_t)from, (size_t)to)];
                m.active = 1;
                m.level = in(0.08f, 0.22f);
                env(m, 0.f, 0.5f, 0.5f, 0.4f);
            }
        }
    }

    // plays P and sets what is heard so three notes peak near 0.55; false = silent, not finite, or still ringing after release
    bool trim(Patch &P)
    {
        heardOps.clear();
        for (int o = 0; o < 12; ++o)
            if (P.sourceNodes[(size_t)o].active.value > 0.5f && P.mixerNodes[(size_t)o].active.value > 0.5f) heardOps.push_back(o);
        float tail = 0;
        bool fin = true;
        float pk = play(P, tail, fin);
        for (int pass = 0; pass < 3 && fin && pk > 1e-4f && (pk > 0.65f || pk < 0.42f); ++pass)
        {
            const float g = 0.55f / pk;
            for (int o : heardOps) P.mixerNodes[(size_t)o].level = std::min(1.f, P.mixerNodes[(size_t)o].level.value * g);
            pk = play(P, tail, fin);
        }
        for (int pass = 0; pass < 6 && fin && pk > 1e-4f && pk < 0.35f; ++pass)
        { // still quiet with every singer at full: raise the main volume
            auto &ol = P.output.level;
            if (ol.value >= ol.meta.maxVal - 1e-4f) break;
            ol = std::min(ol.meta.maxVal, ol.value + 0.12f * (ol.meta.maxVal - ol.meta.minVal));
            pk = play(P, tail, fin);
        }
        lastPeak = pk;
        lastTail = tail;
        return fin && pk > 0.12f && pk < 0.95f && tail < 0.03f;
    }

    // MUTATE: the sound on the table, nudged. amount about 0.35 = a little, 1 = a lot. The wiring stays (a lot may add
    // one thread); depths, possessor ratios, wavetable positions, filters, feedback, pans and tails drift.
    // false = the result did not play properly (the caller puts the old sound back).
    bool mutate(Patch &P, float amount, int what = ALL)
    {
        using MI = baconpaul::six_sines::MatrixIndex;
        auto c01 = [](float v) { return std::clamp(v, 0.f, 1.f); };
        auto within = [](baconpaul::six_sines::Param &p, float v) { p = std::clamp(v, p.meta.minVal, p.meta.maxVal); };
        static const float steps[] = {0.5f, 1, 1, 2, 2, 3, 4, 5, 7};
        std::vector<int> awake;
        for (int o = 0; o < 12; ++o)
        {
            auto &n = P.sourceNodes[(size_t)o];
            if (n.active.value < 0.5f) continue;
            awake.push_back(o);
            const bool heard = P.mixerNodes[(size_t)o].active.value > 0.5f;
            if (what & TUNING)
            {
                if (!heard && chance(0.3f * amount)) within(n.ratio, std::log2(of(steps)));
                else if (chance(0.5f)) within(n.ratio, n.ratio.value + in(-0.006f, 0.006f) * amount);
            }
            if ((what & TABLES) && (int)std::lround(n.species.value) == SN::shadeSpecies && n.beastSet.value > 0.5f)
            {
                n.beastP[1] = c01(n.beastP[1].value + in(-0.2f, 0.2f) * amount);
                n.beastP[3] = c01(n.beastP[3].value + in(-0.15f, 0.15f) * amount);
                n.beastP[5] = c01(n.beastP[5].value + in(-0.15f, 0.15f) * amount);
                if (what == TABLES)
                { // asked for the wavetables alone: a different table too (a built-in one, or one of the owner's files)
                    if (pickTable && chance(0.6f)) giveTable(P, o, 1.f);
                    else { P.setShadeTable((size_t)o, "", ""); n.beastP[0] = (float)pickN(0, 7) / 7.f; }
                }
            }
            auto &t = P.throatNodes[(size_t)o];
            if ((what & FILTERS) && t.kind.value > 0.5f)
            {
                within(t.cutoff, t.cutoff.value + in(-8.f, 8.f) * amount);
                within(t.resonance, t.resonance.value + in(-0.12f, 0.12f) * amount);
            }
            auto &f = P.selfNodes[(size_t)o];
            if ((what & SHAPES) && f.active.value > 0.5f) within(f.fbLevel, f.fbLevel.value * std::exp(in(-0.35f, 0.35f) * amount));
            auto &m = P.mixerNodes[(size_t)o];
            if ((what & SHAPES) && heard)
            {
                within(m.pan, m.pan.value + in(-0.2f, 0.2f) * amount);
                within(m.decay, m.decay.value + in(-0.04f, 0.04f) * amount);
                within(m.release, m.release.value + in(-0.03f, 0.03f) * amount);
            }
        }
        if (awake.empty()) return false;
        for (int a : awake)
            for (int b : awake)
            {
                if (a == b) continue;
                auto &m = P.matrixNodes[MI::positionForSourceTarget((size_t)a, (size_t)b)];
                if ((what & DEPTHS) && m.active.value > 0.5f) within(m.level, m.level.value * std::exp(in(-0.45f, 0.45f) * amount));
            }
        if (what == ALL && amount > 0.7f && awake.size() >= 2 && chance(0.6f))
        { // one new thread between two spirits that are already awake
            const int a = awake[(size_t)pickN(0, (int)awake.size() - 1)];
            int b = awake[(size_t)pickN(0, (int)awake.size() - 1)];
            if (b == a) b = awake[(size_t)((std::find(awake.begin(), awake.end(), a) - awake.begin() + 1) % (long)awake.size())];
            auto &m = P.matrixNodes[MI::positionForSourceTarget((size_t)a, (size_t)b)];
            if (m.active.value < 0.5f)
            {
                m.active = 1;
                m.level = in(0.06f, 0.2f);
                env(m, 0.f, in(0.35f, 0.5f), in(0.f, 0.5f), 0.35f);
            }
        }
        return trim(P);
    }

    // SUMMON AROUND: a new sound in the given character that keeps part of the one on the table.
    //   KEEP_WIRING: which spirits are awake and heard, every thread (with its depth and envelope), feedback, tuning
    //   KEEP_VOICES: what each seat is (sine / wavetable / Surge oscillator, its knobs and table file) and its filter
    //   KEEP_SHAPES: the heard spirits' volume envelopes and pans
    // The rest is made new. "" = nothing that plays came out (P is left as it was).
    enum Keep : int { KEEP_WIRING = 1, KEEP_VOICES = 2, KEEP_SHAPES = 4 };
    std::string summonKeeping(Patch &P, int style, int keep)
    {
        using Param = baconpaul::six_sines::Param;
        auto inside = [](const Param *p, const void *obj, size_t size) { return (const char *)p >= (const char *)obj && (const char *)p < (const char *)obj + size; };
        std::vector<std::pair<Param *, float>> kept, all;
        for (auto *p : P.params)
        {
            all.emplace_back(const_cast<Param *>(p), (float)p->value);
            bool k = false;
            if (keep & KEEP_WIRING)
            {
                for (auto &m : P.matrixNodes) k = k || inside(p, &m, sizeof(m));
                for (auto &m : P.selfNodes) k = k || inside(p, &m, sizeof(m));
                for (size_t o = 0; o < 12; ++o) k = k || p == &P.sourceNodes[o].active || p == &P.sourceNodes[o].ratio || p == &P.mixerNodes[o].active;
            }
            if (keep & KEEP_VOICES)
            {
                for (auto &t : P.throatNodes) k = k || inside(p, &t, sizeof(t));
                for (size_t o = 0; o < 12; ++o)
                {
                    auto &n = P.sourceNodes[o];
                    k = k || p == &n.species || p == &n.beastSet;
                    for (auto &b : n.beastP) k = k || p == &b;
                }
            }
            if (keep & KEEP_SHAPES)
                for (size_t o = 0; o < 12; ++o) k = k || (inside(p, &P.mixerNodes[o], sizeof(P.mixerNodes[o])) && p != &P.mixerNodes[o].active);
            if (k) kept.emplace_back(const_cast<Param *>(p), (float)p->value);
        }
        const auto tables = P.shadeTables;
        const std::string oldName = P.name;
        for (int attempt = 0; attempt < 5; ++attempt)
        {
            build(P, style);
            // the new sound's own volume envelope, for seats the old wiring makes heard that the new one did not
            float ne[4] = {0.f, 0.5f, 0.8f, 0.3f};
            if (!heardOps.empty())
            {
                auto &src = P.mixerNodes[(size_t)heardOps[0]];
                ne[0] = src.attack.value; ne[1] = src.decay.value; ne[2] = src.sustain.value; ne[3] = src.release.value;
            }
            for (auto &[p, v] : kept) *p = v;
            if (keep & KEEP_VOICES) P.shadeTables = tables;
            if ((keep & KEEP_WIRING) && !(keep & KEEP_SHAPES))
                for (size_t o = 0; o < 12; ++o)
                {
                    auto &m = P.mixerNodes[o];
                    if (m.active.value > 0.5f && m.level.value < 0.05f) { m.level = 0.5f; env(m, ne[0], ne[1], ne[2], ne[3]); }
                }
            if (keep & KEEP_WIRING)
                for (size_t o = 0; o < 12; ++o) // a seat the old wiring leaves asleep stays out of the new sound
                    if (P.sourceNodes[o].active.value < 0.5f) { P.mixerNodes[o].active = 0; P.throatNodes[o].kind = (keep & KEEP_VOICES) ? P.throatNodes[o].kind.value : 0.f; }
            if (trim(P))
            {
                const auto nm = name();
                strncpy(P.name, nm.c_str(), sizeof(P.name) - 1);
                P.name[sizeof(P.name) - 1] = 0;
                return nm;
            }
        }
        for (auto &[p, v] : all) *p = v;
        P.shadeTables = tables;
        strncpy(P.name, oldName.c_str(), sizeof(P.name) - 1);
        return {};
    }

    // fills P with a new sound and names it; returns the name ("" = it could not make one that plays, P is left bare)
    std::string summon(Patch &P, int style)
    {
        for (int attempt = 0; attempt < 6; ++attempt)
        {
            build(P, style);
            if (trim(P))
            {
                const auto nm = name();
                strncpy(P.name, nm.c_str(), sizeof(P.name) - 1);
                P.name[sizeof(P.name) - 1] = 0;
                return nm;
            }
        }
        clear(P);
        return {};
    }
    float lastPeak{0}, lastTail{0};
};
} // namespace ghostblock::seance
