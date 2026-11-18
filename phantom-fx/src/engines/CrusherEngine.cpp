// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX: CrushClone (doxiedevops/CrushClone @ aaa5a0c, MIT; its DSP vendored unchanged in third_party/crushclone) as the CRUSHER engine
// of SOUL CRUSH, its eleven presets as voices: the tube / mech distortion (4x oversampled), the resonant filter and the compressor, in
// its own order. CRUSH scales the preset's distortion and compression (our default = the preset's), DARKNESS moves its filter down,
// PARALLEL BLEND mixes it with the dry (aligned to its latency), OUTPUT adds to its master level. CRUSH 0 .. 10 % fades it in from the
// clean signal, so CRUSH 0 is the dry signal.
#include "Engines.h"
#include "AltEngine.h"
#include "DSP/Distortion.h"
#include "DSP/LadderFilter.h"
#include "DSP/Compressor.h"

namespace phantom::engines
{
namespace
{
    // CrushCloneProcessor::initPresets() (PluginProcessor.cpp), without its silent "Default":
    // tube, mech, cutoff, reso, comp, phat, dist on, filter on, comp on, master dB, mix
    struct Preset { const char* name; float tube, mech, cutoff, reso, comp; bool phat, dOn, fOn, cOn; float master, mix; };
    const Preset kPresets[] = {
        { "DRM BigBadBeat", 0.5f, 0.3f, 4000.f, 0.2f, 0.6f, true, true, true, true, 0.0f, 1.0f },
        { "BAS FatSub", 0.7f, 0.0f, 800.f, 0.3f, 0.5f, true, true, true, true, 0.0f, 1.0f },
        { "GTR Crunch", 0.4f, 0.5f, 6000.f, 0.1f, 0.3f, false, true, true, true, 0.0f, 1.0f },
        { "SYN WarmPad", 0.3f, 0.0f, 3000.f, 0.4f, 0.2f, false, true, true, true, 0.0f, 1.0f },
        { "VOX Gritty", 0.6f, 0.2f, 5000.f, 0.15f, 0.4f, false, true, true, true, 0.0f, 1.0f },
        { "MAS Loudener", 0.2f, 0.1f, 12000.f, 0.0f, 0.7f, true, true, true, true, 2.0f, 1.0f },
        { "DRM LoFiHit", 0.8f, 0.6f, 2000.f, 0.5f, 0.4f, false, true, true, true, -2.0f, 1.0f },
        { "SYN Screamer", 0.3f, 0.8f, 7000.f, 0.6f, 0.5f, true, true, true, true, -1.0f, 1.0f },
        { "Gentle Warmth", 0.2f, 0.0f, 15000.f, 0.0f, 0.1f, false, true, true, true, 0.0f, 0.5f },
        { "Telephone", 0.1f, 0.0f, 3500.f, 0.3f, 0.0f, false, true, true, false, 0.0f, 1.0f },
        { "Destroy Everything", 1.0f, 1.0f, 1000.f, 0.7f, 0.8f, true, true, true, true, -4.0f, 1.0f },
    };
    constexpr int kCount = (int) (sizeof (kPresets) / sizeof (kPresets[0]));

    class CrusherEngine final : public AltEngine
    {
    public:
        explicit CrusherEngine (const std::vector<ParamSpec>& specs) { for (auto& s : specs) defs[s.id] = s.def; }
        void prepare (double sampleRate, int maxBlock) override
        {
            sr = sampleRate; mb = juce::jmax (maxBlock, 64);
            dist.prepare (sampleRate, mb, 2); filter.prepare (sampleRate, mb, 2); comp.prepare (sampleRate, mb, 2);
            buf.setSize (2, mb); dry.setSize (2, mb);
            // its distortion's oversampler (DSP/Oversampler.h: JUCE's 4x half-band polyphase IIR, max quality) delays the signal; the dry is
            // delayed to match (whole samples) and the latency reported
            juce::dsp::Oversampling<float> probe { 2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true }; probe.initProcessing ((size_t) mb);
            lat = (int) std::lround (probe.getLatencyInSamples());
            for (auto& d : dly) d.assign ((size_t) juce::jmax (1, lat), 0.0f);   // read before write: a delay of exactly lat samples
            reset();
        }
        void reset() noexcept override { dist.reset(); filter.reset(); comp.reset(); for (auto& d : dly) std::fill (d.begin(), d.end(), 0.0f); dp = 0; primed = false; }
        int latencySamples (int) const noexcept override { return lat; }
        bool appliesLevel() const noexcept override { return true; }
        void setLevel (float g) noexcept override { level = g; }
        void process (float* L, float* R, int n, const Controls& c, int voice, const TempoInfo&) noexcept override
        {
            const auto& p = kPresets[juce::jlimit (0, kCount - 1, voice)];
            const float crushT = c.get ("crush", 0.5f), darkT = c.get ("dark", 0.3f);
            const float blendT = juce::jlimit (0.0f, 1.0f, c.get ("blend", 1.0f) * p.mix), fadeT = juce::jlimit (0.0f, 1.0f, crushT / 0.1f);
            const float gainT = juce::Decibels::decibelsToGain (p.master + c.get ("out", 0.0f)) * level;
            if (! primed) { blend = blendT; fade = fadeT; gain = gainT; smCrush = crushT; smDark = darkT; primed = true; }
            // its modules take their settings per block: here every 16 samples, from controls that glide (80 ms), so a sweep never steps
            const float a = 1.0f - std::exp (-16.0f / (0.08f * (float) sr));
            for (int i0 = 0; i0 < n; i0 += kChunk)
            {
                const int m = juce::jmin (kChunk, n - i0); float* l = L + i0; float* r = R + i0;
                smCrush += a * (crushT - smCrush); smDark += a * (darkT - smDark);
                const float k = juce::jlimit (0.0f, 2.0f, smCrush / juce::jmax (0.01f, def ("crush", 0.5f)));
                const float cutoff = juce::jlimit (200.0f, 20000.0f, p.cutoff * std::pow (2.0f, -(smDark - def ("dark", 0.3f)) * 5.0f));
                dist.setTubeAmount (juce::jlimit (0.0f, 1.0f, p.tube * k)); dist.setMechAmount (juce::jlimit (0.0f, 1.0f, p.mech * k)); dist.setEnabled (p.dOn);
                filter.setCutoff (cutoff); filter.setResonance (p.reso); filter.setEnabled (p.fOn);
                comp.setAmount (juce::jlimit (0.0f, 1.0f, p.comp * k)); comp.setPhatMode (p.phat); comp.setEnabled (p.cOn);
                buf.copyFrom (0, 0, l, m); buf.copyFrom (1, 0, r, m);
                juce::AudioBuffer<float> b (buf.getArrayOfWritePointers(), 2, m);
                dist.process (b); filter.process (b); comp.process (b);   // CrushCloneProcessor::processBlock's chain
                const float sb = (blendT - blend) / (float) n, sf = (fadeT - fade) / (float) n, sg = (gainT - gain) / (float) n;
                for (int i = 0; i < m; ++i)
                {
                    blend += sb; fade += sf; gain += sg;
                    float dl = l[i], dr = r[i]; if (lat > 0) { dl = dly[0][(size_t) dp]; dr = dly[1][(size_t) dp]; dly[0][(size_t) dp] = l[i]; dly[1][(size_t) dp] = r[i]; dp = dp + 1 >= lat ? 0 : dp + 1; }
                    float wl = buf.getSample (0, i), wr = buf.getSample (1, i);
                    if (! std::isfinite (wl)) wl = 0.0f; if (! std::isfinite (wr)) wr = 0.0f;
                    wl = juce::jlimit (-2.0f, 2.0f, wl) * gain; wr = juce::jlimit (-2.0f, 2.0f, wr) * gain;   // its safety limiter, then the levels
                    const float w = blend * fade; l[i] = dl + (wl - dl) * w; r[i] = dr + (wr - dr) * w;
                }
            }
        }
    private:
        float def (const char* id, float fb) const { auto it = defs.find (id); return it == defs.end() ? fb : it->second; }
        static constexpr int kChunk = 16; std::map<std::string, float> defs; DistortionModule dist; LadderFilter filter; CompressorModule comp;
        juce::AudioBuffer<float> buf, dry; std::array<std::vector<float>, 2> dly; int dp = 0, lat = 0, mb = 512; double sr = 48000.0;
        bool primed = false; float blend = 1.0f, fade = 1.0f, gain = 1.0f, level = 1.0f, smCrush = -1.0f, smDark = -1.0f;
    };
} // namespace

int crusherVoices() { return kCount; }
std::string crusherVoiceName (int i) { return kPresets[juce::jlimit (0, kCount - 1, i)].name; }
std::unique_ptr<AltEngine> makeCrusherEngine (const std::vector<ParamSpec>& specs) { return std::make_unique<CrusherEngine> (specs); }
} // namespace phantom::engines
