// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX: QDELAY (DECISIONS.md 93). tiagolr's QDelay (github.com/tiagolr/qdelay @ 3130de7, GPL-3) runs whole and unchanged: its own
// QDelayAudioProcessor, its own factory presets (the 26 voices), its own processBlock. Phantom FX only sets its parameters, the way a
// host's automation would, and gives it a play head carrying the host's tempo.
//
// ECTO ECHO's controls it follows (the rest are greyed out, ControlUse.cpp):
//   SYNC / DIVISION  QDelay's own sync (Straight / Triplet / Dotted x 1/64..1/1, exact to the sample); 2/1, which QDelay has not, as a time
//   TIME             (SYNC off) QDelay's rate in seconds (its own 1 ms steps)
//   FEEDBACK         its feedback (0 .. 0.95)
//   WIDTH            its stereo width
//   DUCKING          its duck amount
// Everything else is the preset's: mode (ping-pong, tap ...), swing, feel, diffusion, tape, pitch, distortion, phaser, EQ.
// QDelay's MIX is held at 100 % wet and its output gain at 0 dB: ECTO ECHO's slot MIX blends the dry, as for every ECHO engine.
//
// A preset change loads the preset (QDelay's own PresetMgr::loadFactory, which allocates) into the second instance on the message thread
// (update()); the audio thread crossfades to it over 50 ms once it is ready, and only then is the first instance free to be loaded again.
#include "QDelayEngine.h"
#include "QDelayPrelude.h"
#include "../dsp/Modulators.h"
#include <atomic>
#include <mutex>

namespace qdelay
{
#include "PluginProcessor.h"
}

namespace phantom::engines
{
namespace
{
const auto& presets() { return qdelay::PresetMgr::factoryPresets; }

struct HostHead final : juce::AudioPlayHead
{
    TempoInfo t;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p; p.setBpm (t.bpm); p.setTimeSignature (TimeSignature { t.timeSigNumerator, t.timeSigDenominator }); p.setPpqPosition (t.ppqPosition);
        p.setIsPlaying (t.playing); p.setIsLooping (t.looping); return p;
    }
};

class QDelayEngine final : public AltEngine
{
public:
    // Its two instances are made only when QDELAY is first chosen (update(), message thread). QDelay's constructor reseeds the C library's
    // shared random generator from the clock (srand (time (nullptr))), which the Airwindows effects draw their dither seeds from: made
    // eagerly in every ECTO ECHO, that made those effects' dither (about 1e-6, far below hearing) differ from run to run (0.1.21's Mac
    // "looks: the sound is untouched" check). Made only on demand, an ECTO ECHO that never picks QDELAY leaves everything as before.
    QDelayEngine() = default;
    void prepare (double sampleRate, int maxBlock) override
    {
        const std::lock_guard<std::mutex> g (makeLock);
        sr = sampleRate; block = juce::jmax (32, maxBlock);
        for (auto& b : tmp) b.setSize (2, block, false, false, true);
        midi.ensureSize (64);
        if (live.load()) for (auto& s : slot) prepareSlot (s);
        reset();
    }
    // QDelay glides its delay time over 150 ms (its RCFilter), bending the pitch of the repeats like tape while TIME moves: by design
    bool zipperEventControl (const std::string& c) const override { return c == "time"; }
    // only the playing instance: the other may be loading a preset on the message thread (it is reset when the fade to it starts)
    void reset() noexcept override { if (live.load (std::memory_order_acquire)) slot[(size_t) active.load()].p->reset(); fadePos = -1; duckSm = -1.0f; fading.store (false); }
    int latencySamples (int) const noexcept override { return live.load() ? slot[(size_t) active.load()].p->getLatencySamples() : 0; }
    // message thread: the chosen preset loaded into the instance not playing, ready for the audio thread to fade to
    // a host may prepare on one thread while the rack refreshes on the message thread: making and loading the instances is serialised
    void update (const Controls&, int voice) override
    {
        const std::lock_guard<std::mutex> g (makeLock);
        if (! live.load())
        {
            for (auto& s : slot) { s.p = std::make_unique<qdelay::QDelayAudioProcessor>(); s.p->setPlayHead (&head); prepareSlot (s); }
            loadInto (0, juce::jlimit (0, (int) presets().size() - 1, voice)); active.store (0);
            live.store (true, std::memory_order_release);
            return;
        }
        const int v = juce::jlimit (0, (int) presets().size() - 1, voice), a = active.load (std::memory_order_acquire);
        if (slot[(size_t) a].voice.load() == v || fading.load (std::memory_order_acquire)) return;
        const int o = 1 - a; if (slot[(size_t) o].voice.load() == v && slot[(size_t) o].ready.load()) return;
        loadInto (o, v);
    }
    void process (float* L, float* R, int n, const Controls& c, int voice, const TempoInfo& t) noexcept override
    {
        if (! live.load (std::memory_order_acquire)) { std::fill (L, L + n, 0.0f); std::fill (R, R + n, 0.0f); return; }   // not made yet
        head.t = t;
        const int v = juce::jlimit (0, (int) presets().size() - 1, voice);
        int a = active.load (std::memory_order_relaxed);
        if (fadePos < 0 && slot[(size_t) a].voice.load() != v)
        {
            const int o = 1 - a;
            if (slot[(size_t) o].ready.load (std::memory_order_acquire) && slot[(size_t) o].voice.load() == v)
            { fading.store (true, std::memory_order_release); fadePos = 0; slot[(size_t) o].p->reset(); }
        }
        const float duckT = juce::jlimit (0.0f, 0.999f, c.get ("duck", 0.0f));
        if (duckSm < 0.0f) duckSm = duckT;
        for (int i0 = 0; i0 < n;)
        {
            // DUCKING glides over 50 ms and is handed to QDelay in 16-sample pieces while it moves: QDelay reads it once a block, so a
            // whole block's step would be heard (the same as a host's automation at a finer grain); otherwise whole blocks
            const bool moving = std::abs (duckT - duckSm) > 1.0e-4f;
            const int m = juce::jmin (moving ? 16 : block, n - i0);
            duckSm += (1.0f - std::exp (-(float) m / (0.05f * (float) sr))) * (duckT - duckSm); if (! moving) duckSm = duckT;
            if (fadePos >= 0)
            {
                const int o = 1 - a; for (int ch = 0; ch < 2; ++ch) tmp[0].copyFrom (ch, 0, ch == 0 ? L + i0 : R + i0, m);
                drive (o, c, t); run (o, tmp[0].getWritePointer (0), tmp[0].getWritePointer (1), m);
                drive (a, c, t); run (a, L + i0, R + i0, m);
                const int len = juce::jmax (1, (int) (0.05 * sr));
                for (int i = 0; i < m; ++i)
                {
                    const float x = juce::jmin (1.0f, (float) (fadePos + i) / (float) len), ga = std::cos (x * juce::MathConstants<float>::halfPi), gb = std::sin (x * juce::MathConstants<float>::halfPi);
                    L[i0 + i] = L[i0 + i] * ga + tmp[0].getSample (0, i) * gb; R[i0 + i] = R[i0 + i] * ga + tmp[0].getSample (1, i) * gb;
                }
                fadePos += m;
                if (fadePos >= len) { active.store (o, std::memory_order_release); a = o; fadePos = -1; slot[(size_t) (1 - o)].ready.store (false); fading.store (false, std::memory_order_release); }
            }
            else { drive (a, c, t); run (a, L + i0, R + i0, m); }
            i0 += m;
        }
    }
private:
    struct Slot { std::unique_ptr<qdelay::QDelayAudioProcessor> p; std::atomic<int> voice { -1 }; std::atomic<bool> ready { false }; std::array<float, 8> last {}; };
    void prepareSlot (Slot& s) { s.p->setRateAndBufferSizeDetails (sr, block); s.p->prepareToPlay (sr, block); s.last.fill (-99.0f); }
    void loadInto (int k, int v)
    {
        auto& s = slot[(size_t) k]; s.ready.store (false);
        s.p->presetmgr->loadFactory (v);
        set (s, 6, "mix", 1.0f); set (s, 7, "out_gain", 0.0f);
        s.voice.store (v); s.last.fill (-99.0f); set (s, 6, "mix", 1.0f); set (s, 7, "out_gain", 0.0f);
        s.ready.store (true, std::memory_order_release);
    }
    // one of QDelay's parameters, in its own units, set only when it changes (as a host's automation would: setValueNotifyingHost)
    static void set (Slot& s, int k, const char* id, float value) noexcept
    {
        if (std::abs (s.last[(size_t) k] - value) < 1.0e-6f) return;
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (s.p->params.getParameter (id))) { p->setValueNotifyingHost (p->convertTo0to1 (value)); s.last[(size_t) k] = value; }
    }
    void drive (int k, const Controls& c, const TempoInfo& t) noexcept
    {
        auto& s = slot[(size_t) k];
        const bool sync = c.get ("sync", 1.0f) >= 0.5f; const int di = juce::jlimit (0, (int) divisions().size() - 1, (int) std::lround (c.get ("div", 7.0f)));
        // ECTO ECHO's division in QDelay's own terms: { sync mode (1 Straight, 2 Triplet, 3 Dotted), rate (0 = 1/64 .. 6 = 1/1) }; 2/1: none
        static constexpr int kMode[] = { 1, 1, 2, 1, 3, 2, 1, 3, 2, 1, 3, 2, 1, 3, 1, 0 }, kRate[] = { 0, 1, 2, 2, 2, 3, 3, 3, 4, 4, 4, 5, 5, 5, 6, 0 };
        set (s, 0, "link", 1.0f);
        if (sync && kMode[di] > 0) { set (s, 1, "sync_l", (float) kMode[di]); set (s, 2, "rate_sync_l", (float) kRate[di]); }
        else
        {
            const double ms = sync ? divisions()[(size_t) di].beats * 60000.0 / juce::jmax (1.0, t.bpm) : (double) c.get ("time", 375.0f);
            set (s, 1, "sync_l", 0.0f); set (s, 3, "rate_l", (float) juce::jlimit (0.001, 5.0, ms / 1000.0));
        }
        set (s, 4, "feedback", juce::jlimit (0.0f, 0.95f, c.get ("fb", 0.45f)));
        set (s, 5, "stereo", juce::jlimit (0.0f, 1.0f, c.get ("width", 1.0f)));
        if (c.has ("duck")) { if (auto* d = s.p->params.getParameter ("duck_amt")) { const float want = duckSm; if (std::abs (d->getValue() - d->convertTo0to1 (want)) > 1.0e-6f) d->setValueNotifyingHost (d->convertTo0to1 (want)); } }
    }
    void run (int k, float* l, float* r, int m) noexcept
    {
        float* ch[2] { l, r }; juce::AudioBuffer<float> buf (ch, 2, m); midi.clear(); slot[(size_t) k].p->processBlock (buf, midi);
    }
    std::array<Slot, 2> slot; HostHead head; std::array<juce::AudioBuffer<float>, 1> tmp; juce::MidiBuffer midi;
    std::atomic<int> active { 0 }; std::atomic<bool> fading { false }, live { false }; std::mutex makeLock; int fadePos = -1, block = 512; double sr = 48000.0; float duckSm = -1.0f;
};
} // namespace

int qdelayVoices() { return (int) presets().size(); }
std::string qdelayVoiceName (int voice)
{
    juce::String n (presets()[(size_t) juce::jlimit (0, (int) presets().size() - 1, voice)].name);
    if (n.startsWith ("--")) n = n.removeCharacters ("-").trim();   // "-- Init --" -> "Init"
    const juce::String cat (presets()[(size_t) juce::jlimit (0, (int) presets().size() - 1, voice)].category);
    return ((cat.isNotEmpty() ? cat.toUpperCase() + " " : juce::String()) + n).toStdString();
}
std::unique_ptr<AltEngine> makeQDelayEngine() { return std::make_unique<QDelayEngine>(); }
}
