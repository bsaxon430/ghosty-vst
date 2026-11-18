# Ghosty Spectrum ARRANGE page ("paint the sounds, then sequence them into a piece"): multitrack timeline of clips made
# anywhere in Spectrum, per-clip pitch / stretch / reverse / fades / gain / pan, spectral morph between adjacent clips,
# tempo grid, loop, background render, drag out audio + MIDI, Ghosty Beats shared folder, save / load, undo.
#
# Sources (NOT yet in the plugin target; the Spectrum owner wires them in when the tab is added, see ArrangePage.h):
#   target_sources(GhostySpectrum PRIVATE SpectrumX/ArrangeModel.cpp SpectrumX/ArrangeEngine.cpp SpectrumX/ArrangePage.cpp)
set(GHOSTY_ARRANGE_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/SpectrumX/ArrangeModel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/SpectrumX/ArrangeEngine.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/SpectrumX/ArrangePage.cpp)

# tests (off by default; CI doesn't build them): sample-exact starts, reverse, pitch keep-length, fade shapes, stems null,
# morph endpoints, loop / jump transport, offline == realtime, state round trip, 64-clip CPU, NaN, state size, screenshots, demos
option(GHOSTY_BUILD_ARRANGE_TESTS "Build the Ghosty Spectrum ARRANGE page test harness" OFF)
if(GHOSTY_BUILD_ARRANGE_TESTS)
  juce_add_console_app(ArrangeTests PRODUCT_NAME "ArrangeTests")
  target_sources(ArrangeTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/SpectrumX/tests/ArrangeTests.cpp ${GHOSTY_ARRANGE_SOURCES})
  target_include_directories(ArrangeTests PRIVATE Shared SpectrumX)
  target_compile_definitions(ArrangeTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
  target_link_libraries(ArrangeTests PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_gui_extra juce::juce_dsp signalsmith-stretch
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
