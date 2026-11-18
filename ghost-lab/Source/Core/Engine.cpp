#include "Engine.h"
#include <juce_events/juce_events.h>

namespace glab
{
// one background thread: renders and file writes run here, in order, never on the audio or message thread
struct Engine::Worker : juce::Thread
{
    Worker() : juce::Thread ("Ghost Lab worker") { startThread (juce::Thread::Priority::low); }
    ~Worker() override { signalThreadShouldExit(); wake.signal(); stopThread (30000); }
    void post (std::function<void()> f) { { const juce::ScopedLock l (lock); jobs.push_back (std::move (f)); } wake.signal(); }
    void run() override
    {
        while (! threadShouldExit())
        {
            std::function<void()> f;
            { const juce::ScopedLock l (lock); if (! jobs.empty()) { f = std::move (jobs.front()); jobs.pop_front(); } }
            if (f) f(); else wake.wait (200);
        }
        // closing: finish what is queued (session files must land; renders see the cancel flag and stop at once)
        for (;;)
        {
            std::function<void()> f;
            { const juce::ScopedLock l (lock); if (jobs.empty()) break; f = std::move (jobs.front()); jobs.pop_front(); }
            f();
        }
    }
    juce::CriticalSection lock;
    std::deque<std::function<void()>> jobs;
    juce::WaitableEvent wake;
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);
};

// ------------------------------------------------------------------------------------------------ one session folder per instance
// Each live instance owns its session folder (its sounds on disk). FL's clone / "Replace" hands a second instance the same
// saved state, so the same session id: that instance makes its own folder (a copy of the sounds) instead of sharing one, so
// nothing one instance does can reach another. This list only says which ids are taken; it holds no sound and no settings.
namespace
{
    juce::CriticalSection& liveLock() { static juce::CriticalSection l; return l; }
    juce::StringArray& liveIds() { static juce::StringArray ids; return ids; }
    bool claimSession (const juce::String& id) { const juce::ScopedLock l (liveLock()); if (liveIds().contains (id)) return false; liveIds().add (id); return true; }
    void releaseSession (const juce::String& id) { const juce::ScopedLock l (liveLock()); liveIds().removeString (id); }
}

Engine::Engine()
{
    cdp = std::make_unique<Cdp> (Cdp::findProgramsDir(), Cdp::defaultWorkRoot());
    cdp->cleanStale();
    sessionId = juce::Uuid().toString();
    claimSession (sessionId);
    history.push_back ({});
    chain = { { Op::GrainCloud, true, { Op::GrainCloud, {}, 1 } },
              { Op::SpectralMelt, true, { Op::SpectralMelt, {}, 1 } },
              { Op::TimeWarp, false, { Op::TimeWarp, {}, 1 } },
              { Op::VoiceFusion, false, { Op::VoiceFusion, {}, 1 } } };
    worker = std::make_unique<Worker>();
}

Engine::~Engine()
{
    cancelFlag = true;
    *worker->alive = false;
    worker.reset();
    releaseSession (sessionId);
}

// ------------------------------------------------------------------------------------------------ specimens
void Engine::setSpecimen (AudioPtr a, const juce::String& name)
{
    original = std::move (a);
    specimenName = name;
    origProfile = makeProfile (*original);
    history.clear();
    history.push_back ({ original, origProfile, "ORIGINAL", {}, {}, original, std::make_shared<Profile> (origProfile) });
    cursor = 0;
    persist (original, "original.wav");
    persist (original, "current.wav");
    if (onChange) onChange();
}

void Engine::setPartner (AudioPtr a, const juce::String& name)
{
    partner = std::move (a);
    partnerName = name;
    partProfile = partner != nullptr ? makeProfile (*partner) : Profile();
    if (partner != nullptr) persist (partner, "partner.wav");
    if (onChange) onChange();
}

void Engine::clearPartner()
{
    partner = nullptr; partnerName = {}; partProfile = {};
    sessionFolder().getChildFile ("partner.wav").deleteFile();
    if (onChange) onChange();
}

bool Engine::loadSpecimen (const juce::File& f, juce::String& error)
{
    bool trimmed = false;
    auto a = loadAudio (f, error, &trimmed);
    if (a == nullptr) return false;
    ++specimenNumber;
    setSpecimen (a, f.getFileNameWithoutExtension());
    if (trimmed) error = "Long sound: Ghost Lab kept the first 10 minutes.";
    return true;
}

bool Engine::loadPartner (const juce::File& f, juce::String& error)
{
    bool trimmed = false;
    auto a = loadAudio (f, error, &trimmed);
    if (a == nullptr) return false;
    setPartner (a, f.getFileNameWithoutExtension());
    if (trimmed) error = "Long sound: Ghost Lab kept the first 10 minutes.";
    return true;
}

// ------------------------------------------------------------------------------------------------ history
void Engine::push (Snapshot s)
{
    if (s.source == nullptr) { s.source = original; s.sourceProfile = std::make_shared<Profile> (origProfile); }
    history.resize ((size_t) cursor + 1);   // a new step drops the redo branch
    history.push_back (std::move (s));
    while ((int) history.size() > kMaxHistory) history.erase (history.begin() + 1);   // the original always stays
    cursor = (int) history.size() - 1;
    persist (current().audio, "current.wav");
    if (onChange) onChange();
}

bool Engine::undo()
{
    if (! canUndo() || busy()) return false;
    --cursor;
    followSource();
    persist (current().audio, "current.wav");
    if (onChange) onChange();
    return true;
}

bool Engine::redo()
{
    if (! canRedo() || busy()) return false;
    ++cursor;
    followSource();
    persist (current().audio, "current.wav");
    if (onChange) onChange();
    return true;
}

void Engine::followSource()
{
    const auto& c = current();
    if (c.source == nullptr || c.source == original) return;
    original = c.source;
    origProfile = c.sourceProfile != nullptr ? *c.sourceProfile : makeProfile (*original);
    persist (original, "original.wav");
}

// the part of a sound between two points, with a 5 ms fade where it was cut (an edge that was not moved is left alone)
static AudioPtr cutOut (const Audio& a, float from, float to)
{
    const int n = a.length();
    const int s0 = juce::jlimit (0, n - 1, (int) std::floor ((double) from * n)), s1 = juce::jlimit (s0 + 1, n, (int) std::ceil ((double) to * n));
    auto out = std::make_shared<Audio>();
    out->sampleRate = a.sampleRate;
    out->buf.setSize (a.channels(), s1 - s0);
    for (int c = 0; c < a.channels(); ++c) out->buf.copyFrom (c, 0, a.buf, c, s0, s1 - s0);
    const int fade = juce::jmin ((s1 - s0) / 4, (int) (0.005 * a.sampleRate));
    if (fade > 1)
    {
        if (s0 > 0) for (int c = 0; c < out->channels(); ++c) out->buf.applyGainRamp (c, 0, fade, 0.0f, 1.0f);
        if (s1 < n) for (int c = 0; c < out->channels(); ++c) out->buf.applyGainRamp (c, s1 - s0 - fade, fade, 1.0f, 0.0f);
    }
    return out;
}

static bool trimRange (const AudioPtr& a, float& from, float& to, juce::String& error)
{
    if (a == nullptr) { error = "Load a sound first."; return false; }
    from = juce::jlimit (0.0f, 1.0f, from); to = juce::jlimit (0.0f, 1.0f, to);
    if (to < from) std::swap (from, to);
    if ((double) (to - from) * a->seconds() < Engine::kMinTrimSeconds) { error = "That is too short to keep."; return false; }
    if (from <= 0.0005f && to >= 0.9995f) { error = "Nothing was trimmed."; return false; }
    return true;
}

static juce::String secs (double s) { return juce::String (s, s < 10.0 ? 2 : 1) + " s"; }

bool Engine::trimOriginal (float from, float to, juce::String& error)
{
    if (busy()) { error = "Still working on the last one..."; return false; }
    if (! trimRange (original, from, to, error)) return false;
    const double len = original->seconds();
    auto cut = cutOut (*original, from, to);
    auto prof = std::make_shared<Profile> (makeProfile (*cut));
    original = cut;
    origProfile = *prof;
    persist (original, "original.wav");
    juce::DynamicObject::Ptr r = new juce::DynamicObject();
    r->setProperty ("op", "trimOriginal"); r->setProperty ("from", from); r->setProperty ("to", to);
    push ({ cut, *prof, "TRIMMED ORIGINAL " + secs ((double) from * len) + " to " + secs ((double) to * len), juce::var (r.get()), {}, cut, prof });
    return true;
}

bool Engine::trimProcessed (float from, float to, juce::String& error)
{
    if (busy()) { error = "Still working on the last one..."; return false; }
    auto now = processedAudio();
    if (! trimRange (now, from, to, error)) return false;
    const double len = now->seconds();
    auto cut = cutOut (*now, from, to);
    juce::DynamicObject::Ptr r = new juce::DynamicObject();
    r->setProperty ("op", "trimProcessed"); r->setProperty ("from", from); r->setProperty ("to", to);
    push ({ cut, makeProfile (*cut), "TRIM " + secs ((double) from * len) + " to " + secs ((double) to * len), juce::var (r.get()), {} });
    return true;
}

void Engine::backToOriginal()
{
    if (! hasSpecimen() || busy()) return;
    push ({ original, origProfile, "ORIGINAL", {}, {} });
}

// ------------------------------------------------------------------------------------------------ processing
juce::String Engine::renderChain (const std::vector<Step>& steps, AudioPtr start, AudioPtr partnerNow, Snapshot& out)
{
    AudioPtr a = std::move (start);
    juce::StringArray labels, cmds;
    juce::Array<juce::var> recipe;
    for (auto& s : steps)
    {
        if (cancelFlag) return "Stopped.";
        auto r = render (s, *a, partnerNow.get(), *cdp, &cancelFlag);
        cmds.addArray (r.commands);
        if (r.audio == nullptr) { out.commands = cmds.joinIntoString ("\n"); return r.error; }
        a = r.audio;
        labels.add (s.describe());
        recipe.add (s.toVar());
    }
    out.commands = cmds.joinIntoString ("\n");
    out.audio = a;
    out.profile = makeProfile (*a);
    out.label = labels.joinIntoString (" > ");
    out.recipe = juce::var (recipe);
    return {};
}

void Engine::runAsync (std::function<juce::String (Snapshot&)> job, Done done, bool replaceLast)
{
    if (working.exchange (true)) { if (done) done (false, "Still working on the last one..."); return; }
    cancelFlag = false;
    auto alive = worker->alive;
    worker->post ([this, job, done, alive, replaceLast]
    {
        auto snap = std::make_shared<Snapshot>();
        auto err = job (*snap);
        juce::MessageManager::callAsync ([this, snap, err, done, alive, replaceLast]
        {
            if (! *alive) return;
            working = false;
            lastCommands = snap->commands;
            if (err.isEmpty())
            {
                if (replaceLast && cursor > 0) { history.resize ((size_t) cursor); cursor = (int) history.size() - 1; }
                push (*snap);
            }
            if (done) done (err.isEmpty(), err.isEmpty() ? snap->label : err);
        });
    });
}

AudioPtr Engine::audioUnderLast() const
{
    if (! hasSpecimen()) return nullptr;
    return cursor > 0 ? history[(size_t) cursor - 1].audio : original;
}

void Engine::applyReplacingLast (const Step& s, Done done)
{
    if (! hasSpecimen()) { if (done) done (false, "Drop a sound first."); return; }
    auto start = audioUnderLast(); auto p = partner;
    runAsync ([this, s, start, p] (Snapshot& out) { return renderChain ({ s }, start, p, out); }, done, true);
}

void Engine::apply (const Step& s, Done done)
{
    if (! hasSpecimen()) { if (done) done (false, "Drop a sound first."); return; }
    auto start = current().audio; auto p = partner;
    runAsync ([this, s, start, p] (Snapshot& out) { return renderChain ({ s }, start, p, out); }, done);
}

std::vector<Step> Engine::mutatedChain (juce::uint32 seed) const
{
    std::vector<Step> steps;
    juce::Random rng ((juce::int64) seed * 2654435761u + 17);
    int i = 0;
    for (auto& slot : chain)
    {
        if (! slot.on) continue;
        Step s = slot.step;
        s.op = slot.op;
        s.seed = seed + (juce::uint32) (++i * 101);
        // a mutation: every front control drifts a little around its setting
        for (auto& c : info (s.op).controls)
        {
            if (c.deep || c.id == "backwards") continue;
            const float v = s.get (c.id), span = c.max - c.min;
            s.values[c.id] = juce::jlimit (c.min, c.max, v + (rng.nextFloat() * 2.0f - 1.0f) * 0.18f * span);
        }
        steps.push_back (s);
    }
    if (steps.empty())
    {
        Step m { Op::Mutate, {}, seed };
        m.values["wild"] = 0.35f + rng.nextFloat() * 0.5f;
        steps.push_back (m);
    }
    return steps;
}

juce::StringArray Engine::chainSteps (juce::uint32 seed) const
{
    juce::StringArray a;
    for (auto& s : mutatedChain (seed)) a.add (s.describe());
    return a;
}

void Engine::runChain (juce::uint32 seed, Done done)
{
    if (! hasSpecimen()) { if (done) done (false, "Drop a sound first."); return; }
    auto steps = mutatedChain (seed);
    auto start = current().audio; auto p = partner;
    runAsync ([this, steps, start, p] (Snapshot& out) { return renderChain (steps, start, p, out); }, done);
}

void Engine::breed (float lean, Done done)
{
    if (! hasSpecimen() || ! hasPartner()) { if (done) done (false, "Breeding needs two specimens: drop a second sound first."); return; }
    Step s { Op::Breed, { { "lean", lean } }, 1 };
    auto start = original; auto p = partner;   // breeding always uses the two specimens as they were dropped
    runAsync ([this, s, start, p] (Snapshot& out) { return renderChain ({ s }, start, p, out); }, done);
}

void Engine::grow (const Step& s, bool asPartner, Done done)
{
    if (working.exchange (true)) { if (done) done (false, "Still working on the last one..."); return; }
    cancelFlag = false;
    auto alive = worker->alive;
    worker->post ([this, s, asPartner, done, alive]
    {
        juce::String err;
        AudioPtr a = rooms::grow (s, err);
        juce::MessageManager::callAsync ([this, a, err, s, asPartner, done, alive]
        {
            if (! *alive) return;
            working = false;
            const auto name = s.describe();
            if (a != nullptr) { if (asPartner) setPartner (a, name); else { ++specimenNumber; setSpecimen (a, name); } }
            if (done) done (a != nullptr, a != nullptr ? name : err);
        });
    });
}

bool Engine::growNow (const Step& s, bool asPartner, juce::String& error)
{
    AudioPtr a = rooms::grow (s, error);
    if (a == nullptr) return false;
    if (asPartner) setPartner (a, s.describe()); else { ++specimenNumber; setSpecimen (a, s.describe()); }
    return true;
}

bool Engine::applyNow (const Step& s, juce::String& error)
{
    if (! hasSpecimen()) { error = "Drop a sound first."; return false; }
    Snapshot out;
    error = renderChain ({ s }, current().audio, partner, out);
    lastCommands = out.commands;
    if (error.isNotEmpty()) return false;
    push (out);
    return true;
}

bool Engine::runChainNow (juce::uint32 seed, juce::String& error)
{
    if (! hasSpecimen()) { error = "Drop a sound first."; return false; }
    Snapshot out;
    error = renderChain (mutatedChain (seed), current().audio, partner, out);
    lastCommands = out.commands;
    if (error.isNotEmpty()) return false;
    push (out);
    return true;
}

bool Engine::breedNow (float lean, juce::String& error)
{
    if (! hasSpecimen() || ! hasPartner()) { error = "Breeding needs two specimens."; return false; }
    Snapshot out;
    error = renderChain ({ { Op::Breed, { { "lean", lean } }, 1 } }, original, partner, out);
    lastCommands = out.commands;
    if (error.isNotEmpty()) return false;
    push (out);
    return true;
}

// ------------------------------------------------------------------------------------------------ chain
void Engine::moveSlot (int from, int to)
{
    if (from < 0 || from >= (int) chain.size() || to < 0 || to >= (int) chain.size() || from == to) return;
    auto s = chain[(size_t) from];
    chain.erase (chain.begin() + from);
    chain.insert (chain.begin() + to, s);
    if (onChange) onChange();
}

juce::var Engine::chainToVar() const
{
    juce::Array<juce::var> a;
    for (auto& s : chain)
    {
        auto v = s.step.toVar();
        v.getDynamicObject()->setProperty ("op", info (s.op).id);
        v.getDynamicObject()->setProperty ("on", s.on);
        a.add (v);
    }
    auto* o = new juce::DynamicObject();
    o->setProperty ("ghostlab_chain", 1);
    o->setProperty ("slots", juce::var (a));
    return juce::var (o);
}

bool Engine::chainFromVar (const juce::var& v)
{
    auto* slots = v["slots"].getArray();
    if (slots == nullptr) return false;
    std::vector<ChainSlot> c;
    for (auto& sv : *slots)
    {
        Step s;
        if (! Step::fromVar (sv, s)) continue;
        c.push_back ({ s.op, (bool) sv["on"], s });
    }
    if (c.empty()) return false;
    chain = c;
    if (onChange) onChange();
    return true;
}

static juce::File docs() { return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("GhostBlock/Ghost Lab"); }
juce::File Engine::chainsFolder() { return docs().getChildFile ("Chains"); }
juce::File Engine::exportsFolder() { return docs(); }

bool Engine::saveChain (const juce::String& name, juce::String& error) const
{
    auto f = chainsFolder().getChildFile (juce::File::createLegalFileName (name.isEmpty() ? "My chain" : name) + ".ghostchain");
    chainsFolder().createDirectory();
    if (! f.replaceWithText (juce::JSON::toString (chainToVar()))) { error = "Couldn't save the chain."; return false; }
    return true;
}

bool Engine::loadChain (const juce::File& f, juce::String& error)
{
    auto v = juce::JSON::parse (f.loadFileAsString());
    if (! chainFromVar (v)) { error = "That isn't a Ghost Lab chain."; return false; }
    return true;
}

// ------------------------------------------------------------------------------------------------ persistence
juce::File Engine::sessionFolder() const
{
   #if JUCE_MAC
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Application Support/GhostBlock/Ghost Lab/Sessions");
   #else
    auto base = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("GhostBlock/Ghost Lab/Sessions");
   #endif
    return base.getChildFile (sessionId);
}

void Engine::persist (const AudioPtr& a, const juce::String& name) const
{
    if (a == nullptr || worker == nullptr) return;
    auto f = sessionFolder().getChildFile (name);
    worker->post ([a, f] { juce::String e; f.getParentDirectory().createDirectory(); writeWav (*a, f.getSiblingFile (f.getFileName() + ".tmp"), 32, e);
                           f.getSiblingFile (f.getFileName() + ".tmp").moveFileTo (f); });
}

juce::ValueTree Engine::saveState() const
{
    juce::ValueTree t ("GhostLab");
    t.setProperty ("version", 1, nullptr);
    t.setProperty ("session", sessionId, nullptr);
    t.setProperty ("specimen", specimenName, nullptr);
    t.setProperty ("partner", partnerName, nullptr);
    t.setProperty ("number", specimenNumber, nullptr);
    t.setProperty ("label", hasSpecimen() ? current().label : juce::String(), nullptr);
    t.setProperty ("chain", juce::JSON::toString (chainToVar(), true), nullptr);
    return t;
}

void Engine::restoreState (const juce::ValueTree& t)
{
    if (! t.hasType ("GhostLab")) return;
    chainFromVar (juce::JSON::parse (t["chain"].toString()));
    const auto id = t["session"].toString();
    if (id.isEmpty()) return;
    // read the sounds from the saved session's folder; if another live instance already owns that id, this one takes a
    // new id and writes its own copy (below), so the two never share files
    auto dir = sessionFolder().getSiblingFile (id);
    bool forked = false;
    if (id != sessionId)
    {
        releaseSession (sessionId);
        if (claimSession (id)) sessionId = id;
        else { sessionId = juce::Uuid().toString(); claimSession (sessionId); forked = true; }
    }
    specimenNumber = juce::jmax (1, (int) t["number"]);
    juce::String e;
    if (auto o = readWav (dir.getChildFile ("original.wav"), e))
    {
        original = o; specimenName = t["specimen"].toString(); origProfile = makeProfile (*o);
        history = { { original, origProfile, "ORIGINAL", {}, {}, original, std::make_shared<Profile> (origProfile) } };
        cursor = 0;
        if (auto c = readWav (dir.getChildFile ("current.wav"), e))
            if (c->length() != o->length() || c->buf.getNumChannels() != o->buf.getNumChannels()
                || std::memcmp (c->buf.getReadPointer (0), o->buf.getReadPointer (0), sizeof (float) * (size_t) o->length()) != 0)
            {
                history.push_back ({ c, makeProfile (*c), t["label"].toString(), {}, {}, original, std::make_shared<Profile> (origProfile) });
                cursor = 1;
            }
    }
    if (auto p = readWav (dir.getChildFile ("partner.wav"), e)) { partner = p; partnerName = t["partner"].toString(); partProfile = makeProfile (*p); }
    if (forked)
    {
        persist (original, "original.wav");
        persist (processedAudio(), "current.wav");
        persist (partner, "partner.wav");
    }
    if (onChange) onChange();
}

juce::File Engine::exportProcessed (juce::String& error, const juce::File& where) const
{
    auto a = processedAudio();
    if (a == nullptr) { error = "Drop a sound first."; return {}; }
    auto dir = where != juce::File() ? where : exportsFolder().getChildFile ("Dragged");
    dir.createDirectory();
    auto base = juce::File::createLegalFileName ((specimenName.isEmpty() ? juce::String ("Specimen") : specimenName) + " - " + current().label.replace (" > ", " ")).substring (0, 90);
    auto f = dir.getNonexistentChildFile (base + " - Ghost Lab", ".wav", true);
    if (! writeWav (*a, f, 24, error)) return {};
    return f;
}
}
