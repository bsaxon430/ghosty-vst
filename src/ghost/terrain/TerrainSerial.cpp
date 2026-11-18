#include "TerrainSerial.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <vector>

namespace ghost::terrain
{
std::string toString (const Terrain& t)
{
    char buf[256];
    // v1: a land saved before the islands (its own noise; it keeps its sound); v2: the islands
    std::snprintf (buf, sizeof (buf), "v%d n=%d g=%.9g e=%.9g p=%.9g f=%d z=%.9g s=%u", t.version >= 2 ? 2 : 1, t.size(), (double) t.grit, (double) t.evolve,
                   (double) t.phase, t.frozen ? 1 : 0, (double) t.freezeZ, (unsigned) t.seed);
    std::string s = buf;
    // ring labels: L=<x>,<z>,<level>,<text as hex>
    for (auto& l : t.labels)
    {
        if (l.level == 0) continue;
        std::snprintf (buf, sizeof (buf), " L=%.6g,%.6g,%d,", (double) l.x, (double) l.z, (int) l.level);
        s += buf;
        for (const char* c = l.text; *c != 0; ++c) { std::snprintf (buf, sizeof (buf), "%02x", (unsigned) (unsigned char) *c); s += buf; }
    }
    for (auto& p : t)
    {
        std::snprintf (buf, sizeof (buf), " | %.9g %.9g %.9g %.9g %.9g %.9g", (double) p.x, (double) p.z, (double) p.height, (double) p.width,
                       (double) p.depth, (double) p.rough);
        s += buf;
    }
    return s;
}

bool fromString (const std::string& s, Terrain& out)
{
    if (s.compare (0, 2, "v1") != 0 && s.compare (0, 2, "v2") != 0) return false;
    Terrain t;
    t.version = s.compare (0, 2, "v2") == 0 ? 2 : 1;
    std::vector<std::string> parts;
    {
        std::string cur;
        std::stringstream ss (s);
        std::string tok;
        while (ss >> tok)
        {
            if (tok == "|") { parts.push_back (cur); cur.clear(); }
            else cur += (cur.empty() ? "" : " ") + tok;
        }
        parts.push_back (cur);
    }
    if (parts.empty()) return false;
    {
        std::stringstream hs (parts[0]);
        std::string tok;
        while (hs >> tok)
        {
            const auto eq = tok.find ('=');
            if (eq == std::string::npos) continue;
            const std::string k = tok.substr (0, eq);
            const double v = std::atof (tok.c_str() + eq + 1);
            if (k == "g") t.grit = (float) v;
            else if (k == "e") t.evolve = (float) v;
            else if (k == "p") t.phase = (float) v;
            else if (k == "f") t.frozen = v != 0.0;
            else if (k == "z") t.freezeZ = (float) v;
            else if (k == "s") t.seed = (uint32_t) std::strtoul (tok.c_str() + eq + 1, nullptr, 10);
            else if (k == "L")
            {
                RingLabel l;
                float x = 0.5f, z = 0.5f; int lev = 0;
                const char* q = tok.c_str() + eq + 1;
                char hex[64] = {};
                if (std::sscanf (q, "%f,%f,%d,%63s", &x, &z, &lev, hex) >= 3)
                {
                    l.x = x; l.z = z; l.level = (int8_t) lev;
                    size_t n = 0;
                    for (size_t h = 0; hex[h] != 0 && hex[h + 1] != 0 && n + 1 < sizeof (l.text); h += 2) { char two[3] = { hex[h], hex[h + 1], 0 }; l.text[n++] = (char) std::strtol (two, nullptr, 16); }
                    if (l.level != 0) t.addLabel (l);
                }
            }
        }
    }
    for (size_t i = 1; i < parts.size(); ++i)
    {
        std::stringstream ps (parts[i]);
        Peak p;
        double x, z, h, w, d, r;
        if (! (ps >> x >> z >> h >> w >> d >> r)) continue;
        p.x = (float) x; p.z = (float) z; p.height = (float) h; p.width = (float) w; p.depth = (float) d; p.rough = (float) r;
        t.add (p);
    }
    t.clampAll();
    out = t;
    return true;
}
}
