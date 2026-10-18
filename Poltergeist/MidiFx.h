#pragma once
// Play helpers, applied to incoming notes before the engine:
//   SCALE LOCK - every note snaps into the chosen key/scale, so nothing sounds wrong
//   CHORD      - one finger plays a full chord (in key when Scale Lock is on)
//   ARP        - held notes are played one by one, synced to the host tempo
#include "Params.h"
#include <random>

namespace gs {

class MidiFx {
public:
    void reset() { for (auto& o : outFor) o.clear(); held.clear(); arpOn = -1; arpIdx = 0; arpUp = true; lastStep = -1; freeClock = 0; }

    void process(juce::MidiBuffer& midi, int numSamples, const Snap& P, double sr, double bpm, double ppq, bool playing) {
        if (!P.scaleOn && P.chord == 0 && !P.arpOn && !wasArp) return;
        juce::MidiBuffer out;
        for (const auto md : midi) {
            const auto m = md.getMessage();
            if (m.isNoteOn()) {
                auto notes = expand(m.getNoteNumber(), P);
                outFor[m.getNoteNumber()] = notes;
                if (P.arpOn) { for (int n : notes) addHeld(n, m.getFloatVelocity()); }
                else for (int n : notes) out.addEvent(juce::MidiMessage::noteOn(m.getChannel(), n, m.getFloatVelocity()), md.samplePosition);
            } else if (m.isNoteOff()) {
                auto& notes = outFor[m.getNoteNumber()];
                if (P.arpOn || wasArp) for (int n : notes) removeHeld(n);
                if (!P.arpOn) for (int n : notes) out.addEvent(juce::MidiMessage::noteOff(m.getChannel(), n), md.samplePosition);
                notes.clear();
            } else out.addEvent(m, md.samplePosition);
        }
        if (P.arpOn) runArp(out, numSamples, P, sr, bpm, ppq, playing);
        else if (wasArp && arpOn >= 0) { out.addEvent(juce::MidiMessage::noteOff(1, arpOn), 0); arpOn = -1; }
        wasArp = P.arpOn;
        midi.swapWith(out);
    }

    // scale helpers (also used by the UI text)
    static const std::vector<int>& scale(int type) {
        static const std::vector<std::vector<int>> S { { 0, 2, 4, 5, 7, 9, 11 }, { 0, 2, 3, 5, 7, 8, 10 }, { 0, 2, 3, 5, 7, 8, 11 }, { 0, 2, 3, 5, 7, 9, 10 },
                                                       { 0, 1, 3, 5, 7, 8, 10 }, { 0, 3, 5, 7, 10 }, { 0, 2, 4, 7, 9 }, { 0, 3, 5, 6, 7, 10 } };
        return S[(size_t) juce::jlimit(0, 7, type)];
    }

private:
    std::vector<int> outFor[128];
    struct Held { int note; float vel; };
    std::vector<Held> held;
    int arpOn = -1, arpIdx = 0; bool arpUp = true, wasArp = false; long lastStep = -1; double freeClock = 0;
    std::mt19937 rng { 7 };

    static int snap(int note, const Snap& P) {
        if (!P.scaleOn) return note;
        const auto& sc = scale(P.scaleType);
        const int rel = ((note - P.scaleRoot) % 12 + 12) % 12, base = note - rel;
        int best = sc[0], bd = 99;
        for (int d : sc) { const int dist = std::abs(d - rel); if (dist < bd || (dist == bd && d < rel)) { bd = dist; best = d; } }
        return base + best;
    }
    // stack thirds inside the scale when Scale Lock is on; plain intervals otherwise
    static std::vector<int> expand(int note, const Snap& P) {
        const int root = snap(note, P);
        if (P.chord == 0) return { root };
        if (P.chord == 1) return { root, root + 7, root + 12 };
        if (P.chord == 7) return { root, root + 12, root + 24 };
        std::vector<int> degs;   // scale steps above the root
        switch (P.chord) { case 2: degs = { 2, 4 }; break; case 3: degs = { 2, 4, 6 }; break; case 4: degs = { 2, 4, 6, 8 }; break; case 5: degs = { 1, 4 }; break; default: degs = { 3, 4 }; }
        std::vector<int> out { root };
        if (P.scaleOn) {
            const auto& sc = scale(P.scaleType); const int N = (int) sc.size();
            const int rel = ((root - P.scaleRoot) % 12 + 12) % 12; int idx = 0;
            for (int i = 0; i < N; ++i) if (sc[(size_t) i] == rel) idx = i;
            for (int d : degs) { const int j = idx + d; out.push_back(root - rel + sc[(size_t) (j % N)] + 12 * (j / N)); }
        } else {
            static const int MAJ[] = { 0, 2, 4, 5, 7, 9, 11, 12, 14 };
            for (int d : degs) out.push_back(root + MAJ[d]);
        }
        return out;
    }
    void addHeld(int n, float v) { for (auto& h : held) if (h.note == n) return; held.push_back({ n, v }); }
    void removeHeld(int n) { held.erase(std::remove_if(held.begin(), held.end(), [n](const Held& h) { return h.note == n; }), held.end()); }

    void runArp(juce::MidiBuffer& out, int numSamples, const Snap& P, double sr, double bpm, double ppq, bool playing) {
        const double stepBeats = lfoDivBeats(P.arpDiv), spb = 60.0 / bpm * sr, stepSamples = stepBeats * spb;
        for (int i = 0; i < numSamples; ++i) {
            if (arpOn >= 0 && offRemain >= 0 && --offRemain <= 0) { out.addEvent(juce::MidiMessage::noteOff(1, arpOn), i); arpOn = -1; offRemain = -1; }
            const double beat = playing ? ppq + i / spb : (freeClock + i) / spb;
            const long step = (long) std::floor(beat / stepBeats);
            if (step == lastStep) continue;
            lastStep = step;
            if (arpOn >= 0) { out.addEvent(juce::MidiMessage::noteOff(1, arpOn), i); arpOn = -1; }
            if (held.empty()) continue;
            // the note list: held notes (sorted unless "as played") repeated over the octaves
            std::vector<Held> list;
            auto base = held;
            if (P.arpPat != 4) std::sort(base.begin(), base.end(), [](const Held& a, const Held& b) { return a.note < b.note; });
            for (int o = 0; o < P.arpOct; ++o) for (auto h : base) { h.note += 12 * o; if (h.note < 128) list.push_back(h); }
            const int N = (int) list.size(); int k = 0;
            switch (P.arpPat) {
                case 1: k = N - 1 - (arpIdx % N); break;
                case 2: { const int cyc = juce::jmax(1, 2 * N - 2); const int p = arpIdx % cyc; k = p < N ? p : cyc - p; break; }
                case 3: k = (int) (rng() % (unsigned) N); break;
                default: k = arpIdx % N;
            }
            ++arpIdx;
            arpOn = list[(size_t) k].note;
            out.addEvent(juce::MidiMessage::noteOn(1, arpOn, list[(size_t) k].vel), i);
            offRemain = juce::jmax(1.0, stepSamples * P.arpGate);
        }
        freeClock += numSamples;
    }
    double offRemain = -1;
};

} // namespace gs
