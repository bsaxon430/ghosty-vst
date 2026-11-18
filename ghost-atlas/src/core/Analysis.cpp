// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Analysis.h"
#include "Dsp.h"
#include "Timbre.h"

namespace atlas
{
using namespace measure;

juce::AudioBuffer<float> renderInstrument (const juce::File& sfz, const SfzShape& shape, const std::vector<Note>& notes,
                                           double seconds, double sampleRate, SfzInfo* info)
{
    const int block = 512;
    const int total = (int) (seconds * sampleRate);
    juce::AudioBuffer<float> out (2, juce::jmax (1, total));
    out.clear();
    SfzLayer layer;
    layer.prepare (sampleRate, block);
    layer.setVoices (64);
    layer.setOffline (true);
    const bool ok = layer.load (sfz.getFullPathName().toStdString(), shape);
    if (info != nullptr) *info = layer.info();
    if (! ok) return out;
    {
        std::vector<std::pair<int, float>> toPlay;
        for (const auto& n : notes) toPlay.emplace_back (n.note, n.velocity);
        layer.primeFor (toPlay);
    }

    struct Ev { int sample; bool on; int note; float vel; };
    std::vector<Ev> evs;
    for (const auto& n : notes)
    {
        evs.push_back ({ (int) (n.start * sampleRate), true, n.note, n.velocity });
        evs.push_back ({ (int) ((n.start + n.length) * sampleRate), false, n.note, 0.0f });
    }
    std::stable_sort (evs.begin(), evs.end(), [] (const Ev& a, const Ev& b) { return a.sample < b.sample; });
    size_t next = 0;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = juce::jmin (block, total - pos);
        while (next < evs.size() && evs[next].sample < pos + n)
        {
            const int delay = juce::jlimit (0, n - 1, evs[next].sample - pos);
            if (evs[next].on) layer.noteOn (delay, evs[next].note, evs[next].vel); else layer.noteOff (delay, evs[next].note, 0.0f);
            ++next;
        }
        layer.render (out.getWritePointer (0, pos), out.getWritePointer (1, pos), n);
    }
    return out;
}

AnalysisResult analyseInstrument (const juce::File& sfz, const juce::String& id, const std::vector<int>& keys)
{
    AnalysisResult r;
    const double fs = 44100.0;
    if (keys.empty()) { r.error = "no key of this instrument plays anything"; return r; }
    auto nearest = [&keys] (int want) { int best = keys.front(); for (int k : keys) if (std::abs (k - want) < std::abs (best - want)) best = k; return best; };
    const int mid = nearest (60);
    const int lowNote = nearest (mid - 12), highNote = nearest (mid + 12);

    // one render holds every probe, each in its own stretch of time
    //   0.0  held note (3 s down, 3 s to ring out)     6.0  soft note      8.0  hard note
    //  10.0  low note   13.0 high note                 16.0 four-note chord (3 s down, 2 s out)
    std::vector<Note> notes;
    notes.push_back ({ mid, 0.8f, 0.0, 3.0 });
    notes.push_back ({ mid, 0.3f, 6.0, 1.2 });
    notes.push_back ({ mid, 1.0f, 8.0, 1.2 });
    notes.push_back ({ lowNote, 0.8f, 10.0, 2.0 });
    notes.push_back ({ highNote, 0.8f, 13.0, 2.0 });
    std::vector<int> chord;
    for (int want : { mid - 12, mid - 5, mid, mid + 4 }) { const int k = nearest (want); if (std::find (chord.begin(), chord.end(), k) == chord.end()) chord.push_back (k); }
    for (int c : chord) notes.push_back ({ c, 0.8f, 16.0, 3.0 });
    const double seconds = 21.0;

    SfzInfo info;
    const auto audio = renderInstrument (sfz, SfzShape::asMade(), notes, seconds, fs, &info);
    r.regions = info.regions;
    for (const auto& u : info.unknownOpcodes) r.unsupported.addIfNotAlreadyThere (juce::String (u));
    if (! info.ok) { r.error = info.error; return r; }

    auto at = [fs] (double t) { return (int) (t * fs); };
    Features f;
    f.probeNote = mid;

    // ---- the held note: its outline
    const auto held = monoOf (audio, 0, at (6.0));
    const auto env = envelopeOf (held, fs, 5.0);            // 5 ms steps
    float top = 0; for (size_t i = 0; i < env.size() && i < 600; ++i) top = juce::jmax (top, env[i]);   // while the key is down
    if (top < 1.0e-5f) { r.error = "the instrument made no sound for note " + noteName (mid); return r; }
    size_t i90 = 0; while (i90 < env.size() && env[i90] < 0.9f * top) ++i90;
    f.attackMs = (float) i90 * 5.0f;
    auto meanEnv = [&env] (double t0, double t1) { double s = 0; int n = 0; for (size_t i = (size_t) (t0 * 200); i < (size_t) (t1 * 200) && i < env.size(); ++i) { s += env[i]; ++n; } return n > 0 ? (float) (s / n) : 0.0f; };
    f.sustain = clamp01 (meanEnv (2.3, 2.8) / top);
    const float atOff = juce::jmax (1.0e-6f, meanEnv (2.9, 3.0));
    size_t iRel = 600; while (iRel < env.size() && env[iRel] > 0.01f * atOff && env[iRel] > 1.0e-5f) ++iRel;
    f.releaseMs = (float) (iRel - 600) * 5.0f;
    const float body = juce::jmax (1.0e-6f, meanEnv (0.3, 1.0));
    float first = 0; for (size_t i = 0; i < 8 && i < env.size(); ++i) first = juce::jmax (first, env[i]);
    f.transient = clamp01 (0.5f * std::log2 (juce::jmax (1.0f, first / body)));

    // ---- pitch: how clearly it has one (YIN), and how far its low partials sit from concert pitch (the held note,
    //      then a low and a high one)
    {
        float clarity = 0;
        const auto m = monoOf (audio, at (0.6), at (0.4));
        pitchOf (m.data(), (int) m.size(), fs, clarity);
        f.harmonicity = clarity;
    }
    std::vector<float> cents;
    auto tuneAt = [&] (double t, int note) {
        const auto m = monoOf (audio, at (t), at (1.6));
        float strength = 0;
        const float c = tuningOf (m.data(), (int) m.size(), fs, midiToHz ((float) note), strength);
        if (strength > 0.15f) cents.push_back (c);
    };
    tuneAt (0.25, mid); tuneAt (10.25, lowNote); tuneAt (13.25, highNote);
    if (! cents.empty())
    {
        std::sort (cents.begin(), cents.end());
        const double m = cents[cents.size() / 2];
        double v = 0; for (float c : cents) v += (c - m) * (c - m);
        f.tuneCents = (float) m; f.pitchSpread = (float) std::sqrt (v / (double) cents.size());
    }

    // ---- how it answers to velocity
    const float soft = rmsOf (audio, at (6.05), at (1.0)), hard = rmsOf (audio, at (8.05), at (1.0));
    f.velocityDb = juce::jlimit (0.0f, 60.0f, gainToDb (juce::jmax (1.0e-6f, hard)) - gainToDb (juce::jmax (1.0e-6f, soft)));

    // ---- tone: the chord
    const int c0 = at (16.25), cn = at (2.7);
    const auto chordMono = monoOf (audio, c0, cn);
    const auto sp = spectrumOf (chordMono.data(), (int) chordMono.size(), fs, 12);
    // loudness: the chord's loudest 400 ms, so a pluck and a pad can be compared (a pluck is gone before an average is taken)
    f.loudnessDb = -120.0f;
    for (int s0 = at (16.0); s0 + at (0.4) <= at (19.0); s0 += at (0.1)) f.loudnessDb = juce::jmax (f.loudnessDb, loudnessDb (audio, fs, s0, at (0.4)));
    { float pk = 0; for (int c = 0; c < 2; ++c) pk = juce::jmax (pk, audio.getMagnitude (c, at (16.0), at (5.0))); f.peakDb = gainToDb (juce::jmax (1.0e-6f, pk)); }
    f.centroidHz = (float) sp.centroid;
    f.low = (float) shareBetween (sp, 0, 200); f.mid = (float) shareBetween (sp, 200, 2000);
    f.high = (float) shareBetween (sp, 2000, 8000); f.air = (float) shareBetween (sp, 8000, 30000);
    // brightness: how much of the sound lies above 2 kHz against what lies below, on a scale where -35 dB is 0 and equal is 1
    {
        const double above = shareBetween (sp, 2000, 30000), below = shareBetween (sp, 0, 2000);
        f.brightness = clamp01 ((float) ((10.0 * std::log10 ((above + 1.0e-9) / (below + 1.0e-9)) + 35.0) / 35.0));
    }
    f.flatness = clamp01 ((float) sp.flatness * 4.0f);
    f.ring = ringOf (sp);
    for (int b = 0; b < kBands; ++b)
        f.bands[(size_t) b] = (float) juce::jlimit (-90.0, 0.0, 10.0 * std::log10 (juce::jmax (1.0e-9, sp.total > 0 ? sp.band[(size_t) b] / sp.total : 0.0)));
    f.width = widthOf (audio, c0, cn);
    f.monoLossDb = monoLossDb (audio, c0, cn);

    // ---- movement while held, and loop seams
    const auto sustainSp = spectrumOf (held.data() + at (0.4), at (2.4), fs, 11);
    f.movement = clamp01 ((float) sustainSp.flux * 6.0f);
    {
        const int s0 = at (1.0), s1 = at (2.9);
        std::vector<float> d; d.reserve ((size_t) (s1 - s0));
        for (int i = s0 + 1; i < s1; ++i) d.push_back (std::abs (held[(size_t) i] - held[(size_t) i - 1]));
        std::vector<float> local;
        for (size_t i = 0; i + 512 <= d.size(); i += 512) { double s = 0; for (size_t k = 0; k < 512; ++k) s += (double) d[i + k] * d[i + k]; local.push_back ((float) std::sqrt (s / 512.0)); }
        if (local.size() > 4)
        {
            std::vector<float> sorted = local; std::sort (sorted.begin(), sorted.end());
            const float med = juce::jmax (1.0e-7f, sorted[sorted.size() / 2]);
            float worst = 0; for (float v : d) worst = juce::jmax (worst, v);
            f.loopClick = clamp01 ((worst / med - 9.0f) / 20.0f) * clamp01 (f.sustain * 2.0f);
        }
    }

    // ---- timbre, with Samplebrain's own analysis
    TimbreSummary ts;
    if (Timbre::build (std::vector<float> (held.begin(), held.begin() + juce::jmin ((int) held.size(), at (3.0))), fs, Timbre::blockFile (id), ts))
    { f.mfcc = ts.mfcc; f.mfccDev = ts.mfccDev; f.fft = ts.fft; }

    // ---- the picture for the browser: 96 points of the held note
    r.preview.resize (96);
    const size_t span = std::max<size_t> (1, env.size() / 96);
    float pmax = 1.0e-6f;
    for (size_t p = 0; p < 96; ++p) { float m = 0; for (size_t k = p * span; k < (p + 1) * span && k < env.size(); ++k) m = juce::jmax (m, env[k]); r.preview[p] = m; pmax = juce::jmax (pmax, m); }
    for (auto& v : r.preview) v = std::sqrt (v / pmax);

    f.valid = true;
    r.features = f;
    r.ok = true;
    return r;
}
} // namespace atlas
