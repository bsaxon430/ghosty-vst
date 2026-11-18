#include "VoiceHost.h"
#include <set>

namespace voice {
namespace {
constexpr int kUndoDepth = 80;
const char* kModeNames[] = { "pitch", "borrow", "contour", "vowel", "tune" };
juce::String noteName(int m) { static const char* N[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; return juce::String(N[((m % 12) + 12) % 12]) + juce::String(m / 12 - 1); }
juce::String mmss(double s) { const int m = (int) (s / 60); return juce::String(m) + ":" + juce::String((int) s % 60).paddedLeft('0', 2); }
double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }
} // namespace

VoiceHost::VoiceHost() : juce::Thread("GhostyVoiceWorker") { cleanExportDir(); startThread(juce::Thread::Priority::low); }
VoiceHost::~VoiceHost() {
    cancelFlag = true; { std::lock_guard<std::mutex> l(lock); for (auto& s : src) if (s) s->cancel = true; tasks.clear(); }
    signalThreadShouldExit(); wake.signal(); stopThread(20000);
}

// ================================================================================================================ sources
juce::StringArray VoiceHost::audioExtensions() { return { ".wav", ".aif", ".aiff", ".flac", ".ogg", ".mp3" }; }

bool VoiceHost::decodeFile(const juce::File& f, std::vector<std::vector<float>>& ch, int& fs, int& bits, juce::String& err, juce::String& note) {
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(f));
    if (r == nullptr) {
        err = f.hasFileExtension("mp3") ? "MP3 can't be decoded in this build: convert " + f.getFileName() + " to WAV or FLAC" : "Can't read " + f.getFileName();
        return false;
    }
    if (r->sampleRate < 1000 || r->sampleRate > 768000 || r->lengthInSamples <= 0 || r->numChannels < 1) { err = f.getFileName() + " is empty or has an unusual format"; return false; }
    const juce::int64 cap = (juce::int64) (kMaxSeconds * r->sampleRate);
    const int n = (int) std::min<juce::int64>(r->lengthInSamples, cap);
    note = r->lengthInSamples > cap ? "trimmed to " + mmss(kMaxSeconds) + " (select a region in your editor to use another part)" : juce::String();
    const int inCh = (int) r->numChannels, outCh = inCh >= 2 ? 2 : 1;
    ch.assign((size_t) outCh, std::vector<float>((size_t) n, 0.0f));
    constexpr int kBlock = 65536; juce::AudioBuffer<float> b(inCh, kBlock);   // read in blocks: multichannel files never balloon
    for (int pos = 0; pos < n; pos += kBlock) {
        const int len = std::min(kBlock, n - pos); r->read(&b, 0, len, pos, true, true);
        for (int c = 0; c < inCh; ++c) { const float* p = b.getReadPointer(c); auto& d = ch[(size_t) (outCh == 2 ? c % 2 : 0)]; const float g = outCh == 2 ? 1.0f / ((inCh + 1 - c % 2) / 2) : 1.0f / inCh;
            for (int i = 0; i < len; ++i) d[(size_t) (pos + i)] += p[i] * g; }
    }
    fs = (int) std::lround(r->sampleRate); bits = (int) r->bitsPerSample <= 16 && !r->usesFloatingPointData ? 16 : 24;
    err = {}; return true;
}

std::shared_ptr<Pcm> VoiceHost::makePcm(std::vector<std::vector<float>> ch, int fs, const juce::String& name, int bits, juce::String* note) {
    if (ch.empty() || fs <= 0) return nullptr;
    if (ch.size() > 2) ch.resize(2);
    size_t n = ch[0].size(); for (auto& c : ch) n = std::min(n, c.size()); for (auto& c : ch) c.resize(n);
    if (n == 0) return nullptr;
    for (auto& c : ch) for (auto& v : c) if (!std::isfinite(v)) v = 0.0f; else v = juce::jlimit(-8.0f, 8.0f, v);
    const int wr = workingRate(fs);
    if (wr != fs) { for (auto& c : ch) c = resample(c, fs, wr); fs = wr; n = ch[0].size(); for (auto& c : ch) { c.resize(n); } }
    const size_t cap = (size_t) (kMaxSeconds * fs);
    if (n > cap) { for (auto& c : ch) c.resize(cap); n = cap; if (note && note->isEmpty()) *note = "trimmed to " + mmss(kMaxSeconds); }
    auto p = std::make_shared<Pcm>(); p->fs = fs; p->bits = bits; p->name = name; p->mono.assign(n, 0.0f);
    for (auto& c : ch) for (size_t i = 0; i < n; ++i) p->mono[i] += c[i] / (float) ch.size();
    double e = 0; for (float v : p->mono) e += (double) v * v; p->rms = (float) std::sqrt(e / std::max<size_t>(1, n));
    if (ch.size() == 2) p->ch = std::move(ch);
    return p;
}

std::shared_ptr<Source> VoiceHost::makeSource(PcmPtr pcm) {
    auto s = std::make_shared<Source>(); s->pcm = std::move(pcm); s->id = nextId++;
    s->opts = opt; return s;
}

bool VoiceHost::setAudio(int slot, std::vector<std::vector<float>> channels, int fs, const juce::String& name, bool async, juce::String* err, int bits) {
    slot = juce::jlimit(0, 1, slot);
    juce::String note; std::shared_ptr<Pcm> pcm;
    try { pcm = makePcm(std::move(channels), fs, name, bits, &note); } catch (const std::bad_alloc&) { if (err) *err = "Out of memory loading " + name; setStatus("Out of memory loading " + name); return false; }
    if (pcm == nullptr) { if (err) *err = "no audio"; return false; }
    if (pcm->seconds() < kMinSeconds) { const juce::String m = name + " is too short (min 50 ms)"; if (err) *err = m; setStatus(m); return false; }
    std::shared_ptr<Source> s;
    {
        std::lock_guard<std::mutex> l(lock); s = makeSource(pcm);
        pushUndo(true);                                  // a new sound is always its own undo step
        if (src[slot]) src[slot]->cancel = true;         // its analysis (if still running) is obsolete now
        src[slot] = s; trimNote[slot] = note; stat = note.isNotEmpty() ? name + ": " + note : juce::String(); statT = juce::Time::getMillisecondCounter();
        if (slot == SlotA) res.reset();
        ++contentVersion; trimMemory();
    }
    changed(true);
    if (async) { enqueueAnalysis(s); return true; }
    bool ok = false;
    guarded([&] {
        auto a = std::make_shared<Analysis>(); std::atomic<bool> never { false };
        ok = analyse(s->pcm->mono.data(), s->pcm->length(), s->pcm->fs, s->opts, *a, { nullptr, &never });
        if (ok) { s->publish(std::move(a)); std::lock_guard<std::mutex> l(lock); analysedLru.push_front(s->id); trimMemory(); }
    });
    s->failed = !ok; changed(true);
    if (ok && autoRender) requestRender();
    return ok;
}

bool VoiceHost::loadFile(int slot, const juce::File& f, juce::String& err) {
    std::vector<std::vector<float>> ch; int fs = 0, bits = 24; juce::String note;
    if (!decodeFile(f, ch, fs, bits, err, note)) return false;
    const bool ok = setAudio(slot, std::move(ch), fs, f.getFileNameWithoutExtension(), false, &err, bits);
    if (ok && note.isNotEmpty()) { std::lock_guard<std::mutex> l(lock); trimNote[slot] = note; stat = f.getFileNameWithoutExtension() + ": " + note; }
    return ok;
}

void VoiceHost::loadFileAsync(int slot, const juce::File& f) {
    const int gen = cancelGen.load();
    post([this, slot, f, gen] {
        job = 4; progress = -1;
        std::vector<std::vector<float>> ch; int fs = 0, bits = 24; juce::String err, note;
        const bool ok = decodeFile(f, ch, fs, bits, err, note); job = 0;
        if (!ok) { setStatus(err); return; }
        if (gen != cancelGen.load()) return;             // CANCEL was pressed while decoding
        if (setAudio(slot, std::move(ch), fs, f.getFileNameWithoutExtension(), true, &err, bits) && note.isNotEmpty()) {
            std::lock_guard<std::mutex> l(lock); trimNote[slot] = note; stat = f.getFileNameWithoutExtension() + ": " + note; statT = juce::Time::getMillisecondCounter(); }
    });
}

juce::String VoiceHost::trimmedNote(int slot) const { std::lock_guard<std::mutex> l(lock); return trimNote[juce::jlimit(0, 1, slot)]; }

void VoiceHost::clearSlot(int slot, bool undoable) {
    slot = juce::jlimit(0, 1, slot);
    { std::lock_guard<std::mutex> l(lock); if (undoable) pushUndo(true); if (src[slot]) src[slot]->cancel = true; src[slot].reset(); trimNote[slot] = {}; stat = {};
      if (slot == SlotA) res.reset();
      ++contentVersion; trimMemory(); }
    ++resultVersion; changed(true); if (autoRender) requestRender();
}
void VoiceHost::swapSlots() {
    { std::lock_guard<std::mutex> l(lock); pushUndo(true); std::swap(src[0], src[1]); std::swap(trimNote[0], trimNote[1]); res.reset(); ++contentVersion; }
    ++resultVersion; changed(true); if (autoRender) requestRender();
}
bool VoiceHost::useResultAs(int slot, bool async) {
    auto r = result(); if (r == nullptr || r->audio.L.empty()) { setStatus("Nothing rendered to bounce"); return false; }
    std::vector<std::vector<float>> ch; ch.push_back(r->audio.L); if (r->audio.stereo()) ch.push_back(r->audio.R);
    auto a = source(SlotA); const juce::String name = (a ? a->name() : juce::String("voice")) + " bounce";
    return setAudio(slot, std::move(ch), r->audio.fs, name, async);
}
SourcePtr VoiceHost::source(int slot) const { std::lock_guard<std::mutex> l(lock); return src[juce::jlimit(0, 1, slot)]; }

void VoiceHost::enqueueAnalysis(const SourcePtr& s) {
    if (s == nullptr || s->ready() || s->queued.exchange(true)) return;   // never twice: one analysis per sound
    s->cancel = false; s->failed = false;
    post([this, s] {
        bool inSlot; { std::lock_guard<std::mutex> l(lock); inSlot = src[0] == s || src[1] == s; }
        if (s->ready() || !inSlot || s->cancel.load()) { if (!s->ready() && s->cancel.load()) s->failed = true; s->queued = false; return; }
        job = 1; jobStart = nowMs(); progress = 0;
        bool ok = false;
        guarded([&] {
            auto a = std::make_shared<Analysis>();
            ok = analyse(s->pcm->mono.data(), s->pcm->length(), s->pcm->fs, s->opts, *a, { &progress, &s->cancel });
            if (ok && !s->ready()) { s->publish(std::move(a)); std::lock_guard<std::mutex> l(lock); analysedLru.push_front(s->id); trimMemory(); }
        });
        if (!ok) { s->failed = true; bool still; { std::lock_guard<std::mutex> l(lock); still = src[0] == s || src[1] == s; } if (still) setStatus(s->name() + ": analysis stopped (press ANALYSE)"); }
        s->queued = false; job = 0; progress = -1;
        changed(true);
        if (ok && autoRender) requestRender();
    });
}

// ================================================================================================================ settings + undo
RenderParams VoiceHost::params() const { std::lock_guard<std::mutex> l(lock); return prm; }
AnalysisOptions VoiceHost::analysisOptions() const { std::lock_guard<std::mutex> l(lock); return opt; }

void VoiceHost::setParams(const RenderParams& p0, bool undoable, const juce::String& key) {
    RenderParams p = p0; sanitise(p);
    {
        std::lock_guard<std::mutex> l(lock);
        const auto now = juce::Time::getMillisecondCounter();
        const bool coalesce = key.isNotEmpty() && key == lastKey && now - lastKeyMs < 400;
        if (undoable && !coalesce) pushUndo();
        lastKey = key; lastKeyMs = now;
        if (p.mode != prm.mode) stat = {};
        prm = p;
    }
    ++paramsVersion; if (autoRender) requestRender();
}

void VoiceHost::setAnalysisOptions(const AnalysisOptions& o, bool undoable) {
    std::vector<SourcePtr> fresh;
    {
        std::lock_guard<std::mutex> l(lock); if (undoable) pushUndo(true); opt = o;
        opt.f0Floor = juce::jlimit(25.0, 400.0, opt.f0Floor); opt.f0Ceil = juce::jlimit(opt.f0Floor * 2.0, 1600.0, opt.f0Ceil);
        opt.method = juce::jlimit(0, 1, opt.method); opt.voicingThreshold = juce::jlimit(0.05f, 0.95f, opt.voicingThreshold);
        for (auto& s : src) if (s) { s->cancel = true; auto n = makeSource(s->pcm); n->opts = opt; s = n; fresh.push_back(s); }
        ++contentVersion; trimMemory();
    }
    ++paramsVersion; changed(true);
    for (auto& s : fresh) enqueueAnalysis(s);
}

VoiceHost::Snap VoiceHost::snap() const { return { prm, opt, src[0], src[1] }; }   // under lock
void VoiceHost::pushUndo(bool force) {                                               // under lock
    if (!force && inGesture) { if (gesturePushed) return; gesturePushed = true; }
    undoStack.push_back(snap()); if ((int) undoStack.size() > kUndoDepth) undoStack.pop_front(); redoStack.clear();
    trimMemory();
}
// keep the history cheap: analyses only for the slots + the 2 most recently analysed other sounds; the audio that only
// the history keeps alive stays under a budget (the oldest steps go first)
void VoiceHost::trimMemory() {
    std::set<juce::uint32> keep; for (auto& s : src) if (s) keep.insert(s->id);
    int extra = 0; std::deque<juce::uint32> lru;
    for (auto id : analysedLru) { if (std::find(lru.begin(), lru.end(), id) != lru.end()) continue; lru.push_back(id); }
    analysedLru = lru; while ((int) analysedLru.size() > 16) analysedLru.pop_back();
    for (auto id : analysedLru) if (!keep.count(id) && extra < kAnalysisCache) { keep.insert(id); ++extra; }
    auto visit = [&](const SourcePtr& s) { if (s && !keep.count(s->id)) { s->drop(); std::lock_guard<std::mutex> b(s->blobLock); s->blob = {}; } };
    for (auto& sn : undoStack) { visit(sn.a); visit(sn.b); }
    for (auto& sn : redoStack) { visit(sn.a); visit(sn.b); }
    auto historyBytes = [&] {
        std::set<const Pcm*> seen; size_t b = 0; for (auto& s : src) if (s && s->pcm) seen.insert(s->pcm.get());
        auto add = [&](const SourcePtr& s) { if (s && s->pcm && seen.insert(s->pcm.get()).second) b += s->pcm->bytes(); };
        for (auto& sn : undoStack) { add(sn.a); add(sn.b); } for (auto& sn : redoStack) { add(sn.a); add(sn.b); } return b; };
    while (historyBytes() > kUndoAudioBudget && (!undoStack.empty() || !redoStack.empty())) { if (!undoStack.empty()) undoStack.pop_front(); else redoStack.pop_front(); }
}
size_t VoiceHost::undoAudioBytes() const {
    std::lock_guard<std::mutex> l(lock); std::set<const Pcm*> seen; size_t b = 0;
    auto add = [&](const SourcePtr& s) { if (s && s->pcm && seen.insert(s->pcm.get()).second) b += s->pcm->bytes(); };
    for (auto& s : src) add(s);
    for (auto& sn : undoStack) { add(sn.a); add(sn.b); }
    for (auto& sn : redoStack) { add(sn.a); add(sn.b); }
    return b;
}
size_t VoiceHost::liveAnalysisBytes() const {
    std::lock_guard<std::mutex> l(lock); std::set<const Analysis*> seen; size_t b = 0;
    auto add = [&](const SourcePtr& s) { if (!s) return; auto a = s->analysis(); if (a && seen.insert(a.get()).second) b += a->memoryBytes(); };
    for (auto& s : src) add(s);
    for (auto& sn : undoStack) { add(sn.a); add(sn.b); }
    for (auto& sn : redoStack) { add(sn.a); add(sn.b); }
    return b;
}
void VoiceHost::restore(const Snap& s) {
    std::vector<SourcePtr> need;
    { std::lock_guard<std::mutex> l(lock);
      const bool aChanged = src[0] != s.a;
      prm = s.p; opt = s.o; src[0] = s.a; src[1] = s.b; if (aChanged) res.reset(); ++contentVersion;
      for (auto& x : src) if (x && !x->ready()) need.push_back(x); }
    ++paramsVersion; ++resultVersion; changed(true);
    for (auto& x : need) { x->failed = false; x->cancel = false; enqueueAnalysis(x); }
    if (autoRender) requestRender();
}
void VoiceHost::beginGesture() { std::lock_guard<std::mutex> l(lock); inGesture = true; gesturePushed = false; }
void VoiceHost::endGesture() { std::lock_guard<std::mutex> l(lock); inGesture = false; gesturePushed = false; lastKey = {}; }
bool VoiceHost::undo() {
    Snap s; { std::lock_guard<std::mutex> l(lock); if (undoStack.empty()) return false; s = undoStack.back(); undoStack.pop_back(); redoStack.push_back(snap()); lastKey = {}; }
    restore(s); return true;
}
bool VoiceHost::redo() {
    Snap s; { std::lock_guard<std::mutex> l(lock); if (redoStack.empty()) return false; s = redoStack.back(); redoStack.pop_back(); undoStack.push_back(snap()); lastKey = {}; }
    restore(s); return true;
}
bool VoiceHost::canUndo() const { std::lock_guard<std::mutex> l(lock); return !undoStack.empty(); }
bool VoiceHost::canRedo() const { std::lock_guard<std::mutex> l(lock); return !redoStack.empty(); }

Curve VoiceHost::traceContour(double stepSec) const {
    Curve c; auto s = source(SlotA); auto a = s ? s->analysis() : nullptr; if (a == nullptr) return c;
    const auto f0 = effectiveF0(*a, params()); const int F = a->frames();
    const int step = std::max(1, (int) std::lround(stepSec * 1000.0 / a->framePeriod));
    for (int i = 0; i < F;) {
        if (f0[(size_t) i] <= 0) { ++i; continue; }
        int j = i; while (j < F && f0[(size_t) j] > 0) ++j;
        for (int k = i; k < j; k += step) {
            std::vector<double> v; for (int q = k; q < std::min(j, k + step); ++q) v.push_back(f0[(size_t) q]);
            std::sort(v.begin(), v.end()); const int mid = (int) v.size() / 2; c.pts.push_back({ a->frameTime(k + mid), (float) hzMidi(v[(size_t) mid]) });
        }
        c.pts.push_back({ a->frameTime(j - 1), (float) hzMidi(f0[(size_t) j - 1]) });
        i = j;
    }
    c.sanitise(0.0f, 127.0f);
    return c;
}

// ================================================================================================================ worker
void VoiceHost::post(std::function<void()> task) { { std::lock_guard<std::mutex> l(lock); tasks.push_back(std::move(task)); } wake.signal(); }
void VoiceHost::guarded(const std::function<void()>& f) {
    try { f(); }
    catch (const std::bad_alloc&) { setStatus("Out of memory: try a shorter sound, a higher pitch-range floor, or close other projects"); }
    catch (const std::exception& e) { setStatus(juce::String("Error: ") + e.what()); }
    catch (...) { setStatus("Unexpected error in the VOICE worker"); }
}

void VoiceHost::requestRender() { renderWanted = true; if (job.load() == 2) cancelFlag = true; wake.signal(); }
void VoiceHost::cancel() {
    renderWanted = false; cancelFlag = true; ++cancelGen;
    std::deque<std::function<void()>> drop;
    { std::lock_guard<std::mutex> l(lock); drop.swap(tasks); for (auto& x : src) if (x && !x->ready()) { x->cancel = true; x->failed = true; x->queued = false; } }
}
void VoiceHost::reanalyse() {
    std::vector<SourcePtr> need; { std::lock_guard<std::mutex> l(lock); for (auto& x : src) if (x && !x->ready()) need.push_back(x); }
    for (auto& x : need) { if (!x->queued.load()) { x->failed = false; x->cancel = false; } enqueueAnalysis(x); }
    ++sourceVersion;
}

void VoiceHost::run() {
    while (!threadShouldExit()) {
        std::function<void()> t;
        { std::lock_guard<std::mutex> l(lock); if (!tasks.empty()) { t = std::move(tasks.front()); tasks.pop_front(); busyTask = true; } }
        if (t) { cancelFlag = false; guarded(t); busyTask = false; continue; }
        if (renderWanted.exchange(false)) { cancelFlag = false; busyTask = true; guarded([this] { doRender(false, nullptr); }); busyTask = false; continue; }
        wake.wait(200);
    }
}

bool VoiceHost::doRender(bool sync, juce::String* err) {
    RenderParams p; SourcePtr a, b; int pv, cv;
    { std::lock_guard<std::mutex> l(lock); p = prm; a = src[0]; b = src[1]; pv = paramsVersion.load(); cv = contentVersion.load(); }
    if (a == nullptr) { if (err) *err = "slot A is empty"; return false; }
    auto aa = a->analysis(); if (aa == nullptr) { if (err) *err = "slot A is not analysed yet"; return false; }
    if (aa->frames() < 2) { if (err) *err = "slot A is too short"; setStatus(a->name() + " is too short (min 50 ms)"); return false; }
    const bool needB = p.mode == RenderParams::Borrow;
    auto bb = (needB && b) ? b->analysis() : nullptr;
    if (needB && b && bb == nullptr && !sync) return false;    // B's analysis will request again when it lands
    if (!sync) { job = 2; jobStart = nowMs(); progress = 0; }
    auto r = std::make_shared<Result>(); r->params = p; r->paramsVersion = pv; r->sourceVersion = cv;
    std::atomic<bool> never { false };
    const bool ok = render(*aa, a->pcm->ref(), bb.get(), p, r->audio, sync ? Progress { nullptr, &never } : Progress { &progress, &cancelFlag }, &r->info);
    if (!sync) { job = 0; progress = -1; }
    if (!ok) return false;                                       // cancelled (a newer request restarts it)
    if (needB && b == nullptr) setStatus("BORROW: drop a voice into slot B (the formant source)");
    else if (needB && bb) { float peak = 0; for (float v : bb->rms) peak = std::max(peak, v); if (peak < 1e-4f) setStatus("Slot B is silent: nothing to borrow"); }
    double e = 0; for (float v : r->audio.L) e += (double) v * v; for (float v : r->audio.R) e += (double) v * v;
    r->rms = (float) std::sqrt(e / std::max<size_t>(1, r->audio.L.size() + r->audio.R.size()));
    r->serial = ++serial;
    { std::lock_guard<std::mutex> l(lock); res = r; }
    ++resultVersion; publishPlay(); return true;
}

bool VoiceHost::renderNow(juce::String& err) { bool ok = false; guarded([&] { ok = doRender(true, &err); }); if (!ok && err.isEmpty()) err = status(); return ok; }

bool VoiceHost::waitIdle(int timeoutMs) {
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    auto idle = [&] { bool empty; { std::lock_guard<std::mutex> l(lock); empty = tasks.empty(); } return empty && !busyTask.load() && job.load() == 0 && !renderWanted.load(); };
    while (juce::Time::getMillisecondCounter() < end) {
        if (idle()) { juce::Thread::sleep(5); if (idle()) return true; }
        juce::Thread::sleep(5);
    }
    return false;
}

ResultPtr VoiceHost::result() const { std::lock_guard<std::mutex> l(lock); return res; }
bool VoiceHost::resultStale() const {
    std::lock_guard<std::mutex> l(lock); if (src[0] == nullptr) return false; if (res == nullptr) return true;
    return res->paramsVersion != paramsVersion.load() || res->sourceVersion != contentVersion.load();
}
double VoiceHost::secondsLeft() const {
    const int p = progress.load(); if (job.load() == 0 || p <= 2) return -1;
    const double el = (nowMs() - jobStart.load()) / 1000.0; return el * (100.0 - p) / p;
}
juce::String VoiceHost::status() const { std::lock_guard<std::mutex> l(lock); return stat; }
juce::uint32 VoiceHost::statusTime() const { std::lock_guard<std::mutex> l(lock); return statT; }
void VoiceHost::setStatus(const juce::String& s) { std::lock_guard<std::mutex> l(lock); stat = s; statT = juce::Time::getMillisecondCounter(); }
void VoiceHost::changed(bool sourcesChanged) { if (sourcesChanged) ++sourceVersion; publishPlay(); }
juce::String VoiceHost::uiPrefs() const { std::lock_guard<std::mutex> l(lock); return ui; }
void VoiceHost::setUiPrefs(const juce::String& s) { std::lock_guard<std::mutex> l(lock); ui = s; }

// ================================================================================================================ export
juce::File VoiceHost::exportDir() { auto d = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("GhostyVoice"); d.createDirectory(); return d; }
juce::File VoiceHost::sharedSoundsDir() { auto d = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Ghosty").getChildFile("Shared Sounds"); d.createDirectory(); return d; }
void VoiceHost::cleanExportDir(double maxAgeHours) {
    const auto cutoff = juce::Time::getCurrentTime() - juce::RelativeTime::hours(maxAgeHours);
    for (const auto& e : juce::RangedDirectoryIterator(exportDir(), false, "*", juce::File::findFilesAndDirectories))
        if (e.getModificationTime() < cutoff) { if (e.isDirectory()) e.getFile().deleteRecursively(); else e.getFile().deleteFile(); }
}

bool VoiceHost::writeWav(const juce::File& f, const Rendered& x) {
    if (x.L.empty() || x.fs <= 0) return false;
    f.deleteFile(); std::unique_ptr<juce::FileOutputStream> os(f.createOutputStream()); if (os == nullptr) return false;
    juce::WavAudioFormat wav; const int nch = x.stereo() ? 2 : 1;
    std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(os.get(), x.fs, (unsigned) nch, 24, {}, 0)); if (w == nullptr) return false;   // GATE: via-caller (VoicePage asked spec.voice.*)
    os.release();
    const float* ch[2] = { x.L.data(), x.stereo() ? x.R.data() : nullptr }; return w->writeFromFloatArrays(ch, nch, (int) x.L.size());
}

juce::File VoiceHost::writeResultWav(juce::String& why, bool renderIfStale) {
    if (renderIfStale && resultStale()) { juce::String err; if (!renderNow(err)) { why = err.isNotEmpty() ? err : juce::String("Nothing rendered yet"); return {}; } }
    auto r = result(); if (r == nullptr || r->audio.L.empty()) { why = "Nothing rendered yet"; return {}; }
    auto a = source(SlotA); const auto& p = r->params;
    juce::String base = juce::File::createLegalFileName(a ? a->name() : juce::String("voice")).trim(); if (base.isEmpty()) base = "voice";
    base << " " << kModeNames[juce::jlimit(0, 4, p.mode)];
    if (std::abs(p.pitchSemis) > 1e-4f) base << " " << (p.pitchSemis > 0 ? "+" : "") << juce::String(p.pitchSemis, std::abs(p.pitchSemis - std::round(p.pitchSemis)) > 1e-3f ? 2 : 0) << "st";
    if (std::abs(p.formantSemis) > 1e-4f) base << " F" << (p.formantSemis > 0 ? "+" : "") << juce::String(p.formantSemis, 1);
    if (std::abs(p.speed - 1.0f) > 1e-4f) base << " x" << juce::String(p.speed, 2);
    auto f = exportDir().getNonexistentChildFile(base, ".wav", true);
    if (!writeWav(f, r->audio)) { why = "Couldn't write " + f.getFullPathName(); return {}; }
    return f;
}

void VoiceHost::renderSet(std::vector<int> semis, bool toShared) {
    post([this, semis, toShared] {
        RenderParams p; SourcePtr a, b; { std::lock_guard<std::mutex> l(lock); p = prm; a = src[0]; b = src[1]; }
        auto aa = a ? a->analysis() : nullptr; if (aa == nullptr) { setStatus("RENDER SET: slot A isn't ready"); return; }
        auto bb = (p.mode == RenderParams::Borrow && b) ? b->analysis() : nullptr;
        const juce::String name = juce::File::createLegalFileName(a->name()).trim();
        auto dir = (toShared ? sharedSoundsDir() : exportDir()).getChildFile(name + " set").getNonexistentSibling(true); dir.createDirectory();
        job = 3; jobStart = nowMs(); progress = 0; const double med = aa->medianF0(); int written = 0;
        for (size_t i = 0; i < semis.size(); ++i) {
            if (cancelFlag.load()) break;
            RenderParams q = p; q.pitchSemis = p.pitchSemis + (float) semis[i]; Rendered out;
            if (!render(*aa, a->pcm->ref(), bb.get(), q, out, { nullptr, &cancelFlag }, nullptr)) break;
            juce::String fn = name + " " + (semis[i] >= 0 ? "+" : "-") + juce::String(std::abs(semis[i])).paddedLeft('0', 2);
            if (med > 0) fn << " " << noteName((int) std::lround(hzMidi(med) + q.pitchSemis));
            if (writeWav(dir.getChildFile(juce::File::createLegalFileName(fn) + ".wav"), out)) ++written;
            progress = (int) (100 * (i + 1) / semis.size());
        }
        job = 0; progress = -1;
        { std::lock_guard<std::mutex> l(lock); setFolder = dir; }
        setStatus("RENDER SET: " + juce::String(written) + " files in " + dir.getFullPathName());
        ++resultVersion;
    });
}
juce::File VoiceHost::lastSetFolder() const { std::lock_guard<std::mutex> l(lock); return setFolder; }

// ================================================================================================================ state
namespace {
juce::String fixesToString(const std::vector<Fix>& f) { juce::String s; for (auto& x : f) s << juce::String(x.t0, 4) << ":" << juce::String(x.t1, 4) << ":" << juce::String(x.semis, 3) << ":" << x.voicing << ";"; return s; }
std::vector<Fix> fixesFromString(const juce::String& s) {
    std::vector<Fix> out;
    for (auto& tok : juce::StringArray::fromTokens(s, ";", "")) { auto p = juce::StringArray::fromTokens(tok, ":", ""); if (p.size() != 4) continue;
        Fix f { p[0].getDoubleValue(), p[1].getDoubleValue(), p[2].getFloatValue(), juce::jlimit(-1, 1, p[3].getIntValue()) };
        if (std::isfinite(f.t0) && std::isfinite(f.t1) && std::isfinite(f.semis)) out.push_back(f);
        if (out.size() >= 2000) break; }
    return out;
}
} // namespace

void VoiceHost::sanitise(RenderParams& p) {
    auto cf = [](float& v, float lo, float hi, float def) { v = std::isfinite(v) ? juce::jlimit(lo, hi, v) : def; };
    p.mode = juce::jlimit(0, 4, p.mode); cf(p.pitchSemis, -24, 24, 0); cf(p.formantSemis, -12, 12, 0); cf(p.speed, 0.25f, 4, 1); cf(p.breath, -1, 1, 0); cf(p.voicing, 0.05f, 0.95f, 0.45f);
    p.timeMode = juce::jlimit(0, 2, p.timeMode); p.carrier = juce::jlimit(0, 2, p.carrier); cf(p.borrowMix, 0, 1, 1); cf(p.dynamics, 0, 1, 0.7f); cf(p.breathFromB, 0, 1, 0.6f);
    p.root = juce::jlimit(0, 11, p.root); p.scale = juce::jlimit(0, 6, p.scale); cf(p.contourAmount, 0, 1, 1); cf(p.retuneMs, 0, 500, 40); cf(p.flatten, 0, 1, 0); cf(p.humanize, 0, 1, 0);
    cf(p.vowelAmount, 0, 1, 1); cf(p.vowelScale, 0.7f, 1.4f, 1); p.vowelSet = juce::jlimit(0, 2, p.vowelSet); for (auto& f : p.customF) cf(f, 150, 5000, 1000);
    p.doubles = p.doubles < 2 ? 0 : juce::jlimit(2, 8, p.doubles); cf(p.doubleCents, 0, 50, 12); cf(p.doubleMs, 0, 60, 15); cf(p.doubleFormant, 0, 2, 0.3f); cf(p.width, 0, 1, 0.8f);
    for (auto& h : p.harmony) { h.steps = juce::jlimit(-14, 14, h.steps); cf(h.level, 0, 1, 0.7f); cf(h.pan, -1, 1, 0); }
    cf(p.gainDb, -24, 12, 0); cf(p.transientSens, 0, 1, 0.5f);
    if (!std::isfinite(p.regionStart) || !std::isfinite(p.regionEnd) || p.regionEnd <= p.regionStart) { p.regionStart = p.regionEnd = -1; }
    if (!std::isfinite(p.trimStart) || !std::isfinite(p.trimEnd) || (p.trimEnd > 0 && p.trimEnd <= std::max(0.0, p.trimStart) + kMinSeconds)) { p.trimStart = p.trimEnd = -1; }
    p.contour.sanitise(12.0f, 120.0f); p.lane.sanitise(0.0f, (float) kVowels); p.pitchLane.sanitise(-24, 24); p.formantLane.sanitise(-12, 12); p.breathLane.sanitise(-1, 1);
    p.fixes.erase(std::remove_if(p.fixes.begin(), p.fixes.end(), [](const Fix& f) { return !std::isfinite(f.t0) || !std::isfinite(f.t1) || !std::isfinite(f.semis); }), p.fixes.end());
    for (auto& f : p.fixes) { f.semis = juce::jlimit(-36.0f, 36.0f, f.semis); f.voicing = juce::jlimit(-1, 1, f.voicing); }
    if (p.fixes.size() > 2000) p.fixes.resize(2000);
}

juce::ValueTree VoiceHost::paramsToTree(const RenderParams& p) {
    juce::ValueTree t("PARAMS");
    auto set = [&](const char* k, const juce::var& v) { t.setProperty(k, v, nullptr); };
    set("mode", p.mode); set("pitch", p.pitchSemis); set("keep", p.keepFormants); set("formant", p.formantSemis); set("speed", p.speed); set("breath", p.breath); set("voicing", p.voicing);
    set("fixes", fixesToString(p.fixes)); set("pitchLane", p.pitchLane.save()); set("formantLane", p.formantLane.save()); set("breathLane", p.breathLane.save());
    set("r0", p.regionStart); set("r1", p.regionEnd); if (p.trimStart >= 0 || p.trimEnd > 0) { set("trim0", p.trimStart); set("trim1", p.trimEnd); } set("trans", p.transients); set("transSens", p.transientSens);
    set("time", p.timeMode); set("carrier", p.carrier); set("mix", p.borrowMix); set("dyn", p.dynamics); set("breathB", p.breathFromB); set("cons", p.consonantsFromB);
    set("contour", p.contour.save()); set("snap", p.contourSnap); set("root", p.root); set("scale", p.scale); set("camt", p.contourAmount); set("force", p.forceVoiced);
    set("retune", p.retuneMs); set("flatten", p.flatten); set("humanize", p.humanize);
    set("lane", p.lane.save()); set("vamt", p.vowelAmount); set("vsize", p.vowelScale); set("vset", p.vowelSet); set("vcustom", p.hasCustomVowel);
    set("vcf", juce::String(p.customF[0]) + "," + juce::String(p.customF[1]) + "," + juce::String(p.customF[2]) + "," + juce::String(p.customF[3]));
    set("doubles", p.doubles); set("dcents", p.doubleCents); set("dms", p.doubleMs); set("dform", p.doubleFormant); set("width", p.width);
    juce::String h; for (auto& x : p.harmony) h << x.steps << ":" << juce::String(x.level, 3) << ":" << juce::String(x.pan, 3) << ";"; set("harmony", h);
    set("gain", p.gainDb); set("norm", p.normalise);
    return t;
}
RenderParams VoiceHost::paramsFromTree(const juce::ValueTree& t) {
    RenderParams p, d;
    auto f = [&](const char* k, float def) { const auto v = t.getProperty(k); if (v.isVoid()) return def; const double x = (double) v; return std::isfinite(x) ? (float) x : def; };
    auto i = [&](const char* k, int def) { return (int) t.getProperty(k, def); };
    auto b = [&](const char* k, bool def) { return (bool) t.getProperty(k, def); };
    p.mode = i("mode", d.mode); p.pitchSemis = f("pitch", 0); p.keepFormants = b("keep", d.keepFormants); p.formantSemis = f("formant", 0); p.speed = f("speed", 1); p.breath = f("breath", 0); p.voicing = f("voicing", d.voicing);
    p.fixes = fixesFromString(t.getProperty("fixes").toString()); p.pitchLane.load(t.getProperty("pitchLane").toString()); p.formantLane.load(t.getProperty("formantLane").toString()); p.breathLane.load(t.getProperty("breathLane").toString());
    p.regionStart = f("r0", -1); p.regionEnd = f("r1", -1); p.trimStart = f("trim0", -1); p.trimEnd = f("trim1", -1); p.transients = b("trans", true); p.transientSens = f("transSens", d.transientSens);
    p.timeMode = i("time", d.timeMode); p.carrier = i("carrier", d.carrier); p.borrowMix = f("mix", d.borrowMix); p.dynamics = f("dyn", d.dynamics); p.breathFromB = f("breathB", d.breathFromB); p.consonantsFromB = b("cons", d.consonantsFromB);
    p.contour.load(t.getProperty("contour").toString()); p.contourSnap = b("snap", false); p.root = i("root", 0); p.scale = i("scale", 0); p.contourAmount = f("camt", 1); p.forceVoiced = b("force", false);
    p.retuneMs = f("retune", d.retuneMs); p.flatten = f("flatten", 0); p.humanize = f("humanize", 0);
    p.lane.load(t.getProperty("lane").toString()); p.vowelAmount = f("vamt", 1); p.vowelScale = f("vsize", 1); p.vowelSet = i("vset", 0); p.hasCustomVowel = b("vcustom", false);
    { auto v = juce::StringArray::fromTokens(t.getProperty("vcf").toString(), ",", ""); if (v.size() == 4) for (int k = 0; k < 4; ++k) p.customF[(size_t) k] = v[k].getFloatValue(); }
    p.doubles = i("doubles", 0); p.doubleCents = f("dcents", d.doubleCents); p.doubleMs = f("dms", d.doubleMs); p.doubleFormant = f("dform", d.doubleFormant); p.width = f("width", d.width);
    { auto hs = juce::StringArray::fromTokens(t.getProperty("harmony").toString(), ";", ""); int k = 0;
      for (auto& h : hs) { if (h.isEmpty() || k >= 3) continue; auto q = juce::StringArray::fromTokens(h, ":", ""); if (q.size() == 3) p.harmony[(size_t) k] = { q[0].getIntValue(), q[1].getFloatValue(), q[2].getFloatValue() }; ++k; } }
    p.gainDb = f("gain", 0); p.normalise = b("norm", true);
    sanitise(p); return p;
}

juce::String VoiceHost::encode(const Source& s) {
    std::lock_guard<std::mutex> l(s.blobLock);
    if (s.blob.isNotEmpty() || s.pcm == nullptr) return s.blob;
    juce::MemoryBlock mb; const auto& pc = *s.pcm; const int nch = pc.channels(); const int bits = pc.bits <= 16 ? 16 : 24;
    {
        juce::FlacAudioFormat flac;
        auto* os = new juce::MemoryOutputStream(mb, false);
        std::unique_ptr<juce::AudioFormatWriter> w(flac.createWriterFor(os, pc.fs, (unsigned) nch, bits, {}, 5));   // NOT-EXPORT: the user's own source sound inside the project state (memory)
        if (w == nullptr) { delete os; return {}; }
        std::vector<std::vector<float>> clipped; for (int c = 0; c < nch; ++c) clipped.emplace_back(pc.channel(c), pc.channel(c) + pc.length());
        for (auto& c : clipped) for (auto& v : c) v = juce::jlimit(-1.0f, 1.0f, v);
        std::vector<const float*> ch; for (auto& c : clipped) ch.push_back(c.data());
        w->writeFromFloatArrays(ch.data(), nch, pc.length());
    }
    s.blob = mb.toBase64Encoding(); return s.blob;
}
bool VoiceHost::decodeBlob(const juce::String& b64, std::vector<std::vector<float>>& ch, int& fs, int& bits) {
    juce::MemoryBlock mb; if (b64.isEmpty() || !mb.fromBase64Encoding(b64) || mb.getSize() == 0) return false;
    juce::FlacAudioFormat flac;
    std::unique_ptr<juce::AudioFormatReader> r(flac.createReaderFor(new juce::MemoryInputStream(mb, false), true));
    if (r == nullptr || r->lengthInSamples <= 0 || r->sampleRate < 1000 || r->sampleRate > 768000 || r->numChannels < 1) return false;
    const int n = (int) std::min<juce::int64>(r->lengthInSamples, (juce::int64) (kMaxSeconds * r->sampleRate));   // capped before allocating
    const int nch = (int) std::min(2u, r->numChannels);
    juce::AudioBuffer<float> b((int) r->numChannels, n); r->read(&b, 0, n, 0, true, true);
    ch.assign((size_t) nch, {}); for (int c = 0; c < nch; ++c) ch[(size_t) c].assign(b.getReadPointer(c), b.getReadPointer(c) + n);
    fs = (int) std::lround(r->sampleRate); bits = (int) r->bitsPerSample <= 16 ? 16 : 24; return fs > 0;
}

juce::ValueTree VoiceHost::toTree() const {
    RenderParams p; AnalysisOptions o; SourcePtr s[2]; juce::String u;
    { std::lock_guard<std::mutex> l(lock); p = prm; o = opt; s[0] = src[0]; s[1] = src[1]; u = ui; }
    juce::ValueTree t("VOICE"); t.setProperty("version", 2, nullptr); t.setProperty("ui", u, nullptr); t.setProperty("auto", autoRender.load(), nullptr);
    { const auto lp = loopRange(); if (lp.second > lp.first) { t.setProperty("loop0", lp.first, nullptr); t.setProperty("loop1", lp.second, nullptr); t.setProperty("loopOn", loopEnabled(), nullptr); } }
    t.appendChild(paramsToTree(p), nullptr);
    juce::ValueTree a("ANALYSIS"); a.setProperty("method", o.method, nullptr); a.setProperty("floor", o.f0Floor, nullptr); a.setProperty("ceil", o.f0Ceil, nullptr);
    a.setProperty("voicing", o.voicingThreshold, nullptr); t.appendChild(a, nullptr);
    for (int i = 0; i < 2; ++i) if (s[i] && s[i]->pcm) {
        juce::ValueTree c("SLOT"); c.setProperty("index", i, nullptr); c.setProperty("name", s[i]->name(), nullptr); c.setProperty("fs", s[i]->fs(), nullptr);
        c.setProperty("flac", encode(*s[i]), nullptr); t.appendChild(c, nullptr);
    }
    return t;
}

void VoiceHost::fromTree(const juce::ValueTree& t) {
    if (!t.hasType("VOICE")) return;
    cancel();
    RenderParams p = paramsFromTree(t.getChildWithName("PARAMS"));
    AnalysisOptions o; const auto a = t.getChildWithName("ANALYSIS");
    if (a.isValid()) { o.method = juce::jlimit(0, 1, (int) a.getProperty("method", 0)); o.f0Floor = juce::jlimit(25.0, 400.0, (double) a.getProperty("floor", 71.0));
                       o.f0Ceil = juce::jlimit(o.f0Floor * 2.0, 1600.0, (double) a.getProperty("ceil", 800.0)); o.voicingThreshold = juce::jlimit(0.05f, 0.95f, (float) (double) a.getProperty("voicing", 0.45));
                       if (!std::isfinite(o.f0Floor) || !std::isfinite(o.f0Ceil)) { o.f0Floor = 71; o.f0Ceil = 800; } }
    PcmPtr pc[2]; juce::String notes[2];
    for (const auto& c : t) if (c.hasType("SLOT")) {
        const int i = juce::jlimit(0, 1, (int) c.getProperty("index", 0)); std::vector<std::vector<float>> ch; int fs = 0, bits = 24;
        try { if (decodeBlob(c.getProperty("flac").toString(), ch, fs, bits)) pc[i] = makePcm(std::move(ch), fs, c.getProperty("name").toString(), bits, &notes[i]); }
        catch (const std::bad_alloc&) { setStatus("Out of memory restoring the VOICE sounds"); }
        if (pc[i] && pc[i]->seconds() < kMinSeconds) pc[i].reset();
    }
    SourcePtr s[2];
    {
        std::lock_guard<std::mutex> l(lock); opt = o;
        for (int i = 0; i < 2; ++i) if (pc[i]) s[i] = makeSource(pc[i]);
        for (auto& x : src) if (x) x->cancel = true;
        prm = p; src[0] = s[0]; src[1] = s[1]; res.reset(); undoStack.clear(); redoStack.clear(); trimNote[0] = notes[0]; trimNote[1] = notes[1];
        ui = t.getProperty("ui").toString(); stat = {}; ++contentVersion;
    }
    autoRender = (bool) t.getProperty("auto", true);
    { const double l0 = (double) t.getProperty("loop0", -1.0), l1 = (double) t.getProperty("loop1", -1.0);   // old sessions: no loop
      if (std::isfinite(l0) && std::isfinite(l1) && l1 > l0 && l0 >= 0) setLoop((bool) t.getProperty("loopOn", false), l0, l1); else setLoop(false, -1, -1); }
    ++paramsVersion; ++resultVersion; changed(true);
    for (auto& x : s) if (x) enqueueAnalysis(x);
}

// ================================================================================================================ preview player
void VoiceHost::publishPlay() {
    auto it = std::make_unique<PlayItem>();
    { std::lock_guard<std::mutex> l(lock); it->r = res; it->a = src[0] ? src[0]->pcm : nullptr; it->b = src[1] ? src[1]->pcm : nullptr; }
    if (levelMatch.load() && it->r && it->r->rms > 1e-6f) {
        if (it->a && it->a->rms > 1e-6f) it->gainA = juce::jlimit(1.0f / 16, 16.0f, it->r->rms / it->a->rms);
        if (it->b && it->b->rms > 1e-6f) it->gainB = juce::jlimit(1.0f / 16, 16.0f, it->r->rms / it->b->rms);
    }
    std::lock_guard<std::mutex> l(publishLock); items.publish(std::move(it));
}
void VoiceHost::prepare(double sampleRate, int) { hostRate = sampleRate > 0 ? sampleRate : 48000.0; }
void VoiceHost::applyLoopFor(int what) {
    const auto lp = loopRange();
    if (lp.first < 0 || lp.second <= lp.first) { loopS0 = -1; loopS1 = -1; return; }
    if (what == PlayResult) { auto r = result(); if (r) { loopS0 = r->info.sourceToOutput(lp.first); loopS1 = r->info.sourceToOutput(lp.second); return; } }
    loopS0 = lp.first; loopS1 = lp.second;
}
void VoiceHost::play(int what, double fromSeconds) { applyLoopFor(what); playFrom = std::max(0.0, fromSeconds); playWhatReq = what; ++playReq; }
void VoiceHost::setLoop(bool on, double s0, double s1) { { std::lock_guard<std::mutex> l(loopLock); loopSrc0 = s0; loopSrc1 = s1; } loopOn = on; applyLoopFor(playing.load()); }
std::pair<double, double> VoiceHost::loopRange() const { std::lock_guard<std::mutex> l(loopLock); return { loopSrc0, loopSrc1 }; }

juce::File VoiceHost::writeRangeWav(double srcA, double srcB, juce::String& why) {
    if (resultStale()) { juce::String err; renderNow(err); }
    auto r = result(); auto a = source(SlotA); Rendered x;
    if (r && !r->audio.L.empty()) {
        const int fs = r->audio.fs, n = (int) r->audio.L.size();
        const int o0 = juce::jlimit(0, n, (int) std::lround(r->info.sourceToOutput(srcA) * fs)), o1 = juce::jlimit(o0, n, (int) std::lround(r->info.sourceToOutput(srcB) * fs));
        x.fs = fs; x.L.assign(r->audio.L.begin() + o0, r->audio.L.begin() + o1); if (r->audio.stereo()) x.R.assign(r->audio.R.begin() + o0, r->audio.R.begin() + o1);
    } else if (a && a->pcm) {
        const auto& pc = *a->pcm; const int n = pc.length(), s0 = juce::jlimit(0, n, (int) std::lround(srcA * pc.fs)), s1 = juce::jlimit(s0, n, (int) std::lround(srcB * pc.fs));
        x.fs = pc.fs; x.L.assign(pc.channel(0) + s0, pc.channel(0) + s1); if (pc.channels() == 2) x.R.assign(pc.channel(1) + s0, pc.channel(1) + s1);
    }
    if (x.L.size() < 16) { why = "Nothing to export there"; return {}; }
    const juce::String base = juce::File::createLegalFileName((a ? a->name() : juce::String("voice")) + " part " + juce::String(srcA, 2) + "-" + juce::String(srcB, 2) + "s").trim();
    auto f = exportDir().getNonexistentChildFile(base, ".wav", true);
    if (!writeWav(f, x)) { why = "Couldn't write " + f.getFullPathName(); return {}; }
    return f;
}
void VoiceHost::toggleAB() {
    const int what = playing.load(); const double t = std::max(0.0, playPos.load()); auto r = result();
    if (what == PlayResult) play(PlayA, r ? r->info.outputToSource(t) : t);
    else if (what == PlayA) play(r ? PlayResult : PlayA, r ? r->info.sourceToOutput(t) : t);
    else play(r ? PlayResult : PlayA, 0.0);
}

void VoiceHost::process(juce::AudioBuffer<float>& out) {
    const PlayItem* it = items.get();
    const int req = playReq.load();
    if (req != playSeen) { playSeen = req; pendingWhat = playWhatReq.load(); pendingFrom = playFrom.load(); switching = true; }
    if (it == nullptr) { if (playing.load() != PlayNone) { playing = PlayNone; playPos = -1.0; } cur = PlayNone; return; }
    const int n = out.getNumSamples(), nch = out.getNumChannels(); const float pg = previewGain.load(); const float fadeStep = (float) (1.0 / (0.004 * hostRate));
    auto sourceOf = [&](int what, const float*& l, const float*& r, int& len, int& fs, float& g) {
        l = r = nullptr; len = 0; fs = 0; g = 1;
        if (what == PlayResult && it->r && !it->r->audio.L.empty()) { l = it->r->audio.L.data(); r = it->r->audio.stereo() ? it->r->audio.R.data() : l; len = (int) it->r->audio.L.size(); fs = it->r->audio.fs; g = it->gainR; }
        else if (what == PlayA && it->a) { l = it->a->channel(0); r = it->a->channel(1); len = it->a->length(); fs = it->a->fs; g = it->gainA; }
        else if (what == PlayB && it->b) { l = it->b->channel(0); r = it->b->channel(1); len = it->b->length(); fs = it->b->fs; g = it->gainB; }
    };
    const float* L; const float* R; int len, fs; float g; sourceOf(cur, L, R, len, fs, g);
    if (cur != PlayNone && L == nullptr) cur = PlayNone;
    if (cur == PlayNone && fade > 0) fade = 0;
    auto hermite = [](const float* x, int count, double p) {
        const int i = (int) p; const float t = (float) (p - i);
        auto at = [&](int k) { return k < 0 || k >= count ? 0.0f : x[k]; };
        const float y0 = at(i - 1), y1 = at(i), y2 = at(i + 1), y3 = at(i + 2);
        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    };
    const bool loop = loopOn.load(); const double ls0 = loopS0.load(), ls1 = loopS1.load();
    for (int i = 0; i < n; ++i) {
        if (switching) {   // fade the old sound out (4 ms), then start the new one
            if (cur == PlayNone || fade <= 0.0f) {
                cur = pendingWhat; switching = false; fade = 0; sourceOf(cur, L, R, len, fs, g);
                if (L == nullptr) cur = PlayNone; else pos = pendingFrom * fs;
            } else fade = std::max(0.0f, fade - fadeStep);
        } else if (cur != PlayNone) fade = std::min(1.0f, fade + fadeStep);
        if (cur == PlayNone) continue;
        if (loop && ls1 > ls0 && pos >= ls1 * fs && !switching) { pendingWhat = cur; pendingFrom = std::max(0.0, ls0); switching = true; }
        if (pos >= len - 1) { if (loop && !switching) { pendingWhat = cur; pendingFrom = std::max(0.0, ls0); switching = true; fade = 0; } else if (!loop) { cur = PlayNone; continue; } }
        const float gg = g * pg * fade;
        const float vl = hermite(L, len, pos) * gg, vr = hermite(R, len, pos) * gg;
        if (nch >= 2) { out.addSample(0, i, vl); out.addSample(1, i, vr); for (int c = 2; c < nch; ++c) out.addSample(c, i, 0.5f * (vl + vr)); }
        else if (nch == 1) out.addSample(0, i, 0.5f * (vl + vr));
        pos += fs / hostRate;
    }
    playing = cur; playPos = cur == PlayNone || fs <= 0 ? -1.0 : pos / fs;
}
} // namespace voice
