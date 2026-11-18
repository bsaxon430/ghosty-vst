// SPDX-License-Identifier: AGPL-3.0-or-later
// GHOST ATLAS - SoundFont 2 files become SFZ instruments. The file is read with TinySoundFont's parser (Bernhard
// Schelling, MIT; third_party/tsf, itself based on Steve Folta's SFZero), which already turns SoundFont generators into
// the same kind of numbers SFZ uses. Each preset is written as one SFZ plus its samples as WAV files, beside the .sf2;
// the .sf2 itself is not changed.
#include "Import.h"

#if defined (__clang__)
 #pragma clang diagnostic push
 #pragma clang diagnostic ignored "-Weverything"
#elif defined (__GNUC__)
 #pragma GCC diagnostic push
 #pragma GCC diagnostic ignored "-Wall"
 #pragma GCC diagnostic ignored "-Wextra"
 #pragma GCC diagnostic ignored "-Wconversion"
 #pragma GCC diagnostic ignored "-Wsign-conversion"
 #pragma GCC diagnostic ignored "-Wfloat-equal"
 #pragma GCC diagnostic ignored "-Wshadow"
 #pragma GCC diagnostic ignored "-Wcast-align"
#endif
#define TSF_IMPLEMENTATION
#include "tsf/tsf.h"
#if defined (__clang__)
 #pragma clang diagnostic pop
#elif defined (__GNUC__)
 #pragma GCC diagnostic pop
#endif

namespace atlas::importer
{
std::vector<Found> convertSoundFont (const juce::File& sf2, const juce::File& outDir, juce::StringArray& log, const std::function<bool (const juce::String&)>& wanted)
{
    std::vector<Found> out;
    tsf* font = tsf_load_filename (sf2.getFullPathName().toRawUTF8());
    if (font == nullptr) { log.add ("  " + sf2.getFileName() + ": not a SoundFont 2 file that can be read"); return out; }
    const auto sampleDir = outDir.getChildFile ("samples");
    sampleDir.createDirectory();
    juce::WavAudioFormat wav;
    std::map<juce::String, bool> written;
    int skippedMod = 0;

    for (int p = 0; p < font->presetNum; ++p)
    {
        const tsf_preset& preset = font->presets[p];
        if (preset.regionNum <= 0) continue;
        const juce::String presetName = juce::String (preset.presetName).trim();
        if (wanted && ! wanted (presetName)) continue;
        juce::String t;
        t << "// " << presetName << "  (bank " << (int) preset.bank << ", preset " << (int) preset.preset << " of " << sf2.getFileName() << ")\n"
          << "// Converted by Ghost Atlas with TinySoundFont's reader. The SoundFont file is not changed.\n<control> default_path=samples/\n";
        int regions = 0;
        for (int r = 0; r < preset.regionNum; ++r)
        {
            const tsf_region& g = preset.regions[r];
            if (g.end <= g.offset || g.sample_rate == 0) continue;
            const juce::String name = "s" + juce::String ((juce::int64) g.offset) + "_" + juce::String ((juce::int64) (g.end - g.offset)) + "_" + juce::String ((int) g.sample_rate) + ".wav";
            if (! written[name])
            {
                const auto file = sampleDir.getChildFile (name);
                if (! file.existsAsFile())
                {
                    const int n = (int) (g.end - g.offset);
                    juce::AudioBuffer<float> buf (1, n);
                    buf.copyFrom (0, 0, font->fontSamples + g.offset, n);
                    if (auto stream = file.createOutputStream())
                    {
                        std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (stream.get(), (double) g.sample_rate, 1, 16, {}, 0));
                        if (w != nullptr) { stream.release(); w->writeFromAudioSampleBuffer (buf, 0, n); }
                    }
                }
                written[name] = true;
            }
            t << "<region> sample=" << name << " lokey=" << (int) g.lokey << " hikey=" << (int) g.hikey << " lovel=" << (int) g.lovel << " hivel=" << (int) g.hivel
              << " pitch_keycenter=" << g.pitch_keycenter;
            if (g.tune != 0) t << " tune=" << g.tune;
            if (g.transpose != 0) t << " transpose=" << g.transpose;
            if (g.pitch_keytrack != 100) t << " pitch_keytrack=" << g.pitch_keytrack;
            if (std::abs (g.attenuation) > 0.01f) t << " volume=" << juce::String (-g.attenuation, 2);
            if (std::abs (g.pan) > 0.001f) t << " pan=" << juce::String (juce::jlimit (-100.0f, 100.0f, g.pan * 200.0f), 1);
            if (g.loop_mode != TSF_LOOPMODE_NONE && g.loop_end > g.loop_start && g.loop_start >= g.offset && g.loop_end <= g.end)
                t << " loop_mode=" << (g.loop_mode == TSF_LOOPMODE_SUSTAIN ? "loop_sustain" : "loop_continuous") << " loop_start=" << (juce::int64) (g.loop_start - g.offset) << " loop_end=" << (juce::int64) (g.loop_end - g.offset - 1);
            const auto& e = g.ampenv;
            if (e.delay > 0.0005f) t << " ampeg_delay=" << juce::String (e.delay, 4);
            if (e.attack > 0.0005f) t << " ampeg_attack=" << juce::String (e.attack, 4);
            if (e.hold > 0.0005f && e.keynumToHold == 0.0f) t << " ampeg_hold=" << juce::String (e.hold, 4);
            if (e.decay > 0.0005f && e.keynumToDecay == 0.0f) t << " ampeg_decay=" << juce::String (e.decay, 4);
            t << " ampeg_sustain=" << juce::String (juce::jlimit (0.0f, 100.0f, e.sustain * 100.0f), 2);
            t << " ampeg_release=" << juce::String (juce::jmax (0.001f, e.release), 4);
            if (g.group != 0) t << " group=" << (juce::int64) g.group << " off_by=" << (juce::int64) g.group;
            if (g.initialFilterFc < 13500 && g.initialFilterFc > 1500)
                t << " fil_type=lpf_2p cutoff=" << juce::String (8.176 * std::pow (2.0, g.initialFilterFc / 1200.0), 1) << " resonance=" << juce::String (g.initialFilterQ / 10.0, 1);
            if (g.modLfoToPitch != 0 || g.vibLfoToPitch != 0 || g.modEnvToPitch != 0 || g.modEnvToFilterFc != 0 || g.modLfoToFilterFc != 0 || g.modLfoToVolume != 0) ++skippedMod;
            t << "\n";
            ++regions;
        }
        if (regions == 0) continue;
        const auto file = outDir.getChildFile (juce::String (preset.bank).paddedLeft ('0', 3) + "-" + juce::String (preset.preset).paddedLeft ('0', 3) + " " + safeName (presetName) + ".sfz");
        file.replaceWithText (t);
        Found f;
        f.sfz = file; f.name = presetName.isEmpty() ? file.getFileNameWithoutExtension() : presetName; f.format = "sf2";
        f.notes.add ("converted from the SoundFont " + sf2.getFileName());
        out.push_back (f);
    }
    if (skippedMod > 0) log.add ("  " + sf2.getFileName() + ": " + juce::String (skippedMod) + " zones use SoundFont vibrato / filter-envelope settings that were not converted (the samples, mapping, loops and volume envelopes were)");
    tsf_close (font);
    return out;
}
} // namespace atlas::importer
