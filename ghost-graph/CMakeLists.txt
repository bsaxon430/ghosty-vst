# Ghost Graph -- music written as equations (its own small CMake project: cmake -S ghosty-pro/GhostGraph -B build).
# Mac: VST3 + Standalone (AU when Xcode is there). No third-party code beyond JUCE.
cmake_minimum_required(VERSION 3.24)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
project(GhostGraph VERSION 1.10.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE URL https://github.com/juce-framework/JUCE/archive/refs/tags/8.0.4.zip DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(JUCE)

set(GHOSTGRAPH_FORMATS VST3 Standalone)
option(GHOSTGRAPH_AU "Also build the AU (needs the Xcode app for Rez)" OFF)
if(APPLE AND GHOSTGRAPH_AU)
  list(APPEND GHOSTGRAPH_FORMATS AU)
endif()

juce_add_binary_data(GhostGraphArt HEADER_NAME BinaryData.h NAMESPACE GhostGraphArt
  SOURCES art/paper.png art/chroma.png art/ink.png art/wine.png art/ti.png art/ShareTechMono-Regular.ttf)
set_target_properties(GhostGraphArt PROPERTIES POSITION_INDEPENDENT_CODE ON)

juce_add_plugin(GhostGraph
  VERSION 1.10.0
  COMPANY_NAME "GhostBlock"
  BUNDLE_ID "io.ghostblock.ghostgraph"
  PLUGIN_MANUFACTURER_CODE Ghbk
  PLUGIN_CODE Ghgr
  FORMATS ${GHOSTGRAPH_FORMATS}
  PRODUCT_NAME "Ghost Graph"
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES Instrument Synth)
target_sources(GhostGraph PRIVATE GraphProcessor.cpp)
# the credits ride inside the plugin bundle
if(APPLE)
  add_custom_command(TARGET GhostGraph_VST3 POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/packaging/CREDITS.txt"
            "$<TARGET_BUNDLE_CONTENT_DIR:GhostGraph_VST3>/Resources/CREDITS.txt")
endif()
target_include_directories(GhostGraph PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(GhostGraph PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(GhostGraph PRIVATE GhostGraphArt juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# the checks (the ghostgraph-mac workflow builds and runs them)
option(GHOSTGRAPH_TESTS "Build the Ghost Graph checks" OFF)
if(GHOSTGRAPH_TESTS)
  juce_add_console_app(GhostGraphTests PRODUCT_NAME "GhostGraphTests")
  target_sources(GhostGraphTests PRIVATE tests/GraphTests.cpp GraphProcessor.cpp)
  target_include_directories(GhostGraphTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(GhostGraphTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
  target_link_libraries(GhostGraphTests PRIVATE GhostGraphArt juce::juce_audio_utils juce::juce_dsp juce::juce_gui_extra
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
