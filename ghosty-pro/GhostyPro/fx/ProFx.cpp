// Ghost FX rack: the audio side (parameters, processing chain, scenes, state).
#include "FxRackImpl.h"
#include "FxPresets.h"
#if GPFX_BOX
 #include "FxBrain.h"
#endif
#include <map>

namespace gpfx {

// ================================================================ Impl
Rack::Impl::Impl()
{
#if GPFX_BOX
    auto gateU = std::make_unique<BoxGateUnit>(); gateU->pumpShape = &pumpShape; gateU->sc = &sc; gateU->ptm = &tm;
#else
    auto gateU = std::make_unique<GateUnit>();
#endif
    gate = gateU.get(); gate->store = &gateStore; gate->tm = &tm;
    auto gridU = std::make_unique<GridUnit>(); grid = gridU.get(); grid->store = &gridStore; grid->tm = &tm;
    auto hauntU = std::make_unique<HauntUnit>(); haunt = hauntU.get(); haunt->pads = &padStore; haunt->tm = &tm;
    auto wreckU = std::make_unique<WreckerUnit>(); wreckU->tm = &tm;
    auto tubeU = std::make_unique<TubeUnit>(); tubeU->tm = &tm;
    auto crushU = std::make_unique<CrushUnit>(); crushU->tm = &tm;
    auto filterU = std::make_unique<FilterUnit>(); filterU->tm = &tm;
    auto chorusU = std::make_unique<ChorusUnit>(); chorusU->tm = &tm;
    auto crystalU = std::make_unique<CrystalUnit>(); crystalU->tm = &tm;
    auto echoU = std::make_unique<EchoUnit>(); echoU->tm = &tm;
    auto spaceU = std::make_unique<SpaceUnit>(); spaceU->tm = &tm;
    auto shimmerU = std::make_unique<ShimmerUnit>(); shimmerU->tm = &tm;
    auto ottU = std::make_unique<OttUnit>(); ottU->tm = &tm;
    auto limU = std::make_unique<LimiterUnit>(); limU->tm = &tm;
    units[uGate] = std::move (gateU); units[uGrid] = std::move (gridU); units[uHaunt] = std::move (hauntU);
    units[uWreck] = std::move (wreckU); units[uTube] = std::move (tubeU); units[uCrush] = std::move (crushU);
    units[uEq] = eq::createUnit(); eqUnit = units[uEq].get();
    units[uFilter] = std::move (filterU); units[uChorus] = std::move (chorusU); units[uCrystal] = std::move (crystalU);
    units[uEcho] = std::move (echoU); units[uSpace] = std::move (spaceU); units[uShimmer] = std::move (shimmerU);
    units[uOtt] = std::move (ottU); units[uLim] = std::move (limU);
#if ! GPFX_BOX
    { auto u = std::make_unique<FattenUnit>(); u->tm = &tm; units[uFatten] = std::move (u); }   // Ghosty Pro: FATTEN (off by default)
    { auto u = std::make_unique<HyperUnit>(); u->tm = &tm; units[uHyper] = std::move (u); }     // Ghosty Pro: HYPER / DIMENSION (off by default)
    { auto u = std::make_unique<DistUnit>(); u->tm = &tm; u->store = &distCurves; units[uDist] = std::move (u); }   // FX RACK (round 3): DISTORTION
    { auto u = std::make_unique<CompUnit>(); u->tm = &tm; u->rackIn = &xfRing; comp = u.get(); units[uComp] = std::move (u); }   // FX RACK (round 3): COMPRESSOR
    { auto u = std::make_unique<BandsUnit>(); u->tm = &tm; bands = u.get(); units[uBands] = std::move (u); }   // FX RACK (round 3): SPLITTER
    { auto u = std::make_unique<Eq3Unit>(); u->tm = &tm; units[uEq3] = std::move (u); }   // FX RACK (round 3): TONE EQ
    for (auto& b : slotType) b.fill (-1);
#endif
#if GPFX_BOX
    { auto u = std::make_unique<WarpUnit>(); u->tm = &tm; u->shape = &warpShape; units[uWarp] = std::move (u); }
    { auto u = std::make_unique<CrackleUnit>(); u->tm = &tm; units[uCrackle] = std::move (u); }
    { auto u = std::make_unique<RetroUnit>(); u->tm = &tm; units[uRetro] = std::move (u); }
    { auto u = std::make_unique<FattenUnit>(); u->tm = &tm; units[uFatten] = std::move (u); }
    { auto u = std::make_unique<HallsUnit>(); u->tm = &tm; units[uHalls] = std::move (u); }
    { auto u = std::make_unique<SplitUnit>(); u->tm = &tm; split = u.get(); units[uSplit] = std::move (u); }
    { auto u = std::make_unique<SpectraUnit>(); u->tm = &tm; u->offline = &offline; u->mask = &spectraMask; units[uSpectra] = std::move (u); }
    { auto u = std::make_unique<BassLabUnit>(); u->tm = &tm; units[uBassLab] = std::move (u); }
    { auto u = std::make_unique<PortalUnit>(); u->tm = &tm; units[uPortal] = std::move (u); }
    { auto u = std::make_unique<FormantUnit>(); u->tm = &tm; u->shape = &formantShape; units[uFormant] = std::move (u); }
    {   // default drawn shapes: a pump that ducks on the beat, a vowel path that talks
        bendlane::Shape p; p.snap = 0; p.add (0.0f, -1.0f, 0.5f); p.add (0.6f, 0.0f); p.add (0.97f, 0.0f); pumpShape.set (p);   // duck on the beat, recover by 60 %, hold, drop again
        bendlane::Shape f; f.snap = 0; f.add (0.0f, -1.0f, 0.3f); f.add (0.25f, 1.0f, -0.3f); f.add (0.5f, -0.3f, 0.3f); f.add (0.75f, 0.6f, -0.3f); formantShape.set (f);
    }
#endif

    for (int i = 0; i < kGatePatterns; ++i) { gatePatterns[(size_t) i] = factoryGatePattern (i); gateStore.bake (i, gatePatterns[(size_t) i]); }
    for (int i = 0; i < kGridPatterns; ++i) { gridPatterns[(size_t) i] = factoryGridPattern (i); gridStore.bake (i, gridPatterns[(size_t) i]); }
    for (int i = 0; i < kNumUnits; ++i) { pendingOrder[(size_t) i].store (i); order[(size_t) i] = i; duckG[(size_t) i] = 1.0f; }
}

void Rack::Impl::setGatePattern (int i, const GatePattern& p)
{
    if (i < 0 || i >= kGatePatterns) return;
    { const juce::ScopedLock sl (dataLock); gatePatterns[(size_t) i] = p; }
    gateStore.bake (i, p); dataVersion.fetch_add (1);
}
GatePattern Rack::Impl::getGatePattern (int i) const { const juce::ScopedLock sl (dataLock); return gatePatterns[(size_t) juce::jlimit (0, kGatePatterns - 1, i)]; }
void Rack::Impl::setGridPattern (int i, const GridPattern& p)
{
    if (i < 0 || i >= kGridPatterns) return;
    { const juce::ScopedLock sl (dataLock); gridPatterns[(size_t) i] = p; }
    gridStore.bake (i, p); dataVersion.fetch_add (1);
}
GridPattern Rack::Impl::getGridPattern (int i) const { const juce::ScopedLock sl (dataLock); return gridPatterns[(size_t) juce::jlimit (0, kGridPatterns - 1, i)]; }

void Rack::Impl::setOrder (const std::array<int, kNumUnits>& o)
{
    // validate: a permutation of all units with the limiter last
    std::array<bool, kNumUnits> seen {}; std::array<int, kNumUnits> v {}; int k = 0;
    for (int u : o) if (u >= 0 && u < kNumUnits && u != uLim && ! seen[(size_t) u] && k < kNumUnits - 1) { seen[(size_t) u] = true; v[(size_t) k++] = u; }
    for (int u = 0; u < kNumUnits; ++u) if (u != uLim && ! seen[(size_t) u]) v[(size_t) k++] = u;
    v[kNumUnits - 1] = uLim;
    bool same = true; for (int i = 0; i < kNumUnits && same; ++i) same = pendingOrder[(size_t) i].load() == v[(size_t) i];
    if (same) return;   // nothing moved: no fade / flush (a mid-play state restore re-applies the same order)
    for (int i = 0; i < kNumUnits; ++i) pendingOrder[(size_t) i].store (v[(size_t) i]);
    orderVersion.fetch_add (1); dataVersion.fetch_add (1);
}
std::array<int, kNumUnits> Rack::Impl::getOrder() const
{
    std::array<int, kNumUnits> o {}; for (int i = 0; i < kNumUnits; ++i) o[(size_t) i] = pendingOrder[(size_t) i].load(); return o;
}

#if GPFX_BOX
// GHOST SPLIT (FxRoute.h): the input of the stage is split into the branch buffers, every branch runs its units in chain order (a unit
// assigned to branch 3 while the mode has 2 runs in branch 2), the branches are aligned to the slowest one and summed back
template <typename F>
void Rack::Impl::runRoute (int k, const BlockCtx&, F& runUnit, float* L0, float* R0, int n)
{
    const int m = routeModeNow, nb = routeBranchCount (m, routeBandsNow);
    route.split (L0, R0, n, m, nb);
    std::array<int, kRouteBranches> lat {}; bool anyActive = false;
    for (int b = 0; b < nb; ++b)
        for (int j = k; j < kNumUnits; ++j)
        {
            const int u = order[(size_t) j];
            if (u == uLim || std::min (routeNow[(size_t) u], nb) != b + 1) continue;
            runUnit (u, *units[(size_t) u], route.br[(size_t) b][0].data(), route.br[(size_t) b][1].data());
            lat[(size_t) b] += units[(size_t) u]->latency();
            anyActive = anyActive || active[(size_t) u];
        }
    route.sum (L0, R0, n, m, nb, lat, ! anyActive && route.gainsUnity());
}
void Rack::Impl::latchRoute() noexcept
{
    routeSeen = routeVersion.load (std::memory_order_acquire);
    routeOnNow = attached && route.on.b(); routeModeNow = attached ? route.mode.i() : 0; routeBandsNow = attached ? route.bands.i() : 0;
    route.keepNow = ! attached || route.keep.b();
    bool any = false;
    for (int u = 0; u < kNumUnits; ++u) { routeNow[(size_t) u] = u == uLim ? 0 : routeOf[(size_t) u].load (std::memory_order_relaxed); any = any || routeNow[(size_t) u] > 0; }
    routeActiveNow = routeOnNow && any;
    route.reset();   // fresh crossovers / alignment for the new routing (the chain is silent and flushing here)
}
bool Rack::Impl::routeRouting() const
{
    if (! attached || ! route.on.b()) return false;
    for (int u = 0; u < kNumUnits; ++u) if (u != uLim && routeOf[(size_t) u].load() > 0) return true;
    return false;
}
#endif

void Rack::Impl::processChunk (float* L0, float* R0, int n, const BlockCtx& ctx)
{
    const int ver = orderVersion.load (std::memory_order_acquire);
#if GPFX_BOX
    // PARALLEL LANES: switching them (or moving a unit between lanes) uses the same click-free fade as a reorder
    const bool parWant = parOnP.b();
    // GHOST SPLIT: assigning units, switching it on / off or changing its mode also goes through that fade (only while it routes units)
    bool routeAny = false; for (int u = 0; u < kNumUnits && ! routeAny; ++u) routeAny = u != uLim && routeOf[(size_t) u].load (std::memory_order_relaxed) > 0;
    const bool routeOnWant = route.on.b();
    if (! routeAny && ! routeActiveNow) { routeOnNow = routeOnWant; routeModeNow = route.mode.i(); routeBandsNow = route.bands.i(); route.keepNow = route.keep.b(); }   // (nothing routed: nothing to fade)
    const bool routeSwitch = routeVersion.load (std::memory_order_acquire) != routeSeen || (routeOnWant != routeOnNow && (routeAny || routeActiveNow))
                          || (routeActiveNow && (route.mode.i() != routeModeNow || route.bands.i() != routeBandsNow || (routeModeNow == rmFreq && route.keep.b() != route.keepNow)));
    const bool wantSwitch = ver != seenVersion || parWant != parNow || laneVersion.load (std::memory_order_acquire) != laneSeen || routeSwitch;
    const bool rt = routeActiveNow;
    const bool par = parNow && ! rt;   // (GHOST SPLIT replaces the parallel lanes while it routes units)
    if (par) { std::copy (L0, L0 + n, laneBL.data()); std::copy (R0, R0 + n, laneBR.data()); }
#else
    const bool wantSwitch = ver != seenVersion;
    {   // FX RACK (round 3): the chain input, kept for the reorder crossfade (a cheap ring write per sample)
        for (int i = 0; i < n; ++i) { xfRing[0].push (L0[i]); xfRing[1].push (R0[i]); }
    }
    if (orderFade != 0)
    {   // FX RACK (round 3): every unit's input fades out before the switch and back in after it, so no unit (delay line, reverb,
        // filter state) ever sees the step between the old and the new chain; the output meanwhile plays the chain's own input
        const int len = reorderLen();
        for (int i = 0; i < n; ++i)
        {
            const float t = clampf ((float) (fadePos + i + 1) / (float) len, 0.0f, 1.0f), w = 0.5f - 0.5f * std::cos (kPi * t);
            reorderIn[(size_t) i] = orderFade == 1 ? 1.0f - w : orderFade == 3 ? w : 1.0f;
        }
    }
#endif
    // start fading out (a fixed ~5 ms, whatever the chunk size) - but only once the rack has settled: a reorder in the middle of a
    // duck / hold / on-off fade (a scene or CHAIN BRAIN apply does both at once) can land one bad sample at the switch
    if (wantSwitch && orderFade == 0)
    {
        bool settled = true;
        for (int u = 0; u < kNumUnits && settled; ++u) settled = (! active[(size_t) u] || (duckG[(size_t) u] >= 1.0f && duckHold[(size_t) u] == 0)) && onSm[(size_t) u].idle();
        switchWait += n;
        if (settled || switchWait > (int) (0.25 * sr)) { orderFade = 1; fadePos = 0; switchWait = 0; }
    }
    // one unit's processing (its on / off fade, duck, mix, safety) on the buffers it is given (the chain, a parallel lane, a GHOST SPLIT branch)
    auto runUnit = [&] (int u, UnitDsp& unit, float* L, float* R)
    {
        const bool onT = onP[(size_t) u].b();
        auto& os = onSm[(size_t) u];
        os.setTarget (onT ? 1.0f : 0.0f);
        if (! onT && os.idle() && os.cur <= 0.0f)
        {
            active[(size_t) u] = false; duckG[(size_t) u] = 1.0f; duckHold[(size_t) u] = 0; duckLow[(size_t) u].store (true, std::memory_order_release);
#if GPFX_BOX
            box.commitDuck (u, unit);
            spG[(size_t) u] = spOut[(size_t) u] = 1.0f; spQuiet[(size_t) u] = spAge[(size_t) u] = 0;   // (GHOST SCENES SPILL: done ringing)
#endif
            unit.delayDry (L, R, n);
            unit.idle (L, R, n);
            this->tm.unitLevel[(size_t) u].store (0.0f, std::memory_order_relaxed);   // (this-> : MSVC reads a bare 'tm' in this lambda as the C struct tm)
            return;
        }
        if (! active[(size_t) u]) { unit.reset(); active[(size_t) u] = true; }
#if GPFX_BOX
        // GHOST SCENES SPILL: a unit the new scene turned off rings out (input faded, settings held, tail added to the chain)
        if (box.scenes.spill[(size_t) u] || spG[(size_t) u] < 1.0f || spOut[(size_t) u] < 1.0f) { spillChunk (u, unit, L, R, n, ctx); return; }
#endif
#if ! GPFX_BOX
        if (orderFade != 0 && u != uLim) for (int i = 0; i < n; ++i) { L[i] *= reorderIn[(size_t) i]; R[i] *= reorderIn[(size_t) i]; }   // FX RACK (round 3): no seams in any unit's input
#endif
        std::copy (L, L + n, dryL.data()); std::copy (R, R + n, dryR.data());
        unit.process (L, R, n, ctx);
        unit.delayDry (dryL.data(), dryR.data(), n);
#if ! GPFX_BOX
        if (u == uWreck || u == uTube || u == uCrush || u == uFatten)
        {
            float& gi = cmIn[(size_t) u]; float& go = cmOut[(size_t) u]; float& g = cmG[(size_t) u];
            if (cleanMatch.load (std::memory_order_relaxed))
            {   // CLEAN: the wet signal is pulled down to the input's loudness when the unit makes it louder (slow RMS, never boosts)
                if (gi <= 0.0f && go <= 0.0f) g = 1.0f;
                for (int i = 0; i < n; ++i)
                {
                    const float pin = dryL[(size_t) i] * dryL[(size_t) i] + dryR[(size_t) i] * dryR[(size_t) i], pout = L[i] * L[i] + R[i] * R[i];
                    gi += cmA * (pin - gi) + 1.0e-24f; go += cmA * (pout - go) + 1.0e-24f;
                    const float t = go > gi ? std::sqrt (gi / go) : 1.0f;
                    g += cmGa * (t - g);
                    L[i] *= g; R[i] *= g;
                }
            }
            else { gi = go = 0.0f; g = 1.0f; }
        }
#endif
        auto& ms = mixSm[(size_t) u];
        ms.target = unit.selfMix() || ! unitInfo (u).hasMix ? 1.0f : clampf (mixP[(size_t) u](), 0.0f, 1.0f);
        // preset / scene loads duck this unit's wet signal to dry for a few ms around the parameter jump (click-free loads)
        const bool userDuck = duckReq[(size_t) u].load (std::memory_order_acquire) != 0;
#if GPFX_BOX
        const bool autoDuck = box.duckWanted (u);   // MORPH switching a discrete / time parameter of this unit
#else
        const bool autoDuck = false;
#endif
        const bool wantDuck = userDuck || autoDuck;
        float& dg = duckG[(size_t) u];
        // after the change the unit stays dry ~30 ms more, so jumps already written into delay lines play out unheard
        int& hold = duckHold[(size_t) u];
        if (wantDuck) hold = (int) ((userDuck ? 0.03 : 0.006) * sr); else if (hold > 0) hold = std::max (0, hold - n);
        const bool duckIdle = ! wantDuck && hold == 0 && dg >= 1.0f;
        const float dStep = (wantDuck || hold > 0) ? -1.0f / (float) (0.004 * sr) : 1.0f / (float) (0.012 * sr);
        const bool full = duckIdle && os.idle() && os.cur >= 1.0f && ms.target >= 1.0f && std::abs (ms.cur - 1.0f) < 1.0e-5f;
        if (! full)
            for (int i = 0; i < n; ++i)
            {
                if (! duckIdle) dg = clampf (dg + dStep, 0.0f, 1.0f);
                const float a = os.next() * ms.next() * dg;
                L[i] = dryL[(size_t) i] + a * (L[i] - dryL[(size_t) i]);
                R[i] = dryR[(size_t) i] + a * (R[i] - dryR[(size_t) i]);
            }
        duckLow[(size_t) u].store (userDuck && dg <= 0.0f, std::memory_order_release);
#if GPFX_BOX
        if (autoDuck && dg <= 0.0f) box.commitDuck (u, unit);
#endif
        // safety: a unit that ever produces non-finite output is reset and replaced by its dry signal for this block
        if (! std::isfinite (L[n - 1]) || ! std::isfinite (R[n - 1]) || ! std::isfinite (L[0]) || ! std::isfinite (R[0]))
        {
            unit.reset();
            std::copy (dryL.data(), dryL.data() + n, L); std::copy (dryR.data(), dryR.data() + n, R);
        }
        float pk = 0; for (int i = 0; i < n; i += 4) pk = std::max (pk, std::abs (L[i]));
        this->tm.unitLevel[(size_t) u].store (pk, std::memory_order_relaxed);
    };
#if GPFX_BOX
    bool routeDone = false;
#endif
    for (int k = 0; k < kNumUnits; ++k)
    {
        const int u = order[(size_t) k];
        auto& unit = *units[(size_t) u];
        float* L = L0; float* R = R0;
#if GPFX_BOX
        if (rt && u != uLim && routeNow[(size_t) u] > 0)
        {   // GHOST SPLIT: the stage sits where its first unit is; every branch runs its units (in chain order), then they are summed
            if (! routeDone) { routeDone = true; runRoute (k, ctx, runUnit, L0, R0, n); }
            continue;
        }
        if (par && u != uLim && laneNow[(size_t) u] == 1) { L = laneBL.data(); R = laneBR.data(); }
        if (par && u == uLim)
        {   // sum the lanes, phase-aligned: the lane with less latency waits for the other
            int la = 0, lb = 0;
            for (int j = 0; j < k; ++j) { const int uj = order[(size_t) j]; (laneNow[(size_t) uj] == 1 ? lb : la) += units[(size_t) uj]->latency(); }
            laneDelA.process (L0, R0, n, std::max (0, lb - la));
            laneDelB.process (laneBL.data(), laneBR.data(), n, std::max (0, la - lb));
            parBlendSm.target = clampf (parBlendP(), 0.0f, 1.0f);
            for (int i = 0; i < n; ++i)
            {
                const float b = parBlendSm.next(), ga = std::min (1.0f, 2.0f * (1.0f - b)), gb = std::min (1.0f, 2.0f * b);
                L0[i] = ga * L0[i] + gb * laneBL[(size_t) i]; R0[i] = ga * R0[i] + gb * laneBR[(size_t) i];
            }
        }
        if (u == uLim)
        {   // MOTION's built-in stage sits just before the limiter; it reads the lanes where this audio sits on the host's
            // (latency-compensated) timeline: earlier units have already delayed it by their latency
            int up = 0;
            if (rt) { std::array<int, kNumUnits> ul {}; upstreamLatency (ul); up = ul[uLim]; }
            else if (par) { int la = 0, lb = 0; for (int j = 0; j < k; ++j) { const int uj = order[(size_t) j]; (laneNow[(size_t) uj] == 1 ? lb : la) += units[(size_t) uj]->latency(); } up = std::max (la, lb); }
            else for (int j = 0; j < k; ++j) up += units[(size_t) order[(size_t) j]]->latency();
            BlockCtx c2 = ctx; c2.ppq -= (double) up / ctx.samplesPerBeat;
            space.process (L, R, n, c2);   // GHOST SPACE (off: untouched)
            print.process (L, R, n);       // GHOSTPRINT targets (defaults: untouched)
            box.builtin (L, R, n, c2);
            box.matchOut (L, R, n);   // CHAIN BRAIN's loudness match sits before the limiter, so matching never pushes overs past it
        }
#endif
#if ! GPFX_BOX
        // FX RACK (round 3): a reorder crossfades the chain's output (just before the limiter) to the chain's own input, delayed
        // by the chain's latency, while the order switches and the latent lines flush, then back: never a drop to silence
        if (u == uLim && orderFade != 0) reorderBlend (L, R, n);
        if (u == uComp) { int up = 0; for (int j = 0; j < k; ++j) up += units[(size_t) order[(size_t) j]]->latency(); comp->keyLatency = up; }   // KEY = RACK INPUT, aligned
#endif
        runUnit (u, unit, L, R);
    }
#if ! GPFX_BOX
    // FX RACK (round 3): the reorder state machine (the blend itself ran before the limiter, reorderBlend)
    if (orderFade != 0)
    {
        if (orderFade == 3) { flushLeft -= n; fadePos += n; if (flushLeft <= 0) { orderFade = 2; fadePos = 0; } }
        else
        {
            fadePos += n;
            if (fadePos >= reorderLen())
            {
                fadePos = 0;
                if (orderFade == 1)
                {   // the chain is all input now: switch, then let the latent lines flush before blending back
                    for (int i = 0; i < kNumUnits; ++i) order[(size_t) i] = pendingOrder[(size_t) i].load (std::memory_order_relaxed);
                    seenVersion = ver;
                    flushLeft = chainLatency() + reorderLen() + 8; orderFade = 3;   // the inputs fade back in, the latent lines catch up
                }
                else orderFade = 0;
            }
        }
    }
    juce::ignoreUnused (L0, R0);
#else
    // click-free reordering: fade out over ~5 ms (across chunks), swap the order, stay silent while the chain's delay lines
    // (the latent units' dry delays, the limiter's lookahead) flush the old signal, then fade back in over ~5 ms
    float* L = L0; float* R = R0;
    if (orderFade != 0)
    {
        const int len = std::max (32, (int) (0.005 * sr));
        if (orderFade == 3)
        {
            std::fill (L, L + n, 0.0f); std::fill (R, R + n, 0.0f);
            flushLeft -= n;
            if (flushLeft <= 0) { orderFade = 2; fadePos = 0; }
            return;
        }
        for (int i = 0; i < n; ++i)
        {
            const float t = clampf ((float) (fadePos + i + 1) / (float) len, 0.0f, 1.0f);
            const float g = orderFade == 1 ? 1.0f - t : t;
            L[i] *= g; R[i] *= g;
        }
        fadePos += n;
        if (fadePos >= len)
        {
            fadePos = 0;
            if (orderFade == 1)
            {   // silent now: switch, then flush
                for (int i = 0; i < kNumUnits; ++i) order[(size_t) i] = pendingOrder[(size_t) i].load (std::memory_order_relaxed);
                seenVersion = ver;
#if GPFX_BOX
                parNow = parWant; laneSeen = laneVersion.load (std::memory_order_acquire);
                for (int i = 0; i < kNumUnits; ++i) laneNow[(size_t) i] = laneOf[(size_t) i].load (std::memory_order_relaxed);
                latchRoute();
#endif
                int lat = 0; for (auto& un : units) lat += un->latency();
                flushLeft = lat + 8; orderFade = 3;
            }
            else orderFade = 0;
        }
    }
#endif
}

#if GPFX_BOX
// GHOST SCENES SPILL (FxBoxScenes.h): out = dry * (1 - a * g) + a * unit (input * g), g = the input gate (1 -> 0 over 8 ms), a = the
// unit's on * mix * the ring-out gain. g = a = 1 is exactly the normal mix (dry + a * (wet - dry)); g = 0 passes the new chain at full and
// adds the old unit's tail. The tail ends below -90 dBFS for 250 ms (30 ms fade) or after 10 s (600 ms fade); then the scene layer lets
// the unit's settings go (it is off in the new scene) and the rack's own off fade + idle take over. A cancelled ring-out (the unit is
// switched on again) opens the gate and the gain back up over 8 ms.
void Rack::Impl::spillChunk (int u, UnitDsp& unit, float* L, float* R, int n, const BlockCtx& ctx)
{
    const size_t k = (size_t) u;
    const bool sp = box.scenes.spill[k];
    const bool onT = onP[k].b();
    auto& os = onSm[k]; auto& ms = mixSm[k];
    const float gT = sp ? 0.0f : (onT ? 1.0f : 0.0f);
    const float step8 = 1.0f / (float) (0.008 * sr);
    std::copy (L, L + n, dryL.data()); std::copy (R, R + n, dryR.data());
    float g = spG[k];
    for (int i = 0; i < n; ++i)
    {
        g = g < gT ? std::min (gT, g + step8) : std::max (gT, g - step8);
        spGate[(size_t) i] = g; spL[(size_t) i] = L[i] * g; spR[(size_t) i] = R[i] * g;
    }
    spG[k] = g;
    unit.process (spL.data(), spR.data(), n, ctx);
    unit.delayDry (dryL.data(), dryR.data(), n);
    if (box.duckWanted (u)) box.commitDuck (u, unit);   // (a held unit never asks; one switched on again may)
    ms.target = unit.selfMix() || ! unitInfo (u).hasMix ? 1.0f : clampf (mixP[k](), 0.0f, 1.0f);
    float pk = 0; for (int i = 0; i < n; ++i) pk = std::max ({ pk, std::abs (spL[(size_t) i]), std::abs (spR[(size_t) i]) });
    float oT, oStep;
    if (sp)
    {
        if (g <= 0.0f)
        {
            spAge[k] += n; spQuiet[k] = pk < 3.2e-5f ? spQuiet[k] + n : 0;
        }
        const bool quiet = spQuiet[k] > (int) (0.25 * sr), old = spAge[k] > (int) (10.0 * sr);
        oT = quiet || old ? 0.0f : 1.0f; oStep = 1.0f / (float) ((quiet ? 0.03 : 0.6) * sr);
    }
    else { oT = onT ? 1.0f : 0.0f; oStep = step8; }
    float o = spOut[k];
    for (int i = 0; i < n; ++i)
    {
        o = o < oT ? std::min (oT, o + oStep) : std::max (oT, o - oStep);
        const float a = os.next() * ms.next() * duckG[k] * o, gi = spGate[(size_t) i];
        L[i] = dryL[(size_t) i] * (1.0f - a * gi) + a * spL[(size_t) i];
        R[i] = dryR[(size_t) i] * (1.0f - a * gi) + a * spR[(size_t) i];
    }
    spOut[k] = o;
    if (sp && o <= 0.0f) { box.scenes.spillDone (u); unit.reset(); spQuiet[k] = spAge[k] = 0; }
    if (! std::isfinite (L[n - 1]) || ! std::isfinite (R[n - 1]) || ! std::isfinite (L[0]) || ! std::isfinite (R[0]))
    {
        unit.reset();
        std::copy (dryL.data(), dryL.data() + n, L); std::copy (dryR.data(), dryR.data() + n, R);
    }
    tm.unitLevel[k].store (pk * o, std::memory_order_relaxed);
}
#endif

#if ! GPFX_BOX
// FX RACK (round 3): reorder crossfade. g = 1 the chain, g = 0 its input delayed by the chain's latency (everything before the
// limiter), a raised-cosine blend over reorderLen() (12 ms): out = g * chain + (1 - g) * input
int Rack::Impl::chainLatency() const
{
    int lat = 0; for (int u = 0; u < kNumUnits; ++u) if (u != uLim) lat += units[(size_t) u]->latency();
    return juce::jlimit (0, xfRing[0].size() - 8, lat);
}
int Rack::Impl::reorderLen() const { return std::max (64, (int) (0.012 * sr)); }
void Rack::Impl::reorderBlend (float* L, float* R, int n)
{
    const int lat = chainLatency(), len = reorderLen();
    for (int i = 0; i < n; ++i)
    {
        float g = 0.0f;
        if (orderFade == 1 || orderFade == 2)
        {
            const float t = clampf ((float) (fadePos + i + 1) / (float) len, 0.0f, 1.0f);
            const float w = 0.5f - 0.5f * std::cos (kPi * t);            // 0 -> 1
            g = orderFade == 1 ? 1.0f - w : w;
        }
        const int back = n - 1 - i + lat;                                 // the ring holds this chunk's input already
        const float dl = xfRing[0].at (back), dr = xfRing[1].at (back);
        L[i] = dl + g * (L[i] - dl); R[i] = dr + g * (R[i] - dr);
    }
}
void Rack::Impl::resetOrder()
{
    std::array<int, kNumUnits> o {}; for (int i = 0; i < kNumUnits; ++i) o[(size_t) i] = i;
    setOrder (o);
}

// FX RACK (round 3): SPLITTER slots. A slot unit binds to the same parameters as the rack's own unit of that kind (its page
// edits both); its meters go to a scratch Telemetry. COMPRESSOR in a slot keys from its band and always has its look-ahead.
std::unique_ptr<UnitDsp> Rack::Impl::makeSlotUnit (int u)
{
    std::unique_ptr<UnitDsp> p;
    switch (u)
    {
        case uWreck:   { auto x = std::make_unique<WreckerUnit>(); x->tm = &slotTm; p = std::move (x); break; }
        case uTube:    { auto x = std::make_unique<TubeUnit>(); x->tm = &slotTm; p = std::move (x); break; }
        case uCrush:   { auto x = std::make_unique<CrushUnit>(); x->tm = &slotTm; p = std::move (x); break; }
        case uFilter:  { auto x = std::make_unique<FilterUnit>(); x->tm = &slotTm; p = std::move (x); break; }
        case uChorus:  { auto x = std::make_unique<ChorusUnit>(); x->tm = &slotTm; p = std::move (x); break; }
        case uCrystal: { auto x = std::make_unique<CrystalUnit>(); x->tm = &slotTm; p = std::move (x); break; }
        case uEcho:    { auto x = std::make_unique<EchoUnit>(); x->tm = &slotTm; p = std::move (x); break; }
        case uSpace:   { auto x = std::make_unique<SpaceUnit>(); x->tm = &slotTm; p = std::move (x); break; }
        case uShimmer: { auto x = std::make_unique<ShimmerUnit>(); x->tm = &slotTm; p = std::move (x); break; }
        case uOtt:     { auto x = std::make_unique<OttUnit>(); x->tm = &slotTm; p = std::move (x); break; }
        case uFatten:  { auto x = std::make_unique<FattenUnit>(); x->tm = &slotTm; p = std::move (x); break; }
        case uHyper:   { auto x = std::make_unique<HyperUnit>(); x->tm = &slotTm; p = std::move (x); break; }
        case uDist:    { auto x = std::make_unique<DistUnit>(); x->tm = &slotTm; x->store = &distCurves; p = std::move (x); break; }
        case uComp:    { auto x = std::make_unique<CompUnit>(); x->tm = &slotTm; p = std::move (x); break; }
        case uEq3:     { auto x = std::make_unique<Eq3Unit>(); x->tm = &slotTm; p = std::move (x); break; }
        default: return {};
    }
    if (apvts != nullptr) p->attach (*apvts);
    if (auto* c = dynamic_cast<CompUnit*> (p.get())) c->hostOn = nullptr;      // (always on inside a slot)
    if (auto* x = dynamic_cast<DistUnit*> (p.get())) x->hostOn = nullptr;
    p->prepare (sr, kSub);
    return p;
}
void Rack::Impl::setBandSlot (int band, int slot, int u)
{
    if (band < 0 || band >= kBands || slot < 0 || slot >= kSlots) return;
    if (u >= 0 && std::find (bandSlotUnits().begin(), bandSlotUnits().end(), u) == bandSlotUnits().end()) u = -1;
    const juce::ScopedLock sl (slotLock);
    if (slotType[(size_t) band][(size_t) slot] == u) return;
    UnitDsp* hand = BandsUnit::emptyMark();
    if (u >= 0) { auto inst = makeSlotUnit (u); hand = inst.get(); slotPool.push_back (std::move (inst)); slotSeen.push_back (-1); }
    bands->slot[(size_t) band][(size_t) slot].pending.store (hand, std::memory_order_release);
    slotType[(size_t) band][(size_t) slot] = u;
    dataVersion.fetch_add (1);
    slotGc();
}
void Rack::Impl::slotGc()
{   // an instance is freed once it was seen unused, and seen unused again after the audio thread finished two more blocks
    const juce::ScopedLock sl (slotLock);
    const int64_t now = bands->epoch.load (std::memory_order_acquire);
    for (size_t i = 0; i < slotPool.size();)
    {
        auto* p = slotPool[i].get(); bool used = false;
        for (auto& b : bands->slot) for (auto& s : b) used = used || s.pending.load() == p || s.curA.load() == p || s.old.load() == p;
        if (used) { slotSeen[i] = -1; ++i; continue; }
        if (slotSeen[i] < 0) { slotSeen[i] = now; ++i; continue; }
        if (now >= slotSeen[i] + 2) { slotPool.erase (slotPool.begin() + (long) i); slotSeen.erase (slotSeen.begin() + (long) i); continue; }
        ++i;
    }
}
#endif

void Rack::Impl::snapSmoothers()
{
    for (int u = 0; u < kNumUnits; ++u)
    {
        const bool onT = onP[(size_t) u].b();
        onSm[(size_t) u].snap (onT ? 1.0f : 0.0f);
        mixSm[(size_t) u].snap (units[(size_t) u]->selfMix() || ! unitInfo (u).hasMix ? 1.0f : clampf (mixP[(size_t) u](), 0.0f, 1.0f));
        duckG[(size_t) u] = 1.0f; duckHold[(size_t) u] = 0;
        if (onT && active[(size_t) u]) units[(size_t) u]->jumpSmoothing();   // delay-time glides land at once
    }
#if GPFX_BOX
    parBlendSm.snap (clampf (parBlendP(), 0.0f, 1.0f));
    route.snap(); space.snap(); print.snap();
    box.snapSmoothing();
#endif
}
void Rack::Impl::applyPendingOrderNow()
{
    for (int i = 0; i < kNumUnits; ++i) order[(size_t) i] = pendingOrder[(size_t) i].load (std::memory_order_relaxed);
    seenVersion = orderVersion.load (std::memory_order_acquire);
#if GPFX_BOX
    parNow = parOnP.b(); laneSeen = laneVersion.load (std::memory_order_acquire);
    for (int i = 0; i < kNumUnits; ++i) laneNow[(size_t) i] = laneOf[(size_t) i].load (std::memory_order_relaxed);
    latchRoute();
#endif
    orderFade = 0; fadePos = 0; switchWait = 0; flushLeft = 0;
}
void Rack::snapOnNextBlock() { impl->snapRequest.store (true); }

// ================================================================ Rack
Rack::Rack() : impl (std::make_unique<Impl>()) { impl->owner = this; }
Rack::~Rack() = default;

void Rack::addLateParameters (juce::AudioProcessorValueTreeState::ParameterLayout& l)
{
#if ! GPFX_BOX
    // Ghosty Pro: FATTEN (2026-10-23), added after every older parameter so no automation index, preset or saved state moves
    addBool (l, pid (uFatten, "on"), juce::String ("FX ") + unitInfo (uFatten).title + " On", false);
    addPct (l, pid (uFatten, "mix"), juce::String ("FX ") + unitInfo (uFatten).title + " Mix", 1.0f);
    addFattenParameters (l);
    // HYPER / DIMENSION (2026-10-24), after FATTEN's parameters for the same reason
    addBool (l, pid (uHyper, "on"), juce::String ("FX ") + unitInfo (uHyper).title + " On", false);
    addPct (l, pid (uHyper, "mix"), juce::String ("FX ") + unitInfo (uHyper).title + " Mix", 0.5f);
    addHyperParameters (l);
    // FX RACK (round 3, 2026-10-25): DISTORTION, after HYPER's
    addBool (l, pid (uDist, "on"), juce::String ("FX ") + unitInfo (uDist).title + " On", false);
    addPct (l, pid (uDist, "mix"), juce::String ("FX ") + unitInfo (uDist).title + " Mix", 1.0f);
    addDistParameters (l);
    // COMPRESSOR
    addBool (l, pid (uComp, "on"), juce::String ("FX ") + unitInfo (uComp).title + " On", false);
    addPct (l, pid (uComp, "mix"), juce::String ("FX ") + unitInfo (uComp).title + " Mix", 1.0f);
    addCompParameters (l);
    // SPLITTER
    addBool (l, pid (uBands, "on"), juce::String ("FX ") + unitInfo (uBands).title + " On", false);
    addPct (l, pid (uBands, "mix"), juce::String ("FX ") + unitInfo (uBands).title + " Mix", 1.0f);
    addBandsParameters (l);
    // CHORUS upgrade (4 TAPS, DELAY 2, wet LP / HP): defaults = the classic chorus
    addChorusProParameters (l);
    // TONE EQ
    addBool (l, pid (uEq3, "on"), juce::String ("FX ") + unitInfo (uEq3).title + " On", false);
    addPct (l, pid (uEq3, "mix"), juce::String ("FX ") + unitInfo (uEq3).title + " Mix", 1.0f);
    addEq3Parameters (l);
#else
    juce::ignoreUnused (l);
#endif
}

void Rack::addParameters (juce::AudioProcessorValueTreeState::ParameterLayout& l)
{
    static const float defMixPro[] { 1, 1, 1, 1, 1, 0.6f, 1, 1, 0.5f, 0.4f, 0.3f, 0.3f, 0.35f, 0.5f, 1 };   // the 15 Ghosty Pro units
    auto defMixOf = [] (int u) -> float
    {
#if GPFX_BOX
        if (u == uWarp || u == uCrackle || u == uRetro || u == uFatten) return 1.0f;
        if (u == uHalls) return 0.3f;
        if (u == uSplit || u == uBassLab || u == uFormant) return 1.0f;
        if (u == uSpectra) return 0.7f;
        if (u == uPortal) return 0.35f;
        static const int proUnits[] { uGate, uGrid, uHaunt, uWreck, uTube, uCrush, uEq, uFilter, uChorus, uCrystal, uEcho, uSpace, uShimmer, uOtt, uLim };
        for (int k = 0; k < 15; ++k) if (proUnits[k] == u) return defMixPro[k];
        return 1.0f;
#else
        if (u == uFatten) return 1.0f;
        if (u == uHyper) return 0.5f;
        if (u == uLim) return defMixPro[14];        // (the late units sit before the limiter in Ghosty Pro's unit list)
        if (u > uOtt) return 1.0f;
        return defMixPro[u];
#endif
    };
    for (int u = 0; u < kNumUnits; ++u)
    {
#if ! GPFX_BOX
        if (std::find (lateUnits().begin(), lateUnits().end(), u) != lateUnits().end()) continue;   // Ghosty Pro: the late units' parameters come last of all (addLateParameters)
#endif
        const auto& in = unitInfo (u);
        const juce::String nm = juce::String ("FX ") + juce::String (in.title).toLowerCase().replace (" ", "_").replace ("_", " ");
        addBool (l, pid (u, "on"), juce::String ("FX ") + in.title + " On", u == uLim);
        if (in.hasMix) addPct (l, pid (u, "mix"), juce::String ("FX ") + in.title + (u == uOtt ? " Depth" : " Mix"), defMixOf (u));
        juce::ignoreUnused (nm);
    }
    auto T = [] (int u, const char* s) { return juce::String ("FX ") + unitInfo (u).title + " " + s; };
    // GHOST GATE
    addInt (l, pid (uGate, "pattern"), T (uGate, "Pattern"), 1, kGatePatterns, 1);
    addPct (l, pid (uGate, "smooth"), T (uGate, "Smooth"), 0.2f);
    addPct (l, pid (uGate, "time"), T (uGate, "Time Amount"), 1.0f);
    addChoice (l, pid (uGate, "length"), T (uGate, "Length"), { "1/2 BAR", "1 BAR", "2 BARS" }, 1);
    // GHOST GRID
    addInt (l, pid (uGrid, "pattern"), T (uGrid, "Pattern"), 1, kGridPatterns, 1);
    addChoice (l, pid (uGrid, "steps"), T (uGrid, "Steps"), { "16 STEPS", "32 STEPS" }, 0);
    for (int r = 0; r < kGridRows; ++r) addPct (l, pid (uGrid, gridRow (r).key), T (uGrid, gridRow (r).name) + " " + gridRow (r).knob, gridRow (r).def);
    // HAUNT EDIT
    addInt (l, pid (uHaunt, "pad"), T (uHaunt, "Pad"), 0, kPads, 0);
    addBool (l, pid (uHaunt, "sync"), T (uHaunt, "Sync Start"), true);
    // WRECKER
    addChoice (l, pid (uWreck, "style"), T (uWreck, "Style"), wreckStyleNames(), 0);
    addPct (l, pid (uWreck, "drive"), T (uWreck, "Drive"), 0.3f);
    addFloat (l, pid (uWreck, "tone"), T (uWreck, "Tone"), juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f, "", 2);
    addFloat (l, pid (uWreck, "lowcut"), T (uWreck, "Low Cut"), skewRange (10.0f, 1000.0f, 90.0f), 10.0f, "Hz", 0);
    addFloat (l, pid (uWreck, "highcut"), T (uWreck, "High Cut"), skewRange (1000.0f, 22000.0f, 6000.0f), 22000.0f, "Hz", 0);
    addBool (l, pid (uWreck, "thump"), T (uWreck, "Thump"), false);
    addBool (l, pid (uWreck, "steep"), T (uWreck, "Steep"), false);
    addBool (l, pid (uWreck, "punish"), T (uWreck, "Punish"), false);
    addBool (l, pid (uWreck, "auto"), T (uWreck, "Auto Gain"), true);
    addFloat (l, pid (uWreck, "out"), T (uWreck, "Output"), juce::NormalisableRange<float> (-24.0f, 12.0f), 0.0f, "dB", 1);
    // TUBE GLOW
    addPct (l, pid (uTube, "drive"), T (uTube, "Drive"), 0.35f);
    addChoice (l, pid (uTube, "char"), T (uTube, "Character"), { "LINE", "MIC" }, 0);
    addFloat (l, pid (uTube, "bass"), T (uTube, "Bass"), juce::NormalisableRange<float> (-12.0f, 12.0f), 0.0f, "dB", 1);
    addFloat (l, pid (uTube, "treble"), T (uTube, "Treble"), juce::NormalisableRange<float> (-12.0f, 12.0f), 0.0f, "dB", 1);
    addFloat (l, pid (uTube, "out"), T (uTube, "Output"), juce::NormalisableRange<float> (-24.0f, 12.0f), 0.0f, "dB", 1);
    // SOUL CRUSH
    addPct (l, pid (uCrush, "crush"), T (uCrush, "Crush"), 0.5f);
    addPct (l, pid (uCrush, "dark"), T (uCrush, "Darkness"), 0.3f);
    addFloat (l, pid (uCrush, "release"), T (uCrush, "Release"), skewRange (5.0f, 1000.0f, 120.0f), 120.0f, "ms", 0);
    addFloat (l, pid (uCrush, "out"), T (uCrush, "Output"), juce::NormalisableRange<float> (-24.0f, 12.0f), 0.0f, "dB", 1);
    // SPIRIT EQ
    eq::addParameters (l);
    // PHANTOM FILTER
    addChoice (l, pid (uFilter, "type"), T (uFilter, "Type"), { "LOW PASS", "BAND PASS", "HIGH PASS", "NOTCH" }, 0);
    addChoice (l, pid (uFilter, "poles"), T (uFilter, "Slope"), { "12 dB", "24 dB" }, 1);
    addChoice (l, pid (uFilter, "routing"), T (uFilter, "Routing"), { "SINGLE", "SERIES", "PARALLEL" }, 0);
    addFloat (l, pid (uFilter, "cutoff"), T (uFilter, "Cutoff"), skewRange (20.0f, 20000.0f, 1000.0f), 1200.0f, "Hz", 0);
    addPct (l, pid (uFilter, "reso"), T (uFilter, "Resonance"), 0.4f);
    addFloat (l, pid (uFilter, "spread"), T (uFilter, "Spread"), juce::NormalisableRange<float> (-4.0f, 4.0f), 1.0f, "oct", 2);
    addPct (l, pid (uFilter, "drive"), T (uFilter, "Drive"), 0.2f);
    addFloat (l, pid (uFilter, "lfoamt"), T (uFilter, "LFO Amount"), juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f, "", 2);
    addChoice (l, pid (uFilter, "lfoshape"), T (uFilter, "LFO Shape"), { "SINE", "TRI", "SAW UP", "SAW DOWN", "SQUARE", "S&H", "SMOOTH" }, 0);
    addBool (l, pid (uFilter, "lfosync"), T (uFilter, "LFO Sync"), true);
    addChoice (l, pid (uFilter, "lfodiv"), T (uFilter, "LFO Division"), divNames (echoDivs()), 9);
    addFloat (l, pid (uFilter, "lforate"), T (uFilter, "LFO Rate"), skewRange (0.05f, 20.0f, 1.0f), 1.0f, "Hz", 2);
    addFloat (l, pid (uFilter, "envamt"), T (uFilter, "Env Amount"), juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f, "", 2);
    addFloat (l, pid (uFilter, "envrel"), T (uFilter, "Env Release"), skewRange (10.0f, 1000.0f, 200.0f), 200.0f, "ms", 0);
    // CHORUS
    addFloat (l, pid (uChorus, "rate"), T (uChorus, "Rate"), skewRange (0.05f, 5.0f, 0.6f), 0.4f, "Hz", 2);
    addPct (l, pid (uChorus, "depth"), T (uChorus, "Depth"), 0.5f);
    addFloat (l, pid (uChorus, "delay"), T (uChorus, "Delay"), juce::NormalisableRange<float> (2.0f, 30.0f), 12.0f, "ms", 1);
    addPct (l, pid (uChorus, "fb"), T (uChorus, "Feedback"), 0.0f);
    addPct (l, pid (uChorus, "width"), T (uChorus, "Width"), 1.0f);
    // CRYSTAL
    addFloat (l, pid (uCrystal, "pitch"), T (uCrystal, "Pitch"), juce::NormalisableRange<float> (-24.0f, 24.0f), 12.0f, "st", 1);
    addFloat (l, pid (uCrystal, "splice"), T (uCrystal, "Splice"), skewRange (20.0f, 600.0f, 150.0f), 180.0f, "ms", 0);
    addFloat (l, pid (uCrystal, "delay"), T (uCrystal, "Delay"), skewRange (0.0f, 1500.0f, 300.0f), 250.0f, "ms", 0);
    addPct (l, pid (uCrystal, "fb"), T (uCrystal, "Feedback"), 0.4f);
    addPct (l, pid (uCrystal, "reverse"), T (uCrystal, "Reverse"), 0.5f);
    addPct (l, pid (uCrystal, "spread"), T (uCrystal, "Spread"), 0.6f);
    // ECTO ECHO
    addChoice (l, pid (uEcho, "mode"), T (uEcho, "Mode"), { "TAPE", "ANALOG", "BBD", "DIGITAL" }, 0);
    addBool (l, pid (uEcho, "sync"), T (uEcho, "Sync"), true);
    addFloat (l, pid (uEcho, "time"), T (uEcho, "Time"), skewRange (1.0f, 2000.0f, 400.0f), 375.0f, "ms", 0);
    addChoice (l, pid (uEcho, "div"), T (uEcho, "Division"), divNames (echoDivs()), 7);
    addPct (l, pid (uEcho, "fb"), T (uEcho, "Feedback"), 0.45f);
    addBool (l, pid (uEcho, "ping"), T (uEcho, "Ping-Pong"), true);
    addPct (l, pid (uEcho, "width"), T (uEcho, "Width"), 1.0f);
    addFloat (l, pid (uEcho, "lowcut"), T (uEcho, "Low Cut"), skewRange (20.0f, 2000.0f, 200.0f), 120.0f, "Hz", 0);
    addFloat (l, pid (uEcho, "highcut"), T (uEcho, "High Cut"), skewRange (1000.0f, 20000.0f, 6000.0f), 6500.0f, "Hz", 0);
    addPct (l, pid (uEcho, "char"), T (uEcho, "Character"), 0.5f);
    addPct (l, pid (uEcho, "duck"), T (uEcho, "Ducking"), 0.0f);
    // SPECTRAL SPACE
    addPct (l, pid (uSpace, "size"), T (uSpace, "Size"), 0.5f);
    addPct (l, pid (uSpace, "blend"), T (uSpace, "Blend"), 0.6f);
    addFloat (l, pid (uSpace, "decay"), T (uSpace, "Decay"), skewRange (0.3f, 12.0f, 2.5f), 2.5f, "s", 2);
    addFloat (l, pid (uSpace, "tone"), T (uSpace, "Tone"), juce::NormalisableRange<float> (-1.0f, 1.0f), 0.0f, "", 2);
    addPct (l, pid (uSpace, "mod"), T (uSpace, "Modulation"), 0.4f);
    addPct (l, pid (uSpace, "glimmer"), T (uSpace, "Glimmer"), 0.2f);
    addPct (l, pid (uSpace, "width"), T (uSpace, "Width"), 1.0f);
    addFloat (l, pid (uSpace, "predelay"), T (uSpace, "Pre-delay"), skewRange (0.0f, 250.0f, 40.0f), 20.0f, "ms", 0);
    addPct (l, pid (uSpace, "duck"), T (uSpace, "Ducking"), 0.0f);
    // SHIMMER
    addPct (l, pid (uShimmer, "size"), T (uShimmer, "Size"), 0.7f);
    addFloat (l, pid (uShimmer, "decay"), T (uShimmer, "Decay"), skewRange (0.5f, 30.0f, 5.0f), 5.0f, "s", 1);
    addPct (l, pid (uShimmer, "damp"), T (uShimmer, "Damping"), 0.4f);
    addPct (l, pid (uShimmer, "shimmer"), T (uShimmer, "Shimmer"), 0.5f);
    addFloat (l, pid (uShimmer, "predelay"), T (uShimmer, "Pre-delay"), skewRange (0.0f, 250.0f, 40.0f), 30.0f, "ms", 0);
    addPct (l, pid (uShimmer, "mod"), T (uShimmer, "Modulation"), 0.5f);
    addPct (l, pid (uShimmer, "width"), T (uShimmer, "Width"), 1.0f);
    addBool (l, pid (uShimmer, "freeze"), T (uShimmer, "Freeze"), false);
    // OTT
    addPct (l, pid (uOtt, "time"), T (uOtt, "Time"), 0.5f);
    addPct (l, pid (uOtt, "up"), T (uOtt, "Upward"), 0.6f);
    addPct (l, pid (uOtt, "down"), T (uOtt, "Downward"), 0.7f);
    addFloat (l, pid (uOtt, "in"), T (uOtt, "Input"), juce::NormalisableRange<float> (-12.0f, 12.0f), 0.0f, "dB", 1);
    addFloat (l, pid (uOtt, "out"), T (uOtt, "Output"), juce::NormalisableRange<float> (-12.0f, 12.0f), 0.0f, "dB", 1);
    // GHOST LIMITER
    addFloat (l, pid (uLim, "ceiling"), T (uLim, "Ceiling"), juce::NormalisableRange<float> (-12.0f, 0.0f), -0.3f, "dB", 1);
    addFloat (l, pid (uLim, "gain"), T (uLim, "Gain"), juce::NormalisableRange<float> (0.0f, 18.0f), 0.0f, "dB", 1);
    addFloat (l, pid (uLim, "release"), T (uLim, "Release"), skewRange (1.0f, 500.0f, 80.0f), 80.0f, "ms", 0);
#if GPFX_BOX
    addBoxParameters (l);
    addSplitParameters (l);
    addBoxUnits2Parameters (l);
    addBool (l, "fx_par_on", "FX Parallel Lanes", false);   // fx_ so scenes set / reset it like any rack setting
    addPct (l, "fx_par_blend", "FX Parallel Blend", 0.5f);
    // Ghost Box 3 layer (prefix box_, so scenes / presets never touch them)
    addPct (l, "box_morph", "Morph", 0.0f);
    addBool (l, "box_fixlat", "Fixed Latency", true);
    addBool (l, "box_mo_reset", "Motion Reset On Start", false);
    for (int k = 0; k < kMacros; ++k)
        addPct (l, "box_macro" + juce::String (k + 1), "Macro " + juce::String (k + 1) + " (" + juce::String (macroDefaultNames()[k]).toLowerCase() + ")", 0.0f);
    for (int k = 0; k < kLanes; ++k)
    {
        const juce::String b = "box_mo_" + juce::String (laneKey (k)) + "_", nm = juce::String ("Motion ") + juce::String (laneName (k)).toLowerCase() + " ";
        addBool (l, b + "on", nm + "On", false);
        addPct (l, b + "depth", nm + "Depth", 1.0f);
        addChoice (l, b + "len", nm + "Length", motionLenNames(), 2);
        addChoice (l, b + "feel", nm + "Feel", motionFeelNames(), 0);
        addChoice (l, b + "mode", nm + "Mode", motionModeNames(), 0);
        addChoice (l, b + "steps", nm + "Steps", motionStepsNames(), 1);
        addPct (l, b + "phase", nm + "Phase", 0.0f);
    }
    // SPIRIT BOARD: the puck (automatable X / Y) and its orbit
    addPct (l, "box_board_x", "Spirit Board X", 0.5f);
    addPct (l, "box_board_y", "Spirit Board Y", 0.5f);
    addChoice (l, "box_orbit", "Spirit Board Orbit", boardOrbitNames(), 0);
    addChoice (l, "box_orbit_rate", "Spirit Board Orbit Rate", boardOrbitRateNames(), 3);
    addPct (l, "box_orbit_depth", "Spirit Board Orbit Depth", 0.25f);
#endif
}

void Rack::attach (juce::AudioProcessorValueTreeState& s)
{
    impl->apvts = &s;
#if GPFX_BOX
    ShadowBinder::current() = &impl->box;   // every unit parameter is read through a MORPH / MOTION / MACRO shadow
#endif
    for (int u = 0; u < kNumUnits; ++u)
    {
        impl->onP[(size_t) u].bind (s, pid (u, "on"), u == uLim ? 1.0f : 0.0f);
        if (unitInfo (u).hasMix) impl->mixP[(size_t) u].bind (s, pid (u, "mix"), 1.0f);
        impl->units[(size_t) u]->attach (s);
    }
#if GPFX_BOX
    impl->route.attach (s);   // GHOST SPLIT (its settings ride the same shadows: MORPH, SCENES, DNA treat them as rack settings)
    impl->space.attach (s); impl->print.attach (s);   // GHOST SPACE + the GHOSTPRINT targets (the same: rack settings, timeline lanes can move them)
    ShadowBinder::current() = nullptr;
    impl->box.finishAttach (s);
    impl->box.scenes.attach (impl->box.sceneSlotInfo(), &impl->pendingOrder, &impl->orderVersion, &impl->dataVersion);   // GHOST SCENES
    impl->box.scenes.sceneParam = s.getRawParameterValue ("box_scene"); impl->box.scenes.morphParam = s.getRawParameterValue ("box_scene_morph");
    impl->box.scenes.mixParam = s.getRawParameterValue ("box_mix");
    impl->parOnP.bind (s, "fx_par_on", 0); impl->parBlendP.bind (s, "fx_par_blend", 0.5f); impl->fixLatP.bind (s, "box_fixlat", 1);
#endif
    impl->collectParams (s);
    impl->attached = true;
}

void Rack::prepare (double sampleRate, int)
{
    auto& d = *impl;
    d.sr = sampleRate > 0 ? sampleRate : 48000.0;
    for (auto& u : d.units) u->prepare (d.sr, kSub);
#if GPFX_BOX
    d.box.prepare (d.sr);
    d.laneDelA.prepare (16384, d.sr); d.laneDelB.prepare (16384, d.sr); d.padDelay.prepare (16384, d.sr); d.padNow = 0;
    d.parBlendSm.setTime (d.sr, 30.0f); d.parBlendSm.snap (d.attached ? d.parBlendP() : 0.5f);
    d.parNow = d.attached && d.parOnP.b(); d.laneSeen = d.laneVersion.load();
    d.spG.fill (1.0f); d.spOut.fill (1.0f); d.spQuiet.fill (0); d.spAge.fill (0);   // GHOST SCENES SPILL
    d.route.prepare (d.sr); d.latchRoute();                                                // GHOST SPLIT
    d.space.prepare (d.sr); d.print.prepare (d.sr);                                        // GHOST SPACE, GHOSTPRINT targets
    for (int i = 0; i < kNumUnits; ++i) d.laneNow[(size_t) i] = d.laneOf[(size_t) i].load();
#endif
    for (int u = 0; u < kNumUnits; ++u)
    {
        d.onSm[(size_t) u].setLen ((int) (d.sr * 0.015));
        d.onSm[(size_t) u].snap (d.attached && d.onP[(size_t) u].b() ? 1.0f : 0.0f);
        d.mixSm[(size_t) u].setTime (d.sr, 25.0f);
        d.mixSm[(size_t) u].snap (d.attached && unitInfo (u).hasMix ? d.mixP[(size_t) u]() : 1.0f);
        d.active[(size_t) u] = false;
    }
#if ! GPFX_BOX
    for (auto& x : d.xfRing) x.prepare ((int) (d.sr * 0.25) + kSub + 64);   // FX RACK (round 3): reorder crossfade (chain latency up to 250 ms)
    { const juce::ScopedLock sl (d.slotLock); for (auto& u : d.slotPool) u->prepare (d.sr, kSub); }   // SPLITTER slots (audio is stopped here)
#endif
    d.cmA = 1.0f - (float) std::exp (-1.0 / (0.2 * d.sr)); d.cmGa = 1.0f - (float) std::exp (-1.0 / (0.025 * d.sr));
    for (int u = 0; u < kNumUnits; ++u) { d.cmIn[(size_t) u] = d.cmOut[(size_t) u] = 0.0f; d.cmG[(size_t) u] = 1.0f; }
    d.prepared = true;
}
void Rack::setCleanMatch (bool on) noexcept { impl->cleanMatch.store (on, std::memory_order_relaxed); }

void Rack::reset()
{   // a full reset: every unit's state AND every delay / latency line in the path (unit dry lines, lookahead, lane alignment, the
    // FIXED LATENCY pad), so no old audio resumes afterwards. Audio-thread safe (no allocation), so a plugin may call it when it
    // releases its own bypass / mute.
    auto& d = *impl;
    for (auto& u : d.units) { u->reset(); u->clearLines(); }
#if GPFX_BOX
    d.laneDelA.clear(); d.laneDelB.clear(); d.padDelay.clear(); d.route.reset(); d.space.reset(); d.print.reset();
#endif
    if (d.orderFade == 3) { d.orderFade = 2; d.fadePos = 0; d.flushLeft = 0; }   // nothing old left to flush
#if ! GPFX_BOX
    for (auto& x : d.xfRing) x.clear();
#endif
    d.freePpq = 0;
}

void Rack::process (juce::AudioBuffer<float>& buffer, const Transport& t)
{
    auto& d = *impl;
    if (! d.prepared || ! d.attached || buffer.getNumChannels() == 0) return;
    juce::ScopedNoDenormals noDenormals;
    const int N = buffer.getNumSamples();
    if (N <= 0) return;
    float* L = buffer.getWritePointer (0);
    float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;
    {   // snap triggers: host state restore, transport start (stopped -> playing), or a relocate / loop jump after an idle gap
        const double nowMs = juce::Time::getMillisecondCounterHiRes();
        const bool gap = d.lastProcessMs >= 0.0 && nowMs - d.lastProcessMs > 200.0;
        const bool startEdge = t.playing && ! d.wasPlaying;
        const bool jumped = t.playing && std::isfinite (t.ppq) && d.expectedPpq >= 0.0 && std::abs (t.ppq - d.expectedPpq) > 0.25;
#if GPFX_BOX
        if (d.snapRequest.exchange (false)) d.snapPendingChunk = d.snapOrderPending = true;
        if (startEdge || (gap && jumped)) d.snapPendingChunk = true;
#else
        juce::ignoreUnused (gap, startEdge, jumped);   // Ghosty Pro: only an explicit snapOnNextBlock (its presets / sessions render as before)
        if (d.snapRequest.exchange (false)) d.snapPendingChunk = true;
#endif
        d.wasPlaying = t.playing; d.lastProcessMs = nowMs;
    }

    BlockCtx ctx;
    ctx.sr = d.sr;
    ctx.bpm = (t.bpm > 1.0 && std::isfinite (t.bpm)) ? juce::jlimit (20.0, 400.0, t.bpm) : 120.0;
    ctx.samplesPerBeat = 60.0 / ctx.bpm * d.sr;
    ctx.playing = t.playing;
#if GPFX_BOX
    ctx.beatsPerBar = t.beatsPerBar > 0.0 && std::isfinite (t.beatsPerBar) ? juce::jlimit (0.5, 64.0, t.beatsPerBar) : 4.0;
#endif
    double ppq0;
    if (t.playing && std::isfinite (t.ppq)) ppq0 = t.ppq;
    else ppq0 = d.freePpq;
    d.freePpq = ppq0 + (double) N / ctx.samplesPerBeat;
    d.expectedPpq = ppq0 + (double) N / ctx.samplesPerBeat;

    float pkL = 0, pkR = 0;
#if GPFX_BOX
    d.box.beginBlock();
    int chunk = d.box.chunkSize();
#else
    const int chunk = kSub;
#endif
    for (int off = 0, n = 0; off < N; off += n)
    {
        n = std::min (chunk, N - off);
#if GPFX_BOX
        n = d.box.scenes.chunkLimit (ppq0 + (double) off / ctx.samplesPerBeat, ctx.samplesPerBeat, n);   // GHOST SCENES: a chunk starts on a landing's grid sample
        n = d.box.tlChunkLimit (ppq0 + (double) off / ctx.samplesPerBeat, ctx.samplesPerBeat, ctx.beatsPerBar, n);   // GHOST MOTION timeline: ... and on the loop restart
#endif
        ctx.ppq = ppq0 + (double) off / ctx.samplesPerBeat;
        ctx.noteOns = (t.noteOns > 0 && t.noteOnAt >= off && t.noteOnAt < off + n) ? t.noteOns : 0;     // the note-on lands in this sub-block ...
        ctx.noteOnAt = ctx.noteOns > 0 ? t.noteOnAt - off : -1;                                         // ... at this sample of it
#if ! GPFX_BOX
        if (t.lastNote >= 0 && off == 0) d.lastNote = t.lastNote;                                         // FX RACK (round 3): KEY TRACK
        ctx.lastNote = d.lastNote;
#endif
        float* l = L + off;
        float* r = R != nullptr ? R + off : d.monoR.data();
        if (R == nullptr) std::copy (l, l + n, r);
#if GPFX_BOX
        d.sc.off = off;
        d.box.measureIn (l, r, n);
        d.box.feedOnsets (l, r, n, ctx);                    // GHOST MOTION timeline: the input onset feed (only while the page or a timeline needs it)
        if (d.box.timelineActive()) d.upstreamLatency (d.box.upLat);
        d.box.beginChunk (ctx, n, d.onP);
        if (d.snapPendingChunk) { d.snapSmoothers(); d.snapPendingChunk = false; }   // after the shadows refreshed for this chunk
        if (d.snapOrderPending) { d.applyPendingOrderNow(); d.snapOrderPending = false; }
        d.processChunk (l, r, n, ctx);
        chunk = d.box.chunkSize();
#else
        if (d.snapPendingChunk) { d.snapSmoothers(); d.snapPendingChunk = false; }
        d.processChunk (l, r, n, ctx);
#endif
        if (R == nullptr) for (int i = 0; i < n; ++i) l[i] = 0.5f * (l[i] + r[i]);
        for (int i = 0; i < n; ++i) { pkL = std::max (pkL, std::abs (l[i])); pkR = std::max (pkR, std::abs (r[i])); }
    }
#if GPFX_BOX
    d.box.endBlock();
    {   // FIXED LATENCY: pad the output so it always arrives at the reported latency
        // always run (cheap): the ring stays current, and a change of the pad crossfades to the end even when the new pad is 0
        // (skipping at pad 0 cut that crossfade short = a hard step, and left stale audio in the ring for the next pad > 0)
        const int pad = std::max (0, d.reportedLatency() - d.actualLatency());
        d.padDelay.process (L, R != nullptr ? R : d.monoR.data(), N, std::min (pad, 16384)); d.padNow = pad;
    }
#endif
    d.tm.outPeakL.store (pkL); d.tm.outPeakR.store (pkR);
    d.blockMs.store ((float) (1000.0 * N / d.sr), std::memory_order_relaxed);
    d.processCount.fetch_add (1, std::memory_order_release);
}

#if GPFX_BOX
void Rack::Impl::upstreamLatency (std::array<int, kNumUnits>& up) const noexcept
{   // audio thread: the latency each unit's input already carries (the units before it on its own path)
    int a = 0, b = 0;
    if (routeActiveNow)
    {   // GHOST SPLIT: the stage at its first unit; inside a branch, the branch's earlier units; after it, the slowest branch
        const int nb = routeBranchCount (routeModeNow, routeBandsNow); bool done = false;
        for (int k = 0; k < kNumUnits; ++k)
        {
            const int u = order[(size_t) k];
            if (u != uLim && routeNow[(size_t) u] > 0)
            {
                if (done) continue;
                done = true; std::array<int, kRouteBranches> bl {};
                for (int j = k; j < kNumUnits; ++j)
                {
                    const int uj = order[(size_t) j]; if (uj == uLim || routeNow[(size_t) uj] == 0) continue;
                    const int br = std::min (routeNow[(size_t) uj], nb) - 1;
                    up[(size_t) uj] = a + bl[(size_t) br]; bl[(size_t) br] += units[(size_t) uj]->latency();
                }
                int mx = 0; for (int x = 0; x < nb; ++x) mx = std::max (mx, bl[(size_t) x]);
                a += std::min (mx, 16384);
                continue;
            }
            up[(size_t) u] = a; a += units[(size_t) u]->latency();
        }
        return;
    }
    for (int k = 0; k < kNumUnits; ++k)
    {
        const int u = order[(size_t) k]; const int lat = units[(size_t) u]->latency();
        if (parNow && u != uLim && laneNow[(size_t) u] == 1) { up[(size_t) u] = b; b += lat; }
        else { if (u == uLim && parNow) a = std::max (a, b); up[(size_t) u] = a; a += lat; }
    }
}
int Rack::Impl::actualLatency() const
{
    int total = 0;
    if (routeRouting())
    {   // GHOST SPLIT: the slowest branch counts once (the others are delayed to match), every other unit as usual
        const int nb = routeBranchCount (route.mode.i(), route.bands.i());
        std::array<int, kRouteBranches> bl {};
        for (int u = 0; u < kNumUnits; ++u)
        {
            const int r = u == uLim ? 0 : routeOf[(size_t) u].load();
            if (r > 0) bl[(size_t) (std::min (r, nb) - 1)] += units[(size_t) u]->latency(); else total += units[(size_t) u]->latency();
        }
        int mx = 0; for (int b = 0; b < nb; ++b) mx = std::max (mx, bl[(size_t) b]);
        return total + std::min (mx, 16384);
    }
    if (attached && parOnP.b())
    {   // parallel: the longer lane sets the latency (the other one is delayed to match), plus the limiter
        int la = 0, lb = 0;
        for (int u = 0; u < kNumUnits; ++u) if (u != uLim) (laneOf[(size_t) u].load() == 1 ? lb : la) += units[(size_t) u]->latency();
        return std::max (la, lb) + units[uLim]->latency();
    }
    for (auto& u : units) total += u->latency();
    return total;
}
int Rack::Impl::fixedLatency() const
{   // the always-there latencies (oversampled colour units, the limiter) plus the biggest single latent unit
    int constant = 0, biggest = 0;
    for (int u = 0; u < kNumUnits; ++u)
    {
        if (u == uSpectra) biggest = std::max (biggest, static_cast<SpectraUnit*> (units[(size_t) u].get())->maxLatency());
        else if (u == uWarp) biggest = std::max (biggest, static_cast<WarpUnit*> (units[(size_t) u].get())->maxLatency());
        else if (u == uGate) biggest = std::max (biggest, static_cast<BoxGateUnit*> (units[(size_t) u].get())->maxLatency());
        else constant += units[(size_t) u]->latency();
    }
    return constant + biggest;
}
int Rack::Impl::reportedLatency() const
{
    const int actual = actualLatency();
    if (! attached || ! fixLatP.b()) return actual;
    return std::max (actual, fixedLatency());   // two latent units at once: the figure rises once, it never drops below the fixed one
}
#endif
int Rack::latencySamples() const
{
#if GPFX_BOX
    return impl->reportedLatency();
#else
    int total = 0;
    for (auto& u : impl->units) total += u->latency();
    return total;
#endif
}
#if GPFX_BOX
void Rack::setSidechain (const float* l, const float* r, int n) { impl->sc.l = l; impl->sc.r = r; impl->sc.n = n; impl->sc.connected = l != nullptr; }
void Rack::setOffline (bool o) { impl->offline.store (o); }
#endif

// ================================================================ state
namespace {
juce::String gatePointsToString (const GatePattern& p)
{
    juce::StringArray s;
    for (auto& pt : p.pts) s.add (juce::String (pt.x, 5) + "," + juce::String (pt.y, 5) + "," + juce::String (pt.t, 4));
    return s.joinIntoString (";");
}
GatePattern gateFromTree (const juce::ValueTree& t, const GatePattern& fallback)
{
    GatePattern p = fallback;
    p.name = t.getProperty ("name", fallback.name).toString();
    auto pts = juce::StringArray::fromTokens (t.getProperty ("points").toString(), ";", "");
    std::vector<GatePoint> v;
    for (auto& s : pts)
    {
        auto c = juce::StringArray::fromTokens (s, ",", "");
        if (c.size() >= 2) v.push_back ({ clampf (c[0].getFloatValue(), 0, 1), clampf (c[1].getFloatValue(), 0, 1), c.size() > 2 ? clampf (c[2].getFloatValue(), -1, 1) : 0.0f });
    }
    if (v.size() >= 2)
    {
        std::sort (v.begin(), v.end(), [] (const GatePoint& a, const GatePoint& b) { return a.x < b.x; });
        v.front().x = 0; v.back().x = 1; p.pts = v;
    }
    auto st = t.getProperty ("steps").toString();
    for (int k = 0; k < kGateSteps && k < st.length(); ++k) p.steps[(size_t) k] = (uint8_t) juce::jlimit (0, kNumTimeModes - 1, (int) (st[k] - '0'));
    return p;
}
}

juce::ValueTree Rack::saveState() const
{
    auto& d = *impl;
    juce::ValueTree t ("GHOSTFX"); t.setProperty ("version", 1, nullptr);
    juce::ValueTree gates ("GATE");
    for (int i = 0; i < kGatePatterns; ++i)
    {
        auto p = d.getGatePattern (i);
        juce::ValueTree c ("P"); c.setProperty ("i", i, nullptr); c.setProperty ("name", p.name, nullptr);
        c.setProperty ("points", gatePointsToString (p), nullptr);
        juce::String st; for (auto s : p.steps) st << juce::String::charToString ((juce::juce_wchar) ('0' + s));
        c.setProperty ("steps", st, nullptr);
        gates.appendChild (c, nullptr);
    }
    t.appendChild (gates, nullptr);
    juce::ValueTree grids ("GRID");
    for (int i = 0; i < kGridPatterns; ++i)
    {
        auto p = d.getGridPattern (i);
        juce::ValueTree c ("P"); c.setProperty ("i", i, nullptr); c.setProperty ("name", p.name, nullptr);
        juce::StringArray rows; for (auto r : p.rows) rows.add (juce::String::toHexString ((juce::int64) r));
        c.setProperty ("rows", rows.joinIntoString (","), nullptr);
        grids.appendChild (c, nullptr);
    }
    t.appendChild (grids, nullptr);
    juce::ValueTree pads ("PADS");
    for (int i = 0; i < kPads; ++i)
    {
        auto p = d.getPad (i);
        juce::ValueTree c ("P"); c.setProperty ("i", i, nullptr); c.setProperty ("type", p.type, nullptr); c.setProperty ("div", p.div, nullptr); c.setProperty ("motion", p.motion, nullptr);
        pads.appendChild (c, nullptr);
    }
    t.appendChild (pads, nullptr);
    juce::StringArray ord; for (int u : d.getOrder()) ord.add (unitInfo (u).key);
#if ! GPFX_BOX
    // Ghosty Pro: FATTEN at its default place (just before the limiter) is left out, so a sound that never moved it saves
    // exactly as before; loading puts a missing FATTEN back there
    // (HYPER likewise, 2026-10-24; FX RACK round 3: every later unit too). Walking the late units from the newest back, each one
    // that sits just before the limiter is dropped: loading appends the missing ones in unit order, which gives back the same chain.
    for (int k = (int) lateUnits().size() - 1; k >= 0; --k)
        if (ord.size() >= 2 && ord[ord.size() - 2] == unitInfo (lateUnits()[(size_t) k]).key) ord.remove (ord.size() - 2);
#endif
    t.setProperty ("order", ord.joinIntoString (","), nullptr);
#if ! GPFX_BOX
    {   // FX RACK (round 3): DISTORTION's X-SHAPER curves, saved only once they differ from the factory curves (older states stay as they were)
        const auto c = d.distCurves.get();
        if (! (c == DistCurves::defaults()))
        {
            juce::ValueTree x ("DISTCURVES"); x.setProperty ("a", DistCurves::toString (c.a), nullptr); x.setProperty ("b", DistCurves::toString (c.b), nullptr);
            x.setProperty ("sym", c.symmetric, nullptr); t.appendChild (x, nullptr);
        }
    }
#endif
#if ! GPFX_BOX
    {   // FX RACK (round 3): SPLITTER slots, saved only when any is set
        juce::StringArray keys; bool any = false;
        for (int b = 0; b < kBands; ++b) for (int k = 0; k < kSlots; ++k) { const int u = d.bandSlot (b, k); keys.add (u >= 0 ? juce::String (unitInfo (u).key) : juce::String()); any = any || u >= 0; }
        if (any) { juce::ValueTree x ("BANDS"); x.setProperty ("slots", keys.joinIntoString (","), nullptr); t.appendChild (x, nullptr); }
    }
#endif
    auto eqTree = eq::saveState (*d.eqUnit);
    if (eqTree.isValid()) t.appendChild (eqTree, nullptr);
    {
        const juce::ScopedLock sl (d.presetLock);
        t.setProperty ("scene", d.sceneName, nullptr); t.setProperty ("sceneEdited", d.sceneEdited, nullptr);
        juce::ValueTree pr ("PRESETS");
        for (int u = 0; u < kNumUnits; ++u)
            if (d.unitPreset[(size_t) u].isNotEmpty())
            {
                juce::ValueTree c ("U"); c.setProperty ("unit", unitInfo (u).key, nullptr);
                c.setProperty ("name", d.unitPreset[(size_t) u], nullptr); c.setProperty ("edited", d.unitEdited[(size_t) u], nullptr);
                pr.appendChild (c, nullptr);
            }
        t.appendChild (pr, nullptr);
    }
#if GPFX_BOX
    {
        const auto w = d.warpShape.get();
        juce::ValueTree c ("WARP"); c.setProperty ("points", w.toString(), nullptr); c.setProperty ("snap", w.snap, nullptr); c.setProperty ("grid", w.grid, nullptr);
        t.appendChild (c, nullptr);
    }
    t.appendChild (d.box.save(), nullptr);
    for (auto* nm : { "FORMANTPATH", "PUMPCURVE" })
    {
        const auto sh = (juce::String (nm) == "FORMANTPATH" ? d.formantShape : d.pumpShape).get();
        juce::ValueTree c (nm); c.setProperty ("points", sh.toString(), nullptr); c.setProperty ("grid", sh.grid, nullptr); t.appendChild (c, nullptr);
    }
    {
        juce::StringArray b; for (int u = 0; u < kNumUnits; ++u) if (d.laneOf[(size_t) u].load() == 1) b.add (unitInfo (u).key);
        t.setProperty ("laneB", b.joinIntoString (","), nullptr);
    }
    {   // GHOST SPLIT: which unit runs in which branch (saved only when any is assigned: older states stay as they were)
        juce::StringArray r; for (int u = 0; u < kNumUnits; ++u) if (const int br = d.routeOf[(size_t) u].load(); br > 0) r.add (juce::String (unitInfo (u).key) + ":" + juce::String (br));
        if (! r.isEmpty()) t.setProperty ("route", r.joinIntoString (","), nullptr);
    }
    if (d.spectraMask.any()) t.setProperty ("spectraMask", d.spectraMask.toString(), nullptr);
    if (! d.space.drawnPath.empty()) { auto c = d.space.drawnPath.toTree (0); c = c.createCopy(); juce::ValueTree x ("GSPACEPATH"); x.copyPropertiesFrom (c, nullptr); t.appendChild (x, nullptr); }   // GHOST SPACE: the DRAWN path (only when drawn)
#endif
    return t;
}

void Rack::loadState (const juce::ValueTree& tree)
{
    auto& d = *impl;
    if (! tree.isValid() || ! tree.hasType ("GHOSTFX")) return;
    if (auto gates = tree.getChildWithName ("GATE"); gates.isValid())
        for (auto c : gates)
        {
            const int i = c.getProperty ("i", -1);
            if (i >= 0 && i < kGatePatterns) d.setGatePattern (i, gateFromTree (c, factoryGatePattern (i)));
        }
    if (auto grids = tree.getChildWithName ("GRID"); grids.isValid())
        for (auto c : grids)
        {
            const int i = c.getProperty ("i", -1);
            if (i < 0 || i >= kGridPatterns) continue;
            GridPattern p = factoryGridPattern (i);
            p.name = c.getProperty ("name", p.name).toString();
            auto rows = juce::StringArray::fromTokens (c.getProperty ("rows").toString(), ",", "");
            for (int r = 0; r < kGridRows && r < rows.size(); ++r) p.rows[(size_t) r] = (uint32_t) rows[r].getHexValue64();
            d.setGridPattern (i, p);
        }
    if (auto pads = tree.getChildWithName ("PADS"); pads.isValid())
        for (auto c : pads)
        {
            const int i = c.getProperty ("i", -1);
            if (i < 0 || i >= kPads) continue;
            PadDef p { juce::jlimit (0, kNumHauntTypes - 1, (int) c.getProperty ("type", 0)), juce::jlimit (0, (int) padDivs().size() - 1, (int) c.getProperty ("div", 7)),
                       clampf ((float) c.getProperty ("motion", 0.0f), 0.0f, 1.0f) };
            d.setPad (i, p);
        }
    if (tree.hasProperty ("order"))
    {
        auto keys = juce::StringArray::fromTokens (tree.getProperty ("order").toString(), ",", "");
        std::array<int, kNumUnits> o; o.fill (-1); int k = 0;
        for (auto& key : keys)
            for (int u = 0; u < kNumUnits; ++u)
                if (key == unitInfo (u).key && k < kNumUnits) o[(size_t) k++] = u;
        d.setOrder (o);
    }
    for (auto c : tree)
        if (c.hasType ("EQ")) eq::loadState (*d.eqUnit, c);
#if ! GPFX_BOX
    {   // FX RACK (round 3): X-SHAPER curves (absent = the factory curves)
        auto c = DistCurves::defaults();
        if (auto x = tree.getChildWithName ("DISTCURVES"); x.isValid())
        { DistCurves::fromString (c.a, x.getProperty ("a").toString()); DistCurves::fromString (c.b, x.getProperty ("b").toString()); c.symmetric = (bool) x.getProperty ("sym", true); }
        if (! (c == d.distCurves.get())) d.distCurves.set (c);
    }
    {   // SPLITTER slots (absent = all empty)
        const auto keys = juce::StringArray::fromTokens (tree.getChildWithName ("BANDS").getProperty ("slots").toString(), ",", "");
        for (int b = 0; b < kBands; ++b)
            for (int k = 0; k < kSlots; ++k)
            {
                const auto key = keys[b * kSlots + k]; int want = -1;
                for (int u : bandSlotUnits()) if (key == unitInfo (u).key) want = u;
                d.setBandSlot (b, k, want);
            }
    }
#endif
#if GPFX_BOX
    if (auto w = tree.getChildWithName ("WARP"); w.isValid())
        d.warpShape.set (bendlane::Shape::fromString (w.getProperty ("points").toString(), (int) w.getProperty ("snap", 1), (int) w.getProperty ("grid", 2)));
    else d.warpShape.set (bendlane::defaultShape());
    d.box.load (tree);
    for (auto* nm : { "FORMANTPATH", "PUMPCURVE" })
        if (auto c = tree.getChildWithName (nm); c.isValid())
            (juce::String (nm) == "FORMANTPATH" ? d.formantShape : d.pumpShape).set (bendlane::Shape::fromString (c.getProperty ("points").toString(), 0, (int) c.getProperty ("grid", 2)));
    {
        const auto keys = juce::StringArray::fromTokens (tree.getProperty ("laneB").toString(), ",", "");
        bool changed = false;
        for (int u = 0; u < kNumUnits; ++u) { const int want = keys.contains (unitInfo (u).key) ? 1 : 0; if (d.laneOf[(size_t) u].load() != want) { d.laneOf[(size_t) u].store (want); changed = true; } }
        if (changed) d.laneVersion.fetch_add (1);
    }
    {   // GHOST SPLIT branches (absent = every unit in the main chain)
        std::array<int, kNumUnits> want {};
        for (auto& tok : juce::StringArray::fromTokens (tree.getProperty ("route").toString(), ",", ""))
            for (int u = 0; u < kNumUnits; ++u)
                if (u != uLim && tok.upToFirstOccurrenceOf (":", false, false) == unitInfo (u).key) want[(size_t) u] = juce::jlimit (0, kRouteBranches, tok.fromFirstOccurrenceOf (":", false, false).getIntValue());
        bool changed = false;
        for (int u = 0; u < kNumUnits; ++u) if (d.routeOf[(size_t) u].load() != want[(size_t) u]) { d.routeOf[(size_t) u].store (want[(size_t) u]); changed = true; }
        if (changed) { d.routeVersion.fetch_add (1); d.dataVersion.fetch_add (1); }
    }
    if (tree.hasProperty ("spectraMask")) d.spectraMask.fromString (tree.getProperty ("spectraMask").toString()); else d.spectraMask.clear();
    {   // GHOST SPACE: the DRAWN path (absent = none)
        const auto x = tree.getChildWithName ("GSPACEPATH");
        const auto np = x.isValid() ? ghosty::spirit::OrbitPath::fromTree (x) : ghosty::spirit::OrbitPath();
        if (np.empty() != d.space.drawnPath.empty() || ! np.empty()) d.setSpacePath (np);
    }
#endif
    {
        const juce::ScopedLock sl (d.presetLock);
        d.sceneName = tree.getProperty ("scene").toString(); d.sceneEdited = (bool) tree.getProperty ("sceneEdited", false);
        for (int u = 0; u < kNumUnits; ++u) { d.unitPreset[(size_t) u] = {}; d.unitEdited[(size_t) u] = false; }
        for (auto c : tree.getChildWithName ("PRESETS"))
            for (int u = 0; u < kNumUnits; ++u)
                if (c.getProperty ("unit").toString() == unitInfo (u).key)
                { d.unitPreset[(size_t) u] = c.getProperty ("name").toString(); d.unitEdited[(size_t) u] = (bool) c.getProperty ("edited", false); }
    }
    d.baselinePending.store (true);
}

// ================================================================ scenes
void Rack::applyScene (juce::AudioProcessorValueTreeState& s, const juce::String& sceneName)
{
    const auto scene = sceneName.trim().toLowerCase();
#if GPFX_BOX
    if (impl->brainAudition >= 0) brain::endAudition (*impl, true);   // a scene replaces the auditioned chain: keep its loudness, end the audition
#endif
    impl->pushUndo();   // every scene load can be undone (also the ones the synth's presets trigger)
    // targets are collected first and written once each at the end, so the audio thread never sees a parameter
    // bounce through its default on the way to the scene's value
    std::map<juce::RangedAudioParameter*, float> tgt;
    struct Done
    {
        Impl& d; juce::String k; std::map<juce::RangedAudioParameter*, float>& t;
        ~Done()
        {
            std::array<bool, kNumUnits> all; all.fill (true);
            d.duckAround (all, [this]
            {
#if GPFX_BOX
                d.box.setMorphOn (false);   // a new scene replaces the sound: MORPH lets go inside the duck (UNDO brings it back)
                d.box.setBoardOn (false);   // ... and so does the SPIRIT BOARD (its ghosts stay)
#endif
                for (auto& [p, v] : t) d.setNorm (p, v);
            });
            d.afterFactoryScene (k);
        }
    } done { *impl, scene, tgt };
    // 1) everything back to defaults and off (the master limiter keeps its settings)
    for (auto* p : s.processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (rp->getParameterID().startsWith ("fx_") && ! rp->getParameterID().startsWith ("fx_lim_"))
                tgt[rp] = rp->getDefaultValue();
    auto set = [&tgt, &s] (const juce::String& id, float v)
    {
        if (auto* p = s.getParameter (id)) tgt[p] = p->convertTo0to1 (v);
    };
    auto on = [&] (int u, float mix = -1.0f) { set (pid (u, "on"), 1.0f); if (mix >= 0 && unitInfo (u).hasMix) set (pid (u, "mix"), mix); };
    auto eqBand = [&] (int k, int type, float freq, float gain = 0.0f, float q = 0.71f, int slope = 1)
    {
        const juce::String b = "fx_eq_b" + juce::String (k) + "_";
        set (b + "on", 1); set (b + "type", (float) type); set (b + "freq", freq); set (b + "gain", gain); set (b + "q", q); set (b + "slope", (float) slope);
    };
    auto P_ = [] (int u, const char* x) { return pid (u, x); };

#if GPFX_BOX
    for (int u = 0; u < kNumUnits; ++u) impl->laneOf[(size_t) u].store (0);   // every scene starts with one lane
    impl->laneVersion.fetch_add (1);
    {   // ... and no GHOST SPLIT branches (only a real change goes through the switch fade)
        bool ch = false; for (int u = 0; u < kNumUnits; ++u) if (impl->routeOf[(size_t) u].exchange (0) != 0) ch = true;
        if (ch) impl->routeVersion.fetch_add (1);
    }
    impl->spectraMask.clear();
#endif
    if (scene.isEmpty() || scene == "dry") return;
    if (scene == "wide")
    {
        on (uChorus, 0.45f); set (P_ (uChorus, "rate"), 0.35f); set (P_ (uChorus, "depth"), 0.45f); set (P_ (uChorus, "width"), 1.0f);
        on (uSpace, 0.15f); set (P_ (uSpace, "size"), 0.3f); set (P_ (uSpace, "blend"), 0.3f); set (P_ (uSpace, "decay"), 1.2f);
    }
    else if (scene == "tape")
    {
        on (uWreck, 1.0f); set (P_ (uWreck, "style"), wsTape); set (P_ (uWreck, "drive"), 0.35f); set (P_ (uWreck, "tone"), -0.15f);
        on (uEcho, 0.15f); set (P_ (uEcho, "mode"), emTape); set (P_ (uEcho, "div"), 10); set (P_ (uEcho, "fb"), 0.3f); set (P_ (uEcho, "char"), 0.6f);
        on (uChorus, 0.22f); set (P_ (uChorus, "rate"), 0.25f); set (P_ (uChorus, "depth"), 0.2f);
    }
    else if (scene == "dub")
    {
        on (uEcho, 0.35f); set (P_ (uEcho, "mode"), emTape); set (P_ (uEcho, "div"), 10); set (P_ (uEcho, "fb"), 0.62f); set (P_ (uEcho, "ping"), 1);
        set (P_ (uEcho, "lowcut"), 250.0f); set (P_ (uEcho, "highcut"), 3200.0f); set (P_ (uEcho, "char"), 0.7f);
        on (uSpace, 0.2f); set (P_ (uSpace, "size"), 0.5f); set (P_ (uSpace, "decay"), 2.0f);
        on (uEq); eqBand (1, eq::btLowCut, 60.0f, 0.0f, 0.71f, 1);
    }
    else if (scene == "shimmer")
    {
        on (uShimmer, 0.45f); set (P_ (uShimmer, "size"), 0.85f); set (P_ (uShimmer, "decay"), 9.0f); set (P_ (uShimmer, "shimmer"), 0.7f); set (P_ (uShimmer, "damp"), 0.3f);
    }
    else if (scene == "gate8") { on (uGate, 1.0f); set (P_ (uGate, "pattern"), 2); }
    else if (scene == "gate16") { on (uGate, 1.0f); set (P_ (uGate, "pattern"), 3); }
    else if (scene == "halftime") { on (uGate, 1.0f); set (P_ (uGate, "pattern"), 6); }
    else if (scene == "stutter") { on (uHaunt, 1.0f); on (uGate, 1.0f); set (P_ (uGate, "pattern"), 8); }
    else if (scene == "lofi")
    {
        on (uWreck, 0.6f); set (P_ (uWreck, "style"), wsDecimate); set (P_ (uWreck, "drive"), 0.35f);
        on (uTube, 1.0f); set (P_ (uTube, "drive"), 0.4f); set (P_ (uTube, "char"), 1);
        on (uEq); eqBand (1, eq::btLowCut, 150.0f); eqBand (8, eq::btHighCut, 6500.0f);
        on (uChorus, 0.3f); set (P_ (uChorus, "rate"), 0.3f); set (P_ (uChorus, "depth"), 0.35f);
    }
    else if (scene == "ott") { on (uOtt, 0.6f); }
    else if (scene == "cleanlimit")
    {   // transparent lookahead limiting only: +4 dB make-up into a -0.5 dB ceiling, medium release (peak-limited plucks sit at bank level)
        on (uLim); set (P_ (uLim, "ceiling"), -0.5f); set (P_ (uLim, "gain"), 4.0f); set (P_ (uLim, "release"), 60.0f);
    }
    else if (scene == "space")
    {
        on (uSpace, 0.35f); set (P_ (uSpace, "size"), 0.85f); set (P_ (uSpace, "blend"), 0.8f); set (P_ (uSpace, "decay"), 6.0f); set (P_ (uSpace, "glimmer"), 0.35f);
        on (uEcho, 0.2f); set (P_ (uEcho, "mode"), emAnalog); set (P_ (uEcho, "div"), 9); set (P_ (uEcho, "fb"), 0.4f);
    }
    else if (scene == "crystal")
    {
        on (uCrystal, 0.45f); set (P_ (uCrystal, "pitch"), 12.0f); set (P_ (uCrystal, "splice"), 180.0f); set (P_ (uCrystal, "delay"), 250.0f);
        set (P_ (uCrystal, "fb"), 0.5f); set (P_ (uCrystal, "reverse"), 0.6f);
        on (uShimmer, 0.15f); set (P_ (uShimmer, "decay"), 4.0f);
    }
    else if (scene == "wreck")
    {
        on (uWreck, 1.0f); set (P_ (uWreck, "style"), wsPentode); set (P_ (uWreck, "drive"), 0.7f); set (P_ (uWreck, "punish"), 1);
        on (uOtt, 0.3f);
    }
    else if (scene == "wreckclean")
    {   // round 2 (2026-10-24): the wrecked bass without PUNISH and with half the OTT, for basses that must stay clean under the grit
        on (uWreck, 1.0f); set (P_ (uWreck, "style"), wsPentode); set (P_ (uWreck, "drive"), 0.5f); set (P_ (uWreck, "punish"), 0);
        on (uOtt, 0.15f);
    }
    else if (scene == "grid") { on (uGrid, 1.0f); set (P_ (uGrid, "pattern"), 1); }
    else if (scene == "radiate")
    {
        on (uTube, 1.0f); set (P_ (uTube, "drive"), 0.55f); set (P_ (uTube, "char"), 1); set (P_ (uTube, "bass"), 3.0f); set (P_ (uTube, "treble"), 2.0f);
    }
    else if (scene == "freak")
    {
        on (uFilter, 1.0f); set (P_ (uFilter, "type"), ftLP); set (P_ (uFilter, "poles"), 1); set (P_ (uFilter, "cutoff"), 900.0f); set (P_ (uFilter, "reso"), 0.6f);
        set (P_ (uFilter, "lfoamt"), 0.5f); set (P_ (uFilter, "lfoshape"), lsStep); set (P_ (uFilter, "lfosync"), 1); set (P_ (uFilter, "lfodiv"), 6); set (P_ (uFilter, "drive"), 0.3f);
    }
    else if (scene == "crush")
    {
        on (uCrush, 0.55f); set (P_ (uCrush, "crush"), 0.65f); set (P_ (uCrush, "dark"), 0.4f); set (P_ (uCrush, "release"), 150.0f);
    }
#if GPFX_BOX
    else if (scene == "retrofat")
    {
        on (uRetro, 1.0f); set (P_ (uRetro, "magic"), 0.55f); set (P_ (uRetro, "noise"), 0.35f); set (P_ (uRetro, "ntype"), rnVinyl);
        set (P_ (uRetro, "wobble"), 0.3f); set (P_ (uRetro, "dist"), 0.35f); set (P_ (uRetro, "mag_on"), 1); set (P_ (uRetro, "mag"), 0.4f);
        on (uFatten, 1.0f); set (P_ (uFatten, "fat"), 0.55f); set (P_ (uFatten, "crunch"), 0.25f); set (P_ (uFatten, "color"), -0.15f);
    }
    else if (scene == "crackedhall")
    {
        on (uCrackle, 1.0f); set (P_ (uCrackle, "bands"), 2); set (P_ (uCrackle, "b1_curve"), ccTube); set (P_ (uCrackle, "b1_drive"), 0.35f);
        set (P_ (uCrackle, "b2_curve"), ccCracked); set (P_ (uCrackle, "b2_drive"), 0.6f); set (P_ (uCrackle, "b3_curve"), ccBitcrush); set (P_ (uCrackle, "b3_drive"), 0.55f);
        set (P_ (uCrackle, "b3_mix"), 0.6f);
        on (uHalls, 0.3f); set (P_ (uHalls, "mode"), hmHall); set (P_ (uHalls, "size"), 0.75f); set (P_ (uHalls, "decay"), 4.5f); set (P_ (uHalls, "predelay"), 35.0f);
    }
    else if (scene == "dustydream")
    {
        on (uRetro, 1.0f); set (P_ (uRetro, "noise"), 0.3f); set (P_ (uRetro, "ntype"), rnDust); set (P_ (uRetro, "wobble"), 0.45f); set (P_ (uRetro, "flutter"), 0.2f);
        set (P_ (uRetro, "dist"), 0.2f); set (P_ (uRetro, "tone"), -0.35f);
        on (uHalls, 0.35f); set (P_ (uHalls, "mode"), hmCloud); set (P_ (uHalls, "size"), 0.85f); set (P_ (uHalls, "decay"), 7.0f); set (P_ (uHalls, "damp"), 0.55f);
        on (uChorus, 0.25f); set (P_ (uChorus, "rate"), 0.2f); set (P_ (uChorus, "depth"), 0.4f);
    }
    else if (scene == "galaxyecho")
    {
        on (uEcho, 0.3f); set (P_ (uEcho, "mode"), emTape); set (P_ (uEcho, "div"), 7); set (P_ (uEcho, "fb"), 0.55f); set (P_ (uEcho, "ping"), 1);
        on (uHalls, 0.35f); set (P_ (uHalls, "mode"), hmGalaxy); set (P_ (uHalls, "size"), 0.9f); set (P_ (uHalls, "decay"), 12.0f); set (P_ (uHalls, "shimmer"), 0.35f); set (P_ (uHalls, "mod"), 0.7f);
    }
    else if (scene == "phonebooth")
    {
        on (uCrackle, 1.0f); set (P_ (uCrackle, "b1_curve"), ccDiode); set (P_ (uCrackle, "b1_drive"), 0.5f); set (P_ (uCrackle, "box"), cbPhone);
        on (uHalls, 0.2f); set (P_ (uHalls, "mode"), hmRoom); set (P_ (uHalls, "size"), 0.25f); set (P_ (uHalls, "decay"), 0.6f);
    }
    else if (scene == "warpflick" || scene == "tapedrop")
    {
        const bool tape = scene == "tapedrop";
        const auto& f = bendlane::factory()[tape ? 3 : 1];   // "Drop & return" / "Up-up-down"
        impl->warpShape.set (f.shape);
        on (uWarp, 1.0f); set (P_ (uWarp, "mode"), tape ? 1.0f : 0.0f); set (P_ (uWarp, "length"), (float) f.lenIndex); set (P_ (uWarp, "range"), (float) f.rangeIndex);
        on (uHalls, 0.18f); set (P_ (uHalls, "mode"), hmRoom); set (P_ (uHalls, "size"), 0.35f); set (P_ (uHalls, "decay"), 0.9f);
    }
    else if (scene == "talkingbass")
    {
        on (uFormant, 1.0f); set (P_ (uFormant, "move"), 1); set (P_ (uFormant, "mdiv"), 6); set (P_ (uFormant, "mdepth"), 0.8f); set (P_ (uFormant, "reso"), 0.6f); set (P_ (uFormant, "drive"), 0.45f); set (P_ (uFormant, "body"), 0.7f);
        on (uBassLab, 1.0f); set (P_ (uBassLab, "sub"), 0.3f); set (P_ (uBassLab, "translate"), 0.4f); set (P_ (uBassLab, "lowwidth"), 0.0f);
    }
    else if (scene == "portalcloud")
    {
        on (uSpectra, 0.5f); set (P_ (uSpectra, "smear"), 0.7f); set (P_ (uSpectra, "ghost"), 0.9f);
        on (uPortal, 0.4f); set (P_ (uPortal, "shape"), 3); set (P_ (uPortal, "move"), 2); set (P_ (uPortal, "size"), 0.85f); set (P_ (uPortal, "decay"), 7.0f);
    }
    else if (scene == "spectralfreeze") { on (uSpectra, 0.8f); set (P_ (uSpectra, "freeze"), 1); set (P_ (uSpectra, "smear"), 0.5f); set (P_ (uSpectra, "ghost"), 0.0f); }
    else if (scene == "bassphone") { on (uBassLab, 1.0f); set (P_ (uBassLab, "translate"), 0.7f); set (P_ (uBassLab, "lowwidth"), 0.0f); set (P_ (uBassLab, "growl"), 0.2f); }
    else if (scene == "pumpfloor") { on (uGate, 1.0f); set (P_ (uGate, "pmode"), 1); set (P_ (uGate, "pdepth"), 0.75f); on (uPortal, 0.25f); set (P_ (uPortal, "size"), 0.5f); }
    else if (scene == "parallelwreck")
    {   // lane A stays clean, lane B gets wrecked; both summed
        set ("fx_par_on", 1); set ("fx_par_blend", 0.5f);
        on (uWreck, 1.0f); set (P_ (uWreck, "style"), wsPentode); set (P_ (uWreck, "drive"), 0.75f); set (P_ (uWreck, "punish"), 1);
        on (uCrush, 0.8f); set (P_ (uCrush, "crush"), 0.7f);
        impl->laneOf[uWreck].store (1); impl->laneOf[uCrush].store (1); impl->laneVersion.fetch_add (1);
    }
    else if (scene == "fatcrush")
    {
        on (uFatten, 1.0f); set (P_ (uFatten, "fat"), 0.8f); set (P_ (uFatten, "crunch"), 0.6f);
        on (uCrackle, 0.5f); set (P_ (uCrackle, "bands"), 1); set (P_ (uCrackle, "b2_curve"), ccFuzz); set (P_ (uCrackle, "b2_drive"), 0.55f); set (P_ (uCrackle, "b1_drive"), 0.2f);
    }
#endif
    else if (scene == "spaceblend")
    {
        on (uSpace, 0.4f); set (P_ (uSpace, "size"), 0.6f); set (P_ (uSpace, "blend"), 0.5f); set (P_ (uSpace, "decay"), 3.5f);
        set (P_ (uSpace, "mod"), 0.6f); set (P_ (uSpace, "glimmer"), 0.5f);
    }
}

} // namespace gpfx
