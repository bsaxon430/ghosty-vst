// GHOST SCENES, the audio side (see FxBoxScenes.h).
#include "FxBoxScenes.h"
#if GPFX_BOX

namespace gpfx {

SceneEngine::SceneEngine() = default;
SceneEngine::~SceneEngine() { data.store (nullptr); }

void SceneEngine::attach (std::vector<SceneSlotInfo> slots, std::array<std::atomic<int>, kNumUnits>* po, std::atomic<int>* ov, std::atomic<int>* dv)
{
    info = std::move (slots);
    const size_t S = info.size();
    cur.assign (S, 0.0f); from.assign (S, 0.0f); to.assign (S, 0.0f); retTo.assign (S, 0.0f); kind.assign (S, kSame);
    pendingOrder = po; orderVersion = ov; dataVersion = dv;
}

void SceneEngine::prepare (double s)
{   // audio is stopped: start clean (a running transition is dropped; the host values are what plays)
    sr = s > 0 ? s : 48000.0;
    isOn = landed = hasPend = false; nHeld = 0; holdLanded = false; spill.fill (false); spillCount.store (0);
    pending.store (-1); progress.store (1.0f); syncParam = true; lastPpq = -1.0e9;
    int k = 0; while (fifo.getNumReady() > 0 && k++ < 64) { auto sc = fifo.read (1); juce::ignoreUnused (sc); }
}

void SceneEngine::publish (std::unique_ptr<SceneData> d)
{
    auto* raw = d.get();
    data.exchange (raw, std::memory_order_acq_rel);
    if (live) retired.push_back ({ std::move (live), blocks.load() });
    live = std::move (d);
    retire();
}
void SceneEngine::retire()
{
    const bool idle = inProcess.load() == 0; const auto b = blocks.load();
    retired.erase (std::remove_if (retired.begin(), retired.end(), [idle, b] (const auto& r) { return idle || b >= r.second + 2; }), retired.end());
}
bool SceneEngine::hasScene (int k) const { return live != nullptr && juce::isPositiveAndBelow (k, kScenes) && ! live->to[(size_t) k].empty(); }

bool SceneEngine::post (int type, int scene)
{
    auto w = fifo.write (1);
    if (w.blockSize1 + w.blockSize2 < 1) return false;
    cmds[(size_t) (w.blockSize1 > 0 ? w.startIndex1 : w.startIndex2)] = { type, scene };
    return true;
}

// ================================================================ audio thread
double SceneEngine::gridFor (double atPpq, double& q) const noexcept
{
    q = blkPlaying ? sceneQuantBeats (quant.load (std::memory_order_relaxed), bpb) : 0.0;
    if (q <= 0.0) return atPpq;
    const double origin = (q >= bpb - 1.0e-9 && haveBar) ? barStart : 0.0;
    const double k = std::ceil ((atPpq - origin) / q - 1.0e-7);
    return origin + k * q;
}

void SceneEngine::trigger (int scene, double atPpq, bool hold) noexcept
{
    auto* d = data.load (std::memory_order_acquire);
    if (scene != kSceneReturn && (d == nullptr || ! juce::isPositiveAndBelow (scene, kScenes) || d->to[(size_t) scene].empty())) return;   // an empty slot
    if (! hold && scene != kSceneReturn) { nHeld = 0; holdLanded = false; }   // a latch ends any hold (no return)
    double q = 0; pendPpq = gridFor (atPpq, q); pendQ = q;
    pendScene = scene; pendHold = hold; hasPend = true;
    pending.store (scene == kSceneReturn ? retActive : scene, std::memory_order_relaxed);
}

void SceneEngine::release (int scene, double atPpq) noexcept
{
    int k = -1; for (int i = 0; i < nHeld; ++i) if (held[(size_t) i] == scene) k = i;
    if (k < 0) return;
    for (int i = k; i + 1 < nHeld; ++i) held[(size_t) i] = held[(size_t) i + 1];
    --nHeld;
    if (nHeld > 0) { trigger (held[(size_t) nHeld - 1], atPpq, true); return; }   // back to the scene still held
    if (hasPend && pendHold && ! holdLanded) { hasPend = false; pending.store (-1); return; }   // released before it landed: nothing happened
    if (holdLanded) trigger (kSceneReturn, atPpq, false);
}

void SceneEngine::midiNote (int note, bool noteOn, double atPpq) noexcept
{
    if (! midiOn.load (std::memory_order_relaxed)) return;
    const int k = note - midiBase.load (std::memory_order_relaxed);
    if (! juce::isPositiveAndBelow (k, kScenes)) return;
    if (midiLatch.load (std::memory_order_relaxed)) { if (noteOn) trigger (k, atPpq, false); return; }
    if (noteOn)
    {
        for (int i = 0; i < nHeld; ++i) if (held[(size_t) i] == k) return;
        if (nHeld < kScenes) held[(size_t) nHeld++] = k;
        trigger (k, atPpq, true);
    }
    else release (k, atPpq);
}

void SceneEngine::beginBlock (double ppq, double spb, bool playing, double beatsPerBar, double bs, bool hb) noexcept
{
    inProcess.store (1, std::memory_order_release);
    blkPpq = ppq; blkSpb = spb; blkPlaying = playing; bpb = beatsPerBar > 0.5 ? beatsPerBar : 4.0; barStart = bs; haveBar = hb;
    if (clearReq.exchange (false))
    {   // a host restore / another load: what plays now is the host's state
        isOn = landed = hasPend = false; nHeld = 0; holdLanded = false; spill.fill (false); spillCount.store (0);
        pending.store (-1); progress.store (1.0f); syncParam = true; active.store (clearActive.load());
    }
    // commands from the window
    for (int guard = 0; fifo.getNumReady() > 0 && guard < 64; ++guard)
    {
        std::pair<int, int> c;
        {
            auto r = fifo.read (1);
            if (r.blockSize1 + r.blockSize2 < 1) break;
            c = cmds[(size_t) (r.blockSize1 > 0 ? r.startIndex1 : r.startIndex2)];
        }
        const int type = c.first, k = c.second;
        if (! juce::isPositiveAndBelow (k, kScenes)) continue;
        if (type == 0) trigger (k, ppq, false);
        else if (type == 1)
        {
            bool already = false; for (int i = 0; i < nHeld; ++i) already = already || held[(size_t) i] == k;
            if (! already && nHeld < kScenes) held[(size_t) nHeld++] = k;
            trigger (k, ppq, true);
        }
        else if (type == 2) release (k, ppq);
        else if (type == 3)
        {   // a short click: it stays (a latch), nothing to go back to
            int j = -1; for (int i = 0; i < nHeld; ++i) if (held[(size_t) i] == k) j = i;
            if (j >= 0) { for (int i = j; i + 1 < nHeld; ++i) held[(size_t) i] = held[(size_t) i + 1]; --nHeld; }
            if (nHeld == 0) { holdLanded = false; if (hasPend && pendScene == k) pendHold = false; }
        }
    }
    // host automation of 'box_scene': a change to 1..8 triggers that scene (the mirror of the active scene does not)
    if (sceneParam != nullptr)
    {
        const int v = juce::jlimit (0, kScenes, (int) std::lround (sceneParam->load (std::memory_order_relaxed)));
        if (syncParam) { lastParam = v; syncParam = false; }
        else if (v != lastParam)
        {
            lastParam = v;
            if (v >= 1 && v - 1 != active.load (std::memory_order_relaxed) && ! (hasPend && pendScene == v - 1)) trigger (v - 1, ppq, false);
        }
    }
}
void SceneEngine::endBlock() noexcept
{
    blocks.fetch_add (1, std::memory_order_acq_rel);
    inProcess.store (0, std::memory_order_release);
}

int SceneEngine::chunkLimit (double ppq, double spb, int n) const noexcept
{
    if (! hasPend) return n;
    const double x = (pendPpq - ppq) * spb;
    if (x <= 1.0e-6) return n;   // lands at this chunk's start
    const int s = (int) std::ceil (x - 1.0e-6);
    return s > 0 && s < n ? s : n;
}

void SceneEngine::land (double ppq, double spb) noexcept
{
    auto* d = data.load (std::memory_order_acquire);
    const int scene = pendScene; const bool isRet = scene == kSceneReturn;
    hasPend = false; pending.store (-1, std::memory_order_relaxed);
    if (! isRet && (d == nullptr || d->to[(size_t) scene].empty())) return;
    const size_t S = info.size();
    const auto& tv = isRet ? retTo : d->to[(size_t) scene];
    if (tv.size() != S) return;
    // the start point: what plays now (the layer's values while it runs, else the host's)
    for (size_t i = 0; i < S; ++i) from[i] = isOn ? cur[i] : info[i].host->load (std::memory_order_relaxed);
    const float mixBase = isOn ? mixCur : (mixParam != nullptr ? mixParam->load (std::memory_order_relaxed) : 1.0f);
    if (pendHold && ! holdLanded && ! isRet)
    {   // the return point of a hold: everything as it is now
        std::copy (from.begin(), from.end(), retTo.begin()); retMix = mixBase; retActive = active.load (std::memory_order_relaxed);
        if (pendingOrder != nullptr) for (int i = 0; i < kNumUnits; ++i) retOrder[(size_t) i] = (*pendingOrder)[(size_t) i].load (std::memory_order_relaxed);
        holdLanded = true;
    }
    for (size_t i = 0; i < S; ++i) to[i] = tv[i];
    const int m = mode.load (std::memory_order_relaxed);
    std::array<bool, kNumUnits> onA {}, onB {};
    std::array<int, kNumUnits> onSlot; onSlot.fill (-1);
    for (size_t i = 0; i < S; ++i)
        if (info[i].isOn && info[i].unit >= 0) { onA[(size_t) info[i].unit] = from[i] > 0.5f; onB[(size_t) info[i].unit] = to[i] > 0.5f; onSlot[(size_t) info[i].unit] = (int) i; }
    // SPILL: units that rang and the new scene turns off keep ringing; a ringing unit the new scene turns on continues normally
    int nsp = 0;
    for (int u = 0; u < kNumUnits; ++u)
    {
        if (spill[(size_t) u] && onB[(size_t) u]) spill[(size_t) u] = false;
        else if (! spill[(size_t) u] && m == smSpill && sceneSpillUnit (u) && onA[(size_t) u] && ! onB[(size_t) u]) spill[(size_t) u] = true;
        nsp += spill[(size_t) u] ? 1 : 0;
    }
    spillCount.store (nsp, std::memory_order_relaxed);
    for (size_t i = 0; i < S; ++i)
    {
        const auto& in = info[i]; const int u = in.unit;
        uint8_t k;
        if (u >= 0 && spill[(size_t) u]) k = kSpill;
        else if (! (from[i] < to[i] || from[i] > to[i]) && ! (in.isMix && u >= 0 && onA[(size_t) u] != onB[(size_t) u])) k = kSame;
        else if (in.isOn) k = (u >= 0 && unitInfo (u).hasMix) ? kOn : kSwitch;
        else if (in.isMix && u >= 0 && onA[(size_t) u] != onB[(size_t) u]) { k = kMix; mixFromEff[(size_t) u] = onA[(size_t) u] ? from[i] : 0.0f; mixToEff[(size_t) u] = onB[(size_t) u] ? to[i] : 0.0f; }
        else if (u >= 0 && onA[(size_t) u] && ! onB[(size_t) u]) k = kHoldFrom;
        else if (u >= 0 && ! onA[(size_t) u]) k = kHoldTo;   // off -> on, or off in both: its settings are in place before it is heard
        else if (in.discrete || in.time) k = kSwitch;
        else if (in.logish && from[i] > 0.0f && to[i] > 0.0f) k = kLog;
        else k = kLin;
        kind[i] = k;
    }
    juce::ignoreUnused (onSlot);
    // the chain order: at the landing (the rack's click-free reorder)
    if (pendingOrder != nullptr && orderVersion != nullptr)
    {
        const std::array<int, kNumUnits>* want = isRet ? &retOrder : (d->hasOrder[(size_t) scene] ? &d->order[(size_t) scene] : nullptr);
        if (want != nullptr)
        {
            bool same = true; for (int i = 0; i < kNumUnits && same; ++i) same = (*pendingOrder)[(size_t) i].load (std::memory_order_relaxed) == (*want)[(size_t) i];
            if (! same)
            {
                for (int i = 0; i < kNumUnits; ++i) (*pendingOrder)[(size_t) i].store ((*want)[(size_t) i], std::memory_order_relaxed);
                orderVersion->fetch_add (1, std::memory_order_release); if (dataVersion != nullptr) dataVersion->fetch_add (1);
            }
        }
    }
    mixFrom = mixBase; mixTo = isRet ? retMix : d->mix[(size_t) scene];
    // MORPH: the morph time; CUT / SPILL: a 10 ms de-click glide of the continuous settings (switches flip at the landing, in the duck)
    glideOnly = m != smMorph;
    autoPos = glideOnly ? 1.0e-6f : 0.0f;
    morphSamples = glideOnly ? std::max (64.0, 0.010 * sr) : std::max (64.0, sceneMorphBeats (morphLen.load (std::memory_order_relaxed), bpb) * spb);
    isOn = landed = true; minOnLeft = (int) (0.03 * sr);
    const int act = isRet ? retActive : scene;
    if (isRet) holdLanded = false;
    active.store (act, std::memory_order_relaxed);
    lastLandPpq.store (ppq, std::memory_order_relaxed); lastLandScene.store (act, std::memory_order_relaxed);
    landScene.store (isRet ? act : scene, std::memory_order_relaxed); landReturn.store (isRet, std::memory_order_relaxed); landHold.store (pendHold && ! isRet, std::memory_order_relaxed);
    mySeq = landSeq.load (std::memory_order_relaxed) + 1;
    landSeq.store (mySeq, std::memory_order_release);
}

void SceneEngine::beginChunk (double ppq, double spb, int n) noexcept
{
    if (info.empty()) return;
    if (hasPend)
    {
        if (ppq < lastPpq - 1.0e-6 && pendQ > 0.0) { double q = 0; pendPpq = gridFor (ppq, q); pendQ = q; }   // the host jumped back (loop): the next grid line from here
        if (! blkPlaying || (pendPpq - ppq) * spb <= 1.0e-6) land (ppq, spb);
    }
    lastPpq = ppq + (double) n / spb;
    if (! landed) { isOn = false; return; }
    committedNow = commitSeq.load (std::memory_order_acquire) == mySeq;
    const float mp = morphParam != nullptr ? clampf (morphParam->load (std::memory_order_relaxed), 0.0f, 1.0f) : 1.0f;
    bool anySpill = false; for (int u = 0; u < kNumUnits; ++u) anySpill = anySpill || spill[(size_t) u];
    pEff = std::min (autoPos, mp);
    isOn = ! (committedNow && autoPos >= 1.0f && mp >= 1.0f && ! anySpill && minOnLeft <= 0);   // (30 ms at least: the ducked flips finish)
    minOnLeft -= n;
    progress.store (autoPos, std::memory_order_relaxed);
    if (isOn) compute();
    if (autoPos < 1.0f) autoPos = (float) std::min (1.0, (double) autoPos + (double) n / morphSamples);
}

void SceneEngine::compute() noexcept
{
    const float p = pEff, c = p * p * (3.0f - 2.0f * p);
    const bool lo = p <= 0.0f, hi = p >= 1.0f;
    const size_t S = info.size();
    for (size_t i = 0; i < S; ++i)
    {
        const auto& in = info[i];
        const float host = in.host->load (std::memory_order_relaxed);
        const float t = committedNow ? host : to[i], f = from[i];
        float v;
        switch (kind[i])
        {
            case kSame:     v = t; break;
            case kLin:      v = hi ? t : f + (t - f) * c; break;
            case kLog:      v = hi ? t : (f > 0.0f && t > 0.0f ? std::exp (std::log (f) + (std::log (t) - std::log (f)) * c) : f + (t - f) * c); break;
            case kSwitch:   v = (glideOnly ? p > 0.0f : p >= 0.5f) ? t : f; break;
            case kOn:       v = lo ? f : hi ? t : 1.0f; break;
            case kMix:      v = lo ? f : hi ? t : mixFromEff[(size_t) in.unit] + ((committedNow ? (t > 0.0f && mixToEff[(size_t) in.unit] > 0.0f ? t : 0.0f) : mixToEff[(size_t) in.unit]) - mixFromEff[(size_t) in.unit]) * c; break;
            case kHoldFrom: v = hi ? t : f; break;
            case kHoldTo:   v = lo ? f : t; break;
            case kSpill:    v = in.unit >= 0 && spill[(size_t) in.unit] ? f : t; break;
            default:        v = t; break;
        }
        cur[i] = std::isfinite (v) ? v : t;
    }
}

float SceneEngine::mixValue (float host) noexcept
{
    if (! isOn) { mixCur = host; return host; }
    const float f = mixFrom, t = committedNow ? host : mixTo;
    const float p = pEff, c = p * p * (3.0f - 2.0f * p);
    mixCur = p >= 1.0f ? t : f + (t - f) * c;
    return mixCur;
}

void SceneEngine::spillDone (int u) noexcept
{
    if (! juce::isPositiveAndBelow (u, kNumUnits) || ! spill[(size_t) u]) return;
    spill[(size_t) u] = false;
    int nsp = 0; for (auto s : spill) nsp += s ? 1 : 0;
    spillCount.store (nsp, std::memory_order_relaxed);
}

} // namespace gpfx
#endif
