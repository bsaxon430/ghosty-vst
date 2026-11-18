// Block Step window size tests (local, no GUI): g++ -std=c++17 -I.. SizeTests.cpp -o SizeTests && ./SizeTests
#include "BlockStepSize.h"
#include <cstdio>
using namespace bstep::winsize; int f = 0;
#define C(c, m) do { bool ok = (c); std::printf("%s %s\n", ok ? "  ok  " : "  FAIL", m); if (!ok) ++f; } while (0)
int main() {
  auto a = initial(1440, 875, 0, 0); C(a.w == 1200 && a.h == 760, "new window on his MacBook Pro: 1200 x 760");
  C(kDefW <= 1240 && kDefH <= 780, "default never bigger than 1240 x 780");
  auto a2 = initial(1440, 843, 0, 0); C(a2.w == 1200 && a2.h == 760, "his MacBook Pro with the Dock showing (usable 1440 x 843): 1200 x 760");
  auto b = initial(1440, 875, 1000, 633); C(b.w == 1000 && b.h == 633, "project's own size reopens");
  auto c = initial(1440, 875, 2400, 1520); C(c.w <= 1400 && c.h <= 815, "a big saved size is kept on the screen");
  auto d = initial(1280, 720, 0, 0); C(d.w <= 1240 && d.h <= 660, "a small screen: smaller, still on screen");
  auto e = initial(0, 0, 0, 0); C(e.w == 1200 && e.h == 760, "no screen info: the default");
  C(kMinW * kDefH == kMinH * kDefW || kMinW * 1000 / kMinH == kDefW * 1000 / kDefH, "min size is the same shape");
  return f;
}
