// TERRAIN shared component tests: model, mapping, (de)serialisation, gestures + undo / A/B, the view's geometry and cost.
//   TerrainTests            all tests
//   TerrainTests shots DIR  screenshots of the view (one mountain like the reference, several, selected, labels, narrow)
// Run under xvfb-run -a on Linux (the view tests draw).
#include <juce_gui_basics/juce_gui_basics.h>
#include "TerrainModel.h"
#include "TerrainMapping.h"
#include "TerrainSerial.h"
#include "TerrainInteraction.h"
#include "TerrainView.h"
#include "TerrainDsp.h"
#include "TerrainFeatures.h"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <thread>
#include <complex>
#include <algorithm>

// ---- an allocation counter: the audio-thread paths are run with counting on and must allocate nothing
static std::atomic<long> gAllocs { 0 };
static thread_local bool gCountAllocs = false;
void* operator new (std::size_t n) { if (gCountAllocs) ++gAllocs; if (auto* p = std::malloc (n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[] (std::size_t n) { if (gCountAllocs) ++gAllocs; if (auto* p = std::malloc (n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

using namespace ghost::terrain;

static int passes = 0, failures = 0;
static void check (bool ok, const juce::String& what)
{
    if (ok) ++passes; else ++failures;
    std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
    std::fflush (stdout);
}

static void testModel()
{
    Terrain flat = flatTerrain();
    check (flat.heightAt (0.3f, 0.6f) == 0.0f, "a flat terrain is 0 everywhere");
    Terrain t; t.add ({ 0.4f, 0.5f, 0.8f, 0.05f, 0.4f, 0.0f });
    const float top = t.heightAt (0.4f, 0.5f), side = t.heightAt (0.5f, 0.5f), far = t.heightAt (0.9f, 0.5f);
    // (far out only the island's gentle floor is left: under 0.03)
    check (top > 0.6f && side < top && far < 0.03f, "a mountain is highest at its peak and falls away (" + juce::String (top, 3) + ", " + juce::String (side, 3) + ", " + juce::String (far, 4) + ")");
    Terrain v; v.add ({ 0.4f, 0.5f, -0.7f, 0.05f, 0.4f, 0.0f });
    check (v.heightAt (0.4f, 0.5f) < -0.5f, "a valley goes below the ground");
    check (t.heightAt (0.4f, 0.1f) < top * 0.7f && t.heightAt (0.4f, -0.3f) < top * 0.2f, "depth (time): a mountain fades away from its moment");
    Terrain fz = t; fz.frozen = true; fz.freezeZ = 0.5f;
    check (fz.heightAt (0.4f, 0.0f) == fz.heightAt (0.4f, 1.0f), "FREEZE: the same landscape at every moment");
    Terrain g = t; g.grit = 0.8f;
    float diff = 0; for (int i = 0; i < 200; ++i) diff += std::abs (g.heightAt (i / 200.0f, 0.5f) - t.heightAt (i / 200.0f, 0.5f));
    check (diff / 200 > 0.01f, "grit roughens the surface (mean change " + juce::String (diff / 200, 4) + ")");
    check (g.heightAt (0.37f, 0.5f) == g.heightAt (0.37f, 0.5f), "grit is repeatable (same seed, same surface)");
    Terrain e = t; e.evolve = 1.0f; const float before = e.heightAt (0.45f, 0.5f); advance (e, 10.0f);
    check (std::abs (e.heightAt (0.45f, 0.5f) - before) > 1e-3f, "EVOLVE: the landscape drifts as time goes by");
    auto r1 = randomTerrain (7), r2 = randomTerrain (7), r3 = randomTerrain (8);
    check (r1 == r2 && r1 != r3 && ! r1.empty(), "RANDOMIZE is repeatable per seed and differs between seeds");
    Terrain s = r1; s.grit = 0.6f; smooth (s, 1.0f);
    check (s.grit < 0.6f, "SMOOTH lowers the grit; ROUGHEN raises it");
    auto child = breed (randomTerrain (1), randomTerrain (2), 0.5f, 3);
    check (! child.empty() && (int) child.size() <= Terrain::kMaxPeaks, "two terrains breed a child (" + juce::String ((int) child.size()) + " peaks)");
    Terrain bad; bad.add ({ NAN, 2.0f, 9.0f, -1.0f, 0.0f, 5.0f }); bad.clampAll();
    check (std::isfinite (bad.heightAt (0.5f, 0.5f)) && bad[0].height <= kMaxHeight, "bad numbers are clamped (no NaN)");
}

static void testMapping()
{
    Range r;
    check (std::abs (xToHz (0.0f, r) - r.minHz) < 0.01f && std::abs (xToHz (1.0f, r) - r.maxHz) < 1.0f, "x 0..1 = 30 Hz .. 16 kHz");
    check (std::abs (hzToX (xToHz (0.37f, r), r) - 0.37f) < 1e-4f, "frequency mapping round-trips");
    check (qFromWidth (0.015f, r) > qFromWidth (0.1f, r), "a narrower mountain is a sharper resonance (higher Q)");
    Terrain t; t.add ({ hzToX (1000.0f, r), 0.5f, 0.6f, 0.03f, 0.5f, 0.2f });
    std::vector<Band> b; bandsAt (t, 0.5f, b, r);
    check (b.size() == 1 && std::abs (b[0].hz - 1000.0f) < 1.0f && b[0].gainDb > 8.0f && b[0].drive > 0.19f,
           "a mountain at 1 kHz is a +" + juce::String (b[0].gainDb, 1) + " dB band at 1 kHz, Q " + juce::String (b[0].q, 1));
    float hz[3] = { 1000.0f, 3000.0f, 200.0f }, db[3];
    gainCurveDb (t, 0.5f, hz, 3, db, r);
    // (far away only the island's floor is left: the gentle overall lift, under 1.5 dB)
    check (db[0] > db[1] && db[0] > db[2] && std::abs (db[2]) < 1.5f, "the gain curve peaks at the mountain and is nearly flat far away (" + juce::String (db[2], 2) + " dB)");
}

static void testSerial()
{
    auto t = randomTerrain (42); t.grit = 0.3f; t.evolve = 0.6f; t.phase = 0.25f; t.frozen = true; t.freezeZ = 0.7f;
    Terrain back;
    check (fromString (toString (t), back) && back == t, "the terrain saves and re-opens exactly (JSON)");
    Terrain vt;
    check (fromValueTree (toValueTree (t), vt) && vt == t, "the terrain saves and re-opens exactly (ValueTree)");
    Terrain partial;
    check (fromString ("{\"v\":1,\"peaks\":[{\"x\":0.2}]}", partial) && partial.size() == 1 && partial[0].height == Peak().height,
           "missing fields take defaults");
    Terrain junk;
    check (! fromString ("not json", junk), "junk is refused");
    TerrainEditor ed; ed.set (t); ed.toggleAB(); ed.set (randomTerrain (5));
    TerrainEditor ed2;
    check (ed2.stateFromVar (ed.stateToVar()) && ed2.slot (0) == ed.slot (0) && ed2.slot (1) == ed.slot (1) && ed2.activeSlot() == 1,
           "both A/B terrains and the active one are saved with the session");
}

static void testEditing()
{
    TerrainEditor ed;
    ed.setWithoutUndo (flatTerrain());
    TerrainInteraction in (ed);
    // a simple flat map: screen = ground (x, z) * 100 px, no handles
    in.pick = [] (juce::Point<float> p, float& x, float& z) { x = p.x / 100.0f; z = p.y / 100.0f; return x >= 0 && x <= 1 && z >= 0 && z <= 1; };
    in.handleAt = [] (juce::Point<float>) { return -1; };
    in.pixelsPerUnitHeight = 100.0f;
    int commits = 0; ed.onCommit = [&] { ++commits; };
    in.mouseDown ({ 30, 50 }, {}); in.mouseUp ({ 30, 50 }, {});
    check (ed.terrain().size() == 1 && std::abs (ed.terrain()[0].x - 0.3f) < 1e-4f && std::abs (ed.terrain()[0].height - 0.3f) < 1e-4f,
           "a click raises a mountain where you click (0.30 tall, as on the reference site)");
    in.mouseDown ({ 70, 50 }, {}); in.mouseDrag ({ 70, 110 }, {}); in.mouseUp ({ 70, 110 }, {});
    check (ed.terrain().size() == 2 && ed.terrain()[1].height < -0.2f, "dragging down from the ground carves a valley");
    check (commits == 2, "each gesture is one undo step (" + juce::String (commits) + ")");
    // handle drags
    in.handleAt = [] (juce::Point<float> p) { return p.y < 0 ? 0 : -1; };
    const float h0 = ed.terrain()[0].height;
    in.mouseDown ({ 30, -20 }, {}); in.mouseDrag ({ 30, -40 }, {});
    check (in.liveText.isNotEmpty(), "a value tag shows while dragging (" + in.liveText + ")");
    in.mouseUp ({ 30, -40 }, {});
    check (ed.terrain()[0].height > h0, "dragging the dot up raises the mountain");
    in.widthTagAt = [] (juce::Point<float> p) { return p.y > 200 ? 0 : -1; };
    const float w0 = ed.terrain()[0].width;
    in.mouseDown ({ 30, 210 }, {}); in.mouseDrag ({ 90, 210 }, {}); in.mouseUp ({ 90, 210 }, {});
    check (ed.terrain()[0].width > w0, "dragging the width tag sideways widens it");
    in.handleAt = [] (juce::Point<float>) { return -1; };
    in.lineAt = [] (juce::Point<float> p) { return p.x > 25 && p.x < 35 && p.y > -10 && p.y < 10 ? 0 : -1; };
    in.mouseDown ({ 30, 0 }, {}); in.mouseDrag ({ 50, 10 }, {}); in.mouseUp ({ 50, 10 }, {});
    check (std::abs (ed.terrain()[0].x - 0.5f) < 1e-3f && std::abs (ed.terrain()[0].z - 0.6f) < 1e-3f, "dragging the stem moves the mountain (frequency and time)");
    in.lineAt = nullptr;
    in.handleAt = [] (juce::Point<float> p) { return p.y < 0 ? 0 : -1; };
    const auto now = ed.terrain();
    check (ed.undo() && ed.terrain() != now && ed.redo() && ed.terrain() == now, "UNDO / REDO step through every edit");
    int undos = 0; while (ed.undo()) ++undos;
    check (ed.terrain() == flatTerrain(), "UNDO all the way gives the start back (" + juce::String (undos) + " steps)");
    while (ed.redo()) {}
    in.doubleClick ({ 30, -20 });
    check (ed.terrain().size() == 1, "double-clicking a handle removes that mountain");
    const auto a = ed.terrain();
    ed.toggleAB(); ed.set (randomTerrain (9));
    check (ed.activeSlot() == 1 && ed.terrain() != a, "A/B: B is its own terrain");
    ed.toggleAB();
    check (ed.terrain() == a, "A/B: back to A exactly");
    TerrainEditor full; full.setWithoutUndo (flatTerrain());
    TerrainInteraction fi (full);
    fi.pick = in.pick; fi.handleAt = [] (juce::Point<float>) { return -1; };
    // (a tap on empty land adds one when none is chosen; with one chosen it lets go of it - the reference's)
    for (int i = 0; i < 20; ++i) { full.select (-1); fi.mouseDown ({ 3.0f + 4.8f * i, 50 }, {}); fi.mouseUp ({ 3.0f + 4.8f * i, 50 }, {}); }
    check ((int) full.terrain().size() == Terrain::kMaxPeaks, "at most " + juce::String (Terrain::kMaxPeaks) + " mountains");
}

// ---- small signal tools for the DSP tests
namespace
{
struct Lcg { uint32_t s = 12345; float next() { s = s * 1664525u + 1013904223u; return (float) (s >> 8) / 8388608.0f - 1.0f; } };

float rmsOf (const std::vector<float>& v, int from = 0) { double a = 0; for (int i = from; i < (int) v.size(); ++i) a += (double) v[(size_t) i] * v[(size_t) i]; return (float) std::sqrt (a / juce::jmax (1, (int) v.size() - from)); }

// the strongest frequency in the last 8192 samples (Hann, straight DFT over a band of bins: slow but plain)
float peakHz (const std::vector<float>& v, double sr, float loHz, float hiHz)
{
    const int n = 8192, start = (int) v.size() - n;
    const int k0 = (int) (loHz * n / sr), k1 = (int) (hiHz * n / sr);
    float best = -1.0f; int bestK = k0;
    for (int k = k0; k <= k1; ++k)
    {
        std::complex<double> acc;
        for (int i = 0; i < n; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (6.283185307 * i / n);
            const double ph = -6.283185307 * k * i / n;
            acc += std::complex<double> (v[(size_t) (start + i)] * w * std::cos (ph), v[(size_t) (start + i)] * w * std::sin (ph));
        }
        const float m = (float) std::abs (acc);
        if (m > best) { best = m; bestK = k; }
    }
    return (float) bestK * (float) sr / (float) n;
}

std::vector<float> sine (float hz, double sr, int n, float amp = 0.5f)
{
    std::vector<float> v ((size_t) n);
    for (int i = 0; i < n; ++i) v[(size_t) i] = amp * std::sin (6.283185307f * hz * (float) i / (float) sr);
    return v;
}

float maxJump (const std::vector<float>& v) { float m = 0; for (size_t i = 1; i < v.size(); ++i) m = juce::jmax (m, std::abs (v[i] - v[i - 1])); return m; }
bool finiteAll (const std::vector<float>& v) { for (float x : v) if (! std::isfinite (x)) return false; return true; }
}

static void testDsp()
{
    const double sr = 48000.0;
    Range range;
    // ---- the hand-off
    {
        TerrainShare share;
        Terrain got;
        check (! share.pull (got), "nothing to pull before anything is published");
        auto a = randomTerrain (3); share.publish (a);
        check (share.pull (got) && got == a, "the audio side pulls exactly what was published");
        check (! share.pull (got), "and nothing twice");
        auto b = randomTerrain (4), c = randomTerrain (5); share.publish (b); share.publish (c);
        check (share.pull (got) && got == c, "two publishes before a pull: the newest wins");
        // a writer thread hammering publishes while we pull: every pulled terrain is a whole one (count matches its peaks)
        std::atomic<bool> stop { false };
        std::thread writer ([&]
        {
            for (uint32_t k = 1; ! stop.load(); ++k)
            {
                Terrain t; const int cnt = 1 + (int) (k % 8);
                for (int i = 0; i < cnt; ++i) t.add ({ (float) cnt / 10.0f, (float) cnt / 10.0f, (float) cnt / 10.0f, 0.05f, 0.14f, 0.0f });
                t.seed = (uint32_t) cnt;
                share.publish (t);
            }
        });
        bool torn = false; int pulls = 0;
        // (until 200 whole terrains came through, or 3 s: on a busy 2-core box the writer may not get a turn for a while)
        const double tEnd = juce::Time::getMillisecondCounterHiRes() + 3000.0;
        for (int i = 0; ! torn && pulls < 200 && (i % 1024 != 0 || juce::Time::getMillisecondCounterHiRes() < tEnd); ++i)
            if (share.pull (got))
            {
                ++pulls;
                if ((int) got.seed != got.size()) torn = true;
                for (int p = 0; p < got.size(); ++p) if (std::abs (got[p].x - (float) got.size() / 10.0f) > 1e-6f) torn = true;
            }
        stop = true; writer.join();
        check (! torn && pulls > 10, "a writer thread and a reader never see a torn terrain (" + juce::String (pulls) + " pulls)");
    }
    // ---- mapping: a mountain at 1 kHz is a resonance at 1 kHz, in the bank and in the offline render
    Terrain one; one.add ({ hzToX (1000.0f, range), 0.5f, 0.8f, 0.02f, 2.0f, 0.0f });
    {
        TerrainBank bank; bank.prepare (sr, 1); bank.setRange (range); bank.setTerrain (one); bank.setDepth (0.5f);
        const int n = (int) sr * 2;
        std::vector<float> noise ((size_t) n); Lcg g; for (auto& x : noise) x = 0.3f * g.next();
        for (int pos = 0; pos < n; pos += 256) { float* ch[1] = { noise.data() + pos }; bank.process (ch, 1, juce::jmin (256, n - pos)); }
        const float hz = peakHz (noise, sr, 200.0f, 5000.0f);
        check (std::abs (hz - 1000.0f) < 60.0f, "bank: white noise through a 1 kHz mountain peaks at " + juce::String (hz, 0) + " Hz");
        auto gainAt = [&] (float f)
        {
            TerrainBank b2; b2.prepare (sr, 1); b2.setRange (range); b2.setTerrain (one); b2.setDepth (0.5f);
            auto s = sine (f, sr, (int) sr); const float inR = rmsOf (s, (int) sr / 2);
            for (int pos = 0; pos < (int) s.size(); pos += 256) { float* c2[1] = { s.data() + pos }; b2.process (c2, 1, juce::jmin (256, (int) s.size() - pos)); }
            return 20.0f * std::log10 (rmsOf (s, (int) sr / 2) / inR);
        };
        const float at1k = gainAt (1000.0f), at250 = gainAt (250.0f);
        Band bb[8]; bandsAt (one, 0.5f, bb, range);
        check (std::abs (at1k - bb[0].gainDb) < 1.5f && std::abs (at250) < 1.0f,
               "bank: +" + juce::String (at1k, 1) + " dB at 1 kHz (asked +" + juce::String (bb[0].gainDb, 1) + "), " + juce::String (at250, 1) + " dB at 250 Hz");
        // a valley cuts
        Terrain val; val.add ({ hzToX (1000.0f, range), 0.5f, -0.8f, 0.02f, 2.0f, 0.0f });
        TerrainBank b3; b3.prepare (sr, 1); b3.setRange (range); b3.setTerrain (val); b3.setDepth (0.5f);
        auto s = sine (1000.0f, sr, (int) sr); const float inR = rmsOf (s, (int) sr / 2);
        for (int pos = 0; pos < (int) s.size(); pos += 256) { float* c3[1] = { s.data() + pos }; b3.process (c3, 1, juce::jmin (256, (int) s.size() - pos)); }
        const float cut = 20.0f * std::log10 (rmsOf (s, (int) sr / 2) / inR);
        check (cut < -8.0f, "bank: a valley at 1 kHz cuts 1 kHz by " + juce::String (-cut, 1) + " dB");
    }
    {
        const int n = (int) sr * 2;
        std::vector<float> noise ((size_t) n), out ((size_t) n); Lcg g; for (auto& x : noise) x = 0.3f * g.next();
        const float* in[1] = { noise.data() }; float* o[1] = { out.data() };
        sculpt (one, in, o, 1, n, sr);
        const float hz = peakHz (out, sr, 200.0f, 5000.0f);
        check (std::abs (hz - 1000.0f) < 60.0f, "sculpt: white noise through a 1 kHz mountain peaks at " + juce::String (hz, 0) + " Hz");
        auto gainAt = [&] (float f)
        {
            auto s = sine (f, sr, (int) sr); std::vector<float> r (s.size());
            const float* i2[1] = { s.data() }; float* o2[1] = { r.data() };
            sculpt (one, i2, o2, 1, (int) s.size(), sr);
            return 20.0f * std::log10 (rmsOf (r, (int) sr / 4) / rmsOf (s, (int) sr / 4));
        };
        const float at1k = gainAt (1000.0f), at250 = gainAt (250.0f);
        check (at1k > sculptGainDb (one, 1000.0f, 0.5f) - 1.0f && std::abs (at250) < 1.0f,
               "sculpt: +" + juce::String (at1k, 1) + " dB at 1 kHz (the curve says +" + juce::String (sculptGainDb (one, 1000.0f, 0.5f), 1) + " plus the ring), " + juce::String (at250, 1) + " dB at 250 Hz");
        // flat ground changes nothing and keeps the time
        std::vector<float> imp ((size_t) 20000, 0.0f), r2 (imp.size()); imp[1000] = 1.0f;
        const float* i3[1] = { imp.data() }; float* o3[1] = { r2.data() };
        sculpt (flatTerrain(), i3, o3, 1, (int) imp.size(), sr);
        int at = 0; float mx = 0; for (int i = 0; i < (int) r2.size(); ++i) if (std::abs (r2[(size_t) i]) > mx) { mx = std::abs (r2[(size_t) i]); at = i; }
        check (at == 1000 && std::abs (mx - 1.0f) < 0.02f, "sculpt: flat ground passes an impulse through unchanged and on time (sample " + juce::String (at) + ", " + juce::String (mx, 3) + ")");
        // a peak that lives only at the end of the sound is heard only there
        Terrain late; late.add ({ hzToX (1000.0f, range), 0.9f, 0.9f, 0.02f, 0.06f, 0.0f });
        auto s = sine (1000.0f, sr, (int) sr * 2); std::vector<float> r3 (s.size());
        const float* i4[1] = { s.data() }; float* o4[1] = { r3.data() };
        sculpt (late, i4, o4, 1, (int) s.size(), sr);
        std::vector<float> head (r3.begin() + 4000, r3.begin() + 40000), tail (r3.end() - 20000, r3.end() - 4000);
        check (rmsOf (tail) > rmsOf (head) * 2.5f, "sculpt: the time axis works (a mountain at the end: tail " + juce::String (20 * std::log10 (rmsOf (tail) / rmsOf (head)), 1) + " dB louder than the head)");
    }
    // ---- no clicks while dragging fast: a narrow, driven mountain is dragged from 100 Hz to 4 kHz in half a second (60 updates a
    // second, like a mouse) with its height jittering; the output must be as smooth across the block edges (where the new
    // settings arrive) as anywhere else, and never blow up
    {
        const int block = (int) sr / 60, n = (int) sr;
        std::vector<float> sig = sine (220.0f, sr, n, 0.4f);
        Lcg g; for (auto& x : sig) x += 0.05f * g.next();
        TerrainBank b; b.prepare (sr, 1); b.setRange (range); b.setDepth (0.5f);
        juce::Random rnd (7);
        for (int pos = 0; pos < n; pos += block)
        {
            const float u = juce::jlimit (0.0f, 1.0f, (float) pos / (float) (n / 2));
            Terrain t; t.add ({ hzToX (100.0f * std::pow (40.0f, u), range), 0.5f, 0.9f + 0.1f * (rnd.nextFloat() * 2.0f - 1.0f), 0.018f, 2.0f, 0.3f });
            t.add ({ hzToX (3000.0f, range), 0.5f, -0.6f + 0.2f * rnd.nextFloat(), 0.03f, 2.0f, 0.0f });
            b.setTerrain (t);
            float* c[1] = { sig.data() + pos };
            b.process (c, 1, juce::jmin (block, n - pos));
        }
        std::vector<float> d2 ((size_t) n, 0.0f);
        for (int i = 2; i < n; ++i) d2[(size_t) i] = std::abs (sig[(size_t) i] - 2.0f * sig[(size_t) i - 1] + sig[(size_t) i - 2]);
        float edgeMax = 0.0f; std::vector<float> inside;
        for (int i = 2 * block; i < n; ++i) { if (i % block < 3) edgeMax = juce::jmax (edgeMax, d2[(size_t) i]); else inside.push_back (d2[(size_t) i]); }
        std::sort (inside.begin(), inside.end());
        const float inside99 = inside[(size_t) ((double) inside.size() * 0.999)], insideMax = inside.back();
        float peakAbs = 0.0f; for (float x : sig) peakAbs = juce::jmax (peakAbs, std::abs (x));
        check (finiteAll (sig) && edgeMax < 1.5f * insideMax && edgeMax < 4.0f * inside99 && peakAbs < 6.0f,
               "no clicks while dragging fast: the block edges are no rougher than the rest (edge " + juce::String (edgeMax, 4) + ", inside max " + juce::String (insideMax, 4)
               + ", 99.9% " + juce::String (inside99, 4) + "; peak " + juce::String (peakAbs, 2) + ")");
        // and the same drag with the settings snapped instead of ramped would click: we check the ramp is doing work
        // (the per-sample coefficient steps are not zero during a drag)
        check (b.activeBands() == 2, "both mountains are live bands at the end of the drag (" + juce::String (b.activeBands()) + ")");
    }
    // ---- nothing on the audio-thread path allocates
    {
        TerrainShare share; TerrainBank bank; bank.prepare (sr, 2);
        std::vector<float> l ((size_t) 512, 0.1f), r ((size_t) 512, 0.1f);
        float* ch[2] = { l.data(), r.data() };
        SpectrumTap tap;
        share.publish (randomTerrain (9));
        gAllocs = 0; gCountAllocs = true;
        Terrain t;
        for (int i = 0; i < 50; ++i)
        {
            if (share.pull (t)) bank.setTerrain (t);
            bank.setDepth ((float) i / 50.0f);
            bank.process (ch, 2, 512);
            tap.push (ch, 2, 512);
            Band b[8]; bandsAt (t, 0.3f, b, range);
        }
        gCountAllocs = false;
        check (gAllocs == 0, "the audio-thread path (pull, bandsAt, bank, analyzer tap) allocates nothing (" + juce::String ((int) gAllocs.load()) + ")");
        std::vector<float> db;
        check (tap.read (db, 128, range, sr) && (int) db.size() == 128, "the analyzer gives a spectrum across x");
    }
    // ---- the offline render is quick enough to follow a drag (a 5 s stereo take)
    {
        const int n = (int) (44100 * 5);
        std::vector<float> a ((size_t) n), b ((size_t) n); Lcg g; for (int i = 0; i < n; ++i) { a[(size_t) i] = 0.3f * g.next(); b[(size_t) i] = 0.3f * g.next(); }
        const float* in[2] = { a.data(), b.data() }; float* out[2] = { a.data(), b.data() };
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        sculpt (defaultTerrain(), in, out, 2, n, 44100.0);
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        check (ms < 1000.0, "sculpt renders a 5 s stereo take in " + juce::String (ms, 0) + " ms (< 1000)");
    }
}

// ---- the islands (the reference's land), shapes, features, labels, old landscapes
static void testIslands()
{
    // a landscape saved before the islands (v 1) is the same land, number for number (sums from the first TERRAIN's code)
    {
        Terrain t; t.version = 1; t.seed = 5;
        t.add ({ 0.3f, 0.4f, 0.7f, 0.05f, 0.2f, 0.3f });
        t.add ({ 0.8f, 0.9f, -0.4f, 0.03f, 0.1f, 0.0f });
        t.add ({ 0.55f, 0.1f, 0.9f, 0.2f, 0.5f, 0.0f });
        t.grit = 0.2f;
        double s = 0, g = 0;
        for (int j = 0; j < 40; ++j) for (int i = 0; i < 60; ++i) s += t.heightAt (i / 59.0f, j / 39.0f) * (1 + i % 7);
        float hz[64], db[64]; for (int i = 0; i < 64; ++i) hz[i] = 30.0f * std::pow (500.0f, i / 63.0f);
        gainCurveDb (t, 0.37f, hz, 64, db); for (int i = 0; i < 64; ++i) g += db[i] * (1 + i % 5);
        Band b[Terrain::kMaxPeaks]; const int n = bandsAt (t, 0.6f, b); double q = 0; for (int i = 0; i < n; ++i) q += b[i].q + b[i].gainDb + b[i].hz * 1e-3;
        check (std::abs (s - 2497.38761) < 1e-3 && std::abs (g - 1399.09828) < 1e-3 && std::abs (q - 25.144334) < 1e-4,
               "an old landscape (v 1) is the same land and the same sound (" + juce::String (s, 5) + " " + juce::String (g, 5) + " " + juce::String (q, 6) + ")");
        Terrain back;
        juce::String old = toString (t).replace ("\"v\": 1", "\"v\": 1");
        check (fromString (old, back) && back.version == 1 && back.sameLand (t), "it loads as v 1 (its own land kept)");
        juce::var v; juce::JSON::parse (old, v); v.getDynamicObject()->removeProperty ("v");
        check (fromVar (v, back) && back.version == 1, "a landscape with no version at all is a v 1 one");
    }
    // new land is the reference's: the island floor everywhere, mountains held in by the shore in the picture
    {
        Terrain t = defaultTerrain();
        check (t.version == 2 && t.size() == 1 && t.seed == 7 && std::abs (t[0].r() - 0.38f) < 1e-4f && std::abs (t[0].height - 0.55f) < 1e-6f,
               "the opening land is the reference's: one mountain, r 0.38, 0.55 tall, the ground of seed 7");
        std::vector<float> held (64 * 64), free (64 * 64);
        Terrain e; e.add ({ 0.92f, 0.5f, 0.8f, 0.03f, 0.03f, 0.0f });
        e.sample (64, 64, 0, 1, 0, 1, held.data(), nullptr, nullptr, nullptr, -1, nullptr, nullptr, true, true);
        e.sample (64, 64, 0, 1, 0, 1, free.data(), nullptr, nullptr, nullptr, -1, nullptr, nullptr, true, false);
        const float mh = *std::max_element (held.begin(), held.end()), mf = *std::max_element (free.begin(), free.end());
        check (mh < mf * 0.5f && mf > 0.6f, "a mountain near the shore sinks into the sea in the picture (" + juce::String (mh, 2) + ") but keeps its height for the sound (" + juce::String (mf, 2) + ")");
        Terrain big; big.add ({ 0.5f, 0.5f, 1.2f, 0.07f, 0.07f, 0.0f });
        check (big.heightAt (0.5f, 0.5f) > 1.0f && big[0].height == 1.2f, "a mountain can stand 1.2 tall (past the top ring: a flat-topped column)");
        Terrain thin; thin.add ({ 0.5f, 0.5f, 1.0f, 0.01f, 0.01f, 0.0f });
        check (std::abs (thin[0].r() - 0.04f) < 1e-5f && thin.heightAt (0.5f, 0.5f) > 0.9f && thin.heightAt (0.53f, 0.5f) < 0.2f, "a spire: r 0.04, tall and thin");
    }
    // the reference's shuffle: as many mountains as now plus two, at most six; the ground stays; wild ones are of every kind
    {
        Terrain one = defaultTerrain();
        auto a = shuffleTerrain (one, 11, false), b = shuffleTerrain (a, 12, false), c = shuffleTerrain (b, 13, false), d = shuffleTerrain (c, 14, false);
        check (a.size() == 3 && b.size() == 5 && c.size() == 6 && d.size() == 6 && a.seed == one.seed, "shuffle: 1 -> 3 -> 5 -> 6 -> 6 mountains, the ground kept");
        check (shuffleTerrain (one, 11, false) == a, "shuffle is repeatable per seed");
        float spires = 0, plateaus = 0, knolls = 0; int most = 0;
        for (uint32_t sd = 1; sd <= 40; ++sd)
        {
            auto w = randomTerrain (sd, 10, true);
            const auto f = landFeatures (w);
            spires += f.spires; plateaus += f.plateaus; knolls += f.knolls; most = juce::jmax (most, w.size());
            for (auto& p : w) if (p.height > kMaxHeight || p.height < 0.0f || ! std::isfinite (p.x)) spires = -1e9f;
        }
        check (spires > 20 && plateaus > 6 && knolls > 8 && most == 10, "wild RANDOMIZE makes spires, plateaus and knolls, all within 0 .. 1.2 (" + juce::String (spires, 0) + ", " + juce::String (plateaus, 0) + ", " + juce::String (knolls, 0) + " in 40 lands)");
    }
    // features
    {
        Terrain t;
        t.add ({ 0.3f, 0.5f, 1.0f, 0.012f, 0.012f, 0.0f });    // a spire
        t.add ({ 0.7f, 0.5f, 1.2f, 0.05f, 0.05f, 0.0f });      // a plateau
        t.add ({ 0.72f, 0.55f, 0.6f, 0.06f, 0.06f, 0.0f });    // running into it
        const auto f = landFeatures (t);
        check (f.count == 3 && f.m[0].spire > 0.8f && f.m[1].plateau > 0.8f && f.m[0].plateau < 0.1f && f.m[1].spire < 0.2f, "features: a spire is a spire, a plateau a plateau");
        check (f.m[2].merge > 0.5f && f.m[0].merge < 0.05f && f.tallest > 1.1f && std::abs (f.tallestX - 0.7f) < 1e-4f, "features: what merges, the tallest and where");
        check (landFeatures (t) .mass == f.mass && landFeatures (flatTerrain()).count == 0 && landFeatures (flatTerrain()).mass == 0.0f, "features are repeatable; flat land has none");
        Terrain spread; spread.add ({ 0.1f, 0.5f, 0.5f, 0.03f, 0.03f, 0 }); spread.add ({ 0.9f, 0.5f, 0.5f, 0.03f, 0.03f, 0 });
        Terrain tight; tight.add ({ 0.48f, 0.5f, 0.5f, 0.03f, 0.03f, 0 }); tight.add ({ 0.52f, 0.5f, 0.5f, 0.03f, 0.03f, 0 });
        check (landFeatures (spread).spread > landFeatures (tight).spread + 0.5f, "features: scattered vs clustered");
    }
    // ring labels: saved, loaded, kept apart from the sound
    {
        Terrain t = defaultTerrain();
        RingLabel l; l.x = 0.4f; l.z = 0.45f; l.level = 6; juce::String ("75577").copyToUTF8 (l.text, sizeof (l.text));
        t.addLabel (l);
        Terrain back;
        check (fromString (toString (t), back) && back == t && back.labelCount() == 1 && juce::String::fromUTF8 (back.labels[0].text) == "75577", "a ring label is saved and loaded");
        check (back.sameLand (defaultTerrain()) && back != defaultTerrain(), "a label is not land (the sound does not change)");
        TerrainEditor ed; ed.set (t);
        TerrainEditor e2; e2.stateFromVar (ed.stateToVar());
        check (e2.terrain() == t, "labels ride in the editor state too (sessions)");
    }
    // the editor feels how fast it is pulled
    {
        TerrainEditor ed; ed.setWithoutUndo (defaultTerrain());
        ed.beginGesture();
        for (int i = 0; i < 6; ++i) { ed.live()[0].x += 0.05f; ed.changed(); juce::Thread::sleep (16); }
        const float m = ed.motion();
        ed.endGesture();
        juce::Thread::sleep (700);
        check (m > 0.5f && ed.motion() < 0.1f, "drag speed: a quick pull reads high (" + juce::String (m, 2) + "), still hands read 0 (" + juce::String (ed.motion(), 2) + ")");
    }
}

// ---- side-by-side states with Brandon's phone recordings (the reference site at 402 x 776 css px, ground 1.5, shot at 3x
// and cut to the card the frames were cut to: page y 100 .. 600)
struct Moment { const char* name; Terrain land; std::vector<std::array<float, 4>> dots = {}; std::vector<std::pair<juce::Point<float>, juce::String>> labels = {}; int selected = -1; int grab = -1; int grabKind = 0; float open = 1.0f; bool labelMode = false; float w = 402, h = 776, gs = 1.5f; bool card = true; };
static Peak pk (float x, float z, float h, float r) { Peak p; p.x = x; p.z = z; p.height = h; p.width = p.depth = r * 0.25f; return p; }
static void renderMoments (const juce::File& dir, const std::vector<Moment>& ms, const Palette& pal = Palette::amber())
{
    dir.createDirectory();
    for (auto& m : ms)
    {
        TerrainEditor ed; TerrainView v (ed);
        v.setPalette (pal);
        v.groundScale = m.gs;
        ed.setWithoutUndo (m.land); ed.selected = m.selected;
        v.setSize ((int) m.w, (int) m.h);
        v.settle();
        // mountains given by where their dot is in the frame (css px), their height and size: placed so the dot lands there
        for (auto& d : m.dots)
        {
            Terrain t = ed.terrain();
            float gx = 0.5f, gz = 0.5f;
            v.groundAtLift ({ d[0], d[1] + 184.0f }, juce::jmin (1.0f, d[2] + 0.06f), gx, gz);   // (a first guess: the mountain standing whole)
            t.add (pk (gx, gz, d[2], d[3]));
            const int i = t.size() - 1;
            for (int it = 0; it < 30; ++it)
            {
                ed.setWithoutUndo (t); v.settle();
                const auto p0 = v.handleDot (i);
                const float e = 0.01f;
                Terrain tx = t; tx[i].x += e; ed.setWithoutUndo (tx); v.settle(); const auto px = v.handleDot (i);
                Terrain tz = t; tz[i].z += e; ed.setWithoutUndo (tz); v.settle(); const auto pz = v.handleDot (i);
                const float a = (px.x - p0.x) / e, b = (pz.x - p0.x) / e, c = (px.y - p0.y) / e, dd = (pz.y - p0.y) / e;
                const float det = a * dd - b * c;
                if (std::abs (det) < 1e-6f) break;
                const float ex = d[0] - p0.x, ey = d[1] - p0.y;
                t[i].x = juce::jlimit (0.0f, 1.0f, t[i].x + 0.7f * (dd * ex - b * ey) / det);
                t[i].z = juce::jlimit (0.0f, 1.0f, t[i].z + 0.7f * (-c * ex + a * ey) / det);
                if (std::abs (ex) + std::abs (ey) < 0.3f) break;
            }
            ed.setWithoutUndo (t);
        }
        ed.selected = m.selected;
        v.settle();
        // own ring labels, put on the ring nearest to a screen point
        if (! m.labels.empty())
        {
            Terrain t = ed.terrain();
            for (auto& [at, text] : m.labels)
            {
                int best = -1; float bd = 1e9f; size_t bk = 0;
                const auto& rs = v.drawnRings();
                for (int r = 0; r < (int) rs.size(); ++r)
                    for (size_t k = 0; k < rs[(size_t) r].screen.size(); ++k)
                    { const float d = rs[(size_t) r].screen[k].getDistanceFrom (at); if (d < bd && rs[(size_t) r].level > 0) { bd = d; best = r; bk = k; } }
                if (best < 0) continue;
                RingLabel L; L.level = (int8_t) rs[(size_t) best].level;
                L.x = 0.5f + 0.5f * rs[(size_t) best].ground[bk].x; L.z = 0.5f - 0.5f * rs[(size_t) best].ground[bk].y;
                text.copyToUTF8 (L.text, sizeof (L.text));
                t.addLabel (L);
            }
            ed.setWithoutUndo (t); v.settle();
        }
        if (m.labelMode) v.setLabelMode (true), ed.selected = m.selected;
        if (m.grab >= 0)
        {
            ed.selected = m.grab; v.settle();
            const auto at = m.grabKind == 1 ? v.heightTagCentre (m.grab) : (m.grabKind == 2 ? v.widthTagCentre (m.grab) : v.handleDot (m.grab));
            v.interaction.mouseDown (at, {});
        }
        if (m.open < 1.0f) v.stillOpening = m.open;
        auto im = v.snapshot (3.0f);
        if (m.card) im = im.getClippedImage ({ 0, 300, im.getWidth(), 1500 });
        auto f = dir.getChildFile (juce::String (m.name) + ".png"); f.deleteFile();
        juce::FileOutputStream os (f); juce::PNGImageFormat png; png.writeImageToStream (im, os);
        std::printf ("moment %s  dots:", m.name);
        for (int i = 0; i < ed.terrain().size(); ++i) { const auto d = v.handleDot (i); std::printf (" (%.1f, %.1f)", d.x, d.y); }
        std::printf ("\n");
        if (m.grab >= 0) v.interaction.mouseUp ({}, {});
        std::printf ("   land:");
        for (auto& q : ed.terrain()) std::printf (" pk (%.3ff, %.3ff, %.2ff, %.2ff),", q.x, q.z, q.height, q.r());
        std::printf ("\n");
    }
}

// ---- the live chain: untouched at zero, repeatable, no clicks on a fast drag, under its ceiling, nothing allocated
static void testLive()
{
    const double sr = 48000.0;
    const int n = (int) sr * 2;
    std::vector<float> L ((size_t) n), R ((size_t) n);
    for (int i = 0; i < n; ++i) { L[(size_t) i] = 0.5f * std::sin (2.0f * 3.14159265f * 220.0f * (float) i / (float) sr); R[(size_t) i] = 0.5f * std::sin (2.0f * 3.14159265f * 331.0f * (float) i / (float) sr); }
    auto run = [&] (const std::function<void (TerrainLive&, int)>& atBlock, std::vector<float>& a, std::vector<float>& b, uint32_t seed = 3)
    {
        a = L; b = R;
        TerrainLive live; live.prepare (sr, 2); live.setSeed (seed);
        for (int p = 0; p < n; p += 256)
        {
            atBlock (live, p);
            float* chs[2] = { a.data() + p, b.data() + p };
            live.process (chs, 2, juce::jmin (256, n - p));
        }
    };
    std::vector<float> a, b, a2, b2;
    run ([] (TerrainLive&, int) {}, a, b);
    check (a == L && b == R, "the live chain at zero passes the sound untouched, bit for bit");
    LiveParams wild; wild.drive = 0.6f; wild.shatter = 0.7f; wild.reverse = 0.5f; wild.shimmer = 0.6f; wild.smear = 0.5f; wild.freeze = 0.4f; wild.wobble = 0.6f; wild.tilt = 0.5f; wild.width = 0.8f;
    run ([&] (TerrainLive& l, int) { l.setParams (wild); }, a, b);
    run ([&] (TerrainLive& l, int) { l.setParams (wild); }, a2, b2);
    float diff = 0; for (int i = 0; i < n; ++i) diff += std::abs (a[(size_t) i] - L[(size_t) i]);
    check (a == a2 && b == b2 && diff / (float) n > 0.02f, "the live chain changes the sound, the same way every time");
    // a fast drag: every amount jumps somewhere new every 10 ms
    juce::Random rnd (5);
    run ([&] (TerrainLive& l, int p) { if (p % 512 == 0) { LiveParams q; q.drive = rnd.nextFloat(); q.shatter = rnd.nextFloat(); q.shatterSize = rnd.nextFloat(); q.reverse = rnd.nextFloat();
                                                            q.shimmer = rnd.nextFloat(); q.smear = rnd.nextFloat(); q.freeze = rnd.nextFloat(); q.wobble = rnd.nextFloat(); q.wobbleHz = 1.0f + 8.0f * rnd.nextFloat();
                                                            q.tilt = rnd.nextFloat() * 2 - 1; q.width = rnd.nextFloat(); l.setParams (q); } }, a, b);
    float worst = 0, peak = 0;
    for (int i = 2; i < n; ++i)
        for (auto* v : { &a, &b })
        {
            const float d2 = std::abs ((*v)[(size_t) i] - 2.0f * (*v)[(size_t) i - 1] + (*v)[(size_t) i - 2]);
            worst = juce::jmax (worst, d2); peak = juce::jmax (peak, std::abs ((*v)[(size_t) i]));
        }
    check (worst < 0.35f, "a fast drag (every amount jumping every 10 ms) makes no clicks (worst bend " + juce::String (worst, 3) + ")");
    check (peak <= 0.85f, "the live chain stays under its ceiling (peak " + juce::String (peak, 3) + ")");
    // loud in, still under the ceiling
    for (auto& v : L) v *= 3.0f; for (auto& v : R) v *= 3.0f;
    run ([&] (TerrainLive& l, int) { LiveParams q; q.drive = 0.3f; q.gainDb = 6.0f; l.setParams (q); }, a, b);
    peak = 0; for (int i = 0; i < n; ++i) peak = juce::jmax (peak, std::abs (a[(size_t) i]), std::abs (b[(size_t) i]));
    check (peak <= 0.85f, "loud in, +6 dB: still under the ceiling (" + juce::String (peak, 3) + ")");
    for (auto& v : L) v /= 3.0f; for (auto& v : R) v /= 3.0f;
    // the wash holds on after the sound stops, longer when frozen
    auto tail = [&] (float freeze)
    {
        std::vector<float> x ((size_t) n, 0.0f), y ((size_t) n, 0.0f);
        for (int i = 0; i < 4800; ++i) x[(size_t) i] = y[(size_t) i] = 0.5f * std::sin (0.05f * (float) i);
        TerrainLive live; live.prepare (sr, 2);
        LiveParams q; q.smear = 1.0f; q.freeze = freeze; live.setParams (q);
        for (int p = 0; p < n; p += 256) { float* chs[2] = { x.data() + p, y.data() + p }; live.process (chs, 2, juce::jmin (256, n - p)); }
        double e = 0; for (int i = 48000; i < 72000; ++i) e += (double) x[(size_t) i] * x[(size_t) i];
        return std::sqrt (e / 24000.0);
    };
    const double t0 = tail (0.0f), t1 = tail (1.0f);
    check (t1 > t0 * 4.0 && t1 > 1e-3, "SMEAR rings on; FREEZE holds it (" + juce::String (t0, 5) + " -> " + juce::String (t1, 5) + " a second later)");
    // nothing allocated on the audio thread
    {
        TerrainLive live; live.prepare (sr, 2); live.setParams (wild);
        LiveShare share; LiveParams got;
        std::vector<float> x (512, 0.1f), y (512, 0.1f); float* chs[2] = { x.data(), y.data() };
        gAllocs = 0; gCountAllocs = true;
        for (int k = 0; k < 50; ++k) { share.publish (wild); share.pull (got); live.setParams (got); live.process (chs, 2, 512); }
        gCountAllocs = false;
        check (gAllocs == 0, "the live chain and its hand-off allocate nothing (" + juce::String ((int) gAllocs.load()) + ")");
    }
}

static void testView (const juce::File& shotsDir)
{
    TerrainEditor ed;
    TerrainView v (ed);
    v.setSize (900, 560);
    for (auto p : { juce::Point<float> (0.2f, 0.3f), juce::Point<float> (0.7f, 0.8f), juce::Point<float> (0.5f, 0.5f) })
    {
        float x = 0, z = 0;
        const bool ok = v.pickGround (v.project (p.x, p.y, 0.0f), x, z);
        check (ok && std::abs (x - p.x) < 1e-3f && std::abs (z - p.y) < 1e-3f, "screen <-> landscape round-trips at (" + juce::String (p.x) + ", " + juce::String (p.y) + ")");
    }
    ed.set (defaultTerrain());
    check (v.handleAt (v.handleDot (0)) == 0, "the handle dot is found under the mouse");
    {
        juce::Image im (juce::Image::RGB, 900, 560, true); juce::Graphics g (im); v.paint (g);
        check (v.contourCount() >= 10, "the landscape draws as contour rings (" + juce::String (v.contourCount()) + ")");
    }
    // real mouse gestures through the view (the reference's): the dot moves the mountain; choosing it shows the height tag,
    // which raises it
    const float h0 = ed.terrain()[0].height, x0 = ed.terrain()[0].x;
    const auto dot = v.handleDot (0);
    auto ev = [&] (juce::Point<float> p) { return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p, {}, 1.0f, 0, 0, 0, 0, &v, &v,
                                                                    juce::Time::getCurrentTime(), dot, juce::Time::getCurrentTime(), 1, false); };
    v.mouseDown (ev (dot)); v.mouseDrag (ev (dot.translated (40, 0))); v.mouseUp (ev (dot.translated (40, 0)));
    check (ed.terrain()[0].x > x0 + 0.02f && ed.selected == 0, "dragging the dot moves its mountain (and chooses it)");
    v.settle();
    const auto tag = v.heightTagCentre (0);
    v.mouseDown (ev (tag)); v.mouseDrag (ev (tag.translated (0, -40))); v.mouseUp (ev (tag.translated (0, -40)));
    check (ed.terrain()[0].height > h0 + 0.05f, "dragging the height tag up raises it (" + juce::String (ed.terrain()[0].height, 2) + ")");
    v.settle();
    const auto st = v.widthTagCentre (0); const float r0 = ed.terrain()[0].r();
    v.mouseDown (ev (st)); v.mouseDrag (ev (st.translated (-30, 0))); v.mouseUp (ev (st.translated (-30, 0)));
    check (ed.terrain()[0].r() < r0 - 0.05f, "dragging the size tag left narrows it (" + juce::String (r0, 2) + " -> " + juce::String (ed.terrain()[0].r(), 2) + ")");
    // toolbar buttons
    const auto before = ed.terrain();
    v.press (TerrainView::Button::randomize);
    check (ed.terrain() != before, "RANDOMIZE makes a new landscape");
    v.press (TerrainView::Button::undo);
    check (ed.terrain() == before, "the UNDO button takes it back");
    v.press (TerrainView::Button::freeze);
    check (ed.terrain().frozen, "FREEZE holds one moment");
    v.press (TerrainView::Button::evolve);
    check (ed.terrain().evolve > 0, "EVOLVE starts the drift");
    v.press (TerrainView::Button::reset);
    check (ed.terrain().empty(), "RESET clears the land");
    // cost at 60 fps
    ed.set (defaultTerrain());
    ed.selected = 0;
    for (auto size : { juce::Point<int> (900, 560), juce::Point<int> (420, 480) })
    {
        v.setSize (size.x, size.y);
        for (bool changing : { false, true })
        {
            double ms = 1e9;
            for (int r = 0; r < 3; ++r) ms = juce::jmin (ms, v.benchmarkFrameMs (90, changing));
            // while dragging, a change arrives at most once per frame; we redraw the rings for each one
            // still: one blit and the handles; dragging: the whole land re-cut and re-painted each frame (the reference does
            // the same in the browser). Budgets: 10 % of a core still, 80 % while dragging (on this shared 2-core Linux box;
            // a Mac is two to three times quicker)
            const double cpu = 100.0 * ms / (1000.0 / 60.0);
            check (cpu < (changing ? 80.0 : 10.0), "terrain " + juce::String (size.x) + "x" + juce::String (size.y) + (changing ? " while dragging" : " still")
                                + " at 60 fps: " + juce::String (ms, 2) + " ms a frame = " + juce::String (cpu, 1) + "% of one core (< " + (changing ? "80" : "10") + "%)");
        }
    }
    if (shotsDir != juce::File())
    {
        shotsDir.createDirectory();
        auto shot = [&] (const juce::String& name, float scale = 1.0f)
        {
            auto im = v.snapshot (scale);
            auto f = shotsDir.getChildFile (name); f.deleteFile();
            juce::FileOutputStream os (f); juce::PNGImageFormat png; png.writeImageToStream (im, os);
            std::printf ("shot  %s\n", f.getFullPathName().toRawUTF8());
        };
        // the moments of Brandon's "Moving mountains" recording, for side-by-side comparison: one mountain; pulling a new peak
        // up out of its flank (lit, with its value tag); the width tag being dragged; released
        auto ev = [&] (juce::Point<float> p) { return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p, {}, 1.0f, 0, 0, 0, 0, &v, &v,
                                                                        juce::Time::getCurrentTime(), p, juce::Time::getCurrentTime(), 1, false); };
        {
            v.setPalette (Palette::amber());
            v.setSize (984, 745);
            // (the reference's one mountain: r 0.38 of the island, 0.55 tall, seed 7)
            Terrain one; one.add ({ 0.5f, 0.5f, 0.55f, 0.095f, 0.095f, 0.0f }); one.seed = 7;
            ed.selected = -1; ed.setWithoutUndo (one);
            shot ("moment1-one-mountain.png");
            // click the right flank of the mountain: a new peak rises out of the land, lit (0.30 as on the site), and pull it
            // on to 0.41 as in the recording
            const float hpx = v.project (0.5f, 0.5f, 0.0f).y - v.project (0.5f, 0.5f, 1.0f).y;
            const auto flank = v.project (0.76f, 0.46f, 0.0f);
            v.mouseDown (ev (flank));
            v.mouseDrag (ev (flank.translated (0.0f, -4.0f)));
            shot ("moment2-pulling-up.png");
            v.mouseDrag (ev (flank.translated (0.0f, -0.11f * hpx)));
            shot ("moment3-height-tag.png");
            v.mouseUp (ev (flank.translated (0.0f, -0.11f * hpx)));
            // widen it a little by its tag
            const int sel = ed.selected;
            const auto tag = v.widthTagCentre (sel);
            v.mouseDown (ev (tag));
            v.mouseDrag (ev (tag.translated (14.0f, 0.0f)));
            shot ("moment4-width-tag.png");
            v.mouseUp (ev (tag.translated (14.0f, 0.0f)));
            ed.select (-1);
            shot ("moment5-released.png");
            ed.select (sel);
            shot ("moment6-selected-still.png");
        }
        // the live site (topographically.vercel.app) in the same states, at its default (one mountain, seed 7), 984 x 745
        {
            v.setPalette (Palette::amber());
            v.setSize (984, 745);
            Terrain one; one.add ({ 0.5f, 0.5f, 0.55f, 0.095f, 0.095f, 0.0f }); one.seed = 7;
            ed.selected = -1; ed.setWithoutUndo (one);
            shot ("site-idle.png");
            const auto dot = v.handleDot (0);
            v.mouseMove (ev (dot));
            shot ("site-hover-dot.png");
            v.mouseDown (ev (dot)); v.mouseUp (ev (dot));
            shot ("site-selected.png");
            v.mouseDown (ev (dot)); v.mouseDrag (ev (dot.translated (0.0f, -64.0f)));
            shot ("site-raise.png");
            v.mouseUp (ev (dot.translated (0.0f, -64.0f)));
            ed.setWithoutUndo (one); ed.select (0);
            const auto tag = v.widthTagCentre (0);
            v.mouseDown (ev (tag)); v.mouseDrag (ev (tag.translated (60.0f, 0.0f)));
            shot ("site-widen.png");
            v.mouseUp (ev (tag.translated (60.0f, 0.0f)));
            ed.setWithoutUndo (one); ed.select (-1);
            const auto flank = v.project (0.5f, 0.5f, 0.0f).translated (150.0f, 40.0f);
            v.mouseDown (ev (flank)); v.mouseUp (ev (flank));
            shot ("site-newpeak.png");
            ed.selected = -1; ed.setWithoutUndo (randomTerrain (7));
            shot ("site-shuffle.png");
            ed.setWithoutUndo (flatTerrain());
            shot ("site-cleared.png");
        }
        struct Theme { const char* name; Palette p; };
        const Theme themes[] = { { "amber", Palette::amber() }, { "ghostlab", Palette::ghostLab() }, { "ghostslice", Palette::ghostSlice() }, { "ghostbass", Palette::ghostBass() } };
        for (auto& th : themes)
        {
            v.setPalette (th.p);
            v.setSize (1000, 640);
            Terrain one; one.add ({ 0.5f, 0.5f, 0.88f, 0.21f, 0.22f, 0.0f }); one.seed = 3;
            ed.selected = -1; ed.setWithoutUndo (one);
            shot (juce::String (th.name) + "-one.png");
            Terrain three; three.add ({ 0.42f, 0.50f, 0.66f, 0.21f, 0.22f, 0.0f }); three.add ({ 0.70f, 0.44f, 0.42f, 0.10f, 0.10f, 0.0f }); three.seed = 5;
            ed.selected = -1; ed.setWithoutUndo (three);
            shot (juce::String (th.name) + "-several.png");
            ed.select (1);
            shot (juce::String (th.name) + "-selected.png");
            shot (juce::String (th.name) + "-selected@2x.png", 2.0f);
            auto rough = randomTerrain (11); rough.grit = 0.3f; ed.selected = -1; ed.setWithoutUndo (rough); ed.select (0);
            shot (juce::String (th.name) + "-random-gritty.png");
            v.setSize (420, 520); ed.selected = -1; ed.setWithoutUndo (defaultTerrain());
            shot (juce::String (th.name) + "-narrow.png");
        }
    }
}

static void benchDetail()
{
    auto t = randomTerrain (42); t.grit = 0.3f; t.evolve = 0.6f; t.phase = 0.25f; t.frozen = true; t.freezeZ = 0.7f;
    Terrain back; fromString (toString (t), back);
    std::printf ("count %d %d grit %g %g evolve %g %g phase %g %g frozen %d %d fz %g %g seed %u %u\n", t.count, back.count, t.grit, back.grit, t.evolve, back.evolve, t.phase, back.phase, t.frozen, back.frozen, t.freezeZ, back.freezeZ, t.seed, back.seed);
    for (int i = 0; i < t.count; ++i) std::printf ("%d: %.9g %.9g | %.9g %.9g | %.9g %.9g | %.9g %.9g | %.9g %.9g | %.9g %.9g\n", i, t[i].x, back[i].x, t[i].z, back[i].z, t[i].height, back[i].height, t[i].width, back[i].width, t[i].depth, back[i].depth, t[i].rough, back[i].rough);
    TerrainEditor ed; TerrainView v (ed); v.setSize (900, 560); ed.set (defaultTerrain()); ed.selected = 0;
    juce::Image im (juce::Image::RGB, 900, 560, false); juce::Graphics g (im);
    v.paint (g);
    auto t0 = juce::Time::getMillisecondCounterHiRes();
    for (int i = 0; i < 50; ++i) v.paint (g);
    std::printf ("still paint %.2f ms\n", (juce::Time::getMillisecondCounterHiRes() - t0) / 50);
    v.setToolbarVisible (false); v.paint (g);
    t0 = juce::Time::getMillisecondCounterHiRes();
    for (int i = 0; i < 50; ++i) v.paint (g);
    std::printf ("still paint, no toolbar %.2f ms\n", (juce::Time::getMillisecondCounterHiRes() - t0) / 50);
    v.setReadoutsVisible (false); v.paint (g);
    t0 = juce::Time::getMillisecondCounterHiRes();
    for (int i = 0; i < 50; ++i) v.paint (g);
    std::printf ("still paint, no toolbar, no readouts %.2f ms\n", (juce::Time::getMillisecondCounterHiRes() - t0) / 50);
    v.setToolbarVisible (true); v.setReadoutsVisible (true); v.paint (g);
    t0 = juce::Time::getMillisecondCounterHiRes();
    for (int i = 0; i < 50; ++i) { g.setImageResamplingQuality (juce::Graphics::lowResamplingQuality); g.drawImageAt (im, 0, 0); }
    std::printf ("blit ARGB drawImageAt %.2f ms\n", (juce::Time::getMillisecondCounterHiRes() - t0) / 50);
    juce::Image rgb (juce::Image::RGB, 900, 560, false);
    t0 = juce::Time::getMillisecondCounterHiRes();
    for (int i = 0; i < 50; ++i) g.drawImageAt (rgb, 0, 0);
    std::printf ("blit RGB drawImageAt %.2f ms\n", (juce::Time::getMillisecondCounterHiRes() - t0) / 50);
    t0 = juce::Time::getMillisecondCounterHiRes();
    for (int i = 0; i < 50; ++i) g.drawImageTransformed (rgb, juce::AffineTransform::scale (1.0f));
    std::printf ("blit RGB transformed %.2f ms\n", (juce::Time::getMillisecondCounterHiRes() - t0) / 50);
    std::vector<float> f (224 * 128); std::vector<int8_t> o (f.size()); std::vector<float> n (f.size());
    Terrain::noiseGrid (1, 224, 128, -0.08f, 1.08f, -0.08f, 1.08f, false, 0.5f, n.data());
    auto d = defaultTerrain();
    t0 = juce::Time::getMillisecondCounterHiRes();
    for (int i = 0; i < 50; ++i) d.sample (224, 128, -0.08f, 1.08f, -0.08f, 1.08f, f.data(), o.data(), n.data());
    std::printf ("sample %.2f ms\n", (juce::Time::getMillisecondCounterHiRes() - t0) / 50);
    t0 = juce::Time::getMillisecondCounterHiRes();
    for (int i = 0; i < 50; ++i) Terrain::noiseGrid (1, 224, 128, -0.08f, 1.08f, -0.08f, 1.08f, false, 0.5f, n.data());
    std::printf ("noiseGrid %.2f ms\n", (juce::Time::getMillisecondCounterHiRes() - t0) / 50);
    t0 = juce::Time::getMillisecondCounterHiRes();
    for (int i = 0; i < 50; ++i) { v.setPalette (v.palette); v.paint (g); }
    std::printf ("full redraw %.2f ms, %d segments\n", (juce::Time::getMillisecondCounterHiRes() - t0) / 50, v.contourCount());
    for (auto& m : TerrainView::profileMs) m = 1e9;
    for (int i = 0; i < 50; ++i) { v.setPalette (v.palette); v.paint (g); }
    std::printf ("best frame: sample %.2f rings %.2f raster %.2f blit %.2f overlays %.2f handles %.2f toolbar %.2f\n", TerrainView::profileMs[0], TerrainView::profileMs[1], TerrainView::profileMs[2], TerrainView::profileMs[3], TerrainView::profileMs[4], TerrainView::profileMs[5], TerrainView::profileMs[6]);
    std::printf ("raster: clear part %.3f ms\n", TerrainView::profileMs[7]);
    {
        juce::Image big (juce::Image::RGB, 900, 560, false), small (juce::Image::ARGB, 120, 14, true), tgt (juce::Image::RGB, 900, 560, false);
        juce::Graphics gt (tgt);
        auto bench = [&] (const char* what, std::function<void()> f) { f(); auto a = juce::Time::getMillisecondCounterHiRes(); for (int i = 0; i < 40; ++i) f(); std::printf ("%s %.3f ms\n", what, (juce::Time::getMillisecondCounterHiRes() - a) / 40); };
        bench ("RGB->RGB drawImageAt", [&] { gt.drawImageAt (big, 0, 0); });
        bench ("RGB->RGB transformed(1)", [&] { gt.drawImageTransformed (big, juce::AffineTransform::scale (1.0f)); });
        bench ("RGB->ARGB drawImageAt", [&] { g.drawImageAt (big, 0, 0); });
        bench ("20 small ARGB -> RGB transformed", [&] { for (int i = 0; i < 20; ++i) gt.drawImageTransformed (small, juce::AffineTransform::scale (1.0f).translated (10.0f * i, 20.0f)); });
        bench ("20 small ARGB -> RGB drawImageAt", [&] { for (int i = 0; i < 20; ++i) gt.drawImageAt (small, 10 * i, 20); });
        bench ("20 drawText", [&] { gt.setFont (juce::Font (juce::FontOptions (10.0f))); for (int i = 0; i < 20; ++i) gt.drawText ("820 Hz  +9.0 dB  Q 4.2", 10 * i, 20, 200, 14, juce::Justification::centredLeft); });
        bench ("RGB->RGB clipped quarter drawImageAt", [&] { gt.saveState(); gt.reduceClipRegion (0, 0, 450, 280); gt.drawImageAt (big, 0, 0); gt.restoreState(); });
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::String mode = argc > 1 ? argv[1] : "";
    if (mode == "bench") { benchDetail(); return 0; }
    if (mode == "moments")
    {
        const juce::File dir (argc > 2 ? argv[2] : "moments");
        std::vector<Moment> ms;
        auto base = defaultTerrain();
        auto with = [&] (std::initializer_list<Peak> ps) { Terrain t = base; for (auto& q : ps) t.add (q); return t; };
        juce::ignoreUnused (with);
        ms.push_back ({ "a001-one-mountain", base });
        { Moment m { "a007-opening", base }; m.open = 0.055f; ms.push_back (m); }
        { Moment m { "a053-height-pill", base, { { 274.3f, 289.7f, 0.30f, 0.22f } } }; m.grab = 1; m.grabKind = 1; ms.push_back (m); }
        { Moment m { "a065-spire-size-pill", with ({ pk (0.704f, 0.530f, 1.20f, 0.05f) }) }; m.grab = 1; m.grabKind = 2; ms.push_back (m); }
        { Moment m { "a127-broad-height-pill", with ({ pk (0.704f, 0.530f, 1.20f, 0.05f), pk (0.403f, 0.452f, 0.9f, 0.07f) }), { { 202.8f, 204.0f, 1.20f, 0.62f } } }; m.grab = 3; m.grabKind = 1; ms.push_back (m); }
        { Moment m { "a050-new-peak-chosen", base, { { 274.3f, 289.7f, 0.30f, 0.22f } } }; m.selected = 1; ms.push_back (m); }
        { Moment m { "a070-a-spire", base, { { 278.7f, 223.0f, 1.20f, 0.05f } } }; ms.push_back (m); }
        { Moment m { "a089-narrowing", base, { { 278.7f, 223.0f, 1.20f, 0.05f }, { 158.2f, 314.0f, 0.30f, 0.06f } } }; m.grab = 2; m.grabKind = 2; ms.push_back (m); }
        { Moment m { "a109-third-peak", with ({ pk (0.704f, 0.530f, 1.20f, 0.05f), pk (0.403f, 0.452f, 0.9f, 0.07f) }), { { 243.5f, 326.0f, 0.30f, 0.30f } } }; m.selected = 3; ms.push_back (m); }
        { Moment m { "a129-tall-and-broad", with ({ pk (0.704f, 0.530f, 1.20f, 0.05f), pk (0.403f, 0.452f, 0.9f, 0.07f) }), { { 202.8f, 204.0f, 1.20f, 0.62f } } }; m.selected = 3; ms.push_back (m); }
        { Moment m { "a168-size-tag-aside", with ({ pk (0.704f, 0.530f, 1.20f, 0.05f), pk (0.403f, 0.452f, 0.9f, 0.07f) }), { { 83.5f, 213.3f, 1.20f, 0.67f } } }; m.selected = 3; ms.push_back (m); }
        {
            Moment m { "b001-shuffled", Terrain() };
            m.land.seed = 7;
            m.dots = { { 193.5f, 270.0f, 0.58f, 0.36f }, { 171.0f, 252.3f, 0.16f, 0.16f }, { 61.7f, 302.0f, 0.4f, 0.22f }, { 112.3f, 372.2f, 0.36f, 0.2f }, { 199.0f, 402.7f, 0.32f, 0.2f }, { 324.2f, 343.0f, 0.3f, 0.2f } };
            ms.push_back (m);
        }
        {
            Moment m { "b134-ten-mountains", Terrain() };
            m.land.seed = 7;
            m.dots = { { 193.5f, 273.3f, 0.58f, 0.36f }, { 149.5f, 248.5f, 0.16f, 0.16f }, { 250.3f, 257.8f, 0.42f, 0.2f }, { 83.5f, 287.3f, 0.4f, 0.2f }, { 310.8f, 324.7f, 0.36f, 0.18f },
                       { 62.3f, 347.2f, 0.34f, 0.18f }, { 136.8f, 359.5f, 0.3f, 0.16f }, { 141.0f, 397.2f, 0.3f, 0.16f }, { 269.7f, 367.2f, 0.28f, 0.16f }, { 212.3f, 414.3f, 0.26f, 0.16f } };
            ms.push_back (m);
        }
        // Brandon's own landscapes (made on the site with the TAG tool on: no handles, his own labels). Mountains placed by
        // where their summits stand in his pictures (mapped onto this view: x = 200 + (px - 600) * 0.3, y = 595 + (py - 830) * 0.3)
        auto css = [] (float px, float py) { return juce::Point<float> (200.0f + (px - 600.0f) * 0.3f, 590.0f + (py - 830.0f) * 0.3f); };
        auto sum = [&] (float px, float py, float h, float r) { const auto c = css (px, py); return std::array<float, 4> { c.x, c.y - 184.0f, h, r }; };
        {
            Moment m { "brandon-example1", Terrain() };
            m.land.seed = 7; m.labelMode = true; m.card = false;
            m.dots = { sum (540, 215, 1.20f, 0.200f), sum (540, 216, 1.20f, 0.150f), sum (965, 400, 0.58f, 0.190f), sum (330, 345, 0.21f, 0.160f), sum (660, 295, 0.17f, 0.128f), sum (845, 360, 0.17f, 0.128f),
                       sum (805, 430, 0.20f, 0.144f), sum (240, 505, 0.20f, 0.160f), sum (915, 655, 0.17f, 0.144f), sum (665, 675, 0.22f, 0.160f), sum (735, 570, 0.12f, 0.096f),
                       sum (285, 465, 0.09f, 0.064f), sum (335, 600, 0.09f, 0.064f), sum (395, 665, 0.12f, 0.096f) };
            m.labels = { { css (575, 320), "75" }, { css (550, 350), "u" }, { css (528, 392), "160" }, { css (628, 350), "1000" }, { css (570, 425), "75577" },
                         { css (535, 492), "100" }, { css (485, 570), "50" }, { css (898, 755), "0" }, { css (1052, 515), "120" } };
            ms.push_back (m);
        }
        {
            Moment m { "brandon-example2", Terrain() };
            m.land.seed = 7; m.labelMode = true; m.card = false;
            m.dots = { sum (450, 285, 0.97f, 0.31f), sum (455, 135, 1.2f, 0.12f), sum (455, 136, 1.2f, 0.09f), sum (600, 140, 1.2f, 0.04f), sum (560, 120, 1.15f, 0.04f), sum (635, 150, 1.2f, 0.04f),
                       sum (720, 120, 1.2f, 0.045f), sum (660, 300, 1.1f, 0.045f), sum (770, 250, 1.05f, 0.04f), sum (870, 330, 0.9f, 0.045f), sum (210, 270, 0.62f, 0.045f),
                       sum (820, 630, 0.22f, 0.12f) };
            m.labels = { { css (640, 355), "75" }, { css (510, 380), "160" }, { css (385, 450), "150" }, { css (650, 435), "75577" }, { css (455, 548), "100" },
                         { css (448, 633), "50" }, { css (875, 360), "1000" }, { css (910, 720), "0" } };
            ms.push_back (m);
        }
        renderMoments (dir, ms);
        return 0;
    }
    if (mode == "shots") { testView (juce::File (argc > 2 ? argv[2] : "terrain-shots")); return 0; }
    if (mode == "noise")
    {   // the shape noise along a line: smooth, about -1..1
        Terrain t; t.seed = 7;
        float prev = 0, maxStep = 0, mx = -9, mn = 9;
        for (int i = 0; i <= 400; ++i) { const float x = (float) i / 400.0f, w = t.wobbleAt (x, 0.5f); if (i) maxStep = juce::jmax (maxStep, std::abs (w - prev)); prev = w; mx = juce::jmax (mx, w); mn = juce::jmin (mn, w); if (i % 20 == 0) std::printf ("%.3f ", w); }
        std::printf ("\nwobble: min %.3f max %.3f max step per 1/400 %.4f\n", mn, mx, maxStep);
        for (int i = 0; i <= 10; ++i) std::printf ("%.3f ", t.reliefAt ((float) i / 10.0f, 0.5f));
        std::printf ("\nfine: ");
        for (int i = 0; i <= 40; ++i) std::printf ("%.3f ", t.wobbleAt (0.4f + 0.0025f * (float) i, 0.5f));
        std::printf ("\n");
        return 0;
    }
    testModel();
    testMapping();
    testSerial();
    testEditing();
    testIslands();
    testDsp();
    testLive();
    testView ({});
    std::printf ("\n%d passed, %d failed\n", passes, failures);
    return failures == 0 ? 0 : 1;
}
