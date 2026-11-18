// SPDX-License-Identifier: AGPL-3.0-or-later
// GHOST ATLAS - the checks, and the command-line tool the build machine and the sound design use.
//
//   GhostAtlasTests                         run every check (exit 0 = all passed)
//   GhostAtlasTests index [--no-analyse]    index everything under <home>/Library and listen to it
//   GhostAtlasTests import <folder> [--link]  import a folder (copied into the library unless --link)
//   GhostAtlasTests analyse [--all]         listen to instruments that have not been analysed (or all)
//   GhostAtlasTests list                    every instrument with what the analysis heard (tab separated)
//   GhostAtlasTests manifest                where every sound comes from (also written to <home>/Reports)
//   GhostAtlasTests render <preset> <out.wav> [chord|arp|line|note] [seconds]
//   GhostAtlasTests shots <folder> [width]  pictures of every page of the screen, as it is drawn
//   GhostAtlasTests mutate <preset> <subtle|evolve|wild> [seed]   one mutation: what was done, what was heard, before.wav / after.wav
//   GhostAtlasTests experiment <folder> [count]   MUTATE against random layering over many instruments, with audio and a report
//
// <home> is ~/Music/GhostBlock/Ghost Atlas, or $GHOST_ATLAS_HOME.
#include "plugin/Processor.h"
#include "plugin/Editor.h"
#include "ui/Theme.h"
#include "ui/Film.h"
#include "engine/SpectralRack.h"
#include "rack/EffectSlot.h"
#include "ui/Pages.h"
#include "core/Import.h"
#include "core/Analysis.h"
#include "core/Dsp.h"
#include "core/Timbre.h"
#include "core/Mutate.h"
#include "core/Reimagine.h"
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <functional>
#include <typeinfo>

using namespace atlas;

namespace
{
int failures = 0, passed = 0;
void check (bool ok, const juce::String& what)
{
    std::printf ("%s %s\n", ok ? "  ok  " : "  FAIL", what.toRawUTF8());
    std::fflush (stdout);
    if (ok) ++passed; else ++failures;
}
void section (const char* name) { std::printf ("\n== %s\n", name); std::fflush (stdout); }

bool writeWav (const juce::File& f, const juce::AudioBuffer<float>& b, double fs, int bits = 24, const juce::StringPairArray& meta = {})
{
    f.getParentDirectory().createDirectory();
    f.deleteFile();
    juce::WavAudioFormat wav;
    std::unique_ptr<juce::FileOutputStream> os (f.createOutputStream());
    if (os == nullptr) return false;
    std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os.get(), fs, (unsigned int) b.getNumChannels(), bits, meta, 0));
    if (w == nullptr) return false;
    os.release();
    return w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
}

// a test tone: a few harmonics with a soft start and a slow fall (kind 0 dull, 1 bright)
juce::AudioBuffer<float> tone (double hz, double seconds, double fs, int kind, float level = 0.5f)
{
    const int n = (int) (seconds * fs);
    juce::AudioBuffer<float> b (2, n);
    const int harmonics = kind == 1 ? 14 : 3;
    for (int i = 0; i < n; ++i)
    {
        const double t = (double) i / fs;
        double s = 0;
        for (int h = 1; h <= harmonics; ++h) if (hz * h < fs * 0.45) s += std::sin (juce::MathConstants<double>::twoPi * hz * h * t) / (kind == 1 ? (double) h : (double) (h * h));
        const double env = juce::jmin (1.0, t / 0.02) * std::exp (-t * 0.25);
        const float v = (float) (s * env) * level;
        b.setSample (0, i, v); b.setSample (1, i, v * 0.9f);
    }
    return b;
}

float pitchOfStretch (const juce::AudioBuffer<float>& b, double fs, double from, double length)
{
    const auto m = measure::monoOf (b, (int) (from * fs), (int) (length * fs));
    float clarity = 0;
    return measure::pitchOf (m.data(), (int) m.size(), fs, clarity);
}
float rmsDb (const juce::AudioBuffer<float>& b, double fs, double from, double length)
{
    return gainToDb (juce::jmax (1.0e-9f, measure::rmsOf (b, (int) (from * fs), (int) (length * fs))));
}
// the largest jump between neighbouring samples, against the level around it: a click stands far above a tone's own slope
float worstJump (const juce::AudioBuffer<float>& b)
{
    float worst = 0;
    for (int c = 0; c < b.getNumChannels(); ++c) { const float* x = b.getReadPointer (c); for (int i = 1; i < b.getNumSamples(); ++i) worst = juce::jmax (worst, std::abs (x[i] - x[i - 1])); }
    return worst;
}
// A coarse picture of a sound: its loudness in twelve bands of most of an octave each, every tenth of a second (dB).
// Two pictures are compared where either is within 60 dB of the first one's loudest cell; the answer is their
// root-mean-square difference in dB. A delay of a few milliseconds does not show in it; a change of tone, level,
// rhythm or tail does.
std::vector<float> soundPicture (const juce::AudioBuffer<float>& a, double fs)
{
    const int hop = (int) (fs * 0.1), frames = a.getNumSamples() / hop;
    std::vector<float> out ((size_t) frames * 12, -120.0f);
    juce::dsp::FFT fft (12);
    std::vector<float> buf (8192, 0.0f);
    for (int f = 0; f < frames; ++f)
    {
        std::fill (buf.begin(), buf.end(), 0.0f);
        for (int i = 0; i < 4096; ++i)
        {
            const int at = f * hop + i - 2048;
            if (at < 0 || at >= a.getNumSamples()) continue;
            const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / 4095.0f);
            buf[(size_t) i] = 0.5f * (a.getSample (0, at) + a.getSample (1, at)) * w;
        }
        fft.performFrequencyOnlyForwardTransform (buf.data());
        for (int b = 0; b < 12; ++b)
        {
            const double lo = 20.0 * std::pow (2.0, (double) b * 0.83), hi = lo * std::pow (2.0, 0.83);
            double e = 0;
            for (int k = juce::jmax (1, (int) (lo * 4096.0 / fs)); k < juce::jmin (2048, (int) (hi * 4096.0 / fs) + 1); ++k) e += (double) buf[(size_t) k] * buf[(size_t) k];
            out[(size_t) (f * 12 + b)] = (float) (10.0 * std::log10 (e / (4096.0 * 4096.0) + 1.0e-12));
        }
    }
    return out;
}
float picturesApart (const std::vector<float>& a, const std::vector<float>& b)
{
    float top = -120.0f; for (float v : a) top = juce::jmax (top, v);
    double sum = 0; int n = 0;
    for (size_t i = 0; i < a.size() && i < b.size(); ++i)
        if (a[i] > top - 60.0f || b[i] > top - 60.0f) { const double d = juce::jlimit (-40.0f, 40.0f, a[i] - b[i]); sum += d * d; ++n; }
    return n > 0 ? (float) std::sqrt (sum / n) : 0.0f;
}

bool allFinite (const juce::AudioBuffer<float>& b)
{
    for (int c = 0; c < b.getNumChannels(); ++c) { const float* x = b.getReadPointer (c); for (int i = 0; i < b.getNumSamples(); ++i) if (! std::isfinite (x[i])) return false; }
    return true;
}

Preset onePreset (const juce::String& instrument)
{
    Preset p = Preset::blank();
    for (auto& s : p.sends) s.on = false;
    for (auto& m : p.master) m.on = false;
    p.layers[0].instrument = instrument;
    p.layers[0].p[lpOn] = 1.0f;
    return p;
}

Progress printer()
{
    return [last = std::make_shared<juce::String>()] (float f, const juce::String& what) {
        if (what != *last) { std::printf ("  [%3d%%] %s\n", (int) (f * 100.0f), what.toRawUTF8()); std::fflush (stdout); *last = what; }
        return true;
    };
}

// ============================================================================================ the checks
int runChecks()
{
    // everything happens in a throw-away home, so a real library is never touched
    const auto home = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ghost-atlas-checks-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
    home.createDirectory();
    setenv ("GHOST_ATLAS_HOME", home.getFullPathName().toRawUTF8(), 1);
    const double fs = 44100.0;

    // ---- a small library made here: loose samples named the way libraries name them, and one SFZ
    const auto drop = home.getChildFile ("dropped").getChildFile ("Test Sounds");
    {
        for (int note : { 36, 48, 60, 72, 84 })
            writeWav (drop.getChildFile ("Glass Pad").getChildFile ("GlassPad_" + juce::MidiMessage::getMidiNoteName (note, true, true, 4) + ".wav"), tone (measure::midiToHz ((float) note), 4.0, fs, 1), fs);
        for (int note : { 57, 69 })
        {
            writeWav (drop.getChildFile ("Soft Keys").getChildFile ("SoftKeys_" + juce::MidiMessage::getMidiNoteName (note, true, true, 4) + "_p.wav"), tone (measure::midiToHz ((float) note), 3.0, fs, 0, 0.2f), fs);
            writeWav (drop.getChildFile ("Soft Keys").getChildFile ("SoftKeys_" + juce::MidiMessage::getMidiNoteName (note, true, true, 4) + "_f.wav"), tone (measure::midiToHz ((float) note), 3.0, fs, 1, 0.6f), fs);
        }
        drop.getChildFile ("Sine Choir.sfz").replaceWithText ("<group> ampeg_attack=0.05 ampeg_release=0.6\n<region> sample=*sine lokey=0 hikey=127 pitch_keycenter=69 some_made_up_opcode=3\n");
        drop.getChildFile ("LICENSE.txt").replaceWithText ("These test sounds are released under CC0 1.0 Universal (public domain dedication).\n");
    }
    auto sizeAndTime = [] (const juce::File& dir) { juce::String s; juce::Array<juce::File> fl; dir.findChildFiles (fl, juce::File::findFiles, true); fl.sort(); for (const auto& f : fl) s << f.getFileName() << ":" << f.getSize() << ":" << f.getLastModificationTime().toMilliseconds() << ";"; return s; };
    const auto before = sizeAndTime (drop);

    section ("reading sample names");
    {
        auto p = importer::parseSampleName (juce::File ("/x/VlnEns_Pizz_F#3_v2_rr1.wav"));
        check (p.note == 54 && p.roundRobin == 1 && p.velocityRank >= 0, "VlnEns_Pizz_F#3_v2_rr1 -> F#3, a velocity layer, round robin 1  (note " + juce::String (p.note) + ")");
        p = importer::parseSampleName (juce::File ("/x/Piano.ff.C4.wav"));
        check (p.note == 60 && p.velocityRank >= 0, "Piano.ff.C4 -> C4 with a dynamic  (note " + juce::String (p.note) + ")");
        p = importer::parseSampleName (juce::File ("/x/glass3_Db2_Slow.wav"));
        check (p.note == 37, "glass3_Db2_Slow -> Db2  (note " + juce::String (p.note) + ")");
        p = importer::parseSampleName (juce::File ("/x/kick hard.wav"));
        check (p.note < 0, "\"kick hard\" has no note in its name");
    }

    section ("importing a folder");
    Library lib;
    const auto rep = lib.importFolder (drop, true, {});
    std::printf ("%s\n", rep.text().toRawUTF8());
    const auto all = lib.all();
    juce::String glassId, keysId, sineId;
    for (const auto& i : all) { if (i.name.containsIgnoreCase ("glass")) glassId = i.id; if (i.name.containsIgnoreCase ("keys")) keysId = i.id; if (i.name.containsIgnoreCase ("sine")) sineId = i.id; }
    check (all.size() == 3 && glassId.isNotEmpty() && keysId.isNotEmpty() && sineId.isNotEmpty(), "three instruments came out of it (" + juce::String ((int) all.size()) + ")");
    check (before == sizeAndTime (drop), "the dropped folder was not changed in any way");
    Instrument glass, keys, sine;
    lib.find (glassId, glass); lib.find (keysId, keys); lib.find (sineId, sine);
    check (glass.format == "mapped" && glass.samples == 5 && glass.loKey == 24 && glass.hiKey == 96, "Glass Pad: five samples mapped from their names, an octave past the lowest and highest (" + juce::String (glass.samples) + " samples, keys " + juce::String (glass.loKey) + ".." + juce::String (glass.hiKey) + ")");
    check (keys.velocityLayers == 2, "Soft Keys: two velocity layers found from _p / _f (" + juce::String (keys.velocityLayers) + ")");
    check (glass.license.containsIgnoreCase ("CC0"), "the licence was read from the folder's own licence file (" + glass.license + ")");
    check (sine.unsupported.contains ("some_made_up_opcode"), "an SFZ setting the engine does not play is reported, not hidden (" + sine.unsupported.joinIntoString (",") + ")");
    check (glass.f.valid && std::abs (glass.f.tuneCents) < 8.0f, "analysis: Glass Pad is in tune (" + juce::String (glass.f.tuneCents, 1) + " cents)");
    check (glass.f.brightness > sine.f.brightness + 0.1f, "analysis: the bright sound measures brighter than the sine (" + juce::String (glass.f.brightness, 2) + " vs " + juce::String (sine.f.brightness, 2) + ")");
    check (keys.f.velocityDb > 6.0f, "analysis: Soft Keys answers to velocity (" + juce::String (keys.f.velocityDb, 1) + " dB)");
    check (Timbre::blockFile (glassId).existsAsFile(), "Samplebrain's blocks were written for it");
    {
        Timbre::Index ix;
        const bool ok = ix.add (glassId) && ix.add (keysId) && ix.add (sineId);
        const double self = ix.distance (glassId, glassId), other = ix.distance (glassId, sineId);
        check (ok && self >= 0 && self < 1.0e-6 && other > self, "Samplebrain distance: a sound is nearest to itself (self " + juce::String (self, 4) + ", to the sine " + juce::String (other, 4) + ")");
    }
    {
        const auto again = lib.importFolder (drop, true, {});
        check (again.duplicates == 1 && lib.size() == 3, "dropping the same folder again is recognised as the same library");
    }
    check (databaseFile().existsAsFile(), "the database was saved");
    { Library reread; check (reread.load() && reread.size() == 3 && reread.sfzPath (glassId).isNotEmpty(), "and reads back, with relative paths that still resolve"); }

    const Engine::Resolver resolver = [&lib] (const juce::String& id) { return lib.sfzPath (id); };
    auto note = [] (int n, double start, double len, float vel = 0.8f) { return Note { n, vel, start, len }; };

    section ("the engine, offline");
    {
        auto p = onePreset (glassId);
        auto a = Engine::bounce (p, resolver, { note (69, 0.1, 1.5) }, 2.5, fs);
        const float hz = pitchOfStretch (a, fs, 0.5, 0.4);
        check (allFinite (a) && measure::peakOf (a) > 0.02f && measure::peakOf (a) < 1.0f, "one layer plays (peak " + juce::String (measure::peakOf (a), 3) + ")");
        check (std::abs (measure::centsBetween (hz, 440.0f)) < 6.0f, "A4 sounds at 440 Hz (" + juce::String (hz, 1) + ")");
        check (rmsDb (a, fs, 0.0, 0.09) < -80.0f, "nothing sounds before the note");

        p.layers[0].p[lpCoarse] = 12.0f;
        auto b = Engine::bounce (p, resolver, { note (69, 0.1, 1.5) }, 2.5, fs);
        check (std::abs (measure::centsBetween (pitchOfStretch (b, fs, 0.5, 0.4), 880.0f)) < 6.0f, "PITCH +12 plays an octave up");
        p.layers[0].p[lpCoarse] = 0.0f; p.layers[0].p[lpFine] = 50.0f;
        b = Engine::bounce (p, resolver, { note (69, 0.1, 1.5) }, 2.5, fs);
        const float fine = measure::centsBetween (pitchOfStretch (b, fs, 0.5, 0.4), 440.0f);
        check (std::abs (fine - 50.0f) < 6.0f, "FINE +50 raises it half a semitone (" + juce::String (fine, 1) + " cents)");
        p.layers[0].p[lpFine] = 0.0f;

        p.layers[0].p[lpKeyLo] = 72.0f;
        b = Engine::bounce (p, resolver, { note (69, 0.1, 1.5) }, 2.0, fs);
        check (measure::peakOf (b) < 1.0e-5f, "a note below the layer's key range is not played");
        p.layers[0].p[lpKeyLo] = 0.0f;

        p.layers[0].p[lpPan] = -1.0f;
        b = Engine::bounce (p, resolver, { note (69, 0.1, 1.5) }, 2.0, fs);
        check (b.getMagnitude (1, 0, b.getNumSamples()) < 0.02f * b.getMagnitude (0, 0, b.getNumSamples()), "PAN hard left leaves the right silent");
        p.layers[0].p[lpPan] = 0.0f;

        p.layers[0].p[lpGain] = -12.0f;
        b = Engine::bounce (p, resolver, { note (69, 0.1, 1.5) }, 2.0, fs);
        const float dropDb = rmsDb (a, fs, 0.4, 1.0) - rmsDb (b, fs, 0.4, 1.0);
        check (std::abs (dropDb - 12.0f) < 0.5f, "LEVEL -12 dB is 12 dB quieter (" + juce::String (dropDb, 2) + ")");
        p.layers[0].p[lpGain] = 0.0f;

        p.layers[0].p[lpFilterType] = 1.0f; p.layers[0].p[lpCutoff] = 600.0f;
        b = Engine::bounce (p, resolver, { note (69, 0.1, 1.5) }, 2.0, fs);
        const auto ma = measure::monoOf (a, (int) (0.4 * fs), (int) fs), mb = measure::monoOf (b, (int) (0.4 * fs), (int) fs);
        const auto sa = measure::spectrumOf (ma.data(), (int) ma.size(), fs), sb = measure::spectrumOf (mb.data(), (int) mb.size(), fs);
        check (sb.centroid < 0.6 * sa.centroid, "the low-pass filter darkens it (centroid " + juce::String (sa.centroid, 0) + " -> " + juce::String (sb.centroid, 0) + " Hz)");
        p.layers[0].p[lpFilterType] = 0.0f; p.layers[0].p[lpCutoff] = 20000.0f;

        p.layers[0].p[lpWidth] = 0.0f;
        b = Engine::bounce (p, resolver, { note (69, 0.1, 1.5) }, 2.0, fs);
        float diff = 0; for (int i = 0; i < b.getNumSamples(); ++i) diff = juce::jmax (diff, std::abs (b.getSample (0, i) - b.getSample (1, i)));
        check (diff < 1.0e-5f, "WIDTH 0 is mono");
        p.layers[0].p[lpWidth] = 1.0f;

        p.layers[0].p[lpMute] = 1.0f;
        b = Engine::bounce (p, resolver, { note (69, 0.1, 1.5) }, 2.0, fs);
        check (measure::peakOf (b) < 1.0e-6f, "MUTE silences the layer");
        p.layers[0].p[lpMute] = 0.0f;

        // two layers and solo
        p.layers[1].instrument = sineId; p.layers[1].p[lpOn] = 1.0f; p.layers[1].p[lpCoarse] = 7.0f;
        b = Engine::bounce (p, resolver, { note (69, 0.1, 1.5) }, 2.0, fs);
        check (rmsDb (b, fs, 0.4, 1.0) > rmsDb (a, fs, 0.4, 1.0) + 0.5f, "a second layer adds to the first");
        p.layers[1].p[lpSolo] = 1.0f;
        b = Engine::bounce (p, resolver, { note (69, 0.1, 1.5) }, 2.0, fs);
        check (std::abs (measure::centsBetween (pitchOfStretch (b, fs, 0.5, 0.4), measure::midiToHz (76.0f))) < 8.0f, "SOLO leaves only that layer (heard a fifth up)");
        p.layers[1] = LayerState();

        // attack and release shaping reach the instrument through the hooks
        p.layers[0].p[lpAttack] = 0.6f;
        b = Engine::bounce (p, resolver, { note (69, 0.1, 1.5) }, 2.5, fs);
        check (rmsDb (b, fs, 0.12, 0.15) < rmsDb (a, fs, 0.12, 0.15) - 10.0f, "ATTACK slows the start");
        p.layers[0].p[lpAttack] = 0.0f; p.layers[0].p[lpRelease] = 0.7f;
        b = Engine::bounce (p, resolver, { note (69, 0.1, 0.6) }, 3.0, fs);
        auto c = Engine::bounce (onePreset (glassId), resolver, { note (69, 0.1, 0.6) }, 3.0, fs);
        check (rmsDb (b, fs, 1.5, 0.5) > rmsDb (c, fs, 1.5, 0.5) + 12.0f, "RELEASE lengthens the tail");
        p.layers[0].p[lpRelease] = 0.0f;

        // reverse, one shot, forced loop: all need the instrument loaded again
        p.layers[0].reverse = true;
        b = Engine::bounce (p, resolver, { note (60, 0.1, 3.5) }, 4.0, fs);
        check (rmsDb (b, fs, 2.8, 0.6) > rmsDb (b, fs, 0.2, 0.6) + 2.0f, "REVERSE plays the sample backwards (it swells instead of falling)");
        p.layers[0].reverse = false;

        // BACKWARDS on an instrument with its own sustain loop does not click (owner, 2026-11-14): a sample that holds
        // steady where its loop is and dies away after it; turned round, the loop must still sit on the steady part
        {
            const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("atlas-reverse-loop");
            dir.deleteRecursively();
            dir.createDirectory();
            const int sr = 44100, n = 44100;
            juce::AudioBuffer<float> w (1, n);
            for (int i = 0; i < n; ++i)
            {
                const float amp = i < 30000 ? 0.5f : 0.5f * (float) (n - i) / (float) (n - 30000);
                w.setSample (0, i, amp * std::sin (2.0f * juce::MathConstants<float>::pi * (float) i / 100.0f)); // 441 Hz, 100 frames a period
            }
            {
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::OutputStream> os (dir.getChildFile ("loop.wav").createOutputStream().release());
                std::unique_ptr<juce::AudioFormatWriter> wr (wav.createWriterFor (os.get(), sr, 1, 24, {}, 0));
                if (wr != nullptr) { os.release(); wr->writeFromAudioSampleBuffer (w, 0, n); }
            }
            dir.getChildFile ("loop.sfz").replaceWithText ("<region> sample=loop.wav pitch_keycenter=69 loop_mode=loop_continuous loop_start=5000 loop_end=24999\n");
            const Engine::Resolver loopRes = [dir] (const juce::String&) { return dir.getChildFile ("loop.sfz").getFullPathName(); };
            auto lp = onePreset ("test-loop");
            lp.layers[0].reverse = true;
            auto r = Engine::bounce (lp, loopRes, { note (69, 0.1, 3.0) }, 3.4, fs);
            float jump = 0, peak = 0;
            for (int i = (int) (0.4 * fs); i < (int) (3.0 * fs); ++i)
            {
                jump = juce::jmax (jump, std::abs (r.getSample (0, i) - r.getSample (0, i - 1)));
                peak = juce::jmax (peak, std::abs (r.getSample (0, i)));
            }
            check (peak > 0.02f && jump < 0.2f * peak, "BACKWARDS keeps a looped instrument's loop seamless: no clicks (biggest step " + juce::String (jump / juce::jmax (1.0e-6f, peak), 3) + " of the peak)");
            dir.deleteRecursively();
        }

        p.layers[0].loopMode = 1;
        b = Engine::bounce (p, resolver, { note (60, 0.1, 9.0) }, 10.0, fs);
        c = Engine::bounce (onePreset (glassId), resolver, { note (60, 0.1, 9.0) }, 10.0, fs);
        check (rmsDb (b, fs, 8.0, 1.0) > -60.0f && rmsDb (c, fs, 8.0, 1.0) < -90.0f, "a forced loop keeps a four-second sample sounding at eight seconds (" + juce::String (rmsDb (b, fs, 8.0, 1.0), 1) + " dB; as made " + juce::String (rmsDb (c, fs, 8.0, 1.0), 1) + ")");
        p.layers[0].loopMode = 0;

        // key crossfade: silent at one end of the range, full at the other
        p.layers[0].p[lpKeyLo] = 60.0f;
        c = Engine::bounce (p, resolver, { note (61, 0.1, 1.0), note (74, 1.5, 1.0) }, 3.0, fs);
        p.layers[0].keyFadeIn = 12.0f;
        b = Engine::bounce (p, resolver, { note (61, 0.1, 1.0), note (74, 1.5, 1.0) }, 3.0, fs);
        const float lowDrop = rmsDb (c, fs, 0.3, 0.6) - rmsDb (b, fs, 0.3, 0.6), highDrop = rmsDb (c, fs, 1.7, 0.6) - rmsDb (b, fs, 1.7, 0.6);
        check (lowDrop > 5.0f && std::abs (highDrop) < 0.5f, "a key crossfade brings the layer in gradually above its low key (" + juce::String (lowDrop, 1) + " dB down near the edge, " + juce::String (highDrop, 1) + " past the fade)");
        p.layers[0].p[lpKeyLo] = 0.0f; p.layers[0].keyFadeIn = 0.0f;

        // TOUCH: at 0 every note is the same strength
        p.layers[0].p[lpVelSens] = 0.0f;
        b = Engine::bounce (p, resolver, { note (69, 0.1, 0.8, 0.2f), note (69, 1.5, 0.8, 1.0f) }, 3.0, fs);
        check (std::abs (rmsDb (b, fs, 0.3, 0.5) - rmsDb (b, fs, 1.7, 0.5)) < 1.0f, "TOUCH 0 plays soft and hard notes alike");
        p.layers[0].p[lpVelSens] = 1.0f;

        // a macro moves what it is aimed at
        p.layers[0].p[lpFilterType] = 1.0f; p.layers[0].p[lpCutoff] = 20000.0f;
        p.macros[0].targets = { { "L1.cutoff", -0.6f } };
        p.macros[0].value = 1.0f;
        b = Engine::bounce (p, resolver, { note (69, 0.1, 1.5) }, 2.0, fs);
        const auto mm = measure::monoOf (b, (int) (0.4 * fs), (int) fs);
        check (measure::spectrumOf (mm.data(), (int) mm.size(), fs).centroid < 0.7 * sa.centroid, "a macro aimed at the cutoff closes the filter");
        p.macros[0].targets.clear(); p.macros[0].value = 0.0f; p.layers[0].p[lpFilterType] = 0.0f;

        // the LFO: tremolo at full depth leaves gaps
        p.layers[0].p[lpLfoAmp] = 1.0f; p.layers[0].p[lpLfoRate] = 4.0f;
        b = Engine::bounce (p, resolver, { note (69, 0.1, 2.5) }, 3.0, fs);
        { const auto env = measure::envelopeOf (measure::monoOf (b, (int) (0.5 * fs), (int) (1.5 * fs)), fs, 10.0); float lo = 1.0e9f, hi = 0; for (float v : env) { lo = juce::jmin (lo, v); hi = juce::jmax (hi, v); }
          check (hi > 0 && lo < 0.25f * hi, "the LFO moves the level (lowest " + juce::String (lo / juce::jmax (1.0e-9f, hi), 2) + " of the top)"); }
        p.layers[0].p[lpLfoAmp] = 0.0f;
    }

    section ("effects: the sends, the master chain, VOICE SHIFT");
    {
        auto dry = onePreset (glassId);
        auto wet = dry;
        wet.sends[2] = fxDefault ("halls"); wet.layers[0].p[lpSendC] = 0.8f;
        const auto a = Engine::bounce (dry, resolver, { note (69, 0.1, 0.4) }, 4.0, fs);
        const auto b = Engine::bounce (wet, resolver, { note (69, 0.1, 0.4) }, 4.0, fs);
        check (allFinite (b) && rmsDb (b, fs, 1.5, 1.0) > rmsDb (a, fs, 1.5, 1.0) + 6.0f, "a send into HAUNTED HALLS leaves a tail (" + juce::String (rmsDb (a, fs, 1.5, 1.0), 1) + " -> " + juce::String (rmsDb (b, fs, 1.5, 1.0), 1) + " dB)");
        wet.sends[2].on = false;
        const auto c = Engine::bounce (wet, resolver, { note (69, 0.1, 0.4) }, 4.0, fs);
        check (std::abs (rmsDb (c, fs, 0.2, 0.3) - rmsDb (a, fs, 0.2, 0.3)) < 0.2f, "a send that is switched off adds nothing (not even the dry signal)");

        auto loud = dry; loud.layers[0].p[lpGain] = 12.0f; loud.masterDb = 6.0f;
        loud.master[2] = fxDefault ("lim");
        const auto l = Engine::bounce (loud, resolver, { note (48, 0.1, 1.0), note (55, 0.1, 1.0), note (60, 0.1, 1.0), note (64, 0.1, 1.0), note (67, 0.1, 1.0) }, 2.0, fs);
        check (allFinite (l) && measure::peakOf (l) > 0.3f && measure::peakOf (l) <= 1.0f, "the master limiter has the last word: +18 dB of level still peaks at " + juce::String (measure::peakOf (l), 3));

        auto sh = dry;
        sh.layers[0].inserts[0] = fxDefault ("shift"); sh.layers[0].inserts[0].values["pitch"] = 7.0f;
        const auto s = Engine::bounce (sh, resolver, { note (69, 0.1, 2.0) }, 3.0, fs);
        const float hz = pitchOfStretch (s, fs, 0.9, 0.5);
        check (std::abs (measure::centsBetween (hz, measure::midiToHz (76.0f))) < 20.0f, "VOICE SHIFT +7 raises the layer a fifth (" + juce::String (hz, 1) + " Hz)");
        check (! fxParams ("shift").empty() && fxUnits().size() == 26, "26 effect units can sit in a slot (Phantom FX's 24, VOICE SHIFT and SPECTRAL FX)");

        // ---- every unit is heard as it arrives, on a held sound (a unit that works in time needs something to work on).
        //      Two wait for a hand: the equaliser arrives flat, and the limiter only holds back what is too loud.
        {
            auto held = dry; held.layers[0].loopMode = 1;
            const std::vector<Note> phrase = { note (40, 0.1, 1.0), note (57, 0.1, 1.0), note (64, 0.1, 1.0), note (45, 1.3, 0.3), note (69, 1.3, 0.3), note (72, 1.8, 0.3) };
            auto pictureOf = [&] (const Preset& q) { return soundPicture (Engine::bounce (q, resolver, phrase, 3.0, fs), fs); };
            const auto was = pictureOf (held);
            juce::String silent, faintest; float least = 1000.0f; int heardUnits = 0;
            for (const auto& u : fxUnits())
            {
                auto q = held; q.layers[0].inserts[0] = fxDefault (u.key);
                const float d = picturesApart (was, pictureOf (q));
                if (d > 0.25f) { ++heardUnits; if (d < least) { least = d; faintest = u.key; } }
                else if (u.key != "eq" && u.key != "lim") silent << u.key << " ";
            }
            check (silent.isEmpty() && heardUnits >= 23, "every unit is heard as it arrives, but for the two that may wait for a hand: the equaliser (flat) and the limiter (" + juce::String (heardUnits)
                   + " heard, the faintest " + faintest + " at " + juce::String (least, 1) + " dB apart" + (silent.isEmpty() ? juce::String() : "; not heard: " + silent.trim()) + ")");
            auto q = held; q.layers[0].inserts[0] = fxDefault ("eq"); q.layers[0].inserts[0].values["2_gain"] = 12.0f;
            const float eqApart = picturesApart (was, pictureOf (q));
            q = held; q.layers[0].inserts[0] = fxDefault ("lim"); q.layers[0].inserts[0].values["gain"] = 18.0f;
            const float limApart = picturesApart (was, pictureOf (q));
            check (eqApart > 1.0f && limApart > 1.0f, "and those two are heard once a gain of theirs is turned (" + juce::String (eqApart, 1) + " and " + juce::String (limApart, 1) + " dB apart)");
        }

        // ---- a layer whose effect makes it speak late (VOICE SHIFT: 120 ms) is met by the others while the host's
        //      timeline runs; played by hand with the host stopped, nothing is held back and only that layer trails
        {
            auto two = onePreset (glassId);
            two.layers[1] = two.layers[0];
            two.layers[1].inserts[0] = fxDefault ("shift");         // nothing moved: only the time it takes
            auto onset = [&] (int muted, bool onTimeline) {
                auto q = two; q.layers[(size_t) muted].p[lpMute] = 1.0f;
                const auto o = Engine::bounce (q, resolver, { note (69, 0.5, 0.5) }, 2.0, fs, true, onTimeline);
                const float top = measure::peakOf (o);
                for (int i = 0; i < o.getNumSamples(); ++i) if (std::abs (o.getSample (0, i)) > 0.25f * top) return (double) i / fs;
                return -1.0;
            };
            const double plainLive = onset (1, false), slowLive = onset (0, false), plainLine = onset (1, true), slowLine = onset (0, true);
            check (plainLive > 0.49 && plainLive < 0.56 && slowLive - plainLive > 0.085 && slowLive - plainLive < 0.16 && std::abs (slowLine - plainLine) < 0.03 && std::abs (slowLine - slowLive) < 0.005,
                   "a layer through VOICE SHIFT speaks " + juce::String ((slowLive - plainLive) * 1000.0, 0) + " ms after the other when played by hand, and "
                   + juce::String (std::abs (slowLine - plainLine) * 1000.0, 0) + " ms from it on the host's timeline (the plain layer at " + juce::String (plainLive * 1000.0, 0) + " ms by hand, "
                   + juce::String (plainLine * 1000.0, 0) + " ms on the timeline)");
            Engine e;
            e.setResolver (resolver);
            e.setOffline (true);
            e.setPreset (two);
            e.prepare (fs);
            juce::AudioBuffer<float> one (2, 512); juce::MidiBuffer none; phantom::TempoInfo t; t.sampleRate = fs;
            one.clear(); e.process (one, none, t);
            const int with = e.layersHeldBackBy(), told = e.latencySamples();
            e.setFx (FxAddress::insert (1, 0), FxState());
            one.clear(); e.process (one, none, t);
            check (with > (int) (0.1 * fs) && with < (int) (0.14 * fs) && told >= with && e.layersHeldBackBy() == 0,
                   "the host is told of that much more latency (" + juce::String (with) + " samples, " + juce::String (1000.0 * with / fs, 0) + " ms), and of none of it once the effect is taken away");
        }

        // ---- SPECTRAL FX: Poltergeist 2's rack (Spectral Corruptor's 38 effects in a chain of four, and WARP CORE) in a slot
        {
            auto effectNamed = [] (const char* name) { for (int e = 1; e < spectral::numEffects(); ++e) if (spectral::effectName (e) == name) return e; return 0; };
            // on its own: an effect that works on the window and changes nothing yet (the equaliser, flat) gives the sound back as late as the slot says, and whole
            int says = -1, lies = -1; double under = 0.0; size_t controls = 0;
            {
                const double rate = 48000.0; const int block = 512, total = 48000;
                phantom::EffectSlot slot (makeSpectralRack());
                const auto specs = slot.params(); controls = specs.size();
                std::map<juce::String, float> v;
                for (const auto& sp : specs) v[juce::String (sp.id)] = sp.def;
                spectral::put (v, 0, effectNamed ("Spectral Equalizer"));
                for (size_t i = 0; i < specs.size(); ++i) slot.setValue ((int) i, v[juce::String (specs[i].id)]);
                slot.setEnabled (true); slot.setMix (1.0f);
                slot.prepare (rate, block, 2); slot.applyPolicy();
                says = slot.getLatencySamples();
                juce::AudioBuffer<float> in (2, total), out;
                juce::Random rng (7);
                for (int i = 0; i < total; ++i) { const float x = i > 2000 ? (rng.nextFloat() * 2.0f - 1.0f) * 0.25f : 0.0f; in.setSample (0, i, x); in.setSample (1, i, 0.5f * x); }
                out.makeCopyOf (in);
                phantom::TempoInfo tempo; tempo.sampleRate = rate;
                for (int at = 0; at + block <= total; at += block) slot.process (juce::dsp::AudioBlock<float> (out.getArrayOfWritePointers(), 2, (size_t) at, (size_t) block), tempo);
                double least = 1.0e30;
                for (int lag = juce::jmax (0, says - 40); lag <= says + 40; ++lag)
                {
                    double e = 0.0;
                    for (int i = 20000; i < 24000; ++i) { const double d = out.getSample (0, i + lag) - in.getSample (0, i); e += d * d; }
                    if (e < least) { least = e; lies = lag; }
                }
                double e = 0.0, d0 = 0.0;
                for (int i = 20000; i < 40000; ++i) { const double d = out.getSample (0, i + lies) - in.getSample (0, i); e += d * d; d0 += (double) in.getSample (0, i) * in.getSample (0, i); }
                under = 10.0 * std::log10 (juce::jmax (1.0e-30, e) / d0);
            }
            check (controls == (size_t) spectral::kControls && spectral::numEffects() == 39 && spectral::folders().size() == 8 && says == 2047 && lies == says && under < -100.0,
                   "SPECTRAL FX is Poltergeist 2's rack, with its 38 effects in their eight folders; holding one that works on the window and changes nothing yet, it gives the sound back exactly as late as it says ("
                   + juce::String (says) + " samples; found " + juce::String (lies) + " late and " + juce::String (under, 0) + " dB from what went in)");

            // every effect, put in the first place of a rack on the layer: none breaks the sound, and each is heard at once but for the three that wait
            // for a hand (Frequency Remap until its points are moved, the equaliser until a band is, Utility until something is)
            auto held = dry; held.layers[0].loopMode = 1;
            const std::vector<Note> phrase = { note (40, 0.1, 1.0), note (57, 0.1, 1.0), note (64, 0.1, 1.0), note (45, 1.3, 0.3), note (69, 1.3, 0.3), note (72, 1.8, 0.3) };
            auto rackWith = [&] (int effect, bool warp) {
                auto q = held; q.layers[0].inserts[0] = fxDefault ("spectral");
                spectral::put (q.layers[0].inserts[0].values, 0, effect);
                q.layers[0].inserts[0].values["w_on"] = warp ? 1.0f : 0.0f;
                return q; };
            auto late = [] (const juce::AudioBuffer<float>& b, int by) {
                juce::AudioBuffer<float> o (b.getNumChannels(), b.getNumSamples()); o.clear();
                for (int c = 0; c < b.getNumChannels(); ++c) o.copyFrom (c, by, b, c, 0, b.getNumSamples() - by);
                return o; };
            const auto plain = Engine::bounce (held, resolver, phrase, 3.0, fs);
            const auto was = soundPicture (plain, fs), wasLate = soundPicture (late (plain, 2047), fs);
            juce::String unheard, broken; int heardCount = 0;
            for (int e = 1; e < spectral::numEffects(); ++e)
            {
                const auto o = Engine::bounce (rackWith (e, false), resolver, phrase, 3.0, fs);
                if (! allFinite (o)) broken << spectral::effectName (e) << "; ";
                if (picturesApart (spectral::usesWindow (e) ? wasLate : was, soundPicture (o, fs)) > 0.25f) ++heardCount; else unheard << spectral::effectName (e) << ", ";
            }
            const auto empty = Engine::bounce (rackWith (0, false), resolver, phrase, 3.0, fs);
            float most = 0.0f;
            for (int i = 0; i < plain.getNumSamples(); ++i) most = juce::jmax (most, std::abs (plain.getSample (0, i) - empty.getSample (0, i)));
            const float warped = picturesApart (was, soundPicture (Engine::bounce (rackWith (0, true), resolver, phrase, 3.0, fs), fs));
            check (broken.isEmpty() && heardCount >= 34 && most < 1.0e-6f && (warped > 1.0f || ! spectral::warpAvailable()),
                   juce::String (heardCount) + " of the 38 effects are heard as they are put in a place (not: " + (unheard.isEmpty() ? juce::String ("none") : unheard.dropLastCharacters (2))
                   + "), none breaks the sound" + (broken.isEmpty() ? juce::String() : " BUT " + broken) + "; a rack with nothing in it passes the sound untouched (" + juce::String (most, 7)
                   + " apart at most), and WARP CORE on its own is heard (" + juce::String (warped, 1) + " dB apart)");

            // on a layer its lateness is the layer's: by hand that layer trails, on the host's timeline the others wait for it
            {
                auto two = onePreset (glassId);
                two.layers[1] = two.layers[0];
                two.layers[1].inserts[0] = fxDefault ("spectral"); spectral::put (two.layers[1].inserts[0].values, 0, effectNamed ("Spectral Equalizer"));
                auto onset = [&] (int muted, bool onTimeline) {
                    auto q = two; q.layers[(size_t) muted].p[lpMute] = 1.0f;
                    const auto o = Engine::bounce (q, resolver, { note (69, 0.5, 0.5) }, 2.0, fs, true, onTimeline);
                    const float top = measure::peakOf (o);
                    for (int i = 0; i < o.getNumSamples(); ++i) if (std::abs (o.getSample (0, i)) > 0.25f * top) return (double) i / fs;
                    return -1.0;
                };
                const double plainLive = onset (1, false), rackLive = onset (0, false), plainLine = onset (1, true), rackLine = onset (0, true);
                check (rackLive - plainLive > 0.035 && rackLive - plainLive < 0.06 && std::abs (rackLine - plainLine) < 0.004,
                       "a layer through SPECTRAL FX speaks " + juce::String ((int) std::lround ((rackLive - plainLive) * 1000.0)) + " ms after the other by hand (its window), and "
                       + juce::String (std::abs (rackLine - plainLine) * 1000.0, 1) + " ms from it on the host's timeline");
            }

            // on the master the host is told, and told again when the window is no longer needed or is made smaller
            {
                Engine e;
                e.setResolver (resolver);
                e.setOffline (true);
                e.setPreset (onePreset (glassId));
                e.prepare (fs);
                juce::AudioBuffer<float> one (2, 512); juce::MidiBuffer none; phantom::TempoInfo t; t.sampleRate = fs;
                const auto without = [&] { one.clear(); e.process (one, none, t); return e.latencySamples(); }();          // (what the rest of the sound already takes)
                auto told = [&] { one.clear(); e.process (one, none, t); return e.latencySamples() - without; };
                e.setFx (FxAddress::master (0), fxDefault ("spectral"));                    // (it arrives with a Temporal Smear, which works on the window)
                const int arrives = told();
                e.setFxValue (FxAddress::master (0), "s1_fx", (float) effectNamed ("Reverb"));          // (works on the sound as it comes)
                const int onSamples = told();
                e.setFxValue (FxAddress::master (0), "s2_fx", (float) effectNamed ("Bin Shuffle"));
                e.setFxValue (FxAddress::master (0), "fft", 2.0f);                                       // (a window of 512)
                const int small = told();
                const auto kept = FxState::fromVar (e.getFx (FxAddress::master (0)).toVar());
                check (arrives == 2047 && onSamples == 0 && small == 511 && kept == e.getFx (FxAddress::master (0)) && (int) std::lround (kept.values.at ("s2_fx")) == effectNamed ("Bin Shuffle"),
                       "on the master the host is told of the rack's window (" + juce::String (arrives) + " samples as it arrives, " + juce::String (onSamples)
                       + " once its only effect works on samples, " + juce::String (small) + " with a window of 512), and a saved sound keeps the rack as it stands");
            }
        }

        // ---- a note that ends at the instant it begins is not left sounding (sfizz 1.2.3 never lets go of a note whose
        //      end falls on the same sample as its start, anywhere but on a block's first sample: SfzLayer guards it)
        {
            Engine e;
            e.setResolver (resolver);
            e.setOffline (true);
            auto held = onePreset (sineId); held.layers[0].loopMode = 1;
            e.setPreset (held);
            e.prepare (fs);
            juce::AudioBuffer<float> one (2, 512); juce::MidiBuffer m; phantom::TempoInfo t; t.sampleRate = fs;
            m.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 100); m.addEvent (juce::MidiMessage::noteOff (1, 60), 100);
            float late = 0.0f;
            for (int k = 0; k < 400; ++k) { one.clear(); e.process (one, m, t); m.clear(); if (k >= 300) late = juce::jmax (late, one.getMagnitude (0, 512)); }
            // (the same instrument, held properly, is still sounding then: the check would notice a note that hangs)
            m.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 100);
            float holding = 0.0f;
            for (int k = 0; k < 400; ++k) { one.clear(); e.process (one, m, t); m.clear(); if (k >= 300) holding = juce::jmax (holding, one.getMagnitude (0, 512)); }
            check (late < 1.0e-4f && holding > 0.01f, "a note that ends at the very sample it begins is not left sounding (level three seconds on: " + juce::String (late, 5)
                   + "; a held note at that time: " + juce::String (holding, 3) + ")");
        }

        // every unit can be built, switched on, and run without producing anything unreasonable
        int bad = 0; juce::String which;
        for (const auto& u : fxUnits())
        {
            auto p = dry;
            p.layers[0].inserts[0] = fxDefault (u.key);
            const auto o = Engine::bounce (p, resolver, { note (60, 0.1, 0.6), note (67, 0.1, 0.6) }, 1.2, fs);
            if (! allFinite (o) || measure::peakOf (o) > 3.9f) { ++bad; which << u.key << " "; }
        }
        check (bad == 0, "every unit runs as a layer insert at its defaults" + (bad ? " (not: " + which + ")" : juce::String()));
    }

    section ("presets");
    {
        Preset p = onePreset (glassId);
        p.name = "Round Trip"; p.category = "Vocal Pads"; p.tags = { "airy", "slow" }; p.author = "checks";
        p.layers[0].p[lpCutoff] = 1234.0f; p.layers[0].loopMode = 1; p.layers[0].keyFadeIn = 5.0f; p.layers[0].velFadeOut = 20.0f; p.layers[0].inserts[1] = fxDefault ("chorus");
        p.layers[3].instrument = sineId; p.layers[3].p[lpOn] = 1.0f; p.layers[3].role = "shimmer";
        p.macros[2].targets = { { "L1.cutoff", 0.4f }, { "R3", 0.5f } }; p.macros[2].value = 0.25f;
        p.returns = { { 0.1f, 0.2f, 0.3f } }; p.masterDb = -3.0f;
        const auto text = juce::JSON::toString (p.toVar());
        const auto back = Preset::fromVar (parseJson (text));
        check (juce::JSON::toString (back.toVar()) == text, "a preset written and read back is the same preset");
        check (back.worldIndex() == 2, "its world follows its category when none is given (Vocal Pads -> " + juce::String (world (back.worldIndex()).name) + ")");
        PresetBank bank;
        const auto f = bank.saveUser (p);
        Preset loaded;
        check (f.existsAsFile() && bank.load (f.getFullPathName(), loaded) && loaded.name == "Round Trip" && ! loaded.factory, "user presets save to the Presets folder and load");
        bool listed = false; for (const auto& i : bank.list()) if (i.name == "Round Trip" && i.layers == 2 && ! i.factory) listed = true;
        check (listed, "and appear in the browser's list");
        {   // the factory sounds made round SPECTRAL FX (0.1.12, tagged "spectral"): each carries a rack that is on and holds something
            int tagged = 0, sound = 0; juce::StringArray without;
            if (auto* all = PresetBank::factoryData().getArray())
                for (const auto& v : *all)
                {
                    const auto fp = Preset::fromVar (v);
                    if (! fp.tags.contains ("spectral")) continue;
                    ++tagged;
                    bool has = false;
                    auto look = [&has] (const FxState& f)
                    {
                        if (f.unit != "spectral" || ! f.on) return;
                        auto at = [&f] (int index) { const auto it = f.values.find (spectral::idOf (index)); return it != f.values.end() ? it->second : 0.0f; };
                        bool holds = at (spectral::iWarpOn) > 0.5f, known = true;
                        for (int place = 0; place < spectral::kPlaces; ++place)
                        {
                            const int e = (int) std::lround (at (spectral::iEffect (place)));
                            known = known && e >= 0 && e < spectral::numEffects();
                            holds = holds || (e > 0 && at (spectral::iOn (place)) > 0.5f);
                        }
                        has = has || (holds && known);
                    };
                    for (const auto& l : fp.layers) for (const auto& f : l.inserts) look (f);
                    for (const auto& f : fp.sends) look (f);
                    for (const auto& f : fp.master) look (f);
                    if (has) ++sound; else without.add (fp.name);
                }
            check (tagged >= 11 && sound == tagged, "the factory sounds made round SPECTRAL FX each carry a rack that is on and holds an effect it knows (" + juce::String (sound) + " of "
                                                    + juce::String (tagged) + (without.isEmpty() ? juce::String() : "; not: " + without.joinIntoString (", ")) + ")");
        }

        // ---- twelve layers (eight until 0.1.10)
        check (kLayers == 12 && layerSlotsShown (-1) == 8 && layerSlotsShown (6) == 8 && layerSlotsShown (7) == 9 && layerSlotsShown (11) == 12,
               "a sound has twelve layers; the screen shows eight slots until the eighth is in use, then one more than the last in use");
        Preset high = onePreset (glassId);
        high.name = "Up High";
        high.layers[11] = high.layers[0]; high.layers[0] = LayerState();
        const auto eight = juce::JSON::toString (onePreset (glassId).toVar());
        const auto again = Preset::fromVar (parseJson (juce::JSON::toString (high.toVar())));
        // a sound saved when there were eight layers has eight in its file: it reads as it was, the other four empty
        auto oldFile = parseJson (eight);
        if (auto* layersThen = oldFile.getProperty ("layers", juce::var()).getArray()) while (layersThen->size() > 8) layersThen->removeLast();
        const auto old = Preset::fromVar (oldFile);
        check (again.layers[11].instrument == glassId && again.layers[11].used() && ! again.layers[0].used() && old.layers[0].instrument == glassId && ! old.layers[8].used() && ! old.layers[11].used(),
               "the twelfth layer is kept by a saved sound, and a sound saved with eight layers reads as it was");
        const auto top = Engine::bounce (high, resolver, { note (69, 0.1, 0.6) }, 1.2, fs);
        // a macro aimed at the eleventh layer moves that layer (its number was once the same as a send's return)
        Preset aimed = onePreset (glassId);
        aimed.layers[10] = aimed.layers[0]; aimed.layers[0] = LayerState();
        aimed.macros[0].targets = { { "L11.gain", -0.5f } };
        const auto asItIs = Engine::bounce (aimed, resolver, { note (69, 0.1, 0.6) }, 1.2, fs);
        aimed.macros[0].value = 1.0f;
        const auto turned = Engine::bounce (aimed, resolver, { note (69, 0.1, 0.6) }, 1.2, fs);
        check (rmsDb (top, fs, 0.15, 0.4) > -50.0f && rmsDb (asItIs, fs, 0.15, 0.4) > -50.0f && rmsDb (turned, fs, 0.15, 0.4) < rmsDb (asItIs, fs, 0.15, 0.4) - 20.0f,
               "the twelfth layer plays (" + juce::String (rmsDb (top, fs, 0.15, 0.4), 1) + " dB), and a macro aimed at the eleventh layer's level turns that layer down ("
               + juce::String (rmsDb (asItIs, fs, 0.15, 0.4), 1) + " -> " + juce::String (rmsDb (turned, fs, 0.15, 0.4), 1) + " dB)");
    }

    section ("the engine, live (loader thread, swaps while playing)");
    {
        Engine e;
        e.setResolver (resolver);
        e.setPreset (onePreset (glassId));
        e.prepare (48000.0);
        e.waitUntilLoaded();
        // (these checks run many times faster than real time, which is what a host does when it exports, so the engine is
        //  told what a host would tell it; playing in real time is checked further down)
        e.setHostOffline (true);
        juce::AudioBuffer<float> out (2, 48000 * 6), block (2, 256);
        out.clear();
        juce::MidiBuffer midi;
        phantom::TempoInfo tempo; tempo.sampleRate = 48000.0;
        int pos = 0, blocks = 0;
        auto run = [&] (int count) {
            for (int k = 0; k < count && pos + 256 <= out.getNumSamples(); ++k, ++blocks)
            {
                block.clear();
                e.process (block, midi, tempo);
                midi.clear();
                out.copyFrom (0, pos, block, 0, 0, 256); out.copyFrom (1, pos, block, 1, 0, 256);
                pos += 256;
            }
        };
        run (20);                                                   // lets the first swap happen
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
        run (150);
        const int beforeSwap = pos;
        check (measure::rmsOf (out, beforeSwap - 4800, 4800) > 0.005f && e.voices() > 0, "a held note sounds (voices " + juce::String (e.voices()) + ")");
        // a different instrument arrives while the note is held: the old one must fade, not stop dead
        auto p2 = onePreset (keysId);
        p2.sends[2] = fxDefault ("halls"); p2.layers[0].p[lpSendC] = 0.5f;
        e.setPreset (p2);
        for (int k = 0; k < 400 && e.loading(); ++k) { run (2); juce::Thread::sleep (2); }
        run (40);
        const int afterSwap = pos;
        midi.addEvent (juce::MidiMessage::noteOn (1, 69, 0.8f), 0);
        run (200);
        juce::AudioBuffer<float> around (2, afterSwap - beforeSwap + 2048);
        for (int c = 0; c < 2; ++c) around.copyFrom (c, 0, out, c, beforeSwap - 2048, around.getNumSamples());
        juce::AudioBuffer<float> steady (2, 4800);
        for (int c = 0; c < 2; ++c) steady.copyFrom (c, 0, out, c, beforeSwap - 4800, 4800);
        check (allFinite (out), "nothing but numbers came out");
        check (worstJump (around) < 3.0f * juce::jmax (0.004f, worstJump (steady)), "the swap does not click (largest step " + juce::String (worstJump (around), 4) + " against " + juce::String (worstJump (steady), 4) + " while steady)");
        check (measure::rmsOf (out, pos - 9600, 4800) > 0.003f, "the new instrument plays after the swap");
        check (e.layerProblem (0).isEmpty(), "and reports no problem");
        e.setLayerSource (0, [&] { auto l = p2.layers[0]; l.instrument = "nowhere:nothing"; return l; }());
        e.waitUntilLoaded();
        run (10);
        check (e.layerProblem (0).isNotEmpty(), "a layer whose instrument is missing says so (\"" + e.layerProblem (0) + "\")");
        // the host changes its rate: everything keeps working
        e.prepare (96000.0);
        e.setPreset (onePreset (glassId));
        e.waitUntilLoaded();
        tempo.sampleRate = 96000.0;
        juce::AudioBuffer<float> hi (2, 96000);
        hi.clear();
        midi.clear(); midi.addEvent (juce::MidiMessage::noteOn (1, 69, 0.8f), 0);
        for (int p = 0; p + 1024 <= hi.getNumSamples(); p += 1024)
        {
            juce::AudioBuffer<float> big (2, 1024);
            e.process (big, midi, tempo); midi.clear();
            hi.copyFrom (0, p, big, 0, 0, 1024); hi.copyFrom (1, p, big, 1, 0, 1024);
        }
        check (std::abs (measure::centsBetween (pitchOfStretch (hi, 96000.0, 0.5, 0.3), 440.0f)) < 6.0f, "after a change to 96 kHz (and 1024-sample blocks) A4 is still 440 Hz");
    }

    section ("effects, changed while a note is held (what the Effects page does)");
    {
        Engine e;
        e.setResolver (resolver);
        e.setPreset (onePreset (glassId));
        e.prepare (48000.0);
        e.waitUntilLoaded();
        e.setHostOffline (true);
        juce::AudioBuffer<float> block (2, 256);
        juce::MidiBuffer midi;
        phantom::TempoInfo tempo; tempo.sampleRate = 48000.0;
        // plays on for a while and says how loud the last part of it was (dB)
        auto run = [&] (int blocks, int measured) {
            double sum = 0; int n = 0;
            for (int k = 0; k < blocks; ++k)
            {
                block.clear();
                e.process (block, midi, tempo);
                midi.clear();
                if (k >= blocks - measured) { for (int c = 0; c < 2; ++c) for (int i = 0; i < 256; ++i) { const float v = block.getSample (c, i); sum += (double) v * v; ++n; } }
            }
            return gainToDb ((float) std::sqrt (sum / juce::jmax (1, n)) + 1.0e-9f);
        };
        auto settle = [&] { for (int k = 0; k < 600 && e.loading(); ++k) { run (2, 1); juce::Thread::sleep (2); } run (60, 1); };
        run (20, 1);
        // a fresh note each time (the test instrument does not sound for ever): how loud its first three quarters of a second are
        auto heard = [&] {
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
            run (30, 1);
            const float level = run (110, 110);
            midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
            run (220, 1);
            return level;
        };
        const float dry = heard();
        // an effect is chosen for the layer's first slot: the multi-band distortion, its first band turned right down
        auto quiet = fxDefault ("crackle"); quiet.values["b1_level"] = -24.0f; quiet.values["b1_mix"] = 1.0f; quiet.mix = 1.0f;
        e.setFx (FxAddress::insert (0, 0), quiet);
        settle();
        const float chosen = heard();
        e.setFxValue (FxAddress::insert (0, 0), "b1_level", 0.0f);
        const float turned = heard();
        e.setFxOn (FxAddress::insert (0, 0), false);
        const float off = heard();
        e.setFxOn (FxAddress::insert (0, 0), true); e.setFxValue (FxAddress::insert (0, 0), "b1_level", -24.0f);
        e.setFxMix (FxAddress::insert (0, 0), 0.0f);
        const float unmixed = heard();
        check (chosen < dry - 12.0f && turned > chosen + 12.0f && std::abs (off - dry) < 1.5f && std::abs (unmixed - dry) < 1.5f,
               "an effect chosen for a layer is heard at once, and so are its knob, its switch and its mix (dry " + juce::String (dry, 1) + " dB; chosen and turned down " + juce::String (chosen, 1)
               + "; turned back up " + juce::String (turned, 1) + "; switched off " + juce::String (off, 1) + "; mixed out " + juce::String (unmixed, 1) + ")");
        e.setFx (FxAddress::insert (0, 0), FxState());
        settle();
        // the same on the master
        e.setFx (FxAddress::master (0), quiet);
        settle();
        const float mastered = heard();
        e.setFxValue (FxAddress::master (0), "b1_level", 0.0f);
        const float masterTurned = heard();
        e.setFxOn (FxAddress::master (0), false);
        const float masterOff = heard();
        check (mastered < dry - 12.0f && masterTurned > mastered + 12.0f && std::abs (masterOff - dry) < 1.5f,
               "and on the master (" + juce::String (mastered, 1) + " dB with it turned down, " + juce::String (masterTurned, 1) + " turned back up, " + juce::String (masterOff, 1) + " with it switched off)");
        e.setFx (FxAddress::master (0), FxState());
        settle();
        // a send: nothing reaches it until the layer is sent to it; then its tail is heard after the note, and its return turns it down
        e.setFx (FxAddress::send (2), fxDefault ("halls"));
        settle();
        auto tailOf = [&] { midi.addEvent (juce::MidiMessage::noteOn (1, 64, 0.8f), 0); run (60, 1); midi.addEvent (juce::MidiMessage::noteOff (1, 64), 0); run (260, 1); const float t = run (60, 60); run (500, 1); return t; };
        e.setLayerParam (0, lpSendC, 0.0f);
        const float unsent = tailOf();
        e.setLayerParam (0, lpSendC, 0.8f);
        const float sent = tailOf();
        e.setReturn (2, 0.0f);
        const float returned = tailOf();
        check (sent > unsent + 12.0f && returned < sent - 12.0f, "a send is heard once a layer is sent to it, and its return level turns it down (tail " + juce::String (unsent, 1) + " dB unsent, "
               + juce::String (sent, 1) + " sent, " + juce::String (returned, 1) + " with the return down)");
        // ---- SPECTRAL FX on the master of that playing engine: chosen, turned and given another effect while notes sound
        {
            auto effectNamed = [] (const char* name) { for (int k = 1; k < spectral::numEffects(); ++k) if (spectral::effectName (k) == name) return k; return 0; };
            const float before = heard();
            const int toldBefore = e.latencySamples();
            auto rack = fxDefault ("spectral"); spectral::put (rack.values, 0, effectNamed ("Spectral Filter"));          // (a low-pass at 1 kHz)
            int freq = -1;
            for (int k = 0; k < spectral::numKnobs (effectNamed ("Spectral Filter")); ++k) if (spectral::knob (effectNamed ("Spectral Filter"), k).name == "Frequency") freq = k;
            e.setFx (FxAddress::master (0), rack);
            settle();
            const int toldWindow = e.latencySamples() - toldBefore;
            const float filtered = heard();
            e.setFxValue (FxAddress::master (0), spectral::idOf (spectral::iKnob (0, juce::jmax (0, freq))), 0.0f);        // its frequency right down, as the page sets it
            const float shut = heard();
            auto next = e.getFx (FxAddress::master (0)); spectral::put (next.values, 0, effectNamed ("Utility"));           // another effect in that place, as the page does it:
            e.setFxValue (FxAddress::master (0), "s1_fx", next.values["s1_fx"]);                                              // the effect, then where its knobs start
            for (int k = 0; k < spectral::kKnobs; ++k) { const auto id = spectral::idOf (spectral::iKnob (0, k)); e.setFxValue (FxAddress::master (0), id, next.values[id]); }
            settle();
            const float open = heard();
            const int toldNone = e.latencySamples() - toldBefore;
            e.setFx (FxAddress::master (0), FxState());
            settle();
            check (freq >= 0 && toldWindow == 2047 && filtered < before + 0.5f && shut < before - 20.0f && std::abs (open - before) < 1.0f && toldNone == 0,
                   "SPECTRAL FX on a playing engine: with its low-pass at 1 kHz the note stands at " + juce::String (filtered, 1) + " dB (" + juce::String (before, 1) + " without), turned right down it shuts ("
                   + juce::String (shut, 1) + "), and with another effect in its place the sound is back (" + juce::String (open, 1) + "); the host is told of " + juce::String (toldWindow)
                   + " samples more, then of " + juce::String (toldNone));
        }
    }

    section ("notes against the disk: played in real time, and exported faster than real time");
    {
        phantom::TempoInfo tempo; tempo.sampleRate = 48000.0;
        juce::AudioBuffer<float> block (2, 256);
        // how loud a note still is 0.8 to 1.0 s after it was struck (its recording is 4 s long: 0.17 s of it is in memory
        // when the note starts, and the rest has to arrive from the disk while it plays)
        auto lateLevel = [&] (Engine& e, int note, bool realTime) {
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, note, 0.8f), 0);
            juce::AudioBuffer<float> got (2, 48000);
            got.clear();
            for (int pos = 0; pos + 256 <= got.getNumSamples(); pos += 256)
            {
                const auto t0 = juce::Time::getMillisecondCounterHiRes();
                block.clear(); e.process (block, midi, tempo); midi.clear();
                got.copyFrom (0, pos, block, 0, 0, 256); got.copyFrom (1, pos, block, 1, 0, 256);
                if (realTime) while (juce::Time::getMillisecondCounterHiRes() - t0 < 256000.0 / 48000.0) juce::Thread::sleep (1);
            }
            midi.addEvent (juce::MidiMessage::noteOff (1, note), 0);
            for (int k = 0; k < 120; ++k) { block.clear(); e.process (block, midi, tempo); midi.clear(); }
            return measure::rmsOf (got, 38400, 9600);
        };
        {
            Engine e; e.setResolver (resolver); e.setPreset (onePreset (glassId)); e.prepare (48000.0); e.waitUntilLoaded();
            juce::MidiBuffer none; for (int k = 0; k < 20; ++k) { block.clear(); e.process (block, none, tempo); }
            const float level = lateLevel (e, 60, true);
            check (level > 0.005f, "played in real time, a note is still sounding a second later (level " + juce::String (level, 4) + ")");
        }
        int played = 0, whole = 0; float quietest = 1.0f;
        for (int round = 0; round < 6; ++round)
        {   // a new engine each round, so every note's recording has to come from the disk again
            Engine e; e.setResolver (resolver); e.setPreset (onePreset (glassId)); e.prepare (48000.0); e.waitUntilLoaded();
            e.setHostOffline (true);
            juce::MidiBuffer none; for (int k = 0; k < 20; ++k) { block.clear(); e.process (block, none, tempo); }
            for (int note : { 36, 48, 60, 72, 84 })
            {
                const float level = lateLevel (e, note, false);
                ++played; if (level > 0.005f) ++whole;
                quietest = juce::jmin (quietest, level);
            }
        }
        check (whole == played, "exported faster than real time, every note sounds in full (" + juce::String (whole) + " of " + juce::String (played) + "; the quietest at " + juce::String (quietest, 4) + ")");
        {   // an export that starts while an instrument is still loading waits for it: the first note is there
            Engine e; e.setResolver (resolver); e.prepare (48000.0); e.waitUntilLoaded();
            e.setHostOffline (true);
            e.setPreset (onePreset (glassId));                 // (asked for, and not waited for)
            const float level = lateLevel (e, 60, false);
            check (level > 0.005f, "an export that begins while a sound is still loading waits for it (the first note at " + juce::String (level, 4) + ")");
        }
    }

    section ("MUTATE and REIMAGINE");
    {
        // more to choose from: a second small library, of oscillator instruments (SFZ text only)
        const auto more = home.getChildFile ("dropped").getChildFile ("Wave Sounds");
        more.createDirectory();
        auto sfz = [&more] (const char* name, const char* body) { more.getChildFile (juce::String (name) + ".sfz").replaceWithText (body); };
        sfz ("Saw Pad", "<group> ampeg_attack=0.6 ampeg_release=1.0\n<region> sample=*saw lokey=24 hikey=96 pitch_keycenter=69 volume=-12\n");
        sfz ("Square Lead", "<group> ampeg_attack=0.005 ampeg_release=0.2\n<region> sample=*square lokey=24 hikey=96 pitch_keycenter=69 volume=-14\n");
        sfz ("Triangle Keys", "<group> ampeg_attack=0.003 ampeg_decay=2.0 ampeg_sustain=20 ampeg_release=0.4\n<region> sample=*triangle lokey=24 hikey=96 pitch_keycenter=69 volume=-8\n");
        sfz ("Saw Pluck", "<group> ampeg_attack=0.002 ampeg_decay=0.5 ampeg_sustain=0 ampeg_release=0.3\n<region> sample=*saw lokey=24 hikey=96 pitch_keycenter=69 volume=-10\n");
        sfz ("Slow Sine", "<group> ampeg_attack=1.2 ampeg_release=1.5\n<region> sample=*sine lokey=24 hikey=96 pitch_keycenter=69 volume=-6\n");
        sfz ("Soft Square", "<group> ampeg_attack=0.3 ampeg_release=0.8 fil_type=lpf_2p cutoff=1200\n<region> sample=*square lokey=24 hikey=96 pitch_keycenter=69 volume=-10\n");
        more.getChildFile ("LICENSE.txt").replaceWithText ("These test sounds are released under CC0 1.0 Universal (public domain dedication).\n");
        const auto rep = lib.importFolder (more, false);
        check (rep.added == 6, "six oscillator instruments join the library (" + juce::String (rep.added) + " added)");
        const Engine::Resolver resolver = [&lib] (const juce::String& id) { return lib.sfzPath (id); };
        Instrument pad;
        for (const auto& i : lib.all()) if (i.name == "Saw Pad") pad = i;
        check (pad.id.isNotEmpty() && pad.f.valid && std::abs (pad.f.tuneCents) < 3.0f, "an oscillator is heard on its key (" + juce::String (pad.f.tuneCents, 1) + " cents)");
        Preset start = onePreset (pad.id);
        start.name = "Check Pad"; start.layers[0].role = "primary"; start.layers[0].p[lpGain] = -6.0f;
        Mutator mutator (lib, resolver);
        const auto heard = mutator.listen (start);
        check (heard.ok && heard.holds && heard.peakDb < 0.0f, "MUTATE's ear hears a held sound as held, with headroom");
        {   // what stands on the master is part of the sound, and is heard; the limiter and the squeeze are not (the master level is set in front of them)
            int filter = 0;
            for (int e = 1; e < spectral::numEffects(); ++e) if (spectral::effectName (e) == "Spectral Filter") filter = e;
            Preset through = start;
            through.master[0] = fxDefault ("spectral");
            spectral::put (through.master[0].values, 0, filter);                                                         // a low pass ...
            through.master[0].values[spectral::idOf (spectral::iKnob (0, 1))] = spectral::toKept (filter, 1, 100.0f);    // ... at 100 Hz ...
            through.master[0].values[spectral::idOf (spectral::iKnob (0, 2))] = spectral::toKept (filter, 2, 3.0f);      // ... 48 dB an octave
            const auto shut = mutator.listen (through);
            Preset squeezed = start;
            squeezed.master[1] = fxDefault ("ott"); squeezed.master[1].on = true; squeezed.master[2] = fxDefault ("lim"); squeezed.master[2].on = true;
            const auto same = mutator.listen (squeezed);
            check (filter > 0 && shut.ok && shut.loudnessDb < heard.loudnessDb - 6.0f && std::abs (same.loudnessDb - heard.loudnessDb) < 0.05f,
                   "MUTATE's ear hears a rack on the master (a chord reads " + juce::String (shut.loudnessDb - heard.loudnessDb, 1) + " dB through a low pass at 100 Hz), and not the limiter or the squeeze ("
                   + juce::String (same.loudnessDb - heard.loudnessDb, 2) + " dB)");
        }
        const std::pair<MutateMode, const char*> modes[] = { { MutateMode::Subtle, "SUBTLE" }, { MutateMode::Evolve, "EVOLVE" }, { MutateMode::Wild, "WILD" } };
        for (const auto& md : modes)
        {
            const auto m = mutator.mutate (start, md.first, 7);
            if (! m.ok) { check (m.failure.isNotEmpty() && m.tried > 0, juce::String (md.second) + " says why when nothing holds up: " + m.failure.substring (0, 90)); continue; }
            bool allPassed = true; for (const auto& c : m.checks) allPassed = allPassed && c.passed;
            Instrument mainNow; lib.findLoose (m.preset.layers[(size_t) juce::jmax (0, mutator.mainLayer (m.preset))].instrument, mainNow);
            const bool mainRight = md.first == MutateMode::Wild ? mainNow.id != pad.id : mainNow.id == pad.id;
            check (allPassed && mainRight && m.preset.layerCount() >= (md.first == MutateMode::Wild ? 1 : 2) && std::abs (m.loudnessDb - (m.originalLoudnessDb + m.originalTrimDb)) < 1.0f && m.originalTrimDb <= 0.0f,
                   juce::String (md.second) + " returns a sound that passed every listening check, compared with the original at one loudness (" + juce::String (m.preset.layerCount()) + " layers, "
                   + juce::String (m.loudnessDb - (m.originalLoudnessDb + m.originalTrimDb), 1) + " dB apart, original trimmed " + juce::String (m.originalTrimDb, 1) + " dB, main: " + mainNow.name + ")");
        }
        {   // the same seed gives the same choice
            const auto a = mutator.mutate (start, MutateMode::Evolve, 21), b = mutator.mutate (start, MutateMode::Evolve, 21);
            bool same = a.ok == b.ok; for (int l = 0; l < kLayers && same; ++l) same = a.preset.layers[(size_t) l].instrument == b.preset.layers[(size_t) l].instrument;
            check (same, "MUTATE with the same seed makes the same choice");
        }
        const auto before = lib.size();
        const auto r = reimagine (lib, resolver, start, 3);
        if (r.ok)
        {
            Instrument made;
            check (lib.find (r.madeInstrument, made) && lib.sfzFile (made).existsAsFile() && made.f.valid && lib.size() == before + 1, "REIMAGINE's rebuilt sound is in the library as an instrument that plays");
            check (r.preset.layers[0].instrument == r.madeInstrument && std::abs (r.loudnessDb - (r.originalLoudnessDb + r.originalTrimDb)) < 1.0f, "the sound it returns plays that instrument, compared with the original at one loudness");
            const juce::File folder (r.madeFolder);
            check (discardReimagined (lib, r) && ! lib.find (r.madeInstrument, made) && ! folder.exists() && lib.size() == before, "put back, the instrument and its files are gone again");
        }
        else check (r.failure.isNotEmpty() && lib.size() == before, "REIMAGINE says why when nothing holds up, and leaves the library as it was: " + r.failure.substring (0, 90));
    }

    section ("recordings with no note names, and the mapping editor");
    {
        // four bursts of noise: nothing in their names or their sound says what key they belong on
        const auto hits = home.getChildFile ("dropped").getChildFile ("Hits");
        juce::Random r (5);
        for (const char* name : { "hit a", "hit b", "hit c", "hit d" })
        {
            juce::AudioBuffer<float> b (2, (int) (0.4 * fs));
            for (int i = 0; i < b.getNumSamples(); ++i) { const float v = (r.nextFloat() * 2.0f - 1.0f) * 0.4f * std::exp (-(float) i / (0.08f * (float) fs)); b.setSample (0, i, v); b.setSample (1, i, v); }
            writeWav (hits.getChildFile (juce::String (name) + ".wav"), b, fs);
        }
        const auto rep = lib.importFolder (hits, false);
        Instrument kit;
        for (const auto& i : lib.all()) if (i.name == "Hits") kit = i;
        auto map = lib.mappingOf (kit);
        check (rep.added == 1 && kit.format == "mapped" && map.size() == 4 && map[0].note == 36 && map[3].note == 39 && map[1].file.endsWith ("hit b.wav"),
               "recordings with no note names are each given a key, from C2 in file order (" + juce::String ((int) map.size()) + " recordings, first on " + juce::String (map.empty() ? -1 : map[0].note) + ")");
        const auto beforeHits = sizeAndTime (hits);
        Library::Mapped moved = map.size() > 1 ? map[1] : Library::Mapped();
        moved.note = 60;
        const auto rep2 = lib.remap (kit, { moved });
        Instrument kit2; lib.find (kit.id, kit2);
        map = lib.mappingOf (kit2);
        bool onC4 = false; for (const auto& m : map) onC4 = onC4 || (m.file.endsWith ("hit b.wav") && m.note == 60);
        check (rep2.failed == 0 && map.size() == 4 && onC4 && kit2.plays (60) && sizeAndTime (hits) == beforeHits,
               "the mapping editor moves a recording to another key, and the recordings themselves are not touched");
        Library again; again.load();
        const auto rep3 = again.importFolder (hits, false);
        juce::ignoreUnused (rep3);
        check (importer::readMapping (kit.libraryId).size() == 1, "the new key is remembered beside the library, not inside it");

        // a library whose record says each recording is an instrument of its own (a drone, a wind)
        const auto drones = home.getChildFile ("dropped").getChildFile ("Drones");
        {
            juce::AudioBuffer<float> hum (2, (int) (1.5 * fs)), wind (2, (int) (1.2 * fs));
            for (int i = 0; i < hum.getNumSamples(); ++i)
            {   // an A, two octaves below the one orchestras tune to, with a little of its octave
                const float v = 0.4f * std::sin (juce::MathConstants<float>::twoPi * 110.0f * (float) i / (float) fs) + 0.1f * std::sin (juce::MathConstants<float>::twoPi * 220.0f * (float) i / (float) fs);
                hum.setSample (0, i, v); hum.setSample (1, i, v);
            }
            for (int i = 0; i < wind.getNumSamples(); ++i) { const float v = (r.nextFloat() * 2.0f - 1.0f) * 0.3f; wind.setSample (0, i, v); wind.setSample (1, i, v); }
            writeWav (drones.getChildFile ("low hum.wav"), hum, fs);
            writeWav (drones.getChildFile ("wind.wav"), wind, fs);
            drones.getChildFile ("ATLAS-SOURCE.json").replaceWithText ("{ \"name\": \"Drones\", \"license\": \"CC0-1.0\", \"singles\": [ \"*.wav\" ], \"rename\": { \"Low Hum\": \"Hum In A\" } }");
        }
        const auto rep4 = lib.importFolder (drones, false);
        Instrument humInst, windInst;
        for (const auto& i : lib.all()) { if (i.name == "Hum In A") humInst = i; if (i.name == "Wind") windInst = i; }
        const auto humSfz = juce::File (lib.sfzPath (humInst.id)).loadFileAsString(), windSfz = juce::File (lib.sfzPath (windInst.id)).loadFileAsString();
        check (rep4.added == 2 && humSfz.contains ("pitch_keycenter=57") && humSfz.contains ("lokey=36 hikey=84") && windSfz.contains ("pitch_keycenter=60") && humInst.plays (36) && humInst.plays (84) && ! humInst.plays (90),
               "a library can say each recording is an instrument of its own: played from C2 to C6, the hum on the A nearest middle C, the wind (no pitch) as recorded on middle C ("
               + juce::String (rep4.added) + " added)");
    }

    section ("the plug-in");
    {
        lib.save();
        Processor proc;
        proc.setRateAndBufferSizeDetails (48000.0, 512);
        proc.prepareToPlay (48000.0, 512);
        auto p = onePreset (glassId);
        p.name = "State Check"; p.masterDb = -4.0f; p.macros[1].value = 0.5f; p.layers[0].p[lpGain] = -7.0f; p.layers[0].p[lpCutoff] = 3000.0f;
        proc.loadPreset (p);
        proc.engine().waitUntilLoaded();
        check (std::abs (proc.getHost (Processor::hpMaster) + 4.0f) < 0.01f && std::abs (proc.getHost (Processor::hpMacro2) - 0.5f) < 0.001f && std::abs (proc.getHost (Processor::hpLevel1) + 7.0f) < 0.01f,
               "loading a preset moves the host's controls to it");
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
        float pk = 0;
        for (int i = 0; i < 80; ++i) { buf.clear(); proc.processBlock (buf, midi); pk = juce::jmax (pk, buf.getMagnitude (0, 512)); }
        check (pk > 0.01f && proc.noteHeld (60) && ! proc.noteHeld (61), "it plays through processBlock, and knows which key is down");
        proc.setHost (Processor::hpLevel1, -20.0f);
        juce::MemoryBlock state;
        proc.getStateInformation (state);
        Processor other;
        other.setRateAndBufferSizeDetails (48000.0, 512);
        other.prepareToPlay (48000.0, 512);
        other.setStateInformation (state.getData(), (int) state.getSize());
        const auto q = other.capture();
        check (q.name == "State Check" && q.layers[0].instrument == glassId && std::abs (q.layers[0].p[lpCutoff] - 3000.0f) < 0.5f && std::abs (q.layers[0].p[lpGain] + 20.0f) < 0.01f
               && std::abs (q.masterDb + 4.0f) < 0.01f && std::abs (q.macros[1].value - 0.5f) < 0.001f, "a project's state restores the whole sound in another instance");
        check (std::abs (other.getHost (Processor::hpLevel1) + 20.0f) < 0.01f, "and the host's controls with it");
        other.engine().waitUntilLoaded();
        midi.clear(); midi.addEvent (juce::MidiMessage::noteOn (1, 64, 0.8f), 0);
        pk = 0;
        for (int i = 0; i < 80; ++i) { buf.clear(); other.processBlock (buf, midi); pk = juce::jmax (pk, buf.getMagnitude (0, 512)); }
        check (pk > 0.001f, "the restored instance plays");
        int levels = 0; juce::String lastId;
        for (auto* prm : other.getParameters()) if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (prm)) { if (withId->paramID.startsWith ("level")) ++levels; lastId = withId->paramID; }
        check (levels == kLayers && lastId == "level" + juce::String (kLayers) && other.getParameters().size() == 5 + kLayers,
               "the host is given a level for each of the twelve layers, after the controls it already had (" + juce::String (other.getParameters().size()) + " in all, the last " + lastId + ")");
    }

    section ("the screen");
    {
        // what a picture looks like, coarsely: its colour, how much it varies, and its light and dark on a 12 x 6 grid
        struct Look { bool ok = false; int w = 0, h = 0; float r = 0, g = 0, b = 0, spread = 0; std::array<float, 72> grid {}; };
        auto lookAt = [] (const juce::Image& im, float x0 = 0.0f, float x1 = 1.0f, float y0 = 0.0f, float y1 = 1.0f) {
            Look k;
            if (! im.isValid()) return k;
            k.ok = true; k.w = im.getWidth(); k.h = im.getHeight();
            juce::Image::BitmapData bd (im, juce::Image::BitmapData::readOnly);
            double sr = 0, sg = 0, sb = 0, sl = 0, sl2 = 0; int n = 0;
            for (int cy = 0; cy < 6; ++cy)
                for (int cx = 0; cx < 12; ++cx)
                {
                    double cell = 0;
                    for (int j = 0; j < 4; ++j)
                        for (int i = 0; i < 4; ++i)
                        {
                            const float fx = x0 + (x1 - x0) * ((float) cx + ((float) i + 0.5f) / 4.0f) / 12.0f, fy = y0 + (y1 - y0) * ((float) cy + ((float) j + 0.5f) / 4.0f) / 6.0f;
                            const auto c = bd.getPixelColour (juce::jlimit (0, k.w - 1, (int) (fx * (float) k.w)), juce::jlimit (0, k.h - 1, (int) (fy * (float) k.h)));
                            const double l = 0.3 * c.getFloatRed() + 0.59 * c.getFloatGreen() + 0.11 * c.getFloatBlue();
                            sr += c.getFloatRed(); sg += c.getFloatGreen(); sb += c.getFloatBlue(); sl += l; sl2 += l * l; cell += l; ++n;
                        }
                    k.grid[(size_t) (cy * 12 + cx)] = (float) (cell / 16.0);
                }
            k.r = (float) (sr / n); k.g = (float) (sg / n); k.b = (float) (sb / n);
            k.spread = (float) std::sqrt (juce::jmax (0.0, sl2 / n - (sl / n) * (sl / n)));
            return k;
        };
        auto apart = [] (const Look& a, const Look& b) { return juce::jmax (std::abs (a.r - b.r), std::abs (a.g - b.g), std::abs (a.b - b.b)); };
        auto alike = [] (const Look& a, const Look& b) {          // how far the light and dark of two pictures go together (-1 .. 1)
            double ma = 0, mb = 0; for (size_t i = 0; i < 72; ++i) { ma += a.grid[i]; mb += b.grid[i]; } ma /= 72.0; mb /= 72.0;
            double ab = 0, aa = 0, bb = 0; for (size_t i = 0; i < 72; ++i) { ab += (a.grid[i] - ma) * (b.grid[i] - mb); aa += (a.grid[i] - ma) * (a.grid[i] - ma); bb += (b.grid[i] - mb) * (b.grid[i] - mb); }
            return aa > 0 && bb > 0 ? (float) (ab / std::sqrt (aa * bb)) : 0.0f;
        };

        // the paintings are decoded off the message thread (the stage does it there), so both ways are looked at
        int pictures = 0, good = 0, same = 0; float flattest = 1.0f; juce::String wrong;
        for (int w = 0; w <= kWorlds; ++w)
            for (int v = 0; v < 3; ++v)
            {
                const juce::String res = v == 0 ? juce::String (world (w).art) : v == 1 ? juce::String (world (w).art2) : "t" + juce::String (w) + "_jpg";
                const Look here = lookAt (ui::loadPicture (res.toRawUTF8()));
                Look there;
                std::thread t ([&] { there = lookAt (ui::loadPicture (res.toRawUTF8())); });
                t.join();
                ++pictures;
                const bool fine = here.ok && here.spread > 0.05f && (v == 2 || (here.w == 2640 && here.h == 1440));
                if (fine) ++good; else wrong << (wrong.isEmpty() ? "" : ", ") << res << " " << here.w << "x" << here.h << " spread " << juce::String (here.spread, 3);
                if (there.ok && apart (here, there) < 0.01f && std::abs (here.spread - there.spread) < 0.01f) ++same;
                if (here.ok) flattest = juce::jmin (flattest, here.spread);
            }
        check (good == pictures, "every painting decodes as a picture (" + juce::String (good) + " of " + juce::String (pictures) + "; the flattest varies by " + juce::String (flattest, 3)
                                 + (wrong.isNotEmpty() ? "; wrong: " + wrong : juce::String()) + ")");
        check (same == pictures, "and decodes the same off the message thread (" + juce::String (same) + " of " + juce::String (pictures) + ")");

        // the Play page, drawn by the real editor: what stands in its open part is its world's painting
        Processor proc;
        proc.setRateAndBufferSizeDetails (48000.0, 512);
        proc.prepareToPlay (48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        auto* editor = dynamic_cast<Editor*> (ed.get());
        check (editor != nullptr, "the editor opens");
        if (editor != nullptr)
        {
            editor->setSize (1320, 720);
            editor->setVisible (true);
            editor->setMotion (1);                 // (alive, not filmed: a film in mid-run is not its painting)
            for (int w : { 0, 4, 6 })
            {
                Preset p = Preset::blank(); p.name = "Screen Check"; p.world = world (w).key;
                proc.loadPreset (p);
                editor->showPage (0);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (80);
                editor->settle();
                juce::MessageManager::getInstance()->runDispatchLoopUntil (120);
                const auto shot = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
                const Look seen = lookAt (shot, 0.30f, 0.97f, 0.14f, 0.66f);
                // (the stage may enlarge a painting by up to 1.3 and move it, so that the point it is framed round stands in the open part:
                //  the painting is looked at under each framing the stage could have chosen, and the best one counts)
                const juce::Image painting = ui::loadPicture (world (w).art);
                Look painted; float agree = -1.0f;
                for (int zi = 0; zi <= 6; ++zi)
                    for (int xi = 0; xi < 5; ++xi)
                        for (int yi = 0; yi < 5; ++yi)
                        {
                            const float z = 1.0f + 0.05f * (float) zi, ox = (1.0f - z) * (float) xi / 4.0f, oy = (1.0f - z) * (float) yi / 4.0f;
                            const Look k = lookAt (painting, (0.30f - ox) / z, (0.97f - ox) / z, (0.14f - oy) / z, (0.66f - oy) / z);
                            if (k.ok && alike (seen, k) > agree) { agree = alike (seen, k); painted = k; }
                            if (zi == 0) { xi = 5; yi = 5; }        // (not enlarged: there is only one framing)
                        }
                const bool shown = seen.ok && painted.ok && apart (seen, painted) < 0.16f && agree > 0.6f;
                check (shown, juce::String ("the Play page shows the painting of ") + world (w).name + " (its colour is within " + juce::String (apart (seen, painted), 2)
                       + " of the painting's, and their light and dark agree " + juce::String (agree, 2) + ")"
                       + (shown ? juce::String() : "  seen " + juce::String (seen.r, 2) + " " + juce::String (seen.g, 2) + " " + juce::String (seen.b, 2)
                                                   + ", painted " + juce::String (painted.r, 2) + " " + juce::String (painted.g, 2) + " " + juce::String (painted.b, 2)));
            }

            // ---- the sound is seen on the Play page: light stands on the water over the notes, and the key that is down is lit
            {
                auto differ = [] (const juce::Image& a, const juce::Image& b, float x0, float x1, float y0, float y1) {
                    if (! a.isValid() || ! b.isValid() || a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight()) return 0.0f;
                    juce::Image::BitmapData pa (a, juce::Image::BitmapData::readOnly), pb (b, juce::Image::BitmapData::readOnly);
                    double sum = 0; int n = 0;
                    for (int y = (int) (y0 * (float) a.getHeight()); y < (int) (y1 * (float) a.getHeight()); y += 2)
                        for (int x = (int) (x0 * (float) a.getWidth()); x < (int) (x1 * (float) a.getWidth()); x += 2)
                        { sum += std::abs (pa.getPixelColour (x, y).getBrightness() - pb.getPixelColour (x, y).getBrightness()); ++n; }
                    return n > 0 ? (float) (sum / n) : 0.0f;
                };
                auto p = onePreset (glassId); p.name = "Light Check"; p.world = world (7).key;
                proc.loadPreset (p);
                proc.engine().waitUntilLoaded();
                editor->showPage (0);
                editor->setMotion (0);                 // (the world itself kept still, so that only the sound changes the picture)
                juce::MessageManager::getInstance()->runDispatchLoopUntil (80);
                editor->settle();
                juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
                const auto quiet = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
                juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer midi;
                midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.85f), 0);
                for (int i = 0; i < 36; ++i) { buf.clear(); proc.processBlock (buf, midi); midi.clear(); if (i % 3 == 2) juce::MessageManager::getInstance()->runDispatchLoopUntil (34); }
                const auto sounding = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
                // (middle C is the 22nd white key of 43: its key and the water above it lie 62 % of the way across)
                const float water = differ (quiet, sounding, 0.57f, 0.68f, 0.60f, 0.71f), key = differ (quiet, sounding, 0.618f, 0.631f, 0.90f, 0.95f), elsewhere = differ (quiet, sounding, 0.30f, 0.95f, 0.16f, 0.40f);
                check (water > 0.012f && key > 0.08f && elsewhere < water * 0.5f,
                       "the sound is seen: light stands on the water over the note (the picture there changed by " + juce::String (water, 3) + "), its key is lit (" + juce::String (key, 3)
                       + "), and the sky is left alone (" + juce::String (elsewhere, 3) + ")");
                midi.addEvent (juce::MidiMessage::allNotesOff (1), 0);
                for (int i = 0; i < 8; ++i) { buf.clear(); proc.processBlock (buf, midi); midi.clear(); }
                for (int i = 0; i < 400; ++i) { buf.clear(); proc.processBlock (buf, midi); if (i % 8 == 7) juce::MessageManager::getInstance()->runDispatchLoopUntil (34); }
                const auto after = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
                const float left = differ (quiet, after, 0.26f, 0.99f, 0.56f, 0.73f);
                check (left < 0.004f, "and when the sound has gone the water is as it was (the picture differs by " + juce::String (left, 4) + ")");
                editor->setMotion (1);
            }

            // ---- the worlds' film (its frames are kept beside the library; the build says where they are)
            if (std::getenv ("GHOST_ATLAS_FILM") == nullptr) std::printf ("INFO the film was not checked: GHOST_ATLAS_FILM does not say where its frames are\n");
            else
            {
                int there = 0, home_ = 0; float least = 1.0f; juce::String wrongFilm;
                for (int w = 0; w < kWorlds; ++w)
                {
                    ui::Film film;
                    if (! film.open (world (w).key) || film.frames() < 16) { wrongFilm << (wrongFilm.isEmpty() ? "" : ", ") << world (w).name << " has none"; continue; }
                    ++there;
                    auto asImage = [] (const ui::Film::Plain& f) {
                        juce::Image im (juce::Image::RGB, f.w, f.h, false, juce::SoftwareImageType());
                        juce::Image::BitmapData bd (im, juce::Image::BitmapData::writeOnly);
                        for (int y = 0; y < f.h; ++y) for (int x = 0; x < f.w; ++x) { const auto* px = f.rgb.data() + ((size_t) y * (size_t) f.w + (size_t) x) * 3; bd.setPixelColour (x, y, juce::Colour (px[0], px[1], px[2])); }
                        return im;
                    };
                    const auto first = film.frame (0), far = film.frame (film.frames() - 1);
                    if (first == nullptr || far == nullptr) { wrongFilm << (wrongFilm.isEmpty() ? "" : ", ") << world (w).name << " does not decode"; continue; }
                    const Look a = lookAt (asImage (*first)), z = lookAt (asImage (*far)), painted = lookAt (ui::loadPicture (world (w).art));
                    const float agree = alike (a, painted);
                    least = juce::jmin (least, agree);
                    if (apart (a, painted) < 0.08f && agree > 0.9f && z.spread > 0.05f && film.width() == 1320 && film.height() == 720) ++home_;
                    else wrongFilm << (wrongFilm.isEmpty() ? "" : ", ") << world (w).name << " starts " << juce::String (apart (a, painted), 2) << " / " << juce::String (agree, 2) << " from its painting";
                }
                check (there == kWorlds && home_ == kWorlds, "every world has its film, and each film starts at its painting (" + juce::String (home_) + " of " + juce::String (kWorlds)
                                                             + "; the least alike agree " + juce::String (least, 2)
                                                             + (wrongFilm.isNotEmpty() ? "; " + wrongFilm : juce::String()) + ")");
                for (int w : { 0, 6 })
                {
                    Preset p = Preset::blank(); p.name = "Film Check"; p.world = world (w).key;
                    proc.loadPreset (p);
                    editor->showPage (0);
                    editor->setMotion (2);
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (80);
                    editor->settle();
                    const Look before = lookAt (editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f), 0.30f, 0.97f, 0.14f, 0.66f);
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (2600);
                    const Look after = lookAt (editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f), 0.30f, 0.97f, 0.14f, 0.66f);
                    float moved = 0.0f; for (size_t i = 0; i < 72; ++i) moved += std::abs (after.grid[i] - before.grid[i]) / 72.0f;
                    check (editor->filmShowing() && moved > 0.004f && apart (before, after) < 0.2f,
                           juce::String ("the film plays in ") + world (w).name + ": after two and a half seconds the picture has moved (by " + juce::String (moved, 3) + ") and is still its world (colour within "
                           + juce::String (apart (before, after), 2) + "); " + juce::String (editor->filmPictures()) + " pictures made so far");
                }
                // how much work it is, at the number of pixels a 1320-point window has on a Retina display
                editor->setSize (2640, 1440);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (400);
                editor->settle();
                juce::MessageManager::getInstance()->runDispatchLoopUntil (2200);
                double paint = 0;
                for (int k = 0; k < 6; ++k)
                {
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (70);
                    const auto t0 = juce::Time::getMillisecondCounterHiRes();
                    const auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
                    paint += (juce::Time::getMillisecondCounterHiRes() - t0) / 6.0;
                }
                const int madeBefore = editor->filmPictures();
                juce::MessageManager::getInstance()->runDispatchLoopUntil (2000);
                std::printf ("INFO film at 2640 x 1440 (a Retina window): one picture takes %.1f ms to make on its own thread; drawing the whole window takes %.1f ms; it settled at %.1f pictures a second; film showing: %s\n",
                             (double) editor->filmMakeMs(), paint, (double) (editor->filmPictures() - madeBefore) / 2.0, editor->filmShowing() ? "yes" : "no");
                editor->setMotion (1);
                editor->setSize (1320, 720);
            }
            ed.reset();
        }
    }

    section ("the screen, used with a mouse and a keyboard as a host's window is");
    {
        Processor proc;
        proc.setRateAndBufferSizeDetails (44100.0, 512);
        proc.prepareToPlay (44100.0, 512);
        auto p = onePreset (glassId);
        p.name = "Hands On";
        p.master[0] = fxDefault ("crackle", false);              // there, but switched off, its first band turned right down
        p.master[0].values["b1_level"] = -24.0f;
        p.master[1].unit = "eq"; p.master[1].on = false; p.master[1].mix = 1.0f; p.master[1].values.clear();      // as every factory sound has it: off, nothing set
        p.sends[0].on = true;                                    // the chorus, with nothing sent to it yet
        // SPECTRAL FX on the layer, switched off, a low-pass in the first place of its chain
        auto rackEffect = [] (const char* name) { for (int e = 1; e < spectral::numEffects(); ++e) if (spectral::effectName (e) == name) return e; return 0; };
        p.layers[0].inserts[0] = fxDefault ("spectral", false);
        spectral::put (p.layers[0].inserts[0].values, 0, rackEffect ("Spectral Filter"));
        proc.loadPreset (p);
        proc.setNonRealtime (true);                              // (these blocks are asked for faster than they would be played)
        proc.engine().waitUntilLoaded();
        juce::AudioBuffer<float> buf (2, 512);
        juce::MidiBuffer midi;
        auto run = [&] (int blocks, int measured) {
            double sum = 0; int n = 0;
            for (int k = 0; k < blocks; ++k)
            {
                buf.clear();
                proc.processBlock (buf, midi);
                if (k >= blocks - measured) for (int c = 0; c < 2; ++c) for (int i = 0; i < 512; ++i) { const float v = buf.getSample (c, i); sum += (double) v * v; ++n; }
            }
            return gainToDb ((float) std::sqrt (sum / juce::jmax (1, n)) + 1.0e-9f);
        };
        auto heard = [&] {              // a fresh note: how loud its first three quarters of a second are
            midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0);
            run (13, 1);
            const float level = run (52, 52);
            midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
            run (100, 1);
            return level;
        };
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        auto* editor = dynamic_cast<Editor*> (ed.get());
        if (editor != nullptr)
        {
            editor->setSize (1320, 720);
            editor->setMotion (0);
            // The checks that move a mouse and type need a real window (only a window can be given the keyboard). One is
            // made only where it is asked for (GHOST_ATLAS_WINDOW_CHECKS: tools/checks-headless.sh sets it), and off
            // the screen even then: on the owner's Mac no window of the checks' own appears, and none takes his keyboard.
            const bool windowed = juce::SystemStats::getEnvironmentVariable ("GHOST_ATLAS_WINDOW_CHECKS", {}).isNotEmpty();
            if (windowed)
            {
                editor->addToDesktop (juce::ComponentPeer::windowIsTemporary);
                editor->setTopLeftPosition (-6000, -6000);
            }
            editor->setVisible (true);
            auto pump = [] (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); };
            pump (200);
            // everything the window holds that asks for the keyboard (a window gives the keyboard to the first of these on any click)
            std::function<void (juce::Component&, juce::StringArray&)> askers = [&] (juce::Component& c, juce::StringArray& found) {
                if (c.getWantsKeyboardFocus()) found.add (juce::String (typeid (c).name()));
                for (int i = 0; i < c.getNumChildComponents(); ++i) askers (*c.getChildComponent (i), found);
            };
            auto asking = [&] { juce::StringArray f; askers (*editor, f); return f; };
            juce::int64 clock = juce::Time::currentTimeMillis();
            auto* peer = windowed ? editor->getPeer() : nullptr;
            auto at = [&] (float x, float y, bool down) {
                if (peer != nullptr) peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, { x, y }, down ? juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier) : juce::ModifierKeys(),
                                                             juce::MouseInputSource::defaultPressure, juce::MouseInputSource::defaultOrientation, clock += 40);
            };
            auto click = [&] (float x, float y) { at (x, y, false); at (x, y, true); at (x, y, false); clock += 900; pump (40); };
            auto drag = [&] (float x0, float y0, float x1, float y1) {
                at (x0, y0, false); at (x0, y0, true);
                for (int k = 1; k <= 8; ++k) at (x0 + (x1 - x0) * (float) k / 8.0f, y0 + (y1 - y0) * (float) k / 8.0f, true);
                at (x1, y1, false); clock += 900; pump (40);
            };
            // ---- the keyboard is the host's: nothing asks for it, on any page
            juce::String who;
            for (int pg = 0; pg < ui::kNumPages; ++pg) { editor->showPage (pg); pump (60); const auto f = asking(); if (! f.isEmpty()) who << "page " << pg << ": " << f.joinIntoString (", ") << "  "; }
            check (who.isEmpty(), "nothing on any page asks for the keyboard (the host's typing keys play notes)" + (who.isEmpty() ? juce::String() : ": " + who));

            if (peer == nullptr) std::printf ("INFO the checks that use a mouse and a keyboard were not run here: they need a window of their own (%s)\n",
                                              windowed ? "none could be made" : "run through tools/checks-headless.sh, or with GHOST_ATLAS_WINDOW_CHECKS=1");
            else
            {
                // ---- the Effects page, by hand: the master's first unit is chosen, switched on, and its knob turned
                editor->showPage (ui::pageEffects);
                pump (80);
                const float dry = heard();
                click (32.0f + 5.0f * 157.0f + 75.0f, 82.0f + 27.0f);        // the tile "Master, first"
                click (306.0f + 40.0f, 164.0f + 14.0f);                      // its On switch
                const bool switched = proc.engine().getFx (FxAddress::master (0)).on;
                const float on = heard();
                drag (40.0f + 4.0f * 103.0f + 48.0f, 222.0f + 48.0f, 40.0f + 4.0f * 103.0f + 48.0f, 222.0f + 48.0f - 190.0f);       // the fifth knob (band 1's level), a whole turn up
                const auto turnedTo = proc.engine().getFx (FxAddress::master (0)).values["b1_level"];
                const float up = heard();
                click (306.0f + 40.0f, 164.0f + 14.0f);
                const float off = heard();
                check (switched && on < dry - 12.0f && turnedTo > 6.0f && up > on + 12.0f && ! proc.engine().getFx (FxAddress::master (0)).on && std::abs (off - dry) < 1.5f,
                       "an effect switched on and turned with the mouse is heard, and switched off again it is gone (dry " + juce::String (dry, 1) + " dB; on " + juce::String (on, 1)
                       + "; its level turned up to " + juce::String (turnedTo, 1) + " dB: " + juce::String (up, 1) + "; off " + juce::String (off, 1) + ")");
                // the equaliser, as it stands in every factory sound (off, nothing set): switched on it is five bands, and a gain turned is heard
                click (32.0f + 6.0f * 157.0f + 75.0f, 82.0f + 27.0f);        // the tile "Master, second"
                int knobs = 0;
                std::function<void (juce::Component&)> count = [&] (juce::Component& c) { if (c.isShowing() && dynamic_cast<ui::Knob*> (&c) != nullptr) ++knobs; for (int i = 0; i < c.getNumChildComponents(); ++i) count (*c.getChildComponent (i)); };
                count (*editor);
                click (306.0f + 40.0f, 164.0f + 14.0f);
                const float flat = heard();
                drag (40.0f + 2.0f * 103.0f + 48.0f, 240.0f + 48.0f, 40.0f + 2.0f * 103.0f + 48.0f, 240.0f + 48.0f - 190.0f);       // the third knob: the low-mid band's gain
                const float boost = proc.engine().getFx (FxAddress::master (1)).values["2_gain"];
                const float lifted = heard();
                check (knobs == 11 && std::abs (flat - dry) < 1.0f && boost > 20.0f && lifted > flat + 6.0f,
                       "the equaliser shows eleven knobs (five bands and its output), is flat when switched on, and a gain turned is heard (" + juce::String (knobs) + " knobs; as it was "
                       + juce::String (dry, 1) + " dB, switched on " + juce::String (flat, 1) + ", its low-mid band at +" + juce::String (boost, 0) + " dB: " + juce::String (lifted, 1) + ")");
                click (306.0f + 40.0f, 164.0f + 14.0f);                      // (off again)

                // a send nothing is sent to: its controls are greyed out until a layer's bar on this page is raised, and then it is heard
                click (32.0f + 2.0f * 157.0f + 75.0f, 82.0f + 27.0f);        // the tile "Send A"
                pump (300);
                auto liveKnobs = [&] { int live = 0, all = 0; std::function<void (juce::Component&)> go = [&] (juce::Component& c) { if (auto* k = dynamic_cast<ui::Knob*> (&c)) if (k->isShowing()) { ++all; if (k->live()) ++live; } for (int i = 0; i < c.getNumChildComponents(); ++i) go (*c.getChildComponent (i)); }; go (*editor); return std::pair<int, int> (live, all); };
                const auto before = liveKnobs();
                const float unsent = heard();
                std::function<juce::Component* (juce::Component&, const juce::String&)> byId = [&] (juce::Component& c, const juce::String& id) -> juce::Component* {
                    if (c.getComponentID() == id && c.isShowing()) return &c;
                    for (int i = 0; i < c.getNumChildComponents(); ++i) if (auto* f = byId (*c.getChildComponent (i), id)) return f;
                    return nullptr; };
                auto* bar = byId (*editor, "feed1");
                if (bar != nullptr) { const auto r = editor->getLocalArea (bar, bar->getLocalBounds()).toFloat(); click (r.getRight() - 62.0f, r.getCentreY()); }
                pump (300);
                const auto after = liveKnobs();
                const float sentNow = proc.engine().getLayerParam (0, lpSendA);
                const float sent = heard();
                check (bar != nullptr && before.second > 0 && before.first == 0 && after.first > 0 && sentNow > 0.3f && std::abs (sent - unsent) > 0.7f,
                       "a send with nothing sent to it is greyed out; a layer's bar raised on the Effects page feeds it, and it is heard (knobs in use " + juce::String (before.first) + " of "
                       + juce::String (before.second) + ", then " + juce::String (after.first) + "; layer 1 sends " + juce::String (sentNow, 2) + "; " + juce::String (unsent, 1) + " dB, then " + juce::String (sent, 1) + ")");
                // SPECTRAL FX on the layer, by hand: switched on, its filter turned right down, then its other parts looked through
                {
                    click (32.0f + 75.0f, 82.0f + 27.0f);                        // the tile "Layer 1, first"
                    pump (200);
                    const float rackOff = heard();
                    click (264.0f + 266.0f + 40.0f, 164.0f + 14.0f);             // its On switch (a layer's place has the layer's chooser before the unit's)
                    const bool switchedOn = proc.engine().getFx (FxAddress::insert (0, 0)).on;
                    const float rackOpen = heard();
                    int freq = 0;
                    for (int k = 0; k < spectral::numKnobs (rackEffect ("Spectral Filter")); ++k) if (spectral::knob (rackEffect ("Spectral Filter"), k).name == "Frequency") freq = k;
                    const auto freqId = spectral::idOf (spectral::iKnob (0, freq));
                    auto place = [&] (juce::Component* c) { return c != nullptr ? editor->getLocalArea (c, c->getLocalBounds()).toFloat() : juce::Rectangle<float>(); };
                    const auto knobAt = place (byId (*editor, freqId));
                    drag (knobAt.getCentreX(), knobAt.getCentreY(), knobAt.getCentreX(), knobAt.getCentreY() + 190.0f);      // Frequency, a whole turn down
                    const float keptFreq = proc.engine().getFx (FxAddress::insert (0, 0)).values[freqId];
                    const float rackShut = heard();
                    const auto tabs = place (byId (*editor, "rack-parts"));
                    click (tabs.getX() + tabs.getWidth() * 4.5f / 6.0f, tabs.getCentreY());                                   // the part "Warp Core"
                    pump (300);
                    const auto warpOff = liveKnobs();
                    const auto warpSwitch = place (byId (*editor, "rack-on"));
                    click (warpSwitch.getX() + 20.0f, warpSwitch.getCentreY());
                    pump (300);
                    const auto warpOn = liveKnobs();
                    const bool warping = proc.engine().getFx (FxAddress::insert (0, 0)).values["w_on"] > 0.5f;
                    // Pitch Affect and Fill Gap say how a moved pitch is treated: with Pitch at 0 st they change nothing and are greyed out
                    auto liveControl = [&] (const char* id) { auto* c = dynamic_cast<ui::Control*> (byId (*editor, id)); return c != nullptr && c->live(); };
                    const bool restAtZero = ! liveControl ("w_affect") && ! liveControl ("w_fill");
                    const auto pitchAt = place (byId (*editor, "w_pitch"));
                    drag (pitchAt.getCentreX(), pitchAt.getCentreY(), pitchAt.getCentreX(), pitchAt.getCentreY() - 60.0f);        // Pitch, a third of a turn up
                    pump (400);
                    const float pitchNow = proc.engine().getFx (FxAddress::insert (0, 0)).values["w_pitch"];
                    const bool wakeWhenMoved = liveControl ("w_affect") && liveControl ("w_fill");
                    proc.engine().setFxValue (FxAddress::insert (0, 0), "w_pitch", 0.0f);                                        // (and back, for what is checked after this)
                    click (tabs.getX() + tabs.getWidth() * 5.5f / 6.0f, tabs.getCentreY());                                   // the part "Window"
                    pump (300);
                    const auto window = liveKnobs();
                    check (switchedOn && ! knobAt.isEmpty() && keptFreq < 0.02f && rackShut < rackOpen - 20.0f && rackOpen < rackOff + 1.0f
                           && warpOff.second == 6 && warpOff.first == 0 && (warpOn.first == 6 || ! spectral::warpAvailable()) && (warping || ! spectral::warpAvailable()) && window.second == 2
                           && restAtZero && ((wakeWhenMoved && std::abs (pitchNow) > 0.5f) || ! spectral::warpAvailable()),
                           "SPECTRAL FX with the mouse: switched on, its filter's frequency turned right down shuts the layer (" + juce::String (rackOpen, 1) + " -> " + juce::String (rackShut, 1)
                           + " dB); Warp Core's six knobs are greyed out until it is switched on (" + juce::String (warpOff.first) + " in use, then " + juce::String (warpOn.first)
                           + "), its Pitch Affect and Fill Gap until Pitch is moved off 0 (" + juce::String (pitchNow, 1) + " st), and the window's part shows its two (" + juce::String (window.second) + ")");
                }
                check (juce::Component::getCurrentlyFocusedComponent() == nullptr && asking().isEmpty(), "and none of those clicks took the keyboard");

                // ---- the Play page: its knobs, its keys and its list are clicked; then the search box, which alone takes the keyboard
                editor->showPage (ui::pagePlay);
                pump (80);
                drag (884.0f + 36.0f, 522.0f + 43.0f, 884.0f + 36.0f, 522.0f + 43.0f - 60.0f);       // TONE
                click (800.0f, 672.0f);                                                             // a key
                click (160.0f, 330.0f);                                                             // a row of the list
                check (juce::Component::getCurrentlyFocusedComponent() == nullptr && asking().isEmpty(),
                       "turning a knob, pressing a key on the screen and choosing from the list leave the keyboard with the host");
                const bool typedBefore = peer != nullptr && peer->handleKeyPress ('a', 'a');
                click (28.0f + 120.0f, 80.0f + 17.0f);                                              // into "Search sounds"
                auto* box = dynamic_cast<juce::TextEditor*> (juce::Component::getCurrentlyFocusedComponent());
                const bool wanted = asking().size() == 1;
                if (box == nullptr && peer != nullptr && ! peer->isFocused())
                    std::printf ("INFO this display gives no window the keyboard, so typing into the search box is not looked at (the box did ask for it: %s)\n", wanted ? "yes" : "no");
                else
                {
                    const bool typed = peer != nullptr && peer->handleKeyPress ('a', 'a');
                    const auto text = box != nullptr ? box->getText() : juce::String();
                    check (! typedBefore && box != nullptr && typed && text == "a", "a click into the search box, and only that, lets it be typed into (before: a key was "
                           + juce::String (typedBefore ? "taken" : "left to the host") + "; after: \"" + text + "\")");
                }
                check (wanted, "the search box asks for the keyboard once it is clicked into");
                click (884.0f + 36.0f, 522.0f + 43.0f);                                             // anywhere else
                const bool freed = juce::Component::getCurrentlyFocusedComponent() == nullptr && asking().isEmpty();
                const bool typedAfter = peer != nullptr && peer->handleKeyPress ('b', 'b');
                check (freed && ! typedAfter, "and a click anywhere else gives the keyboard back to the host");
                click (28.0f + 120.0f, 80.0f + 17.0f);
                if (peer != nullptr) peer->handleKeyPress (juce::KeyPress::escapeKey, 0);
                pump (40);
                check (juce::Component::getCurrentlyFocusedComponent() == nullptr && asking().isEmpty(), "so does Escape");
            }
            ed.reset();
        }
    }

    home.deleteRecursively();
    std::printf ("\n%d passed, %d failed\n%s\n", passed, failures, failures == 0 ? "ALL PASS" : "FAILED");
    return failures == 0 ? 0 : 1;
}

// ============================================================================================ the tool
int listInstruments()
{
    Library lib;
    if (! lib.load()) { std::printf ("no library database at %s\n", databaseFile().getFullPathName().toRawUTF8()); return 1; }
    std::printf ("id\tname\tcategory\tformat\tregions\tkeys\tvel\tMB\tloud\tpeak\tattack\tsustain\trelease\tbright\tlow\tmid\thigh\tair\tflat\tharm\ttune\twidth\tmove\tclick\ttrans\tveldB\ttags\tlicence\n");
    for (const auto& i : lib.all())
        std::printf ("%s\t%s\t%s\t%s\t%d\t%d-%d\t%d\t%.1f\t%.1f\t%.1f\t%.0f\t%.2f\t%.0f\t%.2f\t%.2f\t%.2f\t%.2f\t%.3f\t%.2f\t%.2f\t%.1f\t%.2f\t%.2f\t%.2f\t%.2f\t%.1f\t%s\t%s\n",
                     i.id.toRawUTF8(), i.name.toRawUTF8(), i.category.toRawUTF8(), i.format.toRawUTF8(), i.regions, i.loKey, i.hiKey, i.velocityLayers, (double) i.bytes / 1048576.0,
                     i.f.loudnessDb, i.f.peakDb, i.f.attackMs, i.f.sustain, i.f.releaseMs, i.f.brightness, i.f.low, i.f.mid, i.f.high, i.f.air, i.f.flatness, i.f.harmonicity, i.f.tuneCents,
                     i.f.width, i.f.movement, i.f.loopClick, i.f.transient, i.f.velocityDb, i.tags.joinIntoString (",").toRawUTF8(), i.license.toRawUTF8());
    return 0;
}

int render (const juce::StringArray& args)
{
    if (args.size() < 3) { std::printf ("render <preset file | factory:Name> <out.wav> [chord|arp|line|note] [seconds]\n"); return 2; }
    Library lib; lib.load();
    PresetBank bank; bank.refresh();
    Preset p;
    if (! bank.load (args[1], p)) { std::printf ("cannot load preset %s\n", args[1].toRawUTF8()); return 1; }
    const auto kind = args.size() > 3 ? args[3] : juce::String ("chord");
    const double seconds = args.size() > 4 ? args[4].getDoubleValue() : 9.0;
    std::vector<Note> notes;
    if (kind == "note") notes = { { 60, 0.8f, 0.2, 3.0 } };
    else if (kind == "arp") { const int s[] = { 0, 7, 12, 16, 19, 16, 12, 7 }; for (int k = 0; k < 16; ++k) notes.push_back ({ 48 + s[k % 8], 0.7f + 0.1f * (float) (k % 3), 0.2 + 0.22 * k, 0.3 }); }
    else if (kind == "line") { const int s[] = { 0, 3, 5, 7, 10, 7, 12 }; double t = 0.2; for (int k = 0; k < 7; ++k) { notes.push_back ({ 60 + s[k], 0.75f, t, 0.5 }); t += 0.55; } }
    else notes = { { 48, 0.7f, 0.2, 5.0 }, { 60, 0.75f, 0.25, 5.0 }, { 67, 0.7f, 0.3, 5.0 }, { 71, 0.65f, 0.35, 5.0 }, { 76, 0.7f, 0.4, 5.0 } };
    const auto a = Engine::bounce (p, [&lib] (const juce::String& id) { return lib.sfzPath (id); }, notes, seconds, 48000.0);
    const bool ok = writeWav (juce::File::getCurrentWorkingDirectory().getChildFile (args[2]), a, 48000.0);
    std::printf ("%s: peak %.3f, loudness %.1f dB -> %s\n", p.name.toRawUTF8(), measure::peakOf (a), measure::loudnessDb (a, 48000.0), args[2].toRawUTF8());
    return ok ? 0 : 1;
}
MutateMode modeNamed (const juce::String& s) { return s == "subtle" ? MutateMode::Subtle : (s == "wild" ? MutateMode::Wild : MutateMode::Evolve); }

void printMutation (const Mutation& m)
{
    if (! m.ok) { std::printf ("  nothing came of it: %s (tried %d)\n", m.failure.toRawUTF8(), m.tried); return; }
    std::printf ("  -> %s  [%s]  tried %d, rejected %d; loudness %.1f dB (original %.1f)\n", m.preset.name.toRawUTF8(), m.preset.category.toRawUTF8(), m.tried, m.rejected, m.loudnessDb, m.originalLoudnessDb);
    for (const auto& c : m.changes) std::printf ("     . %s\n", c.toRawUTF8());
    for (const auto& c : m.checks) std::printf ("     %s %-30s %s\n", c.passed ? "ok  " : "FAIL", c.name.toRawUTF8(), c.found.toRawUTF8());
}

int mutateOnce (const juce::StringArray& args)
{
    if (args.size() < 3) { std::printf ("mutate <preset> <subtle|evolve|wild> [seed]\n"); return 2; }
    Library lib; lib.load();
    PresetBank bank; bank.refresh();
    Preset p;
    if (! bank.load (args[1], p)) { std::printf ("cannot load preset %s\n", args[1].toRawUTF8()); return 1; }
    const Engine::Resolver resolver = [&lib] (const juce::String& id) { return lib.sfzPath (id); };
    Mutator mutator (lib, resolver);
    const auto t0 = juce::Time::getMillisecondCounter();
    const auto m = mutator.mutate (p, modeNamed (args[2]), args.size() > 3 ? (juce::uint32) args[3].getIntValue() : 1u, printer());
    std::printf ("%s, %s, %.1f s\n", p.name.toRawUTF8(), args[2].toRawUTF8(), (double) (juce::Time::getMillisecondCounter() - t0) / 1000.0);
    printMutation (m);
    if (! m.ok) return 1;
    const std::vector<Note> notes { { 48, 0.7f, 0.2, 4.0 }, { 60, 0.75f, 0.25, 4.0 }, { 67, 0.7f, 0.3, 4.0 }, { 72, 0.7f, 0.35, 4.0 }, { 64, 0.9f, 5.5, 0.4 }, { 67, 0.6f, 6.0, 0.4 }, { 72, 0.9f, 6.5, 1.5 } };
    writeWav (juce::File::getCurrentWorkingDirectory().getChildFile ("before.wav"), Engine::bounce (p, resolver, notes, 10.0, 48000.0), 48000.0);
    writeWav (juce::File::getCurrentWorkingDirectory().getChildFile ("after.wav"), Engine::bounce (m.preset, resolver, notes, 10.0, 48000.0), 48000.0);
    juce::File::getCurrentWorkingDirectory().getChildFile ("after.atlas").replaceWithText (juce::JSON::toString (m.preset.toVar()));
    return 0;
}

// REIMAGINE once, from the command line: the new instrument goes into the library, before / after audio beside the call
int reimagineOnce (const juce::StringArray& args)
{
    if (args.size() < 2) { std::printf ("reimagine <preset> [seed] [--discard]\n"); return 2; }
    Library lib; lib.load();
    PresetBank bank; bank.refresh();
    Preset p;
    if (! bank.load (args[1], p)) { std::printf ("cannot load preset %s\n", args[1].toRawUTF8()); return 1; }
    const Engine::Resolver resolver = [&lib] (const juce::String& id) { return lib.sfzPath (id); };
    const auto t0 = juce::Time::getMillisecondCounter();
    const auto m = reimagine (lib, resolver, p, args.size() > 2 ? (juce::uint32) args[2].getIntValue() : 1u, printer());
    std::printf ("%s, reimagine, %.1f s\n", p.name.toRawUTF8(), (double) (juce::Time::getMillisecondCounter() - t0) / 1000.0);
    printMutation (m);
    if (! m.ok) return 1;
    const std::vector<Note> notes { { 48, 0.7f, 0.2, 4.0 }, { 60, 0.75f, 0.25, 4.0 }, { 67, 0.7f, 0.3, 4.0 }, { 72, 0.7f, 0.35, 4.0 }, { 64, 0.9f, 5.5, 0.4 }, { 67, 0.6f, 6.0, 0.4 }, { 72, 0.9f, 6.5, 1.5 } };
    writeWav (juce::File::getCurrentWorkingDirectory().getChildFile ("before.wav"), Engine::bounce (p, resolver, notes, 10.0, 48000.0), 48000.0);
    writeWav (juce::File::getCurrentWorkingDirectory().getChildFile ("reimagined.wav"), Engine::bounce (m.preset, resolver, notes, 10.0, 48000.0), 48000.0);
    if (args.contains ("--discard")) { discardReimagined (lib, m); std::printf ("  (the instrument was taken out of the library again)\n"); }
    return 0;
}

// hand-written sounds made ready for the factory set: levels from measured loudness, tuning, the listening checks, one
// loudness for all. Reads an array of sounds (levels written as trims), writes the finished array and a report.
int polishCmd (const juce::StringArray& args)
{
    if (args.size() < 3) { std::printf ("polish <drafts.json> <out.json> [report.tsv] [--only <name part>]\n"); return 2; }
    const auto cwd = juce::File::getCurrentWorkingDirectory();
    const auto in = juce::JSON::parse (cwd.getChildFile (args[1]));
    if (! in.isArray()) { std::printf ("%s is not an array of sounds\n", args[1].toRawUTF8()); return 1; }
    const auto only = args.indexOf ("--only") >= 0 ? args[args.indexOf ("--only") + 1] : juce::String();
    Library lib; lib.load();
    const Engine::Resolver resolver = [&lib] (const juce::String& id) { return lib.sfzPath (id); };
    Mutator mutator (lib, resolver);
    juce::Array<juce::var> out;
    juce::String tsv = "name\tcategory\tlayers\tresult\tpeak_db\tonset_db\tchord_db\tmaster_db\tnotes\n";
    int ok = 0, failed = 0, n = 0;
    for (const auto& v : *in.getArray())
    {
        Preset p = Preset::fromVar (v);
        if (only.isNotEmpty() && ! p.name.containsIgnoreCase (only)) continue;
        ++n;
        juce::StringArray notes; std::vector<QualityCheck> checks; Heard heard; juce::String missing;
        juce::StringArray setAside;
        if (const auto* a = prop (v, "setAside").getArray()) for (const auto& s : *a) setAside.add (s.toString());
        bool good = mutator.polish (p, notes, checks, heard, missing, -17.0f, setAside);
        juce::String why;
        if (missing.isNotEmpty()) why = "MISSING " + missing;
        else for (const auto& c : checks) if (! c.passed) why << (why.isEmpty() ? "" : "; ") << (good ? "set aside: " : "") << c.name << " (" << c.found << ")";
        if (good && ! setAside.contains ("No harsh ring"))
        {   // A rack of SPECTRAL FX is not let off its own ring. The listening forgives a sound the ring its main layer has
            // alone, as that layer stands in the sound: with a rack on it, or on the master, that would forgive the rack.
            // So for a factory sound the ring that is forgiven is the one its instruments have with every rack switched off.
            Preset bare = p; bool racks = false;
            auto off = [&racks] (FxState& f) { if (f.unit == "spectral" && f.on) { f.on = false; racks = true; } };
            for (auto& l : bare.layers) for (auto& f : l.inserts) off (f);
            for (auto& f : bare.sends) off (f);
            for (auto& f : bare.master) off (f);
            if (racks)
            {
                const auto alone = mutator.listen (bare, mutator.mainLayer (bare));
                const float allowed = Mutator::ringAllowed (mutator.ownRing (bare, &alone), heard.holds);
                if (heard.ringDb > allowed)
                {
                    good = false;
                    why << (why.isEmpty() ? "" : "; ") << "No harsh ring (with its racks the sharpest band stands " << juce::String (heard.ringDb, 1) << " dB above its neighbours; its instruments alone allow "
                        << juce::String (allowed, 1) << " dB)";
                }
            }
        }
        std::printf ("%-4s %-28s %-18s L%d  peak %5.1f onset %5.1f chord %5.1f master %5.1f  %s%s\n", good ? "ok" : "FAIL", p.name.toRawUTF8(), p.category.toRawUTF8(), p.layerCount(),
                     heard.peakDb, heard.onsetDb, heard.loudnessDb, p.masterDb, why.toRawUTF8(), notes.isEmpty() ? "" : (" [" + notes.joinIntoString ("; ") + "]").toRawUTF8());
        std::fflush (stdout);
        tsv << p.name << "\t" << p.category << "\t" << p.layerCount() << "\t" << (good ? (why.isEmpty() ? juce::String ("ok") : "ok (" + why + ")") : "FAIL: " + why) << "\t" << juce::String (heard.peakDb, 1) << "\t" << juce::String (heard.onsetDb, 1) << "\t"
            << juce::String (heard.loudnessDb, 1) << "\t" << juce::String (p.masterDb, 1) << "\t" << notes.joinIntoString ("; ") << "\n";
        if (good) { ++ok; p.author = "Ghost Atlas"; p.factory = false; p.file.clear(); out.add (p.toVar()); } else ++failed;
    }
    if (only.isEmpty()) cwd.getChildFile (args[2]).replaceWithText (juce::JSON::toString (juce::var (out)));
    if (args.size() > 3 && ! args[3].startsWith ("--")) cwd.getChildFile (args[3]).replaceWithText (tsv);
    std::printf ("\n%d sounds: %d ready, %d not\n", n, ok, failed);
    return failed > 0 ? 1 : 0;
}

// every factory sound against the library on this computer: is each instrument it names there, with its files?
int factoryCmd()
{
    Library lib; lib.load();
    PresetBank bank; bank.refresh();
    int sounds = 0, broken = 0;
    std::map<juce::String, int> perCategory;
    for (const auto& info : bank.list())
    {
        if (! info.factory) continue;
        Preset p;
        if (! bank.load (info.file, p)) { std::printf ("  cannot read %s\n", info.name.toRawUTF8()); ++broken; continue; }
        ++sounds; ++perCategory[p.category];
        juce::StringArray missing;
        for (const auto& l : p.layers)
        {
            if (! l.used()) continue;
            Instrument inst;
            if (! lib.findLoose (l.instrument, inst)) missing.add (l.instrument + " (not in the library)");
            else if (! lib.sfzFile (inst).existsAsFile()) missing.add (l.instrument + " (its files are gone)");
        }
        if (! missing.isEmpty()) { ++broken; std::printf ("  %s: %s\n", p.name.toRawUTF8(), missing.joinIntoString ("; ").toRawUTF8()); }
    }
    juce::String cats; for (const auto& c : perCategory) cats << (cats.isEmpty() ? "" : ", ") << c.first << " " << c.second;
    std::printf ("%d factory sounds (%s); %d with something missing; %d instruments in the library\n", sounds, cats.toRawUTF8(), broken, lib.size());
    return broken > 0 || sounds == 0 ? 1 : 0;
}

// How much each effect is heard: one factory sound (the first, or the one named) played as it is, then with one thing
// changed at a time - each send switched off, each master unit switched on, and every unit there is put on the first
// layer with the settings it arrives with. What is printed is how far the result is from the sound as it was (dB, over
// time and pitch), against how far two takes of the unchanged sound are from each other.
int fxAudit (const juce::StringArray& args)
{
    Library lib; lib.load();
    PresetBank bank; bank.refresh();
    const Engine::Resolver resolver = [&lib] (const juce::String& id) { return lib.sfzPath (id); };
    Preset base; bool have = false;
    for (const auto& info : bank.list())
        if (info.factory && (args.size() < 2 || info.name.equalsIgnoreCase (args[1]))) { have = bank.load (info.file, base); break; }
    if (! have) { std::printf ("no such factory sound\n"); return 1; }
    const double fs = 44100.0, seconds = 6.0;
    const std::vector<Note> notes = { { 48, 0.75f, 0.2, 2.2 }, { 60, 0.75f, 0.2, 2.2 }, { 64, 0.75f, 0.2, 2.2 }, { 67, 0.75f, 0.2, 2.2 }, { 72, 0.8f, 2.6, 0.25 }, { 76, 0.8f, 3.1, 0.25 } };
    auto apart = [] (const std::vector<float>& x, const std::vector<float>& y) { return picturesApart (x, y); };
    auto take = [&] (const Preset& p) { return soundPicture (Engine::bounce (p, resolver, notes, seconds, fs), fs); };
    const auto was = take (base);
    const float noise = apart (was, take (base));
    std::printf ("%s: two takes of it as it is differ by %.2f dB\n", base.name.toRawUTF8(), (double) noise);
    auto report = [&] (const juce::String& what, const Preset& p) {
        const float d = apart (was, take (p));
        std::printf ("  %-44s %6.2f dB  %s\n", what.toRawUTF8(), (double) d, d < noise + 0.5f ? "NOT HEARD" : (d < noise + 1.5f ? "faint" : ""));
    };
    for (int k = 0; k < kSends; ++k)
    {
        auto p = base; p.sends[(size_t) k].on = false; report ("send " + juce::String::charToString ((juce::juce_wchar) ('A' + k)) + " (" + p.sends[(size_t) k].unit + ") switched off", p);
        p = base; p.returns[(size_t) k] = 2.0f; report ("its return turned right up", p);
    }
    for (int k = 0; k < kMasterSlots; ++k)
    { auto p = base; p.master[(size_t) k].on = ! p.master[(size_t) k].on; report ("master " + p.master[(size_t) k].unit + (p.master[(size_t) k].on ? " switched on" : " switched off"), p); }
    for (int m = 0; m < kMacros; ++m) { auto p = base; p.macros[(size_t) m].value = 1.0f; report (p.macros[(size_t) m].name + " turned right up", p); }
    for (const auto& u : fxUnits())
    { auto p = base; p.layers[0].inserts[0] = fxDefault (u.key); report ("layer 1 through " + u.title + " as it arrives", p); }
    return 0;
}

// SPECTRAL FX: the rack's list (every effect, its folder, its knobs as upstream names them) and what each effect does to
// one sound in the first place of a rack on layer 1: as it arrives (against the sound without the rack, held back by
// the rack's latency), and with its first turning knob turned.  spectral ["Sound Name"] [curve]   |   spectral list <file.json>
int spectralCmd (const juce::StringArray& args)
{
    if (args.size() > 2 && args[1] == "list")
    {   // the rack's list as a file: tools/make_presets.py writes the factory sounds' racks from it, by upstream's own names
        auto kindName = [] (int k) { return k == spectral::cChoice ? "choice" : (k == spectral::cSwitch ? "switch" : (k == spectral::cWhole ? "whole" : "float")); };
        juce::Array<juce::var> effects;
        for (int e = 1; e < spectral::numEffects(); ++e)
        {
            std::map<juce::String, float> starts; spectral::put (starts, 0, e);
            juce::Array<juce::var> knobs;
            for (int k = 0; k < spectral::numKnobs (e); ++k)
            {
                const auto c = spectral::knob (e, k);
                auto* o = new juce::DynamicObject();
                o->setProperty ("name", c.name); o->setProperty ("kind", kindName (c.kind)); o->setProperty ("min", c.min); o->setProperty ("max", c.max); o->setProperty ("log", c.log);
                o->setProperty ("start", starts[spectral::idOf (spectral::iKnob (0, k))]);            // (as it is kept: between 0 and 1)
                juce::Array<juce::var> ch; for (const auto& x : c.choices) ch.add (x);
                o->setProperty ("choices", ch);
                knobs.add (juce::var (o));
            }
            auto* o = new juce::DynamicObject();
            o->setProperty ("index", e); o->setProperty ("name", spectral::effectName (e)); o->setProperty ("folder", spectral::effectFolder (e)); o->setProperty ("window", spectral::usesWindow (e));
            o->setProperty ("knobs", knobs);
            effects.add (juce::var (o));
        }
        const auto specs = spectralRackParams();
        juce::Array<juce::var> warp;
        for (int k = 0; k < spectral::kWarp; ++k)
        {
            const auto& sp = specs[(size_t) spectral::iWarp (k)];
            auto* o = new juce::DynamicObject();
            o->setProperty ("id", juce::String (sp.id)); o->setProperty ("name", juce::String (sp.name)); o->setProperty ("min", sp.min); o->setProperty ("max", sp.max); o->setProperty ("start", sp.def);
            o->setProperty ("kind", sp.type == phantom::ParamSpec::Type::Choice ? "choice" : (sp.type == phantom::ParamSpec::Type::Bool ? "switch" : (sp.type == phantom::ParamSpec::Type::Int ? "whole" : "float")));
            juce::Array<juce::var> ch; for (const auto& x : sp.choices) ch.add (juce::String (x));
            o->setProperty ("choices", ch);
            warp.add (juce::var (o));
        }
        juce::Array<juce::var> windows, shapes;
        for (const auto& x : specs[(size_t) spectral::iWindow].choices) windows.add (juce::String (x).getIntValue());
        for (const auto& x : specs[(size_t) spectral::iShape].choices) shapes.add (juce::String (x));
        auto* root = new juce::DynamicObject();
        root->setProperty ("about", "SPECTRAL FX: its effects and their controls as upstream names them, and where each control starts in Ghost Atlas (kept between 0 and 1). Written by: GhostAtlasTests spectral list <file>");
        root->setProperty ("windows", windows); root->setProperty ("shapes", shapes); root->setProperty ("effects", effects); root->setProperty ("warp", warp);
        const auto out = juce::File::getCurrentWorkingDirectory().getChildFile (args[2]);
        out.replaceWithText (juce::JSON::toString (juce::var (root)).replace ("\r\n", "\n"));
        std::printf ("%d effects written to %s\n", effects.size(), out.getFullPathName().toRawUTF8());
        return 0;
    }
    Library lib; lib.load();
    PresetBank bank; bank.refresh();
    const Engine::Resolver resolver = [&lib] (const juce::String& id) { return lib.sfzPath (id); };
    Preset base; bool have = false;
    for (const auto& info : bank.list())
        if (info.factory && (args.size() < 2 || info.name.equalsIgnoreCase (args[1]))) { have = bank.load (info.file, base); break; }
    if (! have) { std::printf ("no such factory sound\n"); return 1; }
    for (auto& m : base.master) m = FxState();              // (the rack is heard on its own: nothing after it)
    for (auto& r : base.sends) r = FxState();
    for (int l = 1; l < kLayers; ++l) base.layers[(size_t) l] = LayerState();       // (and one layer, so that the rack has the whole sound)
    const double fs = 44100.0, seconds = 5.0;
    const std::vector<Note> notes = { { 48, 0.75f, 0.2, 2.2 }, { 60, 0.75f, 0.2, 2.2 }, { 64, 0.75f, 0.2, 2.2 }, { 67, 0.75f, 0.2, 2.2 }, { 72, 0.8f, 2.6, 0.25 }, { 76, 0.8f, 3.1, 0.25 } };
    auto take = [&] (const Preset& p) { return Engine::bounce (p, resolver, notes, seconds, fs); };
    const auto dry = take (base);
    // how far the rack's output is from the dry sound moved late by `lag` samples (dB under the dry sound's level)
    auto apartFromLate = [&] (const juce::AudioBuffer<float>& out, int lag) {
        double e = 0.0, d = 0.0;
        for (int i = (int) (0.3 * fs); i < (int) (2.3 * fs); ++i) { const double a = dry.getSample (0, i), b = out.getSample (0, i + lag); e += (a - b) * (a - b); d += a * a; }
        return 10.0 * std::log10 (juce::jmax (1.0e-20, e) / juce::jmax (1.0e-20, d));
    };
    std::printf ("%s; WARP CORE runs on %s\n", base.name.toRawUTF8(), spectral::warpAvailable() ? "this processor" : "nothing here (not built for this processor)");
    for (int e = 1; e < spectral::numEffects(); ++e)
    {
        auto p = base; p.layers[0].inserts[0] = fxDefault ("spectral"); spectral::put (p.layers[0].inserts[0].values, 0, e);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        const auto out = take (p);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        const int lag = spectral::usesWindow (e) ? 2047 : 0;
        // its first knob that turns (not a switch or a list): moved well away from where it starts
        int turn = -1; juce::String knobs;
        for (int k = 0; k < spectral::numKnobs (e); ++k)
        {
            const auto c = spectral::knob (e, k);
            if (turn < 0 && c.kind == spectral::cFloat) turn = k;
            knobs << (k ? ", " : "") << c.name << " " << spectral::written (e, k, spectral::fromKept (e, k, p.layers[0].inserts[0].values[spectral::idOf (spectral::iKnob (0, k))])).trim();
        }
        float moved = 0.0f; juce::String to;
        if (turn >= 0)
        {
            const auto c = spectral::knob (e, turn);
            const float at = spectral::toKept (e, turn, c.def), next = at < 0.45f ? at + 0.55f : at - 0.4f;
            auto q = p; q.layers[0].inserts[0].values[spectral::idOf (spectral::iKnob (0, turn))] = next;
            moved = picturesApart (soundPicture (out, fs), soundPicture (take (q), fs));
            to = c.name + " to " + spectral::written (e, turn, spectral::fromKept (e, turn, next)).trim();
        }
        // against the dry sound as late as the rack makes it (what the effect does as it is put in), heard as a picture
        juce::AudioBuffer<float> late (dry.getNumChannels(), dry.getNumSamples()); late.clear();
        for (int ch = 0; ch < dry.getNumChannels(); ++ch) late.copyFrom (ch, lag, dry, ch, 0, dry.getNumSamples() - lag);
        const float arrives = picturesApart (soundPicture (late, fs), soundPicture (out, fs));
        std::printf ("%2d %-17s %-20s %s put in: %5.1f dB apart (level %+5.1f dB); %-34s %5.1f dB more; %s%4.0f ms  [%s]\n", e, spectral::effectFolder (e).toRawUTF8(),
                     spectral::effectName (e).toRawUTF8(), spectral::usesWindow (e) ? "window " : "samples", (double) arrives, (double) (rmsDb (out, fs, 0.4, 2.0) - rmsDb (dry, fs, 0.4, 2.0)),
                     to.toRawUTF8(), (double) moved, allFinite (out) ? "" : "NOT FINITE ", ms, knobs.toRawUTF8());
        juce::ignoreUnused (apartFromLate);
    }
    if (args.contains ("curve"))
    {   // how far each effect moves the sound as its first turning knob goes up (to choose where an effect starts)
        for (int e = 1; e < spectral::numEffects(); ++e)
        {
            int turn = -1;
            for (int k = 0; k < spectral::numKnobs (e) && turn < 0; ++k) if (spectral::knob (e, k).kind == spectral::cFloat) turn = k;
            if (turn < 0) continue;
            const auto c = spectral::knob (e, turn);
            auto p = base; p.layers[0].inserts[0] = fxDefault ("spectral"); spectral::put (p.layers[0].inserts[0].values, 0, e);
            const auto was0 = soundPicture (take (p), fs);
            juce::String line;
            const float at = spectral::toKept (e, turn, c.def);
            for (float step : { 0.08f, 0.15f, 0.25f, 0.35f, 0.5f, 0.7f })
            {
                const float next = at + step <= 1.0f ? at + step : at - step;
                auto q = p; q.layers[0].inserts[0].values[spectral::idOf (spectral::iKnob (0, turn))] = next;
                const auto o = take (q);
                line << spectral::written (e, turn, spectral::fromKept (e, turn, next)).trim() << ": " << juce::String (picturesApart (was0, soundPicture (o, fs)), 1) << " dB (" << juce::String (rmsDb (o, fs, 0.4, 2.0) - rmsDb (dry, fs, 0.4, 2.0), 1) << ")   ";
            }
            std::printf ("%2d %-20s %-12s [%g .. %g, starts %g]  %s\n", e, spectral::effectName (e).toRawUTF8(), c.name.toRawUTF8(), (double) c.min, (double) c.max, (double) c.def, line.toRawUTF8());
        }
        return 0;
    }
    auto p = base; p.layers[0].inserts[0] = fxDefault ("spectral"); spectral::put (p.layers[0].inserts[0].values, 0, 0); p.layers[0].inserts[0].values["w_on"] = 1.0f;
    const auto out = take (p);
    std::printf ("   WARP CORE on its own: %5.1f dB apart, level %+5.1f dB%s\n", (double) picturesApart (soundPicture (dry, fs), soundPicture (out, fs)), (double) (rmsDb (out, fs, 0.4, 2.0) - rmsDb (dry, fs, 0.4, 2.0)), allFinite (out) ? "" : ", NOT FINITE");
    p = base; p.layers[0].inserts[0] = fxDefault ("spectral"); spectral::put (p.layers[0].inserts[0].values, 0, 0);
    std::printf ("   the rack with nothing in it: %5.1f dB from the dry sound\n", apartFromLate (take (p), 0));
    return 0;
}

// what MUTATE's ear makes of one sound: a preset ("factory:Name" or a file), or one instrument on its own
int heardCmd (const juce::StringArray& args)
{
    if (args.size() < 2) { std::printf ("heard <preset | instrument id or name>\n"); return 2; }
    Library lib; lib.load();
    PresetBank bank; bank.refresh();
    Preset p;
    if (! bank.load (args[1], p))
    {
        Instrument inst;
        if (! lib.findLoose (args[1], inst)) { for (const auto& i : lib.all()) if (i.name.equalsIgnoreCase (args[1])) { inst = i; break; } }
        if (inst.id.isEmpty()) { std::printf ("no preset or instrument called %s\n", args[1].toRawUTF8()); return 1; }
        p = Preset::blank(); p.name = inst.name;
        p.layers[0].instrument = inst.id; p.layers[0].p[lpOn] = 1.0f; p.layers[0].p[lpGain] = juce::jlimit (-18.0f, 9.0f, -19.0f - inst.f.loudnessDb); p.layers[0].p[lpSendC] = 0.3f; p.layers[0].role = "primary";
        std::printf ("%s: library says loudness %.1f, tuning %.1f cents (harmonicity %.2f), sustain %.2f, attack %.0f ms, brightness %.2f\n", inst.name.toRawUTF8(), inst.f.loudnessDb, inst.f.tuneCents, inst.f.harmonicity, inst.f.sustain, inst.f.attackMs, inst.f.brightness);
    }
    const Engine::Resolver resolver = [&lib] (const juce::String& id) { return lib.sfzPath (id); };
    Mutator mutator (lib, resolver);
    const auto mix = mutator.listen (p);
    const int mi = mutator.mainLayer (p);
    const auto alone = mi >= 0 ? mutator.listen (p, mi) : Heard();
    std::printf ("%s: peak %.1f, chord %.1f (onset %.1f), low %.0f %%, mono loss %.2f, ring %.1f, tune %.1f cents (strength %.2f), spread %.1f, touch %.1f, seams %.2f\n", p.name.toRawUTF8(),
                 mix.peakDb, mix.loudnessDb, mix.onsetDb, mix.lowShare * 100.0f, mix.monoLossDb, mix.ringDb, mix.tuneCents, mix.tuneStrength, mix.registerSpreadDb, mix.touchDb, mix.seams);
    for (const auto& c : mutator.judge (mix, &alone, p)) std::printf ("     %s %-30s %s\n", c.passed ? "ok  " : "FAIL", c.name.toRawUTF8(), c.found.toRawUTF8());
    if (args.size() > 2) writeWav (juce::File::getCurrentWorkingDirectory().getChildFile (args[2]), mix.audio, 44100.0);
    return 0;
}

// MUTATE against random layering: the same starting instruments, the same number of layers, the same measurements
int experiment (const juce::StringArray& args)
{
    if (args.size() < 2) { std::printf ("experiment <folder> [count]\n"); return 2; }
    const auto dir = juce::File::getCurrentWorkingDirectory().getChildFile (args[1]);
    dir.createDirectory();
    Library lib; lib.load();
    const Engine::Resolver resolver = [&lib] (const juce::String& id) { return lib.sfzPath (id); };
    Mutator mutator (lib, resolver);
    // the starting sounds: instruments that can carry a sound on their own, spread over the categories
    std::vector<Instrument> starts;
    std::map<juce::String, int> perCategory;
    for (const auto& i : lib.all())
        if (i.f.valid && i.f.harmonicity >= 0.8f && i.hiKey - i.loKey >= 36 && i.f.loopClick < 0.25f && perCategory[i.category]++ < 6) starts.push_back (i);
    const int count = juce::jmin ((int) starts.size(), args.size() > 2 ? args[2].getIntValue() : 30);
    juce::Random pickRng (7);
    while ((int) starts.size() > count) starts.erase (starts.begin() + pickRng.nextInt ((int) starts.size()));
    std::printf ("%d starting instruments\n", (int) starts.size());

    struct Row { juce::String start, kind; int layers = 0, failed = 0, tried = 0; Heard h; float behind = 0, masked = 0, pulled = 0; juce::String failures; bool produced = true, mainHolds = true; };
    std::vector<Row> rows;
    auto measure = [&] (const Preset& p, const juce::String& start, const juce::String& kind, juce::AudioBuffer<float>* audio) {
        Row r; r.start = start; r.kind = kind; r.layers = p.layerCount();
        r.h = mutator.listen (p);
        const int mi = mutator.mainLayer (p);
        const Heard alone = mi >= 0 ? mutator.listen (p, mi) : Heard();
        r.behind = r.h.loudnessDb - alone.loudnessDb;
        r.mainHolds = alone.holds;
        // two things MUTATE does not steer by directly, measured from the audio:
        // how much of the main sound lies in bands where the other layers are louder than it is,
        {
            double all = 0, under = 0;
            for (size_t b = 0; b < 24; ++b) { const double mainB = alone.bands[b], othersB = juce::jmax (0.0, (double) r.h.bands[b] - mainB); all += mainB; if (othersB > mainB) under += mainB; }
            r.masked = all > 0 ? (float) (under / all) : 0.0f;
        }
        // and how far the layers pull the pitch away from where the main sound alone sits
        r.pulled = (r.h.tuneStrength >= 0.15f && alone.tuneStrength >= 0.15f) ? std::abs (r.h.tuneCents - alone.tuneCents) : 0.0f;
        for (const auto& c : mutator.judge (r.h, &alone, p)) if (! c.passed) { ++r.failed; r.failures << (r.failures.isEmpty() ? "" : "; ") << c.name; }
        if (audio != nullptr) *audio = r.h.audio;
        return r;
    };
    juce::String tsv = "start\tkind\tlayers\tattempts\tmain_holds\tchecks_failed\tpeak_db\tloudness_db\tlow_share\tmono_loss_db\tring_db\ttune_cents\tregister_spread_db\tothers_add_db\tseams\tmain_masked\tpitch_pulled_cents\tfailures\n";
    int n = 0;
    for (const auto& inst : starts)
    {
        ++n;
        std::printf ("[%d/%d] %s\n", n, (int) starts.size(), inst.name.toRawUTF8()); std::fflush (stdout);
        Preset start = Preset::blank();
        start.name = inst.name; start.category = inst.category;
        start.layers[0].instrument = inst.id; start.layers[0].p[lpOn] = 1.0f; start.layers[0].p[lpGain] = juce::jlimit (-18.0f, 9.0f, -19.0f - inst.f.loudnessDb); start.layers[0].p[lpSendC] = 0.3f;
        start.layers[0].role = "primary";
        juce::AudioBuffer<float> aBefore, aMutate, aRandom, aBlind;
        rows.push_back (measure (start, inst.name, "alone", &aBefore));
        const auto m = mutator.mutate (start, MutateMode::Evolve, (juce::uint32) (1000 + n));
        int layers = 3;
        if (m.ok)
        {
            rows.push_back (measure (m.preset, inst.name, "mutate", &aMutate)); layers = m.preset.layerCount();
            dir.getChildFile (safeName (inst.name) + " - mutate.atlas").replaceWithText (juce::JSON::toString (m.preset.toVar()));
        }
        else { Row r; r.start = inst.name; r.kind = "mutate"; r.produced = false; r.failures = m.failure; rows.push_back (r); }
        rows.back().tried = m.tried;
        // the control that separates the choosing from everything else MUTATE does: MUTATE as it is, except that its layers are
        // drawn blind from the whole library (the same parts in the same order, the same setting-up, the same listening and
        // putting right, the same four attempts); three draws
        bool blindHeard = false;
        for (int k = 0; k < 3; ++k)
        {
            mutator.chooseBlind (true, 5000 + n * 10 + k);
            const auto b = mutator.mutate (start, MutateMode::Evolve, (juce::uint32) (1000 + n));
            mutator.chooseBlind (false);
            if (b.ok)
            {
                rows.push_back (measure (b.preset, inst.name, "blind", blindHeard ? nullptr : &aBlind));
                if (! blindHeard) dir.getChildFile (safeName (inst.name) + " - blind.atlas").replaceWithText (juce::JSON::toString (b.preset.toVar()));
                blindHeard = true;
            }
            else { Row r; r.start = inst.name; r.kind = "blind"; r.produced = false; r.failures = b.failure; rows.push_back (r); }
            rows.back().tried = b.tried;
        }
        // random layering: the same number of layers, each level-matched by its measured loudness, nothing else done (three draws)
        for (int k = 0; k < 3; ++k)
        {
            juce::Random rr (9000 + n * 10 + k);
            const auto rp = mutator.randomLayering (start, layers, rr);
            rows.push_back (measure (rp, inst.name, "random", k == 0 ? &aRandom : nullptr));
            if (k == 0) dir.getChildFile (safeName (inst.name) + " - random.atlas").replaceWithText (juce::JSON::toString (rp.toVar()));
        }
        if (n <= 12)
        {   // the chord of each, one after another, to listen to: alone, MUTATE, random
            // (all three at the loudness of the starting sound, short of clipping: a fair listen)
            const float want = measure::loudnessDb (aBefore, 44100.0, 44100 * 12, 44100 * 3);
            auto chord = [want] (const juce::AudioBuffer<float>& a) {
                juce::AudioBuffer<float> c (2, 44100 * 5); c.clear();
                if (a.getNumSamples() < 44100 * 17) return c;
                for (int ch = 0; ch < 2; ++ch) c.copyFrom (ch, 0, a, ch, 44100 * 12, 44100 * 5);
                const float have = measure::loudnessDb (c, 44100.0, 0, 44100 * 3), top = measure::peakOf (c);
                if (top > 0.0f) c.applyGain (juce::jmin (juce::Decibels::decibelsToGain (want - have), 0.97f / top));
                return c;
            };
            writeWav (dir.getChildFile (safeName (inst.name) + " - 1 alone.wav"), chord (aBefore), 44100.0);
            if (m.ok) writeWav (dir.getChildFile (safeName (inst.name) + " - 2 mutate.wav"), chord (aMutate), 44100.0);
            writeWav (dir.getChildFile (safeName (inst.name) + " - 3 random.wav"), chord (aRandom), 44100.0);
            if (blindHeard) writeWav (dir.getChildFile (safeName (inst.name) + " - 4 blind.wav"), chord (aBlind), 44100.0);
        }
    }
    struct Sum { int n = 0, clean = 0, failed = 0, produced = 0, first = 0; double low = 0, mono = 0, ring = 0, tune = 0, spread = 0, behind = 0, peak = 0, masked = 0, pulled = 0; };
    std::map<juce::String, Sum> sums;
    for (const auto& r : rows)
    {
        auto& s = sums[r.kind]; ++s.n;
        if (! r.produced) { tsv << r.start << "\t" << r.kind << "\t0\t" << r.tried << "\t-\t-\t-\t-\t-\t-\t-\t-\t-\t-\t-\t-\t-\t" << r.failures << "\n"; continue; }
        ++s.produced; s.failed += r.failed; if (r.failed == 0) ++s.clean; if (r.tried == 1) ++s.first;
        s.low += r.h.lowShare; s.mono += r.h.monoLossDb; s.ring += r.h.ringDb; s.tune += std::abs (r.h.tuneCents); s.spread += r.h.registerSpreadDb; s.behind += juce::jmax (0.0f, r.behind); s.peak += r.h.peakDb; s.masked += r.masked; s.pulled += r.pulled;
        tsv << r.start << "\t" << r.kind << "\t" << r.layers << "\t" << r.tried << "\t" << (r.mainHolds ? 1 : 0) << "\t" << r.failed << "\t" << juce::String (r.h.peakDb, 1) << "\t" << juce::String (r.h.loudnessDb, 1) << "\t" << juce::String (r.h.lowShare, 3) << "\t"
            << juce::String (r.h.monoLossDb, 2) << "\t" << juce::String (r.h.ringDb, 1) << "\t" << juce::String (r.h.tuneCents, 1) << "\t" << juce::String (r.h.registerSpreadDb, 1) << "\t" << juce::String (r.behind, 1) << "\t"
            << juce::String (r.h.seams, 2) << "\t" << juce::String (r.masked, 3) << "\t" << juce::String (r.pulled, 1) << "\t" << r.failures << "\n";
    }
    dir.getChildFile ("results.tsv").replaceWithText (tsv);
    juce::String md;
    md << "# MUTATE against random layering\n\n" << (int) starts.size() << " starting instruments from the library, each played four ways:\n\n"
       << "- **alone**: the instrument by itself.\n"
       << "- **mutate**: MUTATE (EVOLVE) as the instrument does it: layers chosen by likeness and then by fit, set up for their parts, played, put right, and tried again if they do not hold up.\n"
       << "- **blind**: MUTATE exactly as above except that its layers are drawn blind from the whole library instead of chosen (the same parts, the same setting-up, the same listening and putting right, "
       << "the same four attempts; three draws each). This is the control that separates the choosing from everything else MUTATE does.\n"
       << "- **random**: the same number of layers drawn at random, each set to the same measured loudness and nothing else done (three draws each). This is plain random layering.\n\n"
       << "Every sound is played through the engine over the same notes (low, middle, high, soft, hard, a five-note chord) with the master effects off, and measured the same way.\n\n"
       << "| | sounds | held up at the first attempt | pass every check | main sound masked | pitch pulled (cents) | others add (dB) | under 150 Hz | mono loss (dB) | sharpest ring (dB) | off pitch (cents) |\n|---|---|---|---|---|---|---|---|---|---|---|\n";
    auto pct = [] (double v) { return juce::String ((int) std::lround (100.0 * v)) + " %"; };
    for (const auto* kind : { "alone", "mutate", "blind", "random" })
    {
        const auto& s = sums[kind];
        if (s.n == 0) continue;
        const double k = juce::jmax (1, s.produced);
        const bool attempts = juce::String (kind) == "mutate" || juce::String (kind) == "blind";
        md << "| " << kind << " | " << s.produced << (s.produced < s.n ? " of " + juce::String (s.n) : juce::String()) << " | " << (attempts ? juce::String (s.first) + " (" + pct (s.first / (double) s.n) + ")" : juce::String ("-")) << " | "
           << s.clean << " (" << pct (s.clean / (double) s.n) << ") | "
           << pct (s.masked / k) << " | " << juce::String (s.pulled / k, 1) << " | " << juce::String (s.behind / k, 1) << " | " << pct (s.low / k) << " | " << juce::String (s.mono / k, 2) << " | "
           << juce::String (s.ring / k, 1) << " | " << juce::String (s.tune / k, 1) << " |\n";
    }
    md << "\nHow to read it. \"Pass every check\" uses MUTATE's own nine listening checks (it sounds; nothing clips; the low end is not piling up; the main sound is not buried; no harsh ring; "
       << "in tune; holds up in mono; no loop seams; even across the keys). MUTATE and the blind control both work towards those checks and give up on what fails them, so what they tell apart is "
       << "\"held up at the first attempt\": how often the first set of layers survived the listening. Two columns are things MUTATE does not steer by directly: \"main sound masked\" is the share of the "
       << "starting sound's energy, over the whole held chord, that lies in bands where the added layers are louder than it is, and \"pitch pulled\" is how far the added layers move the measured pitch "
       << "from where the starting sound sits on its own. Lower is better in both. \"Others add\" is how much louder the whole is than the starting sound alone.\n\n"
       << "What it cannot say: whether a sound is more beautiful. That is for ears. The audio for the first twelve starting instruments is beside this file (a five-note chord of each: alone, MUTATE, "
       << "random, blind; all at the same loudness), and every row is in results.tsv.\n";
    dir.getChildFile ("report.md").replaceWithText (md);
    std::printf ("\n%s\n", md.toRawUTF8());
    return 0;
}

// pictures of the screen: each page (and each world on the Play page), drawn by the real editor
int shots (const juce::StringArray& args)
{
    if (args.size() < 2) { std::printf ("shots <folder> [width]\n"); return 2; }
    const auto dir = juce::File::getCurrentWorkingDirectory().getChildFile (args[1]);
    dir.createDirectory();
    const int width = args.size() > 2 ? juce::jlimit (880, 2640, args[2].getIntValue()) : 1320;
    Processor proc;
    proc.setRateAndBufferSizeDetails (48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* editor = dynamic_cast<Editor*> (ed.get());
    if (editor == nullptr) return 1;
    editor->setSize (width, (int) std::lround (width * 720.0 / 1320.0));
    editor->setVisible (true);
    // a little sound, so the meters and the layers' lanterns have something to show
    juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer midi;
    auto play = [&] (int blocks) { for (int i = 0; i < blocks; ++i) { buf.clear(); proc.processBlock (buf, midi); midi.clear(); } };
    auto snap = [&] (const juce::String& name) {
        proc.engine().waitUntilLoaded (20000);
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, 0.8f), 0); midi.addEvent (juce::MidiMessage::noteOn (1, 67, 0.7f), 0);
        play (60);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (80);      // (what was just asked for reaches the screen)
        editor->settle();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (120);
        const auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        juce::PNGImageFormat png;
        const auto f = dir.getChildFile (name + ".png");
        f.deleteFile();
        if (auto os = f.createOutputStream()) png.writeImageToStream (img, *os);
        midi.addEvent (juce::MidiMessage::allNotesOff (1), 0); play (4);
        std::printf ("  %s\n", f.getFileName().toRawUTF8());
    };
    std::vector<PresetInfo> all;
    { const juce::ScopedLock sl (proc.bankLock()); all = proc.bank().list(); }
    // one sound from each world on the Play page
    for (int w = 0; w < kWorlds; ++w)
    {
        for (const auto& p : all) if (worldIndex (p.world) == w) { proc.loadPresetFile (p.file); break; }
        if (all.empty()) { Preset p = Preset::blank(); p.name = juce::String (world (w).name) + " (no presets yet)"; p.world = world (w).key; proc.loadPreset (p); }
        editor->showPage (0);
        snap ("play-" + juce::String (w + 1) + "-" + world (w).key);
    }
    editor->showPage (1); snap ("layers");
    editor->showPage (2); snap ("effects");
    editor->showOverlay ("send-a"); snap ("effects-send-a");
    editor->showOverlay ("master-1"); snap ("effects-master-eq");
    editor->showOverlay ("layer-1"); snap ("effects-layer");
    {   // SPECTRAL FX on the master: a place of its chain, a Partial effect (which has modes), WARP CORE, and the window
        const Preset first = proc.capture(); Preset q = first;
        auto effect = [] (const char* name) { for (int e = 1; e < spectral::numEffects(); ++e) if (spectral::effectName (e) == name) return e; return 0; };
        q.master[0] = fxDefault ("spectral");
        spectral::put (q.master[0].values, 1, effect ("Spectral Filter")); spectral::put (q.master[0].values, 2, effect ("Stretcher"));
        q.master[0].values["w_on"] = 1.0f; q.name = "Spectral";
        proc.loadPreset (q);
        editor->showPage (2); editor->showOverlay ("master-1"); snap ("effects-rack");
        editor->showOverlay ("rack-3"); snap ("effects-rack-partial");
        editor->showOverlay ("rack-4"); snap ("effects-rack-empty");
        editor->showOverlay ("rack-warp"); snap ("effects-rack-warp");
        editor->showOverlay ("rack-window"); snap ("effects-rack-window");
        // two of the factory sounds made round the rack, as they arrive: a rack on a send, and WARP CORE alone on a layer
        auto factory = [&] (const char* name) { for (const auto& p : all) if (p.factory && p.name == name) { proc.loadPresetFile (p.file); return true; } return false; };
        if (factory ("Held Breath")) { editor->showPage (2); editor->showOverlay ("send-b"); snap ("effects-factory-rack-send"); }
        if (factory ("Turned Bell")) { editor->showPage (2); editor->showOverlay ("layer-1"); editor->showOverlay ("rack-warp"); snap ("effects-factory-rack-warp"); }
        proc.loadPreset (first);
    }
    editor->showOverlay ("send-c");
    editor->showPage (3); snap ("library");
    editor->showOverlay ("instrument"); snap ("library-instrument");
    editor->showOverlay ("mapping"); snap ("library-mapping");
    {   // a sound with all twelve layers in use: how the lanterns, the rows and the sends' bars stand then
        Preset first = proc.capture(), full = first;
        std::vector<LayerState> have;
        for (const auto& l : first.layers) if (l.used()) have.push_back (l);
        if (! have.empty())
        {
            for (int l = 0; l < kLayers; ++l) { full.layers[(size_t) l] = have[(size_t) l % have.size()]; full.layers[(size_t) l].voices = 4; }
            full.name = "Twelve Layers";
            proc.loadPreset (full);
            editor->showPage (0); snap ("play-twelve");
            editor->showPage (1); snap ("layers-twelve");
            editor->showPage (2); editor->showOverlay ("send-c"); snap ("effects-twelve");
            proc.loadPreset (first);
        }
    }
    editor->showPage (1); editor->showOverlay ("picker"); snap ("picker");
    editor->showPage (0); editor->showOverlay ("save"); snap ("save");
    editor->showOverlay ("mutate"); snap ("mutate");
    ed.reset();
    return 0;
}
// How hard a sound is to play: the plug-in itself, fed fast notes over a held chord in real time, at each sample rate and
// block size a host is likely to use. What is timed is processBlock alone, on this thread.
// how long a sound takes to arrive when it is chosen, as the plug-in does it (each layer and effect built on the loader
// thread): every sound with the tag (or whose name holds one of the words), chosen twice, after a sound of another kind;
// and each of its instruments loaded alone, to see which ones take the time
int loadTimeCmd (const juce::StringArray& args)
{
    juce::StringArray want;
    for (int i = 1; i < args.size(); ++i) want.add (args[i]);
    if (want.isEmpty()) want.add ("edm");
    Processor proc;
    proc.setRateAndBufferSizeDetails (48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    std::vector<PresetInfo> all;
    { const juce::ScopedLock sl (proc.bankLock()); all = proc.bank().list(); }
    std::vector<PresetInfo> picked;
    for (const auto& p : all)
        for (const auto& w : want)
            if (p.tags.contains (w) || p.name.containsIgnoreCase (w)) { picked.push_back (p); break; }
    juce::String neutral;
    for (const auto& p : all) if (! p.tags.contains (want[0])) { neutral = p.file; break; }
    std::printf ("loadtime\tsound\tlayers\tfirst_ms\tagain_ms\tslowest_instrument\tits_ms\tinstruments_ms\n");
    double worst = 0, sum = 0;
    for (const auto& p : picked)
    {
        auto timeIt = [&] {
            proc.loadPresetFile (neutral); proc.engine().waitUntilLoaded (60000);
            const double a = juce::Time::getMillisecondCounterHiRes();
            proc.loadPresetFile (p.file);
            proc.engine().waitUntilLoaded (120000);
            return juce::Time::getMillisecondCounterHiRes() - a;
        };
        const double first = timeIt(), again = timeIt();
        juce::String slowest; double slowestMs = 0, instMs = 0;
        for (const auto& id : p.instruments)
        {
            const auto path = proc.library().sfzPath (id);
            if (path.isEmpty()) continue;
            SfzLayer l; l.prepare (48000.0, 512); l.setVoices (32);
            const double a = juce::Time::getMillisecondCounterHiRes();
            l.load (path.toStdString(), SfzShape());
            const double ms = juce::Time::getMillisecondCounterHiRes() - a;
            instMs += ms;
            if (ms > slowestMs) { slowestMs = ms; slowest = id; }
        }
        std::printf ("loadtime\t%s\t%d\t%.0f\t%.0f\t%s\t%.0f\t%.0f\n", p.name.toRawUTF8(), p.instruments.size(), first, again, slowest.toRawUTF8(), slowestMs, instMs);
        std::fflush (stdout);
        worst = juce::jmax (worst, first); sum += first;
    }
    std::printf ("loadtime: %d sounds, %.0f ms on average to arrive, the slowest %.0f ms\n", (int) picked.size(), picked.empty() ? 0.0 : sum / (double) picked.size(), worst);
    return 0;
}

int stressCmd (const juce::StringArray& args)
{
    juce::StringArray names;
    for (int i = 1; i < args.size(); ++i) if (! args[i].containsOnly ("0123456789.")) names.add (args[i]);
    if (names.isEmpty()) names.add ("Eight Lanterns");
    double seconds = 5.0;
    for (int i = 1; i < args.size(); ++i) if (args[i].containsOnly ("0123456789.")) seconds = juce::jlimit (2.0, 30.0, args[i].getDoubleValue());
    static const int run[16] = { 48, 60, 67, 72, 55, 64, 71, 76, 50, 62, 69, 74, 57, 65, 72, 79 };
    static const int chord[5] = { 36, 52, 59, 64, 67 };
    std::printf ("sound\trate\tblock\tload_avg_pct\tload_worst_pct\tblocks_late\tvoices_most\tpeak_db\tbad_samples\n");
    juce::String heaviest; double heaviestLoad = -1.0; int lateInAll = 0, badInAll = 0;
    for (const auto& wanted : names)
        for (double rate : { 44100.0, 48000.0, 96000.0 })
            for (int block : { 64, 128, 256 })
            {
                Processor proc;
                proc.setRateAndBufferSizeDetails (rate, block);
                proc.prepareToPlay (rate, block);
                std::vector<PresetInfo> all;
                { const juce::ScopedLock sl (proc.bankLock()); all = proc.bank().list(); }
                juce::String found;
                for (const auto& p : all) if (p.name.containsIgnoreCase (wanted)) { proc.loadPresetFile (p.file); found = p.name; break; }
                if (found.isEmpty()) { std::printf ("no sound has \"%s\" in its name\n", wanted.toRawUTF8()); return 1; }
                proc.engine().waitUntilLoaded (60000);
                juce::AudioBuffer<float> buf (2, block); juce::MidiBuffer midi;
                const double blockSec = (double) block / rate;
                const int blocks = (int) (seconds / blockSec);
                double sum = 0, worst = 0; int late = 0, voicesMost = 0, bad = 0, step = -1; float peak = 0;
                const double t0 = juce::Time::getMillisecondCounterHiRes();
                for (int b = 0; b < blocks; ++b)
                {
                    // sixteenth notes at 140 to the minute over a held chord: every note a new voice in every layer
                    const int s16 = (int) ((double) b * blockSec / (60.0 / 140.0 / 4.0));
                    if (s16 != step)
                    {
                        if (step >= 0) midi.addEvent (juce::MidiMessage::noteOff (1, run[step % 16]), 0);
                        step = s16;
                        midi.addEvent (juce::MidiMessage::noteOn (1, run[step % 16], 0.5f + 0.1f * (float) ((step * 7) % 5)), 0);
                        if (step == 0) for (int n : chord) midi.addEvent (juce::MidiMessage::noteOn (1, n, 0.7f), 0);
                    }
                    buf.clear();
                    const double a = juce::Time::getMillisecondCounterHiRes();
                    proc.processBlock (buf, midi);
                    const double took = (juce::Time::getMillisecondCounterHiRes() - a) * 0.001;
                    midi.clear();
                    sum += took; worst = juce::jmax (worst, took); if (took > blockSec) ++late;
                    voicesMost = juce::jmax (voicesMost, proc.engine().voices());
                    for (int c = 0; c < 2; ++c)
                    {
                        const float* d = buf.getReadPointer (c);
                        for (int i = 0; i < block; ++i) { if (! std::isfinite (d[i])) ++bad; else peak = juce::jmax (peak, std::abs (d[i])); }
                    }
                    // in real time, as a host would ask for it (the recordings stream from the disk at the speed of a performance)
                    const double due = t0 + (double) (b + 1) * blockSec * 1000.0;
                    while (juce::Time::getMillisecondCounterHiRes() + 1.0 < due) juce::Thread::sleep (1);
                }
                const double avg = 100.0 * sum / ((double) blocks * blockSec), top = 100.0 * worst / blockSec;
                std::printf ("%s\t%d\t%d\t%.1f\t%.0f\t%d\t%d\t%.1f\t%d\n", found.toRawUTF8(), (int) rate, block, avg, top, late, voicesMost, (double) gainToDb (juce::jmax (1.0e-6f, peak)), bad);
                std::fflush (stdout);
                if (avg > heaviestLoad) { heaviestLoad = avg; heaviest = found + " at " + juce::String ((int) rate) + " Hz in blocks of " + juce::String (block); }
                lateInAll += late; badInAll += bad;
            }
    std::printf ("\nstress: the heaviest was %s: %.1f %% of one processor on average; %d blocks in all took longer than their own length; %d samples were not numbers\n",
                 heaviest.toRawUTF8(), heaviestLoad, lateInAll, badInAll);
    return badInAll > 0 ? 1 : 0;
}

// the Play page in motion: pictures of the screen taken at a steady rate while the world's film plays (to look at as a clip)
int filmClip (const juce::StringArray& args)
{
    if (args.size() < 2) { std::printf ("film <folder> [world 0..7] [seconds] [pictures a second] [width] [play] [still|alive|film] [sound=<part of a name>]\n"); return 2; }
    const auto dir = juce::File::getCurrentWorkingDirectory().getChildFile (args[1]);
    dir.createDirectory();
    const int w = args.size() > 2 ? juce::jlimit (0, kWorlds - 1, args[2].getIntValue()) : 0;
    const double seconds = args.size() > 3 ? args[3].getDoubleValue() : 12.0;
    const int rate = args.size() > 4 ? juce::jlimit (2, 30, args[4].getIntValue()) : 15;
    const int width = args.size() > 5 ? juce::jlimit (880, 2640, args[5].getIntValue()) : 1320;
    const bool playing = args.contains ("play");
    const int motion = args.contains ("still") ? 0 : (args.contains ("alive") ? 1 : 2);
    Processor proc;
    proc.setRateAndBufferSizeDetails (48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
    auto* editor = dynamic_cast<Editor*> (ed.get());
    if (editor == nullptr) return 1;
    editor->setSize (width, (int) std::lround (width * 720.0 / 1320.0));
    editor->setVisible (true);
    std::vector<PresetInfo> all;
    { const juce::ScopedLock sl (proc.bankLock()); all = proc.bank().list(); }
    // (with "play": a few notes and a chord are played as the pictures are taken, in step with the clock)
    juce::AudioBuffer<float> buf (2, 512); juce::MidiBuffer midi;
    static const int tune[] = { 48, 55, 60, 64, 67, 72, 76, 79, 84, 72, 67, 60 };
    double played = 0; int struck = -1, sounding = -1;
    std::array<std::vector<float>, 2> heard;            // what was played, kept so the pictures can be given their sound
    auto playUntil = [&] (double seconds) {
        while (played < seconds)
        {
            const int step = (int) (played / 0.45);
            if (step != struck)
            {
                struck = step;
                if (sounding >= 0) midi.addEvent (juce::MidiMessage::noteOff (1, sounding), 0);
                if (step % 16 == 13) { for (int n : { 48, 60, 64, 67, 71 }) midi.addEvent (juce::MidiMessage::noteOn (1, n, 0.75f), 0); sounding = -1; }
                else if (step % 16 == 0 && step > 0) { for (int n : { 48, 60, 64, 67, 71 }) midi.addEvent (juce::MidiMessage::noteOff (1, n), 0); sounding = -1; }
                else if (step % 16 < 13) { sounding = tune[step % 12]; midi.addEvent (juce::MidiMessage::noteOn (1, sounding, 0.55f + 0.03f * (float) (step % 7)), 0); }
            }
            buf.clear(); proc.processBlock (buf, midi); midi.clear();
            for (int c = 0; c < 2; ++c) heard[(size_t) c].insert (heard[(size_t) c].end(), buf.getReadPointer (c), buf.getReadPointer (c) + 512);
            played += 512.0 / 48000.0;
        }
    };
    juce::String wanted;
    for (const auto& a : args) if (a.startsWith ("sound=")) wanted = a.fromFirstOccurrenceOf ("=", false, false);
    bool loaded = false;
    if (wanted.isNotEmpty()) for (const auto& p : all) if (p.name.containsIgnoreCase (wanted)) { proc.loadPresetFile (p.file); loaded = true; break; }
    if (! loaded) for (const auto& p : all) if (worldIndex (p.world) == w) { proc.loadPresetFile (p.file); loaded = true; break; }
    if (! loaded) { Preset p = Preset::blank(); p.name = world (w).name; p.world = world (w).key; proc.loadPreset (p); }
    proc.engine().waitUntilLoaded (20000);
    editor->showPage (0);
    editor->setMotion (motion);
    editor->settle();
    juce::JPEGImageFormat jpeg; jpeg.setQuality (0.9f);
    const int count = (int) std::lround (seconds * rate);
    const auto t0 = juce::Time::getMillisecondCounterHiRes();
    double paint = 0;
    for (int k = 0; k < count; ++k)
    {
        const double due = t0 + 1000.0 * (double) (k + 1) / (double) rate;
        while (juce::Time::getMillisecondCounterHiRes() < due)
        {
            if (playing) playUntil ((juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
        }
        const auto p0 = juce::Time::getMillisecondCounterHiRes();
        const auto img = editor->createComponentSnapshot (editor->getLocalBounds(), true, 1.0f);
        paint += juce::Time::getMillisecondCounterHiRes() - p0;
        const auto f = dir.getChildFile ("f_" + juce::String (k).paddedLeft ('0', 4) + ".jpg");
        f.deleteFile();
        if (auto os = f.createOutputStream()) jpeg.writeImageToStream (img, *os);
    }
    if (playing && ! heard[0].empty())
    {
        juce::AudioBuffer<float> all2 (2, (int) heard[0].size());
        for (int c = 0; c < 2; ++c) all2.copyFrom (c, 0, heard[(size_t) c].data(), (int) heard[(size_t) c].size());
        writeWav (dir.getChildFile ("sound.wav"), all2, 48000.0);
    }
    std::printf ("%s: %d pictures of the screen at %d a second; the film made %d of its own (the last in %.1f ms), showing: %s; drawing the whole screen took %.1f ms on average\n",
                 world (w).name, count, rate, editor->filmPictures(), (double) editor->filmMakeMs(), editor->filmShowing() ? "yes" : "no", paint / juce::jmax (1, count));
    ed.reset();
    return 0;
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add (juce::String::fromUTF8 (argv[i]));
    if (args.isEmpty()) return runChecks();
    const auto cmd = args[0];
    std::printf ("Ghost Atlas %s - home: %s\n", GHOSTATLAS_VERSION, atlasRoot().getFullPathName().toRawUTF8());
    if (cmd == "index")
    {
        Library lib; lib.load();
        const auto rep = lib.indexManagedLibrary (printer(), ! args.contains ("--no-analyse"));
        std::printf ("\n%s\n\n%d added, %d updated, %d could not be used; %d instruments in the library\n", rep.text().toRawUTF8(), rep.added, rep.updated, rep.failed, lib.size());
        reportsDir().createDirectory();
        reportsDir().getChildFile ("last-index.txt").replaceWithText (rep.text());
        reportsDir().getChildFile ("library-manifest.txt").replaceWithText (lib.manifestText());
        return 0;
    }
    if (cmd == "import" && args.size() > 1)
    {
        Library lib; lib.load();
        const auto rep = lib.importFolder (juce::File::getCurrentWorkingDirectory().getChildFile (args[1]), ! args.contains ("--link"), printer());
        std::printf ("\n%s\n", rep.text().toRawUTF8());
        return rep.failed > 0 && rep.added + rep.updated == 0 ? 1 : 0;
    }
    if (cmd == "analyse") { Library lib; lib.load(); std::printf ("%d analysed\n", lib.analyseMissing (printer(), args.contains ("--all"))); return 0; }
    if (cmd == "list") return listInstruments();
    if (cmd == "manifest")
    {
        Library lib; lib.load();
        const auto t = lib.manifestText();
        reportsDir().createDirectory(); reportsDir().getChildFile ("library-manifest.txt").replaceWithText (t);
        std::printf ("%s\n", t.toRawUTF8());
        return 0;
    }
    if (cmd == "render") return render (args);
    if (cmd == "shots") return shots (args);
    if (cmd == "mutate") return mutateOnce (args);
    if (cmd == "heard") return heardCmd (args);
    if (cmd == "factory") return factoryCmd();
    if (cmd == "polish") return polishCmd (args);
    if (cmd == "scan" && args.size() > 1)
    {
        const auto f = juce::File::getCurrentWorkingDirectory().getChildFile (args[1]);
        const auto s = SfzLayer::scan (f.getFullPathName().toStdString(), false);
        std::printf ("ok %d (%s) regions %d keyswitched %d included %d samples %d missing %d keys %d..%d\n", (int) s.ok, s.error.c_str(), s.regions, s.keyswitchRegions, (int) s.included.size(), (int) s.samples.size(), (int) s.missing.size(), s.loKey, s.hiKey);
        for (size_t i = 0; i < s.included.size() && i < 6; ++i) std::printf ("  includes %s\n", s.included[i].c_str());
        if (! s.samples.empty()) std::printf ("  first sample %s (%s)\n", s.samples.front().c_str(), juce::File (juce::String (s.samples.front())).existsAsFile() ? "there" : "NOT THERE");
        return 0;
    }
    if (cmd == "units")
    {   // every effect unit and its controls, for whoever writes presets
        for (const auto& u : fxUnits())
        {
            std::printf ("%s  (%s)\n", u.key.toRawUTF8(), fxTitle (u.key).toRawUTF8());
            for (const auto& p : fxParams (u.key))
            {
                juce::String c; for (const auto& ch : p.choices) c << (c.isEmpty() ? "" : "|") << ch;
                std::printf ("    %-12s %-16s %g..%g def %g %s\n", juce::String (p.id).toRawUTF8(), juce::String (p.name).toRawUTF8(), (double) p.min, (double) p.max, (double) p.def, c.toRawUTF8());
            }
        }
        return 0;
    }
    if (cmd == "reimagine") return reimagineOnce (args);
    if (cmd == "experiment") return experiment (args);
    if (cmd == "film") return filmClip (args);
    if (cmd == "stress") return stressCmd (args);
    if (cmd == "loadtime") return loadTimeCmd (args);
    if (cmd == "fxaudit") return fxAudit (args);
    if (cmd == "spectral") return spectralCmd (args);
    std::printf ("unknown command: %s\n", cmd.toRawUTF8());
    return 2;
}
