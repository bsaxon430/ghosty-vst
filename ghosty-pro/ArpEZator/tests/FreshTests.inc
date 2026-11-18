// ArpEZator (2026-11-07): the size the window opens at, and a fresh start every time. Included from ArpTests.cpp (--fresh).
//   size   opens at about 1200 x 760 (never wider than 1240, never taller than 780), resizable, the whole picture inside the window at
//          every size, the controls on the first page do not overlap
//   fresh  two instances in one process: changing the first leaves the second at factory defaults; destroy + recreate = defaults;
//          nothing learned in one instance (HOOK LAB's taste) reaches another; a new instance makes sound
static juce::String arpStateOf (ArpProcessor& p) { juce::MemoryBlock mb; p.getStateInformation (mb); return mb.toBase64Encoding(); }

static void freshChecks (Counter& C)
{
    std::printf ("== size: opens at about 1200 x 760 (at most 1240 x 780), resizable, nothing off the edge or overlapping\n");
    {
        ArpProcessor p; p.hintsSeen = true;
        std::unique_ptr<arpui::ArpEditor> ed (dynamic_cast<arpui::ArpEditor*> (p.createEditor()));
        juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        const int w = ed->getWidth(), h = ed->getHeight();
        if (auto* d = std::getenv ("ARP_SHOTS")) { juce::File dir (d); dir.createDirectory(); const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::FileOutputStream os (dir.getChildFile ("arpezator-open-size.png")); if (os.openedOk()) { os.setPosition (0); os.truncate(); juce::PNGImageFormat().writeImageToStream (img, os); } }
        C.ok (w <= 1240 && h <= 780 && w >= 1100 && h >= 720, "opens at " + std::to_string (w) + " x " + std::to_string (h));
        C.ok (ed->isResizable(), "resizable");
        juce::Component* content = ed->getNumChildComponents() > 0 ? ed->getChildComponent (0) : nullptr;
        C.ok (content != nullptr && content->getWidth() == 1240, "the design canvas is the editor's first child");
        if (content != nullptr)
        {
            for (auto sz : { juce::Point<int> (w, h), juce::Point<int> (1240, 780), juce::Point<int> (1200, 760), juce::Point<int> (744, 492), juce::Point<int> (1600, 1058), juce::Point<int> (2480, 1640) })
            {
                ed->setSize (sz.x, sz.y); juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
                const auto r = content->getBoundsInParent().toFloat(); const auto win = ed->getLocalBounds().toFloat().expanded (1.0f);
                C.ok (win.contains (r), "at " + std::to_string (ed->getWidth()) + " x " + std::to_string (ed->getHeight()) + " the whole picture is inside the window");
            }
            ed->setSize (w, h); juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
            // the first page's controls (the small ones: buttons, pills, knobs) do not overlap each other
            std::vector<juce::Component*> cs;
            for (auto* c : content->getChildren())
                if (c->isVisible() && ! c->getBounds().isEmpty() && c->getWidth() < 400 && c->getHeight() < 120 && c->getName() != "hints") cs.push_back (c);
            int clash = 0; std::string which;
            for (size_t i = 0; i < cs.size(); ++i)
                for (size_t j = i + 1; j < cs.size(); ++j)
                    if (cs[i]->getBounds().intersects (cs[j]->getBounds()))
                    {
                        ++clash;
                        if (clash <= 8) which += " [" + cs[i]->getBounds().toString().toStdString() + " " + cs[i]->getName().toStdString() + " / " + cs[j]->getBounds().toString().toStdString() + " " + cs[j]->getName().toStdString() + "]";
                    }
            std::printf ("  %dx%d: %zu controls on the first page, %d overlapping\n", w, h, cs.size(), clash);
            C.ok (clash == 0 && cs.size() > 20, "the first page's " + std::to_string (cs.size()) + " controls do not overlap (" + std::to_string (clash) + ")" + which);
        }
        ed->setSize (2000, 1323); ed.reset();
        std::unique_ptr<juce::AudioProcessorEditor> e2 (p.createEditor()); juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
        C.ok (e2->getWidth() == w && e2->getHeight() == h, "reopened at " + std::to_string (e2->getWidth()) + " x " + std::to_string (e2->getHeight()) + " (the fitting size, not the last size)");
    }

    std::printf ("== fresh start: a new ArpEZator is always brand new (nothing carries over from another instance or project)\n");
    juce::String factory; { ArpProcessor f; factory = arpStateOf (f); }
    {
        ArpProcessor a, b; const auto bStart = arpStateOf (b);
        C.ok (! a.persistTaste && ! b.persistTaste, "HOOK LAB's taste is never read from or written to disk");
        a.change ([] (Idea& X) { X.mode = Sunrise; X.seed = 777; applyModeDefaults (X); newIdea (X); }, false);
        setP (a, "volume", 0.3f); setP (a, "cutoff", 0.2f); a.selLane = arp::LaneMelody; a.hintsSeen = true; a.theme = "light";
        a.taste.keeps += 3;
        C.ok (arpStateOf (a) != factory, "the first has a new idea, a new sound and new settings");
        C.ok (bStart == factory && arpStateOf (b) == factory && b.taste.keeps == 0, "the second, open at the same time, is still at factory defaults");
        ArpProcessor c;
        C.ok (arpStateOf (c) == factory && c.taste.keeps == 0 && c.idea().mode == Midnight && c.theme == "auto" && c.faceFor (c.idea().mode) == arpui::faces::Neon, "a third made after the change starts at factory defaults (MIDNIGHT, AUTO = NEON RAIN, nothing learned)");
    }
    {
        ArpProcessor d; C.ok (arpStateOf (d) == factory && ! d.hintsSeen, "destroyed and made again: factory defaults");
        // and it makes sound straight away (the install check: the real engine, the default idea, 2 bars)
        RenderOpts o; o.bars = 2; std::vector<MidiEv> ev; int bp = 0;
        const auto out = render (d, o, ev, bp);
        const float peak = out.getMagnitude (0, out.getNumSamples()), rms = out.getRMSLevel (0, 0, out.getNumSamples());
        C.ok (peak > 0.01f && peak <= 1.0f && rms > 0.001f && ! ev.empty(), "a new ArpEZator plays its first idea (peak " + std::to_string (peak) + ", " + std::to_string (ev.size()) + " MIDI events)");
    }
}

// ---- 2026-11-07: TEMPO ½x | 1x | 2x (Shared/TempoMul.h)
static void tempoChecks (Counter& C)
{
    std::printf ("== TEMPO 1/2x | 1x | 2x: locked to the DAW's bars, MIDI lands as heard at the DAW's tempo, audio rendered as heard, saved with the project\n");
    auto play = [] (int mul, double hostBpm, std::vector<MidiEv>& ev) {
        ArpProcessor p; p.tempoMul.store (mul); RenderOpts o; o.bpm = hostBpm; o.bars = 2; o.sr = 48000; o.block = 512; int bp = 0;
        if (mul == 2) o.bars = 1; if (mul == 0) o.bars = 4;   // (the same music: 2 bars at 1x = 1 DAW bar at 2x = 4 DAW bars at 1/2x)
        return render (p, o, ev, bp); };
    auto same = [] (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b, int n) { if (a.getNumSamples() < n || b.getNumSamples() < n) return false; for (int c = 0; c < 2; ++c) for (int i = 0; i < n; ++i) if (a.getSample (c, i) != b.getSample (c, i)) return false; return true; };
    {
        std::vector<MidiEv> e1, e2, eh;
        const auto a1 = play (1, 120, e1), a2 = play (2, 60, e2), ah = play (0, 240, eh);
        const int n = (int) (48000 * 4.0);   // two bars at 120
        C.ok (same (a1, a2, n), "2x on a DAW at 60 bpm plays exactly what 1x at 120 plays (same sound, same place in the DAW's bars)");
        C.ok (same (a1, ah, n), "1/2x on a DAW at 240 bpm plays exactly what 1x at 120 plays");
        bool evOk = e1.size() > 4; size_t k1 = 0, k2 = 0; for (auto& x : e1) if (x.at < n) ++k1; for (auto& x : e2) if (x.at < n) ++k2; evOk = evOk && k1 == k2;
        C.ok (evOk, "and the same MIDI out at the same samples (" + std::to_string (k1) + " / " + std::to_string (k2) + ")");
    }
    {
        ArpProcessor p; p.hostBpm.store (130.0);
        auto notes = [] (const juce::File& f, double& tempo) { std::vector<double> t; juce::MidiFile mf; juce::FileInputStream in (f); mf.readFrom (in); tempo = 0; const double tpq = mf.getTimeFormat() > 0 ? mf.getTimeFormat() : 960;
            for (int k = 0; k < mf.getNumTracks(); ++k) for (auto* e : *mf.getTrack (k)) { if (e->message.isNoteOn()) t.push_back (e->message.getTimeStamp() / tpq); if (e->message.isTempoMetaEvent()) tempo = 60.0 / e->message.getTempoSecondsPerQuarterNote(); }
            std::sort (t.begin(), t.end()); return t; };
        double t1 = 0, t2 = 0, th = 0;
        p.tempoMul.store (1); const auto f1 = p.writeMidiFile (15, false); const auto n1 = notes (f1, t1);
        p.tempoMul.store (2); const auto f2 = p.writeMidiFile (15, false); const auto n2 = notes (f2, t2);
        p.tempoMul.store (0); const auto fh = p.writeMidiFile (15, false); const auto nh = notes (fh, th);
        double m2 = 0, mh = 0; const bool sz = n1.size() == n2.size() && n1.size() == nh.size() && ! n1.empty();
        if (sz) for (size_t i = 0; i < n1.size(); ++i) { m2 = std::max (m2, std::abs (n2[i] - n1[i] * 0.5)); mh = std::max (mh, std::abs (nh[i] - n1[i] * 2.0)); }
        std::printf ("  MIDI at 130 bpm: %zu notes, last at beat %.3f / %.3f / %.3f, timing error %.4f / %.4f beats, file tempo %.1f / %.1f / %.1f\n", n1.size(), n1.empty() ? 0 : n1.back(), n2.empty() ? 0 : n2.back(), nh.empty() ? 0 : nh.back(), m2, mh, t1, t2, th);
        C.ok (sz && m2 < 0.005 && std::abs (t2 - 130) < 0.01, "2x: every MIDI note at half the DAW beat, the file at the DAW's 130 bpm");
        C.ok (sz && mh < 0.005 && std::abs (th - 130) < 0.01, "1/2x: every MIDI note at twice the DAW beat, the file at 130 bpm");
        C.ok (f2.getFileName().contains ("260bpm") && fh.getFileName().contains ("65bpm") && f1.getFileName().contains ("130bpm"), "MIDI names say the BPM heard: " + f2.getFileName().toStdString() + " / " + fh.getFileName().toStdString());
        // audio: rendered at the tempo heard
        auto seconds = [&] (int mul) { p.tempoMul.store (mul); auto job = p.renderJob (48000.0, p.effectiveBpm()); const auto b = ArpProcessor::renderOffline (job, -1); return b.getNumSamples() / 48000.0 - job.tail; };
        const double s1 = seconds (1), s2 = seconds (2), sh = seconds (0);
        std::printf ("  audio: %.3f s (1x) / %.3f s (2x) / %.3f s (1/2x) of music before the tail\n", s1, s2, sh);
        C.ok (std::abs (s2 - s1 * 0.5) < 0.002 && std::abs (sh - s1 * 2.0) < 0.002, "audio length follows the tempo heard");
        // saved with the project; a new one starts at 1x
        p.tempoMul.store (2); juce::MemoryBlock mb; p.getStateInformation (mb);
        ArpProcessor q; C.ok (q.tempoMul.load() == 1, "a new ArpEZator starts at 1x"); q.setStateInformation (mb.getData(), (int) mb.getSize()); C.ok (q.tempoMul.load() == 2, "the project reopens at 2x");
    }
    {   // the switch: next to BPM / TAP, nothing under it, clicking sets TEMPO
        ArpProcessor p; p.hintsSeen = true; p.hostBpm.store (130.0);
        std::unique_ptr<arpui::ArpEditor> ed (dynamic_cast<arpui::ArpEditor*> (p.createEditor())); juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        auto& sw = ArpEditorTestAccess::tempo (*ed); auto* content = sw.getParentComponent(); int clash = 0; std::string which;
        for (auto* c : content->getChildren()) if (c != &sw && c->isVisible() && c->getBounds().intersects (sw.getBounds()) && c->getWidth() < 600 && c->getName() != "hints") { ++clash; which += " " + c->getBounds().toString().toStdString(); }
        C.ok (sw.isVisible() && content->getLocalBounds().contains (sw.getBounds()) && clash == 0, "the TEMPO switch sits in the transport row with nothing under it" + which);
        const auto c2 = sw.segment (2).getCentre();
        sw.mouseDown (juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), c2, juce::ModifierKeys(), 0, 0, 0, 0, 0, &sw, &sw, juce::Time::getCurrentTime(), c2, juce::Time::getCurrentTime(), 1, false));
        C.ok (p.tempoMul.load() == 2 && std::abs (p.effectiveBpm() - 260) < 0.01, "clicking 2x sets TEMPO 2x (260 heard at 130)");
        if (auto* d = std::getenv ("ARP_SHOTS")) { juce::File dir (d); dir.createDirectory(); juce::MessageManager::getInstance()->runDispatchLoopUntil (20); const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::FileOutputStream os (dir.getChildFile ("arpezator-tempo-2x.png")); if (os.openedOk()) { os.setPosition (0); os.truncate(); juce::PNGImageFormat().writeImageToStream (img, os); } }
    }
}
