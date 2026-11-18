#pragma once
// LINEAR DRUMMING settings: plain data with no dependencies, so gb::Model can hold it by value (BeatModel.h includes this
// header; Linear.h, which needs the engine, includes BeatEngine.h). See Linear.h for the integration note.
#include <cstdint>
#include <array>

namespace gb::linear {

enum Rule : uint8_t { RulePriority = 0, RuleAccents, RuleAlternate, RuleGroove, RuleRandom, RuleCount };

struct Settings {
    bool on = false;            // LINEAR button
    uint8_t rule = RulePriority;
    float strictness = 0.25f;   // STRICTNESS: tolerance window = strictness x one 1/16 (0 = exact simultaneity only, 1 = a whole 1/16 = 0.25 beats)
    bool move = true;           // MOVE: losers go to the nearest free slot; false = DROP them
    uint16_t lock = 0;          // LOCK lanes: bit t set = every hit of track t survives
    int variation = 0;          // VARIATION < >: 0 = the rule as written, 1.. = seeded shake-ups (different winners / move directions)
    int seed = 0;               // with `variation` the seed of those shake-ups: set ONCE when LINEAR is first switched on (0 = not yet; NEW SEED rerolls)
    std::array<uint8_t, 16> order {};   // PRIORITY rule: all 0 = automatic (kick, snare, 808, perc, open hat, hat, break, other); else a full 1..16 ranking
    bool operator==(const Settings& o) const { return on == o.on && rule == o.rule && strictness == o.strictness && move == o.move && lock == o.lock && variation == o.variation && seed == o.seed && order == o.order; }
    bool operator!=(const Settings& o) const { return !(*this == o); }
};

} // namespace gb::linear
