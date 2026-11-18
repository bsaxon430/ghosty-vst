// Ghost Voice - AGPL-3.0-only (see LICENSE, NOTICE.md).
//
// The link to the neural voice engine. Ghost Voice never runs a neural network inside the plug-in: the engine is Seed-VC's own
// singing conversion (Plachtaa/seed-vc, GPL-3.0) in a separate Python process (helper/ghostvoice_engine.py), installed on
// request into one folder the user can delete (helper/install-engine.sh). This file:
//   - finds that folder, the helper and the installer;
//   - writes a render job (the voices, the blends to sing, Seed-VC's own settings, the rights check);
//   - runs the installer or a render on a background thread and reads the helper's progress lines;
//   - MOTION: turns a ladder of renders (the same performance sung at several A / B blends, all from the same start noise, so
//     they line up sample for sample) into one take whose blend moves in time with the song, by crossfading the neighbouring
//     renders. Nothing else touches the sound: no EQ, no limiter, no effect of ours.
#pragma once
#include <juce_core/juce_core.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>
#include <atomic>
#include <functional>
#include <map>
#include <mutex>
#include <vector>

namespace gv
{
// ---------------------------------------------------------------- where things are
juce::File engineDir();       // ~/Library/Application Support/GhostBlock/Ghost Voice/engine (GHOSTVOICE_ENGINE overrides)
juce::File workDir();         // ~/Library/Application Support/GhostBlock/Ghost Voice (takes, renders, jobs)
// Brandon 2026-11-06: no open-source project names in the visible UI (only ABOUT / CREDITS names them). Any text that may
// come from the helper or the installer (progress, errors) goes through this before it is shown.
juce::String uiWords (juce::String text);
juce::File helperDir();       // the bundled helper folder (Contents/Resources/helper), GHOSTVOICE_HELPER_DIR overrides
juce::File enginePython();    // engineDir()/venv/bin/python
bool engineInstalled();       // Seed-VC patched + the venv + the models are there
juce::String engineSummary(); // plain words for the status chip

// ---------------------------------------------------------------- a render job
struct EngineSettings
{
    int steps = 30;            // QUALITY  = Seed-VC --diffusion-steps (its default 30)
    float follow = 0.7f;       // FOLLOW   = Seed-VC --inference-cfg-rate (its default 0.7): how strongly it holds to the voice
    int semitones = 0;         // PITCH    = Seed-VC --semi-tone-shift
    bool detail = false;       // SINGER DETAIL = Seed-VC's normal reference prompt from the nearer singer (off = the lab's
                               //   promptless blend: the blend is the voice vectors alone)
    bool autoPitch = false;    // Seed-VC --auto-f0-adjust (moves the melody into the target singer's range)
    bool cpuOnly = false;      // skip the Apple GPU
    int seed = 1234;           // the start noise: the same for every blend of one render
    float novelty = 0.0f;      // NOVELTY 0..1: the voice vector turned away from both A and B (helper/ghostvoice_engine.py)
    int variation = 1;         // VARIATION: which direction NOVELTY turns it
    // UNDER THE HOOD (build 4): Seed-VC's other inference.py options, at build 3's fixed values. QUALITY / FOLLOW / PITCH / MATCH
    // RANGE above ARE --diffusion-steps / --inference-cfg-rate / --semi-tone-shift / --auto-f0-adjust: one value each.
    float lengthAdjust = 1.0f; // --length-adjust (Seed-VC default 1.0; app_svc.py range 0.5..2.0)
    bool f0Condition = true;   // --f0-condition (Seed-VC's own default is false = the speech model; Ghost Voice always sang with true)
    int fp16 = -1;             // --fp16: -1 = automatic (on for the Apple GPU, off on the CPU, as build 3), 0 = off, 1 = on
    juce::String checkpoint;   // --checkpoint (empty = Seed-VC's own pick for the f0-condition setting)
    juce::String config;       // --config (goes with the checkpoint)
    bool operator== (const EngineSettings&) const = default;
};

struct Job
{
    juce::StringArray a, b;    // the takes of voice A and voice B
    juce::String performance;  // the performance that is re-sung
    std::vector<float> mixes;  // B weights to sing (0 = A, 1 = B)
    EngineSettings settings;
    bool rights = false;       // "I have the rights to these voices"
    juce::File outDir;
    std::vector<std::pair<float, juce::String>> measure;   // MEASURE: takes of the other engines to measure (mix, file)
    double fakeDelay = 0;      // tests only: the stand-in engine waits this long before singing
};
juce::String jobToJson (const Job&);   // keys for the UNDER THE HOOD options are written only when they differ from build 3's values
// why a job cannot run yet (empty = it can): no rights tick, a missing voice / performance, a missing file
juce::String whyNot (const Job&);

// ---------------------------------------------------------------- progress lines from the helper / installer
struct Line
{
    enum Kind { progress, result, time, identity, analysis, error, done, step, installed, failed, other } kind = other;
    float value = 0;           // progress 0..1, result / time: the mix
    double seconds = 0;        // time
    juce::String text;         // the words, the result path, or the JSON (identity, analysis)
};

// what CAMPPlus measured (the helper's GV_IDENTITY): cosine similarity of the new take to voice A, to voice B, to the singer
// of the performance, and of A to B. Distance = 1 - cosine.
struct Identity
{
    bool valid = false;
    float cosA = 0, cosB = 0, cosPerf = 0, cosAB = 0;
    float distA() const { return 1.0f - cosA; }
    float distB() const { return 1.0f - cosB; }
    // UNIQUE: measured at least kFloor from A AND from B, at least half of A's distance from B from each, and no closer to the
    // performance's singer than to A or B (it is not just the performer coming through). kFloor = 0.30 because Seed-VC's render
    // of a singer measures about 0.19 from that same singer (the A/B proof, docs/AB-PROOF.md): anything nearer than ~0.3 could
    // still be that singer. The rule is shown next to the badge.
    static constexpr float kFloor = 0.30f;
    bool unique() const { const float ab = 1.0f - cosAB, need = juce::jmax (kFloor, 0.5f * ab); return valid && distA() >= need && distB() >= need && cosPerf <= juce::jmax (cosA, cosB); }
    static Identity fromJson (const juce::String&);
};
Line parseLine (const juce::String&);

// ---------------------------------------------------------------- the background runner (one task at a time)
class Runner : private juce::Thread
{
public:
    Runner();
    ~Runner() override;
    enum class State { idle, running, done, failed };
    // the program to run; tests point it at a fake helper. Default: the engine's Python + the bundled helper
    std::function<juce::StringArray (const juce::File& jobFile)> renderCommand;
    std::function<juce::StringArray()> installCommand;
    std::function<juce::StringArray (const juce::File& jobFile)> analyzeCommand;
    std::function<juce::StringArray (const juce::File& jobFile)> measureCommand;

    bool startRender (const Job&);
    bool startAnalyze (const Job&);            // CAMPPlus on A and B only (ANALYZE VOICES)
    bool startMeasure (const Job&);            // CAMPPlus on takes of NATURAL / CLASSIC / BEST OF ALL (job.measure)
    bool startInstall();
    void cancel();
    bool busy() const { return isThreadRunning(); }
    State state() const { return st.load(); }
    float progress() const { return prog.load(); }
    juce::String message() const;              // the latest words
    std::map<float, juce::File> results() const;   // mix -> WAV of the last render
    std::map<float, double> seconds() const;
    std::map<float, Identity> identities() const;  // mix -> what CAMPPlus measured
    juce::var analysis() const;                // the last ANALYZE result (an object, or void)
    bool lastWasAnalyze() const { return kind == 2; }
    bool lastWasMeasure() const { return kind == 3; }
    double elapsed() const;                    // seconds of the last task
    juce::String log() const;                  // everything the program printed
    std::function<void()> onFinished;          // called on the message thread

private:
    void run() override;
    juce::StringArray cmd;
    std::atomic<State> st { State::idle };
    std::atomic<float> prog { 0 };
    mutable std::mutex m;
    juce::String msg, fullLog;
    std::map<float, juce::File> res;
    std::map<float, double> secs;
    std::map<float, Identity> ids;
    juce::var ana;
    double t0 = 0, t1 = 0;
    int kind = 0;                              // 0 render, 1 install, 2 analyze, 3 measure
};

// ---------------------------------------------------------------- MOTION
// the blends needed so that every value of the blend curve between lo and hi has two renders around it: the quarter steps
// of 0..1 that cover [lo, hi] (a constant blend needs just that one render)
std::vector<float> ladderFor (float lo, float hi);
// one take from a ladder: at each sample the blend mixAt(seconds) picks the two neighbouring renders and crossfades them
// linearly (they are the same performance, time-aligned, so a linear fade keeps the level). mixes sorted ascending.
std::vector<float> renderMotion (const std::vector<float>& mixes, const std::vector<std::vector<float>>& renders, int fs,
                                 const std::function<float (double seconds)>& mixAt);

// audio files
bool readMono (const juce::File&, std::vector<float>& out, int& fs, juce::String& err);
bool writeWav (const juce::File&, const std::vector<float>& x, int fs, int bits = 24);
}
