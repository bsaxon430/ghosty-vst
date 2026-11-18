# GHOST LAB - experimental sound organism. JUCE plug-in (VST3 + AU instrument "Ghost Lab", VST3 + AU effect "Ghost Lab FX")
# and a standalone Mac app. Its sound engine is CDP8 (LGPL-2.1+), run as separate, unmodified programs that ship inside each
# bundle (Contents/Helpers/cdp; built by cdp/build-cdp.sh). This folder's own code (UI + host) never links CDP.
cmake_minimum_required(VERSION 3.24)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
project(GhostLab VERSION 0.1.0 LANGUAGES C CXX)
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/BUILD GHOSTLAB_BUILD LIMIT_COUNT 1)
set(GHOSTLAB_VERSION 0.1.${GHOSTLAB_BUILD})

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE 8.0.4 (the same pinned zip as the other GhostBlock plug-ins). Local / offline: -DGHOSTLAB_JUCE_DIR=/path/to/JUCE
set(GHOSTLAB_JUCE_DIR "" CACHE PATH "Use this JUCE checkout instead of downloading it")
if(GHOSTLAB_JUCE_DIR)
  add_subdirectory(${GHOSTLAB_JUCE_DIR} juce)
else()
  include(FetchContent)
  FetchContent_Declare(JUCE URL https://github.com/juce-framework/JUCE/archive/refs/tags/8.0.4.zip DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
  FetchContent_MakeAvailable(JUCE)
endif()

option(GHOSTLAB_FAST_DEV "Dev build: no LTO" OFF)
if(GHOSTLAB_FAST_DEV)
  set(GHOSTLAB_LTO "")
else()
  set(GHOSTLAB_LTO juce::juce_recommended_lto_flags)
endif()

# Poppins (SIL Open Font License 1.1; the licence ships beside it: fonts/Poppins-OFL.txt), embedded unmodified
juce_add_binary_data(GhostLabFonts HEADER_NAME GhostLabFonts.h NAMESPACE GhostLabFonts SOURCES
  ${CMAKE_CURRENT_SOURCE_DIR}/fonts/Poppins-Medium.ttf ${CMAKE_CURRENT_SOURCE_DIR}/fonts/Poppins-SemiBold.ttf ${CMAKE_CURRENT_SOURCE_DIR}/fonts/Poppins-Bold.ttf
  ${CMAKE_CURRENT_SOURCE_DIR}/art/lab_banner.png
  ${CMAKE_CURRENT_SOURCE_DIR}/NOTICE.md)
set_target_properties(GhostLabFonts PROPERTIES POSITION_INDEPENDENT_CODE ON)

set(TP ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

# ---- third-party code, built as small static libraries with their own warnings left alone
# PFFFT (Julien Pommier, BSD-like, with FFTPACK for double precision): PaulXStretch's FFT
add_library(gm_pffft STATIC ${TP}/pffft/pffft.c ${TP}/pffft/fftpack.c)
target_include_directories(gm_pffft PUBLIC ${TP}/pffft)
set_target_properties(gm_pffft PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(NOT MSVC)
  target_compile_options(gm_pffft PRIVATE -w)
endif()

# Box2D v3.1.1 (erincatto/box2d, MIT; vendored unmodified): BOUNCE's physics. Its own src/ list, as a static library.
file(GLOB GM_BOX2D_SOURCES ${TP}/box2d-3.1.1/src/*.c)
add_library(gm_box2d STATIC ${GM_BOX2D_SOURCES})
target_include_directories(gm_box2d PUBLIC ${TP}/box2d-3.1.1/include PRIVATE ${TP}/box2d-3.1.1/src)
set_target_properties(gm_box2d PROPERTIES C_STANDARD 17 C_STANDARD_REQUIRED YES C_EXTENSIONS YES POSITION_INDEPENDENT_CODE ON
  C_VISIBILITY_PRESET hidden)
if(NOT MSVC)
  target_compile_options(gm_box2d PRIVATE -w -ffp-contract=off)
endif()

# Pertylizer (pertyjons/pertylizer @ 3726360, MIT): its own Rust crates, built by cargo into a static library with our small
# C interface (third_party/pertylizer-ffi). On a Mac both architectures are built and joined with lipo.
# -DGHOSTLAB_PERTYLIZER_LIB=/path/libghostmatter_pertylizer.a uses one built already.
set(GHOSTLAB_PERTYLIZER_LIB "" CACHE FILEPATH "A prebuilt libghostmatter_pertylizer.a")
set(PFFI ${CMAKE_CURRENT_SOURCE_DIR}/third_party/pertylizer-ffi)
if(GHOSTLAB_PERTYLIZER_LIB)
  set(GM_PERTY_LIB ${GHOSTLAB_PERTYLIZER_LIB})
else()
  find_program(CARGO cargo HINTS $ENV{CARGO_HOME}/bin $ENV{HOME}/.cargo/bin REQUIRED)
  set(PT ${CMAKE_BINARY_DIR}/pertylizer-target)
  set(GM_PERTY_LIB ${CMAKE_BINARY_DIR}/libghostmatter_pertylizer.a)
  if(APPLE)
    # one cargo build per architecture in CMAKE_OSX_ARCHITECTURES (Brandon's Intel Mac: x86_64 only), joined with lipo
    set(GM_PERTY_CMDS)
    set(GM_PERTY_PARTS)
    foreach(arch ${CMAKE_OSX_ARCHITECTURES})
      if(arch STREQUAL "arm64")
        set(triple aarch64-apple-darwin)
      else()
        set(triple x86_64-apple-darwin)
      endif()
      list(APPEND GM_PERTY_CMDS COMMAND ${CARGO} build --release --locked --manifest-path ${PFFI}/Cargo.toml --target ${triple} --target-dir ${PT})
      list(APPEND GM_PERTY_PARTS ${PT}/${triple}/release/libghostmatter_pertylizer.a)
    endforeach()
    add_custom_command(OUTPUT ${GM_PERTY_LIB}
      ${GM_PERTY_CMDS}
      COMMAND lipo -create ${GM_PERTY_PARTS} -output ${GM_PERTY_LIB}
      DEPENDS ${PFFI}/src/lib.rs ${PFFI}/Cargo.toml ${PFFI}/Cargo.lock VERBATIM)
  else()
    add_custom_command(OUTPUT ${GM_PERTY_LIB}
      COMMAND ${CARGO} build --release --locked --manifest-path ${PFFI}/Cargo.toml --target-dir ${PT}
      COMMAND ${CMAKE_COMMAND} -E copy ${PT}/release/libghostmatter_pertylizer.a ${GM_PERTY_LIB}
      DEPENDS ${PFFI}/src/lib.rs ${PFFI}/Cargo.toml ${PFFI}/Cargo.lock VERBATIM)
  endif()
endif()
add_custom_target(gm_pertylizer_build DEPENDS ${GM_PERTY_LIB})
add_library(gm_pertylizer STATIC IMPORTED GLOBAL)
set_target_properties(gm_pertylizer PROPERTIES IMPORTED_LOCATION ${GM_PERTY_LIB})
add_dependencies(gm_pertylizer gm_pertylizer_build)
if(APPLE)
  set(GM_PERTY_SYS "-framework CoreFoundation" "-framework Security")
else()
  set(GM_PERTY_SYS pthread dl m)
endif()

# ---- NEURAL: Magenta's DDSP-VST (magenta/ddsp-vst @ f2996e9, Apache-2.0; third_party/ddsp-vst, unmodified) on TensorFlow
# Lite (tensorflow/tensorflow @ d8ce9f9, the commit DDSP-VST pins, Apache-2.0), built by tensorflow/lite's own CMake.
# -DGHOSTLAB_DDSP=OFF leaves it out (MORPH then says so). -DGHOSTLAB_TENSORFLOW_DIR=/path uses a checkout already there.
option(GHOSTLAB_DDSP "Build Magenta's DDSP (MORPH > NEURAL) in" ON)
set(GHOSTLAB_TENSORFLOW_DIR "" CACHE PATH "A tensorflow source tree at d8ce9f9 (else downloaded)")
set(DV ${TP}/ddsp-vst)
if(GHOSTLAB_DDSP)
  include(FetchContent)
  if(GHOSTLAB_TENSORFLOW_DIR)
    set(GM_TF_DIR ${GHOSTLAB_TENSORFLOW_DIR})
  else()
    # SOURCE_SUBDIR points nowhere: only the source is fetched, its lite/ folder is added below
    FetchContent_Declare(gm_tensorflow URL https://github.com/tensorflow/tensorflow/archive/d8ce9f9c301d021a69953134185ab728c1c248d3.tar.gz
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR gm-none)
    FetchContent_MakeAvailable(gm_tensorflow)
    set(GM_TF_DIR ${gm_tensorflow_SOURCE_DIR})
  endif()
  # its 11 instrument models: the zip DDSP-VST's scripts/download-models.sh fetches (Apache-2.0, its LICENSE inside), pinned
  set(GM_DDSP_MODELS_ZIP ${CMAKE_BINARY_DIR}/DDSP-VST-Models.zip)
  set(GM_DDSP_MODELS_SHA bbb0f4a61be86d8b52460b21210016945899d78f031b99a844a3771e9a2a5308)
  if(NOT EXISTS ${CMAKE_BINARY_DIR}/ddsp-models/DDSP-VST-Models/Violin.tflite)
    file(DOWNLOAD https://storage.googleapis.com/ddsp-vst/releases/DDSP-VST-Models.zip ${GM_DDSP_MODELS_ZIP}
      EXPECTED_HASH SHA256=${GM_DDSP_MODELS_SHA} SHOW_PROGRESS)
    file(ARCHIVE_EXTRACT INPUT ${GM_DDSP_MODELS_ZIP} DESTINATION ${CMAKE_BINARY_DIR}/ddsp-models PATTERNS "DDSP-VST-Models/*")
  endif()
  set(GM_DDSP_MODELS ${CMAKE_BINARY_DIR}/ddsp-models/DDSP-VST-Models)
  # TensorFlow Lite, as DDSP-VST builds it, without XNNPACK (Ghost Lab runs the model offline; it builds the same way for
  # every Mac). Its older sources (and those of the libraries it fetches) lean on <cstdint> arriving through other headers,
  # which newer compilers no longer do, so it is force-included (a compiler flag; the files are not touched), warnings off.
  set(TFLITE_ENABLE_XNNPACK OFF CACHE BOOL "" FORCE)
  set(TFLITE_ENABLE_GPU OFF CACHE BOOL "" FORCE)
  set(TFLITE_ENABLE_NNAPI OFF CACHE BOOL "" FORCE)
  set(TFLITE_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
  set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
  set(GM_SAVED_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -include cstdint -w")
  set(GM_SAVED_C_FLAGS "${CMAKE_C_FLAGS}")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -w")
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
  add_subdirectory(${GM_TF_DIR}/tensorflow/lite ${CMAKE_BINARY_DIR}/tflite EXCLUDE_FROM_ALL)
  set(CMAKE_CXX_FLAGS "${GM_SAVED_CXX_FLAGS}")
  set(CMAKE_C_FLAGS "${GM_SAVED_C_FLAGS}")
  # its BinaryData, as its CMakeLists makes it (cmake/FileList.cmake DDSP_ASSETS)
  juce_add_binary_data(gm_ddsp_assets HEADER_NAME BinaryData.h NAMESPACE BinaryData SOURCES
    ${DV}/assets/icons/folder_icon.svg ${DV}/assets/icons/info_icon.svg ${DV}/assets/icons/refresh_icon.svg ${DV}/assets/icons/logo_thin.svg
    ${DV}/models/pitch-detection/extract_features_micro.tflite
    ${GM_DDSP_MODELS}/Bassoon.tflite ${GM_DDSP_MODELS}/Clarinet.tflite ${GM_DDSP_MODELS}/Flute.tflite ${GM_DDSP_MODELS}/Melodica.tflite
    ${GM_DDSP_MODELS}/Saxophone.tflite ${GM_DDSP_MODELS}/Sitar.tflite ${GM_DDSP_MODELS}/Trombone.tflite ${GM_DDSP_MODELS}/Trumpet.tflite
    ${GM_DDSP_MODELS}/Tuba.tflite ${GM_DDSP_MODELS}/Violin.tflite ${GM_DDSP_MODELS}/Vowels.tflite)
  set_target_properties(gm_ddsp_assets PROPERTIES POSITION_INDEPENDENT_CODE ON)
  # its sources (cmake/FileList.cmake DDSP_SOURCES), unmodified
  set(GM_DDSP_SOURCES
    ${DV}/src/PluginProcessor.cpp ${DV}/src/PluginEditor.cpp
    ${DV}/src/audio/MidiInputProcessor.cpp ${DV}/src/audio/HarmonicSynthesizer.cpp ${DV}/src/audio/NoiseSynthesizer.cpp
    ${DV}/src/audio/tflite/ModelLibrary.cpp ${DV}/src/audio/tflite/FeatureExtractionModel.cpp ${DV}/src/audio/tflite/PredictControlsModel.cpp
    ${DV}/src/audio/tflite/InferencePipeline.cpp
    ${DV}/src/ui/ParamInfo.cpp ${DV}/src/ui/RadioButtonGroupGomponent.cpp ${DV}/src/ui/SliderWithDynamicLabel.cpp
    ${DV}/src/ui/TopPanelComponent.cpp ${DV}/src/ui/ModelRangeVisualizerComponent.cpp ${DV}/src/ui/BottomPanelComponent.cpp
    ${DV}/src/ui/ContentComponent.cpp)
  # they include "JuceHeader.h": ours (shim/) stands in for the one JUCE would generate for its effect target. Its plug-in
  # entry point is renamed so it does not clash with Ghost Lab's own.
  set_source_files_properties(${GM_DDSP_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/Source/Matter/Engine/Ddsp.cpp PROPERTIES
    INCLUDE_DIRECTORIES "${DV}/shim;${DV}/src"
    COMPILE_DEFINITIONS "createPluginFilter=ddspvst_createPluginFilter;DONT_SET_USING_JUCE_NAMESPACE=1")
  set_source_files_properties(${GM_DDSP_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/Source/Matter/Engine/Ddsp.cpp PROPERTIES COMPILE_OPTIONS "-w;-include;cstdint")
  set(GM_DDSP_LIBS tensorflow-lite gm_ddsp_assets)
else()
  set(GM_DDSP_SOURCES)
  set(GM_DDSP_LIBS)
endif()


# The rooms' engines (moved in from Ghost Matter in build 12; AGPL-3.0, see LICENSE and NOTICE.md): our glue to each
# engine (Source/Matter) and the engines' own files, unmodified.
set(ME Source/Matter/Engine)
set(GHOSTLAB_ROOM_SOURCES
  ${ME}/Settings.cpp ${ME}/Dsp.cpp ${ME}/MakeStage.cpp ${ME}/StretchStage.cpp ${ME}/Space.cpp ${ME}/Pattern.cpp ${ME}/Bounce.cpp
  ${ME}/FluesLv2.cpp ${ME}/Ddsp.cpp ${ME}/Words.cpp Source/Matter/Hood/HoodDefs.cpp Source/Matter/Hood/Hood.cpp
  ${TP}/flues/lv2/pm_synth_plugin.cpp ${TP}/flues/lv2/chatterbox_plugin.cpp ${TP}/flues/lv2/disyn_plugin.cpp
  ${TP}/paulxstretch/PS_Source/Stretch.cpp ${TP}/paulxstretch/PS_Source/ProcessedStretch.cpp ${TP}/paulxstretch/PS_Source/globals.cpp
  ${TP}/paulxstretch/PS_Source/StretchSource.cpp ${TP}/paulxstretch/PS_Source/BinauralBeats.cpp ${TP}/paulxstretch/PS_Source/FreeEdit.cpp
  ${TP}/paulxstretch/WDL/resample.cpp ${TP}/paulxstretch/envelope_component.cpp
  ${GM_DDSP_SOURCES})
# Flues' own LV2 plug-in files, unmodified: each one's lv2_descriptor gets its own name so the three link together
set_source_files_properties(${TP}/flues/lv2/pm_synth_plugin.cpp PROPERTIES COMPILE_DEFINITIONS lv2_descriptor=flues_pmsynth_descriptor)
set_source_files_properties(${TP}/flues/lv2/chatterbox_plugin.cpp PROPERTIES COMPILE_DEFINITIONS lv2_descriptor=flues_chatterbox_descriptor)
set_source_files_properties(${TP}/flues/lv2/disyn_plugin.cpp PROPERTIES COMPILE_DEFINITIONS lv2_descriptor=flues_disyn_descriptor)
set(GHOSTLAB_ROOM_INCLUDES Source/Matter/Hood Source/Matter/Engine ${JUCE_MODULES_DIR}/juce_audio_processors/format_types/LV2_SDK/lv2
  ${TP}/paulxstretch ${TP}/paulxstretch/PS_Source ${TP}/flues/pm-synth ${TP}/flues/pm-synth/modules ${TP}/flues/chatterbox ${TP}/flues/disyn)
set(GHOSTLAB_ROOM_WARN -Wno-shadow -Wno-sign-conversion -Wno-conversion
  -Wno-unused-parameter -Wno-implicit-float-conversion -Wno-shorten-64-to-32 -Wno-deprecated-declarations -Wno-unused-variable
  -Wno-missing-field-initializers -Wno-zero-as-null-pointer-constant -Wno-old-style-cast -Wno-unused-function -Wno-switch-enum)

# TERRAIN: the shared landscape control surface (ghosty-pro/Shared/Terrain, our own code)
include(${CMAKE_CURRENT_SOURCE_DIR}/../ghosty-pro/Shared/Terrain/Terrain.cmake)
set(GHOSTLAB_SOURCES
  ${GHOST_TERRAIN_SOURCES}
  Source/Core/Cdp.cpp Source/Core/AudioTools.cpp Source/Core/Actions.cpp Source/Core/Hood.cpp Source/Core/LabTerrainMap.cpp Source/Core/Engine.cpp
  Source/Core/Rooms.cpp
  Source/Plugin/Player.cpp Source/Plugin/Processor.cpp Source/Plugin/SoundBody.cpp Source/Plugin/Editor.cpp Source/Plugin/RoomPage.cpp
  ${GHOSTLAB_ROOM_SOURCES})
set(GHOSTLAB_DEFS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0 JUCE_USE_MP3AUDIOFORMAT=1
  GHOSTLAB_VERSION="${GHOSTLAB_VERSION}" PS_USE_PFFFT=1 GHOSTMATTER_DDSP=$<BOOL:${GHOSTLAB_DDSP}>)
if(APPLE)
  set(GHOSTLAB_FORMATS VST3 AU Standalone)
  set(GHOSTLAB_FX_FORMATS VST3 AU)
else()
  set(GHOSTLAB_FORMATS VST3 Standalone)
  set(GHOSTLAB_FX_FORMATS VST3)
endif()

function(ghostlab_target target)
  target_sources(${target} PRIVATE ${GHOSTLAB_SOURCES})
  add_dependencies(${target} gm_pertylizer_build)
  if(APPLE)
    target_link_libraries(${target} PRIVATE "-framework Accelerate")
  endif()
  target_include_directories(${target} PRIVATE Source ${GHOST_TERRAIN_DIR} ${GHOSTLAB_ROOM_INCLUDES})
  target_compile_definitions(${target} PUBLIC ${GHOSTLAB_DEFS} ${ARGN})
  target_link_libraries(${target} PRIVATE ${GM_DDSP_LIBS} gm_pertylizer ${GM_PERTY_SYS} gm_box2d gm_pffft GhostLabFonts juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags ${GHOSTLAB_LTO} juce::juce_recommended_warning_flags)
  if(NOT MSVC)
    target_compile_options(${target} PRIVATE -Wno-float-equal -Wno-shadow-field ${GHOSTLAB_ROOM_WARN})
  endif()
endfunction()

# the instrument (FL: Channel rack) + the standalone app
juce_add_plugin(GhostLab
  VERSION ${GHOSTLAB_VERSION}
  COMPANY_NAME "GhostBlock"
  BUNDLE_ID "io.ghostblock.ghostlab"
  PLUGIN_MANUFACTURER_CODE Ghbk
  PLUGIN_CODE GhLb
  FORMATS ${GHOSTLAB_FORMATS}
  PRODUCT_NAME "Ghost Lab"
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  COPY_PLUGIN_AFTER_BUILD FALSE
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  VST3_CATEGORIES Instrument Sampler)
ghostlab_target(GhostLab GHOSTLAB_FX=0)

# the effect (FL: Mixer insert): CAPTURE records the track's audio into Ghost Lab
juce_add_plugin(GhostLabFX
  VERSION ${GHOSTLAB_VERSION}
  COMPANY_NAME "GhostBlock"
  BUNDLE_ID "io.ghostblock.ghostlabfx"
  PLUGIN_MANUFACTURER_CODE Ghbk
  PLUGIN_CODE GhLf
  FORMATS ${GHOSTLAB_FX_FORMATS}
  PRODUCT_NAME "Ghost Lab FX"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS TRUE
  COPY_PLUGIN_AFTER_BUILD FALSE
  AU_MAIN_TYPE kAudioUnitType_Effect
  VST3_CATEGORIES Fx)
ghostlab_target(GhostLabFX GHOSTLAB_FX=1)

# tests + demo renders + screenshots (off by default; run under xvfb-run -a on Linux)
option(GHOSTLAB_BUILD_TESTS "Build the Ghost Lab test harness" OFF)
set(GHOSTLAB_CDP_DEV_DIR "" CACHE PATH "Where the CDP programs are for tests / dev builds (cdp/build-cdp.sh output)")
if(GHOSTLAB_BUILD_TESTS)
  juce_add_console_app(GhostLabTests PRODUCT_NAME "GhostLabTests")
  target_sources(GhostLabTests PRIVATE tests/GhostLabTests.cpp ${GHOSTLAB_SOURCES})
  add_dependencies(GhostLabTests gm_pertylizer_build)
  if(APPLE)
    target_link_libraries(GhostLabTests PRIVATE "-framework Accelerate")
  endif()
  target_include_directories(GhostLabTests PRIVATE Source ${GHOST_TERRAIN_DIR} ${GHOSTLAB_ROOM_INCLUDES})
  target_compile_definitions(GhostLabTests PRIVATE ${GHOSTLAB_DEFS} GHOSTLAB_FX=0 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1
    JucePlugin_Name="Ghost Lab" GHOSTLAB_CDP_DEV_DIR="${GHOSTLAB_CDP_DEV_DIR}")
  target_link_libraries(GhostLabTests PRIVATE ${GM_DDSP_LIBS} gm_pertylizer ${GM_PERTY_SYS} gm_box2d gm_pffft GhostLabFonts juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
  target_compile_options(GhostLabTests PRIVATE -Wno-float-equal -Wno-shadow-field ${GHOSTLAB_ROOM_WARN})
  # the same harness built as the effect: host-proxy tests for "Ghost Lab FX" (pass-through, latency, CAPTURE)
  juce_add_console_app(GhostLabFXTests PRODUCT_NAME "GhostLabFXTests")
  target_sources(GhostLabFXTests PRIVATE tests/GhostLabTests.cpp ${GHOSTLAB_SOURCES})
  add_dependencies(GhostLabFXTests gm_pertylizer_build)
  if(APPLE)
    target_link_libraries(GhostLabFXTests PRIVATE "-framework Accelerate")
  endif()
  target_include_directories(GhostLabFXTests PRIVATE Source ${GHOST_TERRAIN_DIR} ${GHOSTLAB_ROOM_INCLUDES})
  target_compile_definitions(GhostLabFXTests PRIVATE ${GHOSTLAB_DEFS} GHOSTLAB_FX=1 JUCE_STANDALONE_APPLICATION=1 JUCE_MODAL_LOOPS_PERMITTED=1
    JucePlugin_Name="Ghost Lab FX" GHOSTLAB_CDP_DEV_DIR="${GHOSTLAB_CDP_DEV_DIR}")
  target_link_libraries(GhostLabFXTests PRIVATE ${GM_DDSP_LIBS} gm_pertylizer ${GM_PERTY_SYS} gm_box2d gm_pffft GhostLabFonts juce::juce_audio_utils juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
  target_compile_options(GhostLabFXTests PRIVATE -Wno-float-equal -Wno-shadow-field ${GHOSTLAB_ROOM_WARN})
  # the shared TERRAIN component's own tests (model, mapping, saving, gestures, view cost, screenshots)
  juce_add_console_app(TerrainTests PRODUCT_NAME "TerrainTests")
  target_sources(TerrainTests PRIVATE ${GHOST_TERRAIN_DIR}/tests/TerrainTests.cpp ${GHOST_TERRAIN_SOURCES})
  target_include_directories(TerrainTests PRIVATE ${GHOST_TERRAIN_DIR})
  target_compile_definitions(TerrainTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_STANDALONE_APPLICATION=1)
  target_link_libraries(TerrainTests PRIVATE juce::juce_gui_basics juce::juce_data_structures
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
  target_compile_options(TerrainTests PRIVATE -Wno-float-equal -Wno-shadow-field ${GHOSTLAB_ROOM_WARN})
endif()
