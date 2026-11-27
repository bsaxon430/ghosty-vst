#include "GraphSong.h"
#include "../graph/GraphStems.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace ghost
{
juce::MemoryBlock componentStateOf (const juce::MemoryBlock& hostState)
{
    juce::MemoryBlock raw;
    if (auto xml = juce::AudioProcessor::getXmlFromBinary (hostState.getData(), (int) hostState.getSize()))
        if (auto* c = xml->getChildByName ("IComponent"))
            raw.fromBase64Encoding (c->getAllSubText());
    return raw;
}

static int kindOfDrumName (const juce::String& soundName)
{
    const auto n = soundName.toLowerCase();
    if (n.contains ("open hat") || n.contains ("openhat") || n.contains ("open_hat") || n.contains ("ohat") || n.contains (" oh ") || n.startsWith ("oh ")) return DOpenHat;
    if (n.contains ("hat") || n.contains ("hh") || n.contains ("hihat")) return DHat;
    if (n.contains ("kick") || n.contains ("bd ") || n.startsWith ("bd") || n.contains ("bassdrum")) return DKick;
    if (n.contains ("clap") || n.contains ("snap")) return DClap;
    if (n.contains ("rim")) return DRim;
    if (n.contains ("snare") || n.contains ("sd ")) return DSnare;
    if (n.contains ("tom")) return DTom;
    if (n.contains ("shaker") || n.contains ("tamb")) return DShaker;
    return -1;
}

DrumMap drumMapFromBeatsState (const juce::MemoryBlock& beatsHostState, juce::String* kitText)
{
    DrumMap m; m.fill (-1);
    const auto raw = componentStateOf (beatsHostState);
    juce::ValueTree t;
    if (auto xml = juce::AudioProcessor::getXmlFromBinary (raw.getData(), (int) raw.getSize())) t = juce::ValueTree::fromXml (*xml);
    if (! t.isValid()) t = juce::ValueTree::readFromData (raw.getData(), raw.getSize());
    juce::StringArray kit;
    std::function<void (const juce::ValueTree&)> walk = [&] (const juce::ValueTree& v)
    {
        if (v.hasProperty ("sound") && v.hasProperty ("i"))
        {
            const int track = (int) v["i"];
            const auto name = v["sound"].toString();
            if (track >= 0 && track < 16 && name.isNotEmpty())
            {
                kit.add (juce::String (track + 1) + " " + name);
                const int k = kindOfDrumName (name);
                if (k >= 0 && m[(size_t) k] < 0) m[(size_t) k] = 36 + track;
            }
        }
        for (const auto& c : v) walk (c);
    };
    if (t.isValid()) walk (t);
    // kinds the kit doesn't have: the nearest one it does
    auto fill = [&] (int k, std::initializer_list<int> instead) { if (m[(size_t) k] < 0) for (int o : instead) if (m[(size_t) o] >= 0) { m[(size_t) k] = m[(size_t) o]; break; } };
    fill (DClap, { DSnare, DRim }); fill (DSnare, { DClap, DRim }); fill (DRim, { DSnare, DClap });
    fill (DOpenHat, { DHat }); fill (DHat, { DOpenHat, DShaker }); fill (DShaker, { DHat }); fill (DTom, { DSnare });
    if (m[DKick] < 0) m[DKick] = 36;   // (an unknown kit: its first track is usually the kick)
    if (kitText) *kitText = kit.joinIntoString (", ");
    return m;
}

std::unique_ptr<GraphSong> graphSongFromState (const juce::MemoryBlock& graphHostState, const DrumMap& drums)
{
    auto song = std::make_unique<GraphSong>();
    const auto raw = componentStateOf (graphHostState);
    auto xml = juce::AudioProcessor::getXmlFromBinary (raw.getData(), (int) raw.getSize());
    if (xml == nullptr) { song->error = "can't read Ghost Graph's song"; return song; }
    auto state = juce::ValueTree::fromXml (*xml);
    auto par = [&] (const char* id, float def) -> float
    {
        for (const auto& c : state) if (c.hasType ("PARAM") && c["id"].toString() == id) return (float) (double) c["value"];
        return def;
    };
    std::vector<gg::Scene> scenes;
    auto sc = state.getChildWithName ("SCENES");
    for (int i = 0; i < sc.getNumChildren(); ++i)
    {
        auto c = sc.getChild (i);
        scenes.push_back ({ "", c["text"].toString().toStdString(), (double) c.getProperty ("speed", 1.0), (double) c.getProperty ("pitch", 1.0) });
    }
    if (scenes.empty()) { song->error = "Ghost Graph has no song yet"; return song; }
    // exactly what Ghost Graph's DRAG does (GraphExport.h)
    const float r = par ("rate", 1.0f);
    const auto plan = gg::planSong (scenes, par ("chain", 1.0f) > 0.5f, (int) par ("scene", 1.0f) - 1, r < 0.5f ? 0.5 : (r < 1.5f ? 1.0 : 2.0),
                                    (int) std::lround (par ("tune", -7.0f)), par ("kit", 1.0f) > 0.5f);
    song->beats = plan.beats;
    if (plan.beats <= 0) { song->error = "Ghost Graph's song is empty"; return song; }
    const auto stems = gg::findStems (scenes, plan);
    bool leadTaken = false, arpTaken = false;
    juce::StringArray drumNames;
    for (const auto& s : stems)
    {
        int target = -1, note = -1;
        if (s.drum)
        {
            int k = -1;
            if (s.role == "Kick") k = DKick; else if (s.role == "Snare") k = DSnare; else if (s.role == "Clap") k = DClap;
            else if (s.role == "Rim") k = DRim; else if (s.role == "Hi-hat") k = DHat; else if (s.role == "Open hat") k = DOpenHat;
            else if (s.role == "Tom") k = DTom; else if (s.role == "Shaker") k = DShaker;
            if (k < 0 || drums[(size_t) k] < 0) continue;
            target = 4; note = drums[(size_t) k];
            drumNames.addIfNotAlreadyThere (juce::String::fromUTF8 (s.role.c_str()));
        }
        else
        {
            if (s.role == "Bass") target = 3;
            else if (s.role == "Pad" || s.role == "Chords") target = 0;
            else if (s.role == "Arp") { target = 1; arpTaken = true; }
            else { target = ! leadTaken ? 2 : (! arpTaken ? 1 : 2); if (target == 2) leadTaken = true; else arpTaken = true; }
            auto& parts = song->laneParts[(size_t) target];
            parts = parts.isEmpty() ? juce::String::fromUTF8 (s.name.c_str()) : parts + ", " + juce::String::fromUTF8 (s.name.c_str());
        }
        for (const auto& n : gg::stemNotes (scenes, plan, s))
        {
            GraphEvent on; on.beat = std::fmod (juce::jmax (0.0, n.start), plan.beats); on.target = target; on.note = s.drum ? note : n.note; on.velocity = n.velocity; on.on = true;
            song->events.push_back (on);
            if (! s.drum)
            {
                GraphEvent off = on; off.on = false; off.beat = std::fmod (n.start + juce::jmax (0.01, n.length * 0.98), plan.beats);
                song->events.push_back (off);
            }
        }
    }
    // offs before ons at the same moment (a repeated note restarts cleanly)
    std::stable_sort (song->events.begin(), song->events.end(), [] (const GraphEvent& a, const GraphEvent& b)
    { return a.beat < b.beat || (a.beat == b.beat && ! a.on && b.on); });
    song->drums = drumNames.joinIntoString (", ");
    return song;
}
} // namespace ghost
