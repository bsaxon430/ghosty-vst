// Ghost Bass b10 - the MIDI script shared by the null test (XTTestGhostLayers.cpp) and the upstream reference renderer
// (ghost_layers_ref.cpp). 48 kHz, 32-sample blocks, events at the start of a block.
#pragma once
#include <vector>
namespace ghost::layers::ref
{
struct Ev
{
    int block;
    int type; // 0 on, 1 off, 2 bend (value = 0..16383), 3 sustain cc
    int a, b;
};
constexpr int kBlocks = 2200;
constexpr int kLead = 10; // idle blocks rendered (and thrown away) before the script: the plug-in has been running silently, as in a host
inline const std::vector<Ev> &script()
{
    static const std::vector<Ev> s = {{0, 0, 36, 100},   {300, 0, 43, 90},  {600, 1, 36, 0},   {800, 1, 43, 0},
                                      {1000, 0, 31, 127}, {1100, 2, 12000, 0}, {1300, 2, 8192, 0}, {1400, 1, 31, 0},
                                      {1600, 0, 48, 80}, {1900, 1, 48, 0}};
    return s;
}
} // namespace ghost::layers::ref
