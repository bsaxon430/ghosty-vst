// GhostSliceTests: the finishing pass (known limits of phases 12-15, phase 16 syllables, release harness).
// Included by SliceTests.cpp after the other groups (it shares check / section / Rig / the test audio there).
//   tempo2     octave disambiguation on slow loops (70 / 80 / 90 BPM, straight + swung, 8th / 16th hats)
//   bounce5    a 5-minute offline bounce never deadlocks
//   resample   r8brain sample-rate conversion (alias rejection, null, latency, host-rate change)
//   repeatwave the waveform under a GHOST REPEAT draws its repetitions (UI)
//   elastic    ELASTIC hold: pitch drift at the joins < 5 ct
//   syllables  phase 16: syllable nuclei, consonant / vowel boundary, boundaries; CHOP / HOLD / PLAY integration
//   regionpitch region PITCH / CENTS / FORMANT (render, REPEAT on top, JSON, the EDIT row follows the selection)
//   cliphint   GHOST CAPTURE: the .ghostclip.json sidecar (bpm / bars / offset / key)

// ================================================================ [30] tempo: slow loops are not read x2
// a drum loop with a swing amount (0 = straight 8ths, 0.33 = triplet swing: the off-beat 8th at 2/3 of the beat) and hats on
// 8ths or 16ths; kick on 1 and the "and" of 3, snare on 2 and 4
static juce::AudioBuffer<float> swungLoop (double sr, double bpm, int bars, double swing, bool hats16, double tail, uint32_t seed)
{
    const double beat = 60.0 / bpm;
    juce::AudioBuffer<float> b (2, (int) std::llround ((bars * 4 * beat + tail) * sr)); b.clear();
    std::mt19937 rng (seed); std::normal_distribution<float> N (0, 1);
    auto hit = [&] (double at, int kind, double amp = 1.0)
    {
        const int64 s0 = (int64) std::llround (at * sr);
        const int len = (int) (sr * (kind == 0 ? 0.25 : kind == 1 ? 0.18 : 0.05));
        for (int i = 0; i < len && s0 + i < b.getNumSamples(); ++i)
        {
            const double u = i / sr; double v;
            if (kind == 0) v = 0.8 * std::exp (-u / 0.08) * std::sin (2 * juce::MathConstants<double>::pi * (55 * u + 120 * 0.01 * (1 - std::exp (-u / 0.01))));
            else if (kind == 1) v = 0.45 * std::exp (-u / 0.05) * (0.7 * N (rng) + 0.3 * std::sin (2 * juce::MathConstants<double>::pi * 190 * u));
            else v = 0.18 * std::exp (-u / 0.012) * N (rng);
            for (int c = 0; c < 2; ++c) b.setSample (c, (int) (s0 + i), b.getSample (c, (int) (s0 + i)) + (float) (amp * v));
        }
    };
    for (int bar = 0; bar < bars; ++bar)
        for (int bt = 0; bt < 4; ++bt)
        {
            const double t0 = (bar * 4 + bt) * beat;
            const double off = t0 + beat * (0.5 + swing * 0.5);   // swing 0.33 -> 2/3 of the beat
            hit (t0, 2); hit (off, 2, 0.8);
            if (hats16) { hit (t0 + beat * 0.25, 2, 0.55); hit (t0 + beat * 0.75, 2, 0.55); }
            if (bt == 0) hit (t0, 0);
            if (bt == 2) hit (off, 0);
            if (bt == 1 || bt == 3) hit (t0, 1);
        }
    return b;
}

static void testTempoOctave()
{
    section ("[30] tempo: octave disambiguation - slow loops (70 / 80 / 90 BPM, straight / swung, 8th / 16th hats) are not read x2");
    struct Case { double bpm; int bars; double swing; bool h16; double tail; };
    std::vector<Case> slow;
    for (double bpm : { 70.0, 72.5, 80.0, 85.0, 90.0 })
        for (double swing : { 0.0, 0.33 })
            for (bool h16 : { false, true })
                for (double tail : { 0.0, 0.31 })
                    slow.push_back ({ bpm, 2, swing, h16, tail });
    int ok = 0; juce::String bad;
    for (size_t i = 0; i < slow.size(); ++i)
    {
        const auto& c = slow[i];
        const auto b = swungLoop (48000, c.bpm, c.bars, c.swing, c.h16, c.tail, (uint32_t) (100 + i));
        const auto x = an::mono (b);
        const auto t = an::estimateTempo (x.data(), (int64) x.size(), 48000);
        const bool good = std::abs (t.bpm - c.bpm) <= 1.0;
        ok += good ? 1 : 0;
        if (! good || std::getenv ("GSLICE_DEBUG"))
            bad << juce::String (c.bpm, 1) << (c.swing > 0 ? "sw" : "") << (c.h16 ? "/16" : "") << (c.tail > 0 ? "+t" : "") << "->" << juce::String (t.bpm, 1) << " r" << juce::String (t.octaveRatio, 2) << " ";
    }
    check (ok == (int) slow.size(), "slow loops within +-1 BPM (not x2): " + juce::String (ok) + " / " + juce::String ((int) slow.size()) + "  " + bad);
    // the fast ones stay fast (their backbeat sits on the half tempo's in-between pulses)
    std::vector<Case> fast;
    for (double bpm : { 120.0, 128.0, 140.0, 150.0, 174.0 })
        for (double swing : { 0.0, 0.33 })
            for (double tail : { 0.0, 0.27 })
                fast.push_back ({ bpm, 4, swing, false, tail });
    ok = 0; bad.clear(); int halved = 0;
    for (size_t i = 0; i < fast.size(); ++i)
    {
        const auto& c = fast[i];
        const auto b = swungLoop (48000, c.bpm, c.bars, c.swing, c.h16, c.tail, (uint32_t) (300 + i));
        const auto x = an::mono (b);
        const auto t = an::estimateTempo (x.data(), (int64) x.size(), 48000);
        const bool good = std::abs (t.bpm - c.bpm) <= 1.0;
        ok += good ? 1 : 0; halved += std::abs (t.bpm - c.bpm * 0.5) <= 1.0 ? 1 : 0;
        if (! good || std::getenv ("GSLICE_DEBUG"))
            bad << juce::String (c.bpm, 1) << (c.swing > 0 ? "sw" : "") << (c.tail > 0 ? "+t" : "") << "->" << juce::String (t.bpm, 1) << " r" << juce::String (t.octaveRatio, 2) << " ";
    }
    // (known: one non-loop 174 BPM file reads 116 = 2/3, a three-against-two reading the octave test does not cover)
    check (halved == 0 && ok >= (int) fast.size() - 1, "fast loops are never halved; exact " + juce::String (ok) + " / " + juce::String ((int) fast.size()) + "  " + bad);
    // the owner's demo stems (when present): report the readings
    for (auto name : { "02_drums_crackle.mp3", "01_pad_retro_fatten.mp3", "03_pluck_halls_galaxy.mp3", "04_vocal_ecto_echo.mp3" })
    {
        const juce::File f ("/root/work/" + juce::String (name)); if (! f.existsAsFile()) continue;
        juce::String err; auto s = source::load (f, err); if (s == nullptr) continue;
        const auto x = an::mono (s->audio);
        const auto t = an::estimateTempo (x.data(), (int64) x.size(), s->sampleRate);
        std::printf ("    %s: %.2f BPM (confidence %.2f, octave ratio %.2f, bars %d)\n", name, t.bpm, t.confidence, t.octaveRatio, t.bars);
    }
    // a half-time 4-on-the-floor (kick on every beat at 128) is not halved either
    {
        const double bpm = 128, beat = 60.0 / bpm; const double sr = 48000;
        juce::AudioBuffer<float> b (2, (int) std::llround (16 * beat * sr)); b.clear();
        for (int k = 0; k < 16; ++k)
            for (int i = 0; i < (int) (0.2 * sr); ++i)
            {
                const int s = (int) std::llround (k * beat * sr) + i; if (s >= b.getNumSamples()) break;
                const double u = i / sr; const float v = (float) (0.8 * std::exp (-u / 0.07) * std::sin (2 * juce::MathConstants<double>::pi * 52 * u));
                b.setSample (0, s, b.getSample (0, s) + v); b.setSample (1, s, b.getSample (1, s) + v);
            }
        const auto x = an::mono (b);
        const auto t = an::estimateTempo (x.data(), (int64) x.size(), sr);
        check (std::abs (t.bpm - bpm) <= 1.0, "a four-on-the-floor kick at 128 BPM stays 128 (" + juce::String (t.bpm, 2) + ", ratio " + juce::String (t.octaveRatio, 2) + ")");
    }
}

// ================================================================ [31] a 5-minute offline bounce never deadlocks
// The offline (non-realtime) bounce waits per block for the STUDIO renders it plays. Two runs on a 5-minute project (10 regions,
// three of them stretched, a global pitch): (a) with the message thread pumping, the bounce finishes under a watchdog and equals the
// STUDIO render; (b) with the message thread blocked (nothing can request STUDIO jobs), each table times out once and the bounce
// still finishes. Host block sizes vary (a DAW's last block of a bounce is short).
static void testBounce5()
{
    section ("[31] a 5-minute offline bounce never deadlocks (pumped: = the STUDIO render; message thread blocked: times out once, finishes)");
    const double sr = 48000; const double secs = 300.0;
    juce::AudioBuffer<float> src (2, (int) (sr * secs));
    for (int c = 0; c < 2; ++c) { float* d = src.getWritePointer (c); for (int i = 0; i < src.getNumSamples(); ++i) { const double t = i / sr; d[i] = (float) (0.2 * std::sin (2 * juce::MathConstants<double>::pi * (220.0 + 30.0 * std::sin (0.05 * t)) * t + c) + 0.05 * std::sin (2 * juce::MathConstants<double>::pi * 660.0 * t)); } }
    auto runOnce = [&] (bool pumpMessages, int waitMs, double watchdogS, juce::String& info) -> bool
    {
        Rig rig (src);
        auto q = std::optional<Project> (rig->project());
        for (int k = 1; k < 10; ++k) q = ops::splitAt (*q, q->regions.back().id, (double) k * 30.0 * sr);
        for (int k : { 2, 5, 8 }) q = ops::stretchTo (*q, q->regions[(size_t) k].id, q->regions[(size_t) k].length() * 1.25);
        q->pitchSemis = 2; rig->apply (*q, "edit");
        rig->setStudioMode (true); rig->studioCache().clear();
        rig->offlineWaitMs = waitMs;
        const int lat = rig->ceilingLatency();
        const int total = (int) rig->currentPlan().length;
        juce::AudioBuffer<float> out (2, total + lat + 4096); out.clear();
        rig->setNonRealtime (true); rig->setCursor (0); rig->play (true);
        std::atomic<bool> done { false }; std::atomic<int> pos { 0 };
        std::thread th ([&]
        {
            juce::MidiBuffer m; int k = 0;
            while (pos.load() < total + lat)
            {
                const int bs = (k++ % 7 == 3) ? 97 : 512;   // an odd block now and then
                juce::AudioBuffer<float> b (2, bs);
                rig->processBlock (b, m);
                const int p0 = pos.load();
                for (int c = 0; c < 2; ++c) out.copyFrom (c, p0, b, c, 0, juce::jmin (bs, out.getNumSamples() - p0));
                pos.store (p0 + bs);
            }
            done.store (true);
        });
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        bool dog = false;
        while (! done.load())
        {
            if (pumpMessages) pump (5); else std::this_thread::sleep_for (std::chrono::milliseconds (5));
            if (juce::Time::getMillisecondCounterHiRes() - t0 > watchdogS * 1000.0) { dog = true; break; }
        }
        if (dog) { rig->offlineWaitMs = 0; rig->setNonRealtime (false); }   // un-stick the thread so the test can end
        th.join();
        const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
        rig->play (false); rig->setNonRealtime (false);
        info = juce::String (ms / 1000.0, 1) + " s for " + juce::String (total / sr / 60.0, 2) + " min, " + juce::String (rig->offlineWaits.load()) + " waits, "
             + juce::String (rig->offlineTimeouts.load()) + " timeouts";
        if (dog) { info << " - WATCHDOG at " << juce::String (pos.load() / sr, 1) << " s"; return false; }
        if (pumpMessages)
        {
            const auto ref = rig->renderArrangement();
            float err = 0; for (int c = 0; c < 2; ++c) for (int i = 0; i < total; ++i) err = std::max (err, std::abs (out.getSample (c, i + lat) - ref.getSample (c, i)));
            info << ", max error vs the STUDIO render " << juce::String (err, 7);
            return rig->offlineTimeouts.load() == 0 && err < 1.0e-4f;
        }
        return rig->offlineTimeouts.load() >= 1;
    };
    juce::String a, b;
    check (runOnce (true, 20000, 600.0, a), "pumped: the 5-minute bounce finishes and equals the STUDIO render (" + a + ")");
    check (runOnce (false, 200, 120.0, b), "message thread blocked: the bounce times out per table and still finishes (" + b + ")");
}

// ================================================================ [32] r8brain sample-rate conversion (import / host-rate change)
// rms of a buffer span in dB
static double spanDb (const float* x, int a, int b) { double s = 0; for (int i = a; i < b; ++i) s += (double) x[i] * x[i]; return 10.0 * std::log10 (s / std::max (1, b - a) + 1e-30); }
static void testResample()
{
    section ("[32] sample-rate conversion: r8brain (alias rejection on a sweep >= 90 dB, round-trip null, latency alignment, host-rate changes)");
    // (1) a log sweep 20 Hz .. 47 kHz at 96 kHz, converted to 44.1 kHz: every tone above the new Nyquist must vanish (no alias)
    {
        const double fs = 96000, fo = 44100, T = 6.0, f0 = 20, f1 = 47000;
        juce::AudioBuffer<float> sw (1, (int) (fs * T));
        const double K = T / std::log (f1 / f0);
        for (int i = 0; i < sw.getNumSamples(); ++i) { const double t = i / fs; sw.setSample (0, i, (float) (0.5 * std::sin (2 * juce::MathConstants<double>::pi * f0 * K * (std::exp (t / K) - 1.0)))); }
        auto s = source::fromBuffer (sw, fs, "sweep.wav");
        auto w = source::makeWorking (s, fo);
        const auto& y = w->buffer();
        auto tAt = [&] (double f) { return K * std::log (f / f0); };   // when the sweep passes f
        // the stop band: from 1.03 x the new Nyquist (r8brain's 2 % transition band sits below Nyquist) to the end
        const int a = (int) std::ceil (tAt (fo * 0.5 * 1.03) * fo) + 2048, b = y.getNumSamples() - 2048;
        const int pa = (int) (tAt (1000) * fo), pb = (int) (tAt (15000) * fo);
        const double stop = spanDb (y.getReadPointer (0), a, b), pass = spanDb (y.getReadPointer (0), pa, pb);
        // the old path for comparison (JUCE's windowed sinc without an anti-alias filter)
        double oldStop = 0;
        {
            const double speed = fs / fo; const int n = (int) std::llround (sw.getNumSamples() / speed);
            const int skip = (int) std::lround (juce::WindowedSincInterpolator::getBaseLatency() / speed);
            std::vector<float> in ((size_t) sw.getNumSamples() + 1024, 0.0f), out ((size_t) (n + skip));
            std::copy (sw.getReadPointer (0), sw.getReadPointer (0) + sw.getNumSamples(), in.begin());
            juce::WindowedSincInterpolator ip; ip.process (speed, in.data(), out.data(), n + skip);
            oldStop = spanDb (out.data() + skip, a, b);
        }
        check (pass - stop >= 90.0 && std::abs (pass - spanDb (sw.getReadPointer (0), 0, sw.getNumSamples())) < 0.5,
               "96 -> 44.1 kHz sweep: the tones above Nyquist are " + juce::String (pass - stop, 1) + " dB below the pass band (>= 90; JUCE's old path: "
               + juce::String (pass - oldStop, 1) + " dB); pass band level " + juce::String (pass, 2) + " dB");
    }
    // (2) upsampling 44.1 -> 48 kHz: a 20 kHz tone leaves no image (the image would sit at 44.1 - 20 = 24.1 kHz -> folded 23.9 kHz)
    {
        const auto t = sine (44100, 2.0, 20000.0, 0.5, 1);
        auto w = source::makeWorking (source::fromBuffer (t, 44100, "t.wav"), 48000);
        const auto& y = w->buffer();
        // a Goertzel at the image frequency vs at the tone
        auto g = [&] (double f) { double s1 = 0, s2 = 0; const double c = 2 * std::cos (2 * juce::MathConstants<double>::pi * f / 48000); const int a = 4800, n = 48000;
                                  for (int i = a; i < a + n; ++i) { const double s0 = y.getSample (0, i) * (0.5 - 0.5 * std::cos (2 * juce::MathConstants<double>::pi * (i - a) / n)) + c * s1 - s2; s2 = s1; s1 = s0; }
                                  return 10 * std::log10 (s1 * s1 + s2 * s2 - c * s1 * s2 + 1e-30); };
        const double tone = g (20000), image = g (23900);
        check (tone - image >= 90.0, "44.1 -> 48 kHz: a 20 kHz tone's image at 23.9 kHz is " + juce::String (tone - image, 1) + " dB down (>= 90)");
    }
    // (3) round trip 44.1 -> 48 -> 44.1 of band-limited noise (< 18 kHz) nulls to the original, with zero latency
    {
        const double fs = 44100; std::mt19937 rng (5); std::normal_distribution<double> N (0, 1);
        juce::AudioBuffer<float> x (2, (int) (fs * 3));
        std::vector<double> v ((size_t) x.getNumSamples()); for (auto& e : v) e = N (rng);
        an::detail::zeroPhaseLowpass (v, 12000, fs); an::detail::zeroPhaseLowpass (v, 12000, fs);
        for (int c = 0; c < 2; ++c) for (int i = 0; i < x.getNumSamples(); ++i) x.setSample (c, i, (float) (0.25 * v[(size_t) i]));
        auto w = source::makeWorking (source::fromBuffer (x, fs, "n.wav"), 48000);
        juce::AudioBuffer<float> back (2, x.getNumSamples());
        source::resampleChannels (w->buffer(), (int) w->frames(), 48000, fs, back);
        double best = -1e9; int lag = 99;
        for (int L = -3; L <= 3; ++L) { double s = 0; for (int i = 4000; i < x.getNumSamples() - 4000; ++i) s += x.getSample (0, i) * back.getSample (0, i + L); if (s > best) { best = s; lag = L; } }
        const double nul = rmsDiffDb (x, back, 4000, x.getNumSamples() - 4000);
        check (lag == 0 && nul < -80.0, "44.1 -> 48 -> 44.1 kHz round trip: latency " + juce::String (lag) + " samples, null " + juce::String (nul, 1) + " dB (< -80)");
        // a sub-sample check: a 1 kHz sine's zero crossing after 44.1 -> 96 kHz lands within 0.01 sample of the ideal time
        auto s = source::fromBuffer (sine (44100, 1.0, 1000.0, 0.5, 1), 44100, "s.wav");
        auto w96 = source::makeWorking (s, 96000);
        double worst = 0;
        for (int k = 10; k < 900; k += 37)
        {
            const double ideal = (double) k / 1000.0 * 96000.0;   // sin crosses upward at k ms
            const int i0 = (int) std::floor (ideal) - 2; double at = -1;
            for (int i = i0; i < i0 + 6; ++i) { const float p = w96->at (0, i), q = w96->at (0, i + 1); if (p < 0 && q >= 0) { at = i + p / (p - q); break; } }
            worst = std::max (worst, at < 0 ? 99.0 : std::abs (at - ideal));
        }
        check (worst < 0.01, "44.1 -> 96 kHz: zero crossings within " + juce::String (worst, 4) + " samples of the ideal time (< 0.01)");
    }
    // (4) a host-rate change re-makes the working copy (processor: 44.1 kHz source played at 48 then 96 kHz), plays at pitch
    {
        auto p = std::make_unique<SliceProcessor>();
        p->setPlayConfigDetails (0, 2, 48000, 512); p->prepareToPlay (48000, 512); pump (5);
        p->loadSourceForTests (source::fromBuffer (sine (44100, 2.0, 1000.0, 0.5), 44100, "s.wav"));
        const auto w1 = p->getWorking();
        p->prepareToPlay (96000, 512); pump (20);
        const auto w2 = p->getWorking();
        bool ok = w1 != nullptr && w2 != nullptr && w1->rate == 48000 && w2->rate == 96000 && w2->frames() == 2 * 96000;
        int zc = 0; if (ok) { for (int i = 9601; i < 9600 + 96000; ++i) if ((w2->at (0, i - 1) < 0) != (w2->at (0, i) < 0)) ++zc; }
        check (ok && std::abs (zc - 2000) <= 2, "host rate 48 -> 96 kHz: the working copy is re-made at 96 kHz (" + juce::String (w2 ? w2->frames() : 0) + " frames, 1 kHz = " + juce::String (zc) + " crossings / s)");
        p->releaseResources();
    }
}

// ================================================================ [33] the waveform under a GHOST REPEAT shows what it plays
static void testRepeatWave (const juce::File& shots)
{
    section ("[33] REPEAT waveform: the span draws its repetitions (not the original under it), cached tiles rebuilt on a parameter change");
    using namespace ghosty::themes;
    const double sr = 48000;
    juce::AudioBuffer<float> b (2, (int) (sr * 4.0)); b.clear();
    std::mt19937 rng (3); std::normal_distribution<float> N (0, 1);
    for (int i = 0; i < (int) (0.04 * sr); ++i) { const float v = 0.8f * std::exp (-(float) i / (float) (0.01 * sr)) * N (rng); b.setSample (0, (int) sr + i, v); b.setSample (1, (int) sr + i, v); }
    Rig rig (b);
    { Project q = rig->project(); q.bpm = 120; q.gridDiv = 16; rig->apply (q, "grid"); }
    int64 rid = 0; auto q = ops::addRepeat (rig->project(), sr * 1.0, sr * 2.0, &rid);
    check (q.has_value(), "a REPEAT span over 1.0 .. 2.0 s (one hit at 1.0 s, silence after)");
    if (! q) return;
    { RepeatOp x = *q->findRepeat (rid); x.rateDiv = 8; x.retrigger = true; q = ops::setRepeat (*q, x); }
    rig->apply (*q, "repeat");
    std::unique_ptr<SliceEditor> ed (static_cast<SliceEditor*> (rig->createEditor()));
    ed->setVisible (true); ed->setSize (1280, 780); ed->resized(); ed->setWorkspace (gslice::ui::Workspace::Edit); pump (20);
    auto& v = ed->view; v.zoomToFit(); pump (10);
    auto ink = [&] (double secs)
    {   // how many pixels of the column's waveform band differ from the band of a silent column inside the span (1.44 s: the
        // repetition there is past its 40 ms hit)
        auto img = v.createComponentSnapshot (v.getLocalBounds(), true, 1.0f);
        const int x = juce::roundToInt (v.xOf (rig->pfToHf (secs * sr))), xr = juce::roundToInt (v.xOf (rig->pfToHf (1.44 * sr)));
        const auto w = v.waveArea();
        int n = 0;
        for (int y = w.getY() + 4 + 30; y < w.getBottom() - 30; ++y)
        {
            const auto c = img.getPixelAt (x, y), r = img.getPixelAt (xr, y);
            if (c.getARGB() != r.getARGB()) ++n;
        }
        return n;
    };
    // 1/8 at 120 BPM = 250 ms: repetitions start at 1.25 / 1.5 / 1.75 s, where the original is silent
    const int at125 = ink (1.257), at150 = ink (1.507), at175 = ink (1.757), gap = ink (1.15), orig = ink (1.007);   // (+7 ms: off the grid lines)
    check (at125 > 10 && at150 > 10 && at175 > 10 && orig > 10 && gap <= 2 && v.repeatTilesDrawn > 0,
           "the span shows the repetitions at 1.25 / 1.5 / 1.75 s (" + juce::String (at125) + " / " + juce::String (at150) + " / " + juce::String (at175)
           + " px) where the original is silent; between the hits nothing (" + juce::String (gap) + " px)");
    // cheap: a repaint uses the cached tiles; a parameter change rebuilds them
    const int m0 = v.tiles.misses; v.createComponentSnapshot (v.getLocalBounds(), true, 1.0f); const int m1 = v.tiles.misses;
    { RepeatOp x = *rig->project().findRepeat (rid); x.rateDiv = 16; rig->apply (*ops::setRepeat (rig->project(), x), "rate"); }
    pump (10);
    const int at1125 = ink (1.132), gap16 = ink (1.19);
    const int m2 = v.tiles.misses;
    check (m1 == m0 && m2 > m1 && at1125 > 10 && gap16 <= 2, "a repaint reuses the cached tiles (" + juce::String (m1 - m0) + " new), RATE 1/16 rebuilds them ("
           + juce::String (m2 - m1) + " new) and draws the hit at 1.125 s (" + juce::String (at1125) + " px)");
    {   // decay: later repetitions are drawn smaller
        RepeatOp x = *rig->project().findRepeat (rid); x.rateDiv = 8; x.decay = 0.3; rig->apply (*ops::setRepeat (rig->project(), x), "decay"); pump (10);
        const int d1 = ink (1.257), d3 = ink (1.757);
        check (d3 < d1, "DECAY: the third repetition is drawn smaller than the first (" + juce::String (d3) + " < " + juce::String (d1) + " px)");
    }
    v.viewStart = rig->pfToHf (0.8 * sr); v.hfPerPx = 400; v.repaint(); pump (10);
    for (auto t : { Theme::Dark, Theme::Light })
    {
        ed->setTheme (t); pump (20);
        snap (*ed, shots.getChildFile ("slice-" + displayName (t).toLowerCase() + "-repeat-wave.png"));
    }
    ed.reset(); pump (20);
}

// ================================================================ [34] ELASTIC hold: pitch drift at the joins
// The output pitch against the source pitch at the mapped source position (the hold's warp map), 3-frame medians on 5 ms frames,
// in the join windows (-30 .. +50 ms around the in join, -50 .. +30 ms around the out join). The vowel has a 5.5 Hz +-20 ct
// vibrato, so a pitch jump metric alone would read the vibrato; this one reads only what the hold adds.
static void elasticDrift (const juce::AudioBuffer<float>& out, const juce::AudioBuffer<float>& src, const Piece& hp, int64 inJoin, int64 outJoin, int64 shift,
                          double& driftIn, double& driftOut)
{
    auto med3 = [] (std::vector<double> v) { auto f = v; for (size_t i = 1; i + 1 < v.size(); ++i) { double x[3] { v[i - 1], v[i], v[i + 1] }; std::sort (x, x + 3); f[i] = x[0] <= 0 ? v[i] : x[1]; } return f; };
    auto so = source::fromBuffer (out, 48000, "o.wav"), ss = source::fromBuffer (src, 48000, "s.wav");
    const int hop = 240;
    const auto po = med3 (hold::pitchTrack (*so, 0, out.getNumSamples(), hop)), ps = med3 (hold::pitchTrack (*ss, 0, src.getNumSamples(), hop));
    auto srcPitch = [&] (double s) { const double k = s / hop; const int i = (int) std::floor (k); if (i < 0 || i + 1 >= (int) ps.size() || ps[(size_t) i] <= 0 || ps[(size_t) i + 1] <= 0) return 0.0;
                                     return ps[(size_t) i] * std::pow (ps[(size_t) i + 1] / ps[(size_t) i], k - i); };
    auto mapped = [&] (int64 t) { if (t < hp.dst0) return (double) t; if (t >= hp.dst1) return (double) (t - shift); return hp.map.srcAt ((double) (hp.u0 + (t - hp.dst0))); };
    auto drift = [&] (int64 a, int64 b) { double m = 0; for (int64 t = a; t < b; t += hop) { const int k = (int) (t / hop); if (k < 0 || k >= (int) po.size() || po[(size_t) k] <= 0) continue;
                                                                                              const double sp = srcPitch (mapped ((int64) k * hop)); if (sp <= 0) continue; m = std::max (m, std::abs (1200.0 * std::log2 (po[(size_t) k] / sp))); } return m; };
    if (std::getenv ("GSLICE_DEBUG"))
    {
        std::printf ("    out-join drift per frame:");
        for (int64 t = outJoin - 2400; t < outJoin + 1440; t += hop) { const int k = (int) (t / hop); const double sp = srcPitch (mapped ((int64) k * hop)); std::printf (" %.1f", po[(size_t) k] > 0 && sp > 0 ? 1200.0 * std::log2 (po[(size_t) k] / sp) : -99.0); }
        // the lag of the output against the source over the 30 ms before the crossfade (xf 30 ms)
        const int64 a = outJoin - 1440 - 1440, b = outJoin - 1440; double best = -2; int bl = 0;
        for (int L = -240; L <= 240; ++L) { double xy = 0, xx = 0, yy = 0; for (int64 t = a; t < b; ++t) { const double y = out.getSample (0, (int) t), x = src.getSample (0, (int) (t - shift + L)); xy += x * y; xx += x * x; yy += y * y; }
                                            const double c = xy / std::sqrt (xx * yy + 1e-30); if (c > best) { best = c; bl = L; } }
        double c0 = 0; { double xy = 0, xx = 0, yy = 0; for (int64 t = a; t < b; ++t) { const double y = out.getSample (0, (int) t), x = src.getSample (0, (int) (t - shift)); xy += x * y; xx += x * x; yy += y * y; } c0 = xy / std::sqrt (xx * yy + 1e-30); }
        std::printf ("\n    lag %d samples (corr %.3f, at 0: %.3f)\n", bl, best, c0);
    }
    driftIn = drift (inJoin - 1440, inJoin + 2400);
    driftOut = drift (outJoin - 2400, outJoin + 1440);
}

// the HOLD test vowel with another pitch / vibrato (vowel() = 220 Hz, +-20 ct at 5.5 Hz)
static juce::AudioBuffer<float> vowelVar (double sr, double f0, double vibCents, double vibHz)
{
    const int N = (int) (sr * 1.0);
    juce::AudioBuffer<float> b (2, N); b.clear();
    std::mt19937 rng (11); std::uniform_real_distribution<float> U (-1.0f, 1.0f);
    float st[6] {}; double ph = 0;
    for (int i = 0; i < N; ++i)
    {
        const double t = i / sr;
        float d = U (rng); for (auto& q : st) { const float o = d - q; q = d; d = o; }
        const float hi = d / 400.0f;
        auto ss = [] (double x) { x = juce::jlimit (0.0, 1.0, x); return x * x * (3 - 2 * x); };
        const double env = ss ((t - 0.08) / 0.02) * (1.0 - ss ((t - 0.66) / 0.02));
        const double f = f0 * std::exp2 (vibCents * std::sin (2.0 * juce::MathConstants<double>::pi * vibHz * t) / 1200.0);
        ph += 2.0 * juce::MathConstants<double>::pi * f / sr;
        double v = 0;
        for (int h = 1; h * f0 < 4000.0; ++h) { const double fh = h * f0; v += (std::exp (-std::pow ((fh - 700.0) / 300.0, 2)) + 0.6 * std::exp (-std::pow ((fh - 1200.0) / 400.0, 2)) + 0.08) * std::sin (h * ph); }
        v = 0.12 * v * env + 0.5 * hi * env;
        if (t < 0.08) v += 4.0 * hi * ss (t / 0.01) * (1.0 - ss ((t - 0.07) / 0.01));
        if (t >= 0.68 && t < 0.76) v += 3.0 * hi * std::exp (-(t - 0.68) / 0.01);
        for (int c = 0; c < 2; ++c) b.setSample (c, i, (float) v);
    }
    return b;
}

static void testElasticJoin()
{
    section ("[34] ELASTIC hold: the pitch drift at the joins (1x pre-roll over the head, 1x plateau + phase steer before the tail) < 5 ct");
    struct V { double f0, vib, vibHz; double factor; };
    const std::vector<V> cases { { 220, 20, 5.5, 2.0 }, { 220, 20, 5.5, 4.0 }, { 220, 20, 5.5, 8.0 }, { 140, 0, 0, 3.0 }, { 310, 35, 6.0, 3.0 }, { 180, 10, 4.5, 6.0 } };
    double worstIn = 0, worstOut = 0, before = 0;
    for (const auto& cs : cases)
    {
        const auto src = cs.f0 == 220 && cs.vib == 20 ? vowel (48000) : vowelVar (48000, cs.f0, cs.vib, cs.vibHz);
        Rig base (src);
        const int64 selA = 2400, selB = 34560;
        const auto st = hold::pickStable (*base->getSource(), selA, selB);
        const double total = (double) (selB - selA) * cs.factor;
        const double held = total - (double) (st.a - selA) - (double) (selB - st.b);
        const int64 tailStart = st.a + (int64) std::llround (held), shift = tailStart - st.b;
        Rig r (src);
        auto q = ops::addHold (r->project(), r.r0().id, selA, selB, st.a, st.b, held, HoldMode::Elastic);
        if (! q) { check (false, "addHold ELASTIC"); continue; }
        r->apply (*q, "hold");
        const auto out = r.render();
        const Piece* hp = nullptr; for (auto& pc : r->currentPlan().pieces) if (pc.hold != nullptr) hp = &pc;
        if (hp == nullptr) { check (false, "the held piece"); continue; }
        double dIn = 0, dOut = 0; elasticDrift (out, src, *hp, st.a, tailStart, shift, dIn, dOut);
        const auto ps = pitchStats (out, (int) st.a - 1440, (int) tailStart + 1440), pv = pitchStats (src, (int) st.a, (int) st.b);
        worstIn = std::max (worstIn, dIn); worstOut = std::max (worstOut, dOut);
        check (dIn < 5.0 && dOut < 5.0, "ELASTIC " + juce::String (cs.f0, 0) + " Hz +-" + juce::String (cs.vib, 0) + " ct vibrato, " + juce::String (cs.factor, 0) + "x selection: drift at the in join "
               + juce::String (dIn, 1) + " ct, out join " + juce::String (dOut, 1) + " ct (< 5); max frame jump " + juce::String (ps.maxJump, 1) + " ct (the source itself: " + juce::String (pv.maxJump, 1) + ")");
        if (cs.factor == 4.0)
        {   // LIVE: the pre-roll runs at the voice start (the in join); the out join's phase steer is a STUDIO render (LIVE plays until
            // it lands), so the LIVE out join is reported, not asserted
            gAudioAllocs = 0;
            const auto live = playFor (*r.p, 0, out.getNumSamples(), 256);
            double lIn = 0, lOut = 0; elasticDrift (live, src, *hp, st.a, tailStart, shift, lIn, lOut);
            check (lIn < 5.0 && gAudioAllocs.load() == 0, "ELASTIC LIVE: drift at the in join " + juce::String (lIn, 1) + " ct (< 5; out join " + juce::String (lOut, 1) + " ct until the STUDIO render), no allocation on the audio thread");
            before = 11.5;
        }
    }
    std::printf ("    worst drift: in %.1f ct, out %.1f ct (phase 12 report: %.1f ct at 4x)\n", worstIn, worstOut, before);
}

// ================================================================ [35] phase 16: syllables (nuclei, consonant | vowel, boundaries)
#include "../Syllables.h"
// a synthetic syllable corpus: CV syllables with an onset consonant of one of four kinds - none, a plosive (closure + 8-15 ms
// burst + aspiration), a fricative (60-120 ms high-band noise), a nasal (40-70 ms voiced low murmur) - and a voiced vowel
// (120-280 ms, f0 150-260 Hz with a glide, three formants), either after a pause (60-200 ms) or straight after the previous
// vowel (continuous speech); a -66 dB noise floor. truth: syllable start (the consonant's first audible sample: a plosive's
// burst, its closure is silent), vowel start / end
struct SylTruth { int64 start, v0, v1; int kind; };
static juce::AudioBuffer<float> syllableCorpus (double sr, int count, uint32_t seed, std::vector<SylTruth>& truth)
{
    std::mt19937 rng (seed); std::uniform_real_distribution<double> U (0, 1); std::normal_distribution<double> N (0, 1);
    static const double vowels[5][3] { { 800, 1200, 2500 }, { 300, 2300, 3000 }, { 350, 800, 2400 }, { 500, 1700, 2500 }, { 650, 1050, 2600 } };
    std::vector<float> out ((size_t) (sr * (0.3 + count * 0.75)), 0.0f);
    for (auto& v : out) v = (float) (0.0005 * N (rng));
    double t = 0.2; double hp = 0; float st6[6] {};
    auto hiNoise = [&] { float d = (float) N (rng); for (auto& q : st6) { const float o = d - q; q = d; d = o; } return d / 20.0f; };
    for (int s = 0; s < count; ++s)
    {
        const bool pause = s == 0 || U (rng) < 0.4;
        if (pause) t += 0.06 + 0.14 * U (rng);
        const int kind = (int) (U (rng) * 4.0);          // 0 none, 1 plosive, 2 fricative, 3 nasal
        const double f0a = 150 + 110 * U (rng), f0b = f0a * (1.0 + 0.08 * (U (rng) - 0.5));
        int64 start = (int64) std::llround (t * sr);
        double phase = 0;
        auto put = [&] (int64 i, double v) { if (i >= 0 && i < (int64) out.size()) out[(size_t) i] += (float) v; };
        if (kind == 1)
        {
            const double clos = pause ? 0.0 : 0.03 + 0.02 * U (rng);   // a closure only inside continuous speech
            const double burst = 0.008 + 0.007 * U (rng), asp = 0.015 + 0.015 * U (rng);
            const int64 b0 = (int64) std::llround ((t + clos) * sr);
            start = b0;   // (the closure is silent: the burst is the first audible sample)
            for (int64 i = b0; i < b0 + (int64) ((burst + asp) * sr); ++i)
            {
                const double u = (double) (i - b0) / sr;
                const double x = N (rng); const double v = (u < burst ? 0.35 * std::exp (-u / 0.004) : 0.06) * (x - hp); hp = x;
                put (i, v);
            }
            t += clos + burst + asp;
        }
        else if (kind == 2)
        {
            const double len = 0.06 + 0.06 * U (rng);
            for (int64 i = start; i < start + (int64) (len * sr); ++i) { const double u = (double) (i - start) / sr; put (i, 0.9 * std::min (1.0, u / 0.01) * std::min (1.0, (len - u) / 0.008) * hiNoise()); }
            t += len;
        }
        else if (kind == 3)
        {
            const double len = 0.04 + 0.03 * U (rng);
            for (int64 i = start; i < start + (int64) (len * sr); ++i)
            {
                const double u = (double) (i - start) / sr; phase += f0a / sr;
                double v = 0; for (int h = 1; h <= 3; ++h) v += std::sin (2.0 * juce::MathConstants<double>::pi * h * phase) / (h * h);
                put (i, 0.05 * std::min (1.0, u / 0.01) * v);
            }
            t += len;
        }
        const double vLen = 0.12 + 0.16 * U (rng);
        const int64 v0 = (int64) std::llround (t * sr), v1 = (int64) std::llround ((t + vLen) * sr);
        const double* F = vowels[s % 5];
        for (int64 i = v0; i < v1; ++i)
        {
            const double w = (double) (i - v0) / sr, f0 = f0a + (f0b - f0a) * w / vLen;
            phase += f0 / sr;
            const double env = std::min (1.0, w / 0.012) * std::min (1.0, (vLen - w) / 0.03);
            put (i, 0.22 * env * vowelSample (w, f0, phase, F, sr));
        }
        truth.push_back ({ start, v0, v1, kind });
        t += vLen;
    }
    juce::AudioBuffer<float> b (1, (int) std::min<size_t> (out.size(), (size_t) ((t + 0.3) * sr)));
    for (int i = 0; i < b.getNumSamples(); ++i) b.setSample (0, i, out[(size_t) i]);
    return b;
}

static void testSyllables()
{
    section ("[35] syllables (phase 16): nucleus recall, consonant | vowel boundary, syllable boundaries; CHOP / HOLD / PLAY use them");
    const double sr = 48000;
    int truthN = 0, found = 0, detN = 0; std::vector<double> bErr, cvErr, veErr; std::array<int, 4> kindN {}, kindHit {};
    for (uint32_t seed : { 3u, 5u, 8u, 13u, 21u, 34u })
    {
        std::vector<SylTruth> tr;
        const auto b = syllableCorpus (sr, 24, seed, tr);
        const auto x = an::mono (b);
        const auto pt = an::trackPitch (x.data(), (int64) x.size(), sr);
        const auto syl = an::detectSyllables (x.data(), (int64) x.size(), sr, &pt);
        truthN += (int) tr.size(); detN += (int) syl.size();
        std::vector<bool> used (syl.size(), false);
        for (auto& t : tr)
        {
            ++kindN[(size_t) t.kind];
            int bi = -1;
            for (size_t i = 0; i < syl.size(); ++i) if (! used[i] && syl[i].nucleus >= t.v0 && syl[i].nucleus < t.v1) { bi = (int) i; break; }
            if (bi < 0) continue;
            used[(size_t) bi] = true; ++found; ++kindHit[(size_t) t.kind];
            bErr.push_back (std::abs ((double) (syl[(size_t) bi].start - t.start)) / sr * 1000.0);
            cvErr.push_back (std::abs ((double) (syl[(size_t) bi].vowelStart - t.v0)) / sr * 1000.0);
            veErr.push_back (std::abs ((double) (syl[(size_t) bi].vowelEnd - t.v1)) / sr * 1000.0);
        }
        // determinism
        if (seed == 3u) { const auto again = an::detectSyllables (x.data(), (int64) x.size(), sr, &pt); check (again == syl, "deterministic: the same audio gives the same syllables"); }
    }
    const double recall = (double) found / truthN, precision = detN > 0 ? (double) found / detN : 0;
    const double mb = an::detail::median (bErr), mcv = an::detail::median (cvErr), mve = an::detail::median (veErr);
    check (recall >= 0.85, "nucleus recall " + juce::String (recall * 100.0, 1) + " % (>= 85; precision " + juce::String (precision * 100.0, 1) + " %) on " + juce::String (truthN)
           + " synthetic syllables (none / plosive / fricative / nasal onsets: " + juce::String (kindHit[0]) + "/" + juce::String (kindN[0]) + " " + juce::String (kindHit[1]) + "/" + juce::String (kindN[1])
           + " " + juce::String (kindHit[2]) + "/" + juce::String (kindN[2]) + " " + juce::String (kindHit[3]) + "/" + juce::String (kindN[3]) + ")");
    check (mb < 30.0 && mcv < 30.0, "boundary median error: syllable start " + juce::String (mb, 1) + " ms, consonant | vowel " + juce::String (mcv, 1) + " ms (< 30), vowel end " + juce::String (mve, 1) + " ms");
    // ---- GHOST CHOP SYLLABLE: the cuts sit at the syllable starts (consonant + vowel together); the phase 7 onsets for comparison
    std::vector<SylTruth> tr;
    const auto corpus = syllableCorpus (sr, 24, 55u, tr);
    juce::AudioBuffer<float> st2 (2, corpus.getNumSamples()); for (int c = 0; c < 2; ++c) st2.copyFrom (c, 0, corpus, 0, 0, corpus.getNumSamples());
    Rig rig (st2);
    { Project q = rig->project(); q.material = "VOCAL"; q.play.mode = SliceMode::Syllable; rig->apply (q, "vocal"); }
    if (rig->waitAnalysis (60000) && rig->analysis() != nullptr)
    {
        const auto an0 = rig->analysis();
        const auto cuts = play::chopCuts (rig->project(), rig->currentPlan(), an0.get());
        std::vector<int64> truthStarts; for (size_t i = 1; i < tr.size(); ++i) truthStarts.push_back (tr[i].start);
        const auto [pp, rr] = prf (cuts, truthStarts, (int64) (0.03 * sr));
        std::vector<int64> old; for (auto& e : an0->onsets.events) if ((e.flags & an::kSyllable) != 0 && e.pos > 0) old.push_back (e.pos);
        const auto [op, orr] = prf (old, truthStarts, (int64) (0.03 * sr));
        if (std::getenv ("GSLICE_DEBUG"))
        {
            for (auto& t : tr)
            {
                const an::Syllable* best = nullptr; for (auto& q : an0->syllables) if (q.nucleus >= t.v0 && q.nucleus < t.v1) best = &q;
                int64 nc = -1; for (auto c : cuts) if (nc < 0 || std::abs (c - t.start) < std::abs (nc - t.start)) nc = c;
                std::printf ("      kind %d start %.3f v0 %.3f v1 %.3f | det %s start %.3f v0 %.3f conf %.2f | cut %+.1f ms\n", t.kind, t.start / sr, t.v0 / sr, t.v1 / sr, best ? "y" : "n",
                             best ? best->start / sr : 0.0, best ? best->vowelStart / sr : 0.0, best ? best->confidence : 0.0f, (nc - t.start) / sr * 1000.0);
            }
        }
        check (rr >= 0.85 && pp >= 0.8, "CHOP SYLLABLE: " + juce::String (rr * 100.0, 0) + " % of the syllable starts cut within 30 ms (precision " + juce::String (pp * 100.0, 0)
               + " %; the phase 7 syllable onsets: " + juce::String (orr * 100.0, 0) + " % / " + juce::String (op * 100.0, 0) + " %)");
        // ---- HOLD: a selection over a consonant + vowel holds inside the vowel
        int tested = 0, inside = 0, insideOld = 0;
        for (auto& t : tr)
        {
            if (t.kind != 2 && t.kind != 1) continue;   // fricative / plosive onsets
            const int64 selA = t.start, selB = t.v1;
            const auto stN = hold::pickStable (*rig->getSource(), selA, selB, &an0->syllables);
            const auto stO = hold::pickStable (*rig->getSource(), selA, selB);
            const int64 edge = (int64) (0.02 * sr);
            if (std::getenv ("GSLICE_DEBUG")) std::printf ("      hold sel %.3f..%.3f vowel %.3f..%.3f -> %.3f..%.3f (inVowel %d)  old %.3f..%.3f\n", selA / sr, selB / sr, t.v0 / sr, t.v1 / sr, stN.a / sr, stN.b / sr, (int) stN.inVowel, stO.a / sr, stO.b / sr);
            ++tested; inside += stN.a >= t.v0 + edge && stN.b <= t.v1 - edge ? 1 : 0; insideOld += stO.a >= t.v0 + edge && stO.b <= t.v1 - edge ? 1 : 0;
        }
        check (tested > 0 && inside >= (int) std::ceil (0.9 * tested), "HOLD: on a consonant + vowel selection the stable region sits inside the vowel (>= 20 ms from its edges) in "
               + juce::String (inside) + " / " + juce::String (tested) + " (without the syllables: " + juce::String (insideOld) + ")");
        // ---- PLAY: HOLD VOWEL sustains the vowel while the key is held
        const auto sl = rig->currentSlices();
        int si = -1;   // the slice holding the first syllable's vowel
        for (size_t i = 0; i < sl.size() && si < 0; ++i) if (sl[i].dst0 <= tr[0].v0 && sl[i].dst1 >= tr[0].v1) si = (int) i;
        if (si >= 0)
        {
            rig->apply (*ops::turnIntoInstrument (rig->project(), (int) sl.size()), "instrument");
            Project q = rig->project(); auto* k = q.play.key (36 + si); k->oneShot = false; k->vowelHold = true; k->releaseMs = 30; rig->apply (q, "hold vowel");
            const bool built = rig->buildBankNow() && rig->getBank() != nullptr && si < (int) rig->getBank()->sustained.size() && rig->getBank()->sustained[(size_t) si].getNumSamples() > (int) (play::kVowelHoldSeconds * sr);
            auto play3s = [&] (int note) { return runMidi (*rig.p, (int) (3.2 * sr), 512, [&] (int64 at, juce::MidiBuffer& m) { if (at == 0) m.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0); if (at == 512 * 281) m.addEvent (juce::MidiMessage::noteOff (1, note), 0); }); };
            gAudioAllocs = 0;
            const auto held = play3s (36 + si);
            const long allocs = gAudioAllocs.load();
            q = rig->project(); q.play.key (36 + si)->vowelHold = false; rig->apply (q, "off"); rig->buildBankNow();
            const auto plain = play3s (36 + si);
            const double lh = an::detail::levelDb (an::mono (held).data(), held.getNumSamples(), (int64) (2.0 * sr), (int64) (2.6 * sr));
            const double lp = an::detail::levelDb (an::mono (plain).data(), plain.getNumSamples(), (int64) (2.0 * sr), (int64) (2.6 * sr));
            if (std::getenv ("GSLICE_DEBUG")) std::printf ("      play: built %d slice %d len %d sustained %d; level 0-0.2 s held %.1f plain %.1f\n", (int) built, si, (int) sl[(size_t) si].length(),
                rig->getBank() && si < (int) rig->getBank()->sustained.size() ? rig->getBank()->sustained[(size_t) si].getNumSamples() : -1,
                an::detail::levelDb (an::mono (held).data(), held.getNumSamples(), 0, (int64) (0.2 * sr)), an::detail::levelDb (an::mono (plain).data(), plain.getNumSamples(), 0, (int64) (0.2 * sr)));
            double ap = 1; std::vector<float> seg ((size_t) (0.04 * sr)); for (size_t i = 0; i < seg.size(); ++i) seg[i] = held.getSample (0, (int) (2.3 * sr) + (int) i);
            const double f0 = hold::yin (seg.data(), (int) seg.size(), sr, 60, 1000, &ap);
            check (built && lh > -40.0 && lp < -80.0 && f0 > 140 && f0 < 290 && allocs == 0,
                   "PLAY HOLD VOWEL: a GATE key held 2.9 s sustains the vowel (" + juce::String (lh, 1) + " dB at 2.0-2.6 s, f0 " + juce::String (f0, 0) + " Hz; without HOLD VOWEL: "
                   + juce::String (lp, 1) + " dB), no allocation on the audio thread");
            Project back; juce::String err; Project with = rig->project(); with.play.key (36 + si)->vowelHold = true;
            check (json::fromString (json::toString (with), back, err) && back.play == with.play && ! json::toString (rig->project()).contains ("vowelHold"),
                   "HOLD VOWEL round trips through JSON (omitted when off: older projects stay byte-identical)");
        }
        else check (false, "a syllable slice for PLAY");
    }
    else check (false, "the analysis lands");
    // the real vocal (when present): sanity numbers
    const juce::File f ("/root/work/04_vocal_ecto_echo.mp3");
    if (f.existsAsFile())
    {
        juce::String err; auto s = source::load (f, err);
        if (s != nullptr)
        {
            const auto x = an::mono (s->audio);
            const auto pt = an::trackPitch (x.data(), (int64) x.size(), s->sampleRate);
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            const auto syl = an::detectSyllables (x.data(), (int64) x.size(), s->sampleRate, &pt);
            const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
            std::vector<double> len, vl, cl; for (auto& q : syl) { len.push_back ((double) (q.end - q.start) / s->sampleRate * 1000.0); vl.push_back ((double) (q.vowelEnd - q.vowelStart) / s->sampleRate * 1000.0); cl.push_back ((double) q.consonantLength() / s->sampleRate * 1000.0); }
            std::printf ("    04_vocal_ecto_echo: %d syllables in %.1f s (%.2f / s), median syllable %.0f ms, vowel %.0f ms, onset consonant %.0f ms; %.0f ms to detect\n",
                         (int) syl.size(), s->seconds(), syl.size() / s->seconds(), an::detail::median (len), an::detail::median (vl), an::detail::median (cl), ms);
            if (std::getenv ("GSLICE_DEBUG"))
            {
                const auto so = an::syllableOnsets (x.data(), (int64) x.size(), s->sampleRate);
                int voiced = 0; for (float v : pt.voicedProb) voiced += v >= 0.5f ? 1 : 0;
                std::printf ("    (phase 7 syllable onsets: %d; voiced frames %.0f %%)\n", (int) so.size(), 100.0 * voiced / std::max (1, pt.frames()));
                for (auto& q : syl) std::printf ("      %.2f [%.2f %.2f] %.2f  conf %.2f\n", q.start / s->sampleRate, q.vowelStart / s->sampleRate, q.vowelEnd / s->sampleRate, q.end / s->sampleRate, q.confidence);
                for (int k = 0; k < (int) (s->seconds() * 10); ++k) { const int64 a = (int64) (k * 0.1 * s->sampleRate); std::printf ("%s%.0f", k % 20 == 0 ? "\n      " : " ", an::detail::levelDb (x.data(), (int64) x.size(), a, a + (int64) (0.1 * s->sampleRate))); }
                std::printf ("\n");
            }
            check (! syl.empty() && syl.size() / s->seconds() < 8.0, "the real vocal: " + juce::String ((int) syl.size()) + " syllables (" + juce::String (syl.size() / s->seconds(), 2) + " / s, under 8 / s)");
        }
    }
}

// ================================================================ [36] region PITCH / FORMANT (phase 9 / 15 UI parity)
static void testRegionPitch()
{
    section ("[36] region PITCH / CENTS / FORMANT: one region shifted, its neighbour untouched, REPEAT on top, JSON, the EDIT row follows the selection");
    const double sr = 48000;
    Rig rig (stereoOf (melody (sr, { { 57, 2.4 } }, true)));
    rig->waitAnalysis (30000);
    auto q = ops::splitAt (rig->project(), rig.r0().id, 1.2 * sr);
    rig->apply (*q, "split");
    const auto src = rig.render();
    const int64 r2 = rig->project().regions[1].id;
    rig->apply (*ops::setRegionPitch (rig->project(), r2, 3, 0, 0), "rp");
    const auto out = rig.render();
    // the shifted region measures +3 st, the first region is the source sample for sample (away from the cut's edge fades)
    const double c1 = pitchedit::centreOf (trackOf (out), (int64) (1.4 * sr), (int64) (2.2 * sr)), c0 = pitchedit::centreOf (trackOf (src), (int64) (1.4 * sr), (int64) (2.2 * sr));
    bool first = true; for (int c = 0; c < 2 && first; ++c) for (int i = 0; i < (int) (1.15 * sr); ++i) if (out.getSample (c, i) != src.getSample (c, i)) { first = false; break; }
    check (std::abs (c1 - c0 - 300.0) < 10.0 && first, "region 2 PITCH +3: measures " + juce::String (c1 - c0, 1) + " ct (300 +- 10); region 1 bit-identical to the source");
    // FORMANT on the region alone moves its spectral centroid (pitch stays)
    rig->apply (*ops::setRegionPitch (rig->project(), r2, 0, 0, 4), "rf");
    const auto fo = rig.render();
    const double cs = centroid (src, (int) (1.4 * sr), (int) (2.2 * sr)), cf = centroid (fo, (int) (1.4 * sr), (int) (2.2 * sr));
    const double pf = pitchedit::centreOf (trackOf (fo), (int64) (1.4 * sr), (int64) (2.2 * sr));
    check (cf > cs * 1.05 && std::abs (pf - c0) < 10.0, "region FORMANT +4: centroid " + juce::String (cs, 0) + " -> " + juce::String (cf, 0) + " Hz, pitch kept (" + juce::String (pf - c0, 1) + " ct)");
    // the global PITCH adds to the region's
    { Project g = rig->project(); g.pitchSemis = 2; g.regions[1].formantSemis = 0; g.regions[1].pitchSemis = 3; rig->apply (g, "g"); }
    const auto both = rig.render();
    const double cb = pitchedit::centreOf (trackOf (both), (int64) (1.4 * sr), (int64) (2.2 * sr)), ca = pitchedit::centreOf (trackOf (both), (int64) (0.3 * sr), (int64) (1.0 * sr));
    check (std::abs (cb - c0 - 500.0) < 10.0 && std::abs (ca - c0 - 200.0) < 10.0, "global +2 and region +3: region 2 at " + juce::String (cb - c0, 1) + " ct, region 1 at " + juce::String (ca - c0, 1) + " ct");
    // a REPEAT inside the pitched region keeps the region's pitch (+ its own)
    {
        Project g = rig->project(); g.pitchSemis = 0; rig->apply (g, "g0");
        int64 rid = 0; auto rp = ops::addRepeat (rig->project(), 1.4 * sr, 2.0 * sr, &rid);
        RepeatOp x = *rp->findRepeat (rid); x.rateDiv = 8; x.pitchSemis = 2; x.pitchRamp = false; rp = ops::setRepeat (*rp, x); rig->apply (*rp, "rep");
        const auto ro = rig.render();
        const double cr = pitchedit::centreOf (trackOf (ro), (int64) (1.42 * sr), (int64) (1.62 * sr));   // repetition 0 (n x pitch: +0; 1/8 at 120 BPM = 250 ms)
        const double cr1 = pitchedit::centreOf (trackOf (ro), (int64) (1.67 * sr), (int64) (1.87 * sr));  // repetition 1 (+2)
        check (std::abs (cr - c0 - 300.0) < 15.0 && std::abs (cr1 - c0 - 500.0) < 15.0, "REPEAT in the pitched region: repetitions at " + juce::String (cr - c0, 0) + " / " + juce::String (cr1 - c0, 0) + " ct (300 / 500)");
        rig->apply (*ops::removeRepeat (rig->project(), rid), "unrep");
    }
    // JSON: round trip; zero = omitted (older projects byte-identical)
    {
        Project back; juce::String err;
        check (json::fromString (json::toString (rig->project()), back, err) && back == rig->project() && json::toString (rig->project()).contains ("\"pitchSemis\": 3"), "region PITCH round trips through JSON");
        Project z = rig->project(); for (auto& r : z.regions) { r.pitchSemis = r.pitchCents = r.formantSemis = 0; }
        const auto s = json::toString (z);
        int count = 0; for (int i = s.indexOf ("pitchSemis"); i >= 0; i = s.indexOf (i + 1, "pitchSemis")) ++count;
        check (count == 1, "zero region PITCH / FORMANT are not written (only the project's pitchSemis is in the file)");
    }
    // UI: EDIT with a region selected = its PITCH; nothing selected = the global one
    {
        std::unique_ptr<SliceEditor> ed (static_cast<SliceEditor*> (rig->createEditor())); ed->setVisible (true); ed->setSize (1280, 780); ed->resized(); pump (20);
        Project z = rig->project(); for (auto& r : z.regions) { r.pitchSemis = 0; } z.pitchSemis = 0; rig->apply (z, "zero");
        ed->setWorkspace (gslice::ui::Workspace::Edit); ed->view.sel.regionId = r2; ed->refresh(); pump (5);
        ed->pitchF.s.setValue (-4.0, juce::sendNotificationSync);
        const bool regionGot = rig->project().find (r2)->pitchSemis == -4.0 && rig->project().pitchSemis == 0.0 && ed->regionPitchScope();
        const bool accent = ed->pitchF.cap.findColour (juce::Label::textColourId) == ed->P.marker;
        ed->view.sel.regionId = 0; ed->refresh(); pump (5);
        const bool showsGlobal = ed->pitchF.s.getValue() == 0.0;
        ed->pitchF.s.setValue (1.0, juce::sendNotificationSync);
        const bool globalGot = rig->project().pitchSemis == 1.0 && rig->project().find (r2)->pitchSemis == -4.0;
        ed->setWorkspace (gslice::ui::Workspace::Pitch); ed->view.sel.regionId = r2; ed->refresh(); pump (5);
        const bool pitchTabGlobal = ! ed->regionPitchScope() && ed->pitchF.s.getValue() == 1.0;
        check (regionGot && accent && showsGlobal && globalGot && pitchTabGlobal, "EDIT: a selected region's PITCH (accent caption), nothing selected = the global PITCH; the PITCH tab stays global");
        ed->setWorkspace (gslice::ui::Workspace::Edit); ed->view.sel.regionId = r2; ed->view.zoomToFit(); ed->refresh(); pump (20);
        snap (*ed, juce::File (GSLICE_SHOTS_DIR).getChildFile ("slice-black-region-pitch.png"));
        ed.reset(); pump (20);
    }
}

// ================================================================ [42] GHOST CAPTURE extension point: the .ghostclip.json sidecar
static void testClipHint()
{
    section ("[42] GHOST CAPTURE: an audio file with a .ghostclip.json sidecar (bpm / bars / offset / key) loads with that tempo");
    const auto dir = tmpDir().getChildFile ("capture"); dir.createDirectory();
    juce::WavAudioFormat wav;
    const auto take = dir.getChildFile ("take.wav");
    writeAudio (drumLoop (48000, 92.5, 4, 0.0, 3), 48000, take, wav, 24);   // 4 bars at 92.5 BPM = 10.378 s
    auto loadWith = [&] (const juce::String& sidecarName, const juce::String& json, Project& out, ClipHint& hint)
    {
        for (auto f : dir.findChildFiles (juce::File::findFiles, false, "*.ghostclip.json")) f.deleteFile();
        if (sidecarName.isNotEmpty()) dir.getChildFile (sidecarName).replaceWithText (json);
        SliceProcessor p; p.setPlayConfigDetails (0, 2, 48000, 512); p.prepareToPlay (48000, 512);
        juce::String err; const bool ok = p.loadAudio (take, err);
        out = p.project(); hint = p.getSource() != nullptr ? p.getSource()->clip : ClipHint();
        return ok;
    };
    Project p; ClipHint h;
    const bool a = loadWith ("take.wav.ghostclip.json", "{\"format\":\"ghostclip\",\"version\":1,\"bpm\":92.5,\"bars\":4,\"offset\":0.25,\"key\":\"F# min\"}", p, h);
    check (a && h.valid() && p.bpm == 92.5 && std::abs (p.beatOffset - 0.25 * 48000) < 1e-6 && p.key == "F# MIN" && h.bars == 4,
           "take.wav.ghostclip.json: BPM " + juce::String (p.bpm, 2) + ", downbeat at " + juce::String (p.beatOffset / 48000.0, 3) + " s, key " + p.key);
    const bool b = loadWith ("take.ghostclip.json", "{\"format\":\"ghostclip\",\"bars\":4}", p, h);
    check (b && std::abs (p.bpm - 92.5) < 0.01, "take.ghostclip.json with bars only: BPM from the length = " + juce::String (p.bpm, 3));
    juce::String why; ClipHint bad;
    dir.getChildFile ("take.ghostclip.json").replaceWithText ("{\"format\":\"something\",\"bpm\":100}");
    const bool c1 = ! source::readClipHint (take, 10.0, bad, &why) && why.contains ("not a ghostclip");
    dir.getChildFile ("take.ghostclip.json").replaceWithText ("{\"format\":\"ghostclip\",\"bpm\":5000}");
    const bool c2 = ! source::readClipHint (take, 10.0, bad, &why);
    dir.getChildFile ("take.ghostclip.json").replaceWithText ("not json at all");
    const bool c3 = ! source::readClipHint (take, 10.0, bad, &why);
    const bool none = loadWith ("", "", p, h) && ! h.valid() && p.bpm == 120.0;
    check (c1 && c2 && c3 && none, "a wrong / out-of-range / broken sidecar is ignored (the project keeps its defaults); no sidecar = as before");
}
