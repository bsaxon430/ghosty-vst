#include <cstring>
#include "HostNotify.h"
#include "SpecProcessor.h"
#include "SpecEditor.h"

static juce::AudioProcessorValueTreeState::ParameterLayout specLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout L;
    L.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { "bpm", 1 }, "Tempo (no host)", juce::NormalisableRange<float>(60, 200, 1), 120));
    L.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { "volume", 1 }, "Volume", juce::NormalisableRange<float>(0, 1), 0.8f));
    // the PAD painter's knobs (new in 2.1; old sessions just get these defaults)
    const auto pct = juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) { return juce::String(juce::roundToInt(v * 100.0f)) + "%"; })
                                                       .withValueFromStringFunction([](const juce::String& t) { return juce::jlimit(0.0f, 1.0f, t.retainCharacters("0123456789.-").getFloatValue() / 100.0f); });
    auto f = [&L, pct](const char* id, const char* name, float def) { L.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float>(0, 1), def, pct)); };
    f("padBloom", "Pad Bloom", 0.45f); f("padSpace", "Pad Space", 0.65f); f("padShimmer", "Pad Shimmer", 0.4f); f("padTape", "Pad Tape Delay", 0.25f);
    f("padDrive", "Pad Drive", 0.12f); f("padPedal", "Pad Open Strings", 0.35f); f("padDrift", "Pad Drift", 0.3f); f("padLevel", "Pad Level", 0.75f); f("padStrum", "Pad Strum", 0.3f);
    L.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "padHold", 1 }, "Pad Hold", true));
    L.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "padFreeze", 1 }, "Pad Freeze", false));
    // SPECTRAL LAB (new in 2.2): the DNA morph amount, automatable
    L.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { "labMorph", 1 }, "Lab Morph A-B", juce::NormalisableRange<float>(0, 1), 0.0f, pct));
    L.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { "labLevel", 1 }, "Lab Level", juce::NormalisableRange<float>(0, 1), 0.8f, pct));
    L.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { "labWidth", 1 }, "Lab Width", juce::NormalisableRange<float>(0, 1), 0.35f, pct));
    L.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { "labOutput", 1 }, "Lab Hear", juce::StringArray { "Canvas", "Ghost", "Split", "Morph" }, 0));
    L.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID { "labGhostHit", 1 }, "Lab Ghost Hit (0 = next each hit)", 0, lab::kMaxGhosts, 0));
    for (int b = 1; b <= lab::kMaxBands; ++b) L.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "labMute" + juce::String(b), 1 }, "Lab Split Band " + juce::String(b) + " Mute", false));
    // BYPASS (shared GhostBlock behaviour, Shared/Bypass.h): always the LAST parameter, so existing automation keeps its index
    L.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "bypass", 1 }, "Bypass", false));
    // PLAY: ALWAYS | ON NOTES (ROADMAP 16, FL channel-mute follow), after bypass (new parameters go last)
    L.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { "play_mode", 1 }, "Play", juce::StringArray { "ALWAYS", "ON NOTES" }, 0));
    return L;
}
// a sidechain input for DETECT KEY (off by default in the standalone app so it doesn't open the microphone)
juce::AudioProcessor::BusesProperties SpecProcessor::makeBuses() {
    const bool standalone = juce::PluginHostType::getPluginLoadedAs() == juce::AudioProcessor::wrapperType_Standalone;
    return BusesProperties().withInput("Sidechain", juce::AudioChannelSet::stereo(), !standalone).withOutput("Output", juce::AudioChannelSet::stereo(), true);
}

SpecProcessor::SpecProcessor() : AudioProcessor(makeBuses()), apvts(*this, nullptr, "GHOSTYSPECTRUM2", specLayout()) {
    { const char* ids[] = { "padBloom", "padSpace", "padShimmer", "padTape", "padDrive", "padPedal", "padDrift", "padLevel", "padStrum", "padHold", "padFreeze" };
      for (int i = 0; i < 11; ++i) padP[i] = apvts.getRawParameterValue(ids[i]); }
    labMorphP = apvts.getRawParameterValue("labMorph"); labLevelP = apvts.getRawParameterValue("labLevel"); labWidthP = apvts.getRawParameterValue("labWidth");
    bypassParam = dynamic_cast<juce::AudioParameterBool*>(apvts.getParameter("bypass")); jassert(bypassParam != nullptr);
    playModeP = apvts.getRawParameterValue("play_mode");
    labOutputP = apvts.getRawParameterValue("labOutput"); labGhostP = apvts.getRawParameterValue("labGhostHit");
    for (int b = 0; b < lab::kMaxBands; ++b) labMuteP[b] = apvts.getRawParameterValue("labMute" + juce::String(b + 1));
    drums = std::make_shared<gb::DrumLibrary>(); drums->build(48000.0);
    defaultArrange = arrange.getState(); defaultArcs = arcsPage.getState().createCopy();
#if GHOSTY_VOICE
    defaultVoice = voice.toTree().createCopy();
    // a VOICE clip re-renders from the VOICE state it was sent with (sources + settings), in a private host
    voiceSource = arrange.registerSource("VOICE", [](const arr::SourceRef& r) -> arr::PayloadPtr {
        auto t = juce::ValueTree::readFromData(r.state.getData(), r.state.getSize()); if (!t.isValid()) return nullptr;
        voice::VoiceHost tmp; tmp.fromTree(t); tmp.waitIdle(120000); juce::String err; if (!tmp.renderNow(err)) return nullptr;
        auto res = tmp.result(); return res ? voicePayload(*res, r.name, r.state) : nullptr; });
#endif
    arcsSource = arrange.registerSource("ARCS", [](const arr::SourceRef& r) -> arr::PayloadPtr { return arcsPayload(r); });
    labSource = arrange.registerSource("LAB", [](const arr::SourceRef& r) -> arr::PayloadPtr { auto e = lab::LabHost::renderExportFromState(r.state); return e ? toPayload(*e) : nullptr; });
    publish();
}
#if GHOSTY_VOICE
arr::PayloadPtr SpecProcessor::voicePayload(const voice::Result& r, const juce::String& name, juce::MemoryBlock state) {
    const auto& a = r.audio; const int n = (int) a.L.size(); if (n <= 0 || a.fs <= 0) return nullptr;
    juce::AudioBuffer<float> b(a.stereo() ? 2 : 1, n); b.copyFrom(0, 0, a.L.data(), n); if (a.stereo()) b.copyFrom(1, 0, a.R.data(), juce::jmin(n, (int) a.R.size()));
    arr::SourceRef src; src.page = "VOICE"; src.name = name; src.state = std::move(state);
    return arr::makePayload(b, a.fs, name, {}, std::move(src), 0);
}
#endif
arr::PayloadPtr SpecProcessor::toPayload(const lab::LabHost::ExportSound& e) {
    const int n = (int) e.audio.m.size(); if (n <= 0) return nullptr;
    juce::AudioBuffer<float> b(2, n); float* L = b.getWritePointer(0); float* Rr = b.getWritePointer(1);
    for (int i = 0; i < n; ++i) { const float sd = (size_t) i < e.audio.s.size() ? e.audio.s[(size_t) i] * e.width : 0.0f; L[i] = (e.audio.m[(size_t) i] + sd) * e.gain; Rr[i] = (e.audio.m[(size_t) i] - sd) * e.gain; }
    const float pk = juce::jmax(b.getMagnitude(0, 0, n), b.getMagnitude(1, 0, n)); if (pk > 0.98f) b.applyGain(0.95f / pk);
    std::vector<arr::Note> notes; for (const auto& x : e.notes) { arr::Note a; a.pitch = x.pitch; a.velocity = x.velocity; a.start = x.start; a.length = x.length; notes.push_back(a); }
    arr::SourceRef src; src.page = "LAB"; src.name = e.name; src.state = e.state;
    return arr::makePayload(b, lab::SR, e.name, std::move(notes), std::move(src), e.bpm);
}
SpecProcessor::~SpecProcessor() {
#if GHOSTY_PROCESS_LAB
    labAudition.release();
#endif
    engine.setSnapshot(nullptr); }

void SpecProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals nd;
    // BYPASS: a ~6 ms fade out, then silence and nothing runs; switching it on stops every voice and player
    bypass.set(bypassParam->get());
    int arcsNoteOff = -1;
    if (bypass.takeEngageEdge()) { bypassStopPending = true; arcsNoteOff = arcsPage.engine.midiNote.load(); }   // ARCS lets go of a held key during the fade
    if (bypass.silent()) {
        if (std::exchange(bypassStopPending, false)) {   // the fade has finished: now stop everything (stopping earlier would cut under the fade = a click)
            engine.panic(); std::fill(std::begin(held), std::end(held), false); lab.stopAll(); arrange.stop(); arcsPage.engine.internalPlay.store(false);
        }
        // BYPASS wins over PLAY: ON NOTES: the note-offs it swallows can't leave the gate stuck open; after un-bypass the next note opens it
        heldNotes.reset(); heldCount = 0; notesGate.prepare(getSampleRate() > 0 ? getSampleRate() : 48000.0, 0.006, playModeP->load() > 0.5f); gateOpenUi.store(playModeP->load() <= 0.5f);
        buffer.clear(); midi.clear(); return;
    }
    if (bypass.isBypassed()) midi.clear();   // incoming MIDI is ignored while bypassed (also during the fade)
    double hbpm = 0, ppq = 0; bool playing = false;
    // SYNC: DAW | OWN. A switch fades the transport-driven players out on the old clock, swaps at silence, and fades them in on the new one
    const int n = buffer.getNumSamples();
    juce::AudioPlayHead* head = getPlayHead();
    { const auto hostPos = head != nullptr ? head->getPosition() : juce::Optional<juce::AudioPlayHead::PositionInfo>{};
      noHostUi.store(!hostPos.hasValue());
      const bool want = ownWanted();
      if (syncFresh) { syncFresh = false; appliedOwn = want; }   // the first block after prepare: no fade, just the mode (old sessions: DAW)
      else if (syncDir == -1 && syncG <= 0.0f) { if (appliedOwn && !want) { sync.stopOwn(); (void) sync.resolve({}, 0); } appliedOwn = want; syncDir = want != appliedOwn ? -1 : 1; }
      else if (want != appliedOwn && syncDir != -1) syncDir = -1;
      ownUi.store(appliedOwn);
      if (!appliedOwn) {   // DAW: exactly the classic reading of the host
          if (hostPos.hasValue()) { if (auto b = hostPos->getBpm()) if (*b > 1) { hbpm = *b; lastHostBpm.store(*b); } ppq = hostPos->getPpqPosition().orFallback(0.0); playing = hostPos->getIsPlaying(); }
          hostPlayingUi.store(playing);
          if (hbpm <= 0) hbpm = tempo();
      } else {             // OWN: Spectrum's own clock; everything reads it through ownHead (ARRANGE, ARCS) or the values below (PAINT LOOP, Lab)
          const auto t = sync.resolve({}, n);
          if (t.justStarted) ownSec = t.ppq * 60.0 / t.bpm;
          hbpm = t.bpm; ppq = t.ppq; playing = t.playing;
          auto& P = ownHead.pos; P = {}; P.setBpm(t.bpm); P.setPpqPosition(t.ppq); P.setIsPlaying(t.playing); P.setTimeSignature(juce::AudioPlayHead::TimeSignature { 4, 4 });
          P.setPpqPositionOfLastBarStart(std::floor(t.ppq / 4.0) * 4.0); P.setTimeInSeconds(ownSec); P.setTimeInSamples((juce::int64) std::llround(ownSec * syncSr));
          if (t.playing) ownSec += n / syncSr;
          head = &ownHead;
      } }
    // MIDI keys play the pad: hold a key to sound it (when HOLD is off), it transposes from the key's root, and each press re-rings the bells
    float strike = 0;
    // PLAY: ON NOTES. Any held key gates the transport-driven players (the Lab's host loop, ARRANGE, ARCS) and still does its own job
    // (Lab keys, pad, ARCS MIDI-in). The gate opens / closes at the note's own sample; a host stop forgets held keys (no stuck gate).
    const bool onNotes = playModeP->load() > 0.5f;
    nGate = 0;
    auto pushGate = [this](int at, bool closed) { if (nGate < kMaxGate) ++nGate; gateAt[nGate - 1] = at; gateClosed[nGate - 1] = closed; };   // full: the newest replaces the last
    if (onNotes) {
        if (lastGateHostPlay && !playing) { if (heldCount > 0) pushGate(0, true); heldNotes.reset(); heldCount = 0; }
        if (!notesGate.isBypassed() && heldCount == 0) pushGate(0, true);   // just switched ALWAYS -> ON NOTES with no key held
        for (const auto md : midi) { const auto m = md.getMessage(); const int at = juce::jlimit(0, juce::jmax(0, n - 1), md.samplePosition);
            if (m.isNoteOn()) { const int nn = m.getNoteNumber(); if (!heldNotes[(size_t) nn]) { heldNotes.set((size_t) nn); if (++heldCount == 1) pushGate(at, false); } }
            else if (m.isNoteOff()) { const int nn = m.getNoteNumber(); if (heldNotes[(size_t) nn]) { heldNotes.reset((size_t) nn); if (--heldCount == 0) pushGate(at, true); } }
            else if (m.isAllNotesOff() || m.isAllSoundOff()) { if (heldCount > 0) pushGate(at, true); heldNotes.reset(); heldCount = 0; } }
    } else { heldNotes.reset(); heldCount = 0; if (notesGate.isBypassed()) pushGate(0, false); }
    lastGateHostPlay = playing;
    // the gate as a gain curve for this block (computed once, used by everything that plays by itself from the transport)
    const bool gateActive = onNotes || nGate > 0 || notesGate.isBypassed() || notesGate.silent() || syncDir != 0;
    if (gateActive) {
        if (gateGain.getNumSamples() < n) gateGain.setSize(1, n, false, false, true);   // only when a host breaks the prepared block size
        auto gateApply = [this](bool closed) { if (!closed && notesGate.silent()) { notesGate.prepare(getSampleRate() > 0 ? getSampleRate() : 48000.0, 0.006, false); return; } notesGate.set(closed); (void) notesGate.takeEngageEdge(); };
        int gi = 0, pos = 0;
        while (pos < n) {
            while (gi < nGate && gateAt[gi] <= pos) { gateApply(gateClosed[gi]); ++gi; }
            const int end = gi < nGate ? juce::jmin(n, juce::jmax(pos + 1, gateAt[gi])) : n;
            juce::AudioBuffer<float> seg(gateGain.getArrayOfWritePointers(), 1, pos, end - pos);
            if (notesGate.silent()) seg.clear(); else { juce::FloatVectorOperations::fill(seg.getWritePointer(0), 1.0f, end - pos); notesGate.applyMute(seg); }
            pos = end;
        }
        while (gi < nGate) { gateApply(gateClosed[gi]); ++gi; }
        if (syncDir != 0) {   // the SYNC switch fade (linear, ~6 ms) on top of the gate
            const float step = (float) (1.0 / (0.006 * syncSr)); float* g = gateGain.getWritePointer(0);
            for (int i = 0; i < n; ++i) { syncG = juce::jlimit(0.0f, 1.0f, syncG + (float) syncDir * step); g[i] *= syncG; }
            if (syncDir == 1 && syncG >= 1.0f) syncDir = 0;
        }
    }
    pageMidi.clear(); pageMidi.addEvents(midi, 0, buffer.getNumSamples(), 0);   // ARCS listens to the keys too (no allocation: sized in prepareToPlay)
    if (arcsNoteOff >= 0) pageMidi.addEvent(juce::MidiMessage::noteOff(1, arcsNoteOff), 0);
    if (bypass.isBypassed()) pageMidi.addEvent(juce::MidiMessage::allNotesOff(1), 0);
    const bool labOpen = lab.open.load();
    if (labOpen && !wasLabOpen) { std::fill(std::begin(held), std::end(held), false); engine.padStopped.store(true); }   // opening the lab stops the pad (no drone under the lab)
    wasLabOpen = labOpen;
    juce::MidiBuffer labMidi; if (lab.wantsMidi()) labMidi.swapWith(midi);   // SPECTRAL LAB open in KEYS mode: the keys play the lab
    for (const auto m : labMidi) {   // ... but a key that was holding the pad still lets go of it
        const auto msg = m.getMessage();
        if (msg.isNoteOff()) held[msg.getNoteNumber()] = false; else if (msg.isAllNotesOff() || msg.isAllSoundOff()) std::fill(std::begin(held), std::end(held), false);
    }
    for (const auto m : midi) {
        const auto msg = m.getMessage();
        if (msg.isNoteOn()) { held[msg.getNoteNumber()] = true; strike = juce::jmax(strike, msg.getFloatVelocity()); engine.padStopped.store(false); }
        else if (msg.isNoteOff()) held[msg.getNoteNumber()] = false;
        else if (msg.isAllNotesOff() || msg.isAllSoundOff()) std::fill(std::begin(held), std::end(held), false);
    }
    int low = -1; for (int k = 0; k < 128; ++k) if (held[k]) { low = k; break; }
    if (low >= 0) lastTranspose = (float) (low - (60 + padRoot.load()));
    midi.clear();
    if (buffer.getNumChannels() < 2) { buffer.clear(); return; }
    if (getTotalNumInputChannels() > 0) { auto in = getBusBuffer(buffer, true, 0); if (in.getNumChannels() > 0) engine.pushInput(in.getReadPointer(0), in.getNumChannels() > 1 ? in.getReadPointer(1) : nullptr, buffer.getNumSamples()); }
    engine.padParams = padParams(); engine.padTranspose = lastTranspose; engine.padMidiGate = low >= 0; if (strike > 0) engine.padMidiStrike = strike;
    // pressing stop in the DAW stops the pad too (even with HOLD on); pressing play starts it again
    if (wasHostPlaying && ! playing) engine.padStopped.store(true);
    if (! wasHostPlaying && playing) engine.padStopped.store(false);
    wasHostPlaying = playing;
    engine.process(buffer, hbpm, ppq, playing, tempo(), gateActive ? gateGain.getReadPointer(0) : nullptr, engine.padParams.hold);   // PAINT LOOP + a HOLD pad follow PLAY: ON NOTES
    { lab::LabParams lp; lp.morph = labMorphP->load(); lp.level = labLevelP->load(); lp.width = labWidthP->load(); lp.output = juce::jlimit(0, 3, (int) labOutputP->load());
      lp.ghostHit = (int) labGhostP->load(); for (int b = 0; b < lab::kMaxBands; ++b) if (labMuteP[b]->load() > 0.5f) lp.mutes |= 1u << b;
      lab.output.store(lp.output, std::memory_order_relaxed); lab.level.store(lp.level, std::memory_order_relaxed); lab.width.store(lp.width, std::memory_order_relaxed); lab.tempo.store(hbpm, std::memory_order_relaxed);
      if (!gateActive) {   // ALWAYS, gate open and settled: exactly the classic path
          lab.process(buffer, labMidi, hbpm, ppq, playing, lp);
          arrange.process(buffer, arr::hostPos(head, isNonRealtime()));                        // adds nothing while the arrangement is empty
          arcsPage.process(buffer, arcs::ArcsHost::readPlayHead(head), true, &pageMidi);         // silent with no lines
#if GHOSTY_VOICE
          voice.process(buffer);                                                                          // the VOICE preview (silent unless it plays)
#endif
      } else {   // the transport-driven players render apart, the gate shapes them, then they join the mix
          if (gateBuf.getNumSamples() < n) gateBuf.setSize(2, n, false, false, true);   // only when a host breaks the prepared block size
          juce::AudioBuffer<float> tb(gateBuf.getArrayOfWritePointers(), 2, n); tb.clear();
          lab.process(buffer, labMidi, hbpm, ppq, playing, lp, &tb);
          arrange.process(tb, arr::hostPos(head, isNonRealtime()));
          arcsPage.process(tb, arcs::ArcsHost::readPlayHead(head), true, &pageMidi);
#if GHOSTY_VOICE
          voice.process(tb);
#endif
          for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::multiply(tb.getWritePointer(c), gateGain.getReadPointer(0), n);   // the same gate curve
          for (int c = 0; c < 2; ++c) buffer.addFrom(c, 0, tb, c, 0, n);
      }
      gateOpenUi.store(!notesGate.isBypassed()); }
    buffer.applyGain(apvts.getRawParameterValue("volume")->load() * 1.25f);
    demo.process(buffer, license.isUnlocked());
#if GHOSTY_PROCESS_LAB
    labAudition.render(buffer, 0, buffer.getNumSamples(), 0.9f);   // PROCESS page audition (silent unless the panel plays)
#endif
    bypass.applyMute(buffer);   // bit-exact no-op while BYPASS is off and settled
}

int SpecProcessor::addLayer(int kind, const juce::String& name, const juce::String& words, uint32_t seed, std::shared_ptr<gs::SampleData> sample) {
    if ((int) model.layers.size() >= sp::kLayers) return -1;
    sp::Layer l; l.kind = kind; l.name = name; l.words = words; l.seed = seed; l.sample = sample;
    if (kind == 1) if (auto s = drums->byName(name)) l.centroidMidi = sp::centroidOf(*s);
    if (kind == 1) l.space = 0.08f;
    model.layers.push_back(l); publish(); return (int) model.layers.size() - 1;
}
int SpecProcessor::layerFor(int kind, const juce::String& name) {
    for (int i = 0; i < (int) model.layers.size(); ++i) if (model.layers[(size_t) i].kind == kind && model.layers[(size_t) i].name == name) return i;
    return -1;
}
void SpecProcessor::removeLayer(int i) {
    if (!juce::isPositiveAndBelow(i, (int) model.layers.size())) return;
    model.layers.erase(model.layers.begin() + i);
    // the sounds are shared by every pattern: the removed sound's notes go from EVERY pattern, and the notes of the sounds after it
    // move down one place in every pattern, so each remaining note keeps its own sound
    auto drop = [i](std::vector<sp::Note>& N) { N.erase(std::remove_if(N.begin(), N.end(), [i](auto& n) { return n.layer == i; }), N.end());
                                               for (auto& n : N) if (n.layer > i) --n.layer; };
    drop(model.notes);
    for (int k = 0; k < sp::kPatterns; ++k) if (k != model.current) drop(model.patterns[(size_t) k].notes);
    model.storeCurrent();
    publish();
}

void SpecProcessor::publish() {
    auto s = std::make_unique<sp::SpecSnapshot>();
    // notes past the pattern's end (kept after the pattern was made shorter, so making it longer again brings them back) never play
    const double loopBeats = model.bars * 4.0;
    s->bars = model.bars; s->layerCount = (int) std::min<size_t>(model.layers.size(), sp::kLayers);
    for (int i = 0; i < s->layerCount; ++i) {
        const auto& L = model.layers[(size_t) i]; auto& o = s->layers[(size_t) i];
        o.kind = L.kind; o.vol = L.vol; o.pan = L.pan; o.tone = L.tone; o.space = L.space; o.mute = L.mute; o.centroid = L.centroidMidi;
        bool anyGlide = false; for (auto& n : model.notes) if (n.layer == i && n.glide && n.start < loopBeats) anyGlide = true;
        if (L.kind == 0) { o.snap = gs::snapFromPatch(gs::Summoner::summon(L.words, L.seed)); }
        else if (L.kind == 2) { gs::Patch p; p["smp_level"] = 0.9f; p["smp_mode"] = 0; p["smp_track"] = 1; p[gs::opId(0, "level")] = 0.0f; p["master"] = -4.0f; o.snap = gs::snapFromPatch(p); }
        if (anyGlide && L.kind != 1) { o.snap.mono = true; o.snap.glide = juce::jmax(o.snap.glide, 0.09f); }
        if (L.kind == 1) { auto d = drums->byName(L.name); o.drum = d.get(); }
        if (L.kind == 2 && L.sample) { keep.push_back(L.sample); engine.engine(i).setSample(L.sample); }
        else if (L.kind == 0) engine.engine(i).setSample(nullptr);
    }
    s->noteCount = 0;
    for (const auto& n : model.notes) if (n.start < loopBeats && s->noteCount < sp::kMaxNotes) s->notes[(size_t) s->noteCount++] = n;
    engine.setSnapshot(s.get()); snaps.push_back(std::move(s));
    syncPads();
    if (snaps.size() > 40) snaps.erase(snaps.begin(), snaps.begin() + 10);
}

// ---------------------------------------------------------------- PAD painter
sp::PadParams SpecProcessor::padParams() const {
    // order: Bloom, Space, Shimmer, Tape, Drive, Pedal, Drift, Level, Strum, Hold, Freeze (cached pointers, set in the constructor)
    auto v = [this](int i) { return padP[i]->load(); };
    sp::PadParams P; P.bloom = v(0); P.space = v(1); P.shimmer = v(2); P.tape = v(3); P.drive = v(4);
    P.pedal = v(5); P.drift = v(6); P.level = v(7); P.strum = v(8); P.hold = v(9) > 0.5f; P.freeze = v(10) > 0.5f;
    P.root = padRoot.load(); P.scale = padScale.load(); P.bpm = tempo();
    return P;
}
void SpecProcessor::syncPads() {
    auto& D = model.pads; bool used[sp::kMaxDots] {};
    for (auto& d : D) { if (d.slot >= 0 && d.slot < sp::kMaxDots && !used[d.slot]) used[d.slot] = true; else d.slot = -1; }
    for (auto& d : D) {
        if (d.id == 0) d.id = nextPadId++;
        if (d.slot < 0) for (int k = 0; k < sp::kMaxDots; ++k) if (!used[k]) { d.slot = k; used[k] = true; break; }
    }
    for (int k = 0; k < sp::kMaxDots; ++k) if (!used[k]) engine.padSlots[k].id.store(0);
    for (auto& d : D) {
        if (d.slot < 0) continue;
        auto& S = engine.padSlots[d.slot];
        S.pitch.store(d.pitch); S.tone.store(d.tone); S.gain.store(d.gain); S.type.store(d.voice); S.id.store(d.id);
    }
    padRoot.store(model.root); padScale.store(model.scale);
}

// ---------------------------------------------------------------- patterns
void SpecProcessor::switchPattern(int i) {
    if (!juce::isPositiveAndBelow(i, sp::kPatterns) || i == model.current) return;
    model.storeCurrent(); model.current = i;
    const auto& P = model.patterns[(size_t) i];
    model.notes = P.notes; model.pads = P.pads; model.bars = P.bars;
    for (auto& d : model.pads) { d.id = 0; d.slot = -1; }   // fresh voices: the old pattern's dots fade out, these fade in
    publish();
}
int SpecProcessor::copyPatternToNextFree() {
    model.storeCurrent();
    for (int k = 1; k < sp::kPatterns; ++k) {
        const int j = (model.current + k) % sp::kPatterns;
        if (!model.patterns[(size_t) j].used()) { model.patterns[(size_t) j] = model.patterns[(size_t) model.current]; switchPattern(j); return j; }
    }
    return -1;
}

// ---------------------------------------------------------------- DETECT KEY
void SpecProcessor::startKeyListen() {
    keyL.inDet.reset(); keyL.outDet.reset(); keyL.consumed = 0;
    keyL.inRead = engine.inPos.load(std::memory_order_acquire); keyL.outRead = engine.ringPos.load(std::memory_order_acquire);
    keyL.startMs = juce::Time::currentTimeMillis(); keyL.active = true;
}
int SpecProcessor::pollKeyListen(juce::String& message, float& progress) {
    if (!keyL.active) return 0;
    const double sr = engine.rate() > 0 ? engine.rate() : 48000.0;
    auto pull = [&](const float* ring, int pos, int& read, sp::KeyDetector& det) {
        int n = (pos - read + sp::SpecEngine::kRing) % sp::SpecEngine::kRing;
        for (int i = 0; i < n; ++i) keyL.tmp[(size_t) i] = ring[(read + i) % sp::SpecEngine::kRing];
        det.add(keyL.tmp.data(), n, sr); read = pos; return n;
    };
    pull(engine.inRing, engine.inPos.load(std::memory_order_acquire), keyL.inRead, keyL.inDet);
    keyL.consumed += pull(engine.ring, engine.ringPos.load(std::memory_order_acquire), keyL.outRead, keyL.outDet);
    const double listenFor = 5.0 * sr; const double waited = (juce::Time::currentTimeMillis() - keyL.startMs) / 1000.0;
    progress = (float) juce::jmin(1.0, juce::jmax(keyL.consumed / listenFor, waited / 9.0));
    if (keyL.consumed < listenFor && waited < 9.0) return 0;
    keyL.active = false;
    const bool useIn = keyL.inDet.frames >= 3 && keyL.inDet.rms() > 0.003;
    const auto& det = useIn ? keyL.inDet : keyL.outDet;
    const auto r = det.result();
    if (r.root < 0 || (!useIn && keyL.outDet.rms() < 0.003)) {
        message = "Didn't hear anything. Send audio into Spectrum's sidechain input (or press PLAY), then DETECT KEY again.";
        return -1;
    }
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    model.root = r.root; model.scale = r.minor ? 1 : 0; syncPads();
    message = juce::String("Heard ") + names[r.root] + (r.minor ? " minor" : " major") + (useIn ? " in the sidechain input" : " in Spectrum's own sound")
              + (r.margin < 0.03f ? " (a close call)." : ".") + " SNAP TO KEY now uses it.";
    return 1;
}

// ---------------------------------------------------------------- save / load
static juce::String notesToString(const std::vector<sp::Note>& N) {
    juce::String s; for (auto& n : N) s << n.layer << "," << n.start << "," << n.len << "," << n.pitch << "," << n.vel << "," << (int) n.glide << ";"; return s; }
static std::vector<sp::Note> notesFromString(const juce::String& str) {
    std::vector<sp::Note> out;
    for (auto& part : juce::StringArray::fromTokens(str, ";", "")) {
        auto f = juce::StringArray::fromTokens(part, ",", ""); if (f.size() < 6) continue;
        out.push_back({ f[0].getIntValue(), f[1].getDoubleValue(), f[2].getDoubleValue(), f[3].getFloatValue(), f[4].getFloatValue(), f[5].getIntValue() != 0 });
    }
    return out;
}
static juce::String padsToString(const std::vector<sp::PadDot>& D) {
    juce::String s; for (auto& d : D) s << d.tone << "," << d.pitch << "," << d.gain << "," << d.voice << ";"; return s; }
static std::vector<sp::PadDot> padsFromString(const juce::String& str) {
    std::vector<sp::PadDot> out;
    for (auto& part : juce::StringArray::fromTokens(str, ";", "")) {
        auto f = juce::StringArray::fromTokens(part, ",", ""); if (f.size() < 4) continue;
        sp::PadDot d; d.tone = juce::jlimit(0.0f, 1.0f, f[0].getFloatValue()); d.pitch = f[1].getFloatValue(); d.gain = juce::jlimit(0.05f, 1.0f, f[2].getFloatValue()); d.voice = juce::jlimit(0, sp::kVoiceTypes - 1, f[3].getIntValue());
        if ((int) out.size() < sp::kMaxDots) out.push_back(d);
    }
    return out;
}

juce::ValueTree SpecProcessor::toTree() const {
    auto M = model; M.storeCurrent();
    juce::ValueTree t("SPECTRUM2"); t.setProperty("bars", M.bars, nullptr); t.setProperty("root", M.root, nullptr); t.setProperty("scale", M.scale, nullptr); t.setProperty("snap", M.snap, nullptr);
    t.setProperty("quantize", M.quantize, nullptr); t.setProperty("grid", M.gridDiv, nullptr); t.setProperty("mode", M.mode, nullptr); t.setProperty("brush", M.brush, nullptr); t.setProperty("current", M.current, nullptr);
    for (auto& L : M.layers) {
        juce::ValueTree x("LAYER"); x.setProperty("kind", L.kind, nullptr); x.setProperty("name", L.name, nullptr); x.setProperty("words", L.words, nullptr); x.setProperty("seed", (int) L.seed, nullptr);
        x.setProperty("vol", L.vol, nullptr); x.setProperty("pan", L.pan, nullptr); x.setProperty("tone", L.tone, nullptr); x.setProperty("space", L.space, nullptr); x.setProperty("mute", L.mute, nullptr); x.setProperty("cent", L.centroidMidi, nullptr);
        if (L.sample) { x.setProperty("srate", L.sample->rate, nullptr); x.setProperty("sample", L.sample->toBase64(), nullptr); }
        t.appendChild(x, nullptr);
    }
    t.setProperty("notes", notesToString(M.patterns[0].notes), nullptr);   // pattern A also where 2.0 looked for it
    for (int i = 0; i < sp::kPatterns; ++i) {
        const auto& P = M.patterns[(size_t) i]; if (!P.used() && P.bars == 2) continue;
        juce::ValueTree x("PATTERN"); x.setProperty("index", i, nullptr); x.setProperty("bars", P.bars, nullptr);
        x.setProperty("notes", notesToString(P.notes), nullptr); x.setProperty("pads", padsToString(P.pads), nullptr);
        t.appendChild(x, nullptr);
    }
    // the pad knobs travel with a saved canvas too
    juce::ValueTree k("PADFX"); for (auto* id : { "padBloom", "padSpace", "padShimmer", "padTape", "padDrive", "padPedal", "padDrift", "padLevel", "padStrum", "padHold", "padFreeze" }) k.setProperty(id, apvts.getRawParameterValue(id)->load(), nullptr);
    t.appendChild(k, nullptr);
    return t;
}
void SpecProcessor::fromTree(const juce::ValueTree& t) {
    model = sp::Model(); model.root = t.getProperty("root", 0); model.scale = t.getProperty("scale", 1); model.snap = t.getProperty("snap", true);
    model.quantize = t.getProperty("quantize", true); model.gridDiv = t.getProperty("grid", 16); if (model.gridDiv != 4 && model.gridDiv != 8) model.gridDiv = 16;
    model.brush = juce::jlimit(0, sp::kVoiceTypes - 1, (int) t.getProperty("brush", 0));
    for (auto x : t) if (x.hasType("LAYER")) {
        sp::Layer L; L.kind = x.getProperty("kind"); L.name = x.getProperty("name").toString(); L.words = x.getProperty("words").toString(); L.seed = (uint32_t) (int) x.getProperty("seed", 1);
        L.vol = (float) (double) x.getProperty("vol", 0.8); L.pan = (float) (double) x.getProperty("pan", 0.0); L.tone = (float) (double) x.getProperty("tone", 0.5); L.space = (float) (double) x.getProperty("space", 0.2);
        L.mute = x.getProperty("mute", false); L.centroidMidi = (float) (double) x.getProperty("cent", 60.0);
        if (x.hasProperty("sample")) L.sample = gs::SampleData::fromBase64(x.getProperty("sample").toString(), (double) x.getProperty("srate", 44100.0), L.name);
        model.layers.push_back(L);
    }
    bool anyPattern = false;
    for (auto x : t) if (x.hasType("PATTERN")) {
        const int i = x.getProperty("index", -1); if (!juce::isPositiveAndBelow(i, sp::kPatterns)) continue;
        auto& P = model.patterns[(size_t) i]; P.bars = juce::jlimit(1, 4, (int) x.getProperty("bars", 2)); if (P.bars == 3) P.bars = 4;
        P.notes = notesFromString(x.getProperty("notes").toString()); P.pads = padsFromString(x.getProperty("pads").toString()); anyPattern = true;
    }
    if (!anyPattern) { auto& P = model.patterns[0]; P.notes = notesFromString(t.getProperty("notes").toString()); P.bars = t.getProperty("bars", 2); }   // a 2.0 file
    for (auto& P : model.patterns) P.notes.erase(std::remove_if(P.notes.begin(), P.notes.end(), [this](auto& n) { return n.layer < 0 || n.layer >= (int) model.layers.size(); }), P.notes.end());
    model.current = juce::jlimit(0, sp::kPatterns - 1, (int) t.getProperty("current", 0));
    const auto& P = model.patterns[(size_t) model.current]; model.notes = P.notes; model.pads = P.pads; model.bars = P.bars;
    model.mode = t.hasProperty("mode") ? juce::jlimit(0, 1, (int) t.getProperty("mode")) : (model.notes.empty() ? 0 : 1);
    auto k = t.getChildWithName("PADFX");
    if (k.isValid()) for (int i = 0; i < k.getNumProperties(); ++i) { const auto id = k.getPropertyName(i).toString(); if (auto* prm = apvts.getParameter(id)) prm->setValueNotifyingHost(prm->convertTo0to1((float) (double) k.getProperty(id))); }
    publish();
}

juce::File SpecProcessor::userDir() const { auto d = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Ghosty").getChildFile("Spectrum"); d.createDirectory(); return d; }
bool SpecProcessor::saveCanvas(const juce::String& name) { if (auto xml = toTree().createXml()) return xml->writeTo(userDir().getChildFile(juce::File::createLegalFileName(name) + ".ghostspec")); return false; }
bool SpecProcessor::loadCanvas(const juce::File& f) { auto xml = juce::XmlDocument::parse(f); if (!xml) return false; auto t = juce::ValueTree::fromXml(*xml); if (!t.hasType("SPECTRUM2")) return false; fromTree(t); return true; }

// ---------------------------------------------------------------- bounce (at the host's tempo when there is one)
// the pattern as heard, frozen on the message thread (copies only), rendered on any thread (SEND TO ARRANGE renders on a worker)
SpecProcessor::LoopRender SpecProcessor::prepareLoopRender(const sp::SpecSnapshot* s, bool withPads) const {
    LoopRender r; r.snap = std::make_shared<sp::SpecSnapshot>(*s);
    for (int i = 0; i < s->layerCount; ++i) r.samples[(size_t) i] = model.layers[(size_t) i].kind == 2 ? model.layers[(size_t) i].sample : nullptr;
    if (withPads) for (int k = 0; k < sp::kMaxDots; ++k) { const auto& a = engine.padSlots[k]; auto& d = r.dots[(size_t) k];
        d.id = a.id.load(); d.pitch = a.pitch.load(); d.tone = a.tone.load(); d.gain = a.gain.load(); d.type = a.type.load(); r.anyPad |= d.id != 0; }
    r.pp = padParams(); r.bpm = tempo(); r.volume = apvts.getRawParameterValue("volume")->load();
    return r;
}
juce::AudioBuffer<float> SpecProcessor::renderLoop(const LoopRender& r) {
    const double sr = 48000; const int block = 512; const auto* s = r.snap.get();
    auto off = std::make_unique<sp::SpecEngine>(); off->prepare(sr, block); off->setSnapshot(s); off->playing = true;
    for (int i = 0; i < s->layerCount; ++i) if (r.samples[(size_t) i]) off->engine(i).setSample(r.samples[(size_t) i]);
    for (int k = 0; k < sp::kMaxDots; ++k) { const auto& d = r.dots[(size_t) k]; auto& b = off->padSlots[k];
        b.pitch.store(d.pitch); b.tone.store(d.tone); b.gain.store(d.gain); b.type.store(d.type); b.id.store(d.id); }
    auto pp = r.pp; off->padTranspose = 0;
    const double b = r.bpm; const int loopN = (int) (s->bars * 4 * 60.0 / b * sr), total = loopN + (int) (sr * (r.anyPad ? 5.0 : 2.0));
    juce::AudioBuffer<float> all(2, total), buf(2, block);
    for (int pos = 0; pos < total; pos += block) {
        const int n = juce::jmin(block, total - pos); buf.setSize(2, n, false, false, true);
        off->playing = pos < loopN; pp.hold = pos < loopN; off->padParams = pp; off->process(buf, b, 0, false, b);
        for (int c = 0; c < 2; ++c) all.copyFrom(c, pos, buf, c, 0, n);
    }
    all.applyGain(r.volume * 1.25f);
    return all;
}
juce::File SpecProcessor::renderTo(const sp::SpecSnapshot* s, const juce::File& f, bool withPads) {
    const auto all = renderLoop(prepareLoopRender(s, withPads));
    f.deleteFile(); juce::WavAudioFormat wav;
    if (auto os = std::unique_ptr<juce::OutputStream>(f.createOutputStream()))
        if (auto* w = wav.createWriterFor(os.get(), 48000, 2, 24, {}, 0)) { os.release();   // GATE: via-caller (bounce / bounceStems asked spec.pattern.*)
            std::unique_ptr<juce::AudioFormatWriter> wr(w); wr->writeFromAudioSampleBuffer(all, 0, all.getNumSamples()); }
    return f;
}
// SEND TO ARRANGE from PAINT PADS / LOOP: render what you hear on a worker, then hand it to ARRANGE as a tempo-tagged clip (message thread)
void SpecProcessor::sendPatternToArrange(std::function<void(juce::uint32)> done) {
    if (snaps.empty()) { if (done) done(0); return; }
    auto job = std::make_shared<LoopRender>(prepareLoopRender(snaps.back().get(), true));
    const juce::String name = juce::String(model.mode == 0 ? "Pad " : "Loop ") + juce::String::charToString((juce::juce_wchar) ('A' + model.current));
    std::weak_ptr<bool> alive = aliveToken;
    juce::Thread::launch([this, job, name, alive, done] {
        auto buf = std::make_shared<juce::AudioBuffer<float>>(renderLoop(*job));
        const float pk = juce::jmax(buf->getMagnitude(0, 0, buf->getNumSamples()), buf->getMagnitude(1, 0, buf->getNumSamples())); if (pk > 0.98f) buf->applyGain(0.95f / pk);
        juce::MessageManager::callAsync([this, buf, name, alive, done, bpm = job->bpm] { if (alive.expired()) return;
            const auto id = arrange.receive(arr::makePayload(*buf, 48000, name, {}, {}, bpm)); if (done) done(id); });
    });
}
// SEND TO ARRANGE from ARCS: freeze the page here, render it on a worker (with its notes as MIDI), hand it to ARRANGE (message thread)
// an ARCS clip from its render blob (re-render, any thread): nullptr when the blob is unreadable or renders nothing
arr::PayloadPtr SpecProcessor::arcsPayload(const arr::SourceRef& r) {
    const auto req = arcs::ArcsHost::fromState(r.state); if (!req.valid()) return nullptr;
    juce::AudioBuffer<float> buf; std::vector<arcs::RenderNote> rn;
    if (!arcs::ArcsHost::renderToBuffer(req, buf, 48000, &rn) || buf.getNumSamples() == 0) return nullptr;
    const float pk = juce::jmax(buf.getMagnitude(0, 0, buf.getNumSamples()), buf.getNumChannels() > 1 ? buf.getMagnitude(1, 0, buf.getNumSamples()) : 0.0f);
    if (!(pk > 0.0f)) return nullptr;   // an empty / unreadable page renders silence: no clip
    if (pk > 0.98f) buf.applyGain(0.95f / pk);
    std::vector<arr::Note> notes; for (const auto& x : rn) { arr::Note a; a.pitch = x.pitch; a.velocity = x.velocity; a.start = x.start; a.length = x.length; notes.push_back(a); }
    arr::SourceRef src { "ARCS", req.name.isNotEmpty() ? req.name : r.name, r.state };
    return arr::makePayload(buf, 48000, src.name.isNotEmpty() ? src.name : juce::String("Arcs"), std::move(notes), std::move(src), req.bpm);
}
void SpecProcessor::sendArcsToArrange(std::function<void(juce::uint32)> done) {
    auto req = std::make_shared<arcs::RenderRequest>(arcsPage.prepareRender());
    if (!req->valid()) { if (done) done(0); return; }
    auto blob = std::make_shared<juce::MemoryBlock>(arcsPage.renderBlob());   // what ARRANGE re-renders from later
    std::weak_ptr<bool> alive = aliveToken;
    juce::Thread::launch([this, req, blob, alive, done] {
        auto pl = std::make_shared<arr::PayloadPtr>(arcsPayload(arr::SourceRef { "ARCS", req->name, *blob }));   // the same path a re-render takes
        juce::MessageManager::callAsync([this, pl, alive, done] { if (alive.expired()) return;
            const auto id = *pl ? arrange.receive(*pl) : 0u; if (done) done(id); });
    });
}
juce::File SpecProcessor::bounce(juce::Component* ui) {
    if (! allowExport(ui, "spec.pattern.wav-drag")) return {};
    auto d = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("GhostySpectrum"); d.createDirectory();
    const juce::String pat = juce::String::charToString((juce::juce_wchar) ('A' + model.current));
    return renderTo(snaps.back().get(), d.getChildFile("Ghosty Spectrum " + pat + " " + juce::String(juce::roundToInt(tempo())) + "bpm " + juce::Time::getCurrentTime().formatted("%H%M%S") + ".wav"));
}
juce::StringArray SpecProcessor::bounceStems(juce::Component* ui) {
    if (! allowExport(ui, "spec.pattern.stems-drag")) return {};
    juce::StringArray out; const auto& base = *snaps.back();
    auto d = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("GhostySpectrum").getChildFile("Stems " + juce::Time::getCurrentTime().formatted("%H%M%S")); d.createDirectory();
    for (int l = 0; l < base.layerCount; ++l) {
        bool used = false; for (int i = 0; i < base.noteCount; ++i) if (base.notes[(size_t) i].layer == l) used = true;
        if (!used || base.layers[(size_t) l].mute) continue;
        auto s = std::make_unique<sp::SpecSnapshot>(base); for (int k = 0; k < s->layerCount; ++k) s->layers[(size_t) k].solo = (k == l); s->anySolo = true;
        auto f = d.getChildFile(juce::String(l + 1).paddedLeft('0', 2) + " " + juce::File::createLegalFileName(model.layers[(size_t) l].name) + ".wav");
        renderTo(s.get(), f, false); out.add(f.getFullPathName());
    }
    if (!model.pads.empty()) {   // the painted pad as its own stem
        auto s = std::make_unique<sp::SpecSnapshot>(base); for (int k = 0; k < s->layerCount; ++k) s->layers[(size_t) k].solo = false; s->anySolo = true;
        auto f = d.getChildFile("00 Pad.wav"); renderTo(s.get(), f, true); out.add(f.getFullPathName());
    }
    return out;
}

// state: the XML block as before (+ the ARCS page as a child), then the ARRANGE blob appended in binary after it:
//   [JUCE xml block][ "GARR" ][ int32 size ][ arrangement ]   - older builds read the XML block and ignore the rest
static constexpr char kArrangeTag[4] = { 'G', 'A', 'R', 'R' };
void SpecProcessor::getStateInformation(juce::MemoryBlock& dest) {
    // demo: the project keeps the user's own imported sounds but no audio the plugin made (ARRANGE renders / bounces / page
    // hand-overs, the LAB's PROCESS results and canvas renders): a saved project is not a way to take rendered audio out
    const bool keepRendered = exportOpen(); int strippedLab = 0, strippedArrange = 0;
    auto st = apvts.copyState(); st.appendChild(toTree(), nullptr); st.appendChild(lab.toTree(keepRendered, &strippedLab), nullptr); st.appendChild(arcsPage.getState(), nullptr);
    { juce::ValueTree ui("SPECTRUMUI"); ui.setProperty("theme_name", ghosty::themes::toString(uiLook), nullptr); ui.setProperty("dark", uiLook != ghosty::themes::Theme::Light, nullptr); /* "dark" kept for older builds */ ui.setProperty("sync", (int) sync.getMode(), nullptr); ui.setProperty("own_bpm", sync.getOwnBpm(), nullptr); ui.setProperty("guideSeen", guideSeen.load(), nullptr); st.appendChild(ui, nullptr); }   // SYNC: per project
    { auto pl = plabState(); if (pl.isValid()) st.appendChild(pl, nullptr); }   // the PROCESS chain (small; no audio)
#if GHOSTY_VOICE
    st.appendChild(voice.toTree(), nullptr);   // "VOICE": settings + sources (FLAC, cached per sound)
#endif
    if (auto xml = st.createXml()) copyXmlToBinary(*xml, dest);
    const auto a = arrange.getState(keepRendered, &strippedArrange);
    if (strippedLab > 0) ghosty::exportgate::renderedAudioLeftOut(nullptr, "save:spec.lab-results");        // (a host save: no click, so no hint)
    if (strippedArrange > 0) ghosty::exportgate::renderedAudioLeftOut(nullptr, "save:spec.arrange-clips");
    if (a.getSize() > 0) { dest.append(kArrangeTag, 4); const juce::int32 n = (juce::int32) a.getSize(); dest.append(&n, 4); dest.append(a.getData(), a.getSize()); }
}
void SpecProcessor::setStateInformation(const void* data, int size) {
    ghosty::StateLoadNotify notifyHost (*this);   // CLAP: tell the host the values changed (Shared/HostNotify.h)
    if (! license.isUnlocked()) return;   // demo mode: a reopened project starts at the default sound (unlocking brings the saved sound back)
    auto xml = getXmlFromBinary(data, size); if (!xml) return; auto t = juce::ValueTree::fromXml(*xml);
    { // ARRANGE: the blob after the XML block (sessions without one load an empty arrangement)
        bool got = false; const auto* b = static_cast<const char*>(data);
        if (size >= 8 && juce::ByteOrder::littleEndianInt(b) == 0x21324356) {
            const int xmlLen = (int) juce::ByteOrder::littleEndianInt(b + 4), at = 8 + xmlLen + 1;
            if (xmlLen >= 0 && at + 8 <= size && std::memcmp(b + at, kArrangeTag, 4) == 0) {
                juce::int32 n = 0; std::memcpy(&n, b + at + 4, 4);
                if (n > 0 && at + 8 + n <= size) got = arrange.setState(b + at + 8, (size_t) n);
            }
        }
        if (!got) arrange.setState(defaultArrange.getData(), defaultArrange.getSize());
    }
    { auto a = t.getChildWithName("ARCSPAGE"); arcsPage.setState(a.isValid() ? a : defaultArcs); if (a.isValid()) t.removeChild(a, nullptr); }
    { auto u = t.getChildWithName("SPECTRUMUI"); // the look: "theme_name" (dark | purple | light); older sessions: the old "dark" switch (true = BLACK, false = LIGHT); none = BLACK
      uiLook = u.isValid() && u.hasProperty("theme_name") ? ghosty::themes::fromString(u["theme_name"].toString(), ghosty::themes::Theme::Dark)
             : (u.isValid() && u.hasProperty("dark") ? ((bool) u["dark"] ? ghosty::themes::Theme::Dark : ghosty::themes::Theme::Light) : ghosty::themes::Theme::Dark); guideSeen = u.isValid() ? (int) u.getProperty("guideSeen", 0) : 0;
      // SYNC: older sessions (no property) = SYNC TO DAW, own tempo = the session's no-host tempo
      sync.setMode(u.isValid() && (int) u.getProperty("sync", 0) == 1 ? ghosty::SyncTransport::Mode::Own : ghosty::SyncTransport::Mode::Daw);
      if (u.isValid() && u.hasProperty("own_bpm")) sync.setOwnBpm((double) u["own_bpm"]); else { auto* b = t.getChildWithProperty("id", "bpm").getPropertyPointer("value"); sync.setOwnBpm(b != nullptr ? (double) *b : 120.0); }
      if (u.isValid()) t.removeChild(u, nullptr); }
#if GHOSTY_VOICE
    { auto v = t.getChildWithName("VOICE"); voice.fromTree(v.isValid() ? v : defaultVoice); if (v.isValid()) t.removeChild(v, nullptr); }   // old sessions: an empty VOICE page
#endif
    { auto pl = t.getChildWithName("PLAB"); { const juce::ScopedLock sl(plabLock); plabTree = pl.isValid() ? pl.createCopy() : juce::ValueTree(); } ++plabGen; if (pl.isValid()) t.removeChild(pl, nullptr); }
    auto m = t.getChildWithName("SPECTRUM2"); if (m.isValid()) { fromTree(m); t.removeChild(m, nullptr); }
    auto lb = t.getChildWithName("SPECTRALLAB"); if (lb.isValid()) { lab.fromTree(lb); t.removeChild(lb, nullptr); }
    if (t.hasType(apvts.state.getType())) {
        // a lab session from before these were parameters: carry its level / hear setting over
        auto had = [&t](const char* id) { for (const auto& c : t) if (c.getProperty("id").toString() == id) return true; return false; };
        const bool oldLab = lb.isValid() && !had("labLevel");
        apvts.replaceState(t);
        if (oldLab) {
            if (auto* p = apvts.getParameter("labLevel")) p->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, (float) (double) lb.getProperty("level", 0.8)));
            if (auto* p = apvts.getParameter("labOutput")) p->setValueNotifyingHost(p->convertTo0to1((float) juce::jlimit(0, 3, (int) lb.getProperty("output", 0))));
            if (auto* p = apvts.getParameter("labWidth")) p->setValueNotifyingHost(0.0f);   // it was mono then
        }
    }
}
juce::AudioProcessorEditor* SpecProcessor::createEditor() { return new SpecEditor(*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SpecProcessor(); }
