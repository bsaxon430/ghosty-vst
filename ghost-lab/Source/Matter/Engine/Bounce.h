// Ghost Matter - AGPL-3.0-only (see LICENSE, NOTICE.md).
//
// BOUNCE: balls fall and bounce in a box, and every collision strikes a physical-model voice: Flues PM-Synth (MIT), run
// through its own LV2 plug-in code at its .ttl defaults (FluesLv2.h).
// The physics is Box2D v3.1.1 (erincatto/box2d, MIT, vendored unmodified in third_party/box2d-3.1.1): its world, gravity,
// circle bodies, walls, restitution, friction and its contact hit events. Build 1's own physics is deleted.
// Ours (the feature): the collision -> strike mapping. Ball size = pitch (big is low, on a minor pentatonic from the key
// you hold), ball kind = the struck body (WOOD = hit, BELL, DRUM, STRING = pluck), Box2D's approach speed = how hard
// (PM-Synth's own interface intensity). The world steps at a fixed 480 Hz on the audio thread, so hits land on time and the
// same throw always plays the same; every body is made in prepare(), so nothing allocates after it.
#pragma once
#include <array>
#include <atomic>
#include <memory>
#include <juce_core/juce_core.h>

#include "FluesLv2.h"
#include "HoodDefs.h"
#include <box2d/box2d.h>

namespace matter
{
class Bounce
{
public:
    static constexpr int kBalls = 12, kVoices = 8, kKinds = 4;
    Bounce();
    ~Bounce();
    void prepare (double sampleRate);
    void reset();

    // message thread: commands (lock free)
    void addBall (float x, float y, int kind, float size, float vx = 0, float vy = 0);   // x, y 0..1 (y up), size 0..1
    void clear();
    void shake (float strength = 1.0f);                       // kick every ball upwards

    struct Params { float level = 0.7f, gravity = 0.5f, bounce = 0.75f; int rootNote = 48; const hood::LivePins* pins = nullptr; };   // pins: UNDER THE HOOD (Box2D, PM-Synth)
    // audio thread: adds into L / R
    void process (float* L, float* R, int n, const Params& p) noexcept;

    // the picture (message thread reads)
    struct BallView { float x, y, r, flash; int kind; };
    int ballCount() const { return shownCount.load(); }
    BallView ball (int i) const;
    std::atomic<int> hits { 0 };
    static const char* kindName (int k);

private:
    struct Ball { b2BodyId body {}; b2ShapeId shape {}; float r = 0.05f, flash = 0; int kind = 0; bool alive = false; };
    std::array<Ball, kBalls> balls {};
    b2WorldId world {};
    struct Slots { int gravity = -1, restitution = -1, friction = -1, substeps = -1, restThresh = -1, hitThresh = -1, hertz = -1, damping = -1, push = -1,
                       maxSpeed = -1, sleep = -1, continuous = -1, linDamp = -1, angDamp = -1, density = -1; } slots;   // UNDER THE HOOD live slots (prepare)
    float restitutionNow = -1.0f, frictionNow = -1.0f;
    float worldNow[9] = { 0.1f, 0.8f, 30.0f, 10.0f, 3.0f, 400.0f, 1.0f, 1.0f, 0.0f }, bodyNow[3] = { 0.0f, 0.0f, 1.0f };   // what Box2D has now (its setters run on a change)
    std::array<float, 32> pmBase {};          // the PM-Synth ports as the voices were made (what an unset control goes back to)
    std::array<bool, 32> pmWas {};
    bool pmAny = false;
    void place (Ball& b, float x, float y, float vx, float vy, float r, int kind) noexcept;
    struct Cmd { int type; float x, y, vx, vy, size; int kind; };
    std::array<Cmd, 64> cmds {};
    juce::AbstractFifo fifo { 64 };
    void push (const Cmd& c);

    struct Voice { std::unique_ptr<fluesplug::Instance> e; int kind = 0, note = -1, quiet = 0; float gain = 0, panL = 1, panR = 1; bool on = false;
                   int onAt = -1, offAt = -1; };
    std::array<Voice, kVoices> voices;
    int nextVoice[kKinds] {};
    double sr = 48000;
    int stepEvery = 100, stepCount = 0;
    Params params;
    std::array<std::atomic<float>, kBalls * 4> shown {};
    std::array<std::atomic<int>, kBalls> shownKind {};
    std::atomic<int> shownCount { 0 };
    std::array<float, 512> mono {};
    void step (float dt) noexcept;
    void strike (const Ball& b, float speed, int frame) noexcept;
    int frameNow = 0, reuse = 0;
};
}
