# GHOST BREAK - GhostBlock's private jungle / drum & bass break instrument (AGPL-3.0-or-later; it hosts Phantom FX's units).
# Included from phantom-fx/CMakeLists.txt (-DPHANTOM_WITH_GHOST_BREAK=ON) so it compiles the very same effect sources, with
# the same per-file settings, as the newest Phantom FX. It adds no dependency of its own.
set(GB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../ghost-break)
file(STRINGS ${GB_DIR}/BUILD GB_BUILD LIMIT_COUNT 1)
set(GB_VERSION 0.1.${GB_BUILD})
set(GB_CORE ${PHANTOM_SOURCES})
list(REMOVE_ITEM GB_CORE src/plugin/PluginProcessor.cpp src/plugin/PluginEditor.cpp)
file(GLOB GB_ART CONFIGURE_DEPENDS ${GB_DIR}/assets/*.jpg ${GB_DIR}/assets/*.png ${GB_DIR}/assets/*.ttf)
juce_add_binary_data(GhostBreakArt HEADER_NAME GhostBreakArt.h NAMESPACE GhostBreakArt SOURCES ${GB_ART})
set_target_properties(GhostBreakArt PROPERTIES POSITION_INDEPENDENT_CODE ON)
# the split sidechain's crossover and level detector: LSP DSP Units (LGPL-3.0, the same pinned commit Phantom FX's limiter uses)
set(GB_LSP ${lspdspunits_SOURCE_DIR}/src/main)
add_library(ghostbreak_lsp STATIC ${GB_LSP}/util/Crossover.cpp ${GB_LSP}/filters/Equalizer.cpp ${GB_LSP}/util/Sidechain.cpp ${GB_LSP}/util/RawRingBuffer.cpp
  ${GB_LSP}/misc/windows.cpp ${GB_LSP}/util/Convolver.cpp)
target_link_libraries(ghostbreak_lsp PUBLIC phantom_lsp)
target_compile_options(ghostbreak_lsp PRIVATE -w)
set_target_properties(ghostbreak_lsp PROPERTIES CXX_STANDARD 17 POSITION_INDEPENDENT_CODE ON)
set(GB_SOURCES ${GB_CORE} ${GB_DIR}/src/Processor.cpp ${GB_DIR}/src/Editor.cpp ${GB_DIR}/src/Duck.cpp)
set(GB_INCLUDES src ${GB_DIR}/src ${GB_DIR}/../ghosty-pro/Mickens)
set(GB_LIBS juce::juce_audio_utils juce::juce_dsp phantom_thirdparty GhostBreakArt ghostbreak_lsp ${PHANTOM_P6_LIBS})

juce_add_plugin(GhostBreak
  COMPANY_NAME "GhostBlock"
  BUNDLE_ID "io.ghostblock.ghostbreak"
  PLUGIN_MANUFACTURER_CODE Ghbk
  PLUGIN_CODE GhBk
  FORMATS ${PHANTOM_FORMATS}
  PRODUCT_NAME "Ghost Break"
  VERSION ${GB_VERSION}
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  COPY_PLUGIN_AFTER_BUILD FALSE
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  VST3_CATEGORIES Instrument Drum Sampler)
target_sources(GhostBreak PRIVATE ${GB_SOURCES})
target_include_directories(GhostBreak PRIVATE ${GB_INCLUDES})
target_compile_definitions(GhostBreak PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_REPORT_APP_USAGE=0)
target_compile_definitions(GhostBreak PRIVATE ${PHANTOM_P6_DEFS} GHOSTBREAK_VERSION="${GB_VERSION}")
target_link_libraries(GhostBreak PRIVATE ${GB_LIBS} PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
if(PHANTOM_BUILD_CLAP)
  clap_juce_extensions_plugin(TARGET GhostBreak CLAP_ID "io.ghostblock.ghostbreak" CLAP_FEATURES instrument drum-machine sampler stereo)
endif()

if(PHANTOM_BUILD_TESTS)
  juce_add_console_app(GhostBreakTests PRODUCT_NAME "GhostBreakTests")
  target_sources(GhostBreakTests PRIVATE ${GB_DIR}/tests/Tests.cpp ${GB_SOURCES})
  target_include_directories(GhostBreakTests PRIVATE ${GB_INCLUDES})
  target_compile_definitions(GhostBreakTests PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_REPORT_APP_USAGE=0
    JucePlugin_Name="Ghost Break" JucePlugin_VersionString="${GB_VERSION}" JucePlugin_Build_Standalone=1
    JucePlugin_IsSynth=1 JucePlugin_WantsMidiInput=1 JucePlugin_ProducesMidiOutput=0 JucePlugin_IsMidiEffect=0
    JucePlugin_EditorRequiresKeyboardFocus=1 JucePlugin_ManufacturerCode=0x4768626b JucePlugin_PluginCode=0x4768426b
    JucePlugin_Manufacturer="GhostBlock" JucePlugin_VersionCode=0x100 JucePlugin_Desc="Ghost Break"
    ${PHANTOM_P6_DEFS} GHOSTBREAK_VERSION="${GB_VERSION}")
  target_link_libraries(GhostBreakTests PRIVATE ${GB_LIBS} juce::juce_gui_extra PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
set_source_files_properties(${GB_DIR}/src/Duck.cpp PROPERTIES COMPILE_OPTIONS "-w")
