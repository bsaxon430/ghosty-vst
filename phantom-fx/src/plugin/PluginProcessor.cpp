// SPDX-License-Identifier: AGPL-3.0-or-later
#include "PluginProcessor.h"
#include "FactoryPresets.h"
#include "PluginEditor.h"

namespace phantom
{
namespace
{
    const juce::Identifier kTopology ("TOPOLOGY"), kMods ("MODS"), kRoute ("ROUTE"), kSteps ("STEPS"), kScenesId ("SCENES"), kScene ("SCENE"), kParams ("PARAMS"), kData ("DATA");

    juce::NormalisableRange<float> rangeFor (const ParamSpec& s)
    {
        juce::NormalisableRange<float> r (s.min, s.max);
        if (s.skewCentre > s.min && s.skewCentre < s.max) r.setSkewForCentre (s.skewCentre);
        return r;
    }
    std::unique_ptr<juce::RangedAudioParameter> makeParam (const juce::String& id, const juce::String& name, const ParamSpec& s)
    {
        switch (s.type)
        {
            case ParamSpec::Type::Choice: { juce::StringArray items; for (auto& c : s.choices) items.add (c); return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, items, (int) s.def); }
            case ParamSpec::Type::Bool: return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, s.def >= 0.5f);
            case ParamSpec::Type::Int: return std::make_unique<juce::AudioParameterInt> (juce::ParameterID { id, 1 }, name, (int) s.min, (int) s.max, (int) s.def);
            case ParamSpec::Type::Float: break;
        }
        const juce::String unit = s.unit;
        auto attrs = juce::AudioParameterFloatAttributes().withLabel (unit)
            .withStringFromValueFunction ([unit] (float v, int) {
                if (unit == "%") return juce::String (juce::roundToInt (v * 100.0f)) + " %";
                if (unit == "Hz" && v >= 1000.0f) return juce::String (v / 1000.0f, 2) + " kHz";
                return juce::String (v, 2) + (unit.isEmpty() ? "" : " " + unit); })
            .withValueFromStringFunction ([unit] (const juce::String& t) {
                auto s2 = t.trim().toLowerCase(); double v = s2.getDoubleValue();
                if (unit == "%") v /= 100.0;
                if (unit == "Hz" && s2.containsChar ('k')) v *= 1000.0;
                return (float) v; });
        return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name, rangeFor (s), s.def, attrs);
    }
    juce::String joinInts (const std::array<int, kNumUnits>& a) { juce::String s; for (int v : a) s << v << ","; return s; }
    void splitInts (const juce::String& s, std::array<int, kNumUnits>& out) { juce::StringArray p; p.addTokens (s, ",", ""); p.removeEmptyStrings(); for (int i = 0; i < kNumUnits && i < p.size(); ++i) out[(size_t) i] = p[i].getIntValue(); }
    juce::String sceneTimeName (int i) { static const char* n[] { "CUT", "1/4 BEAT", "1/2 BEAT", "1 BEAT", "2 BEATS", "1 BAR", "2 BARS", "4 BARS" }; return n[juce::jlimit (0, 7, i)]; }
    double sceneTimeBeats (int i) { static const double b[] { 0.0, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0, 16.0 }; return b[juce::jlimit (0, 7, i)]; }
    double quantBeats (int i) { static const double b[] { 0.0, 0.25, 0.5, 1.0, 4.0, 8.0 }; return b[juce::jlimit (0, 5, i)]; }
}

juce::String PhantomProcessor::versionString() { return JucePlugin_VersionString; }

PhantomProcessor::PhantomProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true).withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undo, "PHANTOMFX", createLayout())
{
    auto raw = [this] (const juce::String& id) { auto* p = apvts.getRawParameterValue (id); jassert (p != nullptr); return p; };
    pIn = raw ("in"); pOut = raw ("out"); pMix = raw ("mix"); pBypass = raw ("bypass"); pOs = raw ("os");
    pScene = raw ("scene"); pSceneTime = raw ("scene_time"); pSceneQuant = raw ("scene_quant");
    for (int k = 0; k < kNumMacros; ++k) pMacro[(size_t) k] = raw ("macro" + juce::String (k + 1));
    for (int k = 0; k < kLanes; ++k) { const auto b = "lane" + juce::String (k) + "_"; pLaneGain[(size_t) k] = raw (b + "gain"); pLanePan[(size_t) k] = raw (b + "pan"); pLaneMute[(size_t) k] = raw (b + "mute"); }
    for (int k = 0; k < kNumLfos; ++k) { const auto b = "lfo" + juce::String (k + 1) + "_"; pLfoShape[(size_t) k] = raw (b + "shape"); pLfoSync[(size_t) k] = raw (b + "sync"); pLfoDiv[(size_t) k] = raw (b + "div"); pLfoRate[(size_t) k] = raw (b + "rate"); }
    for (int k = 0; k < kNumSteps; ++k) { const auto b = "step" + juce::String (k + 1) + "_"; pStepCount[(size_t) k] = raw (b + "count"); pStepLen[(size_t) k] = raw (b + "len"); pStepGlide[(size_t) k] = raw (b + "glide"); }
    for (int k = 0; k < kNumEnvs; ++k) { const auto b = "env" + juce::String (k + 1) + "_"; pEnvAtk[(size_t) k] = raw (b + "atk"); pEnvRel[(size_t) k] = raw (b + "rel"); }
    bindings.resize ((size_t) rack.numSlots());
    for (int u = 0; u < rack.numSlots(); ++u)
    {
        auto& s = rack.slot (u); auto& b = bindings[(size_t) u];
        b.on = raw (pid (u, "on")); b.mix = unitDef (u).hasMix ? raw (pid (u, "mix")) : nullptr; b.trim = raw (pid (u, "trim"));
        for (auto& p : s.params()) { b.values.push_back (raw (pid (u, p.id))); if (! p.isContinuous()) apvts.addParameterListener (pid (u, p.id), this); }
        rack.hostValues[(size_t) u].assign (s.params().size(), 0.0f);
    }
    apvts.addParameterListener ("os", this); apvts.addParameterListener ("scene", this);
    pSpOn = raw ("fx_spatial_on"); pSpOut = raw ("fx_spatial_out");
    for (int u = 0; u < kNumUnits; ++u) if (spatialEligible (u)) { pSpAz[(size_t) u] = raw (spid (u, "azimuth")); pSpEl[(size_t) u] = raw (spid (u, "elevation")); pSpDist[(size_t) u] = raw (spid (u, "distance")); }
    // non-parameter state lives in the same tree so undo, scenes and presets carry it
    for (auto id : { kTopology, kMods, kSteps, kScenesId, kData }) if (! apvts.state.getChildWithName (id).isValid()) apvts.state.appendChild (juce::ValueTree (id), nullptr);
    auto topoT = child (kTopology); Topology t;
    if (! topoT.hasProperty ("order")) { topoT.setProperty ("order", joinInts (t.order), nullptr); topoT.setProperty ("lane", joinInts (t.lane), nullptr); }
    readTopologyFromState(); readRoutesFromState(); readStepsFromState(); readDataFromState();
    startTimerHz (60);
}

PhantomProcessor::~PhantomProcessor()
{
    stopTimer();
    for (int u = 0; u < rack.numSlots(); ++u) for (auto& p : rack.slot (u).params()) if (! p.isContinuous()) apvts.removeParameterListener (pid (u, p.id), this);
    apvts.removeParameterListener ("os", this); apvts.removeParameterListener ("scene", this);
}

juce::AudioProcessorValueTreeState::ParameterLayout PhantomProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    auto dB = [] (float lo, float hi) { return juce::NormalisableRange<float> (lo, hi); };
    auto pct = [] { return juce::AudioParameterFloatAttributes().withLabel ("%").withStringFromValueFunction ([] (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }); };
    auto fmt = [] (const juce::String& unit, int decimals) { return juce::AudioParameterFloatAttributes().withLabel (unit).withStringFromValueFunction ([unit, decimals] (float v, int) { return juce::String (v, decimals) + (unit.isEmpty() ? "" : " " + unit); }); };
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "in", 1 }, "In", dB (-24.0f, 24.0f), 0.0f, fmt ("dB", 1)));
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "out", 1 }, "Out", dB (-24.0f, 24.0f), 0.0f, fmt ("dB", 1)));
    l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "mix", 1 }, "Mix", juce::NormalisableRange<float> (0.0f, 1.0f), 1.0f, pct()));
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "bypass", 1 }, "Bypass", false));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "os", 1 }, "Oversampling", juce::StringArray { "OFF", "2X", "4X", "8X", "RENDER" }, 2));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "scene", 1 }, "Scene", juce::StringArray { "OFF", "1", "2", "3", "4", "5", "6", "7", "8" }, 0));
    { juce::StringArray t; for (int i = 0; i < 8; ++i) t.add (sceneTimeName (i)); l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "scene_time", 1 }, "Scene Morph Time", t, 0)); }
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "scene_quant", 1 }, "Scene Quantise", juce::StringArray { "OFF", "1/16", "1/8", "BEAT", "BAR", "2 BARS" }, 3));
    for (int k = 0; k < kNumMacros; ++k) l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "macro" + juce::String (k + 1), 1 }, "Macro " + juce::String (k + 1) + " (" + juce::String (macroDefaultName (k)).toLowerCase() + ")", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct()));
    for (int k = 0; k < kLanes; ++k)
    {
        const juce::String b = "lane" + juce::String (k) + "_", n = k == 0 ? juce::String ("Main Lane ") : "Lane " + juce::String::charToString ((juce::juce_wchar) ('A' + k - 1)) + " ";
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { b + "gain", 1 }, n + "Gain", dB (-24.0f, 12.0f), 0.0f, fmt ("dB", 1)));
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { b + "pan", 1 }, n + "Pan", juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f, fmt ("", 2)));
        l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { b + "mute", 1 }, n + "Mute", false));
    }
    for (int k = 0; k < kNumLfos; ++k)
    {
        const juce::String b = "lfo" + juce::String (k + 1) + "_", n = "LFO " + juce::String (k + 1) + " ";
        juce::StringArray shapes; for (auto& s : lfoShapeNames()) shapes.add (s); juce::StringArray divs; for (auto& d : divisionNames()) divs.add (d);
        l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { b + "shape", 1 }, n + "Shape", shapes, 0));
        l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { b + "sync", 1 }, n + "Sync", true));
        l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { b + "div", 1 }, n + "Division", divs, 9 + k));
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { b + "rate", 1 }, n + "Rate", juce::NormalisableRange<float> (0.05f, 20.0f, 0.0f, 0.4f), 1.0f, fmt ("Hz", 2)));
    }
    for (int k = 0; k < kNumSteps; ++k)
    {
        const juce::String b = "step" + juce::String (k + 1) + "_", n = "Step Seq " + juce::String (k + 1) + " ";
        l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { b + "count", 1 }, n + "Steps", juce::StringArray { "8", "16", "32" }, 1));
        l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { b + "len", 1 }, n + "Length", juce::StringArray { "1 BEAT", "2 BEATS", "1 BAR", "2 BARS", "4 BARS" }, 2));
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { b + "glide", 1 }, n + "Glide", juce::NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct()));
    }
    for (int k = 0; k < kNumEnvs; ++k)
    {
        const juce::String b = "env" + juce::String (k + 1) + "_", n = "Env " + juce::String (k + 1) + " ";
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { b + "atk", 1 }, n + "Attack", juce::NormalisableRange<float> (0.1f, 200.0f, 0.0f, 0.4f), 5.0f, fmt ("ms", 1)));
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { b + "rel", 1 }, n + "Release", juce::NormalisableRange<float> (5.0f, 2000.0f, 0.0f, 0.4f), 120.0f, fmt ("ms", 0)));
    }
    for (int u = 0; u < rack.numSlots(); ++u)
    {
        auto& s = rack.slot (u); const auto& d = unitDef (u); const juce::String T = juce::String ("FX ") + d.title + " ";
        l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { pid (u, "on"), 1 }, T + "On", u == uLim));
        if (d.hasMix) l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { pid (u, "mix"), 1 }, T + (u == uOtt ? "Depth" : "Mix"), juce::NormalisableRange<float> (0.0f, 1.0f), d.defaultMix, pct()));
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { pid (u, "trim"), 1 }, T + "Trim", dB (-24.0f, 12.0f), 0.0f, fmt ("dB", 1)));
        for (auto& p : s.params()) l.add (makeParam (pid (u, p.id), T + p.name, p));
    }
    // SPATIAL (Phase 6b, SPATIAL.md): the switch, the output mode, and every tile's place (FL records and plays these as normal automation)
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "fx_spatial_on", 1 }, "Spatial On", false));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "fx_spatial_out", 1 }, "Spatial Output", juce::StringArray { "HEADPHONES", "SPEAKERS" }, 0));
    for (int u = 0; u < kNumUnits; ++u)
    {
        if (! spatialEligible (u)) continue;
        const juce::String T = juce::String ("Spatial ") + unitDef (u).title + " ";
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { spid (u, "azimuth"), 1 }, T + "Azimuth", juce::NormalisableRange<float> (-180.0f, 180.0f), 0.0f, fmt ("deg", 0)));
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { spid (u, "elevation"), 1 }, T + "Elevation", juce::NormalisableRange<float> (-90.0f, 90.0f), 0.0f, fmt ("deg", 0)));
        l.add (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { spid (u, "distance"), 1 }, T + "Distance", juce::NormalisableRange<float> (0.0f, kSpatialMaxDistance, 0.0f, 0.5f), 0.0f, fmt ("m", 2)));
    }
    return l;
}

bool PhantomProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet(), in = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    return in == out;
}

void PhantomProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate; maxBlock = samplesPerBlock;
    const int ch = juce::jmax (1, getTotalNumOutputChannels());
    rack.prepare (sampleRate, samplesPerBlock, ch, (OversamplingPolicy) (int) pOs->load());
    globalDry.prepare (1 << 16, ch); dryBuf.setSize (ch, samplesPerBlock);
    tpOs = std::make_unique<juce::dsp::Oversampling<float>> ((size_t) ch, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, false, false);
    tpOs->initProcessing ((size_t) samplesPerBlock); tpBuf.setSize (ch, samplesPerBlock);
    inSm.prepare (sampleRate, 20.0f, pIn->load(), false); outSm.prepare (sampleRate, 20.0f, pOut->load(), false);
    mixSm.prepare (sampleRate, 20.0f, pMix->load(), false); bypassSm.prepare (sampleRate, 10.0f, pBypass->load() >= 0.5f ? 1.0f : 0.0f, false);
    pushHostValues();
    rack.applyTopology(); spatialSwitchPending = false;
    refreshLatency();
}

void PhantomProcessor::reset() { rack.reset(); globalDry.reset(); if (tpOs) tpOs->reset(); }

void PhantomProcessor::refreshLatency()
{
    const int total = rack.getLatencySamples();
    globalDry.setDelay (total);
    if (total != reportedLatency.exchange (total)) setLatencySamples (total);
}

void PhantomProcessor::parameterChanged (const juce::String& id, float value)
{
    if (id == "scene") { const int s = (int) std::lround (value); if (s != lastSceneParam) { lastSceneParam = s; if (s > 0) midiSceneRequest = s - 1; } return; }
    topologyDirty = true; triggerAsyncUpdate();
}

void PhantomProcessor::handleAsyncUpdate()
{
    if (! topologyDirty.exchange (false)) return;
    const juce::ScopedLock sl (getCallbackLock());
    // the units' switches (ENGINE choice, voice, mode...) as the host has them now: the audio thread copies them into the units only at
    // its next block, and the update below must see the new choice (an engine builds / loads its voice there: a CURVES set picked from
    // CURVES played dry until another switch moved, 0.1.25). Under the callback lock no block runs; the next block writes the same values.
    for (int u = 0; u < rack.numSlots(); ++u)
    {
        auto& b = bindings[(size_t) u]; auto& s = rack.slot (u); auto& hv = rack.hostValues[(size_t) u]; const auto& ps = s.params();
        for (size_t i = 0; i < b.values.size() && i < hv.size() && i < ps.size(); ++i)
            if (! ps[i].isContinuous()) { hv[i] = b.values[i]->load (std::memory_order_relaxed); s.setValue ((int) i, hv[i]); }
    }
    rack.setPolicy ((OversamplingPolicy) (int) pOs->load());
    rack.applyTopology();
    refreshLatency();
}

void PhantomProcessor::pushHostValues() noexcept
{
    for (int u = 0; u < rack.numSlots(); ++u)
    {
        auto& b = bindings[(size_t) u]; auto& s = rack.slot (u); auto& hv = rack.hostValues[(size_t) u];
        for (size_t i = 0; i < b.values.size() && i < hv.size(); ++i) hv[i] = b.values[i]->load (std::memory_order_relaxed);
        s.setEnabled (b.on->load (std::memory_order_relaxed) >= 0.5f);
        s.setMix (b.mix ? b.mix->load (std::memory_order_relaxed) : 1.0f);
        s.setTrimDb (b.trim->load (std::memory_order_relaxed));
    }
    for (int k = 0; k < kNumMacros; ++k) rack.macros[(size_t) k] = pMacro[(size_t) k]->load (std::memory_order_relaxed);
    for (int k = 0; k < kLanes; ++k) rack.setLane (k, pLaneGain[(size_t) k]->load (std::memory_order_relaxed), pLanePan[(size_t) k]->load (std::memory_order_relaxed), pLaneMute[(size_t) k]->load (std::memory_order_relaxed) >= 0.5f);
    for (int k = 0; k < kNumLfos; ++k) { auto& l = rack.lfos[(size_t) k]; l.setShape ((LfoShape) (int) pLfoShape[(size_t) k]->load()); l.setSync (pLfoSync[(size_t) k]->load() >= 0.5f, (int) pLfoDiv[(size_t) k]->load()); l.setRateHz (pLfoRate[(size_t) k]->load()); }
    for (int k = 0; k < kNumSteps; ++k) { auto& s = rack.steps[(size_t) k]; static const int counts[] { 8, 16, 32 }; static const double lens[] { 1, 2, 4, 8, 16 }; s.setLength (counts[juce::jlimit (0, 2, (int) pStepCount[(size_t) k]->load())], lens[juce::jlimit (0, 4, (int) pStepLen[(size_t) k]->load())]); s.setGlideMs (pStepGlide[(size_t) k]->load() * 200.0f); }
    for (int k = 0; k < kNumEnvs; ++k) rack.envs[(size_t) k].setTimes (pEnvAtk[(size_t) k]->load(), pEnvRel[(size_t) k]->load());
    rack.spatialRequested.store (pSpOn->load (std::memory_order_relaxed) >= 0.5f, std::memory_order_relaxed);
    rack.spatial.setSpeakers (pSpOut->load (std::memory_order_relaxed) >= 0.5f);
    inSm.setTarget (pIn->load (std::memory_order_relaxed)); outSm.setTarget (pOut->load (std::memory_order_relaxed));
    mixSm.setTarget (pMix->load (std::memory_order_relaxed)); bypassSm.setTarget (pBypass->load (std::memory_order_relaxed) >= 0.5f ? 1.0f : 0.0f);
}

void PhantomProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples(), ch = juce::jmin (buffer.getNumChannels(), dryBuf.getNumChannels());
    if (n == 0 || ch == 0) return;
    if (n > dryBuf.getNumSamples()) { buffer.clear(); return; }

    // one host clock for every rhythmic module
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm()) tempo.bpm = *bpm;
            if (auto ppq = pos->getPpqPosition()) tempo.ppqPosition = *ppq;
            if (auto ts = pos->getTimeSignature()) { tempo.timeSigNumerator = ts->numerator; tempo.timeSigDenominator = ts->denominator; }
            tempo.playing = pos->getIsPlaying(); tempo.looping = pos->getIsLooping();
        }
    tempo.sampleRate = sr;
    // MIDI keys C1..G1 (36..43) pick scenes 1..8; every note also goes to the rhythm units (HAUNT EDIT plays pads from 48..63) at its sample
    int ne = 0;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn() && m.getNoteNumber() >= 36 && m.getNoteNumber() <= 43) midiSceneRequest = m.getNoteNumber() - 36;
        if ((m.isNoteOn() || m.isNoteOff()) && ne < (int) noteEvents.size())
            noteEvents[(size_t) ne++] = { juce::jlimit (0, n - 1, meta.samplePosition), m.getNoteNumber(), m.isNoteOn() ? juce::jmax (0.01f, m.getFloatVelocity()) : 0.0f };
    }
    tempo.notes = noteEvents.data(); tempo.numNotes = ne;
    // quantised scene trigger: fire when the song position crosses the target grid line
    { const double tq = quantiseTargetPpq.load(); if (tq >= 0.0 && tempo.playing && tempo.ppqPosition + n / tempo.samplesPerBeat() >= tq) { quantisedFire = 1; quantiseTargetPpq = -1.0; } }

    pushHostValues();
    updateSpatial();
    juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), (size_t) ch, (size_t) n);
    juce::dsp::AudioBlock<float> dry (dryBuf); dry = dry.getSubBlock (0, (size_t) n);

    float pin[2] { 0, 0 };
    for (int i = 0; i < n; ++i) { const float g = dbToGain (inSm.next()); for (int c = 0; c < ch; ++c) { auto* x = block.getChannelPointer ((size_t) c); x[i] *= g; pin[c & 1] = juce::jmax (pin[c & 1], std::abs (x[i])); } }
    inMeter.peakL.store (pin[0]); inMeter.peakR.store (ch > 1 ? pin[1] : pin[0]);
    dry.copyFrom (block); globalDry.process (dry);

    rack.process (block, tempo);

    float pout[2] { 0, 0 };
    for (int i = 0; i < n; ++i)
    {
        const float mix = mixSm.next() * (1.0f - bypassSm.next()), g = dbToGain (outSm.next());
        for (int c = 0; c < ch; ++c)
        {
            auto* w = block.getChannelPointer ((size_t) c); const float d = dry.getChannelPointer ((size_t) c)[i];
            w[i] = d * (1.0f - mix) + w[i] * g * mix;
            pout[c & 1] = juce::jmax (pout[c & 1], std::abs (w[i]));
        }
    }
    outMeter.peakL.store (pout[0]); outMeter.peakR.store (ch > 1 ? pout[1] : pout[0]);
    // true peak (4x inter-sample) at the output
    if (tpOs)
    {
        juce::dsp::AudioBlock<float> tb (tpBuf); tb = tb.getSubBlock (0, (size_t) n); tb.copyFrom (block);
        auto up = tpOs->processSamplesUp (tb); float tp[2] { 0, 0 };
        for (int c = 0; c < ch; ++c) { auto* x = up.getChannelPointer ((size_t) c); for (size_t i = 0; i < up.getNumSamples(); ++i) tp[c & 1] = juce::jmax (tp[c & 1], std::abs (x[i])); }
        outMeter.truePeakL.store (tp[0]); outMeter.truePeakR.store (ch > 1 ? tp[1] : tp[0]);
    }
    tempo.ppqPosition += n / tempo.samplesPerBeat();
    tempo.notes = nullptr; tempo.numNotes = 0;
}

// SPATIAL: where every tile is this block (its recorded path in READ / TOUCH, else its parameters), and the recording
void PhantomProcessor::updateSpatial() noexcept
{
    const int m = spatialAuto.mode.load (std::memory_order_relaxed);
    const double ppq = tempo.ppqPosition; const bool play = tempo.playing;
    spatialPpq.store (ppq, std::memory_order_relaxed); spatialPlaying.store (play, std::memory_order_relaxed);
    juce::SpinLock::ScopedTryLockType tl (spatialAuto.lock);
    for (int u = 0; u < kNumUnits; ++u)
    {
        if (! spatialEligible (u)) continue;
        auto& view = spatialView[(size_t) u];
        const float az = pSpAz[(size_t) u]->load (std::memory_order_relaxed), el = pSpEl[(size_t) u]->load (std::memory_order_relaxed), d = pSpDist[(size_t) u]->load (std::memory_order_relaxed);
        const bool touch = spatialAuto.touching[(size_t) u].load (std::memory_order_relaxed);
        const bool rec = play && m != spOff && m != spRead && spatialAuto.armed[(size_t) u].load (std::memory_order_relaxed) && (m == spWrite || touch);
        float eaz = az, eel = el, ed = d; bool driven = false;
        if (! rec && m != spOff && ! touch)
        {
            if (tl.isLocked()) { SpPoint p; if (spatialAuto.lookup (u, ppq, tempo.beatsPerBar(), p)) { eaz = p.az; eel = p.el; ed = p.dist; driven = true; } }
            else { eaz = view.az.load (std::memory_order_relaxed); eel = view.el.load (std::memory_order_relaxed); ed = view.dist.load (std::memory_order_relaxed); driven = view.driven.load (std::memory_order_relaxed); }
        }
        if (rec) { spatialAuto.push (u, ppq, az, el, d); spRecording[(size_t) u] = true; }
        else if (spRecording[(size_t) u]) { spatialAuto.pushEnd (u); spRecording[(size_t) u] = false; }
        rack.spatial.setTarget (u, eaz, eel, ed);
        view.az.store (eaz, std::memory_order_relaxed); view.el.store (eel, std::memory_order_relaxed); view.dist.store (ed, std::memory_order_relaxed);
        view.driven.store (driven, std::memory_order_relaxed); view.recording.store (rec, std::memory_order_relaxed);
    }
}

// ---------------------------------------------------------------- state
juce::ValueTree PhantomProcessor::child (const juce::Identifier& id) const { return apvts.state.getChildWithName (id); }

void PhantomProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("version", versionString(), nullptr);
    drainSpatial();
    state.removeChild (state.getChildWithName ("SPATIAL"), nullptr); state.appendChild (spatialAuto.toTree(), nullptr);   // the recorded tile paths
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void PhantomProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
            for (auto id : { kTopology, kMods, kSteps, kScenesId, kData }) if (! apvts.state.getChildWithName (id).isValid()) apvts.state.appendChild (juce::ValueTree (id), nullptr);
            { const juce::ScopedLock sl (getCallbackLock()); readTopologyFromState(); readRoutesFromState(); readStepsFromState(); rack.applyTopology(); }
            readDataFromState();
            spatialAuto.fromTree (apvts.state.getChildWithName ("SPATIAL"));
            // a restored project starts clean: no scene request, quantised trigger or morph from before may move its parameters afterwards
            midiSceneRequest = -1; quantisedFire = -1; quantiseTargetPpq = -1.0; quantisePending = -1; morph.clear(); morphProgress = 1.0f;
            lastSceneParam = (int) std::lround (apvts.getRawParameterValue ("scene")->load());
            refreshLatency();
            topologyDirty = true; triggerAsyncUpdate();
        }
}

Topology PhantomProcessor::getTopology() const
{
    Topology t; auto c = child (kTopology);
    if (c.hasProperty ("order")) splitInts (c["order"].toString(), t.order);
    if (c.hasProperty ("lane")) splitInts (c["lane"].toString(), t.lane);
    // sanity: a permutation of 0..23 with the limiter last
    std::array<bool, kNumUnits> seen {}; bool ok = true;
    for (int v : t.order) { if (v < 0 || v >= kNumUnits || seen[(size_t) v]) { ok = false; break; } seen[(size_t) v] = true; }
    if (! ok) t = Topology();
    for (auto& l : t.lane) l = juce::jlimit (0, kLanes - 1, l);
    return t;
}
void PhantomProcessor::readTopologyFromState()
{
    const auto t = getTopology();
    rack.setTopology (t);
}
void PhantomProcessor::setTopology (const Topology& tIn, bool undoable)
{
    Topology t = tIn;
    // the limiter is always last and always on the main lane
    { int li = 0; for (int k = 0; k < kNumUnits; ++k) if (t.order[(size_t) k] == uLim) li = k; for (int k = li; k + 1 < kNumUnits; ++k) t.order[(size_t) k] = t.order[(size_t) k + 1]; t.order[kNumUnits - 1] = uLim; t.lane[uLim] = 0; }
    auto c = child (kTopology); auto* um = undoable ? &undo : nullptr;
    if (undoable) undo.beginNewTransaction ("Rack order");
    c.setProperty ("order", joinInts (t.order), um); c.setProperty ("lane", joinInts (t.lane), um);
    const juce::ScopedLock sl (getCallbackLock());
    if (rack.topology() == t) return;
    rack.requestTopology (t);   // the audio thread fades to dry; the timer completes the swap (applyTopology + latency) when it says so
}
void PhantomProcessor::moveUnit (int unit, int toIndex)
{
    auto t = getTopology(); int from = 0; for (int k = 0; k < kNumUnits; ++k) if (t.order[(size_t) k] == unit) from = k;
    toIndex = juce::jlimit (0, kNumUnits - 2, toIndex); if (unit == uLim) return;
    std::vector<int> v (t.order.begin(), t.order.end()); v.erase (v.begin() + from); v.insert (v.begin() + toIndex, unit);
    for (int k = 0; k < kNumUnits; ++k) t.order[(size_t) k] = v[(size_t) k];
    setTopology (t);
}
void PhantomProcessor::setUnitLane (int unit, int lane) { auto t = getTopology(); if (unit == uLim) return; t.lane[(size_t) unit] = juce::jlimit (0, kLanes - 1, lane); setTopology (t); }

std::vector<ModRoute> PhantomProcessor::getRoutes() const
{
    std::vector<ModRoute> v; for (auto r : child (kMods)) if (r.hasType (kRoute)) v.push_back ({ (int) r["source"], (int) r["unit"], (int) r["param"], (float) r["depth"], (bool) r.getProperty ("bipolar", true) });
    return v;
}
void PhantomProcessor::setRoutes (const std::vector<ModRoute>& v)
{
    auto c = child (kMods); undo.beginNewTransaction ("Modulation"); c.removeAllChildren (&undo);
    for (auto& r : v) { juce::ValueTree t (kRoute); t.setProperty ("source", r.source, nullptr); t.setProperty ("unit", r.unit, nullptr); t.setProperty ("param", r.param, nullptr); t.setProperty ("depth", r.depth, nullptr); t.setProperty ("bipolar", r.bipolar, nullptr); c.appendChild (t, &undo); }
    const juce::ScopedLock sl (getCallbackLock()); readRoutesFromState();
}
void PhantomProcessor::readRoutesFromState()
{
    const auto v = getRoutes(); const int n = juce::jmin ((int) v.size(), kMaxRoutes);
    for (int i = 0; i < n; ++i) rack.routes[(size_t) i] = v[(size_t) i];
    rack.numRoutes = n;
}
void PhantomProcessor::setStepValue (int seq, int step, float v)
{
    auto c = child (kSteps); const auto key = "s" + juce::String (seq) + "_" + juce::String (step); c.setProperty (key, juce::jlimit (0.0f, 1.0f, v), nullptr);
    rack.steps[(size_t) juce::jlimit (0, kNumSteps - 1, seq)].setStep (step, juce::jlimit (0.0f, 1.0f, v));
}
juce::var PhantomProcessor::getUnitData (int unit) const { return const_cast<PhantomProcessor*> (this)->rack.slot (unit).fx().getData(); }
void PhantomProcessor::setUnitData (int unit, const juce::var& v)
{
    auto& fx = rack.slot (unit).fx(); if (! fx.hasData()) return;
    fx.setData (v);
    child (kData).setProperty (unitDef (unit).key, juce::JSON::toString (fx.getData(), true), nullptr);
}
void PhantomProcessor::readDataFromState()
{
    // a unit without saved data gets its factory data (a fresh start); saved text is kept as it is (the state round trip stays exact)
    auto d = child (kData);
    for (int u = 0; u < rack.numSlots(); ++u)
    {
        auto& fx = rack.slot (u).fx(); if (! fx.hasData()) continue;
        const auto key = juce::Identifier (unitDef (u).key);
        fx.setData (d.hasProperty (key) ? juce::JSON::parse (d[key].toString()) : juce::var());
    }
}
float PhantomProcessor::getStepValue (int seq, int step) const { return (float) child (kSteps).getProperty ("s" + juce::String (seq) + "_" + juce::String (step), 0.0); }
void PhantomProcessor::readStepsFromState() { for (int s = 0; s < kNumSteps; ++s) for (int k = 0; k < StepSequencer::kMaxSteps; ++k) rack.steps[(size_t) s].setStep (k, getStepValue (s, k)); }

// every rack parameter (fx_*, lanes, macros, modulators) as normalised values + the topology
juce::ValueTree PhantomProcessor::captureState() const
{
    juce::ValueTree t (kParams);
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto id = rp->paramID;
            if (id == "in" || id == "out" || id == "bypass" || id == "os" || id.startsWith ("scene")) continue;   // never part of a scene / snapshot
            t.setProperty (id, rp->getValue(), nullptr);
        }
    auto c = child (kTopology); t.setProperty ("order", c["order"], nullptr); t.setProperty ("lane", c["lane"], nullptr);
    // (0.1.10) the drawn patterns travel with a scene / snapshot / A-B state: one JSON text for every unit that has drawn data.
    // "drawnVersion" names the format; a state without "drawn" (0.1.9 and older) leaves the drawn data as it is when recalled.
    t.setProperty ("drawn", drawnSnapshot(), nullptr); t.setProperty ("drawnVersion", 1, nullptr);
    return t;
}
juce::String PhantomProcessor::drawnSnapshot() const
{
    auto* o = new juce::DynamicObject();
    for (int u = 0; u < rack.numSlots(); ++u) { auto& fx = const_cast<PhantomProcessor*> (this)->rack.slot (u).fx(); if (fx.hasData()) o->setProperty (unitDef (u).key, fx.getData()); }
    return juce::JSON::toString (juce::var (o), true);
}
void PhantomProcessor::applyDrawnSnapshot (const juce::String& text)
{
    if (text.isEmpty()) return; const auto v = juce::JSON::parse (text); auto* o = v.getDynamicObject(); if (o == nullptr) return;
    for (int u = 0; u < rack.numSlots(); ++u) if (rack.slot (u).fx().hasData() && o->hasProperty (unitDef (u).key)) setUnitData (u, o->getProperty (unitDef (u).key));
    drawnGeneration.fetch_add (1);   // (open editors re-read their unit's drawn data)
}
void PhantomProcessor::applyCapturedState (const juce::ValueTree& t, bool topologyToo)
{
    for (int i = 0; i < t.getNumProperties(); ++i)
    {
        const auto name = t.getPropertyName (i).toString();
        if (name == "order" || name == "lane") continue;
        if (auto* p = apvts.getParameter (name)) p->setValueNotifyingHost ((float) t[t.getPropertyName (i)]);
    }
    if (t.hasProperty ("drawn")) applyDrawnSnapshot (t["drawn"].toString());
    if (topologyToo && t.hasProperty ("order")) { Topology tp; splitInts (t["order"].toString(), tp.order); splitInts (t["lane"].toString(), tp.lane); setTopology (tp, false); }
}
void PhantomProcessor::setParamNormalised (const juce::String& id, float v01) { if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v01)); }
float PhantomProcessor::getParamNormalised (const juce::String& id) const { if (auto* p = apvts.getParameter (id)) return p->getValue(); return 0.0f; }

// ---- scenes
bool PhantomProcessor::sceneFilled (int i) const { return child (kScenesId).getChildWithProperty ("index", i).isValid(); }
juce::String PhantomProcessor::sceneName (int i) const { auto s = child (kScenesId).getChildWithProperty ("index", i); return s.isValid() ? s["name"].toString() : juce::String (sceneDefaultName (i)); }
void PhantomProcessor::storeScene (int i)
{
    auto scenes = child (kScenesId); undo.beginNewTransaction ("Store scene");
    auto old = scenes.getChildWithProperty ("index", i); const auto name = old.isValid() ? old["name"].toString() : juce::String (sceneDefaultName (i));
    if (old.isValid()) scenes.removeChild (old, &undo);
    juce::ValueTree s (kScene); s.setProperty ("index", i, nullptr); s.setProperty ("name", name, nullptr); s.appendChild (captureState(), nullptr); scenes.appendChild (s, &undo);
    activeSceneIdx = i;
}
void PhantomProcessor::clearScene (int i) { auto scenes = child (kScenesId); auto s = scenes.getChildWithProperty ("index", i); if (s.isValid()) { undo.beginNewTransaction ("Clear scene"); scenes.removeChild (s, &undo); } if (activeSceneIdx == i) activeSceneIdx = -1; }
void PhantomProcessor::renameScene (int i, const juce::String& name) { auto s = child (kScenesId).getChildWithProperty ("index", i); if (s.isValid()) s.setProperty ("name", name, &undo); }
void PhantomProcessor::recallScene (int i)
{
    auto s = child (kScenesId).getChildWithProperty ("index", i); if (! s.isValid()) { storeScene (i); return; }   // an empty slot stores what you hear
    const int q = (int) pSceneQuant->load();
    if (q > 0 && tempo.playing && quantisePending < 0)
    {   // land on the next grid line: the audio thread fires quantisedFire, the timer finishes the job
        const double b = quantBeats (q), next = (std::floor (tempo.ppqPosition / b) + 1.0) * b;
        quantisePending = i; quantiseTargetPpq = next; return;
    }
    const double beats = sceneTimeBeats ((int) pSceneTime->load()), seconds = beats * 60.0 / juce::jmax (1.0, tempo.bpm);
    undo.beginNewTransaction ("Scene " + juce::String (i + 1));
    beginMorph (s.getChildWithName (kParams), seconds);
    activeSceneIdx = i;
}
void PhantomProcessor::beginMorph (const juce::ValueTree& target, double seconds)
{
    morph.clear();
    for (int k = 0; k < target.getNumProperties(); ++k)
    {
        const auto name = target.getPropertyName (k).toString(); if (name == "order" || name == "lane") continue;
        if (auto* p = apvts.getParameter (name)) morph.push_back ({ name, p->getValue(), (float) target[target.getPropertyName (k)], p->isDiscrete() || p->isBoolean() });
    }
    if (target.hasProperty ("drawn")) applyDrawnSnapshot (target["drawn"].toString());   // drawn patterns do not glide: they change at the switch point, whatever the morph time
    morphHasTopology = target.hasProperty ("order"); morphTopologyDone = false;
    if (morphHasTopology) { splitInts (target["order"].toString(), morphTopology.order); splitInts (target["lane"].toString(), morphTopology.lane); }
    morphSeconds = seconds; morphStart = juce::Time::getMillisecondCounterHiRes() * 0.001; morphProgress = 0.0f;
    timerCallback();   // first step now (a CUT lands at once)
}
void PhantomProcessor::timerCallback()
{
    // complete a topology swap the audio thread has faded out for
    if (rack.swapDue()) { { const juce::ScopedLock sl (getCallbackLock()); rack.completeSwap(); } refreshLatency(); spatialSwitchPending = false; }
    // SPATIAL: merge what the audio thread recorded; switching SPATIAL on / off changes the latency, so it goes through the topology fade
    drainSpatial();
    if (! spatialSwitchPending && (pSpOn->load() >= 0.5f) != rack.isSpatialActive() && getTotalNumOutputChannels() == 2 && rack.spatial.ready()) { spatialSwitchPending = true; rack.requestTopology (rack.topology()); }
    // scene requests from MIDI / the scene parameter
    { const int r = midiSceneRequest.exchange (-1); if (r >= 0) recallScene (r); }
    if (quantisedFire.exchange (0) == 1 && quantisePending >= 0) { const int i = quantisePending; quantisePending = -1; recallScene (i); }
    if (morph.empty()) return;
    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const float t = morphSeconds <= 0.0 ? 1.0f : (float) juce::jlimit (0.0, 1.0, (now - morphStart) / morphSeconds);
    const float e = t * t * (3.0f - 2.0f * t);   // smooth glide
    for (auto& m : morph)
        if (auto* p = apvts.getParameter (m.id)) p->setValueNotifyingHost (m.stepped ? (t >= 0.5f ? m.to : m.from) : m.from + (m.to - m.from) * e);
    if (morphHasTopology && ! morphTopologyDone && t >= 0.5f) { morphTopologyDone = true; setTopology (morphTopology, false); }
    morphProgress = t;
    if (t >= 1.0f) morph.clear();
}

// ---- A/B
void PhantomProcessor::abToggle()
{
    auto now = captureState();
    if (! abOther.isValid()) abOther = now.createCopy();   // the first click makes B a copy of A
    auto other = abOther; abOther = now; abIdx = 1 - abIdx;
    undo.beginNewTransaction ("A/B"); applyCapturedState (other, true);
}
void PhantomProcessor::abCopy() { abOther = captureState(); }
juce::var PhantomProcessor::unitToJson (int unit) const
{
    auto* o = new juce::DynamicObject(); o->setProperty ("phantom_fx_unit", unitDef (unit).key); o->setProperty ("version", versionString());
    auto* ps = new juce::DynamicObject();
    for (auto& p : rack.slot (unit).params()) if (auto* rp = apvts.getParameter (pid (unit, p.id))) ps->setProperty (p.id.c_str(), rp->convertFrom0to1 (rp->getValue()));
    if (unitDef (unit).hasMix) ps->setProperty ("mix", pMix ? apvts.getRawParameterValue (pid (unit, "mix"))->load() : 1.0f);
    ps->setProperty ("trim", apvts.getRawParameterValue (pid (unit, "trim"))->load());
    o->setProperty ("params", juce::var (ps));
    if (const_cast<PhantomProcessor*> (this)->rack.slot (unit).fx().hasData()) o->setProperty ("data", const_cast<PhantomProcessor*> (this)->rack.slot (unit).fx().getData());   // (0.1.10) the unit's drawn patterns: unit presets and unit A/B carry them
    return juce::var (o);
}
bool PhantomProcessor::unitFromJson (int unit, const juce::var& v)
{
    auto* o = v.getDynamicObject(); if (o == nullptr) return false; auto ps = o->getProperty ("params"); auto* pso = ps.getDynamicObject(); if (pso == nullptr) return false;
    undo.beginNewTransaction ("Unit preset");
    for (auto& kv : pso->getProperties()) if (auto* rp = apvts.getParameter (pid (unit, kv.name.toString().toStdString()))) rp->setValueNotifyingHost (rp->convertTo0to1 ((float) (double) kv.value));
    if (o->hasProperty ("data") && rack.slot (unit).fx().hasData()) { setUnitData (unit, o->getProperty ("data")); drawnGeneration.fetch_add (1); }   // (a unit preset from 0.1.9 has none: the drawn data stays)
    return true;
}
void PhantomProcessor::unitAbToggle (int unit)
{
    auto cur = unitToJson (unit); juce::ValueTree now ("U"); now.setProperty ("json", juce::JSON::toString (cur), nullptr);
    auto& other = unitAbOther[(size_t) unit]; if (! other.isValid()) other = now.createCopy();
    auto o = other; other = now; unitAb[(size_t) unit] = 1 - unitAb[(size_t) unit];
    unitFromJson (unit, juce::JSON::parse (o["json"].toString()));
}
void PhantomProcessor::unitAbCopy (int unit) { juce::ValueTree now ("U"); now.setProperty ("json", juce::JSON::toString (unitToJson (unit)), nullptr); unitAbOther[(size_t) unit] = now; }

// ---- presets (human-readable JSON)
juce::var PhantomProcessor::toJson (const juce::String& name, const juce::StringArray& tags) const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("phantom_fx_preset", 1); o->setProperty ("version", versionString()); o->setProperty ("name", name); o->setProperty ("tags", tags.joinIntoString (","));
    auto* ps = new juce::DynamicObject();
    for (auto* p : getParameters()) if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p)) { if (rp->paramID == "in" || rp->paramID == "out" || rp->paramID == "bypass" || rp->paramID == "scene") continue; ps->setProperty (rp->paramID, rp->convertFrom0to1 (rp->getValue())); }
    o->setProperty ("params", juce::var (ps));
    const auto t = getTopology(); juce::Array<juce::var> order, lane; for (int v : t.order) order.add (unitDef (v).key); for (int v : t.lane) lane.add (v);
    o->setProperty ("order", order); o->setProperty ("lane", lane);
    juce::Array<juce::var> routes; for (auto& r : getRoutes()) { auto* ro = new juce::DynamicObject(); ro->setProperty ("source", r.source); ro->setProperty ("unit", unitDef (r.unit).key); ro->setProperty ("param", rack.slot (r.unit).params()[(size_t) juce::jlimit (0, (int) rack.slot (r.unit).params().size() - 1, r.param)].id.c_str()); ro->setProperty ("depth", r.depth); ro->setProperty ("bipolar", r.bipolar); routes.add (juce::var (ro)); }
    o->setProperty ("mods", routes);
    o->setProperty ("drawn", juce::JSON::parse (drawnSnapshot())); o->setProperty ("drawnVersion", 1);   // (0.1.10) the drawn patterns, per unit key
    juce::Array<juce::var> steps; for (int s = 0; s < kNumSteps; ++s) { juce::Array<juce::var> a; for (int k = 0; k < StepSequencer::kMaxSteps; ++k) a.add (getStepValue (s, k)); steps.add (a); }
    o->setProperty ("steps", steps);
    juce::Array<juce::var> scenes;
    for (auto s : child (kScenesId))
    {
        auto* so = new juce::DynamicObject(); so->setProperty ("index", (int) s["index"]); so->setProperty ("name", s["name"]);
        auto* sp = new juce::DynamicObject(); auto pr = s.getChildWithName (kParams);
        for (int i = 0; i < pr.getNumProperties(); ++i) { const auto nm = pr.getPropertyName (i); if (nm.toString() == "order" || nm.toString() == "lane" || nm.toString() == "drawn" || nm.toString() == "drawnVersion") { sp->setProperty (nm, pr[nm]); continue; } if (auto* rp = apvts.getParameter (nm.toString())) sp->setProperty (nm, rp->convertFrom0to1 ((float) pr[nm])); }
        so->setProperty ("params", juce::var (sp)); scenes.add (juce::var (so));
    }
    o->setProperty ("scenes", scenes);
    return juce::var (o);
}
bool PhantomProcessor::fromJson (const juce::var& v)
{
    auto* o = v.getDynamicObject(); if (o == nullptr || ! o->hasProperty ("phantom_fx_preset")) return false;
    undo.beginNewTransaction ("Load preset");
    auto keyToUnit = [] (const juce::String& k) { for (int u = 0; u < kNumUnits; ++u) if (k == unitDef (u).key) return u; return -1; };
    if (auto* ps = o->getProperty ("params").getDynamicObject())
        for (auto& kv : ps->getProperties()) if (auto* rp = apvts.getParameter (kv.name.toString())) rp->setValueNotifyingHost (rp->convertTo0to1 ((float) (double) kv.value));
    if (auto* order = o->getProperty ("order").getArray())
    {
        Topology t; std::vector<int> seq; for (auto& k : *order) { const int u = keyToUnit (k.toString()); if (u >= 0) seq.push_back (u); }
        for (int u = 0; u < kNumUnits; ++u) if (std::find (seq.begin(), seq.end(), u) == seq.end()) seq.push_back (u);   // units a newer build added keep their default place
        for (int k = 0; k < kNumUnits; ++k) t.order[(size_t) k] = seq[(size_t) k];
        if (auto* lane = o->getProperty ("lane").getArray()) for (int k = 0; k < kNumUnits && k < lane->size(); ++k) t.lane[(size_t) k] = (int) (*lane)[k];
        setTopology (t, false);
    }
    if (auto* mods = o->getProperty ("mods").getArray())
    {
        std::vector<ModRoute> rs;
        for (auto& m : *mods) if (auto* mo = m.getDynamicObject()) { const int u = keyToUnit (mo->getProperty ("unit").toString()); if (u < 0) continue; const auto& specs = rack.slot (u).params(); int pi = -1; for (size_t i = 0; i < specs.size(); ++i) if (specs[i].id == mo->getProperty ("param").toString().toStdString()) pi = (int) i; if (pi < 0) continue;
            rs.push_back ({ (int) mo->getProperty ("source"), u, pi, (float) (double) mo->getProperty ("depth"), (bool) mo->getProperty ("bipolar") }); }
        setRoutes (rs);
    }
    if (o->hasProperty ("drawn")) applyDrawnSnapshot (juce::JSON::toString (o->getProperty ("drawn"), true));   // (a preset from 0.1.9 has none: the drawn data stays as it is)
    if (auto* steps = o->getProperty ("steps").getArray()) for (int s = 0; s < kNumSteps && s < steps->size(); ++s) if (auto* a = (*steps)[s].getArray()) for (int k = 0; k < StepSequencer::kMaxSteps && k < a->size(); ++k) setStepValue (s, k, (float) (double) (*a)[k]);
    if (auto* scenes = o->getProperty ("scenes").getArray())
    {
        auto sc = child (kScenesId); sc.removeAllChildren (&undo);
        for (auto& s : *scenes) if (auto* so = s.getDynamicObject())
        {
            juce::ValueTree st (kScene); st.setProperty ("index", (int) so->getProperty ("index"), nullptr); st.setProperty ("name", so->getProperty ("name"), nullptr);
            juce::ValueTree pr (kParams);
            if (auto* sp = so->getProperty ("params").getDynamicObject())
                for (auto& kv : sp->getProperties()) { const auto nm = kv.name.toString(); if (nm == "order" || nm == "lane" || nm == "drawn" || nm == "drawnVersion") { pr.setProperty (kv.name, kv.value, nullptr); continue; } if (auto* rp = apvts.getParameter (nm)) pr.setProperty (kv.name, rp->convertTo0to1 ((float) (double) kv.value), nullptr); }
            st.appendChild (pr, nullptr); sc.appendChild (st, &undo);
        }
    }
    return true;
}
juce::File PhantomProcessor::presetFolder() { return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("GhostBlock").getChildFile ("Phantom FX").getChildFile ("Presets"); }
juce::File PhantomProcessor::snapshotFolder() { return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("GhostBlock").getChildFile ("Phantom FX").getChildFile ("Snapshots"); }
bool PhantomProcessor::savePreset (const juce::File& f, const juce::String& name, const juce::StringArray& tags) const { f.getParentDirectory().createDirectory(); return f.replaceWithText (juce::JSON::toString (toJson (name, tags))); }
bool PhantomProcessor::loadPreset (const juce::File& f) { return fromJson (juce::JSON::parse (f)); }
void PhantomProcessor::saveSnapshot (const juce::String& name)
{
    auto f = snapshotFolder().getChildFile (juce::File::createLegalFileName (name) + ".phantomfx.json"); savePreset (f, name, { "snapshot" });
}
juce::Array<juce::File> PhantomProcessor::listSnapshots() const { return snapshotFolder().findChildFiles (juce::File::findFiles, false, "*.phantomfx.json"); }
int PhantomProcessor::numFactoryPresets() { return factory::kCount; }
juce::String PhantomProcessor::factoryPresetName (int i) { return i >= 0 && i < factory::kCount ? juce::String (factory::kBank[i].name) : juce::String(); }
juce::StringArray PhantomProcessor::factoryPresetTags (int i) { return i >= 0 && i < factory::kCount ? juce::StringArray::fromTokens (factory::kBank[i].tags, ",", "") : juce::StringArray(); }
bool PhantomProcessor::loadFactoryPreset (int i, juce::String* problem)
{
    if (i < 0 || i >= factory::kCount) return false;
    initAll(); undo.beginNewTransaction ("Factory preset");
    for (int u = 0; u < rack.numSlots(); ++u) if (rack.slot (u).fx().hasData()) setUnitData (u, juce::var());   // the factory patterns
    drawnGeneration.fetch_add (1); child (kScenesId).removeAllChildren (&undo); activeSceneIdx = -1;
    bool ok = true;
    for (auto& item : juce::StringArray::fromTokens (factory::kBank[i].set, ";", ""))
    {
        const auto t = item.trim(); if (t.isEmpty()) continue;
        const auto unit = t.upToFirstOccurrenceOf (".", false, false), control = t.fromFirstOccurrenceOf (".", false, false).upToFirstOccurrenceOf ("=", false, false).trim();
        const float value = t.fromFirstOccurrenceOf ("=", false, false).getFloatValue();
        auto* rp = apvts.getParameter (unit == "rack" ? control : pid (unit.toRawUTF8(), control.toStdString()));
        if (rp == nullptr) { ok = false; if (problem != nullptr) *problem << t << " "; continue; }
        rp->setValueNotifyingHost (rp->convertTo0to1 (value));
    }
    return ok;
}
void PhantomProcessor::initAll()
{
    undo.beginNewTransaction ("Init");
    for (auto* p : getParameters()) if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p)) if (rp->paramID != "in" && rp->paramID != "out" && rp->paramID != "bypass" && rp->paramID != "os") rp->setValueNotifyingHost (rp->getDefaultValue());
    setTopology (Topology(), false); setRoutes ({});
}

juce::AudioProcessorEditor* PhantomProcessor::createEditor() { return new PhantomEditor (*this); }
} // namespace phantom

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new phantom::PhantomProcessor(); }
