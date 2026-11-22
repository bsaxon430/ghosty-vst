#pragma once
// Ghost Graph -- splitting a song into its parts for DRAG: Kick, Snare, Clap, Hi-hat, Bass, Lead, Pad ...
//
// Every voice of every scene is listened to (its line sampled across the page) and given a part:
//   * drums: a voice named kick / boom, or one that plays short hits that fall fast in pitch, is the Kick; with KIT on,
//     hats are the Hi-hat (ohat / open: the Open hat), snare / clap / rim / shaker are themselves, a voice named tom
//     is a Tom. Drums of one kind are one part, whatever scene they are in.
//   * tones: a name that says what it is wins (bass / sub, lead, arp, pad / drone, chord / harm / stab); otherwise low
//     (mostly under C3) is Bass, a line held nearly all the time on few notes is a Pad, anything else is a Lead. A
//     named voice (riff, acid ...) is its own part in every scene it plays in; unnamed tones of one kind are one part
//     (all the unnamed bass lines are "Bass"). On the Re / Im plane the two voices are "Re" and "Im".
// Each part then gets its notes for MIDI: tones as the nearest semitone (a glide steps through them), drums as one
// General MIDI drum note per hit (Kick 36, Snare 38, Clap 39, Hi-hat 42 ...).

#include "GraphExpr.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

namespace gg
{
struct StemPart { int scene = 0, voice = 0; };

struct Stem
{
    std::string key;            // what joins voices into this part
    std::string name;           // "Kick", "Lead · riff", "Bass 2"
    std::string role;           // Kick, Snare, Clap, Rim, Tom, Hi-hat, Open hat, Shaker, Bass, Lead, Pad
    bool drum = false;
    int gm = 0;                 // its General MIDI drum note (drums)
    std::vector<StemPart> parts;
    int order = 0;
};

struct StemNote { double start = 0, length = 0; int note = 60, velocity = 100; };

// The song as DRAG sees it: which scenes play, in order, and how long each one lasts.
struct SongPlan
{
    std::vector<int> scenes;     // in play order
    std::vector<double> starts;  // in beats
    double beats = 0;            // the whole length
    std::vector<double> speeds, pitches;
    double rate = 1.0;
    int tune = 0;                // semitones (y = 0 is A3 = MIDI 57, plus tune)
    bool kit = true;
};

inline SongPlan planSong (const std::vector<Scene>& sc, bool chain, int current, double rate, int tune, bool kit)
{
    SongPlan s;
    s.rate = rate > 0 ? rate : 1.0;
    s.tune = tune; s.kit = kit;
    for (auto& c : sc) { s.speeds.push_back (std::max (0.1, c.speed)); s.pitches.push_back (c.pitch > 0 ? c.pitch : 1.0); }
    if (sc.empty()) return s;
    if (chain) for (int i = 0; i < (int) sc.size(); ++i) s.scenes.push_back (i);
    else s.scenes.push_back (std::clamp (current, 0, (int) sc.size() - 1));
    for (int k : s.scenes) { s.starts.push_back (s.beats); s.beats += 16.0 / s.speeds[(size_t) k] / s.rate; }
    return s;
}

namespace stems_detail
{
    constexpr int kSteps = 1024;   // 1/64 of x on the 16-unit page
    inline std::string lower (std::string n) { for (auto& c : n) c = (char) std::tolower ((unsigned char) c); return n; }
    inline bool has (const std::string& n, const char* w) { return n.find (w) != std::string::npos; }

    struct Listen
    {
        std::vector<double> midi;          // NaN where silent
        double audible = 0;                // the share of the page it plays
        int onsets = 0;
        double hitBeats = 0;               // how long a hit lasts, on average (in x units)
        double fall = 0;                   // how far a hit falls in pitch, on average (semitones)
        double median = 0;                 // its middle note
        double moves = 0;                  // how often it changes note, per x unit it plays
    };

    inline Listen listen (Program& p, int v, double pitch, int tune)
    {
        Listen L;
        L.midi.assign (kSteps, std::numeric_limits<double>::quiet_NaN());
        int on = 0; double fallSum = 0; double hitStart = 0; bool in = false;
        std::vector<double> heard;
        for (int i = 0; i < kSteps; ++i)
        {
            p.setX (-8.0 + 16.0 * i / kSteps);
            const double y = p.voice (v);
            if (std::isfinite (y))
            {
                const double m = 57.0 + tune + 12.0 * y * pitch;
                const double hz = 220.0 * std::exp2 ((m - 57.0) / 12.0);
                if (hz >= 8.0 && hz <= 20000.0) L.midi[(size_t) i] = m;
            }
            const double m = L.midi[(size_t) i];
            if (std::isfinite (m))
            {
                if (in && std::lround (m) != std::lround (L.midi[(size_t) i - 1])) L.moves += 1;
                ++on; heard.push_back (m);
                if (! in) { ++L.onsets; hitStart = m; in = true; }
            }
            else if (in) { fallSum += hitStart - L.midi[(size_t) i - 1]; in = false; }
        }
        if (in) fallSum += hitStart - L.midi.back();
        L.audible = (double) on / kSteps;
        if (L.onsets > 0) { L.hitBeats = 16.0 * L.audible / L.onsets; L.fall = fallSum / L.onsets; }
        if (on > 0) L.moves /= 16.0 * L.audible;
        if (! heard.empty())
        {
            std::nth_element (heard.begin(), heard.begin() + (long) heard.size() / 2, heard.end());
            L.median = heard[heard.size() / 2];
        }
        return L;
    }

    inline int roleOrder (const std::string& r)
    {
        static const char* order[] = { "Kick", "Snare", "Clap", "Rim", "Tom", "Hi-hat", "Open hat", "Shaker", "Bass", "Lead", "Arp", "Chords", "Pad" };
        for (int i = 0; i < 13; ++i) if (r == order[i]) return i;
        return 13;
    }
}

// The parts of the song, in a sensible order (drums first, then bass, leads, pads).
inline std::vector<Stem> findStems (const std::vector<Scene>& scenes, const SongPlan& plan)
{
    using namespace stems_detail;
    std::vector<Stem> out;
    std::map<std::string, size_t> byKey;
    for (int k : plan.scenes)
    {
        auto p = Program::compile (scenes[(size_t) k].text);
        if (! p->ok()) continue;
        for (int v = 0; v < p->numVoices(); ++v)
        {
            const Listen L = listen (*p, v, plan.pitches[(size_t) k], plan.tune);
            if (L.audible <= 0) continue;
            std::string name = p->voiceName (v);
            if (p->isPlane()) name = std::string (v % 2 == 0 ? "Re" : "Im") + (v >= 2 ? " " + std::to_string (v / 2 + 1) : std::string());
            const std::string n = lower (name);
            const int kind = plan.kit ? p->voiceKind (v) : 0;
            std::string role; bool drum = true; int gm = 0;
            if (kind == 1) { if (has (n, "ohat") || has (n, "open")) { role = "Open hat"; gm = 46; } else { role = "Hi-hat"; gm = 42; } }
            else if (kind == 2)
            {
                if (has (n, "clap")) { role = "Clap"; gm = 39; }
                else if (n == "rim") { role = "Rim"; gm = 37; }
                else if (has (n, "shaker")) { role = "Shaker"; gm = 70; }
                else { role = "Snare"; gm = 38; }
            }
            else if (has (n, "kick") || n == "boom" || n == "bd"
                     || (name.empty() && L.onsets >= 2 && L.hitBeats <= 0.75 && L.fall >= 9.0))   // short hits that drop like a kick
            { role = "Kick"; gm = 36; }
            else if (has (n, "tom")) { role = "Tom"; gm = 45; }
            else
            {
                drum = false;
                if (has (n, "bass") || n == "sub") role = "Bass";
                else if (has (n, "lead")) role = "Lead";
                else if (has (n, "arp")) role = "Arp";
                else if (has (n, "pad") || has (n, "drone")) role = "Pad";
                else if (has (n, "chord") || has (n, "harm") || has (n, "stab")) role = "Chords";
                else if (L.median < 48.0) role = "Bass";
                else if (L.audible > 0.85 && L.onsets <= 2 && L.moves < 0.5) role = "Pad";
                else role = "Lead";
            }
            std::string key, shown;
            if (drum) { key = "d:" + role; shown = role; }
            else if (p->isPlane()) { key = "n:" + name; shown = name; }              // Re, Im
            else if (! name.empty() && lower (role) != n) { key = "n:" + name; shown = role + " \xc2\xb7 " + name; }
            else { key = "u:" + role; shown = role; }   // a voice named bass is just the Bass
            auto it = byKey.find (key);
            if (it == byKey.end())
            {
                Stem s;
                s.key = key; s.name = shown; s.role = role; s.drum = drum; s.gm = gm;
                s.order = roleOrder (role) * 100 + (int) out.size();
                byKey[key] = out.size();
                out.push_back (s);
                it = byKey.find (key);
            }
            out[it->second].parts.push_back ({ k, v });
        }
    }
    std::stable_sort (out.begin(), out.end(), [] (const Stem& a, const Stem& b) { return a.order < b.order; });
    return out;
}

// The notes of one part, in beats from the start of the song.
inline std::vector<StemNote> stemNotes (const std::vector<Scene>& scenes, const SongPlan& plan, const Stem& stem)
{
    using namespace stems_detail;
    std::vector<StemNote> notes;
    for (size_t si = 0; si < plan.scenes.size(); ++si)
    {
        const int k = plan.scenes[si];
        bool any = false;
        for (auto& pt : stem.parts) any = any || pt.scene == k;
        if (! any) continue;
        auto p = Program::compile (scenes[(size_t) k].text);
        if (! p->ok()) continue;
        const double beatPerStep = 16.0 / kSteps / plan.speeds[(size_t) k] / plan.rate;
        for (auto& pt : stem.parts)
        {
            if (pt.scene != k) continue;
            const Listen L = listen (*p, pt.voice, plan.pitches[(size_t) k], plan.tune);
            int cur = -1; double from = 0;
            auto close = [&] (int i)
            {
                if (cur < 0) return;
                const double end = plan.starts[si] + i * beatPerStep;
                double len = end - from;
                if (stem.drum) len = std::clamp (len, 1.0 / 32.0, 1.0);
                if (len > 1e-6) notes.push_back ({ from, len, cur, 100 });
                cur = -1;
            };
            for (int i = 0; i < kSteps; ++i)
            {
                const double m = L.midi[(size_t) i];
                if (! std::isfinite (m)) { close (i); continue; }
                const int want = stem.drum ? stem.gm : (int) std::lround (m);
                if (want < 0 || want > 127) { close (i); continue; }
                if (cur >= 0 && (stem.drum || want == cur)) continue;   // a drum hit lasts until it goes quiet
                close (i);
                cur = want; from = plan.starts[si] + i * beatPerStep;
            }
            close (kSteps);
        }
    }
    std::sort (notes.begin(), notes.end(), [] (const StemNote& a, const StemNote& b) { return a.start < b.start || (a.start == b.start && a.note < b.note); });
    return notes;
}

// which voices of each scene a part plays (for rendering it on its own)
inline std::vector<uint16_t> stemMask (const Stem& s, int numScenes)
{
    std::vector<uint16_t> m ((size_t) std::max (0, numScenes), 0);
    for (auto& pt : s.parts) if (pt.scene >= 0 && pt.scene < numScenes && pt.voice < 16) m[(size_t) pt.scene] |= (uint16_t) (1u << pt.voice);
    return m;
}
} // namespace gg
