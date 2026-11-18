// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX: CHOWMATRIX (DECISIONS.md 94). Chowdhury DSP's ChowMatrix (github.com/Chowdhury-DSP/ChowMatrix @ 40d8e0e, BSD-3) runs whole
// and unchanged: its own ChowMatrix processor (a tree of delay nodes), its own factory presets (the 7 voices), its own processBlock, on
// the chowdsp_utils (c34df55) and Foleys GUI Magic (3f0188f) it was built on. Phantom FX only sets its parameters, the way a host's
// automation would, and gives it a play head carrying the host's tempo.
//
// ECTO ECHO's controls it follows (the rest are greyed out, ControlUse.cpp):
//   SYNC       its Sync switch (each node's delay time locks to a note value of the host tempo)
//   CHARACTER  its Insanity (random wandering of every node's delay time and pan)
//   WIDTH      Phantom FX's own mid/side width on ChowMatrix's output (1 = as the preset pans its nodes, 0 = mono): ChowMatrix has none
// Everything else is the preset's own node tree: each node's delay, feedback, pan, filters, distortion, pitch, diffusion, reverse and
// modulation, and its delay type. Its Dry is held off and its Wet at 0 dB: ECTO ECHO's slot MIX blends the dry, as for every ECHO engine.
//
// A preset change loads the preset (its own preset manager, which allocates) into the second instance on the message thread (update());
// the audio thread crossfades to it over 50 ms once it is ready, and only then is the first instance free to be loaded again.
#include "ChowMatrixEngine.h"
#include "ChowMatrixPrelude.h"
#include "../dsp/Modulators.h"
#include <atomic>
#include <mutex>

namespace chowmatrix
{
#include "ChowMatrix.h"
}

namespace phantom::engines
{
namespace
{
// ChowMatrix's factory presets (res/presets, as its PresetManager adds them; Default is its default preset)
constexpr const char* kPresets[] = { "Default", "Chord", "Crazy", "Lush", "Rhythmic", "Wobble", "Wonky" };
constexpr int kNumPresets = (int) (sizeof (kPresets) / sizeof (kPresets[0]));

struct HostHead final : juce::AudioPlayHead
{
    TempoInfo t;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p; p.setBpm (t.bpm); p.setTimeSignature (TimeSignature { t.timeSigNumerator, t.timeSigDenominator }); p.setPpqPosition (t.ppqPosition);
        p.setIsPlaying (t.playing); p.setIsLooping (t.looping); return p;
    }
};

class ChowMatrixEngine final : public AltEngine
{
public:
    // ChowMatrix's delay lines are large (each node's holds 2^19 samples, and it keeps 8 spare ones ready): its two instances are made
    // only when CHOWMATRIX is first chosen (update(), message thread), so an ECTO ECHO that never uses it costs nothing more
    ChowMatrixEngine() = default;
    // ChowMatrix's controls retry on the message thread (MessageManager::callAsync capturing the processor) when a preset is still
    // loading, and never cancel those calls: deleted at once, a retry still queued would run on a freed processor (the Mac crash in
    // 0.1.22's first install: DelayTypeControl::parameterChanged after a preset load). Its instances are handed to the message queue
    // instead and freed a few turns of it later, after every retry already queued (a retry that runs with no load under way and no
    // audio holding its lock succeeds and queues nothing more)
    ~ChowMatrixEngine() override
    {
        auto keep = std::make_shared<std::array<std::unique_ptr<chowmatrix::ChowMatrix>, 2>>();
        for (size_t k = 0; k < 2; ++k) { if (slot[k].p) slot[k].p->setPlayHead (nullptr); (*keep)[k] = std::move (slot[k].p); }
        if ((*keep)[0] || (*keep)[1]) freeLater (std::move (keep), 4);
    }
    void prepare (double sampleRate, int maxBlock) override
    {
        const std::lock_guard<std::mutex> g (makeLock);
        sr = sampleRate; block = juce::jmax (32, maxBlock);
        tmp.setSize (2, block, false, false, true);
        midi.ensureSize (64);
        if (live.load()) for (auto& s : slot) prepareSlot (s);
        reset();
    }
    // its own "flush delays" (the GUI's panic button): every node's delay line and filters cleared
    void reset() noexcept override
    {
        // only the playing instance: the other may be loading a preset (rebuilding its node tree) on the message thread
        if (live.load (std::memory_order_acquire))
            chowmatrix::NodeManager::doForNodes (slot[(size_t) active.load()].p->getNodes(), [] (chowmatrix::DelayNode* d) { d->flushDelays(); });
        fadePos = -1; widthSm = -1.0f; fading.store (false);
    }
    int latencySamples (int) const noexcept override { return live.load() ? slot[(size_t) active.load()].p->getLatencySamples() : 0; }
    // a host may prepare on one thread while the rack refreshes on the message thread: making and loading the instances is serialised
    void update (const Controls&, int voice) override
    {
        const std::lock_guard<std::mutex> g (makeLock);
        if (! live.load())
        {
            for (auto& s : slot) { s.p = std::make_unique<chowmatrix::ChowMatrix>(); s.p->setPlayHead (&head); prepareSlot (s); }
            loadInto (0, juce::jlimit (0, kNumPresets - 1, voice)); active.store (0);
            live.store (true, std::memory_order_release);
            return;
        }
        const int v = juce::jlimit (0, kNumPresets - 1, voice), a = active.load (std::memory_order_acquire);
        if (slot[(size_t) a].voice.load() == v || fading.load (std::memory_order_acquire)) return;
        const int o = 1 - a; if (slot[(size_t) o].voice.load() == v && slot[(size_t) o].ready.load()) return;
        loadInto (o, v);
    }
    void process (float* L, float* R, int n, const Controls& c, int voice, const TempoInfo& t) noexcept override
    {
        if (! live.load (std::memory_order_acquire)) { std::fill (L, L + n, 0.0f); std::fill (R, R + n, 0.0f); return; }   // not made yet
        head.t = t;
        const int v = juce::jlimit (0, kNumPresets - 1, voice);
        int a = active.load (std::memory_order_relaxed);
        if (fadePos < 0 && slot[(size_t) a].voice.load() != v)
        {
            const int o = 1 - a;
            if (slot[(size_t) o].ready.load (std::memory_order_acquire) && slot[(size_t) o].voice.load() == v)
            { fading.store (true, std::memory_order_release); fadePos = 0; }
        }
        for (int i0 = 0; i0 < n;)
        {
            const int m = juce::jmin (block, n - i0);
            if (fadePos >= 0)
            {
                const int o = 1 - a; for (int ch = 0; ch < 2; ++ch) tmp.copyFrom (ch, 0, ch == 0 ? L + i0 : R + i0, m);
                drive (o, c); run (o, tmp.getWritePointer (0), tmp.getWritePointer (1), m);
                drive (a, c); run (a, L + i0, R + i0, m);
                const int len = juce::jmax (1, (int) (0.05 * sr));
                for (int i = 0; i < m; ++i)
                {
                    const float x = juce::jmin (1.0f, (float) (fadePos + i) / (float) len), ga = std::cos (x * juce::MathConstants<float>::halfPi), gb = std::sin (x * juce::MathConstants<float>::halfPi);
                    L[i0 + i] = L[i0 + i] * ga + tmp.getSample (0, i) * gb; R[i0 + i] = R[i0 + i] * ga + tmp.getSample (1, i) * gb;
                }
                fadePos += m;
                if (fadePos >= len) { active.store (o, std::memory_order_release); a = o; fadePos = -1; slot[(size_t) (1 - o)].ready.store (false); fading.store (false, std::memory_order_release); }
            }
            else { drive (a, c); run (a, L + i0, R + i0, m); }
            widen (L + i0, R + i0, m, juce::jlimit (0.0f, 1.0f, c.get ("width", 1.0f)));
            i0 += m;
        }
    }
private:
    struct Slot { std::unique_ptr<chowmatrix::ChowMatrix> p; std::atomic<int> voice { -1 }; std::atomic<bool> ready { false }; std::array<float, 4> last {}; };
    static void freeLater (std::shared_ptr<std::array<std::unique_ptr<chowmatrix::ChowMatrix>, 2>> keep, int turns)
    {
        if (turns <= 0 || juce::MessageManager::getInstanceWithoutCreating() == nullptr) return;   // freed here (keep goes out of scope)
        juce::MessageManager::callAsync ([keep, turns] () mutable { freeLater (std::move (keep), turns - 1); });
    }
    void prepareSlot (Slot& s) { s.p->setRateAndBufferSizeDetails (sr, block); s.p->prepareToPlay (sr, block); s.last.fill (-99.0f); }
    void loadInto (int k, int v)
    {
        auto& s = slot[(size_t) k]; s.ready.store (false);
        for (int i = 0; i < s.p->getNumPrograms(); ++i)
            if (s.p->getProgramName (i) == kPresets[v]) { s.p->setCurrentProgram (i); break; }
        s.last.fill (-99.0f); set (s, 0, "dry_param", -60.0f); set (s, 1, "wet_param", 0.0f);
        s.voice.store (v); s.ready.store (true, std::memory_order_release);
    }
    // one of ChowMatrix's parameters, in its own units, set only when it changes (as a host's automation would: setValueNotifyingHost)
    static void set (Slot& s, int k, const char* id, float value) noexcept
    {
        if (std::abs (s.last[(size_t) k] - value) < 1.0e-6f) return;
        if (auto* p = s.p->getVTS().getParameter (id)) { p->setValueNotifyingHost (p->convertTo0to1 (value)); s.last[(size_t) k] = value; }
    }
    void drive (int k, const Controls& c) noexcept
    {
        auto& s = slot[(size_t) k];
        set (s, 0, "dry_param", -60.0f); set (s, 1, "wet_param", 0.0f);
        set (s, 2, "snyc", c.get ("sync", 1.0f) >= 0.5f ? 1.0f : 0.0f);   // "snyc": ChowMatrix's own parameter ID
        set (s, 3, "insanity", juce::jlimit (0.0f, 1.0f, c.get ("char", 0.0f)));
    }
    // mid/side width, glided over 20 ms
    void widen (float* l, float* r, int m, float target) noexcept
    {
        if (widthSm < 0.0f) widthSm = target;
        const float k = 1.0f - std::exp (-1.0f / (0.02f * (float) sr));
        for (int i = 0; i < m; ++i)
        {
            widthSm += k * (target - widthSm);
            if (widthSm > 0.99999f && target >= 1.0f) { widthSm = 1.0f; continue; }
            const float mid = 0.5f * (l[i] + r[i]), side = 0.5f * (l[i] - r[i]) * widthSm; l[i] = mid + side; r[i] = mid - side;
        }
    }
    void run (int k, float* l, float* r, int m) noexcept
    {
        float* ch[2] { l, r }; juce::AudioBuffer<float> buf (ch, 2, m); midi.clear(); slot[(size_t) k].p->processBlock (buf, midi);
    }
    std::array<Slot, 2> slot; HostHead head; juce::AudioBuffer<float> tmp; juce::MidiBuffer midi;
    std::atomic<int> active { 0 }; std::atomic<bool> fading { false }, live { false }; std::mutex makeLock; int fadePos = -1, block = 512; double sr = 48000.0; float widthSm = -1.0f;
};
} // namespace

int chowMatrixVoices() { return kNumPresets; }
std::string chowMatrixVoiceName (int voice) { return juce::String (kPresets[juce::jlimit (0, kNumPresets - 1, voice)]).toUpperCase().toStdString(); }
std::unique_ptr<AltEngine> makeChowMatrixEngine() { return std::make_unique<ChowMatrixEngine>(); }
}
