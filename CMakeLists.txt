cmake_minimum_required(VERSION 3.24)

# Mac: one build that runs natively on Apple Silicon and Intel, macOS 11+
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

project(GhostyVST VERSION 1.0.0)

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

# Signalsmith Stretch 1.4.0 (MIT): time-stretch / pitch-shift for Poltergeist's Sample Lab (pulls signalsmith-linear 0.6.4)
FetchContent_Declare(signalsmith-stretch URL https://github.com/Signalsmith-Audio/signalsmith-stretch/archive/refs/tags/1.4.0.zip DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(signalsmith-stretch)

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

# ---------------- Ghosty Beats (drum machine) ----------------
juce_add_plugin(GhostyBeats
  COMPANY_NAME "GhostBlock"
  BUNDLE_ID "io.ghostblock.ghostybeats"
  PLUGIN_MANUFACTURER_CODE Ghbk
  PLUGIN_CODE Ghbt
  FORMATS ${GHOSTY_FORMATS}
  PRODUCT_NAME "Ghosty Beats"
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES Instrument Drum)

# Ghosty Beats 2 (BeatsX/): left-to-right sequencer, 60+ sounds + your samples, patterns, song, pads.
# The old Beats/ headers stay because Ghosty Spectrum uses its drum recipes.
target_sources(GhostyBeats PRIVATE
  BeatsX/BeatsXProcessor.cpp
  BeatsX/BeatsXEditor.cpp)
target_include_directories(GhostyBeats PRIVATE Shared BeatsX)
target_compile_definitions(GhostyBeats PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(GhostyBeats PRIVATE
  juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------- Ghosty Spectrum
juce_add_plugin(GhostySpectrum
  COMPANY_NAME "GhostBlock"
  BUNDLE_ID "io.ghostblock.ghostyspectrum"
  PLUGIN_MANUFACTURER_CODE Ghbk
  PLUGIN_CODE Ghsp
  FORMATS ${GHOSTY_FORMATS}
  PRODUCT_NAME "Ghosty Spectrum"
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES Instrument Synth)

target_sources(GhostySpectrum PRIVATE
  Spectrum/SpectrumProcessor.cpp
  Spectrum/SpectrumEditor.cpp)
target_include_directories(GhostySpectrum PRIVATE Shared Synth Beats Spectrum)
target_compile_definitions(GhostySpectrum PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(GhostySpectrum PRIVATE
  juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------- Poltergeist (FM spirit synth)
juce_add_plugin(Poltergeist
  COMPANY_NAME "GhostBlock"
  BUNDLE_ID "io.ghostblock.poltergeist"
  PLUGIN_MANUFACTURER_CODE Ghbk
  PLUGIN_CODE Pltg
  FORMATS ${GHOSTY_FORMATS}
  PRODUCT_NAME "Poltergeist"
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  COPY_PLUGIN_AFTER_BUILD FALSE
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  VST3_CATEGORIES Instrument Synth)

target_sources(Poltergeist PRIVATE
  Poltergeist/PluginProcessor.cpp
  Poltergeist/PluginEditor.cpp)
target_include_directories(Poltergeist PRIVATE Poltergeist)
target_compile_definitions(Poltergeist PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(Poltergeist PRIVATE
  juce::juce_audio_utils juce::juce_dsp signalsmith-stretch
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_lto_flags juce::juce_recommended_warning_flags)
