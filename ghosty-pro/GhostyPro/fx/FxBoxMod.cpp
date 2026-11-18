// Ghost Box 3 modulation layer: MORPH / MOTION / MACROS / loudness match (GPFX_BOX builds only; see FxBoxMod.h).
#include "FxBoxMod.h"
#if GPFX_BOX
#include <set>

namespace gpfx {

namespace {
int unitOfId (const juce::String& id)
{
    for (int u = 0; u < kNumUnits; ++u)
        if (id.startsWith (juce::String ("fx_") + unitInfo (u).key + "_")) return u;
    return -1;
}
// delay-line times (a jump is a click, so MORPH ducks the unit and snaps): any "ms" parameter whose id names a time / delay,
// which also covers units added later
bool isTimeId (const juce::String& id, const juce::RangedAudioParameter* p)
{
    static const std::set<juce::String> t { "fx_echo_time", "fx_crystal_delay", "fx_chorus_delay", "fx_space_predelay", "fx_shimmer_predelay", "fx_halls_predelay", "fx_portal_predelay" };
    if (t.count (id) > 0) return true;
    return p != nullptr && p->getLabel() == "ms" && (id.contains ("delay") || id.endsWith ("_time"));
}
juce::String snapToString (const std::map<juce::String, float>& m)
{
    juce::String s; s.preallocateBytes (m.size() * 24);
    for (auto& [k, v] : m) s << k << "=" << juce::String ((double) v, 7) << ";";
    return s;
}
std::map<juce::String, float> snapFromString (const juce::String& s)
{
    std::map<juce::String, float> m;
    for (auto& tok : juce::StringArray::fromTokens (s, ";", ""))
        if (tok.containsChar ('=')) m[tok.upToFirstOccurrenceOf ("=", false, false)] = tok.fromFirstOccurrenceOf ("=", false, false).getFloatValue();
    return m;
}
} // namespace

bendlane::Shape flatShape() { bendlane::Shape s; s.snap = 0; s.grid = 2; s.add (0, 0); return s; }

BoxMod::BoxMod()
{
    macroNames = { "DARKNESS", "CHAOS", "SIZE", "IMPACT", "MOVEMENT", "WIDTH", "DIRT", "AIR" };
    for (auto& l : lanes) l.set (flatShape());
    for (auto& t : laneTarget) t.store (0);
    setDefaultMacros();
}
BoxMod::~BoxMod() { *tlAlive = false; if (tlPool) tlPool->removeAllJobs (true, 10000); cfg.store (nullptr); }

std::atomic<float>* BoxMod::shadowFor (const juce::String& id, std::atomic<float>* host)
{
    if (auto it = slotIndex.find (id); it != slotIndex.end()) return &slots[(size_t) it->second]->shadow;
    auto s = std::make_unique<ModSlot>();
    s->id = id; s->host = host; s->shadow.store (host->load()); s->committed = host->load(); s->unit = unitOfId (id);
    slotIndex[id] = (int) slots.size();
    slots.push_back (std::move (s));
    return &slots.back()->shadow;
}

void BoxMod::finishAttach (juce::AudioProcessorValueTreeState& s)
{
    apvts = &s;
    for (auto& sl : slots)
        if (auto* p = s.getParameter (sl->id))
        {
            sl->param = p; sl->range = p->getNormalisableRange();
            sl->discrete = dynamic_cast<juce::AudioParameterFloat*> (p) == nullptr;
        }
    morphP.bind (s, "box_morph", 0);
    boardXP.bind (s, "box_board_x", 0.5f); boardYP.bind (s, "box_board_y", 0.5f); orbitP.bind (s, "box_orbit", 0);
    orbitRateP.bind (s, "box_orbit_rate", 3); orbitDepthP.bind (s, "box_orbit_depth", 0.25f);
    for (int k = 0; k < kMacros; ++k) macroP[(size_t) k].bind (s, "box_macro" + juce::String (k + 1), 0);
    for (int k = 0; k < kLanes; ++k)
    {
        const juce::String b = "box_mo_" + juce::String (laneKey (k)) + "_";
        laneP[(size_t) k].on.bind (s, b + "on", 0); laneP[(size_t) k].depth.bind (s, b + "depth", 1);
        laneP[(size_t) k].len.bind (s, b + "len", 2); laneP[(size_t) k].feel.bind (s, b + "feel", 0);
        laneP[(size_t) k].mode.bind (s, b + "mode", 0); laneP[(size_t) k].stepsN.bind (s, b + "steps", 1); laneP[(size_t) k].phase.bind (s, b + "phase", 0);
    }
    resetP.bind (s, "box_mo_reset", 0);
    auto tgt = [this] (int lane, const juce::String& id, float w) { const int sl = slotOf (id); if (sl >= 0) targets[(size_t) lane].push_back ({ sl, slots[(size_t) sl]->unit, w }); };
    tgt (lnFilter, "fx_filter_cutoff", 0.45f);
    tgt (lnPitch, "fx_warp_amount", 1.0f);
    for (auto* id : { "fx_echo_mix", "fx_space_mix", "fx_shimmer_mix", "fx_halls_mix", "fx_crystal_mix", "fx_split_b_verb" }) tgt (lnSpace, id, 0.5f);
    for (auto* id : { "fx_wreck_drive", "fx_tube_drive", "fx_crush_crush", "fx_crackle_mix", "fx_retro_dist", "fx_fatten_crunch", "fx_split_a_drive" }) tgt (lnDamage, id, 0.5f);
    offsets.assign (slots.size(), 0.0f);
    rebuild();
}

void BoxMod::prepare (double s)
{
    sr = s; smA = 1.0f - (float) std::exp (-1.0 / (0.001 * sr));
    scenes.prepare (sr);
    for (auto& f : djf) f.reset();
    for (auto& p : pitch) p.prepare (sr, 50.0f);
    verb.prepare (sr, 2.0f);
    verb.set (0.7f, 3.5f, 6500.0f, 0.4f, false);
    inLu.prepare (sr); outLu.prepare (sr); trimNow = 1.0f;
    laneSm.fill (0); volG = volG1 = 1.0f; volA = 1.0f - (float) std::exp (-1.0 / (0.0012 * sr)); djMix = 0; pitchMix = 0; verbTail = 0; morphInit = false; wasMorphing = false; wasPlaying = false; startPpq = 0;
    for (auto& sl : slots) { sl->shadow.store (sl->host->load()); sl->hasPending = false; }
    duckPending.fill (false);
    onsets.prepare (sr); tlSm.fill (0.0f); tlIdent.fill (0);   // GHOST MOTION timeline: the feed's ring (allocated here), smoothers start at their targets
}

// ================================================================ audio thread
void BoxMod::beginChunk (const BlockCtx& ctx, int n, const std::array<P, kNumUnits>& onP) noexcept
{
    const ModConfig* c = cfgNow;
    const int S = (int) slots.size();
    if ((int) offsets.size() < S) return;   // not attached yet
    scenes.beginChunk (ctx.ppq, ctx.samplesPerBeat, n);   // GHOST SCENES: a landing on this chunk's first sample, the morph, the base values
    const bool sceneOn = scenes.on() && scenes.slotCount() == S;
    bool any = false;
    // macros
    std::array<float, kMacros> mv {};
    for (int k = 0; k < kMacros; ++k)
    {
        auto& m = macroSm[(size_t) k];
        m.target = clampf (macroP[(size_t) k](), 0.0f, 1.0f);
        m.cur += (m.target - m.cur) * std::min (1.0f, (float) n / (float) (0.006 * sr));
        if (std::abs (m.cur - m.target) < 1.0e-5f) m.cur = m.target;
        mv[(size_t) k] = m.cur;
    }
    std::fill (offsets.begin(), offsets.begin() + S, 0.0f);
    if (c != nullptr)
        for (auto& it : c->macros)
        {
            const int mu = slots[(size_t) it.slot]->unit;
            if (mu >= 0 && ! onP[(size_t) mu].b()) continue;   // a macro only moves units that are on (what you hear is what the badge counts)
            const float x = mv[(size_t) it.macro];
            if (x > 0.0f) { offsets[(size_t) it.slot] += it.depth * shapeCurve (it.curve, x); any = true; }
        }
    // RESET ON START: the lanes count from the moment the transport started instead of from bar 1
    if (ctx.playing && ! wasPlaying) startPpq = ctx.ppq;
    wasPlaying = ctx.playing;
    ppqOrigin = resetP.b() && ctx.playing ? startPpq : 0.0;
    // motion lanes (value at the chunk start for rack targets; the built-in stage evaluates per sample)
    if (const uint32_t sv = stepVer.load (std::memory_order_acquire); sv != stepSeen)
    {
        for (int k = 0; k < kLanes; ++k) { for (int i = 0; i < kMaxSteps; ++i) laneSteps[(size_t) k].v[(size_t) i] = stepVals[(size_t) k][(size_t) i].load (std::memory_order_relaxed); laneSteps[(size_t) k].ties = stepTies[(size_t) k].load (std::memory_order_relaxed); }
        stepSeen = sv;
    }
    for (int k = 0; k < kLanes; ++k)
    {
        laneOn[(size_t) k] = laneP[(size_t) k].on.b();
        lanes[(size_t) k].fetch (laneShape[(size_t) k], laneSeen[(size_t) k]);
        laneDepth[(size_t) k] = clampf (laneP[(size_t) k].depth(), 0.0f, 1.0f);
        laneBeats[(size_t) k] = motionBeats (laneP[(size_t) k].len.i(), laneP[(size_t) k].feel.i());
        laneMode[(size_t) k] = laneP[(size_t) k].mode.i(); laneSteps[(size_t) k].count = motionStepCount (laneP[(size_t) k].stepsN.i());
        lanePhase[(size_t) k] = clampf (laneP[(size_t) k].phase(), 0.0f, 1.0f);
        laneUnits[(size_t) k] = false;
        if (! laneOn[(size_t) k]) { lanePos[(size_t) k].store (-1.0f, std::memory_order_relaxed); continue; }
        any = true;
        const double ph = positiveMod ((ctx.ppq - ppqOrigin) / laneBeats[(size_t) k] + lanePhase[(size_t) k], 1.0);
        const float v = laneValue (k, ctx.ppq) * laneDepth[(size_t) k];
        laneRaw[(size_t) k] = v;
        lanePos[(size_t) k].store ((float) ph, std::memory_order_relaxed); laneVal[(size_t) k].store (v, std::memory_order_relaxed);
        for (auto& t : targets[(size_t) k])
            if (t.unit >= 0 && onP[(size_t) t.unit].b()) { offsets[(size_t) t.slot] += v * t.weight; laneUnits[(size_t) k] = true; }
        laneTarget[(size_t) k].store (laneUnits[(size_t) k] ? 1 : 0, std::memory_order_relaxed);
    }
    // GHOST MOTION timeline: one loop for the whole rack; each lane's value where ITS unit's audio sits (the latency before it), smoothed
    if (c != nullptr && ! c->tl.empty())
    {
        const double bpb = ctx.beatsPerBar > 0.0 ? ctx.beatsPerBar : 4.0, loopBeats = (double) juce::jlimit (1, 8, c->tlBars) * bpb;
        const double edge = kTlEdgeMs * 0.001 * ctx.sr / ctx.samplesPerBeat;   // the event edges in beats
        const float a = 1.0f - (float) std::exp (-(double) n / (0.003 * sr));   // 3 ms
        for (auto& it : c->tl)
        {
            const int k = juce::jlimit (0, kTlLanes - 1, it.lane);
            tlShapes[(size_t) k].fetch (tlShape[(size_t) k], tlSeen[(size_t) k]);
            const double up = it.unit >= 0 ? (double) upLat[(size_t) it.unit] : 0.0;
            const double beat = positiveMod (ctx.ppq - up / ctx.samplesPerBeat - ppqOrigin, loopBeats);
            float v = 0.0f;
            if (it.on)
            {
                v = it.mode == 1 ? tlStepsAt (it.st.data(), it.steps, beat / loopBeats) : tlShape[(size_t) k].valueAt (beat / loopBeats);
                v = clampf (v + tlEventsAt (it.ev.data(), it.nEv, beat, loopBeats, edge), -1.0f, 1.0f) * it.depth;
            }
            float& sm = tlSm[(size_t) k];
            if (tlIdent[(size_t) k] != it.ident) { sm = v; tlIdent[(size_t) k] = it.ident; }   // a new lane starts where it is (no glide from another lane's value)
            else { sm += a * (v - sm); if (std::abs (v - sm) < 1.0e-7f) sm = v; }
            offsets[(size_t) it.slot] += sm;
            tlVal[(size_t) k].store (sm, std::memory_order_relaxed);
        }
        any = true;
        tlPos.store ((float) (positiveMod (ctx.ppq - ppqOrigin, loopBeats) / loopBeats), std::memory_order_relaxed);
        tlBpb.store (bpb, std::memory_order_relaxed);
    }
    else tlPos.store (-1.0f, std::memory_order_relaxed);
    tlSpb.store (ctx.samplesPerBeat, std::memory_order_relaxed); tlOrigin.store (ppqOrigin, std::memory_order_relaxed);
    if (ctx.beatsPerBar > 0.0) tlBpb.store (ctx.beatsPerBar, std::memory_order_relaxed);
    // morph value: a 40 ms linear glide to the knob, exact at the ends
    const bool morphing = c != nullptr && c->morphOn && ! c->morph.empty();
    const float mt = clampf (morphP(), 0.0f, 1.0f);
    if (! morphInit || (morphing && ! wasMorphing)) { morphNow = mt; morphInit = true; }   // MORPH switching on: the rack already IS the knob's state, no sweep
    wasMorphing = morphing;
    const float step = (float) n / (float) (0.04 * sr);
    morphNow = std::abs (mt - morphNow) <= step ? mt : morphNow + (mt > morphNow ? step : -step);
    // SPIRIT BOARD weights: puck = X / Y (+ orbit), weights by distance, glided per chunk (40 ms), exact when settled
    const bool boarding = c != nullptr && c->boardOn && c->boardN > 0;
    boardShared.postTime (ctx.ppq, ctx.bpm, ctx.playing);   // (the BOARD page: is the audio running, and what time is it)
    if (boarding)
    {
        std::array<float, ghosty::spirit::kMaxGhosts> xs {}, ys {}; ghosty::spirit::WeightParams wp; ghosty::spirit::OrbitParams op;
        const int nG = juce::jmin (c->boardN, boardShared.read (xs.data(), ys.data(), wp, op));
        op.mode = (ghosty::spirit::Orbit) juce::jlimit (0, 3, orbitP.i()); op.sync = true; op.rate = (float) (1.0 / boardOrbitBeats (orbitRateP.i())); op.depth = clampf (orbitDepthP(), 0.0f, 1.0f) * 0.5f;
        juce::Point<float> pk;
        if (op.mode != ghosty::spirit::Orbit::Off || ! boardShared.pathPuck (ctx.ppq, pk))   // ORBIT OFF + a DRAWN ORBIT (CUSTOM n): its path
            pk = ghosty::spirit::clampToBoard (juce::Point<float> (clampf (boardXP(), 0, 1), clampf (boardYP(), 0, 1)) + ghosty::spirit::orbitOffset (op, ctx.ppq));
        if (nG > 0)
        {
            ghosty::spirit::computeWeights (xs.data(), ys.data(), nG, pk.x, pk.y, wp, boardWT.data());
            // a linear glide (a jump of the whole weight takes 40 ms, then it is exact)
            const float step = boardInit ? (float) n / (float) (0.04 * sr) : 1.0f; boardInit = true;
            float sum = 0;
            for (int k = 0; k < nG; ++k) { auto& w = boardW[(size_t) k]; const float d = boardWT[(size_t) k] - w; w = std::abs (d) <= step ? boardWT[(size_t) k] : w + (d > 0 ? step : -step); sum += w; }
            bool settled = true; for (int k = 0; k < nG; ++k) settled = settled && boardW[(size_t) k] == boardWT[(size_t) k];
            if (! settled && sum > 0) for (int k = 0; k < nG; ++k) boardW[(size_t) k] /= sum;
            for (int k = 0; k < nG; ++k) if (boardWT[(size_t) k] == 1.0f && boardW[(size_t) k] > 0.9999f) { for (int j = 0; j < nG; ++j) boardW[(size_t) j] = j == k ? 1.0f : 0.0f; break; }
            // the winner (switches / choices): changes only when another ghost leads by 0.08 (no flicker at a border)
            if (boardWinner >= nG) boardWinner = 0;
            for (int k = 0; k < nG; ++k) if (boardW[(size_t) k] > boardW[(size_t) boardWinner] + 0.08f || boardW[(size_t) k] >= 0.9999f) boardWinner = k;
            boardShared.postLive (pk.x, pk.y, boardW.data(), nG);
        }
    }
    else
    {   // board off: the puck is just X / Y (posted for the BOARD page; the weights are not used)
        boardInit = false;
        boardShared.livePx.store (clampf (boardXP(), 0, 1), std::memory_order_relaxed); boardShared.livePy.store (clampf (boardYP(), 0, 1), std::memory_order_relaxed);
    }
    any = any || morphing || boarding || sceneOn;
    busy = any;
    if (! any)
    {
        for (int i = 0; i < S; ++i) { auto& s = *slots[(size_t) i]; s.shadow.store (s.host->load (std::memory_order_relaxed), std::memory_order_relaxed); s.hasPending = false; }
        duckPending.fill (false);
        return;
    }
    // base = host, overridden by morph items
    for (int i = 0; i < S; ++i) slots[(size_t) i]->touched = false;
    if (boarding)
    {
        const int nG = c->boardN;
        for (auto& it : c->board)
        {
            auto& s = *slots[(size_t) it.slot];
            float v = it.v[(size_t) boardWinner];
            int exact = -1; for (int k = 0; k < nG; ++k) if (boardW[(size_t) k] >= 1.0f) exact = k;
            if (exact >= 0) v = it.v[(size_t) exact];   // the puck on a ghost = that scene exactly
            else switch (it.kind)
            {
                case bkLinear: { float a = 0; for (int k = 0; k < nG; ++k) a += boardW[(size_t) k] * it.v[(size_t) k]; v = a; break; }
                case bkLog:    { float a = 0; for (int k = 0; k < nG; ++k) a += boardW[(size_t) k] * std::log (it.v[(size_t) k]); v = std::exp (a); break; }
                case bkOnMix:  { v = 0; for (int k = 0; k < nG; ++k) if (boardW[(size_t) k] > 0 && it.v[(size_t) k] > 0.5f) v = 1; break; }   // on while any weighted ghost has it on (the mix fades it)
                case bkMix:    { float a = 0; for (int k = 0; k < nG; ++k) a += boardW[(size_t) k] * it.eff[(size_t) k]; v = a; break; }
                case bkHold:
                {   // weighted over the ghosts that have the unit on (the others' values would be inaudible jumps)
                    float a = 0, ws = 0; for (int k = 0; k < nG; ++k) if (it.on[(size_t) k]) { a += boardW[(size_t) k] * it.v[(size_t) k]; ws += boardW[(size_t) k]; }
                    if (ws > 1.0e-6f) v = a / ws;
                    break;
                }
                case bkHoldSwitch:
                {   // the leading ghost among those that have the unit on
                    int best = -1; for (int k = 0; k < nG; ++k) if (it.on[(size_t) k] && (best < 0 || boardW[(size_t) k] > boardW[(size_t) best])) best = k;
                    if (best >= 0 && boardW[(size_t) best] > 0.0f) v = it.v[(size_t) best];
                    break;
                }
                default: break;   // bkSwitch: the winner
            }
            if (! std::isfinite (v)) v = it.v[(size_t) boardWinner];
            const float off = offsets[(size_t) it.slot];
            if (off != 0.0f) v = s.from01 (s.to01 (v) + off);
            s.touched = true;
            const float cur = s.shadow.load (std::memory_order_relaxed);
            if ((it.kind == bkSwitch || it.kind == bkHoldSwitch) && s.unit >= 0 && onP[(size_t) s.unit].b() && s.id != pid (s.unit, "on"))
            {   // a switch / choice of a running unit flips inside a short duck (as MORPH does)
                if (v != cur) { s.pending = v; s.hasPending = true; duckPending[(size_t) s.unit] = true; }
                else if (s.hasPending && v == cur) s.hasPending = false;
            }
            else { s.shadow.store (v, std::memory_order_relaxed); s.hasPending = false; }
        }
    }
    const float mc = morphing ? morphCurve (c->morphCurve, morphNow) : 0.0f;
    if (morphing)
        for (auto& it : c->morph)
        {
            auto& s = *slots[(size_t) it.slot];
            float v;
            const bool lo = mc <= 0.0f, hi = mc >= 1.0f;
            switch (it.kind)
            {
                case mkLog:  v = lo ? it.a : hi ? it.b : std::exp (std::log (it.a) + (std::log (it.b) - std::log (it.a)) * mc); break;
                case mkSwitch: v = mc < 0.5f ? it.a : it.b; break;
                case mkTime: v = lo ? it.a : hi ? it.b : (it.a > 0 && it.b > 0 ? std::exp (std::log (it.a) + (std::log (it.b) - std::log (it.a)) * mc) : it.a + (it.b - it.a) * mc); break;
                case mkOn:   v = lo ? it.a : hi ? it.b : 1.0f; break;
                case mkMix:  v = lo ? it.a : hi ? it.b : it.aEff + (it.bEff - it.aEff) * mc; break;
                case mkHoldA: v = hi ? it.b : it.a; break;
                case mkHoldB: v = lo ? it.a : it.b; break;
                default:     v = lo ? it.a : hi ? it.b : it.a + (it.b - it.a) * mc; break;
            }
            const float off = offsets[(size_t) it.slot];
            if (off != 0.0f) v = s.from01 (s.to01 (v) + off);
            s.touched = true;
            const bool duckKind = it.kind == mkSwitch || it.kind == mkTime;
            const float cur = s.shadow.load (std::memory_order_relaxed);
            if (duckKind && s.unit >= 0 && onP[(size_t) s.unit].b())
            {
                bool differs;
                if (it.kind == mkTime && ! lo && ! hi && cur > 0 && v > 0) differs = std::abs (std::log (v / cur)) > 0.04f;
                else differs = v != cur;
                if (differs) { s.pending = v; s.hasPending = true; duckPending[(size_t) s.unit] = true; }
                else if (s.hasPending && v == cur) s.hasPending = false;
            }
            else { s.shadow.store (v, std::memory_order_relaxed); s.hasPending = false; }
        }
    for (int i = 0; i < S; ++i)
    {
        auto& s = *slots[(size_t) i];
        if (s.touched) continue;
        float v = sceneOn ? scenes.value (i) : s.host->load (std::memory_order_relaxed);
        const float off = offsets[(size_t) i];
        if (off != 0.0f && s.param != nullptr) v = s.from01 (s.to01 (v) + off);
        if (sceneOn && scenes.flipsInDuck (i) && s.unit >= 0 && onP[(size_t) s.unit].b())
        {   // GHOST SCENES: a switch / choice / delay time of a running unit flips inside the rack's short duck (as MORPH does)
            const float cur = s.shadow.load (std::memory_order_relaxed);
            if (v != cur) { s.pending = v; s.hasPending = true; duckPending[(size_t) s.unit] = true; }
            else if (s.hasPending) s.hasPending = false;
            continue;
        }
        s.shadow.store (v, std::memory_order_relaxed);
    }
}

std::vector<SceneSlotInfo> BoxMod::sceneSlotInfo() const
{
    std::vector<SceneSlotInfo> v;
    for (auto& sp : slots)
    {
        SceneSlotInfo in; in.host = sp->host; in.unit = sp->unit; in.discrete = sp->discrete;
        in.time = isTimeId (sp->id, sp->param);
        in.logish = sp->param != nullptr && (sp->param->getLabel() == "Hz" || sp->param->getLabel() == "ms" || sp->param->getLabel() == "s");
        in.isOn = sp->unit >= 0 && sp->id == pid (sp->unit, "on");
        in.isMix = sp->unit >= 0 && sp->id == pid (sp->unit, "mix");
        v.push_back (in);
    }
    return v;
}

void BoxMod::snapSmoothing() noexcept
{
    for (auto& m : macroSm) m.cur = m.target;
    morphNow = clampf (morphP(), 0.0f, 1.0f); morphInit = true;
    // the MOTION lane smoothers are NOT touched: they follow the playhead, and a reset here would jump running audio
    for (auto& s : slots) if (s->hasPending) { s->shadow.store (s->pending, std::memory_order_relaxed); s->hasPending = false; }
    duckPending.fill (false);
}

void BoxMod::commitDuck (int u, UnitDsp& unit) noexcept
{
    if (! duckPending[(size_t) u]) return;
    for (auto& s : slots)
        if (s->unit == u && s->hasPending) { s->shadow.store (s->pending, std::memory_order_relaxed); s->hasPending = false; }
    duckPending[(size_t) u] = false;
    unit.jumpSmoothing();
}

float BoxMod::laneValue (int k, double ppq) const noexcept
{
    const double ph = positiveMod ((ppq - ppqOrigin) / laneBeats[(size_t) k] + lanePhase[(size_t) k], 1.0);
    return laneMode[(size_t) k] == 1 ? laneSteps[(size_t) k].valueAt (ph) : laneShape[(size_t) k].valueAt (ph);
}
StepLane BoxMod::getSteps (int lane) const
{
    StepLane sl; lane = juce::jlimit (0, kLanes - 1, lane);
    for (int i = 0; i < kMaxSteps; ++i) sl.v[(size_t) i] = stepVals[(size_t) lane][(size_t) i].load();
    sl.ties = stepTies[(size_t) lane].load(); sl.count = apvts != nullptr ? motionStepCount (laneP[(size_t) lane].stepsN.i()) : 16;
    return sl;
}
void BoxMod::setSteps (int lane, const StepLane& sl)
{
    lane = juce::jlimit (0, kLanes - 1, lane);
    for (int i = 0; i < kMaxSteps; ++i) stepVals[(size_t) lane][(size_t) i].store (clampf (sl.v[(size_t) i], -1.0f, 1.0f));
    stepTies[(size_t) lane].store (sl.ties); stepVer.fetch_add (1, std::memory_order_release);
}
void BoxMod::setStep (int lane, int i, float v) { if (juce::isPositiveAndBelow (lane, kLanes) && juce::isPositiveAndBelow (i, kMaxSteps)) { stepVals[(size_t) lane][(size_t) i].store (clampf (v, -1.0f, 1.0f)); stepVer.fetch_add (1, std::memory_order_release); } }
void BoxMod::setTie (int lane, int i, bool tie)
{
    if (! juce::isPositiveAndBelow (lane, kLanes) || ! juce::isPositiveAndBelow (i, kMaxSteps)) return;
    auto& t = stepTies[(size_t) lane]; uint64_t cur = t.load(); if (tie) cur |= (1ull << i); else cur &= ~(1ull << i); t.store (cur); stepVer.fetch_add (1, std::memory_order_release);
}
int BoxMod::liveAssignCount (int macro, const std::array<P, kNumUnits>& onP) const
{
    if (! juce::isPositiveAndBelow (macro, kMacros)) return 0;
    int n = 0;
    for (auto& a : assigns[(size_t) macro]) { const int sl = slotOf (a.id); if (sl < 0) continue; const int u = slots[(size_t) sl]->unit; if (u < 0 || onP[(size_t) u].b()) ++n; }
    return n;
}

void BoxMod::builtin (float* L, float* R, int n, const BlockCtx& ctx) noexcept
{
    std::array<bool, kLanes> act {};
    act[lnVolume] = laneOn[lnVolume];
    for (int k = 1; k < kLanes; ++k) act[(size_t) k] = laneOn[(size_t) k] && ! laneUnits[(size_t) k];
    bool running = djMix > 0 || pitchMix > 0 || verbTail > 0 || volG != 1.0f;
    for (int k = 0; k < kLanes; ++k) running = running || act[(size_t) k] || laneSm[(size_t) k] != 0.0f;
    if (! running) return;
    for (int i = 0; i < n; ++i)
    {
        const double ppq = ctx.ppqAt (i);
        float volTgt = 0.0f;
        for (int k = 0; k < kLanes; ++k)
        {
            float tgt = 0.0f;
            if (act[(size_t) k]) tgt = laneValue (k, ppq) * laneDepth[(size_t) k];
            if (k == lnVolume) volTgt = tgt;
            float& sm = laneSm[(size_t) k];
            sm += smA * (tgt - sm);
            if (! act[(size_t) k] && std::abs (sm) < 1.0e-6f) sm = 0.0f;
        }
        float l = L[i], r = R[i];
        // DAMAGE: soft saturation
        if (const float d = std::max (0.0f, laneSm[lnDamage]); d > 1.0e-5f)
        {
            const float g = 1.0f + 12.0f * d, comp = 1.0f / std::sqrt (g);
            l += d * (fastTanh (l * g) * comp - l); r += d * (fastTanh (r * g) * comp - r);
        }
        // FILTER: low-pass sweep below 0, high-pass sweep above 0
        {
            const float v = laneSm[lnFilter];
            const float mt = act[lnFilter] ? clampf (std::abs (v) * 25.0f, 0.0f, 1.0f) : 0.0f;
            djMix += smA * (mt - djMix); if (mt == 0.0f && djMix < 1.0e-5f) djMix = 0.0f;
            if (djMix > 0.0f)
            {
                if ((djCount++ & 15) == 0 || (v < 0) != (djLast < 0))
                {
                    const double f = v < 0 ? 20000.0 * std::exp2 (v * 9.0) : 20.0 * std::exp2 (v * 8.0);
                    for (auto& s : djf) s.set (sr, f, 0.85);
                    djLast = v;
                }
                float lp, bp, hp;
                djf[0].tick (l, lp, bp, hp); const float yl = djLast < 0 ? lp : hp;
                djf[1].tick (r, lp, bp, hp); const float yr = djLast < 0 ? lp : hp;
                l += djMix * (yl - l); r += djMix * (yr - r);
            }
        }
        // PITCH: light built-in shifter (+-12 st), faded in only while the lane is off zero
        {
            const float st = laneSm[lnPitch] * 12.0f;
            const float mt = act[lnPitch] ? clampf (std::abs (st) * 2.0f, 0.0f, 1.0f) : 0.0f;
            pitchMix += smA * (mt - pitchMix); if (mt == 0.0f && pitchMix < 1.0e-5f) pitchMix = 0.0f;
            if (pitchMix > 0.0f || act[lnPitch])
            {
                if ((i & 15) == 0) for (auto& p : pitch) p.setSemis (st);
                const float pl = pitch[0].process (l), pr = pitch[1].process (r);
                l += pitchMix * (pl - l); r += pitchMix * (pr - r);
            }
        }
        // SPACE: built-in reverb send
        {
            const float snd = std::max (0.0f, laneSm[lnSpace]);
            if (snd > 1.0e-5f) verbTail = (int) (sr * 5.0);
            if (verbTail > 0)
            {
                float wl, wr; verb.process (l * snd, r * snd, wl, wr);
                l += 0.6f * wl; r += 0.6f * wr;
                if (--verbTail == 0) verb.clear();
            }
        }
        // VOLUME
        // VOLUME: the gain itself is smoothed (1 ms), so jumps in the lane are short fades, never clicks
        {
            const float gT = volTgt == 0.0f ? 1.0f : dbToGain (volTgt < 0 ? volTgt * 40.0f : volTgt * 6.0f);
            volG1 += volA * (gT - volG1); volG += volA * (volG1 - volG);   // two 1.2 ms poles: no corner even on a full jump
            if (std::abs (volG - 1.0f) < 1.0e-6f && std::abs (volG1 - 1.0f) < 1.0e-6f && gT == 1.0f) volG = volG1 = 1.0f;
            if (volG != 1.0f) { l *= volG; r *= volG; }
        }
        L[i] = l; R[i] = r;
    }
}

void BoxMod::matchOut (float* L, float* R, int n) noexcept
{
    const bool on = matchOn.load (std::memory_order_relaxed);
    if (! on && trimNow == 1.0f) return;
    float target = 1.0f;
    if (on)
    {
        outLu.process (L, R, n);
        const float li = inLu.lufs, lo = outLu.lufs;
        lufsIn.store (li, std::memory_order_relaxed);
        if (li > -60.0f && lo > -60.0f) { const float t = clampf (li - lo, -24.0f, 24.0f); trimDb.store (t, std::memory_order_relaxed); target = dbToGain (t); }
        else target = dbToGain (trimDb.load (std::memory_order_relaxed));
        lufsOut.store (lo + gainToDb (trimNow), std::memory_order_relaxed);
    }
    const float a = 1.0f - (float) std::exp (-1.0 / (0.25 * sr));
    for (int i = 0; i < n; ++i) { trimNow += a * (target - trimNow); L[i] *= trimNow; R[i] *= trimNow; }
    if (! on && std::abs (trimNow - 1.0f) < 1.0e-4f) trimNow = 1.0f;
}

// ================================================================ message thread
BoxMod::Snap BoxMod::snapNow (const std::array<int, kNumUnits>& order) const
{
    Snap s;
    if (apvts == nullptr) return s;
    for (auto* p : apvts->processor.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (rp->getParameterID().startsWith ("fx_")) s.vals[rp->getParameterID()] = rp->convertFrom0to1 (rp->getValue());
    juce::StringArray o; for (int u : order) o.add (unitInfo (u).key);
    s.order = o.joinIntoString (",");
    return s;
}

void BoxMod::setState (int which, const Snap& s)
{
    (which == 0 ? snapA : snapB) = s;
    (which == 0 ? hasA : hasB) = true;
    if (! (hasA && hasB)) morphOn = false;
    rebuild();
}

juce::String BoxMod::morphNote() const
{
    if (! hasA || ! hasB) return hasA ? "A is set. Now change the rack and press SET B." : "Press SET A to capture the rack as it is now.";
    juce::String s = juce::String (lastMorphCount) + " settings morph between A and B";
    if (snapA.order != snapB.order) s << "  -  B has a different chain order: MORPH keeps A's order";
    auto par = [] (const Snap& sn) { auto it = sn.vals.find ("fx_par_on"); return it != sn.vals.end() && it->second > 0.5f; };
    if (par (snapA) != par (snapB)) s << "  -  PARALLEL switches at the half-way point";
    return s;
}

void BoxMod::rebuild()
{
    auto c = std::make_unique<ModConfig>();
    c->morphOn = morphOn && hasA && hasB; c->morphCurve = morphCurveSel;
    if (c->morphOn)
    {
        auto val = [] (const Snap& s, const juce::String& id, float def) { auto it = s.vals.find (id); return it == s.vals.end() ? def : it->second; };
        std::array<bool, kNumUnits> onA {}, onB {};
        for (int u = 0; u < kNumUnits; ++u) { onA[(size_t) u] = val (snapA, pid (u, "on"), 0) > 0.5f; onB[(size_t) u] = val (snapB, pid (u, "on"), 0) > 0.5f; }
        for (auto& [id, a] : snapA.vals)
        {
            auto bit = snapB.vals.find (id);
            if (bit == snapB.vals.end()) continue;
            const float b = bit->second;
            const int sl = slotOf (id);
            if (sl < 0) continue;
            const auto& s = *slots[(size_t) sl];
            const int u = s.unit;
            MorphItem it { sl, a, b, a, b, mkLinear };
            if (u >= 0 && id == pid (u, "on"))
            {
                if (a == b) continue;
                it.kind = unitInfo (u).hasMix ? mkOn : mkSwitch;
            }
            else if (u >= 0 && unitInfo (u).hasMix && id == pid (u, "mix") && onA[(size_t) u] != onB[(size_t) u])
            {
                it.kind = mkMix; it.aEff = onA[(size_t) u] ? a : 0.0f; it.bEff = onB[(size_t) u] ? b : 0.0f;
            }
            else
            {
                if (a == b) continue;
                if (u >= 0 && onA[(size_t) u] && ! onB[(size_t) u]) it.kind = mkHoldA;
                else if (u >= 0 && ! onA[(size_t) u] && onB[(size_t) u]) it.kind = mkHoldB;
                else if (u >= 0 && ! onA[(size_t) u] && ! onB[(size_t) u]) it.kind = mkHoldA;
                else if (s.discrete) it.kind = mkSwitch;
                else if (isTimeId (id, s.param)) it.kind = mkTime;
                else if (s.param != nullptr && (s.param->getLabel() == "Hz" || s.param->getLabel() == "ms" || s.param->getLabel() == "s") && a > 0 && b > 0) it.kind = mkLog;
                else it.kind = mkLinear;
            }
            c->morph.push_back (it);
        }
    }
    // SPIRIT BOARD: every rack parameter over the ghosts' scenes (it replaces MORPH while it is on)
    const int nG = juce::jmin ((int) board.ghosts.size(), ghosty::spirit::kMaxGhosts);
    c->boardOn = boardOn && nG > 0; c->boardN = nG;
    if (c->boardOn)
    {
        c->morphOn = false; c->morph.clear();
        std::array<const Snap*, ghosty::spirit::kMaxGhosts> sn {};
        static const Snap empty;
        for (int k = 0; k < nG; ++k) { auto it = boardSnaps.find (board.ghosts[(size_t) k].id); sn[(size_t) k] = it != boardSnaps.end() ? &it->second : &empty; }
        auto val = [] (const Snap& s, const juce::String& id, float def) { auto it = s.vals.find (id); return it == s.vals.end() ? def : it->second; };
        for (int sl = 0; sl < (int) slots.size(); ++sl)
        {
            const auto& s = *slots[(size_t) sl];
            const float host = s.host->load();
            BoardItem it; it.slot = sl; it.unit = s.unit;
            bool allSame = true, allPos = true;
            for (int k = 0; k < nG; ++k)
            {
                it.v[(size_t) k] = val (*sn[(size_t) k], s.id, host);
                it.on[(size_t) k] = s.unit < 0 || val (*sn[(size_t) k], pid (s.unit, "on"), 0) > 0.5f;
                allSame = allSame && it.v[(size_t) k] == it.v[0]; allPos = allPos && it.v[(size_t) k] > 0;
            }
            bool onSame = true; for (int k = 1; k < nG; ++k) onSame = onSame && it.on[(size_t) k] == it.on[0];
            const int u = s.unit;
            if (u >= 0 && s.id == pid (u, "on")) { if (allSame) { it.kind = bkSwitch; } else it.kind = unitInfo (u).hasMix ? bkOnMix : bkSwitch; }
            else if (u >= 0 && unitInfo (u).hasMix && s.id == pid (u, "mix") && ! onSame)
            {   // the unit is on in some ghosts only: its wet fades with the weights of the ghosts that have it on
                it.kind = bkMix; for (int k = 0; k < nG; ++k) it.eff[(size_t) k] = it.on[(size_t) k] ? it.v[(size_t) k] : 0.0f;
            }
            else if (u >= 0 && ! onSame) it.kind = s.discrete ? bkHoldSwitch : bkHold;   // over the ghosts that have the unit on
            else if (s.discrete) it.kind = bkSwitch;
            else if ((isTimeId (s.id, s.param) || (s.param != nullptr && (s.param->getLabel() == "Hz" || s.param->getLabel() == "ms" || s.param->getLabel() == "s"))) && allPos) it.kind = bkLog;
            else it.kind = bkLinear;
            c->board.push_back (it);
        }
    }
    // GHOST MOTION timeline lanes (continuous fx_ settings only: a switch would jump)
    c->tlBars = juce::jlimit (1, 8, timeline.bars);
    for (int k = 0; k < (int) timeline.lanes.size() && k < kTlLanes; ++k)
    {
        const auto& L = timeline.lanes[(size_t) k];
        const int sl = slotOf (L.id);
        if (sl < 0 || slots[(size_t) sl]->discrete) continue;
        TlItem it; it.slot = sl; it.unit = slots[(size_t) sl]->unit; it.lane = k;
        if (it.unit < 0 && (L.id.startsWith ("fx_gspace_") || L.id.startsWith ("fx_print_"))) it.unit = uLim;   // (GHOST SPACE / GHOSTPRINT targets sit just before the limiter)
        it.on = L.on; it.depth = juce::jlimit (-1.0f, 1.0f, L.depth);
        it.ident = (uint32_t) L.id.hashCode() * 31u + (uint32_t) k + 1u;
        it.mode = L.mode; it.steps = tlStepCount (L.stepsIdx); it.st = L.steps;
        for (auto& e : L.events) if (it.nEv < kTlEvents && e.len > 0) it.ev[(size_t) it.nEv++] = e;
        c->tl.push_back (it);
    }
    lastMorphCount = (int) c->morph.size();
    for (int k = 0; k < kMacros; ++k)
        for (auto& a : assigns[(size_t) k])
            if (const int sl = slotOf (a.id); sl >= 0 && ! slots[(size_t) sl]->discrete)   // switches / choices never ride a macro (they would flip with a jump)
                c->macros.push_back ({ sl, k, juce::jlimit (-1.0f, 1.0f, a.depth), a.curve });
    auto* old = cfg.exchange (c.get());
    juce::ignoreUnused (old);
    if (live) retired.push_back ({ std::move (live), blocks.load() });
    live = std::move (c);
    retire();
}

void BoxMod::retire()
{
    const bool idle = inProcess.load() == 0;
    const auto b = blocks.load();
    retired.erase (std::remove_if (retired.begin(), retired.end(), [idle, b] (const auto& r) { return idle || b >= r.second + 1; }), retired.end());
}

bool BoxMod::assign (int macro, const juce::String& id, float depth, int curve)
{
    if (! juce::isPositiveAndBelow (macro, kMacros) || isDiscrete (id)) return false;
    auto& v = assigns[(size_t) macro];
    for (auto& a : v) if (a.id == id) { a.depth = depth; a.curve = curve; rebuild(); return true; }
    v.push_back ({ id, depth, curve });
    rebuild();
    return true;
}
void BoxMod::unassign (int macro, const juce::String& id)
{
    if (! juce::isPositiveAndBelow (macro, kMacros)) return;
    auto& v = assigns[(size_t) macro];
    v.erase (std::remove_if (v.begin(), v.end(), [&id] (const Assign& a) { return a.id == id; }), v.end());
    rebuild();
}
void BoxMod::setDefaultMacros()
{
    using L = std::vector<std::pair<const char*, float>>;
    static const L def[kMacros] {
        { { "fx_filter_cutoff", -0.45f }, { "fx_tube_treble", -0.4f }, { "fx_halls_damp", 0.4f }, { "fx_retro_tone", -0.5f }, { "fx_echo_highcut", -0.4f }, { "fx_split_b_tilt", -0.6f } },
        { { "fx_crackle_b2_drive", 0.4f }, { "fx_retro_wobble", 0.5f }, { "fx_crystal_reverse", 0.5f }, { "fx_echo_fb", 0.3f }, { "fx_filter_lfoamt", 0.5f } },
        { { "fx_halls_size", 0.5f }, { "fx_halls_decay", 0.4f }, { "fx_space_size", 0.5f }, { "fx_shimmer_size", 0.4f }, { "fx_space_decay", 0.3f }, { "fx_split_vdecay", 0.4f } },
        { { "fx_split_attack", 0.5f }, { "fx_ott_mix", 0.3f }, { "fx_crush_crush", 0.3f }, { "fx_fatten_fat", 0.3f } },
        { { "fx_chorus_depth", 0.5f }, { "fx_filter_lfoamt", 0.4f }, { "fx_retro_wobble", 0.3f }, { "fx_echo_mix", 0.2f } },
        { { "fx_chorus_width", 0.5f }, { "fx_echo_width", 0.4f }, { "fx_space_width", 0.4f }, { "fx_split_b_width", 0.4f } },
        { { "fx_wreck_drive", 0.5f }, { "fx_tube_drive", 0.4f }, { "fx_crackle_b1_drive", 0.4f }, { "fx_retro_dist", 0.4f }, { "fx_fatten_crunch", 0.4f }, { "fx_split_a_drive", 0.4f } },
        { { "fx_tube_treble", 0.4f }, { "fx_shimmer_shimmer", 0.4f }, { "fx_space_glimmer", 0.5f }, { "fx_halls_shimmer", 0.4f }, { "fx_split_a_tilt", 0.5f } },
    };
    for (int k = 0; k < kMacros; ++k)
    {
        assigns[(size_t) k].clear();
        for (auto& [id, d] : def[k]) assigns[(size_t) k].push_back ({ id, d, mcLinear });
        macroNames[(size_t) k] = macroDefaultNames()[k];
    }
}

float BoxMod::effective (const juce::String& id) const
{
    const int s = slotOf (id);
    return s < 0 ? 0.0f : slots[(size_t) s]->shadow.load();
}

// ---------------------------------------------------------------- motion
MotionDef BoxMod::currentMotion() const
{
    MotionDef m; m.name = motionName;
    for (int k = 0; k < kLanes; ++k)
    {
        auto& d = m.lanes[(size_t) k]; const auto& p = laneP[(size_t) k];
        d.on = p.on.b(); d.depth = p.depth(); d.len = p.len.i(); d.feel = p.feel.i(); d.shape = lanes[(size_t) k].get();
        d.mode = p.mode.i(); d.stepsIdx = p.stepsN.i(); d.phase = p.phase(); d.steps = getSteps (k);
    }
    return m;
}
void BoxMod::applyMotion (const MotionDef& m)
{
    motionName = m.name;
    for (int k = 0; k < kLanes; ++k)
    {
        const auto& d = m.lanes[(size_t) k];
        lanes[(size_t) k].set (d.shape); setSteps (k, d.steps);
        if (apvts == nullptr) continue;
        const juce::String b = "box_mo_" + juce::String (laneKey (k)) + "_";
        auto set = [this] (const juce::String& id, float v) { if (auto* p = apvts->getParameter (id)) { p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 (v)); p->endChangeGesture(); } };
        set (b + "on", d.on ? 1.0f : 0.0f); set (b + "depth", d.depth); set (b + "len", (float) d.len); set (b + "feel", (float) d.feel);
        set (b + "mode", (float) d.mode); set (b + "steps", (float) d.stepsIdx); set (b + "phase", d.phase);
    }
}
void BoxMod::resetMotion() { MotionDef m; for (auto& l : m.lanes) l.shape = flatShape(); applyMotion (m); }

juce::File motionDir()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Ghosty").getChildFile ("Ghost Box").getChildFile ("Motion");
}
static juce::ValueTree motionToTree (const MotionDef& m)
{
    juce::ValueTree t ("GHOSTMOTION"); t.setProperty ("name", m.name, nullptr); t.setProperty ("version", 1, nullptr);
    for (int k = 0; k < kLanes; ++k)
    {
        const auto& d = m.lanes[(size_t) k];
        juce::ValueTree c ("LANE"); c.setProperty ("lane", laneKey (k), nullptr); c.setProperty ("on", d.on, nullptr);
        c.setProperty ("depth", d.depth, nullptr); c.setProperty ("len", d.len, nullptr); c.setProperty ("feel", d.feel, nullptr);
        c.setProperty ("points", d.shape.toString(), nullptr); c.setProperty ("grid", d.shape.grid, nullptr);
        c.setProperty ("mode", d.mode, nullptr); c.setProperty ("stepsIdx", d.stepsIdx, nullptr); c.setProperty ("phase", d.phase, nullptr);
        juce::StringArray sv; for (int i = 0; i < kMaxSteps; ++i) sv.add (juce::String (d.steps.v[(size_t) i], 3));
        c.setProperty ("steps", sv.joinIntoString (","), nullptr); c.setProperty ("ties", juce::String ((juce::int64) d.steps.ties), nullptr);
        t.appendChild (c, nullptr);
    }
    return t;
}
static void stepsFromTree (const juce::ValueTree& c, StepLane& sl)
{
    auto sv = juce::StringArray::fromTokens (c.getProperty ("steps").toString(), ",", "");
    for (int i = 0; i < kMaxSteps; ++i) sl.v[(size_t) i] = i < sv.size() ? juce::jlimit (-1.0f, 1.0f, sv[i].getFloatValue()) : 0.0f;
    sl.ties = (uint64_t) (juce::int64) c.getProperty ("ties", 0);
}
static MotionDef motionFromTree (const juce::ValueTree& t)
{
    MotionDef m; m.name = t.getProperty ("name").toString();
    for (auto& l : m.lanes) l.shape = flatShape();
    for (auto c : t)
        for (int k = 0; k < kLanes; ++k)
            if (c.getProperty ("lane").toString() == laneKey (k))
            {
                auto& d = m.lanes[(size_t) k];
                d.on = (bool) c.getProperty ("on", false); d.depth = juce::jlimit (0.0f, 1.0f, (float) c.getProperty ("depth", 1.0f));
                d.len = juce::jlimit (0, 6, (int) c.getProperty ("len", 2)); d.feel = juce::jlimit (0, 2, (int) c.getProperty ("feel", 0));
                d.shape = bendlane::Shape::fromString (c.getProperty ("points").toString(), 0, (int) c.getProperty ("grid", 2));
                d.mode = juce::jlimit (0, 1, (int) c.getProperty ("mode", 0)); d.stepsIdx = juce::jlimit (0, 3, (int) c.getProperty ("stepsIdx", 1));
                d.phase = juce::jlimit (0.0f, 1.0f, (float) c.getProperty ("phase", 0.0f)); stepsFromTree (c, d.steps);
            }
    return m;
}
bool saveMotionFile (const MotionDef& m, const juce::File& f)
{
    f.getParentDirectory().createDirectory();
    if (auto x = motionToTree (m).createXml()) return x->writeTo (f);
    return false;
}
bool loadMotionFile (const juce::File& f, MotionDef& out)
{
    auto x = juce::XmlDocument::parse (f);
    if (x == nullptr || ! x->hasTagName ("GHOSTMOTION")) return false;
    out = motionFromTree (juce::ValueTree::fromXml (*x));
    if (out.name.isEmpty()) out.name = f.getFileNameWithoutExtension();
    return true;
}

const std::vector<MotionDef>& factoryMotions()
{
    static const std::vector<MotionDef> v = []
    {
        std::vector<MotionDef> r;
        struct Pt { float x, y, t; bool st; };
        auto shape = [] (std::initializer_list<Pt> pts) { bendlane::Shape s; s.snap = 0; s.grid = 2; for (auto& p : pts) s.add (p.x, p.y, p.t, p.st); s.sanitise(); return s; };
        auto lane = [] (MotionDef& m, int k, int len, int feel, float depth, bendlane::Shape s) { auto& d = m.lanes[(size_t) k]; d.on = true; d.len = len; d.feel = feel; d.depth = depth; d.shape = s; };
        auto mk = [&r] (const char* nm) { MotionDef m; m.name = nm; for (auto& l : m.lanes) l.shape = flatShape(); r.push_back (m); return &r.back(); };
        MotionDef* m;
        m = mk ("4-Bar Riser");      // filter opens from dark to bright, space and damage swell, all snap back on the downbeat
        lane (*m, lnFilter, 4, 0, 1.0f, shape ({ { 0.0f, -0.9f, -0.35f, false }, { 0.985f, 0.3f, 0, false } }));
        lane (*m, lnSpace, 4, 0, 0.9f, shape ({ { 0.0f, 0.0f, -0.5f, false }, { 0.985f, 0.85f, 0, false } }));
        lane (*m, lnDamage, 4, 0, 0.6f, shape ({ { 0.0f, 0.0f, -0.6f, false }, { 0.985f, 0.6f, 0, false } }));
        m = mk ("1-Beat Pump Bass"); // sidechain-style duck on every beat
        lane (*m, lnVolume, 0, 0, 0.8f, shape ({ { 0.0f, -1.0f, 0.55f, false }, { 0.55f, 0.0f, 0, false }, { 0.96f, 0.0f, 0, false } }));
        m = mk ("Half-Time Drop");   // beats 2 and 4 dip, filter sags: the groove feels twice as slow
        lane (*m, lnVolume, 2, 0, 0.7f, shape ({ { 0.0f, 0.0f, 0, false }, { 0.24f, 0.0f, 0, false }, { 0.26f, -0.8f, 0.4f, false }, { 0.48f, 0.0f, 0, false },
                                                 { 0.74f, 0.0f, 0, false }, { 0.76f, -0.8f, 0.4f, false }, { 0.98f, 0.0f, 0, false } }));
        lane (*m, lnFilter, 3, 0, 0.6f, shape ({ { 0.0f, -0.25f, 0.5f, false }, { 0.5f, -0.55f, 0.5f, false } }));
        m = mk ("Filter Breathe");   // slow low-pass breathing over 2 bars
        lane (*m, lnFilter, 3, 0, 0.8f, shape ({ { 0.0f, -0.15f, 0.5f, false }, { 0.5f, -0.75f, 0.5f, false } }));
        m = mk ("Tape Dive");        // the bar ends with a tape-stop style dive
        lane (*m, lnPitch, 2, 0, 1.0f, shape ({ { 0.0f, 0.0f, 0, false }, { 0.6f, 0.0f, 0.6f, false }, { 0.97f, -1.0f, 0, false } }));
        lane (*m, lnDamage, 2, 0, 0.5f, shape ({ { 0.0f, 0.0f, 0, false }, { 0.6f, 0.0f, 0.5f, false }, { 0.97f, 0.7f, 0, false } }));
        m = mk ("Space Throw 1/2");  // a reverb throw on the 2 and the 4
        lane (*m, lnSpace, 1, 0, 1.0f, shape ({ { 0.0f, 0.0f, 0, false }, { 0.45f, 0.0f, 0, false }, { 0.5f, 1.0f, -0.5f, false }, { 0.8f, 0.0f, 0, false } }));
        m = mk ("Stutter Gate 1/16");
        lane (*m, lnVolume, 0, 0, 1.0f, shape ({ { 0.0f, 0.0f, 0, true }, { 0.125f, -1.0f, 0, true }, { 0.25f, 0.0f, 0, true }, { 0.375f, -1.0f, 0, true },
                                                 { 0.5f, 0.0f, 0, true }, { 0.625f, -1.0f, 0, true }, { 0.75f, 0.0f, 0, true }, { 0.875f, -1.0f, 0, true } }));
        m = mk ("Triplet Chop");
        lane (*m, lnVolume, 0, 1, 0.9f, shape ({ { 0.0f, 0.0f, 0, true }, { 0.5f, -1.0f, 0, true } }));
        m = mk ("Dotted Wobble");
        lane (*m, lnFilter, 0, 2, 0.7f, shape ({ { 0.0f, -0.1f, 0.5f, false }, { 0.5f, -0.6f, 0.5f, false } }));
        m = mk ("Damage Swell");
        lane (*m, lnDamage, 3, 0, 0.8f, shape ({ { 0.0f, 0.0f, -0.4f, false }, { 0.9f, 0.9f, 0, false }, { 0.97f, 0.0f, 0, false } }));
        m = mk ("8-Bar Build");
        lane (*m, lnFilter, 5, 0, 1.0f, shape ({ { 0.0f, -0.95f, -0.25f, false }, { 0.99f, 0.15f, 0, false } }));
        lane (*m, lnVolume, 5, 0, 0.5f, shape ({ { 0.0f, -0.6f, -0.3f, false }, { 0.99f, 0.0f, 0, false } }));
        lane (*m, lnSpace, 5, 0, 0.7f, shape ({ { 0.0f, 0.1f, -0.4f, false }, { 0.99f, 0.8f, 0, false } }));
        auto stepLane = [] (MotionDef& md, int k, int len, float depth, int stepsIdx, std::initializer_list<float> vals)
        {
            auto& d = md.lanes[(size_t) k]; d.on = true; d.len = len; d.depth = depth; d.mode = 1; d.stepsIdx = stepsIdx; d.steps.count = motionStepCount (stepsIdx);
            int i = 0; for (float v : vals) if (i < kMaxSteps) d.steps.v[(size_t) i++] = v;
        };
        m = mk ("Tracker Gate 1/32");   // 32 steps over a bar: a hard on/off pattern that only steps can do
        stepLane (*m, lnVolume, 2, 1.0f, 2, { 0, -1, 0, 0, -1, 0, -1, -1, 0, 0, -1, 0, 0, -1, -1, 0, 0, -1, 0, 0, -1, 0, -1, -1, 0, 0, -1, 0, -1, -1, 0, -1 });
        m = mk ("Pitch Steps 1/16");   // a chip-tune style pitch ladder on the built-in shifter / WARP
        stepLane (*m, lnPitch, 2, 1.0f, 1, { 0, 0, 0.583f, 0.583f, 1.0f, 1.0f, 0.583f, 0.583f, 0, 0, -0.417f, -0.417f, 0, 0, 0.583f, 1.0f });
        m = mk ("Filter Steps 1/8");   // eight filter positions per bar, tied pairs
        stepLane (*m, lnFilter, 2, 0.9f, 0, { -0.9f, -0.6f, -0.3f, 0.0f, -0.2f, -0.5f, -0.7f, -0.9f });
        m = mk ("Breakdown Float");
        lane (*m, lnSpace, 4, 0, 0.8f, shape ({ { 0.0f, 0.5f, 0.5f, false }, { 0.5f, 0.9f, 0.5f, false } }));
        lane (*m, lnFilter, 4, 0, 0.7f, shape ({ { 0.0f, -0.35f, 0.5f, false }, { 0.5f, -0.65f, 0.5f, false } }));
        lane (*m, lnPitch, 4, 0, 0.15f, shape ({ { 0.0f, 0.0f, 0.5f, false }, { 0.5f, 0.3f, 0.5f, false } }));
        return r;
    }();
    return v;
}

// ---------------------------------------------------------------- GHOST MOTION timeline (message thread)
juce::ValueTree timelineToTree (const TimelineDef& d)
{
    juce::ValueTree t ("TIMELINE");
    t.setProperty ("bars", d.bars, nullptr); t.setProperty ("rule", d.rule, nullptr); t.setProperty ("evLen", d.eventLen, nullptr); t.setProperty ("evAmt", d.eventAmt, nullptr);
    for (auto& L : d.lanes)
    {
        juce::ValueTree c ("TL"); c.setProperty ("id", L.id, nullptr); c.setProperty ("on", L.on, nullptr); c.setProperty ("depth", L.depth, nullptr);
        c.setProperty ("mode", L.mode, nullptr); c.setProperty ("stepsIdx", L.stepsIdx, nullptr);
        c.setProperty ("points", L.shape.toString(), nullptr); c.setProperty ("grid", L.shape.grid, nullptr);
        juce::StringArray sv; for (float v : L.steps) sv.add (juce::String (v, 4));
        c.setProperty ("steps", sv.joinIntoString (","), nullptr);
        juce::StringArray ev; for (auto& e : L.events) ev.add (juce::String (e.pos, 6) + ":" + juce::String (e.len, 6) + ":" + juce::String (e.amt, 4) + ":" + (e.autoPlaced ? "1" : "0"));
        c.setProperty ("events", ev.joinIntoString (";"), nullptr);
        t.appendChild (c, nullptr);
    }
    return t;
}
TimelineDef timelineFromTree (const juce::ValueTree& t)
{
    TimelineDef d;
    if (! t.isValid()) return d;
    d.bars = juce::jlimit (1, 8, (int) t.getProperty ("bars", 1)); d.rule = juce::jlimit (0, tlRuleNames().size() - 1, (int) t.getProperty ("rule", 0));
    d.eventLen = juce::jlimit (0, tlEventLenNames().size() - 1, (int) t.getProperty ("evLen", 1)); d.eventAmt = juce::jlimit (-1.0f, 1.0f, (float) t.getProperty ("evAmt", 1.0f));
    for (auto c : t)
    {
        if (! c.hasType ("TL") || (int) d.lanes.size() >= kTlLanes) continue;
        TlLane L; L.id = c.getProperty ("id").toString(); L.on = (bool) c.getProperty ("on", true); L.depth = juce::jlimit (-1.0f, 1.0f, (float) c.getProperty ("depth", 0.5f));
        L.mode = juce::jlimit (0, 1, (int) c.getProperty ("mode", 0)); L.stepsIdx = juce::jlimit (0, 3, (int) c.getProperty ("stepsIdx", 1));
        L.shape = c.hasProperty ("points") ? bendlane::Shape::fromString (c.getProperty ("points").toString(), 0, (int) c.getProperty ("grid", 2)) : flatShape();
        auto sv = juce::StringArray::fromTokens (c.getProperty ("steps").toString(), ",", "");
        for (int i = 0; i < kTlMaxSteps; ++i) L.steps[(size_t) i] = i < sv.size() ? juce::jlimit (-1.0f, 1.0f, sv[i].getFloatValue()) : 0.0f;
        for (auto& tok : juce::StringArray::fromTokens (c.getProperty ("events").toString(), ";", ""))
        {
            auto f = juce::StringArray::fromTokens (tok, ":", "");
            if (f.size() < 3 || (int) L.events.size() >= kTlEvents) continue;
            TlEvent e; e.pos = juce::jlimit (0.0, 32.0, f[0].getDoubleValue()); e.len = juce::jlimit (0.0, 32.0, f[1].getDoubleValue()); e.amt = juce::jlimit (-1.0f, 1.0f, f[2].getFloatValue()); e.autoPlaced = f.size() > 3 && f[3] == "1";
            if (e.len > 0) L.events.push_back (e);
        }
        if (L.id.isNotEmpty()) d.lanes.push_back (L);
    }
    return d;
}

TimelineDef BoxMod::tlCurrent() const
{
    TimelineDef d = timeline;
    for (int k = 0; k < (int) d.lanes.size() && k < kTlLanes; ++k) d.lanes[(size_t) k].shape = tlShapes[(size_t) k].get();
    return d;
}
void BoxMod::tlLoad (const TimelineDef& d)
{
    timeline = d;
    if ((int) timeline.lanes.size() > kTlLanes) timeline.lanes.resize ((size_t) kTlLanes);
    for (int k = 0; k < kTlLanes; ++k) tlShapes[(size_t) k].set (k < (int) timeline.lanes.size() ? timeline.lanes[(size_t) k].shape : flatShape());
    rebuild();
}
int BoxMod::tlAddLane (const juce::String& id)
{
    for (int k = 0; k < (int) timeline.lanes.size(); ++k) if (timeline.lanes[(size_t) k].id == id) return k;
    if (! tlAllowed (id) || (int) timeline.lanes.size() >= kTlLanes) return -1;
    TlLane L; L.id = id; L.shape = flatShape();
    timeline.lanes.push_back (L);
    const int k = (int) timeline.lanes.size() - 1;
    tlShapes[(size_t) k].set (L.shape);
    rebuild();
    return k;
}
void BoxMod::tlRemoveLane (int lane)
{
    if (! juce::isPositiveAndBelow (lane, (int) timeline.lanes.size())) return;
    auto d = tlCurrent();
    d.lanes.erase (d.lanes.begin() + lane);
    tlLoad (d);
}
juce::String BoxMod::tlLaneName (int lane) const
{
    if (! juce::isPositiveAndBelow (lane, (int) timeline.lanes.size())) return {};
    const auto& id = timeline.lanes[(size_t) lane].id;
    const int sl = slotOf (id);
    const int u = sl >= 0 ? slots[(size_t) sl]->unit : -1;
    juce::String nm = sl >= 0 && slots[(size_t) sl]->param != nullptr ? slots[(size_t) sl]->param->getName (60) : id;
    if (u >= 0) nm = nm.replace ("FX ", "").replace (unitInfo (u).title, "").trim();
    else nm = nm.replace ("FX ", "");
    return (u >= 0 ? juce::String (unitInfo (u).shortName) + "  " : juce::String()) + nm;
}
int BoxMod::tlPlaceOnHits (int lane, const TlAnalysis& a, int rule)
{
    if (! juce::isPositiveAndBelow (lane, (int) timeline.lanes.size()) || ! a.ok) return 0;
    auto& L = timeline.lanes[(size_t) lane];
    L.events.erase (std::remove_if (L.events.begin(), L.events.end(), [] (const TlEvent& e) { return e.autoPlaced; }), L.events.end());
    const double loopBeats = juce::jlimit (1, 8, timeline.bars) * tlBpb.load();
    int n = 0;
    for (int i : tlPickHits (a.hits, rule, tlBpb.load()))
    {
        if ((int) L.events.size() >= kTlEvents) break;
        TlEvent e; e.pos = juce::jlimit (0.0, loopBeats, a.hits[(size_t) i].beat); e.len = std::min (tlEventLenBeats (timeline.eventLen), loopBeats); e.amt = timeline.eventAmt; e.autoPlaced = true;
        L.events.push_back (e); ++n;
    }
    std::sort (L.events.begin(), L.events.end(), [] (const TlEvent& x, const TlEvent& y) { return x.pos < y.pos; });
    rebuild();
    return n;
}
int BoxMod::tlChunkLimit (double ppq, double spb, double bpb, int n) const noexcept
{
    if (! timelineActive()) return n;
    const double loopBeats = (double) juce::jlimit (1, 8, cfgNow->tlBars) * (bpb > 0.0 ? bpb : 4.0);
    const double next = ppqOrigin + (std::floor ((ppq - ppqOrigin) / loopBeats + 1.0e-9) + 1.0) * loopBeats;
    const double x = (next - ppq) * spb;
    if (x <= 1.0e-6) return n;
    const int s = (int) std::ceil (x - 1.0e-6);
    return s > 0 && s < n ? s : n;
}
TlAnalysis BoxMod::tlDetectNow() const
{
    const double loopBeats = juce::jlimit (1, 8, timeline.bars) * tlBpb.load();
    return tlAnalyseLastLoop (onsets.snapshot(), tlOrigin.load(), loopBeats, tlSpb.load(), onsets.hopSamples());
}
void BoxMod::tlDetectAsync (std::function<void (TlAnalysis)> done)
{   // the copy of the ring is made here (cheap); the analysis runs on the worker
    if (tlPool == nullptr) tlPool = std::make_unique<juce::ThreadPool> (1);
    auto recs = std::make_shared<std::vector<OnsetRec>> (onsets.snapshot());
    const double loopBeats = juce::jlimit (1, 8, timeline.bars) * tlBpb.load(), origin = tlOrigin.load(), spb = tlSpb.load(), hop = onsets.hopSamples();
    auto alive = tlAlive;
    tlPool->addJob ([this, recs, loopBeats, origin, spb, hop, alive, done]
    {
        auto res = std::make_shared<TlAnalysis> (tlAnalyseLastLoop (*recs, origin, loopBeats, spb, hop));
        juce::MessageManager::callAsync ([this, res, alive, done] { if (! *alive) return; lastHits = *res; hitsVersion.fetch_add (1); if (done) done (*res); });
    });
}

// ---------------------------------------------------------------- state
juce::String BoxMod::snapText (const Snap& s) { return snapToString (s.vals) + s.order; }
juce::ValueTree BoxMod::saveBoard() const
{
    auto t = board.toTree();
    t.setProperty ("on", boardOn, nullptr);
    for (auto c : t)
        if (c.hasType ("GHOST"))
        {
            if (auto it = boardSnaps.find (c.getProperty ("id").toString()); it != boardSnaps.end())
            {
                c.setProperty ("scene", snapToString (it->second.vals), nullptr); c.setProperty ("order", it->second.order, nullptr);
                if (it->second.rack.isValid()) c.appendChild (it->second.rack.createCopy(), nullptr);   // the rack's own state (LAYER)
            }
            if (auto pt = boardPct.find (c.getProperty ("id").toString()); pt != boardPct.end()) c.setProperty ("pct", pt->second, nullptr);
        }
    return t;
}
void BoxMod::loadBoard (const juce::ValueTree& t)
{
    board = ghosty::spirit::Board::fromTree (t);
    boardSnaps.clear(); boardPct.clear(); ++boardContentVersion;
    for (auto c : t)
        if (c.hasType ("GHOST") && board.indexOf (c.getProperty ("id").toString()) >= 0)
        {
            Snap s; s.vals = snapFromString (c.getProperty ("scene").toString()); s.order = c.getProperty ("order").toString();
            if (c.getNumChildren() > 0) s.rack = c.getChild (0).createCopy();
            boardSnaps[c.getProperty ("id").toString()] = s;
            if (c.hasProperty ("pct")) boardPct[c.getProperty ("id").toString()] = juce::jlimit (0.0f, 100.0f, (float) c.getProperty ("pct"));
        }
    boardOn = t.isValid() && (bool) t.getProperty ("on", false) && ! board.ghosts.empty();
    if (boardOn) morphOn = false;
    boardShared.publish (board);
    rebuild();
}
std::array<float, ghosty::spirit::kMaxGhosts> BoxMod::boardWeightsNow() const
{
    std::array<float, ghosty::spirit::kMaxGhosts> w {};
    for (int k = 0; k < ghosty::spirit::kMaxGhosts; ++k) w[(size_t) k] = boardShared.liveW[(size_t) k].load (std::memory_order_relaxed);
    return w;
}

juce::ValueTree BoxMod::save() const
{
    juce::ValueTree t ("BOX3");
    if (! board.ghosts.empty() || boardOn) t.appendChild (saveBoard(), nullptr);   // SPIRIT BOARD (none = an older session's layout)
    {
        juce::ValueTree m ("MORPH");
        m.setProperty ("on", morphOn, nullptr); m.setProperty ("curve", morphCurveSel, nullptr);
        m.setProperty ("hasA", hasA, nullptr); m.setProperty ("hasB", hasB, nullptr);
        if (hasA) { m.setProperty ("a", snapToString (snapA.vals), nullptr); m.setProperty ("orderA", snapA.order, nullptr); }
        if (hasB) { m.setProperty ("b", snapToString (snapB.vals), nullptr); m.setProperty ("orderB", snapB.order, nullptr); }
        t.appendChild (m, nullptr);
    }
    {
        juce::ValueTree m ("MACROS");
        for (int k = 0; k < kMacros; ++k)
        {
            juce::ValueTree c ("M"); c.setProperty ("i", k, nullptr); c.setProperty ("name", macroNames[(size_t) k], nullptr);
            for (auto& a : assigns[(size_t) k])
            {
                juce::ValueTree x ("A"); x.setProperty ("id", a.id, nullptr); x.setProperty ("depth", a.depth, nullptr); x.setProperty ("curve", a.curve, nullptr);
                c.appendChild (x, nullptr);
            }
            m.appendChild (c, nullptr);
        }
        t.appendChild (m, nullptr);
    }
    {
        juce::ValueTree m ("MOTION"); m.setProperty ("name", motionName, nullptr);
        for (int k = 0; k < kLanes; ++k)
        {
            const auto s = lanes[(size_t) k].get(); const auto st = getSteps (k);
            juce::ValueTree c ("L"); c.setProperty ("lane", laneKey (k), nullptr); c.setProperty ("points", s.toString(), nullptr); c.setProperty ("grid", s.grid, nullptr);
            juce::StringArray sv; for (int i = 0; i < kMaxSteps; ++i) sv.add (juce::String (st.v[(size_t) i], 3));
            c.setProperty ("steps", sv.joinIntoString (","), nullptr); c.setProperty ("ties", juce::String ((juce::int64) st.ties), nullptr);
            m.appendChild (c, nullptr);
        }
        t.appendChild (m, nullptr);
    }
    {   // GHOST MOTION timeline (none = the default: an older session's layout, saved exactly as before)
        const auto d = tlCurrent();
        if (! d.isDefault()) t.appendChild (timelineToTree (d), nullptr);
    }
    return t;
}

void BoxMod::load (const juce::ValueTree& rackTree)
{
    const auto t = rackTree.getChildWithName ("BOX3");
    loadBoard (t.getChildWithName ("SPIRITBOARD"));   // (none: an empty board, off)
    const auto m = t.getChildWithName ("MORPH");
    hasA = m.isValid() && (bool) m.getProperty ("hasA", false); hasB = m.isValid() && (bool) m.getProperty ("hasB", false);
    snapA = {}; snapB = {};
    if (hasA) { snapA.vals = snapFromString (m.getProperty ("a").toString()); snapA.order = m.getProperty ("orderA").toString(); }
    if (hasB) { snapB.vals = snapFromString (m.getProperty ("b").toString()); snapB.order = m.getProperty ("orderB").toString(); }
    morphOn = m.isValid() && (bool) m.getProperty ("on", false) && hasA && hasB && ! boardOn;
    morphCurveSel = m.isValid() ? juce::jlimit (0, 3, (int) m.getProperty ("curve", 1)) : 1;
    if (const auto mc = t.getChildWithName ("MACROS"); mc.isValid())
    {
        for (auto& a : assigns) a.clear();
        for (auto c : mc)
        {
            const int k = c.getProperty ("i", -1);
            if (! juce::isPositiveAndBelow (k, kMacros)) continue;
            macroNames[(size_t) k] = c.getProperty ("name", macroDefaultNames()[k]).toString();
            for (auto x : c) assigns[(size_t) k].push_back ({ x.getProperty ("id").toString(), juce::jlimit (-1.0f, 1.0f, (float) x.getProperty ("depth", 0.5f)), juce::jlimit (0, 3, (int) x.getProperty ("curve", 0)) });
        }
    }
    else setDefaultMacros();
    const auto mo = t.getChildWithName ("MOTION");
    motionName = mo.getProperty ("name").toString();
    for (int k = 0; k < kLanes; ++k)
    {
        bendlane::Shape s = flatShape(); StepLane sl;
        for (auto c : mo)
            if (c.getProperty ("lane").toString() == laneKey (k)) { s = bendlane::Shape::fromString (c.getProperty ("points").toString(), 0, (int) c.getProperty ("grid", 2)); stepsFromTree (c, sl); }
        lanes[(size_t) k].set (s); setSteps (k, sl);
    }
    tlLoad (timelineFromTree (t.getChildWithName ("TIMELINE")));   // (calls rebuild)
}

} // namespace gpfx
#endif
