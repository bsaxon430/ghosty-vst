// Ghost Matter - AGPL-3.0-only (see LICENSE, NOTICE.md). The UNDER THE HOOD tables (HoodDefs.h).
#include "HoodDefs.h"
#include <cstring>
#include <sstream>

namespace matter::hood
{
#include "HoodTables.inc"

namespace
{
constexpr int kN = (int) (sizeof (kHoodTable) / sizeof (kHoodTable[0]));
constexpr int kE = (int) (sizeof (kHoodEngines) / sizeof (kHoodEngines[0]));

// the controls read on the audio thread: Pertylizer, Box2D and the PM-Synth (BOUNCE's struck voices)
bool isLiveEngine (const char* e)
{
    for (auto* n : { "pm", "panner", "fdn", "euclid", "turing", "box2d" }) if (std::strcmp (e, n) == 0) return true;
    return false;
}
struct Index
{
    std::map<std::string, int> byKey;
    int slot[kN];
    int slotTable[kLiveSlots];
    std::map<std::string, int> base;
    Index()
    {
        int next = 0;
        for (int i = 0; i < kN; ++i)
        {
            byKey[std::string (kHoodTable[i].engine) + "." + kHoodTable[i].id] = i;
            slot[i] = -1;
            if (isLiveEngine (kHoodTable[i].engine) && next < kLiveSlots)
            {
                if (! base.count (kHoodTable[i].engine)) base[kHoodTable[i].engine] = next;
                slotTable[next] = i;
                slot[i] = next++;
            }
        }
    }
};
const Index& idx() { static Index i; return i; }
}

int count() { return kN; }
const HoodControlDef& def (int i) { return kHoodTable[i]; }
int indexOf (const std::string& key) { auto& m = idx().byKey; auto it = m.find (key); return it == m.end() ? -1 : it->second; }
std::string keyOf (const HoodControlDef& d) { return std::string (d.engine) + "." + d.id; }
int engineCount() { return kE; }
const HoodEngineDef& engine (int i) { return kHoodEngines[i]; }
int engineIndex (const std::string& key) { for (int i = 0; i < kE; ++i) if (key == kHoodEngines[i].key) return i; return -1; }
std::vector<std::string> choicesOf (const HoodControlDef& d)
{
    std::vector<std::string> r;
    std::stringstream ss (d.choices);
    std::string s;
    while (std::getline (ss, s, '|')) r.push_back (s);
    return r;
}
int liveSlot (int t) { return t >= 0 && t < kN ? idx().slot[t] : -1; }
int liveSlotOf (const char* e, const char* id) { int t = indexOf (std::string (e) + "." + id); return t < 0 ? -1 : idx().slot[t]; }
int liveBase (const char* e) { auto& m = idx().base; auto it = m.find (e); return it == m.end() ? -1 : it->second; }
const HoodControlDef& liveDef (int slot) { return kHoodTable[idx().slotTable[slot]]; }
}
