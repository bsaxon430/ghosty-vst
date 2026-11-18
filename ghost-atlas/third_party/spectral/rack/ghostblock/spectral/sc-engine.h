/*
 * Poltergeist 2 (GhostBlock) - SPECTRAL FX: the part that runs Spectral Corruptor's code (see sc-engine.cpp).
 * Internal to src/ghostblock/spectral; the editor and the CLAP wrapper use spectral-fx.h.
 */
#pragma once
#include "ghostblock/spectral/spectral-fx.h"

namespace ghostblock::p2::spectral::detail
{
constexpr int kQuantum = 64; // samples per processing step, as upstream's plugin ("quantum")

struct Core; // the STFT (if any spectral effect is loaded), the chain's dry/wet mixer, work buffers
struct Proc; // one slot's effect object

struct CoreCfg
{
    double sampleRate{48000};
    bool stft{false};
    int fftSize{2048};
    int window{0};
    float overlap{4.f};
};
// ---- main thread
void buildCatalogue(std::vector<TypeDesc> &types, std::vector<std::string> &categories);
Core *makeCore(const CoreCfg &cfg, float chainMix01, uint32_t serial);
void freeCore(Core *);
Proc *makeProc(int type, const Core *core, const float *values); // nullptr for the empty slot
void freeProc(Proc *);
bool coreSpectrum(Core *, std::vector<float> &mags, double &rate, bool input);
// ---- any thread (plain reads)
int coreLatency(const Core *) noexcept; // samples: fftSize - 1 with an STFT, else 0
uint32_t coreSerial(const Core *) noexcept;
bool coreHasStft(const Core *) noexcept;
int procType(const Proc *) noexcept;
uint32_t procSerial(const Proc *) noexcept; // the core a spectral proc was made for (0 = any core)
// ---- audio thread
void procApply(Proc *, const float *values) noexcept; // the slot's current values into the running module
void procReset(Proc *) noexcept;
void coreReset(Core *) noexcept;
struct RackIo
{
    Core *core{nullptr};
    Proc *proc[kSlots]{};
    float gain0[kSlots]{}, gain1[kSlots]{}; // each slot's share (bypass fade x slot mix, 0..1) at the start / end of this step
    float limitHz{22050.f};
    double tempo{120};
    float chainMix01{1.f};
    float level[kSlots]{}; // out: peak after each slot
};
// one step (n <= kQuantum), in place: spectral slots in one STFT, then the time slots, then the chain's dry/wet
void rackProcess(RackIo &io, float *L, float *R, int n) noexcept;
} // namespace ghostblock::p2::spectral::detail
