// Ghost Bass purity test, the upstream side: renders a list of presets with UPSTREAM Surge XT (the commit Ghost Bass was forked from,
// unmodified), headless, and writes one raw float32 file per preset (all left samples, then all right samples).
// Ghost Bass's own test (XTTestGhostLayers.cpp, "b11e") renders the same presets, the same notes, the same way, with a fresh Ghost Bass
// at its defaults, and requires every sample to be identical.
//   GB_UP_LIST=<file with one .fxp path per line> GB_UP_OUT=<folder> surge-testrunner "[gbref]"
// This file is only ever compiled into the upstream checkout by the CI workflow (never into Ghost Bass).
#include "catch2/catch_amalgamated.hpp"
#include "HeadlessUtils.h"
#include "SurgeSynthesizer.h"
#include "sst/plugininfra/cpufeatures.h"
#include <cstdlib>
#include <memory>
#include <fstream>
#include <string>
#include <vector>

TEST_CASE("GB upstream render", "[.gbref]")
{
    const char *lst = std::getenv("GB_UP_LIST");
    const char *out = std::getenv("GB_UP_OUT");
    REQUIRE(lst);
    REQUIRE(out);
    std::ifstream in(lst);
    std::string path;
    int idx = 0;
    std::shared_ptr<SurgeSynthesizer> surgeOwner;
    SurgeSynthesizer *surge = nullptr;
    // the same host conditions as the plugin's own process loop (SurgeSynthProcessor::processBlock): flush-to-zero, a 120 bpm tempo in 4/4
    // (or the tempo the sound itself carries), the transport position advancing by one block after each block
    auto fpuguard = sst::plugininfra::cpufeatures::FPUStateGuard();
    float tempo = 120.f;
    auto runBlock = [&]() {
        surge->time_data.tempo = tempo;
        surge->time_data.timeSigNumerator = 4;
        surge->time_data.timeSigDenominator = 4;
        surge->time_data.isPlaying = false;
        surge->transportRunning = false;
        surge->resetStateFromTimeData();
        surge->process();
        surge->time_data.ppqPos += (double)BLOCK_SIZE * surge->time_data.tempo / (60. * surge->storage.samplerate);
    };
    while (std::getline(in, path))
    {
        if (path.empty())
            continue;
        // every sound on a brand new engine (nothing left over from the sound before)
        surgeOwner = Surge::Headless::createSurge(48000, false);
        surge = surgeOwner.get();
        surge->storage.rngGen.g.seed(4242);
        std::srand(4242);
        tempo = 120.f;
        surge->time_data.ppqPos = 0;
        surge->loadPatchByPath(path.c_str(), -1, "null");
        if (surge->storage.unstreamedTempo > -1.f)
            tempo = surge->storage.unstreamedTempo;
        for (int b = 0; b < 12; ++b)
            runBlock();
        surge->storage.rngGen.g.seed(4343);
        std::srand(4343);
        std::vector<float> L, R;
        for (int b = 0; b < 200; ++b)
        {
            if (b == 0)
                surge->playNote(0, 36, 110, 0, -1);
            if (b == 70)
            {
                surge->releaseNote(0, 36, 0, -1);
                for (int n : {48, 55, 60})
                    surge->playNote(0, n, 90, 0, -1);
            }
            if (b == 80)
                surge->channelController(0, 1, 100);
            if (b == 130)
            {
                for (int n : {48, 55, 60})
                    surge->releaseNote(0, n, 0, -1);
                surge->playNote(0, 72, 70, 0, -1);
            }
            runBlock();
            L.insert(L.end(), surge->output[0], surge->output[0] + BLOCK_SIZE);
            R.insert(R.end(), surge->output[1], surge->output[1] + BLOCK_SIZE);
        }
        surge->allNotesOff();
        runBlock();
        std::ofstream o(std::string(out) + "/" + std::to_string(idx) + ".f32", std::ios::binary);
        o.write(reinterpret_cast<const char *>(L.data()), (std::streamsize)(L.size() * 4));
        o.write(reinterpret_cast<const char *>(R.data()), (std::streamsize)(R.size() * 4));
        ++idx;
    }
    REQUIRE(idx > 0);
}
