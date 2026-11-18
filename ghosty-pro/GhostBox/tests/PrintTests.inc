// GHOSTPRINT tests (GhostBox/BoxPrint.h + BoxProcessor print*): synthetic references made here (a pumping 1/4-note sidechain envelope, an
// 8th-note gate pattern over 2 bars, a swept filter, a left-right pan), the extracted gesture against what was put in (envelope correlation
// >= 0.95, loop length, accents on the right steps, brightness / pan curves), the pattern applied to a sustained chord pulses like the
// reference (output envelope correlation >= 0.9), the controls, undo, state round trip, no audio-thread allocation, files (WAV / MP3 /
// bad files give a message, never a crash).
//   GhostBoxTests print          -> these checks
//   GhostBoxTests printui [dir]  -> the PRINT page (entry points, tooltips, contrast in 4 looks, screenshots)

static std::shared_ptr<juce::AudioBuffer<float>> toBuffer (const Stereo& s)
{
    auto b = std::make_shared<juce::AudioBuffer<float>> (2, (int) s.size());
    std::copy (s.l.begin(), s.l.end(), b->getWritePointer (0)); std::copy (s.r.begin(), s.r.end(), b->getWritePointer (1));
    return b;
}
static Stereo chordTone (double secs, float amp = 0.15f)
{   // a sustained chord with a little air (broadband enough for the flux)
    Stereo s; s.resize (S (secs)); uint32_t r = 99;
    for (size_t i = 0; i < s.size(); ++i)
    {
        const double t = (double) i / SR; r = r * 1664525u + 1013904223u; const float nz = ((float) (r >> 8) / 8388608.0f - 1.0f) * 0.05f;
        const float v = (float) (std::sin (2 * kPi * 220.0 * t) + 0.8 * std::sin (2 * kPi * 277.18 * t) + 0.7 * std::sin (2 * kPi * 329.63 * t) + 0.3 * std::sin (2 * kPi * 440.0 * t)) * 0.3f + nz;
        s.l[i] = s.r[i] = v * amp * 2.0f;
    }
    return s;
}
// per-1/16 RMS of a signal (bpm), as the expected envelope
static std::vector<float> stepRms (const Stereo& s, double bpm, int steps, size_t from = 0)
{
    const double st = 60.0 / bpm / 4.0; std::vector<float> v;
    for (int k = 0; k < steps; ++k)
    {
        const size_t a = from + (size_t) (k * st * SR), b = from + (size_t) ((k + 1) * st * SR);
        double e = 0; for (size_t i = a; i < b && i < s.size(); ++i) e += 0.5 * ((double) s.l[i] * s.l[i] + (double) s.r[i] * s.r[i]);
        v.push_back ((float) std::sqrt (e / (double) std::max<size_t> (1, b - a)));
    }
    return v;
}
static float corrOf (const std::vector<float>& a, const std::vector<float>& b) { const int n = (int) std::min (a.size(), b.size()); return boxprint::pearson (a.data(), b.data(), n); }
static double pumpGain (double t, double bpm)
{   // the classic sidechain duck: down on every beat, back up over the beat
    const double beat = 60.0 / bpm, tb = std::fmod (t, beat);
    return 0.08 + 0.92 * (1.0 - std::exp (-tb / (0.22 * beat)));
}
static Stereo pumpRef (double bpm, int bars)
{
    Stereo s = chordTone (bars * 4 * 60.0 / bpm);
    for (size_t i = 0; i < s.size(); ++i) { const float g = (float) pumpGain ((double) i / SR, bpm); s.l[i] *= g; s.r[i] *= g; }
    return s;
}
static const int kGateOpen[2][16] { { 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0 }, { 1, 0, 1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0 } };
static Stereo gateRef (double bpm, int bars)
{   // 8th notes: each opens for one 1/16 (1 ms edges); bar 2 of every pair leaves out two of them (a 2-bar pattern)
    Stereo s = mixOf (chordTone (bars * 4 * 60.0 / bpm), noise (bars * 4 * 60.0 / bpm, 0.08f));
    const double st = 60.0 / bpm / 4.0; const double edge = 0.001;
    for (size_t i = 0; i < s.size(); ++i)
    {
        const double t = (double) i / SR; const int step = (int) (t / st); const double in = t - step * st;
        const int open = kGateOpen[(step / 16) % 2][step % 16];
        float g = 0.0f;
        if (open) g = (float) std::min ({ 1.0, in / edge, (st - in) / edge });
        s.l[i] *= g; s.r[i] *= g;
    }
    return s;
}
static Stereo sweepRef (double bpm, int bars, std::vector<float>* logHz)
{   // noise through a 2-pole low-pass: 300 Hz -> 8 kHz over a bar, back over the next (log)
    Stereo s; s.resize (S (bars * 4 * 60.0 / bpm)); uint32_t r = 7; float ic1 = 0, ic2 = 0;
    const double bar = 4 * 60.0 / bpm;
    for (size_t i = 0; i < s.size(); ++i)
    {
        const double t = (double) i / SR, ph = std::fmod (t / bar, 2.0), u = ph < 1.0 ? ph : 2.0 - ph;
        const double hz = 300.0 * std::pow (8000.0 / 300.0, u);
        r = r * 1664525u + 1013904223u; const float x = ((float) (r >> 8) / 8388608.0f - 1.0f) * 0.4f;
        const float g = (float) std::tan (kPi * hz / SR), k = 1.0f, a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3; ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
        s.l[i] = s.r[i] = v2;
    }
    if (logHz) { logHz->clear(); const double st = 60.0 / bpm / 4.0; for (int k = 0; k < 32; ++k) { const double t = (k + 0.5) * st, ph = std::fmod (t / bar, 2.0), u = ph < 1.0 ? ph : 2.0 - ph; logHz->push_back ((float) u); } }
    return s;
}
static Stereo panRef (double bpm, int bars, std::vector<float>* bal)
{   // mono noise, constant-power pan L -> R -> L over 2 bars
    Stereo s; s.resize (S (bars * 4 * 60.0 / bpm)); uint32_t r = 3; const double per = 2 * 4 * 60.0 / bpm;
    for (size_t i = 0; i < s.size(); ++i)
    {
        const double t = (double) i / SR, p = -std::cos (2 * kPi * t / per);   // -1 (left) at the start
        r = r * 1664525u + 1013904223u; const float x = ((float) (r >> 8) / 8388608.0f - 1.0f) * 0.3f;
        const double a = (p + 1) * 0.25 * kPi; s.l[i] = x * (float) std::cos (a); s.r[i] = x * (float) std::sin (a);
    }
    if (bal) { bal->clear(); const double st = 60.0 / bpm / 4.0; for (int k = 0; k < 32; ++k) { double e = 0; for (int q = 0; q < 16; ++q) { const double t = (k + (q + 0.5) / 16.0) * st, p = -std::cos (2 * kPi * t / per), a = (p + 1) * 0.25 * kPi; e += std::sin (a) * std::sin (a) - std::cos (a) * std::cos (a); } bal->push_back ((float) (e / 16.0)); } }
    return s;
}
static boxprint::Analysis analyseOn (BoxProcessor& p, const Stereo& s, const juce::String& name, boxprint::Options o = {})
{
    p.printLoadBuffer (toBuffer (s), SR, name, o); p.printWait();
    return p.printResult;
}

static void testGhostPrint()
{
    std::printf ("\n[38] GHOSTPRINT: pump / gate / sweep / pan references, apply to a chord, controls, undo, state, allocations, files\n");
    // ---- the pump: tempo detected, 1 bar, the envelope follows the duck
    {
        Proc p (false);
        const auto ref = pumpRef (120.0, 4);
        const auto A = analyseOn (*p, ref, "pump");
        auto expect = stepRms (ref, 120.0, 16, S (2.0));   // bar 2 (they are all the same)
        float mx = 0; for (float v : expect) mx = std::max (mx, v); for (auto& v : expect) v /= mx;
        const float c = A.ok ? corrOf (A.env, expect) : 0.0f;
        std::printf ("    pump: %s | envelope correlation %.3f (>= 0.95), loop %d bar (1)\n", A.message.toRawUTF8(), c, A.loopBars);
        check (A.ok && A.bpmDetected && std::abs (A.bpm - 120.0) < 0.5 && A.loopBars == 1 && c >= 0.95f, "GHOSTPRINT pump: tempo, loop, envelope");
        // at 97 BPM too (detected, snapped to the 4 bars the file is)
        Proc q (false); const auto A2 = analyseOn (*q, pumpRef (97.0, 4), "pump 97");
        std::printf ("    pump at 97 BPM: %s\n", A2.message.toRawUTF8());
        check (A2.ok && std::abs (A2.bpm - 97.0) < 0.5 && A2.loopBars == 1, "GHOSTPRINT tempo detection at 97 BPM");
    }
    // ---- the 8th-note gate (a 2-bar pattern): loop 2 bars, accents exactly on the open steps
    {
        Proc p (false);
        const auto ref = gateRef (120.0, 4);
        const auto A = analyseOn (*p, ref, "gate");
        int right = 0, wrong = 0, missed = 0;
        if (A.ok && A.steps == 32)
            for (int j = 0; j < 32; ++j) { const bool want = kGateOpen[j / 16][j % 16] != 0, got = A.accent[(size_t) j] > 0; if (want && got) ++right; else if (got) ++wrong; else if (want) ++missed; }
        auto expect = stepRms (ref, 120.0, 32); float mx = 0; for (float v : expect) mx = std::max (mx, v); for (auto& v : expect) v /= mx;
        const float c = A.ok ? corrOf (A.env, expect) : 0.0f;
        std::printf ("    gate: %s | loop %d bars (2), accents right %d / 14, extra %d, missed %d, envelope correlation %.3f\n", A.message.toRawUTF8(), A.loopBars, right, wrong, missed, c);
        check (A.ok && A.loopBars == 2 && right == 14 && wrong == 0 && missed == 0 && c >= 0.95f, "GHOSTPRINT gate: loop length, accents on the right steps, envelope");
        // RHYTHM-ONLY: the volume lane is a gate opening on those steps
        boxprint::Settings st; st.rhythmOnly = true; const auto P = boxprint::makePattern (A, st);
        int evOk = 0;
        if (! P.lanes.empty()) for (auto& e : P.lanes[0].events) { const int j = (int) std::lround (e.pos / 0.25); if (j >= 0 && j < 32 && kGateOpen[j / 16][j % 16]) ++evOk; }
        std::printf ("    RHYTHM-ONLY: %d gate events on the open steps (14), base closed %d\n", evOk, (int) (! P.lanes.empty() && P.lanes[0].vals[1] == -1.0f));
        check (! P.lanes.empty() && P.lanes[0].id == "fx_print_vol" && evOk == 14 && (int) P.lanes[0].events.size() == 14, "GHOSTPRINT RHYTHM-ONLY gate");
    }
    // ---- the swept filter: brightness follows the sweep (2-bar loop)
    {
        Proc p (false); std::vector<float> u;
        const auto A = analyseOn (*p, sweepRef (120.0, 4, &u), "sweep", [] { boxprint::Options o; o.bpm = 120.0; return o; }());
        const float c = A.ok ? corrOf (A.bright, u) : 0.0f;
        std::printf ("    swept filter: %s | brightness correlation %.3f (>= 0.95), loop %d bars (2)\n", A.message.toRawUTF8(), c, A.loopBars);
        check (A.ok && A.loopBars == 2 && A.brightMoves && c >= 0.95f, "GHOSTPRINT swept filter: brightness curve, loop");
    }
    // ---- the pan: the balance follows it (2-bar loop), the width barely moves, the level is flat
    {
        Proc p (false); std::vector<float> b;
        const auto A = analyseOn (*p, panRef (120.0, 4, &b), "pan", [] { boxprint::Options o; o.bars = 4; return o; }());
        const float c = A.ok ? corrOf (A.bal, b) : 0.0f;
        std::printf ("    left-right pan: %s | balance correlation %.3f (>= 0.95), loop %d bars (2), envelope range %.1f dB\n", A.message.toRawUTF8(), c, A.loopBars, A.envRangeDb);
        check (A.ok && A.loopBars == 2 && A.panMoves && c >= 0.95f && std::abs (A.bpm - 120.0) < 0.01, "GHOSTPRINT pan: balance curve, loop, BARS field");
        boxprint::Settings st; st.dest = { 0, 0, 0, 2 }; const auto P = boxprint::makePattern (A, st);   // STEREO -> SPACE PAN
        check (P.lanes.size() == 1 && P.lanes[0].id == "fx_gspace_x" && P.lanes[0].steps == false && std::abs (P.lanes[0].vals[0] + 0.5f) < 0.1f, "GHOSTPRINT stereo -> SPACE PAN lane");
    }
    // ---- APPLY the pump to a sustained chord: the output pulses like the reference
    {
        Proc p (false);
        const auto ref = pumpRef (120.0, 4);
        analyseOn (*p, ref, "pump");
        p->printSet = {}; p->printSet.intensity = 1.0f; p->printSet.dest = { 1, 0, 0, 0 };
        const int n = p->printApply();
        auto& tl = p.impl().box.timeline;
        const bool laneOk = n == 1 && tl.lanes.size() == 1 && tl.lanes[0].id == "fx_print_vol" && tl.bars == 1 && tl.lanes[0].mode == 0 && tl.lanes[0].shape.n == 16;
        const auto chord = chordTone (8.0);
        const long a0 = gAudioAllocs.load();
        p.head.ppq = 0;
        const auto out = run (p, chord);
        const long allocs = gAudioAllocs.load() - a0;
        const int lat = p->getLatencySamples();
        Stereo outAligned = slice (out, (size_t) lat, out.size());
        auto oe = stepRms (outAligned, 120.0, 32, S (2.0)), re = stepRms (ref, 120.0, 32, S (2.0));
        const float c = corrOf (oe, re);
        float dOut = 0, dRef = 0; { float a = 1e9f, b = 0; for (float v : oe) { a = std::min (a, v); b = std::max (b, v); } dOut = gainToDb (b / std::max (1e-9f, a)); a = 1e9f; b = 0; for (float v : re) { a = std::min (a, v); b = std::max (b, v); } dRef = gainToDb (b / std::max (1e-9f, a)); }
        std::printf ("    applied to a chord: %d lane (%s), output pulse vs the reference: correlation %.3f (>= 0.9), depth %.1f dB (reference %.1f dB), audio allocations %ld\n",
                     n, laneOk ? "VOLUME, CURVE 16 points, 1 bar" : "WRONG", c, dOut, dRef, allocs);
        check (laneOk && c >= 0.9f && allocs == 0, "GHOSTPRINT pattern applied to a chord pulses like the reference");
        // state round trip: the lanes are part of the sound
        juce::MemoryBlock mb; p->getStateInformation (mb);
        Proc q (false); q->setStateInformation (mb.getData(), (int) mb.getSize()); q.pump (40);
        const auto& t2 = q.impl().box.timeline;
        const bool same = t2.lanes.size() == 1 && t2.lanes[0].id == "fx_print_vol" && q.impl().box.tlShapes[0].get().sameAs (p.impl().box.tlShapes[0].get()) && t2.bars == 1;
        p.head.ppq = q.head.ppq = 0; p->rack.reset(); q->rack.reset();
        const auto o1 = run (p, slice (chord, 0, S (3.0))), o2 = run (q, slice (chord, 0, S (3.0)));
        std::printf ("    state round trip: lanes %d, rendered the same (max diff %.3g)\n", (int) same, maxDiff (o1, o2, S (0.5)));
        check (same && maxDiff (o1, o2, S (0.5)) < 1.0e-4f, "GHOSTPRINT lanes survive a state round trip");
        // UNDO takes the APPLY back in one step
        const bool undone = p.impl().undo() && p.impl().box.timeline.lanes.empty();
        std::printf ("    UNDO after APPLY: lanes gone %d\n", (int) undone);
        check (undone, "GHOSTPRINT APPLY is one UNDO step");
    }
    // ---- the controls: INTENSITY = depth, SMOOTH rounds, INVERT flips, four curves -> four lanes (64 STEPS for a 4-bar loop)
    {
        Proc p (false);
        const auto A = analyseOn (*p, pumpRef (120.0, 4), "pump");
        boxprint::Settings s1; s1.intensity = 0.5f; const auto P1 = boxprint::makePattern (A, s1);
        boxprint::Settings s2; s2.invert = true; const auto P2 = boxprint::makePattern (A, s2);
        boxprint::Settings s3; s3.smooth = 1.0f; const auto P3 = boxprint::makePattern (A, s3);
        auto var = [] (const std::vector<float>& v) { double m = 0, q = 0; for (float x : v) m += x; m /= (double) v.size(); for (float x : v) q += (x - m) * (x - m); return q / (double) v.size(); };
        const bool ok = ! P1.lanes.empty() && std::abs (P1.lanes[0].depth - 0.5f) < 1e-6f && ! P2.lanes.empty() && std::abs (P2.lanes[0].vals[3] + A.env[3]) < 1e-5f
                        && ! P3.lanes.empty() && var (P3.lanes[0].vals) < var (P1.lanes[0].vals);
        // a 4-bar loop (a pattern that does not repeat inside 4 bars) -> 64 STEPS
        Stereo r4 = chordTone (8.0); for (size_t i = 0; i < r4.size(); ++i) { const double t = (double) i / SR; const float g = (float) (0.2 + 0.8 * (0.5 + 0.5 * std::sin (2 * kPi * t / 8.0 - kPi / 2))); r4.l[i] *= g; r4.r[i] *= g; }
        const auto A4 = analyseOn (*p, r4, "slow swell", [] { boxprint::Options o; o.bpm = 120.0; return o; }());
        const auto P4 = boxprint::makePattern (A4, {});
        p->printApply();
        const auto& tl = p.impl().box.timeline;
        const bool steps64 = A4.loopBars == 4 && ! P4.lanes.empty() && P4.lanes[0].steps && tl.bars == 4 && ! tl.lanes.empty() && tl.lanes[0].mode == 1 && tl.lanes[0].stepsIdx == 3;
        std::printf ("    controls: INTENSITY / INVERT / SMOOTH %d | a 4-bar swell -> loop %d bars, 64 STEPS lane %d\n", (int) ok, A4.loopBars, (int) steps64);
        check (ok && steps64, "GHOSTPRINT controls and 64-step lanes");
    }
    // ---- files: WAV and MP3 read like the buffer; bad files give a message, never a crash; the analysis runs off the message thread
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("gbprint"); dir.deleteRecursively(); dir.createDirectory();
        const auto ref = gateRef (120.0, 4);
        writeWav (dir.getChildFile ("gate.wav"), ref);
        Proc p (false);
        p->printLoadFile (dir.getChildFile ("gate.wav"));
        const bool async = p->printBusy.load();
        p->printWait();
        const auto Aw = p->printResult;
        bool mp3 = false; juce::String mp3Note = "ffmpeg missing: MP3 skipped";
        if (std::system (("ffmpeg -y -loglevel error -i " + dir.getChildFile ("gate.wav").getFullPathName().quoted() + " -b:a 192k " + dir.getChildFile ("gate.mp3").getFullPathName().quoted() + " 2>/dev/null").toRawUTF8()) == 0)
        {
            p->printLoadFile (dir.getChildFile ("gate.mp3")); p->printWait();
            mp3 = p->printResult.ok && p->printResult.loopBars == 2; mp3Note = "MP3 " + juce::String (mp3 ? "read: " : "FAILED: ") + p->printMessage + " (" + juce::String (p->printResult.secs, 3) + " s, " + juce::String ((int) p->printResult.sr) + " Hz)";
        }
        else mp3 = true;
        // bad inputs
        dir.getChildFile ("garbage.wav").replaceWithText ("RIFF this is not a wave file at all");
        dir.getChildFile ("notes.mp3").replaceWithText ("hello");
        dir.getChildFile ("empty.flac").create();
        { Stereo tiny; tiny.resize (S (0.1)); for (size_t i = 0; i < tiny.size(); ++i) tiny.l[i] = tiny.r[i] = 0.3f * std::sin ((float) i * 0.05f); writeWav (dir.getChildFile ("tiny.wav"), tiny); }
        { Stereo sil; sil.resize (S (2.0)); writeWav (dir.getChildFile ("silent.wav"), sil); }
        juce::String msgs; int handled = 0;
        for (auto* nm : { "garbage.wav", "notes.mp3", "empty.flac", "tiny.wav", "silent.wav", "missing.aiff" })
        {
            p->printLoadFile (dir.getChildFile (nm)); p->printWait();
            if (! p->printResult.ok && p->printMessage.isNotEmpty()) ++handled;
            msgs << "\n      " << nm << ": " << p->printMessage;
        }
        const int lanes = p->printApply();   // (nothing to apply after a bad file)
        std::printf ("    WAV read off the message thread %d, same gesture as the buffer %d | %s | bad inputs with a message %d / 6 (APPLY then writes %d lanes):%s\n",
                     (int) async, (int) (Aw.ok && Aw.loopBars == 2 && Aw.steps == 32), mp3Note.toRawUTF8(), handled, lanes, msgs.toRawUTF8());
        check (async && Aw.ok && Aw.loopBars == 2 && mp3 && handled == 6 && lanes == 0, "GHOSTPRINT files: WAV / MP3 read, bad files give a message");
        if (! getenv ("GBPRINT_KEEP")) dir.deleteRecursively();
    }
}
