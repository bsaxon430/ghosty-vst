// Poltergeist 2 (GhostBlock) - WarpCore's AVX2 build (upstream file dsp/dsp_avx2.cpp, unmodified). See warpcore-isa.h.
#if defined(__x86_64__) || defined(_M_X64)
#define P2WC_NS p2wc_avx2
#define P2WC_FILE "dsp/dsp_avx2.cpp"
#define P2WC_TABLE dsp_avx2
#define P2WC_SYMBOL kWarpAvx2
#define P2WC_NAME "AVX2"
#include "warpcore-isa-impl.inc"
#endif
