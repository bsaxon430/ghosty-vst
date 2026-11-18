# Ghosty Pro on Vital's engine (GPL-3).
#
# Everything under src/, third_party/, fonts/, icons/, tunings/ and default.vitalskin is Vital's own source
# (mtytel/vital @ 636ca0ef517a4db087a6a08a6a8a5e704e21f836), compiled unmodified through Vital's own unity
# build files. JUCE is Vital's own JUCE 6.0.x copy, driven by JUCE 6.0.5's official CMake support.
# Our code (UI and features only) lives in ghosty/.
#
# Targets
#   GhostyPro          VST3 / AU / Standalone (Mac); on Linux the shared code + Standalone (for tests and screenshots)
#   GhostyProTool      console tool: our processor, presets, renders, tests, screenshots
#   UpstreamHeadless   Vital's own src/headless/main.cpp, unmodified: the reference side of the null test (never shipped)
cmake_minimum_required(VERSION 3.15)
# The project version is the plug-in's own (bundle, VST3, AU). Vital's code writes an engine version into every saved
# preset ("synth_version") and checks it when opening one (it refuses presets of a newer major.minor): that stays 1.5.5,
# Vital's last public release, so presets saved in Ghosty Pro open in Vital 1.5 and Vital 1.5 presets open here.
project(GhostyPro VERSION 2.0.19 LANGUAGES C CXX)
set(GHOSTY_VERSION ${PROJECT_VERSION})
# Vital's engine version: written into and checked in .vital presets / skins / wavetables (marked edits in load_save.cpp,
# skin.cpp, wavetable_creator.cpp read GHOSTY_ENGINE_VERSION_*). The plug-in bundle itself is GHOSTY_VERSION, which must
# Ghosty Pro 2 has its own identity (bundle id, plug-in code), so it lives next to the old Ghosty Pro 3.x without clashing.
set(GHOSTY_ENGINE_VERSION 1.5.5)
set(GHOSTY_ENGINE_VERSION_NUMBER 0x010505)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
  enable_language(OBJC OBJCXX)
endif()

add_subdirectory(third_party/JUCE)

option(GHOSTY_BUILD_PLUGIN "Build the plugin (VST3/AU/Standalone)" ON)

set(VITAL_INCLUDES
  ${CMAKE_CURRENT_SOURCE_DIR}/src/common
  ${CMAKE_CURRENT_SOURCE_DIR}/src/common/wavetable
  ${CMAKE_CURRENT_SOURCE_DIR}/src/interface/editor_components
  ${CMAKE_CURRENT_SOURCE_DIR}/src/interface/editor_sections
  ${CMAKE_CURRENT_SOURCE_DIR}/src/interface/look_and_feel
  ${CMAKE_CURRENT_SOURCE_DIR}/src/interface/wavetable
  ${CMAKE_CURRENT_SOURCE_DIR}/src/interface/wavetable/editors
  ${CMAKE_CURRENT_SOURCE_DIR}/src/interface/wavetable/overlays
  ${CMAKE_CURRENT_SOURCE_DIR}/src/plugin
  ${CMAKE_CURRENT_SOURCE_DIR}/src/synthesis/synth_engine
  ${CMAKE_CURRENT_SOURCE_DIR}/src/synthesis/effects
  ${CMAKE_CURRENT_SOURCE_DIR}/src/synthesis/filters
  ${CMAKE_CURRENT_SOURCE_DIR}/src/synthesis/framework
  ${CMAKE_CURRENT_SOURCE_DIR}/src/synthesis/lookups
  ${CMAKE_CURRENT_SOURCE_DIR}/src/synthesis/modulators
  ${CMAKE_CURRENT_SOURCE_DIR}/src/synthesis/modules
  ${CMAKE_CURRENT_SOURCE_DIR}/src/synthesis/producers
  ${CMAKE_CURRENT_SOURCE_DIR}/src/synthesis/utilities
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

set(VITAL_ENGINE_SOURCES
  src/unity_build/synthesis.cpp
  src/unity_build/common.cpp)
set(VITAL_INTERFACE_SOURCES
  src/unity_build/interface_editor_components.cpp
  src/unity_build/interface_editor_sections.cpp
  src/unity_build/interface_editor_sections2.cpp
  src/unity_build/interface_look_and_feel.cpp
  src/unity_build/interface_wavetable.cpp
  src/unity_build/plugin.cpp)
# Vital's plugin.cpp (synth_plugin.cpp + synth_editor.cpp) also defines createPluginFilter(); Ghosty Pro's processor
# (a SynthPlugin) has its own, so Vital's is renamed for that one file. The file itself is untouched.
set_source_files_properties(src/unity_build/plugin.cpp PROPERTIES COMPILE_DEFINITIONS "createPluginFilter=vitalCreatePluginFilterUnused")

# Small machines (the local test box): Vital's interface files at -O1, which needs far less memory. Only the editor's
# drawing code is affected; the engine files keep the same flags in every target, so the null test is unaffected.
option(GHOSTY_LOWMEM_INTERFACE "Compile Vital's interface files at -O1 (local builds on small machines)" OFF)
if(GHOSTY_LOWMEM_INTERFACE)
  set_property(SOURCE ${VITAL_INTERFACE_SOURCES} APPEND PROPERTY COMPILE_OPTIONS -O1)
endif()

# Vital's flags (plugin/vital.jucer: -ffast-math -ftree-vectorize ... at -O3), used for every target so the null test
# compares like with like.
set(VITAL_FLAGS -ffast-math -fno-strict-aliasing)
if(NOT MSVC)
  list(APPEND VITAL_FLAGS -ftree-vectorize -funroll-loops)
endif()
set(VITAL_DEFS JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0
               JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_STRICT_REFCOUNTEDPOINTER=0 NO_AUTH=1 _ENABLE_EXTENDED_ALIGNED_STORAGE)
if(UNIX AND NOT APPLE)
  list(APPEND VITAL_DEFS LINUX=1)   # the Projucer Linux exporter defines it; Vital's code reads it
endif()

# ---- binary data: Vital's resources (fonts, icons, tunings, default skin) + our presets and art
file(GLOB GHOSTY_PRESETS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ghosty/presets/*.vital)
juce_add_binary_data(GhostyProData SOURCES
  fonts/DroidSansMono.ttf fonts/Lato-Light.ttf fonts/Lato-Regular.ttf fonts/Montserrat-Light.otf fonts/Montserrat-Regular.ttf
  icons/chorus.svg icons/compressor.svg icons/delay.svg icons/distortion.svg icons/drag_drop_arrows.svg
  icons/effects_filter.svg icons/equalizer.svg icons/flanger.svg icons/folder.svg icons/phaser.svg icons/reverb.svg
  # Vital's logo is its trademark: our ghost art takes its place under the same resource names
  ghosty/resources/brand/vital_ring.svg ghosty/resources/brand/vital_v.svg ghosty/resources/brand/vital_word.svg
  ghosty/resources/brand/vital_word_ring.svg
  "tunings/5 Limit.scl" "tunings/7 Limit.scl" tunings/Pythagorean.scl
  # Ghosty Pro's looks as Vital skins; GHOST is compiled in as Vital's own default skin resource (default.vitalskin)
  ghosty/resources/skins/default.vitalskin ghosty/resources/skins/ghosty_black.vitalskin
  ghosty/resources/skins/ghosty_sketch.vitalskin   # the LIGHT look (hand-drawn watercolor)
  ${GHOSTY_PRESETS})
set_target_properties(GhostyProData PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(GHOSTY_SOURCES
  ghosty/src/GhostyProcessor.cpp
  ghosty/src/GhostyEditor.cpp
  ghosty/src/GhostyPages.cpp
  ghosty/src/GhostyLook.cpp
  ghosty/src/VitalPresetFinder.cpp
  ghosty/src/GhostyPresets.cpp
  ghosty/src/GhostyMorph.cpp
  ghosty/src/GhostyMorphSets.cpp
  ghosty/src/GhostyExtras.cpp
  ghosty/src/GhostyMatrix.cpp
  ghosty/src/GhostyPresetMenu.cpp)
set(GHOSTY_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/ghosty/src ${CMAKE_CURRENT_SOURCE_DIR}/ghosty/shared ${CMAKE_CURRENT_SOURCE_DIR}/ghosty/fx)

set(GHOSTY_MODULES juce::juce_audio_basics juce::juce_audio_devices juce::juce_audio_formats juce::juce_audio_processors
  juce::juce_audio_utils juce::juce_core juce::juce_data_structures juce::juce_dsp juce::juce_events juce::juce_graphics
  juce::juce_gui_basics juce::juce_gui_extra juce::juce_opengl)

function(ghosty_vital_target target)
  target_include_directories(${target} PRIVATE ${VITAL_INCLUDES} ${GHOSTY_INCLUDES})
  target_compile_definitions(${target} PRIVATE ${VITAL_DEFS} GHOSTY_VERSION_STRING="${GHOSTY_VERSION}"
    GHOSTY_ENGINE_VERSION_STRING="${GHOSTY_ENGINE_VERSION}" GHOSTY_ENGINE_VERSION_NUMBER=${GHOSTY_ENGINE_VERSION_NUMBER})
  target_compile_options(${target} PRIVATE ${VITAL_FLAGS} $<$<CXX_COMPILER_ID:GNU>:-w> $<$<CXX_COMPILER_ID:AppleClang,Clang>:-Wno-everything>)
endfunction()

# ---- the plugin
if(GHOSTY_BUILD_PLUGIN)
  if(APPLE)
    set(GHOSTY_FORMATS VST3 AU Standalone)
  else()
    set(GHOSTY_FORMATS Standalone)
  endif()
  juce_add_plugin(GhostyPro
    COMPANY_NAME "GhostBlock"
    COMPANY_WEBSITE "https://github.com/bsaxon430"
    BUNDLE_ID "io.ghostblock.ghostypro2"
    PLUGIN_MANUFACTURER_CODE Ghbk
    PLUGIN_CODE Gpr2
    PRODUCT_NAME "Ghosty Pro 2"
    DESCRIPTION "Ghosty Pro 2: a wavetable synth on Vital's open-source engine (GPL-3)"
    VERSION ${PROJECT_VERSION}
    FORMATS ${GHOSTY_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    ICON_BIG ghosty/resources/brand/ghosty-pro-icon-512.png
    COPY_PLUGIN_AFTER_BUILD FALSE)
  juce_generate_juce_header(GhostyPro)
  target_sources(GhostyPro PRIVATE ${VITAL_ENGINE_SOURCES} ${VITAL_INTERFACE_SOURCES} ${GHOSTY_SOURCES})
  ghosty_vital_target(GhostyPro)
  target_compile_definitions(GhostyPro PUBLIC JUCE_OPENGL3=1)
  target_link_libraries(GhostyPro PRIVATE GhostyProData ${GHOSTY_MODULES}
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags)
endif()

# ---- the tool: the same processor and editor, outside a plugin wrapper
juce_add_console_app(GhostyProTool PRODUCT_NAME "GhostyProTool")
juce_generate_juce_header(GhostyProTool)
target_sources(GhostyProTool PRIVATE ${VITAL_ENGINE_SOURCES} ${VITAL_INTERFACE_SOURCES} ${GHOSTY_SOURCES}
  ghosty/tools/ToolMain.cpp ghosty/tests/GhostyTests.cpp)
ghosty_vital_target(GhostyProTool)
target_compile_definitions(GhostyProTool PRIVATE JUCE_OPENGL3=1 GHOSTY_TOOL=1
  JucePlugin_Name="Ghosty Pro 2" JucePlugin_WantsMidiInput=1 JucePlugin_ProducesMidiOutput=0 JucePlugin_IsSynth=1
  JucePlugin_VersionString="${PROJECT_VERSION}")
target_link_libraries(GhostyProTool PRIVATE GhostyProData ${GHOSTY_MODULES} juce::juce_recommended_config_flags)

# ---- Vital's own headless renderer, exactly as upstream builds it (headless/vital.jucer: HEADLESS=1 NO_AUTH=1)
juce_add_console_app(UpstreamHeadless PRODUCT_NAME "UpstreamHeadless" VERSION ${GHOSTY_ENGINE_VERSION})
juce_generate_juce_header(UpstreamHeadless)
target_sources(UpstreamHeadless PRIVATE ${VITAL_ENGINE_SOURCES} src/headless/main.cpp)
target_include_directories(UpstreamHeadless PRIVATE ${VITAL_INCLUDES})
target_compile_definitions(UpstreamHeadless PRIVATE ${VITAL_DEFS} HEADLESS=1)
target_compile_options(UpstreamHeadless PRIVATE ${VITAL_FLAGS} $<$<CXX_COMPILER_ID:GNU>:-w> $<$<CXX_COMPILER_ID:AppleClang,Clang>:-Wno-everything>)
target_link_libraries(UpstreamHeadless PRIVATE GhostyProData juce::juce_audio_basics juce::juce_audio_formats juce::juce_core
  juce::juce_data_structures juce::juce_dsp juce::juce_events juce::juce_recommended_config_flags)
