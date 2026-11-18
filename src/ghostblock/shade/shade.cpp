/*
 * SEANCE (GhostBlock) - the SHADE door. See shade.h. Wires Vital's SynthOscillator the way Vital's own
 * OscillatorModule does (the same inputs, Vital's default values for the ones without a knob here).
 */
#include "shade.h"
#include "synth_oscillator.h"
#include "wavetable.h"
#include "value.h"
#include "wavetable_creator.h"
#include <atomic>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <vector>

using namespace vital;
namespace
{
constexpr float PI2 = 6.283185307179586f;
constexpr int kTables = 8, kFrames = 16;
const char *kTableNames[kTables] = {"Saw to Square", "Fold", "Harmonic Ladder", "Vowels", "Pulse Width", "Bell Metal", "Drawbars", "Grit"};
const char *kParamNames[SHADE_PARAMS] = {"TABLE", "FRAME", "MORPH TYPE", "MORPH", "WARP TYPE", "WARP", "UNISON"};
const float kDefaults[SHADE_PARAMS] = {0.f, 0.f, 0.f, 0.5f, 0.f, 0.5f, 0.f};
// Vital's spectral morphs and the warps that need no second oscillator. Left out: Shepard Tone (it needs a table
// built for it) and Inharmonic Stretch (driven this way, outside Vital's own engine, its output ran away to
// thousands of times full scale; cause not found yet, so it is not offered).
const SynthOscillator::SpectralMorph kMorphs[] = {SynthOscillator::kNoSpectralMorph, SynthOscillator::kVocode, SynthOscillator::kFormScale,
                                                  SynthOscillator::kHarmonicScale, SynthOscillator::kSmear,
                                                  SynthOscillator::kRandomAmplitudes, SynthOscillator::kLowPass, SynthOscillator::kHighPass,
                                                  SynthOscillator::kPhaseDisperse, SynthOscillator::kSkew};
const char *kMorphNames[] = {"Off", "Vocode", "Formant Scale", "Harmonic Stretch", "Smear", "Random Amplitudes",
                             "Low Pass", "High Pass", "Phase Disperse", "Skew"};
constexpr int kNumMorphs = 10;
const SynthOscillator::DistortionType kWarps[] = {SynthOscillator::kNone, SynthOscillator::kSync, SynthOscillator::kFormant, SynthOscillator::kQuantize,
                                                  SynthOscillator::kBend, SynthOscillator::kSqueeze, SynthOscillator::kPulseWidth};
const char *kWarpNames[] = {"Off", "Sync", "Formant", "Quantize", "Bend", "Squeeze", "Pulse Width"};
constexpr int kNumWarps = 7;

int stepOf(float v01, int steps) { return std::max(0, std::min(steps - 1, (int)std::lround(v01 * (float)(steps - 1)))); }

// one frame of one of our own tables, one cycle, by formula
void makeFrame(int table, int frame, float *buf)
{
    const int N = Wavetable::kWaveformSize;
    const float t = (float)frame / (float)(kFrames - 1); // 0 .. 1 through the table
    auto additive = [&](auto amp) { // amp(h) for harmonic h = 1 ..
        std::memset(buf, 0, sizeof(float) * N);
        for (int h = 1; h <= 256; ++h)
        {
            const float a = amp(h);
            if (a == 0.f) continue;
            for (int i = 0; i < N; ++i) buf[i] += a * std::sin(PI2 * (float)h * (float)i / N);
        }
    };
    switch (table)
    {
    case 0: // saw -> square: the even harmonics fade out
        additive([&](int h) { return (h % 2 ? 1.f : 1.f - t) / (float)h; });
        break;
    case 1: // a sine folded harder and harder
        for (int i = 0; i < N; ++i) buf[i] = std::sin((1.f + 7.f * t) * 1.5708f * std::sin(PI2 * (float)i / N));
        break;
    case 2: // one harmonic at a time, climbing (with its neighbours leaking in)
        additive([&](int h) { const float c = 1.f + t * 15.f, d = (float)h - c; return std::exp(-d * d * 1.2f); });
        break;
    case 3: // a buzz through two moving formant peaks (ah .. ee .. oo)
        additive([&](int h) {
            const float f1 = 3.f + 5.f * std::sin(t * 3.1416f), f2 = 9.f + 14.f * t, d1 = (float)h - f1, d2 = (float)h - f2;
            return (std::exp(-d1 * d1 / 3.f) + 0.7f * std::exp(-d2 * d2 / 8.f)) + 0.04f / (float)h;
        });
        break;
    case 4: // pulse, 50 % down to 4 %
    {
        const float w = 0.5f - 0.46f * t;
        additive([&](int h) { return std::sin(3.14159265f * (float)h * w) / (float)h; });
        break;
    }
    case 5: // bell: stretched partials rounded onto the harmonic grid
        additive([&](int h) {
            static const float part[8] = {1.f, 2.76f, 5.4f, 8.93f, 13.34f, 18.64f, 24.8f, 31.87f};
            float a = 0;
            for (int k = 0; k < 8; ++k)
                if (h == (int)std::lround(part[k] * (1.f + t))) a += 1.f / (1.f + (float)k * (1.2f - t));
            return a;
        });
        break;
    case 6: // organ drawbars pulled out one after another
        additive([&](int h) {
            static const int bar[9] = {1, 2, 3, 4, 6, 8, 10, 12, 16};
            for (int k = 0; k < 9; ++k)
                if (h == bar[k]) return std::max(0.f, std::min(1.f, t * 9.f - (float)k + 1.f)) / (1.f + 0.3f * (float)k);
            return 0.f;
        });
        break;
    default: // a saw crushed onto fewer and fewer steps
    {
        const float steps = 64.f * std::pow(2.f, -5.f * t);
        for (int i = 0; i < N; ++i)
        {
            const float s = 1.f - 2.f * (float)i / N;
            buf[i] = std::round(s * steps) / steps;
        }
        break;
    }
    }
    float pk = 1e-9f, dc = 0;
    for (int i = 0; i < N; ++i) dc += buf[i];
    dc /= N;
    for (int i = 0; i < N; ++i) { buf[i] -= dc; pk = std::max(pk, std::fabs(buf[i])); }
    for (int i = 0; i < N; ++i) buf[i] /= pk;
}
} // namespace

struct ShadeWorld
{
    struct Slot
    {
        std::unique_ptr<Wavetable> table;
        int loaded{-1};
        std::atomic<bool> user{false};     // a .vitaltable is loaded: the audio thread leaves the table alone
        std::atomic_flag busy = ATOMIC_FLAG_INIT; // one thread at a time writes the table
        int userFrames{0};
        unsigned long long userTag{0};
        float p[SHADE_PARAMS]{};
        // what Vital's OscillatorModule plugs in as controls; shared by this seat's voices
        cr::Value waveFrame{0.f}, unisonVoices{1.f}, morphType{0.f}, morphAmount{0.5f}, warpType{0.f}, warpAmount{0.5f}, randomPhase{0.f};
    };
    std::array<Slot, SHADE_SLOTS> slots;
    std::vector<std::vector<WaveFrame>> frames; // [table][frame], made once
    int sampleRate{48000};
    // Vital's defaults for everything without a knob here
    cr::Value midiTrack{1.f}, smooth{0.f}, transposeQuantize{0.f}, pan{0.f}, unisonDetune{4.472f}, distortionPhase{0.5f}, blend{0.8f},
        stereoSpread{1.f}, stackStyle{0.f}, detunePower{1.5f}, detuneRange{2.f}, frameSpread{0.f}, distortionSpread{0.f}, morphSpread{0.f},
        spectralUnison{1.f}, activeVoices{1.f};
    Output zero{kMaxBufferSize, 1}, one{kMaxBufferSize, 1}; // transpose / tune = 0, level = 1 (audio-rate inputs)
    std::vector<Value *> all()
    {
        std::vector<Value *> v{&midiTrack, &smooth, &transposeQuantize, &pan, &unisonDetune, &distortionPhase, &blend, &stereoSpread, &stackStyle,
                               &detunePower, &detuneRange, &frameSpread, &distortionSpread, &morphSpread, &spectralUnison, &activeVoices};
        for (auto &s : slots)
            for (auto *x : {&s.waveFrame, &s.unisonVoices, &s.morphType, &s.morphAmount, &s.warpType, &s.warpAmount, &s.randomPhase}) v.push_back(x);
        return v;
    }
    void push() { for (auto *v : all()) v->process(1); }
    void loadTable(Slot &s, int t)
    {
        s.table->setNumFrames(kFrames);
        for (int f = 0; f < kFrames; ++f) s.table->loadWaveFrame(&frames[(size_t)t][(size_t)f], f);
        s.loaded = t;
    }
    ShadeWorld()
    {
        std::vector<float> buf((size_t)Wavetable::kWaveformSize);
        frames.resize(kTables);
        for (int t = 0; t < kTables; ++t)
        {
            frames[(size_t)t].resize(kFrames);
            for (int f = 0; f < kFrames; ++f)
            {
                makeFrame(t, f, buf.data());
                frames[(size_t)t][(size_t)f].loadTimeDomain(buf.data());
                frames[(size_t)t][(size_t)f].index = f;
            }
        }
        for (int i = 0; i < kMaxBufferSize; ++i) { zero.buffer[i] = 0.f; one.buffer[i] = 1.f; }
        for (auto &s : slots)
        {
            s.table = std::make_unique<Wavetable>(kNumOscillatorWaveFrames);
            loadTable(s, 0);
            std::memcpy(s.p, kDefaults, sizeof(kDefaults));
        }
        for (auto *v : all()) v->setSampleRate(sampleRate);
        push();
    }
};

struct ShadeOsc
{
    ShadeWorld &w;
    ShadeWorld::Slot &slot;
    SynthOscillator osc;
    cr::Value note{60.f};
    cr::Value frame{0.f}, morphAmount{0.5f}, warpAmount{0.5f}; // per voice: the seat's knob + this voice's movement
    Output phase{kMaxBufferSize, 1};
    ShadeOsc(ShadeWorld &world, int s) : w(world), slot(world.slots[(size_t)s]), osc(slot.table.get())
    {
        osc.plug(&frame, SynthOscillator::kWaveFrame);
        osc.plug(&note, SynthOscillator::kMidiNote);
        osc.plug(&w.midiTrack, SynthOscillator::kMidiTrack);
        osc.plug(&w.smooth, SynthOscillator::kSmoothlyInterpolate);
        osc.plug(&w.zero, SynthOscillator::kTranspose);
        osc.plug(&w.transposeQuantize, SynthOscillator::kTransposeQuantize);
        osc.plug(&w.zero, SynthOscillator::kTune);
        osc.plug(&w.one, SynthOscillator::kAmplitude);
        osc.plug(&w.pan, SynthOscillator::kPan);
        osc.plug(&slot.unisonVoices, SynthOscillator::kUnisonVoices);
        osc.plug(&w.unisonDetune, SynthOscillator::kUnisonDetune);
        osc.plug(&phase, SynthOscillator::kPhase);
        osc.plug(&w.distortionPhase, SynthOscillator::kDistortionPhase);
        osc.plug(&slot.randomPhase, SynthOscillator::kRandomPhase);
        osc.plug(&w.blend, SynthOscillator::kBlend);
        osc.plug(&w.stereoSpread, SynthOscillator::kStereoSpread);
        osc.plug(&w.stackStyle, SynthOscillator::kStackStyle);
        osc.plug(&w.detunePower, SynthOscillator::kDetunePower);
        osc.plug(&w.detuneRange, SynthOscillator::kDetuneRange);
        osc.plug(&w.frameSpread, SynthOscillator::kUnisonFrameSpread);
        osc.plug(&w.distortionSpread, SynthOscillator::kUnisonDistortionSpread);
        osc.plug(&w.morphSpread, SynthOscillator::kUnisonSpectralMorphSpread);
        osc.plug(&slot.morphType, SynthOscillator::kSpectralMorphType);
        osc.plug(&morphAmount, SynthOscillator::kSpectralMorphAmount);
        osc.plug(&w.spectralUnison, SynthOscillator::kSpectralUnison);
        osc.plug(&slot.warpType, SynthOscillator::kDistortionType);
        osc.plug(&warpAmount, SynthOscillator::kDistortionAmount);
        osc.plug(&w.activeVoices, SynthOscillator::kActiveVoices);
        osc.setSampleRate(w.sampleRate);
        note.setSampleRate(w.sampleRate);
        for (auto *v : {&frame, &morphAmount, &warpAmount}) v->setSampleRate(w.sampleRate);
        for (int i = 0; i < kMaxBufferSize; ++i) phase.buffer[i] = 0.5f;
    }
};

extern "C"
{
int shade_table_count(void) { return kTables; }
const char *shade_table_name(int t) { return t >= 0 && t < kTables ? kTableNames[t] : ""; }
const char *shade_param_name(int p) { return p >= 0 && p < SHADE_PARAMS ? kParamNames[p] : ""; }
float shade_param_default01(int p) { return p >= 0 && p < SHADE_PARAMS ? kDefaults[p] : 0.f; }
int shade_param_steps(int p) { return p == 0 ? kTables : p == 2 ? kNumMorphs : p == 4 ? kNumWarps : p == 6 ? 16 : 0; }
void shade_param_display(int p, float v, char *out, int n)
{
    switch (p)
    {
    case 0: std::snprintf(out, (size_t)n, "%s", kTableNames[stepOf(v, kTables)]); break;
    case 1: std::snprintf(out, (size_t)n, "%.1f", 1.f + v * (float)(kFrames - 1)); break;
    case 2: std::snprintf(out, (size_t)n, "%s", kMorphNames[stepOf(v, kNumMorphs)]); break;
    case 4: std::snprintf(out, (size_t)n, "%s", kWarpNames[stepOf(v, kNumWarps)]); break;
    case 6: std::snprintf(out, (size_t)n, "%d", 1 + stepOf(v, 16)); break;
    default: std::snprintf(out, (size_t)n, "%.0f %%", v * 100.f); break;
    }
}
int shade_param_live(int p, const float *a)
{
    if (p == 3) return stepOf(a[2], kNumMorphs) != 0; // MORPH needs a morph type
    if (p == 5) return stepOf(a[4], kNumWarps) != 0;  // WARP needs a warp type
    return 1;
}
ShadeWorld *shade_world_create(void) { return new ShadeWorld(); }
void shade_world_destroy(ShadeWorld *w) { delete w; }
void shade_world_set_rate(ShadeWorld *w, double sr)
{
    w->sampleRate = (int)std::lround(sr);
    for (auto *v : w->all()) v->setSampleRate(w->sampleRate);
    w->push();
}
void shade_slot_set_params01(ShadeWorld *w, int slot, const float *a)
{
    if (slot < 0 || slot >= SHADE_SLOTS) return;
    auto &s = w->slots[(size_t)slot];
    if (std::memcmp(s.p, a, sizeof(s.p)) == 0) return;
    std::memcpy(s.p, a, sizeof(s.p));
    const int t = stepOf(a[0], kTables);
    if (!s.user.load() && t != s.loaded)
    {
        if (s.busy.test_and_set()) { s.p[0] = -1.f; return; } // the main thread is writing this table: try again next block
        if (!s.user.load()) w->loadTable(s, t);
        s.busy.clear();
    }
    const int uni = 1 + stepOf(a[6], 16);
    s.waveFrame.set(a[1] * (float)(std::max(1, s.table->numFrames()) - 1));
    s.morphType.set((float)kMorphs[stepOf(a[2], kNumMorphs)]);
    s.morphAmount.set(a[3]);
    s.warpType.set((float)kWarps[stepOf(a[4], kNumWarps)]);
    s.warpAmount.set(a[5]);
    s.unisonVoices.set((float)uni);
    s.randomPhase.set(uni > 1 ? 1.f : 0.f); // one voice starts every note at the same phase; a stack starts scattered, as in Vital
    for (auto *x : {&s.waveFrame, &s.unisonVoices, &s.morphType, &s.morphAmount, &s.warpType, &s.warpAmount, &s.randomPhase}) x->process(1);
}
int shade_slot_load_vitaltable(ShadeWorld *w, int slot, const char *text, size_t len)
{
    if (!w || slot < 0 || slot >= SHADE_SLOTS || !text || len == 0) return 0;
    auto &s = w->slots[(size_t)slot];
    json data;
    try
    {
        data = json::parse(std::string(text, len));
    }
    catch (const std::exception &)
    {
        shade_slot_clear_user_table(w, slot);
        return 0;
    }
    if (!data.is_object())
    {
        shade_slot_clear_user_table(w, slot);
        return 0;
    }
    while (s.busy.test_and_set()) {}
    s.user.store(true);
    int frames = 0;
    try
    {
        WavetableCreator creator(s.table.get());
        creator.jsonToState(data);
        creator.render();
        frames = s.table->numFrames();
    }
    catch (const std::exception &)
    {
        frames = 0;
    }
    s.userFrames = frames;
    if (frames <= 0)
    { // not a usable table: hand the seat back to the built-in ones
        s.user.store(false);
        s.loaded = -1;
    }
    s.p[0] = -2.f; // make the audio thread set its values again (the frame count changed)
    s.busy.clear();
    return frames;
}
void shade_slot_clear_user_table(ShadeWorld *w, int slot)
{
    if (!w || slot < 0 || slot >= SHADE_SLOTS) return;
    auto &s = w->slots[(size_t)slot];
    while (s.busy.test_and_set()) {}
    s.user.store(false);
    s.userFrames = 0;
    s.loaded = -1;
    s.p[0] = -2.f;
    s.busy.clear();
}
int shade_slot_user_frames(ShadeWorld *w, int slot) { return w && slot >= 0 && slot < SHADE_SLOTS && w->slots[(size_t)slot].user.load() ? w->slots[(size_t)slot].userFrames : 0; }
unsigned long long shade_slot_user_tag(ShadeWorld *w, int slot) { return w && slot >= 0 && slot < SHADE_SLOTS ? w->slots[(size_t)slot].userTag : 0; }
void shade_slot_set_user_tag(ShadeWorld *w, int slot, unsigned long long tag) { if (w && slot >= 0 && slot < SHADE_SLOTS) w->slots[(size_t)slot].userTag = tag; }
char *shade_wav_to_vitaltable(const void *wav, size_t bytes, const char *name, int *framesOut)
{
    if (framesOut) *framesOut = 0;
    const unsigned char *p = (const unsigned char *)wav;
    auto u16 = [&](size_t o) { return (unsigned)p[o] | ((unsigned)p[o + 1] << 8); };
    auto u32 = [&](size_t o) { return (unsigned)p[o] | ((unsigned)p[o + 1] << 8) | ((unsigned)p[o + 2] << 16) | ((unsigned)p[o + 3] << 24); };
    if (!p || bytes < 44 || std::memcmp(p, "RIFF", 4) != 0 || std::memcmp(p + 8, "WAVE", 4) != 0) return nullptr;
    unsigned fmt = 0, channels = 0, bits = 0;
    size_t dataAt = 0, dataLen = 0;
    int cycle = Wavetable::kWaveformSize;
    for (size_t o = 12; o + 8 <= bytes;)
    {
        const size_t len = u32(o + 4), body = o + 8;
        if (body + len > bytes && std::memcmp(p + o, "data", 4) != 0) break;
        if (std::memcmp(p + o, "fmt ", 4) == 0 && len >= 16)
        {
            fmt = u16(body); channels = u16(body + 2); bits = u16(body + 14);
            if (fmt == 0xFFFE && len >= 26) fmt = u16(body + 24); // extensible: the real format is in the sub-format
        }
        else if (std::memcmp(p + o, "clm ", 4) == 0 && len >= 7 && std::memcmp(p + body, "<!>", 3) == 0)
        {
            const int c = atoi(std::string((const char *)p + body + 3, std::min<size_t>(len - 3, 8)).c_str());
            if (c >= 32 && c <= 65536) cycle = c;
        }
        else if (std::memcmp(p + o, "data", 4) == 0)
        {
            dataAt = body;
            dataLen = std::min(len, bytes - body);
        }
        o = body + len + (len & 1);
    }
    const unsigned bytesPer = bits / 8;
    if (!dataAt || channels < 1 || channels > 8 || !((fmt == 1 && (bits == 8 || bits == 16 || bits == 24 || bits == 32)) || (fmt == 3 && bits == 32))) return nullptr;
    const size_t total = dataLen / (bytesPer * channels);
    if (total < 16) return nullptr;
    std::vector<float> mono(total);
    for (size_t i = 0; i < total; ++i)
    { // the first channel
        const size_t o = dataAt + i * bytesPer * channels;
        float v = 0;
        if (fmt == 3) { std::memcpy(&v, p + o, 4); }
        else if (bits == 8) v = ((float)p[o] - 128.f) / 128.f;
        else if (bits == 16) v = (float)(int16_t)u16(o) / 32768.f;
        else if (bits == 24) v = (float)((int32_t)((u32(o) & 0xffffffu) << 8) >> 8) / 8388608.f;
        else v = (float)((double)(int32_t)u32(o) / 2147483648.0);
        mono[i] = std::isfinite(v) ? v : 0.f;
    }
    if (total < (size_t)cycle) cycle = (int)total; // a single cycle of some other length
    int frames = (int)(total / (size_t)cycle);
    const int keep = std::min(frames, (int)kNumOscillatorWaveFrames);
    const int N = Wavetable::kWaveformSize;
    json keyframes = json::array();
    std::vector<float> buf((size_t)N);
    for (int k = 0; k < keep; ++k)
    {
        const int f = keep == 1 ? 0 : (int)std::lround((double)k * (frames - 1) / (keep - 1));
        const float *src = mono.data() + (size_t)f * (size_t)cycle;
        for (int i = 0; i < N; ++i)
        { // a cycle that is not 2048 long is stretched onto 2048 (straight lines between its samples)
            const double x = (double)i * cycle / N;
            const int a = (int)x, b = (a + 1) % cycle;
            const float t = (float)(x - a);
            buf[(size_t)i] = src[a] * (1.f - t) + src[b] * t;
        }
        json kf;
        kf["position"] = keep == 1 ? 0 : (int)std::lround((double)k * (kNumOscillatorWaveFrames - 1) / (keep - 1));
        kf["wave_data"] = Base64::toBase64(buf.data(), sizeof(float) * (size_t)N).toStdString();
        keyframes.push_back(kf);
    }
    json comp;
    comp["type"] = "Wave Source";
    comp["interpolation"] = 0;
    comp["interpolation_style"] = 1; // glide between the cycles as FRAME moves
    comp["keyframes"] = keyframes;
    json group;
    group["components"] = json::array({comp});
    json root;
    root["groups"] = json::array({group});
    root["name"] = name ? name : "";
    root["author"] = "";
    root["full_normalize"] = true;
    root["remove_all_dc"] = true;
    root["version"] = "1.5.5";
    const std::string text = root.dump();
    char *out = (char *)malloc(text.size() + 1);
    if (!out) return nullptr;
    std::memcpy(out, text.c_str(), text.size() + 1);
    if (framesOut) *framesOut = keep;
    return out;
}
void shade_text_free(char *t) { free(t); }
ShadeOsc *shade_osc_create(ShadeWorld *w, int slot)
{
    if (!w || slot < 0 || slot >= SHADE_SLOTS) return nullptr;
    return new ShadeOsc(*w, slot);
}
void shade_osc_destroy(ShadeOsc *o) { delete o; }
void shade_osc_start(ShadeOsc *o, float key)
{
    if (o->osc.getSampleRate() != o->w.sampleRate)
    {
        o->osc.setSampleRate(o->w.sampleRate);
        o->note.setSampleRate(o->w.sampleRate);
    }
    o->note.set(key);
    o->note.process(1);
    o->osc.reset(constants::kFullMask);
}
void shade_osc_render(ShadeOsc *o, float key, const float *pm, const float *mv, float *out, int n)
{
    if (n > SHADE_MAX_BLOCK) n = SHADE_MAX_BLOCK;
    o->note.set(key);
    o->note.process(1);
    {
        auto &s = o->slot;
        const float top = (float)(std::max(1, s.table->numFrames()) - 1);
        auto c01 = [](float v) { return std::max(0.f, std::min(1.f, v)); };
        o->frame.set(c01(s.p[1] + (mv ? mv[0] : 0.f)) * top);
        o->morphAmount.set(c01(s.p[3] + (mv ? mv[1] : 0.f)));
        o->warpAmount.set(c01(s.p[5] + (mv ? mv[2] : 0.f)));
        o->frame.process(1);
        o->morphAmount.process(1);
        o->warpAmount.process(1);
    }
    if (pm)
        for (int i = 0; i < n; ++i) o->phase.buffer[i] = 0.5f + pm[i];
    else
        for (int i = 0; i < n; ++i) o->phase.buffer[i] = 0.5f;
    o->osc.process(n);
    const poly_float *b = o->osc.output(SynthOscillator::kRaw)->buffer;
    for (int i = 0; i < n; ++i)
    { // a guard, far above anything the oscillator normally makes (its peaks stay under about 3): never pass on a runaway
        const float y = b[i][0];
        out[i] = std::isfinite(y) ? std::max(-6.f, std::min(6.f, y)) : 0.f;
    }
}
}
