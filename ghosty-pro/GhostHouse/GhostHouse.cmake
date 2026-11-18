# ---------------------------------------------------------------- Ghost House (house drum tracker; the web app's UI in a WebView)
# Off by default: the main GhostBlock installer doesn't include it until the owner says so.
# Built on its own by .github/workflows/ghosthouse-mac.yml (cmake -DGHOSTY_GHOSTHOUSE=ON).
set(GUMROAD_GHOSTHOUSE "" CACHE STRING "Gumroad product_id: Ghost House")
if("${GUMROAD_GHOSTHOUSE}" STREQUAL "" AND DEFINED STORE_GHOSTHOUSE AND NOT "${STORE_GHOSTHOUSE}" STREQUAL "")
  set(GUMROAD_GHOSTHOUSE "${STORE_GHOSTHOUSE}")
endif()

juce_add_plugin(GhostHouse
  VERSION 1.0.0
  COMPANY_NAME "GhostBlock"
  BUNDLE_ID "io.ghostblock.ghosthouse"
  PLUGIN_MANUFACTURER_CODE Ghbk
  PLUGIN_CODE Ghse
  FORMATS ${GHOSTY_FORMATS}
  PRODUCT_NAME "Ghost House"
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  NEEDS_WEB_BROWSER TRUE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  COPY_PLUGIN_AFTER_BUILD FALSE
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  VST3_CATEGORIES Instrument Drum)

target_sources(GhostHouse PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/GhostHouseProcessor.cpp
  ${CMAKE_CURRENT_LIST_DIR}/GhostHouseEditor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Shared/GhostSynth.cpp)
# SYNTH page (2026-10-29): Open303 (MIT, third_party/open303-mit) + the Plaits engines (gpro_eurorack, MIT)
file(GLOB GHOST_OPEN303_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/third_party/open303-mit/*.cpp)
target_sources(GhostHouse PRIVATE ${GHOST_OPEN303_SOURCES})
set_source_files_properties(${GHOST_OPEN303_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/Shared/GhostSynth.cpp PROPERTIES COMPILE_OPTIONS "-w")
target_include_directories(GhostHouse PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Shared ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/third_party/open303-mit)
target_compile_definitions(GhostHouse PUBLIC
  JUCE_WEB_BROWSER=1 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_USE_MP3AUDIOFORMAT=1)

# the page + kit manifest + every sound (GhostBlock kit + the synthesised house pack), compiled in
file(GLOB GHOSTHOUSE_SOUNDS CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/data/sounds/*.mp3)
juce_add_binary_data(GhostHouseData
  HEADER_NAME GhostHouseBin.h
  NAMESPACE GhostHouseBin
  SOURCES ${CMAKE_CURRENT_LIST_DIR}/data/index.html ${CMAKE_CURRENT_LIST_DIR}/data/allman.json ${CMAKE_CURRENT_LIST_DIR}/packaging/CREDITS.txt ${GHOSTHOUSE_SOUNDS})

target_link_libraries(GhostHouse PRIVATE GhostHouseData gpro_eurorack juce::juce_audio_utils juce::juce_gui_extra
  PUBLIC juce::juce_recommended_config_flags ${GHOSTY_LTO} juce::juce_recommended_warning_flags)
ghosty_license(GhostHouse "${GUMROAD_GHOSTHOUSE}")

# The host test (tests/HostTest.cpp): loads the built VST3 like FL Studio and checks sound, fresh start and window size.
option(GHOSTHOUSE_HOST_TEST "Build the Ghost House host test" OFF)
if(GHOSTHOUSE_HOST_TEST)
  juce_add_console_app(GhostHouseHostTest PRODUCT_NAME "GhostHouseHostTest")
  target_sources(GhostHouseHostTest PRIVATE ${CMAKE_CURRENT_LIST_DIR}/tests/HostTest.cpp)
  target_compile_definitions(GhostHouseHostTest PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=0 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
  target_link_libraries(GhostHouseHostTest PRIVATE juce::juce_audio_utils ${CMAKE_DL_LIBS}
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
  # the engine tests (tests/EngineTests.cpp): timing, FX, levels, TEMPO ½x / 1x / 2x (sync, MIDI note times, WAV length)
  juce_add_console_app(GhostHouseEngineTests PRODUCT_NAME "GhostHouseEngineTests")
  target_sources(GhostHouseEngineTests PRIVATE ${CMAKE_CURRENT_LIST_DIR}/tests/EngineTests.cpp ${CMAKE_CURRENT_SOURCE_DIR}/Shared/GhostSynth.cpp ${GHOST_OPEN303_SOURCES})
  target_include_directories(GhostHouseEngineTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Shared ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/tests ${CMAKE_CURRENT_SOURCE_DIR}/third_party/open303-mit)
  target_compile_definitions(GhostHouseEngineTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_USE_MP3AUDIOFORMAT=1 JUCE_MODAL_LOOPS_PERMITTED=1
    ENGINE_HEADER="../GhostHouseEngine.h" ENGINE_NS=ghouse SYN_SOUND=5 GHOSTY_GUMROAD_PRODUCT="" GHOSTY_GUMROAD_BUNDLE="" GHOSTY_STORE_URL="")
  target_link_libraries(GhostHouseEngineTests PRIVATE gpro_eurorack juce::juce_audio_formats juce::juce_gui_basics
    PUBLIC juce::juce_recommended_config_flags)
endif()
