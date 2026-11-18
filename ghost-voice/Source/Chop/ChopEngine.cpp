// Ghost Voice - AGPL-3.0-only (see LICENSE, NOTICE.md).
#include "ChopEngine.h"
#include "VoiceEngine.h"
#include "src/PluginProcessor.h"   // INTERSECT (third_party/intersect/src, untouched)
#include "Analysis.h"              // Flayr Chop's analysis (third_party/flayr-chop, untouched)
#include <algorithm>
#include <cmath>
#include <set>

namespace gv::chop
{
namespace an = ::chop::analysis;

juce::String Detected::keyName() const
{
    static const char* n[] { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    return juce::String (n[juce::jlimit (0, 11, key)]) + (minor ? " minor" : " major");
}

juce::String Detected::summary() const
{
    if (! ready) return {};
    juce::String s;
    if (bpm > 0) s << juce::String (bpm, bpm == std::round (bpm) ? 0 : 1) << " BPM" << (bpmConfidence < 0.35f ? " ?" : "");
    if (keyConfidence > 0) s << (s.isEmpty() ? "" : "   ") << keyName() << (keyConfidence < 0.35f ? " ?" : "");
    return s;
}

juce::String autoModeName (AutoMode m)
{
    switch (m)
    {
        case AutoMode::random:   return "RANDOM";
        case AutoMode::hits:     return "HITS";
        case AutoMode::beats:    return "EVERY BEAT";
        case AutoMode::twoBeats: return "EVERY 2 BEATS";
        case AutoMode::bar:      return "EVERY BAR";
        case AutoMode::even:     return "EVEN";
        case AutoMode::phrases:  return "PHRASES";
        case AutoMode::words:    return "WORDS";
        default: break;
    }
    return {};
}

juce::String autoModeTip (AutoMode m)
{
    switch (m)
    {
        case AutoMode::random:   return "Random chops on the beat, 1 or 2 beats long, snapped to the hits. Press again for a new set.";
        case AutoMode::hits:     return "Chops on the strongest musical hits.";
        case AutoMode::beats:    return "A chop on every beat, snapped to the nearest hit.";
        case AutoMode::twoBeats: return "A chop every 2 beats, snapped to the nearest hit.";
        case AutoMode::bar:      return "A chop on every bar, snapped to the nearest hit.";
        case AutoMode::even:     return "Equal-length chops.";
        case AutoMode::phrases:  return "A chop for every phrase of a vocal (it cuts at the silences).";
        case AutoMode::words:    return "A chop for every word, named on its key. Best on clear singing or rap.";
        default: break;
    }
    return {};
}

juce::File ChopEngine::folder() { return workDir().getChildFile ("chop"); }

// ================================================================================================ life
ChopEngine::ChopEngine() : engine (std::make_unique<IntersectProcessor>())
{
    engine->setRateAndBufferSizeDetails (sr, maxBlock);
    keysIn.reset (sr);
    startTimerHz (20);
}

ChopEngine::~ChopEngine()
{
    stopTimer();
    alive->store (false);
    pool.removeAllJobs (true, 4000);
    engine.reset();
}

// ================================================================================================ audio
void ChopEngine::prepare (double sampleRate, int blockSize)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    maxBlock = juce::jmax (32, blockSize);
    scratch.setSize (2, maxBlock, false, true, true);
    engineMidi.ensureSize (2048);
    keysIn.reset (sr);
    engine->setRateAndBufferSizeDetails (sr, maxBlock);
    engine->prepareToPlay (sr, maxBlock);
}

void ChopEngine::process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& hostMidi, juce::AudioPlayHead* ph)
{
    const int n = out.getNumSamples(), och = out.getNumChannels();
    if (n > scratch.getNumSamples()) scratch.setSize (2, n, false, false, true);   // a host that sends more than it said
    engineMidi.clear();
    engineMidi.addEvents (hostMidi, 0, n, 0);
    { juce::MidiBuffer keys; keysIn.removeNextBlockOfMessages (keys, n); engineMidi.addEvents (keys, 0, n, 0); }
    for (const auto m : engineMidi)
    {
        const auto msg = m.getMessage();
        if (! msg.isNoteOnOrOff()) continue;
        const int k = msg.getNoteNumber(); const auto bit = (juce::uint64) 1 << (k & 63);
        if (msg.isNoteOn()) sounding[k >> 6].fetch_or (bit); else sounding[k >> 6].fetch_and (~bit);
    }
    // INTERSECT plays the slices
    juce::AudioBuffer<float> sub (scratch.getArrayOfWritePointers(), 2, n);
    sub.clear();
    engine->setPlayHead (ph);
    engine->processBlock (sub, engineMidi);
    // the clip's own transport (SPACE)
    if (playing.load())
        if (auto c = clip(); c != nullptr && c->frames() > 0)
        {
            const double step = c->rate / sr;
            double pos = playPos.load();
            const bool lp = looping.load(); const double a = (double) loopA.load(), b = (double) loopB.load();
            const int cc = c->audio.getNumChannels(); const auto len = (double) c->frames();
            for (int i = 0; i < n; ++i)
            {
                if (lp && b > a + 1 && pos >= b) pos = a + std::fmod (pos - a, b - a);
                if (pos >= len - 1) { playing = false; break; }
                const auto k = (int) pos; const float t = (float) (pos - k);
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float* s = c->audio.getReadPointer (juce::jmin (ch, cc - 1));
                    sub.addSample (ch, i, s[k] * (1 - t) + s[k + 1] * t);
                }
                pos += step;
            }
            playPos = pos;
        }
    if (och >= 2) { out.copyFrom (0, 0, sub, 0, 0, n); out.copyFrom (1, 0, sub, 1, 0, n); for (int ch = 2; ch < och; ++ch) out.clear (ch, 0, n); }
    else if (och == 1) { out.copyFrom (0, 0, sub, 0, 0, n); out.addFrom (0, 0, sub, 1, 0, n); out.applyGain (0, 0, n, 0.5f); }
}

void ChopEngine::noteOn (int note, float velocity)
{
    auto m = juce::MidiMessage::noteOn (1, juce::jlimit (0, 127, note), juce::jlimit (0.05f, 1.0f, velocity));
    m.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
    keysIn.addMessageToQueue (m);
}

void ChopEngine::noteOff (int note)
{
    auto m = juce::MidiMessage::noteOff (1, juce::jlimit (0, 127, note));
    m.setTimeStamp (juce::Time::getMillisecondCounterHiRes() * 0.001);
    keysIn.addMessageToQueue (m);
}

bool ChopEngine::noteSounding (int note) const
{
    if (note < 0 || note > 127) return false;
    return (sounding[note >> 6].load() >> (note & 63)) & 1;
}

void ChopEngine::play (juce::int64 from, Region loopRegion, bool loop)
{
    auto c = clip(); if (c == nullptr || c->frames() < 2) return;
    loopA = juce::jlimit ((juce::int64) 0, c->frames() - 1, loopRegion.a);
    loopB = juce::jlimit ((juce::int64) 0, c->frames(), loopRegion.b);
    looping = loop && loopRegion.length() > 64;
    if (looping.load() && (from < loopA.load() || from >= loopB.load())) from = loopA.load();
    playPos = (double) juce::jlimit ((juce::int64) 0, c->frames() - 2, from);
    playing = true;
}

void ChopEngine::stop() { playing = false; }

// ================================================================================================ the clip
bool ChopEngine::loadFile (const juce::File& f, juce::String& why)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr) { why = "Can't read " + f.getFileName() + " (WAV, AIFF, FLAC, OGG or MP3)"; return false; }
    const auto maxLen = (juce::int64) (r->sampleRate * 60.0 * 10.0);   // 10 minutes
    if (r->lengthInSamples > maxLen) { why = f.getFileName() + " is longer than 10 minutes"; return false; }
    if (r->lengthInSamples < 64) { why = f.getFileName() + " is empty"; return false; }
    juce::AudioBuffer<float> b ((int) juce::jlimit (1u, 2u, r->numChannels), (int) r->lengthInSamples);
    r->read (&b, 0, b.getNumSamples(), 0, true, b.getNumChannels() > 1);
    setClip (std::move (b), r->sampleRate, f.getFileNameWithoutExtension());
    return true;
}

void ChopEngine::setClip (juce::AudioBuffer<float> audio, double rate, const juce::String& name)
{
    pushUndo();
    auto c = std::make_shared<Clip>(); c->audio = std::move (audio); c->rate = rate > 0 ? rate : 44100.0; c->name = name;
    chops.clear(); labels.clear(); keys = KeysMode::whole;
    install (c, true);
    message = "Loaded " + name;
    changed();
}

void ChopEngine::install (ClipPtr c, bool writeFile)
{
    stop();
    std::atomic_store (&current, c);
    if (writeFile && c != nullptr)
    {
        auto f = folder().getChildFile ("clip_" + juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S") + "_" + juce::String (++loadSerial) + ".wav");
        f.getParentDirectory().createDirectory();
        std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
        if (os != nullptr)
        {
            auto w = juce::WavAudioFormat().createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (c->rate)
                                                                  .withNumChannels (c->audio.getNumChannels()).withBitsPerSample (24));
            if (w != nullptr) { w->writeFromAudioSampleBuffer (c->audio, 0, c->audio.getNumSamples()); w.reset(); currentFile = f; }
        }
    }
    if (c != nullptr && currentFile.existsAsFile())
    {
        engine->loadFileAsync (currentFile);
        pendingRebuild = true;
    }
    analyse();
}

juce::File ChopEngine::writeRegion (Region r, const juce::String& stem) const
{
    auto c = clip(); if (c == nullptr) return {};
    if (r.empty()) r = { 0, c->frames() };
    r.a = juce::jlimit ((juce::int64) 0, c->frames(), r.a); r.b = juce::jlimit (r.a, c->frames(), r.b);
    if (r.length() < 64) return {};
    auto f = folder().getChildFile (stem + "_" + juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S") + ".wav");
    f.getParentDirectory().createDirectory(); f.deleteFile();
    std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
    if (os == nullptr) return {};
    auto w = juce::WavAudioFormat().createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (c->rate)
                                                          .withNumChannels (c->audio.getNumChannels()).withBitsPerSample (24));
    if (w == nullptr) return {};
    w->writeFromAudioSampleBuffer (c->audio, (int) r.a, (int) r.length());
    return f;
}

// ================================================================================================ edits
namespace
{
juce::AudioBuffer<float> slice (const Clip& c, Region r)
{
    juce::AudioBuffer<float> b (c.audio.getNumChannels(), (int) r.length());
    for (int ch = 0; ch < b.getNumChannels(); ++ch) b.copyFrom (ch, 0, c.audio, ch, (int) r.a, (int) r.length());
    return b;
}
// a buffer at the clip's rate and channel count (linear resampling when the rates differ)
juce::AudioBuffer<float> conform (const juce::AudioBuffer<float>& in, double inRate, int channels, double rate)
{
    const double ratio = inRate / rate;
    const int len = (int) std::floor ((double) in.getNumSamples() / ratio);
    juce::AudioBuffer<float> out (channels, juce::jmax (0, len));
    for (int ch = 0; ch < channels; ++ch)
    {
        const float* s = in.getReadPointer (juce::jmin (ch, in.getNumChannels() - 1));
        float* d = out.getWritePointer (ch);
        if (std::abs (ratio - 1.0) < 1.0e-9) { std::copy (s, s + len, d); continue; }
        for (int i = 0; i < len; ++i)
        {
            const double p = i * ratio; const int k = (int) p; const float t = (float) (p - k);
            d[i] = k + 1 < in.getNumSamples() ? s[k] * (1 - t) + s[k + 1] * t : s[juce::jmin (k, in.getNumSamples() - 1)];
        }
    }
    return out;
}
// clip with [r.a, r.b) replaced by `mid`
std::shared_ptr<Clip> splice (const Clip& c, Region r, const juce::AudioBuffer<float>& mid)
{
    auto n = std::make_shared<Clip>(); n->rate = c.rate; n->name = c.name;
    const int ch = c.audio.getNumChannels();
    const int len = (int) (c.frames() - r.length() + mid.getNumSamples());
    n->audio.setSize (ch, juce::jmax (0, len));
    for (int k = 0; k < ch; ++k)
    {
        n->audio.copyFrom (k, 0, c.audio, k, 0, (int) r.a);
        if (mid.getNumSamples() > 0) n->audio.copyFrom (k, (int) r.a, mid, juce::jmin (k, mid.getNumChannels() - 1), 0, mid.getNumSamples());
        n->audio.copyFrom (k, (int) r.a + mid.getNumSamples(), c.audio, k, (int) r.b, (int) (c.frames() - r.b));
    }
    return n;
}
Region clampTo (Region r, juce::int64 frames) { r.a = juce::jlimit ((juce::int64) 0, frames, r.a); r.b = juce::jlimit (r.a, frames, r.b); return r; }
}

void ChopEngine::pushUndo()
{
    if (clip() == nullptr) return;
    undoStack.push_back (snap());
    if (undoStack.size() > kUndoSteps) undoStack.erase (undoStack.begin());
    redoStack.clear();
}

void ChopEngine::restore (const Snapshot& s)
{
    chops = s.chops; keys = s.keys; labels = s.labels; currentFile = s.file;
    if (s.file.existsAsFile()) install (s.clip, false);
    else install (s.clip, true);
    changed();
}

bool ChopEngine::undo()
{
    if (undoStack.empty()) { message = "Nothing to undo"; changed(); return false; }
    redoStack.push_back (snap()); auto s = undoStack.back(); undoStack.pop_back();
    restore (s); message = "Undo"; changed();
    return true;
}

bool ChopEngine::redo()
{
    if (redoStack.empty()) { message = "Nothing to redo"; changed(); return false; }
    undoStack.push_back (snap()); auto s = redoStack.back(); redoStack.pop_back();
    restore (s); message = "Redo"; changed();
    return true;
}

bool ChopEngine::copy (Region r)
{
    auto c = clip(); if (c == nullptr) return false;
    r = clampTo (r, c->frames()); if (r.length() < 64) { message = "Select part of the wave first (drag with SELECT)"; changed(); return false; }
    clipboard = slice (*c, r); clipboardRate = c->rate;
    message = "Copied " + juce::String ((double) r.length() / c->rate, 2) + " s"; changed();
    return true;
}

bool ChopEngine::cut (Region r)
{
    if (! copy (r)) return false;
    auto c = clip(); r = clampTo (r, c->frames());
    if (r.length() >= c->frames() - 64) { message = "That's the whole clip: nothing would be left"; changed(); return false; }
    pushUndo(); chops.clear(); labels.clear(); keys = KeysMode::whole;
    install (splice (*c, r, {}), true);
    message = "Cut " + juce::String ((double) r.length() / c->rate, 2) + " s"; changed();
    return true;
}

bool ChopEngine::erase (Region r)
{
    auto c = clip(); if (c == nullptr) return false;
    r = clampTo (r, c->frames());
    if (r.length() < 64) { message = "Select part of the wave first (drag with SELECT)"; changed(); return false; }
    if (r.length() >= c->frames() - 64) { message = "That's the whole clip: nothing would be left"; changed(); return false; }
    pushUndo(); chops.clear(); labels.clear(); keys = KeysMode::whole;
    install (splice (*c, r, {}), true);
    message = "Deleted " + juce::String ((double) r.length() / c->rate, 2) + " s"; changed();
    return true;
}

bool ChopEngine::paste (juce::int64 at, Region replace)
{
    auto c = clip(); if (c == nullptr || ! hasClipboard()) { message = "Nothing copied yet (Cmd+C)"; changed(); return false; }
    Region r = replace.empty() ? Region { at, at } : replace;
    r = clampTo (r, c->frames());
    const auto mid = conform (clipboard, clipboardRate, c->audio.getNumChannels(), c->rate);
    pushUndo(); chops.clear(); labels.clear(); keys = KeysMode::whole;
    install (splice (*c, r, mid), true);
    message = "Pasted " + juce::String ((double) mid.getNumSamples() / c->rate, 2) + " s"; changed();
    return true;
}

bool ChopEngine::trim (Region r)
{
    auto c = clip(); if (c == nullptr) return false;
    r = clampTo (r, c->frames());
    if (r.length() < 64 || r.length() == c->frames()) { message = "Select the part to keep first"; changed(); return false; }
    pushUndo(); chops.clear(); labels.clear(); keys = KeysMode::whole;
    auto n = std::make_shared<Clip>(); n->rate = c->rate; n->name = c->name; n->audio = slice (*c, r);
    install (n, true);
    message = "Trimmed to " + juce::String ((double) r.length() / c->rate, 2) + " s"; changed();
    return true;
}

bool ChopEngine::duplicate (Region r)
{
    auto c = clip(); if (c == nullptr) return false;
    r = clampTo (r, c->frames());
    if (r.length() < 64) { message = "Select part of the wave first (drag with SELECT)"; changed(); return false; }
    pushUndo(); chops.clear(); labels.clear(); keys = KeysMode::whole;
    install (splice (*c, { r.b, r.b }, slice (*c, r)), true);
    message = "Duplicated " + juce::String ((double) r.length() / c->rate, 2) + " s"; changed();
    return true;
}

bool ChopEngine::replaceRegion (Region r, const juce::AudioBuffer<float>& with, double withRate)
{
    auto c = clip(); if (c == nullptr || with.getNumSamples() < 64) return false;
    r = clampTo (r, c->frames()); if (r.empty()) r = { 0, c->frames() };
    const auto mid = conform (with, withRate, c->audio.getNumChannels(), c->rate);
    const bool sameLength = std::abs ((double) mid.getNumSamples() - (double) r.length()) < c->rate * 0.02;
    pushUndo();
    if (! sameLength) { chops.clear(); labels.clear(); keys = KeysMode::whole; }
    install (splice (*c, r, mid), true);
    changed();
    return true;
}

// ================================================================================================ chops + keys
juce::int64 ChopEngine::toEngine (juce::int64 f) const
{
    const auto& s = engine->getUiSliceSnapshot();
    const auto cf = frames();
    if (cf <= 0 || s.sampleNumFrames <= 0) return f;
    return (juce::int64) std::llround ((double) f * (double) s.sampleNumFrames / (double) cf);
}

juce::int64 ChopEngine::toClip (juce::int64 f) const
{
    const auto& s = engine->getUiSliceSnapshot();
    const auto cf = frames();
    if (cf <= 0 || s.sampleNumFrames <= 0) return f;
    return (juce::int64) std::llround ((double) f * (double) cf / (double) s.sampleNumFrames);
}

bool ChopEngine::engineLoaded() const
{
    const auto& s = engine->getUiSliceSnapshot();
    return currentFile != juce::File() && s.sampleLoaded && s.sampleNumFrames > 0
        && s.sampleFileName.toString() == currentFile.getFileName();
}

std::vector<SliceView> ChopEngine::slices() const
{
    std::vector<SliceView> v;
    if (! engineLoaded()) return v;
    const auto& s = engine->getUiSliceSnapshot();
    for (int i = 0; i < s.numSlices; ++i)
    {
        const auto& sl = s.slices[(size_t) i];
        if (! sl.active || sl.parked) continue;
        v.push_back ({ { toClip (sl.startSample), toClip (sl.endSample) }, sl.midiNote, sl.highNote, i });
    }
    return v;
}

void ChopEngine::rebuildSlices()
{
    if (! engineLoaded()) { pendingRebuild = true; return; }
    // the last rebuild's commands are still in INTERSECT's queue: wait until it has played a block (the timer retries)
    if (rebuildPushed && engine->getUiSliceSnapshotVersion() == pushedVersion) { pendingRebuild = true; return; }
    pendingRebuild = false;
    const auto& s = engine->getUiSliceSnapshot();
    using P = IntersectProcessor;
    auto cmd = [this] (P::CommandType t, int a = 0, int b = 0, float f = 0, int idx = -1)
    { P::Command c; c.type = t; c.intParam1 = a; c.intParam2 = b; c.floatParam1 = f; c.sliceIdx = idx; engine->pushCommand (c); };
    cmd (P::CmdPanic);
    for (int i = s.numSlices - 1; i >= 0; --i) cmd (P::CmdDeleteSlice, i);
    cmd (P::CmdSetRootNote, rootNote);
    const auto total = (juce::int64) s.sampleNumFrames;
    if (keys == KeysMode::whole || chops.empty())
    {   // the whole clip across the keyboard: C3 (60) plays it as it is; stretched, with the formants kept, like a voice
        cmd (P::CmdCreateSlice, 0, (int) total);
        cmd (P::CmdSetSliceParam, P::FieldMidiNote, 0, 0.0f, 0);
        cmd (P::CmdSetSliceParam, P::FieldHighNote, 0, 127.0f, 0);
        cmd (P::CmdSetSliceParam, P::FieldSliceRootNote, 0, 60.0f, 0);
        cmd (P::CmdSetSliceParam, P::FieldAlgorithm, 0, 1.0f, 0);
        cmd (P::CmdSetSliceParam, P::FieldFormantComp, 0, 1.0f, 0);
    }
    else
    {   // one chop per key from C1, in order
        int made = 0;
        for (const auto& r : chops)
        {
            if (made >= 92) break;   // up to G7
            const auto a = juce::jlimit ((juce::int64) 0, total, toEngine (r.a)), b = juce::jlimit (a, total, toEngine (r.b));
            if (b - a < 64) continue;
            cmd (P::CmdCreateSlice, (int) a, (int) b);
            ++made;
        }
    }
    cmd (P::CmdSelectSlice, 0);
    rebuildPushed = true; pushedVersion = engine->getUiSliceSnapshotVersion();
}

void ChopEngine::setKeys (KeysMode k)
{
    if (k == KeysMode::chops && chops.empty()) { keys = k; autoChop(); return; }
    keys = k;
    rebuildSlices();
    message = k == KeysMode::whole ? "WHOLE: the clip plays across the keyboard, C3 = as it is" : "CHOPS: one chop per key from C1";
    changed();
}

void ChopEngine::clearChops()
{
    pushUndo(); chops.clear(); labels.clear(); keys = KeysMode::whole; rebuildSlices(); message = "Chops cleared"; changed();
}

void ChopEngine::splitAt (juce::int64 f)
{
    auto c = clip(); if (c == nullptr) return;
    f = juce::jlimit ((juce::int64) 1, c->frames() - 1, f);
    pushUndo();
    if (chops.empty()) chops.push_back ({ 0, c->frames() });
    bool done = false;
    for (size_t i = 0; i < chops.size() && ! done; ++i)
        if (f > chops[i].a + 64 && f < chops[i].b - 64)
        {
            const Region left { chops[i].a, f }, right { f, chops[i].b };
            chops[i] = left; chops.insert (chops.begin() + (long) i + 1, right); done = true;
            if (i < labels.size()) labels.insert (labels.begin() + (long) i + 1, juce::String());
        }
    if (! done) { chops.push_back ({ f, juce::jmin (c->frames(), f + (juce::int64) (c->rate * 0.5)) }); }
    keys = KeysMode::chops; rebuildSlices();
    message = juce::String ((int) chops.size()) + " chops"; changed();
}

bool ChopEngine::removeChopAt (juce::int64 f)
{
    for (size_t i = chops.size(); i-- > 0;)
        if (f >= chops[i].a && f < chops[i].b)
        {
            pushUndo(); chops.erase (chops.begin() + (long) i);
            if (i < labels.size()) labels.erase (labels.begin() + (long) i);
            if (chops.empty()) keys = KeysMode::whole;
            rebuildSlices(); message = chops.empty() ? "No chops left: the whole clip is on the keys" : juce::String ((int) chops.size()) + " chops"; changed();
            return true;
        }
    return false;
}

bool ChopEngine::autoChop()
{
    auto c = clip();
    if (c == nullptr) { message = "Drag a WAV in first"; changed(); return false; }
    const auto d = detected();
    if (! d.ready) { pendingAutoChop = true; message = "Listening to the clip..."; changed(); return false; }
    pendingAutoChop = false;
    if (usesTool (autoMode))
    {   // PHRASES / WORDS: a separate process on the clip's own WAV (build 9)
        if (! tools.ready (toolKind()))
        { message = autoModeName (autoMode) + " needs its finder installed first: press INSTALL"; changed(); return false; }
        if (tools.busy()) { message = "Already listening..."; changed(); return false; }
        const int serial = ++toolSerial; const auto mode = autoMode; const auto file = currentFile; auto token = alive;
        const bool started = tools.run (toolKind(), file, [this, serial, mode, file, token] (bool ok, std::vector<ToolChop> found)
        {
            if (! token->load() || serial != toolSerial || file != currentFile) return;
            auto c = clip();
            if (! ok || c == nullptr) { message = autoModeName (mode) + " didn't work: " + tools.lastError; changed(); return; }
            std::vector<Region> out; std::vector<juce::String> words;
            for (auto& t : found)
            {
                const auto a = juce::jlimit ((juce::int64) 0, c->frames(), (juce::int64) std::llround (t.a * c->rate));
                const auto b = juce::jlimit (a, c->frames(), (juce::int64) std::llround (t.b * c->rate));
                if (b - a < 64) continue;
                out.push_back ({ a, b }); words.push_back (t.text);
            }
            if (out.empty()) { message = mode == AutoMode::words ? "No words found in this clip" : "No phrases found (the clip has no silences to cut at)"; changed(); return; }
            pushUndo();
            chops = out; labels = words; keys = KeysMode::chops;
            rebuildSlices();
            message = autoModeName (mode) + ": " + juce::String ((int) chops.size()) + (mode == AutoMode::words ? " words" : " phrases") + " on the keys from C1";
            changed();
        });
        message = started ? (autoMode == AutoMode::words ? juce::String ("Finding the words...") : juce::String ("Finding the phrases...")) : "Couldn't start: " + tools.lastError;
        changed();
        return started;
    }
    // mono at the clip's rate, for the cue finder
    std::vector<float> mono ((size_t) c->frames(), 0.0f);
    for (int ch = 0; ch < c->audio.getNumChannels(); ++ch)
    {
        const float* s = c->audio.getReadPointer (ch);
        for (size_t i = 0; i < mono.size(); ++i) mono[i] += s[i] / (float) c->audio.getNumChannels();
    }
    an::Result r; r.bpm = d.bpm; r.firstBeatSec = d.firstBeat; r.bpmConfidence = d.bpmConfidence; r.key = d.key; r.minor = d.minor;
    r.keyConfidence = d.keyConfidence; r.onsetsSec = d.onsets; r.onsetStrength = d.strength;
    const double len = c->seconds();
    const int count = juce::jlimit (1, 64, chopCount);
    std::vector<Region> out;
    auto toFrame = [&] (double t) { return juce::jlimit ((juce::int64) 0, c->frames(), (juce::int64) std::llround (t * c->rate)); };
    if (autoMode == AutoMode::random)
    {   // random whole beats (never the same one twice), each 1 or 2 beats long, snapped to the nearest hit
        const double beat = 60.0 / juce::jmax (20.0, d.bpm > 0 ? d.bpm : 120.0);
        const int beats = juce::jmax (1, (int) ((len - d.firstBeat) / beat) - 1);
        std::set<int> used;
        for (int i = 0; i < count && (int) used.size() < beats; ++i)
        {
            int b = rng.nextInt (beats);
            for (int tries = 0; used.count (b) != 0 && tries < 64; ++tries) b = rng.nextInt (beats);
            if (used.count (b) != 0) break;
            used.insert (b);
            const double t0 = an::snapToTransient (r, juce::jmax (0.0, d.firstBeat + b * beat), 0.03);
            const double t1 = juce::jmin (len, t0 + beat * (rng.nextBool() ? 1.0 : 2.0));
            if (t1 - t0 > 0.02) out.push_back ({ toFrame (t0), toFrame (t1) });
        }
    }
    else
    {
        const auto mode = autoMode == AutoMode::hits ? an::ChopMode::Auto : autoMode == AutoMode::even ? an::ChopMode::Even : an::ChopMode::Beats;
        const double per = autoMode == AutoMode::twoBeats ? 2.0 : autoMode == AutoMode::bar ? 4.0 : 1.0;
        auto cues = an::chop (r, mono.data(), (int) mono.size(), c->rate, count, mode, per);
        std::sort (cues.begin(), cues.end());
        for (size_t i = 0; i < cues.size(); ++i)
        {
            const double t0 = cues[i], t1 = i + 1 < cues.size() ? cues[i + 1] : len;
            if (t1 - t0 > 0.02) out.push_back ({ toFrame (t0), toFrame (t1) });
        }
    }
    if (out.empty()) { message = "Couldn't find anything to chop in this clip"; changed(); return false; }
    pushUndo();
    chops = out; labels.clear(); keys = KeysMode::chops;
    rebuildSlices();
    message = autoModeName (autoMode) + ": " + juce::String ((int) chops.size()) + " chops on the keys from C1";
    changed();
    return true;
}

bool ChopEngine::installTool()
{
    auto token = alive; const auto k = toolKind();
    const bool ok = tools.install (k, [this, token, k] (bool done)
    {
        if (! token->load()) return;
        message = done ? (k == Tools::Kind::words ? juce::String ("The word finder is installed: press AUTO CHOP") : juce::String ("The phrase finder is installed: press AUTO CHOP"))
                       : "The install didn't finish: " + tools.lastError;
        changed();
    });
    if (! ok) message = "Couldn't start the install: " + tools.lastError;
    changed();
    return ok;
}

// ================================================================================================ one chop's sound (INTERSECT's own per-slice settings)
namespace
{
using P = IntersectProcessor;
struct FieldInfo { int field; uint64_t lock; };
float sliceValue (const Slice& s, int f)
{
    switch (f)
    {
        case P::FieldPitch: return s.pitchSemitones;            case P::FieldCentsDetune: return s.centsDetune;
        case P::FieldVolume: return s.volume;                   case P::FieldAttack: return s.attackSec;
        case P::FieldRelease: return s.releaseSec;              case P::FieldReverse: return s.reverse ? 1.0f : 0.0f;
        case P::FieldLoop: return (float) s.loopMode;           case P::FieldOneShot: return s.oneShot ? 1.0f : 0.0f;
        case P::FieldAlgorithm: return (float) s.algorithm;     case P::FieldFormantComp: return s.formantComp ? 1.0f : 0.0f;
        case P::FieldFilterEnabled: return s.filterEnabled ? 1.0f : 0.0f;
        case P::FieldFilterType: return (float) s.filterType;   case P::FieldFilterCutoff: return s.filterCutoff;
        case P::FieldFilterReso: return s.filterReso;
        default: break;
    }
    return 0;
}
uint64_t lockOf (int f)
{
    switch (f)
    {
        case P::FieldPitch: return kLockPitch;                  case P::FieldCentsDetune: return kLockCentsDetune;
        case P::FieldVolume: return kLockVolume;                case P::FieldAttack: return kLockAttack;
        case P::FieldRelease: return kLockRelease;              case P::FieldReverse: return kLockReverse;
        case P::FieldLoop: return kLockLoop;                    case P::FieldOneShot: return kLockOneShot;
        case P::FieldAlgorithm: return kLockAlgorithm;          case P::FieldFormantComp: return kLockFormantComp;
        case P::FieldFilterEnabled: return kLockFilterEnabled;  case P::FieldFilterType: return kLockFilterType;
        case P::FieldFilterCutoff: return kLockFilterCutoff;    case P::FieldFilterReso: return kLockFilterReso;
        default: break;
    }
    return 0;
}
}

float ChopEngine::sliceParam (int idx, int field) const
{
    const auto& s = engine->getUiSliceSnapshot();
    if (idx < 0 || idx >= s.numSlices) return 0;
    const auto& sl = s.slices[(size_t) idx];
    if ((sl.lockMask & lockOf (field)) != 0) return sliceValue (sl, field);
    const float v = s.sampleParamsFor (sl.sampleId).getField (field);   // not set on the chop: the clip's own value
    return field == P::FieldAttack || field == P::FieldRelease ? v / 1000.0f : v;   // (the clip's own value is in ms; a chop's in seconds)
}

void ChopEngine::setSliceParam (int idx, int field, float value)
{
    using C = IntersectProcessor;
    C::Command c; c.type = C::CmdSetSliceParam; c.intParam1 = field; c.floatParam1 = value; c.sliceIdx = idx;
    engine->pushCommand (c);
}

int ChopEngine::sliceCount() const { return engineLoaded() ? engine->getUiSliceSnapshot().numSlices : 0; }

// ================================================================================================ analysis (Flayr Chop)
Detected ChopEngine::detected() const { const juce::ScopedLock l (detLock); return det; }

void ChopEngine::analyse()
{
    auto c = clip();
    { const juce::ScopedLock l (detLock); det = Detected(); }
    if (c == nullptr || c->frames() < 64) return;
    const int serial = ++analysisSerial;
    analysisBusy = true;
    auto token = alive;
    pool.addJob ([this, c, serial, token]
    {
        std::vector<float> mono ((size_t) c->frames(), 0.0f);
        for (int ch = 0; ch < c->audio.getNumChannels(); ++ch)
        {
            const float* s = c->audio.getReadPointer (ch);
            for (size_t i = 0; i < mono.size(); ++i) mono[i] += s[i] / (float) c->audio.getNumChannels();
        }
        const auto r = an::analyse (mono.data(), (int) mono.size(), c->rate);
        Detected d; d.ready = true; d.bpm = r.bpm; d.firstBeat = r.firstBeatSec; d.bpmConfidence = r.bpmConfidence;
        d.key = r.key; d.minor = r.minor; d.keyConfidence = r.keyConfidence; d.onsets = r.onsetsSec; d.strength = r.onsetStrength;
        juce::MessageManager::callAsync ([this, d, serial, token]
        {
            if (! token->load() || serial != analysisSerial) return;
            { const juce::ScopedLock l (detLock); det = d; }
            analysisBusy = false;
            if (pendingAutoChop) autoChop(); else changed();
        });
    });
}

void ChopEngine::timerCallback()
{
    if (pendingRebuild && engineLoaded()) { rebuildSlices(); if (! pendingRebuild) changed(); }
}

// ================================================================================================ state
juce::ValueTree ChopEngine::toTree() const
{
    juce::ValueTree t ("CHOP");
    t.setProperty ("clip", currentFile.getFullPathName(), nullptr);
    if (auto c = clip()) t.setProperty ("name", c->name, nullptr);
    t.setProperty ("keys", (int) keys, nullptr);
    t.setProperty ("auto", (int) autoMode, nullptr);
    t.setProperty ("count", chopCount, nullptr);
    juce::StringArray cs; for (auto& r : chops) cs.add (juce::String (r.a) + ":" + juce::String (r.b));
    t.setProperty ("chops", cs.joinIntoString (","), nullptr);
    { juce::Array<juce::var> ls; for (auto& l : labels) ls.add (l); t.setProperty ("labels", juce::JSON::toString (juce::var (ls), true), nullptr); }
    juce::MemoryBlock mb; engine->getStateInformation (mb);
    t.setProperty ("engine", mb.toBase64Encoding(), nullptr);
    return t;
}

void ChopEngine::fromTree (const juce::ValueTree& t)
{
    if (! t.hasType ("CHOP")) return;
    autoMode = (AutoMode) juce::jlimit (0, (int) AutoMode::kModes - 1, (int) t.getProperty ("auto", 0));
    chopCount = juce::jlimit (1, 64, (int) t.getProperty ("count", 16));
    const juce::File f (t.getProperty ("clip").toString());
    if (! f.existsAsFile()) return;
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr) return;
    auto c = std::make_shared<Clip>(); c->rate = r->sampleRate; c->name = t.getProperty ("name", f.getFileNameWithoutExtension()).toString();
    c->audio.setSize ((int) juce::jlimit (1u, 2u, r->numChannels), (int) r->lengthInSamples);
    r->read (&c->audio, 0, c->audio.getNumSamples(), 0, true, c->audio.getNumChannels() > 1);
    keys = (KeysMode) juce::jlimit (0, 1, (int) t.getProperty ("keys", 1));
    chops.clear();
    for (auto& s : juce::StringArray::fromTokens (t.getProperty ("chops").toString(), ",", {}))
        if (s.contains (":")) chops.push_back ({ s.upToFirstOccurrenceOf (":", false, false).getLargeIntValue(), s.fromFirstOccurrenceOf (":", false, false).getLargeIntValue() });
    labels.clear();
    if (auto lv = juce::JSON::parse (t.getProperty ("labels").toString()); lv.isArray()) for (auto& l : *lv.getArray()) labels.push_back (l.toString());
    undoStack.clear(); redoStack.clear();
    stop();
    std::atomic_store (&current, ClipPtr (c));
    currentFile = f;
    juce::MemoryBlock mb;
    if (mb.fromBase64Encoding (t.getProperty ("engine").toString()) && mb.getSize() > 0)
    { engine->setStateInformation (mb.getData(), (int) mb.getSize()); pendingRebuild = false; }   // INTERSECT restores its own slices
    else { engine->loadFileAsync (f); pendingRebuild = true; }
    analyse();
    changed();
}
}
