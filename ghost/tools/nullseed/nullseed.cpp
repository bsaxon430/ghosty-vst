// Ghost Bass null test helper (test only, never shipped): makes Surge XT's random seeds repeatable, so two
// builds of the same engine can be compared sample for sample. Loaded with LD_PRELOAD into the python that
// renders, the SAME way for upstream Surge XT and for Ghost Bass. Neither source tree is changed.
// Surge seeds from the wall clock (std::chrono::system_clock, time()) and std::random_device: here the clock
// is frozen and random_device returns one fixed value.
#include <chrono>
#include <cstdint>
#include <ctime>
#include <random>

extern "C" time_t time(time_t *t)
{
    if (t)
        *t = 1700000000;
    return 1700000000;
}

namespace std
{
namespace chrono
{
inline namespace _V2
{
system_clock::time_point system_clock::now() noexcept
{
    return system_clock::time_point(system_clock::duration(1700000000LL * 1000000000LL));
}
} // namespace _V2
} // namespace chrono

unsigned int random_device::_M_getval()
{
    // one fixed value for every call: each of Surge's random_device users gets the same seed whatever
    // order or number of them exists (Ghost Bass's MORPH holds one of Surge's own MSEG EvaluatorStates,
    // which takes a seed when it is made - with a running sequence that shifted every later seed)
    return 0x9E3779B9u;
}
} // namespace std
