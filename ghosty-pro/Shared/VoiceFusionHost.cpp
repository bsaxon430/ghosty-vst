#include "VoiceFusionHost.h"
#include <set>

namespace fusion {
namespace {
constexpr int kUndoDepth = 60;
double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }
juce::String mmss(double s) { const int m = (int) (s / 60); return juce::String(m) + ":" + juce::String((int) s % 60).paddedLeft('0', 2); }
} // namespace

// ================================================================================================================ preset
juce::ValueTree Preset::toTree() const { juce::ValueTree t("GHOSTVOICE"); t.setProperty("version", 1, nullptr); t.setProperty("name", name, nullptr); t.appendChild(identity.toTree(), nullptr); t.appendChild(params.toTree(), nullptr); return t; }
Preset Preset::fromTree(const juce::ValueTree& t) { Preset p; if (!t.hasType("GHOSTVOICE")) return p; p.name = t.getProperty("name").toString(); p.identity = Identity::fromTree(t.getChildWithName("IDENTITY")); p.params = Params::fromTree(t.getChildWithName("GVPARAMS")); if (p.identity.name.isEmpty()) p.identity.name = p.name; return p; }

// ================================================================================================================ host
FusionHost::FusionHost() : juce::Thread("GhostVoiceWorker") { prm.engine = 1; cleanExportDir(); startThread(juce::Thread::Priority::low); }   // a fresh page starts on CLEAR; saved sessions keep their own engine
FusionHost::~FusionHost() {
    cancelFlag = true; { std::lock_guard<std::mutex> l(lock); for (auto& s : src) if (s) s->cancel = true; tasks.clear(); }
    signalThreadShouldExit(); wake.signal(); stopThread(20000);
}

// ---- slots
juce::StringArray FusionHost::audioExtensions() { return { ".wav", ".aif", ".aiff", ".flac", ".ogg", ".mp3" }; }
bool FusionHost::decodeFile(const juce::File& f, std::vector<float>& mono, int& fs, juce::String& err) {
    juce::AudioFormatManager fm; fm.registerBasicFormats(); std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(f));
    if (r == nullptr) { err = f.hasFileExtension("mp3") ? "MP3 can't be decoded in this build: convert " + f.getFileName() + " to WAV or FLAC" : "Can't read " + f.getFileName(); return false; }
    if (r->sampleRate < 1000 || r->sampleRate > 768000 || r->lengthInSamples <= 0 || r->numChannels < 1) { err = f.getFileName() + " is empty or has an unusual format"; return false; }
    const int n = (int) std::min<juce::int64>(r->lengthInSamples, (juce::int64) (kMaxSeconds * r->sampleRate)); const int inCh = (int) r->numChannels;
    mono.assign((size_t) n, 0.0f); constexpr int kBlock = 65536; juce::AudioBuffer<float> b(inCh, kBlock);
    for (int pos = 0; pos < n; pos += kBlock) { const int len = std::min(kBlock, n - pos); r->read(&b, 0, len, pos, true, true);
        for (int c = 0; c < inCh; ++c) { const float* p = b.getReadPointer(c); for (int i = 0; i < len; ++i) mono[(size_t) (pos + i)] += p[i] / (float) inCh; } }
    fs = (int) std::lround(r->sampleRate); err = {}; return true;
}
std::shared_ptr<Take> FusionHost::makeTake(std::vector<float> mono, int fs, const juce::String& name) {
    if (mono.empty() || fs <= 0) return nullptr;
    for (auto& v : mono) v = std::isfinite(v) ? juce::jlimit(-8.0f, 8.0f, v) : 0.0f;
    const int wr = voice::workingRate(fs); if (wr != fs) { mono = voice::resample(mono, fs, wr); fs = wr; }
    const size_t cap = (size_t) (kMaxSeconds * fs); if (mono.size() > cap) mono.resize(cap);
    auto t = std::make_shared<Take>(); t->fs = fs; t->name = name; double e = 0; for (float v : mono) e += (double) v * v; t->rms = (float) std::sqrt(e / std::max<size_t>(1, mono.size())); t->mono = std::move(mono); return t;
}
std::shared_ptr<Slot> FusionHost::makeSlot(TakePtr take) { auto s = std::make_shared<Slot>(); s->take = std::move(take); s->id = nextId++; return s; }

bool FusionHost::setTake(int slot, std::vector<float> mono, int fs, const juce::String& name, bool async, juce::String* err) {
    slot = juce::jlimit(0, 2, slot); std::shared_ptr<Take> t;
    try { t = makeTake(std::move(mono), fs, name); } catch (const std::bad_alloc&) { if (err) *err = "Out of memory loading " + name; setStatus("Out of memory loading " + name); return false; }
    if (t == nullptr) { if (err) *err = "no audio"; return false; }
    if (t->seconds() < voice::kMinSeconds) { const juce::String m = name + " is too short (min 50 ms)"; if (err) *err = m; setStatus(m); return false; }
    std::shared_ptr<Slot> s;
    { std::lock_guard<std::mutex> l(lock); s = makeSlot(t); pushUndo(true); if (src[slot]) src[slot]->cancel = true; src[slot] = s; stat = {}; if (slot == SlotA) res.reset(); ++contentVersion; trimMemory(); }
    changed(true);
    if (async) { enqueueAnalysis(s, slot); return true; }
    bool ok = false;
    guarded([&] { auto a = std::make_shared<voice::Analysis>(); std::atomic<bool> never { false }; ok = voice::analyse(t->mono.data(), t->length(), t->fs, voice::AnalysisOptions(), *a, { nullptr, &never });
                  if (ok) { auto id = std::make_shared<Identity>(extractIdentity(*a)); id->name = name; s->publish(std::move(a), std::move(id)); } });
    s->failed = !ok; changed(true); if (ok && autoRender) requestRender(); return ok;
}
bool FusionHost::loadFile(int slot, const juce::File& f, juce::String& err) { std::vector<float> m; int fs = 0; if (!decodeFile(f, m, fs, err)) return false; return setTake(slot, std::move(m), fs, f.getFileNameWithoutExtension(), false, &err); }
void FusionHost::loadFileAsync(int slot, const juce::File& f) {
    const int gen = cancelGen.load();
    post([this, slot, f, gen] { job = 4; progress = -1; std::vector<float> m; int fs = 0; juce::String err; const bool ok = decodeFile(f, m, fs, err); job = 0;
        if (!ok) { setStatus(err); return; } if (gen != cancelGen.load()) return; setTake(slot, std::move(m), fs, f.getFileNameWithoutExtension(), true, &err); });
}
void FusionHost::setIdentity(int slot, const Identity& id, const juce::String& presetName) {
    slot = juce::jlimit(1, 2, slot); auto s = makeSlot(nullptr); s->presetName = presetName; s->publish(nullptr, std::make_shared<Identity>(id));
    { std::lock_guard<std::mutex> l(lock); pushUndo(true); if (src[slot]) src[slot]->cancel = true; src[slot] = s; stat = {}; ++contentVersion; trimMemory(); }
    changed(true); if (autoRender) requestRender();
}
void FusionHost::clearSlot(int slot, bool undoable) {
    slot = juce::jlimit(0, 2, slot);
    { std::lock_guard<std::mutex> l(lock); if (undoable) pushUndo(true); if (src[slot]) src[slot]->cancel = true; src[slot].reset(); stat = {}; if (slot == SlotA) res.reset(); ++contentVersion; trimMemory(); }
    ++resultVersion; changed(true); if (autoRender) requestRender();
}
SlotPtr FusionHost::slot(int i) const { std::lock_guard<std::mutex> l(lock); return src[juce::jlimit(0, 2, i)]; }

void FusionHost::enqueueAnalysis(const SlotPtr& s, int slotIx) {
    if (s == nullptr || !s->take || s->ready() || s->queued.exchange(true)) return;
    s->cancel = false; s->failed = false;
    post([this, s, slotIx] {
        bool inSlot; { std::lock_guard<std::mutex> l(lock); inSlot = src[0] == s || src[1] == s || src[2] == s; }
        if (s->ready() || !inSlot || s->cancel.load()) { if (!s->ready() && s->cancel.load()) s->failed = true; s->queued = false; return; }
        job = 1; jobStart = nowMs(); progress = 0; bool ok = false;
        guarded([&] { auto a = std::make_shared<voice::Analysis>(); ok = voice::analyse(s->take->mono.data(), s->take->length(), s->take->fs, voice::AnalysisOptions(), *a, { &progress, &s->cancel });
                      if (ok && !s->ready()) { auto id = std::make_shared<Identity>(extractIdentity(*a)); id->name = s->take->name; s->publish(std::move(a), std::move(id)); } });
        if (!ok) { s->failed = true; setStatus(s->name() + ": analysis stopped"); }
        else if (slotIx != SlotA) { auto id = s->identity(); if (id && id->frames.size() <= 1) setStatus(s->name() + ": no clear vowels found - it will be used by its overall colour"); }
        s->queued = false; job = 0; progress = -1; changed(true); if (ok && autoRender) requestRender();
    });
}

// ---- settings + undo
Params FusionHost::params() const { std::lock_guard<std::mutex> l(lock); return prm; }
void FusionHost::setParams(const Params& p0, bool undoable, const juce::String& key) {
    Params p = p0; Params::sanitise(p);
    { std::lock_guard<std::mutex> l(lock); const auto now = juce::Time::getMillisecondCounter(); const bool coalesce = key.isNotEmpty() && key == lastKey && now - lastKeyMs < 400;
      if (undoable && !coalesce) pushUndo(); lastKey = key; lastKeyMs = now; prm = p; }
    ++paramsVersion; if (autoRender) requestRender();
}
FusionHost::Snap FusionHost::snap() const { return { prm, { src[0], src[1], src[2] } }; }
void FusionHost::pushUndo(bool force) { if (!force && inGesture) { if (gesturePushed) return; gesturePushed = true; } undoStack.push_back(snap()); if ((int) undoStack.size() > kUndoDepth) undoStack.pop_front(); redoStack.clear(); trimMemory(); }
void FusionHost::trimMemory() {   // analyses + blobs only for the slots in use (+ the 2 most recent); the history keeps the takes themselves
    std::set<juce::uint32> keep; for (auto& s : src) if (s) keep.insert(s->id);
    auto visit = [&](const SlotPtr& s) { if (s && s->take && !keep.count(s->id)) { s->publish(nullptr, nullptr); std::lock_guard<std::mutex> b(s->blobLock); s->blob = {}; } };
    for (auto& sn : undoStack) for (auto& s : sn.s) visit(s);
    for (auto& sn : redoStack) for (auto& s : sn.s) visit(s);
    auto bytes = [&] { std::set<const Take*> seen; size_t b = 0; for (auto& s : src) if (s && s->take) seen.insert(s->take.get());
        auto add = [&](const SlotPtr& s) { if (s && s->take && seen.insert(s->take.get()).second) b += s->take->mono.capacity() * sizeof(float); };
        for (auto& sn : undoStack) for (auto& s : sn.s) add(s); for (auto& sn : redoStack) for (auto& s : sn.s) add(s); return b; };
    while (bytes() > (512u << 20) && (!undoStack.empty() || !redoStack.empty())) { if (!undoStack.empty()) undoStack.pop_front(); else redoStack.pop_front(); }
}
void FusionHost::restore(const Snap& s) {
    std::vector<std::pair<SlotPtr, int>> need;
    { std::lock_guard<std::mutex> l(lock); const bool aChanged = src[0] != s.s[0]; prm = s.p; for (int i = 0; i < 3; ++i) src[i] = s.s[i]; if (aChanged) res.reset(); ++contentVersion;
      for (int i = 0; i < 3; ++i) if (src[i] && src[i]->take && !src[i]->ready()) need.push_back({ src[i], i }); }
    ++paramsVersion; ++resultVersion; changed(true);
    for (auto& x : need) { x.first->failed = false; x.first->cancel = false; enqueueAnalysis(x.first, x.second); }
    if (autoRender) requestRender();
}
void FusionHost::beginGesture() { std::lock_guard<std::mutex> l(lock); inGesture = true; gesturePushed = false; }
void FusionHost::endGesture() { std::lock_guard<std::mutex> l(lock); inGesture = false; gesturePushed = false; lastKey = {}; }
bool FusionHost::undo() { Snap s; { std::lock_guard<std::mutex> l(lock); if (undoStack.empty()) return false; s = undoStack.back(); undoStack.pop_back(); redoStack.push_back(snap()); lastKey = {}; } restore(s); return true; }
bool FusionHost::redo() { Snap s; { std::lock_guard<std::mutex> l(lock); if (redoStack.empty()) return false; s = redoStack.back(); redoStack.pop_back(); undoStack.push_back(snap()); lastKey = {}; } restore(s); return true; }
bool FusionHost::canUndo() const { std::lock_guard<std::mutex> l(lock); return !undoStack.empty(); }
bool FusionHost::canRedo() const { std::lock_guard<std::mutex> l(lock); return !redoStack.empty(); }

// ---- worker
void FusionHost::post(std::function<void()> task) { { std::lock_guard<std::mutex> l(lock); tasks.push_back(std::move(task)); } wake.signal(); }
void FusionHost::guarded(const std::function<void()>& f) {
    try { f(); } catch (const std::bad_alloc&) { setStatus("Out of memory: try a shorter take"); } catch (const std::exception& e) { setStatus(juce::String("Error: ") + e.what()); } catch (...) { setStatus("Unexpected error in the GHOST VOICE worker"); }
}
void FusionHost::requestRender() { renderWanted = true; if (job.load() == 2) cancelFlag = true; wake.signal(); }
void FusionHost::cancel() { renderWanted = false; cancelFlag = true; ++cancelGen; std::deque<std::function<void()>> drop; { std::lock_guard<std::mutex> l(lock); drop.swap(tasks); for (auto& x : src) if (x && x->take && !x->ready()) { x->cancel = true; x->failed = true; x->queued = false; } } }
void FusionHost::reanalyse() { std::vector<std::pair<SlotPtr, int>> need; { std::lock_guard<std::mutex> l(lock); for (int i = 0; i < 3; ++i) if (src[i] && src[i]->take && !src[i]->ready()) need.push_back({ src[i], i }); }
    for (auto& x : need) { if (!x.first->queued.load()) { x.first->failed = false; x.first->cancel = false; } enqueueAnalysis(x.first, x.second); } ++sourceVersion; }
void FusionHost::run() {
    while (!threadShouldExit()) {
        std::function<void()> t; { std::lock_guard<std::mutex> l(lock); if (!tasks.empty()) { t = std::move(tasks.front()); tasks.pop_front(); busyTask = true; } }
        if (t) { cancelFlag = false; guarded(t); busyTask = false; continue; }
        if (renderWanted.exchange(false)) { cancelFlag = false; busyTask = true; guarded([this] { doRender(false, nullptr); }); busyTask = false; continue; }
        wake.wait(200);
    }
}
bool FusionHost::doRender(bool sync, juce::String* err) {
    Params p; SlotPtr a, b, c; int pv, cv; { std::lock_guard<std::mutex> l(lock); p = prm; a = src[0]; b = src[1]; c = src[2]; pv = paramsVersion.load(); cv = contentVersion.load(); }
    if (a == nullptr || !a->take) { if (err) *err = "record or drop your voice first"; return false; }
    auto aa = a->analysis(); if (aa == nullptr) { if (err) *err = "your voice is not analysed yet"; return false; }
    if (aa->frames() < 2) { if (err) *err = "the take is too short"; setStatus(a->name() + " is too short (min 50 ms)"); return false; }
    auto ib = b ? b->identity() : nullptr; auto ic = c ? c->identity() : nullptr;
    if (!sync && ((b && b->take && !ib) || (c && c->take && !ic))) return false;   // their analysis will ask again when it lands
    if (!sync) { job = 2; jobStart = nowMs(); progress = 0; }
    auto r = std::make_shared<Result>(); r->params = p; r->paramsVersion = pv; r->sourceVersion = cv; std::atomic<bool> never { false };
    const bool ok = render(*aa, a->take->ref(), ib.get(), ic.get(), p, r->audio, sync ? voice::Progress { nullptr, &never } : voice::Progress { &progress, &cancelFlag }, &r->info);
    if (!sync) { job = 0; progress = -1; }
    if (!ok) return false;
    if (!b && !c && p.wB + p.wC > 0.01f) setStatus("Add VOICE B: record it, drop a file, or pick a voice");
    double e = 0; for (float v : r->audio.L) e += (double) v * v; for (float v : r->audio.R) e += (double) v * v; r->rms = (float) std::sqrt(e / std::max<size_t>(1, r->audio.L.size() + r->audio.R.size()));
    r->serial = ++serial; { std::lock_guard<std::mutex> l(lock); res = r; } ++resultVersion; publishPlay(); return true;
}
bool FusionHost::renderNow(juce::String& err) { bool ok = false; guarded([&] { ok = doRender(true, &err); }); if (!ok && err.isEmpty()) err = status(); return ok; }
bool FusionHost::waitIdle(int timeoutMs) {
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    auto idle = [&] { bool empty; { std::lock_guard<std::mutex> l(lock); empty = tasks.empty(); } return empty && !busyTask.load() && job.load() == 0 && !renderWanted.load(); };
    while (juce::Time::getMillisecondCounter() < end) { if (idle()) { juce::Thread::sleep(5); if (idle()) return true; } juce::Thread::sleep(5); }
    return false;
}
ResultPtr FusionHost::result() const { std::lock_guard<std::mutex> l(lock); return res; }
bool FusionHost::resultStale() const { std::lock_guard<std::mutex> l(lock); if (src[0] == nullptr) return false; if (res == nullptr) return true; return res->paramsVersion != paramsVersion.load() || res->sourceVersion != contentVersion.load(); }
double FusionHost::secondsLeft() const { const int p = progress.load(); if (job.load() == 0 || p <= 2) return -1; const double el = (nowMs() - jobStart.load()) / 1000.0; return el * (100.0 - p) / p; }
juce::String FusionHost::status() const { std::lock_guard<std::mutex> l(lock); return stat; }
juce::uint32 FusionHost::statusTime() const { std::lock_guard<std::mutex> l(lock); return statT; }
void FusionHost::setStatus(const juce::String& s) { std::lock_guard<std::mutex> l(lock); stat = s; statT = juce::Time::getMillisecondCounter(); }
void FusionHost::changed(bool sources) { if (sources) ++sourceVersion; publishPlay(); }
juce::String FusionHost::uiPrefs() const { std::lock_guard<std::mutex> l(lock); return ui; }
void FusionHost::setUiPrefs(const juce::String& s) { std::lock_guard<std::mutex> l(lock); ui = s; }

// ---- recorder
void FusionHost::startRecording(int slot, int countInBeats, double bpm) {
    if (recording.load() || countingIn.load()) return;
    stop(); recSlot = juce::jlimit(0, 2, slot); recLen = 0; recFull = false; recDone = false; inLevel = 0;
    countBpm = juce::jlimit(30.0, 300.0, bpm); countBeats = juce::jlimit(0, 8, countInBeats); countPhase = 0; clickLeft = 0;
    if (countBeats > 0) { countLeft = countBeats; countingIn = true; setStatus("Count-in..."); } else { recording = true; setStatus("Recording..."); }
}
void FusionHost::stopRecording() {
    if (countingIn.exchange(false)) { countLeft = 0; recording = false; setStatus("Recording cancelled"); return; }
    if (!recording.exchange(false) && !recDone.load()) return;
    finishRecording(false);
}
void FusionHost::pollRecording() { if (recDone.load()) finishRecording(true); }
void FusionHost::finishRecording(bool) {
    recDone = false; const int len = recLen.load(); const int slot = recSlot.load();
    if (len < (int) (voice::kMinSeconds * hostRate)) { setStatus("Nothing recorded (too short)"); return; }
    std::vector<float> m(rec.begin(), rec.begin() + len); juce::String err;
    const juce::String name = slot == SlotA ? "my take" : slot == SlotB ? "voice B take" : "voice C take";
    if (setTake(slot, std::move(m), (int) std::lround(hostRate), name + " " + juce::Time::getCurrentTime().formatted("%H.%M"), true, &err)) setStatus("Recorded " + mmss(len / hostRate) + " into " + juce::String(slot == 0 ? "YOUR VOICE" : slot == 1 ? "VOICE B" : "VOICE C"));
    else setStatus(err);
}
void FusionHost::processRecord(const float* inL, const float* inR, int n, juce::AudioBuffer<float>& out) {
    if (!recording.load() && !countingIn.load()) { if (inL) { float pk = 0; for (int i = 0; i < n; ++i) pk = std::max(pk, std::abs(inL[i])); inLevel = pk; } return; }
    const int outCh = out.getNumChannels(); const double beat = 60.0 / countBpm * hostRate;
    for (int i = 0; i < n; ++i) {
        if (countingIn.load()) {
            const int before = (int) std::floor(countPhase / beat); countPhase += 1.0; const int now = (int) std::floor(countPhase / beat);
            if (i == 0 && countPhase <= 1.0) { clickLeft = (int) (0.025 * hostRate); clickPhase = 0; clickAmp = 0.5f; clickHz = 1500; }   // the first click: a higher accent
            if (now != before) { if (now >= countBeats) { countingIn = false; countLeft = 0; recording = true; recLen = 0; clickLeft = (int) (0.02 * hostRate); clickPhase = 0; clickAmp = 0.35f; clickHz = 1000; }
                                 else { countLeft = countBeats - now; clickLeft = (int) (0.02 * hostRate); clickPhase = 0; clickAmp = 0.4f; clickHz = 1000; } }
        } else if (recording.load()) {
            const int len = recLen.load(); if (len < (int) rec.size()) { rec[(size_t) len] = inL ? (inR ? 0.5f * (inL[i] + inR[i]) : inL[i]) : 0.0f; recLen = len + 1; }
            else { recording = false; recFull = true; recDone = true; }
        }
        if (clickLeft > 0) { const float env = (float) clickLeft / (float) (0.02 * hostRate); const float v = clickAmp * std::min(1.0f, env) * (float) std::sin(clickPhase); clickPhase += 2.0 * juce::MathConstants<double>::pi * clickHz / hostRate; --clickLeft;
            for (int c = 0; c < outCh; ++c) out.addSample(c, i, v); }
    }
    if (inL) { float pk = 0; for (int i = 0; i < n; ++i) pk = std::max(pk, std::abs(inL[i])); inLevel = pk; }
}

// ---- preview
void FusionHost::publishPlay() {
    auto it = std::make_unique<PlayItem>();
    { std::lock_guard<std::mutex> l(lock); it->r = res; it->a = src[0] ? src[0]->take : nullptr; it->b = src[1] ? src[1]->take : nullptr; it->c = src[2] ? src[2]->take : nullptr; }
    if (levelMatch.load() && it->r && it->r->rms > 1e-6f) { auto g = [&](const TakePtr& t) { return t && t->rms > 1e-6f ? juce::jlimit(1.0f / 16, 16.0f, it->r->rms / t->rms) : 1.0f; }; it->gA = g(it->a); it->gB = g(it->b); it->gC = g(it->c); }
    std::lock_guard<std::mutex> l(publishLock); items.publish(std::move(it));
}
void FusionHost::prepare(double sampleRate, int) { hostRate = sampleRate > 0 ? sampleRate : 48000.0; recording = false; countingIn = false; rec.assign((size_t) (kMaxRecordSeconds * hostRate) + 1024, 0.0f); recLen = 0; }
void FusionHost::play(int what, double fromSeconds) { playFrom = std::max(0.0, fromSeconds); playWhatReq = what; ++playReq; }
void FusionHost::toggleAB() {
    const int what = playing.load(); const double t = std::max(0.0, playPos.load()); auto r = result(); const double t0 = r ? r->info.t0 : 0.0;
    if (what == PlayResult) play(PlayA, t + t0); else if (what == PlayA) play(r ? PlayResult : PlayA, r ? std::max(0.0, t - t0) : t); else play(r ? PlayResult : PlayA, 0.0);
}
void FusionHost::process(juce::AudioBuffer<float>& out) {
    const PlayItem* it = items.get(); const int req = playReq.load();
    if (req != playSeen) { playSeen = req; pendingWhat = playWhatReq.load(); pendingFrom = playFrom.load(); switching = true; }
    if (it == nullptr) { if (playing.load() != PlayNone) { playing = PlayNone; playPos = -1.0; } cur = PlayNone; return; }
    const int n = out.getNumSamples(), nch = out.getNumChannels(); const float pg = previewGain.load(); const float fadeStep = (float) (1.0 / (0.004 * hostRate));
    auto sourceOf = [&](int what, const float*& l, const float*& r, int& len, int& fs, float& g) {
        l = r = nullptr; len = 0; fs = 0; g = 1;
        if (what == PlayResult && it->r && !it->r->audio.L.empty()) { l = it->r->audio.L.data(); r = it->r->audio.stereo() ? it->r->audio.R.data() : l; len = (int) it->r->audio.L.size(); fs = it->r->audio.fs; }
        else { const TakePtr& t = what == PlayA ? it->a : what == PlayB ? it->b : what == PlayC ? it->c : it->a; if ((what == PlayA || what == PlayB || what == PlayC) && t) { l = r = t->mono.data(); len = t->length(); fs = t->fs; g = what == PlayA ? it->gA : what == PlayB ? it->gB : it->gC; } }
    };
    const float* L; const float* R; int len, fs; float g; sourceOf(cur, L, R, len, fs, g);
    if (cur != PlayNone && L == nullptr) cur = PlayNone; if (cur == PlayNone && fade > 0) fade = 0;
    auto hermite = [](const float* x, int count, double p) { const int i = (int) p; const float t = (float) (p - i); auto at = [&](int k) { return k < 0 || k >= count ? 0.0f : x[k]; };
        const float y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2); const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2); return ((c3 * t + c2) * t + c1) * t + y1; };
    for (int i = 0; i < n; ++i) {
        if (switching) { if (cur == PlayNone || fade <= 0.0f) { cur = pendingWhat; switching = false; fade = 0; sourceOf(cur, L, R, len, fs, g); if (L == nullptr) cur = PlayNone; else pos = pendingFrom * fs; } else fade = std::max(0.0f, fade - fadeStep); }
        else if (cur != PlayNone) fade = std::min(1.0f, fade + fadeStep);
        if (cur == PlayNone) continue;
        if (pos >= len - 1) { cur = PlayNone; continue; }
        const float gg = g * pg * fade; const float vl = hermite(L, len, pos) * gg, vr = hermite(R, len, pos) * gg;
        if (nch >= 2) { out.addSample(0, i, vl); out.addSample(1, i, vr); for (int c = 2; c < nch; ++c) out.addSample(c, i, 0.5f * (vl + vr)); } else if (nch == 1) out.addSample(0, i, 0.5f * (vl + vr));
        pos += fs / hostRate;
    }
    playing = cur; playPos = cur == PlayNone || fs <= 0 ? -1.0 : pos / fs;
}

// ---- export + presets
juce::File FusionHost::exportDir() { auto d = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("GhostyGhostVoice"); d.createDirectory(); return d; }
void FusionHost::cleanExportDir(double maxAgeHours) { const auto cutoff = juce::Time::getCurrentTime() - juce::RelativeTime::hours(maxAgeHours); for (const auto& e : juce::RangedDirectoryIterator(exportDir(), false, "*", juce::File::findFilesAndDirectories)) if (e.getModificationTime() < cutoff) { if (e.isDirectory()) e.getFile().deleteRecursively(); else e.getFile().deleteFile(); } }
bool FusionHost::writeWav(const juce::File& f, const voice::Rendered& x) {
    if (x.L.empty() || x.fs <= 0) return false; f.deleteFile(); std::unique_ptr<juce::FileOutputStream> os(f.createOutputStream()); if (os == nullptr) return false;
    juce::WavAudioFormat wav; const int nch = x.stereo() ? 2 : 1;
    std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(os.get(), x.fs, (unsigned) nch, 24, {}, 0)); if (w == nullptr) return false;   // GATE: via-caller (the page asked spec.ghostvoice.*)
    os.release(); const float* ch[2] = { x.L.data(), x.stereo() ? x.R.data() : nullptr }; return w->writeFromFloatArrays(ch, nch, (int) x.L.size());
}
juce::File FusionHost::writeResultWav(juce::String& why, bool renderIfStale) {
    if (renderIfStale && resultStale()) { juce::String err; if (!renderNow(err)) { why = err.isNotEmpty() ? err : juce::String("Nothing rendered yet"); return {}; } }
    auto r = result(); if (r == nullptr || r->audio.L.empty()) { why = "Nothing rendered yet"; return {}; }
    auto a = slot(SlotA); auto b = slot(SlotB); const auto& p = r->params;
    juce::String base = juce::File::createLegalFileName(a ? a->name() : juce::String("voice")).trim(); if (base.isEmpty()) base = "voice";
    base << " ghost"; if (b && !b->empty()) base << " " << (int) std::lround(p.wB * 100) << "pct " << juce::File::createLegalFileName(b->name()).trim();
    auto f = exportDir().getNonexistentChildFile(base, ".wav", true); if (!writeWav(f, r->audio)) { why = "Couldn't write " + f.getFullPathName(); return {}; } return f;
}
bool FusionHost::exportResultTo(const juce::File& f, juce::String& why) {
    if (resultStale()) { juce::String err; if (!renderNow(err)) { why = err.isNotEmpty() ? err : juce::String("Nothing rendered yet"); return false; } }
    auto r = result(); if (r == nullptr || r->audio.L.empty()) { why = "Nothing rendered yet"; return false; }
    if (!writeWav(f, r->audio)) { why = "Couldn't write " + f.getFullPathName(); return false; } return true;
}
juce::File FusionHost::voicesDir() { auto d = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Ghosty").getChildFile("Ghosty Spectrum").getChildFile("Voices"); d.createDirectory(); return d; }
juce::StringArray FusionHost::presetFiles() { juce::StringArray n; for (const auto& e : juce::RangedDirectoryIterator(voicesDir(), false, "*.ghostvoice", juce::File::findFiles)) n.add(e.getFile().getFileNameWithoutExtension()); n.sortNatural(); return n; }
Identity FusionHost::currentBlendIdentity() const {
    Params p; SlotPtr a, b, c; { std::lock_guard<std::mutex> l(lock); p = prm; a = src[0]; b = src[1]; c = src[2]; }
    auto ia = a ? a->identity() : nullptr; auto ib = b ? b->identity() : nullptr; auto ic = c ? c->identity() : nullptr;
    if (!ia) { if (ib && p.wB >= p.wC) return *ib; if (ic) return *ic; return {}; }   // no performance yet: the voice IS B (or C)
    return blendIdentity(*ia, ib.get(), ic.get(), p);
}
bool FusionHost::saveVoice(const juce::String& name0, juce::String& err, juce::File* written) {
    const juce::String name = juce::File::createLegalFileName(name0).trim(); if (name.isEmpty()) { err = "Give the voice a name"; return false; }
    Preset pr; pr.name = name; pr.identity = currentBlendIdentity(); pr.identity.name = name; pr.params = params(); pr.params.trimStart = pr.params.trimEnd = -1;
    if (pr.identity.empty()) { err = "Nothing to save yet: record or drop a voice first (and let it analyse)"; return false; }
    auto f = voicesDir().getChildFile(name + ".ghostvoice"); const auto xml = pr.toTree().toXmlString();
    if (!f.replaceWithText(xml)) { err = "Couldn't write " + f.getFullPathName(); return false; }   // NOT-EXPORT: the plugin's own voice preset (an identity + settings, no audio)
    if (written) *written = f; setStatus("Saved voice \"" + name + "\""); return true;
}
bool FusionHost::loadVoice(const juce::File& f, int slot, bool withSettings, juce::String& err) {
    const auto t = juce::ValueTree::fromXml(f.loadFileAsString()); const auto pr = Preset::fromTree(t);
    if (pr.identity.empty()) { err = "Not a Ghost Voice file: " + f.getFileName(); return false; }
    setIdentity(slot, pr.identity, pr.name.isNotEmpty() ? pr.name : f.getFileNameWithoutExtension());
    if (withSettings) { auto p = pr.params; const auto now = params(); p.trimStart = now.trimStart; p.trimEnd = now.trimEnd; setParams(p); }
    return true;
}

// ---- state
juce::String FusionHost::encode(const Slot& s) {
    std::lock_guard<std::mutex> l(s.blobLock); if (s.blob.isNotEmpty() || !s.take) return s.blob;
    juce::MemoryBlock mb; { juce::FlacAudioFormat flac; auto* os = new juce::MemoryOutputStream(mb, false);
        std::unique_ptr<juce::AudioFormatWriter> w(flac.createWriterFor(os, s.take->fs, 1, 24, {}, 5));   // NOT-EXPORT: the user's own take inside the project state (memory)
        if (w == nullptr) { delete os; return {}; } std::vector<float> c(s.take->mono); for (auto& v : c) v = juce::jlimit(-1.0f, 1.0f, v); const float* ch[1] = { c.data() }; w->writeFromFloatArrays(ch, 1, (int) c.size()); }
    s.blob = mb.toBase64Encoding(); return s.blob;
}
bool FusionHost::decodeBlob(const juce::String& b64, std::vector<float>& mono, int& fs) {
    juce::MemoryBlock mb; if (b64.isEmpty() || !mb.fromBase64Encoding(b64) || mb.getSize() == 0) return false;
    juce::FlacAudioFormat flac; std::unique_ptr<juce::AudioFormatReader> r(flac.createReaderFor(new juce::MemoryInputStream(mb, false), true));
    if (r == nullptr || r->lengthInSamples <= 0 || r->sampleRate < 1000 || r->sampleRate > 768000 || r->numChannels < 1) return false;
    const int n = (int) std::min<juce::int64>(r->lengthInSamples, (juce::int64) (kMaxSeconds * r->sampleRate)); juce::AudioBuffer<float> b((int) r->numChannels, n); r->read(&b, 0, n, 0, true, true);
    mono.assign(b.getReadPointer(0), b.getReadPointer(0) + n); fs = (int) std::lround(r->sampleRate); return fs > 0;
}
juce::ValueTree FusionHost::toTree() const {
    Params p; SlotPtr s[3]; juce::String u; { std::lock_guard<std::mutex> l(lock); p = prm; for (int i = 0; i < 3; ++i) s[i] = src[i]; u = ui; }
    juce::ValueTree t("GHOSTVOICEPAGE"); t.setProperty("version", 1, nullptr); t.setProperty("ui", u, nullptr); t.setProperty("auto", autoRender.load(), nullptr); t.appendChild(p.toTree(), nullptr);
    for (int i = 0; i < 3; ++i) if (s[i] && !s[i]->empty()) {
        juce::ValueTree c("SLOT"); c.setProperty("index", i, nullptr); c.setProperty("name", s[i]->name(), nullptr);
        if (s[i]->take) { c.setProperty("fs", s[i]->take->fs, nullptr); c.setProperty("flac", encode(*s[i]), nullptr); }
        else if (auto id = s[i]->identity()) { c.setProperty("preset", s[i]->presetName, nullptr); c.appendChild(id->toTree(), nullptr); }
        t.appendChild(c, nullptr);
    }
    return t;
}
void FusionHost::fromTree(const juce::ValueTree& t) {
    if (!t.hasType("GHOSTVOICEPAGE")) return;
    cancel(); Params p = Params::fromTree(t.getChildWithName("GVPARAMS"));
    std::shared_ptr<Slot> s[3];
    for (const auto& c : t) if (c.hasType("SLOT")) {
        const int i = juce::jlimit(0, 2, (int) c.getProperty("index", 0));
        if (c.hasProperty("flac")) { std::vector<float> m; int fs = 0; try { if (decodeBlob(c.getProperty("flac").toString(), m, fs)) { auto tk = makeTake(std::move(m), fs, c.getProperty("name").toString()); if (tk && tk->seconds() >= voice::kMinSeconds) s[i] = makeSlot(tk); } } catch (const std::bad_alloc&) { setStatus("Out of memory restoring the GHOST VOICE takes"); } }
        else { const auto id = Identity::fromTree(c.getChildWithName("IDENTITY")); if (!id.empty()) { s[i] = makeSlot(nullptr); s[i]->presetName = c.getProperty("preset").toString(); s[i]->publish(nullptr, std::make_shared<Identity>(id)); } }
    }
    { std::lock_guard<std::mutex> l(lock); for (auto& x : src) if (x) x->cancel = true; prm = p; for (int i = 0; i < 3; ++i) src[i] = s[i]; res.reset(); undoStack.clear(); redoStack.clear(); ui = t.getProperty("ui").toString(); stat = {}; ++contentVersion; }
    autoRender = (bool) t.getProperty("auto", true);
    ++paramsVersion; ++resultVersion; changed(true);
    for (int i = 0; i < 3; ++i) if (s[i] && s[i]->take) enqueueAnalysis(s[i], i);
}
} // namespace fusion
