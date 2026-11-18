// Poltergeist 2 (GhostBlock) - shared body of the warpcore-isa-*.cpp files (see warpcore-isa.h).
// Needs: P2WC_NS (a namespace of its own), P2WC_FILE (the upstream variant), P2WC_TABLE (its export), P2WC_SYMBOL (ours).
// Everything upstream's files include comes first, at global scope, so their own #includes are no-ops inside the namespace.
#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <complex>
#include <cstddef>
#include <numbers>
#include <type_traits>
#include <x86/avx2.h>
#include <x86/sse4.1.h>
#include "ghostblock/spectral/warpcore-isa.h"

namespace P2WC_NS
{
#include P2WC_FILE
namespace
{
using State = warpcore::ProcessorState;
void *create() { return new State(); }
void destroy(void *p) { delete static_cast<State *>(p); }
void init(void *p, float sr) { warpcore::P2WC_TABLE.init(*static_cast<State *>(p), sr); }
void reset(void *p) { warpcore::P2WC_TABLE.reset(*static_cast<State *>(p)); }
void update(void *p, const ghostblock::p2::spectral::detail::WarpParams &w)
{
    warpcore::Param q;
    q.bands = w.bands;
    q.f_high = w.fHigh;
    q.filter_scale = w.scale;
    q.filter_order = w.poles;
    q.pitch_shift = w.pitch;
    q.drywet = w.dryWet;
    q.pitch_affect = w.pitchAffect;
    q.fill_gap = w.fillGap;
    q.freq_distribution = static_cast<warpcore::FreqDistrbution>(w.freqMode);
    warpcore::P2WC_TABLE.update(*static_cast<State *>(p), q);
}
void process(void *p, float *l, float *r, int n) { warpcore::P2WC_TABLE.process(*static_cast<State *>(p), l, r, n); }
} // namespace
} // namespace P2WC_NS
namespace ghostblock::p2::spectral::detail
{
extern const WarpIsa P2WC_SYMBOL;
const WarpIsa P2WC_SYMBOL = {P2WC_NAME, &P2WC_NS::create, &P2WC_NS::destroy, &P2WC_NS::init, &P2WC_NS::reset, &P2WC_NS::update, &P2WC_NS::process};
} // namespace ghostblock::p2::spectral::detail
