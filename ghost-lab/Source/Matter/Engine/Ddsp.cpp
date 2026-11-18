// Ghost Matter - AGPL-3.0-only (see LICENSE, NOTICE.md). Runs Magenta's DDSP-VST processor offline (Ddsp.h).
#include "Ddsp.h"
#if GHOSTMATTER_DDSP
 #include "PluginProcessor.h"   // DDSP-VST's own (third_party/ddsp-vst/src, unmodified), through shim/JuceHeader.h
 #include "util/Constants.h"
#endif
#include <cmath>
#include <memory>
#include <mutex>

namespace matter::neural
{
int passesFor (float refeed) { return 1 + juce::jlimit (0, 3, (int) std::lround (3.0f * juce::jlimit (0.0f, 1.0f, refeed))); }

int holdBlocksFor (float wobble)
{
    // CHAOS 0..1: a jump every 0.5 s (25 frames) .. every frame (20 ms)
    return wobble <= 0.001f ? 0 : juce::jmax (1, (int) std::lround (25.0 * std::pow (1.0 / 25.0, (double) juce::jlimit (0.0f, 1.0f, wobble))));
}

int hopBlocksFor (float hop)
{
    // HOP 0..1: a new model every 2 s (100 frames) .. every 40 ms (2 frames)
    return hop <= 0.001f ? 0 : juce::jmax (2, (int) std::lround (100.0 * std::pow (2.0 / 100.0, (double) juce::jlimit (0.0f, 1.0f, hop))));
}

juce::File userModelsFolder()
{
    // where DDSP-VST's ModelLibrary::setPathToUserModels looks
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Magenta").getChildFile ("DDSP").getChildFile ("Models");
}

#if GHOSTMATTER_DDSP
namespace
{
// one processor for the whole process, made the first time it is needed and kept (it holds its 11 models in memory, about
// 80 MB, so it is not made again for every render). One render at a time uses it.
// When your own models folder changes it is made again (its model library reads the folder once, when it is made).
std::mutex& hostLock() { static std::mutex m; return m; }
juce::String folderSignature()
{
    juce::String sig;
    for (auto& f : userModelsFolder().findChildFiles (juce::File::findFiles, true, "*.tflite"))
        sig << f.getFullPathName() << ":" << f.getSize() << ":" << f.getLastModificationTime().toMilliseconds() << ";";
    return sig;
}
DDSPAudioProcessor& host()
{
    static std::unique_ptr<DDSPAudioProcessor>* p = new std::unique_ptr<DDSPAudioProcessor>();   // kept until the plug-in unloads
    static juce::String sig;
    const auto now = folderSignature();
    if (*p == nullptr || now != sig) { p->reset(); *p = std::make_unique<DDSPAudioProcessor> (true); sig = now; }   // single-threaded: inference on the caller
    return **p;
}

void setParam (DDSPAudioProcessor& p, const char* id, float v)
{
    if (auto* prm = p.getValueTree().getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (prm->getNormalisableRange().snapToLegalValue (v)));
}
float defaultOf (DDSPAudioProcessor& p, const char* id)
{
    if (auto* prm = p.getValueTree().getParameter (id)) return prm->convertFrom0to1 (prm->getDefaultValue());
    return 0.0f;
}
}

bool available() { return true; }

juce::StringArray modelNames()
{
    // ModelLibrary's own order (loadEmbeddedModels), then the .tflite files in its folder (searchPathForModels: all of them,
    // by file name; one it cannot load is reported by DDSP itself when it is used). Read without the processor, so the menu
    // never waits for a render.
    juce::StringArray n { "Flute", "Violin", "Trumpet", "Saxophone", "Bassoon", "Clarinet", "Melodica", "Sitar", "Trombone", "Tuba", "Vowels" };
    for (auto& f : userModelsFolder().findChildFiles (juce::File::findFiles, true, "*.tflite")) n.addIfNotAlreadyThere (f.getFileNameWithoutExtension());
    return n;
}

bool render (const juce::AudioBuffer<float>& in, const MorphSettings& m, juce::AudioBuffer<float>& out, juce::String& why,
             const std::atomic<bool>* cancel, std::atomic<float>* progress)
{
    const int n = in.getNumSamples();
    if (n == 0 || in.getNumChannels() == 0) { why = "no sound"; return false; }
    std::lock_guard<std::mutex> lock (hostLock());
    auto& p = host();
    const auto& models = p.getModelLibrary().getModelList();
    int model = -1;
    for (int i = 0; i < (int) models.size(); ++i)
        if (models[(size_t) i].name == m.ddspModel) { model = i; break; }
    if (model < 0) { why = "the model " + m.ddspModel + " is not on this computer"; return false; }

    // its own controls: the ones you set UNDER THE HOOD (ddsp.<its parameter ID>), else its defaults
    const char* ids[] = { "InputGain", "InputPitch", "PitchShift", "HarmonicGain", "NoiseGain", "OutputGain", "ReverbSize", "ReverbDamping", "ReverbWet" };
    std::map<std::string, float> base;
    for (auto* id : ids)
    {
        base[id] = hood::pinOr (m.pins, (std::string ("ddsp.") + id).c_str(), defaultOf (p, id));
        setParam (p, id, base[id]);
    }

    // a host's offline render: non-realtime, so its processBlock runs the model on this thread
    const int hop = (int) std::lround (kRate * ddsp::kModelHopSize / ddsp::kModelSampleRate_Hz);   // 960 samples = one model frame (20 ms)
    p.setNonRealtime (true);
    p.setRateAndBufferSizeDetails (kRate, hop);
    p.prepareToPlay (kRate, hop);
    const int latency = juce::jmax (0, p.getLatencySamples());

    const int passes = passesFor (m.refeed);
    const int hold = holdBlocksFor (m.wobble), hopEvery = hopBlocksFor (m.hop);
    const int blocks = (n + latency + hop - 1) / hop;
    juce::Random rng (0x67686f7374LL);   // the same jumps every time the same sound is rendered
    juce::AudioBuffer<float> src (2, n);
    for (int c = 0; c < 2; ++c) src.copyFrom (c, 0, in, juce::jmin (c, in.getNumChannels() - 1), 0, n);
    juce::AudioBuffer<float> io (2, hop), res (2, blocks * hop);
    juce::MidiBuffer midi;
    int current = model;
    for (int pass = 0; pass < passes; ++pass)
    {
        p.loadModel (model);   // every pass starts from your model (HOP moves on from there)
        current = model;
        p.prepareToPlay (kRate, hop);   // non-realtime: its pipeline starts fresh
        res.clear();
        for (int b = 0; b < blocks; ++b)
        {
            if ((cancel != nullptr && cancel->load()) || juce::Thread::currentThreadShouldExit()) { why = "stopped (a newer change came in)"; return false; }
            if (hold > 0 && b % hold == 0)
            {
                // CHAOS: its own Pitch Shift (semitones), Input Pitch and Input Gain jump to a new random place and stay there a moment
                const float w = m.wobble;
                setParam (p, "PitchShift", base["PitchShift"] + std::round ((rng.nextFloat() * 2.0f - 1.0f) * 24.0f * w));
                setParam (p, "InputPitch", base["InputPitch"] + (rng.nextFloat() * 2.0f - 1.0f) * 0.5f * w);
                setParam (p, "InputGain", base["InputGain"] + (rng.nextFloat() * 2.0f - 1.0f) * 0.5f * w);
            }
            if (hopEvery > 0 && b > 0 && b % hopEvery == 0 && models.size() > 1)
            {
                // HOP: its own loadModel, to another of its models (it swaps in at the next frame, its GRU starting fresh)
                int next = rng.nextInt ((int) models.size() - 1);
                if (next >= current) ++next;
                p.loadModel (next);
                current = next;
            }
            io.clear();
            const int from = b * hop, len = juce::jlimit (0, hop, n - from);
            for (int c = 0; c < 2 && len > 0; ++c) io.copyFrom (c, 0, src, c, from, len);
            p.processBlock (io, midi);
            for (int c = 0; c < 2; ++c) res.copyFrom (c, from, io, c, 0, hop);
            if (progress != nullptr) progress->store ((float) (pass * blocks + b + 1) / (float) (passes * blocks));
        }
        // its output, lined up with its input (the 64 ms it reports as latency taken off)
        for (int c = 0; c < 2; ++c) src.copyFrom (c, 0, res, c, latency, n);
    }
    out = src;
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < n; ++i)
            if (! std::isfinite (out.getSample (c, i))) { why = "the model gave no sound"; return false; }
    return true;
}
#else
bool available() { return false; }
juce::StringArray modelNames() { return {}; }
bool render (const juce::AudioBuffer<float>&, const MorphSettings&, juce::AudioBuffer<float>&, juce::String& why, const std::atomic<bool>*, std::atomic<float>*)
{
    why = "this build has no DDSP in it";
    return false;
}
#endif
}
