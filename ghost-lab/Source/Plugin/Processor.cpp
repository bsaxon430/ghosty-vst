#include "Processor.h"
#include "HoodDefs.h"
#include "Editor.h"
#include <algorithm>

namespace glab
{
juce::AudioProcessor::BusesProperties Processor::buses()
{
    auto b = juce::AudioProcessor::BusesProperties();
   #if GHOSTLAB_FX
    b = b.withInput ("Input", juce::AudioChannelSet::stereo(), true);
   #endif
    return b.withOutput ("Output", juce::AudioChannelSet::stereo(), true);
}

Processor::Processor() : juce::AudioProcessor (buses())
{
    // SPECTRE's controls, as parameters FL can automate (the same travel as the page's controls)
    for (int i = 0; i < spectre::kParams; ++i)
    {
        const auto& in = spectre::param (i);
        juce::NormalisableRange<float> range (in.lo, in.hi,
            [i] (float, float, float t) { return spectre::fromNorm (i, t); },
            [i] (float, float, float v) { return spectre::toNorm (i, v); },
            [in] (float, float, float v) { return juce::jlimit (in.lo, in.hi, v); });
        auto* prm = new juce::AudioParameterFloat (juce::ParameterID { juce::String ("spectre_") + in.key, 1 }, juce::String ("SPECTRE ") + in.name, range, in.def,
            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([i] (float v, int) { return spectre::display (i, v); }));
        addParameter (prm);
        spectreP[(size_t) i] = prm;
        spectreSaved[(size_t) i] = in.def;
    }
    engine.onChange = [this] { syncPlayer(); refreshState(); };
    terrain.onChange = [this]
    {
        terrainShare.publish (terrain.terrain());
        exactPtr.store (nullptr);   // the land moved: the live path plays until its exact render lands
        applyLand();
        if (terrain.inGesture() && terrainPageOpen && ! terrainListening() && terrainBase() != nullptr) { setTerrainListening (true); autoListen = true; }
    };
    terrain.onCommit = [this] { refreshState(); if (onTerrainCommit) onTerrainCommit(); };
    terrainShare.publish (terrain.terrain());
    applyLand();
    refreshState();
    startTimer (100);
}

Processor::~Processor()
{
    stopTimer();
    *alive = false;
    engine.onChange = nullptr;
    terrain.onChange = nullptr;
    terrain.onCommit = nullptr;
}

bool Processor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
   #if GHOSTLAB_FX
    if (l.getMainInputChannelSet() != out) return false;
   #endif
    return true;
}

void Processor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    setLatencySamples (0);   // the lab's processing is offline; only SPECTRE's LIVE (FX) adds latency (set below)
    player.prepare (sampleRate);
    terrainBank.prepare (sampleRate, 2);
    terrainLive.prepare (sampleRate, 2); terrainLive.setSeed (7);
    touchLive.prepare (sampleRate, 2); touchLive.setSeed (11);
    touchWasLive = false;
    previewBlock = juce::jmax (16, samplesPerBlock);
    previewBuf.setSize (2, juce::jmax (8192, samplesPerBlock * 2));   // allocated here, never on the audio thread
    spaceBlock = 256;
    liveSpace.prepare (sampleRate, spaceBlock);
    { float v[rooms::kSpaceControls] {}; Step st; st.op = Op::Space; rooms::spacePins (st, v); (void) rooms::livePinsFor (v); if (spaceV[8].load() == 0.0f && spaceV[5].load() == 0.0f) setSpaceLive (v); }   // (the tables are built here, never on the audio thread; a fresh instance starts at the originals' defaults)
    exactBuf.setSize (2, juce::jmax (8192, samplesPerBlock * 2));
    spectreTrack.prepare (sampleRate, juce::jmax (64, samplesPerBlock));
    spectreLoop.prepare (sampleRate, juce::jmax (64, samplesPerBlock));
    monoBuf.setSize (1, juce::jmax (8192, samplesPerBlock * 2));
    spectreWasLive = false;
    setLatencySamples (spectreLiveOn() ? spectreTrack.latency() : 0);
    const juce::ScopedLock l (captureLock);
    if (isFx && (captureBuf.getNumSamples() == 0 || std::abs (captureRate - sampleRate) > 0.5))
    {
        captureOn = false;
        captureRate = sampleRate;
        captureBuf.setSize (2, (int) (kCaptureSeconds * sampleRate));   // allocated here, never on the audio thread
        captureLen = 0;
    }
}

void Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
   #if GHOSTLAB_FX
    if (captureOn.load())
    {
        const int len = captureLen.load(), room = captureBuf.getNumSamples() - len, take = juce::jmin (room, n);
        for (int c = 0; c < 2; ++c)
            if (take > 0) captureBuf.copyFrom (c, len, buffer, juce::jmin (c, getTotalNumInputChannels() - 1), 0, take);
        captureLen.store (len + juce::jmax (0, take));
        if (take < n) captureOn = false;   // full
    }
    // the input passes through untouched (or through SPECTRE when its LIVE is on); the preview plays on top
    if (spectreOn.load() && n > 0)
    {
        const auto p = spectreParams();
        if (buffer.getNumChannels() >= 2) spectreTrack.process (buffer.getWritePointer (0), buffer.getWritePointer (1), n, p);
        else if (n <= monoBuf.getNumSamples())
        {
            monoBuf.copyFrom (0, 0, buffer, 0, 0, n);
            spectreTrack.process (buffer.getWritePointer (0), monoBuf.getWritePointer (0), n, p);
            buffer.addFrom (0, 0, monoBuf, 0, 0, n); buffer.applyGain (0, 0, n, 0.5f);
        }
        spectreWasLive = true;
    }
    else if (spectreWasLive) { spectreTrack.reset(); spectreWasLive = false; }
   #else
    buffer.clear();
   #endif
    const int nch = juce::jmin (2, buffer.getNumChannels());
    if (player.playingSlot() == Player::Preview && n <= previewBuf.getNumSamples() && nch > 0)
    {
        // TERRAIN listen: the sound underneath the landscape, through the live bank, added on top
        previewBuf.clear();
        juce::AudioBuffer<float> pv (previewBuf.getArrayOfWritePointers(), nch, n);
        const float frac0 = player.playhead();
        renderPlayer (pv, midi, n);
        ghost::terrain::Terrain t;
        if (terrainShare.pull (t)) terrainBank.setTerrain (t);
        terrainBank.setDepth (frac0);
        terrainBank.setAmount (1.0f);
        terrainBank.process (pv.getArrayOfWritePointers(), nch, n);
        // build 4: the live chain the land drives (SHATTER, SMEAR / FREEZE, GRAIN, SHIMMER, WOBBLE ...)
        ghost::terrain::LiveParams lp;
        if (liveShare.pull (lp)) terrainLive.setParams (lp);
        terrainLive.process (pv.getArrayOfWritePointers(), nch, n);
        // ... and the exact render, read at the same place, cross-faded in once it has landed (and out when the land moves)
        const Audio* ex = exactPtr.load (std::memory_order_acquire);
        exactInUse.store (ex, std::memory_order_release);
        const float want = ex != nullptr ? 1.0f : 0.0f;
        if (ex != nullptr || exactMix > 0.0f)
        {
            const Audio* src = ex;
            if (src != nullptr && src->length() > 1)
            {
                const double len = (double) src->length(), step = len / (double) juce::jmax (1, exactBaseLen.load());
                double at = (double) frac0 * len;
                for (int i = 0; i < n; ++i, at += step)
                {
                    if (at >= len - 1) at -= len - 1;
                    const int i0 = (int) at; const float fr = (float) (at - i0);
                    for (int c = 0; c < nch; ++c)
                    {
                        const int sc = juce::jmin (c, src->channels() - 1);
                        const float* d = src->buf.getReadPointer (sc);
                        exactBuf.setSample (c, i, (d[i0] + (d[i0 + 1] - d[i0]) * fr) * previewGain.load());
                    }
                }
            }
            else exactBuf.clear (0, n);
            const float stepMix = (float) n / (float) (0.15 * juce::jmax (8000.0, getSampleRate()));
            for (int i = 0; i < n; ++i)
            {
                exactMix = want > exactMix ? juce::jmin (want, exactMix + stepMix / (float) n) : juce::jmax (want, exactMix - stepMix / (float) n);
                for (int c = 0; c < nch; ++c) pv.setSample (c, i, pv.getSample (c, i) * (1.0f - exactMix) + exactBuf.getSample (c, i) * exactMix);
            }
            exactShown.store (exactMix);
        }
        terrainTap.push (pv.getArrayOfReadPointers(), nch, n);
        for (int c = 0; c < nch; ++c) buffer.addFrom (c, 0, pv, c, 0, n);
    }
    else if (player.playingSlot() == Player::Room && n <= previewBuf.getNumSamples() && nch == 2)
    {
        // the SPACE room's LISTEN: the sound, round and round, through the space engine as the pad and controls are now
        previewBuf.clear (0, n);
        juce::AudioBuffer<float> pv (previewBuf.getArrayOfWritePointers(), 2, n);
        renderPlayer (pv, midi, n);
        if (roomEngine.load() == 1)
            spectreLoop.process (pv.getWritePointer (0), pv.getWritePointer (1), n, spectreParams());   // SPECTRE's LISTEN
        else
        {
            matter::SpaceParams sp; sp.tail = 0.0f; sp.width = 0.0f;   // (every control of both modules comes from the page)
            float v[rooms::kSpaceControls];
            for (int i = 0; i < rooms::kSpaceControls; ++i) v[i] = spaceV[i].load();
            const auto pins = rooms::livePinsFor (v);
            for (int i = 0; i < n; i += spaceBlock)
                liveSpace.process (pv.getWritePointer (0) + i, pv.getWritePointer (1) + i, juce::jmin (spaceBlock, n - i), sp, &pins);
        }
        for (int c = 0; c < 2; ++c)
        {
            buffer.addFrom (c, 0, pv, c, 0, n);
            const float lv = pv.getRMSLevel (c, 0, n);
            spaceLevel[c].store (juce::jmax (lv, spaceLevel[c].load() * 0.9f));
        }
        spaceWasLive = true;
        exactInUse.store (nullptr, std::memory_order_release);
    }
    else if ((player.playingSlot() == Player::Touch || ! touchLive.isIdle()) && n <= previewBuf.getNumSamples() && nch > 0)
    {
        // a hand on THE LIVING SOUND: the processed sound, round and round, through the body's live chain. The chain
        // stays in the path after the hand lets go until it has glided back to nothing (so what plays next - the real
        // result, or whatever played before - fades in under it without a jump).
        previewBuf.clear (0, n);
        juce::AudioBuffer<float> pv (previewBuf.getArrayOfWritePointers(), nch, n);
        renderPlayer (pv, midi, n);
        ghost::terrain::LiveParams lp;
        if (touchShare.pull (lp)) touchLive.setParams (lp);
        touchLive.process (pv.getArrayOfWritePointers(), nch, n);
        for (int c = 0; c < nch; ++c) buffer.addFrom (c, 0, pv, c, 0, n);
        touchWasLive = true;
        exactInUse.store (nullptr, std::memory_order_release);
    }
    else
    {
        renderPlayer (buffer, midi, n);
        ghost::terrain::Terrain t;
        if (terrainShare.pull (t)) terrainBank.setTerrain (t);
        terrainBank.reset();   // the next listen starts clean
        ghost::terrain::LiveParams lp;
        if (liveShare.pull (lp)) terrainLive.setParams (lp);
        if (touchShare.pull (lp)) touchLive.setParams (lp);
        if (touchWasLive) { touchLive.reset(); touchWasLive = false; }   // the next touch starts clean (once, not every block)
        exactInUse.store (nullptr, std::memory_order_release);
    }
    midi.clear();
}

void Processor::renderPlayer (juce::AudioBuffer<float>& dst, juce::MidiBuffer& midi, int n)
{
    int pos = 0;
    for (const auto m : midi)
    {
        const auto msg = m.getMessage();
        const int at = juce::jlimit (0, n, m.samplePosition);
        if (at > pos) { player.render (dst, pos, at - pos, previewGain.load()); pos = at; }
        if (msg.isNoteOn()) player.noteOn (msg.getNoteNumber(), msg.getFloatVelocity());
        else if (msg.isNoteOff()) player.noteOff (msg.getNoteNumber());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff()) player.noteOff (-1), player.stop();
    }
    if (pos < n) player.render (dst, pos, n - pos, previewGain.load());
}

AudioPtr Processor::terrainBase() const
{
    // the landscape applies to the sound under the last TERRAIN step (so it never stacks on itself)
    if (! engine.hasSpecimen()) return nullptr;
    const bool topIsTerrain = engine.historyCursor() > 0 && engine.current().label.startsWith ("TERRAIN");
    return topIsTerrain ? engine.audioUnderLast() : engine.processedAudio();
}

void Processor::setSpaceListening (bool on, AudioPtr base)
{
    if (! on) { if (spaceListening()) player.stop(); return; }
    if (base == nullptr) return;
    roomEngine.store (0);
    player.set (Player::Room, base);
    player.setLoop (Player::Room, true);
    player.play (Player::Room, 0.0);
}

// ---- SPECTRE
spectre::Params Processor::spectreParams() const
{
    spectre::Params p;
    for (int i = 0; i < spectre::kParams; ++i) p.v[(size_t) i] = spectreP[(size_t) i]->get();
    return p;
}

void Processor::setSpectreParam (int p, float v)
{
    if (p < 0 || p >= spectre::kParams) return;
    auto* prm = spectreP[(size_t) p];
    prm->setValueNotifyingHost (prm->convertTo0to1 (juce::jlimit (spectre::param (p).lo, spectre::param (p).hi, v)));
}

void Processor::beginSpectreGesture (int p) { if (p >= 0 && p < spectre::kParams) spectreP[(size_t) p]->beginChangeGesture(); }
void Processor::endSpectreGesture (int p) { if (p >= 0 && p < spectre::kParams) spectreP[(size_t) p]->endChangeGesture(); }

void Processor::setSpectreTable (const juce::String& name, const std::vector<float>& cycle)
{
    tableName = cycle.empty() ? juce::String ("SINE") : name;
    tableCycle = cycle;
    spectreTrack.setTable (tableCycle);
    spectreLoop.setTable (tableCycle);
    spectreDirty = true;
}

void Processor::setSpectreLive (bool on)
{
    if (! isFx) return;
    spectreOn = on;
    setLatencySamples (on ? spectreTrack.latency() : 0);   // the host lines the track up again
    spectreDirty = true;
}

void Processor::setSpectreListening (bool on, AudioPtr base)
{
    if (! on) { if (spectreListening()) player.stop(); return; }
    if (base == nullptr) return;
    roomEngine.store (1);
    player.set (Player::Room, base);
    player.setLoop (Player::Room, true);
    player.play (Player::Room, 0.0);
}

void Processor::setTerrainListening (bool on)
{
    if (! on) { if (terrainListening()) player.stop(); return; }
    auto base = terrainBase();
    if (base == nullptr) return;
    player.set (Player::Preview, base);
    player.setLoop (Player::Preview, true);
    player.play (Player::Preview, 0.0);
}

// ---- THE LIVING SOUND, live
// What the live chain does while the body is held: quick sketches of the real processes, so the hand hears roughly what
// letting go will make.
//   PULL (the real one stretches time)        the sound smears out and holds, pieces of it repeat
//   TWIST (bends the spectrum)                clockwise: brighter, an octave shimmer; anticlockwise: darker, grittier
//   PINCH (squeezes / widens the spectrum)    squeezed: darker and denser; widened: brighter and wide. Before a
//                                             gesture is recognised, the grabbed part bent down / up counts as a little of this
//   a shake (MUTATE)                          it breaks into pieces, some backwards, and wobbles
ghost::terrain::LiveParams Processor::bodyLive (const BodyTouch& b)
{
    ghost::terrain::LiveParams p;
    const float pull = juce::jlimit (0.0f, 1.0f, b.pull), tw = juce::jlimit (-1.0f, 1.0f, b.twist), sh = juce::jlimit (0.0f, 1.0f, b.shake);
    const float squeeze = juce::jlimit (-1.0f, 1.0f, std::abs (b.pinch) > 0.01f ? b.pinch : 0.6f * b.bend);
    p.smear = juce::jlimit (0.0f, 1.0f, 0.85f * pull + 0.35f * juce::jmax (0.0f, squeeze));
    p.freeze = 0.7f * pull;
    p.shatter = juce::jmax (0.3f * pull, 0.9f * sh);
    p.shatterSize = pull + sh > 0.001f ? (0.9f * pull + 0.2f * sh) / (pull + sh) : 0.3f;
    p.reverse = 0.6f * sh;
    p.shimmer = 0.75f * juce::jmax (0.0f, tw) + 0.2f * std::abs (tw) + 0.25f * juce::jmax (0.0f, -squeeze);
    p.drive = juce::jlimit (0.0f, 1.0f, 0.5f * juce::jmax (0.0f, -tw) + 0.1f * std::abs (tw));
    p.gainDb = -14.0f * p.drive;   // (grit makes it louder: trimmed back, so the hand changes the colour, not the level)
    p.tilt = juce::jlimit (-1.0f, 1.0f, 0.8f * tw - 0.75f * squeeze);
    p.width = juce::jmax (0.0f, -squeeze);
    p.wobble = 0.8f * sh;
    p.wobbleHz = 5.0f + 7.0f * sh;
    return p;
}

void Processor::beginBodyTouch (float fromFraction)
{
    if (touchOn || ! engine.hasSpecimen()) return;
    touchOn = true;
    const int now = player.playingSlot();
    touchResume = (now == Player::Original || now == Player::Processed) ? now : -1;
    touchParams = {};
    touchShare.publish (touchParams);
    player.set (Player::Touch, engine.processedAudio());
    player.setLoop (Player::Touch, true);
    // nothing was playing: it starts at the part of the sound the hand grabbed; something was: it carries on from there
    player.play (Player::Touch, touchResume >= 0 ? -1.0 : (double) juce::jlimit (0.0f, 0.98f, fromFraction));
}

void Processor::setBodyTouch (const BodyTouch& b)
{
    if (! touchOn) return;
    touchParams = bodyLive (b);
    touchShare.publish (touchParams);
}

void Processor::endBodyTouch()
{
    if (! touchOn) return;
    touchOn = false;
    touchParams = {};
    touchShare.publish (touchParams);
    if (touchResume >= 0) player.play (touchResume, -1.0); else player.stop();
}

void Processor::forgetBodyTouch()
{
    if (! touchOn) return;
    touchOn = false;
    touchParams = {};
    touchShare.publish (touchParams);
}

void Processor::bodyResultLanded()
{
    const bool ours = touchOn;
    touchOn = false;
    touchParams = {};
    touchShare.publish (touchParams);
    // the new sound, from its start - unless the user has since chosen to listen to the original
    const int now = player.playingSlot();
    if (ours || now < 0 || now == Player::Processed || now == Player::Touch)
    {
        player.setMidiSlot (Player::Processed);
        player.play (Player::Processed, 0.0);
    }
}

void Processor::applyLand()
{
    landNow = mapLabLand (terrain.terrain(), terrain.motion(), engine.hasPartner());
    liveShare.publish (landNow.live);
}

void Processor::setExactPreview (AudioPtr rendered)
{
    if (rendered == nullptr) { exactPtr.store (nullptr); return; }
    if (auto base = terrainBase()) exactBaseLen.store (juce::jmax (1, base->length()));
    exactOwners.push_back (rendered);
    exactPtr.store (rendered.get(), std::memory_order_release);
}

void Processor::timerCallback()
{
    player.collect();
    // the renders the audio thread has let go of
    const Audio* now = exactPtr.load(), *used = exactInUse.load();
    while (exactOwners.size() > 4)
    {
        auto it = std::find_if (exactOwners.begin(), exactOwners.end(), [&] (const AudioPtr& a) { return a.get() != now && a.get() != used; });
        if (it == exactOwners.end()) break;
        exactOwners.erase (it);   // (the oldest one nobody plays; four are kept, so a block that just picked one up is never left holding a freed one)
    }
    // SPECTRE: a control moved (by hand or by FL's automation): the saved state follows, a few times a second at most
    bool moved = spectreDirty.exchange (false);
    for (int i = 0; i < spectre::kParams; ++i) moved = moved || spectreP[(size_t) i]->get() != spectreSaved[(size_t) i];
    if (moved) refreshState();
    // a drag's speed fades (the WOBBLE settles when your hand is still)
    if (terrain.motion() > 0.001f || landNow.live.wobble > 0.001f) applyLand();
}

void Processor::syncPlayer()
{
    player.set (Player::Original, engine.originalAudio());
    player.set (Player::Processed, engine.processedAudio());
    if (terrainListening()) player.set (Player::Preview, terrainBase());
    // (the Touch slot keeps the sound the hand grabbed until the touch is over, so the real result never cuts in under the live chain)
    if (! touchOn && player.playingSlot() != Player::Touch) player.set (Player::Touch, nullptr);
}

void Processor::refreshState()
{
    auto t = engine.saveState();
    t.setProperty ("terrain", juce::JSON::toString (terrain.stateToVar(), true), nullptr);
    {
        auto* o = new juce::DynamicObject();
        for (int i = 0; i < spectre::kParams; ++i) { spectreSaved[(size_t) i] = spectreP[(size_t) i]->get(); o->setProperty (spectre::param (i).key, spectreSaved[(size_t) i]); }
        o->setProperty ("live", spectreOn.load());
        o->setProperty ("table", tableName);
        if (! tableCycle.empty()) o->setProperty ("tableData", spectre::tableToText (tableCycle));
        t.setProperty ("spectre", juce::JSON::toString (juce::var (o), true), nullptr);
    }
    juce::MemoryBlock mb;
    if (auto xml = t.createXml()) copyXmlToBinary (*xml, mb);
    const juce::ScopedLock l (stateLock);
    cachedState = mb;
}

void Processor::getStateInformation (juce::MemoryBlock& dest)
{
    const juce::ScopedLock l (stateLock);
    dest = cachedState;
}

void Processor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr) return;
    auto t = juce::ValueTree::fromXml (*xml);
    // restoring reads WAV files: do it on the message thread
    juce::MessageManager::callAsync ([this, t, a = alive]
    {
        if (*a)
        {
            engine.restoreState (t);
            juce::var tv = juce::JSON::parse (t["terrain"].toString());
            if (tv.isObject()) terrain.stateFromVar (tv);
            // SPECTRE's controls; a session from before build 17 has none: its track plays as it always did (LIVE off)
            juce::var sv = juce::JSON::parse (t["spectre"].toString());
            const spectre::Params d;
            for (int i = 0; i < spectre::kParams; ++i)
                setSpectreParam (i, sv.isObject() && sv.hasProperty (spectre::param (i).key) ? (float) sv[spectre::param (i).key] : d[i]);
            setSpectreTable (sv.isObject() ? sv["table"].toString() : juce::String(), sv.isObject() ? spectre::tableFromText (sv["tableData"].toString()) : std::vector<float>());
            if (isFx) setSpectreLive (sv.isObject() && (bool) sv["live"]);
            syncPlayer(); refreshState();
        }
    });
}

void Processor::startCapture()
{
    const juce::ScopedLock l (captureLock);
    if (captureBuf.getNumSamples() == 0) return;
    captureLen = 0;
    captureOn = true;
}

AudioPtr Processor::stopCapture()
{
    captureOn = false;
    const juce::ScopedLock l (captureLock);
    const int len = captureLen.load();
    if (len < (int) (0.1 * captureRate)) return nullptr;
    auto a = std::make_shared<Audio>();
    a->sampleRate = captureRate;
    a->buf.setSize (2, len);
    for (int c = 0; c < 2; ++c) a->buf.copyFrom (c, 0, captureBuf, c, 0, len);
    if (isEffectivelyMono (*a)) *a = mono (*a);
    return a;
}

juce::AudioProcessorEditor* Processor::createEditor() { return new Editor (*this); }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new glab::Processor(); }
