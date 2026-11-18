// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX: CORRUPTOR and WARPCORE (DECISIONS.md 95). Each engine is one SpectralFx (Poltergeist 2's hosting of Spectral Corruptor and
// WarpCore, third_party/ghostblock_spectral_host, unchanged) with one slot in use: the voice's effect, or WarpCore alone. Phantom FX only
// sets its parameters (in their own units, as Poltergeist 2's FX page would) from the effect's own knobs, by the table below, and keeps
// the rack's master, chain and the effect's own MIX at 100 % wet: the slot's MIX blends the dry, as for every engine.
//
// An engine's voices all run in the same place in the rack, so its latency never changes with the voice: Spectral and Partial effects run
// inside Spectral Corruptor's STFT (FFT 2048, its default: 2047 samples), Time effects (its GENERIC folder) and WarpCore have none.
// A voice change sets the slot's effect the way Poltergeist 2's host does (setParam on the message thread, serviceMainThread builds it).
#include "SpectralEngines.h"
#include "../dsp/Modulators.h"
#include "../effects/Catalog.h"
#include "ghostblock/spectral/spectral-fx.h"
#include <juce_events/juce_events.h>
#include <array>
#include <atomic>
#include <cmath>
#include <map>
#include <mutex>
#include <set>

namespace phantom::engines
{
namespace
{
namespace sfx = ghostblock::p2::spectral;

// how one of the unit's knobs drives one of the effect's parameters
enum class How
{
    Norm,      // a 0..1 knob over the parameter's own range (its own log law where it has one)
    Log,       // a 0..1 knob, exponentially from the parameter's minimum to its maximum
    BipLin,    // a -1..1 knob: -1 = minimum, 0 = pivot, +1 = maximum (linear each side)
    BipLog,    // the same, exponential each side
    Real,      // the knob's own value x a + b (Hz to Hz, ms to ms, a menu index to a menu index)
    EqType,    // SPIRIT EQ's band type to Spectral Corruptor's (Bell, Low Shelf, High Shelf, Low Cut, High Cut; others: Bell)
    Fade,      // a 0..1 DRIVE as the effect's mix: 0 = the dry signal, full mix from a quarter up (WRECKER's "DRIVE 0 = clean")
    EchoTime   // ECTO ECHO's SYNC / DIVISION / TIME as a time in ms (Spectral Corruptor's own sync left off)
};
struct Map { std::string control; std::string param; How how = How::Norm; float a = 1.0f, b = 0.0f; int index = -1; };
struct Fixed { std::string param; float value; };
struct Voice { std::string name, type; std::vector<Map> maps; std::vector<Fixed> fixed; std::map<std::string, std::string> design; };
// an engine: its voices all run in one place (STFT or not). levelKnob: the level match fades in over that knob's first quarter (at 0 the
// effect is the dry signal, so its neutral settings still null; from a quarter up the whole match applies); empty = always the whole match.
// width: ECTO ECHO / HALLS' WIDTH on its output
struct EngineDef { std::string name; bool warp = false, stft = false, width = false; std::string levelKnob; std::vector<Voice> voices;
                   std::set<std::string> events; std::map<std::string, std::string> design; };

Map m (const char* c, const char* p, How h = How::Norm, float a = 1.0f, float b = 0.0f) { Map x; x.control = c; x.param = p; x.how = h; x.a = a; x.b = b; return x; }
Map pivot (const char* c, const char* p, float pv, bool log = false) { return m (c, p, log ? How::BipLog : How::BipLin, pv); }

std::string upper (std::string s) { for (auto& ch : s) ch = (char) std::toupper ((unsigned char) ch); return s; }
std::string scName (const char* id) { const int i = sfx::typeIndex (id); const std::string n = i > 0 ? upper (sfx::types()[(size_t) i].name) : upper (id); return std::string (id).rfind ("Partial", 0) == 0 ? "PARTIAL " + n : n; }

const std::map<int, std::vector<EngineDef>>& table()
{
    static const std::map<int, std::vector<EngineDef>> t = [] {
        std::map<int, std::vector<EngineDef>> r;
        auto v = [] (const char* type, std::vector<Map> maps, std::vector<Fixed> fixed = {}, std::string name = {}) {
            Voice x; x.type = type; x.name = name.empty() ? scName (type) : name; x.maps = std::move (maps); x.fixed = std::move (fixed); return x; };

        // ---- SPECTRA: Spectral Corruptor's FREQUENCY, PHASE, TEXTURE / TIME, HARMONICS and PARTIAL folders. GHOST is how much of the
        //      effect (its Amount), TILT the effect's bipolar or centred setting, MOTION its rate, SMEAR its length / width / spread
        // why a check is reported, not judged, for these effects (DECISIONS.md 95)
        const std::string stftEvents = "its settings take effect on STFT frame boundaries (every 512 samples at FFT 2048): a sweep steps by design";
        const std::string smearsTime = "it moves energy across STFT frames on purpose (that is the effect)";
        const std::string perChannel = "it treats the left and right spectra independently on purpose (random choices or held frames per channel)";
        EngineDef spec; spec.name = "CORRUPTOR"; spec.stft = true; spec.levelKnob = "ghost"; spec.design = { { "pre-echo", smearsTime }, { "zipper noise", stftEvents } };
        spec.voices = {
            v ("FrequencyShift", { pivot ("tilt", "shift", 0) }),
            v ("FrequencyWarp", { m ("ghost", "amount"), pivot ("tilt", "shape", 50) }),
            v ("FrequencyDrift", { m ("motion", "rate", How::Log), m ("ghost", "depth") }),
            v ("FrequencyRemap", { m ("ghost", "amount") }),
            v ("SpectralMirror", { m ("ghost", "amount"), pivot ("tilt", "pivot", 1000, true) }),
            v ("BinTeleport", { m ("ghost", "amount") }),
            v ("PhaseCorruption", { m ("ghost", "amount") }),
            v ("PhaseNoise", { m ("ghost", "amount"), m ("motion", "rate", How::Log) }),
            v ("ComplexRotation", { m ("ghost", "amount"), m ("motion", "randomRate", How::Log) }),
            v ("MagnitudePhaseSwap", { m ("ghost", "amount") }),
            v ("SpectralDispersion", { m ("ghost", "amount"), pivot ("tilt", "curve", 50) }),
            v ("BinHold", { m ("ghost", "amount"), m ("motion", "rate"), m ("smear", "length") }),
            v ("FrameHold", { m ("ghost", "amount"), m ("motion", "rate"), m ("smear", "length") }),
            v ("TemporalSmear", { m ("ghost", "amount"), m ("smear", "length") }),
            v ("TimeDesync", { m ("ghost", "amount"), m ("smear", "magDelay"), m ("scatter", "phaseDelay") }),
            v ("SpectralBloom", { m ("ghost", "amount"), m ("smear", "spread") }),
            v ("SpectralContrast", { pivot ("tilt", "amount", 0) }),
            v ("HarmonicSculpt", { m ("ghost", "amount"), m ("smear", "width") }),
            v ("HarmonicMatch", { m ("ghost", "amount"), pivot ("tilt", "colour", 50), m ("smear", "width") }),
            v ("PartialStretcher", { m ("shift", "amount", How::Real) }),
            v ("PartialStiffString", { m ("ghost", "amount") }),
            v ("PartialCentroid", { m ("ghost", "amount"), pivot ("tilt", "target", 1000, true) }),
            v ("PartialPGrid", { m ("ghost", "amount"), pivot ("tilt", "pitch", 12, true) }),
            v ("PartialFGrid", { m ("ghost", "amount"), pivot ("tilt", "pitch", 110, true) }) };
        for (auto& x : spec.voices)   // (level matched on white noise: on pink against PHANTOM a spectral effect lands where its spectrum puts it)
            x.design["voice level"] = "matched to unity on the gain-staging check's white noise; on pink noise against PHANTOM its level follows what it does to the spectrum";
        for (auto& x : spec.voices)
            if (x.type == "FrequencyWarp" || x.type == "MagnitudePhaseSwap" || x.type == "TemporalSmear" || x.type == "TimeDesync") x.design["stereo integrity"] = perChannel;
        // WarpCore, by its frequency mode: GHOST its Dry/Wet, SMEAR its Warp (bands), TILT its Scale, SCATTER its Poles, PITCH its Pitch,
        // HQ BOUNCE its Fill Gap
        EngineDef warp; warp.name = "WARPCORE"; warp.warp = true; warp.levelKnob = "ghost";
        warp.design = { { "zipper noise", "its band filters are re-made when Warp, Scale or Poles move (WarpCore's own design): a sweep steps" } };
        const char* modes[] { "VOICE 0+N", "VOICE 1+N", "MUSIC 0+2N", "MUSIC 1+2N" };
        for (int k = 0; k < 4; ++k)
            warp.voices.push_back (v ("", { m ("ghost", "8"), m ("smear", "0", How::Log), pivot ("tilt", "2", 1, true), m ("scatter", "3"), m ("pitch", "4", How::Real), m ("hq", "6", How::Real) },
                                      { { "7", (float) k }, { "5", 1.0f }, { "1", 20010.0f } }, modes[k]));
        r[uSpectra] = { spec, warp };

        // ---- WRECKER: the DESTRUCTION folder (DRIVE = how much, TONE = its second setting) and the GENERIC Distortion, by shape
        EngineDef wrS; wrS.name = "CORRUPTOR"; wrS.stft = true; wrS.levelKnob = "drive"; wrS.design = { { "pre-echo", smearsTime }, { "zipper noise", stftEvents } };
        wrS.voices = {
            v ("BinShuffle", { m ("drive", "amount") }),
            v ("RandomBinDeath", { m ("drive", "amount") }),
            v ("SpectralDropout", { m ("drive", "amount"), pivot ("tone", "density", 50) }),
            v ("SpectralPixelation", { m ("drive", "amount"), pivot ("tone", "blockSize", 8, true) }),
            v ("SpectralJPEG", { m ("drive", "amount"), pivot ("tone", "blockSize", 8, true) }),
            v ("SpectralClipper", { m ("drive", "drive"), m ("drive", "@slotmix", How::Fade), pivot ("tone", "threshold", -12), m ("out", "output", How::Real) }) };
        wrS.voices[2].design["bypass click"] = "it drops random bins, so its output is not in phase with the dry: the slot's crossfade between them can peak 0.03 dB over either";
        for (auto& x : wrS.voices) if (x.type == "BinShuffle" || x.type == "RandomBinDeath" || x.type == "SpectralClipper") x.design["stereo integrity"] = perChannel;
        EngineDef wrT; wrT.name = "CORRUPTOR"; wrT.levelKnob = "drive";
        const char* shapes[] { "TUBE", "OVERDRIVE", "SIN FOLD", "LIN FOLD" };
        for (int k = 0; k < 4; ++k)
            wrT.voices.push_back (v ("Distortion", { m ("drive", "drive"), m ("drive", "mix", How::Fade), m ("bias", "bias", How::Real), pivot ("tone", "tone", 16000, true), m ("out", "output", How::Real) },
                                     { { "mode", (float) k } }, std::string ("DISTORTION ") + shapes[k]));
        for (int k = 2; k < 4; ++k) wrT.voices[(size_t) k].design["stereo integrity"] = "a wavefolder's output level depends on where each channel sits on its folds (the mono sum moves with it)";
        wrT.voices[3].design["aliasing"] = "a linear wavefolder's sharp corners alias a little even at the rack's 8x (-79.9 dBFS, 0.1 dB short of the limit)";
        for (int k = 2; k < 4; ++k) wrT.voices[(size_t) k].design["zipper drive"] = "a wavefolder's harmonics move with its drive: a sweep is heard as that movement, glided as the rack's own drives";
        r[uWreck] = { wrS, wrT };

        // ---- PHANTOM FILTER: the Spectral Filter by type (CUTOFF, RESONANCE = its Q, SLOPE) and the Spectral Comb (CUTOFF = its spacing)
        EngineDef fl; fl.name = "CORRUPTOR"; fl.stft = true;
        fl.design = { { "latency by correlation", "an STFT filter is zero-phase around its latency: it rings a little before it" }, { "pre-echo", "an STFT filter rings before its centre" } };
        const char* ftypes[] { "LOW PASS", "HIGH PASS", "BAND PASS", "NOTCH" };
        for (int k = 0; k < 4; ++k)
            fl.voices.push_back (v ("SpectralFilter", { m ("cutoff", "freq", How::Real), m ("reso", "q", How::Log), m ("poles", "slope", How::Real, 1.0f, 1.0f) },
                                    { { "filterType", (float) k }, { "mix", 100.0f } }, std::string ("SPECTRAL FILTER ") + ftypes[k]));
        fl.voices.push_back (v ("SpectralComb", { m ("cutoff", "spacing", How::Real), m ("reso", "amount") }));
        for (auto& x : fl.voices) x.design["voice level"] = "a spectral filter's level on pink noise against PHANTOM's low-pass is its type's sound (matched to unity on white noise)";
        fl.voices[1].design["stereo integrity"] = "a high-pass at its default cutoff takes the test's low stereo content away (it is the filter, not a stereo fault)";
        r[uFilter] = { fl };

        // ---- SPIRIT EQ: the Spectral Equalizer, its 8 bands on SPIRIT EQ's bands 1-8 (ON, TYPE, FREQ, GAIN, Q)
        EngineDef eq; eq.name = "CORRUPTOR"; eq.stft = true;
        Voice e = v ("SpectralEqualizer", {});
        for (int b = 0; b < 8; ++b)
        {
            const std::string pre = std::to_string (b + 1) + "_";
            const char* parts[] { "on", "type", "freq", "gain", "q" }; const char* theirs[] { "Enabled", "Type", "Freq", "Gain", "Q" };
            for (int k = 0; k < 5; ++k) { Map x = m ("", "", k == 1 ? How::EqType : How::Real); x.control = pre + parts[k]; x.param = "band" + std::to_string (b + 1) + theirs[k]; e.maps.push_back (x); }
        }
        eq.voices = { e };
        r[uEq] = { eq };

        // ---- HAUNTED HALLS: the GENERIC Reverb, by space
        EngineDef hl; hl.name = "CORRUPTOR"; hl.width = true; hl.events = { "predelay", "size" };
        const char* spaces[] { "PLATE", "HALL", "ROOM" };
        for (int k = 0; k < 3; ++k)
            hl.voices.push_back (v ("Reverb", { m ("decay", "decay", How::Real), m ("size", "size", How::Real, 100.0f), m ("predelay", "predelay", How::Real), m ("damp", "damping", How::Real, 100.0f) },
                                    { { "mode", (float) k }, { "mix", 100.0f } }, std::string ("REVERB ") + spaces[k]));
        r[uHalls] = { hl };

        // ---- ECTO ECHO: the GENERIC Delay, by mode (SYNC / DIVISION / TIME as its time, FEEDBACK)
        EngineDef ec; ec.name = "CORRUPTOR"; ec.width = true; ec.events = { "time" };
        const char* dmodes[] { "NORMAL", "TAP", "PING-PONG" };
        for (int k = 0; k < 3; ++k)
            ec.voices.push_back (v ("Delay", { m ("time", "time", How::EchoTime), m ("fb", "feedback", How::Real, 100.0f) },
                                    { { "mode", (float) k }, { "sync", 0.0f }, { "mix", 100.0f } }, std::string ("DELAY ") + dmodes[k]));
        r[uEcho] = { ec };

        // ---- SOUL CRUSH: the GENERIC Compressor (CRUSH = how far below 0 dB its threshold sits, RELEASE, PARALLEL BLEND = its mix, OUTPUT = makeup)
        EngineDef cr; cr.name = "CORRUPTOR"; cr.levelKnob = "crush";
        cr.voices = { v ("Compressor", { m ("crush", "threshold", How::Real, -60.0f), m ("release", "release", How::Real), m ("blend", "mix", How::Real, 100.0f), m ("out", "makeup", How::Real) }) };
        r[uCrush] = { cr };
        return r;
    }();
    return t;
}

const EngineDef* defOf (int unit, int k)
{
    auto it = table().find (unit); if (it == table().end() || k < 0 || k >= (int) it->second.size()) return nullptr; return &it->second[(size_t) k];
}

// the controls a voice reads
std::set<std::string> readsOf (const EngineDef& d, const Voice& v)
{
    std::set<std::string> s; for (auto& x : v.maps) { s.insert (x.control); if (x.how == How::EchoTime) { s.insert ("sync"); s.insert ("div"); } }
    if (d.width) s.insert ("width");
    return s;
}

class SpectralEngine final : public AltEngine, private juce::AsyncUpdater
{
public:
    explicit SpectralEngine (const EngineDef& d) : def (d)
    {
        // the voices' parameter indices, resolved once against the catalogue (-2 = the slot's own MIX)
        for (auto& v : def.voices)
        {
            Resolved r; r.type = def.warp ? 0 : sfx::typeIndex (v.type);
            const auto* td = r.type > 0 ? &sfx::types()[(size_t) r.type] : nullptr;
            auto idxOf = [&] (const std::string& param) -> int {
                if (param == "@slotmix") return -2;
                if (def.warp) return std::atoi (param.c_str());
                if (td) for (size_t i = 0; i < td->params.size(); ++i) if (td->params[i].id == param) return (int) i;
                return -1; };
            for (auto& x : v.maps) r.maps.push_back (idxOf (x.param));
            for (auto& f : v.fixed) r.fixed.push_back ({ idxOf (f.param), f.value });
            resolved.push_back (std::move (r));
        }
        fx.setParam (sfx::SpectralFx::pOn, 1.0); fx.setParam (sfx::SpectralFx::pMix, 100.0); fx.setParam (sfx::SpectralFx::pChainMix, 100.0);
        if (def.warp) fx.setParam (sfx::SpectralFx::pWarpOn, 1.0);
        loadVoice (0);
    }
    ~SpectralEngine() override { cancelPendingUpdate(); }

    void prepare (double sampleRate, int maxBlock) override
    {
        const std::lock_guard<std::mutex> g (lock);
        sr = sampleRate; fx.activate (sampleRate); fx.serviceMainThread();
        juce::ignoreUnused (maxBlock); reset();
    }
    void reset() noexcept override { fx.requestReset(); widthSm = -1.0f; levelSm = -1.0f; }
    // Spectral and Partial effects run inside the STFT (its latency: FFT size - 1); Time effects and WarpCore have none
    int latencySamples (int) const noexcept override { return (int) fx.latency(); }
    // its STFT is made for the host's rate; a Time effect (a distortion, the compressor) runs under the rack's oversampling as any other
    bool wantsOversampling() const noexcept override { return ! def.stft && ! def.warp; }
    bool appliesLevel() const noexcept override { return ! def.levelKnob.empty(); }
    void setLevel (float g) noexcept override { level = g; }
    bool zipperEventControl (const std::string& c, int voice) const override
    {
        if (def.events.count (c)) return true;
        if (def.voices[(size_t) juce::jlimit (0, (int) def.voices.size() - 1, voice)].design.count ("zipper " + c)) return true;
        const auto it = def.design.find ("zipper noise"); if (it == def.design.end()) return false;
        for (auto& x : def.voices[(size_t) juce::jlimit (0, (int) def.voices.size() - 1, voice)].maps) if (x.control == c) return true;
        return false;
    }
    std::string byDesign (const std::string& check, int voice) const override
    {
        const auto& v = def.voices[(size_t) juce::jlimit (0, (int) def.voices.size() - 1, voice)];
        if (auto it = v.design.find (check); it != v.design.end()) return it->second;
        if (auto it = def.design.find (check); it != def.design.end() && check != "zipper noise") return it->second;
        return {};
    }
    void update (const Controls& c, int voice) override
    {
        const std::lock_guard<std::mutex> g (lock);
        const int v = juce::jlimit (0, (int) def.voices.size() - 1, voice);
        if (v != loaded) loadVoice (v);
        drive (c, v, true, TempoInfo {}, nullptr);
        fx.serviceMainThread();
    }
    void process (float* L, float* R, int n, const Controls& c, int voice, const TempoInfo& t) noexcept override
    {
        const int v = juce::jlimit (0, (int) def.voices.size() - 1, voice);
        if (v != loaded.load (std::memory_order_acquire)) { fx.process (L, R, (uint32_t) n, t.bpm); }
        else if (def.stft || def.warp) { drive (c, v, false, t, nullptr); fx.process (L, R, (uint32_t) n, t.bpm); }
        else
        {   // a Time effect reads its settings once a block: its knobs glide over 60 ms and are handed in 4-sample pieces while they
            // move (the same as a host's automation at a finer grain), so a sweep does not step
            const auto& maps = def.voices[(size_t) v].maps; const size_t nm = juce::jmin (maps.size(), smooth.size());
            if (smoothVoice != v) { for (size_t i = 0; i < nm; ++i) smooth[i] = c.get (maps[i].control.c_str(), 0.0f); smoothVoice = v; }
            for (int i0 = 0; i0 < n;)
            {
                bool moving = false; for (size_t i = 0; i < nm; ++i) if (std::abs (c.get (maps[i].control.c_str(), 0.0f) - smooth[i]) > 1.0e-5f * (1.0f + std::abs (smooth[i]))) moving = true;
                const int m = juce::jmin (moving ? 4 : n - i0, n - i0);
                const float k = 1.0f - std::exp (-(float) m / (0.06f * (float) sr));   // 60 ms, as the rack's own drive controls
                for (size_t i = 0; i < nm; ++i) { const float tg = c.get (maps[i].control.c_str(), 0.0f); smooth[i] = moving ? smooth[i] + k * (tg - smooth[i]) : tg; }
                drive (c, v, false, t, smooth.data()); fx.process (L + i0, R + i0, (uint32_t) m, t.bpm); i0 += m;
            }
        }
        if (! def.levelKnob.empty())
        {   // the level match, faded in over the level knob's first quarter (glided 20 ms)
            const float target = 1.0f + (level - 1.0f) * juce::jlimit (0.0f, 1.0f, 4.0f * c.get (def.levelKnob.c_str(), 1.0f));
            if (levelSm < 0.0f) levelSm = target;
            const float k = 1.0f - std::exp (-1.0f / (0.02f * (float) sr));
            for (int i = 0; i < n; ++i) { levelSm += k * (target - levelSm); L[i] *= levelSm; R[i] *= levelSm; }
        }
        if (def.width) widen (L, R, n, juce::jlimit (0.0f, 1.0f, c.get ("width", 1.0f)));
        if (fx.takeMainThreadRequest()) triggerAsyncUpdate();
    }

private:
    struct Resolved { int type = 0; std::vector<int> maps; std::vector<std::pair<int, float>> fixed; };
    void handleAsyncUpdate() override { const std::lock_guard<std::mutex> g (lock); fx.serviceMainThread(); }

    // message thread: the voice's effect into slot 1, its fixed settings, and every mapped parameter forgotten (set again on next drive)
    void loadVoice (int v)
    {
        const auto& r = resolved[(size_t) v];
        if (! def.warp) { fx.setParam (sfx::SpectralFx::pSlotType (0), r.type); fx.setParam (sfx::SpectralFx::pSlotOn (0), 1.0); fx.setParam (sfx::SpectralFx::pSlotMix (0), 100.0); }
        last.fill (std::numeric_limits<float>::quiet_NaN()); lastMix = -1.0f;
        loaded.store (v, std::memory_order_release);
        for (auto& [k, value] : r.fixed) setReal (k, value, true);
    }
    const sfx::ParamDesc* desc (int k) const
    {
        if (def.warp) return k >= 0 && k < sfx::kWarpParams ? &sfx::warpParams()[(size_t) k] : nullptr;
        const auto& td = sfx::types()[(size_t) resolved[(size_t) loaded.load()].type];
        return k >= 0 && k < (int) td.params.size() ? &td.params[(size_t) k] : nullptr;
    }
    // one parameter in its real units, only when it changes. The slot's first 8 go through the host path (any thread); the others
    // (the Spectral Equalizer's later bands) through the editor's path, which is the message thread's only
    void setReal (int k, float real, bool messageThread) noexcept
    {
        if (k == -2) { real = juce::jlimit (0.0f, 100.0f, real); if (real != lastMix) { fx.setParam (sfx::SpectralFx::pSlotMix (0), real); lastMix = real; } return; }
        const auto* d = desc (k); if (d == nullptr || k >= (int) last.size()) return;
        real = d->clamp (real);
        if (last[(size_t) k] == real) return;
        if (def.warp) fx.setParam (sfx::SpectralFx::pWarpParam (k), real);
        else if (k < sfx::kHostParamsPerSlot) fx.setParam (sfx::SpectralFx::pSlotParam (0, k), d->toNorm (real));
        else if (messageThread) fx.uiSlotValue (0, k, real, 1);
        else return;
        last[(size_t) k] = real;
    }
    void drive (const Controls& c, int v, bool messageThread, const TempoInfo& t, const float* glided) noexcept
    {
        const auto& voice = def.voices[(size_t) v]; const auto& r = resolved[(size_t) v];
        for (size_t i = 0; i < voice.maps.size(); ++i)
        {
            const auto& x = voice.maps[i]; const int k = r.maps[i]; const auto* d = k == -2 ? nullptr : desc (k);
            if ((d == nullptr && k != -2) || ! c.has (x.control.c_str())) continue;
            const float u = glided != nullptr && i < smooth.size() && x.how != How::EchoTime ? glided[i] : c.get (x.control.c_str(), 0.0f);
            float real = 0.0f;
            switch (x.how)
            {
                case How::Norm: real = d->fromNorm (juce::jlimit (0.0f, 1.0f, u)); break;
                case How::Log: { const float lo = juce::jmax (1.0e-3f, d->min); real = lo * std::pow (d->max / lo, juce::jlimit (0.0f, 1.0f, u)); break; }
                case How::BipLin: { const float b = juce::jlimit (-1.0f, 1.0f, u); real = b >= 0 ? x.a + (d->max - x.a) * b : x.a + (x.a - d->min) * b; break; }
                case How::BipLog: { const float b = juce::jlimit (-1.0f, 1.0f, u), lo = juce::jmax (1.0e-3f, d->min); real = b >= 0 ? x.a * std::pow (d->max / x.a, b) : x.a * std::pow (lo / x.a, -b); break; }
                case How::Real: real = u * x.a + x.b; break;
                case How::EqType: { const int ti = (int) std::lround (u); real = ti >= 0 && ti <= 4 ? (float) ti : 0.0f; break; }
                case How::Fade: real = 100.0f * juce::jlimit (0.0f, 1.0f, u * 4.0f); break;
                case How::EchoTime:
                {
                    const bool sync = c.get ("sync", 1.0f) >= 0.5f; const int di = juce::jlimit (0, (int) divisions().size() - 1, (int) std::lround (c.get ("div", 7.0f)));
                    const double bpm = messageThread ? 120.0 : juce::jmax (1.0, t.bpm);
                    real = sync ? (float) (divisions()[(size_t) di].beats * 60000.0 / bpm) : u; break;
                }
            }
            if (x.how == How::EchoTime && messageThread) continue;   // the tempo is the audio thread's
            setReal (k, real, messageThread);
        }
    }
    // mid/side width, glided over 20 ms (HAUNTED HALLS / ECTO ECHO's WIDTH; Spectral Corruptor's reverb and delay have none)
    void widen (float* l, float* r, int n, float target) noexcept
    {
        if (widthSm < 0.0f) widthSm = target;
        const float k = 1.0f - std::exp (-1.0f / (0.02f * (float) sr));
        for (int i = 0; i < n; ++i)
        {
            widthSm += k * (target - widthSm);
            if (widthSm > 0.99999f && target >= 1.0f) { widthSm = 1.0f; continue; }
            const float mid = 0.5f * (l[i] + r[i]), side = 0.5f * (l[i] - r[i]) * widthSm; l[i] = mid + side; r[i] = mid - side;
        }
    }

    const EngineDef& def; std::vector<Resolved> resolved; sfx::SpectralFx fx; std::mutex lock;
    std::array<float, sfx::kMaxParams> last {}; float lastMix = -1.0f; std::atomic<int> loaded { -1 }; double sr = 48000.0;
    float level = 1.0f, levelSm = -1.0f, widthSm = -1.0f; std::array<float, 48> smooth {}; int smoothVoice = -1;
};
} // namespace

std::vector<SpectralEngineInfo> spectralEngines (int unit)
{
    std::vector<SpectralEngineInfo> out; auto it = table().find (unit); if (it == table().end()) return out;
    for (auto& d : it->second) { SpectralEngineInfo i; i.name = d.name; for (auto& v : d.voices) i.voices.push_back (v.name); out.push_back (std::move (i)); }
    return out;
}
std::unique_ptr<AltEngine> makeSpectralEngine (int unit, int k)
{
    const auto* d = defOf (unit, k); return d ? std::make_unique<SpectralEngine> (*d) : nullptr;
}
const std::set<std::string>* spectralReads (int unit, const std::string& label)
{
    static const std::map<std::string, std::set<std::string>> reads = [] {
        std::map<std::string, std::set<std::string>> r;
        for (auto& [u, defs] : table()) for (auto& d : defs) for (auto& v : d.voices) r[std::to_string (u) + "|" + d.name + " \xc2\xb7 " + v.name] = readsOf (d, v);
        return r; }();
    auto it = reads.find (std::to_string (unit) + "|" + label); return it == reads.end() ? nullptr : &it->second;
}
} // namespace phantom::engines
