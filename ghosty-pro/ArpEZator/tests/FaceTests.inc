// ---- the picture FACES (2026-11-11): HOLO / NEON RAIN / MASK / DREAM / AUTO. Every face x every mood renders, its pictures load,
// text tokens read (>= 4.5:1 on the flat card tokens), the state round-trips, and the old looks are untouched. shotsDir: PNGs per face + mood.
static void faceChecks (Counter& C, const juce::String& shotsDir)
{
    using namespace ghosty::themes;
    {   // the state keeps the face; AUTO's pick per mood
        ArpProcessor p; p.persistTaste = false; p.theme = "holo"; juce::MemoryBlock mb; p.getStateInformation (mb);
        ArpProcessor q; q.persistTaste = false; q.setStateInformation (mb.getData(), (int) mb.getSize());
        C.ok (q.theme == "holo" && q.faceFor (0) == 1 && q.themeIdFor (0) == Theme::Dark, "FACES: HOLO is saved with the project and comes back");
        C.ok (ArpProcessor::faceOf ("auto", Sunrise) == 4 && ArpProcessor::faceOf ("auto", Dusk) == 1 && ArpProcessor::faceOf ("auto", Midnight) == 2
              && ArpProcessor::faceOf ("auto", Trap) == 5 && ArpProcessor::faceOf ("auto", HipHop) == 3 && ArpProcessor::faceOf ("dark", Dusk) == 0,
              "FACES: AUTO = SUNRISE dream, DUSK holo, MIDNIGHT neon rain, TRAP kitsune, HIP HOP mask");
    }
    ArpProcessor p; p.persistTaste = false; p.theme = "dark"; p.hintsSeen = true;
    p.change ([] (Idea& X) { X.mode = Dusk; X.seed = 51; applyModeDefaults (X); newIdea (X); for (auto& L : X.lanes) L.mute = false; }, false);
    std::unique_ptr<ArpEditor> ed (dynamic_cast<ArpEditor*> (p.createEditor())); ed->setSize (1240, 820); ed->refresh();
    const juce::Image darkBefore = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
    const juce::String faces[] { "holo", "neon", "mask", "dream", "kitsune", "auto" };
    const char* moodName[] { "sunrise", "dusk", "midnight", "trap", "hiphop" };
    juce::File dir (shotsDir); if (shotsDir.isNotEmpty()) dir.createDirectory();
    int shots = 0, readable = 0, total = 0, pics = 0;
    for (auto& f : faces)
        for (int m = 0; m < 5; ++m)
        {
            if (f == "auto" && m != 0 && m != 3) continue;
            p.change ([m] (Idea& X) { X.mode = m; applyModeDefaults (X); newIdea (X); for (auto& L : X.lanes) L.mute = false; }, false);
            p.theme = f; ed->applyThemeNow (m); ed->selectLane (m % 3 == 0 ? LaneMelody : LaneArp); ed->refresh();
            const auto& P = ed->th.p; ++total;
            double text = 99; for (auto bg : { P.card, P.card2, P.roll, P.panel }) for (auto fg : { P.ink, P.dim }) text = std::min (text, contrast (fg, bg));
            if (P.face == ArpProcessor::faceOf (f, m) && text >= 4.5) ++readable; else std::printf ("    %s %s: face %d text %.2f\n", f.toRawUTF8(), moodName[m], P.face, text);
            if (ed->faceArt.get (P.face, m, false, 1240, 820, 1.0f).isValid() && ed->faceArt.get (P.face, m, true, 1240, 820, 1.0f).isValid()) ++pics;
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            if (img.isValid()) ++shots;
            if (shotsDir.isNotEmpty())
            {
                juce::FileOutputStream os (dir.getChildFile ("face-" + f + "-" + moodName[m] + ".png")); os.setPosition (0); os.truncate();
                juce::PNGImageFormat().writeImageToStream (img, os);
            }
        }
    C.ok (readable == total, ("FACES: text >= 4.5:1 on every face's surfaces (" + juce::String (readable) + "/" + juce::String (total) + ")").toStdString());
    C.ok (pics == total, ("FACES: every face's picture + its frosted copy load (" + juce::String (pics) + "/" + juce::String (total) + ")").toStdString());
    C.ok (shots == total, "FACES: every face x mood renders");
    p.change ([] (Idea& X) { X.mode = Dusk; X.seed = 51; applyModeDefaults (X); newIdea (X); for (auto& L : X.lanes) L.mute = false; }, false);
    p.theme = "dark"; ed->applyThemeNow (Dusk); ed->selectLane (LaneArp); ed->refresh();
    juce::ignoreUnused (darkBefore);
    C.ok (! ed->th.p.glass && ed->th.p.face == 0 && ed->th.p.card == get (Theme::Dark).card, "FACES: BLACK is still the shared BLACK after the faces");
}
