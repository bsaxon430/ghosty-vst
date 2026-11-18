#include "BoxProcessor.h"
#include "HostNotify.h"
#include "BoxEditor.h"
#include "FxCore.h"
#include "FxRackImpl.h"
#include "BoxLayers.h"
#include "ExportGate.h"
#include "FxPresets.h"   // (GHOST FOCUS: the units' factory presets)

std::unique_ptr<juce::Component> makeCapturePage (BoxProcessor& p);   // (BoxEditor.cpp: the CAPTURE tab's page)
std::unique_ptr<juce::Component> makeDnaPage (BoxProcessor& p);       // (BoxEditor.cpp: the DNA tab's page)
std::unique_ptr<juce::Component> makeSplitPage (BoxProcessor& p);     // (BoxEditor.cpp: the SPLITS tab's page, GHOST SPLIT)
std::unique_ptr<juce::Component> makeFocusPage (BoxProcessor& p);     // (BoxEditor.cpp: the FOCUS tab's page, GHOST FOCUS)
std::unique_ptr<juce::Component> makePrintPage (BoxProcessor& p);     // (BoxEditor.cpp: the PRINT tab's page, GHOSTPRINT)
std::unique_ptr<juce::Component> makeSpacePage (BoxProcessor& p);     // (BoxEditor.cpp: the G-SPACE tab's page, GHOST SPACE)

static bool notInSnapshot (const juce::String& id)
{   // the look, BYPASS and the SPIRIT BOARD's puck / orbit are not part of a sound (nor the GHOST SCENES switch + its morph)
    return id == "bypass" || id == "box_bypass" || id.startsWith ("box_board_") || id.startsWith ("box_orbit") || id.startsWith ("box_scene");
}
bool BoxProcessor::notInScene (const juce::String& id)
{   // a scene never moves the gain staging or the latency mode either
    return notInSnapshot (id) || id == "box_in" || id == "box_out" || id == "box_fixlat";
}

BoxProcessor::BoxProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                                       .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),   // GHOST GATE pump: SIDECHAIN mode
      apvts (*this, nullptr, "GHOSTBOX", layout())
{
    rack.attach (apvts);
    inGainP = apvts.getRawParameterValue ("box_in"); outGainP = apvts.getRawParameterValue ("box_out");
    mixP = apvts.getRawParameterValue ("box_mix"); bypassP = apvts.getRawParameterValue ("box_bypass");
    bypassParam = dynamic_cast<juce::AudioParameterBool*> (apvts.getParameter ("bypass"));
    layers = std::make_unique<boxlayers::Engine>();
    layers->blendP = apvts.getRawParameterValue ("box_board_blend"); layers->lawP = apvts.getRawParameterValue ("box_board_law");
    layers->srcP = apvts.getRawParameterValue ("box_board_wsrc"); layers->kP = apvts.getRawParameterValue ("box_board_k");
    rack.impl->layerRunning = [this] { return layers != nullptr && layers->active(); };
    rack.impl->layerStatus = [this] () -> juce::String
    {
        if (layers != nullptr && layers->preparing.load()) return "LAYER: preparing the ghosts' racks...";
        if (layers == nullptr || ! layers->active()) return {};
        return "LAYER: " + juce::String (layers->activeNow.load()) + " racks running   CPU " + juce::String (juce::roundToInt (layers->cpuPct.load())) + " %";
    };
    snapshots = std::make_unique<ghosty::SnapshotBank> (ghosty::SnapshotBank::Options { "Ghost Box" });
    snapshots->capture = [this] { return captureSound(); };
    snapshots->restore = [this] (const juce::ValueTree& t) { restoreSound (t); };
    rack.impl->snapshots = snapshots.get();
    rack.impl->extraPages.push_back ({ "CAPTURE", "GHOST CAPTURE  -  the last bars Ghost Box played, as clips: drag them into your DAW",
                                       "GHOST CAPTURE: the last 2 / 4 / 8 / 16 bars of what Ghost Box played, as clips you drag into the DAW, save, loop, reverse or send to Ghost Slice",
                                       0xffFF4FD8, [this] { return makeCapturePage (*this); } });
    rack.impl->extraPages.push_back ({ "DNA", "GHOST DNA  -  breed two sounds, mutate one, evolve four: every result is a normal chain",
                                       "GHOST DNA: BREED two sounds, MUTATE (seeded, with locks), EVOLVE 4 variations to audition, UNDO back to the original. Results are gain-matched and editable",
                                       0xff45E6FF, [this] { return makeDnaPage (*this); } });
    rack.impl->extraPages.push_back ({ "SPLITS", "GHOST SPLIT  -  split the sound into branches, each with its own units, summed back in time",
                                       "GHOST SPLIT: FREQUENCY / TRANSIENT-BODY / MID-SIDE / LEVEL / PARALLEL splits; every branch runs its own units (latency-aligned). Keep an 808's sub clean while the top is distorted",
                                       0xff5AF0C8, [this] { return makeSplitPage (*this); }, true });
    rack.impl->extraPages.push_back ({ "FOCUS", "GHOST FOCUS  -  say what you want, hear 4 starting chains, keep one",
                                       "GHOST FOCUS: pick FAT, WIDER, DARKER, DREAMY, DIRTY, MOVE, WEIRD or PUNCH and hear 4 starting chains built from the rack's own units and presets (gain-matched). KEEP one and refine it",
                                       0xffFFC35A, [this] { return makeFocusPage (*this); }, true });
    rack.impl->extraPages.push_back ({ "PRINT", "GHOSTPRINT  -  drop an audio reference, take its movement (never its sound) as timeline lanes",
                                       "GHOSTPRINT: drop a short audio reference (or a CAPTURE clip); its envelope, accents, brightness and stereo movement become editable GHOST MOTION timeline lanes",
                                       0xff7FFFB0, [this] { return makePrintPage (*this); }, true });
    rack.impl->extraPages.push_back ({ "G-SPACE", "GHOST SPACE  -  move the sound through a space: ORBIT, FALL, GHOST, BLACK HOLE or your own path",
                                       "GHOST SPACE: place the sound in a 2-D field around you (distance = level, air, room; angle = pan and ear delay) and let it ORBIT, FALL, trail a GHOST or fall into a BLACK HOLE",
                                       0xff9C8BFF, [this] { return makeSpacePage (*this); }, true });
    rack.impl->onOtherLoad = [this] { if (snapshots) snapshots->clearCurrent(); rack.impl->box.scenes.requestClear(); };   // (another sound: the scene layer lets go)
    sceneSettingsChanged(); scenesChanged();
    startTimerHz (10);
    sceneTimer.startTimerHz (50);
}

BoxProcessor::~BoxProcessor() { *printAlive = false; printCancel = true; printPool.removeAllJobs (true, 20000); *dnaAlive = false; dnaPool.removeAllJobs (true, 20000); sceneTimer.stopTimer(); stopTimer(); layers.reset(); }

void BoxProcessor::syncLayers()
{
    if (layers == nullptr) return;
    auto* fl = apvts.getRawParameterValue ("box_fixlat");
    layers->sync (*rack.impl, fl == nullptr || fl->load() > 0.5f, sr, maxBlock, rack.latencySamples());
}
bool BoxProcessor::waitLayersReady (int timeoutMs)
{
    if (layers == nullptr) return true;
    auto* fl = apvts.getRawParameterValue ("box_fixlat");
    return layers->waitReady (*rack.impl, fl == nullptr || fl->load() > 0.5f, sr, maxBlock, rack.latencySamples(), timeoutMs);
}

juce::AudioProcessorValueTreeState::ParameterLayout BoxProcessor::layout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    // global controls (prefix box_, so FX scenes never touch them)
    gpfx::addFloat (l, "box_in", "Input Gain", juce::NormalisableRange<float> (-24.0f, 24.0f), 0.0f, "dB", 1);
    gpfx::addFloat (l, "box_out", "Output Gain", juce::NormalisableRange<float> (-24.0f, 24.0f), 0.0f, "dB", 1);
    gpfx::addPct (l, "box_mix", "Dry/Wet", 1.0f);
    gpfx::addBool (l, "box_bypass", "Mute Wet (legacy)", false);   // the pre-3.0 whole-box switch (dry/wet to 0); BYPASS is the shared "bypass" parameter below
    gpfx::Rack::addParameters (l);
    l.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { "bypass", 1 }, "Bypass", false));   // so existing automation keeps its index
    // SPIRIT BOARD LAYER mode (appended after everything older: existing automation keeps its index; defaults = SMOOTH, today's sound)
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "box_board_blend", 1 }, "Spirit Board Blend", boxlayers::blendNames(), 0));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "box_board_law", 1 }, "Spirit Board Weight Law", boxlayers::lawNames(), 0));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "box_board_wsrc", 1 }, "Spirit Board Weights", boxlayers::sourceNames(), 0));
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "box_board_k", 1 }, "Spirit Board Layers", boxlayers::kNames(), 2));
    // GHOST SCENES (appended last: existing automation keeps its index; defaults = no scene, 100 % = what the scenes say, today's sound)
    l.add (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "box_scene", 1 }, "Ghost Scene", juce::StringArray { "-", "1", "2", "3", "4", "5", "6", "7", "8" }, 0));
    gpfx::addPct (l, "box_scene_morph", "Ghost Scene Morph", 1.0f);
    // GHOST SPLIT (GhostyPro/fx/FxRoute.h; appended last: existing automation keeps its index; defaults = off, today's sound)
    gpfx::addRouteParameters (l);
    // GHOST SPACE and the GHOSTPRINT targets (GhostyPro/fx/FxGhostSpace.h; appended last for the same reason; defaults = off / exactly the input)
    gpfx::addSpaceParameters (l);
    gpfx::addPrintParameters (l);
    return l;
}

bool BoxProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet(), in = layouts.getMainInputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo()) return false;
    }
    return in == out || (in == juce::AudioChannelSet::mono() && out == juce::AudioChannelSet::stereo());
}

void BoxProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    rack.prepare (sr, samplesPerBlock);
    rack.reset();
    demo.prepare (sr);
    maxBlock = juce::jmax (64, samplesPerBlock);   // hosts may hand over bigger blocks than announced: those are processed in slices, never reallocated for
    dryBuf.setSize (2, maxBlock, false, true, false);
    rawBuf.setSize (2, maxBlock, false, true, false);
    scBuf.setSize (2, maxBlock, false, true, false);
    const int lat = rack.latencySamples();
    // the dry paths (global MIX, BYPASS) follow the reported latency with a 5 ms crossfade whenever it changes; room for any latency
    dryDelay.prepare (juce::jmax (lat, 16384) + maxBlock, sr); rawDelay.prepare (juce::jmax (lat, 16384) + maxBlock, sr); dryDelayLen = lat;
    bypassFader.prepare (sr, 0.010, bypassParam != nullptr && bypassParam->get());
    if (layers != nullptr) { syncLayers(); layers->prepare (sr, maxBlock); }
    history.prepare (sr, history.isOn());   // GHOST CAPTURE: the ring for this rate (allocated here, never on the audio thread)
    previewAt = 0;
    const int latL = layers != nullptr ? layers->latency() : -1;
    latencyNow = latL >= 0 ? latL : lat; setLatencySamples (latencyNow.load());
    lastIn = lastOut = -1.0f;
    mixNow = mixP->load() * (bypassP->load() > 0.5f ? 0.0f : 1.0f);
}

void BoxProcessor::timerCallback()
{
    syncLayers();
    const int latL = layers != nullptr ? layers->latency() : -1;
    const int lat = latL >= 0 ? latL : rack.latencySamples();
    if (lat != latencyNow.load()) { latencyNow = lat; setLatencySamples (lat); }
}

void BoxProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int total = buffer.getNumSamples();
    const int inCh = getMainBusNumInputChannels(), outCh = getMainBusNumOutputChannels();
    if (total <= 0 || outCh == 0) return;
    rack.setOffline (isNonRealtime());
    if (total <= maxBlock) { processSlice (buffer, inCh, outCh, midi, 0); return; }
    // a block bigger than prepared: slices of the prepared size, as views over the host buffer (no allocation on the audio thread)
    for (int off = 0; off < total; off += maxBlock)
    {
        const int n = juce::jmin (maxBlock, total - off);
        juce::AudioBuffer<float> view (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), off, n);
        processSlice (view, inCh, outCh, midi, off);
    }
}

void BoxProcessor::processSlice (juce::AudioBuffer<float>& buffer, int inCh, int outCh, const juce::MidiBuffer& midi, int midiOffset)
{
    const int n = buffer.getNumSamples();
    // the sidechain (if the host connected one) is read before the main input is widened, since it shares the buffer
    const float* scL = nullptr; const float* scR = nullptr;
    if (auto* scBus = getBus (true, 1); scBus != nullptr && scBus->isEnabled() && scBus->getNumberOfChannels() > 0)
    {
        auto sc = getBusBuffer (buffer, true, 1);
        if (sc.getNumChannels() > 0 && scBuf.getNumSamples() >= n)
        {
            for (int c = 0; c < 2; ++c) scBuf.copyFrom (c, 0, sc, juce::jmin (c, sc.getNumChannels() - 1), 0, n);
            scL = scBuf.getReadPointer (0); scR = scBuf.getReadPointer (1);
        }
    }
    rack.setSidechain (scL, scR, n);
    for (int c = inCh; c < outCh; ++c) buffer.copyFrom (c, 0, buffer, 0, 0, n);   // mono in -> both sides

    // the raw input (before any gain): what BYPASS passes through, time-aligned with the processed path
    for (int c = 0; c < 2; ++c) rawBuf.copyFrom (c, 0, buffer, juce::jmin (c, outCh - 1), 0, n);
    // input gain + meter
    const float gi = gpfx::dbToGain (inGainP->load());
    if (lastIn < 0) lastIn = gi;
    for (int c = 0; c < outCh; ++c) buffer.applyGainRamp (c, 0, n, lastIn, gi);   // the main bus only: a host's sidechain input buffer is read-only (CLAP)
    lastIn = gi;
    for (int c = 0; c < juce::jmin (2, outCh); ++c) inPeak[c].store (buffer.getMagnitude (c, 0, n));

    // keep the dry for the global dry/wet (delayed by the rack's latency so they line up)
    for (int c = 0; c < 2; ++c) dryBuf.copyFrom (c, 0, buffer, juce::jmin (c, outCh - 1), 0, n);

    // host tempo / transport
    gpfx::Transport t; t.sampleRate = sr;
    bool havePpq = false;
    double beatsPerBar = 4.0, barStart = 0.0; bool haveBar = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) t.bpm = *b;
            t.playing = pos->getIsPlaying();
            if (auto q = pos->getPpqPosition()) { t.ppq = *q; havePpq = true; }
            if (auto ts = pos->getTimeSignature(); ts && ts->numerator > 0 && ts->denominator > 0) beatsPerBar = 4.0 * ts->numerator / ts->denominator;
            if (auto bs = pos->getPpqPositionOfLastBarStart()) { barStart = *bs; haveBar = true; }
        }
    if (! havePpq) { t.ppq = freePpq; t.playing = false; }
    t.beatsPerBar = beatsPerBar;   // (GHOST MOTION timeline bars)
    const double spb = 60.0 / juce::jlimit (20.0, 400.0, t.bpm > 1.0 && std::isfinite (t.bpm) ? t.bpm : 120.0) * sr;
    freePpq = t.ppq + (double) n / spb;

    // GHOST SCENES: window commands, box_scene automation and MIDI keys (at their sample) before the rack runs this slice
    auto& scenes = rack.impl->box.scenes;
    scenes.beginBlock (t.ppq, spb, t.playing, beatsPerBar, barStart, haveBar);
    for (const auto m : midi)
    {
        const int at = m.samplePosition - midiOffset;
        if (at < 0 || at >= n) continue;
        const auto msg = m.getMessage();
        if (msg.isNoteOn()) scenes.midiNote (msg.getNoteNumber(), true, t.ppq + (double) at / spb);
        else if (msg.isNoteOff()) scenes.midiNote (msg.getNoteNumber(), false, t.ppq + (double) at / spb);
    }

    if (layers == nullptr || ! layers->process (buffer, t, *rack.impl, scL, scR, [this, &t] (juce::AudioBuffer<float>& b) { rack.process (b, t); }))
        rack.process (buffer, t);   // SMOOTH (and LAYER fully off): the plugin's own rack, as always
    scenes.endBlock();

    const float go = gpfx::dbToGain (outGainP->load());
    if (lastOut < 0) lastOut = go;
    for (int c = 0; c < outCh; ++c) buffer.applyGainRamp (c, 0, n, lastOut, go);
    lastOut = go;

    // global dry/wet and the legacy box_bypass (smoothed); the dry paths are delayed by the reported latency
    const int lat = juce::jmin (latencyNow.load(), 16384);
    dryDelayLen = lat;
    float* dl = dryBuf.getWritePointer (0); float* dr = dryBuf.getWritePointer (1);
    dryDelay.process (dl, dr, n, lat);
    rawDelay.process (rawBuf.getWritePointer (0), rawBuf.getWritePointer (1), n, lat);
    const float target = juce::jlimit (0.0f, 1.0f, scenes.mixValue (mixP->load())) * (bypassP->load() > 0.5f ? 0.0f : 1.0f);   // (GHOST SCENES: DRY/WET too)
    const float step = (target - mixNow) / (float) n;
    float* L = buffer.getWritePointer (0); float* R = outCh > 1 ? buffer.getWritePointer (1) : nullptr;
    if (std::abs (target - mixNow) > 1.0e-6f || target < 1.0f)
        for (int i = 0; i < n; ++i)
        {
            mixNow += step;
            L[i] = dl[i] + mixNow * (L[i] - dl[i]);
            if (R) R[i] = dr[i] + mixNow * (R[i] - dr[i]);
        }
    mixNow = target;

    demo.process (buffer, license.isUnlocked());
    // BYPASS: crossfade to the latency-aligned raw input (10 ms); the rack above kept running, so releasing it is seamless
    bypassFader.set (bypassParam != nullptr && bypassParam->get());
    bypassFader.applyDryWet (buffer, rawBuf);
    // GHOST CAPTURE: exactly what goes back to the host, into the history (before any preview is added)
    history.write (buffer.getReadPointer (0), outCh > 1 ? buffer.getReadPointer (1) : nullptr, n, t.ppq, spb, t.playing);
    lastBpm.store (60.0 * sr / spb, std::memory_order_relaxed); lastBeatsPerBar.store (beatsPerBar, std::memory_order_relaxed);
    lastBarStart.store (barStart, std::memory_order_relaxed); lastHaveBar.store (haveBar, std::memory_order_relaxed);
    if (auto* pv = previewBuf.load (std::memory_order_acquire); pv != nullptr && pv->getNumSamples() > 0)
    {   // LOOP preview of a capture, added to the output
        if (pv != previewSeen) { previewSeen = pv; previewAt = 0; }
        const int len = pv->getNumSamples();
        for (int done = 0; done < n;)
        {
            const int c = juce::jmin (n - done, len - (int) previewAt);
            for (int ch = 0; ch < outCh; ++ch) buffer.addFrom (ch, done, *pv, juce::jmin (ch, pv->getNumChannels() - 1), (int) previewAt, c);
            done += c; previewAt = (previewAt + c) % len;
        }
        previewFrac.store ((float) previewAt / (float) len, std::memory_order_relaxed);
    }
    else previewSeen = nullptr;
    audioBlocks.fetch_add (1, std::memory_order_release);
    float pk = 0;
    for (int c = 0; c < juce::jmin (2, outCh); ++c) { const float m = buffer.getMagnitude (c, 0, n); outPeak[c].store (m); pk = juce::jmax (pk, m, rawBuf.getMagnitude (c, 0, n)); }
    const double now = juce::Time::getMillisecondCounterHiRes();
    lastBlockMs.store (now, std::memory_order_relaxed);
    if (pk > 1.0e-4f) lastLoudMs.store (now, std::memory_order_relaxed);
}

juce::AudioProcessorEditor* BoxProcessor::createEditor() { return new BoxEditor (*this); }

juce::ValueTree BoxProcessor::stateTree()
{
    auto state = apvts.copyState();
    state.setProperty ("ghostBoxVersion", 1, nullptr);
    state.removeProperty ("uiTheme", nullptr); state.removeProperty ("theme_name", nullptr);
    state.setProperty ("theme_name", uiTheme.load() == kLookGhost ? juce::String ("ghost") : ghosty::themes::toString (ghosty::themes::fromInt (uiTheme.load())), nullptr);   // "dark" | "purple" | "light" | "ghost" (older builds read "ghost" as unknown = BLACK)
    juce::ValueTree fxs ("FXRACK"); fxs.appendChild (rack.saveState(), nullptr);
    state.appendChild (fxs, nullptr);
    state.appendChild (sceneBank.toTree(), nullptr);   // GHOST SCENES (an older build ignores the child)
    {   // GHOST CAPTURE settings (the captures themselves live in memory only)
        juce::ValueTree c ("GHOSTCAPTURE"); c.setProperty ("bars", boxcapture::barsOf (captureBarsIdx), nullptr); c.setProperty ("history", history.isOn(), nullptr);
        state.appendChild (c, nullptr);
    }
    {   // GHOST DNA settings (seed, strength, blend, locks)
        juce::ValueTree d ("GHOSTDNA"); d.setProperty ("seed", juce::String (dnaSeed), nullptr); d.setProperty ("strength", dnaStrength, nullptr); d.setProperty ("blend", dnaBlend, nullptr);
        d.setProperty ("lockUnits", dnaLocks.unitsText(), nullptr); d.setProperty ("lockParams", dnaLocks.paramsText(), nullptr);
        state.appendChild (d, nullptr);
    }
    return state;
}

void BoxProcessor::applyStateTree (juce::ValueTree in, bool hostRestore)
{
    if (! in.isValid() || ! in.hasType (apvts.state.getType())) return;
    auto state = in.createCopy();
    const auto fxs = state.getChildWithName ("FXRACK");
    state.removeChild (fxs, nullptr);
    const auto scn = state.getChildWithName ("GHOSTSCENES");   // GHOST SCENES: the slots are session state (a host restore loads them; A/B and snapshots leave them)
    state.removeChild (scn, nullptr);
    const auto cap = state.getChildWithName ("GHOSTCAPTURE");   // GHOST CAPTURE settings (session state, never a sound)
    state.removeChild (cap, nullptr);
    const auto dna = state.getChildWithName ("GHOSTDNA");       // GHOST DNA settings (session state, never a sound)
    state.removeChild (dna, nullptr);
    if (hostRestore)
    {
        dnaSeed = dna.isValid() ? juce::jmax ((juce::int64) 1, dna.getProperty ("seed", "1").toString().getLargeIntValue()) : 1;
        dnaStrength = dna.isValid() ? juce::jlimit (0.0f, 1.0f, (float) dna.getProperty ("strength", 0.35f)) : 0.35f;
        dnaBlend = dna.isValid() ? juce::jlimit (0.0f, 1.0f, (float) dna.getProperty ("blend", 0.5f)) : 0.5f;
        dnaLocks.fromText (dna.getProperty ("lockUnits").toString(), dna.getProperty ("lockParams").toString());
        dnaOriginal = {}; dnaCount = 0; dnaAuditioning = -1; dnaVersion.fetch_add (1);
        focusOriginal = {}; focusAuditioning = -1; focusAmount = 1.0f; focusVersion.fetch_add (1);   // (GHOST FOCUS: a restored session starts fresh)
    }
    if (hostRestore)
    {
        const int b = cap.isValid() ? (int) cap.getProperty ("bars", 4) : 4;
        captureBarsIdx = b >= 16 ? 3 : b >= 8 ? 2 : b >= 4 ? 1 : 0;
        const bool hOn = cap.isValid() ? (bool) cap.getProperty ("history", true) : true;
        if (hOn != history.isOn()) setHistoryOn (hOn);
    }
    if (hostRestore)
    {
        sceneBank.fromTree (scn); scenesChanged(); sceneSettingsChanged(); sceneReturnState = {};
        const auto sp = state.getChildWithProperty ("id", "box_scene");
        rack.impl->box.scenes.requestClear (sp.isValid() ? juce::jlimit (0, gpfx::kScenes, (int) std::lround ((float) sp.getProperty ("value"))) - 1 : -1);
    }
    else rack.impl->box.scenes.requestClear();
    state.removeProperty ("uiTheme", nullptr); state.removeProperty ("theme_name", nullptr);   // a UI preference, not a sound
    // anything this version doesn't find (older or newer state) gets its default
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (! state.getChildWithProperty ("id", rp->getParameterID()).isValid())
            {
                juce::ValueTree c ("PARAM");
                c.setProperty ("id", rp->getParameterID(), nullptr);
                c.setProperty ("value", rp->convertFrom0to1 (rp->getDefaultValue()), nullptr);
                state.appendChild (c, nullptr);
            }
    if (hostRestore)
    {
        // "theme_name" (3.0 themes); older sessions: "uiTheme" = "light" -> LIGHT, anything else / none -> BLACK
        const auto t = in.hasProperty ("theme_name") ? ghosty::themes::fromString (in.getProperty ("theme_name").toString())
                                                      : ghosty::themes::fromLegacyBool (in.getProperty ("uiTheme", "").toString() == "light");
        uiTheme.store (in.getProperty ("theme_name").toString().trim().equalsIgnoreCase ("ghost") ? kLookGhost : ghosty::themes::toInt (t));   // GHOST = look 3
        if (! audioIsLive())
        {   // a reopened project (or a restore while nothing sounds): start exactly where it will stay, no fade-in on the first beat
            apvts.replaceState (state);
            if (fxs.isValid() && fxs.getNumChildren() > 0) rack.loadState (fxs.getChild (0));
            rack.snapOnNextBlock();
            return;
        }
        // the host restores while audio plays (a host preset browser, an undo in the host): the units whose sound changes are ducked
        // to dry around the jump and fade back in; units that stay the same (a same-state restore) keep running untouched
        std::array<bool, gpfx::kNumUnits> which {}; bool all = false;
        for (auto c : state)
            if (c.hasType ("PARAM"))
            {
                const auto id = c.getProperty ("id").toString();
                auto* cur = apvts.getRawParameterValue (id);
                if (cur == nullptr || std::abs (cur->load() - (float) c.getProperty ("value")) <= 1.0e-6f) continue;
                int unit = -1;
                for (int u = 0; u < gpfx::kNumUnits && unit < 0; ++u) if (id.startsWith (juce::String ("fx_") + gpfx::unitInfo (u).key + "_")) unit = u;
                if (unit >= 0) which[(size_t) unit] = true;
                else if (! id.startsWith ("box_in") && ! id.startsWith ("box_out") && id != "box_mix" && id != "bypass" && id != "box_bypass") all = true;   // macros / morph / motion / rack-wide
            }
        if (fxs.isValid() && fxs.getNumChildren() > 0 && ! rack.saveState().isEquivalentTo (fxs.getChild (0))) all = true;
        if (all) which.fill (true);
        rack.impl->duckAround (which, [this, &state, &fxs]
        {
            apvts.replaceState (state);
            if (fxs.isValid() && fxs.getNumChildren() > 0) rack.loadState (fxs.getChild (0));
        });
        return;
    }
    // a live change (A/B): every unit is ducked to dry around the jump, then the rack's own fades bring the new state in
    std::array<bool, gpfx::kNumUnits> all; all.fill (true);
    rack.impl->duckAround (all, [this, &state, &fxs]
    {
        for (auto c : state)
            if (c.hasType ("PARAM"))
            {
                const auto id = c.getProperty ("id").toString();
                if (notInSnapshot (id)) continue;   // A/B and SNAPSHOT compare sounds: never BYPASS or the SPIRIT BOARD puck
                if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 ((float) c.getProperty ("value")));
            }
        if (fxs.isValid() && fxs.getNumChildren() > 0) rack.loadState (fxs.getChild (0));
    });
}

bool BoxProcessor::audioIsLive() const
{   // blocks arrived in the last 250 ms and something above -80 dBFS went in or out in the last 500 ms
    const double now = juce::Time::getMillisecondCounterHiRes(), lb = lastBlockMs.load(), ll = lastLoudMs.load();
    return lb >= 0.0 && now - lb < 250.0 && ll >= 0.0 && now - ll < 500.0;
}

juce::ValueTree BoxProcessor::captureSound()
{
    auto t = stateTree();
    t.removeProperty ("theme_name", nullptr); t.removeProperty ("uiTheme", nullptr);
    t.removeChild (t.getChildWithName ("GHOSTSCENES"), nullptr);   // (a sound never carries the scene slots)
    t.removeChild (t.getChildWithName ("GHOSTCAPTURE"), nullptr);
    t.removeChild (t.getChildWithName ("GHOSTDNA"), nullptr);
    for (int i = t.getNumChildren(); --i >= 0;) { auto c = t.getChild (i); if (c.hasType ("PARAM") && notInSnapshot (c.getProperty ("id").toString())) t.removeChild (i, nullptr); }
    auto fx = t.getChildWithName ("FXRACK");
    if (fx.isValid() && fx.getNumChildren() > 0)
        if (auto box = fx.getChild (0).getChildWithName ("BOX3"); box.isValid()) box.removeChild (box.getChildWithName ("SPIRITBOARD"), nullptr);
    return t;
}
void BoxProcessor::restoreSound (const juce::ValueTree& in)
{
    if (! in.isValid() || ! in.hasType (apvts.state.getType())) return;
    rack.impl->pushUndo();
    const auto keepBoard = rack.impl->box.saveBoard();   // the SPIRIT BOARD stays as it is
    auto t = in.createCopy();
    for (int i = t.getNumChildren(); --i >= 0;) { auto c = t.getChild (i); if (c.hasType ("PARAM") && notInSnapshot (c.getProperty ("id").toString())) t.removeChild (i, nullptr); }
    applyStateTree (t, false);                           // the click-free live load (units ducked around the jump)
    rack.impl->box.loadBoard (keepBoard);
}

void BoxProcessor::abToggle()
{
    rack.impl->pushUndo();
    abTree[abSlot] = stateTree();
    const int other = 1 - abSlot;
    if (! abTree[other].isValid()) abTree[other] = abTree[abSlot].createCopy();   // first use: B starts as a copy of A
    abSlot = other;
    applyStateTree (abTree[abSlot], false);
}

void BoxProcessor::abCopyToOther()
{
    abTree[abSlot] = stateTree();
    abTree[1 - abSlot] = abTree[abSlot].createCopy();
}

void BoxProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = stateTree().createXml()) copyXmlToBinary (*xml, dest);
}

void BoxProcessor::setStateInformation (const void* data, int size)
{
    ghosty::StateLoadNotify notifyHost (*this);   // CLAP: tell the host the values changed (Shared/HostNotify.h)
    if (! license.isUnlocked()) return;   // demo mode: a reopened project starts at the default sound (unlocking brings the saved sound back)
    if (auto xml = getXmlFromBinary (data, size)) applyStateTree (juce::ValueTree::fromXml (*xml));
}

// ================================================================ GHOST SCENES (message thread)
gpfx::SceneEngine& BoxProcessor::sceneEngine() { return rack.impl->box.scenes; }

juce::ValueTree BoxProcessor::sceneSound()
{
    auto t = captureSound();
    for (int i = t.getNumChildren(); --i >= 0;) { auto c = t.getChild (i); if (c.hasType ("PARAM") && notInScene (c.getProperty ("id").toString())) t.removeChild (i, nullptr); }
    return t;
}
void BoxProcessor::sceneStore (int i) { sceneBank.store (i, sceneSound()); scenesChanged(); }
void BoxProcessor::sceneClear (int i) { sceneBank.clear (i); scenesChanged(); }
void BoxProcessor::sceneRename (int i, const juce::String& n) { sceneBank.rename (i, n); scenesChanged(); }
void BoxProcessor::sceneCopy (int from, int to) { sceneBank.copy (from, to); scenesChanged(); }
bool BoxProcessor::sceneUndo() { const bool ok = sceneBank.undo(); if (ok) scenesChanged(); return ok; }

void BoxProcessor::sceneSettingsChanged()
{
    auto& e = sceneEngine(); const auto& st = sceneBank.set;
    e.mode.store (juce::jlimit (0, 2, st.mode)); e.quant.store (juce::jlimit (0, 5, st.quant)); e.morphLen.store (juce::jlimit (0, 6, st.morphLen));
    e.midiOn.store (st.midiOn); e.midiLatch.store (st.midiLatch); e.midiBase.store (juce::jlimit (0, 120, st.midiBase));
    if (onScenesChanged) onScenesChanged();
}

// the chain order a saved rack state asks for (as Rack::loadState + setOrder read it: a permutation, the limiter last)
static bool orderOf (const juce::ValueTree& sound, std::array<int, gpfx::kNumUnits>& o)
{
    const auto fx = sound.getChildWithName ("FXRACK");
    const auto rk = fx.isValid() && fx.getNumChildren() > 0 ? fx.getChild (0) : juce::ValueTree();
    if (! rk.isValid() || ! rk.hasProperty ("order")) return false;
    std::array<bool, gpfx::kNumUnits> seen {}; int k = 0;
    for (auto& key : juce::StringArray::fromTokens (rk.getProperty ("order").toString(), ",", ""))
        for (int u = 0; u < gpfx::kNumUnits; ++u)
            if (key == gpfx::unitInfo (u).key && u != gpfx::uLim && ! seen[(size_t) u] && k < gpfx::kNumUnits - 1) { seen[(size_t) u] = true; o[(size_t) k++] = u; }
    for (int u = 0; u < gpfx::kNumUnits; ++u) if (u != gpfx::uLim && ! seen[(size_t) u]) o[(size_t) k++] = u;
    o[gpfx::kNumUnits - 1] = gpfx::uLim;
    return true;
}

void BoxProcessor::scenesChanged()
{   // the audio side's targets: every shadow slot's value as the host parameter will hold it after the commit (missing = default)
    auto& box = rack.impl->box;
    auto d = std::make_unique<gpfx::SceneData>();
    const int S = box.numSlots();
    auto* mixPrm = apvts.getParameter ("box_mix");
    for (int k = 0; k < boxscenes::kSlots; ++k)
    {
        const auto& sl = sceneBank.slots[(size_t) k];
        if (! sl.filled()) continue;
        auto& to = d->to[(size_t) k]; to.resize ((size_t) S);
        auto valueOf = [&sl] (juce::RangedAudioParameter* p, const juce::String& id)
        {
            const auto c = sl.sound.getChildWithProperty ("id", id);
            const float v = c.isValid() ? (float) c.getProperty ("value") : p->convertFrom0to1 (p->getDefaultValue());
            return p->convertFrom0to1 (p->convertTo0to1 (v));
        };
        for (int i = 0; i < S; ++i)
        {
            auto* p = box.slotParam (i);
            to[(size_t) i] = p != nullptr ? valueOf (p, box.slotId (i)) : 0.0f;
        }
        d->mix[(size_t) k] = mixPrm != nullptr ? valueOf (mixPrm, "box_mix") : 1.0f;
        d->hasOrder[(size_t) k] = orderOf (sl.sound, d->order[(size_t) k]);
    }
    box.scenes.publish (std::move (d));
    if (onScenesChanged) onScenesChanged();
}

void BoxProcessor::applySceneSound (const juce::ValueTree& t)
{   // the host parameters + the rack's own state become the scene (what the audio side already plays: nothing moves)
    if (! t.isValid()) return;
    rack.impl->pushUndo();   // a scene change can be undone (UNDO / Ctrl+Z)
    ++rack.impl->internalEdit;
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
        {
            const auto id = rp->getParameterID();
            if (notInScene (id) || ! (id.startsWith ("fx_") || id.startsWith ("box_"))) continue;
            const auto c = t.getChildWithProperty ("id", id);
            const float v01 = c.isValid() ? rp->convertTo0to1 ((float) c.getProperty ("value")) : rp->getDefaultValue();
            if (std::abs (rp->getValue() - v01) > 1.0e-7f) rp->setValueNotifyingHost (v01);
        }
    --rack.impl->internalEdit;
    const auto fx = t.getChildWithName ("FXRACK");
    if (fx.isValid() && fx.getNumChildren() > 0)
    {
        auto cur = rack.saveState();
        if (auto box = cur.getChildWithName ("BOX3"); box.isValid()) box.removeChild (box.getChildWithName ("SPIRITBOARD"), nullptr);
        if (! cur.isEquivalentTo (fx.getChild (0)))
        {
            const auto keepBoard = rack.impl->box.saveBoard();   // the SPIRIT BOARD stays as it is
            rack.loadState (fx.getChild (0));
            rack.impl->box.loadBoard (keepBoard);
        }
    }
    if (snapshots) snapshots->clearCurrent();
}

void BoxProcessor::sceneCommitNow()
{
    auto& e = sceneEngine();
    const int seq = e.landSeq.load (std::memory_order_acquire);
    if (seq != e.commitSeq.load())
    {
        const int scene = e.landScene.load(); const bool ret = e.landReturn.load(), hold = e.landHold.load();
        if (e.landSeq.load (std::memory_order_acquire) != seq) return;   // another landing meanwhile: the next tick commits the newest
        juce::ValueTree target;
        if (ret)
        {
            target = sceneReturnState; sceneReturnState = {};
            if (! target.isValid() && boxscenes::Bank::valid (scene) && sceneBank.slots[(size_t) scene].filled()) target = sceneBank.slots[(size_t) scene].sound;
        }
        else
        {
            if (hold && ! sceneReturnState.isValid()) sceneReturnState = sceneSound();   // the host still holds the sound before the hold
            else if (! hold) sceneReturnState = {};
            if (boxscenes::Bank::valid (scene)) target = sceneBank.slots[(size_t) scene].sound;
        }
        applySceneSound (target);
        e.commitSeq.store (seq, std::memory_order_release);
        if (onScenesChanged) onScenesChanged();
    }
    // box_scene follows the active scene (a gesture, so hosts in write / touch mode record it)
    const int want = e.active.load() + 1;
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter ("box_scene")); p != nullptr && p->getIndex() != juce::jlimit (0, gpfx::kScenes, want))
    {
        p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 ((float) juce::jlimit (0, gpfx::kScenes, want))); p->endChangeGesture();
    }
}

int BoxProcessor::sceneFillStarters()
{   // factory rack scenes (existing units, designed sounds) as starters, built on a private rack: what plays is not touched
    static const char* keys[boxscenes::kSlots] { "dry", "retrofat", "dustydream", "crackedhall", "galaxyecho", "spectralfreeze", "warpflick", "tapedrop" };
    int n = 0; bool pushed = false;
    for (int k = 0; k < boxscenes::kSlots; ++k)
    {
        if (sceneBank.slots[(size_t) k].filled()) continue;
        boxlayers::LayerHost h;
        h.rack.applyScene (h.apvts, keys[k]);
        auto t = h.apvts.copyState();
        juce::ValueTree fxs ("FXRACK"); fxs.appendChild (h.rack.saveState(), nullptr); t.appendChild (fxs, nullptr);
        if (auto box = fxs.getChild (0).getChildWithName ("BOX3"); box.isValid()) box.removeChild (box.getChildWithName ("SPIRITBOARD"), nullptr);
        for (int i = t.getNumChildren(); --i >= 0;) { auto c = t.getChild (i); if (c.hasType ("PARAM") && notInScene (c.getProperty ("id").toString())) t.removeChild (i, nullptr); }
        // the box globals of the live sound (MIX, macros ...) are kept, so a starter never jumps the level
        for (auto* id : { "box_mix" }) if (auto* rp = apvts.getRawParameterValue (id)) if (auto c = t.getChildWithProperty ("id", id); c.isValid()) c.setProperty ("value", rp->load(), nullptr);
        if (! pushed) { sceneBank.pushUndo(); pushed = true; }
        sceneBank.slots[(size_t) k].sound = t; ++n;
    }
    if (n > 0) scenesChanged();
    return n;
}

// ================================================================ GHOST CAPTURE (message thread)
juce::File BoxProcessor::captureDir()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Ghosty").getChildFile ("Ghost Box").getChildFile ("Captures");
}

void BoxProcessor::setHistoryOn (bool on)
{
    if (on == history.isOn()) return;
    history.prepare (sr, on);   // on: a fresh ring (filled from now); off: freed once the audio thread has left it
    shelfVersion.fetch_add (1);
}

juce::String BoxProcessor::captureNow()
{
    retirePreview(); history.retire();
    const int bars = boxcapture::barsOf (captureBarsIdx);
    auto r = history.capture (bars, lastBeatsPerBar.load(), lastBarStart.load(), lastHaveBar.load(), latencyNow.load());
    if (! r.ok) return r.why;
    auto c = std::make_shared<boxcapture::Clip>();
    c->id = nextClipId++; c->sr = r.sr; c->bpm = r.bpm; c->bars = bars; c->barAligned = r.barAligned; c->timeMs = juce::Time::currentTimeMillis();
    c->audio = std::make_shared<const juce::AudioBuffer<float>> (std::move (r.audio));
    c->name = juce::String (bars) + " bars"; c->makePeaks();
    shelf.insert (shelf.begin(), c);
    while ((int) shelf.size() > boxcapture::kShelf) { if (shelf.back()->id == previewId) clipPreview (previewId, false); shelf.pop_back(); }
    shelfVersion.fetch_add (1);
    return "Captured " + juce::String (bars) + " bars (" + juce::String (c->audio->getNumSamples() / c->sr, 1) + " s at " + juce::String (c->bpm, 1) + " BPM)" + (r.barAligned ? "" : ", not bar-aligned (transport stopped or a jump)");
}

std::shared_ptr<boxcapture::Clip> BoxProcessor::clipById (int id) const
{
    for (auto& c : shelf) if (c->id == id) return c;
    return nullptr;
}

void BoxProcessor::retirePreview()
{   // buffers the audio thread may still read are freed once it has finished two blocks after they were replaced (or it is not running)
    const auto b = audioBlocks.load (std::memory_order_acquire);
    const bool idle = lastBlockMs.load() < 0.0 || juce::Time::getMillisecondCounterHiRes() - lastBlockMs.load() > 500.0;
    previewKeep.erase (std::remove_if (previewKeep.begin(), previewKeep.end(), [b, idle] (const auto& k) { return idle || b >= k.second + 2; }), previewKeep.end());
}

void BoxProcessor::clipPreview (int id, bool on)
{
    retirePreview();
    auto c = on ? clipById (id) : nullptr;
    if (previewHold) previewKeep.push_back ({ previewHold, audioBlocks.load() });
    previewHold = c != nullptr ? c->audio : nullptr;
    previewBuf.store (previewHold.get(), std::memory_order_release);
    previewId = c != nullptr ? id : -1;
    shelfVersion.fetch_add (1);
}
float BoxProcessor::previewPos() const { return previewFrac.load (std::memory_order_relaxed); }

void BoxProcessor::clipReverse (int id)
{
    auto c = clipById (id); if (c == nullptr || c->audio == nullptr) return;
    auto rev = std::make_shared<juce::AudioBuffer<float>> (*c->audio);
    for (int ch = 0; ch < rev->getNumChannels(); ++ch) rev->reverse (ch, 0, rev->getNumSamples());
    c->audio = rev; c->reversed = ! c->reversed; c->makePeaks();
    if (previewId == id) clipPreview (id, true);   // the loop plays the new direction
    shelfVersion.fetch_add (1);
}

void BoxProcessor::clipRemove (int id)
{
    if (previewId == id) clipPreview (id, false);
    shelf.erase (std::remove_if (shelf.begin(), shelf.end(), [id] (const auto& c) { return c->id == id; }), shelf.end());
    shelfVersion.fetch_add (1);
}

bool BoxProcessor::clipDrag (int id, juce::Component* ui)
{
    auto c = clipById (id); if (c == nullptr) return false;
    if (! ghosty::exportAllowed (license, ui, "box.capture.drag")) return false;   // demo: refused before anything is written
    const auto f = captureDir().getNonexistentChildFile (c->fileStem(), ".wav", false);
    if (! boxcapture::writeWav (*c, f)) return false;
    return ghosty::exportgate::dragFiles (license, juce::StringArray { f.getFullPathName() }, ui);
}

void BoxProcessor::clipSaveAs (int id, juce::Component* ui)
{
    auto c = clipById (id); if (c == nullptr) return;
    if (! ghosty::exportAllowed (license, ui, "box.capture.save")) return;
    const auto initial = captureDir().getChildFile (c->fileStem() + ".wav");
    std::weak_ptr<boxcapture::Clip> wc = c;
    ghosty::exportgate::saveDialog (captureChooser, "Save the capture as a WAV file", initial, "*.wav", [this, wc] (const juce::File& f)
    {
        auto cc = wc.lock(); if (cc == nullptr || ! ghosty::exportUnlocked (license)) return;
        boxcapture::writeWav (*cc, f.withFileExtension (".wav"));
    });
}

bool BoxProcessor::clipSendToSlice (int id, juce::Component* ui)
{
    auto c = clipById (id); if (c == nullptr) return false;
    if (! ghosty::exportAllowed (license, ui, "box.capture.slice")) return false;
    const auto f = captureDir().getNonexistentChildFile (c->fileStem(), ".wav", false);
    if (! boxcapture::writeWav (*c, f)) return false;
    const auto side = f.getSiblingFile (f.getFileNameWithoutExtension() + ".ghostclip.json");
    if (! side.replaceWithText (c->sidecarJson())) return false;
    return ghosty::exportgate::dragFiles (license, juce::StringArray { f.getFullPathName(), side.getFullPathName() }, ui);
}

// ================================================================ GHOST DNA (message thread; the offline renders on a worker thread)
boxdna::Genome& BoxProcessor::genome() { if (dnaGenome == nullptr) dnaGenome = std::make_unique<boxdna::Genome> (*this); return *dnaGenome; }

juce::StringArray BoxProcessor::dnaSourceNames (std::vector<int>& ids)
{
    juce::StringArray n; ids.clear();
    n.add ("WHAT YOU HEAR"); ids.push_back (0);
    for (int k = 0; k < boxscenes::kSlots; ++k) if (sceneBank.slots[(size_t) k].filled()) { n.add ("SCENE " + juce::String (k + 1) + "  " + sceneBank.slots[(size_t) k].name); ids.push_back (1 + k); }
    if (snapshots) for (int i = 0; i < juce::jmin (12, snapshots->size()); ++i) { n.add ("SNAPSHOT  " + snapshots->info (i).name); ids.push_back (100 + i); }
    return n;
}
juce::ValueTree BoxProcessor::dnaSource (int id)
{
    if (id >= 1 && id <= boxscenes::kSlots && sceneBank.slots[(size_t) (id - 1)].filled()) return sceneBank.slots[(size_t) (id - 1)].sound.createCopy();
    if (id >= 100 && snapshots != nullptr) { auto t = snapshots->stateOf (id - 100); if (t.isValid()) return t; }
    return dnaAuditioning >= 0 && dnaOriginal.isValid() ? dnaOriginal.createCopy() : captureSound();   // what you hear (while auditioning: the original)
}

float BoxProcessor::loudnessOf (const juce::ValueTree& sound, double rate)
{   // offline, on a private rack with the plugin's whole parameter set (MACROS / MORPH / MOTION ride on it as in the plugin), plus the
    // box's IN / OUT gains and DRY/WET around it: the K-weighted loudness of the fixed test signal through this sound (any thread)
    // (this runs on the DNA / FOCUS worker thread: the host owns an APVTS with a Timer, and a Timer callback running on the message thread
    //  must never meet its destructor on this thread - so the host is destroyed on the message thread)
    std::shared_ptr<boxlayers::LayerHost> hostPtr (new boxlayers::LayerHost, [] (boxlayers::LayerHost* p)
    {
        if (juce::MessageManager::getInstanceWithoutCreating() == nullptr || juce::MessageManager::getInstanceWithoutCreating()->isThisTheMessageThread()) delete p;
        else juce::MessageManager::callAsync ([p] { delete p; });
    });
    auto& h = *hostPtr;
    for (auto* ap : h.getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (ap))
        {
            const auto c = sound.getChildWithProperty ("id", rp->getParameterID());
            rp->setValueNotifyingHost (c.isValid() ? rp->convertTo0to1 ((float) c.getProperty ("value")) : rp->getDefaultValue());
        }
    if (auto* fl = h.apvts.getParameter ("box_fixlat")) fl->setValueNotifyingHost (1.0f);
    const auto fx = sound.getChildWithName ("FXRACK");
    if (fx.isValid() && fx.getNumChildren() > 0) { auto rk = fx.getChild (0).createCopy(); if (auto b = rk.getChildWithName ("BOX3"); b.isValid()) b.removeChild (b.getChildWithName ("SPIRITBOARD"), nullptr); h.rack.loadState (rk); }
    h.prepareLayer (rate, 512);
    h.rack.impl->applyPendingOrderNow(); h.rack.snapOnNextBlock();
    auto val = [&sound] (const char* id, float def) { const auto c = sound.getChildWithProperty ("id", id); return c.isValid() ? (float) c.getProperty ("value") : def; };
    const float gin = gpfx::dbToGain (val ("box_in", 0.0f)), gout = gpfx::dbToGain (val ("box_out", 0.0f)), mix = juce::jlimit (0.0f, 1.0f, val ("box_mix", 1.0f)) * (val ("box_bypass", 0.0f) > 0.5f ? 0.0f : 1.0f);
    const auto& sig = boxdna::testSignal (rate);
    gpfx::Loudness lu; lu.prepare (rate);
    juce::AudioBuffer<float> b (2, 512), dry (2, 512); gpfx::LatentDry dd; dd.prepare (16384 + 512, rate);
    gpfx::Transport t; t.sampleRate = rate; t.bpm = 120.0; t.playing = true;
    for (int pos = 0; pos < sig.getNumSamples(); pos += 512)
    {
        const int n = juce::jmin (512, sig.getNumSamples() - pos);
        juce::AudioBuffer<float> view (b.getArrayOfWritePointers(), 2, 0, n);
        for (int c = 0; c < 2; ++c) { view.copyFrom (c, 0, sig, c, pos, n); view.applyGain (c, 0, n, gin); dry.copyFrom (c, 0, view, c, 0, n); }
        t.ppq = pos / (0.5 * rate);
        h.rack.process (view, t);
        dd.process (dry.getWritePointer (0), dry.getWritePointer (1), n, juce::jmin (16384, h.rack.latencySamples()));
        for (int c = 0; c < 2; ++c) { float* w = view.getWritePointer (c); const float* d = dry.getReadPointer (c); for (int i = 0; i < n; ++i) w[i] = d[i] + mix * (w[i] * gout - d[i]); }
        if (pos >= (int) (0.5 * rate)) lu.process (view.getReadPointer (0), view.getReadPointer (1), n);
    }
    return lu.lufs;
}
juce::ValueTree BoxProcessor::gainMatched (const juce::ValueTree& original, float lOrig, juce::ValueTree result, double rate, float* trimOut, float* lFinal)
{   // a variation is never just louder: louder = the OUTPUT (after the limiter) turned down by the difference; quieter = the limiter's
    // input GAIN raised (up to +12 dB, so a boost can never clip), measured again, then the OUTPUT takes off any overshoot
    auto setVal = [&result] (const char* id, float v) { auto c = result.getChildWithProperty ("id", id); if (! c.isValid()) { c = juce::ValueTree ("PARAM"); c.setProperty ("id", id, nullptr); result.appendChild (c, nullptr); } c.setProperty ("value", v, nullptr); };
    auto valOf = [] (const juce::ValueTree& t, const char* id, float def) { const auto c = t.getChildWithProperty ("id", id); return c.isValid() ? (float) c.getProperty ("value") : def; };
    const float outBase = valOf (original, "box_out", 0.0f);
    float trim = 0.0f;
    const float l1 = loudnessOf (result, rate);
    float lEnd = l1;
    if (lOrig > -69.0f && l1 > -69.0f)
    {
        const float d = lOrig - l1;
        if (d <= 0.0f) { trim = juce::jmax (-24.0f, d); setVal ("box_out", juce::jlimit (-24.0f, 24.0f, outBase + trim)); lEnd = l1 + trim; }
        else
        {
            const float gBase = valOf (result, "fx_lim_gain", 0.0f), boost = juce::jmin (12.0f, d, 18.0f - gBase);
            setVal ("fx_lim_gain", gBase + boost);
            const float l2 = loudnessOf (result, rate), over = juce::jmin (0.0f, lOrig - l2);   // (a limiter may not give all of it back: never louder)
            trim = boost + over;
            setVal ("box_out", juce::jlimit (-24.0f, 24.0f, outBase + over)); lEnd = l2 + over;
        }
    }
    else setVal ("box_out", outBase);
    if (trimOut) *trimOut = trim;
    if (lFinal) *lFinal = lEnd;
    return result;
}
juce::ValueTree BoxProcessor::dnaResult (const DnaJob& j, juce::int64 seed, float lOrig, std::function<juce::ValueTree (juce::int64)> make, float& trim, juce::int64& used)
{   // a result that ends up far quieter than the original even after the match (a gate that never opens, a filter shut ...: more
    // than 1 LU off after the match) is rolled again with the next seeds (up to 6, the closest kept) (deterministic: seed, seed + 7919, ...; the one used is shown)
    juce::ValueTree best; float bestGap = 1.0e9f;
    for (int k = 0; k < 6; ++k)
    {
        const auto s = seed + (juce::int64) k * 7919;
        float t = 0, lEnd = lOrig; auto r = gainMatched (j.original, lOrig, make (s), j.rate, &t, &lEnd);
        const float gap = std::abs (lOrig - lEnd);
        if (gap < bestGap) { bestGap = gap; best = r; trim = t; used = s; }
        if (gap <= 1.0f) break;
    }
    return best;
}

void BoxProcessor::dnaStart (bool evolve, std::function<void (DnaJob&)> work)
{   // a job on the worker thread: the results come back on the message thread (dnaVersion bumps; the page refreshes)
    if (dnaBusy.exchange (true)) return;
    if (! dnaOriginal.isValid()) dnaOriginal = captureSound();   // UNDO TO ORIGINAL goes back here
    auto job = std::make_shared<DnaJob>();
    job->original = dnaOriginal.createCopy(); job->rate = sr; job->seed = dnaSeed; job->strength = dnaStrength; job->blend = dnaBlend; job->locks = dnaLocks; job->evolve = evolve;
    dnaSeed += evolve ? 4 : 1;   // the next press rolls new dice (each result shows the seed that made it)
    genome();   // (made on the message thread)
    auto alive = dnaAlive;
    dnaPool.addJob ([this, job, work, alive]
    {
        work (*job);
        juce::MessageManager::callAsync ([this, job, alive]
        {
            if (! *alive) return;
            dnaFinish (*job);
        });
    });
}
void BoxProcessor::dnaFinish (DnaJob& job)
{
    dnaBusy.store (false);
    if (job.applyNow && job.results.size() == 1) { dnaVariations = {}; dnaTrim = {}; dnaNotes = {}; dnaVariations[0] = job.results[0]; dnaTrim[0] = job.trims[0]; dnaNotes[0] = job.notes[0]; dnaCount = 1; dnaAudition (0); }
    else
    {
        dnaVariations = {}; dnaTrim = {}; dnaNotes = {}; dnaCount = (int) std::min<size_t> (4, job.results.size());
        for (int k = 0; k < dnaCount; ++k) { dnaVariations[(size_t) k] = job.results[(size_t) k]; dnaTrim[(size_t) k] = job.trims[(size_t) k]; dnaNotes[(size_t) k] = job.notes[(size_t) k]; dnaSeeds[(size_t) k] = job.seeds[(size_t) k]; }
        dnaAuditioning = -1;
    }
    dnaMessage = job.message;
    if (job.applyNow) dnaSeeds[0] = job.seeds.empty() ? job.seed : job.seeds[0];
    dnaVersion.fetch_add (1);
}

void BoxProcessor::dnaMutate()
{
    dnaStart (false, [this] (DnaJob& j)
    {
        const float lo = loudnessOf (j.original, j.rate);
        float trim = 0; juce::int64 used = j.seed;
        auto r = dnaResult (j, j.seed, lo, [this, &j] (juce::int64 s) { return genome().mutate (j.original, s, j.strength, j.locks); }, trim, used);
        j.results.push_back (r); j.trims.push_back (trim); j.notes.push_back (genome().diffText (j.original, r)); j.seeds.push_back (used); j.applyNow = true;
        j.message = "MUTATE seed " + juce::String (used) + ": " + j.notes.back();
    });
}
void BoxProcessor::dnaBreed (int a, int b)
{
    auto A = dnaSource (a), B = dnaSource (b);
    dnaStart (false, [this, A, B] (DnaJob& j)
    {
        const float lo = loudnessOf (j.original, j.rate);
        float trim = 0; juce::int64 used = j.seed;
        auto r = dnaResult (j, j.seed, lo, [this, &j, A, B] (juce::int64 s) { return genome().breed (A, B, s, j.blend, j.locks); }, trim, used);
        j.results.push_back (r); j.trims.push_back (trim); j.notes.push_back (genome().diffText (j.original, r)); j.seeds.push_back (used); j.applyNow = true;
        j.message = "BREED seed " + juce::String (used) + ": " + j.notes.back();
    });
}
void BoxProcessor::dnaEvolve()
{
    dnaStart (true, [this] (DnaJob& j)
    {
        const float lo = loudnessOf (j.original, j.rate);
        for (int k = 0; k < 4; ++k)
        {
            float trim = 0; juce::int64 used = j.seed + k;
            auto r = dnaResult (j, j.seed + k, lo, [this, &j] (juce::int64 s) { return genome().mutate (j.original, s, j.strength, j.locks); }, trim, used);
            j.results.push_back (r); j.trims.push_back (trim); j.notes.push_back (genome().diffText (j.original, r)); j.seeds.push_back (used);
        }
        j.message = "EVOLVE: 4 variations from seed " + juce::String (j.seed) + ", click one to hear it";
    });
}
void BoxProcessor::dnaAudition (int k)
{
    if (! juce::isPositiveAndBelow (k, dnaCount) || ! dnaVariations[(size_t) k].isValid()) return;
    if (! dnaOriginal.isValid()) dnaOriginal = captureSound();
    restoreSound (dnaVariations[(size_t) k]);   // the click-free live load (an UNDO step)
    dnaAuditioning = k; dnaVersion.fetch_add (1);
}
void BoxProcessor::dnaKeep()
{   // what you hear stays; the next DNA action starts from it
    dnaOriginal = {}; dnaAuditioning = -1; dnaCount = 0; dnaVariations = {}; dnaMessage = "Kept"; dnaVersion.fetch_add (1);
}
void BoxProcessor::dnaUndo()
{
    if (dnaOriginal.isValid()) restoreSound (dnaOriginal);
    dnaOriginal = {}; dnaAuditioning = -1; dnaCount = 0; dnaVariations = {}; dnaMessage = "Back to the original"; dnaVersion.fetch_add (1);
}
bool BoxProcessor::dnaWait (int timeoutMs)
{   // tests: until the worker's results arrived
    const auto t0 = juce::Time::getMillisecondCounter();
    while (dnaBusy.load())
    {
#if JUCE_MODAL_LOOPS_PERMITTED
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
#else
        juce::Thread::sleep (5);   // plugin builds have no modal loops; this hook is for tests only
#endif
        if (juce::Time::getMillisecondCounter() - t0 > (juce::uint32) timeoutMs) return false;
    }
    return true;
}

// ================================================================ GHOST FOCUS (message thread; the loudness match on the worker thread)
juce::ValueTree BoxProcessor::focusChainSound (int intent, int k, const juce::ValueTree& original)
{   // a fresh private rack (every unit off, the limiter on), then the chain's units with their factory presets, mixes and overrides
    boxlayers::LayerHost h;
    auto setp = [&h] (const juce::String& id, float v) { if (auto* q = h.apvts.getParameter (id)) q->setValueNotifyingHost (q->convertTo0to1 (v)); };
    for (auto& st : boxfocus::chain (intent, k).steps)
    {
        int idx = -1; const auto& list = gpfx::unitPresets (st.unit);
        for (int i = 0; i < (int) list.size(); ++i) if (st.preset != nullptr && list[(size_t) i].name == st.preset) idx = i;
        h.rack.impl->applyUnitPreset (st.unit, idx);
        setp (gpfx::pid (st.unit, "on"), 1.0f);
        if (st.mix >= 0.0f && gpfx::unitInfo (st.unit).hasMix) setp (gpfx::pid (st.unit, "mix"), st.mix);
        for (auto& [key, v] : st.set) setp (gpfx::pid (st.unit, key), v);
    }
    auto t = h.apvts.copyState();
    juce::ValueTree fxs ("FXRACK"); fxs.appendChild (h.rack.saveState(), nullptr); t.appendChild (fxs, nullptr);
    if (auto box = fxs.getChild (0).getChildWithName ("BOX3"); box.isValid()) box.removeChild (box.getChildWithName ("SPIRITBOARD"), nullptr);
    for (int i = t.getNumChildren(); --i >= 0;) { auto c = t.getChild (i); if (c.hasType ("PARAM") && notInScene (c.getProperty ("id").toString())) t.removeChild (i, nullptr); }
    if (auto c = t.getChildWithProperty ("id", "box_mix"); c.isValid())   // the box's own DRY / WET stays as the user set it
        if (const auto o = original.getChildWithProperty ("id", "box_mix"); o.isValid()) c.setProperty ("value", o.getProperty ("value"), nullptr);
    return t;
}
void BoxProcessor::focusPick (int intent)
{
    if (focusBusy.exchange (true)) return;
    intent = juce::jlimit (0, boxfocus::kIntents - 1, intent);
    if (! focusOriginal.isValid()) focusOriginal = captureSound();   // BACK TO ORIGINAL goes here
    focusIntent = intent; focusAuditioning = -1; focusVariants = {}; focusTrim = {}; focusVersion.fetch_add (1);
    struct Job { juce::ValueTree original; std::array<juce::ValueTree, boxfocus::kChains> raw, out; std::array<float, boxfocus::kChains> trim {}; double rate = 48000; };
    auto job = std::make_shared<Job>();
    job->original = focusOriginal.createCopy(); job->rate = sr;
    for (int k = 0; k < boxfocus::kChains; ++k) job->raw[(size_t) k] = focusChainSound (intent, k, focusOriginal);
    auto alive = dnaAlive;
    dnaPool.addJob ([this, job, alive, intent]
    {
        const float lo = loudnessOf (job->original, job->rate);
        for (int k = 0; k < boxfocus::kChains; ++k) { float t = 0; job->out[(size_t) k] = gainMatched (job->original, lo, job->raw[(size_t) k], job->rate, &t); job->trim[(size_t) k] = t; }
        juce::MessageManager::callAsync ([this, job, alive, intent]
        {
            if (! *alive) return;
            focusVariants = job->out; focusTrim = job->trim; focusBusy.store (false);
            focusMessage = boxfocus::intentNames()[intent] + ": 4 starting chains, click one to hear it";
            focusVersion.fetch_add (1);
        });
    });
}
void BoxProcessor::focusAudition (int k)
{
    if (! juce::isPositiveAndBelow (k, boxfocus::kChains) || ! focusVariants[(size_t) k].isValid()) return;
    if (! focusOriginal.isValid()) focusOriginal = captureSound();
    restoreSound (focusVariants[(size_t) k]);   // the click-free live load (an UNDO step)
    focusAuditioning = k;
    if (focusAmount < 0.999f) focusApplyAmount();
    focusMessage = "Hearing " + juce::String (boxfocus::chain (focusIntent, k).name) + ": KEEP it, try another, or BACK TO ORIGINAL";
    focusVersion.fetch_add (1);
}
void BoxProcessor::focusApplyAmount()
{   // the chain's units' wet MIX = as designed x AMOUNT (the rack's mix smoothers glide it)
    if (! juce::isPositiveAndBelow (focusAuditioning, boxfocus::kChains) || focusIntent < 0) return;
    const auto& v = focusVariants[(size_t) focusAuditioning];
    ++rack.impl->internalEdit;
    for (auto& st : boxfocus::chain (focusIntent, focusAuditioning).steps)
    {
        if (! gpfx::unitInfo (st.unit).hasMix) continue;
        const auto id = gpfx::pid (st.unit, "mix");
        const auto c = v.getChildWithProperty ("id", id);
        if (auto* q = apvts.getParameter (id); q != nullptr && c.isValid()) q->setValueNotifyingHost (q->convertTo0to1 ((float) c.getProperty ("value") * focusAmount));
    }
    --rack.impl->internalEdit;
}
void BoxProcessor::focusSetAmount (float a) { focusAmount = juce::jlimit (0.0f, 1.0f, a); focusApplyAmount(); }
void BoxProcessor::focusKeep()
{
    if (focusAuditioning >= 0 && focusIntent >= 0) focusMessage = "Kept " + juce::String (boxfocus::chain (focusIntent, focusAuditioning).name) + ": the rack is yours to edit";
    focusOriginal = {}; focusAuditioning = -1; focusAmount = 1.0f; focusVersion.fetch_add (1);
}
void BoxProcessor::focusBack()
{
    if (focusOriginal.isValid()) restoreSound (focusOriginal);
    focusOriginal = {}; focusAuditioning = -1; focusAmount = 1.0f; focusMessage = "Back to the original"; focusVersion.fetch_add (1);
}
bool BoxProcessor::focusWait (int timeoutMs)
{   // tests: until the worker's results arrived (no modal loops in the plugin builds)
    const auto t0 = juce::Time::getMillisecondCounter();
    while (focusBusy.load())
    {
#if JUCE_MODAL_LOOPS_PERMITTED
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
#else
        juce::Thread::sleep (5);
#endif
        if (juce::Time::getMillisecondCounter() - t0 > (juce::uint32) timeoutMs) return false;
    }
    return true;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new BoxProcessor(); }

// ================================================================ GHOSTPRINT (message thread; reading + analysis on the worker thread)
void BoxProcessor::printStart (std::function<void (boxprint::Analysis&, juce::String&)> work)
{   // one job at a time: a newer request cancels the running analysis and queues behind it
    printCancel = true; printPool.removeAllJobs (false, 0); printBusy = true;
    auto alive = printAlive; const int ticket = ++printTicket;
    printPool.addJob ([this, work, alive, ticket]
    {
        printCancel = false;
        auto res = std::make_shared<boxprint::Analysis>(); auto note = std::make_shared<juce::String>();
        work (*res, *note);
        juce::MessageManager::callAsync ([this, res, note, alive, ticket]
        {
            if (! *alive || ticket != printTicket) return;
            printResult = *res;
            printMessage = res->ok ? res->message + (note->isNotEmpty() ? "  (" + *note + ")" : juce::String()) : (note->isNotEmpty() ? *note : res->message);
            printBusy = false; ++printVersion;
        });
    });
}
void BoxProcessor::printLoadBuffer (std::shared_ptr<const juce::AudioBuffer<float>> audio, double rate, const juce::String& name, boxprint::Options opt)
{
    printRef = audio; printRefRate = rate; printName = name; printOpt = opt; printMessage = "Reading the gesture of " + name + "...";
    printReanalyse();
}
void BoxProcessor::printReanalyse()
{
    if (printRef == nullptr) return;
    auto ref = printRef; const double rate = printRefRate; const auto opt = printOpt;
    printStart ([this, ref, rate, opt] (boxprint::Analysis& a, juce::String&) { a = boxprint::analyse (*ref, rate, opt, &printCancel); });
    ++printVersion;
}
void BoxProcessor::printLoadFile (const juce::File& f)
{
    printName = f.getFileName(); printMessage = "Reading " + printName + "..."; printOpt = {};   // (a new reference: tempo detected again)
    const auto opt = printOpt;
    printStart ([this, f, opt] (boxprint::Analysis& a, juce::String& note)
    {
        auto buf = std::make_shared<juce::AudioBuffer<float>>(); double rate = 0; juce::String why;
        if (! boxprint::readFile (f, *buf, rate, why)) { a.ok = false; note = why; return; }
        note = why;   // ("only the first 60 s are used")
        a = boxprint::analyse (*buf, rate, opt, &printCancel);
        juce::MessageManager::callAsync ([this, buf, rate, alive = printAlive] { if (*alive) { printRef = buf; printRefRate = rate; } });
    });
    ++printVersion;
}
void BoxProcessor::printLoadClip (int clipId)
{
    auto c = clipById (clipId);
    if (c == nullptr || c->audio == nullptr) { printMessage = "That capture is gone"; ++printVersion; return; }
    boxprint::Options o; o.bpm = c->bpm; o.bars = 0;   // (a capture knows its tempo: no detection)
    printLoadBuffer (c->audio, c->sr, c->name.isNotEmpty() ? c->name : "capture " + juce::String (c->id), o);
}
int BoxProcessor::printApply()
{
    if (! printResult.ok) return 0;
    const auto P = printPattern();
    if (P.lanes.empty()) { printMessage = "Nothing to apply: every curve is OFF (or does not move)"; ++printVersion; return 0; }
    auto& d = *rack.impl; auto& box = d.box;
    d.pushUndo();   // the whole APPLY is one UNDO step
    int written = 0;
    for (auto& l : P.lanes)
    {
        const int k = box.tlAddLane (l.id);
        if (k < 0) continue;
        auto& L = box.timeline.lanes[(size_t) k];
        L.on = true; L.depth = juce::jlimit (-1.0f, 1.0f, l.depth); L.events = l.events;
        if (l.steps) { L.mode = 1; L.stepsIdx = boxprint::stepsIdxFor ((int) l.vals.size()); L.steps = {}; for (size_t j = 0; j < l.vals.size() && j < L.steps.size(); ++j) L.steps[j] = l.vals[j]; L.shape = gpfx::flatShape(); }
        else { L.mode = 0; L.shape = boxprint::curveShape (l); }
        box.tlShapes[(size_t) k].set (L.shape);
        if (l.unitOn.isNotEmpty())
            if (auto* q = apvts.getParameter (l.unitOn); q != nullptr && q->getValue() < 0.5f) q->setValueNotifyingHost (1.0f);   // (the destination's unit must run)
        ++written;
    }
    box.timeline.bars = juce::jlimit (1, 8, P.loopBars);
    box.tlChanged();
    printMessage = juce::String (written) + (written == 1 ? " lane" : " lanes") + " written to the GHOST MOTION timeline (" + juce::String (P.loopBars) + (P.loopBars == 1 ? " bar" : " bars")
                   + " loop). UNDO takes them back." + (written < (int) P.lanes.size() ? "  The timeline is full (12 lanes): some were not written." : "");
    ++printVersion;
    return written;
}
bool BoxProcessor::printWait (int timeoutMs)
{   // tests: until the worker's result arrived
    const auto t0 = juce::Time::getMillisecondCounter();
    while (printBusy.load())
    {
#if JUCE_MODAL_LOOPS_PERMITTED
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
#else
        juce::Thread::sleep (5);   // plugin builds have no modal loops; this hook is for tests only
#endif
        if (juce::Time::getMillisecondCounter() - t0 > (juce::uint32) timeoutMs) return false;
    }
    return true;
}
