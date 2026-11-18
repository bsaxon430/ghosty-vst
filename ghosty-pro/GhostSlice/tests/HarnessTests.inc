// GhostSliceTests: the release harness (GHOST-SLICE-STUDY.md 9, phase H + the cross-cutting rules). Groups:
//   golden       golden projects (tests/golden/golden.json): STUDIO renders hashed (SHA-256) per platform, with the engine version
//                they were made for; a hash change without an engine-version bump fails. GhostSliceTests golden --update rewrites it
//   proptest     model -> JSON -> model on 1000 random full models (every field: warp, note edits, draws, holds, repeats, keys, region
//                pitch) + JSON idempotence
//   srmatrix     44.1 / 48 / 96 kHz: state saved at one rate, restored at another: the same project, the plan at the new rate, sound
//   rtsafety     every play path (DIRECT, LIVE stretch, STUDIO, HOLD SUSTAIN / ELASTIC / TEXTURE preview, REPEAT, note-edit curve,
//                region pitch, PLAY voices incl. HOLD VOWEL) in 64-frame blocks: no allocation on the audio thread; worst block time
//   blocktime    the worst 64-frame block (median of 20 paced runs) for ELASTIC start, REPEAT ratchet, LIVE stretch start < 60 %
//   pluginsmoke [ARTEFACTS]  the built VST3 + CLAP load, play and save / restore their state (pluginval when it is installed)
#include <dlfcn.h>
#if GSLICE_SMOKE_CLAP
 #include <clap/clap.h>
#endif

namespace harness
{
static juce::String platformKey()
{
   #if JUCE_LINUX
    juce::String os = "linux";
   #elif JUCE_MAC
    juce::String os = "mac";
   #else
    juce::String os = "windows";
   #endif
   #if JUCE_ARM
    return os + "-arm64";
   #else
    return os + "-x64";
   #endif
}
static juce::String sha (const juce::AudioBuffer<float>& b)
{
    juce::MemoryBlock mb;
    for (int c = 0; c < b.getNumChannels(); ++c) mb.append (b.getReadPointer (c), sizeof (float) * (size_t) b.getNumSamples());
    return juce::SHA256 (mb).toHexString();
}

// ---------------------------------------------------------------- golden cases (generated audio only: no third-party samples)
struct Case { const char* name; const char* gen; double sourceRate; std::function<std::optional<Project> (SliceProcessor&)> build; };

static juce::AudioBuffer<float> generate (const juce::String& gen, double sr)
{
    if (gen == "drums") return drums (sr, 3.0);
    if (gen == "vowel") return vowel (sr);
    if (gen == "melody") return stereoOf (voice (sr, fourNotes()));
    if (gen == "pad") return pad (sr, 2.5);
    return {};
}

static std::vector<Case> cases()
{
    std::vector<Case> c;
    c.push_back ({ "edit-arrangement", "drums", 48000, [] (SliceProcessor& p) -> std::optional<Project>
    {
        auto q = ops::splitAt (p.project(), p.project().regions[0].id, 48000.0); if (! q) return q;
        q = ops::splitAt (*q, q->regions.back().id, 96000.0); if (! q) return q;
        q = ops::setReverse (*q, q->regions[1].id, true); if (! q) return q;
        q = ops::setFades (*q, q->regions[0].id, 12.0, 30.0); if (! q) return q;
        q = ops::setGain (*q, q->regions[2].id, -4.5); if (! q) return q;
        q = ops::moveRegion (*q, q->regions[2].id, 30000.0); return q;
    } });
    c.push_back ({ "stretch-warp", "drums", 48000, [] (SliceProcessor& p) -> std::optional<Project>
    {
        auto q = ops::stretchTo (p.project(), p.project().regions[0].id, p.project().regions[0].length() * 1.3); if (! q) return q;
        int64 mk = 0; q = ops::addMarker (*q, q->regions[0].id, 40000.0, &mk); if (! q) return q;
        q = ops::dragMarker (*q, q->regions[0].id, mk, 52000.0, FlexMode::Stretch); return q;
    } });
    c.push_back ({ "global-pitch", "melody", 48000, [] (SliceProcessor& p) -> std::optional<Project> { Project q = p.project(); q.pitchSemis = 3; q.formantSemis = 1; return q; } });
    for (int engine : { 2, 1 })
        c.push_back ({ engine == 2 ? "notes-wide" : "notes-vocal", "melody", 48000, [engine] (SliceProcessor& p) -> std::optional<Project>
        {
            auto notes = notesNow (p); if (notes.size() < 3) return std::nullopt;
            return ops::editNotes (p.project(), notes[1].regionId, { notes[1], notes[2] }, [engine] (PitchEdit& e) { e.cents = 200; e.engine = engine; e.vibrato = 0.5; });
        } });
    for (int mode = 0; mode < 4; ++mode)
    {
        static const char* names[] { "hold-elastic", "hold-sustain", "hold-texture", "hold-freeze" };
        c.push_back ({ names[mode], "vowel", 48000, [mode] (SliceProcessor& p) -> std::optional<Project>
        {
            const int64 selA = 2400, selB = 34560;
            const auto st = hold::pickStable (*p.getSource(), selA, selB);
            const double held = (double) (selB - selA) * 3.0 - (double) (st.a - selA) - (double) (selB - st.b);
            return ops::addHold (p.project(), p.project().regions[0].id, selA, selB, st.a, st.b, held, (HoldMode) mode);
        } });
    }
    c.push_back ({ "repeat-ratchet", "drums", 48000, [] (SliceProcessor& p) -> std::optional<Project>
    {
        int64 rid = 0; auto q = ops::addRepeat (p.project(), 24000.0, 72000.0, &rid); if (! q) return q;
        RepeatOp x = *q->findRepeat (rid); x.rateDiv = 16; x.ratchetOn = true; x.ratchet = { -1.0f, 0.0f, 0.5f, 1.0f, 1.5f }; x.reverse = 1; x.pitchSemis = -2; x.decay = 0.8;
        return ops::setRepeat (*q, x);
    } });
    c.push_back ({ "region-pitch", "melody", 48000, [] (SliceProcessor& p) -> std::optional<Project>
    {
        auto q = ops::splitAt (p.project(), p.project().regions[0].id, 60000.0); if (! q) return q;
        return ops::setRegionPitch (*q, q->regions[1].id, -3, 25, 2);
    } });
    c.push_back ({ "resampled-44k", "pad", 44100, [] (SliceProcessor& p) -> std::optional<Project>
    {
        return ops::stretchTo (p.project(), p.project().regions[0].id, p.project().regions[0].length() * 0.8);
    } });
    return c;
}

// a processor for one case: the generated source at its rate, played at 48 kHz, the analysis landed
static std::unique_ptr<Rig> caseRig (const Case& cs)
{
    auto r = std::make_unique<Rig> (generate (cs.gen, cs.sourceRate), 48000.0);
    if (cs.sourceRate != 48000.0)
    {   // the source at its own rate, played at 48 kHz (the working copy is resampled)
        (*r)->loadSourceForTests (source::fromBuffer (generate (cs.gen, cs.sourceRate), cs.sourceRate, "test.wav"));
        (*r)->setStudioMode (false);
    }
    (*r)->waitAnalysis (60000);
    return r;
}
static juce::AudioBuffer<float> renderCase (Rig& r, const Project& p)
{
    r->apply (p, "golden");
    r->waitAnalysis (60000);   // (the note curves need the pitch track: recompiled once it is there)
    return r.render();
}
} // namespace harness

static juce::File goldenFile() { return juce::File (GSLICE_GOLDEN_DIR).getChildFile ("golden.json"); }

static void updateGolden()
{
    using namespace harness;
    auto* root = new juce::DynamicObject();
    root->setProperty ("format", "ghostslice-golden"); root->setProperty ("engineVersion", kEngineVersion);
    root->setProperty ("note", "Generated by GhostSliceTests golden --update. Generated test audio only. A render change needs an engine-version bump (SliceCore.h kEngineVersion) and a rerun of --update on each platform.");
    // keep the other platforms' hashes when the engine version is the same
    juce::var old = juce::JSON::parse (goldenFile().loadFileAsString());
    const bool sameVersion = old.isObject() && (int) old["engineVersion"] == kEngineVersion;
    juce::Array<juce::var> arr;
    for (auto& cs : cases())
    {
        auto r = caseRig (cs);
        const auto q = cs.build (*r->p);
        if (! q) { std::printf ("  %s: the edit was refused\n", cs.name); continue; }
        const auto out = renderCase (*r, *q);
        auto* o = new juce::DynamicObject();
        o->setProperty ("name", cs.name); o->setProperty ("generator", cs.gen); o->setProperty ("sourceRate", cs.sourceRate); o->setProperty ("hostRate", 48000.0);
        o->setProperty ("project", json::toVar (*q)); o->setProperty ("frames", out.getNumSamples());
        auto* hs = new juce::DynamicObject();
        if (sameVersion)
            if (auto* oc = old["cases"].getArray())
                for (auto& x : *oc) if (x["name"].toString() == cs.name) if (auto* ho = x["sha256"].getDynamicObject()) for (auto& kv : ho->getProperties()) hs->setProperty (kv.name, kv.value);
        hs->setProperty (platformKey(), sha (out));
        o->setProperty ("sha256", juce::var (hs));
        arr.add (juce::var (o));
        std::printf ("  %-18s %s (%d frames)\n", cs.name, sha (out).substring (0, 16).toRawUTF8(), out.getNumSamples());
    }
    root->setProperty ("cases", arr);
    goldenFile().getParentDirectory().createDirectory();
    goldenFile().replaceWithText (juce::JSON::toString (juce::var (root), false));
    std::printf ("  wrote %s (engine v%d, %s)\n", goldenFile().getFullPathName().toRawUTF8(), kEngineVersion, platformKey().toRawUTF8());
}

static void testGolden()
{
    using namespace harness;
    section ("[37] golden projects: STUDIO renders hashed per platform, engine-version guarded (tests/golden/golden.json)");
    const auto v = juce::JSON::parse (goldenFile().loadFileAsString());
    if (! v.isObject() || v["format"].toString() != "ghostslice-golden") { check (false, "golden.json missing or damaged: run GhostSliceTests golden --update"); return; }
    const int ver = (int) v["engineVersion"];
    if (ver != kEngineVersion)
    {   // a bump on purpose: the renders may change; the file must be regenerated (it is not a failure of the render)
        std::printf ("    goldens were made for engine v%d, this build is v%d (bumped on purpose): run GhostSliceTests golden --update on every platform\n", ver, kEngineVersion);
        check (ver < kEngineVersion, "the engine version only moves forward (goldens v" + juce::String (ver) + ", build v" + juce::String (kEngineVersion) + ")");
        return;
    }
    std::map<juce::String, Case> byName; for (auto& cs : cases()) byName.emplace (cs.name, cs);
    int same = 0, differ = 0, missing = 0, total = 0; juce::String diffs;
    if (auto* arr = v["cases"].getArray())
        for (auto& x : *arr)
        {
            ++total;
            const auto name = x["name"].toString();
            auto it = byName.find (name);
            if (it == byName.end()) { diffs << name << " (no builder) "; ++differ; continue; }
            Project p; juce::String err;
            if (! json::fromVar (x["project"], p, err)) { diffs << name << " (" << err << ") "; ++differ; continue; }
            auto r = caseRig (it->second);
            const bool sameSource = p.source.fingerprint == (*r)->project().source.fingerprint;
            p.source = (*r)->project().source;   // (a platform whose generator differs in the last bit: its own hash entry covers it)
            const auto out = renderCase (*r, p);
            const auto want = x["sha256"][juce::Identifier (platformKey())].toString();
            if (want.isEmpty()) { ++missing; std::printf ("    %s: no %s hash yet (run golden --update here)\n", name.toRawUTF8(), platformKey().toRawUTF8()); continue; }
            if (want == sha (out) && out.getNumSamples() == (int) x["frames"]) ++same;
            else { ++differ; diffs << name << (sameSource ? "" : " (generated source differs)") << " "; }
        }
    check (differ == 0 && total >= 12, "golden renders unchanged at engine v" + juce::String (kEngineVersion) + ": " + juce::String (same) + " / " + juce::String (total) + " identical on "
           + platformKey() + (missing > 0 ? " (" + juce::String (missing) + " without a hash for this platform)" : juce::String()) + (differ > 0 ? "; CHANGED: " + diffs + "- bump kEngineVersion if on purpose" : juce::String()));
}

// ---------------------------------------------------------------- property test: 1000 random full models
static Project randomModel (std::mt19937& rng)
{
    std::uniform_real_distribution<double> U (0, 1);
    auto I = [&] (int lo, int hi) { return std::uniform_int_distribution<int> (lo, hi) (rng); };
    auto D = [&] (double lo, double hi) { return lo + (hi - lo) * U (rng); };
    SourceRef ref; ref.fingerprint = juce::String::toHexString ((juce::int64) rng()) + juce::String::repeatedString ("c", 48); ref.sampleRate = I (0, 2) == 0 ? 44100 : 48000;
    ref.frames = I (48000, 4800000); ref.channels = I (1, 2); ref.fileName = "take " + juce::String (I (1, 99)) + ".wav"; ref.path = "/tmp/" + ref.fileName; ref.relPath = I (0, 1) ? "audio/" + ref.fileName : "";
    Project p = makeProject (ref);
    p.bpm = D (60, 200); p.gridDiv = std::array<int, 6> { 0, 4, 8, 16, 32, 64 } [(size_t) I (0, 5)]; p.gridTriplet = I (0, 1); p.beatOffset = D (0, 5000);
    p.pitchSemis = I (-12, 12); p.pitchCents = D (-50, 50); p.formantSemis = D (-5, 5); p.transientLock = I (0, 1);
    p.key = I (0, 1) ? "F# MIN" : "--"; p.projectKey = I (0, 1) ? "C MAJ" : "--"; p.material = std::array<const char*, 4> { "AUTO", "DRUMS", "VOCAL", "MELODIC" } [(size_t) I (0, 3)];
    p.tonalityHz = D (500, 24000); p.pitchEngine = I (0, 2); p.keyMode = I (0, 3);
    p.regions.clear();
    int64 id = 100;
    const int nr = I (1, 6);
    for (int k = 0; k < nr; ++k)
    {
        Region r; r.id = ++id; r.dstStart = D (0, 400000); r.gainDb = D (-24, 6); r.fadeInMs = D (0, 200); r.fadeOutMs = D (0, 200); r.reverse = I (0, 3) == 0; r.muted = I (0, 9) == 0;
        const int64 s0 = I (0, (int) (ref.frames / 2)); double t = 0; int64 s = s0;
        const int nm = I (2, 7);
        for (int m = 0; m < nm; ++m)
        {
            WarpMarker w; w.id = ++id; w.t = t; w.s = s; w.anchor = m > 0 && m + 1 < nm && I (0, 4) == 0; w.kind = I (0, 3) == 0 ? MarkerKind::Auto : MarkerKind::User;
            r.warp.push_back (w);
            t += D (500, 50000) + 0.123456789; s += I (500, 40000);
        }
        if (! r.reverse && r.warp.size() >= 4 && I (0, 1))
        {   // a hold on an inner segment
            HoldOp h; h.id = ++id; h.markerA = r.warp[1].id; h.markerB = r.warp[2].id; h.selA = r.warp[0].s; h.selB = r.warp[3].s; h.mode = (HoldMode) I (0, 3);
            h.autoStable = I (0, 1); h.driftCents = D (0, 50); h.variation = D (0.25, 2); h.smear = D (0.05, 1); h.seed = (uint32_t) rng();
            r.holds.push_back (h);
        }
        const int ne = I (0, 4);
        for (int e = 0; e < ne; ++e)
        {
            PitchEdit pe; pe.id = ++id; pe.srcStart = r.warp.front().s + I (0, 1000); pe.srcEnd = pe.srcStart + I (100, 20000);
            pe.kind = I (0, 2) == 0 ? PitchEditKind::Draw : PitchEditKind::Note;
            pe.cents = D (-1200, 1200); pe.formantCents = D (-500, 500); pe.centre = D (3600, 8400); pe.drift = D (0, 2); pe.vibrato = D (0, 2);
            pe.transitionMs = D (0, 250); pe.glide = I (0, 1); pe.absolute = pe.kind == PitchEditKind::Draw && I (0, 1); pe.engine = I (-1, 2);
            if (pe.kind == PitchEditKind::Draw) for (int k2 = 0, n = I (2, 9); k2 < n; ++k2) pe.curve.push_back ({ pe.srcStart + (pe.srcEnd - pe.srcStart) * k2 / juce::jmax (1, n - 1), D (-800, 800) });
            r.pitch.push_back (pe);
        }
        if (I (0, 2) == 0) { r.pitchSemis = I (-12, 12); r.pitchCents = D (-100, 100); r.formantSemis = D (-6, 6); }
        p.regions.push_back (r);
    }
    for (int k = 0, n = I (0, 3); k < n; ++k)
    {
        RepeatOp x; x.id = ++id; x.start = D (0, 300000); x.length = D (100, 90000); x.rateDiv = std::array<int, 6> { 4, 8, 16, 32, 64, 128 } [(size_t) I (0, 5)]; x.rateMod = I (0, 2);
        x.retrigger = I (0, 1); x.pitchSemis = D (-24, 24); x.pitchRamp = I (0, 1); x.reverse = I (0, 2); x.fadeInMs = D (0, 50); x.fadeOutMs = D (0, 50); x.swing = D (0.5, 0.75);
        x.ramp = D (0.25, 4); x.ratchetOn = I (0, 1); for (auto& f : x.ratchet) f = (float) D (-2, 2); x.decay = D (0, 1); x.gate = D (0.1, 1);
        p.repeats.push_back (x);
    }
    p.play.mode = (SliceMode) I (0, kSliceModes - 1); p.play.equalN = I (2, 64); p.play.sensitivity = D (0, 1); p.play.beatDiv = std::array<int, 4> { 4, 8, 16, 32 } [(size_t) I (0, 3)];
    p.play.polyphony = I (1, 32); p.play.bendRange = I (2, 24); p.play.velToGain = D (0, 1); p.play.velToFilter = D (0, 1);
    for (int k = 0, n = I (0, 4); k < n; ++k) p.play.manual.push_back (D (0, 300000));
    std::sort (p.play.manual.begin(), p.play.manual.end());
    std::set<int> notes;
    for (int k = 0, n = I (0, 12); k < n; ++k)
    {
        KeySlot ks; ks.note = I (0, 127); if (! notes.insert (ks.note).second) continue;
        ks.slice = I (0, 200); ks.pitchSemis = D (-24, 24); ks.reverse = I (0, 1); ks.startMs = D (0, 2000); ks.attackMs = D (0, 2000); ks.decayMs = D (0, 2000); ks.sustain = D (0, 1);
        ks.releaseMs = D (0, 4000); ks.gainDb = D (-48, 12); ks.pan = D (-1, 1); ks.choke = I (0, 8); ks.oneShot = I (0, 1); ks.repeat = I (0, 5); ks.vowelHold = ! ks.oneShot && I (0, 1);
        p.play.keys.push_back (ks);
    }
    std::sort (p.play.keys.begin(), p.play.keys.end(), [] (const KeySlot& a, const KeySlot& b) { return a.note < b.note; });
    p.nextId = id + 1;
    return p;
}

static void testPropertyJson()
{
    section ("[38] property test: model -> JSON -> model on 1000 random full models (warp, note edits, draws, holds, repeats, keys, region pitch)");
    std::mt19937 rng (20261026);
    int bad = 0, notIdem = 0, invalid = 0;
    for (int k = 0; k < 1000; ++k)
    {
        const Project p = randomModel (rng);
        for (auto& r : p.regions) invalid += r.valid() ? 0 : 1;
        const auto s = json::toString (p);
        Project q; juce::String err;
        if (! json::fromString (s, q, err) || ! (q == p)) { if (bad < 3) std::printf ("    case %d differs: %s\n", k, err.toRawUTF8()); ++bad; continue; }
        if (json::toString (q) != s) ++notIdem;
    }
    check (invalid == 0, "the generator makes valid regions only");
    check (bad == 0 && notIdem == 0, "1000 random models survive model -> JSON -> model exactly (" + juce::String (bad) + " differ), and the JSON is idempotent (" + juce::String (notIdem) + " not)");
}

// ---------------------------------------------------------------- sample-rate matrix
static void testSrMatrix()
{
    section ("[39] sample-rate matrix 44.1 / 48 / 96 kHz: state saved at one rate, restored at another (same project, plan at the new rate, sound)");
    const auto wavFile = tmpDir().getChildFile ("srmatrix.wav");
    { juce::WavAudioFormat wav; writeAudio (stereoOf (voice (48000, fourNotes())), 48000, wavFile, wav, 24); }
    const double rates[] { 44100, 48000, 96000 };
    int ok = 0, n = 0; juce::String info;
    for (double a : rates)
        for (double b : rates)
        {
            if (a == b) continue;
            ++n;
            SliceProcessor A; A.setPlayConfigDetails (0, 2, a, 512); A.prepareToPlay (a, 512); pump (5);
            juce::String err; if (! A.loadAudio (wavFile, err)) { info << "load failed "; continue; }
            A.waitAnalysis (60000);
            auto q = ops::splitAt (A.project(), A.project().regions[0].id, 48000.0);
            q = ops::stretchTo (*q, q->regions[1].id, q->regions[1].length() * 1.2);
            q = ops::setRegionPitch (*q, q->regions[0].id, 2, 0, 0);
            int64 rid = 0; q = ops::addRepeat (*q, 20000.0, 40000.0, &rid);
            A.apply (*q, "edits"); A.apply (*ops::turnIntoInstrument (A.project(), (int) A.currentSlices().size()), "keys");
            juce::MemoryBlock st; A.getStateInformation (st);
            SliceProcessor B; B.setPlayConfigDetails (0, 2, b, 512); B.prepareToPlay (b, 512); pump (5);
            B.setStateInformation (st.getData(), (int) st.getSize());
            for (int i = 0; i < 400 && (B.isLoading() || B.getSource() == nullptr); ++i) pump (10);
            B.waitAnalysis (60000); pump (20);
            const bool sameProject = json::fingerprint (B.project()) == json::fingerprint (A.project());
            const int64 want = (int64) std::llround (A.project().length() * b / A.project().sampleRate);
            const bool planOk = std::abs (B.currentPlan().length - want) <= 2 && B.getWorking() != nullptr && B.getWorking()->rate == b;
            const auto ra = render::plan (A.currentPlan(), *A.getWorking(), nullptr, false), rb = render::plan (B.currentPlan(), *B.getWorking(), nullptr, false);
            bool finite = true; double ea = 0, eb = 0;
            for (int c = 0; c < 2; ++c) { for (int i = 0; i < ra.getNumSamples(); ++i) ea += (double) ra.getSample (c, i) * ra.getSample (c, i); for (int i = 0; i < rb.getNumSamples(); ++i) { const float x = rb.getSample (c, i); finite = finite && std::isfinite (x); eb += (double) x * x; } }
            const double la = 10 * std::log10 (ea / juce::jmax (1, ra.getNumSamples()) + 1e-20), lb = 10 * std::log10 (eb / juce::jmax (1, rb.getNumSamples()) + 1e-20);
            const bool level = std::abs (la - lb) < 0.5;
            const bool bank = B.buildBankNow() && B.getBank() != nullptr && B.getBank()->rate == b;
            if (sameProject && planOk && finite && level && bank) ++ok;
            else info << juce::String (a / 1000.0, 1) << "->" << juce::String (b / 1000.0, 1) << (sameProject ? "" : " project") << (planOk ? "" : " plan") << (finite ? "" : " nan") << (level ? "" : " level " + juce::String (la - lb, 2)) << (bank ? "" : " bank") << "; ";
            B.releaseResources(); A.releaseResources();
        }
    check (ok == n, "save at one rate, restore at another: " + juce::String (ok) + " / " + juce::String (n) + " pairs give the same project, the plan + working copy at the new rate, the same level (< 0.5 dB), a PLAY bank " + info);
}

// ---------------------------------------------------------------- RT safety + worst block time
// a host-like audio callback: blocks paced in real time (the host calls once per block period, the PRIMER thread works in
// between), 0.2 s stopped first (a host calls the callback while stopped too), then play from the cursor for `secs`
struct BlockStats { double worst = 0, sum = 0; int blocks = 0, worstAt = 0; };
static BlockStats pacedBlocks (Rig& rig, int block, double secs, bool midiNotes = false, double stoppedS = 0.2)
{
    const double sr = 48000; BlockStats st;
    juce::AudioBuffer<float> b (2, block); juce::MidiBuffer midi;
    const double period = block / sr;
    double next = juce::Time::getMillisecondCounterHiRes() * 0.001;
    static const bool noPace = std::getenv ("GSLICE_NOPACE") != nullptr;   // (unpaced: the primer cannot keep up - the in-block fallback)
    auto pace = [&]
    {
        if (noPace) return;
        next += period;
        for (;;)
        {
            const double now = juce::Time::getMillisecondCounterHiRes() * 0.001, left = next - now;
            if (left <= 0) break;
            if (left > 0.0015) std::this_thread::sleep_for (std::chrono::microseconds ((int) ((left - 0.001) * 1.0e6))); else std::this_thread::yield();
        }
    };
    for (int done = 0; done < (int) (stoppedS * sr); done += block) { midi.clear(); tlAudio = true; rig->processBlock (b, midi); tlAudio = false; pace(); }
    if (! midiNotes) rig->play (true);
    const int frames = (int) (secs * sr);
    for (int done = 0; done < frames; done += block)
    {
        midi.clear();
        if (midiNotes) { const int k = done / 4800; if (done % 4800 == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 36 + k % 4, (juce::uint8) 100), 0); if (done % 4800 == 2400) midi.addEvent (juce::MidiMessage::noteOff (1, 36 + (k % 4)), 0); }
        const auto t0 = juce::Time::getHighResolutionTicks();
        tlAudio = true; rig->processBlock (b, midi); tlAudio = false;
        const double us = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0) * 1.0e6;
        if (us > st.worst) { st.worst = us; st.worstAt = done; }
        st.sum += us; ++st.blocks;
        pace();
    }
    return st;
}

// the start spikes (phase 18): a LIVE stretcher start (outputSeek) and a HOLD ELASTIC's 100 ms pre-roll are primed in the
// background ahead of time; the worst 64-frame block stays under 60 % of its budget. Machine noise: the median of 20 runs.
static void testBlockTime()
{
    section ("[41] block time at 64-frame buffers: ELASTIC start, REPEAT ratchet, LIVE stretch start (median worst block of 20 runs) < 60 %");
    const double sr = 48000; const int block = 64; const double blockUs = block / sr * 1.0e6;
    struct Mode { const char* name; std::function<juce::AudioBuffer<float>()> audio; std::function<void (Rig&)> setup; double secs; };
    std::vector<Mode> modes;
    modes.push_back ({ "LIVE stretch start", [] { return pad (48000, 3.0); }, [] (Rig& r) { r->apply (*ops::stretchTo (r->project(), r.r0().id, r.r0().length() * 1.3), "s"); }, 0.25 });
    modes.push_back ({ "HOLD ELASTIC start", [] { return vowel (48000); }, [] (Rig& r) { const auto st = hold::pickStable (*r->getSource(), 2400, 34560); r->apply (*ops::addHold (r->project(), r.r0().id, 2400, 34560, st.a, st.b, 64000, HoldMode::Elastic), "h"); }, 0.6 });
    modes.push_back ({ "REPEAT ratchet", [] { return drums (48000, 3.0); }, [] (Rig& r) { int64 id = 0; auto q = ops::addRepeat (r->project(), 24000, 96000, &id); RepeatOp x = *q->findRepeat (id); x.ratchetOn = true; x.ratchet = { -1, 0, 1, 1.5f, 2 }; x.pitchSemis = 3; x.reverse = 1; r->apply (*ops::setRepeat (*q, x), "r"); }, 2.1 });
    for (auto& m : modes)
    {
        Rig rig (m.audio());
        m.setup (rig);
        std::vector<double> worst; int adopted = 0, missed = 0; long allocs = 0;
        for (int run = 0; run < 20; ++run)
        {
            rig->play (false); rig->setCursor (0);
            const int a0 = rig->getPlayer().primesAdopted.load(), m0 = rig->getPlayer().primesMissed.load();
            gAudioAllocs = 0;
            const auto st = pacedBlocks (rig, block, m.secs);
            allocs += gAudioAllocs.load();
            worst.push_back (st.worst);
            adopted += rig->getPlayer().primesAdopted.load() - a0; missed += rig->getPlayer().primesMissed.load() - m0;
        }
        rig->play (false);
        std::sort (worst.begin(), worst.end());
        const double med = 0.5 * (worst[9] + worst[10]);
        std::printf ("    %-20s worst block median %6.1f us (%5.1f %%), min %6.1f, max %6.1f us; starts primed in the background %d, in the block %d\n",
                     m.name, med, med / blockUs * 100.0, worst.front(), worst.back(), adopted, missed);
        check (med < 0.6 * blockUs && allocs == 0 && adopted > 0, juce::String (m.name) + ": the median worst 64-frame block " + juce::String (med / blockUs * 100.0, 1)
               + " % of the budget (< 60 %), " + juce::String (adopted) + " starts primed in the background, no allocation on the audio thread");
    }
}

static void testRtSafety()
{
    section ("[40] RT safety: every play path in 64-frame blocks - no allocation on the audio thread; worst block time");
    const double sr = 48000; const int block = 64; const double blockUs = block / sr * 1.0e6;
    struct Mode { const char* name; std::function<juce::AudioBuffer<float>()> audio; std::function<void (Rig&)> setup; bool midi = false; };
    std::vector<Mode> modes;
    modes.push_back ({ "DIRECT (1x)", [] { return pad (48000, 3.0); }, [] (Rig&) {} });
    modes.push_back ({ "LIVE stretch", [] { return pad (48000, 3.0); }, [] (Rig& r) { r->apply (*ops::stretchTo (r->project(), r.r0().id, r.r0().length() * 1.3), "s"); } });
    modes.push_back ({ "STUDIO playback", [] { return pad (48000, 3.0); }, [] (Rig& r) { Project q = r->project(); q.pitchSemis = 3; r->apply (q, "p"); r->setStudioMode (true); r->waitStudio (60000); } });
    modes.push_back ({ "HOLD SUSTAIN", [] { return vowel (48000); }, [] (Rig& r) { const auto st = hold::pickStable (*r->getSource(), 2400, 34560); r->apply (*ops::addHold (r->project(), r.r0().id, 2400, 34560, st.a, st.b, 64000, HoldMode::Sustain), "h"); } });
    modes.push_back ({ "HOLD ELASTIC (pre-roll)", [] { return vowel (48000); }, [] (Rig& r) { const auto st = hold::pickStable (*r->getSource(), 2400, 34560); r->apply (*ops::addHold (r->project(), r.r0().id, 2400, 34560, st.a, st.b, 64000, HoldMode::Elastic), "h"); } });
    modes.push_back ({ "HOLD TEXTURE preview", [] { return vowel (48000); }, [] (Rig& r) { const auto st = hold::pickStable (*r->getSource(), 2400, 34560); r->apply (*ops::addHold (r->project(), r.r0().id, 2400, 34560, st.a, st.b, 64000, HoldMode::Texture), "h"); } });
    modes.push_back ({ "REPEAT ratchet", [] { return drums (48000, 3.0); }, [] (Rig& r) { int64 id = 0; auto q = ops::addRepeat (r->project(), 24000, 96000, &id); RepeatOp x = *q->findRepeat (id); x.ratchetOn = true; x.ratchet = { -1, 0, 1, 1.5f, 2 }; x.pitchSemis = 3; x.reverse = 1; r->apply (*ops::setRepeat (*q, x), "r"); } });
    modes.push_back ({ "note-edit curve (LIVE)", [] { return stereoOf (voice (48000, fourNotes())); }, [] (Rig& r)
    { Project q = r->project(); q.material = "MELODIC"; r->apply (q, "m"); r->waitAnalysis (60000); auto n = notesNow (*r.p); if (n.size() >= 2) r->apply (*ops::shiftNotes (r->project(), n[1].regionId, { n[1] }, 300.0, true), "n"); } });
    modes.push_back ({ "region pitch (LIVE)", [] { return stereoOf (voice (48000, fourNotes())); }, [] (Rig& r) { auto q = ops::splitAt (r->project(), r.r0().id, 50000); r->apply (*ops::setRegionPitch (*q, q->regions[1].id, -4, 0, 1), "rp"); } });
    modes.push_back ({ "PLAY voices + HOLD VOWEL", [] { return stereoOf (voice (48000, fourNotes())); }, [] (Rig& r)
    {
        Project q = r->project(); q.material = "VOCAL"; q.play.mode = SliceMode::Syllable; r->apply (q, "m"); r->waitAnalysis (60000);
        r->apply (*ops::turnIntoInstrument (r->project(), (int) r->currentSlices().size()), "k");
        Project k = r->project(); for (auto& ks : k.play.keys) { ks.oneShot = false; ks.vowelHold = true; ks.repeat = ks.note % 3 == 0 ? 2 : 0; } r->apply (k, "vh"); r->buildBankNow();
    }, true });
    int clean = 0; juce::String report;
    for (auto& m : modes)
    {
        Rig rig (m.audio());
        m.setup (rig);
        rig->setCursor (0);
        gAudioAllocs = 0;
        const auto st = pacedBlocks (rig, block, 2.5, m.midi);
        const double worst = st.worst, sum = st.sum; const int nb = st.blocks;
        rig->play (false);
        const long allocs = gAudioAllocs.load();
        clean += allocs == 0 ? 1 : 0;
        char line[200]; std::snprintf (line, sizeof (line), "    %-26s allocs %ld  worst block %7.1f us (%5.1f %% of %.0f us)  mean %6.1f us\n", m.name, allocs, worst, worst / blockUs * 100.0, blockUs, sum / nb);
        report << line;
    }
    std::printf ("%s", report.toRawUTF8());
    check (clean == (int) modes.size(), "no allocation on the audio thread in " + juce::String (clean) + " / " + juce::String ((int) modes.size()) + " play paths (operator new hook; worst block times above)");
}

// ---------------------------------------------------------------- plugin smoke: the built VST3 + CLAP
static void testPluginSmoke (const juce::File& artefacts)
{
    section ("[41] plugin smoke: the built VST3 and CLAP load, play MIDI, save / restore their state"); std::printf ("    %s\n", artefacts.getFullPathName().toRawUTF8());
    const auto vst3 = artefacts.getChildFile ("VST3").getChildFile ("Ghost Slice.vst3");
    const auto clap = artefacts.getChildFile ("CLAP").getChildFile ("Ghost Slice.clap");
    {
        juce::String why;
        if (! vst3.exists()) check (false, "VST3 bundle built (" + vst3.getFullPathName() + ")");
        else
        {
           #if JUCE_PLUGINHOST_VST3
            juce::VST3PluginFormat fmt; juce::OwnedArray<juce::PluginDescription> found;
            fmt.findAllTypesForFile (found, vst3.getFullPathName());
            std::unique_ptr<juce::AudioPluginInstance> inst;
            if (! found.isEmpty()) inst = fmt.createInstanceFromDescription (*found[0], 48000.0, 512, why);
            bool ok = inst != nullptr, finite = true, state = false;
            if (ok)
            {
                for (double rate : { 44100.0, 48000.0, 96000.0 })
                {
                    inst->setPlayConfigDetails (0, 2, rate, 512); inst->prepareToPlay (rate, 512);
                    juce::AudioBuffer<float> b (2, 512); juce::MidiBuffer m;
                    for (int i = 0; i < 20; ++i)
                    {   // (no audio loaded: silence is right; every rate must prepare and process, finite)
                        b.clear(); m.clear(); if (i == 2) m.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), 0);
                        inst->processBlock (b, m);
                        for (int c = 0; c < 2; ++c) for (int k = 0; k < 512; ++k) finite = finite && std::isfinite (b.getSample (c, k));
                    }
                    inst->releaseResources();
                }
                juce::MemoryBlock mb; inst->getStateInformation (mb); inst->setStateInformation (mb.getData(), (int) mb.getSize());
                juce::MemoryBlock mb2; inst->getStateInformation (mb2); state = mb.getSize() > 0 && mb == mb2;
            }
            check (ok && finite && state, "VST3: " + (found.isEmpty() ? juce::String ("no plugin found in the bundle") : found[0]->name + " " + found[0]->version) + (ok ? "" : " - " + why)
                   + ", prepares + processes at 44.1 / 48 / 96 kHz, state round trip " + (state ? "identical" : "DIFFERS"));
           #else
            std::printf ("    (VST3 hosting is not compiled into this test binary)\n");
           #endif
        }
    }
   #if GSLICE_SMOKE_CLAP
    {
        juce::File so = clap; if (so.isDirectory()) so = clap.getChildFile ("Contents").getChildFile ("x86_64-linux").getChildFile ("Ghost Slice.so");
        void* h = clap.existsAsFile() ? dlopen (clap.getFullPathName().toRawUTF8(), RTLD_NOW | RTLD_LOCAL) : nullptr;
        bool ok = false, processed = false, state = false; juce::String id;
        if (h != nullptr)
            if (auto* entry = (const clap_plugin_entry_t*) dlsym (h, "clap_entry"))
                if (entry->init (clap.getFullPathName().toRawUTF8()))
                {
                    auto* fac = (const clap_plugin_factory_t*) entry->get_factory (CLAP_PLUGIN_FACTORY_ID);
                    if (fac != nullptr && fac->get_plugin_count (fac) >= 1)
                    {
                        const auto* d = fac->get_plugin_descriptor (fac, 0); id = d->id;
                        static clap_host_t host { CLAP_VERSION, nullptr, "GhostSliceTests", "GhostBlock", "", "1.0",
                                                  [] (const clap_host_t*, const char*) -> const void* { return nullptr; }, [] (const clap_host_t*) {}, [] (const clap_host_t*) {}, [] (const clap_host_t*) {} };
                        const clap_plugin_t* pl = fac->create_plugin (fac, &host, d->id);
                        if (pl != nullptr && pl->init (pl))
                        {
                            ok = true;
                            for (double rate : { 44100.0, 48000.0, 96000.0 })
                            {
                                if (! pl->activate (pl, rate, 32, 512)) { ok = false; break; }
                                pl->start_processing (pl);
                                float l[512] {}, r[512] {}; float* chans[2] { l, r };
                                clap_audio_buffer_t out {}; out.data32 = chans; out.channel_count = 2;
                                static clap_input_events_t inEv { nullptr, [] (const clap_input_events_t*) -> uint32_t { return 0; }, [] (const clap_input_events_t*, uint32_t) -> const clap_event_header_t* { return nullptr; } };
                                static clap_output_events_t outEv { nullptr, [] (const clap_output_events_t*, const clap_event_header_t*) -> bool { return true; } };
                                clap_process_t pr {}; pr.steady_time = 0; pr.frames_count = 512; pr.audio_outputs = &out; pr.audio_outputs_count = 1; pr.in_events = &inEv; pr.out_events = &outEv;
                                for (int i = 0; i < 20; ++i) { if (pl->process (pl, &pr) == CLAP_PROCESS_ERROR) ok = false; pr.steady_time += 512; }
                                processed = ok;
                                pl->stop_processing (pl); pl->deactivate (pl);
                            }
                            if (auto* stx = (const clap_plugin_state_t*) pl->get_extension (pl, CLAP_EXT_STATE))
                            {
                                struct Buf { std::vector<uint8_t> d; size_t rd = 0; } buf;
                                clap_ostream_t os { &buf, [] (const clap_ostream_t* s, const void* p, uint64_t n) -> int64_t { auto* b = (Buf*) s->ctx; b->d.insert (b->d.end(), (const uint8_t*) p, (const uint8_t*) p + n); return (int64_t) n; } };
                                clap_istream_t is { &buf, [] (const clap_istream_t* s, void* p, uint64_t n) -> int64_t { auto* b = (Buf*) s->ctx; const size_t k = std::min<size_t> (n, b->d.size() - b->rd); std::memcpy (p, b->d.data() + b->rd, k); b->rd += k; return (int64_t) k; } };
                                state = stx->save (pl, &os) && ! buf.d.empty() && stx->load (pl, &is);
                            }
                            pl->destroy (pl);
                        }
                    }
                    entry->deinit();
                }
        if (h != nullptr) dlclose (h);
        check (ok && processed && state && id == "io.ghostblock.ghostslice", "CLAP: " + (h == nullptr ? juce::String ("could not load ") + clap.getFullPathName() + " (" + juce::String (dlerror() ? "dlopen error" : "missing") + ")" : "id " + id)
               + ", init / activate / process at 44.1 / 48 / 96 kHz, state save + load " + (state ? "ok" : "FAILED"));
        // the CLAP validator (test-only tool, MIT) when it is on this machine
        const juce::File cv ("/root/work/clap-validator-0.4.1/clap-validator");
        if (cv.existsAsFile() && clap.existsAsFile())
        {
            juce::ChildProcess p; const juce::String out = p.start (juce::StringArray { cv.getFullPathName(), "validate", "--only-failed", clap.getFullPathName() }) ? p.readAllProcessOutput() : juce::String();
            const int code = (int) p.getExitCode();
            juce::String plain; for (int i = 0; i < out.length(); ++i) { if (out[i] == 0x1b) { while (i < out.length() && out[i] != 'm') ++i; continue; } plain << juce::String::charToString (out[i]); }
            juce::String summary; { const int at = plain.lastIndexOf ("tests run"); if (at >= 0) summary = plain.substring (plain.substring (0, at).lastIndexOf ("\n") + 1).upToFirstOccurrenceOf ("\n", false, false).trim(); }
            const int failed = summary.fromFirstOccurrenceOf ("passed, ", false, false).getIntValue();
            int validatorBugs = 0; for (int i = plain.indexOf ("bug in the validator"); i >= 0; i = plain.indexOf (i + 1, "bug in the validator")) ++validatorBugs;
            std::printf ("    clap-validator (exit %d): %s%s\n", code, summary.toRawUTF8(), validatorBugs > 0 ? "; the failure is the validator's own (\"attempt to divide by zero. This is a bug in the validator\": a plugin without parameters)" : "");
            check (code == 0 || (failed > 0 && failed <= validatorBugs), "clap-validator 0.4.1 validate: no failure of the plugin (" + summary + ")");
        }
    }
   #else
    std::printf ("    (CLAP headers not available to this test binary: CLAP smoke skipped)\n");
   #endif
    const auto pv = juce::File ("/usr/local/bin/pluginval");
    if (juce::File ("/usr/bin/pluginval").existsAsFile() || pv.existsAsFile()) std::printf ("    pluginval is installed: run it at strictness 10 on %s\n", vst3.getFullPathName().toRawUTF8());
    else std::printf ("    pluginval is not installed on this machine (which pluginval: none): the strictness-10 run is skipped\n");
}

// ================================================================ phase 17 prototype (research): multi-f0 on synthetic chords
// An iterative harmonic-sum estimator with spectral cancellation (after Klapuri, "Multiple fundamental frequency estimation by
// summing harmonic amplitudes", ISMIR 2006 - the paper only): whitened magnitude spectrum of a 93 ms frame, salience(f0) =
// sum over harmonics h of g(h) * max |X| in the harmonic's band, pick the best f0, subtract its harmonics (smoothed), repeat until the
// salience falls under a share of the first. Reports note precision / recall (exact MIDI) for 2 / 3 / 4-note chords.
namespace poly
{
static std::vector<int> estimate (const std::vector<float>& x, double sr, int maxNotes, bool knownCount, int count)
{
    const int N = 8192; an::FFT fft (N);
    std::vector<double> re ((size_t) N, 0.0), im ((size_t) N, 0.0); const auto w = an::detail::hann (N);
    const int c = (int) x.size() / 2 - N / 2;
    for (int i = 0; i < N; ++i) re[(size_t) i] = x[(size_t) (c + i)] * w[(size_t) i];
    fft.transform (re.data(), im.data(), false);
    const int K = N / 2;
    std::vector<double> mag ((size_t) K);
    for (int k = 0; k < K; ++k) mag[(size_t) k] = std::sqrt (re[(size_t) k] * re[(size_t) k] + im[(size_t) k] * im[(size_t) k]);
    // spectral whitening: divide by a smoothed envelope over 1/3-octave bands (compressed)
    std::vector<double> wm ((size_t) K, 0.0);
    for (int k = 1; k < K; ++k)
    {
        const int a = std::max (1, (int) (k / 1.12)), b = std::min (K - 1, (int) (k * 1.12) + 1);
        double s = 0; for (int j = a; j <= b; ++j) s += mag[(size_t) j] * mag[(size_t) j];
        const double env = std::sqrt (s / (b - a + 1)) + 1e-9;
        wm[(size_t) k] = mag[(size_t) k] * std::pow (env, -0.67);
    }
    std::vector<int> notes; double first = 0;
    for (int it = 0; it < maxNotes; ++it)
    {
        double best = -1; int bm = -1;
        for (int m = 40; m <= 90; ++m)
        {
            const double f0 = 440.0 * std::exp2 ((m - 69) / 12.0); double s = 0;
            for (int h = 1; h <= 20 && h * f0 < 5000.0; ++h)
            {
                const double kc = h * f0 * N / sr; const int a = (int) std::floor (kc * 0.985), b = (int) std::ceil (kc * 1.015);
                double mx = 0; for (int k = a; k <= b && k < K; ++k) mx = std::max (mx, wm[(size_t) k]);
                s += (f0 + 52.0) / (h * f0 + 320.0) * mx;
            }
            if (s > best) { best = s; bm = m; }
        }
        if (bm < 0) break;
        if (it == 0) first = best;
        if (! knownCount && best < 0.38 * first) break;
        if (knownCount && it >= count) break;
        notes.push_back (bm);
        // cancel its harmonics (the bins around each partial, scaled down)
        const double f0 = 440.0 * std::exp2 ((bm - 69) / 12.0);
        for (int h = 1; h <= 20 && h * f0 < 5000.0; ++h)
        {
            const double kc = h * f0 * N / sr; const int a = (int) std::floor (kc * 0.985), b = (int) std::ceil (kc * 1.015);
            for (int k = a; k <= b && k < K; ++k) wm[(size_t) k] *= 0.15;
        }
    }
    return notes;
}
}

static void polyPrototype()
{
    section ("phase 17 prototype: multi-f0 (harmonic sum + cancellation) on synthetic chords - research numbers");
    const double sr = 48000; std::mt19937 rng (17); std::uniform_real_distribution<double> U (0, 1);
    for (int poly : { 1, 2, 3, 4 })
        for (bool known : { true, false })
        {
            int tp = 0, fp = 0, fn = 0, exact = 0; const int trials = 200;
            for (int t = 0; t < trials; ++t)
            {
                std::set<int> chord; while ((int) chord.size() < poly) chord.insert (48 + (int) (U (rng) * 31));
                std::vector<float> x ((size_t) (0.5 * sr), 0.0f);
                for (int m : chord)
                {
                    const double f0 = 440.0 * std::exp2 ((m - 69) / 12.0) * std::exp2 ((U (rng) - 0.5) * 20.0 / 1200.0), B = 0.0004 * U (rng);   // +-10 ct detune, piano-like stretch
                    const double amp = 0.1 + 0.15 * U (rng);
                    for (int h = 1; h <= 16; ++h)
                    {
                        const double fh = h * f0 * std::sqrt (1.0 + B * h * h); if (fh > 0.45 * sr) break;
                        const double a = amp * std::pow (h, -0.8 - 0.6 * U (rng)), ph = U (rng) * 6.283;
                        for (size_t i = 0; i < x.size(); ++i) x[i] += (float) (a * std::sin (6.283185307 * fh * (double) i / sr + ph) * std::exp (-(double) i / sr * (1.0 + h * 0.3)));
                    }
                }
                const auto est = poly::estimate (x, sr, 6, known, poly);
                int hit = 0; for (int m : est) hit += chord.count (m) ? 1 : 0;
                tp += hit; fp += (int) est.size() - hit; fn += poly - hit; exact += hit == poly && (int) est.size() == poly ? 1 : 0;
            }
            const double P = tp / std::max (1.0, (double) (tp + fp)), R = tp / std::max (1.0, (double) (tp + fn));
            std::printf ("    %d-note chords, count %s: precision %.1f %%, recall %.1f %%, F %.1f %%, whole chord right %.1f %% (%d trials)\n",
                         poly, known ? "known  " : "guessed", P * 100, R * 100, 200 * P * R / std::max (1e-9, P + R), 100.0 * exact / trials, trials);
        }
}
