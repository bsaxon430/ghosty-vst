# Ghosty Spectrum: SPECTRAL LAB sources + the ARRANGE and ARCS pages (their own .cmake files only build their test harnesses).
set(GHOSTY_SPECTRUM_PAGE_SOURCES
  SpectrumX/LabEngine.cpp SpectrumX/LabFx.cpp SpectrumX/LabHost.cpp SpectrumX/LabPanel.cpp
  SpectrumX/ArrangeModel.cpp SpectrumX/ArrangeEngine.cpp SpectrumX/ArrangePage.cpp
  SpectrumX/ArcsEngine.cpp SpectrumX/ArcsHost.cpp SpectrumX/ArcsPage.cpp)
target_sources(GhostySpectrum PRIVATE ${GHOSTY_SPECTRUM_PAGE_SOURCES} SpectrumX/LabProcessPage.cpp SpectrumX/SpecGuide.cpp)
# PROCESS page (the shared PROCESS LAB on the Lab's sound): same option as Poltergeist's (default ON)
if(NOT DEFINED GHOSTY_PROCESS_LAB)
  option(GHOSTY_PROCESS_LAB "PROCESS LAB pages (Poltergeist SAMPLE LAB, Spectrum PROCESS)" ON)
endif()
if(GHOSTY_PROCESS_LAB)
  target_sources(GhostySpectrum PRIVATE Shared/ProcessLab.cpp Shared/ProcessLabPanel.cpp)
  target_compile_definitions(GhostySpectrum PRIVATE GHOSTY_PROCESS_LAB=1)
endif()

# VOICE page (pitch + formant voice editing on WORLD, modified BSD). Voice.cmake (included from CMakeLists.txt after this
# file) fetches WORLD from its pinned URL + SHA256 and defines GHOSTY_VOICE_SOURCES / ghosty-world; Spectrum turns it on
# by default and wires the target once that file has run (deferred to the end of the directory).
if(NOT DEFINED GHOSTY_VOICE)
  set(GHOSTY_VOICE ON CACHE BOOL "Ghosty Spectrum VOICE page (fetches WORLD v1.0.1, pinned)")
endif()
function(ghosty_spectrum_wire_voice)
  if(GHOSTY_VOICE AND DEFINED GHOSTY_VOICE_SOURCES AND TARGET ghosty-world)
    foreach(t GhostySpectrum SpectrumLabTests)
      if(TARGET ${t})
        target_sources(${t} PRIVATE ${GHOSTY_VOICE_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/SpectrumX/SpecVoiceTab.cpp)
        target_link_libraries(${t} PRIVATE ghosty-world)
        target_compile_definitions(${t} PRIVATE GHOSTY_VOICE=1 JUCE_USE_MP3AUDIOFORMAT=1)
      endif()
    endforeach()
  endif()
endfunction()
cmake_language(DEFER CALL ghosty_spectrum_wire_voice)

# tests (off by default; CI doesn't build them): resynthesis SNR, paint/shift/image/split/morph checks, state back-compat, UI events, screenshots, demos
option(GHOSTY_BUILD_SPECTRUM_TESTS "Build the Ghosty Spectrum SPECTRAL LAB test harness" OFF)
if(GHOSTY_BUILD_SPECTRUM_TESTS)
  juce_add_console_app(SpectrumLabTests PRODUCT_NAME "SpectrumLabTests")
  target_sources(SpectrumLabTests PRIVATE SpectrumX/tests/LabTests.cpp SpectrumX/SpecProcessor.cpp SpectrumX/SpecEditor.cpp ${GHOSTY_SPECTRUM_PAGE_SOURCES} SpectrumX/LabProcessPage.cpp SpectrumX/SpecGuide.cpp)
  if(GHOSTY_PROCESS_LAB)
    target_sources(SpectrumLabTests PRIVATE Shared/ProcessLab.cpp Shared/ProcessLabPanel.cpp)
    target_compile_definitions(SpectrumLabTests PRIVATE GHOSTY_PROCESS_LAB=1)
  endif()
  target_include_directories(SpectrumLabTests PRIVATE Shared SpectrumX Poltergeist BeatsX)
  target_compile_definitions(SpectrumLabTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1
    GHOSTY_GUMROAD_PRODUCT="" GHOSTY_GUMROAD_BUNDLE="" GHOSTY_STORE_URL="")
  target_link_libraries(SpectrumLabTests PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_gui_extra juce::juce_dsp signalsmith-stretch
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
