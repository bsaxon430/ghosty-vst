// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
//
// The Qt app's network side ("net" tab and settings ports), with JUCE's OSC module:
//  - listens on the audio port (62346) and process port (62347) for the same messages the
//    Qt app's engine understood (/ratio, /novelty, /load_sample, /generate_brain, ...);
//  - sends every setting you change to up to 10 other copies (Ghost Mosaic or Samplebrain)
//    at "osc.udp://host" addresses, as the net tab did;
//  - reports status (/report, /sound-item) to the gui port (62345).
// All off until NETWORK is switched on (so two copies in one project don't fight over ports).
#pragma once
#include <juce_osc/juce_osc.h>
#include "Engine.h"

namespace mosaic
{
class OscBridge : private juce::OSCReceiver::Listener<juce::OSCReceiver::RealtimeCallback>,
                  private juce::AudioProcessorValueTreeState::Listener
{
public:
    static constexpr int kDestinations = 10;
    OscBridge (Engine& e, juce::AudioProcessorValueTreeState& p);
    ~OscBridge() override;

    // settings live in the plug-in state (child "NET" of the parameter tree)
    juce::ValueTree net();
    void apply();               // (re)start with the current settings
    juce::String statusText() const { return m_status; }

    // message name + value for a setting change, in the Qt app's scaling
    static bool messageFor (const juce::String& paramId, float value, juce::String& path, juce::OSCArgument& arg, bool& process);
    void sendCommand (const juce::String& path, const juce::String& arg = {}, bool process = true);

private:
    void oscMessageReceived (const juce::OSCMessage& m) override;
    void parameterChanged (const juce::String& id, float v) override;
    void send (const juce::OSCMessage& m, bool process);

    Engine& m_engine;
    juce::AudioProcessorValueTreeState& m_params;
    juce::OSCReceiver m_audioRx, m_processRx;
    juce::OSCSender m_gui;
    struct Dest { std::unique_ptr<juce::OSCSender> audio, process; };
    Dest m_dest[kDestinations];
    juce::CriticalSection m_sendLock;
    std::atomic<bool> m_on { false }, m_inbound { false };
    juce::String m_status { "network off" };
};
}
