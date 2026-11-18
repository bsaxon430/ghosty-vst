#pragma once
// TRAP + HIP HOP checks: notes in range / scale, tempo + swing per flavour, arp in the melody's rests, 808 / sub patterns + slides,
// voicings, archetype coverage, the mood sounds, state round trips (+ older states' preset indices), the new presets rendering
// clean, level match, the variety table, HOOK LAB in the new moods, and the old moods bit-identical (ideas + hooks).
// Included by ArpTests.cpp (after render / renderIdea / moodRender / setP / soundtest).
#include "../UrbanMoods.h"
#include "VarietyTests.h"
#include "HookFingerprint.h"

namespace urbantest
{
using namespace arp;
// the shipped old-mood fingerprints (VarietyTests.h, HookFingerprint.h), measured on origin/main before the TRAP / HIP HOP work
constexpr uint64_t kIdeaPrint[3] { 0x5c5c86c6dbf39997ull, 0xe18f47e20ec5f230ull, 0x04b66986d758a5a4ull };
constexpr uint64_t kHookPrint[3] { 0xc92d42e26593fb8full, 0x22907d449ee513ffull, 0x824336e1cb006059ull };

inline Idea make (int mood, uint32_t seed, int flavour = -1)
{
    Idea I; I.mode = mood; I.seed = seed; I.flavour = flavour; applyModeDefaults (I); newIdea (I); return I;
}
// a melody note is fine when it is in the key / chord / the idea's pentatonic, or a short chromatic note (grace, blue note, b2)
// that moves by a step to the next note
inline int badMelodyNotes (const Idea& I, const std::vector<Note>& ns, std::string* why = nullptr)
{
    std::vector<int> pool; for (int p : I.H.keyPcs) pool.push_back (p);
    int bad = 0;
    for (size_t i = 0; i < ns.size(); ++i)
    {
        const auto& n = ns[i];
        if (n.p < 36 || n.p > 96) { ++bad; if (why) *why = "range " + std::to_string (n.p); continue; }
        const bool inKey = std::find (pool.begin(), pool.end(), mod12 (n.p)) != pool.end() || I.H.chordTone (n.p, n.t);
        if (inKey) continue;
        const bool shortStep = n.d <= PPQ / 2 + 2 && i + 1 < ns.size() && ns[i + 1].t - n.t <= PPQ && std::abs (ns[i + 1].p - n.p) <= 2;
        if (! shortStep) { ++bad; if (why) { *why = "out of key " + std::to_string (n.p) + " d " + std::to_string (n.d) + " at " + std::to_string (n.t) + " next"; for (size_t k = i + 1; k < ns.size() && k < i + 3; ++k) *why += " " + std::to_string (ns[k].p) + "@" + std::to_string (ns[k].t); } }
    }
    return bad;
}

inline void checks (Counter& C)
{
    // ---- old moods: bit-identical (ideas: every lane, EVOLVE, RICH, lane moods; hooks: 4 / 8 bars, styles, candy)
    for (int m = 0; m < 3; ++m)
    {
        const uint64_t a = arptest::moodFingerprint (m, 20), h = arptest::hookFingerprint (m);
        std::printf ("old mood %s: ideas %016llx (shipped %016llx), hooks %016llx (shipped %016llx)\n", modeName (m).c_str(), (unsigned long long) a, (unsigned long long) kIdeaPrint[m], (unsigned long long) h, (unsigned long long) kHookPrint[m]);
        C.ok (a == kIdeaPrint[m], "OLD MOODS: " + modeName (m) + " ideas regenerate bit-identically (same seed -> same notes as shipped)");
        C.ok (h == kHookPrint[m], "OLD MOODS: " + modeName (m) + " HOOK LAB hooks bit-identical");
    }
    // ---- per mood + flavour: tempo, swing, notes, arp in the rests, bass, voicings
    for (int mood : { (int) Trap, (int) HipHop })
    {
        int badMel = 0, badTempo = 0, badSwing = 0, arpInMel = 0, badBass = 0, badVoicing = 0, sevenths = 0, chordsN = 0, slides = 0, ideas = 0, attacks = 0, bars = 0, dt = 0;
        std::string why;
        for (int f = 0; f < kFlavours; ++f)
            for (uint32_t s = 1; s <= 8; ++s)
            {
                const Idea I = make (mood, s * 97 + (uint32_t) f, f); ++ideas;
                const auto fi = flavourInfo (mood, f);
                const bool dtOk = fi.doubleTimeOk && I.doubleTime && I.g.bpm >= 160 && I.g.bpm <= 200;
                if (I.doubleTime) ++dt;
                if (! dtOk && (I.g.bpm < fi.bpmLo || I.g.bpm > fi.bpmHi)) ++badTempo;
                const double sw = grooveToSwing (I.groove);
                if (sw < fi.swingLo - 0.01 || sw > fi.swingHi + 0.01) ++badSwing;
                const auto& mel = I.lanes[LaneMelody].notes;
                badMel += badMelodyNotes (I, mel, &why);
                for (auto& n : mel) if (n.d > PPQ / 4 + 2) ++attacks;
                bars += I.total / BAR;
                if (mood == Trap)
                    for (auto& a : I.lanes[LaneArp].notes) for (auto& m : mel) if (a.t >= m.t && a.t < m.t + m.d) { ++arpInMel; break; }
                const auto& bs = I.lanes[LaneBass].notes;
                for (size_t i = 0; i < bs.size(); ++i)
                {
                    if (bs[i].p < 24 || bs[i].p > 50) ++badBass;
                    if (bs[i].l.type == 1) { ++slides; if (i + 1 < bs.size() && bs[i + 1].p == bs[i].p) ++badBass; }
                    else if (i + 1 < bs.size() && bs[i].t + bs[i].d > bs[i + 1].t) ++badBass;   // mono
                }
                for (auto& c : I.H.chords)
                {
                    ++chordsN;
                    if (c.pcs.size() >= 4) ++sevenths;
                    if (mood == Trap) { if (c.voicing.size() > 3 || c.voicing.front() < 50 || c.voicing.back() > 67) ++badVoicing; }
                    else if (c.voicing.front() < 52 || c.voicing.back() > 74) ++badVoicing;
                }
            }
        const std::string mn = modeName (mood);
        std::printf ("%s: %d ideas, tempo off %d, swing off %d, melody notes off %d%s, melody attacks/bar %.2f, arp onsets inside the melody %d, bass off %d, slides %d, voicings off %d, 4+ note chords %d/%d, double-time %d\n",
                     mn.c_str(), ideas, badTempo, badSwing, badMel, badMel ? (" (" + why + ")").c_str() : "", (double) attacks / std::max (1, bars), arpInMel, badBass, slides, badVoicing, sevenths, chordsN, dt);
        C.ok (badTempo == 0, mn + ": tempo inside each flavour's range (TRAP 92..160, HIP HOP 80..100 or double-time 160..200)");
        C.ok (badSwing == 0, mn + ": swing inside each flavour's range (as the idea's GROOVE)");
        C.ok (badMel == 0, mn + ": melody notes in range and in the key / chord (chromatic only as short steps: graces, blue notes, b2)");
        C.ok (badBass == 0, mn + ": bass in E0..D3, mono, slides only between different notes");
        C.ok (badVoicing == 0, mn + (mood == Trap ? ": dark 2-3 note voicings in D3..G4" : ": voicings in E3..D5"));
        C.ok (slides > 0, mn + ": the bass slides now and then");
        if (mood == Trap) C.ok (arpInMel == 0, "TRAP: the arp plays only in the melody's rests");
        if (mood == HipHop) C.ok (sevenths * 2 > chordsN, "HIP HOP: mostly 7th / 9th chords");
        if (mood == Trap) C.ok ((double) attacks / std::max (1, bars) <= 8.0, "TRAP: sparse melody (<= 8 attacks per bar on average)");
    }
    {   // TRAP stays minor at any DARKNESS (minor / harmonic minor / phrygian / minor pentatonic; dorian only as the brighter minor
        // below 50) in every flavour but EMO (its own checks: EmoTests.h); AUTO = DARK / DRILL / EMO by the seed; DARKNESS 100 vs 0: a
        // lower melody and more b2 / b5
        int notMinor = 0, ideasN = 0, autoDark = 0, emoSkipped = 0; double pitch[2] { 0, 0 }; int pn[2] { 0, 0 }, b25[2] { 0, 0 };
        std::string why;
        for (int dk : { 0, 25, 40, 70, 85, 100 })
            for (int f = -1; f < kFlavours; ++f)
                for (uint32_t sd = 1; sd <= 6; ++sd)
                {
                    Idea I; I.mode = Trap; I.seed = sd * 131 + (uint32_t) (f + 1) * 7 + (uint32_t) dk; I.flavour = f; I.darkness = dk; applyModeDefaults (I); newIdea (I);
                    if (f < 0 && (I.flavourUsed == 0 || I.flavourUsed == 3 || I.flavourUsed == kEmo)) ++autoDark;
                    if (I.flavourUsed == kEmo) { ++emoSkipped; continue; }
                    ++ideasN;
                    const bool scaleOk = I.g.scale == "minor" || I.g.scale == "harmonic" || I.g.scale == "phrygian" || (I.g.scale == "dorian" && dk < 50);
                    const auto has = [&] (int pc) { return std::find (I.H.keyPcs.begin(), I.H.keyPcs.end(), mod12 (I.H.key + pc)) != I.H.keyPcs.end(); };
                    bool tonicMinor = true; for (auto& c : I.H.chords) if (c.root == I.H.key && std::find (c.pcs.begin(), c.pcs.end(), mod12 (c.root + 4)) != c.pcs.end()) tonicMinor = false;
                    if (! scaleOk || ! has (3) || has (4) || ! tonicMinor) { ++notMinor; why = I.g.scale + " flavour " + std::to_string (I.flavourUsed) + " darkness " + std::to_string (dk); }
                    if (f < 0 && (dk == 0 || dk == 100))
                    {
                        const int k = dk == 100 ? 1 : 0;
                        for (auto& n : I.lanes[LaneMelody].notes) { pitch[k] += n.p; ++pn[k]; const int d = mod12 (n.p - I.H.key); if (d == 1 || d == 6) ++b25[k]; }
                    }
                }
        const double m0 = pitch[0] / std::max (1, pn[0]), m1 = pitch[1] / std::max (1, pn[1]), s0 = 100.0 * b25[0] / std::max (1, pn[0]), s1 = 100.0 * b25[1] / std::max (1, pn[1]);
        const int autoN = 6 * 6;
        std::printf ("TRAP DARKNESS: %d ideas (+ %d AUTO ones on EMO), not minor %d%s; AUTO picked DARK / DRILL / EMO %d / %d; DARKNESS 0 -> 100: mean melody pitch %.1f -> %.1f, b2 + b5 notes %.1f%% -> %.1f%%\n",
                     ideasN, emoSkipped, notMinor, notMinor ? (" (" + why + ")").c_str() : "", autoDark, autoN, m0, m1, s0, s1);
        C.ok (notMinor == 0, "TRAP: every idea at every DARKNESS is minor (minor / harmonic / phrygian / pentatonic; dorian only below 50), tonic chord minor (EMO: its own rules)");
        C.ok (autoDark == autoN, "TRAP: AUTO = DARK / DRILL / EMO (by the seed; GENERATE rotates them)");
        C.ok (m1 < m0 - 3 && s1 > s0, "TRAP DARKNESS: 100 sits lower and uses more b2 / b5 than 0");
    }
    {   // every archetype is reachable (TRAP >= 12 808 patterns, HIP HOP >= 10 bass patterns)
        for (int mood : { (int) Trap, (int) HipHop })
        {
            std::set<int> b, m;
            for (uint32_t s = 1; s <= 500; ++s) { const Idea I = make (mood, s * 13 + 5, (int) (s % kFlavours)); b.insert (I.urbanBassArch); m.insert (I.urbanMelArch); }   // (every flavour: TRAP's AUTO is DARK)
            std::printf ("%s archetypes used: bass %zu / %d, melody %zu / %d\n", modeName (mood).c_str(), b.size(), urbanBassArchCount (mood), m.size(), urbanMelArchCount (mood));
            C.ok ((int) b.size() == urbanBassArchCount (mood) && (int) m.size() == urbanMelArchCount (mood) && urbanBassArchCount (mood) >= (mood == Trap ? 12 : 10),
                  modeName (mood) + ": every bass + melody archetype shows up (" + std::to_string (urbanBassArchCount (mood)) + " bass, " + std::to_string (urbanMelArchCount (mood)) + " melody)");
        }
    }
    {   // the 808 slides reach the MIDI out as overlapping notes (the slide contract), a TRAP idea drags as a valid .mid
        const Idea I = make (Trap, 4242, 1);
        const auto mn = laneMidiNotes (I.lanes[LaneBass].notes, I.total); int ov = 0;
        for (size_t i = 0; i + 1 < mn.size(); ++i) if (mn[i].t + mn[i].d > mn[i + 1].t) ++ov;
        int gl = 0; for (auto& n : I.lanes[LaneBass].notes) gl += n.l.type == 1;
        const auto mid = ideaMidi (I, 15, false, I.g.bpm);
        C.ok (gl == 0 || ov > 0, "TRAP: 808 slides export as overlapping MIDI notes");
        C.ok (mid.size() > 100 && mid[0] == 'M' && mid[1] == 'T', "TRAP: the idea exports as MIDI");
    }
    {   // LANE MOOD: a MIDNIGHT idea's melody in TRAP; the other lanes stay
        Idea I = make (Midnight, 77); const auto chords = I.H.chords.size(); const auto arpN = I.lanes[LaneArp].notes;
        I.laneMood[LaneMelody] = Trap; composeLane (I, LaneMelody, true);
        C.ok (! I.lanes[LaneMelody].notes.empty() && I.lanes[LaneArp].notes.size() == arpN.size() && I.H.chords.size() == chords, "LANE MOOD: TRAP on a MIDNIGHT melody, the other lanes untouched");
    }
    // ---- the processor: mood sounds, state round trip, older states
    {
        ArpProcessor p; p.persistTaste = false;
        p.generate (Trap, true, false);
        auto ch = [&] (const char* id) { return (int) std::lround (p.apvts.getRawParameterValue (id)->load()); };
        const bool emo = isEmo (p.idea());
        const bool trapOk = ch ("preset") == arpsyn::TrapFlute && ch ("preset_bass") == 1 + arpsyn::Bass808 && ch ("preset_arp") == 1 + (emo ? arpsyn::GuitarPluck : arpsyn::Bell);
        p.generate (HipHop, true, false);
        const int fl = p.idea().flavourUsed;
        const bool hhOk = ch ("preset") == arpsyn::DustyKeys && (ch ("preset_chords") == 1 + arpsyn::SoulOrgan || ch ("preset_chords") == 1 + arpsyn::WarmPadKeys)
                          && ch ("preset_bass") == 1 + (fl == 1 || fl == 3 ? arpsyn::Bass808 : arpsyn::SubBass);
        p.generate (Midnight, true, false);
        const bool backOk = ch ("preset") == arpsyn::HypnoSaw && ch ("preset_bass") == 0 && p.linkAll;
        C.ok (trapOk, "MOOD SOUNDS: TRAP = TRAP FLUTE melody, BELL arp (EMO: CLEAN GUITAR PLUCK), 808 BASS");
        C.ok (hhOk, "MOOD SOUNDS: HIP HOP = DUSTY KEYS melody, SOUL ORGAN / WARM PAD KEYS chords, ROUND SUB / 808 per flavour");
        C.ok (backOk, "MOOD SOUNDS: back in MIDNIGHT its own sound, every lane linked again");
    }
    {   // state round trip of a TRAP idea (notes, flavour, tempo, mode) + the sound
        ArpProcessor p; p.persistTaste = false; p.generate (Trap, true, false);
        p.change ([] (Idea& X) { X.flavour = 2; X.darkness = 85; newIdea (X); }, false);
        juce::MemoryBlock mb; p.getStateInformation (mb);
        ArpProcessor q; q.persistTaste = false; q.setStateInformation (mb.getData(), (int) mb.getSize());
        const auto& a = p.idea(); const auto& b = q.idea();
        bool same = a.mode == b.mode && a.darkness == b.darkness && a.flavour == b.flavour && a.flavourUsed == b.flavourUsed && a.g.bpm == b.g.bpm && a.colour == b.colour && a.doubleTime == b.doubleTime;
        for (int l = 1; l < kLanes && same; ++l) { same = a.lanes[l].notes.size() == b.lanes[l].notes.size(); for (size_t i = 0; same && i < a.lanes[l].notes.size(); ++i) same = a.lanes[l].notes[i].t == b.lanes[l].notes[i].t && a.lanes[l].notes[i].p == b.lanes[l].notes[i].p && a.lanes[l].notes[i].l.type == b.lanes[l].notes[i].l.type; }
        for (size_t i = 0; same && i < a.H.chords.size(); ++i) same = a.H.chords[i].voicing == b.H.chords[i].voicing;
        same = same && (int) q.apvts.getRawParameterValue ("preset_bass")->load() == 1 + arpsyn::Bass808;
        C.ok (same, "STATE: a TRAP idea (flavour, tempo, notes, voicings, 808 bass) round-trips exactly");
    }
    {   // an older state (no nPresets): factory sounds keep their index, YOUR SAMPLE moves up past the new presets
        ArpProcessor p; p.persistTaste = false;
        juce::MemoryBlock mb; p.getStateInformation (mb);
        auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
        auto* st = xml->getChildByName ("ARPEZATOR"); st->removeAttribute ("nPresets");
        for (auto* pr : st->getChildIterator()) { const auto id = pr->getStringAttribute ("id"); if (id == "preset") pr->setAttribute ("value", arpsyn::kPresetsBeforeUrban); if (id == "preset_melody") pr->setAttribute ("value", arpsyn::kPresetsBeforeUrban + 1); if (id == "preset_arp") pr->setAttribute ("value", 1 + arpsyn::DuskPluck); }
        juce::MemoryBlock old; juce::AudioProcessor::copyXmlToBinary (*xml, old);
        ArpProcessor q; q.persistTaste = false; q.setStateInformation (old.getData(), (int) old.getSize());
        auto ch = [&] (const char* id) { return (int) std::lround (q.apvts.getRawParameterValue (id)->load()); };
        std::printf ("older state: preset %d (YOUR SAMPLE = %d), melody lane %d (YOUR SAMPLE = %d), arp lane %d (DUSK PLUCK = %d)\n", ch ("preset"), arpsyn::kPresets, ch ("preset_melody"), arpsyn::kPresets + 1, ch ("preset_arp"), 1 + arpsyn::DuskPluck);
        C.ok (ch ("preset") == arpsyn::kPresets && ch ("preset_melody") == arpsyn::kPresets + 1 && ch ("preset_arp") == 1 + arpsyn::DuskPluck, "STATE: older projects keep YOUR SAMPLE and every factory sound after the new presets were appended");
    }
    // ---- the new sounds: clean at full velocity, no aliasing, the 808's drop and slide
    {
        double worstPk = 0, worstAl = -200; std::string at, atA; bool nan = false;
        arpsyn::testNoNoise() = true;
        for (int pre : { (int) arpsyn::TrapFlute, (int) arpsyn::Bass808, (int) arpsyn::DustyKeys, (int) arpsyn::SubBass, (int) arpsyn::SoulOrgan, (int) arpsyn::WarmPadKeys })
        {
            const bool bass = pre == arpsyn::Bass808 || pre == arpsyn::SubBass;
            for (int pitch : bass ? std::vector<int> { 24, 31, 36, 43, 48 } : std::vector<int> { 48, 60, 72, 84 })
            {
                arpsyn::Engine e; e.prepare (48000, 512); arpsyn::Params P; P.preset = pre; P.delay = 0; P.reverb = 0; P.pump = 0; P.lfoDepth = 0; P.volume = 0.8f;
                std::vector<float> L (48000, 0.0f), R (48000, 0.0f);
                e.leadStart (2, 1, (float) pitch, 1.0f, 36000, P);
                for (int i = 0; i < 48000; i += 512) e.render (L.data() + i, R.data() + i, std::min (512, 48000 - i), P, 120, 0, 0.0, false);
                for (int i = 0; i < 48000; ++i) { if (! std::isfinite (L[(size_t) i])) nan = true; const double a = std::max (std::abs (L[(size_t) i]), std::abs (R[(size_t) i])); if (a > worstPk) { worstPk = a; at = std::string (arpsyn::presetName (pre)) + " @" + std::to_string (pitch); } }
            }
            for (int pitch : bass ? std::vector<int> { 72, 84 } : std::vector<int> { 72, 84, 90, 96 })   // (the same C5..C7 test as the other presets; a near-pure low sine only measures the window's leakage)
            { const double a = soundtest::aliasDb (pre, pitch); if (a > worstAl) { worstAl = a; atA = std::string (arpsyn::presetName (pre)) + " @" + std::to_string (pitch); } }
        }
        arpsyn::testNoNoise() = false;
        std::printf ("new sounds: worst peak %.3f (%s) at full velocity, worst alias %.1f dB (%s)\n", worstPk, at.c_str(), worstAl, atA.c_str());
        C.ok (! nan && worstPk < 0.891, "NEW SOUNDS: clean at full velocity (peak under -1 dBFS, no NaN; worst " + juce::String (worstPk, 3).toStdString() + ")");
        C.ok (worstAl < -55.0, "NEW SOUNDS: no aliasing (worst " + juce::String (worstAl, 1).toStdString() + " dB, " + atA + ")");
    }
    {   // 808: the pitch drop settles in ~35 ms; a slide lands on the new note in <= 120 ms without a new attack
        auto freqAt = [] (const std::vector<float>& x, int from, int len) {   // interpolated positive zero crossings: (count - 1) periods between the first and the last
            double first = -1, last = -1; int zc = 0;
            for (int i = from + 1; i < from + len; ++i) if (x[(size_t) i - 1] <= 0.0f && x[(size_t) i] > 0.0f) { const double t = i - 1 + x[(size_t) i - 1] / (double) (x[(size_t) i - 1] - x[(size_t) i]); if (first < 0) first = t; last = t; ++zc; }
            return zc > 1 ? (zc - 1) * 48000.0 / (last - first) : 0.0; };
        arpsyn::Engine e; e.prepare (48000, 256); arpsyn::Params P; P.preset = arpsyn::Bass808; P.delay = 0; P.reverb = 0; P.pump = 0; P.lfoDepth = 0;
        std::vector<float> L (48000, 0.0f), R (48000, 0.0f);
        e.leadStart (3, 5, 36.0f, 0.9f, 40000, P);
        int pos = 0; auto run = [&] (int n) { for (int k = 0; k < n; k += 256) { e.render (L.data() + pos, R.data() + pos, 256, P, 120, 0, 0.0, false); pos += 256; } };
        run (12000);   // 250 ms on C2
        const double f1 = freqAt (L, 4800, 7000);
        e.leadGlide (5, 43.0f, 2400); e.leadLegato (5, 43.0f, 0.9f);
        run (12000);
        const double f2 = freqAt (L, 12000 + 6000, 6000);
        std::printf ("808: %.1f Hz on C2 (65.4), %.1f Hz after the slide to G2 (98.0)\n", f1, f2);
        C.ok (std::abs (f1 - 65.4) < 4 && std::abs (f2 - 98.0) < 5, "808 BASS: in tune after the pitch drop, the slide lands on the new note");
    }
    {   // level match (BS.1770, the mood sounds as GENERATE sets them up)
        double old = 0; for (int m = 0; m < 3; ++m) for (uint32_t s : { 11u, 515u }) old += soundtest::lufs (moodRender (m, s, 48000), 48000);
        old /= 6;
        for (int m : { (int) Trap, (int) HipHop })
        {
            double l = 0; for (uint32_t s : { 11u, 515u, 2024u, 128u }) l += soundtest::lufs (moodRender (m, s, 48000), 48000);
            l /= 4;
            std::printf ("loudness %s %.1f LUFS vs the old moods %.1f\n", modeName (m).c_str(), l, old);
            C.ok (std::abs (l - old) <= 1.5, modeName (m) + ": level-matched to the old moods within 1.5 LU");
        }
    }
    // ---- variety
    {
        const int N = 50; std::vector<arptest::VarietyRow> rows, rowsFurther;
        for (int m = 0; m < kMoods; ++m)
        {
            rows.push_back (arptest::varietyOf (modeName (m), N, [m] (Idea& I, uint32_t s, const arptest::Recent&) { I.mode = m; I.seed = s; applyModeDefaults (I); newIdea (I); }));
            rowsFurther.push_back (arptest::varietyOf (modeName (m), N, [m] (Idea& I, uint32_t s, const arptest::Recent& r) { I.mode = m; I.seed = s; applyModeDefaults (I); if (urbanMood (m)) newIdeaFurther (I, r); else newIdea (I); }));
        }
        std::printf ("VARIETY (N = %d; TRAP / HIP HOP with their PUSH FURTHER rule, the old three as shipped):\n", N);
        arptest::printVarietyHeader(); for (auto& r : rowsFurther) arptest::printVarietyRow (r);
        double oldMean = 0, oldMax = 0, oldDupMin = 100; for (int m = 0; m < 3; ++m) { oldMean += rows[(size_t) m].melDist / 3; oldMax = std::max (oldMax, rows[(size_t) m].melDist); oldDupMin = std::min (oldDupMin, rows[(size_t) m].nearDup); }
        for (int m : { (int) Trap, (int) HipHop })
        {
            const auto& r = rowsFurther[(size_t) m];
            std::printf ("%s: %.2fx the old mean distance, %.2fx less similar (1 - distance), near-duplicates %.1f%% vs the old best %.1f%%\n", r.mood.c_str(), r.melDist / oldMean, (1 - oldMean) / std::max (1e-9, 1 - r.melDist), r.nearDup, oldDupMin);
            C.ok (r.melDist > oldMax && r.succDist > r.melDist * 0.85 && r.nearDup < oldDupMin, r.mood + ": more varied than every old mood (distance, vs the last 3, near-duplicates)");
        }
        // cross-check: the engine's distance = the measure's
        const Idea a = make (Trap, 5), b = make (Trap, 6);
        C.ok (std::abs (melodyDistance (a.lanes[LaneMelody].notes, a.H.key, b.lanes[LaneMelody].notes, b.H.key) - arptest::melodyDistance (a.lanes[LaneMelody].notes, a.H.key, b.lanes[LaneMelody].notes, b.H.key)) < 1e-12, "VARIETY: the PUSH FURTHER rule uses exactly the measured distance");
        // PUSH FURTHER keeps the seed: recalling it gives the same idea
        Idea x; x.mode = Trap; x.seed = 9; applyModeDefaults (x); std::vector<RecentIdea> rec { recentOf (make (Trap, 9)), recentOf (make (Trap, 10)) };
        newIdeaFurther (x, rec); const Idea y = make (Trap, x.seed);
        C.ok (arptest::ideaFingerprint (x) == arptest::ideaFingerprint (y), "VARIETY: a PUSH FURTHER idea comes back exactly from its seed (history / recall)");
    }
    // ---- HOOK LAB in the new moods
    {
        auto hookStats = [] (const Idea& I, int style, int bars, int count, double& meanDist, int& bad, int& n) {
            auto hp = hookParams (I, bars, 1234); hp.count = count; hp.style = style;
            const auto hooks = generateHooks (I, hp); n = (int) hooks.size(); bad = 0; double s = 0; int k = 0;
            for (auto& h : hooks) bad += badMelodyNotes (I, h.notes);
            for (size_t i = 0; i < hooks.size(); ++i) for (size_t j = i + 1; j < hooks.size(); ++j) { s += melodyDistance (hooks[i].notes, I.H.key, hooks[j].notes, I.H.key); ++k; }
            meanDist = k ? s / k : 0; };
        double oldD = 0; for (int m = 0; m < 3; ++m) { double d; int b, n; hookStats (make (m, 31), StyleAuto, 4, 20, d, b, n); oldD += d / 3; }
        for (int m : { (int) Trap, (int) HipHop })
        {
            double d; int bad, n; const Idea I = make (m, 31);
            hookStats (I, StyleAuto, 4, 20, d, bad, n);
            std::printf ("HOOK LAB %s: %d hooks, notes off %d, pairwise distance %.3f (old moods %.3f)\n", modeName (m).c_str(), n, bad, d, oldD);
            C.ok (n >= 18 && bad == 0 && d > oldD, "HOOK LAB " + modeName (m) + ": 20 hooks from the mood's engine, in its scales, more varied than the old moods'");
            int okStyles = 0;
            for (int st : { m == Trap ? (int) StyleDark : (int) StyleBoomBap, m == Trap ? (int) StyleTriumphant : (int) StyleSoulful, m == Trap ? (int) StyleHypnoLoop : (int) StyleLaidBack })
            { double d2; int b2, n2; hookStats (I, st, 4, 6, d2, b2, n2); okStyles += n2 >= 5 && b2 == 0; std::printf ("  style %s: %d hooks, notes off %d, distance %.3f\n", styleName (st), n2, b2, d2); }
            double d8; int b8, n8; hookStats (I, StyleAuto, 8, 6, d8, b8, n8);
            auto hp = hookParams (I, 4, 99); hp.count = 6; hp.candy = CandyHook; const auto hc = generateHooks (I, hp);
            std::printf ("  8 bars: %d hooks, notes off %d; candy: %zu hooks\n", n8, b8, hc.size());
            C.ok (okStyles == 3 && n8 >= 5 && b8 == 0 && hc.size() >= 5, "HOOK LAB " + modeName (m) + ": its three styles, 8 bars and EAR CANDY work");
        }
        C.ok (styleAt (Trap, 6) == StyleDark && styleAt (HipHop, 8) == StyleLaidBack && styleButton (Sunrise, StyleDark) == 0 && styleCountFor (Midnight) == 6 && std::string (styleName (StylePlayful)) == "PLAYFUL",
              "HOOK LAB: the new styles append after the classic six (old indices unchanged)");
    }
}

// WAV + MIDI demos: every flavour of both moods, 8 bars, the mood sound as GENERATE sets it up
inline void renders (const juce::File& dir)
{
    dir.createDirectory();
    for (int mood : { (int) Trap, (int) HipHop })
        for (int f = 0; f < kFlavours; ++f)
        {
            ArpProcessor p; p.persistTaste = false;
            p.generate (mood, false, false);
            p.change ([f] (Idea& X) { X.seed = 1000 + (uint32_t) f * 37; X.flavour = f; newIdea (X); }, false);
            p.applyFlavourLanes (true);
            const Idea& I = p.idea();
            juce::String name = juce::String (modeName (mood)).replace (" ", "") + "_" + juce::String (flavourName (mood, f)).replace (" ", "_");
            const auto wav = renderIdea (p, dir, name, 8, I.g.bpm);
            const auto mid = ideaMidi (I, 15, false, I.g.bpm); dir.getChildFile (name + ".mid").replaceWithData (mid.data(), mid.size());
            std::string ch; for (auto& c : I.chords) ch += c.name + " ";
            std::printf ("render %s: %s %s %d bpm swing %.0f%%, chords %s| bass '%s', melody '%s' -> %s\n", name.toRawUTF8(), keyName (I.g.key, I.g.scale).c_str(), I.colour.c_str(), I.g.bpm, grooveToSwing (I.groove), ch.c_str(),
                         urbanBassArchName (mood, I.urbanBassArch), urbanMelArchName (mood, I.urbanMelArch), wav.getFullPathName().toRawUTF8());
        }
}
} // namespace urbantest
