#include "HumProcessor.h"
#include "HumEditor.h"
#if JucePlugin_Build_Standalone
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif

namespace hum {

static constexpr int kDirtySeq = 1, kDirtyPitch = 2, kDirtyHear = 4;

juce::AudioProcessorValueTreeState::ParameterLayout HumProcessor::layout() {
    using namespace juce; std::vector<std::unique_ptr<RangedAudioParameter>> ps;
    auto choice = [&](const char* id, const char* name, StringArray items, int def) { ps.push_back(std::make_unique<AudioParameterChoice>(ParameterID { id, 1 }, name, items, def)); };
    auto flt = [&](const char* id, const char* name, float lo, float hi, float def, float step = 0.0f) { ps.push_back(std::make_unique<AudioParameterFloat>(ParameterID { id, 1 }, name, NormalisableRange<float>(lo, hi, step), def)); };
    auto onoff = [&](const char* id, const char* name, bool def) { ps.push_back(std::make_unique<AudioParameterBool>(ParameterID { id, 1 }, name, def)); };
    choice("mode", "Mode", { "Melody", "Beatbox" }, 0);
    choice("quant", "Quantize", { "Off", "1/4", "1/8", "1/16", "1/32" }, 0);
    flt("qstrength", "Quantize Strength", 0, 100, 100, 1);
    // swing: 50 straight .. 75 underneath (unchanged, so projects keep their swing); shown everywhere as 0-100 % (build 7), in 1 % steps
    ps.push_back(std::make_unique<AudioParameterFloat>(ParameterID { "swing", 1 }, "Swing", NormalisableRange<float>(50.0f, 75.0f, 0.25f), 50.0f,
        AudioParameterFloatAttributes().withLabel("%").withStringFromValueFunction([](float v, int) { return String(roundToInt((v - 50.0f) * 4.0f)); })
                                       .withValueFromStringFunction([](const String& t) { return 50.0f + jlimit(0.0f, 100.0f, t.getFloatValue()) * 0.25f; })));
    choice("tempomode", "Tempo", { "Free", "BPM" }, 0);
    flt("bpm", "BPM", 40, 240, 100, 0.1f);
    onoff("metro", "Metronome", false);
    onoff("countin", "Count-In", true);
    choice("sound", "Sound", { "Piano", "Soft Keys", "Bell", "Pluck" }, 0);
    choice("pitch", "Pitch", { "Original", "Nearest Note", "Snap To Key" }, 1);
    { juce::StringArray keys { "Detected" }; for (int sc = 0; sc < 2; ++sc) for (int k = 0; k < 12; ++k) keys.add(juce::String(keyName(k)) + (sc ? " minor" : " major")); choice("key", "Key", keys, 0); }
    choice("source", "Play", { "On Grid", "Original" }, 0);
    flt("volume", "Volume", 0, 1, 0.8f);
    choice("input", "Input", { "Auto", "Track", "Mac Mic" }, 0);   // build 3: where Ghost listens (AUTO: the track when it has sound, else the Mac's mic)
    choice("ears", "Ears", { "Build 4", "Build 3" }, 0);   // build 4: which listening engine (BUILD 3 = the old one, kept selectable)
    flt("smooth", "Smooth", 0, 100, 50, 1);   // build 4: note smoothing (melody): how held a pitch change must be to make a new note
    onoff("bypass", "Bypass", false);   // LAST (Shared/Bypass.h)
    return { ps.begin(), ps.end() };
}

HumProcessor::HumProcessor()
    : AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true).withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "GhostHum", layout()) {
    takes[0].mode = Melody; takes[1].mode = Beatbox;
    for (auto* id : { "mode", "quant", "qstrength", "swing", "source", "sound", "pitch", "key", "volume", "ears", "smooth" }) apvts.addParameterListener(id, &listener);
    // (2026-11-07, a fresh start every time: the sounds you taught live in the project's own state, not in a file every new Ghost Hum
    // reads on open; only the GHOST ANIMATION look is remembered between instances)
    loadSettings();
    mic.sink = [this](const float* const* in, int nCh, int n) { micCallback(in, nCh, n); };
   #if JucePlugin_Build_Standalone
    // the standalone app: JUCE mutes the microphone by default (feedback protection for effects); Ghost Hum IS the microphone, so the
    // first run starts with it open (the user's own choice in Options is kept afterwards)
    if (juce::JUCEApplicationBase::isStandaloneApp()) if (auto* h = juce::StandalonePluginHolder::getInstance()) if (h->settings != nullptr && !h->settings->containsKey("shouldMuteInput")) h->settings->setValue("shouldMuteInput", false);
   #endif
}
HumProcessor::~HumProcessor() {
    stopTimer(); cancelPendingUpdate(); recording.store(false); micArmed.store(false); closeMic();   // (closeMic waits for a running device callback)
    std::lock_guard<std::mutex> g(analysedLock); aliveFlag->store(false);
}

bool HumProcessor::isBusesLayoutSupported(const BusesLayout& l) const {
    if (l.getMainOutputChannelSet() != juce::AudioChannelSet::stereo() && l.getMainOutputChannelSet() != juce::AudioChannelSet::mono()) return false;
    const auto in = l.getMainInputChannelSet(); return in.isDisabled() || in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void HumProcessor::prepareToPlay(double sr, int block) {
    sampleRate = sr; blockSize = block; synth.prepare(sr); bypass.prepare(sr, 0.006, bypassParam() != nullptr && bypassParam()->get()); dry.setSize(2, juce::jmax(block, 4096));   // (room for a host that sends bigger blocks than it said: no reallocation on the audio thread)
    rec.assign((size_t) (sr * kMaxRecordSeconds) + 1024, 0.0f); recLen.store(0);
}

// ---------------------------------------------------------------- audio
void HumProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals nd; const int n = buffer.getNumSamples(); const double sr = sampleRate;
    // the DAW's tempo (shown as a HOST pill next to BPM; never applied by itself)
    if (auto* ph = getPlayHead()) { if (const auto pos = ph->getPosition()) { if (const auto b = pos->getBpm()) hostBpmA.store(*b > 20 && *b < 400 ? *b : 0.0); } }
    // BYPASS (Shared/Bypass.h): mutes the plugin; recording still runs so a take is never lost
    bypass.set(bypassParam() != nullptr && bypassParam()->get());
    if (bypass.takeEngageEdge()) { synth.allOff(true); if (playing.load()) { playing.store(false); } }
    // input: the mic / the track (mono mix), the meter, the recording. With no input channels (an instrument in FL's channel rack) the
    // take comes from the Mac's microphone through micCallback; the host side still runs the count-in, the metronome and its clock.
    { const int inCh = getTotalNumInputChannels(); float pk = 0; int from = 0;
      if (inCh == 0) buffer.clear();   // (a host may hand an instrument output channels full of leftovers; the dry copy below must be silence)
      // COUNT-IN (BPM mode): one bar of clicks, 4 3 2 1, then the recording starts on the next downbeat (with its own click)
      if (countingIn.load()) { const double bps = workBpm() / 60.0; from = n;
          for (int i = 0; i < n; ++i) { const int before = (int) std::floor(countPhase); countPhase += bps / sr; const int now = (int) std::floor(countPhase);
              if (now != before) { if (now >= 4) { countingIn.store(false); countLeft.store(0); recLen.store(0); micLen.store(0); recClock = 0; clickCount.store(0); recBeatPhase = countPhase - 4.0; recording.store(true); from = i; break; }
                  synth.click(false); countLeft.store(4 - now); } } }
      if (recording.load()) { const int clk = recClock; const bool first = clk == 0;
          if (inCh > 0 && src.load() != SrcMic) { int len = recLen.load(); const int cap = (int) rec.size();   // the host's samples (unless the mic owns the take)
              for (int i = from; i < n && len < cap; ++i) { float s = 0; for (int c = 0; c < inCh; ++c) s += buffer.getReadPointer(c)[i]; s /= (float) inCh; rec[(size_t) len++] = s; pk = juce::jmax(pk, std::abs(s)); }
              recLen.store(len); hostTakePk.store(juce::jmax(hostTakePk.load(), pk)); if (len >= cap) { recording.store(false); recFull.store(true); triggerAsyncUpdate(); } }   // 90 s: it stops by itself; the message thread finishes the take (stopRecording)
          else if (inCh > 0) for (int c = 0; c < inCh; ++c) pk = juce::jmax(pk, buffer.getMagnitude(c, from, n - from));
          recClock += n - from;
          // the metronome while recording at a set tempo (beat 0 = the first recorded sample; accent every bar); after a count-in the downbeat
          // click always plays (it is the "go"); every click's time is kept (seconds of the take, from the host's clock: a mic take starts on
          // the same tap within a block) so the analysis can mask what leaks from the speakers into the mic
          if (!tempoFree()) { const double bps = workBpm() / 60.0, phase0 = recBeatPhase; const int startBeat = (int) std::floor(phase0);
              if (first && (metronome() || countIn())) clickNow(true, 0);
              recBeatPhase += bps * (n - from) / sr; if (metronome()) for (int b = startBeat + 1; b <= (int) std::floor(recBeatPhase); ++b) clickNow(b % 4 == 0, clk + (int) ((b - phase0) / bps * sr)); } }
      else if (inCh > 0) for (int c = 0; c < inCh; ++c) pk = juce::jmax(pk, buffer.getMagnitude(c, 0, n));
      if (pk > kSilence) hostLastSignalMs.store(juce::Time::getMillisecondCounter());   // (AUTO: a track that had sound lately is trusted at the tap)
      hostLevel.store(hostLevel.load() * 0.7f + pk * 0.3f); }
    // the host's own MIDI plays the sounds too (a keyboard on the track)
    for (const auto m : midi) { const auto msg = m.getMessage();
        if (msg.isNoteOn()) { if (mode() == Beatbox) { const int d = drumOfGm(msg.getNoteNumber()); if (d >= 0) synth.drum(d, msg.getFloatVelocity()); else synth.noteOn(msg.getNoteNumber(), msg.getFloatVelocity()); } else synth.noteOn(msg.getNoteNumber(), msg.getFloatVelocity()); }
        else if (msg.isNoteOff()) synth.noteOff(msg.getNoteNumber()); else if (msg.isAllNotesOff() || msg.isAllSoundOff()) synth.allOff(); }
    midi.clear();
    // in a DAW the track's own sound passes through (an insert must not silence the vocal track); the app never feeds the mic to the
    // speakers (that would howl). The piano / drums are added on top.
    const bool passThrough = !juce::JUCEApplicationBase::isStandaloneApp();
    if (passThrough) { dry.setSize(buffer.getNumChannels(), n, false, false, true); for (int c = 0; c < buffer.getNumChannels(); ++c) dry.copyFrom(c, 0, buffer, c, 0, n);
        if (bypass.silent()) { midi.clear(); return; } }   // bypassed: the track's sound, untouched
    else { buffer.clear(); if (bypass.silent()) { midi.clear(); return; } }
    synth.voice = voice(); synth.gain = p("volume")->load();
    { const juce::SpinLock::ScopedTryLockType tl(seqLock); if (tl.isLocked() && seqPending) { seqOld = std::move(seq); seq = seqPending; seqPending.reset(); seqPos = 0; seqEnd = sound::sequenceEnd(*seq); double t = 0; for (auto& e : *seq) { t = e.t; break; } playSec.store(juce::jmin(0.0, t) - 0.02); } }
    if (stopRequest.exchange(false)) { synth.allOff(); playing.store(false); }
    { const int at = auditionType.exchange(-1); if (at == 0) { synth.noteOn(auditionNum.load(), 0.8f); auditionOff = (int) (sr * 0.35); auditionOffNum = auditionNum.load(); } else if (at == 1) synth.drum(auditionNum.load(), 0.9f); }
    float* L = buffer.getWritePointer(0); float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;
    int done = 0; double t = playSec.load(); const int ch = mode() == Melody ? 1 : 10;
    while (done < n) {
        int next = n;
        if (playing.load() && seq && seqPos < seq->size()) { const double et = (*seq)[seqPos].t; const int off = (int) std::ceil((et - t) * sr) + done; if (off <= done) {
                const auto& e = (*seq)[seqPos++]; auto wheel = [](float c) { return juce::jlimit(0, 16383, (int) std::lround(8192 + juce::jlimit(-200.0f, 200.0f, c) / 200.0 * 8192)); };
                if (e.type == 0) { synth.noteOn(e.num, e.vel, e.cents); if (std::abs(e.cents) > 0.5f || wheelBent) { midi.addEvent(juce::MidiMessage::pitchWheel(ch, wheel(e.cents)), done); wheelBent = std::abs(e.cents) > 0.5f; } midi.addEvent(juce::MidiMessage::noteOn(ch, e.num, e.vel), done); }
                else if (e.type == 1) { synth.noteOff(e.num); midi.addEvent(juce::MidiMessage::noteOff(ch, e.num), done); }
                else if (e.type == 3) { synth.bendTo(e.num, e.cents); midi.addEvent(juce::MidiMessage::pitchWheel(ch, wheel(e.cents)), done); wheelBent = true; }
                else { synth.drum(e.num, e.vel); midi.addEvent(juce::MidiMessage::noteOn(ch, gmNote(e.num), e.vel), done); midi.addEvent(juce::MidiMessage::noteOff(ch, gmNote(e.num)), juce::jmin(n - 1, done + 1)); }
                continue; }
            next = juce::jmin(n, off); }
        if (auditionOff > 0) { if (auditionOff <= next - done) { next = done + auditionOff; } }
        const int k = next - done; if (k > 0) { synth.render(L + done, R ? R + done : nullptr, k); done += k; if (playing.load()) { t += k / sr; } if (auditionOff > 0) { auditionOff -= k; if (auditionOff <= 0) { synth.noteOff(auditionOffNum); auditionOff = 0; } } }
    }
    if (playing.load()) { playSec.store(t); if (seq && seqPos >= seq->size() && t > seqEnd + (mode() == Melody ? 1.5 : 0.8)) { playing.store(false); synth.allOff(); } }
    if (passThrough) bypass.applyDryWet(buffer, dry); else bypass.applyMute(buffer);   // the fade on BYPASS engage / release
}

// ---------------------------------------------------------------- recording -> analysis
void HumProcessor::startRecording() {
    if (recording.load() || countingIn.load() || analysing.load()) return;
    if (!armInput()) { changed(); return; }   // (INPUT = TRACK on a channel with no input, or a mic that will not open: a word in the status, no take)
    stop(); recLen.store(0); micLen.store(0); recClock = 0; hostTakePk.store(0); micTakePk.store(0); noSoundSaid = false; clickCount.store(0); recBeatPhase = 0; countPhase = 0; recMode = mode(); recFull.store(false);
    if (!tempoFree() && countIn()) { countLeft.store(4); countingIn.store(true); synth.click(true); status = "Count-in..."; }   // (the first click now: "4")
    else { recording.store(true); status = listeningText(); }
    startTimerHz(10); changed();
}
// the record tap: who gives the sound. TRACK: the host or nothing. MAC MIC: the Mac's microphone or nothing. AUTO: the host when it has
// channels and carried sound in the last 3 s; the mic alone when the host has no channels; otherwise both run and pollSources decides.
bool HumProcessor::armInput() {
    const int inCh = getTotalNumInputChannels(), choice = inputChoice(); micArmed.store(false); const bool hostRecent = hostHasSound();
    const juce::String putOnTrack = "Put Ghost Hum on your microphone's mixer track instead";
    if (choice == InputTrack) { if (inCh == 0) { status = "No audio comes into this channel. " + putOnTrack + ", or set INPUT to MAC MIC."; return false; } src.store(SrcHost); return true; }
    if (choice == InputMic || inCh == 0 || !hostRecent) {
        if (openMic()) { micArmed.store(true); micLastUsedMs = juce::Time::getMillisecondCounter(); src.store(choice == InputMic || inCh == 0 ? SrcMic : SrcNone); return true; }
        if (choice == InputMic || inCh == 0) { status = "Could not open your Mac's microphone" + (mic.error.isNotEmpty() ? " (" + mic.error + ")" : juce::String()) + ". " + (inCh == 0 ? putOnTrack + "." : "Set INPUT to AUTO or TRACK."); return false; } }
    src.store(SrcHost); return true;
}
bool HumProcessor::openMic() {
   #if GHOSTY_TEST_HOOKS
    if (fakeMic.none) { mic.error = "no input device"; return false; }
    if (fakeMic.available) { if (micRate.load() != fakeMic.rate || micRec.empty()) { micRate.store(fakeMic.rate); micRec.assign((size_t) (fakeMic.rate * kMaxRecordSeconds) + 1024, 0.0f); } micReady.store(true); return true; }
   #endif
    if (!mic.isOpen()) { if (!mic.open()) return false; }
    const double r = mic.rate.load(); if (r <= 0) { mic.error = "the input device has no sample rate"; closeMic(); return false; }
    const size_t need = (size_t) (r * kMaxRecordSeconds) + 1024; if (micRec.size() < need || micRate.load() != r) { micRate.store(r); micRec.assign(need, 0.0f); }   // (message thread; the device is not armed yet)
    micReady.store(true); if (!isTimerRunning()) startTimer(1000);   // (the idle watch)
    return true;
}
void HumProcessor::closeMic() { micArmed.store(false); micReady.store(false); mic.close(); micLevel.store(0); }
void HumProcessor::micCallback(const float* const* in, int nCh, int n) {   // the mic device's thread: mono mix into micRec while armed; the meter always
    if (in == nullptr || nCh <= 0 || n <= 0) return; float pk = 0;
    const bool take = micArmed.load() && recording.load() && src.load() != SrcHost; int len = micLen.load(); const int cap = (int) micRec.size();
    for (int i = 0; i < n; ++i) { float s = 0; for (int c = 0; c < nCh; ++c) if (in[c] != nullptr) s += in[c][i]; s /= (float) nCh; pk = juce::jmax(pk, std::abs(s)); if (take && len < cap) micRec[(size_t) len++] = s; }
    if (take) { micLen.store(len); micTakePk.store(juce::jmax(micTakePk.load(), pk)); if (len >= cap) { recording.store(false); recFull.store(true); triggerAsyncUpdate(); } }   // 90 s (the mic's buffer): like the host path
    micLevel.store(micLevel.load() * 0.7f + pk * 0.3f);
}
juce::String HumProcessor::listeningText() const {
    const int s = src.load(); juce::String where = "Listening";
    if (s == SrcMic) where = "Listening through your Mac's microphone"; else if (s == SrcHost && !juce::JUCEApplicationBase::isStandaloneApp()) where = "Listening to this track";
    return where + "... " + (recMode == Melody ? "sing or hum." : "make your beat.") + " Tap again when you're done.";
}
void HumProcessor::pollSources() {
    if (!recording.load()) return; const double el = recordedSeconds();
    if (src.load() == SrcNone) {   // AUTO, both running: the track wins the moment it carries sound; after half a second of digital silence the mic wins if it has any
        if (hostTakePk.load() > kSilence) { src.store(SrcHost); micArmed.store(false); status = listeningText(); changed(); }
        else if (el >= 0.5 && micTakePk.load() > kSilence) { src.store(SrcMic); status = listeningText(); changed(); } }
    const int s = src.load(); const float pk = s == SrcMic ? micTakePk.load() : s == SrcHost ? hostTakePk.load() : juce::jmax(hostTakePk.load(), micTakePk.load());
    if (!noSoundSaid && el >= 2.0 && pk <= kSilence) { noSoundSaid = true; status = noSoundText(s); changed(); }
    else if (noSoundSaid && pk > kSilence) { noSoundSaid = false; status = listeningText(); changed(); }
}
void HumProcessor::timerCallback() {
    if (recording.load() || countingIn.load()) { pollSources(); return; }
    if (micReady.load()) { if (juce::Time::getMillisecondCounter() - micLastUsedMs > 60000) { closeMic(); stopTimer(); } else if (getTimerInterval() != 1000) startTimer(1000); }   // a mic nobody used for a minute goes back to the system
    else stopTimer();
}
void HumProcessor::stopRecording() {
    if (countingIn.exchange(false)) { countLeft.store(0); recording.store(false); micArmed.store(false); status.clear(); changed(); return; }   // a tap during the count-in: never mind
    const bool wasFull = recFull.exchange(false); if (!recording.exchange(false) && !wasFull) return;
    const bool armed = micArmed.exchange(false); micLastUsedMs = juce::Time::getMillisecondCounter(); if (micReady.load()) startTimer(1000); else stopTimer();
    // whose take it is: AUTO undecided at the stop (a short or silent take) -> the one that had sound, else whoever had samples
    int s = src.load(); const float hp = hostTakePk.load(), mp = micTakePk.load();
    if (s == SrcNone) s = hp > kSilence ? SrcHost : (armed && mp > kSilence) ? SrcMic : (recLen.load() > 0 || !armed) ? SrcHost : SrcMic;
    src.store(s); const bool useMic = s == SrcMic; const double rate = useMic ? juce::jmax(1.0, micRate.load()) : sampleRate; const float pkTake = useMic ? mp : hp;
    const int md = recMode;   // (the mode the take was started in: switching the mode mid-take does not change what it is)
    const int len = useMic ? micLen.load() : recLen.load(); const auto& from = useMic ? micRec : rec; recCopy(md).assign(from.begin(), from.begin() + juce::jmin(len, (int) from.size())); recRate = rate; recClicks(md) = lastClickTimes();
    const bool tooShort = len < (int) (rate * 0.25), nothing = !tooShort && pkTake <= kSilence;
    if (teachingDrum >= 0) {   // TEACH GHOST YOUR SOUNDS: this recording is examples of one drum, not a beat
        const int d = teachingDrum; teachingDrum = -1; const int got = tooShort || nothing ? 0 : teachFromRecording(d);
        status = got > 0 ? juce::String("Got your ") + drumName(d) + " (" + juce::String(got) + (got == 1 ? " example)" : " examples)") : nothing ? noSoundText(s) : juce::String("Ghost heard no ") + drumName(d) + ". Try again, a little louder.";
        if (onTaught) onTaught(d, got); changed(); return; }
    if (tooShort) { status = "That was too short. Tap, then sing or beatbox for a few seconds."; changed(); return; }
    if (nothing) { status = noSoundText(s); changed(); return; }   // (digital silence the whole way: not "no notes", nothing arrived)
    if (wasFull) status = "90 seconds: Ghost stopped the take for you.";
    analyseAudio(recCopy(md), recRate, true, recClicks(md), md);
}
void HumProcessor::analyseAudio(const std::vector<float>& x, double sr, bool playAfter, std::vector<double> clicks, int asMode) {
    const int md = asMode < 0 ? mode() : asMode; const bool free = tempoFree(); const double bpm = workBpm(); melody::Options mo; mo.pitch = pitchMode(); mo.keyOverride = keyChoice(); const auto calCopy = cal;
    const int engine = oldEars() ? melody::EngineOld : melody::EngineNew; mo.engine = engine; mo.smooth = smoothing();
    analysing.store(true); status = "Ghost is listening..."; changed();
    auto work = [md, free, bpm, mo, calCopy, x, sr, clicks, engine] {
        Take T; if (md == Melody) { auto m2 = mo; m2.clickTimes = clicks.empty() ? nullptr : &clicks; T = melody::transcribe(x, sr, m2); }
        else { beat::Options bo; bo.engine = engine; bo.cal = calCopy.any() ? &calCopy : nullptr; bo.clickTimes = clicks.empty() ? nullptr : &clicks; T = beat::transcribe(x, sr, bo); }
        if (free) tempo::estimateFor(T); else { T.bpm = bpm; T.origin = 0; T.bpmKnown = true; T.bpmOffers.clear(); std::vector<double> on; std::vector<float> w; tempo::onsetsOf(T, on, w); T.swingHeard = tempo::swingHeard(on, T.bpm, 0); }
        return T; };
   #if GHOSTY_TEST_HOOKS
    if (!juce::MessageManager::getInstance()->isThisTheMessageThread() || testSync) { finishAnalysis(work(), playAfter); return; }
   #endif
    juce::Thread::launch([this, work, playAfter, alive = aliveFlag] {
        Take T = work(); std::lock_guard<std::mutex> g(analysedLock); if (!alive->load()) return;
        analysed = std::make_unique<Take>(std::move(T)); analysedPlay = playAfter; triggerAsyncUpdate(); });
}
void HumProcessor::handleAsyncUpdate() {
    std::unique_ptr<Take> T; bool pl = false; { std::lock_guard<std::mutex> g(analysedLock); T.swap(analysed); pl = analysedPlay; }
    if (T) finishAnalysis(*T, pl);
    if (recFull.load()) stopRecording();   // the 90 s cap stopped the recording on the audio thread
    const int d = dirty.exchange(0);
    if (d & kDirtyHear) { if (analysing.load()) rehearPending = true; else retranscribe(); } else if (d & kDirtyPitch) repitch(); else if (d & kDirtySeq) { rebuildSequence(); changed(); }
}
void HumProcessor::finishAnalysis(Take T, bool playAfter) {
    analysing.store(false); const size_t slot = (size_t) juce::jlimit(0, 1, T.mode);   // (the mode it was recorded in, even if the switch moved meanwhile)
    if (!takes[slot].empty()) undos[slot].push(takes[slot]); takes[slot] = std::move(T); sortTake(takes[slot]);
    if (take().empty()) status = take().mode == Melody ? "Ghost heard no notes. Sing or hum a little louder, close to the mic." : "Ghost heard no hits. Beatbox a little louder, close to the mic.";
    else { status = "Heard " + juce::String(take().count()) + (take().mode == Melody ? " notes" : " hits"); if (take().mode == Melody && take().key >= 0) status << " - " << keyText(take());
        status << " - " << (take().bpmKnown ? juce::String(juce::roundToInt(take().bpm)) + " BPM" : "about " + juce::String(juce::roundToInt(take().bpm)) + " BPM (pick one below)"); }
    rebuildSequence(); changed(); if (playAfter && !take().empty()) play();
    if (rehearPending) { rehearPending = false; retranscribe(); }   // (EARS / SMOOTH moved while Ghost was listening: hear it again with the latest)
}
void HumProcessor::retranscribe() { if (recCopy().empty() || analysing.load()) { rebuildSequence(); changed(); return; } analyseAudio(recCopy(), recRate, false, recClicks()); }
void HumProcessor::repitch() {
    if (take().mode != Melody || take().origNotes.empty()) { rebuildSequence(); changed(); return; }
    undo().push(take()); applyPitch(take(), pitchMode(), keyChoice()); status = juce::String("Pitch: ") + pitchName(pitchMode()); if (pitchMode() == PitchSnapKey) { const int k = keyChoice(); status << " (" << (k < 0 ? keyText(take()) : juce::String(keyName(k % 12)) + (k / 12 ? " minor" : " major")) << ")"; }
    edited();
}
void HumProcessor::parameterChanged(const juce::String& id) {
    if (id == "mode") stop();   // the other take comes up; nothing plays across the switch
    dirty.fetch_or(id == "ears" || (id == "smooth" && mode() == Melody) ? kDirtyHear : id == "pitch" || id == "key" ? kDirtyPitch : kDirtySeq); triggerAsyncUpdate();
}

// ---------------------------------------------------------------- TEACH GHOST YOUR SOUNDS
int HumProcessor::teachFromRecording(int drum) {
    if (recCopy(recMode).empty()) return 0;   // (build 4: the three clearest sounds, as both ears hear them: beat::lessons)
    auto ex = beat::lessons(recCopy(recMode), recRate, drum, recClicks(recMode).empty() ? nullptr : &recClicks(recMode)); if (ex.empty()) return 0;
    cal.clearDrum(drum); for (auto& x : ex) cal.ex.push_back(x);
    saveCalibration(); return (int) ex.size();
}
void HumProcessor::saveCalibration() const { calibrationFile().replaceWithText(juce::JSON::toString(cal.toVar())); }
void HumProcessor::saveSettings() const { auto* o = new juce::DynamicObject(); o->setProperty("ghostAnimation", ghostAnim); settingsFile().replaceWithText(juce::JSON::toString(juce::var(o))); }
void HumProcessor::loadSettings() { const auto f = settingsFile(); if (!f.existsAsFile()) return; const auto v = juce::JSON::parse(f.loadFileAsString()); if (v.isObject()) ghostAnim = (bool) v.getProperty("ghostAnimation", true); }
void HumProcessor::loadCalibration() { const auto f = calibrationFile(); if (f.existsAsFile()) cal = beat::Calibration::fromVar(juce::JSON::parse(f.loadFileAsString())); }

// ---------------------------------------------------------------- playback
void HumProcessor::rebuildSequence() {
    auto s = std::make_shared<const std::vector<sound::Ev>>(sound::sequence(take(), grid(), playOriginal()));
    std::shared_ptr<const std::vector<sound::Ev>> old; { const juce::SpinLock::ScopedLockType l(seqLock); old = std::move(seqOld); seqPending = s; }   // (old frees here, on this thread)
}
void HumProcessor::play() { if (take().empty()) return; rebuildSequence(); playing.store(true); changed(); }
void HumProcessor::stop() { if (playing.load()) { stopRequest.store(true); } }
void HumProcessor::auditionNote(int midi) { auditionNum.store(midi); auditionType.store(0); }
void HumProcessor::auditionDrum(int d) { auditionNum.store(d); auditionType.store(1); }

// ---------------------------------------------------------------- edits
void HumProcessor::clean() { if (take().empty()) return; undo().push(take()); if (take().mode == Melody) clean::melody(take()); else clean::drums(take(), true); status = "Cleaned (UNDO brings it back)"; edited(); }
bool HumProcessor::doUndo() { if (!undo().undo(take())) return false; status = "Undone"; edited(); return true; }
bool HumProcessor::doRedo() { if (!undo().redoIt(take())) return false; status = "Redone"; edited(); return true; }
void HumProcessor::chooseBpm(double bpm, bool keepOriginal) {
    undo().push(take());
    if (keepOriginal) { take().bpmKnown = false; setParam("quant", 0); status = "Playing exactly as you did it (" + juce::String(juce::roundToInt(take().bpm)) + " BPM in the MIDI file)"; }
    else { take().bpm = bpm; take().bpmKnown = true; if (quant() == QOff) setParam("quant", (float) Q16); status = juce::String(juce::roundToInt(bpm)) + " BPM"; }
    edited();
}
void HumProcessor::clearTake() { stop(); if (!take().empty()) undo().push(take()); take() = Take(); take().mode = mode(); status.clear(); edited(); }

// ---------------------------------------------------------------- export
juce::File HumProcessor::writeMidiFile() {
    if (!ghosty::exportUnlocked(license) || take().empty()) return {};
    const auto tag = ghosty::tempomul::fileTag(tempoMul.load() != ghosty::tempomul::Normal && !tempoFree() ? tempoMul.load() : (int) ghosty::tempomul::Normal);
    auto f = files::exportDir("MIDI").getChildFile(files::takeName(take()) + tag + (playOriginal() ? " original" : "") + ".mid").getNonexistentSibling(true);
    auto mf = files::midiOf(take(), grid(), playOriginal(), files::takeName(take()));
    ghosty::tempomul::scaleMidiToHost(mf, midiScale());   // TEMPO ½x / 2x: at the set (DAW) tempo, every time scaled (lands as heard)
    juce::MemoryOutputStream ms; if (!mf.writeTo(ms)) return {};
    return f.replaceWithData(ms.getData(), ms.getDataSize()) ? f : juce::File();   // GATE: via-caller (DRAG MIDI asks exportAllowed first; back-stop above)
}
void HumProcessor::setTempoMul(int code) {
    tempoMul.store(ghosty::tempomul::clampCode(code));
    if (!tempoFree()) for (auto& T : takes) if (!T.empty() && T.bpmKnown) T.bpm = workBpm();   // the take's grid follows (its notes stay where you sang them)
    rebuildSequence(); changed();
}
juce::File HumProcessor::writeWavFile() {
    if (!ghosty::exportUnlocked(license) || take().empty()) return {};
    const auto tag = ghosty::tempomul::fileTag(tempoMul.load() != ghosty::tempomul::Normal && !tempoFree() ? tempoMul.load() : (int) ghosty::tempomul::Normal);
    auto f = files::exportDir("Audio").getChildFile(files::takeName(take()) + tag + (playOriginal() ? " original" : "") + ".wav").getNonexistentSibling(true);
    return files::writeWav(f, take(), grid(), playOriginal(), voice()) ? f : juce::File();   // GATE: via-caller (DRAG WAV asks exportAllowed first; back-stop above)
}

// ---------------------------------------------------------------- state
void HumProcessor::getStateInformation(juce::MemoryBlock& mb) {
    juce::XmlElement root("GhostHumState"); root.setAttribute("version", 2); root.setAttribute("theme", ghosty::themes::toString(theme)); root.setAttribute("anim", ghostAnim ? 1 : 0); if (tempoMul.load() != ghosty::tempomul::Normal) root.setAttribute(ghosty::tempomul::stateKey, tempoMul.load());
    root.addChildElement(apvts.copyState().createXml().release());
    root.setAttribute("melody", juce::JSON::toString(toVar(takes[0]), true)); root.setAttribute("beat", juce::JSON::toString(toVar(takes[1]), true));
    root.setAttribute("cal", juce::JSON::toString(cal.toVar(), true));
    copyXmlToBinary(root, mb);
}
void HumProcessor::setStateInformation(const void* d, int n) {
    auto xml = getXmlFromBinary(d, n); if (!xml || !xml->hasTagName("GhostHumState")) return;
    if (auto* s = xml->getChildByName(apvts.state.getType())) apvts.replaceState(juce::ValueTree::fromXml(*s));
    theme = ghosty::themes::fromString(xml->getStringAttribute("theme"), ghosty::themes::Theme::Purple); if (xml->hasAttribute("anim")) ghostAnim = xml->getIntAttribute("anim", 1) != 0; tempoMul.store(ghosty::tempomul::clampCode(xml->getIntAttribute(ghosty::tempomul::stateKey, (int) ghosty::tempomul::Normal)));
    takes[0] = takeFromVar(juce::JSON::parse(xml->getStringAttribute("melody"))); takes[1] = takeFromVar(juce::JSON::parse(xml->getStringAttribute("beat")));
    takes[0].mode = Melody; takes[1].mode = Beatbox; sortTake(takes[0]); sortTake(takes[1]);
    if (xml->hasAttribute("cal")) { auto c = beat::Calibration::fromVar(juce::JSON::parse(xml->getStringAttribute("cal"))); if (c.any()) cal = c; }
    undos[0].clear(); undos[1].clear(); dirty.store(0); rebuildSequence(); status = take().empty() ? juce::String() : "Heard " + juce::String(take().count()) + (take().mode == Melody ? " notes" : " hits"); changed();
}

juce::AudioProcessorEditor* HumProcessor::createEditor() { return new HumEditor(*this); }

} // namespace hum

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new hum::HumProcessor(); }
