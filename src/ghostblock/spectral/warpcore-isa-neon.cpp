// Poltergeist 2 (GhostBlock) - WarpCore's NEON build (upstream file dsp/dsp_neon.cpp, unmodified). See warpcore-isa.h.
#if defined(__aarch64__) || defined(_M_ARM64)
#define P2WC_NS p2wc_neon
#define P2WC_FILE "dsp/dsp_neon.cpp"
#define P2WC_TABLE dsp_neon
#define P2WC_SYMBOL kWarpNeon
#define P2WC_NAME "NEON"
#include "warpcore-isa-impl.inc"
#endif
