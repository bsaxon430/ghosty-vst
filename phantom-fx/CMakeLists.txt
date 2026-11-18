# PHANTOM FX - GhostBlock's open-source 24-effect rack (AGPL-3.0-or-later). See LICENSE, THIRD_PARTY.md, AUDIT.md, DECISIONS.md.
# JUCE 8 / C++20. Formats: VST3 + CLAP + Standalone everywhere; AU behind PHANTOM_BUILD_AU (needs the Xcode app for Rez).
cmake_minimum_required(VERSION 3.24)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
project(PhantomFX VERSION 0.1.0 LANGUAGES C CXX)
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/BUILD PHANTOM_BUILD LIMIT_COUNT 1)
set(PHANTOM_VERSION 0.1.${PHANTOM_BUILD})
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(PHANTOM_BUILD_AU "Build the AU (needs the Xcode app)" OFF)
option(PHANTOM_BUILD_CLAP "Build the CLAP" ON)
option(PHANTOM_BUILD_TESTS "Build the test harness" ON)
# hidden: the four drawn looks (BLACK, PURPLE, LIGHT, GHOST) are not offered since 0.1.10 (Brandon, 2026-11-10); ON puts them
# back on the look button. Their code is kept either way. (Added to the compile flags only when ON, so OFF rebuilds nothing.)
option(PHANTOM_OLD_LOOKS "Offer the four drawn looks again (hidden, off)" OFF)
mark_as_advanced(PHANTOM_OLD_LOOKS)
set(PHANTOM_JUCE_DIR "" CACHE PATH "Use this JUCE 8.0.4 checkout instead of downloading it")

include(FetchContent)
if(POLICY CMP0169)
  cmake_policy(SET CMP0169 OLD)   # FetchContent_Populate(<name>) for the header-only pins we register ourselves
endif()
# ---- JUCE 8.0.4 (AGPL-3.0 option), pinned: tag 8.0.4 = 51d11a2be6d5c97ccf12b4e5e827006e19f0555a
if(PHANTOM_JUCE_DIR)
  add_subdirectory(${PHANTOM_JUCE_DIR} juce)
else()
  FetchContent_Declare(JUCE URL https://github.com/juce-framework/JUCE/archive/refs/tags/8.0.4.zip DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(JUCE)
endif()
# ---- chowdsp_utils (per-module licences, see THIRD_PARTY.md), pinned by commit
FetchContent_Declare(chowdsp_utils GIT_REPOSITORY https://github.com/Chowdhury-DSP/chowdsp_utils.git GIT_TAG e97b826ef3de0b0fd92b15cb2e286076f678d8b9 GIT_SHALLOW FALSE GIT_PROGRESS FALSE)
FetchContent_GetProperties(chowdsp_utils)
if(NOT chowdsp_utils_POPULATED)
  FetchContent_Populate(chowdsp_utils)   # we register only the modules we use (no GUI / plugin modules)
endif()
juce_add_modules(ALIAS_NAMESPACE chowdsp
  ${chowdsp_utils_SOURCE_DIR}/modules/common/chowdsp_core
  ${chowdsp_utils_SOURCE_DIR}/modules/common/chowdsp_data_structures
  ${chowdsp_utils_SOURCE_DIR}/modules/dsp/chowdsp_simd
  ${chowdsp_utils_SOURCE_DIR}/modules/dsp/chowdsp_buffers
  ${chowdsp_utils_SOURCE_DIR}/modules/dsp/chowdsp_math
  ${chowdsp_utils_SOURCE_DIR}/modules/dsp/chowdsp_dsp_data_structures
  ${chowdsp_utils_SOURCE_DIR}/modules/dsp/chowdsp_filters
  ${chowdsp_utils_SOURCE_DIR}/modules/dsp/chowdsp_dsp_utils
  ${chowdsp_utils_SOURCE_DIR}/modules/dsp/chowdsp_waveshapers
  ${chowdsp_utils_SOURCE_DIR}/modules/dsp/chowdsp_compressor
  ${chowdsp_utils_SOURCE_DIR}/modules/dsp/chowdsp_sources)
# ---- Signalsmith DSP (MIT), header-only, pinned by commit
FetchContent_Declare(signalsmith-dsp GIT_REPOSITORY https://github.com/Signalsmith-Audio/dsp.git GIT_TAG ca390d28c41ea5108a528e2e81faa4088f2f8ac8 GIT_PROGRESS FALSE)
FetchContent_GetProperties(signalsmith-dsp)
if(NOT signalsmith-dsp_POPULATED)
  FetchContent_Populate(signalsmith-dsp)
endif()
# ---- sst-basic-blocks (GPL-3.0), header-only, pinned by commit (sst-filters / waveshapers arrive with Phase 3 and need it)
FetchContent_Declare(sst-basic-blocks GIT_REPOSITORY https://github.com/surge-synthesizer/sst-basic-blocks.git GIT_TAG 5fb60983d57ccd0318865df8c004a1f9d8419b73 GIT_PROGRESS FALSE)
FetchContent_GetProperties(sst-basic-blocks)
if(NOT sst-basic-blocks_POPULATED)
  FetchContent_Populate(sst-basic-blocks)
endif()
# ---- sst-filters / sst-waveshapers (GPL-3.0), header-only, pinned by commit
FetchContent_Declare(sst-filters GIT_REPOSITORY https://github.com/surge-synthesizer/sst-filters.git GIT_TAG 87a171bcba52c014850848a29174a54f0adc6606 GIT_PROGRESS FALSE)
FetchContent_GetProperties(sst-filters)
if(NOT sst-filters_POPULATED)
  FetchContent_Populate(sst-filters)
endif()
FetchContent_Declare(sst-waveshapers GIT_REPOSITORY https://github.com/surge-synthesizer/sst-waveshapers.git GIT_TAG dd12f31a5a9016c9895e52d1a00eee0e1eebe6ce GIT_PROGRESS FALSE)
FetchContent_GetProperties(sst-waveshapers)
if(NOT sst-waveshapers_POPULATED)
  FetchContent_Populate(sst-waveshapers)
endif()
# ---- Phase 4 (Space) foundations, all pinned by commit (THIRD_PARTY.md)
# Signalsmith Stretch (MIT) + Signalsmith Linear (MIT, its FFT / STFT layer), header-only: SHIMMER's pitch shifter in the loop
FetchContent_Declare(signalsmith-stretch GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git GIT_TAG 88061fd4eda56a2353c5d65c74aa3f4ccf76f9da GIT_PROGRESS FALSE)
FetchContent_GetProperties(signalsmith-stretch)
if(NOT signalsmith-stretch_POPULATED)
  FetchContent_Populate(signalsmith-stretch)
endif()
FetchContent_Declare(signalsmith-linear GIT_REPOSITORY https://github.com/Signalsmith-Audio/linear.git GIT_TAG 7bb1c8f36794d6980080ce817c91f8da8fe56a66 GIT_PROGRESS FALSE)
FetchContent_GetProperties(signalsmith-linear)
if(NOT signalsmith-linear_POPULATED)
  FetchContent_Populate(signalsmith-linear)
endif()
# FFTConvolver (MIT), plain C++: DOORWAY's convolution (compiled through src/space/fftconv.cpp; Apple Accelerate FFT on the Mac)
FetchContent_Declare(fftconvolver GIT_REPOSITORY https://github.com/HiFi-LoFi/FFTConvolver.git GIT_TAG f2cdeb04c42141d2caec19ca4f137398b2a76b85 GIT_PROGRESS FALSE)
FetchContent_GetProperties(fftconvolver)
if(NOT fftconvolver_POPULATED)
  FetchContent_Populate(fftconvolver)
endif()
# sst-effects (GPL-3.0), header-only: CHORUS (its voice chorus), CRYSTAL (Nimbus, its granular host)
FetchContent_Declare(sst-effects GIT_REPOSITORY https://github.com/surge-synthesizer/sst-effects.git GIT_TAG 987b39dcb2e2e61e6225c3bc390fd00da824d226 GIT_PROGRESS FALSE)
FetchContent_GetProperties(sst-effects)
if(NOT sst-effects_POPULATED)
  FetchContent_Populate(sst-effects)
endif()
# {fmt} 12.1.0 (MIT), header-only: sst-basic-blocks' parameter metadata (pulled in by the sst voice chorus) needs it, as sst-effects' own build does
FetchContent_Declare(fmt GIT_REPOSITORY https://github.com/fmtlib/fmt.git GIT_TAG 407c905e45ad75fc29bf0f9bb7c5c2fd3475976f GIT_PROGRESS FALSE)
FetchContent_GetProperties(fmt)
if(NOT fmt_POPULATED)
  FetchContent_Populate(fmt)
endif()
# the eurorack granular processor (Emilie Gillet, MIT; the Surge Synth Team's desktop fork, as sst-effects uses it) + its stmlib submodule:
# CRYSTAL's engine. Only the granular processor's sources are compiled (below); "TEST" is how that code is told it is not on hardware
FetchContent_Declare(eurorack GIT_REPOSITORY https://github.com/surge-synthesizer/eurorack.git GIT_TAG 84f4f67aaa25bf696093b224e2a51a5c18143e4f GIT_SUBMODULES stmlib GIT_PROGRESS FALSE)
FetchContent_GetProperties(eurorack)
if(NOT eurorack_POPULATED)
  FetchContent_Populate(eurorack)
endif()
set(PHANTOM_CLOUDS_SOURCES
  ${eurorack_SOURCE_DIR}/clouds/dsp/granular_processor.cc ${eurorack_SOURCE_DIR}/clouds/dsp/correlator.cc ${eurorack_SOURCE_DIR}/clouds/dsp/mu_law.cc
  ${eurorack_SOURCE_DIR}/clouds/dsp/pvoc/frame_transformation.cc ${eurorack_SOURCE_DIR}/clouds/dsp/pvoc/phase_vocoder.cc ${eurorack_SOURCE_DIR}/clouds/dsp/pvoc/stft.cc
  ${eurorack_SOURCE_DIR}/clouds/resources.cc ${eurorack_SOURCE_DIR}/stmlib/dsp/atan.cc ${eurorack_SOURCE_DIR}/stmlib/dsp/units.cc ${eurorack_SOURCE_DIR}/stmlib/utils/random.cc)
set_source_files_properties(${PHANTOM_CLOUDS_SOURCES} PROPERTIES COMPILE_DEFINITIONS TEST=1 COMPILE_OPTIONS "-w")
# Dragonfly Reverb (GPL-3.0) with its bundled freeverb3 (GPL-2.0-or-later): HAUNTED HALLS. freeverb3 is pre-C++17 code, so it and our
# engine boundary (src/space/HallsEngine.cpp) are compiled as C++14, single-precision (LIBFV3_FLOAT), as Dragonfly's own Makefile does
FetchContent_Declare(dragonfly GIT_REPOSITORY https://github.com/michaelwillis/dragonfly-reverb.git GIT_TAG 440ec7b3b3db2fec34b9f80cf4890102ca14d1c3 GIT_SUBMODULES "" GIT_PROGRESS FALSE)
FetchContent_GetProperties(dragonfly)
if(NOT dragonfly_POPULATED)
  FetchContent_Populate(dragonfly)
endif()
set(PHANTOM_FV3_SOURCES)
foreach(f allpass biquad comb delay delayline earlyref efilter nrev nrevb progenitor progenitor2 revbase slot strev utils zrev zrev2)
  list(APPEND PHANTOM_FV3_SOURCES ${dragonfly_SOURCE_DIR}/common/freeverb/${f}.cpp)
endforeach()
list(APPEND PHANTOM_FV3_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/space/HallsEngine.cpp)
set_source_files_properties(${PHANTOM_FV3_SOURCES} PROPERTIES COMPILE_OPTIONS "-std=c++14;-w" COMPILE_DEFINITIONS "LIBFV3_FLOAT=1" INCLUDE_DIRECTORIES "${dragonfly_SOURCE_DIR}/common")
# one include root so sources say #include "signalsmith-dsp/delay.h" whatever the fetch folder is called
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/inc)
file(CREATE_LINK ${signalsmith-dsp_SOURCE_DIR} ${CMAKE_BINARY_DIR}/inc/signalsmith-dsp SYMBOLIC)
file(CREATE_LINK ${signalsmith-stretch_SOURCE_DIR} ${CMAKE_BINARY_DIR}/inc/signalsmith-stretch SYMBOLIC)
file(CREATE_LINK ${signalsmith-linear_SOURCE_DIR} ${CMAKE_BINARY_DIR}/inc/signalsmith-linear SYMBOLIC)
add_library(phantom_thirdparty INTERFACE)
target_include_directories(phantom_thirdparty INTERFACE ${CMAKE_BINARY_DIR}/inc ${sst-basic-blocks_SOURCE_DIR}/include ${sst-filters_SOURCE_DIR}/include ${sst-waveshapers_SOURCE_DIR}/include ${sst-effects_SOURCE_DIR}/include ${fmt_SOURCE_DIR}/include)
target_compile_definitions(phantom_thirdparty INTERFACE FMT_HEADER_ONLY=1 SST_EFFECTS_EURORACK=1)
target_include_directories(phantom_thirdparty INTERFACE ${eurorack_SOURCE_DIR})
target_compile_definitions(phantom_thirdparty INTERFACE SST_FILTERS_COMB_EXTENSION_FACTOR=1)
# the sst libraries are SSE code: native on x86_64; on Apple Silicon they go through simde (SIMD Everywhere, MIT),
# which sst-basic-blocks' own setup.h includes when it is not on x86. A universal (arm64;x86_64) build keeps the Intel slice
# native (-Xarch_x86_64 flags) and gives the arm64 slice simde.
FetchContent_Declare(simde GIT_REPOSITORY https://github.com/simd-everywhere/simde.git GIT_TAG v0.8.2 GIT_PROGRESS FALSE)
FetchContent_GetProperties(simde)
if(NOT simde_POPULATED)
  FetchContent_Populate(simde)
endif()
target_include_directories(phantom_thirdparty INTERFACE ${simde_SOURCE_DIR})
if(APPLE AND CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
  if(CMAKE_OSX_ARCHITECTURES MATCHES "x86_64")
    target_compile_options(phantom_thirdparty INTERFACE "SHELL:-Xarch_x86_64 -msse4.1" "SHELL:-Xarch_x86_64 -DSST_SIMD_NATIVE_X86=1" "SHELL:-Xarch_x86_64 -DSIMDE_UNAVAILABLE=1")
  endif()
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
  # arm64 only: simde for everything
else()
  target_compile_definitions(phantom_thirdparty INTERFACE SST_SIMD_NATIVE_X86=1 SIMDE_UNAVAILABLE=1)
  target_compile_options(phantom_thirdparty INTERFACE -msse4.1)
endif()
target_link_libraries(phantom_thirdparty INTERFACE chowdsp::chowdsp_filters chowdsp::chowdsp_dsp_utils chowdsp::chowdsp_waveshapers chowdsp::chowdsp_compressor chowdsp::chowdsp_sources)
# ChowTapeModel's sources include <JuceHeader.h> and HysteresisSTN.h: our stand-ins live in src/chow
target_include_directories(phantom_thirdparty INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src/chow ${CMAKE_CURRENT_SOURCE_DIR}/third_party/chowtape)
# CloudSeedCore (MIT) is vendored in third_party/cloudseed (one marked patch, THIRD_PARTY.md); src/space/CloudSeedShim.h supplies what its own build defines
target_include_directories(phantom_thirdparty INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/cloudseed)
target_include_directories(phantom_thirdparty INTERFACE ${fftconvolver_SOURCE_DIR})
# Granite (epsilon-records/granite @ bed64f0, MIT): GHOST LIMITER's GRANITE engine, header-only, vendored unchanged (DECISIONS.md 87)
target_include_directories(phantom_thirdparty INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/granite)
# Rhythm group foundations, vendored unchanged (THIRD_PARTY.md): GATE-12 / TIME-12 (src/rhythm/foundations.cpp), Mango + FxmeTools core + WDL FFT
target_include_directories(phantom_thirdparty INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/fxmetools/core)
set_source_files_properties(src/rhythm/wdl_fft.c PROPERTIES COMPILE_OPTIONS "-w")
if(APPLE)
  target_compile_definitions(phantom_thirdparty INTERFACE AUDIOFFT_APPLE_ACCELERATE=1)
endif()

# ---- CLAP (clap-juce-extensions, MIT; pulls the CLAP SDK + clap-helpers as its submodules), pinned by commit
if(PHANTOM_BUILD_CLAP)
  FetchContent_Declare(clap-juce-extensions GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git GIT_TAG 6bde108329ff9fc24404b79b8b397261a75cf6a8 GIT_PROGRESS FALSE)
  FetchContent_MakeAvailable(clap-juce-extensions)
endif()

set(PHANTOM_FORMATS VST3 Standalone)
if(PHANTOM_BUILD_AU AND APPLE)
  list(APPEND PHANTOM_FORMATS AU)
endif()

# ---- Phase 6 (Master) engines, pinned by commit (THIRD_PARTY.md). Each is compiled unchanged; src/master/*.cpp and src/rhythm/rubberband.cpp host them.
set(PHANTOM_LIMITER_ENGINE "ZL" CACHE STRING "GHOST LIMITER engine the plugin uses: ZL (passes the true-peak check, DECISIONS.md 37) or LSP (the brief's baseline); the tests build both")
set(PHANTOM_WARP_ENGINE "SIGNALSMITH" CACHE STRING "WARP PITCH engine the plugin uses: SIGNALSMITH or RUBBERBAND; the tests build both")
# GHOST SQUEEZE: vitOTTx (GPL-3.0) Source/vital_dsp = Vital's MultibandCompressor
FetchContent_Declare(vitottx GIT_REPOSITORY https://github.com/Sakhnovkrg/vitOTTx.git GIT_TAG 738ba9dc33448ced48c37b341ce96bd917857224 GIT_SUBMODULES "" GIT_PROGRESS FALSE)
FetchContent_GetProperties(vitottx)
if(NOT vitottx_POPULATED)
  FetchContent_Populate(vitottx)
endif()
set(PHANTOM_VITAL_DIR ${vitottx_SOURCE_DIR}/Source/vital_dsp)
set(PHANTOM_VITAL_SOURCES ${PHANTOM_VITAL_DIR}/compressor.cpp ${PHANTOM_VITAL_DIR}/linkwitz_riley_filter.cpp ${PHANTOM_VITAL_DIR}/framework/operators.cpp
  ${PHANTOM_VITAL_DIR}/framework/processor.cpp ${PHANTOM_VITAL_DIR}/framework/utils.cpp ${PHANTOM_VITAL_DIR}/framework/value.cpp ${PHANTOM_VITAL_DIR}/utilities/smooth_value.cpp
  src/master/squeeze.cpp)
set_source_files_properties(${PHANTOM_VITAL_SOURCES} PROPERTIES INCLUDE_DIRECTORIES "${PHANTOM_VITAL_DIR};${PHANTOM_VITAL_DIR}/framework" COMPILE_OPTIONS "-w")
# GHOST LIMITER challenger: ZL Limiter (AGPL-3.0) source/dsp, on ZL's own Highway fork (Apache-2.0 / BSD-3), static dispatch at SSE4 as ZL's build does
FetchContent_Declare(zllimiter GIT_REPOSITORY https://github.com/ZL-Audio/ZLLimiter.git GIT_TAG f0de37a6c93ed9eaa3e8678ab4deab5e39209830 GIT_SUBMODULES "" GIT_PROGRESS FALSE)
FetchContent_GetProperties(zllimiter)
if(NOT zllimiter_POPULATED)
  FetchContent_Populate(zllimiter)
endif()
FetchContent_Declare(zlhighway GIT_REPOSITORY https://github.com/ZL-Audio/highway.git GIT_TAG 2607d3b5b0113992fe84d3848859eae13b3b52c1 GIT_PROGRESS FALSE)
FetchContent_GetProperties(zlhighway)
if(NOT zlhighway_POPULATED)
  FetchContent_Populate(zlhighway)
endif()
set(HWY_FORCE_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(HWY_ENABLE_CONTRIB OFF CACHE BOOL "" FORCE)
set(HWY_ENABLE_EXAMPLES OFF CACHE BOOL "" FORCE)
set(HWY_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
set(HWY_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(HWY_COMPILE_ONLY_STATIC ON CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(${zlhighway_SOURCE_DIR} zlhighway EXCLUDE_FROM_ALL)
set(PHANTOM_HWY_FLAGS -msse4.2 -mssse3 -maes -mpclmul -mpopcnt)
target_compile_definitions(hwy PUBLIC HWY_COMPILE_ONLY_STATIC=1)
target_compile_options(hwy PRIVATE ${PHANTOM_HWY_FLAGS} -w)
set(PHANTOM_ZL_SOURCES src/master/zl_limiter.cpp ${zllimiter_SOURCE_DIR}/source/dsp/filter/iir_filter/coeff/ivantsov_coeff.cpp)
set_source_files_properties(${PHANTOM_ZL_SOURCES} PROPERTIES INCLUDE_DIRECTORIES "${zllimiter_SOURCE_DIR}/source;${zlhighway_SOURCE_DIR}" COMPILE_OPTIONS "-msse4.2;-mssse3;-maes;-mpclmul;-mpopcnt;-w" COMPILE_DEFINITIONS "HWY_COMPILE_ONLY_STATIC=1")
# GHOST LIMITER baseline: LSP dsp-units + lsp-common-lib + lsp-dsp-lib (LGPL-3.0), the dsp library with its own x86 SIMD files
FetchContent_Declare(lspcommon GIT_REPOSITORY https://github.com/lsp-plugins/lsp-common-lib.git GIT_TAG f5ab90a8998e02063bfd93ecd29a8faa7fd0362c GIT_PROGRESS FALSE)
FetchContent_Declare(lspdsplib GIT_REPOSITORY https://github.com/lsp-plugins/lsp-dsp-lib.git GIT_TAG 9ddc099ef70e88dd46f3e3c0fe77eda379f90784 GIT_PROGRESS FALSE)
FetchContent_Declare(lspdspunits GIT_REPOSITORY https://github.com/lsp-plugins/lsp-dsp-units.git GIT_TAG b7a2475851bdf86cafcd5fb82115245696be5257 GIT_PROGRESS FALSE)
foreach(l lspcommon lspdsplib lspdspunits)
  FetchContent_GetProperties(${l})
  if(NOT ${l}_POPULATED)
    FetchContent_Populate(${l})
  endif()
endforeach()
set(L1 ${lspcommon_SOURCE_DIR}/src) 
set(L2 ${lspdsplib_SOURCE_DIR}/src/main)
set(L3 ${lspdspunits_SOURCE_DIR}/src/main)
set(PHANTOM_LSP_SOURCES
  ${L1}/main/atomic.cpp ${L1}/main/bits.cpp ${L1}/main/cpuid.cpp ${L1}/main/debug.cpp ${L1}/main/locale.cpp ${L1}/main/memrotate.cpp ${L1}/main/singletone.cpp
  ${L1}/main/ssort.cpp ${L1}/main/status.cpp ${L1}/main/stdio.cpp ${L1}/main/stdlib.cpp ${L1}/main/types.cpp
  ${L1}/arch/generic/generic.cpp ${L1}/arch/x86/x86.cpp ${L1}/arch/x86/sse.cpp ${L1}/arch/x86/avx.cpp ${L1}/arch/x86/avx512.cpp
  ${L2}/dsp.cpp ${L2}/generic/generic.cpp ${L2}/x86/x86.cpp ${L2}/x86/sse.cpp ${L2}/x86/sse2.cpp ${L2}/x86/sse3.cpp ${L2}/x86/sse4.cpp
  ${L2}/x86/avx.cpp ${L2}/x86/avx2.cpp ${L2}/x86/avx512.cpp ${L2}/x86/avx_nz.cpp
  ${L3}/dynamics/Limiter.cpp ${L3}/util/Delay.cpp ${L3}/util/Oversampler.cpp ${L3}/filters/Filter.cpp ${L3}/filters/FilterBank.cpp ${L3}/misc/interpolation.cpp)
if(APPLE)
  # Apple's clang cannot assemble lsp-dsp-lib's AVX2 / AVX-512 inline assembly: those two files are replaced by empty initialisers
  list(REMOVE_ITEM PHANTOM_LSP_SOURCES ${L2}/x86/avx2.cpp ${L2}/x86/avx512.cpp)
  list(APPEND PHANTOM_LSP_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/master/lsp_mac_stubs.cpp)
endif()
add_library(phantom_lsp STATIC ${PHANTOM_LSP_SOURCES})
target_include_directories(phantom_lsp PUBLIC ${lspcommon_SOURCE_DIR}/include ${lspdsplib_SOURCE_DIR}/include ${lspdspunits_SOURCE_DIR}/include PRIVATE ${lspdsplib_SOURCE_DIR}/src)
target_compile_definitions(phantom_lsp PUBLIC LSP_DSP_LIB_BUILTIN LSP_COMMON_LIB_BUILTIN LSP_DSP_UNITS_BUILTIN)
target_compile_options(phantom_lsp PRIVATE -w)
set_target_properties(phantom_lsp PROPERTIES CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
# lsp-dsp-lib's own per-file CPU flags (its src/Makefile)
set_source_files_properties(${L2}/x86/sse.cpp PROPERTIES COMPILE_OPTIONS "-mmmx;-msse")
set_source_files_properties(${L2}/x86/sse2.cpp PROPERTIES COMPILE_OPTIONS "-mmmx;-msse;-msse2")
set_source_files_properties(${L2}/x86/sse3.cpp PROPERTIES COMPILE_OPTIONS "-mmmx;-msse;-msse2;-msse3;-mssse3")
set_source_files_properties(${L2}/x86/sse4.cpp PROPERTIES COMPILE_OPTIONS "-mmmx;-msse;-msse2;-msse3;-mssse3;-msse4.1;-msse4.2")
set_source_files_properties(${L2}/x86/avx.cpp PROPERTIES COMPILE_OPTIONS "-mavx;-mvzeroupper")
set_source_files_properties(${L2}/x86/avx2.cpp PROPERTIES COMPILE_OPTIONS "-mavx;-mvzeroupper;-mavx2;-mfma")
set_source_files_properties(${L2}/x86/avx512.cpp PROPERTIES COMPILE_OPTIONS "-mavx512f;-mavx512vl")
set_source_files_properties(${L2}/x86/avx_nz.cpp PROPERTIES COMPILE_OPTIONS "-mavx;-mno-vzeroupper")
# WARP challenger: Rubber Band Library (GPL-2.0-or-later), its single-file build
FetchContent_Declare(rubberband GIT_REPOSITORY https://github.com/breakfastquay/rubberband.git GIT_TAG e4296ac80b1170018a110bc326fd0d45a0eb27d6 GIT_PROGRESS FALSE)
FetchContent_GetProperties(rubberband)
if(NOT rubberband_POPULATED)
  FetchContent_Populate(rubberband)
endif()
set_source_files_properties(src/rhythm/rubberband.cpp PROPERTIES INCLUDE_DIRECTORIES "${rubberband_SOURCE_DIR}" COMPILE_OPTIONS "-w")
set(PHANTOM_P6_SOURCES ${PHANTOM_VITAL_SOURCES} ${PHANTOM_ZL_SOURCES} src/master/lsp_limiter.cpp src/rhythm/rubberband.cpp)
set(PHANTOM_P6_LIBS hwy phantom_lsp)
if(PHANTOM_LIMITER_ENGINE STREQUAL "ZL")
  set(PHANTOM_P6_DEFS PHANTOM_LIMITER_ZL=1)
else()
  set(PHANTOM_P6_DEFS PHANTOM_LIMITER_ZL=0)
endif()
if(PHANTOM_WARP_ENGINE STREQUAL "RUBBERBAND")
  list(APPEND PHANTOM_P6_DEFS PHANTOM_WARP_RUBBERBAND=1)
else()
  list(APPEND PHANTOM_P6_DEFS PHANTOM_WARP_RUBBERBAND=0)
endif()

# ---- Phase 6b SPATIAL: Steam Audio (Apache-2.0) core, the classes the binaural / panning / distance path needs, vendored unchanged in
# third_party/steamaudio (commit 480dd64f, THIRD_PARTY.md), built as its own minimal build does without IPP / Embree / SOFA (PFFFT for
# FFTs, its pinned commit vendored in third_party/pffft). src/spatial/SteamVoice.cpp is our glue.
set(SA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/steamaudio/src/core)
set(PHANTOM_SA_SOURCES)
foreach(f log memory_allocator context math_functions array_math pffft_fft audio_buffer hrtf_map hrtf_map_factory hrtf_database binaural_effect
          overlap_add_convolution_effect air_absorption distance_attenuation panning_effect speaker_layout eq_effect gain_effect iir bands
          window_function hrtf ambisonics_panning_effect sh)
  list(APPEND PHANTOM_SA_SOURCES ${SA_DIR}/${f}.cpp)
endforeach()
add_library(phantom_steamaudio STATIC ${PHANTOM_SA_SOURCES} ${SA_DIR}/sh/spherical_harmonics.cc
  third_party/pffft/pffft.c third_party/pffft/pffft_common.c src/spatial/SteamVoice.cpp)
target_include_directories(phantom_steamaudio PRIVATE ${SA_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/third_party/pffft)
target_compile_definitions(phantom_steamaudio PRIVATE IPL_DISABLE_SOFA)
target_compile_options(phantom_steamaudio PRIVATE -w)
set_target_properties(phantom_steamaudio PROPERTIES CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
if(CMAKE_BUILD_TYPE STREQUAL "Debug" OR NOT CMAKE_BUILD_TYPE)
  target_compile_options(phantom_steamaudio PRIVATE -O2)   # HRTF database build + convolution are slow unoptimised
endif()
list(APPEND PHANTOM_P6_LIBS phantom_steamaudio)

# ---- extra engines (DECISIONS.md 85): LYNX SHARE's six reverbs (Ka1-S/LYNX-SHARE @ d141370, AGPL-3.0), vendored unchanged in
# third_party/lynx/core (plus two small shims, Song.h / Effects.h, for the declarations they take from the rest of LYNX). THIRD_PARTY.md.
add_library(phantom_lynx STATIC third_party/lynx/core/LynxVerbs.cpp third_party/lynx/core/ApexLimiter.cpp third_party/lynx/core/MixSuiteOversampler.cpp
  third_party/lynx/core/EchoSuite.cpp third_party/lynx/core/DynamicsSuite.cpp third_party/lynx/core/FacetEq.cpp third_party/lynx/core/ParametricEqLinear.cpp third_party/lynx/core/ParametricEqFl.cpp)
target_include_directories(phantom_lynx PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/lynx)
target_compile_options(phantom_lynx PRIVATE -w)
set_target_properties(phantom_lynx PROPERTIES CXX_STANDARD 20 POSITION_INDEPENDENT_CODE ON)
list(APPEND PHANTOM_P6_LIBS phantom_lynx)
# AnClark's Classic Reverb RE-04 and Classic Master Limiter RE-01 (GPL-3.0; PRIVATE BUILD ONLY, DECISIONS.md 85), vendored unchanged in
# third_party/kjaerhus with a minimal DPF shim; each is its own library (each has its own DistrhoPluginInfo.h and createPlugin, renamed here)
add_library(phantom_kj_reverb STATIC third_party/kjaerhus/reverb/ClassicReverbPlugin.cpp third_party/kjaerhus/reverb/CoreDSP.cpp)
target_include_directories(phantom_kj_reverb PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/kjaerhus/reverb ${CMAKE_CURRENT_SOURCE_DIR}/third_party/kjaerhus)
target_compile_definitions(phantom_kj_reverb PRIVATE createPlugin=createPlugin_RE04)
add_library(phantom_kj_limiter STATIC third_party/kjaerhus/limiter/ClassicMasterLimiter.cpp third_party/kjaerhus/limiter/CoreDSP.cpp)
target_include_directories(phantom_kj_limiter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/kjaerhus/limiter ${CMAKE_CURRENT_SOURCE_DIR}/third_party/kjaerhus)
target_compile_definitions(phantom_kj_limiter PRIVATE createPlugin=createPlugin_RE01)
foreach(t phantom_kj_reverb phantom_kj_limiter)
  target_compile_options(${t} PRIVATE -w)
  set_target_properties(${t} PROPERTIES CXX_STANDARD 20 POSITION_INDEPENDENT_CODE ON)
endforeach()
list(APPEND PHANTOM_P6_LIBS phantom_kj_reverb phantom_kj_limiter)
# CrushClone's DSP (doxiedevops/CrushClone @ aaa5a0c, MIT), vendored unchanged in third_party/crushclone, built with the plugin (JUCE)
set(PHANTOM_CRUSH_SOURCES third_party/crushclone/DSP/Distortion.cpp third_party/crushclone/DSP/LadderFilter.cpp third_party/crushclone/DSP/Compressor.cpp third_party/crushclone/DSP/Oversampler.cpp)
set_source_files_properties(${PHANTOM_CRUSH_SOURCES} src/engines/CrusherEngine.cpp PROPERTIES INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/third_party/crushclone" COMPILE_OPTIONS "-w")
# the CURVES engine's sets (assets/curves; PRIVATE: see the README there), embedded as they are
file(GLOB PHANTOM_CURVE_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/curves/*.gbc)
juce_add_binary_data(PhantomCurves HEADER_NAME PhantomCurvesData.h NAMESPACE PhantomCurvesData SOURCES ${PHANTOM_CURVE_FILES} ${CMAKE_CURRENT_SOURCE_DIR}/assets/curves/index.json)
set_target_properties(PhantomCurves PROPERTIES POSITION_INDEPENDENT_CODE ON)
list(APPEND PHANTOM_P6_LIBS PhantomCurves)

set(PHANTOM_SOURCES
  src/plugin/PluginProcessor.cpp
  src/plugin/PluginEditor.cpp
  src/effects/Catalog.cpp src/effects/ControlUse.cpp
  src/engines/Engines.cpp
  src/engines/CurveLibrary.cpp
  src/engines/CrusherEngine.cpp ${PHANTOM_CRUSH_SOURCES}
  src/aw/aw_Tube2.cpp src/aw/aw_Console7Channel.cpp src/aw/aw_DrumSlam.cpp src/aw/aw_Density.cpp src/aw/aw_FathomFive.cpp src/aw/aw_BassKit.cpp
  src/aw/aw_ZLowpass2.cpp src/aw/aw_ZHighpass2.cpp src/aw/aw_ZBandpass2.cpp src/aw/aw_ZNotch2.cpp src/aw/aw_ZAcidLowpass.cpp
  src/chow/ChowTape.cpp
  src/rhythm/foundations.cpp src/rhythm/wdl_fft.c
  src/space/fftconv.cpp src/space/nimbus.cpp ${PHANTOM_CLOUDS_SOURCES} ${PHANTOM_FV3_SOURCES} src/space/cs_Parameters.cpp src/space/cs_Biquad.cpp src/space/cs_RandomBuffer.cpp ${PHANTOM_P6_SOURCES})

# ---- the painted looks (0.1.9): every folder in assets/looks/ is one look (sprites in the designer's export structure + look.json).
# Each folder is zipped at build time and embedded with the bundled typeface; src/ui/PaintedLook.h finds them by name, so a new
# folder (tools/pack_look.py <export>/<look>) is a new look with no code change.
file(GLOB PHANTOM_LOOK_DIRS LIST_DIRECTORIES true CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/assets/looks/*)
set(PHANTOM_LOOK_ZIPS)
foreach(d ${PHANTOM_LOOK_DIRS})
  if(IS_DIRECTORY ${d} AND EXISTS ${d}/look.json)
    get_filename_component(lk ${d} NAME)
    file(GLOB_RECURSE lkfiles CONFIGURE_DEPENDS RELATIVE ${d} ${d}/*.png ${d}/*.jpg ${d}/*.json)
    list(SORT lkfiles)
    set(lkdeps)
    foreach(f ${lkfiles})
      list(APPEND lkdeps ${d}/${f})
    endforeach()
    set(z ${CMAKE_BINARY_DIR}/looks/look-${lk}.zip)
    add_custom_command(OUTPUT ${z} COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/looks COMMAND ${CMAKE_COMMAND} -E rm -f ${z}
      COMMAND ${CMAKE_COMMAND} -E tar cf ${z} --format=zip -- ${lkfiles} WORKING_DIRECTORY ${d} DEPENDS ${lkdeps} COMMENT "Packing the ${lk} look" VERBATIM)
    list(APPEND PHANTOM_LOOK_ZIPS ${z})
  endif()
endforeach()
juce_add_binary_data(PhantomLooks HEADER_NAME PhantomLooksData.h NAMESPACE PhantomLooksData SOURCES ${PHANTOM_LOOK_ZIPS}
  assets/fonts/ZenMaruGothic-Bold.ttf assets/fonts/ZenMaruGothic-Black.ttf assets/fonts/OFL-ZenMaruGothic.txt)
set_target_properties(PhantomLooks PROPERTIES POSITION_INDEPENDENT_CODE ON)

juce_add_plugin(PhantomFX
  COMPANY_NAME "GhostBlock"
  BUNDLE_ID "io.ghostblock.phantomfx"
  PLUGIN_MANUFACTURER_CODE Ghbk
  PLUGIN_CODE PhFx
  FORMATS ${PHANTOM_FORMATS}
  PRODUCT_NAME "Phantom FX"
  VERSION ${PHANTOM_VERSION}
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  COPY_PLUGIN_AFTER_BUILD FALSE
  AU_MAIN_TYPE kAudioUnitType_Effect
  VST3_CATEGORIES Fx)
if(PHANTOM_OLD_LOOKS)
  list(APPEND PHANTOM_P6_DEFS PHANTOM_OLD_LOOKS=1)
endif()
target_sources(PhantomFX PRIVATE ${PHANTOM_SOURCES})
target_include_directories(PhantomFX PRIVATE src)
target_compile_definitions(PhantomFX PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0)
target_compile_definitions(PhantomFX PRIVATE ${PHANTOM_P6_DEFS})
target_link_libraries(PhantomFX PRIVATE juce::juce_audio_utils juce::juce_dsp phantom_thirdparty PhantomLooks ${PHANTOM_P6_LIBS}
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
if(PHANTOM_BUILD_CLAP)
  clap_juce_extensions_plugin(TARGET PhantomFX CLAP_ID "io.ghostblock.phantomfx" CLAP_FEATURES audio-effect multi-effects stereo)
endif()

# ---- the test harness (the check table + rack checks) as a console app that compiles the processor in
if(PHANTOM_BUILD_TESTS)
  juce_add_console_app(PhantomTests PRODUCT_NAME "PhantomTests")
  target_sources(PhantomTests PRIVATE tests/PhantomTests.cpp ${PHANTOM_SOURCES})
  target_include_directories(PhantomTests PRIVATE src)
  target_compile_definitions(PhantomTests PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_REPORT_APP_USAGE=0
    JucePlugin_Name="Phantom FX" JucePlugin_VersionString="${PHANTOM_VERSION}" JucePlugin_Build_Standalone=1
    JucePlugin_IsSynth=0 JucePlugin_WantsMidiInput=1 JucePlugin_ProducesMidiOutput=0 JucePlugin_IsMidiEffect=0
    JucePlugin_EditorRequiresKeyboardFocus=1 JucePlugin_ManufacturerCode=0x4768626b JucePlugin_PluginCode=0x50684678
    JucePlugin_Manufacturer="GhostBlock" JucePlugin_VersionCode=0x100 JucePlugin_Desc="Phantom FX")
  target_compile_definitions(PhantomTests PRIVATE ${PHANTOM_P6_DEFS})
  target_link_libraries(PhantomTests PRIVATE juce::juce_audio_utils juce::juce_dsp juce::juce_gui_extra phantom_thirdparty PhantomLooks ${PHANTOM_P6_LIBS}
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()

# ---- GHOST ATLAS (ghost-atlas/): the eight-layer sample instrument, built from the same effect sources in this directory's
# scope so every per-file setting above applies to it too. Off unless asked for; Phantom FX itself is unchanged by it.
option(PHANTOM_WITH_GHOST_ATLAS "Also build Ghost Atlas (../ghost-atlas)" OFF)
if(PHANTOM_WITH_GHOST_ATLAS)
  include(${CMAKE_CURRENT_SOURCE_DIR}/../ghost-atlas/GhostAtlas.cmake)
endif()
