// MOD (round 3 item 3): the modulation router. ProTests mod: router math (curves, aux, bipolar), per-voice vs global sources,
// 64 connections, state round trip, snapshot / morph with connections, the amount glide, MIDI learn, LFO 3 / 4.
// The UI part (rings, hit testing, menus, matrix, screenshots) is in ModUiTests.inc. Included by TrapTests.cpp after SoundR2Tests.inc.
namespace modtest
{
using namespace gpro::mod;
static float paramValue (ProProcessor& p, const char* id) { auto* v = p.apvts.getRawParameterValue (id); return v != nullptr ? v->load() : 0.0f; }
static void blocks (ProProcessor& p, int nb, int bs = BS)
{
    juce::AudioBuffer<float> b (2, bs);
    for (int k = 0; k < nb; ++k) { juce::MidiBuffer m; b.clear(); p.processBlock (b, m); }
}
static double goertzel (const juce::AudioBuffer<float>& b, double f, int s0, int s1)
{
    const double w = 2.0 * juce::MathConstants<double>::pi * f / SR, c = 2.0 * std::cos (w);
    double q1 = 0, q2 = 0;
    for (int i = s0; i < s1; ++i) { const double q0 = c * q1 - q2 + b.getSample (0, i); q2 = q1; q1 = q0; }
    return std::sqrt (q1 * q1 + q2 * q2 - c * q1 * q2) / (s1 - s0);
}
static std::unique_ptr<ProProcessor> initProc()
{
    auto p = makeProc (-1); p->initPatch(); settle (*p);
    p->setParam ("verb", 0.0f); p->setParam ("echo", 0.0f); p->setParam ("chorus", 0.0f);
    blocks (*p, 4);
    return p;
}

static void mathTests()
{
    section ("MOD router math: curves, bipolar / unipolar, aux");
    const float px[5] { 0, 0.25f, 0.5f, 0.75f, 1 }, py[5] { 0, 1, 0, 1, 0 }, lx[5] { 0, .25f, .5f, .75f, 1 }, ly[5] { 0, .25f, .5f, .75f, 1 };
    struct T { const char* what; float got, want; };
    const T t[] {
        { "ENV linear unipolar r=0.5", shapeOf (env2, 0.5f, cLinear, false, lx, ly, none, 0), 0.5f },
        { "ENV linear bipolar r=0.5 (the centre)", shapeOf (env2, 0.5f, cLinear, true, lx, ly, none, 0), 0.0f },
        { "ENV linear bipolar r=1", shapeOf (env2, 1.0f, cLinear, true, lx, ly, none, 0), 1.0f },
        { "LFO linear bipolar r=-0.3 (= r)", shapeOf (lfo1, -0.3f, cLinear, true, lx, ly, none, 0), -0.3f },
        { "LFO linear unipolar r=-1 (0)", shapeOf (lfo1, -1.0f, cLinear, false, lx, ly, none, 0), 0.0f },
        { "LFO linear unipolar r=0 (0.5)", shapeOf (lfo1, 0.0f, cLinear, false, lx, ly, none, 0), 0.5f },
        { "EXP u=0.5", shapeOf (vel, 0.5f, cExp, false, lx, ly, none, 0), 0.2f },
        { "EXP u=1", shapeOf (vel, 1.0f, cExp, false, lx, ly, none, 0), 1.0f },
        { "LOG u=0.5", shapeOf (vel, 0.5f, cLog, false, lx, ly, none, 0), (float) (std::log2 (8.5) / 4.0) },
        { "S u=0.25", shapeOf (vel, 0.25f, cS, false, lx, ly, none, 0), 0.15625f },
        { "S bipolar u=0.5 (0)", shapeOf (vel, 0.5f, cS, true, lx, ly, none, 0), 0.0f },
        { "CUSTOM zig-zag u=0.125", shapeOf (vel, 0.125f, cCustom, false, px, py, none, 0), 0.5f },
        { "CUSTOM zig-zag u=0.5", shapeOf (vel, 0.5f, cCustom, false, px, py, none, 0), 0.0f },
        { "CUSTOM zig-zag u=0.75", shapeOf (vel, 0.75f, cCustom, false, px, py, none, 0), 1.0f },
        { "AUX macro 1 x velocity 0.5", shapeOf (mac1, 1.0f, cLinear, false, lx, ly, vel, 0.5f), 0.5f },
        { "AUX LFO bipolar r=0.8 x macro 0.25", shapeOf (lfo2, 0.8f, cLinear, true, lx, ly, mac2, 0.25f), 0.2f },
        { "AUX by a bipolar source (NOTE r=0 -> 0.5)", shapeOf (mac1, 1.0f, cLinear, false, lx, ly, gpro::mod::note, 0.0f), 0.5f },
    };
    int bad = 0;
    for (auto& x : t) { const bool ok = std::abs (x.got - x.want) < 1.0e-5f; if (! ok) ++bad; printf ("      %-44s %8.5f (want %8.5f)%s\n", x.what, x.got, x.want, ok ? "" : "  <-- WRONG"); }
    CHECK (bad == 0, "router math: %d of %d values right", (int) std::size (t) - bad, (int) std::size (t));
}

static void applyTests()
{
    section ("MOD router in the engine: global targets through the shadows, per-voice targets per voice, bypass, LFO 3 / 4");
    {   // a GLOBAL target (REVERB): macro 1 at 1 -> +50 % of the knob's travel
        auto p = initProc();
        p->setParam ("verb", 0.2f);
        const int i = p->modr.add (mac1, "verb", 0.5f);
        p->setParam ("mac1", 1.0f); blocks (*p, 20);
        const float on = p->macros.effective ("verb");
        p->setParam ("mac1", 0.0f); blocks (*p, 20);
        const float off = p->macros.effective ("verb");
        p->modr.setBypass (i, true); p->setParam ("mac1", 1.0f); blocks (*p, 20);
        const float byp = p->macros.effective ("verb");
        p->modr.setBypass (i, false); p->modr.setAmount (i, -1.0f); blocks (*p, 40);
        const float neg = p->macros.effective ("verb");
        CHECK (std::abs (on - 0.7f) < 1e-3f && std::abs (off - 0.2f) < 1e-4f && std::abs (byp - 0.2f) < 1e-3f && std::abs (neg - 0.0f) < 1e-4f && std::abs (paramValue (*p, "verb") - 0.2f) < 1e-6f,
               "MACRO 1 -> REVERB +50 %%: knob 20 %% -> %.3f at macro 1, %.3f at macro 0, bypassed %.3f, amount -100 %% -> %.3f (clamped); the parameter itself stays %.3f",
               on, off, byp, neg, paramValue (*p, "verb"));
        // a skewed knob (ENV 2 attack, seconds): the modulation moves the knob's travel, not seconds
        p->setParam ("e2_a", 0.1f);
        auto* prm = p->apvts.getParameter ("e2_a");
        const float n0 = prm->convertTo0to1 (0.1f);
        p->modr.add (mac2, "e2_a", 0.25f); p->setParam ("mac2", 1.0f); blocks (*p, 20);
        const float got = p->macros.effective ("e2_a"), want = prm->convertFrom0to1 (n0 + 0.25f);
        CHECK (std::abs (got - want) < 1e-3f * want, "MACRO 2 -> ENV 2 ATTACK +25 %% of the travel: 0.100 s -> %.4f s (want %.4f s)", got, want);
    }
    {   // PER-VOICE: VELOCITY -> OSC 1 LEVEL -100 % (unipolar): a hard note goes silent, a soft one keeps sounding, together
        auto p = initProc();
        p->modr.add (vel, "o1_lvl", -1.0f);
        std::vector<Ev> e { { 0.0, 48, 1.0f, true }, { 0.0, 84, 0.25f, true }, { 1.0, 48, 0, false }, { 1.0, 84, 0, false } };
        seedSystemRandom (5); auto b = render (*p, e, 1.0);
        auto q = initProc();
        seedSystemRandom (5); auto r = render (*q, e, 1.0);
        const double f48 = juce::MidiMessage::getMidiNoteInHertz (48), f84 = juce::MidiMessage::getMidiNoteInHertz (84);
        const int s0 = (int) (0.3 * SR), s1 = (int) (0.9 * SR);
        const double lowRatio = goertzel (b, f48, s0, s1) / std::max (1e-12, goertzel (r, f48, s0, s1)), highRatio = goertzel (b, f84, s0, s1) / std::max (1e-12, goertzel (r, f84, s0, s1));
        CHECK (lowRatio < 0.05 && highRatio > 0.4 && highRatio < 0.85,
               "VELOCITY -> OSC 1 LEVEL -100 %%, two notes at once: the velocity-1.0 note %.1f %% of its level (silent), the velocity-0.25 note %.0f %% (level 0.8 -> 0.55; CLEAN's level law moves it a little)", lowRatio * 100, highRatio * 100);
    }
    {   // PER-VOICE ENV vs GLOBAL: ENV 2 -> CUTOFF on a voice target follows each voice's own envelope (router unit level)
        gpro::ModVoice a, b; float da[gpro::numDst] {}, db[gpro::numDst] {};
        auto p = initProc();
        p->modr.add (env2, "cutoff", 0.5f); blocks (*p, 20);     // (a new connection fades in over ~10 ms)
        p->modr.voiceStep (a, { 0, 1.0f, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0 }, 0, 16, da);
        p->modr.voiceStep (b, { 0, 0.2f, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0 }, 0, 16, db);
        CHECK (std::abs (da[gpro::dCutoff] - 0.5f) < 1e-5f && std::abs (db[gpro::dCutoff] - 0.1f) < 1e-5f, "two voices, ENV 2 at 1.0 / 0.2 -> their own cutoff offsets %.3f / %.3f", da[gpro::dCutoff], db[gpro::dCutoff]);
    }
    {   // the global path sees the NEWEST voice's ENV 2
        auto p = initProc();
        p->setParam ("e2_a", 0.001f); p->setParam ("e2_d", 0.5f); p->setParam ("e2_s", 0.5f);
        p->modr.add (env2, "verb", 0.4f);
        juce::AudioBuffer<float> b (2, BS); juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
        p->processBlock (b, m);
        blocks (*p, (int) (1.5 * SR / BS));
        const float v = p->macros.effective ("verb");
        CHECK (std::abs (v - 0.4f * 0.5f) < 0.01f, "ENV 2 (sustain 50 %%) -> REVERB +40 %% (a global target): %.3f with a held note (want 0.200)", v);
    }
    {   // LFO 3 -> CUTOFF: the filter moves at the LFO 3 rate
        auto p = initProc();
        p->setParam ("cutoff", 0.5f); p->setParam ("l3_rate", 4.0f); p->setParam ("l3_shape", 0.0f);
        p->modr.add (lfo3, "cutoff", 0.3f);
        std::vector<Ev> e { { 0.0, 48, 0.8f, true }, { 2.0, 48, 0, false } };
        seedSystemRandom (5); auto b = render (*p, e, 2.0);
        // the brightness (energy above 2 kHz per 10 ms) has to swing at 4 Hz
        std::vector<double> en; juce::IIRFilter hp; hp.setCoefficients (juce::IIRCoefficients::makeHighPass (SR, 2000.0));
        juce::AudioBuffer<float> c (b); hp.processSamples (c.getWritePointer (0), c.getNumSamples());
        for (int s = (int) (0.25 * SR); s + 480 < c.getNumSamples(); s += 480) { double acc = 0; for (int i = 0; i < 480; ++i) acc += (double) c.getSample (0, s + i) * c.getSample (0, s + i); en.push_back (acc); }
        double mean = 0; for (auto x : en) mean += x; mean /= (double) en.size();
        int crossings = 0; for (size_t i = 1; i < en.size(); ++i) if ((en[i - 1] - mean) * (en[i] - mean) < 0) ++crossings;
        const double hz = crossings / 2.0 / (en.size() * 0.01);
        CHECK (hz > 3.0 && hz < 5.0, "LFO 3 (4 Hz) -> CUTOFF: the brightness swings at %.1f Hz", hz);
    }
    {   // LFO 1 DEPTH 0 removes the old matrix's LFO too; DEPTH 1 is the sound before (bit for bit, see compare)
        auto p = makeProc (progOf ("Trap Starter Stack"));
        auto q = makeProc (progOf ("Trap Starter Stack"));
        seedSystemRandom (9); auto a = render (*p, testPhrase(), 1.5);
        q->setParam ("l1_depth", 1.0f); seedSystemRandom (9); auto b = render (*q, testPhrase(), 1.5);
        CHECK (maxDiff (a, b) == 0.0f, "LFO 1 DEPTH at 100 %% (its default) = the same render (max diff %.3g)", maxDiff (a, b));
    }
}

static void loadTest()
{
    section ("MOD router: 64 connections, a 65th refused, no NaN, CPU");
    auto p = initProc();
    const char* dsts[] { "cutoff", "res", "f_drive", "o1_pos", "o2_pos", "o1_lvl", "o2_lvl", "o1_det", "o2_det", "sub_lvl", "nz_lvl", "xamt", "verb", "echo", "chorus", "drive",
                         "o1_w1a", "o2_w1a", "nz_stereo", "l1_rate", "l2_rate", "l3_rate", "l1_depth", "ch_amt", "o1_spr", "o2_spr", "fenv", "e2_a", "release", "glide", "crush", "phaser" };
    const int srcs[] { lfo1, lfo2, env2, vel };
    int added = 0;
    for (int s : srcs) for (auto* d : dsts) if (p->modr.add (s, d, 0.05f) >= 0) ++added;
    const int extra = p->modr.add (lfo4, "cutoff", 0.1f);
    CHECK (added == 64 && extra < 0 && p->modr.size() == 64, "%d connections added (64), a 65th refused (%d), list size %d", added, extra, p->modr.size());
    p->setParam ("o2_on", 1.0f); p->setParam ("o1_uni", 4.0f);
    std::vector<Ev> e; for (int k = 0; k < 8; ++k) for (int n : { 48, 55, 60, 64 }) { e.push_back ({ k * 0.5, n + k, 0.9f, true }); e.push_back ({ k * 0.5 + 0.45, n + k, 0, false }); }
    auto timeIt = [&] (ProProcessor& q) { const auto t0 = juce::Time::getMillisecondCounterHiRes(); seedSystemRandom (3); auto b = render (q, e, 4.0); return std::make_pair (juce::Time::getMillisecondCounterHiRes() - t0, b); };
    auto [tm, b] = timeIt (*p);
    int nans = 0; const float pk = peakOf (b, &nans);
    auto q = initProc(); q->setParam ("o2_on", 1.0f); q->setParam ("o1_uni", 4.0f);
    auto [t0, b0] = timeIt (*q);
    CHECK (nans == 0 && pk < 4.0f && tm < 3.0 * t0 + 200.0, "4 s of chords with 64 connections: no NaN (%d), peak %.2f, %.0f ms vs %.0f ms without (x%.2f)", nans, pk, tm, t0, tm / std::max (1.0, t0));
}

static bool sameList (const std::vector<gpro::ModConn>& a, const std::vector<gpro::ModConn>& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        const auto& x = a[i]; const auto& y = b[i];
        if (x.src != y.src || x.dst != y.dst || std::abs (x.amount - y.amount) > 1e-6f || x.bipolar != y.bipolar || x.curve != y.curve || x.aux != y.aux || x.bypass != y.bypass) return false;
        if (x.curve == cCustom) for (int k = 0; k < 5; ++k) if (std::abs (x.px[(size_t) k] - y.px[(size_t) k]) > 1e-3f || std::abs (x.py[(size_t) k] - y.py[(size_t) k]) > 1e-3f) return false;
    }
    return true;
}

static void stateTests()
{
    section ("MOD state: MODS round trip, older states / factory sounds / INIT have none, LFO 3 / 4 left out at their defaults");
    auto p = initProc();
    p->modr.add (lfo3, "cutoff", 0.4f);
    const int b = p->modr.add (mac1, "verb", -0.3f); p->modr.setAux (b, vel); p->modr.setCurve (b, cS); p->modr.setBypass (b, true);
    const int c = p->modr.add (env2, "o1_pos", 0.7f); p->modr.setCustom (c, { 0, 0.2f, 0.5f, 0.8f, 1 }, { 0, 0.9f, 0.1f, 0.6f, 1 }); p->modr.setBipolar (c, true);
    p->setParam ("l3_rate", 7.0f);
    const auto want = p->modr.list();
    juce::MemoryBlock mb; p->getStateInformation (mb);
    auto q = makeProc (0);
    q->setStateInformation (mb.getData(), (int) mb.getSize()); settle (*q);
    CHECK (sameList (q->modr.list(), want) && std::abs (paramValue (*q, "l3_rate") - 7.0f) < 1e-4f, "state round trip: %d connections (source, target, amount, bipolar, curve, custom points, aux, bypass) and LFO 3 RATE %.2f Hz",
           q->modr.size(), paramValue (*q, "l3_rate"));
    // the same amounts sound the same after the round trip
    seedSystemRandom (11); auto ra = render (*p, testPhrase(), 1.5);
    auto r = makeProc (0); r->setStateInformation (mb.getData(), (int) mb.getSize()); settle (*r);
    seedSystemRandom (11); auto rb = render (*r, testPhrase(), 1.5);
    {
        double envErr = 0, specErr = 0; const bool same = sameSound (ra, rb, envErr, specErr);
        CHECK (same, "the reloaded state sounds the same (envelope %.2f dB, spectrum %.2f dB)", envErr, specErr);
    }
    // an older state (no MODS) loads with none; a factory sound has none; INIT clears
    {
        auto x = juce::ValueTree::fromXml (*juce::AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize()));
        x.removeChild (x.getChildWithName ("MODS"), nullptr);
        juce::MemoryBlock old; if (auto xml = x.createXml()) juce::AudioProcessor::copyXmlToBinary (*xml, old);
        auto o = initProc(); o->modr.add (lfo1, "res", 0.5f);
        o->setStateInformation (old.getData(), (int) old.getSize()); settle (*o);
        const int afterOld = o->modr.size();
        o->modr.add (lfo1, "res", 0.5f); o->setCurrentProgram (3); const int afterFactory = o->modr.size();
        o->modr.add (lfo1, "res", 0.5f); o->initPatch(); const int afterInit = o->modr.size();
        CHECK (afterOld == 0 && afterFactory == 0 && afterInit == 0, "a state without MODS -> %d connections, a factory sound -> %d, INIT -> %d", afterOld, afterFactory, afterInit);
    }
    {   // a sound without connections / LFO 3 / 4 settings saves exactly as before: no MODS child, no l3_ / l4_ / depth values
        auto f = makeProc (progOf ("Trap Starter Stack"));
        juce::MemoryBlock m2; f->getStateInformation (m2);
        auto t = juce::ValueTree::fromXml (*juce::AudioProcessor::getXmlFromBinary (m2.getData(), (int) m2.getSize()));
        int modParams = 0; for (const auto& ch : t) if (gpro::ModRouter::isModParam (ch.getProperty ("id", "").toString())) ++modParams;
        CHECK (! t.getChildWithName ("MODS").isValid() && modParams == 0, "a factory sound's state: MODS child %s, %d LFO 3 / 4 / DEPTH values written", t.getChildWithName ("MODS").isValid() ? "PRESENT" : "absent", modParams);
    }
    // MIDI learn: session state, CC moves the knob
    {
        auto m = initProc();
        m->modr.learn ("cutoff");
        juce::AudioBuffer<float> bb (2, BS); juce::MidiBuffer mm; mm.addEvent (juce::MidiMessage::controllerEvent (1, 74, 32), 0);
        m->processBlock (bb, mm); pump (120);
        juce::MidiBuffer m3; m3.addEvent (juce::MidiMessage::controllerEvent (1, 74, 127), 0); m->processBlock (bb, m3); pump (120);
        const float cut = m->apvts.getParameter ("cutoff")->getValue();
        juce::MemoryBlock ms; m->getStateInformation (ms);
        auto n = makeProc (0); n->setStateInformation (ms.getData(), (int) ms.getSize());
        CHECK (m->modr.ccFor ("cutoff") == 74 && std::abs (cut - 1.0f) < 1e-4f && n->modr.ccFor ("cutoff") == 74, "MIDI LEARN: CUTOFF learned CC %d, CC 74 = 127 -> cutoff %.3f, kept in the session state (CC %d)", m->modr.ccFor ("cutoff"), cut, n->modr.ccFor ("cutoff"));
    }
}

static void snapMorphTests()
{
    section ("MOD with SNAPSHOTS and the MORPH BOARD: connections and their amounts are part of a sound");
    {
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("gp-modsnap-" + juce::String (juce::Time::currentTimeMillis()));
        auto p = initProc(); p->snapshots->setFolder (dir);
        const int i = p->modr.add (mac1, "verb", 0.6f);
        p->snapshots->take(); juce::Thread::sleep (3);
        p->modr.setAmount (i, 0.1f); p->modr.add (lfo2, "cutoff", 0.3f);
        p->snapshots->load (0); settle (*p);
        const auto l = p->modr.list();
        CHECK (l.size() == 1 && l[0].src == mac1 && l[0].dst == "verb" && std::abs (l[0].amount - 0.6f) < 1e-6f, "a snapshot brings back its connections and amounts (%d connection(s), amount %.2f)", (int) l.size(), l.empty() ? 0.0f : l[0].amount);
        dir.deleteRecursively();
    }
    {   // two ghosts with the same connection at 20 % and 80 %: the puck halfway -> 50 %
        auto a = initProc(); a->setParam ("mac1", 1.0f); a->modr.add (mac1, "verb", 0.2f); auto sA = a->captureSound();
        auto b = initProc(); b->setParam ("mac1", 1.0f); b->modr.add (mac1, "verb", 0.8f); auto sB = b->captureSound();
        auto p = initProc();
        p->morphAddGhost (0.2f, 0.5f, sA, "A"); p->morphAddGhost (0.8f, 0.5f, sB, "B");
        p->morph.enabled.store (true); p->morphChanged (false);
        p->setParam ("mac1", 1.0f);
        auto puck = [&] (float x) { p->setParam ("morph_x", x); p->setParam ("morph_y", 0.5f); blocks (*p, 2); p->morphTick(); settle (*p); blocks (*p, 40); };
        puck (0.2f); const float atA = p->modr.size() == 1 ? p->modr.liveValue (0) : -1;
        puck (0.5f); const float mid = p->modr.size() == 1 ? p->modr.liveValue (0) : -1;
        puck (0.8f); const float atB = p->modr.size() == 1 ? p->modr.liveValue (0) : -1;
        CHECK (std::abs (atA - 0.2f) < 0.01f && std::abs (mid - 0.5f) < 0.03f && std::abs (atB - 0.8f) < 0.01f, "MORPH: MACRO 1 -> REVERB at ghost A %.3f (0.2), halfway %.3f (0.5), at ghost B %.3f (0.8)", atA, mid, atB);
        // a ghost without the connection counts as 0
        auto c = initProc(); c->setParam ("mac1", 1.0f); auto sC = c->captureSound();
        p->morphSetSound (1, sC, "C"); puck (0.5f); const float half = p->modr.liveValue (0);
        CHECK (std::abs (half - 0.1f) < 0.03f, "MORPH: a ghost without the connection blends as 0 (halfway between 20 %% and none: %.3f)", half);
    }
}

static void glideTest()
{
    section ("MOD amounts glide: a jump in AMOUNT reaches the voices as a ramp (no step)");
    auto p = initProc();
    p->setParam ("cutoff", 0.3f);
    const int i = p->modr.add (mac1, "cutoff", 0.0f); p->setParam ("mac1", 1.0f);
    juce::AudioBuffer<float> b (2, 64); juce::MidiBuffer m; m.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 0);
    p->processBlock (b, m); blocks (*p, 40, 64);
    p->modr.setAmount (i, 0.6f);
    // the amount the voices see, block by block: rises over ~10 ms, never more than a block's share of the jump at once
    float prev = p->modr.liveValue (i), maxStep = 0; int toNinety = -1;
    for (int k = 0; k < 40; ++k) { blocks (*p, 1, 64); const float v = p->modr.liveValue (i); maxStep = std::max (maxStep, v - prev); prev = v; if (toNinety < 0 && v > 0.54f) toNinety = k + 1; }
    {   // removing a route never jumps the others (each route keeps its own amount slot)
        auto q = initProc();
        q->setParam ("mac1", 1.0f);
        q->modr.add (mac1, "cutoff", -0.8f); q->modr.add (mac1, "res", 0.5f); blocks (*q, 200, 64);
        q->modr.remove (0);
        float worst = 0; for (int k = 0; k < 10; ++k) { blocks (*q, 1, 64); worst = std::max (worst, std::abs (q->modr.liveValue (0) - 0.5f)); }
        CHECK (q->modr.size() == 1 && worst < 1e-5f, "remove route 1 of 2 while playing: route 2 stays at +50 %% (largest deviation %.5f)", worst);
    }
    CHECK (maxStep < 0.6f * 0.15f && toNinety > 0 && toNinety * 64 < (int) (0.04 * SR), "AMOUNT 0 -> 60 %%: the largest step per 64-sample block %.3f, 90 %% after %d blocks (%.1f ms)", maxStep, toNinety, toNinety * 64 / SR * 1000.0);
}

static int modTests (const juce::StringArray& args)
{
    juce::ignoreUnused (args);
    mathTests();
    applyTests();
    loadTest();
    stateTests();
    snapMorphTests();
    glideTest();
#ifdef GPRO_MOD_UI_TESTS
    modUiTests (args);
#endif
    printf ("\n%d failures\n", failures);
    return failures;
}
} // namespace modtest
