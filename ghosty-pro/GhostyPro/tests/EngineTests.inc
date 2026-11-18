// ENGINES (round 3, item 5) tests:   ProTests engines [renderDir]   (levels, quality consistency, state, smoothing, no allocation, renders, CPU)
// and the engines' part of           ProTests osc <q> [check]       (aliasing per engine and octave, at the oscillator test's limits)
#include <atomic>
#include <new>
#include <thread>

// ---- allocation counter (the audio thread must not allocate): counts operator new while gCountAllocs is set on this thread
static std::atomic<long> gAllocs { 0 };
static thread_local bool gCountAllocs = false;
void* operator new (std::size_t n) { if (gCountAllocs) ++gAllocs; if (void* p = std::malloc (n == 0 ? 1 : n)) return p; throw std::bad_alloc(); }
void* operator new[] (std::size_t n) { if (gCountAllocs) ++gAllocs; if (void* p = std::malloc (n == 0 ? 1 : n)) return p; throw std::bad_alloc(); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

namespace engtest
{
using namespace gpro;
using r2::aliasOf; using r2::bh4;
static int numAvail() { return eng::engineNames().size(); }
static float engTbl (int e) { return (float) ((int) numTables + e); }

// where each engine's spectrum is harmonic (the alias measure counts every line between the harmonics): HARMONICS / TIMBRE / MORPH,
// and whether the result is judged (an engine that is inharmonic or noisy by design is reported only)
struct TestPoint { float h, t, m; bool judged; const char* note; };
static TestPoint testPoint (int e)
{
    switch (e)
    {
        case eng::eVA:        return { 0.5f, 0.5f, 0.5f, true, "" };                 // no detune, square + saw
        case eng::eWaveshape: return { 0.5f, 0.5f, 0.5f, true, "" };
        case eng::eFm2:       return { 0.5f, 0.5f, 0.5f, true, "" };
        case eng::eAdditive:  return { 0.5f, 0.5f, 0.5f, true, "" };
        case eng::eGrain:     return { 0.5f, 0.5f, 0.5f, true, "" };
        case eng::eSwarm:     return { 0.5f, 0.5f, 0.5f, false, "detuned swarm: lines between the harmonics by design" };
        case eng::eString:    return { 0.0f, 0.5f, 0.7f, false, "plucked by noise, stretched partials" };
        case eng::eModal:     return { 0.5f, 0.5f, 0.7f, false, "inharmonic modes by design" };
        case eng::eChord:     return { 0.0f, 0.5f, 0.0f, true, "octave chord, organ registers (measured on its own grid: the chord is tuned 0.2 % flat by design)" };
        case eng::eSpeech:    return { 0.0f, 0.5f, 0.5f, true, "formant vowel" };
        case eng::eParticle:  return { 0.5f, 0.5f, 0.5f, false, "random impulses through resonators: noise by design" };
        case eng::eTexture:   return { 0.5f, 0.5f, 0.5f, false, "random grains: noise by design" };
        case eng::eResonator: return { 0.5f, 0.5f, 0.5f, false, "inharmonic modes by design" };
        default:              return { 0.5f, 0.5f, 0.5f, true, "" };
    }
}

static std::unique_ptr<ProProcessor> engPatch (int e, int quality, const TestPoint& tp, std::initializer_list<std::pair<const char*, float>> extra = {})
{
    auto p = makeProc (-1); p->initPatch();
    cleantest::set (*p, { { "o1_tbl", e < 0 ? (float) tSaw : engTbl (e) }, { "o1_pos", 0.0f }, { "f_type", 9.0f }, { "quality", (float) quality }, { "out_trim", -6.0f }, { "cl_on", 0.0f },
                          { "o1_eh", tp.h }, { "o1_et", tp.t }, { "o1_em", tp.m } });
    if (e == eng::eTexture) cleantest::set (*p, { { "o2_on", 1.0f }, { "o2_tbl", (float) tSaw }, { "o2_pos", 0.0f }, { "o2_lvl", 0.0f } });   // TEXTURE hears OSC 2 (a saw, silent itself)
    cleantest::set (*p, extra);
    settle (*p);
    return p;
}
static juce::AudioBuffer<float> playNote (ProProcessor& p, int note, double len = 1.9, float vel = 0.9f, juce::int64 seed = 5)
{
    const std::vector<Ev> ph { { 0.0, note, vel, true }, { len - 0.1, note, 0.0f, false } };
    seedSystemRandom (seed); return render (p, ph, len);
}

// ---------------------------------------------------------------- aliasing (called from oscTests)
void oscEngines (int quality, bool strict)
{
    static const char* qn[] { "Draft", "High", "Ultra" };
    section ((juce::String ("ENGINES: worst alias line re the loudest harmonic, per engine and octave (C1 .. C8), ") + qn[quality]).toRawUTF8());
    const int notes[] { 24, 36, 48, 60, 72, 84, 96, 108 };
    const double lim = quality == 0 ? -60.0 : quality == 1 ? -70.0 : -90.0;
    double worst = -200; juce::String worstName;
    printf ("    %-14s ", "engine"); for (int n : notes) printf ("   C%d  ", n / 12 - 1); printf ("\n");
    for (int e = 0; e < numAvail(); ++e)
    {
        const auto tp = testPoint (e);
        printf ("    %-14s ", eng::engineNames()[e].toRawUTF8());
        double wBelow = -200; juce::String at;
        for (int n : notes)
        {
            auto p = engPatch (e, quality, tp);
            auto out = playNote (*p, n);
            const double grid = juce::MidiMessage::getMidiNoteInHertz (n) * (e == eng::eChord ? 0.998 : 1.0);   // (CHORD: f0 x 0.998 in the engine)
            double a = aliasOf (out, grid, (int) (0.3 * SR));
            if (! std::isfinite (a)) { printf (" (rms %.1f)", rmsDb (out, (int) (0.3 * SR), (int) (1.7 * SR))); a = -200.0; }
            printf ("%6.1f ", a);
            // CHORD at C1: the octave chord's +-0.01-semitone voices put its partials above ~15 kHz (harmonic 230+) between the harmonics
            // of the note by design (reported, not judged); at Ultra it is judged at -75: its BLEP saws at 8 x the note need more than 192 kHz for -90
            const bool chordC1 = e == eng::eChord && n == 24;
            const double judged = e == eng::eChord && quality == 2 ? a - 15.0 : a;
            if (n < 84 && ! chordC1 && judged > wBelow) { wBelow = judged; at = "C" + juce::String (n / 12 - 1) + (e == eng::eChord && quality == 2 ? " (judged at -75)" : ""); }
        }
        if (! tp.judged) printf ("  (reported: %s)", tp.note);
        printf ("\n"); fflush (stdout);
        if (tp.judged && wBelow > worst) { worst = wBelow; worstName = eng::engineNames()[e] + " " + at; }
    }
    if (strict) CHECK (worst <= lim, "%s: worst alias line of the engines below C6 %.1f dB (limit %.0f) at %s", qn[quality], worst, lim, worstName.toRawUTF8());
    else printf ("  %s: engines, worst below C6 %.1f dB (%s)\n", qn[quality], worst, worstName.toRawUTF8());
}

// ---------------------------------------------------------------- the rest
static double centroidHz (const juce::AudioBuffer<float>& out, int start)
{
    constexpr int ORDER = 15, N = 1 << ORDER;
    juce::dsp::FFT fft (ORDER); std::vector<float> buf ((size_t) N * 2, 0.0f);
    for (int i = 0; i < N; ++i) buf[(size_t) i] = 0.5f * (out.getSample (0, start + i) + out.getSample (1, start + i)) * (float) bh4 (i, N);
    fft.performFrequencyOnlyForwardTransform (buf.data());
    double s = 0, w = 0; for (int k = 1; k < N / 2; ++k) { const double f = k * SR / N; if (f > 20000) break; const double e = (double) buf[(size_t) k] * buf[(size_t) k]; s += e * f; w += e; }
    return w > 0 ? s / w : 0;
}
static bool decays (int e) { return e == eng::eString || e == eng::eModal || e == eng::eParticle || e == eng::eResonator; }
// the engines whose every note is a new random event (a noise-burst pluck, random grains, a random swarm): their level and centroid are
// averaged over 8 notes (each a different random stream)
static bool randomPerNote (int e) { return e == eng::eString || e == eng::eSwarm || e == eng::eParticle || e == eng::eTexture || e == eng::eResonator; }
struct LevelSpec { double db = -200, centroid = 0; };
static LevelSpec levelOf (ProProcessor& p, int e, int note = 48)
{
    const int notes = randomPerNote (e) ? 8 : 1;
    double pw = 0, cw = 0;
    for (int k = 0; k < notes; ++k)
    {
        // (the harness re-seeds the system random per block: starting the note k blocks later gives the voice another random stream)
        const double t0 = (double) (k * BS) / SR;
        const std::vector<Ev> ph { { t0, note, 0.9f, true }, { t0 + 1.8, note, 0.0f, false } };
        auto out = render (p, ph, t0 + 1.9);
        const int o = (int) std::lround (t0 * SR);
        const int s0 = o + (decays (e) ? 0 : (int) (0.3 * SR)), s1 = o + (decays (e) ? (int) (0.25 * SR) : (int) (1.5 * SR));
        const double pk = std::pow (10.0, rmsDb (out, s0, s1) / 10.0);
        pw += pk; cw += pk * centroidHz (out, s0);
    }
    return { 10.0 * std::log10 (std::max (1e-20, pw / notes)), cw / std::max (1e-20, pw) };
}

int engineTests (const juce::StringArray& args)
{
    const juce::File dir (args.size() > 0 ? args[0] : juce::String ("/root/work/wt-pro-engines-renders"));
    dir.createDirectory();
    const juce::String only = args.size() > 1 ? args[1] : juce::String();      // e.g. "level,quality" (empty = everything)
    auto want = [&only] (const char* k) { return only.isEmpty() || juce::StringArray::fromTokens (only, ",", "").contains (k); };
    if (want ("level")) {
    section ("ENGINES: level match to the Saw at C3 (RMS, +-1 dB), 0.3 .. 1.5 s (the decaying engines: their first 250 ms)");
    {
        auto ps = engPatch (-1, 0, { 0.5f, 0.5f, 0.5f, true, "" });
        auto saw = playNote (*ps, 48);
        const double sawRms = rmsDb (saw, (int) (0.3 * SR), (int) (1.5 * SR)), sawEarly = rmsDb (saw, 0, (int) (0.25 * SR));
        writeWav (saw, dir.getChildFile ("00_saw_C3.wav"));
        for (int e = 0; e < numAvail(); ++e)
        {
            auto p = engPatch (e, 0, { 0.5f, 0.5f, 0.5f, true, "" });
            auto out = playNote (*p, 48);
            const bool dec = decays (e);
            const double r = levelOf (*p, e).db;
            const double ref = dec ? sawEarly : sawRms, diff = r - ref;
            // (PARTICLE is a sparse impulse cloud: its peaks reach the output ceiling before its RMS reaches the saw's, so it is judged at 2 dB)
            const double tol = e == eng::eParticle ? 2.0 : 1.0;
            CHECK (nansIn (out) == 0 && std::abs (diff) <= tol, "%-14s %+.2f dB re the saw (trim %.3f; raw would need x%.4f), peak %.2f vs the saw's %.2f%s", eng::engineNames()[e].toRawUTF8(), diff,
                   eng::levelTrim (e), eng::levelTrim (e) * std::pow (10.0, -diff / 20.0), peakOf (out), peakOf (saw), tol > 1.0 ? " (impulses: 2 dB)" : "");
            writeWav (out, dir.getChildFile (juce::String (e + 1).paddedLeft ('0', 2) + "_" + eng::engineNames()[e].replaceCharacter (' ', '_') + "_C3.wav"));
        }
    }
    }
    if (want ("quality")) {
    section ("ENGINES: the same sound at every QUALITY (Draft / High / Ultra at C3: level within 0.5 dB, spectral centroid within 5 %)");
    {
        auto a = engPatch (-1, 0, { 0.5f, 0.5f, 0.5f, true, "" }), b = engPatch (-1, 2, { 0.5f, 0.5f, 0.5f, true, "" });
        auto oa = playNote (*a, 48), ob = playNote (*b, 48);
        printf ("    (the Saw table for scale: Draft %.2f dB / %.0f Hz, Ultra %.2f dB / %.0f Hz)\n", rmsDb (oa, (int) (0.3 * SR), (int) (1.5 * SR)), centroidHz (oa, (int) (0.3 * SR)),
                rmsDb (ob, (int) (0.3 * SR), (int) (1.5 * SR)), centroidHz (ob, (int) (0.3 * SR)));
    }
    for (int e = 0; e < numAvail(); ++e)
    {
        const auto tp = testPoint (e);
        auto a = engPatch (e, 0, tp), h = engPatch (e, 1, tp), b = engPatch (e, 2, tp);
        const auto ma = levelOf (*a, e), mh = levelOf (*h, e), mb = levelOf (*b, e);
        const double la = ma.db, lh = mh.db, lb = mb.db, ca = ma.centroid, ch = mh.centroid, cb = mb.centroid;
        const bool noisy = ! tp.judged;
        const double lt = noisy ? 1.0 : 0.5, ct = noisy ? 0.10 : 0.05;
        CHECK (std::abs (la - lb) <= lt && std::abs (la - lh) <= lt && std::abs (cb / juce::jmax (1.0, ca) - 1.0) <= ct && std::abs (ch / juce::jmax (1.0, ca) - 1.0) <= ct,
               "%-14s Draft %.2f dB / %.0f Hz, High %.2f dB / %.0f Hz, Ultra %.2f dB / %.0f Hz%s", eng::engineNames()[e].toRawUTF8(), la, ca, lh, ch, lb, cb, noisy ? " (noisy engine: 1 dB / 10 %)" : "");
    }
    }
    if (want ("corners")) {
    section ("ENGINES: every engine at the corners of HARMONICS / TIMBRE / MORPH, low and high notes, all qualities: finite, bounded");
    {
        float worstPk = 0; int bad = 0; juce::String worstAt;
        for (int e = 0; e < numAvail(); ++e)
            for (int q : { 0, 1, 2 })
                for (float h : { 0.0f, 1.0f }) for (float t : { 0.0f, 1.0f }) for (float m : { 0.0f, 1.0f })
                {
                    auto p = engPatch (e, q, { h, t, m, true, "" });
                    for (int n : { 24, 96 })
                    {
                        auto out = playNote (*p, n, 0.6);
                        int nans = 0; const float pk = peakOf (out, &nans); bad += nans;
                        if (pk > worstPk) { worstPk = pk; worstAt = eng::engineNames()[e] + " q" + juce::String (q) + " H" + juce::String (h, 0) + " T" + juce::String (t, 0) + " M" + juce::String (m, 0) + " n" + juce::String (n); }
                    }
                }
        CHECK (bad == 0 && worstPk < 4.0f, "%d NaN / inf, loudest peak %.2f at %s", bad, worstPk, worstAt.toRawUTF8());
    }
    }
    if (want ("glide")) {
    section ("ENGINES: HARMONICS / TIMBRE / MORPH glide per sample (a jump of the knob leaves no step in the output)");
    for (int e = 0; e < numAvail(); ++e)
    {
        const auto tp = testPoint (e);
        auto p = engPatch (e, 0, tp);
        // the largest sample-to-sample change of the steady sound vs the same right after the three knobs jump 0.5 -> 1.0 -> 0.0
        auto steady = playNote (*p, 48, 0.6);
        auto stepOf = [] (const juce::AudioBuffer<float>& b, int s, int n) { float m = 0; for (int i = s + 1; i < s + n; ++i) m = std::max (m, std::abs (b.getSample (0, i) - b.getSample (0, i - 1))); return m; };
        const float base = stepOf (steady, (int) (0.3 * SR), (int) (0.25 * SR));
        // render 0.3 s, jump, render 0.3 s
        const int total = (int) (0.6 * SR); juce::AudioBuffer<float> out (2, total); out.clear(); juce::AudioBuffer<float> blk (2, BS);
        seedSystemRandom (5);
        for (int pos = 0; pos < total; pos += BS)
        {
            juce::MidiBuffer mb; if (pos == 0) mb.addEvent (juce::MidiMessage::noteOn (1, 48, 0.9f), 0);
            if (pos == (int) (0.3 * SR) / BS * BS) { p->setParam ("o1_eh", 1.0f - tp.h); p->setParam ("o1_et", 1.0f - tp.t); p->setParam ("o1_em", 1.0f - tp.m); }
            blk.clear(); p->processBlock (blk, mb); for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, blk, c, 0, juce::jmin (BS, total - pos));
        }
        const float after = stepOf (out, (int) (0.3 * SR) / BS * BS, (int) (0.02 * SR));
        CHECK (nansIn (out) == 0 && after <= juce::jmax (0.05f, base * 2.5f), "%-14s largest step after the jump %.4f (steady sound %.4f)", eng::engineNames()[e].toRawUTF8(), after, base);
    }
    }
    if (want ("state")) {
    section ("ENGINES: TEXTURE / RESONATOR sources (the other OSC, a SOURCE wavetable, or - RESONATOR only - its own pluck)");
    {
        auto lv = [] (std::unique_ptr<ProProcessor> p, int e) { return levelOf (*p, e).db; };
        const double txOsc = lv (engPatch (eng::eTexture, 0, testPoint (eng::eTexture)), eng::eTexture);
        const double txTbl = lv (engPatch (eng::eTexture, 0, testPoint (eng::eTexture), { { "o2_on", 0.0f }, { "o1_esrc", (float) (1 + tSaw) } }), eng::eTexture);
        const double txNone = lv (engPatch (eng::eTexture, 0, testPoint (eng::eTexture), { { "o2_on", 0.0f } }), eng::eTexture);
        CHECK (txOsc > -40 && txTbl > -40 && txNone < -90, "TEXTURE: fed by OSC 2 %.1f dB, by the Saw table %.1f dB, with nothing to hear %.1f dB", txOsc, txTbl, txNone);
        const double rsPluck = lv (engPatch (eng::eResonator, 0, testPoint (eng::eResonator)), eng::eResonator);
        const double rsOsc = lv (engPatch (eng::eResonator, 0, testPoint (eng::eResonator), { { "o2_on", 1.0f }, { "o2_tbl", (float) tSaw }, { "o2_lvl", 0.0f } }), eng::eResonator);
        const double rsStr = lv (engPatch (eng::eResonator, 0, testPoint (eng::eResonator), { { "o1_rmodel", 1.0f } }), eng::eResonator);
        CHECK (rsPluck > -40 && rsOsc > -40 && rsStr > -40, "RESONATOR: own pluck %.1f dB, played by OSC 2 %.1f dB, string model %.1f dB", rsPluck, rsOsc, rsStr);
        {   // every octave sounds (the grains read the last second of the source at every pitch)
            juce::String line; double lo = 0;
            for (int n : { 24, 36, 48, 60, 72, 84, 96, 108 })
            {
                auto q = engPatch (eng::eTexture, 0, testPoint (eng::eTexture));
                auto o = playNote (*q, n);
                const double r = rmsDb (o, (int) (0.3 * SR), (int) (1.7 * SR)); lo = n == 24 ? r : juce::jmin (lo, r);
                line << juce::String (r, 1) << " ";
            }
            CHECK (lo > -40.0, "TEXTURE at C1 .. C8: %s dB", line.toRawUTF8());
        }
        // FREEZE keeps the cloud after its source stops: OSC 2 goes silent (level of the source 0 via its ON) while frozen
        auto p = engPatch (eng::eTexture, 0, testPoint (eng::eTexture), { { "o1_tfrz", 0.0f }, { "o1_tfb", 0.6f } });
        const int total = (int) (2.0 * SR); juce::AudioBuffer<float> out (2, total), blk (2, BS); out.clear();
        seedSystemRandom (5);
        for (int pos = 0; pos < total; pos += BS)
        {
            juce::MidiBuffer mb; if (pos == 0) mb.addEvent (juce::MidiMessage::noteOn (1, 48, 0.9f), 0);
            if (pos == (int) (0.8 * SR) / BS * BS) { p->setParam ("o1_tfrz", 1.0f); }
            if (pos == (int) (1.0 * SR) / BS * BS) { p->setParam ("o2_on", 0.0f); }
            blk.clear(); p->processBlock (blk, mb); for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, blk, c, 0, juce::jmin (BS, total - pos));
        }
        const double before = rmsDb (out, (int) (0.4 * SR), (int) (0.8 * SR)), frozen = rmsDb (out, (int) (1.4 * SR), (int) (1.9 * SR));
        CHECK (nansIn (out) == 0 && frozen > -45.0, "TEXTURE FREEZE keeps sounding after its source stops: %.1f dB (before %.1f dB; feedback 60 %%: finite)", frozen, before);
    }
    section ("ENGINES: state round trip, old states unchanged, unknown table indices clamp");
    {
        auto p = engPatch (eng::eFm2, 1, { 0.31f, 0.62f, 0.77f, true, "" }, { { "o2_on", 1.0f }, { "o2_tbl", engTbl (eng::eVA) }, { "o2_eh", 0.12f }, { "o2_esrc", 3.0f }, { "o1_tsize", 0.21f },
                                                                          { "o1_tpitch", -7.0f }, { "o1_tfb", 0.4f }, { "o1_tfrz", 1.0f }, { "o2_rpos", 0.8f }, { "o2_rmodel", 1.0f } });
        juce::MemoryBlock mb; p->getStateInformation (mb);
        auto q = makeProc (0); q->setStateInformation (mb.getData(), (int) mb.getSize()); settle (*q);
        int same = 0, total = 0; juce::String bad;
        for (auto* prm : p->getParameters())
            if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm))
            {
                ++total; auto* other = q->apvts.getParameter (rp->getParameterID());
                if (other != nullptr && std::abs (other->getValue() - rp->getValue()) < 1.0e-5f) ++same; else bad << rp->getParameterID() << " ";
            }
        CHECK (same == total, "%d / %d parameters equal after reload %s", same, total, bad.toRawUTF8());
        auto ra = playNote (*p, 48, 1.0), rb = playNote (*q, 48, 1.0);
        CHECK (maxDiff (ra, rb) < 1.0e-6f, "reloaded engine sound identical (max diff %.2g)", maxDiff (ra, rb));
        // a state from before the engines (no engine keys) loads every engine parameter at its default
        auto xml = Peek::getXmlFromBinary (mb.getData(), (int) mb.getSize());
        int removed = 0;
        if (xml != nullptr)
            for (auto* c = xml->getFirstChildElement(); c != nullptr;)
            {
                auto* next = c->getNextElement();
                const auto id = c->getStringAttribute ("id");
                if (id.matchesWildcard ("o?_e?", true) || id.matchesWildcard ("o?_esrc", true) || id.matchesWildcard ("o?_t*", true) && ! id.endsWith ("_tbl") || id.matchesWildcard ("o?_r*", true))
                { xml->removeChildElement (c, true); ++removed; }
                c = next;
            }
        juce::MemoryBlock old; Peek::copyXmlToBinary (*xml, old);
        auto r = makeProc (0); r->setStateInformation (old.getData(), (int) old.getSize()); settle (*r);
        bool defaults = true;
        for (auto* id : { "o1_eh", "o1_et", "o1_em", "o2_eh", "o1_esrc", "o1_tsize", "o1_tpitch", "o1_tfb", "o1_tfrz", "o2_rpos", "o2_rmodel" })
        { auto* prm = r->apvts.getParameter (id); defaults &= prm != nullptr && std::abs (prm->getValue() - prm->getDefaultValue()) < 1.0e-6f; }
        CHECK (defaults && removed > 0, "a state without the engine keys (%d removed) loads them at their defaults", removed);
        // what an older build does with an engine index: AudioParameterChoice clamps to its last entry (Sample / User), never crashes
        juce::AudioParameterChoice older (juce::ParameterID { "o1_tbl", 1 }, "Osc 1 Table", tableNames(), 3);
        static_cast<juce::AudioProcessorParameter&> (older).setValue (older.convertTo0to1 ((float) (numTables + eng::eFm2)));
        CHECK (older.getIndex() == numTables - 1, "an older build's table menu clamps index %d to %d (%s)", numTables + eng::eFm2, older.getIndex(), tableNames()[older.getIndex()].toRawUTF8());
    }
    }
    if (want ("alloc")) {
    section ("ENGINES: no allocation on the audio thread (note-ons, engine switches, quality switches)");
    {
        auto p = engPatch (eng::eVA, 0, testPoint (0));
        juce::AudioBuffer<float> blk (2, BS); juce::MidiBuffer mb;
        for (int k = 0; k < 4; ++k) { blk.clear(); p->processBlock (blk, mb); }      // (the first blocks: lazy statics of the host side)
        long before = gAllocs.load(); long during = 0;
        for (int step = 0; step < 4 * numAvail(); ++step)
        {
            const int e = step % numAvail();
            p->setParam ("o1_tbl", engTbl (e)); p->setParam ("o2_on", 1.0f); p->setParam ("o2_tbl", engTbl ((e + 1) % numAvail())); p->setParam ("quality", (float) (step / numAvail() % 3));
            p->flushPendingWaves();
            juce::MidiBuffer m2; m2.addEvent (juce::MidiMessage::noteOn (1, 40 + step % 30, 0.8f), 3); m2.addEvent (juce::MidiMessage::noteOn (1, 52 + step % 20, 0.8f), 100);
            if (step % 3 == 2) m2.addEvent (juce::MidiMessage::allNotesOff (1), 200);
            blk.clear();
            gCountAllocs = true; const long a0 = gAllocs.load(); p->processBlock (blk, m2); during += gAllocs.load() - a0; gCountAllocs = false;
            for (int k = 0; k < 3; ++k) { juce::MidiBuffer e2; blk.clear(); gCountAllocs = true; const long a1 = gAllocs.load(); p->processBlock (blk, e2); during += gAllocs.load() - a1; gCountAllocs = false; }
        }
        (void) before;
        CHECK (during == 0, "%ld allocations inside processBlock over %d engine / quality switches with notes", during, 4 * numAvail());
    }
    }
    if (want ("cpu")) {
    section ("ENGINES: CPU, 8 voices of one engine on OSC 1 (C2 .. C5 chord held 4 s), % of one core in real time; flag > 2 % per voice at 48 kHz");
    {
        // the reference: the same chord on the Saw wavetable
        auto measure = [] (int e, int q) -> double
        {
            auto p = engPatch (e, q, e < 0 ? TestPoint { 0.5f, 0.5f, 0.5f, true, "" } : testPoint (e));
            std::vector<Ev> evs; const int ch[8] { 36, 43, 48, 52, 55, 60, 64, 67 };
            for (int n : ch) { evs.push_back ({ 0.0, n, 0.8f, true }); evs.push_back ({ 3.9, n, 0.0f, false }); }
            double best = 1e9;
            for (int rep = 0; rep < 2; ++rep)
            {
                const auto t0 = juce::Time::getMillisecondCounterHiRes();
                auto b = render (*p, evs, 4.0);
                best = std::min (best, juce::Time::getMillisecondCounterHiRes() - t0);
            }
            return best / 4000.0 * 100.0;
        };
        // one table: the whole synth (8 voices) per quality, then the engine's own share per voice (its total minus the Saw table's, / 8)
        const double saw[3] { measure (-1, 0), measure (-1, 1), measure (-1, 2) };
        printf ("    %-14s | whole synth, 8 voices (%% of a core)  | engine alone, per voice (%%)  \n", "");
        printf ("    %-14s |  Draft    High   Ultra             |  Draft    High   Ultra\n", "");
        printf ("    %-14s | %6.2f  %6.2f  %6.2f             |   (reference)\n", "Saw (table)", saw[0], saw[1], saw[2]);
        int flagged = 0; juce::String flaggedNames;
        for (int e = 0; e < numAvail(); ++e)
        {
            double t[3], pv[3];
            for (int q = 0; q < 3; ++q) { t[q] = measure (e, q); pv[q] = (t[q] - saw[q]) / 8.0; }
            const bool flag = pv[0] > 2.0;
            if (flag) { ++flagged; flaggedNames << eng::engineNames()[e] << " "; }
            printf ("    %-14s | %6.2f  %6.2f  %6.2f             | %6.3f  %6.3f  %6.3f%s\n", eng::engineNames()[e].toRawUTF8(), t[0], t[1], t[2], pv[0], pv[1], pv[2],
                    flag ? "   <-- above 2 % per voice at 48 kHz" : "");
        }
        printf ("    engines above 2 %% per voice at 48 kHz (Draft): %d %s\n", flagged, flaggedNames.toRawUTF8());
    }
    }
    printf ("\n%d failures\n", failures);
    return failures;
}

// ---- the ENGINE row in the OSC MORE pop-down, both themes (screenshots)
int engineShots (const juce::File& dir)
{
    dir.createDirectory();
    int saved = 0;
    for (int theme : { 0, 2 })
        for (int e : { -1, (int) eng::eVA, (int) eng::eString, (int) eng::eTexture, (int) eng::eResonator })
        {
            if (e >= numAvail()) continue;
            auto p = makeProc (-1); p->initPatch(); if (e >= 0) p->setParam ("o1_tbl", engTbl (e)); settle (*p);
            std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor()); ed->setSize (ProEditor::W, ProEditor::H);
            if (auto* pe = dynamic_cast<ProEditor*> (ed.get())) pe->setTheme (theme);
            ed->setVisible (true); pump (120);
            std::vector<juce::TextButton*> mores;
            std::function<void (juce::Component*)> walk = [&] (juce::Component* c) { for (auto* ch : c->getChildren()) { if (auto* b = dynamic_cast<juce::TextButton*> (ch)) if (b->getButtonText() == "MORE") mores.push_back (b); walk (ch); } };
            walk (ed.get());
            // the OSC 1 MORE button: the first MORE on the SYNTH page whose pop-down title starts with UNISON (try them in order)
            const auto name = juce::String ("engine_row_") + (theme == 0 ? "black_" : "light_") + (e < 0 ? juce::String ("saw") : eng::engineNames()[e].replaceCharacter (' ', '_')) + ".png";
            const auto shot = dir.getChildFile (name);
            for (auto* b : mores)
            {
                if (! b->getTooltip().startsWith ("More for this oscillator")) continue;
                setenv ("GHOSTY_MORESHOT", shot.getFullPathName().toRawUTF8(), 1);
                b->triggerClick(); pump (150);
                unsetenv ("GHOSTY_MORESHOT");
                break;
            }
            if (shot.existsAsFile()) { ++saved; printf ("saved %s\n", shot.getFullPathName().toRawUTF8()); }
            // the whole editor too (the OSC header shows the engine's name)
            if (e >= 0)
            {
                auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
                juce::PNGImageFormat png; auto f = dir.getChildFile (juce::String ("editor_") + (theme == 0 ? "black_" : "light_") + eng::engineNames()[e].replaceCharacter (' ', '_') + ".png"); f.deleteFile();
                if (auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream())) png.writeImageToStream (img, *os);
            }
        }
    CHECK (saved > 0, "%d ENGINE-row screenshots", saved);
    return failures;
}

// ---- STRESS (installer #23 Mac crash): every engine switched from the EDITOR (a click on the STUDIO wave palette's tile -> the parameter
// -> the audio thread picks it up) while another thread renders: random block sizes 1..1024, 44.1 / 48 / 96 kHz, Draft / High / Ultra,
// unison on both oscillators, 16 voices, notes coming and going. Run it under the sanitizers (ASan / UBSan build) and TSan if wanted.
//   ProTests engstress [passes]
int engineStress (const juce::StringArray& args)
{
    const int passes = juce::jmax (1, args.size() > 0 ? args[0].getIntValue() : 2);
    section ("ENGINES stress: palette clicks on the message thread while the audio thread renders");
    long totalBlocks = 0, totalClicks = 0; int badOut = 0;
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        seedSystemRandom (991);
        auto p = std::make_unique<ProProcessor>();
        p->prepareToPlay (sr, 0);                   // (some hosts prepare with a 0 block first)
        p->prepareToPlay (sr, 1024);
        p->initPatch();
        cleantest::set (*p, { { "poly", 16.0f }, { "o1_uni", 4.0f }, { "o2_on", 1.0f }, { "o2_uni", 3.0f }, { "o2_lvl", 0.5f }, { "release", 0.3f } });
        settle (*p);
        const int layoutWas = gpro::layout2::savedLayout(); gpro::layout2::saveLayout (1);          // STUDIO: the palettes under the displays
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor()); ed->setVisible (true); pump (60);
        gpro::layout2::saveLayout (layoutWas);
        juce::Component* pal[2] {};
        std::function<void (juce::Component*)> walk = [&] (juce::Component* c) { for (auto* ch : c->getChildren()) { for (int k = 0; k < 2; ++k) if (ch->getComponentID() == "wavePalette" + juce::String (k + 1)) pal[k] = ch; walk (ch); } };
        walk (ed.get());
        CHECK (pal[0] != nullptr && pal[1] != nullptr && pal[0]->getWidth() > 0, "%.0f Hz: both STUDIO wave palettes found (%d x %d)", sr, pal[0] ? pal[0]->getWidth() : 0, pal[0] ? pal[0]->getHeight() : 0);
        const int tiles = eng::tableChoiceNames().size() + 1, cols = 12, rows = (tiles + cols - 1) / cols;
        auto click = [&] (int k, int tile)
        {
            if (pal[k] == nullptr || pal[k]->getWidth() <= 0) { p->setParam (k == 0 ? "o1_tbl" : "o2_tbl", (float) tile); return; }
            const float w = (float) pal[k]->getWidth() / (float) cols, h = (float) pal[k]->getHeight() / (float) rows;
            const juce::Point<float> at (((float) (tile % cols) + 0.5f) * w, ((float) (tile / cols) + 0.5f) * h);
            const auto now = juce::Time::getCurrentTime();
            juce::MouseEvent ev (juce::Desktop::getInstance().getMainMouseSource(), at, juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                 pal[k], pal[k], now, at, now, 1, false);
            pal[k]->mouseDown (ev);
            ++totalClicks;
        };

        std::atomic<bool> run { true }; std::atomic<long> blocks { 0 }; std::atomic<int> bad { 0 };
        std::thread audio ([&]
        {
            juce::Random r ((juce::int64) sr);
            juce::AudioBuffer<float> big (2, 1024); juce::MidiBuffer mb; mb.ensureSize (4096);
            std::array<bool, 128> down {};
            while (run.load())
            {
                const int n = r.nextInt (5) == 0 ? 1 + r.nextInt (16) : 1 + r.nextInt (1024);
                juce::AudioBuffer<float> view (big.getArrayOfWritePointers(), 2, n);
                view.clear(); mb.clear();
                if (r.nextInt (3) == 0)
                {   // a few notes on / off: up to 16 sounding (x unison)
                    const int note = 36 + r.nextInt (48);
                    if (down[(size_t) note]) { mb.addEvent (juce::MidiMessage::noteOff (1, note), r.nextInt (n)); down[(size_t) note] = false; }
                    else { mb.addEvent (juce::MidiMessage::noteOn (1, note, 0.3f + 0.7f * r.nextFloat()), r.nextInt (n)); down[(size_t) note] = true; }
                }
                p->processBlock (view, mb);
                for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < n; ++i) if (! std::isfinite (view.getSample (ch, i)) || std::abs (view.getSample (ch, i)) > 32.0f) { ++bad; break; }
                ++blocks;
            }
        });
        juce::Random r (17);
        for (int q = 0; q < 3; ++q)
        {
            if (auto* prm = p->apvts.getParameter ("quality")) prm->setValueNotifyingHost (prm->convertTo0to1 ((float) q));
            for (int pass = 0; pass < passes; ++pass)
                for (int e = 0; e < numAvail(); ++e)
                    for (int k = 0; k < 2; ++k)
                    {
                        click (k, (int) numTables + (pass % 2 == 0 ? e : numAvail() - 1 - e));
                        if (r.nextInt (4) == 0) click (1 - k, r.nextInt ((int) numTables));          // sometimes a wavetable on the other one
                        if (r.nextInt (3) == 0) for (auto* id : { "o1_eh", "o1_et", "o1_em", "o2_eh", "o2_et", "o2_em" }) p->setParam (id, r.nextFloat());
                        if (r.nextInt (6) == 0) p->setParam ("o1_rmodel", (float) r.nextInt (2));
                        if (r.nextInt (6) == 0) p->setParam ("o1_esrc", (float) r.nextInt (3));
                        pump (2 + r.nextInt (20));
                    }
        }
        run = false; audio.join();
        ed.reset(); pump (20);
        printf ("    %.0f Hz: %ld blocks rendered during the clicks, %d bad blocks\n", sr, blocks.load(), bad.load());
        totalBlocks += blocks.load(); badOut += bad.load();
        p->releaseResources();
    }
    CHECK (badOut == 0, "no NaN / runaway block in %ld blocks, %ld palette clicks", totalBlocks, totalClicks);
    eng::diary::flush();
    const auto log = eng::diary::file();
    CHECK (log.existsAsFile() && log.loadFileAsString().contains ("palette click") && log.loadFileAsString().contains ("first render OK"),
           "ENGINES DIARY written: %s", log.getFullPathName().toRawUTF8());
    printf ("\n%d failures\n", failures);
    return failures;
}
} // namespace engtest
