#include "ArcsHost.h"

namespace arcs {

// ---------------------------------------------------------------- the background render job (streams to disk)
struct ArcsHost::Job : juce::Thread {
    ArcsHost& h; std::unique_ptr<Snapshot> snap; double sr, bpm; juce::File out;
    Job(ArcsHost& host, std::unique_ptr<Snapshot> s, double sampleRate, double tempo, juce::File f)
        : juce::Thread("ARCS render"), h(host), snap(std::move(s)), sr(sampleRate), bpm(tempo), out(std::move(f)) {}
    void run() override {
        bool ok = false; juce::String err;
        try {
            out.getParentDirectory().createDirectory(); out.deleteFile();
            juce::WavAudioFormat wav; std::unique_ptr<juce::OutputStream> os(out.createOutputStream().release());
            std::unique_ptr<juce::AudioFormatWriter> w;
            if (os) if (auto* raw = wav.createWriterFor(os.get(), sr, 2, 24, {}, 0)) { os.release(); w.reset(raw); }   // GATE: via-caller (ArcsPage asked spec.arcs.audio-render)
            if (!w) err = "Could not write " + out.getFullPathName();
            else {
                bool writeOk = true;
                const bool done = Engine::render(*snap, sr, bpm, [&](const float* l, const float* r, int n) { const float* ch[2] = { l, r }; writeOk = w->writeFromFloatArrays(ch, 2, n); return writeOk && !threadShouldExit(); }, &h.progress, &h.jobAbort);
                ok = done && writeOk; if (!writeOk) err = "The disk refused the audio (full?)";
            }
        } catch (const std::bad_alloc&) { err = "Not enough memory to render"; ok = false; }
        catch (...) { err = "Render failed"; ok = false; }
        if (!ok) out.deleteFile();
        h.jobError = err; h.jobOk.store(ok); h.progress.store(100); h.jobDone.store(true);
    }
};

ArcsHost::ArcsHost() {
    // the timer frees retired snapshots: a host that builds processors off the message thread only reclaims on edits
    JUCE_ASSERT_MESSAGE_THREAD
    for (int s = 0; s < kSlots; ++s) { bank[(size_t) s].committed = bank[(size_t) s].page; publishSlot(s); }
    startTimer(200);
}
ArcsHost::~ArcsHost() { alive->store(false); stopTimer(); cancelRender(); }

void ArcsHost::timerCallback() {
    engine.collect();
    const int playing = engine.playingSlot.load();   // MIDI notes 0..7 switched the page on the audio side: follow it
    if (playing != cur && playing == engine.slotRequest.load()) { cur = playing; ++edits; }
}

// ---------------------------------------------------------------- audio
void ArcsHost::prepare(double sampleRate, int maxBlock) { engine.prepare(sampleRate, maxBlock); scratchL.assign((size_t) juce::jmax(512, maxBlock), 0.0f); scratchR = scratchL; }

void ArcsHost::process(float* L, float* R, int n, const HostInfo& host, bool add, const juce::MidiBuffer* midi) {
    lastHostBpm.store(host.bpm > 1 && std::isfinite(host.bpm) ? host.bpm : 0.0, std::memory_order_relaxed);
    if (L != R) { engine.process(L, R, n, host, add, midi); return; }
    // mono bus: fold at -3 dB through the scratch buffers (sized in prepare; a bigger block than promised is cut in pieces)
    if (!add) std::fill(L, L + n, 0.0f);
    const int cap = (int) scratchL.size(); if (cap <= 0) return;
    for (int pos = 0; pos < n; pos += cap) {
        const int m = juce::jmin(cap, n - pos);
        engine.process(scratchL.data(), scratchR.data(), m, host, false, pos == 0 ? midi : nullptr);
        for (int i = 0; i < m; ++i) L[pos + i] += 0.70710678f * (scratchL[(size_t) i] + scratchR[(size_t) i]);
    }
}
void ArcsHost::process(juce::AudioBuffer<float>& b, const HostInfo& host, bool add, const juce::MidiBuffer* midi) {
    if (b.getNumChannels() <= 0 || b.getNumSamples() <= 0) return;
    float* L = b.getWritePointer(0); float* R = b.getNumChannels() > 1 ? b.getWritePointer(1) : L;
    process(L, R, b.getNumSamples(), host, add, midi);
}
HostInfo ArcsHost::readPlayHead(juce::AudioPlayHead* ph) {
    HostInfo h; h.bpm = 0; h.ppq = 0; h.playing = false;
    if (ph != nullptr) if (auto pos = ph->getPosition()) {
        if (auto b = pos->getBpm()) h.bpm = *b;
        if (auto q = pos->getPpqPosition()) h.ppq = *q;
        if (auto t = pos->getTimeInSeconds()) h.timeSec = *t;
        h.playing = pos->getIsPlaying();
    }
    return h;
}

// ---------------------------------------------------------------- edits / undo / bank
void ArcsHost::publishSlot(int s) {
    auto& sl = bank[(size_t) s]; double ms = 0; int rb = 0;
    engine.publish(buildSnapshot(sl.page, caches[(size_t) s], &ms, &rb), s); caches[(size_t) s].prune(sl.page);
    if (s == cur) { publishMs = ms; rebuiltLines = rb; }
}
void ArcsHost::changed() { ++edits; publishSlot(cur); }
void ArcsHost::commit(const juce::String&) {
    auto& s = bank[(size_t) cur]; s.page.clampAll();
    s.undo.push_back(s.committed); while (s.undo.size() > kUndoDepth) s.undo.pop_front();
    s.committed = s.page; s.redo.clear(); changed();
}
bool ArcsHost::undo() {
    auto& s = bank[(size_t) cur]; if (s.undo.empty()) return false;
    s.redo.push_back(s.page); s.page = s.undo.back(); s.undo.pop_back(); s.committed = s.page; changed(); return true;
}
bool ArcsHost::redo() {
    auto& s = bank[(size_t) cur]; if (s.redo.empty()) return false;
    s.undo.push_back(s.page); s.page = s.redo.back(); s.redo.pop_back(); s.committed = s.page; changed(); return true;
}
void ArcsHost::selectSlot(int s) {
    s = juce::jlimit(0, kSlots - 1, s); if (s == cur) return;
    // the slot keeps its page sound: take over the transport settings that belong to the instance
    cur = s; publishSlot(cur); engine.slotRequest.store(cur); ++edits;
}

// ---------------------------------------------------------------- state
juce::ValueTree ArcsHost::getState() const {
    auto t = page().toTree(); t.setProperty("uiDark", dark, nullptr); t.setProperty("slot", cur, nullptr);
    juce::ValueTree b("BANK");
    for (int s = 0; s < kSlots; ++s) if (s != cur && !bank[(size_t) s].page.arcs.empty()) { auto c = bank[(size_t) s].page.toTree(); c.setProperty("slot", s, nullptr); b.appendChild(c, nullptr); }
    if (b.getNumChildren() > 0) t.appendChild(b, nullptr);
    return t;
}
void ArcsHost::setState(const juce::ValueTree& t) {
    if (juce::MessageManager::getInstanceWithoutCreating() != nullptr && !juce::MessageManager::existsAndIsCurrentThread()) {
        // hosts may restore state from a loader thread while the editor runs: apply it on the message thread
        auto copy = t.createCopy(); auto flag = alive;
        juce::MessageManager::callAsync([this, copy, flag] { if (flag->load()) applyState(copy); });
        return;
    }
    applyState(t);
}
void ArcsHost::applyState(const juce::ValueTree& t) {
    if (!t.hasType("ARCSPAGE")) return;
    LoadInfo li; Page p = Page::fromTree(t, &li);
    if (li.futureVersion) { message = li.message(); return; }
    std::array<Page, kSlots> pages; const int s0 = juce::jlimit(0, kSlots - 1, (int) t.getProperty("slot", 0)); pages[(size_t) s0] = p;
    juce::StringArray msgs; if (li.message().isNotEmpty()) msgs.add(li.message());
    for (const auto& c : t.getChildWithName("BANK")) { const int s = (int) c.getProperty("slot", -1); if (s < 0 || s >= kSlots || s == s0) continue;
        LoadInfo l2; Page q = Page::fromTree(c, &l2); if (l2.ok) pages[(size_t) s] = q; if (l2.message().isNotEmpty()) msgs.add(juce::String::charToString((juce::juce_wchar) ('A' + s)) + ": " + l2.message()); }
    dark = t.getProperty("uiDark", dark);
    for (int s = 0; s < kSlots; ++s) { auto& sl = bank[(size_t) s]; sl.page = pages[(size_t) s]; sl.committed = sl.page; sl.undo.clear(); sl.redo.clear(); caches[(size_t) s] = SnapCache(); publishSlot(s); }
    cur = s0; engine.slotRequest.store(cur); ++edits;
    message = msgs.joinIntoString("; ");
}
bool ArcsHost::saveToFile(const juce::File& f) const {
    if (auto xml = page().toTree().createXml()) return xml->writeTo(f);
    return false;
}
bool ArcsHost::loadFromFile(const juce::File& f) {
    if (f.getSize() > 256 * 1024 * 1024) { message = "That file is too big to be a page"; return false; }
    auto xml = juce::XmlDocument::parse(f); if (!xml) { message = "That file is not a page"; return false; }
    LoadInfo li; Page p = Page::fromTree(juce::ValueTree::fromXml(*xml), &li);
    if (!li.ok) { message = li.futureVersion ? li.message() : juce::String("That file is not a page"); return false; }
    edit() = p; commit("Load page"); message = li.message(); return true;
}

// ---------------------------------------------------------------- MIDI export
juce::MidiFile ArcsHost::makeMidi(bool mpe, double tempo, MidiReport* report) const {
    if (tempo <= 1) tempo = bpm();
    auto s = buildSnapshot(page(), caches[(size_t) cur]);
    const double D = juce::jmax(0.001, s->seconds(tempo)), total = s->total;
    auto secsOfPos = [&](float x) { const double w = s->timeAtPos(x); return (s->reverse ? total - w : w) * D; };
    auto posAtSecs = [&](double t) { double w = t / D; if (s->reverse) w = total - w; return s->posAtTime((float) juce::jlimit(0.0, total, w)); };
    auto tick = [&](double t) { return t * tempo / 60.0 * kPpq; };
    const int range = bendRange(mpe);

    struct N { double t0, t1; int arc; };
    std::vector<N> notes;
    for (int i = 0; i < (int) s->arcs.size(); ++i) {
        const auto& a = *s->arcs[(size_t) i]; if (!a.audible) continue;
        const float len = a.span / juce::jmax(0.05f, a.rate);
        for (int k = 0; k < (a.repeat ? 100000 : 1); ++k) {   // each repeat of the line is a note
            const float pa = a.x0 + k * len; if (pa >= 1.0f) break; const float pb = juce::jmin(1.0f, pa + len);
            double t0 = secsOfPos(pa), t1 = secsOfPos(pb); if (t1 < t0) std::swap(t0, t1);
            if (t1 - t0 > 1.0e-4) notes.push_back({ t0 + 1.0e-6, t1 - 1.0e-6, i });
        }
    }
    std::stable_sort(notes.begin(), notes.end(), [](const N& a, const N& b) { return a.t0 < b.t0; });

    std::vector<int> chans; for (int c = mpe ? 2 : 1; c <= 16; ++c) if (mpe || c != 10) chans.push_back(c);   // never the drum channel
    std::vector<juce::MidiMessageSequence> tracks;
    std::vector<std::vector<double>> busy;   // [track][channel index] busy until (seconds)
    auto newTrack = [&] {
        juce::MidiMessageSequence seq; const bool first = tracks.empty();
        if (first) seq.addEvent(juce::MidiMessage::tempoMetaEvent((int) std::round(60.0e6 / tempo)), 0);
        auto rpn = [&](int ch, int param, int value) {
            seq.addEvent(juce::MidiMessage::controllerEvent(ch, 101, 0), 0); seq.addEvent(juce::MidiMessage::controllerEvent(ch, 100, param), 0);
            seq.addEvent(juce::MidiMessage::controllerEvent(ch, 6, value), 0); seq.addEvent(juce::MidiMessage::controllerEvent(ch, 38, 0), 0);
            seq.addEvent(juce::MidiMessage::controllerEvent(ch, 101, 127), 0); seq.addEvent(juce::MidiMessage::controllerEvent(ch, 100, 127), 0); };
        if (mpe) rpn(1, 6, 15);                                   // MPE configuration: lower zone, 15 member channels
        for (int ch : chans) rpn(ch, 0, range);                   // pitch-bend range (MSB semitones)
        tracks.push_back(seq); busy.push_back(std::vector<double>(chans.size(), -1.0));
    };
    newTrack();
    MidiReport rep;
    const double step = 0.002;   // 2 ms curve resolution (a fast dive stays within a few cents)
    for (const auto& nt : notes) {
        const auto& a = *s->arcs[(size_t) nt.arc];
        int tr = -1, ci = -1;
        for (size_t t = 0; t < tracks.size() && tr < 0; ++t) for (size_t c = 0; c < chans.size(); ++c) if (busy[t][c] <= nt.t0) { tr = (int) t; ci = (int) c; break; }
        if (tr < 0) { newTrack(); tr = (int) tracks.size() - 1; ci = 0; }
        busy[(size_t) tr][(size_t) ci] = juce::jmax(busy[(size_t) tr][(size_t) ci], nt.t1);
        auto& seq = tracks[(size_t) tr]; const int ch = chans[(size_t) ci];
        // sample the line along real time
        const int ns = juce::jmax(2, (int) std::ceil((nt.t1 - nt.t0) / step) + 1);
        int seg = 0;
        auto sampleAt = [&](double t, float& midi, float& env, float& pan) {
            float x; const float pp = posAtSecs(juce::jlimit(nt.t0, nt.t1, t)); if (!a.local(pp, x)) x = pp < a.x0 ? a.x0 : a.x1;
            const float u = juce::jlimit(0.0f, 1.0f, (x - a.x0) / a.span); const float f = u * kEnvRes; const int i = juce::jmin(kEnvRes - 1, (int) f); const float r = f - (float) i;
            midi = s->heardMidi(a, x, seg); env = (a.env[(size_t) i] + (a.env[(size_t) i + 1] - a.env[(size_t) i]) * r) * a.gain; pan = a.pan[(size_t) i] + (a.pan[(size_t) i + 1] - a.pan[(size_t) i]) * r; };
        auto expr = [&](float e) { return juce::jlimit(0, 127, (int) std::round(127.0f * std::sqrt(juce::jlimit(0.0f, 1.0f, e)))); };
        auto panCC = [&](float p) { return juce::jlimit(0, 127, (int) std::round(64.0f + 63.5f * p)); };
        float peak = 0; for (int k = 0; k < ns; ++k) { float m, e, pn; sampleAt(nt.t0 + (nt.t1 - nt.t0) * k / (ns - 1), m, e, pn); peak = juce::jmax(peak, e); }
        const int vel = juce::jlimit(1, 127, (int) std::round(24.0f + 103.0f * std::sqrt(juce::jlimit(0.0f, 1.0f, peak))));
        int base = -1, lastB = -1, lastE = -1, lastP = -1; seg = 0;
        auto bendOf = [&](float m) { return juce::jlimit(0, 16383, 8192 + (int) std::round((m - (float) base) / (float) range * 8192.0f)); };
        auto emitCtl = [&](double tk, float e, float pn, bool force) {
            const int ev = expr(e), pv = panCC(pn);
            if (force || std::abs(ev - lastE) >= 2) { seq.addEvent(mpe ? juce::MidiMessage::channelPressureChange(ch, ev) : juce::MidiMessage::controllerEvent(ch, 11, ev), tk); lastE = ev; }
            if (force || std::abs(pv - lastP) >= 2) { seq.addEvent(juce::MidiMessage::controllerEvent(ch, mpe ? 74 : 10, pv), tk); lastP = pv; } };
        for (int k = 0; k < ns; ++k) {
            const double t = k == ns - 1 ? nt.t1 : nt.t0 + k * step; const double tk = tick(t);
            float m, e, pn; sampleAt(t, m, e, pn);
            if (base < 0 || std::abs(m - (float) base) > (float) range - 0.25f) {   // start, or the glide left the bend range: next note
                if (base >= 0) { seq.addEvent(juce::MidiMessage::noteOff(ch, base), tk); ++rep.splits; }
                base = juce::jlimit(0, 127, (int) std::round(m)); lastB = bendOf(m);
                seq.addEvent(juce::MidiMessage::pitchWheel(ch, lastB), tk); emitCtl(tk, e, pn, true);
                seq.addEvent(juce::MidiMessage::noteOn(ch, base, (juce::uint8) vel), tk); ++rep.notes;
                continue;
            }
            const int b = bendOf(m); if (std::abs(b - lastB) >= 2 || (k == ns - 1 && b != lastB)) { seq.addEvent(juce::MidiMessage::pitchWheel(ch, b), tk); lastB = b; }
            emitCtl(tk, e, pn, false);
        }
        seq.addEvent(juce::MidiMessage::noteOff(ch, base), tick(nt.t1));
    }
    juce::MidiFile mf; mf.setTicksPerQuarterNote(kPpq);
    for (auto& t : tracks) { t.updateMatchedPairs(); mf.addTrack(t); }
    rep.tracks = (int) tracks.size();
    if (report) *report = rep;
    return mf;
}
juce::File ArcsHost::writeMidi(const juce::File& f, bool mpe, MidiReport* report) const {
    auto mf = makeMidi(mpe, bpm(), report);
    f.getParentDirectory().createDirectory(); f.deleteFile();
    juce::FileOutputStream os(f); if (!os.openedOk()) return {};
    return mf.writeTo(os, 1) ? f : juce::File();   // GATE: via-caller (ArcsPage asked spec.arcs.midi-drag / midi-save / loop-midi-drag)
}

// ---------------------------------------------------------------- loop export
bool ArcsHost::loopSeconds(const Snapshot& s, double tempo, double& t0, double& t1) {
    if (!s.loopOn || !(s.loopB > s.loopA)) return false;
    if (tempo <= 1) tempo = s.ownBpm;
    const double D = juce::jmax(0.001, s.seconds(tempo)), total = s.total;
    auto secsOfPos = [&](float x) { const double w = s.timeAtPos(x); return (s.reverse ? total - w : w) * D; };
    t0 = secsOfPos(s.loopA); t1 = secsOfPos(s.loopB); if (t1 < t0) std::swap(t0, t1);
    return t1 - t0 > 1.0e-3;
}
juce::MidiFile ArcsHost::makeLoopMidi(bool mpe, double tempo, MidiReport* report) const {
    if (tempo <= 1) tempo = bpm();
    const std::unique_ptr<Snapshot> snap(buildSnapshot(page(), caches[(size_t) cur]));
    double t0 = 0, t1 = 0; const auto full = makeMidi(mpe, tempo, report);
    if (!snap || !loopSeconds(*snap, tempo, t0, t1)) return full;
    const double k0 = t0 * tempo / 60.0 * kPpq, k1 = t1 * tempo / 60.0 * kPpq;
    juce::MidiFile out; out.setTicksPerQuarterNote(kPpq); int notes = 0;
    for (int ti = 0; ti < full.getNumTracks(); ++ti) {
        juce::MidiMessageSequence src(*full.getTrack(ti)); src.updateMatchedPairs(); juce::MidiMessageSequence dst;
        for (int i = 0; i < src.getNumEvents(); ++i) {
            const auto* ev = src.getEventPointer(i); const auto& m = ev->message; const double t = m.getTimeStamp();
            if (m.isMetaEvent() || (t <= 0.0 && !m.isNoteOnOrOff())) { if (t <= k0 || m.isMetaEvent()) { auto c = m; c.setTimeStamp(0); dst.addEvent(c); } continue; }   // tempo, setup
            if (m.isNoteOn()) {
                if (t < k0 || t >= k1) continue;
                auto on = m; on.setTimeStamp(t - k0); dst.addEvent(on); ++notes;
                const double off = ev->noteOffObject != nullptr ? ev->noteOffObject->message.getTimeStamp() : t;
                dst.addEvent(juce::MidiMessage::noteOff(m.getChannel(), m.getNoteNumber()).withTimeStamp(juce::jmin(off, k1) - k0));
            } else if (m.isNoteOff()) continue;   // written with their note-on
            else if (t >= k0 && t < k1) { auto c = m; c.setTimeStamp(t - k0); dst.addEvent(c); }   // bends / CCs inside the loop
        }
        dst.sort(); dst.updateMatchedPairs(); out.addTrack(dst);
    }
    if (report) report->notes = notes;
    return out;
}
juce::File ArcsHost::writeLoopMidi(const juce::File& f, bool mpe, MidiReport* report) const {
    auto mf = makeLoopMidi(mpe, bpm(), report);
    f.getParentDirectory().createDirectory(); f.deleteFile();
    juce::FileOutputStream os(f); if (!os.openedOk()) return {};
    return mf.writeTo(os, 1) ? f : juce::File();   // GATE: via-caller (ArcsPage asked spec.arcs.midi-drag / midi-save / loop-midi-drag)
}
bool ArcsHost::renderLoopToBuffer(const RenderRequest& r, juce::AudioBuffer<float>& out, double sampleRate, const std::atomic<bool>* abort) {
    if (!r.valid()) return false; double t0 = 0, t1 = 0; if (!loopSeconds(*r.snap, r.bpm, t0, t1)) return false;
    juce::AudioBuffer<float> full; if (!renderToBuffer(r, full, sampleRate, nullptr, abort) || full.getNumSamples() == 0) return false;
    const int s0 = juce::jlimit(0, full.getNumSamples() - 1, (int) std::llround(t0 * sampleRate)), s1 = juce::jlimit(s0 + 1, full.getNumSamples(), (int) std::llround(t1 * sampleRate));
    out.setSize(full.getNumChannels(), s1 - s0); for (int c = 0; c < full.getNumChannels(); ++c) out.copyFrom(c, 0, full, c, s0, s1 - s0);
    return true;
}

// ---------------------------------------------------------------- render
bool ArcsHost::startRender(const juce::File& wavOut, double sampleRate) {
    cancelRender();
    jobAbort.store(false); jobDone.store(false); jobOk.store(false); progress.store(0); jobError.clear();
    renderTarget = wavOut; renderEdit = edits;
    const double rate = sampleRate > 0 ? sampleRate : engine.sampleRate();
    job = std::make_unique<Job>(*this, buildSnapshot(page(), caches[(size_t) cur]), rate, bpm(), wavOut);
    return job->startThread(juce::Thread::Priority::low);
}
RenderRequest ArcsHost::prepareRender() const {
    RenderRequest r; r.snap = std::shared_ptr<const Snapshot>(buildSnapshot(page(), caches[(size_t) cur]).release()); r.bpm = bpm(); r.name = page().name; return r;
}
std::vector<RenderNote> ArcsHost::notesOf(const Snapshot& s, double tempo) {
    std::vector<RenderNote> out; if (tempo <= 1) tempo = s.ownBpm;
    const double D = juce::jmax(0.001, s.seconds(tempo)), total = s.total;
    auto secsOfPos = [&](float x) { const double w = s.timeAtPos(x); return (s.reverse ? total - w : w) * D; };
    for (const auto& ap : s.arcs) {
        const auto& a = *ap; if (!a.audible) continue;
        float peak = 0; for (float e : a.env) peak = juce::jmax(peak, e); peak *= a.gain;
        const float len = a.span / juce::jmax(0.05f, a.rate);
        for (int k = 0; k < (a.repeat ? 100000 : 1); ++k) {
            const float pa = a.x0 + k * len; if (pa >= 1.0f) break; const float pb = juce::jmin(1.0f, pa + len);
            double t0 = secsOfPos(pa), t1 = secsOfPos(pb); if (t1 < t0) std::swap(t0, t1); if (t1 - t0 < 1.0e-4) continue;
            int seg = 0; float x = a.x0; (void) a.local(s.reverse ? pb - 1.0e-6f : pa, x);
            RenderNote n; n.pitch = juce::jlimit(0, 127, (int) std::round(s.heardMidi(a, x, seg))); n.velocity = juce::jlimit(0.05f, 1.0f, std::sqrt(peak)); n.start = t0; n.length = t1 - t0;
            out.push_back(n);
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const RenderNote& a, const RenderNote& b) { return a.start < b.start; });
    return out;
}
bool ArcsHost::renderToBuffer(const RenderRequest& r, juce::AudioBuffer<float>& out, double sampleRate, std::vector<RenderNote>* notes, const std::atomic<bool>* abort, std::atomic<int>* progress) {
    if (!r.valid()) return false;
    try {
        sampleRate = juce::jlimit(8000.0, 768000.0, std::isfinite(sampleRate) ? sampleRate : 48000.0);
        const double tempo = r.bpm > 1 ? r.bpm : r.snap->ownBpm;
        const int total = (int) std::ceil(r.snap->durationSeconds(tempo) * sampleRate) + (int) (0.1 * sampleRate) + 1024;   // what Engine::render emits (+ margin)
        out.setSize(2, total, false, true, false); out.clear(); int pos = 0;
        const bool ok = Engine::render(*r.snap, sampleRate, tempo, [&](const float* l, const float* rr, int n) {
            const int m = juce::jmin(n, out.getNumSamples() - pos); if (m > 0) { out.copyFrom(0, pos, l, m); out.copyFrom(1, pos, rr, m); } pos += m; return true; }, progress, abort);
        out.setSize(2, juce::jmax(1, pos), true, true, false);
        if (notes) *notes = notesOf(*r.snap, tempo);
        return ok;
    } catch (const std::bad_alloc&) { out.setSize(2, 0); return false; }
}

juce::MemoryBlock ArcsHost::renderBlob() const {
    auto t = getState(); t.setProperty("renderBpm", bpm(), nullptr);
    juce::MemoryOutputStream os; t.writeToStream(os); return os.getMemoryBlock();   // NOT-EXPORT: page state for ARRANGE re-renders (memory)
}
RenderRequest ArcsHost::fromState(const juce::ValueTree& t, double tempo) {
    RenderRequest r; LoadInfo li; Page p;
    if (t.hasType("ARCSPAGE")) { p = Page::fromTree(t, &li); if (!li.ok) p = Page(); }
    SnapCache cache;   // local: nothing shared with a live page
    r.snap = std::shared_ptr<const Snapshot>(buildSnapshot(p, cache).release());
    const double stored = t.isValid() ? (double) t.getProperty("renderBpm", 0.0) : 0.0;
    r.bpm = tempo > 1 ? tempo : (std::isfinite(stored) && stored > 1 ? juce::jlimit(20.0, 999.0, stored) : p.ownBpm);
    r.name = p.name; return r;
}
RenderRequest ArcsHost::fromState(const juce::MemoryBlock& blob, double tempo) {
    juce::ValueTree t;
    if (blob.getSize() > 0 && blob.getSize() < (size_t) 256 * 1024 * 1024) {
        t = juce::ValueTree::readFromData(blob.getData(), blob.getSize());
        if (!t.hasType("ARCSPAGE")) { if (auto xml = juce::parseXML(blob.toString())) t = juce::ValueTree::fromXml(*xml); }   // an XML blob works too
    }
    return fromState(t, tempo);
}

void ArcsHost::cancelRender() {
    if (!job) return;
    jobAbort.store(true); job->stopThread(10000); job.reset();
    jobDone.store(false); jobOk.store(false);
}

} // namespace arcs
