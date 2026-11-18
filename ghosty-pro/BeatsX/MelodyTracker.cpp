// MELODIC TRACKER: see MelodyTracker.h.
#include "MelodyTracker.h"
#include <map>

namespace gb::mel {

// ---------------------------------------------------------------- song timeline (speeds per row, OWN ORDER with jumps)
static bool isLoopCell(const Cell& c) { return c.fx == FxFine && (c.param >> 4) == 6; }
std::vector<uint8_t> loopRows(const Pattern& pt) {
    const int rows = juce::jlimit(1, kRows, pt.rows); bool any = false;
    for (int r = 0; r < rows && !any; ++r) for (int c = 0; c < kChannels; ++c) if (isLoopCell(pt.cells[(size_t) c][(size_t) r]) && (pt.cells[(size_t) c][(size_t) r].param & 15) > 0) { any = true; break; }
    if (!any) return {};
    std::vector<uint8_t> out; int start = 0, count = 0, row = 0;
    while (row < rows && out.size() < 4096) {
        out.push_back((uint8_t) row); int jumpTo = -1;
        for (int c = 0; c < kChannels; ++c) { const auto& cl = pt.cells[(size_t) c][(size_t) row]; if (!isLoopCell(cl)) continue; const int x = cl.param & 15;
            if (x == 0) start = row;
            else { if (count == 0) { count = x; jumpTo = start; } else if (--count > 0) jumpTo = start; }
            break; }
        row = jumpTo >= 0 ? jumpTo : row + 1;
    }
    return out;
}
void Song::prepare() {
    auto tl = std::make_shared<Timeline>();
    for (int p = 0; p < kPats; ++p) tl->seq[(size_t) p] = loopRows(pat(p));
    for (int p = 0; p < kPats; ++p) {
        const auto& pt = pat(p); int cur = juce::jlimit(1, 32, speed);
        for (int r = 0; r < kRows; ++r) {
            if (r < pt.rows) for (int c = 0; c < kChannels; ++c) { const auto& cl = pt.cells[(size_t) c][(size_t) r]; if (cl.fx == FxSpeed && cl.param >= 1 && cl.param <= 31) cur = cl.param; }
            tl->speed[(size_t) p][(size_t) r] = (uint8_t) cur;
        }
    }
    if (ownOrder) {
        std::vector<uint8_t> ord = order; if (ord.empty()) ord.push_back(0);
        for (auto& o : ord) o = (uint8_t) juce::jlimit(0, kPats - 1, (int) o);
        std::map<long, long> seen; int pos = 0, row = 0; long abs = 0; Timeline::Seg seg; bool open = false; int lStart = 0, lCount = 0;
        while (abs < 65536) {
            const long key = ((long) pos * kRows + row) * 16 + lCount;
            if (auto it = seen.find(key); it != seen.end()) { tl->loopFrom = it->second; break; }
            seen[key] = abs;
            const int p = ord[(size_t) pos]; const auto& pt = pat(p); const int rows = juce::jlimit(1, kRows, pt.rows);
            if (!open) { seg = {}; seg.order = pos; seg.pat = p; seg.row0 = row; seg.abs0 = abs; open = true; }
            int jump = -1, loopTo = -1; for (int c = 0; c < kChannels; ++c) { const auto& cl = pt.cells[(size_t) c][(size_t) row]; if (cl.fx == FxJump) jump = cl.param; }
            for (int c = 0; c < kChannels; ++c) { const auto& cl = pt.cells[(size_t) c][(size_t) row]; if (!isLoopCell(cl)) continue; const int x = cl.param & 15;   // E6x pattern loop
                if (x == 0) lStart = row; else if (lCount == 0) { lCount = x; loopTo = lStart; } else if (--lCount > 0) loopTo = lStart; break; }
            ++abs;
            int npos = pos, nrow = row + 1;
            if (loopTo >= 0 && jump < 0) nrow = loopTo;
            else if (jump >= 0) { npos = jump < (int) ord.size() ? jump : 0; nrow = 0; }
            else if (nrow >= rows) { npos = (pos + 1) % (int) ord.size(); nrow = 0; }
            if (npos != pos || nrow != row + 1) { seg.row1 = row + 1; tl->segs.push_back(seg); open = false; }
            if (npos != pos || (nrow == 0 && loopTo < 0)) { lStart = 0; lCount = 0; }   // a new pattern pass: loop state resets
            pos = npos; row = nrow;
        }
        if (open) { seg.row1 = row; tl->segs.push_back(seg); }
        tl->total = abs;
    }
    timeline = tl;
}
double Song::orderBeats() const { return timeline && ownOrder ? (double) timeline->total * rowBeats() : pat(0).rows * rowBeats(); }
int Song::usedChannels(int p) const { int n = 0; const auto& pt = pat(p); for (int c = 0; c < kChannels; ++c) for (int r = 0; r < pt.rows; ++r) if (!pt.cells[(size_t) c][(size_t) r].empty()) { n = c + 1; break; } return n; }

// ---------------------------------------------------------------- sequencer (shared by audio and MIDI)
void Sequencer::tick(const Song& s, const Pattern& p, int row, int tick, int speed, std::array<ChanCtl, kChannels>& out) {
    row = juce::jlimit(0, kRows - 1, row); speed = juce::jlimit(1, 32, speed);
    for (int c = 0; c < kChannels; ++c) {
        Chan& C = ch[(size_t) c]; ChanCtl& o = out[(size_t) c]; o = ChanCtl();
        if (tick == 0) {
            C.cell = p.cells[(size_t) c][(size_t) row]; C.fx = C.cell.fx < FxCount ? C.cell.fx : (uint8_t) FxNone; C.param = C.cell.param; C.silentRow = false;
            switch (C.fx) {   // effect memory: 00 = the last value of this effect on this channel (nibble-wise for the two LFOs)
                case FxArp: case FxSlideUp: case FxSlideDown: case FxGlide: case FxVolSlide: case FxRetrig: case FxPanSlide:
                    if (C.param == 0) C.param = C.mem[C.fx]; else C.mem[C.fx] = C.param; break;
                case FxVibrato: case FxTremolo: {
                    const int x = (C.param >> 4) ? (C.param >> 4) : (C.mem[C.fx] >> 4), y = (C.param & 15) ? (C.param & 15) : (C.mem[C.fx] & 15);
                    C.mem[C.fx] = C.param = (uint8_t) ((x << 4) | y);
                    if (C.fx == FxVibrato) { C.vibSpeed = x / 64.0f; C.vibDepth = y / 8.0f; } else { C.tremSpeed = x / 64.0f; C.tremDepth = y / 15.0f; }
                    break; }
                case FxPan: C.pan = juce::jlimit(-1.0f, 1.0f, (C.param - 128) / 127.0f); break;
                case FxReverse: if (C.active && C.reverse != (C.param != 0)) o.flip = true; C.reverse = C.param != 0; break;
                case FxCutoff: C.cutoff = C.param / 255.0f; break;
                case FxReso: C.reso = C.param / 255.0f; break;
                case FxSpeed: if (C.param >= 0x20) tempo = C.param; break;   // F20..FF: a tempo command (the engine decides what it changes)
                default: break;
            }
            C.silentRow = C.fx == FxCut && C.param == 0 && C.cell.note >= 1 && C.cell.note <= 128;   // C00 on a note row: that note never sounds
        }
        const int x = C.param >> 4, y = C.param & 15;
        const int delay = C.fx == FxDelay ? (int) C.param : 0;
        if (tick == delay) {   // the row's note / instrument / volume land on this tick
            const Cell& k = C.cell;
            if (k.inst >= 1 && k.inst <= kInstruments) { C.inst = k.inst - 1; if (k.vol == kNoVol) C.vol = 64; }
            if (k.note == kNoteOff) { if (C.active) o.release = true; C.active = false; }
            else if (k.note >= 1 && k.note <= 128) {
                const float n = (float) (k.note - 1);
                if (C.fx == FxGlide && C.active) C.target = n;
                else if (C.silentRow) { if (C.active) o.cut = true; C.active = false; }
                else if (C.inst >= 0 && s.inst[(size_t) C.inst].hasSound()) {
                    const auto& in = s.inst[(size_t) C.inst];
                    C.pitch = C.target = n; C.active = true; C.vibPhase = 0; C.tremPhase = 0; C.offset = C.fx == FxOffset ? C.param / 256.0f : 0.0f; o.trigger = true;
                    if (C.fx != FxReverse) C.reverse = false;
                    if (C.fx != FxCutoff) C.cutoff = in.cutoff; if (C.fx != FxReso) C.reso = in.reso;
                }
            }
            if (k.vol != kNoVol) C.vol = (float) juce::jmin(64, (int) k.vol);
            if (C.fx == FxFine) { const int sub = C.param >> 4, v = C.param & 15;
                if (sub == 1) C.pitch += v / 16.0f; else if (sub == 2) C.pitch -= v / 16.0f; else if (sub == 0xA) C.vol = juce::jmin(64.0f, C.vol + v); else if (sub == 0xB) C.vol = juce::jmax(0.0f, C.vol - v); }
        }
        o.p0 = C.pitch;
        switch (C.fx) {
            case FxSlideUp:   C.pitch += C.param / 16.0f; break;
            case FxSlideDown: C.pitch -= C.param / 16.0f; break;
            case FxGlide: { const float g = C.param / 16.0f; if (C.pitch < C.target) C.pitch = juce::jmin(C.target, C.pitch + g); else if (C.pitch > C.target) C.pitch = juce::jmax(C.target, C.pitch - g); break; }
            default: break;
        }
        C.pitch = juce::jlimit(0.0f, 135.0f, C.pitch);
        o.p1 = C.pitch;
        if (C.fx == FxArp && C.param) o.arp = tick % 3 == 0 ? 0 : (tick % 3 == 1 ? x : y);
        if (C.fx == FxVibrato && C.vibDepth > 0) { o.vib = C.vibDepth; o.vph0 = C.vibPhase; o.vphInc = C.vibSpeed; C.vibPhase = std::fmod(C.vibPhase + C.vibSpeed, 1.0f); }
        if (C.fx == FxTremolo && C.tremDepth > 0) { o.trem = C.tremDepth; o.tph0 = C.tremPhase; o.tphInc = C.tremSpeed; C.tremPhase = std::fmod(C.tremPhase + C.tremSpeed, 1.0f); }
        o.v0 = C.vol / 64.0f;
        if (C.fx == FxVolSlide) C.vol = juce::jlimit(0.0f, 64.0f, C.vol + (float) (x ? x : -y));
        o.v1 = C.vol / 64.0f;
        o.pan0 = C.pan;
        if (C.fx == FxPanSlide) C.pan = juce::jlimit(-1.0f, 1.0f, C.pan + (x ? x : -y) / 64.0f);
        o.pan1 = C.pan;
        if (C.fx == FxRetrig && y > 0 && tick > 0 && tick % y == 0 && C.active) {
            static const float add[16] { 0, -1, -2, -4, -8, -16, 0, 0, 0, 1, 2, 4, 8, 16, 0, 0 };
            if (x == 6) C.vol *= 2.0f / 3.0f; else if (x == 7) C.vol *= 0.5f; else if (x == 14) C.vol *= 1.5f; else if (x == 15) C.vol *= 2.0f; else C.vol += add[x];
            C.vol = juce::jlimit(0.0f, 64.0f, C.vol); o.v0 = o.v1 = C.vol / 64.0f; o.trigger = true; o.p0 = o.p1 = C.pitch; C.offset = 0;
        }
        if (C.fx == FxCut && C.param > 0 && tick == (int) C.param && C.active) { o.cut = true; C.active = false; }
        o.active = C.active; o.inst = C.inst; o.offset = C.offset; o.reverse = C.reverse; o.cutoff = C.cutoff; o.reso = C.reso;
    }
}

bool tickAt(const Song& s, const Snapshot* S, bool songMode, int pattern, double beat, TickPos& out) {
    if (beat < 0) return false;
    const double rb = s.rowBeats();
    const Timeline* tl = s.timeline.get();
    int pat = juce::jlimit(0, kPats - 1, pattern); double origin = 0; long rowAbs = 0; int row = 0;
    if (s.ownOrder) {
        rowAbs = (long) std::floor(beat / rb + 1e-9);
        if (tl && tl->total > 0 && !tl->segs.empty()) {
            long r = rowAbs;
            if (r >= tl->total) { const long span = tl->total - tl->loopFrom; r = span > 0 ? tl->loopFrom + (r - tl->loopFrom) % span : r % tl->total; }
            size_t lo = 0, hi = tl->segs.size(); while (hi - lo > 1) { const size_t mid = (lo + hi) / 2; if (tl->segs[mid].abs0 <= r) lo = mid; else hi = mid; }
            const auto& sg = tl->segs[lo]; pat = sg.pat; row = juce::jlimit(0, kRows - 1, sg.row0 + (int) (r - sg.abs0));
        } else { pat = s.order.empty() ? 0 : juce::jlimit(0, kPats - 1, (int) s.order[0]); row = (int) (rowAbs % juce::jlimit(1, kRows, s.pat(pat).rows)); }
    } else {
        if (songMode && S && S->songCount > 0) {
            const long bar = (long) std::floor(beat / 4.0 + 1e-12); bool found = false;
            for (int i = 0; i < S->songCount; ++i) { const auto& b = S->song[(size_t) i]; if (bar >= b.startBar && bar < b.startBar + b.bars) { pat = juce::jlimit(0, kPats - 1, b.pattern); origin = b.startBar * 4.0; found = true; break; } }
            if (!found) return false;
        }
        rowAbs = (long) std::floor((beat - origin) / rb + 1e-9);
        if (tl && !tl->seq[(size_t) pat].empty()) { const auto& sq = tl->seq[(size_t) pat]; row = sq[(size_t) (rowAbs % (long) sq.size())]; }   // E6x loops
        else row = (int) (rowAbs % juce::jlimit(1, kRows, s.pat(pat).rows));
    }
    int speed = juce::jlimit(1, 32, s.speed);
    if (tl) speed = tl->speed[(size_t) pat][(size_t) row];
    else { const auto& pt = s.pat(pat); for (int r = 0; r <= row && r < pt.rows; ++r) for (int c = 0; c < kChannels; ++c) { const auto& cl = pt.cells[(size_t) c][(size_t) r]; if (cl.fx == FxSpeed && cl.param >= 1 && cl.param <= 31) speed = cl.param; } }
    out.pat = pat; out.origin = origin; out.rowAbs = rowAbs; out.row = row; out.speed = speed; out.rowStart = origin + rowAbs * rb;
    out.tick = juce::jlimit(0, speed - 1, (int) std::floor((beat - out.rowStart) / (rb / speed) + 1e-7));
    return true;
}

// ---------------------------------------------------------------- mip levels (127-tap Kaiser half-band decimation, -90 dB)
std::shared_ptr<const Mips> Mips::make(const Sound& s) {
    auto m = std::make_shared<Mips>();
    static const std::vector<float> h = [] {
        const int N = 127, M = N / 2; std::vector<float> k((size_t) N);
        auto bessel = [](double x) { double sum = 1, term = 1; for (int i = 1; i < 50; ++i) { term *= (x / (2.0 * i)) * (x / (2.0 * i)); sum += term; } return sum; };
        const double fc = 0.225, beta = 8.96, den = bessel(beta); double tot = 0;
        for (int i = 0; i < N; ++i) { const double t = i - M, u = (double) t / M; const double w = bessel(beta * std::sqrt(juce::jmax(0.0, 1.0 - u * u))) / den;
            const double sinc = t == 0 ? 2 * fc : std::sin(juce::MathConstants<double>::twoPi * fc * t) / (juce::MathConstants<double>::pi * t); k[(size_t) i] = (float) (sinc * w); tot += k[(size_t) i]; }
        for (auto& v : k) v = (float) (v / tot);
        return k; }();
    const int N = (int) h.size(), M = N / 2;
    auto dec = [&](const std::vector<float>& x) { const int n = (int) x.size(), out = (n + 1) / 2; std::vector<float> y((size_t) out);
        for (int j = 0; j < out; ++j) { double acc = 0; const int c = 2 * j; const int k0 = juce::jmax(0, M - c), k1 = juce::jmin(N, n - c + M);
            for (int k = k0; k < k1; ++k) acc += h[(size_t) k] * x[(size_t) (c + k - M)]; y[(size_t) j] = (float) acc; }
        return y; };
    const bool st = s.stereo();
    const std::vector<float>* pl = &s.data; const std::vector<float>* pr = st ? &s.right : nullptr;
    for (int l = 1; l < kMaxLevels; ++l) {
        if ((int) pl->size() < 64) break;
        m->L[(size_t) l] = dec(*pl); if (st) m->R[(size_t) l] = dec(*pr);
        pl = &m->L[(size_t) l]; if (st) pr = &m->R[(size_t) l]; m->levels = l + 1;
    }
    return m;
}

// ---------------------------------------------------------------- sinc readers
static double besselI0(double x) { double s = 1, term = 1; for (int k = 1; k < 50; ++k) { term *= (x / (2.0 * k)) * (x / (2.0 * k)); s += term; } return s; }
const float* HiSinc::table() {
    static const std::array<float, N> t = [] {
        std::array<float, N> a {}; const double beta = 8.0, den = besselI0(beta);
        for (int i = 0; i < N; ++i) { const double x = (double) i / PH, u = juce::jmin(1.0, x / HALF), w = besselI0(beta * std::sqrt(juce::jmax(0.0, 1.0 - u * u))) / den;
            a[(size_t) i] = (float) (i == 0 ? 1.0 : std::sin(juce::MathConstants<double>::pi * x) / (juce::MathConstants<double>::pi * x) * w); }
        a[N - 1] = a[N - 2] = 0; return a; }();
    return t.data();
}
int HiSinc::weights(double p, double stretch, float* w, int& first) {
    const float* tbl = table();
    const long c = (long) std::floor(p); const double fr = p - (double) c;
    const double sc = juce::jmin(stretch, 2.5) / kCut;
    const int span = juce::jmin(kMaxSpan, (int) std::ceil(HALF * sc)); const double scale = PH / sc;
    first = (int) (c - span + 1); int cnt = 0; float sum = 0;
    for (int k = -span + 1; k <= span; ++k) {
        const double ax = std::abs(((double) k - fr) * scale); float v = 0;
        if (ax < (double) (HALF * PH)) { const int ti = (int) ax; const float tf = (float) (ax - ti); v = tbl[ti] + (tbl[ti + 1] - tbl[ti]) * tf; }
        w[cnt++] = v; sum += v;
    }
    if (std::abs(sum) > 1e-9f) { const float g = 1.0f / sum; for (int i = 0; i < cnt; ++i) w[i] *= g; }
    return cnt;
}
const float* FastSinc::table() {
    static const std::vector<float> t = [] {
        std::vector<float> a((size_t) ((PH + 1) * TAPS)); const double beta = 8.0, den = besselI0(beta);
        for (int r = 0; r <= PH; ++r) { const double fr = (double) r / PH; double sum = 0;
            for (int k = 0; k < TAPS; ++k) { const double x = (k - (HALF - 1)) - fr, u = juce::jlimit(-1.0, 1.0, x / HALF);
                const double w = besselI0(beta * std::sqrt(juce::jmax(0.0, 1.0 - u * u))) / den, sn = std::abs(x) < 1e-12 ? 1.0 : std::sin(juce::MathConstants<double>::pi * x) / (juce::MathConstants<double>::pi * x);
                a[(size_t) (r * TAPS + k)] = (float) (sn * w); sum += sn * w; }
            for (int k = 0; k < TAPS; ++k) a[(size_t) (r * TAPS + k)] = (float) (a[(size_t) (r * TAPS + k)] / sum); }
        return a; }();
    return t.data();
}
void FastSinc::weights(double frac, float* w) {
    const float* t = table(); const double q = frac * PH; const int i = juce::jlimit(0, PH - 1, (int) q); const float f = (float) (q - i);
    const float* a = t + i * TAPS; const float* b = a + TAPS;
    for (int k = 0; k < TAPS; ++k) w[k] = a[k] + (b[k] - a[k]) * f;
}

// ---------------------------------------------------------------- player
void Player::prepare(double rate) { sr = rate; f2 = juce::jmax(16, (int) std::lround(0.002 * sr)); reset(); }
void Player::reset() { for (auto& c : v) for (auto& x : c) x = Voice(); cur.fill(-1); seq.reset(); ctl = {}; prevCtl = {}; have = false; triggers = 0; audFifo.reset(); }
int Player::activeVoices() const { int n = 0; for (auto& c : v) for (auto& x : c) if (x.snd) ++n; return n; }
int Player::levelOf(int c) const { const int k = cur[(size_t) juce::jlimit(0, kChannels, c)]; return k >= 0 ? v[(size_t) c][(size_t) k].level : -1; }
void Player::audition(int inst, int note, float vel) { int a, b, c, d; audFifo.prepareToWrite(1, a, b, c, d); if (b > 0) { audBuf[(size_t) a] = { inst, note, vel }; audFifo.finishedWrite(1); } }

void Player::stopChannel(int c, int len) {
    auto& V = v[(size_t) c]; const int k = cur[(size_t) c];
    if (k >= 0 && V[(size_t) k].snd && V[(size_t) k].owned) { auto& x = V[(size_t) k]; x.owned = false; x.fadeLen = x.fade = juce::jmax(1, x.fade >= 0 ? juce::jmin(x.fade, len) : len); }
    cur[(size_t) c] = -1;
}
void Player::releaseChannel(int c, const Song& s) {
    auto& V = v[(size_t) c]; const int k = cur[(size_t) c];
    if (k >= 0 && V[(size_t) k].snd && V[(size_t) k].owned) {
        auto& x = V[(size_t) k]; const float rel = juce::jmax(0.002f, s.inst[(size_t) juce::jlimit(0, kInstruments - 1, x.inst)].release);
        x.owned = false; x.envStage = 3; x.envRel = juce::jmax(1e-7f, x.env / (float) (rel * sr));
    }
    cur[(size_t) c] = -1;
}

void Player::start(int c, const Song& s, const Sound* const* snd, const Mips* const* mips, const Snapshot* S, const EngineParams* P, int ii, float offset, float v0, float pan, bool reverse) {
    stopChannel(c, f2);
    ii = juce::jlimit(0, kInstruments - 1, ii); const auto& in = s.inst[(size_t) ii];
    const Sound* sd = snd ? snd[ii] : nullptr;
    if (!sd || sd->data.size() < 8 || !in.used()) return;
    auto& V = v[(size_t) c];
    int pick = -1; for (int k = 0; k < 3; ++k) if (!V[(size_t) k].snd) { pick = k; break; }
    if (pick < 0) { int best = INT_MAX; for (int k = 0; k < 3; ++k) { const int f = V[(size_t) k].fade < 0 ? INT_MAX - 1 : V[(size_t) k].fade; if (f < best) { best = f; pick = k; } } }
    auto& x = V[(size_t) pick]; x = Voice();
    const int len = (int) sd->data.size(); int a = 0, z = len;
    if (in.slice > 0 && in.slice <= (int) in.slices.size()) { a = juce::jlimit(0, len - 2, in.slices[(size_t) in.slice - 1]); if (in.slice < (int) in.slices.size()) z = juce::jlimit(a + 2, len, in.slices[(size_t) in.slice]); }
    { const int a2 = a + (int) std::floor(juce::jlimit(0.0f, 0.99f, in.start) * (z - a)), z2 = a + (int) std::ceil(juce::jlimit(0.01f, 1.0f, in.end) * (z - a)); a = a2; z = juce::jmax(a + 2, z2); }
    x.snd = sd; x.mips = mips ? mips[ii] : nullptr; x.len = len; x.a = a; x.z = z; x.srcRatio = sd->rate / sr; x.inst = ii; x.route = in.routeTrack();
    x.root = (float) in.root; x.tune = in.tune; x.owned = true; x.dir = reverse ? -1 : 1;
    x.trackPitch = (in.followPitch && S) ? S->tracks[(size_t) x.route].pitch + (P ? P->tPitch[x.route] : 0.0f) : 0.0f;
    const double off = std::floor(juce::jlimit(0.0f, 0.99f, offset) * (double) (z - a));
    x.pos = reverse ? (double) (z - 1) - off : (double) a + off;
    x.fadeIn = x.fadeInLen = (off > 0 || reverse) ? f2 : 0;
    x.loop = in.loop && !reverse && in.loopEnd > in.loopStart + 0.01f;
    if (x.loop) { x.le = juce::jlimit((double) a + 4, (double) z - 1, a + (double) in.loopEnd * (z - a)); x.ls = juce::jlimit((double) a + 2, x.le - 2.0, a + (double) in.loopStart * (z - a));
                  x.xf = juce::jmin(0.01 * sd->rate, juce::jmin(x.ls - a, 0.25 * (x.le - x.ls))); if (x.xf < 2) x.loop = false; }
    const float pn = juce::jlimit(-1.0f, 1.0f, in.pan + pan), g = juce::jlimit(0.0f, 2.0f, in.vol) * v0;
    x.panL = std::cos((pn + 1) * juce::MathConstants<float>::pi * 0.25f); x.panR = std::sin((pn + 1) * juce::MathConstants<float>::pi * 0.25f);
    x.ivol = juce::jlimit(0.0f, 2.0f, in.vol); x.ipan = in.pan; x.gain = x.target = g;
    // envelope
    x.envSus = juce::jlimit(0.0f, 1.0f, in.sustain);
    x.envDec = in.decay > 0.0005f ? (1.0f - x.envSus) / (float) (in.decay * sr) : 2.0f;   // (2 = straight to the sustain level)
    if (in.attack > 0.0005f) { x.env = 0.0f; x.envStage = 0; x.envAtt = 1.0f / (float) (in.attack * sr); }
    else if (in.decay > 0.0005f && x.envSus < 1.0f) { x.env = 1.0f; x.envStage = 1; }
    else { x.env = x.envSus; x.envStage = 2; }
    x.cut = x.cutTarget = 1; x.reso = 0;
    cur[(size_t) c] = pick; ++triggers;
}

void Player::apply(const Song& s, const Sound* const* snd, const Mips* const* mips, const Snapshot* S, const EngineParams* P) {
    for (int c = 0; c < kChannels; ++c) {
        const ChanCtl& o = ctl[(size_t) c];
        if (o.trigger) { start(c, s, snd, mips, S, P, o.inst, o.offset, o.v0, o.pan0, o.reverse); const int k = cur[(size_t) c]; if (k >= 0) { auto& x = v[(size_t) c][(size_t) k]; x.cut = x.cutTarget = o.cutoff; x.reso = o.reso; } }
        else if (o.flip) { const int k = cur[(size_t) c]; if (k >= 0 && v[(size_t) c][(size_t) k].owned) { auto& x = v[(size_t) c][(size_t) k]; x.dir = o.reverse ? -1 : 1; if (x.dir < 0) x.loop = false; } }
        if (o.cut) stopChannel(c, f2);
        if (o.release) releaseChannel(c, s);
    }
}

// read one (stereo) sample of a voice at level L (no state change)
static inline void readLevel(const Player::Voice& x, int L, double rate, float& a, float& b) {
    const float* dL; const float* dR; int n; double p;
    if (L <= 0 || !x.mips || L >= x.mips->levels) { dL = x.snd->data.data(); dR = x.snd->stereo() ? x.snd->right.data() : nullptr; n = x.len; p = x.pos; L = 0; }
    else { dL = x.mips->L[(size_t) L].data(); dR = x.mips->R[(size_t) L].empty() ? nullptr : x.mips->R[(size_t) L].data(); n = (int) x.mips->L[(size_t) L].size(); p = x.pos / (double) (1 << L); rate /= (double) (1 << L); }
    const long c = (long) std::floor(p); const double fr = p - (double) c;
    if (fr < 1e-9 && rate <= 1.0000001) { a = c >= 0 && c < n ? dL[c] : 0.0f; b = dR ? (c >= 0 && c < n ? dR[c] : 0.0f) : a; return; }
    float w[2 * HiSinc::kMaxSpan]; int first, cnt;
    if (L == 0 && rate > 1.0000001) cnt = HiSinc::weights(p, rate, w, first);   // +0..+12 st on the full sound: the kernel follows the pitch
    else { FastSinc::weights(fr, w); first = (int) (c - (FastSinc::HALF - 1)); cnt = FastSinc::TAPS; }
    float sa = 0, sb = 0;
    if (first >= 0 && first + cnt <= n) { for (int q = 0; q < cnt; ++q) sa += w[q] * dL[first + q]; if (dR) for (int q = 0; q < cnt; ++q) sb += w[q] * dR[first + q]; }
    else for (int q = 0; q < cnt; ++q) { const int i = first + q; if (i >= 0 && i < n) { sa += w[q] * dL[i]; if (dR) sb += w[q] * dR[i]; } }
    a = sa; b = dR ? sb : sa;
}
static inline int wantLevel(const Player::Voice& x, double rate) {
    if (!x.mips || x.mips->levels <= 1) return 0;
    const int maxL = x.mips->levels - 1; const int L = x.level;
    auto up = [&] { return juce::jmin(maxL, juce::jmax(1, (int) std::ceil(std::log2(rate) - 1e-9))); };
    if (L == 0) return rate >= 1.999 ? up() : 0;
    const double r = rate / (double) (1 << L);
    if (r > 1.1 && L < maxL) return up();
    if (r < 0.5) return rate < 1.9 ? 0 : up();
    return L;
}

void Player::render(int c, const ChanCtl& o, double b0, double spb, int i0, int i1, float* const* lr) {
    auto& V = v[(size_t) c];
    const float smooth = 1.0f - std::exp(-1.0f / (0.001f * (float) sr)), cutSmooth = 1.0f - std::exp(-8.0f / (0.004f * (float) sr));
    const int xfLen = juce::jmax(16, f2);
    for (int k = 0; k < 3; ++k) {
        auto& x = V[(size_t) k]; if (!x.snd) continue;
        const bool owned = x.owned && cur[(size_t) c] == k;
        float* L = lr ? lr[x.route * 2] : nullptr; float* R = lr ? lr[x.route * 2 + 1] : nullptr;
        for (int i = i0; i < i1; ++i) {
            if ((x.cnt++ & 7) == 0) {
                if (owned) {   // pitch, volume, pan, filter follow the tick's curves (refreshed every 8 samples)
                    const float u = (float) juce::jlimit(0.0, 1.0, ((b0 + i / spb) - tickStart) / tickLen);
                    const float pitch = o.pitchAt(u) + x.trackPitch;
                    x.rate = x.srcRatio * std::exp2((double) (pitch - x.root - x.tune / 100.0f) / 12.0);
                    x.target = x.ivol * o.volAt(u);
                    const float pn = juce::jlimit(-1.0f, 1.0f, x.ipan + o.pan0 + (o.pan1 - o.pan0) * u);
                    x.panL += (std::cos((pn + 1) * juce::MathConstants<float>::pi * 0.25f) - x.panL) * 0.25f; x.panR += (std::sin((pn + 1) * juce::MathConstants<float>::pi * 0.25f) - x.panR) * 0.25f;
                    x.cutTarget = o.cutoff; x.reso = o.reso;
                }
                const int want = wantLevel(x, x.rate);
                if (want != x.level) { x.prevLevel = x.level; x.level = want; x.xfade = xfLen; }
                x.cut += (x.cutTarget - x.cut) * cutSmooth;
                x.filt = x.cut < 0.995f || x.reso > 0.005f;
                if (x.filt) { const float f = 30.0f * std::pow(2.0f, x.cut * 9.4f); const float g = std::tan(juce::MathConstants<float>::pi * juce::jlimit(20.0f, (float) sr * 0.45f, f) / (float) sr);
                              x.fk = 2.0f - 1.94f * x.reso; x.fa1 = 1.0f / (1.0f + g * (g + x.fk)); x.fa2 = g * x.fa1; x.fa3 = g * x.fa2; }
            }
            const double maxRate = x.mips ? (double) (1 << (x.mips->levels - 1)) * 1.1 : 2.5;
            if (x.dir > 0 ? (!x.loop && x.pos >= x.z - 1) : (x.pos <= x.a)) { x.snd = nullptr; break; }
            float a, b; readLevel(x, x.level, x.rate, a, b);
            if (x.loop && x.dir > 0 && x.pos > x.le - x.xf) {   // sustain loop: crossfade toward the matching point one loop earlier (both paths, rate 1 too)
                const float m = (float) ((x.le - x.pos) / x.xf); Voice y = x; y.pos = x.pos - (x.le - x.ls); float a2, b2; readLevel(y, x.level, x.rate, a2, b2);
                a = a * m + a2 * (1 - m); b = b * m + b2 * (1 - m);
            }
            if (x.xfade > 0) { float pa, pb; readLevel(x, x.prevLevel, x.rate, pa, pb); const float m = (float) x.xfade / (float) xfLen; a = a * (1 - m) + pa * m; b = b * (1 - m) + pb * m; --x.xfade; }
            x.pos += x.rate * x.dir;
            if (x.loop && x.pos >= x.le) x.pos -= (x.le - x.ls);
            if (x.filt) { const float in2[2] { a, b }; float outv[2];
                for (int ch = 0; ch < 2; ++ch) { const float v3 = in2[ch] - x.f2[ch], v1 = x.fa1 * x.f1[ch] + x.fa2 * v3, v2 = x.f2[ch] + x.fa2 * x.f1[ch] + x.fa3 * v3; x.f1[ch] = 2 * v1 - x.f1[ch]; x.f2[ch] = 2 * v2 - x.f2[ch]; outv[ch] = v2; }
                a = outv[0]; b = outv[1]; }
            // envelope
            switch (x.envStage) {
                case 0: x.env += x.envAtt; if (x.env >= 1.0f) { x.env = 1.0f; x.envStage = 1; } break;
                case 1: x.env -= x.envDec; if (x.env <= x.envSus) { x.env = x.envSus; x.envStage = 2; } break;
                case 3: x.env -= x.envRel; if (x.env <= 0.0f) { x.env = 0.0f; x.envStage = 4; } break;
                default: break;
            }
            if (x.envStage == 4) { x.snd = nullptr; break; }
            if (owned) x.gain += (x.target - x.gain) * smooth;
            float g = x.gain * x.env;
            if (x.rate > maxRate) g = 0.0f;   // beyond the highest level: silent rather than aliased
            if (x.fadeIn > 0) { g *= 1.0f - (float) x.fadeIn / (float) juce::jmax(1, x.fadeInLen); --x.fadeIn; }
            if (x.fade >= 0) { g *= (float) x.fade / (float) juce::jmax(1, x.fadeLen); if (--x.fade < 0) { x.snd = nullptr; break; } }
            if (L) L[i] += a * g * x.panL; if (R) R[i] += b * g * x.panR;
        }
    }
}

void Player::process(const Song& s, const Sound* const* snd, const Mips* const* mips, const Snapshot* S, const EngineParams* P, bool songMode, int pattern,
                     double b0, double spb, int n, float* const* lr, bool running) {
    // auditions (typed notes, MIDI keys): channel kChannels, never recorded, never part of the song
    { int a, b, c2, d; const int k = audFifo.getNumReady(); audFifo.prepareToRead(k, a, b, c2, d);
      auto take = [&](const Aud& q) {
          if (q.note < 0) { releaseChannel(kChannels, s); return; }
          const int ii = juce::jlimit(0, kInstruments - 1, q.inst); const auto& in = s.inst[(size_t) ii];
          prevCtl = ChanCtl(); prevCtl.p0 = prevCtl.p1 = (float) q.note; prevCtl.v0 = prevCtl.v1 = juce::jlimit(0.0f, 1.0f, q.vel); prevCtl.cutoff = in.cutoff; prevCtl.reso = in.reso; prevCtl.active = true;
          start(kChannels, s, snd, mips, S, P, ii, 0.0f, prevCtl.v0, 0.0f, false);
          const int kk = cur[(size_t) kChannels]; if (kk >= 0) { auto& x = v[(size_t) kChannels][(size_t) kk]; x.cut = x.cutTarget = in.cutoff; x.reso = in.reso; } };
      for (int i = 0; i < b; ++i) take(audBuf[(size_t) (a + i)]); for (int i = 0; i < d; ++i) take(audBuf[(size_t) (c2 + i)]); audFifo.finishedRead(b + d); }
    auto renderAll = [&](int i0, int i1) { for (int c = 0; c < kChannels; ++c) render(c, ctl[(size_t) c], b0, spb, i0, i1, lr); };
    auto renderAud = [&] { const double ts = tickStart, tl = tickLen; tickStart = b0; tickLen = 1e9; render(kChannels, prevCtl, b0, spb, 0, n, lr); tickStart = ts; tickLen = tl; };
    if (!running || !s.on) {   // stopped / MELODY off: notes release with their instrument's RELEASE (a jump or BYPASS cuts instead)
        if (have) { have = false; seq.reset(); for (int c = 0; c < kChannels; ++c) { releaseChannel(c, s); ctl[(size_t) c] = ChanCtl(); } }
        renderAll(0, n); renderAud(); return;
    }
    int i = 0;
    while (i < n) {
        const double beat = b0 + i / spb; TickPos tp;
        if (!tickAt(s, S, songMode, pattern, beat, tp)) {
            if (have) { have = false; seq.reset(); for (int c = 0; c < kChannels; ++c) { releaseChannel(c, s); ctl[(size_t) c] = ChanCtl(); } }
            const double nb = (std::floor(beat / 4.0 + 1e-12) + 1) * 4.0;   // silence until the next bar
            const int e = juce::jlimit(i + 1, n, (int) std::ceil((nb - b0) * spb - 1e-7));
            renderAll(i, e); i = e; continue;
        }
        if (!have || !(tp.pat == last.pat && tp.origin == last.origin && tp.rowAbs == last.rowAbs && tp.tick == last.tick)) {
            if (have && !nextTick(last, tp)) { seq.reset(); for (int c = 0; c < kChannels; ++c) stopChannel(c, f2); }   // a jump (a loop restart is not: rows keep counting)
            seq.tick(s, s.pat(tp.pat), tp.row, tp.tick, tp.speed, ctl); apply(s, snd, mips, S, P);
            if (seq.tempo > 0) { tempoOut = seq.tempo; seq.tempo = 0; }
            last = tp; have = true; tickLen = s.rowBeats() / tp.speed; tickStart = tp.rowStart + tp.tick * tickLen;
        }
        const double nb = tickStart + tickLen;
        const int e = juce::jlimit(i + 1, n, (int) std::ceil((nb - b0) * spb - 1e-7));
        renderAll(i, e); i = e;
    }
    renderAud();
}

// ---------------------------------------------------------------- offline audio
juce::AudioBuffer<float> renderAudio(const Song& s0, const Sound* const* snd, const Snapshot* S, bool songMode, int pattern, double sr, double bpm, double beats, double tailSec) {
    Song s = s0; s.prepare();
    std::array<std::shared_ptr<const Mips>, kInstruments> keep; std::array<const Mips*, kInstruments> mp {};
    std::map<const Sound*, std::shared_ptr<const Mips>> made;
    for (int i = 0; i < kInstruments; ++i) if (snd && snd[i]) { auto& m = made[snd[i]]; if (!m) m = Mips::make(*snd[i]); keep[(size_t) i] = m; mp[(size_t) i] = m.get(); }
    const double spb = sr * 60.0 / juce::jmax(20.0, bpm);
    const int loopLen = (int) std::ceil(beats * spb), total = loopLen + (int) (tailSec * sr), block = 512;
    juce::AudioBuffer<float> out(2, juce::jmax(1, total)); out.clear();
    auto pl = std::make_unique<Player>(); pl->prepare(sr);
    for (int pos = 0; pos < total; pos += block) {
        const int n = juce::jmin(block, total - pos);
        float* lr[kTracks * 2]; for (int t = 0; t < kTracks; ++t) { lr[t * 2] = out.getWritePointer(0, pos); lr[t * 2 + 1] = out.getWritePointer(1, pos); }
        const bool run = pos < loopLen;
        pl->process(s, snd, mp.data(), S, nullptr, songMode, pattern, pos / spb, spb, run ? juce::jmin(n, loopLen - pos) : n, lr, run);
        if (run && loopLen - pos < n) {   // after the loop end: stop sequencing (notes release), let it ring
            float* lr2[kTracks * 2]; for (int t = 0; t < kTracks; ++t) { lr2[t * 2] = out.getWritePointer(0, loopLen); lr2[t * 2 + 1] = out.getWritePointer(1, loopLen); }
            pl->process(s, snd, mp.data(), S, nullptr, songMode, pattern, loopLen / spb, spb, n - (loopLen - pos), lr2, false);
        }
    }
    return out;
}

// ---------------------------------------------------------------- MIDI
juce::MidiFile toMidi(const Song& s0, const Snapshot* S, bool songMode, int pattern, double bpm, double beats, int bendRange) {
    Song s = s0; s.prepare();
    const int tpq = 960; bendRange = juce::jlimit(1, 48, bendRange);
    std::array<juce::MidiMessageSequence, kChannels> seqs;
    struct St { bool on = false; int note = -1, arp = 0, bend = -1, cc = -1, cut = -1, res = -1; float trig = 1; };
    std::array<St, kChannels> st {};
    Sequencer sq; std::array<ChanCtl, kChannels> ctl {}; bool have = false; TickPos last;
    for (int c = 0; c < kChannels; ++c) {
        auto& q = seqs[(size_t) c]; const int ch = c + 1;
        q.addEvent(juce::MidiMessage::textMetaEvent(3, "Ghosty Melody " + juce::String(c + 1)), 0);
        if (c == 0) { q.addEvent(juce::MidiMessage::tempoMetaEvent((int) std::llround(60000000.0 / bpm)), 0); q.addEvent(juce::MidiMessage::timeSignatureMetaEvent(4, 4), 0); }
        q.addEvent(juce::MidiMessage::controllerEvent(ch, 101, 0), 0); q.addEvent(juce::MidiMessage::controllerEvent(ch, 100, 0), 0);
        q.addEvent(juce::MidiMessage::controllerEvent(ch, 6, bendRange), 0); q.addEvent(juce::MidiMessage::controllerEvent(ch, 38, 0), 0);
        q.addEvent(juce::MidiMessage::controllerEvent(ch, 101, 127), 0); q.addEvent(juce::MidiMessage::controllerEvent(ch, 100, 127), 0);
    }
    bool used[kChannels] {};
    auto off = [&](int c, double tick) { auto& x = st[(size_t) c]; if (x.on) { seqs[(size_t) c].addEvent(juce::MidiMessage::noteOff(c + 1, x.note), tick); x.on = false; } };
    // walk every tick (the speed can change per row)
    const double rb = s.rowBeats(); double beat = 0;
    while (beat < beats - 1e-9) {
        TickPos tp; const double tick = beat * tpq;
        if (!tickAt(s, S, songMode, pattern, beat + 1e-9, tp)) {
            if (have) { for (int c = 0; c < kChannels; ++c) off(c, tick); sq.reset(); have = false; }
            beat = (std::floor(beat / 4.0 + 1e-12) + 1) * 4.0; continue;
        }
        const double tlen = rb / tp.speed;
        if (!(have && nextTick(last, tp))) { for (int c = 0; c < kChannels; ++c) off(c, tick); sq.reset(); }
        sq.tick(s, s.pat(tp.pat), tp.row, tp.tick, tp.speed, ctl); last = tp; have = true;
        for (int c = 0; c < kChannels; ++c) {
            const ChanCtl& o = ctl[(size_t) c]; auto& x = st[(size_t) c]; auto& q = seqs[(size_t) c]; const int ch = c + 1;
            const float ivol = o.inst >= 0 ? juce::jlimit(0.0f, 1.0f, s.inst[(size_t) o.inst].vol) : 1.0f;
            auto bendAt = [&](float u, double t) {
                const float d = (o.pitchAt(u) - (float) x.note) / (float) bendRange;
                const int b = juce::jlimit(0, 16383, 8192 + (int) std::lround(juce::jlimit(-1.0f, 1.0f, d) * 8191.0f));
                if (b != x.bend) { q.addEvent(juce::MidiMessage::pitchWheel(ch, b), t); x.bend = b; }
            };
            auto ccAt = [&](float vv, double t) { const int v = juce::jlimit(0, 127, (int) std::lround(127.0f * vv / juce::jmax(1.0f / 64.0f, x.trig))); if (v != x.cc) { q.addEvent(juce::MidiMessage::controllerEvent(ch, 11, v), t); x.cc = v; } };
            auto filtAt = [&](double t) { const int cu = juce::jlimit(0, 127, (int) std::lround(o.cutoff * 127)), re = juce::jlimit(0, 127, (int) std::lround(o.reso * 127));
                if (cu != x.cut) { q.addEvent(juce::MidiMessage::controllerEvent(ch, 74, cu), t); x.cut = cu; } if (re != x.res) { q.addEvent(juce::MidiMessage::controllerEvent(ch, 71, re), t); x.res = re; } };
            const int want = juce::jlimit(0, 127, (int) std::lround(o.p0 + o.arp));
            if (o.trigger || (x.on && o.active && o.arp != x.arp)) {
                const float trig = o.trigger ? o.v0 : x.trig;
                off(c, tick);
                x.note = want; x.arp = o.arp; x.trig = juce::jmax(1.0f / 64.0f, trig); x.bend = -1; x.cc = -1;
                bendAt(0.0f, tick); ccAt(o.volAt(0), tick); filtAt(tick);
                q.addEvent(juce::MidiMessage::noteOn(ch, x.note, (juce::uint8) juce::jlimit(1, 127, (int) std::lround(x.trig * ivol * 127.0f))), tick); x.on = true; used[c] = true;
            }
            if (o.cut || o.release) { off(c, tick); continue; }
            if (!x.on) continue;
            const bool moving = o.vib != 0 || o.p1 != o.p0;
            for (int k = 0; k < (moving ? 4 : 1); ++k) bendAt(k * 0.25f, tick + k * 0.25 * tlen * tpq);
            ccAt(o.volAt(0), tick); if (o.v1 != o.v0 || o.trem > 0) { ccAt(o.volAt(0.5f), tick + 0.5 * tlen * tpq); }
            filtAt(tick);
        }
        beat = tp.rowStart + (tp.tick + 1) * tlen;
    }
    for (int c = 0; c < kChannels; ++c) off(c, beats * tpq);
    juce::MidiFile mf; mf.setTicksPerQuarterNote(tpq);
    for (int c = 0; c < kChannels; ++c) if (used[c] || c == 0) { seqs[(size_t) c].sort(); seqs[(size_t) c].updateMatchedPairs(); mf.addTrack(seqs[(size_t) c]); }
    return mf;
}

// ---------------------------------------------------------------- state
juce::ValueTree toTree(const Song& s) {
    juce::ValueTree t("MELODY"); t.setProperty("on", s.on, nullptr); t.setProperty("lpb", s.lpb, nullptr); t.setProperty("speed", s.speed, nullptr);
    t.setProperty("ownorder", s.ownOrder, nullptr); { juce::StringArray a; for (auto o : s.order) a.add(juce::String((int) o)); t.setProperty("order", a.joinIntoString(","), nullptr); }
    for (int i = 0; i < kInstruments; ++i) { const auto& in = s.inst[(size_t) i]; juce::ValueTree x("INST"); x.setProperty("i", i, nullptr);
        x.setProperty("name", in.name, nullptr); x.setProperty("sound", in.sound, nullptr); x.setProperty("route", in.route, nullptr); x.setProperty("root", in.root, nullptr); x.setProperty("tune", in.tune, nullptr);
        x.setProperty("vol", in.vol, nullptr); x.setProperty("pan", in.pan, nullptr); x.setProperty("loop", in.loop, nullptr); x.setProperty("ls", in.loopStart, nullptr);
        x.setProperty("le", in.loopEnd, nullptr); x.setProperty("rel", in.release, nullptr); x.setProperty("att", in.attack, nullptr); x.setProperty("dec", in.decay, nullptr); x.setProperty("sus", in.sustain, nullptr);
        x.setProperty("start", in.start, nullptr); x.setProperty("end", in.end, nullptr); x.setProperty("cutoff", in.cutoff, nullptr); x.setProperty("reso", in.reso, nullptr); x.setProperty("fpitch", in.followPitch, nullptr);
        x.setProperty("slice", in.slice, nullptr); if (!in.slices.empty()) { juce::StringArray a; for (int v : in.slices) a.add(juce::String(v)); x.setProperty("slices", a.joinIntoString(","), nullptr); }
        if (in.src >= 0) x.setProperty("src", in.src, nullptr);
        if (in.sample) { x.setProperty("sname", in.sample->name, nullptr); x.setProperty("srate", in.sample->rate, nullptr); x.setProperty("flac", juce::var(soundToFlac(*in.sample)), nullptr); }
        t.appendChild(x, nullptr); }
    for (int i = 0; i < kPats; ++i) {
        const auto& p = s.pat(i); bool any = p.rows != 64; for (auto& ch : p.cells) for (auto& c : ch) if (!c.empty()) any = true;
        if (!any) continue;
        juce::MemoryOutputStream mo; { juce::GZIPCompressorOutputStream gz(mo, 9); gz.write(p.cells.data(), sizeof(p.cells)); gz.flush(); }
        juce::ValueTree x("MPAT"); x.setProperty("i", i, nullptr); x.setProperty("rows", p.rows, nullptr); x.setProperty("cells", mo.getMemoryBlock().toBase64Encoding(), nullptr); t.appendChild(x, nullptr);
    }
    return t;
}
void fromTree(Song& s, const juce::ValueTree& t, double rate) {
    s = Song();
    if (!t.isValid()) return;
    s.on = t.getProperty("on", false); s.lpb = juce::jlimit(1, 16, (int) t.getProperty("lpb", 4)); s.speed = juce::jlimit(1, 32, (int) t.getProperty("speed", 6));
    s.ownOrder = t.getProperty("ownorder", false);
    { s.order.clear(); for (auto& v : juce::StringArray::fromTokens(t.getProperty("order", "0").toString(), ",", "")) if ((int) s.order.size() < kOrderMax) s.order.push_back((uint8_t) juce::jlimit(0, kPats - 1, v.getIntValue())); if (s.order.empty()) s.order.push_back(0); }
    for (auto x : t) {
        if (x.hasType("INST")) {
            const int i = x.getProperty("i", -1); if (!juce::isPositiveAndBelow(i, kInstruments)) continue; auto& in = s.inst[(size_t) i];
            auto F = [&](const char* k, double d, double lo, double hi) { return (float) juce::jlimit(lo, hi, (double) x.getProperty(k, d)); };
            in.name = x.getProperty("name").toString(); in.sound = x.getProperty("sound").toString();
            in.src = juce::jlimit(-1, kTracks - 1, (int) x.getProperty("src", -1)); in.route = juce::jlimit(-1, kTracks - 1, (int) x.getProperty("route", -1));
            in.root = juce::jlimit(0, 127, (int) x.getProperty("root", 60)); in.tune = F("tune", 0, -100, 100); in.vol = F("vol", 0.8, 0, 2); in.pan = F("pan", 0, -1, 1);
            in.loop = x.getProperty("loop", false); in.loopStart = F("ls", 0.5, 0, 1); in.loopEnd = F("le", 1, 0, 1); in.release = F("rel", 0.03, 0.002, 10);
            in.attack = F("att", 0, 0, 10); in.decay = F("dec", 0, 0, 10); in.sustain = F("sus", 1, 0, 1); in.start = F("start", 0, 0, 0.99); in.end = F("end", 1, 0.01, 1);
            in.cutoff = F("cutoff", 1, 0, 1); in.reso = F("reso", 0, 0, 1); in.followPitch = x.getProperty("fpitch", true); in.slice = juce::jlimit(0, 64, (int) x.getProperty("slice", 0));
            for (auto& v : juce::StringArray::fromTokens(x.getProperty("slices").toString(), ",", "")) if (in.slices.size() < 64) in.slices.push_back(juce::jmax(0, v.getIntValue()));
            if (x.hasProperty("flac")) { const auto v = x.getProperty("flac"); const juce::String nm = x.getProperty("sname").toString();
                in.sample = v.isBinaryData() ? soundFromFlac(*v.getBinaryData(), rate, nm) : soundFromFlacBase64(v.toString().fromFirstOccurrenceOf("base64:", false, false).isNotEmpty() ? v.toString().fromFirstOccurrenceOf("base64:", false, false) : v.toString(), rate, nm); }
            if (in.sample) for (int v2 : in.slices) if (v2 >= (int) in.sample->data.size()) { in.slices.clear(); in.slice = 0; break; }
        } else if (x.hasType("MPAT")) {
            const int i = x.getProperty("i", -1); if (!juce::isPositiveAndBelow(i, kPats)) continue; auto& p = s.pat(i);
            p.rows = juce::jlimit(1, kRows, (int) x.getProperty("rows", 64));
            juce::MemoryBlock z; if (!z.fromBase64Encoding(x.getProperty("cells").toString())) continue;
            juce::MemoryInputStream mi(z, false); juce::GZIPDecompressorInputStream gz(mi); juce::MemoryOutputStream mo; mo.writeFromInputStream(gz, (juce::int64) sizeof(p.cells) + 16);
            if (mo.getDataSize() != sizeof(p.cells)) continue;
            std::memcpy(p.cells.data(), mo.getData(), sizeof(p.cells));
            for (auto& ch : p.cells) for (auto& c : ch) {   // hostile data never reaches the player out of range
                if (c.note > 128 && c.note != kNoteOff) c.note = 0; if (c.inst > kInstruments) c.inst = 0; if (c.vol > 64 && c.vol != kNoVol) c.vol = 64; if (c.fx >= FxCount) { c.fx = 0; c.param = 0; }
            }
        }
    }
}
void takeTrackSound(Instrument& in, const Track& tr, int t) {
    in.sample = tr.sample; in.sound = tr.sample ? juce::String() : tr.sound; in.slices = tr.slices; if (in.slice > (int) in.slices.size()) in.slice = 0;
    in.route = t; in.src = -1;
}
void migrate(Song& s, const std::array<Track, kTracks>& tracks) {
    for (auto& in : s.inst) if (in.src >= 0 && !in.hasSound()) { const int t = juce::jlimit(0, kTracks - 1, in.src); const int keepRoute = in.route; takeTrackSound(in, tracks[(size_t) t], t); if (keepRoute >= 0) in.route = keepRoute; }
}
void hashInto(const Song& s, const std::function<void(const void*, size_t)>& mix) {
    auto S = [&](const juce::String& x) { mix(x.toRawUTF8(), (size_t) x.getNumBytesAsUTF8()); mix("|", 1); };
    const int g[] { s.on, s.lpb, s.speed, s.ownOrder }; mix(g, sizeof(g)); if (!s.order.empty()) mix(s.order.data(), s.order.size());
    for (auto& in : s.inst) {
        S(in.name); S(in.sound); const void* sp = in.sample.get(); mix(&sp, sizeof(sp));
        const int ii[] { in.route, in.slice, in.root, in.loop, in.followPitch, in.src }; mix(ii, sizeof(ii));
        const float ff[] { in.tune, in.vol, in.pan, in.attack, in.decay, in.sustain, in.release, in.start, in.end, in.loopStart, in.loopEnd, in.cutoff, in.reso }; mix(ff, sizeof(ff));
        if (!in.slices.empty()) mix(in.slices.data(), in.slices.size() * sizeof(int));
    }
    for (int i = 0; i < kPats; ++i) { const auto& pt = s.pat(i); mix(&pt.rows, sizeof(int)); mix(pt.cells.data(), sizeof(pt.cells)); }
}

} // namespace gb::mel
