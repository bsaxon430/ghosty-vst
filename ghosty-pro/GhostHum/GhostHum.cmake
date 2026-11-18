# ---------------------------------------------------------------- Ghost Hum ("Sing it. Beatbox it. Make music.")
# Included from CMakeLists.txt. Mac: VST3 + AU + Standalone (no Windows until the owner says so; the CI workflow is Mac only).
# It is an EFFECT with MIDI out (audio in: the mic / the track it sits on; audio out: its own piano / drums; MIDI out: the notes it
# heard, as it plays them), so a DAW gives it the microphone track's audio. AU: Music Effect (aumf). Header-only engine (GhostHum/*.h
# + Shared/HearCore.h), one .cpp. No third-party code beyond JUCE.
set(GHOSTHUM_VERSION "1.0.4")
set(GHOSTHUM_BUILD "8")
juce_add_plugin(GhostHum
  VERSION 1.0.4
  COMPANY_NAME "GhostBlock"
  BUNDLE_ID "io.ghostblock.ghosthum"
  PLUGIN_MANUFACTURER_CODE Ghbk
  PLUGIN_CODE Ghhm
  FORMATS ${GHOSTY_FORMATS}
  PRODUCT_NAME "Ghost Hum"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT TRUE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  COPY_PLUGIN_AFTER_BUILD FALSE
  MICROPHONE_PERMISSION_ENABLED TRUE
  MICROPHONE_PERMISSION_TEXT "Ghost Hum listens while you sing or beatbox, then turns it into notes."
  AU_MAIN_TYPE kAudioUnitType_MusicEffect
  VST3_CATEGORIES Fx Tools Instrument)
target_sources(GhostHum PRIVATE GhostHum/HumProcessor.cpp)
target_include_directories(GhostHum PRIVATE Shared GhostHum)
target_compile_definitions(GhostHum PUBLIC
  JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0
  GHOSTHUM_VERSION="${GHOSTHUM_VERSION}" GHOSTHUM_BUILD="${GHOSTHUM_BUILD}")
target_link_libraries(GhostHum PRIVATE juce::juce_audio_utils juce::juce_dsp
  PUBLIC juce::juce_recommended_config_flags ${GHOSTY_LTO} juce::juce_recommended_warning_flags)
ghosty_license(GhostHum "${GUMROAD_GHOSTHUM}")
ghosty_clap(GhostHum io.ghostblock.ghosthum audio-effect note-detector stereo)

# Ghost Hum tests (off by default; the ghosthum-mac workflow builds and runs them): melody accuracy on synthesised singers, beatbox detection / classification /
# TEACH on synthesised beatboxers, tempo, quantize, CLEAN + UNDO, MIDI / WAV files, plugin state, the plugin path, the window.
option(GHOSTY_BUILD_HUM_TESTS "Build the Ghost Hum test harness" OFF)
if(GHOSTY_BUILD_HUM_TESTS)
  juce_add_console_app(HumTests PRODUCT_NAME "HumTests")
  target_sources(HumTests PRIVATE GhostHum/tests/HumTests.cpp GhostHum/HumProcessor.cpp)
  target_include_directories(HumTests PRIVATE Shared GhostHum)
  target_compile_definitions(HumTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1
    GHOSTHUM_VERSION="${GHOSTHUM_VERSION}" GHOSTHUM_BUILD="${GHOSTHUM_BUILD}" GHOSTY_TEST_HOOKS=1 GHOSTY_GUMROAD_PRODUCT="" GHOSTY_GUMROAD_BUNDLE="" GHOSTY_STORE_URL="")
  target_link_libraries(HumTests PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_gui_extra juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
