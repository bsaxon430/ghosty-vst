// SPDX-License-Identifier: AGPL-3.0-or-later
// Apple's clang cannot assemble lsp-dsp-lib's AVX2 / AVX-512 inline assembly ("expected relocatable expression" on its `offset + %[mem]`
// operands), so on macOS those two files are left out and these empty initialisers stand in: lsp-dsp-lib then keeps its generic, SSE and
// AVX functions (its own dispatch), which is all the GHOST LIMITER baseline needs. Only compiled on Apple (CMakeLists.txt).
#include <lsp-plug.in/common/types.h>
#include <lsp-plug.in/common/cpuid.h>
namespace lsp
{
    namespace avx2 { void dsp_init (const cpuid_t*) {} }
    namespace avx512 { void dsp_init (const cpuid_t*) {} }
}
#if defined(__aarch64__) || defined(__arm64__)
// GhostBlock universal build: the arm64 slice uses lsp-dsp-lib's generic functions (its aarch64 NEON files are not compiled here)
namespace lsp { namespace aarch64 { void dsp_init (const cpuid_t*) {} } }
#endif
