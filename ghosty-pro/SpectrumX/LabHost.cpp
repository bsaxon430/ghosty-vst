#include "LabHost.h"
#include <juce_audio_formats/juce_audio_formats.h>

namespace lab {
namespace {
inline float readAt(const std::vector<float>& x, double pos) {
    if (pos < 0 || x.empty()) return 0.0f; const int i = (int) pos; const int n = (int) x.size(); if (i >= n) return 0.0f;
    const float f = (float) (pos - i);
    const float xm = i > 0 ? x[(size_t) i - 1] : 0.0f, x0 = x[(size_t) i], x1 = i + 1 < n ? x[(size_t) i + 1] : 0.0f, x2 = i + 2 < n ? x[(size_t) i + 2] : 0.0f;
    const float c1 = 0.5f * (x1 - xm), c2 = xm - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm) + 1.5f * (x0 - x1);
    return ((c3 * f + c2) * f + c1) * f + x0;
}
inline float cubic4(const float* h, float f) {   // h = samples at i-1, i, i+1, i+2
    const float c1 = 0.5f * (h[2] - h[0]), c2 = h[0] - 2.5f * h[1] + 2.0f * h[2] - 0.5f * h[3], c3 = 0.5f * (h[3] - h[0]) + 1.5f * (h[1] - h[2]);
    return ((c3 * f + c2) * f + c1) * f + h[1];
}
juce::String b64(const juce::MemoryBlock& m) { return m.toBase64Encoding(); }
juce::MemoryBlock unb64(const juce::String& s) { juce::MemoryBlock m; m.fromBase64Encoding(s); return m; }
bool readAudio(const juce::File& f, std::vector<float>& out, juce::String& err, int stereoIn) {
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r(fm.createReaderFor(f));
    if (!r) {
        if (!f.existsAsFile()) err = "That file isn't there any more.";
        else if (f.hasFileExtension("mp3;m4a;aac;caf;wma;mp4")) err = "Can't decode " + f.getFileExtension().toUpperCase().substring(1) + " here (it needs the macOS or Windows decoder) - use WAV, AIFF, FLAC or OGG.";
        else err = "Can't read that file - use WAV, AIFF, FLAC or OGG.";
        return false;
    }
    const int n = (int) std::min<juce::int64>(r->lengthInSamples, (juce::int64) (r->sampleRate * 30.0));   // the first 30 s
    if (n <= 0) { err = "That sound is empty."; return false; }
    juce::AudioBuffer<float> b((int) std::max(1u, r->numChannels), n); b.clear(); r->read(&b, 0, n, 0, true, true);
    std::vector<float> mono((size_t) n, 0.0f);
    const int ch = b.getNumChannels(); const float* L = b.getReadPointer(0); const float* Rr = b.getReadPointer(std::min(1, ch - 1));
    for (int i = 0; i < n; ++i) {
        if (ch < 2) { mono[(size_t) i] = L[i]; continue; }
        switch (stereoIn) { case InLeft: mono[(size_t) i] = L[i]; break; case InRight: mono[(size_t) i] = Rr[i]; break; case InSide: mono[(size_t) i] = 0.5f * (L[i] - Rr[i]); break; default: mono[(size_t) i] = 0.5f * (L[i] + Rr[i]); }
    }
    out = resampleTo48k(mono.data(), n, r->sampleRate);
    if (out.size() < 1024) { err = "That sound is too short."; return false; }
    return true;
}
juce::String stamp() { return juce::Time::getCurrentTime().formatted("%Y-%m-%d %H%M%S"); }
juce::String hexBytes(const uint8_t* p, int n, int offset) { juce::String s; for (int i = 0; i < n; ++i) s << juce::String::toHexString(((int) (offset ? (int) (int8_t) p[i] : (int) p[i]) + offset) & 0xFF).paddedLeft('0', 2); return s; }
// a stereo file from a render: L = m + w s, R = m - w s, the AUTO LEVEL gain applied, kept below 0 dBFS
bool writeStereo(const juce::File& f, const Stereo& x, float gain, float width) {
    std::vector<float> L(x.m.size()), Rt(x.m.size());
    for (size_t i = 0; i < L.size(); ++i) { const float s = i < x.s.size() ? x.s[i] * width : 0.0f; L[i] = (x.m[i] + s) * gain; Rt[i] = (x.m[i] - s) * gain; }
    const float pk = std::max(peakOf(L), peakOf(Rt)); if (pk > 0.98f) { const float g = 0.95f / pk; for (auto& v : L) v *= g; for (auto& v : Rt) v *= g; }
    return writeWav(f, L, Rt);
}
float autoGain(const std::vector<float>& m) { const float r = rmsOf(m); if (r < 1.0e-5f) return 1.0f; return juce::jlimit(0.25f, 6.0f, 0.16f / r); }   // about -16 dBFS RMS
} // namespace

// ================================================================ construction
LabHost::LabHost() : juce::Thread("Spectral Lab render") {
    const float lane[] = { 0, 0.15f, 0.3f, 0.45f, 0.6f, 0.75f, 0.9f, 1.0f }; for (int i = 0; i < 8; ++i) morphLane[(size_t) i].store(lane[i]);
    undo.getSide = [this] { return sideState(); }; undo.setSide = [this](const juce::MemoryBlock& m) { setSideState(m); };
    canvas.resize(framesFor(4.0));
    publishSplit();
    startThread(juce::Thread::Priority::low);
    requestPreview();
    startTimerHz(20);
}
LabHost::~LabHost() { alive->store(false); stopTimer(); signalThreadShouldExit(); wake.signal(); stopThread(15000); }

void LabHost::Biq::lowpass(double fc, double q) {
    const double w = juce::MathConstants<double>::twoPi * juce::jlimit(0.001, 0.49, fc), cw = std::cos(w), al = std::sin(w) / (2 * q), a0 = 1 + al;
    b0 = (float) ((1 - cw) / 2 / a0); b1 = (float) ((1 - cw) / a0); b2 = b0; a1 = (float) (-2 * cw / a0); a2 = (float) ((1 - al) / a0); z1 = z2 = 0;
}
void LabHost::BL::setup(double step, double startPos) {
    on = step > 1.02; if (!on) return;
    const double fc = 0.45 / step;   // in cycles per source sample
    f1.lowpass(fc, 0.5412); f2.lowpass(fc, 1.3065); last = (long long) std::floor(startPos) - 4; for (auto& v : h) v = 0;
}
float LabHost::BL::read(const std::vector<float>& x, double p) {
    if (p < 0) return 0.0f;
    const long long i = (long long) p, n = (long long) x.size(); if (i >= n + 2) return 0.0f;
    while (last < i + 2) { ++last; const float in = (last >= 0 && last < n) ? x[(size_t) last] : 0.0f; h[0] = h[1]; h[1] = h[2]; h[2] = h[3]; h[3] = f2.tick(f1.tick(in)); }
    return cubic4(h, (float) (p - (double) i));
}

// ================================================================ audio thread
void LabHost::prepare(double sampleRate, int) { hostRate = sampleRate > 0 ? sampleRate : 48000.0; for (auto& v : voices) v.on = false; loopVoice = Voice(); limGain = 1.0f; scrub = Scrub(); }

void LabHost::stopAll() {
    for (auto& v : voices) v.on = false;
    loopVoice.on = false; lastLoopIndex = -1; previewPlaying.store(false); audition.store(-100); scrub = Scrub(); previewBeats = 0; previewSamples = 0;
    playPos.store(-1.0); playingGhost.store(-1);
}
double LabHost::heardSeconds(const PlayBank& b, int out) const {
    if (out == OutGhost && !b.ghosts.empty() && b.ghosts[0]) return b.ghosts[0]->size() / SR;
    if (out == OutMorph && !b.morphs.empty() && b.morphs.front() && b.morphs.back()) { const float t = juce::jlimit(0.0f, 1.0f, morphTarget.load(std::memory_order_relaxed)); return (b.morphs.front()->size() * (1 - t) + b.morphs.back()->size() * t) / SR; }
    return b.main ? b.main->size() / SR : 1.0;
}

void LabHost::renderVoice(Voice& v, const PlayBank& b, const SplitSetup* s, const LabParams& p, double bpm, double beatsOverride, float morph, bool loop, float& yM, float& yS) {
    const double beats = loop ? beatsOverride : v.el * bpm / 60.0;
    const double secs = beats * 60.0 / bpm, sps = SR * v.rate;
    auto rd = [&](const Audio& a, double pos, float g = 1.0f) {
        if (!a) return;
        if (v.blM.on && !loop) { yM += g * v.blM.read(a->m, pos); if (!a->s.empty()) yS += g * v.blS.read(a->s, pos); }
        else { yM += g * readAt(a->m, pos); if (!a->s.empty()) yS += g * readAt(a->s, pos); }
    };
    auto ends = [&](double pos, size_t size) { if (!loop && (pos > (double) size + 4 || (v.end > 0 && pos > v.end))) v.on = false; };
    const int out = p.output;
    if (out == OutGhost && !b.ghosts.empty()) {
        const int n = (int) b.ghosts.size(); const int idx = p.ghostHit > 0 ? (p.ghostHit - 1) % n : ((v.ghost % n) + n) % n;
        const auto& g = b.ghosts[(size_t) idx]; if (!g) return; const double pos = v.start + secs * sps; ends(pos, g->size()); rd(g, pos); return;
    }
    if (out == OutSplit && s && !b.bands.empty()) {
        const float coef = 1.0f - std::exp(-1.0f / (0.0015f * (float) hostRate));
        for (size_t i = 0; i < b.bands.size() && i < (size_t) kMaxBands; ++i) {
            const auto e = evalBand(s->seq[i], beats, (int) i, s->swing, s->seed, ((p.mutes >> i) & 1u) != 0); v.g[i] += (e.gain - v.g[i]) * coef;
            if (v.g[i] > 1.0e-5f && b.bands[i]) { const double pos = e.srcBeats * 60.0 / bpm * sps; yM += v.g[i] * readAt(b.bands[i]->m, pos); if (!b.bands[i]->s.empty()) yS += v.g[i] * readAt(b.bands[i]->s, pos); }
        }
        if (b.main) ends(secs * sps, b.main->size());
        return;
    }
    if (out == OutMorph && b.morphs.size() >= 2) {
        const float m = juce::jlimit(0.0f, 1.0f, morph);
        auto play = [&](const Audio& a, float g, double len) { if (!a) return; const double u = loop ? beats * 60.0 / bpm * SR / std::max(1.0, len) : secs * sps / std::max(1.0, len); if (!loop && u > 1.0) v.on = false; rd(a, u * a->size(), g); };
        if (b.morphExact && std::abs(m - b.morphExactT) < 0.01f) { play(b.morphExact, 1.0f, (double) b.morphExact->size()); return; }
        if (laneOn.load(std::memory_order_relaxed)) for (int i = 0; i < 8; ++i) if (b.laneMorphs[(size_t) i] && std::abs(m - b.laneVals[(size_t) i]) < 0.002f) { play(b.laneMorphs[(size_t) i], 1.0f, (double) b.laneMorphs[(size_t) i]->size()); return; }
        const float x = m * (float) (b.morphs.size() - 1); const int i = std::min((int) b.morphs.size() - 2, (int) x); const float a = x - i;
        const auto& m0 = b.morphs[(size_t) i]; const auto& m1 = b.morphs[(size_t) i + 1]; if (!m0 || !m1) return;
        const double len = m0->size() + (m1->size() - (double) m0->size()) * a;
        play(m0, 1 - a, len); play(m1, a, len);
        return;
    }
    if (!b.main) return;
    if (v.pitchSlot >= 0 && b.pitched[(size_t) v.pitchSlot] && !loop) {   // SPECTRAL keys: the canvas re-rendered at this pitch, same length
        const auto& a = b.pitched[(size_t) v.pitchSlot]; const double pos = v.start + secs * SR; ends(pos, a->size()); yM += readAt(a->m, pos); if (!a->s.empty()) yS += readAt(a->s, pos); return;
    }
    const double pos = v.start + secs * sps; ends(pos, b.main->size()); rd(b.main, pos);
}

void LabHost::process(juce::AudioBuffer<float>& outBuf, const juce::MidiBuffer& midi, double bpm, double ppq, bool hostPlaying, const LabParams& p, juce::AudioBuffer<float>* transportOut) {
    PlayBank* b = banks.get(); const SplitSetup* s = splits.get();
    const bool isOpen = open.load();
    morphTarget.store(laneOn.load(std::memory_order_relaxed) ? morphTarget.load(std::memory_order_relaxed) : p.morph, std::memory_order_relaxed);
    if (!b || outBuf.getNumChannels() < 2) { playPos.store(-1.0); meter.store(0); return; }
    bpm = juce::jlimit(20.0, 400.0, bpm > 0 ? bpm : 120.0);
    const int n = outBuf.getNumSamples(); float* L = outBuf.getWritePointer(0); float* Rt = outBuf.getWritePointer(1);
    const bool split = transportOut != nullptr && transportOut->getNumChannels() >= 2 && transportOut->getNumSamples() >= n;
    float* TL = split ? transportOut->getWritePointer(0) : nullptr; float* TR = split ? transportOut->getWritePointer(1) : nullptr;
    const bool keys = playKeys.load(), shot = oneShot.load(), lane = laneOn.load(), velM = velMorph.load();
    const double lenSec = std::max(0.05, heardSeconds(*b, p.output)); const double loopBeats = std::max(1.0, std::round(lenSec * bpm / 60.0));
    const double rs = regionStart.load(), re = regionEnd.load(); const bool haveRegion = rs >= 0 && re > rs + 256;
    const bool preview = previewPlaying.load() && isOpen;
    const bool regionLoop = preview && !hostPlaying && loopRegion.load() && haveRegion && p.output == OutCanvas && b->main;
    const float ps = juce::jlimit(0.0f, 0.999f, playStart.load()), pe = juce::jlimit(ps + 0.001f, 1.0f, playEnd.load());
    const bool loopOn = isOpen && ((hostPlaying && !keys) || preview) && !regionLoop;
    const float atk = 1.0f - std::exp(-1.0f / (0.002f * (float) hostRate)), rel = 1.0f - std::exp(-1.0f / (0.06f * (float) hostRate));
    const float limRel = 1.0f - std::exp(-1.0f / (0.12f * (float) hostRate)), scrubFade = 1.0f / (0.012f * (float) hostRate);
    const int root = rootNote.load(); const bool spectralKeys = keysPitch.load() == PitchSpectral && p.output == OutCanvas;
    const double step48 = SR / hostRate;   // source samples per output sample at rate 1
    auto startVoice = [&](Voice& v, int note, float vel, double rate, int slot) {
        v = Voice(); v.on = true; v.note = note; v.vel = vel; v.rate = rate; v.pitchSlot = slot; v.ghost = ghostCounter++; for (auto& g : v.g) g = 1.0f;
        if (slot < 0 || !b->pitched[(size_t) slot]) { v.blM.setup(rate * step48, 0); v.blS.setup(rate * step48, 0); }
    };
    if (const int a = audition.exchange(-100); a != -100) {   // a one-shot from the UI (the sound, the region, or one ghost variation)
        Voice* v = &voices[0]; for (auto& x : voices) if (!x.on) { v = &x; break; }
        startVoice(*v, root, 0.85f, 1.0, -1); if (a >= 0) v->ghost = a;
        if (a == -2 && haveRegion) { v->start = rs; v->end = re; v->blM.setup(step48, rs); v->blS.setup(step48, rs); }
    }
    float blockPeak = 0; bool clipped = false; double lastPos = -1; bool ghostAudible = false; int ghostShown = -1;
    auto it = midi.cbegin();
    for (int i = 0; i < n; ++i) {
        for (; it != midi.cend() && (*it).samplePosition <= i; ++it) {
            if (!keys || !isOpen) continue;
            const auto m = (*it).getMessage();
            if (m.isNoteOn()) {
                Voice* v = nullptr; for (auto& x : voices) if (!x.on) { v = &x; break; }
                if (!v) { v = &voices[0]; for (auto& x : voices) if (x.el > v->el) v = &x; }
                const int semis = m.getNoteNumber() - root; int slot = -1;
                if (spectralKeys && std::abs(semis) <= kPitchRange) { slot = semis + kPitchRange; if (semis != 0 && !b->pitched[(size_t) slot]) wantPitch.fetch_or((juce::uint64) 1 << slot); }
                const bool ready = slot >= 0 && (semis == 0 || b->pitched[(size_t) slot]);
                startVoice(*v, m.getNoteNumber(), m.getFloatVelocity(), ready ? 1.0 : std::exp2(semis / 12.0), semis == 0 ? -1 : slot);   // not rendered yet: TAPE for this note
            } else if (m.isNoteOff()) { if (!shot) for (auto& x : voices) if (x.on && x.note == m.getNoteNumber()) x.releasing = true; }
            else if (m.isAllNotesOff() || m.isAllSoundOff()) for (auto& x : voices) x.releasing = true;
        }
        float yM = 0, yS = 0, tM = 0, tS = 0;   // tM / tS: the host-following loop when it is routed apart (PLAY: ON NOTES)
        for (auto& v : voices) {
            if (!v.on) continue;
            v.env += ((v.releasing ? 0.0f : 1.0f) - v.env) * (v.releasing ? rel : atk);
            if (v.releasing && v.env < 1.0e-4f) { v.on = false; continue; }
            const double beatsAbs = v.el * bpm / 60.0;
            const float mm = velM ? v.vel : (lane ? morphLane[(size_t) ((long long) (beatsAbs / 4.0) % 8)].load(std::memory_order_relaxed) : p.morph);
            float vm = 0, vs = 0; renderVoice(v, *b, s, p, bpm, 0, mm, false, vm, vs);
            const float g = v.env * (0.35f + 0.65f * v.vel); yM += vm * g; yS += vs * g;
            if (p.output == OutGhost) { ghostAudible = true; ghostShown = v.ghost; }
            v.el += 1.0 / hostRate; lastPos = std::min(1.0, (v.start + v.el * SR * v.rate) / std::max(1.0, lenSec * SR));
        }
        if (regionLoop) {   // PLAY with LOOP SEL: the selected part of the canvas loops
            const double len = re - rs; const double pos = rs + std::fmod(previewSamples, len);
            yM += readAt(b->main->m, pos); if (!b->main->s.empty()) yS += readAt(b->main->s, pos);
            previewSamples += step48; lastPos = pos / std::max(1.0, lenSec * SR);
        } else if (loopOn) {
            const double beatsAbs = hostPlaying ? ppq + i / hostRate * bpm / 60.0 : previewBeats;
            double bl = std::fmod(std::max(0.0, beatsAbs), loopBeats); const long long idx = (long long) std::floor(std::max(0.0, beatsAbs) / loopBeats);
            if (!hostPlaying && (ps > 0.0f || pe < 1.0f)) {   // the Lab's PLAY between START and END (the WaveRegion flags); untouched = exactly as before
                const double a = ps * loopBeats, len = std::max(1.0e-3, (pe - ps) * loopBeats); bl = a + std::fmod(std::max(0.0, beatsAbs), len); }
            if (idx != lastLoopIndex) { lastLoopIndex = idx; loopVoice.ghost = ghostCounter++; loopVoice.on = true; loopVoice.rate = 1; loopVoice.pitchSlot = -1; for (auto& g : loopVoice.g) if (g == 0) g = 1; }
            const float mm = lane ? morphLane[(size_t) ((long long) (beatsAbs / 4.0) % 8)].load(std::memory_order_relaxed) : p.morph;
            float vm = 0, vs = 0; renderVoice(loopVoice, *b, s, p, bpm, bl, mm, true, vm, vs);
            if (split && hostPlaying) { tM += vm; tS += vs; } else { yM += vm; yS += vs; }
            if (p.output == OutGhost) { ghostAudible = true; ghostShown = loopVoice.ghost; }
            if (!hostPlaying) previewBeats += bpm / 60.0 / hostRate;
            lastPos = bl / loopBeats;
        }
        // scrub: click-hold on the ruler plays from the held spot; a jump crossfades to the new spot
        if (const double sp = scrubPos.load(std::memory_order_relaxed); sp >= 0 && isOpen && b->main) {
            if (!scrub.on) { scrub.on = true; scrub.active = 0; scrub.head[0] = sp; scrub.head[1] = sp; scrub.fade = 1; }
            else if (std::abs(scrub.head[scrub.active] - sp) > 0.06 * SR) { scrub.active ^= 1; scrub.head[scrub.active] = sp; scrub.fade = 0; }
            const int a = scrub.active; const float fa = scrub.fade;
            yM += fa * readAt(b->main->m, scrub.head[a]) + (1 - fa) * readAt(b->main->m, scrub.head[a ^ 1]);
            if (!b->main->s.empty()) yS += fa * readAt(b->main->s, scrub.head[a]) + (1 - fa) * readAt(b->main->s, scrub.head[a ^ 1]);
            scrub.head[0] += step48; scrub.head[1] += step48; scrub.fade = std::min(1.0f, scrub.fade + scrubFade);
            lastPos = scrub.head[a] / std::max(1.0, lenSec * SR);
        } else scrub.on = false;
        const float gain = p.level * b->gain, w = juce::jlimit(0.0f, 1.0f, p.width);
        float l = (yM + w * yS) * gain, r = (yM - w * yS) * gain;
        if (!std::isfinite(l) || !std::isfinite(r)) { l = r = 0; }
        float tl = 0, tr = 0;
        if (split) { tl = (tM + w * tS) * gain; tr = (tM - w * tS) * gain; if (!std::isfinite(tl) || !std::isfinite(tr)) tl = tr = 0; }
        // limiter: never lets the lab go past -0.4 dBFS (instant attack, 120 ms release); one gain for both parts
        const float pk = split ? std::max(std::abs(l + tl), std::abs(r + tr)) : std::max(std::abs(l), std::abs(r));
        if (pk > 1.0f) clipped = true;
        if (pk * limGain > 0.955f) limGain = 0.955f / pk; else limGain += (1.0f - limGain) * limRel;
        l *= limGain; r *= limGain; blockPeak = std::max(blockPeak, std::max(std::abs(l), std::abs(r)));
        L[i] += l; Rt[i] += r;
        if (split) { TL[i] += tl * limGain; TR[i] += tr * limGain; }
    }
    if (!loopOn) { lastLoopIndex = -1; if (!preview) previewBeats = 0; }
    if (!regionLoop) previewSamples = 0;
    if (clipped) clipCount.fetch_add(1, std::memory_order_relaxed);
    meter.store(std::max(blockPeak, meter.load(std::memory_order_relaxed) * 0.9f), std::memory_order_relaxed);
    playPos.store(lastPos);
    const int ng = (int) b->ghosts.size();
    playingGhost.store(ghostAudible && ng > 0 ? (p.ghostHit > 0 ? (p.ghostHit - 1) % ng : ((ghostShown % ng) + ng) % ng) : -1);
}

// ================================================================ message thread
void LabHost::newCanvas(double seconds) { undo.beginAll(canvas); canvas.resize(framesFor(seconds)); sourceAudio.clear(); sourceMade = false; sourceName = {}; image = {}; ++sourceRev; edited(); requestPreview(); }
bool LabHost::importAudio(const juce::File& f, juce::String& err) {
    std::vector<float> x; if (!readAudio(f, x, err, stereoIn.load())) return false;
    undo.beginAll(canvas); analyse(x, canvas); sourceAudio = x; sourceMade = false; sourceName = f.getFileNameWithoutExtension(); image = {}; ++sourceRev;
    rootNote.store(detectRoot(x)); edited(); requestPreview(); return true;
}
void LabHost::importAudioAsync(const juce::File& f, std::function<void(bool, const juce::String&)> done) {
    const int mode = stereoIn.load(); auto alv = alive; busy.store(1);
    post([this, f, mode, done, alv] {
        std::vector<float> x; juce::String err; auto c = std::make_shared<Canvas>(); int root = 60;
        const bool ok = readAudio(f, x, err, mode);
        if (ok) { analyse(x, *c); root = detectRoot(x); }
        if (cancelled()) return;
        auto xs = std::make_shared<std::vector<float>>(std::move(x));
        juce::MessageManager::callAsync([this, ok, err, c, xs, root, f, done, alv] {
            if (!alv->load()) return;
            if (!ok) { if (done) done(false, err); return; }
            undo.beginAll(canvas); canvas = std::move(*c); sourceAudio = std::move(*xs); sourceMade = false; sourceName = f.getFileNameWithoutExtension(); image = {}; ++sourceRev;
            rootNote.store(root); edited(); requestPreview();
            if (done) done(true, "\"" + sourceName + "\" is on the canvas. Paint on it, erase, freeze, shift...");
        });
    });
}
void LabHost::setImage(const juce::Image& img, bool undoable) { image = img; remapImage(undoable); }
void LabHost::remapImage(bool undoable) {
    if (!image.isValid()) return;
    if (undoable) undo.beginAll(canvas);
    imageMap.sanitise(); imageToCanvas(image, imageMap, canvas); sourceAudio.clear(); sourceMade = false; sourceName = {}; ++sourceRev; edited(); requestPreview();
}
void LabHost::remapImageAsync() {
    if (!image.isValid()) return;
    if (remapBusy) { remapAgain = true; return; }
    remapBusy = true; imageMap.sanitise(); auto img = image; const auto m = imageMap; auto alv = alive;
    post([this, img, m, alv] {
        auto c = std::make_shared<Canvas>(); imageToCanvas(img, m, *c);
        if (cancelled()) return;
        juce::MessageManager::callAsync([this, c, alv] {
            if (!alv->load()) return;
            remapBusy = false;
            if (image.isValid()) { canvas = std::move(*c); sourceAudio.clear(); sourceMade = false; sourceName = {}; ++sourceRev; edited(); requestPreview(); }
            if (remapAgain) { remapAgain = false; remapImageAsync(); }
        });
    });
}
bool LabHost::importImage(const juce::File& f) { auto img = juce::ImageFileFormat::loadFrom(f); if (!img.isValid()) return false; setImage(img); return true; }
bool LabHost::loadMorphSound(int which, const juce::File& f, juce::String& err) {
    std::vector<float> x; if (!readAudio(f, x, err, stereoIn.load())) return false;
    (which == 0 ? audioA : audioB) = x; (which == 0 ? aMade : bMade) = false; (which == 0 ? nameA : nameB) = f.getFileNameWithoutExtension(); ++(which == 0 ? aRev : bRev);
    requestMorph(); return true;
}
void LabHost::loadMorphSoundAsync(int which, const juce::File& f, std::function<void(bool, const juce::String&)> done) {
    const int mode = stereoIn.load(); auto alv = alive;
    post([this, which, f, mode, done, alv] {
        auto x = std::make_shared<std::vector<float>>(); juce::String err; const bool ok = readAudio(f, *x, err, mode);
        if (cancelled()) return;
        juce::MessageManager::callAsync([this, which, f, ok, err, x, done, alv] {
            if (!alv->load()) return;
            if (!ok) { if (done) done(false, err); return; }
            (which == 0 ? audioA : audioB) = std::move(*x); (which == 0 ? aMade : bMade) = false; (which == 0 ? nameA : nameB) = f.getFileNameWithoutExtension(); ++(which == 0 ? aRev : bRev);
            requestMorph(); if (done) done(true, juce::String(which == 0 ? "A" : "B") + " loaded, analysing...");
        });
    });
}
void LabHost::useCanvasAs(int which, std::function<void()> done) {
    auto cp = std::make_shared<Canvas>(canvas); auto alv = alive; const bool kt = keepTransients.load();
    post([this, which, cp, done, alv, kt] {
        RenderOpts o; o.side = false; o.keepTransients = kt; auto y = std::make_shared<std::vector<float>>(renderStereo(*cp, Quality::HQ, o).m);
        if (cancelled()) return;
        juce::MessageManager::callAsync([this, which, y, done, alv] {
            if (!alv->load()) return;
            (which == 0 ? audioA : audioB) = std::move(*y); (which == 0 ? aMade : bMade) = true; (which == 0 ? nameA : nameB) = "Canvas"; ++(which == 0 ? aRev : bRev);
            requestMorph(); if (done) done();
        });
    });
}
std::vector<float> LabHost::processSource() {
    bool untouched = !sourceAudio.empty() && canvas.srcLen == (int) sourceAudio.size() && (int) canvas.touched.size() == canvas.T;
    if (untouched) for (auto t : canvas.touched) if (t) { untouched = false; break; }
    for (float a : canvas.amp) if (untouched && a > 0) { untouched = false; break; }   // paint on top of the import: render it
    if (untouched) return sourceAudio;
    RenderOpts o; o.side = false; o.keepTransients = keepTransients.load(); return renderStereo(canvas, Quality::HQ, o).m;
}
void LabHost::setProcessed(const std::vector<float>& x, const juce::String& name, bool keepPrevious) {
    if (x.empty()) return;
    if (keepPrevious) { kept.push_back({ sourceName.isNotEmpty() ? sourceName : juce::String("Lab sound"), processSource() }); if (kept.size() > 8) kept.erase(kept.begin()); }
    undo.beginAll(canvas); analyse(x, canvas); sourceAudio = x; sourceMade = true; sourceName = name; image = {}; ++sourceRev; edited(); requestPreview();   // a PROCESS result: plugin-made
}
void LabHost::useKept(int index) {
    if (index < 0 || index >= (int) kept.size()) return;
    auto k = std::move(kept[(size_t) index]); kept.erase(kept.begin() + index);
    kept.push_back({ sourceName.isNotEmpty() ? sourceName : juce::String("Lab sound"), processSource() });
    setProcessed(k.audio, k.name, false);
}
void LabHost::snapParams() { pGhost = ghost; pGhost.keepTransients = keepTransients.load(); pSplit = split; pOpts.keepTransients = keepTransients.load(); pOpts.seed = ghost.seed; for (int i = 0; i < 8; ++i) pLane[(size_t) i] = morphLane[(size_t) i].load(); }
void LabHost::post(std::function<void()> task) { { const juce::ScopedLock sl(jobLock); tasks.push_back(std::move(task)); } jobs.fetch_or(JobTasks); wake.signal(); }
std::unique_ptr<LabHost::Patch> LabHost::Patch::of(const Canvas& c, int t0, int t1) {
    auto p = std::make_unique<Patch>(); p->T = c.T; p->srcLen = c.srcLen; p->t0 = juce::jlimit(0, std::max(0, c.T - 1), t0); p->t1 = juce::jlimit(p->t0, std::max(0, c.T - 1), t1);
    if (c.T <= 0) { p->t1 = -1; return p; }
    p->cells.assign(c.col(p->t0), c.col(p->t1) + K);
    const auto a = (ptrdiff_t) p->t0 * R, b = (ptrdiff_t) (p->t1 + 1) * R;
    p->amp.assign(c.amp.begin() + a, c.amp.begin() + b); p->fq.assign(c.fq.begin() + a, c.fq.begin() + b); p->noise.assign(c.noise.begin() + a, c.noise.begin() + b);
    if ((int) c.touched.size() == c.T) p->touched.assign(c.touched.begin() + p->t0, c.touched.begin() + p->t1 + 1);
    return p;
}
void LabHost::Patch::applyTo(Canvas& c) const {
    if (c.T != T || t1 < t0) return;
    c.srcLen = srcLen; std::copy(cells.begin(), cells.end(), c.cells.begin() + (ptrdiff_t) t0 * K);
    const auto a = (ptrdiff_t) t0 * R; std::copy(amp.begin(), amp.end(), c.amp.begin() + a); std::copy(fq.begin(), fq.end(), c.fq.begin() + a); std::copy(noise.begin(), noise.end(), c.noise.begin() + a);
    if (!touched.empty()) { if ((int) c.touched.size() != c.T) c.touched.assign((size_t) c.T, 1); std::copy(touched.begin(), touched.end(), c.touched.begin() + t0); }
}
void LabHost::requestPreview() {
    { const juce::ScopedLock sl(jobLock);
      if (dirtyFull || sentT != canvas.T || needFull.exchange(false)) { pendingCanvas = std::make_unique<Canvas>(canvas); pendingPatch.reset(); lastPreviewCopyFrames = canvas.T; }
      else if (dirtyT1 >= dirtyT0) {
          int a = juce::jlimit(0, std::max(0, canvas.T - 1), dirtyT0), b = juce::jlimit(0, std::max(0, canvas.T - 1), dirtyT1);
          if (pendingCanvas) { Patch::of(canvas, a, b)->applyTo(*pendingCanvas); }   // the worker hasn't taken the last copy yet: refresh it in place
          else { if (pendingPatch) { a = std::min(a, pendingPatch->t0); b = std::max(b, pendingPatch->t1); } pendingPatch = Patch::of(canvas, a, b); }
          lastPreviewCopyFrames = b - a + 1;
      } else lastPreviewCopyFrames = 0;
      sentT = canvas.T; dirtyFull = false; dirtyT0 = 1 << 30; dirtyT1 = -1;
      workRev = canvasRev.load(); workSourceMatch = !sourceAudio.empty() && canvas.srcLen == (int) sourceAudio.size(); snapParams(); }
    dirty = false; lastRequestMs = juce::Time::getMillisecondCounter();
    int j = JobPreview; if (output.load() == OutGhost) j |= JobGhost;
    jobs.fetch_or(j); wake.signal();
}
void LabHost::requestGhost() { requestPreview(); jobs.fetch_or(JobGhost); wake.signal(); }
void LabHost::requestSplit() { publishSplit(); { const juce::ScopedLock sl(jobLock); snapParams(); } jobs.fetch_or(JobSplit); wake.signal(); }
void LabHost::requestMorph() {
    if (audioA.empty() || audioB.empty()) return;
    { const juce::ScopedLock sl(jobLock); pendingA = audioA; pendingB = audioB; snapParams(); }
    jobs.fetch_or(JobMorphPrep | JobMorph | JobLane); wake.signal();
}
void LabHost::requestLane() { { const juce::ScopedLock sl(jobLock); snapParams(); } jobs.fetch_or(JobLane); wake.signal(); }
void LabHost::publishSplit() { split.sanitise(); splits.publish(std::make_unique<SplitSetup>(split)); }
void LabHost::sideEdited() { publishSplit(); ++canvasRev; }
void LabHost::generateChaos() {
    undo.beginAll(canvas);
    Canvas follow; const Canvas* f = nullptr;
    if (canvas.hasStft()) f = &canvas; else if (!sourceAudio.empty()) { analyse(sourceAudio, follow); f = &follow; }
    sanitise(chaos); chaosPaint(canvas, chaos, locks, f); edited(); requestPreview();
}
void LabHost::timerCallback() {
    banks.collect(); splits.collect();
    const juce::uint32 gap = canvas.T > 1500 ? 200 : 60;   // long canvases: fewer copies while painting
    if ((dirty || needFull.load()) && juce::Time::getMillisecondCounter() - lastRequestMs > gap) { if (needFull.load()) dirtyFull = true; requestPreview(); }
}
void LabHost::publishBank() {
    auto pb = std::make_unique<PlayBank>();
    { const juce::ScopedLock sl(resultLock);
      pb->main = rMain; pb->ghosts = rGhosts; pb->bands = rBands; pb->morphs = rMorphs; pb->ghostThumbs = rThumbs; pb->ghostStages = rStages;
      pb->morphExact = rExact; pb->morphExactT = rExactT; pb->laneMorphs = rLane; pb->laneVals = rLaneVals; pb->pitched = rPitched; pb->gain = rGain; pb->version = ++bankCounter;
      uiBank = std::make_shared<PlayBank>(*pb); }
    banks.publish(std::move(pb)); bankVersion.fetch_add(1);
}

// ================================================================ the worker
juce::String LabHost::hqKey(juce::uint32 rev, int out, float morphT, double bpm) const {
    return juce::String((juce::int64) rev) + "|" + juce::String(out) + "|" + juce::String(out == OutMorph ? morphT : 0.0f, 3) + "|" + juce::String(out == OutSplit || out == OutGhost ? bpm : 0.0, 2) + "|" + (keepTransients.load() ? "T" : "t") + juce::String(ghost.count) + "," + juce::String(ghost.evolve, 3) + "," + juce::String(ghost.memory, 3) + "," + juce::String(ghost.decay, 3) + "," + juce::String((juce::int64) ghost.seed);
}
void LabHost::run() {
    Canvas work; std::vector<float> A, B; std::shared_ptr<MorphPrep> prep; GhostParams gp; SplitSetup sp; RenderOpts opts; std::array<float, 8> laneV {}; juce::uint32 rev = 0; bool srcMatch = false;
    while (!threadShouldExit()) {
        wake.wait(30);
        if (threadShouldExit()) break;
        int j = jobs.exchange(0); const juce::uint64 pitchBits = wantPitch.exchange(0);
        const juce::uint32 now = juce::Time::getMillisecondCounter();
        // MORPH followed exactly: when the value has settled (or every 250 ms while it keeps moving)
        const float target = morphTarget.load();
        if (target != lastTargetSeen) { lastTargetSeen = target; targetChangedMs = now; }
        float exactT; { const juce::ScopedLock sl(resultLock); exactT = rExactT; }
        const bool exactDue = prep && std::abs(target - exactT) > 0.002f && (now - targetChangedMs > 40 || now - lastExactMs > 250) && output.load() == OutMorph;
        const bool hqDue = rev != 0 && now - lastJobMs > 350 && (hqWanted.load() || open.load());
        if (j == 0 && pitchBits == 0 && !exactDue && !hqDue) continue;
        busy.store(1);
        std::deque<std::function<void()>> todo;
        { const juce::ScopedLock sl(jobLock);
          if (pendingCanvas) { work = std::move(*pendingCanvas); pendingCanvas.reset(); rev = workRev; srcMatch = workSourceMatch; }
          if (pendingPatch) { if (pendingPatch->T == work.T) pendingPatch->applyTo(work); else needFull = true; pendingPatch.reset(); rev = workRev; srcMatch = workSourceMatch; }
          if (!pendingA.empty()) { A = std::move(pendingA); B = std::move(pendingB); pendingA.clear(); pendingB.clear(); }
          gp = pGhost; sp = pSplit; opts = pOpts; laneV = pLane; todo.swap(tasks); }
        if (j != 0) lastJobMs = now;
        bool changed = false;
        if (j & JobPreview) {
            auto m = std::make_shared<Stereo>(renderStereo(work, Quality::Preview, opts)); if (threadShouldExit()) break;
            const float g = autoLevel.load() ? autoGain(m->m) : 1.0f;
            { const juce::ScopedLock sl(resultLock); rMain = m; rGain = g; rMainRev = rev; rPitched = {}; hqCache.reset(); hqCacheKey = {}; }
            { auto blob = b64(save(work, srcMatch)); const juce::ScopedLock sl(stateLock); canvasBlob = blob; canvasBlobRev = rev; }
            if (!rBands.empty() || output.load() == OutSplit) j |= JobSplit;
            changed = true;
        }
        if ((j & JobSplit) && rMain) {
            auto bm = splitBands(rMain->m, sp); auto bs = rMain->s.empty() ? std::vector<std::vector<float>>() : splitBands(rMain->s, sp);
            std::vector<Audio> v; for (size_t i = 0; i < bm.size(); ++i) { auto st = std::make_shared<Stereo>(); st->m = std::move(bm[i]); if (i < bs.size()) st->s = std::move(bs[i]); v.push_back(st); }
            if (threadShouldExit()) break;
            const juce::ScopedLock sl(resultLock); rBands = v; changed = true;
        }
        if (j & JobGhost) {
            const int maxF = framesFor(kGhostMaxSeconds); ghostTrimmed.store(work.T > maxF ? 1 : 0);
            const Canvas src = trimmed(work, maxF); std::vector<Audio> v; std::vector<juce::Image> th; std::vector<juce::String> stg;
            ghostEach(src, gp, [&](int i, const Canvas& c) {
                RenderOpts o = opts; o.seed = gp.seed; v.push_back(std::make_shared<Stereo>(renderStereo(c, Quality::Preview, o)));
                th.push_back(canvasImage(c, 96, 64, 60.0f)); stg.push_back(ghostStage(i, gp.count, gp.evolve)); return !threadShouldExit(); });
            if (threadShouldExit()) break;
            const juce::ScopedLock sl(resultLock); rGhosts = v; rThumbs = th; rStages = stg; changed = true;
        }
        if ((j & JobMorphPrep) && !A.empty() && !B.empty()) { auto np = morphPrepare(A, B); if (threadShouldExit()) break; if (np) { prep = np; const juce::ScopedLock sl(resultLock); morphPrep = prep; rExact.reset(); rExactT = -1; } }
        if ((j & JobMorph) && prep) {
            const bool shortOnes = prep->A.T <= framesFor(4.0) && prep->B.T <= framesFor(4.0); const int pts = shortOnes ? 17 : 9;
            std::vector<Audio> v; Canvas c;
            for (int i = 0; i < pts && !threadShouldExit(); ++i) { morph(*prep, i / (float) (pts - 1), c); RenderOpts o = opts; v.push_back(std::make_shared<Stereo>(renderStereo(c, Quality::Preview, o))); }
            if (threadShouldExit()) break;
            const juce::ScopedLock sl(resultLock); rMorphs = v; changed = true;
        }
        if ((j & JobLane) && prep) {
            std::array<Audio, 8> la {}; Canvas c;
            for (int i = 0; i < 8 && !threadShouldExit(); ++i) {
                for (int k = 0; k < i; ++k) if (laneV[(size_t) k] == laneV[(size_t) i]) la[(size_t) i] = la[(size_t) k];
                if (!la[(size_t) i]) { morph(*prep, laneV[(size_t) i], c); la[(size_t) i] = std::make_shared<Stereo>(renderStereo(c, Quality::Preview, opts)); }
            }
            if (threadShouldExit()) break;
            const juce::ScopedLock sl(resultLock); rLane = la; rLaneVals = laneV; changed = true;
        }
        if (exactDue || ((j & JobMorph) && prep && output.load() == OutMorph)) {
            const float t = morphTarget.load(); Canvas c; morph(*prep, t, c); auto e = std::make_shared<Stereo>(renderStereo(c, Quality::Preview, opts));
            if (threadShouldExit()) break;
            { const juce::ScopedLock sl(resultLock); rExact = e; rExactT = t; } lastExactMs = juce::Time::getMillisecondCounter(); changed = true;
        }
        if (pitchBits != 0 && work.T > 0) {
            for (int slot = 0; slot <= 2 * kPitchRange && !threadShouldExit(); ++slot) {
                if (!((pitchBits >> slot) & 1u)) continue;
                { const juce::ScopedLock sl(resultLock); if (rPitched[(size_t) slot]) continue; }
                Canvas c = work; Sel all; all.t0 = 0; all.t1 = c.T; all.r0 = 0; all.r1 = R - 1; shift(c, all, slot - kPitchRange, opts.keepTransients);
                auto a = std::make_shared<Stereo>(renderStereo(c, Quality::Preview, opts));
                const juce::ScopedLock sl(resultLock); if (rMainRev == rev) rPitched[(size_t) slot] = a; changed = true;
            }
        }
        for (auto& t : todo) { if (threadShouldExit()) break; t(); }
        if (threadShouldExit()) break;
        // DRAG WAV OUT: the high-quality file is rendered while idle, so the drag itself is instant
        if (hqDue && jobs.load() == 0 && rev != 0) {
            const int out = output.load(); const double bpm = tempo.load(); const float mt = morphTarget.load();
            const auto key = hqKey(rev, out, mt, bpm); juce::String have; { const juce::ScopedLock sl(resultLock); have = hqCacheKey; }
            if (key != have) {
                Snapshot s; s.canvas = work; s.ghost = gp; s.split = sp; s.output = out; s.morphT = mt; s.prep = prep; s.bpm = bpm; s.opts = opts;
                auto y = std::make_shared<Stereo>(renderOutput(s));
                if (threadShouldExit()) break;
                const juce::ScopedLock sl(resultLock); hqCache = y; hqCacheKey = key;
            }
            hqWanted.store(false);
        }
        if (changed) publishBank();
        busy.store(0);
    }
    busy.store(0);
}

// ================================================================ exports
LabHost::Snapshot LabHost::snapshot(double bpm) const {
    Snapshot s; s.canvas = canvas; s.ghost = ghost; s.ghost.keepTransients = keepTransients.load(); s.split = split; s.output = output.load(); s.morphT = morphNow.load(); s.bpm = bpm;
    { const juce::ScopedLock sl(resultLock); s.prep = morphPrep; s.gain = rGain; }
    s.width = width.load(); s.opts.keepTransients = keepTransients.load(); s.opts.seed = ghost.seed;
    s.base = "Spectral " + (sourceName.isNotEmpty() ? sourceName : juce::String("Paint"));
    return s;
}
Stereo LabHost::renderOutput(const Snapshot& s) {
    RenderOpts o = s.opts;
    if (s.output == OutSplit) {
        auto main = renderStereo(s.canvas, Quality::HQ, o);
        const double beats = std::max(1.0, std::round(main.size() / SR * s.bpm / 60.0)); const int len = (int) std::lround(beats * 60.0 / s.bpm * SR);
        main.m.resize((size_t) len, 0.0f); main.s.resize((size_t) len, 0.0f);
        Stereo y; y.m = renderSplit(splitBands(main.m, s.split), s.split, s.bpm, len); y.s = renderSplit(splitBands(main.s, s.split), s.split, s.bpm, len); return y;
    }
    if (s.output == OutGhost) {
        const Canvas src = trimmed(s.canvas, framesFor(kGhostMaxSeconds)); const int step = std::max(src.lengthSamples(), (int) (60.0 / s.bpm * SR));
        const int n = juce::jlimit(1, kMaxGhosts, s.ghost.count); Stereo y; y.m.assign((size_t) step * (size_t) n + (size_t) src.lengthSamples(), 0.0f); y.s = y.m;
        ghostEach(src, s.ghost, [&](int i, const Canvas& c) {
            o.seed = s.ghost.seed; auto a = renderStereo(c, Quality::HQ, o);
            for (size_t k = 0; k < a.size() && (size_t) i * step + k < y.m.size(); ++k) { y.m[(size_t) i * step + k] += a.m[k]; y.s[(size_t) i * step + k] += a.s[k]; }
            return !cancelled(); });
        return y;
    }
    if (s.output == OutMorph && s.prep) { Canvas c; morph(*s.prep, s.morphT, c); return renderStereo(c, Quality::HQ, o); }
    return renderStereo(s.canvas, Quality::HQ, o);
}
std::vector<float> LabHost::renderHQ() const { return render(canvas, Quality::HQ); }
juce::File LabHost::sharedSoundsDir() { auto d = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Ghosty").getChildFile("Shared Sounds"); d.createDirectory(); return d; }
juce::File LabHost::uniqueFile(const juce::File& dir, const juce::String& base, const juce::String& ext) {
    auto name = juce::File::createLegalFileName(base).trim(); if (name.isEmpty()) name = "Spectral";
    auto f = dir.getChildFile(name + ext); return f.exists() ? f.getNonexistentSibling(true) : f;
}
juce::StringArray LabHost::sendToBeatsNow(double bpm, juce::String& error) {
    const auto s = snapshot(bpm); juce::StringArray out; const auto d = sharedSoundsDir();
    if (!d.isDirectory()) { error = "Can't create the folder " + d.getFullPathName(); return out; }
    const auto base = s.base + " " + stamp();
    if (s.output == OutGhost) {
        const Canvas src = trimmed(s.canvas, framesFor(kGhostMaxSeconds));
        ghostEach(src, s.ghost, [&](int i, const Canvas& c) {
            RenderOpts o = s.opts; auto x = renderStereo(c, Quality::HQ, o); auto f = uniqueFile(d, base + " ghost " + juce::String(i + 1).paddedLeft('0', 2), ".wav");
            if (writeStereo(f, x, s.gain, s.width)) out.add(f.getFullPathName()); else error = "Can't write " + f.getFileName() + " (disk full or no permission?)";
            progress.store((i + 1) * 100 / std::max(1, s.ghost.count)); return error.isEmpty() && !cancelled(); });
        progress.store(-1); return out;
    }
    auto x = renderOutput(s); auto f = uniqueFile(d, base, ".wav");
    if (writeStereo(f, x, s.gain, s.width)) out.add(f.getFullPathName()); else error = "Can't write " + f.getFileName() + " (disk full or no permission?)";
    return out;
}
void LabHost::sendToBeats(double bpm, std::function<void(juce::StringArray, juce::String)> done) {
    auto self = this; auto alv = alive; progress.store(0);
    // the snapshot is taken now (message thread), the rendering + writing happens on the worker
    auto snap = std::make_shared<Snapshot>(snapshot(bpm));
    post([self, snap, done, alv] {
        juce::StringArray out; juce::String error; const auto d = sharedSoundsDir(); const auto& s = *snap;
        if (!d.isDirectory()) error = "Can't create the folder " + d.getFullPathName();
        else {
            const auto base = s.base + " " + stamp();
            if (s.output == OutGhost) {
                const Canvas src = trimmed(s.canvas, framesFor(kGhostMaxSeconds));
                ghostEach(src, s.ghost, [&](int i, const Canvas& c) {
                    RenderOpts o = s.opts; auto x = renderStereo(c, Quality::HQ, o); auto f = uniqueFile(d, base + " ghost " + juce::String(i + 1).paddedLeft('0', 2), ".wav");
                    if (writeStereo(f, x, s.gain, s.width)) out.add(f.getFullPathName()); else error = "Can't write " + f.getFileName() + " (disk full or no permission?)";
                    self->progress.store((i + 1) * 100 / std::max(1, s.ghost.count)); return error.isEmpty() && !cancelled(); });
            } else {
                auto x = renderOutput(s); auto f = uniqueFile(d, base, ".wav");
                if (writeStereo(f, x, s.gain, s.width)) out.add(f.getFullPathName()); else error = "Can't write " + f.getFileName() + " (disk full or no permission?)";
            }
        }
        self->progress.store(-1);
        if (cancelled()) return;
        juce::MessageManager::callAsync([out, error, done, alv] { if (alv->load() && done) done(out, error); });
    });
}
juce::File LabHost::dragFile(double bpm, juce::String& why) { return dragFileRegion(bpm, 0.0f, 1.0f, why); }
juce::File LabHost::dragFileRegion(double bpm, float a, float b, juce::String& why) {
    const auto key = hqKey(canvasRev.load(), output.load(), morphNow.load(), bpm);
    std::shared_ptr<Stereo> y; { const juce::ScopedLock sl(resultLock); if (hqCacheKey == key) y = hqCache; }
    if (!y || dirty) { tempo.store(bpm); morphTarget.store(morphNow.load()); hqWanted.store(true); if (dirty) requestPreview(); wake.signal(); why = "Rendering the high-quality file... drag again in a moment."; return {}; }
    auto d = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Ghosty").getChildFile("Spectrum").getChildFile("Lab Bounces"); d.createDirectory();
    static const char* what[] = { "Canvas", "Ghost", "Split", "Morph" };
    const bool part = a > 0.0f || b < 1.0f;   // the WaveRegion grip: only the highlighted part (START..END or the LOOP)
    auto f = uniqueFile(d, "Spectral Lab " + juce::String(what[juce::jlimit(0, 3, output.load())]) + (part ? " region " : " ") + stamp(), ".wav");
    float g; { const juce::ScopedLock sl(resultLock); g = rGain; }
    if (part) { const size_t n = y->size(), s0 = (size_t) (juce::jlimit(0.0f, 1.0f, a) * (float) n), s1 = juce::jlimit(s0 + 1, n, (size_t) (juce::jlimit(0.0f, 1.0f, b) * (float) n));
        auto cut = std::make_shared<Stereo>(); cut->m.assign(y->m.begin() + (std::ptrdiff_t) s0, y->m.begin() + (std::ptrdiff_t) s1);
        if (y->s.size() >= s1) cut->s.assign(y->s.begin() + (std::ptrdiff_t) s0, y->s.begin() + (std::ptrdiff_t) s1); y = cut; }
    if (!writeStereo(f, *y, g, width.load())) { why = "Can't write " + f.getFullPathName(); return {}; }
    return f;
}
// notes for MIDI drag-out: the snapped (exact-pitch) lines of the paint plane, one note per run of frames
static std::vector<LabHost::ExportSound::Note> paintedNotes(const Canvas& c) {
    std::vector<LabHost::ExportSound::Note> out; float mx = 1.0e-6f; for (float a : c.amp) mx = std::max(mx, a);
    for (int r = 0; r < R && out.size() < 2000; ++r) {
        int start = -1; float pitch = 0, peak = 0;
        auto flush = [&](int t) { if (start >= 0 && t - start >= 2) { LabHost::ExportSound::Note n; n.pitch = juce::jlimit(0, 127, (int) std::lround(pitch)); n.velocity = juce::jlimit(0.1f, 1.0f, peak / mx);
                                      n.start = std::max(0.0, frameCentre(start)) / SR; n.length = (t - start) * HOP / SR; out.push_back(n); } start = -1; peak = 0; };
        for (int t = 0; t < c.T; ++t) {
            const size_t i = (size_t) t * R + (size_t) r; const bool on = c.amp[i] > mx * 0.05f && c.fq[i] > 0 && c.noise[i] < 0.5f;
            const float m = on ? hzMidi(c.fq[i]) : 0.0f;
            if (on && (start < 0 || std::abs(m - pitch) > 0.5f)) { flush(t); start = t; pitch = m; }
            if (on) peak = std::max(peak, c.amp[i]); else flush(t);
        }
        flush(c.T);
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
    return out;
}
void LabHost::renderExport(double bpm, std::function<void(std::shared_ptr<ExportSound>)> done) {
    auto e = std::make_shared<ExportSound>(); auto snap = std::make_shared<Snapshot>(snapshot(bpm)); auto alv = alive;
    static const char* what[] = { "Canvas", "Ghost", "Split", "Morph" };
    e->output = snap->output; e->gain = snap->gain; e->width = snap->width; e->bpm = (snap->output == OutSplit || snap->output == OutGhost) ? bpm : 0.0;
    e->name = (sourceName.isNotEmpty() ? sourceName : juce::String("Lab")) + " " + what[juce::jlimit(0, 3, snap->output)];
    if (snap->output == OutCanvas) e->notes = paintedNotes(canvas);
    { juce::ValueTree t("LABSOUND"); t.setProperty("bpm", bpm, nullptr); t.setProperty("output", snap->output, nullptr); t.setProperty("morph", snap->morphT, nullptr);
      t.setProperty("gain", snap->gain, nullptr); t.setProperty("width", snap->width, nullptr); t.setProperty("name", e->name, nullptr); t.appendChild(toTree(), nullptr);
      juce::MemoryOutputStream os(e->state, false); t.writeToStream(os); }
    post([e, snap, done, alv] {
        e->audio = renderOutput(*snap);
        if (cancelled()) return;
        juce::MessageManager::callAsync([e, done, alv] { if (alv->load() && done) done(e); });
    });
}
std::shared_ptr<LabHost::ExportSound> LabHost::renderExportFromState(const juce::MemoryBlock& state) {
    auto t = juce::ValueTree::readFromData(state.getData(), state.getSize());
    if (!t.hasType("LABSOUND") || !t.getChildWithName("SPECTRALLAB").isValid()) return nullptr;
    LabHost tmp; tmp.applyTree(t.getChildWithName("SPECTRALLAB"));
    const double bpm = juce::jlimit(20.0, 400.0, (double) t.getProperty("bpm", 120.0));
    tmp.output = juce::jlimit(0, 3, (int) t.getProperty("output", 0)); tmp.morphNow = juce::jlimit(0.0f, 1.0f, (float) (double) t.getProperty("morph", 0.0));
    auto snap = tmp.snapshot(bpm);
    if (snap.output == OutMorph && !tmp.audioA.empty() && !tmp.audioB.empty()) snap.prep = morphPrepare(tmp.audioA, tmp.audioB);
    auto e = std::make_shared<ExportSound>(); e->output = snap.output; e->bpm = (snap.output == OutSplit || snap.output == OutGhost) ? bpm : 0.0;
    e->gain = juce::jlimit(0.05f, 8.0f, (float) (double) t.getProperty("gain", 1.0)); e->width = juce::jlimit(0.0f, 1.0f, (float) (double) t.getProperty("width", 0.35));
    e->name = t.getProperty("name").toString(); e->state = state;
    if (snap.output == OutCanvas) e->notes = paintedNotes(tmp.canvas);
    e->audio = renderOutput(snap);
    return e;
}
juce::File LabHost::exportImage(int w, int h) const {
    auto d = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("Ghosty").getChildFile("Spectrum").getChildFile("Lab Images"); d.createDirectory();
    auto f = uniqueFile(d, "Spectral Lab " + stamp(), ".png");
    juce::PNGImageFormat png; juce::FileOutputStream os(f); if (!os.openedOk() || !png.writeImageToStream(canvasImage(canvas, w, h), os)) return {};
    return f;
}
int LabHost::detectRoot(const std::vector<float>& x) const {
    if (x.size() < 8192) return 60;
    size_t best = 0; double be = 0; for (size_t s = 0; s + 4096 < x.size(); s += 1024) { double e = 0; for (size_t i = 0; i < 4096; ++i) e += (double) x[s + i] * x[s + i]; if (e > be) { be = e; best = s; } }
    const int W = 2048, tmin = (int) (SR / 1200), tmax = (int) (SR / 45); std::vector<double> d((size_t) tmax + 1, 0.0);
    if (best + (size_t) W + (size_t) tmax >= x.size()) best = x.size() > (size_t) (W + tmax + 1) ? x.size() - (size_t) (W + tmax + 1) : 0;
    if (x.size() < (size_t) (W + tmax + 1)) return 60;
    for (int tau = 1; tau <= tmax; ++tau) { double s = 0; for (int i = 0; i < W; ++i) { const double v = x[best + (size_t) i] - x[best + (size_t) i + (size_t) tau]; s += v * v; } d[(size_t) tau] = s; }
    double run = 0; int pick = -1; double minV = 1e9; int minT = tmin;
    for (int tau = 1; tau <= tmax; ++tau) { run += d[(size_t) tau]; const double c = run > 0 ? d[(size_t) tau] * tau / run : 1.0; if (tau < tmin) continue; if (c < minV) { minV = c; minT = tau; } if (c < 0.15 && pick < 0) { pick = tau; } if (pick > 0 && tau > pick && c > minV) break; }
    if (pick < 0) { if (minV > 0.4) return 60; pick = minT; }
    return juce::jlimit(24, 96, (int) std::lround(hzMidi((float) (SR / pick))));
}

// ================================================================ state
void LabHost::sanitise(GhostParams& g) {
    g.count = juce::jlimit(2, kMaxGhosts, g.count); auto c01 = [](float v, float d) { return std::isfinite(v) ? juce::jlimit(0.0f, 1.0f, v) : d; };
    g.evolve = c01(g.evolve, 0.6f); g.memory = c01(g.memory, 0.5f); g.decay = c01(g.decay, 0.3f); g.seed = juce::jlimit<uint32_t>(1, 999999, g.seed);
}
void LabHost::sanitise(ChaosParams& c) {
    auto c01 = [](float v, float d) { return std::isfinite(v) ? juce::jlimit(0.0f, 1.0f, v) : d; };
    c.chaos = c01(c.chaos, 0.4f); c.density = c01(c.density, 0.5f); c.harmony = c01(c.harmony, 0.6f); c.motion = c01(c.motion, 0.3f); c.constraint = c01(c.constraint, 0.0f);
    c.seed = juce::jlimit<uint32_t>(1, 999999, c.seed); c.root = juce::jlimit(0, 11, c.root); c.scale = juce::jlimit(0, 6, c.scale); c.bpm = std::isfinite(c.bpm) ? juce::jlimit(20.0, 400.0, c.bpm) : 120.0;
}
static juce::String splitString(const SplitSetup& split) {
    juce::StringArray sp; sp.add(juce::String(split.bands)); for (float x : split.xover) sp.add(juce::String(x));
    for (auto& b : split.seq) { juce::String st; for (auto v : b.steps) st << (v ? "1" : "0");
        sp.add(juce::String(b.rateDiv) + ":" + juce::String(b.gate) + ":" + juce::String(b.level) + ":" + (b.retrig ? "1" : "0") + ":" + (b.mute ? "1" : "0") + ":" + st
               + ":" + juce::String(b.length) + ":" + hexBytes(b.prob.data(), kSteps, 0) + ":" + hexBytes(reinterpret_cast<const uint8_t*>(b.pitch.data()), kSteps, 12)); }
    return sp.joinIntoString(",");
}
static void parseSplit(const juce::String& str, const juce::String& extra, SplitSetup& split) {
    juce::StringArray a; a.addTokens(str, ",", "");
    if (a.size() >= 1 + kMaxBands - 1) {
        split.bands = a[0].getIntValue(); for (int i = 0; i < kMaxBands - 1; ++i) split.xover[(size_t) i] = a[1 + i].getFloatValue();
        for (int b = 0; b < kMaxBands && kMaxBands + b < a.size(); ++b) {
            juce::StringArray f; f.addTokens(a[kMaxBands + b], ":", ""); if (f.size() < 6) continue; auto& s = split.seq[(size_t) b];
            s.rateDiv = f[0].getIntValue(); s.gate = f[1].getFloatValue(); s.level = f[2].getFloatValue(); s.retrig = f[3] == "1"; s.mute = f[4] == "1";
            for (int k = 0; k < kSteps && k < f[5].length(); ++k) s.steps[(size_t) k] = f[5][k] == '1';
            if (f.size() >= 9) {   // 2.2 additions: length, chance per step, pitch per step
                s.length = f[6].getIntValue();
                for (int k = 0; k < kSteps && 2 * k + 1 < f[7].length(); ++k) s.prob[(size_t) k] = (uint8_t) juce::jlimit(0, 100, f[7].substring(2 * k, 2 * k + 2).getHexValue32());
                for (int k = 0; k < kSteps && 2 * k + 1 < f[8].length(); ++k) s.pitch[(size_t) k] = (int8_t) juce::jlimit(-12, 12, f[8].substring(2 * k, 2 * k + 2).getHexValue32() - 12);
            }
        }
    }
    juce::StringArray x; x.addTokens(extra, ",", ""); if (x.size() >= 2) { split.swing = x[0].getFloatValue(); split.seed = (uint32_t) juce::jlimit<juce::int64>(1, 999999, x[1].getLargeIntValue()); }
    split.sanitise();
}
juce::MemoryBlock LabHost::sideState() const {
    juce::ValueTree t("SIDE"); t.setProperty("split", splitString(split), nullptr); t.setProperty("splitx", juce::String(split.swing) + "," + juce::String((juce::int64) split.seed), nullptr);
    juce::StringArray lk; for (auto& s : locks) lk.add(juce::String(s.t0) + ":" + juce::String(s.t1) + ":" + juce::String(s.r0) + ":" + juce::String(s.r1)); t.setProperty("locks", lk.joinIntoString(","), nullptr);
    juce::StringArray lane; for (auto& v : morphLane) lane.add(juce::String(v.load(), 3)); t.setProperty("lane", lane.joinIntoString(","), nullptr);
    juce::MemoryBlock m; juce::MemoryOutputStream os(m, false); t.writeToStream(os); return m;
}
static void parseLocks(const juce::String& s, std::vector<Sel>& locks, int T) {
    locks.clear(); juce::StringArray a; a.addTokens(s, ",", "");
    for (auto& x : a) { juce::StringArray f; f.addTokens(x, ":", ""); if (f.size() != 4) continue; Sel l; l.t0 = juce::jlimit(0, std::max(0, T), f[0].getIntValue()); l.t1 = juce::jlimit(0, std::max(0, T), f[1].getIntValue());
        l.r0 = juce::jlimit(0.0f, (float) R - 1, f[2].getFloatValue()); l.r1 = juce::jlimit(0.0f, (float) R - 1, f[3].getFloatValue()); if (l.valid() && locks.size() < 256) locks.push_back(l); }
}
void LabHost::setSideState(const juce::MemoryBlock& m) {
    auto t = juce::ValueTree::readFromData(m.getData(), m.getSize()); if (!t.isValid()) return;
    parseSplit(t.getProperty("split").toString(), t.getProperty("splitx").toString(), split);
    parseLocks(t.getProperty("locks").toString(), locks, canvas.T);
    juce::StringArray a; a.addTokens(t.getProperty("lane").toString(), ",", ""); for (int i = 0; i < 8 && i < a.size(); ++i) morphLane[(size_t) i].store(juce::jlimit(0.0f, 1.0f, a[i].getFloatValue()));
    publishSplit(); if (output.load() == OutSplit) requestSplit(); requestLane(); sideRev.fetch_add(1);
}
juce::ValueTree LabHost::toTree() const {
    { const juce::ScopedLock sl(stateLock); if (pendingTree) return pendingTree->createCopy(); }
    juce::ValueTree t("SPECTRALLAB");
    t.setProperty("v", 2, nullptr); t.setProperty("open", open.load(), nullptr); t.setProperty("keys", playKeys.load(), nullptr); t.setProperty("oneShot", oneShot.load(), nullptr);
    t.setProperty("output", output.load(), nullptr); t.setProperty("root", rootNote.load(), nullptr); t.setProperty("level", level.load(), nullptr); t.setProperty("width", width.load(), nullptr);
    t.setProperty("laneOn", laneOn.load(), nullptr); t.setProperty("velMorph", velMorph.load(), nullptr); t.setProperty("keepTransients", keepTransients.load(), nullptr); t.setProperty("autoLevel", autoLevel.load(), nullptr);
    t.setProperty("keysPitch", keysPitch.load(), nullptr); t.setProperty("stereoIn", stereoIn.load(), nullptr);
    juce::StringArray lane; for (auto& v : morphLane) lane.add(juce::String(v.load(), 3)); t.setProperty("lane", lane.joinIntoString(","), nullptr);
    t.setProperty("brush", juce::String(brush.size) + "," + juce::String(brush.hardness) + "," + juce::String(brush.level) + "," + juce::String(brush.noise), nullptr);
    t.setProperty("image", juce::String(imageMap.seconds) + "," + juce::String(imageMap.loHz) + "," + juce::String(imageMap.hiHz) + "," + juce::String(imageMap.gamma), nullptr);
    t.setProperty("ghost", juce::String(ghost.count) + "," + juce::String(ghost.evolve) + "," + juce::String(ghost.memory) + "," + juce::String(ghost.decay) + "," + juce::String((juce::int64) ghost.seed), nullptr);
    t.setProperty("chaos", juce::String(chaos.chaos) + "," + juce::String(chaos.density) + "," + juce::String(chaos.harmony) + "," + juce::String(chaos.motion) + "," + juce::String(chaos.constraint) + "," + juce::String((juce::int64) chaos.seed), nullptr);
    t.setProperty("split", splitString(split), nullptr); t.setProperty("splitx", juce::String(split.swing) + "," + juce::String((juce::int64) split.seed), nullptr);
    juce::StringArray lk; for (auto& s : locks) lk.add(juce::String(s.t0) + ":" + juce::String(s.t1) + ":" + juce::String(s.r0) + ":" + juce::String(s.r1)); t.setProperty("locks", lk.joinIntoString(","), nullptr);
    t.setProperty("sourceName", sourceName, nullptr); t.setProperty("nameA", nameA, nullptr); t.setProperty("nameB", nameB, nullptr);
    if (playStart.load() > 0.0f || playEnd.load() < 1.0f) t.setProperty("play", juce::String(playStart.load(), 5) + "," + juce::String(playEnd.load(), 5), nullptr);   // START / END (only when moved)
    // the big blocks are cached: the worker encodes the canvas after every preview; the audio is encoded once per import
    const bool msg = juce::MessageManager::existsAndIsCurrentThread();
    const juce::ScopedLock sl(stateLock);
    if (msg && canvasBlobRev != canvasRev.load()) { canvasBlob = b64(save(canvas, !sourceAudio.empty() && canvas.srcLen == (int) sourceAudio.size())); canvasBlobRev = canvasRev.load(); }
    if (msg && sourceBlobRev != sourceRev) { sourceBlob = sourceAudio.empty() ? juce::String() : b64(packAudio(sourceAudio)); sourceBlobRev = sourceRev; }
    if (msg && aBlobRev != aRev) { aBlob = audioA.empty() ? juce::String() : b64(packAudio(audioA)); aBlobRev = aRev; }
    if (msg && bBlobRev != bRev) { bBlob = audioB.empty() ? juce::String() : b64(packAudio(audioB)); bBlobRev = bRev; }
    t.setProperty("canvas", canvasBlob, nullptr);
    if (sourceBlob.isNotEmpty()) t.setProperty("source", sourceBlob, nullptr);
    if (aBlob.isNotEmpty()) t.setProperty("audioA", aBlob, nullptr);
    if (bBlob.isNotEmpty()) t.setProperty("audioB", bBlob, nullptr);
    if (sourceMade) t.setProperty("sourceMade", true, nullptr);
    if (aMade) t.setProperty("aMade", true, nullptr);
    if (bMade) t.setProperty("bMade", true, nullptr);
    return t;
}
juce::ValueTree LabHost::toTree(bool keepRendered, int* stripped) const {
    auto t = toTree();
    if (keepRendered) return t;
    t = t.createCopy();   // (the cached / pending tree stays whole: only this save loses the plugin-made audio)
    const char* pairs[][2] { { "source", "sourceMade" }, { "audioA", "aMade" }, { "audioB", "bMade" } };
    for (auto& pr : pairs) if ((bool) t.getProperty(pr[1], false) && t.hasProperty(pr[0])) { t.removeProperty(pr[0], nullptr); t.setProperty(juce::String(pr[0]) + "Stripped", true, nullptr); if (stripped) ++*stripped; }
    return t;
}
void LabHost::fromTree(const juce::ValueTree& t) {
    if (!t.hasType("SPECTRALLAB")) return;
    if (juce::MessageManager::existsAndIsCurrentThread()) { applyTree(t); return; }
    // another thread: keep it (getState returns it meanwhile) and apply it on the message thread, so the UI never sees a half-swapped canvas
    { const juce::ScopedLock sl(stateLock); pendingTree = std::make_unique<juce::ValueTree>(t.createCopy()); }
    auto alv = alive;
    juce::MessageManager::callAsync([this, alv] {
        if (!alv->load()) return;
        std::unique_ptr<juce::ValueTree> p; { const juce::ScopedLock sl(stateLock); p = std::move(pendingTree); }
        if (p) applyTree(*p);
    });
}
void LabHost::applyTree(const juce::ValueTree& t) {
    auto c01 = [](double v, double d) { return std::isfinite(v) ? juce::jlimit(0.0, 1.0, v) : d; };
    open = (bool) t.getProperty("open", false); playKeys = (bool) t.getProperty("keys", true); oneShot = (bool) t.getProperty("oneShot", false);
    output = juce::jlimit(0, 3, (int) t.getProperty("output", 0)); rootNote = juce::jlimit(24, 96, (int) t.getProperty("root", 60));
    level = (float) c01((double) t.getProperty("level", 0.8), 0.8); width = (float) c01((double) t.getProperty("width", 0.0), 0.0);   // sessions from before stereo stay mono
    laneOn = (bool) t.getProperty("laneOn", false); velMorph = (bool) t.getProperty("velMorph", false);
    { juce::StringArray pl; pl.addTokens(t.getProperty("play").toString(), ",", ""); const double a = pl.size() == 2 ? c01(pl[0].getDoubleValue(), 0.0) : 0.0, b2 = pl.size() == 2 ? c01(pl[1].getDoubleValue(), 1.0) : 1.0;
      playStart = (float) (b2 > a + 0.001 ? a : 0.0); playEnd = (float) (b2 > a + 0.001 ? b2 : 1.0); }   // older sessions: the whole loop
    keepTransients = (bool) t.getProperty("keepTransients", true); autoLevel = (bool) t.getProperty("autoLevel", true);
    keysPitch = juce::jlimit(0, 1, (int) t.getProperty("keysPitch", (int) PitchSpectral)); stereoIn = juce::jlimit(0, 3, (int) t.getProperty("stereoIn", 0));
    auto nums = [](const juce::var& v) { juce::StringArray a; a.addTokens(v.toString(), ",", ""); return a; };
    auto fl = [](const juce::String& s, float lo, float hi, float d) { const float v = s.getFloatValue(); return std::isfinite(v) ? juce::jlimit(lo, hi, v) : d; };
    { auto a = nums(t.getProperty("lane")); for (int i = 0; i < 8 && i < a.size(); ++i) morphLane[(size_t) i].store(fl(a[i], 0, 1, 0)); }
    { auto a = nums(t.getProperty("brush")); if (a.size() >= 4) { brush.size = fl(a[0], 0.5f, 24, 4); brush.hardness = fl(a[1], 0, 0.95f, 0.6f); brush.level = fl(a[2], 0.01f, 1, 0.5f); brush.noise = fl(a[3], 0, 1, 0); } }
    { auto a = nums(t.getProperty("image")); if (a.size() >= 4) { imageMap.seconds = a[0].getDoubleValue(); imageMap.loHz = a[1].getDoubleValue(); imageMap.hiHz = a[2].getDoubleValue(); imageMap.gamma = a[3].getFloatValue(); } imageMap.sanitise(); }
    { auto a = nums(t.getProperty("ghost")); if (a.size() >= 5) { ghost.count = a[0].getIntValue(); ghost.evolve = a[1].getFloatValue(); ghost.memory = a[2].getFloatValue(); ghost.decay = a[3].getFloatValue(); ghost.seed = (uint32_t) juce::jlimit<juce::int64>(1, 999999, a[4].getLargeIntValue()); } sanitise(ghost); }
    { auto a = nums(t.getProperty("chaos")); if (a.size() >= 6) { chaos.chaos = a[0].getFloatValue(); chaos.density = a[1].getFloatValue(); chaos.harmony = a[2].getFloatValue(); chaos.motion = a[3].getFloatValue(); chaos.constraint = a[4].getFloatValue(); chaos.seed = (uint32_t) juce::jlimit<juce::int64>(1, 999999, a[5].getLargeIntValue()); } sanitise(chaos); }
    split = SplitSetup(); parseSplit(t.getProperty("split").toString(), t.getProperty("splitx").toString(), split);
    sourceName = t.getProperty("sourceName").toString(); nameA = t.getProperty("nameA").toString(); nameB = t.getProperty("nameB").toString();
    sourceAudio = t.hasProperty("source") ? unpackAudio(unb64(t.getProperty("source").toString())) : std::vector<float>();
    sourceMade = (bool) t.getProperty("sourceMade", false); aMade = (bool) t.getProperty("aMade", false); bMade = (bool) t.getProperty("bMade", false);   // older sessions: imported
    audioA = t.hasProperty("audioA") ? unpackAudio(unb64(t.getProperty("audioA").toString())) : std::vector<float>();
    audioB = t.hasProperty("audioB") ? unpackAudio(unb64(t.getProperty("audioB").toString())) : std::vector<float>();
    Canvas c; bool ok = t.hasProperty("canvas") && load(unb64(t.getProperty("canvas").toString()), c, &sourceAudio);
    if (!ok && !sourceAudio.empty()) { analyse(sourceAudio, c); ok = true; }
    if (ok) canvas = std::move(c);
    parseLocks(t.getProperty("locks").toString(), locks, canvas.T);
    image = {}; ++sourceRev; ++aRev; ++bRev;
    undo.clear(); edited(); publishSplit(); requestPreview(); requestMorph(); sideRev.fetch_add(1);
    if (output.load() == OutSplit) requestSplit();
}
} // namespace lab
