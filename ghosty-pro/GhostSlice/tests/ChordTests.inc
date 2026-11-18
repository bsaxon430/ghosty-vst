// GhostSliceTests: the chord track (phase 17, Chords.h). Groups: chords (detector, cache, key, MATCH KEY USE CHORDS), chordsui
//   synthetic progressions generated here (harmonic tones with decaying partials, random voicings / inversions, 7ths, sus, dim,
//   noise, a melody on top, silent gaps): chord-root accuracy >= 90 % with the note count unknown, boundary median error < 80 ms,
//   silence = N.C.; the analysis cache (versioned); the key with chords; MATCH KEY USE CHORDS; the chord lane in all four looks.
#include "../Chords.h"

namespace chordtest
{
namespace an = gslice::analysis;
struct CTruth { int64 s0, s1; int root; an::ChordQuality q; };
struct Gen
{
    bool sevenths = false, inversions = false, noise = false, melody = false, sus = false, pad = false;
    int chords = 8; uint32_t seed = 1;
};

static double hz (double midi) { return 440.0 * std::exp2 ((midi - 69.0) / 12.0); }

// a harmonic tone (random roll-off and phases, slight detune, piano-like decay or a pad's slow attack) added into x
static void tone (std::vector<float>& x, double sr, int64 a, int64 b, double midi, double amp, std::mt19937& rng, bool pad)
{
    std::uniform_real_distribution<double> U (0, 1);
    const double f0 = hz (midi) * std::exp2 ((U (rng) - 0.5) * 12.0 / 1200.0), B = 0.0003 * U (rng), roll = 0.8 + 0.6 * U (rng);
    const int64 len = b - a, rel = (int64) (0.03 * sr);
    for (int h = 1; h <= 14; ++h)
    {
        const double fh = h * f0 * std::sqrt (1.0 + B * h * h); if (fh > 0.45 * sr) break;
        const double ah = amp * std::pow (h, -roll), ph = U (rng) * 6.283185307, dec = pad ? 0.3 : 1.2 + 0.35 * h;
        const double w = 6.283185307 * fh / sr;
        for (int64 i = 0; i < len + rel && a + i < (int64) x.size(); ++i)
        {
            double e = std::exp (-(double) i / sr * dec);
            if (pad) e *= std::min (1.0, (double) i / (0.04 * sr)); else e *= std::min (1.0, (double) i / (0.002 * sr));
            if (i >= len) e *= 1.0 - (double) (i - len) / (double) rel;   // a 30 ms release (the next chord starts on time)
            x[(size_t) (a + i)] += (float) (ah * e * std::sin (w * (double) i + ph));
        }
    }
}

// a diatonic progression in a random key; chords of 0.6 - 1.4 s, one silent gap of 0.5 s in the middle
static std::vector<float> progression (double sr, const Gen& g, std::vector<CTruth>& truth, int& keyTonic, bool& keyMinor)
{
    std::mt19937 rng (g.seed); std::uniform_real_distribution<double> U (0, 1);
    keyTonic = (int) (U (rng) * 12) % 12; keyMinor = U (rng) < 0.4;
    static const int majDeg[7] { 0, 2, 4, 5, 7, 9, 11 }, minDeg[7] { 0, 2, 3, 5, 7, 8, 10 };
    static const an::ChordQuality majQ[7] { an::ChordQuality::Maj, an::ChordQuality::Min, an::ChordQuality::Min, an::ChordQuality::Maj, an::ChordQuality::Maj, an::ChordQuality::Min, an::ChordQuality::Dim };
    static const an::ChordQuality minQ[7] { an::ChordQuality::Min, an::ChordQuality::Dim, an::ChordQuality::Maj, an::ChordQuality::Min, an::ChordQuality::Maj, an::ChordQuality::Maj, an::ChordQuality::Maj };
    std::vector<std::pair<double, double>> spans; double t = 0.25;
    std::vector<float> x;
    int prevRoot = -1;
    for (int i = 0; i < g.chords; ++i)
    {
        if (i == g.chords / 2) t += 0.5;   // the silent gap
        const double d = 0.6 + 0.8 * U (rng);
        int deg; int root; an::ChordQuality q;
        do { deg = i == 0 ? 0 : (int) (U (rng) * 7) % 7; root = (keyTonic + (keyMinor ? minDeg : majDeg)[deg]) % 12; } while (root == prevRoot);
        prevRoot = root;
        q = (keyMinor ? minQ : majQ)[deg];
        if (g.sevenths && deg == 4 && U (rng) < 0.8) q = an::ChordQuality::Dom7;                   // V7
        if (g.sevenths && q == an::ChordQuality::Maj && U (rng) < 0.25) q = an::ChordQuality::Dom7;
        if (g.sus && (q == an::ChordQuality::Maj || q == an::ChordQuality::Min) && U (rng) < 0.3) q = an::ChordQuality::Sus;
        truth.push_back ({ (int64) (t * sr), (int64) ((t + d) * sr), root, q });
        spans.push_back ({ t, t + d });
        t += d;
    }
    x.assign ((size_t) ((t + 0.4) * sr), 0.0f);
    for (auto& c : truth)
    {
        auto tones = an::chordTones (c.q);
        // voicing: around C4, an inversion = a chord tone other than the root in the bass
        std::vector<double> notes;
        const int base = 55 + (int) (U (rng) * 7);
        for (int tn : tones) { double m = c.root + tn; while (m < base) m += 12; while (m >= base + 12) m -= 12; notes.push_back (m); }
        int bassTone = 0;
        if (g.inversions && U (rng) < 0.6) bassTone = 1 + (int) (U (rng) * (double) (tones.size() - 1)) % (int) (tones.size() - 1);
        double bass = c.root + tones[(size_t) bassTone]; while (bass >= 52) bass -= 12; while (bass < 40) bass += 12;
        if (g.inversions || U (rng) < 0.7) notes.push_back (bass);
        for (double m : notes) tone (x, sr, c.s0, c.s1, m, 0.07 + 0.05 * U (rng), rng, g.pad);
        if (g.melody)
        {   // a melody on top: 3-5 notes per chord, chord tones and scale steps, a little louder than one chord note
            const int k = 3 + (int) (U (rng) * 3);
            for (int j = 0; j < k; ++j)
            {
                const int64 a = c.s0 + (c.s1 - c.s0) * j / k, b = c.s0 + (c.s1 - c.s0) * (j + 1) / k;
                int pc = U (rng) < 0.6 ? (c.root + tones[(size_t) ((int) (U (rng) * tones.size()) % (int) tones.size())]) % 12
                                       : (keyTonic + (keyMinor ? minDeg : majDeg)[(int) (U (rng) * 7) % 7]) % 12;
                double m = 72 + pc; while (m > 81) m -= 12;
                tone (x, sr, a, b, m, 0.10, rng, false);
            }
        }
    }
    if (g.noise)
    {   // white noise 26 dB under the music
        double e = 0; for (float v : x) e += (double) v * v; const double rms = std::sqrt (e / (double) x.size());
        std::normal_distribution<double> Nn (0.0, rms * std::pow (10.0, -26.0 / 20.0));
        for (auto& v : x) v += (float) Nn (rng);
    }
    return x;
}

struct Score { double rootAcc = 0, qualAcc = 0, medBoundMs = 0, silenceNC = 0; int changes = 0; };
static Score score (const an::ChordTrack& est, const std::vector<CTruth>& truth, double sr)
{
    Score s;
    double tot = 0, okR = 0, okQ = 0;
    for (auto& c : truth)
        for (int64 p = c.s0; p < c.s1; p += (int64) (0.01 * sr))
        {
            const auto* e = est.at (p); tot += 1;
            if (e != nullptr && e->root == c.root) { okR += 1; if (e->quality == c.q) okQ += 1; }
        }
    s.rootAcc = tot > 0 ? okR / tot : 0; s.qualAcc = tot > 0 ? okQ / tot : 0;
    // boundaries: every true change (a chord start) against the nearest estimated change
    std::vector<int64> eb; for (size_t i = 1; i < est.chords.size(); ++i) eb.push_back (est.chords[i].s0);
    std::vector<double> err;
    for (auto& c : truth)
    {
        double best = 1e9; for (auto b : eb) best = std::min (best, std::abs ((double) (b - c.s0)) / sr * 1000.0);
        err.push_back (best);
    }
    std::sort (err.begin(), err.end()); s.changes = (int) err.size();
    s.medBoundMs = err.empty() ? 0 : err[err.size() / 2];
    // silence: the gaps between truth chords (and the lead-in), 120 ms in from their edges
    double st = 0, sok = 0;
    for (size_t i = 0; i + 1 < truth.size(); ++i)
        if (truth[i + 1].s0 - truth[i].s1 > (int64) (0.3 * sr))
            for (int64 p = truth[i].s1 + (int64) (0.12 * sr); p < truth[i + 1].s0 - (int64) (0.12 * sr); p += (int64) (0.01 * sr))
            { st += 1; const auto* e = est.at (p); sok += e != nullptr && ! e->isChord() ? 1 : 0; }
    for (int64 p = 0; p < truth.front().s0 - (int64) (0.12 * sr); p += (int64) (0.01 * sr)) { st += 1; const auto* e = est.at (p); sok += e != nullptr && ! e->isChord() ? 1 : 0; }
    s.silenceNC = st > 0 ? sok / st : 1.0;
    return s;
}
} // namespace chordtest

static void testChords()
{
    section ("[42] CHORDS: read-only chord track (multi-f0 -> pitch classes -> templates + Viterbi) on synthetic progressions");
    using namespace chordtest;
    const double sr = 48000;
    struct Row { const char* name; Gen g; };
    std::vector<Row> rows;
    { Gen g; rows.push_back ({ "triads", g }); }
    { Gen g; g.inversions = true; rows.push_back ({ "triads, inversions", g }); }
    { Gen g; g.sevenths = true; g.inversions = true; rows.push_back ({ "7ths + inversions", g }); }
    { Gen g; g.sus = true; g.pad = true; rows.push_back ({ "sus + dim, pad tones", g }); }
    { Gen g; g.noise = true; g.inversions = true; rows.push_back ({ "noise (-26 dB)", g }); }
    { Gen g; g.melody = true; g.inversions = true; rows.push_back ({ "melody on top", g }); }
    { Gen g; g.melody = true; g.noise = true; g.sevenths = true; g.inversions = true; rows.push_back ({ "melody + noise + 7ths", g }); }
    double worstRoot = 1, worstBound = 0, worstSil = 1; double ms = 0; int64 frames = 0;
    for (auto& row : rows)
    {
        if (std::getenv ("GS_CHDBG") && juce::String (row.name) != std::getenv ("GS_CHDBG")) continue;
        double ra = 0, qa = 0, sil = 1; std::vector<double> bounds; const int runs = std::getenv ("GS_CHDBG") ? 2 : 12;
        for (int k = 0; k < runs; ++k)
        {
            Gen g = row.g; g.seed = 1000u + (uint32_t) k * 7u + (uint32_t) (&row - rows.data()) * 131u;
            std::vector<CTruth> truth; int kt = 0; bool km = false;
            const auto x = progression (sr, g, truth, kt, km);
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            const auto est = an::detectChords (x.data(), (int64) x.size(), sr);
            ms += juce::Time::getMillisecondCounterHiRes() - t0; frames += (int64) x.size();
            const auto s = score (est, truth, sr);
            if (std::getenv ("GS_CHDBG") && juce::String (row.name) == std::getenv ("GS_CHDBG") && k < 2)
            {
                std::printf ("      truth:"); for (auto& c : truth) std::printf (" %.2f-%.2f %s", c.s0 / sr, c.s1 / sr, an::Chord { 0, 0, c.root, c.q, 1 }.name().toRawUTF8()); std::printf ("\n      est:  ");
                for (auto& c : est.chords) std::printf (" %.2f %s(%.2f)", c.s0 / sr, c.name().toRawUTF8(), c.confidence); std::printf ("\n");
            }
            ra += s.rootAcc / runs; qa += s.qualAcc / runs; sil = std::min (sil, s.silenceNC); bounds.push_back (s.medBoundMs);
        }
        std::sort (bounds.begin(), bounds.end());
        const double mb = bounds[bounds.size() / 2];
        std::printf ("    %-24s root %5.1f %%  root+quality %5.1f %%  boundary median %5.1f ms  silence N.C. %5.1f %%\n", row.name, ra * 100, qa * 100, mb, sil * 100);
        worstRoot = std::min (worstRoot, ra); worstBound = std::max (worstBound, mb); worstSil = std::min (worstSil, sil);
    }
    std::printf ("    speed: %.0f x real time (one core)\n", (double) frames / sr / (ms / 1000.0));
    check (worstRoot >= 0.90, "chord-root accuracy >= 90 % in every row, note count unknown (worst " + juce::String (worstRoot * 100, 1) + " %)");
    check (worstBound < 80.0, "chord-change boundaries: median error < 80 ms in every row (worst " + juce::String (worstBound, 1) + " ms)");
    check (worstSil >= 0.999, "silence = N.C. (worst " + juce::String (worstSil * 100, 1) + " % of the silent time)");
    {
        std::vector<float> z ((size_t) (2.0 * sr), 0.0f);
        const auto e = an::detectChords (z.data(), (int64) z.size(), sr);
        const auto m = sine (sr, 2.0, 220.0, 0.4, 1);
        const auto e2 = an::detectChords (m.getReadPointer (0), m.getNumSamples(), sr);
        check (e.chords.size() == 1 && ! e.chords[0].isChord() && e.chords[0].s1 == (int64) z.size() && e2.chordShare() < 0.05,
               "digital silence = one N.C. segment; a single sine = N.C. (chord share " + juce::String (e2.chordShare() * 100, 1) + " %)");
        // one harmonic tone (its partials are no chord) and a sung melody (monophonic): no chords, the key stays the notes key
        std::vector<float> t1 ((size_t) (2.0 * sr), 0.0f); std::mt19937 rng (5); tone (t1, sr, 4800, 86400, 57, 0.3, rng, false);
        const auto e3 = an::detectChords (t1.data(), (int64) t1.size(), sr);
        auto src = source::fromBuffer (stereoOf (voice (sr, fourNotes())), sr, "melody.wav");
        auto r = an::analyse (*src, an::Material::Melodic, {});
        check (e3.chordShare() < 0.1 && r != nullptr && r->chords->chordShare() < 0.1 && r->key == r->noteKey,
               "one harmonic tone: N.C. (" + juce::String (e3.chordShare() * 100, 1) + " % chord); a monophonic melody: " + juce::String (r != nullptr ? r->chords->chordShare() * 100 : -1.0, 1)
               + " % chord, the key = the notes key (" + (r != nullptr ? r->key.readout() : juce::String()) + ")");
    }
}

// the analysis side: cache (versioned), the background worker, the key with chords, MATCH KEY USE CHORDS
static void testChordAnalysis()
{
    section ("[43] CHORDS in the analysis: cache chords-v1, background + cancel, key with chords, MATCH KEY USE CHORDS");
    using namespace chordtest;
    const double sr = 48000;
    auto stereo = [] (const std::vector<float>& x) { juce::AudioBuffer<float> b (2, (int) x.size()); for (int c = 0; c < 2; ++c) b.copyFrom (c, 0, x.data(), (int) x.size()); return b; };
    // cache
    {
        Gen g; g.inversions = true; g.seed = 77; std::vector<CTruth> truth; int kt = 0; bool km = false;
        auto src = source::fromBuffer (stereo (progression (sr, g, truth, kt, km)), sr, "chords.wav");
        const auto dir = tmpDir().getChildFile ("chordcache"); dir.deleteRecursively();
        auto r1 = an::analyse (*src, an::Material::Melodic, dir);
        const auto f = an::cache::dirFor (dir, src->fingerprint).getChildFile ("chords-v" + juce::String (an::kChordVersion) + ".bin");
        auto r2 = an::analyse (*src, an::Material::Melodic, dir);
        check (r1 != nullptr && r1->chords != nullptr && f.existsAsFile() && r2 != nullptr && r2->fromCache && *r2->chords == *r1->chords && r2->key == r1->key,
               "the chord track is cached (chords-v" + juce::String (an::kChordVersion) + ".bin): a second analysis reads it (" + juce::String ((int) r1->chords->chords.size()) + " segments, the same key)");
        {   // a file of another version / length is ignored and rewritten
            juce::MemoryOutputStream m; m.writeString ("GSCH"); m.writeInt (an::kChordVersion + 1); m.writeInt64 (5); m.writeInt (0);
            f.replaceWithData (m.getData(), m.getDataSize());
            an::ChordTrack t; const bool readBad = an::cache::readChords (f.getParentDirectory(), (int64) src->audio.getNumSamples(), t);
            auto r3 = an::analyse (*src, an::Material::Melodic, dir);
            check (! readBad && r3 != nullptr && ! r3->fromCache && *r3->chords == *r1->chords, "a chords file of another version is ignored (recomputed, the same chords)");
        }
    }
    // background + cancel: the load returns at once, the analysis lands later; a material change mid-way cancels the running job
    {
        Gen g; g.melody = true; g.seed = 91; g.chords = 16; std::vector<CTruth> truth; int kt = 0; bool km = false;
        const auto x = progression (sr, g, truth, kt, km);
        SliceProcessor p; p.setPlayConfigDetails (0, 2, sr, 512); p.prepareToPlay (sr, 512); pump (5);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        p.loadSourceForTests (source::fromBuffer (stereo (x), sr, "bg.wav"));
        const double loadMs = juce::Time::getMillisecondCounterHiRes() - t0;
        const bool busy = p.analysing();
        { Project q = p.project(); q.material = "VOCAL"; p.apply (q, "material"); }   // restarts (cancels) the analysis
        { Project q = p.project(); q.material = "MELODIC"; p.apply (q, "material"); }
        p.waitAnalysis (60000);
        auto a = p.analysis();
        const auto ref = an::detectChords (an::mono (stereo (x)).data(), (int64) x.size(), sr);
        check (busy && loadMs < 1000.0 && a != nullptr && a->chords != nullptr && *a->chords == ref,
               "the chords are analysed in the background (load returned in " + juce::String (loadMs, 0) + " ms while analysing), a restart mid-way cancels the old job, the result = a clean run");
    }
    // key with chords (harmony only: the monophonic note tracker sees no clean melody)
    {
        int okC = 0, okN = 0, rel = 0; const int runs = 16;
        for (int k = 0; k < runs; ++k)
        {
            Gen g; g.inversions = true; g.sevenths = k % 2 == 1; g.seed = 300u + (uint32_t) k; std::vector<CTruth> truth; int kt = 0; bool km = false;
            auto src = source::fromBuffer (stereo (progression (sr, g, truth, kt, km)), sr, "key.wav");
            auto r = an::analyse (*src, an::Material::Melodic, {});
            if (r == nullptr) continue;
            okC += r->key.tonic == kt && r->key.minor == km ? 1 : 0;
            if (std::getenv ("GS_KEYDBG"))
            {
                std::printf ("      truth %s %s  chords-key %s (%.2f)  notes-key %s :", an::pitchClassName (kt).toRawUTF8(), km ? "MIN" : "MAJ", r->key.readout().toRawUTF8(), r->key.confidence, r->noteKey.readout().toRawUTF8());
                for (auto& c : r->chords->chords) std::printf (" %s", c.name().toRawUTF8()); std::printf ("\n");
            }
            okN += r->noteKey.tonic == kt && r->noteKey.minor == km ? 1 : 0;
            rel += r->key.tonic == (km ? (kt + 3) % 12 : (kt + 9) % 12) && r->key.minor != km ? 1 : 0;
        }
        std::printf ("    key on chord progressions: with chords %d / %d right (%d relative), notes only %d / %d\n", okC, runs, rel, okN, runs);
        check (okC >= runs * 3 / 4 && okC > okN, "key detection with chords: " + juce::String (okC) + " / " + juce::String (runs) + " progressions right (notes only: " + juce::String (okN) + ")");
    }
    // MATCH KEY USE CHORDS: the progression's key moved onto the project key by the chord roots
    {
        int ok = 0, total = 0;
        for (int k = 0; k < 8; ++k)
        {
            Gen g; g.melody = k % 2 == 0; g.seed = 500u + (uint32_t) k; std::vector<CTruth> truth; int kt = 0; bool km = false;
            const auto x = progression (sr, g, truth, kt, km);
            auto src = source::fromBuffer (stereo (x), sr, "mk.wav");
            auto r = an::analyse (*src, an::Material::Melodic, {});
            const int dst = (kt + 1 + 2 * k) % 12; const bool dstMinor = km;
            Project p; p.projectKey = an::pitchClassName (dst) + (dstMinor ? " MIN" : " MAJ"); p.keyMode = (int) keyops::Mode::UseChords;
            int shift = 99; juce::String why;
            const auto q = keyops::matchKey (p, r.get(), &why, &shift);
            int want = ((dst - kt) % 12 + 12) % 12; if (want > 5) want -= 12;
            ++total; ok += q.has_value() && q->pitchSemis == want ? 1 : 0;
        }
        Project p; p.projectKey = "C MAJ"; p.keyMode = (int) keyops::Mode::UseChords; juce::String why;
        an::Result empty; const bool refuses = ! keyops::matchKey (p, &empty, &why).has_value() && why.contains ("No chords");
        check (ok == total && refuses && juce::String (keyops::modeName (keyops::Mode::UseChords)) == "USE CHORDS",
               "MATCH KEY USE CHORDS: " + juce::String (ok) + " / " + juce::String (total) + " progressions moved onto the project key by their chord roots; no chords = a clear message");
        // a minor progression into its relative major needs no shift (its chords already are the key's chords)
        Gen g; g.seed = 611; std::vector<CTruth> truth; int kt = 0; bool km = false;
        const auto x = progression (sr, g, truth, kt, km);
        auto r = an::analyse (*source::fromBuffer (stereo (x), sr, "rel.wav"), an::Material::Melodic, {});
        const int relKey = km ? (kt + 3) % 12 : (kt + 9) % 12;
        Project p2; p2.projectKey = an::pitchClassName (relKey) + (km ? " MAJ" : " MIN"); p2.keyMode = (int) keyops::Mode::UseChords;
        int sh = 99; juce::String why2; const auto q2 = keyops::matchKey (p2, r.get(), &why2, &sh);
        check (! q2.has_value() && why2.contains ("already"), "USE CHORDS into the relative key: no shift (" + why2 + ")");
        // the model keeps the mode (save / load)
        Project pm; pm.keyMode = 4; Project back; juce::String err;
        const bool rt = json::fromVar (json::toVar (pm), back, err) && back.keyMode == 4;
        check (rt, "KEY MODE USE CHORDS survives save / load");
    }
}

// the chord lane: toggle, nothing else moves, chips = the chord track, labels as heard, contrast in all four looks, tooltip, ? help
static void testChordUi (const juce::File& shots)
{
    section ("[44] CHORDS lane UI: toggle, no control moves, chips, transposed labels, 4-look contrast, tooltip, ? help, screenshots");
    using namespace chordtest; using namespace ghosty::themes;
    const double sr = 48000;
    Gen g; g.inversions = true; g.sevenths = true; g.melody = true; g.seed = 4242; g.chords = 8;
    std::vector<CTruth> truth; int kt = 0; bool km = false;
    const auto x = progression (sr, g, truth, kt, km);
    juce::AudioBuffer<float> b (2, (int) x.size()); for (int c = 0; c < 2; ++c) b.copyFrom (c, 0, x.data(), (int) x.size());
    Rig rig (std::move (b));
    { Project q = rig->project(); q.material = "MELODIC"; rig->apply (q, "melodic"); }
    rig->waitAnalysis (60000);
    std::unique_ptr<SliceEditor> ed (static_cast<SliceEditor*> (rig->createEditor()));
    ed->setVisible (true); ed->setSize (1280, 780); ed->resized(); pump (20);
    auto& v = ed->view;
    auto bounds = [&] { std::vector<std::pair<juce::Component*, juce::Rectangle<int>>> r; for (auto* c : ed->getChildren()) if (c != &ed->chordsBtn) r.push_back ({ c, c->getBounds() }); return r; };
    bool allIn = true, noMove = true, noOverlap = true; juce::String info;
    for (auto w : { gslice::ui::Workspace::Pitch, gslice::ui::Workspace::Edit })
    {
        ed->setWorkspace (w); v.zoomToFit(); pump (10);
        const auto before = bounds();
        const bool shown = ed->chordsBtn.isVisible() && ed->chordsBtn.getWidth() >= 70;
        ed->chordsBtn.triggerClick(); pump (10);
        const auto after = bounds();
        noMove = noMove && before == after && v.showChords;
        for (auto& [c, r] : after)
            if (c->isVisible() && c != &v && c != &ed->help && r.intersects (ed->chordsBtn.getBounds()) && dynamic_cast<juce::Button*> (c) != nullptr) { noOverlap = false; info << c->getComponentID() << " "; }
        allIn = allIn && shown && ed->toolArea.contains (ed->chordsBtn.getBounds());
        ed->chordsBtn.triggerClick(); pump (5);
    }
    check (allIn && noMove && noOverlap, "CHORDS toggle in the PITCH row 4 and the EDIT row 3, inside the tools, overlapping no button; toggling it moves no control " + info);
    // the chips = the analysis' chord track through the region map, labels as heard (global PITCH)
    ed->setWorkspace (gslice::ui::Workspace::Pitch); v.zoomToFit(); v.showChords = true; ed->refresh(); pump (10);
    auto a = rig->analysis();
    if (std::getenv ("GS_KEYDBG")) std::printf ("      ui progression: truth %s %s, key %s, notes key %s\n", an::pitchClassName (kt).toRawUTF8(), km ? "MIN" : "MAJ", a->key.readout().toRawUTF8(), a->noteKey.readout().toRawUTF8());
    auto chips = v.chordChips();
    int named = 0, matched = 0;
    for (auto& ch : chips)
    {
        if (ch.nc) continue; ++named;
        const double hf = v.hfAt (ch.r.getCentreX());
        const auto* c = a->chords->at ((int64) std::llround (rig->hfToPf (hf)));
        matched += c != nullptr && c->name() == ch.label ? 1 : 0;
    }
    { Project q = rig->project(); q.pitchSemis = 2; rig->apply (q, "pitch +2"); pump (5); }
    auto chips2 = v.chordChips();
    bool transposed = chips2.size() == chips.size();
    for (size_t i = 0; transposed && i < chips.size(); ++i)
        if (! chips[i].nc)
        {
            const auto* c = a->chords->at ((int64) std::llround (rig->hfToPf (v.hfAt (chips[i].r.getCentreX()))));
            transposed = c != nullptr && chips2[i].label == c->name (2);
        }
    rig->undo(); pump (5);
    check (named >= 6 && matched == named && transposed && v.chordLane().getY() >= (float) v.waveArea().getY(),
           "the lane shows the chord track (" + juce::String (named) + " chords, all at their place) inside the view's top; labels follow the global PITCH (+2 st)");
    // tooltip + ? help + the button's tooltip
    {
        const auto& ch = *std::find_if (chips.begin(), chips.end(), [] (const auto& c) { return ! c.nc; });
        const auto pt = ch.r.getCentre().toInt();
        juce::Desktop::getInstance().getMainMouseSource().setScreenPosition (v.localPointToGlobal (pt).toFloat()); pump (5);
        const auto tip = v.getTooltip();
        bool help = false; for (auto& l : HelpOverlay::lines()) help = help || l.startsWith ("CHORDS|");
        check (tip.contains (ch.label) && tip.contains ("read-only") && help && ed->chordsBtn.getTooltip().contains ("read-only") && ed->keyModeBox.getTooltip().contains ("USE CHORDS"),
               "tooltips: the chip under the mouse (" + tip.upToFirstOccurrenceOf (" - ", false, false) + "), the CHORDS button, KEY MODE USE CHORDS; the ? list has CHORDS");
    }
    // contrast in the four looks: the label rectangles over what is under them (the chips drawn without their text)
    juce::String rows; bool ok = true;
    for (int look = 0; look < SliceEditor::kLooks; ++look)
    {
        ed->setLook (look); pump (10);
        const auto& P = v.pal();
        v.chordLabels = false; v.repaint();
        auto img = v.createComponentSnapshot (v.getLocalBounds(), true, 1.0f);
        v.chordLabels = true;
        double worst = 99; int n = 0;
        const juce::Font f = gslice::ui::LnF::uiFont (11.5f);
        for (auto& ch : v.chordChips())
        {
            if (ch.nc) continue;
            const auto r = ch.r.reduced (1.0f, 0).reduced (4.0f, 0);
            const float tw = std::min (r.getWidth(), ghosty::textWidth (f, ch.label));
            if (tw < 6) continue;
            const auto tr = juce::Rectangle<float> (tw, f.getAscent()).withCentre (r.getCentre()).toNearestInt();
            worst = std::min (worst, worstContrastIn (img, tr, P.chordInk)); ++n;
        }
        const double tokens = std::min (contrast (P.chordInk, P.chordBg), contrast (P.chordNc, P.chordBg));
        rows << SliceEditor::lookName (look) << " " << juce::String (worst, 2) << " (" << n << " labels, tokens " << juce::String (tokens, 2) << ")  ";
        ok = ok && n >= 4 && worst >= 4.5 && tokens >= 4.5;
        snap (*ed, shots.getChildFile ("chords_" + SliceEditor::lookName (look).toLowerCase() + ".png"));
    }
    check (ok, "chord labels readable in all four looks (worst pixel contrast >= 4.5:1): " + rows.trim());
    ed->setLook (0); ed->setWorkspace (gslice::ui::Workspace::Edit); v.zoomToFit(); pump (10);
    snap (*ed, shots.getChildFile ("chords_edit_black.png"));
    ed = nullptr;
}
