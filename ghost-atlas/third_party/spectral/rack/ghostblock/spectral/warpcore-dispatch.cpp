/*
 * Poltergeist 2 (GhostBlock) - SPECTRAL FX: which SIMD build of WarpCore this CPU runs.
 * Same order as upstream's dsp_dispatch.cpp (AVX2+FMA, AVX2, AVX, SSE4.1, SSE2; NEON on arm64); written here because
 * upstream's picker depends on cpp_simd_detector, which has no licence. P2_WARPCORE_ISA=<name> forces one (tests).
 */
#include "ghostblock/spectral/warpcore-isa.h"
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#if defined(__APPLE__)
#include <sys/sysctl.h>
#endif

namespace ghostblock::p2::spectral::detail
{
#if defined(__x86_64__) || defined(_M_X64)
extern const WarpIsa kWarpSse2, kWarpSse4, kWarpAvx, kWarpAvx2, kWarpFma;
namespace
{
struct Cpu
{
    bool sse41{false}, avx{false}, avx2{false}, fma{false};
};
Cpu cpu()
{
    Cpu c;
#if defined(__APPLE__)
    auto has = [](const char *k) {
        int v = 0;
        size_t n = sizeof(v);
        return sysctlbyname(k, &v, &n, nullptr, 0) == 0 && v != 0;
    };
    c.sse41 = has("hw.optional.sse4_1");
    c.avx = has("hw.optional.avx1_0");
    c.avx2 = has("hw.optional.avx2_0");
    c.fma = has("hw.optional.fma");
#elif defined(__GNUC__) || defined(__clang__)
    __builtin_cpu_init();
    c.sse41 = __builtin_cpu_supports("sse4.1");
    c.avx = __builtin_cpu_supports("avx"); // (includes the OS saving the AVX registers)
    c.avx2 = __builtin_cpu_supports("avx2");
    c.fma = __builtin_cpu_supports("fma");
#endif
    return c;
}
} // namespace
const WarpIsa *warpIsaNamed(const char *n)
{
    if (!n) return nullptr;
    const Cpu c = cpu();
    for (auto *t : {&kWarpSse2, &kWarpSse4, &kWarpAvx, &kWarpAvx2, &kWarpFma})
        if (std::strcmp(t->name, n) == 0)
        {
            const bool ok = t == &kWarpSse2 || (t == &kWarpSse4 && c.sse41) || (t == &kWarpAvx && c.avx) || (t == &kWarpAvx2 && c.avx2) ||
                            (t == &kWarpFma && c.avx2 && c.fma);
            return ok ? t : nullptr; // never hand out code the CPU cannot run
        }
    return nullptr;
}
const WarpIsa *warpIsa()
{
    static const WarpIsa *best = [] {
        if (auto *forced = warpIsaNamed(std::getenv("P2_WARPCORE_ISA"))) return forced;
        const Cpu c = cpu();
        if (c.avx2) return c.fma ? &kWarpFma : &kWarpAvx2;
        if (c.avx) return &kWarpAvx;
        if (c.sse41) return &kWarpSse4;
        return &kWarpSse2;
    }();
    return best;
}
#elif defined(__aarch64__) || defined(_M_ARM64)
extern const WarpIsa kWarpNeon;
const WarpIsa *warpIsaNamed(const char *n) { return n && std::strcmp(n, "NEON") == 0 ? &kWarpNeon : nullptr; }
const WarpIsa *warpIsa() { return &kWarpNeon; }
#else
const WarpIsa *warpIsaNamed(const char *) { return nullptr; }
const WarpIsa *warpIsa() { return nullptr; }
#endif
} // namespace ghostblock::p2::spectral::detail
