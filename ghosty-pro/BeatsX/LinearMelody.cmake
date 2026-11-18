# Ghosty Beats LINEAR DRUMMING (BeatsX/Linear.*) + MELODIC TRACKER (BeatsX/MelodyTracker.*, MelodyTrackerView.h).
#
# Sources: CMakeLists.txt adds GHOSTY_LINMEL_SOURCES to the GhostyBeats plugin and to BeatsTests (wired in BeatEngineHooks.h,
# BeatsXProcessor, BeatsXEditor, RhythmLab / LinearPanel.h and MelodyTrackerView.h).
set(GHOSTY_LINMEL_SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/BeatsX/Linear.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/BeatsX/MelodyTracker.cpp)

# tests (off by default; CI doesn't build them): zero overlaps over random patterns + SONG, seeded variations, anchors / LOCK
# lanes, MOVE / DROP counts, retrig / roll / flam hits, bit-exact toggle back, export == playback; tracker pitch (root, octave),
# portamento, arpeggio, vibrato, retrig, cut / delay / offset, aliasing, MIDI export, NaN fuzz, CPU, screenshots, demos
option(GHOSTY_BUILD_LINMEL_TESTS "Build the Ghosty Beats LINEAR + MELODIC TRACKER test harness" OFF)
if(GHOSTY_BUILD_LINMEL_TESTS)
  juce_add_console_app(LinearMelodyTests PRODUCT_NAME "LinearMelodyTests")
  target_sources(LinearMelodyTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/BeatsX/tests/LinearMelodyTests.cpp ${GHOSTY_LINMEL_SOURCES})
  target_include_directories(LinearMelodyTests PRIVATE Shared BeatsX)
  target_compile_definitions(LinearMelodyTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
  target_link_libraries(LinearMelodyTests PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_gui_extra juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
