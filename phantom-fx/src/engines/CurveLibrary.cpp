// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX: the CURVES engine's library (DECISIONS.md 85): the embedded curve sets (assets/curves, "GBCV" files: u32 version, lanes,
// points, then lanes x points float32, little-endian) and their ADAA tables, built once per set on the message thread.
#include "CurveEngine.h"
#include "PhantomCurvesData.h"

namespace phantom::engines
{
const std::vector<CurveSet>& curveLibrary()
{
    static const std::vector<CurveSet> lib = []
    {
        std::vector<CurveSet> out; int size = 0;
        const char* idx = PhantomCurvesData::getNamedResource ("index_json", size);
        if (idx == nullptr) return out;
        const auto arr = juce::JSON::parse (juce::String::fromUTF8 (idx, size));
        if (auto* a = arr.getArray()) for (auto& e : *a)
        {
            const auto file = e["file"].toString();
            for (int r = 0; r < PhantomCurvesData::namedResourceListSize; ++r)
            {
                if (file != PhantomCurvesData::originalFilenames[r]) continue;
                int n = 0; const char* d = PhantomCurvesData::getNamedResource (PhantomCurvesData::namedResourceList[r], n);
                if (d == nullptr || n < 16 || std::memcmp (d, "GBCV", 4) != 0) break;
                std::uint32_t hdr[3]; std::memcpy (hdr, d + 4, 12);
                CurveSet cs; cs.name = e["name"].toString().toStdString(); cs.lanes = (int) hdr[1]; cs.points = (int) hdr[2];
                if (n < 16 + cs.lanes * cs.points * 4 || cs.lanes < 1 || cs.points < 2) break;
                for (int l = 0; l < cs.lanes; ++l) { std::vector<float> v ((size_t) cs.points); std::memcpy (v.data(), d + 16 + (size_t) l * cs.points * 4, (size_t) cs.points * 4); cs.curve.push_back (std::move (v)); }
                out.push_back (std::move (cs)); break;
            }
        }
        return out;
    }();
    return lib;
}

namespace
{
    std::array<std::atomic<const BuiltCurves*>, 256>& slots() { static std::array<std::atomic<const BuiltCurves*>, 256> s {}; return s; }
}

const BuiltCurves* builtCurves (int set)
{
    if (set < 0 || set >= 256) return nullptr; return slots()[(size_t) set].load (std::memory_order_acquire);
}

void buildCurves (int set)
{
    const auto& lib = curveLibrary(); if (set < 0 || set >= (int) lib.size() || set >= 256) return;
    static std::mutex m; std::lock_guard<std::mutex> g (m);
    if (slots()[(size_t) set].load() != nullptr) return;
    // kept for the life of the process (the audio thread may hold it at any time); at most one per set
    slots()[(size_t) set].store (buildTables (lib[(size_t) set]).release(), std::memory_order_release);
}

std::unique_ptr<BuiltCurves> buildTables (const CurveSet& cs)
{
    auto b = std::make_unique<BuiltCurves>();
    const double dxSrc = 2.0 / (cs.points - 1); b->dx = dxSrc; b->inv = 1.0 / dxSrc; b->lo = -BuiltCurves::kSpan;
    b->n = (int) std::ceil (2.0 * BuiltCurves::kSpan / dxSrc) + 1; b->lanes = cs.lanes;
    b->f.resize ((size_t) b->lanes * (size_t) b->n); b->F1.resize (b->f.size()); b->F2.resize (b->f.size());
    for (int l = 0; l < cs.lanes; ++l)
    {
        const auto& c = cs.curve[(size_t) l]; double* f = b->f.data() + (size_t) l * b->n; double* F1 = b->F1.data() + (size_t) l * b->n; double* F2 = b->F2.data() + (size_t) l * b->n;
        for (int i = 0; i < b->n; ++i)
        {   // the curve, held at its end values beyond +-1
            const double x = b->lo + i * dxSrc, p = juce::jlimit (0.0, (double) (cs.points - 1), (x + 1.0) / dxSrc); const int k = juce::jmin ((int) p, cs.points - 2); const double fr = p - k;
            f[i] = (double) c[(size_t) k] + ((double) c[(size_t) k + 1] - (double) c[(size_t) k]) * fr;
        }
        // its first and second antiderivatives (trapezoid), anchored at x = 0 as Phantom's CurveADAA does
        F1[0] = 0.0; for (int i = 1; i < b->n; ++i) F1[i] = F1[i - 1] + 0.5 * (f[i - 1] + f[i]) * dxSrc;
        const int mid = (int) std::lround (-b->lo / dxSrc); { const double c0 = F1[mid]; for (int i = 0; i < b->n; ++i) F1[i] -= c0; }
        F2[0] = 0.0; for (int i = 1; i < b->n; ++i) F2[i] = F2[i - 1] + 0.5 * (F1[i - 1] + F1[i]) * dxSrc;
        { const double c0 = F2[mid]; for (int i = 0; i < b->n; ++i) F2[i] -= c0; }
    }
    return b;
}
} // namespace phantom::engines
