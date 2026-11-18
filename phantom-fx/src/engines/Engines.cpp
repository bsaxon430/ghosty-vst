// SPDX-License-Identifier: AGPL-3.0-or-later
// Phantom FX: which effects have an ENGINE menu and what is in it (DECISIONS.md 85). Labels are "ENGINE · Voice"; the editor shows each
// engine as a sub-menu. Our own names only (never another product's).
#include "Engines.h"
#include "MultiEngine.h"
#include "LynxEngines.h"
#include "CurveEngine.h"
#include "ApexEngine.h"
#include "GraniteEngine.h"
#include "ClassicEngines.h"
#include "EchoEngines.h"
#include "PrismEngine.h"
#include "EngineGains.inc"
#include <functional>

namespace phantom::engines
{
int crusherVoices(); std::string crusherVoiceName (int); std::unique_ptr<AltEngine> makeCrusherEngine (const std::vector<ParamSpec>&);   // CrusherEngine.cpp
namespace
{
    struct Plan { bool build = false; int engines = 0; std::vector<std::unique_ptr<AltEngine>> alts; std::vector<Entry> menu; double tail = 0.0; };
    using LK = LynxVerbEngine::Kind;
    // adds one engine with all its voices to the menu
    template <typename Make> void add (Plan& p, int unit, const char* name, int voices, std::function<std::string (int)> voiceName, Make make, double tail = 0.0)
    {
        const int k = p.engines++; if (p.build) p.alts.push_back (make());   // (labels only: nothing is built)
        for (int v = 0; v < voices; ++v) { Entry e; e.label = std::string (name) + (voices > 1 || ! voiceName (v).empty() ? " \xc2\xb7 " + voiceName (v) : std::string()); e.engine = k; e.voice = v; e.gain = levelMatch (unit, e.label); p.menu.push_back (e); }
        p.tail = juce::jmax (p.tail, tail);
    }
    void lynxVerb (Plan& p, int unit, const char* name, LK kind, const std::vector<ParamSpec>& specs)
    {
        add (p, unit, name, LynxVerbEngine::voiceCount (kind), [kind] (int v) { return LynxVerbEngine::voiceName (kind, v); },
             [&] { return std::make_unique<LynxVerbEngine> (kind, specs); }, 120.0);
    }
    // the plan for unit u; specs = its controls without the ENGINE menu (the alternative engines read them by id)
    Plan plan (int u, const std::vector<ParamSpec>& specs, bool build)
    {
        Plan p; p.build = build; Entry own; own.label = "PHANTOM"; p.menu.push_back (own);
        switch (u)
        {
            case uHalls:
                lynxVerb (p, u, "VINTAGE", LK::Epoch, specs); lynxVerb (p, u, "ROOMS", LK::Atrium, specs); lynxVerb (p, u, "PLATES", LK::Foil, specs);
                add (p, u, "CLASSIC", ClassicReverbEngine::kVoices, [] (int v) { return std::string (ClassicReverbEngine::preset (v).name); }, [&] { return std::make_unique<ClassicReverbEngine> (specs); }, 30.0);
                break;
            case uLim:
                add (p, u, "APEX", ApexEngine::kVoices, [] (int v) { return ApexEngine::voiceName (v); }, [] { return std::make_unique<ApexEngine>(); });
                add (p, u, "CLASSIC", 1, [] (int) { return std::string(); }, [] { return std::make_unique<ClassicLimiterEngine>(); });
                add (p, u, "GRANITE", GraniteLimEngine::kVoices, [] (int v) { return GraniteLimEngine::voiceName (v); }, [] { return std::make_unique<GraniteLimEngine>(); });
                break;
            case uEcho:
                add (p, u, "ECHOFORM", LynxEchoEngine::voiceCount (LynxEchoEngine::Echoform), [] (int v) { return LynxEchoEngine::voiceName (LynxEchoEngine::Echoform, v); }, [&] { return std::make_unique<LynxEchoEngine> (LynxEchoEngine::Echoform, specs); }, 30.0);
                add (p, u, "SPIRAL", 1, [] (int) { return std::string(); }, [&] { return std::make_unique<LynxEchoEngine> (LynxEchoEngine::Spiral, specs); }, 30.0);
                break;
            case uChorus:
                add (p, u, "ORBIT", LynxEchoEngine::voiceCount (LynxEchoEngine::Orbit), [] (int v) { return LynxEchoEngine::voiceName (LynxEchoEngine::Orbit, v); }, [&] { return std::make_unique<LynxEchoEngine> (LynxEchoEngine::Orbit, specs); }, 1.0);
                break;
            case uOtt:
                add (p, u, "PRISM", PrismEngine::kVoices, [] (int v) { return std::string (PrismEngine::voice (v).name); }, [&] { return std::make_unique<PrismEngine> (specs); });
                break;
            case uCrush:
                add (p, u, "CRUSHER", crusherVoices(), [] (int v) { return crusherVoiceName (v); }, [&] { return makeCrusherEngine (specs); });
                break;
            case uSpace:
                lynxVerb (p, u, "NEBULA", LK::Nebula, specs); lynxVerb (p, u, "HORIZON", LK::Horizon, specs); break;
            case uShimmer:
                lynxVerb (p, u, "HALO", LK::Halo, specs); break;
            case uWreck: case uCrackle: case uTube:
            {   // the curve sets (assets/curves): one menu entry per set; DRIVE morphs through its curves
                const auto& lib = curveLibrary();
                add (p, u, "CURVES", (int) lib.size(), [&lib] (int v) { return lib[(size_t) v].name; }, [&] { return std::make_unique<CurveEngine> (u, specs); });
                add (p, u, "DRAW", 1, [] (int) { return std::string(); }, [&] { return std::make_unique<CurveEngine> (u, specs, true); });
                break;
            }
            default: break;
        }
        return p;
    }
} // namespace

std::vector<std::string> engineLabels (int u)
{
    auto p = plan (u, {}, false); if (p.menu.size() <= 1) return {};
    std::vector<std::string> l; for (auto& e : p.menu) l.push_back (e.label); return l;
}

std::unique_ptr<PhantomEffect> withEngines (int u, std::unique_ptr<PhantomEffect> own, int defaultEntry)
{
    auto p = plan (u, unitParams (u, false), true); if (p.menu.size() <= 1) return own;
    auto fx = std::make_unique<MultiEngineFx> (std::move (own), std::move (p.alts), std::move (p.menu), defaultEntry);
    fx->setAltTail (p.tail); return fx;
}
} // namespace phantom::engines
