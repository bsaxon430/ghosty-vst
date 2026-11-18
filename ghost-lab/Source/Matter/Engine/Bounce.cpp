// Ghost Matter - AGPL-3.0-only (see LICENSE, NOTICE.md).
#include "Bounce.h"
#include <cstring>
#include "Pattern.h"
#include <cmath>

namespace matter
{
namespace
{
// ours: a ball's size picks a note of a minor pentatonic over `octaves`
int pentatonicOffset (float v, float octaves) noexcept
{
    static constexpr int scale[5] = { 0, 3, 5, 7, 10 };
    const int notes = std::max (1, (int) std::lround (octaves * 5.0f));
    const int idx = std::clamp ((int) (v * (float) notes), 0, notes);
    return 12 * (idx / 5) + scale[idx % 5];
}
}
namespace
{
// the struck bodies: PM-Synth interface numbers (HIT, BELL, DRUM, PLUCK)
constexpr int kInterface[Bounce::kKinds] = { 1, 6, 7, 0 };
}

const char* Bounce::kindName (int k)
{
    static const char* n[kKinds] = { "WOOD", "BELL", "DRUM", "STRING" };
    return n[juce::jlimit (0, kKinds - 1, k)];
}

namespace
{
constexpr float kS = 10.0f;   // the box is 10 x 10 m in Box2D (its sweet spot is objects of 0.1 .. 10 m); the page sees 0..1
inline int ballIndex (b2ShapeId s) { return (int) (intptr_t) b2Shape_GetUserData (s) - 1; }   // -1 = a wall
}

Bounce::Bounce() = default;
Bounce::~Bounce()
{
    if (b2World_IsValid (world)) b2DestroyWorld (world);
}

void Bounce::prepare (double r)
{
    sr = r;
    slots = { hood::liveSlotOf ("box2d", "gravity"), hood::liveSlotOf ("box2d", "restitution"), hood::liveSlotOf ("box2d", "friction"),
              hood::liveSlotOf ("box2d", "substeps"), hood::liveSlotOf ("box2d", "restitution_threshold"), hood::liveSlotOf ("box2d", "hit_event_threshold"),
              hood::liveSlotOf ("box2d", "contact_hertz"), hood::liveSlotOf ("box2d", "contact_damping"), hood::liveSlotOf ("box2d", "contact_push"),
              hood::liveSlotOf ("box2d", "max_linear_speed"), hood::liveSlotOf ("box2d", "enable_sleep"), hood::liveSlotOf ("box2d", "enable_continuous"),
              hood::liveSlotOf ("box2d", "linear_damping"), hood::liveSlotOf ("box2d", "angular_damping"), hood::liveSlotOf ("box2d", "density") };
    stepEvery = juce::jmax (1, (int) std::lround (sr / 480.0));
    // the voices: Flues PM-Synth's own plug-in at its .ttl defaults, the body set per kind, its attack at 0 (a strike)
    for (int i = 0; i < kVoices; ++i)
    {
        auto& v = voices[(size_t) i];
        v.kind = i % kKinds;
        auto ports = fluesplug::defaults (fluesplug::Plugin::pmSynth);
        ports["interfaceType"] = (float) kInterface[v.kind];
        ports["attack"] = 0.0f;
        ports["dcLevel"] = 0.0f;   // its own DC (breath) level at 0: at the .ttl 0.5 DRUM sits at DC 1.7, past full scale
        v.e = std::make_unique<fluesplug::Instance> (fluesplug::Plugin::pmSynth, ports, sr);
        if (i == 0)
            for (int k = 0; k < 19 && hood::liveBase ("pm") >= 0; ++k)
            {
                auto it = ports.find (hood::liveDef (hood::liveBase ("pm") + k).id);
                pmBase[(size_t) k] = it != ports.end() ? it->second : 0.0f;
            }
    }
    pmWas.fill (false);
    // the Box2D world: walls round the box, every ball made now (disabled until thrown in)
    if (b2World_IsValid (world)) b2DestroyWorld (world);
    b2WorldDef wd = b2DefaultWorldDef();
    wd.gravity = { 0.0f, -10.0f };
    wd.restitutionThreshold = 0.1f;
    wd.hitEventThreshold = 0.8f;      // m/s: slower touches strike nothing
    wd.workerCount = 1;
    world = b2CreateWorld (&wd);
    b2BodyDef wallDef = b2DefaultBodyDef();
    auto walls = b2CreateBody (world, &wallDef);
    b2ShapeDef sd = b2DefaultShapeDef();
    sd.material.restitution = 0.0f;   // Box2D mixes restitution as the larger of the two: the balls' value rules
    sd.material.friction = 0.3f;
    sd.enableHitEvents = true;
    const b2Vec2 c[4] = { { 0, 0 }, { kS, 0 }, { kS, kS }, { 0, kS } };
    for (int i = 0; i < 4; ++i) { b2Segment seg { c[i], c[(i + 1) % 4] }; b2CreateSegmentShape (walls, &sd, &seg); }
    for (int i = 0; i < kBalls; ++i)
    {
        auto& b = balls[(size_t) i];
        b2BodyDef bd = b2DefaultBodyDef();
        bd.type = b2_dynamicBody;
        bd.position = { kS * (0.1f + 0.07f * (float) i), kS * 0.5f };
        b.body = b2CreateBody (world, &bd);
        b2ShapeDef bs = b2DefaultShapeDef();
        bs.density = 1.0f;
        bs.material.friction = 0.3f;
        bs.material.restitution = 0.75f;
        bs.enableHitEvents = true;
        bs.userData = (void*) (intptr_t) (i + 1);
        b2Circle circle { { 0, 0 }, 0.5f };
        b.shape = b2CreateCircleShape (b.body, &bs, &circle);
    }
    // warm-up: every ball in, a pile-up, so Box2D's own arrays have grown to what twelve balls need; then all out again
    for (int k = 0; k < 240; ++k) b2World_Step (world, 1.0f / 480.0f, 2);
    // and the path a throw takes: out, back in at the largest size (so every array Box2D grows on the way has grown)
    for (int round = 0; round < 3; ++round)
    {
        for (auto& b : balls) b2Body_Disable (b.body);
        b2World_Step (world, 1.0f / 480.0f, 2);
        for (int i = 0; i < kBalls; ++i) place (balls[(size_t) i], 0.1f + 0.065f * (float) i, 0.3f + 0.05f * (float) (i % 3), 0.5f, 0.0f, 0.1f, i % kKinds);
        for (int k = 0; k < 480; ++k) b2World_Step (world, 1.0f / 480.0f, 2);
    }
    restitutionNow = -1.0f;
    reset();
}

void Bounce::reset()
{
    for (auto& b : balls) { b.alive = false; if (b2Body_IsValid (b.body)) b2Body_Disable (b.body); }
    for (auto& v : voices) { v.on = false; v.gain = 0; v.note = -1; v.onAt = v.offAt = -1; }
    shownCount = 0;
}

void Bounce::push (const Cmd& c)
{
    int s1, n1, s2, n2;
    fifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 > 0) cmds[(size_t) s1] = c; else if (n2 > 0) cmds[(size_t) s2] = c;
    fifo.finishedWrite (n1 + n2);
}

void Bounce::addBall (float x, float y, int kind, float size, float vx, float vy) { push ({ 0, x, y, vx, vy, size, kind }); }
void Bounce::clear() { push ({ 1, 0, 0, 0, 0, 0, 0 }); }
void Bounce::shake (float s) { push ({ 2, 0, 0, 0, s, 0, 0 }); }

Bounce::BallView Bounce::ball (int i) const
{
    const size_t k = (size_t) juce::jlimit (0, kBalls - 1, i) * 4;
    return { shown[k].load(), shown[k + 1].load(), shown[k + 2].load(), shown[k + 3].load(), shownKind[(size_t) juce::jlimit (0, kBalls - 1, i)].load() };
}

void Bounce::place (Ball& b, float x, float y, float vx, float vy, float r, int kind) noexcept
{
    b.r = r; b.kind = kind; b.flash = 0.6f; b.alive = true;
    b2Circle circle { { 0, 0 }, r * kS };
    b2Shape_SetCircle (b.shape, &circle);
    b2Body_SetTransform (b.body, { juce::jlimit (r, 1 - r, x) * kS, juce::jlimit (r, 1 - r, y) * kS }, b2MakeRot (0));
    b2Body_Enable (b.body);
    b2Body_SetLinearVelocity (b.body, { vx * kS, vy * kS });
    b2Body_SetAngularVelocity (b.body, 0);
    b2Body_SetAwake (b.body, true);
}

void Bounce::strike (const Ball& b, float speed, int frame) noexcept
{
    const float hard = juce::jlimit (0.0f, 1.0f, speed / 2.2f);
    if (hard < 0.04f) return;
    // a voice of this body: the next one round
    Voice* v = nullptr;
    for (int k = 0; k < kVoices && v == nullptr; ++k)
    {
        auto& c = voices[(size_t) ((nextVoice[b.kind] + k) % kVoices)];
        if (c.kind == b.kind) v = &c;
    }
    if (v == nullptr || v->e == nullptr) return;
    nextVoice[b.kind] = (int) ((v - voices.data()) + 1) % kVoices;
    // big balls are low: a minor pentatonic down from two octaves over the key
    const float size01 = juce::jlimit (0.0f, 1.0f, (b.r - 0.025f) / 0.075f);
    const int note = juce::jlimit (0, 127, params.rootNote + 24 - pentatonicOffset (size01, 2.6f));
    v->e->set ("interfaceIntensity", 0.25f + 0.7f * hard);   // PM-Synth's own strike strength
    if (v->note >= 0) v->e->noteOff (v->note, frame);
    v->e->noteOn (note, 110, frame);
    v->note = note;
    v->offAt = frame + (int) (0.03 * sr);
    v->on = true; v->quiet = 0;
    v->gain = 0.35f + 0.65f * hard;
    const float x = b2Body_GetPosition (b.body).x / kS;
    const float pan = juce::jlimit (-1.0f, 1.0f, x * 2.0f - 1.0f) * 0.8f;
    v->panL = std::sqrt (0.5f * (1.0f - pan)) * 1.41421356f; v->panR = std::sqrt (0.5f * (1.0f + pan)) * 1.41421356f;
    ++hits;
}

void Bounce::step (float dt) noexcept
{
    // BOUNCE's knobs, or the Box2D settings you moved UNDER THE HOOD (each by its own id in the table, in Box2D's own units)
    const auto* pn = params.pins;
    const auto& sl = slots;   // looked up in prepare() (no strings on the audio thread)
    auto pinOn = [pn] (int slot) { return pn != nullptr && slot >= 0 && pn->on[slot]; };
    auto pin = [pn] (int slot, float normal) { return pn != nullptr && slot >= 0 ? pn->pick (slot, normal) : normal; };
    const float g = 0.5f + 5.5f * params.gravity;               // box heights per second squared
    b2World_SetGravity (world, { 0.0f, pinOn (sl.gravity) ? -pin (sl.gravity, 0.0f) : -g * kS });
    const float e = pinOn (sl.restitution) ? pin (sl.restitution, 0.0f) : juce::jlimit (0.05f, 0.97f, params.bounce);
    if (e != restitutionNow)
    {
        restitutionNow = e;
        for (auto& b : balls) b2Shape_SetRestitution (b.shape, e);
    }
    const float fr = pin (sl.friction, 0.3f);
    if (fr != frictionNow)
    {
        frictionNow = fr;
        for (auto& b : balls) b2Shape_SetFriction (b.shape, fr);
    }
    // the rest of b2WorldDef / b2BodyDef / b2ShapeDef, through Box2D's own setters, only when a value changed
    const float wv[9] = { pin (sl.restThresh, 0.1f), pin (sl.hitThresh, 0.8f), pin (sl.hertz, 30.0f), pin (sl.damping, 10.0f), pin (sl.push, 3.0f),
                          pin (sl.maxSpeed, 400.0f), pin (sl.sleep, 1.0f), pin (sl.continuous, 1.0f), 0.0f };
    if (wv[0] != worldNow[0]) b2World_SetRestitutionThreshold (world, wv[0]);
    if (wv[1] != worldNow[1]) b2World_SetHitEventThreshold (world, wv[1]);
    if (wv[2] != worldNow[2] || wv[3] != worldNow[3] || wv[4] != worldNow[4]) b2World_SetContactTuning (world, wv[2], wv[3], wv[4]);
    if (wv[5] != worldNow[5]) b2World_SetMaximumLinearSpeed (world, wv[5]);
    if (wv[6] != worldNow[6]) b2World_EnableSleeping (world, wv[6] > 0.5f);
    if (wv[7] != worldNow[7]) b2World_EnableContinuous (world, wv[7] > 0.5f);
    for (int i = 0; i < 9; ++i) worldNow[i] = wv[i];
    const float bv[3] = { pin (sl.linDamp, 0.0f), pin (sl.angDamp, 0.0f), pin (sl.density, 1.0f) };
    if (bv[0] != bodyNow[0]) for (auto& b : balls) b2Body_SetLinearDamping (b.body, bv[0]);
    if (bv[1] != bodyNow[1]) for (auto& b : balls) b2Body_SetAngularDamping (b.body, bv[1]);
    if (bv[2] != bodyNow[2]) for (auto& b : balls) b2Shape_SetDensity (b.shape, bv[2], true);
    for (int i = 0; i < 3; ++i) bodyNow[i] = bv[i];
    b2World_Step (world, dt, pinOn (sl.substeps) ? juce::jlimit (1, 8, (int) std::lround (pin (sl.substeps, 2.0f))) : 2);
    for (auto& b : balls) b.flash *= 0.985f;
    const auto ev = b2World_GetContactEvents (world);
    for (int i = 0; i < ev.hitCount; ++i)
    {
        const auto& h = ev.hitEvents[i];
        const int a = ballIndex (h.shapeIdA), c = ballIndex (h.shapeIdB);
        const float speed = h.approachSpeed / kS;
        const bool both = a >= 0 && c >= 0;
        for (int k : { a, c })
            if (k >= 0 && balls[(size_t) k].alive) { strike (balls[(size_t) k], both ? speed * 0.8f : speed, frameNow); balls[(size_t) k].flash = 1; }
    }
}

void Bounce::process (float* L, float* R, int n, const Params& p) noexcept
{
    params = p;
    if (p.pins != nullptr && hood::liveBase ("pm") >= 0 && (p.pins->any || pmAny))
    {
        // PM-Synth's own ports you moved UNDER THE HOOD, on the struck voices (the body and strike strength come from the ball)
        const int pb = hood::liveBase ("pm");
        pmAny = false;
        for (int k = 0; k < 19; ++k)
        {
            const char* id = hood::liveDef (pb + k).id;
            if (! std::strcmp (id, "interfaceType") || ! std::strcmp (id, "interfaceIntensity")) continue;
            const bool on = p.pins->on[pb + k];
            if (on || pmWas[(size_t) k])
                for (auto& v : voices) if (v.e != nullptr) v.e->set (hood::liveDef (pb + k).id, on ? p.pins->v[pb + k] : pmBase[(size_t) k]);
            pmWas[(size_t) k] = on;
            pmAny = pmAny || on;
        }
    }
    // commands from the page
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (fifo.getNumReady(), s1, n1, s2, n2);
        auto run = [this] (const Cmd& c)
        {
            if (c.type == 0)
            {
                Ball* slot = nullptr;
                for (auto& b : balls) if (! b.alive) { slot = &b; break; }
                if (slot == nullptr) { slot = &balls[(size_t) reuse]; reuse = (reuse + 1) % kBalls; }
                const float r = 0.025f + 0.075f * juce::jlimit (0.0f, 1.0f, c.size);
                place (*slot, c.x, c.y, c.vx, c.vy, r, juce::jlimit (0, kKinds - 1, c.kind));
            }
            else if (c.type == 1) { for (auto& b : balls) if (b.alive) { b.alive = false; b2Body_Disable (b.body); } }
            else if (c.type == 2)
            {
                uint32_t s = 12345;
                for (auto& b : balls)
                    if (b.alive)
                    {
                        s = s * 1664525u + 1013904223u;
                        auto v = b2Body_GetLinearVelocity (b.body);
                        v.y += 2.0f * c.vy * kS; v.x += ((float) (s >> 8) / 16777216.0f - 0.5f) * 1.5f * c.vy * kS;
                        b2Body_SetLinearVelocity (b.body, v);
                        b2Body_SetAwake (b.body, true);
                    }
            }
        };
        for (int i = 0; i < n1; ++i) run (cmds[(size_t) (s1 + i)]);
        for (int i = 0; i < n2; ++i) run (cmds[(size_t) (s2 + i)]);
        fifo.finishedRead (n1 + n2);
    }
    const float dt = (float) stepEvery / (float) sr;
    const float lvl = p.level * 0.55f;
    for (int at = 0; at < n;)
    {
        // a stretch of samples up to the next physics step
        if (stepCount == 0) { frameNow = 0; step (dt); }
        const int len = juce::jmin (n - at, stepEvery - stepCount, 256);
        for (auto& v : voices)
        {
            if (! v.on || v.e == nullptr) continue;
            if (v.offAt >= 0 && v.offAt < len) { v.e->noteOff (v.note, v.offAt); v.note = -1; v.offAt = -1; }
            else if (v.offAt >= 0) v.offAt -= len;
            v.e->run (mono.data(), len);
            float pk = 0;
            for (int i = 0; i < len; ++i)
            {
                const float x = mono[(size_t) i] * v.gain * lvl;
                L[at + i] += x * v.panL; R[at + i] += x * v.panR;
                pk = juce::jmax (pk, std::abs (x));
            }
            if (pk < 1e-5f) { v.quiet += len; if (v.quiet > (int) (0.2 * sr) && v.note < 0) v.on = false; } else v.quiet = 0;
        }
        at += len;
        stepCount += len;
        if (stepCount >= stepEvery) stepCount = 0;
    }
    int count = 0;
    for (int i = 0; i < kBalls; ++i)
    {
        const auto& b = balls[(size_t) i];
        if (! b.alive) continue;
        const auto pos = b2Body_GetPosition (b.body);
        const size_t k = (size_t) count * 4;
        shown[k] = pos.x / kS; shown[k + 1] = pos.y / kS; shown[k + 2] = b.r; shown[k + 3] = b.flash;
        shownKind[(size_t) count] = b.kind;
        ++count;
    }
    shownCount = count;
}
}
