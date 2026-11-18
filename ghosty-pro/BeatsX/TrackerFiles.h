#pragma once
// XM (FastTracker 2) and IT (Impulse Tracker) import / export for the MELODIC TRACKER. Clean-room, written from the public format
// documents ("The XM file format" by Mr.H of Triton, as distributed with FT2 and on modland; ITTECH.TXT by Jeffrey Lim, shipped with
// Impulse Tracker). No library code is used or linked: libxmp is LGPL (not linked by policy) and libopenmpt (BSD) only reads; the
// tracker model here is small, so a reader / writer of our own is the smaller, fully licence-clean dependency.
//
// What travels:  patterns (8 channels, up to 128 rows each, up to 16 patterns, the order list), note / instrument / volume / effect,
//                instrument names, and each instrument's sound as a sample (16-bit mono, its START..END, its sustain LOOP).
// Effects:  ours <-> XM: 0 1 2 3 4 7 8 9 A B (Cxx -> ECx, Dxx -> EDx, E1x E2x EAx EBx E6x, Fxx incl. F20+ tempo, Qxy -> Rxy, Pxy -> Pxy);
//           ours <-> IT: J F E G H R X O D B (Cxx -> SCx, Dxx -> SDx, E1x -> FFx, E2x -> EFx, EAx -> DxF, EBx -> DFx, E6x -> SBx, Fxx -> Axx /
//           Txx, Qxy -> Qxy, Pxy -> Pxy, R01/R00 -> S9F/S9E, Zxx cutoff -> Zxx (00..7F), Yxx resonance -> Z8x).  Anything else is dropped
//           on export (counted in the report) and ignored on import.
// Pitch:  a sound's ROOT plays it at its own rate; XM sample tuning = relative note + finetune, IT = C5 speed. On import every sample gets
//         ROOT C-4 and the rate the module asks for, so notes sound where the module wants them.
// Robustness: every read is bounds-checked; sizes and counts are clamped; a truncated or corrupt file gives an error or a partial import,
//         never a crash (tests fuzz 200 mutated files of each format).
#include "MelodyTracker.h"
#include <functional>

namespace gb::mel::files {

using SoundOf = std::function<SoundPtr(const Instrument&)>;   // the sound an instrument plays (library sound or its own sample)
struct Report { juce::String error; int patterns = 0, instruments = 0, samples = 0, droppedFx = 0, droppedChannels = 0, truncatedRows = 0; int speed = 6, bpm = 125; };

// ---------------------------------------------------------------- byte helpers
struct Reader {
    const uint8_t* d; size_t n, p = 0; bool ok = true;
    Reader(const void* data, size_t size) : d(static_cast<const uint8_t*>(data)), n(size) {}
    bool has(size_t k) const { return ok && p <= n && n - p >= k; }
    uint8_t u8() { if (!has(1)) { ok = false; return 0; } return d[p++]; }
    uint16_t u16() { const uint16_t a = u8(); return (uint16_t) (a | (u8() << 8)); }
    uint32_t u32() { const uint32_t a = u16(); return a | ((uint32_t) u16() << 16); }
    juce::String str(size_t k) { juce::String s; for (size_t i = 0; i < k; ++i) { const char c = (char) u8(); if (c >= 32 && c < 127) s += juce::String::charToString((juce::juce_wchar) (uint8_t) c); else if (c == 0) { skip(k - i - 1); break; } } return s.trimEnd(); }
    void skip(size_t k) { if (!has(k)) { ok = false; p = n; return; } p += k; }
    void seek(size_t at) { if (at > n) { ok = false; p = n; return; } p = at; }
};
struct Writer {
    juce::MemoryOutputStream o;
    void u8(int v) { o.writeByte((char) (uint8_t) v); }
    void u16(int v) { o.writeShort((short) (uint16_t) v); }
    void u32(uint32_t v) { o.writeInt((int) v); }
    void str(const juce::String& s, size_t k) { const auto u = s.toRawUTF8(); size_t i = 0; for (; i < k && u[i]; ++i) u8((uint8_t) u[i] < 127 ? u[i] : '?'); for (; i < k; ++i) u8(0); }
    void zeros(size_t k) { for (size_t i = 0; i < k; ++i) u8(0); }
    size_t pos() { return (size_t) o.getPosition(); }
    void patch32(size_t at, uint32_t v) { auto* b = static_cast<uint8_t*>(const_cast<void*>(o.getData())); b[at] = (uint8_t) v; b[at + 1] = (uint8_t) (v >> 8); b[at + 2] = (uint8_t) (v >> 16); b[at + 3] = (uint8_t) (v >> 24); }
    void patch16(size_t at, int v) { auto* b = static_cast<uint8_t*>(const_cast<void*>(o.getData())); b[at] = (uint8_t) v; b[at + 1] = (uint8_t) (v >> 8); }
};

// a sound's START..END as 16-bit mono (capped at 10 s), plus where its loop sits in that
struct SampleOut { std::vector<int16_t> pcm; double rate = 44100; int loopA = 0, loopB = 0; bool loop = false; };
inline SampleOut sampleOf(const Instrument& in, const SoundPtr& s) {
    SampleOut o; if (!s || s->data.empty()) return o;
    const int n = (int) s->data.size(); int a = juce::jlimit(0, n - 1, (int) (in.start * n)), z = juce::jlimit(a + 1, n, (int) std::ceil(in.end * n));
    if (in.slice > 0 && in.slice <= (int) in.slices.size()) { a = juce::jlimit(0, n - 1, in.slices[(size_t) in.slice - 1]); z = in.slice < (int) in.slices.size() ? juce::jlimit(a + 1, n, in.slices[(size_t) in.slice]) : n; }
    z = juce::jmin(z, a + (int) (s->rate * 10.0)); o.rate = s->rate; o.pcm.resize((size_t) (z - a));
    for (int i = a; i < z; ++i) { float v = s->data[(size_t) i]; if (s->stereo()) v = 0.5f * (v + s->right[(size_t) i]); o.pcm[(size_t) (i - a)] = (int16_t) juce::jlimit(-32767, 32767, (int) std::lround(v * 32767.0f)); }
    if (in.loop) { o.loopA = juce::jlimit(0, (int) o.pcm.size() - 1, (int) (in.loopStart * n) - a); o.loopB = juce::jlimit(o.loopA + 1, (int) o.pcm.size(), (int) (in.loopEnd * n) - a); o.loop = o.loopB - o.loopA > 2; }
    return o;
}
inline SoundPtr soundFromPcm(const std::vector<float>& x, double rate, const juce::String& name) {
    if (x.size() < 2) return nullptr;
    auto s = std::make_shared<Sound>(); s->name = name.isNotEmpty() ? name : juce::String("tracker sample"); s->category = "Sample"; s->rate = juce::jlimit(1000.0, 384000.0, rate); s->data = x; return s;
}
// the sample a module wants played at our ROOT C-4 (MIDI 60): rate in Hz
inline double xmRate(int relNote, int fine) { return 8363.0 * std::pow(2.0, (relNote + fine / 128.0) / 12.0); }
inline void xmTune(double rate, int root, int& relNote, int& fine) {   // play `rate` at MIDI `root` -> relative note + finetune at C-4
    const double st = 12.0 * std::log2(juce::jmax(1.0, rate) / 8363.0) - (root - 60); relNote = (int) std::floor(st); fine = juce::jlimit(-128, 127, (int) std::lround((st - relNote) * 128.0));
    if (fine == 128) { ++relNote; fine = 0; } relNote = juce::jlimit(-96, 95, relNote);
}

// ---------------------------------------------------------------- effect mapping
inline bool fxToXm(const Cell& c, int& t, int& p, int sampleLen) {
    const int x = c.param >> 4, y = c.param & 15; p = c.param;
    switch (c.fx) {
        case FxNone: t = 0; p = 0; return true;
        case FxArp: t = 0; return true; case FxSlideUp: t = 1; return true; case FxSlideDown: t = 2; return true; case FxGlide: t = 3; return true;
        case FxVibrato: t = 4; return true; case FxTremolo: t = 7; return true; case FxPan: t = 8; return true;
        case FxOffset: t = 9; p = juce::jlimit(0, 255, (int) std::lround(c.param / 256.0 * sampleLen / 256.0)); return true;
        case FxVolSlide: t = 10; return true; case FxJump: t = 11; return true;
        case FxCut: t = 14; p = 0xC0 | juce::jmin(15, (int) c.param); return true; case FxDelay: t = 14; p = 0xD0 | juce::jmin(15, (int) c.param); return true;
        case FxFine: if (x == 1 || x == 2 || x == 6 || x == 0xA || x == 0xB) { t = 14; p = (x << 4) | y; return true; } return false;
        case FxSpeed: t = 15; return true; case FxRetrig: t = 27; return true; case FxPanSlide: t = 25; return true;
        default: return false;
    }
}
inline bool fxFromXm(int t, int p, Cell& c, int sampleLen) {
    const int x = p >> 4, y = p & 15; c.param = (uint8_t) p;
    switch (t) {
        case 0: if (p == 0) { c.fx = FxNone; return true; } c.fx = FxArp; return true;
        case 1: c.fx = FxSlideUp; return true; case 2: c.fx = FxSlideDown; return true; case 3: c.fx = FxGlide; return true; case 4: c.fx = FxVibrato; return true;
        case 7: c.fx = FxTremolo; return true; case 8: c.fx = FxPan; return true;
        case 9: c.fx = FxOffset; c.param = (uint8_t) juce::jlimit(0, 255, sampleLen > 0 ? (int) std::lround(p * 256.0 * 256.0 / sampleLen) : p); return true;
        case 10: c.fx = FxVolSlide; return true; case 11: c.fx = FxJump; return true; case 15: if (p == 0) return false; c.fx = FxSpeed; return true;
        case 14: if (x == 0xC) { c.fx = FxCut; c.param = (uint8_t) y; return true; } if (x == 0xD) { c.fx = FxDelay; c.param = (uint8_t) y; return true; }
                 if (x == 1 || x == 2 || x == 6 || x == 0xA || x == 0xB) { c.fx = FxFine; return true; } return false;
        case 25: c.fx = FxPanSlide; return true; case 27: c.fx = FxRetrig; return true;
        default: return false;
    }
}
// IT commands: 1 = A ... 26 = Z
inline bool fxToIt(const Cell& c, int& t, int& p, int sampleLen) {
    const int x = c.param >> 4, y = c.param & 15; p = c.param;
    switch (c.fx) {
        case FxNone: t = 0; p = 0; return true;
        case FxArp: t = 10; return true; case FxSlideUp: t = 6; return true; case FxSlideDown: t = 5; return true; case FxGlide: t = 7; return true;
        case FxVibrato: t = 8; return true; case FxTremolo: t = 18; return true; case FxPan: t = 24; return true;
        case FxOffset: t = 15; p = juce::jlimit(0, 255, (int) std::lround(c.param / 256.0 * sampleLen / 256.0)); return true;
        case FxVolSlide: t = 4; if (x && y) p = x << 4; return true; case FxJump: t = 2; return true;
        case FxCut: t = 19; p = 0xC0 | juce::jmin(15, (int) c.param); return true; case FxDelay: t = 19; p = 0xD0 | juce::jmin(15, (int) c.param); return true;
        case FxFine: if (x == 1) { t = 6; p = 0xF0 | y; return true; } if (x == 2) { t = 5; p = 0xF0 | y; return true; } if (x == 0xA) { t = 4; p = (y << 4) | 0xF; return true; }
                     if (x == 0xB) { t = 4; p = 0xF0 | y; return true; } if (x == 6) { t = 19; p = 0xB0 | y; return true; } return false;
        case FxSpeed: if (c.param >= 0x20) { t = 20; return true; } t = 1; return true;
        case FxRetrig: t = 17; return true; case FxPanSlide: t = 16; return true;
        case FxReverse: t = 19; p = c.param ? 0x9F : 0x9E; return true;
        case FxCutoff: t = 26; p = c.param >> 1; return true; case FxReso: t = 26; p = 0x80 | (c.param >> 4); return true;
        default: return false;
    }
}
inline bool fxFromIt(int t, int p, Cell& c, int sampleLen) {
    const int x = p >> 4, y = p & 15; c.param = (uint8_t) p;
    switch (t) {
        case 0: c.fx = FxNone; return true;
        case 1: if (p == 0) return false; c.fx = FxSpeed; c.param = (uint8_t) juce::jlimit(1, 31, p); return true;
        case 20: if (p < 0x20) return false; c.fx = FxSpeed; return true;
        case 2: c.fx = FxJump; return true;
        case 4: if (y == 0xF && x > 0) { c.fx = FxFine; c.param = (uint8_t) (0xA0 | x); return true; } if (x == 0xF && y > 0) { c.fx = FxFine; c.param = (uint8_t) (0xB0 | y); return true; } c.fx = FxVolSlide; return true;
        case 5: if (x == 0xF) { c.fx = FxFine; c.param = (uint8_t) (0x20 | y); return true; } if (x == 0xE) return false; c.fx = FxSlideDown; return true;
        case 6: if (x == 0xF) { c.fx = FxFine; c.param = (uint8_t) (0x10 | y); return true; } if (x == 0xE) return false; c.fx = FxSlideUp; return true;
        case 7: c.fx = FxGlide; return true; case 8: c.fx = FxVibrato; return true; case 10: c.fx = FxArp; return true;
        case 15: c.fx = FxOffset; c.param = (uint8_t) juce::jlimit(0, 255, sampleLen > 0 ? (int) std::lround(p * 256.0 * 256.0 / sampleLen) : p); return true;
        case 16: c.fx = FxPanSlide; return true; case 17: c.fx = FxRetrig; return true; case 18: c.fx = FxTremolo; return true;
        case 19: if (x == 0xC) { c.fx = FxCut; c.param = (uint8_t) y; return true; } if (x == 0xD) { c.fx = FxDelay; c.param = (uint8_t) y; return true; }
                 if (x == 0xB) { c.fx = FxFine; c.param = (uint8_t) (0x60 | y); return true; } if (p == 0x9F || p == 0x9E) { c.fx = FxReverse; c.param = (uint8_t) (p == 0x9F ? 1 : 0); return true; } return false;
        case 24: c.fx = FxPan; return true;
        case 26: if (p < 0x80) { c.fx = FxCutoff; c.param = (uint8_t) juce::jmin(255, p * 2); return true; } if (p < 0x90) { c.fx = FxReso; c.param = (uint8_t) ((p & 15) << 4); return true; } return false;
        default: return false;
    }
}

// which patterns a module carries: OWN ORDER's sequence, else every pattern up to the last one with notes
inline std::vector<int> orderOf(const Song& s) {
    std::vector<int> ord;
    if (s.ownOrder) { for (auto o : s.order) ord.push_back(juce::jlimit(0, kPats - 1, (int) o)); }
    else { int last = 0; for (int p = 0; p < kPats; ++p) if (s.usedChannels(p) > 0) last = p; for (int p = 0; p <= last; ++p) ord.push_back(p); }
    if (ord.empty()) ord.push_back(0);
    return ord;
}
inline int lastUsedPattern(const Song& s) { int last = 0; for (int p = 0; p < kPats; ++p) if (s.usedChannels(p) > 0) last = p; for (auto o : s.order) if (s.ownOrder) last = juce::jmax(last, (int) o); return last; }
inline int lastUsedInstrument(const Song& s) { int last = 0; for (int i = 0; i < kInstruments; ++i) if (s.inst[(size_t) i].hasSound()) last = i + 1; for (int p = 0; p < kPats; ++p) for (auto& ch : s.pat(p).cells) for (auto& c : ch) if (c.inst > last && c.inst <= kInstruments) last = c.inst; return last; }

// ---------------------------------------------------------------- XM writer
inline juce::MemoryBlock writeXM(const Song& s, const SoundOf& soundOf, const juce::String& title, double bpm, Report& rep) {
    Writer w; const auto ord = orderOf(s); const int nPat = lastUsedPattern(s) + 1, nIns = juce::jmax(1, lastUsedInstrument(s));
    std::array<SampleOut, kInstruments> smp; for (int i = 0; i < nIns; ++i) smp[(size_t) i] = sampleOf(s.inst[(size_t) i], soundOf ? soundOf(s.inst[(size_t) i]) : nullptr);
    w.str("Extended Module: ", 17); w.str(title, 20); w.u8(0x1A); w.str("Ghosty Beats", 20); w.u16(0x0104);
    w.u32(276); w.u16((int) ord.size()); w.u16(0); w.u16(kChannels); w.u16(nPat); w.u16(nIns); w.u16(1);   // linear frequency table
    w.u16(juce::jlimit(1, 31, s.speed)); w.u16(juce::jlimit(32, 255, (int) std::lround(bpm)));
    for (int i = 0; i < 256; ++i) w.u8(i < (int) ord.size() ? ord[(size_t) i] : 0);
    for (int p = 0; p < nPat; ++p) {
        const auto& pt = s.pat(p); const int rows = juce::jlimit(1, 256, pt.rows);
        juce::MemoryOutputStream pk;
        for (int r = 0; r < rows; ++r) for (int c = 0; c < kChannels; ++c) {
            const auto& cl = pt.cells[(size_t) c][(size_t) r]; int note = 0, ins = cl.inst <= kInstruments ? cl.inst : 0, vol = 0, ft = 0, fp = 0;
            if (cl.note == kNoteOff) note = 97; else if (cl.note >= 1 && cl.note <= 128) { const int m = cl.note - 1 - 11; note = m >= 1 && m <= 96 ? m : 0; }
            if (cl.vol != kNoVol) vol = 0x10 + juce::jmin(64, (int) cl.vol);
            const int len = ins >= 1 ? (int) smp[(size_t) ins - 1].pcm.size() : 0;
            if (!fxToXm(cl, ft, fp, len)) { ++rep.droppedFx; ft = fp = 0; }
            uint8_t flags = 0x80; if (note) flags |= 1; if (ins) flags |= 2; if (vol) flags |= 4; if (ft) flags |= 8; if (fp) flags |= 16;
            pk.writeByte((char) flags); if (note) pk.writeByte((char) note); if (ins) pk.writeByte((char) ins); if (vol) pk.writeByte((char) vol); if (ft) pk.writeByte((char) ft); if (fp) pk.writeByte((char) fp);
        }
        w.u32(9); w.u8(0); w.u16(rows); w.u16((int) pk.getDataSize()); w.o.write(pk.getData(), pk.getDataSize());
    }
    for (int i = 0; i < nIns; ++i) {
        const auto& in = s.inst[(size_t) i]; const auto& so = smp[(size_t) i]; const bool has = !so.pcm.empty();
        w.u32(has ? 263 : 29); w.str(in.label(), 22); w.u8(0); w.u16(has ? 1 : 0);
        if (!has) continue;
        w.u32(40); w.zeros(96);   // every note -> sample 1
        w.zeros(48); w.zeros(48); w.zeros(14); w.u16(0); w.zeros(22);   // no envelopes, no auto-vibrato, no fadeout
        int rel = 0, fine = 0; xmTune(so.rate, in.root, rel, fine); fine += (int) std::lround(-in.tune * 128.0 / 100.0); fine = juce::jlimit(-128, 127, fine);
        w.u32((uint32_t) so.pcm.size() * 2); w.u32((uint32_t) so.loopA * 2); w.u32((uint32_t) (so.loop ? (so.loopB - so.loopA) * 2 : 0));
        w.u8(juce::jlimit(0, 64, (int) std::lround(in.vol / 0.8f * 64.0f))); w.u8((uint8_t) (int8_t) fine); w.u8((so.loop ? 1 : 0) | 0x10); w.u8(juce::jlimit(0, 255, (int) std::lround((in.pan + 1.0f) * 127.5f)));
        w.u8((uint8_t) (int8_t) rel); w.u8(0); w.str(in.label(), 22);
        int16_t prev = 0; for (auto v : so.pcm) { w.u16((uint16_t) (int16_t) (v - prev)); prev = v; }   // delta-coded
        ++rep.samples;
    }
    rep.patterns = nPat; rep.instruments = nIns; rep.bpm = (int) std::lround(bpm); rep.speed = s.speed;
    return w.o.getMemoryBlock();
}

// ---------------------------------------------------------------- shared import tail
inline void finishImport(Song& s, const std::vector<int>& ord, int speed, int bpm, Report& rep) {
    s.on = true; s.speed = juce::jlimit(1, 31, speed); s.lpb = juce::jlimit(1, 16, (int) std::lround(24.0 / s.speed));   // speed 6 -> 1/16 rows
    s.ownOrder = true; s.order.clear(); for (int o : ord) if (o >= 0 && o < kPats && s.order.size() < (size_t) kOrderMax) s.order.push_back((uint8_t) o); if (s.order.empty()) s.order.push_back(0);
    if (bpm >= 32) { auto& c0 = s.pat(s.order[0]).cells[0][0]; if (c0.fx == FxNone) { c0.fx = FxSpeed; c0.param = (uint8_t) juce::jlimit(32, 255, bpm); } }   // the module's tempo as F (BPM)
    rep.speed = speed; rep.bpm = bpm; s.prepare();
}

// ---------------------------------------------------------------- XM reader
inline bool readXM(const void* data, size_t size, Song& s, Report& rep) {
    Reader r(data, size);
    if (size < 80 || std::memcmp(data, "Extended Module:", 16) != 0) { rep.error = "Not an XM file."; return false; }
    r.seek(17); r.str(20); r.u8(); r.str(20); r.u16(); const size_t hdrAt = r.p; const uint32_t hsize = r.u32();
    const int songLen = r.u16(); r.u16(); const int nCh = r.u16(), nPat = r.u16(), nIns = r.u16(); r.u16(); const int speed = r.u16(), bpm = r.u16();
    std::vector<int> ord; for (int i = 0; i < 256; ++i) { const int o = r.u8(); if (i < songLen) ord.push_back(o); }
    if (!r.ok || nCh < 1 || nCh > 64 || nPat > 256 || nIns > 128) { rep.error = "Damaged XM header."; return false; }
    r.seek(hdrAt + juce::jlimit<uint32_t>(20, (uint32_t) size, hsize));
    Song out; out.inst = s.inst; for (auto& in : out.inst) in = Instrument();
    std::vector<std::vector<std::array<int, 5>>> rawPats;   // [pattern][row * ch] = note, ins, vol, fxT, fxP (converted after the samples are known)
    std::vector<int> patRows;
    for (int p = 0; p < nPat && r.ok; ++p) {
        const size_t at = r.p; const uint32_t hl = r.u32(); r.u8(); const int rows = r.u16(); const int ps = r.u16(); r.seek(at + juce::jlimit<uint32_t>(9, 4096, hl));
        if (!r.ok) break;
        std::vector<std::array<int, 5>> cells((size_t) juce::jlimit(1, 256, rows) * (size_t) nCh, std::array<int, 5> {});
        const size_t end = juce::jmin(size, r.p + (size_t) ps); size_t k = 0;
        while (ps > 0 && r.p < end && k < cells.size()) {
            const int b = r.u8(); auto& c = cells[k++];
            if (b & 0x80) { if (b & 1) c[0] = r.u8(); if (b & 2) c[1] = r.u8(); if (b & 4) c[2] = r.u8(); if (b & 8) c[3] = r.u8(); if (b & 16) c[4] = r.u8(); }
            else { c[0] = b; c[1] = r.u8(); c[2] = r.u8(); c[3] = r.u8(); c[4] = r.u8(); }
        }
        r.seek(end); rawPats.push_back(std::move(cells)); patRows.push_back(juce::jlimit(1, 256, rows));
    }
    std::array<int, kInstruments> sampleLen {};
    for (int i = 0; i < nIns && r.ok; ++i) {
        const size_t at = r.p; const uint32_t isz = r.u32(); const auto name = r.str(22); r.u8(); const int ns = juce::jmin(16, (int) r.u16());
        if (!r.ok) break;
        std::vector<std::array<int, 6>> sh;   // len, loopStart, loopLen, type, fine, rel   + vol, pan below
        std::vector<std::pair<int, int>> volPan; std::vector<juce::String> sname;
        uint32_t shSize = 40;
        if (ns > 0) { shSize = r.u32(); if (shSize < 40 || shSize > 256) shSize = 40; }
        r.seek(at + juce::jlimit<uint32_t>(29, 4096, isz));
        for (int k = 0; k < ns && r.ok; ++k) {
            const size_t sa = r.p; const int len = (int) juce::jmin<uint32_t>(r.u32(), 64u << 20), ls = (int) juce::jmin<uint32_t>(r.u32(), 64u << 20), ll = (int) juce::jmin<uint32_t>(r.u32(), 64u << 20);
            const int vol = r.u8(), fine = (int8_t) r.u8(), type = r.u8(), pan = r.u8(), rel = (int8_t) r.u8(); r.u8(); sname.push_back(r.str(22)); r.seek(sa + shSize);
            sh.push_back({ len, ls, ll, type, fine, rel }); volPan.push_back({ vol, pan });
        }
        for (int k = 0; k < (int) sh.size() && r.ok; ++k) {
            const auto& h = sh[(size_t) k]; const bool b16 = (h[3] & 0x10) != 0; const int bytes = h[0]; if (!r.has((size_t) bytes)) { r.ok = false; break; }
            if (k == 0 && i < kInstruments) {   // our instrument plays one sound: the first sample
                std::vector<float> x; const int frames = b16 ? bytes / 2 : bytes; x.reserve((size_t) frames); int acc = 0;
                for (int f = 0; f < frames; ++f) { if (b16) { acc = (int16_t) (acc + (int16_t) r.u16()); x.push_back(acc / 32768.0f); } else { acc = (int8_t) (acc + (int8_t) r.u8()); x.push_back(acc / 128.0f); } }
                if (b16 && (bytes & 1)) r.skip(1);
                auto& in = out.inst[(size_t) i]; in.name = name; in.sample = soundFromPcm(x, xmRate(h[5], h[4]), sname[(size_t) k].isNotEmpty() ? sname[(size_t) k] : name);
                in.root = 60; in.tune = 0; in.vol = juce::jlimit(0.0f, 1.0f, volPan[(size_t) k].first / 64.0f * 0.8f); in.pan = juce::jlimit(-1.0f, 1.0f, volPan[(size_t) k].second / 127.5f - 1.0f); in.route = s.inst[(size_t) i].route;
                const int fr = (int) x.size(); sampleLen[(size_t) i] = fr;
                if ((h[3] & 3) != 0 && h[2] > 0 && fr > 2) { const int div = b16 ? 2 : 1; in.loop = true; in.loopStart = juce::jlimit(0.0f, 1.0f, (h[1] / div) / (float) fr); in.loopEnd = juce::jlimit(in.loopStart, 1.0f, ((h[1] + h[2]) / div) / (float) fr); }
                if (in.sample) ++rep.samples;
            } else r.skip((size_t) bytes);
        }
        if (i < kInstruments && out.inst[(size_t) i].name.isEmpty()) out.inst[(size_t) i].name = name;
        rep.instruments = juce::jmin(kInstruments, i + 1);
    }
    // patterns -> our cells (8 channels, 128 rows, 16 patterns)
    for (int p = 0; p < (int) rawPats.size() && p < kPats; ++p) {
        auto& pt = out.pat(p); const int rows = patRows[(size_t) p]; pt.rows = juce::jmin(kRows, rows); if (rows > kRows) ++rep.truncatedRows;
        for (int rr = 0; rr < pt.rows; ++rr) for (int c = 0; c < nCh; ++c) {
            const auto& q = rawPats[(size_t) p][(size_t) (rr * nCh + c)];
            if (c >= kChannels) { if (q[0] || q[1] || q[3] || q[4]) ++rep.droppedChannels; continue; }
            Cell cl; if (q[0] == 97) cl.note = kNoteOff; else if (q[0] >= 1 && q[0] <= 96) cl.note = (uint8_t) (q[0] + 11 + 1);
            cl.inst = (uint8_t) (q[1] >= 1 && q[1] <= kInstruments ? q[1] : 0); if (q[2] >= 0x10 && q[2] <= 0x50) cl.vol = (uint8_t) (q[2] - 0x10);
            const int len = cl.inst ? sampleLen[(size_t) cl.inst - 1] : 0;
            if ((q[3] || q[4]) && !fxFromXm(q[3], q[4], cl, len)) { ++rep.droppedFx; cl.fx = FxNone; cl.param = 0; }
            pt.cells[(size_t) c][(size_t) rr] = cl;
        }
        ++rep.patterns;
    }
    if (rep.patterns == 0 && rep.instruments == 0) { rep.error = r.ok ? "That XM file has no patterns." : "Damaged XM file."; return false; }
    if (!r.ok) rep.error = "The file ends early: imported what was there.";
    finishImport(out, ord, speed, bpm, rep); s = out; return true;
}

// ---------------------------------------------------------------- IT writer (sample mode: the instrument column picks a sample)
inline juce::MemoryBlock writeIT(const Song& s, const SoundOf& soundOf, const juce::String& title, double bpm, Report& rep) {
    Writer w; const auto ord = orderOf(s); const int nPat = lastUsedPattern(s) + 1, nSmp = juce::jmax(1, lastUsedInstrument(s));
    std::array<SampleOut, kInstruments> smp; for (int i = 0; i < nSmp; ++i) smp[(size_t) i] = sampleOf(s.inst[(size_t) i], soundOf ? soundOf(s.inst[(size_t) i]) : nullptr);
    w.str("IMPM", 4); w.str(title, 26); w.u16(0x1004); w.u16((int) ord.size() + 1); w.u16(0); w.u16(nSmp); w.u16(nPat); w.u16(0x0214); w.u16(0x0214);
    w.u16(1 | 8); w.u16(0); w.u8(128); w.u8(48); w.u8(juce::jlimit(1, 255, s.speed)); w.u8(juce::jlimit(32, 255, (int) std::lround(bpm))); w.u8(128); w.u8(0); w.u16(0); w.u32(0); w.u32(0);   // stereo, linear slides
    for (int c = 0; c < 64; ++c) w.u8(c < kChannels ? 32 : 32 | 128); for (int c = 0; c < 64; ++c) w.u8(64);
    for (int o : ord) w.u8(o); w.u8(255);
    const size_t smpOffAt = w.pos(); for (int i = 0; i < nSmp; ++i) w.u32(0); const size_t patOffAt = w.pos(); for (int p = 0; p < nPat; ++p) w.u32(0);
    std::vector<size_t> dataPtrAt((size_t) nSmp);
    for (int i = 0; i < nSmp; ++i) {
        w.patch32(smpOffAt + (size_t) i * 4, (uint32_t) w.pos()); const auto& in = s.inst[(size_t) i]; const auto& so = smp[(size_t) i]; const bool has = !so.pcm.empty();
        w.str("IMPS", 4); w.str("", 12); w.u8(0); w.u8(64); w.u8(has ? (1 | 2 | (so.loop ? 16 : 0)) : 0); w.u8(juce::jlimit(0, 64, (int) std::lround(in.vol / 0.8f * 64.0f))); w.str(in.label(), 26);
        w.u8(1); w.u8(juce::jlimit(0, 64, (int) std::lround((in.pan + 1.0f) * 32.0f)) | 128); w.u32((uint32_t) so.pcm.size()); w.u32((uint32_t) so.loopA); w.u32((uint32_t) (so.loop ? so.loopB : 0));
        w.u32((uint32_t) std::lround(so.rate * std::pow(2.0, (60 - in.root - in.tune / 100.0) / 12.0))); w.u32(0); w.u32(0); dataPtrAt[(size_t) i] = w.pos(); w.u32(0); w.u8(0); w.u8(0); w.u8(0); w.u8(0);
    }
    for (int p = 0; p < nPat; ++p) {
        w.patch32(patOffAt + (size_t) p * 4, (uint32_t) w.pos()); const auto& pt = s.pat(p); const int rows = juce::jlimit(32, 200, juce::jmax(32, pt.rows));
        juce::MemoryOutputStream pk;
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < kChannels; ++c) {
                if (r >= pt.rows) break; const auto& cl = pt.cells[(size_t) c][(size_t) r]; if (cl.empty()) continue;
                int note = -1, ins = cl.inst <= kInstruments ? cl.inst : 0, vol = -1, ft = 0, fp = 0;
                if (cl.note == kNoteOff) note = 255; else if (cl.note >= 1 && cl.note <= 120) note = cl.note - 1;
                if (cl.vol != kNoVol) vol = juce::jmin(64, (int) cl.vol);
                const int len = ins >= 1 ? (int) smp[(size_t) ins - 1].pcm.size() : 0;
                if (!fxToIt(cl, ft, fp, len)) { ++rep.droppedFx; ft = fp = 0; }
                int mask = 0; if (note >= 0) mask |= 1; if (ins) mask |= 2; if (vol >= 0) mask |= 4; if (ft) mask |= 8;
                if (!mask) continue;
                pk.writeByte((char) ((c + 1) | 128)); pk.writeByte((char) mask);
                if (mask & 1) pk.writeByte((char) note); if (mask & 2) pk.writeByte((char) ins); if (mask & 4) pk.writeByte((char) vol); if (mask & 8) { pk.writeByte((char) ft); pk.writeByte((char) fp); }
            }
            pk.writeByte(0);
        }
        w.u16((int) pk.getDataSize()); w.u16(rows); w.u32(0); w.o.write(pk.getData(), pk.getDataSize());
    }
    for (int i = 0; i < nSmp; ++i) { const auto& so = smp[(size_t) i]; if (so.pcm.empty()) continue; w.patch32(dataPtrAt[(size_t) i], (uint32_t) w.pos()); for (auto v : so.pcm) w.u16((uint16_t) v); ++rep.samples; }
    rep.patterns = nPat; rep.instruments = nSmp; rep.bpm = (int) std::lround(bpm); rep.speed = s.speed;
    return w.o.getMemoryBlock();
}

// ---------------------------------------------------------------- IT reader (instrument or sample mode; uncompressed samples)
inline bool readIT(const void* data, size_t size, Song& s, Report& rep) {
    Reader r(data, size);
    if (size < 0xC0 || std::memcmp(data, "IMPM", 4) != 0) { rep.error = "Not an IT file."; return false; }
    r.seek(4); r.str(26); r.u16(); const int ordNum = r.u16(), insNum = r.u16(), smpNum = r.u16(), patNum = r.u16(); r.u16(); r.u16();
    const int flags = r.u16(); r.u16(); r.u8(); r.u8(); const int speed = r.u8(), tempo = r.u8();
    if (!r.ok || ordNum > 256 || insNum > 256 || smpNum > 256 || patNum > 256) { rep.error = "Damaged IT header."; return false; }
    r.seek(0xC0); std::vector<int> ord; for (int i = 0; i < ordNum; ++i) { const int o = r.u8(); if (o < 200) ord.push_back(o); }
    std::vector<uint32_t> insOff, smpOff, patOff; for (int i = 0; i < insNum; ++i) insOff.push_back(r.u32()); for (int i = 0; i < smpNum; ++i) smpOff.push_back(r.u32()); for (int i = 0; i < patNum; ++i) patOff.push_back(r.u32());
    if (!r.ok) { rep.error = "Damaged IT file."; return false; }
    Song out; for (auto& in : out.inst) in = Instrument();
    // samples
    struct Smp { juce::String name; SoundPtr snd; int len = 0; float vol = 0.8f, pan = 0; bool loop = false; float ls = 0, le = 1; };
    std::vector<Smp> smps((size_t) smpNum);
    for (int i = 0; i < smpNum; ++i) {
        r.ok = true; r.seek(smpOff[(size_t) i]); if (!r.ok || !r.has(80)) continue;
        if (r.u32() != 0x53504D49u) continue;   // "IMPS"
        r.skip(12); r.u8(); r.u8(); const int fl = r.u8(), vol = r.u8(); auto& S = smps[(size_t) i]; S.name = r.str(26); const int cvt = r.u8(), dfp = r.u8();
        const int len = (int) juce::jmin<uint32_t>(r.u32(), 16u << 20), lb = (int) r.u32(), le = (int) r.u32(); const uint32_t c5 = r.u32(); r.u32(); r.u32(); const uint32_t ptr = r.u32();
        S.vol = juce::jlimit(0.0f, 1.0f, vol / 64.0f * 0.8f); if (dfp & 128) S.pan = juce::jlimit(-1.0f, 1.0f, (dfp & 127) / 32.0f - 1.0f);
        if (!(fl & 1) || len <= 0 || (fl & 8)) continue;   // no data, or IT214-compressed (not read: the name and settings still come in)
        const bool b16 = (fl & 2) != 0, sgn = (cvt & 1) != 0; const size_t bytes = (size_t) len * (b16 ? 2 : 1);
        r.ok = true; r.seek(ptr); if (!r.has(bytes)) continue;
        std::vector<float> x((size_t) len);
        for (int f = 0; f < len; ++f) { if (b16) { int v = r.u16(); if (!sgn) v -= 32768; x[(size_t) f] = (int16_t) v / 32768.0f; } else { int v = r.u8(); if (!sgn) v -= 128; x[(size_t) f] = (int8_t) v / 128.0f; } }
        S.snd = soundFromPcm(x, c5 > 0 ? (double) c5 : 8363.0, S.name); S.len = len;
        if ((fl & 16) && le > lb && lb >= 0 && le <= len) { S.loop = true; S.ls = lb / (float) len; S.le = le / (float) len; }
    }
    auto takeSample = [&](int i, int si, const juce::String& nm) {
        if (i < 0 || i >= kInstruments || si < 0 || si >= smpNum) return; const auto& S = smps[(size_t) si]; auto& in = out.inst[(size_t) i];
        in.name = nm.isNotEmpty() ? nm : S.name; in.sample = S.snd; in.root = 60; in.tune = 0; in.vol = S.vol; in.pan = S.pan; in.loop = S.loop; in.loopStart = S.ls; in.loopEnd = S.le; in.route = s.inst[(size_t) i].route;
        if (S.snd) ++rep.samples;
    };
    std::array<int, kInstruments> lenOf {};
    if ((flags & 4) && insNum > 0) {   // instrument mode: each instrument plays the sample its C-5 key maps to
        for (int i = 0; i < insNum && i < kInstruments; ++i) {
            r.ok = true; r.seek(insOff[(size_t) i]); if (!r.has(0x40 + 240) || r.u32() != 0x49504D49u) continue;   // "IMPI"
            r.seek(insOff[(size_t) i] + 0x20); const auto nm = r.str(26); r.seek(insOff[(size_t) i] + 0x40 + 60 * 2 + 1); const int si = r.u8() - 1;
            takeSample(i, si, nm); if (si >= 0 && si < smpNum) lenOf[(size_t) i] = smps[(size_t) si].len;
        }
        rep.instruments = juce::jmin(kInstruments, insNum);
    } else { for (int i = 0; i < smpNum && i < kInstruments; ++i) { takeSample(i, i, {}); lenOf[(size_t) i] = smps[(size_t) i].len; } rep.instruments = juce::jmin(kInstruments, smpNum); }
    // patterns
    for (int p = 0; p < patNum && p < kPats; ++p) {
        auto& pt = out.pat(p); pt.rows = 64;
        if (patOff[(size_t) p] == 0) { ++rep.patterns; continue; }   // an empty 64-row pattern
        r.ok = true; r.seek(patOff[(size_t) p]); const int plen = r.u16(), rows = r.u16(); r.skip(4); if (!r.ok) continue;
        pt.rows = juce::jlimit(1, kRows, rows); if (rows > kRows) ++rep.truncatedRows;
        const size_t end = juce::jmin(size, r.p + (size_t) plen); std::array<int, 64> lastMask {}; std::array<std::array<int, 5>, 64> last {};
        int row = 0;
        while (r.ok && r.p < end && row < rows) {
            const int cv = r.u8(); if (cv == 0) { ++row; continue; }
            const int c = (cv - 1) & 63; int mask = lastMask[(size_t) c]; if (cv & 128) { mask = r.u8(); lastMask[(size_t) c] = mask; }
            auto& L = last[(size_t) c]; int note = -1, ins = 0, vol = -1, ft = 0, fp = 0;
            if (mask & 1) { L[0] = r.u8(); note = L[0]; } if (mask & 2) { L[1] = r.u8(); ins = L[1]; } if (mask & 4) { L[2] = r.u8(); vol = L[2]; } if (mask & 8) { L[3] = r.u8(); L[4] = r.u8(); ft = L[3]; fp = L[4]; }
            if (mask & 16) note = L[0]; if (mask & 32) ins = L[1]; if (mask & 64) vol = L[2]; if (mask & 128) { ft = L[3]; fp = L[4]; }
            if (row >= kRows) continue;
            if (c >= kChannels) { if (note >= 0 || ins || ft) ++rep.droppedChannels; continue; }
            Cell cl; if (note == 254 || note == 255) cl.note = kNoteOff; else if (note >= 0 && note <= 119) cl.note = (uint8_t) (note + 1);
            cl.inst = (uint8_t) (ins >= 1 && ins <= kInstruments ? ins : 0); if (vol >= 0 && vol <= 64) cl.vol = (uint8_t) vol;
            if (ft && !fxFromIt(ft, fp, cl, cl.inst ? lenOf[(size_t) cl.inst - 1] : 0)) { ++rep.droppedFx; cl.fx = FxNone; cl.param = 0; }
            pt.cells[(size_t) c][(size_t) row] = cl;
        }
        ++rep.patterns;
    }
    if (rep.patterns == 0 && rep.instruments == 0) { rep.error = "That IT file has nothing we can read."; return false; }
    finishImport(out, ord, speed, tempo, rep); s = out; return true;
}

} // namespace gb::mel::files
