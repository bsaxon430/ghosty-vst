// HOST TORTURE (included by BoxTests.cpp): what a DAW does to a plugin, measured. Every check is a number.
//   T1  every scene and unit at 22.05 / 44.1 / 48 / 96 / 192 kHz with random block sizes 1..8192 (finite, bounded, level-consistent)
//   T2  automation of every host parameter at block rate (64) and sample-ish rate (8): click scan
//   T3  preset / scene browsing storm while audio runs (clicks, stuck state, undo depth)
//   T4  mid-play state save + restore == continuing
//   T5  offline bounce == realtime for every unit and scene
//   T6  bypass null for every unit
//   T7  latency report constant when units toggle, lanes move, loops jump; scales with the sample rate
//   T8  audio-thread allocations (global operator new instrumented) and blocking locks (pthread_mutex_lock interposed)
//   T10 CPU: % of one core per scene at 44.1 / 48 kHz (10 heaviest)
// (T9, UI hammering while audio runs, is the "stress" mode: needs a display, run under Xvfb in Release and ASan.)

// ---- audio-thread allocation instrumentation (counts only while a processBlock call is running; flag + counters live in BoxTests.cpp)
// Not under ASan: it owns operator new / delete (replacing only some of the variants there mixes allocators).
#if ! defined (__SANITIZE_ADDRESS__)
static constexpr bool kAllocProbe = true;
void* operator new (std::size_t n)
{
    if (gInAudio) gAudioAllocs.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete (void* p) noexcept { if (gInAudio) gAudioFrees.fetch_add (1, std::memory_order_relaxed); std::free (p); }
void operator delete (void* p, std::size_t) noexcept { if (gInAudio) gAudioFrees.fetch_add (1, std::memory_order_relaxed); std::free (p); }
void* operator new[] (std::size_t n) { return operator new (n); }
void operator delete[] (void* p) noexcept { operator delete (p); }
void operator delete[] (void* p, std::size_t) noexcept { operator delete (p); }
#else
static constexpr bool kAllocProbe = false;
#endif

// ---- audio-thread lock instrumentation: pthread_mutex_lock (std::mutex, juce::CriticalSection, juce::WaitableEvent...) interposed in this
// executable; blocking locks taken inside processBlock are counted (try-locks are allowed: they never wait). Not under ASan (it owns the symbol).
#if ! defined (__SANITIZE_ADDRESS__)
#include <dlfcn.h>
#include <pthread.h>
static std::atomic<long> gAudioLocks { 0 };
extern "C" int pthread_mutex_lock (pthread_mutex_t* m)
{
    using Fn = int (*) (pthread_mutex_t*);
    static Fn real = (Fn) dlsym (RTLD_NEXT, "pthread_mutex_lock");
    if (gInAudio) gAudioLocks.fetch_add (1, std::memory_order_relaxed);
    return real (m);
}
static constexpr bool kLockProbe = true;
#else
static std::atomic<long> gAudioLocks { 0 };
static constexpr bool kLockProbe = false;
#endif

// run with a caller-chosen block size per call (random sizes), counting audio-thread allocations
static Stereo runBlocks (Proc& pr, const Stereo& in, std::function<int()> nextBlock)
{
    auto& p = *pr.p;
    Stereo out; out.resize (in.size());
    const int chans = pr.sidechain ? 4 : 2;
    juce::AudioBuffer<float> buf (chans, 8192); juce::MidiBuffer midi;
    for (size_t pos = 0; pos < in.size();)
    {
        const int n = (int) std::min<size_t> ((size_t) std::max (1, nextBlock()), in.size() - pos);
        buf.setSize (chans, n, false, false, true); buf.clear();
        std::copy (in.l.begin() + (long) pos, in.l.begin() + (long) pos + n, buf.getWritePointer (0));
        std::copy (in.r.begin() + (long) pos, in.r.begin() + (long) pos + n, buf.getWritePointer (1));
        { AudioScope as; p.processBlock (buf, midi); }
        pr.head.ppq += (double) n / (60.0 / pr.head.bpm * SR);
        std::copy (buf.getReadPointer (0), buf.getReadPointer (0) + n, out.l.begin() + (long) pos);
        std::copy (buf.getReadPointer (1), buf.getReadPointer (1) + n, out.r.begin() + (long) pos);
        pos += (size_t) n;
    }
    return out;
}

static int unitOfParam (const juce::String& id)
{
    for (int u = 0; u < kNumUnits; ++u) if (id.startsWith (juce::String ("fx_") + unitInfo (u).key + "_")) return u;
    return -1;
}

static void testTorture()
{
    std::printf ("\n[T] HOST TORTURE\n");
    const double savedSR = SR;
    uint32_t rng = 0xC0FFEEu; auto rnd01 = [&rng] { rng = rng * 1664525u + 1013904223u; return (float) (rng >> 8) / 16777216.0f; };

    // ------------------------------------------------------------ T1 rates x random block sizes
    if (want ("T1"))
    {
        std::printf ("  T1 every scene + unit at 22.05/44.1/48/96/192 kHz, random blocks 1..8192 (limiter on): finite, peak <= 1, level within 2 dB across rates\n");
        const double rates[] { 22050.0, 44100.0, 48000.0, 96000.0, 192000.0 };
        std::map<juce::String, std::map<int, float>> level;   // config -> rate -> rms dB
        int renders = 0, bad = 0; float worstPeak = 0;
        for (double sr : rates)
        {
            SR = sr;
            const auto sig = mixOf (pad (1.5), drums (1.5), 0.6f);
            auto one = [&] (const juce::String& name, std::function<void (Proc&)> setup)
            {
                Proc p (true, false, 8192); setup (p); p.pump();
                const auto out = runBlocks (p, sig, [&] { return 1 + (int) (rnd01() * rnd01() * 8191.0f); });
                const bool ok = finite (out) && peak (out) <= 1.0f;
                if (! ok) { ++bad; check (false, name + " @ " + juce::String ((int) sr) + " Hz"); }
                worstPeak = std::max (worstPeak, peak (out));
                level[name][(int) sr] = rmsDb (out, S (0.3), S (1.5)); ++renders;
            };
            for (auto& sc : factoryScenes()) one (juce::String ("scene ") + sc.key, [&sc] (Proc& p) { p.scene (sc.key); });
            for (int u = 0; u < kNumUnits; ++u)
                if (u != uLim) one (juce::String ("unit ") + unitInfo (u).key, [u] (Proc& p) { p.set (pid (u, "on"), 1); if (! unitPresets (u).empty()) p.impl().applyUnitPreset (u, 0); });
        }
        SR = savedSR;
        float worstDev = 0; juce::String worstName;
        for (auto& [name, m] : level)
        {
            const float r44 = m[44100], r48 = m[48000], r96 = m[96000];
            const float dev = std::max (std::abs (r44 - r48), std::abs (r96 - r48));
            if (dev > worstDev) { worstDev = dev; worstName = name; }
            if (dev > 2.0f) check (false, "level differs across rates: " + name + " 44.1 " + juce::String (r44, 1) + " 48 " + juce::String (r48, 1) + " 96 " + juce::String (r96, 1));
        }
        std::printf ("     %d renders, %d bad, worst peak %.3f, worst 44.1/96 vs 48 kHz level deviation %.2f dB (%s)\n", renders, bad, worstPeak, worstDev, worstName.toRawUTF8());
    }

    // ------------------------------------------------------------ T2 automation of every host parameter
    if (want ("T2"))
    {
        std::printf ("  T2 automation sweep of every host parameter (min -> max -> min over 1.5 s) at block rate 64 and sample-ish rate 8: click scan on a pad\n");
        Proc probe (false);
        std::vector<juce::RangedAudioParameter*> params;
        for (auto* p : probe->getParameters()) if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p)) params.push_back (rp);
        // the pad is fed continuously: the 0.3 s warm-up is its own beginning, the sweep its continuation (restarting the pad would
        // step the input, and delay-type units carry that step into the measured window)
        const auto pdAll = pad (2.3); const auto pre = slice (pdAll, 0, S (0.3)), pd = slice (pdAll, S (0.3), pdAll.size());
        // a unit's own sound can be "clicky" (granular splices, vinyl crackle, gates): every sweep is judged against the same unit held
        // still, so only clicks CAUSED by the automation count
        auto setupFor = [] (Proc& p, int u) { p.scene ("dry"); if (u >= 0) { p.set (pid (u, "on"), 1); if (unitInfo (u).hasMix) p.set (pid (u, "mix"), 1.0f); } else { p.scene ("radiate"); p.set (pid (uFilter, "on"), 1); p.set (pid (uEcho, "on"), 1); } p.pump(); };
        std::map<int, float> staticScore;
        for (int u = -1; u < kNumUnits; ++u) { Proc p (true); setupFor (p, u); run (p, pre); staticScore[u] = clickScore (run (p, pd)); }
        int tested = 0, discreteClicks = 0; float worstCont = 0; juce::String worstContName; std::vector<std::pair<float, juce::String>> offenders;
        for (auto* rp : params)
        {
            const auto id = rp->getParameterID();
            const bool discrete = dynamic_cast<juce::AudioParameterFloat*> (rp) == nullptr;
            const int u = unitOfParam (id);
            for (int blk : { 64, 8 })
            {
                Proc p (true); setupFor (p, u); run (p, pre);
                auto* lp = p->apvts.getParameter (id); if (lp == nullptr) continue;
                size_t pos = 0;
                const auto out = runBlocks (p, pd, [&] { const double t = (double) pos / SR; const float v = (float) (t < 0.75 ? t / 0.75 : 2.0 - t / 0.75); lp->setValueNotifyingHost (clampf (v, 0.0f, 1.0f)); pos += (size_t) blk; return blk; });
                check (finite (out), "automation NaN: " + id);
                const float cs = clickScore (out);
                float base = std::max (3.0f, staticScore[u]);   // x the unit's own clickiness
                if (! discrete && cs / base > 3.0f)
                {   // a setting can be clicky by nature at its end stops (attack-only SPLIT, full vinyl crackle): judge against the unit held still there too
                    for (float v : { 0.0f, 1.0f })
                    {
                        Proc h (true); setupFor (h, u); h->apvts.getParameter (id)->setValueNotifyingHost (v); run (h, pre);
                        base = std::max (base, clickScore (run (h, pd)));
                    }
                }
                const float excess = cs / base;
                if (discrete) { if (excess > 3.0f) ++discreteClicks; }
                else
                {
                    if (excess > worstCont) { worstCont = excess; worstContName = id + " @" + juce::String (blk) + " (" + juce::String (cs, 1) + " vs static " + juce::String (base, 1) + ")"; }
                    if (excess > 3.0f) offenders.push_back ({ excess, id + " @" + juce::String (blk) + " (" + juce::String (cs, 1) + " vs static " + juce::String (base, 1) + ")" });
                }
                ++tested;
            }
        }
        std::sort (offenders.begin(), offenders.end(), [] (auto& a, auto& b) { return a.first > b.first; });
        std::printf ("     %d sweeps over %d parameters; continuous worst: %.1fx the unit's static click score (%s); %d continuous sweeps > 3x; %d discrete (switch / choice) sweeps > 3x (switches jump by nature)\n",
                     tested, (int) params.size(), worstCont, worstContName.toRawUTF8(), (int) offenders.size(), discreteClicks);
        for (size_t i = 0; i < offenders.size() && i < 16; ++i) std::printf ("       %6.1fx  %s\n", offenders[i].first, offenders[i].second.toRawUTF8());
        check (offenders.empty(), "continuous parameters that click under automation: " + juce::String ((int) offenders.size()));
    }

    // ------------------------------------------------------------ T3 browsing storm
    if (want ("T3"))
    {
        std::printf ("  T3 preset / scene browsing while a pad plays: a storm (a change every 20 ms for 6 s) and a person clicking through (every 250 ms):\n"
                     "     scenes, unit presets, undo. Clicks = 1 ms windows whose edge energy / signal energy exceeds anything the scenes and presets make held still\n");
        // edge ratio: (second-difference energy / signal energy) per 1 ms window, the loudest window; level-independent, so sound coming
        // out of a silent flush with a fade is not a click, while a hard step (even from silence) is
        auto edgeRatio = [] (const Stereo& o, size_t from, double* where)
        {
            const size_t w = S (0.001); float worst = 0; size_t at = 0;
            for (size_t k = from / w; (k + 1) * w < o.size(); ++k)
            {
                double e2 = 0, e0 = 0;
                for (size_t i = std::max<size_t> (2, k * w); i < (k + 1) * w; ++i)
                    for (const auto* ch : { &o.l, &o.r }) { const auto& x = *ch; const double d = (double) x[i] - 2.0 * x[i - 1] + x[i - 2]; e2 += d * d; e0 += (double) x[i] * x[i]; }
                if (e0 < (double) w * 2.0 * 1.0e-8) continue;   // below -80 dBFS
                const float r = (float) (e2 / e0);
                if (r > worst) { worst = r; at = k; }
            }
            if (where) *where = (double) at * 0.001;
            return worst;
        };
        const auto& fs = factoryScenes();
        const auto sig = pad (7.0);
        float staticMax = 0; juce::String staticWorst;
        {   // every scene and every unit preset held still (the same pad)
            auto one = [&] (const juce::String& nm, std::function<void (Proc&)> setup)
            { Proc q (true); setup (q); q.pump (10); run (q, slice (sig, 0, S (0.4))); const float r = edgeRatio (run (q, slice (sig, S (0.4), S (1.4))), 0, nullptr); if (r > staticMax) { staticMax = r; staticWorst = nm; } };
            for (auto& sc : fs) one (juce::String ("scene ") + sc.key, [&sc] (Proc& q) { q.scene (sc.key); });
            for (int u = 0; u < kNumUnits; ++u) for (int i = 0; i < (int) unitPresets (u).size(); ++i)
                one (juce::String (unitInfo (u).key) + " #" + juce::String (i), [u, i] (Proc& q) { q.set (pid (u, "on"), 1); q.impl().applyUnitPreset (u, i); });
        }
        for (double every : { 0.02, 0.25 })
        {
            Proc p (true); p.pump();
            std::vector<std::pair<double, std::function<void()>>> acts; juce::StringArray names;
            for (int i = 0; 0.3 + i * every < 6.6; ++i)
            {
                const double t = 0.3 + i * every; const float r = rnd01();
                if (r < 0.4) { acts.push_back ({ t, [&p, &fs, i] { p.scene (fs[(size_t) i % fs.size()].key); } }); names.add (juce::String ("scene ") + fs[(size_t) i % fs.size()].key); }
                else if (r < 0.9) { const int u = i % kNumUnits; const int n = (int) unitPresets (u).size(); acts.push_back ({ t, [&p, u, n, i] { if (n > 0) p.impl().applyUnitPreset (u, i % n); } }); names.add (juce::String ("preset ") + unitInfo (u).key + " #" + juce::String (n > 0 ? i % n : -1)); }
                else { acts.push_back ({ t, [&p] { p.impl().undo(); } }); names.add ("undo"); }
            }
            std::atomic<int> lastAct { -1 }; float prevX = 0, prevD = 0;
            if (gOnly.contains ("t3dbg"))
                gLiveProbe = [&] (const float* x, int n, size_t q)
                {
                    for (int i = 0; i < n; ++i)
                    {
                        const float d = x[i] - prevX;
                        if (std::abs (x[i]) < 1.0e-4f && std::abs (prevX) > 0.02f)
                        {
                            auto& im = p.impl();
                            std::printf ("       STEP at %.4f s: %.4f -> %.4f (last action %d) orderFade %d flush %d | on:", (double) (q + (size_t) i) / SR, prevX, x[i], lastAct.load(), im.orderFade, im.flushLeft);
                            for (int u = 0; u < kNumUnits; ++u) if (im.active[(size_t) u]) std::printf (" %s(on%.2f m%.2f d%.2f lv%.3f)", unitInfo (u).key, im.onSm[(size_t) u].cur, im.mixSm[(size_t) u].cur, im.duckG[(size_t) u], im.tm.unitLevel[(size_t) u].load());
                            std::printf (" | order:"); for (int k = 0; k < kNumUnits; ++k) if (im.active[(size_t) im.order[(size_t) k]]) std::printf (" %s", unitInfo (im.order[(size_t) k]).key);
                            int z = 0; while (i + z < n && std::abs (x[i + z]) < 1.0e-4f) ++z; std::printf (" | zeros %d", z);
                            std::printf ("\n");
                        }
                        prevD = d; prevX = x[i];
                    }
                };
            for (size_t i = 0; i < acts.size(); ++i) { auto f = acts[i].second; acts[i].second = [f, &lastAct, i] { lastAct = (int) i; f(); }; }
            const auto out = runLive (p, sig, acts);
            gLiveProbe = nullptr;
            bool stuck = false; for (int u = 0; u < kNumUnits; ++u) stuck = stuck || p.impl().duckReq[(size_t) u].load() != 0 || p.impl().duckG[(size_t) u] < 1.0f;
            double at = 0; const float er = edgeRatio (out, S (0.3), &at);
            int cuts = 0;   // hard cuts into digital silence: a sample above -34 dBFS followed by >= 32 exact zeros (fades never do that)
            for (size_t i = 1; i + 32 < out.size(); ++i)
                if (std::abs (out.l[i - 1]) > 0.02f && out.l[i] == 0.0f) { bool z = true; for (size_t j = i; j < i + 32 && z; ++j) z = std::abs (out.l[j]) < 1.0e-6f; if (z) ++cuts; }
            if (gOnly.contains ("dump"))
            {
                writeWav (juce::File ("/root/work/box3/t3_storm_" + juce::String ((int) (every * 1000)) + ".wav"), out);
                for (size_t i = 0; i < acts.size(); ++i) if (std::abs (acts[i].first - at) < 0.12) std::printf ("       %.3f s  %s\n", acts[i].first, names[(int) i].toRawUTF8());
            }
            std::printf ("     every %3d ms: %d changes, finite %d, peak %.3f, undo depth %d (max 48), stuck ducks %d, hard cuts %d, worst edge ratio %.3f at %.3f s (held-still worst %.3f, %s)\n",
                         (int) (every * 1000), (int) acts.size(), (int) finite (out), peak (out), p.impl().undoDepth(), (int) stuck, cuts, er, at, staticMax, staticWorst.toRawUTF8());
            check (finite (out) && peak (out) <= 1.0f && p.impl().undoDepth() <= (int) Rack::Impl::kUndoMax && ! stuck, "browsing storm state");
            check (cuts == 0, "hard cuts while browsing: " + juce::String (cuts));
            check (er <= 1.25f * staticMax, "browsing clicks (edge ratio " + juce::String (er, 3) + " vs held-still " + juce::String (staticMax, 3) + ")");
        }
    }

    // ------------------------------------------------------------ T4 mid-play save + restore
    if (want ("T4"))
    {
        std::printf ("  T4 mid-play state save + restore (same processor) == continuing\n");
        float worst = -200;
        for (auto* key : { "galaxyecho", "parallelwreck", "portalcloud", "talkingbass", "pumpfloor" })
        {
            Proc a (true), b (true); a.scene (key); b.scene (key); a.pump(); b.pump();
            const auto sig = mixOf (pad (4.0), drums (4.0), 0.5f);
            const auto ra = run (a, sig);
            auto rb = run (b, slice (sig, 0, S (2.0)));
            juce::MemoryBlock mb; b->getStateInformation (mb); b->setStateInformation (mb.getData(), (int) mb.getSize());
            append (rb, run (b, slice (sig, S (2.0), sig.size())));
            const float e = diffDb (rb, ra, S (2.05), 0);
            worst = std::max (worst, e);
            std::printf ("     %-14s after the restore vs continuing: %.1f dB\n", key, e);
            check (e < -40.0f, juce::String ("mid-play restore changes the sound: ") + key);
        }
    }

    // ------------------------------------------------------------ T4b host preset browsing (setStateInformation while audio plays)
    if (want ("T4"))
    {
        std::printf ("  T4b host preset browsing: setStateInformation with a different state every 300 ms while a pad plays (ducked, click-free) vs the rack's own scene loads\n");
        const char* keys[] { "radiate", "dub", "galaxyecho", "tape", "wide", "dustydream", "fatcrush", "space" };
        std::vector<juce::MemoryBlock> states;
        for (auto* k : keys) { Proc q (true); q.scene (k); q.pump (20); juce::MemoryBlock mb; q->getStateInformation (mb); states.push_back (mb); }
        const auto sig = pad (3.6);
        Proc a (true), b (true); a.pump(); b.pump();
        std::vector<std::pair<double, std::function<void()>>> hostActs, rackActs;
        for (int i = 0; i < 10; ++i)
        {
            const double t = 0.3 + 0.3 * i; const size_t k = (size_t) i % states.size();
            hostActs.push_back ({ t, [&a, &states, k] { a->setStateInformation (states[k].getData(), (int) states[k].getSize()); } });
            rackActs.push_back ({ t, [&b, &keys, k] { b.scene (keys[k]); } });
        }
        const auto oa = runLive (a, sig, hostActs), ob = runLive (b, sig, rackActs);
        double atA = 0, atB = 0; const float ca = clickScore (oa, &atA), cb = clickScore (ob, &atB);
        std::printf ("     host restores: click score %.1f (at %.2f s), finite %d | rack scene loads: %.1f (at %.2f s)\n", ca, atA, (int) finite (oa), cb, atB);
        check (finite (oa) && ca <= std::max (10.0f, 1.5f * cb), "host preset browsing clicks");
    }

    // ------------------------------------------------------------ T5 offline == realtime
    if (want ("T5"))
    {
        std::printf ("  T5 offline bounce == realtime playback, every unit (first preset) and scene: max |diff|\n");
        float worst = 0; juce::String worstName; int n = 0;
        auto one = [&] (const juce::String& name, std::function<void (Proc&)> setup)
        {
            Proc rt (true), off (true); off->setNonRealtime (true); setup (rt); setup (off); rt.pump(); off.pump();
            const auto sig = mixOf (pad (2.0), drums (2.0), 0.5f);
            const float d = maxDiff (run (rt, sig), run (off, sig), 0, 0);
            if (d > worst) { worst = d; worstName = name; }
            check (d < 1.0e-6f, "offline differs from realtime: " + name + " " + juce::String (d, 6)); ++n;
        };
        for (int u = 0; u < kNumUnits; ++u) if (u != uLim) one (juce::String ("unit ") + unitInfo (u).key, [u] (Proc& p) { p.set (pid (u, "on"), 1); if (! unitPresets (u).empty()) p.impl().applyUnitPreset (u, 0); });
        for (auto& sc : factoryScenes()) one (juce::String ("scene ") + sc.key, [&sc] (Proc& p) { p.scene (sc.key); });
        std::printf ("     %d renders, worst %.2e (%s). SPECTRA's HQ BOUNCE (off by default) is the only offline-only mode and is labelled.\n", n, worst, worstName.toRawUTF8());
    }

    // ------------------------------------------------------------ T6 bypass null per unit
    if (want ("T6"))
    {
        std::printf ("  T6 bypass null with every unit on (first preset): output == input delayed by the reported latency\n");
        float worst = 0, worstLegacy = 0; juce::String worstName;
        const auto in = withTail (drums (1.5), 0.3);
        for (int u = 0; u < kNumUnits; ++u)
        {
            if (u == uLim) continue;
            Proc p (true); p.set (pid (u, "on"), 1); if (! unitPresets (u).empty()) p.impl().applyUnitPreset (u, 0); p.pump();
            run (p, slice (in, 0, S (0.3))); p.set ("bypass", 1); p.pump(); run (p, slice (in, 0, S (0.3)));
            const int lat = p->getLatencySamples();
            const auto out = run (p, in);
            const float e = maxDiff (out, in, S (0.1), lat);
            if (e > worst) { worst = e; worstName = unitInfo (u).key; }
            check (e == 0.0f, juce::String ("BYPASS null ") + unitInfo (u).key + " err " + juce::String (e, 6) + " at latency " + juce::String (lat));
            // the legacy "Mute Wet" switch (dry/wet to 0, input gain kept) nulls too
            p.set ("bypass", 0); p.set ("box_bypass", 1); p.pump(); run (p, slice (in, 0, S (0.3)));
            const float e2 = maxDiff (run (p, in), in, S (0.1), p->getLatencySamples());
            worstLegacy = std::max (worstLegacy, e2);
            check (e2 < 1.0e-4f, juce::String ("Mute Wet null ") + unitInfo (u).key + " err " + juce::String (e2, 6));
        }
        std::printf ("     BYPASS: worst err %.2e (%s) vs the input delayed by the reported latency; legacy Mute Wet: worst %.2e\n", worst, worstName.toRawUTF8(), worstLegacy);
    }

    // ------------------------------------------------------------ T7 latency constant
    if (want ("T7"))
    {
        std::printf ("  T7 reported latency (FIXED LATENCY on) while units toggle, lanes move, the loop jumps; per sample rate\n");
        for (double sr : { 44100.0, 48000.0, 96000.0 })
        {
            SR = sr; Proc p (true); p.pump(); const int base = p->rack.latencySamples(); int minL = base, maxL = base;
            for (int u = 0; u < kNumUnits; ++u) { p.set (pid (u, "on"), 1); run (p, noise (0.05)); const int l = p->rack.latencySamples(); minL = std::min (minL, l); maxL = std::max (maxL, l); p.set (pid (u, "on"), u == uLim ? 1 : 0); }
            p.impl().setLane (uSpectra, 1); p.set ("fx_par_on", 1); run (p, noise (0.05)); minL = std::min (minL, p->rack.latencySamples()); maxL = std::max (maxL, p->rack.latencySamples());
            p.head.ppq = 100; run (p, noise (0.05)); p.head.ppq = 0; run (p, noise (0.05)); minL = std::min (minL, p->rack.latencySamples()); maxL = std::max (maxL, p->rack.latencySamples());
            std::printf ("     %6.0f Hz: %d samples (%.1f ms), min %d max %d while toggling every unit / lanes / loop jump\n", sr, base, base * 1000.0 / sr, minL, maxL);
            check (minL == base && maxL == base, "latency moved at " + juce::String (sr));
        }
        {   // the host changes the sample rate of a running instance (re-prepare): the report follows and BYPASS stays aligned
            SR = 48000.0; Proc p (true); p.scene ("portalcloud"); p.pump(); run (p, noise (0.2)); const int l48 = p->getLatencySamples();
            SR = 96000.0; p->setPlayConfigDetails (2, 2, SR, 512); p->prepareToPlay (SR, 512); p.pump(); const int l96 = p->getLatencySamples();
            p.set ("bypass", 1); const auto in = withTail (drums (0.8), 0.2); run (p, slice (in, 0, S (0.2)));
            const float e = maxDiff (run (p, in), in, S (0.05), l96);
            std::printf ("     re-prepare 48 -> 96 kHz: reported %d -> %d samples; bypassed vs input delayed by the new report: %.2e\n", l48, l96, e);
            check (l96 > l48 + l48 / 2 && e == 0.0f, "sample-rate change: latency report / bypass alignment");
        }
        SR = savedSR;
    }

    // ------------------------------------------------------------ T8 audio-thread allocations
    if (want ("T8"))
    {
        std::printf ("  T8 audio-thread allocations (operator new inside processBlock), scenes + units + modulation, 48 kHz\n");
        {   // the probes themselves: one allocation and one blocking lock inside an audio scope must be seen
            gAudioAllocs = 0; gAudioLocks = 0; std::mutex m;
            { AudioScope as; auto* v = new std::vector<int> (8); delete v; m.lock(); m.unlock(); }
            check ((! kAllocProbe || gAudioAllocs.load() >= 1) && (! kLockProbe || gAudioLocks.load() >= 1), "allocation / lock probes do not fire");
        }
        gAudioAllocs = 0; gAudioFrees = 0; gAudioLocks = 0; long worst = 0, worstLocks = 0; juce::String worstName, worstLockName;
        auto one = [&] (const juce::String& name, std::function<void (Proc&)> setup)
        {
            Proc p (true); setup (p); p.pump();
            const long a0 = gAudioAllocs.load(), l0 = gAudioLocks.load();
            runBlocks (p, mixOf (pad (1.0), drums (1.0), 0.5f), [&] { return 1 + (int) (rnd01() * 1023.0f); });
            const long n = gAudioAllocs.load() - a0, nl = gAudioLocks.load() - l0;
            if (n > worst) { worst = n; worstName = name; }
            if (nl > worstLocks) { worstLocks = nl; worstLockName = name; }
            check (n == 0, "audio-thread allocation in " + name + ": " + juce::String (n));
            check (nl == 0, "audio-thread blocking lock in " + name + ": " + juce::String (nl));
        };
        for (auto& sc : factoryScenes()) one (juce::String ("scene ") + sc.key, [&sc] (Proc& p) { p.scene (sc.key); });
        for (int u = 0; u < kNumUnits; ++u) if (u != uLim) one (juce::String ("unit ") + unitInfo (u).key, [u] (Proc& p) { p.set (pid (u, "on"), 1); if (! unitPresets (u).empty()) p.impl().applyUnitPreset (u, 0); });
        one ("morph + motion + macros", [] (Proc& p) { p.scene ("dustydream"); p.impl().morphSet (0); p.scene ("galaxyecho"); p.impl().morphSet (1); p.set ("box_morph", 0.5f); p.impl().box.applyMotion (factoryMotions()[0]); p.set ("box_macro7", 0.6f); });
        one ("brain audition + match", [] (Proc& p) { brain::audition (p.impl(), 1); });
        std::printf ("     total audio-thread allocations %ld (frees %ld), worst %ld (%s); blocking locks %ld%s, worst %ld (%s)\n", gAudioAllocs.load(), gAudioFrees.load(), worst, worstName.toRawUTF8(),
                     gAudioLocks.load(), kLockProbe ? "" : " (lock probe off under ASan)", worstLocks, worstLockName.toRawUTF8());
    }

    // ------------------------------------------------------------ T11 undo / redo coverage
    if (want ("T11"))
    {
        std::printf ("  T11 undo / redo coverage: knob gestures, switch clicks, unit presets, scenes; one step each; redo; a new edit forks\n");
        Proc p (true); p.pump();
        auto gesture = [&p] (const juce::String& id, float v) { auto* q = p->apvts.getParameter (id); q->beginChangeGesture(); q->setValueNotifyingHost (q->convertTo0to1 (v)); q->endChangeGesture(); juce::Thread::sleep (40); };
        auto& im = p.impl();
        const int d0 = im.undoDepth();
        gesture (pid (uFilter, "on"), 1); gesture (pid (uFilter, "cutoff"), 900.0f); gesture (pid (uFilter, "cutoff"), 300.0f);
        const int d1 = im.undoDepth();
        p.impl().applyUnitPreset (uEcho, 1); const int d2 = im.undoDepth();
        p.scene ("dub"); const int d3 = im.undoDepth();
        im.undo(); im.undo(); im.undo();   // scene, preset, the second cutoff move
        const float c1 = p.get (pid (uFilter, "cutoff"));
        im.redo(); const float c0 = p.get (pid (uFilter, "cutoff"));
        im.redo(); const bool echoBack = p.get (pid (uEcho, "on")) > 0.5f;
        gesture (pid (uFilter, "reso"), 0.7f); const int redoAfterFork = im.redoDepth();
        std::printf ("     steps: 3 gestures -> %d, unit preset -> +%d, scene -> +%d; undo x3 -> cutoff %.0f Hz (900 expected); redo -> cutoff %.0f Hz (300), redo -> ECHO preset back %d; redo depth after a new edit %d\n",
                     d1 - d0, d2 - d1, d3 - d2, c1, c0, (int) echoBack, redoAfterFork);
        check (d1 - d0 == 3 && d2 - d1 == 1 && d3 - d2 == 1 && std::abs (c1 - 900.0f) < 1.0f && std::abs (c0 - 300.0f) < 1.0f && echoBack && redoAfterFork == 0, "undo / redo coverage");
    }

    // ------------------------------------------------------------ T10 CPU budget
    if (want ("T10"))
    {
        std::printf ("  T10 CPU: %% of one core per scene (512-sample blocks, pad + drums), 10 heaviest at 44.1 and 48 kHz\n");
        for (double sr : { 44100.0, 48000.0 })
        {
            SR = sr; std::vector<std::pair<double, juce::String>> cost;
            const auto sig = mixOf (pad (4.0), drums (4.0), 0.5f);
            for (auto& sc : factoryScenes())
            {
                Proc p (true); p.scene (sc.key); p.pump(); run (p, slice (sig, 0, S (0.5)));
                renderSecs = 0; run (p, sig);
                const double msPerBlock = renderSecs * 1000.0 / ((double) sig.size() / 512.0), blockMs = 512.0 / sr * 1000.0;
                cost.push_back ({ msPerBlock / blockMs * 100.0, sc.key });
            }
            std::sort (cost.begin(), cost.end(), [] (auto& a, auto& b) { return a.first > b.first; });
            std::printf ("     %.0f Hz:", sr); for (size_t i = 0; i < cost.size() && i < 10; ++i) std::printf ("  %s %.1f%%", cost[i].second.toRawUTF8(), cost[i].first); std::printf ("\n");
            check (cost[0].first < 35.0, "heaviest scene over 35 % of a core at " + juce::String (sr));
        }
        SR = savedSR;
    }
    SR = savedSR;
}

// ---- T9: UI hammering while audio runs (needs a display; run in Release and under ASan)
static int stressUi (double seconds)
{
    Proc p (true); p.scene ("galaxyecho");
    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
    ed->setSize (1180, 780);
    Panel* panel = nullptr; for (auto* c : ed->getChildren()) if (auto* pp = dynamic_cast<Panel*> (c)) panel = pp;
    if (panel == nullptr) { std::printf ("no panel\n"); return 1; }
    std::vector<juce::RangedAudioParameter*> params;
    for (auto* q : p->getParameters()) if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (q)) params.push_back (rp);
    uint32_t rng = 0xBEEFu; auto r01 = [&rng] { rng = rng * 1664525u + 1013904223u; return (float) (rng >> 8) / 16777216.0f; };
    std::vector<std::pair<double, std::function<void()>>> acts;
    const auto& fs = factoryScenes();
    for (double t = 0.2; t < seconds - 0.3; t += 0.015)
    {
        const float r = r01();
        if (r < 0.55) acts.push_back ({ t, [&, i = (size_t) (r01() * (float) params.size())] { params[std::min (i, params.size() - 1)]->setValueNotifyingHost (r01()); } });
        else if (r < 0.7) acts.push_back ({ t, [&, m = (int) (r01() * 5.0f)] { panel->getProperties().set ("mode", m); panel->resized(); } });
        else if (r < 0.85) acts.push_back ({ t, [&, u = (int) (r01() * (float) kNumUnits)] { panel->getProperties().set ("select", juce::jlimit (0, kNumUnits - 1, u)); panel->resized(); } });
        else if (r < 0.92) acts.push_back ({ t, [&, k = (size_t) (r01() * (float) fs.size())] { p.scene (fs[std::min (k, fs.size() - 1)].key); } });
        else if (r < 0.94) acts.push_back ({ t, [&] { ed->setSize (960 + (int) (r01() * 800), 720 + (int) (r01() * 400)); } });
        else if (r < 0.96) acts.push_back ({ t, [&] { p.set ("bypass", p.get ("bypass") > 0.5f ? 0.0f : 1.0f); } });
        else if (r < 0.97) acts.push_back ({ t, [&] { p->abToggle(); } });
        else if (r < 0.98) acts.push_back ({ t, [&] { panel->getProperties().set ("browse", r01() < 0.5f ? 1 : 0); panel->resized(); } });
        else if (r < 0.99) acts.push_back ({ t, [&] { p.impl().redo(); } });
        else acts.push_back ({ t, [&] { p.impl().undo(); } });
    }
    // Where does a peak above 0 dBFS come from? Per block: the hammered OUTPUT gain (up to +24 dB, after the limiter), INPUT gain on the
    // dry share of DRY/WET (the dry path skips the limiter), and blocks with the limiter switched off. With the limiter on, every block
    // must stay under   out gain x (ceiling + (1 - mix) x in gain x input peak)   (gains / mix taken at their worst over this and the
    // previous two blocks, since they glide), plus the raw input BYPASS may be fading in: anything above that = a unit blowing up.
    const auto sig = mixOf (pad (seconds), drums (seconds), 0.5f);
    struct Blk { float gOut, gIn, mix; bool limOn; };
    std::vector<Blk> hist; float worstRatio = 0, peakLimOn = 0, peakLimOff = 0; Blk atWorst {}; double worstT = 0;
    auto* gOutP = p->apvts.getRawParameterValue ("box_out"); auto* gInP = p->apvts.getRawParameterValue ("box_in"); auto* mixP = p->apvts.getRawParameterValue ("box_mix");
    auto* limP = p->apvts.getRawParameterValue (pid (uLim, "on")); auto* mwP = p->apvts.getRawParameterValue ("box_bypass");
    gLiveProbe = [&] (const float* x, int n, size_t q)
    {
        Blk b { gpfx::dbToGain (gOutP->load()), gpfx::dbToGain (gInP->load()), mwP->load() > 0.5f ? 0.0f : mixP->load(), limP->load() > 0.5f };
        hist.push_back (b);
        Blk w = b; bool limAll = b.limOn;
        for (size_t k = hist.size() >= 3 ? hist.size() - 3 : 0; k < hist.size(); ++k) { w.gOut = std::max (w.gOut, hist[k].gOut); w.gIn = std::max (w.gIn, hist[k].gIn); w.mix = std::min (w.mix, hist[k].mix); limAll = limAll && hist[k].limOn; }
        float pk = 0, inPk = 0; for (int i = 0; i < n; ++i) { pk = std::max (pk, std::abs (x[i])); inPk = std::max (inPk, std::abs (sig.l[q + (size_t) i])); }
        inPk = std::max (inPk, peak (sig));   // (the dry / raw paths are delayed by the latency: the whole signal's peak is the safe bound)
        if (! limAll) { peakLimOff = std::max (peakLimOff, pk); return; }
        peakLimOn = std::max (peakLimOn, pk);
        const float bound = w.gOut * (1.0f + (1.0f - w.mix) * w.gIn * inPk) + inPk;
        const float r = pk / bound;
        if (r > worstRatio) { worstRatio = r; atWorst = w; worstT = (double) q / SR; }
    };
    const auto out = runLive (p, sig, acts);
    gLiveProbe = nullptr;
    std::printf ("stress: %.0f s, %d actions, finite %d, peak %.3f, audio-thread allocations %ld\n", seconds, (int) acts.size(), (int) finite (out), peak (out), gAudioAllocs.load());
    std::printf ("  peak with the limiter on %.3f, with it switched off %.3f; worst block vs its bound %.2f at %.2f s (out gain x%.2f, in gain x%.2f, mix %.2f)\n",
                 peakLimOn, peakLimOff, worstRatio, worstT, atWorst.gOut, atWorst.gIn, atWorst.mix);
    const bool ok = finite (out) && worstRatio <= 1.0f;
    std::printf ("%s\n", ok ? "stress: peaks explained (gains / limiter off), no unit blew up" : "stress: FAIL - a peak above what the gains allow");
    ed.reset();
    return ok ? 0 : 1;
}
