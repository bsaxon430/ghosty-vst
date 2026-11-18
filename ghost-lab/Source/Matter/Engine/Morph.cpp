// Ghost Matter - AGPL-3.0-only (see LICENSE, NOTICE.md).
#include "Morph.h"
#include "MakeStage.h"
#include "Dsp.h"
#include "Cdp.h"
#include "Ddsp.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <cstring>

namespace matter
{
bool morphIsOff (const MorphSettings& m)
{
    const bool morphPins = hood::hasEngine (m.pins, "cdp");
    const bool spectral = m.timbre != Timbre::none && (m.amount > 0.001f || m.detail > 0.001f || morphPins) && (m.timbre != Timbre::own || m.ownAudio.getSize() > 0);
    const bool neural = m.neural > 0.001f && m.ddspModel.isNotEmpty();
    return ! spectral && ! neural;
}

juce::AudioBuffer<float> timbreSound (Timbre t, const MorphSettings& m)
{
    MakeSettings s;
    s.seconds = 3.0f; s.loud = 0.6f; s.note = kRootNote;
    switch (t)
    {
        case Timbre::voice:  s.source = Source::voice; s.body = 0; s.shape = 0.9f; s.bright = 0.55f; s.rough = 0.1f; break;
        case Timbre::metal:  s.source = Source::physical; s.body = 6; s.shape = 0.7f; s.bright = 0.7f; s.rough = 0.45f; break;
        case Timbre::air:    s.source = Source::voice; s.body = 7; s.shape = 0.3f; s.bright = 0.75f; s.rough = 0.3f; break;
        case Timbre::glass:  s.source = Source::physical; s.body = 8; s.shape = 0.6f; s.bright = 0.85f; s.rough = 0.15f; break;
        case Timbre::throat: s.source = Source::voice; s.body = 5; s.shape = 0.4f; s.bright = 0.3f; s.rough = 0.85f; break;
        case Timbre::wood:   s.source = Source::physical; s.body = 1; s.shape = 0.5f; s.bright = 0.4f; s.rough = 0.25f; break;
        case Timbre::own:    return decodeAudio (m.ownAudio, kMaxSourceSeconds);
        default: return {};
    }
    if (s.source == Source::physical) s.loud = 0.0f;   // PM-Synth's own DC level at 0: clean (Presets.cpp)
    return makeSound (s);
}

// CDP's own phase vocoder and spectral programs, one channel at a time (CDP works on mono files): glue only
juce::AudioBuffer<float> spectralMorph (const juce::AudioBuffer<float>& in, const juce::AudioBuffer<float>& otherIn, float amount, float detail,
                                        juce::String* why, const std::atomic<bool>* cancel, const hood::Pins* pinsIn)
{
    const hood::Pins noPins;
    const auto& pins = pinsIn != nullptr ? *pinsIn : noPins;
    auto pin = [&pins] (const char* id, float fallback) { return hood::pinOr (pins, (std::string ("cdp.") + id).c_str(), fallback); };
    auto pinned = [&pins] (const char* id) { return pins.count (std::string ("cdp.") + id) > 0; };
    const bool morphPins = pinned ("morph.mode") || pinned ("morph.as") || pinned ("morph.ae") || pinned ("morph.fs") || pinned ("morph.fe")
                        || pinned ("morph.expa") || pinned ("morph.expf") || pinned ("morph.stagger");
    if (! cdp::available()) { if (why != nullptr) *why = "the morph programs are not installed"; return in; }
    const int n = in.getNumSamples();
    if (n < 4096 || otherIn.getNumSamples() < 64) return in;
    // the other sound, repeated to our length (CDP stops at the shorter file): glue
    juce::AudioBuffer<float> other (2, n);
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < n; ++i)
            other.setSample (c, i, otherIn.getSample (juce::jmin (c, otherIn.getNumChannels() - 1), i % otherIn.getNumSamples()));
    auto job = cdp::newJob();
    juce::AudioBuffer<float> out (2, n);
    out.clear();
    bool ok = true;
    const double dur = n / kRate;
    for (int c = 0; c < 2 && ok; ++c)
    {
        auto a = job.getChildFile ("a.wav"), b = job.getChildFile ("b.wav");
        auto aa = job.getChildFile ("a.ana"), ba = job.getChildFile ("b.ana"), xa = job.getChildFile ("x.ana"), ma = job.getChildFile ("m.ana");
        auto o = job.getChildFile ("o.wav");
        ok = cdp::writeMonoWav (a, in.getReadPointer (juce::jmin (c, in.getNumChannels() - 1)), n, kRate)
          && cdp::writeMonoWav (b, other.getReadPointer (c), n, kRate);
        // pvoc anal mode 1 at CDP's own defaults (1024 points, overlap 3)
        auto step = [&] (juce::StringArray args, const juce::File& expect)
        {
            if (! ok) return;
            auto r = cdp::run (args, job, expect, cancel);
            if (! r.ok) { ok = false; if (why != nullptr) *why = r.error; }
        };
        // pvoc anal mode 1; CDP's own -c (points) and -o (overlap) only when you set them
        juce::StringArray extra;
        if (pinned ("pvoc.points")) extra.add ("-c" + juce::String (1 << (1 + (int) std::lround (pin ("pvoc.points", 9.0f)))));
        if (pinned ("pvoc.overlap")) extra.add ("-o" + juce::String ((int) std::lround (pin ("pvoc.overlap", 3.0f))));
        auto anal = [&] (const juce::File& in, const juce::File& outF)
        {
            juce::StringArray args { "pvoc", "anal", "1", in.getFileName(), outF.getFileName() };
            args.addArray (extra);
            step (args, outF);
        };
        anal (a, aa);
        anal (b, ba);
        juce::File last = aa;
        if (amount > 0.001f)
        {
            // combine cross: our spectral amplitudes replaced by the other's, to degree AMOUNT
            step ({ "combine", "cross", aa.getFileName(), ba.getFileName(), xa.getFileName(), "-i" + juce::String (juce::jlimit (0.0f, 1.0f, amount), 3) }, xa);
            last = xa;
        }
        if (detail > 0.001f || morphPins)
        {
            // morph morph mode 2 (cosinusoidal spline): glides amplitudes and frequencies into the other sound; GLIDE 1 =
            // all the way by the end, 0.5 = half way
            const double end = detail > 0.001f ? dur / juce::jlimit (0.05, 1.0, (double) detail) : dur;
            const auto e = juce::String (end, 3);
            juce::StringArray args { "morph", "morph", pinned ("morph.mode") ? juce::String ((int) std::lround (pin ("morph.mode", 1.0f)) + 1) : juce::String ("2"),
                                     last.getFileName(), ba.getFileName(), ma.getFileName(),
                                     pinned ("morph.as") ? juce::String (pin ("morph.as", 0.0f), 3) : juce::String ("0"),
                                     pinned ("morph.ae") ? juce::String (pin ("morph.ae", 0.0f), 3) : e,
                                     pinned ("morph.fs") ? juce::String (pin ("morph.fs", 0.0f), 3) : juce::String ("0"),
                                     pinned ("morph.fe") ? juce::String (pin ("morph.fe", 0.0f), 3) : e,
                                     pinned ("morph.expa") ? juce::String (pin ("morph.expa", 1.0f), 3) : juce::String ("1"),
                                     pinned ("morph.expf") ? juce::String (pin ("morph.expf", 1.0f), 3) : juce::String ("1") };
            if (pinned ("morph.stagger")) args.add ("-s" + juce::String (pin ("morph.stagger", 0.0f), 3));
            step (args, ma);
            last = ma;
        }
        step ({ "pvoc", "synth", last.getFileName(), o.getFileName() }, o);
        if (ok)
        {
            auto y = cdp::readMonoWav (o);
            for (int i = 0; i < n && i < (int) y.size(); ++i) out.setSample (c, i, y[(size_t) i]);
        }
        for (auto f : { a, b, aa, ba, xa, ma, o }) f.deleteFile();
    }
    job.deleteRecursively();
    return ok ? out : in;
}

juce::AudioBuffer<float> morphSound (const juce::AudioBuffer<float>& in, const MorphSettings& m, juce::String* note, const std::atomic<bool>* cancel,
                                     std::atomic<float>* neuralProgress)
{
    if (morphIsOff (m) || in.getNumSamples() == 0) return in;
    juce::AudioBuffer<float> cur (in);
    if (m.timbre != Timbre::none && (m.amount > 0.001f || m.detail > 0.001f || hood::hasEngine (m.pins, "cdp")))
    {
        auto other = timbreSound (m.timbre, m);
        juce::String why;
        if (other.getNumSamples() > 0) cur = spectralMorph (cur, other, m.amount, m.detail, &why, cancel, &m.pins);
        if (why.isNotEmpty() && note != nullptr) *note = "MORPH OFF: " + why;
    }
    if (cancel != nullptr && cancel->load()) return cur;
    if (m.neural > 0.001f && m.ddspModel.isNotEmpty())
    {
        juce::AudioBuffer<float> nn;
        juce::String why;
        if (neuralProgress != nullptr) neuralProgress->store (0.0f);
        const bool ran = neural::render (cur, m, nn, why, cancel, neuralProgress);
        if (neuralProgress != nullptr) neuralProgress->store (-1.0f);
        if (ran)
        {
            // dry / DDSP, as a plain cross-fade (no level matching of ours)
            nn.setSize (2, cur.getNumSamples(), true, true);
            const float w = juce::jlimit (0.0f, 1.0f, m.neural);
            for (int ch = 0; ch < 2; ++ch)
            {
                auto* c = cur.getWritePointer (ch); auto* q = nn.getReadPointer (ch);
                for (int i = 0; i < cur.getNumSamples(); ++i) c[i] = c[i] * (1.0f - w) + q[i] * w;
            }
            if (note != nullptr) *note = "NEURAL ON";
        }
        else if (note != nullptr) *note = "NEURAL OFF: " + why;
    }
    return cur;   // as CDP / the model made it (build 1 put a tanh ceiling here: WHY.md)
}
}
