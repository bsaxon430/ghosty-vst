// Poltergeist 2 (GhostBlock) - checks: upstream's OWN rack engine (scrr::dsp::SpectralProcessor), as the reference
#pragma once
#include <string>
#include <vector>
namespace p2ref
{
struct Prop
{
    std::string id;
    int kind; // 0 float, 1 int, 2 bool, 3 choice
    double value;
};
struct Module
{
    std::string type;
    std::vector<Prop> props;
    std::string remapCurve; // Frequency Remap only: "x,y,curve;..." three nodes
};
struct Setup
{
    double sampleRate{48000};
    int fftIndex{4}, window{0}, limitIndex{2};
    double overlap{4};
    int slices{1};
    double crossfade{0};
    bool allRacks{false}; // the same chain in CH1..CH4 (so every slice is processed)
    std::vector<Module> chain;
};
// runs upstream's engine exactly as its plugin does (64-sample steps), in place
void run(const Setup &s, float *L, float *R, int n);
} // namespace p2ref
