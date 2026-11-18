# Ghosty Spectrum ARCS page (draw lines = voices: per-line drawn waveform or noise, unison / sub / drive / filter, envelope /
# pan / cutoff lanes, own rate + repeat; page length + tempo sync, loop / reverse / quantised jump / speed lane, oversampled
# line-to-line FM, quantise to key, stamp tools, clipboard, 8-page bank, MIDI-in, factory pages + patches, MIDI (+ MPE) and
# streamed audio drag-out). Headers: ArcsModel.h ArcsFactory.h ArcsEngine.h ArcsHost.h ArcsPage.h.
#
# Sources (NOT in the plugin target; the Spectrum owner wires them in when the tab is added, see ArcsPage.h):
#   target_sources(GhostySpectrum PRIVATE ${GHOSTY_ARCS_SOURCES})
set(GHOSTY_ARCS_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/SpectrumX/ArcsEngine.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/SpectrumX/ArcsHost.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/SpectrumX/ArcsPage.cpp)

# tests (off by default; CI doesn't build them): 440 Hz line at 3 rates, glide tracking, reverse, FM sideband theory + FM / drive
# aliasing, quantise, zipper, MIDI export (dives, overflow, MPE), state round trip + hostile files, transport (tempo changes,
# count-in, seams, quantised jumps), CPU, one regression test per review item, screenshots, demos.  Run: ArcsTests [outDir] [--no-media]
option(GHOSTY_BUILD_ARCS_TESTS "Build the Ghosty Spectrum ARCS page test harness" OFF)
if(GHOSTY_BUILD_ARCS_TESTS)
  juce_add_console_app(ArcsTests PRODUCT_NAME "ArcsTests")
  target_sources(ArcsTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/SpectrumX/tests/ArcsTests.cpp ${GHOSTY_ARCS_SOURCES})
  target_include_directories(ArcsTests PRIVATE Shared SpectrumX)
  target_compile_definitions(ArcsTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
  target_link_libraries(ArcsTests PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_gui_extra juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
