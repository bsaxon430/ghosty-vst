#include "GhostyExtras.h"
#include "FxRhythm.h"

// GHOSTY EXTRAS: two of Ghosty Pro's own rack units that Vital has nothing like, copied from Ghosty Pro's rack:
//   GHOST GATE  tempo-synced volume / time patterns (16 patterns)
//   HAUNT       tempo-synced stutter gestures (hold a pad)
// Their parameters live in a private parameter tree (not host parameters), as the units expect.
namespace ghosty {

namespace {
  // a processor that only owns the units' parameters (never processes audio)
  struct ParamHost : public AudioProcessor {
    const String getName() const override { return "extras"; }
    void prepareToPlay(double, int) override { }
    void releaseResources() override { }
    void processBlock(AudioBuffer<float>&, MidiBuffer&) override { }
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override { }
    const String getProgramName(int) override { return {}; }
    void changeProgramName(int, const String&) override { }
    void getStateInformation(MemoryBlock&) override { }
    void setStateInformation(const void*, int) override { }
  };

  AudioProcessorValueTreeState::ParameterLayout layout() {
    using namespace gpfx;
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add(std::make_unique<AudioParameterInt>(pid(uGate, "pattern"), "Gate Pattern", 1, kGatePatterns, 1));
    l.add(std::make_unique<AudioParameterFloat>(pid(uGate, "smooth"), "Gate Smooth", NormalisableRange<float>(0.0f, 1.0f), 0.2f));
    l.add(std::make_unique<AudioParameterFloat>(pid(uGate, "time"), "Gate Time FX", NormalisableRange<float>(0.0f, 1.0f), 1.0f));
    l.add(std::make_unique<AudioParameterInt>(pid(uGate, "length"), "Gate Length", 0, 2, 1));
    l.add(std::make_unique<AudioParameterInt>(pid(uHaunt, "pad"), "Haunt Pad", 0, kPads, 0));
    l.add(std::make_unique<AudioParameterBool>(pid(uHaunt, "sync"), "Haunt Sync", true));
    return l;
  }

  constexpr int kChunk = gpfx::kSub;
}

struct Extras::Impl {
  ParamHost host;
  AudioProcessorValueTreeState state { host, nullptr, "GHOSTY_EXTRAS_PARAMS", layout() };
  gpfx::GateStore gateStore;
  gpfx::PadStore padStore;
  gpfx::GateUnit gate;
  gpfx::HauntUnit haunt;
  std::atomic<bool> gateOn { false }, hauntOn { false };
  std::atomic<float> gateMix { 1.0f }, hauntMix { 1.0f };
  float gateFade = 0.0f, hauntFade = 0.0f, fadeStep = 0.0f;
  double sr = 44100.0;
  double freePpq = 0.0;   // free-running beat clock while the host is stopped (per instance, never shared)
  AudioBuffer<float> wet;

  Impl() {
    for (int i = 0; i < gpfx::kGatePatterns; ++i)
      gateStore.bake(i, gpfx::factoryGatePattern(i));
    gate.store = &gateStore;
    haunt.pads = &padStore;
    gate.attach(state);
    haunt.attach(state);
  }
};

Extras::Extras() : impl_(std::make_unique<Impl>()) { }
Extras::~Extras() = default;

void Extras::prepare(double sample_rate, int max_block) {
  auto& m = *impl_;
  m.sr = sample_rate;
  m.gate.prepare(sample_rate, kChunk);
  m.haunt.prepare(sample_rate, kChunk);
  m.wet.setSize(2, jmax(max_block, kChunk), false, false, true);
  m.fadeStep = (float) (1.0 / (0.010 * sample_rate));   // 10 ms on / off fades: no clicks
}

bool Extras::anyOn() const noexcept {
  const auto& m = *impl_;
  return m.gateOn.load(std::memory_order_relaxed) || m.hauntOn.load(std::memory_order_relaxed) || m.gateFade > 0.0f || m.hauntFade > 0.0f;
}

void Extras::process(AudioSampleBuffer& buffer, double ppq, double bpm, bool playing) noexcept {
  auto& m = *impl_;
  if (buffer.getNumChannels() < 2)
    return;
  float* L = buffer.getWritePointer(0);
  float* R = buffer.getWritePointer(1);
  const int total = buffer.getNumSamples();
  gpfx::BlockCtx ctx;
  ctx.sr = m.sr; ctx.bpm = bpm; ctx.playing = playing; ctx.samplesPerBeat = m.sr * 60.0 / jmax(1.0, bpm);
  if (!playing) {   // free-running at the host tempo when stopped
    ppq = m.freePpq;
    m.freePpq += total / ctx.samplesPerBeat;
  }
  for (int start = 0; start < total; start += kChunk) {
    const int n = jmin(kChunk, total - start);
    ctx.ppq = ppq + start / ctx.samplesPerBeat;
    auto run = [&](gpfx::UnitDsp& unit, std::atomic<bool>& on, float& fade, float mix) {
      const bool want = on.load(std::memory_order_relaxed);
      if (!want && fade <= 0.0f) { unit.idle(L + start, R + start, n); return; }
      if (want && fade <= 0.0f) unit.reset();
      float* wl = m.wet.getWritePointer(0);
      float* wr = m.wet.getWritePointer(1);
      std::memcpy(wl, L + start, sizeof(float) * (size_t) n);
      std::memcpy(wr, R + start, sizeof(float) * (size_t) n);
      unit.process(wl, wr, n, ctx);
      for (int i = 0; i < n; ++i) {
        fade = jlimit(0.0f, 1.0f, fade + (want ? m.fadeStep : -m.fadeStep));
        const float w = fade * mix;
        L[start + i] += w * (wl[i] - L[start + i]);
        R[start + i] += w * (wr[i] - R[start + i]);
      }
    };
    run(m.gate, m.gateOn, m.gateFade, m.gateMix.load(std::memory_order_relaxed));
    run(m.haunt, m.hauntOn, m.hauntFade, m.hauntMix.load(std::memory_order_relaxed));
  }
}

ValueTree Extras::toTree() const {
  auto& m = *impl_;
  ValueTree t("GHOSTY_EXTRAS");
  t.setProperty("gateOn", m.gateOn.load(), nullptr);
  t.setProperty("hauntOn", m.hauntOn.load(), nullptr);
  t.setProperty("gateMix", m.gateMix.load(), nullptr);
  t.setProperty("hauntMix", m.hauntMix.load(), nullptr);
  t.appendChild(m.state.copyState(), nullptr);
  return t;
}

void Extras::fromTree(const ValueTree& t) {
  auto& m = *impl_;
  m.gateOn = (bool) t.getProperty("gateOn", false);
  m.hauntOn = (bool) t.getProperty("hauntOn", false);
  m.gateMix = (float) t.getProperty("gateMix", 1.0f);
  m.hauntMix = (float) t.getProperty("hauntMix", 1.0f);
  auto params = t.getChildWithName("GHOSTY_EXTRAS_PARAMS");
  if (params.isValid())
    m.state.replaceState(params);
}

void Extras::setGateOn(bool on) { impl_->gateOn = on; }
bool Extras::gateOn() const { return impl_->gateOn.load(); }
void Extras::setHauntOn(bool on) { impl_->hauntOn = on; if (!on) impl_->haunt.uiPad = -1; }
bool Extras::hauntOn() const { return impl_->hauntOn.load(); }

static void setParam(AudioProcessorValueTreeState& s, const String& id, float plain) {
  if (auto* p = s.getParameter(id))
    p->setValueNotifyingHost(p->convertTo0to1(plain));
}

void Extras::setGatePattern(int index) { setParam(impl_->state, gpfx::pid(gpfx::uGate, "pattern"), (float) (jlimit(0, gpfx::kGatePatterns - 1, index) + 1)); }
int Extras::gatePattern() const { return (int) std::lround(impl_->state.getRawParameterValue(gpfx::pid(gpfx::uGate, "pattern"))->load()) - 1; }
void Extras::setGateLength(int index) { setParam(impl_->state, gpfx::pid(gpfx::uGate, "length"), (float) jlimit(0, 2, index)); }
int Extras::gateLength() const { return (int) std::lround(impl_->state.getRawParameterValue(gpfx::pid(gpfx::uGate, "length"))->load()); }
void Extras::holdHauntPad(int pad) { impl_->haunt.uiPad = pad < 0 ? -1 : jlimit(0, gpfx::kPads - 1, pad); }

StringArray Extras::gatePatternNames() {
  StringArray n;
  for (int i = 0; i < gpfx::kGatePatterns; ++i) n.add(gpfx::factoryGatePattern(i).name.toUpperCase());
  return n;
}

StringArray Extras::hauntPadNames() {
  StringArray n;
  for (int i = 0; i < gpfx::kPads; ++i) {
    const auto d = gpfx::factoryPad(i);
    n.add(gpfx::hauntTypeNames()[d.type] + " " + gpfx::padDivs()[(size_t) jlimit(0, (int) gpfx::padDivs().size() - 1, d.div)].name);
  }
  return n;
}

} // namespace ghosty
