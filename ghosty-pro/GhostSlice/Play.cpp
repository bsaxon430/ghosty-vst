#include "Play.h"
#include "Analysis.h"
#include "Hold.h"
#include "Syllables.h"

namespace gslice::play
{
const char* modeName (SliceMode m)
{
    static const char* n[kSliceModes] { "REGIONS", "EQUAL", "MANUAL", "TRANSIENT", "SYLLABLE", "NOTE", "BEAT" };
    return n[juce::jlimit (0, kSliceModes - 1, (int) m)];
}

std::vector<int64> chopCuts (const Project& p, const Plan& plan, const analysis::Result* a)
{
    std::vector<int64> out;
    const auto& m = p.play;
    const double kT = plan.hostRate / p.sampleRate;
    const int64 L = plan.length;
    if (L < 64) return out;
    const double sens = juce::jlimit (0.0, 1.0, m.sensitivity);
    std::vector<int64> src;              // source positions (SF) of the cuts
    double minGapMs = 60;
    if (m.mode == SliceMode::Beat)
    {
        const double step = p.secondsPerBeat() * p.sampleRate * 4.0 / (double) juce::jmax (1, m.beatDiv);
        if (step > 16)
            for (double t = std::fmod (p.beatOffset, step); t * kT < (double) L; t += step)
            { const int64 h = (int64) std::llround (t * kT); if (h > 32 && h < L - 32) out.push_back (h); }
        return out;
    }
    if (a == nullptr) return out;
    const double thr = 0.9 - 0.85 * sens;   // the strength / confidence a candidate needs (sensitivity 0 .. 1)
    if (m.mode == SliceMode::Transient)
        for (auto& e : a->onsets.events) { if (e.strength >= thr && (e.flags & analysis::kBreath) == 0) src.push_back (e.pos); }
    else if (m.mode == SliceMode::Syllable)
    {   // phase 16: the syllables (a cut at each syllable's start: its onset consonant stays with its vowel); without any (no
        // voiced nuclei): the onsets, merged like syllables
        if (! a->syllables.empty())
        {
            const double cthr = 0.6 * (1.0 - sens);
            for (auto& s : a->syllables) if (s.confidence >= cthr) src.push_back (s.start);
        }
        else
        {
            bool any = false; for (auto& e : a->onsets.events) any |= (e.flags & analysis::kSyllable) != 0;
            for (auto& e : a->onsets.events)
                if ((! any || (e.flags & analysis::kSyllable) != 0) && e.strength >= thr * 0.8 && (e.flags & analysis::kBreath) == 0) src.push_back (e.pos);
        }
        minGapMs = 90;                   // merge short syllables (study 7.1)
    }
    else if (m.mode == SliceMode::Note)
    {   // note starts, and a note's end where silence follows it
        const double minLenMs = 140.0 - 100.0 * sens;
        for (size_t i = 0; i < a->notes.size(); ++i)
        {
            const auto& n = a->notes[i];
            if (n.confidence < thr * 0.8 || (double) (n.s1 - n.s0) / a->pitch.sr * 1000.0 < minLenMs) continue;
            src.push_back (n.s0);
            const bool gapAfter = i + 1 >= a->notes.size() || (double) (a->notes[i + 1].s0 - n.s1) / a->pitch.sr > 0.03;
            if (gapAfter) src.push_back (n.s1);
        }
        minGapMs = 40;
    }
    else return out;
    // through every region that plays the source position (output time; reversed regions mirror it)
    for (auto& r : p.regions)
    {
        if (r.muted || ! r.valid()) continue;
        const auto mp = r.map();
        for (auto s : src)
        {
            if (s <= r.srcIn() || s >= r.srcOut()) continue;
            const double t = mp.outAt ((double) s);
            const int64 h = (int64) std::llround ((r.dstStart + (r.reverse ? r.length() - t : t)) * kT);
            if (h > 32 && h < L - 32) out.push_back (h);
        }
    }
    std::sort (out.begin(), out.end());
    const int64 gap = (int64) std::llround (minGapMs * 0.001 * plan.hostRate);
    std::vector<int64> kept;
    for (auto h : out) if (h >= gap && h <= L - gap && (kept.empty() || h - kept.back() >= gap)) kept.push_back (h);
    return kept;
}

std::vector<Slice> slices (const Project& p, const Plan& plan, const analysis::Result* a)
{
    std::vector<Slice> out;
    const double kT = plan.hostRate / p.sampleRate;
    const auto& m = p.play;
    if (m.mode == SliceMode::Regions)
    {
        std::vector<const Region*> rs; for (auto& r : p.regions) if (! r.muted && r.valid()) rs.push_back (&r);
        std::stable_sort (rs.begin(), rs.end(), [] (const Region* x, const Region* y) { return x->dstStart < y->dstStart; });
        for (auto* r : rs)
        {
            Slice s; s.dst0 = (int64) std::llround (r->dstStart * kT); s.dst1 = (int64) std::llround (r->dstEnd() * kT); s.regionId = r->id;
            if (s.length() > 16) out.push_back (s);
        }
        return out;
    }
    const int64 L = plan.length;
    if (L < 64) return out;
    std::vector<int64> cuts { 0 };
    if (m.mode == SliceMode::Equal)
    {
        const int N = juce::jlimit (2, 64, m.equalN);
        for (int i = 1; i < N; ++i) cuts.push_back ((int64) ((double) L * i / N));
    }
    else if (m.mode == SliceMode::Manual)
        for (double c : m.manual) { const int64 h = (int64) std::llround (c * kT); if (h > 32 && h < L - 32) cuts.push_back (h); }
    else { const auto cc = chopCuts (p, plan, a); cuts.insert (cuts.end(), cc.begin(), cc.end()); }
    cuts.push_back (L);
    std::sort (cuts.begin(), cuts.end()); cuts.erase (std::unique (cuts.begin(), cuts.end()), cuts.end());
    for (size_t i = 0; i + 1 < cuts.size(); ++i) if (cuts[i + 1] - cuts[i] >= 64) out.push_back ({ cuts[i], cuts[i + 1], 0 });
    return out;
}

juce::String bankKey (const Project& p, const Plan& plan, const analysis::Result* a)
{
    Project q = p; q.play.keys.clear(); q.play.polyphony = 16; q.play.bendRange = 2; q.play.velToGain = 1; q.play.velToFilter = 0;
    if (q.play.mode == SliceMode::Regions) { q.repeats.clear(); q.play.equalN = 16; q.play.manual.clear(); }
    if (q.play.mode == SliceMode::Equal) q.play.manual.clear();
    Hasher h; h.add ("bank").add (kEngineVersion).add (json::fingerprint (q)).add (plan.hostRate).add (plan.fingerprint);
    if ((int) q.play.mode >= (int) SliceMode::Transient) { const auto cc = chopCuts (p, plan, a); h.add ("chop").add ((int) cc.size()); for (auto c : cc) h.add (c); }
    bool vh = false; for (auto& k : p.play.keys) if (k.vowelHold && ! k.oneShot) { if (! vh) h.add ("vowelhold"); vh = true; h.add (k.slice); }
    if (vh && a != nullptr) h.add (analysis::digest (a->syllables));
    return h.hex();
}

static void thumbOf (const juce::AudioBuffer<float>& b, std::array<float, 48>& t)
{
    t.fill (0.0f);
    const int n = b.getNumSamples(); if (n <= 0) return;
    for (int c = 0; c < b.getNumChannels(); ++c)
    {
        const float* x = b.getReadPointer (c);
        for (int i = 0; i < n; ++i) { auto& v = t[(size_t) juce::jmin (47, (int) ((int64) i * 48 / n))]; v = std::max (v, std::abs (x[i])); }
    }
}

std::shared_ptr<const Bank> buildBank (const Project& p, const Plan& plan, const SourceAudio* src, const WorkingAudio& w, StudioCache* cache, const std::function<bool()>& cancel,
                                       const analysis::Result* a)
{
    auto B = std::make_shared<Bank>();
    B->key = bankKey (p, plan, a); B->rate = w.rate; B->slices = slices (p, plan, a);
    if (p.play.mode == SliceMode::Regions)
    {
        for (auto& s : B->slices)
        {
            if (cancel && cancel()) return nullptr;
            const auto* r = p.find (s.regionId); if (r == nullptr) { B->audio.emplace_back (2, 1); B->audio.back().clear(); continue; }
            Project q = p; q.repeats.clear(); q.regions = { *r }; q.regions[0].dstStart = 0;
            CompileOptions o; o.hostRate = w.rate;
            if (a != nullptr) { o.formantBaseHz = a->medianF0; o.pitchTrack = &a->pitch; }
            const auto pl = compile (q, src, o);
            auto rendered = render::plan (pl, w, cache, false);
            juce::AudioBuffer<float> b (2, (int) juce::jmax<int64> (1, s.length())); b.clear();
            for (int c = 0; c < 2; ++c) b.copyFrom (c, 0, rendered, juce::jmin (c, rendered.getNumChannels() - 1), 0, juce::jmin (b.getNumSamples(), rendered.getNumSamples()));
            B->audio.push_back (std::move (b));
        }
    }
    else if (! B->slices.empty())
    {
        if (cancel && cancel()) return nullptr;
        const auto full = render::plan (plan, w, cache, false);
        const int64 N = full.getNumSamples();
        const int64 win = (int64) std::llround (0.002 * w.rate);
        auto snapZero = [&] (int64 b)
        {
            if (b <= 0 || b >= N) return b;
            const float* x = full.getReadPointer (0);
            for (int64 d = 0; d <= win; ++d)
                for (int64 c : { b - d, b + d })
                    if (c > 0 && c < N && ((x[c - 1] <= 0.0f) != (x[c] <= 0.0f) || x[c] == 0.0f)) return c;
            return b;
        };
        std::vector<int64> edges { B->slices.front().dst0 };
        for (size_t i = 0; i < B->slices.size(); ++i) edges.push_back (i + 1 < B->slices.size() ? snapZero (B->slices[i].dst1) : B->slices[i].dst1);
        const int64 fade = (int64) std::llround (0.001 * w.rate);
        for (size_t i = 0; i < B->slices.size(); ++i)
        {
            B->slices[i].dst0 = edges[i]; B->slices[i].dst1 = std::max (edges[i] + 16, edges[i + 1]);
            const int64 len = B->slices[i].length();
            juce::AudioBuffer<float> b (2, (int) len); b.clear();
            for (int c = 0; c < 2; ++c) { const float* x = full.getReadPointer (juce::jmin (c, full.getNumChannels() - 1)); float* y = b.getWritePointer (c);
                                          for (int64 k = 0; k < len; ++k) { const int64 j = B->slices[i].dst0 + k; y[k] = j >= 0 && j < N ? x[j] : 0.0f; } }
            for (int c = 0; c < 2; ++c)
            {
                float* y = b.getWritePointer (c);
                const int64 f = juce::jmin (fade, len / 2);
                for (int64 k = 0; k < f; ++k) { const float g = fadeShape ((double) k / (double) f); y[k] *= g; y[len - 1 - k] *= g; }
            }
            B->audio.push_back (std::move (b));
        }
    }
    B->thumbs.resize (B->audio.size());
    for (size_t i = 0; i < B->audio.size(); ++i) thumbOf (B->audio[i], B->thumbs[i]);
    B->sustained.resize (B->audio.size());
    if (src != nullptr && a != nullptr && ! a->syllables.empty() && p.play.mode != SliceMode::Regions)
        for (auto& k : p.play.keys)
            if (k.vowelHold && ! k.oneShot && k.slice >= 0 && k.slice < (int) B->audio.size() && B->sustained[(size_t) k.slice].getNumSamples() == 0)
            {
                if (cancel && cancel()) return nullptr;
                B->sustained[(size_t) k.slice] = vowelSustain (plan, B->slices[(size_t) k.slice], B->audio[(size_t) k.slice], *src, w, *a);
            }
    return B;
}

juce::AudioBuffer<float> vowelSustain (const Plan& plan, const Slice& s, const juce::AudioBuffer<float>& sliceAudio, const SourceAudio& src, const WorkingAudio& w,
                                       const analysis::Result& a, double seconds)
{
    juce::AudioBuffer<float> none;
    const double kS = w.ratio;   // WF per SF
    // the syllable whose vowel plays inside this slice through a 1x, unreversed, unpitched piece
    for (auto& syl : a.syllables)
    {
        const int64 edge = (int64) std::lround (0.015 * src.sampleRate);
        const int64 va = syl.vowelStart + edge, vb = syl.vowelEnd - edge;
        if (vb - va < (int64) std::lround (0.04 * src.sampleRate)) continue;
        for (auto& pc : plan.pieces)
        {
            if (pc.stretch || pc.reverse || pc.hold != nullptr || pc.repeatIndex >= 0 || pc.curve != nullptr || pc.dst1 <= s.dst0 || pc.dst0 >= s.dst1) continue;
            const double w0 = pc.map.srcAt ((double) pc.u0), w1 = w0 + (double) pc.length();
            const double vaW = (double) va * kS, vbW = (double) vb * kS;
            if (vaW < w0 || vbW > w1) continue;
            const double outA = (double) pc.dst0 + (vaW - w0);
            if (outA < (double) s.dst0 || outA >= (double) s.dst1) continue;
            // the stable part of the vowel, held from its start
            const auto st = hold::pickStable (src, syl.vowelStart, syl.vowelEnd, &a.syllables);
            const double sa = (double) st.a * kS, sb = (double) st.b * kS;
            const int64 head = (int64) std::llround ((double) pc.dst0 + (sa - w0)) - s.dst0;
            if (head <= 0 || head >= sliceAudio.getNumSamples()) continue;
            HoldOp op; op.id = 1; op.mode = HoldMode::Sustain; op.seed = (uint32_t) (0x5157u ^ (uint32_t) syl.nucleus);
            const int64 len = (int64) std::llround (seconds * w.rate);
            const auto P = hold::makePlan (&src, kS, w.rate, op, 0, len, sa, sb, (double) va * kS, (double) vb * kS, true, false, false);
            if (P == nullptr || P->grains.empty()) continue;
            juce::AudioBuffer<float> out (2, (int) (head + len)); out.clear();
            for (int c = 0; c < 2; ++c) out.copyFrom (c, 0, sliceAudio, juce::jmin (c, sliceAudio.getNumChannels() - 1), 0, (int) head);
            constexpr int kChunk = 512;
            for (int64 v = 0; v < len; v += kChunk)
            {
                const int n = (int) std::min<int64> (kChunk, len - v);
                hold::renderGrains (*P, w, v, out.getWritePointer (0) + head + v, out.getWritePointer (1) + head + v, n);
            }
            if (pc.gain != 1.0f) for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::multiply (out.getWritePointer (c) + head, pc.gain, (int) len);
            // the end of the buffer (a key held longer) fades out over 200 ms
            const int64 f = std::min<int64> (len / 2, (int64) std::lround (0.2 * w.rate));
            for (int c = 0; c < 2; ++c) { float* y = out.getWritePointer (c); for (int64 k = 0; k < f; ++k) y[head + len - 1 - k] *= fadeShape ((double) k / (double) f); }
            return out;
        }
    }
    return none;
}

PlayMap autoMap (const PlayMap& m, int n, int base)
{
    PlayMap o = m; o.keys.clear();
    n = juce::jlimit (0, 128 - base, n);
    for (int i = 0; i < n; ++i)
    {
        KeySlot k; if (const auto* old = m.key (base + i)) k = *old;
        k.note = base + i; k.slice = i;
        o.keys.push_back (k);
    }
    return o;
}

double repeatSeconds (int slot, double bpm)
{
    using ghosty::slicevoice::retrig::periodSeconds;
    switch (slot)
    {
        case 1: return periodSeconds (bpm, 8, 0);
        case 2: return periodSeconds (bpm, 16, 0);
        case 3: return periodSeconds (bpm, 32, 0);
        case 4: return periodSeconds (bpm, 16, 1);
        case 5: return periodSeconds (bpm, 64, 0);
        default: return 0.0;
    }
}
juce::String repeatName (int slot) { static const char* n[] { "OFF", "1/8", "1/16", "1/32", "1/16T", "1/64" }; return n[juce::jlimit (0, 5, slot)]; }
juce::String noteName (int note) { static const char* n[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; return juce::String (n[(note % 12 + 12) % 12]) + juce::String (note / 12 - 2); }

juce::MidiFile mappingMidi (const PlayMap& m, const std::vector<Slice>& sl, double bpm, double hostRate)
{
    juce::MidiFile mf; mf.setTicksPerQuarterNote (960);
    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / juce::jlimit (20.0, 999.0, bpm))), 0);
    seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0);
    const double tpf = 960.0 * bpm / 60.0 / hostRate;
    for (size_t i = 0; i < sl.size(); ++i)
    {
        const KeySlot* k = nullptr; for (auto& x : m.keys) if (x.slice == (int) i && (k == nullptr || x.note < k->note)) k = &x;
        if (k == nullptr) continue;
        const double t0 = std::round ((double) sl[i].dst0 * tpf), t1 = std::max (t0 + 1, std::round ((double) sl[i].dst1 * tpf) - 1);
        seq.addEvent (juce::MidiMessage::noteOn (1, k->note, (juce::uint8) 100), t0);
        seq.addEvent (juce::MidiMessage::noteOff (1, k->note), t1);
    }
    seq.updateMatchedPairs();
    seq.addEvent (juce::MidiMessage::endOfTrack(), seq.getEndTime() + 1);
    mf.addTrack (seq);
    return mf;
}

std::vector<std::array<double, 3>> notesOf (const juce::MidiFile& f)
{
    std::vector<std::array<double, 3>> out;
    for (int t = 0; t < f.getNumTracks(); ++t)
    {
        const auto* s = f.getTrack (t);
        for (int i = 0; i < s->getNumEvents(); ++i)
        {
            const auto& m = s->getEventPointer (i)->message;
            if (! m.isNoteOn()) continue;
            double end = m.getTimeStamp();
            for (int j = i + 1; j < s->getNumEvents(); ++j) { const auto& o = s->getEventPointer (j)->message; if (o.isNoteOff() && o.getNoteNumber() == m.getNoteNumber()) { end = o.getTimeStamp(); break; } }
            out.push_back ({ (double) m.getNoteNumber(), m.getTimeStamp(), end - m.getTimeStamp() });
        }
    }
    return out;
}

// ---------------------------------------------------------------- engine
void Engine::prepare (double sampleRate, int)
{
    sr = sampleRate > 0 ? sampleRate : 48000.0;
    pool.prepare (sr);
    fifo.reset();
    for (auto& s : sounding) s.store (0);
}

void Engine::publish (std::shared_ptr<Table> t)
{
    t->gen = ++genCounter;
    keep.push_back (t);
    current.store (t.get(), std::memory_order_release);
    collectGarbage();
}

void Engine::collectGarbage()
{
    const uint64 inUse = minGenInUse.load (std::memory_order_acquire);
    for (auto it = keep.begin(); it != keep.end() && keep.size() > 1;)
        if ((*it)->gen < inUse && it->get() != current.load()) it = keep.erase (it); else ++it;
    // a host that stopped calling us: nothing sounds any more after a while, so everything but the newest can go
    if (keep.size() > 8 && activeVoices.load() == 0 && fadingVoices.load() == 0)
    {
        auto* cur = current.load();
        keep.erase (std::remove_if (keep.begin(), keep.end(), [cur] (const std::shared_ptr<Table>& t) { return t.get() != cur && t->gen + 8 < cur->gen; }), keep.end());
    }
}

void Engine::audition (int note, float velocity, bool on) noexcept
{
    const juce::SpinLock::ScopedLockType l (auditionLock);
    int s1, n1, s2, n2; fifo.prepareToWrite (1, s1, n1, s2, n2);
    if (n1 > 0) fifoBuf[(size_t) s1] = { note, velocity, on }; else if (n2 > 0) fifoBuf[(size_t) s2] = { note, velocity, on };
    fifo.finishedWrite (n1 + n2);
}

void Engine::handle (const juce::MidiMessage& m, const Table& t, double bpm) noexcept
{
    if (m.isNoteOn())
    {
        const auto* k = t.map.key (m.getNoteNumber());
        if (k == nullptr || t.bank == nullptr || k->slice < 0 || k->slice >= (int) t.bank->audio.size() || std::abs (t.bank->rate - sr) > 1e-6) return;
        const bool sus = k->vowelHold && ! k->oneShot && k->slice < (int) t.bank->sustained.size() && t.bank->sustained[(size_t) k->slice].getNumSamples() > 0;
        const auto& b = sus ? t.bank->sustained[(size_t) k->slice] : t.bank->audio[(size_t) k->slice];
        ghosty::slicevoice::VoiceStart s;
        s.note = k->note; s.velocity = m.getFloatVelocity();
        s.sample.ch[0] = b.getReadPointer (0); s.sample.ch[1] = b.getReadPointer (juce::jmin (1, b.getNumChannels() - 1)); s.sample.length = b.getNumSamples();
        s.startFrame = juce::jmax (0.0, k->startMs) * 0.001 * sr; s.pitchSemis = k->pitchSemis; s.reverse = k->reverse; s.oneShot = k->oneShot;
        s.attackS = k->attackMs * 0.001; s.decayS = k->decayMs * 0.001; s.sustain = (float) k->sustain; s.releaseS = k->releaseMs * 0.001;
        const double v = juce::jlimit (0.0, 1.0, (double) s.velocity);
        s.gain = (float) (juce::Decibels::decibelsToGain (k->gainDb, -100.0) * ((1.0 - t.map.velToGain) + t.map.velToGain * v * v));
        s.pan = (float) k->pan; s.choke = k->choke;
        s.cutoffHz = t.map.velToFilter > 0 ? 20000.0 * std::exp2 (-t.map.velToFilter * (1.0 - v) * 7.0) : 0.0;
        s.retrigS = repeatSeconds (k->repeat, bpm); s.tag = t.gen;
        if (pool.noteOn (s) >= 0) notesPlayed.fetch_add (1, std::memory_order_relaxed);
    }
    else if (m.isNoteOff()) pool.noteOff (m.getNoteNumber());
    else if (m.isPitchWheel()) pool.setBend ((m.getPitchWheelValue() - 8192) / 8192.0 * juce::jlimit (2, 24, t.map.bendRange));
    else if (m.isAllNotesOff()) pool.allNotesOff (false);
    else if (m.isAllSoundOff()) pool.allNotesOff (true);
}

void Engine::process (float* L, float* R, int n, const juce::MidiBuffer& midi, double bpm) noexcept
{
    Table* t = current.load (std::memory_order_acquire);
    if (t != nullptr) pool.setPolyphony (t->map.polyphony);
    {   // UI auditions first (offset 0)
        int s1, n1, s2, n2; fifo.prepareToRead (fifo.getNumReady(), s1, n1, s2, n2);
        auto run = [&] (int a, int c) { for (int i = 0; i < c; ++i) { const auto& e = fifoBuf[(size_t) (a + i)];
                                                                    if (t == nullptr) continue;
                                                                    if (e.on) handle (juce::MidiMessage::noteOn (1, e.note, juce::jlimit (0.01f, 1.0f, e.vel)), *t, bpm);
                                                                    else handle (juce::MidiMessage::noteOff (1, e.note), *t, bpm); } };
        run (s1, n1); run (s2, n2);
        fifo.finishedRead (n1 + n2);
    }
    int pos = 0;
    for (const auto meta : midi)
    {
        const int at = juce::jlimit (0, n, meta.samplePosition);
        if (at > pos) { pool.render (L + pos, R + pos, at - pos); pos = at; }
        if (t != nullptr) handle (meta.getMessage(), *t, bpm);
    }
    if (pos < n) pool.render (L + pos, R + pos, n - pos);
    const uint64 mt = pool.minTag();
    minGenInUse.store (t != nullptr ? std::min (t->gen, mt) : mt, std::memory_order_release);
    activeVoices.store (pool.active(), std::memory_order_relaxed); fadingVoices.store (pool.fading(), std::memory_order_relaxed);
    steals.store (pool.steals, std::memory_order_relaxed);
    uint64 bits[2] {};
    if (t != nullptr) for (auto& k : t->map.keys) if (pool.sounding (k.note)) bits[(size_t) k.note >> 6] |= (uint64) 1 << (k.note & 63);
    sounding[0].store (bits[0], std::memory_order_relaxed); sounding[1].store (bits[1], std::memory_order_relaxed);
}
} // namespace gslice::play

namespace gslice::ops
{
R setPlay (const Project& in, const PlayMap& m)
{
    Project p = in; p.play = m;
    p.play.equalN = juce::jlimit (2, 64, p.play.equalN); p.play.polyphony = juce::jlimit (1, 32, p.play.polyphony); p.play.bendRange = juce::jlimit (2, 24, p.play.bendRange);
    p.play.velToGain = juce::jlimit (0.0, 1.0, p.play.velToGain); p.play.velToFilter = juce::jlimit (0.0, 1.0, p.play.velToFilter);
    std::sort (p.play.manual.begin(), p.play.manual.end());
    for (auto& k : p.play.keys)
    {
        k.pitchSemis = juce::jlimit (-24.0, 24.0, k.pitchSemis); k.startMs = juce::jlimit (0.0, 60000.0, k.startMs);
        k.attackMs = juce::jlimit (0.0, 5000.0, k.attackMs); k.decayMs = juce::jlimit (0.0, 5000.0, k.decayMs); k.sustain = juce::jlimit (0.0, 1.0, k.sustain);
        k.releaseMs = juce::jlimit (0.0, 10000.0, k.releaseMs); k.gainDb = juce::jlimit (-48.0, 12.0, k.gainDb); k.pan = juce::jlimit (-1.0, 1.0, k.pan);
        k.choke = juce::jlimit (0, 8, k.choke); k.repeat = juce::jlimit (0, 5, k.repeat);
    }
    std::sort (p.play.keys.begin(), p.play.keys.end(), [] (const KeySlot& a, const KeySlot& b) { return a.note < b.note; });
    if (p.play == in.play) return std::nullopt;
    return p;
}
R turnIntoInstrument (const Project& in, int n) { return setPlay (in, play::autoMap (in.play, n)); }

R splitAtSlices (const Project& in, const std::vector<double>& cuts, int* made)
{
    Project p = in; int n = 0;
    for (double c : cuts)
    {
        const Region* top = nullptr;   // the topmost region playing there
        for (auto& r : p.regions) if (! r.muted && c > r.dstStart && c < r.dstEnd()) top = &r;
        if (top == nullptr) continue;
        if (auto q = splitAt (p, top->id, c)) { p = std::move (*q); ++n; }
    }
    if (made) *made = n;
    if (n == 0) return std::nullopt;
    p.play.mode = SliceMode::Regions;   // the slices are regions now: PLAY / the lane strip use them as they are
    return p;
}
}
