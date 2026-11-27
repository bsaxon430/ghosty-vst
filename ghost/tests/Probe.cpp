// probe: one synth alone (GHOST_PROBE_BUNDLE, default Ghosty Pro 2): its sound before / after GHOST hands it a sound
#include "HostedPlugin.h"
#include "Catalog.h"
#include <cstdio>
using namespace ghost;
struct PH : juce::AudioPlayHead { juce::Optional<PositionInfo> info; juce::Optional<PositionInfo> getPosition() const override { return info; } };
static float play (juce::AudioPluginInstance& p, PH& ph, int note, int blocks = 200)
{
    juce::AudioBuffer<float> b (juce::jmax (2, juce::jmax (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels())), 512);
    float pk = 0;
    for (int i = 0; i < blocks; ++i)
    {
        juce::AudioPlayHead::PositionInfo in; in.setIsPlaying (false); in.setBpm (120); ph.info = in;
        juce::MidiBuffer m; if (i == 2) m.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0); if (i == blocks - 40) m.addEvent (juce::MidiMessage::noteOff (1, note), 0);
        b.clear(); p.processBlock (b, m);
        for (int c = 0; c < b.getNumChannels(); ++c) pk = juce::jmax (pk, b.getMagnitude (c, 0, 512));
        if (i % 20 == 0) juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
    }
    return pk;
}
int main()
{
    std::setvbuf (stdout, nullptr, _IONBF, 0);
    juce::ScopedJuceInitialiser_GUI init;
    auto bundle = juce::SystemStats::getEnvironmentVariable ("GHOST_PROBE_BUNDLE", "Ghosty Pro 2.vst3");
    juce::String e; auto p = Hosted::open (bundle, 48000, 512, e);
    if (! p) { std::printf ("can't open %s: %s\n", bundle.toRawUTF8(), e.toRawUTF8()); return 1; }
    std::printf ("%s: in %d out %d, buses out %d, latency %d\n", p->getName().toRawUTF8(), p->getTotalNumInputChannels(), p->getTotalNumOutputChannels(), p->getBusCount (false), p->getLatencySamples());
    PH ph; p->setPlayHead (&ph);
    juce::MessageManager::getInstance()->runDispatchLoopUntil (500);
    std::printf ("default sound, note 60: peak %.4f\n", play (*p, ph, 60));
    juce::MemoryBlock own; p->getStateInformation (own);
    auto xml = juce::AudioProcessor::getXmlFromBinary (own.getData(), (int) own.getSize());
    if (xml) { for (auto* c : xml->getChildIterator()) { juce::MemoryBlock mb; mb.fromBase64Encoding (c->getAllSubText()); std::printf ("  own state %s: %d bytes, starts: %s\n", c->getTagName().toRawUTF8(), (int) mb.getSize(), mb.toString().substring (0, 80).replaceCharacters ("\n\r", "  ").toRawUTF8()); } }
    juce::StringArray ids;
    for (auto& s : Catalog::factorySounds())
        if (juce::String (engineBundle (s.engine)) == bundle && ids.size() < 3) ids.add (s.id);
    for (auto id : ids)
    {
        Sound s; if (! Catalog::get().find (id, s)) continue;
        juce::MemoryBlock st; Catalog::hostState (s, st, e);
        p->setStateInformation (st.getData(), (int) st.getSize());
        juce::MessageManager::getInstance()->runDispatchLoopUntil (500);
        std::printf ("%s, note 48: peak %.4f\n", id.toRawUTF8(), play (*p, ph, 48));
        juce::MemoryBlock after; p->getStateInformation (after);
        auto x2 = juce::AudioProcessor::getXmlFromBinary (after.getData(), (int) after.getSize());
        if (x2) if (auto* c = x2->getChildByName ("IComponent")) { juce::MemoryBlock mb; mb.fromBase64Encoding (c->getAllSubText()); const auto t = mb.toString(); const int k = t.indexOf ("preset_name"); std::printf ("  after: %d bytes, %s\n", (int) mb.getSize(), t.substring (k, k + 60).toRawUTF8()); }
    }
    // the same with GHOST's state AND its own controller part
    return 0;
}
