/*
 * Ghost Bass - a bass instrument built on Surge XT.
 * Copyright 2026 the Ghost Bass authors. Based on Surge XT by the Surge Synth Team.
 * Released under the GNU General Public Licence v3 or later (see LICENSE).
 *
 * The arpeggiator: tempo-synced, follows the host's song position (or its own clock when the
 * transport is stopped / in the standalone). While it is on, the keys you hold are not played
 * directly: the arp plays them one at a time (UP, DOWN, UP-DOWN, RANDOM, AS PLAYED) or all at
 * once (CHORD), over 1-4 octaves, at 1/4 .. 1/32 (with triplets), with its own gate, swing and
 * LATCH (keeps playing after you let go, until you play a new chord).
 *
 * Settings are atomics (the UI and patch loader write them lock-free). The held-note list lives
 * on the audio thread only (applyMidi and tick both run there); fixed arrays, no allocation.
 */
#ifndef GHOSTBASS_SRC_GHOST_GHOSTARP_H
#define GHOSTBASS_SRC_GHOST_GHOSTARP_H

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <string>

namespace ghost
{

class Arpeggiator
{
  public:
    enum Mode
    {
        UP = 0,
        DOWN,
        UPDOWN,
        RANDOM,
        CHORD,
        ORDER,
        n_modes
    };
    enum Rate
    {
        R4 = 0,
        R8,
        R8T,
        R16,
        R16T,
        R32,
        n_rates
    };
    static constexpr int kMaxHeld = 16;

    std::atomic<bool> enabled{false};
    std::atomic<int> mode{UP};
    std::atomic<int> rate{R16};
    std::atomic<int> octaves{1}; // 1..4
    std::atomic<float> gate{0.6f};  // 0.05..1 of a step
    std::atomic<float> swing{0.f};  // 0..1: every second step up to a triplet late
    std::atomic<bool> latch{false};
    std::atomic<int> playhead{-1}; // for the UI: which held note (index in the cycle)

    static const char *modeName(int m)
    {
        static const char *n[] = {"UP", "DOWN", "UP-DOWN", "RANDOM", "CHORD", "AS PLAYED"};
        return n[std::clamp(m, 0, (int)n_modes - 1)];
    }
    static const char *rateName(int r)
    {
        static const char *n[] = {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"};
        return n[std::clamp(r, 0, (int)n_rates - 1)];
    }
    static double rateQ(int r) // step length in quarter notes
    {
        static const double q[] = {1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125};
        return q[std::clamp(r, 0, (int)n_rates - 1)];
    }

    // ---- audio thread: keys ----
    void noteOn(int note, int vel)
    {
        if (note < 0 || note > 127)
            return;
        if (latch.load(std::memory_order_relaxed) && physicalDown == 0)
            nHeld = 0; // a new chord after letting go replaces the latched one
        ++physicalDown;
        for (int i = 0; i < nHeld; ++i)
            if (held[i].note == note)
                return;
        if (nHeld < kMaxHeld)
            held[nHeld++] = {note, std::clamp(vel, 1, 127)};
        if (!running)
        {
            running = true;
            counter = 0;
            dir = 1;
            restart = true;
        }
    }
    void noteOff(int note)
    {
        physicalDown = std::max(0, physicalDown - 1);
        if (latch.load(std::memory_order_relaxed))
            return;
        for (int i = 0; i < nHeld; ++i)
            if (held[i].note == note)
            {
                for (int j = i; j + 1 < nHeld; ++j)
                    held[j] = held[j + 1];
                --nHeld;
                break;
            }
    }
    void clear()
    {
        nHeld = 0;
        physicalDown = 0;
    }
    int heldCount() const { return nHeld; }
    bool isSounding(int note) const
    {
        for (int i = 0; i < nSounding; ++i)
            if (sounding[i].note == note)
                return true;
        return false;
    }

    // Called once per Surge block on the audio thread (like the sequencer)
    template <typename On, typename Off>
    void tick(double ppq, double ppqPerSample, int nSamples, On on, Off off)
    {
        const double ppqEnd = ppq + ppqPerSample * nSamples;
        // release what is due
        for (int i = 0; i < nSounding; ++i)
            if (sounding[i].end < ppqEnd || !enabled.load(std::memory_order_relaxed) || nHeld == 0)
            {
                off(sounding[i].note);
                sounding[i] = sounding[--nSounding];
                --i;
            }
        if (!enabled.load(std::memory_order_relaxed) || nHeld == 0)
        {
            running = false;
            playhead.store(-1, std::memory_order_relaxed);
            return;
        }
        const double stepQ = rateQ(rate.load(std::memory_order_relaxed));
        const double g = std::clamp(gate.load(std::memory_order_relaxed), 0.05f, 1.f);
        const double sw = std::clamp(swing.load(std::memory_order_relaxed), 0.f, 1.f) * 0.33;
        if (restart)
        {
            // start on the next grid line (or right now if we are on one)
            restart = false;
            nextStep = (long)std::ceil(ppq / stepQ - 1e-9);
        }
        for (;;)
        {
            const double start = (nextStep + ((nextStep & 1) ? sw : 0.0)) * stepQ;
            if (start >= ppqEnd)
                break;
            if (start >= ppq - 1e-9)
                fire(start, stepQ * g, on, off);
            ++nextStep;
        }
    }

    std::string serialise() const
    {
        return "a1;" + std::to_string(enabled.load() ? 1 : 0) + ";" + std::to_string(mode.load()) + ";" +
               std::to_string(rate.load()) + ";" + std::to_string(octaves.load()) + ";" +
               std::to_string((int)std::lround(gate.load() * 1000)) + ";" +
               std::to_string((int)std::lround(swing.load() * 1000)) + ";" + (latch.load() ? "1" : "0");
    }
    bool deserialise(const std::string &t)
    {
        int v[7];
        if (t.size() < 3 || t.compare(0, 3, "a1;") != 0)
            return false;
        size_t a = 3;
        for (int k = 0; k < 7; ++k)
        {
            size_t b = t.find(';', a);
            std::string f = t.substr(a, b == std::string::npos ? std::string::npos : b - a);
            if (f.empty() || f.find_first_not_of("-0123456789") != std::string::npos)
                return false;
            v[k] = std::atoi(f.c_str());
            if (b == std::string::npos && k < 6)
                return false;
            a = b + 1;
        }
        enabled.store(v[0] != 0);
        mode.store(std::clamp(v[1], 0, (int)n_modes - 1));
        rate.store(std::clamp(v[2], 0, (int)n_rates - 1));
        octaves.store(std::clamp(v[3], 1, 4));
        gate.store(std::clamp(v[4] / 1000.f, 0.05f, 1.f));
        swing.store(std::clamp(v[5] / 1000.f, 0.f, 1.f));
        latch.store(v[6] != 0);
        return true;
    }

  private:
    struct H
    {
        int note, vel;
    };
    struct S
    {
        int note;
        double end;
    };
    H held[kMaxHeld]{};
    int nHeld{0}, physicalDown{0};
    S sounding[kMaxHeld * 4]{};
    int nSounding{0};
    bool running{false}, restart{false};
    long nextStep{0};
    int counter{0}, dir{1}, last{-1};
    uint32_t rng{0x2545F491u};

    template <typename On, typename Off> void fire(double start, double len, On on, Off off)
    {
        // a mono-legato bass must not glide between arp notes: each step ends the last one
        for (int i = 0; i < nSounding; ++i)
            off(sounding[i].note);
        nSounding = 0;

        const int oct = std::clamp(octaves.load(std::memory_order_relaxed), 1, 4);
        const int m = mode.load(std::memory_order_relaxed);
        if (m == CHORD)
        {
            const int o = counter % oct;
            for (int i = 0; i < nHeld && nSounding < kMaxHeld * 4; ++i)
                play(std::clamp(held[i].note + 12 * o, 0, 127), held[i].vel, start + len, on);
            playhead.store(o, std::memory_order_relaxed);
            ++counter;
            return;
        }
        // the cycle: the held notes (sorted, or as played), repeated up the octaves
        int sorted[kMaxHeld], vel[kMaxHeld];
        for (int i = 0; i < nHeld; ++i)
        {
            sorted[i] = held[i].note;
            vel[i] = held[i].vel;
        }
        if (m != ORDER)
            for (int i = 1; i < nHeld; ++i)
                for (int j = i; j > 0 && sorted[j - 1] > sorted[j]; --j)
                {
                    std::swap(sorted[j - 1], sorted[j]);
                    std::swap(vel[j - 1], vel[j]);
                }
        const int L = nHeld * oct;
        int idx;
        switch (m)
        {
        case DOWN:
            idx = L - 1 - (counter % L);
            break;
        case UPDOWN:
        {
            const int period = std::max(1, 2 * L - 2); // the top and bottom notes play once
            const int c = counter % period;
            idx = L == 1 ? 0 : (c < L ? c : period - c);
            break;
        }
        case RANDOM:
        {
            rng = rng * 1664525u + 1013904223u;
            idx = (int)((rng >> 8) % (uint32_t)L);
            if (L > 1 && idx == last)
                idx = (idx + 1) % L; // never the same note twice in a row
            break;
        }
        default: // UP, ORDER
            idx = counter % L;
        }
        last = idx;
        ++counter;
        const int n = std::clamp(sorted[idx % nHeld] + 12 * (idx / nHeld), 0, 127);
        playhead.store(idx, std::memory_order_relaxed);
        play(n, vel[idx % nHeld], start + len, on);
    }
    template <typename On> void play(int note, int vel, double end, On on)
    {
        on(note, vel);
        sounding[nSounding++] = {note, end};
    }
};

} // namespace ghost

#endif
