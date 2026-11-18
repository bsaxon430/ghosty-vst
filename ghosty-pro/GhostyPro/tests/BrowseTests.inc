// PRESET BROWSER + VOICING (round 3 item 6)
//   ProTests browse [shots dir]  -> search / filter / sort / favourites round trip, derived tags, prev / next / random inside a filter,
//                                   the page in the editor (header < > follow a filter, tooltips), BROWSE screenshots in every look
//   ProTests steal               -> VOICING: steal priority Newest / Oldest / Highest / Lowest / Quietest, LIMIT SAME NOTE POLY TO 1
#include "ProBrowser.h"
namespace browsetest
{
using namespace gpro::browse;

int factoryIndex (const juce::String& name) { const auto& b = gpro::factoryBank(); for (int i = 0; i < (int) b.size(); ++i) if (b[(size_t) i].name == name) return i; return -1; }
bool tagged (const juce::String& name, const juce::String& t) { return gpro::factoryTags (factoryIndex (name)).contains (t); }
juce::String tagsOf (const juce::String& name) { return gpro::factoryTags (factoryIndex (name)).joinIntoString (","); }

juce::File tmpDir() { auto d = juce::File ("/root/work/clang-browser/tmp-browse"); d.deleteRecursively(); d.createDirectory(); return d; }

void modelTests()
{
    section ("BROWSE model: no filter = the bank as before; derived tags; search; tag chips; sort; favourites + recent round trip; < > and RANDOM inside a filter");
    const auto& bank = gpro::factoryBank();
    const auto dir = tmpDir();
    Model m; addFactory (m.entries);
    m.favs.open (dir.getChildFile ("favourites.json")); m.recent.open (dir.getChildFile ("recent.json"));
    m.refilter();
    std::vector<int> visibleRows; for (int i = 0; i < (int) m.entries.size(); ++i) if (! m.entries[(size_t) i].legacy) visibleRows.push_back (i);   // SOUND ENGINE V2: LEGACY hidden
    const size_t visibleN = visibleRows.size();
    const bool inOrder = m.shown == visibleRows;
    CHECK (inOrder && ! m.filterActive(), "no filter, bank order: %d rows = the %d factory sounds (of %d; LEGACY hidden) in program order", (int) m.shown.size(), (int) visibleN, (int) bank.size());
    // derived tags of known sounds
    CHECK (tagged ("Hyper Saw Anthem", "unison") && tagged ("Hyper Saw Anthem", "reverb") && tagged ("Hyper Saw Anthem", "clean") && ! tagged ("Hyper Saw Anthem", "mono"),
           "Hyper Saw Anthem: %s", tagsOf ("Hyper Saw Anthem").toRawUTF8());
    CHECK (tagged ("FM Lead Bright", "fm") && tagged ("FM Lead Bright", "mono") && tagged ("FM Lead Bright", "glide") && tagged ("FM Lead Bright", "leads"),
           "FM Lead Bright: %s", tagsOf ("FM Lead Bright").toRawUTF8());
    CHECK (tagged ("Sync Screamer", "sync") && ! tagged ("Sync Screamer", "clean") && ! tagged ("Sync Screamer", "fm"), "Sync Screamer: %s", tagsOf ("Sync Screamer").toRawUTF8());
    CHECK (tagged ("Ghost Runner", "arp") && tagged ("Ghost Runner", "unison") && tagged ("Ghost Runner", "arps"), "Ghost Runner: %s", tagsOf ("Ghost Runner").toRawUTF8());
    CHECK (tagged ("Bell Pluck Shine", "fm") && tagged ("Fanfare Stack", "layered") && tagged ("Boom Sub Long", "mono") && tagged ("Boom Sub Long", "trap") && tagged ("Boom Sub Long", "bass"),
           "Bell Pluck Shine (FM table): %s | Fanfare Stack: %s | Boom Sub Long: %s", tagsOf ("Bell Pluck Shine").toRawUTF8(), tagsOf ("Fanfare Stack").toRawUTF8(), tagsOf ("Boom Sub Long").toRawUTF8());
    {
        int unisonN = 0, cleanN = 0; for (int i = 0; i < (int) bank.size(); ++i) { unisonN += gpro::factoryTags (i).contains ("unison"); cleanN += gpro::factoryTags (i).contains ("clean"); }
        const auto tc = m.tagCounts();
        CHECK (unisonN > 20 && cleanN > 20 && cleanN < (int) bank.size() && tc.size() > 20 && tc[0].first == "unison", "tag chips: %d (first '%s' %d); unison on %d sounds, clean on %d", (int) tc.size(), tc.empty() ? "" : tc[0].first.toRawUTF8(), tc.empty() ? 0 : tc[0].second, unisonN, cleanN);
    }
    // search: name, tag, category, author, several words
    auto rowsFor = [&m] (const juce::String& q) { m.query = q; m.refilter(); return m.shown; };
    auto hasName = [&m] (const std::vector<int>& rows, const juce::String& n) { for (int i : rows) if (m.entries[(size_t) i].name == n) return true; return false; };
    {
        auto byName = rowsFor ("anthem"), byTag = rowsFor ("fm"), byCat = rowsFor ("trap bass"), byAuthor = rowsFor ("factory"), two = rowsFor ("mono fm");
        bool allFm = true; for (int i : byTag) { auto& e = m.entries[(size_t) i]; allFm &= e.tags.joinIntoString (" ").contains ("fm") || e.name.toLowerCase().contains ("fm") || e.cat.toLowerCase().contains ("fm") || e.scene.contains ("fm"); }
        bool allTwo = true; for (int i : two) { auto& e = m.entries[(size_t) i]; allTwo &= e.tags.contains ("mono") && (e.tags.contains ("fm") || e.name.toLowerCase().contains ("fm")); }
        int inCat = 0; bool catOk = true;   // every Trap Bass sound, plus any other sound that has both words somewhere
        for (int i : byCat) { auto& e = m.entries[(size_t) i]; const auto all = (e.name + " " + e.cat + " " + e.tags.joinIntoString (" ")).toLowerCase(); inCat += e.cat == "Trap Bass"; catOk &= all.contains ("trap") && all.contains ("bass"); }
        for (auto& e : m.entries) catOk &= e.cat != "Trap Bass" || hasName (byCat, e.name);
        CHECK (hasName (byName, "Hyper Saw Anthem") && hasName (byTag, "FM Lead Bright") && allFm && catOk && byAuthor.size() == visibleN && hasName (two, "FM Lead Bright") && allTwo,
               "search: 'anthem' %d, 'fm' %d (all fm), 'trap bass' %d (%d Trap Bass + both words), 'factory' %d, 'mono fm' %d (all both)", (int) byName.size(), (int) byTag.size(), (int) byCat.size(), inCat, (int) byAuthor.size(), (int) two.size());
        m.query = {};
    }
    // tag chips: AND of every chip that is on
    {
        m.tags = { "fm", "mono" }; m.refilter();
        bool ok = ! m.shown.empty(); for (int i : m.shown) ok &= m.entries[(size_t) i].tags.contains ("fm") && m.entries[(size_t) i].tags.contains ("mono");
        CHECK (ok && hasName (m.shown, "FM Lead Bright") && m.filterActive(), "chips fm + mono: %d sounds, every one has both", (int) m.shown.size());
        m.tags.clear(); m.refilter();
    }
    // sort
    {
        m.sort = sortName; m.refilter();
        bool byName = true; for (size_t i = 1; i < m.shown.size(); ++i) byName &= m.entries[(size_t) m.shown[i - 1]].name.compareNatural (m.entries[(size_t) m.shown[i]].name) <= 0;
        m.sort = sortCategory; m.refilter();
        bool byCat = true; for (size_t i = 1; i < m.shown.size(); ++i) byCat &= m.entries[(size_t) m.shown[i - 1]].cat.compareNatural (m.entries[(size_t) m.shown[i]].cat) <= 0;
        m.recent.touch (m.entries[5].key()); m.recent.touch (m.entries[40].key()); m.recent.touch (m.entries[7].key()); m.recent.touch (m.entries[40].key());
        m.sort = sortRecent; m.refilter();
        const bool rec = m.shown.size() == visibleN && m.shown[0] == 40 && m.shown[1] == 7 && m.shown[2] == 5 && m.shown[3] == 0 && m.shown[4] == 1;
        Recents r2; r2.open (dir.getChildFile ("recent.json"));
        CHECK (byName && byCat && rec && r2.keys.size() == 3 && r2.keys[0] == m.entries[40].key(), "sort by name %s, by category %s, recent first %s (%d, %d, %d, then bank order); recent.json reloads %d keys",
               byName ? "ok" : "NO", byCat ? "ok" : "NO", rec ? "ok" : "NO", m.shown[0], m.shown[1], m.shown[2], (int) r2.keys.size());
        m.sort = sortBank;
    }
    // favourites: per user, in favourites.json, round trip + the Favourites category + the old favourites.txt
    {
        m.favs.toggle (m.entries[3].key()); m.favs.toggle (m.entries[9].key()); m.favs.toggle (m.entries[11].key()); m.favs.toggle (m.entries[9].key());
        Favourites f2; f2.open (dir.getChildFile ("favourites.json"));
        m.category = "Favourites"; m.refilter();
        const auto json = dir.getChildFile ("favourites.json").loadFileAsString();
        CHECK (f2.keys.size() == 2 && f2.is (m.entries[3].key()) && f2.is (m.entries[11].key()) && ! f2.is (m.entries[9].key()) && m.shown.size() == 2 && json.contains ("\"favourites\""),
               "favourites.json round trip: %d stars (%s), Favourites shows %d", (int) f2.keys.size(), json.replaceCharacters ("\r\n", "  ").toRawUTF8(), (int) m.shown.size());
        m.category = "All";
        auto legacy = dir.getChildFile ("old").getChildFile ("favourites.txt"); legacy.getParentDirectory().createDirectory();
        legacy.replaceWithText ("f:Sync Screamer\nu:My Ghost\n");
        Favourites f3; f3.open (legacy.getSiblingFile ("favourites.json"), legacy);
        Favourites f4; f4.open (legacy.getSiblingFile ("favourites.json"));
        CHECK (f3.keys.size() == 2 && f4.is ("f:Sync Screamer") && f4.is ("u:My Ghost"), "the first build's favourites.txt carries over into favourites.json (%d keys)", (int) f4.keys.size());
        // a preset file never holds a star: the key lives in the user's file only
        CHECK (! json.contains ("<"), "favourites are not written into any sound");
    }
    // < > and RANDOM inside a filter
    {
        m.category = "Bass"; m.refilter();
        const int n = (int) m.shown.size();
        const int first = m.shown[0], second = m.shown[1], last = m.shown[(size_t) n - 1];
        const bool st = m.step (first, 1) == second && m.step (first, -1) == last && m.step (last, 1) == first && m.step (factoryIndex ("Hyper Saw Anthem"), 1) == first && m.step (factoryIndex ("Hyper Saw Anthem"), -1) == last;
        juce::Random rng (42); bool rnd = true; std::set<int> seen;
        for (int k = 0; k < 200; ++k) { const int r = m.random (second, rng); rnd &= r != second && m.rowOf (r) >= 0; seen.insert (r); }
        m.category = "All"; m.tags = { "arp" }; m.refilter();
        bool arpWalk = true; int at = m.shown[0];
        for (int k = 0; k < (int) m.shown.size() + 2; ++k) { at = m.step (at, 1); arpWalk &= m.entries[(size_t) at].tags.contains ("arp"); }
        CHECK (st && rnd && (int) seen.size() >= n - 2 && arpWalk, "Bass (%d): next / prev wrap inside, from outside = first / last; RANDOM never the one playing, %d of %d reached; walking the 'arp' chip stays on arp sounds",
               n, (int) seen.size(), n - 1);
        m.tags.clear(); m.refilter();
    }
}

ProEditor* editorOf (std::unique_ptr<juce::AudioProcessorEditor>& ed) { return dynamic_cast<ProEditor*> (ed.get()); }
juce::TextButton* headerButton (juce::Component* content, const juce::String& text)
{
    for (auto* c : content->getChildren()) if (auto* b = dynamic_cast<juce::TextButton*> (c)) if (b->getButtonText() == text) return b;
    return nullptr;
}
void savePng (const juce::Image& img, const juce::File& f) { f.deleteFile(); if (auto os = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream())) juce::PNGImageFormat().writeImageToStream (img, *os); }

void editorTests (const juce::File& shots)
{
    section ("BROWSE page in the editor: header < > as before without a filter and inside one with it; tooltips; screenshots in every look");
    shots.createDirectory();
    auto p = makeProc (factoryIndex ("Hyper Saw Anthem"));
    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor());
    auto* pe = editorOf (ed); pe->setSize (ProEditor::W, ProEditor::H); ed->setVisible (true); pump (150);
    auto* content = pe->contentComponent();
    auto* next = headerButton (content, ">"); auto* prev = headerButton (content, "<");
    CHECK (next != nullptr && prev != nullptr, "header < > found");
    if (next == nullptr || prev == nullptr) return;
    const int start = p->getCurrentProgram();
    next->triggerClick(); pump (60);
    const int a = p->getCurrentProgram();
    prev->triggerClick(); pump (60);
    CHECK (a == start + 1 && p->getCurrentProgram() == start, "no filter: header > goes %d -> %d, < back to %d (as before)", start, a, p->getCurrentProgram());
    pe->showTab (3); pump (100);
    pe->browseTestFilter ({}, { "fm" }, false); pump (60);
    bool allFm = true;
    for (int k = 0; k < 6; ++k) { next->triggerClick(); pump (60); allFm &= gpro::factoryTags (p->getCurrentProgram()).contains ("fm"); }
    CHECK (allFm, "chip 'fm' set: six header > clicks stay on fm sounds (now %s)", p->getProgramName (p->getCurrentProgram()).toRawUTF8());
    p->setCurrentProgram (factoryIndex ("FM Lead Bright")); pump (120);
    const auto tip = pe->presetNameTooltip();
    CHECK (tip.contains ("Leads") && tip.contains ("fm") && tip.contains ("mono"), "header name tooltip: %s", tip.replaceCharacter ('\n', '|').toRawUTF8());
    // the VOICES readout tooltip
    juce::String vt; for (auto* c : content->getChildren()) if (auto* tc = dynamic_cast<juce::SettableTooltipClient*> (c)) if (tc->getTooltip().startsWith ("VOICES")) vt = tc->getTooltip();
    CHECK (vt.contains ("playing / the most"), "VOICES readout explains playing / max: %s", vt.substring (0, 80).toRawUTF8());
    // screenshots: BLACK / PURPLE / LIGHT; plain, a typed search + chip, the chip panel open
    static const char* themeName[] { "black", "purple", "light" };
    int saved = 0;
    for (int t : { 0, 1, 2 })
    {
        pe->setTheme (t); pump (120);
        pe->browseTestFilter ({}, {}, false); p->setCurrentProgram (factoryIndex ("Hyper Saw Anthem")); pump (200);
        savePng (pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f), shots.getChildFile (juce::String ("browse-") + themeName[t] + "-plain.png")); ++saved;
        pe->browseTestFilter ("lead", { "mono", "fm" }, false); pump (200);
        savePng (pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f), shots.getChildFile (juce::String ("browse-") + themeName[t] + "-filtered.png")); ++saved;
        pe->browseTestFilter ({}, { "unison" }, true); pump (200);
        auto img = pe->createComponentSnapshot (pe->getLocalBounds(), true, 1.0f);
        savePng (img, shots.getChildFile (juce::String ("browse-") + themeName[t] + "-tags.png")); ++saved;
        int holes = 0; for (int y = 0; y < img.getHeight(); y += 4) for (int x = 0; x < img.getWidth(); x += 4) holes += img.getPixelAt (x, y).getAlpha() < 255;
        CHECK (holes == 0 && pe->seeThroughPixels() == 0, "%s BROWSE with the chips open: alpha holes %d, see-through %d", themeName[t], holes, pe->seeThroughPixels());
    }
    pe->browseTestFilter ({}, {}, false); pe->setTheme (0); pump (50);
    // the AMP card's MORE pop-down with the VOICING controls
    {
        std::vector<juce::TextButton*> mores;
        std::function<void (juce::Component*)> walk = [&] (juce::Component* c) { for (auto* ch : c->getChildren()) { if (auto* b = dynamic_cast<juce::TextButton*> (ch)) if (b->getButtonText() == "MORE" && b->getTooltip().contains ("VOICING")) mores.push_back (b); walk (ch); } };
        pe->showTab (0); pump (60); walk (content);
        for (int t : { 0, 2 })
        {
            pe->setTheme (t); pump (80);
            const auto shot = shots.getChildFile (juce::String ("amp-more-voicing-") + themeName[t] + ".png"); shot.deleteFile();
            setenv ("GHOSTY_MORESHOT", shot.getFullPathName().toRawUTF8(), 1);
            if (! mores.empty()) mores[0]->triggerClick();
            pump (100); unsetenv ("GHOSTY_MORESHOT");
            saved += shot.existsAsFile();
        }
        CHECK (mores.size() == 1 && shots.getChildFile ("amp-more-voicing-black.png").existsAsFile(), "AMP MORE (QUALITY / VOICING) found and shot");
        pe->setTheme (0);
    }
    printf ("  %d screenshots in %s\n", saved, shots.getFullPathName().toRawUTF8());
}

int browseTests (const juce::File& shots)
{
    modelTests();
    editorTests (shots);
    printf ("\n%d failures\n", failures);
    return failures;
}

// ---------------------------------------------------------------- VOICING
std::vector<int> notesOf (ProProcessor& p) { int buf[64]; const int n = p.voiceNotes (buf, 64); std::vector<int> v (buf, buf + n); std::sort (v.begin(), v.end()); return v; }
juce::String str (const std::vector<int>& v) { juce::String s; for (int n : v) s << n << " "; return s.trim(); }

int stealTests()
{
    section ("VOICING: steal priority picks the voice it says (released voices first, then held), LIMIT SAME NOTE POLY TO 1, defaults unchanged");
    static const char* names[] { "Newest", "Oldest", "Highest", "Lowest", "Quietest" };
    {
        auto p = makeProc (-1); p->initPatch(); settle (*p);
        CHECK (p->sameNoteP->load() < 0.5f && (int) p->stealP->load() == gpro::stOldest, "INIT: 1 PER NOTE off, STEAL = %s (the old behaviour)", names[(int) p->stealP->load()]);
        auto q = makeProc (factoryIndex ("Hyper Saw Anthem"));
        CHECK (q->sameNoteP->load() < 0.5f && (int) q->stealP->load() == gpro::stOldest, "a factory sound: the same defaults");
    }
    // five voices, all released: A 60 (oldest, loud), B 62 (released early = quietest), C 72 (highest), D 48 (lowest), E 65 (newest); then 70
    const int expect[] { 65, 60, 72, 48, 62 };
    for (int prio = 0; prio < gpro::numSteal; ++prio)
    {
        auto p = makeProc (-1); p->initPatch();
        p->setParam ("poly", 5.0f); p->setParam ("attack", 1.0f); p->setParam ("decay", 0.0f); p->setParam ("sustain", 1.0f); p->setParam ("release", 4.0f);
        p->setParam ("v_steal", (float) prio); settle (*p);
        std::vector<Ev> ev { { 0.00, 60, 0.8f, true }, { 0.10, 62, 0.8f, true }, { 0.15, 62, 0, false }, { 0.20, 72, 0.8f, true }, { 0.30, 48, 0.8f, true }, { 0.40, 65, 0.8f, true },
                             { 0.95, 60, 0, false }, { 1.00, 72, 0, false }, { 1.00, 48, 0, false }, { 1.00, 65, 0, false } };
        render (*p, ev, 1.04);
        const auto before = notesOf (*p);
        auto out = render (*p, { { 0.0, 70, 0.8f, true } }, 0.05);
        const auto after = notesOf (*p);
        int stolen = -1; for (int n : before) if (std::find (after.begin(), after.end(), n) == after.end()) stolen = n;
        CHECK (before.size() == 5 && after.size() == 5 && std::find (after.begin(), after.end(), 70) != after.end() && stolen == expect[prio] && nansIn (out) == 0,
               "released voices, STEAL %-8s: [%s] + 70 -> [%s], took %d (want %d)", names[prio], str (before).toRawUTF8(), str (after).toRawUTF8(), stolen, expect[prio]);
    }
    // three held voices (no released one to take): 64 (oldest), 72 (highest), 55 (lowest, newest); then 67
    const int expectHeld[] { 55, 64, 72, 55, 55 };
    for (int prio = 0; prio < gpro::numSteal; ++prio)
    {
        auto p = makeProc (-1); p->initPatch();
        p->setParam ("poly", 3.0f); p->setParam ("attack", prio == gpro::stQuietest ? 1.0f : 0.001f); p->setParam ("sustain", 1.0f); p->setParam ("release", 0.5f);
        p->setParam ("v_steal", (float) prio); settle (*p);
        render (*p, { { 0.0, 64, 0.8f, true }, { 0.1, 72, 0.8f, true }, { 0.3, 55, 0.8f, true } }, 0.4);
        const auto before = notesOf (*p);
        render (*p, { { 0.0, 67, 0.8f, true } }, 0.03);
        const auto after = notesOf (*p);
        int stolen = -1; for (int n : before) if (std::find (after.begin(), after.end(), n) == after.end()) stolen = n;
        CHECK (stolen == expectHeld[prio] && after.size() == 3, "held voices, STEAL %-8s: [%s] + 67 -> [%s], took %d (want %d)", names[prio], str (before).toRawUTF8(), str (after).toRawUTF8(), stolen, expectHeld[prio]);
    }
    // LIMIT SAME NOTE POLY TO 1: the same key again while it still rings
    for (int lim = 0; lim < 2; ++lim)
    {
        auto p = makeProc (-1); p->initPatch(); p->setParam ("release", 2.0f); p->setParam ("v_same1", (float) lim);
        p->setParam ("o1_tbl", (float) gpro::tClassic); p->setParam ("o1_pos", 0.0f); p->setParam ("verb", 0.0f); p->setParam ("echo", 0.0f); settle (*p);   // a sine: a click would stand out
        auto out = render (*p, { { 0.0, 60, 0.8f, true }, { 0.2, 60, 0, false }, { 0.3, 60, 0.8f, true }, { 0.5, 60, 0, false }, { 0.6, 60, 0.8f, true } }, 0.7);
        const auto v = notesOf (*p);
        const int n60 = (int) std::count (v.begin(), v.end(), 60);
        // no click where the voice is taken over: the biggest sample-to-sample step stays near the sound's own
        float jump = 0, own = 0;
        for (int i = 1; i < out.getNumSamples(); ++i)
        {
            const float d = std::abs (out.getSample (0, i) - out.getSample (0, i - 1));
            if (std::abs (i - (int) (0.3 * SR)) < 400 || std::abs (i - (int) (0.6 * SR)) < 400) jump = std::max (jump, d); else if (i > (int) (0.05 * SR)) own = std::max (own, d);
        }
        CHECK ((lim ? n60 == 1 : n60 == 3) && nansIn (out) == 0 && jump <= own * 1.5f + 1.0e-4f, "1 PER NOTE %s: three hits of C4 while it rings -> %d voice(s) on it; step at the retrigger %.4f vs the sound's own %.4f",
               lim ? "on " : "off", n60, jump, own);
    }
    // the parameters are part of a sound (saved + loaded) and old states load the defaults
    {
        auto p = makeProc (-1); p->initPatch(); p->setParam ("v_same1", 1.0f); p->setParam ("v_steal", (float) gpro::stQuietest); settle (*p);
        juce::MemoryBlock mb; p->getStateInformation (mb);
        auto q = makeProc (0); q->setStateInformation (mb.getData(), (int) mb.getSize()); settle (*q);
        auto xml = juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
        auto tree = juce::ValueTree::fromXml (*xml);
        for (auto* id : { "v_same1", "v_steal" }) tree.removeChild (tree.getChildWithProperty ("id", id), nullptr);
        juce::MemoryBlock old; juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), old);
        q->setStateInformation (old.getData(), (int) old.getSize()); settle (*q);
        CHECK (q->sameNoteP->load() < 0.5f && (int) q->stealP->load() == gpro::stOldest, "a state without them loads 1 PER NOTE off, STEAL Oldest");
        auto r = makeProc (0); r->setStateInformation (mb.getData(), (int) mb.getSize()); settle (*r);
        CHECK (r->sameNoteP->load() > 0.5f && (int) r->stealP->load() == gpro::stQuietest, "saved + reloaded: 1 PER NOTE on, STEAL Quietest");
    }
    printf ("\n%d failures\n", failures);
    return failures;
}
} // namespace browsetest
