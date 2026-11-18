// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
#include "TerrainShaping.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace mosaic
{
using namespace ghost::terrain;

Range terrainRange()
{
    Range r;
    r.minHz = 30.0f; r.maxHz = 16000.0f;
    return r;
}

int bandOfHz (float hz, double sr)
{
    const double half = juce::jmax (1.0, sr * 0.5);
    return juce::jlimit (0, Shaping::kBands - 1, (int) std::floor (hz / half * Shaping::kBands));
}

void bandSpan (int band, double sr, float& lo, float& hi)
{
    const double half = sr * 0.5;
    lo = (float) (half * band / Shaping::kBands);
    hi = (float) (half * (band + 1) / Shaping::kBands);
}

static float median (std::vector<float> v)
{
    if (v.empty()) return 0.0f;
    const size_t m = v.size() / 2;
    std::nth_element (v.begin(), v.begin() + (long) m, v.end());
    return v[m];
}

LandFeatures features (const Terrain& t)
{
    LandFeatures f;
    for (int i = 0; i < t.size(); ++i)
    {
        const auto& p = t[i];
        if (p.height < -0.05f) { ++f.valleys; continue; }
        if (p.height <= 0.05f) continue;
        f.mountains.push_back ({ p.x, p.z, p.height, p.width, p.depth, p.rough });
        f.mass += p.height * p.width * juce::jmin (1.0f, p.depth);
        if (p.height > f.tallest) { f.tallest = p.height; f.tallestX = p.x; }
        f.jagged += p.rough * p.height;
        if (p.height > 0.35f && p.width < 0.035f) { ++f.spires; f.spireAmount += p.height * (0.035f - p.width) / (0.035f - kMinWidth); }
        if (p.height > 0.2f && p.width > 0.14f) { ++f.plateaus; f.plateauAmount += p.height * (p.width - 0.14f) / (kMaxWidth - 0.14f); }
        // (the component's columns: taller than 1 the rings stop and the top goes flat, whatever the width)
        else if (p.height > 1.0f && p.width >= 0.035f) { ++f.plateaus; f.plateauAmount += 2.5f * (p.height - 1.0f); }
    }
    const auto& m = f.mountains;
    for (size_t i = 0; i < m.size(); ++i)
        for (size_t j = i + 1; j < m.size(); ++j)
        {
            // two mountains whose feet overlap run into one: how much they merge (1 = on top of each other)
            const float dx = std::abs (m[i].x - m[j].x) / (2.0f * (m[i].width + m[j].width));
            const float dz = std::abs (m[i].z - m[j].z) / (2.0f * (juce::jmin (1.0f, m[i].depth) + juce::jmin (1.0f, m[j].depth)));
            const float d = std::sqrt (dx * dx + dz * dz);
            if (d < 1.0f) { ++f.merges; f.mergeAmount += (1.0f - d) * juce::jmin (m[i].height, m[j].height); }
        }
    f.spireAmount = juce::jlimit (0.0f, 1.0f, f.spireAmount * 0.6f);
    f.plateauAmount = juce::jlimit (0.0f, 1.0f, f.plateauAmount * 1.2f);
    f.mergeAmount = juce::jlimit (0.0f, 1.0f, f.mergeAmount * 0.8f);
    f.jagged = juce::jlimit (0.0f, 1.0f, f.jagged + t.grit);
    return f;
}

Shaping makeShaping (const Terrain& t, double sr, const std::vector<float>& soundX)
{
    Shaping s;
    if (t.empty() && t.grit <= 0.0f) return s;
    // ---- the whole-landscape moves
    const auto f = features (t);
    s.stutter = f.spireAmount;
    s.smooth = f.plateauAmount * (1.0f - 0.7f * f.spireAmount);
    s.blend = f.mergeAmount;
    s.macrosOn = s.stutter > 0.01f || s.smooth > 0.01f || s.blend > 0.01f;
    if (! s.macrosOn) s.stutter = s.smooth = s.blend = 0;
    // ---- how many sounds are in play, and which
    const int nS = juce::jmin ((int) soundX.size(), Shaping::kMaxSounds);
    const int nM = (int) f.mountains.size();
    if (nS >= 2 && nM >= 1 && nM < 4)
    {
        const int keep = juce::jlimit (1, nS, (int) std::ceil (nS * (0.3f + 0.175f * (float) nM)));
        if (keep < nS)
        {
            std::vector<std::pair<float, int>> d;
            for (int k = 0; k < nS; ++k)
            {
                float best = 2.0f;
                for (auto& mt : f.mountains) best = juce::jmin (best, std::abs (soundX[(size_t) k] - mt.x) / (0.5f + mt.height));
                d.push_back ({ best, k });
            }
            std::stable_sort (d.begin(), d.end());
            for (auto& w : s.mask) w = 0;
            for (int k = 0; k < keep; ++k) s.mask[d[(size_t) k].second >> 5] |= 1u << (d[(size_t) k].second & 31);
            s.maskOn = true; s.inPlay = keep; s.ofSounds = nS;
        }
    }
    constexpr int NX = 96, NZ = Shaping::kSteps;
    std::vector<float> m ((size_t) (NX * NZ)), rough ((size_t) (NX * NZ));
    for (int j = 0; j < NZ; ++j)
    {
        const float z = (float) j / (float) (NZ - 1);
        const float zz = t.effectiveZ (z);
        for (int i = 0; i < NX; ++i)
        {
            const float x = (float) i / (float) (NX - 1);
            // the mountains and valleys only: the island's gentle floor is the page's ground, not a setting
            float h = 0.0f;
            if (! t.empty()) h = t.smoothHeightAt (x, z) - Terrain::floorAt (x, zz, t.reliefAt (x, zz));
            m[(size_t) (j * NX + i)] = h;
            rough[(size_t) (j * NX + i)] = t.roughAt (x, z);
        }
    }
    // ---- seen from the front: the frequency weights (ridges right across all frequencies taken out)
    std::vector<float> F ((size_t) NX, 0.0f);
    {
        std::vector<float> hi ((size_t) NX, 0.0f), lo ((size_t) NX, 0.0f), row ((size_t) NX);
        for (int j = 0; j < NZ; ++j)
        {
            for (int i = 0; i < NX; ++i) row[(size_t) i] = m[(size_t) (j * NX + i)];
            const float med = median (row);
            for (int i = 0; i < NX; ++i)
            {
                const float v = row[(size_t) i] - med;
                hi[(size_t) i] = juce::jmax (hi[(size_t) i], v);
                lo[(size_t) i] = juce::jmin (lo[(size_t) i], v);
            }
        }
        // the stronger of the two (a mountain at one moment and a dip at another don't cancel)
        for (int i = 0; i < NX; ++i) F[(size_t) i] = hi[(size_t) i] >= -lo[(size_t) i] ? hi[(size_t) i] : lo[(size_t) i];
    }
    // ---- where the land stands across, as a brightness preference: raised lows = darker pieces, raised highs =
    // brighter ones (valleys the other way). The weights alone make the lows' LEVEL count; this picks dark pieces.
    {
        double up = 0, upX = 0, dn = 0, dnX = 0; float upMax = 0, dnMax = 0;
        for (int i = 0; i < NX; ++i)
        {
            const float x = (float) i / (float) (NX - 1), v = F[(size_t) i];
            if (v > 0) { up += v; upX += v * x; upMax = juce::jmax (upMax, v); }
            else if (v < 0) { dn -= v; dnX -= v * x; dnMax = juce::jmax (dnMax, -v); }
        }
        float tilt = 0;
        if (up > 0) tilt += juce::jmin (1.0f, upMax) * (float) (upX / up - 0.5) * 2.0f;
        if (dn > 0) tilt -= juce::jmin (1.0f, dnMax) * (float) (dnX / dn - 0.5) * 2.0f;
        tilt = juce::jlimit (-1.0f, 1.0f, tilt * 1.4f);
        s.tilt = std::abs (tilt) > 0.02f ? tilt : 0.0f;
    }

    // ---- seen from the side: the time curve (ridges the whole way front to back taken out), plus roughness
    float wildMax = 0.0f;
    {
        std::vector<float> med ((size_t) NX), col ((size_t) NZ);
        for (int i = 0; i < NX; ++i)
        {
            for (int j = 0; j < NZ; ++j) col[(size_t) j] = m[(size_t) (j * NX + i)];
            med[(size_t) i] = median (col);
        }
        for (int j = 0; j < NZ; ++j)
        {
            float hi = 0.0f, lo = 0.0f, r = 0.0f;
            for (int i = 0; i < NX; ++i)
            {
                const float v = m[(size_t) (j * NX + i)] - med[(size_t) i];
                hi = juce::jmax (hi, v); lo = juce::jmin (lo, v);
                r = juce::jmax (r, rough[(size_t) (j * NX + i)]);
            }
            const float w = juce::jlimit (-1.0f, 1.0f, (hi >= -lo ? hi : lo) + 0.6f * r);
            s.wild[j] = w;
            wildMax = juce::jmax (wildMax, std::abs (w));
        }
    }
    s.timeOn = wildMax > 0.01f;
    if (! s.timeOn) for (auto& w : s.wild) w = 0.0f;

    // ---- the weights per engine band: the land over the band's own frequency span
    const auto range = terrainRange();
    float vmax = 0.0f;
    float v[Shaping::kBands];
    for (int b = 0; b < Shaping::kBands; ++b)
    {
        float lo, hi; bandSpan (b, sr, lo, hi);
        const float x0 = hzToX (juce::jmax (lo, range.minHz), range), x1 = hzToX (juce::jmax (hi, range.minHz), range);
        int i0 = (int) std::floor (x0 * (NX - 1)), i1 = (int) std::ceil (x1 * (NX - 1));
        i0 = juce::jlimit (0, NX - 1, i0); i1 = juce::jlimit (i0, NX - 1, i1);
        float up = 0.0f, down = 0.0f;
        for (int i = i0; i <= i1; ++i) { up = juce::jmax (up, F[(size_t) i]); down = juce::jmin (down, F[(size_t) i]); }
        v[b] = juce::jlimit (-1.3f, 1.3f, up >= -down ? up : down);
        vmax = juce::jmax (vmax, std::abs (v[b]));
    }
    s.weightsOn = vmax > 0.01f;
    if (s.weightsOn)
    {
        // the distance sums squared POWER differences (4th power of level), so "counts as if 18 dB louder" is 8^4 = 4096x
        const float k = 4.0f * std::log (8.0f);
        double sum = 0;
        for (int b = 0; b < Shaping::kBands; ++b) { s.weights[b] = std::exp (k * v[b]); sum += s.weights[b]; }
        const float norm = (float) (Shaping::kBands / sum);
        for (auto& w : s.weights) w *= norm;
    }
    // ---- build 15: the land also moves the search's own TONE / TIMBRE, SHAPE / PITCH ONLY and SEARCH STYLE
    s.tone = s.weightsOn ? juce::jlimit (0.0f, 1.0f, vmax) : 0.0f;
    s.timbre = f.jagged;
    s.pitch = juce::jlimit (0.0f, 1.0f, 0.7f * f.plateauAmount + 0.5f * f.mergeAmount);
    s.net = f.plateauAmount;
    // the land's character, and what stands in the lows / mids / highs at each moment of the target (the shape under the playhead)
    float mn = 0, mx = 0;
    for (float h : m) { mn = juce::jmin (mn, h); mx = juce::jmax (mx, h); }
    s.valley = juce::jlimit (0.0f, 1.0f, -mn);
    s.tall = juce::jlimit (0.0f, 1.0f, mx);
    s.energy = juce::jlimit (0.0f, 1.0f, f.mass * 1.5f + f.tallest * 0.5f + s.valley * 0.5f + f.jagged * 0.3f);
    {
        float zmax = 0;
        for (int j = 0; j < NZ; ++j)
        {
            float hi[3] = { 0, 0, 0 }, lo[3] = { 0, 0, 0 };
            for (int i = 0; i < NX; ++i)
            {
                const int z = i * 3 / NX;
                const float v = m[(size_t) (j * NX + i)];
                hi[z] = juce::jmax (hi[z], v); lo[z] = juce::jmin (lo[z], v);
            }
            float* out[3] = { &s.zl[j], &s.zm[j], &s.zh[j] };
            for (int z = 0; z < 3; ++z) { *out[z] = juce::jlimit (-1.0f, 1.0f, hi[z] >= -lo[z] ? hi[z] : lo[z]); zmax = juce::jmax (zmax, std::abs (*out[z])); }
        }
        s.zonesOn = zmax > 0.02f;
        if (! s.zonesOn) { for (auto& w : s.zl) w = 0; for (auto& w : s.zm) w = 0; for (auto& w : s.zh) w = 0; }
    }
    s.searchOn = s.tone > 0.01f || s.timbre > 0.01f || s.pitch > 0.01f || s.net > 0.01f || s.energy > 0.01f;
    if (! s.searchOn) { s.tone = s.timbre = s.pitch = s.net = 0; s.energy = s.valley = s.tall = 0; }
    return s;
}

ShapingSummary summarise (const Shaping& s, double sr)
{
    ShapingSummary r;
    r.weights = s.weightsOn; r.time = s.timeOn; r.tilt = s.tilt;
    if (s.weightsOn)
    {
        int top = 0, low = 0;
        for (int b = 0; b < Shaping::kBands; ++b)
        {
            if (s.weights[b] > s.weights[top]) top = b;
            if (s.weights[b] < s.weights[low]) low = b;
        }
        float lo, hi;
        bandSpan (top, sr, lo, hi); r.topHz = 0.5f * (lo + hi); r.topWeight = s.weights[top];
        bandSpan (low, sr, lo, hi); r.lowHz = 0.5f * (lo + hi); r.lowWeight = s.weights[low];
    }
    if (s.timeOn)
    {
        int hiI = 0, loI = 0;
        for (int j = 0; j < Shaping::kSteps; ++j)
        {
            if (s.wild[j] > s.wild[hiI]) hiI = j;
            if (s.wild[j] < s.wild[loI]) loI = j;
        }
        r.wildPos = (float) hiI / (Shaping::kSteps - 1); r.wildMax = s.wild[hiI];
        r.calmPos = (float) loI / (Shaping::kSteps - 1); r.calmMin = s.wild[loI];
    }
    return r;
}

std::vector<Push> pushes (const Shaping& s)
{
    std::vector<Push> v;
    if (s.weightsOn)
    {
        int top = 0, low = 0;
        for (int b = 0; b < Shaping::kBands; ++b) { if (s.weights[b] > s.weights[top]) top = b; if (s.weights[b] < s.weights[low]) low = b; }
        const float up = std::log (s.weights[top] / juce::jmax (1e-9f, s.weights[low])) / (8.0f * std::log (8.0f));
        v.push_back ({ top < 2 ? "MATCH ON THE LOWS" : top < 10 ? "MATCH ON THE MIDS" : "MATCH ON THE HIGHS", juce::jlimit (0.0f, 1.0f, up) });
    }
    if (s.timeOn)
    {
        float hi = 0, lo = 0;
        for (auto w : s.wild) { hi = juce::jmax (hi, w); lo = juce::jmin (lo, w); }
        if (hi > 0.01f) v.push_back ({ "WILD (NOVELTY + BOREDOM)", hi });
        if (lo < -0.01f) v.push_back ({ "STICK TO THE TARGET", -lo });
    }
    if (s.tilt < -0.02f) v.push_back ({ "DARKER PIECES", -s.tilt });
    if (s.tilt > 0.02f) v.push_back ({ "BRIGHTER PIECES", s.tilt });
    if (s.stutter > 0.01f) v.push_back ({ "STUTTER (REPEAT)", s.stutter });
    if (s.smooth > 0.01f) v.push_back ({ "SMOOTH RUNS", s.smooth });
    if (s.blend > 0.01f) v.push_back ({ "BLEND (ORIGINAL MIX)", s.blend });
    if (s.maskOn) v.push_back ({ "SOUNDS IN PLAY " + juce::String (s.inPlay) + " / " + juce::String (s.ofSounds), 1.0f - (float) s.inPlay / (float) juce::jmax (1, s.ofSounds) });
    std::stable_sort (v.begin(), v.end(), [] (const Push& a, const Push& b) { return a.amount > b.amount; });
    return v;
}

DragFeel dragFeel (const Terrain& a, const Terrain& b, int i, float dt)
{
    DragFeel f;
    if (i < 0 || i >= a.size() || i >= b.size() || dt <= 0.0f) return f;
    const auto& p = a[i]; const auto& q = b[i];
    const float dx = q.x - p.x, dz = q.z - p.z, dh = q.height - p.height, dw = q.width - p.width;
    const float speed = std::sqrt (dx * dx + dz * dz + dh * dh + 4.0f * dw * dw) / dt;   // land units per second
    f.burst = juce::jlimit (0.0f, 1.0f, (speed - 0.05f) / 2.5f);
    f.side = juce::jlimit (-1.0f, 1.0f, (dx + 2.0f * dw) / dt / 1.5f);
    f.lift = juce::jlimit (-1.0f, 1.0f, dh / dt / 2.5f);
    return f;
}

// ---- SHAPES
static Peak pk (float x, float z, float h, float w, float d, float r = 0) { return { x, z, h, w, d, r }; }
static Terrain ghostShape()
{
    // a ghost lying on the land: a broad head that goes wild (BOREDOM up) in the middle of the target, two eye
    // valleys where it sticks close, and a wavy hem of little spires at the end that stutter
    Terrain t;
    t.add (pk (0.50f, 0.42f, 0.75f, 0.20f, 0.20f, 0.15f));
    t.add (pk (0.40f, 0.36f, -0.6f, 0.035f, 0.05f));
    t.add (pk (0.60f, 0.36f, -0.6f, 0.035f, 0.05f));
    for (int i = 0; i < 4; ++i) t.add (pk (0.32f + 0.12f * (float) i, 0.80f, 0.55f, 0.022f, 0.06f, 0.2f));
    return t;
}
static Terrain spireForest()
{
    Terrain t;
    const float xs[] = { 0.18f, 0.33f, 0.47f, 0.61f, 0.74f, 0.86f, 0.55f, 0.27f };
    const float zs[] = { 0.25f, 0.62f, 0.30f, 0.75f, 0.45f, 0.20f, 0.88f, 0.90f };
    for (int i = 0; i < 8; ++i) t.add (pk (xs[i], zs[i], 0.6f + 0.05f * (float) (i % 3), 0.016f + 0.002f * (float) (i % 4), 0.08f, 0.1f));
    return t;
}
static Terrain volcano()
{
    Terrain t;
    t.add (pk (0.35f, 0.5f, 1.0f, 0.16f, 0.30f, 0.35f));
    t.add (pk (0.35f, 0.5f, -0.7f, 0.035f, 0.06f));
    t.grit = 0.2f;
    return t;
}
static Terrain plateau()
{
    Terrain t;
    t.add (pk (0.45f, 0.5f, 0.55f, kMaxWidth, kMaxDepth));
    return t;
}
static Terrain archipelago()
{
    Terrain t;
    const float xs[] = { 0.08f, 0.30f, 0.52f, 0.78f, 0.93f, 0.66f };
    const float zs[] = { 0.20f, 0.70f, 0.35f, 0.15f, 0.80f, 0.92f };
    for (int i = 0; i < 6; ++i) t.add (pk (xs[i], zs[i], 0.35f + 0.08f * (float) (i % 3), 0.04f, 0.06f));
    return t;
}
static Terrain twinPeaks()
{
    Terrain t;
    t.add (pk (0.44f, 0.5f, 0.9f, 0.06f, 0.4f));
    t.add (pk (0.53f, 0.52f, 0.85f, 0.06f, 0.4f));
    return t;
}
static Terrain bassHunter()
{
    Terrain t;
    t.add (pk (0.12f, 0.5f, 1.0f, 0.06f, kMaxDepth));
    t.add (pk (0.80f, 0.5f, -0.8f, 0.12f, kMaxDepth));
    return t;
}
static Terrain hatHunter()
{
    Terrain t;
    t.add (pk (0.86f, 0.5f, 1.0f, 0.07f, kMaxDepth));
    t.add (pk (0.15f, 0.5f, -0.8f, 0.10f, kMaxDepth));
    return t;
}
static Terrain slowBurn()
{
    // calm at the start, wilder and wilder towards the end of the target
    Terrain t;
    t.add (pk (0.5f, 0.12f, -0.7f, kMaxWidth, 0.08f));
    t.add (pk (0.5f, 0.62f, 0.45f, kMaxWidth, 0.10f));
    t.add (pk (0.5f, 0.92f, 1.0f, kMaxWidth, 0.10f, 0.4f));
    return t;
}

const std::vector<ShapePreset>& shapePresets()
{
    static const std::vector<ShapePreset> p = {
        { "GHOST", "A ghost on the land: wild head, eyes that stick close, a stuttering hem.", &ghostShape },
        { "SPIRE FOREST", "Thin tall spires everywhere: short stuttering repeats, wild moments here and there.", &spireForest },
        { "VOLCANO", "One rough mountain with a crater: wild, gritty, focused sounds.", &volcano },
        { "PLATEAU", "A wide raised table: long smooth runs of the same sound.", &plateau },
        { "ARCHIPELAGO", "Small islands scattered across: many bands count, every sound in play.", &archipelago },
        { "TWIN PEAKS", "Two mountains merging: matched in the mids, sounds blur together.", &twinPeaks },
        { "BASS HUNTER", "Match on the lows only (kick, bass); the highs hardly count.", &bassHunter },
        { "HAT HUNTER", "Match on the highs only (hats, hiss); the lows hardly count.", &hatHunter },
        { "SLOW BURN", "Close to the target at the start, wilder and wilder to the end.", &slowBurn },
    };
    return p;
}

juce::String describe (const ShapingSummary& s)
{
    if (! s.weights && ! s.time && s.tilt == 0.0f) return "Flat land: the matching is exactly as the knobs say.";
    juce::StringArray parts;
    if (s.weights)
    {
        // shown as level: a weight w counts like a band 5 log10 (w) dB louder (power squared)
        auto db = [] (float w) { return 5.0f * std::log10 (juce::jmax (1e-9f, w)); };
        juce::String m = "MATCH ON " + describeHz (s.topHz) + " " + juce::String (db (s.topWeight), 1) + " dB";
        if (s.lowWeight < 0.9f) m << ", " << describeHz (s.lowHz) << " " << juce::String (db (s.lowWeight), 1) << " dB";
        parts.add (m);
    }
    if (s.tilt != 0.0f) parts.add (juce::String (s.tilt < 0 ? "DARKER" : "BRIGHTER") + " PIECES " + juce::String (juce::roundToInt (std::abs (s.tilt) * 100)) + "%");
    if (s.time)
    {
        if (s.wildMax > 0.01f) parts.add ("WILDEST " + juce::String (juce::roundToInt (s.wildPos * 100)) + "% IN (" + juce::String (juce::roundToInt (s.wildMax * 100)) + "%)");
        if (s.calmMin < -0.01f) parts.add ("CLOSEST " + juce::String (juce::roundToInt (s.calmPos * 100)) + "% IN");
    }
    return parts.joinIntoString ("   ");
}
}
