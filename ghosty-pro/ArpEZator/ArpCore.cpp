// Port of theory.js (chord side). Keep the order of every random draw, every short-circuit and every
// floating-point expression exactly as in the JS: the tests compare against the web version's output.
#include "ArpCore.h"
#include <algorithm>
#if defined (__GNUC__) || defined (__clang__)
 #pragma GCC diagnostic ignored "-Wfloat-equal"   // exact comparisons on purpose: same semantics as the JS original
#endif
#include <cmath>
#include <functional>
#include <map>
#include <regex>
#include <set>

namespace arp
{
// ---------------------------------------------------------------- helpers
double Rng::operator()()
{
    s = s + 0x6D2B79F5u;
    uint32_t t = s;
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return (double) (t ^ (t >> 14)) / 4294967296.0;
}

double jsRound (double x)
{
    if (! std::isfinite (x)) return x;
    const double f = std::floor (x);
    return (x - f >= 0.5) ? f + 1.0 : f;
}

static std::u16string utf8to16 (const std::string& s)
{
    std::u16string o; size_t i = 0;
    while (i < s.size())
    {
        uint32_t c = (unsigned char) s[i];
        int n = 0;
        if (c < 0x80) n = 0; else if ((c >> 5) == 6) { c &= 0x1f; n = 1; } else if ((c >> 4) == 14) { c &= 0x0f; n = 2; } else if ((c >> 3) == 30) { c &= 0x07; n = 3; } else { ++i; continue; }
        ++i;
        for (int k = 0; k < n && i < s.size(); ++k, ++i) c = (c << 6) | ((unsigned char) s[i] & 0x3f);
        if (c >= 0x10000) { c -= 0x10000; o.push_back ((char16_t) (0xD800 + (c >> 10))); o.push_back ((char16_t) (0xDC00 + (c & 0x3ff))); }
        else o.push_back ((char16_t) c);
    }
    return o;
}

uint32_t hashStr (const std::u16string& s)
{
    uint32_t h = 2166136261u;
    for (char16_t c : s) { h ^= (uint32_t) c; h *= 16777619u; }
    return h;
}
uint32_t hashStr (const std::string& s) { return hashStr (utf8to16 (s)); }

bool JMap::isIndex (const std::string& k)
{
    if (k.empty() || k.size() > 9) return false;
    if (k.size() > 1 && k[0] == '0') return false;
    for (char c : k) if (c < '0' || c > '9') return false;
    return true;
}
double* JMap::find (const std::string& k) { for (auto& p : kv) if (p.first == k) return &p.second; return nullptr; }
const double* JMap::find (const std::string& k) const { for (auto& p : kv) if (p.first == k) return &p.second; return nullptr; }
void JMap::set (const std::string& k, double v)
{
    if (auto* p = find (k)) { *p = v; return; }
    if (isIndex (k))
    {
        const long n = std::stol (k);
        size_t pos = 0;
        while (pos < kv.size() && isIndex (kv[pos].first) && std::stol (kv[pos].first) < n) ++pos;
        kv.insert (kv.begin() + (long) pos, { k, v });
    }
    else kv.push_back ({ k, v });
}

std::string pickW (Rng& R, const JMap& table)
{
    double tot = 0;
    for (auto& p : table.kv) tot += std::max (0.0, p.second);
    double u = R() * tot;
    for (auto& p : table.kv) { u -= std::max (0.0, p.second); if (u < 0) return p.first; }
    return table.kv.empty() ? std::string() : table.kv[0].first;
}

JMap normH (const JMap& h)
{
    double t = 0; for (auto& p : h.kv) t += p.second;
    JMap o; if (t == 0) return o;
    for (auto& p : h.kv) { const double v = jsRound (p.second / t * 100) / 100; if (v >= 0.01) o.set (p.first, v); }
    return o;
}

static std::vector<std::string> split (const std::string& s)
{
    std::vector<std::string> o; std::string cur;
    for (char c : s) { if (c == ' ') { o.push_back (cur); cur.clear(); } else cur += c; }
    o.push_back (cur); return o;
}
static bool contains (const std::vector<int>& v, int x) { return std::find (v.begin(), v.end(), x) != v.end(); }
static bool containsS (const std::vector<std::string>& v, const std::string& x) { return std::find (v.begin(), v.end(), x) != v.end(); }

// ---------------------------------------------------------------- scales
const std::vector<std::string>& scaleNames() { static const std::vector<std::string> n { "minor", "major", "dorian", "phrygian", "lydian", "mixolydian", "harmonic" }; return n; }
const std::vector<int>& scaleOf (const std::string& name)
{
    static const std::map<std::string, std::vector<int>> S {
        { "minor", { 0, 2, 3, 5, 7, 8, 10 } }, { "major", { 0, 2, 4, 5, 7, 9, 11 } }, { "dorian", { 0, 2, 3, 5, 7, 9, 10 } }, { "phrygian", { 0, 1, 3, 5, 7, 8, 10 } },
        { "lydian", { 0, 2, 4, 6, 7, 9, 11 } }, { "mixolydian", { 0, 2, 4, 5, 7, 9, 10 } }, { "harmonic", { 0, 2, 3, 5, 7, 8, 11 } } };
    auto it = S.find (name); return it != S.end() ? it->second : S.at ("minor");
}
std::string scaleLabel (const std::string& s)
{
    static const std::map<std::string, std::string> L { { "minor", "Minor" }, { "major", "Major" }, { "dorian", "Dorian" }, { "phrygian", "Phrygian" }, { "lydian", "Lydian" }, { "mixolydian", "Mixolydian" }, { "harmonic", "Harmonic minor" } };
    auto it = L.find (s); return it != L.end() ? it->second : s;
}
std::string scaleFeel (const std::string& s)
{
    static const std::map<std::string, std::string> L { { "minor", "emotional" }, { "major", "happy" }, { "dorian", "cool" }, { "phrygian", "dark" }, { "lydian", "dreamy" }, { "mixolydian", "bright" }, { "harmonic", "drama" } };
    auto it = L.find (s); return it != L.end() ? it->second : s;
}
bool minorish (const std::string& s) { return s == "minor" || s == "dorian" || s == "phrygian" || s == "harmonic"; }
static const char* SHARP[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static const char* FLAT[] { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
static bool useFlats (int key, const std::string& scale)
{
    static const std::vector<int> mf { 2, 7, 0, 5, 10, 3, 8 }, Mf { 5, 10, 3, 8, 1, 6 };
    return minorish (scale) ? contains (mf, key) : contains (Mf, key);
}
std::string noteName (int pc, int key, const std::string& scale)
{
    const int p = mod12 (pc);
    if (scale == "phrygian" && p == (key + 1) % 12) return FLAT[p];
    return (useFlats (key, scale) && ! (minorish (scale) && p == (key + 11) % 12)) ? FLAT[p] : SHARP[p];
}
std::string keyName (int key, const std::string& scale)
{
    return minorish (scale) ? std::string ((useFlats (key, scale) ? FLAT : SHARP)[key]) + "m" : std::string ((useFlats (key, scale) ? FLAT : SHARP)[key]);
}

std::string chordSuffix (const std::vector<int>& iv)
{
    std::set<int> s; for (int x : iv) s.insert (mod12 (x));
    auto has = [&] (int x) { return s.count (x) > 0; };
    auto any = [&] (int pc) { for (int x : iv) if (x >= 12 && mod12 (x) == pc) return true; return false; };
    const bool maj3 = has (4), min3 = has (3) && ! has (4), p5 = has (7), d5 = has (6) && ! has (7), a5 = has (8) && ! has (7) && maj3;
    const bool m7 = has (10), M7 = has (11), dim7 = has (9) && d5 && min3 && ! m7;
    const bool n9 = any (2) || (has (2) && (maj3 || min3)), b9 = has (1), e11 = any (5) || (has (5) && (maj3 || min3));
    const bool s11 = any (6) && p5, six = has (9) && ! dim7 && ! m7 && ! M7;
    if (! maj3 && ! min3)
    {
        std::string b = has (2) && has (5) ? "sus2sus4" : has (5) ? "sus4" : has (2) ? "sus2" : p5 ? "5" : "";
        if (m7) b = (has (2) && ! has (5) ? std::string ("9sus") : "7" + b);
        return b;
    }
    if (min3 && d5) { if (dim7) return "dim7"; if (m7) return "m7b5"; return "dim"; }
    if (a5) return M7 ? "maj7#5" : "aug";
    std::string q;
    if (maj3)
    {
        if (M7) q = n9 ? "maj9" : "maj7"; else if (m7) q = n9 ? "9" : "7"; else if (six) q = n9 ? "6/9" : "6"; else q = n9 ? "add9" : "";
        if (s11) q += (q.empty() ? "add" : "") + std::string ("#11");
        if (b9) q += "b9";
    }
    else
    {
        if (M7) q = "m(maj7)"; else if (m7) q = e11 ? "m11" : n9 ? "m9" : "m7"; else if (six) q = "m6"; else q = n9 ? "m(add9)" : "m";
        if (e11 && ! m7) q += "(add11)";
        if (b9) q += "b9";
    }
    return q;
}
std::string triadQuality (const std::vector<int>& iv)
{
    std::set<int> s; for (int x : iv) s.insert (mod12 (x));
    if (s.count (4)) return s.count (8) && ! s.count (7) ? "aug" : "maj";
    if (s.count (3)) return s.count (6) && ! s.count (7) ? "dim" : "min";
    return "sus";
}
static const char* ROMAN[] { "I", "II", "III", "IV", "V", "VI", "VII" };

// ---------------------------------------------------------------- templates
using Tpl = std::vector<std::pair<std::string, std::string>>;
static const std::map<std::string, Tpl>& TEMPLATES()
{
    static const std::map<std::string, Tpl> T {
        { "minor", { { "i VI III VII", "progressive trance epic sad euphoric melodic house pop" }, { "i VII VI VII", "trance epic dark" }, { "VI VII i i", "epic trance euphoric cinematic" },
                     { "i iv VI V", "sad tense cinematic" }, { "i VI iv VII", "progressive sad melodic" }, { "i III VII VI", "progressive epic euphoric" }, { "iv VI i VII", "progressive melodic dreamy" },
                     { "i v VI iv", "sad chill lofi" }, { "VI iv i VII", "progressive dreamy melodic chill" }, { "i i VI VII", "trance techno epic" }, { "i VII v VI", "dark sad" },
                     { "i iv i VII", "dark techno" }, { "i VI VII v", "progressive melodic dark" }, { "VI III VII i", "epic euphoric progressive" }, { "i iv VII III", "sad chill lofi dreamy" },
                     { "i III iv VI", "dreamy sad cinematic" }, { "i VII i VI", "dark techno tense" }, { "i bII i VII", "tense dark techno" }, { "VI VII v i", "melodic progressive sad" },
                     { "i VI III V", "tense epic cinematic trance" }, { "iv i VI VII", "progressive euphoric house" }, { "i iv v iv", "dark techno chill" } } },
        { "major", { { "I V vi IV", "pop happy euphoric progressive" }, { "vi IV I V", "euphoric progressive pop sad house melodic trance" }, { "IV I V vi", "euphoric progressive house happy" },
                     { "I vi IV V", "happy pop" }, { "IV V iii vi", "euphoric dreamy sad pop" }, { "I iii IV V", "happy dreamy" }, { "IV vi I V", "euphoric progressive dreamy" },
                     { "I IV vi V", "happy pop house" }, { "vi V IV V", "euphoric sad trance" }, { "IV I vi V", "dreamy progressive chill" }, { "I V IV IV", "happy house" },
                     { "I bVII IV I", "happy house epic" }, { "IV iv I I", "dreamy sad cinematic chill" }, { "ii V I vi", "lofi chill jazz happy" }, { "I vi ii V", "lofi chill happy" },
                     { "IV iii ii I", "lofi dreamy chill" }, { "I iii vi IV", "dreamy sad" }, { "vi IV V iii", "sad dreamy progressive" }, { "IV V vi I", "euphoric epic progressive trance" },
                     { "I bVI bVII I", "epic cinematic euphoric" }, { "vi ii IV V", "progressive house chill" } } },
        { "lydian", { { "I II I II", "dreamy cinematic chill" }, { "I II vii iii", "dreamy" }, { "I II vi I", "dreamy happy" }, { "vi II I I", "dreamy euphoric" } } },
        { "dorian", { { "i IV i IV", "chill lofi house techno" }, { "i ii i IV", "chill lofi" }, { "i IV VII i", "house chill" }, { "i v IV i", "dark chill techno" } } },
        { "mixolydian", { { "I VII IV I", "happy house epic" }, { "I v IV I", "happy chill" }, { "I VII IV IV", "happy house" } } },
        { "phrygian", { { "i II i i", "tense dark techno" }, { "i II VII i", "tense dark cinematic" }, { "i II i VII", "dark techno" } } } };
    return T;
}
static const Tpl& tplOf (const std::string& fam) { auto& T = TEMPLATES(); auto it = T.find (fam); return it != T.end() ? it->second : T.at ("minor"); }
static const std::vector<std::string>& vampsOf (const std::string& fam)
{
    static const std::map<std::string, std::vector<std::string>> V { { "minor", { "i VI", "i VII", "VI VII", "i iv", "i III", "iv i" } }, { "major", { "I IV", "vi IV", "IV V", "I V", "vi V" } },
        { "lydian", { "I II" } }, { "dorian", { "i IV" } }, { "mixolydian", { "I VII" } }, { "phrygian", { "i II" } } };
    auto it = V.find (fam); return it != V.end() ? it->second : V.at ("minor");
}
static std::string tplFamily (const std::string& scale) { return scale == "harmonic" ? "minor" : scale; }

// ---------------------------------------------------------------- numerals -> chords
TokBase resolveToken (const std::string& tokIn, const std::string& scale)
{
    TokBase b; b.tok = tokIn;
    std::string t = tokIn, acc;
    if (! t.empty() && (t[0] == 'b' || t[0] == '#')) { acc = t.substr (0, 1); t = t.substr (1); }
    if (t.size() >= 2 && (unsigned char) t[t.size() - 2] == 0xC2 && (unsigned char) t.back() == 0xB0) t = t.substr (0, t.size() - 2);   // degree sign
    else if (! t.empty() && t.back() == 'o') t.pop_back();
    static const std::vector<std::string> NUM { "VII", "VI", "IV", "V", "III", "II", "I", "vii", "vi", "iv", "v", "iii", "ii", "i" };
    if (! containsS (NUM, t)) return b;
    std::string upper = t; for (auto& c : upper) c = (char) std::toupper ((unsigned char) c);
    const bool up = (t == upper);
    int d = 0; for (int i = 0; i < 7; ++i) if (upper == ROMAN[i]) d = i;
    const auto S = scaleOf (scale);
    const bool minorFam = minorish (scale);
    b.ok = true; b.dd = d;
    if (acc == "b")
    {
        b.cs = d == 1 ? scaleOf ("phrygian") : scaleOf ("minor"); b.root = b.cs[(size_t) d]; b.chrom = ! contains (S, b.root);
        return b;
    }
    std::vector<int> cs = scale == "harmonic" && d != 4 && d != 6 ? scaleOf ("minor") : S;
    int root = cs[(size_t) d]; bool chrom = false;
    const std::string q = triadQuality ({ 0, mod12 (S[(size_t) ((d + 2) % 7)] - S[(size_t) d]), mod12 (S[(size_t) ((d + 4) % 7)] - S[(size_t) d]) });
    if (up && q == "min" && minorFam && d == 4) { cs = scaleOf ("harmonic"); chrom = scale != "harmonic"; }
    else if (! up && q == "maj" && ! minorFam && (d == 3 || d == 0)) { cs = scaleOf ("minor"); root = cs[(size_t) d]; chrom = true; }
    b.cs = cs; b.root = root; b.chrom = chrom;
    return b;
}
int csTone (const TokBase& b, int k)
{
    const int i = b.dd + k;
    return b.cs[(size_t) (i % 7)] + 12 * (int) std::floor (i / 7.0) - b.cs[(size_t) b.dd];
}
static std::vector<int> ladder (const TokBase& b, int L, double r)
{
    const int third = csTone (b, 2), fifth = csTone (b, 4), sev = csTone (b, 6), ninth = csTone (b, 8), sec = csTone (b, 1), fourth = csTone (b, 3), six = csTone (b, 5), elev = csTone (b, 10);
    const bool ok9 = ninth == 14, okSus2 = sec == 2 && fifth == 7, okSus4 = fourth == 5 && fifth == 7 && third != 5;
    const std::vector<int> tri { 0, third, fifth };
    if (L <= 0) return tri;
    if (L == 1)
    {
        if (r < 0.4 && ok9) return { 0, third, fifth, 14 };
        if (r < 0.7 && okSus2) return { 0, 2, 7 };
        if (okSus4 && third == 4 && r >= 0.7) return { 0, 5, 7 };
        return ok9 ? std::vector<int> { 0, third, fifth, 14 } : okSus2 ? std::vector<int> { 0, 2, 7 } : tri;
    }
    if (L == 2) return { 0, third, fifth, sev };
    if (L == 3) return ok9 ? (r < 0.5 ? std::vector<int> { 0, third, sev, 14 } : std::vector<int> { 0, third, fifth, sev, 14 }) : std::vector<int> { 0, third, fifth, sev };
    if (third == 3 && elev == 17 && ok9) { std::vector<int> o; const int a[] { 0, 3, 7, 10, 14, 17 }; for (int i = 0; i < 6; ++i) if (i != 2 || r < 0.3) o.push_back (a[i]); return o; }
    if (third == 4 && six == 9 && ok9 && sev == 11 && r < 0.5) return { 0, 4, 9, 14 };
    if (third == 4 && sev == 11 && ok9 && elev == 18) return { 0, 4, 11, 14, 18 };
    return ladder (b, 3, r);
}
static std::vector<int> qChord (const TokBase& b, const std::string& q, double r)
{
    const int third = csTone (b, 2), fifth = csTone (b, 4);
    if (q == "add9") return ladder (b, 1, 0.1);
    if (q == "sus") { const int sec = csTone (b, 1), fourth = csTone (b, 3); return sec == 2 && fifth == 7 ? std::vector<int> { 0, 2, 7 } : fourth == 5 && fifth == 7 ? std::vector<int> { 0, 5, 7 } : ladder (b, 1, 0.1); }
    if (q == "sev") return ladder (b, 2, r);
    if (q == "ext") return ladder (b, r < 0.5 ? 3 : 4, r);
    if (q == "six") return csTone (b, 5) == 9 && fifth == 7 ? std::vector<int> { 0, third, 7, 9 } : std::vector<int> { 0, third, fifth };
    return { 0, third, fifth };
}
static int colourLevel (double colour, double r) { return colour < 0.04 ? 0 : (int) clampv (jsRound (colour * 4 + (r - 0.5) * 1.4), 0.0, 4.0); }

const std::vector<std::string>& altNames() { static const std::vector<std::string> n { "auto", "sus", "7th", "lush", "over 3rd", "swap down", "swap up", "borrow" }; return n; }

ChordBase altChord (const Slot& slot, const std::string& scale, double colour)
{
    ChordBase out;
    if (slot.fixed)
    {
        const FixedChord& f = *slot.fixed; const int a = slot.alt % 3;
        std::vector<int> iv = f.iv;
        if (a == 1) { std::vector<int> t; for (int x : iv) if (x < 12 && contains ({ 0, 3, 4, 6, 7, 8, 2, 5 }, x)) t.push_back (x); if (t.size() >= 2) iv = t; }
        out.root = f.root; out.iv = iv; if (a != 2) out.bass = f.bass; out.deg = f.deg; out.chrom = f.chrom; out.roman = f.roman;
        return out;
    }
    TokBase b = resolveToken (slot.tok, scale);
    if (! b.ok) b = resolveToken (minorish (scale) ? "i" : "I", scale);
    const int alt = slot.alt % (int) altNames().size(); const double r = slot.r;
    if (alt == 5 || alt == 6) { const int dd = (b.dd + (alt == 5 ? 5 : 2)) % 7; const auto cs = scaleOf (scale); TokBase nb; nb.cs = cs; nb.dd = dd; nb.root = cs[(size_t) dd]; nb.chrom = false; nb.ok = true; b = nb; }
    if (alt == 7) { const std::vector<int> other = minorish (scale) ? scaleOf ("major") : scaleOf ("minor"); TokBase nb; nb.cs = other; nb.dd = b.dd; nb.root = other[(size_t) b.dd]; nb.chrom = true; nb.ok = true; b = nb; }
    std::vector<int> iv;
    const int L = colourLevel (colour, r);
    if (alt == 1) { const int fifth = csTone (b, 4), sec = csTone (b, 1), fourth = csTone (b, 3); iv = sec == 2 && fifth == 7 ? std::vector<int> { 0, 2, 7 } : fourth == 5 && fifth == 7 ? std::vector<int> { 0, 5, 7 } : ladder (b, 1, 0.1); }
    else if (alt == 2) iv = ladder (b, 2, r);
    else if (alt == 3) iv = ladder (b, 4, r);
    else if (alt == 0 && ! slot.q.empty()) iv = qChord (b, slot.q, r);
    else iv = ladder (b, L, r);
    const auto keyS = scaleOf (scale);
    bool chrom = b.chrom; for (int x : iv) if (! contains (keyS, mod12 (b.root + x))) chrom = true;
    if (alt == 4) out.bass = mod12 (b.root + iv[1]);
    else if (alt == 0 && slot.sl == "3rd" && triadQuality (iv) != "sus") out.bass = mod12 (b.root + iv[1]);
    out.root = b.root; out.iv = iv; out.deg = b.dd; out.chrom = chrom; out.cs = b.cs;
    return out;
}

std::string romanOf (const ChordBase& ch, const std::string& scale)
{
    const auto S = scaleOf (scale); const std::string q = triadQuality (ch.iv);
    auto idx = [] (const std::vector<int>& v, int x) { for (size_t i = 0; i < v.size(); ++i) if (v[i] == x) return (int) i; return -1; };
    int deg = idx (S, mod12 (ch.root)); std::string acc;
    if (deg < 0)
    {
        const auto M = scaleOf ("major"); int d2 = idx (M, mod12 (ch.root + 1));
        if (d2 >= 0) { deg = d2; acc = "b"; } else { d2 = idx (M, mod12 (ch.root - 1)); deg = std::max (0, d2); acc = "#"; }
    }
    std::string n = ROMAN[deg];
    if (q == "min" || q == "dim") for (auto& c : n) c = (char) std::tolower ((unsigned char) c);
    if (q == "dim") n += "\xC2\xB0";
    if (q == "aug") n += "+";
    if (q == "sus") n += "sus";
    return acc + n;
}

// ---------------------------------------------------------------- voice leading
static constexpr int VCENTER = 62;
static std::vector<std::vector<int>> voicings (const std::vector<int>& pcs)
{
    std::vector<std::vector<int>> out; const int n = (int) pcs.size();
    for (int inv = 0; inv < n; ++inv)
    {
        std::vector<int> rot (pcs.begin() + inv, pcs.end()); rot.insert (rot.end(), pcs.begin(), pcs.begin() + inv);
        for (int lo = 48; lo < 62; ++lo)
        {
            if (mod12 (lo) != rot[0]) continue;
            std::vector<int> v { lo };
            for (int i = 1; i < n; ++i) { int p = v[(size_t) i - 1] + 1; while (mod12 (p) != rot[(size_t) i]) ++p; v.push_back (p); }
            out.push_back (v);
            if (n >= 4) { auto d = v; d[(size_t) n - 2] -= 12; std::sort (d.begin(), d.end()); if (d[0] >= 43) out.push_back (d); }
        }
    }
    return out;
}
double vlCost (const std::vector<int>& a, const std::vector<int>& b)
{
    double s = 0;
    for (int x : a) { int m = 99; for (int y : b) m = std::min (m, std::abs (x - y)); s += m; }
    for (int y : b) { int m = 99; for (int x : a) m = std::min (m, std::abs (x - y)); s += m; }
    return s / (double) (a.size() + b.size());
}
std::vector<int> voiceChord (const std::vector<int>& pcsIn, const std::vector<int>* prev, int rootPc)
{
    std::vector<int> pcs; for (int p : pcsIn) if (! contains (pcs, p)) pcs.push_back (p);
    if (pcs.size() > 5) pcs.resize (5);
    if (pcs.size() == 5)
    {
        const int fifth = mod12 (rootPc + 7); int i = -1; for (size_t k = 0; k < pcs.size(); ++k) if (pcs[k] == fifth) { i = (int) k; break; }
        if (i > 0) pcs.erase (pcs.begin() + i); else pcs.pop_back();
    }
    std::vector<int> best; double bc = 1e9;
    for (auto& v : voicings (pcs))
    {
        int semi = 0; for (size_t i = 1; i < v.size(); ++i) if (v[i] - v[i - 1] == 1) ++semi;
        const double ctr = (v[0] + v.back()) / 2.0;
        double c = semi * 2.5 + std::abs (ctr - VCENTER) * 0.18 + (v.back() > 76 ? 3 : 0);
        if (prev != nullptr) c += vlCost (v, *prev) * 1.6; else c += mod12 (v[0]) == rootPc ? 0 : 1.2;
        if (c < bc) { bc = c; best = v; }
    }
    return best;
}

std::vector<Resolved> resolveProg (const ProgState& S)
{
    std::vector<Resolved> out; std::vector<int> prev; bool havePrev = false; std::optional<int> prevBass; int t = 0;
    for (size_t i = 0; i < S.prog.size(); ++i)
    {
        const Slot& slot = S.prog[i];
        ChordBase c = altChord (slot, S.scale, S.colour);
        if (S.simple && ! (S.mood >= 3 && slot.fixed)) c = simplifyChord (c, S.scale, S.mood);   // (TRAP / HIP HOP: their own fixed chords, 7ths kept)
        std::optional<int> bass = c.bass;
        if (S.simple) {}   // SIMPLE: bass = root, no pedal / passing-third basses
        else if (! bass && (S.pedal || slot.sl == "ped") && ! slot.fixed && i > 0)
        {
            bool clash = false; for (int x : c.iv) { const int p = mod12 (c.root + x); if (p == 1 || p == 11) clash = true; }
            if (! clash && c.root != 0) bass = 0;
        }
        if (! S.simple && ! bass && ! slot.fixed && prevBass && slot.r2 < 0.3 && S.colour > 0.3)
        {
            const int third = mod12 (c.root + c.iv[1]);
            const int dr = std::min (mod12 (c.root - *prevBass), mod12 (*prevBass - c.root)), dt = std::min (mod12 (third - *prevBass), mod12 (*prevBass - third));
            if (dr >= 5 && dt <= 2 && dt > 0 && triadQuality (c.iv) != "sus") bass = third;
        }
        std::vector<int> absPcs; for (int x : c.iv) absPcs.push_back (mod12 (S.key + c.root + x));
        auto voicing = voiceChord (absPcs, havePrev ? &prev : nullptr, mod12 (S.key + c.root));
        prev = voicing; havePrev = true;
        const int bpc = mod12 (S.key + (bass ? *bass : c.root)); int bn = 36 + bpc; if (bn < 38) bn += 12;
        prevBass = bass ? *bass : c.root;
        Resolved r; static_cast<ChordBase&> (r) = c; r.bass = bass;
        r.name = noteName (S.key + c.root, S.key, S.scale) + chordSuffix (c.iv) + (bass && mod12 (*bass) != mod12 (c.root) ? "/" + noteName (S.key + *bass, S.key, S.scale) : "");
        r.roman = ! c.roman.empty() ? c.roman : romanOf (c, S.scale);
        r.voicing = voicing; r.bassNote = bn; r.pcs = absPcs; r.t = t; r.len = slot.len ? slot.len : 4; r.tEnd = t + r.len * PPQ;
        t += r.len * PPQ;
        out.push_back (std::move (r));
    }
    return out;
}
int progTicks (const std::vector<Slot>& prog) { int a = 0; for (auto& s : prog) a += (s.len ? s.len : 4) * PPQ; return a; }

// ---------------------------------------------------------------- SIMPLE chords
std::string simpleScale (const std::string& scale) { return minorish (scale) ? "minor" : "major"; }

ChordBase simplifyChord (const ChordBase& c, const std::string& famScale, int mood)
{
    const bool minorF = minorish (famScale);
    const auto& S = scaleOf (minorF ? "minor" : "major");
    auto idx = [&] (int pc) { for (int i = 0; i < 7; ++i) if (S[(size_t) i] == mod12 (pc)) return i; return -1; };
    auto diaQ = [&] (int d) { return triadQuality ({ 0, mod12 (S[(size_t) ((d + 2) % 7)] - S[(size_t) d]), mod12 (S[(size_t) ((d + 4) % 7)] - S[(size_t) d]) }); };
    std::set<int> pcs; for (int x : c.iv) pcs.insert (mod12 (x));
    const std::string cq = pcs.count (4) ? "maj" : pcs.count (3) ? "min" : "sus";
    int root = mod12 (c.root), d = idx (root); std::string q;
    if (d < 0)
    {
        if (! minorF && (root == 3 || root == 8 || root == 10)) q = "maj";   // the classic borrowed bIII / bVI / bVII
        else
        {   // any other chromatic root: the nearest diatonic major / minor chord
            for (int off : { -1, 1, -2, 2 }) { const int d2 = idx (root + off); if (d2 >= 0 && diaQ (d2) != "dim") { root = mod12 (root + off); d = d2; break; } }
            if (d < 0) { root = 0; d = 0; }
            q = diaQ (d);
        }
    }
    else if (minorF && root == 7 && cq == "maj") q = "maj";                    // V major in minor
    else if (! minorF && mood == 1 && root == 5 && cq == "min") q = "min";     // DUSK: iv borrowed in major
    else
    {
        q = diaQ (d);
        if (q == "dim") { d = (d + 5) % 7; root = S[(size_t) d]; q = diaQ (d); }   // no diminished: the chord a third below (vii° -> V, ii° -> VII)
    }
    ChordBase o;
    o.root = root; o.iv = q == "maj" ? std::vector<int> { 0, 4, 7 } : std::vector<int> { 0, 3, 7 }; o.deg = d;
    o.chrom = ! (idx (root) >= 0 && idx (root + o.iv[1]) >= 0);
    static const std::vector<int> MELODIC { 0, 2, 3, 5, 7, 9, 11 };   // V in minor: melodic minor (E F# G# A B C D over E in A minor)
    o.cs = ! o.chrom ? S : minorF ? MELODIC : scaleOf ("minor");
    return o;
}

const std::vector<std::pair<std::string, double>>& classicLoops (int mood, const std::string& scale)
{
    static const std::vector<std::pair<std::string, double>>
        SUN { { "I V vi IV", 3 }, { "vi IV I V", 3 }, { "IV I V vi", 2 }, { "I vi IV V", 1.5 }, { "IV V vi I", 1.5 } },
        MID { { "i VI III VII", 3 }, { "i VII VI VII", 2 }, { "i iv VI V", 1.5 }, { "VI VII i i", 2 }, { "i III VII VI", 2 } },
        DUSK_MAJ { { "vi IV I V", 3 }, { "IV iv I I", 2 } },
        DUSK_MIN { { "i III VII VI", 1 } },
        SUN_MIN { { "i VI III VII", 2 }, { "i III VII VI", 2 }, { "VI VII i i", 1 } };
    const bool minorF = minorish (scale);
    if (mood == 1) return minorF ? DUSK_MIN : DUSK_MAJ;
    if (mood == 0) return minorF ? SUN_MIN : SUN;
    return minorF ? MID : SUN;
}

std::vector<Slot> classicProg (int mood, const std::string& scale, uint32_t seed, int nBars, const std::vector<Slot>* avoid)
{
    const auto& L = classicLoops (mood, scale);
    JMap w; for (size_t i = 0; i < L.size(); ++i) w.set (std::to_string (i), L[i].second);
    std::string cur; if (avoid) for (size_t i = 0; i < avoid->size() && i < 4; ++i) cur += (i ? " " : "") + (*avoid)[i].tok;
    Rng R (seed ^ 0xC1A55u);
    std::string pick;
    for (int k = 0; k < 12; ++k) { pick = L[(size_t) std::stoi (pickW (R, w))].first; if (pick != cur || L.size() < 2) break; }
    const auto toks = split (pick);
    std::vector<Slot> out; int beats = 0; size_t k = 0; const int want = std::max (2, nBars) * 4;
    while (beats < want) { Slot s; s.tok = toks[k % toks.size()]; s.r = R(); s.r2 = R(); s.len = std::min (4, want - beats); out.push_back (s); beats += s.len; ++k; }
    return out;
}

// ---------------------------------------------------------------- prompt parser
bool Parsed::hasTag (const std::string& t) const { return containsS (tags, t); }
namespace
{
using Act = std::function<void (Parsed&, const std::smatch&)>;
struct Lex { std::regex re; Act fn; };
void tag (Parsed& f, const char* t) { if (! f.hasTag (t)) f.tags.push_back (t); }
const std::vector<Lex>& lexTable()
{
    using std::regex;
    auto R = [] (const char* s) { return regex (s, std::regex::ECMAScript | std::regex::optimize); };
    static const std::vector<Lex> L {
        { R ("euphor|uplift|anthem|festival|levels|avicii|big ?room|soar|hands up|joy|ecsta|rave|celebrat"), [] (Parsed& f, const std::smatch&) { tag (f, "euphoric"); } },
        { R ("happy|bright|sunny|summer|fun\\b|cheer|feel ?good|upbeat|playful|cute"), [] (Parsed& f, const std::smatch&) { tag (f, "happy"); } },
        { R ("dream|float|ethereal|airy|lush|heaven|cloud|space|shimmer|nostalg|magic|wonder|sparkl|glow"), [] (Parsed& f, const std::smatch&) { tag (f, "dreamy"); } },
        { R ("sad|melanchol|emotion|cry|tear|lonely|bitter|heartbreak|blue|rain|moody|somber|sombre|longing|miss"), [] (Parsed& f, const std::smatch&) { tag (f, "sad"); } },
        { R ("dark|evil|sinister|night|underground|grim|shadow|haunt|spooky|ghost"), [] (Parsed& f, const std::smatch&) { tag (f, "dark"); } },
        { R ("tense|tension|suspense|anxious|horror|scary|creepy|thriller|edgy|drama|villain|danger"), [] (Parsed& f, const std::smatch&) { tag (f, "tense"); } },
        { R ("epic|hero|massive|huge|anthemic|power|triumph|orchestral|giant|stadium|climax"), [] (Parsed& f, const std::smatch&) { tag (f, "epic"); } },
        { R ("chill|relax|calm|mellow|soft|gentle|lazy|smooth|laid ?back|sunset|beach|cozy|cosy"), [] (Parsed& f, const std::smatch&) { tag (f, "chill"); } },
        { R ("progressive|\\bprog\\b"), [] (Parsed& f, const std::smatch&) { if (! f.genre) f.genre = "progressive"; } },
        { R ("prydz|kaskade|melodic"), [] (Parsed& f, const std::smatch&) { if (! f.genre) f.genre = "melodic"; } },
        { R ("trance|psy\\b|goa"), [] (Parsed& f, const std::smatch&) { if (! f.genre) f.genre = "trance"; } },
        { R ("house|disco|club|groove|garage"), [] (Parsed& f, const std::smatch&) { if (! f.genre) f.genre = "house"; } },
        { R ("techno|industrial|warehouse|berlin|minimal techno|hypnotic techno"), [] (Parsed& f, const std::smatch&) { if (! f.genre) f.genre = "techno"; } },
        { R ("lo-?fi|jazz|hip ?hop|neo ?soul|study|coffee"), [] (Parsed& f, const std::smatch&) { if (! f.genre) f.genre = "lofi"; } },
        { R ("cinematic|film|score|movie|trailer|game music|soundtrack"), [] (Parsed& f, const std::smatch&) { if (! f.genre) f.genre = "cinematic"; } },
        { R ("\\bpop\\b|radio|catchy"), [] (Parsed& f, const std::smatch&) { if (! f.genre) f.genre = "pop"; } },
        { R ("ambient|drone|meditat|zen"), [] (Parsed& f, const std::smatch&) { if (! f.genre) f.genre = "ambient"; } },
        { R ("1/32|32nd|thirty|very fast arp|machine ?gun"), [] (Parsed& f, const std::smatch&) { f.rate = "1/32"; } },
        { R ("1/16 ?t\\b|triplet|1/12|shuffl|swung 16"), [] (Parsed& f, const std::smatch&) { f.rate = "1/16T"; } },
        { R ("1/16|16th|sixteenth"), [] (Parsed& f, const std::smatch&) { if (! f.rate) f.rate = "1/16"; } },
        { R ("1/8|8th|eighth"), [] (Parsed& f, const std::smatch&) { if (! f.rate) f.rate = "1/8"; } },
        { R ("rolling|roll\\b|offbeat|off-beat"), [] (Parsed& f, const std::smatch&) { f.rolling = true; } },
        { R ("pedal|trance ?gate|gated"), [] (Parsed& f, const std::smatch&) { f.style = "pedal"; } },
        { R ("up ?down|bounce|ping ?pong|wave"), [] (Parsed& f, const std::smatch&) { if (! f.style) f.style = "updown"; } },
        { R ("converg|inward|inside out|outside in"), [] (Parsed& f, const std::smatch&) { if (! f.style) f.style = "converge"; } },
        { R ("random|walk|wander|chaotic|chaos|glitch"), [] (Parsed& f, const std::smatch&) { if (! f.style) f.style = "walk"; } },
        { R ("broken|skip|leap"), [] (Parsed& f, const std::smatch&) { if (! f.style) f.style = "broken"; } },
        { R ("motif|hook|riff|hypnotic|repetitive|sequence|loop"), [] (Parsed& f, const std::smatch&) { if (! f.style) f.style = "motif"; } },
        { R ("ascend|rising|\\bup\\b(?! ?down)(?!.*octave)|climb"), [] (Parsed& f, const std::smatch&) { if (! f.style) f.style = "up"; } },
        { R ("descend|falling|\\bdown\\b|cascade"), [] (Parsed& f, const std::smatch&) { if (! f.style) f.style = "down"; } },
        { R ("pluck"), [] (Parsed& f, const std::smatch&) { f.preset = "pluck"; } },
        { R ("supersaw|super saw|\\bsaws?\\b|trance lead|big lead|hoover"), [] (Parsed& f, const std::smatch&) { f.preset = "supersaw"; } },
        { R ("piano|keys|rhodes|\\bep\\b|organ"), [] (Parsed& f, const std::smatch&) { f.preset = "keys"; } },
        { R ("bell|glass|music ?box|chime|mallet|celest|marimba|kalimba"), [] (Parsed& f, const std::smatch&) { f.preset = "bell"; } },
        { R ("acid|303|squelch|wobble"), [] (Parsed& f, const std::smatch&) { f.preset = "acid"; f.glide = true; } },
        { R ("octave up|up an octave|higher|high pitch|\\bhigh\\b|sky"), [] (Parsed& f, const std::smatch&) { f.reg = 1; } },
        { R ("octave down|down an octave|lower|\\blow\\b|deep|bass"), [] (Parsed& f, const std::smatch&) { f.reg = -1; } },
        { R ("three octaves|3 octaves|huge range|very wide"), [] (Parsed& f, const std::smatch&) { f.oct = 3; } },
        { R ("two octaves|2 octaves|\\bwide\\b"), [] (Parsed& f, const std::smatch&) { if (! f.oct) f.oct = 2; } },
        { R ("one octave|1 octave|narrow|tight"), [] (Parsed& f, const std::smatch&) { f.oct = 1; } },
        { R ("sparse|minimal|simple|easy|basic|few notes|stripped|space between"), [] (Parsed& f, const std::smatch&) { f.master = 0.2; } },
        { R ("busy|complex|crazy|wild|intricate|dense|lots of notes|insane|frantic|nerdy"), [] (Parsed& f, const std::smatch&) { f.master = 0.85; } },
        { R ("ratchet|stutter|repeat"), [] (Parsed& f, const std::smatch&) { f.ratchet = true; } },
        { R ("glitch|chaotic|chaos|crazy|wild|frantic|busy"), [] (Parsed& f, const std::smatch&) { f.groove = std::max (f.groove, 0.7); } },
        { R ("rolling|syncopat|groov|swing|shuffl|offbeat|off-beat"), [] (Parsed& f, const std::smatch&) { f.groove = std::max (f.groove, 0.6); } },
        { R ("human|loose|sloppy|ratchet|stutter"), [] (Parsed& f, const std::smatch&) { f.groove = std::max (f.groove, 0.5); } },
        { R ("glide|legato|slide|portamento|smooth"), [] (Parsed& f, const std::smatch&) { f.glide = true; } },
        { R ("lush|jazzy|rich|7ths?\\b|sevenths|9ths?\\b|ninths|extended|neo soul"), [] (Parsed& f, const std::smatch&) { f.colour = 0.85; } },
        { R ("triads|plain|simple chords|basic chords|power chords"), [] (Parsed& f, const std::smatch&) { f.colour = 0.08; } },
        { R ("sus\\b|suspended|sus2|sus4|open chords"), [] (Parsed& f, const std::smatch&) { f.sus = true; if (! f.colour) f.colour = 0.4; } },
        { R ("fast|quick|energetic|driving|uptempo|urgent|hard"), [] (Parsed& f, const std::smatch&) { f.speed = 1; } },
        { R ("slow|downtempo|sleepy"), [] (Parsed& f, const std::smatch&) { f.speed = -1; } },
        { R ("\\b(2|4|8|16) ?bars?\\b"), [] (Parsed& f, const std::smatch& m) { f.bars = std::stoi (m[1].str()); } },
        { R ("dorian"), [] (Parsed& f, const std::smatch&) { f.scale = "dorian"; } },
        { R ("phrygian"), [] (Parsed& f, const std::smatch&) { f.scale = "phrygian"; } },
        { R ("lydian"), [] (Parsed& f, const std::smatch&) { f.scale = "lydian"; } },
        { R ("mixolydian|mixo"), [] (Parsed& f, const std::smatch&) { f.scale = "mixolydian"; } },
        { R ("harmonic|arabic|exotic|egypt"), [] (Parsed& f, const std::smatch&) { f.scale = "harmonic"; } } };
    return L;
}
} // namespace

Parsed parsePrompt (const std::string& p)
{
    std::string low;
    for (size_t i = 0; i < p.size(); ++i)
    {
        const unsigned char c = (unsigned char) p[i];
        if (c == 0xE2 && i + 2 < p.size() && (unsigned char) p[i + 1] == 0x99 && ((unsigned char) p[i + 2] == 0xAF || (unsigned char) p[i + 2] == 0xAD))
        { low += (unsigned char) p[i + 2] == 0xAF ? '#' : 'b'; i += 2; continue; }
        char ch = (char) std::tolower (c < 0x80 ? c : 0) ; if (c >= 0x80) ch = (char) c;
        if (ch == ',' || ch == ';' || ch == '!' || ch == '?' || ch == '(' || ch == ')' || ch == '"') ch = ' ';
        low += ch;
    }
    const std::string s = " " + low + " ";
    Parsed f;
    for (auto& L : lexTable()) { std::smatch m; if (std::regex_search (s, m, L.re)) L.fn (f, m); }
    {
        static const std::regex b1 ("\\b(\\d{2,3}) ?(bpm|beats)"), b2 ("(?:^|\\s)(\\d{2,3})(?=\\s)");
        std::smatch m;
        if (std::regex_search (s, m, b1) || std::regex_search (s, m, b2)) { const int v = std::stoi (m[1].str()); if (v >= 60 && v <= 200) f.bpm = v; }
    }
    {
        static const std::regex k1 ("(?:\\bin |\\bkey of |\\bkey )([a-g])(#|b|-sharp| sharp|-flat| flat)?(?: ?(major|minor|maj|min|m)\\b)?");
        static const std::regex k2 ("\\b([a-g])(#|b)?(?: ?(major|minor|maj\\b|min\\b)|(m)\\b)");
        static const std::regex k3 ("(?:^|\\s)([a-g])(#|b)(?=\\s)");
        std::smatch m;
        if (std::regex_search (s, m, k1) || std::regex_search (s, m, k2) || std::regex_search (s, m, k3))
        {
            static const std::map<char, int> Lk { { 'c', 0 }, { 'd', 2 }, { 'e', 4 }, { 'f', 5 }, { 'g', 7 }, { 'a', 9 }, { 'b', 11 } };
            int k = Lk.at (m[1].str()[0]);
            const std::string acc = m.size() > 2 && m[2].matched ? m[2].str() : "";
            if (acc.find ('#') != std::string::npos || acc.find ("sharp") != std::string::npos) ++k;
            else if (acc == "b" || acc.find ("flat") != std::string::npos) --k;
            f.key = mod12 (k);
            std::string q = m.size() > 3 && m[3].matched ? m[3].str() : "";
            if (q.empty() && m.size() > 4 && m[4].matched) q = m[4].str();
            if (! q.empty() && (q == "m" || q == "min" || q == "minor")) { if (! f.scale) f.scale = "minor"; }
            else if (! q.empty() && q.rfind ("maj", 0) == 0) { if (! f.scale) f.scale = "major"; }
        }
    }
    if (! f.scale)
    {
        static const std::regex mi ("\\bminor\\b"), ma ("\\bmajor\\b");
        if (std::regex_search (s, mi)) f.scale = "minor"; else if (std::regex_search (s, ma)) f.scale = "major";
    }
    return f;
}

// ---------------------------------------------------------------- genres + moods
namespace
{
struct Genre { int bpm; JMap styles, presets; double colour; JMap scales; int oct; double master, pedal; JMap bars; double swing = 0; std::string rate; };
const std::map<std::string, Genre>& GENRES()
{
    static const std::map<std::string, Genre> G {
        { "progressive", { 126, { { "motif", 4 }, { "pedal", 2 }, { "broken", 1 }, { "updown", 1 }, { "converge", 1 } }, { { "pluck", 4 }, { "supersaw", 1 }, { "bell", 1 } }, 0.5, { { "minor", 6 }, { "major", 2 }, { "dorian", 1 } }, 2, 0.5, 0.35, { { "4", 6 }, { "8", 4 }, { "2", 1 } }, 0, "" } },
        { "melodic", { 128, { { "motif", 5 }, { "pedal", 2 }, { "broken", 1 }, { "converge", 1 } }, { { "pluck", 4 }, { "keys", 1 }, { "bell", 1 } }, 0.55, { { "minor", 7 }, { "major", 1 }, { "dorian", 1 }, { "phrygian", 1 } }, 2, 0.5, 0.45, { { "4", 6 }, { "8", 4 }, { "2", 1 } }, 0, "" } },
        { "trance", { 138, { { "pedal", 4 }, { "up", 2 }, { "updown", 2 }, { "motif", 2 } }, { { "supersaw", 3 }, { "pluck", 2 } }, 0.3, { { "minor", 6 }, { "major", 1 }, { "harmonic", 1 } }, 2, 0.6, 0.25, { { "4", 5 }, { "8", 4 } }, 0, "" } },
        { "house", { 124, { { "broken", 2 }, { "motif", 3 }, { "up", 1 }, { "walk", 1 } }, { { "pluck", 3 }, { "keys", 2 } }, 0.6, { { "minor", 3 }, { "major", 3 }, { "dorian", 2 } }, 2, 0.5, 0.2, { { "4", 6 }, { "2", 2 }, { "8", 2 } }, 0, "" } },
        { "techno", { 130, { { "walk", 2 }, { "motif", 4 }, { "pedal", 2 } }, { { "acid", 3 }, { "pluck", 3 } }, 0.15, { { "minor", 3 }, { "phrygian", 3 }, { "dorian", 1 } }, 1, 0.5, 0.5, { { "2", 3 }, { "4", 4 } }, 0, "" } },
        { "lofi", { 84, { { "broken", 2 }, { "up", 2 }, { "updown", 1 }, { "motif", 1 } }, { { "keys", 5 }, { "bell", 1 } }, 0.85, { { "major", 3 }, { "dorian", 2 }, { "minor", 2 } }, 2, 0.35, 0, { { "4", 6 }, { "8", 2 } }, 0.35, "1/8" } },
        { "cinematic", { 94, { { "updown", 3 }, { "up", 2 }, { "converge", 1 }, { "motif", 1 } }, { { "bell", 2 }, { "keys", 2 }, { "supersaw", 1 } }, 0.4, { { "minor", 4 }, { "harmonic", 2 }, { "major", 1 }, { "lydian", 1 } }, 3, 0.55, 0.2, { { "4", 4 }, { "8", 4 } }, 0, "" } },
        { "pop", { 116, { { "up", 2 }, { "broken", 2 }, { "updown", 1 }, { "motif", 2 } }, { { "pluck", 2 }, { "keys", 2 } }, 0.35, { { "major", 4 }, { "minor", 2 } }, 2, 0.45, 0.1, { { "4", 7 }, { "8", 2 } }, 0, "" } },
        { "ambient", { 90, { { "updown", 2 }, { "walk", 1 }, { "up", 1 }, { "motif", 1 } }, { { "bell", 3 }, { "keys", 2 } }, 0.8, { { "lydian", 3 }, { "major", 2 }, { "dorian", 1 } }, 2, 0.3, 0.4, { { "4", 3 }, { "8", 4 } }, 0, "1/8" } } };
    return G;
}
struct MoodP { JMap scales, presets; double colour, master; int bpm, oct; };
const std::map<std::string, MoodP>& MOODS()
{
    static const std::map<std::string, MoodP> M {
        { "euphoric", { { { "major", 3 }, { "minor", 2 }, { "lydian", 1 } }, { { "supersaw", 2 }, { "pluck", 2 } }, 0.05, 0.1, 2, 0 } },
        { "happy", { { { "major", 5 }, { "mixolydian", 2 }, { "lydian", 1 } }, { { "pluck", 1 }, { "keys", 1 } }, -0.05, 0, 2, 0 } },
        { "dreamy", { { { "lydian", 3 }, { "major", 3 }, { "dorian", 1 }, { "minor", 1 } }, { { "bell", 3 }, { "keys", 1 } }, 0.25, -0.08, -4, 0 } },
        { "sad", { { { "minor", 5 }, { "dorian", 2 } }, { { "keys", 2 }, { "pluck", 1 } }, 0.1, -0.05, -4, 0 } },
        { "dark", { { { "minor", 4 }, { "phrygian", 3 } }, { { "pluck", 2 }, { "acid", 1 } }, -0.15, 0, 0, 0 } },
        { "tense", { { { "harmonic", 4 }, { "phrygian", 3 }, { "minor", 1 } }, { { "pluck", 1 }, { "bell", 1 } }, 0.05, 0.05, 0, 0 } },
        { "epic", { { { "minor", 5 }, { "major", 2 } }, { { "supersaw", 3 } }, 0, 0.12, 2, 1 } },
        { "chill", { { { "major", 2 }, { "dorian", 2 }, { "minor", 1 } }, { { "keys", 2 }, { "bell", 1 } }, 0.2, -0.15, -10, 0 } } };
    return M;
}
JMap addW (const JMap& a, const JMap& b, double w) { JMap o = a; for (auto& p : b.kv) o.set (p.first, o.get (p.first, 0) + p.second * w); return o; }

JMap tplWeights (const Tpl& pool, const std::vector<std::string>& want)
{
    JMap tw;
    for (size_t i = 0; i < pool.size(); ++i)
    {
        const auto tg = split (pool[i].second); int n = 0;
        for (auto& w : want) if (containsS (tg, w)) ++n;
        tw.set (std::to_string (i), 1 + 3 * n);
    }
    return tw;
}

// ---------------------------------------------------------------- STYLE
bool okTok (const std::string& tok, const std::string& scale)
{
    if (tok.empty()) return false;
    const TokBase b = resolveToken (tok, scale); if (! b.ok) return false;
    return triadQuality ({ 0, csTone (b, 2), csTone (b, 4) }) != "dim";
}
std::string sampleTok1 (Rng& R, const FamProfile& FP, const std::string& p2, const std::string& p1)
{
    if (p1.empty()) return ! FP.start.empty() ? pickW (R, FP.start) : std::string();
    const JMap* t3 = ! p2.empty() ? FP.triOf (p2 + " " + p1) : nullptr;
    const JMap* t2 = FP.trOf (p1);
    JMap d;
    if (t2) for (auto& p : t2->kv) d.set (p.first, p.second * 0.4);
    if (t3) for (auto& p : t3->kv) d.set (p.first, d.get (p.first, 0) + p.second * 0.6);
    if (d.empty()) d = FP.start;
    return ! d.empty() ? pickW (R, d) : std::string();
}
std::string sampleTok (Rng& R, const FamProfile& FP, const std::string& p2, const std::string& p1, const std::string& scale)
{
    for (int k = 0; k < 4; ++k) { auto t = sampleTok1 (R, FP, p2, p1); if (scale.empty() || okTok (t, scale)) return t; }
    return {};
}
std::vector<Slot> styleProg (const Profile& P, const FamProfile& FP, const std::vector<Slot>& base, int bars, bool full, double amt, Rng& R, const std::string& scale)
{
    auto colour = [&] { return ! P.colour.empty() ? pickW (R, P.colour) : std::string(); };
    auto slash = [&] { return R() < P.slash ? (R() < P.ped ? std::string ("ped") : std::string ("3rd")) : std::string(); };
    std::vector<Slot> out; std::string p2, p1;   // "" = null
    if (full)
    {
        int beats = 0; JMap lens { { "2", 0 }, { "4", 0 }, { "8", 0 } };
        for (auto& kv : P.len.kv) { const int k = std::stoi (kv.first); const std::string n = k <= 2 ? "2" : k < 6 ? "4" : "8"; lens.set (n, lens.get (n) + kv.second); }
        if (! (lens.get ("2") + lens.get ("4") + lens.get ("8"))) lens.set ("4", 1);
        const int pb = bars > 8 ? 8 : bars; const int reps = bars / pb; const int maxC = std::max (2, std::min (8, (int) std::floor (16.0 / reps)));
        while (beats < pb * 4)
        {
            int len = std::stoi (pickW (R, lens)); const int rem = pb * 4 - beats, inBar = 4 - beats % 4;
            if (beats % 4 && len > inBar) len = inBar;
            if (len > rem) len = rem;
            if ((int) out.size() >= maxC - 1) len = rem;
            std::string tok = sampleTok (R, FP, p2, p1, scale); if (tok == p1) tok = sampleTok (R, FP, p2, p1, scale);
            if (tok.empty()) tok = base[out.size() % base.size()].tok;
            Slot s; s.tok = tok; s.r = R(); s.r2 = R(); s.alt = 0; s.len = len; s.q = colour(); s.sl = slash();
            out.push_back (s); p2 = p1; p1 = tok; beats += len;
        }
        const auto ph = out; for (int k = 1; k < reps; ++k) out.insert (out.end(), ph.begin(), ph.end());
        return out;
    }
    for (size_t i = 0; i < base.size(); ++i)
    {
        Slot s = base[i];
        if (i > 0 && R() < amt * 0.6) { auto t = sampleTok (R, FP, p2, p1, scale); if (! t.empty() && t != p1) s.tok = t; }
        if (R() < amt) s.q = colour();
        if (R() < amt) s.sl = slash();
        p2 = p1; p1 = s.tok; out.push_back (s);
    }
    return out;
}
double profColour (const Profile& P)
{
    const auto& c = P.colour;
    return clampv (c.get ("add9") * 0.45 + c.get ("sus") * 0.35 + c.get ("sev") * 0.6 + c.get ("ext") * 0.95 + c.get ("six") * 0.45, 0.0, 1.0);
}
double lerp (double a, double b, double t) { return a + (b - a) * t; }
} // namespace

const JMap* FamProfile::trOf (const std::string& a) const { for (auto& p : tr) if (p.first == a) return &p.second; return nullptr; }
const JMap* FamProfile::triOf (const std::string& ab) const { for (auto& p : tri) if (p.first == ab) return &p.second; return nullptr; }

const Profile& builtinProfile()
{
    static const Profile P = [] {
        Profile p; p.name = "Professor B5"; p.builtin = true;
        for (const char* f : { "minor", "major" })
        {
            JMap start; std::vector<std::pair<std::string, JMap>> tr, tri;
            auto get = [] (std::vector<std::pair<std::string, JMap>>& v, const std::string& k) -> JMap& { for (auto& e : v) if (e.first == k) return e.second; v.push_back ({ k, JMap() }); return v.back().second; };
            const Tpl tpls = tplOf (f);
            for (auto& t : tpls)
            {
                const auto k = split (t.first); const size_t n = k.size(); start.inc (k[0]);
                for (size_t i = 0; i < n; ++i)
                {
                    const auto& a = k[i]; const auto& b = k[(i + 1) % n]; const auto& c = k[(i + 2) % n];
                    if (a != b) get (tr, a).inc (b);
                    if (a != b && b != c) get (tri, a + " " + b).inc (c);
                }
            }
            FamProfile F; F.start = normH (start);
            for (auto& e : tr) F.tr.push_back ({ e.first, normH (e.second) });
            for (auto& e : tri) F.tri.push_back ({ e.first, normH (e.second) });
            if (std::string (f) == "minor") { p.minorF = F; p.hasMinor = true; } else { p.majorF = F; p.hasMajor = true; }
        }
        p.colour = JMap { { "tri", 0.36 }, { "sus", 0.24 }, { "add9", 0.18 }, { "sev", 0.13 }, { "ext", 0.07 }, { "six", 0.02 } };
        p.slash = 0.16; p.ped = 0.35;
        p.len = JMap { { "2", 0.22 }, { "4", 0.6 }, { "8", 0.18 } };
        p.bars = JMap { { "4", 0.55 }, { "8", 0.45 } };
        p.arp.has = true;
        p.arp.rate = JMap { { "1/16", 0.86 }, { "1/8", 0.14 } };
        p.arp.step = JMap { { "1", 0.28 }, { "2", 0.2 }, { "-1", 0.2 }, { "3", 0.1 }, { "-2", 0.12 }, { "0", 0.06 }, { "4", 0.04 } };
        p.arp.rest = 0.16; p.arp.accent = 0.24; p.arp.ratchet = 0.03;
        p.arp.oct = JMap { { "2", 0.6 }, { "3", 0.4 } };
        p.arp.motif = JMap { { "3", 0.22 }, { "4", 0.26 }, { "5", 0.18 }, { "6", 0.26 }, { "7", 0.08 } };
        return p;
    }();
    return P;
}

std::string famOf (const std::string& scale) { return minorish (scale) ? "minor" : "major"; }

std::optional<std::string> degToken (int rel, const std::vector<int>& iv, const std::string& fam)
{
    static const std::map<std::string, std::vector<std::string>> DIA { { "minor", { "i", "ii", "III", "iv", "v", "VI", "VII" } }, { "major", { "I", "ii", "iii", "IV", "V", "vi", "vii" } } };
    const auto S = scaleOf (fam); int d = -1; for (int i = 0; i < 7; ++i) if (S[(size_t) i] == mod12 (rel)) d = i;
    const std::string q = triadQuality (iv);
    if (d < 0)
    {
        const int r = mod12 (rel);
        if (fam == "minor") { if (r == 1) return std::string ("bII"); return std::nullopt; }
        if (r == 1) return std::string ("bII");
        if (r == 3) return std::string ("bIII");
        if (r == 8) return std::string ("bVI");
        if (r == 10) return std::string ("bVII");
        return std::nullopt;
    }
    if (fam == "minor" && d == 4 && q == "maj") return std::string ("V");
    if (fam == "major" && (d == 3 || d == 0) && q == "min") return std::string (d ? "iv" : "i");
    return DIA.at (fam)[(size_t) d];
}

static Gen applyStyle (Gen g, const Parsed& f, StyleArg sty, uint32_t seed, const std::string& prompt)
{
    const double amt = clampv (sty.amt, 0.0, 1.0); const Profile& P = *sty.prof;
    Rng R ((seed ^ hashStr ("style|" + prompt) ^ 0x51a7e5u));
    const FamProfile* FP = P.fam (famOf (g.scale));
    if (FP)
    {
        const bool full = R() < amt; int bars = g.bars;
        if (full && ! f.bars && ! P.bars.empty()) bars = std::stoi (pickW (R, P.bars));
        g.prog = styleProg (P, *FP, g.prog, bars, full, amt, R, g.scale); g.bars = bars;
        g.colour = clampv (lerp (g.colour, profColour (P), amt), 0.0, 1.0);
    }
    const ArpStats& PA = P.arp;
    if (PA.has)
    {
        if (! f.rate && ! PA.rate.empty() && R() < amt) g.rate = pickW (R, PA.rate);
        if (! f.oct && ! PA.oct.empty() && R() < amt) g.oct = std::stoi (pickW (R, PA.oct));
        if (! f.style && R() < amt) g.style = "motif";
    }
    if (! f.bpm && P.bpm && R() < amt * 0.5) g.bpm = (int) clampv (jsRound (lerp (P.bpm->first, P.bpm->second, R())), 60.0, 200.0);
    g.styled = true; g.styAmt = amt; g.styName = P.name;
    return g;
}

static std::string trimLower (const std::string& p)
{
    size_t a = 0, b = p.size();
    while (a < b && (p[a] == ' ' || p[a] == '\t' || p[a] == '\n' || p[a] == '\r')) ++a;
    while (b > a && (p[b - 1] == ' ' || p[b - 1] == '\t' || p[b - 1] == '\n' || p[b - 1] == '\r')) --b;
    std::string o = p.substr (a, b - a); for (auto& c : o) if ((unsigned char) c < 0x80) c = (char) std::tolower ((unsigned char) c);
    return o;
}

Gen generate (const std::string& prompt, uint32_t seed, StyleArg sty)
{
    const Parsed f = parsePrompt (prompt);
    const std::string pl = trimLower (prompt);
    Rng R (seed ^ hashStr (pl) ^ 0x9e3779b9u);
    for (int i = 0; i < 3; ++i) R();
    const std::vector<std::string>& moods = f.tags;
    const std::string genre = f.genre ? *f.genre : pickW (R, containsS (moods, "chill") ? JMap { { "progressive", 2 }, { "melodic", 2 }, { "house", 2 }, { "lofi", 1 } } : JMap { { "progressive", 4 }, { "melodic", 4 }, { "trance", 1 }, { "house", 1 } });
    const Genre& G = GENRES().at (genre);
    JMap scales = G.scales, presets = G.presets;
    double colour = G.colour, master = G.master; double bpm = G.bpm, oct = G.oct;
    for (auto& m : moods) { const MoodP& M = MOODS().at (m); scales = addW (scales, M.scales, 2); presets = addW (presets, M.presets, 1.5); colour += M.colour; master += M.master; bpm += M.bpm; oct += M.oct; }
    const std::string scale = f.scale ? *f.scale : pickW (R, scales);
    const int key = f.key ? *f.key : std::stoi (pickW (R, minorish (scale)
        ? JMap { { "5", 3 }, { "9", 3 }, { "1", 2 }, { "7", 2 }, { "2", 2 }, { "0", 1 }, { "4", 1 }, { "11", 1 }, { "8", 1 }, { "3", 1 }, { "6", 1 }, { "10", 1 } }
        : JMap { { "0", 2 }, { "5", 2 }, { "7", 2 }, { "2", 2 }, { "9", 1 }, { "8", 2 }, { "3", 2 }, { "10", 1 }, { "1", 1 }, { "4", 1 }, { "6", 1 }, { "11", 1 } }));
    const int bars = f.bars ? *f.bars : std::stoi (pickW (R, G.bars));
    std::vector<std::string> want = moods; want.push_back (genre);
    const std::string fam = tplFamily (scale);
    Tpl pool = tplOf (fam);
    if (fam == "lydian" || fam == "mixolydian") { const auto M = tplOf ("major"); pool.insert (pool.end(), M.begin(), M.begin() + 6); }
    else if (fam == "dorian" || fam == "phrygian") { const auto M = tplOf ("minor"); pool.insert (pool.end(), M.begin(), M.begin() + 8); }
    const JMap tw = tplWeights (pool, want);
    const auto tpl = split (pool[(size_t) std::stoi (pickW (R, tw))].first);
    std::vector<std::string> toks;
    if (bars == 2)
    {
        const auto vs = vampsOf (fam);
        if (R() < 0.5) toks = { tpl[0], tpl[1] }; else toks = split (vs[(size_t) std::floor (R() * (double) vs.size())]);
    }
    else if (bars == 4) toks = tpl;
    else
    {
        const double u = R();
        if (u < 0.45)
        {
            auto v = tpl; const std::vector<std::string> alts = minorish (scale) ? std::vector<std::string> { "VII", "v", "VI", "iv", "III" } : std::vector<std::string> { "V", "IV", "iii", "vi", "ii" };
            std::string r = alts[(size_t) std::floor (R() * (double) alts.size())];
            if (r == v[3]) { size_t i = 0; for (size_t k = 0; k < alts.size(); ++k) if (alts[k] == r) i = k; r = alts[(i + 1) % alts.size()]; }
            v[3] = r; toks = tpl; toks.insert (toks.end(), v.begin(), v.end());
        }
        else if (u < 0.75) { for (auto& t : tpl) { toks.push_back (t); toks.push_back (t); } }
        else { const auto t2 = split (pool[(size_t) std::stoi (pickW (R, tw))].first); toks = tpl; toks.insert (toks.end(), t2.begin(), t2.end()); }
        if (bars == 16) { auto c = toks; toks.insert (toks.end(), c.begin(), c.end()); }
    }
    if (bars == 16 && toks.size() < 16) { auto c = toks; std::vector<std::string> x; for (int k = 0; k < 4; ++k) x.insert (x.end(), c.begin(), c.end()); x.resize (16); toks = x; }
    colour = clampv ((f.colour ? *f.colour : colour) + (R() - 0.5) * 0.16, 0.0, 1.0);
    std::vector<Slot> prog;
    for (auto& tok : toks) { Slot s; s.tok = tok; s.r = R(); s.r2 = R(); s.alt = 0; s.len = 4; prog.push_back (s); }
    if (f.sus) for (size_t i = 0; i < prog.size(); ++i) if (i % 2 == 1) prog[i].alt = 1;
    const std::string style = f.style ? *f.style : pickW (R, G.styles);
    const std::string preset = f.preset ? *f.preset : pickW (R, presets);
    std::string rate;
    if (f.rate) rate = *f.rate; else if (! G.rate.empty()) rate = G.rate; else rate = (R() < 0.12 && genre != "lofi") ? "1/8" : "1/16";
    if (containsS (moods, "chill") && ! f.rate && R() < 0.4) rate = "1/8";
    if (f.speed > 0) bpm += 8;
    if (f.speed < 0) bpm -= 14;
    const int bpmI = (int) clampv (jsRound (f.bpm ? (double) *f.bpm : bpm + (R() - 0.5) * 4), 60.0, 200.0);
    master = clampv (f.master ? *f.master : master + (R() - 0.5) * 0.1, 0.0, 1.0);
    const int octI = (int) clampv (f.oct ? (double) *f.oct : jsRound (oct), 1.0, 3.0);
    const bool glide = f.glide || preset == "acid";
    const bool pedal = R() < G.pedal;
    const double gate = preset == "keys" || preset == "bell" ? 0.85 : preset == "supersaw" ? 0.7 : preset == "acid" ? 0.6 : 0.5;
    Gen g;
    g.prompt = prompt; g.seed = seed; g.genre = genre; g.moods = moods; g.key = key; g.scale = scale; g.bars = bars; g.colour = colour; g.prog = prog;
    g.style = style; g.preset = preset; g.rate = rate; g.bpm = bpmI; g.master = master; g.oct = octI; g.reg = f.reg ? *f.reg : 0; g.glide = glide;
    g.ratchet = f.ratchet; g.rolling = f.rolling; g.pedal = pedal; g.gate = gate; g.swing = f.groove != 0 ? G.swing : 0; g.groove = f.groove;
    g.fxTone = preset == "acid" ? 0.5 : 0.72;
    g.fxDelay = containsS (moods, "dreamy") || genre == "ambient" ? 0.45 : genre == "techno" ? 0.2 : 0.32;
    g.fxReverb = containsS (moods, "dreamy") || genre == "cinematic" || genre == "ambient" ? 0.5 : 0.3;
    g.fxPump = genre == "lofi" || genre == "ambient" || genre == "cinematic" ? 0.1 : 0.45;
    g.arpSeed = (uint32_t) std::floor (R() * 1e9); g.progSeed = (uint32_t) std::floor (R() * 1e9);
    if (! sty.active()) return g;
    return applyStyle (g, f, sty, seed, pl);
}

std::vector<Slot> newProg (const Gen& S, uint32_t seed, StyleArg sty)
{
    Rng R (seed); const std::string fam = tplFamily (S.scale); const Tpl& pool = tplOf (fam);
    std::vector<std::string> want = S.moods; want.push_back (S.genre.empty() ? "progressive" : S.genre);
    const JMap tw = tplWeights (pool, want);
    int bars = std::max (2, (int) jsRound ((double) progTicks (S.prog) / BAR)); if (! bars) bars = 4;
    auto toks = split (pool[(size_t) std::stoi (pickW (R, tw))].first);
    if (bars == 2) toks.resize (2);
    else if (bars >= 8) { const auto t2 = split (pool[(size_t) std::stoi (pickW (R, tw))].first); toks.insert (toks.end(), t2.begin(), t2.end()); if (bars == 16) { auto c = toks; toks.insert (toks.end(), c.begin(), c.end()); } }
    std::vector<Slot> prog;
    for (auto& tok : toks) { Slot s; s.tok = tok; s.r = R(); s.r2 = R(); prog.push_back (s); }
    if (! sty.active()) return prog;
    const FamProfile* FP = sty.prof->fam (famOf (S.scale)); if (! FP) return prog;
    Rng R2 ((seed ^ 0x51a7e5u)); const double amt = clampv (sty.amt, 0.0, 1.0);
    const bool full = R2() < amt;
    return styleProg (*sty.prof, *FP, prog, bars, full, amt, R2, S.scale);
}

// ---------------------------------------------------------------- mood
MoodResult analyzeMood (const std::vector<Resolved>& chords, const std::string& scale, int bpm, double master, const std::string& preset, const std::string& rate)
{
    double val = 0, ten = 0, lush = 0, n = 0, heroic = 0;
    static const std::map<std::string, double> MB { { "lydian", 0.45 }, { "major", 0.3 }, { "mixolydian", 0.2 }, { "dorian", -0.1 }, { "minor", -0.3 }, { "harmonic", -0.35 }, { "phrygian", -0.5 } };
    static const std::map<std::string, double> MT { { "harmonic", 0.25 }, { "phrygian", 0.3 } };
    const double modeB = MB.count (scale) ? MB.at (scale) : 0, modeT = MT.count (scale) ? MT.at (scale) : 0;
    for (size_t i = 0; i < chords.size(); ++i)
    {
        const auto& c = chords[i]; std::set<int> s; for (int x : c.iv) s.insert (mod12 (x));
        const double w = (i == 0 ? 1.6 : 1) * (c.len ? c.len : 4) / 4.0; const std::string q = triadQuality (c.iv);
        n += w;
        if (q == "maj") val += w; else if (q == "min") val -= w * 0.8; else if (q == "dim") { val -= 0.5 * w; ten += 1.1 * w; } else if (q == "aug") ten += 0.9 * w; else { lush += 0.35 * w; ten += 0.12 * w; }
        if (s.count (4) && s.count (10)) ten += 0.65 * w;
        if (s.count (1)) ten += 0.8 * w;
        if (s.count (6) && s.count (7)) ten += 0.3 * w;
        if (s.count (11) && s.count (4)) lush += 0.9 * w;
        bool ext = false; for (int x : c.iv) if (x >= 12) ext = true; if (ext) lush += 0.75 * w;
        if (s.count (9) && ! s.count (10)) lush += 0.4 * w;
        if (s.count (10) && s.count (3)) lush += 0.3 * w;
        if (c.chrom) ten += 0.35 * w;
        if (c.bass) lush += 0.15 * w;
        const auto& nx = chords[(i + 1) % chords.size()];
        if (q != "min" && q != "dim" && triadQuality (nx.iv) != "min" && mod12 (nx.root - c.root) == 2) heroic += w;
    }
    if (n <= 0) n = 1;
    val = val / n + modeB; ten = ten / n + modeT; lush = lush / n; heroic = heroic / n;
    const double energy = clampv ((bpm - 80) / 70.0 * 0.5 + master * 0.45 + (preset == "supersaw" ? 0.15 : 0) - (rate == "1/8" ? 0.12 : 0), 0.0, 1.0);
    const std::pair<const char*, double> sc[] {
        { "happy", val * 1.25 + 0.15 - lush * 0.65 - ten * 1.2 - (energy > 0.75 ? 0.1 : 0) },
        { "dreamy", lush * 1.0 + (val > -0.35 ? 0.25 : -0.15) - energy * 0.62 - ten * 0.8 },
        { "sad", -val * 1.15 - energy * 0.25 + lush * 0.1 - ten * 0.5 },
        { "tense", ten * 2.1 - 0.35 },
        { "epic", energy * 1.4 + heroic * 0.9 + std::abs (val) * 0.15 - lush * 0.3 - 0.68 } };
    MoodResult m; m.mood = "happy"; double best = -1e9;
    for (auto& p : sc) if (p.second > best) { best = p.second; m.mood = p.first; }
    m.valence = val; m.tension = ten; m.lush = lush; m.energy = energy;
    return m;
}

// ---------------------------------------------------------------- MIDI writer
static void vlq (std::vector<uint8_t>& d, uint32_t n)
{
    uint8_t b[5]; int k = 0; b[k++] = (uint8_t) (n & 0x7f);
    while ((n >>= 7)) b[k++] = (uint8_t) ((n & 0x7f) | 0x80);
    while (k) d.push_back (b[--k]);
}
std::vector<uint8_t> writeMidi (double bpm, const std::vector<MidiTrack>& tracks)
{
    std::vector<uint8_t> bytes;
    auto u32 = [] (std::vector<uint8_t>& d, uint32_t v) { d.push_back ((uint8_t) (v >> 24)); d.push_back ((uint8_t) (v >> 16)); d.push_back ((uint8_t) (v >> 8)); d.push_back ((uint8_t) v); };
    auto chunk = [&] (const char* id, const std::vector<uint8_t>& data) { for (int i = 0; i < 4; ++i) bytes.push_back ((uint8_t) id[i]); u32 (bytes, (uint32_t) data.size()); bytes.insert (bytes.end(), data.begin(), data.end()); };
    const size_t nt = tracks.size() + 1;
    chunk ("MThd", { 0, 1, (uint8_t) (nt >> 8), (uint8_t) nt, 0, (uint8_t) PPQ });
    const uint32_t us = (uint32_t) jsRound (60e6 / bpm); const std::string name = "ArpEZator";
    std::vector<uint8_t> t0 { 0, 0xff, 0x03, (uint8_t) name.size() }; t0.insert (t0.end(), name.begin(), name.end());
    const uint8_t rest[] { 0, 0xff, 0x51, 3, (uint8_t) ((us >> 16) & 255), (uint8_t) ((us >> 8) & 255), (uint8_t) (us & 255), 0, 0xff, 0x58, 4, 4, 2, 24, 8, 0, 0xff, 0x2f, 0 };
    t0.insert (t0.end(), std::begin (rest), std::end (rest));
    chunk ("MTrk", t0);
    for (auto& tr : tracks)
    {
        struct Ev { int t, ord; uint8_t st, p, v; };   // ord: 0 note-off, 1 controller / bend, 2 note-on (same tick)
        std::vector<Ev> ev;
        for (auto& n : tr.notes)
        {
            const int v = (int) clampv (jsRound (n.v * 127), 1.0, 127.0);
            ev.push_back ({ n.t, 2, (uint8_t) (0x90 | tr.ch), (uint8_t) n.p, (uint8_t) v });
            ev.push_back ({ n.t + std::max (1, n.d), 0, (uint8_t) (0x80 | tr.ch), (uint8_t) n.p, 0 });
        }
        for (auto& c : tr.ctl)
        {
            if (c.kind == 0) { const int b = clampv (c.val, 0, 16383); ev.push_back ({ c.t, 1, (uint8_t) (0xE0 | tr.ch), (uint8_t) (b & 127), (uint8_t) (b >> 7) }); }
            else ev.push_back ({ c.t, 1, (uint8_t) (0xB0 | tr.ch), (uint8_t) clampv (c.num, 0, 127), (uint8_t) clampv (c.val, 0, 127) });
        }
        std::stable_sort (ev.begin(), ev.end(), [] (const Ev& a, const Ev& b) { return a.t != b.t ? a.t < b.t : a.ord < b.ord; });
        std::vector<uint8_t> d { 0, 0xff, 0x03, (uint8_t) tr.name.size() }; d.insert (d.end(), tr.name.begin(), tr.name.end());
        int last = 0;
        for (auto& e : ev) { vlq (d, (uint32_t) (e.t - last)); d.push_back (e.st); d.push_back (e.p); d.push_back (e.v); last = e.t; }
        d.push_back (0); d.push_back (0xff); d.push_back (0x2f); d.push_back (0);
        chunk ("MTrk", d);
    }
    return bytes;
}
} // namespace arp
