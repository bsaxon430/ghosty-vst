// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Processor.h"
#include "Editor.h"

namespace atlas
{
Library& Shared::library() { std::call_once (libOnce, [this] { lib.load(); }); return lib; }
PresetBank& Shared::bank() { std::call_once (bankOnce, [this] { const juce::ScopedLock sl (bankLock); presets.refresh(); }); return presets; }

Processor::Processor()
    : juce::AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    auto add = [this] (const juce::String& id, const juce::String& name, float lo, float hi, float def, const juce::String& unit) {
        auto* p = new juce::AudioParameterFloat (juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (lo, hi), def,
                                                 juce::AudioParameterFloatAttributes().withLabel (unit));
        addParameter (p);
        host.push_back (p);
    };
    add ("master", "Master", -60.0f, 12.0f, 0.0f, "dB");
    for (int i = 0; i < kMacros; ++i) add ("macro" + juce::String (i + 1), "Macro " + juce::String (i + 1), 0.0f, 1.0f, 0.0f, "");
    for (int l = 0; l < kLayers; ++l) add ("level" + juce::String (l + 1), "Layer " + juce::String (l + 1) + " Level", -60.0f, 12.0f, 0.0f, "dB");
    pushed.fill (-1.0e9f);
    for (auto& h : heldBits) h.store (0);

    eng.setResolver ([this] (const juce::String& id) { return library().sfzPath (id); });

    // a new instance opens on the first sound of the factory set: one note should already be worth hearing
    Preset first;
    bool have = false;
    {
        auto& b = bank();
        const juce::ScopedLock sl (bankLock());
        if (! b.list().empty()) have = b.load (b.list().front(), first);
    }
    if (have) { eng.setPreset (first); currentFile = first.file; followPreset (first); }
    startTimerHz (8);
}

Processor::~Processor() { stopTimer(); }

bool Processor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() && layouts.getMainInputChannelSet().isDisabled();
}

void Processor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    eng.prepare (sr);
    tempo.sampleRate = sr;
    if (eng.latencySamples() != getLatencySamples()) setLatencySamples (eng.latencySamples());
}

void Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n <= 0) { midi.clear(); return; }

    // the host's own controls (automation arrives on this thread): only what moved is passed on
    for (int i = 0; i < kHostParams; ++i)
    {
        const float v = host[(size_t) i]->get();
        if (v == pushed[(size_t) i]) continue;
        pushed[(size_t) i] = v;
        if (i == hpMaster) eng.hostMasterDb (v);
        else if (i < hpLevel1) eng.hostMacro (i - hpMacro1, v);
        else eng.hostLayerGain (i - hpLevel1, v);
    }

    // an export runs faster than real time: the engine is told, so that every note waits for its recordings
    eng.setHostOffline (isNonRealtime());

    // one clock for every rhythmic effect: the host's while it plays, free-running when it is stopped
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto bpm = pos->getBpm()) tempo.bpm = *bpm;
            if (auto ts = pos->getTimeSignature()) { tempo.timeSigNumerator = ts->numerator; tempo.timeSigDenominator = ts->denominator; }
            tempo.playing = pos->getIsPlaying(); tempo.looping = pos->getIsLooping();
            if (tempo.playing) if (auto ppq = pos->getPpqPosition()) tempo.ppqPosition = *ppq;
        }
    tempo.sampleRate = sr;
    // while the host plays or exports, layers are lined up with the slowest of them (see Engine::setOnTimeline)
    eng.setOnTimeline (tempo.playing || isNonRealtime());

    // which keys are down, for the screen's keyboard
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn()) { const int k = m.getNoteNumber(); heldBits[(size_t) (k >> 5)].fetch_or (1u << (k & 31), std::memory_order_relaxed); }
        else if (m.isNoteOff()) { const int k = m.getNoteNumber(); heldBits[(size_t) (k >> 5)].fetch_and (~(1u << (k & 31)), std::memory_order_relaxed); }
        else if (m.isAllNotesOff() || m.isAllSoundOff()) for (auto& h : heldBits) h.store (0, std::memory_order_relaxed);
    }

    if (buffer.getNumChannels() >= 2)
    {
        float* chans[2] = { buffer.getWritePointer (0), buffer.getWritePointer (1) };
        juce::AudioBuffer<float> view (chans, 2, n);
        eng.process (view, midi, tempo);
        for (int c = 2; c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, n);
    }
    else buffer.clear();
    tempo.ppqPosition += (double) n / tempo.samplesPerBeat();
    midi.clear();
}

void Processor::timerCallback()
{
    const int lat = eng.latencySamples();
    if (lat != getLatencySamples()) setLatencySamples (lat);
}

// ============================================================================================ presets
void Processor::followPreset (const Preset& p)
{
    auto put = [this] (int i, float v) {
        auto* prm = host[(size_t) i];
        const float nv = prm->convertTo0to1 (juce::jlimit (prm->range.start, prm->range.end, v));
        if (std::abs (prm->convertTo0to1 (prm->get()) - nv) > 1.0e-7f) prm->setValueNotifyingHost (nv);
    };
    put (hpMaster, p.masterDb);
    for (int i = 0; i < kMacros; ++i) put (hpMacro1 + i, p.macros[(size_t) i].value);
    for (int l = 0; l < kLayers; ++l) put (hpLevel1 + l, p.layers[(size_t) l].p[lpGain]);
}

void Processor::loadPreset (const Preset& p)
{
    eng.setPreset (p);
    followPreset (p);
    currentFile = p.file;
    sendChangeMessage();
}

bool Processor::loadPresetFile (const juce::String& file)
{
    Preset p;
    {
        auto& b = bank();
        const juce::ScopedLock sl (bankLock());
        if (! b.load (file, p)) return false;
        // (pictures of the screen taken by the build are not something the owner played)
        if (juce::SystemStats::getEnvironmentVariable ("GHOST_ATLAS_NO_RECENTS", {}).isEmpty()) b.touchRecent (file);
    }
    loadPreset (p);
    return true;
}

Preset Processor::capture() const
{
    Preset p = eng.getPreset();
    // the host's controls are the truth for what they cover (they may have moved while no audio was running)
    p.masterDb = host[hpMaster]->get();
    for (int i = 0; i < kMacros; ++i) p.macros[(size_t) i].value = host[(size_t) (hpMacro1 + i)]->get();
    for (int l = 0; l < kLayers; ++l) p.layers[(size_t) l].p[lpGain] = host[(size_t) (hpLevel1 + l)]->get();
    p.file = currentFile;
    return p;
}

void Processor::setHost (int index, float value)
{
    if (index < 0 || index >= kHostParams) return;
    auto* prm = host[(size_t) index];
    value = juce::jlimit (prm->range.start, prm->range.end, value);
    prm->setValueNotifyingHost (prm->convertTo0to1 (value));
    // (the audio thread passes it on; this keeps the saved sound right when no audio is running)
    if (index == hpMaster) eng.setMasterDb (value);
    else if (index < hpLevel1) eng.setMacro (index - hpMacro1, value);
    else eng.setLayerParam (index - hpLevel1, lpGain, value);
}
float Processor::getHost (int index) const { return index >= 0 && index < kHostParams ? host[(size_t) index]->get() : 0.0f; }
void Processor::beginGesture (int index) { if (index >= 0 && index < kHostParams) host[(size_t) index]->beginChangeGesture(); }
void Processor::endGesture (int index) { if (index >= 0 && index < kHostParams) host[(size_t) index]->endChangeGesture(); }

// ============================================================================================ the project
void Processor::getStateInformation (juce::MemoryBlock& dest)
{
    auto o = newObject();
    o->setProperty ("format", "ghost-atlas-state"); o->setProperty ("version", 1);
    o->setProperty ("preset", capture().toVar());
    o->setProperty ("presetFile", currentFile);
    if (! uiState.isVoid()) o->setProperty ("ui", uiState);
    const auto text = juce::JSON::toString (juce::var (o.get()), true);
    dest.replaceAll (text.toRawUTF8(), text.getNumBytesAsUTF8());
}

void Processor::setStateInformation (const void* data, int size)
{
    if (data == nullptr || size <= 0) return;
    const auto v = parseJson (juce::String::fromUTF8 (static_cast<const char*> (data), size));
    if (! v.isObject() || prop (v, "format").toString() != "ghost-atlas-state") return;
    auto p = Preset::fromVar (prop (v, "preset"));
    p.file = prop (v, "presetFile").toString();
    p.factory = p.file.startsWith ("factory:");
    uiState = prop (v, "ui");
    loadPreset (p);
}

juce::AudioProcessorEditor* Processor::createEditor() { return new Editor (*this); }
} // namespace atlas
