// Poltergeist 2 (GhostBlock) - WarpCore's FMA build (upstream file dsp/dsp_avx2_fma.cpp, unmodified). See warpcore-isa.h.
#if defined(__x86_64__) || defined(_M_X64)
#define P2WC_NS p2wc_fma
#define P2WC_FILE "dsp/dsp_avx2_fma.cpp"
#define P2WC_TABLE dsp_avx2_fma
#define P2WC_SYMBOL kWarpFma
#define P2WC_NAME "FMA"
#include "warpcore-isa-impl.inc"
#endif
