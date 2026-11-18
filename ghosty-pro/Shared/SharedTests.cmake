# Tests for the small shared headers (off by default; CI doesn't build them):
#   BypassTests        Shared/Bypass.h         fade, engage edge, dry/wet crossfade, button <-> parameter
#   SyncTransportTests Shared/SyncTransport.h  DAW mode, own clock, edges, mode switches, no-DAW fallback
#   WaveRegionTests    Shared/WaveRegion.h     handles, right-click START, right-drag LOOP, click never edits, export gate, drag-out
#   ThemeTests         Shared/GhostThemes.h    the three house themes: text contrast >= 4.5:1, opaque surfaces, toned-down LIGHT, cycle, save / migrate
#   SnapshotTests      Shared/SnapshotBank.h   SNAPSHOT bank: take / cycle / restore exactly, corrupt files skipped, restart, delete + undo, prune, export / import, the bar
#   SpiritBoardTests   Shared/SpiritBoard.h    multi-ghost morph board: weights, orbit, model save / load, audio handoff, view
# configure with -DGHOSTY_BUILD_SHARED_TESTS=ON, run the binaries under xvfb-run
option(GHOSTY_BUILD_SHARED_TESTS "Build the Bypass / SyncTransport test harnesses" OFF)
if(GHOSTY_BUILD_SHARED_TESTS)
  foreach(t Bypass SyncTransport WaveRegion Theme SpiritBoard Snapshot)
    juce_add_console_app(${t}Tests PRODUCT_NAME "${t}Tests")
    target_sources(${t}Tests PRIVATE Shared/tests/${t}Tests.cpp)
    target_include_directories(${t}Tests PRIVATE Shared)
    target_compile_definitions(${t}Tests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(${t}Tests PRIVATE juce::juce_audio_processors juce::juce_gui_basics juce::juce_audio_basics juce::juce_events juce::juce_core
      PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
  endforeach()
endif()
# ExportGateLint: the demo export gate's source check (Shared/tests/ExportGateLint.cpp; docs/DEMO-EXPORT-GATE.md)
if(GHOSTY_BUILD_SHARED_TESTS)
  juce_add_console_app(ExportGateLint PRODUCT_NAME "ExportGateLint")
  target_sources(ExportGateLint PRIVATE Shared/tests/ExportGateLint.cpp)
  target_compile_definitions(ExportGateLint PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 GHOSTY_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(ExportGateLint PRIVATE juce::juce_core PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
