#pragma once
// ArpEZator LISTEN: a C++ port of the web app's listen.js. Hear a few seconds of music (sidechain or a file) and
// estimate tempo, key, chord loop and feel. Pure functions on a mono buffer; run it on a background thread.
#include <cstdint>
#include <string>
#include <vector>

namespace listen
{
struct HeardChord { int root = 0; std::string type; std::vector<int> iv; std::string name; };
struct Result
{
    bool ok = false; std::string why;
    double bpm = 0; int bpmInt = 0; double tempoConf = 0;
    int key = 0; std::string scale = "minor"; double keyConf = 0, keyR = 0;
    std::vector<double> chroma;
    std::vector<HeardChord> chords; std::vector<std::string> bars; double chordConf = 0;
    double density = 0; bool triplet = false; double sync = 0; std::string rate = "1/16"; double groove = 0, centroid = 1500; int oct = 2; std::string preset = "pluck";
    double conf = 0, dur = 0, rms = 0;
};
Result analyze (const std::vector<float>& mono, double sampleRate);

// test audio like the web self-test: drums (kick on beats, snare on 2 + 4, hats on 8ths) + a saw pad, one chord per bar
struct SynthSpec { double bpm = 120; std::vector<std::vector<int>> prog; double dur = 12; double sr = 44100; bool noise = false; double swing = 0, lead = 0; };
std::vector<float> synth (const SynthSpec& s);
} // namespace listen
