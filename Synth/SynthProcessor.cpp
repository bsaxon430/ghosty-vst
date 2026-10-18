#include "SynthProcessor.h"
#include "SynthEditor.h"

using namespace ghosty;

// ------------------------------------------------------------------ presets
namespace
{
struct Preset
{
    const char* cat; const char* name; int A, B; float morph; int uni; float width, sub, a, d, s, r, cut, res, fenv, echo, verb, drive, chorus, phaser, crush, haunt;
};
// Shapes: 0 Sine 1 Triangle 2 Saw 3 Square 4 Pulse 5 Organ 6 Glass 7 Growl 8 Digital 9 Vox
const Preset presets[] = {
    { "Keys", "Ghost Piano", 6, 1, 0.35f, 1, 0.0f, 0.0f, 0.002f, 1.6f, 0.0f, 0.8f, 0.62f, 0.1f, 0.55f, 0.1f, 0.35f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Keys", "Dream EP", 0, 1, 0.3f, 2, 0.15f, 0.0f, 0.003f, 1.4f, 0.25f, 0.7f, 0.6f, 0.1f, 0.3f, 0.15f, 0.35f, 0.0f, 0.6f, 0.2f, 0.0f, 0.0f },
    { "Keys", "Glass Keys", 6, 0, 0.3f, 3, 0.2f, 0.0f, 0.002f, 1.2f, 0.2f, 1.0f, 0.66f, 0.2f, 0.5f, 0.3f, 0.45f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Keys", "Digital Bell", 8, 6, 0.6f, 1, 0.0f, 0.0f, 0.001f, 1.5f, 0.0f, 1.4f, 0.86f, 0.1f, 0.2f, 0.35f, 0.55f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Keys", "Music Box", 0, 8, 0.5f, 1, 0.0f, 0.0f, 0.001f, 1.2f, 0.0f, 1.5f, 0.9f, 0.1f, 0.1f, 0.45f, 0.6f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Keys", "Hollow Organ", 5, 1, 0.2f, 2, 0.15f, 0.2f, 0.02f, 0.2f, 0.9f, 0.3f, 0.7f, 0.05f, 0.0f, 0.15f, 0.4f, 0.15f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Keys", "Church Organ", 5, 5, 0.0f, 3, 0.2f, 0.2f, 0.05f, 0.5f, 1.0f, 0.8f, 0.8f, 0.0f, 0.0f, 0.0f, 0.75f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Keys", "Lofi Keys", 1, 6, 0.4f, 2, 0.1f, 0.0f, 0.005f, 1.0f, 0.3f, 0.6f, 0.45f, 0.1f, 0.2f, 0.2f, 0.4f, 0.0f, 0.4f, 0.0f, 0.35f, 0.0f },
    { "Pads", "Vapor Pad", 9, 5, 0.5f, 7, 0.45f, 0.15f, 1.2f, 1.0f, 0.9f, 2.5f, 0.52f, 0.2f, 0.1f, 0.3f, 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Pads", "Haunted Halls", 9, 6, 0.6f, 5, 0.5f, 0.0f, 1.0f, 1.0f, 0.9f, 3.0f, 0.5f, 0.3f, 0.0f, 0.0f, 0.85f, 0.0f, 0.3f, 0.0f, 0.0f, 0.8f },
    { "Pads", "Trance Pad", 2, 3, 0.1f, 7, 0.5f, 0.2f, 0.05f, 0.5f, 1.0f, 0.8f, 0.7f, 0.05f, 0.0f, 0.2f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Pads", "Future Bass Chords", 2, 9, 0.25f, 7, 0.6f, 0.3f, 0.01f, 0.6f, 0.85f, 0.5f, 0.75f, 0.1f, 0.2f, 0.0f, 0.4f, 0.0f, 0.3f, 0.0f, 0.0f, 0.0f },
    { "Pads", "Phaser Sky", 5, 9, 0.4f, 5, 0.4f, 0.0f, 0.8f, 0.5f, 0.9f, 2.0f, 0.6f, 0.1f, 0.0f, 0.0f, 0.7f, 0.0f, 0.0f, 0.8f, 0.0f, 0.0f },
    { "Pads", "Choir Aahs", 9, 9, 0.0f, 7, 0.3f, 0.0f, 0.4f, 1.0f, 0.9f, 1.8f, 0.55f, 0.15f, 0.0f, 0.2f, 0.8f, 0.0f, 0.3f, 0.0f, 0.0f, 0.0f },
    { "Pads", "Slow Strings", 2, 2, 0.0f, 7, 0.35f, 0.0f, 0.6f, 1.0f, 0.9f, 1.6f, 0.5f, 0.1f, 0.05f, 0.1f, 0.7f, 0.0f, 0.2f, 0.0f, 0.0f, 0.0f },
    { "Pads", "Glass Cathedral", 6, 0, 0.5f, 5, 0.4f, 0.0f, 1.5f, 1.0f, 0.9f, 3.0f, 0.6f, 0.2f, 0.0f, 0.35f, 0.9f, 0.0f, 0.0f, 0.0f, 0.0f, 0.3f },
    { "Pads", "Dark Matter", 7, 9, 0.6f, 7, 0.5f, 0.3f, 1.2f, 1.0f, 0.9f, 2.5f, 0.35f, 0.4f, 0.1f, 0.0f, 0.8f, 0.0f, 0.0f, 0.0f, 0.0f, 0.5f },
    { "Bass", "Growl Bass", 7, 2, 0.4f, 3, 0.2f, 0.6f, 0.005f, 0.3f, 0.7f, 0.2f, 0.36f, 0.45f, 0.6f, 0.05f, 0.1f, 0.55f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Bass", "Reese Bass", 2, 2, 0.0f, 5, 0.35f, 0.5f, 0.01f, 0.5f, 0.9f, 0.3f, 0.35f, 0.2f, 0.0f, 0.0f, 0.1f, 0.3f, 0.5f, 0.0f, 0.0f, 0.0f },
    { "Bass", "Acid 303", 2, 3, 0.2f, 1, 0.0f, 0.0f, 0.003f, 0.25f, 0.3f, 0.15f, 0.15f, 0.8f, 0.9f, 0.3f, 0.15f, 0.45f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Bass", "Sub Bass", 0, 1, 0.1f, 1, 0.0f, 0.5f, 0.005f, 0.5f, 1.0f, 0.25f, 0.5f, 0.05f, 0.0f, 0.0f, 0.05f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Bass", "808 Glide", 0, 1, 0.0f, 1, 0.0f, 0.3f, 0.002f, 1.6f, 0.0f, 0.4f, 0.6f, 0.0f, 0.0f, 0.0f, 0.05f, 0.35f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Bass", "Grime Bass", 3, 7, 0.5f, 1, 0.0f, 0.5f, 0.005f, 0.2f, 0.8f, 0.15f, 0.4f, 0.5f, 0.5f, 0.0f, 0.05f, 0.6f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Bass", "Finger Bass", 1, 0, 0.3f, 1, 0.0f, 0.3f, 0.003f, 0.9f, 0.2f, 0.2f, 0.35f, 0.1f, 0.3f, 0.0f, 0.1f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Bass", "Hoover Bass", 2, 4, 0.3f, 7, 0.6f, 0.4f, 0.01f, 0.4f, 0.8f, 0.3f, 0.55f, 0.2f, 0.3f, 0.0f, 0.1f, 0.4f, 0.3f, 0.0f, 0.0f, 0.0f },
    { "Leads", "Neon Supersaw", 2, 3, 0.15f, 7, 0.55f, 0.25f, 0.01f, 0.4f, 0.8f, 0.6f, 0.82f, 0.1f, 0.2f, 0.25f, 0.35f, 0.1f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Leads", "Vox Lead", 9, 2, 0.25f, 3, 0.25f, 0.1f, 0.01f, 0.3f, 0.7f, 0.3f, 0.6f, 0.3f, 0.3f, 0.4f, 0.3f, 0.25f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Leads", "Chip Lead", 3, 8, 0.3f, 1, 0.0f, 0.0f, 0.002f, 0.2f, 0.7f, 0.1f, 0.9f, 0.05f, 0.0f, 0.3f, 0.2f, 0.0f, 0.0f, 0.0f, 0.55f, 0.0f },
    { "Leads", "Screamer", 2, 7, 0.3f, 3, 0.3f, 0.0f, 0.005f, 0.3f, 0.9f, 0.3f, 0.7f, 0.35f, 0.2f, 0.3f, 0.25f, 0.8f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Leads", "Glitch Ghost", 8, 9, 0.5f, 3, 0.3f, 0.0f, 0.005f, 0.3f, 0.8f, 0.4f, 0.7f, 0.3f, 0.0f, 0.3f, 0.3f, 0.0f, 0.0f, 0.0f, 0.25f, 0.3f },
    { "Leads", "Whistle", 0, 0, 0.0f, 1, 0.0f, 0.0f, 0.03f, 0.3f, 1.0f, 0.4f, 0.8f, 0.0f, 0.0f, 0.4f, 0.4f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Leads", "Ghost Flute", 1, 0, 0.3f, 1, 0.0f, 0.0f, 0.06f, 0.3f, 0.9f, 0.4f, 0.55f, 0.1f, 0.0f, 0.2f, 0.45f, 0.0f, 0.2f, 0.0f, 0.0f, 0.0f },
    { "Leads", "Hoover Lead", 2, 4, 0.3f, 7, 0.6f, 0.4f, 0.01f, 0.4f, 0.8f, 0.4f, 0.65f, 0.2f, 0.2f, 0.15f, 0.3f, 0.5f, 0.3f, 0.0f, 0.0f, 0.0f },
    { "Leads", "Brass Stab", 2, 3, 0.2f, 3, 0.2f, 0.0f, 0.01f, 0.35f, 0.6f, 0.25f, 0.35f, 0.15f, 0.7f, 0.1f, 0.3f, 0.2f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Plucks", "Pluck Fever", 3, 4, 0.4f, 3, 0.3f, 0.0f, 0.001f, 0.3f, 0.0f, 0.35f, 0.3f, 0.35f, 0.8f, 0.45f, 0.35f, 0.1f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Plucks", "Hyper Pluck", 3, 2, 0.5f, 5, 0.4f, 0.0f, 0.001f, 0.2f, 0.0f, 0.3f, 0.2f, 0.4f, 0.9f, 0.5f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Plucks", "Kalimba", 0, 6, 0.3f, 1, 0.0f, 0.0f, 0.001f, 0.7f, 0.0f, 0.5f, 0.7f, 0.1f, 0.3f, 0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Plucks", "Marimba", 0, 1, 0.2f, 1, 0.0f, 0.0f, 0.001f, 0.5f, 0.0f, 0.3f, 0.75f, 0.0f, 0.2f, 0.0f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Plucks", "Harp", 1, 6, 0.2f, 1, 0.0f, 0.0f, 0.002f, 1.8f, 0.0f, 1.2f, 0.6f, 0.1f, 0.4f, 0.2f, 0.6f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Plucks", "Steel Drum", 6, 0, 0.4f, 1, 0.0f, 0.0f, 0.001f, 0.9f, 0.0f, 0.6f, 0.8f, 0.1f, 0.2f, 0.2f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Plucks", "Sitar", 7, 2, 0.5f, 1, 0.0f, 0.0f, 0.002f, 1.6f, 0.1f, 0.8f, 0.55f, 0.5f, 0.5f, 0.1f, 0.4f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
    { "Plucks", "Nylon Pluck", 1, 2, 0.25f, 1, 0.0f, 0.0f, 0.002f, 1.5f, 0.0f, 0.7f, 0.45f, 0.15f, 0.6f, 0.0f, 0.3f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f },
};
constexpr int numPresets = (int) (sizeof (presets) / sizeof (presets[0]));
}

// ------------------------------------------------------------------ voice
void GhostVoice::prepare (double s, int block)
{
    sr = s;
    filter.prepare ({ s, (juce::uint32) block, 2 });
    filter.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
}

void GhostVoice::startNote (int midi, float vel, juce::SynthesiserSound*, int wheel)
{
    freq = juce::MidiMessage::getMidiNoteInHertz (midi);
    velocity = 0.35f + vel * 0.65f;
    pitchWheelMoved (wheel);
    auto& r = juce::Random::getSystemRandom();
    for (auto& p : phase) p = r.nextDouble();
    subPhase = 0;
    amp.setSampleRate (sr); fenv.setSampleRate (sr);
    amp.setParameters ({ proc.attack->load(), proc.decay->load(), proc.sustain->load(), proc.release->load() });
    fenv.setParameters ({ proc.attack->load() * 0.8f + 0.001f, proc.decay->load(), 0.0f, proc.release->load() });
    amp.noteOn(); fenv.noteOn();
    filter.reset();
}

void GhostVoice::stopNote (float, bool allowTail)
{
    if (allowTail) { amp.noteOff(); fenv.noteOff(); }
    else { amp.reset(); fenv.reset(); clearCurrentNote(); }
}

void GhostVoice::renderNextBlock (juce::AudioBuffer<float>& out, int start, int num)
{
    if (! isVoiceActive()) return;
    const int live = proc.bank.live.load();
    const auto& A = proc.bank.a[live];
    const auto& B = proc.bank.b[live];
    if (A.t[0].empty()) return;

    const int uni = juce::jlimit (1, 7, (int) proc.unison->load());
    const float morph = proc.morph->load(), width = proc.width->load(), sub = proc.sub->load();
    const float baseCut = SynthProcessor::cutoffHz (proc.cutoff->load()), fAmt = proc.fenvAmt->load();
    filter.setResonance (0.7f + proc.res->load() * 9.0f);

    const double f0 = freq * bend;
    const int mip = MipSet::mipFor (f0 * 1.03);
    std::array<double, 7> inc {}; std::array<float, 7> pl {}, pr {};
    for (int u = 0; u < uni; ++u)
    {
        const double spread = uni == 1 ? 0.0 : ((double) u / (uni - 1) * 2.0 - 1.0);
        inc[(size_t) u] = f0 * std::pow (2.0, spread * width * 50.0 / 1200.0) / sr;
        const float pan = (float) spread * juce::jmin (1.0f, width * 1.6f);
        pl[(size_t) u] = std::sqrt (0.5f * (1.0f - pan)); pr[(size_t) u] = std::sqrt (0.5f * (1.0f + pan));
    }
    const float norm = 0.22f * velocity / std::sqrt ((float) uni);
    auto* L = out.getWritePointer (0, start);
    auto* R = out.getNumChannels() > 1 ? out.getWritePointer (1, start) : nullptr;

    for (int i = 0; i < num; ++i)
    {
        if ((i & 31) == 0)
        {
            const float fe = fenv.getNextSample();
            filter.setCutoffFrequency (juce::jlimit (30.0f, 19000.0f, baseCut * std::pow (2.0f, fAmt * 5.0f * fe)));
        }
        else fenv.getNextSample();

        float l = 0, r = 0;
        for (int u = 0; u < uni; ++u)
        {
            auto& ph = phase[(size_t) u];
            const float a = A.read (mip, ph), b = B.read (mip, ph);
            const float v = a + (b - a) * morph;
            l += v * pl[(size_t) u]; r += v * pr[(size_t) u];
            ph += inc[(size_t) u]; if (ph >= 1.0) ph -= 1.0;
        }
        const float sv = (float) std::sin (subPhase * juce::MathConstants<double>::twoPi) * sub * 0.8f * std::sqrt ((float) uni);
        subPhase += f0 * 0.5 / sr; if (subPhase >= 1.0) subPhase -= 1.0;
        l += sv; r += sv;

        const float env = amp.getNextSample() * norm;
        l = filter.processSample (0, l) * env;
        r = filter.processSample (1, r) * env;
        L[i] += l; if (R) R[i] += r;
    }
    if (! amp.isActive()) clearCurrentNote();
}

// ------------------------------------------------------------------ processor
juce::AudioProcessorValueTreeState::ParameterLayout SynthProcessor::layout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pct = [] (const String& id, const String& nm, float def)
    {
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, nm, NormalisableRange<float> (0.0f, 1.0f), def,
                   AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return String (roundToInt (v * 100)) + "%"; }));
    };
    auto secs = [] (const String& id, const String& nm, float lo, float hi, float def)
    {
        NormalisableRange<float> r (lo, hi); r.setSkewForCentre (lo + (hi - lo) * 0.12f);
        return std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, nm, r, def,
                   AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return v < 1 ? String (roundToInt (v * 1000)) + " ms" : String (v, 2) + " s"; }));
    };
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "waveA", 1 }, "Wave A", shapeNames(), 2));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "waveB", 1 }, "Wave B", shapeNames(), 3));
    l.add (pct ("morph", "Morph", 0.15f));
    l.add (std::make_unique<AudioParameterInt> (ParameterID { "unison", 1 }, "Unison", 1, 7, 7));
    l.add (pct ("width", "Width", 0.55f));
    l.add (pct ("sub", "Sub", 0.25f));
    l.add (secs ("attack", "Attack", 0.001f, 4.0f, 0.01f));
    l.add (secs ("decay", "Decay", 0.01f, 4.0f, 0.4f));
    l.add (pct ("sustain", "Sustain", 0.8f));
    l.add (secs ("release", "Release", 0.01f, 6.0f, 0.6f));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "cutoff", 1 }, "Cutoff", NormalisableRange<float> (0.0f, 1.0f), 0.82f,
               AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { const float f = cutoffHz (v); return f >= 1000 ? String (f / 1000.0f, 1) + " kHz" : String (roundToInt (f)) + " Hz"; })));
    l.add (pct ("res", "Resonance", 0.1f));
    l.add (pct ("fenv", "Filter Env", 0.2f));
    l.add (pct ("drive", "Drive", 0.1f));
    l.add (pct ("crush", "Crush", 0.0f));
    l.add (pct ("chorus", "Chorus", 0.0f));
    l.add (pct ("phaser", "Phaser", 0.0f));
    l.add (pct ("echo", "Echo", 0.25f));
    l.add (pct ("verb", "Reverb", 0.35f));
    l.add (pct ("haunt", "Haunt", 0.0f));
    l.add (pct ("gain", "Volume", 0.75f));
    // play helpers: scale lock, chords, arpeggiator
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "key", 1 }, "Key", keyNames(), 9));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "scale", 1 }, "Scale", scaleNames(), 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "chord", 1 }, "Chords", chordNames(), 0));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "arp", 1 }, "Arp", false));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "arpRate", 1 }, "Arp Rate", arpRateNames(), 2));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "arpMode", 1 }, "Arp Mode", arpModeNames(), 0));
    l.add (std::make_unique<AudioParameterInt> (ParameterID { "arpOct", 1 }, "Arp Octaves", 1, 3, 2));
    l.add (pct ("arpGate", "Arp Gate", 0.7f));
    return l;
}

SynthProcessor::SynthProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "GHOSTY", layout())
{
    auto P = [this] (const char* id) { return apvts.getRawParameterValue (id); };
    morph = P ("morph"); unison = P ("unison"); width = P ("width"); sub = P ("sub");
    attack = P ("attack"); decay = P ("decay"); sustain = P ("sustain"); release = P ("release");
    cutoff = P ("cutoff"); res = P ("res"); fenvAmt = P ("fenv"); gain = P ("gain");
    drive = P ("drive"); crush = P ("crush"); chorus = P ("chorus"); phaser = P ("phaser"); echo = P ("echo"); verb = P ("verb"); haunt = P ("haunt");
    keyP = P ("key"); scaleP = P ("scale"); chordP = P ("chord"); arpP = P ("arp"); arpRateP = P ("arpRate"); arpModeP = P ("arpMode"); arpOctP = P ("arpOct"); arpGateP = P ("arpGate");

    frameA = makeShape (2); frameB = makeShape (3);
    for (int i = 0; i < 16; ++i) synth.addVoice (new GhostVoice (*this));
    synth.addSound (new GhostSound());
    apvts.addParameterListener ("waveA", this);
    apvts.addParameterListener ("waveB", this);
    rebuildWaves();
}

SynthProcessor::~SynthProcessor()
{
    apvts.removeParameterListener ("waveA", this);
    apvts.removeParameterListener ("waveB", this);
    cancelPendingUpdate();
}

void SynthProcessor::parameterChanged (const juce::String& id, float)
{
    if (id == "waveA") needA = true; else if (id == "waveB") needB = true;
    triggerAsyncUpdate();
}

void SynthProcessor::handleAsyncUpdate()
{
    if (needA.exchange (false)) frameA = makeShape ((int) apvts.getRawParameterValue ("waveA")->load());
    if (needB.exchange (false)) frameB = makeShape ((int) apvts.getRawParameterValue ("waveB")->load());
    rebuildWaves();
}

void SynthProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const bool srChanged = std::abs (sampleRate - sampleRateNow) > 1.0;
    sampleRateNow = sampleRate;
    synth.setCurrentPlaybackSampleRate (sampleRate);
    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* v = dynamic_cast<GhostVoice*> (synth.getVoice (i))) v->prepare (sampleRate, samplesPerBlock);
    fx.prepare (sampleRate, samplesPerBlock);
    performer.prepare (sampleRate);
    performed.ensureSize (8192);
    if (srChanged) rebuildWaves();
}

bool SynthProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void SynthProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    keyboardState.processNextMidiBuffer (midi, 0, buffer.getNumSamples(), true);

    Performer::Settings ps;
    ps.key = (int) keyP->load(); ps.scale = (int) scaleP->load(); ps.chord = (int) chordP->load();
    ps.arp = arpP->load() > 0.5f; ps.arpRate = (int) arpRateP->load(); ps.arpMode = (int) arpModeP->load();
    ps.arpOct = (int) arpOctP->load(); ps.arpGate = arpGateP->load();
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) ps.bpm = *b;
            ps.hostPlaying = pos->getIsPlaying();
            if (auto q = pos->getPpqPosition()) { ps.ppq = *q; ps.havePpq = true; }
        }
    performer.process (midi, performed, buffer.getNumSamples(), ps);
    synth.renderNextBlock (buffer, performed, 0, buffer.getNumSamples());
    buffer.applyGain (gain->load() * 1.3f);

    FxParams p;
    p.drive = drive->load(); p.crush = crush->load(); p.chorus = chorus->load(); p.phaser = phaser->load();
    p.echo = echo->load(); p.verb = verb->load(); p.haunt = haunt->load();
    p.bpm = ps.bpm;
    fx.process (buffer, p);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i) d[i] = std::tanh (d[i]);   // soft safety limiter
    }
    const float rms = buffer.getRMSLevel (0, 0, buffer.getNumSamples());
    level.store (level.load() * 0.8f + juce::jmin (1.0f, rms * 4.0f) * 0.2f);
}

int SynthProcessor::getNumPrograms() { return numPresets; }
const juce::String SynthProcessor::getProgramName (int i) { return juce::isPositiveAndBelow (i, numPresets) ? presets[i].name : ""; }
juce::String SynthProcessor::getPresetCategory (int i) { return juce::isPositiveAndBelow (i, numPresets) ? presets[i].cat : ""; }

void SynthProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, numPresets)) return;
    currentProgram = index;
    const auto& p = presets[index];
    auto set = [this] (const char* id, float v) { if (auto* prm = apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (v)); };
    set ("waveA", (float) p.A); set ("waveB", (float) p.B); set ("morph", p.morph); set ("unison", (float) p.uni);
    set ("width", p.width); set ("sub", p.sub); set ("attack", juce::jmax (0.001f, p.a)); set ("decay", juce::jmax (0.01f, p.d));
    set ("sustain", p.s); set ("release", juce::jmax (0.01f, p.r)); set ("cutoff", p.cut); set ("res", p.res); set ("fenv", p.fenv);
    set ("echo", p.echo); set ("verb", p.verb); set ("drive", p.drive); set ("chorus", p.chorus); set ("phaser", p.phaser);
    set ("crush", p.crush); set ("haunt", p.haunt);
    frameA = makeShape (p.A); frameB = makeShape (p.B);
    rebuildWaves();
}

static juce::String frameToString (const Frame& f) { juce::MemoryBlock mb (f.data(), sizeof (float) * FRAME); return mb.toBase64Encoding(); }
static bool frameFromString (const juce::String& s, Frame& f)
{
    juce::MemoryBlock mb; if (! mb.fromBase64Encoding (s) || mb.getSize() != sizeof (float) * FRAME) return false;
    std::memcpy (f.data(), mb.getData(), mb.getSize()); return true;
}

void SynthProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    state.setProperty ("frameA", frameToString (frameA), nullptr);
    state.setProperty ("frameB", frameToString (frameB), nullptr);
    state.setProperty ("program", currentProgram, nullptr);
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void SynthProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        if (! state.isValid()) return;
        apvts.replaceState (state);
        needA = needB = false;
        frameFromString (state.getProperty ("frameA").toString(), frameA);
        frameFromString (state.getProperty ("frameB").toString(), frameB);
        currentProgram = (int) state.getProperty ("program", 0);
        rebuildWaves();
    }
}

juce::AudioProcessorEditor* SynthProcessor::createEditor() { return new SynthEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SynthProcessor(); }
