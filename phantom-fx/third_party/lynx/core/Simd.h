#pragma once

// SSE/AVX intrinsics for the engines that reproduce FL Studio's x86 arithmetic bit for bit (MXCSR rounding and
// flush-to-zero, cvtsd2si, packed float order). On x86 they are the compiler's own; on ARM64 (Apple Silicon) the SSE
// subset is translated to NEON by sse2neon (third_party/sse2neon, MIT), with its precise min/max/div/sqrt options so
// the results stay IEEE-exact. AVX/FMA x86 paths stay behind LYNX_X86 (ARM64 has FMA natively: std::fma).

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
 #include <immintrin.h>
 #define LYNX_X86 1
#elif defined(__aarch64__) || defined(_M_ARM64)
 #define SSE2NEON_SUPPRESS_WARNINGS 1
 #define SSE2NEON_PRECISE_MINMAX 1
 #define SSE2NEON_PRECISE_DIV 1
 #define SSE2NEON_PRECISE_SQRT 1
 #define SSE2NEON_PRECISE_DP 1
 #include "sse2neon.h"
 #include <cstdint>
 #define LYNX_X86 0

// MXCSR on ARM64: sse2neon's _mm_getcsr/_mm_setcsr carry only the rounding mode; the exact engines also switch
// flush-to-zero / denormals-are-zero (bits 15 and 6), so both are mapped onto FPCR here: rounding control (x86 RC
// 00 nearest, 01 down, 10 up, 11 toward zero) ↔ FPCR.RMode (00 nearest, 01 up, 10 down, 11 zero), FTZ/DAZ ↔ FPCR.FZ
// (ARM's flush-to-zero flushes inputs and results, which is FTZ + DAZ together).
inline unsigned lynxGetCsr() noexcept
{
    std::uint64_t fpcr;
    __asm__ __volatile__ ("mrs %0, fpcr" : "=r"(fpcr));
    const auto rm = (unsigned) (fpcr >> 22) & 3u;
    const auto rc = rm == 1u ? 2u : rm == 2u ? 1u : rm;
    return 0x1f80u | (rc << 13) | ((fpcr & (1ull << 24)) != 0 ? 0x8040u : 0u);
}
inline void lynxSetCsr (unsigned csr) noexcept
{
    std::uint64_t fpcr;
    __asm__ __volatile__ ("mrs %0, fpcr" : "=r"(fpcr));
    const auto rc = (csr >> 13) & 3u;
    const auto rm = rc == 1u ? 2u : rc == 2u ? 1u : rc;
    fpcr = (fpcr & ~((3ull << 22) | (1ull << 24))) | ((std::uint64_t) rm << 22) | ((csr & 0x8040u) != 0 ? (1ull << 24) : 0ull);
    __asm__ __volatile__ ("msr fpcr, %0" : : "r"(fpcr));
}
 #define _mm_getcsr lynxGetCsr
 #define _mm_setcsr lynxSetCsr
#else
 #error "LYNX needs x86-64 or ARM64"
#endif
