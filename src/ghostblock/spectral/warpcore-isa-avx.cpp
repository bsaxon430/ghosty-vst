// Poltergeist 2 (GhostBlock) - WarpCore's AVX build (upstream file dsp/dsp_avx.cpp, unmodified). See warpcore-isa.h.
#if defined(__x86_64__) || defined(_M_X64)
#define P2WC_NS p2wc_avx
#define P2WC_FILE "dsp/dsp_avx.cpp"
#define P2WC_TABLE dsp_avx
#define P2WC_SYMBOL kWarpAvx
#define P2WC_NAME "AVX"
#include "warpcore-isa-impl.inc"
#endif
