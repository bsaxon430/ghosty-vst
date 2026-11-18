# PROCESS LAB (shared engine + panel): Shared/ProcessLab.cpp + Shared/ProcessLabPanel.cpp. Plugins add those two sources to their
# own target (see the integration note in Shared/ProcessLab.h) and link signalsmith-stretch + juce_dsp. This file only adds the tests.

# tests (off by default; CI doesn't build them): numbers for every op, threading, undo, audition; screenshots; demos
option(GHOSTY_BUILD_PROCESSLAB_TESTS "Build the PROCESS LAB test harness" OFF)
if(GHOSTY_BUILD_PROCESSLAB_TESTS)
  juce_add_console_app(ProcessLabTests PRODUCT_NAME "ProcessLabTests")
  target_sources(ProcessLabTests PRIVATE Shared/tests/ProcessLabTests.cpp Shared/ProcessLab.cpp Shared/ProcessLabPanel.cpp)
  target_include_directories(ProcessLabTests PRIVATE Shared)
  target_compile_definitions(ProcessLabTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
  target_link_libraries(ProcessLabTests PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_gui_extra juce::juce_dsp signalsmith-stretch
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()

# determinism (#10): no FMA contraction / fast-math reassociation in the engine, so x86_64 and arm64 slices agree as far as libm/FFT allow
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/Shared/ProcessLab.cpp PROPERTIES COMPILE_OPTIONS
  "$<IF:$<CXX_COMPILER_ID:MSVC>,/fp:precise,-ffp-contract=off>")
