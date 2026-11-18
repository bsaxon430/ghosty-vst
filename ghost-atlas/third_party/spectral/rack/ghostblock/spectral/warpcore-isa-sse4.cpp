// Poltergeist 2 (GhostBlock) - WarpCore's SSE4 build (upstream file dsp/dsp_sse4.cpp, unmodified). See warpcore-isa.h.
#if defined(__x86_64__) || defined(_M_X64)
#define P2WC_NS p2wc_sse4
#define P2WC_FILE "dsp/dsp_sse4.cpp"
#define P2WC_TABLE dsp_sse4
#define P2WC_SYMBOL kWarpSse4
#define P2WC_NAME "SSE4"
#include "warpcore-isa-impl.inc"
#endif
