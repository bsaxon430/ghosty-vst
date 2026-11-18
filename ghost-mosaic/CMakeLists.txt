# GHOST MOSAIC - a GhostBlock fork of Samplebrain (Then Try This), GPL-2.0-or-later. See LICENSE / NOTICE.md.
# JUCE plug-in: VST3 + AU instrument "Ghost Mosaic", VST3 + AU effect "Ghost Mosaic FX" (live input is the target),
# and a standalone Mac app. The sound engine is upstream Samplebrain's (samplebrain/brain/src, a git subtree).
cmake_minimum_required(VERSION 3.24)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
project(GhostMosaic VERSION 0.1.0)
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/BUILD GHOSTMOSAIC_BUILD LIMIT_COUNT 1)
set(GHOSTMOSAIC_VERSION 0.1.${GHOSTMOSAIC_BUILD})

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE 8.0.4 (the same pinned release as the other GhostBlock plug-ins; AGPLv3 / commercial dual licence - used here under the AGPLv3)
set(GHOSTMOSAIC_JUCE_DIR "" CACHE PATH "Use this JUCE checkout instead of downloading it")
if(GHOSTMOSAIC_JUCE_DIR)
  add_subdirectory(${GHOSTMOSAIC_JUCE_DIR} juce)
else()
  include(FetchContent)
  FetchContent_Declare(JUCE URL https://github.com/juce-framework/JUCE/archive/refs/tags/8.0.4.zip DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(JUCE)
endif()

# FFTW 3.3.10 (GPL-2.0-or-later), the FFT the Samplebrain engine is written against. Built by cmake/build-fftw.sh
# from the official release (sha256-checked), static, with its thread-safe planner.
set(GHOSTMOSAIC_FFTW_DIR "" CACHE PATH "FFTW install prefix (include/fftw3.h, lib/libfftw3.a)")
if(NOT GHOSTMOSAIC_FFTW_DIR)
  message(FATAL_ERROR "Set -DGHOSTMOSAIC_FFTW_DIR (run cmake/build-fftw.sh first)")
endif()
add_library(fftw3_static STATIC IMPORTED)
set_target_properties(fftw3_static PROPERTIES IMPORTED_LOCATION ${GHOSTMOSAIC_FFTW_DIR}/lib/libfftw3.a
  INTERFACE_INCLUDE_DIRECTORIES ${GHOSTMOSAIC_FFTW_DIR}/include)
find_package(Threads REQUIRED)

option(GHOSTMOSAIC_FAST_DEV "Dev build: no LTO" OFF)
if(GHOSTMOSAIC_FAST_DEV)
  set(GHOSTMOSAIC_LTO "")
else()
  set(GHOSTMOSAIC_LTO juce::juce_recommended_lto_flags)
endif()

# ---- the upstream engine, compiled as upstream wrote it (compat/ stands in for libsndfile, liblo and PortAudio)
set(SB ${CMAKE_CURRENT_SOURCE_DIR}/samplebrain/brain/src)
add_library(samplebrain_engine STATIC
  ${SB}/block.cpp ${SB}/brain.cpp ${SB}/fft.cpp ${SB}/mfcc.cpp ${SB}/renderer.cpp ${SB}/search_params.cpp
  ${SB}/status.cpp ${SB}/window.cpp ${SB}/block_stream.cpp
  ${SB}/aquila/filter/MelFilterBank.cpp ${SB}/aquila/filter/MelFilter.cpp ${SB}/aquila/transform/Dct.cpp
  ${SB}/spiralcore/sample.cpp ${SB}/spiralcore/allocator.cpp ${SB}/spiralcore/stream.cpp)
# The engine's headers are found with -iquote (for "brain.h") plus a folder that holds only its
# spiralcore/ (for <spiralcore/...>): on a Mac's case-insensitive disk a plain -I to the engine's
# folder makes the system's <Block.h> (CoreFoundation) open the engine's block.h.
set(SBINC ${CMAKE_CURRENT_BINARY_DIR}/samplebrain-include)
file(MAKE_DIRECTORY ${SBINC})
if(NOT EXISTS ${SBINC}/spiralcore)
  file(CREATE_LINK ${SB}/spiralcore ${SBINC}/spiralcore SYMBOLIC)
endif()
target_include_directories(samplebrain_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/compat ${SBINC})
target_compile_options(samplebrain_engine PUBLIC "SHELL:-iquote ${SB}")
target_include_directories(samplebrain_engine PRIVATE ${SB})   # (fft.cpp says <fft.h>; the engine's own files include no system Block.h)
target_link_libraries(samplebrain_engine PUBLIC fftw3_static Threads::Threads)
set_target_properties(samplebrain_engine PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_STANDARD 17)
target_compile_options(samplebrain_engine PRIVATE -O3 -w)   # samplebrain.pro: -O3; upstream warnings left alone

# Poppins (SIL Open Font License 1.1, licence beside it), embedded unmodified
juce_add_binary_data(GhostMosaicFonts HEADER_NAME GhostMosaicFonts.h NAMESPACE GhostMosaicFonts SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/fonts/Poppins-Medium.ttf ${CMAKE_CURRENT_SOURCE_DIR}/fonts/Poppins-SemiBold.ttf ${CMAKE_CURRENT_SOURCE_DIR}/fonts/Poppins-Bold.ttf)
set_target_properties(GhostMosaicFonts PROPERTIES POSITION_INDEPENDENT_CODE ON)

# TERRAIN: GhostBlock's own shared landscape component (ghosty-pro/Shared/Terrain), copied in from main
include(${CMAKE_CURRENT_SOURCE_DIR}/../ghosty-pro/Shared/Terrain/Terrain.cmake)

set(GHOSTMOSAIC_ENGINE_SOURCES
  Source/Engine/Compat.cpp Source/Engine/RtRenderer.cpp Source/Engine/LiveTarget.cpp Source/Engine/Engine.cpp
  Source/Engine/Osc.cpp Source/Engine/DemoKits.cpp Source/Engine/TerrainShaping.cpp Source/Engine/Brightness.cpp ${GHOST_TERRAIN_SOURCES})
set(GHOSTMOSAIC_UI_SOURCES
  Source/Plugin/Processor.cpp Source/Plugin/Editor.cpp Source/Plugin/Swarm.cpp Source/Plugin/HeatWord.cpp
  Source/Plugin/HoodPage.cpp Source/Plugin/Look.cpp Source/Plugin/TerrainPage.cpp)
set(GHOSTMOSAIC_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0
  JUCE_USE_MP3AUDIOFORMAT=1 GHOSTMOSAIC_VERSION="${GHOSTMOSAIC_VERSION}")
set(GHOSTMOSAIC_MODULES juce::juce_audio_utils juce::juce_dsp juce::juce_osc)
if(APPLE)
  set(GHOSTMOSAIC_FORMATS VST3 AU Standalone)
  set(GHOSTMOSAIC_FX_FORMATS VST3 AU Standalone)
else()
  set(GHOSTMOSAIC_FORMATS VST3 Standalone)
  set(GHOSTMOSAIC_FX_FORMATS VST3)
endif()

function(ghostmosaic_target target)
  target_sources(${target} PRIVATE ${GHOSTMOSAIC_ENGINE_SOURCES} ${GHOSTMOSAIC_UI_SOURCES})
  target_include_directories(${target} PRIVATE Source Source/Engine Source/Plugin ${GHOST_TERRAIN_DIR})
  target_compile_definitions(${target} PUBLIC ${GHOSTMOSAIC_DEFS} ${ARGN})
  target_link_libraries(${target} PRIVATE samplebrain_engine GhostMosaicFonts ${GHOSTMOSAIC_MODULES}
    PUBLIC juce::juce_recommended_config_flags ${GHOSTMOSAIC_LTO} juce::juce_recommended_warning_flags)
  if(NOT MSVC)
    target_compile_options(${target} PRIVATE -Wno-float-equal -Wno-shadow-field -Wno-sign-conversion -Wno-shorten-64-to-32 -Wno-implicit-float-conversion -Wno-conversion)
  endif()
endfunction()

# the instrument (FL: Channel rack) + the standalone app
juce_add_plugin(GhostMosaic
  VERSION ${GHOSTMOSAIC_VERSION}
  COMPANY_NAME "GhostBlock"
  BUNDLE_ID "io.ghostblock.ghostmosaic"
  PLUGIN_MANUFACTURER_CODE Ghbk
  PLUGIN_CODE GhMo
  FORMATS ${GHOSTMOSAIC_FORMATS}
  PRODUCT_NAME "Ghost Mosaic"
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  COPY_PLUGIN_AFTER_BUILD FALSE
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  VST3_CATEGORIES Instrument Sampler)
ghostmosaic_target(GhostMosaic GHOSTMOSAIC_FX=0)

# the effect (FL: Mixer insert): the track's audio is the target, live
juce_add_plugin(GhostMosaicFX
  VERSION ${GHOSTMOSAIC_VERSION}
  COMPANY_NAME "GhostBlock"
  BUNDLE_ID "io.ghostblock.ghostmosaicfx"
  PLUGIN_MANUFACTURER_CODE Ghbk
  PLUGIN_CODE GhMf
  FORMATS ${GHOSTMOSAIC_FX_FORMATS}
  PRODUCT_NAME "Ghost Mosaic FX"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  COPY_PLUGIN_AFTER_BUILD FALSE
  AU_MAIN_TYPE kAudioUnitType_Effect
  VST3_CATEGORIES Fx)
ghostmosaic_target(GhostMosaicFX GHOSTMOSAIC_FX=1)

# tests: null tests against the upstream renderer, clicks, true peak, latency, allocation, sessions, demos, screenshots
option(GHOSTMOSAIC_BUILD_TESTS "Build the test harness" OFF)
if(GHOSTMOSAIC_BUILD_TESTS)
  foreach(fx 0 1)
    if(fx)
      set(T GhostMosaicFXTests)
      set(N "Ghost Mosaic FX")
    else()
      set(T GhostMosaicTests)
      set(N "Ghost Mosaic")
    endif()
    juce_add_console_app(${T} PRODUCT_NAME "${T}")
    target_sources(${T} PRIVATE tests/MosaicTests.cpp tests/AllocCounter.cpp ${GHOSTMOSAIC_ENGINE_SOURCES} ${GHOSTMOSAIC_UI_SOURCES})
    target_include_directories(${T} PRIVATE Source Source/Engine Source/Plugin ${GHOST_TERRAIN_DIR})
    target_compile_definitions(${T} PRIVATE ${GHOSTMOSAIC_DEFS} GHOSTMOSAIC_FX=${fx} GHOSTMOSAIC_TESTS=1 JUCE_STANDALONE_APPLICATION=1
      JUCE_MODAL_LOOPS_PERMITTED=1 JucePlugin_Name="${N}" JucePlugin_IsSynth=$<NOT:${fx}> JucePlugin_WantsMidiInput=$<NOT:${fx}>
      JucePlugin_ProducesMidiOutput=0 JucePlugin_IsMidiEffect=0)
    target_link_libraries(${T} PRIVATE samplebrain_engine GhostMosaicFonts ${GHOSTMOSAIC_MODULES}
      PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
    target_compile_options(${T} PRIVATE -Wno-float-equal -Wno-shadow-field -Wno-sign-conversion -Wno-shorten-64-to-32 -Wno-implicit-float-conversion -Wno-conversion)
  endforeach()
  # the host test: loads the built VST3 / AU as an instrument, like FL Studio, and checks it makes sound
  juce_add_console_app(GhostMosaicHostTest PRODUCT_NAME "GhostMosaicHostTest")
  target_sources(GhostMosaicHostTest PRIVATE tests/HostTest.cpp)
  target_compile_definitions(GhostMosaicHostTest PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=$<BOOL:${APPLE}> JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
  target_link_libraries(GhostMosaicHostTest PRIVATE juce::juce_audio_utils ${CMAKE_DL_LIBS}
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
