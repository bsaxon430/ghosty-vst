/*
 * SEANCE (GhostBlock) - STEPS: a note sequencer in front of the engine.
 *
 * A pattern is a loop of notes on a 96-ticks-per-quarter grid. Each note is either an offset in semitones from the
 * key you hold (one key plays the whole pattern, in that key) or, in a "ladder" pattern, an index into the notes you
 * hold (lowest first, then up by octaves). While STEPS is on, the keys you play do not sound themselves: they tell
 * the sequencer what to play, and its notes go to the engine exactly as played notes would (so SUB and the
 * crossroads hear them too).
 *
 * Timing: while the host is playing, the pattern position is taken from the song position at every buffer, so it
 * sits on the song's grid whenever you press. With the host stopped it starts at the first key and runs at the
 * host's tempo.
 *
 * Threads: block() is the audio thread. Settings are atomics. The pattern crosses over through a try-lock: the audio
 * thread never waits (it takes the new pattern at the next buffer when it gets the lock). No allocation in block().
 * JUCE-free.
 */
#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <locale>
#include <memory>
#include <sstream>
#include <string>

namespace ghostblock::seance
{
struct StepNote
{
    int32_t t{0}, d{24};  // start and length in ticks (96 = a quarter, 24 = a sixteenth)
    int16_t off{0};       // semitones from the held key, or (ladder) which held note (0 = lowest) + 64 x octaves (up or down)
    uint8_t vel{100};     // 1..127
    uint8_t lane{1};      // low 2 bits: 0 chords, 1 arp, 2 melody, 3 bass (so parts of a pattern can be muted);
                          // high 4 bits (ladder notes): 0 = on the held note, else 8 + semitones off it (-7 .. +7)
    int laneOf() const { return lane & 3; }
    int nudge() const { return (lane >> 4) ? (int)(lane >> 4) - 8 : 0; }
    static uint8_t packLane(int lane, int nudge) { return (uint8_t)((lane & 3) | (nudge == 0 ? 0 : ((std::clamp(nudge, -7, 7) + 8) << 4))); }
};
struct StepPattern
{
    static constexpr int kMax = 1024;
    int32_t loopTicks{384}; // loop length
    int32_t n{0};
    int32_t ladder{0};      // 0: offsets from the held key; 1: indices into the held notes
    int32_t kind{0};        // what made it (the editor's own code; NEW makes another of the same kind)
    uint32_t seed{0};
    char name[64]{};
    StepNote notes[kMax];
    bool add(int t, int d, int off, int vel, int lane)
    {
        if (n >= kMax) return false;
        auto &x = notes[n++];
        x.t = t; x.d = std::max(1, d); x.off = (int16_t)off; x.vel = (uint8_t)std::clamp(vel, 1, 127); x.lane = (uint8_t)(lane & 0xF3); // (lane + nudge, see StepNote)
        return true;
    }
    void setName(const std::string &s) { std::snprintf(name, sizeof(name), "%s", s.c_str()); }
    void sortNotes()
    {
        std::stable_sort(notes, notes + n, [](const StepNote &a, const StepNote &b) { return a.t != b.t ? a.t < b.t : a.off < b.off; });
    }
};

struct StepSeq
{
    static constexpr int PPQ = 96, BAR = 384, kHeld = 32, kActive = 96;

    // ---- settings (any thread)
    std::atomic<bool> on{false}, latch{false};
    std::atomic<int> speed{1};        // 0 half, 1 as written, 2 double
    std::atomic<float> gate{1.f};     // note length x 0.1 .. 1.5
    std::atomic<float> swing{0.f};    // 0 .. 0.75: how late every second sixteenth lands
    std::atomic<int> octave{0};       // -2 .. 2
    std::atomic<int> laneMask{0xF};   // bit per lane: 1 = plays
    // ---- for the screen
    std::atomic<float> posTicks{0.f};
    std::atomic<bool> running{false};
    std::atomic<int> rootKey{-1};
    std::atomic<uint32_t> changed{0}; // counts up whenever settings or the pattern were replaced from outside the page

    // ---- the pattern (main thread writes, audio thread picks it up)
    void setPattern(const StepPattern &p)
    {
        while (lock.test_and_set(std::memory_order_acquire)) {}
        pending = p;
        pending.n = std::clamp(pending.n, 0, (int32_t)StepPattern::kMax);
        pending.loopTicks = std::max(24, pending.loopTicks);
        pending.name[sizeof(pending.name) - 1] = 0;
        version.fetch_add(1, std::memory_order_release);
        lock.clear(std::memory_order_release);
    }
    void getPattern(StepPattern &p) const
    {
        while (lock.test_and_set(std::memory_order_acquire)) {}
        p = pending;
        lock.clear(std::memory_order_release);
    }

    // ---- audio thread
    struct KeyEv { uint32_t time; int16_t key; float vel; bool down; };
    struct Out { uint32_t time; int16_t key; float vel; bool down; };

    bool isHeld(int key) const
    {
        for (int i = 0; i < nHeld; ++i)
            if (held[i] == key) return true;
        return false;
    }
    bool busy() const { return nAct > 0 || nHeld > 0 || run; } // still has notes of its own to end / keys to track

    // One host buffer. keys = the notes the player pressed / released in it (time order). Writes the notes for the
    // engine into out (time order, releases before presses at the same sample); returns how many.
    int block(const KeyEv *keys, int nKeys, uint32_t frames, double sr, double bpm, bool hostPlaying, double beatPos, Out *out, int maxOut)
    {
        nOut = 0; outBuf = out; outMax = maxOut;
        const bool isOn = on.load(std::memory_order_relaxed);
        takePattern();
        const double mul = speed.load(std::memory_order_relaxed) == 0 ? 0.5 : speed.load(std::memory_order_relaxed) == 2 ? 2.0 : 1.0;
        const double tps = std::max(1.0, bpm) / 60.0 * PPQ * mul / std::max(1.0, sr); // pattern ticks per sample
        const int loop = std::max(24, (int)live.loopTicks);
        if (!isOn)
        { // switched off: end what is sounding, forget the keys
            if (run || nAct > 0) { cutAll(0); run = false; }
            nHeld = 0; nChord = 0;
            publish();
            return finish();
        }
        if (run && nHeld == 0 && !latch.load(std::memory_order_relaxed)) stop(0); // LATCH let go with no key down
        double pull = 0; // host playing: stay on the song's grid
        if (run && hostPlaying)
        {
            const double want = wrap(beatPos * PPQ * mul, loop);
            double d = want - pos;
            if (d > loop * 0.5) d -= loop;
            if (d < -loop * 0.5) d += loop;
            if (std::fabs(d) < 1.0) pull = d * 0.5; // a hair off: ease onto it inside this buffer (nothing is skipped or played twice)
            else pos = want;                        // the song jumped (loop, locate, or it has just started)
        }
        uint32_t s0 = 0;
        int ki = 0;
        while (s0 < frames || ki < nKeys)
        {
            const uint32_t s1 = ki < nKeys ? std::min(frames, keys[ki].time) : frames;
            if (s1 > s0)
            {
                advance(s0, s1 - s0, tps, loop, pull);
                pull = 0;
                s0 = s1;
            }
            if (ki >= nKeys) break;
            // every key event at this sample
            const auto &k = keys[ki++];
            const double hostPosHere = wrap((beatPos + (double)s0 / std::max(1.0, sr) * bpm / 60.0) * PPQ * mul, loop);
            if (k.down) keyDown(k.key, k.vel, s0, hostPlaying, hostPosHere, loop);
            else keyUp(k.key, s0, loop);
        }
        publish();
        return finish();
    }

    // ---- state (main thread). Text, one section.
    std::string toState() const
    {
        StepPattern p;
        getPattern(p);
        std::ostringstream o;
        o.imbue(std::locale::classic());
        o << "SEANCESTEPS 1\n";
        o << (on.load() ? 1 : 0) << ' ' << (latch.load() ? 1 : 0) << ' ' << speed.load() << ' ' << gate.load() << ' ' << swing.load() << ' ' << octave.load() << ' ' << laneMask.load() << '\n';
        o << p.loopTicks << ' ' << p.n << ' ' << p.ladder << ' ' << p.kind << ' ' << p.seed << '\n';
        std::string nm(p.name);
        for (auto &c : nm) if (c == '\n' || c == '\r') c = ' ';
        o << nm << '\n';
        for (int i = 0; i < p.n; ++i) o << p.notes[i].t << ' ' << p.notes[i].d << ' ' << p.notes[i].off << ' ' << (int)p.notes[i].vel << ' ' << (int)p.notes[i].lane << '\n';
        return o.str();
    }
    bool fromState(const std::string &s)
    {
        std::istringstream in(s);
        in.imbue(std::locale::classic());
        std::string tag;
        int ver = 0;
        if (!(in >> tag >> ver) || tag != "SEANCESTEPS" || ver != 1) return false;
        int o = 0, l = 0, sp = 1, oc = 0, lm = 0xF;
        float g = 1, sw = 0;
        auto p = std::make_unique<StepPattern>();
        int n = 0;
        if (!(in >> o >> l >> sp >> g >> sw >> oc >> lm)) return false;
        if (!(in >> p->loopTicks >> n >> p->ladder >> p->kind >> p->seed)) return false;
        if (n < 0 || n > StepPattern::kMax || p->loopTicks < 24 || p->loopTicks > BAR * 64) return false;
        std::string nm;
        std::getline(in, nm); // (the rest of the numbers' line)
        std::getline(in, nm);
        p->setName(nm);
        for (int i = 0; i < n; ++i)
        {
            int t, d, off, vel, lane;
            if (!(in >> t >> d >> off >> vel >> lane)) return false;
            if (t < 0 || t >= p->loopTicks || d < 1 || off < -127 || off > 127) continue; // (a note outside the loop is dropped)
            p->add(t, d, off, vel, lane);
        }
        setPattern(*p);
        on = o != 0; latch = l != 0;
        speed = std::clamp(sp, 0, 2);
        gate = std::isfinite(g) ? std::clamp(g, 0.1f, 1.5f) : 1.f;
        swing = std::isfinite(sw) ? std::clamp(sw, 0.f, 0.75f) : 0.f;
        octave = std::clamp(oc, -2, 2);
        laneMask = lm & 0xF;
        changed.fetch_add(1);
        return true;
    }
    void resetToDefaults()
    {
        auto p = std::make_unique<StepPattern>();
        setPattern(*p);
        on = false; latch = false; speed = 1; gate = 1.f; swing = 0.f; octave = 0; laneMask = 0xF;
        changed.fetch_add(1);
    }

  private:
    mutable std::atomic_flag lock = ATOMIC_FLAG_INIT;
    StepPattern pending;
    std::atomic<uint32_t> version{0};
    // audio thread only
    StepPattern live;
    uint32_t liveVersion{0};
    int16_t held[kHeld]{};     // keys down, in the order pressed
    float heldVel[kHeld]{};
    int nHeld{0};
    int16_t chord[kHeld]{};    // what the pattern plays over, lowest first (kept while latched)
    int nChord{0};
    int root{60};
    float rootVel{0.8f};
    bool run{false};
    double pos{0}, clock{0};   // pattern position; a clock that only goes forward (for note ends)
    struct Act { int16_t key; double end; };
    Act act[kActive];
    int nAct{0};
    Out *outBuf{nullptr};
    int nOut{0}, outMax{0};

    static double wrap(double x, int loop)
    {
        x = std::fmod(x, (double)loop);
        return x < 0 ? x + loop : x;
    }
    void takePattern()
    {
        if (version.load(std::memory_order_acquire) == liveVersion) return;
        if (lock.test_and_set(std::memory_order_acquire)) return; // the main thread is writing: next buffer
        live = pending;
        liveVersion = version.load(std::memory_order_relaxed);
        lock.clear(std::memory_order_release);
        if (run) { cutAll(0); sustainers(0, std::max(24, (int)live.loopTicks)); }
    }
    void emit(uint32_t time, int key, float vel, bool down)
    {
        if (nOut < outMax) outBuf[nOut++] = {time, (int16_t)key, vel, down};
    }
    int finish()
    { // time order; at one sample, releases first (insertion sort: a handful of events)
        for (int i = 1; i < nOut; ++i)
        {
            const Out x = outBuf[i];
            int j = i - 1;
            while (j >= 0 && (outBuf[j].time > x.time || (outBuf[j].time == x.time && outBuf[j].down && !x.down)))
            {
                outBuf[j + 1] = outBuf[j];
                --j;
            }
            outBuf[j + 1] = x;
        }
        return nOut;
    }
    void publish()
    {
        posTicks.store((float)pos, std::memory_order_relaxed);
        running.store(run, std::memory_order_relaxed);
        rootKey.store(run ? root : -1, std::memory_order_relaxed);
    }
    void cutAll(uint32_t time)
    {
        for (int i = 0; i < nAct; ++i) emit(time, act[i].key, 0.f, false);
        nAct = 0;
    }
    void stop(uint32_t time)
    {
        cutAll(time);
        run = false;
        pos = 0;
    }
    double startOf(const StepNote &x) const
    { // SWING: every second sixteenth lands late
        const float sw = swing.load(std::memory_order_relaxed);
        if (sw > 0.f && x.t % 24 == 0 && ((x.t / 24) & 1)) return x.t + sw * 12.0;
        return x.t;
    }
    double lengthOf(const StepNote &x) const { return std::max(6.0, x.d * (double)gate.load(std::memory_order_relaxed)); }
    int pitchOf(const StepNote &x) const
    {
        const int oc = 12 * octave.load(std::memory_order_relaxed);
        if (live.ladder)
        {
            if (nChord <= 0) return -1;
            const int off = x.off, idx = off & 63, lift = (off - idx) / 64; // (stored as index + 64 x octaves, up or down)
            return chord[idx % nChord] + 12 * (idx / nChord) + 12 * lift + x.nudge() + oc;
        }
        return root + x.off + oc;
    }
    void noteOn(uint32_t time, const StepNote &x, double endAt)
    {
        if (!(laneMask.load(std::memory_order_relaxed) & (1 << (x.lane & 3)))) return;
        const int key = pitchOf(x);
        if (key < 0 || key > 127) return;
        for (int i = 0; i < nAct; ++i)
            if (act[i].key == key)
            { // the same key again: end the old one first
                emit(time, key, 0.f, false);
                act[i] = act[--nAct];
                break;
            }
        if (nAct >= kActive) return;
        act[nAct++] = {(int16_t)key, endAt};
        emit(time, key, std::clamp((x.vel / 127.f) * (0.4f + 0.6f * rootVel), 0.02f, 1.f), true);
    }
    // long notes already under way at the current position (a chord held across the bar) start now, for what is left of them
    void sustainers(uint32_t time, int loop)
    {
        for (int i = 0; i < live.n; ++i)
        {
            const auto &x = live.notes[i];
            if (x.d < PPQ) continue;
            const double st = startOf(x), len = lengthOf(x);
            double into = pos - st;
            if (into < 0) into += loop;
            if (into > 0.5 && len - into > 24.0) noteOn(time, x, clock + (len - into));
        }
    }
    void advance(uint32_t s0, uint32_t n, double tps, int loop, double pull)
    {
        const double real = n * tps;                        // how far the clock moves
        const double span = std::max(real * 0.5, real + pull); // how far the pattern moves (a little more or less while easing onto the song)
        if (run)
        {
            for (int i = 0; i < live.n; ++i)
            {
                const auto &x = live.notes[i];
                double st = startOf(x);
                if (st >= loop) st -= loop;
                double ahead = st - pos; // ticks from now until it starts
                if (ahead < 0) ahead += loop;
                // a span longer than the loop would need the note more than once; loops are at least a sixteenth, buffers far shorter
                if (ahead < span) noteOn(s0 + (uint32_t)std::min((double)(n - 1), ahead / tps), x, clock + std::min(ahead, real) + lengthOf(x));
            }
        }
        for (int i = 0; i < nAct;)
        {
            const double left = act[i].end - clock;
            if (left < real)
            {
                emit(s0 + (uint32_t)std::clamp(left / tps, 0.0, (double)(n - 1)), act[i].key, 0.f, false);
                act[i] = act[--nAct];
            }
            else
                ++i;
        }
        clock += real;
        if (run) pos = wrap(pos + span, loop);
    }
    void setChordFromHeld()
    {
        nChord = nHeld;
        for (int i = 0; i < nHeld; ++i) chord[i] = held[i];
        std::sort(chord, chord + nChord);
    }
    void keyDown(int key, float vel, uint32_t time, bool hostPlaying, double hostPos, int loop)
    {
        const bool fresh = nHeld == 0; // nothing was down: a new phrase (also replaces a latched one)
        if (isHeld(key)) return;
        if (nHeld < kHeld)
        {
            held[nHeld] = (int16_t)key;
            heldVel[nHeld] = vel;
            ++nHeld;
        }
        const int oldRoot = root;
        root = key;
        rootVel = vel;
        if (fresh) { nChord = 0; }
        setChordFromHeld();
        if (!run)
        {
            run = true;
            pos = hostPlaying ? hostPos : 0.0;
            sustainers(time, loop);
        }
        else if (!live.ladder && root != oldRoot)
        { // a new key under a running one-key pattern: move what is sounding to the new key
            cutAll(time);
            sustainers(time, loop);
        }
    }
    void keyUp(int key, uint32_t time, int loop)
    {
        int at = -1;
        for (int i = 0; i < nHeld; ++i)
            if (held[i] == key) { at = i; break; }
        if (at < 0) return;
        for (int i = at; i + 1 < nHeld; ++i) { held[i] = held[i + 1]; heldVel[i] = heldVel[i + 1]; }
        --nHeld;
        if (nHeld == 0)
        {
            if (!latch.load(std::memory_order_relaxed)) stop(time);
            return; // (latched: keeps playing over the last chord / key)
        }
        setChordFromHeld();
        if (!live.ladder && key == root)
        { // the key the pattern sat on was let go while others are down: fall back to the last one pressed
            root = held[nHeld - 1];
            rootVel = heldVel[nHeld - 1];
            cutAll(time);
            sustainers(time, loop);
        }
    }
};
} // namespace ghostblock::seance
