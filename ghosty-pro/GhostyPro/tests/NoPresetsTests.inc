// FACTORY PRESETS OFF (2026-10-26, docs/research/PRO-FACTORY-PRESETS-OFF.md)
//   ProTests nopresetsref <dir>          -> (any build WITH the factory bank: main / an ON build) the reference: INIT render, project
//                                           states that carry factory sounds (plain, scene, STACK layers, a picked layer, CLASSIC engine,
//                                           macros, LEGACY, V2 showcase, a tweak) + a saved sound file, and how each one renders reloaded
//   ProTests nopresets <dir> [shots]     -> (an OFF build: -DGHOSTY_PRO_TESTS_FACTORY_PRESETS=OFF) a fresh instance = INIT bit-exact,
//                                           the empty BROWSE / header / menus, host programs, the reference projects render bit-exact,
//                                           your sounds save / load / walk; screenshots of BROWSE + header in all four looks
namespace noprtest
{
static juce::uint64 hashOf (const juce::AudioBuffer<float>& b)
{
    juce::uint64 h = 1469598103934665603ull;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
        for (int i = 0; i < b.getNumSamples(); ++i) { juce::uint32 u; const float v = b.getSample (ch, i); std::memcpy (&u, &v, 4); h = (h ^ u) * 1099511628211ull; }
    return h;
}
static juce::String hex (juce::uint64 h) { return juce::String::toHexString ((juce::int64) h); }
static juce::File testHome (const juce::File& dir)
{   // your sounds live in ~/Documents/Ghosty/Ghosty Pro: the test runs with its own HOME so the real folder is never touched
    auto home = dir.getChildFile ("home"); home.deleteRecursively(); home.createDirectory();
    setenv ("HOME", home.getFullPathName().toRawUTF8(), 1);
    return home;
}
static std::unique_ptr<ProProcessor> freshProc()
{   // a new instance exactly as a host makes one (no program call)
    seedSystemRandom (777);
    auto p = std::make_unique<ProProcessor>();
    p->prepareToPlay (SR, BS);
    settle (*p);
    return p;
}
static juce::AudioBuffer<float> renderState (const juce::MemoryBlock& mb)
{
    auto q = freshProc();
    q->setStateInformation (mb.getData(), (int) mb.getSize());
    settle (*q);
    return seededRender (*q);
}

// ------------------------------------------------------------------ the reference (a build with the factory bank)
int makeRef (const juce::File& dir)
{
    dir.deleteRecursively(); dir.createDirectory();
    testHome (dir);
    {   // today's INIT: a new instance, INIT pressed
        auto p = freshProc(); p->initPatch(); settle (*p);
        auto b = seededRender (*p);
        writeRaw (b, dir.getChildFile ("init.f32"));
        dir.getChildFile ("init.hash").replaceWithText (hex (hashOf (b)));
        printf ("INIT hash %s\n", hex (hashOf (b)).toRawUTF8());
    }
    const auto& bank = gpro::factoryBank();
    auto first = [&] (std::function<bool (const gpro::FactoryPreset&)> pred) { for (int i = 0; i < (int) bank.size(); ++i) if (pred (bank[(size_t) i])) return i; return -1; };
    struct S { juce::String file; int prog; int kind; };   // kind: 0 as loaded, 1 + a picked layer + tweaks, 2 CLASSIC engine
    std::vector<S> list {
        { "program0", 0, 0 },
        { "scene", first ([] (auto& f) { return f.scene.isNotEmpty() && ! f.spec.contains ("ly2"); }), 0 },
        { "stack_ly2", first ([] (auto& f) { return f.spec.contains ("ly2="); }), 0 },
        { "stack_ly3", first ([] (auto& f) { return f.spec.contains ("ly3="); }), 0 },
        { "macros", first ([] (auto& f) { return f.spec.contains ("mc="); }), 0 },
        { "classics", first ([] (auto& f) { return f.cat == "Ghosty Classics"; }), 0 },
        { "legacy", first ([] (auto& f) { return gpro::isLegacy (f.name); }), 0 },
        { "v2showcase", first ([] (auto& f) { return f.cat == "V2 Showcase"; }), 0 },
        { "trapbrass", first ([] (auto& f) { return f.cat == "Trap Brass"; }), 0 },
        { "picked_layer_tweaked", first ([] (auto& f) { return f.cat == "Pads"; }), 1 },
        { "classic_engine", first ([] (auto& f) { return f.cat == "Bass"; }), 2 },
    };
    juce::StringArray lines;
    for (auto& s : list)
    {
        if (s.prog < 0) { printf ("FAIL  no factory sound for %s\n", s.file.toRawUTF8()); ++failures; continue; }
        seedSystemRandom (777);
        auto p = std::make_unique<ProProcessor>(); p->prepareToPlay (SR, BS);
        if (s.kind == 2) p->setSoundEngine (ProProcessor::engineClassic);
        p->setCurrentProgram (s.prog);
        if (s.kind == 1)
        {
            p->setLayerSound (1, bank[(size_t) first ([] (auto& f) { return f.cat == "Bells & Mallets"; })].name);   // a layer you picked in TRAP
            p->setParam ("cutoff", 0.47f); p->setParam ("ly2_lvl", 0.6f); p->setParam ("verb", 0.31f);
        }
        settle (*p);
        juce::MemoryBlock mb; p->getStateInformation (mb);
        dir.getChildFile (s.file + ".state").replaceWithData (mb.getData(), mb.getSize());
        writeRaw (renderState (mb), dir.getChildFile (s.file + ".f32"));
        {   // the same project opened by a host that prepares an instance at INIT first (an OFF build's new instance is INIT)
            seedSystemRandom (777);
            auto q = std::make_unique<ProProcessor>(); q->initPatch(); q->prepareToPlay (SR, BS); settle (*q);
            q->setStateInformation (mb.getData(), (int) mb.getSize()); settle (*q);
            writeRaw (seededRender (*q), dir.getChildFile (s.file + ".initprior.f32"));
        }
        {   // ... and by a host that loads the project before it prepares the instance
            seedSystemRandom (777);
            auto q = std::make_unique<ProProcessor>(); q->setStateInformation (mb.getData(), (int) mb.getSize()); q->prepareToPlay (SR, BS); settle (*q);
            writeRaw (seededRender (*q), dir.getChildFile (s.file + ".statefirst.f32"));
        }
        if (s.kind == 1)
        {   // the same sound as a saved sound file (it names its layer: the OFF build must still find it)
            p->saveUserPreset ("NP Ref Layered");
            ProProcessor::userPresetDir().getChildFile ("NP Ref Layered.ghostypro").copyFileTo (dir.getChildFile ("user_layered.ghostypro"));
            auto u = freshProc(); u->loadUserPreset (dir.getChildFile ("user_layered.ghostypro")); settle (*u);
            writeRaw (seededRender (*u), dir.getChildFile ("user_layered.f32"));
        }
        lines.add (s.file + "|" + bank[(size_t) s.prog].name + "|" + p->layerSound (1) + "|" + p->layerSound (2));
        printf ("  ref %-22s %s (layers: '%s' '%s')\n", s.file.toRawUTF8(), bank[(size_t) s.prog].name.toRawUTF8(), p->layerSound (1).toRawUTF8(), p->layerSound (2).toRawUTF8());
    }
    dir.getChildFile ("sessions.txt").replaceWithText (lines.joinIntoString ("\n"));
    printf ("\nreference in %s, %d failures\n", dir.getFullPathName().toRawUTF8(), failures);
    return failures;
}

#ifdef GHOSTY_FACTORY_DATA     // (this branch on: the OFF checks)
static double lum (juce::Colour c)
{
    auto ch = [] (juce::uint8 v) { const double x = v / 255.0; return x <= 0.03928 ? x / 12.92 : std::pow ((x + 0.055) / 1.055, 2.4); };
    return 0.2126 * ch (c.getRed()) + 0.7152 * ch (c.getGreen()) + 0.0722 * ch (c.getBlue());
}
static double contrast (juce::Colour a, juce::Colour b) { const double x = lum (a), y = lum (b); return (std::max (x, y) + 0.05) / (std::min (x, y) + 0.05); }
// the text contrast inside an area of a screenshot: the background = the most common colour, the text = the pixel furthest from it
static double areaContrast (const juce::Image& img, juce::Rectangle<int> r)
{
    std::map<juce::uint32, int> count;
    r = r.getIntersection (img.getBounds());
    for (int y = r.getY(); y < r.getBottom(); ++y) for (int x = r.getX(); x < r.getRight(); ++x) ++count[img.getPixelAt (x, y).getARGB() & 0xfcfcfcfcu];
    juce::uint32 bgc = 0; int best = -1; for (auto& [c, n] : count) if (n > best) { best = n; bgc = c; }
    const auto bg = juce::Colour (bgc);
    double top = 1.0;
    for (int y = r.getY(); y < r.getBottom(); ++y) for (int x = r.getX(); x < r.getRight(); ++x) top = std::max (top, contrast (bg, img.getPixelAt (x, y)));
    return top;
}

static bool factorySoundCheck() { const auto t = ProProcessor::factorySound (3); return t.isValid() && t.getProperty ("patchName").toString() == "Init"; }

int run (const juce::File& ref, const juce::File& shots)
{
    section ("FACTORY PRESETS OFF: no factory sounds; INIT + your sounds; older projects play as before");
    CHECK (! gpro::factoryPresetsBuilt() && gpro::factoryBank().empty(), "this build has no factory sound presets (factoryBank %d)", (int) gpro::factoryBank().size());
    const auto home = testHome (shots.getParentDirectory().getChildFile ("nopresets-run"));
    // (a) a fresh instance = today's INIT, bit for bit
    {
        auto p = freshProc();
        auto b = seededRender (*p);
        auto r = readRaw (ref.getChildFile ("init.f32"));
        // INIT's render hash from main (42be6c1, 2026-10-26; ProTests nopresetsref): used when the reference folder has none
        const auto want = ref.getChildFile ("init.hash").existsAsFile() ? ref.getChildFile ("init.hash").loadFileAsString().trim() : juce::String ("4ac230103c441877");
        CHECK (hex (hashOf (b)) == want && (r.getNumSamples() == 0 || maxDiff (b, r) == 0.0f) && peakOf (b) > 0.01f,
               "(a) a new instance renders INIT bit-exact: hash %s (main %s), max diff %g, peak %.3f", hex (hashOf (b)).toRawUTF8(), want.toRawUTF8(), maxDiff (b, r), peakOf (b));
        CHECK (p->patchName() == "Init" && ! p->isUserPatch() && p->getCurrentProgram() == 0, "(a) its name is Init, program 0 (name '%s', program %d)", p->patchName().toRawUTF8(), p->getCurrentProgram());
        auto q = freshProc(); q->initPatch(); settle (*q);
        CHECK (maxDiff (seededRender (*q), r) == 0.0f, "(a) INIT pressed on it: still the same INIT");
    }
    // (c) host programs
    {
        auto p = freshProc();
        CHECK (p->getNumPrograms() == 1 && p->getProgramName (0) == "Init" && p->getProgramName (1).isEmpty() && p->getProgramName (-1).isEmpty(),
               "(c) no saved sounds: 1 program (%d), program 0 = '%s'", p->getNumPrograms(), p->getProgramName (0).toRawUTF8());
        bool same = true;
        for (int idx : { 57, 1, 263, -1, 100000 })
        {   // an older version's factory program number (a host that remembered it): INIT, no error
            auto q = freshProc(); q->setParam ("cutoff", 0.2f); q->setCurrentProgram (idx); settle (*q);
            same &= q->getCurrentProgram() == 0 && q->patchName() == "Init" && maxDiff (seededRender (*q), readRaw (ref.getChildFile ("init.f32"))) == 0.0f;
        }
        CHECK (same, "(c) setCurrentProgram 57 / 1 / 263 / -1 / 100000 (programs this build doesn't have): each loads INIT, bit-exact");
        auto f = factorySoundCheck();
        CHECK (f, "(c) MORPH's factory-sound loader with no factory sounds: an INIT sound, no crash");
    }
    // (e) your sounds: save / load round trip, host programs, the header < >
    juce::AudioBuffer<float> soundA;
    {
        auto p = freshProc();
        p->setParam ("cutoff", 0.33f); p->setParam ("o1_uni", 5.0f); p->setParam ("verb", 0.4f); settle (*p);
        soundA = seededRender (*p);
        const bool savedA = p->saveUserPreset ("NP Sound A");
        p->initPatch(); p->setParam ("res", 0.5f); p->setParam ("mono", 1.0f); settle (*p);
        const bool savedB = p->saveUserPreset ("NP Sound B");
        CHECK (savedA && savedB && p->getNumPrograms() == 3 && p->getProgramName (1) == "NP Sound A" && p->getProgramName (2) == "NP Sound B" && p->getCurrentProgram() == 2,
               "(e) two saved sounds: %d programs (%s, %s), current %d", p->getNumPrograms(), p->getProgramName (1).toRawUTF8(), p->getProgramName (2).toRawUTF8(), p->getCurrentProgram());
        auto q = freshProc();
        CHECK (q->getNumPrograms() == 3, "(e) a new instance lists them as programs too (%d)", q->getNumPrograms());
        q->setCurrentProgram (1); settle (*q);
        const float d1 = maxDiff (seededRender (*q), soundA);
        auto u = freshProc(); const bool ok = u->loadUserPreset (ProProcessor::userPresetDir().getChildFile ("NP Sound A.ghostypro")); settle (*u);
        const float d2 = maxDiff (seededRender (*u), soundA);
        CHECK (d1 == 0.0f && ok && d2 == 0.0f && q->patchName() == "NP Sound A" && q->isUserPatch() && q->getCurrentProgram() == 1 && u->getCurrentProgram() == 1,
               "(e) program 1 / LOAD of the saved file play it bit-exact (diff %g / %g), name '%s', program %d / %d", d1, d2, q->patchName().toRawUTF8(), q->getCurrentProgram(), u->getCurrentProgram());
        juce::MemoryBlock mb; q->getStateInformation (mb);
        auto r = freshProc(); r->setStateInformation (mb.getData(), (int) mb.getSize()); settle (*r);
        CHECK (maxDiff (seededRender (*r), soundA) == 0.0f && r->getCurrentProgram() == 1 && r->patchName() == "NP Sound A", "(e) a project with it reopens bit-exact, program %d", r->getCurrentProgram());
        // the file a build WITH the old bank saved (its STACK layer is a factory sound, named in the file)
        auto l = freshProc(); const bool okL = l->loadUserPreset (ref.getChildFile ("user_layered.ghostypro")); settle (*l);
        const float dl = maxDiff (seededRender (*l), readRaw (ref.getChildFile ("user_layered.f32")));
        CHECK (okL && dl == 0.0f && l->layerSound (1).isNotEmpty(), "(e) a saved sound from the old build with a factory STACK layer ('%s') loads and plays bit-exact (diff %g)", l->layerSound (1).toRawUTF8(), dl);
    }
    // (d) older projects: the same host order on both sides (main also renders a project a little differently in its first notes when the
    //     instance was prepared on another sound: the FX rack's on / mix ramps start from what was playing; see the research note)
    {
        juce::StringArray lines; ref.getChildFile ("sessions.txt").readLines (lines);
        int same = 0, n = 0;
        for (auto& ln : lines)
        {
            if (ln.isEmpty()) continue;
            juce::StringArray f; f.addTokens (ln, "|", ""); ++n;
            juce::MemoryBlock mb; ref.getChildFile (f[0] + ".state").loadFileAsData (mb);
            auto q = freshProc(); q->setStateInformation (mb.getData(), (int) mb.getSize()); settle (*q);       // prepared (at INIT), then the project
            const auto b = seededRender (*q); const auto r = readRaw (ref.getChildFile (f[0] + ".initprior.f32"));
            const float d = r.getNumSamples() == b.getNumSamples() ? maxDiff (b, r) : 1.0f;
            seedSystemRandom (777);
            auto q2 = std::make_unique<ProProcessor>(); q2->setStateInformation (mb.getData(), (int) mb.getSize()); q2->prepareToPlay (SR, BS); settle (*q2);   // the project, then prepared
            const auto b2 = seededRender (*q2); const auto r2 = readRaw (ref.getChildFile (f[0] + ".statefirst.f32"));
            const float d2 = r2.getNumSamples() == b2.getNumSamples() ? maxDiff (b2, r2) : 1.0f;
            const float dMain = maxDiff (b, readRaw (ref.getChildFile (f[0] + ".f32")));
            const bool layersOk = q->layerSound (1) == f[2] && q->layerSound (2) == f[3];
            bool ok = d == 0.0f && d2 == 0.0f && layersOk && peakOf (b) > 0.001f && q->patchName() == f[1];
            juce::MemoryBlock mb2; q->getStateInformation (mb2);
            auto q3 = freshProc(); q3->setStateInformation (mb2.getData(), (int) mb2.getSize()); settle (*q3);
            const float d3 = maxDiff (seededRender (*q3), r);
            ok = ok && d3 == 0.0f;
            same += ok ? 1 : 0;
            printf ("  %s %-22s %-24s diff %g / %g (prepared first / project first), re-saved %g; layers '%s' '%s', shown as '%s'  [vs main's program-0 instance %g]\n", ok ? "same" : "DIFF",
                    f[0].toRawUTF8(), f[1].toRawUTF8(), d, d2, d3, q->layerSound (1).toRawUTF8(), q->layerSound (2).toRawUTF8(), q->patchName().toRawUTF8(), dMain);
        }
        CHECK (n >= 10 && same == n, "(d) %d of %d projects saved by main with a factory sound (plain, scene, STACK layers, a picked layer, CLASSIC engine, macros, LEGACY, ...) "
               "render bit-exact here in both host orders, keep their layers, show their old name, and re-save / reopen bit-exact", same, n);
    }
    // (b) the editor: BROWSE empty state, header, arrows, search, favourites, RANDOM; all four looks
    {
        home.getChildFile ("Documents/Ghosty/Ghosty Pro").deleteRecursively();       // no saved sounds
        auto p = freshProc();
        std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
        auto* pe = browsetest::editorOf (ed); pe->setSize (ProEditor::W, ProEditor::H); ed->setVisible (true); pump (150);
        auto* content = pe->contentComponent();
        auto* next = browsetest::headerButton (content, ">"); auto* prev = browsetest::headerButton (content, "<");
        pump (100);
        CHECK (next != nullptr && prev != nullptr && ! next->isEnabled() && ! prev->isEnabled() && next->getTooltip().contains ("No saved sounds"),
               "(b) header < > greyed with no saved sounds; tooltip: %s", next != nullptr ? next->getTooltip().toRawUTF8() : "-");
        CHECK (pe->presetNameTooltip().isNotEmpty(), "(b) header name: tooltip '%s'", pe->presetNameTooltip().replaceCharacter ('\n', '|').toRawUTF8());
        if (next != nullptr) { next->triggerClick(); prev->triggerClick(); pump (30); }
        pe->showTab (3); pump (150);
        auto e = pe->browseEmptyInfo();
        CHECK (e.shown && e.initButton && ! e.legacyButton && ! e.tagsButton && e.entries == 0 && e.text.contains ("hand-made") && e.title == "No factory sounds yet",
               "(b) BROWSE: empty state '%s / %s', START FROM INIT shown, SHOW LEGACY + TAGS hidden, %d entries", e.title.toRawUTF8(), e.text.replaceCharacter ('\n', ' ').toRawUTF8(), e.entries);
        // nothing crashes on an empty list: search, chips, < > RANDOM inside it, favourites of the current sound, keys
        pe->browseTestFilter ("lead", { "fm" }, true); pump (60);
        auto ef = pe->browseEmptyInfo();
        pe->browseTestFilter ("", {}, false); pump (60);
        bool survived = true;
        {
            std::function<void (juce::Component*)> walk = [&] (juce::Component* c)
            { for (auto* ch : c->getChildren()) { if (auto* tb = dynamic_cast<juce::TextButton*> (ch)) { const auto t = tb->getButtonText(); if (t == "<" || t == ">" || t == "RANDOM" || t == "STAR / UNSTAR" || t == "DELETE (your sound)") { if (t != "DELETE (your sound)") tb->triggerClick(); } } walk (ch); } };
            walk (content); pump (100);
            pe->keyPressed (juce::KeyPress (juce::KeyPress::rightKey)); pump (20);
        }
        CHECK (survived && ef.shown && ef.title == "Nothing matches" && p->patchName() == "Init", "(b) search 'lead' + chip on the empty list: '%s'; < > RANDOM STAR on it: no crash, still Init", ef.title.toRawUTF8());
        // the empty state in every look: text contrast, no see-through
        shots.createDirectory();
        static const char* lookName[] { "black", "purple", "light", "ghost" };
        int shotsN = 0;
        for (int t : { 0, 1, 2, 3 })
        {
            pe->setTheme (t); pump (200);
            const auto ei = pe->browseEmptyInfo();
            auto img = pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f);
            browsetest::savePng (img, shots.getChildFile (juce::String ("browse-empty-") + lookName[t] + ".png")); ++shotsN;
            browsetest::savePng (img.getClippedImage ({ 0, 0, ProEditor::W, 70 }), shots.getChildFile (juce::String ("header-init-") + lookName[t] + ".png")); ++shotsN;
            const double cTitle = areaContrast (img, ei.area.withHeight (28)), cText = areaContrast (img, ei.area.withTrimmedTop (28));
            int holes = 0; for (int y = 0; y < img.getHeight(); y += 4) for (int x = 0; x < img.getWidth(); x += 4) holes += img.getPixelAt (x, y).getAlpha() < 255;
            const int see = t == 3 ? 0 : pe->seeThroughPixels();     // (GHOST: glass cards by design; the window itself has no hole)
            CHECK (cTitle >= 4.5 && cText >= 4.5 && holes == 0 && see == 0, "(b) %s: empty-state contrast title %.1f:1, text %.1f:1 (>= 4.5), alpha holes %d, see-through %d", lookName[t], cTitle, cText, holes, see);
        }
        // with saved sounds: the arrows walk them (and only them)
        {
            p->setParam ("cutoff", 0.4f); p->saveUserPreset ("NP Walk 1"); p->setParam ("cutoff", 0.6f); p->saveUserPreset ("NP Walk 2");
            p->initPatch(); pe->showTab (3); pump (200);
            const auto ew = pe->browseEmptyInfo();
            juce::StringArray seen;
            for (int k = 0; k < 3; ++k) { next->triggerClick(); pump (60); seen.add (p->patchName()); }
            prev->triggerClick(); pump (60); seen.add (p->patchName());
            CHECK (next->isEnabled() && seen.joinIntoString (",") == "NP Walk 1,NP Walk 2,NP Walk 1,NP Walk 2" && ! ew.shown && ew.rows == 2,
                   "(b) two saved sounds: arrows enabled, > > > < from INIT = %s; BROWSE lists %d rows, no empty state", seen.joinIntoString (",").toRawUTF8(), ew.rows);
            pe->setTheme (0); pump (150);
            browsetest::savePng (pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f), shots.getChildFile ("browse-your-sounds-black.png")); ++shotsN;
        }
        pe->showTab (5); pump (100); pe->showTab (0); pump (60);   // TRAP + SYNTH pages with the empty bank
        printf ("  %d screenshots in %s\n", shotsN, shots.getFullPathName().toRawUTF8());
    }
    printf ("\n%d failures\n", failures);
    return failures;
}
#endif
} // namespace noprtest
