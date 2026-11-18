// MUTANT ARP in the processor: the cell lives in the idea (Idea::mutant), these calls change it (one undo step each) and realise
// the ARP lane (and the MELODY lane with TWO LINES) from it. Everything downstream (pattern, MIDI out, drag-outs, roll, tools)
// sees ordinary lane notes. See Mutant.h.
#include "ArpProcessor.h"

using namespace arp;
juce::String extendLanes (Idea& X, int oldTotal);   // ArpProcessor.cpp: loops the other lanes to a longer idea

MutantParams ArpProcessor::mutantDefaults (int mode, int bpm)
{
    MutantParams P; P.mode = juce::jlimit (0, kMutantModes - 1, mode);
    switch (P.mode)
    {
        case MutRage:    P.density = 0.55; P.octaveJump = 0.55; P.intervalChaos = 0.3; P.retrigger = 0.3; P.gate = 0.5; P.scaleLock = true; break;
        case MutAlien:   P.density = 0.45; P.octaveJump = 0.4; P.intervalChaos = 0.5; P.retrigger = 0.3; P.gate = 0.55; P.glide = 0.2; P.swing = 0.08; P.scaleLock = true; break;   // (2026-11-13: in key by default too: its chromatic approach notes sounded wrong; SCALE LOCK off brings them back)
        case MutDigital: P.density = 0.55; P.octaveJump = 0.5; P.intervalChaos = 0.25; P.retrigger = 0.4; P.gate = 0.4; P.scaleLock = true; break;
        case MutDream:   P.density = 0.4; P.octaveJump = 0.3; P.intervalChaos = 0.2; P.retrigger = 0.1; P.gate = 0.95; P.glide = 0.5; P.swing = 0.12; P.scaleLock = true; break;
        case MutHyper:   P.density = 0.6; P.octaveJump = 0.6; P.intervalChaos = 0.35; P.retrigger = 0.45; P.gate = 0.85; P.velRandom = 0.4; P.scaleLock = true; P.lineBReg = 1; break;   // HYPER: bright, dense, the answer above
        case MutCyberpop: P.density = 0.5; P.octaveJump = 0.5; P.intervalChaos = 0.3; P.retrigger = 0.3; P.gate = 0.6; P.velRandom = 0.3; P.scaleLock = true; P.lineBReg = 1; break;   // CYBERPOP: the learned table's own numbers, the answer above
        default:         P.density = 0.45; P.octaveJump = 0.25; P.intervalChaos = 0.25; P.retrigger = 0.15; P.gate = 0.7; P.velRandom = 0.35; P.glide = 0.25; P.swing = 0.06; P.scaleLock = true; P.lineBReg = -1; break;   // FLUTE: sparse phrases, room for the 808
    }
    if (bpm > 0 && bpm < 100) P.div = 32;   // a slow host: the same speed in 32nds
    return P;
}

void ArpProcessor::mutantApply (Idea& X) const
{
    auto& Mu = X.mutant; if (! Mu.cell.valid()) return;
    const int wantBars = juce::jmax (Mu.P.evolveBars, Mu.P.twoLine == TwoAnswer ? 2 * mutantVariationBars (Mu.P) : 0);
    if (wantBars > 0 && X.total < wantBars * BAR) { const int old = X.total; X.songBars = juce::jmax (X.songBars, wantBars); rebuildChords (X); extendLanes (X, old); }
    std::vector<Note> A, B;
    mutantRealizeTwo (Mu.cell, Mu.cellB, X.H, Mu.P, X.total, A, B);
    auto& LA = X.lanes[LaneArp]; LA.notes = A; LA.edited = false;
    if (Mu.P.twoLine != TwoOff) { auto& LB = X.lanes[LaneMelody]; LB.notes = B; LB.edited = true; }
    X.arpSpb = Mu.P.div >= 32 ? 32 : Mu.P.div <= 8 ? 8 : 16;   // the roll's grid (and GROOVE) follow the step size
    Mu.harm = harmonyPrint (X.H);
}

void ArpProcessor::mutantSetActive (bool on)
{
    if (on == I.mutant.active) return;
    if (on)
    {
        change ([&] (Idea& X) {
            auto& Mu = X.mutant;
            if (! Mu.cell.valid()) { Mu.P = mutantDefaults (Mu.P.mode, X.g.bpm); Mu.P.seed = X.seed; Mu.cell = arp::mutantGenerate (X.H, Mu.P); Mu.generation = 0; }
            if (! Mu.active) { Mu.classic = X.lanes[LaneArp].notes; Mu.hasClassic = true; } Mu.active = true;
            if (Mu.P.twoLine == TwoInterlock && ! Mu.cellB.valid()) Mu.cellB = mutantSecondCell (Mu.cell, X.H, Mu.P);
            mutantApply (X);
        }, true, "MUTANT ARP on");
    }
    else change ([&] (Idea& X) {
        auto& Mu = X.mutant; Mu.active = false;
        if (Mu.hasClassic) { X.lanes[LaneArp].notes = Mu.classic; Mu.classic.clear(); Mu.hasClassic = false; }   // the classic lane's own locks come back
        rerealizeArp (X); }, true, "MUTANT ARP off");   // the classic figure takes the lane back
}

void ArpProcessor::mutantGenerate (bool newSeed)
{
    change ([&] (Idea& X) {
        auto& Mu = X.mutant;
        if (newSeed) Mu.P.seed = (uint32_t) juce::Random::getSystemRandom().nextInt ({ 1, 999999 });
        Mu.cell = arp::mutantGenerate (X.H, Mu.P, Mu.cell.valid() ? &Mu.cell : nullptr); Mu.generation = 0;
        if (Mu.P.twoLine == TwoInterlock) Mu.cellB = mutantSecondCell (Mu.cell, X.H, Mu.P);
        if (! Mu.active) { Mu.classic = X.lanes[LaneArp].notes; Mu.hasClassic = true; } Mu.active = true; mutantApply (X);
    }, true, "MUTANT GENERATE");
}

void ArpProcessor::mutantMutate (int kind, double amount)
{
    change ([&] (Idea& X) {
        auto& Mu = X.mutant; if (! Mu.cell.valid()) return;
        ++Mu.generation;
        Mu.cell = arp::mutantMutate (Mu.cell, X.H, Mu.P, Mu.P.seed + 7919u * (uint32_t) Mu.generation + (uint32_t) kind * 131u, amount, kind);
        if (Mu.P.twoLine == TwoInterlock) Mu.cellB = mutantSecondCell (Mu.cell, X.H, Mu.P);
        if (! Mu.active) { Mu.classic = X.lanes[LaneArp].notes; Mu.hasClassic = true; } Mu.active = true; mutantApply (X);
    }, true, kind == 1 ? "OCTAVE CHAOS" : kind == 2 ? "RHYTHM CHAOS" : "MUTATE");
}

void ArpProcessor::mutantSetParams (const MutantParams& P, bool regenerate)
{
    change ([&] (Idea& X) {
        auto& Mu = X.mutant; const bool modeChanged = P.mode != Mu.P.mode; const int twoBefore = Mu.P.twoLine;
        Mu.P = P;
        if (regenerate || modeChanged || ! Mu.cell.valid()) { Mu.cell = arp::mutantGenerate (X.H, Mu.P, Mu.cell.valid() ? &Mu.cell : nullptr); Mu.generation = 0; }
        if (Mu.P.twoLine == TwoInterlock && (regenerate || modeChanged || twoBefore != TwoInterlock || ! Mu.cellB.valid())) Mu.cellB = mutantSecondCell (Mu.cell, X.H, Mu.P);
        if (Mu.active) mutantApply (X);
    }, true, "MUTANT settings");
}

void ArpProcessor::mutantLockStep (int step, bool on)
{
    change ([&] (Idea& X) { auto& c = X.mutant.cell; if (step >= 0 && step < c.len) c.s[(size_t) step].lock = on; if (X.mutant.active) mutantApply (X); }, true, on ? "LOCK NOTE" : "UNLOCK NOTE");
}
void ArpProcessor::mutantLockFirst (int n)
{
    change ([&] (Idea& X) { arp::mutantLockFirst (X.mutant.cell, n); if (X.mutant.active) mutantApply (X); }, true, n > 0 ? "LOCK FIRST " + juce::String (n) : "UNLOCK ALL");
}
void ArpProcessor::mutantLockSelected (const std::vector<bool>& sel)
{
    change ([&] (Idea& X) {
        auto& Mu = X.mutant; const auto& ns = X.lanes[LaneArp].notes;
        for (size_t i = 0; i < ns.size() && i < sel.size(); ++i) if (sel[i]) { const int st = mutantStepAt (Mu.cell, Mu.P, ns[i].t); if (st >= 0) Mu.cell.s[(size_t) st].lock = true; }
        if (Mu.active) mutantApply (X);
    }, true, "LOCK SELECTED");
}
void ArpProcessor::mutantSeedStep (int dir)
{
    change ([&] (Idea& X) {
        auto& Mu = X.mutant; Mu.P.seed = (uint32_t) juce::jmax (1, (int) Mu.P.seed + dir);
        Mu.cell = arp::mutantGenerate (X.H, Mu.P, &Mu.cell); Mu.generation = 0;
        if (Mu.P.twoLine == TwoInterlock) Mu.cellB = mutantSecondCell (Mu.cell, X.H, Mu.P);
        if (! Mu.active) { Mu.classic = X.lanes[LaneArp].notes; Mu.hasClassic = true; } Mu.active = true; mutantApply (X);
    }, true, "MUTANT SEED");
}

juce::File ArpProcessor::writeMutantMidi()
{
    const int mask = (1 << LaneArp) | (I.mutant.P.twoLine != TwoOff ? (1 << LaneMelody) : 0);
    return writeMidiFile (mask, false);
}
