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
