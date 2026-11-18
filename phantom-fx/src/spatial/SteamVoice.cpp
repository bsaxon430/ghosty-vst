// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX SPATIAL: Steam Audio glue (see SteamVoice.h). Only Steam Audio's own classes touch the sound here; this file creates
// them, feeds them a direction and a distance, and sums their outputs.
#include "SteamVoice.h"
#include "binaural_effect.h"
#include "panning_effect.h"
#include "eq_effect.h"
#include "gain_effect.h"
#include "distance_attenuation.h"
#include "air_absorption.h"
#include "speaker_layout.h"
#include <cmath>
#include <vector>

namespace phantom::spatial
{
using namespace ipl;

struct Source
{
    std::unique_ptr<EQEffect> eq; std::unique_ptr<GainEffect> gain; std::unique_ptr<BinauralEffect> binaural; std::unique_ptr<PanningEffect> pan;
};
struct Voice { Source s[2]; };

struct Renderer::Impl
{
    int fs = 48000, frame = 128;
    std::unique_ptr<HRTFDatabase> hrtf;
    SpeakerLayout stereo { SpeakerLayoutType::Stereo };
    std::vector<Voice> voices;
    DistanceAttenuationModel distanceModel;   // Steam Audio's default: 1 / max(d, 1 m)
    AirAbsorptionModel airModel;              // Steam Audio's default 3-band coefficients
    std::unique_ptr<AudioBuffer> mono, shaped, bin, mid, panned;
    float cosS = 1, sinS = 0;
};

Renderer::Renderer() = default;
Renderer::~Renderer() = default;
bool Renderer::ready() const noexcept { return impl != nullptr && impl->hrtf != nullptr; }

void Renderer::prepare (int sampleRate, int frameSize, int numVoices)
{
    impl.reset();
    try { build (sampleRate, frameSize, numVoices); }
    catch (...) { impl.reset(); }   // Steam Audio's built-in HRTF has no data for this sample rate (it has 44.1 and 48 kHz): SPATIAL stays unavailable
}

void Renderer::build (int sampleRate, int frameSize, int numVoices)
{
    auto m = std::make_unique<Impl>();
    m->fs = sampleRate; m->frame = frameSize;
    AudioSettings as; as.samplingRate = sampleRate; as.frameSize = frameSize;
    HRTFSettings hs;                                               // the built-in default set (CIPIC subject 124)
    m->hrtf = std::make_unique<HRTFDatabase> (hs, sampleRate, frameSize);
    BinauralEffectSettings bs; bs.hrtf = m->hrtf.get();
    PanningEffectSettings ps; ps.speakerLayout = &m->stereo;
    m->voices.resize ((size_t) numVoices);
    for (auto& v : m->voices)
        for (auto& s : v.s)
        {
            s.eq = std::make_unique<EQEffect> (as); s.gain = std::make_unique<GainEffect> (as);
            s.binaural = std::make_unique<BinauralEffect> (as, bs); s.pan = std::make_unique<PanningEffect> (ps);
        }
    m->mono = std::make_unique<AudioBuffer> (1, frameSize); m->shaped = std::make_unique<AudioBuffer> (1, frameSize);
    m->bin = std::make_unique<AudioBuffer> (2, frameSize); m->mid = std::make_unique<AudioBuffer> (1, frameSize); m->panned = std::make_unique<AudioBuffer> (2, frameSize);
    const float a = kSpreadDegrees * 3.14159265358979f / 180.0f; m->cosS = std::cos (a); m->sinS = std::sin (a);
    impl = std::move (m);
}

void Renderer::resetVoice (int v) noexcept
{
    if (! impl || v < 0 || v >= (int) impl->voices.size()) return;
    for (auto& s : impl->voices[(size_t) v].s) { s.eq->reset(); s.gain->reset(); s.binaural->reset(); s.pan->reset(); }
}

void Renderer::render (int v, const float* inL, const float* inR, float x, float y, float z, float distM, bool speakers, float* outL, float* outR) noexcept
{
    if (! impl || v < 0 || v >= (int) impl->voices.size()) return;
    auto& m = *impl; const int F = m.frame;
    // distance: as DirectEffect::calculateGainAndEQ does it (attenuation x air absorption, EQ normalised by EQEffect::normalizeGains)
    float gain = m.distanceModel.evaluate (distM);
    float eq[Bands::kNumBands]; for (int b = 0; b < Bands::kNumBands; ++b) eq[b] = m.airModel.evaluate (distM, b);
    EQEffect::normalizeGains (eq, gain);
    gain *= 0.70710678f;   // two sources (the wet's left and right) each reach both ears: -3 dB each keeps the wet's power where it was
    for (int side = 0; side < 2; ++side)
    {
        auto& s = m.voices[(size_t) v].s[side];
        // the two sources sit kSpreadDegrees either side of the direction (a turn about the vertical axis)
        const float sn = side == 0 ? -m.sinS : m.sinS;
        Vector3f dir (x * m.cosS - z * sn, y, z * m.cosS + x * sn);
        const float len = std::sqrt (dir.x() * dir.x() + dir.y() * dir.y() + dir.z() * dir.z());
        if (len < 1.0e-6f) dir = Vector3f (0.0f, 0.0f, -1.0f); else dir = Vector3f (dir.x() / len, dir.y() / len, dir.z() / len);
        const float* src = side == 0 ? inL : inR;
        for (int i = 0; i < F; ++i) (*m.mono)[0][i] = src[i];
        EQEffectParams ep; ep.gains = eq; s.eq->apply (ep, *m.mono, *m.shaped);
        GainEffectParams gp; gp.gain = gain; s.gain->apply (gp, *m.shaped, *m.shaped);
        BinauralEffectParams bp; bp.direction = &dir; bp.hrtf = m.hrtf.get(); bp.interpolation = HRTFInterpolation::Bilinear;
        s.binaural->apply (bp, *m.shaped, *m.bin);
        if (! speakers) { for (int i = 0; i < F; ++i) { outL[i] += (*m.bin)[0][i]; outR[i] += (*m.bin)[1][i]; } continue; }
        // speakers: the binaural render's mid keeps the HRTF's spectral (above / below) colour; Steam Audio's stereo panner places it
        for (int i = 0; i < F; ++i) (*m.mid)[0][i] = 0.5f * ((*m.bin)[0][i] + (*m.bin)[1][i]);
        PanningEffectParams pp; pp.direction = &dir; s.pan->apply (pp, *m.mid, *m.panned);
        for (int i = 0; i < F; ++i) { outL[i] += (*m.panned)[0][i]; outR[i] += (*m.panned)[1][i]; }
    }
}
} // namespace phantom::spatial
