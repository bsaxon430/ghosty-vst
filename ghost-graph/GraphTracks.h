// Ghost Graph -- the factory tracks: each one is a reference video, scene for scene, equation for equation.
// (mod binds tighter than + and -, and looser than a product: "3n mod 8" is mod(3n, 8), "7 - j mod 2" is 7 - mod(j, 2).)
#pragma once
#include "GraphExpr.h"

namespace gg {

// wave: 0 sine, 1 triangle, 2 saw, 3 square, 4 soft saw, 5 round; tune: semitones from A3 for y = 0 (measured from each video's sound)
// pitches: octaves per unit of y for each scene (1 if not given); look: the look it switches to (-1: leave it)
struct Track { std::string name; double bpm = 120; std::vector<std::string> scenes; int wave = 3; int tune = 0; float tone = 20000.f; std::vector<double> speeds {}; std::vector<double> pitches {}; int look = -1; };

inline std::vector<Track> factoryTracks()
{
    std::vector<Track> t;

    // ---------------------------------------------------------------- 1: the first video (kick, chords, motor, sub)
    {
        Track k { "Track 1", 75, {}, 4, -7 };   // SOFT SAW, y = 0 is D3
        for (auto& s : factoryScenes()) { k.scenes.push_back (s.text); k.speeds.push_back (s.speed); }
        t.push_back (k);
    }

    // ---------------------------------------------------------------- 2: kick, riff, clap, lead, fifth, hats, arp, pad
    {
        const std::string common = R"GG(
g = v > 0 and (f < 0.7 or (k = 14 and mod(c, 2) = 1))
d = mod(v, 5) - 1
o = v > 4
S(i) = floor((12i + 1)/7)
v = mod(floor(N/10^(15 - k)), 10)
N = {c = 0: NA, c = 1: NB, c = 2: NC, ND}
)GG";
        const std::string clock = R"GG(
k = mod(n, 16)
c = floor(n/16)
n = floor(4x) + 32
f = frac(4x)
b = frac(x)
)GG";
        const std::string drums = R"GG(
kick = {b < 1/3: 2.5e^(-20b) - 1.75}
riff = {g: (S(d) + 12o)/12 - 2 + 0.2e^(-9f)}
clap = {mod(k, 8) = 4 and f < 0.75: 1.3 - f}
)GG";
        const std::string arpW = R"GG(
arp = {w > 0 and f < 0.7: {w = 3: 7, w = 4: 10, w = 5: 5, w = 6: 3, 12w - 12}/12}
w = mod(floor({c = 0: WA, c = 1: WB, c = 2: WC, WD}/10^(15 - k)), 10)
WC = WA
)GG";
        Track k { "Track 2", 120, {}, 5, 0 };   // ROUND on A3
        k.scenes.push_back ("y = outline(kick, riff, clap)" + drums + common
            + "NA = 1001001300206010\nNB = 1001002006030134\nNC = NA\nND = NB" + clock);
        k.scenes.push_back ("y = outline(kick, riff, clap, lead, fifth)\n"
            "fifth = {g and (c < 3 or k < 9): (S(d + 4) + 12o)/12 + a2 + 0.1e^(-9f)}\n"
            "lead = {c = 3 and k > 9: {f < 0.7: {mod(k - 10, 3) = 0: -1, mod(k - 10, 3) = 1: -5/12, 0} + 0.1e^(-9f)}, {g and (c < 3 or k != 9): (S(d + 2) + 12o)/12 + a + 0.1e^(-9f)}}\n"
            "a = -1\na2 = a" + drums + common
            + "NA = 1001001300206010\nNB = 1001002006030134\nNC = NA\nND = NB" + clock);
        k.scenes.push_back ("y = outline(kick, riff, clap, hats, riser, lead, fifth, pad)\n"
            "hats = {c > 1 and f < {mod(k, 4) = 2: 0.5, 0.25}: 1.7 - 0.25(mod(k, 4) != 2) - f}\n"
            "riser = 3clamp((x + 4)/12, 0, 1) - 1\n"
            "pad = {c < 2: 0, c = 2: 1, 7}/12\n"
            "lead = {g: (S(d + 2) + 12o)/12 + a + 0.1e^(-9f)}\n"
            "fifth = {g: (S(d + 4) + 12o)/12 + a2 + 0.1e^(-9f)}\n"
            "a = -1\na2 = a" + drums + common
            + "NA = 1001001300206010\nNB = 1001002006030134\nNC = NA\nND = NB" + clock);
        k.scenes.push_back ("y = outline(kick, riff, clap, hats, arp, lead, fifth, pad)\n"
            "pad = {c < 2: 12, c = 2: 13, 7}/12\n"
            "WA = 2201022201020110\nWB = 2011020102201112\nWD = 2011020102132132\n"
            "a = 0\na2 = -1\n"
            "NA = 1001001306206016\nNB = 1001062006030134\nND = 1001002006031634\n"
            "hats = {f < {mod(k, 4) = 2: 0.5, 0.25}: 1.7 - 0.25(mod(k, 4) != 2) - f}\n"
            "lead = {g: (S(d + 2) + 12o)/12 + a + 0.1e^(-9f)}\n"
            "fifth = {g: (S(d + 4) + 12o)/12 + a2 + 0.1e^(-9f)}" + drums + arpW + common + "NC = NA" + clock);
        k.scenes.push_back ("y = outline(kick, riff, clap, ohat, arp, fall, lead)\n"
            "fall = 1 - 3clamp((x + 4)/12, 0, 1)\n"
            "WA = 2000102000011000\nWB = 2001002000100000\nWD = 2001002000245361\n"
            "lead = {g and c < 2: (S(d + 2) + 12o)/12 + a + 0.1e^(-9f)}\n"
            "ohat = {mod(k, 4) = 2 and f < 0.5: 1.7 - f}\n"
            "a = -1" + drums + arpW + common
            + "NA = 1001001300206010\nNB = 1001002006030134\nNC = NA\nND = NB" + clock);
        t.push_back (k);
    }

    // ---------------------------------------------------------------- 3: boom, snare, hats, drum, bell, acid, stab, tom, drone
    {
        Track k { "Track 3", 137, {}, 5, 0 };   // ROUND on A3
        k.scenes.push_back (R"GG(y = outline(boom, snare, hats)
boom = (3mod(floor(x/4), 2) - 2(x >= 0) - 6(x >= 13/2) - 24)/12 + 2.5e^(-18h)
h = min(mod(x, 4), mod(x - 5/2, 4))
snare = {mod(x - 2, 4) < 3/16: 1.3 - 4mod(x - 2, 4)}
hats = {frac(R x) < 0.5: 1.2 + 0.4frac(x) - frac(R x)/2}
R = {mod(j, 8) = 7: 8, mod(j, 4) = 3: 6, 2 + 2mod(j, 2)}
j = floor(x)
)GG");
        k.scenes.push_back (R"GG(y = outline(kick, drum, clap, hats)
kick = {b < 1/3: 2.5e^(-20b) - 1.75}
drum = {mod(5n + 10, 16) < 5 and f < 0.95: (floor((12d + 3)/5) - 21)/12 + 0.3e^(-4f)}
d = mod(floor((5n + 10)/16), 4) + 2mod(floor(x/4), 2)
clap = {mod(n, 8) = 4 and f < 0.75: 1.3 - f}
hats = {f < 0.35: 1.1 + 0.4(mod(n, 2) = 1) - f}
n = floor(4x)
f = frac(4x)
b = frac(x)
)GG");
        k.scenes.push_back (R"GG(y = outline(boom, snare, hats, bell)
boom = (-24 - 2(mod(x, 8) >= 4))/12 + 2.5e^(-18h)
h = min(mod(x, 2), mod(x - 3/4, 4))
snare = {mod(x - 1, 2) < 3/16: 1.3 - 4mod(x - 1, 2)}
hats = {f < 0.4: 1.2 + 0.3(mod(n, 2) = 0) - f}
bell = {mod(7n, 16) < 7 and f < 0.8: (floor((12d + 1)/7) - 2)/12}
d = 7 - mod(floor(7n/16), 8)
n = floor(4x)
f = frac(4x)
)GG");
        k.scenes.push_back (R"GG(y = outline(kick, acid, clap, hats)
kick = {b < 1/3: 2.5e^(-20b) - 1.75}
acid = {f < 0.7 or mod(3n, 8) < 3: (floor((12d + 1)/7) + 12(mod(5n, 16) < 5) - 24)/12}
d = mod(3n, 8)
clap = {mod(n, 8) = 4 and f < 0.75: 1.3 - f}
hats = {mod(n, 4) = 2 and f < 0.5: 1.7 - f}
n = floor(4x)
f = frac(4x)
b = frac(x)
)GG");
        k.scenes.push_back (R"GG(y = outline(boom, snare, hats)
boom = (r(q) + (r(q + 1) - r(q))clamp(4u - 3, 0, 1) - 24)/12 + 2.5e^(-18mod(x, 2))
r(i) = 3mod(i, 2) - 2mod(floor(i/2), 2) - 6(i = 3)
q = floor(x/2)
u = frac(x/2)
snare = {c < 3/16: 1.3 - 4c}
c = min(mod(x - 2, 4), mod(x - 31/4, 8))
hats = {g < 0.4: 1.2 + 0.3(mod(floor(3x), 3) = 0) - g}
g = frac(3x)
)GG");
        k.scenes.push_back (R"GG(y = outline(kick, sub, clap, hats, stab)
kick = {mod(5n + 50, 16) < 5 and f < 0.9: 2.5e^(-5f) - 1.75}
sub = (-24 - 2(mod(x, 8) >= 4))/12
clap = {mod(n, 8) = 4 and f < 0.75: 1.3 - f}
hats = {f < 0.3: 1.2 + 0.3(mod(n, 2) = 0) - f}
stab = {mod(3n, 8) < 3 and f < 0.6: (7mod(m, 2)(p = 1) + floor((12mod(m, 4) + 13)/7)(p = 2))/12}
p = mod(floor(3n/8), 3)
m = floor(n/8)
n = floor(4x)
f = frac(4x)
)GG");
        k.scenes.push_back (R"GG(y = outline(kick, acid, clap, hats)
kick = {b < 1/3: 2.5e^(-20b) - 1.75}
acid = {f < 0.7 or mod(n, 3) = 2: (floor((12d + 1)/7) + 12(mod(n, 5) = 0) - 24)/12}
d = mod(4n, 7)
clap = {mod(n, 8) = 4 and f < 0.75: 1.3 - f}
hats = {mod(n, 4) = 2 and f < 0.5: 1.7 - f}
n = floor(4x)
f = frac(4x)
b = frac(x)
)GG");
        k.scenes.push_back (R"GG(y = outline(kick, tom, rim, hats, drone)
kick = {mod(mod(n, 16), 6) = 0 and f < 0.9: 2.5e^(-5f) - 1.75}
tom = {mod(5n + 6, 16) < 5 and f < 0.8: (5mod(n, 2) - 14)/12 + 0.5e^(-8f)}
rim = {mod(n, 8) = 4 and f < 0.3: 1.1 - f}
hats = {f < 0.3: 1 + 0.4(mod(n, 4) = 2) - f}
drone = ((B > 0) + 6(B = 2) - 12)/12
B = floor(x/4) + 2
n = floor(4x)
f = frac(4x)
)GG");
        t.push_back (k);
    }

    // ---------------------------------------------------------------- 4: the melody built from cosines, then kick, bass, arp, harm
    {
        const std::string clock = R"GG(
B(u) = clamp(144cos(pi u), -1, 1)
n = floor(4x)
k = mod(n, 16)
f = frac(4x)
j = floor(x)
b = frac(x)
)GG";
        const std::string hatsFor = "hats = {f < 0.25 - 0.07(mod(k, 4) != 2) and mod(floor(%/2^mod(n, 32)), 2) = 1: 2.4 - 2.5f - 0.25(mod(k, 4) != 2)}\n";
        auto hats = [&] (const char* num) { std::string s = hatsFor; s.replace (s.find ('%'), 1, num); return s; };
        const std::string clap = "clap = {mod(j, 2) = 1 and b < 1/10: 2.3 - 6b}\n";
        const std::string r = "r = 5(1 - B(x/2))/2\n";
        const std::string tw = R"GG(arp = h(T)/12 - 1
harm = h(T - 2)/12 - 1
T = W + mod(R + 3 + B(2x) - 2B(x), 7)
W = 7(2 + B(x/4) + B(x/8))/2
h(d) = floor((12d + 2)/7) + (q = 3)(mod(d, 7) = 6)
q = floor((x + 8)/4)
)GG";
        Track k { "Track 4", 130, {} };
        k.scenes.push_back (R"GG(y = (24B(x/8) + 12B(x/4) + 7B(x/2) + 5B(x) + 3B(2x) + 3)/24
B(u) = clamp(18cos(pi u), -1, 1)
)GG");
        k.scenes.push_back ("y = outline(kick, bass, arp)\n"
            "kick = {b < 1/3: -2 + 2.5e^(-20b)}\n"
            "bass = {mod(floor(27724/2^k), 2) = 1 and f < 0.8: (r - 24 + v)/12}\n"
            "v = 7(p = 1) + (12 - 2mod(j, 2))(p = 3)\n"
            "p = mod(n, 4)\n"
            "arp = K/24\n"
            "K = 12B(x/8) + 12B(x/4) + 7B(x/2) + 5B(x) + 3B(2x) + 15\n" + r + clock);
        k.scenes.push_back ("y = outline(kick, bass, clap, hats, arp, harm)\n"
            "kick = {b < 1/3: -2 + 2.5e^(-20b)}\n"
            "bass = {mod(floor(58956/2^k), 2) = 1 and f < 0.8: (r - 24 + v)/12}\n"
            "v = 7(p = 1) + (12 - 2mod(j, 2))(p = 3)\n"
            "p = mod(n, 4)\n" + clap + hats ("2349088964") +
            "arp = K/24\n"
            "harm = (K/2 - 3 - 2(c = 0) - (c = 7))/12\n"
            "c = mod(K/2 - r, 12)\n"
            "K = 12B(x/8) + 12B(x/4) + 7B(x/2) + 5B(x) - 3B(2x) + 15\n" + r + clock);
        k.scenes.push_back ("y = outline(kick, bass, clap, hats, arp, harm)\n"
            "kick = {b < 1/3: -2 + 2.5e^(-20b)}\n"
            "bass = {mod(floor(44718/2^k), 2) = 1 and f < 0.8: h(R + v)/12 - 2}\n"
            "v = 4(p = 1) + (7 - mod(j, 2))(p = 3)\n"
            "p = mod(n, 4)\n" + clap + hats ("3229879364") + tw +
            "R = mod(floor(534/10^(3 - q)), 10)\n" + clock);
        k.scenes.push_back ("y = outline(kick, bass, clap, hats, arp, harm)\n"
            "kick = {b < 1/2: h(R)/12 - 2 + 3e^(-30b)}\n"
            "bass = {mod(floor(44718/2^k), 2) = 1 and f < 0.8: h(R + v)/12 - 2}\n"
            "v = 4(p = 1) + (7 - mod(j, 2))(p = 3)\n"
            "p = mod(n, 4)\n" + clap + hats ("2252620876") + tw +
            "R = mod(floor(5624/10^(3 - q)), 10)\n" + clock);
        k.scenes.push_back ("y = outline(kick, bass, clap, hats, arp)\n"
            "kick = {b < 1/2: r/12 - 2 + 3e^(-30b)}\n"
            "bass = {mod(floor(44718/2^k), 2) = 1 and f < 0.8: (r - 24 + v)/12}\n"
            "v = 7(p = 1) + (12 - 2mod(j, 2))(p = 3)\n"
            "p = mod(n, 4)\n" + clap + hats ("1678558404") +
            "arp = K/24 - 3max(0, x - 7)^2\n"
            "K = 12B(x/8) + 12B(x/2) + 7B(x/2) - 5B(x) - 3B(2x) + 15\n" + r + clock);
        k.scenes.push_back (R"GG(y = outline(kick, bass, clap, hats, arp)
kick = {x < 4 and b < 1/3: -2 + 2.5e^(-20b)}
bass = {x < 4 and mod(floor(58956/2^k), 2) = 1 and f < 0.8: (r - 24 + v)/12}
v = 7(p = 1) + (12 - 2mod(j, 2))(p = 3)
p = mod(n, 4)
clap = {x < 4 and mod(j, 2) = 1 and b < 1/10: 2.3 - 6b}
hats = {x < 4 and f < 0.25 - 0.07(mod(k, 4) != 2) and mod(floor(2349088964/2^mod(n, 32)), 2) = 1: 2.4 - 2.5f - 0.25(mod(k, 4) != 2)}
arp = {x < 4: K/24, x < 6 or frac(8x) < 0.6 - 0.1floor(x - 4): D(x - (x >= 6)mod(n, 2)/4)/24 - 1.2max(0, x - 7)^2}
D(i) = 7B(i/2) + 5B(2i - 1/2) + 3B(4i - 1/2) + 39 - 24floor(i - 4)
K = 12B(x/8) + 12B(x/4) + 7B(x/2) + 5B(x) + 3B(2x) + 15
)GG" + r + clock);
        t.push_back (k);
    }

    // ---------------------------------------------------------------- 5: G chords, hats, acid, bass
    {
        const std::string clock = R"GG(
D(N) = mod(floor(N/10^(3 - b)), 10)
a = q(j = 1) + p(j = 2) + 7(j = 3)
j = mod(floor(3s/8), 4)
k = mod(s, 16)
s = floor(4x)
f = frac(4x)
b = floor((x + 8)/4)
)GG";
        const std::string kick = "kick = {frac(x) < 1/4: -2 + 3e^(-20frac(x))}\n";
        const std::string G = "G = {frac(3s/8) < 3/8: (R + a)/12 - 1 + 0.08e^(-12f), {f < 0.35: R/12 - 1}}\n";
        auto hats = [] (const char* num)
        {
            return std::string ("hats = {f < 0.25 - 0.07(mod(k, 4) != 2) and (mod(floor(") + num
                 + "/2^k), 2) = 1 or (b = 3 and k >= 12)): 2.25 - 2f - 0.3(mod(k, 4) != 2)}\n";
        };
        const std::string acidCore = "(h(n(s)) + 12O + lift + z(h(n(s + 1)) - h(n(s)) - 12O + tgt)clamp(2f - 1, 0, 1))/12 + 0.3(mod(s, 3) = 0)e^(-14f)";
        const std::string acidParts = R"GG(z = k = 3 or k = 13
h(d) = floor((12d + 3)/5)
n(v) = (u(v) > 0)(mod(u(v) - 1 + w(v), 4) + 1)
u(v) = mod(floor(H(v)/10^(7 - mod(v, 8))), 10)
H(v) = {mod(floor(v/8), 2) = 0: 203043, 403210}
)GG";
        Track k { "Track 5", 130, {}, 3, -5, 2200.f };   // a darker square on E3 (measured from the video), TONE at 2.2 kHz
        k.scenes.push_back (R"GG(y = [{frac(x) < 1/4: -2 + 3e^(-20frac(x))}, G, G + 7/12]
G = {frac(3s/8) < 3/8: (3(j = 1) + 5(j = 2) + 7(j = 3))/12 - 1 + 0.08e^(-12f), {f < 0.35: -1}}
j = mod(floor(3s/8), 4)
s = floor(4x)
f = frac(4x)
)GG");
        k.scenes.push_back ("y = [kick, G, G + 7/12, hats]\n" + kick + G + hats ("50756")
            + "R = mod(floor(12120810/100^(3 - b)), 100) - 12\nq = D(3322)\np = D(5545)" + clock);
        k.scenes.push_back ("y = [kick, G, G + 7/12, hats, acid]\n" + kick + G + hats ("58956")
            + "acid = {mod(floor(27995/2^k), 2) = 1 and (f < 0.8 or z = 1): " + acidCore + "}\n" + acidParts
            + "w(v) = 0\nO = k = 6\n"
              "lift = 2(k = 10)(mod(b, 2) = 1) + 4(k = 14)(b >= 2) + 12(k = 8)(b = 1) + 12(k = 0)(b = 1)\n"
              "tgt = 4(k = 13)(b >= 2)\n"
              "R = mod(floor(12120810/100^(3 - b)), 100) - 12\nq = D(3322)\np = D(5545)" + clock);
        k.scenes.push_back ("y = [kick, G, G + 7/12, hats, acid]\n" + kick + G + hats ("61132")
            + "acid = {mod(floor(27995/2^k), 2) = 1 and (f < 0.8 or z = 1): " + acidCore + "}\n" + acidParts
            + "w(v) = 0\nO = k = 6\n"
              "lift = 2(k = 10)(mod(b, 2) = 1) + 4(k = 14)(b >= 2) + (k = 4)(b = 2) + (k = 6)(b = 3) + 2(k = 10)(b = 3) + 12(k = 0)(b = 0)\n"
              "tgt = 4(k = 13)(b >= 2) + (k = 3)(b = 2)\n"
              "R = mod(floor(12120807/100^(3 - b)), 100) - 12\nq = D(3325)\np = D(5547)" + clock);
        k.scenes.push_back ("y = outline(kick, hats, acid, bass)\n" + kick + hats ("61132")
            + "acid = {b = 3 and k >= 12 and f < 0.8: (10 + 2(k - 12) + (k >= 14))/12 + 0.3(mod(s, 3) = 0)e^(-14f), "
              "mod(floor(27995/2^k), 2) = 1 and (f < 0.8 or z = 1): " + acidCore + "}\n" + acidParts
            + "w(v) = 3mod(floor(v/8), 4)\nO = k = 6 or k = 14\nlift = 0\ntgt = 0\n"
              "bass = {b = 3 and k >= 12: {f < 0.8: (2(k - 12) + (k >= 14) - 2)/12 - 1 + 0.25(mod(s, 3) = 0)e^(-9f)}, "
              "(R + 12(r5 = 2) - 2(r5 = 4)(1 - e^(-2f)))/12 - 1 + 0.25(mod(s, 3) = 0)e^(-9f)}\n"
              "r5 = mod(s + 2, 5)\n"
              "R = mod(floor(12120810/100^(3 - b)), 100) - 12\nq = D(3322)\np = D(5545)" + clock);
        k.scenes.push_back ("y = [kick, G, G + 7/12, hats, acid, bass]\n" + kick
            + "G = {b >= 2 and frac(3s/8) < 3/8: (R + a)/12 - 1 + 0.08e^(-12f), {b >= 2 and f < 0.35: R/12 - 1}}\n"
              "hats = {b < 3 and f < 0.25 - 0.07(mod(k, 4) != 2) and mod(floor(61132/2^k), 2) = 1: 2.25 - 2f - 0.3(mod(k, 4) != 2)}\n"
              "acid = {b = 3 and k >= 12 and f < 0.8: (7 - 2(k - 12) + (k = 15))/12 + 0.3(mod(s, 3) = 0)e^(-14f), "
              "mod(floor(27995/2^k), 2) = 1 and (b < 3 or k <= 10) and (f < 0.8 or z = 1): " + acidCore + "}\n" + acidParts
            + "w(v) = 0\nO = k = 6\n"
              "lift = 2(k = 10)(mod(b, 2) = 1) + 4(k = 14)(b = 2) + (k = 4)(b = 2) + (k = 6)(b = 3) + 2(k = 10)(b = 3)\n"
              "tgt = 4(k = 13)(b = 2) + (k = 3)(b = 2)\n"
              "bass = {b = 3 and k >= 12: {f < 0.8: (7 - 2(k - 12) + (k = 15))/12 - 1 + 0.25(mod(s, 3) = 0)e^(-9f)}, "
              "(R + 12(R < -3) + 12(r5 = 2) - 2(r5 = 4)(1 - e^(-2f)))/12 - 1 + 0.25(mod(s, 3) = 0)e^(-9f)}\n"
              "r5 = mod(s + 2, 5)\n"
              "R = mod(floor(12121707/100^(3 - b)), 100) - 12\nq = D(3335)\np = D(5557)" + clock);
        t.push_back (k);
    }
    // ---------------------------------------------------------------- 6: the burgundy video (nine scenes, 80 BPM, soft saw on D3)
    {
        Track k { "Track 6", 80, {}, 4, -7 };
        k.scenes.push_back (R"GG(y = [kick, G(0), G(0.566), G(1.163), G(1.465), G(1.963), H(0), H(7)]
kick = {w < 0.1: 1.3e^(-30w) - 2}
G(o) = {w < 0.9: o - 1 - 0.04w + 0.03e^(-25w)}
H(i) = {w > 0.35 and w < 0.96: (i + 3c clamp((w - 0.6)/0.15, 0, 1) - clamp((w - 0.88)/0.08, 0, 1))/12 - 1}
w = frac(x/8)
c = floor(x/8) + 1
)GG");
        k.scenes.push_back (R"GG(y = [kick, A(1, 0), A(2, 6), A(3, 13), whine]
kick = {h < 0.14: 1.3e^(-30h) - 2, {h > 0.28 and h < 0.4: 0.8e^(-30(h - 0.28)) - 2}}
A(n, i) = {c > n - 0.5 and q < 0.95: (i + (c = 3)(n = 3)clamp((q - 0.5)/0.3, 0, 1))/12 - 1}
whine = 19/12 + u/6
h = frac(6u + 6u^2)
u = frac(x/16 + 0.5)
q = frac(x/4)
c = floor(x/4) + 2
)GG");
        k.scenes.push_back (R"GG(y = [kick, wing, H(0, 0), H(7, 18), H(14, 27)]
kick = {w < 0.1: 1.3e^(-30w) - 2}
wing = {frac(4.5x - 6sin(pi x/4)/pi) < 0.5: -1 + 7s/12}
H(i, j) = {w < 0.96: (i + c(j - i) + 2s)/12 - 1}
s = (sin(pi x/8))^2
w = frac(x/8)
c = floor(x/8) + 1
)GG");
        k.scenes.push_back (R"GG(y = [kick, chant, chant + 7/12, chant + 1, H(0), H(7), R(7, -4), R(10, 0), R(13, 4)]
R(i, a) = {x > a and x < 7.7: (i + 3(x - a)/8)/12}
kick = {mod(m, 8) = 0 and f < 0.8: 1.3e^(-5f) - 2}
chant = {D(m) < 9 and f < 0.8: (D(m) - 2)/12 - 1}
D(j) = mod(floor(2292203222922350/10^(15 - j)), 10)
H(i) = {frac(x/8) < 0.95: i/12 - 2}
m = mod(floor(2x) + 16, 16)
f = frac(2x)
)GG");
        k.scenes.push_back (R"GG(y = [kick, U(0), U(7), U(12), U(15)]
kick = {w < 0.1: 1.4e^(-30w) - 2}
U(i) = {w < 0.5: (i + 3c - clamp((w - 0.3)/0.2, 0, 1))/12 - 1, {w > 0.55: (i + 3(1 - c)r)/12 - 2 + r}}
r = ((w - 0.55)/0.45)^3
w = frac(x/8)
c = floor(x/8) + 1
)GG");
        k.scenes.push_back (R"GG(y = [kick, tom, N(0), N(7), N(12), C(14), C(15)]
C(i) = {x > 0 and x < 7.8: (i + x/4)/12}
kick = {g < 0.8: 1.3e^(-5g) - 2}
tom = {(m = 3 or m = 8 or m > 12.5) and f < 0.6: 0.6e^(-9f) - 1.2 + (m - 13)(m > 12.5)/8}
N(i) = {g < 3.5: (i + (G = 10) - 2clamp((g - 1.5)/2, 0, 1))/12 - 1}
g = m + f - G
G = {m < 5: 0, m < 10: 5, 10}
m = mod(floor(2x) + 16, 16)
f = frac(2x)
)GG");
        k.scenes.push_back (R"GG(y = [kick, S(0, -1, 0, 0), S(1, 1, 7, -6), S(-5, 0, -9, 3), -2, H(7)]
kick = {v < 0.11: 1.7e^(-25v) - 1.8}
v = frac(frac(x/4)2^n)/2^n
n = floor(x/4) + 2
S(i, a, j, b) = {frac(8x) < 0.5 and w < 0.92: ((i + a o)(1 - c) + (j + b o)c)/12}
H(i) = {w < 0.96: (i - c o)/12 - 2}
o = clamp((w - 0.3)/0.4, 0, 1)
w = frac(x/8)
c = floor(x/8) + 1
)GG");
        k.scenes.push_back (R"GG(y = [kick, tom, chug, top, top - 1, low]
kick = {(s = 0 or s = 3 or s = 6 or s = 8 or s = 11 or s = 14) and g < 0.9: 1.3e^(-4g) - 2}
tom = {(s = 4 or s = 12 or s = 13 or s = 15) and g < 0.8: 0.6e^(-6g) - 1.2 + (s - 12)(s > 11)/10}
chug = {g < 0.55: mod(floor(3001000030015/10^(15 - s)), 10)/12 - 1}
top = {w < 0.94: (7 - 5clamp(1 - w/0.08, 0, 1) + (3 + 3c)clamp((w - 0.4)/0.1, 0, 1) - (8 + 3c)clamp((w - 0.72)/0.22, 0, 1))/12}
low = {w < 0.94: -2}
s = mod(floor(4x) + 64, 16)
g = frac(4x)
w = frac(x/8)
c = floor(x/8) + 1
)GG");
        k.scenes.push_back (R"GG(y = [kick, S(0), S(1), S(2), S(3), S(4), -1, -5/12]
kick = {h < 0.3: 1.5e^(-14h) - 2.1}
h = frac(x/2)
S(k) = 0.3 + k/8 + p + (0.012 + 0.05p)sin((9 + 2.3k)x + 2k)
p = 1 - abs(x)/8
)GG");
        t.push_back (k);
    }
    // ---------------------------------------------------------------- 7: the zeta video -- the Riemann zeta function
    // along the critical line, zeta(1/2 + iu), drawn on the Re / Im plane on grey crumpled paper (black ink, a blue dot),
    // its real and imaginary parts sung by two voices. The video runs u from 0 to about 60 in 20 seconds; one page here
    // (x from -8 to 8) is u = 4x + 32, 0 to 64, at 90 BPM and half speed (0.75 x a second = 3 u a second, as measured).
    // Its two voices glide 7/12 of an octave (a fifth) per unit around D2 (measured: y = 0 at 73 Hz).
    {
        Track k { "Track 7", 90, {}, 2, -19 };   // SAW (its strong second harmonic, as measured), y = 0 is D2
        k.scenes.push_back ("y = plane(zre(u), zim(u))\nu = 4x + 32\n");
        k.speeds = { 0.5 };
        k.pitches = { 7.0 / 12.0 };
        k.look = 5;   // SILVER
        t.push_back (k);
    }

    return t;
}

} // namespace gg
