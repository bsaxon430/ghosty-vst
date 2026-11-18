// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
#include "Processor.h"
#include "Editor.h"
#include "DemoKits.h"
#include "TerrainSerial.h"
#include <cstdlib>
#include <string>

namespace mosaic
{
// every live instance (newest last): the host test's hook works on the newest
static juce::CriticalSection gInstancesLock;
static std::vector<Processor*> gInstances;
void Processor::registerInstance (Processor* p, bool add)
{
    const juce::ScopedLock l (gInstancesLock);
    if (add) gInstances.push_back (p);
    else gInstances.erase (std::remove (gInstances.begin(), gInstances.end(), p), gInstances.end());
}
Processor* Processor::newestInstance()
{
    const juce::ScopedLock l (gInstancesLock);
    return gInstances.empty() ? nullptr : gInstances.back();
}

const std::vector<Preset>& factoryPresets()
{
    // our own presets (no artist names); the sounds they load are synthesised by DemoKits (CC0)
    static const std::vector<Preset> p = {
        { "Clean Start", "The original's own starting settings.", {} },
        { "Voice From Drums", "A sung line rebuilt from synth drums.",
          { { "ratio", 0.7f }, { "novelty", 0.15f }, { "boredom", 0.9f }, { "stickyness", 0.1f }, { "src_block", 1500 }, { "tgt_block", 1500 },
            { "src_overlap", 0.75f }, { "tgt_overlap", 0.75f }, { "src_window", 6 }, { "tgt_window", 6 }, { "algo", 0 } }, "drums", "vowels" },
        { "Drums From Tones", "A beat played by bells, plucks and pads.",
          { { "ratio", 0.4f }, { "novelty", 0.1f }, { "src_block", 900 }, { "tgt_block", 900 }, { "src_window", 6 }, { "tgt_window", 6 }, { "algo", 0 } }, "tones", "beat" },
        { "Piano In The Air", "A melody rebuilt from noise and air.",
          { { "ratio", 0.2f }, { "n_ratio", 0.3f }, { "src_block", 2048 }, { "tgt_block", 2048 }, { "src_window", 2 }, { "tgt_window", 2 }, { "stickyness", 0.3f } }, "noise", "piano" },
        { "Glitch Shuffle", "Repeats and jumps: novelty high, short pieces.",
          { { "stretch", 3 }, { "novelty", 0.6f }, { "boredom", 0.5f }, { "src_block", 600 }, { "tgt_block", 600 }, { "src_window", 2 }, { "tgt_window", 2 } } },
        { "Long Smears", "Sticky, long pieces that run into each other.",
          { { "stickyness", 0.8f }, { "src_block", 4096 }, { "tgt_block", 4096 }, { "src_window", 6 }, { "tgt_window", 6 }, { "algo", 2 }, { "synapses", 40 } } },
        { "Network Walk", "Wander the network: changes on every loop.",
          { { "algo", 2 }, { "synapses", 30 }, { "novelty", 0.3f }, { "src_window", 6 }, { "tgt_window", 6 } } },
        { "Slide Warp", "Waits for a close match: timing bends.",
          { { "algo", 3 }, { "slide_error", 400 }, { "synapses", 60 }, { "src_window", 6 }, { "tgt_window", 6 } } },
        { "Opposites", "Picks the least similar piece every time.",
          { { "algo", 1 }, { "src_window", 6 }, { "tgt_window", 6 } } },
        { "Pitch Follow", "Pieces bend towards the target's pitch.",
          { { "autotune", 0.3f }, { "n_ratio", 0.5f }, { "n_mix", 0.3f }, { "src_window", 6 }, { "tgt_window", 6 } } },
        { "Ghost Blend", "A little of the target left in.",
          { { "target_mix", 0.35f }, { "src_window", 6 }, { "tgt_window", 6 } } },
    };
    return p;
}

juce::AudioProcessor::BusesProperties Processor::buses()
{
#if GHOSTMOSAIC_FX
    return juce::AudioProcessor::BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true).withOutput ("Output", juce::AudioChannelSet::stereo(), true);
#else
    return juce::AudioProcessor::BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true);
#endif
}

Processor::Processor()
    : juce::AudioProcessor (buses()),
      m_params (*this, nullptr, "GHOSTMOSAIC", makeLayout (GHOSTMOSAIC_FX != 0))
{
    m_history = std::make_unique<SettingsHistory> (m_params);
    m_history->reset();
    m_engine = std::make_unique<Engine> (m_params, GHOSTMOSAIC_FX != 0);
    m_osc = std::make_unique<OscBridge> (*m_engine, m_params);
    m_osc->net();
    m_slotA = m_params.copyState();
    terrain.onChange = [this] { pushTerrain(); };
    m_engine->onTerrainLoaded = [this] (const juce::String& st) { applyTerrainState (st); };
    applyTerrainState ({});
    startTimerHz (20);
    registerInstance (this, true);
    if (preloadNewInstances)
    {
        // open ready to play: a few built-in sounds and the built-in target (only what it starts with;
        // the settings are the original's own; a saved session replaces all of it)
        loadKit ("drums");
        if (! isEffect()) loadDemoTarget ("vowels");
        m_engine->setStatus (isEffect() ? "Built-in sounds loaded: press PLAY on your track, or drop your own sounds."
                                        : "Built-in sounds loaded: press GENERATE to hear them, or drop your own.");
    }
}

juce::String Processor::terrainStateString() const
{
    auto v = terrain.stateToVar();
    if (auto* o = v.getDynamicObject()) o->setProperty ("mosaicOn", m_terrainOn);
    return juce::JSON::toString (v, true);
}

void Processor::pushTerrain()
{
    m_engine->setTerrain (m_terrainOn ? terrain.terrain() : ghost::terrain::flatTerrain(), terrainStateString());
}

void Processor::setTerrainOn (bool on)
{
    if (on == m_terrainOn) return;
    m_terrainOn = on;
    pushTerrain();
}

void Processor::applyTerrainState (const juce::String& json)
{
    const auto v = json.isNotEmpty() ? juce::JSON::parse (json) : juce::var();
    m_terrainOn = v.isObject() ? (bool) v.getProperty ("mosaicOn", true) : true;
    if (! terrain.stateFromVar (v))
    {
        // flat in both slots (a new instance, or a session from before build 9)
        auto* o = new juce::DynamicObject();
        o->setProperty ("a", ghost::terrain::toVar (ghost::terrain::flatTerrain()));
        o->setProperty ("b", ghost::terrain::toVar (ghost::terrain::flatTerrain()));
        o->setProperty ("active", 0);
        terrain.stateFromVar (juce::var (o));
    }
}

Processor::~Processor()
{
    registerInstance (this, false);
    stopTimer();
    m_osc.reset();
    m_engine.reset();
}

bool Processor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
#if GHOSTMOSAIC_FX
    const auto in = l.getMainInputChannelSet();
    if (in != juce::AudioChannelSet::stereo() && in != juce::AudioChannelSet::mono()) return false;
#endif
    return true;
}

void Processor::prepareToPlay (double sr, int block)
{
    m_engine->prepare (sr, block);
    m_reportedLatency = m_engine->latencySamples();
    setLatencySamples (m_reportedLatency);
}

void Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    bool playing = false, valid = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            playing = pos->getIsPlaying(); valid = true;
            const auto bpm = pos->getBpm(); const auto ppq = pos->getPpqPosition();
            m_engine->setHostTempo (bpm ? *bpm : 0.0, ppq ? *ppq : 0.0, ppq.hasValue() && playing);
        }
#if ! GHOSTMOSAIC_FX
    buffer.clear();
#else
    {
        float peak = 0;
        for (int c = 0; c < buffer.getNumChannels(); ++c)
        {
            const auto mm = juce::FloatVectorOperations::findMinAndMax (buffer.getReadPointer (c), buffer.getNumSamples());
            peak = juce::jmax (peak, mm.getEnd(), -mm.getStart());
        }
        uiInputLevel.store (juce::jmax (peak, uiInputLevel.load() * 0.9f));
    }
#endif
    m_engine->process (buffer, midi, playing, valid);
    midi.clear();
}

void Processor::timerCallback()
{
    m_history->poll();
    m_engine->refreshTerrain();
    const int lat = m_engine->latencySamples();
    if (lat != m_reportedLatency) { m_reportedLatency = lat; setLatencySamples (lat); }
}

void Processor::getStateInformation (juce::MemoryBlock& dest) { m_engine->getState (dest); }

void Processor::setStateInformation (const void* data, int size)
{
    m_engine->setState (data, size);
    applyTerrainState (m_engine->terrainState());
    m_osc->apply();
    m_slotA = m_params.copyState();
    m_abB = false;
    m_history->reset();
}

void Processor::setParam (const char* id, float v, bool undoable)
{
    if (auto* p = m_params.getParameter (id))
    {
        if (undoable) m_history->beginNewTransaction();
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (v));
        p->endChangeGesture();
    }
}

float Processor::getParam (const char* id) const
{
    if (auto* r = m_params.getRawParameterValue (id)) return r->load();
    return paramInfo (id).def;
}

void Processor::mutate (juce::uint32 seed)
{
    juce::Random r (seed != 0 ? (juce::int64) seed : juce::Time::currentTimeMillis());
    m_history->beginNewTransaction ("Mutate");
    auto nudge = [&] (const char* id, float spread, float lo, float hi)
    {
        const float v = juce::jlimit (lo, hi, getParam (id) + (r.nextFloat() * 2 - 1) * spread);
        if (auto* p = m_params.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
    };
    nudge ("ratio", 0.35f, 0, 1);
    nudge ("novelty", 0.3f, 0, 0.85f);
    nudge ("boredom", 0.2f, 0.3f, 0.99f);
    nudge ("stickyness", 0.3f, 0, 0.9f);
    if (r.nextFloat() < 0.35f) nudge ("stretch", 2.0f, 1, 4);
    if (r.nextFloat() < 0.25f) nudge ("n_ratio", 0.3f, 0, 1);
    if (r.nextFloat() < 0.2f) { if (auto* p = m_params.getParameter ("algo")) p->setValueNotifyingHost (p->convertTo0to1 ((float) r.nextInt (4))); }
    m_engine->restart();
    uiShockwave.fetch_add (1);
}

// ------------------------------------------------------------------ settings history
std::vector<float> SettingsHistory::capture() const
{
    std::vector<float> v;
    for (auto& info : paramTable())
        if (auto* r = m_params.getRawParameterValue (info.id)) v.push_back (r->load());
    return v;
}

void SettingsHistory::restore (const std::vector<float>& v)
{
    size_t k = 0;
    for (auto& info : paramTable())
        if (auto* prm = m_params.getParameter (info.id))
        {
            if (k < v.size()) { prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->convertTo0to1 (v[k])); prm->endChangeGesture(); }
            ++k;
        }
    m_base = m_last = capture();
}

// two sets of settings are the same if no setting moved by more than a hair (a knob that snaps a
// value to its step, or a float round trip, is not a change of yours)
static bool sameSettings (const std::vector<float>& a, const std::vector<float>& b)
{
    if (a.size() != b.size()) return false;
    size_t k = 0;
    for (auto& info : paramTable())
    {
        if (k >= a.size()) break;
        if (std::abs (a[k] - b[k]) > 1e-4f * (info.max - info.min)) return false;
        ++k;
    }
    return true;
}

bool SettingsHistory::canUndo() const { return ! m_undo.empty() || ! sameSettings (capture(), m_base); }

void SettingsHistory::commit()
{
    const auto cur = capture();
    if (m_base.empty()) { m_base = m_last = cur; return; }
    if (sameSettings (cur, m_base)) return;
    m_undo.push_back (m_base);
    if (m_undo.size() > 200) m_undo.erase (m_undo.begin());
    m_redo.clear();
    m_base = m_last = cur;
}

void SettingsHistory::poll()
{
    const auto cur = capture();
    const double now = juce::Time::getMillisecondCounterHiRes();
    if (cur != m_last) { m_last = cur; m_lastChange = now; }
    else if (! sameSettings (cur, m_base) && now - m_lastChange > 350) commit();
}

void SettingsHistory::undo()
{
    commit();
    if (m_undo.empty()) return;
    auto v = m_undo.back(); m_undo.pop_back();
    m_redo.push_back (m_base);
    restore (v);
}

void SettingsHistory::redo()
{
    commit();
    if (m_redo.empty()) return;
    auto v = m_redo.back(); m_redo.pop_back();
    m_undo.push_back (m_base);
    restore (v);
}

void Processor::storeAB() { (m_abB ? m_slotB : m_slotA) = m_params.copyState(); }

void Processor::switchAB()
{
    m_history->beginNewTransaction ("A / B");
    storeAB();
    m_abB = ! m_abB;
    auto& other = m_abB ? m_slotB : m_slotA;
    if (! other.isValid()) other = m_params.copyState();
    // only the settings change: sounds stay; the NET child and the state's sounds are left alone
    for (auto& info : paramTable())
        if (auto* p = m_params.getParameter (info.id))
            for (auto c : other)
                if (c.hasType ("PARAM") && c["id"].toString() == info.id) p->setValueNotifyingHost (p->convertTo0to1 ((float) c["value"]));
}

void Processor::applyPreset (int index)
{
    auto& ps = factoryPresets();
    if (index < 0 || index >= (int) ps.size()) return;
    auto& pr = ps[(size_t) index];
    m_history->beginNewTransaction ("Preset");
    for (auto& info : paramTable())
    {
        if (juce::String (info.id) == "live_input" || juce::String (info.id) == "dry" || juce::String (info.id) == "play_mode" || juce::String (info.id) == "limiter" || juce::String (info.id) == "declick") continue;   // (your extras are yours, not the preset's)
        float v = info.def;
        for (auto& kv : pr.values) if (juce::String (kv.first) == info.id) v = kv.second;
        if (auto* p = m_params.getParameter (info.id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
    }
    if (pr.kit != nullptr) { m_engine->clearSounds(); loadKit (pr.kit); }
    if (pr.target != nullptr && ! isEffect()) loadDemoTarget (pr.target);
    m_engine->generateAll (true);
    uiShockwave.fetch_add (1);
}

void Processor::generate()
{
    auto& e = *m_engine;
    if (e.sounds().empty() && ! e.busy()) { e.setStatus ("Add some sounds to SOURCE SOUNDS first (or pick a preset)."); return; }
    if (! isEffect() && e.targetName().isEmpty())
    {
        loadDemoTarget ("vowels");
        e.setNotice ("No target loaded, so GENERATE uses the built-in sung vowels. Drop your own sound on TARGET to rebuild that instead.");
    }
    e.generateAll (true);
    uiShockwave.fetch_add (1);
}

juce::String Processor::stateLine() const
{
    auto& e = *m_engine;
    const auto b = e.buildInfo();
    const bool pending = e.playPending();
    if (b.phase > 0)
    {
        juce::String s = "Building library... " + juce::String ((int) (b.fraction * 100)) + "%";
        if (b.eta >= 0) s << "  -  " << Engine::timeWords (b.eta) << " left";
        if (pending) s << ", then it plays";
        return s;
    }
    if (e.busy()) return pending ? "Getting ready to play..." : "Working...";
    const auto snd = e.sounds();
    const auto lib = e.libraryPicture();
    const int mode = (int) std::lround (getParam ("play_mode"));
    if (snd.empty()) return "Silent: no source sounds. Drop sounds on SOURCE SOUNDS (or pick a preset).";
    if (! isEffect() && e.targetName().isEmpty()) return "Silent: no target loaded. GENERATE uses the built-in one.";
    if (lib == nullptr || lib->numBlocks == 0) return "Silent: the library isn't built yet. Press GENERATE.";
    if (e.renderingNow())
    {
        if (getParam ("volume") < 0.005f) return "Playing, but VOLUME is at 0.";
        return isEffect() ? "Playing: your track, rebuilt from your sounds." : "Playing";
    }
    if (mode == 1) return "Silent: PLAY BY is MIDI keys. Hold a note to play.";
    if (mode == 2) return e.hostSaysTransport() ? "Silent: PLAY BY is DAW play. Press play in your DAW."
                                                : "Silent: PLAY BY is DAW play, and your DAW doesn't say it is playing.";
    return isEffect() ? "Ready. Press PLAY." : "Ready. Press GENERATE or PLAY.";
}

void Processor::loadKit (const juce::String& which)
{
    const double sr = m_engine->sampleRate() > 0 ? m_engine->sampleRate() : 44100;
    for (auto& s : demo::kit (which, sr)) m_engine->addMemorySound (s.name, s.audio, sr);
}

void Processor::loadDemoTarget (const juce::String& which)
{
    const double sr = m_engine->sampleRate() > 0 ? m_engine->sampleRate() : 44100;
    if (which == "beat") m_engine->setTargetMemory ("synth beat", demo::beatLoop (sr), sr);
    else if (which == "vowels") m_engine->setTargetMemory ("sung vowels", demo::vowelPhrase (sr), sr);
    else m_engine->setTargetMemory ("plucked melody", demo::pianoLine (sr), sr);
}

juce::File Processor::userFolder() const
{
    auto f = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("GhostBlock").getChildFile ("Ghost Mosaic");
    f.createDirectory();
    return f;
}

juce::File Processor::nextRecordingFile() const
{
    auto dir = userFolder().getChildFile ("Recordings");
    dir.createDirectory();
    juce::File f;
    do f = dir.getChildFile ("mosaic-" + juce::String (m_recordId++) + ".wav"); while (f.exists());   // the Qt app's "<name>-<n>"
    return f;
}

juce::AudioProcessorEditor* Processor::createEditor() { return new Editor (*this); }
}

#if ! GHOSTMOSAIC_TESTS
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new mosaic::Processor(); }
#endif

// The host test's hook (tests/HostTest.cpp). A host can only reach a plug-in through parameters, MIDI,
// audio and saved state, so the test, which loads the real VST3 / AU like FL Studio does, uses this to do
// what your hands do on the screen: drop sounds on SOURCE SOUNDS and TARGET, and press GENERATE. With the
// screen open it goes through the screen's own drop areas and the GENERATE button itself.
// It does nothing unless the environment variable GHOSTMOSAIC_HOST_TEST is set (only the test sets it).
// (It lives here, not in a file of its own: the plug-in links its code from a library, and a file nothing
//  calls would be left out.)
extern "C" __attribute__ ((visibility ("default"))) const char* ghostmosaic_host_test (const char* cmd, const char* arg)
{
    static thread_local std::string out;
    out.clear();
    if (std::getenv ("GHOSTMOSAIC_HOST_TEST") == nullptr || cmd == nullptr) return out.c_str();
    auto* p = mosaic::Processor::newestInstance();
    if (p == nullptr) { out = "no instance"; return out.c_str(); }
    const juce::String c (cmd), a (arg != nullptr ? arg : "");
    auto* ed = dynamic_cast<mosaic::Editor*> (p->getActiveEditor());
    auto& e = p->engine();
    if (c == "clear") { e.clearSounds(); if (! p->isEffect()) e.clearTarget(); out = "ok"; }
    else if (c == "sources")
    {
        const auto files = juce::StringArray::fromLines (a);
        if (ed != nullptr) ed->sources().filesDropped (files, 10, 10); else e.addFiles (files);
        out = ed != nullptr ? "dropped on SOURCE SOUNDS" : "added (no screen)";
    }
    else if (c == "target")
    {
        if (ed != nullptr) ed->target().filesDropped (juce::StringArray (a), 10, 10); else e.setTargetFile (juce::File (a));
        out = ed != nullptr ? "dropped on TARGET" : "set (no screen)";
    }
    else if (c == "generate")
    {
        if (auto* b = ed != nullptr ? dynamic_cast<juce::Button*> (ed->findById ("generate")) : nullptr) { b->triggerClick(); out = "clicked GENERATE"; }
        else { p->generate(); out = "generate (no screen)"; }
    }
    else if (c == "set") { p->setParam (a.upToFirstOccurrenceOf ("=", false, false).toRawUTF8(), a.fromFirstOccurrenceOf ("=", false, false).getFloatValue()); out = "ok"; }
    else if (c == "state") out = p->stateLine().toStdString();
    else if (c == "status") out = e.status().toStdString();
    else if (c == "notice") out = e.notice().toStdString();
    else if (c == "stop") { e.pause(); out = "ok"; }
    else if (c == "pending") out = e.playPending() ? "1" : "0";
    else if (c == "busy") out = e.busy() ? "1" : "0";
    else if (c == "sounds") out = std::to_string (e.sounds().size()) + " sounds, target '" + e.targetName().toStdString() + "'";
    else if (c == "editor") out = ed != nullptr ? "1" : "0";
    else out = "unknown command";
    return out.c_str();
}
