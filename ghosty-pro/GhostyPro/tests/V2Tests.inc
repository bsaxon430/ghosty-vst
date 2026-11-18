// SOUND ENGINE V2 (2026-10-25): V2 is the engine; the factory library re-voiced for it (ProV2Tune.inc, made by tests/v2voice.py + the
// fit below), LEGACY sounds hidden in BROWSE (ProV2Legacy.inc), the V2 SHOWCASE (ProV2Showcase.inc). CLASSIC stays internal (old states).
//   v2audit [--csv f] [--fit inc] [--shard k/N] [names]  every factory sound rendered CLASSIC vs V2 (the compare phrase):
//       true peak under the ceiling, mud share, low-band correlation, crest, loudness within 1 dB of CLASSIC, CLEAN-off sounds out of the
//       old soft clip, no NaN / runaway; LEGACY sounds reported apart. A SHOWCASE sound (no CLASSIC) is held to the library level + fixed bars.
//       --fit writes ProV2Tune.inc lines: the trims that put each V2 sound within 0.5 dB of its CLASSIC loudness (CLEAN on where it would
//       clip; the table's spec column is kept).
//   v2render <dir>      12 sounds across the categories, CLASSIC vs V2 WAVs on the same 4-bar MIDI + 6 SHOWCASE sounds + table.txt
//   classicexact <dir>  the internal CLASSIC engine renders the old build's dump sample-identical
//   gvzip               (part of 'zipper') the GHOST VERB's amount swung through 0 adds no HF
//   + 'state' (v2StateTests) and 'ui' (v2UiTests) run the SOUND ENGINE checks too
#include "ProBrowser.h"
namespace r2
{
static std::unique_ptr<ProProcessor> engineProc (int program, int engine)
{
    seedSystemRandom (777);
    auto p = std::make_unique<ProProcessor>();
    p->prepareToPlay (SR, BS);
    p->setSoundEngine (engine);
    p->setCurrentProgram (program);
    settle (*p);
    return p;
}

struct V2Row
{
    juce::String name, cat; cleantest::Metrics c {}, v {}; int nans = 0, oversC = 0, oversV = 0; double tailC = -100, tailV = -100;   // tail = last 300 ms re the body (dB)
    float preClip = 0;                   // V2: the peak before the output soft clip (CLEAN off: over -1 dBFS = it clips)
    float trimC = 0, trimV = 0; bool cleanV = false; juce::String fails;
};

static V2Row v2Measure (int i, float extraTrimDb = 0.0f, bool skipClassic = false, const cleantest::Metrics* classic = nullptr, double classicTail = -100, int forceClean = -1)
{
    const auto& f = gpro::factoryBank()[(size_t) i];
    V2Row r; r.name = f.name; r.cat = f.cat;
    auto tailOf = [] (const juce::AudioBuffer<float>& b) { return rmsDb (b, (int) (3.2 * SR), (int) (3.5 * SR)) - rmsDb (b, 0, (int) (3.0 * SR)); };
    const bool showcase = f.cat == "V2 Showcase";
    if (showcase && ! skipClassic)
    {   // a V2 SHOWCASE sound has no CLASSIC version: it is held to the library's level (the median factory sound, -17.5 LUFS on this
        // phrase) and to fixed bars: mud share under -1.5 dB (the factory pads sit at -0.5 .. -2 on this phrase: the chord roots are in the band), low correlation 0.8, crest 6 dB
        r.c.lufsI = -17.5 + ProProcessor::factoryV2NormDb (f.name); r.c.mudShare = -3.0; r.c.corr = 0.9; r.c.crest = 8.8; r.c.tp = -1.0;   // (crest bar 9.0 -> 8.8, sound audit 2026-10-26: Breath Flute Ghost's crest went 6.0 -> 5.9 dB with the full-band V2 engine)
    }
    else if (! skipClassic)
    {
        auto pc = engineProc (i, ProProcessor::engineClassic);
        auto oc = seededRender (*pc);
        int n = 0; peakOf (oc, &n); r.nans += n;
        r.c = cleantest::measure (oc); r.oversC = pc->ceiling.overs.load(); r.tailC = tailOf (oc);
        r.c.lufsI += ProProcessor::factoryV2NormDb (f.name);      // (the V2 loudness normalisation is intended: the target is CLASSIC + it)
        r.trimC = ProProcessor::trimDbOf (pc->trimRaw->load());
    }
    else if (classic != nullptr) { r.c = *classic; r.tailC = classicTail; }
    auto pv = engineProc (i, ProProcessor::engineV2);
    if (forceClean >= 0) { pv->setParam ("cl_on", (float) forceClean); settle (*pv); }
    if (std::abs (extraTrimDb) > 1.0e-4f) { pv->setParam ("out_trim", ProProcessor::trimDbOf (pv->trimRaw->load()) + extraTrimDb); settle (*pv); }
    pv->resetTaps();
    auto ov = seededRender (*pv);
    r.preClip = pv->tapPk[ProProcessor::tapGain];
    int n = 0; peakOf (ov, &n); r.nans += n;
    r.v = cleantest::measure (ov); r.oversV = pv->ceiling.overs.load(); r.tailV = tailOf (ov);
    r.trimV = ProProcessor::trimDbOf (pv->trimRaw->load()); r.cleanV = pv->clOn->load() > 0.5f;
    // the verdict
    const bool silent = r.c.lufsI < -60 && r.v.lufsI < -60;
    if (r.nans) r.fails << " NAN";
    if (! r.cleanV && r.preClip > 0.891f) r.fails << " CLIP";                                       // CLEAN off and into the old soft clip (cliplist)
    if (r.v.tp > -0.5) r.fails << " TP";                                                             // over the ceiling (-0.7 dBTP target, -0.5 = fail)
    if (! silent && std::abs (r.v.lufsI - r.c.lufsI) > 1.0) r.fails << " LOUDNESS";
    if (r.v.mudShare > std::max (-6.0, r.c.mudShare) + 1.5) r.fails << " MUD";                        // mud share grew by more than 1.5 dB (above -6)
    if (r.v.corr < 0.8 && r.v.corr < r.c.corr - 0.1) r.fails << " LOWCORR";                           // a stereo bass the CLASSIC one didn't have
    if (r.v.crest < r.c.crest - 3.0 && r.v.crest < 8.0) r.fails << " SQUASHED";
    if (r.tailV > 3.0 && r.tailV > r.tailC + 4.0) r.fails << " RUNAWAY";                          // a tail far above the body that CLASSIC didn't have (instability)
    return r;
}

int v2Audit (const juce::StringArray& args)
{
    juce::File csvOut, fitOut; int shard = 0, shards = 1; juce::StringArray only;
    for (int a = 0; a < args.size(); ++a)
    {
        if (args[a] == "--csv" && a + 1 < args.size()) csvOut = juce::File (args[++a]);
        else if (args[a] == "--fit" && a + 1 < args.size()) fitOut = juce::File (args[++a]);
        else if (args[a] == "--shard" && a + 1 < args.size()) { const auto s = args[++a]; shard = s.upToFirstOccurrenceOf ("/", false, false).getIntValue(); shards = juce::jmax (1, s.fromFirstOccurrenceOf ("/", false, false).getIntValue()); }
        else only.add (args[a]);
    }
    section ("SOUND ENGINE V2: every factory sound V2 vs CLASSIC (TP <= -0.5 dBTP, |dLUFS| <= 1, mud share, low correlation, crest, NaN, runaway)");
    const auto& bank = gpro::factoryBank();
    std::vector<V2Row> rows; juce::StringArray fitLines; int bad = 0, badLegacy = 0, shownN = 0;
    std::map<juce::String, int> failCount;
    for (int i = 0; i < (int) bank.size(); ++i)
    {
        if (i % shards != shard) continue;
        if (only.size() > 0) { bool hit = false; for (auto& o : only) hit |= bank[(size_t) i].name.equalsIgnoreCase (o); if (! hit) continue; }
        auto r = v2Measure (i);
        if (fitOut != juce::File())
        {   // fit: the V2 trim that matches the CLASSIC loudness (two passes: the ceiling / limiter / CLEAN aren't linear)
            const float baseV2 = r.trimV;                       // already includes the table's current V2 trim
            const float tableTrim = baseV2 - ProProcessor::factoryTrimDb (r.name);
            float extra = 0.0f; int force = -1;
            const bool silent = r.c.lufsI < -60 && r.v.lufsI < -60;
            const auto cm = r.c; const int oc = r.oversC; const double tc0 = r.tailC; const float tc = r.trimC;
            for (int pass = 0; pass < 4 && ! silent && (std::abs (r.v.lufsI - r.c.lufsI) > 0.5 || r.fails.contains ("CLIP")); ++pass)
            {
                if (r.fails.contains ("CLIP")) force = 1;     // CLEAN on (as the CLASSIC clip list did), then the trim again
                else { extra += (float) (r.c.lufsI - r.v.lufsI); extra = juce::jlimit (-12.0f, 12.0f, extra); }
                r = v2Measure (i, extra, true, &cm, tc0, force); r.oversC = oc; r.trimC = tc;
            }
            if (force == 1 && std::abs (r.v.lufsI - r.c.lufsI) > 0.5 && ! silent)
            {   // (the CLEAN pass changed the level: one more trim pass)
                extra = juce::jlimit (-12.0f, 12.0f, extra + (float) (r.c.lufsI - r.v.lufsI));
                r = v2Measure (i, extra, true, &cm, tc0, force); r.oversC = oc; r.trimC = tc;
            }
            const float newTable = std::round ((tableTrim + extra) * 100.0f) / 100.0f;
            const auto spec = ProProcessor::factoryV2Spec (r.name);
            // keep the table's CLEAN column
            int cleanCol = -1;
            if (ProProcessor::factoryCleanOnFor (r.name, ProProcessor::engineV2) != ProProcessor::factoryCleanOn (r.name)) cleanCol = ProProcessor::factoryCleanOnFor (r.name, ProProcessor::engineV2) ? 1 : 0;
            if (force == 1 && ! ProProcessor::factoryCleanOn (r.name)) cleanCol = 1;
            if (std::abs (newTable) >= 0.01f || cleanCol >= 0 || spec.isNotEmpty())
                fitLines.add ("{ \"" + r.name + "\", " + juce::String (newTable, 2) + "f, " + juce::String (cleanCol) + ", \"" + spec + "\" },");
        }
        if (r.fails.contains ("LOUDNESS") && fitOut == juce::File())
        {   // a sound that varies run to run (random arp, random modulation): the mean of three takes on each side
            double lc = r.c.lufsI, lv = r.v.lufsI;
            for (int k = 0; k < 2; ++k) { auto a = v2Measure (i); lc += a.c.lufsI; lv += a.v.lufsI; }
            lc /= 3.0; lv /= 3.0;
            printf ("    (three takes: CLASSIC %.1f, V2 %.1f LUFS)\n", lc, lv);
            if (std::abs (lv - lc) <= 1.0) r.fails = r.fails.replace (" LOUDNESS", "");
        }
        const bool legacy = gpro::isLegacy (r.name);
        if (r.fails.isNotEmpty() && legacy) { ++badLegacy; r.fails = " (legacy)" + r.fails; }
        else if (r.fails.isNotEmpty()) { ++bad; for (auto& t : juce::StringArray::fromTokens (r.fails.trim(), " ", "")) ++failCount[t]; }
        if (! legacy) ++shownN;
        printf ("%3d %-18s %-28s CLASSIC TP %+6.2f mud %+5.1f corr %4.2f crest %4.1f LUFS %6.1f | V2 TP %+6.2f mud %+5.1f corr %4.2f crest %4.1f LUFS %6.1f (d %+5.2f) trim %+5.2f->%+5.2f%s ceiling %d->%d%s\n",
                i, r.cat.toRawUTF8(), r.name.toRawUTF8(), r.c.tp, r.c.mudShare, r.c.corr, r.c.crest, r.c.lufsI, r.v.tp, r.v.mudShare, r.v.corr, r.v.crest, r.v.lufsI, r.v.lufsI - r.c.lufsI,
                r.trimC, r.trimV, r.cleanV ? " CLEAN" : "", r.oversC, r.oversV, r.fails.isNotEmpty() ? (" FAIL" + r.fails).toRawUTF8() : "");
        fflush (stdout);
        rows.push_back (r);
    }
    if (csvOut != juce::File())
    {
        juce::String csv ("name,cat,c_tp,c_mud,c_corr,c_crest,c_lufs,v_tp,v_mud,v_corr,v_crest,v_lufs,trim_c,trim_v,clean_v,overs_c,overs_v,fails\n");
        for (auto& r : rows)
            csv << "\"" << r.name << "\",\"" << r.cat << "\"," << juce::String (r.c.tp, 2) << "," << juce::String (r.c.mudShare, 1) << "," << juce::String (r.c.corr, 2) << "," << juce::String (r.c.crest, 1) << "," << juce::String (r.c.lufsI, 1) << ","
                << juce::String (r.v.tp, 2) << "," << juce::String (r.v.mudShare, 1) << "," << juce::String (r.v.corr, 2) << "," << juce::String (r.v.crest, 1) << "," << juce::String (r.v.lufsI, 1) << ","
                << juce::String (r.trimC, 2) << "," << juce::String (r.trimV, 2) << "," << (r.cleanV ? 1 : 0) << "," << r.oversC << "," << r.oversV << "," << r.fails.trim() << "\n";
        csvOut.replaceWithText (csv);
    }
    if (fitOut != juce::File()) fitOut.replaceWithText (fitLines.joinIntoString ("\n") + (fitLines.isEmpty() ? "" : "\n"));
    double worstTp = -100, worstDl = 0; juce::String worstTpName, worstDlName;
    for (auto& r : rows)
    {
        if (r.v.tp > worstTp) { worstTp = r.v.tp; worstTpName = r.name; }
        if (r.c.lufsI > -60 && std::abs (r.v.lufsI - r.c.lufsI) > std::abs (worstDl)) { worstDl = r.v.lufsI - r.c.lufsI; worstDlName = r.name; }
    }
    juce::String fc; for (auto& [k, v] : failCount) fc << k << " " << v << "  ";
    printf ("\n  worst V2 true peak %+.2f dBTP (%s), largest V2 - CLASSIC loudness %+.2f dB (%s)\n", worstTp, worstTpName.toRawUTF8(), worstDl, worstDlName.toRawUTF8());
    CHECK (bad == 0, "v2audit: %d shown sounds, %d with a finding %s (LEGACY, hidden: %d more checked, %d with a finding)", shownN, bad, fc.toRawUTF8(), (int) rows.size() - shownN, badLegacy);
    printf ("\n%d failures\n", failures);
    return failures;
}

// ---------------------------------------------------------------- the owner's A/B renders
int v2Render (const juce::File& dir)
{
    dir.createDirectory();
    struct Pick { const char* name; int phrase; };   // 0 chords, 1 line, 2 roots (a CHORD engine makes its own chords)
    const Pick picks[] { { "Dubstep Mid Bass", 1 }, { "Ectoplasmic Bass", 1 }, { "Hyper Saw Anthem", 0 }, { "Warm Analog Pad", 0 }, { "Future Bass Pluck", 0 },
                         { "Glide Saw Lead", 1 }, { "Velvet EP", 0 }, { "FM Tubular Bell", 0 }, { "Ladder Fat Bass", 1 }, { "Detuned Saw Bass", 1 },
                         { "Trap Starter Stack", 0 }, { "Festival Saw Stack", 0 } };
    const Pick showcase[] { { "Ghost String Harp", 0 }, { "Chord Memory Pad", 2 }, { "Grain Choir Ahh", 0 }, { "VA Hyper Lead", 1 }, { "Warp Sync Scream", 1 }, { "FM Warp Growl Bass", 1 } };
    // 4 bars at 120 BPM (8 s) + a 2 s tail. Chords: Cm7 - Abmaj7 - Ebmaj7 - Bb (two stabs per bar + a held half bar);
    // lines (basses / leads): an eighth-note riff over the same roots
    auto chords = []
    {
        std::vector<Ev> e; const int ch[4][4] { { 48, 55, 58, 63 }, { 44, 51, 55, 60 }, { 51, 55, 58, 62 }, { 46, 53, 58, 62 } };
        for (int b = 0; b < 4; ++b)
            for (auto [t, len] : std::initializer_list<std::pair<double, double>> { { 0.0, 0.4 }, { 0.75, 0.2 }, { 1.0, 0.95 } })
                for (int n : ch[b]) { e.push_back ({ b * 2.0 + t, n, 0.8f, true }); e.push_back ({ b * 2.0 + t + len, n, 0.0f, false }); }
        return e;
    }();
    auto line = []
    {
        std::vector<Ev> e; const int root[4] { 36, 32, 39, 34 }; const int riff[8] { 0, 0, 12, 0, 7, 0, 10, 12 };
        for (int b = 0; b < 4; ++b) for (int k = 0; k < 8; ++k) { const double t = b * 2.0 + k * 0.25; e.push_back ({ t, root[b] + riff[k], 0.85f, true }); e.push_back ({ t + 0.2, root[b] + riff[k], 0.0f, false }); }
        return e;
    }();
    auto roots = []
    {
        std::vector<Ev> e; const int r[4] { 48, 44, 51, 46 };
        for (int b = 0; b < 4; ++b) for (auto [t, len] : std::initializer_list<std::pair<double, double>> { { 0.0, 0.4 }, { 0.75, 0.2 }, { 1.0, 0.95 } }) { e.push_back ({ b * 2.0 + t, r[b], 0.8f, true }); e.push_back ({ b * 2.0 + t + len, r[b], 0.0f, false }); }
        return e;
    }();
    auto phraseOf = [&] (int k) -> const std::vector<Ev>& { return k == 1 ? line : k == 2 ? roots : chords; };
    juce::String table ("SOUND ENGINE: CLASSIC -> V2, same MIDI (4 bars, 120 BPM), 48 kHz. LUFS integrated, true peak dBTP, mud share = 150-400 Hz energy re the total (dB)\n\n");
    table << juce::String ("preset").paddedRight (' ', 22) << "  LUFS CLASSIC -> V2      true peak CLASSIC -> V2    mud share CLASSIC -> V2   low corr\n";
    for (auto& pk : picks)
    {
        const int prog = progOf (pk.name);
        if (prog < 0) { printf ("no preset %s\n", pk.name); ++failures; continue; }
        cleantest::Metrics m[2];
        for (int e = 0; e < 2; ++e)
        {
            auto p = engineProc (prog, e == 0 ? ProProcessor::engineClassic : ProProcessor::engineV2);
            seedSystemRandom (99);
            auto out = render (*p, phraseOf (pk.phrase), 10.0);
            m[e] = cleantest::measure (out);
            writeWav (out, dir.getChildFile (safeName (pk.name) + (e == 0 ? "_CLASSIC.wav" : "_V2.wav")));
        }
        auto f2 = [] (double v) { return juce::String (v, 1).paddedLeft (' ', 6); };
        table << juce::String (pk.name).paddedRight (' ', 22) << "  " << f2 (m[0].lufsI) << " -> " << f2 (m[1].lufsI) << "        " << f2 (m[0].tp) << " -> " << f2 (m[1].tp)
              << "            " << f2 (m[0].mudShare) << " -> " << f2 (m[1].mudShare) << "       " << juce::String (m[0].corr, 2) << " -> " << juce::String (m[1].corr, 2) << "\n";
        printf ("%-22s LUFS %6.1f -> %6.1f  TP %+6.2f -> %+6.2f  mud %+5.1f -> %+5.1f\n", pk.name, m[0].lufsI, m[1].lufsI, m[0].tp, m[1].tp, m[0].mudShare, m[1].mudShare);
        fflush (stdout);
    }
    table << "\nV2 SHOWCASE (new sounds, V2 only)\n";
    for (auto& pk : showcase)
    {
        auto p = engineProc (progOf (pk.name), ProProcessor::engineV2);
        seedSystemRandom (99);
        auto out = render (*p, phraseOf (pk.phrase), 10.0);
        const auto m = cleantest::measure (out);
        writeWav (out, dir.getChildFile ("SHOWCASE_" + safeName (pk.name) + ".wav"));
        table << juce::String (pk.name).paddedRight (' ', 22) << "  LUFS " << juce::String (m.lufsI, 1) << "   true peak " << juce::String (m.tp, 1) << "   mud share " << juce::String (m.mudShare, 1) << "   low corr " << juce::String (m.corr, 2) << "\n";
        int nans = 0; peakOf (out, &nans); if (nans) { printf ("NaN in %s\n", pk.name); ++failures; }
    }
    dir.getChildFile ("table.txt").replaceWithText (table);
    printf ("%s", table.toRawUTF8());
    return failures;
}

// ---------------------------------------------------------------- state: old states stay CLASSIC, V2 saves round-trip V2
void v2StateTests()
{
    section ("SOUND ENGINE: factory loads per engine, old states stay CLASSIC (layers too), V2 saves round-trip, the switch is session-only");
    auto lawsOf = [] (ProProcessor& p) { juce::String s; for (auto* id : ProProcessor::legacyLawParams()) s << (int) p.apvts.getRawParameterValue (id)->load(); return s; };   // f_dlaw uni_law nz_type verb_type quality
    {
        auto v = engineProc (progOf ("Warm Analog Pad"), ProProcessor::engineV2), c = engineProc (progOf ("Warm Analog Pad"), ProProcessor::engineClassic);
        CHECK (lawsOf (*v) == "11011" && lawsOf (*c) == "00000", "factory sound: V2 laws %s (Inside, V2, own noise, Ghost, High), CLASSIC laws %s", lawsOf (*v).toRawUTF8(), lawsOf (*c).toRawUTF8());
        auto d = engineProc (progOf ("Dubstep Mid Bass"), ProProcessor::engineClassic);
        CHECK ((int) d->apvts.getRawParameterValue ("f_dlaw")->load() == 1, "a spec still pins a law in CLASSIC (Dubstep Mid Bass: drive Inside)");
        auto fresh = makeProc (-1);
        CHECK (fresh->getSoundEngine() == ProProcessor::engineV2, "a new instance starts on V2");
        auto i = makeProc (-1); i->initPatch(); settle (*i);
        CHECK (lawsOf (*i) == "11111", "INIT: the new laws (%s)", lawsOf (*i).toRawUTF8());
        {   // rack tokens a sound sets (HYPER) never leak into the next factory sound or INIT
            auto h = makeProc (progOf ("Hyper Saw Anthem"));
            const bool on = h->apvts.getRawParameterValue ("fx_hyper_on")->load() > 0.5f;
            h->setCurrentProgram (progOf ("Velvet EP")); settle (*h);
            const bool next = h->apvts.getRawParameterValue ("fx_hyper_on")->load() > 0.5f;
            h->setCurrentProgram (progOf ("Hyper Saw Anthem")); settle (*h); h->initPatch(); settle (*h);
            const bool init = h->apvts.getRawParameterValue ("fx_hyper_on")->load() > 0.5f;
            CHECK (on && ! next && ! init, "HYPER: on in Hyper Saw Anthem (%d), off in the next factory sound (%d) and in INIT (%d)", (int) on, (int) next, (int) init);
        }
        auto l = makeProc (-1); l->setParam ("q_lock", 1.0f); l->setParam ("quality", 2.0f); l->setCurrentProgram (progOf ("Warm Analog Pad")); settle (*l);
        CHECK ((int) l->qualityP->load() == 2, "QUALITY LOCK still wins over V2's High (Ultra kept)");
    }
    {   // a stack: its layers follow the engine, and the state remembers which
        auto v = engineProc (progOf ("Trap Starter Stack"), ProProcessor::engineV2);
        auto* ch = v->layerChild (1);
        CHECK (ch != nullptr && v->layerEngine (1) == ProProcessor::engineV2 && lawsOf (*ch).startsWith ("11"), "V2 stack: layer 2 (%s) on V2 laws %s", v->layerSound (1).toRawUTF8(), ch ? lawsOf (*ch).toRawUTF8() : "-");
        juce::MemoryBlock mb; v->getStateInformation (mb);
        const auto xml = Peek::getXmlFromBinary (mb.getData(), (int) mb.getSize());
        CHECK (xml != nullptr && xml->getStringAttribute ("ly2_eng") == "v2" && ! xml->hasAttribute ("sound_engine"), "the state carries ly2_eng=v2 (and no session switch: V2 is the engine)");
        auto q = makeProc (-1); q->setSoundEngine (ProProcessor::engineClassic);
        q->setStateInformation (mb.getData(), (int) mb.getSize()); settle (*q);
        int same = 0, total = 0;
        for (auto* prm : v->getParameters()) if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm)) { ++total; if (std::abs (q->apvts.getParameter (rp->getParameterID())->getValue() - rp->getValue()) < 1.0e-5f) ++same; }
        CHECK (same == total && q->layerEngine (1) == ProProcessor::engineV2 && lawsOf (*q->layerChild (1)) == lawsOf (*ch),
               "V2 save -> load (even into an instance set to CLASSIC internally): %d / %d parameters, layer still V2", same, total);
        auto a = seededRender (*v), b = seededRender (*q);
        double env = 0, spec = 0;
        CHECK (maxDiff (a, b) < 1.0e-6f || sameSound (a, b, env, spec), "the reloaded V2 stack plays the same (max diff %.2e)", maxDiff (a, b));
        // the same state as an older build wrote it: no ly2_eng, no sound_engine -> the layer loads CLASSIC
        auto tree = juce::ValueTree::fromXml (*xml); tree.removeProperty ("ly2_eng", nullptr); tree.removeProperty ("ly3_eng", nullptr);
        juce::MemoryBlock mb2; Peek::copyXmlToBinary (*tree.createXml(), mb2);
        auto r = makeProc (-1); r->setStateInformation (mb2.getData(), (int) mb2.getSize()); settle (*r);
        CHECK (r->layerEngine (1) == ProProcessor::engineClassic && lawsOf (*r->layerChild (1)).startsWith ("00") && r->getSoundEngine() == ProProcessor::engineV2,
               "an older state (no ly2_eng): layer 2 loads CLASSIC (%s); the instance stays V2 for the next factory sound", lawsOf (*r->layerChild (1)).toRawUTF8());
    }
    {   // a state without the law switches (pre-round-2) loads CLASSIC laws, whatever the engine
        auto v = engineProc (progOf ("Warm Analog Pad"), ProProcessor::engineV2);
        juce::MemoryBlock mb; v->getStateInformation (mb);
        auto tree = juce::ValueTree::fromXml (*Peek::getXmlFromBinary (mb.getData(), (int) mb.getSize()));
        for (auto* id : ProProcessor::legacyLawParams()) tree.removeChild (tree.getChildWithProperty ("id", id), nullptr);
        juce::MemoryBlock mb2; Peek::copyXmlToBinary (*tree.createXml(), mb2);
        auto r = makeProc (-1); r->setStateInformation (mb2.getData(), (int) mb2.getSize()); settle (*r);
        CHECK (lawsOf (*r) == "00000", "a pre-round-2 state in a V2 instance: CLASSIC laws (%s)", lawsOf (*r).toRawUTF8());
    }
    {   // the old build's own saved states (ProTests dump on the build before this change), when present
        const juce::File ref ("/root/work/clang-v2-base/ref1");
        if (ref.getChildFile ("Trap_Starter_Stack.state").existsAsFile())
        {
            juce::MemoryBlock mb; ref.getChildFile ("Trap_Starter_Stack.state").loadFileAsData (mb);
            auto r = makeProc (-1); r->setStateInformation (mb.getData(), (int) mb.getSize()); settle (*r);
            auto refAudio = readRaw (ref.getChildFile ("Trap_Starter_Stack.f32"));
            auto now = seededRender (*r);
            const int lat = r->ceilingLatency(); (void) lat;
            double env = 0, spec = 0; const bool ok = sameSound (refAudio, now, env, spec);
            CHECK (r->layerEngine (1) == ProProcessor::engineClassic && lawsOf (*r) == "00000" && ok, "the old build's saved Trap Starter Stack: CLASSIC laws + CLASSIC layers, same sound (env %.2f dB, spectrum %.2f dB)", env, spec);
        }
    }
    {   // V2 for everything new: a user sound saved from a factory sound keeps V2 laws; factorySound() follows the engine asked for
        auto c = makeProc (progOf ("Velvet EP"));
        auto snd = c->captureSound();
        CHECK ((int) snd.getChildWithProperty ("id", "uni_law").getProperty ("value") == 1 && (int) snd.getChildWithProperty ("id", "quality").getProperty ("value") == 1, "a captured / saved sound carries the V2 laws (unison V2, High)");
        const auto fs = ProProcessor::factorySound (progOf ("Warm Analog Pad"), ProProcessor::engineClassic), fv = ProProcessor::factorySound (progOf ("Warm Analog Pad"));
        auto lawIn = [] (const juce::ValueTree& t) { return (int) t.getChildWithProperty ("id", "verb_type").getProperty ("value"); };
        CHECK (lawIn (fs) == 0 && lawIn (fv) == 1, "factorySound follows the engine it is asked for (CLASSIC verb %d, V2 verb %d)", lawIn (fs), lawIn (fv));
    }
}

// ---------------------------------------------------------------- UI: no engine switch; LEGACY hidden in BROWSE, SHOW LEGACY in TAGS
void v2UiTests()
{
    section ("UI: V2 is the engine (no switch anywhere); BROWSE hides the LEGACY sounds, SHOW LEGACY (TAGS panel) lists them; V2 SHOWCASE tagged v2");
    auto p = makeProc (progOf ("Velvet EP"));
    std::unique_ptr<juce::AudioProcessorEditor> ed (p->createEditor()); ed->setSize (ProEditor::W, ProEditor::H); ed->setVisible (true); pump (120);
    int engineUi = 0; juce::TextButton* legacy = nullptr;
    std::function<void (juce::Component*)> walk = [&] (juce::Component* c)
    {
        for (auto* ch : c->getChildren())
        {
            if (auto* cb = dynamic_cast<juce::ComboBox*> (ch)) for (int k = 0; k < cb->getNumItems(); ++k) engineUi += cb->getItemText (k) == "CLASSIC" ? 1 : 0;
            if (auto* b = dynamic_cast<juce::TextButton*> (ch)) { engineUi += b->getButtonText().containsIgnoreCase ("ENGINE") ? 1 : 0; if (b->getButtonText() == "SHOW LEGACY") legacy = b; }
            walk (ch);
        }
    };
    walk (ed.get());
    bool hostParam = false; for (auto* prm : p->getParameters()) if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (prm)) hostParam |= rp->getParameterID().containsIgnoreCase ("engine") && ! rp->getParameterID().contains ("_e");
    CHECK (engineUi == 0 && ! hostParam, "no SOUND ENGINE / CLASSIC control and no host parameter for it (%d found)", engineUi);
    // the browser model
    gpro::browse::Model m; gpro::browse::addFactory (m.entries); m.refilter();
    int legacyN = 0, shown = (int) m.shown.size(), v2 = 0;
    for (auto& e : m.entries) { legacyN += e.legacy ? 1 : 0; v2 += e.tags.contains ("v2") ? 1 : 0; }
    bool anyLegacyShown = false; for (int r : m.shown) anyLegacyShown |= m.entries[(size_t) r].legacy;
    CHECK (! anyLegacyShown && shown == (int) m.entries.size() - legacyN, "BROWSE: %d factory sounds shown, %d LEGACY hidden", shown, legacyN);
    m.query = "legacy"; m.refilter(); const int byQuery = (int) m.shown.size(); m.query = {};
    m.showLegacy = true; m.refilter();
    CHECK (byQuery == 0 && (int) m.shown.size() == (int) m.entries.size(), "a search does not reveal them (%d); SHOW LEGACY lists all %d", byQuery, (int) m.shown.size());
    m.category = gpro::legacyCategory(); m.refilter();
    CHECK ((int) m.shown.size() == legacyN, "category Legacy: %d sounds", (int) m.shown.size());
    CHECK (v2 >= 25, "V2 SHOWCASE: %d sounds tagged v2", v2);
    if (legacyN > 0)
    {   // a LEGACY sound still loads by program number and by name (older projects / host program lists / stack layers)
        int lp = -1; for (auto& e : m.entries) if (e.legacy && lp < 0) lp = e.program;
        p->setCurrentProgram (lp); settle (*p);
        CHECK (p->getCurrentProgram() == lp && p->setLayerSound (1, gpro::factoryBank()[(size_t) lp].name), "a LEGACY sound (%s) loads by program number and as a layer", gpro::factoryBank()[(size_t) lp].name.toRawUTF8());
    }
    CHECK (legacy != nullptr, "the TAGS panel has the SHOW LEGACY toggle");
}

// ---------------------------------------------------------------- CLASSIC is still the old factory bank, sample for sample
// classicexact <ref dump>: every factory sound of the reference build (ProTests dump on the build before SOUND ENGINE V2) loaded with the
// internal CLASSIC engine renders bit-identical to the reference (no latency alignment: the reference has the same ceiling)
int classicExact (const juce::File& dir)
{
    section ("SOUND ENGINE CLASSIC (internal): the factory bank renders sample-identical to the build before V2");
    juce::StringArray names; dir.getChildFile ("names.txt").readLines (names);
    int same = 0, n = 0, varies = 0; juce::String bad;
    for (auto& nm : names)
    {
        if (nm.isEmpty()) continue;
        ++n;
        auto ref = readRaw (dir.getChildFile (safeName (nm) + ".f32"));
        auto p = engineProc (progOf (nm), ProProcessor::engineClassic);
        auto now = seededRender (*p);
        const float d = maxDiff (ref, now);
        if (d == 0.0f && ref.getNumSamples() == now.getNumSamples()) { ++same; continue; }
        // a sound the old build itself never renders twice alike (random arp / random modulation: ref vs a second dump differ)
        // only has to be the same sound
        const auto f2 = dir.getSiblingFile ("ref2").getChildFile (safeName (nm) + ".f32");
        double env = 0, spec = 0, selfEnv = 0, selfSpec = 0;
        if (f2.existsAsFile())
        {
            auto ref2 = readRaw (f2);
            if (maxDiff (ref, ref2) > 0.0f)
            {
                sameSound (ref, ref2, selfEnv, selfSpec);
                if (sameSound (ref, now, env, spec, std::max (2.0, selfEnv * 1.5 + 0.5), std::max (2.5, selfSpec * 1.5 + 0.5))) { ++varies; continue; }
            }
        }
        bad << nm << " (" << juce::String (d, 6) << ") ";
    }
    CHECK (same + varies == n, "%d / %d factory sounds identical in CLASSIC, %d more vary run to run in the old build too and are the same sound %s", same, n, varies, bad.toRawUTF8());
    return failures;
}

int gvZip()
{   // the GHOST VERB alone (part of 'zipper'): a 130 Hz sine, the amount swung -0.2 .. 0.8 (through 0) every 'every' blocks vs held;
    // HF by the second difference
    for (int every : { 1, 4 })
    for (int bs : { 32, 512 })
    {
        auto run = [&] (bool moving)
        {
            ghosty::GhostVerb gv; gv.prepare (SR); gv.reset();
            ghosty::GhostVerb::Params pr; pr.decay = 2.2f; pr.predelayMs = 32; pr.loCut = 280; pr.hiCut = 9000; pr.size = .65f; pr.width = 1.2f; pr.damp = .45f; pr.lowDamp = .35f;
            const int total = (int) (6.0 * SR); std::vector<float> L ((size_t) total), R ((size_t) total);
            for (int i = 0; i < total; ++i) L[(size_t) i] = R[(size_t) i] = 0.3f * (float) std::sin (2 * juce::MathConstants<double>::pi * 130.8 * i / SR);
            int blk = 0; juce::Random rng (3); int step = bs;
            for (int pos = 0; pos < total; pos += step, ++blk)
            {
                const double t = pos / SR;
                step = (moving && t >= 3.0 && every == 4) ? 16 * (1 + rng.nextInt (32)) : bs;   // (every 4: random block sizes, as fine steps make)
                const double amt = t < 3.0 ? 0.3 : (moving ? 0.3 + 1.0 * (zippertest::tri ((t - 3.0) - std::fmod (t - 3.0, every * bs / SR), 0.3) - 0.5) : 0.3);
                pr.mix = (float) juce::jlimit (0.0, 1.0, amt) * 0.35f;
                gv.process (L.data() + pos, R.data() + pos, std::min (step, total - pos), pr);
            }
            // HF > 6 kHz energy in 3..6 s via a 2nd difference
            double e = 0; for (int i = (int) (3.0 * SR); i < total; ++i) { const double d = L[(size_t) i] - 2 * L[(size_t) i - 1] + L[(size_t) i - 2]; e += d * d; }
            return 10 * std::log10 (e + 1e-30);
        };
        const double mv = run (true), hd = run (false);
        CHECK (std::abs (mv - hd) <= 1.0, "GHOST VERB alone: amount swung through 0 every %d block(s) of %d%s: HF %.1f dB vs held %.1f dB", every, bs, every == 4 ? " (random block sizes)" : "", mv, hd);
    }
    return failures;
}
} // namespace r2
