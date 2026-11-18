/*
 * Ghost Bass - a bass instrument built on Surge XT.
 * Copyright 2026 the Ghost Bass authors. Based on Surge XT by the Surge Synth Team.
 * Released under the GNU General Public Licence v3 or later (see LICENSE).
 *
 * The bass sequencer: 16 or 32 sixteenth-note steps. Each step has on/off, a note (semitones
 * from the key's root), octave, accent, slide, its own gate (short / normal / long / tie) and a
 * retrigger count (1-4 hits inside the step). Global: key, scale, groove template and amount,
 * gate length, and CHORD mode (the pattern plays while you hold notes: one note transposes it,
 * a chord makes every step snap to the chord's tones).
 *
 * Runs on the audio thread from the host's song position (or its own clock in the standalone),
 * so it follows the DAW exactly. Every setting is a plain atomic: the UI and the patch loader edit
 * it lock-free, and the audio thread never allocates or blocks here.
 *
 * generate(): a two-bar phrase in a key and scale, built the way a bass player would: a rhythm
 * family (offbeat house, rolling sixteenths, 3-3-2 syncopation, half-time dub, acid run), a
 * one-beat or two-beat motif that repeats, a variation on the answering bar, the root on the
 * downbeats, chord tones on strong steps, passing tones and slides into them, rests, ghost notes,
 * and a fill (retriggers / octave jump) at the end of the phrase.
 */
#ifndef GHOSTBASS_SRC_GHOST_GHOSTSEQUENCER_H
#define GHOSTBASS_SRC_GHOST_GHOSTSEQUENCER_H

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace ghost
{

struct SeqStep
{
    bool on{false};
    int semi{0}; // semitones above the root (-24..+24)
    int oct{0};  // -1, 0, +1
    bool accent{false};
    bool slide{false};
    int gate{1};    // 0 short, 1 normal (the global gate), 2 long, 3 tie
    int ratchet{1}; // 1..4 hits inside the step
};

class Sequencer
{
  public:
    static constexpr int kMaxSteps = 32;

    enum Scale
    {
        MINOR = 0,
        PHRYGIAN,
        DORIAN,
        HARM_MINOR,
        MAJOR,
        MIXOLYDIAN,
        MINOR_PENT,
        n_scales
    };
    enum Groove
    {
        STRAIGHT = 0,
        SWING16,
        SHUFFLE8,
        PUSH,
        LAZY,
        n_grooves
    };
    enum Style
    {
        ANY = -1,
        OFFBEAT = 0,
        ROLLING,
        SYNCOPATED,
        HALFTIME,
        ACID,
        n_styles
    };

    std::atomic<bool> playing{false};
    std::atomic<int> length{16};
    std::atomic<int> root{36}; // MIDI note of the key's root (C2)
    std::atomic<int> scale{MINOR};
    std::atomic<int> groove{STRAIGHT};
    std::atomic<float> swing{0.5f}; // groove amount 0..1
    std::atomic<float> gate{0.55f};
    std::atomic<bool> chordMode{false};
    std::atomic<int> playhead{-1}; // for the UI
    std::atomic<int> lastStyle{0}; // what generate() picked (for the UI)

    Sequencer()
    {
        for (auto &s : packed)
            s.store(0);
        for (auto &h : held)
            h.store(0);
    }

    SeqStep get(int i) const { return unpack(packed[std::clamp(i, 0, kMaxSteps - 1)].load()); }
    void set(int i, const SeqStep &s) { packed[std::clamp(i, 0, kMaxSteps - 1)].store(pack(s)); }
    uint32_t raw(int i) const { return packed[std::clamp(i, 0, kMaxSteps - 1)].load(); }
    void setRaw(int i, uint32_t v) { packed[std::clamp(i, 0, kMaxSteps - 1)].store(v); }

    static const char *scaleName(int s)
    {
        static const char *n[] = {"MINOR", "PHRYGIAN", "DORIAN", "HARM MINOR", "MAJOR", "MIXOLYDIAN", "MINOR PENT"};
        return n[std::clamp(s, 0, (int)n_scales - 1)];
    }
    static const char *grooveName(int g)
    {
        static const char *n[] = {"STRAIGHT", "SWING 16", "SHUFFLE 8", "PUSH", "LAZY"};
        return n[std::clamp(g, 0, (int)n_grooves - 1)];
    }
    static const char *styleName(int s)
    {
        static const char *n[] = {"OFFBEAT", "ROLLING", "SYNCOPATED", "HALF-TIME", "ACID"};
        return n[std::clamp(s, 0, (int)n_styles - 1)];
    }
    static const char *noteName(int midi)
    {
        static const char *n[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        return n[((midi % 12) + 12) % 12];
    }
    static const int *scaleSteps(int s)
    {
        static const int sc[n_scales][7] = {{0, 2, 3, 5, 7, 8, 10}, {0, 1, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 9, 10},
                                            {0, 2, 3, 5, 7, 8, 11}, {0, 2, 4, 5, 7, 9, 11}, {0, 2, 4, 5, 7, 9, 10},
                                            {0, 3, 5, 7, 10, 12, 15}};
        return sc[std::clamp(s, 0, (int)n_scales - 1)];
    }

    // ---- pattern generation (message thread) ----
    void generate(int scaleIdx, float density, uint32_t seed, int style = ANY)
    {
        scale.store(std::clamp(scaleIdx, 0, (int)n_scales - 1));
        const int *deg = scaleSteps(scaleIdx);
        std::mt19937 r(seed * 2654435761u + 17u);
        auto U = [&]() { return std::uniform_real_distribution<float>(0, 1)(r); };
        auto pick = [&](int n) { return (int)(r() % (uint32_t)std::max(1, n)); };
        density = std::clamp(density, 0.15f, 1.f);
        if (style < 0 || style >= n_styles)
            style = pick(n_styles);
        lastStyle.store(style);

        // rhythm templates: probability of a note on each step of one bar
        static const float rhythm[n_styles][16] = {
            // OFFBEAT: house - the "and" of every beat, a pickup before beat 1
            {0.25f, 0, 1, 0.2f, 0.15f, 0, 1, 0.25f, 0.2f, 0, 1, 0.2f, 0.15f, 0, 1, 0.45f},
            // ROLLING: sixteenths with the 1 left for the kick
            {0.15f, 0.9f, 0.85f, 0.9f, 0.15f, 0.9f, 0.85f, 0.9f, 0.15f, 0.9f, 0.85f, 0.9f, 0.15f, 0.9f, 0.85f, 0.9f},
            // SYNCOPATED: 3-3-2 / tresillo
            {1, 0.1f, 0.1f, 1, 0.1f, 0.15f, 1, 0.2f, 0.4f, 0.15f, 0.5f, 1, 0.2f, 0.15f, 0.7f, 0.3f},
            // HALFTIME: dub / dubstep - long notes on 1 and the "a" of 2
            {1, 0, 0, 0, 0, 0, 0.3f, 0.6f, 0, 0, 0.5f, 0, 0.2f, 0, 0.5f, 0.2f},
            // ACID: busy, accent-driven
            {1, 0.55f, 0.8f, 0.55f, 0.7f, 0.5f, 0.8f, 0.6f, 0.85f, 0.5f, 0.75f, 0.6f, 0.7f, 0.55f, 0.85f, 0.65f},
        };
        const int len = std::clamp(length.load(), 1, kMaxSteps);
        const int motifLen = (style == HALFTIME || pick(3) == 0) ? 8 : 4;

        // chord tones of the key (1, 3, 5, 7) and a weighted choice of degrees
        auto degSemi = [&](int d) { return deg[((d % 7) + 7) % 7] % 12 + 12 * (int)std::floor(d / 7.0); };
        auto chordTone = [&]() {
            static const int ct[] = {0, 0, 0, 4, 2, 6, 7};
            return ct[pick(7)];
        };

        // 1. motif
        std::array<SeqStep, 8> motif{};
        int prevDeg = 0;
        for (int k = 0; k < motifLen; ++k)
        {
            SeqStep s;
            float p = rhythm[style][k % 16] * (0.55f + 0.65f * density);
            s.on = U() < p;
            int d = 0;
            if (k > 0 && s.on)
            {
                float u = U();
                if (u < 0.40f)
                    d = 0; // the root is the bass player's home
                else if (u < 0.75f)
                    d = chordTone();
                else
                    d = std::clamp(prevDeg + (U() < 0.5f ? 1 : -1), -2, 8); // step-wise motion
            }
            s.semi = degSemi(d);
            s.oct = (k > 0 && s.on && U() < (style == ACID ? 0.3f : 0.12f)) ? 1 : 0;
            if (style == HALFTIME && s.on && U() < 0.3f)
                s.oct = -1;
            s.accent = s.on && ((k % 4) == 0 || U() < (style == ACID ? 0.35f : 0.15f));
            s.slide = s.on && k > 0 && std::abs(d - prevDeg) <= 2 && U() < (style == ACID ? 0.35f : 0.15f);
            s.gate = style == HALFTIME ? (U() < 0.6f ? 3 : 2) : (U() < 0.2f ? 0 : (U() < 0.15f ? 2 : 1));
            if (style == OFFBEAT && s.on)
                s.gate = U() < 0.7f ? 0 : 1; // short, bouncy
            s.ratchet = 1;
            motif[k] = s;
            if (s.on)
                prevDeg = d;
        }

        // 2. lay it out over the phrase: repeat, then answer with a variation
        for (int i = 0; i < kMaxSteps; ++i)
        {
            SeqStep s = motif[i % motifLen];
            const int inBar = i % 16;
            const bool answerBar = (i / 16) % 2 == 1 || (len <= 16 && inBar >= 8);
            const bool lastBeat = (i % std::max(16, len)) >= std::max(16, len) - 4 && len >= 16;
            if (answerBar && s.on && inBar != 0 && U() < 0.35f)
            {
                // variation: move to another chord tone or answer with the fifth / seventh
                s.semi = degSemi(U() < 0.5f ? 4 : (U() < 0.5f ? 6 : 2));
            }
            if (answerBar && !s.on && rhythm[style][inBar] > 0.3f && U() < 0.3f * density)
            {
                s.on = true; // the answer is a little busier
                s.semi = degSemi(chordTone());
                s.gate = 0;
            }
            if (lastBeat)
            {
                // fill / turnaround into the next phrase
                if (U() < 0.5f)
                {
                    s.on = true;
                    s.semi = degSemi(U() < 0.5f ? 4 : 6);
                }
                if (s.on && U() < 0.3f)
                    s.ratchet = 2 + (U() < 0.3f ? 1 : 0);
                if (s.on && U() < 0.25f)
                    s.oct = 1;
                if (inBar == 15 && U() < 0.5f)
                {
                    s.on = true;
                    s.semi = degSemi(U() < 0.5f ? -1 : 1); // leading tone into the root
                    s.slide = true;
                }
            }
            if (inBar == 0 && style != OFFBEAT && style != ROLLING)
            {
                s.on = true; // the root on every bar's downbeat
                s.semi = 0;
                s.oct = 0;
                s.accent = true;
                s.ratchet = 1;
            }
            if (!s.on)
            {
                s.slide = false;
                s.accent = false;
                s.ratchet = 1;
            }
            set(i, s);
        }
        // never an empty bar
        for (int b = 0; b < kMaxSteps; b += 16)
        {
            bool any = false;
            for (int i = b; i < b + 16; ++i)
                any = any || get(i).on;
            if (!any)
            {
                SeqStep s = get(b + 2);
                s.on = true;
                s.semi = 0;
                set(b + 2, s);
            }
        }
    }

    // ---- CHORD mode: held notes (audio thread: called from MIDI handling) ----
    void hold(int note, bool on)
    {
        if (note < 0 || note > 127)
            return;
        auto &w = held[note >> 6];
        uint64_t bit = 1ull << (note & 63);
        if (on)
            w.fetch_or(bit, std::memory_order_relaxed);
        else
            w.fetch_and(~bit, std::memory_order_relaxed);
    }
    void releaseAllHeld()
    {
        for (auto &h : held)
            h.store(0, std::memory_order_relaxed);
    }
    int heldCount() const
    {
        int c = 0;
        for (auto &h : held)
            c += __builtin_popcountll(h.load(std::memory_order_relaxed));
        return c;
    }

    // the note a step plays (root + offsets, mapped onto a held chord in CHORD mode); -1 = silent
    int noteFor(const SeqStep &s) const
    {
        int base = root.load(std::memory_order_relaxed);
        int semi = s.semi;
        if (chordMode.load(std::memory_order_relaxed))
        {
            int tones[16], n = 0, low = -1;
            for (int k = 0; k < 128 && n < 16; ++k)
                if (held[k >> 6].load(std::memory_order_relaxed) & (1ull << (k & 63)))
                {
                    if (low < 0)
                        low = k;
                    tones[n++] = k - low;
                }
            if (n == 0)
                return -1;
            // the pattern is written in the key's root: move it to the lowest held note,
            // keeping the octave the pattern sits in
            base = low;
            while (base - root.load(std::memory_order_relaxed) > 6)
                base -= 12;
            while (root.load(std::memory_order_relaxed) - base > 6)
                base += 12;
            if (n > 1)
            {
                // snap to the nearest chord tone (any octave); ties go down
                int best = 0, bestD = 99;
                for (int o = -2; o <= 2; ++o)
                    for (int k = 0; k < n; ++k)
                    {
                        int t = tones[k] % 12 + 12 * o;
                        int d = std::abs(t - semi);
                        if (d < bestD || (d == bestD && t < best))
                        {
                            bestD = d;
                            best = t;
                        }
                    }
                semi = best;
            }
        }
        return std::clamp(base + semi + 12 * s.oct, 0, 127);
    }

    // groove: timing offset (fraction of a step) and velocity scale for a step's place in the beat
    void grooveAt(int idx, double &offset, float &vel) const
    {
        const int g = groove.load(std::memory_order_relaxed);
        const float a = std::clamp(swing.load(std::memory_order_relaxed), 0.f, 1.f);
        const int p = idx & 3;
        static const float off[n_grooves][4] = {{0, 0, 0, 0},
                                                {0, 0.33f, 0, 0.33f},       // 16th swing, up to triplet
                                                {0, 0.17f, 0.33f, 0.17f},   // 8th shuffle
                                                {0, -0.1f, -0.04f, -0.1f},  // rushing
                                                {0, 0.1f, 0.16f, 0.12f}};   // laid back
        static const float vl[n_grooves][4] = {{1, 1, 1, 1},
                                               {1, 0.85f, 0.95f, 0.85f},
                                               {1, 0.85f, 0.9f, 0.8f},
                                               {1, 0.8f, 0.95f, 0.8f},
                                               {1, 0.85f, 0.9f, 0.8f}};
        const int gi = std::clamp(g, 0, (int)n_grooves - 1);
        offset = off[gi][p] * a;
        vel = 1.f + (vl[gi][p] - 1.f) * a;
    }

    // Called once per Surge block (32 samples) on the audio thread.
    // ppq: song position (quarter notes) at the block start; ppqPerSample: tempo / (60 sr)
    template <typename NoteOn, typename NoteOff>
    void tick(double ppq, double ppqPerSample, int nSamples, NoteOn noteOn, NoteOff noteOff)
    {
        const bool chordSilent = chordMode.load(std::memory_order_relaxed) && heldCount() == 0;
        if (!playing.load(std::memory_order_relaxed) || chordSilent)
        {
            if (sounding >= 0)
            {
                noteOff(sounding);
                sounding = -1;
            }
            playhead.store(chordSilent && playing.load(std::memory_order_relaxed) ? playhead.load() : -1,
                           std::memory_order_relaxed);
            return;
        }
        const int len = std::clamp(length.load(std::memory_order_relaxed), 1, kMaxSteps);
        const double stepQ = 0.25; // a sixteenth note
        const double g = std::clamp(gate.load(std::memory_order_relaxed), 0.05f, 1.f);
        const double ppqEnd = ppq + ppqPerSample * nSamples;
        long firstStep = (long)std::floor(ppq / stepQ) - 1;
        long lastS = (long)std::floor(ppqEnd / stepQ) + 1;
        for (long n = firstStep; n <= lastS; ++n)
        {
            if (n < 0)
                continue;
            const int idx = (int)(n % len);
            const SeqStep s = get(idx);
            double off;
            float gv;
            grooveAt(idx, off, gv);
            const double start = (n + off) * stepQ;
            const SeqStep nx = get((int)((n + 1) % len));
            const bool legatoNext = s.on && (s.slide || s.gate == 3) && nx.on;
            const int hits = std::clamp(s.ratchet, 1, 4);
            for (int h = 0; h < hits; ++h)
            {
                const double hs = start + h * stepQ / hits;
                if (hs < ppq || hs >= ppqEnd)
                    continue;
                if (h == 0)
                    playhead.store(idx, std::memory_order_relaxed);
                if (!s.on)
                    continue;
                const int note = noteFor(s);
                if (note < 0)
                    continue;
                double gl;
                if (hits > 1)
                    gl = 0.5 * stepQ / hits;
                else if (legatoNext)
                    gl = stepQ * 1.08;
                else
                {
                    static const double gm[4] = {0.3, -1, 0.85, 1.0};
                    gl = (gm[std::clamp(s.gate, 0, 3)] < 0 ? g : gm[std::clamp(s.gate, 0, 3)]) * stepQ;
                }
                const int vel = std::clamp((int)std::lround((s.accent ? 120.f : 88.f) * (h ? 0.85f : gv)), 1, 127);
                const int prev = sounding;
                if (prev == note && prevLegato && h == 0)
                {
                    soundingEnd = hs + gl; // a tie into the same note: just hold it longer
                    prevLegato = (h == hits - 1) && legatoNext;
                    continue;
                }
                if (prev >= 0 && !(prevLegato && h == 0))
                {
                    noteOff(prev); // a new attack
                    sounding = -1;
                }
                noteOn(note, vel);
                // slide / tie: the new note starts before the old ends (legato -> Surge glides)
                if (sounding >= 0 && sounding != note)
                    noteOff(sounding);
                sounding = note;
                soundingEnd = hs + gl;
                prevLegato = (h == hits - 1) && legatoNext;
            }
        }
        if (sounding >= 0 && soundingEnd < ppqEnd)
        {
            noteOff(sounding);
            sounding = -1;
            prevLegato = false;
        }
    }

    // the pattern as note events (start and length in quarter notes) - for MIDI drag-out
    struct Ev
    {
        double start, len;
        int note, vel;
    };
    std::vector<Ev> events(int bars = 0) const
    {
        std::vector<Ev> out;
        const int len = std::clamp(length.load(), 1, kMaxSteps);
        const int total = bars > 0 ? bars * 16 : len;
        const double stepQ = 0.25, g = std::clamp(gate.load(), 0.05f, 1.f);
        for (int n = 0; n < total; ++n)
        {
            const SeqStep s = get(n % len);
            if (!s.on)
                continue;
            const SeqStep nx = get((n + 1) % len);
            const bool legatoNext = (s.slide || s.gate == 3) && nx.on;
            double off;
            float gv;
            grooveAt(n % len, off, gv);
            const int hits = std::clamp(s.ratchet, 1, 4);
            int note = std::clamp(root.load() + s.semi + 12 * s.oct, 0, 127);
            for (int h = 0; h < hits; ++h)
            {
                static const double gm[4] = {0.3, -1, 0.85, 1.0};
                double gl = hits > 1 ? 0.5 * stepQ / hits
                                     : (legatoNext ? stepQ * 1.08
                                                   : (gm[std::clamp(s.gate, 0, 3)] < 0 ? g : gm[std::clamp(s.gate, 0, 3)]) * stepQ);
                out.push_back({(n + off) * stepQ + h * stepQ / hits, gl, note,
                               std::clamp((int)std::lround((s.accent ? 120.f : 88.f) * (h ? 0.85f : gv)), 1, 127)});
            }
        }
        return out;
    }

    // compact text form for presets / state ("v2;len;root;scale;groove;swing;gate;chord;s0,s1,...")
    std::string serialise() const
    {
        std::string o = "v2;" + std::to_string(length.load()) + ";" + std::to_string(root.load()) + ";" +
                        std::to_string(scale.load()) + ";" + std::to_string(groove.load()) + ";" +
                        std::to_string((int)std::lround(swing.load() * 1000)) + ";" +
                        std::to_string((int)std::lround(gate.load() * 1000)) + ";" + (chordMode.load() ? "1" : "0") + ";";
        for (int i = 0; i < kMaxSteps; ++i)
            o += std::to_string(raw(i)) + (i + 1 < kMaxSteps ? "," : "");
        return o;
    }
    bool deserialise(const std::string &t)
    {
        std::vector<std::string> f;
        size_t a = 0;
        for (int k = 0; k < 8; ++k)
        {
            auto b = t.find(';', a);
            if (b == std::string::npos)
                return false;
            f.push_back(t.substr(a, b - a));
            a = b + 1;
        }
        if (f[0] != "v2")
            return false;
        try
        {
            length.store(std::clamp(std::stoi(f[1]), 1, kMaxSteps));
            root.store(std::clamp(std::stoi(f[2]), 12, 72));
            scale.store(std::clamp(std::stoi(f[3]), 0, (int)n_scales - 1));
            groove.store(std::clamp(std::stoi(f[4]), 0, (int)n_grooves - 1));
            swing.store(std::clamp(std::stoi(f[5]) / 1000.f, 0.f, 1.f));
            gate.store(std::clamp(std::stoi(f[6]) / 1000.f, 0.05f, 1.f));
            chordMode.store(f[7] == "1");
            std::string rest = t.substr(a);
            size_t p = 0;
            for (int i = 0; i < kMaxSteps; ++i)
            {
                auto q = rest.find(',', p);
                std::string v = rest.substr(p, q == std::string::npos ? std::string::npos : q - p);
                setRaw(i, v.empty() ? 0u : (uint32_t)std::stoul(v));
                if (q == std::string::npos)
                {
                    for (int j = i + 1; j < kMaxSteps; ++j)
                        setRaw(j, 0);
                    break;
                }
                p = q + 1;
            }
        }
        catch (...)
        {
            return false;
        }
        return true;
    }

    // standalone / no-transport clock
    double internalPpq{0};

  private:
    std::array<std::atomic<uint32_t>, kMaxSteps> packed;
    std::array<std::atomic<uint64_t>, 2> held;
    int sounding{-1};
    double soundingEnd{0};
    bool prevLegato{false};

    static uint32_t pack(const SeqStep &s)
    {
        return (s.on ? 1u : 0u) | (s.accent ? 2u : 0u) | (s.slide ? 4u : 0u) | ((uint32_t)(s.oct + 2) & 7u) << 3 |
               ((uint32_t)(s.semi + 64) & 255u) << 6 | ((uint32_t)std::clamp(s.gate, 0, 3) ^ 1u) << 14 |
               ((uint32_t)(std::clamp(s.ratchet, 1, 4) - 1)) << 16;
    }
    static SeqStep unpack(uint32_t v)
    {
        SeqStep s;
        s.on = v & 1u;
        s.accent = v & 2u;
        s.slide = v & 4u;
        s.oct = (int)((v >> 3) & 7u) - 2;
        s.semi = (int)((v >> 6) & 255u) - 64;
        s.gate = (int)(((v >> 14) & 3u) ^ 1u); // stored so that 0 bits = "normal"
        s.ratchet = (int)((v >> 16) & 3u) + 1;
        return s;
    }
};

} // namespace ghost

#endif
