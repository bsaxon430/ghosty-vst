# GHOST VOICE - a free, open-source singing-voice blender by GhostBlock (AGPL-3.0-only; see LICENSE and NOTICE.md).
# JUCE plug-in: VST3 + AU INSTRUMENT "Ghost Voice" (build 8: a channel-rack instrument, the CHOP page on INTERSECT's engine) and a standalone Mac app. The neural engine (Seed-VC, GPL-3.0) is NOT built in:
# it is an optional download that runs as a separate helper process (helper/), started by the plug-in.
# Build 2: our own DSP engines NATURAL and CLASSIC are built in (Source/Fusion: byte-for-byte copies of ghosty-pro/Shared's
# VoiceEngine / VoiceFusion / VoiceFusionClear / VoiceFusionNatural, on WORLD v1.0.1, modified BSD).
# Mac only for users (Linux builds exist only for the tests and screenshots).
cmake_minimum_required(VERSION 3.24)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
project(GhostVoice VERSION 0.1.0 LANGUAGES C CXX)
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/BUILD GHOSTVOICE_BUILD LIMIT_COUNT 1)
set(GHOSTVOICE_VERSION 0.1.${GHOSTVOICE_BUILD})
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE 8.0.14 (build 8: INTERSECT's own pin, which its code needs; used under its AGPLv3 option). Offline: -DGHOSTVOICE_JUCE_DIR=...
set(GHOSTVOICE_JUCE_DIR "" CACHE PATH "Use this JUCE checkout instead of downloading it")
if(GHOSTVOICE_JUCE_DIR)
  add_subdirectory(${GHOSTVOICE_JUCE_DIR} juce)
else()
  include(FetchContent)
  FetchContent_Declare(JUCE URL https://github.com/juce-framework/JUCE/archive/refs/tags/8.0.14.zip DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(JUCE)
endif()
# WORLD v1.0.1 (M. Morise, modified BSD): the analysis / synthesis under NATURAL and CLASSIC. The same pinned zip + SHA256 as
# ghosty-pro/SpectrumX/Voice.cmake. Offline: -DFETCHCONTENT_SOURCE_DIR_WORLD=/path/to/World-1.0.1
include(FetchContent)
FetchContent_Declare(world
  URL https://github.com/mmorise/World/archive/refs/tags/v1.0.1.zip
  URL_HASH SHA256=408d2429cdc33dac8dae253de4932b1a57d8a697b49d3ac4cd73dc2000fb24d1
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR ghostvoice-no-cmake-here)
FetchContent_MakeAvailable(world)
add_library(ghostvoice-world STATIC
  ${world_SOURCE_DIR}/src/cheaptrick.cpp ${world_SOURCE_DIR}/src/codec.cpp ${world_SOURCE_DIR}/src/common.cpp ${world_SOURCE_DIR}/src/d4c.cpp
  ${world_SOURCE_DIR}/src/dio.cpp ${world_SOURCE_DIR}/src/fft.cpp ${world_SOURCE_DIR}/src/harvest.cpp ${world_SOURCE_DIR}/src/matlabfunctions.cpp
  ${world_SOURCE_DIR}/src/stonemask.cpp ${world_SOURCE_DIR}/src/synthesis.cpp ${world_SOURCE_DIR}/src/synthesisrealtime.cpp)
target_include_directories(ghostvoice-world PUBLIC ${world_SOURCE_DIR}/src)
set_target_properties(ghostvoice-world PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(ghostvoice-world PRIVATE -w)   # third-party code: its warnings are not ours

include(Source/Chop/Intersect.cmake)

option(GHOSTVOICE_BUILD_PLUGIN "Build the plug-in + app (off = tests only)" ON)
option(GHOSTVOICE_BUILD_TESTS "Build GhostVoiceTests" ON)

set(GV_SOURCES
  Source/Engine/VoiceEngine.cpp Source/Engine/VoiceTerrainMap.cpp Source/Engine/DspEngine.cpp
  Source/Fusion/VoiceEngine.cpp Source/Fusion/VoiceFusion.cpp Source/Fusion/VoiceFusionClear.cpp Source/Fusion/VoiceFusionNatural.cpp
  Source/Terrain/TerrainModel.cpp Source/Terrain/TerrainMapping.cpp Source/Terrain/TerrainFeatures.cpp Source/Terrain/TerrainSerial.cpp
  Source/Terrain/TerrainInteraction.cpp Source/Terrain/TerrainDsp.cpp Source/Terrain/TerrainView.cpp
  Source/Plugin/Processor.cpp Source/Plugin/Editor.cpp Source/Plugin/UnderTheHood.cpp Source/Engine/SeedVcOptions.cpp
  Source/Chop/ChopEngine.cpp Source/Chop/ChopPage.cpp Source/Chop/ChopTools.cpp ${GV_INTERSECT_SOURCES})
set(GV_INCLUDES Source/Engine Source/Plugin Source/Shared Source/Terrain Source/UI Source/Chop ${GV_INTERSECT_INCLUDES})
set(GV_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_USE_MP3AUDIOFORMAT=1
  ${GV_INTERSECT_DEFS} GHOSTVOICE_VERSION="${GHOSTVOICE_VERSION}" GHOSTVOICE_DEV_HELPER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/helper")
set(GV_LIBS ghostvoice-world ${GV_INTERSECT_LIBS} juce::juce_audio_utils juce::juce_audio_formats juce::juce_gui_extra juce::juce_dsp)

if(GHOSTVOICE_BUILD_PLUGIN)
  juce_add_plugin(GhostVoice
    COMPANY_NAME "GhostBlock" BUNDLE_ID "com.ghostblock.ghostvoice" PLUGIN_MANUFACTURER_CODE Ghbk PLUGIN_CODE GhVi
    PRODUCT_NAME "Ghost Voice" VERSION ${GHOSTVOICE_VERSION}
    FORMATS VST3 AU Standalone IS_SYNTH TRUE NEEDS_MIDI_INPUT TRUE NEEDS_MIDI_OUTPUT FALSE EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice VST3_CATEGORIES Instrument Sampler
    MICROPHONE_PERMISSION_ENABLED TRUE MICROPHONE_PERMISSION_TEXT "Ghost Voice records your singing from the input when you press REC."
    COPY_PLUGIN_AFTER_BUILD FALSE)
  target_sources(GhostVoice PRIVATE ${GV_SOURCES})
  target_include_directories(GhostVoice PRIVATE ${GV_INCLUDES})
  target_compile_definitions(GhostVoice PUBLIC ${GV_DEFS})
  target_link_libraries(GhostVoice PRIVATE ${GV_LIBS} PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
  # the helper (and the lab's blend code it uses) ship inside every bundle: Contents/Resources/helper
  if(APPLE)
    foreach(fmt VST3 AU Standalone)
      if(TARGET GhostVoice_${fmt})
        get_target_property(_out GhostVoice_${fmt} LIBRARY_OUTPUT_DIRECTORY)
        add_custom_command(TARGET GhostVoice_${fmt} POST_BUILD
          COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:GhostVoice_${fmt}>/Resources/helper"
          COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/helper "$<TARGET_BUNDLE_CONTENT_DIR:GhostVoice_${fmt}>/Resources/helper")
      endif()
    endforeach()
  endif()
endif()

if(GHOSTVOICE_BUILD_TESTS)
  juce_add_console_app(GhostVoiceTests PRODUCT_NAME "GhostVoiceTests")
  target_sources(GhostVoiceTests PRIVATE tests/GhostVoiceTests.cpp ${GV_SOURCES})
  target_include_directories(GhostVoiceTests PRIVATE ${GV_INCLUDES})
  target_compile_definitions(GhostVoiceTests PRIVATE ${GV_DEFS} JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1 JucePlugin_VersionString="${GHOSTVOICE_VERSION}" JucePlugin_Name="Ghost Voice"
    GHOSTVOICE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(GhostVoiceTests PRIVATE ${GV_LIBS} PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
