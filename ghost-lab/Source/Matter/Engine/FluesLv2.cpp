// Ghost Matter - AGPL-3.0-only (see LICENSE, NOTICE.md). The LV2 host glue for Flues' own plug-in code (FluesLv2.h).
#include "FluesLv2.h"
#include <lv2/core/lv2.h>
#include <lv2/atom/atom.h>
#include <lv2/atom/util.h>
#include <lv2/midi/midi.h>
#include <lv2/urid/urid.h>
#include <cstring>

// each vendored plug-in file's lv2_descriptor, renamed at compile time (CMake: -Dlv2_descriptor=...)
extern "C" const LV2_Descriptor* flues_pmsynth_descriptor (uint32_t);
extern "C" const LV2_Descriptor* flues_chatterbox_descriptor (uint32_t);
extern "C" const LV2_Descriptor* flues_disyn_descriptor (uint32_t);

namespace matter::fluesplug
{
namespace
{
struct Port { const char* sym; float def; };
// port index order and defaults, as in each plug-in's .ttl (third_party/flues/lv2/*.ttl; a test checks them)
const std::vector<Port> kPm = { { "audio_out", 0 }, { "midi_in", 0 }, { "dcLevel", 0.5f }, { "noiseLevel", 0.15f }, { "toneLevel", 0.0f },
    { "attack", 0.3333333333f }, { "release", 0.2821702825f }, { "interfaceType", 2 }, { "interfaceIntensity", 0.5f }, { "tuning", 0.5f },
    { "ratio", 0.5f }, { "delay1Feedback", 0.9595959596f }, { "delay2Feedback", 0.9595959596f }, { "filterFeedback", 0.0f },
    { "filterFrequency", 0.5663233348f }, { "filterQ", 0.1879018247f }, { "filterShape", 0.0f }, { "lfoFrequency", 0.7383519587f },
    { "modulationTypeLevel", 0.5f }, { "reverbSize", 0.5f }, { "reverbLevel", 0.3f } };
const std::vector<Port> kCb = { { "audio_out", 0 }, { "midi_in", 0 }, { "pitch", 0.30f }, { "voiced", 1.0f }, { "aspirated", 0.0f },
    { "noise_level", 0.2f }, { "f1", 0.5f }, { "f2", 0.4f }, { "f3", 0.5f }, { "f4", 0.5f }, { "nasal", 0.0f }, { "sing", 0.0f },
    { "shout", 0.0f }, { "fry", 0.0f }, { "stress", 0.3f }, { "attack", 0.33f }, { "release", 0.33f }, { "reverb_size", 0.3f },
    { "reverb_level", 0.2f }, { "master_gain", 0.8f } };
const std::vector<Port> kDs = { { "audio_out_l", 0 }, { "audio_out_r", 0 }, { "midi_in", 0 }, { "algorithmType", 3 }, { "param1", 0.55f },
    { "param2", 0.5f }, { "param3", 0.0f }, { "attack", 0.5f }, { "release", 0.5f }, { "reverbSize", 0.5f }, { "reverbLevel", 0.3f },
    { "masterGain", 0.8f } };

const std::vector<Port>& portsOf (Plugin p) { return p == Plugin::pmSynth ? kPm : p == Plugin::chatterbox ? kCb : kDs; }
bool isAudio (const char* s) { return std::strncmp (s, "audio_out", 9) == 0; }
bool isMidi (const char* s) { return std::strcmp (s, "midi_in") == 0; }

// a URID map: the plug-ins ask for the atom / MIDI URIs
struct UridMap
{
    std::vector<std::string> uris;
    static LV2_URID map (LV2_URID_Map_Handle h, const char* uri)
    {
        auto& u = static_cast<UridMap*> (h)->uris;
        for (size_t i = 0; i < u.size(); ++i) if (u[i] == uri) return (LV2_URID) (i + 1);
        u.emplace_back (uri);
        return (LV2_URID) u.size();
    }
};

// one LV2 atom sequence holding at most two MIDI events
struct MidiSeq
{
    alignas (8) uint8_t mem[256] {};
    LV2_Atom_Sequence* seq() { return reinterpret_cast<LV2_Atom_Sequence*> (mem); }
    void clear (LV2_URID seqType, LV2_URID frameType)
    {
        std::memset (mem, 0, sizeof (mem));
        seq()->atom.type = seqType; seq()->atom.size = sizeof (LV2_Atom_Sequence_Body);
        seq()->body.unit = frameType; seq()->body.pad = 0;
    }
    void add (int64_t frame, LV2_URID midiType, uint8_t a, uint8_t b, uint8_t c)
    {
        auto* end = mem + sizeof (LV2_Atom) + seq()->atom.size;
        auto* ev = reinterpret_cast<LV2_Atom_Event*> (end);
        ev->time.frames = frame; ev->body.type = midiType; ev->body.size = 3;
        auto* d = reinterpret_cast<uint8_t*> (ev + 1); d[0] = a; d[1] = b; d[2] = c;
        seq()->atom.size += (uint32_t) lv2_atom_pad_size (sizeof (LV2_Atom_Event) + 3);
    }
};
}

std::map<std::string, float> defaults (Plugin p)
{
    std::map<std::string, float> m;
    for (auto& q : portsOf (p)) if (! isAudio (q.sym) && ! isMidi (q.sym)) m[q.sym] = q.def;
    return m;
}

std::map<std::string, float> parseTtlDefaults (const juce::String& ttl)
{
    std::map<std::string, float> m;
    // each port block: lv2:symbol "x" ; ... lv2:default v ;
    int at = 0;
    while ((at = ttl.indexOf (at, "lv2:symbol")) >= 0)
    {
        const int q0 = ttl.indexOfChar (at, '"'), q1 = ttl.indexOfChar (q0 + 1, '"');
        const auto sym = ttl.substring (q0 + 1, q1);
        const int nextSym = ttl.indexOf (q1, "lv2:symbol");
        const int d = ttl.indexOf (q1, "lv2:default");
        if (d >= 0 && (nextSym < 0 || d < nextSym))
            m[sym.toStdString()] = ttl.substring (d + 11).trimStart().upToFirstOccurrenceOf (" ", false, false).upToFirstOccurrenceOf (";", false, false).getFloatValue();
        at = q1;
    }
    return m;
}

juce::AudioBuffer<float> render (Plugin p, const std::map<std::string, float>& ports, const Note& note, double seconds, double sr)
{
    const auto* desc = p == Plugin::pmSynth ? flues_pmsynth_descriptor (0) : p == Plugin::chatterbox ? flues_chatterbox_descriptor (0) : flues_disyn_descriptor (0);
    const int total = juce::jmax (64, (int) (seconds * sr));
    juce::AudioBuffer<float> out (2, total);
    out.clear();
    if (desc == nullptr) return out;
    UridMap um;
    LV2_URID_Map map { &um, &UridMap::map };
    LV2_Feature mapF { LV2_URID__map, &map };
    const LV2_Feature* feats[] = { &mapF, nullptr };
    LV2_Handle h = desc->instantiate (desc, sr, "", feats);
    if (h == nullptr) return out;
    const auto& ps = portsOf (p);
    std::vector<float> values (ps.size(), 0.0f);
    constexpr int kBlock = 256;
    std::vector<float> audio[2] { std::vector<float> (kBlock), std::vector<float> (kBlock) };
    MidiSeq midi;
    const LV2_URID seqT = UridMap::map (&um, LV2_ATOM__Sequence), frameT = UridMap::map (&um, LV2_ATOM__frameTime), midiT = UridMap::map (&um, LV2_MIDI__MidiEvent);
    int audioIdx = 0;
    for (size_t i = 0; i < ps.size(); ++i)
    {
        if (isAudio (ps[i].sym)) desc->connect_port (h, (uint32_t) i, audio[audioIdx++].data());
        else if (isMidi (ps[i].sym)) desc->connect_port (h, (uint32_t) i, midi.seq());
        else
        {
            auto it = ports.find (ps[i].sym);
            values[i] = it != ports.end() ? it->second : ps[i].def;
            desc->connect_port (h, (uint32_t) i, &values[i]);
        }
    }
    if (desc->activate) desc->activate (h);
    // Chatterbox keeps its last formant values in function statics shared by every instance, so a second instance with
    // the same values would never set them: one empty run with other values first makes it apply ours (glue, no sound)
    if (p == Plugin::chatterbox)
    {
        std::vector<float> keep = values;
        for (size_t i = 0; i < ps.size(); ++i) if (ps[i].sym[0] == 'f' && ps[i].sym[1] >= '1' && ps[i].sym[1] <= '4') values[i] = -1.0f;
        midi.clear (seqT, frameT);
        desc->run (h, 0);
        values = keep;
    }
    const int offAt = juce::jlimit (1, total - 1, (int) (note.offAt * total));
    for (int at = 0; at < total; at += kBlock)
    {
        const int n = juce::jmin (kBlock, total - at);
        midi.clear (seqT, frameT);
        if (at == 0) midi.add (0, midiT, LV2_MIDI_MSG_NOTE_ON, (uint8_t) juce::jlimit (0, 127, note.note), (uint8_t) juce::jlimit (1, 127, note.velocity));
        if (offAt >= at && offAt < at + n) midi.add (offAt - at, midiT, LV2_MIDI_MSG_NOTE_OFF, (uint8_t) juce::jlimit (0, 127, note.note), 0);
        desc->run (h, (uint32_t) n);
        for (int c = 0; c < 2; ++c)
        {
            const auto& src = audio[audioIdx == 2 ? c : 0];
            for (int i = 0; i < n; ++i) out.setSample (c, at + i, std::isfinite (src[(size_t) i]) ? src[(size_t) i] : 0.0f);
        }
    }
    if (desc->deactivate) desc->deactivate (h);
    desc->cleanup (h);
    return out;
}

struct Instance::Impl
{
    const LV2_Descriptor* desc = nullptr;
    UridMap um;
    LV2_URID_Map map { &um, &UridMap::map };
    LV2_Feature mapF { LV2_URID__map, &map };
    std::vector<Port> ps;
    std::vector<float> values;
    std::vector<float> audio = std::vector<float> (256);
    MidiSeq midi;
    LV2_URID seqT = 0, frameT = 0, midiT = 0;
    bool pendingClear = true;
};

Instance::Instance (Plugin p, const std::map<std::string, float>& ports, double sr) : impl (std::make_unique<Impl>())
{
    auto& m = *impl;
    m.desc = p == Plugin::pmSynth ? flues_pmsynth_descriptor (0) : p == Plugin::chatterbox ? flues_chatterbox_descriptor (0) : flues_disyn_descriptor (0);
    if (m.desc == nullptr || p == Plugin::disyn) return;   // mono plug-ins only
    const LV2_Feature* feats[] = { &m.mapF, nullptr };
    m.seqT = UridMap::map (&m.um, LV2_ATOM__Sequence); m.frameT = UridMap::map (&m.um, LV2_ATOM__frameTime); m.midiT = UridMap::map (&m.um, LV2_MIDI__MidiEvent);
    handle = m.desc->instantiate (m.desc, sr, "", feats);
    if (handle == nullptr) return;
    m.ps = portsOf (p);
    m.values.assign (m.ps.size(), 0.0f);
    for (size_t i = 0; i < m.ps.size(); ++i)
    {
        if (isAudio (m.ps[i].sym)) m.desc->connect_port (handle, (uint32_t) i, m.audio.data());
        else if (isMidi (m.ps[i].sym)) m.desc->connect_port (handle, (uint32_t) i, m.midi.seq());
        else
        {
            auto it = ports.find (m.ps[i].sym);
            m.values[i] = it != ports.end() ? it->second : m.ps[i].def;
            m.desc->connect_port (handle, (uint32_t) i, &m.values[i]);
        }
    }
    if (m.desc->activate) m.desc->activate (handle);
    m.midi.clear (m.seqT, m.frameT);
    // one silent run here: the plug-in applies its ports on its first run (PM-Synth makes its interface model then), so
    // that allocation happens now and not on the audio thread
    m.desc->run (handle, 1);
}

Instance::~Instance()
{
    if (handle != nullptr)
    {
        if (impl->desc->deactivate) impl->desc->deactivate (handle);
        impl->desc->cleanup (handle);
    }
}

void Instance::set (const char* symbol, float value) noexcept
{
    for (size_t i = 0; i < impl->ps.size(); ++i)
        if (std::strcmp (impl->ps[i].sym, symbol) == 0) { impl->values[i] = value; return; }
}

void Instance::noteOn (int note, int velocity, int frame) noexcept
{
    if (impl->pendingClear) { impl->midi.clear (impl->seqT, impl->frameT); impl->pendingClear = false; }
    if (impl->midi.seq()->atom.size + 32 < sizeof (impl->midi.mem) - sizeof (LV2_Atom))
        impl->midi.add (frame, impl->midiT, LV2_MIDI_MSG_NOTE_ON, (uint8_t) juce::jlimit (0, 127, note), (uint8_t) juce::jlimit (1, 127, velocity));
}

void Instance::noteOff (int note, int frame) noexcept
{
    if (impl->pendingClear) { impl->midi.clear (impl->seqT, impl->frameT); impl->pendingClear = false; }
    if (impl->midi.seq()->atom.size + 32 < sizeof (impl->midi.mem) - sizeof (LV2_Atom))
        impl->midi.add (frame, impl->midiT, LV2_MIDI_MSG_NOTE_OFF, (uint8_t) juce::jlimit (0, 127, note), 0);
}

void Instance::run (float* out, int n) noexcept
{
    if (handle == nullptr) { std::fill (out, out + n, 0.0f); return; }
    if (impl->pendingClear) impl->midi.clear (impl->seqT, impl->frameT);
    n = juce::jmin (n, 256);
    impl->desc->run (handle, (uint32_t) n);
    for (int i = 0; i < n; ++i) out[i] = std::isfinite (impl->audio[(size_t) i]) ? impl->audio[(size_t) i] : 0.0f;
    impl->pendingClear = true;
}
}
