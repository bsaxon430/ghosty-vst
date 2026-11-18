// Poltergeist 2 (GhostBlock) - WarpCore's SSE2 build (upstream file dsp/dsp_sse2.cpp, unmodified). See warpcore-isa.h.
#if defined(__x86_64__) || defined(_M_X64)
#define P2WC_NS p2wc_sse2
#define P2WC_FILE "dsp/dsp_sse2.cpp"
#define P2WC_TABLE dsp_sse2
#define P2WC_SYMBOL kWarpSse2
#define P2WC_NAME "SSE2"
#include "warpcore-isa-impl.inc"
#endif
