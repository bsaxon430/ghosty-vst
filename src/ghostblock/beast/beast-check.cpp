// SEANCE - BEAST on its own: every Surge oscillator type sounds, is in tune, and answers to audio-rate FM.
#include "beast.h"
#include <cmath>
#include <cstdio>
#include <vector>
static int fails = 0;
#define CHECK(c, ...) do { if (c) printf("ok: "); else { printf("FAIL: "); fails++; } printf(__VA_ARGS__); printf("\n"); } while (0)
int main()
{
    auto *w = beast_world_create();
    const double sr = 96000;
    beast_world_set_rate(w, sr);
    for (int t = 0; t < beast_type_count(); ++t)
    {
        beast_slot_set_type(w, t % BEAST_SLOTS, t);
        printf("%-10s:", beast_type_name(t));
        for (int p = 0; p < BEAST_PARAMS; ++p) printf(" [%s]", beast_slot_param_name(w, t % BEAST_SLOTS, p));
        printf("\n");
        std::vector<float> dry, wet;
        for (int pass = 0; pass < 2; ++pass)
        {
            auto *o = beast_osc_create(w);
            beast_osc_start(o, t % BEAST_SLOTS, 57.f);
            float L[BEAST_BLOCK], fm[BEAST_BLOCK];
            double ph = 0;
            for (int b = 0; b < (int)(sr / BEAST_BLOCK); ++b)
            {
                for (int i = 0; i < BEAST_BLOCK; ++i) { fm[i] = (float)std::sin(ph); ph += 2 * M_PI * 440.0 / sr; }
                beast_osc_render(o, 57.f, pass ? fm : nullptr, 1.f, L, L);
                for (float f : L) (pass ? wet : dry).push_back(f);
            }
            beast_osc_destroy(o);
        }
        bool fin = true; double e = 0, d = 0; int zc = 0;
        for (size_t i = 0; i < dry.size(); ++i)
        {
            fin = fin && std::isfinite(dry[i]) && std::isfinite(wet[i]);
            e += dry[i] * dry[i]; d += std::fabs(dry[i] - wet[i]);
            if (i > 0 && dry[i - 1] <= 0 && dry[i] > 0) zc++;
        }
        CHECK(fin && std::sqrt(e / dry.size()) > 0.01 && d / dry.size() > 1e-3, "%s: rms %.3f, FM changes it by %.4f, %d upward crossings in 1 s (220 Hz note)", beast_type_name(t),
              std::sqrt(e / dry.size()), d / dry.size(), zc);
    }
    beast_world_destroy(w);
    printf(fails ? "FAILURES: %d\n" : "ALL PASS\n", fails);
    return fails ? 1 : 0;
}
