# GHOST (GhostBlock) - the fast song maker.
#
# GHOST is a host: it opens the owner's own installed plug-ins inside itself and wires them together.
#   ArpEZator      -> writes the song (chords, arp, melody, bass on MIDI channels 3, 1, 2, 4)
#   4 lanes        -> each lane plays one sound from Ghosty Pro 2, Poltergeist 2 or Ghost Atlas (the real plug-ins)
#   SPECTRAL FX    -> Poltergeist 2's FX rack (4 bays + WARP), compiled in, one per lane
#   Ghosty Beats   -> the drums (its own QUICK page, sequencer and drum sounds)
# Nothing of those plug-ins is copied or rebuilt here except the SPECTRAL FX code (upstream DSP, unmodified, in libs/).
cmake_minimum_required(VERSION 3.24)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
project(Ghost VERSION 1.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE URL https://github.com/juce-framework/JUCE/archive/refs/tags/8.0.4.zip DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(JUCE)
include(cmake/PatchJuceHost.cmake)

if(APPLE)
  set(GHOST_FORMATS VST3)
else()
  set(GHOST_FORMATS VST3 Standalone)
endif()

juce_add_plugin(Ghost
  COMPANY_NAME "GhostBlock"
  BUNDLE_ID "io.ghostblock.ghost"
  PLUGIN_MANUFACTURER_CODE Ghbk
  PLUGIN_CODE Ghst
  PRODUCT_NAME "GHOST"
  DESCRIPTION "GHOST: ArpEZator + Ghosty Pro 2 + Poltergeist 2 + Ghost Atlas + Ghosty Beats in one song machine"
  VERSION ${PROJECT_VERSION}
  FORMATS ${GHOST_FORMATS}
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  VST3_CATEGORIES Instrument Synth
  COPY_PLUGIN_AFTER_BUILD FALSE)
juce_generate_juce_header(Ghost)

# the factory sounds of the three synths (their own files, so GHOST can list and load them without the synth's help)
# (tools/pack_presets.py packs each synth's factory files into one .pack, keeping every sound's folder = its category)
juce_add_binary_data(GhostPresets HEADER_NAME GhostPresets.h NAMESPACE GhostPresets SOURCES
  presets/ghostypro2.pack presets/poltergeist2.pack presets/atlas/factory.json)
set_target_properties(GhostPresets PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---- SPECTRAL FX: Poltergeist 2's rack (our hosting code in p2/ghostblock/spectral, upstream DSP in libs/ghostblock-spectral)
set(SFX_DIR ${CMAKE_CURRENT_SOURCE_DIR}/p2/ghostblock/spectral)
set(SFX_SC ${CMAKE_CURRENT_SOURCE_DIR}/libs/ghostblock-spectral/spectral-corruptor/Source)
set(SFX_WC ${CMAKE_CURRENT_SOURCE_DIR}/libs/ghostblock-spectral/warpcore/src)
set(SFX_SC_SOURCES ${SFX_SC}/dsp/FFTEngine.cpp ${SFX_SC}/dsp/PartialProcessor.cpp ${SFX_SC}/dsp/DryWetMixer.cpp
                   ${SFX_DIR}/sc-engine.cpp ${SFX_DIR}/spectral-fx.cpp)
set_source_files_properties(${SFX_SC_SOURCES} PROPERTIES INCLUDE_DIRECTORIES "${SFX_SC}")
set(SFX_WC_SOURCES ${SFX_DIR}/warpcore-dispatch.cpp)
set(SFX_WC_COMMON "-ffast-math -Wno-psabi")
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(SFX_WC_COMMON "${SFX_WC_COMMON} -Wno-nan-infinity-disabled -Wno-unknown-warning-option")
endif()
function(sfx_warp_variant name)
  set(f ${SFX_DIR}/warpcore-isa-${name}.cpp)
  set(flags "${SFX_WC_COMMON}")
  foreach(m ${ARGN})
    if(APPLE)
      set(flags "${flags} -Xarch_x86_64 ${m}")
    else()
      set(flags "${flags} ${m}")
    endif()
  endforeach()
  set_source_files_properties(${f} PROPERTIES COMPILE_FLAGS "${flags}" INCLUDE_DIRECTORIES "${SFX_WC};${CMAKE_CURRENT_SOURCE_DIR}/libs/simde/simde")
  set(SFX_WC_SOURCES ${SFX_WC_SOURCES} ${f} PARENT_SCOPE)
endfunction()
if(APPLE OR CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64")
  sfx_warp_variant(sse2 -msse2)
  sfx_warp_variant(sse4 -msse4.1)
  sfx_warp_variant(avx -mavx)
  sfx_warp_variant(avx2 -mavx2)
  sfx_warp_variant(fma -mavx2 -mfma)
endif()
if(APPLE OR CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64")
  sfx_warp_variant(neon)
endif()

target_sources(Ghost PRIVATE
  src/GhostProcessor.cpp
  src/GhostEditor.cpp
  src/Catalog.cpp
  src/HostedPlugin.cpp
  src/LanePanels.cpp
  src/GraphSong.cpp
  ${SFX_SC_SOURCES} ${SFX_WC_SOURCES})
target_include_directories(Ghost PRIVATE src p2 libs/clap/include libs/sst-cpputils/include)
target_compile_definitions(Ghost PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0
  JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=0
  GHOST_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(Ghost PRIVATE GhostPresets
  juce::juce_audio_utils juce::juce_audio_processors juce::juce_dsp juce::juce_gui_extra
  PUBLIC juce::juce_recommended_config_flags)
if(NOT GHOST_FAST_DEV)
  target_link_libraries(Ghost PUBLIC juce::juce_recommended_lto_flags)
endif()

# ---- checks (Linux / CI): the catalogue, the state blobs, the routing (no plug-ins needed)
add_executable(ghost-tests EXCLUDE_FROM_ALL tests/GhostTests.cpp src/Catalog.cpp src/GraphSong.cpp)
target_include_directories(ghost-tests PRIVATE src)
target_compile_definitions(ghost-tests PRIVATE JUCE_STANDALONE_APPLICATION=1 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(ghost-tests PRIVATE GhostPresets juce::juce_audio_processors juce::juce_recommended_config_flags)

# the whole plug-in in a test program: opens real plug-ins from GHOST_VST3_DIR, plays, counts notes per lane, draws the window
add_executable(ghost-host-test EXCLUDE_FROM_ALL tests/HostTest.cpp src/GhostProcessor.cpp src/GhostEditor.cpp src/Catalog.cpp
  src/HostedPlugin.cpp src/LanePanels.cpp src/GraphSong.cpp ${SFX_SC_SOURCES} ${SFX_WC_SOURCES})
target_include_directories(ghost-host-test PRIVATE src p2 libs/clap/include libs/sst-cpputils/include)
target_compile_definitions(ghost-host-test PRIVATE JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0
  JUCE_PLUGINHOST_VST3=1 GHOST_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(ghost-host-test PRIVATE GhostPresets juce::juce_audio_utils juce::juce_audio_processors juce::juce_dsp
  juce::juce_gui_extra juce::juce_recommended_config_flags)
add_executable(ghost-probe EXCLUDE_FROM_ALL tests/Probe.cpp src/HostedPlugin.cpp src/Catalog.cpp)
target_include_directories(ghost-probe PRIVATE src)
target_compile_definitions(ghost-probe PRIVATE JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_PLUGINHOST_VST3=1)
target_link_libraries(ghost-probe PRIVATE GhostPresets juce::juce_audio_utils juce::juce_audio_processors juce::juce_recommended_config_flags)
