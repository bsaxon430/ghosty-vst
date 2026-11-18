// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
#include "Osc.h"

namespace mosaic
{
OscBridge::OscBridge (Engine& e, juce::AudioProcessorValueTreeState& p) : m_engine (e), m_params (p)
{
    for (auto& info : paramTable()) m_params.addParameterListener (info.id, this);
    m_audioRx.addListener (this);
    m_processRx.addListener (this);
    m_engine.onStatusMessage = [this] (const juce::String& path, const juce::StringArray& args)
    {
        if (! m_on.load()) return;
        juce::OSCMessage m (path);
        for (auto& a : args) m.addString (a);
        const juce::ScopedLock l (m_sendLock);
        m_gui.send (m);
    };
}

OscBridge::~OscBridge()
{
    m_engine.onStatusMessage = nullptr;
    for (auto& info : paramTable()) m_params.removeParameterListener (info.id, this);
    m_audioRx.disconnect(); m_processRx.disconnect();
}

juce::ValueTree OscBridge::net()
{
    auto n = m_params.state.getOrCreateChildWithName ("NET", nullptr);
    if (! n.hasProperty ("on"))
    {
        n.setProperty ("on", false, nullptr);
        n.setProperty ("gui_port", 62345, nullptr);
        n.setProperty ("audio_port", 62346, nullptr);
        n.setProperty ("process_port", 62347, nullptr);
        for (int i = 0; i < kDestinations; ++i)
        {
            n.setProperty ("dest" + juce::String (i), i == 0 ? "osc.udp://localhost" : "", nullptr);
            n.setProperty ("dest_on" + juce::String (i), false, nullptr);
        }
    }
    return n;
}

void OscBridge::apply()
{
    auto n = net();
    m_audioRx.disconnect(); m_processRx.disconnect(); m_gui.disconnect();
    for (auto& d : m_dest) { d.audio.reset(); d.process.reset(); }
    m_on = (bool) n["on"];
    if (! m_on) { m_status = "network off"; return; }
    const int ap = n["audio_port"], pp = n["process_port"], gp = n["gui_port"];
    juce::StringArray problems;
    if (! m_audioRx.connect (ap)) problems.add ("audio port " + juce::String (ap));
    if (! m_processRx.connect (pp)) problems.add ("process port " + juce::String (pp));
    m_gui.connect ("127.0.0.1", gp);
    int dests = 0;
    for (int i = 0; i < kDestinations; ++i)
    {
        if (! (bool) n["dest_on" + juce::String (i)]) continue;
        auto host = n["dest" + juce::String (i)].toString().fromFirstOccurrenceOf ("://", false, false);
        if (host.isEmpty()) continue;
        host = host.upToFirstOccurrenceOf (":", false, false);
        auto& d = m_dest[i];
        d.audio = std::make_unique<juce::OSCSender>(); d.process = std::make_unique<juce::OSCSender>();
        d.audio->connect (host == "localhost" ? "127.0.0.1" : host, ap);
        d.process->connect (host == "localhost" ? "127.0.0.1" : host, pp);
        ++dests;
    }
    m_status = problems.isEmpty() ? "listening on " + juce::String (ap) + " / " + juce::String (pp) + ", sending to " + juce::String (dests)
                                  : "couldn't open " + problems.joinIntoString (", ") + " (in use?)";
}

bool OscBridge::messageFor (const juce::String& id, float v, juce::String& path, juce::OSCArgument& arg, bool& process)
{
    // (MainWindow.h's slots: sliders send value/100 as a float, spin boxes ints)
    struct M { const char* id; const char* path; bool isInt; bool proc; };
    static const M map[] = {
        { "ratio", "/ratio", false, false }, { "n_ratio", "/n_ratio", false, false }, { "fft_start", "/fft1_start", true, false },
        { "fft_end", "/fft1_end", true, false }, { "novelty", "/novelty", false, false }, { "boredom", "/boredom", false, false },
        { "stickyness", "/stickyness", false, false }, { "stretch", "/search-stretch", true, false }, { "algo", "/search_algo", true, false },
        { "synapses", "/synapses", true, false }, { "slide_error", "/slide-error", true, false }, { "autotune", "/autotune", false, false },
        { "n_mix", "/n_mix", false, false }, { "target_mix", "/target_mix", false, false }, { "stereo", "/stereo", true, false },
        { "volume", "/volume", false, false }, { "live_input", "/mic", true, false },
        { "tgt_block", "/target_block_size", true, true }, { "tgt_overlap", "/target_overlap", false, true }, { "tgt_window", "/target_window_type", true, true },
        { "src_block", "/source_block_size", true, true }, { "src_overlap", "/source_overlap", false, true }, { "src_window", "/window_type", true, true } };
    for (auto& m : map)
        if (id == m.id)
        {
            path = m.path; process = m.proc;
            arg = m.isInt ? juce::OSCArgument ((juce::int32) std::lround (v)) : juce::OSCArgument (v);
            return true;
        }
    return false;
}

void OscBridge::parameterChanged (const juce::String& id, float v)
{
    if (! m_on.load() || m_inbound.load()) return;   // don't echo what came in
    juce::String path; juce::OSCArgument arg (0.0f); bool proc = false;
    if (! messageFor (id, v, path, arg, proc)) return;
    juce::OSCMessage m (path); m.addArgument (arg);
    send (m, proc);
}

void OscBridge::sendCommand (const juce::String& path, const juce::String& a, bool proc)
{
    if (! m_on.load()) return;
    juce::OSCMessage m (path);
    if (a.isNotEmpty()) m.addString (a);
    send (m, proc);
}

void OscBridge::send (const juce::OSCMessage& m, bool proc)
{
    const juce::ScopedLock l (m_sendLock);
    for (auto& d : m_dest)
        if (auto* s = proc ? d.process.get() : d.audio.get()) s->send (m);
}

void OscBridge::oscMessageReceived (const juce::OSCMessage& m)
{
    juce::var a0;
    if (m.size() > 0)
    {
        const auto& a = m[0];
        if (a.isFloat32()) a0 = a.getFloat32();
        else if (a.isInt32()) a0 = a.getInt32();
        else if (a.isString()) a0 = a.getString();
    }
    m_inbound = true;
    m_engine.handleOsc (m.getAddressPattern().toString(), a0);
    m_inbound = false;
}
}
