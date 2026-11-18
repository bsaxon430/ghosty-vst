cmake_minimum_required(VERSION 3.24)

# Mac: one build that runs natively on Apple Silicon and Intel, macOS 11+
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")

project(GhostyPro VERSION 3.1.0)

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

# CLAP (all hosts that load CLAP: Bitwig, Reaper, FL Studio 2024+, Studio One...). clap-juce-extensions (MIT,
# free-audio, wraps the JUCE plugin as a .clap) + the CLAP SDK 1.2.7 (MIT) and clap-helpers (MIT), both git submodules
# of clap-juce-extensions (so this one is fetched with Git, submodules included). The repo publishes no newer release
# tag than 0.26.0 (CLAP 0.26, pre-1.0, which hosts no longer load), so it is pinned to a main commit.
# Local / offline: -DFETCHCONTENT_SOURCE_DIR_CLAP-JUCE-EXTENSIONS=/root/vendor/clap-juce-extensions
# The CLAP builds share each plugin's code target with VST3 / AU / Standalone (the shared code also gets
# HAS_CLAP_JUCE_EXTENSIONS=1, which no GhostBlock source reads), so those formats are unchanged.
option(GHOSTY_BUILD_CLAP "Also build every plugin as a CLAP (<Target>_CLAP)" ON)
# CI sets this OFF and builds the CLAP targets in a second `cmake --build --target ...` of the same configure, so a CLAP
# compile error can never stop (or hide behind) the VST3 / AU / Standalone build of the installer
option(GHOSTY_CLAP_IN_ALL "CLAP targets are part of the default (ALL) build" ON)
if(GHOSTY_BUILD_CLAP)
  FetchContent_Declare(clap-juce-extensions
    GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
    GIT_TAG 7adee3a1bd4684d4caa5601100e364abccff4b4d   # main 2026-10-06 (CLAP 1.2.7, clap-helpers a61bcdf)
    GIT_SUBMODULES_RECURSE TRUE)
  FetchContent_MakeAvailable(clap-juce-extensions)
endif()
# ghosty_clap(<target> <reverse-DNS CLAP id = the bundle id> <CLAP features...>)
function(ghosty_clap target clap_id)
  if(GHOSTY_BUILD_CLAP)
    clap_juce_extensions_plugin(TARGET ${target} CLAP_ID "${clap_id}" CLAP_FEATURES ${ARGN})
    if(NOT GHOSTY_CLAP_IN_ALL)
      set_target_properties(${target}_CLAP PROPERTIES EXCLUDE_FROM_ALL TRUE)
    endif()
  endif()
endfunction()

# ---------------- store / license keys (Gumroad) ----------------
# Fast local dev builds (CI leaves this OFF): no link-time optimisation + ccache. Release installers keep LTO.
option(GHOSTY_FAST_DEV "Dev build: no LTO, use ccache" OFF)
if(GHOSTY_FAST_DEV)
  set(GHOSTY_LTO "")
  find_program(GHOSTY_CCACHE ccache)
  if(GHOSTY_CCACHE)
    set(CMAKE_C_COMPILER_LAUNCHER ${GHOSTY_CCACHE})
    set(CMAKE_CXX_COMPILER_LAUNCHER ${GHOSTY_CCACHE})
  endif()
else()
  set(GHOSTY_LTO juce::juce_recommended_lto_flags)
endif()
include(${CMAKE_CURRENT_SOURCE_DIR}/store.cmake OPTIONAL)
# Fill these with the product IDs from Gumroad (Product > Content > License key). Left empty, the plugins build fully
# unlocked (no demo mode), so a build is never crippled before the store exists.
set(GUMROAD_BUNDLE "" CACHE STRING "Gumroad product_id of the all-in-one bundle (unlocks every plugin)")
set(GUMROAD_MICKENS "" CACHE STRING "Gumroad product_id: Mr. Mickens")
set(GUMROAD_POLTERGEIST "" CACHE STRING "Gumroad product_id: Poltergeist")
set(GUMROAD_BEATS_SPECTRUM "" CACHE STRING "Gumroad product_id: Ghosty Beats + Ghosty Spectrum")
set(GUMROAD_GHOSTYPRO "" CACHE STRING "Gumroad product_id: Ghosty Pro")
set(GUMROAD_GHOSTBOX "" CACHE STRING "Gumroad product_id: Ghost Box")
set(GUMROAD_ARPEZATOR "" CACHE STRING "Gumroad product_id: ArpEZator")
set(GUMROAD_GHOSTSLICE "" CACHE STRING "Gumroad product_id: Ghost Slice")
set(GUMROAD_GHOSTHUM "" CACHE STRING "Gumroad product_id: Ghost Hum")
set(GHOSTY_STORE_URL "https://bsaxon430.github.io/ghosty-vst/store/" CACHE STRING "Where BUY goes")
foreach(v BUNDLE MICKENS POLTERGEIST BEATS_SPECTRUM GHOSTYPRO GHOSTBOX ARPEZATOR GHOSTSLICE GHOSTHUM)   # store.cmake fills them unless given on the command line
  if("${GUMROAD_${v}}" STREQUAL "" AND NOT "${STORE_${v}}" STREQUAL "")
    set(GUMROAD_${v} "${STORE_${v}}")
  endif()
endforeach()
function(ghosty_license target product)
  target_compile_definitions(${target} PRIVATE GHOSTY_GUMROAD_PRODUCT="${product}" GHOSTY_GUMROAD_BUNDLE="${GUMROAD_BUNDLE}" GHOSTY_STORE_URL="${GHOSTY_STORE_URL}")
endfunction()

# ---------------- Ghosty Beats (drum machine) ----------------
juce_add_plugin(GhostyBeats
  VERSION 3.3.3
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
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  COPY_PLUGIN_AFTER_BUILD FALSE
  VST3_CATEGORIES Instrument Drum)

# Ghosty Beats 2 (BeatsX/): step grid + lanes + tracker, 330 synthesised sounds (rendered at load, cached on disk) + your samples,
# break chop, rhythm lab (DNA / breed / evolve), per-track processing, drag-out MIDI / audio, patterns, song, pads.
# The old Beats/ headers stay because Ghosty Spectrum uses its drum recipes.
target_sources(GhostyBeats PRIVATE
  BeatsX/BeatsXProcessor.cpp
  BeatsX/BeatsXEditor.cpp)
target_include_directories(GhostyBeats PRIVATE Shared BeatsX)
target_compile_definitions(GhostyBeats PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
# BLOCK STEP / GHOST HOUSE BANKS in Ghost Kit (owner 2026-10-30): both plugins' drum banks + their built-in synth drums (BeatsX/banks)
file(GLOB GHOSTKIT_BANK_FILES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/BeatsX/banks/*.mp3 ${CMAKE_CURRENT_SOURCE_DIR}/BeatsX/banks/*.wav)
juce_add_binary_data(GhostKitBanks HEADER_NAME GhostKitBanks.h NAMESPACE GhostKitBanks SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/BeatsX/banks/banks.json ${GHOSTKIT_BANK_FILES})
set_target_properties(GhostKitBanks PROPERTIES POSITION_INDEPENDENT_CODE ON)
# Ghosty Beats' look: the GHOSTY BEATS logo artwork (BeatsX/art, the owner's own art, used as is) and the pixel font for buttons and
# tabs, Press Start 2P (SIL Open Font License 1.1, google/fonts ofl/pressstart2p; the license ships beside it,
# BeatsX/fonts/PressStart2P-OFL.txt, and in the Mac package). Unmodified, embedded only (drawn 80 % wide).
juce_add_binary_data(GhostyAssets HEADER_NAME GhostyAssets.h NAMESPACE GhostyAssets SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/BeatsX/fonts/PressStart2P-Regular.ttf ${CMAKE_CURRENT_SOURCE_DIR}/BeatsX/art/ghosty_beats_logo.png)
set_target_properties(GhostyAssets PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(GhostyBeats PRIVATE GhostKitBanks GhostyAssets
  juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags ${GHOSTY_LTO} juce::juce_recommended_warning_flags)

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
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  COPY_PLUGIN_AFTER_BUILD FALSE
  MICROPHONE_PERMISSION_ENABLED TRUE
  MICROPHONE_PERMISSION_TEXT "Ghosty Spectrum's GHOST VOICE page records your voice when you press REC."
  VST3_CATEGORIES Instrument Synth)

# Ghosty Spectrum 2 (SpectrumX/): paint a loop on the frequency spectrum. Uses Poltergeist's engine for its
# synth sounds and the Ghosty Beats 2 drum library.
target_sources(GhostySpectrum PRIVATE
  SpectrumX/SpecProcessor.cpp
  SpectrumX/SpecEditor.cpp)
target_include_directories(GhostySpectrum PRIVATE Shared SpectrumX Poltergeist BeatsX)
target_compile_definitions(GhostySpectrum PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(GhostySpectrum PRIVATE
  juce::juce_audio_utils juce::juce_dsp signalsmith-stretch
  PUBLIC juce::juce_recommended_config_flags ${GHOSTY_LTO} juce::juce_recommended_warning_flags)

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
# PROCESS LAB data in Poltergeist projects (the old SAMPLE LAB page; its UI left in 2026-10): the engine part stays so an old
# project's processed recordings load and save unchanged (the test targets always build it; OFF compiles it out of the plugin)
option(GHOSTY_PROCESS_LAB "Poltergeist: PROCESS LAB data in old projects" ON)
if(GHOSTY_PROCESS_LAB)
  target_sources(Poltergeist PRIVATE Shared/ProcessLab.cpp)
  target_compile_definitions(Poltergeist PRIVATE GHOSTY_PROCESS_LAB=1)
endif()
target_include_directories(Poltergeist PRIVATE Shared Poltergeist)
target_compile_definitions(Poltergeist PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_USE_MP3AUDIOFORMAT=1)
target_link_libraries(Poltergeist PRIVATE
  juce::juce_audio_utils juce::juce_dsp signalsmith-stretch
  PUBLIC juce::juce_recommended_config_flags ${GHOSTY_LTO} juce::juce_recommended_warning_flags)

# Poltergeist VOCAL CHOP tests (off by default; CI doesn't build them): onsets, slice pitch / formants, Lab section
# pitch, TUNE TO KEY, state + undo round trips, screenshots and a demo render.
option(GHOSTY_BUILD_POLTERGEIST_TESTS "Build the Poltergeist chop test harness" OFF)
if(GHOSTY_BUILD_POLTERGEIST_TESTS)
  juce_add_console_app(PoltergeistTests PRODUCT_NAME "PoltergeistTests")
  target_sources(PoltergeistTests PRIVATE Poltergeist/tests/ChopTests.cpp Poltergeist/PluginProcessor.cpp Poltergeist/PluginEditor.cpp Shared/ProcessLab.cpp)
  target_include_directories(PoltergeistTests PRIVATE Shared Poltergeist)
  target_compile_definitions(PoltergeistTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1 GHOSTY_PROCESS_LAB=1 JUCE_USE_MP3AUDIOFORMAT=1)
  target_link_libraries(PoltergeistTests PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_gui_extra juce::juce_dsp signalsmith-stretch
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
  # BASS tests: old presets / projects unchanged, every new bass preset measured, filters, wobble sync, sub solo, screenshots, demos
  juce_add_console_app(PoltergeistBassTests PRODUCT_NAME "PoltergeistBassTests")
  target_sources(PoltergeistBassTests PRIVATE Poltergeist/tests/BassTests.cpp Poltergeist/PluginProcessor.cpp Poltergeist/PluginEditor.cpp Shared/ProcessLab.cpp)
  target_include_directories(PoltergeistBassTests PRIVATE Shared Poltergeist Poltergeist/tests)
  target_compile_definitions(PoltergeistBassTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1 GHOSTY_PROCESS_LAB=1 JUCE_USE_MP3AUDIOFORMAT=1)
  target_link_libraries(PoltergeistBassTests PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_gui_extra juce::juce_dsp signalsmith-stretch
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()

# Ghosty Beats tests (off by default; CI doesn't build them): every built-in sound, sample-accurate timing,
# MIDI / audio export, dropped samples, back-compat, undo, screenshots and demo renders.
option(GHOSTY_BUILD_BEATS_TESTS "Build the Ghosty Beats test harness" OFF)
if(GHOSTY_BUILD_BEATS_TESTS)
  juce_add_console_app(BeatsTests PRODUCT_NAME "BeatsTests")
  target_sources(BeatsTests PRIVATE BeatsX/tests/BeatsTests.cpp BeatsX/BeatsXProcessor.cpp BeatsX/BeatsXEditor.cpp)
  target_include_directories(BeatsTests PRIVATE Shared BeatsX)
  target_compile_definitions(BeatsTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
  target_link_libraries(BeatsTests PRIVATE GhostKitBanks GhostyAssets juce::juce_audio_utils juce::juce_audio_formats juce::juce_gui_extra juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
  target_compile_definitions(BeatsTests PRIVATE GHOSTY_GUMROAD_PRODUCT="" GHOSTY_GUMROAD_BUNDLE="" GHOSTY_STORE_URL="")   # tests run as a licensed user
endif()

# ---------------------------------------------------------------- Mr. Mickens (drum & bass break machine)
# 100 re-created classic breaks on an original vintage kit (no sampled recordings), 4 bays, drums, sidechain, FX.
juce_add_plugin(MrMickens
  COMPANY_NAME "GhostBlock"
  BUNDLE_ID "io.ghostblock.mrmickens"
  PLUGIN_MANUFACTURER_CODE Ghbk
  PLUGIN_CODE Mick
  FORMATS ${GHOSTY_FORMATS}
  PRODUCT_NAME "Mr Mickens"
  VERSION 3.1.0
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  COPY_PLUGIN_AFTER_BUILD FALSE
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  VST3_CATEGORIES Instrument Drum)

target_sources(MrMickens PRIVATE
  Mickens/MickensProcessor.cpp
  Mickens/MickensEditor.cpp)
target_include_directories(MrMickens PRIVATE Shared Mickens BeatsX)
target_compile_definitions(MrMickens PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(MrMickens PRIVATE
  juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags ${GHOSTY_LTO} juce::juce_recommended_warning_flags)

# Mr. Mickens 2 test harness (off by default; CI does not build it)
include(${CMAKE_CURRENT_SOURCE_DIR}/Mickens/tests/MickensTests.cmake OPTIONAL)
include(${CMAKE_CURRENT_SOURCE_DIR}/SpectrumX/SpectralLab.cmake OPTIONAL)   # Ghosty Spectrum SPECTRAL LAB sources + tests
include(${CMAKE_CURRENT_SOURCE_DIR}/SpectrumX/Arrange.cmake OPTIONAL)        # Ghosty Spectrum ARRANGE page tests (sources wired by the owner)
include(${CMAKE_CURRENT_SOURCE_DIR}/SpectrumX/Voice.cmake OPTIONAL)          # Ghosty Spectrum VOICE page (WORLD, BSD) + tests (sources wired by the owner)
include(${CMAKE_CURRENT_SOURCE_DIR}/BeatsX/LinearMelody.cmake OPTIONAL)          # Ghosty Beats LINEAR DRUMMING + MELODIC TRACKER + tests (sources wired by the owner)
include(${CMAKE_CURRENT_SOURCE_DIR}/SpectrumX/Arcs.cmake OPTIONAL)           # Ghosty Spectrum ARCS page (drawn lines = voices) + tests (sources wired by the owner)
include(${CMAKE_CURRENT_SOURCE_DIR}/Shared/ProcessLab.cmake OPTIONAL)        # PROCESS LAB shared engine tests (plugins add the sources themselves)
include(${CMAKE_CURRENT_SOURCE_DIR}/Shared/SharedTests.cmake OPTIONAL)       # Bypass / SyncTransport header tests (off by default)
if(GHOSTY_LINMEL_SOURCES)   # Ghosty Beats: LINEAR DRUMMING + MELODIC TRACKER are part of the plugin (and its test harness)
  target_sources(GhostyBeats PRIVATE ${GHOSTY_LINMEL_SOURCES})
  if(GHOSTY_BUILD_BEATS_TESTS)
    target_sources(BeatsTests PRIVATE ${GHOSTY_LINMEL_SOURCES})
  endif()
endif()

ghosty_license(GhostyBeats "${GUMROAD_BEATS_SPECTRUM}")
ghosty_license(GhostySpectrum "${GUMROAD_BEATS_SPECTRUM}")
ghosty_license(Poltergeist "${GUMROAD_POLTERGEIST}")
ghosty_license(MrMickens "${GUMROAD_MICKENS}")
ghosty_clap(GhostyBeats io.ghostblock.ghostybeats instrument drum-machine stereo)
ghosty_clap(GhostySpectrum io.ghostblock.ghostyspectrum instrument synthesizer stereo)
ghosty_clap(Poltergeist io.ghostblock.poltergeist instrument synthesizer sampler stereo)
ghosty_clap(MrMickens io.ghostblock.mrmickens instrument drum-machine stereo)

# ---------------------------------------------------------------- Ghosty Pro (paid upgrade of the free Ghosty synth)
# Same core as the free Ghosty (which stays unchanged in the public repo) plus unison,
# sample -> wavetable, mod matrix and the Ghost FX rack (GhostyPro/fx/).
juce_add_plugin(GhostyPro
  COMPANY_NAME "GhostBlock"
  BUNDLE_ID "io.ghostblock.ghostypro"
  PLUGIN_MANUFACTURER_CODE Ghbk
  PLUGIN_CODE Ghpr
  FORMATS ${GHOSTY_FORMATS}
  PRODUCT_NAME "Ghosty Pro"
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  COPY_PLUGIN_AFTER_BUILD FALSE
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  VST3_CATEGORIES Instrument Synth)
# ENGINES (round 3): the oscillator engines' DSP - the MIT-licensed STM32 module code (plaits / clouds / rings / stmlib) of
# github.com/pichenettes/eurorack, vendored IN the repo (pinned commits, patched: third_party/eurorack-mit/PINNED.txt + PATCHES.md;
# licence: THIRD_PARTY_LICENSES.md). GHOSTY_EURORACK_DIR can point at another copy of the same tree.
set(GHOSTY_EURORACK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/eurorack-mit" CACHE PATH "Ghosty Pro ENGINES: the vendored eurorack-mit tree")
set(GHOSTY_EURORACK_SOURCES
  plaits/resources.cc
  plaits/dsp/engine/virtual_analog_engine.cc plaits/dsp/engine/waveshaping_engine.cc plaits/dsp/engine/fm_engine.cc plaits/dsp/engine/additive_engine.cc
  plaits/dsp/engine/grain_engine.cc plaits/dsp/engine/swarm_engine.cc plaits/dsp/engine/string_engine.cc plaits/dsp/engine/modal_engine.cc
  plaits/dsp/engine/chord_engine.cc plaits/dsp/engine/particle_engine.cc plaits/dsp/chords/chord_bank.cc
  plaits/dsp/physical_modelling/string.cc plaits/dsp/physical_modelling/string_voice.cc plaits/dsp/physical_modelling/modal_voice.cc plaits/dsp/physical_modelling/resonator.cc
  plaits/dsp/speech/naive_speech_synth.cc plaits/dsp/speech/sam_speech_synth.cc
  clouds/resources.cc clouds/dsp/mu_law.cc
  rings/resources.cc rings/dsp/resonator.cc rings/dsp/string.cc
  stmlib/dsp/atan.cc
  stmlib/dsp/units.cc stmlib/utils/random.cc)
list(TRANSFORM GHOSTY_EURORACK_SOURCES PREPEND "${GHOSTY_EURORACK_DIR}/")
add_library(gpro_eurorack STATIC ${GHOSTY_EURORACK_SOURCES})
target_include_directories(gpro_eurorack PUBLIC ${GHOSTY_EURORACK_DIR})
target_compile_definitions(gpro_eurorack PRIVATE TEST=1)      # (stmlib: the desktop path, no ARM assembly)
set_target_properties(gpro_eurorack PROPERTIES POSITION_INDEPENDENT_CODE ON CXX_STANDARD 17)
if(MSVC)
  target_compile_options(gpro_eurorack PRIVATE /W0)
else()
  target_compile_options(gpro_eurorack PRIVATE -w)
endif()

# FACTORY PRESETS (2026-10-26, owner: "delete all the sounds from it, all the presets"): the shipped plugin has NO factory sound
# presets; the bank is INIT + your own sounds. The old library stays in the repo (GhostyPro/Pro*.inc) and comes back with
# -DGHOSTY_FACTORY_PRESETS=ON. GHOSTY_FACTORY_LAYER_COMPAT keeps the old data compiled in, hidden, only so older projects /
# saved sounds that play a factory sound as a STACK layer (stored by name) still sound the same.
# See docs/research/PRO-FACTORY-PRESETS-OFF.md. (ProTests: GHOSTY_PRO_TESTS_FACTORY_PRESETS, ON by default.)
option(GHOSTY_FACTORY_PRESETS "Ghosty Pro plugin: compile in the old factory sound presets (BROWSE + host programs)" OFF)
option(GHOSTY_FACTORY_LAYER_COMPAT "Ghosty Pro plugin with GHOSTY_FACTORY_PRESETS OFF: keep the old sounds hidden for older projects' STACK layers" OFF)   # owner 2026-10-27: no old projects to keep; all old sounds out of the plugin
file(GLOB GHOSTYPRO_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/GhostyPro/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/GhostyPro/fx/*.cpp)
target_sources(GhostyPro PRIVATE ${GHOSTYPRO_SOURCES})
target_compile_definitions(GhostyPro PRIVATE GHOSTY_FACTORY_PRESETS=$<BOOL:${GHOSTY_FACTORY_PRESETS}> GHOSTY_FACTORY_LAYER_COMPAT=$<BOOL:${GHOSTY_FACTORY_LAYER_COMPAT}>)
target_include_directories(GhostyPro PRIVATE Shared GhostyPro GhostyPro/fx)
target_compile_definitions(GhostyPro PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_USE_MP3AUDIOFORMAT=1)
# Ghosty Pro is big: MSVC's link-time code generation runs the Windows build machine out of memory, so no LTO there.
# signalsmith-stretch: the TRAP sample layer (pitch with formant keep).
if(MSVC)
  target_link_libraries(GhostyPro PRIVATE juce::juce_audio_utils juce::juce_dsp signalsmith-stretch gpro_eurorack
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
  target_compile_options(GhostyPro PRIVATE /bigobj)
else()
  target_link_libraries(GhostyPro PRIVATE juce::juce_audio_utils juce::juce_dsp signalsmith-stretch gpro_eurorack
    PUBLIC juce::juce_recommended_config_flags ${GHOSTY_LTO} juce::juce_recommended_warning_flags)
endif()
# (installer #23 Mac crash: an explicit destructor call through an abstract base with a non-virtual destructor is UB, and Apple
# clang compiled it to a trap. Never again: it is a build error on every clang.)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(GhostyPro PRIVATE -Werror=delete-abstract-non-virtual-dtor -Werror=delete-non-abstract-non-virtual-dtor)
endif()
ghosty_license(GhostyPro "${GUMROAD_GHOSTYPRO}")
ghosty_clap(GhostyPro io.ghostblock.ghostypro instrument synthesizer stereo)

# Ghosty Pro TRAP tests (off by default; CI doesn't build them): old presets unchanged vs a reference dump, stack sums,
# sample layer pitch / formants, character gain, tape, brass, state round trip, levels, screenshots and demo renders.
option(GHOSTY_BUILD_PRO_TESTS "Build the Ghosty Pro test harness" OFF)
option(GHOSTY_PRO_TESTS_FACTORY_PRESETS "ProTests: build with the old factory presets ON (the existing suites need them; OFF = the shipped bank, for 'ProTests nopresets')" ON)
if(GHOSTY_BUILD_PRO_TESTS)
  juce_add_console_app(ProTests PRODUCT_NAME "ProTests")
  file(GLOB PROTESTS_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/GhostyPro/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/GhostyPro/fx/*.cpp)
  target_sources(ProTests PRIVATE GhostyPro/tests/TrapTests.cpp ${PROTESTS_SOURCES})
  target_include_directories(ProTests PRIVATE Shared GhostyPro GhostyPro/fx GhostyPro/tests)
  target_compile_definitions(ProTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_USE_MP3AUDIOFORMAT=1 JucePlugin_Name="Ghosty Pro" GPRO_TAPS=1
    GHOSTY_FACTORY_PRESETS=$<BOOL:${GHOSTY_PRO_TESTS_FACTORY_PRESETS}> GHOSTY_FACTORY_LAYER_COMPAT=$<BOOL:${GHOSTY_FACTORY_LAYER_COMPAT}>)
  target_link_libraries(ProTests PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_gui_extra juce::juce_dsp signalsmith-stretch gpro_eurorack
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()

# ---------------------------------------------------------------- Ghost Box (the Ghost FX rack as an effect for any track)
# Shares the rack sources with Ghosty Pro (GhostyPro/fx/*.cpp compiled again here with GPFX_BOX=1, which adds the
# WARP (Signalsmith Stretch pitch shift), RETRO, FATTEN, CRACKLE and HAUNTED HALLS units; Ghosty Pro builds them with GPFX_BOX unset, so its state is unchanged).
juce_add_plugin(GhostBox
  COMPANY_NAME "GhostBlock"
  BUNDLE_ID "io.ghostblock.ghostbox"
  PLUGIN_MANUFACTURER_CODE Ghbk
  PLUGIN_CODE GbBx
  FORMATS ${GHOSTY_FORMATS}
  PRODUCT_NAME "Ghost Box"
  VERSION 3.2.0   # 3.2.0: opens at 1200 x 760 (fits FL on a 1440 x 900 MacBook Pro), a fresh start every time, TEMPO 1/2x | 1x | 2x (was the project's 3.1.0)
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT TRUE               # GHOST SCENES: MIDI keys trigger scenes (VST3 event input / CLAP note port; AU stays an effect, aufx)
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE    # Shift+Alt+S = SNAPSHOT
  COPY_PLUGIN_AFTER_BUILD FALSE
  AU_MAIN_TYPE kAudioUnitType_Effect
  VST3_CATEGORIES Fx)
file(GLOB GHOSTBOX_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/GhostBox/*.cpp ${CMAKE_CURRENT_SOURCE_DIR}/GhostyPro/fx/*.cpp)
target_sources(GhostBox PRIVATE ${GHOSTBOX_SOURCES})
target_include_directories(GhostBox PRIVATE Shared GhostBox GhostyPro/fx)
target_compile_definitions(GhostBox PRIVATE GPFX_BOX=1 JUCE_USE_MP3AUDIOFORMAT=1)   # GHOSTPRINT reads MP3 references
target_compile_definitions(GhostBox PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
# Same as Ghosty Pro: no LTO under MSVC (the Windows build machine runs out of memory) and /bigobj for the big rack files.
if(MSVC)
  target_link_libraries(GhostBox PRIVATE juce::juce_audio_utils juce::juce_dsp signalsmith-stretch
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
  target_compile_options(GhostBox PRIVATE /bigobj)
else()
  target_link_libraries(GhostBox PRIVATE juce::juce_audio_utils juce::juce_dsp signalsmith-stretch
    PUBLIC juce::juce_recommended_config_flags ${GHOSTY_LTO} juce::juce_recommended_warning_flags)
endif()
ghosty_license(GhostBox "${GUMROAD_GHOSTBOX}")
ghosty_clap(GhostBox io.ghostblock.ghostbox audio-effect multi-effects stereo)

# Ghost Box tests (off by default; CI doesn't build them): every Box unit and preset (NaN / level / tail), latency and
# bypass nulls, SPECTRA / BASS LAB / PORTAL / FORMANT / PUMP measurements, PARALLEL lanes, MORPH, MOTION, MACROS,
# CHAIN BRAIN, order tips, state round trip, CPU, screenshots and demo renders.
option(GHOSTY_BUILD_BOX_TESTS "Build the Ghost Box test harness" OFF)
if(GHOSTY_BUILD_BOX_TESTS)
  juce_add_console_app(GhostBoxTests PRODUCT_NAME "GhostBoxTests")
  target_sources(GhostBoxTests PRIVATE GhostBox/tests/BoxTests.cpp ${GHOSTBOX_SOURCES})
  target_include_directories(GhostBoxTests PRIVATE Shared GhostBox GhostyPro/fx)
  target_compile_definitions(GhostBoxTests PRIVATE GPFX_BOX=1 JUCE_USE_MP3AUDIOFORMAT=1 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1
    GHOSTY_GUMROAD_PRODUCT="" GHOSTY_GUMROAD_BUNDLE="" GHOSTY_STORE_URL="")   # tests run as a licensed user
  target_link_libraries(GhostBoxTests PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_gui_extra juce::juce_dsp signalsmith-stretch
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()

# ---------------------------------------------------------------- Professor Bernstein's ArpEZator (chords -> arp -> melody idea machine)
# Instrument that also sends MIDI (drive any synth, or drag the lanes out as .mid). The chord generator is a 1:1 port of
# the web app (same prompt + seed = same chords); the melody engines, recipe tables and starters live in ArpEZator/.
juce_add_plugin(ArpEZator
  COMPANY_NAME "GhostBlock"
  BUNDLE_ID "io.ghostblock.arpezator"
  PLUGIN_MANUFACTURER_CODE Ghbk
  PLUGIN_CODE ArpZ
  FORMATS ${GHOSTY_FORMATS}
  PRODUCT_NAME "ArpEZator"
  VERSION 3.2.9   # 3.2.9: no crash when FL Studio closes; 3.2.8: no clicks (PUMP, tied notes), no vinyl crackle on DUSTY KEYS, STUTTER keeps a made melody, locks survive MUTANT <-> CLASSIC, ALIEN in key; 3.2.7: every mood opens with CHORDS + MELODY + BASS playing (ARP muted), on the CHORDS lane; 3.2.6: the faces only (AUTO default = NEON RAIN in MIDNIGHT), the name bigger, layout fixes; 3.2.5: the picture FACES (HOLO / NEON RAIN / MASK / DREAM / AUTO) in a FACE list; 3.2.4: TEMPO ½x | 1x | 2x; 3.2.3: opens at the fitting size + a fresh start every time; 3.2.2: CYBERPOP learned from the first MIDI pack set; 3.2.1: the TRAP flute writer (slow singing phrases, versioned per session) + MUTANT CYBERPOP (learned from MIDI packs); 3.2.0: MUTANT ARP
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT TRUE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  COPY_PLUGIN_AFTER_BUILD FALSE
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  VST3_CATEGORIES Instrument Synth)
file(GLOB ARPEZATOR_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ArpEZator/*.cpp)
file(GLOB_RECURSE ARPEZATOR_DATA CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/ArpEZator/data/*.json ${CMAKE_CURRENT_SOURCE_DIR}/ArpEZator/data/*.mid ${CMAKE_CURRENT_SOURCE_DIR}/ArpEZator/data/faces/*.jpg ${CMAKE_CURRENT_SOURCE_DIR}/ArpEZator/data/faces/fonts/*.ttf)   # faces/*.jpg: the picture FACES (ArpFaces.h)
target_sources(ArpEZator PRIVATE ${ARPEZATOR_SOURCES})
target_include_directories(ArpEZator PRIVATE Shared ArpEZator)
target_compile_definitions(ArpEZator PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_USE_MP3AUDIOFORMAT=1)   # MP3 samples / LISTEN files on every computer (JUCE's own decoder)
if(ARPEZATOR_DATA)
  juce_add_binary_data(ArpEZatorData SOURCES ${ARPEZATOR_DATA})
  target_link_libraries(ArpEZator PRIVATE ArpEZatorData)
  target_compile_definitions(ArpEZator PRIVATE ARPEZATOR_HAS_DATA=1)
endif()
# the chord generator must round exactly like the JS (no fused multiply-add contraction; arm64 clang would otherwise use FMA)
if(MSVC)
  target_compile_options(ArpEZator PRIVATE /bigobj /fp:precise)
  target_link_libraries(ArpEZator PRIVATE juce::juce_audio_utils signalsmith-stretch
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
else()
  target_compile_options(ArpEZator PRIVATE -ffp-contract=off)
  target_link_libraries(ArpEZator PRIVATE juce::juce_audio_utils signalsmith-stretch
    PUBLIC juce::juce_recommended_config_flags ${GHOSTY_LTO} juce::juce_recommended_warning_flags)
endif()
ghosty_license(ArpEZator "${GUMROAD_ARPEZATOR}")
ghosty_clap(ArpEZator io.ghostblock.arpezator instrument synthesizer note-effect stereo)

# ArpEZator tests (off by default; CI doesn't build them): chord parity with the web version, melody stats, LISTEN,
# and a render harness (host-synced fake playhead -> WAV + MIDI timing / note balance checks).
option(GHOSTY_BUILD_TESTS "Build the ArpEZator test harness" OFF)
if(GHOSTY_BUILD_TESTS)
  juce_add_console_app(ArpEZatorTests PRODUCT_NAME "ArpEZatorTests")
  target_sources(ArpEZatorTests PRIVATE ArpEZator/tests/ArpTests.cpp ${ARPEZATOR_SOURCES})
  target_include_directories(ArpEZatorTests PRIVATE Shared ArpEZator)
  target_compile_definitions(ArpEZatorTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_USE_MP3AUDIOFORMAT=1)
  if(ARPEZATOR_DATA)
    target_link_libraries(ArpEZatorTests PRIVATE ArpEZatorData)
    target_compile_definitions(ArpEZatorTests PRIVATE ARPEZATOR_HAS_DATA=1)
  endif()
  if(NOT MSVC)
    target_compile_options(ArpEZatorTests PRIVATE -ffp-contract=off)
  endif()
  target_link_libraries(ArpEZatorTests PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_gui_extra signalsmith-stretch
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()

# ---------------------------------------------------------------- Ghost Slice (drop a sound, press GENERATE: instruments, pads, chops, textures)
include(${CMAKE_CURRENT_SOURCE_DIR}/GhostSlice/GhostSlice.cmake)
# ---------------------------------------------------------------- Ghost Hum (sing or beatbox -> MIDI)
include(${CMAKE_CURRENT_SOURCE_DIR}/GhostHum/GhostHum.cmake)

# Demo export gate test hooks (Shared/ExportGate.h): GHOSTY_TEST_HOOKS=1 on every test harness (a target named *Tests), never
# on a plugin. It lets a test force the licence locked / unlocked and records drags / save dialogs instead of doing them.
function(ghosty_test_hooks)
  get_property(_targets DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY BUILDSYSTEM_TARGETS)
  foreach(t ${_targets})
    if(t MATCHES "Tests$")
      target_compile_definitions(${t} PRIVATE GHOSTY_TEST_HOOKS=1)
    endif()
  endforeach()
endfunction()
cmake_language(DEFER CALL ghosty_test_hooks)
