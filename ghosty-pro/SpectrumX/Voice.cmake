# Ghosty Spectrum VOICE page: pitch + formant analysis / resynthesis (Shared/VoiceEngine = the engine, also used by GHOST VOICE; VoiceHost, VoicePage).
#
# WORLD v1.0.1 (M. Morise, modified BSD; https://github.com/mmorise/World) does the f0 / spectral envelope /
# aperiodicity analysis and the synthesis. Fetched as a pinned zip like JUCE; offline builds point
# FETCHCONTENT_SOURCE_DIR_WORLD at a vendored copy (/root/vendor/World-1.0.1 on the build server, LICENSE.txt inside).
# WORLD's own CMakeLists builds examples / tools we don't want, so SOURCE_SUBDIR points at a folder without one and the
# library target is declared here from its src/ files.
# Binary distribution: WORLD's licence (clause 2) must ship with the plugin -> put LICENSE.txt (or the text from
# voice::thirdPartyNotice()) in the installer / about box / manual.
#
# Nothing here is fetched or built unless GHOSTY_VOICE (the owner turns it on when wiring the tab) or the tests are ON,
# so CI / other plugin builds are unchanged.
option(GHOSTY_VOICE "Fetch WORLD and define GHOSTY_VOICE_SOURCES for the Ghosty Spectrum VOICE page (SpectralLab.cmake turns it ON for Spectrum first)" ON)
option(GHOSTY_BUILD_VOICE_TESTS "Build the Ghosty Spectrum VOICE test harness" OFF)
if(NOT GHOSTY_VOICE AND NOT GHOSTY_BUILD_VOICE_TESTS)
  return()
endif()
include(FetchContent)
FetchContent_Declare(world
  URL https://github.com/mmorise/World/archive/refs/tags/v1.0.1.zip
  URL_HASH SHA256=408d2429cdc33dac8dae253de4932b1a57d8a697b49d3ac4cd73dc2000fb24d1
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR ghosty-no-cmake-here)
FetchContent_MakeAvailable(world)
if(NOT TARGET ghosty-world)
  add_library(ghosty-world STATIC
    ${world_SOURCE_DIR}/src/cheaptrick.cpp ${world_SOURCE_DIR}/src/codec.cpp ${world_SOURCE_DIR}/src/common.cpp ${world_SOURCE_DIR}/src/d4c.cpp
    ${world_SOURCE_DIR}/src/dio.cpp ${world_SOURCE_DIR}/src/fft.cpp ${world_SOURCE_DIR}/src/harvest.cpp ${world_SOURCE_DIR}/src/matlabfunctions.cpp
    ${world_SOURCE_DIR}/src/stonemask.cpp ${world_SOURCE_DIR}/src/synthesis.cpp ${world_SOURCE_DIR}/src/synthesisrealtime.cpp)
  target_include_directories(ghosty-world PUBLIC ${world_SOURCE_DIR}/src)
  set_target_properties(ghosty-world PROPERTIES POSITION_INDEPENDENT_CODE ON)
  if(NOT MSVC)
    target_compile_options(ghosty-world PRIVATE -w)   # third-party code: its warnings are not ours
  endif()
endif()

# The page's sources. NOT added to the GhostySpectrum target yet: the Spectrum owner wires them in (see the
# INTEGRATION NOTE at the top of SpectrumX/VoicePage.h), e.g.
#   target_sources(GhostySpectrum PRIVATE ${GHOSTY_VOICE_SOURCES})
#   target_link_libraries(GhostySpectrum PRIVATE ghosty-world)
set(GHOSTY_VOICE_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/Shared/VoiceEngine.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/SpectrumX/VoiceHost.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/SpectrumX/VoicePage.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Shared/VoiceFusion.cpp          # GHOST VOICE: the fusion engine (Shared: a standalone Ghost Voice plugin can reuse it)
  ${CMAKE_CURRENT_SOURCE_DIR}/Shared/VoiceFusionClear.cpp     # GHOST VOICE NATURAL engine: identity maths, STFT shaping, PSOLA, loudness
  ${CMAKE_CURRENT_SOURCE_DIR}/Shared/VoiceFusionNatural.cpp   # GHOST VOICE NATURAL engine: the render
  ${CMAKE_CURRENT_SOURCE_DIR}/Shared/VoiceFusionHost.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/SpectrumX/GhostVoicePage.cpp)

# Tests (off by default; CI doesn't build them): synthetic voices (glottal pulses through formant filters at known
# F1 / F2), pitch shift with / without formant keep, formant borrow, drawn contours, vowel morph endpoints,
# silence / noise / unvoiced inputs, 44.1 / 48 / 96 kHz, CPU, state + undo round trips, screenshots and demo renders.
if(GHOSTY_BUILD_VOICE_TESTS)
  juce_add_console_app(VoiceTests PRODUCT_NAME "VoiceTests")
  target_sources(VoiceTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/SpectrumX/tests/VoiceTests.cpp ${GHOSTY_VOICE_SOURCES})
  target_include_directories(VoiceTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Shared ${CMAKE_CURRENT_SOURCE_DIR}/SpectrumX)
  target_compile_definitions(VoiceTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_USE_MP3AUDIOFORMAT=1)
  target_link_libraries(VoiceTests PRIVATE ghosty-world juce::juce_audio_utils juce::juce_audio_formats juce::juce_gui_extra juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
  # GHOST VOICE tests: synthesised voices, the blend measures (F0 RMSE, contour correlation, V/UV, drift, formant positions, consistency), host, page, screenshots, demos
  juce_add_console_app(GhostVoiceTests PRODUCT_NAME "GhostVoiceTests")
  target_sources(GhostVoiceTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/SpectrumX/tests/GhostVoiceTests.cpp ${GHOSTY_VOICE_SOURCES})
  target_include_directories(GhostVoiceTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Shared ${CMAKE_CURRENT_SOURCE_DIR}/SpectrumX)
  target_compile_definitions(GhostVoiceTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_USE_MP3AUDIOFORMAT=1
    GHOSTY_GUMROAD_PRODUCT="" GHOSTY_GUMROAD_BUNDLE="" GHOSTY_STORE_URL="")
  target_link_libraries(GhostVoiceTests PRIVATE ghosty-world juce::juce_audio_utils juce::juce_audio_formats juce::juce_gui_extra juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
  # GHOST VOICE stage renders (CLASSIC vs CLEAR, stage by stage, timed) for docs/research/tools/ghostvoice_measure.py
  juce_add_console_app(GhostVoiceStages PRODUCT_NAME "GhostVoiceStages")
  target_sources(GhostVoiceStages PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/SpectrumX/tests/GhostVoiceStages.cpp ${GHOSTY_VOICE_SOURCES})
  target_include_directories(GhostVoiceStages PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Shared ${CMAKE_CURRENT_SOURCE_DIR}/SpectrumX)
  target_compile_definitions(GhostVoiceStages PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1 JUCE_USE_MP3AUDIOFORMAT=1
    GHOSTY_GUMROAD_PRODUCT="" GHOSTY_GUMROAD_BUNDLE="" GHOSTY_STORE_URL="")
  target_link_libraries(GhostVoiceStages PRIVATE ghosty-world juce::juce_audio_utils juce::juce_audio_formats juce::juce_gui_extra juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
