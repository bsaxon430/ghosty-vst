#pragma once
// Ghosty Beats SCRIPTS v1: a tiny, safe rule language for custom mutations, run per step and per track.
//
//   # comments start with '#'; one statement per line (or separated by ';')
//   if bar % 4 == 3 and step > 12 then retrig(4, accel=0.3)
//   if role == 3 and not hit and chance(0.2) then hit(40)
//   if prev_hit and hit then vel(vel * 0.7)
//
// Values:   numbers, + - * / %, == != < <= > >=, and or not, ( ), if/then/else
// Variables: bar step pos beat track role hit vel pitch prev_hit next_hit density len seed
//            (pos = 16th inside the bar 0..15, beat 0..3, role 1 kick 2 snare 3 hat 4 perc 5 808 6 open hat 7 break)
// Functions: chance(p) rand() min(a,b) max(a,b) abs(x) floor(x) every(n) (= step % n == 0)
// Actions:  hit(vel=100) mute() vel(v) accent(x=0.3) pitch(semis) retrig(n, rate=16, accel=0, ramp=0, vramp=0)
//           reverse() offset(pct) cut(pct) nudge(pct) prob(pct) flam(ms) decay(pct) slice(n) destroy(pct) freeze(ms) frev(mask)
//
// Safety: no loops, no file / OS access; compiled once on the message thread into bytecode, run on the audio thread with a
// fixed stack and an operation budget (no allocation). chance()/rand() are seeded from the pattern seed, bar, step and track,
// so the same project gives the same result every time.
#include "BeatModel.h"

namespace gb::script {

enum class Op : uint8_t { Push, Load, Neg, Not, Add, Sub, Mul, Div, Mod, Eq, Ne, Lt, Le, Gt, Ge, And, Or, Func, Act, Jz, Jmp, Pop };
struct Ins { Op op; uint8_t a = 0, b = 0; int c = 0; double v = 0; };

inline const juce::StringArray& varNames() { static juce::StringArray v { "bar", "step", "pos", "beat", "track", "role", "hit", "vel", "pitch", "prev_hit", "next_hit", "density", "len", "seed" }; return v; }
enum Var { VBar, VStep, VPos, VBeat, VTrack, VRole, VHit, VVel, VPitch, VPrev, VNext, VDensity, VLen, VSeed, VCount };
inline const juce::StringArray& funcNames() { static juce::StringArray f { "chance", "rand", "min", "max", "abs", "floor", "every" }; return f; }
inline int funcArgs(int f) { static const int a[] { 1, 0, 2, 2, 1, 1, 1 }; return a[f]; }

struct ActDef { const char* name; std::vector<std::pair<const char*, double>> params; };   // name, (param, default) - NAN = required
inline const std::vector<ActDef>& actions() {
    static const std::vector<ActDef> a {
        { "hit", { { "vel", 100 } } }, { "mute", {} }, { "vel", { { "v", NAN } } }, { "accent", { { "x", 0.3 } } }, { "pitch", { { "semis", NAN } } },
        { "retrig", { { "n", NAN }, { "rate", 16 }, { "accel", 0 }, { "ramp", 0 }, { "vramp", 0 } } }, { "reverse", {} }, { "offset", { { "pct", NAN } } },
        { "cut", { { "pct", NAN } } }, { "nudge", { { "pct", NAN } } }, { "prob", { { "pct", NAN } } }, { "flam", { { "ms", 18 } } }, { "decay", { { "pct", NAN } } },
        { "slice", { { "n", NAN } } }, { "destroy", { { "pct", 50 } } }, { "freeze", { { "ms", 300 } } }, { "frev", { { "mask", 1 } } } };
    return a;
}

struct Program {
    std::vector<Ins> code; juce::String error; int errLine = 0, errCol = 0;
    bool ok() const { return error.isEmpty(); }
};

// ---------------- compiler (message thread)
class Compiler {
public:
    static constexpr int kMaxOps = 4000, kMaxDepth = 64, kMaxChars = 16384;
    static std::shared_ptr<Program> compile(const juce::String& src) {
        auto p = std::make_shared<Program>();
        if (src.length() > kMaxChars) { p->error = "script too long (16 kB max)"; p->errLine = 1; p->errCol = 1; return p; }
        Compiler c(src, *p);
        try { c.lex(); c.program(); } catch (const Err& e) { p->code.clear(); p->error = e.msg; p->errLine = e.line; p->errCol = e.col; }
        if (p->code.size() > kMaxOps && p->ok()) { p->code.clear(); p->error = "script too long (" + juce::String(kMaxOps) + " operations max)"; }
        return p;
    }
private:
    struct Tok { int kind = 0; juce::String s; double v = 0; int line = 1, col = 1; };   // 0 end, 1 num, 2 ident, 3 sym, 4 newline
    struct Err { juce::String msg; int line, col; };
    juce::String src; Program& P; std::vector<Tok> t; size_t i = 0; int depth = 0;
    struct Depth { Compiler& c; Depth(Compiler& cc) : c(cc) { if (++c.depth > kMaxDepth) c.fail("expression too deep (64 levels max)", c.cur()); } ~Depth() { --c.depth; } };   // bounded recursion
    Compiler(const juce::String& s, Program& p) : src(s), P(p) {}
    [[noreturn]] void fail(const juce::String& m, const Tok& k) { throw Err { m, k.line, k.col }; }
    void lex() {
        int line = 1, col = 1; auto s = src.toStdString(); size_t k = 0;
        auto push = [&](int kind, juce::String str, double v, int l, int c) { t.push_back({ kind, str, v, l, c }); };
        while (k < s.size()) {
            const char ch = s[k];
            if (ch == '#') { while (k < s.size() && s[k] != '\n') ++k; continue; }
            if (ch == '\n' || ch == ';') { push(4, "", 0, line, col); ++k; if (ch == '\n') { ++line; col = 1; } else ++col; continue; }
            if (ch == ' ' || ch == '\t' || ch == '\r') { ++k; ++col; continue; }
            const int l0 = line, c0 = col;
            if (std::isdigit((unsigned char) ch) || (ch == '.' && k + 1 < s.size() && std::isdigit((unsigned char) s[k + 1]))) {
                size_t e = k; while (e < s.size() && (std::isdigit((unsigned char) s[e]) || s[e] == '.')) ++e;
                push(1, "", juce::String(s.substr(k, e - k)).getDoubleValue(), l0, c0); col += (int) (e - k); k = e; continue;   // locale independent
            }
            if (std::isalpha((unsigned char) ch) || ch == '_') {
                size_t e = k; while (e < s.size() && (std::isalnum((unsigned char) s[e]) || s[e] == '_')) ++e;
                push(2, juce::String(s.substr(k, e - k)).toLowerCase(), 0, l0, c0); col += (int) (e - k); k = e; continue;
            }
            static const char* two[] { "==", "!=", "<=", ">=" };
            bool done = false; for (auto* tw : two) if (k + 1 < s.size() && s[k] == tw[0] && s[k + 1] == tw[1]) { push(3, tw, 0, l0, c0); k += 2; col += 2; done = true; break; }
            if (done) continue;
            if (juce::String("+-*/%<>(),=").containsChar(ch)) { push(3, juce::String::charToString(ch), 0, l0, c0); ++k; ++col; continue; }
            throw Err { "unexpected character '" + juce::String::charToString(ch) + "'", l0, c0 };
        }
        t.push_back({ 0, "", 0, line, col });
    }
    const Tok& cur() const { return t[i]; }
    bool isSym(const char* s) const { return cur().kind == 3 && cur().s == s; }
    bool isWord(const char* s) const { return cur().kind == 2 && cur().s == s; }
    void expectSym(const char* s) { if (!isSym(s)) fail("expected '" + juce::String(s) + "'", cur()); ++i; }
    void emit(Op op, double v = 0, int a = 0, int b = 0) { P.code.push_back({ op, (uint8_t) a, (uint8_t) b, 0, v }); }
    void program() {
        while (cur().kind != 0) {
            if (cur().kind == 4) { ++i; continue; }
            statement();
            if (cur().kind != 4 && cur().kind != 0) fail("expected the end of the line", cur());
        }
    }
    void statement() {
        if (isWord("if")) {
            ++i; expr();
            if (!isWord("then")) fail("expected 'then'", cur()); ++i;
            const size_t jz = P.code.size(); emit(Op::Jz);
            actionList();
            if (isWord("else")) { ++i; const size_t jmp = P.code.size(); emit(Op::Jmp); P.code[jz].c = (int) P.code.size(); actionList(); P.code[jmp].c = (int) P.code.size(); }
            else P.code[jz].c = (int) P.code.size();
        } else actionList();
    }
    void actionList() { action(); while (isSym(",")) { ++i; action(); } }
    void action() {
        if (cur().kind != 2) fail("expected an action like retrig(4) or vel(90)", cur());
        const auto name = cur().s; const auto at = cur(); int id = -1; for (int k = 0; k < (int) actions().size(); ++k) if (name == actions()[(size_t) k].name) id = k;
        if (id < 0) fail("unknown action '" + name + "'", at);
        ++i; const auto& def = actions()[(size_t) id];
        std::vector<std::vector<Ins>> args(def.params.size()); std::vector<bool> given(def.params.size(), false);
        if (isSym("(")) {
            ++i; int pos = 0;
            while (!isSym(")")) {
                int slot = pos;
                if (cur().kind == 2 && i + 1 < t.size() && t[i + 1].kind == 3 && t[i + 1].s == "=") {
                    slot = -1; for (int k = 0; k < (int) def.params.size(); ++k) if (cur().s == def.params[(size_t) k].first) slot = k;
                    if (slot < 0) fail(name + "() has no '" + cur().s + "'", cur()); i += 2;
                } else ++pos;
                if (slot >= (int) def.params.size()) fail(name + "() takes " + juce::String((int) def.params.size()) + " value(s)", cur());
                const size_t start = P.code.size(); expr(); args[(size_t) slot].assign(P.code.begin() + (long) start, P.code.end()); P.code.resize(start); given[(size_t) slot] = true;
                if (isSym(",")) ++i; else if (!isSym(")")) fail("expected ',' or ')'", cur());
            }
            ++i;
        }
        for (size_t k = 0; k < def.params.size(); ++k) {
            if (given[k]) P.code.insert(P.code.end(), args[k].begin(), args[k].end());
            else if (std::isnan(def.params[k].second)) fail(name + "() needs '" + def.params[k].first + "'", at);
            else emit(Op::Push, def.params[k].second);
        }
        emit(Op::Act, 0, id, (int) def.params.size());
    }
    void expr() { andExpr(); while (isWord("or")) { ++i; andExpr(); emit(Op::Or); } }
    void andExpr() { notExpr(); while (isWord("and")) { ++i; notExpr(); emit(Op::And); } }
    void notExpr() { if (isWord("not")) { Depth d(*this); ++i; notExpr(); emit(Op::Not); } else cmp(); }
    void cmp() {
        add();
        static const std::pair<const char*, Op> ops[] { { "==", Op::Eq }, { "!=", Op::Ne }, { "<=", Op::Le }, { ">=", Op::Ge }, { "<", Op::Lt }, { ">", Op::Gt } };
        for (auto& o : ops) if (isSym(o.first)) { ++i; add(); emit(o.second); return; }
        if (isSym("=")) fail("use '==' to compare", cur());
    }
    void add() { mul(); for (;;) { if (isSym("+")) { ++i; mul(); emit(Op::Add); } else if (isSym("-")) { ++i; mul(); emit(Op::Sub); } else break; } }
    void mul() { unary(); for (;;) { if (isSym("*")) { ++i; unary(); emit(Op::Mul); } else if (isSym("/")) { ++i; unary(); emit(Op::Div); } else if (isSym("%")) { ++i; unary(); emit(Op::Mod); } else break; } }
    void unary() { if (isSym("-")) { Depth d(*this); ++i; unary(); emit(Op::Neg); } else primary(); }
    void primary() {
        const auto k = cur();
        if (k.kind == 1) { ++i; emit(Op::Push, k.v); return; }
        if (isSym("(")) { Depth d(*this); ++i; expr(); expectSym(")"); return; }
        if (k.kind == 2) {
            if (k.s == "true") { ++i; emit(Op::Push, 1); return; } if (k.s == "false") { ++i; emit(Op::Push, 0); return; }
            const int v = varNames().indexOf(k.s); if (v >= 0) { ++i; emit(Op::Load, 0, v); return; }
            const int f = funcNames().indexOf(k.s);
            if (f >= 0) { ++i; expectSym("("); int n = 0; while (!isSym(")")) { expr(); ++n; if (isSym(",")) ++i; else if (!isSym(")")) fail("expected ',' or ')'", cur()); } ++i;
                if (n != funcArgs(f)) fail(k.s + "() takes " + juce::String(funcArgs(f)) + " value(s)", k); emit(Op::Func, 0, f, n); return; }
            for (auto& a : actions()) if (k.s == a.name) fail("'" + k.s + "' is an action: use it after 'then'", k);
            fail("unknown name '" + k.s + "'", k);
        }
        fail(k.kind == 0 || k.kind == 4 ? juce::String("the line ends too early") : "unexpected '" + k.s + "'", k);
    }
};

// ---------------- runtime (audio thread safe: fixed stack, budget, no allocation)
struct Env { double var[VCount] {}; uint32_t seed = 1; long sAbs = 0; int track = 0, scriptIdx = 0; };
inline float rnd(const Env& e, int k) {
    uint32_t x = (uint32_t) (e.sAbs * 2654435761L) ^ (uint32_t) (e.track * 40503 + e.scriptIdx * 7919 + k * 104729) ^ (e.seed * 0x9e3779b9u);
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return (x & 0xffffff) / 16777216.0f;
}
inline void applyAction(int id, const double* a, Step& st) {
    auto c = [](double v, double lo, double hi) { return std::isfinite(v) ? juce::jlimit(lo, hi, v) : lo; };
    switch (id) {
        case 0: if (!st.vel) { st = Step(); st.vel = (uint8_t) c(a[0], 1, 127); } break;
        case 1: st.vel = 0; break;
        case 2: st.vel = (uint8_t) c(a[0], 0, 127); break;
        case 3: if (st.vel) st.vel = (uint8_t) c(st.vel * (1 + a[0]), 1, 127); break;
        case 4: st.pitch = (int8_t) c(std::round(a[0]), -24, 24); break;
        case 5: st.roll = (uint8_t) c(std::round(a[0]), 1, 16); st.rate = (uint8_t) c(std::round(a[1]), 0, 64); st.accel = (int8_t) c(std::round(a[2] * (std::abs(a[2]) <= 1 ? 100 : 1)), -100, 100);
                st.pitchRamp = (int8_t) c(std::round(a[3]), -24, 24); st.velRamp = (int8_t) c(std::round(a[4]), -100, 100); break;
        case 6: st.flags ^= 1; break;
        case 7: st.offset = (uint8_t) c(a[0], 0, 99); break;
        case 8: st.cut = (uint8_t) c(a[0], 0, 100); break;
        case 9: st.nudge = (int8_t) c(a[0], -50, 50); break;
        case 10: st.prob = (uint8_t) c(a[0], 1, 100); break;
        case 11: st.flam = (uint8_t) c(a[0], 0, 40); break;
        case 12: st.decay = (uint8_t) c(a[0], 5, 100); break;
        case 13: st.slice = (uint8_t) c(std::round(a[0]), 0, 64); break;
        case 14: st.destroy = (uint8_t) c(a[0], 0, 100); break;
        case 15: st.freeze = (uint8_t) c(a[0] / 10.0, 1, 255); break;
        case 16: st.frev = (uint8_t) c(std::round(a[0]), 0, 7); break;
        default: break;
    }
}
// runs the program on one step; returns false if it hit the budget (the step is then left as it was)
inline bool run(const Program& p, Env& e, Step& st) {
    if (!p.ok() || p.code.empty()) return true;
    double stack[72]; int sp = 0, budget = 8192, rcount = 0; Step work = st;   // the budget is above the compile cap, so every script that compiles runs
    for (int pc = 0; pc < (int) p.code.size();) {
        if (--budget < 0 || sp > 60) return false;
        const auto& in = p.code[(size_t) pc++];
        auto pop = [&] { return sp > 0 ? stack[--sp] : 0.0; };
        switch (in.op) {
            case Op::Push: stack[sp++] = in.v; break;
            case Op::Load: stack[sp++] = in.a == VVel ? work.vel : (in.a == VHit ? (work.vel ? 1.0 : 0.0) : (in.a == VPitch ? work.pitch : e.var[in.a])); break;
            case Op::Neg: stack[sp - 1] = -stack[sp - 1]; break;
            case Op::Not: stack[sp - 1] = stack[sp - 1] != 0 ? 0 : 1; break;
            case Op::Pop: --sp; break;
            case Op::Jz: { const double v = pop(); if (v == 0) pc = in.c; } break;
            case Op::Jmp: pc = in.c; break;
            case Op::Func: {
                double a[2] {}; for (int k = in.b - 1; k >= 0; --k) a[k] = pop(); double r = 0;
                switch (in.a) { case 0: { const double pr = a[0] > 1 ? a[0] / 100.0 : a[0]; r = rnd(e, ++rcount) < pr ? 1 : 0; } break; case 1: r = rnd(e, ++rcount); break;
                                case 2: r = juce::jmin(a[0], a[1]); break; case 3: r = juce::jmax(a[0], a[1]); break; case 4: r = std::abs(a[0]); break; case 5: r = std::floor(a[0]); break;
                                case 6: r = a[0] >= 1 && std::fmod(e.var[VStep], std::floor(a[0])) == 0 ? 1 : 0; break; default: break; }
                stack[sp++] = r; } break;
            case Op::Act: { double a[8] {}; for (int k = in.b - 1; k >= 0; --k) a[k] = pop(); applyAction(in.a, a, work); } break;
            default: {
                const double b = pop(), a = pop(); double r = 0;
                switch (in.op) {
                    case Op::Add: r = a + b; break; case Op::Sub: r = a - b; break; case Op::Mul: r = a * b; break; case Op::Div: r = b != 0 ? a / b : 0; break;
                    case Op::Mod: r = b != 0 ? a - b * std::floor(a / b) : 0; break;
                    case Op::Eq: r = std::abs(a - b) < 1e-9; break; case Op::Ne: r = std::abs(a - b) >= 1e-9; break; case Op::Lt: r = a < b; break; case Op::Le: r = a <= b; break;
                    case Op::Gt: r = a > b; break; case Op::Ge: r = a >= b; break; case Op::And: r = (a != 0 && b != 0); break; case Op::Or: r = (a != 0 || b != 0); break;
                    default: break;
                }
                stack[sp++] = r;
            }
        }
    }
    st = work; return true;
}

// the 6 modules that ship with the plugin
struct Module { const char* name; const char* code; };
inline const std::vector<Module>& examples() {
    static const std::vector<Module> m {
        { "Turnaround rolls", "# last 4 steps of every 4th bar: snares and hats roll faster and faster\nif bar % 4 == 3 and step > 12 and (role == 2 or role == 3) then retrig(4, accel=0.3, vramp=40)\n" },
        { "Ghost answers", "# a soft snare ghost right after a kick, sometimes\nif role == 2 and not hit and prev_hit == 0 and pos % 4 == 1 and chance(0.35) then hit(32)\n" },
        { "Hat breathing", "# hats get louder through the bar, quieter off the beat\nif role == 3 and hit then vel(min(127, vel * (0.7 + pos / 40)))\nif role == 3 and hit and pos % 2 == 1 then accent(-0.25)\n" },
        { "Glitch tail", "# every 8th bar the last beat glitches: short retrigs, cuts and reverses\nif bar % 8 == 7 and beat == 3 and hit then retrig(3, rate=32, ramp=7), cut(60)\nif bar % 8 == 7 and beat == 3 and chance(0.3) then reverse()\n" },
        { "Sparse breakdown", "# bars 13-16 of 16: only kicks on the beat survive, everything else thins out\nif bar % 16 >= 12 and role != 1 and chance(0.6) then mute()\nif bar % 16 >= 12 and role == 1 and pos % 4 != 0 then mute()\n" },
        { "Freeze accents", "# loud snares on the backbeat freeze into a held spectral tail every 4th bar\nif role == 2 and vel >= 110 and bar % 4 == 3 then freeze(400)\nif role == 4 and hit and chance(0.2) then frev(4)\n" } };
    return m;
}

// THIS BEAT (Brandon 2026-11-21): the pattern on the SEQUENCER written out as a script, so the SCRIPTS page shows the beat that's playing.
// Every track gets a picture of its steps (X accent, x hit, - soft, . rest, | every beat) and the rules that play exactly those hits:
// steps with the same settings share one line. (step counts from 0, track from 1, like everywhere in the script language.)
inline juce::String beatAsScript(const Model& m, int patIndex, const juce::String& patName) {
    const auto& p = m.patterns[(size_t) juce::jlimit(0, kPatterns - 1, patIndex)];
    juce::String out; int used = 0;
    out << "# THIS BEAT: pattern " << patName << ", " << p.length << " steps, " << resNames()[juce::jlimit(0, resNames().size() - 1, p.res)] << "\n"
        << "# the beat on the SEQUENCER as a script: change the pattern and this changes; change this and the pattern changes\n"
        << "# edit a picture (X accent  x hit  - soft  . rest  | beat) or the rules; step counts from 0\n";
    for (int t = 0; t < kTracks; ++t) {
        const int len = p.lenOf(t); const auto& row = p.steps[(size_t) t];
        int hits = 0; for (int i = 0; i < len; ++i) if (row[(size_t) i].vel) ++hits;
        const auto& tr = m.tracks[(size_t) t];
        if (hits == 0 && tr.sound.isEmpty()) continue; if (hits) ++used;
        out << "\n# " << juce::String(t + 1).paddedLeft('0', 2) << " " << (tr.sound.isNotEmpty() ? tr.sound : juce::String("(empty)"))
            << (len != p.length ? "  [" + juce::String(len) + " steps]" : juce::String()) << (tr.mute ? "  (muted)" : "") << "\n";
        for (int a = 0; a < len; a += 16) {   // the picture, 16 steps a line
            juce::String pic; for (int i = a; i < juce::jmin(len, a + 16); ++i) { if (i > a && i % 4 == 0) pic << "| ";
                const int v = row[(size_t) i].vel; pic << (v == 0 ? "." : v >= 120 ? "X" : v < 60 ? "-" : "x") << " "; }
            out << "#   " << pic.trimEnd() << "\n";
        }
        std::vector<std::pair<juce::String, juce::Array<int>>> groups;   // same actions -> one rule
        for (int i = 0; i < len; ++i) {
            const auto& st = row[(size_t) i]; if (! st.vel) continue;
            juce::String act; act << "hit(" << (int) st.vel << ")";
            if (st.prob < 100) act << ", prob(" << (int) st.prob << ")";
            if (st.pitch != 0) act << ", pitch(" << (int) st.pitch << ")";
            if (st.roll > 1 || st.rate || st.accel || st.pitchRamp || st.velRamp) { act << ", retrig(" << (int) st.roll << ", rate=" << (int) st.rate;
                if (st.accel) act << ", accel=" << juce::String(st.accel / 100.0, 2); if (st.pitchRamp) act << ", ramp=" << (int) st.pitchRamp; if (st.velRamp) act << ", vramp=" << (int) st.velRamp; act << ")"; }
            if (st.flam) act << ", flam(" << (int) st.flam << ")";
            if (st.nudge) act << ", nudge(" << (int) st.nudge << ")";
            if (st.offset) act << ", offset(" << (int) st.offset << ")";
            if (st.cut) act << ", cut(" << (int) st.cut << ")";
            if (st.decay != 100) act << ", decay(" << (int) st.decay << ")";
            if (st.slice) act << ", slice(" << (int) st.slice << ")";
            if (st.destroy) act << ", destroy(" << (int) st.destroy << ")";
            if (st.freeze) act << ", freeze(" << (int) st.freeze * 10 << ")";
            if (st.frev) act << ", frev(" << (int) st.frev << ")";
            if (st.flags & 1) act << ", reverse()";
            bool found = false; for (auto& g : groups) if (g.first == act) { g.second.add(i); found = true; break; }
            if (! found) groups.push_back({ act, juce::Array<int> { i } });
        }
        for (auto& g : groups)
            for (int a = 0; a < g.second.size(); a += 4) {   // 4 steps a line, so every line fits on the page
                juce::String cond; const int n = juce::jmin(4, g.second.size() - a);
                for (int k = 0; k < n; ++k) cond << (k ? " or " : "") << "step == " << g.second[a + k];
                out << "if track == " << (t + 1) << " and " << (n > 1 ? "(" + cond + ")" : cond) << " then " << g.first << "\n";
            }
    }
    if (used == 0) out << "\n# (no hits yet: type x's into a picture above, or put steps in on the SEQUENCER)\n";
    return out;
}

// ...and back (Brandon 2026-11-21: edit the beat in the script and the SEQUENCER follows). The script is run on an empty pattern
// (every track, every step) and what it hits becomes the pattern; then any picture line you changed wins over the rules for those steps.
// Settings a script can't write (fine pitch, LOCKs) stay on steps that are still hits. Returns false (pattern untouched) on an error.
inline bool beatFromScript(const juce::String& text, Pattern& p, const std::array<uint8_t, kTracks>& roles, juce::String& err) {
    auto prog = Compiler::compile(text);
    if (! prog->ok()) { err = "line " + juce::String(prog->errLine) + ": " + prog->error; return false; }
    std::array<std::array<Step, kMaxSteps>, kTracks> res {};
    const double sb = stepBeats(p.res);
    for (int t = 0; t < kTracks; ++t) for (int i = 0; i < p.lenOf(t); ++i) {
        Env e; e.seed = (uint32_t) p.seed; e.sAbs = i; e.track = t; const double q = std::fmod(i * sb, 4.0);
        e.var[VBar] = std::floor(i * sb / 4.0); e.var[VStep] = i; e.var[VPos] = std::floor(q / 0.25 + 1e-9); e.var[VBeat] = std::floor(q + 1e-9);
        e.var[VTrack] = t + 1; e.var[VRole] = roles[(size_t) t]; e.var[VLen] = p.lenOf(t); e.var[VSeed] = p.seed;
        Step st; if (! run(*prog, e, st)) { err = "the script is too long to run"; return false; }
        res[(size_t) t][(size_t) i] = st;
    }
    // pictures: "# NN name" then "#   x . . . | ..." lines
    auto cls = [](int v) { return v == 0 ? '.' : v >= 120 ? 'X' : v < 60 ? '-' : 'x'; };
    int cur = -1; juce::String pic; std::vector<std::pair<int, juce::String>> pics;
    auto flush = [&] { if (cur >= 0 && pic.isNotEmpty()) pics.push_back({ cur, pic }); pic.clear(); };
    for (auto line : juce::StringArray::fromLines(text)) {
        line = line.trimEnd(); if (! line.startsWith("#")) { flush(); cur = -1; continue; }
        const auto body = line.substring(1);
        const auto head = body.trimStart();
        if (body.startsWith(" ") && head.length() >= 2 && juce::CharacterFunctions::isDigit(head[0]) && juce::CharacterFunctions::isDigit(head[1]) && (head.length() == 2 || head[2] == ' ')) {
            flush(); cur = head.substring(0, 2).getIntValue() - 1; if (! juce::isPositiveAndBelow(cur, kTracks)) cur = -1; continue; }
        if (cur >= 0 && body.startsWith("   ") && head.isNotEmpty() && head.containsOnly("Xx-.| ")) { pic << head.removeCharacters("| "); continue; }
        flush(); cur = -1;
    }
    flush();
    for (auto& [t, pc] : pics) for (int i = 0; i < juce::jmin(pc.length(), p.lenOf(t)); ++i) {
        auto& st = res[(size_t) t][(size_t) i]; const auto& old = p.steps[(size_t) t][(size_t) i]; const auto c = (char) pc[i];
        if (c == cls(old.vel) || c == cls(st.vel)) continue;   // the picture wasn't changed here (or says what the rules say): the rules win
        if (c == '.') st = Step();
        else { if (! st.vel) st = old.vel ? old : Step();
               if (cls(st.vel) != c) st.vel = (uint8_t) (c == 'X' ? 127 : c == '-' ? 40 : 100); }
    }
    for (int t = 0; t < kTracks; ++t) for (int i = 0; i < p.lenOf(t); ++i) {
        auto& old = p.steps[(size_t) t][(size_t) i]; auto st = res[(size_t) t][(size_t) i];
        if (st.vel) { st.cents = old.vel ? old.cents : 0; st.flags = (uint8_t) ((st.flags & 1) | (old.vel ? (old.flags & ~1) : 0)); }
        old = st.vel ? st : Step();
    }
    return true;
}

} // namespace gb::script
