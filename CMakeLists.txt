cmake_minimum_required(VERSION 3.24)

# Mac: one build that runs natively on Apple Silicon and Intel, macOS 11+
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

project(GhostyVST VERSION 1.1.0)

if(APPLE)
  set(GHOSTY_FORMATS VST3 AU Standalone)   # FL Studio on Mac loads VST3 and AU
else()
  set(GHOSTY_FORMATS VST3 Standalone)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)
# Downloaded as a zip so Git isn't needed.
FetchContent_Declare(
  JUCE
  URL https://github.com/juce-framework/JUCE/archive/refs/tags/8.0.4.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(JUCE)

# ---------------- Ghosty (synth) ----------------
juce_add_plugin(GhostySynth
  COMPANY_NAME "GhostBlock"
  BUNDLE_ID "io.ghostblock.ghosty"
  PLUGIN_MANUFACTURER_CODE Ghbk
  PLUGIN_CODE Ghsy
  FORMATS ${GHOSTY_FORMATS}
  PRODUCT_NAME "Ghosty"
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES Instrument Synth)

target_sources(GhostySynth PRIVATE
  Synth/SynthProcessor.cpp
  Synth/SynthEditor.cpp)
target_include_directories(GhostySynth PRIVATE Shared Synth)
target_compile_definitions(GhostySynth PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(GhostySynth PRIVATE
  juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# The other GhostBlock plugins (Mr. Mickens, Poltergeist, Ghosty Beats, Ghosty Spectrum) are paid and built privately.
