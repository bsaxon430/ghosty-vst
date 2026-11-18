# ---------------------------------------------------------------- Block Step (vertical drum tracker; the web app's UI in a WebView)
# Off by default: the main GhostBlock installer doesn't include it until the owner says so.
# Built on its own by .github/workflows/blockstep-mac.yml (cmake -DGHOSTY_BLOCKSTEP=ON).
set(GUMROAD_BLOCKSTEP "" CACHE STRING "Gumroad product_id: Block Step")
if("${GUMROAD_BLOCKSTEP}" STREQUAL "" AND DEFINED STORE_BLOCKSTEP AND NOT "${STORE_BLOCKSTEP}" STREQUAL "")
  set(GUMROAD_BLOCKSTEP "${STORE_BLOCKSTEP}")
endif()

juce_add_plugin(BlockStep
  VERSION 1.0.0
  COMPANY_NAME "GhostBlock"
  BUNDLE_ID "io.ghostblock.blockstep"
  PLUGIN_MANUFACTURER_CODE Ghbk
  PLUGIN_CODE Bstp
  FORMATS ${GHOSTY_FORMATS}
  PRODUCT_NAME "Block Step"
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  NEEDS_WEB_BROWSER TRUE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  COPY_PLUGIN_AFTER_BUILD FALSE
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  VST3_CATEGORIES Instrument Drum)

target_sources(BlockStep PRIVATE
  ${CMAKE_CURRENT_LIST_DIR}/BlockStepProcessor.cpp
  ${CMAKE_CURRENT_LIST_DIR}/BlockStepEditor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Shared/GhostSynth.cpp)
# SYNTH page (2026-10-29): Open303 (MIT, third_party/open303-mit) + the Plaits engines (gpro_eurorack, MIT)
file(GLOB GHOST_OPEN303_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/third_party/open303-mit/*.cpp)
target_sources(BlockStep PRIVATE ${GHOST_OPEN303_SOURCES})
set_source_files_properties(${GHOST_OPEN303_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/Shared/GhostSynth.cpp PROPERTIES COMPILE_OPTIONS "-w")
target_include_directories(BlockStep PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Shared ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/third_party/open303-mit)
target_compile_definitions(BlockStep PUBLIC
  JUCE_WEB_BROWSER=1 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_USE_MP3AUDIOFORMAT=1)

# the page + kit manifest + every sound (Block kit + CC0 breaks library), compiled in
file(GLOB BLOCKSTEP_SOUNDS CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/data/sounds/*.mp3)
juce_add_binary_data(BlockStepData
  HEADER_NAME BlockStepBin.h
  NAMESPACE BlockStepBin
  SOURCES ${CMAKE_CURRENT_LIST_DIR}/data/index.html ${CMAKE_CURRENT_LIST_DIR}/data/allman.json ${CMAKE_CURRENT_LIST_DIR}/packaging/CREDITS.txt ${BLOCKSTEP_SOUNDS})

target_link_libraries(BlockStep PRIVATE BlockStepData gpro_eurorack juce::juce_audio_utils juce::juce_gui_extra
  PUBLIC juce::juce_recommended_config_flags ${GHOSTY_LTO} juce::juce_recommended_warning_flags)
ghosty_license(BlockStep "${GUMROAD_BLOCKSTEP}")

# The host test (tests/HostTest.cpp): loads the built VST3 like FL Studio and checks sound, fresh start and window size.
option(BLOCKSTEP_HOST_TEST "Build the Block Step host test" OFF)
if(BLOCKSTEP_HOST_TEST)
  juce_add_console_app(BlockStepHostTest PRODUCT_NAME "BlockStepHostTest")
  target_sources(BlockStepHostTest PRIVATE ${CMAKE_CURRENT_LIST_DIR}/tests/HostTest.cpp)
  target_compile_definitions(BlockStepHostTest PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=0 JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1)
  target_link_libraries(BlockStepHostTest PRIVATE juce::juce_audio_utils ${CMAKE_DL_LIBS}
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
  # the engine tests (tests/EngineTests.cpp): timing, FX, levels, TEMPO ½x / 1x / 2x (sync, MIDI note times, WAV length)
  juce_add_console_app(BlockStepEngineTests PRODUCT_NAME "BlockStepEngineTests")
  target_sources(BlockStepEngineTests PRIVATE ${CMAKE_CURRENT_LIST_DIR}/tests/EngineTests.cpp ${CMAKE_CURRENT_SOURCE_DIR}/Shared/GhostSynth.cpp ${GHOST_OPEN303_SOURCES})
  target_include_directories(BlockStepEngineTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Shared ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_LIST_DIR}/tests ${CMAKE_CURRENT_SOURCE_DIR}/third_party/open303-mit)
  target_compile_definitions(BlockStepEngineTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1 JUCE_USE_MP3AUDIOFORMAT=1 JUCE_MODAL_LOOPS_PERMITTED=1
    ENGINE_HEADER="../BlockStepEngine.h" ENGINE_NS=bstep SYN_SOUND=0 GHOSTY_GUMROAD_PRODUCT="" GHOSTY_GUMROAD_BUNDLE="" GHOSTY_STORE_URL="")
  target_link_libraries(BlockStepEngineTests PRIVATE gpro_eurorack juce::juce_audio_formats juce::juce_gui_basics
    PUBLIC juce::juce_recommended_config_flags)
endif()
