#pragma once
// GHOST HUM - "Sing it. Beatbox it. Make music."  your voice. your music.
// The data: what one recording became (a TAKE), the settings that shape how it plays (quantize, swing, tempo), and undo.
// Everything is in SECONDS of the recording: the tempo / grid is a view on top of it, so changing the BPM, the quantize or the
// swing never loses the performance. The as-heard transcription (ORIGINAL) is kept beside the edited one (ON GRID).
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <vector>
#include <cmath>

namespace hum {

enum Mode : int { Melody = 0, Beatbox = 1 };
enum Drum : int { Kick = 0, Snare, HatClosed, HatOpen, Clap, kDrums };
inline const char* drumName(int d) { static const char* n[] { "KICK", "SNARE", "HI-HAT", "OPEN HAT", "CLAP" }; return d >= 0 && d < kDrums ? n[d] : "?"; }
inline int gmNote(int d) { static const int n[] { 36, 38, 42, 46, 39 }; return d >= 0 && d < kDrums ? n[d] : 36; }   // General MIDI drum notes
inline int drumOfGm(int note) { for (int d = 0; d < kDrums; ++d) if (gmNote(d) == note) return d; return -1; }

// QUANTIZE: OFF | 1/4 | 1/8 | 1/16 | 1/32
enum Quant : int { QOff = 0, Q4, Q8, Q16, Q32 };
inline const char* quantName(int q) { static const char* n[] { "OFF", "1/4", "1/8", "1/16", "1/32" }; return q >= 0 && q <= 4 ? n[q] : "?"; }
inline double quantBeats(int q) { return q == Q4 ? 1.0 : q == Q8 ? 0.5 : q == Q16 ? 0.25 : q == Q32 ? 0.125 : 0.0; }   // grid step in beats (0 = off)

struct Note { double t = 0, len = 0.25; int midi = 60; float vel = 0.8f; float cents = 0; };   // t / len in seconds; cents = how far the voice was from the key (-50..50) when PITCH FIX is off
struct Hit  { double t = 0; int drum = Kick; float vel = 0.8f; float conf = 1; };   // conf: how sure Ghost was about the drum (0..1; the roll marks faint ones)

// PITCH: ORIGINAL (as sung, bends to the exact pitch) | NEAREST NOTE (the nearest semitone, default) | SNAP TO KEY (the nearest note of the key)
enum PitchMode : int { PitchOriginal = 0, PitchNearest, PitchSnapKey };
inline const char* pitchName(int m) { static const char* n[] { "ORIGINAL", "NEAREST NOTE", "SNAP TO KEY" }; return m >= 0 && m < 3 ? n[m] : "?"; }

struct Take {
    int mode = Melody;
    double seconds = 0;                 // length of the recording
    double bpm = 120, origin = 0;       // the grid: beat 0 sits at `origin` seconds
    bool bpmKnown = false;              // false = FREE recording whose tempo is only an estimate (KEEP ORIGINAL / offers)
    std::vector<double> bpmOffers;      // FREE: the tempos we offer to audition (best, x2 / /2)
    std::vector<Note> notes, origNotes; // notes = the working copy (pitch setting applied, then your edits); origNotes = exactly as heard, never changed
                                        // (the quantized positions are a third, derived view: Grid::place at playback / export time)
    std::vector<Hit> hits, origHits;
    int key = -1, scale = 0;            // detected key (0 = C .. 11 = B; -1 = none) and scale (0 major, 1 minor)
    double swingHeard = 50;             // the swing the performance had (50 = straight)
    bool empty() const { return notes.empty() && hits.empty(); }
    int count() const { return mode == Melody ? (int) notes.size() : (int) hits.size(); }
};

// the playback / export view of a take: where each note really lands with the current QUANTIZE, strength and SWING
struct Grid {
    int quant = QOff; float strength = 1.0f; double swing = 50;   // swing: 50 straight .. 75 (the off-beat of the grid step lands later)
    double place(double t, const Take& T) const {
        const double step = quantBeats(quant); if (step <= 0 || T.bpm <= 0) return t;
        const double beat = 60.0 / T.bpm, g = step * beat; const double b = (t - T.origin) / g;
        double n = std::round(b);
        // swing: every odd grid slot moves late by (swing - 50) % of the slot
        const double sw = juce::jlimit(0.0, 0.5, (swing - 50.0) / 50.0);
        if (sw > 0) {   // which slot is this time nearest: the straight one or the swung odd one
            const double lo = std::floor(b), hi = lo + 1; auto pos = [&](double k) { return k + ((long long) std::llround(k) % 2 != 0 ? sw : 0.0); };
            const double c1 = pos(lo), c2 = pos(hi), c0 = pos(lo - 1);
            double best = c1; for (double c : { c2, c0 }) if (std::abs(b - c) < std::abs(b - best)) best = c;
            n = best;
        }
        const double q = T.origin + n * g; if (n < 0 && q < 0) return t;
        return t + (q - t) * juce::jlimit(0.0f, 1.0f, strength);
    }
};

inline const char* keyName(int k) { static const char* n[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; return k >= 0 && k < 12 ? n[k] : "?"; }
inline juce::String keyText(const Take& T) { return T.key < 0 ? juce::String() : juce::String(keyName(T.key)) + (T.scale ? " minor" : " major"); }
inline bool inScale(int midi, int key, int scale) {
    if (key < 0) return true; static const bool maj[12] { 1,0,1,0,1,1,0,1,0,1,0,1 }, min[12] { 1,0,1,1,0,1,0,1,1,0,1,0 };
    const int d = ((midi - key) % 12 + 12) % 12; return scale ? min[d] : maj[d];
}
inline int snapToScale(int midi, int key, int scale, float cents = 0) {   // the nearest scale note (ties go the way the voice leaned)
    if (key < 0 || inScale(midi, key, scale)) return midi;
    const bool up = cents >= 0; for (int d = 1; d < 7; ++d) { const int a = up ? midi + d : midi - d, b = up ? midi - d : midi + d; if (inScale(a, key, scale)) return a; if (inScale(b, key, scale)) return b; }
    return midi;
}

// the working notes from the as-heard notes with a PITCH setting (keyOverride: -1 = the detected key, else 0..11 + 12 * minor)
inline void applyPitch(Take& T, int pitchMode, int keyOverride = -1) {
    int key = T.key, scale = T.scale; if (keyOverride >= 0) { key = keyOverride % 12; scale = keyOverride / 12; }
    T.notes = T.origNotes;
    for (auto& n : T.notes) {
        if (pitchMode == PitchOriginal) continue;                       // the cents stay: playback / MIDI bend to the sung pitch
        if (pitchMode == PitchSnapKey && key >= 0) n.midi = snapToScale(n.midi, key, scale, n.cents);
        n.cents = 0;
    }
}

// ---------------------------------------------------------------- JSON (plugin state, files)
inline juce::var toVar(const Take& T) {
    auto* o = new juce::DynamicObject(); juce::var v(o);
    o->setProperty("mode", T.mode); o->setProperty("seconds", T.seconds); o->setProperty("bpm", T.bpm); o->setProperty("origin", T.origin); o->setProperty("bpmKnown", T.bpmKnown);
    o->setProperty("key", T.key); o->setProperty("scale", T.scale); o->setProperty("swingHeard", T.swingHeard);
    juce::Array<juce::var> offers; for (double b : T.bpmOffers) offers.add(b); o->setProperty("offers", offers);
    auto notes = [](const std::vector<Note>& ns) { juce::Array<juce::var> a; for (auto& n : ns) { juce::Array<juce::var> e { n.t, n.len, n.midi, (double) n.vel, (double) n.cents }; a.add(e); } return juce::var(a); };
    auto hits = [](const std::vector<Hit>& hs) { juce::Array<juce::var> a; for (auto& h : hs) { juce::Array<juce::var> e { h.t, h.drum, (double) h.vel, (double) h.conf }; a.add(e); } return juce::var(a); };
    o->setProperty("notes", notes(T.notes)); o->setProperty("origNotes", notes(T.origNotes)); o->setProperty("hits", hits(T.hits)); o->setProperty("origHits", hits(T.origHits));
    return v;
}
inline Take takeFromVar(const juce::var& v) {
    Take T; if (!v.isObject()) return T;
    T.mode = (int) v.getProperty("mode", 0); T.seconds = (double) v.getProperty("seconds", 0.0); T.bpm = (double) v.getProperty("bpm", 120.0); T.origin = (double) v.getProperty("origin", 0.0);
    T.bpmKnown = (bool) v.getProperty("bpmKnown", false); T.key = (int) v.getProperty("key", -1); T.scale = (int) v.getProperty("scale", 0); T.swingHeard = (double) v.getProperty("swingHeard", 50.0);
    if (auto* a = v.getProperty("offers", juce::var()).getArray()) for (auto& b : *a) T.bpmOffers.push_back((double) b);
    auto notes = [&](const char* k, std::vector<Note>& out) { if (auto* a = v.getProperty(k, juce::var()).getArray()) for (auto& e : *a) if (auto* q = e.getArray(); q && q->size() >= 4) { Note n; n.t = (double) (*q)[0]; n.len = (double) (*q)[1]; n.midi = (int) (*q)[2]; n.vel = (float) (double) (*q)[3]; if (q->size() > 4) n.cents = (float) (double) (*q)[4]; out.push_back(n); } };
    auto hits = [&](const char* k, std::vector<Hit>& out) { if (auto* a = v.getProperty(k, juce::var()).getArray()) for (auto& e : *a) if (auto* q = e.getArray(); q && q->size() >= 3) { Hit h; h.t = (double) (*q)[0]; h.drum = juce::jlimit(0, kDrums - 1, (int) (*q)[1]); h.vel = (float) (double) (*q)[2]; if (q->size() > 3) h.conf = (float) (double) (*q)[3]; out.push_back(h); } };
    notes("notes", T.notes); notes("origNotes", T.origNotes); hits("hits", T.hits); hits("origHits", T.origHits);
    if (T.bpm < 20 || T.bpm > 400 || !std::isfinite(T.bpm)) T.bpm = 120;
    return T;
}

// ---------------------------------------------------------------- undo (whole-take snapshots; every edit, CLEAN and REDO pushes one)
struct Undo {
    std::vector<Take> stack, redo; static constexpr size_t kMax = 64;
    void push(const Take& T) { stack.push_back(T); if (stack.size() > kMax) stack.erase(stack.begin()); redo.clear(); }
    bool canUndo() const { return !stack.empty(); }
    bool canRedo() const { return !redo.empty(); }
    bool undo(Take& T) { if (stack.empty()) return false; redo.push_back(T); T = stack.back(); stack.pop_back(); return true; }
    bool redoIt(Take& T) { if (redo.empty()) return false; stack.push_back(T); T = redo.back(); redo.pop_back(); return true; }
    void clear() { stack.clear(); redo.clear(); }
};

inline void sortTake(Take& T) {
    std::stable_sort(T.notes.begin(), T.notes.end(), [](const Note& a, const Note& b) { return a.t < b.t; });
    std::stable_sort(T.hits.begin(), T.hits.end(), [](const Hit& a, const Hit& b) { return a.t < b.t; });
}

} // namespace hum
