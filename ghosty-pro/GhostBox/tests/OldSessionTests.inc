// Old sessions render bit-identically: a few saved states (two Ghost Box 2 sessions, a Ghost Box 3 session with MORPH / MOTION /
// MACROS / parallel lanes / SNAPSHOT-era settings, and the default state) rendered with a fixed signal and transport. The hashes in
// fixtures/old-session-hashes.txt were written by the build BEFORE GHOST SCENES / CAPTURE / DNA (GhostBoxTests oldhash write), so any
// change in how an old session sounds fails here.
//   GhostBoxTests oldhash          -> check against the fixture
//   GhostBoxTests oldhash write    -> (re)write the fixture (only from a build whose sound is the reference)

static juce::File oldHashFile() { return fixtureDir().getChildFile ("old-session-hashes.txt"); }

static juce::uint64 hashStereo (const Stereo& s)
{   // FNV-1a over the float bits of both channels
    juce::uint64 h = 1469598103934665603ull;
    auto mix = [&h] (float x) { uint32_t b; std::memcpy (&b, &x, 4); for (int k = 0; k < 4; ++k) { h ^= (b >> (8 * k)) & 0xffu; h *= 1099511628211ull; } };
    for (size_t i = 0; i < s.size(); ++i) { mix (s.l[i]); mix (s.r[i]); }
    return h;
}

// the Ghost Box 3 session (written once by the reference build into the fixtures folder, then only read)
static juce::File gb3SessionFile() { return fixtureDir().getChildFile ("ghostbox3_morph_motion.xml"); }
static void writeGb3Session()
{
    Proc p (true); p.pump();
    p.scene ("galaxyecho"); p.impl().morphSet (0); p.scene ("dustydream"); p.impl().morphSet (1); p.impl().morphEnable (true);
    p.set ("box_morph", 0.4f); p.set ("box_macro3", 0.35f); p.set ("box_mix", 0.85f);
    for (auto& m : factoryMotions()) if (m.name == "4-Bar Riser") p.impl().box.applyMotion (m);
    p.set (pid (uWreck, "on"), 1); p.set (pid (uWreck, "drive"), 0.4f); p.impl().setLane (uWreck, 1); p.set ("fx_par_on", 1);
    p.pump (60);
    juce::MemoryBlock mb; p->getStateInformation (mb);
    if (auto xml = p->getXmlFromBinary (mb.getData(), (int) mb.getSize())) xml->writeTo (gb3SessionFile());
}

static std::vector<std::pair<juce::String, juce::uint64>> oldSessionHashes()
{
    std::vector<std::pair<juce::String, juce::uint64>> out;
    rs = 1; const auto sig = mixOf (drums (4.0), pad (4.0), 0.8f);   // (the noise generator restarts: the same signal every time)
    for (auto* name : { "default", "ghostbox2_cracked_hall.xml", "ghostbox2_retro_gate.xml", "ghostbox3_morph_motion.xml" })
    {
        Proc p (true); p.head.ppq = 0; p.head.playing = true;
        if (juce::String (name) != "default")
        {
            juce::MemoryBlock mb; { auto xml = juce::XmlDocument::parse (fixtureDir().getChildFile (name)); if (xml) p->copyXmlToBinary (*xml, mb); else { std::printf ("  !! missing fixture %s\n", name); ++failures; continue; } }
            p->setStateInformation (mb.getData(), (int) mb.getSize());
        }
        p.pump (150);   // the latency timer
        p->prepareToPlay (SR, 512); p.head.ppq = 0;
        out.push_back ({ name, hashStereo (run (p, sig)) });
    }
    return out;
}

static int oldHashMode (bool write)
{
    if (write && ! gb3SessionFile().existsAsFile()) writeGb3Session();
    const auto a = oldSessionHashes(), b = oldSessionHashes();
    bool det = a.size() == b.size(); for (size_t i = 0; det && i < a.size(); ++i) det = a[i].second == b[i].second;
    if (! det) { std::printf ("old-session renders are not deterministic run to run\n"); for (size_t i = 0; i < a.size() && i < b.size(); ++i) std::printf ("  %s %llx %llx\n", a[i].first.toRawUTF8(), (unsigned long long) a[i].second, (unsigned long long) b[i].second); return 1; }
    if (write)
    {
        juce::String s; for (auto& [n, h] : a) s << n << " " << juce::String::toHexString ((juce::int64) h) << "\n";
        oldHashFile().replaceWithText (s); std::printf ("%s", s.toRawUTF8()); return 0;
    }
    return 0;
}

static void testOldSessions()
{
    std::printf ("\n[30] OLD SESSIONS: saved states render bit-identically to the build before GHOST SCENES / CAPTURE / DNA (hash of 4 s drums + pad)\n");
    juce::StringArray lines; lines.addLines (oldHashFile().loadFileAsString()); lines.removeEmptyStrings();
    const auto now = oldSessionHashes();
    int bad = 0;
    for (auto& [n, h] : now)
    {
        juce::String want; for (auto& l : lines) if (l.upToFirstOccurrenceOf (" ", false, false) == n) want = l.fromFirstOccurrenceOf (" ", false, false).trim();
        const bool ok = want.isNotEmpty() && want == juce::String::toHexString ((juce::int64) h);
        std::printf ("    %-28s %s %s\n", n.toRawUTF8(), juce::String::toHexString ((juce::int64) h).toRawUTF8(), ok ? "identical" : ("DIFFERENT (want " + want + ")").toRawUTF8());
        if (! ok) ++bad;
    }
    check (bad == 0 && lines.size() >= 4, "old sessions must render bit-identically");
}
