# ---------------------------------------------------------------- Ghost Slice (GENERATE: drop a sound, press GENERATE)
# Included from CMakeLists.txt. C++20. Build 6: WORLD (modified BSD) + our own SMS morph / harmonic synthesis / phase-locked vocoder,
# the melody brain (ours + ArpEZator's engine + the POP909 / Aicd n-gram models, MIT). Older takes: our own TD-PSOLA voice engine + granular clouds + the HOLD engine's spectral freeze / blur,
# Signalsmith Stretch (MIT) for the phase-vocoder pitching, r8brain (MIT) for the rate conversion, Ghost Verb (our own) for SPACE.
# The render path is built without fast-math and without FMA contraction, so a take renders bit-exact on the same build + machine
# (a saved session re-renders its takes from their recipes). Test build number: GhostSlice/BUILD.
set(GSLICE_VERSION 1.11.0)
# r8brain-free-src (MIT, tag version-6.5, vendored unmodified in third_party/r8brain-free-src; see its PINNED.txt): the
# anti-aliased sample-rate conversion of the working copy (source rate -> host rate)
add_library(gslice_r8brain STATIC third_party/r8brain-free-src/r8bbase.cpp)
target_include_directories(gslice_r8brain PUBLIC third_party/r8brain-free-src)
set_target_properties(gslice_r8brain PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  target_compile_options(gslice_r8brain PRIVATE /fp:precise /W0)
else()
  target_compile_options(gslice_r8brain PRIVATE -ffp-contract=off -w)
endif()
# WORLD v1.0.1 (M. Morise, modified BSD; its licence ships with the plug-in: packaging/third_party/WORLD-LICENSE.txt): build 6's voice
# analysis (StoneMask / CheapTrick / D4C) for INSTRUMENT and PAD (GhostSlice/Vocoder.cpp). The same pinned zip + target as Voice.cmake
# (included earlier from CMakeLists.txt); declared here too when that file did not make it.
if(NOT TARGET ghosty-world)
  include(FetchContent)
  FetchContent_Declare(world
    URL https://github.com/mmorise/World/archive/refs/tags/v1.0.1.zip
    URL_HASH SHA256=408d2429cdc33dac8dae253de4932b1a57d8a697b49d3ac4cd73dc2000fb24d1
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR ghosty-no-cmake-here)
  FetchContent_MakeAvailable(world)
  add_library(ghosty-world STATIC
    ${world_SOURCE_DIR}/src/cheaptrick.cpp ${world_SOURCE_DIR}/src/codec.cpp ${world_SOURCE_DIR}/src/common.cpp ${world_SOURCE_DIR}/src/d4c.cpp
    ${world_SOURCE_DIR}/src/dio.cpp ${world_SOURCE_DIR}/src/fft.cpp ${world_SOURCE_DIR}/src/harvest.cpp ${world_SOURCE_DIR}/src/matlabfunctions.cpp
    ${world_SOURCE_DIR}/src/stonemask.cpp ${world_SOURCE_DIR}/src/synthesis.cpp ${world_SOURCE_DIR}/src/synthesisrealtime.cpp)
  target_include_directories(ghosty-world PUBLIC ${world_SOURCE_DIR}/src)
  set_target_properties(ghosty-world PROPERTIES POSITION_INDEPENDENT_CODE ON)
  if(NOT MSVC)
    target_compile_options(ghosty-world PRIVATE -w)
  endif()
endif()
# Professor Bernstein's ArpEZator engine (our own, JUCE-free): build 6's melody brain links its composer (ARPEZATOR / TRAP / HIP HOP /
# HOOK LAB styles) straight from ArpEZator/, the same files the ArpEZator plug-in builds; its recipe is embedded (GhostSlice/BrainData.inc)
add_library(gslice_arp STATIC ArpEZator/ArpCore.cpp ArpEZator/Composer.cpp ArpEZator/UrbanMoods.cpp ArpEZator/HookLab.cpp ArpEZator/Idea.cpp ArpEZator/Recipe.cpp)
target_include_directories(gslice_arp PUBLIC ArpEZator)
set_target_properties(gslice_arp PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
if(MSVC)
  target_compile_options(gslice_arp PRIVATE /fp:precise /W0 /bigobj)
else()
  target_compile_options(gslice_arp PRIVATE -ffp-contract=off -w)
endif()
# build 12: TERRAIN, the shared landscape control surface (Shared/Terrain, our own code)
include(${CMAKE_CURRENT_SOURCE_DIR}/Shared/Terrain/Terrain.cmake)
set(GSLICE_CORE_SOURCES
  ${GHOST_TERRAIN_SOURCES} GhostSlice/SliceTerrain.cpp
  GhostSlice/SliceTerrainMap.cpp
  GhostSlice/SourceAudio.cpp GhostSlice/Export.cpp
  GhostSlice/Analysis.cpp GhostSlice/AnalysisPitch.cpp GhostSlice/AnalysisCache.cpp GhostSlice/Syllables.cpp GhostSlice/Chords.cpp
  GhostSlice/Hold.cpp GhostSlice/Psola.cpp GhostSlice/GenDsp.cpp GhostSlice/Vocoder.cpp GhostSlice/VoiceMorph.cpp GhostSlice/PhaseLock.cpp GhostSlice/Melody.cpp GhostSlice/Brain.cpp GhostSlice/PlayEdit.cpp GhostSlice/Generate.cpp GhostSlice/GenerateEngine.cpp)
set(GSLICE_PLUGIN_SOURCES ${GSLICE_CORE_SOURCES} GhostSlice/SliceProcessor.cpp GhostSlice/SliceEditor.cpp)
# build 16: the window's art. Cinzel Regular / SemiBold and Cormorant Garamond Medium Italic (SIL Open Font License 1.1, from the
# google/fonts families via @fontsource; their licences ship beside them, GhostSlice/art/*-OFL.txt, and in the Mac package). Embedded
# only. (Build 18: the picture is drawn in code, no image.)
juce_add_binary_data(GhostSliceArt HEADER_NAME GhostSliceArt.h NAMESPACE GhostSliceArt SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/GhostSlice/art/Cinzel-Regular.ttf ${CMAKE_CURRENT_SOURCE_DIR}/GhostSlice/art/Cinzel-SemiBold.ttf
  ${CMAKE_CURRENT_SOURCE_DIR}/GhostSlice/art/CormorantGaramond-MediumItalic.ttf)
set_target_properties(GhostSliceArt PROPERTIES POSITION_INDEPENDENT_CODE ON)

juce_add_plugin(GhostSlice
  VERSION ${GSLICE_VERSION}
  COMPANY_NAME "GhostBlock"
  BUNDLE_ID "io.ghostblock.ghostslice"
  PLUGIN_MANUFACTURER_CODE Ghbk
  PLUGIN_CODE GhSl
  FORMATS ${GHOSTY_FORMATS}
  PRODUCT_NAME "Ghost Slice"
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  MICROPHONE_PERMISSION_ENABLED TRUE
  MICROPHONE_PERMISSION_TEXT "Ghost Slice records your voice or any sound when you press RECORD."
  COPY_PLUGIN_AFTER_BUILD FALSE
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  VST3_CATEGORIES Instrument Sampler)
target_sources(GhostSlice PRIVATE ${GSLICE_PLUGIN_SOURCES})
target_include_directories(GhostSlice PRIVATE Shared Shared/Terrain GhostSlice)
set_target_properties(GhostSlice PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_compile_definitions(GhostSlice PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_USE_MP3AUDIOFORMAT=1
  GSLICE_VERSION="${GSLICE_VERSION}")
if(MSVC)
  target_compile_options(GhostSlice PRIVATE /bigobj /fp:precise)
else()
  target_compile_options(GhostSlice PRIVATE -ffp-contract=off)
endif()
target_link_libraries(GhostSlice PRIVATE GhostSliceArt juce::juce_audio_utils juce::juce_dsp juce::juce_cryptography signalsmith-stretch gslice_r8brain ghosty-world gslice_arp
  PUBLIC juce::juce_recommended_config_flags ${GHOSTY_LTO} juce::juce_recommended_warning_flags)
# the shared code target of every format is C++20 too (the format wrappers compile in their own targets)
foreach(_fmt VST3 AU Standalone)
  if(TARGET GhostSlice_${_fmt})
    set_target_properties(GhostSlice_${_fmt} PROPERTIES CXX_STANDARD 20)
  endif()
endforeach()
# exact float compares are on purpose (model equality, unity rates): a per-source option lands after JUCE's warning flags
if(NOT MSVC)
  set_source_files_properties(${GSLICE_PLUGIN_SOURCES} GhostSlice/tests/SliceTests.cpp PROPERTIES COMPILE_OPTIONS "-Wno-float-equal")
endif()
ghosty_license(GhostSlice "${GUMROAD_GHOSTSLICE}")
ghosty_clap(GhostSlice io.ghostblock.ghostslice instrument sampler stereo)

# Ghost Slice tests (off by default; CI doesn't build them): every TYPE in key / clean / deterministic, MIDI playback, no allocation on
# the audio thread, the session with its takes, old sessions, loudness, speed, the four looks, the demo export gate, example renders.
option(GHOSTY_BUILD_SLICE_TESTS "Build the Ghost Slice test harness" OFF)
if(GHOSTY_BUILD_SLICE_TESTS)
  juce_add_console_app(GhostSliceTests PRODUCT_NAME "GhostSliceTests")
  target_sources(GhostSliceTests PRIVATE GhostSlice/tests/SliceTests.cpp ${GSLICE_PLUGIN_SOURCES})
  target_include_directories(GhostSliceTests PRIVATE Shared Shared/Terrain GhostSlice)
  set_target_properties(GhostSliceTests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
  target_compile_definitions(GhostSliceTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_USE_MP3AUDIOFORMAT=1 GSLICE_VERSION="${GSLICE_VERSION}" JucePlugin_Name="Ghost Slice" GSLICE_SHOTS_DIR="${CMAKE_BINARY_DIR}/uishots"
    GHOSTY_GUMROAD_PRODUCT="" GHOSTY_GUMROAD_BUNDLE="" GHOSTY_STORE_URL="")   # tests run as a licensed user unless they force the lock
  if(NOT MSVC)
    target_compile_options(GhostSliceTests PRIVATE -ffp-contract=off)
  endif()
  target_link_libraries(GhostSliceTests PRIVATE GhostSliceArt juce::juce_audio_utils juce::juce_audio_formats juce::juce_gui_extra juce::juce_dsp juce::juce_cryptography signalsmith-stretch gslice_r8brain ghosty-world gslice_arp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
