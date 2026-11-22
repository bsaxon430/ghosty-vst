// Ghost Mosaic - a fork of Samplebrain (Then Try This), GPL-2.0-or-later.
// mosaic::tap::Model (TapModel.h): the message thread empties the tap's queue and keeps the picture of the
// engine; a low-priority thread of the model's own does every search again to explain it (TapWhy.h).
#include "TapModel.h"
#include "Engine.h"
#include <cfloat>
#include <cmath>

namespace mosaic::tap
{
namespace
{
constexpr int kReadChunk = 4096;               // cells per read of the queue
constexpr int kMaxCellsPerTick = 1 << 17;      // (four times the queue: a tick always catches up)
constexpr int kScanBehind = 32;                // picks the explaining thread may be behind before it lets the oldest go unexplained
constexpr int kMaxVoices = 512;
constexpr int kLiveRows = 256;                 // live-input pieces kept for Model::liveTarget
constexpr uint64_t kRingMask = (uint64_t) Model::kMaxPicks - 1;
static_assert ((Model::kMaxPicks & (Model::kMaxPicks - 1)) == 0, "the pick ring is a power of two");

inline bool sameBits (float a, float b) noexcept { return std::memcmp (&a, &b, sizeof (float)) == 0; }
template <class T> inline T cellAs (const Cell* c) noexcept { T t; std::memcpy (&t, c, 32); return t; }

// what the message thread hands to the explaining thread, in the engine's order
struct Job
{
    enum Type : uint8_t { pick = 0, world, sounds, weights, setUsed, usage, lost };
    Type type = pick;
    uint64_t serial = 0;
    Match m {}; Used u {}; WhySettings s;
    uint32_t brainGen = 0, targetGen = 0;
    bool hasLive = false, complete = true, usedKnown = false, restart = false;
    uint8_t liveFlags = 0; int liveBands = 0;
    uint32_t first = 0; int count = 0; float value = 0;
    float f[kFeatures];                         // a live-input piece's rows / TERRAIN's weights / a slice of "used" counters
    std::vector<uint32_t> list;                 // the sounds
    std::shared_ptr<const BrainSnapshot> brain;
    std::shared_ptr<const TargetSnapshot> target;
};
struct Result { uint64_t serial = 0; Why why; };

const Pick& emptyPick() { static const Pick p {}; return p; }
const Voice& emptyVoice() { static const Voice v {}; return v; }
}

// ------------------------------------------------------------------------------------ state
struct Model::Impl : public juce::Thread
{
    explicit Impl (Engine& e) : juce::Thread ("Ghost Mosaic tap explainer"), engine (e), tap (e.tap())
    {
        cells.resize ((size_t) kReadChunk);
        ring.resize ((size_t) Model::kMaxPicks); pickMono.assign ((size_t) Model::kMaxPicks, 0.0);
        voices.reserve ((size_t) kMaxVoices); voiceMono.reserve ((size_t) kMaxVoices);
        liveRows.assign ((size_t) kLiveRows * (size_t) kFeatures, 0.0f); liveInfo.resize ((size_t) kLiveRows);
        jobs.reserve (512); results.reserve (512); got.reserve (512);
        for (auto& w : weights) w = 1.0f;
    }
    ~Impl() override
    {
        setActive (false);
        signalThreadShouldExit(); notify();
        stopThread (10000);
    }

    Engine& engine;
    Tap& tap;
    bool wanted = false, attached = false;

    // ---- the queue
    std::vector<Cell> cells; int carry = 0;
    bool worldSeen = false, prevWasGap = false;

    // ---- the world
    World world {};
    std::shared_ptr<const BrainSnapshot> brain;
    std::shared_ptr<const TargetSnapshot> target;
    LibraryView libView; TargetView tgtView;
    std::vector<uint8_t> soundState;
    double rate = 44100;

    // ---- picks, voices
    std::vector<Pick> ring; std::vector<double> pickMono;
    uint64_t serial = 0, serialBase = 0;
    std::vector<Voice> voices; std::vector<double> voiceMono;
    std::vector<float> liveRows;
    struct LiveInfo { uint64_t id = 0; float freq = 0; uint8_t flags = 0; int bands = 0; };
    std::vector<LiveInfo> liveInfo; uint64_t liveCount = 0;

    // ---- clocks (per channel): mono = base + est never goes back at a restart
    struct Chan { double base = 0, est = 0, clockRender = 0, newestPick = -1.0e30; bool have = false, clockFresh = false; float targetTime = 0, keyPitch = 1; } chan[2];
    bool running = false;
    uint32_t lastClock = 0, parkedBase = 0; bool haveClock = false, haveParked = false;
    uint64_t audioSamples = 0;

    // ---- settings, transport, meters
    ParamsA pa {}; ParamsB pb {}; bool havePa = false, havePb = false;
    Play tr {}; bool haveTr = false;
    Settings settings; float weights[kBands];
    Transport transport; Meters meters; Playhead playhead[2]; Counters counters; ScoreScale scale;
    int waitingCount = 0;
    std::vector<float> usedView; bool usedValidView = false; int usedCheckedView = 0;

    // ---- shared with the explaining thread (under `lock`)
    juce::CriticalSection lock;
    std::vector<Job> jobs;
    std::vector<Result> results;
    std::vector<float> usedPub; bool usedPubFresh = false, usedPubValid = false; int usedPubChecked = 0; uint32_t usedPubGen = 0;
    double explainPubMs = 0;
    std::atomic<int> pending { 0 };
    std::atomic<bool> explainAllFlag { false }, busyFlag { false };
    std::vector<Job> staged;                    // message thread: jobs made during this tick
    std::vector<Result> got;

    // ---- the explaining thread's own
    Replay replay;
    std::shared_ptr<const TargetSnapshot> exTarget;
    double explainAvgMs = 0;

    // =========================================================================== message thread
    void setActive (bool on)
    {
        if (on == wanted) return;
        wanted = on;
        if (! on)
        {
            if (attached) { tap.wantUsage (false); tap.wantLive (false); tap.detach (this); }
            attached = false; busyFlag.store (false);
            counters.attached = false; counters.note = "Switched off.";
            return;
        }
        // a fresh start: nothing from an earlier session is shown
        carry = 0; worldSeen = false; prevWasGap = false;
        serialBase = serial;
        voices.clear(); voiceMono.clear();
        for (auto& c : chan) c = Chan();
        running = false; haveClock = false; haveParked = false; audioSamples = 0;
        havePa = havePb = haveTr = false;
        brain.reset(); target.reset(); soundState.clear();
        world = World {};
        usedView.clear(); usedValidView = false; usedCheckedView = 0;
        settings = Settings(); transport = Transport(); transport.whyNot = "Waiting for the engine.";
        meters = Meters(); playhead[0] = playhead[1] = Playhead(); scale.reset();
        counters = Counters(); counters.fullRead = mosaic::tap::fullRead(); waitingCount = 0;
        refreshViews();
        attached = tap.attach (this);
        counters.attached = attached;
        counters.note = attached ? "Following the engine." : "Another view is already following the engine.";
        if (attached)
        {
            tap.wantLive (true);
            busyFlag.store (true);
            if (! isThreadRunning()) startThread (juce::Thread::Priority::low);
            notify();
        }
    }

    double mono (int ch) const noexcept { return chan[ch].base + chan[ch].est; }

    void refreshViews()
    {
        libView.s = brain.get(); libView.state = &soundState;
        libView.why = brain != nullptr ? "" : (! worldSeen ? "Waiting for the engine." : ((world.flags & worldFlag::hasLibrary) == 0 ? "No library yet." : "The library's details are not here yet."));
        const bool live = (world.flags & worldFlag::live) != 0;
        tgtView.s = target.get(); tgtView.isLive = live; tgtView.hopNow = (int) world.hop; tgtView.rateNow = rate;
        tgtView.why = live ? (mosaic::tap::fullRead() ? "Live target: there is nothing to look ahead at. Each pick shows the piece of input it matched."
                                                      : "Live target: not available yet (this build cannot read the input pieces' measurements).")
                    : (target != nullptr ? "" : (! worldSeen ? "Waiting for the engine." : ((world.flags & worldFlag::hasTarget) == 0 ? "No target yet." : "The target's details are not here yet.")));
    }

    void post (Job&& j) { staged.push_back (std::move (j)); pending.fetch_add (1); }

    // ---- one record from the queue
    void onWorld (const World& w)
    {
        const bool first = ! worldSeen;
        const bool brainChanged = first || w.brainGen != world.brainGen || ((w.flags ^ world.flags) & worldFlag::hasLibrary) != 0;
        const bool targetChanged = first || w.targetGen != world.targetGen || ((w.flags ^ world.flags) & (worldFlag::live | worldFlag::hasTarget)) != 0;
        world = w; worldSeen = true;
        rate = engine.sampleRate() > 0 ? engine.sampleRate() : 44100.0;
        if (brainChanged)
        {
            auto snap = engine.brainSnapshot();
            brain = snap != nullptr && snap->gen == w.brainGen && (w.flags & worldFlag::hasLibrary) != 0 ? snap : nullptr;
            soundState.clear();
            usedView.clear(); usedValidView = false; usedCheckedView = 0;
        }
        if (targetChanged)
        {
            auto snap = engine.targetSnapshot();
            target = snap != nullptr && snap->gen == w.targetGen && (w.flags & worldFlag::live) == 0 && (w.flags & worldFlag::hasTarget) != 0 ? snap : nullptr;
        }
        if (brainChanged || targetChanged)
        {
            Job j; j.type = Job::world; j.brain = brain; j.target = target;
            j.restart = brainChanged; j.usedKnown = ! first && ! prevWasGap;   // a library seen arriving starts with the snapshot's own "used" counters
            post (std::move (j));
            const bool live = (w.flags & worldFlag::live) != 0;
            for (auto& p : ring)
                if (p.serial > serialBase)
                    p.current = p.libraryGen == w.brainGen && (p.liveTarget ? live : (! live && p.targetGen == w.targetGen));
            refreshViews();
        }
    }

    void onSounds (const Cell* rec)
    {
        const Cell& h = rec[0];
        const int listed = (int) juce::jmin (h.w[2], (uint32_t) kMaxSoundsSent, (uint32_t) h.more * 4u);
        if (h.w[1] != world.brainGen) return;
        Job j; j.type = Job::sounds; j.complete = h.a == 0;
        j.list.resize ((size_t) listed * 2);
        if (listed > 0) std::memcpy (j.list.data(), rec + 1, (size_t) listed * 8);
        if (brain != nullptr)
        {
            // which of the snapshot's sounds are still there, and switched on (the engine's list only ever loses sounds)
            soundState.assign (brain->sounds.size(), (uint8_t) (j.complete ? 2 : 0));
            size_t cursor = 0;
            for (int k = 0; k < listed; ++k)
            {
                const uint32_t firstPiece = j.list[(size_t) k * 2] & 0x7fffffffu;
                const bool on = (j.list[(size_t) k * 2] >> 31) != 0;
                size_t at = cursor;
                while (at < brain->sounds.size() && (uint32_t) brain->sounds[at].first != firstPiece) ++at;
                if (at >= brain->sounds.size()) continue;
                soundState[at] = on ? 1 : 0;
                cursor = at + 1;
            }
        }
        post (std::move (j));
    }

    void rebuildSettings()
    {
        auto& s = settings;
        if (havePa)
        {
            s.style = pa.algo; s.styleSetByTerrain = (pa.flags & paramFlag::terrainStyle) != 0;
            s.toneTimbre = pa.ratio; s.shapePitchOnly = pa.nRatio;
            s.lowestBand = (int) juce::jmin (pa.fftStart, 1000000u); s.highestBand = (int) juce::jmin (pa.fftEnd, 1000000u);
            s.smooth = pa.stickyness; s.repeat = (int) juce::jmin (pa.stretch, 1000000u); s.connections = (int) juce::jmin (pa.synapses, 1000000u);
            s.stereo = (pa.flags & paramFlag::stereo) != 0;
            s.terrainWeights = (pa.flags & paramFlag::weightsOn) != 0; s.terrainFactor = (pa.flags & paramFlag::factorOn) != 0;
            s.terrainMovesNovelty = (pa.flags & paramFlag::timeShaped) != 0; s.terrainMask = (pa.flags & paramFlag::maskOn) != 0;
        }
        if (havePb)
        {
            s.waitUntilClose = pb.slideError; s.autotune = pb.autotune; s.flatten = pb.nMix; s.originalMix = pb.targetMix;
            s.volume = pb.volume; s.dry = pb.dry; s.terrainTilt = pb.biasTilt;
            s.pieceGain = (1.0f - pb.targetMix) * 0.2f * pb.volume; s.originalGain = pb.targetMix * 0.2f * pb.volume;
        }
    }

    void removeVoice (size_t i)
    {
        if (const Pick* p = find (voices[i].pickSerial)) const_cast<Pick*> (p)->sounding = false;
        voices.erase (voices.begin() + (std::ptrdiff_t) i); voiceMono.erase (voiceMono.begin() + (std::ptrdiff_t) i);
    }

    const Pick* find (uint64_t s) const noexcept
    {
        if (s == 0 || s <= serialBase || s > serial) return nullptr;
        const Pick& p = ring[(size_t) (s & kRingMask)];
        return p.serial == s ? &p : nullptr;
    }

    void onPick (const Cell* rec, int cellsInRecord)
    {
        const Match m = cellAs<Match> (rec);
        const Used u = cellAs<Used> (rec + 1);
        if (rec[1].kind != kUsed || m.ch > 1) return;
        const int ch = m.ch;
        Pick& p = ring[(size_t) (++serial & kRingMask)];
        if (p.serial > serialBase && p.why.state == Why::waiting) --waitingCount;   // (it leaves the ring unexplained)
        ++waitingCount;
        p = Pick();
        p.serial = serial; p.channel = ch; p.targetPiece = (int) m.tgt; p.libraryPiece = (int) m.src;
        p.previousPiece = (m.flags & matchFlag::prevUnknown) != 0 ? -1 : (int) m.prevSrc;
        p.style = m.algo; p.startSample = m.time;
        p.targetMovedOn = (m.flags & matchFlag::movesOn) != 0;
        p.liveTarget = (m.flags & matchFlag::liveTarget) != 0;
        p.libraryGen = world.brainGen; p.targetGen = world.targetGen; p.current = true;
        p.settings = settings;
        p.settings.style = m.algo; p.settings.novelty = m.novelty; p.settings.boredom = m.boredom;
        p.settings.connections = (int) juce::jmin (u.synapses, 1000000u);
        if (! p.targetMovedOn)
        {
            p.heldBySlide = m.algo == 3 && ! (u.error < (double) pb.slideError);
            p.heldByRepeat = ! p.heldBySlide;
        }
        const bool nextExists = (m.flags & matchFlag::nextExists) != 0 && (m.flags & matchFlag::prevUnknown) == 0;
        const bool maybeFollowed = m.algo != 1 && pa.stickyness > 0.0f && nextExists && m.src == m.prevSrc + 1;
        if (! maybeFollowed) p.playedFollowing = Pick::no;
        else if (m.algo == 0)
        {
            // Closest: the search marks its winner. If the piece played was not marked, it was not the winner: SMOOTH played it
            const float faded = u.nextBefore * m.boredom;
            const float stamped = (float) ((double) faded + 1000.0);
            p.playedFollowing = sameBits (u.srcAfter, stamped) ? Pick::no : Pick::yes;
        }
        else p.playedFollowing = Pick::notYetKnown;
        pickMono[(size_t) (serial & kRingMask)] = chan[ch].base + (double) m.time;
        chan[ch].newestPick = pickMono[(size_t) (serial & kRingMask)];
        p.sounding = true;
        ++counters.picksSeen;

        Job j; j.type = Job::pick; j.serial = serial; j.m = m; j.u = u;
        j.brainGen = world.brainGen; j.targetGen = world.targetGen;
        auto& ws = j.s;
        ws.style = m.algo; ws.ratio = pa.ratio; ws.nRatio = pa.nRatio; ws.fftStart = pa.fftStart; ws.fftEnd = pa.fftEnd;
        ws.novelty = m.novelty; ws.boredom = m.boredom; ws.stickyness = pa.stickyness; ws.synapses = u.synapses;
        ws.slideLimit = (double) pb.slideError;
        ws.weightsOn = (pa.flags & paramFlag::weightsOn) != 0; ws.factorOn = (pa.flags & paramFlag::factorOn) != 0; ws.tilt = pb.biasTilt;

        if ((m.flags & matchFlag::liveBlock) != 0 && cellsInRecord >= 3 + kLiveBlockCells && rec[2].kind == kLiveBlock)
        {
            // the live-input piece it was matched to: tidy the numbers (not-a-number becomes 0, and is flagged)
            const size_t slot = (size_t) (liveCount % (uint64_t) kLiveRows);
            float* row = liveRows.data() + slot * (size_t) kFeatures;
            std::memcpy (row, rec + 3, sizeof (float) * (size_t) kFeatures);
            uint8_t flags = 0;
            static constexpr int from[5] = { 0, kBands, kBands + kTimbre, 2 * kBands + kTimbre, kFeatures };
            static constexpr uint8_t bit[4] = { rowFlag::badBands, rowFlag::badTimbre, rowFlag::badFlatBands, rowFlag::badFlatTimbre };
            for (int k = 0; k < 4; ++k)
                for (int i = from[k]; i < from[k + 1]; ++i)
                    if (! std::isfinite (row[i])) { row[i] = 0.0f; flags = (uint8_t) (flags | bit[k]); }
            float fr; std::memcpy (&fr, &rec[2].w[1], 4);
            liveInfo[slot] = { ++liveCount, std::isfinite (fr) ? fr : 0.0f, flags, (int) juce::jmin (rec[2].w[2], (uint32_t) kBands) };
            p.liveRow = (int) (liveCount & 0x7fffffff);
            j.hasLive = true; j.liveFlags = flags; j.liveBands = liveInfo[slot].bands;
            std::memcpy (j.f, row, sizeof (float) * (size_t) kFeatures);
        }
        post (std::move (j));

        if (voices.size() >= (size_t) kMaxVoices) removeVoice (0);
        Voice v;
        v.pickSerial = serial; v.channel = ch; v.libraryPiece = (int) m.src; v.targetPiece = (int) m.tgt; v.startSample = m.time;
        v.length = brain != nullptr ? brain->pcmLen : (int) world.srcLen;
        voices.push_back (v); voiceMono.push_back (pickMono[(size_t) (serial & kRingMask)]);
    }

    void handle (const Cell* rec, int n)
    {
        const uint8_t kind = rec[0].kind;
        if (kind == kGap)
        {
            counters.cellsLost += cellAs<Gap> (rec).lostCells;
            prevWasGap = true;
            Job j; j.type = Job::lost; post (std::move (j));
            return;
        }
        if (! worldSeen && kind != kWorld) return;               // (left over from before we attached)
        switch (kind)
        {
            case kWorld: onWorld (cellAs<World> (rec)); break;
            case kSounds: onSounds (rec); break;
            case kParamsA: pa = cellAs<ParamsA> (rec); havePa = true; rebuildSettings(); break;
            case kParamsB: pb = cellAs<ParamsB> (rec); havePb = true; rebuildSettings(); break;
            case kWeights:
            {
                std::memcpy (weights, rec + 1, sizeof (weights));
                for (auto& w : weights) if (! std::isfinite (w)) w = 0.0f;
                Job j; j.type = Job::weights; std::memcpy (j.f, rec + 1, sizeof (weights)); post (std::move (j));
                break;
            }
            case kTransport: tr = cellAs<Play> (rec); haveTr = true; break;
            case kReset:
            {
                const Reset r = cellAs<Reset> (rec);
                if (r.ch > 1) break;
                auto& c = chan[r.ch];
                c.base = juce::jmax (c.base + c.est, c.newestPick + 1.0);         // (time on the screen never runs backwards)
                c.est = 0; c.clockRender = 0; c.clockFresh = false;
                for (size_t i = voices.size(); i-- > 0;) if (voices[i].channel == r.ch) removeVoice (i);   // (a restart cuts every piece of the channel)
                playhead[r.ch].restarts += juce::jmax (1u, r.count); playhead[r.ch].lastRestart = r.where == 0 ? 1 : 2;
                break;
            }
            case kMatch: onPick (rec, n); break;
            case kEnd:
            {
                const End e = cellAs<End> (rec);
                for (size_t i = 0; i < voices.size(); ++i)
                    if (voices[i].channel == e.ch && voices[i].startSample == e.time && voices[i].libraryPiece == (int) e.src) { removeVoice (i); break; }
                break;
            }
            case kMeter:
            {
                const Meter m = cellAs<Meter> (rec);
                meters.outputPeak = std::isfinite (m.peak) ? m.peak : 0.0f;
                meters.outputPeakHold = juce::jmax (meters.outputPeakHold, meters.outputPeak);
                meters.limiterGain = m.limGain; meters.limiterOn = (m.flags & meterFlag::limiterOn) != 0;
                meters.fade = m.fade; meters.velocityGain = m.velGain;
                running = (m.flags & meterFlag::running) != 0;
                if (haveClock) { const uint32_t d = m.clock - lastClock; audioSamples += d; meters.frameSeconds = (double) d / rate; }
                lastClock = m.clock; haveClock = true;
                if (! haveParked) { parkedBase = m.parked; haveParked = true; }
                counters.parkedBuffers = m.parked - parkedBase;
                counters.secondsSinceEngine = 0;
                break;
            }
            case kShape:
            {
                const Shape s = cellAs<Shape> (rec);
                meters.shapeValid = s.minL <= s.maxL && s.minR <= s.maxR && std::isfinite (s.minL) && std::isfinite (s.maxL) && std::isfinite (s.minR) && std::isfinite (s.maxR);
                if (meters.shapeValid) { meters.outMin[0] = s.minL; meters.outMax[0] = s.maxL; meters.outMin[1] = s.minR; meters.outMax[1] = s.maxR; }
                break;
            }
            case kClock:
            {
                const Clock k = cellAs<Clock> (rec);
                if (k.ch > 1) break;
                auto& c = chan[k.ch];
                c.clockRender = (double) k.renderTime; c.clockFresh = true; c.targetTime = k.targetTime; c.keyPitch = k.keyPitch;
                meters.engineVoices[k.ch] = (int) k.active;
                if (k.ch == 0) meters.droppedPieces = k.droppedBlocks; else meters.droppedPieces += k.droppedBlocks;
                playhead[k.ch].valid = true;
                break;
            }
            case kRest:
            {
                const Rest r = cellAs<Rest> (rec);
                Job j; j.type = Job::setUsed; j.first = r.piece; j.value = r.usageAfter; post (std::move (j));
                if ((r.flags & restFlag::someLost) != 0) { Job l; l.type = Job::lost; post (std::move (l)); }
                break;
            }
            case kUsage:
            {
                if (rec[0].w[2] != world.brainGen) break;
                Job j; j.type = Job::usage; j.first = rec[0].w[0]; j.count = (int) juce::jmin (rec[0].w[1], (uint32_t) kUsageFloats);
                std::memcpy (j.f, rec + 1, sizeof (float) * (size_t) kUsageFloats);
                post (std::move (j));
                break;
            }
            default: break;
        }
        prevWasGap = false;
    }

    static int recordCells (const Cell* c, int have) noexcept
    {
        if (c->kind == kMatch) return (cellAs<Match> (c).flags & matchFlag::liveBlock) != 0 ? 3 + kLiveBlockCells : 2;
        if (isBulk (c->kind)) return 1 + (int) c->more;
        juce::ignoreUnused (have);
        return 1;
    }

    void drain()
    {
        int total = 0;
        for (;;)
        {
            const int gotCells = tap.read (cells.data() + carry, (int) cells.size() - carry);
            const int n = carry + gotCells;
            int at = 0;
            while (at < n)
            {
                const int need = recordCells (cells.data() + at, n - at);
                if (need > (int) cells.size()) { ++at; continue; }               // (cannot be: no record is that long)
                if (at + need > n) break;                                        // the rest of this record comes with the next read
                handle (cells.data() + at, need);
                at += need;
            }
            carry = n - at;
            if (carry > 0 && at > 0) std::memmove (cells.data(), cells.data() + at, (size_t) carry * 32);
            total += gotCells; counters.cellsRead += (uint64_t) gotCells;
            if (gotCells == 0 || total >= kMaxCellsPerTick) break;
        }
    }

    float pitchRatioOf (const Voice& v, bool& known) const noexcept
    {
        known = true;
        float ratio = 1.0f;
        const float at = pb.autotune;
        if (at > 0.0f)
        {
            float tf = 0.0f, sf = 0.0f;
            const Pick* p = find (v.pickSerial);
            if (brain != nullptr && v.libraryPiece < brain->numBlocks) sf = brain->rows.freq[(size_t) v.libraryPiece];
            if (p != nullptr && p->liveTarget)
            {
                const uint64_t id = (uint64_t) (uint32_t) p->liveRow;
                const LiveInfo& li = liveInfo[(size_t) ((id + (uint64_t) kLiveRows - 1) % (uint64_t) kLiveRows)];
                if (p->liveRow > 0 && (li.id & 0x7fffffff) == id) tf = li.freq;
            }
            else if (p != nullptr && p->current && target != nullptr && v.targetPiece < target->numBlocks) tf = target->rows[v.channel].freq[(size_t) v.targetPiece];
            if (tf > 0.0f && sf > 0.0f)
            {
                ratio = tf / sf;                                                 // RtRenderer::render
                const float max = 1 + (at * at) * 100.0f;
                if (ratio > max) ratio = max;
                if (ratio < 1 / max) ratio = 1 / max;
            }
            else known = false;
        }
        const float key = chan[v.channel].keyPitch;
        if (key > 0.0f && ! sameBits (key, 1.0f)) ratio *= key;
        return ratio;
    }

    void tick (double dt)
    {
        const double t0 = juce::Time::getMillisecondCounterHiRes();
        if (! std::isfinite (dt) || dt < 0) dt = 0;
        if (wanted && ! attached)
        {
            // (another reader had the tap: it may have let go since)
            wanted = false; setActive (true);
        }
        if (! attached) { counters.modelSeconds += dt; return; }
        drain();

        // ---- hand the new work to the explaining thread and take what it finished. Never waits: if that thread
        //      has the lock this instant, both wait for the next tick. No wake-up call either (it looks by itself).
        got.clear();
        {
            const juce::ScopedTryLock l (lock);
            if (l.isLocked())
            {
                for (auto& j : staged) jobs.push_back (std::move (j));
                staged.clear();
                got.swap (results);
                if (usedPubFresh)
                {
                    if (usedPubGen == world.brainGen) { usedView.swap (usedPub); usedValidView = usedPubValid; usedCheckedView = usedPubChecked; }
                    usedPubFresh = false;
                    counters.explainMs = explainPubMs;
                }
            }
        }
        for (auto& r : got)
        {
            if (r.why.state == Why::ready) { ++counters.picksExplained; scale.feed (r.why, dt); }
            else if (r.why.state == Why::skipped) ++counters.picksSkipped;
            else ++counters.picksUnavailable;
            if (const Pick* cp = find (r.serial))
            {
                Pick& p = *const_cast<Pick*> (cp);
                if (p.why.state == Why::waiting && r.why.state != Why::waiting) --waitingCount;
                p.why = r.why;
                if (r.why.state == Why::ready && r.why.agrees) p.playedFollowing = r.why.stickyPlayed ? Pick::yes : Pick::no;
                else if (p.playedFollowing == Pick::notYetKnown && r.why.state != Why::waiting) p.playedFollowing = Pick::no;
            }
        }

        // ---- clocks
        const Engine::ShapeNow now = engine.shapeNow();
        settings.novelty = now.novelty; settings.boredom = now.boredom;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto& c = chan[ch];
            if (running) c.est += dt * rate;
            if (c.clockFresh)
            {
                const double err = c.clockRender - c.est;
                if (! c.have || std::abs (err) > 0.25 * rate) c.est = c.clockRender; else c.est += 0.25 * err;
                c.have = true; c.clockFresh = false;
            }
            auto& ph = playhead[ch];
            const double hop = world.hop > 0 ? (double) world.hop : 1.0;
            ph.targetPiece = (double) c.targetTime / hop; ph.targetSeconds = (double) c.targetTime / rate;
            ph.renderSample = (uint32_t) juce::jmax (0.0, c.est); ph.renderSeconds = c.est / rate;
        }
        const size_t shown = (size_t) juce::jmin ((uint64_t) Model::kMaxPicks, serial - serialBase);
        for (size_t i = 0; i < shown; ++i)
        {
            const size_t at = (size_t) ((serial - i) & kRingMask);
            Pick& p = ring[at];
            p.ageSeconds = (mono (p.channel) - pickMono[at]) / rate;
        }
        waitingCount = juce::jlimit (0, (int) shown, waitingCount);
        counters.picksWaiting = waitingCount; counters.explainingDone = (int) shown - waitingCount; counters.explainingOf = (int) shown;

        // ---- voices
        const Floats fade = libView.fade();
        for (size_t i = voices.size(); i-- > 0;)
        {
            Voice& v = voices[i];
            v.ageSeconds = (mono (v.channel) - voiceMono[i]) / rate;
            v.pitchRatio = pitchRatioOf (v, v.pitchKnown);
            v.pieceGain = settings.pieceGain; v.originalGain = settings.originalGain;
            const double e = v.ageSeconds * rate, len = (double) juce::jmax (1, v.length), r = (double) juce::jmax (1.0e-3f, v.pitchRatio);
            double pos = 0, life = len;
            if (e > 0)
            {
                if (r <= 1.0) { pos = juce::jmin (len, e * r); life = len / r; }
                else if (e < len) { pos = std::fmod (e * r, len); }
                else { pos = juce::jmin (len, std::fmod (len * r, len) + (e - len) * r); life = len + (len - std::fmod (len * r, len)) / r; }
            }
            v.position = (float) pos; v.progress = (float) juce::jlimit (0.0, 1.0, e / life);
            v.fade = fade.size() == (size_t) v.length && v.length > 0 ? fade[(size_t) juce::jlimit (0, v.length - 1, (int) pos)] : 1.0f;
            if (e > 2.0 * life + rate) removeVoice (i);                          // (its ending was lost: let it go)
        }

        // ---- transport, meters, counters
        if (haveTr)
        {
            auto& t = transport;
            const bool hasLib = (world.flags & worldFlag::hasLibrary) != 0, hasTgt = (world.flags & worldFlag::hasTarget) != 0;
            const bool run = (tr.flags & playFlag::running) != 0;
            t.playMode = tr.playMode; t.notesDown = tr.notesDown; t.lastNote = tr.lastNote; t.velocity = tr.velocity;
            t.hostPlaying = (tr.flags & playFlag::hostPlaying) != 0; t.hostReports = (tr.flags & playFlag::hostReports) != 0;
            t.bpm = tr.bpm; t.ppq = tr.ppq;
            t.recording = (tr.flags & playFlag::recording) != 0; t.fadingOut = (tr.flags & playFlag::stopAfterFade) != 0;
            t.waitingForLiveInput = (tr.flags & playFlag::livePending) != 0;
            t.wanted = t.playMode == 1 ? t.notesDown > 0 : (t.playMode == 2 && t.hostReports ? t.hostPlaying : (tr.flags & playFlag::wish) != 0);
            t.playing = run && hasLib && hasTgt && ! t.waitingForLiveInput;
            t.whyNot = t.playing ? ""
                     : counters.secondsSinceEngine > 0.5 ? "The audio is not running right now."
                     : ! hasLib ? "No library yet: add sounds and press GENERATE."
                     : ! hasTgt ? "No target yet."
                     : t.playMode == 1 && t.notesDown == 0 ? "Waiting for a MIDI key (PLAY BY: MIDI keys)."
                     : t.playMode == 2 && t.hostReports && ! t.hostPlaying ? "Your DAW is stopped (PLAY BY: DAW play)."
                     : ! t.wanted ? "Paused."
                     : t.waitingForLiveInput ? "Waiting for the live input to fill its first piece."
                     : "Starting.";
        }
        else transport.whyNot = "Waiting for the engine.";
        meters.outputPeakHold *= (float) std::exp (-dt / 0.6);
        counters.usedValid = usedValidView; counters.usedChecked = usedCheckedView;
        counters.audioSeconds = (double) audioSamples / rate;
        counters.modelSeconds += dt; counters.secondsSinceEngine += dt;
        tap.wantUsage (brain != nullptr && ! usedValidView);

        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        counters.lastTickMs = ms; counters.worstTickMs = juce::jmax (counters.worstTickMs, ms);
    }

    // =========================================================================== the explaining thread
    void publishUsed()
    {
        const juce::ScopedLock l (lock);
        usedPub = replay.used();
        usedPubValid = replay.usedValid(); usedPubChecked = replay.usedChecked();
        usedPubGen = replay.library() != nullptr ? replay.library()->gen : 0;
        explainPubMs = explainAvgMs;
        usedPubFresh = true;
    }

    void work (Job& j, int picksLeft)
    {
        switch (j.type)
        {
            case Job::world:
                if (j.restart || replay.library() != j.brain.get()) replay.setLibrary (j.brain, j.usedKnown);
                exTarget = j.target;
                break;
            case Job::sounds: replay.setSounds (j.list.data(), (int) j.list.size() / 2, j.complete); break;
            case Job::weights: replay.setWeights (j.f); break;
            case Job::setUsed: replay.setUsed (j.first, j.value); break;
            case Job::usage: replay.usageSlice (j.first, j.f, j.count); break;
            case Job::lost: replay.usedLost(); break;
            case Job::pick:
            {
                Result r; r.serial = j.serial;
                const BrainSnapshot* lib = replay.library();
                if (lib == nullptr || lib->gen != j.brainGen) { r.why.state = Why::unavailable; r.why.note = "This pick's library is no longer here."; }
                else
                {
                    WhyTarget t;
                    const int ch = j.m.ch;
                    if (j.hasLive)
                    {
                        t.bands = j.f; t.timbre = j.f + kBands; t.flatBands = j.f + kBands + kTimbre; t.flatTimbre = j.f + 2 * kBands + kTimbre;
                        t.bandCount = j.liveBands; t.flags = j.liveFlags;
                    }
                    else if (exTarget != nullptr && exTarget->gen == j.targetGen && (j.m.flags & matchFlag::liveTarget) == 0
                             && j.m.tgt < (uint32_t) exTarget->numBlocks && exTarget->hasMeasurements)
                    {
                        const int i = (int) j.m.tgt;
                        t.bands = exTarget->bandsOf (ch, i); t.timbre = exTarget->timbreOf (ch, i);
                        if (exTarget->hasFlattened) { t.flatBands = exTarget->flatBandsOf (ch, i); t.flatTimbre = exTarget->flatTimbreOf (ch, i); }
                        t.bandCount = exTarget->bandCount; t.flags = exTarget->rows[ch].flags[(size_t) i];
                    }
                    const double t0 = juce::Time::getMillisecondCounterHiRes();
                    replay.pick (j.s, t, j.m, j.u, explainAllFlag.load (std::memory_order_relaxed) || picksLeft <= kScanBehind, r.why);
                    if (r.why.state == Why::ready)
                    {
                        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
                        explainAvgMs = explainAvgMs > 0 ? explainAvgMs + 0.05 * (ms - explainAvgMs) : ms;
                    }
                }
                const juce::ScopedLock l (lock);
                results.push_back (r);
                break;
            }
        }
    }

    void run() override
    {
        std::vector<Job> local;
        local.reserve (512);
        while (! threadShouldExit())
        {
            { const juce::ScopedLock l (lock); local.swap (jobs); }
            if (local.empty()) { wait (busyFlag.load (std::memory_order_relaxed) ? 3 : 250); continue; }   // (it looks for work itself: the message thread never pays for a wake-up)
            int picksLeft = 0;
            for (auto& j : local) if (j.type == Job::pick) ++picksLeft;
            double lastPub = juce::Time::getMillisecondCounterHiRes();
            for (auto& j : local)
            {
                if (threadShouldExit()) break;
                work (j, picksLeft);
                if (j.type == Job::pick) --picksLeft;
                if (const double t = juce::Time::getMillisecondCounterHiRes(); t - lastPub > 33.0) { publishUsed(); lastPub = t; }
                pending.fetch_sub (1);
            }
            local.clear();
            publishUsed();
        }
    }
};

// ------------------------------------------------------------------------------------ the public face
Model::Model (Engine& e) : impl (std::make_unique<Impl> (e)) {}
Model::~Model() = default;

void Model::setActive (bool on) { impl->setActive (on); }
void Model::tick (double dt) { impl->tick (dt); }
bool Model::active() const noexcept { return impl->attached; }

const LibraryView& Model::library() const noexcept { return impl->libView; }
const TargetView& Model::target() const noexcept { return impl->tgtView; }

int Model::numPicks() const noexcept { return (int) juce::jmin ((uint64_t) kMaxPicks, impl->serial - impl->serialBase); }
const Pick& Model::pick (int index) const noexcept
{
    if (index < 0 || index >= numPicks()) return emptyPick();
    return impl->ring[(size_t) ((impl->serial - (uint64_t) index) & kRingMask)];
}
const Pick* Model::findPick (uint64_t serial) const noexcept { return impl->find (serial); }
const Pick* Model::newestPick (int channel) const noexcept
{
    const int n = numPicks();
    for (int i = 0; i < n; ++i) if (const Pick& p = pick (i); p.channel == channel) return &p;
    return nullptr;
}

TargetPiece Model::targetPieceOf (const Pick& p) const noexcept
{
    TargetPiece t;
    if (p.serial == 0) { t.note = "No pick."; return t; }
    t.live = p.liveTarget;
    if (! p.liveTarget)
    {
        const TargetView& tv = impl->tgtView;
        if (! p.current) { t.note = "This pick's target has been replaced since."; return t; }
        if (! tv.valid (p.channel, p.targetPiece)) { t.note = tv.ok() ? "Not a piece of the target." : tv.note(); return t; }
        if (! tv.hasMeasurements()) { t.note = "The target is too large for its measurements to be kept."; return t; }
        t.valid = true; t.note = "";
        t.bands = tv.bands (p.channel, p.targetPiece); t.timbre = tv.timbre (p.channel, p.targetPiece);
        t.flatBands = tv.flatBands (p.channel, p.targetPiece); t.flatTimbre = tv.flatTimbre (p.channel, p.targetPiece);
        t.pitchAsEngineHolds = tv.pitchAsEngineHolds (p.channel, p.targetPiece); t.finite = tv.measurementsFinite (p.channel, p.targetPiece);
        return t;
    }
    if (p.liveRow <= 0) { t.note = "Live target: not available yet for this pick."; return t; }
    const uint64_t id = (uint64_t) (uint32_t) p.liveRow;
    const size_t slot = (size_t) ((id + (uint64_t) kLiveRows - 1) % (uint64_t) kLiveRows);
    const auto& li = impl->liveInfo[slot];
    if ((li.id & 0x7fffffff) != id) { t.note = "Live target: this piece is too old, its measurements were let go."; return t; }
    const float* row = impl->liveRows.data() + slot * (size_t) kFeatures;
    t.valid = true; t.note = "";
    t.bands = Floats (row, (size_t) li.bands); t.timbre = Floats (row + kBands, (size_t) kTimbre);
    t.flatBands = Floats (row + kBands + kTimbre, (size_t) li.bands); t.flatTimbre = Floats (row + 2 * kBands + kTimbre, (size_t) kTimbre);
    t.pitchAsEngineHolds = li.freq; t.finite = li.flags == 0;
    return t;
}

int Model::numVoices() const noexcept { return (int) impl->voices.size(); }
const Voice& Model::voice (int index) const noexcept { return index >= 0 && index < numVoices() ? impl->voices[(size_t) index] : emptyVoice(); }

float Model::used (int piece) const noexcept
{
    if (piece < 0 || (size_t) piece >= impl->usedView.size()) return 0.0f;
    const float v = impl->usedView[(size_t) piece];
    return std::isfinite (v) ? v : 0.0f;
}
float Model::usedUnit (int piece) const noexcept { return juce::jlimit (0.0f, 1.0f, used (piece) / 1000.0f); }
bool Model::usedValid() const noexcept { return impl->usedValidView; }

const Settings& Model::settings() const noexcept { return impl->settings; }
Floats Model::terrainWeights() const noexcept { return impl->settings.terrainWeights ? Floats (impl->weights, (size_t) kBands) : Floats(); }
const Transport& Model::transport() const noexcept { return impl->transport; }
const Meters& Model::meters() const noexcept { return impl->meters; }
const Playhead& Model::playhead (int channel) const noexcept { return impl->playhead[channel == 1 ? 1 : 0]; }
const Counters& Model::counters() const noexcept { return impl->counters; }
const ScoreScale& Model::scoreScale() const noexcept { return impl->scale; }

void Model::explainEveryPick (bool on) { impl->explainAllFlag.store (on); }
bool Model::waitUntilExplained (int timeoutMs)
{
    {
        const juce::ScopedLock l (impl->lock);
        for (auto& j : impl->staged) impl->jobs.push_back (std::move (j));
        impl->staged.clear();
    }
    impl->notify();
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) juce::jmax (0, timeoutMs);
    while (impl->pending.load() > 0 && juce::Time::getMillisecondCounter() < end) juce::Thread::sleep (1);
    return impl->pending.load() <= 0;
}
}
