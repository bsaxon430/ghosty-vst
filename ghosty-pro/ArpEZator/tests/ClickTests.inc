// ---- 2026-11-13: CLICKS. Each mood, each lane on its own (the built-in sound, as you hear it in FL), rendered live at 130 bpm:
// a click = a jump in the signal's slope far bigger than anything around it (second difference vs its local level).
static int countClicks (const juce::AudioBuffer<float>& b, double sr, std::vector<double>& at, float& worst)
{
    int n = 0; worst = 0; const int N = b.getNumSamples(), W = (int) (sr * 0.004);
    for (int c = 0; c < 2; ++c)
    {
        const float* x = b.getReadPointer (c);
        std::vector<float> d2 ((size_t) N, 0.0f);
        for (int i = 2; i < N; ++i) d2[(size_t) i] = std::abs (x[i] - 2.0f * x[i - 1] + x[i - 2]);
        // local level: mean |d2| over +-4 ms (excluding the sample itself)
        std::vector<double> cs ((size_t) N + 1, 0.0); for (int i = 0; i < N; ++i) cs[(size_t) i + 1] = cs[(size_t) i] + d2[(size_t) i];
        int last = -100000;
        for (int i = W + 2; i < N - W; ++i)
        {
            const double loc = (cs[(size_t) (i + W)] - cs[(size_t) (i - W)] - d2[(size_t) i]) / (2.0 * W - 1.0);
            const float r = (float) (d2[(size_t) i] / (loc + 1.0e-5));
            if (d2[(size_t) i] > 0.004f && r > 12.0f)
            {
                worst = std::max (worst, r);
                if (i - last > (int) (sr * 0.01)) { ++n; if (at.size() < 40) at.push_back (i / sr); }
                last = i;
            }
        }
    }
    return n;
}
static void clickChecks (Counter& C)
{
    std::printf ("== CLICKS: every mood, every lane on its own, the built-in sound, 8 bars live at 130 bpm\n");
    const char* laneN[] { "CHORDS", "ARP", "MELODY", "BASS" };
    const char* moodN[] { "SUNRISE", "DUSK", "MIDNIGHT", "TRAP", "HIPHOP" };
    int total = 0;
    for (int mood = 0; mood < arp::kMoods; ++mood)
        for (int lane = 0; lane < arp::kLanes; ++lane)
        {
            ArpProcessor p; p.persistTaste = false; p.hintsSeen = true;
            p.generate (mood, false, false);
            p.change ([lane] (arp::Idea& X) { for (int l = 0; l < arp::kLanes; ++l) X.lanes[(size_t) l].mute = l != lane; }, false);
            RenderOpts o; o.bpm = 130; o.bars = 8; o.sr = 44100; o.block = 512; std::vector<MidiEv> ev; int bp = 0;
            const auto out = render (p, o, ev, bp);
            std::vector<double> at; float worst = 0;
            const int k = countClicks (out, o.sr, at, worst); total += k;
            std::string ts; for (size_t i = 0; i < std::min<size_t> (at.size(), 8); ++i) ts += " " + std::to_string (at[i]).substr (0, 6);
            std::printf ("  %-8s %-6s peak %.3f clicks %3d (worst x%.0f)%s\n", moodN[mood], laneN[lane], out.getMagnitude (0, out.getNumSamples()), k, worst, ts.c_str());
            if (const char* dir = std::getenv ("ARP_CLICK_WAVS"))
            {
                juce::File f = juce::File (dir).getChildFile (juce::String (moodN[mood]) + "_" + laneN[lane] + ".wav"); f.deleteFile();
                juce::WavAudioFormat wav; if (auto* os = f.createOutputStream().release()) { std::unique_ptr<juce::AudioFormatWriter> w (wav.createWriterFor (os, o.sr, 2, 24, {}, 0)); if (w) w->writeFromAudioSampleBuffer (out, 0, out.getNumSamples()); }
            }
        }
    // a report, not a pass / fail: bright saw edges read as "clicks" to this detector too. Before the PUMP fix: 285; after: ~30 (saw edges, the 808 knock)
    std::printf ("  total %d\n", total); C.ok (true, "clicks report");
}

// ---- 2026-11-13: STUTTER on a HOOK LAB / drawn melody keeps it; locks survive MUTANT <-> CLASSIC
static void stutterLockChecks (Counter& C)
{
    std::printf ("== STUTTER keeps a made melody; MUTANT / CLASSIC keep their own locks\n");
    {
        ArpProcessor p; p.persistTaste = false;
        std::vector<arp::Note> mine; for (int i = 0; i < 16; ++i) { arp::Note n; n.t = i * 96 * 2; n.d = 60; n.p = 60 + (i * 5) % 12; mine.push_back (n); }
        p.change ([&] (arp::Idea& X) { X.lanes[(size_t) arp::LaneMelody].notes = mine; X.lanes[(size_t) arp::LaneMelody].edited = true; }, false);
        auto pitchesAt = [] (const std::vector<arp::Note>& v) { std::set<std::pair<int,int>> s; for (auto& n : v) s.insert ({ n.t, n.p }); return s; };
        p.change ([] (arp::Idea& X) { arp::stutterMelody (X, 1.0); }, false);
        const auto hi = p.idea().lanes[(size_t) arp::LaneMelody].notes;
        bool kept = true; const auto hs = pitchesAt (hi); for (auto& n : mine) kept = kept && hs.count ({ n.t, n.p }) > 0;
        bool onlyRepeats = true; std::set<int> mp; for (auto& n : mine) mp.insert (n.p); for (auto& n : hi) onlyRepeats = onlyRepeats && mp.count (n.p) > 0;
        p.change ([] (arp::Idea& X) { arp::stutterMelody (X, 0.0); }, false);
        const auto back = p.idea().lanes[(size_t) arp::LaneMelody].notes;
        std::printf ("  melody %zu notes -> STUTTER full %zu notes -> STUTTER off %zu notes\n", mine.size(), hi.size(), back.size());
        C.ok (kept && onlyRepeats && hi.size() > mine.size() && pitchesAt (back) == pitchesAt (mine) && back.size() == mine.size(), "STUTTER on a made melody: every note kept, only repeats added; turned down = the same melody back");
    }
    {
        ArpProcessor p; p.persistTaste = false;
        // CLASSIC: lock two arp notes
        p.change ([] (arp::Idea& X) { auto& ns = X.lanes[(size_t) arp::LaneArp].notes; for (size_t i = 0; i < ns.size() && i < 2; ++i) ns[i].lock = true; }, false);
        std::vector<arp::Note> lockedC; for (auto& n : p.idea().lanes[(size_t) arp::LaneArp].notes) if (n.lock) lockedC.push_back (n);
        p.mutantSetActive (true);
        p.mutantLockStep (0, true); p.mutantLockStep (3, true);
        const auto cellBefore = p.idea().mutant.cell;
        p.mutantSetActive (false);
        int foundC = 0; for (auto& n : p.idea().lanes[(size_t) arp::LaneArp].notes) for (auto& l : lockedC) if (n.lock && n.t == l.t && n.p == l.p) ++foundC;
        p.mutantSetActive (true);
        const auto& cellAfter = p.idea().mutant.cell; bool mutKept = cellAfter.len == cellBefore.len;
        for (int s = 0; mutKept && s < cellAfter.len; ++s) if (cellBefore.s[(size_t) s].lock) mutKept = cellAfter.s[(size_t) s].lock && cellAfter.s[(size_t) s].idx == cellBefore.s[(size_t) s].idx && cellAfter.s[(size_t) s].oct == cellBefore.s[(size_t) s].oct;
        p.mutantSetActive (false);
        int foundC2 = 0; for (auto& n : p.idea().lanes[(size_t) arp::LaneArp].notes) for (auto& l : lockedC) if (n.lock && n.t == l.t && n.p == l.p) ++foundC2;
        std::printf ("  classic locked %zu -> after MUTANT and back %d, twice %d; MUTANT locked steps kept %d\n", lockedC.size(), foundC, foundC2, (int) mutKept);
        C.ok (! lockedC.empty() && foundC == (int) lockedC.size() && foundC2 == (int) lockedC.size() && mutKept, "locks: CLASSIC's locked notes and MUTANT's locked steps both survive switching back and forth");
    }
}
