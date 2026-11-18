/*
 * Ghost Bass b11 - UPSTREAM REFERENCE RENDERER (test tool, not shipped).
 * Compiles Bombaz's own sources in the global namespace, with their own parameter layouts, and renders the
 * shared MIDI script the way the upstream plug-in's processBlock does. The null test compares Ghost Bass's SUB
 * layer against these files, bit for bit.
 */
#define GB_NO_USING_JUCE 1
#include "shim/JuceHeader.h"
#include "LayerHostProcessor.h"
#include "ref/RefScript.h"
#include <cstdio>

#include "Parameters.h"
#include "Synth.h"

static void setNorm(juce::AudioProcessorValueTreeState &v, const char *id, float plain)
{
    auto *p = v.getParameter(id);
    p->setValue(p->convertTo0to1(plain));
}

int ref_bombaz(bool custom, const char *out)
{
    GhostLayerHostProcessor host;
    juce::AudioProcessorValueTreeState apvts(host, nullptr, "Parameters", Parameters::createParameterLayout());
    Parameters params(apvts);
    Synth synth(params);
    if (custom)
    {
        setNorm(apvts, "width", 70.f);
        setNorm(apvts, "drive", 60.f);
        setNorm(apvts, "octave", 2.f);
        setNorm(apvts, "tuning", 12.f);
        setNorm(apvts, "glideTime", 120.f);
    }
    // = BassSynthAudioProcessor::prepareToPlay
    params.prepareToPlay(48000.f);
    params.reset();
    synth.prepareToPlay(48000.f, 32);
    synth.reset();
    std::vector<float> all;
    float buf[32];
    for (int b = -ghost::layers::ref::kLead; b < ghost::layers::ref::kBlocks; ++b)
    {
        for (auto &e : ghost::layers::ref::script())
            if (e.block == b)
            {
                if (e.type == 0) synth.midiMessage(0x90, (uint8_t)e.a, (uint8_t)e.b);
                else if (e.type == 1) synth.midiMessage(0x80, (uint8_t)e.a, 0);
                else if (e.type == 2) synth.midiMessage(0xE0, (uint8_t)(e.a & 127), (uint8_t)(e.a >> 7));
                else synth.midiMessage(0xB0, 64, (uint8_t)e.a);
            }
        juce::ScopedNoDenormals noDenormals; // = processBlock
        params.update();
        synth.render(buf, 32);
        if (b >= 0)
            all.insert(all.end(), buf, buf + 32);
    }
    auto *f = std::fopen(out, "wb");
    std::fwrite(all.data(), sizeof(float), all.size(), f);
    std::fclose(f);
    return 0;
}
